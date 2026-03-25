#include "putty.h"
#include "terminal.h"

void modalfatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

const char *const appname = "test_lineedit";

char *platform_default_s(const char *name)
{ return NULL; }
bool platform_default_b(const char *name, bool def)
{ return def; }
int platform_default_i(const char *name, int def)
{ return def; }
FontSpec *platform_default_fontspec(const char *name)
{ return fontspec_new_default(); }
Filename *platform_default_filename(const char *name)
{ return filename_from_str(""); }

const struct BackendVtable *const backends[] = { NULL };

typedef struct Mock {
    Terminal *term;
    Conf *conf;
    struct unicode_data ucsdata[1];
    strbuf *title;

    strbuf *context;

    bool any_test_failed;

    /*
     * clipboard_text stores the most recent OSC 52 payload written via
     * mock_clip_write, re-encoded as UTF-8 for easy comparison.
     * clipboard_id records which clipboard was targeted.
     */
    strbuf *clipboard_text;
    int    clipboard_id;

    TermWin tw;
} Mock;

static unsigned long mock_ticks;
static unsigned long mock_tickcount(Terminal *term)
{
    return mock_ticks;
}

static bool mock_setup_draw_ctx(TermWin *win) { return false; }
static void mock_draw_text(TermWin *win, int x, int y, wchar_t *text, int len,
                           unsigned long attrs, int lattrs, truecolour tc) {}
static void mock_draw_cursor(TermWin *win, int x, int y, wchar_t *text,
                             int len, unsigned long attrs, int lattrs,
                             truecolour tc) {}
static void mock_set_raw_mouse_mode(TermWin *win, bool enable) {}
static void mock_set_raw_mouse_mode_pointer(TermWin *win, bool enable) {}
static void mock_palette_set(TermWin *win, unsigned start, unsigned ncolours,
                             const rgb *colours) {}
static void mock_palette_get_overrides(TermWin *tw, Terminal *term) {}
static void mock_set_title(TermWin *win, const char *title, int codepage);
static void mock_set_icon_title(TermWin *win, const char *title, int cp) {}

static void mock_clip_write(TermWin *win, int clipboard, wchar_t *text,
                            int *attrs, truecolour *colours, int len,
                            bool deselect)
{
    Mock *mk = container_of(win, Mock, tw);
    strbuf_clear(mk->clipboard_text);
    mk->clipboard_id = clipboard;
    /*
     * Re-encode the wide string to UTF-8 so tests can compare against plain
     * string literals.  The bit masks below are standard UTF-8 encoding as
     * per RFC 3629: continuation bytes use 0x80 marker + 6 data bits (0x3F
     * mask); lead bytes use 0xC0/0xE0/0xF0 for 2/3/4-byte sequences.
     */
    for (int i = 0; i < len; i++) {
        unsigned long wc = text[i];
        if (wc == 0) break;  /* skip NUL terminator on SELECTION_NUL_TERMINATED platforms */
        if (wc < 0x80) {
            put_byte(mk->clipboard_text, (unsigned char)wc);
        } else if (wc < 0x800) {
            put_byte(mk->clipboard_text, 0xC0 | (wc >> 6));
            put_byte(mk->clipboard_text, 0x80 | (wc & 0x3F));
        } else if (wc < 0x10000) {
            put_byte(mk->clipboard_text, 0xE0 | (wc >> 12));
            put_byte(mk->clipboard_text, 0x80 | ((wc >> 6) & 0x3F));
            put_byte(mk->clipboard_text, 0x80 | (wc & 0x3F));
        } else {
            put_byte(mk->clipboard_text, 0xF0 | (wc >> 18));
            put_byte(mk->clipboard_text, 0x80 | ((wc >> 12) & 0x3F));
            put_byte(mk->clipboard_text, 0x80 | ((wc >> 6) & 0x3F));
            put_byte(mk->clipboard_text, 0x80 | (wc & 0x3F));
        }
    }
}

static void mock_clip_request_paste(TermWin *win, int clipboard) {}

