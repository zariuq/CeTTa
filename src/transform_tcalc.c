#include "transform_tcalc.h"
#include "native/kernel/kernel.h"
#include "../native/tcalc/tcalc_core.h"
#include "petta_semantics.h"
#include "stats.h"
#include "term_canon.h"
#include "u64_index.h"
#include "var_index.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct TfTc TfTc;
typedef struct {
    TcTerms *terms;
    TcProgram *program;
    uint32_t *key;
    size_t key_length;
    uint32_t calls, maximum_term;
    bool busy, retire;
} TfTcCache;

static pthread_once_t tftc_cache_once = PTHREAD_ONCE_INIT;
static pthread_key_t tftc_cache_key;
static _Atomic bool tftc_cache_ready;

static void tftc_cache_flush(TfTcCache *cache) {
    tc_program_free(cache->program);
    tc_terms_free(cache->terms);
    free(cache->key);
    *cache = (TfTcCache){0};
}

static void tftc_cache_destroy(void *raw) {
    TfTcCache *cache = raw;
    if (!cache) return;
    if (cache->busy) { cache->retire = true; return; }
    tftc_cache_flush(cache);
    free(cache);
}

void cetta_transform_tcalc_cache_free_for_current_thread(void) {
    if (!atomic_load_explicit(&tftc_cache_ready, memory_order_acquire)) return;
    TfTcCache *cache = pthread_getspecific(tftc_cache_key);
    if (!cache) return;
    if (cache->busy) { cache->retire = true; return; }
    pthread_setspecific(tftc_cache_key, NULL);
    tftc_cache_destroy(cache);
}

static void tftc_cache_initialize(void) {
    if (pthread_key_create(&tftc_cache_key, tftc_cache_destroy) != 0) return;
    if (atexit(cetta_transform_tcalc_cache_free_for_current_thread) != 0) {
        pthread_key_delete(tftc_cache_key);
        return;
    }
    atomic_store_explicit(&tftc_cache_ready, true, memory_order_release);
}

static TfTcCache *tftc_cache_take(void) {
    const char *setting = getenv("CETTA_TRANSFORM_TCALC_CACHE");
    if (setting && strcmp(setting, "0") == 0) return NULL;
    pthread_once(&tftc_cache_once, tftc_cache_initialize);
    if (!atomic_load_explicit(&tftc_cache_ready, memory_order_acquire)) return NULL;
    TfTcCache *cache = pthread_getspecific(tftc_cache_key);
    if (!cache) {
        cache = calloc(1u, sizeof(*cache));
        if (!cache) return NULL;
        if (pthread_setspecific(tftc_cache_key, cache) != 0) { free(cache); return NULL; }
    }
    /* These evict code between calls, never limit an admitted input. A
     * reentrant firing uses an independent ephemeral table. */
    if (cache->busy) return NULL;
    if (cache->retire || cache->calls >= 256u || cache->maximum_term >= 32768u)
        tftc_cache_flush(cache);
    cache->busy = true;
    cache->calls++;
    return cache;
}
typedef struct {
    TfTc *owner;
    SpaceOccurrenceCursor rows;
    Space *space;
    uint32_t location;
    bool closed;
} TfTcCursor;

typedef struct {
    Space *space;
    TcTerm tag;
} TfTcLocation;

typedef struct {
    Space *space;
    SpaceReadToken token;
    CettaIndex position;
    Atom *atom;
    uint32_t location;
} TfTcRow;

typedef struct {
    Atom *atom;
    TcTerm term;
    uint32_t next;
} TfTcConstant;

typedef struct {
    Atom **atoms;
    TcTerm *terms;
    uint32_t length;
    size_t capacity;
    CettaVarIndex index;
} TfTcVariables;

struct TfTc {
    Arena *arena;
    TcTerms *terms;
    TfTcCache *cache;
    uint32_t maximum_term;
    const CettaTransformFiring *firing;
    TfTcConstant *constants;
    size_t constant_length, constant_capacity;
    CettaU64Index constant_hashes, constant_terms;
    TfTcLocation *locations;
    size_t location_length, location_capacity;
    TfTcCursor **cursors;
    size_t cursor_length, cursor_capacity;
    TfTcRow *rows;
    size_t row_length, row_capacity;
    TcOccurrence *selected;
    size_t takes;
    bool unsupported;
    bool no_memory;
    bool protocol_started;
    Atom *error;
    CettaTransformOutcome committed;
};

static bool tftc_grow(void **array, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity)
        return true;
    size_t count = *capacity ? *capacity : 8u;
    while (count < needed) {
        if (count > SIZE_MAX / 2u)
            return false;
        count *= 2u;
    }
    if (count > SIZE_MAX / width)
        return false;
    void *next = realloc(*array, count * width);
    if (!next)
        return false;
    *array = next; *capacity = count;
    return true;
}

/* Only the canonical first-order incidence diagram is cached. Atomic
 * values, variable identities, locations and all solver state are supplied
 * afresh. Exact comparison includes every constructor and equality edge. */
static TcProgram *tftc_program(TfTc *ctx, const TcRule *rule) {
    if (!ctx->cache) {
        TcProgram *program = tc_program_new(ctx->terms);
        if (program && tc_program_add_rule(program, rule)) return program;
        tc_program_free(program);
        return NULL;
    }
    size_t length = 6u + (size_t)rule->takes + rule->reads +
        2u * rule->tells + 5u * rule->checks;
    if (length > SIZE_MAX / sizeof(uint32_t)) return NULL;
    uint32_t *key = malloc(length * sizeof(*key));
    if (!key) return NULL;
    size_t at = 0u;
    key[at++] = rule->takes; key[at++] = rule->reads;
    key[at++] = rule->tells; key[at++] = rule->checks;
    key[at++] = rule->locals; key[at++] = rule->make[0];
    for (uint32_t i = 0u; i < rule->takes; i++) key[at++] = rule->take[i];
    for (uint32_t i = 0u; i < rule->reads; i++) key[at++] = rule->read[i];
    for (size_t i = 0u; i < 2u * (size_t)rule->tells; i++) key[at++] = rule->tell[i];
    for (uint32_t i = 0u; i < rule->checks; i++) {
        key[at++] = (uint32_t)rule->check[i].kind;
        key[at++] = rule->check[i].left; key[at++] = rule->check[i].right;
        key[at++] = (uint32_t)rule->check[i].count;
        key[at++] = (uint32_t)(rule->check[i].count >> 32u);
    }
    TfTcCache *cache = ctx->cache;
    if (cache->program && cache->key_length == length &&
        memcmp(cache->key, key, length * sizeof(*key)) == 0) {
        free(key);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_CODE_HIT);
        return cache->program;
    }
    TcProgram *program = tc_program_new(ctx->terms);
    if (!program || !tc_program_add_rule(program, rule)) {
        tc_program_free(program); free(key); return NULL;
    }
    tc_program_free(cache->program); free(cache->key);
    cache->program = program; cache->key = key; cache->key_length = length;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_CODE_MISS);
    return program;
}

static void tftc_variables_free(TfTcVariables *variables) {
    free(variables->atoms); free(variables->terms);
    cetta_var_index_free(&variables->index);
}

