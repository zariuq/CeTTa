/*
 * ndsearch: a standalone test bed for nondeterministic evaluation strategies
 * over a first-order subset of MeTTa (PeTTa spelling).
 *
 * WIP research prototype: this pure first-order subset is not wired into
 * production CeTTa.  Unchecked uint64_t weight accumulation can overflow;
 * the DFS-to-frontier handoff still needs a correspondence proof.  The
 * arithmetic representation also assumes results fit its tagged integers.
 * Differential examples do not discharge these obligations.
 *
 * The same compiled program runs under schedulers that share one step
 * function:
 *
 *   dfs    path at a time: one choice point per branching instruction,
 *          activation slots copied at the choice point, frames allocated in
 *          an arena that backtracking resets (instant reclaiming).
 *   merge  a reconvergent weighted frontier: configurations advance in
 *          rounds; a configuration is keyed by its program point, the values
 *          of the slots live there, and its (hash-consed) return frame; equal
 *          keys are merged by adding their weights.  The answer bag is
 *          recovered from the weights, so multiplicity is preserved.
 *   hybrid DFS initially, with a handoff to the weighted frontier.
 *
 * Language: (= (f p ...) body) clauses (overlapping clauses add answers),
 * patterns over integers, symbols, variables and constructor terms, and
 * bodies built from let, if, superpose, empty, arithmetic and comparison,
 * constructor terms and calls.  A program ends with one query
 * !(size-atom (collapse E)), !(collapse E) or !(foldall-sum E); the result
 * is printed with a digest of the answer bag (value -> multiplicity), so
 * strategies can be compared answer for answer.
 *
 * Usage: ndsearch [--engine dfs|merge|frontier|hybrid|both|all]
 *                 [--adaptive] [--stats] [--print-bag] FILE
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Values: tagged words.  Integers end in 1, symbols in 10, and terms in */
/* 00 with a hash-consed term number, so equal values are equal words.   */

typedef uint64_t Val;
#define V_INT(i) ((((uint64_t)(int64_t)(i)) << 1) | 1u)
#define V_IS_INT(v) (((v) & 1u) == 1u)
#define V_GET_INT(v) (((int64_t)(v)) >> 1)
#define V_SYM(s) ((((uint64_t)(s)) << 2) | 2u)
#define V_IS_SYM(v) (((v) & 3u) == 2u)
#define V_GET_SYM(v) ((uint32_t)((v) >> 2))
#define V_TERM(t) (((uint64_t)(t)) << 2)
#define V_IS_TERM(v) (((v) & 3u) == 0u)
#define V_GET_TERM(v) ((uint32_t)((v) >> 2))

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}
static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}
static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}

static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33; return x;
}

/* Symbols. */
static char **sym_names; static uint32_t sym_len, sym_cap;
static uint32_t intern(const char *s) {
    for (uint32_t i = 0; i < sym_len; i++)
        if (strcmp(sym_names[i], s) == 0) return i;
    if (sym_len == sym_cap) {
        sym_cap = sym_cap ? 2 * sym_cap : 64;
        sym_names = xrealloc(sym_names, sym_cap * sizeof *sym_names);
    }
    sym_names[sym_len] = strdup(s);
    return sym_len++;
}

/* Hash-consed terms: (constructor symbol, children). */
typedef struct { uint32_t ctor, arity, first; } Term;
static Term *terms; static uint32_t term_len, term_cap;
static Val *term_kids; static size_t kid_len, kid_cap;
static uint32_t *term_table; static size_t term_table_cap; /* 0 = empty, else id+1 */

static uint64_t term_hash(uint32_t ctor, uint32_t n, const Val *kids) {
    uint64_t h = mix64(ctor * 0x9e3779b97f4a7c15ULL + n);
    for (uint32_t i = 0; i < n; i++) h = mix64(h ^ kids[i]);
    return h;
}
static void term_table_grow(void) {
    size_t cap = term_table_cap ? term_table_cap * 2 : 1024;
    uint32_t *table = xcalloc(cap, sizeof *table);
    for (uint32_t t = 0; t < term_len; t++) {
        uint64_t h = term_hash(terms[t].ctor, terms[t].arity, term_kids + terms[t].first);
        size_t i = h & (cap - 1);
        while (table[i]) i = (i + 1) & (cap - 1);
        table[i] = t + 1;
    }
    free(term_table); term_table = table; term_table_cap = cap;
}
static Val make_term(uint32_t ctor, uint32_t n, const Val *kids) {
    if ((term_len + 1) * 2 >= term_table_cap) term_table_grow();
    uint64_t h = term_hash(ctor, n, kids);
    size_t i = h & (term_table_cap - 1);
    while (term_table[i]) {
        Term *t = &terms[term_table[i] - 1];
        if (t->ctor == ctor && t->arity == n &&
            memcmp(term_kids + t->first, kids, n * sizeof(Val)) == 0)
            return V_TERM(term_table[i] - 1);
        i = (i + 1) & (term_table_cap - 1);
    }
    if (term_len == term_cap) {
        term_cap = term_cap ? 2 * term_cap : 1024;
        terms = xrealloc(terms, term_cap * sizeof *terms);
    }
    if (kid_len + n > kid_cap) {
        while (kid_len + n > kid_cap) kid_cap = kid_cap ? 2 * kid_cap : 4096;
        term_kids = xrealloc(term_kids, kid_cap * sizeof *term_kids);
    }
    memcpy(term_kids + kid_len, kids, n * sizeof(Val));
    terms[term_len] = (Term){ctor, n, (uint32_t)kid_len};
    kid_len += n;
    term_table[i] = term_len + 1;
    return V_TERM(term_len++);
}

static uint32_t SYM_TRUE, SYM_FALSE;

static void print_val(FILE *f, Val v) {
    if (V_IS_INT(v)) fprintf(f, "%" PRId64, V_GET_INT(v));
    else if (V_IS_SYM(v)) fputs(sym_names[V_GET_SYM(v)], f);
    else {
        Term *t = &terms[V_GET_TERM(v)];
        fprintf(f, "(%s", sym_names[t->ctor]);
        for (uint32_t i = 0; i < t->arity; i++) { fputc(' ', f); print_val(f, term_kids[t->first + i]); }
        fputc(')', f);
    }
}

/* ------------------------------------------------------------------ */
/* S-expressions. */

typedef enum { S_INT, S_SYM, S_VAR, S_LIST } SKind;
typedef struct SExp { SKind kind; int64_t i; char *name; struct SExp **items; int n; } SExp;

static const char *src_pos;
static void skip_ws(void) {
    for (;;) {
        while (*src_pos && isspace((unsigned char)*src_pos)) src_pos++;
        if (*src_pos == ';') { while (*src_pos && *src_pos != '\n') src_pos++; continue; }
        break;
    }
}
static SExp *parse_sexp(void) {
    skip_ws();
    if (!*src_pos) return NULL;
    SExp *e = xcalloc(1, sizeof *e);
    if (*src_pos == '(') {
        src_pos++;
        e->kind = S_LIST;
        int cap = 4; e->items = xmalloc(cap * sizeof *e->items);
        for (;;) {
            skip_ws();
            if (*src_pos == ')') { src_pos++; break; }
            if (!*src_pos) { fprintf(stderr, "unclosed expression\n"); exit(2); }
            if (e->n == cap) { cap *= 2; e->items = xrealloc(e->items, cap * sizeof *e->items); }
            e->items[e->n++] = parse_sexp();
        }
        return e;
    }
    const char *start = src_pos;
    while (*src_pos && !isspace((unsigned char)*src_pos) && *src_pos != '(' && *src_pos != ')') src_pos++;
    size_t len = (size_t)(src_pos - start);
    char *tok = xmalloc(len + 1); memcpy(tok, start, len); tok[len] = 0;
    char *end = NULL;
    long long iv = strtoll(tok, &end, 10);
    if (end && *end == 0 && len > 0 && (isdigit((unsigned char)tok[0]) || (tok[0] == '-' && len > 1))) {
        e->kind = S_INT; e->i = iv; free(tok);
    } else if (tok[0] == '$') {
        e->kind = S_VAR; e->name = tok;
    } else {
        e->kind = S_SYM; e->name = tok;
    }
    return e;
}
static bool is_sym(const SExp *e, const char *s) { return e && e->kind == S_SYM && strcmp(e->name, s) == 0; }