static const TermWinVtable mock_termwin_vt = {
    .setup_draw_ctx = mock_setup_draw_ctx,
    .draw_text = mock_draw_text,
    .draw_cursor = mock_draw_cursor,
    .set_title = mock_set_title,
    .set_icon_title = mock_set_icon_title,
    .set_raw_mouse_mode = mock_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = mock_set_raw_mouse_mode_pointer,
    .palette_set = mock_palette_set,
    .palette_get_overrides = mock_palette_get_overrides,
    .clip_write = mock_clip_write,
    .clip_request_paste = mock_clip_request_paste,
};

static Mock *mock_new(void)
{
    Mock *mk = snew(Mock);
    memset(mk, 0, sizeof(*mk));

    mk->conf = conf_new();
    do_defaults(NULL, mk->conf);

    init_ucs_generic(mk->conf, mk->ucsdata);
    mk->ucsdata->line_codepage = CP_ISO8859_1;

    mk->context = strbuf_new();
    mk->title = strbuf_new();
    mk->clipboard_text = strbuf_new();
    mk->clipboard_id = -1;

    mk->tw.vt = &mock_termwin_vt;

    return mk;
}

static void mock_free(Mock *mk)
{
    strbuf_free(mk->context);
    conf_free(mk->conf);
    term_free(mk->term);
    strbuf_free(mk->title);
    strbuf_free(mk->clipboard_text);
    sfree(mk);
}

static void mock_set_title(TermWin *win, const char *title, int codepage)
{
    Mock *mk = container_of(win, Mock, tw);
    strbuf_clear(mk->title);
    put_dataz(mk->title, title);
}

static void reset(Mock *mk)
{
    term_pwron(mk->term, true);
    term_size(mk->term, 24, 80, 0);
    term_set_trust_status(mk->term, false);
    strbuf_clear(mk->context);
    strbuf_clear(mk->title);
    strbuf_clear(mk->clipboard_text);
    mk->clipboard_id = -1;
}

#if 0

static void test_context(Mock *mk, const char *fmt, ...)
{
    strbuf_clear(mk->context);
    va_list ap;
    va_start(ap, fmt);
    put_fmtv(mk->context, fmt, ap);
    va_end(ap);
}

#endif

static void report_fail(Mock *mk, const char *file, int line,
                        const char *fmt, ...)
{
    printf("%s:%d", file, line);
    if (mk->context->len)
        printf(" (%s)", mk->context->s);
    printf(": ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    mk->any_test_failed = true;
}

static inline void check_iequal(Mock *mk, const char *file, int line,
                                long long lhs, long long rhs)
{
    if (lhs != rhs)
        report_fail(mk, file, line, "%lld != %lld / %#llx != %#llx",
                    lhs, rhs, lhs, rhs);
}

static inline void check_sequal(Mock *mk, const char *file, int line,
                                const char *lhs, const char *rhs)
{
    if (strcmp(lhs, rhs) != 0)
        report_fail(mk, file, line, "'%s' != '%s'", lhs, rhs);
}

#define IEQUAL(lhs, rhs) check_iequal(mk, __FILE__, __LINE__, lhs, rhs)
#define SEQUAL(lhs, rhs) check_sequal(mk, __FILE__, __LINE__, lhs, rhs)

static inline void term_datapl(Terminal *term, ptrlen pl)
{
    term_data(term, pl.ptr, pl.len);
}

static struct termchar get_termchar(Terminal *term, int x, int y)
{
    termline *tl = term_get_line(term, y);
    termchar tc;
    if (0 <= x && x < tl->cols)
        tc = tl->chars[x];
    else
        tc = term->erase_char;
    term_release_line(tl);
    return tc;
}

static unsigned short get_lineattr(Terminal *term, int y)
{
    termline *tl = term_get_line(term, y);
    unsigned short lattr = tl->lattr;
    term_release_line(tl);
    return lattr;
}

static void test_hello_world(Mock *mk)
{
    /* A trivial test just to kick off this test framework */
    mk->ucsdata->line_codepage = CP_ISO8859_1;

    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("hello, world"));
    IEQUAL(mk->term->curs.x, 12);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(get_termchar(mk->term, 0, 0).chr, CSET_ASCII | 'h');
    IEQUAL(get_termchar(mk->term, 1, 0).chr, CSET_ASCII | 'e');
    IEQUAL(get_termchar(mk->term, 2, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 3, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 4, 0).chr, CSET_ASCII | 'o');
    IEQUAL(get_termchar(mk->term, 5, 0).chr, CSET_ASCII | ',');
    IEQUAL(get_termchar(mk->term, 6, 0).chr, CSET_ASCII | ' ');
    IEQUAL(get_termchar(mk->term, 7, 0).chr, CSET_ASCII | 'w');
    IEQUAL(get_termchar(mk->term, 8, 0).chr, CSET_ASCII | 'o');
    IEQUAL(get_termchar(mk->term, 9, 0).chr, CSET_ASCII | 'r');
    IEQUAL(get_termchar(mk->term, 10, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 11, 0).chr, CSET_ASCII | 'd');
}

