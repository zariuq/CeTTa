/* tcalc_cetta_module.c - tcalc as a module of MeTTa: (import! &self tcalc).
 *
 *   (tcalc:load PATH)                      the program of a file, in any of the
 *                                          three notations, as an expression of its
 *                                          forms in the canonical form
 *   (tcalc:outcomes PROGRAM PROFILE)       a run, as `cetta --lang tcalc` prints it:
 *                                          per interpretation its line, then
 *                                          (outcome ROW ...), (ambiguous ...) and
 *                                          (explored ...)
 *   (tcalc:step PROGRAM PROFILE STATE)     one step from a state, an expression of
 *                                          rows: (successor ROW ...) per state reached
 *   (tcalc:query PROGRAM PROFILE PATTERN)  in each outcome, the rows a read of
 *                                          PATTERN can select:
 *                                          (answers (outcome ROW ...) ROW ...)
 *
 * PROGRAM is an expression of the program's forms as a .tcalc file writes them,
 * ((step one) (token 1) (rule a ...)), so a program is MeTTa data that MeTTa
 * can build and take apart.  PROFILE is a string of axis settings, "" for none.
 * A variable in a row is an unknown of the state; an unknown in an answer is
 * the variable $uN.  A refusal is an Error holding the runner's message.
 *
 * Every operation runs the same runner as the command line, on the canonical
 * text of its arguments, so a program answers here as it does there. */
#define _POSIX_C_SOURCE 200809L   /* open_memstream */

#include "tcalc_cetta_module.h"
#include "tcalc_cli.h"
#include "tcalc_surface.h"
#include "tcalc_syntax.h"
#include "eval.h"
#include "grounded.h"
#include "library.h"
#include "parser.h"
#include "petta_semantics.h"
#include "relational_plan.h"
#include "space_theory.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TCALC_HEAD "__cetta_lib_tcalc_"

static bool is_string(const Atom *x) {
    return x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING;
}

/* A form as the runner reads it.  A variable is written by its identity,
 * $vBASE_EPOCH: MeTTa prints a variable by its name, and two variables
 * renamed by one let or call can print alike, but no two share an identity.
 * Nesting is as deep as the reader's own (tcalc_syntax.c reads recursively). */
static void print_form(Atom *x, FILE *f) {
    if (x->kind == ATOM_VAR) {
        fprintf(f, "$v%" PRIu64 "_%" PRIu64, x->var_id.base, x->var_id.epoch.word);
    } else if (x->kind == ATOM_EXPR) {
        fputc('(', f);
        for (CettaExprLen i = 0; i < x->expr.len; i++) {
            if (i) fputc(' ', f);
            print_form(x->expr.elems[i], f);
        }
        fputc(')', f);
    } else {
        atom_print(x, f);
    }
}

/* Text the runner reads: each item of an expression on a line of its own,
 * each written as (put ITEM) when `rows`. */
static char *lines_of(Atom *items, bool rows, size_t *length) {
    char *buf = NULL;
    *length = 0;
    FILE *f = open_memstream(&buf, length);
    if (!f) return NULL;
    for (CettaExprLen i = 0; i < items->expr.len; i++) {
        if (rows) fputs("(put ", f);
        print_form(items->expr.elems[i], f);
        fputs(rows ? ")\n" : "\n", f);
    }
    fclose(f);
    return buf;
}

static char *text_of(Atom *x, size_t *length) {
    char *buf = NULL;
    *length = 0;
    FILE *f = open_memstream(&buf, length);
    if (!f) return NULL;
    print_form(x, f);
    fclose(f);
    return buf;
}

/* The runner's output as atoms, one per line.  An unknown, which tcalc prints
 * ?N, is written as the variable $uN, so that rows read back as a state keep
 * their unknowns. */
static Atom *output_atoms(Arena *a, const char *text) {
    size_t n = strlen(text);
    char *rewritten = malloc(2 * n + 1);
    if (!rewritten) return NULL;
    char *w = rewritten;
    for (size_t i = 0; i < n; i++) {
        bool starts = text[i] == '?' && i + 1 < n && isdigit((unsigned char)text[i + 1]) &&
                      (i == 0 || text[i - 1] == ' ' || text[i - 1] == '(');
        if (starts) { *w++ = '$'; *w++ = 'u'; continue; }
        *w++ = text[i];
    }
    *w = 0;
    Atom **atoms = NULL;
    int count = parse_metta_text(rewritten, a, &atoms);
    free(rewritten);
    if (count < 0) { free(atoms); return NULL; }
    Atom **kept = arena_alloc(a, sizeof(Atom *) * (size_t)(count ? count : 1));
    for (int i = 0; i < count; i++) kept[i] = atoms[i];
    free(atoms);
    return atom_expr(a, kept, (CettaExprLen)count);
}

/* (Error (tcalc:OPERATION ARG ...) MESSAGE), naming the operation as lib/tcalc.metta does. */
static Atom *refusal(Arena *a, Atom *head, Atom **args, uint32_t nargs, const char *message) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (nargs + 1u));
    char public_name[64];
    snprintf(public_name, sizeof public_name, "tcalc:%s", atom_name_cstr(head) + strlen(TCALC_HEAD));
    for (char *p = public_name + 6; *p; p++) {
        if (*p == '_') *p = '-';
    }
    items[0] = atom_symbol(a, public_name);
    for (uint32_t i = 0; i < nargs; i++) items[i + 1] = args[i];
    size_t n = strlen(message);
    char *trimmed = arena_alloc(a, n + 1);
    memcpy(trimmed, message, n + 1);
    while (n > 0 && trimmed[n - 1] == '\n') trimmed[--n] = 0;
    return atom_error(a, atom_expr(a, items, nargs + 1u), atom_string(a, trimmed));
}

/* The program of a file, in the notation its suffix names, as canonical text. */
static char *load_text(const char *path, char *message, size_t message_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(message, message_size, "error: cannot open %s", path); return NULL; }
    char *text = NULL;
    size_t length = 0, cap = 0;
    for (;;) {
        if (length + 1 >= cap) {
            cap = cap ? cap * 2 : 65536;
            char *g = realloc(text, cap);
            if (!g) { free(text); fclose(f); snprintf(message, message_size, "error: out of memory"); return NULL; }
            text = g;
        }
        size_t got = fread(text + length, 1, cap - length - 1, f);
        length += got;
        if (got == 0) break;
    }
    fclose(f);
    text[length] = 0;
    TcSurface surface = tc_surface_of_path(path);
    if (surface == TC_SURFACE_TCALC) return text;
    TcCanonical c;
    TcSyntaxError e;
    bool ok = tc_surface_translate(surface, text, length, &c, &e);
    free(text);
    if (!ok) {
        snprintf(message, message_size, "error: %zu:%zu: %s", e.line, e.column, e.message);
        return NULL;
    }
    char *canonical = malloc(c.length + 1);
    if (canonical) { memcpy(canonical, c.text, c.length); canonical[c.length] = 0; }
    tc_canonical_free(&c);
    return canonical;
}

/* ---- Live Space definitions compiled to demand-driven tcalc rules ---------
 * Compiles a live Space's reachable first-order function definitions (`=`),
 * local `let` bindings, and `(match &self (rel ...) body)` queries into
 * demand-driven tcalc rules guarded by per-head version counters
 * (`SpaceHeadSetStamp`).
 *
 * Store discipline is chosen by what the caller observes:
 *   - Multiset (default): each definition clause and each data occurrence has
 *     a unique identity; `(__deriv call tag subcalls value)` records the
 *     derivation hypergraph, `(__dedge caller callee)` records the demand
 *     graph for exact cycle detection, and a linear bottom-up semiring pass
 *     computes exact MeTTa answer multiplicities while keeping tabling speed.
 *   - Presence-only (set store, e.g. inside `unique`): runs under
 *     `(store set) (step every)` directly without derivation counting so
 *     cyclic relations terminate and deduplicate.
 *
 * Every compiled entry is cached per (space, entry_head, presence_only) under
 * `SpaceHeadSetStamp` over every consulted head symbol (defined function
 * heads, inert constructor heads, and queried data relation heads). */

#define TCALC_SPACE_CACHE_BOUND 64u
#define TCALC_SPACE_MAX_DEPTH 2048u
#define TCALC_SPACE_MAX_HEADS 512u
#define TCALC_SPACE_MAX_LOCALS 512u
#define TCALC_SPACE_MAX_ANSWERS 65536u
#define TCALC_SPACE_MAX_SLG_CALLS 262144u
#define TCALC_SPACE_MAX_SLG_DERIVS 524288u
#define TCALC_SPACE_BUDGET UINT64_C(2000000)
#define TCALC_DATA_IDX_COLS 4u
#define TCALC_SPACE_MAX_JOIN_LEGS 16u
#define TCALC_SPACE_MAX_HO_CAPS 32u

typedef enum {
    TCPRIM_NONE = 0,
    TCPRIM_ADD,
    TCPRIM_SUB,
    TCPRIM_MUL,
    TCPRIM_DIV,
    TCPRIM_MOD,
    TCPRIM_EQ,
    TCPRIM_NEQ,
    TCPRIM_LT,
    TCPRIM_LE,
    TCPRIM_GT,
    TCPRIM_GE,
    TCPRIM_AND,
    TCPRIM_OR,
    TCPRIM_NOT,
    TCPRIM_CAR,
    TCPRIM_CDR,
    TCPRIM_CONS,
    TCPRIM_DECONS,
    TCPRIM_SIZE,
    TCPRIM_INDEX,
} TcalcPrimOp;

typedef enum {
    TCEXEC_CALL = 0,
    TCEXEC_HO_CALL,
    TCEXEC_HO_LIST,
    TCEXEC_QUERY,
    TCEXEC_JOIN,
    TCEXEC_PRIM,
    TCEXEC_MATCH,
    TCEXEC_TRY_MATCH,
    TCEXEC_MOV,
    TCEXEC_IF,
    TCEXEC_JUMP,
    TCEXEC_FORK,
    TCEXEC_SUPERPOSE,
    TCEXEC_EMPTY,
    TCEXEC_RETURN,
} TcalcExecOpKind;

typedef enum {
    TCHO_MAP = 0,
    TCHO_FILTER,
    TCHO_FOLDL,
} TcalcHoListKind;

typedef struct {
    TcalcHoListKind kind;
    TcTerm helper_head_tc;
    uint32_t cap_locals[TCALC_SPACE_MAX_HO_CAPS];
    uint32_t cap_len;
} TcalcHoListSpec;

typedef struct {
    uint64_t w[TCALC_SPACE_MAX_LOCALS / 64u];
} TcalcLocalMask;

static inline void tcalc_lmask_set(TcalcLocalMask *m, uint32_t idx) {
    if (idx < TCALC_SPACE_MAX_LOCALS)
        m->w[idx >> 6] |= (UINT64_C(1) << (idx & 63u));
}

static inline bool tcalc_lmask_test(const TcalcLocalMask *m, uint32_t idx) {
    return idx < TCALC_SPACE_MAX_LOCALS &&
           ((m->w[idx >> 6] & (UINT64_C(1) << (idx & 63u))) != 0u);
}

static inline bool tcalc_lmask_subset(const TcalcLocalMask *a,
                                      const TcalcLocalMask *b) {
    for (size_t i = 0u; i < TCALC_SPACE_MAX_LOCALS / 64u; i++) {
        if ((a->w[i] & ~b->w[i]) != 0u) return false;
    }
    return true;
}

static inline bool tcalc_lmask_intersects(const TcalcLocalMask *a,
                                          const TcalcLocalMask *b) {
    for (size_t i = 0u; i < TCALC_SPACE_MAX_LOCALS / 64u; i++) {
        if ((a->w[i] & b->w[i]) != 0u) return true;
    }
    return false;
}

static inline void tcalc_lmask_or(TcalcLocalMask *dst, const TcalcLocalMask *src) {
    for (size_t i = 0u; i < TCALC_SPACE_MAX_LOCALS / 64u; i++)
        dst->w[i] |= src->w[i];
}

static inline uint32_t tcalc_lmask_diff_count(const TcalcLocalMask *a,
                                              const TcalcLocalMask *b) {
    uint32_t c = 0u;
    for (size_t i = 0u; i < TCALC_SPACE_MAX_LOCALS / 64u; i++)
        c += (uint32_t)__builtin_popcountll(a->w[i] & ~b->w[i]);
    return c;
}

typedef struct {
    SymbolId rel_sym;
    TcTerm rel_tc;
    TcTerm pat_tc;
    uint32_t rel_idx;
    uint32_t arity;
    TcalcLocalMask all_vars;
    TcalcLocalMask arg_vars[TCALC_DATA_IDX_COLS];
} TcalcJoinLeg;

typedef struct {
    TcalcJoinLeg legs[TCALC_SPACE_MAX_JOIN_LEGS];
    uint32_t nlegs;
    TcalcLocalMask outer_bound;
    bool is_cyclic;
    uint32_t order[TCALC_SPACE_MAX_JOIN_LEGS];
    uint64_t plan_gen;
} TcalcJoinSpec;

typedef struct {
    uint64_t *keys;
    uint32_t *head_node;
    uint32_t *bucket_len;
    size_t cap;
    uint32_t *next_occ;
    uint32_t distinct_keys;
} TcalcKeyIndex;

typedef struct {
    SymbolId rel_sym;
    TcTerm rel_tc;
    SpaceHeadSetStamp head_stamp;
    uint64_t he_eq_rev;
    bool ready;
    TcTerm *atoms;
    uint32_t atom_len;
    uint32_t atom_cap;
    uint32_t min_arity;
    uint32_t max_arity;
    uint8_t needed_idx_mask; /* bits 0..3: arg_idx[0..3], bit 4: pair12_idx */
    TcalcKeyIndex arg_idx[TCALC_DATA_IDX_COLS];
    bool col_has_float[TCALC_DATA_IDX_COLS];
    TcalcKeyIndex pair12_idx;
    bool pair12_has_float;
} TcalcDataRelIndex;

typedef struct {
    TcalcExecOpKind kind;
    TcalcPrimOp prim;
    TcTerm primary;
    TcTerm secondary;
    TcTerm target;
    uint32_t jump_pc;
} TcalcExecOp;

typedef struct {
    TcTerm head_tc;
    TcTerm lhs_tc;
    uint32_t rule_ordinal;
    uint32_t nlocals;
    TcalcExecOp *ops;
    uint32_t op_len;
} TcalcExecClause;

typedef struct {
    char *name;
    TcTerm read[2];
    uint32_t reads;
    TcTerm make[2];
    uint32_t makes;
    uint32_t locals;
} TcalcSpaceCompiledRule;

typedef struct {
    Space *space;
    uint64_t space_instance_id;
    SymbolId entry_head;
    CettaLanguageId language_id;
    bool presence_only;
    bool compile_declined;
    bool has_recursion;
    bool has_branching_recursion;
    bool has_prims;
    bool terms_need_reset;
    SpaceTheoryToken declined_token;
    char declined_reason[128];
    SymbolId *rule_dep_heads;
    uint32_t rule_dep_len;
    SpaceHeadSetStamp rule_stamp;
    SymbolId *data_dep_heads;
    uint32_t data_dep_len;
    TcalcDataRelIndex *data_rels;
    uint64_t data_refresh_count;
    TcTerms *terms;
    TcTerm demand_sym;
    TcTerm value_sym;
    TcTerm data_sym;
    TcTerm matched_sym;
    TcTerm dedge_sym;
    TcTerm deriv_sym;
    TcTerm bool_sym;
    TcTerm float_sym;
    TcTerm true_sym_tc;
    TcTerm false_sym_tc;
    TcTerm true_lower_tc;
    TcTerm false_lower_tc;
    TcalcSpaceCompiledRule *rules;
    size_t rule_len;
    size_t rule_cap;
    TcalcExecClause *clauses;
    size_t clause_len;
    size_t clause_cap;
    TcalcJoinSpec *joins;
    uint32_t join_len;
    uint32_t join_cap;
    TcalcHoListSpec *ho_lists;
    uint32_t ho_list_len;
    uint32_t ho_list_cap;
    TcTerm *head_syms;
    uint32_t *head_off;
    uint32_t *head_clause_idx;
    uint32_t head_count;
    size_t data_fact_len;
    uint64_t compile_count;
    uint64_t hit_count;
} TcalcSpaceCacheEntry;

typedef struct {
    pthread_mutex_t lock;
    TcalcSpaceCacheEntry *entries;
    size_t len;
    size_t cap;
} TcalcSpaceCache;

static inline uint64_t tcalc_mix64(uint64_t x);

static inline uint64_t tcalc_col_key(uint32_t arity, TcTerm val) {
    uint64_t k = ((uint64_t)arity << 32) | (uint64_t)val;
    return k ? k : UINT64_C(1);
}

static inline uint64_t tcalc_pair_key(uint32_t arity, TcTerm a, TcTerm b) {
    uint64_t k = tcalc_mix64(((uint64_t)a << 32) | (uint64_t)b) ^
                 tcalc_mix64((uint64_t)arity + UINT64_C(0x9e3779b97f4a7c15));
    return k ? k : UINT64_C(1);
}

static void tcalc_key_idx_free(TcalcKeyIndex *idx) {
    if (!idx) return;
    free(idx->keys);
    free(idx->head_node);
    free(idx->bucket_len);
    free(idx->next_occ);
    memset(idx, 0, sizeof(*idx));
}

static void tcalc_data_rel_free_payload(TcalcDataRelIndex *rel) {
    if (!rel) return;
    free(rel->atoms);
    rel->atoms = NULL;
    rel->atom_len = rel->atom_cap = 0u;
    rel->min_arity = UINT32_MAX;
    rel->max_arity = 0u;
    for (uint32_t p = 0u; p < TCALC_DATA_IDX_COLS; p++) {
        tcalc_key_idx_free(&rel->arg_idx[p]);
        rel->col_has_float[p] = false;
    }
    tcalc_key_idx_free(&rel->pair12_idx);
    rel->pair12_has_float = false;
    rel->ready = false;
}

static void tcalc_space_entry_release_compiled(TcalcSpaceCacheEntry *entry) {
    if (!entry) return;
    free(entry->rule_dep_heads);
    entry->rule_dep_heads = NULL;
    entry->rule_dep_len = 0u;
    entry->rule_stamp = (SpaceHeadSetStamp){0};
    if (entry->data_rels) {
        for (uint32_t r = 0u; r < entry->data_dep_len; r++)
            tcalc_data_rel_free_payload(&entry->data_rels[r]);
        free(entry->data_rels);
        entry->data_rels = NULL;
    }
    free(entry->data_dep_heads);
    entry->data_dep_heads = NULL;
    entry->data_dep_len = 0u;
    entry->data_refresh_count = 0u;
    for (size_t i = 0u; i < entry->rule_len; i++)
        free(entry->rules[i].name);
    free(entry->rules);
    entry->rules = NULL;
    entry->rule_len = entry->rule_cap = 0u;
    for (size_t i = 0u; i < entry->clause_len; i++)
        free(entry->clauses[i].ops);
    free(entry->clauses);
    entry->clauses = NULL;
    entry->clause_len = entry->clause_cap = 0u;
    free(entry->joins);
    entry->joins = NULL;
    entry->join_len = entry->join_cap = 0u;
    free(entry->ho_lists);
    entry->ho_lists = NULL;
    entry->ho_list_len = entry->ho_list_cap = 0u;
    free(entry->head_syms);
    free(entry->head_off);
    free(entry->head_clause_idx);
    entry->head_syms = NULL;
    entry->head_off = NULL;
    entry->head_clause_idx = NULL;
    entry->head_count = 0u;
    entry->data_fact_len = 0u;
    entry->has_recursion = false;
    entry->has_branching_recursion = false;
    entry->has_prims = false;
    entry->terms_need_reset = false;
    tc_terms_free(entry->terms);
    entry->terms = NULL;
}

