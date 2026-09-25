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
    /* Choice zero: no answer.  Only answer producers compile it; their step
     * lowering turns it into a failing step, so no executor ever meets it. */
    PREPARED_PURE_ZERO,
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
    /* A register over typed structural operands: bit i marks operand i as a
     * literal symbol whose only type is %Undefined%.  match_types takes that
     * type as a wildcard, so the operands share a type without a query; the
     * source program token pins the declarations the fact was read from. */
    uint8_t undefined_type_operands;
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
    /* A non-variable head over distinct fresh variables: field i binds
     * bind_vars[first_var + i - 1]. */
    bool flat_fresh_fields;
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
#define PREPARED_PURE_DECLARE_VARIANT_INSTANCE(name) void *name;
#define PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP(name) \
    PreparedPureEphemeronAtomMap name;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(
        PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT,
        PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN,
        PREPARED_PURE_DECLARE_LOGICAL_BINDINGS,
        PREPARED_PURE_DECLARE_OUTCOME_SET,
        PREPARED_PURE_DECLARE_VARIANT_INSTANCE,
        PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP)
} PreparedPureGcRoots;
#undef PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP
#undef PREPARED_PURE_DECLARE_OUTCOME_SET
#undef PREPARED_PURE_DECLARE_VARIANT_INSTANCE
#undef PREPARED_PURE_DECLARE_LOGICAL_BINDINGS
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT

typedef struct {
    Atom *lhs;
    /* The Space occurrence compiled here; a resumed enumeration names its
     * next untried equation by it. */
    Atom *equation;
    CettaIndex logical_index;
    uint32_t arity;
    uint32_t root;
    uint32_t local_count;
    uint32_t first_pattern_var;
    uint32_t pattern_var_count;
    uint32_t scalar_guard;
    /* When every argument pattern is a variable, argument i binds the slot
     * argument_slots[first_argument_slot + i]. */
    uint32_t first_argument_slot;
    bool variable_arguments;
    /* Otherwise, when no pattern node is a dialect view form, matching
     * ready arguments runs match_ops[first_match_op, +match_op_count) over
     * match_register_count registers, the first `arity` holding the
     * arguments. */
    bool compiled_match;
    uint32_t first_match_op;
    uint32_t match_op_count;
    uint32_t match_register_count;
    /* The position among those ops of the first bind, or UINT32_MAX:
     * a check failing after it fails after a variable was bound. */
    uint32_t first_bind_op;
    /* A pattern variable occurs more than once.  Its first occurrence binds
     * it; each later one requires an equal value, as the canonical matcher
     * decides a pair of ground values.  Compiled only where every argument
     * is a value when equations are matched. */
    bool repeated_variables;
    /* An answer producer's equation whose body is a value tree or a last
     * call over value trees answers directly.  Any other runs as the step
     * program steps[first_step, +step_count) over frame_slot_count locals:
     * its local_count, then temporaries. */
    bool tail_only;
    uint32_t first_step;
    uint32_t step_count;
    uint32_t frame_slot_count;
} PreparedPureEquation;

typedef enum {
    /* slot <- the value of node */
    PREPARED_PURE_STEP_EVAL = 0,
    /* slot <- each answer of the head applied to the argument slots */
    PREPARED_PURE_STEP_CALL,
    /* The answers of the head applied to the argument slots are this body's
     * answers. */
    PREPARED_PURE_STEP_TAIL,
    /* Bind the pattern against slot; a mismatch leaves no answer. */
    PREPARED_PURE_STEP_MATCH,
    /* True in slot continues, False jumps to target. */
    PREPARED_PURE_STEP_BRANCH,
    PREPARED_PURE_STEP_JUMP,
    /* The value of node is an answer of this body. */
    PREPARED_PURE_STEP_RETURN,
    /* No answer. */
    PREPARED_PURE_STEP_FAIL,
} PreparedPureStepKind;

typedef struct {
    uint8_t kind;
    /* EVAL and RETURN: the node is a value tree, built without the executor. */
    bool value_tree;
    uint32_t node;
    uint32_t slot;
    uint32_t target;
    /* CALL and TAIL: the head index; MATCH: the bind pattern. */
    uint32_t auxiliary;
    /* CALL and TAIL: argument slots step_arguments[first_argument, +arity). */
    uint32_t first_argument;
    uint32_t arity;
    /* CALL: a node for the rest of the equation body once the call's answer
     * is in `slot` -- operands already evaluated are slot reads, the rest is
     * source structure.  It is deoptimization metadata: decoding it gives
     * the source computation the continuation stands for.  NO_TEMPLATE when
     * the lowering could not describe it. */
    uint32_t resume_template;
} PreparedPureStep;

#define PREPARED_PURE_NO_TEMPLATE UINT32_MAX

typedef enum {
    /* Slot `operand` takes register `source`. */
    PREPARED_PURE_MATCH_OP_BIND = 0,
    /* Register `source` equals the atomic literal. */
    PREPARED_PURE_MATCH_OP_ATOM = 1,
    /* Register `source` is an expression of `length` elements, which load
     * into registers `operand` onward. */
    PREPARED_PURE_MATCH_OP_EXPR = 2,
    /* Register `source` equals the value slot `operand` took earlier. */
    PREPARED_PURE_MATCH_OP_SAME = 3,
} PreparedPureMatchOpKind;

typedef struct {
    Atom *literal;
    uint32_t source;
    uint32_t operand;
    uint32_t length;
    uint8_t kind;
} PreparedPureMatchOp;

enum {
    PREPARED_PURE_MATCH_MAX_REGISTERS = 64u,
    PREPARED_PURE_MATCH_MAX_OPS = 256u,
};

typedef struct {
    Atom *literal;
    uint32_t slot;
    bool from_slot;
} PreparedPureScalarGuardArgument;

typedef struct {
    Atom *head;
    uint32_t first_argument;
    uint32_t argument_count;
    bool expected_truth;
} PreparedPureScalarGuard;

typedef struct {
    uint32_t arity;
    uint32_t equation_count;
    uint64_t universally_constrained_arguments;
    CettaMatchDecision *selector;
} PreparedPureDecisionProgram;

typedef struct {
    SymbolId head;
    uint32_t first_equation;
    uint32_t equation_count;
    uint32_t first_decision;
    uint32_t decision_count;
    bool compiled;
    /* Weak-head matching selects at most one equation, and no body reaches
     * choice: a call that is not deterministic, or choice zero. */
    bool deterministic;
    /* A type annotation names the head. */
    bool declares_type;
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

/* Keep the mutually recursive answer interpreter in one compiler-managed hot
 * text region.  This also isolates its branch layout from unrelated growth in
 * storage-provider translation units. */
#if defined(__GNUC__) || defined(__clang__)
#define PREPARED_PURE_HOT __attribute__((hot))
#define PREPARED_PURE_NOINLINE __attribute__((noinline))
#else
#define PREPARED_PURE_HOT
#define PREPARED_PURE_NOINLINE
#endif

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
    /* Owners: the creator, and each answer cursor reading the program. */
    uint32_t references;
    uint64_t host_stamp;
    /* Whether a node among the first structural_scan_len builds a value. */
    size_t structural_scan_len;
    bool builds_structural_values;
    Space *space;
    SpaceProgramToken source_program;
    SpaceEquationToken equation_projection;
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
    bool answer_producer;
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
    PreparedPureEquation *equations;
    size_t equation_len;
    size_t equation_cap;
    PreparedPureScalarGuard *scalar_guards;
    size_t scalar_guard_len;
    size_t scalar_guard_cap;
    PreparedPureScalarGuardArgument *scalar_guard_arguments;
    size_t scalar_guard_argument_len;
    size_t scalar_guard_argument_cap;
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
    uint32_t *argument_slots;
    size_t argument_slot_len;
    size_t argument_slot_cap;
    PreparedPureMatchOp *match_ops;
    size_t match_op_len;
    size_t match_op_cap;
    /* Some equation is not tail-only, so its answers resume continuations. */
    bool continuation_steps;
    PreparedPureStep *steps;
    size_t step_len;
    size_t step_cap;
    uint32_t *step_arguments;
    size_t step_argument_len;
    size_t step_argument_cap;

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

    /* Translation evidence is consulted only while the immutable node graph
     * is compiled.  Keep it after execution-hot frames, values, slots, and
     * memo state so adding a source observer does not perturb their layout. */
    CettaPreparedPureSourceView source_view;

    Arena gc_survivor;
    bool gc_survivor_ready;
    size_t gc_survivor_bytes;
    size_t gc_low_reclaim_growth_bytes;

};

enum {
    PREPARED_PURE_MAX_COMPILE_DEPTH = 256u,
    PREPARED_PURE_MAX_HEADS = 4096u,
    PREPARED_PURE_MAX_EQUATIONS = 65536u,
    PREPARED_PURE_MAX_SLOTS = 65536u,
    /* Sparse decision lookup is load-bearing for large generated equation
     * families, while the direct matcher is cheaper for tiny groups. */
    PREPARED_PURE_DECISION_MIN_EQUATIONS = 8u,
    PREPARED_PURE_GC_INITIAL_NURSERY_INTERVALS = 3u,
    PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS = 16u,
};

#define PREPARED_PURE_NO_SCALAR_GUARD UINT32_MAX

static bool prepared_pure_debug_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *debug = getenv("CETTA_PREPARED_PURE_DEBUG");
        enabled = debug && debug[0] != '\0' && debug[0] != '0';
    }
    return enabled != 0;
}

static bool prepared_pure_scalar_guard_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PREPARED_PURE_SCALAR_GUARD_REFERENCE");
        enabled = !(reference && reference[0] != '\0' &&
                    reference[0] != '0');
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

/* Node kinds are fixed when a node is appended, and runtime head compilation
 * only appends, so the answer extends over the nodes added since the last
 * scan. */
static bool prepared_pure_plan_builds_structural_values(
    CettaPreparedPureProgram *program) {
    if (!program)
        return false;
    for (; program->structural_scan_len < program->node_len;
         program->structural_scan_len++) {
        const PreparedPureNode *node =
            &program->nodes[program->structural_scan_len];
        if (node->kind == PREPARED_PURE_BUILD ||
            (node->kind == PREPARED_PURE_INTRINSIC &&
             node->intrinsic_instruction ==
                 CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION))
            program->builds_structural_values = true;
    }
    return program->builds_structural_values;
}

static size_t prepared_pure_gc_trigger_bytes(
    CettaPreparedPureProgram *program, const Arena *arena,
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
    bool flat = pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
        pattern->expr.elems[0] &&
        pattern->expr.elems[0]->kind != ATOM_VAR &&
        pattern->expr.elems[0]->kind != ATOM_EXPR &&
        count_size == pattern->expr.len - 1u;
    for (CettaExprIndex i = 1u; flat && i < pattern->expr.len; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[first_var + i - 1u];
        flat = pattern->expr.elems[i] &&
            pattern->expr.elems[i]->kind == ATOM_VAR &&
            binding->var == pattern->expr.elems[i]->var_id &&
            !binding->prebound;
    }
    *pattern_index_out = (uint32_t)program->bind_pattern_len;
    program->bind_patterns[program->bind_pattern_len++] =
        (PreparedPureBindPattern){
            .pattern = pattern,
            .first_var = first_var,
            .var_count = (uint32_t)count_size,
            .flat_fresh_fields = flat,
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
    if (!space_program_token_is_current(program->source_program))
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
        !defined ||
        !space_program_token_is_current(program->source_program)) {
        return prepared_pure_reject(
            program, "user head is not revision-pinned pure", NULL);
    }
    return prepared_pure_head_index_admitted(
        program, head, index_out);
}

static bool prepared_pure_compile_template(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
    uint32_t *node_out);

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, uint32_t *node_out);

static CettaPreparedPureSourceRole prepared_pure_source_role(
        const CettaPreparedPureProgram *program,
        const void *source_view) {
    if (!program || !source_view || !program->source_view.role)
        return CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED;
    return program->source_view.role(
        program->source_view.context, source_view);
}

static const void *prepared_pure_source_child(
        const CettaPreparedPureProgram *program,
        const void *source_view, CettaExprIndex child_index) {
    if (!program || !source_view || !program->source_view.child)
        return NULL;
    return program->source_view.child(
        program->source_view.context, source_view, child_index);
}

static const void *prepared_pure_projected_source_child(
        const CettaPreparedPureProgram *program,
        Atom *source, const void *source_view, Atom *projected) {
    if (!source || !source_view || !projected ||
        source->kind != ATOM_EXPR)
        return NULL;
    for (CettaExprIndex index = 0u;
         index < source->expr.len; index++) {
        if (source->expr.elems[index] == projected)
            return prepared_pure_source_child(
                program, source_view, index);
    }
    return NULL;
}

typedef enum {
    PREPARED_PURE_GUARDED_EQUATION_ERROR = -1,
    PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE = 0,
    PREPARED_PURE_GUARDED_EQUATION_READY = 1,
} PreparedPureGuardedEquationState;

static bool prepared_pure_expression_is_zero(
        CettaPreparedPureProgram *program, Atom *source,
        const void *source_view) {
    if (!program || !source || !program->expression_view)
        return false;
    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(program, source_view);
    if (source_role != CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
        source_role != CETTA_PREPARED_PURE_SOURCE_CALL)
        return false;
    if (source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
        source->kind == ATOM_EXPR && source->expr.len > 0u &&
        source->expr.elems[0] &&
        source->expr.elems[0]->kind == ATOM_SYMBOL &&
        space_equations_may_match_known_head(
            program->space, source->expr.elems[0]->sym_id))
        return false;
    CettaPreparedPureExpressionView view = {0};
    return program->expression_view(source, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_ZERO;
}

bool cetta_prepared_pure_single_result_requires_answer_effect(
        const Atom *expression,
        CettaPreparedPureExpressionViewFn expression_view) {
    if (!expression || !expression_view ||
        expression->kind != ATOM_EXPR || expression->expr.len != 4u ||
        !expression->expr.elems[0] ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;

    CettaGsltFoldControl control;
    if (!prepared_pure_control_program(
            expression->expr.elems[0]->sym_id, 3u, &control) ||
        control != CETTA_GSLT_FOLD_CONTROL_BRANCH)
        return false;

    CettaPreparedPureExpressionView view = {0};
    if (expression_view(expression->expr.elems[2], &view) ==
            CETTA_PREPARED_PURE_EXPRESSION_ZERO)
        return true;
    memset(&view, 0, sizeof(view));
    return expression_view(expression->expr.elems[3], &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_ZERO;
}

static bool prepared_pure_scalar_guard_flat_lhs(Atom *lhs) {
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u)
        return false;
    for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
        Atom *pattern = lhs->expr.elems[index];
        if (!pattern ||
            (pattern->kind == ATOM_EXPR && pattern->expr.len != 0u))
            return false;
    }
    return true;
}

static bool prepared_pure_plain_scalar_truth_head(SymbolId head) {
    return head == g_builtin_syms.op_lt ||
           head == g_builtin_syms.op_gt ||
           head == g_builtin_syms.op_le ||
           head == g_builtin_syms.op_ge ||
           head == g_builtin_syms.numeric_eq;
}

static bool prepared_pure_compile_scalar_guard(
        CettaPreparedPureProgram *program,
        const PreparedPureCompileContext *context,
        Atom *condition, bool expected_truth,
        uint32_t *guard_out) {
    if (!program || !context || !condition || !guard_out ||
        condition->kind != ATOM_EXPR || condition->expr.len == 0u ||
        condition->expr.len - 1u >
            PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS)
        return false;
    Atom *head = condition->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_pure_plain_scalar_truth_head(head->sym_id) ||
        !grounded_op_is_type_pure(head->sym_id) ||
        program->scalar_guard_len >= UINT32_MAX ||
        program->scalar_guard_argument_len >= UINT32_MAX)
        return false;

    size_t argument_mark = program->scalar_guard_argument_len;
    CettaExprLen argument_count = condition->expr.len - 1u;
    if (!prepared_pure_reserve(
            (void **)&program->scalar_guard_arguments,
            sizeof(*program->scalar_guard_arguments),
            &program->scalar_guard_argument_cap,
            argument_mark + argument_count) ||
        !prepared_pure_reserve(
            (void **)&program->scalar_guards,
            sizeof(*program->scalar_guards),
            &program->scalar_guard_cap,
            program->scalar_guard_len + 1u))
        return false;

    for (CettaExprIndex index = 1u;
         index < condition->expr.len; index++) {
        Atom *argument = condition->expr.elems[index];
        PreparedPureScalarGuardArgument compiled = {0};
        if (argument && argument->kind == ATOM_VAR) {
            uint32_t slot = 0u;
            if (!prepared_pure_context_lookup(
                    context, argument->var_id, &slot)) {
                program->scalar_guard_argument_len = argument_mark;
                return false;
            }
            compiled.slot = slot;
            compiled.from_slot = true;
        } else if (argument && argument->kind == ATOM_GROUNDED &&
                   (argument->ground.gkind == GV_INT ||
                    argument->ground.gkind == GV_FLOAT)) {
            compiled.literal = argument;
        } else {
            program->scalar_guard_argument_len = argument_mark;
            return false;
        }
        program->scalar_guard_arguments[
            program->scalar_guard_argument_len++] = compiled;
    }

    uint32_t guard_index = (uint32_t)program->scalar_guard_len;
    program->scalar_guards[program->scalar_guard_len++] =
        (PreparedPureScalarGuard){
            .head = head,
            .first_argument = (uint32_t)argument_mark,
            .argument_count = argument_count,
            .expected_truth = expected_truth,
        };
    *guard_out = guard_index;
    return true;
}

