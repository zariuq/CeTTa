#include "prepared_pure_machine.h"

#include "eval.h"
#include "grounded.h"
#include "petta_semantics.h"
#include "stats.h"
#include "symbol.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PREPARED_PURE_LITERAL = 0,
    PREPARED_PURE_ENTRY_ARGUMENT,
    PREPARED_PURE_EVAL_ENTRY_ARGUMENT,
    PREPARED_PURE_SLOT,
    PREPARED_PURE_EVAL_SLOT,
    PREPARED_PURE_BUILD,
    PREPARED_PURE_OBSERVE,
    PREPARED_PURE_REGISTER,
    PREPARED_PURE_INTRINSIC,
    PREPARED_PURE_BIND,
    PREPARED_PURE_IF,
    PREPARED_PURE_CALL,
} PreparedPureNodeKind;

typedef struct {
    PreparedPureNodeKind kind;
    Atom *atom;
    SymbolId head;
    CettaGsltRegisterInstruction instruction;
    CettaGsltRegisterResultKind result_kind;
    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    uint32_t first_child;
    uint32_t child_count;
    uint32_t auxiliary;
    uint32_t first_live_slot;
    uint32_t live_slot_count;
    bool call_arguments_are_values;
    bool tail_position;
} PreparedPureNode;

typedef struct {
    VarId var;
    uint32_t slot;
} PreparedPureVarSlot;

typedef struct {
    VarId var;
    uint32_t slot;
    bool prebound;
} PreparedPureBindVar;

typedef struct {
    Atom *pattern;
    uint32_t first_var;
    uint32_t var_count;
} PreparedPureBindPattern;

typedef struct {
    Atom **items;
    size_t len;
} PreparedPureAtomSpan;

typedef struct {
    CettaPreparedPureProgram *program;
} PreparedPureEphemeronAtomMap;

#define PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT(name) Atom **name;
#define PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN(name) PreparedPureAtomSpan name;
#define PREPARED_PURE_DECLARE_LOGICAL_BINDINGS(name) void *name;
#define PREPARED_PURE_DECLARE_OUTCOME_SET(name) void *name;
#define PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP(name) \
    PreparedPureEphemeronAtomMap name;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(
        PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT,
        PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN,
        PREPARED_PURE_DECLARE_LOGICAL_BINDINGS,
        PREPARED_PURE_DECLARE_OUTCOME_SET,
        PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP)
} PreparedPureGcRoots;
#undef PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP
#undef PREPARED_PURE_DECLARE_OUTCOME_SET
#undef PREPARED_PURE_DECLARE_LOGICAL_BINDINGS
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT

typedef struct {
    Atom *lhs;
    uint32_t arity;
    uint32_t root;
    uint32_t local_count;
    uint32_t first_pattern_var;
    uint32_t pattern_var_count;
} PreparedPureClause;

typedef struct {
    uint32_t arity;
    uint32_t clause_count;
    uint64_t universally_constrained_arguments;
    CettaMatchDecision *selector;
} PreparedPureDecisionProgram;

typedef struct {
    SymbolId head;
    uint32_t first_clause;
    uint32_t clause_count;
    uint32_t first_decision;
    uint32_t decision_count;
    bool compiled;
} PreparedPureHead;

typedef struct {
    SymbolId head;
    uint32_t arity;
    bool callable;
    bool occupied;
} PreparedPureCallableCacheEntry;

typedef struct {
    uint32_t node;
    uint32_t runtime_head_index;
    uint32_t local_base;
    uint32_t value_base;
    uint32_t child_index;
    uint32_t saved_slot_len;
    uint64_t ready_arguments;
    uint32_t demanded_argument;
    uint32_t memo_index;
    uint8_t state;
} PreparedPureFrame;

#define PREPARED_PURE_RUNTIME_NODE UINT32_MAX
#define PREPARED_PURE_NO_MEMO UINT32_MAX

typedef enum {
    PREPARED_PURE_MEMO_EVALUATING = 1,
    PREPARED_PURE_MEMO_VALUE = 2,
    PREPARED_PURE_MEMO_TAIL_PENDING = 3,
} PreparedPureMemoState;

typedef struct {
    Atom *pattern;
    Atom *value;
    uint32_t argument;
} PreparedPurePatternPair;

typedef struct {
    Atom *atom;
    uint32_t value_base;
    uint32_t child_index;
} PreparedPureDynamicFrame;

typedef struct {
    PreparedPureVarSlot *bindings;
    size_t len;
    size_t cap;
    uint32_t next_slot;
} PreparedPureCompileContext;

struct CettaPreparedPureProgram {
    Space *space;
    SpaceReadToken read;
    CettaPreparedPureBooleanValue boolean_value;
    CettaPreparedPureConstructValue construct_value;
    CettaPreparedPureOpaqueValue opaque_value;
    CettaPreparedPureRegisterViewFn register_view;
    CettaPreparedPureExpressionViewFn expression_view;
    CettaPreparedPurePatternViewFn pattern_view;
    CettaGsltPureCallMode call_mode;
    bool total_structural_equality;
    CettaMatchDecisionSemanticIdentity match_decision_semantics;
    bool closed_program;
    bool allow_callable_templates;
    uint32_t root;
    uint32_t root_local_count;
    uint32_t accumulator_slot;
    uint32_t item_slot;

    /* Closed entry values belong to one invocation, not to the compiled
     * node graph.  Keeping them separate makes a parked cached program
     * root-free and prevents it from retaining pointers into an eval arena. */
    SymbolId entry_head;
    Atom **entry_arguments;
    size_t entry_argument_count;

    PreparedPureNode *nodes;
    size_t node_len;
    size_t node_cap;
    uint32_t *children;
    size_t child_len;
    size_t child_cap;
    PreparedPureHead *heads;
    size_t head_len;
    size_t head_cap;
    uint32_t *head_buckets;
    size_t head_bucket_cap;
    PreparedPureCallableCacheEntry *callable_buckets;
    size_t callable_bucket_cap;
    size_t callable_bucket_len;
    PreparedPureClause *clauses;
    size_t clause_len;
    size_t clause_cap;
    PreparedPureDecisionProgram *decisions;
    size_t decision_len;
    size_t decision_cap;
    PreparedPureVarSlot *pattern_vars;
    size_t pattern_var_len;
    size_t pattern_var_cap;
    PreparedPureBindPattern *bind_patterns;
    size_t bind_pattern_len;
    size_t bind_pattern_cap;
    PreparedPureBindVar *bind_vars;
    size_t bind_var_len;
    size_t bind_var_cap;
    uint32_t *live_slots;
    size_t live_slot_len;
    size_t live_slot_cap;

    PreparedPureFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    Atom **frame_atoms;
    size_t frame_atom_cap;
    Atom **values;
    size_t value_len;
    size_t value_cap;
    Atom **slots;
    size_t slot_len;
    size_t slot_cap;
    uint8_t *slot_live;
    size_t slot_live_cap;
    Atom **match_values;
    size_t match_cap;
    Atom **selected_values;
    size_t selected_cap;
    PreparedPurePatternPair *pattern_pairs;
    size_t pattern_pair_len;
    size_t pattern_pair_cap;
    PreparedPureDynamicFrame *dynamic_frames;
    size_t dynamic_frame_len;
    size_t dynamic_frame_cap;
    Atom **dynamic_values;
    size_t dynamic_value_len;
    size_t dynamic_value_cap;

    /* Call-by-need update cells.  Source expressions remain immutable Atoms;
     * this side table supplies the missing indirection/update operation while
     * preserving pointer sharing between every occurrence of a suspension. */
    Atom **memo_keys;
    size_t memo_key_cap;
    Atom **memo_values;
    size_t memo_value_cap;
    uint8_t *memo_states;
    size_t memo_state_cap;
    size_t memo_len;
    uint32_t *memo_buckets;
    size_t memo_bucket_cap;

    Arena gc_survivor;
    bool gc_survivor_ready;
    size_t gc_survivor_bytes;
    size_t gc_low_reclaim_growth_bytes;
};

enum {
    PREPARED_PURE_MAX_COMPILE_DEPTH = 256u,
    PREPARED_PURE_MAX_HEADS = 4096u,
    PREPARED_PURE_MAX_CLAUSES = 65536u,
    PREPARED_PURE_MAX_SLOTS = 65536u,
    /* Sparse decision lookup is load-bearing for large generated equation
     * families, while the direct matcher is cheaper for tiny groups. */
    PREPARED_PURE_DECISION_MIN_CLAUSES = 8u,
    PREPARED_PURE_GC_INITIAL_NURSERY_INTERVALS = 3u,
};

static bool prepared_pure_debug_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *debug = getenv("CETTA_PREPARED_PURE_DEBUG");
        enabled = debug && debug[0] != '\0' && debug[0] != '0';
    }
    return enabled != 0;
}

static bool prepared_pure_reject(
    CettaPreparedPureProgram *program, const char *reason, Atom *atom) {
    (void)program;
    if (prepared_pure_debug_enabled()) {
        fprintf(stderr, "prepared-pure compile decline: %s", reason);
        if (atom) {
            fputs(" atom=", stderr);
            atom_print(atom, stderr);
        }
        fputc('\n', stderr);
    }
    return false;
}

static bool prepared_pure_runtime_decline(
    const CettaPreparedPureProgram *program, const char *reason,
    const PreparedPureNode *node) {
    (void)program;
    (void)node;
    if (prepared_pure_debug_enabled())
        fprintf(stderr, "prepared-pure runtime decline: %s\n", reason);
    return false;
}

static bool prepared_pure_reserve(
    void **items, size_t item_size, size_t *capacity, size_t required) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 16u;
    while (next < required) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (item_size != 0u && next > SIZE_MAX / item_size)
        return false;
    void *grown = realloc(*items, item_size * next);
    if (!grown)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

static size_t prepared_pure_symbol_hash(SymbolId symbol) {
    uint64_t bits = (uint64_t)symbol;
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xc4ceb9fe1a85ec53);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static size_t prepared_pure_callable_hash(
    SymbolId head, uint32_t arity) {
    uint64_t bits = (uint64_t)prepared_pure_symbol_hash(head);
    bits ^= (uint64_t)arity + UINT64_C(0x9e3779b97f4a7c15) +
            (bits << 6u) + (bits >> 2u);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static bool prepared_pure_callable_bucket_insert(
    CettaPreparedPureProgram *program,
    PreparedPureCallableCacheEntry entry) {
    if (!program || !entry.occupied || entry.head == SYMBOL_ID_NONE ||
        program->callable_bucket_cap == 0u)
        return false;
    size_t mask = program->callable_bucket_cap - 1u;
    size_t bucket = prepared_pure_callable_hash(
        entry.head, entry.arity) & mask;
    for (size_t probe = 0u;
         probe < program->callable_bucket_cap; probe++) {
        PreparedPureCallableCacheEntry *slot =
            &program->callable_buckets[bucket];
        if (!slot->occupied) {
            *slot = entry;
            program->callable_bucket_len++;
            return true;
        }
        if (slot->head == entry.head && slot->arity == entry.arity) {
            slot->callable = entry.callable;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_callable_buckets_rebuild(
    CettaPreparedPureProgram *program, size_t required_entries) {
    if (!program || required_entries > SIZE_MAX / 2u)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_entries * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->callable_bucket_cap >= required_buckets)
        return true;
    PreparedPureCallableCacheEntry *previous =
        program->callable_buckets;
    size_t previous_cap = program->callable_bucket_cap;
    PreparedPureCallableCacheEntry *grown = calloc(
        required_buckets, sizeof(*grown));
    if (!grown)
        return false;
    program->callable_buckets = grown;
    program->callable_bucket_cap = required_buckets;
    program->callable_bucket_len = 0u;
    for (size_t i = 0u; i < previous_cap; i++) {
        if (previous[i].occupied &&
            !prepared_pure_callable_bucket_insert(
                program, previous[i])) {
            free(previous);
            return false;
        }
    }
    free(previous);
    return true;
}

static bool prepared_pure_callable_cache_lookup(
    const CettaPreparedPureProgram *program, SymbolId head,
    uint32_t arity, bool *callable) {
    if (!program || head == SYMBOL_ID_NONE || !callable ||
        program->callable_bucket_cap == 0u)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_LOOKUP);
    size_t mask = program->callable_bucket_cap - 1u;
    size_t bucket = prepared_pure_callable_hash(head, arity) & mask;
    for (size_t probe = 0u;
         probe < program->callable_bucket_cap; probe++) {
        const PreparedPureCallableCacheEntry *slot =
            &program->callable_buckets[bucket];
        if (!slot->occupied)
            return false;
        if (slot->head == head && slot->arity == arity) {
            *callable = slot->callable;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_HIT);
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_callable_cache_store(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t arity, bool callable) {
    if (!program || head == SYMBOL_ID_NONE)
        return false;
    if (program->callable_bucket_cap == 0u ||
        program->callable_bucket_len + 1u >
            program->callable_bucket_cap / 2u) {
        if (!prepared_pure_callable_buckets_rebuild(
                program, program->callable_bucket_len + 1u))
            return false;
    }
    return prepared_pure_callable_bucket_insert(
        program, (PreparedPureCallableCacheEntry){
            .head = head,
            .arity = arity,
            .callable = callable,
            .occupied = true,
        });
}

static bool prepared_pure_head_bucket_insert(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len ||
        program->head_bucket_cap == 0u)
        return false;
    size_t mask = program->head_bucket_cap - 1u;
    size_t bucket = prepared_pure_symbol_hash(
        program->heads[head_index].head) & mask;
    for (size_t probe = 0u; probe < program->head_bucket_cap; probe++) {
        if (program->head_buckets[bucket] == 0u) {
            program->head_buckets[bucket] = head_index + 1u;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_head_buckets_rebuild(
    CettaPreparedPureProgram *program, size_t required_heads) {
    if (!program || required_heads > UINT32_MAX)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_heads * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->head_bucket_cap >= required_buckets)
        return true;
    uint32_t *grown = realloc(
        program->head_buckets,
        sizeof(*program->head_buckets) * required_buckets);
    if (!grown)
        return false;
    program->head_buckets = grown;
    program->head_bucket_cap = required_buckets;
    memset(program->head_buckets, 0,
           sizeof(*program->head_buckets) * program->head_bucket_cap);
    for (uint32_t i = 0u; i < program->head_len; i++) {
        if (!prepared_pure_head_bucket_insert(program, i))
            return false;
    }
    return true;
}

static bool prepared_pure_head_bucket_lookup(
    const CettaPreparedPureProgram *program, SymbolId symbol,
    uint32_t *head_index) {
    if (!program || symbol == SYMBOL_ID_NONE || !head_index ||
        program->head_bucket_cap == 0u)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_HEAD_LOOKUP);
    size_t mask = program->head_bucket_cap - 1u;
    size_t bucket = prepared_pure_symbol_hash(symbol) & mask;
    for (size_t probe = 0u; probe < program->head_bucket_cap; probe++) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_HEAD_PROBE);
        uint32_t encoded = program->head_buckets[bucket];
        if (encoded == 0u)
            return false;
        uint32_t index = encoded - 1u;
        if (index < program->head_len &&
            program->heads[index].head == symbol) {
            *head_index = index;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static size_t prepared_pure_memo_hash(Atom *key) {
    uintptr_t bits = (uintptr_t)key;
    bits ^= bits >> 17u;
    bits *= (uintptr_t)UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11u;
    return (size_t)bits;
}

static bool prepared_pure_memo_insert_bucket(
    CettaPreparedPureProgram *program, uint32_t index) {
    if (!program || index >= program->memo_len ||
        !program->memo_keys[index] || program->memo_bucket_cap == 0u)
        return false;
    size_t mask = program->memo_bucket_cap - 1u;
    size_t bucket = prepared_pure_memo_hash(
        program->memo_keys[index]) & mask;
    for (size_t probe = 0u; probe < program->memo_bucket_cap; probe++) {
        if (program->memo_buckets[bucket] == 0u) {
            program->memo_buckets[bucket] = index + 1u;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

/* Moving collection rewrites the live ephemeron keys, so the pointer index is
 * rebuilt after every generated evacuation/compaction pass. */
static bool prepared_pure_memo_rebuild_buckets(
    CettaPreparedPureProgram *program, size_t required_entries) {
    if (!program || required_entries > UINT32_MAX)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_entries * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->memo_bucket_cap < required_buckets) {
        uint32_t *grown = realloc(
            program->memo_buckets,
            sizeof(*program->memo_buckets) * required_buckets);
        if (!grown)
            return false;
        program->memo_buckets = grown;
        program->memo_bucket_cap = required_buckets;
    }
    memset(program->memo_buckets, 0,
           sizeof(*program->memo_buckets) * program->memo_bucket_cap);
    for (uint32_t i = 0u; i < program->memo_len; i++) {
        if (!prepared_pure_memo_insert_bucket(program, i))
            return false;
    }
    return true;
}

static bool prepared_pure_memo_lookup(
    const CettaPreparedPureProgram *program, Atom *key,
    uint32_t *index_out) {
    if (index_out)
        *index_out = PREPARED_PURE_NO_MEMO;
    if (!program || !key || !index_out || program->memo_bucket_cap == 0u)
        return false;
    size_t mask = program->memo_bucket_cap - 1u;
    size_t bucket = prepared_pure_memo_hash(key) & mask;
    for (size_t probe = 0u; probe < program->memo_bucket_cap; probe++) {
        uint32_t encoded = program->memo_buckets[bucket];
        if (encoded == 0u)
            return false;
        uint32_t index = encoded - 1u;
        if (index < program->memo_len &&
            program->memo_keys[index] == key) {
            *index_out = index;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_memo_begin(
    CettaPreparedPureProgram *program, Atom *key,
    uint32_t *index_out, bool *existing_out) {
    if (index_out)
        *index_out = PREPARED_PURE_NO_MEMO;
    if (existing_out)
        *existing_out = false;
    if (!program || !key || !index_out || !existing_out)
        return false;
    if (prepared_pure_memo_lookup(program, key, index_out)) {
        *existing_out = true;
        return true;
    }
    if (program->memo_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->memo_keys, sizeof(*program->memo_keys),
            &program->memo_key_cap, program->memo_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->memo_values, sizeof(*program->memo_values),
            &program->memo_value_cap, program->memo_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->memo_states, sizeof(*program->memo_states),
            &program->memo_state_cap, program->memo_len + 1u))
        return false;
    if (program->memo_bucket_cap == 0u ||
        (program->memo_len + 1u) * 10u >=
            program->memo_bucket_cap * 7u) {
        if (!prepared_pure_memo_rebuild_buckets(
                program, program->memo_len + 1u))
            return false;
    }
    uint32_t index = (uint32_t)program->memo_len++;
    program->memo_keys[index] = key;
    program->memo_values[index] = NULL;
    program->memo_states[index] = PREPARED_PURE_MEMO_EVALUATING;
    if (!prepared_pure_memo_insert_bucket(program, index)) {
        program->memo_len--;
        return false;
    }
    *index_out = index;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_STORE);
    return true;
}

static bool prepared_pure_memo_complete(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame, Atom *value) {
    if (!program || !frame || !value)
        return false;
    if (frame->memo_index == PREPARED_PURE_NO_MEMO)
        return true;
    if (frame->memo_index >= program->memo_len ||
        program->memo_states[frame->memo_index] !=
            PREPARED_PURE_MEMO_EVALUATING)
        return false;
    program->memo_values[frame->memo_index] = value;
    program->memo_states[frame->memo_index] = PREPARED_PURE_MEMO_VALUE;
    return true;
}

static bool prepared_pure_memo_defer_tail(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame) {
    if (!program || !frame)
        return false;
    if (frame->memo_index == PREPARED_PURE_NO_MEMO)
        return true;
    if (frame->memo_index >= program->memo_len ||
        program->memo_states[frame->memo_index] !=
            PREPARED_PURE_MEMO_EVALUATING ||
        program->memo_values[frame->memo_index] != NULL)
        return false;
    program->memo_states[frame->memo_index] =
        PREPARED_PURE_MEMO_TAIL_PENDING;
    return true;
}

static bool prepared_pure_memo_complete_deferred(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value)
        return false;
    for (size_t i = 0u; i < program->memo_len; i++) {
        if (program->memo_states[i] !=
            PREPARED_PURE_MEMO_TAIL_PENDING)
            continue;
        if (program->memo_values[i] != NULL)
            return false;
        program->memo_values[i] = value;
        program->memo_states[i] = PREPARED_PURE_MEMO_VALUE;
    }
    return true;
}

static void prepared_pure_memo_clear(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    if (program->memo_len > 0u) {
        memset(program->memo_keys, 0,
               sizeof(*program->memo_keys) * program->memo_len);
        memset(program->memo_values, 0,
               sizeof(*program->memo_values) * program->memo_len);
        memset(program->memo_states, 0,
               sizeof(*program->memo_states) * program->memo_len);
    }
    program->memo_len = 0u;
    if (program->memo_buckets && program->memo_bucket_cap > 0u)
        memset(program->memo_buckets, 0,
               sizeof(*program->memo_buckets) *
                   program->memo_bucket_cap);
}

static size_t prepared_pure_saturating_add(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static size_t prepared_pure_saturating_mul(size_t value, size_t multiplier) {
    return multiplier != 0u && value > SIZE_MAX / multiplier
        ? SIZE_MAX : value * multiplier;
}

static size_t prepared_pure_arena_bytes_above(
    const Arena *arena, ArenaMark anchor) {
    size_t live = arena_accounted_live_bytes(arena);
    size_t base = arena_mark_accounted_live_bytes(anchor);
    return live >= base ? live - base : 0u;
}

static bool prepared_pure_plan_builds_structural_values(
    const CettaPreparedPureProgram *program) {
    if (!program)
        return false;
    for (size_t index = 0u; index < program->node_len; index++) {
        const PreparedPureNode *node = &program->nodes[index];
        if (node->kind == PREPARED_PURE_BUILD ||
            (node->kind == PREPARED_PURE_INTRINSIC &&
             node->intrinsic_instruction ==
                 CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION))
            return true;
    }
    return false;
}

static size_t prepared_pure_gc_trigger_bytes(
    const CettaPreparedPureProgram *program, const Arena *arena,
    ArenaMark anchor, size_t nursery_budget_bytes) {
    if (!program || !arena || nursery_budget_bytes == 0u)
        return SIZE_MAX;
    size_t fresh_threshold = prepared_pure_saturating_add(
        nursery_budget_bytes, program->gc_survivor_bytes);
    fresh_threshold = prepared_pure_saturating_add(
        fresh_threshold, program->gc_low_reclaim_growth_bytes);
    /* The first collection is also the first survival sample.  A generated
     * plan which explicitly builds structural values gets three bounded
     * nursery intervals before paying for that sample; scalar/register-only
     * plans retain the ordinary early collection needed for arithmetic
     * churn.  Every later interval is determined by measured survival and
     * reclamation below. */
    if (!program->gc_survivor_ready &&
        prepared_pure_plan_builds_structural_values(program)) {
        fresh_threshold = prepared_pure_saturating_add(
            fresh_threshold,
            prepared_pure_saturating_mul(
                nursery_budget_bytes,
                PREPARED_PURE_GC_INITIAL_NURSERY_INTERVALS - 1u));
    }
    return prepared_pure_saturating_add(
        arena_mark_accounted_live_bytes(anchor), fresh_threshold);
}

static bool prepared_pure_mark_node_live_slots(
    CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t local_base) {
    if (!program || node_index >= program->node_len)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->first_live_slot > program->live_slot_len ||
        node->live_slot_count >
            program->live_slot_len - node->first_live_slot)
        return false;
    for (uint32_t i = 0u; i < node->live_slot_count; i++) {
        uint32_t relative =
            program->live_slots[node->first_live_slot + i];
        size_t absolute = (size_t)local_base + relative;
        if (absolute >= program->slot_len)
            return false;
        program->slot_live[absolute] = 1u;
    }
    return true;
}

static bool prepared_pure_mark_bind_prebound_slots(
    CettaPreparedPureProgram *program, const PreparedPureNode *node,
    uint32_t local_base) {
    if (!program || !node || node->auxiliary >= program->bind_pattern_len)
        return false;
    const PreparedPureBindPattern *pattern =
        &program->bind_patterns[node->auxiliary];
    if (pattern->first_var > program->bind_var_len ||
        pattern->var_count >
            program->bind_var_len - pattern->first_var)
        return false;
    for (uint32_t i = 0u; i < pattern->var_count; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[pattern->first_var + i];
        if (!binding->prebound)
            continue;
        size_t absolute = (size_t)local_base + binding->slot;
        if (absolute >= program->slot_len)
            return false;
        program->slot_live[absolute] = 1u;
    }
    return true;
}

static bool prepared_pure_mark_frame_live_slots(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame) {
    if (!program || !frame)
        return false;
    if (frame->node == PREPARED_PURE_RUNTIME_NODE)
        return true;
    if (frame->node >= program->node_len)
        return false;
    const PreparedPureNode *node = &program->nodes[frame->node];

    if (node->kind == PREPARED_PURE_LITERAL ||
        node->kind == PREPARED_PURE_ENTRY_ARGUMENT ||
        node->kind == PREPARED_PURE_EVAL_ENTRY_ARGUMENT)
        return true;
    if (node->kind == PREPARED_PURE_SLOT ||
        node->kind == PREPARED_PURE_EVAL_SLOT) {
        return frame->state != 0u ||
               prepared_pure_mark_node_live_slots(
                   program, frame->node, frame->local_base);
    }
    if (node->kind == PREPARED_PURE_BIND) {
        if (frame->state == 0u)
            return prepared_pure_mark_node_live_slots(
                program, frame->node, frame->local_base);
        if (frame->state == 1u) {
            return prepared_pure_mark_bind_prebound_slots(
                       program, node, frame->local_base) &&
                   node->child_count == 2u &&
                   prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 1u],
                       frame->local_base);
        }
        return true;
    }
    if (node->kind == PREPARED_PURE_IF) {
        if (frame->state == 0u)
            return prepared_pure_mark_node_live_slots(
                program, frame->node, frame->local_base);
        if (frame->state == 1u) {
            if (node->child_count != 3u)
                return false;
            return prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 1u],
                       frame->local_base) &&
                   prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 2u],
                       frame->local_base);
        }
        return true;
    }

    if (frame->state == 0u)
        return prepared_pure_mark_node_live_slots(
            program, frame->node, frame->local_base);
    if (frame->state != 1u)
        return true;
    if (frame->child_index >= node->child_count)
        return frame->child_index == node->child_count;
    for (uint32_t i = frame->child_index + 1u;
         i < node->child_count; i++) {
        if (!prepared_pure_mark_node_live_slots(
                program, program->children[node->first_child + i],
                frame->local_base))
            return false;
    }
    return true;
}

/* Compile-time node descriptors determine the exact positional environment
 * roots of every active continuation state.  Clearing the complement before
 * evacuation both trims environments and prevents stale from-space pointers
 * from surviving in slots the machine has proved it will never read again. */
static bool prepared_pure_trim_dead_slots(
    CettaPreparedPureProgram *program) {
    if (!program || !prepared_pure_reserve(
            (void **)&program->slot_live, sizeof(*program->slot_live),
            &program->slot_live_cap, program->slot_len))
        return false;
    if (program->slot_len > 0u)
        memset(program->slot_live, 0, program->slot_len);
    for (size_t i = 0u; i < program->frame_len; i++) {
        if (!prepared_pure_mark_frame_live_slots(
                program, &program->frames[i]))
            return false;
    }
    uint64_t live = 0u;
    uint64_t trimmed = 0u;
    for (size_t i = 0u; i < program->slot_len; i++) {
        if (program->slot_live[i]) {
            if (program->slots[i])
                live++;
        } else if (program->slots[i]) {
            program->slots[i] = NULL;
            trimmed++;
        }
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_DEAD_SLOTS,
        trimmed);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_LIVE_SLOTS_PEAK,
        live);
    return true;
}

