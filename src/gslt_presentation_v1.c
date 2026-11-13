/* Presentations of MeTTa atoms compiled at build time from GSLT modules.
 * See gslt_presentation_v1.h and tools/gslt_presentation_compile_v1.c. */

#include "gslt_presentation_v1.h"

#include "atom.h"
#include "parser.h"
#include "petta_compiled_reader.h"
#include "native/bnfc_layout_v1.h"
#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* every presentation of PRESENTATIONS in the Makefile, with its tables */
#include "generated/gslt_presentation_registry_v1.generated.h"

const GSLTPresentationTablesV1 *gslt_presentation_v1_find(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof gslt_presentation_registry / sizeof gslt_presentation_registry[0]; i++)
        if (strcmp(gslt_presentation_registry[i]->name, name) == 0) return gslt_presentation_registry[i];
    return NULL;
}

const char *gslt_presentation_v1_names(void) {
    return gslt_presentation_registry_names;
}

const GSLTPresentationTablesV1 *gslt_presentation_v1_at(size_t i) {
    return i < sizeof gslt_presentation_registry / sizeof gslt_presentation_registry[0]
               ? gslt_presentation_registry[i] : NULL;
}

static void set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    if (!buf || !size || buf[0]) return; /* keep the first diagnosis */
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------ grounding */

typedef struct {
    GSLTPresentationGroundingKindV1 kind;
    ParserHostProjectionV1 *host;
    PeTTaGroundingV1 *petta;
} Ground;

static bool ground_open(Ground *g, GSLTPresentationGroundingKindV1 kind, TermUniverse *u) {
    memset(g, 0, sizeof *g);
    g->kind = kind;
    if (kind == GSLT_PRES_GROUNDING_PETTA) {
        g->petta = petta_grounding_v1_new(u);
        return g->petta != NULL;
    }
    g->host = parser_host_projection_v1_new(u);
    return g->host != NULL;
}

static void ground_close(Ground *g) {
    if (g->petta) petta_grounding_v1_free(g->petta);
    if (g->host) parser_host_projection_v1_free(g->host);
    memset(g, 0, sizeof *g);
}

static bool ground_begin(Ground *g) {
    return g->petta ? petta_grounding_v1_begin_form(g->petta)
                    : parser_host_projection_v1_begin_form(g->host);
}

static AtomId ground_word(Ground *g, const uint8_t *b, size_t n) {
    return g->petta ? petta_grounding_v1_word(g->petta, b, n)
                    : parser_host_projection_v1_word_bytes(g->host, b, n);
}

static AtomId ground_variable(Ground *g, const uint8_t *b, size_t n) {
    if (g->petta) return petta_grounding_v1_variable(g->petta, b, n);
    if (n == 0) return parser_host_projection_v1_word_bytes(g->host, (const uint8_t *)"$", 1u);
    if (n == 1 && b[0] == '_') return parser_host_projection_v1_anonymous_variable(g->host);
    return parser_host_projection_v1_variable_bytes(g->host, b, n);
}

static AtomId ground_string(Ground *g, const uint8_t *b, size_t n) {
    return g->petta ? petta_grounding_v1_string(g->petta, b, n)
                    : parser_host_projection_v1_string_bytes(g->host, b, n);
}

static AtomId ground_expression(Ground *g, const AtomId *kids, size_t n) {
    return g->petta ? petta_grounding_v1_expression(g->petta, kids, n)
                    : parser_host_projection_v1_expression(g->host, kids, (CettaExprLen)n);
}

/* ------------------------------------------------------------ lexing */

typedef struct {
    uint32_t start, end;
    int term;
    int raw_term; /* the token a reserved word was lexed as, or -1 */
    bool virt;    /* inserted by layout: no text */
} Tok;

/* the comments a reading skipped: their text spans, in order */
typedef struct {
    uint32_t *start, *end;
    size_t n, cap;
} CommentCap;

static bool comment_cap_push(CommentCap *c, uint32_t a, uint32_t b) {
    if (!c) return true;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 32;
        uint32_t *s = realloc(c->start, nc * sizeof *s);
        if (!s) return false;
        c->start = s;
        uint32_t *e = realloc(c->end, nc * sizeof *e);
        if (!e) return false;
        c->end = e;
        c->cap = nc;
    }
    c->start[c->n] = a;
    c->end[c->n] = b;
    c->n++;
    return true;
}

static size_t utf8_decode(const uint8_t *s, size_t n, size_t i, uint32_t *cp) {
    uint8_t c = s[i];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0 && i + 1 < n && (s[i + 1] & 0xC0) == 0x80) {
        *cp = (uint32_t)(c & 0x1F) << 6 | (s[i + 1] & 0x3F);
        return *cp >= 0x80 ? 2 : 0;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < n && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
        *cp = (uint32_t)(c & 0x0F) << 12 | (uint32_t)(s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F);
        return *cp >= 0x800 && (*cp < 0xD800 || *cp > 0xDFFF) ? 3 : 0;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < n && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 &&
        (s[i + 3] & 0xC0) == 0x80) {
        *cp = (uint32_t)(c & 0x07) << 18 | (uint32_t)(s[i + 1] & 0x3F) << 12 | (uint32_t)(s[i + 2] & 0x3F) << 6 |
              (s[i + 3] & 0x3F);
        return *cp >= 0x10000 && *cp < 0x110000 ? 4 : 0;
    }
    return 0;
}

static int class_of(const GSLTPresentationTablesV1 *tb, uint32_t cp) {
    if (cp < 256) return tb->class_of_low[cp];
    int lo = 0, hi = (int)tb->n_high - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < tb->high_lo[mid]) hi = mid - 1;
        else if (cp > tb->high_hi[mid]) lo = mid + 1;
        else return tb->high_class[mid];
    }
    return -1;
}

static void line_col(const uint8_t *s, size_t pos, unsigned *line, unsigned *col) {
    unsigned l = 1, c = 1;
    for (size_t i = 0; i < pos; i++) {
        if (s[i] == '\n') { l++; c = 1; } else if ((s[i] & 0xC0) != 0x80) c++;
    }
    *line = l;
    *col = c;
}

static bool starts_with(const uint8_t *s, size_t n, size_t i, const char *lit) {
    size_t k = strlen(lit);
    return k && i + k <= n && memcmp(s + i, lit, k) == 0;
}

/* The terminal a lexeme denotes when it is a reserved word, or -1. */
static int reserved_terminal(const GSLTPresentationTablesV1 *tb, const uint8_t *s, size_t len) {
    for (uint16_t r = 0; r < tb->n_reserved; r++) {
        const char *w = tb->reserved_text[r];
        if (strlen(w) == len && memcmp(w, s, len) == 0) return tb->reserved_terminal[r];
    }
    return -1;
}

/* 1 a token, 0 end of input, -1 an error; CAP, when given, receives the
   text spans of the comments skipped */
static int lex_next_cap(const GSLTPresentationTablesV1 *tb, const uint8_t *s, size_t n, size_t *pos, Tok *tok,
                        char *err, size_t errsz, CommentCap *cap) {
    size_t i = *pos;
    for (;;) {
        if (i >= n) { *pos = i; return 0; }
        bool skipped = false;
        for (uint16_t k = 0; k < tb->n_line_comments && !skipped; k++) {
            if (starts_with(s, n, i, tb->line_comment[k])) {
                size_t body = i + strlen(tb->line_comment[k]);
                while (i < n && s[i] != '\n') i++;
                if (!comment_cap_push(cap, (uint32_t)body, (uint32_t)i)) return -1;
                skipped = true;
            }
        }
        for (uint16_t k = 0; k < tb->n_block_comments && !skipped; k++) {
            if (starts_with(s, n, i, tb->block_open[k])) {
                size_t j = i + strlen(tb->block_open[k]);
                while (j < n && !starts_with(s, n, j, tb->block_close[k])) j++;
                if (j >= n) {
                    unsigned l, c;
                    line_col(s, i, &l, &c);
                    set_error(err, errsz, "line %u, column %u: unterminated comment", l, c);
                    return -1;
                }
                if (!comment_cap_push(cap, (uint32_t)(i + strlen(tb->block_open[k])), (uint32_t)j)) return -1;
                i = j + strlen(tb->block_close[k]);
                skipped = true;
            }
        }
        if (skipped) continue;
        int st = 0, last_rule = -1;
        size_t j = i, last_end = i;
        while (j < n) {
            uint32_t cp;
            size_t w = utf8_decode(s, n, j, &cp);
            if (!w) break;
            int c = class_of(tb, cp);
            if (c < 0) break;
            int nx = tb->dfa_next[(size_t)st * tb->n_classes + (size_t)c];
            if (nx < 0) break;
            st = nx;
            j += w;
            if (tb->dfa_accept[st] >= 0) { last_rule = tb->dfa_accept[st]; last_end = j; }
        }
        if (last_rule < 0) {
            unsigned l, c;
            line_col(s, i, &l, &c);
            uint32_t cp = 0;
            size_t w = utf8_decode(s, n, i, &cp);
            if (!w) set_error(err, errsz, "line %u, column %u: invalid UTF-8", l, c);
            else set_error(err, errsz, "line %u, column %u: no token begins with '%.*s'", l, c, (int)w, s + i);
            return -1;
        }
        if (tb->rule_kind[last_rule] == GSLT_PRES_RULE_TRIVIA) { i = last_end; continue; }
        tok->start = (uint32_t)i;
        tok->end = (uint32_t)last_end;
        tok->raw_term = -1;
        tok->virt = false;
        if (tb->rule_kind[last_rule] == GSLT_PRES_RULE_SYMBOL) {
            tok->term = tb->rule_terminal[last_rule];
        } else {
            int r = reserved_terminal(tb, s + i, last_end - i);
            tok->term = r >= 0 ? r : tb->rule_terminal[last_rule];
            if (r >= 0 && tb->terminal_kind[r] == GSLT_PRES_TERM_KEYWORD) tok->raw_term = tb->rule_terminal[last_rule];
        }
        *pos = last_end;
        return 1;
    }
}

static int lex_next(const GSLTPresentationTablesV1 *tb, const uint8_t *s, size_t n, size_t *pos, Tok *tok,
                    char *err, size_t errsz) {
    return lex_next_cap(tb, s, n, pos, tok, err, errsz, NULL);
}

/* ------------------------------------------------------------ layout */

/* A token with its position (BNFC's layout works on lines and columns); an
   inserted token has no text; `extra` carries the token a keyword was
   lexed as. */
typedef CettaBnfcLayoutTokenV1 LTok;
typedef CettaBnfcLayoutTokensV1 LToks;

/* Every token, with its line and column (a tab advances to the next
   multiple of 8, as BNFC's lexer counts). */
static int lex_all(const GSLTPresentationTablesV1 *tb, const uint8_t *s, size_t n, LToks *out, char *err,
                   size_t errsz, CommentCap *cap) {
    size_t pos = 0;
    for (;;) {
        Tok tok;
        int r = lex_next_cap(tb, s, n, &pos, &tok, err, errsz, cap);
        if (r < 0) return -1;
        if (r == 0) break;
        LTok k = {tok.start, tok.end, tok.term, 0, 0, false, tok.raw_term};
        if (!cetta_bnfc_layout_v1_push(out, k)) return -1;
    }
    cetta_bnfc_layout_v1_positions(s, n, out->v, out->n);
    return 0;
}

/* BNFC's layout resolution, shared with the run-time token route
   (native/bnfc_layout_v1.h). */
static int resolve_layout(const GSLTPresentationTablesV1 *tb, const LToks *in, LToks *out, char *err,
                          size_t errsz) {
    CettaBnfcLayoutRulesV1 rules = {tb->layout_open, tb->layout_sep, tb->layout_close, tb->layout_toplevel,
                                    tb->terminal_layout, tb->n_terminals};
    char lerr[256] = "";
    if (!cetta_bnfc_layout_v1_resolve(&rules, in->v, in->n, out, lerr, sizeof lerr)) {
        set_error(err, errsz, "%s", lerr);
        return -1;
    }
    return 0;
}

/* The parser's tokens: lexed as it goes, or resolved for layout first. */
typedef struct {
    const GSLTPresentationTablesV1 *tb;
    const uint8_t *s;
    size_t n, pos;
    const LToks *arr;
    size_t idx;
    CommentCap *cap;
} TokSrc;

static int src_next(TokSrc *ts, Tok *tok, char *err, size_t errsz) {
    if (ts->arr) {
        if (ts->idx >= ts->arr->n) return 0;
        const LTok *k = &ts->arr->v[ts->idx++];
        tok->start = k->start;
        tok->end = k->end;
        tok->term = k->term;
        tok->raw_term = k->extra;
        tok->virt = k->virt;
        return 1;
    }
    return lex_next_cap(ts->tb, ts->s, ts->n, &ts->pos, tok, err, errsz, ts->cap);
}

/* the terminal of TEXT when it lexes as exactly one token, else -1; with RAW,
   the token a keyword was lexed as (a keyword is a name where the grammar
   cannot take the keyword) */