static void tcalc_space_cache_free(void *raw) {
    TcalcSpaceCache *cache = raw;
    if (!cache) return;
    for (size_t i = 0u; i < cache->len; i++)
        tcalc_space_entry_release_compiled(&cache->entries[i]);
    free(cache->entries);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

static TcalcSpaceCache *tcalc_space_cache_get(CettaLibraryContext *ctx) {
    if (!ctx) return NULL;
    if (!ctx->tcalc_space_cache) {
        TcalcSpaceCache *cache = calloc(1u, sizeof(*cache));
        if (!cache) return NULL;
        pthread_mutex_init(&cache->lock, NULL);
        ctx->tcalc_space_cache = cache;
        ctx->tcalc_space_cache_free = tcalc_space_cache_free;
    }
    return (TcalcSpaceCache *)ctx->tcalc_space_cache;
}

static int tcalc_sym_cmp(const void *a, const void *b) {
    SymbolId x = *(const SymbolId *)a;
    SymbolId y = *(const SymbolId *)b;
    return (x > y) - (x < y);
}

static bool tcalc_sym_set_add(SymbolId **items, uint32_t *len, uint32_t *cap,
                              SymbolId sym) {
    if (sym == SYMBOL_ID_NONE) return true;
    for (uint32_t i = 0u; i < *len; i++) {
        if ((*items)[i] == sym) return true;
    }
    if (*len >= TCALC_SPACE_MAX_HEADS) return false;
    if (*len == *cap) {
        uint32_t next = *cap ? *cap * 2u : 16u;
        SymbolId *grown = realloc(*items, (size_t)next * sizeof(*grown));
        if (!grown) return false;
        *items = grown;
        *cap = next;
    }
    (*items)[(*len)++] = sym;
    return true;
}

static bool tcalc_sym_set_contains(const SymbolId *items, uint32_t len,
                                   SymbolId sym) {
    for (uint32_t i = 0u; i < len; i++) {
        if (items[i] == sym) return true;
    }
    return false;
}

static bool tcalc_space_head_is_reserved_op(SymbolId sym,
                                            CettaLanguageId language_id) {
    if (sym == SYMBOL_ID_NONE) return true;
    if (is_grounded_op(sym)) return true;
    if (symbol_id_is_builtin(sym)) return true;
    const char *name = g_symbols ? symbol_bytes(g_symbols, sym) : NULL;
    if (name && name[0] == '_' && name[1] == '_' &&
        strncmp(name, "__cetta_", 8u) == 0)
        return true;
    if (language_id == CETTA_LANGUAGE_PETTA) {
        if (petta_semantics_registered_builtin(sym))
            return true;
        bool native_lib = false;
        if (petta_semantics_form_and_native_library(sym, &native_lib) !=
                PETTA_FORM_NONE || native_lib)
            return true;
    }
    return false;
}

static TcTerm tcalc_intern_symbol(TcTerms *terms, SymbolId sym) {
    const char *bytes = symbol_bytes(g_symbols, sym);
    size_t len = symbol_len(g_symbols, sym);
    return bytes ? tc_symbol(terms, bytes, len) : 0u;
}

static TcTerm tcalc_wrap1(TcTerms *terms, TcTerm head, TcTerm arg) {
    TcTerm pair[2] = {head, arg};
    return tc_expression(terms, pair, 2u);
}

static TcTerm tcalc_wrap2(TcTerms *terms, TcTerm head, TcTerm a, TcTerm b) {
    TcTerm triple[3] = {head, a, b};
    return tc_expression(terms, triple, 3u);
}

static TcTerm tcalc_wrap4(TcTerms *terms, TcTerm head, TcTerm a, TcTerm b,
                          TcTerm c, TcTerm d) {
    TcTerm quint[5] = {head, a, b, c, d};
    return tc_expression(terms, quint, 5u);
}

static TcTerm tcalc_encode_bool(TcTerms *terms, TcTerm bool_sym, bool bval) {
    TcTerm flag = tc_integer(terms, bval ? 1 : 0);
    return flag && bool_sym ? tcalc_wrap1(terms, bool_sym, flag) : 0u;
}

static inline bool tcalc_term_as_float(const TcTerms *terms, TcTerm float_sym,
                                       TcTerm t, double *out) {
    if (float_sym && tc_kind(terms, t) == TC_EXPRESSION && tc_length(terms, t) == 2u &&
        tc_item(terms, t, 0u) == float_sym &&
        tc_kind(terms, tc_item(terms, t, 1u)) == TC_INTEGER) {
        int64_t bits = tc_value(terms, tc_item(terms, t, 1u));
        double d = 0.0;
        memcpy(&d, &bits, sizeof(d));
        *out = d;
        return true;
    }
    return false;
}

static inline TcTerm tcalc_encode_float(TcTerms *terms, TcTerm float_sym,
                                        double d) {
    if (!isfinite(d) || !float_sym) return 0u;
    if (d == 0.0) d = 0.0;
    int64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    TcTerm b = tc_integer(terms, bits);
    return b ? tcalc_wrap1(terms, float_sym, b) : 0u;
}

static TcalcPrimOp tcalc_prim_op_of(SymbolId sym, uint32_t arity,
                                    CettaLanguageId language_id) {
    if (arity == 1u) {
        if (sym == g_builtin_syms.op_not) return TCPRIM_NOT;
        if (sym == g_builtin_syms.car_atom) return TCPRIM_CAR;
        if (sym == g_builtin_syms.cdr_atom) return TCPRIM_CDR;
        if (sym == g_builtin_syms.decons_atom) return TCPRIM_DECONS;
        if (sym == g_builtin_syms.size_atom) return TCPRIM_SIZE;
        return TCPRIM_NONE;
    }
    if (arity != 2u) return TCPRIM_NONE;
    if (sym == g_builtin_syms.op_plus) return TCPRIM_ADD;
    if (sym == g_builtin_syms.op_minus) return TCPRIM_SUB;
    if (sym == g_builtin_syms.op_mul) return TCPRIM_MUL;
    if (sym == g_builtin_syms.op_div) return TCPRIM_DIV;
    if (sym == g_builtin_syms.op_mod) return TCPRIM_MOD;
    if (sym == g_builtin_syms.op_eq) return TCPRIM_EQ;
    if (sym == g_builtin_syms.op_neq && language_id == CETTA_LANGUAGE_PETTA)
        return TCPRIM_NEQ;
    if (sym == g_builtin_syms.op_lt) return TCPRIM_LT;
    if (sym == g_builtin_syms.op_le) return TCPRIM_LE;
    if (sym == g_builtin_syms.op_gt) return TCPRIM_GT;
    if (sym == g_builtin_syms.op_ge) return TCPRIM_GE;
    if (sym == g_builtin_syms.op_and) return TCPRIM_AND;
    if (sym == g_builtin_syms.op_or) return TCPRIM_OR;
    if (sym == g_builtin_syms.cons_atom) return TCPRIM_CONS;
    if (sym == g_builtin_syms.index_atom) return TCPRIM_INDEX;
    return TCPRIM_NONE;
}

static const char *tcalc_prim_op_name(TcalcPrimOp op) {
    switch (op) {
    case TCPRIM_ADD: return "__prim_add";
    case TCPRIM_SUB: return "__prim_sub";
    case TCPRIM_MUL: return "__prim_mul";
    case TCPRIM_DIV: return "__prim_div";
    case TCPRIM_MOD: return "__prim_mod";
    case TCPRIM_EQ: return "__prim_eq";
    case TCPRIM_NEQ: return "__prim_neq";
    case TCPRIM_LT: return "__prim_lt";
    case TCPRIM_LE: return "__prim_le";
    case TCPRIM_GT: return "__prim_gt";
    case TCPRIM_GE: return "__prim_ge";
    case TCPRIM_AND: return "__prim_and";
    case TCPRIM_OR: return "__prim_or";
    case TCPRIM_NOT: return "__prim_not";
    case TCPRIM_CAR: return "__prim_car";
    case TCPRIM_CDR: return "__prim_cdr";
    case TCPRIM_CONS: return "__prim_cons";
    case TCPRIM_DECONS: return "__prim_decons";
    case TCPRIM_SIZE: return "__prim_size";
    case TCPRIM_INDEX: return "__prim_index";
    default: return "__prim";
    }
}

static bool tcalc_space_if_is_standard(Space *space, CettaLanguageId language_id) {
    if (!space) return false;
    if (language_id == CETTA_LANGUAGE_PETTA)
        return !space_equations_may_match_known_head(space, g_builtin_syms.if_text);
    SpaceTheoryView view = space_theory_view_open(space);
    Atom **eqs = NULL;
    size_t eq_len = 0u;
    bool via_alias = false;
    if (!space_theory_view_collect_head_rules(
            &view, g_builtin_syms.if_text,
            CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL,
            &eqs, &eq_len, &via_alias))
        return false;
    if (via_alias || eq_len == 0u) {
        free(eqs);
        return false;
    }
    bool saw_true = false, saw_false = false;
    size_t n_arity3 = 0u;
    for (size_t i = 0u; i < eq_len; i++) {
        Atom *eq = eqs[i];
        if (!eq || eq->kind != ATOM_EXPR || eq->expr.len != 3u ||
            !atom_is_symbol_id(eq->expr.elems[0], g_builtin_syms.equals))
            continue;
        Atom *lhs = eq->expr.elems[1];
        Atom *rhs = eq->expr.elems[2];
        if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 4u ||
            !atom_is_symbol_id(lhs->expr.elems[0], g_builtin_syms.if_text))
            continue;
        n_arity3++;
        if (lhs->expr.elems[1]->kind != ATOM_GROUNDED ||
            lhs->expr.elems[1]->ground.gkind != GV_BOOL ||
            lhs->expr.elems[2]->kind != ATOM_VAR ||
            lhs->expr.elems[3]->kind != ATOM_VAR ||
            var_id_eq(lhs->expr.elems[2]->var_id, lhs->expr.elems[3]->var_id) ||
            !rhs || rhs->kind != ATOM_VAR)
            continue;
        if (lhs->expr.elems[1]->ground.bval &&
            var_id_eq(rhs->var_id, lhs->expr.elems[2]->var_id))
            saw_true = true;
        else if (!lhs->expr.elems[1]->ground.bval &&
                 var_id_eq(rhs->var_id, lhs->expr.elems[3]->var_id))
            saw_false = true;
    }
    free(eqs);
    return n_arity3 == 2u && saw_true && saw_false;
}

/* Convert a ground CeTTa Atom to a TcTerm. */
static bool tcalc_ground_atom_to_tc(TcTerms *terms, TcTerm bool_sym,
                                    TcTerm float_sym, const Atom *atom,
                                    uint32_t depth, TcTerm *out,
                                    char *err, size_t err_cap) {
    if (!atom || depth > TCALC_SPACE_MAX_DEPTH) {
        snprintf(err, err_cap, "term exceeds depth limit");
        return false;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        snprintf(err, err_cap, "call or data atom must be ground");
        return false;
    case ATOM_SYMBOL:
        *out = tcalc_intern_symbol(terms, atom->sym_id);
        return *out != 0u;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            *out = tc_integer(terms, atom->ground.ival);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_FLOAT) {
            if (!isfinite(atom->ground.fval)) {
                snprintf(err, err_cap, "non-finite float outside compiled fragment");
                return false;
            }
            *out = tcalc_encode_float(terms, float_sym, atom->ground.fval);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_STRING) {
            if (memchr(atom->ground.sval, '\0', atom->ground.slen) != NULL) {
                snprintf(err, err_cap, "embedded NUL string outside compiled fragment");
                return false;
            }
            *out = tc_string(terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            *out = tcalc_encode_bool(terms, bool_sym, atom->ground.bval);
            return *out != 0u;
        }
        snprintf(err, err_cap, "unsupported grounded value in tcalc fragment");
        return false;
    case ATOM_EXPR: {
        uint32_t n = (uint32_t)atom->expr.len;
        TcTerm stack_items[16] = {0};
        TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
        if (!items) {
            snprintf(err, err_cap, "out of memory");
            return false;
        }
        bool ok = true;
        for (uint32_t i = 0u; ok && i < n; i++) {
            ok = tcalc_ground_atom_to_tc(terms, bool_sym, float_sym,
                                         atom->expr.elems[i], depth + 1u,
                                         &items[i], err, err_cap);
        }
        TcTerm res = ok ? tc_expression(terms, items, n) : 0u;
        if (items != stack_items) free(items);
        if (!ok || !res) {
            if (ok) snprintf(err, err_cap, "out of memory");
            return false;
        }
        *out = res;
        return true;
    }
    }
    return false;
}

static Atom *tcalc_tc_to_atom(const TcTerms *terms, TcTerm bool_sym,
                              TcTerm float_sym, CettaLanguageId language_id,
                              TcTerm t, Arena *a, uint32_t depth) {
    if (!t || depth > TCALC_SPACE_MAX_DEPTH) return NULL;
    switch (tc_kind(terms, t)) {
    case TC_SYMBOL: {
        size_t len = 0u;
        const char *bytes = tc_bytes(terms, t, &len);
        if (!bytes) return NULL;
        char stack_buf[64];
        char *buf = len < sizeof(stack_buf) ? stack_buf : malloc(len + 1u);
        if (!buf) return NULL;
        memcpy(buf, bytes, len);
        buf[len] = '\0';
        Atom *sym = atom_symbol(a, buf);
        if (buf != stack_buf) free(buf);
        return sym;
    }
    case TC_INTEGER:
        return atom_int(a, tc_value(terms, t));
    case TC_STRING: {
        size_t len = 0u;
        const char *bytes = tc_bytes(terms, t, &len);
        if (!bytes) return NULL;
        char stack_buf[64];
        char *buf = len < sizeof(stack_buf) ? stack_buf : malloc(len + 1u);
        if (!buf) return NULL;
        memcpy(buf, bytes, len);
        buf[len] = '\0';
        Atom *str = atom_string(a, buf);
        if (buf != stack_buf) free(buf);
        return str;
    }
    case TC_LOCAL: {
        char buf[32];
        snprintf(buf, sizeof buf, "v%u", tc_index(terms, t));
        return atom_var(a, buf);
    }
    case TC_UNKNOWN: {
        char buf[32];
        snprintf(buf, sizeof buf, "u%u", tc_index(terms, t));
        return atom_var(a, buf);
    }
    case TC_EXPRESSION: {
        uint32_t n = tc_length(terms, t);
        if (bool_sym && n == 2u && tc_item(terms, t, 0u) == bool_sym &&
            tc_kind(terms, tc_item(terms, t, 1u)) == TC_INTEGER) {
            bool bval = tc_value(terms, tc_item(terms, t, 1u)) != 0;
            return language_id == CETTA_LANGUAGE_PETTA
                ? petta_semantics_boolean_value(a, bval)
                : atom_bool(a, bval);
        }
        double fv = 0.0;
        if (tcalc_term_as_float(terms, float_sym, t, &fv))
            return atom_float(a, fv);
        Atom *stack_elems[16] = {0};
        Atom **elems = n <= 16u ? stack_elems : malloc((size_t)(n ? n : 1u) * sizeof(*elems));
        if (!elems) return NULL;
        bool ok = true;
        for (uint32_t i = 0u; ok && i < n; i++) {
            elems[i] = tcalc_tc_to_atom(terms, bool_sym, float_sym, language_id,
                                        tc_item(terms, t, i), a, depth + 1u);
            if (!elems[i]) ok = false;
        }
        Atom *res = ok ? atom_expr(a, elems, (CettaExprLen)n) : NULL;
        if (elems != stack_elems) free(elems);
        return res;
    }
    }
    return NULL;
}

typedef struct {
    VarId var_id;
    uint32_t local_idx;
} TcalcVarSlot;

typedef struct {
    Space *space;
    TcalcSpaceCacheEntry *entry;
    TcTerms *terms;
    CettaLanguageId language_id;
    TcTerm bool_sym;
    TcTerm float_sym;
    bool presence_only;
    bool has_prims;
    SymbolId *defined_heads;
    uint32_t defined_len;
    SymbolId **rule_deps;
    uint32_t *rule_dep_len;
    uint32_t *rule_dep_cap;
    SymbolId **data_deps;
    uint32_t *data_dep_len;
    uint32_t *data_dep_cap;
    uint32_t *ho_seq;
    TcalcVarSlot vars[TCALC_SPACE_MAX_LOCALS];
    uint32_t var_len;
    uint32_t next_local;
    TcalcExecOp *ops;
    uint32_t op_len;
    uint32_t op_cap;
    char *err;
    size_t err_cap;
} TcalcEqLower;

static bool tcalc_entry_push_clause(TcalcSpaceCacheEntry *entry,
                                    TcalcExecClause cl);

static bool tcalc_entry_push_join(TcalcSpaceCacheEntry *entry,
                                  const TcalcJoinSpec *spec,
                                  uint32_t *idx_out) {
    if (entry->join_len == entry->join_cap) {
        uint32_t next = entry->join_cap ? entry->join_cap * 2u : 8u;
        TcalcJoinSpec *grown = realloc(entry->joins, (size_t)next * sizeof(*grown));
        if (!grown) return false;
        entry->joins = grown;
        entry->join_cap = next;
    }
    uint32_t idx = entry->join_len++;
    entry->joins[idx] = *spec;
    *idx_out = idx;
    return true;
}

static bool tcalc_entry_push_ho_list(TcalcSpaceCacheEntry *entry,
                                     const TcalcHoListSpec *spec,
                                     uint32_t *idx_out) {
    if (entry->ho_list_len == entry->ho_list_cap) {
        uint32_t next = entry->ho_list_cap ? entry->ho_list_cap * 2u : 8u;
        TcalcHoListSpec *grown = realloc(entry->ho_lists, (size_t)next * sizeof(*grown));
        if (!grown) return false;
        entry->ho_lists = grown;
        entry->ho_list_cap = next;
    }
    uint32_t idx = entry->ho_list_len++;
    entry->ho_lists[idx] = *spec;
    *idx_out = idx;
    return true;
}

static void tcalc_collect_tc_locals(const TcTerms *terms, TcTerm t,
                                    TcalcLocalMask *mask, uint32_t depth) {
    if (!t || depth > 64u) return;
    TcKind k = tc_kind(terms, t);
    if (k == TC_LOCAL) {
        tcalc_lmask_set(mask, tc_index(terms, t));
        return;
    }
    if (k != TC_EXPRESSION) return;
    uint32_t n = tc_length(terms, t);
    for (uint32_t i = 0u; i < n; i++)
        tcalc_collect_tc_locals(terms, tc_item(terms, t, i), mask, depth + 1u);
}

static void tcalc_collect_free_outer_vars(const TcalcEqLower *L, const Atom *expr,
                                          VarId skip1, bool has_skip1,
                                          VarId skip2, bool has_skip2,
                                          uint32_t *caps, uint32_t *cap_len,
                                          VarId *cap_vids, uint32_t depth) {
    if (!expr || depth > 64u) return;
    if (expr->kind == ATOM_VAR) {
        if ((has_skip1 && var_id_eq(expr->var_id, skip1)) ||
            (has_skip2 && var_id_eq(expr->var_id, skip2)))
            return;
        for (uint32_t i = L->var_len; i > 0u; i--) {
            if (var_id_eq(L->vars[i - 1u].var_id, expr->var_id)) {
                uint32_t loc = L->vars[i - 1u].local_idx;
                for (uint32_t k = 0u; k < *cap_len; k++) {
                    if (caps[k] == loc) return;
                }
                if (*cap_len < TCALC_SPACE_MAX_HO_CAPS) {
                    caps[*cap_len] = loc;
                    cap_vids[*cap_len] = expr->var_id;
                    (*cap_len)++;
                }
                return;
            }
        }
        return;
    }
    if (expr->kind == ATOM_EXPR) {
        for (CettaExprLen i = 0u; i < expr->expr.len; i++) {
            tcalc_collect_free_outer_vars(L, expr->expr.elems[i],
                                          skip1, has_skip1, skip2, has_skip2,
                                          caps, cap_len, cap_vids, depth + 1u);
        }
    }
}

static bool tcalc_lower_push_op(TcalcEqLower *L, TcalcExecOp op, uint32_t *idx_out) {
    if (L->op_len == L->op_cap) {
        uint32_t next = L->op_cap ? L->op_cap * 2u : 16u;
        TcalcExecOp *grown = realloc(L->ops, (size_t)next * sizeof(*grown));
        if (!grown) {
            snprintf(L->err, L->err_cap, "out of memory");
            return false;
        }
        L->ops = grown;
        L->op_cap = next;
    }
    uint32_t idx = L->op_len++;
    L->ops[idx] = op;
    if (idx_out) *idx_out = idx;
    return true;
}

static bool tcalc_lower_bind_var(TcalcEqLower *L, VarId id, TcTerm *out) {
    for (uint32_t i = 0u; i < L->var_len; i++) {
        if (var_id_eq(L->vars[i].var_id, id)) {
            *out = tc_local(L->terms, L->vars[i].local_idx);
            return *out != 0u;
        }
    }
    if (L->var_len >= TCALC_SPACE_MAX_LOCALS || L->next_local >= TCALC_SPACE_MAX_LOCALS) {
        snprintf(L->err, L->err_cap, "too many variables in function definition");
        return false;
    }
    uint32_t loc = L->next_local++;
    L->vars[L->var_len++] = (TcalcVarSlot){
        .var_id = id,
        .local_idx = loc,
    };
    *out = tc_local(L->terms, loc);
    return *out != 0u;
}

static bool tcalc_lower_lookup_var(TcalcEqLower *L, VarId id, TcTerm *out) {
    for (uint32_t i = L->var_len; i > 0u; i--) {
        if (var_id_eq(L->vars[i - 1u].var_id, id)) {
            *out = tc_local(L->terms, L->vars[i - 1u].local_idx);
            return *out != 0u;
        }
    }
    snprintf(L->err, L->err_cap, "variable used before any pattern binds it");
    return false;
}

static bool tcalc_lower_fresh_local(TcalcEqLower *L, TcTerm *out) {
    if (L->next_local >= TCALC_SPACE_MAX_LOCALS) {
        snprintf(L->err, L->err_cap, "too many local variables in function definition");
        return false;
    }
    *out = tc_local(L->terms, L->next_local++);
    return *out != 0u;
}

/* Lower a pure first-order pattern (LHS argument or let/match pattern).
 * Every expression head inside a pattern must be an inert constructor. */
static bool tcalc_lower_pattern(TcalcEqLower *L, const Atom *atom,
                                bool is_lhs_root, uint32_t depth,
                                TcTerm *out) {
    if (!atom || depth > TCALC_SPACE_MAX_DEPTH) {
        snprintf(L->err, L->err_cap, "pattern exceeds depth limit");
        return false;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        return tcalc_lower_bind_var(L, atom->var_id, out);
    case ATOM_SYMBOL:
        *out = tcalc_intern_symbol(L->terms, atom->sym_id);
        return *out != 0u;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            *out = tc_integer(L->terms, atom->ground.ival);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_FLOAT) {
            if (!isfinite(atom->ground.fval)) {
                snprintf(L->err, L->err_cap, "non-finite float in pattern");
                return false;
            }
            *out = tcalc_encode_float(L->terms, L->float_sym, atom->ground.fval);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_STRING) {
            if (memchr(atom->ground.sval, '\0', atom->ground.slen) != NULL) {
                snprintf(L->err, L->err_cap, "embedded NUL string in pattern");
                return false;
            }
            *out = tc_string(L->terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            *out = tcalc_encode_bool(L->terms, L->bool_sym, atom->ground.bval);
            return *out != 0u;
        }
        snprintf(L->err, L->err_cap, "unsupported grounded value in pattern");
        return false;
    case ATOM_EXPR: {
        if (atom->expr.len == 0u) {
            *out = tc_expression(L->terms, NULL, 0u);
            return *out != 0u;
        }
        Atom *head = atom->expr.elems[0];
        if (!head) {
            snprintf(L->err, L->err_cap, "malformed pattern expression");
            return false;
        }
        if (is_lhs_root && head->kind != ATOM_SYMBOL) {
            snprintf(L->err, L->err_cap, "pattern expression must start with a head symbol");
            return false;
        }
        if (!is_lhs_root && head->kind == ATOM_SYMBOL) {
            if (tcalc_sym_set_contains(L->defined_heads, L->defined_len, head->sym_id) ||
                space_equations_may_match_known_head(L->space, head->sym_id)) {
                snprintf(L->err, L->err_cap,
                         "defined function head '%s' inside pattern is not supported",
                         symbol_bytes(g_symbols, head->sym_id));
                return false;
            }
            if (tcalc_space_head_is_reserved_op(head->sym_id, L->language_id)) {
                snprintf(L->err, L->err_cap,
                         "builtin head '%s' inside pattern is not supported",
                         symbol_bytes(g_symbols, head->sym_id));
                return false;
            }
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap,
                                   head->sym_id)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
        }
        uint32_t n = (uint32_t)atom->expr.len;
        TcTerm stack_items[16] = {0};
        TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
        if (!items) {
            snprintf(L->err, L->err_cap, "out of memory");
            return false;
        }
        bool ok = true;
        if (head->kind == ATOM_SYMBOL) {
            items[0] = tcalc_intern_symbol(L->terms, head->sym_id);
            ok = items[0] != 0u;
        } else {
            ok = tcalc_lower_pattern(L, head, false, depth + 1u, &items[0]);
        }
        for (uint32_t i = 1u; ok && i < n; i++) {
            ok = tcalc_lower_pattern(L, atom->expr.elems[i], false,
                                     depth + 1u, &items[i]);
        }
        TcTerm res = ok ? tc_expression(L->terms, items, n) : 0u;
        if (items != stack_items) free(items);
        if (!ok || !res) {
            if (ok) snprintf(L->err, L->err_cap, "out of memory");
            return false;
        }
        *out = res;
        return true;
    }
    }
    return false;
}