static void prepared_pure_gc_discard_survivor(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    program->gc_low_reclaim_growth_bytes = 0u;
    if (!program->gc_survivor_ready)
        return;
    arena_free(&program->gc_survivor);
    memset(&program->gc_survivor, 0, sizeof(program->gc_survivor));
    program->gc_survivor_ready = false;
    program->gc_survivor_bytes = 0u;
}

/* Resolve completed update cells before the generic copier traverses their
 * immutable suspension syntax.  This is graph-update path compression: the
 * copied continuation points directly at the value, so evaluated thunk
 * spines do not survive merely because the source Atom graph was immutable. */
static Atom *prepared_pure_gc_resolve_thunk(
    void *context, Atom *source) {
    CettaPreparedPureProgram *program = context;
    if (!program || !source)
        return NULL;
    for (size_t hop = 0u; hop <= program->memo_len; hop++) {
        uint32_t index = PREPARED_PURE_NO_MEMO;
        if (!prepared_pure_memo_lookup(program, source, &index) ||
            program->memo_states[index] != PREPARED_PURE_MEMO_VALUE)
            return source;
        Atom *next = program->memo_values[index];
        if (!next || next == source)
            return NULL;
        source = next;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_PATH_COMPRESSION);
    }
    return NULL;
}

/* Trace the update table as an ephemeron map.  Strong frame/register roots
 * are copied first by the generated arm.  A memoized value is copied only
 * when that episode has already forwarded its suspension key; copied values
 * can expose more keys, so the scan continues to a least fixed point.
 * Compaction also rewrites every active frame's memo index atomically with
 * the table, preserving the update-cell invariant across moving collection. */
static bool prepared_pure_gc_visit_thunk_updates(
    AtomDeepCopySession *session, PreparedPureEphemeronAtomMap map) {
    CettaPreparedPureProgram *program = map.program;
    if (!session || !program)
        return false;
    size_t old_len = program->memo_len;
    if (old_len == 0u)
        return true;
    if (old_len > SIZE_MAX / sizeof(uint32_t))
        return false;

    uint8_t *keep = calloc(old_len, sizeof(*keep));
    uint32_t *remap = malloc(old_len * sizeof(*remap));
    if (!keep || !remap) {
        free(remap);
        free(keep);
        return false;
    }
    for (size_t i = 0u; i < old_len; i++)
        remap[i] = PREPARED_PURE_NO_MEMO;

    bool changed;
    do {
        changed = false;
        for (size_t i = 0u; i < old_len; i++) {
            if (keep[i])
                continue;
            Atom *old_key = program->memo_keys[i];
            Atom *new_key = atom_deep_copy_session_forwarded(
                session, old_key);
            if (!new_key)
                continue;

            Atom *new_value = NULL;
            if (program->memo_states[i] == PREPARED_PURE_MEMO_VALUE) {
                Atom *old_value = program->memo_values[i];
                if (!old_value || !(new_value =
                        atom_deep_copy_session_copy(session, old_value))) {
                    free(remap);
                    free(keep);
                    return false;
                }
            } else if ((program->memo_states[i] !=
                            PREPARED_PURE_MEMO_EVALUATING &&
                        program->memo_states[i] !=
                            PREPARED_PURE_MEMO_TAIL_PENDING) ||
                       program->memo_values[i] != NULL) {
                free(remap);
                free(keep);
                return false;
            }
            program->memo_keys[i] = new_key;
            program->memo_values[i] = new_value;
            keep[i] = 1u;
            changed = true;
        }
    } while (changed);

    size_t write = 0u;
    for (size_t read = 0u; read < old_len; read++) {
        if (!keep[read])
            continue;
        remap[read] = (uint32_t)write;
        if (write != read) {
            program->memo_keys[write] = program->memo_keys[read];
            program->memo_values[write] = program->memo_values[read];
            program->memo_states[write] = program->memo_states[read];
        }
        write++;
    }
    for (size_t i = write; i < old_len; i++) {
        program->memo_keys[i] = NULL;
        program->memo_values[i] = NULL;
        program->memo_states[i] = 0u;
    }
    for (size_t i = 0u; i < program->frame_len; i++) {
        uint32_t old_index = program->frames[i].memo_index;
        if (old_index == PREPARED_PURE_NO_MEMO)
            continue;
        if (old_index >= old_len ||
            remap[old_index] == PREPARED_PURE_NO_MEMO) {
            free(remap);
            free(keep);
            return false;
        }
        program->frames[i].memo_index = remap[old_index];
    }
    program->memo_len = write;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_EPHEMERON_LIVE_PEAK,
        (uint64_t)write);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_EPHEMERON_RECLAIMED,
        (uint64_t)(old_len - write));
    free(remap);
    free(keep);
    return true;
}

static void prepared_pure_gc_collect(
    CettaPreparedPureProgram *program, Arena *arena, ArenaMark anchor) {
    if (!program || !arena)
        return;
    if (!prepared_pure_trim_dead_slots(program))
        return;

    size_t before_fresh = prepared_pure_arena_bytes_above(arena, anchor);
    size_t before_survivor = program->gc_survivor_ready
        ? arena_accounted_live_bytes(&program->gc_survivor) : 0u;
    size_t before = prepared_pure_saturating_add(
        before_fresh, before_survivor);

    Arena evacuated;
    arena_init(&evacuated);
    arena_set_hashcons(&evacuated, NULL);
    arena_set_runtime_kind(
        &evacuated, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&evacuated);
    if (!session) {
        arena_free(&evacuated);
        return;
    }
    atom_deep_copy_session_set_resolver(
        session, prepared_pure_gc_resolve_thunk, program);

    PreparedPureGcRoots roots = {
        .values = {program->values, program->value_len},
        .live_slots = {program->slots, program->slot_len},
        .runtime_frames = {program->frame_atoms, program->frame_len},
        .thunk_updates = {program},
    };
    bool copied = true;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN, FAIL) do {          \
    PreparedPureAtomSpan prepared_pure_span__ = (SPAN);                   \
    for (size_t prepared_pure_i__ = 0u;                                  \
         prepared_pure_i__ < prepared_pure_span__.len;                   \
         prepared_pure_i__++) {                                          \
        Atom **prepared_pure_slot__ =                                    \
            &prepared_pure_span__.items[prepared_pure_i__];              \
        if (!*prepared_pure_slot__)                                      \
            continue;                                                    \
        Atom *prepared_pure_next__ = atom_deep_copy_session_copy(        \
            (SESSION), *prepared_pure_slot__);                           \
        if (!prepared_pure_next__) { FAIL; }                             \
        *prepared_pure_slot__ = prepared_pure_next__;                    \
    }                                                                    \
} while (0)
#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL) do {         \
    if (!prepared_pure_gc_visit_thunk_updates((SESSION), (MAP))) {        \
        FAIL;                                                             \
    }                                                                     \
} while (0)
    CETTA_EVAL_GC_ARM_prepared_pure_machine(
        session, &roots, goto copy_failed);
    if (!prepared_pure_memo_rebuild_buckets(
            program, program->memo_len))
        goto copy_failed;
    goto copy_finished;
copy_failed:
    copied = false;
copy_finished:
#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    atom_deep_copy_session_free(session);
    if (!copied) {
        /* A prior root may already point into the new semispace. */
        fputs("fatal: prepared pure-machine evacuation failed\n", stderr);
        abort();
    }

    arena_reset(arena, anchor);
    if (program->gc_survivor_ready)
        arena_free(&program->gc_survivor);
    arena_set_runtime_kind(&evacuated, CETTA_ARENA_RUNTIME_KIND_OTHER);
    program->gc_survivor = evacuated;
    memset(&evacuated, 0, sizeof(evacuated));
    arena_set_runtime_kind(
        &program->gc_survivor, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    arena_set_hashcons(&program->gc_survivor, NULL);
    program->gc_survivor_ready = true;
    program->gc_survivor_bytes =
        arena_accounted_live_bytes(&program->gc_survivor);

    size_t reclaimed = before > program->gc_survivor_bytes
        ? before - program->gc_survivor_bytes : 0u;
    /* A copying collection that retains at least three quarters of its
     * input is dominated by immutable live structure, not garbage.  Give
     * that live graph one additional survivor-sized growth interval before
     * copying it again.  Reclamation-rich calls retain the ordinary budget,
     * and this evidence is reset with the invocation's survivor arena. */
    program->gc_low_reclaim_growth_bytes =
        before > 0u && reclaimed <= before / 4u
            ? program->gc_survivor_bytes : 0u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_COLLECTION);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_EVACUATED_BYTES,
        (uint64_t)program->gc_survivor_bytes);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_RECLAIMED_BYTES,
        (uint64_t)reclaimed);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_SURVIVOR_BYTES_PEAK,
        (uint64_t)program->gc_survivor_bytes);
    (void)reclaimed;
}

static bool prepared_pure_control_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltFoldControl *control_out) {
#define PREPARED_PURE_CONTROL(field, expected_arity, control) \
    if (head == g_builtin_syms.field && arity == (expected_arity)) { \
        if (control_out) \
            *control_out = (control); \
        return true; \
    }
    CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(PREPARED_PURE_CONTROL)
#undef PREPARED_PURE_CONTROL
    return false;
}

static bool prepared_pure_register_program(
    const CettaPreparedPureProgram *program,
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out) {
#define PREPARED_PURE_REGISTER(field, expected_arity, result_kind, instruction) \
    if (head == g_builtin_syms.field && arity == (expected_arity)) { \
        if (kind_out) \
            *kind_out = (result_kind); \
        if (instruction_out) \
            *instruction_out = (instruction); \
        return true; \
    }
    CETTA_GSLT_REGISTER_HEAD_ROWS(PREPARED_PURE_REGISTER)
#undef PREPARED_PURE_REGISTER
    if (!program || !program->register_view)
        return false;
    CettaGsltRegisterResultKind kind;
    CettaGsltRegisterInstruction instruction;
    CettaGsltRegisterOperandDiscipline discipline;
    if (!program->register_view(
            head, arity, &kind, &instruction) ||
        (kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER &&
         kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN) ||
        !cetta_gslt_register_operand_discipline(
            instruction, &discipline))
        return false;
    (void)discipline;
    if (kind_out)
        *kind_out = kind;
    if (instruction_out)
        *instruction_out = instruction;
    return true;
}

static bool prepared_pure_intrinsic_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltPreparedPureIntrinsicInstruction *instruction_out) {
#define PREPARED_PURE_INTRINSIC(                                        \
    field, expected_arity, discipline, instruction)                    \
    if (head == g_builtin_syms.field && arity == (expected_arity)) {    \
        if ((discipline) !=                                             \
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL)    \
            return false;                                               \
        if (instruction_out)                                            \
            *instruction_out = (instruction);                           \
        return true;                                                    \
    }
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(
        PREPARED_PURE_INTRINSIC)
