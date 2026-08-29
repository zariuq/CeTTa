#include "petta_search_machine.h"

#include "grounded.h"
#include "match.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "petta_typecheck_census.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"
#include "variant_shape.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PETTA_MACHINE_HEAP_WINDOW_BYTES \
    ((size_t)8u * 1024u * 1024u)
#define PETTA_MACHINE_BINDING_WINDOW_ENTRIES ((size_t)4096u)
#define PETTA_MACHINE_CHOICE_BINDING_MIN_WINDOW_ENTRIES ((size_t)4096u)
#define PETTA_TABLE_ENTRY_NONE ((size_t)SIZE_MAX)
#define PETTA_MEMO_FREQUENCY_SKETCH_SIZE ((size_t)8192u)

static _Atomic uint64_t g_petta_machine_instance_counter = 1u;

static const CettaVariantShapeOptions kPettaTableVariantOptions = {
    .slot_policy = CETTA_VARIANT_SLOT_ORDINAL_NAME,
    .slot_name = "$P",
    .share_immutable = false,
};

typedef enum {
    PETTA_TABLE_ENTRY_NEW = 0,
    PETTA_TABLE_ENTRY_EVALUATING,
    PETTA_TABLE_ENTRY_COMPLETE,
} PettaTableEntryState;

typedef struct {
    Atom *key;
    /* Lookup policy may normalize a key (for example decimal floats), while
     * the generator must still evaluate the exact first authored call. */
    Atom *generator_key;
    uint64_t hash;
    Atom **answers;
    size_t answer_len;
    size_t answer_cap;
    PettaTableEntryState state;
    size_t tarjan_index;
    size_t tarjan_lowlink;
    SymbolId memo_head;
    CettaExprLen memo_arity;
    uint64_t access_tick;
    uint64_t retained_bytes;
    bool tarjan_on_stack;
    bool self_edge;
    bool memoized;
} PettaTableEntry;

struct PettaMachineTable {
    Arena arena;
    PettaTableEntry *entries;
    size_t entry_len;
    size_t entry_cap;
    size_t *slots;
    size_t slot_cap;
    size_t *tarjan_stack;
    size_t tarjan_len;
    size_t tarjan_cap;
    size_t tarjan_next;
    uint16_t *frequency_sketch;
    uint64_t frequency_accesses;
    uint64_t access_tick;
    uint64_t memo_retained_bytes;
    uint64_t mutation_epoch;
    bool failed;
    bool root_leased;
    bool memo_policy_dirty;
};

typedef PettaMachineTable PettaTableShared;

typedef enum {
    PETTA_TABLE_CHOICE_GENERATE_INITIAL = 0,
    PETTA_TABLE_CHOICE_GENERATE_FIXPOINT,
    PETTA_TABLE_CHOICE_REPLAY,
} PettaTableChoicePhase;

typedef enum {
    PETTA_GOAL_SOLVE = 0,
    PETTA_GOAL_SOLVE_ACTIVATION,
    PETTA_GOAL_FORCE,
    PETTA_GOAL_SOLVE_COUNTED_COLLECTION,
    PETTA_GOAL_CALL_READY,
    PETTA_GOAL_CALL_READY_RESOLVED,
    PETTA_GOAL_CALL_READY_DATA,
    PETTA_GOAL_CALL_READY_COUNTED_COLLECTION,
    PETTA_GOAL_TYPE_ACCEPT,
    PETTA_GOAL_TYPE_MATCH,
    PETTA_GOAL_TYPE_ASCRIBE_READY,
    PETTA_GOAL_TYPE_REQUIRE_READY,
    PETTA_GOAL_APPLY_READY,
    PETTA_GOAL_LAMBDA_READY,
    PETTA_GOAL_PARTIAL_READY,
    PETTA_GOAL_OVERAPPLICATION_READY,
    PETTA_GOAL_BOOLEAN_READY,
    PETTA_GOAL_UNIFY,
    PETTA_GOAL_EQUAL_READY,
    PETTA_GOAL_APPEND_READY,
    PETTA_GOAL_CONS_READY,
    PETTA_GOAL_RELATIONAL_MEMBER_READY,
    PETTA_GOAL_LIST_SIZE_READY,
    PETTA_GOAL_COLLECTION_COUNT_READY,
    PETTA_GOAL_WRAP_COLLECTION_COUNT,
    PETTA_GOAL_LIST_UNARY_READY,
    PETTA_GOAL_LIST_SET_READY,
    PETTA_GOAL_SREAD_READY,
    PETTA_GOAL_MAP_ATOM_READY,
    PETTA_GOAL_FOLDL_ATOM_READY,
    PETTA_GOAL_FILTER_ATOM_READY,
    PETTA_GOAL_MATCH_SPACE_READY,
    PETTA_GOAL_PAIR_SELECT,
    PETTA_GOAL_MEMBER_READY,
    PETTA_GOAL_IF_SELECT,
    PETTA_GOAL_CASE_SELECT,
    PETTA_GOAL_TEST_COMPARE,
    PETTA_GOAL_HOST_STRICT_READY,
    PETTA_GOAL_HOST_READY,
    PETTA_GOAL_EXTENSION_READY,
    PETTA_GOAL_RELATIONAL_EXTENSION_READY,
    PETTA_GOAL_NAMED_STATE_READY,
    PETTA_GOAL_CLAUSE_ONLY,
    PETTA_GOAL_CUT,
    PETTA_GOAL_ONCE_COMMIT,
    PETTA_GOAL_TRANSACTION_COMMIT,
    PETTA_GOAL_MUTEX_RELEASE,
    PETTA_GOAL_SET_ANSWER_WEIGHT,
    PETTA_GOAL_EQUAL_COMMIT,
    PETTA_GOAL_REDUCE_READY,
    PETTA_GOAL_CATCH_READY,
} PettaGoalKind;

enum {
    PETTA_ACTIVATION_SOURCE_FIRST = 1u << 0,
    PETTA_ACTIVATION_SOURCE_SECOND = 1u << 1,
    PETTA_ACTIVATION_SOURCE_THIRD = 1u << 2,
    PETTA_ACTIVATION_SOURCE_FOURTH = 1u << 3,
};

typedef struct {
    PettaGoalKind kind;
    uint32_t barrier;
    uint64_t instance_id;
    Atom *first;
    Atom *second;
    Atom *third;
    Atom *fourth;
    const PettaPlanNode *plan;
    const PettaPlanNode *second_plan;
    const PettaPlanNode *third_plan;
    const PettaPlanNode *fourth_plan;
    const PettaEquationTemplate *activation_template;
    /* Persistent authored call source paired with activation_template and
     * the epoch boundary below.  A shallow Atom shell remains `first` for
     * generic control services; exact equation matching consumes this view. */
    Atom *equation_query_source;
    size_t choice_index;
    ChoicePoint catch_trail;
    size_t catch_type_obligation_mark;
    uint64_t binding_growth_mark;
    uint64_t answer_weight;
    /* A source occurrence may remain paired with its equation-activation
     * environment instead of being rebuilt eagerly.  Source-field bits name
     * immutable TermUniverse syntax; the other fields remain ordinary
     * machine-heap roots.  `activation_first_entry` is a logical entry-prefix
     * boundary and is translated exactly whenever binding GC compacts the
     * environment. */
    uint32_t activation_epoch;
    uint32_t activation_first_entry;
    uint8_t activation_source_fields;
    /* The first operand was fully substituted immediately before this
     * top-of-stack continuation was pushed.  Only the clause-result helpers
     * set this bit; no intervening goal can extend the logical environment. */
    bool first_operand_resolved;
} PettaGoal;

static void petta_machine_record_goal_class(
    PettaMachineStats *stats, PettaGoalKind kind) {
    if (!stats)
        return;
    switch (kind) {
    case PETTA_GOAL_SOLVE:
    case PETTA_GOAL_SOLVE_ACTIVATION:
    case PETTA_GOAL_FORCE:
    case PETTA_GOAL_SOLVE_COUNTED_COLLECTION:
        stats->solve_goal_transitions++;
        return;
    case PETTA_GOAL_CALL_READY:
    case PETTA_GOAL_CALL_READY_RESOLVED:
    case PETTA_GOAL_CALL_READY_DATA:
    case PETTA_GOAL_CALL_READY_COUNTED_COLLECTION:
    case PETTA_GOAL_APPLY_READY:
    case PETTA_GOAL_LAMBDA_READY:
    case PETTA_GOAL_PARTIAL_READY:
    case PETTA_GOAL_OVERAPPLICATION_READY:
    case PETTA_GOAL_RELATIONAL_EXTENSION_READY:
    case PETTA_GOAL_CLAUSE_ONLY:
        stats->call_goal_transitions++;
        return;
    case PETTA_GOAL_TYPE_ACCEPT:
    case PETTA_GOAL_TYPE_MATCH:
    case PETTA_GOAL_TYPE_ASCRIBE_READY:
    case PETTA_GOAL_TYPE_REQUIRE_READY:
    case PETTA_GOAL_UNIFY:
    case PETTA_GOAL_EQUAL_READY:
    case PETTA_GOAL_TEST_COMPARE:
        stats->unify_goal_transitions++;
        return;
    case PETTA_GOAL_APPEND_READY:
    case PETTA_GOAL_CONS_READY:
    case PETTA_GOAL_RELATIONAL_MEMBER_READY:
    case PETTA_GOAL_LIST_SIZE_READY:
    case PETTA_GOAL_COLLECTION_COUNT_READY:
    case PETTA_GOAL_WRAP_COLLECTION_COUNT:
    case PETTA_GOAL_LIST_UNARY_READY:
    case PETTA_GOAL_LIST_SET_READY:
    case PETTA_GOAL_SREAD_READY:
    case PETTA_GOAL_MAP_ATOM_READY:
    case PETTA_GOAL_FOLDL_ATOM_READY:
    case PETTA_GOAL_FILTER_ATOM_READY:
    case PETTA_GOAL_MATCH_SPACE_READY:
    case PETTA_GOAL_PAIR_SELECT:
    case PETTA_GOAL_MEMBER_READY:
        stats->collection_goal_transitions++;
        return;
    case PETTA_GOAL_BOOLEAN_READY:
    case PETTA_GOAL_IF_SELECT:
    case PETTA_GOAL_CASE_SELECT:
    case PETTA_GOAL_CUT:
    case PETTA_GOAL_ONCE_COMMIT:
    case PETTA_GOAL_TRANSACTION_COMMIT:
    case PETTA_GOAL_MUTEX_RELEASE:
    case PETTA_GOAL_SET_ANSWER_WEIGHT:
    case PETTA_GOAL_EQUAL_COMMIT:
    case PETTA_GOAL_REDUCE_READY:
    case PETTA_GOAL_CATCH_READY:
        stats->control_goal_transitions++;
        return;
    case PETTA_GOAL_HOST_STRICT_READY:
    case PETTA_GOAL_HOST_READY:
    case PETTA_GOAL_EXTENSION_READY:
    case PETTA_GOAL_NAMED_STATE_READY:
        stats->host_goal_transitions++;
        return;
    }
    stats->other_goal_transitions++;
}

typedef struct {
    size_t index;
    PettaGoal previous;
} PettaGoalTrailEntry;

typedef enum {
    PETTA_CHOICE_CLAUSE = 0,
    PETTA_CHOICE_OUTCOMES,
    PETTA_CHOICE_SUPERPOSE,
    PETTA_CHOICE_BOOLEAN,
    PETTA_CHOICE_APPEND,
    PETTA_CHOICE_MEMBER,
    PETTA_CHOICE_RELATIONAL_MEMBER,
    PETTA_CHOICE_LIST_LENGTH,
    PETTA_CHOICE_MATCH,
    PETTA_CHOICE_TYPED_CALL,
    PETTA_CHOICE_CASE_DEFAULT,
    PETTA_CHOICE_EQUAL_DEFAULT,
    PETTA_CHOICE_RELATIONAL_EXTENSION,
    PETTA_CHOICE_COLLAPSE,
    PETTA_CHOICE_COUNT_COLLAPSE,
    PETTA_CHOICE_ONCE,
    PETTA_CHOICE_TRANSACTION,
    PETTA_CHOICE_MUTEX,
    PETTA_CHOICE_TABLE,
} PettaChoiceKind;

typedef struct {
    PettaChoiceKind kind;
    ChoicePoint trail;
    /*
     * WAM instant reclaiming: bindings and continuation slots already roll
     * back to this choice point, so branch-local atoms allocated after the
     * same point must roll back with them.  COLLAPSE is the sole exception:
     * a suspended collapse retains already collected child answers in the
     * parent heap until the child stream completes.
     */
    ArenaMark heap_mark;
    bool heap_mark_captured;
    bool retain_heap_across_resume;
    size_t goal_height;
    size_t goal_trail_mark;
    size_t type_obligation_mark;
    size_t previous_protected_goal_height;
    uint32_t barrier;
    union {
        struct {
            PettaClauseCandidate *candidates;
            size_t equation_len;
            size_t next_equation;
            uint64_t call_occurrence;
            Atom *query;
            Atom *query_source;
            const PettaEquationTemplate *query_template;
            uint32_t query_epoch;
            uint32_t query_first_entry;
            Atom *expected;
            bool evaluate_result;
            bool translate_result;
            bool count_collection_result;
            bool equation_template_c0_closed_query;
        } clause;
        struct {
            OutcomeSet *outcomes;
            CettaCount next;
            Atom *expected;
        } outcomes;
        struct {
            Atom *items;
            CettaExprIndex next;
            Atom *expected;
            const PettaPlanNode *items_plan;
        } superpose;
        struct {
            Atom *expression;
            Atom *expected;
            uint32_t next_row;
            bool inputs_determined;
        } boolean;
        struct {
            Atom *whole;
            CettaExprIndex next_split;
            Atom *left;
            Atom *right;
        } append;
        struct {
            Atom *needle;
            Atom *items;
            Atom *expected;
            CettaExprIndex next;
            bool saw_match;
            bool emitted_false;
        } member;
        struct {
            Atom *needle;
            Atom *items;
            Atom *expected;
            uint32_t next_clause;
        } relational_member;
        struct {
            Atom *open_tail;
            Atom *expected;
            uint64_t prefix_length;
            uint64_t next_suffix_length;
        } list_length;
        struct {
            Space *space;
            SpaceReadToken read;
            CettaIndex next_index;
            Atom **snapshot;
            CettaCount snapshot_len;
            BindingSet binding_snapshot;
            bool binding_snapshot_mode;
            bool snapshot_mode;
            Atom *pattern;
            Atom *template;
            Atom *expected;
            bool terminal_count_fold;
        } match;
        struct {
            Atom **types;
            uint32_t count;
            uint32_t next;
            bool overload_dispatch;
            Atom *expression;
            Atom *expected;
            const PettaPlanNode *plan;
        } typed_call;
        struct {
            bool saw_answer;
            Atom *expression;
            Atom *expected;
            const PettaPlanNode *plan;
        } case_default;
        struct {
            bool saw_answer;
            Atom *expected;
            /* Nonzero only for a committed residual type guard.  The
             * fallback choice is the guard's trailed in-flight marker: it
             * survives backtracking among get-type alternatives and is
             * discarded when control leaves the guard's outer extent. */
            uint64_t type_obligation_id;
            Atom *guarded_value;
            Atom *guarded_formal;
            SpaceReadToken authority_read;
            uint64_t authority_epoch;
            uint32_t authority_policy;
            PettaMachineAuthorityToken authority;
        } equal_default;
        struct {
            bool fallback_started;
            Atom *expression;
            Atom *expected;
        } relational_extension;
        struct {
            PettaMachine *machine;
            Atom **items;
            size_t item_len;
            size_t item_cap;
            Atom *expected;
            bool emitted;
        } collapse;
        struct {
            PettaMachine *machine;
            Atom *expected;
            uint64_t count;
            uint64_t error_count;
            bool emitted;
            bool wrap_collection;
        } count_collapse;
        struct {
            void *handle;
            Space *previous_space;
            bool active;
        } transaction;
        struct {
            void *handle;
            bool active;
        } mutex;
        struct {
            PettaTableChoicePhase phase;
            size_t requested_entry;
            size_t parent_entry;
            size_t iteration_entry;
            size_t scc_begin;
            size_t scc_cursor;
            size_t replay_next;
            Atom *query;
            Atom *expected;
            PettaMachine *generator;
            Arena *round_arena;
            Atom *generator_query;
            CettaVarMap goal_instantiation;
            CettaVarMap generator_slots;
            Atom **round_answers;
            size_t round_len;
            size_t round_cap;
            PettaMemoAggregateMode aggregate_mode;
            uint32_t answer_limit;
            bool memoized;
            bool ground_query;
            bool aggregate_emitted;
            bool truncation_observed;
            bool pass_changed;
        } table;
    } as;
} PettaChoice;

/* A query may remain split into its atom head and argument fields while the
 * revision-pinned Space executable selects and matches equation
 * occurrences.  `whole` is required only by a semantic consumer or a saved
 * continuation. */
typedef struct {
    Atom *whole;
    Atom *head;
    Atom *const *arguments;
    CettaExprLen arity;
    Atom *source;
    const PettaEquationTemplate *source_template;
    uint32_t source_epoch;
    uint32_t source_first_entry;
} PettaSpaceQueryView;

typedef struct {
    VarId id;
    Atom *variable;
} PettaVisibleVariable;

/*
 * A source-level `(the T X)` and an open residual TYPE_ACCEPT both denote a
 * binding-time obligation, not a wrapper around X.  Keep the obligation next
 * to the machine's logical environment so term identity remains unchanged;
 * choice points trail the append-only length together with their bindings.
 */
typedef enum {
    PETTA_TYPE_OBLIGATION_UNCHECKED = 0,
    PETTA_TYPE_OBLIGATION_OPEN_VALUE,
    PETTA_TYPE_OBLIGATION_NATIVE_ESTABLISHED,
    PETTA_TYPE_OBLIGATION_DYNAMIC_COMPATIBLE,
    PETTA_TYPE_OBLIGATION_RELATIONAL_ESTABLISHED,
} PettaTypeObligationState;

typedef struct {
    /* Stable, monotonically allocated identity.  Vector positions are
     * branch-local and may be reused after rollback, so they cannot name a
     * continuation or proof receipt. */
    uint64_t id;
    Atom *value;
    Atom *formal;
    uint32_t barrier;
    /*
     * Exact resolved operands from the last examination.  Logical
     * binding growth is global to the branch, while an obligation normally
     * depends on only a few of those bindings.  Retain the authoritative
     * resolved pair so unrelated writes do not repeat the full native type
     * judgment.  Choice rollback invalidates these derived pointers before
     * resetting the nursery; deterministic GC relocates them with the other
     * obligation roots.
     */
    Atom *checked_value;
    Atom *checked_formal;
    SpaceReadToken checked_read;
    uint64_t checked_epoch;
    uint32_t checked_policy;
    PettaMachineAuthorityToken checked_authority;
    PettaTypeObligationState state;
    /* Once an authored get-type relation has established this obligation,
     * later authority changes must re-establish it relationally.  Losing the
     * classifier is not permission to weaken the same live requirement back
     * to gradual compatibility. */
    bool relational_required;
} PettaTypeObligation;

typedef struct {
    SpaceReadToken read;
    SymbolId head;
    CettaExprLen arity;
    CettaMatchDecisionMode mode;
    /* Borrowed candidate-array identity is compared only after `read` proves
     * the decision current.  Revision mismatch drops this entry before the
     * possibly freed address is considered, preventing an allocator ABA from
     * authorizing a stale decision. */
    const PettaClauseCandidate *snapshot_identity;
    Atom **equations;
    size_t equation_len;
    CettaMatchDecision *decision;
} PettaMatchDecisionCacheEntry;

/* One equation/pattern pair's nested application roots are classified once
 * for an exact Space revision and host-callability authority.  The source
 * equation and live classifier remain authoritative; this record only avoids
 * repeating the same tree walk while both authorities are unchanged. */
typedef struct {
    Atom *equation;
    const PettaSpecializerPatternNode *pattern_root;
    bool contains_callable;
} PettaEquationCallabilityCacheEntry;

#define PETTA_ACTIVATION_FRAME_CACHE_CAP 8u

typedef struct {
    BindingsDenseEpochFrame frame;
    const PettaEquationTemplate *equation_template;
    uint32_t activation_epoch;
    uint32_t activation_first_entry;
} PettaActivationFrameCacheEntry;

typedef struct {
    SymbolId query_head;
    uint64_t query_start;
    uint32_t query_limit;
    bool query_enabled;
    bool query_heads_only;
    bool choice_kind;
    bool choice;
} PettaMachineTraceConfig;

typedef struct PettaContinuationTermPool PettaContinuationTermPool;

struct PettaMachineImpl {
    uint64_t instance_id;
    Space *space;
    Arena *answer_arena;
    /*
     * Immutable atoms are allocated young and promoted into `tenured`.
     * A tenured atom may never point back into `heap`; constructors only
     * create new parents over existing children, so the invariant follows
     * from allocation order without a write barrier.
     */
    Arena heap;
    Arena tenured;
    /* Immutable term graphs restored from the controller remain in one
     * append-only pool shared by the live machine and its latent siblings.
     * Branch-local vectors and bindings retain independent ownership. */
    PettaContinuationTermPool *continuation_term_pool;
    CettaGsltGroundDenseWorkspaceV1 equation_template_c0_workspace;
    PettaActivationFrameCacheEntry
        activation_frames[PETTA_ACTIVATION_FRAME_CACHE_CAP];
    uint32_t activation_frame_replacement;
    /* Derived occurrence plans are machine-lifetime metadata, not GC atoms. */
    Arena plan_arena;
    SearchContext search;
    PettaMachineHost host;
    PettaGoal *goals;
    size_t goal_len;
    size_t goal_cap;
    size_t goal_initialized_len;
    PettaGoalTrailEntry *goal_trail;
    size_t goal_trail_len;
    size_t goal_trail_cap;
    size_t protected_goal_height;
    PettaChoice *choices;
    size_t choice_len;
    size_t choice_cap;
    PettaVisibleVariable *visible;
    size_t visible_len;
    size_t visible_cap;
    PettaTypeObligation *type_obligations;
    size_t type_obligation_len;
    size_t type_obligation_cap;
    uint64_t next_type_obligation_id;
    uint64_t type_obligation_checked_growth;
    SpaceReadToken type_obligation_checked_read;
    uint64_t type_obligation_checked_epoch;
    uint32_t type_obligation_checked_policy;
    PettaMachineAuthorityToken type_obligation_checked_authority;
    bool type_obligation_check_pending;
    PettaMatchDecisionCacheEntry *match_decisions;
    size_t match_decision_len;
    size_t match_decision_cap;
    PettaEquationCallabilityCacheEntry *equation_callability;
    size_t equation_callability_len;
    size_t equation_callability_cap;
    SpaceReadToken equation_callability_read;
    PettaMachineAuthorityToken equation_callability_authority;
    bool equation_callability_current;
    Atom *query;
    Atom *answer_variable;
    Atom *raised_error;
    bool yielded;
    bool suspended_choice;
    bool terminal;
    bool owns_table_shared;
    bool borrowed_root_table;
    bool bypass_root_table;
    bool count_only_emission;
    uint64_t pending_answer_weight;
    uint64_t last_answer_weight;
    PettaMachineStep terminal_step;
    PettaTableShared *table_shared;
    size_t table_generator;
    uint64_t next_goal_instance;
    size_t heap_collect_after;
    size_t tenured_major_after;
    uint64_t binding_growth_collect_after;
    bool first_answer_timed;
    bool equation_template_c0_enabled;
    PettaMachineTraceConfig trace;
    char typecheck_diagnostic[512];
    int typecheck_exit_code;
    PettaMachineStats stats;
};

struct PettaContinuationTermPool {
    _Atomic size_t references;
    _Atomic size_t reclaim_after_bytes;
    Arena owner;
};

typedef struct {
    uint64_t source_machine_instance;
    SpaceReadToken space_read;
    CettaBranchAuthorityToken capture_authority;
} PettaContinuationAuthorityComponent;

typedef struct {
    PettaContinuationTermPool *pool;
} PettaContinuationTermComponent;

typedef struct {
    BindingsBuilder bindings;
} PettaContinuationBindingComponent;

typedef struct {
    PettaGoal *goals;
    size_t goal_len;
    size_t goal_initialized_len;
    size_t protected_goal_height;
    PettaGoalTrailEntry *goal_trail;
    size_t goal_trail_len;
    PettaChoice *choices;
    size_t choice_len;
    uint64_t next_goal_instance;
    uint64_t binding_growth_collect_after;
} PettaContinuationControlComponent;

typedef struct {
    PettaVisibleVariable *visible;
    size_t visible_len;
    PettaTypeObligation *type_obligations;
    size_t type_obligation_len;
    uint64_t next_type_obligation_id;
} PettaContinuationObligationComponent;

typedef struct {
    Atom *query;
    Atom *answer_variable;
    Atom *raised_error;
    bool yielded;
    bool suspended_choice;
    bool terminal;
    bool count_only_emission;
    uint64_t pending_answer_weight;
    uint64_t last_answer_weight;
    PettaMachineStep terminal_step;
} PettaContinuationReadoutComponent;

typedef struct PettaOwnedContinuationImpl {
    PettaContinuationAuthorityComponent authority;
    PettaContinuationTermComponent terms;
    PettaContinuationBindingComponent bindings;
    PettaContinuationControlComponent control;
    PettaContinuationObligationComponent obligations;
    PettaContinuationReadoutComponent readout;
    size_t exclusive_vector_bytes;
} PettaOwnedContinuationImpl;

static void petta_machine_invalidate_activation_frame(
        PettaMachineImpl *machine) {
    if (!machine)
        return;
    for (uint32_t index = 0u;
         index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
        machine->activation_frames[index].equation_template = NULL;
        machine->activation_frames[index].activation_epoch = 0u;
        machine->activation_frames[index].activation_first_entry = 0u;
    }
    machine->activation_frame_replacement = 0u;
}

static SymbolId petta_machine_reify_head(const PettaMachineImpl *machine) {
    return machine && machine->host.reify_head != SYMBOL_ID_NONE
        ? machine->host.reify_head
        : g_builtin_syms.collapse;
}

static bool petta_machine_is_reify_head(
    const PettaMachineImpl *machine, SymbolId head) {
    SymbolId primary = petta_machine_reify_head(machine);
    return head == primary ||
        head == g_builtin_syms.reify ||
        head == g_builtin_syms.collapse;
}

static bool petta_machine_atom_is_reify_head(
    const PettaMachineImpl *machine, const Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL &&
        petta_machine_is_reify_head(machine, atom->sym_id);
}

static bool petta_machine_type_obligations_enabled(
    const PettaMachineImpl *machine) {
    return machine &&
        machine->host.analysis &&
        (machine->host.analysis->capabilities &
         PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS) != 0u;
}

static uint32_t petta_machine_analysis_policy(
    const PettaMachineImpl *machine) {
    return machine && machine->host.analysis &&
           machine->host.analysis->policy_identity
        ? machine->host.analysis->policy_identity(
              machine->host.context)
        : 0u;
}

static bool petta_machine_analysis_has_runtime_classifier(
    const PettaMachineImpl *machine, Atom *requirement) {
    return machine && machine->host.analysis &&
        machine->host.analysis->type_has_runtime_classifier &&
        machine->host.analysis->type_has_runtime_classifier(
            machine->host.context, machine->space, requirement);
}

static bool petta_machine_authority_token(
    PettaMachineImpl *machine, PettaMachineAuthorityToken *token) {
    if (!machine || !token)
        return false;
    *token = (PettaMachineAuthorityToken){0};
    const PettaAnalysisService *analysis = machine->host.analysis;
    if (!analysis)
        return true;
    PettaMachineAuthorityToken mutable = {0};
    if (analysis->mutable_authority_token &&
        !analysis->mutable_authority_token(
            machine->host.context, &mutable)) {
        return false;
    }
    return cetta_nik_direct_authority_v1_token(
        analysis->authority,
        petta_machine_analysis_policy(machine), &mutable, token);
}

static bool petta_machine_authority_token_eq(
    const PettaMachineAuthorityToken *left,
    const PettaMachineAuthorityToken *right) {
    if (!left || !right || left->length != right->length ||
        left->length > PETTA_MACHINE_AUTHORITY_WORD_CAPACITY) {
        return false;
    }
    if (left->length == 0u)
        return true;
    return cetta_nik_direct_authority_token_v1_equal(left, right);
}

static void petta_machine_invalidate_type_obligation_cache(
    PettaMachineImpl *machine) {
    if (!machine)
        return;
    machine->type_obligation_check_pending = true;
    machine->type_obligation_checked_read = (SpaceReadToken){0};
    machine->type_obligation_checked_epoch = 0u;
    machine->type_obligation_checked_policy = 0u;
    machine->type_obligation_checked_authority =
        (PettaMachineAuthorityToken){0};
    for (size_t index = 0u;
         index < machine->type_obligation_len; index++) {
        PettaTypeObligation *obligation =
            &machine->type_obligations[index];
        obligation->checked_value = NULL;
        obligation->checked_formal = NULL;
        obligation->checked_read = (SpaceReadToken){0};
        obligation->checked_epoch = 0u;
        obligation->checked_policy = 0u;
        obligation->checked_authority =
            (PettaMachineAuthorityToken){0};
        obligation->state = PETTA_TYPE_OBLIGATION_UNCHECKED;
    }
}

static uint64_t petta_machine_monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static void petta_machine_add_u64(
    uint64_t *target, uint64_t amount) {
    if (!target)
        return;
    *target = UINT64_MAX - *target < amount
        ? UINT64_MAX : *target + amount;
}

static uint64_t petta_machine_arena_growth(
    const Arena *arena, size_t before) {
    size_t live = arena_accounted_live_bytes(arena);
    if (live <= before)
        return 0u;
    size_t growth = live - before;
    return growth > UINT64_MAX
        ? UINT64_MAX : (uint64_t)growth;
}

static bool petta_plan_open_template_admitted(
    const PettaPlanNode *plan) {
    return plan && plan->open_template_admitted;
}

static void petta_machine_note_binding_apply_environment(
    PettaMachineImpl *machine, const Bindings *bindings,
    uint32_t first_entry, bool epoch_view) {
    if (!machine || !machine->host.measure_stats)
        return;
    size_t environment_entries = bindings ? bindings->len : 0u;
    petta_machine_add_u64(
        &machine->stats.binding_apply_environment_entries,
        environment_entries > UINT64_MAX
            ? UINT64_MAX : (uint64_t)environment_entries);
    if (environment_entries >
        machine->stats.maximum_binding_apply_environment_entries) {
        machine->stats.maximum_binding_apply_environment_entries =
            environment_entries;
    }
    if (!epoch_view)
        return;
    machine->stats.binding_apply_epoch_calls++;
    size_t suffix_entries = first_entry <= environment_entries
        ? environment_entries - first_entry : 0u;
    petta_machine_add_u64(
        &machine->stats.binding_apply_epoch_suffix_entries,
        suffix_entries > UINT64_MAX
            ? UINT64_MAX : (uint64_t)suffix_entries);
    if (suffix_entries >
        machine->stats.maximum_binding_apply_epoch_suffix_entries) {
        machine->stats.maximum_binding_apply_epoch_suffix_entries =
            suffix_entries;
    }
}

static Atom *petta_machine_apply_bindings(
    PettaMachineImpl *machine, const Bindings *bindings,
    Arena *arena, Atom *atom) {
    if (!machine)
        return bindings_apply_if_vars(bindings, arena, atom);
    machine->stats.binding_apply_calls++;
    petta_machine_note_binding_apply_environment(
        machine, bindings, 0u, false);
    bool rewrites = bindings && bindings->len != 0u && atom &&
        atom_has_vars(atom);
    if (rewrites)
        machine->stats.binding_apply_rewrites++;
    if (!machine->host.measure_stats)
        return bindings_apply_if_vars(bindings, arena, atom);
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = bindings_apply_if_vars(bindings, arena, atom);
    petta_machine_add_u64(
        &machine->stats.binding_apply_allocated_bytes,
        petta_machine_arena_growth(arena, before));
    return result;
}

static Atom *petta_machine_apply_bindings_epoch_since(
    PettaMachineImpl *machine, Bindings *bindings,
    Arena *arena, Atom *atom, uint32_t epoch,
    uint32_t first_entry) {
    if (!machine)
        return bindings_apply_epoch_since(
            bindings, arena, atom, epoch, first_entry);
    machine->stats.binding_apply_calls++;
    petta_machine_note_binding_apply_environment(
        machine, bindings, first_entry, true);
    bool rewrites = bindings && bindings->len > first_entry &&
        atom && atom_has_vars(atom);
    if (rewrites)
        machine->stats.binding_apply_rewrites++;
    if (!machine->host.measure_stats)
        return bindings_apply_epoch_since(
            bindings, arena, atom, epoch, first_entry);
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = bindings_apply_epoch_since(
        bindings, arena, atom, epoch, first_entry);
    petta_machine_add_u64(
        &machine->stats.binding_apply_allocated_bytes,
        petta_machine_arena_growth(arena, before));
    return result;
}

static Atom *petta_machine_apply_bindings_epoch_then_all(
    PettaMachineImpl *machine, Bindings *bindings,
    Arena *arena, Atom *atom, uint32_t epoch,
    uint32_t first_entry) {
    if (!machine)
        return bindings_apply_epoch_then_all(
            bindings, arena, atom, epoch, first_entry);
    machine->stats.binding_apply_calls++;
    petta_machine_note_binding_apply_environment(
        machine, bindings, first_entry, true);
    bool rewrites = bindings && bindings->len > first_entry &&
        atom && atom_has_vars(atom);
    if (rewrites)
        machine->stats.binding_apply_rewrites++;
    if (!machine->host.measure_stats)
        return bindings_apply_epoch_then_all(
            bindings, arena, atom, epoch, first_entry);
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = bindings_apply_epoch_then_all(
        bindings, arena, atom, epoch, first_entry);
    petta_machine_add_u64(
        &machine->stats.binding_apply_allocated_bytes,
        petta_machine_arena_growth(arena, before));
    return result;
}

/*
 * A structural observer needs a value together with its environment, not a
 * recursively substituted copy of the value.  Follow only a root variable
 * chain here; consumers such as the logical-list shape walker then resolve
 * the next spine node on demand.  Element payloads remain untouched until a
 * consumer actually observes them, and the ordinary deep application path
 * remains authoritative at materialization boundaries.
 */
static Atom *petta_machine_resolve_root(
    const Bindings *bindings, Atom *atom) {
    if (!bindings || bindings->len == 0u || !atom ||
        atom->kind != ATOM_VAR) {
        return atom;
    }
    return bindings_resolve_atom_preview(
        (Bindings *)bindings, atom);
}

typedef enum {
    PETTA_ATOM_COPY_QUERY,
    PETTA_ATOM_COPY_ANSWER,
    PETTA_ATOM_COPY_VISIBLE_VARIABLE,
    PETTA_ATOM_COPY_VISIBLE_VALUE,
    PETTA_ATOM_COPY_ERROR,
} PettaAtomCopyPurpose;

static void petta_machine_note_copy_purpose(
    PettaMachineStats *stats, PettaAtomCopyPurpose purpose,
    uint64_t allocated_bytes) {
    if (!stats)
        return;
    uint64_t *calls = NULL;
    uint64_t *bytes = NULL;
    switch (purpose) {
    case PETTA_ATOM_COPY_QUERY:
        calls = &stats->atom_copy_query_calls;
        bytes = &stats->atom_copy_query_allocated_bytes;
        break;
    case PETTA_ATOM_COPY_ANSWER:
        calls = &stats->atom_copy_answer_calls;
        bytes = &stats->atom_copy_answer_allocated_bytes;
        break;
    case PETTA_ATOM_COPY_VISIBLE_VARIABLE:
        calls = &stats->atom_copy_visible_variable_calls;
        bytes = &stats->atom_copy_visible_variable_allocated_bytes;
        break;
    case PETTA_ATOM_COPY_VISIBLE_VALUE:
        calls = &stats->atom_copy_visible_value_calls;
        bytes = &stats->atom_copy_visible_value_allocated_bytes;
        break;
    case PETTA_ATOM_COPY_ERROR:
        calls = &stats->atom_copy_error_calls;
        bytes = &stats->atom_copy_error_allocated_bytes;
        break;
    }
    petta_machine_add_u64(calls, 1u);
    petta_machine_add_u64(bytes, allocated_bytes);
}

static Atom *petta_machine_copy_atom(
    PettaMachineImpl *machine, Arena *arena, Atom *atom,
    PettaAtomCopyPurpose purpose) {
    if (!machine)
        return atom_deep_copy(arena, atom);
    machine->stats.atom_copy_calls++;
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = atom_deep_copy(arena, atom);
    uint64_t allocated_bytes =
        petta_machine_arena_growth(arena, before);
    petta_machine_add_u64(
        &machine->stats.atom_copy_allocated_bytes, allocated_bytes);
    petta_machine_note_copy_purpose(
        &machine->stats, purpose, allocated_bytes);
    return result;
}

static Atom *petta_machine_freshen_atom(
    PettaMachineImpl *machine, Atom *atom, uint32_t epoch) {
    if (!machine)
        return NULL;
    machine->stats.atom_freshen_calls++;
    size_t before = arena_accounted_live_bytes(&machine->heap);
    Atom *result = atom_freshen_epoch(
        &machine->heap, atom, epoch);
    petta_machine_add_u64(
        &machine->stats.atom_freshen_allocated_bytes,
        petta_machine_arena_growth(&machine->heap, before));
    return result;
}

static bool petta_machine_host_valid(const PettaMachineHost *host) {
    if (!host)
        return true;
    uint32_t known_analyses =
        PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS;
    const PettaAnalysisService *analysis = host->analysis;
    if (!analysis)
        return true;
    bool type_obligations =
        (analysis->capabilities &
         PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS) != 0u;
    return (analysis->capabilities & ~known_analyses) == 0u &&
        analysis->capabilities != 0u &&
        cetta_nik_direct_authority_v1_is_valid(
            analysis->authority) &&
        analysis->policy_identity &&
        analysis->mutable_authority_token &&
        (!type_obligations ||
         (analysis->judge_value &&
          analysis->type_has_runtime_classifier &&
          analysis->error_atom &&
          analysis->reason_name &&
          analysis->validate_ready_call));
}

static uint64_t petta_machine_fresh_instance_id(void) {
    uint64_t instance = atomic_fetch_add_explicit(
        &g_petta_machine_instance_counter, 1u,
        memory_order_relaxed);
    if (instance == 0u) {
        fputs("fatal: PeTTa machine identity space exhausted\n", stderr);
        abort();
    }
    return instance;
}

static bool petta_machine_init_internal(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *query_plan,
    const Bindings *base_environment,
    const PettaMachineHost *host,
    bool borrow_query,
    PettaTableShared *table_shared,
    bool owns_table_shared, bool borrowed_root_table,
    bool bypass_root_table,
    size_t table_generator);

typedef struct {
    Atom *result;
    Bindings environment;
    bool present;
    bool capacity;
    bool result_fully_resolved;
    bool result_is_activation;
    uint32_t activation_epoch;
    uint32_t activation_first_entry;
} PettaClauseMatch;

typedef enum {
    PETTA_CLAUSE_SLOT_NO_MATCH = 0,
    PETTA_CLAUSE_SLOT_MATCH,
    PETTA_CLAUSE_SLOT_CAPACITY,
} PettaClauseSlotMatch;

static bool petta_machine_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0)
        enabled = getenv("CETTA_PETTA_MACHINE_TRACE") ? 1 : 0;
    return enabled == 1;
}

static bool petta_goal_growth_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("CETTA_PETTA_GOAL_GROWTH_TRACE")
            ? 1 : 0;
    }
    return enabled == 1;
}

static uint64_t petta_trace_u64(
    const char *name, uint64_t fallback, uint64_t maximum) {
    const char *requested = getenv(name);
    if (!requested || requested[0] == '\0')
        return fallback;
    char *end = NULL;
    unsigned long long parsed = strtoull(requested, &end, 10);
    if (end == requested || !end || *end != '\0')
        return fallback;
    return parsed > maximum ? maximum : (uint64_t)parsed;
}

static void petta_machine_trace_config_init(
    PettaMachineTraceConfig *config) {
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->query_head = SYMBOL_ID_NONE;
    config->query_limit = (uint32_t)petta_trace_u64(
        "CETTA_PETTA_QUERY_TRACE_LIMIT", 64u, UINT32_MAX);
    config->query_start = petta_trace_u64(
        "CETTA_PETTA_QUERY_TRACE_START", 0u, UINT64_MAX);
    const char *requested = getenv("CETTA_PETTA_QUERY_TRACE");
    if (requested) {
        config->query_enabled = true;
        if (strcmp(requested, "*") == 0) {
            config->query_heads_only = true;
        } else {
            config->query_head = symbol_intern_cstr(
                g_symbols, requested);
        }
    }
    config->choice_kind =
        getenv("CETTA_PETTA_CHOICE_KIND_TRACE") != NULL;
    config->choice =
        getenv("CETTA_PETTA_CHOICE_TRACE") != NULL;
}

static bool petta_query_trace_head(
    const PettaMachineImpl *machine, SymbolId head) {
    if (!machine || !machine->trace.query_enabled ||
        head == SYMBOL_ID_NONE) {
        return false;
    }
    return machine->trace.query_heads_only ||
        machine->trace.query_head == head;
}

static bool petta_clause_slot_frame_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        /* The activation-slot path is the ordinary machine representation.
         * This switch exists solely to keep the former isolated-environment
         * matcher executable as a differential reference. */
        enabled = getenv(
            "CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE") == NULL;
    }
    return enabled == 1;
}

static bool petta_equation_template_c0_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PETTA_EQUATION_TEMPLATE_C0_REFERENCE");
        enabled = !reference || strcmp(reference, "1") != 0;
    }
    return enabled != 0;
}

static bool petta_space_query_executable_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PETTA_SPACE_QUERY_EXECUTABLE_REFERENCE");
        enabled = !reference || strcmp(reference, "1") != 0;
    }
    return enabled != 0;
}

static bool petta_solve_expected_root_view_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv(
            "CETTA_PETTA_SOLVE_EXPECTED_ROOT_VIEW");
        enabled = value
            ? (value[0] == '1' ? 1 : 0)
            : (petta_space_query_executable_enabled() ? 1 : 0);
    }
    return enabled == 1;
}

static bool petta_solve_expression_root_view_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv(
            "CETTA_PETTA_SOLVE_EXPRESSION_ROOT_VIEW");
        enabled = value
            ? (value[0] == '1' ? 1 : 0)
            : (petta_space_query_executable_enabled() ? 1 : 0);
    }
    return enabled == 1;
}

static bool petta_relational_equation_view_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PETTA_RELATIONAL_EQUATION_VIEW_REFERENCE");
        enabled = !reference || strcmp(reference, "1") != 0;
    }
    return enabled != 0;
}

/* The isolated equation matcher projects rule-variable aliases back onto the
 * caller's visible variables before returning.  The direct frame obtains the
 * same normal form by orienting an unbound variable pair from the fresh rule
 * slot to the caller variable.  Reject any contrary direct alias here so a
 * later body traversal can never expose a frame-local name in place of its
 * caller-visible name.  Ordinary caller bindings to structured rule values
 * remain valid and are rolled back with the clause choice. */
static bool petta_clause_slot_aliases_normalized(
    const Bindings *bindings, uint32_t first_entry,
    uint32_t epoch, bool *cross_frame_alias) {
    if (cross_frame_alias)
        *cross_frame_alias = false;
    if (!bindings || first_entry > bindings->len || epoch == 0u)
        return false;
    for (uint32_t index = first_entry;
         index < bindings->len; index++) {
        const Binding *entry = &bindings->entries[index];
        bool entry_is_rule_local =
            var_epoch_suffix(entry->var_id) == epoch;
        bool value_is_rule_local =
            entry->val && entry->val->kind == ATOM_VAR &&
            var_epoch_suffix(entry->val->var_id) == epoch;
        if (entry->val && entry->val->kind == ATOM_VAR &&
            entry_is_rule_local != value_is_rule_local &&
            cross_frame_alias) {
            *cross_frame_alias = true;
        }
        if (!entry_is_rule_local && value_is_rule_local) {
            return false;
        }
    }
    return true;
}

static bool petta_clause_body_activation_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *candidate = getenv(
            "CETTA_PETTA_CLAUSE_BODY_ACTIVATION");
        const char *reference = getenv(
            "CETTA_PETTA_CLAUSE_BODY_ACTIVATION_REFERENCE");
        /*
         * The dense frame is a derived view of authoritative Bindings, but
         * its control-specialized let/translator/deep-chain path is not yet
         * semantically complete.  Keep it available for differential
         * experiments without making it the language default.  The older
         * REFERENCE switch remains a force-off control for paired runs.
         */
        enabled = candidate && candidate[0] != '\0' &&
            strcmp(candidate, "0") != 0 &&
            strcmp(candidate, "false") != 0 &&
            strcmp(candidate, "off") != 0 &&
            (!reference || strcmp(reference, "1") != 0);
    }
    return enabled != 0;
}

static bool petta_let_count_fusion_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value =
            getenv("CETTA_PETTA_LET_COUNT_FUSION");
        enabled =
            !value || value[0] == '\0' ||
            (strcmp(value, "0") != 0 &&
             strcmp(value, "false") != 0 &&
             strcmp(value, "off") != 0);
    }
    return enabled == 1;
}

static bool petta_match_existence_fusion_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value =
            getenv("CETTA_PETTA_MATCH_EXISTENCE_FUSION");
        enabled =
            !value || value[0] == '\0' ||
            (strcmp(value, "0") != 0 &&
             strcmp(value, "false") != 0 &&
             strcmp(value, "off") != 0);
    }
    return enabled == 1;
}

static void petta_machine_trace_atom(
    const char *label, Atom *atom) {
    if (!petta_machine_trace_enabled())
        return;
    fputs(label, stderr);
    if (atom)
        atom_print(atom, stderr);
    else
        fputs("<null>", stderr);
    fputc('\n', stderr);
}

static bool petta_machine_reserve(
    void **items, size_t *capacity, size_t needed, size_t item_size) {
    if (needed <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2u : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / item_size)
        return false;
    *items = cetta_realloc(*items, next * item_size);
    *capacity = next;
    return true;
}

static PettaTableShared *petta_table_shared_new(void) {
    PettaTableShared *shared = cetta_malloc(sizeof(*shared));
    memset(shared, 0, sizeof(*shared));
    arena_init(&shared->arena);
    arena_set_runtime_kind(
        &shared->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&shared->arena, NULL);
    shared->frequency_sketch = cetta_malloc(
        sizeof(*shared->frequency_sketch) *
        PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    memset(
        shared->frequency_sketch, 0,
        sizeof(*shared->frequency_sketch) *
            PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    shared->mutation_epoch = space_global_mutation_epoch();
    return shared;
}

static void petta_table_shared_free(PettaTableShared *shared) {
    if (!shared)
        return;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        free(shared->entries[index].answers);
    }
    free(shared->entries);
    free(shared->slots);
    free(shared->tarjan_stack);
    free(shared->frequency_sketch);
    arena_free(&shared->arena);
    free(shared);
}

static void petta_table_shared_reset(PettaTableShared *shared) {
    if (!shared)
        return;
    bool leased = shared->root_leased;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        free(shared->entries[index].answers);
    }
    free(shared->entries);
    free(shared->slots);
    free(shared->tarjan_stack);
    free(shared->frequency_sketch);
    arena_free(&shared->arena);
    memset(shared, 0, sizeof(*shared));
    arena_init(&shared->arena);
    arena_set_runtime_kind(
        &shared->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&shared->arena, NULL);
    shared->frequency_sketch = cetta_malloc(
        sizeof(*shared->frequency_sketch) *
        PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    memset(
        shared->frequency_sketch, 0,
        sizeof(*shared->frequency_sketch) *
            PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    shared->mutation_epoch = space_global_mutation_epoch();
    shared->root_leased = leased;
}

PettaMachineTable *petta_machine_table_new(void) {
    return petta_table_shared_new();
}

void petta_machine_table_free(PettaMachineTable *table) {
    petta_table_shared_free(table);
}

void petta_machine_table_reset(PettaMachineTable *table) {
    petta_table_shared_reset(table);
}

static bool petta_table_shared_reusable(
    const PettaTableShared *shared) {
    if (!shared || shared->failed || shared->tarjan_len != 0u)
        return false;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        if (shared->entries[index].state !=
            PETTA_TABLE_ENTRY_COMPLETE) {
            return false;
        }
    }
    return shared->mutation_epoch ==
        space_global_mutation_epoch();
}

static bool petta_table_shared_epoch_current(
    const PettaTableShared *shared) {
    return shared &&
           shared->mutation_epoch ==
               space_global_mutation_epoch();
}

static void petta_table_memo_note_access(
    PettaTableShared *shared, uint64_t hash) {
    if (!shared || !shared->frequency_sketch)
        return;
    if (shared->access_tick != UINT64_MAX)
        shared->access_tick++;
    size_t slot =
        (size_t)hash & (PETTA_MEMO_FREQUENCY_SKETCH_SIZE - 1u);
    if (shared->frequency_sketch[slot] != UINT16_MAX)
        shared->frequency_sketch[slot]++;
    if (shared->frequency_accesses != UINT64_MAX)
        shared->frequency_accesses++;
    if (shared->frequency_accesses <
        PETTA_MEMO_FREQUENCY_SKETCH_SIZE) {
        return;
    }
    for (size_t index = 0u;
         index < PETTA_MEMO_FREQUENCY_SKETCH_SIZE; index++) {
        shared->frequency_sketch[index] /= 2u;
    }
    shared->frequency_accesses = 0u;
}

static bool petta_table_rehash(
    PettaTableShared *shared, size_t capacity) {
    if (!shared || capacity < 16u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(*shared->slots)) {
        return false;
    }
    size_t *slots = cetta_malloc(
        sizeof(*slots) * capacity);
    memset(slots, 0, sizeof(*slots) * capacity);
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        size_t slot =
            (size_t)shared->entries[index].hash &
            (capacity - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (capacity - 1u);
        slots[slot] = index + 1u;
    }
    free(shared->slots);
    shared->slots = slots;
    shared->slot_cap = capacity;
    return true;
}

static bool petta_table_ensure_slot_capacity(
    PettaTableShared *shared, size_t needed) {
    if (!shared)
        return false;
    if (shared->slot_cap == 0u)
        return petta_table_rehash(shared, 16u);
    if (needed <=
        shared->slot_cap / 2u +
            shared->slot_cap / 5u) {
        return true;
    }
    if (shared->slot_cap > SIZE_MAX / 2u)
        return false;
    return petta_table_rehash(
        shared, shared->slot_cap * 2u);
}

static size_t petta_table_find(
    const PettaTableShared *shared, Atom *key,
    uint64_t hash) {
    if (!shared || !key || shared->slot_cap == 0u)
        return PETTA_TABLE_ENTRY_NONE;
    size_t slot =
        (size_t)hash & (shared->slot_cap - 1u);
    for (size_t probes = 0u;
         probes < shared->slot_cap; probes++) {
        size_t encoded = shared->slots[slot];
        if (encoded == 0u)
            return PETTA_TABLE_ENTRY_NONE;
        size_t index = encoded - 1u;
        if (index < shared->entry_len &&
            shared->entries[index].hash == hash &&
            atom_eq(shared->entries[index].key, key)) {
            return index;
        }
        slot = (slot + 1u) & (shared->slot_cap - 1u);
    }
    return PETTA_TABLE_ENTRY_NONE;
}

static bool petta_table_find_or_insert(
    PettaTableShared *shared, Atom *key, Atom *generator_key,
    uint64_t hash,
    size_t *index_out, bool *inserted_out) {
    if (index_out)
        *index_out = PETTA_TABLE_ENTRY_NONE;
    if (inserted_out)
        *inserted_out = false;
    if (!shared || !key || !generator_key ||
        !index_out || !inserted_out)
        return false;

    size_t found = petta_table_find(shared, key, hash);
    if (found != PETTA_TABLE_ENTRY_NONE) {
        *index_out = found;
        return true;
    }
    if (!petta_table_ensure_slot_capacity(
            shared, shared->entry_len + 1u) ||
        !petta_machine_reserve(
            (void **)&shared->entries,
            &shared->entry_cap,
            shared->entry_len + 1u,
            sizeof(*shared->entries))) {
        return false;
    }

    size_t retained_before =
        arena_accounted_live_bytes(&shared->arena);
    Atom *owned = atom_deep_copy(&shared->arena, key);
    if (!owned)
        return false;
    Atom *owned_generator = atom_eq(key, generator_key)
        ? owned
        : atom_deep_copy(&shared->arena, generator_key);
    if (!owned_generator)
        return false;
    size_t retained_after =
        arena_accounted_live_bytes(&shared->arena);
    size_t index = shared->entry_len++;
    shared->entries[index] = (PettaTableEntry){
        .key = owned,
        .generator_key = owned_generator,
        .hash = hash,
        .retained_bytes = retained_after >= retained_before
            ? (uint64_t)(retained_after - retained_before)
            : 0u,
        .state = PETTA_TABLE_ENTRY_NEW,
        .tarjan_index = PETTA_TABLE_ENTRY_NONE,
        .tarjan_lowlink = PETTA_TABLE_ENTRY_NONE,
    };
    size_t slot =
        (size_t)hash & (shared->slot_cap - 1u);
    while (shared->slots[slot] != 0u)
        slot = (slot + 1u) & (shared->slot_cap - 1u);
    shared->slots[slot] = index + 1u;
    *index_out = index;
    *inserted_out = true;
    return true;
}

static bool petta_table_tarjan_push(
    PettaTableShared *shared, size_t entry) {
    if (!shared ||
        !petta_machine_reserve(
            (void **)&shared->tarjan_stack,
            &shared->tarjan_cap,
            shared->tarjan_len + 1u,
            sizeof(*shared->tarjan_stack))) {
        return false;
    }
    shared->tarjan_stack[shared->tarjan_len++] = entry;
    return true;
}

typedef struct {
    Atom *answer;
    size_t existing_count;
    size_t round_count;
} PettaTableAnswerCountSlot;

static PettaTableAnswerCountSlot *petta_table_answer_count_slot(
    PettaTableAnswerCountSlot *slots, size_t capacity,
    Atom *answer) {
    if (!slots || capacity == 0u || !answer)
        return NULL;
    size_t slot =
        (size_t)atom_hash(answer) & (capacity - 1u);
    for (size_t probes = 0u; probes < capacity; probes++) {
        if (!slots[slot].answer ||
            atom_eq(slots[slot].answer, answer)) {
            if (!slots[slot].answer)
                slots[slot].answer = answer;
            return &slots[slot];
        }
        slot = (slot + 1u) & (capacity - 1u);
    }
    return NULL;
}

static bool petta_table_merge_round_answers(
    PettaTableShared *shared, PettaTableEntry *entry,
    Atom *const *answers, size_t answer_len,
    bool *changed) {
    if (changed)
        *changed = false;
    if (!shared || !entry || !changed ||
        (answer_len > 0u && !answers)) {
        return false;
    }

    if (entry->answer_len > SIZE_MAX - answer_len)
        return false;
    size_t items = entry->answer_len + answer_len;
    size_t capacity = 16u;
    while (capacity / 2u < items) {
        if (capacity > SIZE_MAX / 2u)
            return false;
        capacity *= 2u;
    }
    if (capacity >
        SIZE_MAX / sizeof(PettaTableAnswerCountSlot)) {
        return false;
    }
    PettaTableAnswerCountSlot *slots = cetta_malloc(
        sizeof(*slots) * capacity);
    memset(slots, 0, sizeof(*slots) * capacity);
    for (size_t index = 0u;
         index < entry->answer_len; index++) {
        PettaTableAnswerCountSlot *slot =
            petta_table_answer_count_slot(
                slots, capacity, entry->answers[index]);
        if (!slot || slot->existing_count == SIZE_MAX) {
            free(slots);
            return false;
        }
        slot->existing_count++;
    }

    for (size_t index = 0u; index < answer_len; index++) {
        Atom *answer = answers[index];
        PettaTableAnswerCountSlot *slot =
            petta_table_answer_count_slot(
                slots, capacity, answer);
        if (!slot || slot->round_count == SIZE_MAX) {
            free(slots);
            return false;
        }
        slot->round_count++;
        if (slot->round_count <= slot->existing_count)
            continue;
        size_t answer_cap_before = entry->answer_cap;
        size_t retained_before =
            arena_accounted_live_bytes(&shared->arena);
        if (!petta_machine_reserve(
                (void **)&entry->answers,
                &entry->answer_cap,
                entry->answer_len + 1u,
                sizeof(*entry->answers))) {
            free(slots);
            return false;
        }
        Atom *owned = atom_deep_copy(
            &shared->arena, answer);
        if (!owned) {
            free(slots);
            return false;
        }
        entry->answers[entry->answer_len++] = owned;
        size_t retained_after =
            arena_accounted_live_bytes(&shared->arena);
        uint64_t retained_delta = retained_after >= retained_before
            ? (uint64_t)(retained_after - retained_before)
            : 0u;
        if (entry->answer_cap > answer_cap_before) {
            uint64_t slots_delta =
                (uint64_t)(entry->answer_cap - answer_cap_before) *
                (uint64_t)sizeof(*entry->answers);
            if (UINT64_MAX - retained_delta < slots_delta)
                retained_delta = UINT64_MAX;
            else
                retained_delta += slots_delta;
        }
        if (UINT64_MAX - entry->retained_bytes < retained_delta)
            entry->retained_bytes = UINT64_MAX;
        else
            entry->retained_bytes += retained_delta;
        if (entry->memoized) {
            if (UINT64_MAX - shared->memo_retained_bytes <
                retained_delta) {
                shared->memo_retained_bytes = UINT64_MAX;
            } else {
                shared->memo_retained_bytes += retained_delta;
            }
            shared->memo_policy_dirty = true;
        }
        slot->existing_count++;
        *changed = true;
    }
    free(slots);
    return true;
}

typedef struct {
    size_t entry_index;
    SymbolId head;
    CettaExprLen arity;
    uint64_t primary;
    uint64_t secondary;
} PettaMemoRetentionCandidate;

static int petta_memo_candidate_group_compare(
    const void *left_pointer, const void *right_pointer) {
    const PettaMemoRetentionCandidate *left = left_pointer;
    const PettaMemoRetentionCandidate *right = right_pointer;
    if (left->head != right->head)
        return left->head < right->head ? -1 : 1;
    if (left->arity != right->arity)
        return left->arity < right->arity ? -1 : 1;
    if (left->primary != right->primary)
        return left->primary > right->primary ? -1 : 1;
    if (left->secondary != right->secondary)
        return left->secondary > right->secondary ? -1 : 1;
    if (left->entry_index != right->entry_index)
        return left->entry_index > right->entry_index ? -1 : 1;
    return 0;
}

static int petta_memo_candidate_priority_compare(
    const void *left_pointer, const void *right_pointer) {
    const PettaMemoRetentionCandidate *left = left_pointer;
    const PettaMemoRetentionCandidate *right = right_pointer;
    if (left->primary != right->primary)
        return left->primary > right->primary ? -1 : 1;
    if (left->secondary != right->secondary)
        return left->secondary > right->secondary ? -1 : 1;
    if (left->entry_index != right->entry_index)
        return left->entry_index > right->entry_index ? -1 : 1;
    return 0;
}

static void petta_table_shared_release_contents(
    PettaTableShared *shared) {
    if (!shared)
        return;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        free(shared->entries[index].answers);
    }
    free(shared->entries);
    free(shared->slots);
    free(shared->tarjan_stack);
    free(shared->frequency_sketch);
    arena_free(&shared->arena);
}

static bool petta_table_shared_copy_selected(
    PettaTableShared *shared, const bool *keep) {
    if (!shared || !keep || shared->root_leased ||
        !petta_table_shared_reusable(shared)) {
        return false;
    }

    PettaTableShared replacement;
    memset(&replacement, 0, sizeof(replacement));
    arena_init(&replacement.arena);
    arena_set_runtime_kind(
        &replacement.arena,
        CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&replacement.arena, NULL);
    replacement.frequency_sketch = cetta_malloc(
        sizeof(*replacement.frequency_sketch) *
        PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    memcpy(
        replacement.frequency_sketch,
        shared->frequency_sketch,
        sizeof(*replacement.frequency_sketch) *
            PETTA_MEMO_FREQUENCY_SKETCH_SIZE);
    replacement.frequency_accesses =
        shared->frequency_accesses;
    replacement.access_tick = shared->access_tick;
    replacement.mutation_epoch = shared->mutation_epoch;

    bool ok = true;
    for (size_t index = 0u;
         ok && index < shared->entry_len; index++) {
        if (!keep[index])
            continue;
        PettaTableEntry *source = &shared->entries[index];
        if (!petta_machine_reserve(
                (void **)&replacement.entries,
                &replacement.entry_cap,
                replacement.entry_len + 1u,
                sizeof(*replacement.entries))) {
            ok = false;
            break;
        }
        size_t retained_before =
            arena_accounted_live_bytes(&replacement.arena);
        Atom *key = atom_deep_copy(
            &replacement.arena, source->key);
        Atom *generator_key =
            source->generator_key == source->key ||
                    atom_eq(source->generator_key, source->key)
                ? key
                : atom_deep_copy(
                      &replacement.arena,
                      source->generator_key);
        Atom **answers = source->answer_len
            ? cetta_malloc(
                  sizeof(*answers) * source->answer_len)
            : NULL;
        if (!key || !generator_key ||
            (source->answer_len > 0u && !answers)) {
            free(answers);
            ok = false;
            break;
        }
        for (size_t answer_index = 0u;
             answer_index < source->answer_len; answer_index++) {
            answers[answer_index] = atom_deep_copy(
                &replacement.arena,
                source->answers[answer_index]);
            if (!answers[answer_index]) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            free(answers);
            break;
        }
        size_t retained_after =
            arena_accounted_live_bytes(&replacement.arena);
        uint64_t retained = retained_after >= retained_before
            ? (uint64_t)(retained_after - retained_before)
            : 0u;
        uint64_t answer_bytes =
            (uint64_t)source->answer_len *
            (uint64_t)sizeof(*answers);
        if (UINT64_MAX - retained < answer_bytes)
            retained = UINT64_MAX;
        else
            retained += answer_bytes;

        PettaTableEntry copied = *source;
        copied.key = key;
        copied.generator_key = generator_key;
        copied.answers = answers;
        copied.answer_cap = copied.answer_len;
        copied.retained_bytes = retained;
        copied.state = PETTA_TABLE_ENTRY_COMPLETE;
        copied.tarjan_index = PETTA_TABLE_ENTRY_NONE;
        copied.tarjan_lowlink = PETTA_TABLE_ENTRY_NONE;
        copied.tarjan_on_stack = false;
        copied.self_edge = false;
        replacement.entries[replacement.entry_len++] = copied;
        if (copied.memoized) {
            if (UINT64_MAX - replacement.memo_retained_bytes <
                retained) {
                replacement.memo_retained_bytes = UINT64_MAX;
            } else {
                replacement.memo_retained_bytes += retained;
            }
        }
    }

    size_t slot_capacity = 16u;
    while (ok &&
           replacement.entry_len >
               slot_capacity / 2u + slot_capacity / 5u) {
        if (slot_capacity > SIZE_MAX / 2u) {
            ok = false;
            break;
        }
        slot_capacity *= 2u;
    }
    if (ok)
        ok = petta_table_rehash(
            &replacement, slot_capacity);
    if (!ok) {
        petta_table_shared_release_contents(&replacement);
        return false;
    }

    replacement.memo_policy_dirty = false;
    petta_table_shared_release_contents(shared);
    *shared = replacement;
    return true;
}

bool petta_machine_table_maintain(
    PettaMachineTable *table,
    PettaMemoRetentionPolicy policy,
    uint32_t unique_limit,
    uint64_t size_limit_bytes) {
    PettaTableShared *shared = table;
    if (!shared || shared->root_leased ||
        unique_limit == 0u || size_limit_bytes == 0u) {
        return false;
    }
    if (!shared->memo_policy_dirty)
        return true;
    if (!petta_table_shared_reusable(shared))
        return true;

    bool *keep = shared->entry_len
        ? cetta_malloc(sizeof(*keep) * shared->entry_len)
        : NULL;
    PettaMemoRetentionCandidate *candidates = shared->entry_len
        ? cetta_malloc(
              sizeof(*candidates) * shared->entry_len)
        : NULL;
    if (shared->entry_len > 0u && (!keep || !candidates)) {
        free(keep);
        free(candidates);
        petta_table_shared_reset(shared);
        return false;
    }
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        keep[index] = true;
    }

    size_t candidate_len = 0u;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        PettaTableEntry *entry = &shared->entries[index];
        if (!entry->memoized)
            continue;
        uint64_t frequency = shared->frequency_sketch
            ? shared->frequency_sketch[
                  (size_t)entry->hash &
                  (PETTA_MEMO_FREQUENCY_SKETCH_SIZE - 1u)]
            : 0u;
        candidates[candidate_len++] =
            (PettaMemoRetentionCandidate){
                .entry_index = index,
                .head = entry->memo_head,
                .arity = entry->memo_arity,
                .primary =
                    policy == PETTA_MEMO_RETENTION_LRU
                        ? entry->access_tick : frequency,
                .secondary =
                    policy == PETTA_MEMO_RETENTION_LRU
                        ? 0u : entry->access_tick,
            };
    }

    qsort(
        candidates, candidate_len,
        sizeof(*candidates),
        petta_memo_candidate_group_compare);
    size_t group_rank = 0u;
    for (size_t index = 0u; index < candidate_len; index++) {
        if (index == 0u ||
            candidates[index].head != candidates[index - 1u].head ||
            candidates[index].arity != candidates[index - 1u].arity) {
            group_rank = 0u;
        }
        if (group_rank >= (size_t)unique_limit)
            keep[candidates[index].entry_index] = false;
        group_rank++;
    }

    qsort(
        candidates, candidate_len,
        sizeof(*candidates),
        petta_memo_candidate_priority_compare);
    uint64_t retained = 0u;
    for (size_t index = 0u; index < candidate_len; index++) {
        size_t entry_index = candidates[index].entry_index;
        if (!keep[entry_index])
            continue;
        uint64_t bytes =
            shared->entries[entry_index].retained_bytes;
        if (bytes > size_limit_bytes - retained) {
            keep[entry_index] = false;
            continue;
        }
        retained += bytes;
    }

    bool changed = false;
    for (size_t index = 0u;
         index < shared->entry_len; index++) {
        if (!keep[index]) {
            changed = true;
            break;
        }
    }
    bool ok = true;
    if (changed)
        ok = petta_table_shared_copy_selected(shared, keep);
    else {
        shared->memo_retained_bytes = retained;
        shared->memo_policy_dirty = false;
    }
    free(keep);
    free(candidates);
    if (!ok)
        petta_table_shared_reset(shared);
    return ok;
}

static bool petta_goal_trail_record_overwrite(
    PettaMachineImpl *machine, size_t index) {
    if (!machine || machine->choice_len == 0u ||
        index >= machine->protected_goal_height) {
        return machine != NULL;
    }
    if (index >= machine->goal_initialized_len ||
        !petta_machine_reserve(
            (void **)&machine->goal_trail,
            &machine->goal_trail_cap,
            machine->goal_trail_len + 1u,
            sizeof(*machine->goal_trail))) {
        return false;
    }
    machine->goal_trail[machine->goal_trail_len++] =
        (PettaGoalTrailEntry){
            .index = index,
            .previous = machine->goals[index],
        };
    machine->stats.choice_continuation_items_trailed++;
    if (machine->goal_trail_len >
        machine->stats.maximum_choice_continuation_trail) {
        machine->stats.maximum_choice_continuation_trail =
            machine->goal_trail_len;
    }
    return true;
}

static bool petta_goal_trail_rollback(
    PettaMachineImpl *machine, size_t mark) {
    if (!machine || mark > machine->goal_trail_len)
        return false;
    while (machine->goal_trail_len > mark) {
        PettaGoalTrailEntry entry =
            machine->goal_trail[--machine->goal_trail_len];
        if (entry.index >= machine->goal_initialized_len)
            return false;
        machine->goals[entry.index] = entry.previous;
    }
    return true;
}

static bool petta_goal_push(PettaMachineImpl *machine, PettaGoal goal) {
    if (petta_goal_growth_trace_enabled() &&
        machine->goal_len + 1u > machine->goal_cap) {
        uint64_t counts[PETTA_GOAL_CATCH_READY + 1u] = {0};
        uint64_t choice_counts[PETTA_CHOICE_TABLE + 1u] = {0};
        for (size_t index = 0u; index < machine->goal_len; index++) {
            PettaGoalKind kind = machine->goals[index].kind;
            if ((unsigned)kind <= PETTA_GOAL_CATCH_READY)
                counts[kind]++;
        }
        for (size_t index = 0u; index < machine->choice_len; index++) {
            PettaChoiceKind kind = machine->choices[index].kind;
            if ((unsigned)kind <= PETTA_CHOICE_TABLE)
                choice_counts[kind]++;
        }
        SymbolId first_head = atom_head_symbol_id(goal.first);
        const BindingsBuilder *builder =
            search_context_builder(&machine->search);
        const Bindings *bindings =
            search_context_bindings(&machine->search);
        size_t malloc_in_use = 0u;
        size_t malloc_free = 0u;
        size_t malloc_mapped = 0u;
#if defined(__GLIBC__)
        struct mallinfo2 allocator = mallinfo2();
        malloc_in_use = (size_t)allocator.uordblks;
        malloc_free = (size_t)allocator.fordblks;
        malloc_mapped = (size_t)allocator.hblkhd;
#endif
        fprintf(
            stderr,
            "[petta-goal-growth] len=%zu cap=%zu item-bytes=%zu"
            " machine=%p incoming=%u first-head=%s"
            " plan-role=%u plan-execution=%u"
            " choices=%zu choice-cap=%zu"
            " bindings=%u binding-cap=%u constraints=%u constraint-cap=%u"
            " binding-trail=%u binding-trail-cap=%u goal-trail=%zu"
            " binding-prime=%u heap-collect-after=%zu"
            " nursery-live=%zu nursery-reserved=%zu nursery-spare=%zu"
            " tenured-live=%zu tenured-reserved=%zu tenured-spare=%zu"
            " plan-live=%zu plan-reserved=%zu"
            " answer-live=%zu answer-reserved=%zu"
            " table-live=%zu table-reserved=%zu"
            " match-cache=%zu/%zu callability-cache=%zu/%zu"
            " malloc-in-use=%zu malloc-free=%zu malloc-mapped=%zu"
            " goal-kinds=",
            machine->goal_len, machine->goal_cap,
            sizeof(*machine->goals), (void *)machine,
            (unsigned)goal.kind,
            first_head == SYMBOL_ID_NONE
                ? "<none>" : symbol_bytes(g_symbols, first_head),
            goal.plan ? (unsigned)goal.plan->role : UINT_MAX,
            goal.plan ? (unsigned)goal.plan->execution : UINT_MAX,
            machine->choice_len, machine->choice_cap,
            bindings ? bindings->len : 0u,
            bindings ? bindings->cap : 0u,
            bindings ? bindings->eq_len : 0u,
            bindings ? bindings->eq_cap : 0u,
            builder ? builder->trail_len : 0u,
            builder ? builder->trail_cap : 0u,
            machine->goal_trail_len,
            builder && bindings_builder_prime_present(builder) ? 1u : 0u,
            machine->heap_collect_after,
            arena_accounted_live_bytes(&machine->heap),
            machine->heap.reserved_bytes, machine->heap.spare_bytes,
            arena_accounted_live_bytes(&machine->tenured),
            machine->tenured.reserved_bytes,
            machine->tenured.spare_bytes,
            arena_accounted_live_bytes(&machine->plan_arena),
            machine->plan_arena.reserved_bytes,
            arena_accounted_live_bytes(machine->answer_arena),
            machine->answer_arena
                ? machine->answer_arena->reserved_bytes : 0u,
            machine->table_shared
                ? arena_accounted_live_bytes(
                      &machine->table_shared->arena) : 0u,
            machine->table_shared
                ? machine->table_shared->arena.reserved_bytes : 0u,
            machine->match_decision_len,
            machine->match_decision_cap,
            machine->equation_callability_len,
            machine->equation_callability_cap,
            malloc_in_use, malloc_free, malloc_mapped);
        bool wrote_kind = false;
        for (unsigned kind = 0u;
             kind <= PETTA_GOAL_CATCH_READY; kind++) {
            if (counts[kind] != 0u) {
                fprintf(
                    stderr, "%s%u:%" PRIu64,
                    wrote_kind ? "," : "", kind,
                    counts[kind]);
                wrote_kind = true;
            }
        }
        fputs(" choice-kinds=", stderr);
        wrote_kind = false;
        for (unsigned kind = 0u; kind <= PETTA_CHOICE_TABLE; kind++) {
            if (choice_counts[kind] != 0u) {
                fprintf(
                    stderr, "%s%u:%" PRIu64,
                    wrote_kind ? "," : "", kind,
                    choice_counts[kind]);
                wrote_kind = true;
            }
        }
        fputc('\n', stderr);
    }
    if (!petta_machine_reserve(
            (void **)&machine->goals, &machine->goal_cap,
            machine->goal_len + 1u, sizeof(*machine->goals))) {
        return false;
    }
    if (!petta_goal_trail_record_overwrite(
            machine, machine->goal_len)) {
        return false;
    }
    goal.instance_id = ++machine->next_goal_instance;
    machine->goals[machine->goal_len++] = goal;
    if (machine->goal_len > machine->goal_initialized_len)
        machine->goal_initialized_len = machine->goal_len;
    if (machine->goal_len > machine->stats.maximum_goal_depth)
        machine->stats.maximum_goal_depth = machine->goal_len;
    return true;
}

static void petta_machine_release_child(
    PettaMachineImpl *parent, PettaMachine **child) {
    if (!child || !*child)
        return;
    uint64_t started_ns = parent && parent->host.measure_stats
        ? petta_machine_monotonic_ns() : 0u;
    petta_machine_destroy(*child);
    free(*child);
    *child = NULL;
    if (!parent)
        return;
    parent->stats.child_machine_destroy_calls++;
    uint64_t finished_ns = started_ns
        ? petta_machine_monotonic_ns() : 0u;
    if (finished_ns >= started_ns) {
        petta_machine_add_u64(
            &parent->stats.child_machine_destroy_elapsed_ns,
            finished_ns - started_ns);
    }
}

static void petta_choice_release(
    PettaMachineImpl *machine, PettaChoice *choice) {
    if (!machine || !choice)
        return;
    if (choice->kind == PETTA_CHOICE_OUTCOMES &&
        choice->as.outcomes.outcomes) {
        outcome_set_free(choice->as.outcomes.outcomes);
        free(choice->as.outcomes.outcomes);
        choice->as.outcomes.outcomes = NULL;
    }
    if (choice->kind == PETTA_CHOICE_TYPED_CALL) {
        free(choice->as.typed_call.types);
        choice->as.typed_call.types = NULL;
    }
    if (choice->kind == PETTA_CHOICE_CLAUSE) {
        free(choice->as.clause.candidates);
        choice->as.clause.candidates = NULL;
        choice->as.clause.equation_len = 0u;
    }
    if (choice->kind == PETTA_CHOICE_COLLAPSE) {
        petta_machine_release_child(
            machine, &choice->as.collapse.machine);
        free(choice->as.collapse.items);
        choice->as.collapse.items = NULL;
        choice->as.collapse.item_len = 0u;
        choice->as.collapse.item_cap = 0u;
    }
    if (choice->kind == PETTA_CHOICE_COUNT_COLLAPSE) {
        petta_machine_release_child(
            machine, &choice->as.count_collapse.machine);
    }
    if (choice->kind == PETTA_CHOICE_MATCH) {
        free(choice->as.match.snapshot);
        choice->as.match.snapshot = NULL;
        choice->as.match.snapshot_len = 0u;
        binding_set_free(&choice->as.match.binding_snapshot);
        choice->as.match.binding_snapshot_mode = false;
    }
    if (choice->kind == PETTA_CHOICE_TRANSACTION &&
        choice->as.transaction.active) {
        if (machine->host.transaction_rollback) {
            machine->host.transaction_rollback(
                machine->host.context,
                choice->as.transaction.handle);
        }
        machine->space = choice->as.transaction.previous_space;
        choice->as.transaction.active = false;
    }
    if (choice->kind == PETTA_CHOICE_MUTEX &&
        choice->as.mutex.active) {
        if (machine->host.mutex_release) {
            machine->host.mutex_release(
                machine->host.context,
                choice->as.mutex.handle);
        }
        choice->as.mutex.active = false;
    }
    if (choice->kind == PETTA_CHOICE_TABLE) {
        if (choice->as.table.generator) {
            petta_machine_destroy(choice->as.table.generator);
            free(choice->as.table.generator);
            choice->as.table.generator = NULL;
        }
        free(choice->as.table.round_answers);
        choice->as.table.round_answers = NULL;
        choice->as.table.round_len = 0u;
        choice->as.table.round_cap = 0u;
        choice->as.table.generator_query = NULL;
        cetta_var_map_free(
            &choice->as.table.goal_instantiation);
        cetta_var_map_free(
            &choice->as.table.generator_slots);
        if (choice->as.table.round_arena) {
            arena_free(choice->as.table.round_arena);
            free(choice->as.table.round_arena);
            choice->as.table.round_arena = NULL;
        }
        if (choice->as.table.phase !=
                PETTA_TABLE_CHOICE_REPLAY &&
            machine->table_shared) {
            machine->table_shared->failed = true;
        }
    }
}

static bool petta_choice_push_at_goal_trail_mark(
    PettaMachineImpl *machine, PettaChoice choice,
    size_t goal_trail_mark) {
    /*
     * The first clause alternative may be selected before its choice record
     * is installed.  In that case the original continuation remains the
     * prefix below `goal_height`, while newly scheduled branch goals occupy
     * the suffix.  Protect that stable prefix: later stack-slot overwrites
     * are trailed once they occur, so the choice stores only a trail mark and
     * a height rather than copying the continuation eagerly.
     */
    if (choice.goal_height > machine->goal_len ||
        goal_trail_mark > machine->goal_trail_len) {
        petta_choice_release(machine, &choice);
        return false;
    }
    if (machine->choice_len == 0u) {
        /*
         * With no older alternative, retained overwrite history has no
         * observer.  Start the next choice era at an empty trail.
         */
        machine->goal_trail_len = 0u;
        machine->protected_goal_height = 0u;
        goal_trail_mark = 0u;
    }
    if (!choice.heap_mark_captured) {
        choice.heap_mark = arena_mark(&machine->heap);
        choice.heap_mark_captured = true;
    }
    choice.goal_trail_mark = goal_trail_mark;
    choice.type_obligation_mark = machine->type_obligation_len;
    choice.previous_protected_goal_height =
        machine->protected_goal_height;
    machine->stats.choice_continuation_snapshots++;
    if (machine->trace.choice_kind) {
        fprintf(
            stderr,
            "[petta-choice-kind] kind=%u goals=%zu depth=%zu\n",
            (unsigned)choice.kind, choice.goal_height,
            machine->choice_len + 1u);
    }
    if (petta_machine_trace_enabled()) {
        fprintf(
            stderr,
            "[petta-machine] snapshot choice=%u goals=%zu"
            " next-choice-depth=%zu\n",
            (unsigned)choice.kind, choice.goal_height,
            machine->choice_len + 1u);
    }
    if (!petta_machine_reserve(
            (void **)&machine->choices, &machine->choice_cap,
            machine->choice_len + 1u, sizeof(*machine->choices))) {
        petta_choice_release(machine, &choice);
        return false;
    }
    machine->choices[machine->choice_len++] = choice;
    if (choice.goal_height > machine->protected_goal_height)
        machine->protected_goal_height = choice.goal_height;
    if (machine->choice_len > machine->stats.maximum_choice_depth)
        machine->stats.maximum_choice_depth = machine->choice_len;
    return true;
}

static bool petta_choice_restore(
    PettaMachineImpl *machine, PettaChoice *choice) {
    if (!machine || !choice)
        return false;
    search_context_rollback(&machine->search, choice->trail);
    petta_machine_invalidate_activation_frame(machine);
    if (!petta_goal_trail_rollback(
            machine, choice->goal_trail_mark) ||
        choice->goal_height >
            machine->goal_initialized_len) {
        return false;
    }
    machine->goal_len = choice->goal_height;
    if (choice->type_obligation_mark >
        machine->type_obligation_len) {
        return false;
    }
    machine->type_obligation_len =
        choice->type_obligation_mark;
    petta_machine_invalidate_type_obligation_cache(machine);
    if (choice->heap_mark_captured &&
        !choice->retain_heap_across_resume) {
        size_t before = arena_accounted_live_bytes(&machine->heap);
        if (before >
            machine->stats.maximum_nursery_live_bytes) {
            machine->stats.maximum_nursery_live_bytes =
                before;
        }
        arena_reset(&machine->heap, choice->heap_mark);
        machine->stats.choice_heap_resets++;
        size_t after = arena_accounted_live_bytes(&machine->heap);
        if (before > after) {
            machine->stats.choice_heap_bytes_reclaimed +=
                before - after;
        }
    }
    machine->stats.rollbacks++;
    return true;
}

static bool petta_choice_push(
    PettaMachineImpl *machine, PettaChoice choice) {
    return petta_choice_push_at_goal_trail_mark(
        machine, choice, machine->goal_trail_len);
}

static void petta_choice_pop(PettaMachineImpl *machine) {
    if (!machine || machine->choice_len == 0u)
        return;
    PettaChoice *choice =
        &machine->choices[machine->choice_len - 1u];
    size_t previous_protected_goal_height =
        choice->previous_protected_goal_height;
    petta_choice_release(machine, choice);
    machine->choice_len--;
    machine->protected_goal_height =
        previous_protected_goal_height;
    if (machine->choice_len == 0u) {
        machine->protected_goal_height = 0u;
        machine->goal_trail_len = 0u;
    }
}

static void petta_choice_truncate(
    PettaMachineImpl *machine, size_t length) {
    while (machine->choice_len > length)
        petta_choice_pop(machine);
}

static bool petta_choice_exhausted_after_success(
    const PettaChoice *choice) {
    if (!choice)
        return false;
    switch (choice->kind) {
    case PETTA_CHOICE_CLAUSE:
        return choice->as.clause.next_equation >=
               choice->as.clause.equation_len;
    case PETTA_CHOICE_OUTCOMES:
        return !choice->as.outcomes.outcomes ||
               choice->as.outcomes.next >=
                   choice->as.outcomes.outcomes->len;
    case PETTA_CHOICE_SUPERPOSE:
        return !choice->as.superpose.items ||
               choice->as.superpose.next >=
                   choice->as.superpose.items->expr.len;
    case PETTA_CHOICE_BOOLEAN: {
        uint32_t arity = 0u;
        return !choice->as.boolean.expression ||
               !petta_semantics_boolean_relation_arity(
                   atom_head_symbol_id(
                       choice->as.boolean.expression),
                   &arity) ||
               choice->as.boolean.next_row >=
                   (arity == 1u ? 2u : 4u);
    }
    case PETTA_CHOICE_APPEND:
        return !choice->as.append.whole ||
               choice->as.append.next_split >
                   choice->as.append.whole->expr.len;
    case PETTA_CHOICE_MEMBER:
        return choice->as.member.items &&
               choice->as.member.next >=
                   choice->as.member.items->expr.len &&
               (choice->as.member.saw_match ||
                choice->as.member.emitted_false);
    case PETTA_CHOICE_RELATIONAL_MEMBER:
        return choice->as.relational_member.next_clause >= 2u;
    case PETTA_CHOICE_LIST_LENGTH:
        return false;
    case PETTA_CHOICE_TYPED_CALL:
        return choice->as.typed_call.next >=
               choice->as.typed_call.count;
    case PETTA_CHOICE_CASE_DEFAULT:
        return choice->as.case_default.saw_answer;
    case PETTA_CHOICE_EQUAL_DEFAULT:
        return choice->as.equal_default.saw_answer;
    case PETTA_CHOICE_RELATIONAL_EXTENSION:
        return choice->as.relational_extension.fallback_started;
    case PETTA_CHOICE_COLLAPSE:
        return choice->as.collapse.emitted;
    case PETTA_CHOICE_COUNT_COLLAPSE:
        return choice->as.count_collapse.emitted;
    case PETTA_CHOICE_MATCH:
        /*
         * A materialized snapshot has a fixed final occurrence.  A live
         * cursor remains as an invalidation witness even after its current
         * last row, so mutation cannot silently turn invalidation into
         * ordinary exhaustion.
         */
        return choice->as.match.snapshot_mode &&
               choice->as.match.next_index >=
                   choice->as.match.snapshot_len;
    case PETTA_CHOICE_ONCE:
    case PETTA_CHOICE_TRANSACTION:
    case PETTA_CHOICE_MUTEX:
        return false;
    case PETTA_CHOICE_TABLE:
        /*
         * The authoritative answer length lives in the shared table rather
         * than in this record.  Let ordinary backtracking retire the
         * consumer after its final replay instead of guessing here.
         */
        return false;
    }
    return false;
}

/*
 * An exhausted cursor is not a choice point anymore.  Its selected
 * alternative has already installed all required bindings and continuation
 * goals; retaining the dead record turns deterministic recursion into a
 * linear chain of heap continuations and keeps completed host OutcomeSets
 * alive.
 *
 * This is the WAM's "last alternative" discipline in the representation used
 * here: discard only the top exhausted record, after successful selection,
 * and never roll back its trail.  The selected branch remains live while
 * failure correctly falls through to the next older real alternative.
 */
static void petta_choice_retire_exhausted(
    PettaMachineImpl *machine) {
    if (!machine || machine->choice_len == 0u)
        return;
    PettaChoice *choice =
        &machine->choices[machine->choice_len - 1u];
    if (!petta_choice_exhausted_after_success(choice))
        return;
    petta_choice_pop(machine);
}

static Atom *petta_fresh_variable(PettaMachineImpl *machine) {
    return atom_var_with_id(
        &machine->heap, "__petta_machine", fresh_var_id());
}

static bool petta_visible_contains(
    const PettaMachineImpl *machine, VarId id) {
    for (size_t index = 0u; index < machine->visible_len; index++) {
        if (machine->visible[index].id == id)
            return true;
    }
    return false;
}

static bool petta_collect_visible_variable(
    void *context, VarId var_id, SymbolId spelling,
    Atom *name_key) {
    PettaMachineImpl *machine = context;
    if (!machine || var_id == VAR_ID_NONE)
        return false;
    if (petta_visible_contains(machine, var_id))
        return true;
    Atom *variable = atom_var_with_presentation(
        &machine->heap, spelling, name_key, var_id);
    if (!variable)
        return false;
    if (!petta_machine_reserve(
            (void **)&machine->visible, &machine->visible_cap,
            machine->visible_len + 1u,
            sizeof(*machine->visible))) {
        return false;
    }
    machine->visible[machine->visible_len++] =
        (PettaVisibleVariable){var_id, variable};
    return true;
}

static size_t petta_size_add_saturating(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static uint64_t petta_u64_add_saturating(
    uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static void petta_machine_heap_arena_init(Arena *arena) {
    arena_init(arena);
    arena_set_runtime_kind(
        arena, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    arena_set_hashcons(arena, NULL);
}

static size_t petta_machine_total_heap_live(
    const PettaMachineImpl *machine) {
    return machine
        ? petta_size_add_saturating(
              arena_accounted_live_bytes(&machine->heap),
              arena_accounted_live_bytes(&machine->tenured))
        : 0u;
}

/*
 * Let the nursery grow with the live old generation, but cap it at half the
 * old generation so collection memory stays bounded.  The geometric window
 * makes the sum of root scans linear when the live graph itself grows
 * linearly, while retaining an 8 MiB floor for small, allocation-heavy runs.
 */
static size_t petta_machine_nursery_window(
    const PettaMachineImpl *machine) {
    size_t window = machine
        ? arena_accounted_live_bytes(&machine->tenured) / 2u : 0u;
    return window > PETTA_MACHINE_HEAP_WINDOW_BYTES
        ? window : PETTA_MACHINE_HEAP_WINDOW_BYTES;
}

static size_t petta_machine_next_major_threshold(
    size_t tenured_live) {
    size_t growth = tenured_live > PETTA_MACHINE_HEAP_WINDOW_BYTES
        ? tenured_live : PETTA_MACHINE_HEAP_WINDOW_BYTES;
    return petta_size_add_saturating(tenured_live, growth);
}

/*
 * A full choice-aware projection visits every live continuation.  A constant
 * collection interval would therefore reintroduce quadratic administrative
 * work as the choice stack grows.  Bindings already have a maintained
 * open-addressing lookup index, so lookup no longer requires frequent
 * projection.  Keep a fixed floor for small searches and grow the interval
 * with the retained environment and continuation instead.
 */
static size_t petta_binding_window(
    const PettaMachineImpl *machine, size_t retained_entries,
    size_t minimum) {
    size_t live_continuation = machine
        ? machine->goal_len : 0u;
    if (machine &&
        machine->protected_goal_height > live_continuation) {
        live_continuation = machine->protected_goal_height;
    }
    size_t window = retained_entries > live_continuation
        ? retained_entries : live_continuation;
    return window > minimum ? window : minimum;
}

static size_t petta_choice_binding_window(
    const PettaMachineImpl *machine, size_t retained_entries) {
    return petta_binding_window(
        machine, retained_entries,
        PETTA_MACHINE_CHOICE_BINDING_MIN_WINDOW_ENTRIES);
}

static size_t petta_deterministic_binding_window(
    const PettaMachineImpl *machine, size_t retained_entries) {
    return petta_binding_window(
        machine, retained_entries,
        PETTA_MACHINE_BINDING_WINDOW_ENTRIES);
}

static Atom *petta_copy_optional_atom(
    AtomDeepCopySession *session, Atom *atom, bool *ok) {
    if (!atom)
        return NULL;
    Atom *copy = atom_deep_copy_session_copy(session, atom);
    if (!copy)
        *ok = false;
    return copy;
}

static Atom *petta_copy_goal_field(
    AtomDeepCopySession *session, Atom *atom,
    uint8_t source_fields, uint8_t source_bit,
    bool *ok) {
    if ((source_fields & source_bit) == 0u)
        return petta_copy_optional_atom(session, atom, ok);
    if (!atom) {
        *ok = false;
        return NULL;
    }
    cetta_provenance_assert_not_transient(
        atom, "petta.goal.activation-source");
    return atom;
}

static bool petta_copy_binding_atoms(
    Bindings *bindings, Arena *destination,
    AtomDeepCopySession *session) {
    if (!bindings || !destination || !session)
        return false;
    bool ok = true;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        bindings->entries[i].name_key = petta_copy_optional_atom(
            session, bindings->entries[i].name_key, &ok);
        bindings->entries[i].val = petta_copy_optional_atom(
            session, bindings->entries[i].val, &ok);
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        bindings->constraints[i].lhs = petta_copy_optional_atom(
            session, bindings->constraints[i].lhs, &ok);
        bindings->constraints[i].rhs = petta_copy_optional_atom(
            session, bindings->constraints[i].rhs, &ok);
    }
    if (!ok)
        return false;
    if (!bindings->prime_ext)
        return true;
    if (!prime_need_snapshot_promote(
            destination, &bindings->prime_ext->prime_need) ||
        !prime_need_branch_state_promote(
            destination, &bindings->prime_ext->branch_state))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_promote(
            destination, &bindings->prime_ext->receipt))
        return false;
#endif
    return true;
}

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PettaBindingRoots;

typedef struct {
    BindingsEpochRoot *epoch_roots;
    size_t epoch_root_len;
    size_t epoch_root_cap;
    PettaGoal **mark_goals;
    uint32_t *entry_marks;
    size_t mark_len;
    size_t mark_cap;
    size_t entry_mark_cap;
} PettaActivationRoots;

static void petta_activation_roots_free(
    PettaActivationRoots *roots) {
    if (!roots)
        return;
    free(roots->epoch_roots);
    free(roots->mark_goals);
    free(roots->entry_marks);
    memset(roots, 0, sizeof(*roots));
}

static bool petta_activation_roots_add_epoch_root(
    PettaActivationRoots *roots, TermUniverse *universe, Atom *atom,
    uint32_t epoch) {
    if (!roots || !atom || epoch == 0u ||
        !petta_machine_reserve(
            (void **)&roots->epoch_roots,
            &roots->epoch_root_cap,
            roots->epoch_root_len + 1u,
            sizeof(*roots->epoch_roots))) {
        return false;
    }
    const CettaTermVariableSupport *variable_support = NULL;
    if (universe) {
        AtomId id = term_universe_lookup_atom_id(universe, atom);
        /* Support IDs are local to the universe-owned canonical term.  A
         * structurally equivalent transient atom may carry different VarIds,
         * so it must retain the exact traversal fallback. */
        if (id != CETTA_ATOM_ID_NONE &&
            term_universe_get_atom(universe, id) == atom &&
            !term_universe_variable_support(
                universe, id, &variable_support)) {
            variable_support = NULL;
        }
    }
    roots->epoch_roots[roots->epoch_root_len++] =
        (BindingsEpochRoot){
            .atom = atom,
            .epoch = epoch,
            .variable_support = variable_support,
        };
    return true;
}

static bool petta_activation_roots_add_goal(
    PettaActivationRoots *roots, TermUniverse *universe,
    PettaGoal *goal) {
    if (!roots || !goal)
        return false;
    bool equation_query_view =
        goal->equation_query_source != NULL;
    if (goal->activation_source_fields == 0u &&
        !equation_query_view)
        return true;
    if (goal->activation_epoch == 0u ||
        !petta_machine_reserve(
            (void **)&roots->mark_goals,
            &roots->mark_cap, roots->mark_len + 1u,
            sizeof(*roots->mark_goals)) ||
        !petta_machine_reserve(
            (void **)&roots->entry_marks,
            &roots->entry_mark_cap, roots->mark_len + 1u,
            sizeof(*roots->entry_marks))) {
        return false;
    }
    roots->mark_goals[roots->mark_len] = goal;
    roots->entry_marks[roots->mark_len] =
        goal->activation_first_entry;
    roots->mark_len++;
    if ((goal->activation_source_fields &
            PETTA_ACTIVATION_SOURCE_FIRST) != 0u &&
        !petta_activation_roots_add_epoch_root(
            roots, universe, goal->first, goal->activation_epoch)) {
        return false;
    }
    if ((goal->activation_source_fields &
            PETTA_ACTIVATION_SOURCE_SECOND) != 0u &&
        !petta_activation_roots_add_epoch_root(
            roots, universe, goal->second, goal->activation_epoch)) {
        return false;
    }
    if ((goal->activation_source_fields &
            PETTA_ACTIVATION_SOURCE_THIRD) != 0u &&
        !petta_activation_roots_add_epoch_root(
            roots, universe, goal->third, goal->activation_epoch)) {
        return false;
    }
    if ((goal->activation_source_fields &
            PETTA_ACTIVATION_SOURCE_FOURTH) != 0u &&
        !petta_activation_roots_add_epoch_root(
            roots, universe, goal->fourth, goal->activation_epoch)) {
        return false;
    }
    if (equation_query_view &&
        !petta_activation_roots_add_epoch_root(
            roots, universe, goal->equation_query_source,
            goal->activation_epoch)) {
        return false;
    }
    return true;
}

static void petta_activation_roots_commit_marks(
    const PettaActivationRoots *roots) {
    if (!roots)
        return;
    for (size_t i = 0u; i < roots->mark_len; i++)
        roots->mark_goals[i]->activation_first_entry =
            roots->entry_marks[i];
}

static bool petta_activation_roots_apply_to_goal_copy(
    const PettaActivationRoots *roots,
    const PettaGoal *source, PettaGoal *destination,
    size_t goal_count) {
    if (!roots || (!source && goal_count > 0u) ||
        (!destination && goal_count > 0u)) {
        return false;
    }
    for (size_t i = 0u; i < roots->mark_len; i++) {
        const PettaGoal *goal = roots->mark_goals[i];
        if (goal < source || goal >= source + goal_count)
            return false;
        size_t index = (size_t)(goal - source);
        destination[index].activation_first_entry =
            roots->entry_marks[i];
    }
    return true;
}

static bool petta_binding_roots_add(
    PettaBindingRoots *roots, Atom *atom) {
    if (!roots || !atom)
        return roots != NULL;
    if (!petta_machine_reserve(
            (void **)&roots->items, &roots->cap,
            roots->len + 1u, sizeof(*roots->items))) {
        return false;
    }
    roots->items[roots->len++] = atom;
    return true;
}

static bool petta_binding_roots_add_goal(
    PettaBindingRoots *roots,
    PettaActivationRoots *activation_roots,
    TermUniverse *universe,
    PettaGoal *goal) {
    if (!goal || !petta_activation_roots_add_goal(
            activation_roots, universe, goal)) {
        return false;
    }
    return
        (((goal->activation_source_fields &
               PETTA_ACTIVATION_SOURCE_FIRST) != 0u) ||
         petta_binding_roots_add(roots, goal->first)) &&
        (((goal->activation_source_fields &
               PETTA_ACTIVATION_SOURCE_SECOND) != 0u) ||
         petta_binding_roots_add(roots, goal->second)) &&
        (((goal->activation_source_fields &
               PETTA_ACTIVATION_SOURCE_THIRD) != 0u) ||
         petta_binding_roots_add(roots, goal->third)) &&
        (((goal->activation_source_fields &
               PETTA_ACTIVATION_SOURCE_FOURTH) != 0u) ||
         petta_binding_roots_add(roots, goal->fourth));
}

static bool petta_binding_roots_add_map(
    PettaBindingRoots *roots, const CettaVarMap *map) {
    if (!roots || !map)
        return roots != NULL;
    for (uint32_t i = 0u; i < map->len; i++) {
        if (!petta_binding_roots_add(
                roots, map->items[i].mapped_var)) {
            return false;
        }
    }
    return true;
}

static bool petta_binding_roots_add_bindings(
    PettaBindingRoots *roots, const Bindings *bindings) {
    if (!roots || !bindings)
        return roots != NULL;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        if (!petta_binding_roots_add(
                roots, bindings->entries[i].name_key) ||
            !petta_binding_roots_add(
                roots, bindings->entries[i].val)) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        if (!petta_binding_roots_add(
                roots, bindings->constraints[i].lhs) ||
            !petta_binding_roots_add(
                roots, bindings->constraints[i].rhs)) {
            return false;
        }
    }
    return true;
}

static bool petta_binding_roots_add_outcome(
    PettaBindingRoots *roots, const Outcome *outcome) {
    if (!roots || !outcome)
        return roots != NULL;
    if (!petta_binding_roots_add(roots, outcome->atom) ||
        !petta_binding_roots_add(
            roots, outcome->materialized_atom) ||
        !petta_binding_roots_add_bindings(
            roots, &outcome->env)) {
        return false;
    }
    if (outcome->kind != CETTA_OUTCOME_ANSWER_REF)
        return true;
    if (!petta_binding_roots_add_map(
            roots, &outcome->answer_ref.goal_instantiation)) {
        return false;
    }
    const AnswerRecord *record = answer_bank_get(
        outcome->answer_ref.bank, outcome->answer_ref.ref);
    return !record ||
           (petta_binding_roots_add(roots, record->result) &&
            petta_binding_roots_add_bindings(
                roots, &record->bindings));
}

static bool petta_binding_roots_add_choice(
    PettaBindingRoots *roots, const PettaChoice *choice) {
    if (!roots || !choice)
        return roots != NULL;
    switch (choice->kind) {
    case PETTA_CHOICE_CLAUSE:
        return petta_binding_roots_add(
                   roots, choice->as.clause.query) &&
               petta_binding_roots_add(
                   roots, choice->as.clause.expected);
    case PETTA_CHOICE_OUTCOMES:
        if (!petta_binding_roots_add(
                roots, choice->as.outcomes.expected)) {
            return false;
        }
        if (!choice->as.outcomes.outcomes)
            return true;
        for (CettaCount i = 0u;
             i < choice->as.outcomes.outcomes->len; i++) {
            if (!petta_binding_roots_add_outcome(
                    roots,
                    &choice->as.outcomes.outcomes->items[i])) {
                return false;
            }
        }
        return true;
    case PETTA_CHOICE_SUPERPOSE:
        return petta_binding_roots_add(
                   roots, choice->as.superpose.items) &&
               petta_binding_roots_add(
                   roots, choice->as.superpose.expected);
    case PETTA_CHOICE_BOOLEAN:
        return petta_binding_roots_add(
                   roots, choice->as.boolean.expression) &&
               petta_binding_roots_add(
                   roots, choice->as.boolean.expected);
    case PETTA_CHOICE_APPEND:
        return petta_binding_roots_add(
                   roots, choice->as.append.whole) &&
               petta_binding_roots_add(
                   roots, choice->as.append.left) &&
               petta_binding_roots_add(
                   roots, choice->as.append.right);
    case PETTA_CHOICE_MEMBER:
        return petta_binding_roots_add(
                   roots, choice->as.member.needle) &&
               petta_binding_roots_add(
                   roots, choice->as.member.items) &&
               petta_binding_roots_add(
                   roots, choice->as.member.expected);
    case PETTA_CHOICE_RELATIONAL_MEMBER:
        return petta_binding_roots_add(
                   roots, choice->as.relational_member.needle) &&
               petta_binding_roots_add(
                   roots, choice->as.relational_member.items) &&
               petta_binding_roots_add(
                   roots, choice->as.relational_member.expected);
    case PETTA_CHOICE_LIST_LENGTH:
        return petta_binding_roots_add(
                   roots, choice->as.list_length.open_tail) &&
               petta_binding_roots_add(
                   roots, choice->as.list_length.expected);
    case PETTA_CHOICE_MATCH:
        if (!petta_binding_roots_add(
                roots, choice->as.match.pattern) ||
            !petta_binding_roots_add(
                roots, choice->as.match.template) ||
            !petta_binding_roots_add(
                roots, choice->as.match.expected)) {
            return false;
        }
        for (CettaCount i = 0u;
             i < choice->as.match.snapshot_len; i++) {
            if (!petta_binding_roots_add(
                    roots, choice->as.match.snapshot[i])) {
                return false;
            }
        }
        for (CettaCount i = 0u;
             i < choice->as.match.binding_snapshot.len; i++) {
            if (!petta_binding_roots_add_bindings(
                    roots,
                    &choice->as.match.binding_snapshot.items[i])) {
                return false;
            }
        }
        return true;
    case PETTA_CHOICE_TYPED_CALL:
        if (!petta_binding_roots_add(
                roots, choice->as.typed_call.expression) ||
            !petta_binding_roots_add(
                roots, choice->as.typed_call.expected)) {
            return false;
        }
        for (uint32_t i = 0u;
             i < choice->as.typed_call.count; i++) {
            if (!petta_binding_roots_add(
                    roots, choice->as.typed_call.types[i])) {
                return false;
            }
        }
        return true;
    case PETTA_CHOICE_CASE_DEFAULT:
        return petta_binding_roots_add(
                   roots, choice->as.case_default.expression) &&
               petta_binding_roots_add(
                   roots, choice->as.case_default.expected);
    case PETTA_CHOICE_EQUAL_DEFAULT:
        return petta_binding_roots_add(
                   roots, choice->as.equal_default.expected) &&
               petta_binding_roots_add(
                   roots, choice->as.equal_default.guarded_value) &&
               petta_binding_roots_add(
                   roots, choice->as.equal_default.guarded_formal);
    case PETTA_CHOICE_RELATIONAL_EXTENSION:
        return petta_binding_roots_add(
                   roots,
                   choice->as.relational_extension.expression) &&
               petta_binding_roots_add(
                   roots,
                   choice->as.relational_extension.expected);
    case PETTA_CHOICE_COLLAPSE:
        if (!petta_binding_roots_add(
                roots, choice->as.collapse.expected)) {
            return false;
        }
        for (size_t i = 0u;
             i < choice->as.collapse.item_len; i++) {
            if (!petta_binding_roots_add(
                    roots, choice->as.collapse.items[i])) {
                return false;
            }
        }
        return true;
    case PETTA_CHOICE_COUNT_COLLAPSE:
        return petta_binding_roots_add(
            roots, choice->as.count_collapse.expected);
    case PETTA_CHOICE_ONCE:
    case PETTA_CHOICE_TRANSACTION:
    case PETTA_CHOICE_MUTEX:
        return true;
    case PETTA_CHOICE_TABLE:
        if (!petta_binding_roots_add(
                roots, choice->as.table.query) ||
            !petta_binding_roots_add(
                roots, choice->as.table.expected) ||
            !petta_binding_roots_add(
                roots, choice->as.table.generator_query) ||
            !petta_binding_roots_add_map(
                roots,
                &choice->as.table.goal_instantiation) ||
            !petta_binding_roots_add_map(
                roots,
                &choice->as.table.generator_slots)) {
            return false;
        }
        for (size_t i = 0u;
             i < choice->as.table.round_len; i++) {
            if (!petta_binding_roots_add(
                    roots, choice->as.table.round_answers[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool petta_machine_collect_choice_bindings(
    PettaMachineImpl *machine) {
    if (!machine || machine->choice_len == 0u)
        return true;
    PettaBindingRoots roots = {0};
    PettaActivationRoots activation_roots = {0};
    bool ok =
        petta_binding_roots_add(&roots, machine->query) &&
        petta_binding_roots_add(
            &roots, machine->answer_variable);
    for (size_t i = 0u; ok && i < machine->visible_len; i++) {
        ok = petta_binding_roots_add(
            &roots, machine->visible[i].variable);
    }
    for (size_t i = 0u;
         ok && i < machine->type_obligation_len; i++) {
        ok = petta_binding_roots_add(
                 &roots, machine->type_obligations[i].value) &&
             petta_binding_roots_add(
                 &roots, machine->type_obligations[i].formal);
    }
    size_t rooted_goal_len = machine->goal_len >
            machine->protected_goal_height
        ? machine->goal_len : machine->protected_goal_height;
    if (rooted_goal_len > machine->goal_initialized_len) {
        free(roots.items);
        petta_activation_roots_free(&activation_roots);
        return false;
    }
    for (size_t i = 0u; ok && i < rooted_goal_len; i++)
        ok = petta_binding_roots_add_goal(
            &roots, &activation_roots,
            machine->space->native.universe,
            &machine->goals[i]);
    for (size_t i = 0u;
         ok && i < machine->goal_trail_len; i++) {
        ok = petta_binding_roots_add_goal(
            &roots, &activation_roots,
            machine->space->native.universe,
            &machine->goal_trail[i].previous);
    }
    for (size_t i = 0u; ok && i < machine->choice_len; i++)
        ok = petta_binding_roots_add_choice(
            &roots, &machine->choices[i]);
    if (!ok) {
        free(roots.items);
        petta_activation_roots_free(&activation_roots);
        return false;
    }
    if (machine->choice_len >
        SIZE_MAX / sizeof(uint32_t)) {
        free(roots.items);
        petta_activation_roots_free(&activation_roots);
        return false;
    }
    uint32_t *marks = cetta_malloc(
        machine->choice_len * sizeof(*marks));
    for (size_t i = 0u; i < machine->choice_len; i++)
        marks[i] = machine->choices[i].trail.bindings_mark;

    uint64_t discarded_items = 0u;
    uint64_t discarded_trail = 0u;
    ok = bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
        search_context_builder(&machine->search),
        roots.items, roots.len,
        activation_roots.epoch_roots,
        activation_roots.epoch_root_len,
        marks, machine->choice_len,
        activation_roots.entry_marks,
        activation_roots.mark_len,
        &discarded_items, &discarded_trail);
    free(roots.items);
    if (!ok) {
        free(marks);
        petta_activation_roots_free(&activation_roots);
        return false;
    }
    petta_activation_roots_commit_marks(&activation_roots);
    petta_activation_roots_free(&activation_roots);
    petta_machine_invalidate_activation_frame(machine);
    for (size_t i = 0u; i < machine->choice_len; i++)
        machine->choices[i].trail.bindings_mark = marks[i];
    free(marks);

    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    const Bindings *current = bindings_builder_bindings(builder);
    machine->stats.choice_binding_collections++;
    machine->stats.choice_binding_items_discarded +=
        discarded_items;
    machine->stats.choice_trail_entries_discarded +=
        discarded_trail;
    machine->binding_growth_collect_after =
        petta_u64_add_saturating(
            builder->growth_count,
            (uint64_t)petta_choice_binding_window(
                machine, current->len));
    return true;
}

/*
 * Clause-choice nursery evacuation
 * ---------------------------------
 *
 * A clause choice retains an authored alternative and its rollback-visible
 * continuation.  That must not disable nursery reclamation while the chosen
 * branch performs a long deterministic computation.  The logical binding
 * compactor above already identifies the complete rollback-visible root set
 * and translates binding/activation marks.  For the two choice forms below,
 * every remaining machine-heap pointer is enumerated here as well:
 *
 *   - CLAUSE owns only query and expected atoms; its candidates and plans are
 *     immutable program metadata.
 *   - ONCE owns no atom payload.
 *
 * Evacuation is transactional.  All roots are copied into tenured storage
 * before any live slot changes.  Once committed, the nursery is empty and
 * every physical choice mark is rebased to that same empty origin.  Logical
 * binding, goal-trail, and type-obligation marks retain their translated
 * values, so later rollback has exactly the same observable state.  Other
 * choice kinds remain on the conservative non-moving path until they acquire
 * equally complete payload visitors.
 */
static bool petta_choice_heap_evacuation_supported(
    PettaMachineImpl *machine) {
    if (!machine || machine->choice_len == 0u ||
        bindings_builder_prime_present(
            search_context_builder(&machine->search))) {
        return false;
    }
    for (size_t i = 0u; i < machine->choice_len; i++) {
        const PettaChoice *choice = &machine->choices[i];
        if (choice->retain_heap_across_resume ||
            (choice->kind != PETTA_CHOICE_CLAUSE &&
             choice->kind != PETTA_CHOICE_ONCE)) {
            return false;
        }
    }
    return true;
}

static bool petta_copy_goal_atoms(
    AtomDeepCopySession *session, PettaGoal *goal) {
    if (!session || !goal)
        return false;
    bool ok = true;
    goal->first = petta_copy_goal_field(
        session, goal->first, goal->activation_source_fields,
        PETTA_ACTIVATION_SOURCE_FIRST, &ok);
    goal->second = petta_copy_goal_field(
        session, goal->second, goal->activation_source_fields,
        PETTA_ACTIVATION_SOURCE_SECOND, &ok);
    goal->third = petta_copy_goal_field(
        session, goal->third, goal->activation_source_fields,
        PETTA_ACTIVATION_SOURCE_THIRD, &ok);
    goal->fourth = petta_copy_goal_field(
        session, goal->fourth, goal->activation_source_fields,
        PETTA_ACTIVATION_SOURCE_FOURTH, &ok);
    return ok;
}

static bool petta_copy_supported_choice_atoms(
    AtomDeepCopySession *session, PettaChoice *choice) {
    if (!session || !choice)
        return false;
    bool ok = true;
    switch (choice->kind) {
    case PETTA_CHOICE_CLAUSE:
        choice->as.clause.query = petta_copy_optional_atom(
            session, choice->as.clause.query, &ok);
        choice->as.clause.expected = petta_copy_optional_atom(
            session, choice->as.clause.expected, &ok);
        return ok;
    case PETTA_CHOICE_ONCE:
        return true;
    default:
        return false;
    }
}

static bool petta_machine_collect_choice_heap(
    PettaMachineImpl *machine) {
    if (!petta_choice_heap_evacuation_supported(machine))
        return true;
    uint64_t collection_started_ns =
        petta_machine_monotonic_ns();

    /* Checked operands are derived cache entries, not rollback authority. */
    petta_machine_invalidate_type_obligation_cache(machine);
    if (!petta_machine_collect_choice_bindings(machine))
        return false;

    size_t rooted_goal_len = machine->goal_len >
            machine->protected_goal_height
        ? machine->goal_len : machine->protected_goal_height;
    if (rooted_goal_len > machine->goal_initialized_len ||
        rooted_goal_len > SIZE_MAX / sizeof(PettaGoal) ||
        machine->goal_trail_len >
            SIZE_MAX / sizeof(PettaGoalTrailEntry) ||
        machine->choice_len > SIZE_MAX / sizeof(PettaChoice) ||
        machine->visible_len >
            SIZE_MAX / sizeof(PettaVisibleVariable) ||
        machine->type_obligation_len >
            SIZE_MAX / sizeof(PettaTypeObligation)) {
        return false;
    }

    PettaGoal *next_goals = rooted_goal_len
        ? cetta_malloc(rooted_goal_len * sizeof(*next_goals))
        : NULL;
    PettaGoalTrailEntry *next_goal_trail = machine->goal_trail_len
        ? cetta_malloc(
              machine->goal_trail_len * sizeof(*next_goal_trail))
        : NULL;
    PettaChoice *next_choices = machine->choice_len
        ? cetta_malloc(machine->choice_len * sizeof(*next_choices))
        : NULL;
    PettaVisibleVariable *next_visible = machine->visible_len
        ? cetta_malloc(machine->visible_len * sizeof(*next_visible))
        : NULL;
    PettaTypeObligation *next_type_obligations =
        machine->type_obligation_len
            ? cetta_malloc(
                  machine->type_obligation_len *
                      sizeof(*next_type_obligations))
            : NULL;
    if (rooted_goal_len) {
        memcpy(next_goals, machine->goals,
               rooted_goal_len * sizeof(*next_goals));
    }
    if (machine->goal_trail_len) {
        memcpy(next_goal_trail, machine->goal_trail,
               machine->goal_trail_len * sizeof(*next_goal_trail));
    }
    if (machine->choice_len) {
        memcpy(next_choices, machine->choices,
               machine->choice_len * sizeof(*next_choices));
    }
    if (machine->visible_len) {
        memcpy(next_visible, machine->visible,
               machine->visible_len * sizeof(*next_visible));
    }
    if (machine->type_obligation_len) {
        memcpy(next_type_obligations, machine->type_obligations,
               machine->type_obligation_len *
                   sizeof(*next_type_obligations));
    }

    Bindings next_bindings;
    bindings_init(&next_bindings);
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    const Bindings *current = bindings_builder_bindings(builder);
    if (!bindings_clone(&next_bindings, current)) {
        free(next_goals);
        free(next_goal_trail);
        free(next_choices);
        free(next_visible);
        free(next_type_obligations);
        return false;
    }

    size_t before_nursery_bytes =
        arena_accounted_live_bytes(&machine->heap);
    size_t before_tenured_bytes =
        arena_accounted_live_bytes(&machine->tenured);
    ArenaMark destination_mark = arena_mark(&machine->tenured);
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&machine->tenured);
    if (!session) {
        bindings_free(&next_bindings);
        free(next_goals);
        free(next_goal_trail);
        free(next_choices);
        free(next_visible);
        free(next_type_obligations);
        return false;
    }

    bool ok = true;
    Atom *next_query = petta_copy_optional_atom(
        session, machine->query, &ok);
    Atom *next_answer_variable = petta_copy_optional_atom(
        session, machine->answer_variable, &ok);
    Atom *next_raised_error = petta_copy_optional_atom(
        session, machine->raised_error, &ok);
    for (size_t i = 0u; ok && i < machine->visible_len; i++) {
        next_visible[i].variable = petta_copy_optional_atom(
            session, next_visible[i].variable, &ok);
    }
    for (size_t i = 0u;
         ok && i < machine->type_obligation_len; i++) {
        PettaTypeObligation *obligation =
            &next_type_obligations[i];
        obligation->value = petta_copy_optional_atom(
            session, obligation->value, &ok);
        obligation->formal = petta_copy_optional_atom(
            session, obligation->formal, &ok);
        obligation->checked_value = petta_copy_optional_atom(
            session, obligation->checked_value, &ok);
        obligation->checked_formal = petta_copy_optional_atom(
            session, obligation->checked_formal, &ok);
    }
    for (size_t i = 0u; ok && i < rooted_goal_len; i++)
        ok = petta_copy_goal_atoms(session, &next_goals[i]);
    for (size_t i = 0u;
         ok && i < machine->goal_trail_len; i++) {
        ok = petta_copy_goal_atoms(
            session, &next_goal_trail[i].previous);
    }
    for (size_t i = 0u; ok && i < machine->choice_len; i++) {
        ok = petta_copy_supported_choice_atoms(
            session, &next_choices[i]);
    }
    if (ok)
        ok = petta_copy_binding_atoms(
            &next_bindings, &machine->tenured, session);
    atom_deep_copy_session_free(session);

    if (!ok || !next_query || !next_answer_variable) {
        arena_reset(&machine->tenured, destination_mark);
        bindings_free(&next_bindings);
        free(next_goals);
        free(next_goal_trail);
        free(next_choices);
        free(next_visible);
        free(next_type_obligations);
        return false;
    }

    size_t after_tenured_bytes =
        arena_accounted_live_bytes(&machine->tenured);
    size_t promoted_bytes = after_tenured_bytes >= before_tenured_bytes
        ? after_tenured_bytes - before_tenured_bytes : 0u;

    arena_free(&machine->heap);
    petta_machine_heap_arena_init(&machine->heap);
    machine->query = next_query;
    machine->answer_variable = next_answer_variable;
    machine->raised_error = next_raised_error;
    if (rooted_goal_len) {
        memcpy(machine->goals, next_goals,
               rooted_goal_len * sizeof(*next_goals));
    }
    machine->goal_initialized_len = rooted_goal_len;
    if (machine->goal_trail_len) {
        memcpy(machine->goal_trail, next_goal_trail,
               machine->goal_trail_len * sizeof(*next_goal_trail));
    }
    if (machine->choice_len) {
        memcpy(machine->choices, next_choices,
               machine->choice_len * sizeof(*next_choices));
    }
    if (machine->visible_len) {
        memcpy(machine->visible, next_visible,
               machine->visible_len * sizeof(*next_visible));
    }
    if (machine->type_obligation_len) {
        memcpy(machine->type_obligations, next_type_obligations,
               machine->type_obligation_len *
                   sizeof(*next_type_obligations));
    }
    bindings_replace(&builder->current, &next_bindings);
    ArenaMark nursery_origin = arena_mark(&machine->heap);
    for (size_t i = 0u; i < machine->choice_len; i++) {
        machine->choices[i].heap_mark = nursery_origin;
        machine->choices[i].heap_mark_captured = true;
    }
    petta_machine_invalidate_activation_frame(machine);

    free(next_goals);
    free(next_goal_trail);
    free(next_choices);
    free(next_visible);
    free(next_type_obligations);

    machine->stats.choice_nursery_evacuations++;
    machine->stats.choice_nursery_goal_roots_scanned +=
        (uint64_t)rooted_goal_len + machine->goal_trail_len;
    machine->stats.choice_nursery_bytes_evacuated +=
        (uint64_t)promoted_bytes;
    if (before_nursery_bytes > promoted_bytes) {
        machine->stats.choice_nursery_bytes_reclaimed +=
            (uint64_t)(before_nursery_bytes - promoted_bytes);
    }
    machine->heap_collect_after =
        petta_machine_nursery_window(machine);
    if (petta_goal_growth_trace_enabled()) {
        fprintf(
            stderr,
            "[petta-choice-evacuation] choices=%zu goals=%zu"
            " goal-trail=%zu bindings=%u nursery-before=%zu"
            " evacuated=%zu tenured-after=%zu next-window=%zu\n",
            machine->choice_len, rooted_goal_len,
            machine->goal_trail_len, builder->current.len,
            before_nursery_bytes, promoted_bytes,
            arena_accounted_live_bytes(&machine->tenured),
            machine->heap_collect_after);
    }
    uint64_t collection_finished_ns =
        petta_machine_monotonic_ns();
    if (collection_finished_ns >= collection_started_ns) {
        petta_machine_add_u64(
            &machine->stats.choice_nursery_evacuation_elapsed_ns,
            collection_finished_ns - collection_started_ns);
    }
    return true;
}

static bool petta_machine_owned_profile_supported(
    const PettaMachineImpl *machine) {
    size_t rooted_goal_len = machine &&
            machine->goal_len > machine->protected_goal_height
        ? machine->goal_len
        : machine ? machine->protected_goal_height : 0u;
    if (!machine || !machine->search.owns_scratch_arena ||
        machine->typecheck_exit_code != 0 ||
        machine->table_generator != PETTA_TABLE_ENTRY_NONE ||
        (machine->table_shared &&
         machine->table_shared->entry_len != 0u) ||
        machine->goal_len > machine->goal_initialized_len ||
        machine->protected_goal_height >
            machine->goal_initialized_len ||
        machine->goal_trail_len > machine->goal_trail_cap ||
        machine->choice_len > machine->choice_cap ||
        machine->visible_len > machine->visible_cap ||
        machine->type_obligation_len >
            machine->type_obligation_cap) {
        return false;
    }
    const BindingsBuilder *builder = &machine->search.bindings;
    for (size_t i = 0u; i < machine->goal_trail_len; i++) {
        if (machine->goal_trail[i].index >= rooted_goal_len) {
            return false;
        }
    }
    for (size_t i = 0u; i < machine->choice_len; i++) {
        const PettaChoice *choice = &machine->choices[i];
        if (choice->retain_heap_across_resume ||
            choice->goal_height > machine->goal_initialized_len ||
            choice->goal_trail_mark > machine->goal_trail_len ||
            choice->type_obligation_mark >
                machine->type_obligation_len ||
            choice->trail.bindings_mark > builder->trail_len ||
            (choice->kind != PETTA_CHOICE_CLAUSE &&
             choice->kind != PETTA_CHOICE_ONCE)) {
            return false;
        }
        if (choice->kind == PETTA_CHOICE_CLAUSE &&
            (choice->as.clause.next_equation >
                 choice->as.clause.equation_len ||
             (choice->as.clause.equation_len != 0u &&
              !choice->as.clause.candidates))) {
            return false;
        }
    }
    return true;
}

static CettaContinuationStatus
petta_machine_owned_profile_admission(
    PettaMachineImpl *machine,
    CettaBranchAuthorityToken *authority) {
    if (!machine || !authority ||
        !machine->host.admit_branch_capture) {
        return CETTA_CONTINUATION_DEFERRED;
    }
    CettaBranchAdmission admission =
        machine->host.admit_branch_capture(
            machine->host.context, machine->space);
    if (admission.status == CETTA_BRANCH_ADMISSION_DEFERRED)
        return CETTA_CONTINUATION_DEFERRED;
    if (admission.status == CETTA_BRANCH_ADMISSION_INVALIDATED)
        return CETTA_CONTINUATION_INVALIDATED;
    if (admission.status != CETTA_BRANCH_ADMISSION_AVAILABLE ||
        !cetta_branch_capture_admits(
            admission.capacity,
            CETTA_BRANCH_STORAGE_OWNED_MULTI_SHOT) ||
        admission.authority.length >
            CETTA_BRANCH_AUTHORITY_TOKEN_WORD_CAPACITY) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    *authority = admission.authority;
    return CETTA_CONTINUATION_READY;
}

static void petta_owned_continuation_choices_free(
    PettaChoice *choices, size_t choice_len) {
    if (!choices)
        return;
    for (size_t i = 0u; i < choice_len; i++) {
        if (choices[i].kind == PETTA_CHOICE_CLAUSE) {
            free(choices[i].as.clause.candidates);
            choices[i].as.clause.candidates = NULL;
            choices[i].as.clause.equation_len = 0u;
        }
    }
    free(choices);
}

static bool petta_owned_continuation_choices_clone(
    PettaChoice **out, const PettaChoice *source,
    size_t choice_len) {
    if (!out || (choice_len != 0u && !source) ||
        choice_len > SIZE_MAX / sizeof(**out)) {
        return false;
    }
    *out = choice_len
        ? cetta_malloc(choice_len * sizeof(**out)) : NULL;
    for (size_t i = 0u; i < choice_len; i++) {
        (*out)[i] = source[i];
        (*out)[i].heap_mark = (ArenaMark){0};
        (*out)[i].heap_mark_captured = false;
        if (source[i].kind != PETTA_CHOICE_CLAUSE)
            continue;
        (*out)[i].as.clause.candidates = NULL;
        size_t len = source[i].as.clause.equation_len;
        if (len == 0u)
            continue;
        if (!source[i].as.clause.candidates ||
            len > SIZE_MAX / sizeof(PettaClauseCandidate)) {
            petta_owned_continuation_choices_free(*out, i + 1u);
            *out = NULL;
            return false;
        }
        (*out)[i].as.clause.candidates =
            cetta_malloc(len * sizeof(PettaClauseCandidate));
        memcpy(
            (*out)[i].as.clause.candidates,
            source[i].as.clause.candidates,
            len * sizeof(PettaClauseCandidate));
    }
    return true;
}

static PettaContinuationTermPool *petta_continuation_term_pool_new(void) {
    PettaContinuationTermPool *pool = cetta_malloc(sizeof(*pool));
    atomic_init(&pool->references, 1u);
    atomic_init(&pool->reclaim_after_bytes, (size_t)1u << 20u);
    arena_init(&pool->owner);
    arena_set_runtime_kind(
        &pool->owner, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&pool->owner, NULL);
    return pool;
}

static bool petta_continuation_term_pool_retain(
        PettaContinuationTermPool *pool) {
    if (!pool)
        return false;
    size_t references = atomic_load_explicit(
        &pool->references, memory_order_relaxed);
    for (;;) {
        if (references == 0u || references == SIZE_MAX)
            return false;
        if (atomic_compare_exchange_weak_explicit(
                &pool->references, &references, references + 1u,
                memory_order_relaxed, memory_order_relaxed)) {
            return true;
        }
    }
}

static void petta_continuation_term_pool_release(
        PettaContinuationTermPool *pool) {
    if (!pool)
        return;
    size_t prior = atomic_fetch_sub_explicit(
        &pool->references, 1u, memory_order_acq_rel);
    assert(prior != 0u);
    if (prior != 1u)
        return;
    arena_free(&pool->owner);
    free(pool);
}

static size_t petta_continuation_term_pool_bytes(
        const PettaContinuationTermPool *pool) {
    if (!pool)
        return 0u;
    size_t atoms = arena_accounted_live_bytes(&pool->owner);
    return atoms > SIZE_MAX - sizeof(*pool)
        ? SIZE_MAX : atoms + sizeof(*pool);
}

static void petta_owned_continuation_impl_free(
    PettaOwnedContinuationImpl *continuation) {
    if (!continuation)
        return;
    petta_owned_continuation_choices_free(
        continuation->control.choices,
        continuation->control.choice_len);
    free(continuation->control.goals);
    free(continuation->control.goal_trail);
    free(continuation->obligations.visible);
    free(continuation->obligations.type_obligations);
    bindings_builder_free(&continuation->bindings.bindings);
    petta_continuation_term_pool_release(
        continuation->terms.pool);
    free(continuation);
}

static bool petta_owned_continuation_copy_atoms_with_session(
    PettaOwnedContinuationImpl *continuation,
    Arena *destination,
    AtomDeepCopySession *session) {
    if (!continuation || !destination || !session)
        return false;
    bool ok = true;
    continuation->readout.query = petta_copy_optional_atom(
        session, continuation->readout.query, &ok);
    continuation->readout.answer_variable = petta_copy_optional_atom(
        session, continuation->readout.answer_variable, &ok);
    continuation->readout.raised_error = petta_copy_optional_atom(
        session, continuation->readout.raised_error, &ok);
    for (size_t i = 0u;
         ok && i < continuation->obligations.visible_len; i++) {
        continuation->obligations.visible[i].variable =
            petta_copy_optional_atom(
                session,
                continuation->obligations.visible[i].variable,
                &ok);
    }
    for (size_t i = 0u;
         ok && i < continuation->obligations.type_obligation_len; i++) {
        PettaTypeObligation *obligation =
            &continuation->obligations.type_obligations[i];
        obligation->value = petta_copy_optional_atom(
            session, obligation->value, &ok);
        obligation->formal = petta_copy_optional_atom(
            session, obligation->formal, &ok);
        obligation->checked_value = petta_copy_optional_atom(
            session, obligation->checked_value, &ok);
        obligation->checked_formal = petta_copy_optional_atom(
            session, obligation->checked_formal, &ok);
    }
    for (size_t i = 0u;
         ok && i < continuation->control.goal_initialized_len; i++) {
        ok = petta_copy_goal_atoms(
            session, &continuation->control.goals[i]);
    }
    for (size_t i = 0u;
         ok && i < continuation->control.goal_trail_len; i++) {
        ok = petta_copy_goal_atoms(
            session,
            &continuation->control.goal_trail[i].previous);
    }
    for (size_t i = 0u;
         ok && i < continuation->control.choice_len; i++) {
        ok = petta_copy_supported_choice_atoms(
            session, &continuation->control.choices[i]);
    }
    if (ok) {
        ok = bindings_promote_logical_atoms_with_session(
            &continuation->bindings.bindings.current, session);
    }
    if (ok) {
        ok = bindings_builder_promote_prime_atoms_to_arena(
            &continuation->bindings.bindings, destination);
    }
    return ok && continuation->readout.query &&
        continuation->readout.answer_variable &&
        bindings_logical_atoms_closed_for_arena(
            &continuation->bindings.bindings.current, destination);
}

static bool petta_owned_continuation_copy_atoms(
    PettaOwnedContinuationImpl *continuation,
    Arena *destination) {
    if (!continuation || !destination)
        return false;
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(destination);
    if (!session)
        return false;
    bool ok = petta_owned_continuation_copy_atoms_with_session(
        continuation, destination, session);
    atom_deep_copy_session_free(session);
    return ok;
}

static size_t petta_owned_continuation_vector_bytes(
    const PettaOwnedContinuationImpl *continuation);

static PettaOwnedContinuationImpl *
petta_owned_continuation_clone_to_term_pool(
        const PettaOwnedContinuationImpl *source,
        PettaContinuationTermPool *pool,
        AtomDeepCopySession *session) {
    if (!source || !source->terms.pool || !pool || !session ||
        source->control.goal_len >
            source->control.goal_initialized_len ||
        source->control.protected_goal_height >
            source->control.goal_initialized_len ||
        (source->control.goal_initialized_len != 0u &&
         !source->control.goals) ||
        (source->control.goal_trail_len != 0u &&
         !source->control.goal_trail) ||
        (source->control.choice_len != 0u &&
         !source->control.choices) ||
        (source->obligations.visible_len != 0u &&
         !source->obligations.visible) ||
        (source->obligations.type_obligation_len != 0u &&
         !source->obligations.type_obligations) ||
        source->control.goal_initialized_len >
            SIZE_MAX / sizeof(PettaGoal) ||
        source->control.goal_trail_len >
            SIZE_MAX / sizeof(PettaGoalTrailEntry) ||
        source->obligations.visible_len >
            SIZE_MAX / sizeof(PettaVisibleVariable) ||
        source->obligations.type_obligation_len >
            SIZE_MAX / sizeof(PettaTypeObligation) ||
        !petta_continuation_term_pool_retain(pool)) {
        return NULL;
    }

    PettaOwnedContinuationImpl *copy = cetta_malloc(sizeof(*copy));
    memset(copy, 0, sizeof(*copy));
    copy->authority = source->authority;
    copy->terms.pool = pool;
    copy->control.goal_len = source->control.goal_len;
    copy->control.goal_initialized_len =
        source->control.goal_initialized_len;
    copy->control.protected_goal_height =
        source->control.protected_goal_height;
    copy->control.goal_trail_len = source->control.goal_trail_len;
    copy->control.choice_len = source->control.choice_len;
    copy->control.next_goal_instance =
        source->control.next_goal_instance;
    copy->control.binding_growth_collect_after =
        source->control.binding_growth_collect_after;
    copy->obligations.visible_len = source->obligations.visible_len;
    copy->obligations.type_obligation_len =
        source->obligations.type_obligation_len;
    copy->obligations.next_type_obligation_id =
        source->obligations.next_type_obligation_id;
    copy->readout = source->readout;

    bool ok = bindings_builder_clone(
        &copy->bindings.bindings,
        &source->bindings.bindings);
    if (ok && copy->control.goal_initialized_len != 0u) {
        size_t bytes = copy->control.goal_initialized_len *
            sizeof(*copy->control.goals);
        copy->control.goals = cetta_malloc(bytes);
        memcpy(copy->control.goals, source->control.goals, bytes);
    }
    if (ok && copy->control.goal_trail_len != 0u) {
        size_t bytes = copy->control.goal_trail_len *
            sizeof(*copy->control.goal_trail);
        copy->control.goal_trail = cetta_malloc(bytes);
        memcpy(
            copy->control.goal_trail,
            source->control.goal_trail, bytes);
    }
    if (ok) {
        ok = petta_owned_continuation_choices_clone(
            &copy->control.choices, source->control.choices,
            copy->control.choice_len);
    }
    if (ok && copy->obligations.visible_len != 0u) {
        size_t bytes = copy->obligations.visible_len *
            sizeof(*copy->obligations.visible);
        copy->obligations.visible = cetta_malloc(bytes);
        memcpy(
            copy->obligations.visible,
            source->obligations.visible, bytes);
    }
    if (ok && copy->obligations.type_obligation_len != 0u) {
        size_t bytes = copy->obligations.type_obligation_len *
            sizeof(*copy->obligations.type_obligations);
        copy->obligations.type_obligations = cetta_malloc(bytes);
        memcpy(
            copy->obligations.type_obligations,
            source->obligations.type_obligations, bytes);
    }
    if (ok) {
        ok = petta_owned_continuation_copy_atoms_with_session(
            copy, &pool->owner, session);
    }
    if (!ok) {
        petta_owned_continuation_impl_free(copy);
        return NULL;
    }
    copy->exclusive_vector_bytes =
        petta_owned_continuation_vector_bytes(copy);
    return copy;
}

static size_t petta_continuation_next_reclaim_threshold(
        size_t live_bytes) {
    const size_t minimum_growth = (size_t)1u << 20u;
    size_t additive = live_bytes > SIZE_MAX - minimum_growth
        ? SIZE_MAX : live_bytes + minimum_growth;
    size_t doubled = live_bytes > SIZE_MAX / 2u
        ? SIZE_MAX : live_bytes * 2u;
    return additive > doubled ? additive : doubled;
}

static bool petta_continuation_reclaim_due(const void *payload) {
    const PettaOwnedContinuationImpl *continuation = payload;
    PettaContinuationTermPool *pool = continuation
        ? continuation->terms.pool : NULL;
    return pool && petta_continuation_term_pool_bytes(pool) >=
        atomic_load_explicit(
            &pool->reclaim_after_bytes, memory_order_relaxed);
}

static CettaContinuationStatus petta_continuation_reclaim_terms(
        const void *const *source_payloads, size_t length,
        void ***replacement_payloads,
        CettaContinuationReclamationReceipt *receipt) {
    if (!source_payloads || length == 0u || !replacement_payloads ||
        *replacement_payloads || !receipt ||
        length > SIZE_MAX / sizeof(void *)) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    *receipt = (CettaContinuationReclamationReceipt){0};
    const PettaOwnedContinuationImpl *first = source_payloads[0];
    PettaContinuationTermPool *old_pool =
        first ? first->terms.pool : NULL;
    if (!old_pool)
        return CETTA_CONTINUATION_UNSUPPORTED;
    for (size_t i = 0u; i < length; i++) {
        const PettaOwnedContinuationImpl *source = source_payloads[i];
        if (!source || source->terms.pool != old_pool)
            return CETTA_CONTINUATION_UNSUPPORTED;
    }

    size_t shared_before =
        petta_continuation_term_pool_bytes(old_pool);
    size_t threshold = atomic_load_explicit(
        &old_pool->reclaim_after_bytes, memory_order_relaxed);
    if (shared_before < threshold)
        return CETTA_CONTINUATION_DEFERRED;

    size_t exclusive_before = 0u;
    for (size_t i = 0u; i < length; i++) {
        const PettaOwnedContinuationImpl *source = source_payloads[i];
        exclusive_before = petta_size_add_saturating(
            exclusive_before, source->exclusive_vector_bytes);
    }

    PettaContinuationTermPool *new_pool =
        petta_continuation_term_pool_new();
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&new_pool->owner);
    void **prepared = calloc(length, sizeof(*prepared));
    if (!session || !prepared) {
        atom_deep_copy_session_free(session);
        free(prepared);
        petta_continuation_term_pool_release(new_pool);
        return CETTA_CONTINUATION_CAPACITY;
    }
    bool ok = true;
    size_t exclusive_after = 0u;
    for (size_t i = 0u; ok && i < length; i++) {
        prepared[i] = petta_owned_continuation_clone_to_term_pool(
            source_payloads[i], new_pool, session);
        ok = prepared[i] != NULL;
        if (ok) {
            exclusive_after = petta_size_add_saturating(
                exclusive_after,
                ((PettaOwnedContinuationImpl *)prepared[i])
                    ->exclusive_vector_bytes);
        }
    }
    atom_deep_copy_session_free(session);
    if (!ok) {
        for (size_t i = 0u; i < length; i++)
            petta_owned_continuation_impl_free(prepared[i]);
        free(prepared);
        petta_continuation_term_pool_release(new_pool);
        return CETTA_CONTINUATION_CAPACITY;
    }

    size_t shared_after =
        petta_continuation_term_pool_bytes(new_pool);
    atomic_store_explicit(
        &new_pool->reclaim_after_bytes,
        petta_continuation_next_reclaim_threshold(shared_after),
        memory_order_relaxed);
    *receipt = (CettaContinuationReclamationReceipt){
        .live_occurrences = length,
        .shared_bytes_before = shared_before,
        .shared_bytes_after = shared_after,
        .exclusive_bytes_before = exclusive_before,
        .exclusive_bytes_after = exclusive_after,
    };
    petta_continuation_term_pool_release(new_pool);
    *replacement_payloads = prepared;
    return CETTA_CONTINUATION_READY;
}

static size_t petta_continuation_array_bytes(
        size_t count, size_t item_size) {
    return item_size != 0u && count <= SIZE_MAX / item_size
        ? count * item_size : SIZE_MAX;
}

static size_t petta_continuation_component_exclusive_bytes(
        const PettaOwnedContinuationImpl *continuation,
        CettaContinuationComponent component) {
    if (!continuation ||
        component < CETTA_CONTINUATION_COMPONENT_AUTHORITY ||
        component >= CETTA_CONTINUATION_COMPONENT_COUNT) {
        return 0u;
    }
    size_t nested_fixed =
        sizeof(continuation->authority) +
        sizeof(continuation->terms) +
        sizeof(continuation->bindings) +
        sizeof(continuation->control) +
        sizeof(continuation->obligations) +
        sizeof(continuation->readout);
    size_t bytes = 0u;
    switch (component) {
    case CETTA_CONTINUATION_COMPONENT_AUTHORITY:
        bytes = sizeof(continuation->authority);
        if (nested_fixed <= sizeof(*continuation)) {
            bytes = petta_size_add_saturating(
                bytes, sizeof(*continuation) - nested_fixed);
        }
        break;
    case CETTA_CONTINUATION_COMPONENT_TERMS:
        bytes = sizeof(continuation->terms);
        break;
    case CETTA_CONTINUATION_COMPONENT_BINDINGS: {
        const BindingsBuilder *builder =
            &continuation->bindings.bindings;
        bytes = sizeof(continuation->bindings);
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                builder->current.cap, sizeof(Binding)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                builder->current.eq_cap,
                sizeof(BindingConstraint)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                builder->trail_cap,
                sizeof(BindingsBuilderTrailEntry)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                builder->prime_trail_cap,
                sizeof(PrimeOccurrence)));
        if (builder->current.prime_ext) {
            bytes = petta_size_add_saturating(
                bytes, sizeof(PrimeOccurrence));
        }
        break;
    }
    case CETTA_CONTINUATION_COMPONENT_CONTROL:
        bytes = sizeof(continuation->control);
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                continuation->control.goal_initialized_len,
                sizeof(PettaGoal)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                continuation->control.goal_trail_len,
                sizeof(PettaGoalTrailEntry)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                continuation->control.choice_len,
                sizeof(PettaChoice)));
        for (size_t i = 0u;
             i < continuation->control.choice_len; i++) {
            if (continuation->control.choices[i].kind !=
                PETTA_CHOICE_CLAUSE) {
                continue;
            }
            bytes = petta_size_add_saturating(
                bytes, petta_continuation_array_bytes(
                    continuation->control.choices[i]
                        .as.clause.equation_len,
                    sizeof(PettaClauseCandidate)));
        }
        break;
    case CETTA_CONTINUATION_COMPONENT_OBLIGATIONS:
        bytes = sizeof(continuation->obligations);
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                continuation->obligations.visible_len,
                sizeof(PettaVisibleVariable)));
        bytes = petta_size_add_saturating(
            bytes, petta_continuation_array_bytes(
                continuation->obligations.type_obligation_len,
                sizeof(PettaTypeObligation)));
        break;
    case CETTA_CONTINUATION_COMPONENT_READOUT:
        bytes = sizeof(continuation->readout);
        break;
    case CETTA_CONTINUATION_COMPONENT_COUNT:
        return 0u;
    }
    return bytes;
}

static size_t petta_owned_continuation_vector_bytes(
        const PettaOwnedContinuationImpl *continuation) {
    if (!continuation)
        return 0u;
    size_t bytes = 0u;
    for (CettaContinuationComponent component =
             CETTA_CONTINUATION_COMPONENT_AUTHORITY;
         component < CETTA_CONTINUATION_COMPONENT_COUNT;
         component++) {
        bytes = petta_size_add_saturating(
            bytes,
            petta_continuation_component_exclusive_bytes(
                continuation, component));
    }
    return bytes;
}

static bool petta_continuation_component_storage(
        const void *payload,
        CettaContinuationComponent component,
        CettaContinuationStorage *storage) {
    const PettaOwnedContinuationImpl *continuation = payload;
    if (!continuation || !storage ||
        component < CETTA_CONTINUATION_COMPONENT_AUTHORITY ||
        component >= CETTA_CONTINUATION_COMPONENT_COUNT) {
        return false;
    }
    *storage = (CettaContinuationStorage){
        .exclusive_bytes =
            petta_continuation_component_exclusive_bytes(
                continuation, component),
    };
    if (component == CETTA_CONTINUATION_COMPONENT_TERMS) {
        storage->shared_identity = continuation->terms.pool;
        storage->shared_bytes =
            petta_continuation_term_pool_bytes(
                continuation->terms.pool);
    }
    return true;
}

static bool petta_continuation_storage(
    const void *payload,
    CettaContinuationStorage *storage) {
    const PettaOwnedContinuationImpl *continuation = payload;
    if (!continuation || !storage) {
        return false;
    }
    *storage = (CettaContinuationStorage){
        .shared_identity = continuation->terms.pool,
        .shared_bytes = petta_continuation_term_pool_bytes(
            continuation->terms.pool),
        .exclusive_bytes = continuation->exclusive_vector_bytes,
    };
    return true;
}

typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
} PettaContinuationTraceWriter;

static bool petta_continuation_trace_reserve(
        PettaContinuationTraceWriter *writer, size_t additional) {
    if (!writer || additional >
            CETTA_CONTINUATION_TRACE_BYTE_LIMIT - writer->length) {
        return false;
    }
    size_t required = writer->length + additional;
    if (required <= writer->capacity)
        return true;
    size_t capacity = writer->capacity ? writer->capacity : 256u;
    while (capacity < required) {
        size_t next = capacity * 2u;
        if (next < capacity ||
            next > CETTA_CONTINUATION_TRACE_BYTE_LIMIT) {
            capacity = CETTA_CONTINUATION_TRACE_BYTE_LIMIT;
            break;
        }
        capacity = next;
    }
    uint8_t *bytes = realloc(writer->bytes, capacity);
    if (!bytes)
        return false;
    writer->bytes = bytes;
    writer->capacity = capacity;
    return true;
}

static bool petta_continuation_trace_append(
        PettaContinuationTraceWriter *writer,
        const void *bytes, size_t length) {
    if (!petta_continuation_trace_reserve(writer, length))
        return false;
    if (length != 0u)
        memcpy(&writer->bytes[writer->length], bytes, length);
    writer->length += length;
    return true;
}

static bool petta_continuation_trace_u8(
        PettaContinuationTraceWriter *writer, uint8_t value) {
    return petta_continuation_trace_append(writer, &value, 1u);
}

static bool petta_continuation_trace_u32(
        PettaContinuationTraceWriter *writer, uint32_t value) {
    uint8_t bytes[4];
    for (size_t i = 0u; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(value >> (8u * i));
    return petta_continuation_trace_append(
        writer, bytes, sizeof(bytes));
}

static bool petta_continuation_trace_u64(
        PettaContinuationTraceWriter *writer, uint64_t value) {
    uint8_t bytes[8];
    for (size_t i = 0u; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(value >> (8u * i));
    return petta_continuation_trace_append(
        writer, bytes, sizeof(bytes));
}

static bool petta_continuation_trace_atom(
        PettaContinuationTraceWriter *writer, Arena *scratch,
        Atom *atom) {
    if (!atom)
        return petta_continuation_trace_u32(writer, UINT32_MAX);
    ArenaMark mark = arena_mark(scratch);
    char *rendered = atom_to_parseable_string_petta(scratch, atom);
    size_t length = strlen(rendered);
    bool appended = length <= UINT32_MAX &&
        petta_continuation_trace_u32(writer, (uint32_t)length) &&
        petta_continuation_trace_append(writer, rendered, length);
    arena_reset(scratch, mark);
    return appended;
}

static CettaContinuationStatus petta_continuation_trace(
        const void *payload, CettaContinuationTrace *trace) {
    const PettaOwnedContinuationImpl *continuation = payload;
    if (!continuation || !trace || trace->bytes ||
        trace->length != 0u || trace->projection_identity != 0u ||
        continuation->control.goal_len > UINT32_MAX ||
        continuation->bindings.bindings.current.len > UINT32_MAX ||
        continuation->bindings.bindings.current.eq_len > UINT32_MAX) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    PettaContinuationTraceWriter writer = {0};
    Arena scratch;
    arena_init(&scratch);
    static const uint8_t schema[] = {
        'p', 'e', 't', 't', 'a', '-', 'c', 'o', 'n', 't', '-', 'v', '2'
    };
    /* The query is already the persisted model condition through its key.
     * Repeating it in every candidate trace would count shared context as
     * candidate evidence and bias compression rate toward shorter traces. */
    bool ok = petta_continuation_trace_append(
            &writer, schema, sizeof(schema)) &&
        petta_continuation_trace_u32(
            &writer, (uint32_t)continuation->control.goal_len);
    for (size_t i = 0u;
         ok && i < continuation->control.goal_len; i++) {
        const PettaGoal *goal = &continuation->control.goals[i];
        ok = petta_continuation_trace_u32(
                &writer, (uint32_t)goal->kind) &&
            petta_continuation_trace_u8(
                &writer, goal->activation_source_fields) &&
            petta_continuation_trace_u8(
                &writer, goal->first_operand_resolved ? 1u : 0u) &&
            petta_continuation_trace_atom(
                &writer, &scratch, goal->first) &&
            petta_continuation_trace_atom(
                &writer, &scratch, goal->second) &&
            petta_continuation_trace_atom(
                &writer, &scratch, goal->third) &&
            petta_continuation_trace_atom(
                &writer, &scratch, goal->fourth);
    }
    const Bindings *bindings =
        &continuation->bindings.bindings.current;
    if (ok)
        ok = petta_continuation_trace_u32(
            &writer, bindings->len);
    for (uint32_t i = 0u; ok && i < bindings->len; i++) {
        const Binding *binding = &bindings->entries[i];
        ok = petta_continuation_trace_u64(
                &writer, (uint64_t)binding->var_id) &&
            petta_continuation_trace_u32(
                &writer, (uint32_t)binding->spelling) &&
            petta_continuation_trace_u8(
                &writer, binding->legacy_name_fallback ? 1u : 0u) &&
            petta_continuation_trace_atom(
                &writer, &scratch, binding->name_key) &&
            petta_continuation_trace_atom(
                &writer, &scratch, binding->val);
    }
    if (ok)
        ok = petta_continuation_trace_u32(
            &writer, bindings->eq_len);
    for (uint32_t i = 0u; ok && i < bindings->eq_len; i++) {
        ok = petta_continuation_trace_atom(
                &writer, &scratch, bindings->constraints[i].lhs) &&
            petta_continuation_trace_atom(
                &writer, &scratch, bindings->constraints[i].rhs);
    }
    arena_free(&scratch);
    if (!ok || writer.length == 0u) {
        free(writer.bytes);
        return CETTA_CONTINUATION_CAPACITY;
    }
    trace->bytes = writer.bytes;
    trace->length = writer.length;
    trace->projection_identity =
        UINT64_C(0x5054544154524332); /* "PTTATRC2" */
    return CETTA_CONTINUATION_READY;
}

static CettaContinuationStatus petta_continuation_capture(
    void *opaque_machine, void **payload) {
    PettaMachine *machine = opaque_machine;
    if (!machine || !machine->impl || !payload || *payload) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    PettaMachineImpl *source = machine->impl;
    source->stats.owned_continuation_capture_attempts++;
    CettaBranchAuthorityToken authority = {0};
    CettaContinuationStatus admitted =
        petta_machine_owned_profile_admission(
            source, &authority);
    if (admitted != CETTA_CONTINUATION_READY) {
        if (admitted == CETTA_CONTINUATION_DEFERRED)
            source->stats.owned_continuation_capture_deferred++;
        else if (admitted ==
                 CETTA_CONTINUATION_INVALIDATED)
            source->stats.owned_continuation_capture_invalidated++;
        else
            source->stats.owned_continuation_capture_unsupported++;
        return admitted;
    }
    if (source->terminal ||
        !petta_machine_owned_profile_supported(source)) {
        source->stats.owned_continuation_capture_unsupported++;
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    SpaceReadToken read = space_read_token(source->space);
    if (!space_read_token_matches_live_space(read, source->space)) {
        source->stats.owned_continuation_capture_invalidated++;
        return CETTA_CONTINUATION_INVALIDATED;
    }

    PettaOwnedContinuationImpl *owned =
        cetta_malloc(sizeof(*owned));
    memset(owned, 0, sizeof(*owned));
    if (source->continuation_term_pool) {
        if (!petta_continuation_term_pool_retain(
                source->continuation_term_pool)) {
            free(owned);
            return CETTA_CONTINUATION_CAPACITY;
        }
        owned->terms.pool = source->continuation_term_pool;
    } else {
        owned->terms.pool = petta_continuation_term_pool_new();
    }
    ArenaMark term_mark = arena_mark(&owned->terms.pool->owner);
    size_t term_bytes_before = arena_accounted_live_bytes(
        &owned->terms.pool->owner);
    owned->authority.source_machine_instance = source->instance_id;
    owned->authority.space_read = read;
    owned->authority.capture_authority = authority;
    owned->control.goal_len = source->goal_len;
    owned->control.goal_initialized_len =
        source->goal_len > source->protected_goal_height
            ? source->goal_len
            : source->protected_goal_height;
    owned->control.protected_goal_height =
        source->protected_goal_height;
    owned->control.goal_trail_len = source->goal_trail_len;
    owned->control.choice_len = source->choice_len;
    owned->obligations.visible_len = source->visible_len;
    owned->obligations.type_obligation_len =
        source->type_obligation_len;
    owned->obligations.next_type_obligation_id =
        source->next_type_obligation_id;
    owned->readout.query = source->query;
    owned->readout.answer_variable = source->answer_variable;
    owned->readout.raised_error = source->raised_error;
    owned->readout.yielded = source->yielded;
    owned->readout.suspended_choice = source->suspended_choice;
    owned->readout.terminal = source->terminal;
    owned->readout.count_only_emission =
        source->count_only_emission;
    owned->readout.pending_answer_weight =
        source->pending_answer_weight;
    owned->readout.last_answer_weight = source->last_answer_weight;
    owned->readout.terminal_step = source->terminal_step;
    owned->control.next_goal_instance = source->next_goal_instance;
    owned->control.binding_growth_collect_after =
        source->binding_growth_collect_after;

    bool ok =
        owned->control.goal_initialized_len <=
            SIZE_MAX / sizeof(*owned->control.goals) &&
        owned->control.goal_trail_len <=
            SIZE_MAX / sizeof(*owned->control.goal_trail) &&
        owned->obligations.visible_len <=
            SIZE_MAX / sizeof(*owned->obligations.visible) &&
        owned->obligations.type_obligation_len <=
            SIZE_MAX /
                sizeof(*owned->obligations.type_obligations) &&
        bindings_builder_clone(
            &owned->bindings.bindings,
            &source->search.bindings);
    if (ok && owned->control.goal_initialized_len) {
        owned->control.goals = cetta_malloc(
            owned->control.goal_initialized_len *
                sizeof(*owned->control.goals));
        memcpy(
            owned->control.goals, source->goals,
            owned->control.goal_initialized_len *
                sizeof(*owned->control.goals));
    }
    if (ok && owned->control.goal_trail_len) {
        owned->control.goal_trail = cetta_malloc(
            owned->control.goal_trail_len *
                sizeof(*owned->control.goal_trail));
        memcpy(
            owned->control.goal_trail, source->goal_trail,
            owned->control.goal_trail_len *
                sizeof(*owned->control.goal_trail));
    }
    if (ok) {
        ok = petta_owned_continuation_choices_clone(
            &owned->control.choices, source->choices,
            owned->control.choice_len);
    }
    if (ok && owned->obligations.visible_len) {
        owned->obligations.visible = cetta_malloc(
            owned->obligations.visible_len *
                sizeof(*owned->obligations.visible));
        memcpy(
            owned->obligations.visible, source->visible,
            owned->obligations.visible_len *
                sizeof(*owned->obligations.visible));
    }
    if (ok && owned->obligations.type_obligation_len) {
        owned->obligations.type_obligations = cetta_malloc(
            owned->obligations.type_obligation_len *
                sizeof(*owned->obligations.type_obligations));
        memcpy(
            owned->obligations.type_obligations,
            source->type_obligations,
            owned->obligations.type_obligation_len *
                sizeof(*owned->obligations.type_obligations));
    }
    if (ok)
        ok = petta_owned_continuation_copy_atoms(
            owned, &owned->terms.pool->owner);

    CettaBranchAuthorityToken confirmed_authority = {0};
    CettaContinuationStatus confirmed =
        petta_machine_owned_profile_admission(
            source, &confirmed_authority);
    bool current =
        space_read_token_matches_live_space(
            read, source->space) &&
        confirmed == CETTA_CONTINUATION_READY &&
        cetta_branch_authority_token_equal(
            &authority, &confirmed_authority);
    if (!ok || !current) {
        arena_reset(&owned->terms.pool->owner, term_mark);
        petta_owned_continuation_impl_free(owned);
        if (!current) {
            source->stats.owned_continuation_capture_invalidated++;
            return CETTA_CONTINUATION_INVALIDATED;
        }
        return CETTA_CONTINUATION_CAPACITY;
    }
    size_t term_bytes_after = arena_accounted_live_bytes(
        &owned->terms.pool->owner);
    size_t term_bytes_added = term_bytes_after >= term_bytes_before
        ? term_bytes_after - term_bytes_before : 0u;
    owned->exclusive_vector_bytes =
        petta_owned_continuation_vector_bytes(owned);
    source->stats.owned_continuation_captures++;
    petta_machine_add_u64(
        &source->stats.owned_continuation_atom_bytes_captured,
        (uint64_t)term_bytes_added);
    petta_machine_add_u64(
        &source->stats.owned_continuation_vector_bytes_captured,
        (uint64_t)owned->exclusive_vector_bytes);
    *payload = owned;
    return CETTA_CONTINUATION_READY;
}

static void petta_continuation_control_rebase_marks(
    PettaContinuationControlComponent *control, ArenaMark scratch_origin,
    ArenaMark heap_origin) {
    if (!control)
        return;
    for (size_t i = 0u; i < control->choice_len; i++) {
        control->choices[i].trail.scratch_mark = scratch_origin;
        control->choices[i].trail.has_scratch_mark = true;
        control->choices[i].heap_mark = heap_origin;
        control->choices[i].heap_mark_captured = true;
    }
    for (size_t i = 0u;
         i < control->goal_initialized_len; i++) {
        if (control->goals[i].catch_trail.has_scratch_mark)
            control->goals[i].catch_trail.scratch_mark =
                scratch_origin;
    }
    for (size_t i = 0u; i < control->goal_trail_len; i++) {
        if (control->goal_trail[i]
                .previous.catch_trail.has_scratch_mark) {
            control->goal_trail[i]
                .previous.catch_trail.scratch_mark = scratch_origin;
        }
    }
}

static CettaContinuationStatus petta_continuation_restore(
    void *opaque_machine, void **payload) {
    PettaMachine *machine = opaque_machine;
    if (!machine || !machine->impl || !payload || !*payload) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    PettaMachineImpl *destination = machine->impl;
    PettaOwnedContinuationImpl *saved = *payload;
    if (destination->instance_id !=
            saved->authority.source_machine_instance ||
        !petta_machine_owned_profile_supported(destination)) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    CettaBranchAuthorityToken authority = {0};
    CettaContinuationStatus admitted =
        petta_machine_owned_profile_admission(
            destination, &authority);
    if (admitted != CETTA_CONTINUATION_READY)
        return admitted;
    if (!space_read_token_matches_live_space(
            saved->authority.space_read, destination->space) ||
        !cetta_branch_authority_token_equal(
            &saved->authority.capture_authority, &authority)) {
        destination->stats.owned_continuation_restore_invalidated++;
        return CETTA_CONTINUATION_INVALIDATED;
    }

    CettaBranchAuthorityToken confirmed_authority = {0};
    CettaContinuationStatus confirmed =
        petta_machine_owned_profile_admission(
            destination, &confirmed_authority);
    bool current =
        space_read_token_matches_live_space(
            saved->authority.space_read, destination->space) &&
        confirmed == CETTA_CONTINUATION_READY &&
        cetta_branch_authority_token_equal(
            &saved->authority.capture_authority,
            &confirmed_authority);
    if (!current) {
        destination->stats.owned_continuation_restore_invalidated++;
        return CETTA_CONTINUATION_INVALIDATED;
    }
    if (!saved->terms.pool)
        return CETTA_CONTINUATION_UNSUPPORTED;

    uint64_t next_goal_instance = destination->next_goal_instance;
    uint64_t next_type_obligation_id =
        destination->next_type_obligation_id;
    /* Restore consumes one continuation handle.  Its branch-local vectors
     * are exclusively owned, while its immutable term pool may also serve
     * latent siblings.  Validate first, then transfer both the vectors and
     * this handle's pool reference into the stable machine fields. */
    petta_machine_invalidate_activation_frame(destination);
    cetta_gslt_ground_dense_workspace_discard_match_v1(
        &destination->equation_template_c0_workspace);
    petta_choice_truncate(destination, 0u);
    free(destination->choices);
    free(destination->goals);
    free(destination->goal_trail);
    free(destination->visible);
    free(destination->type_obligations);
    destination->choices = NULL;
    destination->goals = NULL;
    destination->goal_trail = NULL;
    destination->visible = NULL;
    destination->type_obligations = NULL;
    search_context_free(&destination->search);
    arena_free(&destination->heap);
    petta_machine_heap_arena_init(&destination->heap);
    arena_free(&destination->tenured);
    petta_machine_heap_arena_init(&destination->tenured);
    arena_set_runtime_kind(
        &destination->tenured,
        CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    arena_set_hashcons(&destination->tenured, NULL);
    petta_continuation_term_pool_release(
        destination->continuation_term_pool);
    destination->continuation_term_pool = saved->terms.pool;
    saved->terms.pool = NULL;

    memset(&destination->search, 0, sizeof(destination->search));
    arena_init(&destination->search.owned_scratch_arena);
    arena_set_runtime_kind(
        &destination->search.owned_scratch_arena,
        CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    destination->search.scratch_arena =
        &destination->search.owned_scratch_arena;
    destination->search.owns_scratch_arena = true;
    destination->search.bindings = saved->bindings.bindings;
    saved->bindings.bindings = (BindingsBuilder){0};

    destination->goals = saved->control.goals;
    destination->goal_len = saved->control.goal_len;
    destination->goal_initialized_len =
        saved->control.goal_initialized_len;
    destination->goal_cap = saved->control.goal_initialized_len;
    destination->protected_goal_height =
        saved->control.protected_goal_height;
    saved->control.goals = NULL;
    saved->control.goal_len = 0u;
    saved->control.goal_initialized_len = 0u;
    destination->goal_trail = saved->control.goal_trail;
    destination->goal_trail_len = saved->control.goal_trail_len;
    destination->goal_trail_cap = saved->control.goal_trail_len;
    saved->control.goal_trail = NULL;
    saved->control.goal_trail_len = 0u;
    destination->choices = saved->control.choices;
    destination->choice_len = saved->control.choice_len;
    destination->choice_cap = saved->control.choice_len;
    saved->control.choices = NULL;
    saved->control.choice_len = 0u;
    destination->visible = saved->obligations.visible;
    destination->visible_len = saved->obligations.visible_len;
    destination->visible_cap = saved->obligations.visible_len;
    saved->obligations.visible = NULL;
    saved->obligations.visible_len = 0u;
    destination->type_obligations =
        saved->obligations.type_obligations;
    destination->type_obligation_len =
        saved->obligations.type_obligation_len;
    destination->type_obligation_cap =
        saved->obligations.type_obligation_len;
    saved->obligations.type_obligations = NULL;
    saved->obligations.type_obligation_len = 0u;

    destination->query = saved->readout.query;
    destination->answer_variable = saved->readout.answer_variable;
    destination->raised_error = saved->readout.raised_error;
    destination->yielded = saved->readout.yielded;
    destination->suspended_choice = saved->readout.suspended_choice;
    destination->terminal = saved->readout.terminal;
    destination->count_only_emission =
        saved->readout.count_only_emission;
    destination->pending_answer_weight =
        saved->readout.pending_answer_weight;
    destination->last_answer_weight =
        saved->readout.last_answer_weight;
    destination->terminal_step = saved->readout.terminal_step;
    destination->next_goal_instance = next_goal_instance >
            saved->control.next_goal_instance
        ? next_goal_instance : saved->control.next_goal_instance;
    destination->next_type_obligation_id =
        next_type_obligation_id >
                saved->obligations.next_type_obligation_id
            ? next_type_obligation_id
            : saved->obligations.next_type_obligation_id;

    ArenaMark scratch_origin = arena_mark(
        destination->search.scratch_arena);
    ArenaMark heap_origin = arena_mark(&destination->heap);
    PettaContinuationControlComponent installed = {
        .goals = destination->goals,
        .goal_initialized_len =
            destination->goal_initialized_len,
        .goal_trail = destination->goal_trail,
        .goal_trail_len = destination->goal_trail_len,
        .choices = destination->choices,
        .choice_len = destination->choice_len,
    };
    petta_continuation_control_rebase_marks(
        &installed, scratch_origin, heap_origin);

    petta_machine_invalidate_type_obligation_cache(destination);
    destination->heap_collect_after =
        petta_machine_nursery_window(destination);
    destination->tenured_major_after =
        petta_machine_next_major_threshold(
            arena_accounted_live_bytes(&destination->tenured));
    destination->binding_growth_collect_after =
        saved->control.binding_growth_collect_after;
    destination->stats.owned_continuation_restores++;

    petta_owned_continuation_impl_free(saved);
    *payload = NULL;
    return CETTA_CONTINUATION_READY;
}

static void petta_continuation_destroy(void *payload) {
    petta_owned_continuation_impl_free(payload);
}

static bool petta_machine_resume_choice(
    PettaMachineImpl *machine, PettaChoice *choice,
    PettaMachineStep *failure);

static bool petta_machine_clause_expansion_supported(
    const PettaMachineImpl *machine) {
    if (!machine || machine->terminal || machine->yielded ||
        machine->suspended_choice || machine->choice_len == 0u ||
        (machine->host.record_clause_use &&
         !machine->host.begin_relation_call)) {
        return false;
    }
    size_t clause_index = machine->choice_len - 1u;
    if (clause_index != 0u &&
        (!machine->host.control_plan ||
         machine->host.control_plan->readout !=
             CETTA_OBSERVATION_FIRST)) {
        return false;
    }
    for (size_t i = 0u; i < clause_index; i++) {
        /*
         * A delimited first-answer scope is linear until it accepts a
         * witness.  It may therefore surround the clause choice being
         * split without becoming another branch of the frontier.  Every
         * other prefix choice still carries an unpartitioned alternative
         * and must be refused rather than duplicated across successors.
         */
        if (machine->choices[i].kind != PETTA_CHOICE_ONCE)
            return false;
    }
    const PettaChoice *choice = &machine->choices[clause_index];
    bool occurrence_scoped =
        machine->host.begin_relation_call != NULL;
    return choice->kind == PETTA_CHOICE_CLAUSE &&
        ((choice->as.clause.call_occurrence != 0u) ==
         occurrence_scoped) &&
        !choice->as.clause.translate_result &&
        !choice->as.clause.count_collection_result &&
        choice->as.clause.next_equation <
            choice->as.clause.equation_len &&
        machine->goal_len > choice->goal_height;
}

bool petta_machine_external_branch_ready(const PettaMachine *machine) {
    return machine && machine->impl &&
        machine->impl->host.externalize_clause_choices &&
        petta_machine_clause_expansion_supported(machine->impl) &&
        petta_machine_owned_profile_supported(machine->impl);
}

static bool petta_owned_continuation_compact_committed_bindings(
    PettaOwnedContinuationImpl *continuation) {
    if (!continuation || continuation->control.choice_len != 0u ||
        continuation->bindings.bindings.trail_len != 0u ||
        continuation->bindings.bindings.prime_trail_len != 0u) {
        return false;
    }

    PettaBindingRoots roots = {0};
    PettaActivationRoots activation_roots = {0};
    size_t rooted_goal_len =
        continuation->control.goal_len >
                continuation->control.protected_goal_height
            ? continuation->control.goal_len
            : continuation->control.protected_goal_height;
    bool compacted =
        rooted_goal_len <= continuation->control.goal_initialized_len &&
        petta_binding_roots_add(
            &roots, continuation->readout.query) &&
        petta_binding_roots_add(
            &roots, continuation->readout.answer_variable) &&
        petta_binding_roots_add(
            &roots, continuation->readout.raised_error);
    for (size_t i = 0u;
         compacted && i < continuation->obligations.visible_len; i++) {
        compacted = petta_binding_roots_add(
            &roots, continuation->obligations.visible[i].variable);
    }
    for (size_t i = 0u;
         compacted &&
             i < continuation->obligations.type_obligation_len;
         i++) {
        PettaTypeObligation *obligation =
            &continuation->obligations.type_obligations[i];
        compacted =
            petta_binding_roots_add(&roots, obligation->value) &&
            petta_binding_roots_add(&roots, obligation->formal) &&
            petta_binding_roots_add(
                &roots, obligation->checked_value) &&
            petta_binding_roots_add(
                &roots, obligation->checked_formal);
    }
    for (size_t i = 0u; compacted && i < rooted_goal_len; i++) {
        compacted = petta_binding_roots_add_goal(
            &roots, &activation_roots, NULL,
            &continuation->control.goals[i]);
    }
    for (size_t i = 0u;
         compacted && i < continuation->control.goal_trail_len; i++) {
        compacted = petta_binding_roots_add_goal(
            &roots, &activation_roots, NULL,
            &continuation->control.goal_trail[i].previous);
    }
    uint64_t discarded_items = 0u;
    uint64_t discarded_trail = 0u;
    if (compacted) {
        compacted =
            bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
                &continuation->bindings.bindings,
                roots.items, roots.len,
                activation_roots.epoch_roots,
                activation_roots.epoch_root_len,
                NULL, 0u,
                activation_roots.entry_marks,
                activation_roots.mark_len,
                &discarded_items, &discarded_trail);
    }
    free(roots.items);
    if (compacted)
        petta_activation_roots_commit_marks(&activation_roots);
    petta_activation_roots_free(&activation_roots);
    return compacted && discarded_trail == 0u;
}

static bool petta_owned_continuation_commit_expanded_clause(
    PettaOwnedContinuationImpl *continuation) {
    if (!continuation || continuation->control.choice_len == 0u ||
        !continuation->control.choices) {
        return false;
    }
    size_t clause_index = continuation->control.choice_len - 1u;
    for (size_t i = 0u; i < clause_index; i++) {
        if (continuation->control.choices[i].kind !=
            PETTA_CHOICE_ONCE) {
            return false;
        }
    }
    PettaChoice clause = continuation->control.choices[clause_index];
    if (clause.kind != PETTA_CHOICE_CLAUSE ||
        clause.goal_trail_mark >
            continuation->control.goal_trail_len ||
        clause.previous_protected_goal_height >
            continuation->control.goal_initialized_len) {
        return false;
    }

    free(continuation->control.choices[clause_index]
             .as.clause.candidates);
    continuation->control.choices[clause_index]
        .as.clause.candidates = NULL;
    continuation->control.choice_len = clause_index;
    if (clause_index == 0u) {
        free(continuation->control.choices);
        continuation->control.choices = NULL;
        bindings_builder_commit(&continuation->bindings.bindings);
    } else {
        continuation->control.choices = cetta_realloc(
            continuation->control.choices,
            clause_index * sizeof(*continuation->control.choices));
    }
    continuation->control.protected_goal_height =
        clause.previous_protected_goal_height;
    continuation->control.goal_trail_len = clause.goal_trail_mark;
    if (continuation->control.goal_trail_len == 0u) {
        free(continuation->control.goal_trail);
        continuation->control.goal_trail = NULL;
    } else {
        continuation->control.goal_trail = cetta_realloc(
            continuation->control.goal_trail,
            continuation->control.goal_trail_len *
                sizeof(*continuation->control.goal_trail));
    }

    if (clause_index == 0u &&
        !petta_owned_continuation_compact_committed_bindings(
            continuation)) {
        return false;
    }
    continuation->exclusive_vector_bytes =
        petta_owned_continuation_vector_bytes(continuation);
    return true;
}

static void petta_continuation_payload_array_destroy(
    void **payloads, size_t length) {
    if (!payloads)
        return;
    for (size_t i = 0u; i < length; i++)
        petta_owned_continuation_impl_free(payloads[i]);
    free(payloads);
}

static bool petta_continuation_payload_array_push(
    void ***payloads, size_t *length, size_t *capacity,
    void *payload) {
    if (!payloads || !length || !capacity || !payload)
        return false;
    if (*length == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 4u;
        if (next < *capacity || next > SIZE_MAX / sizeof(**payloads))
            return false;
        *payloads = cetta_realloc(
            *payloads, next * sizeof(**payloads));
        *capacity = next;
    }
    (*payloads)[(*length)++] = payload;
    return true;
}

static CettaContinuationStatus petta_machine_note_expansion_status(
    PettaMachineImpl *machine, CettaContinuationStatus status,
    size_t successors) {
    if (!machine)
        return status;
    if (status == CETTA_CONTINUATION_READY) {
        machine->stats.owned_continuation_expansions++;
        petta_machine_add_u64(
            &machine->stats.owned_continuation_expansion_successors,
            (uint64_t)successors);
    } else if (status == CETTA_CONTINUATION_DEFERRED) {
        machine->stats.owned_continuation_expansion_deferred++;
    } else if (status == CETTA_CONTINUATION_INVALIDATED) {
        machine->stats.owned_continuation_expansion_invalidated++;
    } else if (status == CETTA_CONTINUATION_CAPACITY) {
        machine->stats.owned_continuation_expansion_capacity++;
    } else {
        machine->stats.owned_continuation_expansion_unsupported++;
    }
    return status;
}

static CettaContinuationStatus petta_relational_continuation_expand(
    void *opaque_machine, void ***payloads, size_t *length) {
    PettaMachine *machine = opaque_machine;
    if (!machine || !machine->impl || !payloads || *payloads ||
        !length || *length != 0u) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    PettaMachineImpl *source = machine->impl;
    source->stats.owned_continuation_expansion_attempts++;
    if (source->choice_len == 0u) {
        return petta_machine_note_expansion_status(
            source, CETTA_CONTINUATION_DEFERRED, 0u);
    }
    if (!petta_machine_clause_expansion_supported(source) ||
        !petta_machine_owned_profile_supported(source)) {
        return petta_machine_note_expansion_status(
            source, CETTA_CONTINUATION_UNSUPPORTED, 0u);
    }

    PettaMachineStats baseline_stats = source->stats;
    bool establish_shared_terms =
        source->continuation_term_pool == NULL;
    void *restore_payload = NULL;
    CettaContinuationStatus status = petta_continuation_capture(
        machine, &restore_payload);
    if (status != CETTA_CONTINUATION_READY) {
        source->stats = baseline_stats;
        return petta_machine_note_expansion_status(
            source, status, 0u);
    }
    if (establish_shared_terms) {
        status = petta_continuation_restore(
            machine, &restore_payload);
        if (status != CETTA_CONTINUATION_READY) {
            petta_owned_continuation_impl_free(restore_payload);
            source->stats = baseline_stats;
            return petta_machine_note_expansion_status(
                source, status, 0u);
        }
        status = petta_continuation_capture(
            machine, &restore_payload);
        if (status != CETTA_CONTINUATION_READY) {
            source->stats = baseline_stats;
            return petta_machine_note_expansion_status(
                source, status, 0u);
        }
    }

    void **successors = NULL;
    size_t successor_len = 0u;
    size_t successor_cap = 0u;
    for (;;) {
        void *successor = NULL;
        status = petta_continuation_capture(machine, &successor);
        if (status != CETTA_CONTINUATION_READY)
            break;
        if (!petta_owned_continuation_commit_expanded_clause(successor) ||
            !petta_continuation_payload_array_push(
                &successors, &successor_len, &successor_cap,
                successor)) {
            petta_owned_continuation_impl_free(successor);
            status = CETTA_CONTINUATION_CAPACITY;
            break;
        }

        PettaChoice *choice =
            &source->choices[source->choice_len - 1u];
        PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
        if (!petta_machine_resume_choice(source, choice, &failure)) {
            if (failure == PETTA_MACHINE_STEP_EXHAUSTED)
                status = CETTA_CONTINUATION_READY;
            else if (failure == PETTA_MACHINE_STEP_INVALIDATED)
                status = CETTA_CONTINUATION_INVALIDATED;
            else if (failure == PETTA_MACHINE_STEP_CAPACITY)
                status = CETTA_CONTINUATION_CAPACITY;
            else
                status = CETTA_CONTINUATION_UNSUPPORTED;
            break;
        }
        if (petta_choice_exhausted_after_success(choice)) {
            void *last = NULL;
            status = petta_continuation_capture(machine, &last);
            if (status == CETTA_CONTINUATION_READY &&
                petta_owned_continuation_commit_expanded_clause(last) &&
                petta_continuation_payload_array_push(
                    &successors, &successor_len, &successor_cap,
                    last)) {
                status = CETTA_CONTINUATION_READY;
            } else {
                petta_owned_continuation_impl_free(last);
                if (status == CETTA_CONTINUATION_READY)
                    status = CETTA_CONTINUATION_CAPACITY;
            }
            break;
        }
    }

    CettaContinuationStatus restored =
        petta_continuation_restore(machine, &restore_payload);
    source->stats = baseline_stats;
    if (restored != CETTA_CONTINUATION_READY) {
        petta_owned_continuation_impl_free(restore_payload);
        petta_continuation_payload_array_destroy(
            successors, successor_len);
        return petta_machine_note_expansion_status(
            source, restored, 0u);
    }
    if (status != CETTA_CONTINUATION_READY || successor_len == 0u) {
        petta_continuation_payload_array_destroy(
            successors, successor_len);
        return petta_machine_note_expansion_status(
            source,
            status == CETTA_CONTINUATION_READY
                ? CETTA_CONTINUATION_UNSUPPORTED : status,
            0u);
    }

    *payloads = successors;
    *length = successor_len;
    return petta_machine_note_expansion_status(
        source, CETTA_CONTINUATION_READY, successor_len);
}

static const CettaContinuationProvider kPettaContinuationProvider = {
    .representation_name = "shared-terms-owned-state",
    .ownership = {
        .capture = petta_continuation_capture,
        .restore = petta_continuation_restore,
        .destroy = petta_continuation_destroy,
        .storage = petta_continuation_storage,
    },
    .branching = {
        .expand = petta_relational_continuation_expand,
    },
    .projection = {
        .trace = petta_continuation_trace,
    },
    .components = {
        .representation = {
            [CETTA_CONTINUATION_COMPONENT_AUTHORITY] =
                "revision-authority",
            [CETTA_CONTINUATION_COMPONENT_TERMS] =
                "shared-term-pool",
            [CETTA_CONTINUATION_COMPONENT_BINDINGS] =
                "owned-abt-bindings",
            [CETTA_CONTINUATION_COMPONENT_CONTROL] =
                "owned-control-vectors",
            [CETTA_CONTINUATION_COMPONENT_OBLIGATIONS] =
                "owned-obligation-vectors",
            [CETTA_CONTINUATION_COMPONENT_READOUT] =
                "inline-readout-state",
        },
        .storage = petta_continuation_component_storage,
    },
    .maintenance = {
        .reclaim_due = petta_continuation_reclaim_due,
        .reclaim = petta_continuation_reclaim_terms,
    },
};

CettaContinuationMachine petta_machine_continuation_machine(
    PettaMachine *machine) {
    return (CettaContinuationMachine){
        .machine = machine,
        .provider = &kPettaContinuationProvider,
    };
}

/*
 * Deterministic generational collection
 * -------------------------------------
 *
 * With no choice point, no rollback state is semantically reachable.  The
 * pending goals, answer projection, and their transitive logical bindings are
 * therefore the complete root set.
 *
 * New atoms live in `heap` (the nursery).  A minor collection copies only
 * nursery survivors into `tenured`; destination-owned subgraphs retain their
 * identity.  Since atoms are immutable, new parents may point to older
 * children but an old atom edge can never point into the nursery.  Each
 * surviving node is therefore promoted at most once between major
 * collections, instead of being recopied on every safe point.
 *
 * The tenured arena is compacted at geometric growth thresholds.  These major
 * collections bound stale promoted storage while making the sum of full-heap
 * copying linear in total growth.  Choice-bearing states continue to use the
 * separate logical compactor above until every choice payload has both a
 * relocation visitor and a liveness visitor.
 */
static bool petta_machine_collect_deterministic_heap(
    PettaMachineImpl *machine) {
    if (!machine || machine->choice_len != 0u)
        return true;
    uint64_t collection_started_ns =
        petta_machine_monotonic_ns();
    if (machine->goal_len > (SIZE_MAX - 2u - machine->visible_len) / 4u)
        return false;
    size_t root_count =
        2u + machine->visible_len + machine->goal_len * 4u;
    if (machine->type_obligation_len >
        (SIZE_MAX - root_count) / 4u) {
        return false;
    }
    root_count += machine->type_obligation_len * 4u;
    if (root_count > SIZE_MAX / sizeof(Atom *))
        return false;
    Atom **roots = cetta_malloc(root_count * sizeof(*roots));
    PettaActivationRoots activation_roots = {0};
    size_t root_index = 0u;
    roots[root_index++] = machine->query;
    roots[root_index++] = machine->answer_variable;
    for (size_t i = 0u; i < machine->visible_len; i++)
        roots[root_index++] = machine->visible[i].variable;
    for (size_t i = 0u;
         i < machine->type_obligation_len; i++) {
        roots[root_index++] = machine->type_obligations[i].value;
        roots[root_index++] = machine->type_obligations[i].formal;
        roots[root_index++] =
            machine->type_obligations[i].checked_value;
        roots[root_index++] =
            machine->type_obligations[i].checked_formal;
    }
    for (size_t i = 0u; i < machine->goal_len; i++) {
        PettaGoal *goal = &machine->goals[i];
        if (!petta_activation_roots_add_goal(
                &activation_roots,
                machine->space->native.universe,
                goal)) {
            free(roots);
            petta_activation_roots_free(&activation_roots);
            return false;
        }
        roots[root_index++] =
            (goal->activation_source_fields &
                 PETTA_ACTIVATION_SOURCE_FIRST) != 0u
                ? NULL : goal->first;
        roots[root_index++] =
            (goal->activation_source_fields &
                 PETTA_ACTIVATION_SOURCE_SECOND) != 0u
                ? NULL : goal->second;
        roots[root_index++] =
            (goal->activation_source_fields &
                 PETTA_ACTIVATION_SOURCE_THIRD) != 0u
                ? NULL : goal->third;
        roots[root_index++] =
            (goal->activation_source_fields &
                 PETTA_ACTIVATION_SOURCE_FOURTH) != 0u
                ? NULL : goal->fourth;
    }
    assert(root_index == root_count);

    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    const Bindings *current = bindings_builder_bindings(builder);
    Bindings projected;
    if (!bindings_project_reachable_with_epoch_roots_and_entry_marks(
            current, roots, root_count,
            activation_roots.epoch_roots,
            activation_roots.epoch_root_len,
            activation_roots.entry_marks,
            activation_roots.mark_len,
            &projected)) {
        free(roots);
        petta_activation_roots_free(&activation_roots);
        return false;
    }
    free(roots);

    PettaGoal *next_goals = NULL;
    PettaVisibleVariable *next_visible = NULL;
    PettaTypeObligation *next_type_obligations = NULL;
    if (machine->goal_len > 0u) {
        if (machine->goal_len >
            SIZE_MAX / sizeof(*next_goals)) {
            bindings_free(&projected);
            petta_activation_roots_free(&activation_roots);
            return false;
        }
        next_goals = cetta_malloc(
            machine->goal_len * sizeof(*next_goals));
        memcpy(next_goals, machine->goals,
               machine->goal_len * sizeof(*next_goals));
        if (!petta_activation_roots_apply_to_goal_copy(
                &activation_roots, machine->goals,
                next_goals, machine->goal_len)) {
            bindings_free(&projected);
            free(next_goals);
            petta_activation_roots_free(&activation_roots);
            return false;
        }
    }
    petta_activation_roots_free(&activation_roots);
    if (machine->visible_len > 0u) {
        if (machine->visible_len >
            SIZE_MAX / sizeof(*next_visible)) {
            bindings_free(&projected);
            free(next_goals);
            return false;
        }
        next_visible = cetta_malloc(
            machine->visible_len * sizeof(*next_visible));
        memcpy(next_visible, machine->visible,
               machine->visible_len * sizeof(*next_visible));
    }
    if (machine->type_obligation_len > 0u) {
        if (machine->type_obligation_len >
            SIZE_MAX / sizeof(*next_type_obligations)) {
            bindings_free(&projected);
            free(next_goals);
            free(next_visible);
            return false;
        }
        next_type_obligations = cetta_malloc(
            machine->type_obligation_len *
                sizeof(*next_type_obligations));
        memcpy(next_type_obligations,
               machine->type_obligations,
               machine->type_obligation_len *
                   sizeof(*next_type_obligations));
    }

    bool major =
        arena_accounted_live_bytes(&machine->tenured) >=
        machine->tenured_major_after;
    size_t before_nursery_bytes =
        arena_accounted_live_bytes(&machine->heap);
    size_t before_tenured_bytes =
        arena_accounted_live_bytes(&machine->tenured);
    size_t before_bytes = petta_size_add_saturating(
        before_nursery_bytes, before_tenured_bytes);
    size_t before_bindings = current->len;
    uint64_t binding_growth = builder->growth_count;
    Arena next_tenured;
    Arena *destination = &machine->tenured;
    ArenaMark destination_mark = {0};
    if (major) {
        petta_machine_heap_arena_init(&next_tenured);
        destination = &next_tenured;
    } else {
        destination_mark = arena_mark(destination);
    }
    size_t destination_start_bytes =
        arena_accounted_live_bytes(destination);
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(destination);
    if (!session) {
        bindings_free(&projected);
        free(next_goals);
        free(next_visible);
        free(next_type_obligations);
        if (major)
            arena_free(&next_tenured);
        return false;
    }
    bool ok = true;
    Atom *next_query = petta_copy_optional_atom(
        session, machine->query, &ok);
    Atom *next_answer_variable = petta_copy_optional_atom(
        session, machine->answer_variable, &ok);
    size_t after_query_atom_bytes =
        arena_accounted_live_bytes(destination);
    for (size_t i = 0u; i < machine->visible_len; i++) {
        next_visible[i].variable = petta_copy_optional_atom(
            session, next_visible[i].variable, &ok);
    }
    size_t after_visible_atom_bytes =
        arena_accounted_live_bytes(destination);
    for (size_t i = 0u;
         i < machine->type_obligation_len; i++) {
        next_type_obligations[i].value = petta_copy_optional_atom(
            session, next_type_obligations[i].value, &ok);
        next_type_obligations[i].formal = petta_copy_optional_atom(
            session, next_type_obligations[i].formal, &ok);
        next_type_obligations[i].checked_value =
            petta_copy_optional_atom(
                session,
                next_type_obligations[i].checked_value, &ok);
        next_type_obligations[i].checked_formal =
            petta_copy_optional_atom(
                session,
                next_type_obligations[i].checked_formal, &ok);
    }
    size_t after_type_atom_bytes =
        arena_accounted_live_bytes(destination);
    size_t goal_first_bytes_promoted = 0u;
    size_t goal_second_bytes_promoted = 0u;
    size_t goal_third_bytes_promoted = 0u;
    size_t goal_fourth_bytes_promoted = 0u;
    for (size_t i = 0u; i < machine->goal_len; i++) {
        size_t before_field_bytes =
            arena_accounted_live_bytes(destination);
        next_goals[i].first = petta_copy_goal_field(
            session, next_goals[i].first,
            next_goals[i].activation_source_fields,
            PETTA_ACTIVATION_SOURCE_FIRST, &ok);
        goal_first_bytes_promoted +=
            arena_accounted_live_bytes(destination) -
            before_field_bytes;
        before_field_bytes = arena_accounted_live_bytes(destination);
        next_goals[i].second = petta_copy_goal_field(
            session, next_goals[i].second,
            next_goals[i].activation_source_fields,
            PETTA_ACTIVATION_SOURCE_SECOND, &ok);
        goal_second_bytes_promoted +=
            arena_accounted_live_bytes(destination) -
            before_field_bytes;
        before_field_bytes = arena_accounted_live_bytes(destination);
        next_goals[i].third = petta_copy_goal_field(
            session, next_goals[i].third,
            next_goals[i].activation_source_fields,
            PETTA_ACTIVATION_SOURCE_THIRD, &ok);
        goal_third_bytes_promoted +=
            arena_accounted_live_bytes(destination) -
            before_field_bytes;
        before_field_bytes = arena_accounted_live_bytes(destination);
        next_goals[i].fourth = petta_copy_goal_field(
            session, next_goals[i].fourth,
            next_goals[i].activation_source_fields,
            PETTA_ACTIVATION_SOURCE_FOURTH, &ok);
        goal_fourth_bytes_promoted +=
            arena_accounted_live_bytes(destination) -
            before_field_bytes;
    }
    size_t after_root_atom_bytes =
        arena_accounted_live_bytes(destination);
    if (ok)
        ok = petta_copy_binding_atoms(
            &projected, destination, session);
    atom_deep_copy_session_free(session);
    if (!ok || !next_query || !next_answer_variable) {
        bindings_free(&projected);
        free(next_goals);
        free(next_visible);
        free(next_type_obligations);
        if (major)
            arena_free(&next_tenured);
        else
            arena_reset(destination, destination_mark);
        return false;
    }

    size_t promoted_bytes = major
        ? arena_accounted_live_bytes(&next_tenured)
        : arena_accounted_live_bytes(&machine->tenured) -
              before_tenured_bytes;
    size_t root_atom_bytes_promoted =
        after_root_atom_bytes - destination_start_bytes;
    size_t query_atom_bytes_promoted =
        after_query_atom_bytes - destination_start_bytes;
    size_t visible_atom_bytes_promoted =
        after_visible_atom_bytes - after_query_atom_bytes;
    size_t type_atom_bytes_promoted =
        after_type_atom_bytes - after_visible_atom_bytes;
    size_t goal_atom_bytes_promoted =
        after_root_atom_bytes - after_type_atom_bytes;
    size_t binding_atom_bytes_promoted =
        promoted_bytes - root_atom_bytes_promoted;
    size_t after_bytes = major
        ? arena_accounted_live_bytes(&next_tenured)
        : arena_accounted_live_bytes(&machine->tenured);
    size_t after_bindings = projected.len;
    /*
     * Arena provenance tracks the Arena object's address.  Unregister the
     * staged major arena before moving its block lists, then register the
     * stable machine field after the move.  A minor collection leaves the
     * tenured Arena object in place.
     */
    arena_free(&machine->heap);
    petta_machine_heap_arena_init(&machine->heap);
    if (major) {
        arena_set_runtime_kind(
            &next_tenured, CETTA_ARENA_RUNTIME_KIND_OTHER);
        arena_free(&machine->tenured);
        machine->tenured = next_tenured;
        arena_set_runtime_kind(
            &machine->tenured,
            CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
        arena_set_hashcons(&machine->tenured, NULL);
    }

    machine->query = next_query;
    machine->answer_variable = next_answer_variable;
    if (machine->goal_len > 0u)
        memcpy(machine->goals, next_goals,
               machine->goal_len * sizeof(*next_goals));
    machine->goal_initialized_len = machine->goal_len;
    machine->goal_trail_len = 0u;
    machine->protected_goal_height = 0u;
    if (machine->visible_len > 0u)
        memcpy(machine->visible, next_visible,
               machine->visible_len * sizeof(*next_visible));
    if (machine->type_obligation_len > 0u)
        memcpy(machine->type_obligations,
               next_type_obligations,
               machine->type_obligation_len *
                   sizeof(*next_type_obligations));
    free(next_goals);
    free(next_visible);
    free(next_type_obligations);

    bindings_builder_free(&machine->search.bindings);
    bindings_builder_init_owned(
        &machine->search.bindings, &projected);
    machine->search.bindings.growth_count = binding_growth;
    petta_machine_invalidate_activation_frame(machine);

    machine->stats.deterministic_heap_collections++;
    if (major)
        machine->stats.deterministic_major_heap_collections++;
    else
        machine->stats.deterministic_minor_heap_collections++;
    machine->stats.deterministic_goal_roots_scanned +=
        (uint64_t)machine->goal_len;
    machine->stats.deterministic_heap_bytes_promoted +=
        (uint64_t)promoted_bytes;
    if (major) {
        machine->stats.deterministic_major_heap_bytes_promoted +=
            (uint64_t)promoted_bytes;
    } else {
        machine->stats.deterministic_minor_heap_bytes_promoted +=
            (uint64_t)promoted_bytes;
    }
    machine->stats.deterministic_root_atom_bytes_promoted +=
        (uint64_t)root_atom_bytes_promoted;
    machine->stats.deterministic_query_atom_bytes_promoted +=
        (uint64_t)query_atom_bytes_promoted;
    machine->stats.deterministic_visible_atom_bytes_promoted +=
        (uint64_t)visible_atom_bytes_promoted;
    machine->stats.deterministic_type_atom_bytes_promoted +=
        (uint64_t)type_atom_bytes_promoted;
    machine->stats.deterministic_goal_atom_bytes_promoted +=
        (uint64_t)goal_atom_bytes_promoted;
    machine->stats.deterministic_goal_first_bytes_promoted +=
        (uint64_t)goal_first_bytes_promoted;
    machine->stats.deterministic_goal_second_bytes_promoted +=
        (uint64_t)goal_second_bytes_promoted;
    machine->stats.deterministic_goal_third_bytes_promoted +=
        (uint64_t)goal_third_bytes_promoted;
    machine->stats.deterministic_goal_fourth_bytes_promoted +=
        (uint64_t)goal_fourth_bytes_promoted;
    machine->stats.deterministic_binding_atom_bytes_promoted +=
        (uint64_t)binding_atom_bytes_promoted;
    if (before_bytes > after_bytes) {
        machine->stats.deterministic_heap_bytes_reclaimed +=
            before_bytes - after_bytes;
    }
    if (before_bindings > after_bindings) {
        machine->stats.deterministic_binding_entries_discarded +=
            before_bindings - after_bindings;
    }
    machine->heap_collect_after =
        petta_machine_nursery_window(machine);
    if (major) {
        machine->tenured_major_after =
            petta_machine_next_major_threshold(
                arena_accounted_live_bytes(&machine->tenured));
    }
    machine->binding_growth_collect_after =
        petta_u64_add_saturating(
            binding_growth,
            (uint64_t)petta_deterministic_binding_window(
                machine, after_bindings));
    uint64_t collection_finished_ns =
        petta_machine_monotonic_ns();
    uint64_t collection_elapsed_ns =
        collection_finished_ns >= collection_started_ns
            ? collection_finished_ns - collection_started_ns
            : 0u;
    petta_machine_add_u64(
        &machine->stats.deterministic_heap_collection_elapsed_ns,
        collection_elapsed_ns);
    petta_machine_add_u64(
        major
            ? &machine->stats
                   .deterministic_major_heap_collection_elapsed_ns
            : &machine->stats
                   .deterministic_minor_heap_collection_elapsed_ns,
        collection_elapsed_ns);
    return true;
}

static bool petta_machine_maybe_collect(
    PettaMachineImpl *machine) {
    if (!machine)
        return false;
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    const Bindings *bindings = bindings_builder_bindings(builder);
    size_t nursery_live =
        arena_accounted_live_bytes(&machine->heap);
    size_t tenured_live =
        arena_accounted_live_bytes(&machine->tenured);
    if (nursery_live >
            machine->stats.maximum_nursery_live_bytes) {
        machine->stats.maximum_nursery_live_bytes =
            nursery_live;
    }
    if (tenured_live >
            machine->stats.maximum_tenured_live_bytes) {
        machine->stats.maximum_tenured_live_bytes =
            tenured_live;
    }
    size_t total_heap_live =
        petta_machine_total_heap_live(machine);
    if (total_heap_live >
        machine->stats.maximum_heap_live_bytes) {
        machine->stats.maximum_heap_live_bytes =
            total_heap_live;
    }
    if ((size_t)bindings->len >
        machine->stats.maximum_binding_entries) {
        machine->stats.maximum_binding_entries =
            bindings->len;
    }
    if (machine->choice_len != 0u) {
        if (nursery_live >= machine->heap_collect_after &&
            petta_choice_heap_evacuation_supported(machine) &&
            !petta_machine_collect_choice_heap(machine)) {
            return false;
        }
        if (builder->growth_count <
            machine->binding_growth_collect_after) {
            return true;
        }
        return petta_machine_collect_choice_bindings(machine);
    }
    /*
     * Logical rollback shrinks the current environment but cannot erase the
     * allocation and matching work already performed.  Schedule collection
     * from the builder's monotone successful-write count, not from the
     * oscillating live depth, so repeated ascent through the same depth does
     * not collect on every branch.
     */
    if (nursery_live <
            machine->heap_collect_after &&
        builder->growth_count <
            machine->binding_growth_collect_after) {
        return true;
    }
    return petta_machine_collect_deterministic_heap(machine);
}

static bool petta_clause_capture(
    Atom *result, const Bindings *environment, void *context) {
    PettaClauseMatch *match = context;
    if (!match || match->present)
        return false;
    if (!bindings_clone(&match->environment, environment)) {
        match->capacity = true;
        return false;
    }
    match->result = result;
    match->present = true;
    return false;
}

static bool petta_machine_cons_clause_match(
    PettaMachineImpl *machine, Atom *equation, Atom *query,
    bool lhs_contains_cons_constraint, PettaClauseMatch *match) {
    if (!machine || !equation || !query || !match ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0], g_builtin_syms.equals) ||
        !lhs_contains_cons_constraint) {
        return false;
    }

    uint32_t epoch = fresh_var_suffix();
    Atom *lhs = petta_machine_freshen_atom(
        machine, equation->expr.elems[1], epoch);
    Atom *rhs = petta_machine_freshen_atom(
        machine, equation->expr.elems[2], epoch);
    if (!lhs || !rhs) {
        match->capacity = true;
        return true;
    }

    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL)) {
        match->capacity = true;
        return true;
    }
    if (petta_semantics_match_cons_constraint(
            &machine->heap, lhs, query, &builder) &&
        !bindings_has_loop(
            (Bindings *)bindings_builder_bindings(&builder))) {
        const Bindings *environment =
            bindings_builder_bindings(&builder);
        Atom *result = petta_machine_apply_bindings(machine,
            environment, &machine->heap, rhs);
        Atom *roots[] = {query};
        Bindings visible;
        bindings_init(&visible);
        if (!result ||
            !bindings_project_reachable(
                environment, roots, 1u, &visible)) {
            match->capacity = true;
        } else {
            match->result = result;
            bindings_move(&match->environment, &visible);
            match->present = true;
        }
        bindings_free(&visible);
    }
    bindings_builder_free(&builder);
    return true;
}

/*
 * Unify atoms already rewritten through the machine's current environment.
 * Keeping this core separate from the defensive wrapper below lets a
 * transition apply its environment exactly once while retaining one
 * authoritative matcher, trail, and occurs-check path.
 */
static bool petta_machine_finish_unification(
    PettaMachineImpl *machine, BindingsBuilder *builder,
    uint64_t growth_before, size_t heap_before,
    bool matched) {
    if (!matched)
        machine->stats.unification_failures++;
    if (builder->growth_count >= growth_before) {
        petta_machine_add_u64(
            &machine->stats.unification_binding_writes,
            builder->growth_count - growth_before);
    }
    petta_machine_add_u64(
        &machine->stats.unification_allocated_bytes,
        petta_machine_arena_growth(&machine->heap, heap_before));
    return matched;
}

static bool petta_machine_unify_resolved(
    PettaMachineImpl *machine, Atom *left, Atom *right) {
    BindingsBuilder *builder = search_context_builder(&machine->search);
    machine->stats.unification_calls++;
    uint64_t growth_before = builder->growth_count;
    size_t heap_before =
        arena_accounted_live_bytes(&machine->heap);
    uint32_t mark = bindings_builder_save(builder);
    bool left_truth = false;
    bool right_truth = false;
    if (petta_semantics_truth_value(left, &left_truth) &&
        petta_semantics_truth_value(right, &right_truth)) {
        return petta_machine_finish_unification(
            machine, builder, growth_before, heap_before,
            left_truth == right_truth);
    }
    bool left_open_cons =
        petta_semantics_is_open_cons_value(left);
    bool right_open_cons =
        petta_semantics_is_open_cons_value(right);
    if (left_open_cons || right_open_cons) {
        bool matched = petta_semantics_match_cons_constraint(
            &machine->heap,
            left_open_cons ? left : right,
            left_open_cons ? right : left,
            builder);
        if (matched &&
            bindings_has_loop(
                (Bindings *)bindings_builder_bindings(builder))) {
            bindings_builder_rollback(builder, mark);
            return petta_machine_finish_unification(
                machine, builder, growth_before, heap_before, false);
        }
        return petta_machine_finish_unification(
            machine, builder, growth_before, heap_before, matched);
    }
    if (match_atoms_builder(left, right, builder)) {
        if (bindings_has_loop(
                bindings_builder_bindings(builder))) {
            bindings_builder_rollback(builder, mark);
            return petta_machine_finish_unification(
                machine, builder, growth_before, heap_before, false);
        }
        return petta_machine_finish_unification(
            machine, builder, growth_before, heap_before, true);
    }
    bindings_builder_rollback(builder, mark);
    return petta_machine_finish_unification(
        machine, builder, growth_before, heap_before, false);
}

static bool petta_machine_unify(
    PettaMachineImpl *machine, Atom *left, Atom *right) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    /* The authoritative matcher dereferences variables through its live
     * BindingsBuilder as it walks each pair.  Materializing both complete
     * operands here first repeats that same traversal and turns a sequence of
     * small unifications into repeated whole-term substitution.  Resolve only
     * root variable chains so PeTTa's truth/open-list discriminators see the
     * current outer shape; ordinary nested variables remain slot-like trail
     * references for match_atoms_builder, and the open-list matcher performs
     * its own demand-local resolution. */
    left = petta_machine_resolve_root(environment, left);
    right = petta_machine_resolve_root(environment, right);
    return left && right &&
           petta_machine_unify_resolved(
               machine, left, right);
}

/* Standardize a stored match candidate lazily against the live trail instead
 * of materializing a fully freshened term first. */
static bool petta_machine_match_epoch_candidate(
    PettaMachineImpl *machine, Atom *pattern,
    Atom *candidate, uint32_t epoch) {
    if (!machine || !pattern || !candidate)
        return false;
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    machine->stats.match_candidate_epoch_views++;
    machine->stats.unification_calls++;
    uint64_t growth_before = builder->growth_count;
    size_t heap_before =
        arena_accounted_live_bytes(&machine->heap);
    uint32_t mark = bindings_builder_save(builder);
    bool matched = match_atoms_epoch_builder(
        pattern, candidate, builder, &machine->heap, epoch);
    if (matched && bindings_has_loop(
            (Bindings *)bindings_builder_bindings(builder))) {
        matched = false;
    }
    if (!matched)
        bindings_builder_rollback(builder, mark);
    return petta_machine_finish_unification(
        machine, builder, growth_before, heap_before, matched);
}

/*
 * Match an ordinary equation directly into the machine's activation trail.
 * The epoch gives the stored rule a fresh logical frame without copying its
 * syntax.  Failure rolls back to the candidate mark; success leaves the
 * frame live until the enclosing clause choice restores its saved trail.
 */
static bool petta_machine_prepare_equation_frame(
    PettaMachineImpl *machine,
    const PettaEquationTemplate *equation_template,
    uint32_t activation_epoch,
    uint32_t activation_first_entry,
    Bindings **bindings_out,
    BindingsDenseEpochFrame **frame_out);

static PettaClauseSlotMatch petta_machine_clause_slot_match(
    PettaMachineImpl *machine, Atom *equation, Atom *query,
    Atom *query_source,
    const PettaEquationTemplate *query_template,
    uint32_t query_epoch, uint32_t query_first_entry,
    Atom **result_out, bool *result_fully_resolved_out,
    bool *result_is_activation_out,
    uint32_t *activation_epoch_out,
    uint32_t *activation_first_entry_out,
    uint64_t *materialized_bytes_out) {
    if (result_out)
        *result_out = NULL;
    if (result_fully_resolved_out)
        *result_fully_resolved_out = false;
    if (result_is_activation_out)
        *result_is_activation_out = false;
    if (activation_epoch_out)
        *activation_epoch_out = 0u;
    if (activation_first_entry_out)
        *activation_first_entry_out = 0u;
    if (materialized_bytes_out)
        *materialized_bytes_out = 0u;
    bool supplied_query_frame = query_source && query_template &&
        query_epoch != 0u;
    if (!machine || !equation ||
        (!query && !supplied_query_frame) || !result_out ||
        !result_fully_resolved_out || !result_is_activation_out ||
        !activation_epoch_out || !activation_first_entry_out ||
        !materialized_bytes_out ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0], g_builtin_syms.equals)) {
        return PETTA_CLAUSE_SLOT_CAPACITY;
    }

    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    Bindings *bindings =
        (Bindings *)bindings_builder_bindings(builder);
    uint32_t activation_first_entry = bindings->len;
    uint32_t mark = bindings_builder_save(builder);
    uint32_t epoch = fresh_var_suffix();
    uint64_t growth_before = builder->growth_count;
    size_t heap_before =
        arena_accounted_live_bytes(&machine->heap);
    machine->stats.match_candidate_epoch_views++;
    machine->stats.unification_calls++;
    Bindings *query_bindings = NULL;
    BindingsDenseEpochFrame *query_dense_frame = NULL;
    bool query_frame = supplied_query_frame &&
        petta_machine_prepare_equation_frame(
            machine, query_template, query_epoch,
            query_first_entry, &query_bindings,
            &query_dense_frame);
    /* A source/frame pair is only an alternate representation of the query.
     * If its dense accelerator cannot be prepared, reconstruct the ordinary
     * query before entering the authoritative matcher. */
    if (!query_frame && !query && supplied_query_frame) {
        query = petta_machine_apply_bindings_epoch_then_all(
            machine, bindings, &machine->heap, query_source,
            query_epoch, query_first_entry);
        if (!query) {
            bindings_builder_rollback(builder, mark);
            return PETTA_CLAUSE_SLOT_CAPACITY;
        }
    }
    bool matched = query_frame
        ? match_atoms_dense_epoch_view_builder_rule_local(
              query_source,
              query_dense_frame,
              lhs, builder, &machine->heap, epoch)
        : match_atoms_epoch_builder_rule_local(
              query, lhs, builder, &machine->heap, epoch);
    (void)query_bindings;
    if (matched && bindings_has_loop(
            (Bindings *)bindings_builder_bindings(builder))) {
        matched = false;
    }
    if (matched) {
        bool cross_frame_alias = false;
        if (!petta_clause_slot_aliases_normalized(
                bindings, activation_first_entry, epoch,
                &cross_frame_alias)) {
            matched = false;
        } else if (cross_frame_alias) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CLAUSE_SLOT_ALIAS_PRESERVED);
        }
    }
    if (!matched) {
        bindings_builder_rollback(builder, mark);
        (void)petta_machine_finish_unification(
            machine, builder, growth_before,
            heap_before, false);
        return PETTA_CLAUSE_SLOT_NO_MATCH;
    }
    (void)petta_machine_finish_unification(
        machine, builder, growth_before,
        heap_before, true);
    /* A payload-visible receipt observes the historical activation-local
     * result, so retain the exact suffix-only boundary.  Otherwise compose
     * activation substitution with the already-live outer environment in one
     * traversal.  The resulting body is immediately scheduled at the top of
     * the goal stack and need not be reconstructed on the next transition. */
    bool payload_observed = machine->host.record_clause_use &&
        (!machine->host.clause_result_payload_observed ||
         machine->host.clause_result_payload_observed(
             machine->host.context));
    bool activation =
        petta_clause_body_activation_enabled() &&
        !payload_observed && term_universe_atom_is_stable(rhs);
    if (activation) {
        cetta_provenance_assert_not_transient(
            rhs, "petta.clause.activation-source");
        *result_out = rhs;
        *result_is_activation_out = true;
        *activation_epoch_out = epoch;
        *activation_first_entry_out = activation_first_entry;
        return PETTA_CLAUSE_SLOT_MATCH;
    }
    size_t materialize_before =
        arena_accounted_live_bytes(&machine->heap);
    Atom *result = payload_observed
        ? petta_machine_apply_bindings_epoch_since(
              machine, bindings, &machine->heap, rhs, epoch,
              activation_first_entry)
        : petta_machine_apply_bindings_epoch_then_all(
              machine, bindings, &machine->heap, rhs, epoch,
              activation_first_entry);
    *materialized_bytes_out = petta_machine_arena_growth(
        &machine->heap, materialize_before);
    if (!result) {
        bindings_builder_rollback(builder, mark);
        return PETTA_CLAUSE_SLOT_CAPACITY;
    }
    *result_out = result;
    *result_fully_resolved_out = !payload_observed;
    return PETTA_CLAUSE_SLOT_MATCH;
}

typedef enum {
    PETTA_BINDING_MERGE_CLAUSE = 0,
    PETTA_BINDING_MERGE_OUTCOME,
} PettaBindingMergeKind;

static bool petta_machine_merge(
    PettaMachineImpl *machine, const Bindings *environment,
    PettaBindingMergeKind kind) {
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    uint64_t growth_before = builder->growth_count;
    uint64_t source_items = environment
        ? (uint64_t)environment->len + (uint64_t)environment->eq_len
        : 0u;
    bool merged = bindings_builder_try_merge(builder, environment);
    uint64_t logical_writes = builder->growth_count >= growth_before
        ? builder->growth_count - growth_before
        : UINT64_MAX;

    uint64_t *calls = kind == PETTA_BINDING_MERGE_CLAUSE
        ? &machine->stats.clause_binding_merge_calls
        : &machine->stats.outcome_binding_merge_calls;
    uint64_t *items = kind == PETTA_BINDING_MERGE_CLAUSE
        ? &machine->stats.clause_binding_merge_source_items
        : &machine->stats.outcome_binding_merge_source_items;
    uint64_t *writes = kind == PETTA_BINDING_MERGE_CLAUSE
        ? &machine->stats.clause_binding_merge_logical_writes
        : &machine->stats.outcome_binding_merge_logical_writes;
    uint64_t *failures = kind == PETTA_BINDING_MERGE_CLAUSE
        ? &machine->stats.clause_binding_merge_failures
        : &machine->stats.outcome_binding_merge_failures;
    petta_machine_add_u64(calls, 1u);
    petta_machine_add_u64(items, source_items);
    petta_machine_add_u64(writes, logical_writes);
    if (!merged)
        petta_machine_add_u64(failures, 1u);
    return merged;
}

static bool petta_machine_factor_outcome_prefixes(
    PettaMachineImpl *machine, OutcomeSet *outcomes,
    const Bindings *base) {
    if (!machine || !outcomes || !base)
        return machine && outcomes && base;
    if (base->len == 0u && base->eq_len == 0u)
        return true;
    for (CettaCount i = 0u; i < outcomes->len; i++) {
        bool factored = false;
        uint64_t elided = 0u;
        petta_machine_add_u64(
            &machine->stats.outcome_prefix_factor_attempts, 1u);
        if (!bindings_factor_prefix(
                &outcomes->items[i].env, base,
                &factored, &elided)) {
            return false;
        }
        if (factored) {
            petta_machine_add_u64(
                &machine->stats.outcome_prefix_factor_successes, 1u);
            petta_machine_add_u64(
                &machine->stats.outcome_prefix_logical_items_elided,
                elided);
        }
        petta_machine_add_u64(
            &machine->stats.outcome_prefix_residual_items,
            (uint64_t)outcomes->items[i].env.len +
                (uint64_t)outcomes->items[i].env.eq_len);
    }
    return true;
}

static bool petta_push_solve(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE,
            .barrier = barrier,
            .first = expression,
            .second = expected,
        });
}

static bool petta_push_solve_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE,
            .barrier = barrier,
            .first = expression,
            .second = expected,
            .plan = plan,
        });
}

static bool petta_push_solve_activation_planned(
    PettaMachineImpl *machine, Atom *source, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    const PettaEquationTemplate *equation_template,
    uint32_t epoch, uint32_t first_entry) {
    if (!machine || !source || !expected || epoch == 0u)
        return false;
    cetta_provenance_assert_not_transient(
        source, "petta.goal.activation-source");
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE_ACTIVATION,
            .barrier = barrier,
            .first = source,
            .second = expected,
            .plan = plan,
            .activation_template = equation_template,
            .activation_epoch = epoch,
            .activation_first_entry = first_entry,
            .activation_source_fields =
                PETTA_ACTIVATION_SOURCE_FIRST,
        });
}

static bool petta_push_solve_planned_resolved(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE,
            .barrier = barrier,
            .first = expression,
            .second = expected,
            .plan = plan,
            .first_operand_resolved = true,
        });
}

/*
 * Evaluate a value as code at an explicit PeTTa forcing boundary.  Ordinary
 * SOLVE retains the authored role of a variable: substitution may supply an
 * expression value, but it must not retroactively turn that source variable
 * into a call.  `eval` and `reduce` are the language constructs that
 * deliberately cross this value-to-code boundary.
 */
static bool petta_push_force(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_FORCE,
            .barrier = barrier,
            .first = expression,
            .second = expected,
        });
}

static bool petta_push_unify(
    PettaMachineImpl *machine, Atom *left, Atom *right,
    uint32_t barrier) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_UNIFY,
            .barrier = barrier,
            .first = left,
            .second = right,
        });
}

static bool petta_push_counted_collection_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE_COUNTED_COLLECTION,
            .barrier = barrier,
            .first = expression,
            .second = expected,
            .plan = plan,
        });
}

static bool petta_push_counted_collection_planned_resolved(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan) {
    return petta_goal_push(
        machine,
        (PettaGoal){
            .kind = PETTA_GOAL_SOLVE_COUNTED_COLLECTION,
            .barrier = barrier,
            .first = expression,
            .second = expected,
            .plan = plan,
            .first_operand_resolved = true,
        });
}

static bool petta_push_clause_result(
    PettaMachineImpl *machine, Atom *result, Atom *expected,
    uint32_t barrier, bool evaluate_result,
    bool translate_result, bool count_collection_result,
    const PettaPlanNode *plan, bool result_fully_resolved) {
    if (!evaluate_result)
        return petta_push_unify(
            machine, result, expected, barrier);
    if (count_collection_result && !translate_result) {
        return result_fully_resolved
            ? petta_push_counted_collection_planned_resolved(
                  machine, result, expected, barrier, plan)
            : petta_push_counted_collection_planned(
                  machine, result, expected, barrier, plan);
    }
    /*
     * A Predicate value returned by an ordinary equation is reified data.
     * PeTTa does not immediately call its list-to-compound constructor at
     * this clause boundary; a later demanded occurrence or predicate
     * operation owns that phase transition.
     */
    if (!translate_result &&
        result && result->kind == ATOM_EXPR &&
        result->expr.len == 2u &&
        petta_semantics_form(
            atom_head_symbol_id(result)) ==
                PETTA_FORM_PREDICATE) {
        return petta_push_unify(
            machine, result, expected, barrier);
    }
    if (!translate_result)
        return result_fully_resolved
            ? petta_push_solve_planned_resolved(
                  machine, result, expected, barrier, plan)
            : petta_push_solve_planned(
                  machine, result, expected, barrier, plan);

    /*
     * A registered translator relation computes a program fragment and then
     * runs that fragment.  Reify the two stages as ordinary machine goals:
     * the first stage may itself be relational, and the second stage inherits
     * the same choice, cut, effect, and suspension machinery as every other
     * PeTTa call.
     */
    Atom *generated = petta_fresh_variable(machine);
    Atom *translated = count_collection_result
        ? petta_fresh_variable(machine) : expected;
    if (!generated || !translated)
        return false;
    if (count_collection_result &&
        !petta_push_counted_collection_planned(
            machine, translated, expected, barrier, NULL)) {
        return false;
    }
    if (!petta_push_force(
            machine, generated, translated, barrier)) {
        return false;
    }
    return result_fully_resolved
        ? petta_push_solve_planned_resolved(
              machine, result, generated, barrier, plan)
        : petta_push_solve_planned(
              machine, result, generated, barrier, plan);
}

/*
 * Decide whether an argument has already crossed every evaluation boundary
 * required by its source occurrence.  Atomic and empty values are immediate.
 * A source variable is also immediate after substitution: PeTTa variables
 * carry values, so an expression supplied through one does not become a call
 * retroactively.  Canonical closures, partials, open-list nodes, and Prolog
 * compounds are likewise value representations.
 */
static bool petta_machine_immediate_value(
    Atom *atom, const PettaPlanNode *plan) {
    if (!atom)
        return false;
    if (plan && plan->role == PETTA_PLAN_VALUE)
        return true;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return true;
    Atom *body = NULL;
    int64_t counted = 0;
    return petta_semantics_lambda_body(atom, &body) ||
           petta_semantics_nullary_lambda_body(atom, &body) ||
           petta_semantics_partial_view(atom, NULL, NULL) ||
           petta_semantics_is_open_cons_value(atom) ||
           atom_petta_prolog_compound_body(atom, &body) ||
           atom_prolog_compound_body(atom, &body) ||
           atom_petta_counted_collection_count(atom, &counted);
}

static bool petta_machine_is_value_reference(Atom *atom) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        const char *name = symbol_bytes(g_symbols, atom->sym_id);
        return name && name[0] == '&';
    }
    Atom *name_key = NULL;
    return registry_ref_name_key(atom, &name_key) && name_key;
}

static bool petta_push_evaluated_expression_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    uint32_t barrier, const PettaPlanNode *plan);

static bool petta_push_evaluated_expression_range_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    CettaExprIndex end_evaluated, uint32_t barrier,
    const PettaPlanNode *plan);

/*
 * An authored data occurrence is a constructor, not a function whose result
 * must be generated before it can be tested.  Unify its outer shape first,
 * then evaluate independently compiled children against the corresponding
 * constrained slots.  This is the machine form of putting the translated
 * constructor directly in a Prolog clause head: inverse calls receive demand
 * from their expected result instead of enumerating an unbounded value before
 * comparison.
 */
static bool petta_push_data_expression_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    CettaExprIndex first_evaluated, uint32_t barrier,
    const PettaPlanNode *plan) {
    CettaExprLen length = expression->expr.len;
    if (first_evaluated > length ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) {
        return false;
    }
    Atom *inline_slots[9];
    Atom **slots = length <= 9u
        ? inline_slots
        : arena_alloc(
              &machine->heap,
              sizeof(*slots) * (size_t)length);
    if (length > 0u && !slots)
        return false;
    for (CettaExprIndex index = 0u; index < length; index++) {
        slots[index] = index < first_evaluated
            ? expression->expr.elems[index]
            : petta_fresh_variable(machine);
        if (!slots[index])
            return false;
    }
    Atom *shape = atom_expr(&machine->heap, slots, length);
    if (!shape)
        return false;

    /*
     * Goals are a LIFO stack.  Push child computations first and the shape
     * constraint last so the constraint executes before any child effect or
     * open relational search.  Children retain authored left-to-right order.
     */
    for (CettaExprIndex index = length;
         index > first_evaluated; index--) {
        CettaExprIndex argument = index - 1u;
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[argument],
                slots[argument], barrier,
                petta_plan_child(plan, argument))) {
            return false;
        }
    }
    return petta_push_unify(
        machine, shape, expected, barrier);
}

static bool petta_push_evaluated_expression(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    uint32_t barrier) {
    return petta_push_evaluated_expression_planned(
        machine, expression, expected, final_kind,
        first_evaluated, barrier, NULL);
}

static bool petta_push_evaluated_expression_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    uint32_t barrier, const PettaPlanNode *plan) {
    return petta_push_evaluated_expression_range_planned(
        machine, expression, expected, final_kind,
        first_evaluated, expression->expr.len, barrier, plan);
}

static bool petta_push_evaluated_expression_range_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    CettaExprIndex end_evaluated, uint32_t barrier,
    const PettaPlanNode *plan) {
    CettaExprLen length = expression->expr.len;
    if (first_evaluated > end_evaluated ||
        end_evaluated > length ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) {
        return false;
    }
    Atom *inline_ready[9];
    uint8_t inline_needs_evaluation[9];
    Atom **ready_elements = length <= 9u
        ? inline_ready
        : arena_alloc(
              &machine->heap,
              sizeof(*ready_elements) * (size_t)length);
    uint8_t *needs_evaluation = length <= 9u
        ? inline_needs_evaluation
        : arena_alloc(
              &machine->heap,
              sizeof(*needs_evaluation) * (size_t)length);
    if ((length > 0u && !ready_elements) ||
        (length > 0u && !needs_evaluation)) {
        return false;
    }
    memset(
        needs_evaluation, 0,
        sizeof(*needs_evaluation) * (size_t)length);
    const Bindings *environment =
        search_context_bindings(&machine->search);
    for (CettaExprIndex index = 0u; index < length; index++) {
        Atom *source = expression->expr.elems[index];
        if (index < first_evaluated || index >= end_evaluated) {
            ready_elements[index] = source;
            continue;
        }
        Atom *resolved = petta_machine_apply_bindings(machine,
            environment, &machine->heap, source);
        if (!resolved)
            return false;
        if (petta_machine_immediate_value(
                resolved, petta_plan_child(plan, index))) {
            ready_elements[index] = resolved;
            continue;
        }
        ready_elements[index] = petta_fresh_variable(machine);
        needs_evaluation[index] = 1u;
        if (!ready_elements[index])
            return false;
    }
    Atom *ready = atom_expr(&machine->heap, ready_elements, length);
    if (!ready)
        return false;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = final_kind,
                .barrier = barrier,
                .first = ready,
                .second = expected,
                .plan = plan,
            })) {
        return false;
    }
    for (CettaExprIndex index = end_evaluated;
         index > first_evaluated; index--) {
        CettaExprIndex argument = index - 1u;
        if (!needs_evaluation[argument])
            continue;
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[argument],
                ready_elements[argument], barrier,
                petta_plan_child(plan, argument))) {
            return false;
        }
    }
    return true;
}

static bool petta_machine_schedule_named_state(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PeTTaForm form, uint32_t barrier,
    const PettaPlanNode *plan) {
    if (!machine || !expression || !expected ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u) {
        return false;
    }

    /*
     * Upstream PeTTa treats new-state as the data wrapper accepted by bind!,
     * while computations nested in its payload are translated normally.
     * Preserve that phase boundary in the explicit machine.
     */
    if (form == PETTA_FORM_NEW_STATE) {
        return petta_push_data_expression_planned(
            machine, expression, expected,
            1u, barrier, plan);
    }

    CettaExprLen expected_length =
        form == PETTA_FORM_GET_STATE ? 2u : 3u;
    if (expression->expr.len != expected_length)
        return false;

    Atom *value_expression = NULL;
    const PettaPlanNode *value_plan = NULL;
    if (form == PETTA_FORM_BIND_STATE) {
        Atom *wrapper = expression->expr.elems[2];
        if (!wrapper || wrapper->kind != ATOM_EXPR ||
            wrapper->expr.len != 2u ||
            !atom_is_symbol_id(
                wrapper->expr.elems[0],
                g_builtin_syms.new_state)) {
            return false;
        }
        value_expression = wrapper->expr.elems[1];
        value_plan = petta_plan_child(
            petta_plan_child(plan, 2u), 1u);
    } else if (form == PETTA_FORM_CHANGE_STATE) {
        value_expression = expression->expr.elems[2];
        value_plan = petta_plan_child(plan, 2u);
    }

    Atom *ready_name = petta_fresh_variable(machine);
    Atom *ready_value = value_expression
        ? petta_fresh_variable(machine) : NULL;
    if (!ready_name || (value_expression && !ready_value) ||
        !petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_NAMED_STATE_READY,
                .barrier = barrier,
                .first = ready_name,
                .second = ready_value,
                .third = expected,
                .choice_index = (size_t)form,
            })) {
        return false;
    }

    /*
     * Goals execute LIFO.  Push the value first so the name is evaluated
     * first, matching PeTTa's left-to-right translated argument order.
     */
    if (value_expression &&
        !petta_push_solve_planned(
            machine, value_expression, ready_value,
            barrier, value_plan)) {
        return false;
    }
    Atom *name_expression = expression->expr.elems[1];
    if (name_expression->kind == ATOM_SYMBOL) {
        /* A state name denotes the registry location at this boundary.  In
         * particular, an authored `&name` must not pass through ordinary
         * value-reference resolution and turn into the value already stored
         * there before get-state/change-state! receives the location. */
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_UNIFY,
                .barrier = barrier,
                .first = name_expression,
                .second = ready_name,
            });
    }
    return petta_push_solve_planned(
        machine, name_expression, ready_name,
        barrier, petta_plan_child(plan, 1u));
}

static Atom *petta_machine_boolean_value(
    PettaMachineImpl *machine, bool value) {
    return machine->host.boolean_value
        ? machine->host.boolean_value(
              machine->host.context, &machine->heap, value)
        : petta_semantics_boolean_value(&machine->heap, value);
}

static bool petta_machine_boolean(
    PettaMachineImpl *machine, bool value, Atom *expected) {
    Atom *boolean = petta_machine_boolean_value(machine, value);
    return boolean && petta_machine_unify(machine, boolean, expected);
}

/*
 * PeTTa's Boolean operators are finite relations, not merely strict
 * functions.  Enumerate their canonical Prolog clause order:
 * true/true, true/false, false/true, false/false.  Unary `not` uses the
 * corresponding true, false order.  Keeping this table inside the explicit
 * search machine lets open arguments participate in ordinary trail rollback
 * and backtracking instead of being prematurely treated as inert data.
 */
static bool petta_machine_boolean_row(
    SymbolId head, uint32_t row, bool inputs[2],
    uint32_t *arity, bool *result) {
    uint32_t relation_arity = 0u;
    if (!inputs || !arity || !result ||
        !petta_semantics_boolean_relation_arity(
            head, &relation_arity)) {
        return false;
    }
    if (relation_arity == 1u) {
        if (row >= 2u)
            return false;
        inputs[0] = row == 0u;
        inputs[1] = false;
        *result = !inputs[0];
        *arity = relation_arity;
        return true;
    }
    if (row >= 4u)
        return false;
    inputs[0] = row < 2u;
    inputs[1] = (row & 1u) == 0u;
    if (head == g_builtin_syms.op_and) {
        *result = inputs[0] && inputs[1];
    } else if (head == g_builtin_syms.op_or) {
        *result = inputs[0] || inputs[1];
    } else if (head == g_builtin_syms.op_xor) {
        *result = inputs[0] != inputs[1];
    } else {
        return false;
    }
    *arity = relation_arity;
    return true;
}

static Atom *petta_machine_cons_constraint(
    PettaMachineImpl *machine, Atom *head, Atom *tail) {
    return machine
        ? petta_semantics_open_cons_value(
              &machine->heap, head, tail)
        : NULL;
}

typedef enum {
    PETTA_LIST_INVALID = 0,
    PETTA_LIST_CLOSED,
    PETTA_LIST_OPEN,
} PettaListShape;

/*
 * Inspect the flat-expression/open-cons dual representation without
 * materializing it.  This is the list analogue of dereferencing a WAM term:
 * a closed flat tail contributes its whole arity, while an unbound tail is
 * returned as the one place a relational length operation may extend.
 */
static PettaListShape petta_machine_list_shape(
    PettaMachineImpl *machine, Atom *list,
    uint64_t *prefix_length, Atom **open_tail) {
    if (!machine || !list || !prefix_length || !open_tail)
        return PETTA_LIST_INVALID;
    *prefix_length = 0u;
    *open_tail = NULL;
    for (;;) {
        list = petta_machine_resolve_root(
            search_context_bindings(&machine->search), list);
        if (!list)
            return PETTA_LIST_INVALID;
        if (petta_semantics_is_cons_constraint(list)) {
            if (*prefix_length == UINT64_MAX)
                return PETTA_LIST_INVALID;
            (*prefix_length)++;
            list = list->expr.elems[2];
            continue;
        }
        if (list->kind == ATOM_VAR) {
            *open_tail = list;
            return PETTA_LIST_OPEN;
        }
        if (list->kind != ATOM_EXPR ||
            *prefix_length >
                UINT64_MAX - (uint64_t)list->expr.len) {
            return PETTA_LIST_INVALID;
        }
        *prefix_length += (uint64_t)list->expr.len;
        return PETTA_LIST_CLOSED;
    }
}

static Atom *petta_machine_fresh_list(
    PettaMachineImpl *machine, uint64_t length) {
    if (!machine || length > UINT64_MAX - 1u ||
        !cetta_expr_len_fits_size((CettaExprLen)length) ||
        !cetta_expr_len_mul_fits_size(
            (CettaExprLen)length, sizeof(Atom *))) {
        return NULL;
    }
    Atom **items = length
        ? arena_alloc(
              &machine->heap,
              sizeof(*items) * (size_t)length)
        : NULL;
    for (uint64_t index = 0u; index < length; index++) {
        items[index] = petta_fresh_variable(machine);
        if (!items[index])
            return NULL;
    }
    return atom_expr(
        &machine->heap, items, (CettaExprLen)length);
}

/*
 * Closed cons constraints are an internal relational representation.  At
 * the answer boundary restore PeTTa's observable flat tuple carrier.
 */
static Atom *petta_machine_materialize_list(
    PettaMachineImpl *machine, Atom *list) {
    if (!machine || !list)
        return NULL;
    list = petta_machine_apply_bindings(machine,
        search_context_bindings(&machine->search),
        &machine->heap, list);
    if (!petta_semantics_is_open_cons_value(list))
        return list;

    Atom **items = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    Atom *cursor = list;
    for (;;) {
        cursor = petta_machine_apply_bindings(machine,
            search_context_bindings(&machine->search),
            &machine->heap, cursor);
        if (!cursor)
            goto unresolved;
        if (petta_semantics_is_open_cons_value(cursor)) {
            if (!petta_machine_reserve(
                    (void **)&items, &capacity, length + 1u,
                    sizeof(*items))) {
                free(items);
                return NULL;
            }
            Atom *head = petta_machine_apply_bindings(machine,
                search_context_bindings(&machine->search),
                &machine->heap, cursor->expr.elems[1]);
            if (!head)
                goto unresolved;
            items[length++] = head;
            cursor = cursor->expr.elems[2];
            continue;
        }
        if (cursor->kind != ATOM_EXPR)
            goto unresolved;
        if ((uint64_t)cursor->expr.len >
            (uint64_t)(SIZE_MAX - length)) {
            free(items);
            return NULL;
        }
        size_t total = length + (size_t)cursor->expr.len;
        if (!petta_machine_reserve(
                (void **)&items, &capacity, total,
                sizeof(*items))) {
            free(items);
            return NULL;
        }
        for (CettaExprIndex index = 0u;
             index < cursor->expr.len; index++) {
            Atom *item = petta_machine_apply_bindings(machine,
                search_context_bindings(&machine->search),
                &machine->heap, cursor->expr.elems[index]);
            if (!item)
                goto unresolved;
            items[length++] = item;
        }
        if (!cetta_expr_len_fits_size((CettaExprLen)length)) {
            free(items);
            return NULL;
        }
        Atom *result = atom_expr(
            &machine->heap, items, (CettaExprLen)length);
        free(items);
        return result;
    }

unresolved:
    free(items);
    return list;
}

/*
 * Ground list primitives consume the observable closed tuple carrier.
 * Relational clauses may hand them an equivalent internal cons spine; close
 * that representation once before any primitive indexes expression
 * elements.  An unresolved open tail is not a finite input.
 */
static Atom *petta_machine_closed_list(
    PettaMachineImpl *machine, Atom *list) {
    if (!machine || !list)
        return NULL;
    if (petta_semantics_is_open_cons_value(list)) {
        list = petta_machine_materialize_list(machine, list);
    }
    return list && list->kind == ATOM_EXPR &&
                   !petta_semantics_is_open_cons_value(list)
        ? list
        : NULL;
}

typedef struct {
    Atom *source;
    Atom *memo_source;
    Atom **result_children;
    CettaExprIndex next;
    Atom **result_slot;
} PettaAnswerMaterializeFrame;

typedef struct {
    const Atom *source;
    Atom *result;
} PettaAnswerMaterializeMemoSlot;

enum {
    PETTA_ANSWER_MATERIALIZE_MEMO_INLINE_CAP = 64u,
};

typedef struct {
    PettaAnswerMaterializeMemoSlot
        inline_slots[PETTA_ANSWER_MATERIALIZE_MEMO_INLINE_CAP];
    PettaAnswerMaterializeMemoSlot *slots;
    size_t capacity;
    size_t used;
} PettaAnswerMaterializeMemo;

static void petta_answer_materialize_memo_init(
    PettaAnswerMaterializeMemo *memo) {
    memset(memo, 0, sizeof(*memo));
    memo->slots = memo->inline_slots;
    memo->capacity = PETTA_ANSWER_MATERIALIZE_MEMO_INLINE_CAP;
}

static void petta_answer_materialize_memo_free(
    PettaAnswerMaterializeMemo *memo) {
    if (!memo)
        return;
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memset(memo, 0, sizeof(*memo));
}

static size_t petta_answer_materialize_memo_hash(
    const Atom *source) {
    uintptr_t value = (uintptr_t)source;
    value >>= 4u;
    value ^= value >> 33u;
    value *= (uintptr_t)UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33u;
    return (size_t)value;
}

static Atom *petta_answer_materialize_memo_lookup(
    const PettaAnswerMaterializeMemo *memo,
    const Atom *source) {
    if (!memo || !source || !memo->slots || memo->capacity == 0u)
        return NULL;
    size_t mask = memo->capacity - 1u;
    size_t index =
        petta_answer_materialize_memo_hash(source) & mask;
    for (;;) {
        const PettaAnswerMaterializeMemoSlot *slot =
            &memo->slots[index];
        if (!slot->source)
            return NULL;
        if (slot->source == source)
            return slot->result;
        index = (index + 1u) & mask;
    }
}

static bool petta_answer_materialize_memo_grow(
    PettaAnswerMaterializeMemo *memo) {
    if (!memo || memo->capacity > SIZE_MAX / 2u)
        return false;
    size_t next_capacity = memo->capacity * 2u;
    if (next_capacity > SIZE_MAX / sizeof(*memo->slots))
        return false;
    PettaAnswerMaterializeMemoSlot *next =
        calloc(next_capacity, sizeof(*next));
    if (!next)
        return false;
    size_t next_mask = next_capacity - 1u;
    for (size_t index = 0u; index < memo->capacity; index++) {
        PettaAnswerMaterializeMemoSlot slot = memo->slots[index];
        if (!slot.source)
            continue;
        size_t target =
            petta_answer_materialize_memo_hash(slot.source) & next_mask;
        while (next[target].source)
            target = (target + 1u) & next_mask;
        next[target] = slot;
    }
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = next;
    memo->capacity = next_capacity;
    return true;
}

static bool petta_answer_materialize_memo_store(
    PettaAnswerMaterializeMemo *memo,
    const Atom *source, Atom *result) {
    if (!memo || !source || !result)
        return false;
    if ((memo->used + 1u) * 4u >= memo->capacity * 3u &&
        !petta_answer_materialize_memo_grow(memo)) {
        return false;
    }
    size_t mask = memo->capacity - 1u;
    size_t index =
        petta_answer_materialize_memo_hash(source) & mask;
    for (;;) {
        PettaAnswerMaterializeMemoSlot *slot = &memo->slots[index];
        if (!slot->source) {
            slot->source = source;
            slot->result = result;
            memo->used++;
            return true;
        }
        if (slot->source == source) {
            slot->result = result;
            return true;
        }
        index = (index + 1u) & mask;
    }
}

static bool petta_answer_materialize_frame_reserve(
    PettaAnswerMaterializeFrame **frames,
    size_t *capacity, size_t required) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2u : 32u;
    while (next < required) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**frames))
        return false;
    void *grown = realloc(*frames, sizeof(**frames) * next);
    if (!grown)
        return false;
    *frames = grown;
    *capacity = next;
    return true;
}

/*
 * Open-cons nodes are an internal relational carrier.  Remove them at the
 * answer boundary while leaving quoted syntax and suspended callables
 * opaque.  Closed spines become ordinary flat PeTTa lists; an unresolved
 * tail becomes authored `(cons Head Tail)` syntax, never the private carrier
 * tag.  The explicit frame stack makes finite authored depth a resource
 * question rather than silently changing the answer at a fixed limit.
 */
static Atom *petta_machine_materialize_answer(
    PettaMachineImpl *machine, Atom *atom) {
    if (!machine || !atom)
        return NULL;
    Atom *resolved = petta_machine_apply_bindings(
        machine, search_context_bindings(&machine->search),
        &machine->heap, atom);
    if (!resolved)
        return NULL;
    /* Open-cons is the only private carrier normalized at this boundary.
     * Constructor-derived structural facts compose through expressions, so
     * absence of every internal tag proves that the complete transformation
     * is the identity.  Missing or hand-built facts remain conservative and
     * take the general graph-preserving traversal below. */
    if (!atom_structural_may_have_internal_tag(resolved))
        return resolved;

    Atom *result = NULL;
    PettaAnswerMaterializeFrame *frames = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    PettaAnswerMaterializeMemo memo;
    petta_answer_materialize_memo_init(&memo);

#define PETTA_ANSWER_MATERIALIZE_PUSH(source_atom, destination_slot) do { \
    Atom *petta_memo_source__ = (source_atom); \
    Atom *petta_source__ = petta_memo_source__; \
    Atom **petta_slot__ = (destination_slot); \
    if (!petta_source__ || !petta_slot__) \
        goto fail; \
    Atom *petta_memoized__ = petta_answer_materialize_memo_lookup( \
        &memo, petta_memo_source__); \
    if (petta_memoized__) { \
        *petta_slot__ = petta_memoized__; \
        break; \
    } \
    if (petta_semantics_is_open_cons_value(petta_source__)) { \
        petta_source__ = petta_semantics_materialize_logical_list( \
            &machine->heap, petta_source__); \
        if (!petta_source__) \
            goto fail; \
    } \
    Atom *petta_callable_body__ = NULL; \
    bool petta_opaque__ = petta_source__->kind == ATOM_EXPR && \
        petta_source__->expr.len > 0u && \
        (petta_semantics_lambda_body( \
             petta_source__, &petta_callable_body__) || \
         petta_semantics_nullary_lambda_body( \
             petta_source__, &petta_callable_body__) || \
         petta_semantics_partial_view( \
             petta_source__, NULL, NULL) || \
         atom_is_symbol_id( \
             petta_source__->expr.elems[0], \
             g_builtin_syms.quote)); \
    if (petta_source__->kind != ATOM_EXPR || \
        petta_source__->expr.len == 0u || petta_opaque__) { \
        if (!petta_answer_materialize_memo_store( \
                &memo, petta_memo_source__, petta_source__)) \
            goto fail; \
        *petta_slot__ = petta_source__; \
        break; \
    } \
    if (!cetta_expr_len_mul_fits_size( \
            petta_source__->expr.len, sizeof(Atom *)) || \
        !petta_answer_materialize_frame_reserve( \
            &frames, &capacity, length + 1u)) { \
        goto fail; \
    } \
    Atom **petta_results__ = arena_alloc( \
        &machine->heap, sizeof(*petta_results__) * \
            (size_t)petta_source__->expr.len); \
    if (!petta_results__) \
        goto fail; \
    frames[length++] = (PettaAnswerMaterializeFrame){ \
        .source = petta_source__, \
        .memo_source = petta_memo_source__, \
        .result_children = petta_results__, \
        .result_slot = petta_slot__, \
    }; \
} while (0)

    PETTA_ANSWER_MATERIALIZE_PUSH(resolved, &result);
    while (length > 0u) {
        PettaAnswerMaterializeFrame *frame = &frames[length - 1u];
        if (frame->next < frame->source->expr.len) {
            CettaExprIndex index = frame->next++;
            PETTA_ANSWER_MATERIALIZE_PUSH(
                frame->source->expr.elems[index],
                &frame->result_children[index]);
            continue;
        }
        bool changed = false;
        for (CettaExprIndex index = 0u;
             index < frame->source->expr.len; index++) {
            changed = changed ||
                frame->result_children[index] !=
                    frame->source->expr.elems[index];
        }
        Atom *built = changed
            ? atom_expr(
                  &machine->heap, frame->result_children,
                  frame->source->expr.len)
            : frame->source;
        if (!built)
            goto fail;
        if (!petta_answer_materialize_memo_store(
                &memo, frame->memo_source, built)) {
            goto fail;
        }
        *frame->result_slot = built;
        length--;
    }
    free(frames);
    petta_answer_materialize_memo_free(&memo);
#undef PETTA_ANSWER_MATERIALIZE_PUSH
    return result;

fail:
    free(frames);
    petta_answer_materialize_memo_free(&memo);
#undef PETTA_ANSWER_MATERIALIZE_PUSH
    return NULL;
}

/*
 * Error is a reserved result head in HE and PeTTa.  It is neither ordinary
 * unification failure nor a Prolog-style throw which prunes enclosing
 * alternatives: the raising branch completes with an Error answer and later
 * alternatives remain available.  A catch marker is the sole delimiter
 * which converts that raised result back into an ordinary value.
 */
static bool petta_machine_raise_error(
    PettaMachineImpl *machine, Atom *error) {
    if (!machine || !atom_is_error(error))
        return false;

    Atom *materialized = petta_machine_materialize_answer(
        machine, error);
    Atom *stable = materialized
        ? petta_machine_copy_atom(
              machine, &machine->tenured, materialized,
              PETTA_ATOM_COPY_ERROR)
        : NULL;
    if (!stable)
        return false;

    for (size_t height = machine->goal_len; height > 0u; height--) {
        size_t index = height - 1u;
        PettaGoal marker = machine->goals[index];
        if (marker.kind != PETTA_GOAL_CATCH_READY)
            continue;

        /*
         * Catch abandons alternatives and bindings created by its protected
         * body, while preserving every enclosing choice.  The goal trail
         * restores this marker when a body alternative is retried, so the
         * dynamic extent survives ordinary nondeterministic backtracking.
         */
        size_t entry_choices = marker.choice_index;
        if (entry_choices > machine->choice_len)
            entry_choices = machine->choice_len;
        petta_choice_truncate(machine, entry_choices);
        search_context_rollback(
            &machine->search, marker.catch_trail);
        petta_machine_invalidate_activation_frame(machine);
        if (marker.catch_type_obligation_mark >
            machine->type_obligation_len) {
            return false;
        }
        machine->type_obligation_len =
            marker.catch_type_obligation_mark;
        petta_machine_invalidate_type_obligation_cache(machine);
        machine->goal_len = index;
        return petta_push_unify(
            machine, stable, marker.second, marker.barrier);
    }

    /*
     * No catch: expose this branch's Error as the next machine answer.  Do
     * not truncate choices.  Answer emission performs no goal-stack writes,
     * and the next call backtracks before dispatch, so protected continuation
     * slots remain intact for sibling alternatives.
     */
    machine->raised_error = stable;
    machine->goal_len = 0u;
    return true;
}

static bool petta_symbol_name_is(SymbolId id, const char *name) {
    const char *actual =
        id == SYMBOL_ID_NONE ? NULL : symbol_bytes(g_symbols, id);
    return actual && strcmp(actual, name) == 0;
}

static PeTTaNamedArity petta_machine_extension_named_arity_using(
    PettaMachineImpl *machine, SymbolId head,
    CettaExprLen supplied,
    PeTTaNamedArity (*foreign_named_arity)(
        void *, SymbolId, CettaExprLen)) {
    if (!machine || head == SYMBOL_ID_NONE)
        return (PeTTaNamedArity){0};
    if (machine->host.native_named_arity) {
        PeTTaNamedArity native =
            machine->host.native_named_arity(
                machine->host.context, head, supplied);
        if (native.known)
            return native;
    }
    return foreign_named_arity
        ? foreign_named_arity(
              machine->host.context, head, supplied)
        : (PeTTaNamedArity){0};
}

static PeTTaNamedArity petta_machine_extension_named_arity(
    PettaMachineImpl *machine, SymbolId head,
    CettaExprLen supplied) {
    return petta_machine_extension_named_arity_using(
        machine, head, supplied,
        machine ? machine->host.foreign_named_arity : NULL);
}

static PeTTaNamedArity
petta_machine_extension_named_arity_resolved(
    PettaMachineImpl *machine, SymbolId head,
    CettaExprLen supplied) {
    return petta_machine_extension_named_arity_using(
        machine, head, supplied,
        machine && machine->host.foreign_named_arity_resolved
            ? machine->host.foreign_named_arity_resolved
            : machine ? machine->host.foreign_named_arity : NULL);
}

static PeTTaNamedArity
petta_machine_extension_named_arity_resolving(
    PettaMachineImpl *machine, SymbolId head,
    CettaExprLen supplied) {
    return petta_machine_extension_named_arity_using(
        machine, head, supplied,
        machine ? machine->host.foreign_named_arity_resolving : NULL);
}

static bool petta_machine_extension_callable(
    PettaMachineImpl *machine, Atom *atom) {
    if (!machine || !atom ||
        atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    PeTTaNamedArity arity =
        petta_machine_extension_named_arity(
            machine,
            atom->expr.elems[0]->sym_id,
            atom->expr.len - 1u);
    /* A name the boundary knows only at OTHER arities is data at this
     * occurrence; the reference calls engine predicates at their exact
     * arity alone. */
    return arity.exact;
}

typedef struct {
    Atom *atom;
    const PettaSpecializerPatternNode *pattern;
    const PettaPlanNode *plan;
} PettaDataWalkItem;

static bool petta_machine_is_rigid_data(
    PettaMachineImpl *machine, Atom *root) {
    if (!machine || !root || atom_has_vars(root))
        return false;
    PettaDataWalkItem *stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
#define PETTA_DATA_PUSH(value) do { \
    if (length == capacity) { \
        size_t next = capacity ? capacity * 2u : 32u; \
        if (next <= capacity || \
            next > SIZE_MAX / sizeof(*stack)) { \
            free(stack); \
            return false; \
        } \
        stack = stack \
            ? cetta_realloc(stack, sizeof(*stack) * next) \
            : cetta_malloc(sizeof(*stack) * next); \
        capacity = next; \
    } \
    stack[length++] = (PettaDataWalkItem){ \
        .atom = (value)}; \
} while (0)
    PETTA_DATA_PUSH(root);
    while (length > 0u) {
        PettaDataWalkItem item = stack[--length];
        Atom *atom = item.atom;
        if (!atom) {
            free(stack);
            return false;
        }
        if (atom->kind == ATOM_VAR) {
            free(stack);
            return false;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        SymbolId head = atom_head_symbol_id(atom);
        PeTTaForm form = head == SYMBOL_ID_NONE
            ? PETTA_FORM_NONE : petta_semantics_form(head);
        bool callable =
            (head == g_builtin_syms.quote &&
             !machine->host.quote_is_inert_data) ||
            head == g_builtin_syms.return_text ||
            head == g_builtin_syms.superpose ||
            petta_semantics_boolean_relation_arity(head, NULL) ||
            head == g_builtin_syms.petta_member ||
            head == g_builtin_syms.petta_last ||
            head == g_builtin_syms.reverse ||
            head == g_builtin_syms.if_text ||
            form != PETTA_FORM_NONE ||
            (head != SYMBOL_ID_NONE &&
             space_equations_may_match_known_head(
                 machine->space, head)) ||
            petta_machine_extension_callable(machine, atom) ||
            (machine->host.classify &&
             machine->host.classify(
                 machine->host.context, machine->space, atom) !=
                 PETTA_MACHINE_HOST_NONE);
        if (callable) {
            free(stack);
            return false;
        }
        for (CettaExprIndex index = atom->expr.len;
             index > 1u; index--) {
            PETTA_DATA_PUSH(atom->expr.elems[index - 1u]);
        }
    }
    free(stack);
#undef PETTA_DATA_PUSH
    return true;
}

static bool petta_machine_contains_callable(
    PettaMachineImpl *machine, Atom *root, bool include_root,
    const PettaSpecializerPatternNode *pattern_root,
    const PettaPlanNode *plan_root) {
    if (!machine || !root)
        return false;
    PettaDataWalkItem *stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
#define PETTA_CALLABLE_PUSH(value, item_pattern, item_plan) do { \
    if (length == capacity) { \
        size_t next = capacity ? capacity * 2u : 32u; \
        if (next <= capacity || \
            next > SIZE_MAX / sizeof(*stack)) { \
            free(stack); \
            /* Unknown is callable: never certify a value on exhaustion. */ \
            return true; \
        } \
        stack = stack \
            ? cetta_realloc(stack, sizeof(*stack) * next) \
            : cetta_malloc(sizeof(*stack) * next); \
        capacity = next; \
    } \
    stack[length++] = (PettaDataWalkItem){ \
        .atom = (value), \
        .pattern = (item_pattern), \
        .plan = (item_plan)}; \
} while (0)
    if (root->kind != ATOM_EXPR) {
        free(stack);
        return false;
    }
    CettaExprIndex first = include_root ? 0u : 1u;
    for (CettaExprIndex index = root->expr.len;
         index > first; index--) {
        PETTA_CALLABLE_PUSH(
            root->expr.elems[index - 1u],
            petta_specializer_pattern_child(
                pattern_root, index - 1u),
            petta_plan_child(plan_root, index - 1u));
    }
    while (length > 0u) {
        PettaDataWalkItem item = stack[--length];
        Atom *atom = item.atom;
        if (!atom) {
            free(stack);
            return false;
        }
        /* A source variable is a value occurrence after substitution.  Its
         * expression-shaped value does not become a call retroactively, and
         * no descendant of that value acquires evaluation demand here. */
        if (item.plan && item.plan->role == PETTA_PLAN_VALUE)
            continue;
        if (item.plan &&
            (item.plan->role == PETTA_PLAN_STATIC_CALL ||
             item.plan->role == PETTA_PLAN_DYNAMIC_CALL)) {
            free(stack);
            return true;
        }
        if (atom->kind != ATOM_EXPR ||
            atom->expr.len == 0u) {
            continue;
        }
        const PettaPlanNode *node_plan =
            item.plan && item.plan->child_count == atom->expr.len
                ? item.plan : NULL;
        SymbolId head = atom_head_symbol_id(atom);
        if (head == g_builtin_syms.quote)
            continue;
        PeTTaForm form = head == SYMBOL_ID_NONE
            ? PETTA_FORM_NONE : petta_semantics_form(head);
        bool callable = head != g_builtin_syms.colon &&
            head != g_builtin_syms.arrow &&
            (atom->expr.elems[0]->kind == ATOM_VAR ||
             head == g_builtin_syms.return_text ||
             head == g_builtin_syms.superpose ||
             head == g_builtin_syms.empty_form ||
             petta_semantics_boolean_relation_arity(head, NULL) ||
             head == g_builtin_syms.petta_member ||
             head == g_builtin_syms.petta_last ||
             head == g_builtin_syms.reverse ||
             head == g_builtin_syms.if_text ||
             form != PETTA_FORM_NONE ||
             (head != SYMBOL_ID_NONE &&
              space_equations_may_match_known_head(
                  machine->space, head)) ||
             petta_machine_extension_callable(machine, atom) ||
             (machine->host.classify &&
              machine->host.classify(
                  machine->host.context, machine->space, atom) !=
                  PETTA_MACHINE_HOST_NONE));
        if (callable &&
            !petta_specializer_pattern_is_structural(
                item.pattern)) {
            free(stack);
            return true;
        }
        for (CettaExprIndex index = atom->expr.len;
             index > 1u; index--) {
            PETTA_CALLABLE_PUSH(
                atom->expr.elems[index - 1u],
                petta_specializer_pattern_child(
                    item.pattern, index - 1u),
                petta_plan_child(node_plan, index - 1u));
        }
    }
    free(stack);
#undef PETTA_CALLABLE_PUSH
    return false;
}

static bool petta_machine_callability_authority(
        PettaMachineImpl *machine,
        PettaMachineAuthorityToken *token) {
    if (!machine || !token ||
        !machine->host.callability_authority_token) {
        return false;
    }
    *token = (PettaMachineAuthorityToken){0};
    return machine->host.callability_authority_token(
        machine->host.context, token);
}

static bool petta_space_read_token_eq(
        SpaceReadToken left, SpaceReadToken right) {
    return left.space == right.space &&
        left.instance_id == right.instance_id &&
        left.revision == right.revision;
}

static size_t petta_equation_callability_hash(
        Atom *equation,
        const PettaSpecializerPatternNode *pattern_root) {
    uintptr_t left = (uintptr_t)equation >> 4u;
    uintptr_t right = (uintptr_t)pattern_root >> 4u;
    left ^= left >> 17u;
    right ^= right >> 19u;
    uintptr_t mixed = left ^
        (right + (uintptr_t)UINT64_C(0x9e3779b97f4a7c15) +
         (left << 6u) + (left >> 2u));
    mixed ^= mixed >> 29u;
    return (size_t)mixed;
}

static size_t petta_equation_callability_slot(
        const PettaMachineImpl *machine, Atom *equation,
        const PettaSpecializerPatternNode *pattern_root,
        bool *found) {
    if (found)
        *found = false;
    if (!machine || machine->equation_callability_cap == 0u)
        return SIZE_MAX;
    size_t mask = machine->equation_callability_cap - 1u;
    size_t slot = petta_equation_callability_hash(
        equation, pattern_root) & mask;
    for (size_t probes = 0u;
         probes < machine->equation_callability_cap; probes++) {
        const PettaEquationCallabilityCacheEntry *entry =
            &machine->equation_callability[slot];
        if (!entry->equation)
            return slot;
        if (entry->equation == equation &&
            entry->pattern_root == pattern_root) {
            if (found)
                *found = true;
            return slot;
        }
        slot = (slot + 1u) & mask;
    }
    return SIZE_MAX;
}

static bool petta_equation_callability_rehash(
        PettaMachineImpl *machine, size_t capacity) {
    if (!machine || capacity < 16u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(*machine->equation_callability)) {
        return false;
    }
    PettaEquationCallabilityCacheEntry *entries =
        cetta_malloc(sizeof(*entries) * capacity);
    memset(entries, 0, sizeof(*entries) * capacity);
    PettaEquationCallabilityCacheEntry *previous =
        machine->equation_callability;
    size_t previous_cap = machine->equation_callability_cap;
    size_t mask = capacity - 1u;
    for (size_t index = 0u; index < previous_cap; index++) {
        PettaEquationCallabilityCacheEntry entry = previous[index];
        if (!entry.equation)
            continue;
        size_t slot = petta_equation_callability_hash(
            entry.equation, entry.pattern_root) & mask;
        while (entries[slot].equation)
            slot = (slot + 1u) & mask;
        entries[slot] = entry;
    }
    machine->equation_callability = entries;
    machine->equation_callability_cap = capacity;
    free(previous);
    return true;
}

static bool petta_equation_callability_ensure_capacity(
        PettaMachineImpl *machine, size_t needed) {
    if (!machine)
        return false;
    if (machine->equation_callability_cap == 0u)
        return petta_equation_callability_rehash(machine, 16u);
    if (needed <= machine->equation_callability_cap / 2u)
        return true;
    if (machine->equation_callability_cap > SIZE_MAX / 2u)
        return false;
    return petta_equation_callability_rehash(
        machine, machine->equation_callability_cap * 2u);
}

static void petta_equation_callability_prepare(
        PettaMachineImpl *machine, SpaceReadToken read,
        const PettaMachineAuthorityToken *authority) {
    if (!machine || !authority)
        return;
    if (machine->equation_callability_current &&
        petta_space_read_token_eq(
            machine->equation_callability_read, read) &&
        petta_machine_authority_token_eq(
            &machine->equation_callability_authority, authority)) {
        return;
    }
    if (machine->equation_callability &&
        machine->equation_callability_cap > 0u) {
        memset(
            machine->equation_callability, 0,
            sizeof(*machine->equation_callability) *
                machine->equation_callability_cap);
    }
    machine->equation_callability_len = 0u;
    machine->equation_callability_read = read;
    machine->equation_callability_authority = *authority;
    machine->equation_callability_current = true;
}

/* Classify the complete nested LHS forest once for an exact Space/host
 * authority pair.  A Space mutation or foreign/native authority change
 * clears the table before it can authorize another attempt.  Generated
 * specialization patterns retain their node-specific structural marks, so
 * their identity is part of the key rather than a reason to bypass reuse. */
static bool petta_equation_lhs_contains_callable(
        PettaMachineImpl *machine,
        const PettaClauseCandidate *candidate,
        const PettaSpecializerPatternNode *pattern_root) {
    if (!machine || !candidate || !candidate->equation ||
        candidate->equation->kind != ATOM_EXPR ||
        candidate->equation->expr.len != 3u) {
        return true;
    }
    Atom *lhs = candidate->equation->expr.elems[1];
    if (!machine->host.callability_authority_token) {
        return petta_machine_contains_callable(
            machine, lhs, false, pattern_root, NULL);
    }

    SpaceReadToken read = space_read_token(machine->space);
    PettaMachineAuthorityToken authority = {0};
    if (!petta_machine_callability_authority(
            machine, &authority)) {
        return petta_machine_contains_callable(
            machine, lhs, false, pattern_root, NULL);
    }

    petta_equation_callability_prepare(machine, read, &authority);
    bool found = false;
    size_t slot = petta_equation_callability_slot(
        machine, candidate->equation, pattern_root, &found);
    if (found) {
        return machine->equation_callability[slot].contains_callable;
    }

    bool contains_callable = petta_machine_contains_callable(
        machine, lhs, false, pattern_root, NULL);
    SpaceReadToken confirmed_read = space_read_token(machine->space);
    PettaMachineAuthorityToken confirmed_authority = {0};
    if (!petta_machine_callability_authority(
            machine, &confirmed_authority) ||
        !petta_space_read_token_eq(read, confirmed_read) ||
        !petta_machine_authority_token_eq(
            &authority, &confirmed_authority)) {
        return contains_callable;
    }

    petta_equation_callability_prepare(
        machine, confirmed_read, &confirmed_authority);
    if (!petta_equation_callability_ensure_capacity(
            machine, machine->equation_callability_len + 1u)) {
        return contains_callable;
    }
    slot = petta_equation_callability_slot(
        machine, candidate->equation, pattern_root, &found);
    if (slot == SIZE_MAX)
        return contains_callable;
    if (!found)
        machine->equation_callability_len++;
    machine->equation_callability[slot] =
        (PettaEquationCallabilityCacheEntry){
            .equation = candidate->equation,
            .pattern_root = pattern_root,
            .contains_callable = contains_callable,
        };
    return contains_callable;
}

typedef struct {
    Atom *pattern;
    Atom *value;
    const PettaSpecializerPatternNode *pattern_role;
    bool solve;
} PettaRelationalPatternAction;

static bool petta_machine_callable_root(
    PettaMachineImpl *machine, Atom *atom,
    const PettaSpecializerPatternNode *pattern) {
    if (!machine || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind == ATOM_VAR ||
        petta_specializer_pattern_is_structural(pattern)) {
        return false;
    }
    SymbolId head = atom_head_symbol_id(atom);
    /*
     * A quote in an equation head is a constructor pattern, not a
     * relational subgoal.  Solving it would evaluate away the very
     * structure that the clause is meant to discriminate.
     */
    if (head == g_builtin_syms.quote)
        return false;
    /* `:` and `->` are PeTTa's typed-data constructors.  Their fields may
     * themselves be callable, so callers must still descend through them,
     * but the constructor roots are never relational subgoals.  Keep clause
     * discrimination in lockstep with execution rather than asking the HE
     * host to classify these shared spellings. */
    if (head == g_builtin_syms.colon ||
        head == g_builtin_syms.arrow)
        return false;
    PeTTaForm form = head == SYMBOL_ID_NONE
        ? PETTA_FORM_NONE : petta_semantics_form(head);
    bool callable = head == g_builtin_syms.return_text ||
           head == g_builtin_syms.superpose ||
           head == g_builtin_syms.empty_form ||
           petta_semantics_boolean_relation_arity(head, NULL) ||
           head == g_builtin_syms.petta_member ||
           head == g_builtin_syms.petta_last ||
           head == g_builtin_syms.reverse ||
           head == g_builtin_syms.if_text ||
           form != PETTA_FORM_NONE ||
           (head != SYMBOL_ID_NONE &&
            space_equations_may_match_known_head(
                machine->space, head)) ||
           petta_machine_extension_callable(machine, atom) ||
           (machine->host.classify &&
            machine->host.classify(
                machine->host.context, machine->space, atom) !=
                 PETTA_MACHINE_HOST_NONE);
    return callable;
}

/* Whether one resolved occurrence is already a value under its authored
 * plan.  A substituted value stays a value even when its syntax looks like a
 * call; DATA owns its root constructor but may still contain authored calls
 * below it.  Missing/call plans retain the conservative runtime scan. */
static bool petta_machine_planned_value_ready(
    PettaMachineImpl *machine, Atom *value,
    const PettaPlanNode *plan) {
    if (!machine || !value)
        return false;
    if (plan && plan->role == PETTA_PLAN_VALUE)
        return true;
    if (plan && plan->role == PETTA_PLAN_DATA) {
        return !petta_machine_contains_callable(
            machine, value, false, NULL, plan);
    }
    return !petta_machine_callable_root(
               machine, value, NULL) &&
           !petta_machine_contains_callable(
               machine, value, false, NULL, NULL);
}

typedef struct {
    Atom *pattern;
    Atom *value;
    const PettaSpecializerPatternNode *pattern_role;
} PettaClauseShapePair;

/*
 * Prove only rigid structural impossibility at the instant an alternative is
 * attempted.  This timing is load-bearing: an earlier alternative may add a
 * relation which turns a nested expression pattern into a callable goal.
 * Callable, logical-list, open, or bounded-out shapes therefore retain the
 * canonical matcher.  A false result is a proof that the current matcher
 * would reject before producing a binding; true means possible or unknown.
 */
static bool petta_clause_pattern_may_match_now(
    PettaMachineImpl *machine, Atom *pattern, Atom *value,
    const PettaSpecializerPatternNode *pattern_role,
    bool lhs_structural_for_authority) {
    enum {
        PETTA_CLAUSE_SHAPE_STACK_CAPACITY = 128,
        PETTA_CLAUSE_SHAPE_NODE_LIMIT = 512,
    };
    PettaClauseShapePair stack[
        PETTA_CLAUSE_SHAPE_STACK_CAPACITY];
    size_t length = 0u;
    size_t visited = 0u;
    PeTTaConsShapeFacts cons_facts;

    if (!machine || !pattern || !value ||
        pattern->kind != ATOM_EXPR ||
        value->kind != ATOM_EXPR ||
        !petta_semantics_cons_shape_facts(&cons_facts)) {
        return true;
    }
    CettaExprLen aligned = pattern->expr.len < value->expr.len
        ? pattern->expr.len : value->expr.len;
    if ((size_t)(aligned > 0u ? aligned - 1u : 0u) >
        PETTA_CLAUSE_SHAPE_STACK_CAPACITY) {
        return true;
    }
    for (CettaExprIndex index = 1u; index < aligned; index++) {
        stack[length++] = (PettaClauseShapePair){
            .pattern = pattern->expr.elems[index],
            .value = value->expr.elems[index],
            .pattern_role = petta_specializer_pattern_child(
                pattern_role, index),
        };
    }

    while (length > 0u) {
        if (visited++ >= PETTA_CLAUSE_SHAPE_NODE_LIMIT)
            return true;
        PettaClauseShapePair pair = stack[--length];
        Atom *left = pair.pattern;
        Atom *right = pair.value;
        if (!left || !right || left->kind == ATOM_VAR ||
            right->kind == ATOM_VAR) {
            continue;
        }
        if (petta_semantics_facts_is_open_cons_value(
                &cons_facts, left) ||
            petta_semantics_facts_is_open_cons_value(
                &cons_facts, right)) {
            continue;
        }
        if (petta_semantics_facts_is_cons_constraint(
                &cons_facts, left)) {
            if (!petta_semantics_cons_pattern_may_match(left, right))
                goto impossible;
            continue;
        }
        if (!lhs_structural_for_authority &&
            left->kind == ATOM_EXPR &&
            petta_machine_callable_root(
                machine, left, pair.pattern_role)) {
            continue;
        }
        if (left->kind != right->kind)
            goto impossible;
        if (left->kind != ATOM_EXPR) {
            if (!atom_eq(left, right))
                goto impossible;
            continue;
        }
        if (left->expr.len != right->expr.len)
            goto impossible;
        if ((size_t)left->expr.len >
            PETTA_CLAUSE_SHAPE_STACK_CAPACITY - length) {
            return true;
        }
        for (CettaExprIndex index = 0u;
             index < left->expr.len; index++) {
            stack[length++] = (PettaClauseShapePair){
                .pattern = left->expr.elems[index],
                .value = right->expr.elems[index],
                .pattern_role = petta_specializer_pattern_child(
                    pair.pattern_role, index),
            };
        }
    }
    return true;

impossible:
    return !petta_semantics_cons_shape_facts_current(&cons_facts);
}

typedef struct {
    PettaMachineImpl *machine;
    const PettaClauseCandidate *candidates;
    size_t candidate_count;
} PettaMatchDecisionContext;

static void petta_match_decision_cache_entry_free(
    PettaMatchDecisionCacheEntry *entry) {
    if (!entry)
        return;
    cetta_match_decision_free(entry->decision);
    free(entry->equations);
    memset(entry, 0, sizeof(*entry));
}

static void petta_match_decision_cache_drop_stale(
    PettaMachineImpl *machine) {
    if (!machine)
        return;
    size_t write = 0u;
    for (size_t read = 0u;
         read < machine->match_decision_len; read++) {
        PettaMatchDecisionCacheEntry *entry =
            &machine->match_decisions[read];
        if (!cetta_match_decision_is_current(
                entry->decision, machine->space,
                machine->host.match_decision_semantics)) {
            petta_match_decision_cache_entry_free(entry);
            continue;
        }
        if (write != read) {
            machine->match_decisions[write] = *entry;
            memset(entry, 0, sizeof(*entry));
        }
        write++;
    }
    machine->match_decision_len = write;
}

static bool petta_match_decision_same_equations(
    const PettaMatchDecisionCacheEntry *entry,
    const PettaClauseCandidate *candidates,
    size_t candidate_count,
    const PettaClauseCandidate *snapshot_identity) {
    if (!entry || !candidates ||
        entry->equation_len != candidate_count)
        return false;
    if (entry->snapshot_identity || snapshot_identity) {
        return entry->snapshot_identity == snapshot_identity;
    }
    for (size_t index = 0u; index < candidate_count; index++) {
        if (entry->equations[index] != candidates[index].equation)
            return false;
    }
    return true;
}

static const PettaSpecializerPatternNode *
petta_match_decision_pattern_role(
    const PettaMatchDecisionContext *context,
    uint32_t source_ref, const CettaExprIndex *path,
    uint32_t path_len) {
    if (!context || !context->machine || !context->candidates ||
        source_ref >= context->candidate_count)
        return NULL;
    Atom *equation = context->candidates[source_ref].equation;
    const PettaSpecializerPatternNode *role =
        petta_specializer_pattern_root(
            context->machine->space, equation);
    for (uint32_t depth = 0u;
         role && depth < path_len; depth++) {
        role = petta_specializer_pattern_child(role, path[depth]);
    }
    return role;
}

static CettaMatchDecisionPatternClass
petta_match_decision_classify_pattern(
    void *raw_context, uint32_t source_ref,
    const CettaExprIndex *path, uint32_t path_len,
    Atom *pattern) {
    PettaMatchDecisionContext *context = raw_context;
    if (!context || !context->machine || !pattern)
        return CETTA_MATCH_DECISION_PATTERN_OPAQUE;
    const PettaSpecializerPatternNode *role =
        petta_match_decision_pattern_role(
            context, source_ref, path, path_len);
    if (petta_specializer_pattern_is_structural(role))
        return CETTA_MATCH_DECISION_PATTERN_STRUCTURAL;
    if (petta_semantics_is_open_cons_value(pattern) ||
        petta_semantics_is_cons_constraint(pattern))
        return CETTA_MATCH_DECISION_PATTERN_OPAQUE;
    const PettaClauseCandidate *candidate =
        source_ref < context->candidate_count
            ? &context->candidates[source_ref] : NULL;
    bool lhs_structural_for_authority = candidate &&
        !petta_equation_lhs_contains_callable(
            context->machine, candidate,
            petta_specializer_pattern_root(
                context->machine->space,
                candidate->equation));
    if (!lhs_structural_for_authority &&
        pattern->kind == ATOM_EXPR &&
        petta_machine_callable_root(
            context->machine, pattern, role)) {
        return CETTA_MATCH_DECISION_PATTERN_OPAQUE;
    }
    return CETTA_MATCH_DECISION_PATTERN_STRUCTURAL;
}

static bool petta_match_decision_verify_candidate(
    void *raw_context, uint32_t source_ref,
    Atom *pattern, Atom *query) {
    PettaMatchDecisionContext *context = raw_context;
    if (!context || !context->machine || !context->candidates ||
        source_ref >= context->candidate_count)
        return true;
    const PettaClauseCandidate *candidate =
        &context->candidates[source_ref];
    const PettaSpecializerPatternNode *pattern_root =
        petta_specializer_pattern_root(
            context->machine->space, candidate->equation);
    bool lhs_structural_for_authority =
        !petta_equation_lhs_contains_callable(
            context->machine, candidate, pattern_root);
    if (!petta_clause_pattern_may_match_now(
            context->machine, pattern, query, pattern_root,
            lhs_structural_for_authority)) {
        return false;
    }
    /* Shape filtering cannot decide a nonlinear head such as `(same $x
     * $x)` against two concrete arguments.  When the LHS is ordinary
     * structural syntax, run the authoritative rule-local matcher in an
     * isolated trail so candidate selection observes exactly the same
     * aliases and occurs check as clause entry without changing the live
     * branch.  Relational and open-cons heads retain their specialized
     * runtime matching paths. */
    if (!lhs_structural_for_authority ||
        (candidate->activation_layout.lhs == pattern &&
         candidate->activation_layout.
             lhs_contains_cons_constraint_valid
             ? candidate->activation_layout.
                   lhs_contains_cons_constraint
             : petta_semantics_contains_cons_constraint(pattern))) {
        return true;
    }
    BindingsBuilder verifier;
    if (!bindings_builder_init(&verifier, NULL))
        return true;
    bool matched = match_atoms_epoch_builder_rule_local(
        query, pattern, &verifier, &context->machine->heap,
        fresh_var_suffix());
    if (matched && bindings_has_loop(
            (Bindings *)bindings_builder_bindings(&verifier))) {
        matched = false;
    }
    bindings_builder_free(&verifier);
    return matched;
}

typedef enum {
    PETTA_MATCH_DECISION_SETTING_DEFAULT = 0,
    PETTA_MATCH_DECISION_SETTING_LINEAR,
    PETTA_MATCH_DECISION_SETTING_OFF,
} PettaMatchDecisionSetting;

static PettaMatchDecisionSetting petta_match_decision_setting(void) {
    static _Thread_local int setting = -1;
    if (setting < 0) {
        const char *value = getenv("CETTA_PETTA_MATCH_DECISION");
        setting = value && strcmp(value, "linear") == 0
            ? PETTA_MATCH_DECISION_SETTING_LINEAR
            : value && (strcmp(value, "0") == 0 ||
                        strcmp(value, "off") == 0)
                ? PETTA_MATCH_DECISION_SETTING_OFF
                : PETTA_MATCH_DECISION_SETTING_DEFAULT;
    }
    return (PettaMatchDecisionSetting)setting;
}

static bool petta_match_decision_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0)
        enabled = getenv("CETTA_PETTA_MATCH_DECISION_TRACE") != NULL;
    return enabled != 0;
}

static CettaMatchDecisionMode petta_match_decision_mode(
    bool deep_admissible) {
    if (!deep_admissible ||
        petta_match_decision_setting() ==
            PETTA_MATCH_DECISION_SETTING_LINEAR) {
        return CETTA_MATCH_DECISION_LINEAR;
    }
    return CETTA_MATCH_DECISION_DEEP;
}

static CettaMatchDecision *petta_match_decision_prepare(
    PettaMachineImpl *machine, SymbolId head, CettaExprLen arity,
    const PettaClauseCandidate *candidates, size_t candidate_count,
    const PettaClauseCandidate *snapshot_identity,
    CettaMatchDecisionMode mode,
    PettaMatchDecisionContext *context) {
    if (!machine || !candidates || candidate_count == 0u || !context ||
        petta_match_decision_setting() ==
            PETTA_MATCH_DECISION_SETTING_OFF)
        return NULL;
    petta_match_decision_cache_drop_stale(machine);
    for (size_t index = 0u;
         index < machine->match_decision_len; index++) {
        PettaMatchDecisionCacheEntry *entry =
            &machine->match_decisions[index];
        if (entry->head == head && entry->arity == arity &&
            entry->mode == mode &&
            petta_match_decision_same_equations(
                entry, candidates, candidate_count,
                snapshot_identity)) {
            machine->stats.match_decision_cache_hits++;
            return entry->decision;
        }
    }

    CettaMatchDecisionClause *clauses =
        malloc(sizeof(*clauses) * candidate_count);
    if (!clauses)
        return NULL;
    for (size_t index = 0u; index < candidate_count; index++) {
        Atom *equation = candidates[index].equation;
        clauses[index] = (CettaMatchDecisionClause){
            .pattern = equation && equation->kind == ATOM_EXPR &&
                       equation->expr.len == 3u
                ? equation->expr.elems[1] : NULL,
            .source_ref = (uint32_t)index,
        };
    }
    SpaceReadToken read = space_read_token(machine->space);
    CettaMatchDecision *decision = cetta_match_decision_compile(
        read, machine->host.match_decision_semantics,
        clauses, candidate_count, mode, 0u,
        mode == CETTA_MATCH_DECISION_DEEP
            ? petta_match_decision_classify_pattern : NULL,
        context);
    free(clauses);
    if (!decision)
        return NULL;
    CettaMatchDecisionStats compiled_stats = {0};
    cetta_match_decision_stats(decision, &compiled_stats);
    machine->stats.match_decision_key_index_build_probes +=
        compiled_stats.key_index_build_probes;
    if (petta_match_decision_trace_enabled()) {
        fprintf(
            stderr,
            "[petta-match-decision] compile head=%s arity=%u "
            "clauses=%zu backend=%s revision=%" PRIu64 "\n",
            symbol_bytes(g_symbols, head), (unsigned)arity,
            candidate_count,
            mode == CETTA_MATCH_DECISION_DEEP ? "deep" : "linear",
            read.revision);
    }

    Atom **equations = NULL;
    if (!snapshot_identity) {
        equations = malloc(sizeof(*equations) * candidate_count);
        if (!equations) {
            cetta_match_decision_free(decision);
            return NULL;
        }
        for (size_t index = 0u; index < candidate_count; index++)
            equations[index] = candidates[index].equation;
    }
    if (!petta_machine_reserve(
            (void **)&machine->match_decisions,
            &machine->match_decision_cap,
            machine->match_decision_len + 1u,
            sizeof(*machine->match_decisions))) {
        free(equations);
        cetta_match_decision_free(decision);
        return NULL;
    }
    machine->match_decisions[machine->match_decision_len++] =
        (PettaMatchDecisionCacheEntry){
            .read = read,
            .head = head,
            .arity = arity,
            .mode = mode,
            .snapshot_identity = snapshot_identity,
            .equations = equations,
            .equation_len = candidate_count,
            .decision = decision,
        };
    machine->stats.match_decision_compilations++;
    return decision;
}

static CettaMatchDecisionSelectState
petta_match_decision_select_candidates(
    PettaMachineImpl *machine, SymbolId head,
    const PettaSpaceQueryView *query,
    const PettaClauseCandidate *candidates, size_t candidate_count,
    const PettaClauseCandidate *snapshot_identity,
    bool deep_admissible,
    const uint32_t **selected, size_t *selected_count,
    bool *structurally_verified) {
    if (structurally_verified)
        *structurally_verified = false;
    if (!machine || !query || !query->head ||
        (!query->whole && query->arity > 0u && !query->arguments) ||
        !candidates || candidate_count == 0u ||
        !selected || !selected_count || !structurally_verified)
        return CETTA_MATCH_DECISION_SELECT_ERROR;
    PettaMatchDecisionContext context = {
        .machine = machine,
        .candidates = candidates,
        .candidate_count = candidate_count,
    };
    CettaMatchDecisionMode mode =
        petta_match_decision_mode(deep_admissible);
    CettaMatchDecision *decision = petta_match_decision_prepare(
        machine, head, query->arity,
        candidates, candidate_count, snapshot_identity,
        mode, &context);
    if (!decision)
        return CETTA_MATCH_DECISION_SELECT_ERROR;

    CettaMatchDecisionStats before = {0};
    CettaMatchDecisionStats after = {0};
    cetta_match_decision_stats(decision, &before);
    CettaMatchDecisionSelectState state = query->whole
        ? cetta_match_decision_select(
              decision, machine->space,
              machine->host.match_decision_semantics,
              query->whole, UINT64_MAX,
              mode == CETTA_MATCH_DECISION_DEEP
                  ? petta_match_decision_verify_candidate : NULL,
              &context, selected, selected_count)
        : cetta_match_decision_select_parts(
              decision, machine->space,
              machine->host.match_decision_semantics,
              query->head, query->arguments,
              query->arity, UINT64_MAX,
              selected, selected_count);
    /* The split-register selector deliberately returns a candidate
     * superset: it cannot verify a nonlinear pattern spanning two argument
     * fields.  Most calls become singleton by indexing alone.  For the
     * remaining source-bound ambiguity, instantiate one logical query view
     * and let the same deep verifier used by whole calls remove impossible
     * alternatives before a choice point is captured.  Bound payloads remain
     * shared; only the authored call shell is reconstructed. */
    if (state == CETTA_MATCH_DECISION_SELECT_READY &&
        *selected_count > 1u && !query->whole &&
        mode == CETTA_MATCH_DECISION_DEEP &&
        query->source && query->source_template &&
        query->source_epoch != 0u) {
        Atom *resolved =
            petta_machine_apply_bindings_epoch_then_all(
                machine,
                (Bindings *)search_context_bindings(
                    &machine->search),
                &machine->heap, query->source,
                query->source_epoch,
                query->source_first_entry);
        if (!resolved) {
            state = CETTA_MATCH_DECISION_SELECT_ERROR;
        } else {
            state = cetta_match_decision_select(
                decision, machine->space,
                machine->host.match_decision_semantics,
                resolved, UINT64_MAX,
                petta_match_decision_verify_candidate,
                &context, selected, selected_count);
            if (state == CETTA_MATCH_DECISION_SELECT_READY)
                *structurally_verified = true;
        }
    }
    cetta_match_decision_stats(decision, &after);
    machine->stats.match_decision_runs += after.runs - before.runs;
    machine->stats.match_decision_clause_inputs +=
        after.clause_inputs - before.clause_inputs;
    machine->stats.match_decision_clause_survivors +=
        after.clause_survivors - before.clause_survivors;
    machine->stats.match_decision_key_index_select_probes +=
        after.key_index_select_probes - before.key_index_select_probes;
    machine->stats.match_decision_generic_key_policy_scans +=
        after.generic_key_policy_scans - before.generic_key_policy_scans;
    machine->stats.match_decision_linear_fallbacks +=
        after.linear_fallbacks - before.linear_fallbacks;
    machine->stats.match_decision_unavailable_path_fallbacks +=
        after.unavailable_path_fallbacks -
        before.unavailable_path_fallbacks;
    if (state == CETTA_MATCH_DECISION_SELECT_INVALIDATED)
        machine->stats.match_decision_invalidations++;
    if (state == CETTA_MATCH_DECISION_SELECT_READY &&
        !*structurally_verified) {
        *structurally_verified = query->whole &&
            mode == CETTA_MATCH_DECISION_DEEP;
    }
    return state;
}

/*
 * Equation heads may contain a higher-order application pattern such as
 * `($f (cons $x $xs))`.  Its variable-headed spine is structural—the head
 * binds to the demanded callable—while nested known calls remain relational.
 * Build the constraint goals iteratively so deeply nested patterns do not
 * reintroduce a C-stack limit into the explicit search machine.
 */
static bool petta_push_relational_pattern(
    PettaMachineImpl *machine, Atom *pattern, Atom *value,
    uint32_t barrier,
    const PettaSpecializerPatternNode *pattern_role,
    uint32_t activation_epoch,
    uint32_t activation_first_entry) {
    PettaRelationalPatternAction *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    PettaRelationalPatternAction *actions = NULL;
    size_t action_len = 0u;
    size_t action_cap = 0u;

    if (!petta_machine_reserve(
            (void **)&work, &work_cap, 1u, sizeof(*work))) {
        return false;
    }
    work[work_len++] = (PettaRelationalPatternAction){
        .pattern = pattern,
        .value = value,
        .pattern_role = pattern_role,
    };

    while (work_len > 0u) {
        PettaRelationalPatternAction item = work[--work_len];
        Atom *left = item.pattern;
        Atom *right = item.value;
        bool structural =
            left && right &&
            left->kind == ATOM_EXPR &&
            right->kind == ATOM_EXPR &&
            left->expr.len == right->expr.len &&
            left->expr.len > 0u &&
            (petta_specializer_pattern_is_structural(
                 item.pattern_role) ||
             left->expr.elems[0]->kind == ATOM_VAR ||
             petta_machine_contains_callable(
                 machine, left, false,
                 item.pattern_role, NULL));
        if (structural &&
            !petta_machine_callable_root(
                machine, left, item.pattern_role)) {
            if ((uint64_t)left->expr.len >
                (uint64_t)(SIZE_MAX - work_len)) {
                free(actions);
                free(work);
                return false;
            }
            if (!petta_machine_reserve(
                    (void **)&work, &work_cap,
                    work_len + (size_t)left->expr.len,
                    sizeof(*work))) {
                free(actions);
                free(work);
                return false;
            }
            for (CettaExprIndex index = left->expr.len;
                 index > 0u; index--) {
                CettaExprIndex child = index - 1u;
                work[work_len++] =
                    (PettaRelationalPatternAction){
                        .pattern = left->expr.elems[child],
                        .value = right->expr.elems[child],
                        .pattern_role =
                            petta_specializer_pattern_child(
                                item.pattern_role, child),
                    };
            }
            continue;
        }

        if (!petta_machine_reserve(
                (void **)&actions, &action_cap,
                action_len + 1u, sizeof(*actions))) {
            free(actions);
            free(work);
            return false;
        }
        actions[action_len++] =
            (PettaRelationalPatternAction){
                .pattern = left,
                .value = right,
                .pattern_role = item.pattern_role,
                .solve =
                    petta_machine_callable_root(
                        machine, left, item.pattern_role),
            };
    }
    free(work);

    for (size_t index = action_len; index > 0u; index--) {
        PettaRelationalPatternAction *action =
            &actions[index - 1u];
        bool pushed;
        if (activation_epoch != 0u) {
            pushed = action->solve
                ? petta_push_solve_activation_planned(
                      machine, action->pattern, action->value,
                      barrier, NULL, NULL, activation_epoch,
                      activation_first_entry)
                : petta_goal_push(
                      machine,
                      (PettaGoal){
                          .kind = PETTA_GOAL_UNIFY,
                          .barrier = barrier,
                          .first = action->pattern,
                          .second = action->value,
                          .activation_epoch = activation_epoch,
                          .activation_first_entry =
                              activation_first_entry,
                          .activation_source_fields =
                              PETTA_ACTIVATION_SOURCE_FIRST,
                      });
        } else {
            pushed = action->solve
                ? petta_push_solve(
                      machine, action->pattern, action->value,
                      barrier)
                : petta_push_unify(
                      machine, action->pattern, action->value,
                      barrier);
        }
        if (!pushed) {
            free(actions);
            return false;
        }
    }
    free(actions);
    return true;
}

static bool petta_machine_start_clause_choice(
    PettaMachineImpl *machine, Atom *query, Atom *expected,
    uint32_t barrier, bool evaluate_result,
    bool count_collection_result, Atom *query_source,
    const PettaEquationTemplate *query_template,
    uint32_t query_epoch, uint32_t query_first_entry);

static bool petta_machine_start_space_query(
    PettaMachineImpl *machine, const PettaSpaceQueryView *query,
    Atom *expected, uint32_t barrier, bool evaluate_result,
    bool count_collection_result);

static bool petta_machine_start_outcome_choice(
    PettaMachineImpl *machine, OutcomeSet *outcomes, Atom *expected,
    uint32_t barrier);

static bool petta_machine_try_extension_call(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, bool *recognized,
    PettaMachineStep *failure);

static bool petta_machine_start_tabled_call(
    PettaMachineImpl *machine, Atom *query, Atom *expected,
    uint32_t barrier, bool *handled,
    PettaMachineStep *failure);

static bool petta_machine_start_collapse(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure);

static bool petta_machine_start_count_collapse(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    bool wrap_collection, PettaMachineStep *failure);

static bool petta_machine_start_append_choice(
    PettaMachineImpl *machine, Atom *whole, Atom *left, Atom *right,
    uint32_t barrier);

static bool petta_machine_start_match_choice(
    PettaMachineImpl *machine, Space *space, Atom *pattern,
    Atom *template, Atom *expected, uint32_t barrier,
    const PettaPlanNode *template_plan);

static Atom *petta_machine_lower_match_conjunction(
    PettaMachineImpl *machine, Atom *reference,
    Atom *pattern, Atom *template) {
    if (!machine || !reference || !pattern || !template ||
        pattern->kind != ATOM_EXPR ||
        pattern->expr.len == 0u ||
        !atom_is_symbol_id(
            pattern->expr.elems[0], g_builtin_syms.comma)) {
        return NULL;
    }
    /*
     * This is the correctness fallback for conjunctive match.  A product
     * plan may replace it only when the store proves that plan admissible;
     * otherwise nested logical-update-view matches preserve shared
     * variables, answer order, multiplicity, and effects.
     */
    Atom *body = template;
    for (CettaExprIndex index = pattern->expr.len;
         index > 1u; index--) {
        Atom *elements[4] = {
            atom_symbol_id(
                &machine->heap, g_builtin_syms.match),
            reference,
            pattern->expr.elems[index - 1u],
            body,
        };
        body = atom_expr(&machine->heap, elements, 4u);
        if (!body)
            return NULL;
    }
    return body;
}

typedef struct {
    Atom *slot;
    Atom *call;
} PettaFunctionalPatternGoal;

typedef struct {
    PettaFunctionalPatternGoal *items;
    size_t len;
    size_t cap;
} PettaFunctionalPatternGoals;

static bool petta_machine_named_space_has_schema(
    PettaMachineImpl *machine, Atom *reference) {
    if (!machine || !reference || reference->kind != ATOM_SYMBOL)
        return false;
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(
        machine->space, &machine->heap, reference, &types);
    bool found = false;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *type = types[index];
        if (type && type->kind == ATOM_EXPR &&
            type->expr.len == 2u &&
            petta_symbol_name_is(
                atom_head_symbol_id(type), "SpaceOf")) {
            found = true;
            break;
        }
    }
    free(types);
    return found;
}

static bool petta_machine_unique_det_pattern_call(
    PettaMachineImpl *machine, Atom *pattern) {
    if (!machine || !pattern || pattern->kind != ATOM_EXPR ||
        pattern->expr.len < 2u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        petta_symbol_name_is(
            pattern->expr.elems[0]->sym_id, "cons")) {
        return false;
    }
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(
        machine->space, &machine->heap,
        pattern->expr.elems[0], &types);
    uint32_t det_count = 0u;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *type = types[index];
        if (!type || type->kind != ATOM_EXPR ||
            type->expr.len != pattern->expr.len + 1u ||
            type->expr.len == 0u ||
            type->expr.elems[0]->kind != ATOM_SYMBOL) {
            continue;
        }
        const char *mode = symbol_bytes(
            g_symbols, type->expr.elems[0]->sym_id);
        if (mode &&
            (strcmp(mode, "-[det]->") == 0 ||
             strcmp(mode, "-[deterministic]->") == 0)) {
            det_count++;
        }
    }
    free(types);
    return det_count == 1u;
}

static bool petta_functional_pattern_goals_push(
    PettaFunctionalPatternGoals *goals,
    PettaFunctionalPatternGoal goal) {
    if (!goals || goals->len == SIZE_MAX)
        return false;
    if (goals->len == goals->cap) {
        size_t next = goals->cap ? goals->cap * 2u : 8u;
        if (next <= goals->cap ||
            next > SIZE_MAX / sizeof(*goals->items)) {
            return false;
        }
        goals->items = goals->items
            ? cetta_realloc(
                  goals->items, next * sizeof(*goals->items))
            : cetta_malloc(next * sizeof(*goals->items));
        goals->cap = next;
    }
    goals->items[goals->len++] = goal;
    return true;
}

typedef struct {
    Atom *source;
    Atom **children;
    CettaExprIndex next;
    Atom **result_slot;
} PettaFunctionalPatternFrame;

static bool petta_functional_pattern_frame_reserve(
    PettaFunctionalPatternFrame **frames,
    size_t *capacity, size_t required) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2u : 32u;
    while (next < required) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**frames))
        return false;
    void *grown = realloc(*frames, sizeof(**frames) * next);
    if (!grown)
        return false;
    *frames = grown;
    *capacity = next;
    return true;
}

static Atom *petta_machine_elaborate_functional_pattern(
    PettaMachineImpl *machine, Atom *pattern,
    PettaFunctionalPatternGoals *goals, bool *ok) {
    if (!machine || !pattern || !goals || !ok || !*ok) {
        if (ok)
            *ok = false;
        return pattern;
    }

    Atom *result = NULL;
    PettaFunctionalPatternFrame *frames = NULL;
    size_t length = 0u;
    size_t capacity = 0u;

#define PETTA_FUNCTIONAL_PATTERN_PUSH(source_atom, destination_slot) do { \
    Atom *petta_source__ = (source_atom); \
    Atom **petta_slot__ = (destination_slot); \
    if (!petta_source__ || !petta_slot__) \
        goto fail; \
    if (petta_source__->kind != ATOM_EXPR || \
        petta_source__->expr.len == 0u) { \
        *petta_slot__ = petta_source__; \
        break; \
    } \
    if (petta_machine_unique_det_pattern_call( \
            machine, petta_source__)) { \
        Atom *petta_fresh__ = petta_fresh_variable(machine); \
        if (!petta_fresh__ || \
            !petta_functional_pattern_goals_push( \
                goals, (PettaFunctionalPatternGoal){ \
                    .slot = petta_fresh__, \
                    .call = petta_source__, \
                })) { \
            goto fail; \
        } \
        *petta_slot__ = petta_fresh__; \
        break; \
    } \
    if (!cetta_expr_len_mul_fits_size( \
            petta_source__->expr.len, sizeof(Atom *)) || \
        !petta_functional_pattern_frame_reserve( \
            &frames, &capacity, length + 1u)) { \
        goto fail; \
    } \
    Atom **petta_children__ = cetta_malloc( \
        sizeof(*petta_children__) * \
            (size_t)petta_source__->expr.len); \
    petta_children__[0] = petta_source__->expr.elems[0]; \
    frames[length++] = (PettaFunctionalPatternFrame){ \
        .source = petta_source__, \
        .children = petta_children__, \
        .next = 1u, \
        .result_slot = petta_slot__, \
    }; \
} while (0)

    PETTA_FUNCTIONAL_PATTERN_PUSH(pattern, &result);
    while (length > 0u) {
        PettaFunctionalPatternFrame *frame = &frames[length - 1u];
        if (frame->next < frame->source->expr.len) {
            CettaExprIndex index = frame->next++;
            PETTA_FUNCTIONAL_PATTERN_PUSH(
                frame->source->expr.elems[index],
                &frame->children[index]);
            continue;
        }
        bool changed = false;
        for (CettaExprIndex index = 1u;
             index < frame->source->expr.len; index++) {
            changed = changed ||
                frame->children[index] !=
                    frame->source->expr.elems[index];
        }
        Atom *built = changed
            ? atom_expr(
                  &machine->heap, frame->children,
                  frame->source->expr.len)
            : frame->source;
        free(frame->children);
        frame->children = NULL;
        if (!built)
            goto fail;
        *frame->result_slot = built;
        length--;
    }
    free(frames);
#undef PETTA_FUNCTIONAL_PATTERN_PUSH
    return result;

fail:
    for (size_t index = 0u; index < length; index++)
        free(frames[index].children);
    free(frames);
    *ok = false;
#undef PETTA_FUNCTIONAL_PATTERN_PUSH
    return pattern;
}

static bool petta_machine_elaborate_typed_match_pattern(
    PettaMachineImpl *machine, Atom *reference, Atom **pattern_io,
    Atom **template_io) {
    if (!machine || !reference || !pattern_io || !*pattern_io ||
        !template_io || !*template_io) {
        return false;
    }
    if (!petta_machine_named_space_has_schema(machine, reference))
        return true;

    PettaFunctionalPatternGoals goals = {0};
    bool ok = true;
    Atom *pattern = petta_machine_elaborate_functional_pattern(
        machine, *pattern_io, &goals, &ok);
    Atom *body = *template_io;
    Atom *let_symbol = atom_symbol_id(
        &machine->heap, g_builtin_syms.let);
    for (size_t index = goals.len; ok && index > 0u; index--) {
        PettaFunctionalPatternGoal *goal = &goals.items[index - 1u];
        Atom *elements[4] = {
            let_symbol, goal->slot, goal->call, body,
        };
        body = let_symbol
            ? atom_expr(&machine->heap, elements, 4u)
            : NULL;
        ok = body != NULL;
    }
    free(goals.items);
    if (!ok || !pattern || !body)
        return false;
    *pattern_io = pattern;
    *template_io = body;
    return true;
}

/*
 * Append's operands are list-valued computations.  A list whose first item
 * is itself an expression cannot be a direct named or variable-headed call,
 * so a known segment constrains each item independently.  Other operands
 * retain the ordinary solve path, including calls that compute whole lists.
 */
static bool petta_push_append_operand(
    PettaMachineImpl *machine, Atom *operand, Atom *segment,
    uint32_t barrier) {
    if (!machine || !operand || !segment)
        return false;
    if (operand->kind != ATOM_EXPR ||
        segment->kind != ATOM_EXPR ||
        operand->expr.len != segment->expr.len ||
        operand->expr.len == 0u ||
        operand->expr.elems[0]->kind != ATOM_EXPR) {
        return petta_push_solve(
            machine, operand, segment, barrier);
    }
    for (CettaExprIndex index = operand->expr.len;
         index > 0u; index--) {
        CettaExprIndex item = index - 1u;
        if (!petta_push_solve(
                machine, operand->expr.elems[item],
                segment->expr.elems[item], barrier)) {
            return false;
        }
    }
    return true;
}

static bool petta_machine_start_transaction(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    if (!machine || !body || !expected ||
        !machine->host.transaction_begin ||
        !machine->host.transaction_commit ||
        !machine->host.transaction_rollback) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }

    void *handle = NULL;
    Space *transaction_space = NULL;
    Space *previous_space = machine->space;
    if (!machine->host.transaction_begin(
            machine->host.context, previous_space, &machine->heap,
            &handle, &transaction_space) ||
        !handle || !transaction_space) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }

    size_t entry_choice_len = machine->choice_len;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_TRANSACTION,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.transaction = {
            .handle = handle,
            .previous_space = previous_space,
            .active = true,
        },
    };
    if (!petta_choice_push(machine, choice)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    machine->space = transaction_space;
    size_t transaction_choice = machine->choice_len - 1u;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_TRANSACTION_COMMIT,
                .barrier = barrier,
                .choice_index = transaction_choice,
            }) ||
        !petta_push_solve_planned(
            machine, body, expected,
            (uint32_t)machine->choice_len, plan)) {
        petta_choice_truncate(machine, entry_choice_len);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return true;
}

static bool petta_machine_start_once(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    if (!machine || !body || !expected) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    size_t entry_choice_len = machine->choice_len;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_ONCE,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
    };
    if (!petta_choice_push(machine, choice)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    size_t once_choice = machine->choice_len - 1u;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_ONCE_COMMIT,
                .barrier = barrier,
                .choice_index = once_choice,
            }) ||
        !petta_push_solve_planned(
            machine, body, expected,
            (uint32_t)machine->choice_len, plan)) {
        petta_choice_truncate(machine, entry_choice_len);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return true;
}

static bool petta_machine_start_mutex(
    PettaMachineImpl *machine, Atom *name, Atom *body,
    Atom *expected, uint32_t barrier,
    const PettaPlanNode *body_plan,
    PettaMachineStep *failure) {
    if (!machine || !name || !body || !expected ||
        !machine->host.mutex_acquire ||
        !machine->host.mutex_release) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    void *handle = NULL;
    if (!machine->host.mutex_acquire(
            machine->host.context, &machine->heap,
            name, &handle) ||
        !handle) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    size_t entry_choice_len = machine->choice_len;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_MUTEX,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.mutex = {
            .handle = handle,
            .active = true,
        },
    };
    if (!petta_choice_push(machine, choice)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    size_t mutex_choice = machine->choice_len - 1u;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_MUTEX_RELEASE,
                .barrier = barrier,
                .choice_index = mutex_choice,
            }) ||
        !petta_push_solve_planned(
            machine, body, expected,
            (uint32_t)machine->choice_len, body_plan)) {
        petta_choice_truncate(machine, entry_choice_len);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return true;
}

/* Determinism-annotated arrows (`-[mode]->`, extended PeTTa) are
   deliberately absent here: the reference performs no runtime type
   acceptance — its determinism/type checks run statically at load — and a
   mode-arrow-typed head with clauses dispatches like any untyped relation,
   while a clauseless one declares a constructor's field types (the chainer
   types `:`, `STV`, and its record heads this way, including
   mode-POLYMORPHIC arrows such as `-[$e]->` that no symbol-level
   recognizer could match).  Load-time mode discipline consumes the arrow
   names separately. */
static bool petta_machine_type_signature_applies(
    const Atom *type, CettaExprLen nargs) {
    return type && type->kind == ATOM_EXPR &&
           type->expr.len == nargs + 2u &&
           atom_is_symbol_id(
               type->expr.elems[0], g_builtin_syms.arrow);
}

static bool petta_machine_type_is_atom_data(const Atom *type) {
    return type && type->kind == ATOM_SYMBOL &&
           type->sym_id == g_builtin_syms.atom;
}

static bool petta_machine_type_is_unconstrained(const Atom *type) {
    return type && type->kind == ATOM_SYMBOL &&
           (type->sym_id == g_builtin_syms.undefined_type ||
            petta_symbol_name_is(type->sym_id, "_"));
}

/* A dependent domain `(: $value Type)` has two independent runtime duties:
 * check the argument against `Type`, then bind `$value` to the accepted
 * argument so later domains and the codomain can depend on it.  The wrapper
 * is telescope structure, not itself an expected runtime type. */
static bool petta_machine_split_dependent_domain(
    Atom *domain, Atom **binder_out, Atom **formal_out) {
    if (binder_out)
        *binder_out = NULL;
    if (formal_out)
        *formal_out = domain;
    if (!domain || domain->kind != ATOM_EXPR ||
        domain->expr.len != 3u ||
        !atom_is_symbol_id(
            domain->expr.elems[0], g_builtin_syms.colon) ||
        domain->expr.elems[1]->kind != ATOM_VAR) {
        return false;
    }
    if (binder_out)
        *binder_out = domain->expr.elems[1];
    if (formal_out)
        *formal_out = domain->expr.elems[2];
    return true;
}

static bool petta_machine_schedule_typed_call(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    Atom *type, uint32_t barrier, bool overload_dispatch,
    const PettaPlanNode *plan) {
    CettaExprLen length = expression->expr.len;
    CettaExprLen nargs = length - 1u;
    if (!petta_machine_type_signature_applies(type, nargs) ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) {
        return false;
    }
    Atom **ready_elements = arena_alloc(
        &machine->heap, sizeof(*ready_elements) * (size_t)length);
    if (!ready_elements)
        return false;
    ready_elements[0] = expression->expr.elems[0];
    for (CettaExprIndex index = 1u; index < length; index++) {
        Atom *formal = NULL;
        (void)petta_machine_split_dependent_domain(
            type->expr.elems[index], NULL, &formal);
        ready_elements[index] =
            petta_machine_type_is_atom_data(formal)
                ? expression->expr.elems[index]
                : petta_fresh_variable(machine);
        if (!ready_elements[index])
            return false;
    }
    Atom *ready = atom_expr(&machine->heap, ready_elements, length);
    Atom *result = petta_fresh_variable(machine);
    Atom *result_type = type->expr.elems[nargs + 1u];
    bool result_is_atom_data =
        petta_machine_type_is_atom_data(result_type);
    bool result_requires_check =
        !result_is_atom_data &&
        !petta_machine_type_is_unconstrained(result_type);
    PettaGoalKind type_goal = overload_dispatch
        ? PETTA_GOAL_TYPE_MATCH : PETTA_GOAL_TYPE_ACCEPT;
    if (!ready || !result ||
        !petta_push_unify(
            machine, result, expected, barrier)) {
        return false;
    }
    /* Postcondition: the call computes its result before acceptance. */
    if (result_requires_check &&
        !petta_goal_push(
            machine,
            (PettaGoal){
                .kind = type_goal,
                .barrier = barrier,
                .first = result,
                .second = result_type,
            })) {
        return false;
    }
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = result_is_atom_data
                    ? PETTA_GOAL_CALL_READY_DATA
                    : PETTA_GOAL_CALL_READY,
                .barrier = barrier,
                .first = ready,
                .second = result,
            })) {
        return false;
    }
    /*
     * typecheck-v2 additionally installs the result obligation before the
     * body runs.  This preserves binding-time enforcement and gives catch a
     * delimited type error, while the postcondition above still owns
     * relational/user-type fallback after a value is available.
     */
    if (petta_machine_type_obligations_enabled(machine) &&
        result_requires_check &&
        !petta_goal_push(
            machine,
            (PettaGoal){
                .kind = type_goal,
                .barrier = barrier,
                .first = result,
                .second = result_type,
            })) {
        return false;
    }
    for (CettaExprIndex index = length; index > 1u; index--) {
        CettaExprIndex argument = index - 1u;
        Atom *binder = NULL;
        Atom *formal = NULL;
        (void)petta_machine_split_dependent_domain(
            type->expr.elems[argument], &binder, &formal);
        if (petta_machine_type_is_atom_data(formal)) {
            if (binder &&
                !petta_push_unify(
                    machine, binder, ready_elements[argument], barrier)) {
                return false;
            }
            continue;
        }
        /* Goals are a LIFO stack.  Install the telescope binding first so it
         * runs only after evaluation and successful type acceptance, but
         * before the next domain or result contract is inspected. */
        if (binder &&
            !petta_push_unify(
                machine, binder, ready_elements[argument], barrier)) {
            return false;
        }
        /*
         * In typecheck-v2, install the argument constraint before its
         * source expression runs.  This is the native analogue of the
         * reference checker's attributed type variable: evaluation may
         * then distinguish an Error admitted by the formal type from an
         * Error that must propagate.  Keep the ordinary post-evaluation
         * TYPE_ACCEPT below as the relational fallback for open/user
         * types, and keep the historical scheduling unchanged in every
         * other profile.
         */
        if (!petta_machine_type_is_unconstrained(formal)) {
            if (!petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind = type_goal,
                        .barrier = barrier,
                        .first = ready_elements[argument],
                        .second = formal,
                    })) {
                return false;
            }
        }
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[argument],
                ready_elements[argument], barrier,
                petta_plan_child(plan, argument))) {
            return false;
        }
        if (petta_machine_type_obligations_enabled(machine) &&
            !petta_machine_type_is_unconstrained(formal) &&
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = type_goal,
                    .barrier = barrier,
                    .first = ready_elements[argument],
                    .second = formal,
                })) {
            return false;
        }
    }
    return true;
}

static PeTTaNamedArity petta_machine_named_arity(
    PettaMachineImpl *machine, Atom *head_atom,
    CettaExprLen nargs) {
    if (!machine)
        return (PeTTaNamedArity){0};
    PeTTaNamedArity result = petta_semantics_named_arity(
        machine->space, &machine->heap, head_atom, nargs);
    if (head_atom && head_atom->kind == ATOM_SYMBOL) {
        PeTTaNamedArity extension =
            petta_machine_extension_named_arity(
                machine, head_atom->sym_id, nargs);
        result.known = result.known || extension.known;
        result.exact = result.exact || extension.exact;
        result.larger = result.larger || extension.larger;
        result.smaller = result.smaller || extension.smaller;
    }
    return result;
}

static Atom *petta_machine_overapplication_error(
    PettaMachineImpl *machine, Atom *expression) {
    if (!machine || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    CettaExprLen nargs = expression->expr.len - 1u;
    if (nargs > (CettaExprLen)INT64_MAX ||
        !cetta_expr_len_mul_fits_size(
            nargs, sizeof(CettaExprLen))) {
        return NULL;
    }
    CettaExprLen *known = nargs
        ? arena_alloc(
              &machine->heap,
              sizeof(*known) * (size_t)nargs)
        : NULL;
    if (nargs > 0u && !known)
        return NULL;

    size_t known_count = 0u;
    for (CettaExprLen arity = 0u; arity < nargs; arity++) {
        PeTTaNamedArity candidate = petta_machine_named_arity(
            machine, expression->expr.elems[0], arity);
        if (candidate.exact)
            known[known_count++] = arity;
    }
    return petta_semantics_function_overapplication_error(
        &machine->heap, expression->expr.elems[0],
        known, known_count, nargs);
}

/*
 * PeTTa's translatePredicate boundary exposes a predicate-shaped view of a
 * native function relation.  The ordinary convention places the result in
 * the final argument; Prolog's `is/2` is the one reversed convention used by
 * the core language.  Unknown predicate contracts remain inert so an
 * extension adapter can own them without changing native search semantics.
 */
static bool petta_machine_space_predicate_view(
    PettaMachineImpl *machine, Atom *predicate,
    Space **space_out, Atom **pattern_out) {
    if (space_out)
        *space_out = NULL;
    if (pattern_out)
        *pattern_out = NULL;
    if (!machine || !predicate || !space_out || !pattern_out)
        return false;

    /*
     * `lib_spaces` protects a dynamically constructed predicate with
     * `catch(Goal, _, fail)`.  A native space lookup cannot raise Prolog's
     * undefined-predicate exception: absence is ordinary search exhaustion.
     * Recognize exactly that fail-guard normal form and no broader catch
     * contract.
     */
    Atom *candidate = predicate;
    if (candidate->kind == ATOM_EXPR &&
        candidate->expr.len == 4u &&
        candidate->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_symbol_name_is(
            candidate->expr.elems[0]->sym_id, "catch") &&
        candidate->expr.elems[3]->kind == ATOM_SYMBOL &&
        petta_symbol_name_is(
            candidate->expr.elems[3]->sym_id, "fail")) {
        candidate = candidate->expr.elems[1];
    }

    /*
     * PeTTa's `Predicate` constructor maps a list
     *   (&space relation arg ...)
     * to the Prolog callable
     *   '&space'(relation, arg, ...).
     * For a native Space this is precisely one relational match against the
     * tail expression.  Keeping it as a machine choice preserves declaration
     * order, duplicate occurrences, bindings, and logical-update semantics.
     */
    if (!candidate || candidate->kind != ATOM_EXPR ||
        candidate->expr.len != 2u ||
        candidate->expr.elems[0]->kind != ATOM_SYMBOL ||
        !petta_symbol_name_is(
            candidate->expr.elems[0]->sym_id, "Predicate")) {
        return false;
    }
    Atom *callable = candidate->expr.elems[1];
    if (!callable || callable->kind != ATOM_EXPR ||
        callable->expr.len < 2u)
        return false;
    Space *space = machine->host.resolve_space
        ? machine->host.resolve_space(
              machine->host.context, machine->space,
              &machine->heap, callable->expr.elems[0])
        : NULL;
    if (!space)
        return false;
    Atom *pattern = atom_expr(
        &machine->heap, callable->expr.elems + 1u,
        callable->expr.len - 1u);
    if (!pattern)
        return false;
    *space_out = space;
    *pattern_out = pattern;
    return true;
}

static bool petta_machine_predicate_wrapper_body(
    Atom *wrapper, Atom **body) {
    if (body)
        *body = NULL;
    if (!wrapper || !body ||
        wrapper->kind != ATOM_EXPR ||
        wrapper->expr.len != 2u ||
        wrapper->expr.elems[0]->kind != ATOM_SYMBOL ||
        !petta_symbol_name_is(
            wrapper->expr.elems[0]->sym_id, "Predicate")) {
        return false;
    }
    *body = wrapper->expr.elems[1];
    return *body != NULL;
}

static bool petta_machine_push_native_predicate(
    PettaMachineImpl *machine, Atom *predicate, Atom *expected,
    uint32_t barrier, bool *recognized) {
    if (recognized)
        *recognized = false;
    if (!machine || !predicate || !expected || !recognized ||
        predicate->kind != ATOM_EXPR ||
        predicate->expr.len < 2u ||
        predicate->expr.elems[0]->kind != ATOM_SYMBOL) {
        return true;
    }

    SymbolId head = predicate->expr.elems[0]->sym_id;
    Atom *success = petta_semantics_success_value(&machine->heap);
    if (!success)
        return false;

    if (petta_symbol_name_is(head, "is") &&
        predicate->expr.len == 3u) {
        *recognized = true;
        return petta_push_unify(
                   machine, success, expected, barrier) &&
               petta_push_solve(
                   machine, predicate->expr.elems[2],
                   predicate->expr.elems[1], barrier);
    }

    /*
     * The result-last view exposes a NATIVE function relation through the
     * predicate boundary.  Foreign named-arity knowledge must not leak in
     * here: a head that names a live Prolog predicate (assertz/2 is the
     * motivating case) is a direct goal for the foreign path below, and
     * viewing it result-last would strip its final argument and evaluate a
     * reified body.
     */
    CettaExprLen function_arity = predicate->expr.len - 2u;
    PeTTaNamedArity arity = petta_semantics_named_arity(
        machine->space, &machine->heap,
        predicate->expr.elems[0], function_arity);
    if (!arity.known || !arity.exact)
        return true;

    Atom *application = atom_expr(
        &machine->heap, predicate->expr.elems,
        predicate->expr.len - 1u);
    if (!application)
        return false;
    *recognized = true;
    return petta_push_unify(
               machine, success, expected, barrier) &&
           petta_push_solve(
               machine, application,
               predicate->expr.elems[predicate->expr.len - 1u],
               barrier);
}

/*
 * The PeTTa source compiler extends a definition when its body is visibly an
 * under-applied callable.  Re-derive that rule over source equations: adding
 * `extra` arguments must turn the body from a genuine partial application
 * into an exact call.  A data-valued body never passes this test.
 */
static bool petta_machine_rhs_extends_by(
    PettaMachineImpl *machine, Atom *rhs,
    CettaExprLen extra) {
    if (!machine || !rhs || extra == 0u)
        return false;

    Atom *base = NULL;
    Atom *bound = NULL;
    CettaExprLen bound_count = 0u;
    if (petta_semantics_partial_view(rhs, &base, &bound)) {
        bound_count = bound->expr.len;
    } else if (rhs->kind == ATOM_EXPR &&
               rhs->expr.len > 0u &&
               rhs->expr.elems[0]->kind == ATOM_SYMBOL) {
        base = rhs->expr.elems[0];
        bound_count = rhs->expr.len - 1u;
    } else {
        return false;
    }
    if (!base || base->kind != ATOM_SYMBOL ||
        bound_count > UINT64_MAX - extra) {
        return false;
    }

    PeTTaNamedArity before = petta_machine_named_arity(
        machine, base, bound_count);
    PeTTaNamedArity after = petta_machine_named_arity(
        machine, base, bound_count + extra);
    return before.known && !before.exact && before.larger &&
           after.exact;
}

static bool petta_machine_equation_extends_query(
    PettaMachineImpl *machine, Atom *equation, Atom *query) {
    if (!machine || !equation || !query ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0], g_builtin_syms.equals) ||
        query->kind != ATOM_EXPR || query->expr.len == 0u) {
        return false;
    }
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len == 0u ||
        lhs->expr.len >= query->expr.len ||
        !atom_eq(lhs->expr.elems[0], query->expr.elems[0])) {
        return false;
    }
    return petta_machine_rhs_extends_by(
        machine, rhs, query->expr.len - lhs->expr.len);
}

typedef enum {
    PETTA_EXTENSION_NONE = 0,
    PETTA_EXTENSION_PRESENT,
    PETTA_EXTENSION_INVALIDATED,
} PettaExtensionState;

static PettaExtensionState petta_machine_extension_state(
    PettaMachineImpl *machine, Atom *query) {
    if (!machine || !query || query->kind != ATOM_EXPR ||
        query->expr.len == 0u ||
        query->expr.elems[0]->kind != ATOM_SYMBOL) {
        return PETTA_EXTENSION_NONE;
    }
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(
            machine->space, query->expr.elems[0]->sym_id,
            &cursor)) {
        return PETTA_EXTENSION_NONE;
    }
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            return PETTA_EXTENSION_NONE;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED)
            return PETTA_EXTENSION_INVALIDATED;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(id, &occurrence))
            return PETTA_EXTENSION_INVALIDATED;
        if (petta_machine_equation_extends_query(
                machine, occurrence.equation, query)) {
            return PETTA_EXTENSION_PRESENT;
        }
    }
}

typedef enum {
    PETTA_PARTIAL_NO = 0,
    PETTA_PARTIAL_YES,
    PETTA_PARTIAL_OVERAPPLIED,
    PETTA_PARTIAL_INVALIDATED,
} PettaPartialDecision;

static PettaPartialDecision petta_machine_named_partial_decision(
    PettaMachineImpl *machine, Atom *expression) {
    if (!machine || !expression ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return PETTA_PARTIAL_NO;
    }

    CettaExprLen nargs = expression->expr.len - 1u;
    PeTTaNamedArity arity = petta_machine_named_arity(
        machine, expression->expr.elems[0], nargs);
    if (!arity.known || arity.exact)
        return PETTA_PARTIAL_NO;
    if (arity.larger)
        return PETTA_PARTIAL_YES;

    PettaExtensionState extension =
        petta_machine_extension_state(machine, expression);
    if (extension == PETTA_EXTENSION_INVALIDATED)
        return PETTA_PARTIAL_INVALIDATED;
    if (extension == PETTA_EXTENSION_PRESENT)
        return PETTA_PARTIAL_NO;
    return arity.smaller
        ? PETTA_PARTIAL_OVERAPPLIED : PETTA_PARTIAL_YES;
}

/*
 * Expand application of the machine's canonical partial value without
 * crossing the host-evaluator boundary.  This keeps user relations,
 * backtracking, cut, and failure under the same explicit search machine
 * after currying.
 */
static bool petta_machine_expand_partial_application(
    PettaMachineImpl *machine, Atom *application,
    Atom **expanded, bool *recognized) {
    if (expanded)
        *expanded = NULL;
    if (recognized)
        *recognized = false;
    if (!machine || !application || !expanded || !recognized ||
        application->kind != ATOM_EXPR ||
        application->expr.len == 0u) {
        return false;
    }

    Atom *base = NULL;
    Atom *bound = NULL;
    if (!petta_semantics_partial_view(
            application->expr.elems[0], &base, &bound)) {
        return true;
    }
    *recognized = true;
    CettaExprLen extra = application->expr.len - 1u;
    if (!base || !bound || bound->kind != ATOM_EXPR ||
        bound->expr.len > UINT64_MAX - extra - 1u) {
        return false;
    }
    CettaExprLen combined = bound->expr.len + extra + 1u;
    if (!cetta_expr_len_mul_fits_size(
            combined, sizeof(Atom *))) {
        return false;
    }
    Atom **elements = arena_alloc(
        &machine->heap,
        sizeof(*elements) * (size_t)combined);
    if (!elements)
        return false;
    elements[0] = base;
    if (bound->expr.len > 0u) {
        memcpy(
            elements + 1u, bound->expr.elems,
            sizeof(*elements) * (size_t)bound->expr.len);
    }
    if (extra > 0u) {
        memcpy(
            elements + 1u + bound->expr.len,
            application->expr.elems + 1u,
            sizeof(*elements) * (size_t)extra);
    }
    *expanded = atom_expr(
        &machine->heap, elements, combined);
    return *expanded != NULL;
}

static Atom *petta_machine_apply_clause_remainder(
    PettaMachineImpl *machine, Atom *query, Atom *lhs,
    Atom *result) {
    if (!machine || !query || !lhs || !result ||
        query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR ||
        lhs->expr.len >= query->expr.len) {
        return NULL;
    }
    CettaExprLen remainder =
        query->expr.len - lhs->expr.len;
    CettaExprLen length = remainder + 1u;
    if (!cetta_expr_len_mul_fits_size(
            length, sizeof(Atom *))) {
        return NULL;
    }
    Atom **elements = arena_alloc(
        &machine->heap,
        sizeof(*elements) * (size_t)length);
    elements[0] = result;
    memcpy(
        elements + 1u, query->expr.elems + lhs->expr.len,
        sizeof(*elements) * (size_t)remainder);
    return atom_expr(&machine->heap, elements, length);
}

/*
 * Applying surplus call arguments to a clause result introduces a new
 * expression occurrence which did not exist in the source tree.  Its head
 * retains the authored RHS plan; the surplus arguments are values already
 * evaluated by CALL_READY.  Keeping this derived plan beside the synthesized
 * application is what distinguishes a returned callable from expression
 * data with the same parenthesized shape.
 */
static bool petta_machine_clause_remainder_plan(
    PettaMachineImpl *machine, Atom *application,
    const PettaPlanNode *result_plan,
    const PettaPlanNode **application_plan) {
    if (application_plan)
        *application_plan = NULL;
    if (!machine || !application || !application_plan ||
        application->kind != ATOM_EXPR ||
        application->expr.len == 0u) {
        return false;
    }
    if (!result_plan)
        return true;
    if (!cetta_expr_len_mul_fits_size(
            application->expr.len, sizeof(PettaPlanNode))) {
        return false;
    }

    PettaPlanNode *root =
        arena_alloc(&machine->plan_arena, sizeof(*root));
    PettaPlanNode *children = arena_alloc(
        &machine->plan_arena,
        sizeof(*children) * (size_t)application->expr.len);
    if (!root || !children)
        return false;

    memset(
        children, 0,
        sizeof(*children) * (size_t)application->expr.len);
    children[0] = *result_plan;
    for (CettaExprIndex index = 1u;
         index < application->expr.len; index++) {
        children[index].role = PETTA_PLAN_VALUE;
    }
    *root = (PettaPlanNode){
        .role = PETTA_PLAN_DYNAMIC_CALL,
        .child_count = application->expr.len,
        .children = children,
    };
    *application_plan = root;
    return true;
}

static void petta_machine_stats_accumulate(
    PettaMachineStats *target, const PettaMachineStats *source) {
    if (!target || !source)
        return;
    target->transitions += source->transitions;
    target->solve_goal_transitions +=
        source->solve_goal_transitions;
    target->call_goal_transitions +=
        source->call_goal_transitions;
    target->unify_goal_transitions +=
        source->unify_goal_transitions;
    target->collection_goal_transitions +=
        source->collection_goal_transitions;
    target->control_goal_transitions +=
        source->control_goal_transitions;
    target->host_goal_transitions +=
        source->host_goal_transitions;
    target->other_goal_transitions +=
        source->other_goal_transitions;
    target->clause_snapshot_calls +=
        source->clause_snapshot_calls;
    target->clause_snapshot_cache_hits +=
        source->clause_snapshot_cache_hits;
    target->clause_snapshot_live_occurrences +=
        source->clause_snapshot_live_occurrences;
    target->clause_snapshot_records_examined +=
        source->clause_snapshot_records_examined;
    target->clause_snapshot_pointer_identity_hits +=
        source->clause_snapshot_pointer_identity_hits;
    target->clause_snapshot_equality_checks +=
        source->clause_snapshot_equality_checks;
    target->clause_snapshot_alpha_checks +=
        source->clause_snapshot_alpha_checks;
    target->clause_snapshot_candidates +=
        source->clause_snapshot_candidates;
    target->clause_snapshot_candidates_copied +=
        source->clause_snapshot_candidates_copied;
    target->clause_candidates += source->clause_candidates;
    target->clause_candidates_shape_pruned +=
        source->clause_candidates_shape_pruned;
    target->match_decision_compilations +=
        source->match_decision_compilations;
    target->match_decision_cache_hits +=
        source->match_decision_cache_hits;
    target->match_decision_runs +=
        source->match_decision_runs;
    target->match_decision_clause_inputs +=
        source->match_decision_clause_inputs;
    target->match_decision_clause_survivors +=
        source->match_decision_clause_survivors;
    target->match_decision_key_index_build_probes +=
        source->match_decision_key_index_build_probes;
    target->match_decision_key_index_select_probes +=
        source->match_decision_key_index_select_probes;
    target->match_decision_generic_key_policy_scans +=
        source->match_decision_generic_key_policy_scans;
    target->match_decision_linear_fallbacks +=
        source->match_decision_linear_fallbacks;
    target->match_decision_unavailable_path_fallbacks +=
        source->match_decision_unavailable_path_fallbacks;
    target->match_decision_invalidations +=
        source->match_decision_invalidations;
    target->clause_match_attempts +=
        source->clause_match_attempts;
    target->clause_branches_scheduled +=
        source->clause_branches_scheduled;
    target->clause_match_allocated_bytes +=
        source->clause_match_allocated_bytes;
    target->match_candidates += source->match_candidates;
    target->match_candidate_epoch_views +=
        source->match_candidate_epoch_views;
    target->unification_calls += source->unification_calls;
    target->unification_failures +=
        source->unification_failures;
    target->unification_binding_writes +=
        source->unification_binding_writes;
    target->unification_allocated_bytes +=
        source->unification_allocated_bytes;
    target->clause_binding_merge_calls +=
        source->clause_binding_merge_calls;
    target->clause_binding_merge_source_items +=
        source->clause_binding_merge_source_items;
    target->clause_binding_merge_logical_writes +=
        source->clause_binding_merge_logical_writes;
    target->clause_binding_merge_failures +=
        source->clause_binding_merge_failures;
    target->outcome_binding_merge_calls +=
        source->outcome_binding_merge_calls;
    target->outcome_binding_merge_source_items +=
        source->outcome_binding_merge_source_items;
    target->outcome_binding_merge_logical_writes +=
        source->outcome_binding_merge_logical_writes;
    target->outcome_binding_merge_failures +=
        source->outcome_binding_merge_failures;
    target->outcome_prefix_factor_attempts +=
        source->outcome_prefix_factor_attempts;
    target->outcome_prefix_factor_successes +=
        source->outcome_prefix_factor_successes;
    target->outcome_prefix_logical_items_elided +=
        source->outcome_prefix_logical_items_elided;
    target->outcome_prefix_residual_items +=
        source->outcome_prefix_residual_items;
    target->binding_apply_calls += source->binding_apply_calls;
    target->binding_apply_rewrites +=
        source->binding_apply_rewrites;
    target->binding_apply_allocated_bytes +=
        source->binding_apply_allocated_bytes;
    target->binding_apply_environment_entries +=
        source->binding_apply_environment_entries;
    target->binding_apply_epoch_calls +=
        source->binding_apply_epoch_calls;
    target->binding_apply_epoch_suffix_entries +=
        source->binding_apply_epoch_suffix_entries;
    target->solve_expression_apply_calls +=
        source->solve_expression_apply_calls;
    target->solve_expression_apply_allocated_bytes +=
        source->solve_expression_apply_allocated_bytes;
    target->solve_expression_open_template_admitted_calls +=
        source->solve_expression_open_template_admitted_calls;
    target->solve_expression_open_template_admitted_allocated_bytes +=
        source->solve_expression_open_template_admitted_allocated_bytes;
    target->solve_expected_apply_calls +=
        source->solve_expected_apply_calls;
    target->solve_expected_apply_allocated_bytes +=
        source->solve_expected_apply_allocated_bytes;
    target->solve_expected_open_template_admitted_calls +=
        source->solve_expected_open_template_admitted_calls;
    target->solve_expected_open_template_admitted_allocated_bytes +=
        source->solve_expected_open_template_admitted_allocated_bytes;
    target->activation_materialization_calls +=
        source->activation_materialization_calls;
    target->activation_materialization_allocated_bytes +=
        source->activation_materialization_allocated_bytes;
    target->activation_open_template_admitted_calls +=
        source->activation_open_template_admitted_calls;
    target->activation_open_template_admitted_allocated_bytes +=
        source->activation_open_template_admitted_allocated_bytes;
    target->constructor_slot_frame_entries +=
        source->constructor_slot_frame_entries;
    target->constructor_slot_frame_direct_unifications +=
        source->constructor_slot_frame_direct_unifications;
    target->pure_grounded_slot_frame_entries +=
        source->pure_grounded_slot_frame_entries;
    target->pure_grounded_slot_frame_direct_dispatches +=
        source->pure_grounded_slot_frame_direct_dispatches;
    target->relation_slot_frame_entries +=
        source->relation_slot_frame_entries;
    target->relation_slot_operands_reused +=
        source->relation_slot_operands_reused;
    target->atom_copy_calls += source->atom_copy_calls;
    target->atom_copy_allocated_bytes +=
        source->atom_copy_allocated_bytes;
    target->atom_copy_query_calls +=
        source->atom_copy_query_calls;
    target->atom_copy_query_allocated_bytes +=
        source->atom_copy_query_allocated_bytes;
    target->atom_copy_answer_calls +=
        source->atom_copy_answer_calls;
    target->atom_copy_answer_allocated_bytes +=
        source->atom_copy_answer_allocated_bytes;
    target->atom_copy_visible_variable_calls +=
        source->atom_copy_visible_variable_calls;
    target->atom_copy_visible_variable_allocated_bytes +=
        source->atom_copy_visible_variable_allocated_bytes;
    target->atom_copy_visible_value_calls +=
        source->atom_copy_visible_value_calls;
    target->atom_copy_visible_value_allocated_bytes +=
        source->atom_copy_visible_value_allocated_bytes;
    target->atom_copy_error_calls +=
        source->atom_copy_error_calls;
    target->atom_copy_error_allocated_bytes +=
        source->atom_copy_error_allocated_bytes;
    target->atom_freshen_calls += source->atom_freshen_calls;
    target->atom_freshen_allocated_bytes +=
        source->atom_freshen_allocated_bytes;
    target->specializer_prepare_calls +=
        source->specializer_prepare_calls;
    target->specializer_prepare_filtered +=
        source->specializer_prepare_filtered;
    target->specializer_prepare_relation_filtered +=
        source->specializer_prepare_relation_filtered;
    target->specializer_prepare_relevance_bounded +=
        source->specializer_prepare_relevance_bounded;
    target->specializer_prepare_rewritten +=
        source->specializer_prepare_rewritten;
    target->specializer_prepare_unchanged +=
        source->specializer_prepare_unchanged;
    target->specializer_prepare_capacity_declines +=
        source->specializer_prepare_capacity_declines;
    target->specializer_prepare_elapsed_ns +=
        source->specializer_prepare_elapsed_ns;
    target->choice_resumes += source->choice_resumes;
    target->choice_continuation_snapshots +=
        source->choice_continuation_snapshots;
    target->choice_continuation_items_copied +=
        source->choice_continuation_items_copied;
    target->choice_continuation_items_trailed +=
        source->choice_continuation_items_trailed;
    target->deterministic_clause_choices_elided +=
        source->deterministic_clause_choices_elided;
    target->singleton_outcome_choices_elided +=
        source->singleton_outcome_choices_elided;
    target->rollbacks += source->rollbacks;
    target->answers += source->answers;
    target->deterministic_heap_collections +=
        source->deterministic_heap_collections;
    target->deterministic_minor_heap_collections +=
        source->deterministic_minor_heap_collections;
    target->deterministic_major_heap_collections +=
        source->deterministic_major_heap_collections;
    target->deterministic_heap_collection_elapsed_ns +=
        source->deterministic_heap_collection_elapsed_ns;
    target->deterministic_minor_heap_collection_elapsed_ns +=
        source->deterministic_minor_heap_collection_elapsed_ns;
    target->deterministic_major_heap_collection_elapsed_ns +=
        source->deterministic_major_heap_collection_elapsed_ns;
    target->deterministic_goal_roots_scanned +=
        source->deterministic_goal_roots_scanned;
    target->deterministic_heap_bytes_promoted +=
        source->deterministic_heap_bytes_promoted;
    target->deterministic_minor_heap_bytes_promoted +=
        source->deterministic_minor_heap_bytes_promoted;
    target->deterministic_major_heap_bytes_promoted +=
        source->deterministic_major_heap_bytes_promoted;
    target->deterministic_root_atom_bytes_promoted +=
        source->deterministic_root_atom_bytes_promoted;
    target->deterministic_query_atom_bytes_promoted +=
        source->deterministic_query_atom_bytes_promoted;
    target->deterministic_visible_atom_bytes_promoted +=
        source->deterministic_visible_atom_bytes_promoted;
    target->deterministic_type_atom_bytes_promoted +=
        source->deterministic_type_atom_bytes_promoted;
    target->deterministic_goal_atom_bytes_promoted +=
        source->deterministic_goal_atom_bytes_promoted;
    target->deterministic_goal_first_bytes_promoted +=
        source->deterministic_goal_first_bytes_promoted;
    target->deterministic_goal_second_bytes_promoted +=
        source->deterministic_goal_second_bytes_promoted;
    target->deterministic_goal_third_bytes_promoted +=
        source->deterministic_goal_third_bytes_promoted;
    target->deterministic_goal_fourth_bytes_promoted +=
        source->deterministic_goal_fourth_bytes_promoted;
    target->deterministic_binding_atom_bytes_promoted +=
        source->deterministic_binding_atom_bytes_promoted;
    target->deterministic_heap_bytes_reclaimed +=
        source->deterministic_heap_bytes_reclaimed;
    target->deterministic_binding_entries_discarded +=
        source->deterministic_binding_entries_discarded;
    target->choice_binding_collections +=
        source->choice_binding_collections;
    target->choice_binding_items_discarded +=
        source->choice_binding_items_discarded;
    target->choice_trail_entries_discarded +=
        source->choice_trail_entries_discarded;
    target->choice_nursery_evacuations +=
        source->choice_nursery_evacuations;
    target->choice_nursery_evacuation_elapsed_ns +=
        source->choice_nursery_evacuation_elapsed_ns;
    target->choice_nursery_goal_roots_scanned +=
        source->choice_nursery_goal_roots_scanned;
    target->choice_nursery_bytes_evacuated +=
        source->choice_nursery_bytes_evacuated;
    target->choice_nursery_bytes_reclaimed +=
        source->choice_nursery_bytes_reclaimed;
    target->choice_heap_resets +=
        source->choice_heap_resets;
    target->choice_heap_bytes_reclaimed +=
        source->choice_heap_bytes_reclaimed;
    target->owned_continuation_capture_attempts +=
        source->owned_continuation_capture_attempts;
    target->owned_continuation_captures +=
        source->owned_continuation_captures;
    target->owned_continuation_capture_deferred +=
        source->owned_continuation_capture_deferred;
    target->owned_continuation_capture_unsupported +=
        source->owned_continuation_capture_unsupported;
    target->owned_continuation_capture_invalidated +=
        source->owned_continuation_capture_invalidated;
    target->owned_continuation_restores +=
        source->owned_continuation_restores;
    target->owned_continuation_restore_invalidated +=
        source->owned_continuation_restore_invalidated;
    target->owned_continuation_atom_bytes_captured +=
        source->owned_continuation_atom_bytes_captured;
    target->owned_continuation_vector_bytes_captured +=
        source->owned_continuation_vector_bytes_captured;
    target->owned_continuation_expansion_attempts +=
        source->owned_continuation_expansion_attempts;
    target->owned_continuation_expansions +=
        source->owned_continuation_expansions;
    target->owned_continuation_expansion_successors +=
        source->owned_continuation_expansion_successors;
    target->owned_continuation_expansion_deferred +=
        source->owned_continuation_expansion_deferred;
    target->owned_continuation_expansion_unsupported +=
        source->owned_continuation_expansion_unsupported;
    target->owned_continuation_expansion_invalidated +=
        source->owned_continuation_expansion_invalidated;
    target->owned_continuation_expansion_capacity +=
        source->owned_continuation_expansion_capacity;
    target->table_lookups += source->table_lookups;
    target->table_hits += source->table_hits;
    target->table_generator_rounds +=
        source->table_generator_rounds;
    target->table_scc_completions +=
        source->table_scc_completions;
    target->table_answer_replays +=
        source->table_answer_replays;
    target->count_aggregate_answers +=
        source->count_aggregate_answers;
    target->count_aggregate_boundary_copies_avoided +=
        source->count_aggregate_boundary_copies_avoided;
    target->count_aggregate_match_folds +=
        source->count_aggregate_match_folds;
    target->count_aggregate_match_answers +=
        source->count_aggregate_match_answers;
    target->count_aggregate_let_fusions +=
        source->count_aggregate_let_fusions;
    target->match_existence_observer_folds +=
        source->match_existence_observer_folds;
    target->child_machine_init_attempts +=
        source->child_machine_init_attempts;
    target->child_machine_init_successes +=
        source->child_machine_init_successes;
    target->child_machine_projected_entries +=
        source->child_machine_projected_entries;
    target->child_machine_projection_elapsed_ns +=
        source->child_machine_projection_elapsed_ns;
    target->child_machine_init_elapsed_ns +=
        source->child_machine_init_elapsed_ns;
    target->child_machine_destroy_calls +=
        source->child_machine_destroy_calls;
    target->child_machine_destroy_elapsed_ns +=
        source->child_machine_destroy_elapsed_ns;
    target->host_environment_entries_observed +=
        source->host_environment_entries_observed;
    target->host_environment_entries_forwarded +=
        source->host_environment_entries_forwarded;
    if (source->maximum_goal_depth > target->maximum_goal_depth)
        target->maximum_goal_depth = source->maximum_goal_depth;
    if (source->maximum_choice_depth > target->maximum_choice_depth)
        target->maximum_choice_depth = source->maximum_choice_depth;
    if (source->maximum_choice_continuation_trail >
        target->maximum_choice_continuation_trail) {
        target->maximum_choice_continuation_trail =
            source->maximum_choice_continuation_trail;
    }
    if (source->maximum_heap_live_bytes >
        target->maximum_heap_live_bytes) {
        target->maximum_heap_live_bytes =
            source->maximum_heap_live_bytes;
    }
    if (source->maximum_nursery_live_bytes >
        target->maximum_nursery_live_bytes) {
        target->maximum_nursery_live_bytes =
            source->maximum_nursery_live_bytes;
    }
    if (source->maximum_tenured_live_bytes >
        target->maximum_tenured_live_bytes) {
        target->maximum_tenured_live_bytes =
            source->maximum_tenured_live_bytes;
    }
    if (source->maximum_binding_entries >
        target->maximum_binding_entries) {
        target->maximum_binding_entries =
            source->maximum_binding_entries;
    }
    if (source->maximum_binding_apply_environment_entries >
        target->maximum_binding_apply_environment_entries) {
        target->maximum_binding_apply_environment_entries =
            source->maximum_binding_apply_environment_entries;
    }
    if (source->maximum_binding_apply_epoch_suffix_entries >
        target->maximum_binding_apply_epoch_suffix_entries) {
        target->maximum_binding_apply_epoch_suffix_entries =
            source->maximum_binding_apply_epoch_suffix_entries;
    }
    if (source->maximum_host_environment_entries_forwarded >
        target->maximum_host_environment_entries_forwarded) {
        target->maximum_host_environment_entries_forwarded =
            source->maximum_host_environment_entries_forwarded;
    }
}

static size_t petta_table_stack_find(
    const PettaTableShared *shared, size_t entry) {
    if (!shared)
        return PETTA_TABLE_ENTRY_NONE;
    for (size_t index = 0u;
         index < shared->tarjan_len; index++) {
        if (shared->tarjan_stack[index] == entry)
            return index;
    }
    return PETTA_TABLE_ENTRY_NONE;
}

static bool petta_table_begin_entry(
    PettaTableShared *shared, size_t entry_index) {
    if (!shared || entry_index >= shared->entry_len ||
        shared->tarjan_next == SIZE_MAX) {
        return false;
    }
    PettaTableEntry *entry = &shared->entries[entry_index];
    if (entry->state != PETTA_TABLE_ENTRY_NEW)
        return true;
    size_t index = shared->tarjan_next++;
    entry->state = PETTA_TABLE_ENTRY_EVALUATING;
    entry->tarjan_index = index;
    entry->tarjan_lowlink = index;
    entry->tarjan_on_stack = true;
    entry->self_edge = false;
    if (!petta_table_tarjan_push(shared, entry_index)) {
        entry->state = PETTA_TABLE_ENTRY_NEW;
        entry->tarjan_index = PETTA_TABLE_ENTRY_NONE;
        entry->tarjan_lowlink = PETTA_TABLE_ENTRY_NONE;
        entry->tarjan_on_stack = false;
        shared->tarjan_next--;
        return false;
    }
    return true;
}

static void petta_table_record_dependency(
    PettaTableShared *shared, size_t source_index,
    size_t target_index) {
    if (!shared || source_index >= shared->entry_len ||
        target_index >= shared->entry_len) {
        return;
    }
    PettaTableEntry *source = &shared->entries[source_index];
    PettaTableEntry *target = &shared->entries[target_index];
    if (source->state != PETTA_TABLE_ENTRY_EVALUATING ||
        !source->tarjan_on_stack ||
        target->state != PETTA_TABLE_ENTRY_EVALUATING ||
        !target->tarjan_on_stack) {
        return;
    }
    if (source_index == target_index)
        source->self_edge = true;
    if (target->tarjan_index < source->tarjan_lowlink)
        source->tarjan_lowlink = target->tarjan_index;
}

static void petta_table_record_child_completion(
    PettaTableShared *shared, size_t parent_index,
    size_t child_index) {
    if (!shared || parent_index >= shared->entry_len ||
        child_index >= shared->entry_len) {
        return;
    }
    PettaTableEntry *parent = &shared->entries[parent_index];
    PettaTableEntry *child = &shared->entries[child_index];
    if (parent->state == PETTA_TABLE_ENTRY_EVALUATING &&
        parent->tarjan_on_stack &&
        child->state == PETTA_TABLE_ENTRY_EVALUATING &&
        child->tarjan_on_stack &&
        child->tarjan_lowlink < parent->tarjan_lowlink) {
        parent->tarjan_lowlink = child->tarjan_lowlink;
    }
}

static void petta_table_complete_suffix(
    PettaTableShared *shared, size_t begin) {
    if (!shared || begin > shared->tarjan_len)
        return;
    for (size_t index = begin;
         index < shared->tarjan_len; index++) {
        size_t entry_index = shared->tarjan_stack[index];
        if (entry_index >= shared->entry_len)
            continue;
        PettaTableEntry *entry =
            &shared->entries[entry_index];
        entry->state = PETTA_TABLE_ENTRY_COMPLETE;
        entry->tarjan_on_stack = false;
    }
    shared->tarjan_len = begin;
}

static bool petta_table_choice_begin_generator(
    PettaMachineImpl *machine, PettaChoice *choice) {
    if (!machine || !choice ||
        choice->kind != PETTA_CHOICE_TABLE ||
        !machine->table_shared ||
        choice->as.table.iteration_entry >=
            machine->table_shared->entry_len ||
        choice->as.table.generator ||
        choice->as.table.round_arena) {
        return false;
    }

    Arena *round_arena = cetta_malloc(sizeof(*round_arena));
    arena_init(round_arena);
    arena_set_runtime_kind(
        round_arena, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    arena_set_hashcons(round_arena, NULL);
    CettaVarMap no_goal_instantiation;
    cetta_var_map_init(&no_goal_instantiation);
    cetta_var_map_init(
        &choice->as.table.generator_slots);
    Atom *key =
        machine->table_shared
            ->entries[choice->as.table.iteration_entry].generator_key;
    Atom *generator_query = variant_shape_materialize_atom(
        round_arena, key, &no_goal_instantiation,
        &choice->as.table.generator_slots);
    cetta_var_map_free(&no_goal_instantiation);
    if (!generator_query) {
        cetta_var_map_free(
            &choice->as.table.generator_slots);
        arena_free(round_arena);
        free(round_arena);
        return false;
    }
    PettaMachine *generator = cetta_malloc(sizeof(*generator));
    Bindings empty;
    bindings_init(&empty);
        if (!petta_machine_init_internal(
                generator, machine->space, round_arena,
                generator_query, NULL,
                &empty, &machine->host, false,
                machine->table_shared,
                false, false, true,
                choice->as.table.iteration_entry)) {
        cetta_var_map_free(
            &choice->as.table.generator_slots);
        arena_free(round_arena);
        free(round_arena);
        free(generator);
        return false;
    }
    choice->as.table.generator = generator;
    choice->as.table.round_arena = round_arena;
    choice->as.table.generator_query = generator_query;
    choice->as.table.round_len = 0u;
    return true;
}

static void petta_table_choice_finish_generator(
    PettaMachineImpl *machine, PettaChoice *choice) {
    if (!machine || !choice ||
        choice->kind != PETTA_CHOICE_TABLE)
        return;
    if (choice->as.table.generator) {
        PettaMachineStats child_stats;
        if (petta_machine_stats(
                choice->as.table.generator, &child_stats)) {
            petta_machine_stats_accumulate(
                &machine->stats, &child_stats);
        }
        petta_machine_destroy(choice->as.table.generator);
        free(choice->as.table.generator);
        choice->as.table.generator = NULL;
    }
}

static void petta_table_choice_clear_round(
    PettaChoice *choice) {
    if (!choice || choice->kind != PETTA_CHOICE_TABLE)
        return;
    free(choice->as.table.round_answers);
    choice->as.table.round_answers = NULL;
    choice->as.table.round_len = 0u;
    choice->as.table.round_cap = 0u;
    choice->as.table.generator_query = NULL;
    cetta_var_map_free(
        &choice->as.table.generator_slots);
    if (choice->as.table.round_arena) {
        arena_free(choice->as.table.round_arena);
        free(choice->as.table.round_arena);
        choice->as.table.round_arena = NULL;
    }
}

static Atom *petta_table_choice_canonical_answer(
    PettaMachineImpl *machine, PettaChoice *choice, Atom *answer,
    const Bindings *environment) {
    if (!machine || !choice || choice->kind != PETTA_CHOICE_TABLE ||
        !choice->as.table.round_arena ||
        !choice->as.table.generator_query ||
        !answer || !environment) {
        return NULL;
    }
    Atom *instantiated_query = petta_machine_apply_bindings(machine,
        environment, choice->as.table.round_arena,
        choice->as.table.generator_query);
    if (!instantiated_query)
        return NULL;
    Atom *record_items[2] = {
        instantiated_query,
        answer,
    };
    Atom *record = atom_expr(
        choice->as.table.round_arena,
        record_items, 2u);
    if (!record)
        return NULL;

    CettaVarMap answer_map;
    cetta_var_map_init(&answer_map);
    for (uint32_t index = 0u;
         index < choice->as.table.generator_slots.len;
         index++) {
        const CettaVarMapEntry *slot =
            &choice->as.table.generator_slots.items[index];
        Atom *fresh = slot->mapped_var;
        Atom *canonical_slot =
            fresh && fresh->kind == ATOM_VAR
                ? atom_var_like(
                      choice->as.table.round_arena,
                      fresh, slot->source_id)
                : NULL;
        if (!canonical_slot ||
            !cetta_var_map_add(
                &answer_map, fresh->var_id,
                canonical_slot)) {
            cetta_var_map_free(&answer_map);
            return NULL;
        }
    }
    Atom *canonical = variant_shape_canonicalize_atom(
        choice->as.table.round_arena, record,
        &answer_map, NULL, &kPettaTableVariantOptions);
    cetta_var_map_free(&answer_map);
    return canonical;
}

static bool petta_table_choice_drive_generator(
    PettaMachineImpl *machine, PettaChoice *choice,
    bool *changed, PettaMachineStep *failure) {
    if (changed)
        *changed = false;
    if (!machine || !choice || !changed || !failure ||
        choice->kind != PETTA_CHOICE_TABLE ||
        !machine->table_shared ||
        choice->as.table.iteration_entry >=
            machine->table_shared->entry_len) {
        if (failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (!choice->as.table.generator &&
        !petta_table_choice_begin_generator(machine, choice)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        machine->table_shared->failed = true;
        return false;
    }

    for (;;) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(
            choice->as.table.generator,
            &answer, &environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            PettaTableEntry *entry =
                &machine->table_shared->entries[
                    choice->as.table.iteration_entry];
            uint64_t retained = (uint64_t)entry->answer_len +
                (uint64_t)choice->as.table.round_len;
            if (choice->as.table.memoized &&
                retained >= choice->as.table.answer_limit) {
                if (!choice->as.table.truncation_observed &&
                    machine->host.memoized_relation_truncated &&
                    choice->as.table.query &&
                    choice->as.table.query->kind == ATOM_EXPR &&
                    choice->as.table.query->expr.len > 0u &&
                    choice->as.table.query->expr.elems[0]->kind ==
                        ATOM_SYMBOL) {
                    machine->host.memoized_relation_truncated(
                        machine->host.context,
                        choice->as.table.query->expr.elems[0]->sym_id,
                        choice->as.table.query->expr.len - 1u);
                    choice->as.table.truncation_observed = true;
                }
                bindings_free(&environment);
                continue;
            }
            Atom *canonical_answer =
                petta_table_choice_canonical_answer(
                    machine, choice, answer, &environment);
            if (!petta_machine_reserve(
                    (void **)&choice->as.table.round_answers,
                    &choice->as.table.round_cap,
                    choice->as.table.round_len + 1u,
                    sizeof(*choice->as.table.round_answers)) ||
                !canonical_answer) {
                bindings_free(&environment);
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                machine->table_shared->failed = true;
                return false;
            }
            choice->as.table.round_answers[
                choice->as.table.round_len++] =
                    canonical_answer;
            bindings_free(&environment);
            continue;
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            *failure = step;
            return false;
        }
        if (step != PETTA_MACHINE_STEP_EXHAUSTED) {
            *failure = step;
            machine->table_shared->failed = true;
            return false;
        }
        break;
    }

    petta_table_choice_finish_generator(machine, choice);
    PettaTableEntry *entry =
        &machine->table_shared
             ->entries[choice->as.table.iteration_entry];
    bool merged = petta_table_merge_round_answers(
        machine->table_shared, entry,
        choice->as.table.round_answers,
        choice->as.table.round_len, changed);
    petta_table_choice_clear_round(choice);
    machine->stats.table_generator_rounds++;
    if (!merged) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        machine->table_shared->failed = true;
        return false;
    }
    return true;
}

static Atom *petta_table_choice_aggregate_answer(
    PettaMachineImpl *machine, PettaChoice *choice,
    PettaTableEntry *entry) {
    if (!machine || !choice || !entry ||
        choice->kind != PETTA_CHOICE_TABLE ||
        choice->as.table.aggregate_mode ==
            PETTA_MEMO_AGGREGATE_NONE) {
        return NULL;
    }
    if (choice->as.table.aggregate_mode ==
            PETTA_MEMO_AGGREGATE_COUNT) {
        return entry->answer_len <= (size_t)INT64_MAX
            ? atom_int(
                  &machine->heap,
                  (int64_t)entry->answer_len)
            : NULL;
    }
    if (entry->answer_len == 0u) {
        return choice->as.table.aggregate_mode ==
                   PETTA_MEMO_AGGREGATE_SUM
            ? atom_int(&machine->heap, 0)
            : NULL;
    }
    if (entry->answer_len > SIZE_MAX / sizeof(Atom *))
        return NULL;
    Atom **values = cetta_malloc(
        sizeof(*values) * entry->answer_len);
    bool ok = true;
    for (size_t index = 0u;
         index < entry->answer_len; index++) {
        CettaVarMap local_slots;
        cetta_var_map_init(&local_slots);
        Atom *record = variant_shape_materialize_atom(
            &machine->heap, entry->answers[index],
            &choice->as.table.goal_instantiation,
            &local_slots);
        cetta_var_map_free(&local_slots);
        values[index] =
            record && record->kind == ATOM_EXPR &&
                    record->expr.len == 2u
                ? record->expr.elems[1] : NULL;
        if (!values[index]) {
            ok = false;
            break;
        }
    }
    Atom *result = NULL;
    if (ok && choice->as.table.aggregate_mode ==
                  PETTA_MEMO_AGGREGATE_SUM) {
        result = atom_int(&machine->heap, 0);
        Atom *plus = atom_symbol_id(
            &machine->heap, g_builtin_syms.op_plus);
        for (size_t index = 0u;
             result && plus && index < entry->answer_len; index++) {
            Atom *arguments[2] = {result, values[index]};
            result = grounded_dispatch(
                &machine->heap, plus, arguments, 2u);
        }
    } else if (ok) {
        Atom *list = atom_expr(
            &machine->heap, values,
            (CettaExprLen)entry->answer_len);
        SymbolId operation =
            choice->as.table.aggregate_mode ==
                    PETTA_MEMO_AGGREGATE_MIN
                ? g_builtin_syms.min_atom
                : g_builtin_syms.max_atom;
        Atom *head = atom_symbol_id(
            &machine->heap, operation);
        Atom *arguments[1] = {list};
        result = list && head
            ? grounded_dispatch(
                  &machine->heap, head, arguments, 1u)
            : NULL;
    }
    free(values);
    return result;
}

static bool petta_machine_resume_table_choice(
    PettaMachineImpl *machine, PettaChoice *choice,
    PettaMachineStep *failure) {
    if (!machine || !choice || !failure ||
        choice->kind != PETTA_CHOICE_TABLE ||
        !machine->table_shared ||
        machine->table_shared->failed ||
        !petta_table_shared_epoch_current(
            machine->table_shared)) {
        if (failure) {
            *failure = machine && machine->table_shared &&
                       !petta_table_shared_epoch_current(
                           machine->table_shared)
                ? PETTA_MACHINE_STEP_INVALIDATED
                : PETTA_MACHINE_STEP_CAPACITY;
        }
        return false;
    }

    for (;;) {
        PettaTableShared *shared = machine->table_shared;
        if (choice->as.table.phase ==
            PETTA_TABLE_CHOICE_REPLAY) {
            if (choice->as.table.requested_entry >=
                shared->entry_len) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                shared->failed = true;
                return false;
            }
            PettaTableEntry *requested =
                &shared->entries[
                    choice->as.table.requested_entry];
            if (choice->as.table.ground_query &&
                choice->as.table.aggregate_mode !=
                    PETTA_MEMO_AGGREGATE_NONE) {
                if (choice->as.table.aggregate_emitted)
                    return false;
                choice->as.table.aggregate_emitted = true;
                Atom *aggregate =
                    petta_table_choice_aggregate_answer(
                        machine, choice, requested);
                if (!aggregate)
                    return false;
                if (!petta_push_unify(
                        machine, aggregate,
                        choice->as.table.expected,
                        choice->barrier)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    shared->failed = true;
                    return false;
                }
                machine->stats.table_answer_replays++;
                return true;
            }
            while (choice->as.table.replay_next <
                   requested->answer_len) {
                Atom *stored =
                    requested->answers[
                        choice->as.table.replay_next++];
                CettaVarMap local_slots;
                cetta_var_map_init(&local_slots);
                Atom *record = variant_shape_materialize_atom(
                    &machine->heap, stored,
                    &choice->as.table.goal_instantiation,
                    &local_slots);
                cetta_var_map_free(&local_slots);
                if (!record || record->kind != ATOM_EXPR ||
                    record->expr.len != 2u ||
                    !petta_push_unify(
                        machine, record->expr.elems[1],
                        choice->as.table.expected,
                        choice->barrier)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    shared->failed = true;
                    return false;
                }
                /* Quantized ground keys intentionally identify nearby raw
                 * arguments.  Re-unifying the stored first call with a later
                 * raw call would defeat that policy.  Variant calls still
                 * replay their complete solved input/output relation. */
                if ((!choice->as.table.memoized ||
                     !choice->as.table.ground_query) &&
                    !petta_push_unify(
                        machine, record->expr.elems[0],
                        choice->as.table.query,
                        choice->barrier)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    shared->failed = true;
                    return false;
                }
                machine->stats.table_answer_replays++;
                return true;
            }
            return false;
        }

        bool changed = false;
        if (!petta_table_choice_drive_generator(
                machine, choice, &changed, failure)) {
            return false;
        }

        if (choice->as.table.phase ==
            PETTA_TABLE_CHOICE_GENERATE_INITIAL) {
            size_t entry_index =
                choice->as.table.iteration_entry;
            PettaTableEntry *entry =
                &shared->entries[entry_index];
            petta_table_record_child_completion(
                shared, choice->as.table.parent_entry,
                entry_index);
            if (entry->tarjan_lowlink !=
                entry->tarjan_index) {
                choice->as.table.phase =
                    PETTA_TABLE_CHOICE_REPLAY;
                continue;
            }

            size_t begin =
                petta_table_stack_find(shared, entry_index);
            if (begin == PETTA_TABLE_ENTRY_NONE) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                shared->failed = true;
                return false;
            }
            bool cyclic =
                shared->tarjan_len > begin + 1u ||
                entry->self_edge;
            if (!cyclic) {
                petta_table_complete_suffix(shared, begin);
                machine->stats.table_scc_completions++;
                choice->as.table.phase =
                    PETTA_TABLE_CHOICE_REPLAY;
                continue;
            }

            choice->as.table.phase =
                PETTA_TABLE_CHOICE_GENERATE_FIXPOINT;
            choice->as.table.scc_begin = begin;
            choice->as.table.scc_cursor = begin;
            choice->as.table.pass_changed = false;
            choice->as.table.iteration_entry =
                shared->tarjan_stack[begin];
            continue;
        }

        choice->as.table.pass_changed =
            choice->as.table.pass_changed || changed;
        choice->as.table.scc_cursor++;
        if (choice->as.table.scc_cursor <
            shared->tarjan_len) {
            choice->as.table.iteration_entry =
                shared->tarjan_stack[
                    choice->as.table.scc_cursor];
            continue;
        }
        if (choice->as.table.pass_changed) {
            choice->as.table.scc_cursor =
                choice->as.table.scc_begin;
            choice->as.table.pass_changed = false;
            choice->as.table.iteration_entry =
                shared->tarjan_stack[
                    choice->as.table.scc_cursor];
            continue;
        }

        petta_table_complete_suffix(
            shared, choice->as.table.scc_begin);
        machine->stats.table_scc_completions++;
        choice->as.table.phase =
            PETTA_TABLE_CHOICE_REPLAY;
    }
}

static bool petta_machine_advance_choice(
    PettaMachineImpl *machine, PettaChoice *choice,
    PettaMachineStep *failure, bool restore_choice_state) {
    if (restore_choice_state) {
        machine->stats.choice_resumes++;
        if (!petta_choice_restore(machine, choice)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }

    if (choice->kind == PETTA_CHOICE_TABLE) {
        return petta_machine_resume_table_choice(
            machine, choice, failure);
    }

    if (choice->kind == PETTA_CHOICE_CLAUSE) {
        bool slot_match = petta_clause_slot_frame_enabled();
        while (choice->as.clause.next_equation <
               choice->as.clause.equation_len) {
            PettaClauseCandidate candidate =
                choice->as.clause.candidates[
                    choice->as.clause.next_equation++];
            Atom *equation = candidate.equation;
            Atom *lhs_shape =
                equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u
                    ? equation->expr.elems[1]
                    : NULL;
            bool lhs_contains_cons_constraint = lhs_shape &&
                (candidate.activation_layout.lhs == lhs_shape &&
                 candidate.activation_layout.
                     lhs_contains_cons_constraint_valid
                     ? candidate.activation_layout.
                           lhs_contains_cons_constraint
                     : petta_semantics_contains_cons_constraint(lhs_shape));
            const PettaSpecializerPatternNode *pattern_root =
                petta_specializer_pattern_root(
                    machine->space, equation);
            bool relational_head =
                equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u &&
                petta_equation_lhs_contains_callable(
                    machine, &candidate, pattern_root);
            bool source_query =
                choice->as.clause.query_source &&
                choice->as.clause.query_template &&
                choice->as.clause.query_epoch != 0u;
            Atom *query = choice->as.clause.query;
            bool direct_frame_match = !query && source_query &&
                slot_match && !relational_head && lhs_shape &&
                lhs_shape->kind == ATOM_EXPR &&
                choice->as.clause.query_source->kind == ATOM_EXPR &&
                lhs_shape->expr.len ==
                    choice->as.clause.query_source->expr.len &&
                lhs_shape->expr.len > 0u &&
                atom_eq(
                    lhs_shape->expr.elems[0],
                    choice->as.clause.query_source->expr.elems[0]) &&
                !lhs_contains_cons_constraint;
            if (!query && !direct_frame_match && source_query) {
                query = petta_machine_apply_bindings_epoch_then_all(
                    machine,
                    (Bindings *)search_context_bindings(
                        &machine->search),
                    &machine->heap,
                    choice->as.clause.query_source,
                    choice->as.clause.query_epoch,
                    choice->as.clause.query_first_entry);
                if (!query) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            if (!query && !direct_frame_match) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            /* A saved continuation carries a shallow whole query.  The
             * synchronous singleton case can instead feed the exact matcher
             * directly from the source/frame pair. */
            if (query && !slot_match) {
                query = petta_machine_apply_bindings(
                    machine,
                    search_context_bindings(&machine->search),
                    &machine->heap, query);
                if (!query) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            if (query && !petta_clause_pattern_may_match_now(
                    machine, lhs_shape, query, pattern_root,
                    !relational_head)) {
                machine->stats.clause_candidates_shape_pruned++;
                if (petta_machine_trace_enabled()) {
                    fputs(
                        "[petta-machine] clause shape-pruned ",
                        stderr);
                    atom_print(equation, stderr);
                    fputc('\n', stderr);
                }
                continue;
            }
            machine->stats.clause_candidates++;
            machine->stats.clause_match_attempts++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            if (petta_machine_trace_enabled()) {
                fprintf(
                    stderr,
                    "[petta-machine] clause candidate plan=%d ",
                    candidate.rhs_plan
                        ? (int)candidate.rhs_plan->role : -1);
                atom_print(equation, stderr);
                fputc('\n', stderr);
            }
            bool extends =
                query && choice->as.clause.evaluate_result &&
                petta_machine_equation_extends_query(
                    machine, equation, query);
            Atom *match_query = query;
            if (extends) {
                if (!lhs_shape ||
                    lhs_shape->kind != ATOM_EXPR ||
                    lhs_shape->expr.len == 0u) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                match_query = atom_expr(
                    &machine->heap, query->expr.elems,
                    lhs_shape->expr.len);
                if (!match_query) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            PettaClauseMatch match = {0};
            bindings_init(&match.environment);
            ChoicePoint candidate_trail =
                search_context_save(&machine->search);
            bool slot_matched = false;
            size_t match_heap_before =
                arena_accounted_live_bytes(&machine->heap);
            if (choice->as.clause.equation_template_c0_closed_query &&
                machine->equation_template_c0_enabled &&
                candidate.equation_template_c0 &&
                !extends && !relational_head) {
                Atom *compiled_result = NULL;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_EXECUTION_ADMITTED);
                PettaEquationTemplateC0Status compiled_status =
                    petta_equation_template_c0_apply(
                        candidate.equation_template_c0,
                        &machine->equation_template_c0_workspace,
                        match_query, &machine->heap,
                        &compiled_result);
                if (compiled_status == PETTA_EQUATION_TEMPLATE_C0_CAPACITY) {
                    bindings_free(&match.environment);
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                if (compiled_status == PETTA_EQUATION_TEMPLATE_C0_MISMATCH) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_EXECUTION_MISMATCH);
                    bindings_free(&match.environment);
                    continue;
                }
                if (compiled_status == PETTA_EQUATION_TEMPLATE_C0_MATCH) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_EXECUTION_MATCH);
                    match.result = compiled_result;
                    match.present = true;
                    match.result_fully_resolved = true;
                    slot_matched = true;
                } else {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_PETTA_EQUATION_TEMPLATE_C0_EXECUTION_FALLBACK);
                }
            }
            if (slot_match &&
                !slot_matched &&
                !relational_head &&
                !lhs_contains_cons_constraint) {
                Atom *slot_result = NULL;
                bool slot_result_fully_resolved = false;
                bool slot_result_is_activation = false;
                uint32_t slot_activation_epoch = 0u;
                uint32_t slot_activation_first_entry = 0u;
                uint64_t slot_materialized_bytes = 0u;
                PettaClauseSlotMatch slot_status =
                    petta_machine_clause_slot_match(
                        machine, equation, match_query,
                        extends
                            ? NULL
                            : choice->as.clause.query_source,
                        extends
                            ? NULL
                            : choice->as.clause.query_template,
                        extends
                            ? 0u
                            : choice->as.clause.query_epoch,
                        extends
                            ? 0u
                            : choice->as.clause.query_first_entry,
                        &slot_result,
                        &slot_result_fully_resolved,
                        &slot_result_is_activation,
                        &slot_activation_epoch,
                        &slot_activation_first_entry,
                        &slot_materialized_bytes);
                if (slot_status == PETTA_CLAUSE_SLOT_CAPACITY) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                if (slot_status == PETTA_CLAUSE_SLOT_MATCH) {
                    if (petta_machine_trace_enabled()) {
                        const PettaPlanNode *rhs_plan =
                            candidate.rhs_plan;
                        fprintf(
                            stderr,
                            "[petta-machine] clause body-materialize "
                            "bytes=%" PRIu64 " role=%d execution=%d "
                            "head=",
                            slot_materialized_bytes,
                            rhs_plan ? (int)rhs_plan->role : -1,
                            rhs_plan ? (int)rhs_plan->execution : -1);
                        Atom *rhs = equation->expr.elems[2];
                        Atom *head = rhs && rhs->kind == ATOM_EXPR &&
                                rhs->expr.len > 0u
                            ? rhs->expr.elems[0] : rhs;
                        atom_print(head, stderr);
                        fputc('\n', stderr);
                    }
                    match.result = slot_result;
                    match.present = true;
                    slot_matched = true;
                    match.result_fully_resolved =
                        slot_result_fully_resolved;
                    match.result_is_activation =
                        slot_result_is_activation;
                    match.activation_epoch =
                        slot_activation_epoch;
                    match.activation_first_entry =
                        slot_activation_first_entry;
                }
            }
            if (!slot_matched && !relational_head && match_query) {
                bool handled = petta_machine_cons_clause_match(
                    machine, equation, match_query,
                    lhs_contains_cons_constraint, &match);
                if (!handled) {
                    (void)query_equation_visit(
                        equation, match_query,
                        &machine->heap,
                        petta_clause_capture, &match);
                }
            }
            petta_machine_add_u64(
                &machine->stats.clause_match_allocated_bytes,
                petta_machine_arena_growth(
                    &machine->heap, match_heap_before));
            if (match.capacity) {
                bindings_free(&match.environment);
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (match.present) {
                petta_machine_trace_atom(
                    "[petta-machine] clause result ",
                    match.result);
                bool merged = slot_matched ||
                    petta_machine_merge(
                        machine, &match.environment,
                        PETTA_BINDING_MERGE_CLAUSE);
                bindings_free(&match.environment);
                if (!merged)
                    continue;
                Atom *result = match.result;
                bool activation_supported =
                    match.result_is_activation && !extends &&
                    choice->as.clause.evaluate_result &&
                    !choice->as.clause.translate_result &&
                    !choice->as.clause.count_collection_result;
                if (match.result_is_activation &&
                    !activation_supported) {
                    Bindings *live_bindings =
                        (Bindings *)search_context_bindings(
                            &machine->search);
                    result = petta_machine_apply_bindings_epoch_then_all(
                        machine, live_bindings, &machine->heap,
                        result, match.activation_epoch,
                        match.activation_first_entry);
                    if (!result) {
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                    match.result_is_activation = false;
                    match.result_fully_resolved = true;
                }
                if (machine->host.record_clause_use) {
                    Bindings evidence_delta;
                    bindings_init(&evidence_delta);
                    bool recorded = machine->host.record_clause_use(
                        machine->host.context, &machine->heap,
                        choice->as.clause.call_occurrence,
                        &candidate, result,
                        search_context_bindings(&machine->search),
                        &evidence_delta);
                    if (!recorded) {
                        bindings_free(&evidence_delta);
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                    bool evidence_merged = petta_machine_merge(
                        machine, &evidence_delta,
                        PETTA_BINDING_MERGE_CLAUSE);
                    bindings_free(&evidence_delta);
                    if (!evidence_merged) {
                        if (slot_matched)
                            search_context_rollback(
                                &machine->search,
                                candidate_trail);
                        petta_machine_invalidate_activation_frame(
                            machine);
                        continue;
                    }
                }
                const PettaPlanNode *result_plan =
                    candidate.rhs_plan;
                if (extends) {
                    result = petta_machine_apply_clause_remainder(
                        machine, query, lhs_shape, result);
                    if (!result ||
                        !petta_machine_clause_remainder_plan(
                            machine, result, candidate.rhs_plan,
                            &result_plan)) {
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                }
                bool scheduled = activation_supported
                    ? petta_push_solve_activation_planned(
                          machine, result,
                          choice->as.clause.expected,
                          choice->barrier, result_plan,
                          candidate.equation_template,
                          match.activation_epoch,
                          match.activation_first_entry)
                    : petta_push_clause_result(
                          machine, result,
                          choice->as.clause.expected,
                          choice->barrier,
                          choice->as.clause.evaluate_result,
                          choice->as.clause.translate_result,
                          choice->as.clause.count_collection_result,
                          result_plan,
                          match.result_fully_resolved);
                if (!scheduled) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                machine->stats.clause_branches_scheduled++;
                return true;
            }
            bindings_free(&match.environment);

            /*
             * PeTTa permits a relation call in an equation-head argument:
              * `(= (h (f $x)) rhs)`.  When ordinary structural matching
              * cannot discharge such an LHS, activate the equation in a
              * fresh namespace, solve each LHS argument against the
              * corresponding call argument, then evaluate the RHS under the
              * resulting bindings.  The occurrence choice remains below
              * these goals, so failure resumes at the next
              * occurrence without a special-case retry path.
            */
            bool observer_requests_materialized_source =
                machine->host.record_clause_use != NULL;
            Atom *activation_lhs =
                candidate.activation_layout.lhs;
            Atom *activation_rhs =
                candidate.activation_layout.rhs;
            bool activation_supported =
                petta_relational_equation_view_enabled() &&
                !observer_requests_materialized_source &&
                !extends &&
                choice->as.clause.evaluate_result &&
                !choice->as.clause.translate_result &&
                !choice->as.clause.count_collection_result &&
                activation_lhs && activation_rhs &&
                term_universe_atom_is_stable(activation_lhs) &&
                term_universe_atom_is_stable(activation_rhs);
            uint32_t epoch = fresh_var_suffix();
            uint32_t activation_first_entry =
                search_context_bindings(&machine->search)->len;
            if (activation_supported) {
                Atom *lhs = activation_lhs;
                Atom *rhs = activation_rhs;
                if (!lhs || lhs->kind != ATOM_EXPR ||
                    !match_query ||
                    match_query->kind != ATOM_EXPR ||
                    lhs->expr.len != match_query->expr.len ||
                    lhs->expr.len == 0u ||
                    !atom_eq(
                        lhs->expr.elems[0],
                        match_query->expr.elems[0])) {
                    continue;
                }
                if (!bindings_builder_prepare_fresh_entries(
                        search_context_builder(&machine->search),
                        candidate.activation_layout.
                            static_variable_count)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                if (!petta_push_solve_activation_planned(
                        machine, rhs,
                        choice->as.clause.expected,
                        choice->barrier, candidate.rhs_plan,
                        candidate.equation_template,
                        epoch, activation_first_entry)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                for (CettaExprIndex index = lhs->expr.len;
                     index > 1u; index--) {
                    CettaExprIndex argument = index - 1u;
                    if (!petta_push_relational_pattern(
                            machine, lhs->expr.elems[argument],
                            match_query->expr.elems[argument],
                            choice->barrier,
                            petta_specializer_pattern_child(
                                pattern_root, argument),
                            epoch, activation_first_entry)) {
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                }
                machine->stats.clause_branches_scheduled++;
                return true;
            }
            uint64_t freshen_bytes_before =
                machine->stats.atom_freshen_allocated_bytes;
            (void)freshen_bytes_before;
            Atom *fresh_equation = petta_machine_freshen_atom(
                machine, equation, epoch);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_WHOLE_EQUATION_FRESHEN);
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_WHOLE_EQUATION_FRESHEN_BYTES,
                machine->stats.atom_freshen_allocated_bytes -
                    freshen_bytes_before);
            if (!fresh_equation ||
                fresh_equation->kind != ATOM_EXPR ||
                fresh_equation->expr.len != 3u ||
                !atom_is_symbol_id(
                    fresh_equation->expr.elems[0],
                    g_builtin_syms.equals)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *lhs = fresh_equation->expr.elems[1];
            Atom *rhs = fresh_equation->expr.elems[2];
            if (!lhs || lhs->kind != ATOM_EXPR ||
                !match_query ||
                match_query->kind != ATOM_EXPR ||
                lhs->expr.len != match_query->expr.len ||
                lhs->expr.len == 0u ||
                !atom_eq(
                    lhs->expr.elems[0],
                    match_query->expr.elems[0])) {
                continue;
            }
            Atom *result = rhs;
            const PettaPlanNode *result_plan =
                candidate.rhs_plan;
            if (extends) {
                result = petta_machine_apply_clause_remainder(
                    machine, query, lhs, rhs);
                if (!result ||
                    !petta_machine_clause_remainder_plan(
                        machine, result, candidate.rhs_plan,
                        &result_plan)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            bool body_scheduled = petta_push_clause_result(
                machine, result,
                choice->as.clause.expected,
                choice->barrier,
                choice->as.clause.evaluate_result,
                choice->as.clause.translate_result,
                choice->as.clause.count_collection_result,
                result_plan, false);
            if (!body_scheduled) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            for (CettaExprIndex index = lhs->expr.len;
                 index > 1u; index--) {
                CettaExprIndex argument = index - 1u;
                if (!petta_push_relational_pattern(
                        machine, lhs->expr.elems[argument],
                        match_query->expr.elems[argument],
                        choice->barrier,
                        petta_specializer_pattern_child(
                            pattern_root, argument),
                        0u, 0u)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            machine->stats.clause_branches_scheduled++;
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_OUTCOMES) {
        OutcomeSet *outcomes = choice->as.outcomes.outcomes;
        while (outcomes && choice->as.outcomes.next < outcomes->len) {
            Outcome *outcome =
                &outcomes->items[choice->as.outcomes.next++];
            Atom *value = outcome->materialized_atom
                ? outcome->materialized_atom : outcome->atom;
            if (!value)
                continue;
            if (!petta_machine_merge(
                    machine, &outcome->env,
                    PETTA_BINDING_MERGE_OUTCOME))
                continue;
            if (!petta_push_unify(
                    machine, value, choice->as.outcomes.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_SUPERPOSE) {
        while (choice->as.superpose.next <
               choice->as.superpose.items->expr.len) {
            CettaExprIndex item = choice->as.superpose.next++;
            Atom *value =
                choice->as.superpose.items->expr.elems[item];
            if (!petta_push_solve_planned(
                    machine, value, choice->as.superpose.expected,
                    choice->barrier,
                    petta_plan_child(
                        choice->as.superpose.items_plan, item))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_BOOLEAN) {
        Atom *expression = choice->as.boolean.expression;
        SymbolId head = atom_head_symbol_id(expression);
        bool inputs[2] = {false, false};
        bool result = false;
        uint32_t arity = 0u;
        uint32_t row_count = 0u;
        if (!expression || expression->kind != ATOM_EXPR ||
            !petta_semantics_boolean_relation_arity(
                head, &arity) ||
            expression->expr.len != (CettaExprLen)arity + 1u) {
            return false;
        }
        row_count = arity == 1u ? 2u : 4u;
        while (choice->as.boolean.next_row < row_count) {
            uint32_t row = choice->as.boolean.next_row++;
            if (choice->as.boolean.inputs_determined)
                choice->as.boolean.next_row = row_count;
            if (!petta_choice_restore(machine, choice)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (!petta_machine_boolean_row(
                    head, row, inputs, &arity, &result)) {
                continue;
            }
            bool matched = true;
            for (uint32_t argument = 0u;
                 argument < arity; argument++) {
                if (!petta_machine_boolean(
                        machine, inputs[argument],
                        expression->expr.elems[argument + 1u])) {
                    matched = false;
                    break;
                }
            }
            if (!matched ||
                !petta_machine_boolean(
                    machine, result,
                    choice->as.boolean.expected)) {
                continue;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_APPEND) {
        Atom *whole = choice->as.append.whole;
        while (choice->as.append.next_split <= whole->expr.len) {
            CettaExprIndex split =
                choice->as.append.next_split++;
            Atom *prefix = atom_expr(
                &machine->heap, whole->expr.elems, split);
            Atom *suffix = atom_expr(
                &machine->heap, whole->expr.elems + split,
                whole->expr.len - split);
            if (!prefix || !suffix ||
                !petta_push_append_operand(
                    machine, choice->as.append.right, suffix,
                    choice->barrier) ||
                !petta_push_append_operand(
                    machine, choice->as.append.left, prefix,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_MEMBER) {
        while (choice->as.member.next <
               choice->as.member.items->expr.len) {
            Atom *candidate =
                choice->as.member.items->expr.elems[
                    choice->as.member.next++];
            if (!petta_machine_unify(
                    machine, choice->as.member.needle,
                    candidate)) {
                continue;
            }
            choice->as.member.saw_match = true;
            if (!petta_machine_boolean(
                    machine, true,
                    choice->as.member.expected)) {
                continue;
            }
            return true;
        }
        if (!choice->as.member.saw_match &&
            !choice->as.member.emitted_false) {
            choice->as.member.emitted_false = true;
            return petta_machine_boolean(
                machine, false,
                choice->as.member.expected);
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_RELATIONAL_MEMBER) {
        while (choice->as.relational_member.next_clause < 2u) {
            uint32_t clause =
                choice->as.relational_member.next_clause++;
            if (!petta_choice_restore(machine, choice)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }

            Atom *head = clause == 0u
                ? choice->as.relational_member.needle
                : petta_fresh_variable(machine);
            Atom *tail = petta_fresh_variable(machine);
            Atom *constraint =
                petta_machine_cons_constraint(machine, head, tail);
            if (!head || !tail || !constraint) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (!petta_semantics_match_cons_constraint(
                    &machine->heap, constraint,
                    choice->as.relational_member.items,
                    search_context_builder(&machine->search)) ||
                bindings_has_loop(
                    (Bindings *)search_context_bindings(
                        &machine->search))) {
                continue;
            }
            if (clause == 0u) {
                if (!petta_machine_boolean(
                        machine, true,
                        choice->as.relational_member.expected)) {
                    continue;
                }
                return true;
            }

            Atom *member = atom_symbol(&machine->heap, "member");
            Atom *recursive = member
                ? atom_expr3(
                      &machine->heap, member,
                      choice->as.relational_member.needle, tail)
                : NULL;
            if (!recursive ||
                !petta_push_solve(
                    machine, recursive,
                    choice->as.relational_member.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_LIST_LENGTH) {
        for (;;) {
            uint64_t suffix =
                choice->as.list_length.next_suffix_length++;
            if (suffix == UINT64_MAX ||
                choice->as.list_length.prefix_length >
                    (uint64_t)INT64_MAX - suffix) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (!petta_choice_restore(machine, choice)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *tail =
                petta_machine_fresh_list(machine, suffix);
            Atom *length = atom_int(
                &machine->heap,
                (int64_t)(
                    choice->as.list_length.prefix_length +
                    suffix));
            if (!tail || !length) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (!petta_machine_unify(
                    machine,
                    choice->as.list_length.open_tail, tail) ||
                !petta_machine_unify(
                    machine, choice->as.list_length.expected,
                    length)) {
                continue;
            }
            return true;
        }
    }

    if (choice->kind == PETTA_CHOICE_TYPED_CALL) {
        while (choice->as.typed_call.next <
               choice->as.typed_call.count) {
            uint32_t index = choice->as.typed_call.next++;
            Atom *type = choice->as.typed_call.types[index];
            CettaExprLen nargs =
                choice->as.typed_call.expression->expr.len - 1u;
            if (!petta_machine_type_signature_applies(type, nargs))
                continue;
            if (!petta_machine_schedule_typed_call(
                    machine, choice->as.typed_call.expression,
                    choice->as.typed_call.expected, type,
                    choice->barrier,
                    choice->as.typed_call.overload_dispatch,
                    choice->as.typed_call.plan)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (choice->kind == PETTA_CHOICE_EQUAL_DEFAULT) {
        if (choice->as.equal_default.saw_answer)
            return false;
        choice->as.equal_default.saw_answer = true;
        return petta_machine_boolean(
            machine, false,
            choice->as.equal_default.expected);
    }

    if (choice->kind == PETTA_CHOICE_CASE_DEFAULT) {
        if (choice->as.case_default.saw_answer)
            return false;
        choice->as.case_default.saw_answer = true;
        if (!petta_push_solve_planned(
                machine, choice->as.case_default.expression,
                choice->as.case_default.expected,
                choice->barrier,
                choice->as.case_default.plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (choice->kind == PETTA_CHOICE_RELATIONAL_EXTENSION) {
        if (choice->as.relational_extension.fallback_started)
            return false;
        /*
         * Intrinsic host answers are the first logical clause.  Once those
         * alternatives are exhausted, ordinary backtracking continues with
         * the explicitly authored relation clauses exactly once.
         */
        choice->as.relational_extension.fallback_started = true;
        if (!petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_CLAUSE_ONLY,
                    .barrier = choice->barrier,
                    .first =
                        choice->as.relational_extension.expression,
                    .second =
                        choice->as.relational_extension.expected,
                })) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (choice->kind == PETTA_CHOICE_COLLAPSE) {
        if (choice->as.collapse.emitted ||
            !choice->as.collapse.machine) {
            return false;
        }
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(
                choice->as.collapse.machine,
                &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                if (answer && !atom_is_empty(answer)) {
                    if (!petta_machine_reserve(
                            (void **)&choice->as.collapse.items,
                            &choice->as.collapse.item_cap,
                            choice->as.collapse.item_len + 1u,
                            sizeof(*choice->as.collapse.items))) {
                        bindings_free(&environment);
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                    choice->as.collapse.items[
                        choice->as.collapse.item_len++] = answer;
                }
                bindings_free(&environment);
                continue;
            }
            bindings_free(&environment);
            if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                *failure = step;
                return false;
            }
            if (step != PETTA_MACHINE_STEP_EXHAUSTED) {
                *failure = step;
                return false;
            }
            PettaMachineStats inner_stats;
            if (petta_machine_stats(
                    choice->as.collapse.machine,
                    &inner_stats)) {
                petta_machine_stats_accumulate(
                    &machine->stats, &inner_stats);
            }
            bool has_success = false;
            for (size_t index = 0u;
                 index < choice->as.collapse.item_len; index++) {
                if (!atom_is_error(
                        choice->as.collapse.items[index])) {
                    has_success = true;
                    break;
                }
            }
            size_t visible_len = 0u;
            for (size_t index = 0u;
                 index < choice->as.collapse.item_len; index++) {
                Atom *item = choice->as.collapse.items[index];
                if (has_success && atom_is_error(item))
                    continue;
                choice->as.collapse.items[visible_len++] = item;
            }
            if (!cetta_expr_len_fits_size(
                    (CettaExprLen)visible_len)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *collected = atom_expr(
                &machine->heap,
                choice->as.collapse.items,
                (CettaExprLen)visible_len);
            if (!collected ||
                !petta_push_unify(
                    machine, collected,
                    choice->as.collapse.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            choice->as.collapse.emitted = true;
            return true;
        }
    }

    if (choice->kind == PETTA_CHOICE_COUNT_COLLAPSE) {
        if (choice->as.count_collapse.emitted ||
            !choice->as.count_collapse.machine) {
            return false;
        }
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(
                choice->as.count_collapse.machine,
                &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                if (answer && !atom_is_empty(answer)) {
                    uint64_t answer_weight =
                        choice->as.count_collapse.machine->impl
                            ? choice->as.count_collapse.machine
                                  ->impl->last_answer_weight
                            : 0u;
                    if (answer_weight == 0u)
                        answer_weight = 1u;
                    if (answer_weight >
                        (uint64_t)INT64_MAX -
                            choice->as.count_collapse.count) {
                        bindings_free(&environment);
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                    choice->as.count_collapse.count +=
                        answer_weight;
                    if (atom_is_error(answer))
                        choice->as.count_collapse.error_count +=
                            answer_weight;
                    machine->stats
                        .count_aggregate_boundary_copies_avoided +=
                            answer_weight;
                }
                bindings_free(&environment);
                continue;
            }
            bindings_free(&environment);
            if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                *failure = step;
                return false;
            }
            if (step != PETTA_MACHINE_STEP_EXHAUSTED) {
                *failure = step;
                return false;
            }
            PettaMachineStats inner_stats;
            if (petta_machine_stats(
                    choice->as.count_collapse.machine,
                    &inner_stats)) {
                petta_machine_stats_accumulate(
                    &machine->stats, &inner_stats);
            }
            uint64_t visible_count =
                choice->as.count_collapse.count >
                        choice->as.count_collapse.error_count
                    ? choice->as.count_collapse.count -
                          choice->as.count_collapse.error_count
                    : choice->as.count_collapse.error_count;
            machine->stats.count_aggregate_answers +=
                visible_count;
            Atom *count =
                choice->as.count_collapse.wrap_collection
                    ? atom_petta_counted_collection(
                          &machine->heap,
                          (int64_t)visible_count)
                    : atom_int(
                          &machine->heap,
                          (int64_t)visible_count);
            if (!count ||
                !petta_push_unify(
                    machine, count,
                    choice->as.count_collapse.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            choice->as.count_collapse.emitted = true;
            return true;
        }
    }

    if (choice->kind == PETTA_CHOICE_ONCE ||
        choice->kind == PETTA_CHOICE_TRANSACTION ||
        choice->kind == PETTA_CHOICE_MUTEX) {
        petta_choice_release(machine, choice);
        return false;
    }

    if (choice->kind == PETTA_CHOICE_MATCH &&
        choice->as.match.terminal_count_fold) {
        if (choice->as.match.binding_snapshot_mode) {
            CettaCount count =
                choice->as.match.binding_snapshot.len;
            if (count == 0u)
                return false;
            if (count > (CettaCount)INT64_MAX ||
                !petta_machine_merge(
                    machine,
                    &choice->as.match.binding_snapshot.items[0],
                    PETTA_BINDING_MERGE_OUTCOME) ||
                !petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind = PETTA_GOAL_SET_ANSWER_WEIGHT,
                        .barrier = choice->barrier,
                        .answer_weight = count,
                    }) ||
                !petta_push_unify(
                    machine, choice->as.match.template,
                    choice->as.match.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            choice->as.match.next_index = count;
            choice->as.match.terminal_count_fold = false;
            machine->stats.match_candidates += count;
            machine->stats.count_aggregate_match_folds++;
            machine->stats.count_aggregate_match_answers += count;
            return true;
        }
        CettaCount length =
            choice->as.match.snapshot_len;
        Atom *first_match = NULL;
        uint64_t match_count = 0u;
        for (CettaIndex index = 0u;
             index < length; index++) {
            if (!petta_choice_restore(machine, choice)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *candidate =
                choice->as.match.snapshot[index];
            if (!candidate)
                continue;
            machine->stats.match_candidates++;
            Atom *fresh = atom_has_vars(candidate)
                ? petta_machine_freshen_atom(
                      machine, candidate, fresh_var_suffix())
                : candidate;
            if (!fresh) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (!petta_machine_unify(
                    machine, choice->as.match.pattern,
                    fresh)) {
                continue;
            }
            if (match_count >=
                (uint64_t)INT64_MAX) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            match_count++;
            if (!first_match)
                first_match = candidate;
        }
        if (!petta_choice_restore(machine, choice)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        choice->as.match.next_index = length;
        choice->as.match.terminal_count_fold = false;
        if (match_count == 0u)
            return false;

        Atom *fresh = atom_has_vars(first_match)
            ? petta_machine_freshen_atom(
                  machine, first_match, fresh_var_suffix())
            : first_match;
        if (!fresh ||
            !petta_machine_unify(
                machine, choice->as.match.pattern,
                fresh) ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind =
                        PETTA_GOAL_SET_ANSWER_WEIGHT,
                    .barrier = choice->barrier,
                    .answer_weight = match_count,
                }) ||
            !petta_push_unify(
                machine, choice->as.match.template,
                choice->as.match.expected,
                choice->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        machine->stats.count_aggregate_match_folds++;
        machine->stats.count_aggregate_match_answers +=
            match_count;
        return true;
    }

    if (!choice->as.match.snapshot_mode &&
        !space_read_token_is_current(choice->as.match.read)) {
        *failure = PETTA_MACHINE_STEP_INVALIDATED;
        return false;
    }
    CettaCount length = choice->as.match.binding_snapshot_mode
        ? choice->as.match.binding_snapshot.len
        : choice->as.match.snapshot_mode
              ? choice->as.match.snapshot_len
              : space_length64(choice->as.match.space);
    while (choice->as.match.next_index < length) {
        CettaIndex candidate_index =
            choice->as.match.next_index++;
        if (choice->as.match.binding_snapshot_mode) {
            machine->stats.match_candidates++;
            if (!petta_machine_merge(
                    machine,
                    &choice->as.match.binding_snapshot.items[
                        candidate_index],
                    PETTA_BINDING_MERGE_OUTCOME)) {
                continue;
            }
            if (!petta_push_solve(
                    machine, choice->as.match.template,
                    choice->as.match.expected,
                    choice->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        Atom *candidate = choice->as.match.snapshot_mode
            ? choice->as.match.snapshot[candidate_index]
            : space_get_at64(
                  choice->as.match.space, candidate_index);
        if (!candidate)
            continue;
        petta_machine_trace_atom(
            "[petta-machine] match candidate ", candidate);
        machine->stats.match_candidates++;
        bool matched = false;
        if (atom_has_vars(candidate) &&
            choice->as.match.pattern->kind != ATOM_VAR &&
            !petta_semantics_is_open_cons_value(
                choice->as.match.pattern) &&
            !petta_semantics_is_open_cons_value(candidate)) {
            matched = petta_machine_match_epoch_candidate(
                machine, choice->as.match.pattern,
                candidate, fresh_var_suffix());
        } else {
            Atom *fresh = atom_has_vars(candidate)
                ? petta_machine_freshen_atom(
                      machine, candidate, fresh_var_suffix())
                : candidate;
            if (!fresh) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            matched = petta_machine_unify(
                machine, choice->as.match.pattern, fresh);
        }
        if (!matched) {
            continue;
        }
        if (!petta_push_solve(
                machine, choice->as.match.template,
                choice->as.match.expected,
                choice->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    return false;
}

static bool petta_machine_resume_choice(
    PettaMachineImpl *machine, PettaChoice *choice,
    PettaMachineStep *failure) {
    return petta_machine_advance_choice(
        machine, choice, failure, true);
}

static bool petta_machine_backtrack(
    PettaMachineImpl *machine, PettaMachineStep *failure) {
    while (machine->choice_len > 0u) {
        PettaChoice *choice =
            &machine->choices[machine->choice_len - 1u];
        if (petta_machine_trace_enabled()) {
            fprintf(
                stderr,
                "[petta-machine] backtrack choice=%u depth=%zu\n",
                (unsigned)choice->kind, machine->choice_len);
        }
        if (petta_machine_resume_choice(machine, choice, failure))
        {
            petta_choice_retire_exhausted(machine);
            return true;
        }
        if (*failure != PETTA_MACHINE_STEP_EXHAUSTED)
            return false;
        petta_choice_pop(machine);
    }
    return false;
}

static Atom *petta_machine_materialize_space_query(
        PettaMachineImpl *machine,
        const PettaSpaceQueryView *query) {
    if (!machine || !query || !query->head ||
        (query->arity > 0u && !query->arguments) ||
        query->arity == UINT32_MAX) {
        return NULL;
    }
    if (query->whole)
        return query->whole;
    CettaExprLen length = query->arity + 1u;
    Atom *draft = atom_expr_builder_begin(
        &machine->heap, length);
    if (!draft)
        return NULL;
    draft->expr.elems[0] = query->head;
    for (CettaExprIndex index = 0u;
         index < query->arity; index++) {
        draft->expr.elems[index + 1u] =
            query->arguments[index];
    }
    return atom_expr_builder_finish(&machine->heap, draft);
}

static bool petta_machine_start_space_query(
    PettaMachineImpl *machine, const PettaSpaceQueryView *query,
    Atom *expected, uint32_t barrier, bool evaluate_result,
    bool count_collection_result) {
    if (!machine || !query || !query->head ||
        query->head->kind != ATOM_SYMBOL ||
        (!query->whole && query->arity > 0u && !query->arguments)) {
        return false;
    }
    SymbolId head = query->head->sym_id;
    bool translate_result =
        machine->host.translator_rule_contains &&
        machine->host.translator_rule_contains(
            machine->host.context, head);
    if (head == SYMBOL_ID_NONE) {
        return false;
    }
    bool source_view = query->source && query->source_template &&
        query->source_epoch != 0u;
    if (source_view) {
        if (query->source_first_entry >
                search_context_bindings(&machine->search)->len ||
            !term_universe_atom_is_stable(query->source)) {
            source_view = false;
        } else {
            cetta_provenance_assert_not_transient(
                query->source, "petta.equation.query-source");
        }
    }
    if (petta_query_trace_head(machine, head)) {
        static _Thread_local uint32_t emitted = 0u;
        if (emitted < machine->trace.query_limit &&
            machine->stats.transitions >= machine->trace.query_start) {
            const Bindings *bindings =
                search_context_bindings(&machine->search);
            fprintf(
                stderr,
                "[petta-query] transitions=%" PRIu64
                " head=%s bindings=%u",
                machine->stats.transitions,
                symbol_bytes(g_symbols, head),
                bindings ? bindings->len : 0u);
            if (!machine->trace.query_heads_only) {
                Atom *resolved = source_view
                    ? petta_machine_apply_bindings_epoch_then_all(
                          machine, (Bindings *)bindings, &machine->heap,
                          query->source, query->source_epoch,
                          query->source_first_entry)
                    : query->whole
                        ? petta_machine_apply_bindings(
                              machine, bindings, &machine->heap,
                              query->whole)
                        : NULL;
                fputs(" resolved=", stderr);
                if (resolved)
                    atom_print(resolved, stderr);
                else
                    fputs("<unavailable>", stderr);
            }
            fputc('\n', stderr);
            emitted++;
        }
    }
    /*
     * Clause selection obeys the same logical-update view as `match`.
     * Snapshot only the head-indexed candidate occurrences, rather than the
     * whole space, so a clause may add or remove definitions without
     * invalidating the alternatives of the call already in progress.
     */
    PettaClauseSnapshotLease candidate_lease = {0};
    PettaClauseCandidate *fallback_candidates = NULL;
    size_t fallback_len = 0u;
    size_t equation_cap = 0u;
    PettaClauseSnapshotStats snapshot_stats = {0};
    if (machine->host.clause_snapshot_lease) {
        if (!machine->host.clause_snapshot_lease(
                machine->host.context, machine->space, head,
                &candidate_lease, &snapshot_stats)) {
            machine->terminal = true;
            machine->terminal_step =
                PETTA_MACHINE_STEP_INVALIDATED;
            return false;
        }
    } else {
        snapshot_stats.snapshots = 1u;
        SpaceEquationCursor cursor;
        if (!space_equation_cursor_init(
                machine->space, head, &cursor)) {
            return false;
        }
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step =
                space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            if (step == SPACE_EQUATION_CURSOR_INVALIDATED) {
                free(fallback_candidates);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            SpaceEquationOccurrence occurrence;
            if (!space_equation_occurrence_resolve(
                    id, &occurrence)) {
                free(fallback_candidates);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            if (!petta_machine_reserve(
                    (void **)&fallback_candidates, &equation_cap,
                    fallback_len + 1u,
                    sizeof(*fallback_candidates))) {
                free(fallback_candidates);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            fallback_candidates[fallback_len++] =
                (PettaClauseCandidate){
                    .equation = occurrence.equation,
                    .occurrence = occurrence.id,
                };
            snapshot_stats.live_occurrences_scanned++;
        }
        snapshot_stats.candidates_emitted = fallback_len;
        candidate_lease.items = fallback_candidates;
        candidate_lease.len = fallback_len;
        candidate_lease.owned_items = fallback_candidates;
    }
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_calls,
        snapshot_stats.snapshots);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_cache_hits,
        snapshot_stats.cache_hits);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_live_occurrences,
        snapshot_stats.live_occurrences_scanned);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_records_examined,
        snapshot_stats.declaration_records_examined);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_pointer_identity_hits,
        snapshot_stats.pointer_identity_hits);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_equality_checks,
        snapshot_stats.structural_equality_checks);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_alpha_checks,
        snapshot_stats.alpha_equality_checks);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_candidates,
        snapshot_stats.candidates_emitted);

    const PettaClauseCandidate *source_candidates =
        candidate_lease.items;
    size_t source_len = candidate_lease.len;
    PettaClauseCandidate *equations = NULL;
    size_t equation_len = 0u;
    bool candidates_structurally_verified = false;
    if (source_len > 0u) {
        if (!source_candidates) {
            petta_program_clause_snapshot_lease_release(
                &candidate_lease);
            machine->terminal = true;
            machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        /* Whole and split-register calls describe the same logical query.
         * The deep selector has a dedicated split-parts entry point and
         * still returns only a candidate superset; the ordinary matcher
         * remains authoritative below.  Admission therefore depends on the
         * relation proof, not on whether the call was materialized. */
        bool deep_admissible =
            machine->host.tabled_relation_admissible &&
            machine->host.tabled_relation_admissible(
                machine->host.context, machine->space, head,
                query->arity);
        const uint32_t *selected = NULL;
        size_t selected_count = 0u;
        CettaMatchDecisionSelectState selection =
            petta_match_decision_select_candidates(
                machine, head, query,
                source_candidates, source_len,
                candidate_lease.owned_items
                    ? NULL : source_candidates,
                deep_admissible, &selected, &selected_count,
                &candidates_structurally_verified);
        if (selection != CETTA_MATCH_DECISION_SELECT_READY) {
            petta_program_clause_snapshot_lease_release(
                &candidate_lease);
            machine->terminal = true;
            machine->terminal_step =
                selection == CETTA_MATCH_DECISION_SELECT_INVALIDATED
                    ? PETTA_MACHINE_STEP_INVALIDATED
                    : PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        petta_machine_add_u64(
            &machine->stats.clause_candidates_shape_pruned,
            source_len - selected_count);
        if (selected_count > SIZE_MAX / sizeof(*equations)) {
            petta_program_clause_snapshot_lease_release(
                &candidate_lease);
            machine->terminal = true;
            machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        equations = selected_count
            ? malloc(sizeof(*equations) * selected_count)
            : NULL;
        if (selected_count && !equations) {
            petta_program_clause_snapshot_lease_release(
                &candidate_lease);
            machine->terminal = true;
            machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (size_t index = 0u; index < selected_count; index++) {
            if (selected[index] >= source_len) {
                petta_program_clause_snapshot_lease_release(
                    &candidate_lease);
                free(equations);
                machine->terminal = true;
                machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            PettaClauseCandidate candidate =
                source_candidates[selected[index]];
            Atom *equation = candidate.equation;
            Atom *lhs =
                equation && equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u
                    ? equation->expr.elems[1]
                    : NULL;
            bool may_match = candidates_structurally_verified ||
                !lhs || !query->whole ||
                petta_semantics_cons_pattern_may_match(
                    lhs, query->whole);
            if (!candidates_structurally_verified &&
                may_match && lhs &&
                lhs->kind == ATOM_EXPR &&
                lhs->expr.len > 0u) {
                CettaExprLen lhs_arity = lhs->expr.len - 1u;
                CettaExprLen aligned = lhs_arity < query->arity
                    ? lhs_arity : query->arity;
                for (CettaExprIndex argument = 0u;
                     argument < aligned; argument++) {
                    Atom *pattern =
                        lhs->expr.elems[argument + 1u];
                    Atom *value = query->whole
                        ? query->whole->expr.elems[argument + 1u]
                        : query->arguments[argument];
                    if (!pattern || !value ||
                        atom_has_vars(value)) {
                        continue;
                    }
                    bool literal =
                        pattern->kind == ATOM_SYMBOL ||
                        pattern->kind == ATOM_GROUNDED ||
                        (pattern->kind == ATOM_EXPR &&
                         pattern->expr.len == 0u);
                    if (literal && !atom_eq(pattern, value)) {
                        may_match = false;
                        break;
                    }
                }
            }
            if (!may_match) {
                machine->stats.clause_candidates_shape_pruned++;
                continue;
            }
            if (!candidate.rhs_plan) {
                candidate.rhs_plan =
                    petta_specializer_equation_plan(
                        machine->space, candidate.equation);
            }
            equations[equation_len++] = candidate;
        }
    }
    petta_program_clause_snapshot_lease_release(&candidate_lease);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_candidates_copied,
        equation_len);

    /* A split query can stay register-like for a singleton selected
     * occurrence.  Multiple possible occurrences need a saved logical-update
     * continuation, so publish one shallow shell before capturing its heap
     * mark.  Its children remain the already resolved query fields. */
    Atom *whole_query = query->whole;
    if (!whole_query &&
        (equation_len > 1u || machine->host.begin_relation_call)) {
        whole_query = petta_machine_materialize_space_query(
            machine, query);
        if (!whole_query) {
            free(equations);
            machine->terminal = true;
            machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }

    size_t entry_depth = machine->choice_len;
    size_t goal_trail_mark = machine->goal_trail_len;
    uint64_t call_occurrence = 0u;
    if (machine->host.begin_relation_call) {
        call_occurrence = machine->host.begin_relation_call(
            machine->host.context, space_read_token(machine->space),
            whole_query);
        if (call_occurrence == 0u) {
            free(equations);
            machine->terminal = true;
            machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }
    PettaChoice choice = {
        .kind = PETTA_CHOICE_CLAUSE,
        .trail = search_context_save(&machine->search),
        .heap_mark = arena_mark(&machine->heap),
        .heap_mark_captured = true,
        .goal_height = machine->goal_len,
        .goal_trail_mark = goal_trail_mark,
        .type_obligation_mark = machine->type_obligation_len,
        .barrier = (uint32_t)entry_depth,
        .as.clause = {
            .candidates = equations,
            .equation_len = equation_len,
            .next_equation = 0u,
            .call_occurrence = call_occurrence,
            .query = whole_query,
            .query_source = source_view ? query->source : NULL,
            .query_template = source_view
                ? query->source_template : NULL,
            .query_epoch = source_view
                ? query->source_epoch : 0u,
            .query_first_entry =
                source_view ? query->source_first_entry : 0u,
            .expected = expected,
            .evaluate_result =
                evaluate_result || translate_result,
            .translate_result = translate_result,
            .count_collection_result =
                count_collection_result,
            .equation_template_c0_closed_query =
                whole_query && !atom_has_vars(whole_query),
        },
    };
    /*
     * Select the first viable clause against the current trail and
     * continuation before allocating a choice record.  The continuation
     * prefix is unchanged by clause selection: selected branch goals are
     * appended above it.  If selection reaches the last candidate, this is
     * the WAM `trust` case and no choice point exists at all.  Only a genuine
     * remaining alternative pays to snapshot the prefix for later `retry`.
     */
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    bool selected = equation_len > 0u &&
        petta_machine_advance_choice(
            machine, &choice, &failure, false);
    if (selected &&
        petta_choice_exhausted_after_success(&choice)) {
        petta_choice_release(machine, &choice);
        machine->stats.deterministic_clause_choices_elided++;
        return true;
    }
    if (selected) {
        if (machine->trace.choice) {
            fprintf(
                stderr,
                "[petta-choice] head=%s candidates=%zu next=%zu depth=%zu\n",
                symbol_bytes(g_symbols, head),
                choice.as.clause.equation_len,
                choice.as.clause.next_equation,
                machine->choice_len + 1u);
        }
        /* The first selection consumed the exact source/frame view
         * synchronously.  Stored alternatives retain the fully rooted
         * shallow query and use the ordinary exact matcher, avoiding a new
         * choice-payload liveness contract. */
        choice.as.clause.query_source = NULL;
        choice.as.clause.query_template = NULL;
        choice.as.clause.query_epoch = 0u;
        choice.as.clause.query_first_entry = 0u;
        if (petta_choice_push_at_goal_trail_mark(
                machine, choice, goal_trail_mark)) {
            return true;
        }
        (void)petta_choice_restore(machine, &choice);
        machine->terminal = true;
        machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    (void)petta_choice_restore(machine, &choice);
    petta_choice_release(machine, &choice);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    (void)barrier;
    return false;
}

static bool petta_machine_start_clause_choice(
    PettaMachineImpl *machine, Atom *query, Atom *expected,
    uint32_t barrier, bool evaluate_result,
    bool count_collection_result, Atom *query_source,
    const PettaEquationTemplate *query_template,
    uint32_t query_epoch, uint32_t query_first_entry) {
    if (!query || query->kind != ATOM_EXPR ||
        query->expr.len == 0u) {
        return false;
    }
    PettaSpaceQueryView view = {
        .whole = query,
        .head = query->expr.elems[0],
        .arguments = query->expr.elems + 1u,
        .arity = query->expr.len - 1u,
        .source = query_source,
        .source_template = query_template,
        .source_epoch = query_epoch,
        .source_first_entry = query_first_entry,
    };
    return petta_machine_start_space_query(
        machine, &view, expected, barrier,
        evaluate_result, count_collection_result);
}

static bool petta_machine_start_outcome_choice(
    PettaMachineImpl *machine, OutcomeSet *outcomes, Atom *expected,
    uint32_t barrier) {
    CettaCount outcome_count = outcomes ? outcomes->len : 0u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_SET);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_ITEM,
        outcome_count);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_ITEM_PEAK,
        outcome_count);
    if (outcome_count == 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_EMPTY);
    } else if (outcome_count == 1u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_SINGLETON);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_OUTCOME_CHOICE_MULTIPLE);
    }
    PettaChoice choice = {
        .kind = PETTA_CHOICE_OUTCOMES,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.outcomes = {
            .outcomes = outcomes,
            .next = 0u,
            .expected = expected,
        },
    };
    /*
     * A completed singleton OutcomeSet is deterministic.  Consume it through
     * the same selector as an ordinary choice, but do not retain a
     * continuation snapshot for a second outcome which cannot exist.
     */
    if (!outcomes || outcomes->len <= 1u) {
        PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
        bool selected = outcomes && outcomes->len == 1u &&
            petta_machine_advance_choice(
                machine, &choice, &failure, false);
        petta_choice_release(machine, &choice);
        machine->stats.singleton_outcome_choices_elided++;
        if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
            machine->terminal = true;
            machine->terminal_step = failure;
        }
        return selected;
    }
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_start_intrinsic_get_type(
    PettaMachineImpl *machine, Atom *value, Atom *expected,
    uint32_t barrier, PettaMachineStep *failure) {
    if (!machine || !value || !expected || !failure ||
        !machine->host.get_type) {
        return false;
    }
    Atom **types = NULL;
    uint32_t type_count = 0u;
    if (!machine->host.get_type(
            machine->host.context, machine->space,
            &machine->heap, value, &types, &type_count)) {
        free(types);
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    if (type_count == 0u) {
        free(types);
        return false;
    }
    if (type_count == 1u) {
        Atom *type = types[0];
        free(types);
        return petta_machine_unify(
            machine, type, expected);
    }

    OutcomeSet *intrinsic = cetta_malloc(sizeof(*intrinsic));
    outcome_set_init_with_owner(intrinsic, &machine->heap);
    Bindings empty;
    bindings_init(&empty);
    for (uint32_t index = 0u; index < type_count; index++)
        outcome_set_add(intrinsic, types[index], &empty);
    bindings_free(&empty);
    free(types);
    return petta_machine_start_outcome_choice(
        machine, intrinsic, expected, barrier);
}

static bool petta_machine_try_extension_call(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, bool *recognized,
    PettaMachineStep *failure) {
    if (recognized)
        *recognized = false;
    if (!machine || !expression || !expected ||
        !recognized || !failure ||
        !machine->host.extension_call) {
        return machine && expression && expected &&
               recognized && failure;
    }

    OutcomeSet *outcomes = cetta_malloc(sizeof(*outcomes));
    outcome_set_init_with_owner(outcomes, &machine->heap);
    bool handled = false;
    bool called = machine->host.extension_call(
        machine->host.context, &machine->heap,
        expression, expected,
        search_context_bindings(&machine->search),
        outcomes, &handled);
    if (!called) {
        outcome_set_free(outcomes);
        free(outcomes);
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    *recognized = handled;
    if (!handled) {
        outcome_set_free(outcomes);
        free(outcomes);
        return true;
    }
    return petta_machine_start_outcome_choice(
        machine, outcomes, expected, barrier);
}

static bool petta_machine_schedule_relational_extension(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier) {
    if (!machine || !expression || !expected)
        return false;
    size_t entry_choice_len = machine->choice_len;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_RELATIONAL_EXTENSION,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.relational_extension = {
            .fallback_started = false,
            .expression = expression,
            .expected = expected,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    /*
     * Evaluate the host relation first.  Its OutcomeSet choice is pushed
     * above this record; once exhausted, ordinary backtracking resumes the
     * explicit clause fallback stored below it.
     */
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_HOST_READY,
                .barrier = barrier,
                .first = expression,
                .second = expected,
            })) {
        petta_choice_truncate(machine, entry_choice_len);
        return false;
    }
    return true;
}

typedef struct {
    Atom *source;
    Atom **destination;
    Atom **children;
    CettaExprIndex next_child;
} PettaMemoQuantizeFrame;

static double petta_memo_quantize_float(
    double value, uint32_t precision) {
    static const double powers_of_ten[] = {
        1.0,
        10.0,
        100.0,
        1000.0,
        10000.0,
        100000.0,
        1000000.0,
        10000000.0,
        100000000.0,
        1000000000.0,
        10000000000.0,
        100000000000.0,
        1000000000000.0,
        10000000000000.0,
        100000000000000.0,
        1000000000000000.0,
    };
    if (!isfinite(value) ||
        precision >= sizeof(powers_of_ten) / sizeof(powers_of_ten[0])) {
        return value;
    }
    double scale = powers_of_ten[precision];
    double scaled = value * scale;
    if (!isfinite(scaled))
        return value;
    double quantized = round(scaled) / scale;
    return quantized == 0.0 ? 0.0 : quantized;
}

/* Build a temporary ground key with decimal floats quantized.  Non-float
 * leaves may be shared because variant canonicalization immediately copies
 * the complete key into table-owned storage.  The explicit work stack keeps
 * deeply nested data independent of the C call stack. */
static Atom *petta_memo_quantized_ground_key(
    PettaMachineImpl *machine, Atom *source,
    uint32_t precision) {
    if (!machine || !source)
        return NULL;
    if (source->kind == ATOM_GROUNDED &&
        source->ground.gkind == GV_FLOAT) {
        return atom_float(
            &machine->heap,
            petta_memo_quantize_float(
                source->ground.fval, precision));
    }
    if (source->kind != ATOM_EXPR)
        return source;

    PettaMemoQuantizeFrame *frames = NULL;
    size_t frame_len = 0u;
    size_t frame_cap = 0u;
    Atom *result = NULL;
    Atom **root_children = source->expr.len
        ? cetta_malloc(
              sizeof(*root_children) * (size_t)source->expr.len)
        : NULL;
    if (!petta_machine_reserve(
            (void **)&frames, &frame_cap, 1u,
            sizeof(*frames))) {
        free(root_children);
        return NULL;
    }
    frames[frame_len++] = (PettaMemoQuantizeFrame){
        .source = source,
        .destination = &result,
        .children = root_children,
    };

    bool ok = true;
    while (frame_len > 0u) {
        PettaMemoQuantizeFrame *frame = &frames[frame_len - 1u];
        if (frame->next_child < frame->source->expr.len) {
            CettaExprIndex child_index = frame->next_child++;
            Atom *child = frame->source->expr.elems[child_index];
            if (child->kind == ATOM_GROUNDED &&
                child->ground.gkind == GV_FLOAT) {
                frame->children[child_index] = atom_float(
                    &machine->heap,
                    petta_memo_quantize_float(
                        child->ground.fval, precision));
                continue;
            }
            if (child->kind != ATOM_EXPR) {
                frame->children[child_index] = child;
                continue;
            }
            Atom **children = child->expr.len
                ? cetta_malloc(
                      sizeof(*children) *
                      (size_t)child->expr.len)
                : NULL;
            if (!petta_machine_reserve(
                    (void **)&frames, &frame_cap,
                    frame_len + 1u, sizeof(*frames))) {
                free(children);
                ok = false;
                break;
            }
            frame = &frames[frame_len - 1u];
            frames[frame_len++] = (PettaMemoQuantizeFrame){
                .source = child,
                .destination = &frame->children[child_index],
                .children = children,
            };
            continue;
        }
        Atom *rebuilt = atom_expr(
            &machine->heap, frame->children,
            frame->source->expr.len);
        free(frame->children);
        frame->children = NULL;
        if (!rebuilt) {
            ok = false;
            break;
        }
        *frame->destination = rebuilt;
        frame_len--;
    }
    while (frame_len > 0u)
        free(frames[--frame_len].children);
    free(frames);
    return ok ? result : NULL;
}

static bool petta_machine_start_tabled_call(
    PettaMachineImpl *machine, Atom *query, Atom *expected,
    uint32_t barrier, bool *handled,
    PettaMachineStep *failure) {
    if (handled)
        *handled = false;
    if (!machine || !query || !expected || !handled ||
        !failure || query->kind != ATOM_EXPR ||
        query->expr.len == 0u ||
        query->expr.elems[0]->kind != ATOM_SYMBOL ||
        !machine->host.tabled_relation_contains ||
        !machine->host.tabled_relation_contains(
            machine->host.context,
            query->expr.elems[0]->sym_id,
            query->expr.len - 1u)) {
        return false;
    }
    if (machine->host.tabled_relation_admissible &&
        !machine->host.tabled_relation_admissible(
            machine->host.context, machine->space,
            query->expr.elems[0]->sym_id,
            query->expr.len - 1u)) {
        return false;
    }

    /*
     * A generator evaluates its own root through ordinary clause dispatch;
     * recursive tabled calls made by that evaluation still enter this path.
     */
    if (machine->bypass_root_table) {
        machine->bypass_root_table = false;
        return false;
    }

    *handled = true;
    machine->stats.table_lookups++;
    bool ground_query = !atom_has_vars(query);
    bool memoized =
        machine->host.memoized_relation_contains &&
        machine->host.memoized_relation_contains(
            machine->host.context,
            query->expr.elems[0]->sym_id,
            query->expr.len - 1u);
    Atom *key_query = query;
    if (memoized && ground_query &&
        machine->host.memoized_relation_float_precision) {
        uint32_t precision =
            machine->host.memoized_relation_float_precision(
                machine->host.context,
                query->expr.elems[0]->sym_id,
                query->expr.len - 1u);
        key_query = petta_memo_quantized_ground_key(
            machine, query, precision);
        if (!key_query) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }
    CettaVarMap query_to_slot;
    CettaVarMap goal_instantiation;
    cetta_var_map_init(&query_to_slot);
    cetta_var_map_init(&goal_instantiation);
    Atom *canonical = variant_shape_canonicalize_atom(
        &machine->heap, key_query, &query_to_slot,
        &goal_instantiation, &kPettaTableVariantOptions);
    cetta_var_map_free(&query_to_slot);
    uint64_t hash = canonical
        ? (uint64_t)atom_hash(canonical) : 0u;
    if (!canonical) {
        cetta_var_map_free(&goal_instantiation);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (memoized)
        petta_table_memo_note_access(
            machine->table_shared, hash);
    if (!machine->table_shared ||
        machine->table_shared->failed) {
        cetta_var_map_free(&goal_instantiation);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (!petta_table_shared_epoch_current(
            machine->table_shared)) {
        cetta_var_map_free(&goal_instantiation);
        *failure = PETTA_MACHINE_STEP_INVALIDATED;
        return false;
    }

    size_t entry_index = PETTA_TABLE_ENTRY_NONE;
    bool inserted = false;
    if (!petta_table_find_or_insert(
            machine->table_shared, canonical,
            memoized && ground_query ? query : canonical,
            hash,
            &entry_index, &inserted) ||
        entry_index >= machine->table_shared->entry_len) {
        cetta_var_map_free(&goal_instantiation);
        machine->table_shared->failed = true;
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    PettaTableEntry *entry =
        &machine->table_shared->entries[entry_index];
    if (memoized) {
        if (!entry->memoized) {
            entry->memoized = true;
            entry->memo_head = query->expr.elems[0]->sym_id;
            entry->memo_arity = query->expr.len - 1u;
            if (UINT64_MAX -
                    machine->table_shared->memo_retained_bytes <
                entry->retained_bytes) {
                machine->table_shared->memo_retained_bytes =
                    UINT64_MAX;
            } else {
                machine->table_shared->memo_retained_bytes +=
                    entry->retained_bytes;
            }
            machine->table_shared->memo_policy_dirty = true;
        }
        entry->access_tick = machine->table_shared->access_tick;
    }
    if (entry->state == PETTA_TABLE_ENTRY_NEW &&
        !petta_table_begin_entry(
            machine->table_shared, entry_index)) {
        cetta_var_map_free(&goal_instantiation);
        machine->table_shared->failed = true;
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (!inserted)
        machine->stats.table_hits++;
    PettaMemoAggregateMode aggregate_mode =
        memoized && ground_query &&
                machine->host.memoized_relation_aggregate
            ? machine->host.memoized_relation_aggregate(
                  machine->host.context,
                  query->expr.elems[0]->sym_id,
                  query->expr.len - 1u)
            : PETTA_MEMO_AGGREGATE_NONE;
    uint32_t answer_limit =
        memoized && machine->host.memoized_relation_answer_limit
            ? machine->host.memoized_relation_answer_limit(
                  machine->host.context,
                  query->expr.elems[0]->sym_id,
                  query->expr.len - 1u)
            : UINT32_MAX;
    if (answer_limit == 0u)
        answer_limit = 1u;
    if (memoized &&
        machine->host.memoized_relation_observed) {
        machine->host.memoized_relation_observed(
            machine->host.context,
            query->expr.elems[0]->sym_id,
            query->expr.len - 1u, !inserted);
    }
    if (entry->state == PETTA_TABLE_ENTRY_EVALUATING) {
        petta_table_record_dependency(
            machine->table_shared,
            machine->table_generator, entry_index);
    }

    PettaChoice choice = {
        .kind = PETTA_CHOICE_TABLE,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.table = {
            .phase = inserted
                ? PETTA_TABLE_CHOICE_GENERATE_INITIAL
                : PETTA_TABLE_CHOICE_REPLAY,
            .requested_entry = entry_index,
            .parent_entry = machine->table_generator,
            .iteration_entry = entry_index,
            .scc_begin = PETTA_TABLE_ENTRY_NONE,
            .scc_cursor = PETTA_TABLE_ENTRY_NONE,
            .query = query,
            .expected = expected,
            .goal_instantiation = goal_instantiation,
            .aggregate_mode = aggregate_mode,
            .answer_limit = answer_limit,
            .memoized = memoized,
            .ground_query = ground_query,
        },
    };
    if (!petta_choice_push(machine, choice)) {
        machine->table_shared->failed = true;
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaChoice *stored =
        &machine->choices[machine->choice_len - 1u];
    if (petta_machine_resume_choice(
            machine, stored, failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    if (*failure == PETTA_MACHINE_STEP_SUSPENDED)
        return false;
    petta_choice_pop(machine);
    return false;
}

/*
 * A nested machine is a closure boundary.  Its query refers only to the
 * variables in `body`, so forward exactly their transitive logical
 * environment rather than cloning the parent's complete search history.
 * The shared projection retains connected constraints and orthogonal Prime
 * occurrence state; the parent keeps the authoritative full environment for
 * its continuation and choice points.
 */
static PettaMachine *petta_machine_new_child(
    PettaMachineImpl *machine, Atom *body,
    const PettaPlanNode *plan) {
    if (!machine || !body)
        return NULL;
    const Bindings *parent_environment =
        search_context_bindings(&machine->search);
    Atom *roots[] = {body};
    Bindings child_environment;
    machine->stats.child_machine_init_attempts++;
    uint64_t projection_started_ns = machine->host.measure_stats
        ? petta_machine_monotonic_ns() : 0u;
    if (!bindings_project_reachable(
            parent_environment, roots, 1u,
            &child_environment)) {
        return NULL;
    }
    petta_machine_add_u64(
        &machine->stats.child_machine_projected_entries,
        child_environment.len);
    uint64_t projection_finished_ns = projection_started_ns
        ? petta_machine_monotonic_ns() : 0u;
    if (projection_finished_ns >= projection_started_ns) {
        petta_machine_add_u64(
            &machine->stats.child_machine_projection_elapsed_ns,
            projection_finished_ns - projection_started_ns);
    }
    PettaMachine *inner = cetta_malloc(sizeof(*inner));
    uint64_t init_started_ns = machine->host.measure_stats
        ? petta_machine_monotonic_ns() : 0u;
    bool initialized = petta_machine_init_internal(
            inner, machine->space, &machine->heap, body,
            plan,
            &child_environment,
            &machine->host, true, machine->table_shared,
            false, false, false,
            machine->table_generator);
    uint64_t init_finished_ns = init_started_ns
        ? petta_machine_monotonic_ns() : 0u;
    if (init_finished_ns >= init_started_ns) {
        petta_machine_add_u64(
            &machine->stats.child_machine_init_elapsed_ns,
            init_finished_ns - init_started_ns);
    }
    bindings_free(&child_environment);
    if (!initialized) {
        free(inner);
        return NULL;
    }
    machine->stats.child_machine_init_successes++;
    return inner;
}

static bool petta_machine_start_collapse(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    if (!machine || !body || !expected || !failure) {
        if (failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaMachine *inner =
        petta_machine_new_child(machine, body, plan);
    if (!inner) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaChoice choice = {
        .kind = PETTA_CHOICE_COLLAPSE,
        .trail = search_context_save(&machine->search),
        .retain_heap_across_resume = true,
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.collapse = {
            .machine = inner,
            .expected = expected,
        },
    };
    if (!petta_choice_push(machine, choice)) {
        petta_machine_release_child(machine, &inner);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaChoice *stored =
        &machine->choices[machine->choice_len - 1u];
    if (petta_machine_resume_choice(
            machine, stored, failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    if (*failure == PETTA_MACHINE_STEP_SUSPENDED)
        return false;
    petta_choice_pop(machine);
    return false;
}

/*
 * COUNT(COLLAPSE body) is a fold over the exact collapse stream.  It uses the
 * same child machine as materializing collapse, including its logical-update
 * view, effects, answer order, and bag multiplicity, but retains only the
 * count.  The child exposes each resolved answer long enough to distinguish
 * PeTTa's empty result and does not copy the answer or its visible
 * environment into the parent arena.
 */
static bool petta_machine_start_count_collapse(
    PettaMachineImpl *machine, Atom *body, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan,
    bool wrap_collection, PettaMachineStep *failure) {
    if (!machine || !body || !expected || !failure) {
        if (failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaMachine *inner =
        petta_machine_new_child(machine, body, plan);
    if (!inner) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    inner->impl->count_only_emission = true;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_COUNT_COLLAPSE,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.count_collapse = {
            .machine = inner,
            .expected = expected,
            .wrap_collection = wrap_collection,
        },
    };
    if (!petta_choice_push(machine, choice)) {
        petta_machine_release_child(machine, &inner);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    PettaChoice *stored =
        &machine->choices[machine->choice_len - 1u];
    if (petta_machine_resume_choice(
            machine, stored, failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    if (*failure == PETTA_MACHINE_STEP_SUSPENDED)
        return false;
    petta_choice_pop(machine);
    return false;
}

static bool petta_machine_start_superpose(
    PettaMachineImpl *machine, Atom *items, Atom *expected,
    uint32_t barrier, const PettaPlanNode *items_plan) {
    if (!items || items->kind != ATOM_EXPR)
        return false;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_SUPERPOSE,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.superpose = {
            .items = items,
            .next = 0u,
            .expected = expected,
            .items_plan = items_plan,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    return false;
}

static bool petta_machine_start_boolean_choice(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier) {
    uint32_t arity = 0u;
    if (!expression || expression->kind != ATOM_EXPR ||
        !expected ||
        !petta_semantics_boolean_relation_arity(
            atom_head_symbol_id(expression), &arity) ||
        expression->expr.len != (CettaExprLen)arity + 1u) {
        return false;
    }
    bool inputs_determined = true;
    uint32_t determined_row = 0u;
    const Bindings *environment =
        search_context_bindings(&machine->search);
    for (uint32_t argument = 0u; argument < arity; argument++) {
        Atom *resolved = petta_machine_apply_bindings(
            machine, environment, &machine->heap,
            expression->expr.elems[argument + 1u]);
        bool value = false;
        if (!resolved ||
            !petta_semantics_truth_value(resolved, &value)) {
            inputs_determined = false;
            break;
        }
        if (!value)
            determined_row += argument == 0u ? 2u : 1u;
    }
    if (arity == 1u && inputs_determined)
        determined_row /= 2u;

    PettaChoice choice = {
        .kind = PETTA_CHOICE_BOOLEAN,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.boolean = {
            .expression = expression,
            .expected = expected,
            .next_row = inputs_determined ? determined_row : 0u,
            .inputs_determined = inputs_determined,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_start_append_choice(
    PettaMachineImpl *machine, Atom *whole, Atom *left, Atom *right,
    uint32_t barrier) {
    if (!whole || whole->kind != ATOM_EXPR)
        return false;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_APPEND,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.append = {
            .whole = whole,
            .next_split = 0u,
            .left = left,
            .right = right,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_start_member_choice(
    PettaMachineImpl *machine, Atom *needle, Atom *items,
    Atom *expected, uint32_t barrier) {
    if (!needle || !items || items->kind != ATOM_EXPR ||
        !expected) {
        return false;
    }
    PettaChoice choice = {
        .kind = PETTA_CHOICE_MEMBER,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.member = {
            .needle = needle,
            .items = items,
            .expected = expected,
            .next = 0u,
            .saw_match = false,
            .emitted_false = false,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_start_relational_member_choice(
    PettaMachineImpl *machine, Atom *needle, Atom *items,
    Atom *expected, uint32_t barrier) {
    if (!machine || !needle || !items || !expected)
        return false;
    PettaChoice choice = {
        .kind = PETTA_CHOICE_RELATIONAL_MEMBER,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.relational_member = {
            .needle = needle,
            .items = items,
            .expected = expected,
            .next_clause = 0u,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_start_list_length_choice(
    PettaMachineImpl *machine, Atom *list, Atom *expected,
    uint32_t barrier) {
    if (!machine || !list || !expected)
        return false;
    uint64_t prefix_length = 0u;
    Atom *open_tail = NULL;
    PettaListShape shape = petta_machine_list_shape(
        machine, list, &prefix_length, &open_tail);
    if (shape == PETTA_LIST_INVALID ||
        prefix_length > (uint64_t)INT64_MAX) {
        return false;
    }
    if (shape == PETTA_LIST_CLOSED) {
        return petta_machine_unify(
            machine, atom_int(
                &machine->heap, (int64_t)prefix_length),
            expected);
    }

    Atom *resolved_expected = petta_machine_apply_bindings(machine,
        search_context_bindings(&machine->search),
        &machine->heap, expected);
    if (!resolved_expected)
        return false;
    if (resolved_expected->kind == ATOM_GROUNDED &&
        resolved_expected->ground.gkind == GV_INT) {
        int64_t requested = resolved_expected->ground.ival;
        if (requested < 0 ||
            (uint64_t)requested < prefix_length) {
            return false;
        }
        Atom *suffix = petta_machine_fresh_list(
            machine, (uint64_t)requested - prefix_length);
        return suffix &&
               petta_machine_unify(
                   machine, open_tail, suffix);
    }

    PettaChoice choice = {
        .kind = PETTA_CHOICE_LIST_LENGTH,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.list_length = {
            .open_tail = open_tail,
            .expected = expected,
            .prefix_length = prefix_length,
            .next_suffix_length = 0u,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_collect_match_binding(
    const Bindings *bindings, void *context) {
    BindingSet *snapshot = context;
    return snapshot && bindings &&
           binding_set_push(snapshot, bindings);
}

static bool petta_machine_start_match_choice(
    PettaMachineImpl *machine, Space *space, Atom *pattern,
    Atom *template, Atom *expected, uint32_t barrier,
    const PettaPlanNode *template_plan) {
    if (!machine || !space || !pattern || !template || !expected)
        return false;
    (void)template_plan;
    bool terminal_count_fold =
        machine->count_only_emission &&
        machine->goal_len == 0u &&
        expected->kind == ATOM_VAR &&
        machine->answer_variable &&
        machine->answer_variable->kind == ATOM_VAR &&
        expected->var_id ==
            machine->answer_variable->var_id &&
        pattern->kind == ATOM_EXPR &&
        pattern->expr.len > 0u &&
        atom_eq(pattern, template);

    /*
     * COUNT over a terminal identity match needs neither substitutions nor
     * reconstructed rows.  `match` substitutes its template as data; it does
     * not evaluate that template, so the source planner's value/call role is
     * irrelevant here (including a variable relation head).  Resolve the
     * pattern once, then let the backend admit only an exact fragment.  A
     * declined pattern falls through to the ordinary choice/unification path
     * unchanged.
     */
    if (terminal_count_fold) {
        const Bindings *environment =
            search_context_bindings(&machine->search);
        Atom *resolved_pattern = petta_machine_apply_bindings(machine,
            environment, &machine->heap, pattern);
        uint64_t count = 0u;
        CettaIndex examined = 0u;
        if (resolved_pattern &&
            space_match_count_flat_linear64(
                space, &machine->heap, resolved_pattern,
                &count, &examined) &&
            count <= (uint64_t)INT64_MAX) {
            machine->stats.match_candidates += examined;
            if (count == 0u)
                return false;
            size_t goal_mark = machine->goal_len;
            size_t trail_mark = machine->goal_trail_len;
            if (!petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind =
                            PETTA_GOAL_SET_ANSWER_WEIGHT,
                        .barrier = barrier,
                        .answer_weight = count,
                    }) ||
                !petta_push_unify(
                    machine, resolved_pattern,
                    expected, barrier)) {
                (void)petta_goal_trail_rollback(
                    machine, trail_mark);
                machine->goal_len = goal_mark;
                return false;
            }
            machine->stats.count_aggregate_match_folds++;
            machine->stats.count_aggregate_match_answers += count;
            return true;
        }
    }

    /*
     * A PeTTa match has Prolog's logical-update view: its alternatives are
     * the occurrences visible when the call begins.  Effects in one answer
     * may change what a later match sees, but may neither delete a pending
     * alternative nor append a new one to this choice point.  Snapshotting
     * occurrence references also preserves duplicates and declaration order.
     *
     * The shared match backend's candidate list is a complete superset: it
     * includes the pattern bucket and every stored-side wildcard bucket, in
     * logical occurrence order.  Snapshot only those occurrences rather than
     * the whole space.  The ordinary unifier below remains the semantic
     * authority, so a false-positive candidate is harmless while a backend
     * that omitted a possible match would be caught by the indexed-OFF
     * differential gates.
     */
    Atom **snapshot = NULL;
    BindingSet binding_snapshot;
    binding_set_init(&binding_snapshot);
    bool binding_snapshot_mode = false;

    /* A counted PathMap cursor already owns the logical-update snapshot and
     * returns exact query bindings with occurrence multiplicity.  Keep those
     * bindings as the choice evidence instead of materializing a second
     * native AtomId sequence plus discrimination trie.  The ordinary atom
     * snapshot remains the semantic oracle for a declined query shape. */
    if (space->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !space->match_backend.pathmap.bridge.preserve_logical_order) {
        const Bindings *environment =
            search_context_bindings(&machine->search);
        Atom *resolved_pattern = petta_machine_apply_bindings(
            machine, environment, &machine->heap, pattern);
        if (!resolved_pattern) {
            binding_set_free(&binding_snapshot);
            return false;
        }
        SpaceMatchPullVisitResult indexed =
            space_match_backend_try_visit_bindings_indexed(
                space, &machine->heap, resolved_pattern,
                petta_machine_collect_match_binding,
                &binding_snapshot);
        if (indexed == SPACE_MATCH_PULL_VISIT_COMPLETE) {
            binding_snapshot_mode = true;
        } else if (indexed == SPACE_MATCH_PULL_VISIT_DECLINED &&
                   space_match_backend_visit_bindings_direct(
                       space, &machine->heap, resolved_pattern,
                       petta_machine_collect_match_binding,
                       &binding_snapshot)) {
            binding_snapshot_mode = true;
        } else {
            binding_set_free(&binding_snapshot);
            binding_set_init(&binding_snapshot);
        }
    }

    CettaIndex *candidate_indices = NULL;
    CettaIndex candidate_len = binding_snapshot_mode
        ? 0u
        : space_match_candidates64(
              space, pattern, &candidate_indices);
    if (candidate_len >
        (CettaCount)(SIZE_MAX / sizeof(*snapshot))) {
        free(candidate_indices);
        return false;
    }
    CettaCount snapshot_len = 0u;
    if (candidate_len > 0u) {
        snapshot = cetta_malloc(
            sizeof(*snapshot) * (size_t)candidate_len);
        for (CettaIndex index = 0u; index < candidate_len; index++) {
            Atom *candidate = space_match_candidate_at64(
                space, candidate_indices[index]);
            if (candidate)
                snapshot[snapshot_len++] = candidate;
        }
    }
    free(candidate_indices);
    PettaChoice choice = {
        .kind = PETTA_CHOICE_MATCH,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.match = {
            .space = space,
            .read = space_read_token(space),
            .next_index = 0u,
            .snapshot = snapshot,
            .snapshot_len = snapshot_len,
            .binding_snapshot = binding_snapshot,
            .binding_snapshot_mode = binding_snapshot_mode,
            .snapshot_mode = true,
            .pattern = pattern,
            .template = template,
            .expected = expected,
            .terminal_count_fold = terminal_count_fold,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static bool petta_machine_typed_signature_is_natively_refuted(
    PettaMachineImpl *machine, Atom *expression, Atom *type);

static bool petta_machine_start_typed_call_choice(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    Atom **types, uint32_t count, uint32_t barrier,
    const PettaPlanNode *plan) {
    if (!machine || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u || !expected || !types ||
        count == 0u) {
        free(types);
        return false;
    }
    CettaExprLen nargs = expression->expr.len - 1u;
    uint32_t applicable = 0u;
    for (uint32_t index = 0u; index < count; index++) {
        if (petta_machine_type_signature_applies(types[index], nargs))
            types[applicable++] = types[index];
    }
    bool overload_dispatch = applicable > 1u;
    uint32_t retained = 0u;
    for (uint32_t index = 0u; index < applicable; index++) {
        if (!overload_dispatch ||
            !petta_machine_typed_signature_is_natively_refuted(
                machine, expression, types[index])) {
            types[retained++] = types[index];
        }
    }
    count = retained;
    if (count == 0u) {
        free(types);
        return false;
    }
    if (count == 1u) {
        Atom *type = types[0];
        free(types);
        return petta_machine_schedule_typed_call(
            machine, expression, expected, type,
            barrier, false, plan);
    }
    PettaChoice choice = {
        .kind = PETTA_CHOICE_TYPED_CALL,
        .trail = search_context_save(&machine->search),
        .goal_height = machine->goal_len,
        .barrier = barrier,
        .as.typed_call = {
            .types = types,
            .count = count,
            .next = 0u,
            .overload_dispatch = overload_dispatch,
            .expression = expression,
            .expected = expected,
            .plan = plan,
        },
    };
    if (!petta_choice_push(machine, choice))
        return false;
    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (petta_machine_resume_choice(
            machine, &machine->choices[machine->choice_len - 1u],
            &failure)) {
        petta_choice_retire_exhausted(machine);
        return true;
    }
    petta_choice_pop(machine);
    if (failure != PETTA_MACHINE_STEP_EXHAUSTED) {
        machine->terminal = true;
        machine->terminal_step = failure;
    }
    return false;
}

static PettaAnalysisCallable petta_machine_analysis_callable(
    void *context, Atom *value, CettaExprLen arity) {
    PettaMachineImpl *machine = context;
    if (!machine || !value)
        return PETTA_ANALYSIS_CALLABLE_UNKNOWN;
    if (value->kind == ATOM_VAR)
        return PETTA_ANALYSIS_CALLABLE_UNKNOWN;
    if (value->kind != ATOM_SYMBOL)
        return PETTA_ANALYSIS_CALLABLE_NO;

    CettaExprLen minimum = 0u;
    CettaExprLen maximum = 0u;
    bool equation_exact = false;
    bool found_equation = space_equation_head_arity_bounds(
        machine->space, value->sym_id, &minimum, &maximum,
        &equation_exact, arity);
    CettaExprLen intrinsic_arity = 0u;
    bool intrinsic_exact =
        petta_semantics_intrinsic_partial_arity(
            value->sym_id, &intrinsic_arity) &&
        intrinsic_arity == arity;
    bool extension_exact =
        petta_machine_extension_named_arity(
            machine, value->sym_id, arity).exact;
    (void)minimum;
    (void)maximum;
    if (!(found_equation && equation_exact) &&
        !intrinsic_exact && !extension_exact) {
        return PETTA_ANALYSIS_CALLABLE_NO;
    }

    /* The revision-keyed memo is the declaration authority for typed
     * callable values.  An executable but untyped head is conservatively
     * unknown; it is never rejected merely for lacking an annotation. */
    return space_head_has_arrow_signature(
               machine->space, value->sym_id, arity)
        ? PETTA_ANALYSIS_CALLABLE_YES
        : PETTA_ANALYSIS_CALLABLE_UNKNOWN;
}

static void petta_machine_record_typecheck_failure(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    const PettaAnalysisResult *result, int exit_code) {
    if (!machine || machine->typecheck_exit_code != 0)
        return;
    ArenaMark mark = arena_mark(&machine->heap);
    char *value_text = atom_to_string(&machine->heap, value);
    char *formal_text = atom_to_string(&machine->heap, formal);
    const char *kind = exit_code == 2
        ? "PeTTa type error" : "PeTTa typechecker fault";
    const char *reason = "invalid";
    if (result && machine->host.analysis &&
        machine->host.analysis->reason_name) {
        const char *provided = machine->host.analysis->reason_name(
            machine->host.context, result->reason);
        if (provided)
            reason = provided;
    }
    snprintf(
        machine->typecheck_diagnostic,
        sizeof(machine->typecheck_diagnostic),
        "%s: value %s does not satisfy %s (%s)",
        kind, value_text ? value_text : "<unprintable>",
        formal_text ? formal_text : "<unprintable>", reason);
    arena_reset(&machine->heap, mark);
    machine->typecheck_exit_code = exit_code;
}

static bool petta_machine_judge_native_type(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    PettaAnalysisResult *result) {
    if (!machine || !value || !formal || !result ||
        !machine->host.analysis ||
        !machine->host.analysis->judge_value)
        return false;
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(
        &scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    bool judged = machine->host.analysis->judge_value(
        machine->host.context, machine->space, &scratch,
        value, formal, petta_machine_analysis_callable,
        machine, result);
    arena_free(&scratch);
    return judged;
}

static bool petta_machine_typecheck_value_ready(
    PettaMachineImpl *machine, Atom *value);

/* A closed direct judgment can reject an overload before the machine
 * allocates and later rolls back that branch.  This is deliberately only a
 * ground prefilter: dependent/open formals, executable arguments, runtime
 * classifiers, incomplete judgments, and changed authority state all retain
 * the original goal-scheduling route. */
static bool petta_machine_typed_signature_is_natively_refuted(
    PettaMachineImpl *machine, Atom *expression, Atom *type) {
    if (!cetta_nik_typed_applicability_pruning_enabled() ||
        !petta_machine_type_obligations_enabled(machine) ||
        !machine->space ||
        !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        !petta_machine_type_signature_applies(
            type, expression->expr.len - 1u)) {
        return false;
    }

    CettaExprLen length = expression->expr.len;
    for (CettaExprIndex index = 1u; index < length; index++) {
        Atom *binder = NULL;
        Atom *formal = NULL;
        (void)petta_machine_split_dependent_domain(
            type->expr.elems[index], &binder, &formal);
        Atom *value = expression->expr.elems[index];
        if (binder || !formal || atom_has_vars(formal) ||
            petta_machine_analysis_has_runtime_classifier(
                machine, formal) ||
            !value || atom_is_error(value) || atom_has_vars(value) ||
            !petta_machine_typecheck_value_ready(machine, value)) {
            return false;
        }
    }

    uint64_t authority_epoch = space_global_mutation_epoch();
    uint32_t authority_policy = petta_machine_analysis_policy(machine);
    SpaceReadToken authority_read = space_read_token(machine->space);
    PettaMachineAuthorityToken authority_before;
    if (!petta_machine_authority_token(machine, &authority_before))
        return false;

    bool refuted = false;
    for (CettaExprIndex index = 1u; index < length; index++) {
        Atom *formal = NULL;
        (void)petta_machine_split_dependent_domain(
            type->expr.elems[index], NULL, &formal);
        if (petta_machine_type_is_atom_data(formal) ||
            petta_machine_type_is_unconstrained(formal)) {
            continue;
        }
        PettaAnalysisResult result = {0};
        if (!petta_machine_judge_native_type(
                machine, expression->expr.elems[index], formal, &result) ||
            result.fault != PETTA_ANALYSIS_FAULT_NONE ||
            result.verdict == PETTA_ANALYSIS_INCOMPLETE) {
            return false;
        }
        if (result.verdict == PETTA_ANALYSIS_REFUTED)
            refuted = true;
    }

    PettaMachineAuthorityToken authority_after;
    if (!petta_machine_authority_token(machine, &authority_after) ||
        space_global_mutation_epoch() != authority_epoch ||
        petta_machine_analysis_policy(machine) != authority_policy ||
        !space_read_token_matches_live_space(
            authority_read, machine->space) ||
        !petta_machine_authority_token_eq(
            &authority_before, &authority_after)) {
        return false;
    }
    if (refuted) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TYPED_DISPATCH_SIGNATURE_REFUTED);
    }
    return refuted;
}

static bool petta_machine_add_type_obligation(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    uint32_t barrier) {
    if (!machine || !value || !formal)
        return false;
    if (machine->next_type_obligation_id == UINT64_MAX)
        return false;
    if (!petta_machine_reserve(
            (void **)&machine->type_obligations,
            &machine->type_obligation_cap,
            machine->type_obligation_len + 1u,
            sizeof(*machine->type_obligations))) {
        return false;
    }
    machine->type_obligations[machine->type_obligation_len++] =
        (PettaTypeObligation){
            .id = ++machine->next_type_obligation_id,
            .value = value,
            .formal = formal,
            .barrier = barrier,
            .checked_value = NULL,
            .checked_formal = NULL,
            .state = PETTA_TYPE_OBLIGATION_UNCHECKED,
        };
    machine->type_obligation_check_pending = true;
    return true;
}

static Atom *petta_machine_type_obligation_for_value(
    PettaMachineImpl *machine, Atom *value) {
    if (!machine || !value)
        return NULL;
    const Bindings *environment =
        search_context_bindings(&machine->search);
    Atom *resolved_value =
        petta_machine_resolve_root(environment, value);
    for (size_t index = machine->type_obligation_len;
         index > 0u; index--) {
        PettaTypeObligation *obligation =
            &machine->type_obligations[index - 1u];
        Atom *resolved_subject = petta_machine_resolve_root(
            environment, obligation->value);
        bool same = resolved_value && resolved_subject &&
            (resolved_value == resolved_subject ||
             (resolved_value->kind == ATOM_VAR &&
              resolved_subject->kind == ATOM_VAR &&
              resolved_value->var_id == resolved_subject->var_id));
        if (!same)
            continue;
        return petta_machine_apply_bindings(
            machine, environment, &machine->heap,
            obligation->formal);
    }
    return NULL;
}

static bool petta_machine_raise_typecheck_error(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    const PettaAnalysisResult *result) {
    if (!machine || !value || !formal || !result ||
        !machine->host.analysis ||
        !machine->host.analysis->error_atom)
        return false;
    ArenaMark mark = arena_mark(&machine->heap);
    char *value_text = atom_to_string(&machine->heap, value);
    char *formal_text = atom_to_string(&machine->heap, formal);
    char diagnostic[512];
    const char *reason = "invalid";
    if (machine->host.analysis->reason_name) {
        const char *provided = machine->host.analysis->reason_name(
            machine->host.context, result->reason);
        if (provided)
            reason = provided;
    }
    snprintf(
        diagnostic, sizeof(diagnostic),
        "value %s does not satisfy %s (%s)",
        value_text ? value_text : "<unprintable>",
        formal_text ? formal_text : "<unprintable>",
        reason);
    arena_reset(&machine->heap, mark);
    Atom *error = machine->host.analysis->error_atom(
        machine->host.context, &machine->heap,
        value, 2, diagnostic);
    return error && petta_machine_raise_error(machine, error);
}

static PettaTypeObligation *petta_machine_type_obligation_by_id(
    PettaMachineImpl *machine, uint64_t id) {
    if (!machine || id == 0u)
        return NULL;
    for (size_t index = 0u;
         index < machine->type_obligation_len; index++) {
        if (machine->type_obligations[index].id == id)
            return &machine->type_obligations[index];
    }
    return NULL;
}

static bool petta_machine_type_guard_in_flight(
    const PettaMachineImpl *machine, uint64_t obligation_id) {
    if (!machine || obligation_id == 0u)
        return false;
    for (size_t index = machine->choice_len;
         index > 0u; index--) {
        const PettaChoice *choice = &machine->choices[index - 1u];
        if (choice->kind == PETTA_CHOICE_EQUAL_DEFAULT &&
            choice->as.equal_default.type_obligation_id ==
                obligation_id) {
            return true;
        }
    }
    return false;
}

/* Schedule the ordinary committed get-type protocol.  For a nonzero
 * obligation identity, the fallback choice is also the trailed in-flight
 * marker and carries the authority captured before relational search. */
static bool petta_machine_schedule_committed_type_guard(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    uint32_t barrier, uint64_t obligation_id,
    SpaceReadToken authority_read, uint64_t authority_epoch,
    uint32_t authority_policy,
    const PettaMachineAuthorityToken *authority) {
    if (!machine || !value || !formal ||
        (obligation_id != 0u && !authority)) {
        return false;
    }
    Atom *type_call = atom_expr2(
        &machine->heap,
        atom_symbol_id(
            &machine->heap, g_builtin_syms.get_type),
        value);
    Atom *matched = petta_fresh_variable(machine);
    Atom *truth = machine->host.boolean_value
        ? machine->host.boolean_value(
              machine->host.context, &machine->heap, true)
        : petta_semantics_boolean_value(&machine->heap, true);
    if (!type_call || !matched || !truth)
        return false;
    if (obligation_id != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TYPE_OBLIGATION_GUARD_SCHEDULED);
    }

    size_t fallback_index = machine->choice_len;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_TYPE_REQUIRE_READY,
                .barrier = barrier,
                .first = matched,
                .second = value,
                .third = formal,
                .choice_index = fallback_index,
            })) {
        return false;
    }
    PettaChoice fallback = {0};
    fallback.kind = PETTA_CHOICE_EQUAL_DEFAULT;
    fallback.trail = search_context_save(&machine->search);
    fallback.goal_height = machine->goal_len;
    fallback.barrier = barrier;
    fallback.as.equal_default.saw_answer = false;
    fallback.as.equal_default.expected = matched;
    fallback.as.equal_default.type_obligation_id = obligation_id;
    fallback.as.equal_default.guarded_value = value;
    fallback.as.equal_default.guarded_formal = formal;
    fallback.as.equal_default.authority_read = authority_read;
    fallback.as.equal_default.authority_epoch = authority_epoch;
    fallback.as.equal_default.authority_policy = authority_policy;
    if (authority)
        fallback.as.equal_default.authority = *authority;
    if (!petta_choice_push(machine, fallback) ||
        !petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_EQUAL_COMMIT,
                .barrier = barrier,
                .first = truth,
                .second = matched,
                .choice_index = fallback_index,
            }) ||
        !petta_push_solve(
            machine, type_call, formal, barrier)) {
        return false;
    }
    return true;
}

/*
 * PeTTa's open type requirements are attributed-variable constraints in the
 * reference.  The machine representation is an append-only obligation set:
 * every successful goal that may have changed logical bindings revisits it,
 * and choice/catch rollback truncates it to the matching environment mark.
 * ESTABLISHED obligations may be rechecked; retaining them avoids a second
 * mutable trail and keeps rollback exact.
 */
static bool petta_machine_check_type_obligations(
    PettaMachineImpl *machine, PettaMachineStep *failure) {
    if (!machine || !failure)
        return false;
    if (machine->raised_error || machine->type_obligation_len == 0u)
        return true;
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    uint64_t current_epoch = space_global_mutation_epoch();
    uint32_t current_policy =
        petta_machine_analysis_policy(machine);
    PettaMachineAuthorityToken current_authority;
    if (!petta_machine_authority_token(
            machine, &current_authority)) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    if (!machine->type_obligation_check_pending &&
        builder->growth_count != UINT64_MAX &&
        machine->type_obligation_checked_growth ==
            builder->growth_count &&
        machine->type_obligation_checked_epoch == current_epoch &&
        machine->type_obligation_checked_policy == current_policy &&
        petta_machine_authority_token_eq(
            &machine->type_obligation_checked_authority,
            &current_authority) &&
        space_read_token_matches_live_space(
            machine->type_obligation_checked_read,
            machine->space)) {
        return true;
    }
    for (uint32_t attempt = 0u; attempt < 2u; attempt++) {
        uint64_t authority_epoch = space_global_mutation_epoch();
        uint32_t authority_policy =
            petta_machine_analysis_policy(machine);
        SpaceReadToken authority_read =
            space_read_token(machine->space);
        PettaMachineAuthorityToken authority_token;
        if (!petta_machine_authority_token(
                machine, &authority_token)) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        bool restart = false;
        const Bindings *environment =
            search_context_bindings(&machine->search);
        for (size_t index = 0u;
             index < machine->type_obligation_len; index++) {
            PettaTypeObligation *live =
                &machine->type_obligations[index];
            /* The fallback choice, not the derived receipt cache, owns the
             * lifetime of an active relational search.  Serializing these
             * committed guards prevents a later obligation from nesting a
             * second search inside the first guard's alternatives. */
            if (petta_machine_type_guard_in_flight(
                    machine, live->id)) {
                machine->type_obligation_check_pending = true;
                return true;
            }
            PettaTypeObligation obligation = *live;
            Atom *value = petta_machine_apply_bindings(
                machine, environment, &machine->heap,
                obligation.value);
            Atom *formal = petta_machine_apply_bindings(
                machine, environment, &machine->heap,
                obligation.formal);
            if (!value || !formal) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (live->state != PETTA_TYPE_OBLIGATION_UNCHECKED &&
                live->checked_value && live->checked_formal &&
                live->checked_epoch == authority_epoch &&
                live->checked_policy == authority_policy &&
                petta_machine_authority_token_eq(
                    &live->checked_authority,
                    &authority_token) &&
                space_read_token_matches_live_space(
                    live->checked_read, machine->space) &&
                atom_eq(live->checked_value, value) &&
                atom_eq(live->checked_formal, formal)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PETTA_TYPE_OBLIGATION_CACHE_HIT);
                continue;
            }
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PETTA_TYPE_OBLIGATION_CACHE_MISS);
            bool value_ready =
                petta_machine_typecheck_value_ready(machine, value);
            bool relational_required = value_ready &&
                (live->relational_required ||
                 petta_machine_analysis_has_runtime_classifier(
                     machine, formal));
            PettaAnalysisResult result = {0};
            bool judged = true;
            if (value_ready && !relational_required) {
                judged = petta_machine_judge_native_type(
                    machine, value, formal, &result);
            }
            PettaMachineAuthorityToken confirmed_authority;
            if (!petta_machine_authority_token(
                    machine, &confirmed_authority)) {
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
            if (space_global_mutation_epoch() != authority_epoch ||
                petta_machine_analysis_policy(machine) !=
                    authority_policy ||
                !petta_machine_authority_token_eq(
                    &authority_token, &confirmed_authority) ||
                !space_read_token_matches_live_space(
                    authority_read, machine->space)) {
                restart = true;
                break;
            }
            if (!judged) {
                petta_machine_record_typecheck_failure(
                    machine, value, formal, &result, 1);
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
            if (value_ready &&
                result.verdict == PETTA_ANALYSIS_REFUTED) {
                if (!petta_machine_raise_typecheck_error(
                        machine, value, formal, &result)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                return true;
            }
            if (value_ready &&
                result.verdict == PETTA_ANALYSIS_INCOMPLETE) {
                petta_machine_record_typecheck_failure(
                    machine, value, formal, &result, 1);
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
            if (relational_required) {
                live->relational_required = true;
                live->state = PETTA_TYPE_OBLIGATION_UNCHECKED;
                live->checked_value = NULL;
                live->checked_formal = NULL;
                /* Native absence of proof is not evidence.  Delegate to
                 * PeTTa's ordinary committed get-type relation and retain
                 * the fallback choice as the trailed in-flight marker. */
                if (!petta_machine_schedule_committed_type_guard(
                        machine, value, formal, obligation.barrier,
                        obligation.id, authority_read,
                        authority_epoch, authority_policy,
                        &authority_token)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                machine->type_obligation_check_pending = true;
                return true;
            }
            /* An open value is deferred control state.  Native ESTABLISHED
             * is an exact proof; UNDETERMINED without an authored runtime
             * classifier is only gradual/dynamic compatibility.  Preserve
             * that weaker state explicitly so it can never authorize exact
             * optimizer facts. */
            live->state = !value_ready
                ? PETTA_TYPE_OBLIGATION_OPEN_VALUE
                : result.verdict == PETTA_ANALYSIS_ESTABLISHED
                    ? PETTA_TYPE_OBLIGATION_NATIVE_ESTABLISHED
                    : PETTA_TYPE_OBLIGATION_DYNAMIC_COMPATIBLE;
            live->checked_value = value;
            live->checked_formal = formal;
            live->checked_read = authority_read;
            live->checked_epoch = authority_epoch;
            live->checked_policy = authority_policy;
            live->checked_authority = authority_token;
        }
        if (restart) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PETTA_TYPE_OBLIGATION_AUTHORITY_RETRY);
            petta_machine_invalidate_type_obligation_cache(machine);
            continue;
        }
        machine->type_obligation_check_pending = false;
        machine->type_obligation_checked_growth = builder->growth_count;
        machine->type_obligation_checked_read = authority_read;
        machine->type_obligation_checked_epoch = authority_epoch;
        machine->type_obligation_checked_policy = authority_policy;
        machine->type_obligation_checked_authority = authority_token;
        return true;
    }
    petta_machine_invalidate_type_obligation_cache(machine);
    *failure = PETTA_MACHINE_STEP_INVALIDATED;
    return false;
}

static bool petta_machine_typecheck_value_ready(
    PettaMachineImpl *machine, Atom *value) {
    if (!machine || !value)
        return false;
    if (value->kind != ATOM_EXPR)
        return value->kind != ATOM_VAR;
    if (value->expr.len == 0u)
        return true;

    SymbolId head = atom_head_symbol_id(value);
    CettaExprLen nargs = value->expr.len - 1u;
    if (head != SYMBOL_ID_NONE) {
        /* These are source computations whose result has not yet reached
         * this seam.  A failed/unsupported computation may remain printed
         * as syntax, but that does not turn it into a concrete value that
         * the residual checker may refute.  An arrow declaration without a
         * live equation is instead PeTTa's constructor convention and is
         * rigid data, not a pending call. */
        CettaExprLen minimum = 0u;
        CettaExprLen maximum = 0u;
        bool exact = false;
        bool reducible = space_equation_head_arity_bounds(
            machine->space, head, &minimum, &maximum,
            &exact, nargs) && exact;
        (void)minimum;
        (void)maximum;
        if (head == g_builtin_syms.petta_make_list || reducible) {
            return false;
        }
        if (head == g_builtin_syms.colon ||
            space_head_has_arrow_signature(
                machine->space, head, nargs)) {
            return true;
        }
    }
    if (atom_has_vars(value))
        return false;
    return petta_machine_is_rigid_data(machine, value);
}

static bool petta_machine_get_type_has_exact_extension(
    PettaMachineImpl *machine) {
    if (!machine || !machine->space)
        return false;
    CettaExprLen minimum = 0u;
    CettaExprLen maximum = 0u;
    bool exact = false;
    bool present = space_equation_head_arity_bounds(
        machine->space, g_builtin_syms.get_type,
        &minimum, &maximum, &exact, 1u);
    (void)minimum;
    (void)maximum;
    return present && exact;
}

static bool petta_machine_type_accept(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    uint32_t barrier, bool match_only,
    PettaMachineStep *failure) {
    if (!machine || !value || !formal)
        return false;
    if (atom_is_error(value)) {
        /*
         * Error is normally control, but Roman's type language can name it
         * as data inside a union.  Consult the declared formal before
         * propagating it; only an ESTABLISHED judgment suppresses the
         * ordinary error path.
         */
        if (petta_machine_type_obligations_enabled(machine)) {
            PettaAnalysisResult result = {0};
            if (!petta_machine_judge_native_type(
                    machine, value, formal, &result)) {
                petta_machine_record_typecheck_failure(
                    machine, value, formal, &result, 1);
                if (failure)
                    *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
            if (result.verdict == PETTA_ANALYSIS_ESTABLISHED)
                return true;
        }
        if (match_only)
            return false;
        bool raised = petta_machine_raise_error(machine, value);
        if (!raised && failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return raised;
    }
    /* TYPE_ACCEPT is also used while a typed clause result is still an
     * executable source expression.  The residual checker owns settled
     * values only: rejecting a callable/intermediate expression here would
     * turn an evaluation obligation into a false type error.  Rigid data
     * expressions (including literal lists and tuples) are safe to inspect;
     * everything else remains with the existing relational get-type path. */
    bool native_value_ready =
        petta_machine_typecheck_value_ready(machine, value);
    bool runtime_classified_formal =
        petta_machine_type_obligations_enabled(machine) &&
        petta_machine_analysis_has_runtime_classifier(
            machine, formal);
    if (petta_machine_type_obligations_enabled(machine) &&
        native_value_ready && !runtime_classified_formal) {
        PettaAnalysisResult result = {0};
        bool judged = petta_machine_judge_native_type(
            machine, value, formal, &result);
        if (!judged) {
            petta_machine_record_typecheck_failure(
                machine, value, formal, &result, 1);
            if (failure)
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        if (result.verdict == PETTA_ANALYSIS_REFUTED) {
            if (match_only)
                return false;
            petta_machine_record_typecheck_failure(
                machine, value, formal, &result, 2);
            if (failure)
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        /* A closed native proof is authoritative.  Open or otherwise
         * undetermined judgments continue through relational get-type below,
         * preserving overload bindings and logical choicepoints. */
        if (result.verdict == PETTA_ANALYSIS_ESTABLISHED)
            return true;
    }
    if (petta_machine_type_obligations_enabled(machine) &&
        atom_has_vars(value)) {
        if (match_only)
            return true;
        if (!petta_machine_add_type_obligation(
                machine, value, formal, barrier)) {
            if (failure)
                *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (atom_is_symbol_id(
            formal, g_builtin_syms.undefined_type)) {
        return true;
    }
    if (atom_is_meta_type(formal))
        return atom_meta_type_accepts(
            &machine->heap, formal, value);

    /* TYPE_ACCEPT already owns the ready-value boundary.  When no authored
     * clause extends `get-type`, inserting a SOLVE call only redispatches the
     * same intrinsic service through another machine transition.  A root
     * logical variable is itself a valid relational type subject; callable
     * expressions still take the ordinary evaluation path. */
    if (machine->host.get_type && failure &&
        (value->kind == ATOM_VAR || native_value_ready) &&
        !petta_machine_get_type_has_exact_extension(machine)) {
        return petta_machine_start_intrinsic_get_type(
            machine, value, formal, barrier, failure);
    }

    /*
     * User types are a PeTTa relation, not merely a static annotation lookup.
     * Route the check through get-type so its intrinsic candidate clause and
     * explicit (= (get-type ...)) extensions share ordinary ordered
     * backtracking.  This is also what lets a formal type variable receive
     * the selected actual type through the normal trail.
     */
    if (!runtime_classified_formal || match_only) {
        Atom *type_call = atom_expr2(
            &machine->heap,
            atom_symbol_id(
                &machine->heap, g_builtin_syms.get_type),
            value);
        if (!type_call)
            return false;
        return petta_push_solve(
            machine, type_call, formal, barrier);
    }

    /* Roman's typecheck_or_error is committed: a successful relational
     * classifier satisfies one ordinary contract, while exhaustion raises a
     * type error.  Overload TYPE_MATCH deliberately keeps the non-throwing
     * solve above so another signature may be tried. */
    return petta_machine_schedule_committed_type_guard(
        machine, value, formal, barrier, 0u,
        (SpaceReadToken){0}, 0u, 0u, NULL);
}

static bool petta_machine_type_ascribe_ready(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    uint32_t barrier, PettaMachineStep *failure) {
    if (!machine || !value || !formal || !failure)
        return false;
    PettaAnalysisResult result = {0};
    if (!petta_machine_judge_native_type(
            machine, value, formal, &result)) {
        petta_machine_record_typecheck_failure(
            machine, value, formal, &result, 1);
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    if (result.verdict == PETTA_ANALYSIS_ESTABLISHED)
        return true;
    if (result.verdict == PETTA_ANALYSIS_REFUTED) {
        if (!petta_machine_raise_typecheck_error(
                machine, value, formal, &result)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (result.verdict == PETTA_ANALYSIS_INCOMPLETE) {
        petta_machine_record_typecheck_failure(
            machine, value, formal, &result, 1);
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    if (!petta_machine_add_type_obligation(
            machine, value, formal, barrier)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return true;
}

static bool petta_machine_append(
    PettaMachineImpl *machine, Atom *left, Atom *right,
    Atom *expected, uint32_t barrier) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    left = petta_machine_apply_bindings(machine,
        environment, &machine->heap, left);
    right = petta_machine_apply_bindings(machine,
        environment, &machine->heap, right);
    expected = petta_machine_apply_bindings(machine,
        environment, &machine->heap, expected);
    if (!left || !right || !expected)
        return false;

    bool left_open =
        petta_semantics_is_open_cons_value(left);
    bool right_open =
        petta_semantics_is_open_cons_value(right);

    /*
     * Append operates on PeTTa's logical list, not on the physical
     * expression that happens to carry it.  In particular an open-cons node
     * is one list cell; treating its three implementation fields as tuple
     * elements leaks the carrier tag and creates a spurious answer.
     */
    if (left_open) {
        Atom *result_tail = petta_fresh_variable(machine);
        Atom *result = result_tail
            ? petta_semantics_open_cons_value(
                  &machine->heap, left->expr.elems[1],
                  result_tail)
            : NULL;
        Atom *append = atom_symbol(&machine->heap, "append");
        Atom *continuation =
            append
                ? atom_expr3(
                      &machine->heap, append,
                      left->expr.elems[2], right)
                : NULL;
        if (!result_tail || !result || !continuation ||
            !petta_machine_unify(machine, result, expected)) {
            return false;
        }
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_APPEND_READY,
                .barrier = barrier,
                .first = continuation,
                .second = result_tail,
            });
    }

    if (left->kind == ATOM_EXPR && right_open) {
        Atom *result = right;
        for (CettaExprIndex index = left->expr.len;
             index > 0u; index--) {
            result = petta_semantics_open_cons_value(
                &machine->heap,
                left->expr.elems[index - 1u], result);
            if (!result)
                return false;
        }
        return petta_machine_unify(machine, result, expected);
    }

    if (left->kind == ATOM_EXPR &&
        right->kind == ATOM_EXPR && !right_open) {
        if (left->expr.len >
                UINT64_MAX - right->expr.len) {
            return false;
        }
        CettaExprLen length =
            left->expr.len + right->expr.len;
        if (
            !cetta_expr_len_mul_fits_size(
                length, sizeof(Atom *))) {
            return false;
        }
        Atom **items = length
            ? arena_alloc(
                  &machine->heap,
                  sizeof(*items) * (size_t)length)
            : NULL;
        if (length && !items)
            return false;
        if (left->expr.len) {
            memcpy(
                items, left->expr.elems,
                sizeof(*items) * (size_t)left->expr.len);
        }
        if (right->expr.len) {
            memcpy(
                items + left->expr.len, right->expr.elems,
                sizeof(*items) * (size_t)right->expr.len);
        }
        return petta_machine_unify(
            machine,
            atom_expr(&machine->heap, items, length),
            expected);
    }
    if (expected->kind == ATOM_EXPR) {
        if (petta_semantics_is_open_cons_value(expected)) {
            Atom *materialized =
                petta_machine_materialize_list(
                    machine, expected);
            if (!materialized || materialized == expected)
                return false;
            expected = materialized;
        }
        return petta_machine_start_append_choice(
            machine, expected, left, right, barrier);
    }
    return false;
}

static bool petta_machine_cons(
    PettaMachineImpl *machine, Atom *head, Atom *tail,
    Atom *expected, uint32_t barrier) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    head = petta_machine_apply_bindings(machine,
        environment, &machine->heap, head);
    tail = petta_machine_apply_bindings(machine,
        environment, &machine->heap, tail);
    expected = petta_machine_apply_bindings(machine,
        environment, &machine->heap, expected);
    if (!head || !tail || !expected)
        return false;

    if (petta_semantics_is_open_cons_value(expected)) {
        return petta_push_solve(
                   machine, tail, expected->expr.elems[2],
                   barrier) &&
               petta_push_solve(
                   machine, head, expected->expr.elems[1],
                   barrier);
    }
    if (expected->kind == ATOM_EXPR) {
        if (expected->expr.len == 0u)
            return false;
        Atom *spine = petta_semantics_flat_list_spine(
            &machine->heap, expected);
        if (!petta_semantics_is_open_cons_value(spine) ||
            !petta_push_solve(
                machine, tail, spine->expr.elems[2], barrier) ||
            !petta_push_solve(
                machine, head, spine->expr.elems[1], barrier)) {
            return false;
        }
        return true;
    }

    /*
     * Keep constructor results as shared cons cells inside the relational
     * machine.  Flattening here would copy a tail of length k at every
     * recursive return and turn an otherwise linear map/range into
     * 1 + ... + N retained element pointers.  Observable answers are
     * materialized once by petta_machine_materialize_answer.
     */
    Atom *constraint =
        petta_semantics_open_cons_value(
            &machine->heap, head, tail);
    return constraint &&
           petta_machine_unify(
               machine, constraint, expected);
}

/*
 * PeTTa's map-atom is a relational list traversal, not an HE stdlib call.
 * Every element application remains a machine goal, so mapper
 * nondeterminism forms the ordinary depth-first product and duplicate
 * answers remain observable.  The three-argument form carries an explicit
 * lexical binder and body; the two-argument form applies a callable value.
 */
static bool petta_machine_map_atom(
    PettaMachineImpl *machine, Atom *items, Atom *mapper_or_binder,
    Atom *body, Atom *expected, uint32_t barrier,
    const Bindings *environment) {
    if (!machine || !items || !mapper_or_binder || !expected ||
        (body && mapper_or_binder->kind != ATOM_VAR)) {
        return false;
    }
    items = petta_machine_closed_list(machine, items);
    if (!items ||
        !cetta_expr_len_mul_fits_size(
            items->expr.len, sizeof(Atom *))) {
        return false;
    }

    if (body && machine->host.map_single_result) {
        Atom *prepared_result = NULL;
        PettaMachineFoldResult prepared =
            machine->host.map_single_result(
                machine->host.context, machine->space,
                &machine->heap, items, mapper_or_binder, body,
                environment, &prepared_result);
        if (prepared == PETTA_MACHINE_FOLD_VALUE) {
            return prepared_result &&
                petta_push_unify(
                    machine, prepared_result, expected, barrier);
        }
        if (prepared == PETTA_MACHINE_FOLD_INTERRUPTED)
            return true;
    }

    Atom **mapped_items = items->expr.len > 0u
        ? arena_alloc(
              &machine->heap,
              sizeof(*mapped_items) * (size_t)items->expr.len)
        : NULL;
    for (CettaExprIndex index = 0u;
         index < items->expr.len; index++) {
        mapped_items[index] = petta_fresh_variable(machine);
        if (!mapped_items[index])
            return false;
    }
    Atom *mapped = atom_expr(
        &machine->heap, mapped_items, items->expr.len);
    if (!mapped ||
        !petta_push_unify(
            machine, mapped, expected, barrier)) {
        return false;
    }

    for (CettaExprIndex index = items->expr.len;
         index > 0u; index--) {
        CettaExprIndex item_index = index - 1u;
        Atom *application = NULL;
        if (!body) {
            application = atom_expr2(
                &machine->heap, mapper_or_binder,
                items->expr.elems[item_index]);
        } else {
            Bindings substitution;
            bindings_init(&substitution);
            bool bound = bindings_add_id(
                &substitution, mapper_or_binder->var_id,
                mapper_or_binder->sym_id,
                items->expr.elems[item_index]);
            application = bound
                ? petta_machine_apply_bindings(machine,
                      &substitution, &machine->heap, body)
                : NULL;
            bindings_free(&substitution);
        }
        if (!application ||
            !petta_push_solve(
                machine, application, mapped_items[item_index],
                barrier)) {
            return false;
        }
    }
    return true;
}

/*
 * PeTTa has two foldl-atom syntax forms:
 *
 *   (foldl-atom Items Initial AccVar ItemVar Body)
 *   (foldl-atom Items Initial Callable)
 *
 * The list and initial accumulator are demanded before this function.  Each
 * step remains an ordinary machine goal, so step nondeterminism branches in
 * depth-first order, failures prune only their branch, and suspension/cut
 * behavior is inherited from the same continuation stack as every other
 * relation.  The shared binder helper performs lexical binder substitution
 * and freshens step-local variables independently at every iteration.
 */
static bool petta_machine_foldl_atom(
    PettaMachineImpl *machine, Atom *items, Atom *initial,
    Atom *expression, Atom *expected, uint32_t barrier,
    bool item_first, const Bindings *environment) {
    if (!machine || !items || !initial || !expression || !expected ||
        expression->kind != ATOM_EXPR ||
        (expression->expr.len != 4u &&
         expression->expr.len != 6u)) {
        return false;
    }
    items = petta_machine_closed_list(machine, items);
    if (!items ||
        items->expr.len == UINT64_MAX ||
        !cetta_expr_len_mul_fits_size(
            items->expr.len + 1u, sizeof(Atom *))) {
        return false;
    }

    bool lexical = expression->expr.len == 6u;
    Atom *accumulator_binder =
        lexical ? expression->expr.elems[3] : NULL;
    Atom *item_binder =
        lexical ? expression->expr.elems[4] : NULL;
    Atom *step =
        lexical ? expression->expr.elems[5]
                : expression->expr.elems[3];
    if (!step ||
        (lexical &&
         (!accumulator_binder || !item_binder ||
          accumulator_binder->kind != ATOM_VAR ||
          item_binder->kind != ATOM_VAR))) {
        return false;
    }

    if (lexical && machine->host.foldl_single_result) {
        Atom *prepared_result = NULL;
        PettaMachineFoldResult prepared =
            machine->host.foldl_single_result(
                machine->host.context, machine->space,
                &machine->heap, items, initial,
                accumulator_binder, item_binder, step,
                environment, &prepared_result);
        if (prepared == PETTA_MACHINE_FOLD_VALUE) {
            return prepared_result &&
                petta_push_unify(
                    machine, prepared_result, expected, barrier);
        }
        if (prepared == PETTA_MACHINE_FOLD_INTERRUPTED)
            return true;
    }

    Atom **accumulators = arena_alloc(
        &machine->heap,
        sizeof(*accumulators) *
            (size_t)(items->expr.len + 1u));
    if (!accumulators)
        return false;
    accumulators[0] = initial;
    for (CettaExprIndex index = 0u;
         index < items->expr.len; index++) {
        accumulators[index + 1u] =
            petta_fresh_variable(machine);
        if (!accumulators[index + 1u])
            return false;
    }

    if (!petta_push_unify(
            machine, accumulators[items->expr.len],
            expected, barrier)) {
        return false;
    }
    for (CettaExprIndex index = items->expr.len;
         index > 0u; index--) {
        CettaExprIndex item_index = index - 1u;
        Atom *application = lexical
            ? cetta_fold_bind_step_atom(
                  &machine->heap, step,
                  accumulator_binder,
                  accumulators[item_index],
                  item_binder,
                  items->expr.elems[item_index])
            : item_first
                ? atom_expr3(
                      &machine->heap, step,
                      items->expr.elems[item_index],
                      accumulators[item_index])
                : atom_expr3(
                      &machine->heap, step,
                      accumulators[item_index],
                      items->expr.elems[item_index]);
        if (!application ||
            !petta_push_solve(
                machine, application,
                accumulators[item_index + 1u],
                barrier)) {
            return false;
        }
    }
    return true;
}

/*
 * PeTTa filter-atom is an existential predicate test per item: retain the
 * item iff some condition answer is `true`, commit after the first such
 * answer, and omit the item when the condition exhausts.  Lower that law to
 * the machine's own delimited once/case/let controls.  This keeps effects,
 * answer order, suspension, and branch rollback under one search engine
 * instead of implementing a second hidden evaluator.
 */
static bool petta_machine_filter_atom(
    PettaMachineImpl *machine, Atom *items, Atom *mapper_or_binder,
    Atom *body, Atom *expected, uint32_t barrier) {
    if (!machine || !items || !mapper_or_binder || !expected ||
        (body && mapper_or_binder->kind != ATOM_VAR)) {
        return false;
    }
    items = petta_machine_closed_list(machine, items);
    if (!items)
        return false;

    Atom *rest = atom_expr(&machine->heap, NULL, 0u);
    Atom *truth = machine->host.boolean_value
        ? machine->host.boolean_value(
              machine->host.context, &machine->heap, true)
        : petta_semantics_boolean_value(&machine->heap, true);
    Atom *append = atom_symbol(&machine->heap, "append");
    Atom *let = atom_symbol(&machine->heap, "let");
    Atom *empty_pattern = atom_symbol_id(
        &machine->heap, g_builtin_syms.empty);
    if (!rest || !truth || !append || !let || !empty_pattern)
        return false;

    for (CettaExprIndex index = items->expr.len;
         index > 0u; index--) {
        CettaExprIndex item_index = index - 1u;
        Atom *item = items->expr.elems[item_index];
        Atom *condition = body
            ? cetta_fold_bind_step_atom(
                  &machine->heap, body,
                  NULL, NULL, mapper_or_binder, item)
            : atom_expr2(
                  &machine->heap, mapper_or_binder, item);
        Atom *singleton_items[1] = {item};
        Atom *singleton = atom_expr(
            &machine->heap, singleton_items, 1u);
        Atom *quoted = singleton
            ? atom_expr2(
                  &machine->heap,
                  atom_symbol_id(
                      &machine->heap, g_builtin_syms.quote),
                  singleton)
            : NULL;
        Atom *include = quoted
            ? atom_expr3(
                  &machine->heap, append, quoted, rest)
            : NULL;
        Atom *let_items[4] = {
            let, condition, truth, truth,
        };
        Atom *predicate = condition
            ? atom_expr(&machine->heap, let_items, 4u)
            : NULL;
        Atom *first_true = predicate
            ? atom_expr2(
                  &machine->heap,
                  atom_symbol_id(
                      &machine->heap, g_builtin_syms.once),
                  predicate)
            : NULL;
        Atom *true_branch = include
            ? atom_expr2(&machine->heap, truth, include)
            : NULL;
        Atom *empty_branch = atom_expr2(
            &machine->heap, empty_pattern, rest);
        Atom *branch_items[2] = {
            true_branch, empty_branch,
        };
        Atom *branches =
            true_branch && empty_branch
                ? atom_expr(&machine->heap, branch_items, 2u)
                : NULL;
        rest =
            first_true && branches
                ? atom_expr3(
                      &machine->heap,
                      atom_symbol_id(
                          &machine->heap,
                          g_builtin_syms.case_text),
                      first_true, branches)
                : NULL;
        if (!rest)
            return false;
    }
    return petta_push_solve(
        machine, rest, expected, barrier);
}

static bool petta_machine_integer_value(
    const Atom *atom, int64_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || !value) {
        return false;
    }
    *value = atom->ground.ival;
    return true;
}

/*
 * The expected-result slot is the third place of PeTTa's integer-addition
 * relation.  Any two known integers determine the third; fewer than two
 * belong to the residual-constraint layer and produce no premature answer.
 */
static bool petta_machine_integer_add(
    PettaMachineImpl *machine, Atom *left, Atom *right,
    Atom *expected) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    left = petta_machine_apply_bindings(machine,
        environment, &machine->heap, left);
    right = petta_machine_apply_bindings(machine,
        environment, &machine->heap, right);
    expected = petta_machine_apply_bindings(machine,
        environment, &machine->heap, expected);
    if (!left || !right || !expected)
        return false;

    int64_t left_value = 0;
    int64_t right_value = 0;
    int64_t expected_value = 0;
    bool left_known =
        petta_machine_integer_value(left, &left_value);
    bool right_known =
        petta_machine_integer_value(right, &right_value);
    bool expected_known =
        petta_machine_integer_value(expected, &expected_value);
    int64_t solved = 0;

    if (left_known && right_known) {
        if (__builtin_add_overflow(
                left_value, right_value, &solved)) {
            return false;
        }
        return petta_machine_unify(
            machine, atom_int(&machine->heap, solved), expected);
    }
    if (left_known && expected_known) {
        if (__builtin_sub_overflow(
                expected_value, left_value, &solved)) {
            return false;
        }
        return petta_machine_unify(
            machine, right, atom_int(&machine->heap, solved));
    }
    if (right_known && expected_known) {
        if (__builtin_sub_overflow(
                expected_value, right_value, &solved)) {
            return false;
        }
        return petta_machine_unify(
            machine, left, atom_int(&machine->heap, solved));
    }
    return false;
}

static bool petta_machine_all_arguments_immediate(
    Atom *expression, const PettaPlanNode *plan) {
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u) {
        return false;
    }
    for (CettaExprIndex index = 1u;
         index < expression->expr.len; index++) {
        const PettaPlanNode *argument_plan =
            petta_plan_child(plan, index);
        if (!petta_machine_immediate_value(
                expression->expr.elems[index],
                argument_plan) ||
            petta_semantics_value_contains_observable_open_cons(
                expression->expr.elems[index])) {
            return false;
        }
    }
    return true;
}

/* A relation-slot plan proves that no authored argument occurrence contains
 * a call.  Recheck the resolved values against the live presentation before
 * eliding strict argument continuations: a DATA subtree may have acquired a
 * newly callable name since compilation, while a VALUE occurrence retains
 * value authority even when its substituted syntax resembles a call. */
static bool petta_machine_relation_slots_ready(
    PettaMachineImpl *machine, Atom *expression,
    const PettaPlanNode *plan) {
    if (!machine || !expression || !plan ||
        plan->execution != PETTA_PLAN_EXEC_RELATION_SLOTS ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        plan->child_count != expression->expr.len) {
        return false;
    }
    for (CettaExprIndex index = 1u;
         index < expression->expr.len; index++) {
        const PettaPlanNode *argument_plan =
            petta_plan_child(plan, index);
        if (!argument_plan || argument_plan->contains_call ||
            !petta_machine_planned_value_ready(
                machine, expression->expr.elems[index],
                argument_plan)) {
            return false;
        }
    }
    return true;
}

typedef struct {
    Atom *atom;
    bool executable;
} PettaCountUseWork;

static bool petta_atom_contains_var_id(
    Atom *root, VarId variable) {
    if (!root || variable == VAR_ID_NONE)
        return false;
    Atom **stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    if (!petta_machine_reserve(
            (void **)&stack, &capacity, 1u,
            sizeof(*stack))) {
        return true;
    }
    stack[length++] = root;
    while (length > 0u) {
        Atom *atom = stack[--length];
        if (atom->kind == ATOM_VAR &&
            atom->var_id == variable) {
            free(stack);
            return true;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            Atom *child = atom->expr.elems[index];
            if (child->kind == ATOM_VAR &&
                child->var_id == variable) {
                free(stack);
                return true;
            }
        }
        if ((uint64_t)atom->expr.len >
                (uint64_t)(SIZE_MAX - length) ||
            !petta_machine_reserve(
                (void **)&stack, &capacity,
                length + (size_t)atom->expr.len,
                sizeof(*stack))) {
            free(stack);
            return true;
        }
        for (CettaExprIndex index = atom->expr.len;
             index > 0u; index--) {
            Atom *child = atom->expr.elems[index - 1u];
            if (child->kind != ATOM_VAR)
                stack[length++] = child;
        }
    }
    free(stack);
    return false;
}

static bool petta_count_use_children_executable(
    Atom *expression) {
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return true;
    }
    SymbolId head = expression->expr.elems[0]->sym_id;
    PeTTaForm form = petta_semantics_form(head);
    return head != g_builtin_syms.quote &&
           head != g_builtin_syms.return_text &&
           form != PETTA_FORM_LAMBDA &&
           form != PETTA_FORM_PREDICATE;
}

/*
 * Admit collection-count fusion only when every observable occurrence of
 * `variable` is the complete argument of an executable `length` call.
 * Occurrences under quoted/callable data are not observations of the list
 * and therefore reject the optimization rather than leaking its private
 * counted carrier.
 */
static bool petta_count_use_scan(
    Atom *root, VarId variable, uint64_t *uses) {
    if (!root || variable == VAR_ID_NONE || !uses)
        return false;
    PettaCountUseWork *work = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    if (!petta_machine_reserve(
            (void **)&work, &capacity, 1u,
            sizeof(*work))) {
        return false;
    }
    work[length++] = (PettaCountUseWork){
        .atom = root,
        .executable = true,
    };
    while (length > 0u) {
        PettaCountUseWork item = work[--length];
        Atom *atom = item.atom;
        if (atom->kind == ATOM_VAR &&
            atom->var_id == variable) {
            free(work);
            return false;
        }
        if (atom->kind != ATOM_EXPR)
            continue;

        bool direct_length =
            item.executable &&
            atom->expr.len == 2u &&
            atom->expr.elems[0]->kind == ATOM_SYMBOL &&
            petta_semantics_form(
                atom->expr.elems[0]->sym_id) ==
                    PETTA_FORM_LENGTH &&
            atom->expr.elems[1]->kind == ATOM_VAR &&
            atom->expr.elems[1]->var_id == variable;
        if (direct_length) {
            (*uses)++;
            continue;
        }

        /*
         * A direct occurrence outside the admitted length position rejects
         * fusion immediately.  Check all shallow children before descending
         * into any potentially large substituted sibling.
         */
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            Atom *child = atom->expr.elems[index];
            if (child->kind == ATOM_VAR &&
                child->var_id == variable) {
                free(work);
                return false;
            }
        }
        bool executable =
            item.executable &&
            petta_count_use_children_executable(atom);
        if ((uint64_t)atom->expr.len >
                (uint64_t)(SIZE_MAX - length) ||
            !petta_machine_reserve(
                (void **)&work, &capacity,
                length + (size_t)atom->expr.len,
                sizeof(*work))) {
            free(work);
            return false;
        }
        for (CettaExprIndex index = atom->expr.len;
             index > 0u; index--) {
            Atom *child = atom->expr.elems[index - 1u];
            if (child->kind == ATOM_VAR)
                continue;
            work[length++] = (PettaCountUseWork){
                .atom = child,
                .executable = executable,
            };
        }
    }
    free(work);
    return true;
}

static bool petta_environment_var_is_private(
    const Bindings *environment, VarId variable) {
    if (!environment || variable == VAR_ID_NONE)
        return false;
    for (uint32_t index = 0u;
         index < environment->len; index++) {
        const Binding *binding = &environment->entries[index];
        if (binding->var_id == variable ||
            petta_atom_contains_var_id(
                binding->val, variable)) {
            return false;
        }
    }
    for (uint32_t index = 0u;
         index < environment->eq_len; index++) {
        const BindingConstraint *constraint =
            &environment->constraints[index];
        if (petta_atom_contains_var_id(
                constraint->lhs, variable) ||
            petta_atom_contains_var_id(
                constraint->rhs, variable)) {
            return false;
        }
    }
    return true;
}

static bool petta_let_binding_count_only(
    PettaMachineImpl *machine, Atom *bindings,
    const PettaPlanNode *bindings_plan,
    CettaExprIndex binding_index, Atom *body,
    const PettaPlanNode *body_plan) {
    if (!petta_let_count_fusion_enabled() ||
        !machine || !bindings ||
        bindings->kind != ATOM_EXPR ||
        binding_index >= bindings->expr.len || !body) {
        return false;
    }
    bool may_contain_length =
        body_plan && body_plan->contains_length_call;
    for (CettaExprIndex index = binding_index + 1u;
         !may_contain_length &&
         bindings_plan &&
         index < bindings_plan->child_count; index++) {
        may_contain_length =
            bindings_plan->children[index]
                .contains_length_call;
    }
    if (!may_contain_length)
        return false;

    Atom *binding = bindings->expr.elems[binding_index];
    if (!binding || binding->kind != ATOM_EXPR ||
        binding->expr.len != 2u ||
        binding->expr.elems[0]->kind != ATOM_VAR) {
        return false;
    }
    VarId variable = binding->expr.elems[0]->var_id;
    if (variable == VAR_ID_NONE ||
        petta_atom_contains_var_id(
            binding->expr.elems[1], variable)) {
        return false;
    }

    uint64_t uses = 0u;
    for (CettaExprIndex index = binding_index + 1u;
         index < bindings->expr.len; index++) {
        if (!petta_count_use_scan(
                bindings->expr.elems[index],
                variable, &uses)) {
            return false;
        }
    }
    return petta_count_use_scan(body, variable, &uses) &&
           uses > 0u &&
           petta_environment_var_is_private(
               search_context_bindings(&machine->search),
               variable);
}

static bool petta_direct_let_binding_count_only(
    PettaMachineImpl *machine, Atom *binder, Atom *producer,
    Atom *body, const PettaPlanNode *body_plan) {
    if (!petta_let_count_fusion_enabled() || !machine || !binder ||
        !producer || !body || binder->kind != ATOM_VAR ||
        binder->var_id == VAR_ID_NONE ||
        (body_plan && !body_plan->contains_length_call) ||
        petta_atom_contains_var_id(producer, binder->var_id)) {
        return false;
    }
    uint64_t uses = 0u;
    return petta_count_use_scan(body, binder->var_id, &uses) &&
           uses > 0u &&
           petta_environment_var_is_private(
               search_context_bindings(&machine->search),
               binder->var_id);
}

typedef struct {
    uint64_t count;
} PettaCollectionCardinality;

static bool petta_collection_count_item(
    void *context, Atom *item) {
    PettaCollectionCardinality *cardinality = context;
    (void)item;
    if (!cardinality || cardinality->count >= (uint64_t)INT64_MAX)
        return false;
    cardinality->count++;
    return true;
}

static bool petta_push_collection_count_fallback(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, const PettaPlanNode *plan) {
    Atom *value = petta_fresh_variable(machine);
    return value &&
           petta_goal_push(
               machine,
               (PettaGoal){
                   .kind = PETTA_GOAL_COLLECTION_COUNT_READY,
                   .barrier = barrier,
                   .first = value,
                   .second = expected,
               }) &&
           petta_push_solve_planned(
               machine, expression, value, barrier, plan);
}

static bool petta_machine_dispatch_counted_collection(
    PettaMachineImpl *machine, const PettaGoal *goal,
    Atom *expression, Atom *expected,
    const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    if (!machine || !goal || !expression || !expected ||
        !failure) {
        if (failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    int64_t counted = 0;
    if (atom_petta_counted_collection_count(
            expression, &counted)) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    bool source_value =
        (plan && plan->role == PETTA_PLAN_VALUE) ||
        (goal->first && goal->first->kind == ATOM_VAR);
    if (source_value ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u) {
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_COLLECTION_COUNT_READY,
                .barrier = goal->barrier,
                .first = expression,
                .second = expected,
            });
    }

    if (machine->host.pull_collection_single_result) {
        PettaCollectionCardinality cardinality = {0};
        PettaMachineFoldResult pulled =
            machine->host.pull_collection_single_result(
                machine->host.context, machine->space,
                expression,
                search_context_bindings(&machine->search),
                petta_collection_count_item, &cardinality);
        if (pulled == PETTA_MACHINE_FOLD_VALUE) {
            Atom *collection = atom_petta_counted_collection(
                &machine->heap, (int64_t)cardinality.count);
            return collection &&
                   petta_machine_unify_resolved(
                       machine, collection, expected);
        }
        if (pulled == PETTA_MACHINE_FOLD_INTERRUPTED)
            return true;
    }

    SymbolId head = atom_head_symbol_id(expression);
    PeTTaForm form = head == SYMBOL_ID_NONE
        ? PETTA_FORM_NONE : petta_semantics_form(head);
    CettaExprLen nargs = expression->expr.len - 1u;
    if (petta_machine_is_reify_head(machine, head) && nargs == 1u) {
        return petta_machine_start_count_collapse(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u),
            true, failure);
    }
    if ((form == PETTA_FORM_ID ||
         form == PETTA_FORM_CATCH) &&
        nargs == 1u) {
        return petta_push_counted_collection_planned(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u));
    }

    bool ordinary_clause_call =
        head != SYMBOL_ID_NONE &&
        form == PETTA_FORM_NONE &&
        (!plan || plan->role != PETTA_PLAN_DATA) &&
        !petta_machine_extension_callable(machine, expression) &&
        space_equations_may_match_known_head(
            machine->space, head);
    if (ordinary_clause_call) {
        PettaPartialDecision partial =
            petta_machine_named_partial_decision(
                machine, expression);
        if (partial == PETTA_PARTIAL_INVALIDATED) {
            *failure = PETTA_MACHINE_STEP_INVALIDATED;
            return false;
        }
        if (partial == PETTA_PARTIAL_OVERAPPLIED) {
            if (!petta_push_evaluated_expression_planned(
                    machine, expression, expected,
                    PETTA_GOAL_OVERAPPLICATION_READY,
                    1u, goal->barrier, plan)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        if (partial == PETTA_PARTIAL_NO) {
            Atom **types = NULL;
            uint32_t type_count =
                space_get_declared_types(
                    machine->space, &machine->heap,
                    expression->expr.elems[0], &types);
            free(types);
            PettaMachineHostMode host_mode =
                machine->host.classify
                    ? machine->host.classify(
                          machine->host.context,
                          machine->space, expression)
                    : PETTA_MACHINE_HOST_NONE;
            if (type_count == 0u &&
                host_mode == PETTA_MACHINE_HOST_NONE) {
                return petta_push_evaluated_expression_planned(
                    machine, expression, expected,
                    PETTA_GOAL_CALL_READY_COUNTED_COLLECTION,
                    1u, goal->barrier, plan);
            }
        }
    }

    return petta_push_collection_count_fallback(
        machine, goal->first, goal->second,
        goal->barrier, plan);
}

static bool petta_machine_start_predicate_template(
    PettaMachineImpl *machine, Atom *predicate_body,
    Atom *template, Atom *expected, uint32_t barrier,
    PettaMachineStep *failure) {
    Atom *predicate_symbol = atom_symbol(
        &machine->heap, "Predicate");
    Atom *call_symbol = atom_symbol(
        &machine->heap, "callPredicate");
    Atom *ignored = petta_fresh_variable(machine);
    Atom *predicate_value = predicate_symbol && predicate_body
        ? atom_expr2(
              &machine->heap, predicate_symbol, predicate_body)
        : NULL;
    Atom *call = call_symbol && predicate_value
        ? atom_expr2(
              &machine->heap, call_symbol, predicate_value)
        : NULL;
    Atom *let_symbol = atom_symbol_id(
        &machine->heap, g_builtin_syms.let);
    Atom *lowered = call && let_symbol && ignored
        ? atom_expr(
              &machine->heap,
              (Atom *[]){
                  let_symbol, ignored, call, template,
              },
              4u)
        : NULL;
    if (!lowered) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return petta_push_solve(
        machine, lowered, expected, barrier);
}

static bool petta_machine_start_ready_match(
    PettaMachineImpl *machine, Atom *reference, Atom *pattern,
    Atom *template, Atom *expected, uint32_t barrier,
    const PettaPlanNode *template_plan,
    PettaMachineStep *failure) {
    if (!machine || !reference || !pattern || !template ||
        !expected || !failure) {
        return false;
    }

    Atom *conjunction = petta_machine_lower_match_conjunction(
        machine, reference, pattern, template);
    if (conjunction)
        return petta_push_solve(
            machine, conjunction, expected, barrier);

    Space *target = machine->host.resolve_space
        ? machine->host.resolve_space(
              machine->host.context, machine->space,
              &machine->heap, reference)
        : NULL;
    if (!target && reference->kind == ATOM_SYMBOL &&
        reference->sym_id == g_builtin_syms.self) {
        target = machine->space;
    }

    /* A variable pattern enumerates every row regardless of relation arity.
     * This is the reference's get-atoms clause, invoked through the existing
     * Predicate boundary so its binding returns to the machine trail. */
    if (!target && reference->kind == ATOM_SYMBOL &&
        pattern->kind == ATOM_VAR) {
        Atom *get_atoms = atom_symbol_id(
            &machine->heap, g_builtin_syms.get_atoms);
        Atom *predicate_body = get_atoms
            ? atom_expr3(
                  &machine->heap, get_atoms, reference, pattern)
            : NULL;
        return predicate_body &&
               petta_machine_start_predicate_template(
                   machine, predicate_body, template, expected,
                   barrier, failure);
    }

    /* A plain-symbol space is represented by a Prolog predicate in the
     * reference implementation.  Preserve its clauses and substitutions
     * through the existing foreign predicate boundary. */
    if (!target && reference->kind == ATOM_SYMBOL &&
        pattern->kind == ATOM_EXPR && pattern->expr.len > 0u) {
        CettaExprLen row_len = pattern->expr.len;
        PeTTaNamedArity space_arity =
            petta_machine_extension_named_arity_resolving(
                machine, reference->sym_id, row_len - 1u);
        if (!space_arity.known)
            return false;
        Atom *cons_symbol = atom_symbol(&machine->heap, "cons");
        if (!cons_symbol) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom *predicate_body = atom_expr3(
            &machine->heap, cons_symbol, reference, pattern);
        return predicate_body &&
               petta_machine_start_predicate_template(
                   machine, predicate_body, template, expected,
                   barrier, failure);
    }
    if (!target)
        return false;

    Atom *match_pattern = pattern;
    Atom *match_template = template;
    if (!petta_machine_elaborate_typed_match_pattern(
            machine, reference,
            &match_pattern, &match_template)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return petta_machine_start_match_choice(
        machine, target, match_pattern, match_template,
        expected, barrier, template_plan);
}

/*
 * A constant atomic template makes `once(match ...)` an existence observer:
 * collapse returns either one nonempty value or the empty collection.  Fold
 * that observer into exact membership only when Space proves that ground
 * matching and structural membership coincide.  Stored variables, typed
 * pattern elaboration, computed/error/empty templates, and backend queries
 * requiring a native projection all retain the ordinary machine path.
 */
static bool petta_machine_try_match_existence_observer(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    bool *out_handled) {
    if (out_handled)
        *out_handled = false;
    if (!machine || !expression || !expected || !out_handled ||
        !petta_match_existence_fusion_enabled() ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len != 3u ||
        !atom_is_symbol_id(
            expression->expr.elems[0], g_builtin_syms.op_eq)) {
        return false;
    }
    Atom *observed = NULL;
    if (expression->expr.elems[1] &&
        expression->expr.elems[1]->kind == ATOM_EXPR &&
        expression->expr.elems[1]->expr.len == 0u)
        observed = expression->expr.elems[2];
    else if (expression->expr.elems[2] &&
             expression->expr.elems[2]->kind == ATOM_EXPR &&
             expression->expr.elems[2]->expr.len == 0u)
        observed = expression->expr.elems[1];
    if (!observed || observed->kind != ATOM_EXPR ||
        observed->expr.len != 2u ||
        !petta_machine_atom_is_reify_head(
            machine, observed->expr.elems[0])) {
        return false;
    }

    Atom *once = observed->expr.elems[1];
    if (!once || once->kind != ATOM_EXPR ||
        once->expr.len != 2u ||
        !atom_is_symbol_id(
            once->expr.elems[0], g_builtin_syms.once)) {
        return false;
    }

    Atom *match = once->expr.elems[1];
    if (!match || match->kind != ATOM_EXPR ||
        match->expr.len != 4u ||
        !atom_is_symbol_id(
            match->expr.elems[0], g_builtin_syms.match)) {
        return false;
    }

    Atom *reference = match->expr.elems[1];
    Atom *pattern = match->expr.elems[2];
    Atom *template = match->expr.elems[3];
    bool constant_template =
        template && !atom_has_vars(template) && !atom_is_error(template) &&
        (template->kind == ATOM_SYMBOL ||
         (template->kind == ATOM_GROUNDED &&
          term_universe_atom_is_stable(template)));
    if (!reference || !pattern || !constant_template ||
        petta_machine_named_space_has_schema(machine, reference)) {
        return false;
    }

    Space *target = machine->host.resolve_space
        ? machine->host.resolve_space(
              machine->host.context, machine->space,
              &machine->heap, reference)
        : NULL;
    if (!target && reference->kind == ATOM_SYMBOL &&
        reference->sym_id == g_builtin_syms.self) {
        target = machine->space;
    }
    if (!target)
        return false;

    bool applicable = false;
    bool found = space_match_exists_ground_exact(
        target, pattern, &applicable);
    if (!applicable)
        return false;

    machine->stats.match_existence_observer_folds++;
    *out_handled = true;
    return petta_machine_boolean(machine, !found, expected);
}

static bool petta_machine_dispatch_solve(
    PettaMachineImpl *machine, const PettaGoal *goal,
    PettaMachineStep *failure);

static bool petta_machine_prepare_equation_frame(
        PettaMachineImpl *machine,
        const PettaEquationTemplate *equation_template,
        uint32_t activation_epoch,
        uint32_t activation_first_entry,
        Bindings **bindings_out,
        BindingsDenseEpochFrame **frame_out) {
    if (bindings_out)
        *bindings_out = NULL;
    if (frame_out)
        *frame_out = NULL;
    if (!machine || !bindings_out || !frame_out ||
        !petta_space_query_executable_enabled() ||
        !equation_template || activation_epoch == 0u) {
        return false;
    }
    Bindings *bindings = (Bindings *)search_context_bindings(
        &machine->search);
    if (!bindings || activation_first_entry > bindings->len)
        return false;
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    if (!builder || builder->growth_count == UINT64_MAX ||
        builder->rollback_count == UINT64_MAX)
        return false;
    PettaActivationFrameCacheEntry *refresh = NULL;
    for (uint32_t index = 0u;
         index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
        PettaActivationFrameCacheEntry *entry =
            &machine->activation_frames[index];
        if (entry->equation_template != equation_template ||
            entry->activation_epoch != activation_epoch ||
            entry->activation_first_entry != activation_first_entry) {
            continue;
        }
        if (entry->frame.builder == builder &&
            entry->frame.builder_instance != 0u &&
            entry->frame.builder_instance == builder->instance_id &&
            entry->frame.binding_growth == builder->growth_count &&
            entry->frame.binding_rollbacks == builder->rollback_count &&
            entry->frame.scanned_len == bindings->len) {
            *bindings_out = bindings;
            *frame_out = &entry->frame;
            return true;
        }
        if (entry->frame.builder == builder &&
            entry->frame.builder_instance != 0u &&
            entry->frame.builder_instance == builder->instance_id &&
            entry->frame.binding_rollbacks == builder->rollback_count &&
            entry->frame.binding_growth < builder->growth_count &&
            entry->frame.scanned_len <= bindings->len &&
            (!refresh ||
             entry->frame.binding_growth >
                 refresh->frame.binding_growth)) {
            refresh = entry;
        }
    }
    if (refresh && bindings_dense_epoch_frame_refresh(
            &refresh->frame, builder)) {
        *bindings_out = bindings;
        *frame_out = &refresh->frame;
        return true;
    }

    PettaActivationFrameCacheEntry *selected = NULL;
    for (uint32_t index = 0u;
         index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
        PettaActivationFrameCacheEntry *entry =
            &machine->activation_frames[index];
        if (entry->equation_template == equation_template &&
            entry->activation_epoch == activation_epoch &&
            entry->activation_first_entry == activation_first_entry) {
            selected = entry;
            break;
        }
    }
    if (!selected) {
        for (uint32_t index = 0u;
             index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
            PettaActivationFrameCacheEntry *entry =
                &machine->activation_frames[index];
            if (!entry->equation_template) {
                selected = entry;
                break;
            }
        }
    }
    if (!selected) {
        uint32_t replacement =
            machine->activation_frame_replacement++ %
            PETTA_ACTIVATION_FRAME_CACHE_CAP;
        selected = &machine->activation_frames[replacement];
    }
    selected->equation_template = NULL;
    const VarId *source_ids = NULL;
    Atom *const *source_variables = NULL;
    uint32_t variable_count = 0u;
    if (!petta_equation_template_variable_inventory(
            equation_template, &source_ids,
            &source_variables, &variable_count) ||
        !bindings_dense_epoch_frame_prepare(
            &selected->frame, builder,
            source_ids, source_variables, variable_count,
            activation_epoch, activation_first_entry)) {
        return false;
    }
    selected->equation_template = equation_template;
    selected->activation_epoch = activation_epoch;
    selected->activation_first_entry = activation_first_entry;
    *bindings_out = bindings;
    *frame_out = &selected->frame;
    return true;
}

static bool petta_machine_prepare_activation_frame(
        PettaMachineImpl *machine, const PettaGoal *goal,
        Bindings **bindings_out,
        BindingsDenseEpochFrame **frame_out) {
    return goal && petta_machine_prepare_equation_frame(
        machine, goal->activation_template,
        goal->activation_epoch, goal->activation_first_entry,
        bindings_out, frame_out);
}

static Atom *petta_machine_apply_activation_frame(
        PettaMachineImpl *machine, Bindings *bindings,
        BindingsDenseEpochFrame *frame,
        Atom *source, const PettaPlanNode *plan) {
    if (!machine || !bindings || !frame || !source)
        return NULL;
    machine->stats.binding_apply_calls++;
    petta_machine_note_binding_apply_environment(
        machine, bindings,
        frame->first_entry, true);
    if (atom_has_vars(source))
        machine->stats.binding_apply_rewrites++;
    size_t before = machine->host.measure_stats
        ? arena_accounted_live_bytes(&machine->heap) : 0u;
    Atom *result = plan && plan->has_equation_variable_slot &&
            source->kind == ATOM_VAR
        ? bindings_apply_dense_epoch_frame_slot_then_all(
              frame->builder, &machine->heap,
              frame,
              source,
              plan->equation_variable_slot)
        : bindings_apply_dense_epoch_frame_then_all(
              frame->builder, &machine->heap, source, frame);
    if (!result && plan && plan->has_equation_variable_slot &&
        source->kind == ATOM_VAR) {
        /* Derived slot metadata is an accelerator, never authority. */
        result = bindings_apply_dense_epoch_frame_then_all(
            frame->builder, &machine->heap, source, frame);
    }
    if (machine->host.measure_stats) {
        petta_machine_add_u64(
            &machine->stats.binding_apply_allocated_bytes,
            petta_machine_arena_growth(&machine->heap, before));
    }
    return result;
}

/* A prepared slot is usable only when it names this authored source
 * occurrence.  Reconciliation may pair alpha-equivalent equations whose
 * variable identifiers are permuted; in that case the complete dense lookup
 * remains authoritative. */
static Atom *petta_machine_resolve_activation_source_root(
        PettaMachineImpl *machine, Bindings *bindings,
        BindingsDenseEpochFrame *frame,
        Atom *source, const PettaPlanNode *plan) {
    if (!machine || !bindings || !frame || !source)
        return NULL;
    if (plan && plan->has_equation_variable_slot &&
        source->kind == ATOM_VAR) {
        Atom *resolved =
            bindings_resolve_dense_epoch_frame_slot_root(
                frame->builder, &machine->heap, frame, source,
                plan->equation_variable_slot);
        if (resolved)
            return resolved;
    }
    if (!atom_has_vars(source))
        return source;
    return petta_machine_apply_activation_frame(
        machine, bindings, frame, source, plan);
}

/* Match one authored source occurrence through its dense activation frame
 * against an ordinary live value.  A binding which must retain the complete
 * source still materializes at that exact escape point inside the matcher;
 * structural comparisons and already-bound destinations remain views. */
static bool petta_machine_unify_activation_source(
        PettaMachineImpl *machine, const PettaGoal *goal,
        Atom *source, Atom *value, bool *handled_out) {
    if (handled_out)
        *handled_out = false;
    if (!machine || !goal || !source || !value || !handled_out ||
        goal->activation_epoch == 0u) {
        return false;
    }
    if (!atom_has_vars(source)) {
        *handled_out = true;
        return petta_machine_unify(machine, source, value);
    }
    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    Bindings *bindings =
        (Bindings *)bindings_builder_bindings(builder);
    if (goal->activation_first_entry > bindings->len)
        return false;

    BindingsDenseEpochFrame *frame = NULL;
    bool dense_ready = goal->activation_template && goal->plan &&
        petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame);
    if (dense_ready && source->kind == ATOM_VAR) {
        Atom *resolved = petta_machine_resolve_activation_source_root(
            machine, bindings, frame, source, goal->plan);
        if (!resolved)
            return false;
        *handled_out = true;
        return petta_machine_unify(machine, resolved, value);
    }
    Atom *value_root = petta_machine_resolve_root(bindings, value);
    if (!value_root ||
        petta_semantics_is_open_cons_value(source) ||
        petta_semantics_is_open_cons_value(value_root)) {
        return false;
    }

    machine->stats.unification_calls++;
    uint64_t growth_before = builder->growth_count;
    size_t heap_before = arena_accounted_live_bytes(&machine->heap);
    uint32_t mark = bindings_builder_save(builder);
    bool matched = dense_ready
        ? match_atoms_dense_epoch_view_builder_current(
              source, frame, value_root, builder, &machine->heap)
        : match_atoms_epoch_view_builder_current(
              source, goal->activation_epoch,
              goal->activation_first_entry, value_root,
              builder, &machine->heap);
    if (matched && bindings_has_loop(
            bindings_builder_bindings(builder))) {
        matched = false;
    }
    if (!matched)
        bindings_builder_rollback(builder, mark);
    *handled_out = true;
    return petta_machine_finish_unification(
        machine, builder, growth_before, heap_before, matched);
}

/* A DATA occurrence owns its root constructor.  Preserve the source/frame
 * view for every child; only the shallow result shape and the fresh result
 * slots belong to the machine arena. */
static bool petta_push_data_activation_planned(
        PettaMachineImpl *machine, const PettaGoal *goal) {
    if (!machine || !goal || !goal->first || !goal->second ||
        !goal->plan || goal->plan->role != PETTA_PLAN_DATA ||
        goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len == 0u ||
        goal->plan->child_count != goal->first->expr.len ||
        atom_has_vars(goal->first->expr.elems[0]) ||
        !cetta_expr_len_mul_fits_size(
            goal->first->expr.len, sizeof(Atom *))) {
        return false;
    }
    CettaExprLen length = goal->first->expr.len;
    Atom *inline_slots[9];
    Atom **slots = length <= 9u
        ? inline_slots
        : arena_alloc(
              &machine->heap,
              sizeof(*slots) * (size_t)length);
    if (!slots)
        return false;
    slots[0] = goal->first->expr.elems[0];
    for (CettaExprIndex index = 1u; index < length; index++) {
        slots[index] = petta_fresh_variable(machine);
        if (!slots[index])
            return false;
    }
    Atom *shape = atom_expr(&machine->heap, slots, length);
    if (!shape)
        return false;
    for (CettaExprIndex index = length; index > 1u; index--) {
        CettaExprIndex child = index - 1u;
        if (!petta_push_solve_activation_planned(
                machine, goal->first->expr.elems[child],
                slots[child], goal->barrier,
                petta_plan_child(goal->plan, child),
                goal->activation_template,
                goal->activation_epoch,
                goal->activation_first_entry)) {
            return false;
        }
    }
    return petta_push_unify(
        machine, shape, goal->second, goal->barrier);
}

/* Execute a pure grounded occurrence directly from frame-resolved argument
 * registers.  Declining any guard returns to the complete materializing
 * path; an admitted empty result is ordinary relational failure. */
static bool petta_machine_try_activation_pure_grounded(
        PettaMachineImpl *machine, const PettaGoal *goal,
        bool *handled_out) {
    if (handled_out)
        *handled_out = false;
    if (!machine || !goal || !handled_out || !goal->plan ||
        goal->plan->execution !=
            PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS ||
        !goal->first || goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len == 0u || !goal->second ||
        goal->first->expr.elems[0]->kind != ATOM_SYMBOL ||
        goal->plan->child_count != goal->first->expr.len) {
        return false;
    }
    Bindings *bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame)) {
        return false;
    }
    CettaExprLen nargs = goal->first->expr.len - 1u;
    PeTTaNamedArity arity = petta_semantics_named_arity(
        machine->space, &machine->heap,
        goal->first->expr.elems[0], nargs);
    Atom *inline_arguments[8];
    Atom **arguments = nargs <= 8u
        ? inline_arguments
        : arena_alloc(
              &machine->heap,
              sizeof(*arguments) * (size_t)nargs);
    bool ready = arity.known && arity.exact &&
        (nargs <= 8u || arguments != NULL);
    for (CettaExprIndex index = 0u;
         ready && index < nargs; index++) {
        Atom *source = goal->first->expr.elems[index + 1u];
        const PettaPlanNode *argument_plan =
            petta_plan_child(goal->plan, index + 1u);
        Atom *argument =
            petta_machine_resolve_activation_source_root(
                machine, bindings, frame, source, argument_plan);
        ready = argument_plan &&
            argument_plan->role == PETTA_PLAN_VALUE &&
            argument && !atom_has_vars(argument) &&
            !petta_semantics_value_contains_observable_open_cons(
                argument);
        if (ready) {
            arguments[index] = argument;
        }
    }
    if (!ready)
        return false;
    bool truth = false;
    bool scalar_truth = grounded_try_plain_scalar_truth(
        goal->first->expr.elems[0], arguments,
        (uint32_t)nargs, &truth);
    Atom *direct = scalar_truth
        ? petta_machine_boolean_value(machine, truth)
        : grounded_dispatch(
              &machine->heap, goal->first->expr.elems[0],
              arguments, (uint32_t)nargs);
    if (!direct)
        return false;
    *handled_out = true;
    machine->stats.pure_grounded_slot_frame_entries++;
    if (atom_is_empty(direct))
        return false;
    machine->stats.pure_grounded_slot_frame_direct_dispatches++;
    if (!scalar_truth &&
        petta_semantics_truth_value(direct, &truth)) {
        direct = petta_machine_boolean_value(machine, truth);
    }
    return direct && petta_machine_unify(
        machine, direct, goal->second);
}

/* Expand let* into source/frame goals without constructing the enclosing
 * control term.  Resolve each binding pattern once through the frame and
 * feed it directly to its producer; no intermediate result value escapes. */
static bool petta_push_activation_let_star(
        PettaMachineImpl *machine, const PettaGoal *goal,
        bool *applicable_out) {
    if (applicable_out)
        *applicable_out = false;
    if (!machine || !goal || !applicable_out || !goal->plan ||
        !goal->first || goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len != 3u || !goal->second ||
        goal->plan->child_count != 3u) {
        return false;
    }
    Atom *bindings = goal->first->expr.elems[1];
    const PettaPlanNode *bindings_plan =
        petta_plan_child(goal->plan, 1u);
    const PettaPlanNode *body_plan =
        petta_plan_child(goal->plan, 2u);
    if (!bindings || bindings->kind != ATOM_EXPR ||
        !bindings_plan ||
        bindings_plan->child_count != bindings->expr.len ||
        !body_plan) {
        return false;
    }
    for (CettaExprIndex index = 0u;
         index < bindings->expr.len; index++) {
        Atom *binding = bindings->expr.elems[index];
        const PettaPlanNode *binding_plan =
            petta_plan_child(bindings_plan, index);
        if (!binding || binding->kind != ATOM_EXPR ||
            binding->expr.len != 2u || !binding_plan ||
            binding_plan->child_count != 2u) {
            return false;
        }
    }
    Bindings *frame_bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &frame_bindings, &frame)) {
        return false;
    }
    *applicable_out = true;
    if (!petta_push_solve_activation_planned(
            machine, goal->first->expr.elems[2],
            goal->second, goal->barrier, body_plan,
            goal->activation_template,
            goal->activation_epoch,
            goal->activation_first_entry)) {
        return false;
    }
    for (CettaExprIndex index = bindings->expr.len;
         index > 0u; index--) {
        CettaExprIndex binding_index = index - 1u;
        Atom *binding = bindings->expr.elems[binding_index];
        const PettaPlanNode *binding_plan =
            petta_plan_child(bindings_plan, binding_index);
        Atom *pattern_source = binding->expr.elems[0];
        const PettaPlanNode *pattern_plan =
            petta_plan_child(binding_plan, 0u);
        Atom *pattern =
            petta_machine_resolve_activation_source_root(
                machine, frame_bindings, frame,
                pattern_source, pattern_plan);
        if (!pattern ||
            !petta_push_solve_activation_planned(
                machine, binding->expr.elems[1], pattern,
                goal->barrier,
                petta_plan_child(binding_plan, 1u),
                goal->activation_template,
                goal->activation_epoch,
                goal->activation_first_entry)) {
            return false;
        }
    }
    return true;
}

static Atom *petta_machine_materialize_activation_source(
    PettaMachineImpl *machine, const PettaGoal *goal,
    Atom *source) {
    if (!machine || !goal || !source ||
        goal->activation_epoch == 0u) {
        return NULL;
    }
    Bindings *bindings = (Bindings *)search_context_bindings(
        &machine->search);
    if (goal->activation_first_entry > bindings->len)
        return NULL;
    Bindings *dense_bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    bool dense_frame = petta_machine_prepare_activation_frame(
        machine, goal, &dense_bindings, &frame);
    uint64_t before = machine->host.measure_stats
        ? machine->stats.binding_apply_allocated_bytes : 0u;
    Atom *materialized = dense_frame
        ? petta_machine_apply_activation_frame(
              machine, dense_bindings, frame, source, goal->plan)
        : petta_machine_apply_bindings_epoch_then_all(
              machine, bindings, &machine->heap, source,
              goal->activation_epoch,
              goal->activation_first_entry);
    if (machine->host.measure_stats) {
        uint64_t allocated =
            machine->stats.binding_apply_allocated_bytes - before;
        machine->stats.activation_materialization_calls++;
        petta_machine_add_u64(
            &machine->stats.activation_materialization_allocated_bytes,
            allocated);
        if (petta_plan_open_template_admitted(goal->plan)) {
            machine->stats.activation_open_template_admitted_calls++;
            petta_machine_add_u64(
                &machine->stats
                    .activation_open_template_admitted_allocated_bytes,
                allocated);
        }
    }
    return materialized;
}

/* Realize only the authored call-free DATA syntax.  A source variable is
 * resolved to its current root value and that value is shared; it is not
 * recursively substituted again.  Consequently a recursive constructor
 * result allocates one new prefix shell and retains its already-realized
 * suffix, while the final unifier remains the authority for aliases and the
 * occurs check. */
static Atom *petta_machine_materialize_call_free_data_node(
        PettaMachineImpl *machine, Bindings *bindings,
        BindingsDenseEpochFrame *frame,
        Atom *source, const PettaPlanNode *plan) {
    if (!machine || !bindings || !frame || !source || !plan ||
        plan->contains_call) {
        return NULL;
    }
    if (!atom_has_vars(source))
        return source;
    if (source->kind == ATOM_VAR) {
        return petta_machine_resolve_activation_source_root(
            machine, bindings, frame, source, plan);
    }
    if (source->kind != ATOM_EXPR ||
        plan->role != PETTA_PLAN_DATA ||
        plan->child_count != source->expr.len ||
        !cetta_expr_len_fits_size(source->expr.len)) {
        return NULL;
    }
    Atom *draft = atom_expr_builder_begin(
        &machine->heap, source->expr.len);
    if (!draft)
        return NULL;
    for (CettaExprIndex index = 0u;
         index < source->expr.len; index++) {
        Atom *child =
            petta_machine_materialize_call_free_data_node(
                machine, bindings, frame,
                source->expr.elems[index],
                petta_plan_child(plan, index));
        if (!child)
            return NULL;
        draft->expr.elems[index] = child;
    }
    return atom_expr_builder_finish(&machine->heap, draft);
}

static Atom *petta_machine_materialize_call_free_activation_data(
        PettaMachineImpl *machine, const PettaGoal *goal) {
    if (!machine || !goal || !goal->first || !goal->plan ||
        goal->plan->role != PETTA_PLAN_DATA ||
        goal->plan->contains_call ||
        goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len == 0u ||
        goal->plan->child_count != goal->first->expr.len) {
        return NULL;
    }
    Bindings *bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame)) {
        return NULL;
    }
    return petta_machine_materialize_call_free_data_node(
        machine, bindings, frame, goal->first, goal->plan);
}

/* A variable-headed expression is conservatively compiled as a dynamic
 * call.  When every direct field is nevertheless an authored ready value,
 * retain those resolved roots and build only the shallow application shell.
 * The ordinary SOLVE dispatcher remains the authority for deciding whether
 * the resolved head denotes a call or data.  Any executable descendant or
 * unresolved field declines to the complete activation materializer. */
static Atom *petta_machine_materialize_dynamic_call_fields(
        PettaMachineImpl *machine, const PettaGoal *goal) {
    if (!machine || !goal || !goal->first || !goal->plan ||
        goal->plan->role != PETTA_PLAN_DYNAMIC_CALL ||
        goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len == 0u ||
        goal->plan->child_count != goal->first->expr.len ||
        !cetta_expr_len_mul_fits_size(
            goal->first->expr.len, sizeof(Atom *))) {
        return NULL;
    }
    Bindings *bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame)) {
        return NULL;
    }
    CettaExprLen length = goal->first->expr.len;
    Atom *inline_fields[16];
    Atom **fields = length <= 16u
        ? inline_fields
        : arena_alloc(
              &machine->heap,
              sizeof(*fields) * (size_t)length);
    if (!fields)
        return NULL;
    for (CettaExprIndex index = 0u; index < length; index++) {
        const PettaPlanNode *field_plan =
            petta_plan_child(goal->plan, index);
        if (!field_plan ||
            (field_plan->contains_call &&
             field_plan->role != PETTA_PLAN_VALUE)) {
            return NULL;
        }
        Atom *field = petta_machine_resolve_activation_source_root(
            machine, bindings, frame,
            goal->first->expr.elems[index], field_plan);
        if (!field || !petta_machine_planned_value_ready(
                machine, field, field_plan)) {
            return NULL;
        }
        fields[index] = field;
    }
    return atom_expr(&machine->heap, fields, length);
}

typedef enum {
    PETTA_ACTIVATION_CALL_VIEW_DECLINED = 0,
    PETTA_ACTIVATION_CALL_VIEW_READY,
    PETTA_ACTIVATION_CALL_VIEW_CAPACITY,
} PettaActivationCallViewStatus;

typedef struct {
    PettaSpaceQueryView query;
    Atom *inline_arguments[8];
} PettaActivationSpaceQuery;

/* Resolve the direct fields of an authored equation query into a dense
 * activation frame.  The split representation is itself executable; a
 * shallow Atom shell is built only when generic dispatch or a saved
 * continuation genuinely asks for one. */
static PettaActivationCallViewStatus
petta_machine_build_activation_space_query(
        PettaMachineImpl *machine, const PettaGoal *goal,
        Atom *source, PettaActivationSpaceQuery *query_out) {
    if (query_out)
        memset(query_out, 0, sizeof(*query_out));
    if (!machine || !goal || !source || !query_out ||
        !goal->plan ||
        goal->plan->execution != PETTA_PLAN_EXEC_RELATION_SLOTS ||
        source->kind != ATOM_EXPR || source->expr.len == 0u ||
        source->expr.elems[0]->kind != ATOM_SYMBOL ||
        goal->plan->child_count != source->expr.len) {
        return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    }
    for (CettaExprIndex index = 1u;
         index < source->expr.len; index++) {
        const PettaPlanNode *argument_plan =
            petta_plan_child(goal->plan, index);
        if (!argument_plan || argument_plan->contains_call)
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    }
    Bindings *bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame)) {
        return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    }
    CettaExprLen arity = source->expr.len - 1u;
    Atom **arguments = arity <= 8u
        ? query_out->inline_arguments
        : arena_alloc(
              &machine->heap,
              sizeof(*arguments) * (size_t)arity);
    if (arity > 8u && !arguments)
        return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
    for (CettaExprIndex index = 1u;
         index < source->expr.len; index++) {
        Atom *source_argument = source->expr.elems[index];
        const PettaPlanNode *argument_plan =
            petta_plan_child(goal->plan, index);
        Atom *argument =
            petta_machine_resolve_activation_source_root(
                machine, bindings, frame,
                source_argument, argument_plan);
        if (!argument)
            return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
        if (!petta_machine_planned_value_ready(
                machine, argument, argument_plan))
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
        arguments[index - 1u] = argument;
    }
    query_out->query = (PettaSpaceQueryView){
        .head = source->expr.elems[0],
        .arguments = arguments,
        .arity = arity,
        .source = source,
        .source_template = goal->activation_template,
        .source_epoch = goal->activation_epoch,
        .source_first_entry = goal->activation_first_entry,
    };
    return PETTA_ACTIVATION_CALL_VIEW_READY;
}

/* Keep a compiled relation or pure grounded head while evaluating only the
 * argument occurrences which the plan marks callable.  The resulting shallow
 * ready expression is an ordinary continuation input; any missing frame fact
 * declines to full activation materialization below. */
static PettaActivationCallViewStatus
petta_machine_push_activation_arguments(
        PettaMachineImpl *machine, const PettaGoal *goal,
        PettaGoalKind ready_kind) {
    if (!machine || !goal || !goal->first || !goal->second ||
        !goal->plan || !goal->plan->contains_call ||
        goal->first->kind != ATOM_EXPR ||
        goal->first->expr.len == 0u ||
        goal->first->expr.elems[0]->kind != ATOM_SYMBOL ||
        goal->plan->child_count != goal->first->expr.len ||
        (ready_kind != PETTA_GOAL_CALL_READY &&
         ready_kind != PETTA_GOAL_HOST_READY)) {
        return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    }

    SymbolId head = goal->first->expr.elems[0]->sym_id;
    CettaExprLen nargs = goal->first->expr.len - 1u;
    if (ready_kind == PETTA_GOAL_CALL_READY) {
        if (!goal->plan->relation_head_admitted)
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    } else {
        PeTTaNamedArity arity = petta_semantics_named_arity(
            machine->space, &machine->heap,
            goal->first->expr.elems[0], nargs);
        if (!grounded_op_is_type_pure(head) ||
            !arity.known || !arity.exact) {
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
        }
    }

    Bindings *bindings = NULL;
    BindingsDenseEpochFrame *frame = NULL;
    if (!petta_machine_prepare_activation_frame(
            machine, goal, &bindings, &frame)) {
        return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    }

    CettaExprLen length = goal->first->expr.len;
    if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
        return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
    Atom *inline_ready[9];
    uint8_t inline_needs_evaluation[9];
    Atom **ready_elements = length <= 9u
        ? inline_ready
        : arena_alloc(
              &machine->heap,
              sizeof(*ready_elements) * (size_t)length);
    uint8_t *needs_evaluation = length <= 9u
        ? inline_needs_evaluation
        : arena_alloc(
              &machine->heap,
              sizeof(*needs_evaluation) * (size_t)length);
    if (!ready_elements || !needs_evaluation)
        return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
    memset(
        needs_evaluation, 0,
        sizeof(*needs_evaluation) * (size_t)length);
    ready_elements[0] = goal->first->expr.elems[0];
    uint64_t reused = 0u;
    for (CettaExprIndex index = 1u; index < length; index++) {
        Atom *source = goal->first->expr.elems[index];
        const PettaPlanNode *argument_plan =
            petta_plan_child(goal->plan, index);
        if (!source || !argument_plan)
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
        if (argument_plan->contains_call &&
            argument_plan->role != PETTA_PLAN_VALUE) {
            ready_elements[index] = petta_fresh_variable(machine);
            if (!ready_elements[index])
                return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
            needs_evaluation[index] = 1u;
            continue;
        }
        Atom *value = petta_machine_resolve_activation_source_root(
            machine, bindings, frame, source, argument_plan);
        if (!value)
            return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
        if (!petta_machine_planned_value_ready(
                machine, value, argument_plan)) {
            return PETTA_ACTIVATION_CALL_VIEW_DECLINED;
        }
        ready_elements[index] = value;
        reused++;
    }

    Atom *ready = atom_expr(&machine->heap, ready_elements, length);
    if (!ready)
        return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
    if (!petta_goal_push(
            machine,
            (PettaGoal){
                .kind = ready_kind,
                .barrier = goal->barrier,
                .first = ready,
                .second = goal->second,
                .plan = goal->plan,
            })) {
        return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
    }
    for (CettaExprIndex index = length; index > 1u; index--) {
        CettaExprIndex argument = index - 1u;
        if (!needs_evaluation[argument])
            continue;
        if (!petta_push_solve_activation_planned(
                machine, goal->first->expr.elems[argument],
                ready_elements[argument], goal->barrier,
                petta_plan_child(goal->plan, argument),
                goal->activation_template,
                goal->activation_epoch,
                goal->activation_first_entry)) {
            return PETTA_ACTIVATION_CALL_VIEW_CAPACITY;
        }
    }
    if (ready_kind == PETTA_GOAL_CALL_READY) {
        machine->stats.relation_slot_frame_entries++;
        petta_machine_add_u64(
            &machine->stats.relation_slot_operands_reused, reused);
    }
    return PETTA_ACTIVATION_CALL_VIEW_READY;
}

/*
 * An equation activation is a source term plus an epoch-indexed environment.
 * Keep the dominant control constructors as views and materialize only the
 * operands whose semantics demands them.  Every other form returns through
 * the canonical fully materialized SOLVE path in the same transition.
 */
static bool petta_machine_dispatch_activation_solve(
    PettaMachineImpl *machine, const PettaGoal *goal,
    PettaMachineStep *failure) {
    if (!machine || !goal || !failure ||
        goal->kind != PETTA_GOAL_SOLVE_ACTIVATION ||
        goal->activation_source_fields !=
            PETTA_ACTIVATION_SOURCE_FIRST ||
        !goal->first || !goal->second ||
        goal->activation_epoch == 0u) {
        if (failure)
            *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    Atom *source = goal->first;

    /* VALUE and call-free DATA plans need only exact matching against the
     * destination.  Keep their variable-bearing syntax paired with the
     * dense frame instead of constructing a substituted tree first. */
    if (goal->plan &&
        (goal->plan->role == PETTA_PLAN_VALUE ||
         (goal->plan->role == PETTA_PLAN_DATA &&
          !goal->plan->contains_call))) {
        bool handled = false;
        bool matched = petta_machine_unify_activation_source(
            machine, goal, source, goal->second, &handled);
        if (handled) {
            if (goal->plan->execution ==
                    PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS) {
                machine->stats.constructor_slot_frame_entries++;
                machine->stats
                    .constructor_slot_frame_direct_unifications++;
            }
            return matched;
        }
    }

    /* A call-free DATA result whose destination is not bound yet shares the
     * root values supplied by its activation frame.  The resulting shallow
     * constructor is unified normally, so cyclic open results still fail the
     * authoritative occurs check. */
    if (goal->plan && goal->plan->role == PETTA_PLAN_DATA &&
        !goal->plan->contains_call && source->kind == ATOM_EXPR &&
        source->expr.len > 0u &&
        goal->plan->child_count == source->expr.len &&
        !atom_has_vars(source->expr.elems[0])) {
        Atom *shared =
            petta_machine_materialize_call_free_activation_data(
                machine, goal);
        if (shared)
            return petta_machine_unify(
                machine, shared, goal->second);
    }

    /* DATA with executable children is a shallow constructor plus child
     * source/frame goals.  The child plans retain the same authored
     * evaluation order as the ordinary plan executor. */
    if (goal->plan && goal->plan->role == PETTA_PLAN_DATA &&
        goal->plan->contains_call && source->kind == ATOM_EXPR &&
        source->expr.len > 0u &&
        goal->plan->child_count == source->expr.len &&
        !atom_has_vars(source->expr.elems[0])) {
        if (!petta_push_data_activation_planned(machine, goal)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (goal->plan->execution ==
                PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS) {
            machine->stats.constructor_slot_frame_entries++;
        }
        return true;
    }

    bool grounded_handled = false;
    bool grounded_result =
        petta_machine_try_activation_pure_grounded(
            machine, goal, &grounded_handled);
    if (grounded_handled)
        return grounded_result;

    PettaPlanControl control = goal->plan
        ? goal->plan->control : PETTA_PLAN_CONTROL_NONE;
    if (!goal->plan && source->kind == ATOM_EXPR &&
        source->expr.len > 0u &&
        source->expr.elems[0]->kind == ATOM_SYMBOL) {
        SymbolId head = source->expr.elems[0]->sym_id;
        PeTTaForm form = petta_semantics_form(head);
        control = form == PETTA_FORM_IF
            ? PETTA_PLAN_CONTROL_IF
            : form == PETTA_FORM_LET
                ? PETTA_PLAN_CONTROL_LET
                : form == PETTA_FORM_CHAIN
                    ? PETTA_PLAN_CONTROL_CHAIN
                    : head == g_builtin_syms.let_star
                        ? PETTA_PLAN_CONTROL_LET_STAR
                        : PETTA_PLAN_CONTROL_NONE;
    }
    if (source->kind == ATOM_EXPR &&
        source->expr.len > 0u &&
        source->expr.elems[0]->kind == ATOM_SYMBOL) {
        CettaExprLen nargs = source->expr.len - 1u;

        if (control == PETTA_PLAN_CONTROL_IF &&
            (nargs == 2u || nargs == 3u)) {
            Atom *condition = petta_fresh_variable(machine);
            Atom *otherwise = nargs == 3u
                ? source->expr.elems[3]
                : atom_empty(&machine->heap);
            uint8_t source_fields =
                PETTA_ACTIVATION_SOURCE_SECOND;
            if (nargs == 3u)
                source_fields |= PETTA_ACTIVATION_SOURCE_THIRD;
            if (!condition || !otherwise ||
                !petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind = PETTA_GOAL_IF_SELECT,
                        .barrier = goal->barrier,
                        .first = condition,
                        .second = source->expr.elems[2],
                        .third = otherwise,
                        .fourth = goal->second,
                        .second_plan =
                            petta_plan_child(goal->plan, 2u),
                        .third_plan = nargs == 3u
                            ? petta_plan_child(goal->plan, 3u)
                            : NULL,
                        .activation_template =
                            goal->activation_template,
                        .activation_epoch =
                            goal->activation_epoch,
                        .activation_first_entry =
                            goal->activation_first_entry,
                        .activation_source_fields = source_fields,
                    }) ||
                !petta_push_solve_activation_planned(
                    machine, source->expr.elems[1],
                    condition, goal->barrier,
                    petta_plan_child(goal->plan, 1u),
                    goal->activation_template,
                    goal->activation_epoch,
                    goal->activation_first_entry)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }

        if ((control == PETTA_PLAN_CONTROL_LET ||
             control == PETTA_PLAN_CONTROL_CHAIN) &&
            nargs == 3u) {
            Atom *joined = petta_fresh_variable(machine);
            if (!joined) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            const PettaPlanNode *binder_plan =
                petta_plan_child(goal->plan, 1u);
            const PettaPlanNode *producer_plan =
                petta_plan_child(goal->plan, 2u);
            bool solve_right_first =
                control == PETTA_PLAN_CONTROL_LET && producer_plan &&
                producer_plan->role == PETTA_PLAN_DATA &&
                !producer_plan->contains_call && binder_plan &&
                binder_plan->contains_call;
            if (!joined ||
                !petta_push_solve_activation_planned(
                    machine, source->expr.elems[3],
                    goal->second, goal->barrier,
                    petta_plan_child(goal->plan, 3u),
                    goal->activation_template,
                    goal->activation_epoch,
                    goal->activation_first_entry)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (solve_right_first) {
                if (!petta_push_solve_activation_planned(
                        machine, source->expr.elems[1], joined,
                        goal->barrier, binder_plan,
                        goal->activation_template,
                        goal->activation_epoch,
                        goal->activation_first_entry) ||
                    !petta_push_solve_activation_planned(
                        machine, source->expr.elems[2], joined,
                        goal->barrier, producer_plan,
                        goal->activation_template,
                        goal->activation_epoch,
                        goal->activation_first_entry)) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            } else if (!petta_push_solve_activation_planned(
                           machine, source->expr.elems[2], joined,
                           goal->barrier, producer_plan,
                           goal->activation_template,
                           goal->activation_epoch,
                           goal->activation_first_entry) ||
                       !petta_push_solve_activation_planned(
                           machine, source->expr.elems[1], joined,
                           goal->barrier, binder_plan,
                           goal->activation_template,
                           goal->activation_epoch,
                           goal->activation_first_entry)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }

        if (control == PETTA_PLAN_CONTROL_LET_STAR &&
            nargs == 2u) {
            bool applicable = false;
            bool scheduled = petta_push_activation_let_star(
                machine, goal, &applicable);
            if (applicable) {
                if (!scheduled)
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                return scheduled;
            }
        }
    }

    PettaActivationSpaceQuery activation_query = {0};
    PettaActivationCallViewStatus call_view_status =
        petta_machine_build_activation_space_query(
            machine, goal, source, &activation_query);
    if (call_view_status == PETTA_ACTIVATION_CALL_VIEW_CAPACITY) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (call_view_status == PETTA_ACTIVATION_CALL_VIEW_READY) {
        PettaMachineSpaceQueryAdmission admission =
            PETTA_MACHINE_SPACE_QUERY_DEFER;
        if (!machine->host.analysis &&
            !machine->host.prepare_resolved_call &&
            !machine->host.begin_relation_call &&
            !machine->host.record_clause_use &&
            machine->host.admit_space_query) {
            admission = machine->host.admit_space_query(
                machine->host.context, machine->space,
                activation_query.query.head->sym_id,
                activation_query.query.arguments,
                activation_query.query.arity);
        }
        if (admission == PETTA_MACHINE_SPACE_QUERY_INVALIDATED) {
            *failure = PETTA_MACHINE_STEP_INVALIDATED;
            return false;
        }
        if (admission == PETTA_MACHINE_SPACE_QUERY_ADMITTED) {
            Atom *expected = petta_machine_resolve_root(
                search_context_bindings(&machine->search),
                goal->second);
            if (!expected) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            machine->stats.relation_slot_frame_entries++;
            petta_machine_add_u64(
                &machine->stats.relation_slot_operands_reused,
                activation_query.query.arity);
            bool scheduled = petta_machine_start_space_query(
                machine, &activation_query.query, expected,
                goal->barrier, true, false);
            if (!scheduled && machine->terminal)
                *failure = machine->terminal_step;
            return scheduled;
        }

        Atom *call_view = petta_machine_materialize_space_query(
            machine, &activation_query.query);
        if (!call_view) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        PettaGoal solve = *goal;
        solve.kind = PETTA_GOAL_SOLVE;
        solve.first = call_view;
        solve.equation_query_source = source;
        solve.activation_source_fields = 0u;
        solve.first_operand_resolved = true;
        return petta_machine_dispatch_solve(
            machine, &solve, failure);
    }

    PettaGoalKind ready_kind = PETTA_GOAL_SOLVE;
    if (goal->plan && goal->plan->contains_call) {
        if (goal->plan->execution ==
                PETTA_PLAN_EXEC_RELATION_SLOTS) {
            ready_kind = PETTA_GOAL_CALL_READY;
        } else if (goal->plan->execution ==
                       PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS) {
            ready_kind = PETTA_GOAL_HOST_READY;
        }
    }
    if (ready_kind != PETTA_GOAL_SOLVE) {
        PettaActivationCallViewStatus argument_status =
            petta_machine_push_activation_arguments(
                machine, goal, ready_kind);
        if (argument_status == PETTA_ACTIVATION_CALL_VIEW_CAPACITY) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (argument_status == PETTA_ACTIVATION_CALL_VIEW_READY)
            return true;
    }

    Atom *dynamic_call_fields =
        petta_machine_materialize_dynamic_call_fields(
            machine, goal);
    if (dynamic_call_fields) {
        PettaGoal solve = *goal;
        solve.kind = PETTA_GOAL_SOLVE;
        solve.first = dynamic_call_fields;
        solve.activation_epoch = 0u;
        solve.activation_first_entry = 0u;
        solve.activation_source_fields = 0u;
        solve.first_operand_resolved = true;
        return petta_machine_dispatch_solve(
            machine, &solve, failure);
    }

    Atom *materialized =
        petta_machine_materialize_activation_source(
            machine, goal, source);
    if (!materialized) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    /*
     * This is the evaluate=true, translate=false, count=false portion of the
     * authoritative equation-result boundary.  Preserve its Predicate-as-data
     * phase rule, then fuse the fully materialized activation into SOLVE in
     * this transition.  Enqueuing an equivalent SOLVE goal here would add one
     * scheduler round for every ordinary RHS without changing the trail,
     * plan, barrier, or observable choice order.
     */
    if (materialized->kind == ATOM_EXPR &&
        materialized->expr.len == 2u &&
        petta_semantics_form(
            atom_head_symbol_id(materialized)) ==
                PETTA_FORM_PREDICATE) {
        return petta_machine_unify(
            machine, materialized, goal->second);
    }
    PettaGoal solve = *goal;
    solve.kind = PETTA_GOAL_SOLVE;
    solve.first = materialized;
    solve.activation_epoch = 0u;
    solve.activation_first_entry = 0u;
    solve.activation_source_fields = 0u;
    solve.first_operand_resolved = true;
    return petta_machine_dispatch_solve(
        machine, &solve, failure);
}

static bool petta_machine_dispatch_solve(
    PettaMachineImpl *machine, const PettaGoal *goal,
    PettaMachineStep *failure) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    const PettaPlanNode *plan =
        goal->kind == PETTA_GOAL_FORCE ? NULL : goal->plan;

    /* Observe the source-level equality before ordinary eager operand
     * evaluation starts a collapse child.  Once that child has run, the
     * existence law's syntactic witness has already been materialized away. */
    if (goal->kind == PETTA_GOAL_SOLVE && goal->first &&
        goal->first->kind == ATOM_EXPR &&
        goal->first->expr.len == 3u &&
        atom_is_symbol_id(
            goal->first->expr.elems[0], g_builtin_syms.op_eq)) {
        Atom *observer_expression = goal->first_operand_resolved
            ? goal->first
            : petta_machine_apply_bindings(
                  machine, environment, &machine->heap,
                  goal->first);
        Atom *observer_expected = petta_machine_apply_bindings(
            machine, environment, &machine->heap,
            goal->second);
        if (!observer_expression || !observer_expected) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        bool handled = false;
        bool result = petta_machine_try_match_existence_observer(
            machine, observer_expression,
            observer_expected, &handled);
        if (handled)
            return result;
    }

    /* A transparent constructor plan is already a slot program: constrain
     * its outer shape against the live trail, then run only child occurrences
     * which the authored plan marked callable.  Do this before materializing
     * the complete substituted term; the authoritative matcher dereferences
     * constructor fields on demand. */
    if (goal->kind == PETTA_GOAL_SOLVE && plan &&
        plan->role == PETTA_PLAN_DATA &&
        plan->execution == PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS &&
        goal->first &&
        goal->first->kind == ATOM_EXPR &&
        goal->first->expr.len > 1u) {
        machine->stats.constructor_slot_frame_entries++;
        if (!plan->contains_call) {
            machine->stats
                .constructor_slot_frame_direct_unifications++;
            return petta_machine_unify(
                machine, goal->first, goal->second);
        }
        if (!petta_push_data_expression_planned(
                machine, goal->first, goal->second,
                1u, goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /* Type-pure grounded operators with value-role operands are an exact
     * register instruction.  Resolve only their argument slots, require a
     * ground value in every slot, and preserve PeTTa currying by admitting
     * only an exact native arity.  A failed guard leaves the canonical call
     * path untouched. */
    if (goal->kind == PETTA_GOAL_SOLVE && plan &&
        plan->execution == PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS &&
        goal->first &&
        goal->first->kind == ATOM_EXPR &&
        goal->first->expr.len > 0u && goal->second &&
        goal->first->expr.elems[0]->kind == ATOM_SYMBOL &&
        goal->first->expr.len - 1u <= UINT32_MAX) {
        CettaExprLen nargs = goal->first->expr.len - 1u;
        PeTTaNamedArity arity = petta_semantics_named_arity(
            machine->space, &machine->heap,
            goal->first->expr.elems[0], nargs);
        Atom *inline_arguments[8];
        Atom **arguments = nargs <= 8u
            ? inline_arguments
            : arena_alloc(
                  &machine->heap,
                  sizeof(*arguments) * (size_t)nargs);
        bool ready = arity.known && arity.exact &&
            (nargs <= 8u || arguments != NULL);
        for (CettaExprIndex index = 0u;
             ready && index < nargs; index++) {
            const PettaPlanNode *argument_plan =
                petta_plan_child(plan, index + 1u);
            Atom *argument = petta_machine_resolve_root(
                environment,
                goal->first->expr.elems[index + 1u]);
            ready = argument_plan &&
                argument_plan->role == PETTA_PLAN_VALUE &&
                argument && !atom_has_vars(argument) &&
                !petta_semantics_value_contains_observable_open_cons(
                    argument);
            if (ready)
                arguments[index] = argument;
        }
        if (ready) {
            machine->stats.pure_grounded_slot_frame_entries++;
            bool truth = false;
            bool scalar_truth = grounded_try_plain_scalar_truth(
                goal->first->expr.elems[0], arguments,
                (uint32_t)nargs, &truth);
            Atom *direct = scalar_truth
                ? petta_machine_boolean_value(machine, truth)
                : grounded_dispatch(
                      &machine->heap, goal->first->expr.elems[0],
                      arguments, (uint32_t)nargs);
            if (direct) {
                if (atom_is_empty(direct))
                    return false;
                machine->stats
                    .pure_grounded_slot_frame_direct_dispatches++;
                if (!scalar_truth &&
                    petta_semantics_truth_value(direct, &truth)) {
                    direct = petta_machine_boolean_value(machine, truth);
                }
                return direct && petta_machine_unify(
                    machine, direct, goal->second);
            }
        }
    }

    Atom *expression = goal->first;
    if (!goal->first_operand_resolved) {
        uint64_t before = machine->stats.binding_apply_allocated_bytes;
        bool expression_view =
            petta_solve_expression_root_view_enabled() &&
            petta_plan_open_template_admitted(plan);
        expression = expression_view
            ? petta_machine_resolve_root(environment, goal->first)
            : petta_machine_apply_bindings(
                  machine, environment, &machine->heap, goal->first);
        if (machine->host.measure_stats) {
            uint64_t allocated =
                machine->stats.binding_apply_allocated_bytes - before;
            machine->stats.solve_expression_apply_calls++;
            petta_machine_add_u64(
                &machine->stats.solve_expression_apply_allocated_bytes,
                allocated);
            if (petta_plan_open_template_admitted(plan)) {
                machine->stats
                    .solve_expression_open_template_admitted_calls++;
                petta_machine_add_u64(
                    &machine->stats
                        .solve_expression_open_template_admitted_allocated_bytes,
                    allocated);
            }
        }
    }
    if (!expression) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    uint64_t expected_before =
        machine->stats.binding_apply_allocated_bytes;
    bool expected_view =
        petta_solve_expected_root_view_enabled() &&
        petta_plan_open_template_admitted(plan);
    Atom *expected = expected_view
        ? petta_machine_resolve_root(environment, goal->second)
        : petta_machine_apply_bindings(
              machine, environment, &machine->heap, goal->second);
    if (machine->host.measure_stats) {
        uint64_t allocated =
            machine->stats.binding_apply_allocated_bytes -
                expected_before;
        machine->stats.solve_expected_apply_calls++;
        petta_machine_add_u64(
            &machine->stats.solve_expected_apply_allocated_bytes,
            allocated);
        if (petta_plan_open_template_admitted(plan)) {
            machine->stats
                .solve_expected_open_template_admitted_calls++;
            petta_machine_add_u64(
                &machine->stats
                    .solve_expected_open_template_admitted_allocated_bytes,
                allocated);
        }
    }
    if (!expected) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    bool resolved_value_reference = false;
    if (goal->kind == PETTA_GOAL_SOLVE &&
        goal->first && goal->first->kind != ATOM_VAR &&
        (!plan || plan->role != PETTA_PLAN_VALUE ||
         machine->host.resolve_value_references_in_value_role) &&
        machine->host.resolve_value_reference &&
        petta_machine_is_value_reference(expression)) {
        Atom *resolved = NULL;
        if (!machine->host.resolve_value_reference(
                machine->host.context, &machine->heap,
                expression, &resolved)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (resolved) {
            expression = resolved;
            resolved_value_reference = true;
        }
    }

    if (goal->kind ==
        PETTA_GOAL_SOLVE_COUNTED_COLLECTION) {
        return petta_machine_dispatch_counted_collection(
            machine, goal, expression, expected,
            plan, failure);
    }

    if (!resolved_value_reference && plan &&
        (plan->role == PETTA_PLAN_STATIC_CALL ||
         plan->role == PETTA_PLAN_DYNAMIC_CALL) &&
        machine->host.prepare_resolved_call) {
        Atom *prepared = NULL;
        if (!machine->host.prepare_resolved_call(
                machine->host.context, machine->space,
                &machine->heap, expression, &prepared) ||
            !prepared) {
            *failure = PETTA_MACHINE_STEP_DECLINED;
            return false;
        }
        expression = prepared;
    }

    /*
     * PeTTa compiles a source variable to a value-producing Prolog variable,
     * not to a runtime dispatch.  Keep that intensional role even when the
     * current environment substitutes an expression with a callable head.
     * An explicit FORCE goal (introduced only by eval/reduce) intentionally
     * evaluates the substituted value as code.
    */
    if (goal->kind == PETTA_GOAL_SOLVE &&
        (resolved_value_reference ||
         (plan && plan->role == PETTA_PLAN_VALUE) ||
         (goal->first && goal->first->kind == ATOM_VAR))) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    /*
     * Canonical PeTTa callables are values.  In particular, a lambda passed
     * as an argument must not be traversed as ordinary expression data:
     * doing so evaluates its locally-nameless body before substitution and
     * can turn a suspended arithmetic body into a BadArgType value.
     * Application is handled when the callable occurs in head position;
     * here the whole expression is the value being solved.
     */
    Atom *callable_body = NULL;
    if (petta_semantics_lambda_body(
            expression, &callable_body) ||
        petta_semantics_nullary_lambda_body(
            expression, &callable_body) ||
        petta_semantics_partial_view(
            expression, NULL, NULL)) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    /*
     * The internal open-list carrier is always a value.  It may arrive by
     * dereferencing a source variable or be retained inside a dynamically
     * installed clause; in neither case is its representation tag a
     * callable head.
     */
    if (petta_semantics_is_open_cons_value(expression)) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    int64_t counted_collection = 0;
    if (atom_petta_counted_collection_count(
            expression, &counted_collection)) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    if (expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u) {
        /* Empty is the evaluator's additive zero, not an ordinary atomic
         * answer.  Structural consumers such as case retain their own
         * non-evaluating marker handling. */
        if (atom_is_empty(expression))
            return false;
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    /*
     * A Prolog compound returned through the optional libpl boundary has an
     * unforgeable internal tag while retaining its structural children for
     * substitution.  It is a value, not a dynamic application of its visible
     * functor.
     */
    Atom *prolog_compound_body = NULL;
    if (atom_petta_prolog_compound_body(
            expression, &prolog_compound_body) ||
        atom_prolog_compound_body(
            expression, &prolog_compound_body)) {
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    /*
     * An Error is control unless the binding-time type obligation on its
     * destination explicitly admits that Error as data (for example the
     * Error arm of a union).  The obligation is installed before a typed
     * argument is evaluated, so this check happens before host dispatch can
     * raise the value.  Unknown or refuted judgments deliberately fall
     * through to the ordinary error path.
     */
    if (petta_machine_type_obligations_enabled(machine) &&
        atom_is_error(expression)) {
        Atom *contextual_type =
            petta_machine_type_obligation_for_value(
                machine, expected);
        if (contextual_type) {
            PettaAnalysisResult result = {0};
            if (!petta_machine_judge_native_type(
                    machine, expression, contextual_type,
                    &result)) {
                petta_machine_record_typecheck_failure(
                    machine, expression, contextual_type,
                    &result, 1);
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
            if (result.verdict == PETTA_ANALYSIS_ESTABLISHED) {
                return petta_machine_unify_resolved(
                    machine, expression, expected);
            }
        }
    }

    PettaMachineHostMode override_mode = machine->host.classify
        ? machine->host.classify(
              machine->host.context, machine->space, expression)
        : PETTA_MACHINE_HOST_NONE;
    if (override_mode == PETTA_MACHINE_HOST_READY_OVERRIDE) {
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_HOST_READY,
                .barrier = goal->barrier,
                .first = expression,
                .second = expected,
            });
    }

    /*
     * A data occurrence stays data even if a later definition or foreign
     * import registers its head as a function.  Its children retain their
     * independently compiled roles, so `(data-head (known-call ...))` still
     * evaluates the known child exactly as PeTTa's translator does.
     */
    if (plan && plan->role == PETTA_PLAN_DATA) {
        if (!petta_push_data_expression_planned(
                machine, expression, expected,
                1u, goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    Atom *head = expression->expr.elems[0];
    SymbolId head_id =
        head->kind == ATOM_SYMBOL ? head->sym_id : SYMBOL_ID_NONE;
    CettaExprLen nargs = expression->expr.len - 1u;
    PeTTaForm form =
        head_id == SYMBOL_ID_NONE
            ? PETTA_FORM_NONE : petta_semantics_form(head_id);

    Atom *expanded_partial = NULL;
    bool partial_head = false;
    if (!petta_machine_expand_partial_application(
            machine, expression,
            &expanded_partial, &partial_head)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    if (partial_head) {
        return petta_push_solve(
            machine, expanded_partial, expected,
            goal->barrier);
    }

    if (head_id == g_builtin_syms.op_eq && nargs == 2u) {
        bool handled = false;
        bool result = petta_machine_try_match_existence_observer(
            machine, expression, expected, &handled);
        if (handled)
            return result;
    }

    /*
     * A canonical lambda is already a value, but an expression headed by
     * that value is an application.  Evaluate its arguments with the same
     * machine that owns the surrounding search, then beta-reduce into an
     * ordinary SOLVE continuation.  Sending the application through the
     * shared host would place the substituted body behind the host-depth
     * guard, where PeTTa failure such as `(empty)` becomes inert data.
     */
    Atom *lambda_body = NULL;
    if (petta_semantics_lambda_body(head, &lambda_body) ||
        petta_semantics_nullary_lambda_body(
            head, &lambda_body)) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_LAMBDA_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * The shared runtime owns OS-thread scheduling for hyperpose.  Delegate
     * only the two delimited wrappers for which that scheduler has a complete
     * contract: reify every branch, or race to the first
     * answer under once.  Ordinary hyperpose remains the relational choice
     * below, so all other control and backtracking stay in this machine.
     */
    bool host_hyperpose_wrapper =
        (head_id == g_builtin_syms.once ||
         petta_machine_is_reify_head(machine, head_id)) &&
        nargs == 1u &&
        expression->expr.elems[1] &&
        expression->expr.elems[1]->kind == ATOM_EXPR &&
        expression->expr.elems[1]->expr.len == 2u &&
        atom_is_symbol_id(
            expression->expr.elems[1]->expr.elems[0],
            g_builtin_syms.hyperpose);
    if (host_hyperpose_wrapper && machine->host.evaluate) {
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_HOST_READY,
                .barrier = goal->barrier,
                .first = expression,
                .second = expected,
            });
    }

    if (head_id == g_builtin_syms.quote && nargs == 1u)
        return petta_machine_unify_resolved(
            machine,
            machine->host.quote_is_inert_data
                ? expression : expression->expr.elems[1],
            expected);

    if (head_id == g_builtin_syms.return_text && nargs == 1u)
        return petta_machine_unify_resolved(
            machine, expression->expr.elems[1], expected);

    /*
     * `sealed` is the reference translator's variable-privacy form, not a
     * strict operator: copy_term(Vars, Goal+Result, _, Copy), then the COPY
     * runs.  Only the variables of the first argument are freshened; every
     * other variable keeps caller identity, and because the copy is taken
     * before execution, bindings the goal makes to sealed variables stay
     * private to the copy.  An empty seal list is the identity.  Strict
     * grounded dispatch would instead evaluate the goal first and apply
     * HE's complementary standardize-apart contract — severing the variable
     * linkage rule compilers thread through this form.
     */
    if (head_id == g_builtin_syms.sealed_text && nargs == 2u) {
        Atom *vars_term = petta_machine_apply_bindings(
            machine, environment, &machine->heap,
            expression->expr.elems[1]);
        Atom *goal_term = petta_machine_apply_bindings(
            machine, environment, &machine->heap,
            expression->expr.elems[2]);
        Atom *copy = vars_term && goal_term
            ? rename_vars_only(
                  &machine->heap, goal_term, vars_term)
            : NULL;
        if (!copy) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_push_solve(
            machine, copy, expected, goal->barrier);
    }

    /*
     * Source ingestion can erase a statically visible ascription, but an
     * ascription reconstructed under quote/eval reaches the machine at run
     * time.  Evaluate only its payload, preserve the declared type as data,
     * and install a binding-time obligation before exposing the value to the
     * enclosing continuation.
     */
    if (petta_machine_type_obligations_enabled(machine) &&
        head_id == g_builtin_syms.petta_the && nargs == 2u) {
        Atom *value = petta_fresh_variable(machine);
        if (!value ||
            !petta_push_unify(
                machine, value, expected, goal->barrier) ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_TYPE_ASCRIBE_READY,
                    .barrier = goal->barrier,
                    .first = value,
                    .second = expression->expr.elems[1],
                }) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[2], value,
                goal->barrier, petta_plan_child(plan, 2u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    Atom *contextual_type =
        petta_machine_type_obligation_for_value(machine, expected);
    bool atom_list_context =
        head_id == g_builtin_syms.petta_make_list &&
        contextual_type && contextual_type->kind == ATOM_EXPR &&
        contextual_type->expr.len == 2u &&
        petta_symbol_name_is(
            atom_head_symbol_id(contextual_type), "List") &&
        contextual_type->expr.elems[1]->kind == ATOM_SYMBOL &&
        contextual_type->expr.elems[1]->sym_id == g_builtin_syms.atom;
    if (atom_list_context) {
        if (!cetta_expr_len_mul_fits_size(
                nargs, sizeof(Atom *))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom **items = nargs == 0u
            ? NULL
            : arena_alloc(
                  &machine->heap,
                  sizeof(*items) * (size_t)nargs);
        if (nargs > 0u && !items) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = 0u; index < nargs; index++) {
            items[index] = petta_machine_apply_bindings(
                machine, environment, &machine->heap,
                expression->expr.elems[index + 1u]);
            if (!items[index]) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        Atom *list = atom_expr(&machine->heap, items, nargs);
        return list && petta_machine_unify_resolved(
                           machine, list, expected);
    }

    /* `data` is extended PeTTa's non-callable tuple.  Typecheck-v2 adds
     * `make-list`; its live `the` ascription is handled above.  In extended,
     * brand/the annotations have already erased during ingestion. */
    if (machine->host.is_data_constructor &&
        machine->host.is_data_constructor(
            machine->host.context, head_id, nargs)) {
        if (!cetta_expr_len_mul_fits_size(
                nargs, sizeof(Atom *))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom **slots = nargs == 0u
            ? NULL
            : arena_alloc(
                  &machine->heap,
                  sizeof(*slots) * (size_t)nargs);
        if (nargs > 0u && !slots) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = 0u; index < nargs; index++) {
            slots[index] = petta_fresh_variable(machine);
            if (!slots[index]) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        Atom *tuple = atom_expr(&machine->heap, slots, nargs);
        if (!tuple) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = nargs; index > 0u; index--) {
            if (!petta_push_solve_planned(
                    machine, expression->expr.elems[index],
                    slots[index - 1u], goal->barrier,
                    petta_plan_child(plan, index))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        return petta_push_unify(
            machine, tuple, expected, goal->barrier);
    }

    /*
     * Runtime `:` and `->` expressions are PeTTa's transparent typed-data
     * constructors.  Their authored child occurrences evaluate in place,
     * while a value substituted through a variable remains a value because
     * the occurrence plan still belongs to that variable.  Keeping both
     * constructors here preserves that value/code boundary and avoids
     * sending ordinary constructor assembly through the HE-shaped host.
     */
    if ((head_id == g_builtin_syms.colon ||
         head_id == g_builtin_syms.arrow) &&
        nargs >= 1u) {
        /* The entry substitution above has already resolved the constructor
         * through the live environment.  Avoid rebuilding a long proof/type
         * tree one field at a time when every field is already a value.  A
         * VALUE/DATA plan is occurrence authority; otherwise inspect the
         * resolved subtree for a callable and retain the ordinary child
         * continuations exactly when evaluation is still required. */
        bool children_ready = true;
        for (CettaExprIndex index = 1u;
             index <= nargs; index++) {
            const PettaPlanNode *child_plan =
                petta_plan_child(plan, index);
            Atom *child = expression->expr.elems[index];
            if (!petta_machine_planned_value_ready(
                    machine, child, child_plan)) {
                children_ready = false;
                break;
            }
        }
        if (children_ready) {
            return petta_machine_unify(
                machine, expression, expected);
        }
        if (!cetta_expr_len_mul_fits_size(
                nargs, sizeof(Atom *))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom **slots = arena_alloc(
            &machine->heap, sizeof(*slots) * ((size_t)nargs + 1u));
        if (!slots) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        slots[0] = expression->expr.elems[0];
        for (CettaExprIndex index = 0u; index < nargs; index++) {
            slots[index + 1u] = petta_fresh_variable(machine);
            if (!slots[index + 1u]) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        Atom *tuple = atom_expr(
            &machine->heap, slots, (CettaExprLen)(nargs + 1u));
        if (!tuple) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = nargs; index > 0u; index--) {
            if (!petta_push_solve_planned(
                    machine, expression->expr.elems[index],
                    slots[index], goal->barrier,
                    petta_plan_child(plan, index))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        return petta_push_unify(
            machine, tuple, expected, goal->barrier);
    }

    /*
     * `Predicate` crosses from MeTTa list syntax to the optional
     * Prolog-callable representation.  Apply current bindings to its body,
     * but do not execute the visible functor or arguments.
     */
    if (form == PETTA_FORM_PREDICATE && nargs == 1u) {
        Atom *body = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Atom *compound =
            body ? atom_petta_prolog_compound(
                       &machine->heap, body)
                 : NULL;
        return compound &&
               petta_machine_unify_resolved(
                   machine, compound, expected);
    }

    if (form == PETTA_FORM_BIND_STATE ||
        form == PETTA_FORM_GET_STATE ||
        form == PETTA_FORM_CHANGE_STATE ||
        form == PETTA_FORM_NEW_STATE) {
        return petta_machine_schedule_named_state(
            machine, expression, expected, form,
            goal->barrier, plan);
    }

    /*
     * PeTTa's repr/1 renders the value produced by its argument.  Demand the
     * argument through its authored plan first: a known under-application
     * becomes a canonical partial value, while an unknown/data occurrence
     * remains syntax and is rendered unchanged.
     */
    if (head_id == g_builtin_syms.repr && nargs == 1u) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_HOST_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if ((head_id == g_builtin_syms.superpose ||
         head_id == g_builtin_syms.hyperpose) &&
        nargs == 1u) {
        Atom *items = petta_machine_apply_bindings(machine,
            environment, &machine->heap, expression->expr.elems[1]);
        /*
         * The alternative list is a LOGICAL list: a bound closed open-cons
         * chain (a chainer result set, for example) denotes the same
         * alternatives as its flat spelling and must distribute the same
         * way, not iterate the carrier's implementation fields.
         */
        items = petta_semantics_flatten_closed_open_cons(
            &machine->heap, items);
        return petta_machine_start_superpose(
            machine, items, expected, goal->barrier,
            petta_plan_child(plan, 1u));
    }

    if (head_id == g_builtin_syms.petta_member && nargs == 2u) {
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_RELATIONAL_MEMBER_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.size_atom && nargs == 1u) {
        /*
         * size-atom is an observer, not a forcing boundary.  Its argument
         * has already had the current environment applied at SOLVE entry;
         * inspect that value directly.  Re-solving it here would turn a
         * list such as `(and implies iff)` into a Boolean call merely
         * because its first data item happens to name a callable.
         */
        Atom *items = expression->expr.elems[1];
        int64_t counted = 0;
        if (atom_petta_counted_collection_count(
                items, &counted)) {
            return petta_machine_unify(
                machine, atom_int(&machine->heap, counted), expected);
        }
        if (items->kind != ATOM_EXPR && items->kind != ATOM_VAR &&
            !petta_semantics_is_open_cons_value(items)) {
            return petta_machine_unify(
                machine, atom_unit(&machine->heap), expected);
        }
        return petta_machine_start_list_length_choice(
            machine, items, expected, goal->barrier);
    }

    if (form == PETTA_FORM_LENGTH && nargs == 1u) {
        Atom *argument = expression->expr.elems[1];
        if (argument && argument->kind == ATOM_EXPR &&
            argument->expr.len == 2u &&
            petta_machine_atom_is_reify_head(
                machine, argument->expr.elems[0])) {
            return petta_machine_start_count_collapse(
                machine, argument->expr.elems[1],
                expected, goal->barrier,
                petta_plan_child(
                    petta_plan_child(plan, 1u), 1u),
                false, failure);
        }
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_LIST_SIZE_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if ((head_id == g_builtin_syms.car_atom ||
         head_id == g_builtin_syms.cdr_atom ||
         head_id == g_builtin_syms.petta_last ||
         head_id == g_builtin_syms.reverse ||
         head_id == g_builtin_syms.petta_msort) &&
        nargs == 1u) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_LIST_UNARY_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (((form == PETTA_FORM_LIST_TO_SET && nargs == 1u) ||
         (form == PETTA_FORM_EXCLUDE_ITEM && nargs == 2u))) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_LIST_SET_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_SREAD && nargs == 1u) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_SREAD_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.empty_form && nargs == 0u)
        return false;

    if (head_id == g_builtin_syms.once && nargs == 1u) {
        return petta_machine_start_once(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u),
            failure);
    }

    if (head_id == g_builtin_syms.petta_transaction &&
        nargs == 1u) {
        return petta_machine_start_transaction(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u),
            failure);
    }

    if (head_id == g_builtin_syms.petta_with_mutex &&
        nargs == 2u) {
        return petta_machine_start_mutex(
            machine, expression->expr.elems[1],
            expression->expr.elems[2], expected,
            goal->barrier, petta_plan_child(plan, 2u),
            failure);
    }

    if (petta_machine_is_reify_head(machine, head_id) && nargs == 1u) {
        return petta_machine_start_collapse(
            machine, expression->expr.elems[1],
            expected, goal->barrier,
            petta_plan_child(plan, 1u), failure);
    }

    /*
     * PeTTa's test form is a relational observation boundary: collect the
     * complete answer bag of the first argument, evaluate the expected value,
     * then delegate only the diagnostic effect to the shared host.  Owning
     * the two computations here preserves their compiled occurrence plans;
     * the host receives quoted ready values and therefore cannot re-evaluate
     * either side.
     */
    if (petta_semantics_form(head_id) == PETTA_FORM_TEST &&
        nargs == 2u) {
        Atom *actual_bag = petta_fresh_variable(machine);
        Atom *expected_value = petta_fresh_variable(machine);
        if (!actual_bag || !expected_value ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_TEST_COMPARE,
                    .barrier = goal->barrier,
                    .first = actual_bag,
                    .second = expected_value,
                    .third = expression->expr.elems[0],
                    .fourth = expected,
                }) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[2],
                expected_value, goal->barrier,
                petta_plan_child(plan, 2u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_start_collapse(
            machine, expression->expr.elems[1],
            actual_bag, goal->barrier,
            petta_plan_child(plan, 1u), failure);
    }

    if (head_id == g_builtin_syms.foldl_atom &&
        (nargs == 3u || nargs == 5u)) {
        Atom *items = petta_fresh_variable(machine);
        Atom *initial = petta_fresh_variable(machine);
        if (!items || !initial ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_FOLDL_ATOM_READY,
                    .barrier = goal->barrier,
                    .first = items,
                    .second = initial,
                    .third = expression,
                    .fourth = expected,
                }) ||
            !petta_push_solve(
                machine, expression->expr.elems[2],
                initial, goal->barrier) ||
            !petta_push_solve(
                machine, expression->expr.elems[1],
                items, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * SWI's foldl/4 is exposed by PeTTa in function form as
     * `(foldl Callable Items Initial)`.  Normalize it to the native
     * foldl-atom continuation shape so both forms share one relational
     * implementation, including step nondeterminism and cut/suspension
     * behavior.
     */
    if (form == PETTA_FORM_FOLDL && nargs == 3u) {
        Atom *items = petta_fresh_variable(machine);
        Atom *initial = petta_fresh_variable(machine);
        Atom *foldl_atom =
            atom_symbol_id(&machine->heap, g_builtin_syms.foldl_atom);
        Atom *normalized_elements[4] = {
            foldl_atom,
            expression->expr.elems[2],
            expression->expr.elems[3],
            expression->expr.elems[1],
        };
        Atom *normalized = foldl_atom
            ? atom_expr(&machine->heap, normalized_elements, 4u)
            : NULL;
        if (!items || !initial || !normalized ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_FOLDL_ATOM_READY,
                    .barrier = goal->barrier,
                    .first = items,
                    .second = initial,
                    .third = normalized,
                    .fourth = expected,
                    .choice_index = 1u,
                }) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[3],
                initial, goal->barrier,
                petta_plan_child(plan, 3u)) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[2],
                items, goal->barrier,
                petta_plan_child(plan, 2u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.filter_atom &&
        (nargs == 2u || nargs == 3u)) {
        Atom *items = petta_fresh_variable(machine);
        if (!items ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_FILTER_ATOM_READY,
                    .barrier = goal->barrier,
                    .first = items,
                    .second = expression->expr.elems[2],
                    .third = nargs == 3u
                        ? expression->expr.elems[3] : NULL,
                    .fourth = expected,
                }) ||
            !petta_push_solve(
                machine, expression->expr.elems[1],
                items, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.match && nargs == 3u) {
        Atom *reference = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Space *direct_target = machine->host.resolve_space
            ? machine->host.resolve_space(
                  machine->host.context, machine->space,
                  &machine->heap, reference)
            : NULL;
        /*
         * The space expression is strict, while pattern and template remain
         * authored syntax.  Resolve already-materialized space handles
         * directly; otherwise evaluate the space exactly once and resume at
         * a dedicated ready boundary.  Re-entering generic host evaluation
         * here would either evaluate the payload or hide PeTTa clauses behind
         * the host-depth guard.
         */
        if (!direct_target && reference->kind == ATOM_EXPR) {
            Atom *ready_reference = petta_fresh_variable(machine);
            if (!ready_reference ||
                !petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind = PETTA_GOAL_MATCH_SPACE_READY,
                        .barrier = goal->barrier,
                        .first = ready_reference,
                        .second = expression->expr.elems[2],
                        .third = expression->expr.elems[3],
                        .fourth = expected,
                        .plan = petta_plan_child(plan, 3u),
                    }) ||
                !petta_push_solve_planned(
                    machine, expression->expr.elems[1],
                    ready_reference, goal->barrier,
                    petta_plan_child(plan, 1u))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return petta_machine_start_ready_match(
            machine, reference, expression->expr.elems[2],
            expression->expr.elems[3], expected,
            goal->barrier, petta_plan_child(plan, 3u), failure);
    }

    if (head_id == g_builtin_syms.equals && nargs == 2u) {
        Atom *left = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Atom *right = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[2]);
        /*
         * Closed, already-ready operands need neither temporary result slots
         * nor a later EQUAL_READY continuation.  The same unifier decides the
         * equality here; open operands retain the relational path below.
         */
        if (left && right &&
            !atom_has_vars(left) &&
            !atom_has_vars(right) &&
            petta_machine_immediate_value(
                left, petta_plan_child(plan, 1u)) &&
            petta_machine_immediate_value(
                right, petta_plan_child(plan, 2u))) {
            bool equal = petta_machine_unify_resolved(
                machine, left, right);
            return petta_machine_boolean(
                machine, equal, expected);
        }
        /*
         * Operands evaluate before the comparison — a cons cell built in
         * source becomes its list value and a quote yields its payload, as
         * the reference's translate-then-'='/3 sequence does — so cons
         * handling lives in the EQUAL_READY continuation, where the judge
         * is representation-aware.  Short-circuiting here on the raw
         * operands would compare unevaluated pattern syntax (the chainer's
         * `(quote (...))`-carrying premise patterns are the motivating
         * case).
         */
        bool left_open = left && atom_has_vars(left);
        bool right_open = right && atom_has_vars(right);
        Atom *open = NULL;
        Atom *rigid = NULL;
        if (left_open && !right_open &&
            petta_machine_is_rigid_data(machine, right)) {
            open = left;
            rigid = right;
        } else if (right_open && !left_open &&
                   petta_machine_is_rigid_data(machine, left)) {
            open = right;
            rigid = left;
        }
        /*
         * An open operand against rigid data keeps the demand-driven solve:
         * expected-side demand is what lets a function occurrence in the
         * open operand run inverted (functionhead.metta) instead of
         * enumerating forward.  PeTTa's =/3 is nevertheless a total boolean:
         * a comparison with no solution is the false answer, never
         * relational branch failure — otherwise the else branch of every
         * `(if (= ...) ...)` is silently erased.  The fallback choice below
         * emits exactly one false once the solve exhausts with no success;
         * each success marks it consumed through PETTA_GOAL_EQUAL_COMMIT.
         */
        if (open) {
            PettaChoice fallback = {
                .kind = PETTA_CHOICE_EQUAL_DEFAULT,
                .trail = search_context_save(&machine->search),
                .goal_height = machine->goal_len,
                .barrier = goal->barrier,
                .as.equal_default = {
                    .saw_answer = false,
                    .expected = expected,
                },
            };
            if (!petta_choice_push(machine, fallback)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *truth = machine->host.boolean_value
                ? machine->host.boolean_value(
                      machine->host.context, &machine->heap, true)
                : petta_semantics_boolean_value(
                      &machine->heap, true);
            if (!truth ||
                !petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind = PETTA_GOAL_EQUAL_COMMIT,
                        .barrier = goal->barrier,
                        .first = truth,
                        .second = expected,
                        .choice_index = machine->choice_len - 1u,
                    }) ||
                !petta_push_solve(
                    machine, open, rigid, goal->barrier)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_EQUAL_READY, 1u, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.case_text && nargs == 2u) {
        Atom *branches = expression->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR)
            return petta_machine_unify_resolved(
                machine, expression, expected);
        Atom *value = petta_fresh_variable(machine);
        if (!value) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        size_t entry_choice_len = machine->choice_len;
        size_t default_choice_index = SIZE_MAX;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u ||
                branch->expr.elems[0]->kind != ATOM_SYMBOL ||
                branch->expr.elems[0]->sym_id != g_builtin_syms.empty) {
                continue;
            }
            PettaChoice default_choice = {
                .kind = PETTA_CHOICE_CASE_DEFAULT,
                .trail = search_context_save(&machine->search),
                .goal_height = machine->goal_len,
                .barrier = goal->barrier,
                .as.case_default = {
                    .saw_answer = false,
                    .expression = branch->expr.elems[1],
                    .expected = expected,
                    .plan = petta_plan_child(
                        petta_plan_child(
                            petta_plan_child(plan, 2u), index),
                        1u),
                },
            };
            if (!petta_choice_push(machine, default_choice)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            default_choice_index = machine->choice_len - 1u;
            break;
        }
        if (!petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_CASE_SELECT,
                    .barrier = goal->barrier,
                    .first = value,
                    .second = branches,
                    .third = expected,
                    .second_plan = petta_plan_child(plan, 2u),
                    .choice_index = default_choice_index,
                }) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[1], value,
                goal->barrier, petta_plan_child(plan, 1u))) {
            petta_choice_truncate(machine, entry_choice_len);
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    bool numeric_min = head_id == g_builtin_syms.petta_min;
    bool numeric_max = head_id == g_builtin_syms.petta_max;
    if ((numeric_min || numeric_max) && nargs == 2u) {
        Atom *left = petta_fresh_variable(machine);
        Atom *right = petta_fresh_variable(machine);
        Atom *comparison = left && right
            ? atom_expr3(
                  &machine->heap,
                  atom_symbol_id(
                      &machine->heap,
                      numeric_min
                          ? g_builtin_syms.op_le
                          : g_builtin_syms.op_ge),
                  left, right)
            : NULL;
        Atom *selection_elements[4] = {
            atom_symbol_id(
                &machine->heap,
                symbol_intern_cstr(g_symbols, "if")),
            comparison,
            left,
            right,
        };
        Atom *selection = comparison
            ? atom_expr(
                  &machine->heap, selection_elements, 4u)
            : NULL;
        if (!selection ||
            !petta_push_solve(
                machine, selection, expected,
                goal->barrier) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[2], right,
                goal->barrier, petta_plan_child(plan, 2u)) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[1], left,
                goal->barrier, petta_plan_child(plan, 1u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (form == PETTA_FORM_IF &&
        (nargs == 2u || nargs == 3u)) {
        Atom *condition = petta_fresh_variable(machine);
        Atom *otherwise = nargs == 3u
            ? expression->expr.elems[3]
            : atom_empty(&machine->heap);
        if (!petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_IF_SELECT,
                    .barrier = goal->barrier,
                    .first = condition,
                    .second = expression->expr.elems[2],
                    .third = otherwise,
                    .fourth = expected,
                    .second_plan = petta_plan_child(plan, 2u),
                    .third_plan = nargs == 3u
                        ? petta_plan_child(plan, 3u) : NULL,
                })) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_push_solve_planned(
            machine, expression->expr.elems[1],
            condition, goal->barrier,
            petta_plan_child(plan, 1u));
    }

    if (form == PETTA_FORM_LET && nargs == 3u &&
        petta_direct_let_binding_count_only(
            machine, expression->expr.elems[1],
            expression->expr.elems[2],
            expression->expr.elems[3],
            petta_plan_child(plan, 3u))) {
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[3], expected,
                goal->barrier, petta_plan_child(plan, 3u)) ||
            !petta_push_counted_collection_planned(
                machine, expression->expr.elems[2],
                expression->expr.elems[1], goal->barrier,
                petta_plan_child(plan, 2u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        machine->stats.count_aggregate_let_fusions++;
        return true;
    }

    if ((form == PETTA_FORM_LET || form == PETTA_FORM_CHAIN) &&
        nargs == 3u) {
        Atom *joined = petta_fresh_variable(machine);
        bool solve_right_first =
            form == PETTA_FORM_LET &&
            petta_machine_is_rigid_data(
                machine, expression->expr.elems[2]) &&
            !petta_machine_is_rigid_data(
                machine, expression->expr.elems[1]);
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[3],
                expected, goal->barrier,
                petta_plan_child(plan, 3u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (solve_right_first) {
            if (!petta_push_solve_planned(
                    machine, expression->expr.elems[1],
                    joined, goal->barrier,
                    petta_plan_child(plan, 1u)) ||
                !petta_push_solve_planned(
                    machine, expression->expr.elems[2],
                    joined, goal->barrier,
                    petta_plan_child(plan, 2u))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        } else if (!petta_push_solve_planned(
                       machine, expression->expr.elems[2],
                       joined, goal->barrier,
                       petta_plan_child(plan, 2u)) ||
                   !petta_push_solve_planned(
                       machine, expression->expr.elems[1],
                       joined, goal->barrier,
                       petta_plan_child(plan, 1u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id == g_builtin_syms.let_star && nargs == 2u) {
        Atom *bindings = expression->expr.elems[1];
        const PettaPlanNode *bindings_plan =
            petta_plan_child(plan, 1u);
        const PettaPlanNode *body_plan =
            petta_plan_child(plan, 2u);
        if (!bindings || bindings->kind != ATOM_EXPR)
            return petta_machine_unify_resolved(
                machine, expression, expected);
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[2],
                expected, goal->barrier,
                petta_plan_child(plan, 2u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = bindings->expr.len;
             index > 0u; index--) {
            Atom *binding = bindings->expr.elems[index - 1u];
            if (!binding || binding->kind != ATOM_EXPR ||
                binding->expr.len != 2u) {
                *failure = PETTA_MACHINE_STEP_EXHAUSTED;
                return false;
            }
            const PettaPlanNode *binding_plan =
                petta_plan_child(
                    petta_plan_child(plan, 1u),
                    index - 1u);
            bool count_only =
                petta_let_binding_count_only(
                    machine, bindings, bindings_plan,
                    index - 1u,
                    expression->expr.elems[2], body_plan);
            bool pushed = count_only
                ? petta_push_counted_collection_planned(
                      machine, binding->expr.elems[1],
                      binding->expr.elems[0],
                      goal->barrier,
                      petta_plan_child(binding_plan, 1u))
                : petta_push_solve_planned(
                      machine, binding->expr.elems[1],
                      binding->expr.elems[0],
                      goal->barrier,
                      petta_plan_child(binding_plan, 1u));
            if (!pushed) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (count_only)
                machine->stats.count_aggregate_let_fusions++;
        }
        return true;
    }

    if ((form == PETTA_FORM_APPEND ||
         head_id == g_builtin_syms.union_atom) &&
        nargs == 2u) {
        if (expected->kind == ATOM_EXPR) {
            return petta_machine_append(
                machine, expression->expr.elems[1],
                expression->expr.elems[2], expected,
                goal->barrier);
        }
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_APPEND_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_CONS && nargs == 2u) {
        if (expected->kind == ATOM_EXPR) {
            return petta_machine_cons(
                machine, expression->expr.elems[1],
                expression->expr.elems[2], expected,
                goal->barrier);
        }
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_CONS_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (((form == PETTA_FORM_MAP_ATOM &&
          (nargs == 2u || nargs == 3u)) ||
         (form == PETTA_FORM_MAPLIST && nargs == 2u))) {
        Atom *items_expression =
            form == PETTA_FORM_MAPLIST
                ? expression->expr.elems[2]
                : expression->expr.elems[1];
        Atom *items = petta_fresh_variable(machine);
        if (!items ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_MAP_ATOM_READY,
                    .barrier = goal->barrier,
                    .first = items,
                    .second = form == PETTA_FORM_MAPLIST
                        ? expression->expr.elems[1]
                        : expression->expr.elems[2],
                    .third = form == PETTA_FORM_MAP_ATOM &&
                             nargs == 3u
                        ? expression->expr.elems[3] : NULL,
                    .fourth = expected,
                }) ||
            !petta_push_solve(
                machine, items_expression,
                items, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_ID && nargs == 1u) {
        return petta_push_solve_planned(
            machine, expression->expr.elems[1],
            expected, goal->barrier,
            petta_plan_child(plan, 1u));
    }

    if ((form == PETTA_FORM_FIRST_FROM_PAIR ||
         form == PETTA_FORM_SECOND_FROM_PAIR) &&
        nargs == 1u) {
        Atom *pair = petta_fresh_variable(machine);
        if (!pair ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_PAIR_SELECT,
                    .barrier = goal->barrier,
                    .first = pair,
                    .second = expected,
                    .choice_index =
                        form == PETTA_FORM_FIRST_FROM_PAIR
                            ? 0u : 1u,
                }) ||
            !petta_push_solve(
                machine, expression->expr.elems[1],
                pair, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_INT_ADD && nargs == 2u) {
        return petta_machine_integer_add(
            machine, expression->expr.elems[1],
            expression->expr.elems[2], expected);
    }

    if (form == PETTA_FORM_IS_MEMBER && nargs == 2u) {
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_MEMBER_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_CALL && nargs == 1u) {
        return petta_push_force(
            machine, expression->expr.elems[1],
            expected, goal->barrier);
    }

    if (form == PETTA_FORM_EVAL && nargs == 2u) {
        /* (eval <term> <space>) — explicit-context spelling; the rewritten
         * evalc call takes the machine's ordinary foreign-builtin path. */
        Atom *rewritten = atom_expr(
            &machine->heap,
            (Atom *[]) {
                atom_symbol_id(&machine->heap, g_builtin_syms.evalc),
                expression->expr.elems[1],
                expression->expr.elems[2]
            }, 3u);
        if (!rewritten) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_push_force(machine, rewritten, expected, goal->barrier);
    }

    if ((form == PETTA_FORM_EVAL ||
         form == PETTA_FORM_REDUCE) &&
        nargs == 1u) {
        /*
         * Dynamic evaluation is two-staged: the argument computes a TERM,
         * and that term then runs as an expression.  A cons-built term
         * arrives as an open-cons carrier, so the spine is normalized to a
         * flat expression before the second evaluation — this is how
         * (reduce (cons Formula Args)) applies the formula, exactly as the
         * reference's homoiconic reduce/2 does over Prolog lists.
         */
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_REDUCE_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * PeTTa catch is a delimited Error-to-value conversion.  Evaluate the
     * body through a private result slot so a raised Error bypasses any
     * result-type constraint between the body and this delimiter.  The
     * marker also carries the entry trail and choice depth: raising abandons
     * only the protected body's alternatives and bindings.
     */
    if (form == PETTA_FORM_CATCH && nargs == 1u) {
        Atom *result = petta_fresh_variable(machine);
        size_t goal_mark = machine->goal_len;
        size_t goal_trail_mark = machine->goal_trail_len;
        if (!result ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_CATCH_READY,
                    .barrier = goal->barrier,
                    .first = result,
                    .second = expected,
                    .choice_index = machine->choice_len,
                    .catch_trail =
                        search_context_save(&machine->search),
                    .catch_type_obligation_mark =
                        machine->type_obligation_len,
                }) ||
            !petta_push_solve_planned(
                machine, expression->expr.elems[1],
                result, goal->barrier,
                petta_plan_child(plan, 1u))) {
            (void)petta_goal_trail_rollback(
                machine, goal_trail_mark);
            machine->goal_len = goal_mark;
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_TRANSLATE_PREDICATE &&
        nargs == 1u) {
        Atom *predicate = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Space *predicate_space = NULL;
        Atom *predicate_pattern = NULL;
        if (petta_machine_space_predicate_view(
                machine, predicate,
                &predicate_space, &predicate_pattern)) {
            Atom *success =
                petta_semantics_success_value(
                    &machine->heap);
            return success &&
                   petta_machine_start_match_choice(
                       machine, predicate_space,
                       predicate_pattern, success,
                       expected, goal->barrier, NULL);
        }
        bool recognized = false;
        if (!petta_machine_push_native_predicate(
                machine, predicate, expected,
                goal->barrier, &recognized)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return recognized ||
               petta_machine_unify_resolved(
                   machine, expression, expected);
    }

    /*
     * Foreign predicates are an optional boundary around the native PeTTa
     * machine, never an alternate evaluator.  Control forms remain machine
     * goals; the adapter receives only the predicate-shaped operation and
     * returns an ordinary bag of outcomes.
     */
    if (form == PETTA_FORM_IMPORT_PROLOG_FUNCTION &&
        nargs == 1u) {
        /*
         * The imported-name argument is an ordinary computation — the
         * reference distributes (import_prolog_function (superpose (a b)))
         * into one registration per branch — so it must evaluate before
         * the boundary sees it; the raw superpose expression would
         * otherwise reach the registry as a single unrecognizable name.
         */
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_EXTENSION_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (form == PETTA_FORM_CALL_PREDICATE &&
        nargs == 1u) {
        Atom *predicate = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Atom *predicate_body = NULL;
        bool native_recognized = false;
        if (petta_machine_predicate_wrapper_body(
                predicate, &predicate_body) &&
            !petta_machine_push_native_predicate(
                machine, predicate_body, expected,
                goal->barrier, &native_recognized)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (native_recognized)
            return true;

        bool foreign_recognized = false;
        bool dispatched = petta_machine_try_extension_call(
            machine, expression, expected, goal->barrier,
            &foreign_recognized, failure);
        if (foreign_recognized || !dispatched)
            return dispatched;
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    if ((form == PETTA_FORM_ASSERTA_PREDICATE ||
         form == PETTA_FORM_ASSERTZ_PREDICATE ||
         form == PETTA_FORM_RETRACT_PREDICATE) &&
        nargs == 1u) {
        Atom *predicate = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        Atom *predicate_body = NULL;
        if (!petta_machine_predicate_wrapper_body(
                predicate, &predicate_body)) {
            if (!petta_push_evaluated_expression_planned(
                    machine, expression, expected,
                    PETTA_GOAL_EXTENSION_READY, 1u,
                    goal->barrier, plan)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }

        bool recognized = false;
        bool dispatched = petta_machine_try_extension_call(
            machine, expression, expected, goal->barrier,
            &recognized, failure);
        if (recognized || !dispatched)
            return dispatched;
        return petta_machine_unify_resolved(
            machine, expression, expected);
    }

    if (form == PETTA_FORM_TABLED && nargs == 1u) {
        Atom *relation = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        if (!relation || relation->kind != ATOM_EXPR ||
            relation->expr.len == 0u ||
            relation->expr.elems[0]->kind != ATOM_SYMBOL ||
            !machine->host.tabled_relation_set ||
            !machine->host.tabled_relation_set(
                machine->host.context,
                relation->expr.elems[0]->sym_id,
                relation->expr.len - 1u, true)) {
            return petta_machine_unify_resolved(
                machine, expression, expected);
        }
        return petta_machine_boolean(
            machine, true, expected);
    }

    if ((form == PETTA_FORM_ADD_TRANSLATOR_RULE ||
         form == PETTA_FORM_REMOVE_TRANSLATOR_RULE) &&
        nargs == 1u) {
        Atom *rule = petta_machine_apply_bindings(machine,
            environment, &machine->heap,
            expression->expr.elems[1]);
        if (!rule || rule->kind != ATOM_SYMBOL ||
            !machine->host.translator_rule_set ||
            !machine->host.translator_rule_set(
                machine->host.context, rule->sym_id,
                form == PETTA_FORM_ADD_TRANSLATOR_RULE)) {
            return false;
        }
        return petta_machine_boolean(
            machine, true, expected);
    }

    if (form == PETTA_FORM_CUT && nargs == 0u) {
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_CUT,
                .barrier = goal->barrier,
                .first = expected,
            });
    }

    /*
     * A PeTTa sequence is relational conjunction in source order.  Keep it
     * inside the search machine so every child retains its compiled
     * occurrence role and ordinary choice-point continuations preserve
     * nondeterminism.  PROG1 differs only by retaining the first answer while
     * the remaining goals run for their effects.
     */
    if (form == PETTA_FORM_PROGN ||
        form == PETTA_FORM_PROG1) {
        if (nargs == 0u)
            return false;
        if (form == PETTA_FORM_PROGN) {
            if (!petta_push_solve_planned(
                    machine, expression->expr.elems[nargs],
                    expected, goal->barrier,
                    petta_plan_child(plan, nargs))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            for (CettaExprIndex index = nargs;
                 index > 1u; index--) {
                Atom *ignored = petta_fresh_variable(machine);
                CettaExprIndex child = index - 1u;
                if (!ignored ||
                    !petta_push_solve_planned(
                        machine, expression->expr.elems[child],
                        ignored, goal->barrier,
                        petta_plan_child(plan, child))) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
            return true;
        }

        Atom *saved = petta_fresh_variable(machine);
        if (!saved ||
            !petta_push_unify(
                machine, saved, expected, goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        for (CettaExprIndex index = nargs;
             index > 1u; index--) {
            Atom *ignored = petta_fresh_variable(machine);
            if (!ignored ||
                !petta_push_solve_planned(
                    machine, expression->expr.elems[index],
                    ignored, goal->barrier,
                    petta_plan_child(plan, index))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        if (!petta_push_solve_planned(
                machine, expression->expr.elems[1],
                saved, goal->barrier,
                petta_plan_child(plan, 1u))) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    uint32_t boolean_arity = 0u;
    if (petta_semantics_boolean_relation_arity(
            head_id, &boolean_arity) &&
        nargs == (CettaExprLen)boolean_arity) {
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_BOOLEAN_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * A registered translator relation consumes source syntax, produces a
     * replacement program, and only then evaluates that program.  Dispatch
     * it before the generic strict-call path evaluates the original
     * arguments; otherwise binders such as `(for $x collection body)` lose
     * both their variable identity and their unevaluated body before the
     * translator clause can construct the replacement.
     */
    if (head_id != SYMBOL_ID_NONE &&
        form == PETTA_FORM_NONE &&
        machine->host.translator_rule_contains &&
        machine->host.translator_rule_contains(
            machine->host.context, head_id)) {
        return petta_machine_start_clause_choice(
            machine, expression, expected,
            goal->barrier, true, false,
            NULL, NULL, 0u, 0u);
    }

    /*
     * A name becomes an extension function only after an explicit import.
     * PeTTa's function convention evaluates every supplied argument before
     * appending the result argument at the extension boundary.  Reify that
     * evaluation here rather than letting the adapter observe source
     * expressions: an inline computation and the same computation named by
     * `let` must be observationally identical.
     *
     * Under-applications fall through to the ordinary partial-application
     * rule below.  The ready continuation calls the adapter directly, so
     * scheduling it here cannot recursively re-enter this dispatch case.
     */
    if (head_id != SYMBOL_ID_NONE &&
        form == PETTA_FORM_NONE &&
        (machine->host.native_named_arity ||
         machine->host.foreign_named_arity)) {
        /*
         * Registry membership carries call authority only where the compiled
         * occurrence is a static or dynamic call.  A source occurrence that
         * merely spells a name imported later remains data, matching PeTTa's
         * source-ordered translation.
         */
        bool call_position =
            !plan ||
            plan->role == PETTA_PLAN_STATIC_CALL ||
            plan->role == PETTA_PLAN_DYNAMIC_CALL;
        PeTTaNamedArity extension_arity = {0};
        if (call_position) {
            extension_arity =
                plan && plan->role == PETTA_PLAN_STATIC_CALL
                    ? petta_machine_extension_named_arity_resolved(
                          machine, head_id, nargs)
                    : petta_machine_extension_named_arity(
                          machine, head_id, nargs);
        }
        if (call_position &&
            extension_arity.known && extension_arity.exact) {
            if (!petta_push_evaluated_expression_planned(
                    machine, expression, expected,
                    PETTA_GOAL_EXTENSION_READY, 1u,
                    goal->barrier, plan)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
    }

    /*
     * PeTTa curries every known fixed-arity callable.  Decide
     * under-application before dispatching a grounded host operator:
     * otherwise a valid partial such as `(=alpha 1)` reaches the host's
     * exact-arity check and is irreversibly converted into an error.
     */
    PettaPartialDecision partial =
        petta_machine_named_partial_decision(machine, expression);
    if (partial == PETTA_PARTIAL_INVALIDATED) {
        *failure = PETTA_MACHINE_STEP_INVALIDATED;
        return false;
    }
    if (partial == PETTA_PARTIAL_OVERAPPLIED) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_OVERAPPLICATION_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (partial == PETTA_PARTIAL_YES) {
        if (!petta_push_evaluated_expression(
                machine, expression, expected,
                PETTA_GOAL_PARTIAL_READY, 1u,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * The generic strict-call path allocates result slots and one SOLVE goal
     * per argument.  When the compiled occurrence proves that every argument
     * is already a value, those goals are administrative identities.  Run the
     * same grounded dispatcher immediately and retain the ordinary path as
     * the NULL/not-ready fallback.
     */
    if (head_id != SYMBOL_ID_NONE &&
        grounded_op_is_type_pure(head_id) &&
        nargs <= UINT32_MAX &&
        petta_machine_all_arguments_immediate(
            expression, plan)) {
        Atom *direct = grounded_dispatch(
            &machine->heap, head,
            expression->expr.elems + 1u,
            (uint32_t)nargs);
        if (direct) {
            if (atom_is_empty(direct))
                return false;
            bool truth = false;
            if (petta_semantics_truth_value(direct, &truth)) {
                direct = machine->host.boolean_value
                    ? machine->host.boolean_value(
                          machine->host.context,
                          &machine->heap, truth)
                    : petta_semantics_boolean_value(
                          &machine->heap, truth);
            }
            return direct &&
                   petta_machine_unify_resolved(
                       machine, direct, expected);
        }
    }

    PettaMachineHostMode host_mode = machine->host.classify
        ? machine->host.classify(
              machine->host.context, machine->space, expression)
        : PETTA_MACHINE_HOST_NONE;
    /* A ready intrinsic `get-type` answer is a language-analysis service,
     * not a nested evaluation episode.  Keep explicit user extensions on
     * the relational path below, and retain strict child evaluation whenever
     * the authored occurrence is not already a value. */
    if (head_id == g_builtin_syms.get_type && nargs == 1u &&
        host_mode == PETTA_MACHINE_HOST_STRICT_APPLICATION &&
        machine->host.get_type &&
        petta_machine_planned_value_ready(
            machine, expression->expr.elems[1],
            petta_plan_child(plan, 1u))) {
        return petta_machine_start_intrinsic_get_type(
            machine, expression->expr.elems[1], expected,
            goal->barrier, failure);
    }
    /*
     * A ready host form is a control/aggregate form whose children cannot be
     * evaluated uniformly.  Give that semantic owner precedence over the
     * conservative equation-head may-match index; wildcard equations must
     * not turn a built-in fold, map, quantifier, or lambda into an ordinary
     * clause call.
     */
    if (host_mode == PETTA_MACHINE_HOST_READY_APPLICATION ||
        host_mode == PETTA_MACHINE_HOST_READY_OVERRIDE) {
        return petta_goal_push(
            machine,
            (PettaGoal){
                .kind = PETTA_GOAL_HOST_READY,
                .barrier = goal->barrier,
                .first = expression,
                .second = expected,
            });
    }
    if (host_mode ==
            PETTA_MACHINE_HOST_READY_RELATIONAL_EXTENSION) {
        if (!petta_machine_schedule_relational_extension(
                machine, expression, expected,
                goal->barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (host_mode ==
            PETTA_MACHINE_HOST_STRICT_RELATIONAL_EXTENSION) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_RELATIONAL_EXTENSION_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (host_mode == PETTA_MACHINE_HOST_STRICT_APPLICATION) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_HOST_STRICT_READY, 1u, goal->barrier,
                plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }
    if (host_mode ==
            PETTA_MACHINE_HOST_STRICT_FIRST_APPLICATION) {
        if (expression->expr.len < 2u ||
            !petta_push_evaluated_expression_range_planned(
                machine, expression, expected,
                PETTA_GOAL_HOST_READY, 1u, 2u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * A compiled pure-grounded occurrence carries enough authority to run
     * without an evaluator adapter.  The immediate case above remains
     * allocation-free; this case evaluates only the operands that are not
     * ready yet and rejoins the same pure dispatcher.  Effects and live
     * state reads remain owned by the host, while unknown heads remain data.
     */
    if (head_id != SYMBOL_ID_NONE &&
        grounded_op_is_type_pure(head_id) && plan &&
        plan->execution == PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS) {
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_HOST_STRICT_READY, 1u,
                goal->barrier, plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (head_id != SYMBOL_ID_NONE &&
        space_equations_may_match_known_head(
            machine->space, head_id)) {
        Atom **declared_types = NULL;
        uint32_t declared_type_count = space_get_declared_types(
            machine->space, &machine->heap, head,
            &declared_types);
        bool has_applicable_type = false;
        for (uint32_t index = 0u;
             index < declared_type_count; index++) {
            if (petta_machine_type_signature_applies(
                    declared_types[index], nargs)) {
                has_applicable_type = true;
                break;
            }
        }
        if (has_applicable_type) {
            return petta_machine_start_typed_call_choice(
                machine, expression, expected, declared_types,
                declared_type_count, goal->barrier, plan);
        }
        free(declared_types);
        if (petta_machine_relation_slots_ready(
                machine, expression, plan)) {
            BindingsBuilder *builder =
                search_context_builder(&machine->search);
            machine->stats.relation_slot_frame_entries++;
            machine->stats.relation_slot_operands_reused += nargs;
            return petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_CALL_READY_RESOLVED,
                    .barrier = goal->barrier,
                    .first = expression,
                    .second = expected,
                    .plan = plan,
                    .activation_template =
                        goal->activation_template,
                    .equation_query_source =
                        goal->equation_query_source,
                    .activation_epoch =
                        goal->activation_epoch,
                    .activation_first_entry =
                        goal->activation_first_entry,
                    .binding_growth_mark = builder->growth_count,
                });
        }
        if (!petta_push_evaluated_expression_planned(
                machine, expression, expected,
                PETTA_GOAL_CALL_READY, 1u, goal->barrier,
                plan)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    /*
     * A source occurrence may have been compiled while its head was
     * registered and then reached after the final defining clause was
     * removed.  PeTTa's runtime dispatcher unregisters such a head and
     * reifies the application as data.  Do the same only when no live
     * equation can own the head; an existing relation whose clauses merely
     * fail must still fail rather than turning into syntax.
     */
    if (plan && plan->role == PETTA_PLAN_STATIC_CALL)
        return petta_machine_unify_resolved(
            machine, expression, expected);

    return petta_push_evaluated_expression_planned(
        machine, expression, expected,
        head_id == SYMBOL_ID_NONE
            ? PETTA_GOAL_APPLY_READY
            : PETTA_GOAL_UNIFY,
        head_id == SYMBOL_ID_NONE ? 0u : 1u,
        goal->barrier, plan);
}

static bool petta_machine_dispatch_goal(
    PettaMachineImpl *machine, PettaGoal goal,
    PettaMachineStep *failure) {
    machine->stats.transitions++;
    petta_machine_record_goal_class(
        &machine->stats, goal.kind);
    if (petta_machine_trace_enabled()) {
        fprintf(stderr, "[petta-machine] goal=%d barrier=%u choices=%zu ",
                (int)goal.kind, goal.barrier, machine->choice_len);
        if (goal.plan) {
            fprintf(
                stderr,
                "plan=%d execution=%d contains-call=%u open=%u ",
                (int)goal.plan->role,
                (int)goal.plan->execution,
                goal.plan->contains_call ? 1u : 0u,
                goal.plan->open_template_admitted ? 1u : 0u);
        }
        if (goal.first)
            atom_print(goal.first, stderr);
        if (goal.second) {
            fputs(" => ", stderr);
            atom_print(goal.second, stderr);
        }
        fputc('\n', stderr);
    }
    if (goal.kind == PETTA_GOAL_SOLVE_ACTIVATION)
        return petta_machine_dispatch_activation_solve(
            machine, &goal, failure);

    if (goal.kind == PETTA_GOAL_SOLVE ||
        goal.kind == PETTA_GOAL_FORCE ||
        goal.kind == PETTA_GOAL_SOLVE_COUNTED_COLLECTION)
        return petta_machine_dispatch_solve(machine, &goal, failure);

    if (goal.kind == PETTA_GOAL_SET_ANSWER_WEIGHT) {
        if (!machine->count_only_emission ||
            goal.answer_weight == 0u ||
            machine->pending_answer_weight != 0u) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        machine->pending_answer_weight =
            goal.answer_weight;
        return true;
    }

    const Bindings *environment =
        search_context_bindings(&machine->search);

    /* Unification consumes the live trail directly.  Keep its operands in
     * source form instead of eagerly substituting both complete terms here;
     * petta_machine_unify resolves root discriminators and the authoritative
     * matcher dereferences nested variables on demand. */
    if (goal.kind == PETTA_GOAL_UNIFY ||
        goal.kind == PETTA_GOAL_CATCH_READY) {
        Atom *left = goal.first;
        Atom *right = goal.second;
        if (goal.kind == PETTA_GOAL_UNIFY &&
            goal.activation_source_fields != 0u) {
            if (goal.activation_source_fields ==
                    PETTA_ACTIVATION_SOURCE_FIRST) {
                bool handled = false;
                bool matched =
                    petta_machine_unify_activation_source(
                        machine, &goal, goal.first,
                        goal.second, &handled);
                if (handled)
                    return matched;
            }
            if ((goal.activation_source_fields &
                    PETTA_ACTIVATION_SOURCE_FIRST) != 0u) {
                left = petta_machine_materialize_activation_source(
                    machine, &goal, goal.first);
            }
            if ((goal.activation_source_fields &
                    PETTA_ACTIVATION_SOURCE_SECOND) != 0u) {
                right = petta_machine_materialize_activation_source(
                    machine, &goal, goal.second);
            }
            if (!left || !right) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        return petta_machine_unify(machine, left, right);
    }

    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    bool ready_operands_still_resolved =
        goal.kind == PETTA_GOAL_CALL_READY_RESOLVED &&
        goal.binding_growth_mark != UINT64_MAX &&
        builder->growth_count == goal.binding_growth_mark;
    bool equation_query_view_ready =
        ready_operands_still_resolved &&
        goal.equation_query_source && goal.activation_template &&
        goal.activation_epoch != 0u;
    Atom *first = ready_operands_still_resolved
        ? goal.first
        : petta_machine_apply_bindings(
              machine, environment, &machine->heap, goal.first);
    Atom *second = !goal.second
        ? NULL
        : (goal.activation_source_fields &
               PETTA_ACTIVATION_SOURCE_SECOND) != 0u
            ? goal.second
        : ready_operands_still_resolved
            ? goal.second
            : petta_machine_apply_bindings(
                  machine, environment, &machine->heap, goal.second);

    /* Generic grounded and host operators consume PeTTa values, never the
     * machine's private open-cons carrier.  Earlier slot optimizations
     * decline when a carrier is present; this common boundary performs the
     * one observable materialization needed by both direct and fallback
     * host dispatch.  Quoted syntax and opaque callable values remain
     * untouched by the shared materializer. */
    if ((goal.kind == PETTA_GOAL_HOST_READY ||
         goal.kind == PETTA_GOAL_HOST_STRICT_READY) &&
        first &&
        petta_semantics_value_contains_observable_open_cons(first)) {
        first = petta_semantics_materialize_value(
            &machine->heap, first);
        if (!first) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }

    if (goal.kind == PETTA_GOAL_TYPE_ACCEPT ||
        goal.kind == PETTA_GOAL_TYPE_MATCH)
        return petta_machine_type_accept(
            machine, first, second, goal.barrier,
            goal.kind == PETTA_GOAL_TYPE_MATCH, failure);

    if (goal.kind == PETTA_GOAL_TYPE_ASCRIBE_READY)
        return petta_machine_type_ascribe_ready(
            machine, first, second, goal.barrier, failure);

    if (goal.kind == PETTA_GOAL_TYPE_REQUIRE_READY) {
        bool matched = false;
        if (!petta_semantics_truth_value(first, &matched) ||
            !goal.third) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (matched) {
            if (goal.choice_index < machine->choice_len) {
                PettaChoice *guard_choice =
                    &machine->choices[goal.choice_index];
                if (guard_choice->kind !=
                    PETTA_CHOICE_EQUAL_DEFAULT) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                uint64_t obligation_id = guard_choice->as
                    .equal_default.type_obligation_id;
                if (obligation_id != 0u) {
                    PettaMachineAuthorityToken confirmed_authority;
                    if (!petta_machine_authority_token(
                            machine, &confirmed_authority)) {
                        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                        return false;
                    }
                    PettaTypeObligation *obligation =
                        petta_machine_type_obligation_by_id(
                            machine, obligation_id);
                    const Bindings *guard_environment =
                        search_context_bindings(&machine->search);
                    Atom *resolved_value = obligation
                        ? petta_machine_apply_bindings(
                              machine, guard_environment,
                              &machine->heap, obligation->value)
                        : NULL;
                    Atom *resolved_formal = obligation
                        ? petta_machine_apply_bindings(
                              machine, guard_environment,
                              &machine->heap, obligation->formal)
                        : NULL;
                    bool same_value = resolved_value &&
                        guard_choice->as.equal_default.guarded_value &&
                        atom_eq(
                            resolved_value,
                            guard_choice->as.equal_default.guarded_value);
                    if (!obligation || !resolved_formal || !same_value ||
                        space_global_mutation_epoch() !=
                            guard_choice->as.equal_default.authority_epoch ||
                        petta_machine_analysis_policy(machine) !=
                            guard_choice->as.equal_default.authority_policy ||
                        !space_read_token_matches_live_space(
                            guard_choice->as.equal_default.authority_read,
                            machine->space) ||
                        !petta_machine_authority_token_eq(
                            &guard_choice->as.equal_default.authority,
                            &confirmed_authority)) {
                        /* A relational answer established under changed
                         * authority is never published as a proof receipt.
                         * The caller restarts the invalidated computation. */
                        *failure = PETTA_MACHINE_STEP_INVALIDATED;
                        return false;
                    }
                    SpaceReadToken checked_read =
                        guard_choice->as.equal_default.authority_read;
                    uint64_t checked_epoch =
                        guard_choice->as.equal_default.authority_epoch;
                    uint32_t checked_policy =
                        guard_choice->as.equal_default.authority_policy;
                    PettaMachineAuthorityToken checked_authority =
                        guard_choice->as.equal_default.authority;
                    petta_choice_truncate(
                        machine, goal.choice_index);
                    obligation = petta_machine_type_obligation_by_id(
                        machine, obligation_id);
                    if (!obligation) {
                        *failure = PETTA_MACHINE_STEP_INVALIDATED;
                        return false;
                    }
                    obligation->checked_value = resolved_value;
                    obligation->checked_formal = resolved_formal;
                    obligation->checked_read = checked_read;
                    obligation->checked_epoch = checked_epoch;
                    obligation->checked_policy = checked_policy;
                    obligation->checked_authority = checked_authority;
                    obligation->state =
                        PETTA_TYPE_OBLIGATION_RELATIONAL_ESTABLISHED;
                    obligation->relational_required = true;
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_PETTA_TYPE_OBLIGATION_GUARD_ESTABLISHED);
                    machine->type_obligation_check_pending = true;
                    return true;
                }
                petta_choice_truncate(machine, goal.choice_index);
            }
            return true;
        }
        PettaAnalysisResult result = {
            .verdict = PETTA_ANALYSIS_REFUTED,
            .reason = PETTA_ANALYSIS_REASON_MISMATCH,
            .fault = PETTA_ANALYSIS_FAULT_NONE,
        };
        if (!petta_machine_raise_typecheck_error(
                machine, second, goal.third, &result)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (goal.kind == PETTA_GOAL_CLAUSE_ONLY)
        return petta_machine_start_clause_choice(
            machine, first, second, goal.barrier, true, false,
            NULL, NULL, 0u, 0u);

    if (goal.kind == PETTA_GOAL_RELATIONAL_EXTENSION_READY) {
        if (!petta_machine_schedule_relational_extension(
                machine, first, second, goal.barrier)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (goal.kind == PETTA_GOAL_NAMED_STATE_READY) {
        if (!machine->host.named_state ||
            goal.choice_index > (size_t)PETTA_FORM_CHAIN) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        Atom *expected = goal.third
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.third)
            : NULL;
        if (!first || !expected ||
            (goal.second && !second)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        OutcomeSet *outcomes = cetta_malloc(sizeof(*outcomes));
        outcome_set_init_with_owner(outcomes, &machine->heap);
        bool applied = machine->host.named_state(
            machine->host.context, machine->space,
            &machine->heap, (PeTTaForm)goal.choice_index,
            first, second, environment, outcomes);
        if (!applied ||
            !petta_machine_factor_outcome_prefixes(
                machine, outcomes, environment)) {
            outcome_set_free(outcomes);
            free(outcomes);
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        return petta_machine_start_outcome_choice(
            machine, outcomes, expected, goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_EXTENSION_READY) {
        bool recognized = false;
        bool dispatched = petta_machine_try_extension_call(
            machine, first, second, goal.barrier,
            &recognized, failure);
        if (recognized || !dispatched)
            return dispatched;
        return petta_machine_unify(machine, first, second);
    }

    if (goal.kind == PETTA_GOAL_APPLY_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len == 0u) {
            return petta_machine_unify(machine, first, second);
        }
        Atom *head = first->expr.elems[0];
        Atom *expanded_partial = NULL;
        bool partial_head = false;
        if (!petta_machine_expand_partial_application(
                machine, first,
                &expanded_partial, &partial_head)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        if (partial_head) {
            return petta_push_solve(
                machine, expanded_partial,
                second, goal.barrier);
        }
        SymbolId head_id = head->kind == ATOM_SYMBOL
            ? head->sym_id : SYMBOL_ID_NONE;
        PeTTaForm form = head_id == SYMBOL_ID_NONE
            ? PETTA_FORM_NONE : petta_semantics_form(head_id);
        bool callable =
            form != PETTA_FORM_NONE ||
            (head_id != SYMBOL_ID_NONE &&
             space_equations_may_match_known_head(
                 machine->space, head_id)) ||
            petta_machine_extension_callable(machine, first) ||
            (machine->host.classify &&
             machine->host.classify(
                 machine->host.context, machine->space, first) !=
                 PETTA_MACHINE_HOST_NONE);
        return callable
            ? petta_push_solve(
                  machine, first, second, goal.barrier)
            : petta_machine_unify(machine, first, second);
    }

    if (goal.kind == PETTA_GOAL_LAMBDA_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len == 0u) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom *next = cetta_petta_apply_ready_callable(
            &machine->heap, first->expr.elems[0],
            first->expr.elems + 1u,
            first->expr.len - 1u);
        if (!next) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_push_solve(
            machine, next, second, goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_PARTIAL_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len == 0u) {
            return false;
        }
        Atom *partial = petta_semantics_partial_value(
            &machine->heap, first->expr.elems[0],
            first->expr.elems + 1u, first->expr.len - 1u);
        return partial &&
               petta_machine_unify(machine, partial, second);
    }

    if (goal.kind == PETTA_GOAL_OVERAPPLICATION_READY) {
        Atom *error = petta_machine_overapplication_error(
            machine, first);
        if (!error) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_unify(machine, error, second);
    }

    if (goal.kind == PETTA_GOAL_BOOLEAN_READY) {
        return petta_machine_start_boolean_choice(
            machine, first, second, goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_RELATIONAL_MEMBER_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 3u) {
            return false;
        }
        return petta_machine_start_relational_member_choice(
            machine, first->expr.elems[1],
            first->expr.elems[2], second,
            goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_COLLECTION_COUNT_READY) {
        int64_t existing = 0;
        if (atom_petta_counted_collection_count(
                first, &existing)) {
            return petta_machine_unify(
                machine, first, second);
        }
        Atom *count = petta_fresh_variable(machine);
        if (!count)
            return false;
        size_t goal_mark = machine->goal_len;
        size_t trail_mark = machine->goal_trail_len;
        if (!petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_WRAP_COLLECTION_COUNT,
                    .barrier = goal.barrier,
                    .first = count,
                    .second = second,
                }) ||
            !petta_machine_start_list_length_choice(
                machine, first, count, goal.barrier)) {
            (void)petta_goal_trail_rollback(
                machine, trail_mark);
            machine->goal_len = goal_mark;
            return false;
        }
        return true;
    }

    if (goal.kind == PETTA_GOAL_WRAP_COLLECTION_COUNT) {
        if (!first || first->kind != ATOM_GROUNDED ||
            first->ground.gkind != GV_INT ||
            first->ground.ival < 0) {
            return false;
        }
        Atom *collection = atom_petta_counted_collection(
            &machine->heap, first->ground.ival);
        return collection &&
               petta_machine_unify(
                   machine, collection, second);
    }

    if (goal.kind == PETTA_GOAL_LIST_SIZE_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 2u) {
            return false;
        }
        int64_t counted = 0;
        if (atom_petta_counted_collection_count(
                first->expr.elems[1], &counted)) {
            return petta_machine_unify(
                machine, atom_int(&machine->heap, counted),
                second);
        }
        return petta_machine_start_list_length_choice(
            machine, first->expr.elems[1],
            second, goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_LIST_UNARY_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 2u ||
            first->expr.elems[0]->kind != ATOM_SYMBOL) {
            return false;
        }
        SymbolId operation = first->expr.elems[0]->sym_id;
        Atom *items = first->expr.elems[1];
        if (operation == g_builtin_syms.car_atom ||
            operation == g_builtin_syms.cdr_atom) {
            if (items->kind == ATOM_VAR) {
                Atom *other = petta_fresh_variable(machine);
                Atom *list = operation == g_builtin_syms.car_atom
                    ? petta_semantics_open_cons_value(
                          &machine->heap, second, other)
                    : petta_semantics_open_cons_value(
                          &machine->heap, other, second);
                return other && list &&
                       petta_machine_unify(machine, items, list);
            }
            if (petta_semantics_is_open_cons_value(items)) {
                return petta_machine_unify(
                    machine,
                    items->expr.elems[
                        operation == g_builtin_syms.car_atom
                            ? 1u : 2u],
                    second);
            }
            if (items->kind != ATOM_EXPR ||
                items->expr.len == 0u) {
                return false;
            }
            if (operation == g_builtin_syms.car_atom) {
                return petta_machine_unify(
                    machine, items->expr.elems[0], second);
            }
            Atom *tail = atom_expr(
                &machine->heap, items->expr.elems + 1u,
                items->expr.len - 1u);
            return tail &&
                   petta_machine_unify(machine, tail, second);
        }
        if (petta_semantics_is_open_cons_value(items)) {
            items = petta_machine_materialize_list(
                machine, items);
        }
        if (!items || items->kind != ATOM_EXPR)
            return false;
        if (operation == g_builtin_syms.petta_last) {
            return items->expr.len > 0u &&
                   petta_machine_unify(
                       machine,
                       items->expr.elems[items->expr.len - 1u],
                       second);
        }
        if (operation == g_builtin_syms.petta_msort) {
            Atom *sorted =
                petta_semantics_msort(&machine->heap, items);
            return sorted &&
                   petta_machine_unify(
                       machine, sorted, second);
        }
        if (operation != g_builtin_syms.reverse)
            return false;
        Atom **reversed = items->expr.len
            ? arena_alloc(
                  &machine->heap,
                  sizeof(*reversed) * (size_t)items->expr.len)
            : NULL;
        for (CettaExprIndex index = 0u;
             index < items->expr.len; index++) {
            reversed[index] =
                items->expr.elems[items->expr.len - index - 1u];
        }
        Atom *result = atom_expr(
            &machine->heap, reversed, items->expr.len);
        return result &&
               petta_machine_unify(machine, result, second);
    }

    if (goal.kind == PETTA_GOAL_LIST_SET_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len < 2u ||
            first->expr.elems[0]->kind != ATOM_SYMBOL) {
            return false;
        }
        PeTTaForm operation = petta_semantics_form(
            first->expr.elems[0]->sym_id);
        Atom *list = NULL;
        Atom *result = NULL;
        if (operation == PETTA_FORM_LIST_TO_SET &&
            first->expr.len == 2u) {
            list = first->expr.elems[1];
            if (petta_semantics_is_open_cons_value(list))
                list = petta_machine_materialize_list(machine, list);
            result = petta_semantics_list_to_set(
                &machine->heap, list);
        } else if (operation == PETTA_FORM_EXCLUDE_ITEM &&
                   first->expr.len == 3u) {
            list = first->expr.elems[2];
            if (petta_semantics_is_open_cons_value(list))
                list = petta_machine_materialize_list(machine, list);
            result = petta_semantics_exclude_item(
                &machine->heap, first->expr.elems[1], list);
        }
        return result &&
               petta_machine_unify(machine, result, second);
    }

    if (goal.kind == PETTA_GOAL_SREAD_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 2u) {
            return false;
        }
        Atom *input = first->expr.elems[1];
        Atom *text = input;
        if (input->kind == ATOM_SYMBOL) {
            const char *name = atom_name_cstr(input);
            text = name ? atom_string(&machine->heap, name) : NULL;
        }
        Atom *parse_head =
            atom_symbol_id(&machine->heap, g_builtin_syms.parse);
        Atom *arguments[1] = {text};
        Atom *parsed = text && parse_head
            ? grounded_dispatch(
                  &machine->heap, parse_head, arguments, 1u)
            : NULL;
            return parsed &&
               petta_machine_unify(machine, parsed, second);
    }

    if (goal.kind == PETTA_GOAL_EQUAL_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 3u) {
            return false;
        }
        /*
         * A logical list may arrive as an open-cons carrier on one side
         * and a flat expression on the other; the cons matcher normalizes
         * both, exactly like equation-head matching.  Value pairs without
         * a carrier use the ordinary unifier.
         */
        Atom *left_value = first->expr.elems[1];
        Atom *right_value = first->expr.elems[2];
        bool left_cons =
            petta_semantics_contains_cons_constraint(left_value);
        bool right_cons =
            petta_semantics_contains_cons_constraint(right_value);
        bool equal = left_cons || right_cons
            ? petta_semantics_match_cons_constraint(
                  &machine->heap,
                  left_cons ? left_value : right_value,
                  left_cons ? right_value : left_value,
                  search_context_builder(&machine->search))
            : petta_machine_unify(
                  machine, left_value, right_value);
        return petta_machine_boolean(machine, equal, second);
    }

    if (goal.kind == PETTA_GOAL_APPEND_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 3u) {
            return false;
        }
        return petta_machine_append(
            machine, first->expr.elems[1],
            first->expr.elems[2], second,
            goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_CONS_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 3u) {
            return false;
        }
        return petta_machine_cons(
            machine, first->expr.elems[1],
            first->expr.elems[2], second,
            goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_MATCH_SPACE_READY) {
        Atom *template = goal.third
            ? petta_machine_apply_bindings(
                  machine, environment, &machine->heap, goal.third)
            : NULL;
        Atom *expected = goal.fourth
            ? petta_machine_apply_bindings(
                  machine, environment, &machine->heap, goal.fourth)
            : NULL;
        if (!first || !second || !template || !expected) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_start_ready_match(
            machine, first, second, template, expected,
            goal.barrier, goal.plan, failure);
    }

    if (goal.kind == PETTA_GOAL_MAP_ATOM_READY) {
        Atom *body = goal.third
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.third)
            : NULL;
        Atom *expected = petta_machine_apply_bindings(machine,
            environment, &machine->heap, goal.fourth);
        if (!first || !second || !expected ||
            (goal.third && !body)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_map_atom(
            machine, first, second, body, expected,
            goal.barrier, environment);
    }

    if (goal.kind == PETTA_GOAL_FOLDL_ATOM_READY) {
        Atom *expression = goal.third
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.third)
            : NULL;
        Atom *expected = goal.fourth
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.fourth)
            : NULL;
        if (!first || !second || !expression || !expected) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_foldl_atom(
            machine, first, second, expression, expected,
            goal.barrier, goal.choice_index != 0u,
            environment);
    }

    if (goal.kind == PETTA_GOAL_FILTER_ATOM_READY) {
        Atom *body = goal.third
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.third)
            : NULL;
        Atom *expected = goal.fourth
            ? petta_machine_apply_bindings(machine,
                  environment, &machine->heap, goal.fourth)
            : NULL;
        if (!first || !second || !expected ||
            (goal.third && !body)) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return petta_machine_filter_atom(
            machine, first, second, body, expected,
            goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_PAIR_SELECT) {
        if (!first || !second || goal.choice_index > 1u)
            return false;
        if (first->kind == ATOM_VAR) {
            Atom *other = petta_fresh_variable(machine);
            if (!other)
                return false;
            Atom *elements[2];
            elements[goal.choice_index] = second;
            elements[1u - goal.choice_index] = other;
            Atom *pair = atom_expr(&machine->heap, elements, 2u);
            return pair && petta_machine_unify(machine, first, pair);
        }
        return first->kind == ATOM_EXPR &&
               first->expr.len == 2u &&
               petta_machine_unify(
                   machine,
                   first->expr.elems[goal.choice_index],
                   second);
    }

    if (goal.kind == PETTA_GOAL_MEMBER_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 3u) {
            return false;
        }
        return petta_machine_start_member_choice(
            machine, first->expr.elems[1],
            first->expr.elems[2], second,
            goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_TEST_COMPARE) {
        Atom *test_head = petta_machine_apply_bindings(machine,
            environment, &machine->heap, goal.third);
        Atom *result_expected = petta_machine_apply_bindings(machine,
            environment, &machine->heap, goal.fourth);
        if (!first || !second || !test_head || !result_expected) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom *actual = first;
        if (first->kind == ATOM_EXPR && first->expr.len == 1u)
            actual = first->expr.elems[0];
        actual = petta_machine_materialize_answer(machine, actual);
        Atom *materialized_expected =
            petta_machine_materialize_answer(machine, second);
        if (!actual || !materialized_expected) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        Atom *quote =
            atom_symbol_id(&machine->heap, g_builtin_syms.quote);
        Atom *quoted_actual =
            quote ? atom_expr2(&machine->heap, quote, actual) : NULL;
        Atom *quoted_expected =
            quote
                ? atom_expr2(
                      &machine->heap, quote,
                      materialized_expected)
                : NULL;
        Atom *ready_test =
            quoted_actual && quoted_expected
                ? atom_expr3(
                      &machine->heap, test_head,
                      quoted_actual, quoted_expected)
                : NULL;
        if (!ready_test ||
            !petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_HOST_READY,
                    .barrier = goal.barrier,
                    .first = ready_test,
                    .second = result_expected,
                })) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        return true;
    }

    if (goal.kind == PETTA_GOAL_REDUCE_READY) {
        if (!first || first->kind != ATOM_EXPR ||
            first->expr.len != 2u) {
            return false;
        }
        Atom *value = first->expr.elems[1];
        if (petta_semantics_is_open_cons_value(value)) {
            Atom *flattened =
                petta_machine_materialize_list(machine, value);
            if (!flattened) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            value = flattened;
        }
        return petta_push_solve(
            machine, value, second, goal.barrier);
    }

    if (goal.kind == PETTA_GOAL_EQUAL_COMMIT) {
        if (goal.choice_index < machine->choice_len &&
            machine->choices[goal.choice_index].kind ==
                PETTA_CHOICE_EQUAL_DEFAULT) {
            machine->choices[goal.choice_index]
                .as.equal_default.saw_answer = true;
        }
        return petta_machine_unify(machine, first, second);
    }

    if (goal.kind == PETTA_GOAL_CASE_SELECT) {
        if (goal.choice_index < machine->choice_len &&
            machine->choices[goal.choice_index].kind ==
                PETTA_CHOICE_CASE_DEFAULT) {
            machine->choices[goal.choice_index]
                .as.case_default.saw_answer = true;
        }
        Atom *expected = petta_machine_apply_bindings(machine,
            environment, &machine->heap, goal.third);
        if (!first || !second || second->kind != ATOM_EXPR ||
            !expected) {
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < second->expr.len; index++) {
            Atom *branch = second->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u)
                continue;
            Atom *pattern = branch->expr.elems[0];
            if (pattern->kind == ATOM_SYMBOL &&
                pattern->sym_id == g_builtin_syms.empty) {
                continue;
            }
            BindingsBuilder *builder =
                search_context_builder(&machine->search);
            uint32_t mark = bindings_builder_save(builder);
            bool matched =
                petta_semantics_contains_cons_constraint(pattern)
                    ? petta_semantics_match_cons_constraint(
                          &machine->heap, pattern, first, builder)
                    : petta_machine_unify(machine, pattern, first);
            if (matched &&
                bindings_has_loop(
                    (Bindings *)search_context_bindings(
                        &machine->search))) {
                bindings_builder_rollback(builder, mark);
                matched = false;
            }
            if (!matched)
                continue;
            if (!petta_push_solve_planned(
                    machine, branch->expr.elems[1], expected,
                    goal.barrier,
                    petta_plan_child(
                        petta_plan_child(
                            goal.second_plan, index),
                        1u))) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }
        return false;
    }

    if (goal.kind == PETTA_GOAL_CALL_READY ||
        goal.kind == PETTA_GOAL_CALL_READY_RESOLVED ||
        goal.kind == PETTA_GOAL_CALL_READY_DATA ||
        goal.kind ==
            PETTA_GOAL_CALL_READY_COUNTED_COLLECTION) {
        bool ordinary_ready_call =
            goal.kind == PETTA_GOAL_CALL_READY ||
            goal.kind == PETTA_GOAL_CALL_READY_RESOLVED;
        if (machine->host.analysis &&
            machine->host.analysis->validate_ready_call) {
            char diagnostic[512] = {0};
            PettaMachineBoundaryResult boundary =
                machine->host.analysis->validate_ready_call(
                    machine->host.context, machine->space, first,
                    diagnostic, sizeof(diagnostic));
            if (boundary != PETTA_MACHINE_BOUNDARY_ACCEPTED) {
                if (machine->typecheck_exit_code == 0) {
                    snprintf(
                        machine->typecheck_diagnostic,
                        sizeof(machine->typecheck_diagnostic),
                        "%s",
                        diagnostic[0]
                            ? diagnostic
                            : (boundary == PETTA_MACHINE_BOUNDARY_REFUTED
                                   ? "PeTTa type error: committed call boundary rejected"
                                   : "PeTTa typechecker fault: call boundary analysis failed"));
                    machine->typecheck_exit_code =
                        boundary == PETTA_MACHINE_BOUNDARY_REFUTED ? 2 : 1;
                }
                *failure = PETTA_MACHINE_STEP_HOST_ERROR;
                return false;
            }
        }
        PettaMachineQuerySpecializationAdmission
            specialization_admission =
                PETTA_MACHINE_QUERY_SPECIALIZATION_DEFER;
        if (ordinary_ready_call &&
            machine->host.admit_query_without_specialization && first &&
            first->kind == ATOM_EXPR && first->expr.len > 0u &&
            first->expr.elems[0] &&
            first->expr.elems[0]->kind == ATOM_SYMBOL) {
            specialization_admission =
                machine->host.admit_query_without_specialization(
                    machine->host.context, machine->space,
                    first->expr.elems[0]->sym_id,
                    first->expr.elems + 1u,
                    first->expr.len - 1u);
            if (specialization_admission ==
                    PETTA_MACHINE_QUERY_SPECIALIZATION_INVALIDATED) {
                *failure = PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
        }
        /*
         * Specialization is a binding-time operation on argument values, not
         * on their source expressions.  Every strict argument has reached
         * its translated value at this continuation; Atom-typed arguments
         * are intentionally already source-data values.  Preparing earlier
         * can confuse a nested call with constructor data and derive a head
         * that no evaluated invocation can match.
         */
        if (machine->host.prepare_call &&
            specialization_admission !=
                PETTA_MACHINE_QUERY_SPECIALIZATION_BYPASS) {
            Atom *view_input = first;
            Atom *prepared = first;
            uint64_t specializer_started_ns =
                machine->host.measure_stats
                    ? petta_machine_monotonic_ns() : 0u;
            PettaSpecializeResult specialized =
                machine->host.prepare_call(
                    machine->host.context, machine->space,
                    &machine->heap, first, &prepared);
            machine->stats.specializer_prepare_calls++;
            if (specializer_started_ns != 0u) {
                uint64_t finished_ns =
                    petta_machine_monotonic_ns();
                if (finished_ns >= specializer_started_ns) {
                    petta_machine_add_u64(
                        &machine->stats
                            .specializer_prepare_elapsed_ns,
                        finished_ns - specializer_started_ns);
                }
            }
            if (specialized ==
                PETTA_SPECIALIZE_UNCHANGED_FILTERED) {
                machine->stats.specializer_prepare_filtered++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_UNCHANGED_RELATION_FILTERED) {
                machine->stats
                    .specializer_prepare_relation_filtered++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_UNCHANGED_RELEVANCE_BOUNDED) {
                machine->stats
                    .specializer_prepare_relevance_bounded++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_REWRITTEN) {
                machine->stats.specializer_prepare_rewritten++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_UNCHANGED) {
                machine->stats.specializer_prepare_unchanged++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_CAPACITY) {
                machine->stats
                    .specializer_prepare_capacity_declines++;
            }
            if (specialized == PETTA_SPECIALIZE_INVALIDATED) {
                *failure = PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            if (specialized == PETTA_SPECIALIZE_CAPACITY)
                prepared = first;
            if (!prepared) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            if (prepared != view_input ||
                specialized == PETTA_SPECIALIZE_REWRITTEN) {
                equation_query_view_ready = false;
            }
            first = prepared;
        }
        bool count_collection_result =
            goal.kind ==
                PETTA_GOAL_CALL_READY_COUNTED_COLLECTION;
        SymbolId prepared_head = atom_head_symbol_id(first);
        CettaExprLen prepared_arity =
            first && first->kind == ATOM_EXPR &&
                    first->expr.len > 0u
                ? first->expr.len - 1u : 0u;
        bool relation_declared_tabled =
            prepared_head != SYMBOL_ID_NONE &&
            machine->host.tabled_relation_contains &&
            machine->host.tabled_relation_contains(
                machine->host.context,
                prepared_head, prepared_arity);
        if (count_collection_result && relation_declared_tabled) {
            Atom *value = petta_fresh_variable(machine);
            if (!value ||
                !petta_goal_push(
                    machine,
                    (PettaGoal){
                        .kind =
                            PETTA_GOAL_COLLECTION_COUNT_READY,
                        .barrier = goal.barrier,
                        .first = value,
                        .second = second,
                    })) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            bool tabled = false;
            bool scheduled =
                petta_machine_start_tabled_call(
                    machine, first, value, goal.barrier,
                    &tabled, failure);
            if (!tabled) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return scheduled;
        }
        bool tabled = false;
        if (!count_collection_result) {
            bool scheduled = petta_machine_start_tabled_call(
                machine, first, second, goal.barrier,
                &tabled, failure);
            if (tabled)
                return scheduled;
        }
        /* The same revision-pinned register instruction used by HE and Prime
         * can consume a strict PeTTa singleton once its arguments reach this
         * ready continuation.  A table generator deliberately bypasses the
         * table once to evaluate its root clauses; that bypass must not admit
         * either direct accelerator, because recursive calls and answer
         * publication still belong to the SLG protocol.  Counting,
         * translator-rule, and declared-tabled calls therefore keep their
         * ordinary proof/multiplicity machinery. */
        bool translated =
            prepared_head != SYMBOL_ID_NONE &&
            machine->host.translator_rule_contains &&
            machine->host.translator_rule_contains(
                machine->host.context, prepared_head);
        if (petta_machine_trace_enabled()) {
            fprintf(
                stderr,
                "[petta-machine] ready kind=%u translated=%u "
                "prepared-pure=%u head=%s\n",
                (unsigned)goal.kind, translated ? 1u : 0u,
                machine->host.execute_prepared_pure_call ? 1u : 0u,
                prepared_head == SYMBOL_ID_NONE
                    ? "<none>"
                    : symbol_bytes(g_symbols, prepared_head));
        }
        if (!count_collection_result && !translated &&
            !relation_declared_tabled &&
            ordinary_ready_call &&
            machine->host.execute_prepared_pure_call) {
            Atom *pure_result =
                machine->host.execute_prepared_pure_call(
                    machine->host.context, machine->space,
                    &machine->heap, first);
            if (pure_result) {
                /*
                 * Foreign imports can make a previously inert symbol
                 * callable without mutating the MeTTa space revision used
                 * by the prepared program.  A prepared result headed by
                 * such a symbol is therefore a suspended canonical call,
                 * not the final value.  Decline this accelerator result and
                 * let the ordinary clause machine execute it through the
                 * live foreign registry.
                 */
                if (!petta_machine_extension_callable(
                        machine, pure_result)) {
                    return petta_machine_unify_resolved(
                        machine, pure_result, second);
                }
            }
        }
        if (!count_collection_result && !translated &&
            !relation_declared_tabled &&
            ordinary_ready_call &&
            prepared_head != SYMBOL_ID_NONE) {
            SpacePreparedEquation prepared_equation;
            if (space_prepare_single_equation(
                    machine->space, prepared_head,
                    &prepared_equation)) {
                Atom *register_result = NULL;
                SpacePreparedRegisterStep register_step =
                    space_prepared_equation_run_register_loop(
                        &prepared_equation, first,
                        &machine->heap, 4096u,
                        &register_result);
                if (register_step ==
                    SPACE_PREPARED_REGISTER_NOT_APPLICABLE) {
                    register_step =
                        space_prepared_equation_run_register_recursion(
                            &prepared_equation, first,
                            &machine->heap, SIZE_MAX,
                            &register_result);
                }
                if (register_step == SPACE_PREPARED_REGISTER_VALUE) {
                    return petta_machine_unify_resolved(
                        machine, register_result, second);
                }
                if (register_step ==
                    SPACE_PREPARED_REGISTER_TAIL_CALL) {
                    if (!petta_push_solve(
                            machine, register_result,
                            second, goal.barrier)) {
                        *failure = PETTA_MACHINE_STEP_CAPACITY;
                        return false;
                    }
                    return true;
                }
            }
        }
        return petta_machine_start_clause_choice(
            machine, first, second, goal.barrier,
            goal.kind != PETTA_GOAL_CALL_READY_DATA,
            count_collection_result,
            equation_query_view_ready && ordinary_ready_call &&
                    !count_collection_result && !translated &&
                    !relation_declared_tabled
                ? goal.equation_query_source : NULL,
            equation_query_view_ready && ordinary_ready_call &&
                    !count_collection_result && !translated &&
                    !relation_declared_tabled
                ? goal.activation_template : NULL,
            equation_query_view_ready ? goal.activation_epoch : 0u,
            equation_query_view_ready
                ? goal.activation_first_entry : 0u);
    }

    if (goal.kind == PETTA_GOAL_IF_SELECT) {
        bool truth = false;
        /*
         * Each successful PeTTa condition result is compared with `true`.
         * Any other value selects the else branch; a condition with no
         * result fails before this continuation is reached.
         */
        (void)petta_semantics_truth_value(first, &truth);
        if (petta_machine_trace_enabled())
            petta_machine_trace_atom(
                "[petta-machine] if condition ", first);
        uint8_t selected_bit = truth
            ? PETTA_ACTIVATION_SOURCE_SECOND
            : PETTA_ACTIVATION_SOURCE_THIRD;
        Atom *selected = truth ? goal.second : goal.third;
        const PettaPlanNode *selected_plan = truth
            ? goal.second_plan : goal.third_plan;
        if ((goal.activation_source_fields & selected_bit) != 0u) {
            return petta_push_solve_activation_planned(
                machine, selected, goal.fourth,
                goal.barrier, selected_plan,
                goal.activation_template,
                goal.activation_epoch,
                goal.activation_first_entry);
        }
        return petta_push_solve_planned(
            machine, selected, goal.fourth,
            goal.barrier, selected_plan);
    }

    if (goal.kind == PETTA_GOAL_CUT) {
        size_t barrier = goal.barrier;
        if (barrier > machine->choice_len)
            barrier = machine->choice_len;
        petta_choice_truncate(machine, barrier);
        return petta_machine_boolean(machine, true, first);
    }

    if (goal.kind == PETTA_GOAL_ONCE_COMMIT) {
        if (goal.choice_index >= machine->choice_len ||
            machine->choices[goal.choice_index].kind !=
                PETTA_CHOICE_ONCE) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        petta_choice_truncate(machine, goal.choice_index);
        return true;
    }

    if (goal.kind == PETTA_GOAL_TRANSACTION_COMMIT) {
        if (goal.choice_index >= machine->choice_len) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        PettaChoice *choice =
            &machine->choices[goal.choice_index];
        if (choice->kind != PETTA_CHOICE_TRANSACTION ||
            !choice->as.transaction.active ||
            !machine->host.transaction_commit ||
            !machine->host.transaction_commit(
                machine->host.context,
                choice->as.transaction.handle)) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        machine->space = choice->as.transaction.previous_space;
        choice->as.transaction.active = false;
        petta_choice_truncate(machine, goal.choice_index);
        return true;
    }

    if (goal.kind == PETTA_GOAL_MUTEX_RELEASE) {
        if (goal.choice_index >= machine->choice_len) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        PettaChoice *choice =
            &machine->choices[goal.choice_index];
        if (choice->kind != PETTA_CHOICE_MUTEX ||
            !choice->as.mutex.active ||
            !machine->host.mutex_release) {
            *failure = PETTA_MACHINE_STEP_HOST_ERROR;
            return false;
        }
        machine->host.mutex_release(
            machine->host.context,
            choice->as.mutex.handle);
        choice->as.mutex.active = false;
        petta_choice_truncate(machine, goal.choice_index);
        return true;
    }

    /*
     * Strict, deterministic grounded operators need no host OutcomeSet or
     * choice record once their arguments are ready.  Execute only the shared
     * positive-list of pure operators here; a NULL result falls through to
     * the host so "not ready" and extension behavior remain unchanged.
     */
    PettaMachineHostMode ready_host_mode =
        first && machine->host.classify
            ? machine->host.classify(
                  machine->host.context, machine->space, first)
            : PETTA_MACHINE_HOST_NONE;
    if (ready_host_mode != PETTA_MACHINE_HOST_READY_OVERRIDE &&
        (goal.kind == PETTA_GOAL_HOST_READY ||
         goal.kind == PETTA_GOAL_HOST_STRICT_READY) &&
        first && first->kind == ATOM_EXPR &&
        first->expr.len > 0u &&
        first->expr.len - 1u <= UINT32_MAX) {
        Atom *head = first->expr.elems[0];
        SymbolId head_id = head && head->kind == ATOM_SYMBOL
            ? head->sym_id : SYMBOL_ID_NONE;
        if (head_id == g_builtin_syms.get_type &&
            first->expr.len == 2u && machine->host.get_type) {
            return petta_machine_start_intrinsic_get_type(
                machine, first->expr.elems[1], second,
                goal.barrier, failure);
        }
        bool direct =
            goal.kind == PETTA_GOAL_HOST_STRICT_READY
                ? is_grounded_op(head_id)
                : grounded_op_is_type_pure(head_id);
        if (direct) {
            Atom *direct = grounded_dispatch(
                &machine->heap, head,
                first->expr.elems + 1u,
                (uint32_t)(first->expr.len - 1u));
            if (direct) {
                if (atom_is_empty(direct))
                    return false;
                bool truth = false;
                if (petta_semantics_truth_value(direct, &truth)) {
                    direct = machine->host.boolean_value
                        ? machine->host.boolean_value(
                              machine->host.context,
                              &machine->heap, truth)
                        : petta_semantics_boolean_value(
                              &machine->heap, truth);
                }
                return petta_machine_unify(
                    machine, direct, second);
            }
        }
    }

    if (!machine->host.evaluate) {
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }

    /*
     * A host call is an evaluation boundary, not a transfer of the machine's
     * entire search history.  `first` has already been rewritten through the
     * current environment above.  Only residual variables reachable from
     * that expression can affect the host computation; the machine retains
     * every other binding for its continuation and choice points.
     *
     * Ground calls therefore forward no logical entries at all.  Open calls
     * use the shared transitive projection, which also retains connected
     * constraints and the orthogonal Prime occurrence state.  Host outcomes
     * are still merged into the authoritative machine environment below.
     */
    Bindings host_environment;
    bindings_init(&host_environment);
    bool projected = true;
    if (first && atom_has_vars(first)) {
        Atom *roots[] = {first};
        projected = bindings_project_reachable(
            environment, roots, 1u, &host_environment);
    } else {
        bindings_prime_assign(&host_environment, environment);
    }
    if (!projected) {
        bindings_free(&host_environment);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    machine->stats.host_environment_entries_observed +=
        environment->len;
    machine->stats.host_environment_entries_forwarded +=
        host_environment.len;
    if ((size_t)host_environment.len >
        machine->stats
            .maximum_host_environment_entries_forwarded) {
        machine->stats
            .maximum_host_environment_entries_forwarded =
            host_environment.len;
    }

    OutcomeSet *outcomes = cetta_malloc(sizeof(*outcomes));
    outcome_set_init_with_owner(outcomes, &machine->heap);
    bool evaluated = machine->host.evaluate(
            machine->host.context, machine->space, &machine->heap,
            first, &host_environment, outcomes);
    if (evaluated) {
        evaluated = petta_machine_factor_outcome_prefixes(
            machine, outcomes, &host_environment);
    }
    bindings_free(&host_environment);
    if (!evaluated) {
        outcome_set_free(outcomes);
        free(outcomes);
        *failure = PETTA_MACHINE_STEP_HOST_ERROR;
        return false;
    }
    return petta_machine_start_outcome_choice(
        machine, outcomes, second, goal.barrier);
}

static bool petta_machine_emit(
    PettaMachineImpl *machine, Atom **answer, Bindings *environment) {
    const Bindings *current =
        search_context_bindings(&machine->search);
    uint64_t answer_weight =
        machine->pending_answer_weight
            ? machine->pending_answer_weight : 1u;
    Atom *resolved = machine->raised_error
        ? machine->raised_error
        : petta_machine_apply_bindings(machine,
              current, &machine->heap, machine->answer_variable);
    if (!resolved)
        return false;
    if (machine->count_only_emission) {
        bindings_init(environment);
        *answer = resolved;
        machine->raised_error = NULL;
        machine->pending_answer_weight = 0u;
        machine->last_answer_weight = answer_weight;
        machine->stats.answers += answer_weight;
        return true;
    }
    if (petta_goal_growth_trace_enabled()) {
        fprintf(
            stderr,
            "[petta-answer-publication] lane=answer phase=before"
            " visible=%zu bindings=%u heap=%zu answer=%zu\n",
            machine->visible_len, current ? current->len : 0u,
            arena_accounted_live_bytes(&machine->heap),
            arena_accounted_live_bytes(machine->answer_arena));
    }
    resolved = petta_machine_materialize_answer(
        machine, resolved);
    Atom *promoted = petta_machine_copy_atom(
        machine, machine->answer_arena, resolved,
        PETTA_ATOM_COPY_ANSWER);
    if (!promoted)
        return false;
    bindings_init(environment);
    if (bindings_prime_present(current)) {
        bindings_prime_assign(environment, current);
        if (!bindings_promote_atoms_to_arena(
                environment, machine->answer_arena)) {
            bindings_free(environment);
            bindings_init(environment);
            return false;
        }
    }
    for (size_t index = 0u; index < machine->visible_len; index++) {
        Atom *variable = machine->visible[index].variable;
        Atom *value = petta_machine_apply_bindings(machine,
            current, &machine->heap, variable);
        if (!value || (value->kind == ATOM_VAR &&
                       value->var_id == variable->var_id)) {
            continue;
        }
        if (petta_goal_growth_trace_enabled()) {
            fprintf(
                stderr,
                "[petta-answer-publication] lane=visible phase=before"
                " index=%zu variable=",
                index);
            atom_print(variable, stderr);
            fprintf(
                stderr, " heap=%zu answer=%zu\n",
                arena_accounted_live_bytes(&machine->heap),
                arena_accounted_live_bytes(machine->answer_arena));
        }
        value = petta_machine_materialize_answer(
            machine, value);
        if (!value)
            return false;
        Atom *promoted_variable = petta_machine_copy_atom(
            machine, machine->answer_arena, variable,
            PETTA_ATOM_COPY_VISIBLE_VARIABLE);
        Atom *promoted_value = petta_machine_copy_atom(
            machine, machine->answer_arena, value,
            PETTA_ATOM_COPY_VISIBLE_VALUE);
        if (!promoted_variable || !promoted_value ||
            !bindings_add_var(
                environment, promoted_variable, promoted_value)) {
            bindings_free(environment);
            bindings_init(environment);
            return false;
        }
    }
    *answer = promoted;
    machine->raised_error = NULL;
    machine->pending_answer_weight = 0u;
    machine->last_answer_weight = answer_weight;
    machine->stats.answers += answer_weight;
    return true;
}

static bool petta_machine_init_internal(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *query_plan,
    const Bindings *base_environment,
    const PettaMachineHost *host,
    bool borrow_query,
    PettaTableShared *table_shared,
    bool owns_table_shared, bool borrowed_root_table,
    bool bypass_root_table,
    size_t table_generator) {
    if (machine)
        machine->impl = NULL;
    if (!machine || !space || !answer_arena || !query ||
        !table_shared || !petta_machine_host_valid(host))
        return false;

    PettaMachineImpl *impl = cetta_malloc(sizeof(*impl));
    memset(impl, 0, sizeof(*impl));
    impl->instance_id = petta_machine_fresh_instance_id();
    impl->space = space;
    impl->answer_arena = answer_arena;
    impl->table_shared = table_shared;
    impl->owns_table_shared = owns_table_shared;
    impl->borrowed_root_table = borrowed_root_table;
    impl->bypass_root_table = bypass_root_table;
    impl->table_generator = table_generator;
    if (host)
        impl->host = *host;
    petta_machine_trace_config_init(&impl->trace);
    petta_machine_heap_arena_init(&impl->heap);
    petta_machine_heap_arena_init(&impl->tenured);
    cetta_gslt_ground_dense_workspace_init_v1(
        &impl->equation_template_c0_workspace);
    for (uint32_t index = 0u;
         index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
        bindings_dense_epoch_frame_init(
            &impl->activation_frames[index].frame);
    }
    arena_init(&impl->plan_arena);
    impl->equation_template_c0_enabled = petta_equation_template_c0_enabled();
    impl->heap_collect_after =
        PETTA_MACHINE_HEAP_WINDOW_BYTES;
    impl->tenured_major_after =
        PETTA_MACHINE_HEAP_WINDOW_BYTES;
    impl->binding_growth_collect_after =
        (uint64_t)petta_deterministic_binding_window(impl, 0u);
    if (!search_context_init(
            &impl->search, base_environment, NULL)) {
        arena_free(&impl->heap);
        arena_free(&impl->tenured);
        cetta_gslt_ground_dense_workspace_free_v1(
            &impl->equation_template_c0_workspace);
        for (uint32_t index = 0u;
             index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
            bindings_dense_epoch_frame_free(
                &impl->activation_frames[index].frame);
        }
        arena_free(&impl->plan_arena);
        if (impl->owns_table_shared)
            petta_table_shared_free(impl->table_shared);
        else if (impl->borrowed_root_table)
            impl->table_shared->root_leased = false;
        free(impl);
        return false;
    }
    /* A nested collapse child executes synchronously while its parent is
     * paused.  The parent heap therefore outlives the child, just as it
     * already does for the projected environment and compiled plan.  Borrow
     * that immutable query instead of recursively copying a closed-over
     * persistent value on every child entry.  Public/root machines retain
     * the defensive copy promised by their API boundary. */
    impl->query = borrow_query
        ? query
        : petta_machine_copy_atom(
              impl, &impl->heap, query, PETTA_ATOM_COPY_QUERY);
    impl->answer_variable = petta_fresh_variable(impl);
    if (!impl->query || !impl->answer_variable ||
        !eval_visit_lexical_free_variables(
            impl->query, petta_collect_visible_variable, impl) ||
        !petta_push_solve_planned(
            impl, impl->query, impl->answer_variable, 0u,
            query_plan)) {
        petta_machine_destroy(&(PettaMachine){.impl = impl});
        return false;
    }
    impl->terminal_step = PETTA_MACHINE_STEP_EXHAUSTED;
    machine->impl = impl;
    return true;
}

bool petta_machine_init(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const Bindings *base_environment,
    const PettaMachineHost *host) {
    if (machine)
        machine->impl = NULL;
    if (!machine || !space || !answer_arena || !query ||
        !petta_machine_host_valid(host))
        return false;
    PettaTableShared *shared = host ? host->shared_table : NULL;
    bool owns_shared = true;
    bool borrowed_shared = false;
    if (shared && !shared->root_leased) {
        if (!petta_table_shared_reusable(shared))
            petta_table_shared_reset(shared);
        shared->root_leased = true;
        owns_shared = false;
        borrowed_shared = true;
    } else {
        shared = petta_table_shared_new();
    }
    if (!shared)
        return false;
    bool initialized = petta_machine_init_internal(
        machine, space, answer_arena, query, NULL,
        base_environment, host, false, shared,
        owns_shared, borrowed_shared, false,
        PETTA_TABLE_ENTRY_NONE);
    if (!initialized && borrowed_shared)
        shared->root_leased = false;
    return initialized;
}

bool petta_machine_init_with_plan(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *plan,
    const Bindings *base_environment,
    const PettaMachineHost *host) {
    if (machine)
        machine->impl = NULL;
    if (!machine || !space || !answer_arena || !query ||
        !petta_machine_host_valid(host))
        return false;
    PettaTableShared *shared = host ? host->shared_table : NULL;
    bool owns_shared = true;
    bool borrowed_shared = false;
    if (shared && !shared->root_leased) {
        if (!petta_table_shared_reusable(shared))
            petta_table_shared_reset(shared);
        shared->root_leased = true;
        owns_shared = false;
        borrowed_shared = true;
    } else {
        shared = petta_table_shared_new();
    }
    if (!shared)
        return false;
    bool initialized = petta_machine_init_internal(
        machine, space, answer_arena, query, plan,
        base_environment, host, false, shared,
        owns_shared, borrowed_shared, false,
        PETTA_TABLE_ENTRY_NONE);
    if (!initialized && borrowed_shared)
        shared->root_leased = false;
    return initialized;
}

static PettaMachineStep petta_machine_finish_next(
    PettaMachineImpl *impl, uint64_t started_ns,
    PettaMachineStep step) {
    if (!impl || !impl->host.measure_stats || started_ns == 0u)
        return step;
    uint64_t finished_ns = petta_machine_monotonic_ns();
    if (finished_ns >= started_ns) {
        petta_machine_add_u64(
            &impl->stats.active_elapsed_ns,
            finished_ns - started_ns);
    }
    if (step == PETTA_MACHINE_STEP_ANSWER &&
        !impl->first_answer_timed) {
        impl->first_answer_timed = true;
        impl->stats.time_to_first_answer_ns =
            impl->stats.active_elapsed_ns;
        impl->stats.first_answer_transition =
            impl->stats.transitions;
    }
    return step;
}

PettaMachineStep petta_machine_next(
    PettaMachine *machine, Atom **answer, Bindings *environment) {
    if (answer)
        *answer = NULL;
    if (environment)
        bindings_init(environment);
    if (!machine || !machine->impl || !answer || !environment)
        return PETTA_MACHINE_STEP_CAPACITY;
    PettaMachineImpl *impl = machine->impl;
    uint64_t started_ns = impl->host.measure_stats
        ? petta_machine_monotonic_ns() : 0u;
    impl->last_answer_weight = 0u;
    if (impl->terminal)
        return petta_machine_finish_next(
            impl, started_ns, impl->terminal_step);
    if (impl->table_shared &&
        impl->table_shared->entry_len > 0u &&
        !petta_table_shared_epoch_current(
            impl->table_shared)) {
        impl->terminal = true;
        impl->terminal_step =
            PETTA_MACHINE_STEP_INVALIDATED;
        return petta_machine_finish_next(
            impl, started_ns, impl->terminal_step);
    }
    if (impl->table_shared &&
        impl->table_shared->failed) {
        impl->terminal = true;
        impl->terminal_step =
            PETTA_MACHINE_STEP_CAPACITY;
        return petta_machine_finish_next(
            impl, started_ns, impl->terminal_step);
    }

    PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
    if (impl->suspended_choice) {
        impl->suspended_choice = false;
        if (!petta_machine_backtrack(impl, &failure)) {
            if (failure == PETTA_MACHINE_STEP_SUSPENDED) {
                impl->suspended_choice = true;
                return petta_machine_finish_next(
                    impl, started_ns, failure);
            }
            impl->terminal = true;
            impl->terminal_step = failure;
            return petta_machine_finish_next(
                impl, started_ns, failure);
        }
    }
    if (impl->yielded) {
        if (impl->host.permit_transition &&
            !impl->host.permit_transition(impl->host.context)) {
            return petta_machine_finish_next(
                impl, started_ns,
                PETTA_MACHINE_STEP_SUSPENDED);
        }
        impl->yielded = false;
        if (!petta_machine_backtrack(impl, &failure)) {
            impl->terminal = true;
            impl->terminal_step = failure;
            return petta_machine_finish_next(
                impl, started_ns, failure);
        }
    }

    for (;;) {
        if (impl->table_shared &&
            impl->table_shared->entry_len > 0u &&
            !petta_table_shared_epoch_current(
                impl->table_shared)) {
            impl->terminal = true;
            impl->terminal_step =
                PETTA_MACHINE_STEP_INVALIDATED;
            return petta_machine_finish_next(
                impl, started_ns, impl->terminal_step);
        }
        if (petta_machine_external_branch_ready(machine)) {
            return petta_machine_finish_next(
                impl, started_ns, PETTA_MACHINE_STEP_SUSPENDED);
        }
        if (impl->host.permit_transition &&
            !impl->host.permit_transition(impl->host.context)) {
            return petta_machine_finish_next(
                impl, started_ns,
                PETTA_MACHINE_STEP_SUSPENDED);
        }
        if (impl->raised_error) {
            assert(impl->goal_len == 0u);
            if (!petta_machine_emit(impl, answer, environment)) {
                impl->terminal = true;
                impl->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
                return petta_machine_finish_next(
                    impl, started_ns, impl->terminal_step);
            }
            impl->yielded = true;
            return petta_machine_finish_next(
                impl, started_ns, PETTA_MACHINE_STEP_ANSWER);
        }
        if (!petta_machine_maybe_collect(impl)) {
            impl->terminal = true;
            impl->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
            return petta_machine_finish_next(
                impl, started_ns, impl->terminal_step);
        }
        if (impl->goal_len == 0u) {
            if (!petta_machine_emit(impl, answer, environment)) {
                impl->terminal = true;
                impl->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
                return petta_machine_finish_next(
                    impl, started_ns, impl->terminal_step);
            }
            impl->yielded = true;
            return petta_machine_finish_next(
                impl, started_ns, PETTA_MACHINE_STEP_ANSWER);
        }

        PettaGoal goal = impl->goals[--impl->goal_len];
        failure = PETTA_MACHINE_STEP_EXHAUSTED;
        if (petta_machine_dispatch_goal(impl, goal, &failure)) {
            if (petta_machine_check_type_obligations(
                    impl, &failure)) {
                continue;
            }
        }
        if (impl->terminal)
            return petta_machine_finish_next(
                impl, started_ns, impl->terminal_step);
        if (failure == PETTA_MACHINE_STEP_SUSPENDED) {
            impl->suspended_choice = true;
            return petta_machine_finish_next(
                impl, started_ns, failure);
        }
        if (failure != PETTA_MACHINE_STEP_EXHAUSTED ||
            !petta_machine_backtrack(impl, &failure)) {
            impl->terminal = true;
            impl->terminal_step = failure;
            return petta_machine_finish_next(
                impl, started_ns, failure);
        }
    }
}

void petta_machine_destroy(PettaMachine *machine) {
    if (!machine || !machine->impl)
        return;
    PettaMachineImpl *impl = machine->impl;
    petta_choice_truncate(impl, 0u);
    free(impl->choices);
    free(impl->goals);
    free(impl->goal_trail);
    free(impl->visible);
    free(impl->type_obligations);
    for (size_t index = 0u;
         index < impl->match_decision_len; index++) {
        petta_match_decision_cache_entry_free(
            &impl->match_decisions[index]);
    }
    free(impl->match_decisions);
    free(impl->equation_callability);
    search_context_free(&impl->search);
    arena_free(&impl->heap);
    arena_free(&impl->tenured);
    petta_continuation_term_pool_release(
        impl->continuation_term_pool);
    cetta_gslt_ground_dense_workspace_free_v1(
        &impl->equation_template_c0_workspace);
    for (uint32_t index = 0u;
         index < PETTA_ACTIVATION_FRAME_CACHE_CAP; index++) {
        bindings_dense_epoch_frame_free(
            &impl->activation_frames[index].frame);
    }
    arena_free(&impl->plan_arena);
    if (impl->owns_table_shared)
        petta_table_shared_free(impl->table_shared);
    else if (impl->borrowed_root_table && impl->table_shared)
        impl->table_shared->root_leased = false;
    free(impl);
    machine->impl = NULL;
}

const char *petta_machine_typecheck_diagnostic(
    const PettaMachine *machine) {
    if (!machine || !machine->impl ||
        machine->impl->typecheck_exit_code == 0)
        return NULL;
    return machine->impl->typecheck_diagnostic;
}

int petta_machine_typecheck_exit_code(const PettaMachine *machine) {
    return machine && machine->impl
        ? machine->impl->typecheck_exit_code : 0;
}

bool petta_machine_stats(
    const PettaMachine *machine, PettaMachineStats *stats) {
    if (!machine || !machine->impl || !stats)
        return false;
    *stats = machine->impl->stats;
    /* A suspended collection keeps its producer machine live until the
     * stream completes.  Completed producers are accumulated when released;
     * include only currently-owned children here so an in-flight diagnostic
     * observes the work where it is actually happening. */
    for (size_t index = 0u;
         index < machine->impl->choice_len; index++) {
        const PettaChoice *choice =
            &machine->impl->choices[index];
        const PettaMachine *child = NULL;
        if (choice->kind == PETTA_CHOICE_COLLAPSE)
            child = choice->as.collapse.machine;
        else if (choice->kind == PETTA_CHOICE_COUNT_COLLAPSE)
            child = choice->as.count_collapse.machine;
        if (child && child->impl) {
            PettaMachineStats child_stats;
            if (!petta_machine_stats(child, &child_stats))
                return false;
            petta_machine_stats_accumulate(stats, &child_stats);
        }
    }
    return true;
}