static TcTerm tftc_variable(TfTc *ctx, TfTcVariables *variables,
        TcDecideScope *scope, Atom *atom) {
    uint32_t position = cetta_var_index_find_atom(&variables->index,
        variables->atoms, variables->length, atom->var_id);
    if (position < variables->length)
        return variables->terms[position];
    size_t capacity = variables->capacity;
    if (variables->length == UINT32_MAX ||
        !tftc_grow((void **)&variables->atoms, &variables->capacity,
                    (size_t)variables->length + 1u, sizeof(*variables->atoms)))
        return 0u;
    if (capacity != variables->capacity) {
        TcTerm *terms = realloc(variables->terms, variables->capacity * sizeof(*terms));
        if (!terms)
            return 0u;
        variables->terms = terms;
    }
    TcTerm term = scope ? tc_decide_unknown(scope)
                        : tc_local(ctx->terms, variables->length);
    if (!term)
        return 0u;
    variables->atoms[variables->length] = atom;
    variables->terms[variables->length++] = term;
    if (!cetta_var_index_note_atom(&variables->index,
                                          variables->atoms, variables->length))
        return 0u;
    return term;
}

static bool tftc_unsupported(const Atom *atom, void *unused) {
    (void)unused;
    if (atom->kind == ATOM_EXPR)
        return atom_petta_value_representation(atom) != PETTA_VALUE_ORDINARY || atom_is_carrier(atom);
    if (atom->kind != ATOM_GROUNDED)
        return false;
    return (atom->ground.gkind == GV_FLOAT && isnan(atom->ground.fval)) ||
        atom->ground.gkind == GV_TERM_GRAPH || atom->ground.gkind == GV_BINDINGS ||
        atom->ground.gkind == GV_STRUCTURAL || atom->ground.gkind == GV_PRIME_CONTEXT ||
        atom->ground.gkind == GV_PRIME_NEED_CAPABILITY;
}

/* Flat syntax cannot contain a cycle or a shared expression subgraph. Keep
 * its capability check in the host carrier; nested values use the checked
 * graph traversal, including its allocation-failure result. */
static bool tftc_admitted_graph(TfTc *ctx, Atom *atom, AtomTreePredicate refuses) {
    if (atom) {
        if (refuses(atom, NULL)) {
            ctx->unsupported = true;
            return false;
        }
        if (atom->kind != ATOM_EXPR)
            return true;
        bool flat = true;
        for (CettaExprLen i = 0u; flat && i < atom->expr.len; i++) {
            const Atom *child = atom->expr.elems[i];
            if (!child || child->kind == ATOM_EXPR) {
                flat = false;
            } else if (refuses(child, NULL)) {
                ctx->unsupported = true;
                return false;
            }
        }
        if (flat)
            return true;
    }
    bool complete = false;
    bool unsupported = atom_graph_any_checked(atom, refuses, NULL,
                                               NULL, SIZE_MAX, &complete);
    if (unsupported)
        ctx->unsupported = true;
    if (!complete)
        ctx->no_memory = true;
    return complete && !unsupported;
}

static bool tftc_supported(TfTc *ctx, Atom *atom) {
    return tftc_admitted_graph(ctx, atom, tftc_unsupported);
}

static bool tftc_mutable_matching(const Atom *atom, void *unused) {
    return tftc_unsupported(atom, unused) ||
        (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STATE);
}

/* Ground state observations may compare contents while leaf matching uses
 * identity. They require the host's mutable observation contract rather
 * than one inert constant equality in the first-order adapter. */
static bool tftc_port_supported(TfTc *ctx, Atom *atom) {
    return tftc_admitted_graph(ctx, atom, tftc_mutable_matching);
}

/* Atomic values are constants for matching, while their complete original
 * values remain available for publication. Number hashes follow the lane's
 * exact value equality, including promoted numeric representations. */
static uint32_t tftc_constant_hash(Atom *atom) {
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STATE) {
        uint64_t pointer = (uint64_t)(uintptr_t)atom->ground.ptr;
        return (uint32_t)(pointer ^ (pointer >> 32u)) ^ UINT32_C(0x793a4c6b);
    }
    return atom_value_hash(atom);
}

static bool tftc_constant_equal(const TfTc *ctx, Atom *left, Atom *right) {
    /* Symbols have the same identity observation in both dialects. Avoid
     * entering the general value walker for copied symbol leaves. */
    if (left->kind == ATOM_SYMBOL || right->kind == ATOM_SYMBOL)
        return left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL &&
            left->sym_id == right->sym_id;
    if (left->kind == ATOM_GROUNDED && left->ground.gkind == GV_STATE)
        return right->kind == ATOM_GROUNDED && right->ground.gkind == GV_STATE &&
            left->ground.ptr == right->ground.ptr;
    if (right->kind == ATOM_GROUNDED && right->ground.gkind == GV_STATE)
        return false;
    return ctx->firing->dialect == CETTA_TRANSFORM_DIALECT_PETTA
        ? atom_eq(left, right) : atom_value_eq(left, right);
}

