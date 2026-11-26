#ifndef CETTA_OFFSIDE_LAYOUT_V1_H
#define CETTA_OFFSIDE_LAYOUT_V1_H

/* The offside rule of indented blocks, as the Python Language Reference
 * states it (Lexical analysis, "Line structure"):
 *   - A logical line ends at a line end outside every bracket pair that no
 *     line continuation escapes; the NEWLINE terminal follows it.
 *   - Lines inside a bracket pair join, and so does a line ending in the
 *     continuation text.
 *   - Blank lines, and lines holding only a comment, are no logical lines.
 *   - The indentation of a logical line is the width of its leading blanks:
 *     a space advances one column, a tab to the next multiple of 8, and a
 *     form feed restarts the count.  It is compared with a stack of
 *     indentations, starting at 0: a deeper line pushes its indentation and
 *     is preceded by INDENT; a shallower one pops every deeper level, each
 *     with a DEDENT, and must then equal the indentation on top.
 *   - The comparison must not depend on the width of a tab: counted with
 *     tabs one column wide, it must agree, or the indentation is
 *     inconsistent (Python's TabError).
 *   - At the end of the input the last logical line ends and every level
 *     above 0 is closed.
 * The resolver works on the lexed tokens and the text between them, so the
 * lexer needs no state.  Header-only, like native/bnfc_layout_v1.h, whose
 * token type it shares. */

#include "bnfc_layout_v1.h"

typedef struct {
    int32_t newline, indent, dedent;  /* the terminals it emits */
    const uint8_t *bracket;           /* per terminal: 1 opens a pair, 2 closes one */
    uint32_t n_terminals;
    const char *continuation;         /* or NULL */
    const char *const *line_comment;  /* line comment openers */
    uint16_t n_line_comments;
} CettaOffsideLayoutRulesV1;

/* What the layout does not hand to the parser, in text order: comments, and
   line ends that end no logical line.  A reader that reports every token of
   a text, as Python's tokenize does, needs them. */
enum { CETTA_OFFSIDE_COMMENT = 1, CETTA_OFFSIDE_NL = 2 };

typedef struct {
    uint8_t kind;
    uint32_t start, end;
} CettaOffsideTriviaV1;

typedef struct {
    CettaOffsideTriviaV1 *v;
    size_t n, cap;
} CettaOffsideTriviasV1;

static inline bool cetta_offside_layout_v1_trivia(CettaOffsideTriviasV1 *t, uint8_t kind, size_t a, size_t b) {
    if (!t) return true;
    if (t->n == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 256;
        CettaOffsideTriviaV1 *v = (CettaOffsideTriviaV1 *)realloc(t->v, nc * sizeof *v);
        if (!v) return false;
        t->v = v;
        t->cap = nc;
    }
    t->v[t->n].kind = kind;
    t->v[t->n].start = (uint32_t)a;
    t->v[t->n].end = (uint32_t)b;
    t->n++;
    return true;
}

/* the length of the line end at P (\n, \r\n or a lone \r), or 0 */
static inline size_t cetta_offside_layout_v1_eol(const uint8_t *s, size_t n, size_t p) {
    if (p >= n) return 0;
    if (s[p] == '\n') return 1;
    if (s[p] == '\r') return p + 1 < n && s[p + 1] == '\n' ? 2 : 1;
    return 0;
}

static inline bool cetta_offside_layout_v1_at(const uint8_t *s, size_t n, size_t p, const char *lit) {
    size_t k = lit ? strlen(lit) : 0;
    return k && p + k <= n && memcmp(s + p, lit, k) == 0;
}

/* line (from 1) and column (from 1, code points, a tab to the next multiple
   of 8) of byte P, from the line starts LS */