static bool tcalc_space_symbol_has_bare_rule(Space *space, SymbolId sym) {
    if (!space || sym == SYMBOL_ID_NONE ||
        !space_equations_may_match_known_head(space, sym))
        return false;
    SpaceTheoryView view = space_theory_view_open(space);
    Atom **eqs = NULL;
    size_t eq_len = 0u;
    bool via_alias = false;
    if (!space_theory_view_collect_head_rules(
            &view, sym, CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL,
            &eqs, &eq_len, &via_alias)) {
        return true;
    }
    bool found = via_alias;
    for (size_t i = 0u; !found && i < eq_len; i++) {
        Atom *eq = eqs[i];
        if (!eq || eq->kind != ATOM_EXPR || eq->expr.len != 3u ||
            !eq->expr.elems[1] || eq->expr.elems[1]->kind != ATOM_EXPR)
            found = true;
    }
    free(eqs);
    return found;
}

static bool tcalc_lower_rhs(TcalcEqLower *L, const Atom *atom,
                            uint32_t depth, TcTerm *out);

static bool tcalc_lower_let_binding(TcalcEqLower *L, const Atom *pat,
                                    const Atom *expr, uint32_t depth) {
    if (expr && expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        tcalc_sym_set_contains(L->defined_heads, L->defined_len,
                               expr->expr.elems[0]->sym_id)) {
        uint32_t n = (uint32_t)expr->expr.len;
        TcTerm stack_items[16] = {0};
        TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
        if (!items) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
        items[0] = tcalc_intern_symbol(L->terms, expr->expr.elems[0]->sym_id);
        bool ok = items[0] != 0u;
        for (uint32_t i = 1u; ok && i < n; i++)
            ok = tcalc_lower_rhs(L, expr->expr.elems[i], depth + 1u, &items[i]);
        TcTerm call_tc = ok ? tc_expression(L->terms, items, n) : 0u;
        if (items != stack_items) free(items);
        if (!ok || !call_tc) return false;
        TcTerm pat_tc = 0u;
        if (!tcalc_lower_pattern(L, pat, false, depth + 1u, &pat_tc))
            return false;
        return tcalc_lower_push_op(L, (TcalcExecOp){
            .kind = TCEXEC_CALL, .primary = call_tc, .target = pat_tc
        }, NULL);
    }
    TcTerm val_tc = 0u;
    if (!tcalc_lower_rhs(L, expr, depth + 1u, &val_tc))
        return false;
    TcTerm pat_tc = 0u;
    if (!tcalc_lower_pattern(L, pat, false, depth + 1u, &pat_tc))
        return false;
    return tcalc_lower_push_op(L, (TcalcExecOp){
        .kind = TCEXEC_MATCH, .primary = val_tc, .target = pat_tc
    }, NULL);
}

/* Flatten an RHS expression call-by-value (innermost first, left to right),
 * emitting compiled execution ops with strict lexical variable scoping. */
static bool tcalc_lower_rhs(TcalcEqLower *L, const Atom *atom,
                            uint32_t depth, TcTerm *out) {
    if (!atom || depth > TCALC_SPACE_MAX_DEPTH) {
        snprintf(L->err, L->err_cap, "expression exceeds depth limit");
        return false;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        return tcalc_lower_lookup_var(L, atom->var_id, out);
    case ATOM_SYMBOL:
        if (atom_is_empty((Atom *)atom) ||
            atom->sym_id == g_builtin_syms.empty ||
            atom->sym_id == g_builtin_syms.self ||
            tcalc_space_symbol_has_bare_rule(L->space, atom->sym_id)) {
            snprintf(L->err, L->err_cap,
                     "symbol '%s' has special evaluation semantics outside the tcalc fragment",
                     symbol_bytes(g_symbols, atom->sym_id));
            return false;
        }
        if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap,
                               atom->sym_id)) {
            snprintf(L->err, L->err_cap, "too many dependency head symbols");
            return false;
        }
        *out = tcalc_intern_symbol(L->terms, atom->sym_id);
        return *out != 0u;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            *out = tc_integer(L->terms, atom->ground.ival);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_FLOAT) {
            if (!isfinite(atom->ground.fval)) {
                snprintf(L->err, L->err_cap, "non-finite float in function body");
                return false;
            }
            *out = tcalc_encode_float(L->terms, L->float_sym, atom->ground.fval);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_STRING) {
            if (memchr(atom->ground.sval, '\0', atom->ground.slen) != NULL) {
                snprintf(L->err, L->err_cap, "embedded NUL string in function body");
                return false;
            }
            *out = tc_string(L->terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            *out = tcalc_encode_bool(L->terms, L->bool_sym, atom->ground.bval);
            return *out != 0u;
        }
        snprintf(L->err, L->err_cap, "unsupported grounded value in function body");
        return false;
    case ATOM_EXPR: {
        if (atom->expr.len == 0u) {
            *out = tc_expression(L->terms, NULL, 0u);
            return *out != 0u;
        }
        Atom *head = atom->expr.elems[0];
        if (!head) {
            snprintf(L->err, L->err_cap, "malformed expression in function body");
            return false;
        }
        if (head->kind == ATOM_VAR) {
            /* Higher-order variable application: ($f a1 ... an) */
            uint32_t n = (uint32_t)atom->expr.len;
            TcTerm stack_items[16] = {0};
            TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
            if (!items) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
            bool ok = tcalc_lower_lookup_var(L, head->var_id, &items[0]);
            for (uint32_t i = 1u; ok && i < n; i++)
                ok = tcalc_lower_rhs(L, atom->expr.elems[i], depth + 1u, &items[i]);
            TcTerm built = ok ? tc_expression(L->terms, items, n) : 0u;
            if (items != stack_items) free(items);
            if (!ok || !built) {
                if (ok) snprintf(L->err, L->err_cap, "out of memory");
                return false;
            }
            L->has_prims = true;
            TcTerm fresh = 0u;
            if (!tcalc_lower_fresh_local(L, &fresh)) return false;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_HO_CALL, .primary = built, .target = fresh
                }, NULL))
                return false;
            *out = fresh;
            return true;
        }
        if (head->kind == ATOM_GROUNDED) {
            /* Literal grounded-headed data tuple/list such as (1 2 3) */
            uint32_t n = (uint32_t)atom->expr.len;
            TcTerm stack_items[16] = {0};
            TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
            if (!items) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
            bool ok = true;
            for (uint32_t i = 0u; ok && i < n; i++)
                ok = tcalc_lower_rhs(L, atom->expr.elems[i], depth + 1u, &items[i]);
            TcTerm built = ok ? tc_expression(L->terms, items, n) : 0u;
            if (items != stack_items) free(items);
            if (!ok || !built) {
                if (ok) snprintf(L->err, L->err_cap, "out of memory");
                return false;
            }
            *out = built;
            return true;
        }
        if (head->kind != ATOM_SYMBOL) {
            /* Expression-headed tuple (e.g. ((Pair 1 2) (Pair 3 4))) when head is
             * an inert data expression (not callable). */
            if (head->kind == ATOM_EXPR &&
                (head->expr.len == 0u ||
                 (head->expr.elems[0]->kind == ATOM_SYMBOL &&
                  !tcalc_sym_set_contains(L->defined_heads, L->defined_len,
                                          head->expr.elems[0]->sym_id) &&
                  !space_equations_may_match_known_head(L->space,
                                                        head->expr.elems[0]->sym_id) &&
                  !tcalc_space_head_is_reserved_op(head->expr.elems[0]->sym_id,
                                                   L->language_id)))) {
                uint32_t n = (uint32_t)atom->expr.len;
                TcTerm stack_items[16] = {0};
                TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
                if (!items) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
                bool ok = true;
                for (uint32_t i = 0u; ok && i < n; i++)
                    ok = tcalc_lower_rhs(L, atom->expr.elems[i], depth + 1u, &items[i]);
                TcTerm built = ok ? tc_expression(L->terms, items, n) : 0u;
                if (items != stack_items) free(items);
                if (!ok || !built) {
                    if (ok) snprintf(L->err, L->err_cap, "out of memory");
                    return false;
                }
                *out = built;
                return true;
            }
            snprintf(L->err, L->err_cap,
                     "non-symbol application is outside the tcalc fragment");
            return false;
        }
        SymbolId sym = head->sym_id;

        /* (empty) -> terminates current derivation branch with zero answers. */
        if (sym == g_builtin_syms.empty_form && atom->expr.len == 1u) {
            L->has_prims = true;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){.kind = TCEXEC_EMPTY}, NULL))
                return false;
            return tcalc_lower_fresh_local(L, out);
        }

        /* (if COND THEN ELSE) -> lazy conditional with disjoint branch scopes. */
        if (sym == g_builtin_syms.if_text && atom->expr.len == 4u) {
            if (!tcalc_space_if_is_standard(L->space, L->language_id)) {
                snprintf(L->err, L->err_cap, "non-standard 'if' definition");
                return false;
            }
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            L->has_prims = true;
            TcTerm cond_tc = 0u;
            if (!tcalc_lower_rhs(L, atom->expr.elems[1], depth + 1u, &cond_tc))
                return false;
            uint32_t if_idx = 0u;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_IF, .primary = cond_tc, .jump_pc = 0u
                }, &if_idx))
                return false;
            TcTerm res_local = 0u;
            if (!tcalc_lower_fresh_local(L, &res_local))
                return false;
            uint32_t saved_vars = L->var_len;
            TcTerm then_tc = 0u;
            if (!tcalc_lower_rhs(L, atom->expr.elems[2], depth + 1u, &then_tc))
                return false;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_MOV, .primary = then_tc, .target = res_local
                }, NULL))
                return false;
            uint32_t jmp_idx = 0u;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_JUMP, .jump_pc = 0u
                }, &jmp_idx))
                return false;
            L->var_len = saved_vars;
            L->ops[if_idx].jump_pc = L->op_len;
            TcTerm else_tc = 0u;
            if (!tcalc_lower_rhs(L, atom->expr.elems[3], depth + 1u, &else_tc))
                return false;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_MOV, .primary = else_tc, .target = res_local
                }, NULL))
                return false;
            L->var_len = saved_vars;
            L->ops[jmp_idx].jump_pc = L->op_len;
            *out = res_local;
            return true;
        }

        /* (case SCRUT ((PAT1 BODY1) (PAT2 BODY2) ...)) */
        if (sym == g_builtin_syms.case_text && atom->expr.len == 3u &&
            !space_equations_may_match_known_head(L->space, sym)) {
            const Atom *branches = atom->expr.elems[2];
            if (!branches || branches->kind != ATOM_EXPR) {
                snprintf(L->err, L->err_cap, "malformed case branch list");
                return false;
            }
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            L->has_prims = true;
            TcTerm scrut_tc = 0u;
            if (!tcalc_lower_rhs(L, atom->expr.elems[1], depth + 1u, &scrut_tc))
                return false;
            TcTerm res_local = 0u;
            if (!tcalc_lower_fresh_local(L, &res_local))
                return false;
            CettaExprLen nb = branches->expr.len;
            uint32_t *end_jmps = nb ? calloc(nb, sizeof(*end_jmps)) : NULL;
            if (nb && !end_jmps) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
            bool ok = true;
            for (CettaExprLen bi = 0u; ok && bi < nb; bi++) {
                const Atom *br = branches->expr.elems[bi];
                if (!br || br->kind != ATOM_EXPR || br->expr.len != 2u) {
                    snprintf(L->err, L->err_cap, "malformed case branch");
                    ok = false;
                    break;
                }
                const Atom *bpat = br->expr.elems[0];
                const Atom *bbody = br->expr.elems[1];
                if (bpat && bpat->kind == ATOM_SYMBOL &&
                    (atom_is_empty((Atom *)bpat) ||
                     bpat->sym_id == g_builtin_syms.empty ||
                     atom_is_symbol((Atom *)bpat, "%void%"))) {
                    snprintf(L->err, L->err_cap, "empty-scrutinee case branch handled by evaluator");
                    ok = false;
                    break;
                }
                uint32_t saved_vars = L->var_len;
                TcTerm pat_tc = 0u;
                uint32_t try_idx = 0u;
                TcTerm body_tc = 0u;
                if (!tcalc_lower_pattern(L, bpat, false, depth + 1u, &pat_tc) ||
                    !tcalc_lower_push_op(L, (TcalcExecOp){
                        .kind = TCEXEC_TRY_MATCH, .primary = scrut_tc,
                        .target = pat_tc, .jump_pc = 0u
                    }, &try_idx) ||
                    !tcalc_lower_rhs(L, bbody, depth + 1u, &body_tc) ||
                    !tcalc_lower_push_op(L, (TcalcExecOp){
                        .kind = TCEXEC_MOV, .primary = body_tc, .target = res_local
                    }, NULL) ||
                    !tcalc_lower_push_op(L, (TcalcExecOp){
                        .kind = TCEXEC_JUMP, .jump_pc = 0u
                    }, &end_jmps[bi])) {
                    L->var_len = saved_vars;
                    ok = false;
                    break;
                }
                L->var_len = saved_vars;
                L->ops[try_idx].jump_pc = L->op_len;
            }
            if (ok) {
                ok = tcalc_lower_push_op(L, (TcalcExecOp){.kind = TCEXEC_EMPTY}, NULL);
                uint32_t end_pc = L->op_len;
                for (CettaExprLen bi = 0u; bi < nb; bi++)
                    L->ops[end_jmps[bi]].jump_pc = end_pc;
            }
            free(end_jmps);
            if (!ok) return false;
            *out = res_local;
            return true;
        }

        /* (superpose (e1 e2 ...)) or (superpose EXPR) */
        if (sym == g_builtin_syms.superpose && atom->expr.len == 2u &&
            !space_equations_may_match_known_head(L->space, sym)) {
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            L->has_prims = true;
            const Atom *arg = atom->expr.elems[1];
            if (arg && arg->kind == ATOM_EXPR) {
                CettaExprLen nk = arg->expr.len;
                if (nk == 0u) {
                    if (!tcalc_lower_push_op(L, (TcalcExecOp){.kind = TCEXEC_EMPTY}, NULL))
                        return false;
                    return tcalc_lower_fresh_local(L, out);
                }
                TcTerm res_local = 0u;
                if (!tcalc_lower_fresh_local(L, &res_local)) return false;
                uint32_t *end_jmps = calloc(nk, sizeof(*end_jmps));
                if (!end_jmps) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
                bool ok = true;
                for (CettaExprLen ki = 0u; ok && ki < nk; ki++) {
                    uint32_t fork_idx = UINT32_MAX;
                    if (ki + 1u < nk) {
                        ok = tcalc_lower_push_op(L, (TcalcExecOp){
                            .kind = TCEXEC_FORK, .jump_pc = 0u
                        }, &fork_idx);
                    }
                    uint32_t saved_vars = L->var_len;
                    TcTerm br_tc = 0u;
                    ok = ok && tcalc_lower_rhs(L, arg->expr.elems[ki], depth + 1u, &br_tc) &&
                         tcalc_lower_push_op(L, (TcalcExecOp){
                             .kind = TCEXEC_MOV, .primary = br_tc, .target = res_local
                         }, NULL) &&
                         tcalc_lower_push_op(L, (TcalcExecOp){
                             .kind = TCEXEC_JUMP, .jump_pc = 0u
                         }, &end_jmps[ki]);
                    L->var_len = saved_vars;
                    if (ok && fork_idx != UINT32_MAX)
                        L->ops[fork_idx].jump_pc = L->op_len;
                }
                if (ok) {
                    uint32_t end_pc = L->op_len;
                    for (CettaExprLen ki = 0u; ki < nk; ki++)
                        L->ops[end_jmps[ki]].jump_pc = end_pc;
                }
                free(end_jmps);
                if (!ok) return false;
                *out = res_local;
                return true;
            }
            TcTerm seq_tc = 0u;
            TcTerm dst = 0u;
            if (!tcalc_lower_rhs(L, arg, depth + 1u, &seq_tc) ||
                !tcalc_lower_fresh_local(L, &dst) ||
                !tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_SUPERPOSE, .primary = seq_tc, .target = dst
                }, NULL))
                return false;
            *out = dst;
            return true;
        }

        /* Higher-order list combinators: map-atom, filter-atom, foldl-atom */
        if (((sym == g_builtin_syms.map_atom && atom->expr.len == 4u) ||
             (sym == g_builtin_syms.filter_atom && atom->expr.len == 4u) ||
             (sym == g_builtin_syms.foldl_atom && atom->expr.len == 6u)) &&
            L->entry && L->ho_seq &&
            !space_equations_may_match_known_head(L->space, sym)) {
            bool is_fold = (sym == g_builtin_syms.foldl_atom);
            const Atom *list_expr = atom->expr.elems[1];
            const Atom *init_expr = is_fold ? atom->expr.elems[2] : NULL;
            const Atom *acc_var = is_fold ? atom->expr.elems[3] : NULL;
            const Atom *item_var = is_fold ? atom->expr.elems[4] : atom->expr.elems[2];
            const Atom *body_expr = is_fold ? atom->expr.elems[5] : atom->expr.elems[3];
            if (!item_var || item_var->kind != ATOM_VAR ||
                (is_fold && (!acc_var || acc_var->kind != ATOM_VAR ||
                             var_id_eq(acc_var->var_id, item_var->var_id)))) {
                snprintf(L->err, L->err_cap, "malformed higher-order list combinator binder");
                return false;
            }
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            L->has_prims = true;
            TcTerm list_tc = 0u, init_tc = 0u;
            if (!tcalc_lower_rhs(L, list_expr, depth + 1u, &list_tc))
                return false;
            if (is_fold && !tcalc_lower_rhs(L, init_expr, depth + 1u, &init_tc))
                return false;

            uint32_t cap_locals[TCALC_SPACE_MAX_HO_CAPS];
            VarId cap_vids[TCALC_SPACE_MAX_HO_CAPS];
            uint32_t cap_len = 0u;
            tcalc_collect_free_outer_vars(
                L, body_expr,
                item_var->var_id, true,
                is_fold ? acc_var->var_id : (VarId){0}, is_fold,
                cap_locals, &cap_len, cap_vids, 0u);

            char hname[48];
            snprintf(hname, sizeof hname, "__tcalc_ho_%u", (*L->ho_seq)++);
            TcTerm helper_head = tc_symbol(L->terms, hname, strlen(hname));
            if (!helper_head) { snprintf(L->err, L->err_cap, "out of memory"); return false; }

            TcalcEqLower subL = {
                .space = L->space,
                .entry = L->entry,
                .terms = L->terms,
                .language_id = L->language_id,
                .bool_sym = L->bool_sym,
                .float_sym = L->float_sym,
                .presence_only = L->presence_only,
                .has_prims = false,
                .defined_heads = L->defined_heads,
                .defined_len = L->defined_len,
                .rule_deps = L->rule_deps,
                .rule_dep_len = L->rule_dep_len,
                .rule_dep_cap = L->rule_dep_cap,
                .data_deps = L->data_deps,
                .data_dep_len = L->data_dep_len,
                .data_dep_cap = L->data_dep_cap,
                .ho_seq = L->ho_seq,
                .err = L->err,
                .err_cap = L->err_cap,
            };
            uint32_t nargs = cap_len + (is_fold ? 2u : 1u);
            TcTerm lhs_items[TCALC_SPACE_MAX_HO_CAPS + 3u];
            lhs_items[0] = helper_head;
            bool sub_ok = true;
            for (uint32_t ci = 0u; sub_ok && ci < cap_len; ci++)
                sub_ok = tcalc_lower_bind_var(&subL, cap_vids[ci], &lhs_items[1u + ci]);
            if (sub_ok && is_fold)
                sub_ok = tcalc_lower_bind_var(&subL, acc_var->var_id, &lhs_items[1u + cap_len]);
            if (sub_ok)
                sub_ok = tcalc_lower_bind_var(&subL, item_var->var_id,
                                              &lhs_items[1u + cap_len + (is_fold ? 1u : 0u)]);
            TcTerm sub_lhs = sub_ok ? tc_expression(L->terms, lhs_items, 1u + nargs) : 0u;
            TcTerm sub_rhs = 0u;
            sub_ok = sub_ok && sub_lhs &&
                     tcalc_lower_rhs(&subL, body_expr, depth + 1u, &sub_rhs) &&
                     tcalc_lower_push_op(&subL, (TcalcExecOp){
                         .kind = TCEXEC_RETURN, .primary = sub_rhs
                     }, NULL);
            if (!sub_ok) {
                free(subL.ops);
                return false;
            }
            TcalcExecClause hcl = {
                .head_tc = helper_head,
                .lhs_tc = sub_lhs,
                .rule_ordinal = UINT32_MAX,
                .nlocals = subL.next_local,
                .ops = subL.ops,
                .op_len = subL.op_len,
            };
            if (!tcalc_entry_push_clause(L->entry, hcl)) {
                free(subL.ops);
                snprintf(L->err, L->err_cap, "out of memory");
                return false;
            }
            TcalcHoListSpec spec = {
                .kind = is_fold ? TCHO_FOLDL
                    : (sym == g_builtin_syms.map_atom ? TCHO_MAP : TCHO_FILTER),
                .helper_head_tc = helper_head,
                .cap_len = cap_len,
            };
            for (uint32_t ci = 0u; ci < cap_len; ci++)
                spec.cap_locals[ci] = cap_locals[ci];
            uint32_t ho_idx = 0u;
            TcTerm dst = 0u;
            if (!tcalc_entry_push_ho_list(L->entry, &spec, &ho_idx) ||
                !tcalc_lower_fresh_local(L, &dst) ||
                !tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_HO_LIST,
                    .primary = list_tc,
                    .secondary = init_tc,
                    .target = dst,
                    .jump_pc = ho_idx,
                }, NULL)) {
                snprintf(L->err, L->err_cap, "out of memory");
                return false;
            }
            *out = dst;
            return true;
        }

        /* (let PAT EXPR BODY) */
        if (sym == g_builtin_syms.let && atom->expr.len == 4u) {
            uint32_t saved_vars = L->var_len;
            bool ok = tcalc_lower_let_binding(L, atom->expr.elems[1],
                                              atom->expr.elems[2], depth) &&
                      tcalc_lower_rhs(L, atom->expr.elems[3], depth + 1u, out);
            L->var_len = saved_vars;
            return ok;
        }

        /* (let* ((PAT1 EXPR1) (PAT2 EXPR2) ...) BODY) */
        if (sym == g_builtin_syms.let_star && atom->expr.len == 3u) {
            const Atom *bindings = atom->expr.elems[1];
            const Atom *body = atom->expr.elems[2];
            if (!bindings || bindings->kind != ATOM_EXPR) {
                snprintf(L->err, L->err_cap, "malformed let* bindings list");
                return false;
            }
            uint32_t saved_vars = L->var_len;
            bool ok = true;
            for (CettaExprLen bi = 0u; ok && bi < bindings->expr.len; bi++) {
                const Atom *pair = bindings->expr.elems[bi];
                if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u) {
                    snprintf(L->err, L->err_cap, "malformed let* binding pair");
                    ok = false;
                    break;
                }
                ok = tcalc_lower_let_binding(L, pair->expr.elems[0],
                                             pair->expr.elems[1], depth);
            }
            if (ok) ok = tcalc_lower_rhs(L, body, depth + 1u, out);
            L->var_len = saved_vars;
            return ok;
        }

        /* (match &self PAT BODY) — supports single patterns, conjunctive
         * `(, p1 p2 ... pk)`, and fuses pure nested matches into one planned join. */
        if (sym == g_builtin_syms.match && atom->expr.len == 4u) {
            Atom *raw_pats[TCALC_SPACE_MAX_JOIN_LEGS];
            uint32_t nlegs = 0u;
            const Atom *cur_match = atom;
            const Atom *final_body = NULL;
            while (cur_match && cur_match->kind == ATOM_EXPR &&
                   cur_match->expr.len == 4u &&
                   cur_match->expr.elems[0]->kind == ATOM_SYMBOL &&
                   cur_match->expr.elems[0]->sym_id == g_builtin_syms.match) {
                const Atom *sp_arg = cur_match->expr.elems[1];
                const Atom *pat = cur_match->expr.elems[2];
                const Atom *body = cur_match->expr.elems[3];
                bool self_space = (sp_arg && sp_arg->kind == ATOM_SYMBOL &&
                                   sp_arg->sym_id == g_builtin_syms.self) ||
                                  (sp_arg && sp_arg->kind == ATOM_GROUNDED &&
                                   sp_arg->ground.gkind == GV_SPACE &&
                                   (Space *)sp_arg->ground.ptr == L->space);
                if (!self_space) {
                    if (nlegs == 0u) {
                        snprintf(L->err, L->err_cap,
                                 "match against a space other than the compiled space is outside the tcalc fragment");
                        return false;
                    }
                    break;
                }
                if (!pat || pat->kind != ATOM_EXPR || pat->expr.len == 0u ||
                    pat->expr.elems[0]->kind != ATOM_SYMBOL) {
                    if (nlegs == 0u) {
                        snprintf(L->err, L->err_cap,
                                 "match pattern must be an expression starting with a head symbol");
                        return false;
                    }
                    break;
                }
                if (pat->expr.elems[0]->sym_id == g_builtin_syms.comma) {
                    if (pat->expr.len < 2u ||
                        nlegs + (uint32_t)(pat->expr.len - 1u) > TCALC_SPACE_MAX_JOIN_LEGS) {
                        if (nlegs == 0u) {
                            snprintf(L->err, L->err_cap,
                                     "conjunctive match leg count outside compiled bound");
                            return false;
                        }
                        break;
                    }
                    bool all_ok = true;
                    for (CettaExprLen li = 1u; li < pat->expr.len; li++) {
                        Atom *lp = pat->expr.elems[li];
                        if (!lp || lp->kind != ATOM_EXPR || lp->expr.len == 0u ||
                            lp->expr.elems[0]->kind != ATOM_SYMBOL) {
                            all_ok = false;
                            break;
                        }
                        SymbolId rel = lp->expr.elems[0]->sym_id;
                        if (tcalc_sym_set_contains(L->defined_heads, L->defined_len, rel) ||
                            space_equations_may_match_known_head(L->space, rel) ||
                            tcalc_space_head_is_reserved_op(rel, L->language_id)) {
                            all_ok = false;
                            break;
                        }
                    }
                    if (!all_ok) {
                        if (nlegs == 0u) {
                            snprintf(L->err, L->err_cap,
                                     "conjunctive match leg must be a pure data head symbol");
                            return false;
                        }
                        break;
                    }
                    for (CettaExprLen li = 1u; li < pat->expr.len; li++)
                        raw_pats[nlegs++] = pat->expr.elems[li];
                } else {
                    if (nlegs >= TCALC_SPACE_MAX_JOIN_LEGS) break;
                    SymbolId rel = pat->expr.elems[0]->sym_id;
                    if (tcalc_sym_set_contains(L->defined_heads, L->defined_len, rel) ||
                        space_equations_may_match_known_head(L->space, rel) ||
                        tcalc_space_head_is_reserved_op(rel, L->language_id)) {
                        if (nlegs == 0u) {
                            snprintf(L->err, L->err_cap,
                                     "match relation head '%s' must be a pure data head symbol",
                                     symbol_bytes(g_symbols, rel));
                            return false;
                        }
                        break;
                    }
                    raw_pats[nlegs++] = (Atom *)pat;
                }
                final_body = body;
                cur_match = body;
            }
            if (nlegs == 0u || !final_body) {
                snprintf(L->err, L->err_cap, "malformed match expression");
                return false;
            }
            uint32_t saved_vars = L->var_len;
            TcalcLocalMask outer_bound = {{0}};
            for (uint32_t vi = 0u; vi < saved_vars; vi++)
                tcalc_lmask_set(&outer_bound, L->vars[vi].local_idx);

            if (nlegs == 1u) {
                SymbolId rel = raw_pats[0]->expr.elems[0]->sym_id;
                if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, rel) ||
                    !tcalc_sym_set_add(L->data_deps, L->data_dep_len, L->data_dep_cap, rel)) {
                    snprintf(L->err, L->err_cap, "too many dependency head symbols");
                    return false;
                }
                TcTerm pat_tc = 0u;
                TcTerm rel_tc = tcalc_intern_symbol(L->terms, rel);
                if (!rel_tc || !tcalc_lower_pattern(L, raw_pats[0], true, depth + 1u, &pat_tc)) {
                    L->var_len = saved_vars;
                    return false;
                }
                TcTerm occ_local = 0u;
                if (!L->presence_only && !tcalc_lower_fresh_local(L, &occ_local)) {
                    L->var_len = saved_vars;
                    return false;
                }
                bool ok = tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_QUERY,
                    .primary = pat_tc,
                    .secondary = rel_tc,
                    .target = occ_local,
                    .jump_pc = UINT32_MAX,
                }, NULL) && tcalc_lower_rhs(L, final_body, depth + 1u, out);
                L->var_len = saved_vars;
                return ok;
            }

            if (!L->entry) {
                L->var_len = saved_vars;
                snprintf(L->err, L->err_cap, "missing cache entry for join");
                return false;
            }
            TcalcJoinSpec jspec;
            memset(&jspec, 0, sizeof(jspec));
            jspec.nlegs = nlegs;
            jspec.outer_bound = outer_bound;
            bool cyclic = false;
            if (cetta_relational_patterns_cyclic(raw_pats, nlegs, &cyclic))
                jspec.is_cyclic = cyclic;
            bool ok = true;
            for (uint32_t li = 0u; ok && li < nlegs; li++) {
                SymbolId rel = raw_pats[li]->expr.elems[0]->sym_id;
                if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, rel) ||
                    !tcalc_sym_set_add(L->data_deps, L->data_dep_len, L->data_dep_cap, rel)) {
                    snprintf(L->err, L->err_cap, "too many dependency head symbols");
                    ok = false;
                    break;
                }
                TcTerm rel_tc = tcalc_intern_symbol(L->terms, rel);
                TcTerm pat_tc = 0u;
                if (!rel_tc || !tcalc_lower_pattern(L, raw_pats[li], true, depth + 1u, &pat_tc)) {
                    ok = false;
                    break;
                }
                TcalcJoinLeg *leg = &jspec.legs[li];
                leg->rel_sym = rel;
                leg->rel_tc = rel_tc;
                leg->pat_tc = pat_tc;
                leg->rel_idx = UINT32_MAX;
                leg->arity = tc_length(L->terms, pat_tc);
                tcalc_collect_tc_locals(L->terms, pat_tc, &leg->all_vars, 0u);
                uint32_t ncols = leg->arity > 1u ? leg->arity - 1u : 0u;
                if (ncols > TCALC_DATA_IDX_COLS) ncols = TCALC_DATA_IDX_COLS;
                for (uint32_t p = 0u; p < ncols; p++) {
                    tcalc_collect_tc_locals(L->terms, tc_item(L->terms, pat_tc, p + 1u),
                                            &leg->arg_vars[p], 0u);
                }
                jspec.order[li] = li;
            }
            uint32_t join_idx = 0u;
            if (ok && !tcalc_entry_push_join(L->entry, &jspec, &join_idx)) {
                snprintf(L->err, L->err_cap, "out of memory");
                ok = false;
            }
            if (ok) {
                ok = tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_JOIN,
                    .primary = jspec.legs[0].pat_tc,
                    .secondary = jspec.legs[0].rel_tc,
                    .jump_pc = join_idx,
                }, NULL) && tcalc_lower_rhs(L, final_body, depth + 1u, out);
            }
            L->var_len = saved_vars;
            return ok;
        }

        /* Arithmetic, comparison, and boolean builtins */
        TcalcPrimOp pop = tcalc_prim_op_of(sym, (uint32_t)(atom->expr.len - 1u),
                                           L->language_id);
        if (pop != TCPRIM_NONE &&
            !tcalc_sym_set_contains(L->defined_heads, L->defined_len, sym) &&
            !space_equations_may_match_known_head(L->space, sym)) {
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            L->has_prims = true;
            TcTerm a0 = 0u, a1 = 0u;
            if (!tcalc_lower_rhs(L, atom->expr.elems[1], depth + 1u, &a0))
                return false;
            if (atom->expr.len == 3u &&
                !tcalc_lower_rhs(L, atom->expr.elems[2], depth + 1u, &a1))
                return false;
            TcTerm dst = 0u;
            if (!tcalc_lower_fresh_local(L, &dst))
                return false;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_PRIM,
                    .prim = pop,
                    .primary = a0,
                    .secondary = a1,
                    .target = dst,
                }, NULL))
                return false;
            *out = dst;
            return true;
        }

        bool is_defined = tcalc_sym_set_contains(L->defined_heads, L->defined_len, sym);
        if (tcalc_space_head_is_reserved_op(sym, L->language_id)) {
            snprintf(L->err, L->err_cap,
                     "builtin or grounded operation '%s' is outside the compiled tcalc fragment",
                     symbol_bytes(g_symbols, sym));
            return false;
        }
        if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, sym)) {
            snprintf(L->err, L->err_cap, "too many dependency head symbols");
            return false;
        }
        uint32_t n = (uint32_t)atom->expr.len;
        TcTerm stack_items[16] = {0};
        TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
        if (!items) { snprintf(L->err, L->err_cap, "out of memory"); return false; }
        items[0] = tcalc_intern_symbol(L->terms, sym);
        bool ok = items[0] != 0u;
        for (uint32_t i = 1u; ok && i < n; i++) {
            ok = tcalc_lower_rhs(L, atom->expr.elems[i], depth + 1u, &items[i]);
        }
        TcTerm built = ok ? tc_expression(L->terms, items, n) : 0u;
        if (items != stack_items) free(items);
        if (!ok || !built) {
            if (ok) snprintf(L->err, L->err_cap, "out of memory");
            return false;
        }
        if (is_defined) {
            TcTerm fresh = 0u;
            if (!tcalc_lower_fresh_local(L, &fresh))
                return false;
            if (!tcalc_lower_push_op(L, (TcalcExecOp){
                    .kind = TCEXEC_CALL, .primary = built, .target = fresh
                }, NULL))
                return false;
            *out = fresh;
            return true;
        }
        *out = built;
        return true;
    }
    }
    return false;
}