static TcTerm tftc_constant(TfTc *ctx, Atom *atom) {
    uint32_t hash = tftc_constant_hash(atom), first = UINT32_MAX, last = UINT32_MAX;
    if (cetta_u64_index_find(&ctx->constant_hashes, hash, &first)) {
        for (uint32_t at = first; at != UINT32_MAX; at = ctx->constants[at].next) {
            if (tftc_constant_equal(ctx, ctx->constants[at].atom, atom))
                return ctx->constants[at].term;
            last = at;
        }
    }
    if (ctx->constant_length >= UINT32_MAX ||
        !tftc_grow((void **)&ctx->constants, &ctx->constant_capacity,
                    ctx->constant_length + 1u, sizeof(*ctx->constants)))
        return 0u;
    uint32_t position = (uint32_t)ctx->constant_length;
    char name[40];
    int length = snprintf(name, sizeof(name), "constant-%u", position);
    TcTerm term = tc_symbol(ctx->terms, name, (size_t)length);
    if (!term)
        return 0u;
    ctx->constants[ctx->constant_length++] = (TfTcConstant){atom, term, UINT32_MAX};
    if (first == UINT32_MAX) {
        if (cetta_u64_index_insert(&ctx->constant_hashes, hash, position) != CETTA_GSLT_U32_INDEX_INSERTED_V1)
            return 0u;
    } else {
        ctx->constants[last].next = position;
    }
    if (cetta_u64_index_insert(&ctx->constant_terms, term, position) != CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return 0u;
    return term;
}

typedef struct { uint64_t key, result; } TfTcMemo;
typedef struct { uint64_t key; uint32_t memo, count, next; uint64_t *children; } TfTcFrame;
typedef struct {
    TfTc *owner;
    bool encode;
    TfTcVariables *variables;
    TcDecideScope *scope;
    CettaU64Index index;
    TfTcMemo *memo;
    size_t length, capacity;
} TfTcFold;

static void tftc_fold_free(TfTcFold *fold) {
    cetta_u64_index_free(&fold->index);
    free(fold->memo);
}

static bool tftc_node(TfTcFold *fold, uint64_t key, bool *branch,
                       uint32_t *count, uint64_t *leaf) {
    TfTc *ctx = fold->owner;
    *leaf = 0u;
    if (fold->encode) {
        Atom *atom = (Atom *)(uintptr_t)key;
        *branch = atom->kind == ATOM_EXPR;
        if (*branch) { *count = atom->expr.len; return true; }
        *leaf = atom->kind == ATOM_VAR
            ? tftc_variable(ctx, fold->variables, fold->scope, atom)
            : tftc_constant(ctx, atom);
    } else {
        TcTerm term = (TcTerm)key;
        TcKind kind = tc_kind(ctx->terms, term);
        *branch = kind == TC_EXPRESSION;
        if (*branch) { *count = tc_length(ctx->terms, term); return true; }
        Atom *atom = NULL;
        uint32_t constant = 0u;
        if (kind == TC_SYMBOL && cetta_u64_index_find(&ctx->constant_terms, term, &constant))
            atom = atom_deep_copy(ctx->arena, ctx->constants[constant].atom);
        else if (kind == TC_UNKNOWN)
            atom = atom_var_with_id(ctx->arena, "__tcalc", fresh_var_id());
        if (!atom)
            return false;
        *leaf = (uint64_t)(uintptr_t)atom;
    }
    return *leaf != 0u;
}

static uint64_t tftc_child(TfTcFold *fold, uint64_t key, uint32_t child) {
    return fold->encode
        ? (uint64_t)(uintptr_t)((Atom *)(uintptr_t)key)->expr.elems[child]
        : tc_item(fold->owner->terms, (TcTerm)key, child);
}

static uint64_t tftc_build(TfTcFold *fold, const uint64_t *children, uint32_t count) {
    if (fold->encode) {
        TcTerm *terms = malloc((count ? count : 1u) * sizeof(*terms));
        if (!terms) return 0u;
        for (uint32_t i = 0u; i < count; i++) terms[i] = (TcTerm)children[i];
        TcTerm result = tc_expression(fold->owner->terms, terms, count);
        free(terms);
        return result;
    }
    Atom **atoms = malloc((count ? count : 1u) * sizeof(*atoms));
    if (!atoms) return 0u;
    for (uint32_t i = 0u; i < count; i++) atoms[i] = (Atom *)(uintptr_t)children[i];
    Atom *result = atom_expr(fold->owner->arena, atoms, count);
    free(atoms);
    return (uint64_t)(uintptr_t)result;
}

/* One iterative DAG fold transports both directions. Each import has its
 * own variable inventory; the rule's roots share theirs. Active entries
 * distinguish a malformed cycle from a repeated completed subterm. */
static uint64_t tftc_fold(TfTcFold *fold, uint64_t root) {
    TfTcFrame *stack = NULL;
    size_t length = 0u, capacity = 0u;
    uint64_t key = root, result = 0u;
    for (;;) {
        uint32_t position = 0u;
        if (cetta_u64_index_find(&fold->index, key, &position)) {
            result = fold->memo[position].result;
            if (!result) { fold->owner->unsupported = true; goto fail; }
        } else {
            if (fold->length >= UINT32_MAX ||
                !tftc_grow((void **)&fold->memo, &fold->capacity, fold->length + 1u, sizeof(*fold->memo)))
                goto fail;
            position = (uint32_t)fold->length++;
            fold->memo[position] = (TfTcMemo){key, 0u};
            if (cetta_u64_index_insert(&fold->index, key, position) != CETTA_GSLT_U32_INDEX_INSERTED_V1)
                goto fail;
            bool branch = false;
            uint32_t count = 0u;
            if (!tftc_node(fold, key, &branch, &count, &result)) goto fail;
            if (branch && count) {
                if (!tftc_grow((void **)&stack, &capacity, length + 1u, sizeof(*stack))) goto fail;
                uint64_t *children = malloc((size_t)count * sizeof(*children));
                if (!children) goto fail;
                stack[length++] = (TfTcFrame){key, position, count, 0u, children};
                key = tftc_child(fold, key, 0u);
                continue;
            }
            if (branch) result = tftc_build(fold, NULL, 0u);
            if (!result) goto fail;
            fold->memo[position].result = result;
            if (fold->encode)
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_LOWERED_NODE);
        }
        while (length) {
            TfTcFrame *frame = &stack[length - 1u];
            frame->children[frame->next++] = result;
            if (frame->next < frame->count) {
                key = tftc_child(fold, frame->key, frame->next);
                break;
            }
            result = tftc_build(fold, frame->children, frame->count);
            free(frame->children);
            length--;
            if (!result) goto fail;
            fold->memo[frame->memo].result = result;
            if (fold->encode)
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_LOWERED_NODE);
        }
        if (!length) {
            if (fold->encode && result > fold->owner->maximum_term)
                fold->owner->maximum_term = (uint32_t)result;
            free(stack); return result;
        }
    }
fail:
    for (size_t i = 0u; i < length; i++) free(stack[i].children);
    free(stack);
    if (!fold->owner->unsupported) fold->owner->no_memory = true;
    return 0u;
}

static void tftc_close(void *raw) {
    TfTcCursor *cursor = raw;
    if (cursor && !cursor->closed) {
        space_occurrence_cursor_release(&cursor->rows);
        cursor->closed = true;
    }
}

static void tftc_close_all(TfTc *ctx) {
    for (size_t i = 0u; i < ctx->cursor_length; i++) tftc_close(ctx->cursors[i]);
}

static TcStoreStatus tftc_cursor(TfTc *ctx, uint32_t location, Atom *pattern, void **out) {
    TfTcCursor *cursor = arena_alloc(ctx->arena, sizeof(*cursor));
    if (!cursor || !tftc_grow((void **)&ctx->cursors, &ctx->cursor_capacity,
                              ctx->cursor_length + 1u, sizeof(*ctx->cursors)))
        return TC_STORE_NO_MEMORY;
    *cursor = (TfTcCursor){.owner = ctx, .space = ctx->locations[location].space, .location = location};
    space_occurrence_cursor_init_empty(&cursor->rows);
    Atom *index = petta_semantics_match_index_pattern(ctx->arena, pattern, NULL);
    if (!index || !space_occurrence_cursor_init(cursor->space, index, &cursor->rows)) {
        space_occurrence_cursor_release(&cursor->rows);
        return TC_STORE_NO_MEMORY;
    }
    ctx->cursors[ctx->cursor_length++] = cursor;
    *out = cursor;
    return TC_STORE_ROW;
}

static TcStoreStatus tftc_next(void *raw, TcOccurrence *out) {
    TfTcCursor *cursor = raw;
    TfTc *ctx = cursor->owner;
    if (ctx->firing->spend && !ctx->firing->spend(ctx->firing->context, 1u))
        return TC_STORE_INTERRUPTED;
    CettaIndex position = 0u;
    Atom *atom = NULL;
    SpaceOccurrenceCursorStep step = space_occurrence_cursor_next(&cursor->rows, &position, &atom);
    if (step == SPACE_OCCURRENCE_CURSOR_END) return TC_STORE_END;
    if (step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) return TC_STORE_INVALIDATED;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_SCAN_ROW);
    for (size_t i = 0u; i < ctx->row_length; i++) {
        TfTcRow *row = &ctx->rows[i];
        if (row->space == cursor->space && row->position == position &&
            row->token.instance_id == cursor->rows.read.instance_id &&
            row->token.prefix_epoch == cursor->rows.read.prefix_epoch) {
            *out = (TcOccurrence)i + 1u;
            return TC_STORE_ROW;
        }
    }
    if (!atom || !tftc_grow((void **)&ctx->rows, &ctx->row_capacity,
                             ctx->row_length + 1u, sizeof(*ctx->rows)))
        return TC_STORE_NO_MEMORY;
    ctx->rows[ctx->row_length] = (TfTcRow){cursor->space, cursor->rows.read, position, atom, cursor->location};
    *out = (TcOccurrence)++ctx->row_length;
    return TC_STORE_ROW;
}

static TcTerm tftc_located(TfTc *ctx, uint32_t location, TcTerm term) {
    TcTerm parts[2] = {ctx->locations[location].tag, term};
    TcTerm located = term ? tc_expression(ctx->terms, parts, 2u) : 0u;
    if (located > ctx->maximum_term) ctx->maximum_term = located;
    return located;
}

static TcStoreStatus tftc_open(void *raw, const TcTerms *terms, TcTerm pattern, void **out) {
    TfTc *ctx = raw;
    ctx->terms = (TcTerms *)terms;
    if (tc_kind(terms, pattern) != TC_EXPRESSION || tc_length(terms, pattern) != 2u)
        return TC_STORE_INVALIDATED;
    TcTerm tag = tc_item(terms, pattern, 0u);
    uint32_t location = 0u;
    while (location < ctx->location_length && ctx->locations[location].tag != tag) location++;
    if (location == ctx->location_length) return TC_STORE_INVALIDATED;
    TfTcFold fold = {.owner = ctx};
    Atom *decoded = (Atom *)(uintptr_t)tftc_fold(&fold, tc_item(terms, pattern, 1u));
    tftc_fold_free(&fold);
    return decoded ? tftc_cursor(ctx, location, decoded, out) : TC_STORE_NO_MEMORY;
}