static PreparedPureGuardedEquationState
prepared_pure_compile_guarded_equation(
        CettaPreparedPureProgram *program,
        PreparedPureCompileContext *context,
        Atom *lhs, Atom *rhs, const void *rhs_view,
        uint32_t depth,
        uint32_t *root_out, uint32_t *guard_out) {
    if (!program || !context || !lhs || !rhs || !root_out || !guard_out)
        return PREPARED_PURE_GUARDED_EQUATION_ERROR;
    CettaPreparedPureSourceRole rhs_role =
        prepared_pure_source_role(program, rhs_view);
    if ((rhs_role != CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
         rhs_role != CETTA_PREPARED_PURE_SOURCE_CALL) ||
        !prepared_pure_scalar_guard_enabled() ||
        !prepared_pure_scalar_guard_flat_lhs(lhs) ||
        rhs->kind != ATOM_EXPR || rhs->expr.len != 4u ||
        !rhs->expr.elems[0] ||
        rhs->expr.elems[0]->kind != ATOM_SYMBOL)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;

    CettaGsltFoldControl control;
    if (!prepared_pure_control_program(
            rhs->expr.elems[0]->sym_id, 3u, &control) ||
        control != CETTA_GSLT_FOLD_CONTROL_BRANCH)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;
    bool then_zero = prepared_pure_expression_is_zero(
        program, rhs->expr.elems[2],
        prepared_pure_source_child(program, rhs_view, 2u));
    bool else_zero = prepared_pure_expression_is_zero(
        program, rhs->expr.elems[3],
        prepared_pure_source_child(program, rhs_view, 3u));
    if (then_zero == else_zero)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;

    size_t guard_mark = program->scalar_guard_len;
    size_t argument_mark = program->scalar_guard_argument_len;
    uint32_t guard = PREPARED_PURE_NO_SCALAR_GUARD;
    bool expected_truth = else_zero;
    if (!prepared_pure_compile_scalar_guard(
            program, context, rhs->expr.elems[1],
            expected_truth, &guard)) {
        program->scalar_guard_len = guard_mark;
        program->scalar_guard_argument_len = argument_mark;
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;
    }
    Atom *result_branch = rhs->expr.elems[then_zero ? 3u : 2u];
    const void *result_view = prepared_pure_source_child(
        program, rhs_view, then_zero ? 3u : 2u);
    if (!prepared_pure_compile_eval(
            program, context, result_branch, result_view,
            depth + 1u, root_out)) {
        program->scalar_guard_len = guard_mark;
        program->scalar_guard_argument_len = argument_mark;
        return PREPARED_PURE_GUARDED_EQUATION_ERROR;
    }
    *guard_out = guard;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_ADMITTED);
    return PREPARED_PURE_GUARDED_EQUATION_READY;
}

static bool prepared_pure_compile_children(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom **source, CettaExprLen count,
    const void *parent_source_view,
    CettaExprIndex first_source_child, uint32_t depth,
    bool evaluate, bool require_inert_templates,
    uint32_t **children_out) {
    if (!children_out ||
        (count > 0u && (size_t)count > SIZE_MAX / sizeof(uint32_t)))
        return false;
    uint32_t *children = count ? malloc(sizeof(*children) * count) : NULL;
    if (count && !children)
        return false;
    for (CettaExprIndex i = 0u; i < count; i++) {
        const void *child_view = prepared_pure_source_child(
            program, parent_source_view, first_source_child + i);
        bool ok = evaluate
            ? prepared_pure_compile_eval(
                  program, context, source[i], child_view,
                  depth + 1u, &children[i])
            : prepared_pure_compile_template(
                  program, context, source[i], child_view, depth + 1u,
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
            state == CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY ||
            state == CETTA_PREPARED_PURE_EXPRESSION_ZERO)
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
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
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
    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(program, source_view);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return prepared_pure_reject(
            program, "source view declines prepared template", source);
    /* normalize-before-delay: a total register or control head inside
     * constructor payload is computation, not quoted data.  The canonical
     * evaluator computes these positions, so the machine must as well. */
    if ((source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED ||
         source_role == CETTA_PREPARED_PURE_SOURCE_CALL) &&
        source->expr.len > 0u) {
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
                    program, context, source, source_view,
                    depth, node_out);
            }
        }
    }
    bool source_head_is_inert =
        source_role == CETTA_PREPARED_PURE_SOURCE_VALUE ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DATA ||
        (source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
         prepared_pure_template_head_is_inert(program, source));
    if (require_inert_head && !source_head_is_inert)
        return prepared_pure_reject(
            program, "callable syntax occurs in a data template", source);
    uint32_t *children = NULL;
    if (!prepared_pure_compile_children(
            program, context, source->expr.elems, source->expr.len,
            source_view, 0u, depth, false, false, &children))
        return false;
    bool ok = prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
        children, source->expr.len, node_out);
    free(children);
    return ok;
}

/* The operands of a register over typed structural operands that are
 * literal symbols typed %Undefined% by the program's declarations. */
static uint8_t prepared_pure_undefined_type_operands(
    const CettaPreparedPureProgram *program,
    CettaGsltRegisterInstruction instruction,
    const uint32_t *children, uint32_t arity) {
    CettaGsltRegisterOperandDiscipline discipline;
    if (!program || !children || arity != 2u ||
        program->total_structural_equality ||
        !cetta_gslt_register_operand_discipline(instruction, &discipline) ||
        discipline !=
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS)
        return 0u;
    uint8_t operands = 0u;
    for (uint32_t i = 0u; i < arity; i++) {
        const PreparedPureNode *child = &program->nodes[children[i]];
        if (child->kind == PREPARED_PURE_LITERAL &&
            eval_symbol_type_undefined(program->space, child->atom))
            operands |= (uint8_t)(1u << i);
    }
    return operands;
}

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, uint32_t *node_out) {
    if (!program || !context || !source || !node_out ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (source->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, source->var_id, &slot))
            return false;
        /* Eager equation and let bindings are populated only after their
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
    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(program, source_view);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return prepared_pure_reject(
            program, "source view declines prepared evaluation", source);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_VALUE) {
        if (source->kind == ATOM_EXPR && atom_has_vars(source))
            return prepared_pure_reject(
                program, "open expression replaced a source value", source);
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    /* Translation-time data classification says the source occurrence is
     * not a call; it does not override the dialect's value representation.
     * A closed partial/lambda/foreign value is already atomic to evaluation
     * and must not be decomposed into constructor work.  Open values continue
     * below so their variables are represented by slots. */
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
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DATA) {
        if (source->kind != ATOM_EXPR)
            return prepared_pure_reject(
                program, "source data is not an expression", source);
        for (CettaExprIndex index = 0u;
             index < source->expr.len; index++) {
            if (!prepared_pure_source_child(
                    program, source_view, index))
                return prepared_pure_reject(
                    program, "source data shape does not match its view",
                    source);
        }
        uint32_t *children = NULL;
        bool evaluate_children =
            program->call_mode == CETTA_GSLT_PURE_CALL_EAGER;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems,
                source->expr.len, source_view, 0u, depth,
                evaluate_children, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
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
                prepared_pure_projected_source_child(
                    program, source, source_view,
                    expression_view.projected),
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
                    prepared_pure_projected_source_child(
                        program, source, source_view,
                        expression_view.projected),
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
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_ZERO) {
            if (!program->answer_producer)
                return prepared_pure_reject(
                    program,
                    "choice zero is not an ordinary prepared value",
                    source);
            return prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_ZERO},
                NULL, 0u, node_out);
        }
        if (expression_view_state !=
            CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return prepared_pure_reject(
                program, "invalid dialect expression view", source);
    }
    if (source->kind != ATOM_EXPR)
        return prepared_pure_compile_template(
            program, context, source, source_view,
            depth, true, node_out);
    if (source->expr.len == 0u)
        return prepared_pure_compile_template(
            program, context, source, source_view,
            depth, true, node_out);
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
                source_view, 0u, depth, true, false, &children))
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
                prepared_pure_source_child(program, source_view, 1u),
                depth + 1u, node_out);
        }
        if (control == CETTA_GSLT_FOLD_CONTROL_BRANCH) {
            if (arity != 3u)
                return false;
            uint32_t *children = NULL;
            if (!prepared_pure_compile_children(
                    program, context, &source->expr.elems[1], 3u,
                    source_view, 1u, depth, true, false, &children))
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
                    prepared_pure_source_child(
                        program, source_view, 2u),
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
                prepared_pure_source_child(
                    program, source_view, 3u),
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
                source_view, 1u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_REGISTER,
                .atom = head_atom,
                .head = head,
                .instruction = instruction,
                .result_kind = result_kind,
                .undefined_type_operands =
                    prepared_pure_undefined_type_operands(
                        program, instruction, children, arity),
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
                source_view, 1u, depth, true, false, &children))
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
                source_view, 1u, depth, eager_arguments,
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
    if (source_role == CETTA_PREPARED_PURE_SOURCE_CALL)
        return prepared_pure_reject(
            program, "translated call has no live implementation", source);
    PreparedPureHeadRole head_role =
        prepared_pure_head_role(program, source);
    if (head_role == PREPARED_PURE_HEAD_CALLABLE)
        return prepared_pure_reject(
            program, "unsupported evaluator syntax", source);
    if (head_role == PREPARED_PURE_HEAD_UNKNOWN)
        return prepared_pure_reject(
            program, "dialect-owned form requires canonical evaluation",
            source);
    if (program->call_mode == CETTA_GSLT_PURE_CALL_EAGER) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems, source->expr.len,
                source_view, 0u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    return prepared_pure_compile_template(
        program, context, source, source_view,
        depth, true, node_out);
}

static bool prepared_pure_bind_pattern_vars(
    PreparedPureCompileContext *context, Atom *pattern, bool *repeated) {
    if (!context || !pattern || !repeated)
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
            uint32_t existing = 0u;
            if (prepared_pure_context_lookup(
                    context, current->var_id, &existing)) {
                *repeated = true;
                continue;
            }
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

/* The single-result machine may compile a multi-equation LHS only when every
 * same-arity pair is separated by information exposed at weak-head normal
 * form.  A variable overlaps everything.  Two expressions are separated here
 * only by outer arity or constructor head; differences below the constructor
 * would require a path-sensitive demand continuation, and overlapping equations
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

static bool prepared_pure_equations_whnf_disjoint(
    const PreparedPureEquation *left, const PreparedPureEquation *right) {
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

/* Fast common-case determinacy proof.  A single argument whose equations
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
    if (!program || !head || head->equation_count < 2u ||
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;
    uint32_t bucket_count = 4u;
    while (bucket_count < head->equation_count * 2u) {
        if (bucket_count > UINT32_MAX / 2u)
            return false;
        bucket_count *= 2u;
    }
    PreparedPureWhnfDiscriminatorKey *keys = calloc(
        head->equation_count, sizeof(*keys));
    uint32_t *buckets = calloc(bucket_count, sizeof(*buckets));
    if (!keys || !buckets) {
        free(keys);
        free(buckets);
        return false;
    }
    uint32_t key_count = 0u;
    bool distinct = true;
    uint32_t mask = bucket_count - 1u;
    for (uint32_t i = 0u; i < head->equation_count && distinct; i++) {
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + i];
        if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
            argument >= equation->arity) {
            distinct = false;
            break;
        }
        Atom *pattern = equation->lhs->expr.elems[argument + 1u];
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
                if (key_count >= head->equation_count) {
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
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;
    if (head->equation_count > 1u) {
        uint32_t arity = program->equations[head->first_equation].arity;
        bool same_arity = arity <= 64u;
        for (uint32_t i = 1u; i < head->equation_count; i++) {
            if (program->equations[head->first_equation + i].arity != arity) {
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
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        const PreparedPureEquation *left =
            &program->equations[head->first_equation + i];
        for (uint32_t j = i + 1u; j < head->equation_count; j++) {
            const PreparedPureEquation *right =
                &program->equations[head->first_equation + j];
            if (!prepared_pure_equations_whnf_disjoint(left, right))
                return false;
        }
    }
    return true;
}

static bool prepared_pure_compile_decision_group(
    CettaPreparedPureProgram *program, PreparedPureHead *head,
    uint32_t arity) {
    if (!program || !head || arity > 64u ||
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;

    uint32_t equation_count = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        if (program->equations[head->first_equation + i].arity == arity)
            equation_count++;
    }
    if (equation_count < PREPARED_PURE_DECISION_MIN_EQUATIONS || arity == 0u)
        return true;
    if (program->decision_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->decisions,
            sizeof(*program->decisions), &program->decision_cap,
            program->decision_len + 1u))
        return false;

    CettaMatchDecisionEquation *equations =
        malloc(sizeof(*equations) * equation_count);
    if (!equations)
        return false;
    uint64_t universally_constrained =
        arity == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << arity) - 1u;
    uint32_t write = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t equation_index = head->first_equation + i;
        PreparedPureEquation *equation = &program->equations[equation_index];
        if (equation->arity != arity)
            continue;
        if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
            equation->lhs->expr.len != arity + 1u) {
            free(equations);
            return false;
        }
        equations[write++] = (CettaMatchDecisionEquation){
            .pattern = equation->lhs,
            .source_ref = equation_index,
        };
        for (uint32_t argument = 0u; argument < arity; argument++) {
            Atom *pattern = equation->lhs->expr.elems[argument + 1u];
            if (!pattern || pattern->kind == ATOM_VAR) {
                universally_constrained &=
                    ~(UINT64_C(1) << argument);
            }
        }
    }
    if (write != equation_count) {
        free(equations);
        return false;
    }

    CettaMatchDecision *selector =
        cetta_match_decision_compile_equation_projection(
        program->equation_projection,
        program->match_decision_semantics,
        equations, equation_count, CETTA_MATCH_DECISION_DEEP,
        0u, cetta_match_decision_realization_from_process(),
        NULL, NULL);
    free(equations);
    if (!selector)
        return false;

    uint32_t decision_index = (uint32_t)program->decision_len;
    program->decisions[program->decision_len++] =
        (PreparedPureDecisionProgram){
            .arity = arity,
            .equation_count = equation_count,
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
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t arity =
            program->equations[head->first_equation + i].arity;
        bool seen = false;
        for (uint32_t j = 0u; j < i; j++) {
            if (program->equations[head->first_equation + j].arity == arity) {
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

/* Mark only the result-preserving spine of an equation body.  A call reached
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

static bool prepared_pure_equation_var_slot(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, VarId var,
    uint32_t *slot_out);

/* Emit the match of one pattern node read from register `source`.  Clears
 * *compilable, emitting nothing further, when a node needs the general
 * matcher: a dialect view form, or a bound exceeded. */
static bool prepared_pure_emit_match_ops(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation,
    Atom *pattern, uint32_t source, uint32_t *next_register,
    bool *compilable) {
    if (!*compilable)
        return true;
    if (!pattern) {
        *compilable = false;
        return true;
    }
    PreparedPureMatchOp op = {.source = source};
    if (pattern->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_equation_var_slot(
                program, equation, pattern->var_id, &slot) ||
            slot >= equation->local_count) {
            *compilable = false;
            return true;
        }
        op.kind = PREPARED_PURE_MATCH_OP_BIND;
        op.operand = slot;
        const PreparedPureMatchOp *emitted =
            &program->match_ops[equation->first_match_op];
        for (uint32_t i = 0u; i < equation->match_op_count; i++) {
            if (emitted[i].kind == PREPARED_PURE_MATCH_OP_BIND &&
                emitted[i].operand == slot) {
                op.kind = PREPARED_PURE_MATCH_OP_SAME;
                break;
            }
        }
    } else {
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            if (program->pattern_view(pattern, pattern, &view) !=
                    CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE) {
                *compilable = false;
                return true;
            }
        }
        if (pattern->kind == ATOM_EXPR) {
            if (pattern->expr.len >
                    PREPARED_PURE_MATCH_MAX_REGISTERS - *next_register) {
                *compilable = false;
                return true;
            }
            op.kind = PREPARED_PURE_MATCH_OP_EXPR;
            op.operand = *next_register;
            op.length = pattern->expr.len;
            *next_register += pattern->expr.len;
        } else {
            op.kind = PREPARED_PURE_MATCH_OP_ATOM;
            op.literal = pattern;
        }
    }
    if (equation->match_op_count >= PREPARED_PURE_MATCH_MAX_OPS) {
        *compilable = false;
        return true;
    }
    if (!prepared_pure_reserve(
            (void **)&program->match_ops, sizeof(*program->match_ops),
            &program->match_op_cap, program->match_op_len + 1u))
        return false;
    program->match_ops[program->match_op_len++] = op;
    equation->match_op_count++;
    if (op.kind != PREPARED_PURE_MATCH_OP_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        if (!prepared_pure_emit_match_ops(
                program, equation, pattern->expr.elems[i],
                op.operand + i, next_register, compilable))
            return false;
    }
    return true;
}

/* Compile the patterns of an equation that has some constructor pattern.
 * A variable's first occurrence binds its slot and each later one compares
 * with it; ops run in emission order, so every bind precedes its checks. */
static bool prepared_pure_record_match_program(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation) {
    equation->compiled_match = false;
    equation->first_match_op = (uint32_t)program->match_op_len;
    equation->match_op_count = 0u;
    if (equation->arity > PREPARED_PURE_MATCH_MAX_REGISTERS ||
        program->match_op_len > UINT32_MAX - PREPARED_PURE_MATCH_MAX_OPS)
        return true;
    uint32_t next_register = equation->arity;
    bool compilable = true;
    for (uint32_t i = 0u; compilable && i < equation->arity; i++) {
        if (!prepared_pure_emit_match_ops(
                program, equation, equation->lhs->expr.elems[i + 1u], i,
                &next_register, &compilable))
            return false;
    }
    if (!compilable) {
        program->match_op_len = equation->first_match_op;
        equation->match_op_count = 0u;
        return true;
    }
    equation->match_register_count = next_register;
    equation->first_bind_op = UINT32_MAX;
    for (uint32_t i = 0u; i < equation->match_op_count; i++) {
        if (program->match_ops[equation->first_match_op + i].kind ==
                PREPARED_PURE_MATCH_OP_BIND) {
            equation->first_bind_op = i;
            break;
        }
    }
    equation->compiled_match = true;
    return true;
}

/* Record the argument-to-slot map of an equation whose argument patterns are
 * all variables; matching it binds each argument to its slot, or compares it
 * with a repeated variable's first value.  Other equations compile a match
 * program when their patterns allow it, and keep the general matcher
 * otherwise. */
static bool prepared_pure_record_argument_slots(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation) {
    equation->variable_arguments = false;
    for (uint32_t i = 0u; i < equation->arity; i++) {
        Atom *pattern = equation->lhs->expr.elems[i + 1u];
        if (!pattern || pattern->kind != ATOM_VAR)
            return prepared_pure_record_match_program(program, equation);
    }
    if (program->argument_slot_len > UINT32_MAX - equation->arity ||
        !prepared_pure_reserve(
            (void **)&program->argument_slots,
            sizeof(*program->argument_slots), &program->argument_slot_cap,
            program->argument_slot_len + equation->arity))
        return false;
    size_t first = program->argument_slot_len;
    for (uint32_t i = 0u; i < equation->arity; i++) {
        uint32_t slot = 0u;
        if (!prepared_pure_equation_var_slot(
                program, equation,
                equation->lhs->expr.elems[i + 1u]->var_id, &slot) ||
            slot >= equation->local_count)
            return true;
        program->argument_slots[first + i] = slot;
    }
    program->argument_slot_len += equation->arity;
    equation->first_argument_slot = (uint32_t)first;
    equation->variable_arguments = true;
    return true;
}