#undef PREPARED_PURE_INTRINSIC
    return false;
}

static bool prepared_pure_builtin_syntax(SymbolId head) {
    return symbol_id_is_builtin(head);
}

static bool prepared_pure_context_lookup(
    const PreparedPureCompileContext *context, VarId var,
    uint32_t *slot_out) {
    if (!context || var == VAR_ID_NONE)
        return false;
    for (size_t i = context->len; i > 0u; i--) {
        if (context->bindings[i - 1u].var == var) {
            if (slot_out)
                *slot_out = context->bindings[i - 1u].slot;
            return true;
        }
    }
    return false;
}

static bool prepared_pure_context_bind(
    PreparedPureCompileContext *context, VarId var,
    bool allow_shadow, uint32_t *slot_out) {
    if (!context || var == VAR_ID_NONE ||
        context->next_slot >= PREPARED_PURE_MAX_SLOTS)
        return false;
    uint32_t existing = 0u;
    if (!allow_shadow &&
        prepared_pure_context_lookup(context, var, &existing))
        return false;
    if (!prepared_pure_reserve(
            (void **)&context->bindings, sizeof(*context->bindings),
            &context->cap, context->len + 1u))
        return false;
    uint32_t slot = context->next_slot++;
    context->bindings[context->len++] = (PreparedPureVarSlot){
        .var = var,
        .slot = slot,
    };
    if (slot_out)
        *slot_out = slot;
    return true;
}

static bool prepared_pure_bind_var_index(
    const CettaPreparedPureProgram *program,
    uint32_t first_var, uint32_t var_count, VarId var,
    uint32_t *index_out) {
    if (!program || !index_out || var == VAR_ID_NONE ||
        first_var > program->bind_var_len ||
        var_count > program->bind_var_len - first_var)
        return false;
    for (uint32_t i = 0u; i < var_count; i++) {
        if (program->bind_vars[first_var + i].var == var) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

/* Compile a binder pattern into positional slot actions.  A variable already
 * visible at the binder is a rigid reference; the first occurrence of a fresh
 * variable allocates a slot, and later occurrences share that same slot. */
static bool prepared_pure_compile_bind_pattern(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context, Atom *pattern,
    uint32_t *pattern_index_out) {
    if (!program || !context || !pattern || !pattern_index_out ||
        program->bind_pattern_len >= UINT32_MAX ||
        program->bind_var_len >= UINT32_MAX)
        return false;

    const size_t saved_context_len = context->len;
    const uint32_t saved_next_slot = context->next_slot;
    const size_t saved_bind_var_len = program->bind_var_len;
    const uint32_t first_var = (uint32_t)saved_bind_var_len;
    Atom **stack = NULL;
    size_t stack_len = 0u;
    size_t stack_cap = 0u;
    bool ok = prepared_pure_reserve(
        (void **)&stack, sizeof(*stack), &stack_cap, 1u);
    if (ok)
        stack[stack_len++] = pattern;

    while (ok && stack_len > 0u) {
        Atom *current = stack[--stack_len];
        if (!current) {
            ok = false;
            break;
        }
        if (current->kind == ATOM_VAR) {
            uint32_t ignored = 0u;
            uint32_t count = (uint32_t)(
                program->bind_var_len - saved_bind_var_len);
            if (prepared_pure_bind_var_index(
                    program, first_var, count,
                    current->var_id, &ignored))
                continue;

            uint32_t slot = 0u;
            bool prebound = prepared_pure_context_lookup(
                context, current->var_id, &slot);
            if (!prebound && !prepared_pure_context_bind(
                    context, current->var_id, false, &slot)) {
                ok = false;
                break;
            }
            if (program->bind_var_len >= UINT32_MAX ||
                !prepared_pure_reserve(
                    (void **)&program->bind_vars,
                    sizeof(*program->bind_vars),
                    &program->bind_var_cap,
                    program->bind_var_len + 1u)) {
                ok = false;
                break;
            }
            program->bind_vars[program->bind_var_len++] =
                (PreparedPureBindVar){
                    .var = current->var_id,
                    .slot = slot,
                    .prebound = prebound,
                };
            continue;
        }
        if (current->kind != ATOM_EXPR)
            continue;
        if (!prepared_pure_reserve(
                (void **)&stack, sizeof(*stack), &stack_cap,
                stack_len + current->expr.len)) {
            ok = false;
            break;
        }
        for (CettaExprIndex i = current->expr.len; i > 0u; i--)
            stack[stack_len++] = current->expr.elems[i - 1u];
    }
    free(stack);

    size_t count_size = program->bind_var_len - saved_bind_var_len;
    if (!ok || count_size > UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->bind_patterns,
            sizeof(*program->bind_patterns),
            &program->bind_pattern_cap,
            program->bind_pattern_len + 1u)) {
        context->len = saved_context_len;
        context->next_slot = saved_next_slot;
        program->bind_var_len = saved_bind_var_len;
        return false;
    }
    *pattern_index_out = (uint32_t)program->bind_pattern_len;
    program->bind_patterns[program->bind_pattern_len++] =
        (PreparedPureBindPattern){
            .pattern = pattern,
            .first_var = first_var,
            .var_count = (uint32_t)count_size,
        };
    return true;
}

static bool prepared_pure_append_node_live_slot(
    CettaPreparedPureProgram *program, size_t first, uint32_t slot) {
    if (!program || first > program->live_slot_len)
        return false;
    for (size_t i = first; i < program->live_slot_len; i++) {
        if (program->live_slots[i] == slot)
            return true;
    }
    if (program->live_slot_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->live_slots,
            sizeof(*program->live_slots), &program->live_slot_cap,
            program->live_slot_len + 1u))
        return false;
    program->live_slots[program->live_slot_len++] = slot;
    return true;
}

static bool prepared_pure_add_node(
    CettaPreparedPureProgram *program, PreparedPureNode node,
    const uint32_t *children, uint32_t child_count,
    uint32_t *node_out) {
    if (!program || !node_out ||
        program->node_len >= UINT32_MAX ||
        program->child_len > UINT32_MAX - child_count)
        return false;
    if (!prepared_pure_reserve(
            (void **)&program->children, sizeof(*program->children),
            &program->child_cap, program->child_len + child_count) ||
        !prepared_pure_reserve(
            (void **)&program->nodes, sizeof(*program->nodes),
            &program->node_cap, program->node_len + 1u))
        return false;

    size_t first_live = program->live_slot_len;
    bool live_ok = true;
    if (node.kind == PREPARED_PURE_SLOT ||
        node.kind == PREPARED_PURE_EVAL_SLOT) {
        live_ok = prepared_pure_append_node_live_slot(
            program, first_live, node.auxiliary);
    } else if (node.kind == PREPARED_PURE_BIND) {
        if (node.auxiliary >= program->bind_pattern_len) {
            live_ok = false;
        } else {
            const PreparedPureBindPattern *pattern =
                &program->bind_patterns[node.auxiliary];
            if (pattern->first_var > program->bind_var_len ||
                pattern->var_count >
                    program->bind_var_len - pattern->first_var) {
                live_ok = false;
            } else {
                for (uint32_t i = 0u;
                     live_ok && i < pattern->var_count; i++) {
                    const PreparedPureBindVar *binding =
                        &program->bind_vars[pattern->first_var + i];
                    if (binding->prebound)
                        live_ok = prepared_pure_append_node_live_slot(
                            program, first_live, binding->slot);
                }
            }
        }
    }
    for (uint32_t i = 0u; live_ok && i < child_count; i++) {
        if (children[i] >= program->node_len) {
            live_ok = false;
            break;
        }
        const PreparedPureNode *child = &program->nodes[children[i]];
        if (child->first_live_slot > program->live_slot_len ||
            child->live_slot_count >
                program->live_slot_len - child->first_live_slot) {
            live_ok = false;
            break;
        }
        for (uint32_t j = 0u;
             live_ok && j < child->live_slot_count; j++) {
            live_ok = prepared_pure_append_node_live_slot(
                program, first_live,
                program->live_slots[child->first_live_slot + j]);
        }
    }
    size_t live_count = program->live_slot_len - first_live;
    if (!live_ok || first_live > UINT32_MAX || live_count > UINT32_MAX) {
        program->live_slot_len = first_live;
        return false;
    }
    node.first_live_slot = (uint32_t)first_live;
    node.live_slot_count = (uint32_t)live_count;
    node.first_child = (uint32_t)program->child_len;
    node.child_count = child_count;
    if (child_count > 0u)
        memcpy(&program->children[program->child_len], children,
               sizeof(*children) * child_count);
    program->child_len += child_count;
    *node_out = (uint32_t)program->node_len;
    program->nodes[program->node_len++] = node;
    return true;
}

static bool prepared_pure_head_index_admitted(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *index_out) {
    if (!program || head == SYMBOL_ID_NONE || !index_out)
        return false;
    if (prepared_pure_head_bucket_lookup(program, head, index_out))
        return true;
    if (program->head_len >= PREPARED_PURE_MAX_HEADS ||
        program->head_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->heads, sizeof(*program->heads),
            &program->head_cap, program->head_len + 1u) ||
        !prepared_pure_head_buckets_rebuild(
            program, program->head_len + 1u))
        return false;
    if (!space_read_token_is_current(program->read))
        return false;
    *index_out = (uint32_t)program->head_len;
    program->heads[program->head_len++] = (PreparedPureHead){
        .head = head,
    };
    return prepared_pure_head_bucket_insert(program, *index_out);
}

static bool prepared_pure_head_index(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *index_out) {
    if (!program || head == SYMBOL_ID_NONE || !index_out)
        return false;
    if (prepared_pure_head_bucket_lookup(program, head, index_out))
        return true;
    bool defined = false;
    if (space_query_effect_for_head(
            program->space, head, &defined) !=
            CETTA_GSLT_QUERY_EFFECT_PURE ||
        !defined || !space_read_token_is_current(program->read)) {
        return prepared_pure_reject(
            program, "user head is not revision-pinned pure", NULL);
    }
    return prepared_pure_head_index_admitted(
        program, head, index_out);
}

static bool prepared_pure_compile_template(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, uint32_t depth, bool require_inert_head,
    uint32_t *node_out);

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, uint32_t depth, uint32_t *node_out);

static bool prepared_pure_compile_children(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom **source, CettaExprLen count, uint32_t depth,
    bool evaluate, bool require_inert_templates,
    uint32_t **children_out) {
    if (!children_out ||
        (count > 0u && (size_t)count > SIZE_MAX / sizeof(uint32_t)))
        return false;
    uint32_t *children = count ? malloc(sizeof(*children) * count) : NULL;
    if (count && !children)
        return false;
    for (CettaExprIndex i = 0u; i < count; i++) {
        bool ok = evaluate
            ? prepared_pure_compile_eval(
                  program, context, source[i], depth + 1u, &children[i])
            : prepared_pure_compile_template(
                  program, context, source[i], depth + 1u,
                  require_inert_templates, &children[i]);
        if (!ok) {
            free(children);
            return prepared_pure_reject(
                program,
                evaluate
                    ? "evaluated child is outside the pure machine fragment"
                    : "delayed child is outside the pure machine fragment",
                source[i]);
        }
    }
    *children_out = children;
    return true;
}

typedef enum {
    PREPARED_PURE_HEAD_INERT = 0,
    PREPARED_PURE_HEAD_CALLABLE = 1,
    PREPARED_PURE_HEAD_UNKNOWN = 2,
} PreparedPureHeadRole;

/*
 * One conservative capability judgment serves compilation, inert-template
 * validation, and run-time suspension handling.  UNKNOWN means that a
 * dialect owns the occurrence but this accelerator cannot implement it;
 * UNKNOWN therefore never licenses an inert-data optimization.
 */
static PreparedPureHeadRole prepared_pure_head_role(
    CettaPreparedPureProgram *program, Atom *source) {
    if (!program || !source)
        return PREPARED_PURE_HEAD_UNKNOWN;
    if (source->kind != ATOM_EXPR || source->expr.len == 0u)
        return PREPARED_PURE_HEAD_INERT;
    Atom *head = source->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return PREPARED_PURE_HEAD_UNKNOWN;

    if (program->expression_view) {
        CettaPreparedPureExpressionView view = {0};
        CettaPreparedPureExpressionViewState state =
            program->expression_view(source, &view);
        if (state == CETTA_PREPARED_PURE_EXPRESSION_DECLINE ||
            state == CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY)
            return PREPARED_PURE_HEAD_UNKNOWN;
        if (state == CETTA_PREPARED_PURE_EXPRESSION_PROJECT ||
            state == CETTA_PREPARED_PURE_EXPRESSION_OBSERVE)
            return PREPARED_PURE_HEAD_CALLABLE;
        if (state != CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return PREPARED_PURE_HEAD_UNKNOWN;
    }

    CettaExprLen arity = source->expr.len - 1u;
    if (prepared_pure_control_program(head->sym_id, arity, NULL) ||
        prepared_pure_register_program(program,
            head->sym_id, arity, NULL, NULL) ||
        prepared_pure_intrinsic_program(
            head->sym_id, arity, NULL) ||
        is_grounded_op(head->sym_id) ||
        prepared_pure_builtin_syntax(head->sym_id)) {
        return PREPARED_PURE_HEAD_CALLABLE;
    }
    bool defined = false;
    (void)space_query_effect_for_head(
        program->space, head->sym_id, &defined);
    return defined || space_equations_may_match_known_head(
                          program->space, head->sym_id)
        ? PREPARED_PURE_HEAD_CALLABLE
        : PREPARED_PURE_HEAD_INERT;
}

/* A template is data, not a suspended evaluator call.  Reject every outer
 * head not proved inert so the machine never changes an argument-evaluation
 * contract by treating active or unknown syntax as data. */
static bool prepared_pure_template_head_is_inert(
    CettaPreparedPureProgram *program, Atom *source) {
    return prepared_pure_head_role(program, source) ==
           PREPARED_PURE_HEAD_INERT;
}

static bool prepared_pure_compile_template(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, uint32_t depth, bool require_inert_head,
    uint32_t *node_out) {
    if (!program || !context || !source || !node_out ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (source->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, source->var_id, &slot))
            return false;
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_SLOT,
                .auxiliary = slot,
            },
            NULL, 0u, node_out);
    }
    if (source->kind != ATOM_EXPR) {
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    /* normalize-before-delay: a total register or control head inside
     * constructor payload is computation, not quoted data.  The canonical
     * evaluator computes these positions, so the machine must as well. */
    if (source->expr.len > 0u) {
        Atom *payload_head = source->expr.elems[0];
        if (payload_head && payload_head->kind == ATOM_SYMBOL) {
            CettaExprLen payload_arity = source->expr.len - 1u;
            if (prepared_pure_register_program(program,
                    payload_head->sym_id, payload_arity, NULL, NULL) ||
                prepared_pure_intrinsic_program(
                    payload_head->sym_id, payload_arity, NULL) ||
                prepared_pure_control_program(
                    payload_head->sym_id, payload_arity, NULL)) {
                return prepared_pure_compile_eval(
                    program, context, source, depth, node_out);
            }
        }
    }
    if (require_inert_head &&
        !prepared_pure_template_head_is_inert(program, source))
        return prepared_pure_reject(
            program, "callable syntax occurs in a data template", source);
    uint32_t *children = NULL;
    if (!prepared_pure_compile_children(
            program, context, source->expr.elems, source->expr.len,
            depth, false, false, &children))
        return false;
    bool ok = prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
        children, source->expr.len, node_out);
    free(children);
    return ok;
}

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, uint32_t depth, uint32_t *node_out) {
    if (!program || !context || !source || !node_out ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (source->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, source->var_id, &slot))
            return false;
        /* Eager clause and let bindings are populated only after their
         * producing child has completed.  Re-evaluating an expression-valued
         * result would mistake flat data such as `(1)` for a dynamic call.
         * Need bindings retain EVAL_SLOT because they may still be suspended. */
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = program->call_mode == CETTA_GSLT_PURE_CALL_EAGER
                    ? PREPARED_PURE_SLOT
                    : PREPARED_PURE_EVAL_SLOT,
                .auxiliary = slot,
            },
            NULL, 0u, node_out);
    }
    if (source->kind == ATOM_EXPR && program->opaque_value &&
        program->opaque_value(source) && !atom_has_vars(source)) {
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    CettaPreparedPureExpressionViewState expression_view_state =
        CETTA_PREPARED_PURE_EXPRESSION_DEFAULT;
    CettaPreparedPureExpressionView expression_view = {0};
    if (source->kind == ATOM_EXPR && program->expression_view) {
        expression_view_state =
            program->expression_view(source, &expression_view);
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_PROJECT) {
            if (!expression_view.projected ||
                expression_view.projected == source)
                return prepared_pure_reject(
                    program, "dialect projection did not make progress",
                    source);
            return prepared_pure_compile_eval(
                program, context, expression_view.projected,
                depth + 1u, node_out);
        }
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_OBSERVE) {
            if (!expression_view.projected ||
                expression_view.observation !=
                    CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION)
                return prepared_pure_reject(
                    program, "invalid dialect value observation", source);
            uint32_t child = 0u;
            uint32_t child_count = 0u;
            Atom *static_operand = expression_view.projected;
            if (expression_view.projected->kind == ATOM_VAR) {
                if (!prepared_pure_compile_template(
                        program, context, expression_view.projected,
                        depth + 1u, false, &child))
                    return false;
                child_count = 1u;
                static_operand = NULL;
            }
            return prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_OBSERVE,
                    .atom = static_operand,
                    .auxiliary = (uint32_t)expression_view.observation,
                },
                child_count ? &child : NULL, child_count, node_out);
        }
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY) {
            return prepared_pure_reject(
                program, "dialect-owned form requires canonical evaluation",
                source);
        }
        if (expression_view_state !=
                CETTA_PREPARED_PURE_EXPRESSION_DEFAULT &&
            expression_view_state !=
                CETTA_PREPARED_PURE_EXPRESSION_DECLINE)
            return prepared_pure_reject(
                program, "invalid dialect expression view", source);
    }
    if (source->kind != ATOM_EXPR)
        return prepared_pure_compile_template(
            program, context, source, depth, true, node_out);
    if (source->expr.len == 0u)
        return prepared_pure_compile_template(
            program, context, source, depth, true, node_out);
    Atom *head_atom = source->expr.elems[0];
    if (!head_atom)
        return false;
    if (head_atom->kind != ATOM_SYMBOL) {
        if (program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
            head_atom->kind != ATOM_GROUNDED ||
            head_atom->ground.gkind == GV_CAPTURE ||
            head_atom->ground.gkind == GV_FOREIGN) {
            return false;
        }
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems, source->expr.len,
                depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    SymbolId head = head_atom->sym_id;
    CettaExprLen arity = source->expr.len - 1u;
    CettaGsltFoldControl control;
    if (prepared_pure_control_program(head, arity, &control)) {
        if (control == CETTA_GSLT_FOLD_CONTROL_EVALUATE) {
            return arity == 1u && prepared_pure_compile_eval(
                program, context, source->expr.elems[1],
                depth + 1u, node_out);
        }
        if (control == CETTA_GSLT_FOLD_CONTROL_BRANCH) {
            if (arity != 3u)
                return false;
            uint32_t *children = NULL;
            if (!prepared_pure_compile_children(
                    program, context, &source->expr.elems[1], 3u,
                    depth, true, false, &children))
                return false;
            bool ok = prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
                children, 3u, node_out);
            free(children);
            return ok;
        }
        if (control == CETTA_GSLT_FOLD_CONTROL_BIND) {
            if (arity != 3u || !source->expr.elems[1])
                return false;
            uint32_t bound = 0u;
            if (!prepared_pure_compile_eval(
                    program, context, source->expr.elems[2],
                    depth + 1u, &bound))
                return prepared_pure_reject(
                    program, "let bound expression is outside the fragment",
                    source->expr.elems[2]);
            size_t saved_bindings = context->len;
            uint32_t pattern_index = 0u;
            if (!prepared_pure_compile_bind_pattern(
                    program, context, source->expr.elems[1],
                    &pattern_index))
                return prepared_pure_reject(
                    program, "let pattern is outside the fragment",
                    source->expr.elems[1]);
            uint32_t body = 0u;
            bool body_ok = prepared_pure_compile_eval(
                program, context, source->expr.elems[3],
                depth + 1u, &body);
            context->len = saved_bindings;
            if (!body_ok)
                return prepared_pure_reject(
                    program, "let body is outside the fragment",
                    source->expr.elems[3]);
            uint32_t children[2] = {bound, body};
            return prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_BIND,
                    .auxiliary = pattern_index,
                },
                children, 2u, node_out);
        }
        return false;
    }

    CettaGsltRegisterResultKind result_kind;
    CettaGsltRegisterInstruction instruction;
    if (prepared_pure_register_program(program,
            head, arity, &result_kind, &instruction)) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_REGISTER,
                .atom = head_atom,
                .head = head,
                .instruction = instruction,
                .result_kind = result_kind,
            },
            children, arity, node_out);
        free(children);
        return ok;
    }

    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    if (prepared_pure_intrinsic_program(
            head, arity, &intrinsic_instruction)) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_INTRINSIC,
                .atom = head_atom,
                .head = head,
                .intrinsic_instruction = intrinsic_instruction,
            },
            children, arity, node_out);
        free(children);
        return ok;
    }

    bool defined = false;
    CettaGsltQueryEffect effect =
        space_query_effect_for_head(program->space, head, &defined);
    if (defined) {
        if (effect != CETTA_GSLT_QUERY_EFFECT_PURE)
            return prepared_pure_reject(
                program, "user call may have a relational effect", source);
        if (!CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(
                program->space, head, arity))
            return prepared_pure_reject(
                program, "call policy is unsupported", source);
        uint32_t head_index = 0u;
        if (!prepared_pure_head_index_admitted(
                program, head, &head_index))
            return false;
        uint32_t *children = NULL;
        bool eager_arguments =
            program->call_mode == CETTA_GSLT_PURE_CALL_EAGER;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                depth, eager_arguments,
                !program->allow_callable_templates,
                &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_CALL,
                .head = head,
                .auxiliary = head_index,
                .call_arguments_are_values = eager_arguments,
            },
            children, arity, node_out);
        free(children);
        return ok;
    }
    PreparedPureHeadRole head_role =
        prepared_pure_head_role(program, source);
    if (head_role == PREPARED_PURE_HEAD_CALLABLE)
        return prepared_pure_reject(
            program, "unsupported evaluator syntax", source);
    if (head_role == PREPARED_PURE_HEAD_UNKNOWN ||
        expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_DECLINE)
        return prepared_pure_reject(
            program, "dialect-owned form requires canonical evaluation",
            source);
    if (program->call_mode == CETTA_GSLT_PURE_CALL_EAGER) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems, source->expr.len,
                depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    return prepared_pure_compile_template(
        program, context, source, depth, true, node_out);
}