static TcStoreStatus tftc_import(void *raw, TcDecideScope *scope, TcTerms *terms,
                                 TcOccurrence occurrence, TcTerm *out) {
    TfTc *ctx = raw;
    ctx->terms = terms;
    if (!occurrence || occurrence > ctx->row_length)
        return TC_STORE_INVALIDATED;
    TfTcRow *row = &ctx->rows[occurrence - 1u];
    TfTcVariables variables = {0};
    TfTcFold fold = {.owner = ctx, .encode = true, .variables = &variables, .scope = scope};
    TcTerm term = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)row->atom);
    *out = tftc_located(ctx, row->location, term);
    tftc_fold_free(&fold);
    tftc_variables_free(&variables);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_IMPORT_ROW);
    return *out ? TC_STORE_ROW : TC_STORE_NO_MEMORY;
}

static int tftc_spend(void *raw, uint64_t units) {
    TfTc *ctx = raw;
    return !ctx->firing->spend || ctx->firing->spend(ctx->firing->context, units);
}

typedef struct { TcTerm term; Atom *expected; uint32_t next; } TfTcAgreement;

/* Compare in the codec's matching observation, not by re-reading mutable
 * state contents. The two variable maps check a bijection of full identities.
 * Retaining original publications then also retains their extra metadata. */
static bool tftc_agrees(TfTc *ctx, const TcTerm *made, size_t count) {
    if (count != ctx->firing->expected_count)
        return false;
    TfTcAgreement *stack = NULL, *seen = NULL;
    size_t length = 0u, capacity = 0u, seen_length = 0u, seen_capacity = 0u;
    CettaU64Index pairs = {0}, actual_variables = {0};
    TfTcVariables expected_variables = {0};
    bool agrees = false;
    if (!tftc_grow((void **)&stack, &capacity, count, sizeof(*stack))) goto no_memory;
    for (size_t i = 0u; i < count; i++)
        stack[length++] = (TfTcAgreement){made[i], ctx->firing->expected[i], UINT32_MAX};
    while (length) {
        TfTcAgreement pair = stack[--length];
        uint64_t hash = (uint64_t)(uintptr_t)pair.expected ^
            ((uint64_t)pair.term * UINT64_C(0x9e3779b97f4a7c15));
        uint32_t first = UINT32_MAX, last = UINT32_MAX;
        bool known = false;
        if (cetta_u64_index_find(&pairs, hash, &first)) {
            for (uint32_t at = first; at != UINT32_MAX; at = seen[at].next) {
                if (seen[at].term == pair.term && seen[at].expected == pair.expected) { known = true; break; }
                last = at;
            }
        }
        if (known) continue;
        if (seen_length >= UINT32_MAX ||
            !tftc_grow((void **)&seen, &seen_capacity, seen_length + 1u, sizeof(*seen))) goto no_memory;
        uint32_t position = (uint32_t)seen_length;
        seen[seen_length++] = pair;
        if (first == UINT32_MAX) {
            if (cetta_u64_index_insert(&pairs, hash, position) != CETTA_GSLT_U32_INDEX_INSERTED_V1) goto no_memory;
        } else seen[last].next = position;
        TcKind kind = tc_kind(ctx->terms, pair.term);
        if (kind == TC_UNKNOWN) {
            if (pair.expected->kind != ATOM_VAR) goto done;
            uint32_t existing = 0u;
            if (cetta_u64_index_find(&actual_variables, pair.term, &existing)) {
                if (!var_id_eq(expected_variables.atoms[existing]->var_id, pair.expected->var_id)) goto done;
            } else {
                if (cetta_var_index_find_atom(&expected_variables.index, expected_variables.atoms,
                        expected_variables.length, pair.expected->var_id) < expected_variables.length) goto done;
                if (expected_variables.length == UINT32_MAX ||
                    !tftc_grow((void **)&expected_variables.atoms, &expected_variables.capacity,
                        (size_t)expected_variables.length + 1u, sizeof(*expected_variables.atoms))) goto no_memory;
                expected_variables.atoms[expected_variables.length++] = pair.expected;
                if (!cetta_var_index_note_atom(&expected_variables.index,
                        expected_variables.atoms, expected_variables.length)) goto no_memory;
                if (cetta_u64_index_insert(&actual_variables, pair.term,
                        expected_variables.length - 1u) != CETTA_GSLT_U32_INDEX_INSERTED_V1) goto no_memory;
            }
        } else if (kind == TC_EXPRESSION) {
            uint32_t arity = tc_length(ctx->terms, pair.term);
            if (pair.expected->kind != ATOM_EXPR || pair.expected->expr.len != arity) goto done;
            if (!tftc_grow((void **)&stack, &capacity, length + arity, sizeof(*stack))) goto no_memory;
            for (uint32_t i = 0u; i < arity; i++)
                stack[length++] = (TfTcAgreement){tc_item(ctx->terms, pair.term, i),
                    pair.expected->expr.elems[i], UINT32_MAX};
        } else if (kind == TC_SYMBOL) {
            uint32_t constant = 0u;
            if (!cetta_u64_index_find(&ctx->constant_terms, pair.term, &constant) ||
                !tftc_constant_equal(ctx, ctx->constants[constant].atom, pair.expected)) goto done;
        } else goto done;
    }
    agrees = true;
    goto done;
no_memory:
    ctx->no_memory = true;
done:
    free(stack); free(seen);
    cetta_u64_index_free(&pairs); cetta_u64_index_free(&actual_variables);
    tftc_variables_free(&expected_variables);
    return agrees;
}

static TcCommitStatus tftc_commit(void *raw, const TcTerms *terms,
        const TcOccurrence *taken, size_t takes, const TcTerm *made, size_t count) {
    TfTc *ctx = raw;
    ctx->terms = (TcTerms *)terms;
    if (takes != ctx->takes)
        return TC_COMMIT_REFUSED;
    for (size_t i = 0u; i < takes; i++)
        if (taken[i] != ctx->selected[i]) return TC_COMMIT_REFUSED;
    if (!tftc_agrees(ctx, made, count)) {
        if (ctx->no_memory) return TC_COMMIT_NO_MEMORY;
        ctx->error = atom_error(ctx->arena, ctx->firing->publication,
            atom_symbol(ctx->arena, "TransformProtocolResultMismatch"));
        ctx->committed = CETTA_TRANSFORM_ERROR;
        return TC_COMMIT_HOST_ERROR;
    }
    /* Original publications already own their leaves. Releasing pins before
     * the first removal avoids detaching an entire Space for this call. */
    tftc_close_all(ctx);
    ctx->committed = ctx->firing->commit(ctx->firing->context, ctx->firing->expected, count);
    return ctx->committed == CETTA_TRANSFORM_FIRED ? TC_COMMIT_DONE
        : ctx->committed == CETTA_TRANSFORM_NONE ? TC_COMMIT_REFUSED
        : ctx->committed == CETTA_TRANSFORM_INTERRUPTED ? TC_COMMIT_INTERRUPTED
        : ctx->committed == CETTA_TRANSFORM_DECLINED ? TC_COMMIT_NO_MEMORY : TC_COMMIT_HOST_ERROR;
}

