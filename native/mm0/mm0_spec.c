/* mm0_spec.c - reading a .mm0 specification against a checked MMB file.
 *
 * This follows mm0-c/parser.c: statements are read in step with the MMB
 * statement loop, each math string is parsed by the notation-driven operator
 * precedence parser of mm0.md "Secondary parsing", and the parse is compared
 * with the declaration's unify stream (parser.c check_expr).  Differences:
 *
 *  - Distinct UDummy commands must match distinct specification dummies
 *    (the dummy-injectivity repair of mm0-c-fix/; without it a definition
 *    with two dummies matches a specification that uses one dummy twice).
 *  - Unnamed arrow-type arguments get their own variables; parser.c leaves
 *    their unify-heap entries pointing at a previous statement's store.
 *  - A notation must use each of its variables exactly once, and a token may
 *    begin at most one prefix notation and be at most one infix operator
 *    (mm0.md "Notations"; mm0-rs reports these, parser.c overwrites).
 *  - Coercion paths are closed transitively and the undirected coercion
 *    graph must be acyclic (mm0.md "Secondary parsing").
 *  - The expression parser keeps its own stack, so nesting depth is limited
 *    by memory rather than by the C stack.
 *  - input/output statements are reported as unsupported.
 */
#include "mm0_internal.h"

#include <stdlib.h>

#define PREC_MAX UINT32_MAX
#define APP_PREC 1024u
#define NO_INDEX UINT32_MAX

enum { DELIM_LEFT = 1, DELIM_RIGHT = 2, DELIM_SPACE = 4 };

typedef struct { uint8_t constant; uint32_t value; uint32_t prec; } Literal;
typedef struct { uint32_t term, nargs, nlits; Literal *lits; } Prefix;
typedef struct { uint32_t term; uint8_t rassoc; } Infix;
typedef struct {
    const char *text;
    uint32_t len;
    uint32_t prefix, infix;   /* NO_INDEX when absent */
    uint32_t prec;
    uint8_t prec_set;
} Token;
typedef struct { uint8_t tag; uint32_t term, via; } Coe;
enum { COE_EMPTY, COE_ONE, COE_TRANS };
typedef struct { uint32_t prec; uint8_t rassoc; } PrecAssoc;
typedef struct { Mm0Expr *expr; uint32_t sort; } ExprSort;

struct Mm0Spec {
    const char *text;
    size_t len, pos;
    Mm0Steps *steps;
    Mm0Error *error;
    bool text_ok;

    Mm0Arena arena;          /* notation tables, kept for the whole file */
    Mm0Arena scratch;        /* the current statement's parse */
    Mm0NameMap sorts, terms, thms, vars, token_map;
    Token *tokens; size_t ntokens, cap_tokens;
    Prefix *prefixes; size_t nprefixes, cap_prefixes;
    Infix *infixes; size_t ninfixes, cap_infixes;
    PrecAssoc *prec_assoc; size_t nprec, cap_prec;
    uint8_t delims[256];
    uint32_t open_paren, close_paren;

    /* coercions: coe[s1 * nsorts + s2]; prov[s]: the provable sort reached */
    uint32_t nsorts;
    Coe *coe;
    uint32_t *prov, *component;

    /* the current statement's variables */
    uint32_t nvars, nargs;
    uint32_t *var_sorts; size_t cap_var_sorts;
    const char **var_names; size_t cap_var_names;
    const char **dummy_order; size_t ndummy_order, cap_dummy_order;
    Mm0Expr **uheap; size_t uhp, cap_uheap;
    Mm0Expr **ustack; size_t usp, cap_ustack;

    struct Frame *frames; size_t nframes, cap_frames;
};

/* ---- errors ------------------------------------------------------------ */

static bool fail(Mm0Spec *s, Mm0Status status, const char *reason) {
    Mm0Error *e = s->error;
    if (e && e->status == MM0_OK) {
        e->status = status;
        e->reason = reason;
        uint32_t line = 1, column = 1;
        for (size_t i = 0; i < s->pos && i < s->len; ++i) {
            if (s->text[i] == '\n') { ++line; column = 1; }
            else ++column;
        }
        e->line = line;
        e->column = column;
    }
    return false;
}
#define ENSURE(cond, reason) \
    do { if (__builtin_expect(!(cond), 0)) return fail(s, MM0_REFUSED, reason); } while (0)
#define CHECK(expr) do { if (!(expr)) return false; } while (0)
#define GROW(vec, cap, need) \
    do { if (!MM0_RESERVE(vec, cap, need)) return fail(s, MM0_NOMEM, "specification allocation"); } while (0)

static bool step(Mm0Spec *s) {
    Mm0Status status = mm0_step(s->steps);
    if (status == MM0_OK) return true;
    return fail(s, status, status == MM0_LIMIT ? "step budget exhausted" : "interrupted");
}

/* ---- lexical structure (mm0.md "Lexical structure"; parser.c ws/ch/kw) -- */