static inline void cetta_offside_layout_v1_place(const uint8_t *s, const size_t *ls, size_t nls, size_t p,
                                                 uint32_t *line, uint32_t *col) {
    size_t lo = 0, hi = nls - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (ls[mid] <= p) lo = mid; else hi = mid - 1;
    }
    uint32_t c = 1;
    for (size_t q = ls[lo]; q < p; q++) {
        if (s[q] == '\t') c = ((c - 1) / 8 + 1) * 8 + 1;
        else if ((s[q] & 0xC0) != 0x80) c++;
    }
    *line = (uint32_t)lo + 1;
    *col = c;
}

/* Resolve the tokens IN of the text S into OUT (owned by the caller), with
   the trivia into TRIVIA when it is given.  Returns false with a diagnosis. */
static inline bool cetta_offside_layout_v1_resolve(const CettaOffsideLayoutRulesV1 *r, const uint8_t *s,
                                                   size_t len, const CettaBnfcLayoutTokenV1 *in, size_t n,
                                                   CettaBnfcLayoutTokensV1 *out,
                                                   CettaOffsideTriviasV1 *trivia, char *err, size_t errsz) {
    size_t *ls = NULL, nls = 0, cap_ls = 0;
    uint32_t *col_stack = NULL, *alt_stack = NULL;
    size_t sp = 0, cap_stack = 0, depth = 0;
    bool ok = false;
#define OL_GROW(arr, cnt, cap, type_) do { \
        if ((cnt) == (cap)) { \
            size_t nc_ = (cap) ? (cap) * 2 : 64; \
            type_ *na_ = (type_ *)realloc((arr), nc_ * sizeof *na_); \
            if (!na_) goto ol_oom; \
            (arr) = na_; \
            (cap) = nc_; \
        } \
    } while (0)
#define OL_PUSH_LEVEL(c, a) do { \
        if (sp == cap_stack) { \
            size_t nc_ = cap_stack ? cap_stack * 2 : 64; \
            uint32_t *c_ = (uint32_t *)realloc(col_stack, nc_ * sizeof *c_); \
            if (!c_) goto ol_oom; \
            col_stack = c_; \
            uint32_t *a_ = (uint32_t *)realloc(alt_stack, nc_ * sizeof *a_); \
            if (!a_) goto ol_oom; \
            alt_stack = a_; \
            cap_stack = nc_; \
        } \
        col_stack[sp] = (c); \
        alt_stack[sp] = (a); \
        sp++; \
    } while (0)
#define OL_VIRTUAL(term_, a, b) do { \
        CettaBnfcLayoutTokenV1 v_; \
        memset(&v_, 0, sizeof v_); \
        v_.start = (uint32_t)(a); \
        v_.end = (uint32_t)(b); \
        v_.term = (term_); \
        v_.virt = true; \
        v_.extra = -1; \
        cetta_offside_layout_v1_place(s, ls, nls, (a), &v_.line, &v_.col); \
        if (!cetta_bnfc_layout_v1_push(out, v_)) goto ol_oom; \
    } while (0)