static void test_wrap(Mock *mk)
{
    /* Test behaviour when printing characters wrap to the next line */
    mk->ucsdata->line_codepage = CP_UTF8;

    /* Print 'abc' without enough space for the c, in wrapping mode */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* The 'a' prints without anything unusual happening */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    /* The 'b' prints, leaving the cursor where it is with wrapnext set */
    term_datapl(mk->term, PTRLEN_LITERAL("b"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'b');
    /* And now the 'c' causes a deferred wrap and goes to the next line */
    term_datapl(mk->term, PTRLEN_LITERAL("c"));
    IEQUAL(mk->term->curs.x, 1);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), LATTR_WRAPPED);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'b');
    IEQUAL(get_termchar(mk->term, 0, 1).chr, CSET_ASCII | 'c');
    /* If we backspace once, the cursor moves back on to the c */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    /* Now backspace again, and the cursor returns to the b */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* Now try it with a double-width character in place of ab */
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* The DW character goes directly to the wrapnext state */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, 0xAC00);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, UCSWIDE);
    /* And the 'c' causes a deferred wrap as before */
    term_datapl(mk->term, PTRLEN_LITERAL("c"));
    IEQUAL(mk->term->curs.x, 1);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), LATTR_WRAPPED);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, 0xAC00);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, UCSWIDE);
    IEQUAL(get_termchar(mk->term, 0, 1).chr, CSET_ASCII | 'c');
    /* If we backspace once, the cursor moves back on to the c */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    /* Now backspace again, and the cursor goes to the RHS of the DW char */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* Now put the DW character in place of bc */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* The 'a' prints as before */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    /* The DW character wraps, setting LATTR_WRAPPED2 */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 2);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), LATTR_WRAPPED | LATTR_WRAPPED2);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | ' ');
    IEQUAL(get_termchar(mk->term, 0, 1).chr, 0xAC00);
    IEQUAL(get_termchar(mk->term, 1, 1).chr, UCSWIDE);
    /* If we backspace once, cursor moves to the RHS of the DW char */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 1);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    /* Backspace again, and cursor moves from RHS to LHS of that char */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    /* Now backspace again, and the cursor skips the empty column so
     * that it can return to the previous logical character, to wit, the a */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 78);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* Print 'ab' up to the rightmost column, and then backspace */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* As before, the 'ab' put us in the rightmost column with wrapnext set */
    term_datapl(mk->term, PTRLEN_LITERAL("ab"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'b');
    /* Backspacing just clears the wrapnext flag, so we're logically
     * back on the b again */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* For completeness, the easy case: just print 'a' then backspace */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* 'a' printed in column n-1 takes us to column n */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    /* Backspacing moves us back a space on to the a */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 78);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /*
     * Now test the special cases that arise when the terminal is only
     * one column wide!
     */

    reset(mk);
    term_size(mk->term, 24, 1, 0);
    mk->term->curs.x = 0;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* Printing a single-width character takes us into wrapnext immediately */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 0, 0).chr, CSET_ASCII | 'a');
    /* Printing a second one wraps, and takes us _back_ to wrapnext */
    term_datapl(mk->term, PTRLEN_LITERAL("b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_lineattr(mk->term, 0), LATTR_WRAPPED);
    IEQUAL(get_termchar(mk->term, 0, 0).chr, CSET_ASCII | 'a');
    IEQUAL(get_termchar(mk->term, 0, 1).chr, CSET_ASCII | 'b');
    /* Backspacing once clears the wrapnext flag, putting us on the b */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 1);
    IEQUAL(mk->term->wrapnext, 0);
    /* Backspacing again returns to the previous line, putting us on the a */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* And now try with a double-width character */
    reset(mk);
    term_size(mk->term, 24, 1, 0);
    mk->term->curs.x = 0;
    mk->term->curs.y = 0;
    mk->term->wrap = true;
    /* DW character won't fit at all, so it transforms into U+FFFD
     * REPLACEMENT CHARACTER and then behaves like a SW char */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 0);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 1);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 0, 0).chr, 0xFFFD);
}