static bool prepared_pure_bind_pattern_vars(
    PreparedPureCompileContext *context, Atom *pattern) {
    if (!context || !pattern)
        return false;
    Atom **stack = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    if (!prepared_pure_reserve(
            (void **)&stack, sizeof(*stack), &cap, 1u))
        return false;
    stack[len++] = pattern;
    while (len > 0u) {
        Atom *current = stack[--len];
        if (!current) {
            free(stack);
            return false;
        }
        if (current->kind == ATOM_VAR) {
            if (!prepared_pure_context_bind(
                    context, current->var_id, false, NULL)) {
                free(stack);
                return false;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR)
            continue;
        if (!prepared_pure_reserve(
                (void **)&stack, sizeof(*stack), &cap,
                len + current->expr.len)) {
            free(stack);
            return false;
        }
        for (CettaExprIndex i = 0u; i < current->expr.len; i++)
            stack[len++] = current->expr.elems[i];
    }
    free(stack);
    return true;
}

static int prepared_pure_var_slot_compare(
    const void *left_ptr, const void *right_ptr) {
    const PreparedPureVarSlot *left = left_ptr;
    const PreparedPureVarSlot *right = right_ptr;
    if (left->var < right->var)
        return -1;
    if (left->var > right->var)
        return 1;
    return 0;
}

static bool prepared_pure_append_pattern_vars(
    CettaPreparedPureProgram *program,
    const PreparedPureCompileContext *context,
    uint32_t *first_out, uint32_t *count_out) {
    if (!program || !context || !first_out || !count_out ||
        program->pattern_var_len > UINT32_MAX - context->len)
        return false;
    if (!prepared_pure_reserve(
            (void **)&program->pattern_vars,
            sizeof(*program->pattern_vars),
            &program->pattern_var_cap,
            program->pattern_var_len + context->len))
        return false;
    *first_out = (uint32_t)program->pattern_var_len;
    *count_out = (uint32_t)context->len;
    if (context->len > 0u) {
        PreparedPureVarSlot *destination =
            &program->pattern_vars[program->pattern_var_len];
        memcpy(destination, context->bindings,
               sizeof(*context->bindings) * context->len);
        qsort(destination, context->len, sizeof(*destination),
              prepared_pure_var_slot_compare);
    }
    program->pattern_var_len += context->len;
    return true;
}

/* The single-result machine may compile a multi-clause head only when every
 * same-arity pair is separated by information exposed at weak-head normal
 * form.  A variable overlaps everything.  Two expressions are separated here
 * only by outer arity or constructor head; differences below the constructor
 * would require a path-sensitive demand continuation, and overlapping clauses
 * require a choicepoint machine.  Both cases therefore fall back to the
 * canonical evaluator instead of committing one result. */
static bool prepared_pure_patterns_whnf_disjoint(
    Atom *left, Atom *right) {
    if (!left || !right)
        return false;
    CettaGsltPatternKind left_kind = left->kind == ATOM_VAR
        ? CETTA_GSLT_PATTERN_VARIABLE
        : left->kind == ATOM_EXPR
          ? CETTA_GSLT_PATTERN_EXPRESSION
          : CETTA_GSLT_PATTERN_ATOM;
    CettaGsltPatternKind right_kind = right->kind == ATOM_VAR
        ? CETTA_GSLT_PATTERN_VARIABLE
        : right->kind == ATOM_EXPR
          ? CETTA_GSLT_PATTERN_EXPRESSION
          : CETTA_GSLT_PATTERN_ATOM;
    bool expressions = left->kind == ATOM_EXPR &&
                       right->kind == ATOM_EXPR;
    uint64_t left_arity = expressions ? left->expr.len : 0u;
    uint64_t right_arity = expressions ? right->expr.len : 0u;
    Atom *left_head = expressions && left->expr.len > 0u
        ? left->expr.elems[0] : NULL;
    Atom *right_head = expressions && right->expr.len > 0u
        ? right->expr.elems[0] : NULL;
    return cetta_gslt_pure_call_whnf_disjoint(
        left_kind, right_kind,
        !expressions && atom_eq(left, right),
        left_arity, right_arity,
        left_head && left_head->kind != ATOM_VAR &&
            left_head->kind != ATOM_EXPR,
        right_head && right_head->kind != ATOM_VAR &&
            right_head->kind != ATOM_EXPR,
        left_head && right_head && atom_eq(left_head, right_head));
}

static bool prepared_pure_clauses_whnf_disjoint(
    const PreparedPureClause *left, const PreparedPureClause *right) {
    if (!left || !right)
        return false;
    if (left->arity != right->arity)
        return true;
    if (!left->lhs || !right->lhs || left->lhs->kind != ATOM_EXPR ||
        right->lhs->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < left->arity; i++) {
        if (prepared_pure_patterns_whnf_disjoint(
                left->lhs->expr.elems[i + 1u],
                right->lhs->expr.elems[i + 1u]))
            return true;
    }
    return false;
}

/* Fast common-case determinacy proof.  A single argument whose clauses
 * are all distinct literals or distinct exact expression-head/arity pairs is
 * already a complete weak-head discriminator.  Proving that fact with an
 * open-addressed set avoids the old O(C^2) pairwise overlap pass for large
 * generated dispatch families; complex jointly-discriminating patterns retain
 * the exact pairwise fallback below. */
typedef struct {
    uint8_t kind;
    uint32_t expression_length;
    Atom *atom;
} PreparedPureWhnfDiscriminatorKey;

static size_t prepared_pure_whnf_discriminator_hash(
    uint8_t kind, uint32_t expression_length, Atom *atom) {
    uint64_t bits = (uint64_t)kind * UINT64_C(0x9e3779b185ebca87);
    bits ^= (uint64_t)expression_length *
        UINT64_C(0xc2b2ae3d27d4eb4f);
    bits ^= (uint64_t)atom_hash(atom) *
        UINT64_C(0x165667b19e3779f9);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static bool prepared_pure_whnf_discriminator_key_equal(
    const PreparedPureWhnfDiscriminatorKey *key,
    uint8_t kind, uint32_t expression_length, Atom *atom) {
    return key && key->kind == kind &&
           key->expression_length == expression_length && key->atom && atom &&
           (key->atom == atom || atom_eq(key->atom, atom));
}

static bool prepared_pure_argument_is_standalone_whnf_discriminator(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head, uint32_t argument) {
    if (!program || !head || head->clause_count < 2u ||
        head->first_clause > program->clause_len ||
        head->clause_count > program->clause_len - head->first_clause)
        return false;
    uint32_t bucket_count = 4u;
    while (bucket_count < head->clause_count * 2u) {
        if (bucket_count > UINT32_MAX / 2u)
            return false;
        bucket_count *= 2u;
    }
    PreparedPureWhnfDiscriminatorKey *keys = calloc(
        head->clause_count, sizeof(*keys));
    uint32_t *buckets = calloc(bucket_count, sizeof(*buckets));
    if (!keys || !buckets) {
        free(keys);
        free(buckets);
        return false;
    }
    uint32_t key_count = 0u;
    bool distinct = true;
    uint32_t mask = bucket_count - 1u;
    for (uint32_t i = 0u; i < head->clause_count && distinct; i++) {
        const PreparedPureClause *clause =
            &program->clauses[head->first_clause + i];
        if (!clause->lhs || clause->lhs->kind != ATOM_EXPR ||
            argument >= clause->arity) {
            distinct = false;
            break;
        }
        Atom *pattern = clause->lhs->expr.elems[argument + 1u];
        if (!pattern || pattern->kind == ATOM_VAR) {
            distinct = false;
            break;
        }
        uint8_t kind = 1u;
        uint32_t expression_length = 0u;
        Atom *atom = pattern;
        if (pattern->kind == ATOM_EXPR) {
            expression_length = pattern->expr.len;
            Atom *pattern_head = expression_length > 0u
                ? pattern->expr.elems[0] : NULL;
            if (!pattern_head || pattern_head->kind == ATOM_VAR ||
                pattern_head->kind == ATOM_EXPR) {
                distinct = false;
                break;
            }
            kind = 2u;
            atom = pattern_head;
        }
        uint32_t bucket = (uint32_t)(
            prepared_pure_whnf_discriminator_hash(
                kind, expression_length, atom) & mask);
        bool inserted = false;
        for (uint32_t probe = 0u; probe < bucket_count; probe++) {
            uint32_t encoded = buckets[bucket];
            if (encoded == 0u) {
                if (key_count >= head->clause_count) {
                    distinct = false;
                    break;
                }
                keys[key_count] = (PreparedPureWhnfDiscriminatorKey){
                    .kind = kind,
                    .expression_length = expression_length,
                    .atom = atom,
                };
                buckets[bucket] = ++key_count;
                inserted = true;
                break;
            }
            uint32_t key_index = encoded - 1u;
            if (key_index >= key_count) {
                distinct = false;
                break;
            }
            if (prepared_pure_whnf_discriminator_key_equal(
                    &keys[key_index], kind, expression_length, atom)) {
                distinct = false;
                break;
            }
            bucket = (bucket + 1u) & mask;
        }
        if (!inserted && distinct)
            distinct = false;
    }
    free(keys);
    free(buckets);
    return distinct;
}

static bool prepared_pure_head_is_whnf_determinate(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head) {
    if (!program || !head ||
        head->first_clause > program->clause_len ||
        head->clause_count > program->clause_len - head->first_clause)
        return false;
    if (head->clause_count > 1u) {
        uint32_t arity = program->clauses[head->first_clause].arity;
        bool same_arity = arity <= 64u;
        for (uint32_t i = 1u; i < head->clause_count; i++) {
            if (program->clauses[head->first_clause + i].arity != arity) {
                same_arity = false;
                break;
            }
        }
        if (same_arity) {
            for (uint32_t argument = 0u; argument < arity; argument++) {
                if (prepared_pure_argument_is_standalone_whnf_discriminator(
                        program, head, argument))
                    return true;
            }
        }
    }
    for (uint32_t i = 0u; i < head->clause_count; i++) {
        const PreparedPureClause *left =
            &program->clauses[head->first_clause + i];
        for (uint32_t j = i + 1u; j < head->clause_count; j++) {
            const PreparedPureClause *right =
                &program->clauses[head->first_clause + j];
            if (!prepared_pure_clauses_whnf_disjoint(left, right))
                return false;
        }
    }
    return true;
}

static bool prepared_pure_compile_decision_group(
    CettaPreparedPureProgram *program, PreparedPureHead *head,
    uint32_t arity) {
    if (!program || !head || arity > 64u ||
        head->first_clause > program->clause_len ||
        head->clause_count > program->clause_len - head->first_clause)
        return false;

    uint32_t clause_count = 0u;
    for (uint32_t i = 0u; i < head->clause_count; i++) {
        if (program->clauses[head->first_clause + i].arity == arity)
            clause_count++;
    }
    if (clause_count < PREPARED_PURE_DECISION_MIN_CLAUSES || arity == 0u)
        return true;
    if (program->decision_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->decisions,
            sizeof(*program->decisions), &program->decision_cap,
            program->decision_len + 1u))
        return false;

    CettaMatchDecisionClause *clauses =
        malloc(sizeof(*clauses) * clause_count);
    if (!clauses)
        return false;
    uint64_t universally_constrained =
        arity == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << arity) - 1u;
    uint32_t write = 0u;
    for (uint32_t i = 0u; i < head->clause_count; i++) {
        uint32_t clause_index = head->first_clause + i;
        PreparedPureClause *clause = &program->clauses[clause_index];
        if (clause->arity != arity)
            continue;
        if (!clause->lhs || clause->lhs->kind != ATOM_EXPR ||
            clause->lhs->expr.len != arity + 1u) {
            free(clauses);
            return false;
        }
        clauses[write++] = (CettaMatchDecisionClause){
            .pattern = clause->lhs,
            .source_ref = clause_index,
        };
        for (uint32_t argument = 0u; argument < arity; argument++) {
            Atom *pattern = clause->lhs->expr.elems[argument + 1u];
            if (!pattern || pattern->kind == ATOM_VAR) {
                universally_constrained &=
                    ~(UINT64_C(1) << argument);
            }
        }
    }
    if (write != clause_count) {
        free(clauses);
        return false;
    }

    CettaMatchDecision *selector = cetta_match_decision_compile(
        program->read, program->match_decision_semantics,
        clauses, clause_count, CETTA_MATCH_DECISION_DEEP,
        0u, NULL, NULL);
    free(clauses);
    if (!selector)
        return false;

    uint32_t decision_index = (uint32_t)program->decision_len;
    program->decisions[program->decision_len++] =
        (PreparedPureDecisionProgram){
            .arity = arity,
            .clause_count = clause_count,
            .universally_constrained_arguments =
                universally_constrained,
            .selector = selector,
        };
    head = &program->heads[(uint32_t)(head - program->heads)];
    if (head->decision_count == 0u)
        head->first_decision = decision_index;
    head->decision_count++;
    return true;
}

static bool prepared_pure_compile_decisions_for_head(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len)
        return false;
    /* Dialect-owned pattern views may reinterpret the outer pattern shape.
     * Until the LanguageDef compiler emits a corresponding discriminator,
     * retain the exact generic matcher as the only authority. */
    if (program->pattern_view)
        return true;
    PreparedPureHead *head = &program->heads[head_index];
    for (uint32_t i = 0u; i < head->clause_count; i++) {
        uint32_t arity =
            program->clauses[head->first_clause + i].arity;
        bool seen = false;
        for (uint32_t j = 0u; j < i; j++) {
            if (program->clauses[head->first_clause + j].arity == arity) {
                seen = true;
                break;
            }
        }
        if (!seen && !prepared_pure_compile_decision_group(
                         program, head, arity))
            return false;
        head = &program->heads[head_index];
    }
    return true;
}

/* Mark only the result-preserving spine of a clause body.  A call reached
 * through a selected branch or let body is still in tail position; operands,
 * conditions, and bound expressions are not. */
static bool prepared_pure_mark_tail_spine(
    CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    PreparedPureNode *node = &program->nodes[node_index];
    node->tail_position = true;
    if (node->kind == PREPARED_PURE_IF) {
        if (node->child_count != 3u)
            return false;
        return prepared_pure_mark_tail_spine(
                   program,
                   program->children[node->first_child + 1u],
                   depth + 1u) &&
               prepared_pure_mark_tail_spine(
                   program,
                   program->children[node->first_child + 2u],
                   depth + 1u);
    }
    if (node->kind == PREPARED_PURE_BIND) {
        if (node->child_count != 2u)
            return false;
        return prepared_pure_mark_tail_spine(
            program, program->children[node->first_child + 1u],
            depth + 1u);
    }
    return true;
}