/* ------------------------------------------------------------------ */
/* Compiled code.  Operands: >= 0 a slot, < 0 the constant -(k+1). */

typedef enum {
    I_MOVE, I_PRIM, I_CONS, I_MATCH, I_EQ, I_BR, I_JMP,
    I_CHOOSE, I_SPLIT, I_INDEX, I_CALL, I_TAIL, I_RET, I_FAIL,
} IKind;
typedef enum { P_ADD, P_SUB, P_MUL, P_DIV, P_MOD, P_EQ, P_NE, P_LT, P_GT, P_LE, P_GE, P_AND, P_OR, P_NOT } Prim;

typedef struct {
    uint8_t op, prim;
    uint16_t n;          /* list length (CONS args, MATCH fields, CHOOSE/SPLIT alternatives, CALL args) */
    int32_t dst;         /* destination slot */
    int32_t a, b;        /* operands */
    int32_t target;      /* else / jump target */
    int32_t fn;          /* callee function, or constructor symbol */
    uint32_t list;       /* offset into code_lists */
} Ins;

typedef struct {
    char *name; uint32_t arity; uint32_t entry; uint32_t nslots; bool defined;
    SExp **clauses; int nclauses;
} Fn;

static Ins *code; static uint32_t code_len, code_cap;
static int32_t *code_lists; static uint32_t list_len, list_cap;
static Val *consts; static uint32_t const_len, const_cap;
static Fn *fns; static uint32_t fn_len, fn_cap;
static uint32_t *ins_fn; /* owning function of each instruction */
static uint64_t *live_in; /* live slot set before each instruction */

static uint32_t emit(Ins in) {
    if (code_len == code_cap) { code_cap = code_cap ? 2 * code_cap : 256; code = xrealloc(code, code_cap * sizeof *code); }
    code[code_len] = in;
    return code_len++;
}
static uint32_t list_alloc(uint32_t n) {
    if (list_len + n > list_cap) { while (list_len + n > list_cap) list_cap = list_cap ? 2 * list_cap : 256; code_lists = xrealloc(code_lists, list_cap * sizeof *code_lists); }
    uint32_t at = list_len; list_len += n; return at;
}
static int32_t const_operand(Val v) {
    for (uint32_t i = 0; i < const_len; i++) if (consts[i] == v) return -(int32_t)i - 1;
    if (const_len == const_cap) { const_cap = const_cap ? 2 * const_cap : 64; consts = xrealloc(consts, const_cap * sizeof *consts); }
    consts[const_len] = v;
    return -(int32_t)(const_len++) - 1;
}
static int find_fn(const char *name, uint32_t arity) {
    for (uint32_t i = 0; i < fn_len; i++) if (fns[i].arity == arity && strcmp(fns[i].name, name) == 0) return (int)i;
    return -1;
}
static int add_fn(const char *name, uint32_t arity) {
    int f = find_fn(name, arity);
    if (f >= 0) return f;
    if (fn_len == fn_cap) { fn_cap = fn_cap ? 2 * fn_cap : 32; fns = xrealloc(fns, fn_cap * sizeof *fns); }
    fns[fn_len] = (Fn){strdup(name), arity, 0, 0, false, NULL, 0};
    return (int)fn_len++;
}
static bool fn_name_exists(const char *name) {
    for (uint32_t i = 0; i < fn_len; i++) if (strcmp(fns[i].name, name) == 0) return true;
    return false;
}

/* Compilation scope: variable name -> slot. */
typedef struct { char *name; int32_t slot; } Binding;
typedef struct { Binding *b; int n, cap; int32_t next_slot; int32_t max_slot; } Scope;
static int32_t scope_lookup(Scope *s, const char *name) {
    for (int i = s->n - 1; i >= 0; i--) if (strcmp(s->b[i].name, name) == 0) return s->b[i].slot;
    return -1;
}
static int32_t scope_fresh(Scope *s) {
    int32_t slot = s->next_slot++;
    if (s->next_slot > s->max_slot) s->max_slot = s->next_slot;
    if (s->max_slot > 64) { fprintf(stderr, "too many slots in one function\n"); exit(2); }
    return slot;
}
static void scope_bind(Scope *s, const char *name, int32_t slot) {
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->b = xrealloc(s->b, s->cap * sizeof *s->b); }
    s->b[s->n++] = (Binding){(char *)name, slot};
}

static Val literal_value(const SExp *e, bool *ok) {
    *ok = true;
    if (e->kind == S_INT) return V_INT(e->i);
    if (e->kind == S_SYM) return V_SYM(intern(e->name));
    if (e->kind == S_LIST && e->n == 0) return V_SYM(intern("()"));
    *ok = false; return 0;
}

static int prim_of(const char *s, int n) {
    static const struct { const char *s; int p; int n; } table[] = {
        {"+", P_ADD, 2}, {"-", P_SUB, 2}, {"*", P_MUL, 2}, {"/", P_DIV, 2}, {"%", P_MOD, 2},
        {"==", P_EQ, 2}, {"!=", P_NE, 2}, {"<", P_LT, 2}, {">", P_GT, 2}, {"<=", P_LE, 2}, {">=", P_GE, 2},
        {"and", P_AND, 2}, {"or", P_OR, 2}, {"not", P_NOT, 1},
    };
    for (size_t i = 0; i < sizeof table / sizeof *table; i++)
        if (strcmp(table[i].s, s) == 0 && table[i].n == n) return table[i].p;
    return -1;
}

/* First-argument clause index.  A clause's first pattern is a ground value,
 * a constructor with variables (keyed by constructor and arity), or a
 * variable.  Each key maps to the clauses it can match, in clause order. */
typedef struct { uint64_t key; uint32_t first, n; } IndexEntry;
typedef struct { IndexEntry *slots; size_t cap; uint32_t any_first, any_n; } ClauseIndex;
static ClauseIndex *indexes; static uint32_t index_len, index_cap;
static int32_t *index_targets; static uint32_t itarget_len, itarget_cap;
static uint64_t functor_key(uint32_t ctor, uint32_t arity) { return mix64(((uint64_t)ctor << 20) ^ arity ^ 0xF00DULL) | 1u; }
static uint64_t ground_key(Val v) { return mix64(v ^ 0xABCDEF12345ULL) & ~(uint64_t)1; }
static uint32_t itarget_push(int32_t t) {
    if (itarget_len == itarget_cap) { itarget_cap = itarget_cap ? 2 * itarget_cap : 256; index_targets = xrealloc(index_targets, itarget_cap * sizeof *index_targets); }
    index_targets[itarget_len] = t; return itarget_len++;
}
static IndexEntry *index_slot(ClauseIndex *ix, uint64_t key) {
    size_t j = key & (ix->cap - 1);
    while (ix->slots[j].n && ix->slots[j].key != key) j = (j + 1) & (ix->cap - 1);
    return &ix->slots[j];
}

/* Patch list for fail targets: a pattern mismatch jumps to a FAIL. */
static uint32_t fail_label(void) { return emit((Ins){.op = I_FAIL}); }