static void test_nonwrap(Mock *mk)
{
    /* Test behaviour when printing characters hit end of line without wrap.
     * The wrapnext flag is never set in this mode. */
    mk->ucsdata->line_codepage = CP_UTF8;

    /* Print 'abc' without enough space for the c */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = false;
    /* The 'a' prints without anything unusual happening */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    /* The 'b' prints, leaving the cursor where it is */
    term_datapl(mk->term, PTRLEN_LITERAL("b"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'b');
    /* The 'c' overwrites the b */
    term_datapl(mk->term, PTRLEN_LITERAL("c"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'c');
    /* Since wrapnext was never set, backspacing returns us to the a */
    term_datapl(mk->term, PTRLEN_LITERAL("\b"));
    IEQUAL(mk->term->curs.x, 78);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);

    /* Now try it with a double-width character in place of ab */
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = false;
    /* The DW character occupies the rightmost two columns */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, 0xAC00);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, UCSWIDE);
    /* The 'c' must overprint the RHS of the DW char, clearing the LHS */
    term_datapl(mk->term, PTRLEN_LITERAL("c"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | ' ');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, CSET_ASCII | 'c');

    /* Now put the DW char in place of the bc */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = false;
    /* The 'a' prints as before */
    term_datapl(mk->term, PTRLEN_LITERAL("a"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    /* The DW char won't fit, so turns into U+FFFD REPLACEMENT CHARACTER */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | 'a');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, 0xFFFD);

    /* Just for completeness, try both of those together */
    reset(mk);
    mk->term->curs.x = 78;
    mk->term->curs.y = 0;
    mk->term->wrap = false;
    /* First DW character occupies the rightmost columns */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x80"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, 0xAC00);
    IEQUAL(get_termchar(mk->term, 79, 0).chr, UCSWIDE);
    /* Second DW char becomes U+FFFD, overwriting RHS of the first one */
    term_datapl(mk->term, PTRLEN_LITERAL("\xEA\xB0\x81"));
    IEQUAL(mk->term->curs.x, 79);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(mk->term->wrapnext, 0);
    IEQUAL(get_lineattr(mk->term, 0), 0);
    IEQUAL(get_termchar(mk->term, 78, 0).chr, CSET_ASCII | ' ');
    IEQUAL(get_termchar(mk->term, 79, 0).chr, 0xFFFD);
}

static void test_wintitle(Mock *mk)
{
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]0;foo\033\\"));
    term_update(mk->term);
    SEQUAL(mk->title->s, "foo");
    term_datapl(mk->term, PTRLEN_LITERAL("\033]0;bar\007"));
    term_update(mk->term);
    SEQUAL(mk->title->s, "bar");

    /* Regression test for a bug in which a DCS string was
     * accidentally treated as a title-setting OSC 0. We expect that
     * this is not a window-title setting sequence, and leaves the
     * title unchanged. */
    term_datapl(mk->term, PTRLEN_LITERAL("\033Pzz\033\\"));
    term_update(mk->term);
    SEQUAL(mk->title->s, "bar");
}

/*
 * base64("hello") = "aGVsbG8="
 * base64("world") = "d29ybGQ="
 * base64("a\x01b") = "YQFi"  (contains ASCII control char 0x01)
 * base64("a\tb")   = "YQli"  (tab, which we keep)
 */