static bool prepared_pure_compile_head(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len)
        return false;
    PreparedPureHead *head = &program->heads[head_index];
    if (head->compiled)
        return true;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(program->space, head->head, &cursor))
        return false;
    head->first_clause = (uint32_t)program->clause_len;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            program->clause_len >= PREPARED_PURE_MAX_CLAUSES ||
            program->clause_len >= UINT32_MAX)
            return false;
        SpaceEquationOccurrence occurrence = {0};
        if (!space_equation_occurrence_resolve(id, &occurrence) ||
            !occurrence.lhs || occurrence.lhs->kind != ATOM_EXPR ||
            occurrence.lhs->expr.len == 0u ||
            !atom_is_symbol_id(
                occurrence.lhs->expr.elems[0], head->head))
            return prepared_pure_reject(
                program, "wildcard or malformed equation", occurrence.lhs);

        PreparedPureCompileContext context = {0};
        bool pattern_ok = true;
        for (CettaExprIndex i = 1u;
             i < occurrence.lhs->expr.len; i++) {
            if (!prepared_pure_bind_pattern_vars(
                    &context, occurrence.lhs->expr.elems[i])) {
                pattern_ok = false;
                break;
            }
        }
        uint32_t first_pattern_var = 0u;
        uint32_t pattern_var_count = 0u;
        if (!pattern_ok || !prepared_pure_append_pattern_vars(
                program, &context,
                &first_pattern_var, &pattern_var_count)) {
            free(context.bindings);
            return prepared_pure_reject(
                program, "nonlinear or oversized clause pattern",
                occurrence.lhs);
        }
        uint32_t root = 0u;
        bool rhs_ok = prepared_pure_compile_eval(
            program, &context, occurrence.rhs, 0u, &root);
        if (rhs_ok)
            rhs_ok = prepared_pure_mark_tail_spine(
                program, root, 0u);
        if (!rhs_ok || context.next_slot > PREPARED_PURE_MAX_SLOTS ||
            !prepared_pure_reserve(
                (void **)&program->clauses,
                sizeof(*program->clauses), &program->clause_cap,
                program->clause_len + 1u)) {
            free(context.bindings);
            return prepared_pure_reject(
                program, "clause body is outside the pure machine fragment",
                occurrence.rhs);
        }
        program->clauses[program->clause_len++] = (PreparedPureClause){
            .lhs = occurrence.lhs,
            .arity = occurrence.lhs->expr.len - 1u,
            .root = root,
            .local_count = context.next_slot,
            .first_pattern_var = first_pattern_var,
            .pattern_var_count = pattern_var_count,
        };
        head = &program->heads[head_index];
        head->clause_count++;
        free(context.bindings);
    }
    head = &program->heads[head_index];
    if (head->clause_count == 0u ||
        !space_read_token_is_current(program->read))
        return prepared_pure_reject(
            program, "empty or invalidated user head", NULL);
    if (!prepared_pure_head_is_whnf_determinate(program, head))
        return prepared_pure_reject(
            program,
            "head is not determinate from weak-head patterns", NULL);
    if (!prepared_pure_compile_decisions_for_head(program, head_index))
        return prepared_pure_reject(
            program, "failed to compile shared match decision program",
            NULL);
    head = &program->heads[head_index];
    head->compiled = true;
    return true;
}

static bool prepared_pure_compile_pending_heads(
    CettaPreparedPureProgram *program) {
    if (!program)
        return false;
    for (uint32_t i = 0u; i < program->head_len; i++) {
        if (!program->heads[i].compiled &&
            !prepared_pure_compile_head(program, i))
            return false;
    }
    return space_read_token_is_current(program->read);
}

static bool prepared_pure_runtime_head_index(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *head_index) {
    return program && program->closed_program && head_index &&
           prepared_pure_head_index(program, head, head_index) &&
           prepared_pure_compile_pending_heads(program);
}

static bool prepared_pure_push_frame(
    CettaPreparedPureProgram *program, uint32_t node,
    uint32_t local_base) {
    if (!program || node >= program->node_len ||
        !prepared_pure_reserve(
            (void **)&program->frames, sizeof(*program->frames),
            &program->frame_cap, program->frame_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->frame_atoms, sizeof(*program->frame_atoms),
            &program->frame_atom_cap, program->frame_len + 1u))
        return false;
    size_t frame_index = program->frame_len++;
    program->frames[frame_index] = (PreparedPureFrame){
        .node = node,
        .local_base = local_base,
        .memo_index = PREPARED_PURE_NO_MEMO,
    };
    program->frame_atoms[frame_index] = NULL;
    return true;
}

static bool prepared_pure_push_runtime_frame(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !program->closed_program || !atom ||
        !prepared_pure_reserve(
            (void **)&program->frames, sizeof(*program->frames),
            &program->frame_cap, program->frame_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->frame_atoms, sizeof(*program->frame_atoms),
            &program->frame_atom_cap, program->frame_len + 1u))
        return false;
    size_t frame_index = program->frame_len++;
    program->frames[frame_index] = (PreparedPureFrame){
        .node = PREPARED_PURE_RUNTIME_NODE,
        .memo_index = PREPARED_PURE_NO_MEMO,
    };
    program->frame_atoms[frame_index] = atom;
    return true;
}

static bool prepared_pure_push_value(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value ||
        !prepared_pure_reserve(
            (void **)&program->values, sizeof(*program->values),
            &program->value_cap, program->value_len + 1u))
        return false;
    program->values[program->value_len++] = value;
    return true;
}

static bool prepared_pure_push_pattern_pair(
    CettaPreparedPureProgram *program, Atom *pattern, Atom *value,
    uint32_t argument) {
    if (!program || !pattern || !value ||
        !prepared_pure_reserve(
            (void **)&program->pattern_pairs,
            sizeof(*program->pattern_pairs),
            &program->pattern_pair_cap,
            program->pattern_pair_len + 1u))
        return false;
    program->pattern_pairs[program->pattern_pair_len++] =
        (PreparedPurePatternPair){pattern, value, argument};
    return true;
}

static bool prepared_pure_clause_var_slot(
    const CettaPreparedPureProgram *program,
    const PreparedPureClause *clause, VarId var,
    uint32_t *slot_out) {
    if (!program || !clause || !slot_out)
        return false;
    uint32_t lower = 0u;
    uint32_t upper = clause->pattern_var_count;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const PreparedPureVarSlot *entry =
            &program->pattern_vars[
                clause->first_pattern_var + middle];
        if (entry->var < var) {
            lower = middle + 1u;
        } else if (entry->var > var) {
            upper = middle;
        } else {
            *slot_out = entry->slot;
            return true;
        }
    }
    return false;
}

static bool prepared_pure_expression_is_callable(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value || value->kind != ATOM_EXPR ||
        value->expr.len == 0u)
        return false;
    Atom *head = value->expr.elems[0];
    if (!head)
        return true;
    if (head->kind != ATOM_SYMBOL) {
        return head->kind != ATOM_GROUNDED ||
               head->ground.gkind == GV_CAPTURE ||
               head->ground.gkind == GV_FOREIGN;
    }
    CettaExprLen expression_arity = value->expr.len - 1u;
    if (expression_arity > UINT32_MAX)
        return true;
    uint32_t arity = (uint32_t)expression_arity;
    bool callable = false;
    if (prepared_pure_callable_cache_lookup(
            program, head->sym_id, arity, &callable))
        return callable;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_MISS);
    callable = prepared_pure_head_role(program, value) !=
               PREPARED_PURE_HEAD_INERT;
    /* The program is pinned to one space revision and dialect.  This cache is
     * therefore semantic data for that program, not a process-global policy
     * table.  Allocation failure only loses the optimization. */
    (void)prepared_pure_callable_cache_store(
        program, head->sym_id, arity, callable);
    return callable;
}

typedef struct {
    CettaPreparedPureProgram *program;
    Atom **slots;
    size_t capacity;
    size_t used;
} PreparedPureEscapingSuspensions;

static bool prepared_pure_escaping_suspensions_grow(
    PreparedPureEscapingSuspensions *seen) {
    if (!seen || seen->capacity > SIZE_MAX / 2u)
        return false;
    size_t next_capacity = seen->capacity ? seen->capacity * 2u : 16u;
    if (next_capacity > SIZE_MAX / sizeof(*seen->slots))
        return false;
    Atom **next = calloc(next_capacity, sizeof(*next));
    if (!next)
        return false;
    for (size_t i = 0u; i < seen->capacity; i++) {
        Atom *atom = seen->slots[i];
        if (!atom)
            continue;
        size_t index = (size_t)atom_hash(atom) & (next_capacity - 1u);
        while (next[index])
            index = (index + 1u) & (next_capacity - 1u);
        next[index] = atom;
    }
    free(seen->slots);
    seen->slots = next;
    seen->capacity = next_capacity;
    return true;
}

/* Returns false when an equivalent suspension was already present. */
static bool prepared_pure_escaping_suspensions_insert(
    PreparedPureEscapingSuspensions *seen, Atom *atom) {
    if (!seen || !atom)
        return false;
    if (seen->capacity == 0u ||
        seen->used + 1u > seen->capacity / 2u) {
        if (!prepared_pure_escaping_suspensions_grow(seen))
            return false;
    }
    size_t index = (size_t)atom_hash(atom) & (seen->capacity - 1u);
    while (seen->slots[index]) {
        if (atom_eq(seen->slots[index], atom))
            return false;
        index = (index + 1u) & (seen->capacity - 1u);
    }
    seen->slots[index] = atom;
    seen->used++;
    return true;
}

static bool prepared_pure_duplicate_callable_result_node(
    const Atom *atom, void *context) {
    PreparedPureEscapingSuspensions *seen = context;
    if (!seen || !prepared_pure_expression_is_callable(
                     seen->program, (Atom *)atom))
        return false;
    return !prepared_pure_escaping_suspensions_insert(
        seen, (Atom *)atom);
}

/* The private memo table is an execution-time implementation of Need update
 * cells.  Two equivalent raw calls cannot cross the machine boundary in its
 * place: once embedded in a returned constructor they would be two new
 * computations rather than two references to one computation.  A single
 * suspended continuation may safely return to the outer machine.  Until the
 * result ABI carries virtual suspensions, decline the ambiguous duplicated
 * representation and let the ordinary Prime machine preserve source CellId. */
static bool prepared_pure_result_has_escaping_suspension(
    CettaPreparedPureProgram *program, Atom *result) {
    if (!program || !result || !program->closed_program ||
        program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED)
        return false;
    PreparedPureEscapingSuspensions seen = {
        .program = program,
    };
    bool duplicated = atom_tree_any(
        result, prepared_pure_duplicate_callable_result_node, &seen);
    free(seen.slots);
    return duplicated;
}

typedef enum {
    PREPARED_PURE_MATCH_ERROR = -1,
    PREPARED_PURE_MATCH_MISMATCH = 0,
    PREPARED_PURE_MATCH_MATCHED = 1,
    PREPARED_PURE_MATCH_NEEDS_ARGUMENT = 2,
} PreparedPureMatchState;

static PreparedPureMatchState prepared_pure_match_mismatch(
    CettaPreparedPureProgram *program, Atom *value,
    uint32_t argument, uint64_t ready_arguments,
    uint32_t *demanded_argument) {
    if (argument < 64u &&
        (ready_arguments & (UINT64_C(1) << argument)) == 0u &&
        prepared_pure_expression_is_callable(program, value)) {
        if (demanded_argument)
            *demanded_argument = argument;
        return PREPARED_PURE_MATCH_NEEDS_ARGUMENT;
    }
    return PREPARED_PURE_MATCH_MISMATCH;
}

static PreparedPureMatchState prepared_pure_match_clause(
    CettaPreparedPureProgram *program,
    const PreparedPureClause *clause,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments, uint32_t *demanded_argument) {
    if (!program || !clause || !arguments || arity != clause->arity ||
        !prepared_pure_reserve(
            (void **)&program->match_values,
            sizeof(*program->match_values), &program->match_cap,
            clause->local_count))
        return PREPARED_PURE_MATCH_ERROR;
    if (clause->local_count > 0u)
        memset(program->match_values, 0,
               sizeof(*program->match_values) * clause->local_count);
    program->pattern_pair_len = 0u;
    for (uint32_t i = 0u; i < arity; i++) {
        if (!prepared_pure_push_pattern_pair(
                program, clause->lhs->expr.elems[i + 1u],
                arguments[i], i))
            return PREPARED_PURE_MATCH_ERROR;
    }
    while (program->pattern_pair_len > 0u) {
        PreparedPurePatternPair pair =
            program->pattern_pairs[--program->pattern_pair_len];
        Atom *pattern = pair.pattern;
        Atom *value = pair.value;
        if (pattern->kind == ATOM_VAR) {
            uint32_t slot = 0u;
            if (!prepared_pure_clause_var_slot(
                    program, clause, pattern->var_id, &slot) ||
                slot >= clause->local_count)
                return PREPARED_PURE_MATCH_ERROR;
            program->match_values[slot] = value;
            continue;
        }
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            CettaPreparedPurePatternViewState view_state =
                program->pattern_view(pattern, value, &view);
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH) {
                return prepared_pure_match_mismatch(
                    program, value, pair.argument, ready_arguments,
                    demanded_argument);
            }
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
                if ((view.child_count > 0u &&
                     (!view.pattern_children || !view.value_children)))
                    return PREPARED_PURE_MATCH_ERROR;
                for (CettaExprIndex i = 0u;
                     i < view.child_count; i++) {
                    if (!prepared_pure_push_pattern_pair(
                            program, view.pattern_children[i],
                            view.value_children[i], pair.argument))
                        return PREPARED_PURE_MATCH_ERROR;
                }
                continue;
            }
            if (view_state !=
                CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
                return PREPARED_PURE_MATCH_ERROR;
        }
        if (pattern->kind != value->kind)
            return prepared_pure_match_mismatch(
                program, value, pair.argument, ready_arguments,
                demanded_argument);
        if (pattern->kind != ATOM_EXPR) {
            if (!atom_eq(pattern, value))
                return prepared_pure_match_mismatch(
                    program, value, pair.argument, ready_arguments,
                    demanded_argument);
            continue;
        }
        if (pattern->expr.len != value->expr.len)
            return prepared_pure_match_mismatch(
                program, value, pair.argument, ready_arguments,
                demanded_argument);
        for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
            if (!prepared_pure_push_pattern_pair(
                    program, pattern->expr.elems[i],
                    value->expr.elems[i], pair.argument))
                return PREPARED_PURE_MATCH_ERROR;
        }
    }
    return PREPARED_PURE_MATCH_MATCHED;
}

static PreparedPureMatchState prepared_pure_match_bind_pattern(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value) {
    if (!program || !descriptor || !value ||
        descriptor->first_var > program->bind_var_len ||
        descriptor->var_count >
            program->bind_var_len - descriptor->first_var ||
        !prepared_pure_reserve(
            (void **)&program->match_values,
            sizeof(*program->match_values), &program->match_cap,
            descriptor->var_count))
        return PREPARED_PURE_MATCH_ERROR;
    if (descriptor->var_count > 0u)
        memset(program->match_values, 0,
               sizeof(*program->match_values) * descriptor->var_count);

    program->pattern_pair_len = 0u;
    if (!prepared_pure_push_pattern_pair(
            program, descriptor->pattern, value, 0u))
        return PREPARED_PURE_MATCH_ERROR;
    while (program->pattern_pair_len > 0u) {
        PreparedPurePatternPair pair =
            program->pattern_pairs[--program->pattern_pair_len];
        Atom *pattern = pair.pattern;
        Atom *candidate = pair.value;
        if (pattern->kind == ATOM_VAR) {
            uint32_t index = 0u;
            if (!prepared_pure_bind_var_index(
                    program, descriptor->first_var,
                    descriptor->var_count, pattern->var_id, &index))
                return PREPARED_PURE_MATCH_ERROR;
            const PreparedPureBindVar *binding =
                &program->bind_vars[descriptor->first_var + index];
            if (binding->prebound) {
                size_t slot = (size_t)local_base + binding->slot;
                if (slot >= program->slot_len ||
                    !program->slots[slot])
                    return PREPARED_PURE_MATCH_ERROR;
                if (!atom_eq(program->slots[slot], candidate))
                    return PREPARED_PURE_MATCH_MISMATCH;
            } else if (program->match_values[index]) {
                if (!atom_eq(program->match_values[index], candidate))
                    return PREPARED_PURE_MATCH_MISMATCH;
            } else {
                program->match_values[index] = candidate;
            }
            continue;
        }
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            CettaPreparedPurePatternViewState view_state =
                program->pattern_view(pattern, candidate, &view);
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH)
                return PREPARED_PURE_MATCH_MISMATCH;
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
                if ((view.child_count > 0u &&
                     (!view.pattern_children || !view.value_children)))
                    return PREPARED_PURE_MATCH_ERROR;
                for (CettaExprIndex i = view.child_count; i > 0u; i--) {
                    CettaExprIndex child = i - 1u;
                    if (!prepared_pure_push_pattern_pair(
                            program, view.pattern_children[child],
                            view.value_children[child], 0u))
                        return PREPARED_PURE_MATCH_ERROR;
                }
                continue;
            }
            if (view_state !=
                CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
                return PREPARED_PURE_MATCH_ERROR;
        }
        if (pattern->kind != candidate->kind)
            return PREPARED_PURE_MATCH_MISMATCH;
        if (pattern->kind != ATOM_EXPR) {
            if (!atom_eq(pattern, candidate))
                return PREPARED_PURE_MATCH_MISMATCH;
            continue;
        }
        if (pattern->expr.len != candidate->expr.len)
            return PREPARED_PURE_MATCH_MISMATCH;
        for (CettaExprIndex i = pattern->expr.len; i > 0u; i--) {
            CettaExprIndex child = i - 1u;
            if (!prepared_pure_push_pattern_pair(
                    program, pattern->expr.elems[child],
                    candidate->expr.elems[child], 0u))
                return PREPARED_PURE_MATCH_ERROR;
        }
    }

    for (uint32_t i = 0u; i < descriptor->var_count; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[descriptor->first_var + i];
        if (binding->prebound)
            continue;
        size_t slot = (size_t)local_base + binding->slot;
        if (slot >= program->slot_len || !program->match_values[i])
            return PREPARED_PURE_MATCH_ERROR;
        program->slots[slot] = program->match_values[i];
    }
    return PREPARED_PURE_MATCH_MATCHED;
}

typedef enum {
    PREPARED_PURE_DECISION_ERROR = -1,
    PREPARED_PURE_DECISION_NOT_APPLICABLE = 0,
    PREPARED_PURE_DECISION_READY = 1,
    PREPARED_PURE_DECISION_NEEDS_ARGUMENT = 2,
} PreparedPureDecisionState;

static const PreparedPureDecisionProgram *
prepared_pure_decision_for_arity(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head, uint32_t arity) {
    if (!program || !head ||
        head->first_decision > program->decision_len ||
        head->decision_count >
            program->decision_len - head->first_decision)
        return NULL;
    for (uint32_t i = 0u; i < head->decision_count; i++) {
        const PreparedPureDecisionProgram *decision =
            &program->decisions[head->first_decision + i];
        if (decision->arity == arity)
            return decision;
    }
    return NULL;
}

/* Shared MatchDecision performs refutation only.  Already available
 * non-callable arguments may be observed without changing Need behaviour;
 * callable arguments remain unavailable until the evaluator has forced them.
 * Direct demand is permitted only when every clause constrains the same
 * argument, preserving the exact matcher's source-order demand policy. */