static bool tcalc_entry_push_rule(TcalcSpaceCacheEntry *entry, const char *name,
                                  const TcTerm *reads, uint32_t nreads,
                                  const TcTerm *makes, uint32_t nmakes,
                                  uint32_t locals) {
    if (nreads > 2u || nmakes == 0u || nmakes > 2u) return false;
    if (entry->rule_len == entry->rule_cap) {
        size_t next = entry->rule_cap ? entry->rule_cap * 2u : 16u;
        TcalcSpaceCompiledRule *grown = realloc(entry->rules, next * sizeof(*grown));
        if (!grown) return false;
        entry->rules = grown;
        entry->rule_cap = next;
    }
    char *copy = strdup(name);
    if (!copy) return false;
    TcalcSpaceCompiledRule r = {
        .name = copy,
        .reads = nreads,
        .makes = nmakes,
        .locals = locals,
    };
    for (uint32_t i = 0u; i < nreads; i++)
        r.read[i] = reads[i];
    for (uint32_t i = 0u; i < nmakes; i++)
        r.make[i] = makes[i];
    entry->rules[entry->rule_len++] = r;
    return true;
}

/* Discover all head symbols with function definitions reachable from ENTRY_HEAD. */
static bool tcalc_collect_reachable_heads(
        Space *space, const SpaceTheoryView *view, SymbolId entry_head,
        CettaLanguageId language_id,
        SymbolId **defined_out, uint32_t *defined_len_out,
        char *err, size_t err_cap) {
    SymbolId *queue = NULL;
    uint32_t qlen = 0u, qcap = 0u;
    if (!tcalc_sym_set_add(&queue, &qlen, &qcap, entry_head)) {
        snprintf(err, err_cap, "out of memory");
        return false;
    }
    for (uint32_t qi = 0u; qi < qlen; qi++) {
        SymbolId h = queue[qi];
        Atom **eqs = NULL;
        size_t eq_len = 0u;
        bool via_alias = false;
        if (!space_theory_view_collect_head_rules(
                view, h, CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL,
                &eqs, &eq_len, &via_alias)) {
            free(queue);
            snprintf(err, err_cap, "failed to inspect function definitions for head '%s'",
                     symbol_bytes(g_symbols, h));
            return false;
        }
        if (via_alias) {
            free(eqs);
            free(queue);
            snprintf(err, err_cap, "space has active =e equations");
            return false;
        }
        if (qi == 0u && eq_len == 0u) {
            free(eqs);
            free(queue);
            snprintf(err, err_cap, "head symbol '%s' has no function definitions",
                     symbol_bytes(g_symbols, h));
            return false;
        }
        if (tcalc_space_head_is_reserved_op(h, language_id)) {
            free(eqs);
            free(queue);
            snprintf(err, err_cap,
                     "builtin or grounded operation '%s' is handled by the evaluator",
                     symbol_bytes(g_symbols, h));
            return false;
        }
        if (space_theory_view_head_declares_type(view, h)) {
            free(eqs);
            free(queue);
            snprintf(err, err_cap,
                     "head symbol '%s' has a type assignment handled by the evaluator",
                     symbol_bytes(g_symbols, h));
            return false;
        }
        /* Walk RHS syntax of every definition of h to find called defined heads. */
        for (size_t e = 0u; e < eq_len; e++) {
            Atom *eq = eqs[e];
            if (!eq || eq->kind != ATOM_EXPR || eq->expr.len != 3u ||
                !atom_is_symbol_id(eq->expr.elems[0], g_builtin_syms.equals)) {
                free(eqs);
                free(queue);
                snprintf(err, err_cap,
                         "only ordinary function definitions (= lhs rhs) are supported");
                return false;
            }
            if (!eq->expr.elems[1] || eq->expr.elems[1]->kind != ATOM_EXPR ||
                eq->expr.elems[1]->expr.len == 0u ||
                eq->expr.elems[1]->expr.elems[0]->kind != ATOM_SYMBOL ||
                eq->expr.elems[1]->expr.elems[0]->sym_id != h) {
                free(eqs);
                free(queue);
                snprintf(err, err_cap,
                         "function definition left side must be a call expression for '%s'",
                         symbol_bytes(g_symbols, h));
                return false;
            }
            const Atom *stack[256];
            size_t sp = 0u;
            stack[sp++] = eq->expr.elems[2];
            while (sp > 0u) {
                const Atom *cur = stack[--sp];
                if (!cur || cur->kind != ATOM_EXPR || cur->expr.len == 0u)
                    continue;
                const Atom *ch = cur->expr.elems[0];
                if (ch && ch->kind == ATOM_SYMBOL) {
                    SymbolId cs = ch->sym_id;
                    if (cs == g_builtin_syms.empty_form && cur->expr.len == 1u)
                        continue;
                    if (cs == g_builtin_syms.if_text && cur->expr.len == 4u) {
                        if (sp + 3u <= 256u) {
                            stack[sp++] = cur->expr.elems[1];
                            stack[sp++] = cur->expr.elems[2];
                            stack[sp++] = cur->expr.elems[3];
                        }
                        continue;
                    }
                    if (cs == g_builtin_syms.case_text && cur->expr.len == 3u &&
                        !space_equations_may_match_known_head(space, cs)) {
                        if (sp < 256u) stack[sp++] = cur->expr.elems[1];
                        const Atom *bl = cur->expr.elems[2];
                        if (bl && bl->kind == ATOM_EXPR) {
                            for (CettaExprLen bi = 0u; bi < bl->expr.len; bi++) {
                                const Atom *br = bl->expr.elems[bi];
                                if (br && br->kind == ATOM_EXPR &&
                                    br->expr.len == 2u && sp < 256u)
                                    stack[sp++] = br->expr.elems[1];
                            }
                        }
                        continue;
                    }
                    if (cs == g_builtin_syms.superpose && cur->expr.len == 2u &&
                        !space_equations_may_match_known_head(space, cs)) {
                        const Atom *sa = cur->expr.elems[1];
                        if (sa && sa->kind == ATOM_EXPR) {
                            for (CettaExprLen si = 0u; si < sa->expr.len && sp < 256u; si++)
                                stack[sp++] = sa->expr.elems[si];
                        } else if (sp < 256u) {
                            stack[sp++] = sa;
                        }
                        continue;
                    }
                    if ((cs == g_builtin_syms.map_atom ||
                         cs == g_builtin_syms.filter_atom) && cur->expr.len == 4u &&
                        !space_equations_may_match_known_head(space, cs)) {
                        if (sp + 2u <= 256u) {
                            stack[sp++] = cur->expr.elems[1];
                            stack[sp++] = cur->expr.elems[3];
                        }
                        continue;
                    }
                    if (cs == g_builtin_syms.foldl_atom && cur->expr.len == 6u &&
                        !space_equations_may_match_known_head(space, cs)) {
                        if (sp + 3u <= 256u) {
                            stack[sp++] = cur->expr.elems[1];
                            stack[sp++] = cur->expr.elems[2];
                            stack[sp++] = cur->expr.elems[5];
                        }
                        continue;
                    }
                    if (cs == g_builtin_syms.match && cur->expr.len == 4u) {
                        if (sp < 256u) stack[sp++] = cur->expr.elems[3];
                        continue;
                    }
                    if (cs == g_builtin_syms.let && cur->expr.len == 4u) {
                        if (sp + 2u <= 256u) {
                            stack[sp++] = cur->expr.elems[2];
                            stack[sp++] = cur->expr.elems[3];
                        }
                        continue;
                    }
                    if (cs == g_builtin_syms.let_star && cur->expr.len == 3u) {
                        const Atom *bl = cur->expr.elems[1];
                        if (bl && bl->kind == ATOM_EXPR) {
                            for (CettaExprLen bi = 0u; bi < bl->expr.len; bi++) {
                                const Atom *pair = bl->expr.elems[bi];
                                if (pair && pair->kind == ATOM_EXPR &&
                                    pair->expr.len == 2u && sp < 256u)
                                    stack[sp++] = pair->expr.elems[1];
                            }
                        }
                        if (sp < 256u) stack[sp++] = cur->expr.elems[2];
                        continue;
                    }
                    if (tcalc_prim_op_of(cs, (uint32_t)(cur->expr.len - 1u),
                                         language_id) != TCPRIM_NONE &&
                        !space_equations_may_match_known_head(space, cs)) {
                        for (CettaExprLen i = 1u; i < cur->expr.len && sp < 256u; i++)
                            stack[sp++] = cur->expr.elems[i];
                        continue;
                    }
                    if (tcalc_space_head_is_reserved_op(cs, language_id)) {
                        free(eqs);
                        free(queue);
                        snprintf(err, err_cap,
                                 "builtin or grounded operation '%s' is outside the compiled tcalc fragment",
                                 symbol_bytes(g_symbols, cs));
                        return false;
                    }
                    if (space_equations_may_match_known_head(space, cs)) {
                        if (!tcalc_sym_set_add(&queue, &qlen, &qcap, cs)) {
                            free(eqs);
                            free(queue);
                            snprintf(err, err_cap, "too many reachable function heads");
                            return false;
                        }
                    }
                }
                for (CettaExprLen i = 1u; i < cur->expr.len && sp < 256u; i++) {
                    const Atom *arg = cur->expr.elems[i];
                    if (arg && arg->kind == ATOM_SYMBOL &&
                        !tcalc_space_head_is_reserved_op(arg->sym_id, language_id) &&
                        !space_theory_view_head_declares_type(view, arg->sym_id) &&
                        space_equations_may_match_known_head(space, arg->sym_id)) {
                        /* Enqueue function heads passed as higher-order values. */
                        (void)tcalc_sym_set_add(&queue, &qlen, &qcap, arg->sym_id);
                    }
                    stack[sp++] = arg;
                }
                if (ch && ch->kind == ATOM_EXPR && sp < 256u)
                    stack[sp++] = ch;
            }
        }
        free(eqs);
    }
    *defined_out = queue;
    *defined_len_out = qlen;
    return true;
}

static bool tcalc_atom_is_inert_value(Space *space, const Atom *atom,
                                      CettaLanguageId language_id,
                                      uint32_t depth);

static bool tcalc_key_idx_build(TcalcKeyIndex *idx, const uint64_t *occ_keys,
                                uint32_t count) {
    tcalc_key_idx_free(idx);
    if (count == 0u) return true;
    size_t cap = 16u;
    size_t target = (size_t)count * 2u + 8u;
    while (cap < target) cap <<= 1;
    uint64_t *keys = calloc(cap, sizeof(*keys));
    uint32_t *head_node = malloc(cap * sizeof(*head_node));
    uint32_t *bucket_len = calloc(cap, sizeof(*bucket_len));
    uint32_t *next_occ = malloc((size_t)count * sizeof(*next_occ));
    if (!keys || !head_node || !bucket_len || !next_occ) {
        free(keys); free(head_node); free(bucket_len); free(next_occ);
        return false;
    }
    for (size_t i = 0u; i < cap; i++) head_node[i] = UINT32_MAX;
    size_t mask = cap - 1u;
    uint32_t distinct = 0u;
    /* Insert backwards so chains preserve declaration order ascending. */
    for (uint32_t i = count; i > 0u; i--) {
        uint32_t occ = i - 1u;
        uint64_t k = occ_keys[occ];
        if (!k) { next_occ[occ] = UINT32_MAX; continue; }
        size_t slot = (size_t)tcalc_mix64(k) & mask;
        while (keys[slot] && keys[slot] != k)
            slot = (slot + 1u) & mask;
        if (!keys[slot]) {
            keys[slot] = k;
            distinct++;
        }
        next_occ[occ] = head_node[slot];
        head_node[slot] = occ;
        bucket_len[slot]++;
    }
    idx->keys = keys;
    idx->head_node = head_node;
    idx->bucket_len = bucket_len;
    idx->cap = cap;
    idx->next_occ = next_occ;
    idx->distinct_keys = distinct;
    return true;
}