static bool prepared_pure_compile_head(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len)
        return false;
    PreparedPureHead *head = &program->heads[head_index];
    if (head->compiled)
        return true;
    head->declares_type =
        space_head_declares_type(program->space, head->head);
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(program->space, head->head, &cursor))
        return false;
    head->first_equation = (uint32_t)program->equation_len;
    size_t occurrence_ordinal = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            program->equation_len >= PREPARED_PURE_MAX_EQUATIONS ||
            program->equation_len >= UINT32_MAX)
            return false;
        SpaceEquationOccurrence occurrence = {0};
        if (!space_equation_occurrence_resolve(id, &occurrence) ||
            !occurrence.lhs || occurrence.lhs->kind != ATOM_EXPR ||
            occurrence.lhs->expr.len == 0u ||
            !atom_is_symbol_id(
                occurrence.lhs->expr.elems[0], head->head))
            return prepared_pure_reject(
                program, "wildcard or malformed equation", occurrence.lhs);

        const void *rhs_view = NULL;
        if (program->source_view.equation_rhs &&
            !program->source_view.equation_rhs(
                program->source_view.context, program->space,
                head->head, occurrence_ordinal, occurrence.id,
                occurrence.equation, &rhs_view))
            return prepared_pure_reject(
                program, "equation has no exact source view",
                occurrence.rhs);
        occurrence_ordinal++;

        PreparedPureCompileContext context = {0};
        bool pattern_ok = true;
        bool repeated_variables = false;
        for (CettaExprIndex i = 1u;
             i < occurrence.lhs->expr.len; i++) {
            if (!prepared_pure_bind_pattern_vars(
                    &context, occurrence.lhs->expr.elems[i],
                    &repeated_variables)) {
                pattern_ok = false;
                break;
            }
        }
        /* Occurrences of a repeated variable compare values.  An eager
         * closed program forces every argument before it matches, so its
         * arguments are values; elsewhere one may still be unevaluated. */
        if (repeated_variables &&
            (!program->closed_program ||
             program->call_mode != CETTA_GSLT_PURE_CALL_EAGER))
            pattern_ok = false;
        uint32_t first_pattern_var = 0u;
        uint32_t pattern_var_count = 0u;
        if (!pattern_ok || !prepared_pure_append_pattern_vars(
                program, &context,
                &first_pattern_var, &pattern_var_count)) {
            free(context.bindings);
            return prepared_pure_reject(
                program,
                "oversized equation pattern, or a repeated variable "
                "matched before its arguments are values",
                occurrence.lhs);
        }
        uint32_t root = 0u;
        uint32_t scalar_guard = PREPARED_PURE_NO_SCALAR_GUARD;
        PreparedPureGuardedEquationState guarded =
            prepared_pure_compile_guarded_equation(
                program, &context, occurrence.lhs, occurrence.rhs,
                rhs_view,
                0u, &root, &scalar_guard);
        bool rhs_ok = guarded == PREPARED_PURE_GUARDED_EQUATION_READY;
        if (guarded == PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE) {
            rhs_ok = prepared_pure_compile_eval(
                program, &context, occurrence.rhs, rhs_view,
                0u, &root);
        }
        if (rhs_ok)
            rhs_ok = prepared_pure_mark_tail_spine(
                program, root, 0u);
        if (!rhs_ok || context.next_slot > PREPARED_PURE_MAX_SLOTS ||
            !prepared_pure_reserve(
                (void **)&program->equations,
                sizeof(*program->equations), &program->equation_cap,
                program->equation_len + 1u)) {
            free(context.bindings);
            return prepared_pure_reject(
                program, "equation body is outside the pure machine fragment",
                occurrence.rhs);
        }
        program->equations[program->equation_len++] = (PreparedPureEquation){
            .lhs = occurrence.lhs,
            .equation = occurrence.equation,
            .logical_index = occurrence.id.logical_index,
            .arity = occurrence.lhs->expr.len - 1u,
            .root = root,
            .local_count = context.next_slot,
            .first_pattern_var = first_pattern_var,
            .pattern_var_count = pattern_var_count,
            .scalar_guard = scalar_guard,
            .repeated_variables = repeated_variables,
        };
        free(context.bindings);
        if (!prepared_pure_record_argument_slots(
                program, &program->equations[program->equation_len - 1u]))
            return prepared_pure_reject(
                program, "cannot record equation argument slots",
                occurrence.lhs);
        head = &program->heads[head_index];
        head->equation_count++;
    }
    head = &program->heads[head_index];
    if (head->equation_count == 0u ||
        !space_program_token_is_current(program->source_program))
        return prepared_pure_reject(
            program, "empty or invalidated user head", NULL);
    bool has_scalar_guard = false;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        if (program->equations[head->first_equation + i].scalar_guard !=
                PREPARED_PURE_NO_SCALAR_GUARD) {
            has_scalar_guard = true;
            break;
        }
    }
    if (!program->answer_producer &&
        !prepared_pure_head_is_whnf_determinate(program, head) &&
        !has_scalar_guard)
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
    return space_program_token_is_current(program->source_program);
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

static bool PREPARED_PURE_HOT prepared_pure_push_value(
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

static bool prepared_pure_equation_var_slot(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, VarId var,
    uint32_t *slot_out) {
    if (!program || !equation || !slot_out)
        return false;
    uint32_t lower = 0u;
    uint32_t upper = equation->pattern_var_count;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const PreparedPureVarSlot *entry =
            &program->pattern_vars[
                equation->first_pattern_var + middle];
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

/* Why an equation head refused its arguments, as equation search counts a
 * failed unification attempt. */
typedef enum {
    PREPARED_PURE_MISMATCH_HEAD = 0,
    PREPARED_PURE_MISMATCH_AFTER_BIND,
    PREPARED_PURE_MISMATCH_REPEATED_VARIABLE,
} PreparedPureMismatch;

/* A later occurrence of a pattern variable against the value its first
 * occurrence bound.  The canonical matcher decides a pair of ground values by
 * structural equality; a value with variables would unify instead, which
 * this matcher leaves to it. */
static PreparedPureMatchState prepared_pure_match_same_value(
    Atom *bound, Atom *value) {
    if (!bound || !value || atom_has_vars(bound) || atom_has_vars(value))
        return PREPARED_PURE_MATCH_ERROR;
    return atom_eq(bound, value) ||
            cetta_he_promoted_numbers_equal(bound, value)
        ? PREPARED_PURE_MATCH_MATCHED : PREPARED_PURE_MATCH_MISMATCH;
}

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

/* A finished equation-head match counts one unification attempt, classified
 * as equation search classifies one: success, a repeated variable's values
 * differing, or a mismatch before or after a variable was bound.  A decline
 * or a demanded argument is not an attempt; its match runs again. */
static inline PreparedPureMatchState prepared_pure_count_match(
    PreparedPureMatchState state, PreparedPureMismatch mismatch) {
    if (state == PREPARED_PURE_MATCH_MATCHED) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS);
    } else if (state == PREPARED_PURE_MATCH_MISMATCH) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
        cetta_runtime_stats_inc(
            mismatch == PREPARED_PURE_MISMATCH_REPEATED_VARIABLE
                ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL
                : mismatch == PREPARED_PURE_MISMATCH_AFTER_BIND
                    ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_AFTER_BIND_FAIL
                    : CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL);
    }
    return state;
}

static PreparedPureMatchState prepared_pure_match_equation(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments, uint32_t *demanded_argument) {
    if (!program || !equation || (!arguments && arity > 0u) ||
        arity != equation->arity ||
        !prepared_pure_reserve(
            (void **)&program->match_values,
            sizeof(*program->match_values), &program->match_cap,
            equation->local_count))
        return PREPARED_PURE_MATCH_ERROR;
    if (equation->local_count > 0u)
        memset(program->match_values, 0,
               sizeof(*program->match_values) * equation->local_count);
    if (equation->variable_arguments && !equation->repeated_variables) {
        const uint32_t *slots =
            &program->argument_slots[equation->first_argument_slot];
        for (uint32_t i = 0u; i < arity; i++)
            program->match_values[slots[i]] = arguments[i];
        return prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    }
    uint64_t all_arguments = arity >= 64u
        ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
    if (equation->variable_arguments) {
        const uint32_t *slots =
            &program->argument_slots[equation->first_argument_slot];
        /* A repeated variable compares values; see compilation. */
        if ((ready_arguments & all_arguments) != all_arguments)
            return PREPARED_PURE_MATCH_ERROR;
        for (uint32_t i = 0u; i < arity; i++) {
            Atom **slot = &program->match_values[slots[i]];
            if (*slot) {
                PreparedPureMatchState same =
                    prepared_pure_match_same_value(*slot, arguments[i]);
                if (same != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_count_match(
                        same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
                continue;
            }
            *slot = arguments[i];
        }
        return prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    }
    if (equation->compiled_match &&
        (ready_arguments & all_arguments) == all_arguments) {
        /* Every argument is a value, so a failed check is a mismatch. */
        Atom *registers[PREPARED_PURE_MATCH_MAX_REGISTERS];
        for (uint32_t i = 0u; i < arity; i++)
            registers[i] = arguments[i];
        const PreparedPureMatchOp *first =
            &program->match_ops[equation->first_match_op];
        const PreparedPureMatchOp *end = first + equation->match_op_count;
        for (const PreparedPureMatchOp *op = first; op < end; op++) {
            Atom *value = registers[op->source];
            bool equal = true;
            switch ((PreparedPureMatchOpKind)op->kind) {
            case PREPARED_PURE_MATCH_OP_BIND:
                program->match_values[op->operand] = value;
                break;
            case PREPARED_PURE_MATCH_OP_SAME: {
                PreparedPureMatchState same =
                    prepared_pure_match_same_value(
                        program->match_values[op->operand], value);
                if (same != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_count_match(
                        same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
                break;
            }
            case PREPARED_PURE_MATCH_OP_ATOM:
                equal = value && op->literal &&
                    ((value->kind == op->literal->kind &&
                      atom_eq(op->literal, value)) ||
                     cetta_he_promoted_numbers_equal(op->literal, value));
                break;
            case PREPARED_PURE_MATCH_OP_EXPR:
                equal = value->kind == ATOM_EXPR &&
                        value->expr.len == op->length;
                /* An empty expression has no element array. */
                if (equal && op->length > 0u)
                    memcpy(&registers[op->operand], value->expr.elems,
                           sizeof(*registers) * op->length);
                break;
            }
            if (!equal)
                return prepared_pure_count_match(
                    PREPARED_PURE_MATCH_MISMATCH,
                    (uint32_t)(op - first) > equation->first_bind_op
                        ? PREPARED_PURE_MISMATCH_AFTER_BIND
                        : PREPARED_PURE_MISMATCH_HEAD);
        }
        return prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    }
    /* A repeated variable compares values; see compilation. */
    if (equation->repeated_variables &&
        (ready_arguments & all_arguments) != all_arguments)
        return PREPARED_PURE_MATCH_ERROR;
    /* The general matcher follows its pairs in stack order; a mismatch
     * after a bind in that order fails after a variable was bound. */
    PreparedPureMismatch mismatch = PREPARED_PURE_MISMATCH_HEAD;
    program->pattern_pair_len = 0u;
    for (uint32_t i = 0u; i < arity; i++) {
        if (!prepared_pure_push_pattern_pair(
                program, equation->lhs->expr.elems[i + 1u],
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
            if (!prepared_pure_equation_var_slot(
                    program, equation, pattern->var_id, &slot) ||
                slot >= equation->local_count)
                return PREPARED_PURE_MATCH_ERROR;
            if (equation->repeated_variables &&
                program->match_values[slot]) {
                PreparedPureMatchState same =
                    prepared_pure_match_same_value(
                        program->match_values[slot], value);
                if (same != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_count_match(
                        same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
                continue;
            }
            program->match_values[slot] = value;
            mismatch = PREPARED_PURE_MISMATCH_AFTER_BIND;
            continue;
        }
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            CettaPreparedPurePatternViewState view_state =
                program->pattern_view(pattern, value, &view);
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH) {
                return prepared_pure_count_match(
                    prepared_pure_match_mismatch(
                        program, value, pair.argument, ready_arguments,
                        demanded_argument),
                    mismatch);
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
        if (pattern->kind != value->kind ||
            (pattern->kind != ATOM_EXPR && !atom_eq(pattern, value)) ||
            (pattern->kind == ATOM_EXPR &&
             pattern->expr.len != value->expr.len))
            return prepared_pure_count_match(
                prepared_pure_match_mismatch(
                    program, value, pair.argument, ready_arguments,
                    demanded_argument),
                mismatch);
        if (pattern->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
            if (!prepared_pure_push_pattern_pair(
                    program, pattern->expr.elems[i],
                    value->expr.elems[i], pair.argument))
                return PREPARED_PURE_MATCH_ERROR;
        }
    }
    return prepared_pure_count_match(
        PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
}

typedef enum {
    PREPARED_PURE_GUARD_DECLINED = -1,
    PREPARED_PURE_GUARD_REFUTED = 0,
    PREPARED_PURE_GUARD_ACCEPTED = 1,
} PreparedPureGuardState;

static PreparedPureGuardState prepared_pure_evaluate_scalar_guard(
        CettaPreparedPureProgram *program,
        const PreparedPureEquation *equation) {
    if (!program || !equation)
        return PREPARED_PURE_GUARD_DECLINED;
    if (equation->scalar_guard == PREPARED_PURE_NO_SCALAR_GUARD)
        return PREPARED_PURE_GUARD_ACCEPTED;
    if (equation->scalar_guard >= program->scalar_guard_len) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }
    const PreparedPureScalarGuard *guard =
        &program->scalar_guards[equation->scalar_guard];
    if (!guard->head ||
        guard->argument_count >
            PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS ||
        guard->first_argument > program->scalar_guard_argument_len ||
        guard->argument_count >
            program->scalar_guard_argument_len - guard->first_argument) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }

    Atom *arguments[PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS];
    for (uint32_t index = 0u; index < guard->argument_count; index++) {
        const PreparedPureScalarGuardArgument *argument =
            &program->scalar_guard_arguments[
                guard->first_argument + index];
        Atom *value = argument->literal;
        if (argument->from_slot) {
            if (argument->slot >= equation->local_count ||
                argument->slot >= program->match_cap) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
                return PREPARED_PURE_GUARD_DECLINED;
            }
            value = program->match_values[argument->slot];
        }
        if (!value || atom_has_vars(value) ||
            petta_semantics_value_contains_observable_open_cons(value)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
            return PREPARED_PURE_GUARD_DECLINED;
        }
        arguments[index] = value;
    }

    bool truth = false;
    if (!grounded_try_plain_scalar_truth(
            guard->head, arguments, guard->argument_count, &truth)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_EVALUATED);
    if (truth != guard->expected_truth) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_REFUTED);
        return PREPARED_PURE_GUARD_REFUTED;
    }
    return PREPARED_PURE_GUARD_ACCEPTED;
}

/* One non-variable pattern atom against a value, as the binder matcher
 * below decides it: the dialect view first, then structural equality.
 * False when the view decomposes, which only the general matcher follows. */
static bool prepared_pure_match_rigid_atom(
    CettaPreparedPureProgram *program, Atom *pattern, Atom *value,
    bool *equal_out) {
    if (program->pattern_view) {
        CettaPreparedPurePatternView view = {0};
        CettaPreparedPurePatternViewState view_state =
            program->pattern_view(pattern, value, &view);
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH) {
            *equal_out = false;
            return true;
        }
        if (view_state != CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
            return false;
    }
    *equal_out = pattern->kind == value->kind && atom_eq(pattern, value);
    return true;
}

/* The flat-field case of the binder matcher below, in its order: the
 * dialect view of the whole pattern first, then the head and the fields.
 * *handled_out is false for a view the general matcher must follow. */
static PreparedPureMatchState prepared_pure_match_flat_fields(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value, bool *handled_out) {
    *handled_out = true;
    Atom *pattern = descriptor->pattern;
    CettaExprLen fields = pattern->expr.len - 1u;
    Atom *const *values = NULL;
    if (program->pattern_view) {
        CettaPreparedPurePatternView view = {0};
        CettaPreparedPurePatternViewState view_state =
            program->pattern_view(pattern, value, &view);
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH)
            return PREPARED_PURE_MATCH_MISMATCH;
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
            if (view.child_count != fields ||
                view.pattern_children != pattern->expr.elems + 1u ||
                !view.value_children) {
                *handled_out = false;
                return PREPARED_PURE_MATCH_ERROR;
            }
            values = view.value_children;
        } else if (view_state !=
                   CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE) {
            return PREPARED_PURE_MATCH_ERROR;
        }
    }
    if (!values) {
        if (value->kind != ATOM_EXPR ||
            value->expr.len != pattern->expr.len)
            return PREPARED_PURE_MATCH_MISMATCH;
        bool equal = false;
        if (!value->expr.elems[0] ||
            !prepared_pure_match_rigid_atom(
                program, pattern->expr.elems[0], value->expr.elems[0],
                &equal)) {
            *handled_out = false;
            return PREPARED_PURE_MATCH_ERROR;
        }
        if (!equal)
            return PREPARED_PURE_MATCH_MISMATCH;
        values = value->expr.elems + 1u;
    }
    for (CettaExprLen i = 0u; i < fields; i++) {
        size_t slot = (size_t)local_base +
            program->bind_vars[descriptor->first_var + i].slot;
        if (slot >= program->slot_len || !values[i])
            return PREPARED_PURE_MATCH_ERROR;
        program->slots[slot] = values[i];
    }
    return PREPARED_PURE_MATCH_MATCHED;
}

static PreparedPureMatchState prepared_pure_match_bind_pattern(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value) {
    if (program && descriptor && value && descriptor->flat_fresh_fields) {
        bool handled = false;
        PreparedPureMatchState flat = prepared_pure_match_flat_fields(
            program, descriptor, local_base, value, &handled);
        if (handled)
            return flat;
    }
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
 * Direct demand is permitted only when every equation constrains the same
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
        decision->equation_count < PREPARED_PURE_DECISION_MIN_EQUATIONS)
        return PREPARED_PURE_DECISION_NOT_APPLICABLE;
    if (!decision->selector)
        return PREPARED_PURE_DECISION_ERROR;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_RUN);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_EQUATION_INPUT,
        decision->equation_count);

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

    if (head->first_equation >= program->equation_len)
        return PREPARED_PURE_DECISION_ERROR;
    Atom *lhs = program->equations[head->first_equation].lhs;
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
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_EQUATION_SURVIVOR,
        *candidate_count_out);
    /* An equation the decision refutes counts as the head failure equation
     * search would meet, as a shape-pruned candidate does there. */
    if (*candidate_count_out < decision->equation_count) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT,
            decision->equation_count - *candidate_count_out);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL,
            decision->equation_count - *candidate_count_out);
    }
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
    const PreparedPureEquation *equation;
    uint32_t demanded_argument;
} PreparedPureSelection;