static bool tftc_location(TfTc *ctx, Space *space, uint32_t *out) {
    for (size_t i = 0u; i < ctx->location_length; i++)
        if (ctx->locations[i].space == space) { *out = (uint32_t)i; return true; }
    if (!space || ctx->location_length >= UINT32_MAX ||
        !tftc_grow((void **)&ctx->locations, &ctx->location_capacity,
                    ctx->location_length + 1u, sizeof(*ctx->locations))) return false;
    char name[40];
    int length = snprintf(name, sizeof(name), "space-%zu", ctx->location_length);
    TcTerm tag = tc_symbol(ctx->terms, name, (size_t)length);
    if (!tag) return false;
    *out = (uint32_t)ctx->location_length;
    ctx->locations[ctx->location_length++] = (TfTcLocation){space, tag};
    return true;
}

static bool tftc_used(const TfTc *ctx, TcOccurrence occurrence) {
    for (size_t i = 0u; i < ctx->takes; i++)
        if (ctx->selected[i] == occurrence) return true;
    return false;
}

static TcStoreStatus tftc_select(TfTc *ctx, const CettaTransformPort *port,
                                 uint32_t location, TcOccurrence *out) {
    void *raw = NULL;
    TcStoreStatus status = tftc_cursor(ctx, location, port->receipt, &raw);
    if (status != TC_STORE_ROW) return status;
    while ((status = tftc_next(raw, out)) == TC_STORE_ROW) {
        TfTcRow *row = &ctx->rows[*out - 1u];
        if (tftc_used(ctx, *out)) continue;
        bool matched = false;
        if (!port->exact || atom_alpha_eq(row->atom, port->receipt)) {
            Atom *opened = atom_has_vars(row->atom)
                ? cetta_instantiate_frame_syntax(ctx->arena, row->atom) : row->atom;
            BindingsBuilder bindings;
            if (!opened || !bindings_builder_init(&bindings, NULL)) return TC_STORE_NO_MEMORY;
            matched = match_atoms_builder(port->receipt, opened, &bindings, ctx->arena);
            bindings_builder_free(&bindings);
        }
        if (matched) {
            if (!tftc_port_supported(ctx, row->atom)) return TC_STORE_NO_MEMORY;
            return TC_STORE_ROW;
        }
    }
    return status;
}

static CettaTransformOutcome tftc_terms_decide(void *unused, Arena *arena,
        const CettaTransformFiring *firing, Atom **error) {
    (void)unused;
    *error = NULL;
    TfTc ctx = {.arena = arena, .firing = firing, .committed = CETTA_TRANSFORM_NONE};
    ctx.cache = tftc_cache_take();
    ctx.terms = ctx.cache && ctx.cache->terms ? ctx.cache->terms : tc_terms_new();
    if (ctx.cache) ctx.cache->terms = ctx.terms;
    TcProgram *program = NULL;
    TfTcVariables variables = {0};
    TfTcFold fold = {.owner = &ctx, .encode = true, .variables = &variables};
    size_t ports = firing->port_count;
    TcTerm *take = calloc(ports ? ports : 1u, sizeof(*take));
    TcTerm *read = calloc(ports ? ports : 1u, sizeof(*read));
    TcTerm *tell = calloc(firing->equation_count ? 2u * firing->equation_count : 1u, sizeof(*tell));
    TcCheck *checks = calloc(firing->check_count ? firing->check_count : 1u, sizeof(*checks));
    ctx.selected = calloc(ports ? ports : 1u, sizeof(*ctx.selected));
    CettaTransformOutcome result = CETTA_TRANSFORM_ERROR;
    if (!ctx.terms || !take || !read || !tell || !checks || !ctx.selected) goto done;
    uint32_t takes = 0u, reads = 0u;
    for (unsigned phase = 0u; phase < 2u; phase++) {
        for (size_t i = 0u; i < ports; i++) {
            const CettaTransformPort *port = &firing->ports[i];
            if (port->take != (phase == 0u)) continue;
            if (!port->space) { result = CETTA_TRANSFORM_NONE; goto done; }
            if (!tftc_port_supported(&ctx, port->pattern) ||
                !tftc_port_supported(&ctx, port->receipt)) goto done;
            uint32_t location = 0u;
            if (!tftc_location(&ctx, port->space, &location)) goto done;
            TcTerm pattern = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)port->pattern);
            pattern = tftc_located(&ctx, location, pattern);
            if (!pattern) goto done;
            TcOccurrence selected = 0u;
            TcStoreStatus found = tftc_select(&ctx, port, location, &selected);
            if (found != TC_STORE_ROW) {
                if (found == TC_STORE_END) result = CETTA_TRANSFORM_NONE;
                else if (found == TC_STORE_INTERRUPTED) result = CETTA_TRANSFORM_INTERRUPTED;
                else if (found == TC_STORE_INVALIDATED) ctx.error = atom_symbol(arena, "TransformReadViewInvalidated");
                goto done;
            }
            if (port->take) {
                take[takes] = pattern; ctx.selected[takes++] = selected; ctx.takes = takes;
            } else {
                read[reads] = pattern; ctx.selected[takes + reads++] = selected;
            }
        }
    }
    for (size_t i = 0u; i < firing->equation_count; i++) {
        if (!tftc_supported(&ctx, firing->equations[i].left) || !tftc_supported(&ctx, firing->equations[i].right)) goto done;
        tell[2u * i] = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)firing->equations[i].left);
        tell[2u * i + 1u] = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)firing->equations[i].right);
        if (!tell[2u * i] || !tell[2u * i + 1u]) goto done;
    }
    for (size_t i = 0u; i < firing->check_count; i++) {
        if (!tftc_supported(&ctx, firing->checks[i].equation.left) || !tftc_supported(&ctx, firing->checks[i].equation.right)) goto done;
        TcTerm left = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)firing->checks[i].equation.left);
        TcTerm right = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)firing->checks[i].equation.right);
        if (!left || !right) goto done;
        checks[i] = (TcCheck){TC_CHECK_EQUAL, left, right, 0u};
    }
    if (!tftc_supported(&ctx, firing->publication)) goto done;
    TcTerm publication = (TcTerm)tftc_fold(&fold, (uint64_t)(uintptr_t)firing->publication);
    if (!publication) goto done;
    TcRule rule = {"transform", take, takes, read, reads, tell, (uint32_t)firing->equation_count,
        checks, (uint32_t)firing->check_count, &publication, 1u, variables.length};
    program = tftc_program(&ctx, &rule);
    if (!program) goto done;
    TcInterpretation how = {.step = TC_STEP_ONE,
        .ask = firing->checks_before_tells ? TC_ASK_BEFORE_TELLS : TC_ASK_AFTER_TELLS,
        .store = TC_STORE_MULTISET, .matches = TC_MATCHES_KEYS, .budget = UINT64_MAX};
    TcStoreOps ops = {&ctx, tftc_open, tftc_next, tftc_close, tftc_import, tftc_commit, tftc_spend};
    const TcTerm *made = NULL;
    size_t count = 0u;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_DECISION);
    ctx.protocol_started = true;
    /* The protocol borrows the table into its own header and lends that
     * header to callbacks. Always use the supplied header while borrowed;
     * the owner's updated table is returned when the decision ends. */
    TcTerms *owner_terms = ctx.terms;
    TcDecision decided = tc_decide(program, 0u, &how, &ops, ctx.selected, ports, owner_terms, &made, &count);
    ctx.terms = owner_terms;
    if (decided == TC_DECIDED_ADMITTED)
        for (size_t i = 0u; i < count; i++)
            if (made[i] > ctx.maximum_term) ctx.maximum_term = made[i];
    switch (decided) {
    case TC_DECIDED_ADMITTED: result = CETTA_TRANSFORM_FIRED; break;
    case TC_DECIDED_DECLINED:
    case TC_DECIDED_STORE_REFUSED:
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_REJECTED);
        result = CETTA_TRANSFORM_NONE; break;
    case TC_DECIDED_INTERRUPTED: result = CETTA_TRANSFORM_INTERRUPTED; break;
    case TC_DECIDED_INVALIDATED: ctx.error = atom_symbol(arena, "TransformReadViewInvalidated"); break;
    case TC_DECIDED_HOST_ERROR: result = CETTA_TRANSFORM_ERROR; break;
    case TC_DECIDED_NO_MEMORY: break;
    }