#define OL_FAIL(p, msg) do { \
        uint32_t l_, c_; \
        cetta_offside_layout_v1_place(s, ls, nls, (p), &l_, &c_); \
        if (err && errsz) snprintf(err, errsz, "line %u, column %u: %s", (unsigned)l_, (unsigned)c_, (msg)); \
        goto ol_done; \
    } while (0)

    /* line starts */
    OL_GROW(ls, nls, cap_ls, size_t);
    ls[nls++] = 0;
    for (size_t p = 0; p < len;) {
        size_t e = cetta_offside_layout_v1_eol(s, len, p);
        if (e) {
            p += e;
            OL_GROW(ls, nls, cap_ls, size_t);
            ls[nls++] = p;
        } else {
            p++;
        }
    }
    OL_PUSH_LEVEL(0u, 0u);

    for (size_t i = 0; i <= n; i++) {
        /* the text before token i (or after the last one) */
        size_t from = i ? in[i - 1].end : 0, to = i < n ? in[i].start : len;
        size_t ends = (size_t)-1; /* where the last logical line ends, if it does here */
        bool broke = false;
        for (size_t p = from; p < to;) {
            bool comment = false;
            for (uint16_t k = 0; k < r->n_line_comments && !comment; k++)
                comment = cetta_offside_layout_v1_at(s, len, p, r->line_comment[k]);
            if (comment) {
                size_t q = p;
                while (q < to && !cetta_offside_layout_v1_eol(s, len, q)) q++;
                if (!cetta_offside_layout_v1_trivia(trivia, CETTA_OFFSIDE_COMMENT, p, q)) goto ol_oom;
                p = q;
                continue;
            }
            if (r->continuation && cetta_offside_layout_v1_at(s, len, p, r->continuation)) {
                size_t e = cetta_offside_layout_v1_eol(s, len, p + strlen(r->continuation));
                if (e) { p += strlen(r->continuation) + e; continue; }
            }
            size_t e = cetta_offside_layout_v1_eol(s, len, p);
            if (e) {
                if (depth == 0 && i > 0 && ends == (size_t)-1) {
                    ends = p;
                } else if (!cetta_offside_layout_v1_trivia(trivia, CETTA_OFFSIDE_NL, p, p + e)) {
                    goto ol_oom;
                }
                if (depth == 0) broke = true;
                p += e;
                continue;
            }
            p++;
        }
        if (i == n) {
            /* the end of the input */
            if (n > 0) OL_VIRTUAL(r->newline, ends != (size_t)-1 ? ends : len, ends != (size_t)-1 ? ends : len);
            while (sp > 1) {
                OL_VIRTUAL(r->dedent, len, len);
                sp--;
            }
            break;
        }
        CettaBnfcLayoutTokenV1 t = in[i];
        if (depth == 0 && (i == 0 || broke)) {
            /* token i begins a logical line */
            if (i > 0) OL_VIRTUAL(r->newline, ends, ends);
            uint32_t ln, cl;
            cetta_offside_layout_v1_place(s, ls, nls, t.start, &ln, &cl);
            size_t bol = ls[ln - 1];
            uint32_t col = 0, alt = 0;
            for (size_t q = bol; q < t.start; q++) {
                if (s[q] == ' ') { col++; alt++; }
                else if (s[q] == '\t') { col = (col / 8 + 1) * 8; alt++; }
                else if (s[q] == '\f') { col = 0; alt = 0; }
            }
            uint32_t top = col_stack[sp - 1], alt_top = alt_stack[sp - 1];
            if (col == top) {
                if (alt != alt_top) OL_FAIL(t.start, "inconsistent use of tabs and spaces in indentation");
            } else if (col > top) {
                if (alt <= alt_top) OL_FAIL(t.start, "inconsistent use of tabs and spaces in indentation");
                OL_PUSH_LEVEL(col, alt);
                OL_VIRTUAL(r->indent, bol, t.start);
            } else {
                while (sp > 1 && col < col_stack[sp - 1]) {
                    OL_VIRTUAL(r->dedent, t.start, t.start);
                    sp--;
                }
                if (col != col_stack[sp - 1]) OL_FAIL(t.start, "unindent does not match any outer indentation level");
                if (alt != alt_stack[sp - 1]) OL_FAIL(t.start, "inconsistent use of tabs and spaces in indentation");
            }
        }
        if (!cetta_bnfc_layout_v1_push(out, t)) goto ol_oom;
        int kind = r->bracket && t.term >= 0 && (uint32_t)t.term < r->n_terminals ? r->bracket[t.term] : 0;
        if (kind == 1) depth++;
        else if (kind == 2 && depth > 0) depth--;
    }
    ok = true;
    goto ol_done;
ol_oom:
    if (err && errsz && !err[0]) snprintf(err, errsz, "out of memory resolving the layout");
ol_done:
#undef OL_GROW
#undef OL_PUSH_LEVEL
#undef OL_VIRTUAL
#undef OL_FAIL
    free(ls);
    free(col_stack);
    free(alt_stack);
    return ok;
}

#endif