static PreparedPureSelectState PREPARED_PURE_HOT
prepared_pure_consider_equation(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments,
    const PreparedPureEquation **selected,
    bool *needs_argument,
    uint32_t *first_demanded_argument) {
    if (!program || !equation || !arguments || !selected ||
        !needs_argument || !first_demanded_argument)
        return PREPARED_PURE_SELECT_ERROR;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
    uint32_t demanded_argument = 0u;
    PreparedPureMatchState matched = prepared_pure_match_equation(
        program, equation, arguments, arity,
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
    PreparedPureGuardState guard =
        prepared_pure_evaluate_scalar_guard(program, equation);
    if (guard == PREPARED_PURE_GUARD_DECLINED)
        return PREPARED_PURE_SELECT_ERROR;
    if (guard == PREPARED_PURE_GUARD_REFUTED)
        return PREPARED_PURE_SELECT_NO_MATCH;
    if (*selected)
        return PREPARED_PURE_SELECT_AMBIGUOUS;
    if (!prepared_pure_reserve(
            (void **)&program->selected_values,
            sizeof(*program->selected_values),
            &program->selected_cap, equation->local_count))
        return PREPARED_PURE_SELECT_ERROR;
    if (equation->local_count > 0u)
        memcpy(program->selected_values, program->match_values,
               sizeof(*program->selected_values) * equation->local_count);
    *selected = equation;
    return PREPARED_PURE_SELECT_NO_MATCH;
}

static PreparedPureSelection prepared_pure_select_equation(
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
    const PreparedPureEquation *selected = NULL;
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
            uint32_t equation_index = decision_candidate_refs[i];
            if (equation_index >= program->equation_len)
                return (PreparedPureSelection){
                    .state = PREPARED_PURE_SELECT_ERROR,
                };
            PreparedPureSelectState state =
                prepared_pure_consider_equation(
                    program, &program->equations[equation_index],
                    arguments, arity, ready_arguments,
                    &selected, &needs_argument,
                    &first_demanded_argument);
            if (state == PREPARED_PURE_SELECT_ERROR ||
                state == PREPARED_PURE_SELECT_AMBIGUOUS)
                return (PreparedPureSelection){.state = state};
        }
    } else {
        for (uint32_t i = 0u; i < head->equation_count; i++) {
            const PreparedPureEquation *equation =
                &program->equations[head->first_equation + i];
            if (equation->arity != arity)
                continue;
            PreparedPureSelectState state =
                prepared_pure_consider_equation(
                    program, equation, arguments, arity,
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
        result.equation = selected;
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

/* PeTTa `if` takes the else branch for any other non-empty, non-error value.
 * A variable still belongs to the general machine, which tries both branches. */
static bool prepared_pure_petta_else_value(Atom *condition) {
    return eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        condition &&
        condition->kind != ATOM_VAR &&
        !atom_is_error(condition) &&
        !atom_is_empty(condition);
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
    case GV_BINDINGS:
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
    uint8_t undefined_type_operands, Atom *left, Atom *right) {
    if (!program || !arena || !left || !right)
        return false;
    if (program->total_structural_equality || atom_eq(left, right))
        return true;
    if (undefined_type_operands != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_UNDEFINED_TYPE_OPERAND);
        return true;
    }
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
    uint8_t undefined_type_operands,
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
            program, arena, undefined_type_operands,
            arguments[0], arguments[1]))
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
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH: {
        Atom *result = grounded_dispatch(
            arena, head, (Atom **)arguments, arity);
        return result && !atom_is_empty_or_error(result) ? result : NULL;
    }
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
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS: {
        if (arity != 2u || !arguments[0] || !arguments[1] ||
            arguments[1]->kind != ATOM_EXPR ||
            arguments[1]->expr.len == UINT64_MAX)
            return NULL;
        CettaExprLen length = arguments[1]->expr.len + 1u;
        if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
            return NULL;
        Atom **elements = arena_alloc(
            arena, sizeof(*elements) * (size_t)length);
        elements[0] = arguments[0];
        if (arguments[1]->expr.len > 0u) {
            memcpy(
                elements + 1u, arguments[1]->expr.elems,
                sizeof(*elements) * (size_t)arguments[1]->expr.len);
        }
        return atom_expr(arena, elements, length);
    }
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS: {
        if (arity != 2u || !arguments[0] || !arguments[1] ||
            arguments[0]->kind != ATOM_EXPR ||
            arguments[1]->kind != ATOM_EXPR ||
            arguments[0]->expr.len >
                UINT64_MAX - arguments[1]->expr.len)
            return NULL;
        CettaExprLen length =
            arguments[0]->expr.len + arguments[1]->expr.len;
        if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
            return NULL;
        Atom **elements = length
            ? arena_alloc(arena, sizeof(*elements) * (size_t)length)
            : NULL;
        if (arguments[0]->expr.len > 0u) {
            memcpy(
                elements, arguments[0]->expr.elems,
                sizeof(*elements) * (size_t)arguments[0]->expr.len);
        }
        if (arguments[1]->expr.len > 0u) {
            memcpy(
                elements + arguments[0]->expr.len,
                arguments[1]->expr.elems,
                sizeof(*elements) * (size_t)arguments[1]->expr.len);
        }
        return atom_expr(arena, elements, length);
    }
    }
    return NULL;
}

/* A head can have two authored realizations: a representation-specific
 * register arm and a general pure intrinsic. Compose them at the ready-value
 * boundary, without replaying operand computations. The register result kind
 * describes only that arm; the intrinsic retains its own value contract. */
static Atom *prepared_pure_execute_register_intrinsic(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltRegisterResultKind expected_kind,
    Atom *head, Atom *const *arguments, uint32_t arity) {
    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_pure_intrinsic_program(
            head->sym_id, arity, &intrinsic_instruction) ||
        intrinsic_instruction !=
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH)
        return NULL;
    Atom *result = prepared_pure_execute_intrinsic(
        program, arena, intrinsic_instruction, head, arguments, arity);
    if (result && expected_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        /* Grounded comparison truth is shared; its public representation is
         * owned by the calling dialect, just as for the exact register arm. */
        if (!program->boolean_value)
            return NULL;
        if (prepared_pure_is_true(result))
            return program->boolean_value(arena, true);
        if (prepared_pure_is_false(result))
            return program->boolean_value(arena, false);
        return NULL;
    }
    return result;
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
                  program, arena, instruction, result_kind, 0u,
                  &program->dynamic_values[frame->value_base], arity)
            : prepared_pure_execute_intrinsic(
                  program, arena, intrinsic_instruction, head,
                  &program->dynamic_values[frame->value_base], arity);
        if (!result && is_register)
            result = prepared_pure_execute_register_intrinsic(
                program, arena, result_kind, head,
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

enum {
    /* Bounds on the operand tree one machine step evaluates in place, so a
     * step stays a bounded unit of work between interrupt polls. */
    PREPARED_PURE_INLINE_DEPTH = 4u,
    PREPARED_PURE_INLINE_NODES = 32u,
    PREPARED_PURE_INLINE_OPERANDS = 8u,
};

/* The value of a node that needs no evaluation step: a literal, a
 * positional slot or a ready entry argument.  NULL leaves the node to its
 * frame, which reports a missing value itself. */
static inline Atom *prepared_pure_immediate_value(
    const CettaPreparedPureProgram *program, const PreparedPureNode *node,
    uint32_t local_base) {
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom;
    case PREPARED_PURE_SLOT: {
        size_t slot = (size_t)local_base + node->auxiliary;
        return slot < program->slot_len ? program->slots[slot] : NULL;
    }
    case PREPARED_PURE_ENTRY_ARGUMENT:
        return node->auxiliary < program->entry_argument_count
            ? program->entry_arguments[node->auxiliary] : NULL;
    default:
        return NULL;
    }
}

/* A register operand held unboxed where its register arm allows: `atom` is
 * its value as an atom when one exists (every immediate operand); a small
 * exact integer or a truth computed here may have none. */
typedef struct {
    Atom *atom;
    int64_t integer;
    bool is_integer;
    bool is_boolean;
    bool boolean;
} PreparedPureScalar;

static Atom *prepared_pure_inline_value(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget);

/* Whether prepared_pure_operands_share_type holds of two unboxed operands,
 * decided without the type service: the same structural, literal and
 * intrinsic facts, an unboxed integer standing for a Number.  False leaves
 * the node to its frame, which asks the type service. */
static bool prepared_pure_scalar_operands_share_type(
    const CettaPreparedPureProgram *program, const PreparedPureNode *node,
    const PreparedPureScalar *left, const PreparedPureScalar *right) {
    if (program->total_structural_equality)
        return true;
    CettaGsltRegisterOperandDiscipline discipline;
    if (!cetta_gslt_register_operand_discipline(
            node->instruction, &discipline))
        return false;
    if (discipline !=
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS ||
        (left->atom && right->atom && atom_eq(left->atom, right->atom)))
        return true;
    if (node->undefined_type_operands != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_UNDEFINED_TYPE_OPERAND);
        return true;
    }
    bool left_number = left->is_integer ||
        (left->atom && left->atom->kind == ATOM_GROUNDED &&
         prepared_pure_numeric_ground_kind(left->atom->ground.gkind));
    bool right_number = right->is_integer ||
        (right->atom && right->atom->kind == ATOM_GROUNDED &&
         prepared_pure_numeric_ground_kind(right->atom->ground.gkind));
    if ((left_number && right_number) ||
        (left->atom && right->atom &&
         prepared_pure_grounded_intrinsic_type_equal(
             left->atom, right->atom))) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_INTRINSIC_TYPE_HIT);
        return true;
    }
    return false;
}

static bool prepared_pure_inline_register_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out);

/* A node's value held unboxed where no step is needed: an immediate or
 * inline value, or a register arm decided on unboxed operands.  False
 * leaves the node to its frame. */
static bool prepared_pure_inline_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out) {
    *out = (PreparedPureScalar){0};
    Atom *atom = prepared_pure_immediate_value(program, node, local_base);
    if (!atom && node->kind != PREPARED_PURE_REGISTER) {
        atom = prepared_pure_inline_value(
            program, arena, node, local_base, depth, budget);
    }
    if (atom) {
        out->atom = atom;
        if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
            out->is_integer = true;
            out->integer = atom->ground.ival;
        }
        return true;
    }
    if (node->kind != PREPARED_PURE_REGISTER || node->child_count != 2u ||
        depth == 0u || *budget == 0u)
        return false;
    return prepared_pure_inline_register_scalar(
        program, arena, node, local_base, depth, budget, out);
}

/* The register arm of prepared_pure_execute_register on unboxed operands,
 * for the cases where it is decided without a boxed value: structural
 * equality of operands known to share a type, and small exact integers
 * without overflow.  Every other case returns false and the node is
 * evaluated on atoms.  Kept out of line so that the immediate operands of
 * prepared_pure_inline_scalar need no frame for these operands. */
static bool PREPARED_PURE_NOINLINE prepared_pure_inline_register_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out) {
    (*budget)--;
    PreparedPureScalar left;
    PreparedPureScalar right;
    if (!prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child]],
            local_base, depth - 1u, budget, &left) ||
        !prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child + 1u]],
            local_base, depth - 1u, budget, &right))
        return false;
    if (node->instruction == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) {
        if (node->result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
            (!program->total_structural_equality &&
             !prepared_pure_scalar_operands_share_type(
                 program, node, &left, &right)))
            return false;
        bool equal;
        Atom integer_leaf;
        if (left.atom && right.atom) {
            equal = atom_value_eq(left.atom, right.atom);
        } else if (left.is_integer && right.is_integer) {
            equal = left.integer == right.integer;
        } else if (left.is_integer && right.atom) {
            atom_scalar_leaf_int(&integer_leaf, left.integer);
            equal = atom_value_eq(&integer_leaf, right.atom);
        } else if (right.is_integer && left.atom) {
            atom_scalar_leaf_int(&integer_leaf, right.integer);
            equal = atom_value_eq(left.atom, &integer_leaf);
        } else {
            return false;
        }
        out->is_boolean = true;
        out->boolean = equal;
        return true;
    }
    CettaGsltRegisterOperandDiscipline discipline;
    if (!left.is_integer || !right.is_integer ||
        !cetta_gslt_register_operand_discipline(
            node->instruction, &discipline) ||
        discipline != CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS)
        return false;
    int64_t integer = 0;
    bool boolean = false;
    bool promote = false;
    CettaGsltRegisterResultKind kind = node->result_kind;
    if (!cetta_gslt_register_execute_small_binary(
            node->instruction, &integer, &boolean,
            left.integer, right.integer, &kind, &promote) ||
        promote || kind != node->result_kind)
        return false;
    if (kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        out->is_integer = true;
        out->integer = integer;
        return true;
    }
    if (kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        out->is_boolean = true;
        out->boolean = boolean;
        return true;
    }
    return false;
}

/* Evaluate a register, intrinsic or constructor node whose operands are
 * immediate or again such nodes, without frames.  Each operation is the one
 * its frame runs, on the same operand values; NULL leaves the node to its
 * frames, which also report any decline. */
static Atom *prepared_pure_inline_value(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget) {
    Atom *immediate =
        prepared_pure_immediate_value(program, node, local_base);
    if (immediate)
        return immediate;
    if (depth == 0u || *budget == 0u ||
        node->child_count > PREPARED_PURE_INLINE_OPERANDS ||
        (node->kind != PREPARED_PURE_REGISTER &&
         node->kind != PREPARED_PURE_INTRINSIC &&
         node->kind != PREPARED_PURE_BUILD))
        return NULL;
    if (node->kind == PREPARED_PURE_REGISTER) {
        uint32_t scalar_budget = *budget;
        PreparedPureScalar scalar;
        if (prepared_pure_inline_scalar(
                program, arena, node, local_base, depth, &scalar_budget,
                &scalar)) {
            *budget = scalar_budget;
            if (scalar.is_boolean)
                return program->boolean_value(arena, scalar.boolean);
            if (scalar.atom)
                return scalar.atom;
            return scalar.is_integer ? atom_int(arena, scalar.integer) : NULL;
        }
    }
    (*budget)--;
    Atom *operands[PREPARED_PURE_INLINE_OPERANDS];
    for (uint32_t i = 0u; i < node->child_count; i++) {
        operands[i] = prepared_pure_inline_value(
            program, arena,
            &program->nodes[program->children[node->first_child + i]],
            local_base, depth - 1u, budget);
        if (!operands[i])
            return NULL;
    }
    if (node->kind == PREPARED_PURE_BUILD)
        return program->construct_value(
            arena, operands, node->child_count);
    if (node->kind == PREPARED_PURE_INTRINSIC) {
        Atom *result = prepared_pure_execute_intrinsic(
            program, arena, node->intrinsic_instruction, node->atom,
            operands, node->child_count);
        return result && !atom_is_error(result) ? result : NULL;
    }
    Atom *result = prepared_pure_execute_register(
        program, arena, node->instruction, node->result_kind,
        node->undefined_type_operands, operands, node->child_count);
    if (!result)
        result = prepared_pure_execute_register_intrinsic(
            program, arena, node->result_kind, node->atom,
            operands, node->child_count);
    return result &&
        !(result->kind == ATOM_EXPR && atom_is_error(result))
        ? result : NULL;
}

typedef enum {
    PREPARED_PURE_TRUTH_UNAVAILABLE = 0,
    PREPARED_PURE_TRUTH_TRUE,
    PREPARED_PURE_TRUTH_FALSE,
    PREPARED_PURE_TRUTH_NOT_BOOLEAN,
} PreparedPureTruth;

/* The truth of a conditional's condition when it can be evaluated in place.
 * UNAVAILABLE schedules the condition's frame; NOT_BOOLEAN is the value the
 * frame would reject. */
static PreparedPureTruth prepared_pure_inline_truth(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base) {
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    PreparedPureScalar condition;
    if (!prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child]],
            local_base, PREPARED_PURE_INLINE_DEPTH, &budget, &condition))
        return PREPARED_PURE_TRUTH_UNAVAILABLE;
    if (condition.is_boolean)
        return condition.boolean
            ? PREPARED_PURE_TRUTH_TRUE : PREPARED_PURE_TRUTH_FALSE;
    if (condition.atom && prepared_pure_is_true(condition.atom))
        return PREPARED_PURE_TRUTH_TRUE;
    if (condition.atom &&
        (prepared_pure_is_false(condition.atom) ||
         prepared_pure_petta_else_value(condition.atom)))
        return PREPARED_PURE_TRUTH_FALSE;
    return PREPARED_PURE_TRUTH_NOT_BOOLEAN;
}

static Atom *prepared_pure_inline_child(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t child, uint32_t local_base) {
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    return prepared_pure_inline_value(
        program, arena,
        &program->nodes[program->children[node->first_child + child]],
        local_base, PREPARED_PURE_INLINE_DEPTH, &budget);
}

