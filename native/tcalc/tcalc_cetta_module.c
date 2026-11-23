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
#include "grounded.h"
#include "library.h"
#include "parser.h"
#include "space_theory.h"

#include <ctype.h>
#include <inttypes.h>
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
 * demand-driven `(store set) (step every)` rules matching lib/tcalc_modules.metta:
 *   (demand (f args...))          call asked
 *   (value (f args...) result)    call result
 *   (__space_data (rel args...))  ground data atom for a queried relation head
 *   (rK/i lhs t1 ... ti)          stage i of definition K
 * Every compiled program is cached per (space, entry_head) and guarded by
 * SpaceHeadSetStamp version counters over every consulted head symbol (defined
 * function heads, inert constructor heads, and queried data relation heads). */

#define TCALC_SPACE_CACHE_BOUND 64u
#define TCALC_SPACE_MAX_DEPTH 128u
#define TCALC_SPACE_MAX_HEADS 512u
#define TCALC_SPACE_MAX_LOCALS 512u
#define TCALC_SPACE_BUDGET UINT64_C(2000000)

typedef struct {
    char *name;
    TcTerm read[2];
    uint32_t reads;
    TcTerm make[1];
    uint32_t makes;
    uint32_t locals;
} TcalcSpaceCompiledRule;

typedef struct {
    Space *space;
    uint64_t space_instance_id;
    SymbolId entry_head;
    SymbolId *rule_dep_heads;
    uint32_t rule_dep_len;
    SpaceHeadSetStamp rule_stamp;
    SymbolId *data_dep_heads;
    uint32_t data_dep_len;
    SpaceHeadSetStamp data_stamp;
    TcTerms *terms;
    TcTerm demand_sym;
    TcTerm value_sym;
    TcTerm data_sym;
    TcalcSpaceCompiledRule *rules;
    size_t rule_len;
    size_t rule_cap;
    TcTerm *data_facts;
    size_t data_fact_len;
    bool data_facts_ready;
    uint64_t compile_count;
    uint64_t hit_count;
} TcalcSpaceCacheEntry;

typedef struct {
    pthread_mutex_t lock;
    TcalcSpaceCacheEntry *entries;
    size_t len;
    size_t cap;
} TcalcSpaceCache;