static int lex_single_as(const GSLTPresentationTablesV1 *tb, const char *text, size_t len, bool raw) {
    size_t pos = 0;
    Tok tok;
    char err[8] = "";
    if (lex_next(tb, (const uint8_t *)text, len, &pos, &tok, err, sizeof err) != 1) return -1;
    if (tok.start != 0 || tok.end != len) return -1;
    return raw && tok.raw_term >= 0 ? tok.raw_term : tok.term;
}

static int lex_single(const GSLTPresentationTablesV1 *tb, const char *text, size_t len) {
    return lex_single_as(tb, text, len, false);
}

/* Whether the parser could shift TERM from the state stack, after the
   reductions it would make first: an LALR(1) table may reduce on a
   terminal it later rejects, so the first action alone does not tell. */
static bool lr_viable(const GSLTPresentationTablesV1 *tb, const int *states, size_t sp, int term) {
    int extra[256];
    int ne = 0;
    size_t k = sp;
    for (int guard = 0; guard < 100000; guard++) {
        int st = ne ? extra[ne - 1] : states[k - 1];
        int16_t act = tb->action[(size_t)st * tb->n_terminals + (size_t)term];
        if (act == 0) return false;
        if (act > 0) return true; /* a shift, or the accepting action */
        int p = -act - 1;
        int len = tb->prod_len[p];
        while (len > 0) {
            if (ne) ne--;
            else if (k > 1) k--;
            else return false;
            len--;
        }
        int top = ne ? extra[ne - 1] : states[k - 1];
        int to = tb->goto_[(size_t)top * tb->n_nonterminals + (size_t)tb->prod_lhs[p]];
        if (to < 0 || ne == 256) return false;
        extra[ne++] = to;
    }
    return false;
}

/* ------------------------------------------------------------ reading */

enum { V_NONE = 0, V_ATOM, V_LIST, V_RUN, V_TOKEN };

typedef struct {
    uint8_t kind;
    AtomId id;           /* V_ATOM, V_RUN */
    uint32_t head, len;  /* V_LIST: first cell, length; V_TOKEN: start, end */
    uint32_t pos;        /* where its text begins, or UINT32_MAX */
    uint32_t node;       /* its node in a recorded parse tree (the lint) */
} Val;

/* The parse tree a reading records for the lint: a node per token and per
   reduction, with its production, its children and its atom. */
typedef struct {
    int16_t prod;        /* -1 for a token */
    uint32_t first_kid, n_kids;
    AtomId atom;         /* the node's atom, or CETTA_ATOM_ID_NONE */
    uint32_t pos;
} LNode;

typedef struct {
    LNode *nodes;
    uint32_t n, cap;
    uint32_t *kids;
    uint32_t nk, capk;
    uint32_t root;
} LintTree;

static bool lint_node(LintTree *lt, int16_t prod, const Val *kids, int n_kids, AtomId atom, uint32_t pos,
                      uint32_t *out) {
    if (lt->n == lt->cap) {
        uint32_t nc = lt->cap ? lt->cap * 2u : 1024u;
        LNode *nn = realloc(lt->nodes, (size_t)nc * sizeof *nn);
        if (!nn) return false;
        lt->nodes = nn;
        lt->cap = nc;
    }
    while (lt->nk + (uint32_t)n_kids > lt->capk) {
        uint32_t nc = lt->capk ? lt->capk * 2u : 1024u;
        uint32_t *nk = realloc(lt->kids, (size_t)nc * sizeof *nk);
        if (!nk) return false;
        lt->kids = nk;
        lt->capk = nc;
    }
    LNode *node = &lt->nodes[lt->n];
    node->prod = prod;
    node->first_kid = lt->nk;
    node->n_kids = (uint32_t)n_kids;
    node->atom = atom;
    node->pos = pos;
    for (int k = 0; k < n_kids; k++) lt->kids[lt->nk++] = kids[k].node;
    *out = lt->n++;
    return true;
}

typedef struct {
    Val v;
    uint32_t next;
} Cell;

#define NIL UINT32_MAX

typedef struct {
    const GSLTPresentationTablesV1 *tb;
    TermUniverse *u;
    Ground g;
    const uint8_t *src;
    size_t src_len;
    Cell *cells;
    uint32_t n_cells, cap_cells;
    AtomId *tmp;
    size_t n_tmp, cap_tmp;
    AtomId *consts;
    char *err;
    size_t errsz;
    char *buf; /* token text scratch */
    size_t cap_buf;
    /* channel scopes: the channels each category named, and per open
       scope the heights its channels start at */
    AtomId *chs[GSLT_PRES_MAX_CHANNELS];
    size_t ch_len[GSLT_PRES_MAX_CHANNELS], ch_cap[GSLT_PRES_MAX_CHANNELS];
    size_t (*scopes)[GSLT_PRES_MAX_CHANNELS];
    size_t n_scopes, cap_scopes;
    uint32_t scope_pos;
} Rd;

static bool scope_open(Rd *r) {
    if (r->n_scopes == r->cap_scopes) {
        size_t nc = r->cap_scopes ? r->cap_scopes * 2 : 8;
        size_t (*s)[GSLT_PRES_MAX_CHANNELS] = realloc(r->scopes, nc * sizeof *s);
        if (!s) return false;
        r->scopes = s;
        r->cap_scopes = nc;
    }
    for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) r->scopes[r->n_scopes][k] = r->ch_len[k];
    r->n_scopes++;
    return true;
}

static void scope_close(Rd *r) {
    if (!r->n_scopes) return;
    r->n_scopes--;
    for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) r->ch_len[k] = r->scopes[r->n_scopes][k];
}

static bool channel_push(Rd *r, int k, AtomId id) {
    if (r->ch_len[k] == r->ch_cap[k]) {
        size_t nc = r->ch_cap[k] ? r->ch_cap[k] * 2 : 16;
        AtomId *c = realloc(r->chs[k], nc * sizeof *c);
        if (!c) return false;
        r->chs[k] = c;
        r->ch_cap[k] = nc;
    }
    r->chs[k][r->ch_len[k]++] = id;
    return true;
}

static bool cell_push(Rd *r, Val v, uint32_t next, uint32_t *out) {
    if (r->n_cells == r->cap_cells) {
        uint32_t nc = r->cap_cells ? r->cap_cells * 2u : 1024u;
        Cell *c = realloc(r->cells, (size_t)nc * sizeof *c);
        if (!c) return false;
        r->cells = c;
        r->cap_cells = nc;
    }
    r->cells[r->n_cells].v = v;
    r->cells[r->n_cells].next = next;
    *out = r->n_cells++;
    return true;
}

static bool tmp_push(Rd *r, AtomId id) {
    if (r->n_tmp == r->cap_tmp) {
        size_t nc = r->cap_tmp ? r->cap_tmp * 2u : 256u;
        AtomId *t = realloc(r->tmp, nc * sizeof *t);
        if (!t) return false;
        r->tmp = t;
        r->cap_tmp = nc;
    }
    r->tmp[r->n_tmp++] = id;
    return true;
}

static uint8_t *scratch(Rd *r, size_t n) {
    if (n + 1 > r->cap_buf) {
        size_t nc = n + 64;
        char *b = realloc(r->buf, nc);
        if (!b) return NULL;
        r->buf = b;
        r->cap_buf = nc;
    }
    return (uint8_t *)r->buf;
}

/* a leaf from a token, as the production's token form reads it */
static AtomId leaf_of(Rd *r, int kind, uint32_t a, uint32_t b) {
    const uint8_t *s = r->src + a;
    size_t n = b - a;
    switch (kind) {
    case GSLT_PRES_LEAF_VAR:
        return n >= 1 && s[0] == '$' ? ground_variable(&r->g, s + 1, n - 1) : CETTA_ATOM_ID_NONE;
    case GSLT_PRES_LEAF_WORD:
        if (n >= 1 && s[0] == '$') return ground_variable(&r->g, s + 1, n - 1);
        return ground_word(&r->g, s, n);
    case GSLT_PRES_LEAF_NAME:
    case GSLT_PRES_LEAF_NUMBER:
        return ground_word(&r->g, s, n);
    case GSLT_PRES_LEAF_NEGATIVE: {
        uint8_t *t = scratch(r, n + 1);
        if (!t) return CETTA_ATOM_ID_NONE;
        t[0] = '-';
        memcpy(t + 1, s, n);
        return ground_word(&r->g, t, n + 1);
    }
    case GSLT_PRES_LEAF_QUOTED: {
        if (n < 2) return CETTA_ATOM_ID_NONE;
        uint8_t *t = scratch(r, n);
        size_t k = 0;
        if (!t) return CETTA_ATOM_ID_NONE;
        for (size_t i = 1; i + 1 < n; i++) {
            if (s[i] == '\\' && i + 2 < n) i++;
            t[k++] = s[i];
        }
        if (!k) return CETTA_ATOM_ID_NONE;
        return ground_word(&r->g, t, k);
    }
    case GSLT_PRES_LEAF_STRING: {
        if (n < 2) return CETTA_ATOM_ID_NONE;
        uint8_t *t = scratch(r, n);
        size_t k = 0;
        if (!t) return CETTA_ATOM_ID_NONE;
        for (size_t i = 1; i + 1 < n; i++) {
            uint8_t c = s[i];
            if (c == '\\' && i + 2 < n) {
                uint8_t e = s[++i];
                switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'f': c = '\f'; break;
                default: c = e; break;
                }
            }
            t[k++] = c;
        }
        return ground_string(&r->g, t, k);
    }
    case GSLT_PRES_LEAF_RAW:
        /* r#"..."# */
        if (n < 5) return CETTA_ATOM_ID_NONE;
        return ground_string(&r->g, s + 3, n - 5);
    default:
        return CETTA_ATOM_ID_NONE;
    }
}

static AtomId val_atom(Rd *r, Val v);

static AtomId list_atom(Rd *r, uint32_t head, uint32_t len) {
    size_t base = r->n_tmp;
    for (uint32_t c = head, k = 0; c != NIL && k < len; c = r->cells[c].next, k++) {
        AtomId id = val_atom(r, r->cells[c].v);
        if (id == CETTA_ATOM_ID_NONE || !tmp_push(r, id)) { r->n_tmp = base; return CETTA_ATOM_ID_NONE; }
    }
    AtomId out = ground_expression(&r->g, r->tmp + base, r->n_tmp - base);
    r->n_tmp = base;
    return out;
}

static AtomId val_atom(Rd *r, Val v) {
    switch (v.kind) {
    case V_ATOM: return v.id;
    case V_LIST: return list_atom(r, v.head, v.len);
    case V_TOKEN: return ground_word(&r->g, r->src + v.head, v.len - v.head);
    case V_RUN:
        set_error(r->err, r->errsz, "a !-statement inside an expression");
        return CETTA_ATOM_ID_NONE;
    default: return CETTA_ATOM_ID_NONE;
    }
}

/* the items of V as list cells */
static bool val_list(Rd *r, Val v, uint32_t *head, uint32_t *len) {
    if (v.kind == V_LIST) { *head = v.head; *len = v.len; return true; }
    AtomId id = val_atom(r, v);
    if (id == CETTA_ATOM_ID_NONE || tu_kind(r->u, id) != ATOM_EXPR) {
        set_error(r->err, r->errsz, "a template puts items in front of an atom that is no expression");
        return false;
    }
    CettaExprLen n = tu_arity(r->u, id);
    uint32_t h = NIL;
    for (CettaExprLen k = n; k > 0; k--) {
        Val e = {V_ATOM, tu_child(r->u, id, (CettaExprIndex)(k - 1)), 0, 0};
        if (!cell_push(r, e, h, &h)) return false;
    }
    *head = h;
    *len = (uint32_t)n;
    return true;
}