static PreparedPureChildrenState prepared_pure_finish_children(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame, const PreparedPureNode *node) {
    if (frame->state == 0u) {
        frame->value_base = (uint32_t)program->value_len;
        frame->child_index = 0u;
        frame->state = 1u;
    } else if (frame->child_index < node->child_count) {
        if (program->value_len !=
            (size_t)frame->value_base + frame->child_index + 1u)
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->child_index++;
    }
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    while (frame->child_index < node->child_count) {
        uint32_t child =
            program->children[node->first_child + frame->child_index];
        Atom *value = prepared_pure_inline_value(
            program, arena, &program->nodes[child], frame->local_base,
            PREPARED_PURE_INLINE_DEPTH, &budget);
        if (!value) {
            if (!prepared_pure_push_frame(program, child, frame->local_base))
                return PREPARED_PURE_CHILDREN_FAILED;
            return PREPARED_PURE_CHILDREN_PENDING;
        }
        if (!prepared_pure_push_value(program, value))
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->child_index++;
    }
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

/* Replace a completed equation's tail-continuation spine with the tail call's
 * already-evaluated argument frame.  The nearest waiting call owns that
 * equation activation; selected branches and let bodies are transparent only
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

static bool PREPARED_PURE_HOT prepared_pure_resume_call(
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
            return prepared_pure_push_runtime_frame(
                program, argument);
        }
    }

    PreparedPureSelection selection = prepared_pure_select_equation(
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
    const PreparedPureEquation *equation = selection.equation;
    if (selection.state != PREPARED_PURE_SELECT_SELECTED || !equation ||
        program->slot_len > UINT32_MAX - equation->local_count ||
        !prepared_pure_reserve(
            (void **)&program->slots,
            sizeof(*program->slots), &program->slot_cap,
            program->slot_len + equation->local_count))
        return false;
    frame = &program->frames[program->frame_len - 1u];
    frame->saved_slot_len = (uint32_t)program->slot_len;
    uint32_t call_base = frame->saved_slot_len;
    if (equation->local_count > 0u)
        memcpy(&program->slots[program->slot_len],
               program->selected_values,
               sizeof(*program->slots) * equation->local_count);
    program->slot_len += equation->local_count;
    program->value_len = frame->value_base;
    frame->state = 2u;
    return prepared_pure_push_frame(
        program, equation->root, call_base);
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    if (!space || !expression || accumulator_var == VAR_ID_NONE ||
        item_var == VAR_ID_NONE || accumulator_var == item_var ||
        !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED))
        return NULL;
    CettaPreparedPureProgram *program = calloc(1u, sizeof(*program));
    if (!program)
        return NULL;
    program->references = 1u;
    program->space = space;
    program->source_program = space_program_token(space);
    program->equation_projection = space_equation_token(space);
    program->boolean_value = boolean_value;
    program->construct_value = construct_value;
    program->opaque_value = opaque_value;
    program->register_view = register_view;
    program->expression_view = expression_view;
    program->pattern_view = pattern_view;
    if (source_view)
        program->source_view = *source_view;
    program->call_mode = call_mode;
    program->total_structural_equality = total_structural_equality;
    program->match_decision_semantics = match_decision_semantics;
    PreparedPureCompileContext context = {0};
    if (!prepared_pure_context_bind(
            &context, accumulator_var, false,
            &program->accumulator_slot) ||
        !prepared_pure_context_bind(
            &context, item_var, false, &program->item_slot) ||
        !prepared_pure_compile_eval(
            program, &context, expression,
            program->source_view.root, 0u, &program->root)) {
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

    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(
            program, program->source_view.root);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_VALUE ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DATA)
        return true;
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return false;

    /* Entry arguments being values does not make the operator an equation
     * call.  Apply the same dialect interpretation as compile_eval before
     * installing the reusable entry-call instruction.  A projection retains
     * the entry's value/demand discipline; recompiling an already evaluated
     * child as source could execute callable-looking data a second time. */
    if (program->expression_view) {
        CettaPreparedPureExpressionView view = {0};
        CettaPreparedPureExpressionViewState state =
            program->expression_view(expression, &view);
        if (state == CETTA_PREPARED_PURE_EXPRESSION_PROJECT) {
            for (CettaExprIndex index = 1u;
                 index < expression->expr.len; index++) {
                if (expression->expr.elems[index] != view.projected)
                    continue;
                uint32_t *children = NULL;
                *admitted = true;
                if (!prepared_pure_compile_entry_arguments(
                        program, expression,
                        entry_arguments_are_values
                            ? PREPARED_PURE_ENTRY_ARGUMENT
                            : PREPARED_PURE_EVAL_ENTRY_ARGUMENT,
                        &children))
                    return false;
                *root_out = children[index - 1u];
                free(children);
                program->entry_head = expression->expr.elems[0]->sym_id;
                return true;
            }
            return prepared_pure_reject(
                program, "dialect projection is not an entry argument",
                expression);
        }
        if (state != CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return prepared_pure_reject(
                program, "dialect-owned entry requires canonical evaluation",
                expression);
    }

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

static bool prepared_pure_answer_value_node(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, uint32_t node_index,
    uint32_t depth) {
    if (!program || !equation || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL)
        return node->atom != NULL;
    if (node->kind == PREPARED_PURE_SLOT)
        return node->auxiliary < equation->local_count;
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (!prepared_pure_answer_value_node(
                program, equation,
                program->children[node->first_child + index],
                depth + 1u))
            return false;
    }
    return true;
}

/* A node whose evaluation cannot choose: it holds no choice zero and calls
 * only deterministic heads. */
static bool prepared_pure_node_chooses(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return true;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_ZERO)
        return true;
    if (node->kind == PREPARED_PURE_CALL &&
        (node->auxiliary >= program->head_len ||
         !program->heads[node->auxiliary].deterministic))
        return true;
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return true;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (prepared_pure_node_chooses(
                program, program->children[node->first_child + index],
                depth + 1u))
            return true;
    }
    return false;
}

/* The greatest fixed point: start from the heads weak-head matching makes
 * determinate and drop any whose body can choose, until none changes. */
static void prepared_pure_classify_deterministic_heads(
    CettaPreparedPureProgram *program) {
    for (uint32_t index = 0u; index < program->head_len; index++) {
        PreparedPureHead *head = &program->heads[index];
        head->deterministic = head->compiled &&
            prepared_pure_head_is_whnf_determinate(program, head);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t index = 0u; index < program->head_len; index++) {
            PreparedPureHead *head = &program->heads[index];
            if (!head->deterministic)
                continue;
            for (uint32_t offset = 0u;
                 offset < head->equation_count; offset++) {
                const PreparedPureEquation *equation =
                    &program->equations[head->first_equation + offset];
                if (prepared_pure_node_chooses(
                        program, equation->root, 0u)) {
                    head->deterministic = false;
                    changed = true;
                    break;
                }
            }
        }
    }
}

/* A value tree evaluates to itself: literals and locals under constructors,
 * with no computation to order. */
static bool prepared_pure_value_tree(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL)
        return node->atom != NULL;
    if (node->kind == PREPARED_PURE_SLOT)
        return true;
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (!prepared_pure_value_tree(
                program, program->children[node->first_child + index],
                depth + 1u))
            return false;
    }
    return true;
}

/* One enclosing construct of the node being lowered: its copy, the child
 * position the node occupies, and the operands already lowered before it. */
typedef struct {
    PreparedPureNode node;
    uint32_t hole;
    const uint32_t *before;
} PreparedPureZipperFrame;

typedef struct {
    CettaPreparedPureProgram *program;
    uint32_t next_slot;
    PreparedPureZipperFrame *zipper;
    size_t zipper_len;
    size_t zipper_cap;
} PreparedPureLowering;

static bool prepared_pure_zipper_push(
    PreparedPureLowering *lowering, const PreparedPureNode *node,
    uint32_t hole, const uint32_t *before) {
    if (!prepared_pure_reserve(
            (void **)&lowering->zipper, sizeof(*lowering->zipper),
            &lowering->zipper_cap, lowering->zipper_len + 1u))
        return false;
    lowering->zipper[lowering->zipper_len++] = (PreparedPureZipperFrame){
        .node = *node, .hole = hole, .before = before,
    };
    return true;
}

static void prepared_pure_zipper_pop(PreparedPureLowering *lowering) {
    if (lowering->zipper_len > 0u)
        lowering->zipper_len--;
}

/* The rest of the body once a call's answer is in `slot`: rebuild each
 * enclosing construct around the hole, innermost first.  Operands lowered
 * before the hole are the nodes that read their values; later children are
 * the source nodes still to run. */
static uint32_t prepared_pure_resume_template(
    PreparedPureLowering *lowering, uint32_t slot) {
    CettaPreparedPureProgram *program = lowering->program;
    uint32_t current = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){.kind = PREPARED_PURE_SLOT, .auxiliary = slot},
            NULL, 0u, &current))
        return PREPARED_PURE_NO_TEMPLATE;
    for (size_t index = lowering->zipper_len; index-- > 0u;) {
        const PreparedPureZipperFrame *frame = &lowering->zipper[index];
        uint32_t count = frame->node.child_count;
        if (frame->hole >= count ||
            frame->node.first_child > program->child_len ||
            count > program->child_len - frame->node.first_child)
            return PREPARED_PURE_NO_TEMPLATE;
        uint32_t *children = malloc(sizeof(*children) * count);
        if (!children)
            return PREPARED_PURE_NO_TEMPLATE;
        for (uint32_t child = 0u; child < count; child++) {
            if (child == frame->hole)
                children[child] = current;
            else if (child < frame->hole && frame->before)
                children[child] = frame->before[child];
            else
                children[child] =
                    program->children[frame->node.first_child + child];
        }
        bool ok = prepared_pure_add_node(
            program, frame->node, children, count, &current);
        free(children);
        if (!ok)
            return PREPARED_PURE_NO_TEMPLATE;
    }
    return current;
}

static bool prepared_pure_emit_step(
    PreparedPureLowering *lowering, PreparedPureStep step,
    uint32_t *index_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (program->step_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->steps, sizeof(*program->steps),
            &program->step_cap, program->step_len + 1u))
        return false;
    if (index_out)
        *index_out = (uint32_t)program->step_len;
    program->steps[program->step_len++] = step;
    return true;
}

static bool prepared_pure_lower_node(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth);

/* The slot holding a child's value once its steps have run. */
static bool prepared_pure_lower_to_slot(
    PreparedPureLowering *lowering, uint32_t child, uint32_t depth,
    uint32_t *slot_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (child < program->node_len &&
        program->nodes[child].kind == PREPARED_PURE_SLOT) {
        *slot_out = program->nodes[child].auxiliary;
        return true;
    }
    if (lowering->next_slot >= PREPARED_PURE_MAX_SLOTS)
        return false;
    uint32_t temporary = lowering->next_slot++;
    if (!prepared_pure_lower_node(
            lowering, child, false, temporary, depth + 1u))
        return false;
    *slot_out = temporary;
    return true;
}

/* A node standing for a child once its steps have run: a value tree stays
 * in place, since it has no evaluation to order; anything else becomes a
 * read of the temporary its steps fill. */
static bool prepared_pure_lower_operand(
    PreparedPureLowering *lowering, uint32_t child, uint32_t depth,
    uint32_t *node_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (prepared_pure_value_tree(program, child, 0u)) {
        *node_out = child;
        return true;
    }
    uint32_t slot = 0u;
    return prepared_pure_lower_to_slot(lowering, child, depth, &slot) &&
        prepared_pure_add_node(
            program,
            (PreparedPureNode){.kind = PREPARED_PURE_SLOT, .auxiliary = slot},
            NULL, 0u, node_out);
}

static bool prepared_pure_lower_operands(
    PreparedPureLowering *lowering, const PreparedPureNode *node,
    uint32_t depth, uint32_t **operands_out) {
    CettaPreparedPureProgram *program = lowering->program;
    uint32_t count = node->child_count;
    uint32_t first = node->first_child;
    uint32_t *operands = malloc(sizeof(*operands) * (count ? count : 1u));
    if (!operands)
        return false;
    for (uint32_t index = 0u; index < count; index++) {
        bool ok = prepared_pure_zipper_push(lowering, node, index, operands) &&
            prepared_pure_lower_operand(
                lowering, program->children[first + index], depth,
                &operands[index]);
        prepared_pure_zipper_pop(lowering);
        if (!ok) {
            free(operands);
            return false;
        }
    }
    *operands_out = operands;
    return true;
}

/* Lower one node of an answer producer's body into steps, in the order the
 * executor evaluates it: operands left to right, a condition before its
 * selected branch, a bound expression before its pattern and body.  A node
 * that cannot choose stays one executor step.  In tail position the node's
 * answers are the body's; otherwise its value lands in `destination`. */
static bool prepared_pure_lower_node(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth) {
    CettaPreparedPureProgram *program = lowering->program;
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (!prepared_pure_node_chooses(program, node_index, 0u))
        return prepared_pure_emit_step(
            lowering,
            (PreparedPureStep){
                .kind = tail ? PREPARED_PURE_STEP_RETURN
                             : PREPARED_PURE_STEP_EVAL,
                .value_tree =
                    prepared_pure_value_tree(program, node_index, 0u),
                .node = node_index,
                .slot = destination,
            },
            NULL);
    /* Nodes are appended below; work from a copy. */
    PreparedPureNode node = program->nodes[node_index];
    if (node.first_child > program->child_len ||
        node.child_count > program->child_len - node.first_child)
        return false;
    const uint32_t *children = &program->children[node.first_child];
    switch (node.kind) {
    case PREPARED_PURE_ZERO:
        return prepared_pure_emit_step(
            lowering, (PreparedPureStep){.kind = PREPARED_PURE_STEP_FAIL},
            NULL);
    case PREPARED_PURE_IF: {
        if (node.child_count != 3u)
            return false;
        uint32_t then_node = children[1];
        uint32_t else_node = children[2];
        uint32_t condition = 0u;
        uint32_t branch = 0u;
        uint32_t join = 0u;
        bool lowered_condition =
            prepared_pure_zipper_push(lowering, &node, 0u, NULL) &&
            prepared_pure_lower_to_slot(
                lowering, children[0], depth, &condition);
        prepared_pure_zipper_pop(lowering);
        if (!lowered_condition ||
            !prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = PREPARED_PURE_STEP_BRANCH, .slot = condition},
                &branch) ||
            !prepared_pure_lower_node(
                lowering, then_node, tail, destination, depth + 1u) ||
            (!tail &&
             !prepared_pure_emit_step(
                 lowering,
                 (PreparedPureStep){.kind = PREPARED_PURE_STEP_JUMP},
                 &join)))
            return false;
        program->steps[branch].target = (uint32_t)program->step_len;
        if (!prepared_pure_lower_node(
                lowering, else_node, tail, destination, depth + 1u))
            return false;
        if (!tail)
            program->steps[join].target = (uint32_t)program->step_len;
        return true;
    }
    case PREPARED_PURE_BIND: {
        if (node.child_count != 2u)
            return false;
        uint32_t body = children[1];
        uint32_t bound = 0u;
        bool lowered_bound =
            prepared_pure_zipper_push(lowering, &node, 0u, NULL) &&
            prepared_pure_lower_to_slot(
                lowering, children[0], depth, &bound);
        prepared_pure_zipper_pop(lowering);
        return lowered_bound &&
            prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = PREPARED_PURE_STEP_MATCH,
                    .slot = bound,
                    .auxiliary = node.auxiliary,
                },
                NULL) &&
            prepared_pure_lower_node(
                lowering, body, tail, destination, depth + 1u);
    }
    case PREPARED_PURE_CALL:
        if (node.auxiliary >= program->head_len)
            return false;
        if (!program->heads[node.auxiliary].deterministic) {
            uint32_t *operands = NULL;
            if (!prepared_pure_lower_operands(
                    lowering, &node, depth, &operands))
                return false;
            bool ok = program->step_argument_len <=
                    UINT32_MAX - node.child_count &&
                prepared_pure_reserve(
                    (void **)&program->step_arguments,
                    sizeof(*program->step_arguments),
                    &program->step_argument_cap,
                    program->step_argument_len + node.child_count);
            uint32_t first_argument = (uint32_t)program->step_argument_len;
            if (ok) {
                if (node.child_count > 0u)
                    memcpy(&program->step_arguments[first_argument],
                           operands,
                           sizeof(*operands) * node.child_count);
                program->step_argument_len += node.child_count;
            }
            free(operands);
            uint32_t resume_template = ok && !tail
                ? prepared_pure_resume_template(lowering, destination)
                : PREPARED_PURE_NO_TEMPLATE;
            return ok && prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = tail ? PREPARED_PURE_STEP_TAIL
                                 : PREPARED_PURE_STEP_CALL,
                    .slot = destination,
                    .auxiliary = node.auxiliary,
                    .first_argument = first_argument,
                    .arity = node.child_count,
                    .resume_template = resume_template,
                },
                NULL);
        }
        /* fall through: only the operands choose */
    case PREPARED_PURE_BUILD:
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_INTRINSIC:
    case PREPARED_PURE_OBSERVE: {
        uint32_t *operands = NULL;
        uint32_t lowered = 0u;
        if (!prepared_pure_lower_operands(
                lowering, &node, depth, &operands))
            return false;
        bool ok = prepared_pure_add_node(
            program, node, operands, node.child_count, &lowered);
        free(operands);
        return ok && prepared_pure_emit_step(
            lowering,
            (PreparedPureStep){
                .kind = tail ? PREPARED_PURE_STEP_RETURN
                             : PREPARED_PURE_STEP_EVAL,
                .value_tree = prepared_pure_value_tree(program, lowered, 0u),
                .node = lowered,
                .slot = destination,
            },
            NULL);
    }
    default:
        return prepared_pure_reject(
            program, "node kind has no step lowering", node.atom);
    }
}

/* A value tree, or a last call over value trees, answers directly. */
static bool prepared_pure_equation_tail_only(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation) {
    if (prepared_pure_answer_value_node(
            program, equation, equation->root, 0u))
        return true;
    const PreparedPureNode *root = &program->nodes[equation->root];
    if (root->kind != PREPARED_PURE_CALL || !root->tail_position ||
        root->auxiliary >= program->head_len ||
        root->first_child > program->child_len ||
        root->child_count > program->child_len - root->first_child)
        return false;
    for (uint32_t child = 0u; child < root->child_count; child++) {
        if (!prepared_pure_answer_value_node(
                program, equation,
                program->children[root->first_child + child], 0u))
            return false;
    }
    return true;
}

/* This is an answer-effect capability check, not a second evaluator.  An
 * equation whose body is a value tree, or a last call over value trees,
 * answers directly.  Every other body is lowered to a step program whose
 * deterministic regions stay executor evaluations; a failed lowering leaves
 * the canonical evaluator authoritative. */
static bool prepared_pure_lower_answer_producer(
    CettaPreparedPureProgram *program) {
    if (!program || !program->answer_producer ||
        !program->closed_program ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        program->root >= program->node_len ||
        program->entry_argument_count > 64u)
        return false;
    const PreparedPureNode *entry = &program->nodes[program->root];
    if (entry->kind != PREPARED_PURE_CALL ||
        entry->head != program->entry_head ||
        entry->auxiliary >= program->head_len ||
        entry->child_count != program->entry_argument_count)
        return prepared_pure_reject(
            program, "answer producer entry is not a closed call", NULL);
    for (uint32_t head_index = 0u;
         head_index < program->head_len; head_index++) {
        const PreparedPureHead *head = &program->heads[head_index];
        if (!head->compiled || head->equation_count == 0u ||
            head->first_equation > program->equation_len ||
            head->equation_count >
                program->equation_len - head->first_equation)
            return prepared_pure_reject(
                program, "answer producer reaches an uncompiled head",
                NULL);
    }
    prepared_pure_classify_deterministic_heads(program);
    for (size_t index = 0u; index < program->equation_len; index++) {
        PreparedPureEquation *equation = &program->equations[index];
        if (equation->arity > 64u ||
            equation->root >= program->node_len)
            return false;
        equation->tail_only =
            prepared_pure_equation_tail_only(program, equation);
        if (equation->tail_only)
            continue;
        PreparedPureLowering lowering = {
            .program = program,
            .next_slot = equation->local_count,
        };
        size_t first_step = program->step_len;
        bool lowered = first_step <= UINT32_MAX &&
            prepared_pure_lower_node(
                &lowering, equation->root, true, 0u, 0u);
        free(lowering.zipper);
        if (!lowered)
            return prepared_pure_reject(
                program, "answer body has no step lowering",
                equation->lhs);
        /* Lowering appends nodes, never equations; re-address the entry. */
        equation = &program->equations[index];
        equation->first_step = (uint32_t)first_step;
        equation->step_count = (uint32_t)(program->step_len - first_step);
        equation->frame_slot_count = lowering.next_slot;
        program->continuation_steps = true;
    }
    return true;
}