/* Match operand `src` against pattern `pat`, binding new variables. */
static void compile_pattern(Scope *s, const SExp *pat, int32_t src) {
    if (pat->kind == S_VAR) {
        if (strcmp(pat->name, "$_") == 0) return;
        int32_t slot = scope_lookup(s, pat->name);
        if (slot < 0) {
            if (src >= 0) { scope_bind(s, pat->name, src); return; }
            int32_t fresh = scope_fresh(s);
            emit((Ins){.op = I_MOVE, .dst = fresh, .a = src});
            scope_bind(s, pat->name, fresh);
            return;
        }
        uint32_t at = emit((Ins){.op = I_EQ, .a = src, .b = slot});
        uint32_t skip = emit((Ins){.op = I_JMP});
        uint32_t f = fail_label();
        code[at].target = (int32_t)f;
        code[skip].target = (int32_t)code_len;
        return;
    }
    bool ok;
    Val lit = literal_value(pat, &ok);
    if (ok) {
        int32_t c = const_operand(lit);
        uint32_t at = emit((Ins){.op = I_EQ, .a = src, .b = c});
        uint32_t skip = emit((Ins){.op = I_JMP});
        uint32_t f = fail_label();
        code[at].target = (int32_t)f;
        code[skip].target = (int32_t)code_len;
        return;
    }
    /* Constructor pattern (C p1 ... pn). */
    if (pat->kind != S_LIST || pat->items[0]->kind != S_SYM) { fprintf(stderr, "bad pattern\n"); exit(2); }
    uint32_t ctor = intern(pat->items[0]->name);
    uint32_t n = (uint32_t)pat->n - 1;
    uint32_t list = list_alloc(n);
    for (uint32_t i = 0; i < n; i++) code_lists[list + i] = scope_fresh(s);
    uint32_t at = emit((Ins){.op = I_MATCH, .a = src, .fn = (int32_t)ctor, .n = (uint16_t)n, .list = list});
    uint32_t skip = emit((Ins){.op = I_JMP});
    uint32_t f = fail_label();
    code[at].target = (int32_t)f;
    code[skip].target = (int32_t)code_len;
    for (uint32_t i = 0; i < n; i++) compile_pattern(s, pat->items[i + 1], code_lists[list + i]);
}

static int32_t compile_expr(Scope *s, const SExp *e, bool tail);

/* Compile e; when tail, the code returns its value; otherwise the result is
 * an operand. */
static int32_t value_or_ret(int32_t operand, bool tail) {
    if (tail) emit((Ins){.op = I_RET, .a = operand});
    return operand;
}

static int32_t compile_expr(Scope *s, const SExp *e, bool tail) {
    if (e->kind == S_VAR) {
        int32_t slot = scope_lookup(s, e->name);
        if (slot < 0) { fprintf(stderr, "unbound variable %s\n", e->name); exit(2); }
        return value_or_ret(slot, tail);
    }
    bool ok;
    Val lit = literal_value(e, &ok);
    if (ok) return value_or_ret(const_operand(lit), tail);
    const SExp *h = e->items[0];
    int nargs = e->n - 1;
    if (is_sym(h, "empty") && nargs == 0) { emit((Ins){.op = I_FAIL}); return 0; }
    if (is_sym(h, "let") && nargs == 3) {
        int32_t v = compile_expr(s, e->items[2], false);
        int saved = s->n;
        compile_pattern(s, e->items[1], v);
        int32_t r = compile_expr(s, e->items[3], tail);
        s->n = saved;
        return r;
    }
    if (is_sym(h, "if") && nargs == 3) {
        int32_t c = compile_expr(s, e->items[1], false);
        uint32_t br = emit((Ins){.op = I_BR, .a = c});
        int32_t dst = tail ? 0 : scope_fresh(s);
        int32_t t = compile_expr(s, e->items[2], tail);
        uint32_t jmp = 0;
        if (!tail) { emit((Ins){.op = I_MOVE, .dst = dst, .a = t}); jmp = emit((Ins){.op = I_JMP}); }
        code[br].target = (int32_t)code_len;
        int32_t f = compile_expr(s, e->items[3], tail);
        if (!tail) { emit((Ins){.op = I_MOVE, .dst = dst, .a = f}); code[jmp].target = (int32_t)code_len; }
        return dst;
    }
    if (is_sym(h, "superpose") && nargs == 1 && e->items[1]->kind == S_LIST) {
        const SExp *alts = e->items[1];
        bool all_lit = true;
        for (int i = 0; i < alts->n; i++) { literal_value(alts->items[i], &ok); if (!ok) all_lit = false; }
        if (alts->n == 0) { emit((Ins){.op = I_FAIL}); return 0; }
        int32_t dst = scope_fresh(s);
        if (all_lit) {
            uint32_t list = list_alloc((uint32_t)alts->n);
            for (int i = 0; i < alts->n; i++) code_lists[list + i] = const_operand(literal_value(alts->items[i], &ok));
            emit((Ins){.op = I_CHOOSE, .dst = dst, .n = (uint16_t)alts->n, .list = list});
            return value_or_ret(dst, tail);
        }
        uint32_t list = list_alloc((uint32_t)alts->n);
        emit((Ins){.op = I_SPLIT, .n = (uint16_t)alts->n, .list = list});
        uint32_t *joins = xmalloc(alts->n * sizeof *joins);
        for (int i = 0; i < alts->n; i++) {
            code_lists[list + i] = (int32_t)code_len;
            int32_t r = compile_expr(s, alts->items[i], tail);
            if (!tail) { emit((Ins){.op = I_MOVE, .dst = dst, .a = r}); joins[i] = emit((Ins){.op = I_JMP}); }
        }
        if (!tail) for (int i = 0; i < alts->n; i++) code[joins[i]].target = (int32_t)code_len;
        free(joins);
        return dst;
    }
    if (h->kind != S_SYM) { fprintf(stderr, "unsupported head\n"); exit(2); }
    int p = prim_of(h->name, nargs);
    if (p >= 0 && !fn_name_exists(h->name)) {
        int32_t a = compile_expr(s, e->items[1], false);
        int32_t b = nargs > 1 ? compile_expr(s, e->items[2], false) : 0;
        int32_t dst = scope_fresh(s);
        emit((Ins){.op = I_PRIM, .prim = (uint8_t)p, .dst = dst, .a = a, .b = b});
        return value_or_ret(dst, tail);
    }
    int f = find_fn(h->name, (uint32_t)nargs);
    uint32_t list = list_alloc((uint32_t)nargs);
    int32_t *ops = xmalloc((nargs ? nargs : 1) * sizeof *ops);
    for (int i = 0; i < nargs; i++) ops[i] = compile_expr(s, e->items[i + 1], false);
    for (int i = 0; i < nargs; i++) code_lists[list + i] = ops[i];
    free(ops);
    if (f < 0) { /* constructor term */
        int32_t dst = scope_fresh(s);
        emit((Ins){.op = I_CONS, .dst = dst, .fn = (int32_t)intern(h->name), .n = (uint16_t)nargs, .list = list});
        return value_or_ret(dst, tail);
    }
    if (tail) { emit((Ins){.op = I_TAIL, .fn = f, .n = (uint16_t)nargs, .list = list}); return 0; }
    int32_t dst = scope_fresh(s);
    emit((Ins){.op = I_CALL, .fn = f, .dst = dst, .n = (uint16_t)nargs, .list = list});
    return dst;
}