static bool t_eval(Rd *r, const int32_t **pc, const Val *args, int nargs, Val *out) {
    int32_t op = *(*pc)++;
    switch (op) {
    case GSLT_PRES_T_WORD:
    case GSLT_PRES_T_STRING: {
        int32_t k = *(*pc)++;
        out->kind = V_ATOM;
        out->id = r->consts[k];
        return out->id != CETTA_ATOM_ID_NONE;
    }
    case GSLT_PRES_T_ARG: {
        int32_t i = *(*pc)++;
        if (i < 0 || i >= nargs) return false;
        *out = args[i];
        return true;
    }
    case GSLT_PRES_T_EXPR: {
        int32_t n = *(*pc)++;
        size_t base = r->n_tmp;
        for (int32_t k = 0; k < n; k++) {
            Val v;
            if (!t_eval(r, pc, args, nargs, &v)) { r->n_tmp = base; return false; }
            AtomId id = val_atom(r, v);
            if (id == CETTA_ATOM_ID_NONE || !tmp_push(r, id)) { r->n_tmp = base; return false; }
        }
        out->kind = V_ATOM;
        out->id = ground_expression(&r->g, r->tmp + base, (size_t)n);
        r->n_tmp = base;
        return out->id != CETTA_ATOM_ID_NONE;
    }
    case GSLT_PRES_T_CONS: {
        Val x, l;
        uint32_t head, len, cell;
        if (!t_eval(r, pc, args, nargs, &x) || !t_eval(r, pc, args, nargs, &l)) return false;
        AtomId xid = val_atom(r, x);
        if (xid == CETTA_ATOM_ID_NONE || !val_list(r, l, &head, &len)) return false;
        Val xv = {V_ATOM, xid, 0, 0};
        if (!cell_push(r, xv, head, &cell)) return false;
        out->kind = V_LIST;
        out->head = cell;
        out->len = len + 1u;
        return true;
    }
    case GSLT_PRES_T_RUN: {
        Val e;
        if (!t_eval(r, pc, args, nargs, &e)) return false;
        out->kind = V_RUN;
        out->id = val_atom(r, e);
        return out->id != CETTA_ATOM_ID_NONE;
    }
    case GSLT_PRES_T_FIXED: {
        int32_t i = *(*pc)++;
        int32_t k = *(*pc)++;
        if (i < 0 || i >= nargs) return false;
        AtomId got = val_atom(r, args[i]);
        if (got != r->consts[k]) {
            unsigned l, c;
            line_col(r->src, args[i].pos != UINT32_MAX ? args[i].pos : 0u, &l, &c);
            set_error(r->err, r->errsz, "line %u, column %u: only %s has an atom here", l, c, r->tb->tconst[k]);
            return false;
        }
        return t_eval(r, pc, args, nargs, out);
    }
    case GSLT_PRES_T_CHANNEL: {
        int32_t k = *(*pc)++;
        unsigned l, c;
        if (!r->n_scopes || k < 0 || k >= GSLT_PRES_MAX_CHANNELS) return false;
        size_t from = r->scopes[r->n_scopes - 1][k];
        line_col(r->src, r->scope_pos, &l, &c);
        if (r->ch_len[k] <= from) {
            set_error(r->err, r->errsz, "line %u, column %u: the construct names no %s", l, c,
                      r->tb->nonterminal_name[0] ? "channel" : "channel");
            return false;
        }
        AtomId v = r->chs[k][from];
        for (size_t i = from + 1; i < r->ch_len[k]; i++) {
            if (r->chs[k][i] != v) {
                set_error(r->err, r->errsz,
                          "line %u, column %u: every channel of one construct must name the same atom "
                          "(one step reads one store and routes into one destination)", l, c);
                return false;
            }
        }
        out->kind = V_ATOM;
        out->id = v;
        return true;
    }
    default:
        return false;
    }
}

static void expected_terms(const GSLTPresentationTablesV1 *tb, int state, char *out, size_t n) {
    size_t k = 0;
    int shown = 0;
    out[0] = '\0';
    for (int t = 0; t < tb->n_terminals && k + 32 < n; t++) {
        if (!tb->action[(size_t)state * tb->n_terminals + (size_t)t]) continue;
        if (shown == 12) { k += (size_t)snprintf(out + k, n - k, ", ..."); break; }
        k += (size_t)snprintf(out + k, n - k, "%s%s%s%s", shown ? ", " : "",
                              tb->terminal_kind[t] == GSLT_PRES_TERM_TOKEN ? "" : "'",
                              tb->terminal_text[t],
                              tb->terminal_kind[t] == GSLT_PRES_TERM_TOKEN ? "" : "'");
        shown++;
    }
}

static int read_ids_internal(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                             const char *text, size_t len, TermUniverse *universe, AtomId **out_ids,
                             GSLTPresentationCommentV1 **comments, size_t *n_comments, char *err,
                             size_t errsz, LintTree *lint);

int gslt_presentation_v1_read_ids(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                                  const char *text, size_t len, TermUniverse *universe, AtomId **out_ids,
                                  char *err, size_t errsz) {
    return read_ids_internal(tb, grounding, text, len, universe, out_ids, NULL, NULL, err, errsz, NULL);
}

int gslt_presentation_v1_read_ids_comments(const GSLTPresentationTablesV1 *tb,
                                           GSLTPresentationGroundingKindV1 grounding, const char *text,
                                           size_t len, TermUniverse *universe, AtomId **out_ids,
                                           GSLTPresentationCommentV1 **comments, size_t *n_comments,
                                           char *err, size_t errsz) {
    if (comments) *comments = NULL;
    if (n_comments) *n_comments = 0;
    if (!comments || !n_comments) return -1;
    return read_ids_internal(tb, grounding, text, len, universe, out_ids, comments, n_comments, err, errsz,
                             NULL);
}

void gslt_presentation_v1_comments_free(GSLTPresentationCommentV1 *comments, size_t n) {
    for (size_t i = 0; comments && i < n; i++) free(comments[i].text);
    free(comments);
}

static int read_ids_internal(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                             const char *text, size_t len, TermUniverse *universe, AtomId **out_ids,
                             GSLTPresentationCommentV1 **comments, size_t *n_comments, char *err,
                             size_t errsz, LintTree *lint) {
    Rd r;
    int result = -1;
    int *states = NULL;
    Val *vals = NULL;
    size_t sp = 0, cap = 0;
    AtomId *out = NULL;
    size_t n_out = 0, cap_out = 0;
    LToks resolved = {0};
    CommentCap ccap = {0};
    uint32_t *stmt_pos = NULL;
    size_t n_stmt = 0, cap_stmt = 0;
    if (err && errsz) err[0] = '\0';
    if (out_ids) *out_ids = NULL;
    if (!tb || !text || !universe || !out_ids) return -1;
    memset(&r, 0, sizeof r);
    r.tb = tb;
    r.u = universe;
    r.src = (const uint8_t *)text;
    r.src_len = len;
    r.err = err;
    r.errsz = errsz;
    if (!ground_open(&r.g, grounding, universe) || !ground_begin(&r.g)) {
        set_error(err, errsz, "cannot open the reader's grounding");
        ground_close(&r.g);
        return -1;
    }
    r.consts = calloc(tb->n_tconst ? tb->n_tconst : 1u, sizeof *r.consts);
    if (!r.consts) goto done;
    for (uint16_t k = 0; k < tb->n_tconst; k++) {
        const char *c = tb->tconst[k];
        size_t n = tb->tconst_len[k] & 0x7fffffffu;
        r.consts[k] = (tb->tconst_len[k] & 0x80000000u) ? ground_string(&r.g, (const uint8_t *)c, n)
                                                        : ground_word(&r.g, (const uint8_t *)c, n);
    }
    AtomId bang = ground_word(&r.g, (const uint8_t *)"!", 1u);
    if (!ground_begin(&r.g)) goto done;

    TokSrc ts = {tb, r.src, len, 0, NULL, 0, comments ? &ccap : NULL};
    if (tb->has_layout) {
        LToks lexed = {0};
        int lr = lex_all(tb, r.src, len, &lexed, err, errsz, comments ? &ccap : NULL);
        if (lr == 0) lr = resolve_layout(tb, &lexed, &resolved, err, errsz);
        free(lexed.v);
        if (lr < 0) goto done;
        ts.arr = &resolved;
    }
    Tok tok;
    tok.raw_term = -1;
    int have = src_next(&ts, &tok, err, errsz);
    if (have < 0) goto done;
    if (have == 0) { tok.term = tb->eof_terminal; tok.start = tok.end = (uint32_t)len; tok.raw_term = -1; }
    cap = 256;
    states = malloc(cap * sizeof *states);
    vals = malloc(cap * sizeof *vals);
    if (!states || !vals) goto done;
    states[0] = 0;
    sp = 1;
    for (;;) {
        int st = states[sp - 1];
        if (tok.raw_term >= 0) {
            /* A keyword is a name wherever the grammar cannot take the
               keyword but can take the name; where it can take the
               keyword, the keyword is read. */
            if (!lr_viable(tb, states, sp, tok.term) && lr_viable(tb, states, sp, tok.raw_term))
                tok.term = tok.raw_term;
            tok.raw_term = -1;
        }
        int16_t act = tb->action[(size_t)st * tb->n_terminals + (size_t)tok.term];
        if (act == 0) {
            unsigned l, c;
            char exp[512];
            line_col(r.src, tok.start, &l, &c);
            expected_terms(tb, st, exp, sizeof exp);
            if (tok.term == tb->eof_terminal)
                set_error(err, errsz, "line %u, column %u: unexpected end of input; expected %s", l, c, exp);
            else
                set_error(err, errsz, "line %u, column %u: unexpected '%.*s'; expected %s", l, c,
                          (int)(tok.end - tok.start > 40 ? 40 : tok.end - tok.start), r.src + tok.start, exp);
            goto done;
        }
        if (act == GSLT_PRES_ACCEPT) break;
        if (sp + 1 >= cap) {
            cap *= 2;
            int *ns = realloc(states, cap * sizeof *states);
            if (!ns) goto done;
            states = ns;
            Val *nv = realloc(vals, cap * sizeof *vals);
            if (!nv) goto done;
            vals = nv;
        }
        if (act > 0) {
            if (tb->terminal_scope[tok.term]) {
                if (!scope_open(&r)) goto done;
                r.scope_pos = tok.start;
            }
            states[sp] = act - 1;
            vals[sp].kind = V_TOKEN;
            vals[sp].head = tok.start;
            vals[sp].len = tok.end;
            vals[sp].id = CETTA_ATOM_ID_NONE;
            vals[sp].pos = tok.virt ? UINT32_MAX : tok.start;
            vals[sp].node = 0;
            if (lint && !lint_node(lint, -1, NULL, 0, CETTA_ATOM_ID_NONE, vals[sp].pos, &vals[sp].node))
                goto done;
            sp++;
            if (tok.term == tb->eof_terminal) {
                set_error(err, errsz, "the parse table shifts past the end of input");
                goto done;
            }
            have = src_next(&ts, &tok, err, errsz);
            if (have < 0) goto done;
            if (have == 0) { tok.term = tb->eof_terminal; tok.start = tok.end = (uint32_t)len; tok.raw_term = -1; }
            continue;
        }
        int p = -act - 1;
        int plen = tb->prod_len[p];
        const int16_t *rhs = tb->prod_rhs + tb->prod_rhs_off[p];
        Val *kids = vals + (sp - (size_t)plen);
        Val args[32];
        int nargs = 0;
        for (int k = 0; k < plen; k++) {
            if (rhs[k] < 0 || tb->terminal_kind[rhs[k]] == GSLT_PRES_TERM_TOKEN) {
                if (nargs == 32) goto done;
                args[nargs++] = kids[k];
            }
        }
        Val res;
        memset(&res, 0, sizeof res);
        switch (tb->prod_kind[p]) {
        case GSLT_PRES_PROD_COERCION:
            for (int k = 0; k < plen; k++) if (rhs[k] < 0) res = kids[k];
            break;
        case GSLT_PRES_PROD_LIST_EMPTY:
            res.kind = V_LIST; res.head = NIL; res.len = 0;
            break;
        case GSLT_PRES_PROD_LIST_ONE: {
            uint32_t cell;
            if (nargs != 1) goto done;
            Val x = args[0];
            if (x.kind == V_TOKEN) { x.id = val_atom(&r, x); x.kind = V_ATOM; }
            if (!cell_push(&r, x, NIL, &cell)) goto done;
            res.kind = V_LIST; res.head = cell; res.len = 1;
            break;
        }
        case GSLT_PRES_PROD_LIST_CONS: {
            uint32_t cell;
            if (nargs != 2 || args[1].kind != V_LIST) goto done;
            Val x = args[0];
            if (x.kind == V_TOKEN) { x.id = val_atom(&r, x); x.kind = V_ATOM; }
            if (!cell_push(&r, x, args[1].head, &cell)) goto done;
            res.kind = V_LIST; res.head = cell; res.len = args[1].len + 1u;
            break;
        }
        case GSLT_PRES_PROD_TOKEN:
            if (nargs != 1 || args[0].kind != V_TOKEN) goto done;
            res.kind = V_ATOM;
            res.id = leaf_of(&r, tb->prod_leaf[p], args[0].head, args[0].len);
            if (res.id == CETTA_ATOM_ID_NONE) {
                unsigned l, c;
                line_col(r.src, args[0].head, &l, &c);
                set_error(err, errsz, "line %u, column %u: cannot read the token '%.*s'", l, c,
                          (int)(args[0].len - args[0].head), r.src + args[0].head);
                goto done;
            }
            break;
        case GSLT_PRES_PROD_PLAIN: {
            const int32_t *pc = tb->tcode + tb->prod_template[p];
            if (!t_eval(&r, &pc, args, nargs, &res)) {
                unsigned l, c;
                line_col(r.src, kids[0].kind == V_TOKEN ? kids[0].head : tok.start, &l, &c);
                set_error(err, errsz, "line %u: the form %s does not build an atom", l, tb->prod_label[p]);
                goto done;
            }
            break;
        }
        default:
            goto done;
        }
        res.pos = UINT32_MAX;
        for (int k = 0; k < plen; k++)
            if (kids[k].pos < res.pos) res.pos = kids[k].pos;
        if (lint) {
            /* the program and a list of statements have no atom of their own */
            bool statements = tb->prod_lhs[p] == tb->entry_nt ||
                              (res.kind == V_LIST && plen > 0 && rhs[0] < 0 && -rhs[0] - 1 == tb->statement_nt);
            AtomId atom = res.kind == V_RUN ? res.id
                        : statements ? CETTA_ATOM_ID_NONE
                        : res.kind == V_ATOM || res.kind == V_LIST ? val_atom(&r, res) : CETTA_ATOM_ID_NONE;
            if (!lint_node(lint, (int16_t)p, kids, plen, atom, res.pos, &res.node)) goto done;
            lint->root = res.node;
        }
        sp -= (size_t)plen;
        int lhs = tb->prod_lhs[p];
        if (tb->prod_scope[p]) scope_close(&r);
        if (lhs == tb->statement_nt && comments) {
            if (n_stmt == cap_stmt) {
                cap_stmt = cap_stmt ? cap_stmt * 2 : 256;
                uint32_t *np = realloc(stmt_pos, cap_stmt * sizeof *np);
                if (!np) goto done;
                stmt_pos = np;
            }
            stmt_pos[n_stmt++] = res.pos;
        }
        if (tb->nt_channel[lhs] >= 0) {
            AtomId cid = val_atom(&r, res);
            if (cid == CETTA_ATOM_ID_NONE || !channel_push(&r, tb->nt_channel[lhs], cid)) goto done;
        }
        int to = tb->goto_[(size_t)states[sp - 1] * tb->n_nonterminals + (size_t)lhs];
        if (to < 0) { set_error(err, errsz, "the parse table has no goto"); goto done; }
        states[sp] = to;
        vals[sp] = res;
        sp++;
        if (lhs == tb->statement_nt && !ground_begin(&r.g)) goto done;
    }
    /* the entry's value: the statements */
    size_t *first_atom = NULL;
    {
        Val prog = vals[sp - 1];
        uint32_t head, n;
        if (prog.kind != V_LIST) {
            if (!val_list(&r, prog, &head, &n)) goto done;
        } else {
            head = prog.head;
            n = prog.len;
        }
        if (comments && !(first_atom = calloc((size_t)n + 1u, sizeof *first_atom))) goto done;
        for (uint32_t c = head, k = 0; c != NIL && k < n; c = r.cells[c].next, k++) {
            Val v = r.cells[c].v;
            if (first_atom) first_atom[k] = n_out;
            if (n_out + 2 > cap_out) {
                cap_out = cap_out ? cap_out * 2 : 256;
                AtomId *no = realloc(out, cap_out * sizeof *no);
                if (!no) { free(first_atom); goto done; }
                out = no;
            }
            if (v.kind == V_RUN) {
                out[n_out++] = bang;
                out[n_out++] = v.id;
            } else {
                AtomId id = val_atom(&r, v);
                if (id == CETTA_ATOM_ID_NONE) { free(first_atom); goto done; }
                out[n_out++] = id;
            }
        }
        if (comments) {
            /* each comment before the first statement that begins after it */
            GSLTPresentationCommentV1 *cs = calloc(ccap.n ? ccap.n : 1u, sizeof *cs);
            if (!cs) { free(first_atom); goto done; }
            size_t s = 0;
            for (size_t i = 0; i < ccap.n; i++) {
                while (s < n_stmt && s < n && stmt_pos[s] <= ccap.start[i]) s++;
                cs[i].before = s < n_stmt && s < n ? first_atom[s] : n_out;
                size_t tl = ccap.end[i] - ccap.start[i];
                cs[i].text = malloc(tl + 1u);
                if (!cs[i].text) { gslt_presentation_v1_comments_free(cs, i); free(first_atom); goto done; }
                memcpy(cs[i].text, r.src + ccap.start[i], tl);
                cs[i].text[tl] = '\0';
            }
            *comments = cs;
            *n_comments = ccap.n;
        }
        free(first_atom);
    }
    *out_ids = out;
    out = NULL;
    result = (int)n_out;
done:
    if (result < 0) set_error(err, errsz, "the %s presentation could not read the text", tb->name);
    free(ccap.start);
    free(ccap.end);
    free(stmt_pos);
    free(out);
    free(states);
    free(vals);
    free(r.cells);
    free(r.tmp);
    free(r.consts);
    free(r.buf);
    for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) free(r.chs[k]);
    free(r.scopes);
    free(resolved.v);
    ground_close(&r.g);
    return result;
}