static CettaPreparedPureProgram *
prepared_pure_program_compile_closed_mode(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    bool answer_producer) {
    if (!space || !expression || atom_has_vars(expression) ||
        !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED) ||
        (answer_producer &&
         (call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
          !entry_arguments_are_values)))
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
    program->references = 1u;
    program->space = space;
    program->source_program = space_program_token(space);
    program->equation_projection = space_equation_token(space);
    program->boolean_value = boolean_value;
    program->construct_value = construct_value;
    program->opaque_value = opaque_value;
    program->register_view = register_view;
    program->expression_view = expression_view;
    program->pattern_view = pattern_view;
    if (source_view)
        program->source_view = *source_view;
    program->call_mode = call_mode;
    program->total_structural_equality = total_structural_equality;
    program->match_decision_semantics = match_decision_semantics;
    program->closed_program = true;
    program->answer_producer = answer_producer;
    program->allow_callable_templates = true;
    PreparedPureCompileContext context = {0};
    bool entry_call_admitted = false;
    bool compiled = prepared_pure_compile_closed_entry_call(
        program, expression, entry_arguments_are_values,
        &entry_call_admitted, &program->root);
    if (compiled && !entry_call_admitted) {
        compiled = prepared_pure_compile_eval(
            program, &context, expression,
            program->source_view.root, 0u, &program->root);
    }
    program->root_local_count = context.next_slot;
    free(context.bindings);
    if (!compiled || !prepared_pure_compile_pending_heads(program) ||
        (answer_producer &&
         !prepared_pure_lower_answer_producer(program))) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    return program;
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
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    return prepared_pure_program_compile_closed_mode(
        space, expression, call_mode, boolean_value, construct_value,
        opaque_value, register_view, expression_view, pattern_view,
        source_view, entry_arguments_are_values,
        total_structural_equality, match_decision_semantics, false);
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed_answers(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    return prepared_pure_program_compile_closed_mode(
        space, expression, call_mode, boolean_value, construct_value,
        opaque_value, register_view, expression_view, pattern_view,
        source_view, entry_arguments_are_values,
        total_structural_equality, match_decision_semantics, true);
}

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program) {
    return program &&
           space_program_token_is_current(program->source_program);
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

/* Where an answer of a call goes: the step after the call in its caller's
 * step program, over the caller's locals as they stood at the call.  A NULL
 * continuation is the cursor's consumer.  A record never changes once a call
 * holds it, so every answer of the call resumes the same state. */
typedef struct PreparedPureContinuation PreparedPureContinuation;
struct PreparedPureContinuation {
    const PreparedPureContinuation *parent;
    Atom **locals;
    uint32_t equation;
    uint32_t step;
    uint32_t slot;
};

typedef struct {
    uint32_t head_index;
    uint32_t arity;
    uint32_t next_equation;
    size_t argument_base;
    bool matched_equation;
    /* Value-arena position before this call's arguments were built.  A call
     * entered as a last call inherits its caller's position, because its
     * arguments may share the caller's. */
    ArenaMark mark;
    /* Position once the call was entered: what its earlier equations built
     * lies above it and is dead when it tries the next. */
    ArenaMark alternative_mark;
    const PreparedPureContinuation *continuation;
} PreparedPureAnswerFrame;

/* Build a value tree over `locals`: matched variables for a direct answer,
 * or the running step program's locals. */
static bool prepared_pure_project_answer_value(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *const *locals, uint32_t local_count, uint32_t node_index,
    uint32_t depth, const CettaPreparedPureAnswerLimits *limits,
    size_t *scratch_remaining, bool *limit_hit, Atom **value_out) {
    if (value_out)
        *value_out = NULL;
    if (!program || !arena || (!locals && local_count > 0u) ||
        !value_out || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL) {
        *value_out = node->atom;
        return *value_out != NULL;
    }
    if (node->kind == PREPARED_PURE_SLOT) {
        if (node->auxiliary >= local_count)
            return false;
        *value_out = locals[node->auxiliary];
        return *value_out != NULL;
    }
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;

    size_t allocation_bound = 0u;
    if (!limits->construct_allocation_bound)
        return false;
    if (!limits->construct_allocation_bound(
            node->child_count, &allocation_bound) ||
        allocation_bound > *scratch_remaining) {
        *limit_hit = true;
        return false;
    }
    *scratch_remaining -= allocation_bound;

    enum { PREPARED_PURE_INLINE_ANSWER_CHILDREN = 16u };
    Atom *inline_children[PREPARED_PURE_INLINE_ANSWER_CHILDREN];
    Atom **children = node->child_count <=
            PREPARED_PURE_INLINE_ANSWER_CHILDREN
        ? inline_children
        : malloc(sizeof(*children) * node->child_count);
    if (!children)
        return false;
    bool ok = true;
    for (uint32_t child = 0u; child < node->child_count; child++) {
        if (!prepared_pure_project_answer_value(
                program, arena, locals, local_count,
                program->children[node->first_child + child],
                depth + 1u, limits, scratch_remaining, limit_hit,
                &children[child])) {
            ok = false;
            break;
        }
    }
    if (ok)
        *value_out = program->construct_value(
            arena, children, node->child_count);
    if (children != inline_children)
        free(children);
    return ok && *value_out != NULL;
}

struct CettaPreparedPureAnswerCursor {
    CettaPreparedPureProgram *program;
    Arena *arena;
    Arena owned_arena;
    bool owns_arena;
    /* The frontier's values all live in the owned arena. */
    bool detached;
    CettaPreparedPureUnmatchedCall unmatched_call;
    CettaPreparedPureAnswerLimits limits;
    uint64_t remaining_transitions;
    /* A caller arena is charged and never recharged.  An owned arena bounds
     * the bytes live above base_live, so reclaimed storage is recharged. */
    size_t scratch_remaining;
    size_t base_live;
    CettaPreparedPureInterruptPollFn interrupt_poll;
    void *interrupt_context;
    uint32_t poll_interval;
    uint32_t poll_countdown;
    PreparedPureAnswerFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    Atom **arguments;
    size_t argument_len;
    size_t argument_cap;
    /* Owned arena: the storage of the last answer, dead once consumed. */
    bool answer_pending;
    ArenaMark answer_mark;
    /* The step that produced the last answer, for unyield. */
    bool undo_valid;
    size_t undo_frame;
    uint32_t undo_next_equation;
    bool undo_matched_equation;
    uint8_t *head_seen;
    SymbolId *seen_heads;
    uint32_t seen_head_len;
    CettaPreparedPureHeadAdmissionFn head_admission;
    void *head_admission_context;
    uint64_t answers;
    uint64_t tail_calls;
    CettaPreparedPureHandoffReason handoff;
};

/* Record a relation's first entry and ask whether it is admitted. */
static bool prepared_pure_answer_cursor_see_head(
    CettaPreparedPureAnswerCursor *cursor, uint32_t head_index) {
    if (head_index >= cursor->program->head_len)
        return false;
    if (cursor->head_seen[head_index])
        return true;
    cursor->head_seen[head_index] = 1u;
    SymbolId head = cursor->program->heads[head_index].head;
    cursor->seen_heads[cursor->seen_head_len++] = head;
    return !cursor->head_admission ||
        cursor->head_admission(cursor->head_admission_context, head);
}

/* Reserve room for one more call before anything is written, so a failed
 * push leaves the frontier intact. */
static bool prepared_pure_answer_cursor_reserve(
    CettaPreparedPureAnswerCursor *cursor, uint32_t arity) {
    return arity <= 64u &&
        cursor->argument_len <= SIZE_MAX - arity &&
        prepared_pure_reserve(
            (void **)&cursor->frames, sizeof(*cursor->frames),
            &cursor->frame_cap, cursor->frame_len + 1u) &&
        prepared_pure_reserve(
            (void **)&cursor->arguments, sizeof(*cursor->arguments),
            &cursor->argument_cap, cursor->argument_len + arity);
}

static void prepared_pure_answer_cursor_push_reserved(
    CettaPreparedPureAnswerCursor *cursor, uint32_t head_index,
    Atom *const *arguments, uint32_t arity, ArenaMark mark,
    const PreparedPureContinuation *continuation) {
    size_t base = cursor->argument_len;
    if (arity > 0u)
        memcpy(&cursor->arguments[base], arguments,
               sizeof(*cursor->arguments) * arity);
    cursor->frames[cursor->frame_len++] = (PreparedPureAnswerFrame){
        .head_index = head_index,
        .arity = arity,
        .argument_base = base,
        .mark = mark,
        .alternative_mark = arena_mark(cursor->arena),
        .continuation = continuation,
    };
    cursor->argument_len += arity;
}

static size_t prepared_pure_answer_cursor_scratch(
    const CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor->owns_arena)
        return cursor->scratch_remaining;
    size_t live = arena_accounted_live_bytes(cursor->arena);
    size_t used = live > cursor->base_live ? live - cursor->base_live : 0u;
    return used < cursor->limits.max_scratch_bytes
        ? cursor->limits.max_scratch_bytes - used : 0u;
}

static CettaPreparedPureCursorStep prepared_pure_answer_cursor_handoff(
    CettaPreparedPureAnswerCursor *cursor,
    CettaPreparedPureHandoffReason reason) {
    if (prepared_pure_debug_enabled()) {
        static const char *const names[] = {
            "none", "unsupported", "no match", "limit", "interrupt", "stale",
        };
        fprintf(stderr, "prepared-pure cursor handoff: %s\n",
                (size_t)reason < sizeof(names) / sizeof(names[0])
                    ? names[reason] : "unknown");
    }
    cursor->handoff = reason;
    return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
}

/* Undo a step that failed after advancing its call: the frontier returns to
 * the state before the step, and an owned arena drops what it built. */
static void prepared_pure_answer_cursor_restore_step(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t next_equation, bool matched_equation, ArenaMark step_mark) {
    PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
    frame->next_equation = next_equation;
    frame->matched_equation = matched_equation;
    if (cursor->owns_arena)
        arena_reset(cursor->arena, step_mark);
}

CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open(
    CettaPreparedPureProgram *program,
    const CettaPreparedPureAnswerCursorOptions *options) {
    if (!program || !options || !program->answer_producer ||
        (program->continuation_steps && !options->allow_continuations) ||
        !cetta_prepared_pure_program_is_current(program) ||
        program->root >= program->node_len ||
        (options->arena && options->arena->hashcons))
        return NULL;
    const PreparedPureNode *entry = &program->nodes[program->root];
    if (entry->kind != PREPARED_PURE_CALL ||
        entry->auxiliary >= program->head_len ||
        entry->child_count != program->entry_argument_count ||
        entry->child_count > 64u ||
        (entry->child_count > 0u && !program->entry_arguments))
        return NULL;
    CettaPreparedPureAnswerCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor)
        return NULL;
    cursor->head_seen = calloc(program->head_len, sizeof(*cursor->head_seen));
    cursor->seen_heads = malloc(
        program->head_len * sizeof(*cursor->seen_heads));
    cursor->program = cetta_prepared_pure_program_retain(program);
    if (!cursor->head_seen || !cursor->seen_heads || !cursor->program) {
        if (cursor->program)
            cetta_prepared_pure_program_free(cursor->program);
        free(cursor->head_seen);
        free(cursor->seen_heads);
        free(cursor);
        return NULL;
    }
    cursor->unmatched_call = options->unmatched_call;
    cursor->limits = options->limits;
    cursor->remaining_transitions = options->limits.max_transitions;
    cursor->scratch_remaining = options->limits.max_scratch_bytes;
    cursor->interrupt_poll = options->interrupt_poll;
    cursor->interrupt_context = options->interrupt_context;
    cursor->poll_interval = options->interrupt_poll_interval
        ? options->interrupt_poll_interval : 1u;
    cursor->poll_countdown = cursor->poll_interval;
    cursor->head_admission = options->head_admission;
    cursor->head_admission_context = options->head_admission_context;
    if (options->arena) {
        cursor->arena = options->arena;
    } else {
        arena_init(&cursor->owned_arena);
        arena_set_runtime_kind(
            &cursor->owned_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_hashcons(&cursor->owned_arena, NULL);
        cursor->arena = &cursor->owned_arena;
        cursor->owns_arena = true;
    }

    uint32_t arity = entry->child_count;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < arity; index++)
        ok = program->entry_arguments[index] != NULL;
    cursor->base_live = arena_accounted_live_bytes(cursor->arena);
    ok = ok && prepared_pure_answer_cursor_reserve(cursor, arity);
    if (ok) {
        prepared_pure_answer_cursor_push_reserved(
            cursor, entry->auxiliary, program->entry_arguments, arity,
            arena_mark(cursor->arena), NULL);
        ok = prepared_pure_answer_cursor_see_head(cursor, entry->auxiliary);
    }
    if (!ok) {
        cetta_prepared_pure_answer_cursor_close(cursor);
        return NULL;
    }
    return cursor;
}

static bool PREPARED_PURE_HOT prepared_pure_program_execute_internal(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom *runtime_expression,
    uint32_t step_node, uint32_t step_slot_count,
    bool closed,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context,
    Atom **result_out);

/* The call a frame stands for, as data. */
static Atom *prepared_pure_answer_cursor_call_atom(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
    enum { PREPARED_PURE_INLINE_CALL_ELEMENTS = 17u };
    Atom *inline_elements[PREPARED_PURE_INLINE_CALL_ELEMENTS];
    Atom **elements = frame->arity < PREPARED_PURE_INLINE_CALL_ELEMENTS
        ? inline_elements
        : malloc(sizeof(*elements) * (frame->arity + 1u));
    if (!elements || frame->head_index >= program->head_len)
        return NULL;
    elements[0] = atom_symbol_id(
        cursor->arena, program->heads[frame->head_index].head);
    if (frame->arity > 0u)
        memcpy(&elements[1], &cursor->arguments[frame->argument_base],
               sizeof(*elements) * frame->arity);
    Atom *call = elements[0]
        ? program->construct_value(cursor->arena, elements, frame->arity + 1u)
        : NULL;
    if (elements != inline_elements)
        free(elements);
    return call;
}

typedef enum {
    PREPARED_PURE_RUN_CONTINUE = 0,
    PREPARED_PURE_RUN_ANSWER,
    PREPARED_PURE_RUN_HANDOFF,
} PreparedPureRunResult;

/* A caller arena is charged for what an executor evaluation built; an owned
 * arena is measured live. */
static bool prepared_pure_answer_cursor_charge(
    CettaPreparedPureAnswerCursor *cursor, size_t live_before) {
    if (cursor->owns_arena)
        return prepared_pure_answer_cursor_scratch(cursor) > 0u;
    size_t live = arena_accounted_live_bytes(cursor->arena);
    size_t built = live > live_before ? live - live_before : 0u;
    if (built > cursor->scratch_remaining) {
        cursor->scratch_remaining = 0u;
        return false;
    }
    cursor->scratch_remaining -= built;
    return true;
}

/* The value of an EVAL or RETURN step over the installed locals. */
static Atom *prepared_pure_answer_step_value(
    CettaPreparedPureAnswerCursor *cursor, const PreparedPureStep *step,
    uint32_t slot_count, bool *limit_hit) {
    CettaPreparedPureProgram *program = cursor->program;
    Atom *value = NULL;
    size_t live_before = arena_accounted_live_bytes(cursor->arena);
    if (step->value_tree) {
        size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
        if (!prepared_pure_project_answer_value(
                program, cursor->arena, program->slots, slot_count,
                step->node, 0u, &cursor->limits, &scratch, limit_hit,
                &value))
            return NULL;
        if (!cursor->owns_arena)
            cursor->scratch_remaining = scratch;
        return value;
    }
    bool evaluated = prepared_pure_program_execute_internal(
        program, cursor->arena, NULL, NULL, NULL, step->node, slot_count,
        true, 0u, NULL, NULL, &value);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->slot_len = slot_count;
    if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
        *limit_hit = true;
        return NULL;
    }
    return evaluated ? value : NULL;
}

/* Enter the call of a CALL or TAIL step.  A CALL records where its answers
 * resume; a TAIL passes on the continuation it runs under.  The caller is
 * the frontier's last call; with no equation left it is replaced. */