static bool tcalc_data_rel_ingest_atom(TcalcSpaceCacheEntry *entry,
                                       Space *space, TcalcDataRelIndex *rel,
                                       Atom *atom, char *err, size_t err_cap) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.elems[0]->sym_id != rel->rel_sym)
        return true;
    if (space_theory_classify_atom(atom, NULL, NULL) != SPACE_THEORY_ATOM_DATA)
        return true;
    if (entry->language_id == CETTA_LANGUAGE_HE) {
        for (CettaExprLen ai = 1u; ai < atom->expr.len; ai++) {
            if (!tcalc_atom_is_inert_value(space, atom->expr.elems[ai],
                                           CETTA_LANGUAGE_HE, 0u)) {
                snprintf(err, err_cap, "data atom carries a callable expression");
                return false;
            }
        }
    }
    TcTerm atom_tc = 0u;
    if (!tcalc_ground_atom_to_tc(entry->terms, entry->bool_sym,
                                 entry->float_sym, atom, 0u,
                                 &atom_tc, err, err_cap))
        return false;
    if (rel->atom_len == rel->atom_cap) {
        uint32_t next = rel->atom_cap ? rel->atom_cap * 2u : 16u;
        TcTerm *ga = realloc(rel->atoms, (size_t)next * sizeof(*ga));
        if (!ga) { snprintf(err, err_cap, "out of memory"); return false; }
        rel->atoms = ga;
        rel->atom_cap = next;
    }
    rel->atoms[rel->atom_len++] = atom_tc;
    uint32_t ar = tc_length(entry->terms, atom_tc);
    if (ar < rel->min_arity) rel->min_arity = ar;
    if (ar > rel->max_arity) rel->max_arity = ar;
    return true;
}

static bool tcalc_data_rel_build_indexes(TcalcSpaceCacheEntry *entry,
                                         TcalcDataRelIndex *rel,
                                         char *err, size_t err_cap) {
    uint32_t n = rel->atom_len;
    if (n == 0u) return true;
    TcTerms *terms = entry->terms;
    uint64_t *kbuf = malloc((size_t)n * sizeof(*kbuf));
    if (!kbuf) { snprintf(err, err_cap, "out of memory"); return false; }

    /* Build only the argument-key indexes that compiled queries/joins can look up. */
    uint8_t mask = rel->needed_idx_mask;
    uint32_t max_cols = rel->max_arity > 1u ? (rel->max_arity - 1u) : 0u;
    if (max_cols > TCALC_DATA_IDX_COLS) max_cols = TCALC_DATA_IDX_COLS;
    for (uint32_t p = 0u; p < max_cols; p++) {
        rel->col_has_float[p] = false;
        if ((mask & (uint8_t)(1u << p)) == 0u) continue;
        bool any_key = false;
        bool has_float = false;
        for (uint32_t i = 0u; i < n; i++) {
            TcTerm at = rel->atoms[i];
            uint32_t ar = tc_length(terms, at);
            if (p + 1u < ar) {
                TcTerm arg = tc_item(terms, at, p + 1u);
                double fv = 0.0;
                if (tcalc_term_as_float(terms, entry->float_sym, arg, &fv))
                    has_float = true;
                kbuf[i] = tcalc_col_key(ar, arg);
                any_key = true;
            } else {
                kbuf[i] = 0u;
            }
        }
        rel->col_has_float[p] = has_float;
        if (any_key && !tcalc_key_idx_build(&rel->arg_idx[p], kbuf, n)) {
            free(kbuf);
            snprintf(err, err_cap, "out of memory");
            return false;
        }
    }

    /* Build 2-argument composite hash index on (arg 1, arg 2) only when needed. */
    rel->pair12_has_float = false;
    if (rel->max_arity >= 3u &&
        (mask & (uint8_t)(1u << TCALC_DATA_IDX_COLS)) != 0u) {
        bool any_pair = false;
        bool has_float = rel->col_has_float[0] || rel->col_has_float[1];
        bool scan_float = !has_float && ((mask & 3u) != 3u);
        for (uint32_t i = 0u; i < n; i++) {
            TcTerm at = rel->atoms[i];
            uint32_t ar = tc_length(terms, at);
            if (ar >= 3u) {
                TcTerm a1 = tc_item(terms, at, 1u);
                TcTerm a2 = tc_item(terms, at, 2u);
                if (scan_float && !has_float) {
                    double fv = 0.0;
                    if (tcalc_term_as_float(terms, entry->float_sym, a1, &fv) ||
                        tcalc_term_as_float(terms, entry->float_sym, a2, &fv))
                        has_float = true;
                }
                kbuf[i] = tcalc_pair_key(ar, a1, a2);
                any_pair = true;
            } else {
                kbuf[i] = 0u;
            }
        }
        rel->pair12_has_float = has_float;
        if (any_pair && !tcalc_key_idx_build(&rel->pair12_idx, kbuf, n)) {
            free(kbuf);
            snprintf(err, err_cap, "out of memory");
            return false;
        }
    }
    free(kbuf);
    return true;
}

/* Order conjunctive match legs using bound-variable connectivity, pure-filter
 * priority, and live indexed selectivity (estimated fanout from per-head
 * cardinalities & distinct key counts). */
static void tcalc_plan_join_order(const TcalcSpaceCacheEntry *entry,
                                  TcalcJoinSpec *join) {
    uint32_t n = join->nlegs;
    if (n <= 1u) {
        join->plan_gen = entry->data_refresh_count;
        return;
    }
    TcalcLocalMask bound = join->outer_bound;
    bool used[TCALC_SPACE_MAX_JOIN_LEGS] = {false};

    for (uint32_t step = 0u; step < n; step++) {
        uint32_t best = 0u;
        bool have_best = false;
        double best_cost = 0.0;
        bool best_filter = false;
        bool best_conn = false;
        uint32_t best_unbound = UINT32_MAX;

        for (uint32_t j = 0u; j < n; j++) {
            if (used[j]) continue;
            const TcalcJoinLeg *leg = &join->legs[j];
            const TcalcDataRelIndex *rel = leg->rel_idx < entry->data_dep_len
                ? &entry->data_rels[leg->rel_idx] : NULL;
            uint32_t card = rel ? rel->atom_len : 0u;
            bool is_filter = tcalc_lmask_subset(&leg->all_vars, &bound);
            bool connected = is_filter || tcalc_lmask_intersects(&leg->all_vars, &bound);
            uint32_t unbound_vars = tcalc_lmask_diff_count(&leg->all_vars, &bound);

            double est = (double)card;
            if (card == 0u) {
                est = 0.0;
            } else if (is_filter) {
                est = 0.25;
            } else if (rel) {
                uint32_t ncols = leg->arity > 1u ? leg->arity - 1u : 0u;
                if (ncols > TCALC_DATA_IDX_COLS) ncols = TCALC_DATA_IDX_COLS;
                bool arg_bound[TCALC_DATA_IDX_COLS] = {false};
                uint32_t nbound_args = 0u;
                for (uint32_t p = 0u; p < ncols; p++) {
                    if (tcalc_lmask_subset(&leg->arg_vars[p], &bound)) {
                        arg_bound[p] = true;
                        nbound_args++;
                    }
                }
                if (ncols >= 2u && arg_bound[0] && arg_bound[1] &&
                    rel->pair12_idx.distinct_keys > 0u) {
                    est = (double)card / (double)rel->pair12_idx.distinct_keys;
                } else if (nbound_args > 0u) {
                    double min_est = (double)card;
                    for (uint32_t p = 0u; p < ncols; p++) {
                        if (arg_bound[p] && rel->arg_idx[p].distinct_keys > 0u) {
                            double ce = (double)card /
                                        (double)rel->arg_idx[p].distinct_keys;
                            if (ce < min_est) min_est = ce;
                        }
                    }
                    est = min_est;
                } else {
                    /* Unbound leg: penalize Cartesian expansion when any connected leg exists */
                    est = (double)card * 1024.0 + 1.0;
                }
            }

            bool better = false;
            if (!have_best) {
                better = true;
            } else if (card == 0u && best_cost > 0.0) {
                better = true;
            } else if (best_cost == 0.0 && card > 0u) {
                better = false;
            } else if (is_filter != best_filter) {
                better = is_filter;
            } else if (connected != best_conn) {
                better = connected;
            } else if (est < best_cost * 0.999) {
                better = true;
            } else if (est <= best_cost * 1.001 && unbound_vars < best_unbound) {
                better = true;
            }
            if (better) {
                have_best = true;
                best = j;
                best_cost = est;
                best_filter = is_filter;
                best_conn = connected;
                best_unbound = unbound_vars;
            }
        }
        used[best] = true;
        join->order[step] = best;
        tcalc_lmask_or(&bound, &join->legs[best].all_vars);
    }
    join->plan_gen = entry->data_refresh_count;
}

static bool tcalc_space_refresh_data_facts(TcalcSpaceCacheEntry *entry,
                                           Space *space, char *err,
                                           size_t err_cap) {
    if (entry->data_dep_len == 0u) return true;
    uint64_t eq_rev = entry->language_id == CETTA_LANGUAGE_HE
        ? space_equation_revision(space) : 0u;

    uint32_t stale_count = 0u;
    bool stale_flags[TCALC_SPACE_MAX_HEADS];
    for (uint32_t r = 0u; r < entry->data_dep_len; r++) {
        TcalcDataRelIndex *rel = &entry->data_rels[r];
        bool cur = rel->ready && rel->head_stamp.active &&
            (entry->language_id != CETTA_LANGUAGE_HE || rel->he_eq_rev == eq_rev) &&
            space_theory_head_set_clock_current(
                space, &rel->rel_sym, 1u, &rel->head_stamp);
        stale_flags[r] = !cur;
        if (!cur) stale_count++;
    }
    if (stale_count == 0u) return true;

    /* Refresh ONLY stale data heads without recompiling rules. */
    bool use_head_snap = (space->match_backend.kind == SPACE_ENGINE_NATIVE &&
                          !space_has_dependencies(space) &&
                          !space->overlay_base);
    SpaceHeadSnapshot snap;
    bool have_snap = use_head_snap && space_head_snapshot_init(space, &snap);

    for (uint32_t r = 0u; r < entry->data_dep_len; r++) {
        if (!stale_flags[r]) continue;
        tcalc_data_rel_free_payload(&entry->data_rels[r]);
        entry->data_rels[r].min_arity = UINT32_MAX;
        entry->data_rels[r].max_arity = 0u;
    }

    bool ok = true;
    if (have_snap) {
        for (uint32_t r = 0u; ok && r < entry->data_dep_len; r++) {
            if (!stale_flags[r]) continue;
            TcalcDataRelIndex *rel = &entry->data_rels[r];
            SpaceOccurrenceCursor cur;
            space_occurrence_cursor_init_empty(&cur);
            if (!space_head_snapshot_select(&snap, &rel->rel_sym, 1u, &cur)) {
                ok = false;
                snprintf(err, err_cap, "failed to select head snapshot");
                break;
            }
            Atom *atom = NULL;
            while (space_occurrence_cursor_next(&cur, NULL, &atom) ==
                   SPACE_OCCURRENCE_CURSOR_ITEM) {
                if (!tcalc_data_rel_ingest_atom(entry, space, rel, atom,
                                                err, err_cap)) {
                    ok = false;
                    break;
                }
            }
            space_occurrence_cursor_release(&cur);
        }
        space_head_snapshot_release(&snap);
    } else {
        CettaCount total = space_view_length64(space);
        for (CettaIndex i = 0u; ok && i < total; i++) {
            Atom *atom = space_view_get_at64(space, i);
            if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
                atom->expr.elems[0]->kind != ATOM_SYMBOL)
                continue;
            SymbolId h = atom->expr.elems[0]->sym_id;
            for (uint32_t r = 0u; r < entry->data_dep_len; r++) {
                if (!stale_flags[r] || entry->data_rels[r].rel_sym != h)
                    continue;
                ok = tcalc_data_rel_ingest_atom(entry, space, &entry->data_rels[r],
                                                atom, err, err_cap);
                break;
            }
        }
    }

    for (uint32_t r = 0u; ok && r < entry->data_dep_len; r++) {
        if (!stale_flags[r]) continue;
        TcalcDataRelIndex *rel = &entry->data_rels[r];
        if (!tcalc_data_rel_build_indexes(entry, rel, err, err_cap)) {
            ok = false;
            break;
        }
        SpaceHeadSetStamp stamp = {0};
        if (!space_theory_head_set_clock_capture(
                space, &rel->rel_sym, 1u,
                SPACE_THEORY_ROLE_DATA | SPACE_THEORY_ROLE_ALIAS, &stamp)) {
            snprintf(err, err_cap, "failed to capture data head version counter");
            ok = false;
            break;
        }
        rel->head_stamp = stamp;
        rel->he_eq_rev = eq_rev;
        rel->ready = true;
    }

    if (!ok) {
        for (uint32_t r = 0u; r < entry->data_dep_len; r++) {
            if (stale_flags[r])
                tcalc_data_rel_free_payload(&entry->data_rels[r]);
        }
        return false;
    }

    size_t total_facts = 0u;
    for (uint32_t r = 0u; r < entry->data_dep_len; r++)
        total_facts += entry->data_rels[r].atom_len;
    entry->data_fact_len = total_facts;
    entry->data_refresh_count++;

    /* Re-plan join orders against the refreshed live cardinalities. */
    for (uint32_t ji = 0u; ji < entry->join_len; ji++) {
        if (entry->joins[ji].plan_gen != entry->data_refresh_count)
            tcalc_plan_join_order(entry, &entry->joins[ji]);
    }
    return true;
}

static bool tcalc_entry_push_clause(TcalcSpaceCacheEntry *entry,
                                    TcalcExecClause cl) {
    if (entry->clause_len == entry->clause_cap) {
        size_t next = entry->clause_cap ? entry->clause_cap * 2u : 16u;
        TcalcExecClause *grown = realloc(entry->clauses, next * sizeof(*grown));
        if (!grown) return false;
        entry->clauses = grown;
        entry->clause_cap = next;
    }
    entry->clauses[entry->clause_len++] = cl;
    return true;
}