/* ------------------------------------------------------------ printing */

enum { P_ATOM = 0, P_RUN, P_SLICE, P_NONE };

typedef struct {
    uint8_t kind;
    AtomId id;
    uint32_t off; /* P_SLICE: the items of id from off */
} PT;

typedef struct {
    uint32_t off, len; /* into text */
    bool gl, gr, glue;
    int16_t term;      /* the terminal, or -1 for a leaf or glue */
} PTok;

/* The role a position gives the atom printed there; for a list, the role of
   each item (a call's arguments, by its head's declared positions). */
typedef struct {
    uint8_t role;
    const uint8_t *items; /* the roles of the first n_items items, or NULL */
    uint8_t n_items;
    uint8_t rest;         /* the role of the items after them */
} Role;

/* a document's own declaration (: f (-> A1 .. An R)): Ai = Atom holds */
typedef struct {
    AtomId head;
    uint8_t n;
    uint8_t roles[32];
} DocSig;

typedef struct {
    const GSLTPresentationTablesV1 *tb;
    TermUniverse *u;
    Ground g;
    AtomId *consts;
    DocSig *sigs;
    size_t n_sigs;
    PTok *toks;
    size_t n_toks, cap_toks;
    char *text;
    size_t n_text, cap_text;
    Arena arena;
    char *err;
    size_t errsz;
    bool has_run;
    /* channels the template of the form being matched names, and the
       channels of the scopes being printed */
    AtomId mbind[GSLT_PRES_MAX_CHANNELS];
    bool mbind_set[GSLT_PRES_MAX_CHANNELS];
    AtomId ctx[GSLT_PRES_MAX_CHANNELS];
    bool ctx_set[GSLT_PRES_MAX_CHANNELS];
    uint32_t ctx_used[GSLT_PRES_MAX_CHANNELS];
    /* soft: a symbol spelled like a keyword may be written bare, to be
       kept only if the statement reads back as the same atoms */
    bool soft, used_soft;
    GSLTPresentationGroundingKindV1 grounding;
} Pr;

static bool pt_is_expr(const Pr *p, PT t) {
    return t.kind == P_SLICE || (t.kind == P_ATOM && tu_kind(p->u, t.id) == ATOM_EXPR);
}
static uint32_t pt_len(const Pr *p, PT t) {
    if (t.kind == P_SLICE) return (uint32_t)tu_arity(p->u, t.id) - t.off;
    return (uint32_t)tu_arity(p->u, t.id);
}
static PT pt_item(const Pr *p, PT t, uint32_t k) {
    PT out = {P_ATOM, tu_child(p->u, t.id, (CettaExprIndex)((t.kind == P_SLICE ? t.off : 0u) + k)), 0};
    return out;
}

static bool tok_push(Pr *p, const char *s, size_t n, bool gl, bool gr, bool glue) {
    if (p->n_toks == p->cap_toks) {
        size_t nc = p->cap_toks ? p->cap_toks * 2 : 256;
        PTok *t = realloc(p->toks, nc * sizeof *t);
        if (!t) return false;
        p->toks = t;
        p->cap_toks = nc;
    }
    if (p->n_text + n + 1 > p->cap_text) {
        size_t nc = (p->n_text + n + 1) * 2;
        char *t = realloc(p->text, nc);
        if (!t) return false;
        p->text = t;
        p->cap_text = nc;
    }
    memcpy(p->text + p->n_text, s, n);
    p->toks[p->n_toks].off = (uint32_t)p->n_text;
    p->toks[p->n_toks].len = (uint32_t)n;
    p->toks[p->n_toks].gl = gl;
    p->toks[p->n_toks].gr = gr;
    p->toks[p->n_toks].glue = glue;
    p->toks[p->n_toks].term = -1;
    p->n_text += n;
    p->n_toks++;
    return true;
}

/* a terminal token, remembering which terminal it is */
static bool term_push(Pr *p, int term, const char *s, bool gl, bool gr) {
    if (!tok_push(p, s, strlen(s), gl, gr, false)) return false;
    p->toks[p->n_toks - 1].term = (int16_t)term;
    return true;
}