done:
    if (ctx.unsupported && !ctx.protocol_started) result = CETTA_TRANSFORM_DECLINED;
    if (result == CETTA_TRANSFORM_ERROR && !*error)
        *error = ctx.error && atom_is_error(ctx.error) ? ctx.error
            : atom_error(arena, firing->publication,
                          ctx.error ? ctx.error : atom_symbol(arena, "OutOfMemory"));
    tftc_close_all(&ctx);
    tftc_fold_free(&fold);
    tftc_variables_free(&variables);
    cetta_u64_index_free(&ctx.constant_hashes); cetta_u64_index_free(&ctx.constant_terms);
    free(ctx.constants); free(ctx.locations); free(ctx.cursors); free(ctx.rows); free(ctx.selected);
    free(take); free(read); free(tell); free(checks);
    if (ctx.cache) {
        if (ctx.maximum_term > ctx.cache->maximum_term) ctx.cache->maximum_term = ctx.maximum_term;
        ctx.cache->busy = false;
        if (ctx.cache->retire) tftc_cache_flush(ctx.cache);
    } else {
        tc_program_free(program); tc_terms_free(ctx.terms);
    }
    return result;
}

typedef struct {
    Atom *actual, *expected;
    uint32_t next;
} TfTcAtomPair;

static bool tftc_scalar(const Atom *atom) {
    if (atom->kind == ATOM_SYMBOL) return true;
    if (atom->kind != ATOM_GROUNDED) return false;
    switch (atom->ground.gkind) {
    case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
    case GV_BIGINT: case GV_RATIONAL: case GV_INTERNAL_TAG:
        return true;
    default: return false;
    }
}

/* The same matching observation as the term codec, without materializing a
 * second carrier. Alpha agreement preserves the complete alias partition;
 * a guard instead compares unresolved variables by their full identities. */
static bool tftc_atoms_agree(TfTc *ctx, Atom *actual, Atom *expected, bool alpha) {
    if (!actual || !expected) return false;
    if (actual->kind == ATOM_VAR && expected->kind == ATOM_VAR)
        return alpha || var_id_eq(actual->var_id, expected->var_id);
    if (actual->kind != ATOM_EXPR && expected->kind != ATOM_EXPR &&
        actual->kind != ATOM_VAR && expected->kind != ATOM_VAR)
        return tftc_constant_equal(ctx, actual, expected);
    /* Classify the whole flat pair before comparing any leaf. Opaque
     * equality services and variables retain the general graph observer. */
    if (actual->kind == ATOM_EXPR && expected->kind == ATOM_EXPR &&
        actual->expr.len == expected->expr.len) {
        bool flat = true;
        for (CettaExprLen i = 0u; flat && i < actual->expr.len; i++)
            flat = tftc_scalar(actual->expr.elems[i]) && tftc_scalar(expected->expr.elems[i]);
        if (flat) {
            for (CettaExprLen i = 0u; i < actual->expr.len; i++)
                if (!tftc_constant_equal(ctx, actual->expr.elems[i], expected->expr.elems[i])) return false;
            return true;
        }
    }
    TfTcAtomPair *stack = NULL, *seen = NULL;
    size_t length = 0u, capacity = 0u, seen_length = 0u, seen_capacity = 0u;
    CettaU64Index pairs = {0};
    TfTcVariables av = {0}, ev = {0};
    bool agrees = false;
    if (!tftc_grow((void **)&stack, &capacity, 1u, sizeof(*stack))) goto no_memory;
    stack[length++] = (TfTcAtomPair){actual, expected, UINT32_MAX};
    while (length) {
        TfTcAtomPair pair = stack[--length];
        Atom *a = pair.actual, *e = pair.expected;
        if (!a || !e) goto done;
        if (a->kind == ATOM_VAR || e->kind == ATOM_VAR) {
            if (a->kind != ATOM_VAR || e->kind != ATOM_VAR) goto done;
            if (!alpha) {
                if (!var_id_eq(a->var_id, e->var_id)) goto done;
                continue;
            }
            uint32_t ai = cetta_var_index_find_atom(&av.index, av.atoms, av.length, a->var_id);
            uint32_t ei = cetta_var_index_find_atom(&ev.index, ev.atoms, ev.length, e->var_id);
            if (ai < av.length || ei < ev.length) {
                if (ai != ei) goto done;
                continue;
            }
            if (av.length == UINT32_MAX ||
                !tftc_grow((void **)&av.atoms, &av.capacity, (size_t)av.length + 1u, sizeof(*av.atoms)) ||
                !tftc_grow((void **)&ev.atoms, &ev.capacity, (size_t)ev.length + 1u, sizeof(*ev.atoms))) goto no_memory;
            av.atoms[av.length++] = a; ev.atoms[ev.length++] = e;
            if (!cetta_var_index_note_atom(&av.index, av.atoms, av.length) ||
                !cetta_var_index_note_atom(&ev.index, ev.atoms, ev.length)) goto no_memory;
            continue;
        }
        if (a->kind != ATOM_EXPR || e->kind != ATOM_EXPR) {
            if (!tftc_constant_equal(ctx, a, e)) goto done;
            continue;
        }
        if (a->expr.len != e->expr.len) goto done;
        uint64_t hash = (uint64_t)(uintptr_t)a ^
            ((uint64_t)(uintptr_t)e * UINT64_C(0x9e3779b97f4a7c15));
        uint32_t first = UINT32_MAX, last = UINT32_MAX;
        bool known = false;
        if (cetta_u64_index_find(&pairs, hash, &first)) {
            for (uint32_t i = first; i != UINT32_MAX; i = seen[i].next) {
                if (seen[i].actual == a && seen[i].expected == e) { known = true; break; }
                last = i;
            }
        }
        if (known) continue;
        if (seen_length >= UINT32_MAX ||
            !tftc_grow((void **)&seen, &seen_capacity, seen_length + 1u, sizeof(*seen)) ||
            !tftc_grow((void **)&stack, &capacity, length + a->expr.len, sizeof(*stack))) goto no_memory;
        uint32_t here = (uint32_t)seen_length;
        seen[seen_length++] = pair;
        if (first == UINT32_MAX) {
            if (cetta_u64_index_insert(&pairs, hash, here) != CETTA_GSLT_U32_INDEX_INSERTED_V1) goto no_memory;
        } else {
            seen[last].next = here;
        }
        for (CettaExprLen i = 0u; i < a->expr.len; i++)
            stack[length++] = (TfTcAtomPair){a->expr.elems[i], e->expr.elems[i], UINT32_MAX};
    }
    agrees = true;
    goto done;
no_memory:
    ctx->no_memory = true;
done:
    free(stack); free(seen); cetta_u64_index_free(&pairs);
    tftc_variables_free(&av); tftc_variables_free(&ev);
    return agrees;
}

/* Observe a flat, closed binding image without constructing substituted
 * syntax. The canonical resolver retains lexical context or defers; an open
 * or nested image keeps the complete materializing observer below. */