static bool tcalc_space_compile_into(TcalcSpaceCacheEntry *entry,
                                     Space *space, SymbolId entry_head,
                                     CettaLanguageId language_id,
                                     bool presence_only,
                                     char *err, size_t err_cap) {
    if (!space) {
        snprintf(err, err_cap, "missing space");
        return false;
    }
    if (space->overlay_base) {
        snprintf(err, err_cap, "overlay spaces are outside the tcalc fragment");
        return false;
    }
    if (space_may_have_equational_aliases(space) &&
        space_has_active_equations_slow(space)) {
        snprintf(err, err_cap, "space has active =e equations");
        return false;
    }
    if (space_equations_may_match_unnamed_head(space)) {
        snprintf(err, err_cap, "space has wildcard function definitions");
        return false;
    }

    tcalc_space_entry_release_compiled(entry);
    entry->space = space;
    entry->space_instance_id = space_instance_id(space);
    entry->entry_head = entry_head;
    entry->language_id = language_id;
    entry->presence_only = presence_only;
    entry->compile_declined = false;
    entry->declined_reason[0] = '\0';

    SpaceTheoryView view = space_theory_view_open(space);
    SymbolId *defined = NULL;
    uint32_t defined_len = 0u;
    if (!tcalc_collect_reachable_heads(space, &view, entry_head, language_id,
                                       &defined, &defined_len, err, err_cap)) {
        return false;
    }

    TcTerms *terms = tc_terms_new();
    if (!terms) {
        free(defined);
        snprintf(err, err_cap, "out of memory");
        return false;
    }
    entry->terms = terms;
    entry->demand_sym = tc_symbol(terms, "demand", 6u);
    entry->value_sym = tc_symbol(terms, "value", 5u);
    entry->data_sym = tc_symbol(terms, "__space_data", 12u);
    entry->matched_sym = tc_symbol(terms, "__matched", 9u);
    entry->dedge_sym = tc_symbol(terms, "__dedge", 7u);
    entry->deriv_sym = tc_symbol(terms, "__deriv", 7u);
    entry->bool_sym = tc_symbol(terms, "__tc_bool", 9u);
    entry->float_sym = tc_symbol(terms, "__tc_float", 10u);
    entry->true_sym_tc = tc_symbol(terms, "True", 4u);
    entry->false_sym_tc = tc_symbol(terms, "False", 5u);
    entry->true_lower_tc = tc_symbol(terms, "true", 4u);
    entry->false_lower_tc = tc_symbol(terms, "false", 5u);
    TcTerm tag_sym = tc_symbol(terms, "__tag", 5u);
    TcTerm subs_sym = tc_symbol(terms, "__subs", 6u);
    TcTerm cond_sym = tc_symbol(terms, "__if", 4u);
    TcTerm bind_sym = tc_symbol(terms, "__match", 7u);

    SymbolId *rule_deps = NULL;
    uint32_t rule_dep_len = 0u, rule_dep_cap = 0u;
    SymbolId *data_deps = NULL;
    uint32_t data_dep_len = 0u, data_dep_cap = 0u;
    bool ok = entry->demand_sym && entry->value_sym && entry->data_sym &&
              entry->matched_sym && entry->dedge_sym && entry->deriv_sym &&
              entry->bool_sym && entry->float_sym && entry->true_sym_tc &&
              entry->false_sym_tc && entry->true_lower_tc &&
              entry->false_lower_tc && tag_sym && subs_sym && cond_sym && bind_sym;
    for (uint32_t i = 0u; ok && i < defined_len; i++) {
        ok = tcalc_sym_set_add(&rule_deps, &rule_dep_len, &rule_dep_cap, defined[i]);
    }

    uint32_t rule_ordinal = 0u;
    uint32_t ho_seq = 0u;
    for (uint32_t di = 0u; ok && di < defined_len; di++) {
        SymbolId h = defined[di];
        TcTerm h_tc = tcalc_intern_symbol(terms, h);
        Atom **eqs = NULL;
        size_t eq_len = 0u;
        bool via_alias = false;
        if (!h_tc || !space_theory_view_collect_head_rules(
                &view, h, CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL,
                &eqs, &eq_len, &via_alias) || via_alias) {
            free(eqs);
            snprintf(err, err_cap, "failed to inspect definitions for '%s'",
                     symbol_bytes(g_symbols, h));
            ok = false;
            break;
        }
        for (size_t ei = 0u; ok && ei < eq_len; ei++) {
            Atom *eq = eqs[ei];
            Atom *lhs = eq->expr.elems[1];
            Atom *rhs = eq->expr.elems[2];
            if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
                lhs->expr.elems[0]->kind != ATOM_SYMBOL ||
                lhs->expr.elems[0]->sym_id != h) {
                snprintf(err, err_cap, "malformed function definition left side");
                ok = false;
                break;
            }
            TcalcEqLower L = {
                .space = space,
                .entry = entry,
                .terms = terms,
                .language_id = language_id,
                .bool_sym = entry->bool_sym,
                .float_sym = entry->float_sym,
                .presence_only = presence_only,
                .has_prims = false,
                .defined_heads = defined,
                .defined_len = defined_len,
                .rule_deps = &rule_deps,
                .rule_dep_len = &rule_dep_len,
                .rule_dep_cap = &rule_dep_cap,
                .data_deps = &data_deps,
                .data_dep_len = &data_dep_len,
                .data_dep_cap = &data_dep_cap,
                .ho_seq = &ho_seq,
                .err = err,
                .err_cap = err_cap,
            };
            TcTerm lhs_tc = 0u, rhs_tc = 0u;
            ok = tcalc_lower_pattern(&L, lhs, true, 0u, &lhs_tc) &&
                 tcalc_lower_rhs(&L, rhs, 0u, &rhs_tc) &&
                 tcalc_lower_push_op(&L, (TcalcExecOp){
                     .kind = TCEXEC_RETURN, .primary = rhs_tc
                 }, NULL);
            if (ok) {
                if (L.has_prims) entry->has_prims = true;
                /* Emit inspectable tcalc rules for tcalc:space-program. */
                TcTerm demand_lhs = tcalc_wrap1(terms, entry->demand_sym, lhs_tc);
                TcTerm matched_lhs = tcalc_wrap1(terms, entry->matched_sym, lhs_tc);
                if (!demand_lhs || !matched_lhs) {
                    ok = false;
                } else {
                    char mname[64];
                    snprintf(mname, sizeof mname, "r%u.match", rule_ordinal);
                    TcTerm mread[1] = {demand_lhs};
                    TcTerm mmake[1] = {matched_lhs};
                    ok = tcalc_entry_push_rule(entry, mname, mread, 1u,
                                               mmake, 1u, L.next_local);
                    TcTerm prev = demand_lhs;
                    uint32_t step = 0u;
                    for (uint32_t oi = 0u; ok && oi < L.op_len; oi++) {
                        const TcalcExecOp *op = &L.ops[oi];
                        if (op->kind == TCEXEC_CALL) {
                            step++;
                            char rname[64], sname[64];
                            snprintf(rname, sizeof rname, "r%u/%u.ask", rule_ordinal, step);
                            snprintf(sname, sizeof sname, "r%u/%u", rule_ordinal, step);
                            TcTerm stage_sym = tc_symbol(terms, sname, strlen(sname));
                            TcTerm ask_makes[2];
                            ask_makes[0] = tcalc_wrap1(terms, entry->demand_sym, op->primary);
                            uint32_t ask_nmakes = 1u;
                            if (!presence_only) {
                                ask_makes[1] = tcalc_wrap2(terms, entry->dedge_sym,
                                                           lhs_tc, op->primary);
                                ask_nmakes = 2u;
                                ok = ok && ask_makes[1] != 0u;
                            }
                            TcTerm ask_read[1] = {prev};
                            ok = ok && stage_sym && ask_makes[0] &&
                                 tcalc_entry_push_rule(entry, rname, ask_read, 1u,
                                                       ask_makes, ask_nmakes,
                                                       L.next_local);
                            TcTerm val_pat = tcalc_wrap2(terms, entry->value_sym,
                                                         op->primary, op->target);
                            TcTerm next = tcalc_wrap2(terms, stage_sym, lhs_tc, op->target);
                            TcTerm got_reads[2] = {prev, val_pat};
                            TcTerm got_makes[1] = {next};
                            ok = ok && val_pat && next &&
                                 tcalc_entry_push_rule(entry, sname, got_reads, 2u,
                                                       got_makes, 1u, L.next_local);
                            prev = next;
                        } else if (op->kind == TCEXEC_QUERY) {
                            step++;
                            char sname[64];
                            snprintf(sname, sizeof sname, "r%u/%u", rule_ordinal, step);
                            TcTerm stage_sym = tc_symbol(terms, sname, strlen(sname));
                            TcTerm data_pat = presence_only
                                ? tcalc_wrap1(terms, entry->data_sym, op->primary)
                                : tcalc_wrap2(terms, entry->data_sym, op->target, op->primary);
                            TcTerm next = stage_sym
                                ? tcalc_wrap2(terms, stage_sym, lhs_tc, op->primary) : 0u;
                            TcTerm got_reads[2] = {prev, data_pat};
                            TcTerm got_makes[1] = {next};
                            ok = ok && data_pat && next &&
                                 tcalc_entry_push_rule(entry, sname, got_reads, 2u,
                                                       got_makes, 1u, L.next_local);
                            prev = next;
                        } else if (op->kind == TCEXEC_JOIN && op->jump_pc < entry->join_len) {
                            const TcalcJoinSpec *js = &entry->joins[op->jump_pc];
                            for (uint32_t lj = 0u; ok && lj < js->nlegs; lj++) {
                                step++;
                                char sname[64];
                                snprintf(sname, sizeof sname, "r%u/%u.join", rule_ordinal, step);
                                TcTerm stage_sym = tc_symbol(terms, sname, strlen(sname));
                                TcTerm data_pat = tcalc_wrap1(terms, entry->data_sym,
                                                              js->legs[lj].pat_tc);
                                TcTerm next = stage_sym
                                    ? tcalc_wrap2(terms, stage_sym, lhs_tc,
                                                  js->legs[lj].pat_tc) : 0u;
                                TcTerm got_reads[2] = {prev, data_pat};
                                TcTerm got_makes[1] = {next};
                                ok = ok && data_pat && next &&
                                     tcalc_entry_push_rule(entry, sname, got_reads, 2u,
                                                           got_makes, 1u, L.next_local);
                                prev = next;
                            }
                        } else if (op->kind == TCEXEC_PRIM) {
                            step++;
                            char sname[64];
                            snprintf(sname, sizeof sname, "r%u/%u.prim", rule_ordinal, step);
                            const char *pn = tcalc_prim_op_name(op->prim);
                            TcTerm psym = tc_symbol(terms, pn, strlen(pn));
                            TcTerm pexpr = psym
                                ? (op->secondary
                                   ? tcalc_wrap2(terms, psym, op->primary, op->secondary)
                                   : tcalc_wrap1(terms, psym, op->primary))
                                : 0u;
                            TcTerm pval = pexpr
                                ? tcalc_wrap2(terms, entry->value_sym, pexpr, op->target)
                                : 0u;
                            TcTerm stage_sym = tc_symbol(terms, sname, strlen(sname));
                            TcTerm next = stage_sym
                                ? tcalc_wrap2(terms, stage_sym, lhs_tc, op->target) : 0u;
                            TcTerm reads[2] = {prev, pval};
                            TcTerm makes[1] = {next};
                            ok = ok && pval && next &&
                                 tcalc_entry_push_rule(entry, sname, reads, 2u,
                                                       makes, 1u, L.next_local);
                            prev = next;
                        } else if (op->kind == TCEXEC_IF) {
                            step++;
                            char sname[64];
                            snprintf(sname, sizeof sname, "r%u/%u.if", rule_ordinal, step);
                            TcTerm next = tcalc_wrap2(terms, cond_sym, lhs_tc, op->primary);
                            TcTerm reads[1] = {prev};
                            TcTerm makes[1] = {next};
                            ok = ok && next &&
                                 tcalc_entry_push_rule(entry, sname, reads, 1u,
                                                       makes, 1u, L.next_local);
                            prev = next;
                        } else if (op->kind == TCEXEC_MATCH || op->kind == TCEXEC_TRY_MATCH) {
                            step++;
                            char sname[64];
                            snprintf(sname, sizeof sname, "r%u/%u.match", rule_ordinal, step);
                            TcTerm next = tcalc_wrap2(terms, bind_sym, op->primary, op->target);
                            TcTerm reads[1] = {prev};
                            TcTerm makes[1] = {next};
                            ok = ok && next &&
                                 tcalc_entry_push_rule(entry, sname, reads, 1u,
                                                       makes, 1u, L.next_local);
                            prev = next;
                        }
                    }
                    if (ok) {
                        char rname[64];
                        snprintf(rname, sizeof rname, "r%u", rule_ordinal);
                        TcTerm final_read[1] = {prev};
                        TcTerm final_makes[2];
                        final_makes[0] = tcalc_wrap2(terms, entry->value_sym, lhs_tc, rhs_tc);
                        uint32_t final_nmakes = 1u;
                        if (!presence_only) {
                            TcTerm ord_tc = tc_integer(terms, (int64_t)rule_ordinal);
                            TcTerm tag_t = ord_tc ? tcalc_wrap1(terms, tag_sym, ord_tc) : 0u;
                            TcTerm sub_t = tc_expression(terms, &subs_sym, 1u);
                            final_makes[1] = (tag_t && sub_t)
                                ? tcalc_wrap4(terms, entry->deriv_sym, lhs_tc,
                                              tag_t, sub_t, rhs_tc)
                                : 0u;
                            final_nmakes = 2u;
                            ok = ok && final_makes[1] != 0u;
                        }
                        ok = ok && final_makes[0] &&
                             tcalc_entry_push_rule(entry, rname, final_read, 1u,
                                                   final_makes, final_nmakes,
                                                   L.next_local);
                    }
                }
            }
            if (ok) {
                TcalcExecClause cl = {
                    .head_tc = h_tc,
                    .lhs_tc = lhs_tc,
                    .rule_ordinal = rule_ordinal,
                    .nlocals = L.next_local,
                    .ops = L.ops,
                    .op_len = L.op_len,
                };
                if (!tcalc_entry_push_clause(entry, cl)) {
                    free(L.ops);
                    snprintf(err, err_cap, "out of memory");
                    ok = false;
                }
            } else {
                free(L.ops);
            }
            rule_ordinal++;
        }
        free(eqs);
    }

    /* Collect all distinct compiled heads (user heads + lifted HO helpers)
     * and build CSR head -> clause index plus exact same-SCC branching analysis. */
    uint32_t total_heads = defined_len + entry->ho_list_len;
    if (ok && total_heads > 0u) {
        TcTerm *hsyms = calloc(total_heads, sizeof(*hsyms));
        uint32_t *hoff = calloc((size_t)total_heads + 1u, sizeof(*hoff));
        uint32_t *hcur = calloc(total_heads, sizeof(*hcur));
        uint32_t *hcidx = entry->clause_len
            ? malloc(entry->clause_len * sizeof(*hcidx)) : NULL;
        uint8_t *cg = calloc((size_t)total_heads * (size_t)total_heads, sizeof(*cg));
        if (!hsyms || !hoff || !hcur || (entry->clause_len && !hcidx) || !cg) {
            free(hsyms); free(hoff); free(hcur); free(hcidx); free(cg);
            snprintf(err, err_cap, "out of memory");
            ok = false;
        } else {
            for (uint32_t i = 0u; i < defined_len; i++)
                hsyms[i] = tcalc_intern_symbol(terms, defined[i]);
            for (uint32_t i = 0u; i < entry->ho_list_len; i++)
                hsyms[defined_len + i] = entry->ho_lists[i].helper_head_tc;
            for (size_t ci = 0u; ci < entry->clause_len; ci++) {
                TcTerm ch = entry->clauses[ci].head_tc;
                uint32_t caller_idx = UINT32_MAX;
                for (uint32_t i = 0u; i < total_heads; i++) {
                    if (hsyms[i] == ch) { caller_idx = i; hoff[i + 1u]++; break; }
                }
                if (caller_idx == UINT32_MAX) continue;
                const TcalcExecClause *cl = &entry->clauses[ci];
                for (uint32_t oi = 0u; oi < cl->op_len; oi++) {
                    TcTerm callee_hd = 0u;
                    if (cl->ops[oi].kind == TCEXEC_CALL) {
                        callee_hd = tc_item(terms, cl->ops[oi].primary, 0u);
                    } else if (cl->ops[oi].kind == TCEXEC_HO_LIST &&
                               cl->ops[oi].jump_pc < entry->ho_list_len) {
                        callee_hd = entry->ho_lists[cl->ops[oi].jump_pc].helper_head_tc;
                    }
                    if (!callee_hd) continue;
                    for (uint32_t dj = 0u; dj < total_heads; dj++) {
                        if (hsyms[dj] == callee_hd) {
                            cg[(size_t)caller_idx * total_heads + dj] = 1u;
                            break;
                        }
                    }
                }
            }
            for (uint32_t k = 0u; k < total_heads; k++) {
                for (uint32_t i = 0u; i < total_heads; i++) {
                    if (!cg[(size_t)i * total_heads + k]) continue;
                    for (uint32_t j = 0u; j < total_heads; j++) {
                        if (cg[(size_t)k * total_heads + j])
                            cg[(size_t)i * total_heads + j] = 1u;
                    }
                }
            }
            for (uint32_t d = 0u; d < total_heads; d++) {
                if (cg[(size_t)d * total_heads + d]) {
                    entry->has_recursion = true;
                    break;
                }
            }
            /* True branching recursion: a clause makes >= 2 calls into its OWN
             * strongly connected component (cg[caller][callee] && cg[callee][caller]). */
            for (size_t ci = 0u; ci < entry->clause_len && !entry->has_branching_recursion; ci++) {
                const TcalcExecClause *cl = &entry->clauses[ci];
                uint32_t caller_idx = UINT32_MAX;
                for (uint32_t i = 0u; i < total_heads; i++) {
                    if (hsyms[i] == cl->head_tc) { caller_idx = i; break; }
                }
                if (caller_idx == UINT32_MAX ||
                    !cg[(size_t)caller_idx * total_heads + caller_idx])
                    continue;
                uint32_t same_scc_calls = 0u;
                for (uint32_t oi = 0u; oi < cl->op_len; oi++) {
                    TcTerm callee_hd = 0u;
                    if (cl->ops[oi].kind == TCEXEC_CALL) {
                        callee_hd = tc_item(terms, cl->ops[oi].primary, 0u);
                    } else if (cl->ops[oi].kind == TCEXEC_HO_LIST &&
                               cl->ops[oi].jump_pc < entry->ho_list_len) {
                        callee_hd = entry->ho_lists[cl->ops[oi].jump_pc].helper_head_tc;
                    }
                    if (!callee_hd) continue;
                    for (uint32_t dj = 0u; dj < total_heads; dj++) {
                        if (hsyms[dj] == callee_hd &&
                            cg[(size_t)caller_idx * total_heads + dj] &&
                            cg[(size_t)dj * total_heads + caller_idx]) {
                            same_scc_calls++;
                            break;
                        }
                    }
                }
                if (same_scc_calls >= 2u)
                    entry->has_branching_recursion = true;
            }
            free(cg);

            for (uint32_t i = 0u; i < total_heads; i++) {
                hoff[i + 1u] += hoff[i];
                hcur[i] = hoff[i];
            }
            for (size_t ci = 0u; ci < entry->clause_len; ci++) {
                TcTerm ch = entry->clauses[ci].head_tc;
                for (uint32_t i = 0u; i < total_heads; i++) {
                    if (hsyms[i] == ch) { hcidx[hcur[i]++] = (uint32_t)ci; break; }
                }
            }
            free(hcur);
            entry->head_syms = hsyms;
            entry->head_off = hoff;
            entry->head_clause_idx = hcidx;
            entry->head_count = total_heads;
        }
    }
    free(defined);

    if (ok && rule_dep_len > 0u) {
        qsort(rule_deps, rule_dep_len, sizeof(*rule_deps), tcalc_sym_cmp);
        SpaceHeadSetStamp rstamp = {0};
        if (!space_theory_head_set_clock_capture(
                space, rule_deps, rule_dep_len,
                SPACE_THEORY_ROLE_RULES | SPACE_THEORY_ROLE_DECLS |
                    SPACE_THEORY_ROLE_ALIAS,
                &rstamp)) {
            snprintf(err, err_cap, "failed to capture version counters for dependency heads");
            ok = false;
        } else {
            entry->rule_dep_heads = rule_deps;
            entry->rule_dep_len = rule_dep_len;
            entry->rule_stamp = rstamp;
            rule_deps = NULL;
        }
    }
    if (ok && data_dep_len > 0u) {
        qsort(data_deps, data_dep_len, sizeof(*data_deps), tcalc_sym_cmp);
        TcalcDataRelIndex *rels = calloc(data_dep_len, sizeof(*rels));
        if (!rels) {
            snprintf(err, err_cap, "out of memory");
            ok = false;
        } else {
            for (uint32_t r = 0u; ok && r < data_dep_len; r++) {
                rels[r].rel_sym = data_deps[r];
                rels[r].rel_tc = tcalc_intern_symbol(terms, data_deps[r]);
                rels[r].min_arity = UINT32_MAX;
                if (!rels[r].rel_tc) {
                    snprintf(err, err_cap, "out of memory");
                    ok = false;
                }
            }
            if (ok) {
                entry->data_dep_heads = data_deps;
                entry->data_dep_len = data_dep_len;
                entry->data_rels = rels;
                data_deps = NULL;
                /* Bind every TCEXEC_QUERY and TcalcJoinLeg directly to its data_rels slot
                 * and record exactly which argument-key indexes each relation needs. */
                for (size_t ci = 0u; ci < entry->clause_len; ci++) {
                    TcalcExecClause *cl = &entry->clauses[ci];
                    TcalcLocalMask known = {{0}};
                    tcalc_collect_tc_locals(terms, cl->lhs_tc, &known, 0u);
                    for (uint32_t oi = 0u; oi < cl->op_len; oi++) {
                        TcalcExecOp *op = &cl->ops[oi];
                        if (op->kind == TCEXEC_QUERY) {
                            TcTerm rtc = op->secondary;
                            uint32_t rslot = data_dep_len;
                            for (uint32_t r = 0u; r < data_dep_len; r++) {
                                if (rels[r].rel_tc == rtc) {
                                    op->jump_pc = r;
                                    rslot = r;
                                    break;
                                }
                            }
                            if (rslot < data_dep_len) {
                                uint32_t ar = tc_length(terms, op->primary);
                                uint32_t ncols = ar > 1u ? ar - 1u : 0u;
                                if (ncols > TCALC_DATA_IDX_COLS) ncols = TCALC_DATA_IDX_COLS;
                                uint8_t bcols = 0u;
                                for (uint32_t p = 0u; p < ncols; p++) {
                                    TcalcLocalMask av = {{0}};
                                    tcalc_collect_tc_locals(
                                        terms, tc_item(terms, op->primary, p + 1u),
                                        &av, 0u);
                                    if (tcalc_lmask_subset(&av, &known))
                                        bcols = (uint8_t)(bcols | (uint8_t)(1u << p));
                                }
                                if ((bcols & 3u) == 3u)
                                    bcols = (uint8_t)(bcols | (uint8_t)(1u << TCALC_DATA_IDX_COLS));
                                rels[rslot].needed_idx_mask |= bcols;
                            }
                            tcalc_collect_tc_locals(terms, op->primary, &known, 0u);
                        } else if (op->kind == TCEXEC_JOIN && op->jump_pc < entry->join_len) {
                            const TcalcJoinSpec *js = &entry->joins[op->jump_pc];
                            for (uint32_t li = 0u; li < js->nlegs; li++)
                                tcalc_lmask_or(&known, &js->legs[li].all_vars);
                        } else if (op->target) {
                            tcalc_collect_tc_locals(terms, op->target, &known, 0u);
                        }
                    }
                }
                for (uint32_t ji = 0u; ji < entry->join_len; ji++) {
                    TcalcJoinSpec *js = &entry->joins[ji];
                    for (uint32_t li = 0u; li < js->nlegs; li++) {
                        SymbolId rs = js->legs[li].rel_sym;
                        uint32_t rslot = data_dep_len;
                        for (uint32_t r = 0u; r < data_dep_len; r++) {
                            if (rels[r].rel_sym == rs) {
                                js->legs[li].rel_idx = r;
                                rslot = r;
                                break;
                            }
                        }
                        if (rslot < data_dep_len) {
                            TcalcLocalMask avail = js->outer_bound;
                            for (uint32_t lj = 0u; lj < js->nlegs; lj++) {
                                if (lj != li)
                                    tcalc_lmask_or(&avail, &js->legs[lj].all_vars);
                            }
                            uint32_t ncols = js->legs[li].arity > 1u
                                ? js->legs[li].arity - 1u : 0u;
                            if (ncols > TCALC_DATA_IDX_COLS) ncols = TCALC_DATA_IDX_COLS;
                            uint8_t bcols = 0u;
                            for (uint32_t p = 0u; p < ncols; p++) {
                                if (tcalc_lmask_subset(&js->legs[li].arg_vars[p], &avail))
                                    bcols = (uint8_t)(bcols | (uint8_t)(1u << p));
                            }
                            if ((bcols & 3u) == 3u)
                                bcols = (uint8_t)(bcols | (uint8_t)(1u << TCALC_DATA_IDX_COLS));
                            rels[rslot].needed_idx_mask |= bcols;
                        }
                    }
                }
            } else {
                free(rels);
            }
        }
    }
    free(rule_deps);
    free(data_deps);

    if (ok && !space_theory_view_is_current(&view)) {
        snprintf(err, err_cap, "space changed during compilation");
        ok = false;
    }
    if (!ok) {
        tcalc_space_entry_release_compiled(entry);
        return false;
    }
    entry->compile_count++;
    return true;
}

static TcalcSpaceCacheEntry *tcalc_space_acquire_locked(
        TcalcSpaceCache *cache, Space *space, SymbolId entry_head,
        CettaLanguageId language_id, bool presence_only,
        bool count_hit, char *err, size_t err_cap) {
    uint64_t inst = space_instance_id(space);
    TcalcSpaceCacheEntry *slot = NULL;
    for (size_t i = 0u; i < cache->len; i++) {
        TcalcSpaceCacheEntry *e = &cache->entries[i];
        if (e->space == space && e->space_instance_id == inst &&
            e->entry_head == entry_head && e->language_id == language_id &&
            e->presence_only == presence_only) {
            slot = e;
            break;
        }
    }
    if (slot) {
        if (slot->terms && !slot->terms_need_reset && slot->rule_stamp.active &&
            space_theory_head_set_clock_current(
                space, slot->rule_dep_heads, slot->rule_dep_len, &slot->rule_stamp)) {
            if (count_hit) slot->hit_count++;
            return slot;
        }
        if (slot->compile_declined &&
            space_theory_token_is_current(slot->declined_token)) {
            snprintf(err, err_cap, "%s", slot->declined_reason);
            return NULL;
        }
    }
    if (!slot) {
        if (cache->len >= TCALC_SPACE_CACHE_BOUND) {
            for (size_t i = 0u; i < cache->len; i++)
                tcalc_space_entry_release_compiled(&cache->entries[i]);
            cache->len = 0u;
        }
        if (cache->len == cache->cap) {
            size_t next = cache->cap ? cache->cap * 2u : 8u;
            TcalcSpaceCacheEntry *grown = realloc(cache->entries, next * sizeof(*grown));
            if (!grown) {
                snprintf(err, err_cap, "out of memory");
                return NULL;
            }
            cache->entries = grown;
            cache->cap = next;
        }
        slot = &cache->entries[cache->len++];
        memset(slot, 0, sizeof(*slot));
    }
    uint64_t prev_compiles = slot->compile_count;
    bool was_reset = slot->terms_need_reset;
    if (!tcalc_space_compile_into(slot, space, entry_head, language_id,
                                  presence_only, err, err_cap)) {
        slot->space = space;
        slot->space_instance_id = inst;
        slot->entry_head = entry_head;
        slot->language_id = language_id;
        slot->presence_only = presence_only;
        slot->compile_declined = true;
        slot->declined_token = space_theory_token(space);
        snprintf(slot->declined_reason, sizeof slot->declined_reason, "%s",
                 err[0] ? err : "outside the compiled fragment");
        return NULL;
    }
    if (was_reset) {
        slot->compile_count = prev_compiles;
        if (count_hit) slot->hit_count++;
    }
    return slot;
}

static Space *tcalc_resolve_space_arg(Space *current, Atom *arg) {
    if (!arg) return NULL;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_SPACE)
        return (Space *)arg->ground.ptr;
    if (arg->kind == ATOM_SYMBOL && arg->sym_id == g_builtin_syms.self)
        return current;
    return NULL;
}

/* Compact open-addressed map over (call_id, val) keys for exact O(V+E)
 * bottom-up DAG multiplicity computation. */
static inline uint64_t tcalc_mix64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static size_t tcalc_pow2_cap(size_t need) {
    size_t cap = 16u;
    size_t target = need * 2u + 8u;
    while (cap < target) cap <<= 1;
    return cap;
}

typedef struct {
    uint64_t *keys;
    uint64_t *vals;
    size_t mask;
} TcalcMultMap;

static bool tcalc_mult_map_init(TcalcMultMap *m, size_t max_items) {
    size_t cap = tcalc_pow2_cap(max_items);
    m->keys = calloc(cap, sizeof(*m->keys));
    m->vals = calloc(cap, sizeof(*m->vals));
    if (!m->keys || !m->vals) {
        free(m->keys);
        free(m->vals);
        m->keys = NULL;
        m->vals = NULL;
        return false;
    }
    m->mask = cap - 1u;
    return true;
}

static void tcalc_mult_map_free(TcalcMultMap *m) {
    free(m->keys);
    free(m->vals);
    m->keys = NULL;
    m->vals = NULL;
}

static inline uint64_t tcalc_cv_key(TcTerm call_tc, TcTerm val_tc) {
    return ((uint64_t)call_tc << 32) | (uint64_t)val_tc;
}

static uint64_t tcalc_mult_map_get(const TcalcMultMap *m, TcTerm call_tc,
                                   TcTerm val_tc) {
    uint64_t k = tcalc_cv_key(call_tc, val_tc);
    size_t i = (size_t)tcalc_mix64(k) & m->mask;
    while (m->keys[i]) {
        if (m->keys[i] == k) return m->vals[i];
        i = (i + 1u) & m->mask;
    }
    return 0u;
}

static bool tcalc_mult_map_add(TcalcMultMap *m, TcTerm call_tc, TcTerm val_tc,
                               uint64_t delta, bool *is_new) {
    uint64_t k = tcalc_cv_key(call_tc, val_tc);
    size_t i = (size_t)tcalc_mix64(k) & m->mask;
    while (m->keys[i]) {
        if (m->keys[i] == k) {
            if (delta > UINT64_MAX - m->vals[i]) return false;
            m->vals[i] += delta;
            if (is_new) *is_new = false;
            return true;
        }
        i = (i + 1u) & m->mask;
    }
    m->keys[i] = k;
    m->vals[i] = delta;
    if (is_new) *is_new = true;
    return true;
}