static bool word_like(const char *s) { return (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'); }

static bool tmatch(Pr *p, const int32_t **pc, PT t, PT *env) {
    int32_t op = *(*pc)++;
    switch (op) {
    case GSLT_PRES_T_WORD:
    case GSLT_PRES_T_STRING: {
        int32_t k = *(*pc)++;
        return t.kind == P_ATOM && t.id == p->consts[k];
    }
    case GSLT_PRES_T_ARG: {
        int32_t i = *(*pc)++;
        env[i] = t;
        return true;
    }
    case GSLT_PRES_T_RUN:
        if (t.kind != P_RUN) return false;
        t.kind = P_ATOM;
        return tmatch(p, pc, t, env);
    case GSLT_PRES_T_FIXED: {
        /* the fixed child is written as its constant */
        int32_t i = *(*pc)++;
        int32_t k = *(*pc)++;
        env[i].kind = P_ATOM;
        env[i].id = p->consts[k];
        env[i].off = 0;
        return tmatch(p, pc, t, env);
    }
    case GSLT_PRES_T_EXPR: {
        /* a failed match abandons the template, so the code position after
           a failure is never read */
        int32_t n = *(*pc)++;
        if (!pt_is_expr(p, t) || pt_len(p, t) != (uint32_t)n) return false;
        for (int32_t k = 0; k < n; k++)
            if (!tmatch(p, pc, pt_item(p, t, (uint32_t)k), env)) return false;
        return true;
    }
    case GSLT_PRES_T_CONS: {
        if (!pt_is_expr(p, t) || pt_len(p, t) < 1) return false;
        if (!tmatch(p, pc, pt_item(p, t, 0), env)) return false;
        PT rest = {P_SLICE, t.id, (t.kind == P_SLICE ? t.off : 0u) + 1u};
        return tmatch(p, pc, rest, env);
    }
    case GSLT_PRES_T_CHANNEL: {
        int32_t k = *(*pc)++;
        if (t.kind != P_ATOM || k < 0 || k >= GSLT_PRES_MAX_CHANNELS) return false;
        if (p->mbind_set[k]) return p->mbind[k] == t.id;
        p->mbind[k] = t.id;
        p->mbind_set[k] = true;
        return true;
    }
    default:
        return false;
    }
}

/* whether the template at *pc builds a !-statement; advances past it */
static bool t_has_run(const int32_t **pc) {
    int32_t op = *(*pc)++;
    bool run = false;
    switch (op) {
    case GSLT_PRES_T_WORD:
    case GSLT_PRES_T_STRING:
    case GSLT_PRES_T_ARG:
        (*pc)++;
        break;
    case GSLT_PRES_T_RUN:
        (void)t_has_run(pc);
        run = true;
        break;
    case GSLT_PRES_T_FIXED:
        (*pc) += 2;
        run = t_has_run(pc);
        break;
    case GSLT_PRES_T_EXPR: {
        int32_t n = *(*pc)++;
        for (int32_t k = 0; k < n; k++) run = t_has_run(pc) || run;
        break;
    }
    case GSLT_PRES_T_CONS:
        run = t_has_run(pc);
        run = t_has_run(pc) || run;
        break;
    case GSLT_PRES_T_CHANNEL:
        (*pc)++;
        break;
    default:
        break;
    }
    return run;
}

static bool render(Pr *p, PT t, int nt, Role r);

static Role role_plain(uint8_t role) {
    Role r = {role, NULL, 0, role};
    return r;
}

/* the role of item I of a list in role R */
static Role item_role(Role r, uint32_t i) {
    if (r.role == GSLT_PRES_ROLE_DATA) return role_plain(GSLT_PRES_ROLE_DATA);
    if (r.items) return role_plain(i < r.n_items ? r.items[i] : r.rest);
    if (r.role == GSLT_PRES_ROLE_LIST) return role_plain(GSLT_PRES_ROLE_RUN);
    return role_plain(r.role);
}

/* the rest of a list after its first item */
static Role role_shift(Role r) {
    if (r.items) {
        if (r.n_items > 1) { r.items++; r.n_items--; }
        else { r.items = NULL; r.n_items = 0; r.role = r.rest; }
    }
    return r;
}

/* the text of a leaf as the standard printer writes it */
static const char *leaf_text(Pr *p, AtomId id) {
    return term_universe_atom_to_parseable_string(&p->arena, p->u, id);
}

static bool reads_back(Pr *p, const char *word, size_t n, AtomId id) {
    return ground_word(&p->g, (const uint8_t *)word, n) == id;
}

/* the shortest digits that read back to the same double, written with a
   point and an exponent without '+': 2.0, 1.9999999999999998, 1.0e17 */
static bool float_spelling(double v, char *out, size_t cap) {
    char tmp[64];
    if (v != v || v - v != 0.0) return false;
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(tmp, sizeof tmp, "%.*g", prec, v);
        double back = strtod(tmp, NULL);
        if (memcmp(&back, &v, sizeof v) == 0) break;
    }
    char *e = strchr(tmp, 'e');
    size_t mant = e ? (size_t)(e - tmp) : strlen(tmp);
    size_t k = 0;
    if (mant + 24 > cap) return false;
    memcpy(out, tmp, mant);
    k = mant;
    if (!memchr(tmp, '.', mant)) { out[k++] = '.'; out[k++] = '0'; }
    if (e) {
        const char *q = e + 1;
        out[k++] = 'e';
        if (*q == '+') q++;
        while (*q && k + 1 < cap) out[k++] = *q++;
    }
    out[k] = '\0';
    return true;
}

/* the first token production of leaf kind KIND in family FAM, or -1 */
static int leaf_prod_in(const GSLTPresentationTablesV1 *tb, int kind, int fam) {
    for (int q = 0; q < tb->n_productions; q++)
        if (tb->prod_kind[q] == GSLT_PRES_PROD_TOKEN && tb->prod_leaf[q] == kind &&
            tb->nt_family[tb->prod_lhs[q]] == fam)
            return q;
    return -1;
}

static int token_term_of(const GSLTPresentationTablesV1 *tb, int prod) {
    const int16_t *rhs = tb->prod_rhs + tb->prod_rhs_off[prod];
    for (int k = 0; k < tb->prod_len[prod]; k++)
        if (rhs[k] >= 0 && tb->terminal_kind[rhs[k]] == GSLT_PRES_TERM_TOKEN) return rhs[k];
    return -1;
}

static bool render_form(Pr *p, int prod, const PT *env, int fam, int lvl, Role r);

/* the spelling of a leaf in family FAM: the first of its token forms whose
   token reads it back */
static bool render_leaf(Pr *p, PT t, int fam) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    if (t.kind != P_ATOM) return false;
    AtomKind kind = tu_kind(p->u, t.id);
    if (kind == ATOM_VAR) {
        SymbolId sym = tu_sym(p->u, t.id);
        const char *name = symbol_bytes(g_symbols, sym);
        size_t n = symbol_len(g_symbols, sym);
        char *s = arena_alloc(&p->arena, n + 2);
        s[0] = '$';
        memcpy(s + 1, name, n);
        s[n + 1] = '\0';
        for (int k = 0; k < 9; k++) {
            if (k != GSLT_PRES_LEAF_VAR && k != GSLT_PRES_LEAF_WORD) continue;
            int prod = leaf_prod_in(tb, k, fam);
            if (prod < 0) continue;
            int term = token_term_of(tb, prod);
            if (lex_single(tb, s, n + 1) == term) return tok_push(p, s, n + 1, false, false, false);
        }
        set_error(p->err, p->errsz, "no spelling for the variable $%.*s", (int)n, name);
        return false;
    }
    if (kind == ATOM_GROUNDED && tu_ground_kind(p->u, t.id) == GV_STRING) {
        Atom *a = term_universe_get_atom(p->u, t.id);
        const char *sv = a->ground.sval;
        size_t n = a->ground.slen;
        bool newline = memchr(sv, '\n', n) != NULL;
        bool fence = false;
        for (size_t i = 0; i + 1 < n; i++) if (sv[i] == '"' && sv[i + 1] == '#') fence = true;
        int raw = leaf_prod_in(tb, GSLT_PRES_LEAF_RAW, fam), str = leaf_prod_in(tb, GSLT_PRES_LEAF_STRING, fam);
        if (raw >= 0 && newline && !fence) {
            char *s = arena_alloc(&p->arena, n + 6);
            memcpy(s, "r#\"", 3);
            memcpy(s + 3, sv, n);
            memcpy(s + 3 + n, "\"#", 3);
            if (lex_single(tb, s, n + 5) == token_term_of(tb, raw))
                return tok_push(p, s, n + 5, false, false, false);
        }
        if (str >= 0) {
            char *s = arena_alloc(&p->arena, 2 * n + 3);
            size_t k = 0;
            s[k++] = '"';
            for (size_t i = 0; i < n; i++) {
                char c = sv[i];
                if (c == '"' || c == '\\') { s[k++] = '\\'; s[k++] = c; }
                else if (c == '\n') { s[k++] = '\\'; s[k++] = 'n'; }
                else if (c == '\t') { s[k++] = '\\'; s[k++] = 't'; }
                else if (c == '\r') { s[k++] = '\\'; s[k++] = 'r'; }
                else if (c == '\f') { s[k++] = '\\'; s[k++] = 'f'; }
                else s[k++] = c;
            }
            s[k++] = '"';
            s[k] = '\0';
            if (lex_single(tb, s, k) == token_term_of(tb, str))
                return tok_push(p, s, k, false, false, false);
        }
        set_error(p->err, p->errsz, "no spelling for a string");
        return false;
    }
    if (kind == ATOM_EXPR) return false;
    const char *text = NULL;
    if (kind == ATOM_GROUNDED && tu_ground_kind(p->u, t.id) == GV_FLOAT) {
        /* The standard printer's float text may not read back (it writes
           1.9999999999999998 as 2); a float is spelled by its shortest
           digits that read back to the same bits, as d.d[e[-]d]. */
        char *s = arena_alloc(&p->arena, 64);
        if (!float_spelling(tu_float(p->u, t.id), s, 64)) {
            set_error(p->err, p->errsz, "no spelling for a float that is not finite");
            return false;
        }
        text = s;
    } else {
        text = leaf_text(p, t.id);
    }
    if (!text) return false;
    size_t n = strlen(text);
    const char *num = NULL;
    size_t num_n = 0;
    for (int p_i = 0; p_i < tb->n_productions; p_i++) {
        if (tb->prod_kind[p_i] != GSLT_PRES_PROD_TOKEN || tb->nt_family[tb->prod_lhs[p_i]] != fam) continue;
        int lk = tb->prod_leaf[p_i];
        const int16_t *rhs = tb->prod_rhs + tb->prod_rhs_off[p_i];
        int term = -1;
        for (int k = 0; k < tb->prod_len[p_i]; k++)
            if (rhs[k] >= 0 && tb->terminal_kind[rhs[k]] == GSLT_PRES_TERM_TOKEN) term = rhs[k];
        if (term < 0) continue;
        if (lk == GSLT_PRES_LEAF_NAME || lk == GSLT_PRES_LEAF_NUMBER || lk == GSLT_PRES_LEAF_WORD) {
            if (lex_single(tb, text, n) == term && reads_back(p, text, n, t.id))
                return tok_push(p, text, n, false, false, false);
            if (p->soft && lk == GSLT_PRES_LEAF_NAME && lex_single_as(tb, text, n, true) == term &&
                reads_back(p, text, n, t.id)) {
                p->used_soft = true;
                return tok_push(p, text, n, false, false, false);
            }
            if (num && lex_single(tb, num, num_n) == term && reads_back(p, num, num_n, t.id))
                return tok_push(p, num, num_n, false, false, false);
        } else if (lk == GSLT_PRES_LEAF_NEGATIVE) {
            const char *body = num ? num : text;
            size_t bn = num ? num_n : n;
            if (bn > 1 && body[0] == '-' && lex_single(tb, body + 1, bn - 1) == term &&
                reads_back(p, body, bn, t.id))
                return tok_push(p, body, bn, false, false, false);
        }
    }
    int q = leaf_prod_in(tb, GSLT_PRES_LEAF_QUOTED, fam);
    if (q >= 0) {
        char *s = arena_alloc(&p->arena, 2 * n + 3);
        size_t k = 0;
        s[k++] = '`';
        for (size_t i = 0; i < n; i++) {
            if (text[i] == '`' || text[i] == '\\') s[k++] = '\\';
            s[k++] = text[i];
        }
        s[k++] = '`';
        s[k] = '\0';
        if (lex_single(tb, s, k) == token_term_of(tb, q) && reads_back(p, text, n, t.id))
            return tok_push(p, s, k, false, false, false);
    }
    set_error(p->err, p->errsz, "no spelling for the atom %s", text);
    return false;
}

static bool guard_ok(Pr *p, int guard, PT t) {
    if (guard == GSLT_PRES_GUARD_SYMBOL_HEAD) {
        if (!pt_is_expr(p, t) || pt_len(p, t) == 0) return false;
        PT h = pt_item(p, t, 0);
        AtomKind k = tu_kind(p->u, h.id);
        if (k == ATOM_SYMBOL) return true;
        if (k != ATOM_GROUNDED || tu_ground_kind(p->u, h.id) == GV_STRING) return false;
        /* an operation spelled by a name, not a number */
        const char *s = leaf_text(p, h.id);
        return s && !(s[0] >= '0' && s[0] <= '9') && !(s[0] == '-' && s[1] >= '0' && s[1] <= '9');
    }
    if (guard == GSLT_PRES_GUARD_CALLABLE_HEAD) {
        if (!pt_is_expr(p, t) || pt_len(p, t) == 0) return false;
        PT h = pt_item(p, t, 0);
        AtomKind k = tu_kind(p->u, h.id);
        if (k == ATOM_EXPR || k == ATOM_SYMBOL) return true;
        if (k == ATOM_VAR) return false;
        if (k == ATOM_GROUNDED) {
            /* a literal (a string or a number of any width) is no callee */
            if (tu_ground_kind(p->u, h.id) == GV_STRING) return false;
            const char *s = leaf_text(p, h.id);
            if (!s) return false;
            if (s[0] >= '0' && s[0] <= '9') return false;
            if (s[0] == '-' && s[1] >= '0' && s[1] <= '9') return false;
            return true;
        }
        return false;
    }
    return true;
}

static int16_t item_at(const GSLTPresentationTablesV1 *tb, int prod, int k) {
    return tb->prod_rhs[tb->prod_rhs_off[prod] + (uint32_t)k];
}

static bool is_arg_item(const GSLTPresentationTablesV1 *tb, int prod, int k) {
    int16_t sym = item_at(tb, prod, k);
    return sym < 0 || tb->terminal_kind[sym] == GSLT_PRES_TERM_TOKEN;
}

static bool same_head(Pr *p, AtomId a, AtomId b) {
    if (a == b) return true;
    return tu_kind(p->u, a) == ATOM_SYMBOL && tu_kind(p->u, b) == ATOM_SYMBOL && tu_sym(p->u, a) == tu_sym(p->u, b);
}

/* The roles of a call's N arguments: the document's own declaration of its
   head with N arguments, else the builtins' declaration of the head with N
   arguments, else every argument runs (an undeclared position runs). */
static Role call_args_role(Pr *p, PT head, uint32_t n) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    Role r = role_plain(GSLT_PRES_ROLE_RUN);
    if (head.kind != P_ATOM) return r;
    for (size_t k = 0; k < p->n_sigs; k++) {
        if (p->sigs[k].n == n && same_head(p, p->sigs[k].head, head.id)) {
            r.items = p->sigs[k].roles;
            r.n_items = p->sigs[k].n;
            return r;
        }
    }
    const char *name = NULL;
    size_t len = 0;
    AtomKind kind = tu_kind(p->u, head.id);
    if (kind == ATOM_SYMBOL) {
        name = symbol_bytes(g_symbols, tu_sym(p->u, head.id));
        len = symbol_len(g_symbols, tu_sym(p->u, head.id));
    } else if (kind == ATOM_GROUNDED && tu_ground_kind(p->u, head.id) != GV_STRING) {
        name = leaf_text(p, head.id);
        len = name ? strlen(name) : 0;
    }
    if (!name || !tb->n_sigs) return r;
    for (uint16_t k = 0; k < tb->n_sigs; k++) {
        if (tb->sig_len[k] == n && strlen(tb->sig_head[k]) == len && memcmp(tb->sig_head[k], name, len) == 0) {
            r.items = tb->role_code + tb->sig_off[k];
            r.n_items = tb->sig_len[k];
            return r;
        }
    }
    return r;
}

/* The role of child K of production PROD printed in role PARENT: inside data
   everything is data; a definition's head is a call pattern whose arguments
   are data; otherwise the production's declared role for the child. */