static int tftc_bound_flat_agrees(TfTc *ctx, const Bindings *bindings,
                                  Atom *left, Atom *right) {
    CettaGsltTermViewV1 view = bindings_term_view_v1(left, bindings);
    Atom *l = NULL, *r = NULL;
    if (cetta_gslt_term_view_resolve_root_v1(&view, left, &l) != CETTA_GSLT_TERM_VIEW_OK_V1 ||
        cetta_gslt_term_view_resolve_root_v1(&view, right, &r) != CETTA_GSLT_TERM_VIEW_OK_V1)
        return -1;
    if (tftc_unsupported(l, NULL) || tftc_unsupported(r, NULL))
        return -1;
    if (tftc_scalar(l) && tftc_scalar(r))
        return tftc_constant_equal(ctx, l, r);
    if (l->kind != ATOM_EXPR || r->kind != ATOM_EXPR || l->expr.len != r->expr.len)
        return -1;
    for (CettaExprLen i = 0u; i < l->expr.len; i++) {
        Atom *lc = NULL, *rc = NULL;
        if (cetta_gslt_term_view_resolve_root_v1(&view, l->expr.elems[i], &lc) != CETTA_GSLT_TERM_VIEW_OK_V1 ||
            cetta_gslt_term_view_resolve_root_v1(&view, r->expr.elems[i], &rc) != CETTA_GSLT_TERM_VIEW_OK_V1 ||
            !tftc_scalar(lc) || !tftc_scalar(rc))
            return -1;
        if (!tftc_constant_equal(ctx, lc, rc))
            return 0;
    }
    return 1;
}

/* Observe a small first-order binding image directly. The bijection records
 * every unresolved variable, including repeated occurrences. Larger graphs,
 * cycles and contextual views retain the full observer; this bound only
 * selects an implementation, never truncates a value or an answer. */
static int tftc_bound_small_agrees(TfTc *ctx, const Bindings *bindings,
                                   Atom *actual, Atom *expected) {
    TfTcAtomPair stack[32] = {{actual, expected, UINT32_MAX}};
    VarId actual_vars[32], expected_vars[32];
    size_t length = 1u, variables = 0u, work = 0u;
    CettaGsltTermViewV1 view = bindings_term_view_v1(actual, bindings);
    while (length) {
        if (++work > 32u) return -1;
        TfTcAtomPair pair = stack[--length];
        Atom *a = NULL, *e = pair.expected;
        if (cetta_gslt_term_view_resolve_root_v1(&view, pair.actual, &a) !=
                CETTA_GSLT_TERM_VIEW_OK_V1 || !e ||
            tftc_unsupported(a, NULL) || tftc_unsupported(e, NULL))
            return -1;
        if (a->kind == ATOM_VAR || e->kind == ATOM_VAR) {
            if (a->kind != ATOM_VAR || e->kind != ATOM_VAR) return 0;
            size_t ai = 0u, ei = 0u;
            while (ai < variables && !var_id_eq(actual_vars[ai], a->var_id)) ai++;
            while (ei < variables && !var_id_eq(expected_vars[ei], e->var_id)) ei++;
            if (ai != ei) return 0;
            if (ai == variables) {
                if (variables == 32u) return -1;
                actual_vars[variables] = a->var_id;
                expected_vars[variables++] = e->var_id;
            }
        } else if (a->kind == ATOM_EXPR && e->kind == ATOM_EXPR) {
            if (a->expr.len != e->expr.len) return 0;
            if (a->expr.len > 32u - length) return -1;
            for (CettaExprIndex i = 0u; i < a->expr.len; i++)
                stack[length++] = (TfTcAtomPair){a->expr.elems[i], e->expr.elems[i], UINT32_MAX};
        } else if (a->kind == ATOM_EXPR || e->kind == ATOM_EXPR ||
                   !tftc_constant_equal(ctx, a, e)) {
            return 0;
        }
    }
    return 1;
}

/* Fire.module starts at a match's principal frame. This instance keeps that
 * frame in the host's carrier; the TcTerms instance remains the differential
 * reference. No term table, raw value dictionary or solver answer is shared
 * between firings. The protocol declaration itself is unchanged. */
typedef struct {
    TfTc store;
    BindingsBuilder matching;
    const Bindings *borrowed;
    uint32_t mark;
    Atom *instance;
    CettaTransformOutcome outcome;
} Explorer;

static bool tc_fire_after_tells(Explorer *x) {
    return !x->store.firing->checks_before_tells;
}
static bool tc_fire_mark(Explorer *x) {
    if (!x->borrowed) x->mark = bindings_builder_save(&x->matching);
    return true;
}
static void tc_fire_undo(Explorer *x) {
    if (!x->borrowed) bindings_builder_rollback(&x->matching, x->mark);
}
static bool tc_fire_tells(Explorer *x) {
    const CettaTransformFiring *f = x->store.firing;
    for (size_t i = 0u; i < f->equation_count; i++) {
        if (!tftc_supported(&x->store, f->equations[i].left) ||
            !tftc_supported(&x->store, f->equations[i].right)) return false;
        if (x->borrowed) {
            int observed = tftc_bound_flat_agrees(&x->store, x->borrowed,
                                                  f->equations[i].left, f->equations[i].right);
            if (observed >= 0) {
                if (!observed) return false;
                continue;
            }
            Atom *left = bindings_apply_if_vars(x->borrowed, x->store.arena, f->equations[i].left);
            Atom *right = bindings_apply_if_vars(x->borrowed, x->store.arena, f->equations[i].right);
            if (!left || !right) { x->store.no_memory = true; return false; }
            if (!tftc_atoms_agree(&x->store, left, right, false)) return false;
        } else if (!match_atoms_builder(f->equations[i].left, f->equations[i].right,
                                         &x->matching, x->store.arena)) return false;
    }
    return true;
}
static bool tc_fire_checks(Explorer *x) {
    const CettaTransformFiring *f = x->store.firing;
    const Bindings *bindings = x->borrowed ? x->borrowed : bindings_builder_bindings(&x->matching);
    for (size_t i = 0u; i < f->check_count; i++) {
        if (f->spend && !f->spend(f->context, 1u)) {
            x->outcome = CETTA_TRANSFORM_INTERRUPTED; return false;
        }
        if (!tftc_supported(&x->store, f->checks[i].equation.left) ||
            !tftc_supported(&x->store, f->checks[i].equation.right)) return false;
        Atom *left = bindings_apply_if_vars(bindings, x->store.arena, f->checks[i].equation.left);
        Atom *right = bindings_apply_if_vars(bindings, x->store.arena, f->checks[i].equation.right);
        if (!left || !right) { x->store.no_memory = true; return false; }
        if (!tftc_atoms_agree(&x->store, left, right, false)) return false;
    }
    return true;
}
static bool tc_fire_checks_first(Explorer *x) { return tc_fire_checks(x); }
static bool tc_fire_loose(Explorer *x) { (void)x; return false; }
static bool tc_fire_admit_loose(Explorer *x) { (void)x; return false; }
static bool tc_fire_admit(Explorer *x) {
    const CettaTransformFiring *f = x->store.firing;
    if (!tftc_supported(&x->store, f->publication)) return false;
    const Bindings *bindings = x->borrowed ? x->borrowed : bindings_builder_bindings(&x->matching);
    if (f->expected_count == 1u && !atom_has_vars(f->expected[0])) {
        int observed = tftc_bound_flat_agrees(&x->store, bindings,
                                              f->publication, f->expected[0]);
        if (observed == 1) {
            x->outcome = CETTA_TRANSFORM_FIRED;
            return true;
        }
    }
    if (f->expected_count == 1u &&
        tftc_bound_small_agrees(&x->store, bindings, f->publication, f->expected[0]) == 1) {
        x->outcome = CETTA_TRANSFORM_FIRED;
        return true;
    }
    x->instance = bindings_apply_if_vars(bindings, x->store.arena, f->publication);
    if (!x->instance) { x->store.no_memory = true; return false; }
    if (f->expected_count != 1u ||
        !tftc_atoms_agree(&x->store, x->instance, f->expected[0], true)) {
        x->store.error = atom_error(x->store.arena, f->publication,
            atom_symbol(x->store.arena, "TransformProtocolResultMismatch"));
        x->outcome = CETTA_TRANSFORM_ERROR;
        return false;
    }
    x->outcome = CETTA_TRANSFORM_FIRED;
    return true;
}