/* Emit decoded answers directly into *OUT as exact MeTTa answers (failure,
 * single completed value, or completed frontier of individual answers). */
static bool tcalc_emit_answers_outcome(const TcalcSpaceCacheEntry *entry,
                                       CettaLanguageId language_id, Arena *a,
                                       const TcTerm *vals, const uint64_t *counts,
                                       size_t distinct_len, uint64_t total_answers,
                                       CettaCallOutcome *out) {
    if (total_answers == 0u) {
        *out = cetta_call_failure();
        return true;
    }
    if (total_answers > TCALC_SPACE_MAX_ANSWERS)
        return false;
    if (total_answers == 1u) {
        for (size_t i = 0u; i < distinct_len; i++) {
            if (counts[i] == 0u) continue;
            Atom *v = tcalc_tc_to_atom(entry->terms, entry->bool_sym,
                                       entry->float_sym, language_id,
                                       vals[i], a, 0u);
            if (!v) return false;
            *out = cetta_call_completed_value(v);
            return true;
        }
        *out = cetta_call_failure();
        return true;
    }
    Atom *frontier = atom_expr_builder_begin(a, (CettaExprLen)total_answers);
    if (!frontier) return false;
    CettaExprLen pos = 0u;
    for (size_t i = 0u; i < distinct_len; i++) {
        uint64_t c = counts[i];
        if (c == 0u) continue;
        Atom *v = tcalc_tc_to_atom(entry->terms, entry->bool_sym,
                                   entry->float_sym, language_id,
                                   vals[i], a, 0u);
        if (!v) return false;
        for (uint64_t k = 0u; k < c; k++)
            frontier->expr.elems[pos++] = v;
    }
    frontier = atom_expr_builder_finish(a, frontier);
    if (!frontier) return false;
    *out = cetta_call_completed_frontier(frontier);
    return true;
}

#include "tcalc_space_slg.inc"

/* Verify that every argument inside CALL is an inert ground value that needs
 * no prior evaluation under strict call-by-value semantics. */
static bool tcalc_atom_is_inert_value(Space *space, const Atom *atom,
                                      CettaLanguageId language_id,
                                      uint32_t depth) {
    if (!atom || depth > TCALC_SPACE_MAX_DEPTH) return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return false;
    case ATOM_SYMBOL:
        if (atom_is_empty((Atom *)atom) ||
            atom->sym_id == g_builtin_syms.empty ||
            atom->sym_id == g_builtin_syms.self ||
            is_grounded_op(atom->sym_id) ||
            tcalc_space_symbol_has_bare_rule(space, atom->sym_id))
            return false;
        return true;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL)
            return true;
        if (atom->ground.gkind == GV_FLOAT)
            return isfinite(atom->ground.fval);
        if (atom->ground.gkind == GV_STRING)
            return memchr(atom->ground.sval, '\0', atom->ground.slen) == NULL;
        return false;
    case ATOM_EXPR:
        if (atom->expr.len == 0u) return true;
        if (!atom->expr.elems[0]) return false;
        if (atom->expr.elems[0]->kind == ATOM_SYMBOL) {
            SymbolId h = atom->expr.elems[0]->sym_id;
            if (space_equations_may_match_known_head(space, h) ||
                tcalc_space_head_is_reserved_op(h, language_id))
                return false;
        } else {
            if (!tcalc_atom_is_inert_value(space, atom->expr.elems[0],
                                           language_id, depth + 1u))
                return false;
        }
        for (CettaExprLen i = 1u; i < atom->expr.len; i++) {
            if (!tcalc_atom_is_inert_value(space, atom->expr.elems[i],
                                           language_id, depth + 1u))
                return false;
        }
        return true;
    }
    return false;
}

static pthread_once_t g_tcalc_eval_mode_once = PTHREAD_ONCE_INIT;
static _Atomic int g_tcalc_eval_mode_ready = 0;
static int g_tcalc_eval_mode = 1; /* 0 = off, 1 = auto (default), 2 = always */
static void tcalc_eval_mode_init(void);

void cetta_tcalc_set_eval_mode_from_startup(int mode) {
    g_tcalc_eval_mode = mode;
    atomic_store_explicit(&g_tcalc_eval_mode_ready, 1, memory_order_release);
}

static inline int tcalc_get_eval_mode(void) {
    if (__builtin_expect(
            atomic_load_explicit(&g_tcalc_eval_mode_ready, memory_order_acquire) != 0,
            1))
        return g_tcalc_eval_mode;
    pthread_once(&g_tcalc_eval_mode_once, tcalc_eval_mode_init);
    atomic_store_explicit(&g_tcalc_eval_mode_ready, 1, memory_order_release);
    return g_tcalc_eval_mode;
}

bool cetta_tcalc_eval_mode_is_always(void) {
    return tcalc_get_eval_mode() == 2;
}

extern char **environ;

static void tcalc_eval_mode_init(void) {
    const char *env = NULL;
    bool disable_auto = false;
    if (environ) {
        for (char **ep = environ; *ep; ep++) {
            const char *e = *ep;
            if (e[0] != 'C' || strncmp(e, "CETTA_", 6u) != 0)
                continue;
            const char *rest = e + 6u;
            if (strncmp(rest, "TCALC_EVAL=", 11u) == 0) {
                env = rest + 11u;
            } else if ((strncmp(rest, "PREPARED_PURE_DEBUG=", 20u) == 0 &&
                        rest[20] && strcmp(rest + 20u, "0") != 0) ||
                       (strncmp(rest, "PREPARED_PURE_CODE_REFERENCE=", 29u) == 0 &&
                        rest[29] && strcmp(rest + 29u, "0") != 0) ||
                       (strncmp(rest, "SEARCH_CONTROLLER=", 18u) == 0 &&
                        rest[18]) ||
                       (strncmp(rest, "SEARCH_CONTROLLER_TRACE=", 24u) == 0 &&
                        rest[24] && strcmp(rest + 24u, "0") != 0) ||
                       (strncmp(rest, "PETTA_MACHINE_STATS=", 20u) == 0 &&
                        rest[20] && strcmp(rest + 20u, "0") != 0) ||
                       (strncmp(rest, "PETTA_MACHINE_TRANSITION_LIMIT=", 31u) == 0 &&
                        rest[31] && strcmp(rest + 31u, "0") != 0)) {
                disable_auto = true;
            }
        }
    }
    if (!env || !env[0]) {
        g_tcalc_eval_mode = disable_auto ? 0 : 1;
    } else if (strcmp(env, "0") == 0 || strcmp(env, "off") == 0 ||
               strcmp(env, "false") == 0) {
        g_tcalc_eval_mode = 0;
    } else if (strcmp(env, "always") == 0 || strcmp(env, "2") == 0) {
        g_tcalc_eval_mode = 2;
    } else {
        g_tcalc_eval_mode = 1;
    }
}

static _Thread_local uint32_t g_tcalc_space_eval_depth = 0u;

/* Prepare CALL arguments: accept already-inert values directly, and evaluate
 * single-value nested calls inside the compiled fragment (e.g. `(place 8 (upto 8) Nil)`
 * or `(len (nrev (upto 400)))`) without leaving the compiled route. */
static Atom *tcalc_prepare_call_args(struct CettaLibraryContext *ctx,
                                     Space *space, Arena *a, Atom *call,
                                     CettaLanguageId language_id) {
    if (!call || call->kind != ATOM_EXPR || call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    bool all_inert = true;
    for (CettaExprLen i = 1u; i < call->expr.len; i++) {
        if (!tcalc_atom_is_inert_value(space, call->expr.elems[i], language_id, 0u)) {
            all_inert = false;
            break;
        }
    }
    if (all_inert) return call;
    if (g_tcalc_space_eval_depth >= 8u) return NULL;

    CettaExprLen n = call->expr.len;
    Atom *stack_elems[16];
    Atom **elems = n <= 16u ? stack_elems : malloc((size_t)n * sizeof(*elems));
    if (!elems) return NULL;
    elems[0] = call->expr.elems[0];
    bool ok = true;
    for (CettaExprLen i = 1u; ok && i < n; i++) {
        Atom *arg = call->expr.elems[i];
        if (tcalc_atom_is_inert_value(space, arg, language_id, 0u)) {
            elems[i] = arg;
            continue;
        }
        if (arg && arg->kind == ATOM_EXPR && arg->expr.len > 0u &&
            arg->expr.elems[0]->kind == ATOM_SYMBOL &&
            space_equations_may_match_known_head(space, arg->expr.elems[0]->sym_id)) {
            CettaCallOutcome sub_out;
            if (cetta_tcalc_space_try_eval(ctx, space, a, arg, language_id,
                                           CETTA_TCALC_OBSERVE_MULTISET, &sub_out) &&
                sub_out.kind == CETTA_CALL_VALUE &&
                !cetta_call_result_is_frontier(sub_out.result_form) &&
                sub_out.term && tcalc_atom_is_inert_value(space, sub_out.term,
                                                          language_id, 0u)) {
                elems[i] = sub_out.term;
                continue;
            }
        }
        ok = false;
    }
    Atom *res = ok ? atom_expr(a, elems, n) : NULL;
    if (elems != stack_elems) free(elems);
    return res;
}

static inline bool tcalc_entry_auto_eligible(const TcalcSpaceCacheEntry *entry,
                                             CettaLanguageId language_id,
                                             CettaTcalcObservation observation) {
    if (!entry) return false;
    if (observation == CETTA_TCALC_OBSERVE_MULTISET ||
        observation == CETTA_TCALC_OBSERVE_PRESENCE ||
        g_tcalc_eval_mode == 2)
        return true;
    if (observation == CETTA_TCALC_OBSERVE_MULTISET_AUTO_EARLY)
        return entry->has_branching_recursion;
    /* Automatic cost model:
     *   - PeTTa's search machine already executes linear recursion, single-atom
     *     one-shot data scans, and non-overlapping backtracking (queens-8) in
     *     tight frames without compilation overhead; compiled SLG + argument-key
     *     indexes + join planning wins on same-SCC branching recursion
     *     (overlapping subproblems) and on data-matching workloads with
     *     recursion, multi-leg joins, or higher-order list batching.
     *   - HE's tree-walking interpreter benefits from compiled SLG on all
     *     recursive functions and all data-matching / join workloads. */
    bool data_worth_compiling = entry->data_dep_len > 0u &&
        (entry->has_recursion || entry->join_len > 0u || entry->ho_list_len > 0u);
    if (language_id == CETTA_LANGUAGE_PETTA)
        return entry->has_branching_recursion || data_worth_compiling;
    return entry->has_recursion || data_worth_compiling;
}

/* Zero-allocation syntactic pre-filter for AUTO / AUTO_EARLY: checks whether
 * any reachable definition of ENTRY_HEAD makes >= 2 calls into its OWN strongly
 * connected component along a single control path (true branching recursion
 * like fib/bfib, NOT linear helpers like nrev calling rev+app or queens
 * calling pick+safe+place), and detects data matches, conjunctive/nested joins,
 * and higher-order list operations. */
static void tcalc_collect_probe_edges(Space *space, CettaLanguageId language_id,
                                      const Atom *expr, uint32_t depth,
                                      SymbolId *queue, uint32_t *qlen,
                                      uint16_t *edge_mask, bool *has_data_match,
                                      bool *has_multi_data, bool in_match) {
    if (!expr || depth > 24u || expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return;
    const Atom *hd = expr->expr.elems[0];
    if (hd && hd->kind == ATOM_SYMBOL) {
        SymbolId cs = hd->sym_id;
        if (cs == g_builtin_syms.match && expr->expr.len == 4u) {
            if (has_data_match) *has_data_match = true;
            const Atom *pat = expr->expr.elems[2];
            if (has_multi_data &&
                (in_match ||
                 (pat && pat->kind == ATOM_EXPR && pat->expr.len >= 3u &&
                  pat->expr.elems[0]->kind == ATOM_SYMBOL &&
                  pat->expr.elems[0]->sym_id == g_builtin_syms.comma)))
                *has_multi_data = true;
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[3],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, true);
            return;
        }
        if (cs == g_builtin_syms.case_text && expr->expr.len == 3u) {
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[1],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            const Atom *bl = expr->expr.elems[2];
            if (bl && bl->kind == ATOM_EXPR) {
                for (CettaExprLen bi = 0u; bi < bl->expr.len; bi++) {
                    const Atom *br = bl->expr.elems[bi];
                    if (br && br->kind == ATOM_EXPR && br->expr.len == 2u)
                        tcalc_collect_probe_edges(space, language_id,
                                                  br->expr.elems[1], depth + 1u,
                                                  queue, qlen, edge_mask,
                                                  has_data_match, has_multi_data,
                                                  in_match);
                }
            }
            return;
        }
        if ((cs == g_builtin_syms.map_atom || cs == g_builtin_syms.filter_atom) &&
            expr->expr.len == 4u) {
            if (has_multi_data) *has_multi_data = true;
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[1],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[3],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            return;
        }
        if (cs == g_builtin_syms.foldl_atom && expr->expr.len == 6u) {
            if (has_multi_data) *has_multi_data = true;
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[1],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[2],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            tcalc_collect_probe_edges(space, language_id, expr->expr.elems[5],
                                      depth + 1u, queue, qlen, edge_mask,
                                      has_data_match, has_multi_data, in_match);
            return;
        }
        if (!tcalc_space_head_is_reserved_op(cs, language_id) &&
            space_equations_may_match_known_head(space, cs)) {
            uint32_t idx = UINT32_MAX;
            for (uint32_t i = 0u; i < *qlen; i++) {
                if (queue[i] == cs) { idx = i; break; }
            }
            if (idx == UINT32_MAX && *qlen < 16u) {
                idx = (*qlen)++;
                queue[idx] = cs;
            }
            if (idx < 16u)
                *edge_mask = (uint16_t)(*edge_mask | (uint16_t)(1u << idx));
        }
    }
    for (CettaExprLen i = 1u; i < expr->expr.len; i++) {
        tcalc_collect_probe_edges(space, language_id, expr->expr.elems[i],
                                  depth + 1u, queue, qlen, edge_mask,
                                  has_data_match, has_multi_data, in_match);
    }
}

static uint32_t tcalc_count_path_scc_calls(const Atom *expr, uint32_t depth,
                                           const SymbolId *queue, uint32_t qlen,
                                           uint16_t scc_mask) {
    if (!expr || depth > 24u || expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return 0u;
    const Atom *hd = expr->expr.elems[0];
    if (hd && hd->kind == ATOM_SYMBOL) {
        SymbolId cs = hd->sym_id;
        if (cs == g_builtin_syms.if_text && expr->expr.len == 4u) {
            uint32_t c = tcalc_count_path_scc_calls(
                expr->expr.elems[1], depth + 1u, queue, qlen, scc_mask);
            uint32_t t = tcalc_count_path_scc_calls(
                expr->expr.elems[2], depth + 1u, queue, qlen, scc_mask);
            uint32_t e = tcalc_count_path_scc_calls(
                expr->expr.elems[3], depth + 1u, queue, qlen, scc_mask);
            return c + (t > e ? t : e);
        }
        uint32_t self_call = 0u;
        for (uint32_t i = 0u; i < qlen; i++) {
            if (queue[i] == cs) {
                if ((scc_mask & (uint16_t)(1u << i)) != 0u)
                    self_call = 1u;
                break;
            }
        }
        uint32_t sub = 0u;
        for (CettaExprLen i = 1u; i < expr->expr.len && sub + self_call < 2u; i++) {
            sub += tcalc_count_path_scc_calls(
                expr->expr.elems[i], depth + 1u, queue, qlen, scc_mask);
        }
        return self_call + sub;
    }
    return 0u;
}

static bool tcalc_expr_has_effectful_head(const Atom *expr, uint32_t depth);

static bool tcalc_probe_same_scc_branching(Space *space, SymbolId entry_head,
                                           CettaLanguageId language_id,
                                           bool check_effectful,
                                           bool *effectful_out,
                                           bool *has_data_out,
                                           bool *has_multi_data_out,
                                           bool *has_rec_out) {
    SymbolId queue[16];
    uint16_t adj[16] = {0};
    uint32_t qlen = 1u;
    queue[0] = entry_head;
    bool has_data = false;
    bool has_multi_data = false;
    for (uint32_t qi = 0u; qi < qlen; qi++) {
        SpaceEquationCursor cur;
        if (!space_rule_cursor_init(
                space, queue[qi],
                CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, &cur))
            break;
        SpaceEquationOccurrence occ;
        uint32_t checked = 0u;
        bool entry_has_non_prelude = (qi != 0u || space->imported_len == 0u);
        while (space_rule_cursor_read(&cur, &occ) == SPACE_EQUATION_CURSOR_ITEM) {
            const Atom *eq = occ.equation;
            if (!eq || eq->kind != ATOM_EXPR || eq->expr.len != 3u) continue;
            if (qi == 0u && !entry_has_non_prelude &&
                (occ.id.read.space != space ||
                 occ.id.logical_index >= space->imported_len))
                entry_has_non_prelude = true;
            if (check_effectful && qi == 0u && checked < 8u &&
                tcalc_expr_has_effectful_head(eq->expr.elems[2], 0u)) {
                if (effectful_out) *effectful_out = true;
                return false;
            }
            tcalc_collect_probe_edges(space, language_id, eq->expr.elems[2],
                                      0u, queue, &qlen, &adj[qi], &has_data,
                                      &has_multi_data, false);
            checked++;
        }
        if (qi == 0u && !entry_has_non_prelude) {
            if (effectful_out) *effectful_out = true;
            return false;
        }
    }
    if (has_data_out) *has_data_out = has_data;
    if (has_multi_data_out) *has_multi_data_out = has_multi_data;
    for (uint32_t k = 0u; k < qlen; k++) {
        uint16_t kbit = (uint16_t)(1u << k);
        for (uint32_t i = 0u; i < qlen; i++) {
            if (adj[i] & kbit)
                adj[i] = (uint16_t)(adj[i] | adj[k]);
        }
    }
    bool any_rec = false;
    for (uint32_t qi = 0u; qi < qlen; qi++) {
        if ((adj[qi] & (uint16_t)(1u << qi)) == 0u)
            continue; /* not recursive */
        any_rec = true;
        uint16_t scc_mask = 0u;
        for (uint32_t j = 0u; j < qlen; j++) {
            if ((adj[qi] & (uint16_t)(1u << j)) &&
                (adj[j] & (uint16_t)(1u << qi)))
                scc_mask = (uint16_t)(scc_mask | (uint16_t)(1u << j));
        }
        SpaceEquationCursor cur;
        if (!space_rule_cursor_init(
                space, queue[qi],
                CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, &cur))
            continue;
        SpaceEquationOccurrence occ;
        while (space_rule_cursor_read(&cur, &occ) == SPACE_EQUATION_CURSOR_ITEM) {
            const Atom *eq = occ.equation;
            if (!eq || eq->kind != ATOM_EXPR || eq->expr.len != 3u) continue;
            if (tcalc_count_path_scc_calls(eq->expr.elems[2], 0u,
                                           queue, qlen, scc_mask) >= 2u) {
                if (has_rec_out) *has_rec_out = true;
                return true;
            }
        }
    }
    if (has_rec_out) *has_rec_out = any_rec;
    return false;
}

enum {
    TCALC_PROBE_KNOWN_BRANCH = 1u << 0,
    TCALC_PROBE_MAY_BRANCH   = 1u << 1,
    TCALC_PROBE_NO_EQ        = 1u << 2,
    TCALC_PROBE_DECLINED     = 1u << 3,
    TCALC_PROBE_RESERVED     = 1u << 4,
    TCALC_PROBE_HAS_DATA     = 1u << 5,
    TCALC_PROBE_HAS_REC      = 1u << 6,
    TCALC_PROBE_MULTI_DATA   = 1u << 7,
};

typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t eq_rev;
    uint64_t decl_rev;
    SymbolId head;
    uint8_t lang;
    uint8_t flags;
} TcalcBranchProbeSlot;

static _Thread_local TcalcBranchProbeSlot g_tcalc_branch_probe[64];

static inline TcalcBranchProbeSlot *tcalc_probe_slot(
        const Space *space, SymbolId entry_head, CettaLanguageId language_id,
        uint64_t inst, uint64_t eq_rev, uint64_t decl_rev) {
    uint32_t slot_idx = ((uint32_t)entry_head * 2654435761u) & 63u;
    TcalcBranchProbeSlot *slot = &g_tcalc_branch_probe[slot_idx];
    if (slot->space != space || slot->instance_id != inst ||
        slot->eq_rev != eq_rev || slot->decl_rev != decl_rev ||
        slot->head != entry_head || slot->lang != (uint8_t)language_id) {
        *slot = (TcalcBranchProbeSlot){
            .space = space,
            .instance_id = inst,
            .eq_rev = eq_rev,
            .decl_rev = decl_rev,
            .head = entry_head,
            .lang = (uint8_t)language_id,
            .flags = 0u,
        };
    }
    return slot;
}

static bool tcalc_head_might_branch_recurse(Space *space, SymbolId entry_head,
                                            CettaLanguageId language_id,
                                            TcalcBranchProbeSlot *slot) {
    if (slot->flags & TCALC_PROBE_KNOWN_BRANCH)
        return (slot->flags & TCALC_PROBE_MAY_BRANCH) != 0u;
    bool has_data = false, has_multi_data = false, has_rec = false;
    bool result = tcalc_probe_same_scc_branching(
        space, entry_head, language_id, false, NULL, &has_data,
        &has_multi_data, &has_rec);
    slot->flags = (uint8_t)(slot->flags | TCALC_PROBE_KNOWN_BRANCH |
                            (result ? TCALC_PROBE_MAY_BRANCH : 0u) |
                            (has_data ? TCALC_PROBE_HAS_DATA : 0u) |
                            (has_multi_data ? TCALC_PROBE_MULTI_DATA : 0u) |
                            (has_rec ? TCALC_PROBE_HAS_REC : 0u));
    return result;
}

static bool tcalc_expr_has_effectful_head(const Atom *expr, uint32_t depth) {
    if (!expr || depth > 24u || expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return false;
    const Atom *hd = expr->expr.elems[0];
    if (hd && hd->kind == ATOM_SYMBOL) {
        SymbolId s = hd->sym_id;
        if (s == g_builtin_syms.new_space ||
            s == g_builtin_syms.add_atom ||
            s == g_builtin_syms.add_atom_nodup ||
            s == g_builtin_syms.remove_atom ||
            s == g_builtin_syms.add_reduct ||
            s == g_builtin_syms.get_atoms ||
            s == g_builtin_syms.new_state ||
            s == g_builtin_syms.change_state_bang ||
            s == g_builtin_syms.get_state ||
            s == g_builtin_syms.import_bang ||
            s == g_builtin_syms.import_as_bang ||
            s == g_builtin_syms.include ||
            s == g_builtin_syms.println_bang)
            return true;
    }
    for (CettaExprLen i = 1u; i < expr->expr.len; i++) {
        if (tcalc_expr_has_effectful_head(expr->expr.elems[i], depth + 1u))
            return true;
    }
    return false;
}

static bool tcalc_head_has_effectful_definition(Space *space, SymbolId entry_head) {
    SpaceEquationCursor cur;
    if (!space_rule_cursor_init(
            space, entry_head,
            CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL, &cur))
        return false;
    SpaceEquationOccurrence occ;
    uint32_t checked = 0u;
    while (checked < 8u &&
           space_rule_cursor_read(&cur, &occ) == SPACE_EQUATION_CURSOR_ITEM) {
        const Atom *eq = occ.equation;
        if (eq && eq->kind == ATOM_EXPR && eq->expr.len == 3u &&
            tcalc_expr_has_effectful_head(eq->expr.elems[2], 0u))
            return true;
        checked++;
    }
    return false;
}

bool cetta_tcalc_space_head_auto_eligible(Space *space, SymbolId entry_head,
                                          CettaLanguageId language_id,
                                          bool early) {
    if (!space || entry_head == SYMBOL_ID_NONE)
        return false;
    if (language_id != CETTA_LANGUAGE_HE && language_id != CETTA_LANGUAGE_PETTA)
        return false;
    int eval_mode = tcalc_get_eval_mode();
    if (eval_mode == 0)
        return false;
    TcalcBranchProbeSlot *probe_slot = NULL;
    if (!space->overlay_base) {
        uint64_t inst = space_instance_id(space);
        uint64_t eq_rev = space_equation_revision(space);
        uint64_t decl_rev = space_declaration_revision(space);
        probe_slot = tcalc_probe_slot(
            space, entry_head, language_id, inst, eq_rev, decl_rev);
        if (probe_slot->flags &
            (TCALC_PROBE_RESERVED | TCALC_PROBE_NO_EQ | TCALC_PROBE_DECLINED))
            return false;
        if (probe_slot->flags & TCALC_PROBE_KNOWN_BRANCH) {
            if (eval_mode == 2) return true;
            if (early) return (probe_slot->flags & TCALC_PROBE_MAY_BRANCH) != 0u;
            bool data_worth = (probe_slot->flags & TCALC_PROBE_HAS_DATA) != 0u &&
                (probe_slot->flags &
                 (TCALC_PROBE_HAS_REC | TCALC_PROBE_MULTI_DATA)) != 0u;
            if (language_id == CETTA_LANGUAGE_PETTA)
                return (probe_slot->flags & TCALC_PROBE_MAY_BRANCH) != 0u || data_worth;
            return (probe_slot->flags & TCALC_PROBE_HAS_REC) != 0u || data_worth;
        }
    }
    if (tcalc_space_head_is_reserved_op(entry_head, language_id)) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_RESERVED;
        return false;
    }
    if (!space_equations_may_match_known_head(space, entry_head)) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_NO_EQ;
        return false;
    }
    if (eval_mode == 2) {
        if (tcalc_head_has_effectful_definition(space, entry_head)) {
            if (probe_slot) probe_slot->flags |= TCALC_PROBE_DECLINED;
            return false;
        }
        return true;
    }
    bool effectful = false, has_data = false, has_multi_data = false, has_rec = false;
    bool may_branch = tcalc_probe_same_scc_branching(
        space, entry_head, language_id, true, &effectful, &has_data,
        &has_multi_data, &has_rec);
    if (effectful) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_DECLINED;
        return false;
    }
    if (probe_slot) {
        probe_slot->flags = (uint8_t)(probe_slot->flags |
            TCALC_PROBE_KNOWN_BRANCH |
            (may_branch ? TCALC_PROBE_MAY_BRANCH : 0u) |
            (has_data ? TCALC_PROBE_HAS_DATA : 0u) |
            (has_multi_data ? TCALC_PROBE_MULTI_DATA : 0u) |
            (has_rec ? TCALC_PROBE_HAS_REC : 0u));
    }
    if (early) return may_branch;
    bool data_worth = has_data && (has_rec || has_multi_data);
    if (language_id == CETTA_LANGUAGE_PETTA)
        return may_branch || data_worth;
    return has_rec || data_worth;
}