static PreparedPureDecisionState prepared_pure_decision_candidates(
    CettaPreparedPureProgram *program,
    const PreparedPureHead *head,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments,
    const uint32_t **candidate_refs_out,
    size_t *candidate_count_out,
    uint32_t *demanded_argument_out) {
    if (candidate_refs_out)
        *candidate_refs_out = NULL;
    if (candidate_count_out)
        *candidate_count_out = 0u;
    if (demanded_argument_out)
        *demanded_argument_out = 0u;
    if (!program || !head || !arguments || !candidate_refs_out ||
        !candidate_count_out || !demanded_argument_out)
        return PREPARED_PURE_DECISION_ERROR;

    const PreparedPureDecisionProgram *decision =
        prepared_pure_decision_for_arity(program, head, arity);
    if (!decision ||
        decision->clause_count < PREPARED_PURE_DECISION_MIN_CLAUSES)
        return PREPARED_PURE_DECISION_NOT_APPLICABLE;
    if (!decision->selector)
        return PREPARED_PURE_DECISION_ERROR;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_RUN);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_CLAUSE_INPUT,
        decision->clause_count);

    uint64_t observable_arguments = ready_arguments;
    for (uint32_t argument = 0u; argument < arity; argument++) {
        uint64_t bit = UINT64_C(1) << argument;
        if ((observable_arguments & bit) == 0u &&
            !prepared_pure_expression_is_callable(
                program, arguments[argument])) {
            observable_arguments |= bit;
        }
    }

    uint64_t unresolved =
        decision->universally_constrained_arguments &
        ~observable_arguments;
    while (unresolved != 0u) {
        uint32_t argument = (uint32_t)__builtin_ctzll(unresolved);
        unresolved &= unresolved - 1u;
        if (argument >= arity || !arguments[argument])
            return PREPARED_PURE_DECISION_ERROR;
        if (prepared_pure_expression_is_callable(
                program, arguments[argument])) {
            *demanded_argument_out = argument;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_DIRECT_DEMAND);
            return PREPARED_PURE_DECISION_NEEDS_ARGUMENT;
        }
    }

    if (head->first_clause >= program->clause_len)
        return PREPARED_PURE_DECISION_ERROR;
    Atom *lhs = program->clauses[head->first_clause].lhs;
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u)
        return PREPARED_PURE_DECISION_ERROR;
    CettaMatchDecisionSelectState selected =
        cetta_match_decision_select_parts(
            decision->selector, program->space,
            program->match_decision_semantics,
            lhs->expr.elems[0], arguments, arity,
            observable_arguments,
            candidate_refs_out, candidate_count_out);
    if (selected == CETTA_MATCH_DECISION_SELECT_INVALIDATED)
        return PREPARED_PURE_DECISION_ERROR;
    if (selected != CETTA_MATCH_DECISION_SELECT_READY)
        return PREPARED_PURE_DECISION_ERROR;

    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_CLAUSE_SURVIVOR,
        *candidate_count_out);
    return PREPARED_PURE_DECISION_READY;
}

typedef enum {
    PREPARED_PURE_SELECT_ERROR = -1,
    PREPARED_PURE_SELECT_NO_MATCH = 0,
    PREPARED_PURE_SELECT_SELECTED = 1,
    PREPARED_PURE_SELECT_NEEDS_ARGUMENT = 2,
    PREPARED_PURE_SELECT_AMBIGUOUS = 3,
} PreparedPureSelectState;

typedef struct {
    PreparedPureSelectState state;
    const PreparedPureClause *clause;
    uint32_t demanded_argument;
} PreparedPureSelection;

static PreparedPureSelectState prepared_pure_consider_clause(
    CettaPreparedPureProgram *program,
    const PreparedPureClause *clause,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments,
    const PreparedPureClause **selected,
    bool *needs_argument,
    uint32_t *first_demanded_argument) {
    if (!program || !clause || !arguments || !selected ||
        !needs_argument || !first_demanded_argument)
        return PREPARED_PURE_SELECT_ERROR;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
    uint32_t demanded_argument = 0u;
    PreparedPureMatchState matched = prepared_pure_match_clause(
        program, clause, arguments, arity,
        ready_arguments, &demanded_argument);
    if (matched == PREPARED_PURE_MATCH_ERROR)
        return PREPARED_PURE_SELECT_ERROR;
    if (matched == PREPARED_PURE_MATCH_NEEDS_ARGUMENT) {
        if (!*needs_argument ||
            demanded_argument < *first_demanded_argument)
            *first_demanded_argument = demanded_argument;
        *needs_argument = true;
        return PREPARED_PURE_SELECT_NO_MATCH;
    }
    if (matched != PREPARED_PURE_MATCH_MATCHED)
        return PREPARED_PURE_SELECT_NO_MATCH;
    if (*selected)
        return PREPARED_PURE_SELECT_AMBIGUOUS;
    if (!prepared_pure_reserve(
            (void **)&program->selected_values,
            sizeof(*program->selected_values),
            &program->selected_cap, clause->local_count))
        return PREPARED_PURE_SELECT_ERROR;
    if (clause->local_count > 0u)
        memcpy(program->selected_values, program->match_values,
               sizeof(*program->selected_values) * clause->local_count);
    *selected = clause;
    return PREPARED_PURE_SELECT_NO_MATCH;
}

static PreparedPureSelection prepared_pure_select_clause(
    CettaPreparedPureProgram *program, uint32_t head_index,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments) {
    PreparedPureSelection result = {
        .state = PREPARED_PURE_SELECT_NO_MATCH,
    };
    if (!program || head_index >= program->head_len)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_ERROR,
        };
    const PreparedPureHead *head = &program->heads[head_index];
    const PreparedPureClause *selected = NULL;
    bool needs_argument = false;
    uint32_t first_demanded_argument = 0u;
    const uint32_t *decision_candidate_refs = NULL;
    size_t decision_candidate_count = 0u;
    uint32_t decision_demanded_argument = 0u;
    PreparedPureDecisionState decision_state =
        prepared_pure_decision_candidates(
            program, head, arguments, arity, ready_arguments,
            &decision_candidate_refs, &decision_candidate_count,
            &decision_demanded_argument);
    if (decision_state == PREPARED_PURE_DECISION_NEEDS_ARGUMENT)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_NEEDS_ARGUMENT,
            .demanded_argument = decision_demanded_argument,
        };
    if (decision_state == PREPARED_PURE_DECISION_ERROR)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_ERROR,
        };
    if (decision_state == PREPARED_PURE_DECISION_READY) {
        for (size_t i = 0u; i < decision_candidate_count; i++) {
            uint32_t clause_index = decision_candidate_refs[i];
            if (clause_index >= program->clause_len)
                return (PreparedPureSelection){
                    .state = PREPARED_PURE_SELECT_ERROR,
                };
            PreparedPureSelectState state =
                prepared_pure_consider_clause(
                    program, &program->clauses[clause_index],
                    arguments, arity, ready_arguments,
                    &selected, &needs_argument,
                    &first_demanded_argument);
            if (state == PREPARED_PURE_SELECT_ERROR ||
                state == PREPARED_PURE_SELECT_AMBIGUOUS)
                return (PreparedPureSelection){.state = state};
        }
    } else {
        for (uint32_t i = 0u; i < head->clause_count; i++) {
            const PreparedPureClause *clause =
                &program->clauses[head->first_clause + i];
            if (clause->arity != arity)
                continue;
            PreparedPureSelectState state =
                prepared_pure_consider_clause(
                    program, clause, arguments, arity,
                    ready_arguments, &selected, &needs_argument,
                    &first_demanded_argument);
            if (state == PREPARED_PURE_SELECT_ERROR ||
                state == PREPARED_PURE_SELECT_AMBIGUOUS)
                return (PreparedPureSelection){.state = state};
        }
    }
    if (needs_argument) {
        result.state = PREPARED_PURE_SELECT_NEEDS_ARGUMENT;
        result.demanded_argument = first_demanded_argument;
        return result;
    }
    if (selected) {
        result.state = PREPARED_PURE_SELECT_SELECTED;
        result.clause = selected;
    }
    return result;
}

static bool prepared_pure_is_true(Atom *atom) {
    bool petta_value = false;
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        petta_semantics_truth_value(atom, &petta_value))
        return petta_value;
    return atom_is_symbol_id(atom, g_builtin_syms.true_text) ||
           (atom && atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_BOOL && atom->ground.bval);
}

static bool prepared_pure_is_false(Atom *atom) {
    bool petta_value = true;
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        petta_semantics_truth_value(atom, &petta_value))
        return !petta_value;
    return atom_is_symbol_id(atom, g_builtin_syms.false_text) ||
           (atom && atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_BOOL && !atom->ground.bval);
}

#if CETTA_BUILD_WITH_GMP
typedef struct {
    mpz_t storage;
    mpz_srcptr value;
    bool owns_storage;
} PreparedPureIntegerView;

static bool prepared_pure_integer_view_init(
    Atom *atom, PreparedPureIntegerView *view) {
    if (!atom || !view || atom->kind != ATOM_GROUNDED)
        return false;
    view->value = NULL;
    view->owns_storage = false;
    if (atom->ground.gkind == GV_BIGINT) {
        view->value = atom_bigint_mpz_view(atom);
        return view->value != NULL;
    }
    if (atom->ground.gkind != GV_INT)
        return false;
    mpz_init(view->storage);
    view->owns_storage = true;
    uint64_t magnitude = atom->ground.ival < 0
        ? (uint64_t)(-(atom->ground.ival + 1)) + 1u
        : (uint64_t)atom->ground.ival;
    mpz_import(
        view->storage, 1u, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (atom->ground.ival < 0)
        mpz_neg(view->storage, view->storage);
    view->value = view->storage;
    return true;
}

static void prepared_pure_integer_view_clear(
    PreparedPureIntegerView *view) {
    if (view && view->owns_storage)
        mpz_clear(view->storage);
}
#endif

static bool prepared_pure_numeric_ground_kind(GroundedKind kind) {
    return kind == GV_INT || kind == GV_BIGINT ||
           kind == GV_RATIONAL || kind == GV_FLOAT;
}

/* Grounded literals carry an intrinsic HE/Prime type independent of the
 * mutable annotation space.  Proving that two operands share that intrinsic
 * type lets the generated register machine avoid a full type-service query;
 * cases whose type depends on payload or space state deliberately fall back. */
static bool prepared_pure_grounded_intrinsic_type_equal(
    Atom *left, Atom *right) {
    if (!left || !right || left->kind != ATOM_GROUNDED ||
        right->kind != ATOM_GROUNDED)
        return false;
    GroundedKind left_kind = left->ground.gkind;
    GroundedKind right_kind = right->ground.gkind;
    if (prepared_pure_numeric_ground_kind(left_kind) &&
        prepared_pure_numeric_ground_kind(right_kind))
        return true;
    if (left_kind != right_kind)
        return false;
    switch (left_kind) {
    case GV_BOOL:
    case GV_STRING:
        return true;
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
        return true;
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_FOREIGN:
    case GV_INTERNAL_TAG:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
        /* These values carry identity-, payload-, capability-, or
         * space-dependent typing.  Kind equality alone is not evidence. */
        return false;
    }
    return false;
}

static bool prepared_pure_operands_share_type(
    const CettaPreparedPureProgram *program, Arena *arena,
    Atom *left, Atom *right) {
    if (!program || !arena || !left || !right)
        return false;
    if (program->total_structural_equality || atom_eq(left, right))
        return true;
    if (prepared_pure_grounded_intrinsic_type_equal(left, right)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_INTRINSIC_TYPE_HIT);
        return true;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_TYPE_SERVICE_FALLBACK);

    Atom **left_types = NULL;
    Atom **right_types = NULL;
    uint32_t left_count = eval_get_atom_types_profiled_transient(
        program->space, arena, left, &left_types);
    uint32_t right_count = eval_get_atom_types_profiled_transient(
        program->space, arena, right, &right_types);
    bool compatible = left_count == 0u || right_count == 0u;
    for (uint32_t i = 0u; !compatible && i < left_count; i++) {
        for (uint32_t j = 0u; !compatible && j < right_count; j++) {
            Bindings type_bindings;
            bindings_init(&type_bindings);
            compatible = match_types(
                right_types[j], left_types[i], &type_bindings);
            bindings_free(&type_bindings);
        }
    }
    free(right_types);
    free(left_types);
    return compatible;
}

/* Execute the generated register arm directly.  Dialect dispatch is absent by
 * construction: instruction and result layout were emitted from the active
 * execution-contract presentation when the program was compiled. */
static Atom *prepared_pure_execute_register(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltRegisterInstruction instruction,
    CettaGsltRegisterResultKind expected_kind,
    Atom *const *arguments, uint32_t arity) {
    if (!program || !program->boolean_value || !arena || !arguments ||
        arity != 2u)
        return NULL;
    CettaGsltRegisterOperandDiscipline discipline;
    if (!cetta_gslt_register_operand_discipline(
            instruction, &discipline))
        return NULL;
    if (discipline ==
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS &&
        !prepared_pure_operands_share_type(
            program, arena, arguments[0], arguments[1]))
        return NULL;
    bool atom_boolean_result = false;
    CettaGsltRegisterResultKind atom_result_kind = expected_kind;
    if (cetta_gslt_register_execute_atom_binary(
            instruction, arguments[0], arguments[1],
            &atom_boolean_result, &atom_result_kind)) {
        if (atom_result_kind != expected_kind ||
            atom_result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
            return NULL;
        return program->boolean_value(arena, atom_boolean_result);
    }
    if (arguments[0] && arguments[1] &&
        arguments[0]->kind == ATOM_GROUNDED &&
        arguments[1]->kind == ATOM_GROUNDED &&
        arguments[0]->ground.gkind == GV_INT &&
        arguments[1]->ground.gkind == GV_INT) {
        int64_t integer_result = 0;
        bool boolean_result = false;
        bool promote = false;
        CettaGsltRegisterResultKind actual_kind = expected_kind;
        if (!cetta_gslt_register_execute_small_binary(
                instruction, &integer_result, &boolean_result,
                arguments[0]->ground.ival, arguments[1]->ground.ival,
                &actual_kind, &promote) ||
            actual_kind != expected_kind)
            return NULL;
        if (!promote &&
            actual_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return atom_int(arena, integer_result);
        if (!promote &&
            actual_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
            return program->boolean_value(arena, boolean_result);
    }
#if CETTA_BUILD_WITH_GMP
    PreparedPureIntegerView left = {0};
    PreparedPureIntegerView right = {0};
    mpz_t integer_result;
    mpz_init(integer_result);
    bool boolean_result = false;
    CettaGsltRegisterResultKind actual_kind = expected_kind;
    bool ok = prepared_pure_integer_view_init(arguments[0], &left) &&
              prepared_pure_integer_view_init(arguments[1], &right) &&
              cetta_gslt_register_execute_binary(
                  instruction, integer_result, &boolean_result,
                  left.value, right.value, &actual_kind) &&
              actual_kind == expected_kind;
    Atom *result = NULL;
    if (ok && actual_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
        result = atom_bigint_take_mpz(arena, integer_result);
    else if (ok && actual_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
        result = program->boolean_value(arena, boolean_result);
    prepared_pure_integer_view_clear(&right);
    prepared_pure_integer_view_clear(&left);
    mpz_clear(integer_result);
    return result;
#else
    (void)arena;
    (void)instruction;
    (void)expected_kind;
    (void)arguments;
    (void)arity;
    return NULL;
#endif
}

/* Execute only the successful value arm named by the generated intrinsic
 * program.  A fault, computation zero, or unsupported payload returns NULL;
 * the caller then replays the source through the canonical evaluator. */
static Atom *prepared_pure_execute_intrinsic(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltPreparedPureIntrinsicInstruction instruction,
    Atom *head, Atom *const *arguments, uint32_t arity) {
    if (!program || !program->construct_value || !arena || !head ||
        !arguments)
        return NULL;
    switch (instruction) {
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH:
        return grounded_dispatch(arena, head, (Atom **)arguments, arity);
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION: {
        if (arity != 1u || !arguments[0] ||
            arguments[0]->kind != ATOM_EXPR ||
            arguments[0]->expr.len == 0u)
            return NULL;
        Atom *tail = program->construct_value(
            arena, arguments[0]->expr.elems + 1u,
            arguments[0]->expr.len - 1u);
        if (!tail)
            return NULL;
        Atom *pair[2] = {arguments[0]->expr.elems[0], tail};
        return program->construct_value(arena, pair, 2u);
    }
    }
    return NULL;
}

static bool prepared_pure_push_dynamic_frame(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !atom || program->dynamic_value_len > UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->dynamic_frames,
            sizeof(*program->dynamic_frames),
            &program->dynamic_frame_cap,
            program->dynamic_frame_len + 1u))
        return false;
    program->dynamic_frames[program->dynamic_frame_len++] =
        (PreparedPureDynamicFrame){
            .atom = atom,
            .value_base = (uint32_t)program->dynamic_value_len,
        };
    return true;
}

static bool prepared_pure_push_dynamic_value(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !atom ||
        !prepared_pure_reserve(
            (void **)&program->dynamic_values,
            sizeof(*program->dynamic_values),
            &program->dynamic_value_cap,
            program->dynamic_value_len + 1u))
        return false;
    program->dynamic_values[program->dynamic_value_len++] = atom;
    return true;
}

/* Evaluate the value demanded by an eval-position variable.  This deliberately
 * accepts only the generated register and intrinsic languages.  Any user call,
 * control form, unadmitted grounded extension, or inert constructor expression
 * declines to the canonical evaluator.  The explicit stack keeps nested
 * arithmetic and structural operations off the native C stack. */
static bool prepared_pure_eval_dynamic_register_value(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *input, Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!program || !arena || !input || !result_out)
        return false;
    program->dynamic_frame_len = 0u;
    program->dynamic_value_len = 0u;
    if (!prepared_pure_push_dynamic_frame(program, input))
        return false;
    while (program->dynamic_frame_len > 0u) {
        PreparedPureDynamicFrame *frame =
            &program->dynamic_frames[program->dynamic_frame_len - 1u];
        Atom *current = frame->atom;
        if (current->kind != ATOM_EXPR || current->expr.len == 0u) {
            if (!prepared_pure_push_dynamic_value(program, current))
                return false;
            program->dynamic_frame_len--;
            continue;
        }
        Atom *head = current->expr.elems[0];
        CettaExprLen arity = current->expr.len - 1u;
        CettaGsltRegisterResultKind result_kind = {0};
        CettaGsltRegisterInstruction instruction = {0};
        CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction = {0};
        if (!head || head->kind != ATOM_SYMBOL)
            return false;
        bool is_register = prepared_pure_register_program(
            program, head->sym_id, arity, &result_kind, &instruction);
        bool is_intrinsic = !is_register &&
            prepared_pure_intrinsic_program(
                head->sym_id, arity, &intrinsic_instruction);
        if (!is_register && !is_intrinsic)
            return false;
        if (frame->child_index < arity) {
            if (program->dynamic_value_len !=
                (size_t)frame->value_base + frame->child_index)
                return false;
            Atom *child =
                current->expr.elems[frame->child_index + 1u];
            frame->child_index++;
            if (!prepared_pure_push_dynamic_frame(program, child))
                return false;
            continue;
        }
        if (program->dynamic_value_len !=
            (size_t)frame->value_base + arity)
            return false;
        Atom *result = is_register
            ? prepared_pure_execute_register(
                  program, arena, instruction, result_kind,
                  &program->dynamic_values[frame->value_base], arity)
            : prepared_pure_execute_intrinsic(
                  program, arena, intrinsic_instruction, head,
                  &program->dynamic_values[frame->value_base], arity);
        program->dynamic_value_len = frame->value_base;
        if (!result || atom_is_error(result) ||
            !prepared_pure_push_dynamic_value(program, result))
            return false;
        program->dynamic_frame_len--;
    }
    if (program->dynamic_value_len != 1u ||
        !program->dynamic_values[0])
        return false;
    *result_out = program->dynamic_values[0];
    return true;
}

typedef enum {
    PREPARED_PURE_CHILDREN_FAILED = -1,
    PREPARED_PURE_CHILDREN_PENDING = 0,
    PREPARED_PURE_CHILDREN_READY = 1,
} PreparedPureChildrenState;

static PreparedPureChildrenState prepared_pure_finish_children(
    CettaPreparedPureProgram *program, PreparedPureFrame *frame,
    const PreparedPureNode *node) {
    if (frame->state == 0u) {
        frame->value_base = (uint32_t)program->value_len;
        frame->child_index = 0u;
        frame->state = 1u;
        if (node->child_count == 0u)
            return PREPARED_PURE_CHILDREN_READY;
        if (!prepared_pure_push_frame(
                program, program->children[node->first_child],
                frame->local_base))
            return PREPARED_PURE_CHILDREN_FAILED;
        return PREPARED_PURE_CHILDREN_PENDING;
    }
    if (frame->child_index < node->child_count) {
        if (program->value_len !=
            (size_t)frame->value_base + frame->child_index + 1u)
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->child_index++;
    }
    if (frame->child_index == node->child_count) {
        if (node->kind == PREPARED_PURE_CALL &&
            node->call_arguments_are_values) {
            if (node->child_count > 64u)
                return PREPARED_PURE_CHILDREN_FAILED;
            frame->ready_arguments = node->child_count == 64u
                ? UINT64_MAX
                : (UINT64_C(1) << node->child_count) - UINT64_C(1);
        }
        return PREPARED_PURE_CHILDREN_READY;
    }
    if (!prepared_pure_push_frame(
            program,
            program->children[node->first_child + frame->child_index],
            frame->local_base))
        return PREPARED_PURE_CHILDREN_FAILED;
    return PREPARED_PURE_CHILDREN_PENDING;
}

/* Replace a completed clause's tail-continuation spine with the tail call's
 * already-evaluated argument frame.  The nearest waiting call owns that
 * clause activation; selected branches and let bodies are transparent only
 * when the compiler marked their result spine as tail-position. */
static bool prepared_pure_tail_reenter(
    CettaPreparedPureProgram *program, bool *reentered_out) {
    if (reentered_out)
        *reentered_out = false;
    if (!program || !reentered_out || program->frame_len == 0u)
        return false;
    size_t current = program->frame_len - 1u;
    PreparedPureFrame *tail = &program->frames[current];
    if (tail->node >= program->node_len ||
        program->nodes[tail->node].kind != PREPARED_PURE_CALL ||
        !program->nodes[tail->node].tail_position)
        return true;

    size_t owner = current;
    bool found = false;
    while (owner > 0u) {
        owner--;
        PreparedPureFrame *candidate = &program->frames[owner];
        bool is_call = candidate->node == PREPARED_PURE_RUNTIME_NODE;
        if (!is_call && candidate->node < program->node_len)
            is_call = program->nodes[candidate->node].kind ==
                PREPARED_PURE_CALL;
        if (is_call && candidate->state == 2u) {
            found = true;
            break;
        }
    }
    if (!found)
        return true;
    for (size_t i = owner + 1u; i < current; i++) {
        PreparedPureFrame *intermediate = &program->frames[i];
        if (intermediate->node >= program->node_len ||
            !program->nodes[intermediate->node].tail_position)
            return false;
    }

    PreparedPureFrame *caller = &program->frames[owner];
    if (caller->saved_slot_len > program->slot_len ||
        !prepared_pure_memo_defer_tail(program, caller))
        return false;
    PreparedPureFrame next = *tail;
    Atom *next_atom = program->frame_atoms[current];
    program->slot_len = caller->saved_slot_len;
    program->frames[owner] = next;
    program->frame_atoms[owner] = next_atom;
    program->frame_len = owner + 1u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_TAIL_REENTRY);
    *reentered_out = true;
    return true;
}