#include "../native/tcalc/tcalc_fire.generated.h"

static CettaTransformOutcome tftc_reference_commit(void *unused,
        Atom *const *made, size_t count) {
    (void)unused; (void)made; (void)count;
    return CETTA_TRANSFORM_FIRED;
}

static bool tftc_witnesses_current(const CettaTransformFiring *firing) {
    for (size_t i = 0u; i < firing->port_count; i++) {
        const CettaTransformPort *port = &firing->ports[i];
        if (!port->witness || port->witness->space != port->space ||
            !space_read_token_matches_live_space(port->witness->view, port->space)) return false;
    }
    return true;
}

static bool tftc_witnesses_enabled(const CettaTransformFiring *firing) {
    if (!tftc_witnesses_current(firing)) return false;
    for (size_t i = 0u; i < firing->port_count; i++) {
        const CettaTransformPort *take = &firing->ports[i];
        if (!take->take) continue;
        for (size_t j = 0u; j < firing->port_count; j++) {
            if (i == j) continue;
            const CettaTransformPort *other = &firing->ports[j];
            if (take->space == other->space &&
                take->witness->position == other->witness->position) return false;
        }
    }
    return true;
}

CettaTransformOutcome cetta_transform_tcalc_decide(void *unused, Arena *arena,
        const CettaTransformFiring *firing, Atom **error) {
    if (cetta_kernel_requested() == CETTA_KERNEL_X86 || cetta_transform_tcalc_terms())
        return tftc_terms_decide(unused, arena, firing, error);
    bool compare_carriers = cetta_transform_tcalc_checking();
    *error = NULL;
    Explorer x = {.store = {.arena = arena, .firing = firing}, .outcome = CETTA_TRANSFORM_NONE};
    bool initialized = false;
    size_t ports = firing->port_count;
    CettaTransformOutcome result = CETTA_TRANSFORM_ERROR;
    if (firing->principal) {
        /* Failure of a receipt recognizer is an optimization decline, not
         * evidence that the original firing has no resource selection. */
        if (!tftc_witnesses_enabled(firing)) return CETTA_TRANSFORM_DECLINED;
        x.borrowed = firing->principal;
        for (size_t i = 0u; i < ports; i++) {
            const CettaTransformPort *port = &firing->ports[i];
            if (!tftc_port_supported(&x.store, port->witness->opened) ||
                !tftc_port_supported(&x.store, port->pattern) ||
                !tftc_port_supported(&x.store, port->receipt)) goto done;
            /* The same-call loan establishes Fire's already-unified-match
             * precondition. Checked execution also rematches here and in
             * the independent TcTerms carrier. Extra tells and publication
             * agreement are still checked on both routes. */
            if (compare_carriers) {
                Atom *pattern = bindings_apply_if_vars(x.borrowed, arena, port->pattern);
                Atom *row = bindings_apply_if_vars(x.borrowed, arena, port->witness->opened);
                if (!pattern || !row) goto done;
                if (!tftc_atoms_agree(&x.store, pattern, row, false)) {
                    if (x.store.no_memory) goto done;
                    return CETTA_TRANSFORM_DECLINED;
                }
            }
        }
        goto protocol;
    }
    initialized = bindings_builder_init(&x.matching, NULL);
    x.store.selected = calloc(ports ? ports : 1u, sizeof(*x.store.selected));
    if (!initialized || !x.store.selected) goto done;
    uint32_t takes = 0u, reads = 0u;
    for (unsigned phase = 0u; phase < 2u; phase++) {
        for (size_t i = 0u; i < ports; i++) {
            const CettaTransformPort *port = &firing->ports[i];
            if (port->take != (phase == 0u)) continue;
            if (!port->space) { result = CETTA_TRANSFORM_NONE; goto done; }
            if (!tftc_port_supported(&x.store, port->pattern) ||
                !tftc_port_supported(&x.store, port->receipt)) goto done;
            uint32_t location = 0u;
            while (location < x.store.location_length && x.store.locations[location].space != port->space) location++;
            if (location == x.store.location_length) {
                if (x.store.location_length >= UINT32_MAX ||
                    !tftc_grow((void **)&x.store.locations, &x.store.location_capacity,
                              x.store.location_length + 1u, sizeof(*x.store.locations))) goto done;
                x.store.locations[x.store.location_length++] = (TfTcLocation){.space = port->space};
            }
            TcOccurrence selected = 0u;
            TcStoreStatus found = tftc_select(&x.store, port, location, &selected);
            if (found != TC_STORE_ROW) {
                if (found == TC_STORE_END) result = CETTA_TRANSFORM_NONE;
                else if (found == TC_STORE_INTERRUPTED) result = CETTA_TRANSFORM_INTERRUPTED;
                else if (found == TC_STORE_INVALIDATED) x.store.error = atom_symbol(arena, "TransformReadViewInvalidated");
                goto done;
            }
            if (firing->spend && !firing->spend(firing->context, 1u)) {
                result = CETTA_TRANSFORM_INTERRUPTED; goto done;
            }
            Atom *row = x.store.rows[selected - 1u].atom;
            if (atom_has_vars(row)) row = cetta_instantiate_frame_syntax(arena, row);
            if (!row) goto done;
            if (!match_atoms_builder(port->pattern, row, &x.matching, arena)) {
                result = CETTA_TRANSFORM_NONE; goto done;
            }
            if (port->take) {
                x.store.selected[takes++] = selected; x.store.takes = takes;
            } else {
                x.store.selected[takes + reads++] = selected;
            }
        }
    }
protocol:
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TRANSFORM_TCALC_DECISION);
    (void)tc_fire(&x);
    result = x.outcome;
done:
    if (x.store.unsupported) result = CETTA_TRANSFORM_DECLINED;
    if (x.store.no_memory) result = CETTA_TRANSFORM_ERROR;
    if (compare_carriers && result != CETTA_TRANSFORM_ERROR && result != CETTA_TRANSFORM_INTERRUPTED) {
        CettaTransformFiring reference = *firing;
        reference.commit = tftc_reference_commit;
        reference.spend = NULL;
        Atom *reference_error = NULL;
        CettaTransformOutcome checked = tftc_terms_decide(NULL, arena, &reference, &reference_error);
        if (firing->principal && !tftc_witnesses_current(firing) && checked != CETTA_TRANSFORM_ERROR) {
            result = CETTA_TRANSFORM_DECLINED;
        } else if (checked != result) {
            result = CETTA_TRANSFORM_ERROR;
            x.store.error = checked == CETTA_TRANSFORM_ERROR ? reference_error
                : atom_error(arena, firing->publication,
                    atom_symbol(arena, "TransformProtocolCarrierMismatch"));
        }
    }
    tftc_close_all(&x.store);
    if (firing->principal && result != CETTA_TRANSFORM_ERROR && !tftc_witnesses_current(firing))
        result = CETTA_TRANSFORM_DECLINED;
    if (result == CETTA_TRANSFORM_FIRED)
        result = firing->commit(firing->context, firing->expected, firing->expected_count);
    if (result == CETTA_TRANSFORM_ERROR)
        *error = x.store.error && atom_is_error(x.store.error) ? x.store.error
            : atom_error(arena, firing->publication,
                x.store.error ? x.store.error : atom_symbol(arena, "OutOfMemory"));
    if (initialized) bindings_builder_free(&x.matching);
    free(x.store.locations); free(x.store.cursors); free(x.store.rows); free(x.store.selected);
    return result;
}