bool cetta_tcalc_space_try_eval(struct CettaLibraryContext *ctx, Space *space,
                                Arena *a, Atom *call,
                                CettaLanguageId language_id,
                                CettaTcalcObservation observation,
                                CettaCallOutcome *out) {
    if (!ctx || !space || !a || !call || !out) return false;
    if (g_tcalc_space_eval_depth >= 12u) return false;
    if (language_id != CETTA_LANGUAGE_HE && language_id != CETTA_LANGUAGE_PETTA)
        return false;
    if (call->kind != ATOM_EXPR || call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    SymbolId entry_head = call->expr.elems[0]->sym_id;
    bool is_auto_early = (observation == CETTA_TCALC_OBSERVE_MULTISET_AUTO_EARLY);
    bool is_auto = is_auto_early || (observation == CETTA_TCALC_OBSERVE_MULTISET_AUTO);
    bool presence_only = (observation == CETTA_TCALC_OBSERVE_PRESENCE);
    TcalcBranchProbeSlot *probe_slot = NULL;
    if (!presence_only && !space->overlay_base) {
        uint64_t inst = space_instance_id(space);
        uint64_t eq_rev = space_equation_revision(space);
        uint64_t decl_rev = space_declaration_revision(space);
        probe_slot = tcalc_probe_slot(
            space, entry_head, language_id, inst, eq_rev, decl_rev);
        if (probe_slot->flags &
            (TCALC_PROBE_RESERVED | TCALC_PROBE_NO_EQ | TCALC_PROBE_DECLINED))
            return false;
        if (is_auto_early && tcalc_get_eval_mode() != 2 &&
            (probe_slot->flags & TCALC_PROBE_KNOWN_BRANCH) &&
            !(probe_slot->flags & TCALC_PROBE_MAY_BRANCH))
            return false;
    }
    if (tcalc_space_head_is_reserved_op(entry_head, language_id)) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_RESERVED;
        return false;
    }
    if (!space_equations_may_match_known_head(space, entry_head)) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_NO_EQ;
        return false;
    }
    if (tcalc_head_has_effectful_definition(space, entry_head)) {
        if (probe_slot) probe_slot->flags |= TCALC_PROBE_DECLINED;
        return false;
    }
    if (is_auto) {
        if (tcalc_get_eval_mode() == 0) return false;
        if (language_id == CETTA_LANGUAGE_PETTA &&
            (ctx->petta_translator_rule_len > 0u ||
             cetta_library_petta_tabling_enabled(ctx) ||
             ctx->petta_memo.all_arity_len > 0u ||
             ctx->petta_memo.exact_arity_len > 0u))
            return false;
    }
    if (is_auto_early && tcalc_get_eval_mode() != 2) {
        TcalcBranchProbeSlot local_slot = {0};
        TcalcBranchProbeSlot *bs = probe_slot ? probe_slot : &local_slot;
        if (!tcalc_head_might_branch_recurse(space, entry_head, language_id, bs))
            return false;
    }

    TcalcSpaceCache *cache = tcalc_space_cache_get(ctx);
    if (!cache) return false;

    char message[256] = {0};

    /* Fast single-lock path when all arguments are already inert values. */
    bool all_inert = true;
    for (CettaExprLen i = 1u; i < call->expr.len; i++) {
        if (!tcalc_atom_is_inert_value(space, call->expr.elems[i], language_id, 0u)) {
            all_inert = false;
            break;
        }
    }

    Atom *ready_call = call;
    bool was_already_compiled = false;
    if (!all_inert) {
        pthread_mutex_lock(&cache->lock);
        uint64_t inst = space_instance_id(space);
        for (size_t i = 0u; i < cache->len; i++) {
            TcalcSpaceCacheEntry *e = &cache->entries[i];
            if (e->space == space && e->space_instance_id == inst &&
                e->entry_head == entry_head && e->language_id == language_id &&
                e->presence_only == presence_only && e->terms &&
                !e->terms_need_reset && e->rule_stamp.active &&
                space_theory_head_set_clock_current(
                    space, e->rule_dep_heads, e->rule_dep_len, &e->rule_stamp)) {
                was_already_compiled = true;
                break;
            }
        }
        TcalcSpaceCacheEntry *probe = tcalc_space_acquire_locked(
            cache, space, entry_head, language_id, presence_only,
            false, message, sizeof message);
        bool eligible = tcalc_entry_auto_eligible(probe, language_id, observation);
        if (!probe && probe_slot)
            probe_slot->flags |= TCALC_PROBE_DECLINED;
        pthread_mutex_unlock(&cache->lock);
        if (!eligible) return false;
        g_tcalc_space_eval_depth++;
        ready_call = tcalc_prepare_call_args(ctx, space, a, call, language_id);
        g_tcalc_space_eval_depth--;
        if (!ready_call) return false;
    }

    bool handled = false;
    g_tcalc_space_eval_depth++;
    pthread_mutex_lock(&cache->lock);
    if (all_inert) {
        uint64_t inst = space_instance_id(space);
        for (size_t i = 0u; i < cache->len; i++) {
            TcalcSpaceCacheEntry *e = &cache->entries[i];
            if (e->space == space && e->space_instance_id == inst &&
                e->entry_head == entry_head && e->language_id == language_id &&
                e->presence_only == presence_only && e->terms &&
                !e->terms_need_reset && e->rule_stamp.active &&
                space_theory_head_set_clock_current(
                    space, e->rule_dep_heads, e->rule_dep_len, &e->rule_stamp)) {
                was_already_compiled = true;
                break;
            }
        }
    }
    TcalcSpaceCacheEntry *entry = tcalc_space_acquire_locked(
        cache, space, entry_head, language_id, presence_only,
        false, message, sizeof message);
    if (!entry && probe_slot)
        probe_slot->flags |= TCALC_PROBE_DECLINED;
    if (tcalc_entry_auto_eligible(entry, language_id, observation)) {
        if (was_already_compiled)
            entry->hit_count++;
        if (tcalc_space_refresh_data_facts(entry, space, message, sizeof message)) {
            TcTerm call_tc = 0u;
            if (tcalc_ground_atom_to_tc(entry->terms, entry->bool_sym,
                                        entry->float_sym, ready_call, 0u,
                                        &call_tc, message, sizeof message)) {
                handled = tcalc_space_exec_slg(entry, call_tc, ready_call,
                                               language_id, presence_only, a, out);
            }
        }
    }
    pthread_mutex_unlock(&cache->lock);
    g_tcalc_space_eval_depth--;
    return handled;
}

static Atom *tcalc_space_build_program_atom(const TcalcSpaceCacheEntry *entry,
                                            CettaLanguageId language_id,
                                            Arena *a) {
    size_t total = entry->rule_len + 2u;
    Atom **forms = malloc(total * sizeof(*forms));
    if (!forms) return NULL;
    Atom *store_pair[2] = {
        atom_symbol(a, "store"),
        atom_symbol(a, entry->presence_only ? "set" : "multiset"),
    };
    Atom *step_pair[2] = {atom_symbol(a, "step"), atom_symbol(a, "every")};
    forms[0] = atom_expr(a, store_pair, 2u);
    forms[1] = atom_expr(a, step_pair, 2u);
    bool ok = forms[0] && forms[1];
    for (size_t i = 0u; ok && i < entry->rule_len; i++) {
        const TcalcSpaceCompiledRule *r = &entry->rules[i];
        Atom *read_items[3];
        read_items[0] = atom_symbol(a, "read");
        for (uint32_t j = 0u; j < r->reads; j++)
            read_items[1u + j] = tcalc_tc_to_atom(entry->terms, entry->bool_sym,
                                                  entry->float_sym, language_id,
                                                  r->read[j], a, 0u);
        Atom *read_form = atom_expr(a, read_items, (CettaExprLen)(1u + r->reads));
        Atom *make_items[3];
        make_items[0] = atom_symbol(a, "make");
        bool makes_ok = true;
        for (uint32_t j = 0u; j < r->makes; j++) {
            make_items[1u + j] = tcalc_tc_to_atom(entry->terms, entry->bool_sym,
                                                  entry->float_sym, language_id,
                                                  r->make[j], a, 0u);
            if (!make_items[1u + j]) makes_ok = false;
        }
        Atom *make_form = makes_ok
            ? atom_expr(a, make_items, (CettaExprLen)(1u + r->makes)) : NULL;
        Atom *rule_items[4] = {
            atom_symbol(a, "rule"),
            atom_symbol(a, r->name),
            read_form,
            make_form,
        };
        forms[2u + i] = (read_form && make_form) ? atom_expr(a, rule_items, 4u) : NULL;
        ok = forms[2u + i] != NULL;
    }
    Atom *res = ok ? atom_expr(a, forms, (CettaExprLen)total) : NULL;
    free(forms);
    return res;
}

static void tcalc_eval_interpreter_fallback(Space *target, Arena *a, Atom *call,
                                            CettaLanguageId language_id,
                                            CettaCallOutcome *out) {
    EvalOutcome eo;
    eval_outcome_init(&eo);
    Registry *reg = eval_current_registry ? eval_current_registry() : NULL;
    Arena *persistent = eval_current_persistent_arena();
    if (!persistent) persistent = a;
    g_tcalc_space_eval_depth++;
    eval_top_with_registry_outcome(target, a, persistent, reg, call, &eo,
                                   NULL, NULL);
    g_tcalc_space_eval_depth--;
    if (language_id == CETTA_LANGUAGE_PETTA && eo.petta_raised_error &&
        eo.results.len > 0u) {
        *out = cetta_call_raised(eo.results.items[0]);
    } else if (eo.completion != CETTA_EVAL_COMPLETE) {
        *out = cetta_call_interrupted();
    } else if (eo.results.len == 0u) {
        *out = cetta_call_failure();
    } else if (eo.results.len == 1u) {
        *out = cetta_call_completed_value(eo.results.items[0]);
    } else {
        Atom *answers = atom_expr_builder_begin(a, (CettaExprLen)eo.results.len);
        bool copied = answers != NULL;
        for (CettaCount i = 0u; copied && i < eo.results.len; i++)
            answers->expr.elems[i] = eo.results.items[i];
        answers = copied ? atom_expr_builder_finish(a, answers) : NULL;
        *out = answers ? cetta_call_completed_frontier(answers)
                       : cetta_call_interrupted();
    }
    eval_outcome_free(&eo);
}

typedef enum {
    OP_LOAD,
    OP_OUTCOMES,
    OP_STEP,
    OP_QUERY,
    OP_SPACE_EVAL,
    OP_SPACE_PROGRAM,
    OP_SPACE_CACHE_STATS
} Operation;

bool cetta_tcalc_module_call(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                             Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    const char *name = atom_name_cstr(head);
    if (strncmp(name, TCALC_HEAD, strlen(TCALC_HEAD)) != 0) return false;
    name += strlen(TCALC_HEAD);
    Operation op;
    uint32_t arity;
    if (strcmp(name, "load") == 0) op = OP_LOAD, arity = 1;
    else if (strcmp(name, "outcomes") == 0) op = OP_OUTCOMES, arity = 2;
    else if (strcmp(name, "step") == 0) op = OP_STEP, arity = 3;
    else if (strcmp(name, "query") == 0) op = OP_QUERY, arity = 3;
    else if (strcmp(name, "space_eval") == 0) op = OP_SPACE_EVAL, arity = 2;
    else if (strcmp(name, "space_program") == 0) op = OP_SPACE_PROGRAM, arity = 2;
    else if (strcmp(name, "space_cache_stats") == 0) op = OP_SPACE_CACHE_STATS, arity = 2;
    else return false;

    char message[512] = {0};
    if (nargs != arity) {
        snprintf(message, sizeof message, "tcalc:%s takes %u arguments", name, arity);
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    if (op == OP_SPACE_EVAL || op == OP_SPACE_PROGRAM || op == OP_SPACE_CACHE_STATS) {
        Space *target = tcalc_resolve_space_arg(space, args[0]);
        if (!target) {
            snprintf(message, sizeof message, "first argument must be a space");
            *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
            return true;
        }
        TcalcSpaceCache *cache = tcalc_space_cache_get(ctx);
        if (!cache) {
            snprintf(message, sizeof message, "missing library context");
            *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
            return true;
        }
        CettaLanguageId language_id = ctx->session.language_id;
        if (op == OP_SPACE_CACHE_STATS) {
            if (!args[1] || args[1]->kind != ATOM_SYMBOL) {
                snprintf(message, sizeof message, "second argument must be a head symbol");
                *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
                return true;
            }
            SymbolId eh = args[1]->sym_id;
            uint64_t inst = space_instance_id(target);
            uint64_t compiles = 0u, hits = 0u;
            SymbolId *deps_copy = NULL;
            uint32_t deps_len = 0u;
            pthread_mutex_lock(&cache->lock);
            for (size_t i = 0u; i < cache->len; i++) {
                TcalcSpaceCacheEntry *e = &cache->entries[i];
                if (e->space == target && e->space_instance_id == inst &&
                    e->entry_head == eh && e->language_id == language_id) {
                    compiles += e->compile_count;
                    hits += e->hit_count;
                    if (!deps_copy && e->rule_dep_len > 0u) {
                        deps_copy = malloc((size_t)e->rule_dep_len * sizeof(*deps_copy));
                        if (deps_copy) {
                            memcpy(deps_copy, e->rule_dep_heads,
                                   (size_t)e->rule_dep_len * sizeof(*deps_copy));
                            deps_len = e->rule_dep_len;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&cache->lock);
            Atom **dep_atoms = deps_len ? malloc((size_t)deps_len * sizeof(*dep_atoms)) : NULL;
            for (uint32_t i = 0u; dep_atoms && i < deps_len; i++)
                dep_atoms[i] = atom_symbol_id(a, deps_copy[i]);
            Atom *deps_list = atom_expr(a, dep_atoms, dep_atoms ? (CettaExprLen)deps_len : 0u);
            free(dep_atoms);
            free(deps_copy);
            Atom *c_pair[2] = {atom_symbol(a, "compiles"), atom_int(a, (int64_t)compiles)};
            Atom *h_pair[2] = {atom_symbol(a, "hits"), atom_int(a, (int64_t)hits)};
            Atom *d_pair[2] = {atom_symbol(a, "deps"), deps_list};
            Atom *stat_items[4] = {
                atom_symbol(a, "tcalc-cache-stats"),
                atom_expr(a, c_pair, 2u),
                atom_expr(a, h_pair, 2u),
                atom_expr(a, d_pair, 2u),
            };
            *out = cetta_call_completed_value(atom_expr(a, stat_items, 4u));
            return true;
        }
        if (op == OP_SPACE_PROGRAM) {
            if (!args[1] || args[1]->kind != ATOM_SYMBOL) {
                snprintf(message, sizeof message, "second argument must be a head symbol");
                *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
                return true;
            }
            pthread_mutex_lock(&cache->lock);
            TcalcSpaceCacheEntry *entry = tcalc_space_acquire_locked(
                cache, target, args[1]->sym_id, language_id, false, false,
                message, sizeof message);
            Atom *prog_atom = entry ? tcalc_space_build_program_atom(entry, language_id, a) : NULL;
            pthread_mutex_unlock(&cache->lock);
            if (prog_atom) {
                *out = cetta_call_completed_value(prog_atom);
            } else {
                Atom *nc[3] = {
                    atom_symbol(a, "tcalc-not-compiled"),
                    args[1],
                    atom_string(a, message[0] ? message : "outside the compiled fragment"),
                };
                *out = cetta_call_completed_value(atom_expr(a, nc, 3u));
            }
            return true;
        }
        /* OP_SPACE_EVAL: exact multiset compiled route with seamless fallback
         * to CeTTa's interpreter whenever the call is undefined or outside the
         * compiled fragment (never an Error for unsupported features). */
        Atom *call = args[1];
        if (call && call->kind == ATOM_EXPR && call->expr.len == 2u &&
            atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.quote))
            call = call->expr.elems[1];
        if (call && cetta_tcalc_space_try_eval(
                ctx, target, a, call, language_id,
                CETTA_TCALC_OBSERVE_MULTISET, out)) {
            return true;
        }
        tcalc_eval_interpreter_fallback(target, a, call, language_id, out);
        return true;
    }
    if (op == OP_LOAD) {
        Atom *result = NULL;
        if (!is_string(args[0])) {
            snprintf(message, sizeof message, "tcalc:load takes the path of a program as a string");
        } else {
            char *text = load_text(args[0]->ground.sval, message, sizeof message);
            TcTerms *terms = text ? tc_terms_new() : NULL;
            TcDeclarations d;
            TcSyntaxError e;
            TcProgram *p = terms ? tc_read_program(terms, text, strlen(text), &d, &e) : NULL;
            if (p) {
                free(d.region);
                result = output_atoms(a, text);
            } else if (text) {
                snprintf(message, sizeof message, "error: %zu:%zu: %s", e.line, e.column, e.message);
            }
            tc_program_free(p);
            tc_terms_free(terms);
            free(text);
        }
        *out = cetta_call_completed_value(result ? result : refusal(a, head, args, nargs, message));
        return true;
    }

    /* the program and the profile, as the runner reads them */
    if (args[0]->kind != ATOM_EXPR || !is_string(args[1])) {
        snprintf(message, sizeof message,
                 "tcalc:%s takes a program, an expression of its forms, and a profile string", name);
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    if (op == OP_STEP && args[2]->kind != ATOM_EXPR) {
        snprintf(message, sizeof message, "tcalc:step takes a state, an expression of rows");
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    size_t program_length = 0, third_length = 0;
    char *program = lines_of(args[0], false, &program_length);
    char *third = op == OP_STEP ? lines_of(args[2], true, &third_length)
                : op == OP_QUERY ? text_of(args[2], &third_length) : NULL;
    const char *profile = args[1]->ground.sval;
    char *result_text = NULL, *error_text = NULL;
    size_t result_length = 0, error_length = 0;
    FILE *result = open_memstream(&result_text, &result_length);
    FILE *error = open_memstream(&error_text, &error_length);
    int status = 1;
    if (program && result && error && (op == OP_OUTCOMES || third)) {
        if (op == OP_OUTCOMES)
            status = tcalc_run_text(program, program_length, "tcalc", profile, NULL, result, error);
        else if (op == OP_STEP)
            status = tcalc_step_text(program, program_length, third, third_length, profile, result, error);
        else
            status = tcalc_query_text(program, program_length, third, third_length, profile, result, error);
    }
    if (result) fclose(result);
    if (error) fclose(error);
    Atom *value = status == 0 && result_text ? output_atoms(a, result_text) : NULL;
    if (!value)
        value = refusal(a, head, args, nargs,
                        error_text && error_text[0] ? error_text : "error: out of memory");
    free(program); free(third); free(result_text); free(error_text);
    *out = cetta_call_completed_value(value);
    return true;
}