static int cur(const Mm0Spec *s) { return s->pos < s->len ? (unsigned char)s->text[s->pos] : 0; }
static int peek(const Mm0Spec *s, size_t k) {
    return s->pos + k < s->len ? (unsigned char)s->text[s->pos + k] : 0;
}
static bool is_ws(int c) { return c == ' ' || c == '\n'; }
static bool ident_start(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static bool ident_rest(int c) { return ident_start(c) || (c >= '0' && c <= '9'); }

/* whitespace and line comments */
static void ws(Mm0Spec *s) {
    for (;;) {
        int c = cur(s);
        if (c == '-' && peek(s, 1) == '-') {
            while (s->pos < s->len && s->text[s->pos] != '\n') ++s->pos;
            if (s->pos < s->len) ++s->pos;
        } else if (is_ws(c)) {
            ++s->pos;
        } else {
            return;
        }
    }
}

static bool ch(Mm0Spec *s, int c) {
    if (cur(s) != c || c == 0) return false;
    ++s->pos;
    ws(s);
    return true;
}

typedef struct { const char *text; size_t len; bool blank; } Ident;

/* identifier ::= [a-zA-Z_][a-zA-Z0-9_]*  ('_' alone is blank) */
static bool ident(Mm0Spec *s, Ident *out) {
    size_t start = s->pos;
    ENSURE(ident_start(cur(s)), "expecting identifier");
    while (ident_rest(cur(s))) ++s->pos;
    *out = (Ident){s->text + start, s->pos - start, s->pos - start == 1 && s->text[start] == '_'};
    ws(s);
    return true;
}

static bool named_ident(Mm0Spec *s, Ident *out) {
    CHECK(ident(s, out));
    ENSURE(!out->blank, "expecting an identifier, not '_'");
    return true;
}

typedef enum {
    KW_NONE, KW_AXIOM, KW_COERCION, KW_DEF, KW_DELIMITER, KW_FREE, KW_INFIXL,
    KW_INFIXR, KW_INPUT, KW_MAX, KW_NOTATION, KW_OUTPUT, KW_PREC, KW_PREFIX,
    KW_PROVABLE, KW_PURE, KW_SORT, KW_STRICT, KW_TERM, KW_THEOREM
} Keyword;

/* A keyword is an identifier spelled as one (mm0.md "Pseudo-keywords"). */
static Keyword kw(Mm0Spec *s) {
    static const struct { const char *text; Keyword kw; } words[] = {
        {"axiom", KW_AXIOM}, {"coercion", KW_COERCION}, {"def", KW_DEF},
        {"delimiter", KW_DELIMITER}, {"free", KW_FREE}, {"infixl", KW_INFIXL},
        {"infixr", KW_INFIXR}, {"input", KW_INPUT}, {"max", KW_MAX},
        {"notation", KW_NOTATION}, {"output", KW_OUTPUT}, {"prec", KW_PREC},
        {"prefix", KW_PREFIX}, {"provable", KW_PROVABLE}, {"pure", KW_PURE},
        {"sort", KW_SORT}, {"strict", KW_STRICT}, {"term", KW_TERM},
        {"theorem", KW_THEOREM},
    };
    if (!ident_start(cur(s))) return KW_NONE;
    size_t end = s->pos;
    while (end < s->len && ident_rest((unsigned char)s->text[end])) ++end;
    size_t n = end - s->pos;
    for (size_t i = 0; i < sizeof words / sizeof words[0]; ++i) {
        if (strlen(words[i].text) == n && memcmp(words[i].text, s->text + s->pos, n) == 0) {
            s->pos = end;
            ws(s);
            return words[i].kw;
        }
    }
    return KW_NONE;
}

/* precedence-lvl ::= number | 'max' */
static bool prec(Mm0Spec *s, uint32_t *out) {
    int c = cur(s);
    if (c < '0' || c > '9') {
        ENSURE(kw(s) == KW_MAX, "expected a number or 'max'");
        *out = PREC_MAX;
        return true;
    }
    uint64_t value = 0;
    while ((c = cur(s)) >= '0' && c <= '9') {
        value = value * 10 + (uint64_t)(c - '0');
        ENSURE(value <= (UINT64_C(1) << 31), "number out of range");
        ++s->pos;
    }
    ENSURE(!ident_rest(cur(s)), "invalid number");
    ws(s);
    *out = (uint32_t)value;
    return true;
}

/* ---- tokens ------------------------------------------------------------ */

static bool token_index(Mm0Spec *s, const char *text, size_t len, uint32_t *out, bool create) {
    uint32_t index;
    if (mm0_names_get(&s->token_map, text, len, &index)) { *out = index; return true; }
    if (!create) { *out = NO_INDEX; return true; }
    GROW(s->tokens, s->cap_tokens, s->ntokens + 1);
    ENSURE(s->ntokens < NO_INDEX, "too many tokens");
    index = (uint32_t)s->ntokens;
    s->tokens[index] = (Token){text, (uint32_t)len, NO_INDEX, NO_INDEX, 0, 0};
    if (!mm0_names_put(&s->token_map, text, len, index))
        return fail(s, MM0_NOMEM, "specification allocation");
    ++s->ntokens;
    *out = index;
    return true;
}

/* constant ::= '$' whitechar* constant-tk whitechar* '$' (parser.c parse_new_token) */
static bool new_token(Mm0Spec *s, uint32_t *out) {
    ENSURE(cur(s) == '$', "expecting '$'");
    do ++s->pos; while (is_ws(cur(s)));
    size_t start = s->pos;
    ENSURE(cur(s) != '$' && cur(s) != 0, "expecting token character");
    while (cur(s) != 0 && !is_ws(cur(s)) && cur(s) != '$') ++s->pos;
    size_t end = s->pos;
    while (is_ws(cur(s))) ++s->pos;
    ENSURE(ch(s, '$'), "expecting '$'");
    CHECK(token_index(s, s->text + start, end - start, out, true));
    ENSURE(*out != s->open_paren && *out != s->close_paren, "parentheses are not valid tokens");
    return true;
}

static bool set_prec(Mm0Spec *s, uint32_t token, uint32_t p) {
    Token *t = &s->tokens[token];
    if (t->prec_set) {
        ENSURE(t->prec == p, "precedence mismatch");
    } else {
        t->prec_set = 1;
        t->prec = p;
    }
    return true;
}

static bool insert_prec_assoc(Mm0Spec *s, uint32_t p, uint8_t rassoc) {
    for (size_t i = 0; i < s->nprec; ++i) {
        if (s->prec_assoc[i].prec == p) {
            ENSURE(s->prec_assoc[i].rassoc == rassoc, "precedence level is both left and right assoc");
            return true;
        }
    }
    GROW(s->prec_assoc, s->cap_prec, s->nprec + 1);
    s->prec_assoc[s->nprec++] = (PrecAssoc){p, rassoc};
    return true;
}

/* The math-string lexer (mm0.md "Secondary parsing"; parser.c token):
 * split on whitespace, after every left delimiter and before every right
 * delimiter. */
typedef struct { size_t start, end; } Span;
static bool math_token(Mm0Spec *s, Span *out) {
    CHECK(step(s));
    int c = cur(s);
    ENSURE(c != '$' && c != 0, "expecting token");
    size_t start = s->pos;
    for (;;) {
        ++s->pos;
        if (s->delims[c] & DELIM_LEFT) break;
        c = cur(s);
        if (c == 0 || (s->delims[c] & DELIM_RIGHT)) break;
    }
    *out = (Span){start, s->pos};
    while (is_ws(cur(s))) ++s->pos;
    return true;
}

static const Token *token_of(const Mm0Spec *s, Span t) {
    uint32_t index;
    if (!mm0_names_get(&s->token_map, s->text + t.start, t.end - t.start, &index)) return NULL;
    return &s->tokens[index];
}

/* ---- sorts and MMB tables ----------------------------------------------- */

static uint32_t term_nargs(const MmbTables *t, uint32_t term) {
    return mm0_le16(t->term_table + 8 * (size_t)term);
}
static uint8_t term_sortbyte(const MmbTables *t, uint32_t term) {
    return t->term_table[8 * (size_t)term + 2];
}
static const uint8_t *term_args(const MmbTables *t, uint32_t term) {
    return t->file + mm0_le32(t->term_table + 8 * (size_t)term + 4);
}
static uint32_t term_arg_sort(const MmbTables *t, uint32_t term, uint32_t i) {
    return MMB_TYPE_SORT(mm0_le64(term_args(t, term) + 8 * (size_t)i));
}
static uint32_t thm_nargs(const MmbTables *t, uint32_t thm) {
    return mm0_le16(t->thm_table + 8 * (size_t)thm);
}
static const uint8_t *thm_args(const MmbTables *t, uint32_t thm) {
    return t->file + mm0_le32(t->thm_table + 8 * (size_t)thm + 4);
}

static bool sort_name(Mm0Spec *s, uint32_t *out) {
    Ident id;
    if (!ident_start(cur(s))) return fail(s, MM0_REFUSED, "expecting sort");
    CHECK(ident(s, &id));
    uint32_t index;
    ENSURE(!id.blank && mm0_names_get(&s->sorts, id.text, id.len, &index), "expecting sort");
    *out = index;
    return true;
}

static bool term_name(Mm0Spec *s, uint32_t *out) {
    Ident id;
    if (!ident_start(cur(s))) return fail(s, MM0_REFUSED, "unknown term");
    CHECK(ident(s, &id));
    ENSURE(!id.blank && mm0_names_get(&s->terms, id.text, id.len, out), "unknown term");
    return true;
}

/* ---- coercions (mm0.md "Secondary parsing": at most one path) ----------- */

static uint32_t find(uint32_t *parent, uint32_t x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static bool add_coercion(Mm0Spec *s, uint32_t term, uint32_t s1, uint32_t s2, uint32_t known) {
    uint32_t n = s->nsorts;
    ENSURE(s1 != s2, "coercion cycle detected");
    Coe *coe = s->coe;
    ENSURE(coe[(size_t)s2 * n + s1].tag == COE_EMPTY, "coercion cycle detected");
    ENSURE(find(s->component, s1) != find(s->component, s2), "coercion diamond detected");
    /* Every sort that reaches s1 now reaches every sort s2 reaches. */
    for (uint32_t l = 0; l < known; ++l) {
        if (l != s1 && coe[(size_t)l * n + s1].tag == COE_EMPTY) continue;
        for (uint32_t r = 0; r < known; ++r) {
            if (r != s2 && coe[(size_t)s2 * n + r].tag == COE_EMPTY) continue;
            Coe *c = &coe[(size_t)l * n + r];
            if (l == s1 && r == s2) *c = (Coe){COE_ONE, term, 0};
            else *c = (Coe){COE_TRANS, 0, l != s1 ? s1 : s2};
        }
    }
    /* Each sort reaches at most one provable sort. */
    if (s->prov[s2] != NO_INDEX) {
        for (uint32_t l = 0; l < known; ++l) {
            if (l != s1 && coe[(size_t)l * n + s1].tag == COE_EMPTY) continue;
            ENSURE(s->prov[l] == NO_INDEX, "coercion diamond to provable detected");
        }
        for (uint32_t l = 0; l < known; ++l) {
            if (l != s1 && coe[(size_t)l * n + s1].tag == COE_EMPTY) continue;
            s->prov[l] = s->prov[s2];
        }
    }
    s->component[find(s->component, s1)] = find(s->component, s2);
    return true;
}

/* Insert the coercions from `e`'s sort to `target` (mm0.md "Type checking").
 * target == nsorts means "some provable sort". */
static bool coerce(Mm0Spec *s, const MmbTables *tables, ExprSort e, uint32_t target, Mm0Expr **out) {
    uint32_t n = s->nsorts;
    if (target == n) {
        if (e.sort < n && s->prov[e.sort] == e.sort) { *out = e.expr; return true; }
        ENSURE(e.sort < n && s->prov[e.sort] != NO_INDEX, "type mismatch");
        target = s->prov[e.sort];
    }
    while (e.sort != target) {
        Coe c = s->coe[(size_t)e.sort * n + target];
        ENSURE(c.tag != COE_EMPTY, "type mismatch");
        uint32_t next = c.tag == COE_ONE ? target : c.via;
        /* follow the first hop from e.sort toward next */
        for (;;) {
            Coe h = s->coe[(size_t)e.sort * n + next];
            if (h.tag == COE_ONE) {
                Mm0Expr *x = mm0_expr_app(&s->scratch, h.term, 1);
                if (!x) return fail(s, MM0_NOMEM, "specification allocation");
                x->args[0] = e.expr;
                e = (ExprSort){x, term_sortbyte(tables, h.term) & 0x7Fu};
                break;
            }
            ENSURE(h.tag == COE_TRANS, "type mismatch");
            next = h.via;
        }
    }
    *out = e.expr;
    return true;
}

/* ---- variables of the current statement --------------------------------- */

static void clear_vars(Mm0Spec *s) {
    mm0_names_free(&s->vars);
    s->nvars = 0;
    s->ndummy_order = 0;
}

static bool new_var(Mm0Spec *s, uint32_t index, Ident name) {
    GROW(s->var_names, s->cap_var_names, (size_t)index + 1);
    GROW(s->var_sorts, s->cap_var_sorts, (size_t)index + 1);
    s->var_names[index] = NULL;
    if (name.blank) return true;
    uint32_t existing;
    ENSURE(!mm0_names_get(&s->vars, name.text, name.len, &existing), "variable shadowing is not allowed");
    if (!mm0_names_put(&s->vars, name.text, name.len, index))
        return fail(s, MM0_NOMEM, "specification allocation");
    s->var_names[index] = mm0_arena_strndup(&s->scratch, name.text, name.len);
    if (!s->var_names[index]) return fail(s, MM0_NOMEM, "specification allocation");
    return true;
}

static bool lookup_var(Mm0Spec *s, uint32_t *out) {
    Ident id;
    if (!ident_start(cur(s))) return fail(s, MM0_REFUSED, "expecting variable");
    CHECK(ident(s, &id));
    ENSURE(!id.blank && mm0_names_get(&s->vars, id.text, id.len, out), "expecting variable");
    return true;
}

/* ---- math strings (mm0.md "Secondary parsing"; parser.c expr) ----------- */

typedef enum {
    F_DONE, F_EXPR, F_LHS, F_LHS_RHS, F_LHS_INNER, F_PAREN, F_NOTA, F_APP
} FrameKind;
struct Frame {
    FrameKind kind;
    uint32_t prec, p2, infix, index;
    ExprSort lhs, rhs;
    Mm0Expr *node;
    uint32_t prefix;
};
typedef struct Frame Frame;

static bool push_frame(Mm0Spec *s, Frame f) {
    GROW(s->frames, s->cap_frames, s->nframes + 1);
    s->frames[s->nframes++] = f;
    return true;
}

static bool at_math_end(const Mm0Spec *s) { return cur(s) == '$' || cur(s) == 0; }

/* The infix operator a token starts, when it binds at precedence >= p. */
static const Token *infix_at(const Mm0Spec *s, Span t, uint32_t p) {
    const Token *tk = token_of(s, t);
    if (!tk || tk->infix == NO_INDEX || tk->prec < p) return NULL;
    return tk;
}

static bool make_infix(Mm0Spec *s, const MmbTables *tables, Frame *f) {
    const Infix *ifx = &s->infixes[f->infix];
    Mm0Expr *e = mm0_expr_app(&s->scratch, ifx->term, 2);
    if (!e) return fail(s, MM0_NOMEM, "specification allocation");
    CHECK(coerce(s, tables, f->lhs, term_arg_sort(tables, ifx->term, 0), &e->args[0]));
    CHECK(coerce(s, tables, f->rhs, term_arg_sort(tables, ifx->term, 1), &e->args[1]));
    f->lhs = (ExprSort){e, term_sortbyte(tables, ifx->term) & 0x7Fu};
    f->kind = F_LHS;
    return true;
}

typedef enum { M_PREFIX, M_NOTA, M_RETURN, M_LHS, M_INNER } Mode;

/* expression(prec), iteratively: frames stand for the pending calls of the
 * recursive prefix_expr/lhs_expr pair in parser.c. */
static bool parse_expr(Mm0Spec *s, const MmbTables *tables, uint32_t prec0, ExprSort *out) {
    size_t base = s->nframes;
    CHECK(push_frame(s, (Frame){.kind = F_DONE}));
    CHECK(push_frame(s, (Frame){.kind = F_EXPR, .prec = prec0}));
    Mode mode = M_PREFIX;
    uint32_t pprec = prec0;
    ExprSort ret = {0};
    for (;;) {
        Frame *f = &s->frames[s->nframes - 1];
        switch (mode) {
        case M_PREFIX: {
            Span t;
            CHECK(math_token(s, &t));
            uint32_t index;
            CHECK(token_index(s, s->text + t.start, t.end - t.start, &index, false));
            if (index != NO_INDEX && index == s->open_paren) {
                CHECK(push_frame(s, (Frame){.kind = F_PAREN}));
                CHECK(push_frame(s, (Frame){.kind = F_EXPR, .prec = 0}));
                pprec = 0;
                continue;
            }
            const Token *tk = index == NO_INDEX ? NULL : &s->tokens[index];
            if (tk && tk->prefix != NO_INDEX) {
                ENSURE(tk->prec >= pprec, "expecting prefix expression, precedence mismatch");
                const Prefix *p = &s->prefixes[tk->prefix];
                Mm0Expr *e = mm0_expr_app(&s->scratch, p->term, p->nargs);
                if (!e) return fail(s, MM0_NOMEM, "specification allocation");
                CHECK(push_frame(s, (Frame){.kind = F_NOTA, .node = e, .prefix = tk->prefix}));
                mode = M_NOTA;
                continue;
            }
            uint32_t var;
            if (mm0_names_get(&s->vars, s->text + t.start, t.end - t.start, &var)) {
                Mm0Expr *e = mm0_expr_var(&s->scratch, var);
                if (!e) return fail(s, MM0_NOMEM, "specification allocation");
                ret = (ExprSort){e, s->var_sorts[var]};
                mode = M_RETURN;
                continue;
            }
            uint32_t term;
            ENSURE(mm0_names_get(&s->terms, s->text + t.start, t.end - t.start, &term),
                   "expecting prefix expression, unrecognized token");
            uint32_t nargs = term_nargs(tables, term);
            Mm0Expr *e = mm0_expr_app(&s->scratch, term, nargs);
            if (!e) return fail(s, MM0_NOMEM, "specification allocation");
            if (!nargs) {
                ret = (ExprSort){e, term_sortbyte(tables, term) & 0x7Fu};
                mode = M_RETURN;
                continue;
            }
            ENSURE(pprec <= APP_PREC, "unexpected application at this precedence");
            CHECK(push_frame(s, (Frame){.kind = F_APP, .node = e}));
            CHECK(push_frame(s, (Frame){.kind = F_EXPR, .prec = PREC_MAX}));
            pprec = PREC_MAX;
            continue;
        }
        case M_NOTA: {
            const Prefix *p = &s->prefixes[f->prefix];
            while (f->index < p->nlits && p->lits[f->index].constant) {
                Span t;
                CHECK(math_token(s, &t));
                uint32_t index;
                CHECK(token_index(s, s->text + t.start, t.end - t.start, &index, false));
                ENSURE(index == p->lits[f->index].value, "expecting notation token");
                ++f->index;
            }
            if (f->index == p->nlits) {
                ret = (ExprSort){f->node, term_sortbyte(tables, p->term) & 0x7Fu};
                --s->nframes;
                mode = M_RETURN;
                continue;
            }
            uint32_t q = p->lits[f->index].prec;
            CHECK(push_frame(s, (Frame){.kind = F_EXPR, .prec = q}));
            pprec = q;
            mode = M_PREFIX;
            continue;
        }
        case M_RETURN:
            switch (f->kind) {
            case F_DONE:
                --s->nframes;
                if (s->nframes != base) return fail(s, MM0_MALFORMED, "parser state");
                *out = ret;
                return true;
            case F_EXPR:
                *f = (Frame){.kind = F_LHS, .prec = f->prec, .lhs = ret};
                mode = M_LHS;
                continue;
            case F_PAREN: {
                --s->nframes;
                Span t;
                CHECK(math_token(s, &t));
                uint32_t index;
                CHECK(token_index(s, s->text + t.start, t.end - t.start, &index, false));
                ENSURE(index != NO_INDEX && index == s->close_paren, "expecting ')'");
                continue;
            }
            case F_NOTA: {
                const Prefix *p = &s->prefixes[f->prefix];
                uint32_t var = p->lits[f->index].value;
                CHECK(coerce(s, tables, ret, term_arg_sort(tables, p->term, var), &f->node->args[var]));
                ++f->index;
                mode = M_NOTA;
                continue;
            }
            case F_APP: {
                uint32_t term = f->node->head;
                CHECK(coerce(s, tables, ret, term_arg_sort(tables, term, f->index), &f->node->args[f->index]));
                if (++f->index < f->node->nargs) {
                    CHECK(push_frame(s, (Frame){.kind = F_EXPR, .prec = PREC_MAX}));
                    pprec = PREC_MAX;
                    mode = M_PREFIX;
                } else {
                    ret = (ExprSort){f->node, term_sortbyte(tables, term) & 0x7Fu};
                    --s->nframes;
                }
                continue;
            }
            case F_LHS_RHS:
            case F_LHS_INNER:
                f->rhs = ret;
                f->kind = F_LHS_INNER;
                mode = M_INNER;
                continue;
            default:
                return fail(s, MM0_MALFORMED, "parser state");
            }
        case M_LHS: {
            /* lhs_expr: while the next token is an infix operator binding at
             * least as tightly as this level, consume it and its right side. */
            const Token *tk = NULL;
            size_t start = s->pos;
            if (!at_math_end(s)) {
                Span t;
                CHECK(math_token(s, &t));
                tk = infix_at(s, t, f->prec);
            }
            if (!tk) {
                s->pos = start;
                ret = f->lhs;
                --s->nframes;
                mode = M_RETURN;
                continue;
            }
            f->infix = tk->infix;
            f->p2 = s->infixes[tk->infix].rassoc ? tk->prec : tk->prec + 1;
            f->kind = F_LHS_RHS;
            pprec = f->p2;
            mode = M_PREFIX;
            continue;
        }
        case M_INNER: {
            /* An operator binding at least as tightly as p2 takes the right
             * side as its left operand before this infix is built. */
            size_t start = s->pos;
            const Token *tk = NULL;
            if (!at_math_end(s)) {
                Span t;
                CHECK(math_token(s, &t));
                tk = infix_at(s, t, f->p2);
            }
            s->pos = start;
            if (tk) {
                CHECK(push_frame(s, (Frame){.kind = F_LHS, .prec = tk->prec, .lhs = f->rhs}));
                mode = M_LHS;
                continue;
            }
            CHECK(make_infix(s, tables, f));
            mode = M_LHS;
            continue;
        }
        }
    }
}

/* '$' expression(0) '$', coerced to `target` (nsorts: provable) */
static bool formula(Mm0Spec *s, const MmbTables *tables, uint32_t target, Mm0Expr **out) {
    ENSURE(cur(s) == '$', "expecting '$'");
    do ++s->pos; while (is_ws(cur(s)));
    ExprSort e;
    CHECK(parse_expr(s, tables, 0, &e));
    CHECK(coerce(s, tables, e, target, out));
    ENSURE(ch(s, '$'), "expecting '$'");
    return true;
}

/* ---- binders (parser.c binders) ----------------------------------------- */

typedef struct { Mm0Expr **items; size_t n, cap; } ExprList;

static bool list_push(Mm0Spec *s, ExprList *l, Mm0Expr *e) {
    GROW(l->items, l->cap, l->n + 1);
    l->items[l->n++] = e;
    return true;
}

typedef enum { B_TERM, B_DEF, B_THEOREM } BinderKind;

/* Reads binders and the arrow type of a term, def, notation or theorem and
 * checks them against the table entry's argument words.  For theorems the
 * formulas are returned in `hyps` in declaration order, followed by the
 * conclusion. */
static bool binders(Mm0Spec *s, const MmbTables *tables, BinderKind kind, uint32_t nargs,
                    const uint8_t *args, uint64_t ret, ExprList *hyps) {
    clear_vars(s);
    s->nargs = nargs;
    s->nvars = nargs;
    uint32_t arg_idx = 0;
    bool had_hyp = false;
    while (!ch(s, ':')) {
        CHECK(step(s));
        bool curly = ch(s, '{');
        ENSURE(curly || ch(s, '('), "expecting an argument binder");
        int close = curly ? '}' : ')';
        uint32_t group_start = arg_idx, dummy_start = s->nvars;
        /* lookahead: a group whose type is a formula binds hypotheses */
        size_t save = s->pos;
        while (!ch(s, ':')) {
            ch(s, '.');
            Ident id;
            CHECK(ident(s, &id));
        }
        bool is_hyp = cur(s) == '$';
        s->pos = save;
        uint32_t names_in_group = 0;
        while (!ch(s, ':')) {
            bool dummy = ch(s, '.');
            Ident id;
            CHECK(ident(s, &id));
            ++names_in_group;
            if (dummy) ENSURE(kind == B_DEF, "dummies are only allowed in defs");
            if (is_hyp) continue;
            ENSURE(!had_hyp, "hypotheses must follow variables");
            uint32_t index;
            if (dummy) {
                ENSURE(s->nvars < UINT32_MAX, "too many variables");
                index = s->nvars++;
            } else {
                ENSURE(arg_idx < nargs, "too many arguments");
                index = arg_idx++;
            }
            CHECK(new_var(s, index, id));
        }
        if (is_hyp) {
            ENSURE(kind == B_THEOREM, "hypotheses are only allowed in axioms/theorems");
            ENSURE(!curly, "hypotheses are only allowed in regular binders");
            Mm0Expr *e;
            CHECK(formula(s, tables, s->nsorts, &e));
            ENSURE(ch(s, ')'), "expecting '$)'");
            for (uint32_t i = 0; i < names_in_group; ++i) CHECK(list_push(s, hyps, e));
            had_hyp = true;
            continue;
        }
        uint32_t sort;
        CHECK(sort_name(s, &sort));
        uint64_t type = (uint64_t)sort << 56 | (curly ? MMB_TYPE_BOUND_BIT : 0);
        if (!ch(s, close)) {
            ENSURE(!curly, "bound variables should not have dependencies");
            ENSURE(dummy_start == s->nvars, "dummies should not have dependencies");
            do {
                uint32_t var;
                CHECK(lookup_var(s, &var));
                ENSURE(var < group_start, "expecting variable");
                uint64_t a = mm0_le64(args + 8 * (size_t)var);
                ENSURE(a & MMB_TYPE_BOUND_BIT, "expecting bound variable");
                type |= a & MMB_TYPE_DEPS_MASK;
            } while (!ch(s, close));
        }
        for (uint32_t i = group_start; i < arg_idx; ++i) {
            uint64_t a = mm0_le64(args + 8 * (size_t)i);
            ENSURE(type == (curly ? a & ~((UINT64_C(1) << 56) - 1) : a),
                   "variable type does not match theorem");
            s->var_sorts[i] = sort;
        }
        for (uint32_t i = dummy_start; i < s->nvars; ++i) s->var_sorts[i] = sort;
    }
    /* arrow-type ::= type | type '>' arrow-type, with formulas for theorems */
    for (;;) {
        CHECK(step(s));
        if (cur(s) == '$') {
            ENSURE(kind == B_THEOREM, "hypotheses are only allowed in axioms/theorems");
            Mm0Expr *e;
            CHECK(formula(s, tables, s->nsorts, &e));
            CHECK(list_push(s, hyps, e));
            had_hyp = true;
            if (!ch(s, '>')) break;
            continue;
        }
        ENSURE(!had_hyp, "hypotheses must follow variables");
        uint32_t sort;
        CHECK(sort_name(s, &sort));
        uint64_t type = (uint64_t)sort << 56;
        while (ident_start(cur(s))) {
            uint32_t var;
            CHECK(lookup_var(s, &var));
            ENSURE(var < arg_idx, "expecting variable");
            uint64_t a = mm0_le64(args + 8 * (size_t)var);
            ENSURE(a & MMB_TYPE_BOUND_BIT, "expecting bound variable");
            type |= a & MMB_TYPE_DEPS_MASK;
        }
        if (ch(s, '>')) {
            ENSURE(arg_idx < nargs, "too many arguments");
            ENSURE(type == mm0_le64(args + 8 * (size_t)arg_idx), "variable type does not match theorem");
            /* An unnamed argument still has its own variable. */
            CHECK(new_var(s, arg_idx, (Ident){NULL, 0, true}));
            s->var_sorts[arg_idx] = sort;
            ++arg_idx;
        } else {
            ENSURE(kind != B_THEOREM, "the return type of a theorem should end be a statement");
            ENSURE(type == ret, "return type does not match theorem");
            break;
        }
    }
    ENSURE(arg_idx == nargs, "incorrect number of arguments");
    if (kind == B_THEOREM) ENSURE(hyps->n > 0, "the return type of a theorem should end be a statement");
    return true;
}

/* ---- matching a parse with a unify stream (parser.c check_expr) --------- */

static bool deep_eq(Mm0Spec *s, Mm0Expr *a, Mm0Expr *b) {
    size_t base = s->usp;
    GROW(s->ustack, s->cap_ustack, s->usp + 2);
    s->ustack[s->usp++] = a;
    s->ustack[s->usp++] = b;
    while (s->usp > base) {
        Mm0Expr *y = s->ustack[--s->usp], *x = s->ustack[--s->usp];
        if (x == y) continue;
        CHECK(step(s));
        ENSURE(x->var == y->var && x->head == y->head, "expression mismatch");
        if (x->var) continue;
        GROW(s->ustack, s->cap_ustack, s->usp + 2 * (size_t)x->nargs);
        for (uint32_t i = 0; i < x->nargs; ++i) {
            s->ustack[s->usp++] = x->args[i];
            s->ustack[s->usp++] = y->args[i];
        }
    }
    return true;
}

/* `hyps` holds the hypotheses in declaration order; the stream reads them
 * last first (mmb.md "Unify Stream"). */
static bool check_expr(Mm0Spec *s, const MmbTables *tables, uint32_t nargs, const uint8_t *cmd,
                       Mm0Expr *target, Mm0Expr **hyps, size_t nhyps) {
    (void)tables;
    s->uhp = 0;
    GROW(s->uheap, s->cap_uheap, nargs);
    for (uint32_t i = 0; i < nargs; ++i) {
        Mm0Expr *x = mm0_expr_var(&s->scratch, i);
        if (!x) return fail(s, MM0_NOMEM, "specification allocation");
        s->uheap[s->uhp++] = x;
    }
    s->usp = 0;
    GROW(s->ustack, s->cap_ustack, 1);
    s->ustack[s->usp++] = target;
    s->ndummy_order = 0;
    for (;;) {
        CHECK(step(s));
        MmbCmd c = mmb_cmd(cmd);
        cmd += c.size;
        switch (c.op) {
        case MMB_CMD_END:
            ENSURE(nhyps == 0, "unfinished hypothesis stack");
            ENSURE(s->usp == 0, "unfinished unify stack");
            return true;
        case MMB_UNIFY_REF: {
            ENSURE(c.data < s->uhp && s->usp > 0, "bad ref step");
            Mm0Expr *e = s->ustack[--s->usp];
            CHECK(deep_eq(s, s->uheap[c.data], e));
            break;
        }
        case MMB_UNIFY_TERM:
        case MMB_UNIFY_TERM_SAVE: {
            ENSURE(s->usp > 0, "unify stack underflow");
            Mm0Expr *e = s->ustack[--s->usp];
            ENSURE(!e->var, "store type error");
            ENSURE(e->head == c.data, "unify failure at term");
            GROW(s->ustack, s->cap_ustack, s->usp + e->nargs);
            for (uint32_t i = e->nargs; i-- > 0;) s->ustack[s->usp++] = e->args[i];
            if (c.op == MMB_UNIFY_TERM_SAVE) {
                GROW(s->uheap, s->cap_uheap, s->uhp + 1);
                s->uheap[s->uhp++] = e;
            }
            break;
        }
        case MMB_UNIFY_DUMMY: {
            ENSURE(s->usp > 0, "unify stack underflow");
            Mm0Expr *e = s->ustack[--s->usp];
            ENSURE(e->var, "store type error");
            ENSURE(e->head >= nargs, "expected a dummy");
            ENSURE(c.data == s->var_sorts[e->head], "unify failure at dummy");
            /* Distinct UDummy commands match distinct specification dummies. */
            for (size_t i = nargs; i < s->uhp; ++i)
                ENSURE(!s->uheap[i]->var || s->uheap[i]->head != e->head,
                       "dummy matched more than once");
            GROW(s->uheap, s->cap_uheap, s->uhp + 1);
            s->uheap[s->uhp++] = e;
            GROW(s->dummy_order, s->cap_dummy_order, s->ndummy_order + 1);
            s->dummy_order[s->ndummy_order++] = s->var_names[e->head];
            break;
        }
        case MMB_UNIFY_HYP:
            ENSURE(nhyps > 0, "hypothesis number mismatch");
            GROW(s->ustack, s->cap_ustack, s->usp + 1);
            s->ustack[s->usp++] = hyps[--nhyps];
            break;
        default:
            return fail(s, MM0_REFUSED, "unknown opcode in statement");
        }
    }
}

/* ---- statements (parser.c parse_until) ---------------------------------- */

static bool record_names(Mm0Spec *s, Ident name, Mm0SpecNames *names, bool dummies) {
    names->name = name.text;
    names->name_len = name.len;
    size_t count = s->nargs + (dummies ? s->ndummy_order : 0);
    names->var_count = count;
    names->var_names = NULL;
    if (!count) return true;
    names->var_names = mm0_arena_alloc(&s->scratch, count * sizeof *names->var_names);
    if (!names->var_names) return fail(s, MM0_NOMEM, "specification allocation");
    for (size_t i = 0; i < s->nargs; ++i) names->var_names[i] = s->var_names[i];
    for (size_t i = s->nargs; i < count; ++i) names->var_names[i] = s->dummy_order[i - s->nargs];
    return true;
}

static bool delimiter_stmt(Mm0Spec *s) {
    ENSURE(cur(s) == '$', "expecting '$'");
    do ++s->pos; while (is_ws(cur(s)));
    size_t start = s->pos;
    while (!ch(s, '$')) {
        int c = cur(s);
        ENSURE(c != 0, "expecting '$'");
        s->delims[c] |= DELIM_LEFT;
        ++s->pos;
        int w = cur(s);
        ENSURE(is_ws(w) || w == '$', "multi-character delimiters not supported");
        while (is_ws(cur(s))) ++s->pos;
    }
    if (cur(s) == ';') {
        s->pos = start;  /* one string: its characters are both left and right */
    } else {
        ENSURE(cur(s) == '$', "expecting '$' or ';'");
        do ++s->pos; while (is_ws(cur(s)));
    }
    while (!ch(s, '$')) {
        int c = cur(s);
        ENSURE(c != 0, "expecting '$'");
        s->delims[c] |= DELIM_RIGHT;
        ++s->pos;
        int w = cur(s);
        ENSURE(is_ws(w) || w == '$', "multi-character delimiters not supported");
        while (is_ws(cur(s))) ++s->pos;
    }
    ENSURE(ch(s, ';'), "expecting ';'");
    return true;
}

static bool simple_notation(Mm0Spec *s, const MmbTables *tables, Keyword k) {
    uint32_t term;
    CHECK(term_name(s, &term));
    ENSURE(ch(s, ':'), "expecting ':'");
    uint32_t token;
    CHECK(new_token(s, &token));
    ENSURE(kw(s) == KW_PREC, "expecting 'prec'");
    uint32_t p;
    CHECK(prec(s, &p));
    CHECK(set_prec(s, token, p));
    uint32_t nargs = term_nargs(tables, term);
    if (k == KW_PREFIX) {
        ENSURE(s->tokens[token].prefix == NO_INDEX, "token already has a prefix notation");
        Literal *lits = nargs ? mm0_arena_alloc(&s->arena, nargs * sizeof *lits) : NULL;
        if (nargs && !lits) return fail(s, MM0_NOMEM, "specification allocation");
        /* OP expression(max){n} expression(p) */
        for (uint32_t i = 0; i < nargs; ++i)
            lits[i] = (Literal){0, i, i + 1 < nargs ? PREC_MAX : p};
        GROW(s->prefixes, s->cap_prefixes, s->nprefixes + 1);
        s->prefixes[s->nprefixes] = (Prefix){term, nargs, nargs, lits};
        s->tokens[token].prefix = (uint32_t)s->nprefixes++;
        if (nargs) CHECK(insert_prec_assoc(s, p, 1));
    } else {
        ENSURE(nargs == 2, "expected a binary operator");
        ENSURE(p < PREC_MAX - 1, "precedence out of range");
        ENSURE(s->tokens[token].infix == NO_INDEX, "token already has an infix notation");
        uint8_t rassoc = k == KW_INFIXR;
        GROW(s->infixes, s->cap_infixes, s->ninfixes + 1);
        s->infixes[s->ninfixes] = (Infix){term, rassoc};
        s->tokens[token].infix = (uint32_t)s->ninfixes++;
        CHECK(insert_prec_assoc(s, p, rassoc));
    }
    ENSURE(ch(s, ';'), "expecting ';'");
    return true;
}

/* gen-notation-stmt ::= 'notation' identifier (type-binder)* ':' type '='
 *   prec-constant (notation-literal)* ';' */
static bool general_notation(Mm0Spec *s, const MmbTables *tables) {
    uint32_t term;
    CHECK(term_name(s, &term));
    uint32_t nargs = term_nargs(tables, term);
    const uint8_t *args = term_args(tables, term);
    ExprList none = {0};
    bool ok = binders(s, tables, B_TERM, nargs, args, mm0_le64(args + 8 * (size_t)nargs), &none);
    free(none.items);
    CHECK(ok);
    ENSURE(ch(s, '='), "expecting '='");
    ENSURE(ch(s, '('), "expecting '('");
    uint32_t token;
    CHECK(new_token(s, &token));
    ENSURE(ch(s, ':'), "expecting ':'");
    uint32_t p;
    CHECK(prec(s, &p));
    ENSURE(ch(s, ')'), "expecting ')'");
    CHECK(set_prec(s, token, p));
    ENSURE(s->tokens[token].prefix == NO_INDEX, "token already has a prefix notation");
    Literal *lits = NULL;
    size_t nlits = 0, cap = 0;
    uint32_t pending = NO_INDEX;   /* a variable literal waiting for its precedence */
    uint8_t *used = nargs ? calloc(nargs, 1) : NULL;
    if (nargs && !used) return fail(s, MM0_NOMEM, "specification allocation");
    bool good = true;
    while (good && cur(s) != ';') {
        if (!MM0_RESERVE(lits, cap, nlits + 1)) { good = fail(s, MM0_NOMEM, "specification allocation"); break; }
        if (ch(s, '(')) {
            uint32_t c, q;
            good = new_token(s, &c) && (ch(s, ':') || fail(s, MM0_REFUSED, "expecting ':'")) &&
                   prec(s, &q) && (ch(s, ')') || fail(s, MM0_REFUSED, "expecting ')'")) &&
                   set_prec(s, c, q);
            if (!good) break;
            lits[nlits++] = (Literal){1, c, 0};
            if (pending != NO_INDEX) {
                /* a constant after a variable is infixy: the variable binds
                 * one level tighter (mm0.md P((c: p) lits, q) = p+1) */
                if (!(q < PREC_MAX - 1)) { good = fail(s, MM0_REFUSED, "precedence out of range"); break; }
                lits[pending].prec = q + 1;
                pending = NO_INDEX;
            }
        } else {
            uint32_t var;
            if (!lookup_var(s, &var)) { good = false; break; }
            if (var >= nargs) { good = fail(s, MM0_REFUSED, "expecting variable"); break; }
            if (used[var]) { good = fail(s, MM0_REFUSED, "notation variable used twice"); break; }
            used[var] = 1;
            if (pending != NO_INDEX) lits[pending].prec = PREC_MAX;  /* P(v lits, q) = max */
            lits[nlits] = (Literal){0, var, 0};
            pending = (uint32_t)nlits++;
        }
    }
    for (uint32_t i = 0; good && i < nargs; ++i)
        if (!used[i]) good = fail(s, MM0_REFUSED, "notation does not use every variable");
    free(used);
    if (good && pending != NO_INDEX) {
        lits[pending].prec = p;   /* P([], q) = q */
        good = insert_prec_assoc(s, p, 1);
    }
    Literal *kept = NULL;
    if (good && nlits) {
        kept = mm0_arena_alloc(&s->arena, nlits * sizeof *kept);
        if (!kept) good = fail(s, MM0_NOMEM, "specification allocation");
        else memcpy(kept, lits, nlits * sizeof *kept);
    }
    free(lits);
    CHECK(good);
    GROW(s->prefixes, s->cap_prefixes, s->nprefixes + 1);
    s->prefixes[s->nprefixes] = (Prefix){term, nargs, (uint32_t)nlits, kept};
    s->tokens[token].prefix = (uint32_t)s->nprefixes++;
    clear_vars(s);
    ENSURE(ch(s, ';'), "expecting ';'");
    return true;
}

/* coercion-stmt ::= 'coercion' identifier ':' identifier '>' identifier ';' */
static bool coercion_stmt(Mm0Spec *s, const MmbTables *tables) {
    uint32_t term;
    CHECK(term_name(s, &term));
    ENSURE(term_nargs(tables, term) == 1, "expected a unary operator");
    ENSURE(ch(s, ':'), "expecting ':'");
    uint32_t s1, s2;
    CHECK(sort_name(s, &s1));
    ENSURE(ch(s, '>'), "expecting '>'");
    CHECK(sort_name(s, &s2));
    ENSURE(ch(s, ';'), "expecting ';'");
    /* the term must have the coercion's type */
    ENSURE(term_arg_sort(tables, term, 0) == s1 &&
           (term_sortbyte(tables, term) & 0x7Fu) == s2, "coercion type mismatch");
    return add_coercion(s, term, s1, s2, (uint32_t)s->sorts.count);
}

typedef enum { WANT_SORT, WANT_TERM, WANT_AXIOM, WANT_THM, WANT_END } Want;

/* Reads notation statements, then the declaration keyword, which must be
 * of the wanted kind.  Returns the keyword read in *k. */
static bool next_declaration(Mm0Spec *s, const MmbTables *tables, Want want, Keyword *out,
                             uint8_t *mods) {
    if (!s->text_ok) return fail(s, MM0_MALFORMED, "Null character in input");
    for (;;) {
        CHECK(step(s));
        *mods = 0;
        Keyword k;
        for (;;) {
            k = kw(s);
            if (k == KW_PURE) *mods |= MM0_SORT_PURE;
            else if (k == KW_STRICT) *mods |= MM0_SORT_STRICT;
            else if (k == KW_PROVABLE) *mods |= MM0_SORT_PROVABLE;
            else if (k == KW_FREE) *mods |= MM0_SORT_FREE;
            else break;
        }
        if (k == KW_SORT) {
            ENSURE(want == WANT_SORT, "expecting a sort");
            *out = k;
            return true;
        }
        ENSURE(*mods == 0, "modifiers not expected at this position");
        switch (k) {
        case KW_TERM:
        case KW_DEF:
            ENSURE(want == WANT_TERM, "expecting a term/def");
            *out = k;
            return true;
        case KW_AXIOM:
            ENSURE(want == WANT_AXIOM, "expecting an axiom");
            *out = k;
            return true;
        case KW_THEOREM:
            ENSURE(want == WANT_THM, "expecting a theorem");
            *out = k;
            return true;
        case KW_DELIMITER:
            CHECK(delimiter_stmt(s));
            break;
        case KW_PREFIX:
        case KW_INFIXL:
        case KW_INFIXR:
            CHECK(simple_notation(s, tables, k));
            break;
        case KW_NOTATION:
            CHECK(general_notation(s, tables));
            break;
        case KW_COERCION:
            CHECK(coercion_stmt(s, tables));
            break;
        case KW_INPUT:
        case KW_OUTPUT:
            return fail(s, MM0_UNSUPPORTED, "input and output statements are not supported");
        default:
            ENSURE(cur(s) == 0 && want == WANT_END, "invalid command keyword");
            *out = KW_NONE;
            return true;
        }
    }
}

Mm0Status mm0_spec_sort(Mm0Spec *s, const MmbTables *tables, uint32_t index,
                        Mm0SpecNames *names, Mm0Error *error) {
    s->error = error;
    Keyword k;
    uint8_t mods;
    if (!next_declaration(s, tables, WANT_SORT, &k, &mods)) return error->status;
    Ident name;
    uint32_t existing;
    if (!named_ident(s, &name)) return error->status;
    if (mm0_names_get(&s->sorts, name.text, name.len, &existing)) {
        fail(s, MM0_REFUSED, "duplicate sort name");
        return error->status;
    }
    if (mods != tables->sorts[index]) { fail(s, MM0_REFUSED, "sort modifiers do not match"); return error->status; }
    if (!mm0_names_put(&s->sorts, name.text, name.len, index)) {
        fail(s, MM0_NOMEM, "specification allocation");
        return error->status;
    }
    if (mods & MM0_SORT_PROVABLE) s->prov[index] = index;
    if (!ch(s, ';')) { fail(s, MM0_REFUSED, "expecting ';'"); return error->status; }
    *names = (Mm0SpecNames){name.text, name.len, NULL, 0};
    return MM0_OK;
}

Mm0Status mm0_spec_term(Mm0Spec *s, const MmbTables *tables, uint32_t index,
                        Mm0SpecNames *names, Mm0Error *error) {
    s->error = error;
    Keyword k;
    uint8_t mods;
    if (!next_declaration(s, tables, WANT_TERM, &k, &mods)) return error->status;
    bool is_def = (term_sortbyte(tables, index) & 0x80) != 0;
    if (k == KW_TERM && is_def) { fail(s, MM0_REFUSED, "expecting a term"); return error->status; }
    if (k == KW_DEF && !is_def) { fail(s, MM0_REFUSED, "expecting a def"); return error->status; }
    Ident name;
    if (!named_ident(s, &name)) return error->status;
    uint32_t nargs = term_nargs(tables, index);
    const uint8_t *args = term_args(tables, index);
    ExprList none = {0};
    bool ok = binders(s, tables, k == KW_DEF ? B_DEF : B_TERM, nargs, args,
                      mm0_le64(args + 8 * (size_t)nargs), &none);
    free(none.items);
    if (!ok) return error->status;
    s->ndummy_order = 0;
    if (k == KW_DEF && ch(s, '=')) {
        Mm0Expr *body;
        if (!formula(s, tables, term_sortbyte(tables, index) & 0x7Fu, &body) ||
            !check_expr(s, tables, nargs, args + 8 * ((size_t)nargs + 1), body, NULL, 0))
            return error->status;
    }
    uint32_t existing;
    if (mm0_names_get(&s->terms, name.text, name.len, &existing)) {
        fail(s, MM0_REFUSED, "duplicate term/def name");
        return error->status;
    }
    if (!mm0_names_put(&s->terms, name.text, name.len, index)) {
        fail(s, MM0_NOMEM, "specification allocation");
        return error->status;
    }
    if (!record_names(s, name, names, true)) return error->status;
    clear_vars(s);
    if (!ch(s, ';')) { fail(s, MM0_REFUSED, "expecting ';'"); return error->status; }
    return MM0_OK;
}

Mm0Status mm0_spec_thm(Mm0Spec *s, const MmbTables *tables, uint32_t index, bool axiom,
                       Mm0SpecNames *names, Mm0Error *error) {
    s->error = error;
    Keyword k;
    uint8_t mods;
    if (!next_declaration(s, tables, axiom ? WANT_AXIOM : WANT_THM, &k, &mods)) return error->status;
    Ident name;
    if (!named_ident(s, &name)) return error->status;
    uint32_t nargs = thm_nargs(tables, index);
    const uint8_t *args = thm_args(tables, index);
    ExprList formulas = {0};
    bool ok = binders(s, tables, B_THEOREM, nargs, args, 0, &formulas) &&
              check_expr(s, tables, nargs, args + 8 * (size_t)nargs,
                         formulas.items[formulas.n - 1], formulas.items, formulas.n - 1);
    free(formulas.items);
    if (!ok) return error->status;
    uint32_t existing;
    if (mm0_names_get(&s->thms, name.text, name.len, &existing)) {
        fail(s, MM0_REFUSED, "duplicate theorem name");
        return error->status;
    }
    if (!mm0_names_put(&s->thms, name.text, name.len, index)) {
        fail(s, MM0_NOMEM, "specification allocation");
        return error->status;
    }
    if (!record_names(s, name, names, false)) return error->status;
    clear_vars(s);
    if (!ch(s, ';')) { fail(s, MM0_REFUSED, "expecting ';'"); return error->status; }
    return MM0_OK;
}

Mm0Status mm0_spec_finish(Mm0Spec *s, const MmbTables *tables, Mm0Error *error) {
    s->error = error;
    Keyword k;
    uint8_t mods;
    if (!next_declaration(s, tables, WANT_END, &k, &mods)) return error->status;
    return MM0_OK;
}

void mm0_spec_release(Mm0Spec *s) {
    mm0_arena_reset(&s->scratch, (Mm0ArenaMark){0});
}

Mm0Spec *mm0_spec_new(const char *text, size_t len, Mm0Steps *steps) {
    Mm0Spec *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->text = text;
    s->len = len;
    s->steps = steps;
    mm0_arena_init(&s->arena);
    mm0_arena_init(&s->scratch);
    /* parser.c init_parser: whitespace and '$' end tokens */
    s->delims[' '] = DELIM_SPACE | DELIM_RIGHT;
    s->delims['\n'] = DELIM_SPACE | DELIM_RIGHT;
    s->delims['$'] = DELIM_RIGHT;
    s->nsorts = 128;
    s->coe = calloc((size_t)s->nsorts * s->nsorts, sizeof *s->coe);
    s->prov = malloc(s->nsorts * sizeof *s->prov);
    s->component = malloc(s->nsorts * sizeof *s->component);
    if (!s->coe || !s->prov || !s->component ||
        !token_index(s, "(", 1, &s->open_paren, true) ||
        !token_index(s, ")", 1, &s->close_paren, true)) {
        mm0_spec_free(s);
        return NULL;
    }
    for (uint32_t i = 0; i < s->nsorts; ++i) {
        s->prov[i] = NO_INDEX;
        s->component[i] = i;
    }
    s->text_ok = memchr(text, 0, len) == NULL;
    ws(s);
    return s;
}

void mm0_spec_free(Mm0Spec *s) {
    if (!s) return;
    mm0_arena_free(&s->arena);
    mm0_arena_free(&s->scratch);
    mm0_names_free(&s->sorts);
    mm0_names_free(&s->terms);
    mm0_names_free(&s->thms);
    mm0_names_free(&s->vars);
    mm0_names_free(&s->token_map);
    free(s->tokens);
    free(s->prefixes);
    free(s->infixes);
    free(s->prec_assoc);
    free(s->coe);
    free(s->prov);
    free(s->component);
    free(s->var_sorts);
    free(s->var_names);
    free(s->dummy_order);
    free(s->uheap);
    free(s->ustack);
    free(s->frames);
    free(s);
}