static void compile_fn(int f) {
    Fn *fn = &fns[f];
    fn->entry = code_len;
    uint32_t split = 0, list = 0;
    bool indexed = fn->nclauses > 1 && fn->arity > 0;
    if (fn->nclauses > 1) {
        list = list_alloc((uint32_t)fn->nclauses);
        split = emit((Ins){.op = indexed ? I_INDEX : I_SPLIT, .n = (uint16_t)fn->nclauses, .list = list});
    }
    int32_t max_slots = (int32_t)fn->arity;
    for (int c = 0; c < fn->nclauses; c++) {
        if (fn->nclauses > 1) code_lists[list + c] = (int32_t)code_len;
        SExp *clause = fn->clauses[c];
        SExp *head = clause->items[1];
        Scope s = {0};
        s.next_slot = (int32_t)fn->arity; s.max_slot = s.next_slot;
        for (uint32_t i = 0; i < fn->arity; i++) compile_pattern(&s, head->items[i + 1], (int32_t)i);
        compile_expr(&s, clause->items[2], true);
        if (s.max_slot > max_slots) max_slots = s.max_slot;
        free(s.b);
    }
    if (fn->nclauses == 0) emit((Ins){.op = I_FAIL});
    if (indexed) {
        /* Classify each clause by its first pattern. */
        int nc = fn->nclauses;
        uint64_t *keys = xmalloc(nc * sizeof *keys); int *kind = xmalloc(nc * sizeof *kind);
        for (int c = 0; c < nc; c++) {
            SExp *pat = fn->clauses[c]->items[1]->items[1];
            bool ok; Val lit = literal_value(pat, &ok);
            if (ok) { kind[c] = 1; keys[c] = ground_key(lit); continue; }
            if (pat->kind == S_LIST && pat->n > 0 && pat->items[0]->kind == S_SYM) {
                bool ground = true;
                Val kids[16];
                for (int k = 1; k < pat->n && ground; k++) { kids[k - 1] = literal_value(pat->items[k], &ok); if (!ok) ground = false; }
                if (ground && pat->n - 1 <= 16) { kind[c] = 1; keys[c] = ground_key(make_term(intern(pat->items[0]->name), (uint32_t)pat->n - 1, kids)); }
                else { kind[c] = 2; keys[c] = functor_key(intern(pat->items[0]->name), (uint32_t)pat->n - 1); }
                continue;
            }
            kind[c] = 0; keys[c] = 0;
        }
        if (index_len == index_cap) { index_cap = index_cap ? 2 * index_cap : 16; indexes = xrealloc(indexes, index_cap * sizeof *indexes); }
        ClauseIndex *ix = &indexes[index_len];
        ix->cap = 16; while (ix->cap < (size_t)nc * 4) ix->cap *= 2;
        ix->slots = xcalloc(ix->cap, sizeof *ix->slots);
        /* Variable clauses. */
        ix->any_first = itarget_len; ix->any_n = 0;
        for (int c = 0; c < nc; c++) if (kind[c] == 0) { itarget_push(code_lists[list + c]); ix->any_n++; }
        /* Each distinct key: its clauses in order, with the functor's and the variable clauses. */
        for (int c = 0; c < nc; c++) {
            if (kind[c] == 0) continue;
            IndexEntry *e = index_slot(ix, keys[c]);
            if (e->n) continue;
            uint64_t fkey = 0;
            if (kind[c] == 1) {
                SExp *pat = fn->clauses[c]->items[1]->items[1];
                if (pat->kind == S_LIST && pat->n > 0) fkey = functor_key(intern(pat->items[0]->name), (uint32_t)pat->n - 1);
            }
            uint32_t first = itarget_len, n = 0;
            for (int d = 0; d < nc; d++)
                if ((kind[d] == 1 && keys[d] == keys[c]) || kind[d] == 0 ||
                    (kind[d] == 2 && (keys[d] == keys[c] || keys[d] == fkey)))
                    { itarget_push(code_lists[list + d]); n++; }
            e->key = keys[c]; e->first = first; e->n = n;
        }
        free(keys); free(kind);
        code[split].fn = (int32_t)index_len++;
    }
    fn->nslots = (uint32_t)max_slots;
    if (fn->nslots > 64) { fprintf(stderr, "too many slots\n"); exit(2); }
}