static bool prepared_pure_resume_call(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame,
    uint32_t head_index, uint32_t arity) {
    if (!program || !arena || !frame || head_index >= program->head_len)
        return false;
    if (frame->state == 2u) {
        if (program->value_len !=
                (size_t)frame->value_base + 1u ||
            frame->saved_slot_len > program->slot_len)
            return false;
        if (!prepared_pure_memo_complete(
                program, frame,
                program->values[frame->value_base]))
            return false;
        program->slot_len = frame->saved_slot_len;
        program->frame_len--;
        return true;
    }
    if (frame->state == 3u) {
        if (frame->demanded_argument >= arity ||
            frame->demanded_argument >= 64u ||
            program->value_len !=
                (size_t)frame->value_base + arity + 1u)
            return false;
        Atom *value = program->values[--program->value_len];
        if (((!eval_current_language_id ||
              eval_current_language_id() != CETTA_LANGUAGE_PRIME) &&
             atom_is_empty(value)) || atom_is_error(value))
            return false;
        program->values[
            frame->value_base + frame->demanded_argument] = value;
        frame->ready_arguments |=
            UINT64_C(1) << frame->demanded_argument;
        frame->state = 1u;
    }
    if (frame->state != 1u ||
        program->value_len != (size_t)frame->value_base + arity ||
        (program->closed_program && arity > 64u))
        return false;

    if (program->closed_program &&
        program->call_mode == CETTA_GSLT_PURE_CALL_EAGER) {
        for (uint32_t i = 0u; i < arity; i++) {
            uint64_t bit = UINT64_C(1) << i;
            if ((frame->ready_arguments & bit) != 0u)
                continue;
            Atom *argument = program->values[frame->value_base + i];
            if (!prepared_pure_expression_is_callable(
                    program, argument)) {
                frame->ready_arguments |= bit;
                continue;
            }
            frame->demanded_argument = i;
            frame->state = 3u;
            return prepared_pure_push_runtime_frame(program, argument);
        }
    }

    PreparedPureSelection selection = prepared_pure_select_clause(
        program, head_index,
        &program->values[frame->value_base], arity,
        frame->ready_arguments);
    if (selection.state == PREPARED_PURE_SELECT_NEEDS_ARGUMENT) {
        uint32_t argument = selection.demanded_argument;
        if (!program->closed_program || argument >= 64u ||
            argument >= arity)
            return false;
        frame->demanded_argument = argument;
        frame->state = 3u;
        return prepared_pure_push_runtime_frame(
            program, program->values[frame->value_base + argument]);
    }
    const PreparedPureClause *clause = selection.clause;
    if (selection.state != PREPARED_PURE_SELECT_SELECTED || !clause ||
        program->slot_len > UINT32_MAX - clause->local_count ||
        !prepared_pure_reserve(
            (void **)&program->slots,
            sizeof(*program->slots), &program->slot_cap,
            program->slot_len + clause->local_count))
        return false;
    frame = &program->frames[program->frame_len - 1u];
    frame->saved_slot_len = (uint32_t)program->slot_len;
    uint32_t call_base = frame->saved_slot_len;
    if (clause->local_count > 0u)
        memcpy(&program->slots[program->slot_len],
               program->selected_values,
               sizeof(*program->slots) * clause->local_count);
    program->slot_len += clause->local_count;
    program->value_len = frame->value_base;
    frame->state = 2u;
    return prepared_pure_push_frame(
        program, clause->root, call_base);
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    if (!space || !expression || accumulator_var == VAR_ID_NONE ||
        item_var == VAR_ID_NONE || accumulator_var == item_var ||
        !boolean_value || !construct_value)
        return NULL;
    CettaPreparedPureProgram *program = calloc(1u, sizeof(*program));
    if (!program)
        return NULL;
    program->space = space;
    program->read = space_read_token(space);
    program->boolean_value = boolean_value;
    program->construct_value = construct_value;
    program->opaque_value = opaque_value;
    program->register_view = register_view;
    program->expression_view = expression_view;
    program->pattern_view = pattern_view;
    program->call_mode = CETTA_GSLT_PURE_CALL_CALL_BY_NEED;
    program->total_structural_equality = total_structural_equality;
    program->match_decision_semantics = match_decision_semantics;
    PreparedPureCompileContext context = {0};
    if (!prepared_pure_context_bind(
            &context, accumulator_var, false,
            &program->accumulator_slot) ||
        !prepared_pure_context_bind(
            &context, item_var, false, &program->item_slot) ||
        !prepared_pure_compile_eval(
            program, &context, expression, 0u, &program->root)) {
        free(context.bindings);
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    program->root_local_count = context.next_slot;
    free(context.bindings);
    if (!prepared_pure_compile_pending_heads(program)) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    return program;
}

/* A closed Need entry receives suspended arguments, while a CALL_READY entry
 * receives values already computed by its enclosing machine.  Neither is
 * source syntax to recursively recompile.  Entry nodes preserve that boundary
 * and make admission independent of the depth of the carried representation. */
static bool prepared_pure_compile_entry_arguments(
    CettaPreparedPureProgram *program, Atom *expression,
    PreparedPureNodeKind kind, uint32_t **children_out) {
    if (children_out)
        *children_out = NULL;
    if (!program || !expression || !children_out ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        (kind != PREPARED_PURE_ENTRY_ARGUMENT &&
         kind != PREPARED_PURE_EVAL_ENTRY_ARGUMENT) ||
        program->entry_head != SYMBOL_ID_NONE ||
        program->entry_arguments || program->entry_argument_count != 0u)
        return false;

    CettaExprLen arity = expression->expr.len - 1u;
    program->entry_arguments = arity
        ? calloc((size_t)arity, sizeof(*program->entry_arguments))
        : NULL;
    program->entry_argument_count = arity;
    if (arity > 0u && !program->entry_arguments)
        return false;

    uint32_t *children = arity
        ? malloc(sizeof(*children) * (size_t)arity)
        : NULL;
    if (arity > 0u && !children)
        return false;
    for (CettaExprIndex i = 0u; i < arity; i++) {
        if (!prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = kind,
                    .auxiliary = i,
                },
                NULL, 0u, &children[i])) {
            free(children);
            return false;
        }
        program->entry_arguments[i] = expression->expr.elems[i + 1u];
    }
    *children_out = children;
    return true;
}

static bool prepared_pure_compile_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression,
    bool entry_arguments_are_values,
    bool *admitted, uint32_t *root_out) {
    if (admitted)
        *admitted = false;
    if (!program || !expression || !admitted || !root_out ||
        (program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED &&
         !entry_arguments_are_values) ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        !expression->expr.elems[0] ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL)
        return true;

    SymbolId head = expression->expr.elems[0]->sym_id;
    CettaExprLen arity = expression->expr.len - 1u;
    CettaGsltFoldControl control;
    if (prepared_pure_control_program(head, arity, &control)) {
        /* A Need control's operands are invocation data while its scheduling
         * discipline is generated language semantics.  Compile that fixed
         * discipline once and demand only the operands selected at runtime.
         * Bind carries a pattern/environment extension and therefore remains
         * on the ordinary per-expression compiler until it has an equally
         * general parameter representation. */
        if (program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED ||
            (control != CETTA_GSLT_FOLD_CONTROL_BRANCH &&
             control != CETTA_GSLT_FOLD_CONTROL_EVALUATE))
            return true;

        uint32_t *children = NULL;
        *admitted = true;
        if (!prepared_pure_compile_entry_arguments(
                program, expression,
                PREPARED_PURE_EVAL_ENTRY_ARGUMENT, &children))
            return false;
        bool compiled = false;
        if (control == CETTA_GSLT_FOLD_CONTROL_BRANCH && arity == 3u) {
            compiled = prepared_pure_add_node(
                program,
                (PreparedPureNode){.kind = PREPARED_PURE_IF},
                children, arity, root_out);
        } else if (control == CETTA_GSLT_FOLD_CONTROL_EVALUATE &&
                   arity == 1u) {
            *root_out = children[0];
            compiled = true;
        }
        free(children);
        if (compiled)
            program->entry_head = head;
        return compiled;
    }
    /* Generated value rows are expressions, not equation-defined entry
     * relations.  Let compile_eval lower their concrete occurrences. */
    if (prepared_pure_register_program(
            program, head, arity, NULL, NULL) ||
        prepared_pure_intrinsic_program(head, arity, NULL)) {
        return true;
    }
    bool defined = false;
    if (space_query_effect_for_head(
            program->space, head, &defined) !=
            CETTA_GSLT_QUERY_EFFECT_PURE ||
        !defined)
        return true;

    *admitted = true;
    uint32_t head_index = 0u;
    if (!prepared_pure_head_index_admitted(
            program, head, &head_index))
        return false;

    uint32_t *children = NULL;
    if (!prepared_pure_compile_entry_arguments(
            program, expression, PREPARED_PURE_ENTRY_ARGUMENT,
            &children))
        return false;
    bool compiled = prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_CALL,
            .head = head,
            .auxiliary = head_index,
            .call_arguments_are_values = entry_arguments_are_values,
        },
        children, arity, root_out);
    free(children);
    if (compiled)
        program->entry_head = head;
    return compiled;
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    if (!space || !expression || atom_has_vars(expression) ||
        !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED))
        return NULL;
    if (expression->kind == ATOM_EXPR && expression->expr.len > 0u) {
        Atom *root_head = expression->expr.elems[0];
        CettaExprLen root_arity = expression->expr.len - 1u;
        bool generated_expression =
            root_head && root_head->kind == ATOM_SYMBOL &&
            (prepared_pure_control_program(
                 root_head->sym_id, root_arity, NULL) ||
             prepared_pure_register_program(
                 NULL, root_head->sym_id, root_arity, NULL, NULL) ||
             prepared_pure_intrinsic_program(
                 root_head->sym_id, root_arity, NULL));
        if (!generated_expression && root_head &&
            root_head->kind == ATOM_SYMBOL &&
            !CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(
                space, root_head->sym_id,
                root_arity))
            return NULL;
    }
    CettaPreparedPureProgram *program = calloc(1u, sizeof(*program));
    if (!program)
        return NULL;
    program->space = space;
    program->read = space_read_token(space);
    program->boolean_value = boolean_value;
    program->construct_value = construct_value;
    program->opaque_value = opaque_value;
    program->register_view = register_view;
    program->expression_view = expression_view;
    program->pattern_view = pattern_view;
    program->call_mode = call_mode;
    program->total_structural_equality = total_structural_equality;
    program->match_decision_semantics = match_decision_semantics;
    program->closed_program = true;
    program->allow_callable_templates = true;
    PreparedPureCompileContext context = {0};
    bool entry_call_admitted = false;
    bool compiled = prepared_pure_compile_closed_entry_call(
        program, expression, entry_arguments_are_values,
        &entry_call_admitted, &program->root);
    if (compiled && !entry_call_admitted) {
        compiled = prepared_pure_compile_eval(
            program, &context, expression, 0u, &program->root);
    }
    program->root_local_count = context.next_slot;
    free(context.bindings);
    if (!compiled || !prepared_pure_compile_pending_heads(program)) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    return program;
}

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program) {
    return program && space_read_token_is_current(program->read);
}

bool cetta_prepared_pure_program_rebind_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression) {
    if (!program || !expression || !program->closed_program ||
        atom_has_vars(expression) ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        program->root >= program->node_len)
        return false;

    Atom *head = expression->expr.elems[0];
    CettaExprLen arity = expression->expr.len - 1u;
    if (!head || head->kind != ATOM_SYMBOL ||
        program->entry_head == SYMBOL_ID_NONE ||
        program->entry_head != head->sym_id ||
        program->entry_argument_count != arity ||
        (arity > 0u && !program->entry_arguments))
        return false;
    for (CettaExprIndex i = 0u; i < arity; i++) {
        program->entry_arguments[i] = expression->expr.elems[i + 1u];
    }
    return true;
}

void cetta_prepared_pure_program_clear_closed_entry_call(
    CettaPreparedPureProgram *program) {
    if (!program || !program->entry_arguments)
        return;
    memset(program->entry_arguments, 0,
           sizeof(*program->entry_arguments) *
               program->entry_argument_count);
}