static Role child_role(Pr *p, int prod, int k, const PT *env, Role parent) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    if (parent.role == GSLT_PRES_ROLE_DATA) return role_plain(GSLT_PRES_ROLE_DATA);
    uint8_t kind = tb->prod_kind[prod];
    if (kind == GSLT_PRES_PROD_LIST_ONE || kind == GSLT_PRES_PROD_LIST_CONS)
        return k == 0 ? item_role(parent, 0) : role_shift(parent);
    int32_t off = tb->prod_role_off ? tb->prod_role_off[prod] : -1;
    uint8_t code = off < 0 ? GSLT_PRES_ROLE_INHERIT : tb->role_code[off + k];
    if (parent.role == GSLT_PRES_ROLE_HEAD)
        return role_plain(code == GSLT_PRES_ROLE_RUN ? GSLT_PRES_ROLE_HEAD : GSLT_PRES_ROLE_DATA);
    if (code == GSLT_PRES_ROLE_PATTERN)
        code = parent.role == GSLT_PRES_ROLE_SOURCE ? GSLT_PRES_ROLE_DATA : GSLT_PRES_ROLE_INHERIT;
    if (code == GSLT_PRES_ROLE_INHERIT)
        return role_plain(parent.role == GSLT_PRES_ROLE_LIST ? GSLT_PRES_ROLE_RUN : parent.role);
    if (code == GSLT_PRES_ROLE_ARGS) {
        PT args = env[k];
        uint32_t n = pt_is_expr(p, args) ? pt_len(p, args) : 0;
        return call_args_role(p, env[0], n);
    }
    return role_plain(code);
}

static bool render_items(Pr *p, int prod, const PT *env, int from, int env_base, Role parent) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    int len = tb->prod_len[prod];
    int argk = env_base;
    /* the data look's braces, f{a, b}, are brackets of a literal: glued to
       the head and tight inside, never a block */
    bool data_look = tb->prod_look && tb->prod_look[prod] == GSLT_PRES_LOOK_BIT(GSLT_PRES_ROLE_DATA);
    for (int k = from; k < len; k++) {
        int16_t sym = item_at(tb, prod, k);
        bool is_arg = sym < 0 || tb->terminal_kind[sym] == GSLT_PRES_TERM_TOKEN;
        /* a call f(a) and data f[a] or f{a} keep their bracket on the head */
        bool next_open = k + 1 < len && item_at(tb, prod, k + 1) >= 0 &&
                         tb->terminal_kind[item_at(tb, prod, k + 1)] != GSLT_PRES_TERM_TOKEN &&
                         (strcmp(tb->terminal_text[item_at(tb, prod, k + 1)], "(") == 0 ||
                          (is_arg_item(tb, prod, k) &&
                           (strcmp(tb->terminal_text[item_at(tb, prod, k + 1)], "[") == 0 ||
                            (data_look && strcmp(tb->terminal_text[item_at(tb, prod, k + 1)], "{") == 0))));
        if (!is_arg) {
            const char *s = tb->terminal_text[sym];
            bool brace = data_look && (!strcmp(s, "{") || !strcmp(s, "}"));
            bool closer = !strcmp(s, ")") || !strcmp(s, "]") || !strcmp(s, ",") || !strcmp(s, ";") ||
                          (brace && !strcmp(s, "}"));
            bool opener = !strcmp(s, "(") || !strcmp(s, "[") || (brace && !strcmp(s, "{"));
            bool next_nt = k + 1 < len && (item_at(tb, prod, k + 1) < 0 ||
                                           tb->terminal_kind[item_at(tb, prod, k + 1)] == GSLT_PRES_TERM_TOKEN);
            bool gl = false, gr = false;
            int spacing = tb->terminal_spacing ? tb->terminal_spacing[sym] : GSLT_PRES_SPACING_DEFAULT;
            if (spacing == GSLT_PRES_SPACING_TIGHT) gl = gr = true;
            else if (spacing == GSLT_PRES_SPACING_PREFIX) gr = true;
            else if (closer) gl = true;
            else if (opener) gr = true;
            else if (k == 0 && next_nt && !word_like(s)) gr = true;
            if (!term_push(p, sym, s, gl, gr)) return false;
            if (brace) p->toks[p->n_toks - 1].term = -1; /* a literal's brace, no block */
            if (word_like(s) && next_open && spacing != GSLT_PRES_SPACING_SPACED &&
                !tok_push(p, "", 0, false, false, true))
                return false;
            continue;
        }
        Role sub_role = child_role(p, prod, argk, env, parent);
        PT sub = env[argk++];
        if (sub.kind == P_NONE) {
            /* a channel item the template leaves to its scope */
            int ch = sym < 0 ? tb->nt_channel[-sym - 1] : -1;
            if (ch < 0 || !p->ctx_set[ch]) return false;
            sub.kind = P_ATOM;
            sub.id = p->ctx[ch];
            sub.off = 0;
            p->ctx_used[ch]++;
        }
        if (sym < 0) {
            if (!render(p, sub, -sym - 1, sub_role)) return false;
        } else {
            if (!render_leaf(p, sub, tb->leaf_family)) return false;
        }
        if (next_open && !tok_push(p, "", 0, false, false, true)) return false;
    }
    return true;
}

static bool render_list(Pr *p, PT t, int nt, Role r) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    if (!pt_is_expr(p, t)) return false;
    uint32_t n = pt_len(p, t);
    if (n == 0) return tb->nt_list_empty[nt] >= 0;
    PT head = pt_item(p, t, 0);
    PT rest = {P_SLICE, t.id, (t.kind == P_SLICE ? t.off : 0u) + 1u};
    if (n == 1 && tb->nt_list_one[nt] >= 0) {
        PT env[1] = {head};
        return render_items(p, tb->nt_list_one[nt], env, 0, 0, r);
    }
    if (tb->nt_list_cons[nt] < 0) return false;
    PT env[2] = {head, rest};
    return render_items(p, tb->nt_list_cons[nt], env, 0, 0, r);
}

static bool family_has_leaves(const GSLTPresentationTablesV1 *tb, int fam) {
    for (int q = 0; q < tb->n_productions; q++)
        if (tb->prod_kind[q] == GSLT_PRES_PROD_TOKEN && tb->nt_family[tb->prod_lhs[q]] == fam) return true;
    return false;
}

static bool render(Pr *p, PT t, int nt, Role r) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    if (tb->nt_is_list[nt]) return render_list(p, t, nt, r);
    int fam = tb->nt_family[nt], lvl = tb->nt_level[nt];
    bool leaf = t.kind == P_ATOM && tu_kind(p->u, t.id) != ATOM_EXPR;
    if (leaf && fam == tb->leaf_family) return render_leaf(p, t, fam);
    if (leaf && family_has_leaves(tb, fam)) {
        size_t mark_t = p->n_toks, mark_x = p->n_text;
        if (render_leaf(p, t, fam)) return true;
        p->n_toks = mark_t;
        p->n_text = mark_x;
    }
    for (uint16_t f = 0; f < tb->n_forms; f++) {
        int prod = tb->forms[f];
        int lhs = tb->prod_lhs[prod];
        if (tb->nt_family[lhs] != fam) continue;
        /* a form prints only in the roles of its look; a list prints only
           in the forms that declare the list look */
        uint8_t look = tb->prod_look ? tb->prod_look[prod] : 0;
        if (look ? !(look & GSLT_PRES_LOOK_BIT(r.role)) : r.role == GSLT_PRES_ROLE_LIST) continue;
        PT env[32];
        for (int k = 0; k < 32; k++) env[k].kind = P_NONE;
        for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) p->mbind_set[k] = false;
        const int32_t *pc = tb->tcode + tb->prod_template[prod];
        if (!tmatch(p, &pc, t, env)) continue;
        if (!guard_ok(p, tb->prod_guard[prod], t)) continue;
        /* the channels this form's template names become its scope's */
        AtomId saved_ctx[GSLT_PRES_MAX_CHANNELS];
        bool saved_set[GSLT_PRES_MAX_CHANNELS], bound[GSLT_PRES_MAX_CHANNELS];
        uint32_t saved_used[GSLT_PRES_MAX_CHANNELS];
        bool any_bound = false;
        for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) {
            bound[k] = p->mbind_set[k];
            if (!bound[k]) continue;
            any_bound = true;
            saved_ctx[k] = p->ctx[k];
            saved_set[k] = p->ctx_set[k];
            saved_used[k] = p->ctx_used[k];
            p->ctx[k] = p->mbind[k];
            p->ctx_set[k] = true;
            p->ctx_used[k] = 0;
        }
        size_t mark_t = p->n_toks, mark_x = p->n_text;
        bool ok = render_form(p, prod, env, fam, lvl, r);
        if (ok && any_bound) {
            /* every channel the atom has must be written at least once */
            for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++)
                if (bound[k] && p->ctx_used[k] == 0) ok = false;
        }
        for (int k = 0; k < GSLT_PRES_MAX_CHANNELS; k++) {
            if (!bound[k]) continue;
            p->ctx[k] = saved_ctx[k];
            p->ctx_set[k] = saved_set[k];
            p->ctx_used[k] = saved_used[k];
        }
        if (ok) return true;
        p->n_toks = mark_t;
        p->n_text = mark_x;
    }
    return false;
}

/* a form's items, parenthesized when its level is below the one asked for */
static bool render_form(Pr *p, int prod, const PT *env, int fam, int lvl, Role r) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    int lhs = tb->prod_lhs[prod];
    {
        size_t mark_t = p->n_toks, mark_x = p->n_text;
        bool paren = tb->nt_level[lhs] < lvl;
        int pp = paren ? tb->family_paren[fam] : -1;
        if (paren && pp < 0) return false;
        if (paren) {
            /* the coercion's terminals before its category */
            int len = tb->prod_len[pp];
            for (int k = 0; k < len && item_at(tb, pp, k) >= 0; k++) {
                const char *s = tb->terminal_text[item_at(tb, pp, k)];
                if (!tok_push(p, s, strlen(s), false, true, false)) return false;
            }
        }
        if (render_items(p, prod, env, 0, 0, r)) {
            if (paren) {
                int len = tb->prod_len[pp], k = 0;
                while (k < len && item_at(tb, pp, k) >= 0) k++;
                for (k = k + 1; k < len; k++) {
                    const char *s = tb->terminal_text[item_at(tb, pp, k)];
                    if (!tok_push(p, s, strlen(s), true, false, false)) return false;
                }
            }
            return true;
        }
        p->n_toks = mark_t;
        p->n_text = mark_x;
    }
    return false;
}

typedef struct {
    char *s;
    size_t n, cap;
} SB;

static bool sb_put(SB *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t nc = (b->n + n + 1) * 2;
        char *t = realloc(b->s, nc);
        if (!t) return false;
        b->s = t;
        b->cap = nc;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
    return true;
}

static void join(Pr *p, size_t from, size_t to, SB *out) {
    bool nospace = true;
    for (size_t i = from; i < to; i++) {
        PTok *t = &p->toks[i];
        if (t->glue) { nospace = true; continue; }
        if (!(nospace || t->gl)) sb_put(out, " ", 1);
        sb_put(out, p->text + t->off, t->len);
        nospace = t->gr;
    }
}

static bool tok_is(Pr *p, size_t i, const char *s) {
    PTok *t = &p->toks[i];
    return !t->glue && t->len == strlen(s) && memcmp(p->text + t->off, s, t->len) == 0;
}

/* a block's brace (a literal's braces, f{a}, carry no terminal) */
static bool tok_is_block(Pr *p, size_t i, const char *s) {
    return tok_is(p, i, s) && p->toks[i].term >= 0;
}

#define LAYOUT_WIDTH 96

/* A statement too long for one line: a brace group stays flat when it fits,
   otherwise it breaks after "{", after each comma directly inside it and
   before "}"; a let chain breaks after "in".  Layout never changes the atoms. */
static void layout(Pr *p, size_t from, size_t to, SB *out) {
    char stack[256];
    int sp = 0, ind = 0;
    bool nospace = true, fresh = false;
    size_t col = 0;
    for (size_t i = from; i < to; i++) {
        PTok *t = &p->toks[i];
        if (t->glue) { nospace = true; continue; }
        const char *sep = fresh ? "" : ((nospace || t->gl) ? "" : " ");
        if (tok_is_block(p, i, "{")) {
            size_t j = i, depth = 0;
            for (; j < to; j++) {
                if (tok_is_block(p, j, "{")) depth++;
                else if (tok_is_block(p, j, "}") && --depth == 0) break;
            }
            SB flat = {0};
            join(p, i, j < to ? j + 1 : to, &flat);
            if (col + strlen(sep) + flat.n <= LAYOUT_WIDTH) {
                sb_put(out, sep, strlen(sep));
                sb_put(out, flat.s ? flat.s : "", flat.n);
                col += strlen(sep) + flat.n;
                free(flat.s);
                i = j < to ? j : to - 1;
                nospace = false;
                fresh = false;
                continue;
            }
            free(flat.s);
            ind++;
            sb_put(out, sep, strlen(sep));
            sb_put(out, "{\n", 2);
            for (int k = 0; k < ind; k++) sb_put(out, "  ", 2);
            if (sp < 255) stack[sp++] = '{';
            nospace = true;
            fresh = true;
            col = (size_t)(2 * ind);
            continue;
        }
        if (tok_is_block(p, i, "}") && sp > 0 && stack[sp - 1] == '{') {
            ind--;
            sb_put(out, "\n", 1);
            for (int k = 0; k < ind; k++) sb_put(out, "  ", 2);
            sb_put(out, "}", 1);
            sp--;
            nospace = false;
            fresh = false;
            col = (size_t)(2 * ind + 1);
            continue;
        }
        sb_put(out, sep, strlen(sep));
        sb_put(out, p->text + t->off, t->len);
        col += strlen(sep) + t->len;
        if (tok_is(p, i, "(") || tok_is(p, i, "[") || tok_is(p, i, "{")) {
            if (sp < 255) stack[sp++] = '(';
            nospace = t->gr;
            fresh = false;
        } else if (tok_is(p, i, ")") || tok_is(p, i, "]") || tok_is(p, i, "}")) {
            if (sp > 0) sp--;
            nospace = t->gr;
            fresh = false;
        } else if ((t->term >= 0 && p->tb->terminal_break && p->tb->terminal_break[t->term] && sp > 0 &&
                    stack[sp - 1] == '{') ||
                   (tok_is(p, i, "in") && !(sp > 0 && stack[sp - 1] != '{'))) {
            sb_put(out, "\n", 1);
            for (int k = 0; k < ind; k++) sb_put(out, "  ", 2);
            nospace = true;
            fresh = true;
            col = (size_t)(2 * ind);
        } else {
            nospace = t->gr;
            fresh = false;
        }
    }
}