static void tcalc_space_entry_release_compiled(TcalcSpaceCacheEntry *entry) {
    if (!entry) return;
    free(entry->rule_dep_heads);
    entry->rule_dep_heads = NULL;
    entry->rule_dep_len = 0u;
    entry->rule_stamp = (SpaceHeadSetStamp){0};
    free(entry->data_dep_heads);
    entry->data_dep_heads = NULL;
    entry->data_dep_len = 0u;
    entry->data_stamp = (SpaceHeadSetStamp){0};
    for (size_t i = 0u; i < entry->rule_len; i++)
        free(entry->rules[i].name);
    free(entry->rules);
    entry->rules = NULL;
    entry->rule_len = entry->rule_cap = 0u;
    free(entry->data_facts);
    entry->data_facts = NULL;
    entry->data_fact_len = 0u;
    entry->data_facts_ready = false;
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

static bool tcalc_space_head_is_reserved_op(SymbolId sym) {
    if (sym == SYMBOL_ID_NONE) return true;
    if (is_grounded_op(sym)) return true;
    if (symbol_id_is_builtin(sym)) return true;
    return false;
}

static bool tcalc_space_head_has_nonstrict_type(Space *space, SymbolId head) {
    SpaceTheoryView view = space_theory_view_open(space);
    if (!space_theory_view_head_declares_type(&view, head))
        return false;
    Arena scratch;
    arena_init_detached(&scratch);
    Atom *head_atom = atom_symbol_id(&scratch, head);
    Atom **types = NULL;
    uint32_t count = head_atom
        ? space_get_declared_types(space, &scratch, head_atom, &types)
        : 0u;
    bool nonstrict = false;
    for (uint32_t i = 0u; i < count && !nonstrict; i++) {
        Atom *ty = types[i];
        if (!ty || ty->kind != ATOM_EXPR || ty->expr.len < 2u ||
            !atom_is_symbol_id(ty->expr.elems[0], g_builtin_syms.arrow))
            continue;
        /* Every parameter position (1 .. len - 2) must be strict. */
        for (CettaExprLen p = 1u; p + 1u < ty->expr.len; p++) {
            Atom *param = ty->expr.elems[p];
            if (param && param->kind == ATOM_SYMBOL &&
                (param->sym_id == g_builtin_syms.atom ||
                 param->sym_id == g_builtin_syms.expression)) {
                nonstrict = true;
                break;
            }
        }
    }
    free(types);
    arena_free(&scratch);
    return nonstrict;
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

/* Convert a ground CeTTa Atom to a TcTerm. */
static bool tcalc_ground_atom_to_tc(TcTerms *terms, const Atom *atom,
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
        if (atom->ground.gkind == GV_STRING) {
            *out = tc_string(terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            const char *s = atom->ground.bval ? "True" : "False";
            *out = tc_symbol(terms, s, strlen(s));
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
            ok = tcalc_ground_atom_to_tc(terms, atom->expr.elems[i],
                                         depth + 1u, &items[i], err, err_cap);
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

static Atom *tcalc_tc_to_atom(const TcTerms *terms, TcTerm t, Arena *a,
                              uint32_t depth) {
    if (!t || depth > TCALC_SPACE_MAX_DEPTH) return NULL;
    switch (tc_kind(terms, t)) {
    case TC_SYMBOL: {
        size_t len = 0u;
        const char *bytes = tc_bytes(terms, t, &len);
        if (!bytes) return NULL;
        if (len == 4u && memcmp(bytes, "True", 4u) == 0)
            return atom_bool(a, true);
        if (len == 5u && memcmp(bytes, "False", 5u) == 0)
            return atom_bool(a, false);
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
        Atom *stack_elems[16] = {0};
        Atom **elems = n <= 16u ? stack_elems : malloc((size_t)(n ? n : 1u) * sizeof(*elems));
        if (!elems) return NULL;
        bool ok = true;
        for (uint32_t i = 0u; ok && i < n; i++) {
            elems[i] = tcalc_tc_to_atom(terms, tc_item(terms, t, i), a, depth + 1u);
            if (!elems[i]) ok = false;
        }
        Atom *res = ok ? atom_expr(a, elems, (CettaExprLen)n) : NULL;
        if (elems != stack_elems) free(elems);
        return res;
    }
    }
    return NULL;
}

typedef enum {
    TCALC_PREM_CALL = 0,
    TCALC_PREM_QUERY = 1,
} TcalcSpacePremiseKind;

typedef struct {
    TcalcSpacePremiseKind kind;
    TcTerm primary; /* Call: call term; Query: data pattern term */
    TcTerm target;  /* Call: target pattern/local */
} TcalcSpacePremise;

typedef struct {
    VarId var_id;
    TcTerm bound_term;
    bool has_subst;
    uint32_t local_idx;
} TcalcVarSlot;

typedef struct {
    Space *space;
    TcTerms *terms;
    SymbolId *defined_heads;
    uint32_t defined_len;
    SymbolId **rule_deps;
    uint32_t *rule_dep_len;
    uint32_t *rule_dep_cap;
    SymbolId **data_deps;
    uint32_t *data_dep_len;
    uint32_t *data_dep_cap;
    TcalcVarSlot vars[TCALC_SPACE_MAX_LOCALS];
    uint32_t var_len;
    uint32_t next_local;
    TcalcSpacePremise *prems;
    size_t prem_len;
    size_t prem_cap;
    char *err;
    size_t err_cap;
} TcalcEqLower;

static bool tcalc_lower_push_prem(TcalcEqLower *L, TcalcSpacePremise p) {
    if (L->prem_len == L->prem_cap) {
        size_t next = L->prem_cap ? L->prem_cap * 2u : 8u;
        TcalcSpacePremise *grown = realloc(L->prems, next * sizeof(*grown));
        if (!grown) {
            snprintf(L->err, L->err_cap, "out of memory");
            return false;
        }
        L->prems = grown;
        L->prem_cap = next;
    }
    L->prems[L->prem_len++] = p;
    return true;
}

static bool tcalc_lower_var_local(TcalcEqLower *L, VarId id, bool allow_subst,
                                  TcTerm *out) {
    for (uint32_t i = 0u; i < L->var_len; i++) {
        if (var_id_eq(L->vars[i].var_id, id)) {
            if (allow_subst && L->vars[i].has_subst) {
                *out = L->vars[i].bound_term;
                return true;
            }
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
        .bound_term = 0u,
        .has_subst = false,
        .local_idx = loc,
    };
    *out = tc_local(L->terms, loc);
    return *out != 0u;
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
        return tcalc_lower_var_local(L, atom->var_id, false, out);
    case ATOM_SYMBOL:
        *out = tcalc_intern_symbol(L->terms, atom->sym_id);
        return *out != 0u;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            *out = tc_integer(L->terms, atom->ground.ival);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_STRING) {
            *out = tc_string(L->terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            const char *s = atom->ground.bval ? "True" : "False";
            *out = tc_symbol(L->terms, s, strlen(s));
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
        if (!head || head->kind != ATOM_SYMBOL) {
            snprintf(L->err, L->err_cap, "pattern expression must start with a head symbol");
            return false;
        }
        if (!is_lhs_root) {
            if (tcalc_sym_set_contains(L->defined_heads, L->defined_len, head->sym_id) ||
                space_equations_may_match_known_head(L->space, head->sym_id)) {
                snprintf(L->err, L->err_cap,
                         "defined function head '%s' inside pattern is not supported",
                         symbol_bytes(g_symbols, head->sym_id));
                return false;
            }
            if (tcalc_space_head_is_reserved_op(head->sym_id)) {
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
        items[0] = tcalc_intern_symbol(L->terms, head->sym_id);
        bool ok = items[0] != 0u;
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

/* Flatten an RHS expression call-by-value (innermost first, left to right),
 * emitting Call and Query premises and returning the resulting value TcTerm. */
static bool tcalc_lower_rhs(TcalcEqLower *L, const Atom *atom,
                            uint32_t depth, TcTerm *out) {
    if (!atom || depth > TCALC_SPACE_MAX_DEPTH) {
        snprintf(L->err, L->err_cap, "expression exceeds depth limit");
        return false;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        return tcalc_lower_var_local(L, atom->var_id, true, out);
    case ATOM_SYMBOL:
        *out = tcalc_intern_symbol(L->terms, atom->sym_id);
        return *out != 0u;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            *out = tc_integer(L->terms, atom->ground.ival);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_STRING) {
            *out = tc_string(L->terms, atom->ground.sval, atom->ground.slen);
            return *out != 0u;
        }
        if (atom->ground.gkind == GV_BOOL) {
            const char *s = atom->ground.bval ? "True" : "False";
            *out = tc_symbol(L->terms, s, strlen(s));
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
        if (!head || head->kind != ATOM_SYMBOL) {
            snprintf(L->err, L->err_cap,
                     "higher-order or non-symbol application is outside the tcalc fragment");
            return false;
        }
        SymbolId sym = head->sym_id;
        /* (let PAT EXPR BODY) */
        if (sym == g_builtin_syms.let && atom->expr.len == 4u) {
            const Atom *pat = atom->expr.elems[1];
            const Atom *expr = atom->expr.elems[2];
            const Atom *body = atom->expr.elems[3];
            /* If EXPR is a direct call to a defined function, lower its args
             * and bind the Call premise directly to PAT. */
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
                if (!tcalc_lower_push_prem(L, (TcalcSpacePremise){
                        .kind = TCALC_PREM_CALL, .primary = call_tc, .target = pat_tc}))
                    return false;
                return tcalc_lower_rhs(L, body, depth + 1u, out);
            }
            /* Otherwise flatten EXPR and substitute if PAT is a fresh variable. */
            TcTerm val_tc = 0u;
            if (!tcalc_lower_rhs(L, expr, depth + 1u, &val_tc))
                return false;
            if (!pat || pat->kind != ATOM_VAR) {
                snprintf(L->err, L->err_cap,
                         "let with non-variable pattern over a non-call value is outside the tcalc fragment");
                return false;
            }
            /* Reject shadowing an already-seen variable in the same definition. */
            for (uint32_t i = 0u; i < L->var_len; i++) {
                if (var_id_eq(L->vars[i].var_id, pat->var_id)) {
                    snprintf(L->err, L->err_cap,
                             "let rebinding an existing variable is outside the tcalc fragment");
                    return false;
                }
            }
            if (L->var_len >= TCALC_SPACE_MAX_LOCALS) {
                snprintf(L->err, L->err_cap, "too many variables in function definition");
                return false;
            }
            L->vars[L->var_len++] = (TcalcVarSlot){
                .var_id = pat->var_id,
                .bound_term = val_tc,
                .has_subst = true,
                .local_idx = 0u,
            };
            return tcalc_lower_rhs(L, body, depth + 1u, out);
        }
        /* (match &self (rel a1 ... am) BODY) */
        if (sym == g_builtin_syms.match && atom->expr.len == 4u) {
            const Atom *sp_arg = atom->expr.elems[1];
            const Atom *pat = atom->expr.elems[2];
            const Atom *body = atom->expr.elems[3];
            bool self_space = (sp_arg && sp_arg->kind == ATOM_SYMBOL &&
                               sp_arg->sym_id == g_builtin_syms.self) ||
                              (sp_arg && sp_arg->kind == ATOM_GROUNDED &&
                               sp_arg->ground.gkind == GV_SPACE &&
                               (Space *)sp_arg->ground.ptr == L->space);
            if (!self_space) {
                snprintf(L->err, L->err_cap,
                         "match against a space other than the compiled space is outside the tcalc fragment");
                return false;
            }
            if (!pat || pat->kind != ATOM_EXPR || pat->expr.len == 0u ||
                pat->expr.elems[0]->kind != ATOM_SYMBOL) {
                snprintf(L->err, L->err_cap,
                         "match pattern must be an expression starting with a head symbol");
                return false;
            }
            SymbolId rel = pat->expr.elems[0]->sym_id;
            if (tcalc_sym_set_contains(L->defined_heads, L->defined_len, rel) ||
                space_equations_may_match_known_head(L->space, rel) ||
                tcalc_space_head_is_reserved_op(rel)) {
                snprintf(L->err, L->err_cap,
                         "match relation head '%s' must be a pure data head symbol",
                         symbol_bytes(g_symbols, rel));
                return false;
            }
            if (!tcalc_sym_set_add(L->rule_deps, L->rule_dep_len, L->rule_dep_cap, rel) ||
                !tcalc_sym_set_add(L->data_deps, L->data_dep_len, L->data_dep_cap, rel)) {
                snprintf(L->err, L->err_cap, "too many dependency head symbols");
                return false;
            }
            /* Allow variables or pure expressions inside the data query pattern. */
            TcTerm pat_tc = 0u;
            if (!tcalc_lower_pattern(L, pat, true, depth + 1u, &pat_tc))
                return false;
            if (!tcalc_lower_push_prem(L, (TcalcSpacePremise){
                    .kind = TCALC_PREM_QUERY, .primary = pat_tc, .target = 0u}))
                return false;
            return tcalc_lower_rhs(L, body, depth + 1u, out);
        }

        bool is_defined = tcalc_sym_set_contains(L->defined_heads, L->defined_len, sym);
        if (!is_defined && tcalc_space_head_is_reserved_op(sym)) {
            snprintf(L->err, L->err_cap,
                     "builtin or grounded operation '%s' is outside the pure symbolic tcalc fragment",
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
            if (!tcalc_lower_push_prem(L, (TcalcSpacePremise){
                    .kind = TCALC_PREM_CALL, .primary = built, .target = fresh}))
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

static void tcalc_mark_locals(const TcTerms *terms, TcTerm t, bool *bound,
                              uint32_t max_locals) {
    if (!t) return;
    TcKind k = tc_kind(terms, t);
    if (k == TC_LOCAL) {
        uint32_t idx = tc_index(terms, t);
        if (idx < max_locals) bound[idx] = true;
        return;
    }
    if (k == TC_EXPRESSION) {
        uint32_t n = tc_length(terms, t);
        for (uint32_t i = 0u; i < n; i++)
            tcalc_mark_locals(terms, tc_item(terms, t, i), bound, max_locals);
    }
}

static bool tcalc_all_locals_bound(const TcTerms *terms, TcTerm t,
                                   const bool *bound, uint32_t max_locals) {
    if (!t) return true;
    TcKind k = tc_kind(terms, t);
    if (k == TC_LOCAL) {
        uint32_t idx = tc_index(terms, t);
        return idx < max_locals && bound[idx];
    }
    if (k == TC_EXPRESSION) {
        uint32_t n = tc_length(terms, t);
        for (uint32_t i = 0u; i < n; i++) {
            if (!tcalc_all_locals_bound(terms, tc_item(terms, t, i), bound, max_locals))
                return false;
        }
    }
    return true;
}

static bool tcalc_entry_push_rule(TcalcSpaceCacheEntry *entry, const char *name,
                                  const TcTerm *reads, uint32_t nreads,
                                  TcTerm make_term, uint32_t locals) {
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
        .makes = 1u,
        .locals = locals,
    };
    for (uint32_t i = 0u; i < nreads && i < 2u; i++)
        r.read[i] = reads[i];
    r.make[0] = make_term;
    entry->rules[entry->rule_len++] = r;
    return true;
}

/* Discover all head symbols with function definitions reachable from ENTRY_HEAD. */
static bool tcalc_collect_reachable_heads(
        Space *space, const SpaceTheoryView *view, SymbolId entry_head,
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
            snprintf(err, err_cap, "failed to read function definitions for head '%s'",
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
        if (tcalc_space_head_has_nonstrict_type(space, h)) {
            free(eqs);
            free(queue);
            snprintf(err, err_cap,
                     "head symbol '%s' has a non-strict type assignment",
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
                    if (cs == g_builtin_syms.match && cur->expr.len == 4u) {
                        /* Only push match body, not the data pattern head. */
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
                    if (space_equations_may_match_known_head(space, cs)) {
                        if (!tcalc_sym_set_add(&queue, &qlen, &qcap, cs)) {
                            free(eqs);
                            free(queue);
                            snprintf(err, err_cap, "too many reachable function heads");
                            return false;
                        }
                    }
                }
                for (CettaExprLen i = 1u; i < cur->expr.len && sp < 256u; i++)
                    stack[sp++] = cur->expr.elems[i];
            }
        }
        free(eqs);
    }
    *defined_out = queue;
    *defined_len_out = qlen;
    return true;
}

static bool tcalc_space_refresh_data_facts(TcalcSpaceCacheEntry *entry,
                                           Space *space, char *err,
                                           size_t err_cap) {
    if (entry->data_dep_len == 0u) {
        entry->data_facts_ready = true;
        return true;
    }
    if (entry->data_facts_ready && entry->data_stamp.active &&
        space_theory_head_set_clock_current(
            space, entry->data_dep_heads, entry->data_dep_len,
            &entry->data_stamp)) {
        return true;
    }
    TcTerm *facts = NULL;
    size_t flen = 0u, fcap = 0u;
    CettaCount total = space_view_length64(space);
    for (CettaIndex i = 0u; i < total; i++) {
        Atom *atom = space_view_get_at64(space, i);
        if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
            atom->expr.elems[0]->kind != ATOM_SYMBOL)
            continue;
        if (space_theory_classify_atom(atom, NULL, NULL) != SPACE_THEORY_ATOM_DATA)
            continue;
        SymbolId h = atom->expr.elems[0]->sym_id;
        if (!tcalc_sym_set_contains(entry->data_dep_heads, entry->data_dep_len, h))
            continue;
        TcTerm atom_tc = 0u;
        if (!tcalc_ground_atom_to_tc(entry->terms, atom, 0u, &atom_tc, err, err_cap)) {
            free(facts);
            return false;
        }
        TcTerm wrapped = tcalc_wrap1(entry->terms, entry->data_sym, atom_tc);
        if (!wrapped) {
            free(facts);
            snprintf(err, err_cap, "out of memory");
            return false;
        }
        if (flen == fcap) {
            size_t next = fcap ? fcap * 2u : 16u;
            TcTerm *grown = realloc(facts, next * sizeof(*grown));
            if (!grown) {
                free(facts);
                snprintf(err, err_cap, "out of memory");
                return false;
            }
            facts = grown;
            fcap = next;
        }
        facts[flen++] = wrapped;
    }
    SpaceHeadSetStamp stamp = {0};
    if (!space_theory_head_set_clock_capture(
            space, entry->data_dep_heads, entry->data_dep_len,
            SPACE_THEORY_ROLE_DATA | SPACE_THEORY_ROLE_ALIAS, &stamp)) {
        free(facts);
        snprintf(err, err_cap, "failed to capture data head version counters");
        return false;
    }
    free(entry->data_facts);
    entry->data_facts = facts;
    entry->data_fact_len = flen;
    entry->data_stamp = stamp;
    entry->data_facts_ready = true;
    return true;
}

static bool tcalc_space_compile_into(TcalcSpaceCacheEntry *entry,
                                     Space *space, SymbolId entry_head,
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

    SpaceTheoryView view = space_theory_view_open(space);
    SymbolId *defined = NULL;
    uint32_t defined_len = 0u;
    if (!tcalc_collect_reachable_heads(space, &view, entry_head,
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

    SymbolId *rule_deps = NULL;
    uint32_t rule_dep_len = 0u, rule_dep_cap = 0u;
    SymbolId *data_deps = NULL;
    uint32_t data_dep_len = 0u, data_dep_cap = 0u;
    bool ok = entry->demand_sym && entry->value_sym && entry->data_sym;
    for (uint32_t i = 0u; ok && i < defined_len; i++) {
        ok = tcalc_sym_set_add(&rule_deps, &rule_dep_len, &rule_dep_cap, defined[i]);
    }

    uint32_t rule_ordinal = 0u;
    for (uint32_t di = 0u; ok && di < defined_len; di++) {
        SymbolId h = defined[di];
        Atom **eqs = NULL;
        size_t eq_len = 0u;
        bool via_alias = false;
        if (!space_theory_view_collect_head_rules(
                &view, h, CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL,
                &eqs, &eq_len, &via_alias) || via_alias) {
            free(eqs);
            snprintf(err, err_cap, "failed to read definitions for '%s'",
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
                .terms = terms,
                .defined_heads = defined,
                .defined_len = defined_len,
                .rule_deps = &rule_deps,
                .rule_dep_len = &rule_dep_len,
                .rule_dep_cap = &rule_dep_cap,
                .data_deps = &data_deps,
                .data_dep_len = &data_dep_len,
                .data_dep_cap = &data_dep_cap,
                .err = err,
                .err_cap = err_cap,
            };
            TcTerm lhs_tc = 0u, rhs_tc = 0u;
            ok = tcalc_lower_pattern(&L, lhs, true, 0u, &lhs_tc) &&
                 tcalc_lower_rhs(&L, rhs, 0u, &rhs_tc);
            if (ok) {
                /* Verify every variable used in a demand or make is bound by reads. */
                bool bound[TCALC_SPACE_MAX_LOCALS];
                memset(bound, 0, sizeof(bound));
                tcalc_mark_locals(terms, lhs_tc, bound, L.next_local);
                for (size_t pi = 0u; ok && pi < L.prem_len; pi++) {
                    if (L.prems[pi].kind == TCALC_PREM_CALL) {
                        if (!tcalc_all_locals_bound(terms, L.prems[pi].primary,
                                                    bound, L.next_local)) {
                            snprintf(err, err_cap,
                                     "variable used before any read binds it");
                            ok = false;
                        } else {
                            tcalc_mark_locals(terms, L.prems[pi].target,
                                              bound, L.next_local);
                        }
                    } else {
                        tcalc_mark_locals(terms, L.prems[pi].primary,
                                          bound, L.next_local);
                    }
                }
                if (ok && !tcalc_all_locals_bound(terms, rhs_tc, bound, L.next_local)) {
                    snprintf(err, err_cap, "variable used before any read binds it");
                    ok = false;
                }
            }
            if (ok) {
                /* Emit demand-driven stage rules for this definition. */
                TcTerm prev = tcalc_wrap1(terms, entry->demand_sym, lhs_tc);
                TcTerm *stage_args = malloc((L.prem_len + 2u) * sizeof(*stage_args));
                if (!prev || !stage_args) {
                    free(stage_args);
                    snprintf(err, err_cap, "out of memory");
                    ok = false;
                } else {
                    size_t ts_len = 0u;
                    stage_args[1] = lhs_tc;
                    for (size_t pi = 0u; ok && pi < L.prem_len; pi++) {
                        uint32_t step = (uint32_t)(pi + 1u);
                        char rname[64];
                        char sname[64];
                        snprintf(sname, sizeof sname, "r%u/%u", rule_ordinal, step);
                        TcTerm stage_sym = tc_symbol(terms, sname, strlen(sname));
                        if (!stage_sym) { ok = false; break; }
                        if (L.prems[pi].kind == TCALC_PREM_CALL) {
                            snprintf(rname, sizeof rname, "r%u/%u.ask", rule_ordinal, step);
                            TcTerm ask_make = tcalc_wrap1(terms, entry->demand_sym,
                                                          L.prems[pi].primary);
                            TcTerm ask_read[1] = {prev};
                            ok = ask_make &&
                                 tcalc_entry_push_rule(entry, rname, ask_read, 1u,
                                                       ask_make, L.next_local);
                            stage_args[2u + ts_len++] = L.prems[pi].target;
                            stage_args[0] = stage_sym;
                            TcTerm next = tc_expression(terms, stage_args,
                                                        (uint32_t)(ts_len + 2u));
                            TcTerm val_pat = tcalc_wrap2(terms, entry->value_sym,
                                                         L.prems[pi].primary,
                                                         L.prems[pi].target);
                            TcTerm got_reads[2] = {prev, val_pat};
                            ok = ok && next && val_pat &&
                                 tcalc_entry_push_rule(entry, sname, got_reads, 2u,
                                                       next, L.next_local);
                            prev = next;
                        } else {
                            stage_args[2u + ts_len++] = L.prems[pi].primary;
                            stage_args[0] = stage_sym;
                            TcTerm next = tc_expression(terms, stage_args,
                                                        (uint32_t)(ts_len + 2u));
                            TcTerm data_pat = tcalc_wrap1(terms, entry->data_sym,
                                                          L.prems[pi].primary);
                            TcTerm got_reads[2] = {prev, data_pat};
                            ok = next && data_pat &&
                                 tcalc_entry_push_rule(entry, sname, got_reads, 2u,
                                                       next, L.next_local);
                            prev = next;
                        }
                    }
                    if (ok) {
                        char rname[64];
                        snprintf(rname, sizeof rname, "r%u", rule_ordinal);
                        TcTerm final_read[1] = {prev};
                        TcTerm final_make = tcalc_wrap2(terms, entry->value_sym,
                                                        lhs_tc, rhs_tc);
                        ok = final_make &&
                             tcalc_entry_push_rule(entry, rname, final_read, 1u,
                                                   final_make, L.next_local);
                    }
                    free(stage_args);
                }
            }
            free(L.prems);
            rule_ordinal++;
        }
        free(eqs);
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
        entry->data_dep_heads = data_deps;
        entry->data_dep_len = data_dep_len;
        data_deps = NULL;
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
        char *err, size_t err_cap) {
    uint64_t inst = space_instance_id(space);
    TcalcSpaceCacheEntry *slot = NULL;
    for (size_t i = 0u; i < cache->len; i++) {
        TcalcSpaceCacheEntry *e = &cache->entries[i];
        if (e->space == space && e->space_instance_id == inst &&
            e->entry_head == entry_head) {
            slot = e;
            break;
        }
    }
    if (slot && slot->terms && slot->rule_stamp.active &&
        space_theory_head_set_clock_current(
            space, slot->rule_dep_heads, slot->rule_dep_len, &slot->rule_stamp)) {
        slot->hit_count++;
        return slot;
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
    if (!tcalc_space_compile_into(slot, space, entry_head, err, err_cap))
        return NULL;
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

static Atom *tcalc_space_build_program_atom(const TcalcSpaceCacheEntry *entry,
                                            Arena *a) {
    size_t total = entry->rule_len + 2u;
    Atom **forms = malloc(total * sizeof(*forms));
    if (!forms) return NULL;
    Atom *store_pair[2] = {atom_symbol(a, "store"), atom_symbol(a, "set")};
    Atom *step_pair[2] = {atom_symbol(a, "step"), atom_symbol(a, "every")};
    forms[0] = atom_expr(a, store_pair, 2u);
    forms[1] = atom_expr(a, step_pair, 2u);
    bool ok = forms[0] && forms[1];
    for (size_t i = 0u; ok && i < entry->rule_len; i++) {
        const TcalcSpaceCompiledRule *r = &entry->rules[i];
        Atom *read_items[3];
        read_items[0] = atom_symbol(a, "read");
        for (uint32_t j = 0u; j < r->reads; j++)
            read_items[1u + j] = tcalc_tc_to_atom(entry->terms, r->read[j], a, 0u);
        Atom *read_form = atom_expr(a, read_items, (CettaExprLen)(1u + r->reads));
        Atom *make_items[2] = {
            atom_symbol(a, "make"),
            tcalc_tc_to_atom(entry->terms, r->make[0], a, 0u),
        };
        Atom *make_form = make_items[1] ? atom_expr(a, make_items, 2u) : NULL;
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
                    e->entry_head == eh) {
                    compiles = e->compile_count;
                    hits = e->hit_count;
                    if (e->rule_dep_len > 0u) {
                        deps_copy = malloc((size_t)e->rule_dep_len * sizeof(*deps_copy));
                        if (deps_copy) {
                            memcpy(deps_copy, e->rule_dep_heads,
                                   (size_t)e->rule_dep_len * sizeof(*deps_copy));
                            deps_len = e->rule_dep_len;
                        }
                    }
                    break;
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
                cache, target, args[1]->sym_id, message, sizeof message);
            Atom *prog_atom = entry ? tcalc_space_build_program_atom(entry, a) : NULL;
            pthread_mutex_unlock(&cache->lock);
            *out = cetta_call_completed_value(
                prog_atom ? prog_atom
                          : refusal(a, head, args, nargs,
                                    message[0] ? message : "error: out of memory"));
            return true;
        }
        /* OP_SPACE_EVAL */
        Atom *call = args[1];
        if (call && call->kind == ATOM_EXPR && call->expr.len == 2u &&
            atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.quote))
            call = call->expr.elems[1];
        if (!call || call->kind != ATOM_EXPR || call->expr.len == 0u ||
            call->expr.elems[0]->kind != ATOM_SYMBOL) {
            snprintf(message, sizeof message,
                     "call must be an expression starting with a head symbol");
            *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
            return true;
        }
        SymbolId entry_head = call->expr.elems[0]->sym_id;
        pthread_mutex_lock(&cache->lock);
        TcalcSpaceCacheEntry *entry = tcalc_space_acquire_locked(
            cache, target, entry_head, message, sizeof message);
        Atom *result = NULL;
        if (entry && tcalc_space_refresh_data_facts(entry, target, message, sizeof message)) {
            TcTerm call_tc = 0u;
            if (tcalc_ground_atom_to_tc(entry->terms, call, 0u, &call_tc,
                                        message, sizeof message)) {
                TcProgram *prog = tc_program_new(entry->terms);
                bool built = prog != NULL;
                for (size_t i = 0u; built && i < entry->rule_len; i++) {
                    const TcalcSpaceCompiledRule *r = &entry->rules[i];
                    TcRule tr = {
                        .name = r->name,
                        .read = r->read,
                        .reads = r->reads,
                        .make = r->make,
                        .makes = r->makes,
                        .locals = r->locals,
                    };
                    built = tc_program_add_rule(prog, &tr);
                }
                for (size_t i = 0u; built && i < entry->data_fact_len; i++)
                    built = tc_program_add_row(prog, entry->data_facts[i]);
                TcTerm demand = built ? tcalc_wrap1(entry->terms, entry->demand_sym, call_tc) : 0u;
                built = built && demand && tc_program_add_row(prog, demand);
                if (!built) {
                    snprintf(message, sizeof message, "failed to construct tcalc program");
                } else {
                    TcInterpretation how = {
                        .step = TC_STEP_EVERY,
                        .ask = TC_ASK_AFTER_TELLS,
                        .store = TC_STORE_SET,
                        .matches = TC_MATCHES_SYMMETRIC,
                        .budget = TCALC_SPACE_BUDGET,
                        .backend = TC_BACKEND_C,
                        .observe = TC_OBSERVE_OUTCOMES,
                        .shortcuts = TC_SHORTCUTS_TAKEN,
                    };
                    TcOutcomes *outcomes = tc_explore(prog, &how);
                    if (!outcomes || !tc_outcomes_finished(outcomes) ||
                        !tc_outcomes_complete_memory(outcomes) ||
                        tc_outcomes_count(outcomes) == 0u) {
                        snprintf(message, sizeof message,
                                 "tcalc evaluation did not complete within budget");
                    } else {
                        size_t nfacts = 0u;
                        const TcTerm *facts = tc_outcome_rows(outcomes, 0u, &nfacts);
                        Atom **vals = NULL;
                        size_t vlen = 0u, vcap = 0u;
                        bool ok = true;
                        for (size_t fi = 0u; ok && fi < nfacts; fi++) {
                            TcTerm item = facts[fi];
                            if (tc_kind(entry->terms, item) != TC_EXPRESSION ||
                                tc_length(entry->terms, item) != 3u)
                                continue;
                            if (tc_item(entry->terms, item, 0u) != entry->value_sym ||
                                tc_item(entry->terms, item, 1u) != call_tc)
                                continue;
                            Atom *v = tcalc_tc_to_atom(
                                entry->terms, tc_item(entry->terms, item, 2u), a, 0u);
                            if (!v) { ok = false; break; }
                            if (vlen == vcap) {
                                size_t next = vcap ? vcap * 2u : 8u;
                                Atom **grown = realloc(vals, next * sizeof(*grown));
                                if (!grown) { ok = false; break; }
                                vals = grown;
                                vcap = next;
                            }
                            vals[vlen++] = v;
                        }
                        if (ok)
                            result = atom_expr(a, vals, (CettaExprLen)vlen);
                        else
                            snprintf(message, sizeof message, "out of memory decoding tcalc result");
                        free(vals);
                    }
                    tc_outcomes_free(outcomes);
                }
                tc_program_free(prog);
            }
        }
        pthread_mutex_unlock(&cache->lock);
        *out = cetta_call_completed_value(
            result ? result
                   : refusal(a, head, args, nargs,
                             message[0] ? message : "error: out of memory"));
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