static bool prepared_pure_program_execute_internal(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom *runtime_expression,
    bool closed,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context,
    Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!program || !arena || !result_out ||
        program->closed_program != closed ||
        (!closed && (!accumulator || !item)) ||
        (!closed && runtime_expression) ||
        (runtime_expression && atom_has_vars(runtime_expression)) ||
        !cetta_prepared_pure_program_is_current(program) ||
        !prepared_pure_reserve(
            (void **)&program->slots, sizeof(*program->slots),
            &program->slot_cap, program->root_local_count))
        return prepared_pure_runtime_decline(
            program, "invalid or stale execution request", NULL);
    prepared_pure_gc_discard_survivor(program);
    prepared_pure_memo_clear(program);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->slot_len = program->root_local_count;
    if (program->slot_len > 0u)
        memset(program->slots, 0,
               sizeof(*program->slots) * program->slot_len);
    if (!closed) {
        program->slots[program->accumulator_slot] = accumulator;
        program->slots[program->item_slot] = item;
    }
    bool pushed_root = runtime_expression
        ? prepared_pure_push_runtime_frame(program, runtime_expression)
        : prepared_pure_push_frame(program, program->root, 0u);
    if (!pushed_root)
        return prepared_pure_runtime_decline(
            program, "cannot push root frame", NULL);

    ArenaMark gc_anchor = arena_mark(arena);
    size_t gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
        program, arena, gc_anchor, nursery_budget_bytes);
    uint32_t interrupt_poll_steps = 0u;

    while (program->frame_len > 0u) {
        if (interrupt_poll &&
            (interrupt_poll_steps++ & 255u) == 0u &&
            interrupt_poll(interrupt_context)) {
            return prepared_pure_runtime_decline(
                program, "execution interrupted", NULL);
        }
        size_t gc_current_bytes = prepared_pure_saturating_add(
            arena->live_bytes, arena->external_bytes);
        if (closed && nursery_budget_bytes != 0u &&
            gc_current_bytes >= gc_trigger_bytes) {
            prepared_pure_gc_collect(program, arena, gc_anchor);
            gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
                program, arena, gc_anchor, nursery_budget_bytes);
        }
        PreparedPureFrame *frame =
            &program->frames[program->frame_len - 1u];
        if (frame->node == PREPARED_PURE_RUNTIME_NODE) {
            Atom *source = program->frame_atoms[program->frame_len - 1u];
            if (!source)
                return prepared_pure_runtime_decline(
                    program, "runtime frame has no expression", NULL);
            if (frame->state == 0u) {
                if (source->kind == ATOM_EXPR &&
                    program->expression_view) {
                    CettaPreparedPureExpressionView view = {0};
                    CettaPreparedPureExpressionViewState state =
                        program->expression_view(source, &view);
                    if (state ==
                        CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY) {
                        return prepared_pure_runtime_decline(
                            program,
                            "dynamic dialect form requires canonical evaluation",
                            NULL);
                    }
                }
                if (!prepared_pure_expression_is_callable(
                        program, source)) {
                    if (!prepared_pure_push_value(program, source))
                        return prepared_pure_runtime_decline(
                            program, "cannot push runtime value", NULL);
                    program->frame_len--;
                    continue;
                }
                if (program->call_mode ==
                        CETTA_GSLT_PURE_CALL_CALL_BY_NEED) {
                    uint32_t memo_index = PREPARED_PURE_NO_MEMO;
                    bool existing = false;
                    if (!prepared_pure_memo_begin(
                            program, source, &memo_index, &existing))
                        return prepared_pure_runtime_decline(
                            program, "cannot allocate thunk update cell",
                            NULL);
                    if (existing) {
                        if (memo_index >= program->memo_len)
                            return prepared_pure_runtime_decline(
                                program, "thunk update cell is invalid",
                                NULL);
                        if (program->memo_states[memo_index] ==
                                PREPARED_PURE_MEMO_VALUE) {
                            Atom *memo_value =
                                program->memo_values[memo_index];
                            if (!memo_value ||
                                !prepared_pure_push_value(
                                    program, memo_value))
                                return prepared_pure_runtime_decline(
                                    program, "memoized thunk has no value",
                                    NULL);
                            cetta_runtime_stats_inc(
                                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_HIT);
                            program->frame_len--;
                            continue;
                        }
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_BLACKHOLE);
                        return prepared_pure_runtime_decline(
                            program, "recursive thunk blackhole", NULL);
                    }
                    frame->memo_index = memo_index;
                }
                if (source->kind != ATOM_EXPR ||
                    source->expr.len == 0u ||
                    !source->expr.elems[0] ||
                    source->expr.elems[0]->kind != ATOM_SYMBOL)
                    return prepared_pure_runtime_decline(
                        program, "runtime operator is not a symbol", NULL);
                SymbolId head = source->expr.elems[0]->sym_id;
                uint32_t arity = source->expr.len - 1u;
                CettaGsltFoldControl control;
                if (prepared_pure_control_program(
                        head, arity, &control)) {
                    frame->value_base = (uint32_t)program->value_len;
                    if (control == CETTA_GSLT_FOLD_CONTROL_EVALUATE &&
                        arity == 1u) {
                        frame->state = 20u;
                        if (!prepared_pure_push_runtime_frame(
                                program, source->expr.elems[1]))
                            return prepared_pure_runtime_decline(
                                program, "cannot push dynamic evaluation",
                                NULL);
                        continue;
                    }
                    if (control == CETTA_GSLT_FOLD_CONTROL_BRANCH &&
                        arity == 3u) {
                        frame->state = 30u;
                        if (!prepared_pure_push_runtime_frame(
                                program, source->expr.elems[1]))
                            return prepared_pure_runtime_decline(
                                program,
                                "cannot push dynamic branch condition",
                                NULL);
                        continue;
                    }
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic control lies outside the machine",
                        NULL);
                }
                CettaGsltRegisterResultKind result_kind = {0};
                CettaGsltRegisterInstruction instruction = {0};
                CettaGsltPreparedPureIntrinsicInstruction
                    intrinsic_instruction = {0};
                bool generated_value_program =
                    prepared_pure_register_program(program,
                        head, arity, &result_kind, &instruction);
                if (!generated_value_program) {
                    generated_value_program =
                        prepared_pure_intrinsic_program(
                            head, arity, &intrinsic_instruction);
                }
                if (generated_value_program) {
                    (void)result_kind;
                    (void)instruction;
                    (void)intrinsic_instruction;
                    frame->value_base = (uint32_t)program->value_len;
                    frame->child_index = 0u;
                    frame->state = 10u;
                    if (arity == 0u)
                        return prepared_pure_runtime_decline(
                            program, "zero-arity generated instruction",
                            NULL);
                    if (!prepared_pure_push_runtime_frame(
                            program, source->expr.elems[1]))
                        return prepared_pure_runtime_decline(
                            program, "cannot push generated argument", NULL);
                    continue;
                }
                uint32_t head_index = 0u;
                if (!prepared_pure_runtime_head_index(
                        program, head, &head_index))
                    return prepared_pure_runtime_decline(
                        program, "runtime head is not revision-pinned pure",
                        NULL);
                frame = &program->frames[program->frame_len - 1u];
                frame->runtime_head_index = head_index;
                frame->value_base = (uint32_t)program->value_len;
                for (uint32_t i = 0u; i < arity; i++) {
                    if (!prepared_pure_push_value(
                            program, source->expr.elems[i + 1u]))
                        return prepared_pure_runtime_decline(
                            program, "cannot stage runtime argument", NULL);
                }
                frame->state = 1u;
            }
            frame = &program->frames[program->frame_len - 1u];
            source = program->frame_atoms[program->frame_len - 1u];
            uint32_t arity = source->expr.len - 1u;
            if (frame->state == 20u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic evaluation return invariant failed",
                        NULL);
                if (!prepared_pure_memo_complete(
                        program, frame,
                        program->values[frame->value_base]))
                    return prepared_pure_runtime_decline(
                        program, "cannot update demanded thunk", NULL);
                program->frame_len--;
                continue;
            }
            if (frame->state == 30u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic branch condition invariant failed",
                        NULL);
                Atom *condition =
                    program->values[--program->value_len];
                CettaExprIndex branch;
                if (prepared_pure_is_true(condition))
                    branch = 2u;
                else if (prepared_pure_is_false(condition))
                    branch = 3u;
                else
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic branch condition is not boolean",
                        NULL);
                frame->state = 31u;
                if (!prepared_pure_push_runtime_frame(
                        program, source->expr.elems[branch]))
                    return prepared_pure_runtime_decline(
                        program, "cannot push dynamic selected branch",
                        NULL);
                continue;
            }
            if (frame->state == 31u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic selected branch invariant failed",
                        NULL);
                if (!prepared_pure_memo_complete(
                        program, frame,
                        program->values[frame->value_base]))
                    return prepared_pure_runtime_decline(
                        program, "cannot update dynamic branch thunk",
                        NULL);
                program->frame_len--;
                continue;
            }
            if (frame->state == 10u) {
                if (program->value_len !=
                    (size_t)frame->value_base + frame->child_index + 1u)
                    return prepared_pure_runtime_decline(
                        program, "generated argument invariant failed",
                        NULL);
                frame->child_index++;
                if (frame->child_index < arity) {
                    if (!prepared_pure_push_runtime_frame(
                            program,
                            source->expr.elems[frame->child_index + 1u]))
                        return prepared_pure_runtime_decline(
                            program, "cannot push generated argument", NULL);
                    continue;
                }
                CettaGsltRegisterResultKind result_kind = {0};
                CettaGsltRegisterInstruction instruction = {0};
                CettaGsltPreparedPureIntrinsicInstruction
                    intrinsic_instruction = {0};
                Atom *head = source->expr.elems[0];
                bool is_register = prepared_pure_register_program(
                    program, head->sym_id, arity,
                    &result_kind, &instruction);
                bool is_intrinsic = !is_register &&
                    prepared_pure_intrinsic_program(
                        head->sym_id, arity, &intrinsic_instruction);
                if (!is_register && !is_intrinsic)
                    return prepared_pure_runtime_decline(
                        program, "generated descriptor changed", NULL);
                Atom *result = is_register
                    ? prepared_pure_execute_register(
                          program, arena, instruction, result_kind,
                          &program->values[frame->value_base], arity)
                    : prepared_pure_execute_intrinsic(
                          program, arena, intrinsic_instruction, head,
                          &program->values[frame->value_base], arity);
                program->value_len = frame->value_base;
                if (!result || atom_is_error(result) ||
                    !prepared_pure_push_value(program, result))
                    return prepared_pure_runtime_decline(
                        program, "dynamic generated arm declined", NULL);
                if (!prepared_pure_memo_complete(
                        program, frame, result))
                    return prepared_pure_runtime_decline(
                        program, "cannot update demanded thunk", NULL);
                program->frame_len--;
                continue;
            }
            if (!prepared_pure_resume_call(
                    program, arena, frame,
                    frame->runtime_head_index, arity))
                return prepared_pure_runtime_decline(
                    program, "runtime call declined", NULL);
            continue;
        }
        if (frame->node >= program->node_len)
            return prepared_pure_runtime_decline(
                program, "frame references an invalid node", NULL);
        const PreparedPureNode *node = &program->nodes[frame->node];
        if (node->kind == PREPARED_PURE_LITERAL) {
            if (!prepared_pure_push_value(program, node->atom))
                return prepared_pure_runtime_decline(
                    program, "cannot push literal", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_ENTRY_ARGUMENT) {
            if (node->auxiliary >= program->entry_argument_count ||
                !program->entry_arguments[node->auxiliary] ||
                !prepared_pure_push_value(
                    program,
                    program->entry_arguments[node->auxiliary]))
                return prepared_pure_runtime_decline(
                    program, "missing invocation entry argument", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_EVAL_ENTRY_ARGUMENT) {
            if (node->auxiliary >= program->entry_argument_count ||
                !program->entry_arguments[node->auxiliary])
                return prepared_pure_runtime_decline(
                    program, "missing demanded entry argument", node);
            if (frame->state == 0u) {
                Atom *source =
                    program->entry_arguments[node->auxiliary];
                if (!prepared_pure_expression_is_callable(
                        program, source)) {
                    if (!prepared_pure_push_value(program, source))
                        return prepared_pure_runtime_decline(
                            program, "cannot push demanded entry value",
                            node);
                    program->frame_len--;
                    continue;
                }
                frame->value_base = (uint32_t)program->value_len;
                frame->state = 1u;
                if (!prepared_pure_push_runtime_frame(program, source))
                    return prepared_pure_runtime_decline(
                        program,
                        "demanded entry argument lies outside the machine",
                        node);
                continue;
            }
            if (frame->state != 1u ||
                program->value_len !=
                    (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program,
                    "demanded entry argument return invariant failed",
                    node);
            program->entry_arguments[node->auxiliary] =
                program->values[frame->value_base];
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_SLOT) {
            size_t slot = (size_t)frame->local_base + node->auxiliary;
            if (slot >= program->slot_len || !program->slots[slot] ||
                !prepared_pure_push_value(program, program->slots[slot]))
                return prepared_pure_runtime_decline(
                    program, "missing positional slot", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_EVAL_SLOT) {
            size_t slot = (size_t)frame->local_base + node->auxiliary;
            if (program->closed_program) {
                if (frame->state == 0u) {
                    if (slot >= program->slot_len || !program->slots[slot])
                        return prepared_pure_runtime_decline(
                            program, "missing dynamic positional slot", node);
                    Atom *source = program->slots[slot];
                    if (!prepared_pure_expression_is_callable(
                            program, source)) {
                        if (!prepared_pure_push_value(program, source))
                            return prepared_pure_runtime_decline(
                                program, "cannot push dynamic value", node);
                        program->frame_len--;
                        continue;
                    }
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_runtime_frame(
                            program, source))
                        return prepared_pure_runtime_decline(
                            program, "dynamic value lies outside the machine",
                            NULL);
                    continue;
                }
                if (frame->state != 1u ||
                    program->value_len !=
                        (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "dynamic value return invariant failed",
                        node);
                /* Call-by-need is call-by-name plus update.  Every later use
                 * of this positional binding must see the demanded value,
                 * rather than rebuilding and re-evaluating the original
                 * suspension. */
                program->slots[slot] =
                    program->values[frame->value_base];
                program->frame_len--;
                continue;
            }
            Atom *evaluated = NULL;
            if (slot >= program->slot_len || !program->slots[slot] ||
                !prepared_pure_eval_dynamic_register_value(
                    program, arena, program->slots[slot], &evaluated) ||
                !prepared_pure_push_value(program, evaluated))
                return prepared_pure_runtime_decline(
                    program, "dynamic register value declined", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_BIND) {
            if (frame->state == 0u) {
                frame->value_base = (uint32_t)program->value_len;
                frame->state = 1u;
                if (!prepared_pure_push_frame(
                        program, program->children[node->first_child],
                        frame->local_base))
                    return prepared_pure_runtime_decline(
                        program, "cannot push let binding", node);
                continue;
            }
            if (frame->state == 1u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "let binding produced wrong arity", node);
                if (node->auxiliary >= program->bind_pattern_len)
                    return prepared_pure_runtime_decline(
                        program, "let pattern descriptor is invalid", node);
                Atom *value = program->values[program->value_len - 1u];
                PreparedPureMatchState matched =
                    prepared_pure_match_bind_pattern(
                        program, &program->bind_patterns[node->auxiliary],
                        frame->local_base, value);
                if (matched != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_runtime_decline(
                        program, matched == PREPARED_PURE_MATCH_MISMATCH
                            ? "let pattern did not match"
                            : "let pattern matcher failed",
                        node);
                program->value_len--;
                frame->state = 2u;
                if (!prepared_pure_push_frame(
                        program,
                        program->children[node->first_child + 1u],
                        frame->local_base))
                    return prepared_pure_runtime_decline(
                        program, "cannot push let body", node);
                continue;
            }
            if (program->value_len !=
                (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program, "let body produced wrong arity", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_IF) {
            if (frame->state == 0u) {
                frame->value_base = (uint32_t)program->value_len;
                frame->state = 1u;
                if (!prepared_pure_push_frame(
                        program, program->children[node->first_child],
                        frame->local_base))
                    return prepared_pure_runtime_decline(
                        program, "cannot push branch condition", node);
                continue;
            }
            if (frame->state == 1u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "branch condition produced wrong arity", node);
                Atom *condition =
                    program->values[--program->value_len];
                uint32_t branch;
                if (prepared_pure_is_true(condition))
                    branch = 1u;
                else if (prepared_pure_is_false(condition))
                    branch = 2u;
                else
                    return prepared_pure_runtime_decline(
                        program, "non-boolean branch condition", node);
                frame->state = 2u;
                if (!prepared_pure_push_frame(
                        program,
                        program->children[node->first_child + branch],
                        frame->local_base))
                    return prepared_pure_runtime_decline(
                        program, "cannot push selected branch", node);
                continue;
            }
            if (program->value_len !=
                (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program, "selected branch produced wrong arity", node);
            program->frame_len--;
            continue;
        }

        if (node->kind == PREPARED_PURE_CALL &&
            (frame->state == 2u || frame->state == 3u)) {
            if (!prepared_pure_resume_call(
                    program, arena, frame, node->auxiliary,
                    node->child_count))
                return prepared_pure_runtime_decline(
                    program, "user call continuation failed", node);
            continue;
        }

        PreparedPureChildrenState children_state =
            prepared_pure_finish_children(program, frame, node);
        if (children_state == PREPARED_PURE_CHILDREN_FAILED)
            return prepared_pure_runtime_decline(
                program, "child scheduling failed", node);
        if (children_state == PREPARED_PURE_CHILDREN_PENDING)
            continue;
        frame = &program->frames[program->frame_len - 1u];
        node = &program->nodes[frame->node];
        if (program->value_len !=
            (size_t)frame->value_base + node->child_count)
            return prepared_pure_runtime_decline(
                program, "child evaluation arity invariant failed", node);

        if (node->kind == PREPARED_PURE_BUILD) {
            Atom *built = program->construct_value(
                arena, &program->values[frame->value_base],
                node->child_count);
            program->value_len = frame->value_base;
            if (!prepared_pure_push_value(program, built))
                return prepared_pure_runtime_decline(
                    program, "cannot push constructed value", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_OBSERVE) {
            if (node->auxiliary !=
                    CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION ||
                node->child_count > 1u)
                return prepared_pure_runtime_decline(
                    program, "invalid value observation descriptor", node);
            Atom *operand = node->child_count == 1u
                ? program->values[frame->value_base]
                : node->atom;
            Atom *result = operand
                ? program->boolean_value(
                      arena, operand->kind == ATOM_EXPR)
                : NULL;
            program->value_len = frame->value_base;
            if (!result || !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "value observation failed", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_REGISTER) {
            Atom *result = prepared_pure_execute_register(
                program, arena, node->instruction, node->result_kind,
                &program->values[frame->value_base], node->child_count);
            program->value_len = frame->value_base;
            if (!result || atom_is_error(result) ||
                !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "generated register arm declined", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_INTRINSIC) {
            Atom *result = prepared_pure_execute_intrinsic(
                program, arena, node->intrinsic_instruction, node->atom,
                &program->values[frame->value_base], node->child_count);
            program->value_len = frame->value_base;
            if (!result || atom_is_error(result) ||
                !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "generated intrinsic arm declined", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_CALL) {
            bool tail_reentered = false;
            if (!prepared_pure_tail_reenter(
                    program, &tail_reentered))
                return prepared_pure_runtime_decline(
                    program, "tail-call frame reuse failed", node);
            if (tail_reentered)
                frame = &program->frames[program->frame_len - 1u];
            if (!prepared_pure_resume_call(
                    program, arena, frame, node->auxiliary,
                    node->child_count))
                return prepared_pure_runtime_decline(
                    program, "user call was not uniquely matched", node);
            continue;
        }
        return prepared_pure_runtime_decline(
            program, "unknown machine node", node);
    }
    if (program->value_len != 1u || !program->values[0])
        return prepared_pure_runtime_decline(
            program, "machine did not produce one value", NULL);
    Atom *result = program->values[0];
    if (!prepared_pure_memo_complete_deferred(program, result))
        return prepared_pure_runtime_decline(
            program, "cannot complete deferred tail updates", NULL);
    /* A runtime-expression execution is an internal weak-head step: its
     * caller consumes the value synchronously and owns any continuation
     * identity proof.  The ordinary closed-entry ABI publishes its result,
     * so it retains the stricter duplicate-suspension boundary. */
    if (!runtime_expression &&
        prepared_pure_result_has_escaping_suspension(program, result))
        return prepared_pure_runtime_decline(
            program,
            "call-by-need result requires a virtual suspension",
            NULL);
    prepared_pure_memo_clear(program);
    *result_out = result;
    return true;
}

bool cetta_prepared_pure_program_execute(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, accumulator, item, NULL, false, 0u,
        NULL, NULL, result_out);
}

bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, accumulator, item, NULL, false, 0u,
        interrupt_poll, interrupt_context, result_out);
}

bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, NULL, true,
        nursery_budget_bytes, NULL, NULL, result_out);
}

bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, NULL, true,
        nursery_budget_bytes, interrupt_poll, interrupt_context,
        result_out);
}

bool cetta_prepared_pure_program_execute_closed_expression_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *expression, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, expression, true,
        nursery_budget_bytes, interrupt_poll, interrupt_context,
        result_out);
}

static bool prepared_pure_value_has_callable_node(
    const Atom *atom, void *context) {
    return prepared_pure_expression_is_callable(
        context, (Atom *)atom);
}

bool cetta_prepared_pure_program_value_is_fully_evaluated(
    CettaPreparedPureProgram *program, Atom *value) {
    return program && value &&
           !atom_tree_any(
               value, prepared_pure_value_has_callable_node, program);
}

void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    prepared_pure_gc_discard_survivor(program);
    prepared_pure_memo_clear(program);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->slot_len = 0u;
}

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    free(program->nodes);
    free(program->entry_arguments);
    free(program->children);
    free(program->heads);
    free(program->head_buckets);
    free(program->callable_buckets);
    free(program->clauses);
    for (size_t index = 0u; index < program->decision_len; index++)
        cetta_match_decision_free(program->decisions[index].selector);
    free(program->decisions);
    free(program->pattern_vars);
    free(program->bind_patterns);
    free(program->bind_vars);
    free(program->live_slots);
    free(program->frames);
    free(program->frame_atoms);
    free(program->values);
    free(program->slots);
    free(program->slot_live);
    free(program->match_values);
    free(program->selected_values);
    free(program->pattern_pairs);
    free(program->dynamic_frames);
    free(program->dynamic_values);
    free(program->memo_keys);
    free(program->memo_values);
    free(program->memo_states);
    free(program->memo_buckets);
    prepared_pure_gc_discard_survivor(program);
    free(program);
}