static PreparedPureRunResult prepared_pure_answer_cursor_enter(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t ordinal, bool matched_before, ArenaMark step_mark,
    uint32_t equation_index, uint32_t pc, const PreparedPureStep *step,
    const PreparedPureContinuation *continuation) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureEquation *body = &program->equations[equation_index];
    uint32_t callee = step->auxiliary;
    uint32_t arity = step->arity;
    if (callee >= program->head_len || arity > 64u ||
        step->first_argument > program->step_argument_len ||
        arity > program->step_argument_len - step->first_argument) {
        prepared_pure_answer_cursor_restore_step(
            cursor, frame_index, ordinal, matched_before, step_mark);
        (void)prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    enum { PREPARED_PURE_INLINE_STEP_ARGUMENTS = 16u };
    Atom *inline_arguments[PREPARED_PURE_INLINE_STEP_ARGUMENTS];
    Atom **arguments = arity <= PREPARED_PURE_INLINE_STEP_ARGUMENTS
        ? inline_arguments
        : malloc(sizeof(*arguments) * arity);
    size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
    bool limit_hit = false;
    bool projected = arguments != NULL;
    for (uint32_t index = 0u; projected && index < arity; index++) {
        projected = prepared_pure_project_answer_value(
                program, cursor->arena, program->slots,
                body->frame_slot_count,
                program->step_arguments[step->first_argument + index],
                0u, &cursor->limits, &scratch, &limit_hit,
                &arguments[index]) &&
            arguments[index] &&
            !atom_has_vars(arguments[index]) &&
            !atom_has_thread_local_resource(arguments[index]);
    }
    if (!cursor->owns_arena)
        cursor->scratch_remaining = scratch;
    const PreparedPureContinuation *callee_continuation = continuation;
    if (projected && step->kind == PREPARED_PURE_STEP_CALL) {
        size_t live_before = arena_accounted_live_bytes(cursor->arena);
        uint32_t slot_count = body->frame_slot_count;
        PreparedPureContinuation *record =
            arena_alloc(cursor->arena, sizeof(*record));
        Atom **locals = arena_alloc(
            cursor->arena, sizeof(*locals) * (slot_count ? slot_count : 1u));
        if (!record || !locals) {
            projected = false;
        } else {
            if (slot_count > 0u)
                memcpy(locals, program->slots, sizeof(*locals) * slot_count);
            *record = (PreparedPureContinuation){
                .parent = continuation,
                .locals = locals,
                .equation = equation_index,
                .step = pc + 1u,
                .slot = step->slot,
            };
            callee_continuation = record;
            if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
                projected = false;
                limit_hit = true;
            }
        }
    }
    bool reserved = projected &&
        prepared_pure_answer_cursor_reserve(cursor, arity);
    if (!reserved) {
        if (arguments != inline_arguments)
            free(arguments);
        prepared_pure_answer_cursor_restore_step(
            cursor, frame_index, ordinal, matched_before, step_mark);
        (void)prepared_pure_answer_cursor_handoff(
            cursor, limit_hit || projected
                ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
    ArenaMark child_mark = step_mark;
    if (frame_index + 1u == cursor->frame_len &&
        frame->next_equation >=
            program->heads[frame->head_index].equation_count) {
        /* Last call: the caller has no alternative left. */
        child_mark = frame->mark;
        cursor->argument_len = frame->argument_base;
        cursor->frame_len--;
    }
    prepared_pure_answer_cursor_push_reserved(
        cursor, callee, arguments, arity, child_mark, callee_continuation);
    if (arguments != inline_arguments)
        free(arguments);
    if (step->kind == PREPARED_PURE_STEP_TAIL)
        cursor->tail_calls++;
    else
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_ANSWER_PRODUCER_CONTINUATION_CALL);
    if (!prepared_pure_answer_cursor_see_head(cursor, callee)) {
        (void)prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    return PREPARED_PURE_RUN_CONTINUE;
}

/* Run a step program from a matched equation's first step, or deliver a
 * value to `continuation`, until an answer reaches the consumer, a call is
 * entered, or no answer remains on this path.  Each resumption starts from
 * the continuation's own copy of the caller's locals. */
static PreparedPureRunResult prepared_pure_answer_cursor_run(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t ordinal, bool matched_before, ArenaMark step_mark,
    uint32_t equation_index, const PreparedPureContinuation *continuation,
    Atom *delivered, Atom **answer_out) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureEquation *body = NULL;
    uint32_t equation = equation_index;
    uint32_t pc = 0u;
    Atom *value = delivered;
    ArenaMark value_mark = step_mark;
    bool limit_hit = false;
    if (!value) {
        body = &program->equations[equation];
        if (!prepared_pure_reserve(
                (void **)&program->slots, sizeof(*program->slots),
                &program->slot_cap, body->frame_slot_count))
            goto unsupported;
        if (body->local_count > 0u)
            memcpy(program->slots, program->match_values,
                   sizeof(*program->slots) * body->local_count);
        if (body->frame_slot_count > body->local_count)
            memset(&program->slots[body->local_count], 0,
                   sizeof(*program->slots) *
                       (body->frame_slot_count - body->local_count));
        program->slot_len = body->frame_slot_count;
        pc = body->first_step;
    }
    for (;;) {
        if (value) {
            if (!continuation) {
                cursor->answers++;
                cursor->undo_valid = false;
                if (cursor->owns_arena) {
                    cursor->answer_pending = true;
                    cursor->answer_mark = value_mark;
                }
                *answer_out = value;
                return PREPARED_PURE_RUN_ANSWER;
            }
            equation = continuation->equation;
            if (equation >= program->equation_len)
                goto unsupported;
            body = &program->equations[equation];
            if (continuation->slot >= body->frame_slot_count ||
                !prepared_pure_reserve(
                    (void **)&program->slots, sizeof(*program->slots),
                    &program->slot_cap, body->frame_slot_count))
                goto unsupported;
            memcpy(program->slots, continuation->locals,
                   sizeof(*program->slots) * body->frame_slot_count);
            program->slots[continuation->slot] = value;
            program->slot_len = body->frame_slot_count;
            pc = continuation->step;
            continuation = continuation->parent;
            value = NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_ANSWER_PRODUCER_RESUMPTION);
            continue;
        }
        if (pc < body->first_step ||
            pc - body->first_step >= body->step_count)
            goto unsupported;
        const PreparedPureStep *step = &program->steps[pc];
        switch (step->kind) {
        case PREPARED_PURE_STEP_EVAL:
        case PREPARED_PURE_STEP_RETURN: {
            ArenaMark mark = arena_mark(cursor->arena);
            Atom *result = prepared_pure_answer_step_value(
                cursor, step, body->frame_slot_count, &limit_hit);
            if (!result || atom_has_vars(result) ||
                atom_has_thread_local_resource(result))
                goto unsupported;
            if (step->kind == PREPARED_PURE_STEP_RETURN) {
                value = result;
                value_mark = mark;
                break;
            }
            if (step->slot >= body->frame_slot_count)
                goto unsupported;
            program->slots[step->slot] = result;
            pc++;
            break;
        }
        case PREPARED_PURE_STEP_MATCH: {
            if (step->slot >= body->frame_slot_count ||
                step->auxiliary >= program->bind_pattern_len ||
                !program->slots[step->slot])
                goto unsupported;
            PreparedPureMatchState matched =
                prepared_pure_match_bind_pattern(
                    program, &program->bind_patterns[step->auxiliary], 0u,
                    program->slots[step->slot]);
            if (matched == PREPARED_PURE_MATCH_MISMATCH)
                return PREPARED_PURE_RUN_CONTINUE;
            if (matched != PREPARED_PURE_MATCH_MATCHED)
                goto unsupported;
            pc++;
            break;
        }
        case PREPARED_PURE_STEP_BRANCH: {
            Atom *condition = step->slot < body->frame_slot_count
                ? program->slots[step->slot] : NULL;
            if (condition && prepared_pure_is_true(condition))
                pc++;
            else if (condition &&
                     (prepared_pure_is_false(condition) ||
                      prepared_pure_petta_else_value(condition)))
                pc = step->target;
            else
                goto unsupported;
            break;
        }
        case PREPARED_PURE_STEP_JUMP:
            pc = step->target;
            break;
        case PREPARED_PURE_STEP_FAIL:
            return PREPARED_PURE_RUN_CONTINUE;
        case PREPARED_PURE_STEP_CALL:
        case PREPARED_PURE_STEP_TAIL:
            return prepared_pure_answer_cursor_enter(
                cursor, frame_index, ordinal, matched_before, step_mark,
                equation, pc, step, continuation);
        default:
            goto unsupported;
        }
    }
unsupported:
    if (prepared_pure_debug_enabled())
        fprintf(stderr, "prepared-pure step program stops at step %u (%s)\n",
                pc, body && pc - body->first_step < body->step_count
                    ? (const char *[]){"eval", "call", "tail", "match",
                                       "branch", "jump", "return", "fail"}
                          [program->steps[pc].kind]
                    : "resumption");
    prepared_pure_answer_cursor_restore_step(
        cursor, frame_index, ordinal, matched_before, step_mark);
    (void)prepared_pure_answer_cursor_handoff(
        cursor, limit_hit ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                          : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
    return PREPARED_PURE_RUN_HANDOFF;
}

CettaPreparedPureCursorStep cetta_prepared_pure_answer_cursor_next(
    CettaPreparedPureAnswerCursor *cursor, Atom **answer_out) {
    if (answer_out)
        *answer_out = NULL;
    if (!cursor || !answer_out)
        return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
    if (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE)
        return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
    cursor->undo_valid = false;
    if (cursor->answer_pending) {
        arena_reset(cursor->arena, cursor->answer_mark);
        cursor->answer_pending = false;
    }
    CettaPreparedPureProgram *program = cursor->program;
    if (!space_program_token_is_current(program->source_program))
        return prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_STALE);

    while (cursor->frame_len > 0u) {
        if (cursor->interrupt_poll && --cursor->poll_countdown == 0u) {
            cursor->poll_countdown = cursor->poll_interval;
            if (cursor->interrupt_poll(cursor->interrupt_context))
                return prepared_pure_answer_cursor_handoff(
                    cursor, CETTA_PREPARED_PURE_HANDOFF_INTERRUPT);
        }
        if (cursor->remaining_transitions == 0u)
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
        cursor->remaining_transitions--;
        size_t frame_index = cursor->frame_len - 1u;
        PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
        if (frame->head_index >= program->head_len ||
            frame->argument_base > cursor->argument_len ||
            frame->arity > cursor->argument_len - frame->argument_base)
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        const PreparedPureHead *head =
            &program->heads[frame->head_index];
        if (frame->next_equation >= head->equation_count) {
            /* A dialect that reduces an unmatched call to itself still
             * type-checks a call of a head with declared types; that call is
             * left to it. */
            if (!frame->matched_equation &&
                (cursor->unmatched_call ==
                     CETTA_PREPARED_PURE_UNMATCHED_DECLINES ||
                 (cursor->unmatched_call ==
                      CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF &&
                  head->declares_type)))
                return prepared_pure_answer_cursor_handoff(
                    cursor, CETTA_PREPARED_PURE_HANDOFF_NO_MATCH);
            if (!frame->matched_equation &&
                cursor->unmatched_call ==
                    CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF) {
                /* The call's one answer is itself; afterwards the call is
                 * finished like any other. */
                frame->matched_equation = true;
                ArenaMark self_mark = cursor->owns_arena
                    ? arena_mark(cursor->arena) : (ArenaMark){0};
                size_t live_before = arena_accounted_live_bytes(cursor->arena);
                Atom *self = prepared_pure_answer_cursor_call_atom(
                    cursor, frame_index);
                if (!self || !prepared_pure_answer_cursor_charge(
                        cursor, live_before)) {
                    prepared_pure_answer_cursor_restore_step(
                        cursor, frame_index, frame->next_equation, false,
                        self_mark);
                    return prepared_pure_answer_cursor_handoff(
                        cursor, self ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                                     : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
                }
                if (!frame->continuation) {
                    cursor->answers++;
                    cursor->undo_valid = false;
                    if (cursor->owns_arena) {
                        cursor->answer_pending = true;
                        cursor->answer_mark = self_mark;
                    }
                    *answer_out = self;
                    return CETTA_PREPARED_PURE_CURSOR_ANSWER;
                }
                PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                    cursor, frame_index, frame->next_equation, false,
                    self_mark, 0u, frame->continuation, self, answer_out);
                if (run == PREPARED_PURE_RUN_CONTINUE)
                    continue;
                return run == PREPARED_PURE_RUN_ANSWER
                    ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                    : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
            }
            cursor->argument_len = frame->argument_base;
            cursor->frame_len--;
            if (cursor->owns_arena)
                arena_reset(cursor->arena, frame->mark);
            continue;
        }
        /* What this call's earlier equations built is dead. */
        if (cursor->owns_arena)
            arena_reset(cursor->arena, frame->alternative_mark);
        uint32_t ordinal = frame->next_equation;
        bool matched_before = frame->matched_equation;
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + ordinal];
        frame->next_equation++;
        if (equation->arity != frame->arity)
            continue;
        uint64_t ready_arguments = frame->arity == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << frame->arity) - UINT64_C(1);
        uint32_t demanded_argument = 0u;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
        PreparedPureMatchState matched = prepared_pure_match_equation(
            program, equation, &cursor->arguments[frame->argument_base],
            frame->arity, ready_arguments, &demanded_argument);
        if (matched == PREPARED_PURE_MATCH_MISMATCH)
            continue;
        if (matched != PREPARED_PURE_MATCH_MATCHED) {
            frame->next_equation = ordinal;
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }
        frame->matched_equation = true;
        PreparedPureGuardState guard =
            prepared_pure_evaluate_scalar_guard(program, equation);
        if (guard == PREPARED_PURE_GUARD_REFUTED)
            continue;
        ArenaMark step_mark = cursor->owns_arena
            ? arena_mark(cursor->arena) : (ArenaMark){0};
        if (guard != PREPARED_PURE_GUARD_ACCEPTED) {
            prepared_pure_answer_cursor_restore_step(
                cursor, frame_index, ordinal, matched_before, step_mark);
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }

        if (!equation->tail_only) {
            PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                cursor, frame_index, ordinal, matched_before, step_mark,
                head->first_equation + ordinal, frame->continuation, NULL,
                answer_out);
            if (run == PREPARED_PURE_RUN_CONTINUE)
                continue;
            return run == PREPARED_PURE_RUN_ANSWER
                ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
        }

        const PreparedPureNode *root = &program->nodes[equation->root];
        size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
        bool limit_hit = false;
        if (root->kind != PREPARED_PURE_CALL) {
            Atom *answer = NULL;
            if (!prepared_pure_project_answer_value(
                    program, cursor->arena, program->match_values,
                    equation->local_count, equation->root, 0u,
                    &cursor->limits, &scratch, &limit_hit, &answer) ||
                !answer || atom_has_vars(answer) ||
                atom_has_thread_local_resource(answer)) {
                prepared_pure_answer_cursor_restore_step(
                    cursor, frame_index, ordinal, matched_before,
                    step_mark);
                return prepared_pure_answer_cursor_handoff(
                    cursor, limit_hit
                        ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                        : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
            }
            if (!cursor->owns_arena)
                cursor->scratch_remaining = scratch;
            if (frame->continuation) {
                PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                    cursor, frame_index, ordinal, matched_before, step_mark,
                    0u, frame->continuation, answer, answer_out);
                if (run == PREPARED_PURE_RUN_CONTINUE)
                    continue;
                return run == PREPARED_PURE_RUN_ANSWER
                    ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                    : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
            }
            cursor->answers++;
            cursor->undo_valid = true;
            cursor->undo_frame = frame_index;
            cursor->undo_next_equation = ordinal;
            cursor->undo_matched_equation = matched_before;
            if (cursor->owns_arena) {
                cursor->answer_pending = true;
                cursor->answer_mark = step_mark;
            }
            *answer_out = answer;
            return CETTA_PREPARED_PURE_CURSOR_ANSWER;
        }

        enum { PREPARED_PURE_INLINE_TAIL_ARGUMENTS = 16u };
        Atom *inline_tail_arguments[PREPARED_PURE_INLINE_TAIL_ARGUMENTS];
        uint32_t callee = root->auxiliary;
        uint32_t arity = root->child_count;
        Atom **tail_arguments = arity <= PREPARED_PURE_INLINE_TAIL_ARGUMENTS
            ? inline_tail_arguments
            : malloc(sizeof(*tail_arguments) * arity);
        bool projected = tail_arguments != NULL &&
            callee < program->head_len;
        for (uint32_t child = 0u; projected && child < arity; child++) {
            projected = prepared_pure_project_answer_value(
                    program, cursor->arena, program->match_values,
                    equation->local_count,
                    program->children[root->first_child + child],
                    0u, &cursor->limits, &scratch, &limit_hit,
                    &tail_arguments[child]) &&
                tail_arguments[child] &&
                !atom_has_vars(tail_arguments[child]) &&
                !atom_has_thread_local_resource(tail_arguments[child]);
        }
        bool reserved = projected &&
            prepared_pure_answer_cursor_reserve(cursor, arity);
        if (!reserved) {
            if (tail_arguments != inline_tail_arguments)
                free(tail_arguments);
            prepared_pure_answer_cursor_restore_step(
                cursor, frame_index, ordinal, matched_before, step_mark);
            return prepared_pure_answer_cursor_handoff(
                cursor, limit_hit || projected
                    ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                    : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }
        /* Reservation can move the frame array. */
        frame = &cursor->frames[frame_index];
        ArenaMark child_mark = step_mark;
        const PreparedPureContinuation *inherited = frame->continuation;
        if (frame->next_equation >= head->equation_count) {
            /* Last call: the caller has no alternative left. */
            child_mark = frame->mark;
            cursor->argument_len = frame->argument_base;
            cursor->frame_len--;
        }
        prepared_pure_answer_cursor_push_reserved(
            cursor, callee, tail_arguments, arity, child_mark, inherited);
        if (tail_arguments != inline_tail_arguments)
            free(tail_arguments);
        if (!cursor->owns_arena)
            cursor->scratch_remaining = scratch;
        cursor->tail_calls++;
        /* The call is on the frontier either way; a relation its consumer
         * does not admit is never stepped here. */
        if (!prepared_pure_answer_cursor_see_head(cursor, callee))
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
    }
    return CETTA_PREPARED_PURE_CURSOR_EXHAUSTED;
}

bool cetta_prepared_pure_answer_cursor_detach(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return false;
    if (!cursor->owns_arena || cursor->detached)
        return true;
    /* A continuation's locals are not frontier arguments; a consumer that
     * detaches never opens a program with continuations. */
    if (cursor->program->continuation_steps)
        return false;
    /* The borrowed answer's storage is about to lie below live values. */
    cursor->answer_pending = false;
    cursor->undo_valid = false;
    AtomDeepCopySession *session = atom_deep_copy_session_new(cursor->arena);
    bool ok = session != NULL;
    for (size_t index = 0u; ok && index < cursor->argument_len; index++) {
        Atom *copy = atom_deep_copy_session_copy(
            session, cursor->arguments[index]);
        if (copy)
            cursor->arguments[index] = copy;
        else
            ok = false;
    }
    if (session)
        atom_deep_copy_session_free(session);
    /* Copies sit above every call's position, so no finished call may
     * reclaim below them, whether or not all values were copied. */
    ArenaMark mark = arena_mark(cursor->arena);
    for (size_t index = 0u; index < cursor->frame_len; index++) {
        cursor->frames[index].mark = mark;
        cursor->frames[index].alternative_mark = mark;
    }
    cursor->detached = ok;
    return ok;
}

bool cetta_prepared_pure_answer_cursor_unyield(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor || !cursor->undo_valid ||
        cursor->undo_frame >= cursor->frame_len)
        return false;
    PreparedPureAnswerFrame *frame = &cursor->frames[cursor->undo_frame];
    frame->next_equation = cursor->undo_next_equation;
    frame->matched_equation = cursor->undo_matched_equation;
    cursor->undo_valid = false;
    if (cursor->answer_pending) {
        arena_reset(cursor->arena, cursor->answer_mark);
        cursor->answer_pending = false;
    }
    cursor->answers--;
    return true;
}