/* Liveness: live_in[i] = slots read before written on some path from i. */
static inline uint64_t bit(int32_t operand) { return operand >= 0 ? (UINT64_C(1) << operand) : 0; }
static void compute_liveness(void) {
    live_in = xcalloc(code_len, sizeof *live_in);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int64_t k = (int64_t)code_len - 1; k >= 0; k--) {
            Ins *in = &code[k];
            uint64_t out = 0, use = 0, def = 0, lin;
            uint64_t next = (uint32_t)(k + 1) < code_len && ins_fn[k + 1] == ins_fn[k] ? live_in[k + 1] : 0;
            switch (in->op) {
            case I_MOVE: use = bit(in->a); def = bit(in->dst); out = next; lin = use | (out & ~def); break;
            case I_PRIM: use = bit(in->a) | (in->prim == P_NOT ? 0 : bit(in->b)); def = bit(in->dst); out = next; lin = use | (out & ~def); break;
            case I_CONS: for (uint32_t j = 0; j < in->n; j++) use |= bit(code_lists[in->list + j]); def = bit(in->dst); lin = use | (next & ~def); break;
            case I_MATCH: for (uint32_t j = 0; j < in->n; j++) def |= bit(code_lists[in->list + j]);
                lin = bit(in->a) | (next & ~def) | live_in[in->target]; break;
            case I_EQ: lin = bit(in->a) | bit(in->b) | next | live_in[in->target]; break;
            case I_BR: lin = bit(in->a) | next | live_in[in->target]; break;
            case I_JMP: lin = live_in[in->target]; break;
            case I_CHOOSE: lin = next & ~bit(in->dst); break;
            case I_SPLIT: lin = 0; for (uint32_t j = 0; j < in->n; j++) lin |= live_in[code_lists[in->list + j]]; break;
            case I_INDEX: lin = bit(0); for (uint32_t j = 0; j < in->n; j++) lin |= live_in[code_lists[in->list + j]]; break;
            case I_CALL: for (uint32_t j = 0; j < in->n; j++) use |= bit(code_lists[in->list + j]);
                lin = use | (next & ~bit(in->dst)); break;
            case I_TAIL: for (uint32_t j = 0; j < in->n; j++) use |= bit(code_lists[in->list + j]); lin = use; break;
            case I_RET: lin = bit(in->a); break;
            default: lin = 0; break;
            }
            if (lin != live_in[k]) { live_in[k] = lin; changed = true; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Observation of the answer bag. */

typedef enum { OBS_COUNT, OBS_COLLECT, OBS_SUM } ObsKind;
typedef struct { Val v; uint64_t w; } BagEntry;
typedef struct { BagEntry *e; size_t cap, len; uint64_t total; } Bag;
static void bag_add(Bag *b, Val v, uint64_t w) {
    b->total += w;
    if ((b->len + 1) * 2 >= b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        BagEntry *ne = xcalloc(cap, sizeof *ne);
        for (size_t i = 0; i < b->cap; i++) if (b->e[i].w) {
            size_t j = mix64(b->e[i].v) & (cap - 1);
            while (ne[j].w) j = (j + 1) & (cap - 1);
            ne[j] = b->e[i];
        }
        free(b->e); b->e = ne; b->cap = cap;
    }
    size_t j = mix64(v) & (b->cap - 1);
    while (b->e[j].w && b->e[j].v != v) j = (j + 1) & (b->cap - 1);
    if (!b->e[j].w) { b->e[j].v = v; b->len++; }
    b->e[j].w += w;
}
/* A digest of the bag that does not depend on order: a sum over entries of
 * a hash of (printed value, multiplicity). */
static uint64_t bag_digest(const Bag *b) {
    uint64_t d = 0;
    char buf[4096];
    for (size_t i = 0; i < b->cap; i++) if (b->e[i].w) {
        FILE *m = fmemopen(buf, sizeof buf, "w");
        print_val(m, b->e[i].v); fputc(0, m); fclose(m);
        uint64_t h = 1469598103934665603ULL;
        for (const char *p = buf; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
        d += mix64(h ^ mix64(b->e[i].w));
    }
    return d;
}

/* ------------------------------------------------------------------ */
/* Machine state shared by both schedulers. */

#define MAX_SLOTS 64
typedef struct Frame {
    uint32_t ret_pc;     /* instruction after the call */
    int32_t dst;         /* slot receiving the callee's value */
    uint32_t parent;     /* frame id (merge) or arena index (dfs); 0 = root */
    uint32_t nsaved;
    uint64_t mask;       /* saved slots */
    uint64_t h;          /* structural hash of this frame and its parents (dfs) */
    Val saved[];         /* their values, in slot order */
} Frame;

typedef struct {
    uint64_t steps, events, choice_points, resumes, configs_in, configs_merged, frames, rounds, max_frontier;
    uint64_t answers_weighted, answer_events, switched;
} Stats;
static Stats st;

static inline Val opnd(const Val *slots, int32_t o) { return o >= 0 ? slots[o] : consts[-o - 1]; }

static bool prim_apply(int p, Val a, Val b, Val *out) {
    if (p == P_EQ) { *out = V_SYM(a == b ? SYM_TRUE : SYM_FALSE); return true; }
    if (p == P_NE) { *out = V_SYM(a != b ? SYM_TRUE : SYM_FALSE); return true; }
    if (p == P_NOT) { if (!V_IS_SYM(a)) return false; *out = V_SYM(V_GET_SYM(a) == SYM_TRUE ? SYM_FALSE : SYM_TRUE); return true; }
    if (p == P_AND || p == P_OR) {
        if (!V_IS_SYM(a) || !V_IS_SYM(b)) return false;
        bool x = V_GET_SYM(a) == SYM_TRUE, y = V_GET_SYM(b) == SYM_TRUE;
        *out = V_SYM((p == P_AND ? (x && y) : (x || y)) ? SYM_TRUE : SYM_FALSE); return true;
    }
    if (!V_IS_INT(a) || !V_IS_INT(b)) return false;
    int64_t x = V_GET_INT(a), y = V_GET_INT(b);
    switch (p) {
    case P_ADD: *out = V_INT(x + y); return true;
    case P_SUB: *out = V_INT(x - y); return true;
    case P_MUL: *out = V_INT(x * y); return true;
    case P_DIV: if (!y) return false; *out = V_INT(x / y); return true;
    case P_MOD: if (!y) return false; { int64_t r = x % y; *out = V_INT(r); } return true;
    case P_LT: *out = V_SYM(x < y ? SYM_TRUE : SYM_FALSE); return true;
    case P_GT: *out = V_SYM(x > y ? SYM_TRUE : SYM_FALSE); return true;
    case P_LE: *out = V_SYM(x <= y ? SYM_TRUE : SYM_FALSE); return true;
    case P_GE: *out = V_SYM(x >= y ? SYM_TRUE : SYM_FALSE); return true;
    }
    return false;
}

/* The outcome of running a configuration to its next event. */
typedef enum { EV_FAIL, EV_ANSWER, EV_CHOOSE, EV_SPLIT, EV_CALL, EV_TAIL, EV_RET } Event;
/* The clause list an INDEX instruction selected (an EV_SPLIT over it). */
static const int32_t *index_list; static uint32_t index_n;

/* Run deterministic instructions from *pc over slots until an event. */
static Event run_det(uint32_t *pc, Val *slots, Val *result) {
    for (;;) {
        Ins *in = &code[*pc];
        st.steps++;
        switch (in->op) {
        case I_MOVE: slots[in->dst] = opnd(slots, in->a); (*pc)++; break;
        case I_PRIM: {
            Val out;
            if (!prim_apply(in->prim, opnd(slots, in->a), in->prim == P_NOT ? 0 : opnd(slots, in->b), &out)) return EV_FAIL;
            slots[in->dst] = out; (*pc)++; break;
        }
        case I_CONS: {
            Val kids[16];
            for (uint32_t j = 0; j < in->n; j++) kids[j] = opnd(slots, code_lists[in->list + j]);
            slots[in->dst] = make_term((uint32_t)in->fn, in->n, kids); (*pc)++; break;
        }
        case I_MATCH: {
            Val v = opnd(slots, in->a);
            if (V_IS_TERM(v)) {
                Term *t = &terms[V_GET_TERM(v)];
                if (t->ctor == (uint32_t)in->fn && t->arity == in->n) {
                    for (uint32_t j = 0; j < in->n; j++) slots[code_lists[in->list + j]] = term_kids[t->first + j];
                    (*pc)++; break;
                }
            }
            *pc = (uint32_t)in->target; break;
        }
        case I_EQ: if (opnd(slots, in->a) == opnd(slots, in->b)) (*pc)++; else *pc = (uint32_t)in->target; break;
        case I_BR: {
            Val c = opnd(slots, in->a);
            if (c == V_SYM(SYM_TRUE)) (*pc)++;
            else if (c == V_SYM(SYM_FALSE)) *pc = (uint32_t)in->target;
            else return EV_FAIL;
            break;
        }
        case I_JMP: *pc = (uint32_t)in->target; break;
        case I_CHOOSE: return EV_CHOOSE;
        case I_SPLIT: index_list = code_lists + in->list; index_n = in->n; return EV_SPLIT;
        case I_INDEX: {
            ClauseIndex *ix = &indexes[in->fn];
            Val v = slots[0];
            IndexEntry *e = index_slot(ix, ground_key(v));
            if (!e->n && V_IS_TERM(v)) e = index_slot(ix, functor_key(terms[V_GET_TERM(v)].ctor, terms[V_GET_TERM(v)].arity));
            if (e->n) { index_list = index_targets + e->first; index_n = e->n; }
            else { index_list = index_targets + ix->any_first; index_n = ix->any_n; }
            if (index_n == 0) return EV_FAIL;
            return EV_SPLIT;
        }
        case I_CALL: return EV_CALL;
        case I_TAIL: return EV_TAIL;
        case I_RET: *result = opnd(slots, in->a); return EV_RET;
        case I_FAIL: return EV_FAIL;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Depth-first scheduler: choice points copy the activation's slots;
 * frames live in an arena that backtracking cuts back. */

typedef struct {
    uint32_t pc;        /* the CHOOSE or SPLIT instruction */
    uint32_t next_alt;
    uint32_t nalts;
    const int32_t *alts; /* SPLIT targets */
    uint32_t frame;     /* arena offset of the current frame */
    size_t arena_mark;
    uint32_t nslots;
    Val slots[];        /* copy at the choice point (only nslots used) */
} Choice;

static uint8_t *arena; static size_t arena_len, arena_cap;
static size_t arena_alloc(size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (arena_len + n > arena_cap) { while (arena_len + n > arena_cap) arena_cap = arena_cap ? 2 * arena_cap : (1u << 20); arena = xrealloc(arena, arena_cap); }
    size_t at = arena_len; arena_len += n; return at;
}
#define FRAME_AT(off) ((Frame *)(arena + (off)))

static uint8_t *cstack; static size_t cstack_len, cstack_cap; static size_t *cstack_tops; static size_t ctop_len, ctop_cap;

static bool hybrid = false;
static uint64_t *sample_table; static uint64_t samples, sample_hits;
#define SAMPLE_BITS 16
static uint64_t config_hash(uint32_t pc, uint64_t frame_h, const Val *slots) {
    uint64_t h = mix64(((uint64_t)pc << 32) ^ frame_h);
    for (uint64_t m = live_in[pc]; m; m &= m - 1) h = mix64(h ^ slots[__builtin_ctzll(m)]);
    return h;
}
static void merge_init(void);
static void merge_loop_from_dfs(uint32_t pc, const Val *slots, uint32_t frame, Bag *bag);

static void dfs_run(uint32_t entry, Bag *bag) {
    Val slots[MAX_SLOTS];
    memset(slots, 0, sizeof slots);
    uint32_t pc = entry;
    arena_len = 0; arena_alloc(sizeof(Frame)); /* offset 0 = root */
    FRAME_AT(0)->h = 0x5bd1e995u;
    uint32_t frame = 0;
    cstack_len = 0; ctop_len = 0;
    if (hybrid) { if (!sample_table) sample_table = xcalloc((size_t)1 << SAMPLE_BITS, sizeof *sample_table); samples = sample_hits = 0; }
    for (;;) {
        Val result = 0;
        Event ev = run_det(&pc, slots, &result);
        st.events++;
        Ins *in = &code[pc];
        bool fail = false;
        switch (ev) {
        case EV_FAIL: fail = true; break;
        case EV_CHOOSE: case EV_SPLIT: {
            uint32_t n = ev == EV_SPLIT ? index_n : in->n;
            const int32_t *alts = ev == EV_SPLIT ? index_list : NULL;
            uint32_t ns = fns[ins_fn[pc]].nslots;
            if (n > 1) {
                size_t sz = sizeof(Choice) + ns * sizeof(Val);
                if (cstack_len + sz > cstack_cap) { while (cstack_len + sz > cstack_cap) cstack_cap = cstack_cap ? 2 * cstack_cap : (1u << 20); cstack = xrealloc(cstack, cstack_cap); }
                if (ctop_len == ctop_cap) { ctop_cap = ctop_cap ? 2 * ctop_cap : 1024; cstack_tops = xrealloc(cstack_tops, ctop_cap * sizeof *cstack_tops); }
                Choice *c = (Choice *)(cstack + cstack_len);
                c->pc = pc; c->next_alt = 1; c->frame = frame; c->arena_mark = arena_len; c->nslots = ns;
                c->nalts = n; c->alts = alts;
                memcpy(c->slots, slots, ns * sizeof(Val));
                cstack_tops[ctop_len++] = cstack_len;
                cstack_len += sz;
                st.choice_points++;
            }
            if (ev == EV_CHOOSE) { slots[in->dst] = opnd(slots, code_lists[in->list]); pc++; }
            else pc = (uint32_t)alts[0];
            break;
        }
        case EV_CALL: case EV_TAIL: {
            Fn *callee = &fns[in->fn];
            Val args[MAX_SLOTS];
            for (uint32_t j = 0; j < in->n; j++) args[j] = opnd(slots, code_lists[in->list + j]);
            if (ev == EV_CALL) {
                uint64_t mask = live_in[pc + 1] & ~bit(in->dst);
                uint32_t nsaved = (uint32_t)__builtin_popcountll(mask);
                size_t off = arena_alloc(sizeof(Frame) + nsaved * sizeof(Val));
                Frame *fr = FRAME_AT(off);
                fr->ret_pc = pc + 1; fr->dst = in->dst; fr->parent = frame; fr->nsaved = nsaved; fr->mask = mask;
                uint32_t k = 0;
                uint64_t h = mix64(((uint64_t)(pc + 1) << 32) ^ (uint32_t)in->dst ^ FRAME_AT(frame)->h);
                for (uint64_t m = mask; m; m &= m - 1) { fr->saved[k] = slots[__builtin_ctzll(m)]; h = mix64(h ^ fr->saved[k]); k++; }
                fr->h = h;
                frame = (uint32_t)off;
                st.frames++;
            }
            memcpy(slots, args, in->n * sizeof(Val));
            pc = callee->entry;
            if (hybrid && ((st.events >> 12) & 7u) == 0) goto sample;
            break;
        }
        case EV_RET: {
            if (frame == 0) { bag_add(bag, result, 1); st.answer_events++; fail = true; break; }
            Frame *fr = FRAME_AT(frame);
            uint32_t k = 0;
            for (uint64_t m = fr->mask; m; m &= m - 1) slots[__builtin_ctzll(m)] = fr->saved[k++];
            slots[fr->dst] = result;
            pc = fr->ret_pc;
            frame = fr->parent;
            if (hybrid && ((st.events >> 12) & 7u) == 0) goto sample;
            break;
        }
        default: fail = true; break;
        }
        if (!fail) continue;
        if (0) {
        sample: {
            /* Sample the configuration key; a repeated key is a reconvergence. */
            uint64_t h = config_hash(pc, FRAME_AT(frame)->h, slots);
            uint64_t *slot = &sample_table[h & (((uint64_t)1 << SAMPLE_BITS) - 1)];
            samples++;
            if (*slot == h) sample_hits++; else *slot = h;
            if ((samples & 1023u) == 0 && samples >= 4096 && sample_hits * 4 >= samples) {
                st.switched = samples;
                merge_loop_from_dfs(pc, slots, frame, bag);
                return;
            }
            continue;
        }
        }
        /* Backtrack to the most recent choice point. */
        if (ctop_len == 0) return;
        Choice *c = (Choice *)(cstack + cstack_tops[ctop_len - 1]);
        Ins *cin = &code[c->pc];
        uint32_t alt = c->next_alt++;
        memcpy(slots, c->slots, c->nslots * sizeof(Val));
        frame = c->frame;
        arena_len = c->arena_mark;
        st.resumes++;
        if (cin->op == I_CHOOSE) { slots[cin->dst] = opnd(slots, code_lists[cin->list + alt]); pc = c->pc + 1; }
        else pc = (uint32_t)c->alts[alt];
        if (c->next_alt >= c->nalts) { cstack_len = cstack_tops[--ctop_len]; }
    }
}

/* ------------------------------------------------------------------ */
/* Reconvergent weighted frontier.  Frames are hash-consed by
 * (return pc, destination, saved live values, parent frame). */

static Frame **mframes; static uint32_t mframe_len, mframe_cap;
static uint32_t *mframe_table; static size_t mframe_table_cap;

static uint64_t frame_hash(uint32_t ret_pc, int32_t dst, uint32_t parent, uint32_t n, const Val *v) {
    uint64_t h = mix64(((uint64_t)ret_pc << 32) ^ (uint32_t)dst ^ ((uint64_t)parent * 0x9e3779b97f4a7c15ULL));
    for (uint32_t i = 0; i < n; i++) h = mix64(h ^ v[i]);
    return h;
}
static void mframe_grow(void) {
    size_t cap = mframe_table_cap ? 2 * mframe_table_cap : 1024;
    uint32_t *table = xcalloc(cap, sizeof *table);
    for (uint32_t i = 1; i < mframe_len; i++) {
        Frame *f = mframes[i];
        size_t j = frame_hash(f->ret_pc, f->dst, f->parent, f->nsaved, f->saved) & (cap - 1);
        while (table[j]) j = (j + 1) & (cap - 1);
        table[j] = i;
    }
    free(mframe_table); mframe_table = table; mframe_table_cap = cap;
}
static uint32_t mframe_intern(uint32_t ret_pc, int32_t dst, uint32_t parent, uint64_t mask, const Val *slots) {
    Val saved[MAX_SLOTS]; uint32_t n = 0;
    for (uint64_t m = mask; m; m &= m - 1) saved[n++] = slots[__builtin_ctzll(m)];
    if ((mframe_len + 1) * 2 >= mframe_table_cap) mframe_grow();
    size_t j = frame_hash(ret_pc, dst, parent, n, saved) & (mframe_table_cap - 1);
    while (mframe_table[j]) {
        Frame *f = mframes[mframe_table[j]];
        if (f->ret_pc == ret_pc && f->dst == dst && f->parent == parent && f->nsaved == n &&
            memcmp(f->saved, saved, n * sizeof(Val)) == 0)
            return mframe_table[j];
        j = (j + 1) & (mframe_table_cap - 1);
    }
    if (mframe_len == mframe_cap) { mframe_cap = mframe_cap ? 2 * mframe_cap : 1024; mframes = xrealloc(mframes, mframe_cap * sizeof *mframes); }
    Frame *f = xmalloc(sizeof(Frame) + n * sizeof(Val));
    f->ret_pc = ret_pc; f->dst = dst; f->parent = parent; f->nsaved = n; f->mask = mask;
    memcpy(f->saved, saved, n * sizeof(Val));
    mframes[mframe_len] = f;
    mframe_table[j] = mframe_len;
    st.frames++;
    return mframe_len++;
}

/* A configuration: program point, frame, live slot values, weight. */
typedef struct {
    uint32_t pc, frame, nlive, pad;
    uint64_t weight;
    uint64_t hash;
    Val live[];
} Config;

typedef struct {
    uint8_t *data; size_t len, cap;     /* packed configurations */
    uint32_t *table; size_t table_cap;  /* offsets + 1 into data */
    size_t count;
} Frontier;

static size_t config_size(uint32_t nlive) { return (sizeof(Config) + nlive * sizeof(Val) + 7) & ~(size_t)7; }

static bool merging_enabled = true;
static bool adaptive = false;
static uint64_t *pc_inserts, *pc_merges; /* adaptive statistics per program point */
static bool *pc_merge_off;

static void frontier_reset(Frontier *f) {
    f->len = 0; f->count = 0;
    if (f->table) memset(f->table, 0, f->table_cap * sizeof *f->table);
}
static void frontier_table_grow(Frontier *f) {
    size_t cap = f->table_cap ? 2 * f->table_cap : 4096;
    uint32_t *table = xcalloc(cap, sizeof *table);
    for (size_t off = 0; off < f->len; ) {
        Config *c = (Config *)(f->data + off);
        if (c->hash != 0) {
            size_t j = c->hash & (cap - 1);
            while (table[j]) j = (j + 1) & (cap - 1);
            table[j] = (uint32_t)(off / 8) + 1;
        }
        off += config_size(c->nlive);
    }
    free(f->table); f->table = table; f->table_cap = cap;
}
static void frontier_add(Frontier *f, uint32_t pc, uint32_t frame, const Val *slots, uint64_t weight) {
    uint64_t mask = live_in[pc];
    uint32_t nlive = (uint32_t)__builtin_popcountll(mask);
    Val live[MAX_SLOTS]; uint32_t k = 0;
    for (uint64_t m = mask; m; m &= m - 1) live[k++] = slots[__builtin_ctzll(m)];
    st.configs_in++;
    bool merge = merging_enabled && !(adaptive && pc_merge_off[pc]);
    uint64_t h = 0;
    if (merge) {
        h = mix64(((uint64_t)pc << 32) ^ frame);
        for (uint32_t i = 0; i < nlive; i++) h = mix64(h ^ live[i]);
        if (h == 0) h = 1;
        if ((f->count + 1) * 2 >= f->table_cap) frontier_table_grow(f);
        size_t j = h & (f->table_cap - 1);
        if (adaptive) pc_inserts[pc]++;
        while (f->table[j]) {
            Config *c = (Config *)(f->data + (size_t)(f->table[j] - 1) * 8);
            if (c->hash == h && c->pc == pc && c->frame == frame && c->nlive == nlive &&
                memcmp(c->live, live, nlive * sizeof(Val)) == 0) {
                c->weight += weight;
                st.configs_merged++;
                if (adaptive) pc_merges[pc]++;
                return;
            }
            j = (j + 1) & (f->table_cap - 1);
        }
        f->table[j] = (uint32_t)(f->len / 8) + 1;
    }
    size_t sz = config_size(nlive);
    if (f->len + sz > f->cap) { while (f->len + sz > f->cap) f->cap = f->cap ? 2 * f->cap : (1u << 20); f->data = xrealloc(f->data, f->cap); }
    Config *c = (Config *)(f->data + f->len);
    c->pc = pc; c->frame = frame; c->nlive = nlive; c->weight = weight; c->hash = h;
    memcpy(c->live, live, nlive * sizeof(Val));
    f->len += sz;
    f->count++;
}

static size_t max_frontier_bytes = (size_t)8 << 30;

static void merge_init(void) {
    mframe_len = 1; /* id 0 = root */
    if (!mframes) { mframe_cap = 1024; mframes = xmalloc(mframe_cap * sizeof *mframes); }
    free(mframe_table); mframe_table = NULL; mframe_table_cap = 0;
    mframe_grow();
}
static void merge_loop(Frontier cur, Bag *bag);
static void merge_run(uint32_t entry, Bag *bag) {
    merge_init();
    Frontier cur = {0};
    Val slots[MAX_SLOTS]; memset(slots, 0, sizeof slots);
    frontier_add(&cur, entry, 0, slots, 1);
    merge_loop(cur, bag);
}
static void merge_loop(Frontier cur, Bag *bag) {
    Frontier next = {0};
    Val slots[MAX_SLOTS]; memset(slots, 0, sizeof slots);
    while (cur.count) {
        st.rounds++;
        if (cur.count > st.max_frontier) st.max_frontier = cur.count;
        frontier_reset(&next);
        for (size_t off = 0; off < cur.len; ) {
            Config *c = (Config *)(cur.data + off);
            off += config_size(c->nlive);
            uint32_t pc = c->pc; uint32_t frame = c->frame; uint64_t w = c->weight;
            uint64_t mask = live_in[pc]; uint32_t k = 0;
            for (uint64_t m = mask; m; m &= m - 1) slots[__builtin_ctzll(m)] = c->live[k++];
            Val result = 0;
            Event ev = run_det(&pc, slots, &result);
            st.events++;
            Ins *in = &code[pc];
            switch (ev) {
            case EV_FAIL: break;
            case EV_CHOOSE:
                for (uint32_t a = 0; a < in->n; a++) {
                    slots[in->dst] = opnd(slots, code_lists[in->list + a]);
                    frontier_add(&next, pc + 1, frame, slots, w);
                }
                break;
            case EV_SPLIT: {
                const int32_t *alts = index_list; uint32_t n = index_n;
                for (uint32_t a = 0; a < n; a++) frontier_add(&next, (uint32_t)alts[a], frame, slots, w);
                break;
            }
            case EV_CALL: case EV_TAIL: {
                Fn *callee = &fns[in->fn];
                Val args[MAX_SLOTS];
                for (uint32_t j = 0; j < in->n; j++) args[j] = opnd(slots, code_lists[in->list + j]);
                uint32_t nf = frame;
                if (ev == EV_CALL) nf = mframe_intern(pc + 1, in->dst, frame, live_in[pc + 1] & ~bit(in->dst), slots);
                frontier_add(&next, callee->entry, nf, args, w);
                break;
            }
            case EV_RET:
                if (frame == 0) { bag_add(bag, result, w); st.answer_events++; break; }
                {
                    Frame *fr = mframes[frame];
                    uint32_t j = 0;
                    for (uint64_t m = fr->mask; m; m &= m - 1) slots[__builtin_ctzll(m)] = fr->saved[j++];
                    slots[fr->dst] = result;
                    frontier_add(&next, fr->ret_pc, fr->parent, slots, w);
                }
                break;
            default: break;
            }
        }
        if (next.len > max_frontier_bytes) { fprintf(stderr, "frontier exceeds its byte budget\n"); exit(3); }
        if (adaptive) {
            /* Stop hashing at program points whose inserts rarely merge. */
            for (uint32_t p = 0; p < code_len; p++)
                if (!pc_merge_off[p] && pc_inserts[p] >= 4096 && pc_merges[p] * 16 < pc_inserts[p])
                    pc_merge_off[p] = true;
        }
        Frontier t = cur; cur = next; next = t;
    }
    free(cur.data); free(cur.table); free(next.data); free(next.table);
}

/* Intern an arena frame chain as hash-consed frames. */
static uint32_t intern_dfs_frame(uint32_t off) {
    if (off == 0) return 0;
    Frame *fr = FRAME_AT(off);
    uint32_t parent = intern_dfs_frame(fr->parent);
    Val slots[MAX_SLOTS];
    uint32_t k = 0;
    for (uint64_t m = fr->mask; m; m &= m - 1) slots[__builtin_ctzll(m)] = fr->saved[k++];
    return mframe_intern(fr->ret_pc, fr->dst, parent, fr->mask, slots);
}

/* The depth-first state is the current configuration plus every untried
 * alternative of every choice point: together they are the frontier. */
static void merge_loop_from_dfs(uint32_t pc, const Val *slots, uint32_t frame, Bag *bag) {
    merge_init();
    Frontier cur = {0};
    frontier_add(&cur, pc, intern_dfs_frame(frame), slots, 1);
    for (size_t t = 0; t < ctop_len; t++) {
        Choice *c = (Choice *)(cstack + cstack_tops[t]);
        Ins *cin = &code[c->pc];
        uint32_t f = intern_dfs_frame(c->frame);
        Val s2[MAX_SLOTS];
        memcpy(s2, c->slots, c->nslots * sizeof(Val));
        for (uint32_t alt = c->next_alt; alt < c->nalts; alt++) {
            if (cin->op == I_CHOOSE) {
                s2[cin->dst] = opnd(s2, code_lists[cin->list + alt]);
                frontier_add(&cur, c->pc + 1, f, s2, 1);
            } else {
                frontier_add(&cur, (uint32_t)c->alts[alt], f, s2, 1);
            }
        }
    }
    ctop_len = 0; cstack_len = 0;
    merge_loop(cur, bag);
}

/* ------------------------------------------------------------------ */
/* Programs. */

static ObsKind obs_kind;
static uint32_t main_fn;

static void load_program(const char *text) {
    src_pos = text;
    SExp *query = NULL;
    SExp **clauses = NULL; int ncl = 0, ccap = 0;
    for (;;) {
        skip_ws();
        if (!*src_pos) break;
        if (*src_pos == '!') { src_pos++; query = parse_sexp(); continue; }
        SExp *e = parse_sexp();
        if (!e) break;
        if (e->kind == S_LIST && e->n == 3 && is_sym(e->items[0], "=") && e->items[1]->kind == S_LIST) {
            if (ncl == ccap) { ccap = ccap ? 2 * ccap : 16; clauses = xrealloc(clauses, ccap * sizeof *clauses); }
            clauses[ncl++] = e;
        }
    }
    if (!query) { fprintf(stderr, "no query\n"); exit(2); }
    SYM_TRUE = intern("True"); SYM_FALSE = intern("False");
    for (int i = 0; i < ncl; i++) {
        SExp *head = clauses[i]->items[1];
        int f = add_fn(head->items[0]->name, (uint32_t)head->n - 1);
        Fn *fn = &fns[f];
        fn->clauses = xrealloc(fn->clauses, (fn->nclauses + 1) * sizeof *fn->clauses);
        fn->clauses[fn->nclauses++] = clauses[i];
    }
    /* The query becomes the body of a nullary function. */
    SExp *body = query;
    if (query->kind == S_LIST && query->n == 2 && is_sym(query->items[0], "size-atom") &&
        query->items[1]->kind == S_LIST && query->items[1]->n == 2 && is_sym(query->items[1]->items[0], "collapse")) {
        obs_kind = OBS_COUNT; body = query->items[1]->items[1];
    } else if (query->kind == S_LIST && query->n == 2 && is_sym(query->items[0], "collapse")) {
        obs_kind = OBS_COLLECT; body = query->items[1];
    } else if (query->kind == S_LIST && query->n == 2 && is_sym(query->items[0], "foldall-sum")) {
        obs_kind = OBS_SUM; body = query->items[1];
    } else if (query->kind == S_LIST && query->n == 4 && is_sym(query->items[0], "foldall") &&
               is_sym(query->items[1], "merge")) {
        obs_kind = OBS_SUM; body = query->items[2];
    } else { fprintf(stderr, "unsupported query\n"); exit(2); }
    int m = add_fn("__query", 0);
    SExp *head = xcalloc(1, sizeof *head); head->kind = S_LIST; head->n = 1;
    head->items = xmalloc(sizeof *head->items);
    head->items[0] = xcalloc(1, sizeof(SExp)); head->items[0]->kind = S_SYM; head->items[0]->name = "__query";
    SExp *cl = xcalloc(1, sizeof *cl); cl->kind = S_LIST; cl->n = 3; cl->items = xmalloc(3 * sizeof *cl->items);
    cl->items[0] = NULL; cl->items[1] = head; cl->items[2] = body;
    fns[m].clauses = xmalloc(sizeof *fns[m].clauses);
    fns[m].clauses[0] = cl; fns[m].nclauses = 1;
    main_fn = (uint32_t)m;
    for (uint32_t f = 0; f < fn_len; f++) {
        uint32_t start = code_len;
        compile_fn((int)f);
        ins_fn = xrealloc(ins_fn, code_len * sizeof *ins_fn);
        for (uint32_t k = start; k < code_len; k++) ins_fn[k] = f;
    }
    compute_liveness();
    pc_inserts = xcalloc(code_len, sizeof *pc_inserts);
    pc_merges = xcalloc(code_len, sizeof *pc_merges);
    pc_merge_off = xcalloc(code_len, sizeof *pc_merge_off);
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static uint64_t observe(const Bag *b) {
    if (obs_kind == OBS_COUNT) return b->total;
    if (obs_kind == OBS_SUM) {
        uint64_t s = 0;
        for (size_t i = 0; i < b->cap; i++) if (b->e[i].w && V_IS_INT(b->e[i].v)) s += (uint64_t)V_GET_INT(b->e[i].v) * b->e[i].w;
        return s;
    }
    return b->total;
}

static void report(const char *engine, const Bag *b, double secs, bool stats) {
    printf("%s\tresult=%" PRIu64 "\tanswers=%" PRIu64 "\tdistinct=%zu\tdigest=%016" PRIx64 "\tseconds=%.4f",
           engine, observe(b), b->total, b->len, bag_digest(b), secs);
    if (stats)
        printf("\tsteps=%" PRIu64 "\tevents=%" PRIu64 "\tchoice_points=%" PRIu64 "\tresumes=%" PRIu64
               "\tconfigs_in=%" PRIu64 "\tconfigs_merged=%" PRIu64 "\tframes=%" PRIu64 "\trounds=%" PRIu64
               "\tmax_frontier=%" PRIu64 "\tswitched_after=%" PRIu64,
               st.steps, st.events, st.choice_points, st.resumes, st.configs_in, st.configs_merged,
               st.frames, st.rounds, st.max_frontier, st.switched);
    printf("\n");
    fflush(stdout);
}

static bool print_bag = false;
static void dump_bag(const Bag *b) {
    for (size_t i = 0; i < b->cap; i++) if (b->e[i].w) {
        printf("bag\t"); print_val(stdout, b->e[i].v); printf("\t%" PRIu64 "\n", b->e[i].w);
    }
}

static int run_file(const char *path, const char *engine, bool stats) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = xmalloc((size_t)n + 1);
    if (fread(text, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; }
    text[n] = 0; fclose(f);
    load_program(text);
    uint32_t entry = fns[main_fn].entry;
    if (!strcmp(engine, "dfs") || !strcmp(engine, "both") || !strcmp(engine, "all")) {
        Bag b = {0}; memset(&st, 0, sizeof st); hybrid = false;
        double t0 = now(); dfs_run(entry, &b); double t1 = now();
        report("dfs", &b, t1 - t0, stats);
        if (print_bag) dump_bag(&b);
    }
    if (!strcmp(engine, "hybrid") || !strcmp(engine, "all")) {
        Bag b = {0}; memset(&st, 0, sizeof st); hybrid = true; merging_enabled = true;
        double t0 = now(); dfs_run(entry, &b); double t1 = now();
        hybrid = false;
        report("hybrid", &b, t1 - t0, stats);
    }
    if (!strcmp(engine, "merge") || !strcmp(engine, "both") || !strcmp(engine, "frontier") || !strcmp(engine, "all")) {
        Bag b = {0}; memset(&st, 0, sizeof st);
        merging_enabled = strcmp(engine, "frontier") != 0;
        double t0 = now(); merge_run(entry, &b); double t1 = now();
        report(merging_enabled ? (adaptive ? "merge-adaptive" : "merge") : "frontier", &b, t1 - t0, stats);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *engine = "both"; bool stats = false; const char *file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine") && i + 1 < argc) engine = argv[++i];
        else if (!strcmp(argv[i], "--stats")) stats = true;
        else if (!strcmp(argv[i], "--adaptive")) adaptive = true;
        else if (!strcmp(argv[i], "--print-bag")) print_bag = true;
        else file = argv[i];
    }
    if (!file) { fprintf(stderr, "usage: ndsearch [--engine dfs|merge|frontier|both] [--adaptive] [--stats] FILE\n"); return 2; }
    return run_file(file, engine, stats);
}