/* A statement of a presentation with layout.  A block (the grammar's "{" ...
   "}" after a layout word) is written by indentation when nothing follows it
   on its line except the end of the statement, the separator of an enclosing
   indented block, or a stop word; otherwise with explicit braces, which the
   layout rules read the same way.  Indented blocks open 4 columns right of
   the line that opens them, so the layout rules give back the braces and
   separators the printer left out. */
static void layout_blocks(Pr *p, size_t from, size_t to, SB *out) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    size_t n = to - from;
    bool *implicit = calloc(n ? n : 1, sizeof *implicit); /* by the index of "{" */
    size_t *match = calloc(n ? n : 1, sizeof *match);     /* "}" -> its "{" */
    size_t *stk = calloc(n ? n : 1, sizeof *stk);
    bool *stk_imp = calloc(n ? n : 1, sizeof *stk_imp);
    if (!implicit || !match || !stk || !stk_imp) goto out;
    size_t sp = 0;
    for (size_t i = 0; i < n; i++) {
        PTok *t = &p->toks[from + i];
        if (t->glue) continue;
        if (t->term == tb->layout_open) stk[sp++] = i;
        else if (t->term == tb->layout_close && sp > 0) match[i] = stk[--sp];
    }
    /* which blocks may be indented, from the end */
    {
        /* A block right before a stop word is always indented: under BNFC's
           layout rules a stop word after an explicit block would close the
           enclosing indented block instead. */
        bool tail = true;
        sp = 0;
        for (size_t j = n; j-- > 0;) {
            PTok *t = &p->toks[from + j];
            if (t->glue) continue;
            if (t->term == tb->layout_close) {
                implicit[match[j]] = tail;
                stk_imp[sp++] = tail;
                continue;
            }
            if (t->term == tb->layout_open) {
                if (sp > 0) sp--;
                tail = false;
                continue;
            }
            if (t->term >= 0 && tb->terminal_layout[t->term] == 2) { tail = true; continue; }
            if (t->term == tb->layout_sep && sp > 0) { tail = stk_imp[sp - 1]; continue; }
            tail = false;
        }
    }
    /* write */
    {
        size_t indent_stack[256];
        int isp = 0;
        size_t line_indent = 0;
        bool nospace = true;
        sp = 0;
        for (size_t i = 0; i < n; i++) {
            PTok *t = &p->toks[from + i];
            if (t->glue) { nospace = true; continue; }
            if (t->term == tb->layout_open) {
                if (implicit[i]) {
                    if (isp < 256) indent_stack[isp++] = line_indent;
                    stk_imp[sp++] = true;
                    line_indent += 4;
                    sb_put(out, "\n", 1);
                    for (size_t k = 0; k < line_indent; k++) sb_put(out, " ", 1);
                    nospace = true;
                } else {
                    stk_imp[sp++] = false;
                    if (!nospace) sb_put(out, " ", 1);
                    sb_put(out, "{", 1);
                    nospace = false;
                }
                continue;
            }
            if (t->term == tb->layout_close) {
                bool imp = sp > 0 ? stk_imp[--sp] : false;
                if (imp) {
                    if (isp > 0) line_indent = indent_stack[--isp];
                    /* a stop word follows on a line of its own */
                    size_t j = i + 1;
                    while (j < n && p->toks[from + j].glue) j++;
                    if (j < n && p->toks[from + j].term >= 0 && tb->terminal_layout[p->toks[from + j].term] == 2) {
                        sb_put(out, "\n", 1);
                        for (size_t k = 0; k < line_indent; k++) sb_put(out, " ", 1);
                        nospace = true;
                    }
                } else {
                    sb_put(out, " }", 2);
                    nospace = false;
                }
                continue;
            }
            if (t->term == tb->layout_sep && sp > 0) {
                if (stk_imp[sp - 1]) {
                    sb_put(out, "\n", 1);
                    for (size_t k = 0; k < line_indent; k++) sb_put(out, " ", 1);
                    nospace = true;
                } else {
                    sb_put(out, ";", 1);
                    nospace = false;
                }
                continue;
            }
            bool layout_word = t->term >= 0 && tb->terminal_layout[t->term] == 1;
            if (!(nospace || t->gl || layout_word)) sb_put(out, " ", 1);
            sb_put(out, p->text + t->off, t->len);
            nospace = t->gr;
        }
    }
out:
    free(implicit);
    free(match);
    free(stk);
    free(stk_imp);
}

/* A canonical view of atoms for comparing readers: every leaf is tagged with
   its kind and written as a quoted string, so no two atoms share a view.
   (var "n") (sym "s") (str "...") (gnd "text"), expressions in parentheses,
   a !-statement as (run atom). */
static void put_quoted(SB *b, const char *s, size_t n) {
    sb_put(b, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { sb_put(b, "\\", 1); sb_put(b, &c, 1); }
        else if (c == '\n') sb_put(b, "\\n", 2);
        else if (c == '\t') sb_put(b, "\\t", 2);
        else if (c == '\r') sb_put(b, "\\r", 2);
        else sb_put(b, &c, 1);
    }
    sb_put(b, "\"", 1);
}

/* the variables of one statement, numbered by first occurrence */
typedef struct {
    VarId *ids;
    size_t n, cap;
} VarOrder;

static size_t var_ordinal(VarOrder *vo, VarId v) {
    for (size_t i = 0; i < vo->n; i++)
        if (var_id_eq(vo->ids[i], v)) return i;
    if (vo->n == vo->cap) {
        size_t nc = vo->cap ? vo->cap * 2 : 16;
        VarId *ids = realloc(vo->ids, nc * sizeof *ids);
        if (!ids) return SIZE_MAX;
        vo->ids = ids;
        vo->cap = nc;
    }
    vo->ids[vo->n] = v;
    return vo->n++;
}

static void canonical_atom(TermUniverse *u, Arena *a, AtomId id, SB *out, VarOrder *vo) {
    switch (tu_kind(u, id)) {
    case ATOM_VAR: {
        SymbolId s = tu_sym(u, id);
        char num[32];
        sb_put(out, "(var ", 5);
        put_quoted(out, symbol_bytes(g_symbols, s), symbol_len(g_symbols, s));
        snprintf(num, sizeof num, " %zu)", var_ordinal(vo, tu_var_id(u, id)));
        sb_put(out, num, strlen(num));
        return;
    }
    case ATOM_SYMBOL: {
        SymbolId s = tu_sym(u, id);
        sb_put(out, "(sym ", 5);
        put_quoted(out, symbol_bytes(g_symbols, s), symbol_len(g_symbols, s));
        sb_put(out, ")", 1);
        return;
    }
    case ATOM_GROUNDED: {
        if (tu_ground_kind(u, id) == GV_STRING) {
            Atom *atom = term_universe_get_atom(u, id);
            sb_put(out, "(str ", 5);
            put_quoted(out, atom->ground.sval, atom->ground.slen);
            sb_put(out, ")", 1);
            return;
        }
        char buf[64];
        const char *text;
        if (tu_ground_kind(u, id) == GV_FLOAT && float_spelling(tu_float(u, id), buf, sizeof buf)) text = buf;
        else text = term_universe_atom_to_parseable_string(a, u, id);
        sb_put(out, "(gnd ", 5);
        put_quoted(out, text ? text : "?", text ? strlen(text) : 1);
        sb_put(out, ")", 1);
        return;
    }
    case ATOM_EXPR: {
        CettaExprLen n = tu_arity(u, id);
        sb_put(out, "(", 1);
        for (CettaExprLen k = 0; k < n; k++) {
            if (k) sb_put(out, " ", 1);
            canonical_atom(u, a, tu_child(u, id, (CettaExprIndex)k), out, vo);
        }
        sb_put(out, ")", 1);
        return;
    }
    }
}

char *gslt_presentation_v1_canonical_ids(TermUniverse *universe, const AtomId *ids, size_t n) {
    SB out = {0};
    Arena a;
    arena_init(&a);
    sb_put(&out, "", 0);
    VarOrder vo = {0};
    for (size_t i = 0; i < n; i++) {
        vo.n = 0; /* a statement is a variable scope */
        if (tu_kind(universe, ids[i]) == ATOM_SYMBOL && symbol_len(g_symbols, tu_sym(universe, ids[i])) == 1 &&
            symbol_bytes(g_symbols, tu_sym(universe, ids[i]))[0] == '!' && i + 1 < n) {
            sb_put(&out, "(run ", 5);
            canonical_atom(universe, &a, ids[++i], &out, &vo);
            sb_put(&out, ")\n", 2);
            continue;
        }
        canonical_atom(universe, &a, ids[i], &out, &vo);
        sb_put(&out, "\n", 1);
    }
    free(vo.ids);
    arena_free(&a);
    return out.s;
}

/* a kept comment in the presentation's comment syntax, a line at a time */
static void put_comment(const GSLTPresentationTablesV1 *tb, const char *text, SB *out) {
    const char *open = tb->n_line_comments ? tb->line_comment[0] : NULL;
    if (!open && tb->n_block_comments && !strstr(text, tb->block_close[0])) {
        sb_put(out, tb->block_open[0], strlen(tb->block_open[0]));
        sb_put(out, text, strlen(text));
        sb_put(out, tb->block_close[0], strlen(tb->block_close[0]));
        sb_put(out, "\n", 1);
        return;
    }
    if (!open) return;
    const char *line = text;
    for (;;) {
        const char *nl = strchr(line, '\n');
        size_t ln = nl ? (size_t)(nl - line) : strlen(line);
        sb_put(out, open, strlen(open));
        if (ln && line[0] != ' ') sb_put(out, " ", 1);
        sb_put(out, line, ln);
        sb_put(out, "\n", 1);
        if (!nl) break;
        line = nl + 1;
    }
}

void gslt_presentation_v1_put_metta_comment(const char *text, void *file) {
    FILE *f = file;
    const char *line = text;
    for (;;) {
        const char *nl = strchr(line, '\n');
        size_t ln = nl ? (size_t)(nl - line) : strlen(line);
        fputc(';', f);
        if (ln && line[0] != ' ') fputc(' ', f);
        fwrite(line, 1, ln, f);
        fputc('\n', f);
        if (!nl) break;
        line = nl + 1;
    }
}

static char *print_ids_internal(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                                TermUniverse *universe, const AtomId *ids, size_t n,
                                const GSLTPresentationCommentV1 *comments, size_t n_comments, char *err,
                                size_t errsz);

static bool g_soft_keywords = true;

static bool symbol_named(TermUniverse *u, AtomId a, const char *name) {
    if (tu_kind(u, a) != ATOM_SYMBOL) return false;
    SymbolId s = tu_sym(u, a);
    size_t n = strlen(name);
    return symbol_len(g_symbols, s) == n && memcmp(symbol_bytes(g_symbols, s), name, n) == 0;
}

/* The document's own declarations (: f (-> A1 .. An R)) give the positions
   of f's arguments: Ai = Atom or (Data T) holds, every other type runs. */