static void test_osc52(Mock *mk)
{
    /* --- Happy path: empty selector routes to default clipboard --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "hello");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- Empty payload clears clipboard --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- 'c' selector routes to CLIP_CLIPBOARD, ST terminator --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;c;d29ybGQ=\033\\"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "world");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- 's' selector behaves like 'c' (also CLIP_CLIPBOARD) --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;s;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "hello");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- 'p' selector routes to CLIP_PRIMARY on Unix --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;p;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "hello");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_PRIMARY);
#endif

    /* --- Comma-separated: first valid token 'c' wins --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;x,c,s;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "hello");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- Query payload '?' silently ignored; clipboard_id stays -1 --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;?\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
    IEQUAL(mk->clipboard_id, -1);

    /* --- Invalid base64 payload ignored; clipboard_id stays -1 --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;not!base64\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
    IEQUAL(mk->clipboard_id, -1);

    /* --- Max-size payload accepted (clipboard_id is set) --- */
    reset(mk);
    {
        strbuf *payload = strbuf_new();
        strbuf *seq = strbuf_new();
        for (size_t i = 0; i < OSC52_B64_MAX; i++)
            put_byte(payload, 'A');
        put_fmt(seq, "\033]52;;%s\007", payload->s);
        term_datapl(mk->term, ptrlen_from_strbuf(seq));
        term_update(mk->term);
#ifdef PLATFORM_IS_UTF16
        IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
        IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif
        strbuf_free(seq);
        strbuf_free(payload);
    }

    /* --- Missing ';' separator ignored; clipboard_id stays -1 --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
    IEQUAL(mk->clipboard_id, -1);

    /* --- Unknown-only selectors ignored; clipboard_id stays -1 --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;z;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
    IEQUAL(mk->clipboard_id, -1);

    /* --- Sanitization: control char 0x01 stripped, tab preserved --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;YQFi\007")); /* "a\x01b" */
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "ab");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;YQli\007")); /* "a\tb" */
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "a\tb");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- Sanitization: DEL and C1 controls stripped --- */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;YX9i\007")); /* "a\x7fb" */
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "ab");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;YcKbYg==\007")); /* "a\u009bb" */
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "ab");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- Sanitization: newlines must be preserved (multi-line copy) ---
     * base64("line1\nline2") = "bGluZTEKbGluZTI="
     */
    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;bGluZTEKbGluZTI=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "line1\nline2");
#ifdef PLATFORM_IS_UTF16
    IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
    IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif

    /* --- Rate limit: only first N writes in window are accepted --- */
    reset(mk);
    {
        static const char *const b64s[] = {
            "YQ==", "Yg==", "Yw==", "ZA==", "ZQ==", "Zg=="
        };
        static const char *const dec[] = {
            "a", "b", "c", "d", "e", "f"
        };
        strbuf *seq = strbuf_new();
        size_t count = OSC52_RATE_MAX_WRITES + 1;
        mk->term->get_tickcount = mock_tickcount;
        mock_ticks = 1000;
        mk->term->osc52_window_start = mock_ticks;
        mk->term->osc52_writes_in_window = 0;
        for (size_t i = 0; i < count; i++) {
            strbuf_clear(seq);
            put_fmt(seq, "\033]52;;%s\007", b64s[i]);
            term_datapl(mk->term, ptrlen_from_strbuf(seq));
            term_update(mk->term);
        }
        SEQUAL(mk->clipboard_text->s, dec[OSC52_RATE_MAX_WRITES - 1]);
#ifdef PLATFORM_IS_UTF16
        IEQUAL(mk->clipboard_id, CLIP_SYSTEM);
#else
        IEQUAL(mk->clipboard_id, CLIP_CLIPBOARD);
#endif
        mk->term->get_tickcount = NULL;
        strbuf_free(seq);
    }

    /* --- no_osc52=true suppresses all writes; clipboard_id stays -1 --- */
    reset(mk);
    conf_set_bool(mk->conf, CONF_no_osc52, true);
    term_reconfig(mk->term, mk->conf);
    term_datapl(mk->term, PTRLEN_LITERAL("\033]52;;aGVsbG8=\007"));
    term_update(mk->term);
    SEQUAL(mk->clipboard_text->s, "");
    IEQUAL(mk->clipboard_id, -1);

    /* Restore default for subsequent tests. */
    conf_set_bool(mk->conf, CONF_no_osc52, false);
    term_reconfig(mk->term, mk->conf);
}

int main(void)
{
    Mock *mk = mock_new();
    mk->term = term_init(mk->conf, mk->ucsdata, &mk->tw);

    test_hello_world(mk);
    test_wrap(mk);
    test_nonwrap(mk);
    test_wintitle(mk);
    test_osc52(mk);

    bool failed = mk->any_test_failed;
    mock_free(mk);

    if (failed) {
        printf("Test suite FAILED!\n");
        return 1;
    } else {
        printf("Test suite passed\n");
        return 0;
    }
}