CettaPreparedPureHandoffReason cetta_prepared_pure_answer_cursor_handoff_reason(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->handoff : CETTA_PREPARED_PURE_HANDOFF_NONE;
}

SpaceProgramToken cetta_prepared_pure_answer_cursor_program_token(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->program->source_program
                  : (SpaceProgramToken){0};
}

size_t cetta_prepared_pure_answer_cursor_head_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->seen_head_len : 0u;
}

SymbolId cetta_prepared_pure_answer_cursor_head(
    const CettaPreparedPureAnswerCursor *cursor, size_t index) {
    return cursor && index < cursor->seen_head_len
        ? cursor->seen_heads[index] : SYMBOL_ID_NONE;
}

size_t cetta_prepared_pure_answer_cursor_frame_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->frame_len : 0u;
}

bool cetta_prepared_pure_answer_cursor_frame(
    const CettaPreparedPureAnswerCursor *cursor, size_t index,
    CettaPreparedPureAnswerFrame *frame_out) {
    if (!cursor || !frame_out || index >= cursor->frame_len)
        return false;
    const CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureAnswerFrame *frame = &cursor->frames[index];
    if (frame->head_index >= program->head_len)
        return false;
    const PreparedPureHead *head = &program->heads[frame->head_index];
    *frame_out = (CettaPreparedPureAnswerFrame){
        .head = head->head,
        .arguments = &cursor->arguments[frame->argument_base],
        .arity = frame->arity,
        .next_ordinal = frame->next_equation,
        .equation_count = head->equation_count,
    };
    if (frame->next_equation < head->equation_count) {
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + frame->next_equation];
        frame_out->next_equation = equation->equation;
        frame_out->next_logical_index = equation->logical_index;
    }
    frame_out->resumes_continuation = frame->continuation != NULL;
    return true;
}

/* Decoding a resume template back into source syntax.  Slot values are
 * imported into the destination arena; a slot not yet computed at the call
 * (a pattern variable bound later, for example) becomes one fresh variable
 * shared by every read of that slot. */
typedef struct {
    const CettaPreparedPureProgram *program;
    Arena *arena;
    Atom **values;
    Atom **fresh;
    uint32_t slot_count;
    CettaPreparedPureImportValueFn import_value;
    void *import_context;
} PreparedPureDecode;

static Atom *prepared_pure_decode_slot(PreparedPureDecode *decode, uint32_t slot) {
    if (slot >= decode->slot_count)
        return NULL;
    if (decode->values[slot])
        return decode->values[slot];
    if (!decode->fresh[slot])
        decode->fresh[slot] = atom_var_with_id(
            decode->arena, "__resume", fresh_var_id());
    return decode->fresh[slot];
}

static Atom *prepared_pure_decode_pattern(
    PreparedPureDecode *decode, const PreparedPureBindPattern *pattern,
    Atom *atom, uint32_t depth) {
    if (!atom || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        const CettaPreparedPureProgram *program = decode->program;
        for (uint32_t index = 0u; index < pattern->var_count; index++) {
            const PreparedPureBindVar *var =
                &program->bind_vars[pattern->first_var + index];
            if (var->var == atom->var_id)
                return prepared_pure_decode_slot(decode, var->slot);
        }
        return NULL;
    }
    if (atom->kind != ATOM_EXPR)
        return decode->import_value(decode->import_context, atom);
    CettaExprLen len = atom->expr.len;
    Atom **elements = malloc(sizeof(*elements) * (len ? len : 1u));
    if (!elements)
        return NULL;
    bool ok = true;
    for (CettaExprLen index = 0u; ok && index < len; index++) {
        elements[index] = prepared_pure_decode_pattern(
            decode, pattern, atom->expr.elems[index], depth + 1u);
        ok = elements[index] != NULL;
    }
    Atom *result = ok ? atom_expr(decode->arena, elements, len) : NULL;
    free(elements);
    return result;
}

static Atom *prepared_pure_decode_node(
    PreparedPureDecode *decode, uint32_t node_index, uint32_t depth) {
    const CettaPreparedPureProgram *program = decode->program;
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return NULL;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return NULL;
    const uint32_t *children = &program->children[node->first_child];
    Atom *head = NULL;
    uint32_t offset = 0u;
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom
            ? decode->import_value(decode->import_context, node->atom)
            : NULL;
    case PREPARED_PURE_SLOT:
        return prepared_pure_decode_slot(decode, node->auxiliary);
    case PREPARED_PURE_ZERO: {
        Atom *empty = atom_symbol(decode->arena, "empty");
        return empty ? atom_expr(decode->arena, &empty, 1u) : NULL;
    }
    case PREPARED_PURE_BUILD:
        break;
    case PREPARED_PURE_CALL:
        head = atom_symbol_id(decode->arena, node->head);
        offset = 1u;
        break;
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_INTRINSIC:
        head = node->atom
            ? decode->import_value(decode->import_context, node->atom)
            : NULL;
        offset = 1u;
        break;
    case PREPARED_PURE_IF:
        if (node->child_count != 3u)
            return NULL;
        head = atom_symbol(decode->arena, "if");
        offset = 1u;
        break;
    case PREPARED_PURE_BIND: {
        if (node->child_count != 2u ||
            node->auxiliary >= program->bind_pattern_len)
            return NULL;
        const PreparedPureBindPattern *pattern =
            &program->bind_patterns[node->auxiliary];
        Atom *elements[4] = {
            atom_symbol(decode->arena, "let"),
            prepared_pure_decode_pattern(decode, pattern, pattern->pattern, 0u),
            prepared_pure_decode_node(decode, children[0], depth + 1u),
            prepared_pure_decode_node(decode, children[1], depth + 1u),
        };
        for (size_t index = 0u; index < 4u; index++)
            if (!elements[index])
                return NULL;
        return atom_expr(decode->arena, elements, 4u);
    }
    default:
        /* No source reading: entry arguments, delayed slots, observers. */
        return NULL;
    }
    if (offset > 0u && !head)
        return NULL;
    uint32_t len = node->child_count + offset;
    Atom **elements = malloc(sizeof(*elements) * (len ? len : 1u));
    if (!elements)
        return NULL;
    if (offset > 0u)
        elements[0] = head;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < node->child_count; index++) {
        elements[index + offset] =
            prepared_pure_decode_node(decode, children[index], depth + 1u);
        ok = elements[index + offset] != NULL;
    }
    Atom *result = ok ? atom_expr(decode->arena, elements, len) : NULL;
    free(elements);
    return result;
}

bool cetta_prepared_pure_answer_cursor_frame_resumption(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, Arena *arena,
    Atom *hole, CettaPreparedPureImportValueFn import_value,
    void *import_context, Atom **term_out) {
    if (term_out)
        *term_out = NULL;
    if (!cursor || !arena || !hole || !import_value || !term_out ||
        index >= cursor->frame_len)
        return false;
    const CettaPreparedPureProgram *program = cursor->program;
    Atom *term = hole;
    for (const PreparedPureContinuation *record =
             cursor->frames[index].continuation;
         record; record = record->parent) {
        if (record->equation >= program->equation_len)
            return false;
        const PreparedPureEquation *body = &program->equations[record->equation];
        uint32_t call = record->step - 1u;
        if (record->step == 0u || call < body->first_step ||
            call - body->first_step >= body->step_count)
            return false;
        const PreparedPureStep *step = &program->steps[call];
        uint32_t slots = body->frame_slot_count;
        if (step->kind != PREPARED_PURE_STEP_CALL ||
            step->resume_template == PREPARED_PURE_NO_TEMPLATE ||
            record->slot >= slots)
            return false;
        Atom **values = calloc(slots ? slots : 1u, sizeof(*values));
        Atom **fresh = calloc(slots ? slots : 1u, sizeof(*fresh));
        bool ok = values && fresh;
        for (uint32_t slot = 0u; ok && slot < slots; slot++) {
            if (slot == record->slot)
                values[slot] = term;
            else if (record->locals[slot]) {
                values[slot] = import_value(import_context, record->locals[slot]);
                ok = values[slot] != NULL;
            }
        }
        PreparedPureDecode decode = {
            .program = program, .arena = arena, .values = values,
            .fresh = fresh, .slot_count = slots,
            .import_value = import_value, .import_context = import_context,
        };
        Atom *outer = ok
            ? prepared_pure_decode_node(&decode, step->resume_template, 0u)
            : NULL;
        free(values);
        free(fresh);
        if (!outer)
            return false;
        term = outer;
    }
    if (term == hole)
        return false;
    *term_out = term;
    return true;
}

uint64_t cetta_prepared_pure_answer_cursor_answer_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->answers : 0u;
}

uint64_t cetta_prepared_pure_answer_cursor_tail_call_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->tail_calls : 0u;
}

void cetta_prepared_pure_answer_cursor_close(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return;
    free(cursor->frames);
    free(cursor->arguments);
    free(cursor->head_seen);
    free(cursor->seen_heads);
    if (cursor->owns_arena)
        arena_free(&cursor->owned_arena);
    cetta_prepared_pure_program_free(cursor->program);
    free(cursor);
}

CettaPreparedPureAnswersResult
cetta_prepared_pure_program_visit_closed_answers(
    CettaPreparedPureProgram *program, Arena *arena,
    const CettaPreparedPureAnswerLimits *limits,
    CettaPreparedPureUnmatchedCall unmatched_call,
    CettaPreparedPureAnswerVisitorFn visitor, void *visitor_context,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, uint64_t *answer_count_out,
    uint64_t *tail_call_count_out) {
    if (answer_count_out)
        *answer_count_out = 0u;
    if (tail_call_count_out)
        *tail_call_count_out = 0u;
    if (!limits || !visitor)
        return CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    /* Every answer is visited before the next step, and nothing escapes a
     * run that stops short of completion. */
    CettaPreparedPureAnswerCursorOptions options = {
        .arena = arena,
        .limits = *limits,
        .unmatched_call = unmatched_call,
        .interrupt_poll = interrupt_poll,
        .interrupt_context = interrupt_context,
        .interrupt_poll_interval = 1u,
        .allow_continuations = true,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(program, &options);
    if (!cursor)
        return CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    CettaPreparedPureAnswersResult result =
        CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    for (;;) {
        Atom *answer = NULL;
        CettaPreparedPureCursorStep step =
            cetta_prepared_pure_answer_cursor_next(cursor, &answer);
        if (step == CETTA_PREPARED_PURE_CURSOR_ANSWER) {
            if (visitor(answer, visitor_context))
                continue;
            result = CETTA_PREPARED_PURE_ANSWERS_STOPPED;
        } else if (step == CETTA_PREPARED_PURE_CURSOR_EXHAUSTED) {
            result = CETTA_PREPARED_PURE_ANSWERS_COMPLETE;
        } else if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_LIMIT) {
            result = CETTA_PREPARED_PURE_ANSWERS_LIMIT;
        } else if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_INTERRUPT) {
            result = CETTA_PREPARED_PURE_ANSWERS_STOPPED;
        }
        break;
    }
    if (answer_count_out)
        *answer_count_out = cursor->answers;
    if (tail_call_count_out)
        *tail_call_count_out = cursor->tail_calls;
    cetta_prepared_pure_answer_cursor_close(cursor);
    return result;
}

/* `step_node`, when not PREPARED_PURE_RUNTIME_NODE, evaluates that node of an
 * answer producer's step program over the step_slot_count locals already
 * installed at the base of program->slots. */
static bool PREPARED_PURE_HOT prepared_pure_program_execute_internal(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom *runtime_expression,
    uint32_t step_node, uint32_t step_slot_count,
    bool closed,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context,
    Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    bool step = step_node != PREPARED_PURE_RUNTIME_NODE;
    if (!program || !arena || !result_out ||
        program->closed_program != closed ||
        (!closed && (!accumulator || !item)) ||
        (!closed && runtime_expression) ||
        (runtime_expression && atom_has_vars(runtime_expression)) ||
        (step && (runtime_expression || !closed ||
                  step_node >= program->node_len ||
                  step_slot_count > program->slot_cap)) ||
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
    if (step) {
        program->slot_len = step_slot_count;
    } else {
        program->slot_len = program->root_local_count;
        if (program->slot_len > 0u)
            memset(program->slots, 0,
                   sizeof(*program->slots) * program->slot_len);
    }
    if (!closed) {
        program->slots[program->accumulator_slot] = accumulator;
        program->slots[program->item_slot] = item;
    }
    bool pushed_root = step
        ? prepared_pure_push_frame(program, step_node, 0u)
        : runtime_expression
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
                            CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY ||
                        state == CETTA_PREPARED_PURE_EXPRESSION_ZERO) {
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
                else if (prepared_pure_is_false(condition) ||
                         prepared_pure_petta_else_value(condition))
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
                          program, arena, instruction, result_kind, 0u,
                          &program->values[frame->value_base], arity)
                    : prepared_pure_execute_intrinsic(
                          program, arena, intrinsic_instruction, head,
                          &program->values[frame->value_base], arity);
                if (!result && is_register)
                    result = prepared_pure_execute_register_intrinsic(
                        program, arena, result_kind, head,
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
                    if (!prepared_pure_push_runtime_frame(program, source))
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
        /* A let body's value and a selected branch's value are the value of
         * the form itself, so the body or branch takes over the form's frame
         * rather than returning through it. */
        if (node->kind == PREPARED_PURE_BIND) {
            if (node->auxiliary >= program->bind_pattern_len)
                return prepared_pure_runtime_decline(
                    program, "let pattern descriptor is invalid", node);
            Atom *value = NULL;
            if (frame->state == 0u) {
                value = prepared_pure_inline_child(
                    program, arena, node, 0u, frame->local_base);
                if (!value) {
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_frame(
                            program, program->children[node->first_child],
                            frame->local_base))
                        return prepared_pure_runtime_decline(
                            program, "cannot push let binding", node);
                    continue;
                }
            } else {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "let binding produced wrong arity", node);
                value = program->values[--program->value_len];
            }
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
            *frame = (PreparedPureFrame){
                .node = program->children[node->first_child + 1u],
                .local_base = frame->local_base,
                .memo_index = PREPARED_PURE_NO_MEMO,
            };
            continue;
        }
        if (node->kind == PREPARED_PURE_IF) {
            PreparedPureTruth truth = PREPARED_PURE_TRUTH_UNAVAILABLE;
            if (frame->state == 0u) {
                truth = prepared_pure_inline_truth(
                    program, arena, node, frame->local_base);
                if (truth == PREPARED_PURE_TRUTH_UNAVAILABLE) {
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_frame(
                            program, program->children[node->first_child],
                            frame->local_base))
                        return prepared_pure_runtime_decline(
                            program, "cannot push branch condition", node);
                    continue;
                }
            } else {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "branch condition produced wrong arity",
                        node);
                Atom *condition = program->values[--program->value_len];
                truth = prepared_pure_is_true(condition)
                    ? PREPARED_PURE_TRUTH_TRUE
                    : prepared_pure_is_false(condition) ||
                        prepared_pure_petta_else_value(condition)
                        ? PREPARED_PURE_TRUTH_FALSE
                        : PREPARED_PURE_TRUTH_NOT_BOOLEAN;
            }
            if (truth == PREPARED_PURE_TRUTH_NOT_BOOLEAN)
                return prepared_pure_runtime_decline(
                    program, "non-boolean branch condition", node);
            uint32_t branch = truth == PREPARED_PURE_TRUTH_TRUE ? 1u : 2u;
            *frame = (PreparedPureFrame){
                .node = program->children[node->first_child + branch],
                .local_base = frame->local_base,
                .memo_index = PREPARED_PURE_NO_MEMO,
            };
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
            prepared_pure_finish_children(program, arena, frame, node);
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
                arena,
                &program->values[frame->value_base],
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
                      arena,
                      operand->kind == ATOM_EXPR)
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
                node->undefined_type_operands,
                &program->values[frame->value_base], node->child_count);
            if (!result)
                result = prepared_pure_execute_register_intrinsic(
                    program, arena, node->result_kind, node->atom,
                    &program->values[frame->value_base], node->child_count);
            program->value_len = frame->value_base;
            if (!result ||
                (result->kind == ATOM_EXPR && atom_is_error(result)) ||
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
    if (!runtime_expression && !step &&
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
        program, arena, accumulator, item, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, false, 0u,
        NULL, NULL, result_out);
}

bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, accumulator, item, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, false, 0u,
        interrupt_poll, interrupt_context, result_out);
}

bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, true,
        nursery_budget_bytes, NULL, NULL, result_out);
}

bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, true,
        nursery_budget_bytes, interrupt_poll, interrupt_context,
        result_out);
}

bool cetta_prepared_pure_program_execute_closed_expression_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *expression, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, expression,
        PREPARED_PURE_RUNTIME_NODE, 0u, true,
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

size_t cetta_prepared_pure_program_equation_count(
    const CettaPreparedPureProgram *program) {
    return program ? program->equation_len : 0u;
}

bool cetta_prepared_pure_program_equation_signature(
    const CettaPreparedPureProgram *program, size_t index,
    SymbolId *head_out, uint32_t *arity_out) {
    if (!program || !head_out || !arity_out ||
        index >= program->equation_len)
        return false;
    const PreparedPureEquation *equation = &program->equations[index];
    if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
        equation->lhs->expr.len == 0u ||
        equation->lhs->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *head_out = equation->lhs->expr.elems[0]->sym_id;
    *arity_out = equation->arity;
    return true;
}

uint64_t cetta_prepared_pure_program_host_stamp(
    const CettaPreparedPureProgram *program) {
    return program ? program->host_stamp : 0u;
}

void cetta_prepared_pure_program_set_host_stamp(
    CettaPreparedPureProgram *program, uint64_t stamp) {
    if (program)
        program->host_stamp = stamp;
}

CettaPreparedPureProgram *cetta_prepared_pure_program_retain(
    CettaPreparedPureProgram *program) {
    if (!program || program->references == 0u ||
        program->references == UINT32_MAX)
        return NULL;
    program->references++;
    return program;
}

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    if (program->references > 1u) {
        program->references--;
        return;
    }
    free(program->nodes);
    free(program->entry_arguments);
    free(program->children);
    free(program->heads);
    free(program->head_buckets);
    free(program->callable_buckets);
    free(program->equations);
    free(program->scalar_guards);
    free(program->scalar_guard_arguments);
    for (size_t index = 0u; index < program->decision_len; index++)
        cetta_match_decision_free(program->decisions[index].selector);
    free(program->decisions);
    free(program->pattern_vars);
    free(program->bind_patterns);
    free(program->bind_vars);
    free(program->live_slots);
    free(program->argument_slots);
    free(program->match_ops);
    free(program->steps);
    free(program->step_arguments);
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