static bool collect_doc_sigs(Pr *p, const AtomId *ids, size_t n, AtomId bang) {
    TermUniverse *u = p->u;
    size_t cap = 0;
    for (size_t i = 0; i < n; i++) {
        if (ids[i] == bang && i + 1 < n) { i++; continue; }
        AtomId d = ids[i];
        if (tu_kind(u, d) != ATOM_EXPR || tu_arity(u, d) != 3 || !symbol_named(u, tu_child(u, d, 0), ":"))
            continue;
        AtomId ty = tu_child(u, d, 2);
        if (tu_kind(u, ty) != ATOM_EXPR || tu_arity(u, ty) < 2 || !symbol_named(u, tu_child(u, ty, 0), "->"))
            continue;
        size_t na = (size_t)tu_arity(u, ty) - 2u;
        if (na > 32) continue;
        if (p->n_sigs == cap) {
            cap = cap ? cap * 2 : 16;
            DocSig *s = realloc(p->sigs, cap * sizeof *s);
            if (!s) return false;
            p->sigs = s;
        }
        DocSig *s = &p->sigs[p->n_sigs++];
        s->head = tu_child(u, d, 1);
        s->n = (uint8_t)na;
        for (size_t j = 0; j < na; j++) {
            /* one rule with the builtins' declarations */
            AtomId a = tu_child(u, ty, (CettaExprIndex)(1u + j));
            bool expr = tu_kind(u, a) == ATOM_EXPR;
            AtomId h = expr && tu_arity(u, a) > 0 ? tu_child(u, a, 0) : a;
            const char *hn = NULL;
            size_t hl = 0;
            if (tu_kind(u, h) == ATOM_SYMBOL) {
                hn = symbol_bytes(g_symbols, tu_sym(u, h));
                hl = symbol_len(g_symbols, tu_sym(u, h));
            }
            s->roles[j] = gslt_presentation_v1_role_of_type(hn, hl, expr ? (size_t)tu_arity(u, a) : 0u);
        }
    }
    return true;
}

void gslt_presentation_v1_set_soft_keywords(bool enabled) {
    g_soft_keywords = enabled;
}

char *gslt_presentation_v1_print_ids(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                                     TermUniverse *universe, const AtomId *ids, size_t n, char *err,
                                     size_t errsz) {
    return print_ids_internal(tb, grounding, universe, ids, n, NULL, 0, err, errsz);
}

char *gslt_presentation_v1_print_ids_comments(const GSLTPresentationTablesV1 *tb,
                                              GSLTPresentationGroundingKindV1 grounding, TermUniverse *universe,
                                              const AtomId *ids, size_t n,
                                              const GSLTPresentationCommentV1 *comments, size_t n_comments,
                                              char *err, size_t errsz) {
    return print_ids_internal(tb, grounding, universe, ids, n, comments, n_comments, err, errsz);
}

static char *print_ids_internal(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                                TermUniverse *universe, const AtomId *ids, size_t n,
                                const GSLTPresentationCommentV1 *comments, size_t n_comments, char *err,
                                size_t errsz) {
    Pr p;
    SB out = {0};
    bool ok = false;
    size_t next_comment = 0;
    if (err && errsz) err[0] = '\0';
    if (!tb || !universe || (n && !ids)) return NULL;
    memset(&p, 0, sizeof p);
    p.tb = tb;
    p.u = universe;
    p.err = err;
    p.errsz = errsz;
    arena_init(&p.arena);
    if (!ground_open(&p.g, grounding, universe) || !ground_begin(&p.g)) goto done;
    p.consts = calloc(tb->n_tconst ? tb->n_tconst : 1u, sizeof *p.consts);
    if (!p.consts) goto done;
    for (uint16_t k = 0; k < tb->n_tconst; k++) {
        size_t len = tb->tconst_len[k] & 0x7fffffffu;
        p.consts[k] = (tb->tconst_len[k] & 0x80000000u) ? ground_string(&p.g, (const uint8_t *)tb->tconst[k], len)
                                                        : ground_word(&p.g, (const uint8_t *)tb->tconst[k], len);
    }
    for (uint16_t q = 0; q < tb->n_productions; q++) {
        if (tb->prod_template[q] < 0) continue;
        const int32_t *pc = tb->tcode + tb->prod_template[q];
        if (t_has_run(&pc)) p.has_run = true;
    }
    AtomId bang = ground_word(&p.g, (const uint8_t *)"!", 1u);
    p.grounding = grounding;
    if (!collect_doc_sigs(&p, ids, n, bang)) goto done;
    sb_put(&out, "", 0);
    for (size_t i = 0; i < n; i++) {
        size_t first = i;
        while (next_comment < n_comments && comments[next_comment].before <= first)
            put_comment(tb, comments[next_comment++].text, &out);
        PT t = {P_ATOM, ids[i], 0};
        if (p.has_run && ids[i] == bang && i + 1 < n) {
            t.kind = P_RUN;
            t.id = ids[++i];
        }
        /* First with keyword-spelled symbols written bare; kept only when
           the statement reads back as the same atoms. */
        SB stmt = {0};
        bool done_stmt = false;
        for (int attempt = g_soft_keywords ? 0 : 1; attempt < 2 && !done_stmt; attempt++) {
            p.soft = attempt == 0;
            p.used_soft = false;
            p.n_toks = 0;
            p.n_text = 0;
            free(stmt.s);
            memset(&stmt, 0, sizeof stmt);
            if (!render(&p, t, tb->statement_nt, role_plain(GSLT_PRES_ROLE_RUN))) {
                if (attempt == 0) continue;
                char *txt = term_universe_atom_to_string(&p.arena, universe, t.id);
                set_error(err, errsz, "no view of the %s presentation prints %s%s", tb->name,
                          t.kind == P_RUN ? "!" : "", txt ? txt : "an atom");
                goto done;
            }
            if (tb->has_layout) {
                layout_blocks(&p, 0, p.n_toks, &stmt);
            } else {
                SB line = {0};
                join(&p, 0, p.n_toks, &line);
                if (line.n <= LAYOUT_WIDTH) sb_put(&stmt, line.s ? line.s : "", line.n);
                else layout(&p, 0, p.n_toks, &stmt);
                free(line.s);
            }
            sb_put(&stmt, tb->statement_end, strlen(tb->statement_end));
            if (attempt == 0 && p.used_soft) {
                AtomId *back = NULL;
                char rerr[256] = "";
                int nb = gslt_presentation_v1_read_ids(tb, grounding, stmt.s, stmt.n, universe, &back,
                                                       rerr, sizeof rerr);
                bool same = false;
                if (nb == (int)(i - first + 1)) {
                    char *a = gslt_presentation_v1_canonical_ids(universe, ids + first, i - first + 1);
                    char *b = gslt_presentation_v1_canonical_ids(universe, back, (size_t)nb);
                    same = a && b && strcmp(a, b) == 0;
                    free(a);
                    free(b);
                }
                free(back);
                if (!same) continue;
            }
            done_stmt = true;
        }
        sb_put(&out, stmt.s ? stmt.s : "", stmt.n);
        free(stmt.s);
    }
    while (next_comment < n_comments) put_comment(tb, comments[next_comment++].text, &out);
    ok = true;
done:
    free(p.toks);
    free(p.text);
    free(p.consts);
    free(p.sigs);
    arena_free(&p.arena);
    ground_close(&p.g);
    if (!ok) { free(out.s); return NULL; }
    return out.s;
}

/* ------------------------------------------------------------ the lint */

/* The role of every node of the parse tree, by the printer's own rule
   (child_role), from the statement down; a look that its position's role does
   not admit is reported.  Reading is position-free, so a wrong look never
   changes the atoms: the report says how the text misleads its reader. */
static void lint_report(SB *out, const uint8_t *src, uint32_t pos, const char *what, const char *text,
                        size_t text_len) {
    unsigned l = 1, c = 1;
    if (pos != UINT32_MAX) line_col(src, pos, &l, &c);
    char head[96];
    snprintf(head, sizeof head, "line %u, column %u: ", l, c);
    sb_put(out, head, strlen(head));
    sb_put(out, text, text_len);
    sb_put(out, what, strlen(what));
    sb_put(out, "\n", 1);
}

/* the report on node ND written in a look of production Q, in role R */
static void lint_look(Pr *p, const LNode *nd, int q, Role r, const uint8_t *src, SB *out, int *found) {
    uint8_t look = p->tb->prod_look ? p->tb->prod_look[q] : 0;
    if (!look || nd->atom == CETTA_ATOM_ID_NONE) return;
    PT t = {P_ATOM, nd->atom, 0};
    const char *what = NULL;
    bool data_look = look == GSLT_PRES_LOOK_BIT(GSLT_PRES_ROLE_DATA);
    bool call_look = (look & GSLT_PRES_LOOK_BIT(GSLT_PRES_ROLE_RUN)) &&
                     !(look & GSLT_PRES_LOOK_BIT(GSLT_PRES_ROLE_DATA));
    bool list_look = !data_look && !call_look;
    if (data_look && r.role == GSLT_PRES_ROLE_RUN)
        what = " is written as data where its position runs: it is a call";
    else if (list_look && r.role == GSLT_PRES_ROLE_RUN && guard_ok(p, GSLT_PRES_GUARD_CALLABLE_HEAD, t))
        what = " is written as a list where its position runs: it is a call of its first item";
    else if (call_look && r.role == GSLT_PRES_ROLE_DATA)
        what = " is written as a call where its position holds it as data";
    else if (call_look && r.role == GSLT_PRES_ROLE_LIST)
        what = " is written as a call where its position holds a list";
    if (!what) return;
    char *txt = term_universe_atom_to_string(&p->arena, p->u, nd->atom);
    lint_report(out, src, nd->pos, what, txt ? txt : "an expression", txt ? strlen(txt) : 13);
    (*found)++;
}

static void lint_walk(Pr *p, const LintTree *lt, uint32_t node, Role r, const uint8_t *src, SB *out,
                      int *found) {
    const GSLTPresentationTablesV1 *tb = p->tb;
    /* a list's rest is walked by this loop rather than by recursion, so a
       document of many statements needs no deep stack */
    for (;;) {
        const LNode *nd = &lt->nodes[node];
        if (nd->prod < 0) return;
        int q = nd->prod;
        uint8_t kind = tb->prod_kind[q];
        if (kind == GSLT_PRES_PROD_TOKEN) return;
        if (kind == GSLT_PRES_PROD_LIST_CONS) {
            uint32_t kids[2];
            int nk = 0;
            for (uint32_t k = 0; k < nd->n_kids && nk < 2; k++)
                if (is_arg_item(tb, q, (int)k)) kids[nk++] = lt->kids[nd->first_kid + k];
            if (nk != 2) return;
            lint_walk(p, lt, kids[0], item_role(r, 0), src, out, found);
            node = kids[1];
            r = role_shift(r);
            continue;
        }
        if (kind == GSLT_PRES_PROD_COERCION) {
            /* a coercion (parentheses among them) keeps the role */
            for (uint32_t k = 0; k < nd->n_kids; k++)
                lint_walk(p, lt, lt->kids[nd->first_kid + k], r, src, out, found);
            return;
        }
        lint_look(p, nd, q, r, src, out, found);
        /* the children's roles, as the printer gives them */
        PT env[32];
        int argk = 0;
        for (int k = 0; k < 32; k++) env[k].kind = P_NONE;
        for (uint32_t k = 0; k < nd->n_kids && argk < 32; k++) {
            if (!is_arg_item(tb, q, (int)k)) continue;
            const LNode *kid = &lt->nodes[lt->kids[nd->first_kid + k]];
            if (kid->atom != CETTA_ATOM_ID_NONE) {
                env[argk].kind = P_ATOM;
                env[argk].id = kid->atom;
                env[argk].off = 0;
            }
            argk++;
        }
        argk = 0;
        for (uint32_t k = 0; k < nd->n_kids && argk < 32; k++) {
            if (!is_arg_item(tb, q, (int)k)) continue;
            Role sub = child_role(p, q, argk, env, r);
            lint_walk(p, lt, lt->kids[nd->first_kid + k], sub, src, out, found);
            argk++;
        }
        return;
    }
}

int gslt_presentation_v1_check_looks(const GSLTPresentationTablesV1 *tb, GSLTPresentationGroundingKindV1 grounding,
                                     const char *text, size_t len, TermUniverse *universe, char **report,
                                     char *err, size_t errsz) {
    LintTree lt;
    memset(&lt, 0, sizeof lt);
    AtomId *ids = NULL;
    *report = NULL;
    int n = read_ids_internal(tb, grounding, text, len, universe, &ids, NULL, NULL, err, errsz, &lt);
    if (n < 0) {
        free(lt.nodes);
        free(lt.kids);
        return -1;
    }
    Pr p;
    memset(&p, 0, sizeof p);
    p.tb = tb;
    p.u = universe;
    p.err = err;
    p.errsz = errsz;
    arena_init(&p.arena);
    int found = 0;
    SB out = {0};
    sb_put(&out, "", 0);
    if (ground_open(&p.g, grounding, universe) && ground_begin(&p.g)) {
        AtomId bang = ground_word(&p.g, (const uint8_t *)"!", 1u);
        if (collect_doc_sigs(&p, ids, (size_t)n, bang) && lt.n)
            lint_walk(&p, &lt, lt.root, role_plain(GSLT_PRES_ROLE_RUN), (const uint8_t *)text, &out, &found);
    }
    ground_close(&p.g);
    free(p.sigs);
    arena_free(&p.arena);
    free(ids);
    free(lt.nodes);
    free(lt.kids);
    *report = out.s;
    return found;
}
