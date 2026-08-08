#include "petta_search_machine.h"

#include "grounded.h"
#include "match.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "stats.h"
#include "symbol.h"
#include "variant_shape.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PETTA_MACHINE_HEAP_WINDOW_BYTES \
    ((size_t)8u * 1024u * 1024u)
#define PETTA_MACHINE_BINDING_WINDOW_ENTRIES ((size_t)4096u)
#define PETTA_MACHINE_CHOICE_BINDING_MIN_WINDOW_ENTRIES ((size_t)4096u)
#define PETTA_TABLE_ENTRY_NONE ((size_t)SIZE_MAX)

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
    uint64_t hash;
    Atom **answers;
    size_t answer_len;
    size_t answer_cap;
    PettaTableEntryState state;
    size_t tarjan_index;
    size_t tarjan_lowlink;
    bool tarjan_on_stack;
    bool self_edge;
} PettaTableEntry;

typedef struct {
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
    uint64_t mutation_epoch;
    bool failed;
} PettaTableShared;

typedef enum {
    PETTA_TABLE_CHOICE_GENERATE_INITIAL = 0,
    PETTA_TABLE_CHOICE_GENERATE_FIXPOINT,
    PETTA_TABLE_CHOICE_REPLAY,
} PettaTableChoicePhase;

typedef enum {
    PETTA_GOAL_SOLVE = 0,
    PETTA_GOAL_FORCE,
    PETTA_GOAL_SOLVE_COUNTED_COLLECTION,
    PETTA_GOAL_CALL_READY,
    PETTA_GOAL_CALL_READY_DATA,
    PETTA_GOAL_CALL_READY_COUNTED_COLLECTION,
    PETTA_GOAL_TYPE_ACCEPT,
    PETTA_GOAL_APPLY_READY,
    PETTA_GOAL_PARTIAL_READY,
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
    PETTA_GOAL_PAIR_SELECT,
    PETTA_GOAL_MEMBER_READY,
    PETTA_GOAL_IF_SELECT,
    PETTA_GOAL_CASE_SELECT,
    PETTA_GOAL_TEST_COMPARE,
    PETTA_GOAL_HOST_STRICT_READY,
    PETTA_GOAL_HOST_READY,
    PETTA_GOAL_FOREIGN_READY,
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
} PettaGoalKind;

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
    size_t choice_index;
    uint64_t answer_weight;
} PettaGoal;

static void petta_machine_record_goal_class(
    PettaMachineStats *stats, PettaGoalKind kind) {
    if (!stats)
        return;
    switch (kind) {
    case PETTA_GOAL_SOLVE:
    case PETTA_GOAL_FORCE:
    case PETTA_GOAL_SOLVE_COUNTED_COLLECTION:
        stats->solve_goal_transitions++;
        return;
    case PETTA_GOAL_CALL_READY:
    case PETTA_GOAL_CALL_READY_DATA:
    case PETTA_GOAL_CALL_READY_COUNTED_COLLECTION:
    case PETTA_GOAL_APPLY_READY:
    case PETTA_GOAL_PARTIAL_READY:
    case PETTA_GOAL_RELATIONAL_EXTENSION_READY:
    case PETTA_GOAL_CLAUSE_ONLY:
        stats->call_goal_transitions++;
        return;
    case PETTA_GOAL_TYPE_ACCEPT:
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
        stats->control_goal_transitions++;
        return;
    case PETTA_GOAL_HOST_STRICT_READY:
    case PETTA_GOAL_HOST_READY:
    case PETTA_GOAL_FOREIGN_READY:
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
    size_t previous_protected_goal_height;
    uint32_t barrier;
    union {
        struct {
            PettaClauseCandidate *candidates;
            size_t equation_len;
            size_t next_equation;
            uint64_t call_occurrence;
            Atom *query;
            Atom *expected;
            bool evaluate_result;
            bool translate_result;
            bool count_collection_result;
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
            Atom *expression;
            Atom *expected;
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
            bool pass_changed;
        } table;
    } as;
} PettaChoice;

typedef struct {
    VarId id;
    Atom *variable;
} PettaVisibleVariable;

typedef struct {
    SpaceReadToken read;
    SymbolId head;
    CettaExprLen arity;
    CettaMatchDecisionMode mode;
    Atom **equations;
    size_t equation_len;
    CettaMatchDecision *decision;
} PettaMatchDecisionCacheEntry;

struct PettaMachineImpl {
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
    PettaMatchDecisionCacheEntry *match_decisions;
    size_t match_decision_len;
    size_t match_decision_cap;
    Atom *query;
    Atom *answer_variable;
    bool yielded;
    bool suspended_choice;
    bool terminal;
    bool owns_table_shared;
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
    PettaMachineStats stats;
};

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

static Atom *petta_machine_apply_bindings(
    PettaMachineImpl *machine, const Bindings *bindings,
    Arena *arena, Atom *atom) {
    if (!machine)
        return bindings_apply_if_vars(bindings, arena, atom);
    machine->stats.binding_apply_calls++;
    bool rewrites = bindings && bindings->len != 0u && atom &&
        atom_has_vars(atom);
    if (rewrites)
        machine->stats.binding_apply_rewrites++;
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = bindings_apply_if_vars(bindings, arena, atom);
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

static Atom *petta_machine_copy_atom(
    PettaMachineImpl *machine, Arena *arena, Atom *atom) {
    if (!machine)
        return atom_deep_copy(arena, atom);
    machine->stats.atom_copy_calls++;
    size_t before = arena_accounted_live_bytes(arena);
    Atom *result = atom_deep_copy(arena, atom);
    petta_machine_add_u64(
        &machine->stats.atom_copy_allocated_bytes,
        petta_machine_arena_growth(arena, before));
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

static bool petta_machine_init_internal(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *query_plan,
    const Bindings *base_environment,
    const PettaMachineHost *host,
    PettaTableShared *table_shared,
    bool owns_table_shared, bool bypass_root_table,
    size_t table_generator);

typedef struct {
    Atom *result;
    Bindings environment;
    bool present;
    bool capacity;
} PettaClauseMatch;

static bool petta_machine_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0)
        enabled = getenv("CETTA_PETTA_MACHINE_TRACE") ? 1 : 0;
    return enabled == 1;
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
    arena_free(&shared->arena);
    free(shared);
}

static bool petta_table_shared_epoch_current(
    const PettaTableShared *shared) {
    return shared &&
           shared->mutation_epoch ==
               space_global_mutation_epoch();
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
    PettaTableShared *shared, Atom *key, uint64_t hash,
    size_t *index_out, bool *inserted_out) {
    if (index_out)
        *index_out = PETTA_TABLE_ENTRY_NONE;
    if (inserted_out)
        *inserted_out = false;
    if (!shared || !key || !index_out || !inserted_out)
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

    Atom *owned = atom_deep_copy(&shared->arena, key);
    if (!owned)
        return false;
    size_t index = shared->entry_len++;
    shared->entries[index] = (PettaTableEntry){
        .key = owned,
        .hash = hash,
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
        slot->existing_count++;
        *changed = true;
    }
    free(slots);
    return true;
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
        if (choice->as.collapse.machine) {
            petta_machine_destroy(
                choice->as.collapse.machine);
            free(choice->as.collapse.machine);
            choice->as.collapse.machine = NULL;
        }
        free(choice->as.collapse.items);
        choice->as.collapse.items = NULL;
        choice->as.collapse.item_len = 0u;
        choice->as.collapse.item_cap = 0u;
    }
    if (choice->kind == PETTA_CHOICE_COUNT_COLLAPSE) {
        if (choice->as.count_collapse.machine) {
            petta_machine_destroy(
                choice->as.count_collapse.machine);
            free(choice->as.count_collapse.machine);
            choice->as.count_collapse.machine = NULL;
        }
    }
    if (choice->kind == PETTA_CHOICE_MATCH) {
        free(choice->as.match.snapshot);
        choice->as.match.snapshot = NULL;
        choice->as.match.snapshot_len = 0u;
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
    choice.previous_protected_goal_height =
        machine->protected_goal_height;
    machine->stats.choice_continuation_snapshots++;
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
    if (!petta_goal_trail_rollback(
            machine, choice->goal_trail_mark) ||
        choice->goal_height >
            machine->goal_initialized_len) {
        return false;
    }
    machine->goal_len = choice->goal_height;
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
    bindings->lookup_cache_count = 0u;
    bindings->lookup_cache_next = 0u;
    if (!ok)
        return false;
    if (!bindings->prime_ext)
        return true;
    return prime_need_snapshot_promote(
               destination,
               &bindings->prime_ext->prime_need) &&
           prime_need_receipt_promote(
               destination,
               &bindings->prime_ext->prime_receipt);
}

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PettaBindingRoots;

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
    PettaBindingRoots *roots, const PettaGoal *goal) {
    return goal &&
           petta_binding_roots_add(roots, goal->first) &&
           petta_binding_roots_add(roots, goal->second) &&
           petta_binding_roots_add(roots, goal->third) &&
           petta_binding_roots_add(roots, goal->fourth);
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
            roots, choice->as.equal_default.expected);
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
    bool ok =
        petta_binding_roots_add(&roots, machine->query) &&
        petta_binding_roots_add(
            &roots, machine->answer_variable);
    for (size_t i = 0u; ok && i < machine->visible_len; i++) {
        ok = petta_binding_roots_add(
            &roots, machine->visible[i].variable);
    }
    size_t rooted_goal_len = machine->goal_len >
            machine->protected_goal_height
        ? machine->goal_len : machine->protected_goal_height;
    if (rooted_goal_len > machine->goal_initialized_len) {
        free(roots.items);
        return false;
    }
    for (size_t i = 0u; ok && i < rooted_goal_len; i++)
        ok = petta_binding_roots_add_goal(
            &roots, &machine->goals[i]);
    for (size_t i = 0u;
         ok && i < machine->goal_trail_len; i++) {
        ok = petta_binding_roots_add_goal(
            &roots, &machine->goal_trail[i].previous);
    }
    for (size_t i = 0u; ok && i < machine->choice_len; i++)
        ok = petta_binding_roots_add_choice(
            &roots, &machine->choices[i]);
    if (!ok) {
        free(roots.items);
        return false;
    }
    if (machine->choice_len >
        SIZE_MAX / sizeof(uint32_t)) {
        free(roots.items);
        return false;
    }
    uint32_t *marks = cetta_malloc(
        machine->choice_len * sizeof(*marks));
    for (size_t i = 0u; i < machine->choice_len; i++)
        marks[i] = machine->choices[i].trail.bindings_mark;

    uint64_t discarded_items = 0u;
    uint64_t discarded_trail = 0u;
    ok = bindings_builder_compact_reachable(
        search_context_builder(&machine->search),
        roots.items, roots.len, marks, machine->choice_len,
        &discarded_items, &discarded_trail);
    free(roots.items);
    if (!ok) {
        free(marks);
        return false;
    }
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
    if (machine->goal_len > (SIZE_MAX - 2u - machine->visible_len) / 4u)
        return false;
    size_t root_count =
        2u + machine->visible_len + machine->goal_len * 4u;
    if (root_count > SIZE_MAX / sizeof(Atom *))
        return false;
    Atom **roots = cetta_malloc(root_count * sizeof(*roots));
    size_t root_index = 0u;
    roots[root_index++] = machine->query;
    roots[root_index++] = machine->answer_variable;
    for (size_t i = 0u; i < machine->visible_len; i++)
        roots[root_index++] = machine->visible[i].variable;
    for (size_t i = 0u; i < machine->goal_len; i++) {
        roots[root_index++] = machine->goals[i].first;
        roots[root_index++] = machine->goals[i].second;
        roots[root_index++] = machine->goals[i].third;
        roots[root_index++] = machine->goals[i].fourth;
    }
    assert(root_index == root_count);

    BindingsBuilder *builder =
        search_context_builder(&machine->search);
    const Bindings *current = bindings_builder_bindings(builder);
    Bindings projected;
    if (!bindings_project_reachable(
            current, roots, root_count, &projected)) {
        free(roots);
        return false;
    }
    free(roots);

    PettaGoal *next_goals = NULL;
    PettaVisibleVariable *next_visible = NULL;
    if (machine->goal_len > 0u) {
        if (machine->goal_len >
            SIZE_MAX / sizeof(*next_goals)) {
            bindings_free(&projected);
            return false;
        }
        next_goals = cetta_malloc(
            machine->goal_len * sizeof(*next_goals));
        memcpy(next_goals, machine->goals,
               machine->goal_len * sizeof(*next_goals));
    }
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
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(destination);
    if (!session) {
        bindings_free(&projected);
        free(next_goals);
        free(next_visible);
        if (major)
            arena_free(&next_tenured);
        return false;
    }
    bool ok = true;
    Atom *next_query = petta_copy_optional_atom(
        session, machine->query, &ok);
    Atom *next_answer_variable = petta_copy_optional_atom(
        session, machine->answer_variable, &ok);
    for (size_t i = 0u; i < machine->visible_len; i++) {
        next_visible[i].variable = petta_copy_optional_atom(
            session, next_visible[i].variable, &ok);
    }
    for (size_t i = 0u; i < machine->goal_len; i++) {
        next_goals[i].first = petta_copy_optional_atom(
            session, next_goals[i].first, &ok);
        next_goals[i].second = petta_copy_optional_atom(
            session, next_goals[i].second, &ok);
        next_goals[i].third = petta_copy_optional_atom(
            session, next_goals[i].third, &ok);
        next_goals[i].fourth = petta_copy_optional_atom(
            session, next_goals[i].fourth, &ok);
    }
    if (ok)
        ok = petta_copy_binding_atoms(
            &projected, destination, session);
    atom_deep_copy_session_free(session);
    if (!ok || !next_query || !next_answer_variable) {
        bindings_free(&projected);
        free(next_goals);
        free(next_visible);
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
    free(next_goals);
    free(next_visible);

    bindings_builder_free(&machine->search.bindings);
    bindings_builder_init_owned(
        &machine->search.bindings, &projected);
    machine->search.bindings.growth_count = binding_growth;

    machine->stats.deterministic_heap_collections++;
    if (major)
        machine->stats.deterministic_major_heap_collections++;
    else
        machine->stats.deterministic_minor_heap_collections++;
    machine->stats.deterministic_goal_roots_scanned +=
        (uint64_t)machine->goal_len;
    machine->stats.deterministic_heap_bytes_promoted +=
        (uint64_t)promoted_bytes;
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
    PettaClauseMatch *match) {
    if (!machine || !equation || !query || !match ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0], g_builtin_syms.equals) ||
        !petta_semantics_contains_cons_constraint(
            equation->expr.elems[1])) {
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
 * A ground call has no query-visible variables.  Its ordinary equation match
 * therefore computes only fresh clause-local bindings, then projects those
 * bindings away after rewriting the body.  Perform that same relation on the
 * machine's existing trail: the enclosing clause choice already owns the
 * rollback mark, so successful bindings can feed the body without a cloned
 * environment or a clone-and-merge round trip.
 *
 * Open calls, relational heads, and constrained cons patterns retain the
 * general matcher.  The switch is deliberately off by default so exact
 * OFF/ON comparison remains the authority for any future promotion.
 */
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
    left = petta_machine_apply_bindings(machine,
        environment, &machine->heap, left);
    right = petta_machine_apply_bindings(machine,
        environment, &machine->heap, right);
    return left && right &&
           petta_machine_unify_resolved(
               machine, left, right);
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

static bool petta_push_clause_result(
    PettaMachineImpl *machine, Atom *result, Atom *expected,
    uint32_t barrier, bool evaluate_result,
    bool translate_result, bool count_collection_result,
    const PettaPlanNode *plan) {
    if (!evaluate_result)
        return petta_push_unify(
            machine, result, expected, barrier);
    if (count_collection_result && !translate_result) {
        return petta_push_counted_collection_planned(
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
        return petta_push_solve_planned(
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
    return petta_push_force(
               machine, generated, translated, barrier) &&
           petta_push_solve_planned(
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

static bool petta_push_evaluated_expression_planned(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    PettaGoalKind final_kind, CettaExprIndex first_evaluated,
    uint32_t barrier, const PettaPlanNode *plan);

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
    Atom **slots = arena_alloc(
        &machine->heap, sizeof(*slots) * (size_t)length);
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
    CettaExprLen length = expression->expr.len;
    if (first_evaluated > length ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) {
        return false;
    }
    Atom **ready_elements = arena_alloc(
        &machine->heap, sizeof(*ready_elements) * (size_t)length);
    uint8_t *needs_evaluation = arena_alloc(
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
        if (index < first_evaluated) {
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
    for (CettaExprIndex index = length;
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
    return petta_push_solve_planned(
        machine, expression->expr.elems[1], ready_name,
        barrier, petta_plan_child(plan, 1u));
}

static bool petta_machine_boolean(
    PettaMachineImpl *machine, bool value, Atom *expected) {
    Atom *boolean = petta_semantics_boolean_value(
        &machine->heap, value);
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

/*
 * Open-cons nodes are an internal relational carrier.  Remove them
 * recursively at the answer boundary while leaving quoted syntax and
 * suspended callables opaque.  Thus a list nested inside an ordinary tuple
 * prints as an ordinary PeTTa list, but a lambda body is still not forced or
 * rewritten merely because the closure is returned.
 */
static Atom *petta_machine_materialize_answer_rec(
    PettaMachineImpl *machine, Atom *atom, uint32_t depth) {
    if (!machine || !atom || depth > 2048u)
        return NULL;
    atom = petta_machine_apply_bindings(machine,
        search_context_bindings(&machine->search),
        &machine->heap, atom);
    if (!atom)
        return NULL;
    if (petta_semantics_is_open_cons_value(atom)) {
        Atom *list = petta_machine_materialize_list(
            machine, atom);
        if (!list || list == atom)
            return list;
        return petta_machine_materialize_answer_rec(
            machine, list, depth + 1u);
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return atom;

    Atom *callable_body = NULL;
    if (petta_semantics_lambda_body(atom, &callable_body) ||
        petta_semantics_nullary_lambda_body(
            atom, &callable_body) ||
        petta_semantics_partial_view(atom, NULL, NULL) ||
        atom_is_symbol_id(
            atom->expr.elems[0], g_builtin_syms.quote)) {
        return atom;
    }
    if (!cetta_expr_len_mul_fits_size(
            atom->expr.len, sizeof(Atom *))) {
        return NULL;
    }
    Atom **elements = arena_alloc(
        &machine->heap,
        sizeof(*elements) * (size_t)atom->expr.len);
    bool changed = false;
    for (CettaExprIndex index = 0u;
         index < atom->expr.len; index++) {
        elements[index] =
            petta_machine_materialize_answer_rec(
                machine, atom->expr.elems[index],
                depth + 1u);
        if (!elements[index])
            return NULL;
        changed = changed ||
                  elements[index] != atom->expr.elems[index];
    }
    return changed
        ? atom_expr(&machine->heap, elements, atom->expr.len)
        : atom;
}

static Atom *petta_machine_materialize_answer(
    PettaMachineImpl *machine, Atom *atom) {
    return petta_machine_materialize_answer_rec(
        machine, atom, 0u);
}

static bool petta_symbol_name_is(SymbolId id, const char *name) {
    const char *actual =
        id == SYMBOL_ID_NONE ? NULL : symbol_bytes(g_symbols, id);
    return actual && strcmp(actual, name) == 0;
}

static bool petta_machine_foreign_callable(
    PettaMachineImpl *machine, Atom *atom) {
    if (!machine || !atom ||
        atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        !machine->host.foreign_named_arity) {
        return false;
    }
    PeTTaNamedArity arity =
        machine->host.foreign_named_arity(
            machine->host.context,
            atom->expr.elems[0]->sym_id,
            atom->expr.len - 1u);
    /* A name the boundary knows only at OTHER arities is data at this
     * occurrence; the reference calls engine predicates at their exact
     * arity alone. */
    return arity.exact;
}

typedef struct {
    Atom *atom;
    uint32_t depth;
    const PettaSpecializerPatternNode *pattern;
} PettaDataWalkItem;

static bool petta_machine_is_rigid_data(
    PettaMachineImpl *machine, Atom *root) {
    if (!machine || !root || atom_has_vars(root))
        return false;
    PettaDataWalkItem *stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
#define PETTA_DATA_PUSH(value, item_depth) do { \
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
        .atom = (value), .depth = (item_depth)}; \
} while (0)
    PETTA_DATA_PUSH(root, 0u);
    while (length > 0u) {
        PettaDataWalkItem item = stack[--length];
        Atom *atom = item.atom;
        if (!atom || item.depth > 2048u) {
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
            head == g_builtin_syms.quote ||
            head == g_builtin_syms.return_text ||
            head == g_builtin_syms.superpose ||
            petta_semantics_boolean_relation_arity(head, NULL) ||
            petta_symbol_name_is(head, "member") ||
            petta_symbol_name_is(head, "last") ||
            petta_symbol_name_is(head, "reverse") ||
            petta_symbol_name_is(head, "if") ||
            form != PETTA_FORM_NONE ||
            (head != SYMBOL_ID_NONE &&
             space_equations_may_match_known_head(
                 machine->space, head)) ||
            petta_machine_foreign_callable(machine, atom) ||
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
            PETTA_DATA_PUSH(
                atom->expr.elems[index - 1u],
                item.depth + 1u);
        }
    }
    free(stack);
#undef PETTA_DATA_PUSH
    return true;
}

static bool petta_machine_contains_callable(
    PettaMachineImpl *machine, Atom *root, bool include_root,
    const PettaSpecializerPatternNode *pattern_root) {
    if (!machine || !root)
        return false;
    PettaDataWalkItem *stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
#define PETTA_CALLABLE_PUSH(value, item_depth, item_pattern) do { \
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
        .atom = (value), .depth = (item_depth), \
        .pattern = (item_pattern)}; \
} while (0)
    if (root->kind != ATOM_EXPR) {
        free(stack);
        return false;
    }
    CettaExprIndex first = include_root ? 0u : 1u;
    for (CettaExprIndex index = root->expr.len;
         index > first; index--) {
        PETTA_CALLABLE_PUSH(
            root->expr.elems[index - 1u], 0u,
            petta_specializer_pattern_child(
                pattern_root, index - 1u));
    }
    while (length > 0u) {
        PettaDataWalkItem item = stack[--length];
        Atom *atom = item.atom;
        if (!atom || item.depth > 2048u) {
            free(stack);
            return false;
        }
        if (atom->kind != ATOM_EXPR ||
            atom->expr.len == 0u) {
            continue;
        }
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
             petta_semantics_boolean_relation_arity(head, NULL) ||
             petta_symbol_name_is(head, "member") ||
             petta_symbol_name_is(head, "last") ||
             petta_symbol_name_is(head, "reverse") ||
             petta_symbol_name_is(head, "if") ||
             form != PETTA_FORM_NONE ||
             (head != SYMBOL_ID_NONE &&
              space_equations_may_match_known_head(
                  machine->space, head)) ||
             petta_machine_foreign_callable(machine, atom) ||
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
                item.depth + 1u,
                petta_specializer_pattern_child(
                    item.pattern, index - 1u));
        }
    }
    free(stack);
#undef PETTA_CALLABLE_PUSH
    return false;
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
           petta_semantics_boolean_relation_arity(head, NULL) ||
           petta_symbol_name_is(head, "member") ||
           petta_symbol_name_is(head, "last") ||
           petta_symbol_name_is(head, "reverse") ||
           petta_symbol_name_is(head, "if") ||
           form != PETTA_FORM_NONE ||
           (head != SYMBOL_ID_NONE &&
            space_equations_may_match_known_head(
                machine->space, head)) ||
           petta_machine_foreign_callable(machine, atom) ||
           (machine->host.classify &&
            machine->host.classify(
                machine->host.context, machine->space, atom) !=
                 PETTA_MACHINE_HOST_NONE);
    return callable;
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
    const PettaSpecializerPatternNode *pattern_role) {
    enum {
        PETTA_CLAUSE_SHAPE_STACK_CAPACITY = 128,
        PETTA_CLAUSE_SHAPE_NODE_LIMIT = 512,
    };
    PettaClauseShapePair stack[
        PETTA_CLAUSE_SHAPE_STACK_CAPACITY];
    size_t length = 0u;
    size_t visited = 0u;

    if (!machine || !pattern || !value ||
        pattern->kind != ATOM_EXPR ||
        value->kind != ATOM_EXPR) {
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
        if (petta_semantics_is_open_cons_value(left) ||
            petta_semantics_is_open_cons_value(right)) {
            continue;
        }
        if (petta_semantics_is_cons_constraint(left)) {
            if (!petta_semantics_cons_pattern_may_match(left, right))
                return false;
            continue;
        }
        if (left->kind == ATOM_EXPR &&
            petta_machine_callable_root(
                machine, left, pair.pattern_role)) {
            continue;
        }
        if (left->kind != right->kind)
            return false;
        if (left->kind != ATOM_EXPR) {
            if (!atom_eq(left, right))
                return false;
            continue;
        }
        if (left->expr.len != right->expr.len)
            return false;
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
    size_t candidate_count) {
    if (!entry || !candidates ||
        entry->equation_len != candidate_count)
        return false;
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
    if (pattern->kind == ATOM_EXPR &&
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
    return petta_clause_pattern_may_match_now(
        context->machine, pattern, query,
        petta_specializer_pattern_root(
            context->machine->space,
            context->candidates[source_ref].equation));
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
    PettaClauseCandidate *candidates, size_t candidate_count,
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
                entry, candidates, candidate_count)) {
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

    Atom **equations = malloc(sizeof(*equations) * candidate_count);
    if (!equations) {
        cetta_match_decision_free(decision);
        return NULL;
    }
    for (size_t index = 0u; index < candidate_count; index++)
        equations[index] = candidates[index].equation;
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
            .equations = equations,
            .equation_len = candidate_count,
            .decision = decision,
        };
    machine->stats.match_decision_compilations++;
    return decision;
}

static CettaMatchDecisionSelectState
petta_match_decision_select_candidates(
    PettaMachineImpl *machine, SymbolId head, Atom *query,
    PettaClauseCandidate *candidates, size_t candidate_count,
    bool deep_admissible,
    const uint32_t **selected, size_t *selected_count,
    bool *structurally_verified) {
    if (structurally_verified)
        *structurally_verified = false;
    if (!machine || !query || !candidates || candidate_count == 0u ||
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
        machine, head,
        query->kind == ATOM_EXPR && query->expr.len > 0u
            ? query->expr.len - 1u : 0u,
        candidates, candidate_count, mode, &context);
    if (!decision)
        return CETTA_MATCH_DECISION_SELECT_ERROR;

    CettaMatchDecisionStats before = {0};
    CettaMatchDecisionStats after = {0};
    cetta_match_decision_stats(decision, &before);
    CettaMatchDecisionSelectState state = cetta_match_decision_select(
        decision, machine->space,
        machine->host.match_decision_semantics,
        query, UINT64_MAX,
        mode == CETTA_MATCH_DECISION_DEEP
            ? petta_match_decision_verify_candidate : NULL,
        &context, selected, selected_count);
    cetta_match_decision_stats(decision, &after);
    machine->stats.match_decision_runs += after.runs - before.runs;
    machine->stats.match_decision_clause_inputs +=
        after.clause_inputs - before.clause_inputs;
    machine->stats.match_decision_clause_survivors +=
        after.clause_survivors - before.clause_survivors;
    machine->stats.match_decision_linear_fallbacks +=
        after.linear_fallbacks - before.linear_fallbacks;
    machine->stats.match_decision_unavailable_path_fallbacks +=
        after.unavailable_path_fallbacks -
        before.unavailable_path_fallbacks;
    if (state == CETTA_MATCH_DECISION_SELECT_INVALIDATED)
        machine->stats.match_decision_invalidations++;
    if (state == CETTA_MATCH_DECISION_SELECT_READY)
        *structurally_verified = mode == CETTA_MATCH_DECISION_DEEP;
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
    const PettaSpecializerPatternNode *pattern_role) {
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
                 item.pattern_role));
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
        bool pushed = action->solve
            ? petta_push_solve(
                  machine, action->pattern, action->value,
                  barrier)
            : petta_push_unify(
                  machine, action->pattern, action->value,
                  barrier);
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
    bool count_collection_result);

static bool petta_machine_start_outcome_choice(
    PettaMachineImpl *machine, OutcomeSet *outcomes, Atom *expected,
    uint32_t barrier);

static bool petta_machine_try_foreign_call(
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

static bool petta_machine_type_is_expression(const Atom *type) {
    return type && type->kind == ATOM_SYMBOL &&
           petta_symbol_name_is(type->sym_id, "Expression");
}

static bool petta_machine_schedule_typed_call(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    Atom *type, uint32_t barrier) {
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
        ready_elements[index] =
            petta_machine_type_is_expression(type->expr.elems[index])
                ? expression->expr.elems[index]
                : petta_fresh_variable(machine);
        if (!ready_elements[index])
            return false;
    }
    Atom *ready = atom_expr(&machine->heap, ready_elements, length);
    Atom *result = petta_fresh_variable(machine);
    Atom *result_type = type->expr.elems[nargs + 1u];
    bool result_is_expression =
        petta_machine_type_is_expression(result_type);
    if (!ready || !result ||
        !petta_push_unify(
            machine, result, expected, barrier) ||
        (!result_is_expression &&
         !petta_goal_push(
             machine,
             (PettaGoal){
                 .kind = PETTA_GOAL_TYPE_ACCEPT,
                 .barrier = barrier,
                 .first = result,
                 .second = result_type,
             })) ||
        !petta_goal_push(
            machine,
            (PettaGoal){
                .kind = result_is_expression
                    ? PETTA_GOAL_CALL_READY_DATA
                    : PETTA_GOAL_CALL_READY,
                .barrier = barrier,
                .first = ready,
                .second = result,
            })) {
        return false;
    }
    for (CettaExprIndex index = length; index > 1u; index--) {
        CettaExprIndex argument = index - 1u;
        if (petta_machine_type_is_expression(
                type->expr.elems[argument])) {
            continue;
        }
        if (!petta_goal_push(
                machine,
                (PettaGoal){
                    .kind = PETTA_GOAL_TYPE_ACCEPT,
                    .barrier = barrier,
                    .first = ready_elements[argument],
                    .second = type->expr.elems[argument],
                })) {
            return false;
        }
        if (!petta_push_solve(
                machine, expression->expr.elems[argument],
                ready_elements[argument], barrier)) {
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
    if (head_atom && head_atom->kind == ATOM_SYMBOL &&
        machine->host.foreign_named_arity) {
        PeTTaNamedArity foreign =
            machine->host.foreign_named_arity(
                machine->host.context,
                head_atom->sym_id, nargs);
        result.known = result.known || foreign.known;
        result.exact = result.exact || foreign.exact;
        result.larger = result.larger || foreign.larger;
    }
    return result;
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
    /*
     * Currying is a property of the native function registry.  A bare
     * engine predicate visible only at larger arities is ordinary data at
     * this occurrence (the reference calls such names solely at their
     * exact arity), so only native arity knowledge may open a partial.
     */
    PeTTaNamedArity native = petta_semantics_named_arity(
        machine->space, &machine->heap,
        expression->expr.elems[0], nargs);
    if (native.larger)
        return PETTA_PARTIAL_YES;

    PettaExtensionState extension =
        petta_machine_extension_state(machine, expression);
    if (extension == PETTA_EXTENSION_INVALIDATED)
        return PETTA_PARTIAL_INVALIDATED;
    return extension == PETTA_EXTENSION_PRESENT
        ? PETTA_PARTIAL_NO : PETTA_PARTIAL_YES;
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
    root->role = PETTA_PLAN_DYNAMIC_CALL;
    root->child_count = application->expr.len;
    root->children = children;
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
    target->clause_snapshot_equality_checks +=
        source->clause_snapshot_equality_checks;
    target->clause_snapshot_alpha_checks +=
        source->clause_snapshot_alpha_checks;
    target->clause_snapshot_candidates +=
        source->clause_snapshot_candidates;
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
    target->match_decision_linear_fallbacks +=
        source->match_decision_linear_fallbacks;
    target->match_decision_unavailable_path_fallbacks +=
        source->match_decision_unavailable_path_fallbacks;
    target->match_decision_invalidations +=
        source->match_decision_invalidations;
    target->clause_match_attempts +=
        source->clause_match_attempts;
    target->clause_match_allocated_bytes +=
        source->clause_match_allocated_bytes;
    target->match_candidates += source->match_candidates;
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
    target->atom_copy_calls += source->atom_copy_calls;
    target->atom_copy_allocated_bytes +=
        source->atom_copy_allocated_bytes;
    target->atom_freshen_calls += source->atom_freshen_calls;
    target->atom_freshen_allocated_bytes +=
        source->atom_freshen_allocated_bytes;
    target->specializer_prepare_calls +=
        source->specializer_prepare_calls;
    target->specializer_prepare_filtered +=
        source->specializer_prepare_filtered;
    target->specializer_prepare_relevance_bounded +=
        source->specializer_prepare_relevance_bounded;
    target->specializer_prepare_rewritten +=
        source->specializer_prepare_rewritten;
    target->specializer_prepare_unchanged +=
        source->specializer_prepare_unchanged;
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
    target->deterministic_goal_roots_scanned +=
        source->deterministic_goal_roots_scanned;
    target->deterministic_heap_bytes_promoted +=
        source->deterministic_heap_bytes_promoted;
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
    target->choice_heap_resets +=
        source->choice_heap_resets;
    target->choice_heap_bytes_reclaimed +=
        source->choice_heap_bytes_reclaimed;
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
            ->entries[choice->as.table.iteration_entry].key;
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
                &empty, &machine->host, machine->table_shared,
            false, true, choice->as.table.iteration_entry)) {
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
                        choice->barrier) ||
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
        while (choice->as.clause.next_equation <
               choice->as.clause.equation_len) {
            PettaClauseCandidate candidate =
                choice->as.clause.candidates[
                    choice->as.clause.next_equation++];
            Atom *equation = candidate.equation;
            Atom *query = petta_machine_apply_bindings(machine,
                search_context_bindings(&machine->search),
                &machine->heap, choice->as.clause.query);
            Atom *lhs_shape =
                equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u
                    ? equation->expr.elems[1]
                    : NULL;
            const PettaSpecializerPatternNode *pattern_root =
                petta_specializer_pattern_root(
                    machine->space, equation);
            if (!petta_clause_pattern_may_match_now(
                    machine, lhs_shape, query, pattern_root)) {
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
                choice->as.clause.evaluate_result &&
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
            bool relational_head =
                equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u &&
                petta_machine_contains_callable(
                    machine,
                    equation->expr.elems[1],
                    false, pattern_root);
            size_t match_heap_before =
                arena_accounted_live_bytes(&machine->heap);
            if (!relational_head) {
                bool handled = petta_machine_cons_clause_match(
                    machine, equation, match_query, &match);
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
                bool merged =
                    petta_machine_merge(
                        machine, &match.environment,
                        PETTA_BINDING_MERGE_CLAUSE);
                bindings_free(&match.environment);
                if (!merged)
                    continue;
                Atom *result = match.result;
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
                    if (!evidence_merged)
                        continue;
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
                bool scheduled = petta_push_clause_result(
                    machine, result,
                    choice->as.clause.expected,
                    choice->barrier,
                    choice->as.clause.evaluate_result,
                    choice->as.clause.translate_result,
                    choice->as.clause.count_collection_result,
                    result_plan);
                if (!scheduled) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
                return true;
            }
            bindings_free(&match.environment);

            /*
             * PeTTa permits a relation call in an equation-head argument:
             * `(= (h (f $x)) rhs)`.  When ordinary structural matching
             * cannot discharge such a head, resolve the clause exactly as
             * an SLD step: freshen the whole clause, solve each head
             * argument against the corresponding call argument, then run
             * the body under the resulting bindings.  The clause choice
             * remains below these goals, so failure resumes at the next
             * occurrence without a special-case retry path.
             */
            uint32_t epoch = fresh_var_suffix();
            uint64_t freshen_bytes_before =
                machine->stats.atom_freshen_allocated_bytes;
            (void)freshen_bytes_before;
            Atom *fresh_equation = petta_machine_freshen_atom(
                machine, equation, epoch);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_WHOLE_CLAUSE_FRESHEN);
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_WHOLE_CLAUSE_FRESHEN_BYTES,
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
                result_plan);
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
                            pattern_root, argument))) {
                    *failure = PETTA_MACHINE_STEP_CAPACITY;
                    return false;
                }
            }
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
                    choice->barrier)) {
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
            if (!cetta_expr_len_fits_size(
                    (CettaExprLen)
                        choice->as.collapse.item_len)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *collected = atom_expr(
                &machine->heap,
                choice->as.collapse.items,
                (CettaExprLen)
                    choice->as.collapse.item_len);
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
                    machine->stats.count_aggregate_answers +=
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
            Atom *count =
                choice->as.count_collapse.wrap_collection
                    ? atom_petta_counted_collection(
                          &machine->heap,
                          (int64_t)
                              choice->as.count_collapse.count)
                    : atom_int(
                          &machine->heap,
                          (int64_t)
                              choice->as.count_collapse.count);
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
    CettaCount length = choice->as.match.snapshot_mode
        ? choice->as.match.snapshot_len
        : space_length64(choice->as.match.space);
    while (choice->as.match.next_index < length) {
        CettaIndex candidate_index =
            choice->as.match.next_index++;
        Atom *candidate = choice->as.match.snapshot_mode
            ? choice->as.match.snapshot[candidate_index]
            : space_get_at64(
                  choice->as.match.space, candidate_index);
        if (!candidate)
            continue;
        petta_machine_trace_atom(
            "[petta-machine] match candidate ", candidate);
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
                machine, choice->as.match.pattern, fresh)) {
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

static bool petta_machine_start_clause_choice(
    PettaMachineImpl *machine, Atom *query, Atom *expected,
    uint32_t barrier, bool evaluate_result,
    bool count_collection_result) {
    SymbolId head = atom_head_symbol_id(query);
    bool translate_result =
        machine->host.translator_rule_contains &&
        machine->host.translator_rule_contains(
            machine->host.context, head);
    if (head == SYMBOL_ID_NONE) {
        return false;
    }
    /*
     * Clause selection obeys the same logical-update view as `match`.
     * Snapshot only the head-indexed candidate occurrences, rather than the
     * whole space, so a clause may add or remove definitions without
     * invalidating the alternatives of the call already in progress.
     */
    PettaClauseCandidate *equations = NULL;
    size_t equation_len = 0u;
    size_t equation_cap = 0u;
    PettaClauseSnapshotStats snapshot_stats = {0};
    if (machine->host.clause_snapshot) {
        if (!machine->host.clause_snapshot(
                machine->host.context, machine->space, head,
                &equations, &equation_len, &snapshot_stats)) {
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
                free(equations);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            SpaceEquationOccurrence occurrence;
            if (!space_equation_occurrence_resolve(
                    id, &occurrence)) {
                free(equations);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            if (!petta_machine_reserve(
                    (void **)&equations, &equation_cap,
                    equation_len + 1u, sizeof(*equations))) {
                free(equations);
                machine->terminal = true;
                machine->terminal_step =
                    PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            equations[equation_len++] =
                (PettaClauseCandidate){
                    .equation = occurrence.equation,
                    .occurrence = occurrence.id,
                };
            snapshot_stats.live_occurrences_scanned++;
        }
        snapshot_stats.candidates_emitted = equation_len;
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
        &machine->stats.clause_snapshot_equality_checks,
        snapshot_stats.structural_equality_checks);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_alpha_checks,
        snapshot_stats.alpha_equality_checks);
    petta_machine_add_u64(
        &machine->stats.clause_snapshot_candidates,
        snapshot_stats.candidates_emitted);

    bool candidates_structurally_verified = false;
    if (equation_len > 0u) {
        bool deep_admissible =
            machine->host.tabled_relation_admissible &&
            machine->host.tabled_relation_admissible(
                machine->host.context, machine->space, head,
                query->kind == ATOM_EXPR && query->expr.len > 0u
                    ? query->expr.len - 1u : 0u);
        const uint32_t *selected = NULL;
        size_t selected_count = 0u;
        CettaMatchDecisionSelectState selection =
            petta_match_decision_select_candidates(
                machine, head, query, equations, equation_len,
                deep_admissible, &selected, &selected_count,
                &candidates_structurally_verified);
        if (selection != CETTA_MATCH_DECISION_SELECT_READY) {
            free(equations);
            machine->terminal = true;
            machine->terminal_step =
                selection == CETTA_MATCH_DECISION_SELECT_INVALIDATED
                    ? PETTA_MACHINE_STEP_INVALIDATED
                    : PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
        size_t original_len = equation_len;
        for (size_t index = 0u; index < selected_count; index++) {
            if (selected[index] >= original_len) {
                free(equations);
                machine->terminal = true;
                machine->terminal_step = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            equations[index] = equations[selected[index]];
        }
        equation_len = selected_count;
        petta_machine_add_u64(
            &machine->stats.clause_candidates_shape_pruned,
            original_len - selected_count);

    }

    /*
     * Apply only proof-producing fragments of WAM-style clause indexing
     * before any candidate can bind the call.  Filtering against the
     * pre-choice query is load-bearing: using bindings installed by the first
     * answer would incorrectly prune later answers to a relational call such
     * as `(shape $items)`.
     *
     * The cons discriminator handles PeTTa's flat-list/open-spine duality.
     * The literal discriminator handles aligned, closed symbol/grounded/empty
     * arguments.  In each case only a structural impossibility is removed;
     * every ambiguous candidate stays in declaration order and reaches the
     * ordinary matcher.
     */
    if (!candidates_structurally_verified) {
        size_t retained = 0u;
        for (size_t index = 0u; index < equation_len; index++) {
            Atom *equation = equations[index].equation;
            Atom *lhs =
                equation && equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u
                    ? equation->expr.elems[1]
                    : NULL;
            bool may_match =
                !lhs ||
                petta_semantics_cons_pattern_may_match(lhs, query);
            if (may_match && lhs &&
                lhs->kind == ATOM_EXPR &&
                query->kind == ATOM_EXPR) {
                CettaExprLen aligned =
                    lhs->expr.len < query->expr.len
                        ? lhs->expr.len : query->expr.len;
                for (CettaExprIndex argument = 1u;
                     argument < aligned; argument++) {
                    Atom *pattern = lhs->expr.elems[argument];
                    Atom *value = query->expr.elems[argument];
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
            equations[retained++] = equations[index];
        }
        equation_len = retained;
    }

    size_t entry_depth = machine->choice_len;
    size_t goal_trail_mark = machine->goal_trail_len;
    uint64_t call_occurrence = 0u;
    if (machine->host.begin_relation_call) {
        call_occurrence = machine->host.begin_relation_call(
            machine->host.context, space_read_token(machine->space), query);
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
        .barrier = (uint32_t)entry_depth,
        .as.clause = {
            .candidates = equations,
            .equation_len = equation_len,
            .next_equation = 0u,
            .call_occurrence = call_occurrence,
            .query = query,
            .expected = expected,
            .evaluate_result =
                evaluate_result || translate_result,
            .translate_result = translate_result,
            .count_collection_result =
                count_collection_result,
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

static bool petta_machine_start_outcome_choice(
    PettaMachineImpl *machine, OutcomeSet *outcomes, Atom *expected,
    uint32_t barrier) {
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

static bool petta_machine_try_foreign_call(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    uint32_t barrier, bool *recognized,
    PettaMachineStep *failure) {
    if (recognized)
        *recognized = false;
    if (!machine || !expression || !expected ||
        !recognized || !failure ||
        !machine->host.foreign_call) {
        return machine && expression && expected &&
               recognized && failure;
    }

    OutcomeSet *outcomes = cetta_malloc(sizeof(*outcomes));
    outcome_set_init_with_owner(outcomes, &machine->heap);
    bool handled = false;
    bool called = machine->host.foreign_call(
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
    CettaVarMap query_to_slot;
    CettaVarMap goal_instantiation;
    cetta_var_map_init(&query_to_slot);
    cetta_var_map_init(&goal_instantiation);
    Atom *canonical = variant_shape_canonicalize_atom(
        &machine->heap, query, &query_to_slot,
        &goal_instantiation, &kPettaTableVariantOptions);
    cetta_var_map_free(&query_to_slot);
    uint64_t hash = canonical
        ? (uint64_t)atom_hash(canonical) : 0u;
    if (!canonical) {
        cetta_var_map_free(&goal_instantiation);
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
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
            machine->table_shared, canonical, hash,
            &entry_index, &inserted) ||
        entry_index >= machine->table_shared->entry_len) {
        cetta_var_map_free(&goal_instantiation);
        machine->table_shared->failed = true;
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    PettaTableEntry *entry =
        &machine->table_shared->entries[entry_index];
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
    if (!bindings_project_reachable(
            parent_environment, roots, 1u,
            &child_environment)) {
        return NULL;
    }
    PettaMachine *inner = cetta_malloc(sizeof(*inner));
    bool initialized = petta_machine_init_internal(
            inner, machine->space, &machine->heap, body,
            plan,
            &child_environment,
            &machine->host, machine->table_shared,
            false, false, machine->table_generator);
    bindings_free(&child_environment);
    if (!initialized) {
        free(inner);
        return NULL;
    }
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
        petta_machine_destroy(inner);
        free(inner);
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
        petta_machine_destroy(inner);
        free(inner);
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
    CettaIndex *candidate_indices = NULL;
    CettaIndex candidate_len = space_match_candidates64(
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
            Atom *candidate = space_get_at64(
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

static bool petta_machine_start_typed_call_choice(
    PettaMachineImpl *machine, Atom *expression, Atom *expected,
    Atom **types, uint32_t count, uint32_t barrier) {
    if (!machine || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u || !expected || !types ||
        count == 0u) {
        free(types);
        return false;
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
            .expression = expression,
            .expected = expected,
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

static bool petta_machine_type_accept(
    PettaMachineImpl *machine, Atom *value, Atom *formal,
    uint32_t barrier) {
    if (!machine || !value || !formal)
        return false;
    if (atom_is_symbol_id(
            formal, g_builtin_syms.undefined_type)) {
        return true;
    }
    if (atom_is_meta_type(formal))
        return atom_meta_type_accepts(
            &machine->heap, formal, value);

    /*
     * User types are a PeTTa relation, not merely a static annotation lookup.
     * Route the check through get-type so its intrinsic candidate clause and
     * explicit (= (get-type ...)) extensions share ordinary ordered
     * backtracking.  This is also what lets a formal type variable receive
     * the selected actual type through the normal trail.
     */
    Atom *type_call = atom_expr2(
        &machine->heap,
        atom_symbol_id(
            &machine->heap, g_builtin_syms.get_type),
        value);
    return type_call &&
           petta_push_solve(
               machine, type_call, formal, barrier);
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
    Atom *body, Atom *expected, uint32_t barrier) {
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
 * PeTTa has two foldl-atom surfaces:
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
    Atom *truth = petta_semantics_boolean_value(
        &machine->heap, true);
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
        if (!petta_machine_immediate_value(
                expression->expr.elems[index],
                petta_plan_child(plan, index))) {
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
    if (head == g_builtin_syms.collapse && nargs == 1u) {
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
        !petta_machine_foreign_callable(machine, expression) &&
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

static bool petta_machine_dispatch_solve(
    PettaMachineImpl *machine, const PettaGoal *goal,
    PettaMachineStep *failure) {
    const Bindings *environment =
        search_context_bindings(&machine->search);
    Atom *expression = petta_machine_apply_bindings(machine,
        environment, &machine->heap, goal->first);
    Atom *expected = petta_machine_apply_bindings(machine,
        environment, &machine->heap, goal->second);
    if (!expression || !expected) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }

    const PettaPlanNode *plan =
        goal->kind == PETTA_GOAL_FORCE ? NULL : goal->plan;

    if (goal->kind ==
        PETTA_GOAL_SOLVE_COUNTED_COLLECTION) {
        return petta_machine_dispatch_counted_collection(
            machine, goal, expression, expected,
            plan, failure);
    }

    /*
     * PeTTa compiles a source variable to a value-producing Prolog variable,
     * not to a runtime dispatch.  Keep that intensional role even when the
     * current environment substitutes an expression with a callable head.
     * An explicit FORCE goal (introduced only by eval/reduce) intentionally
     * evaluates the substituted value as code.
     */
    if (goal->kind == PETTA_GOAL_SOLVE &&
        ((plan && plan->role == PETTA_PLAN_VALUE) ||
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

    /*
     * The shared runtime owns OS-thread scheduling for hyperpose.  Delegate
     * only the two delimited wrappers for which that scheduler has a complete
     * contract: collect every branch into collapse, or race to the first
     * answer under once.  Ordinary hyperpose remains the relational choice
     * below, so all other control and backtracking stay in this machine.
     */
    bool host_hyperpose_wrapper =
        (head_id == g_builtin_syms.once ||
         head_id == g_builtin_syms.collapse) &&
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
            machine, expression->expr.elems[1], expected);

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
     * `data` is typecheck-v2's explicitly non-callable tuple: fields
     * evaluate, the head is erased, and no field value can turn the result
     * into a call.  Only the extended profile interprets it — base PeTTa
     * has no such head, so elsewhere it remains inert syntax.  Its erased
     * siblings brand/the never reach this machine: document ingestion
     * rewrites them away exactly as the reference translator does.
     */
    if (cetta_petta_typecheck_op_applies(head_id, nargs)) {
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
     * A runtime `:` expression is PeTTa DATA whose nested computations
     * evaluate in place: the reference registers no function under the
     * head, so its translator builds the structure with each child
     * translated as an ordinary expression (calls become calls, data stays
     * data) and the head surviving.  The chainer's externalizer depends on
     * this — (: KB (externalize-query-proof P) T TV) must evaluate the
     * proof walk while remaining a `:` tuple.  Handing the expression to
     * the host would instead apply the HE typing operator's data-owned
     * argument contract.
     */
    if (head_id == g_builtin_syms.colon && nargs >= 1u) {
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

    if (petta_symbol_name_is(head_id, "member") && nargs == 2u) {
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
        if (atom_petta_counted_collection_count(items, &counted)) {
            return petta_machine_unify(
                machine, atom_int(&machine->heap, counted), expected);
        }
        return petta_machine_start_list_length_choice(
            machine, items, expected, goal->barrier);
    }

    if (form == PETTA_FORM_LENGTH && nargs == 1u) {
        Atom *argument = expression->expr.elems[1];
        if (argument && argument->kind == ATOM_EXPR &&
            argument->expr.len == 2u &&
            atom_is_symbol_id(
                argument->expr.elems[0],
                g_builtin_syms.collapse)) {
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
         petta_symbol_name_is(head_id, "last") ||
         petta_symbol_name_is(head_id, "reverse") ||
         petta_symbol_name_is(head_id, "msort")) &&
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

    if (petta_symbol_name_is(head_id, "empty") && nargs == 0u)
        return false;

    if (head_id == g_builtin_syms.once && nargs == 1u) {
        return petta_machine_start_once(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u),
            failure);
    }

    if (petta_symbol_name_is(head_id, "transaction") &&
        nargs == 1u) {
        return petta_machine_start_transaction(
            machine, expression->expr.elems[1], expected,
            goal->barrier, petta_plan_child(plan, 1u),
            failure);
    }

    if (petta_symbol_name_is(head_id, "with_mutex") &&
        nargs == 2u) {
        return petta_machine_start_mutex(
            machine, expression->expr.elems[1],
            expression->expr.elems[2], expected,
            goal->barrier, petta_plan_child(plan, 2u),
            failure);
    }

    if (head_id == g_builtin_syms.collapse && nargs == 1u) {
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
     * foldl-atom continuation shape so both surfaces share one relational
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
        Atom *conjunction =
            petta_machine_lower_match_conjunction(
                machine, reference, expression->expr.elems[2],
                expression->expr.elems[3]);
        if (conjunction) {
            return petta_push_solve(
                machine, conjunction, expected,
                goal->barrier);
        }
        Space *target = machine->host.resolve_space
            ? machine->host.resolve_space(
                  machine->host.context, machine->space,
                  &machine->heap, reference)
            : NULL;
        if (!target && reference->kind == ATOM_SYMBOL &&
            petta_symbol_name_is(reference->sym_id, "&self")) {
            target = machine->space;
        }
        /*
         * A plain-symbol space that no registry binding resolves is the
         * reference's Prolog-clause representation: match builds
         * Term =.. [Space | PatternElems] and enumerates the predicate's
         * clauses, one template evaluation per solution.  Lower to the
         * boundary's callPredicate over the same cons cell — the pattern's
         * variables come back as ordinary solution bindings — guarded by
         * engine-side existence so a space with no clauses stays an empty
         * match exactly like the reference's fail-guard.
         */
        if (!target && reference->kind == ATOM_SYMBOL &&
            expression->expr.elems[2] &&
            expression->expr.elems[2]->kind == ATOM_EXPR &&
            expression->expr.elems[2]->expr.len > 0u) {
            /*
             * The reference's match on a plain-symbol space calls the
             * space-name predicate under catch(_, _, fail): every clause is
             * one solution, and a space with no clauses (or no predicate at
             * all) is an empty match, never an error.
             */
            CettaExprLen row_len =
                expression->expr.elems[2]->expr.len;
            PeTTaNamedArity space_arity =
                machine->host.foreign_named_arity_resolving
                    ? machine->host.foreign_named_arity_resolving(
                          machine->host.context,
                          reference->sym_id,
                          row_len > 0u ? row_len - 1u : 0u)
                    : (PeTTaNamedArity){0};
            if (!space_arity.known)
                return false;
            Atom *cons_symbol = atom_symbol(
                &machine->heap, "cons");
            Atom *predicate_symbol = atom_symbol(
                &machine->heap, "Predicate");
            Atom *call_symbol = atom_symbol(
                &machine->heap, "callPredicate");
            Atom *ignored = petta_fresh_variable(machine);
            if (!cons_symbol || !predicate_symbol ||
                !call_symbol || !ignored) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *cons_cell = atom_expr3(
                &machine->heap, cons_symbol, reference,
                expression->expr.elems[2]);
            Atom *predicate_value = cons_cell
                ? atom_expr2(
                      &machine->heap, predicate_symbol,
                      cons_cell)
                : NULL;
            Atom *call = predicate_value
                ? atom_expr2(
                      &machine->heap, call_symbol,
                      predicate_value)
                : NULL;
            Atom *let_symbol = atom_symbol_id(
                &machine->heap, g_builtin_syms.let);
            Atom *lowered = call && let_symbol
                ? atom_expr(
                      &machine->heap,
                      (Atom *[]){
                          let_symbol, ignored, call,
                          expression->expr.elems[3],
                      },
                      4u)
                : NULL;
            if (!lowered) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return petta_push_solve(
                machine, lowered, expected, goal->barrier);
        }
        if (!target)
            return false;
        return petta_machine_start_match_choice(
            machine, target, expression->expr.elems[2],
            expression->expr.elems[3], expected,
            goal->barrier, petta_plan_child(plan, 3u));
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
            Atom *truth = petta_semantics_boolean_value(
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
                !petta_symbol_name_is(
                    branch->expr.elems[0]->sym_id, "Empty")) {
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

    bool numeric_min =
        petta_symbol_name_is(head_id, "min");
    bool numeric_max =
        petta_symbol_name_is(head_id, "max");
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
     * PeTTa's source translator maps host exceptions to Error values.  The
     * native evaluator already represents grounded-operation failures as
     * Error atoms, so catch is a delimited value-preserving computation:
     * evaluate its body once and pass either its value or Error value
     * through unchanged.  Search failure remains search failure.
     */
    if (form == PETTA_FORM_CATCH && nargs == 1u) {
        return petta_push_solve_planned(
            machine, expression->expr.elems[1],
            expected, goal->barrier,
            petta_plan_child(plan, 1u));
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
                PETTA_GOAL_FOREIGN_READY, 1u,
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
        bool dispatched = petta_machine_try_foreign_call(
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
                    PETTA_GOAL_FOREIGN_READY, 1u,
                    goal->barrier, plan)) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            return true;
        }

        bool recognized = false;
        bool dispatched = petta_machine_try_foreign_call(
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
     * A name becomes a foreign function only after an explicit import.
     * PeTTa's function convention evaluates every supplied argument before
     * appending the result argument at the Prolog boundary.  Reify that
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
        machine->host.foreign_named_arity) {
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
        PeTTaNamedArity foreign_arity = {0};
        if (call_position) {
            foreign_arity =
                plan && plan->role == PETTA_PLAN_STATIC_CALL &&
                machine->host.foreign_named_arity_resolved
                    ? machine->host.foreign_named_arity_resolved(
                          machine->host.context, head_id, nargs)
                    : machine->host.foreign_named_arity(
                          machine->host.context, head_id, nargs);
        }
        if (call_position &&
            foreign_arity.known && foreign_arity.exact) {
            if (!petta_push_evaluated_expression_planned(
                    machine, expression, expected,
                    PETTA_GOAL_FOREIGN_READY, 1u,
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
        is_grounded_op(head_id) &&
        nargs <= UINT32_MAX &&
        petta_machine_all_arguments_immediate(
            expression, plan)) {
        Atom *direct = grounded_dispatch(
            &machine->heap, head,
            expression->expr.elems + 1u,
            (uint32_t)nargs);
        if (direct) {
            bool truth = false;
            if (petta_semantics_truth_value(direct, &truth)) {
                direct = petta_semantics_boolean_value(
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
                declared_type_count, goal->barrier);
        }
        free(declared_types);
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
        if (goal.plan)
            fprintf(stderr, "plan=%d ", (int)goal.plan->role);
        if (goal.first)
            atom_print(goal.first, stderr);
        if (goal.second) {
            fputs(" => ", stderr);
            atom_print(goal.second, stderr);
        }
        fputc('\n', stderr);
    }
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

    Atom *first = petta_machine_apply_bindings(machine,
        environment, &machine->heap, goal.first);
    Atom *second = goal.second
        ? petta_machine_apply_bindings(machine,
              environment, &machine->heap, goal.second)
        : NULL;

    if (goal.kind == PETTA_GOAL_UNIFY)
        return petta_machine_unify_resolved(
            machine, first, second);

    if (goal.kind == PETTA_GOAL_TYPE_ACCEPT)
        return petta_machine_type_accept(
            machine, first, second, goal.barrier);

    if (goal.kind == PETTA_GOAL_CLAUSE_ONLY)
        return petta_machine_start_clause_choice(
            machine, first, second, goal.barrier, true, false);

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

    if (goal.kind == PETTA_GOAL_FOREIGN_READY) {
        bool recognized = false;
        bool dispatched = petta_machine_try_foreign_call(
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
            petta_machine_foreign_callable(machine, first) ||
            (machine->host.classify &&
             machine->host.classify(
                 machine->host.context, machine->space, first) !=
                 PETTA_MACHINE_HOST_NONE);
        return callable
            ? petta_push_solve(
                  machine, first, second, goal.barrier)
            : petta_machine_unify(machine, first, second);
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
        if (petta_symbol_name_is(operation, "last")) {
            return items->expr.len > 0u &&
                   petta_machine_unify(
                       machine,
                       items->expr.elems[items->expr.len - 1u],
                       second);
        }
        if (petta_symbol_name_is(operation, "msort")) {
            Atom *sorted =
                petta_semantics_msort(&machine->heap, items);
            return sorted &&
                   petta_machine_unify(
                       machine, sorted, second);
        }
        if (!petta_symbol_name_is(operation, "reverse"))
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
            goal.barrier);
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
                petta_symbol_name_is(pattern->sym_id, "Empty")) {
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
        goal.kind == PETTA_GOAL_CALL_READY_DATA ||
        goal.kind ==
            PETTA_GOAL_CALL_READY_COUNTED_COLLECTION) {
        /*
         * Specialization is a binding-time operation on argument values, not
         * on their source expressions.  Every strict argument has reached
         * its translated value at this continuation; Expression-typed
         * arguments are intentionally already values.  Preparing earlier
         * can confuse a nested call with constructor data and derive a head
         * that no evaluated invocation can match.
         */
        if (machine->host.prepare_call) {
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
                       PETTA_SPECIALIZE_UNCHANGED_RELEVANCE_BOUNDED) {
                machine->stats
                    .specializer_prepare_relevance_bounded++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_REWRITTEN) {
                machine->stats.specializer_prepare_rewritten++;
            } else if (specialized ==
                       PETTA_SPECIALIZE_UNCHANGED) {
                machine->stats.specializer_prepare_unchanged++;
            }
            if (specialized == PETTA_SPECIALIZE_INVALIDATED) {
                *failure = PETTA_MACHINE_STEP_INVALIDATED;
                return false;
            }
            if (specialized == PETTA_SPECIALIZE_CAPACITY ||
                !prepared) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
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
            goal.kind == PETTA_GOAL_CALL_READY &&
            machine->host.execute_prepared_pure_call) {
            Atom *pure_result =
                machine->host.execute_prepared_pure_call(
                    machine->host.context, machine->space,
                    &machine->heap, first);
            if (pure_result) {
                return petta_machine_unify_resolved(
                    machine, pure_result, second);
            }
        }
        if (!count_collection_result && !translated &&
            !relation_declared_tabled &&
            goal.kind == PETTA_GOAL_CALL_READY &&
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
            count_collection_result);
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
        return petta_push_solve_planned(
            machine, truth ? goal.second : goal.third,
            goal.fourth, goal.barrier,
            truth ? goal.second_plan : goal.third_plan);
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
    if ((goal.kind == PETTA_GOAL_HOST_READY ||
         goal.kind == PETTA_GOAL_HOST_STRICT_READY) &&
        first && first->kind == ATOM_EXPR &&
        first->expr.len > 0u &&
        first->expr.len - 1u <= UINT32_MAX) {
        Atom *head = first->expr.elems[0];
        SymbolId head_id = head && head->kind == ATOM_SYMBOL
            ? head->sym_id : SYMBOL_ID_NONE;
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
                bool truth = false;
                if (petta_semantics_truth_value(direct, &truth)) {
                    direct = petta_semantics_boolean_value(
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
    Atom *resolved = petta_machine_apply_bindings(machine,
        current, &machine->heap, machine->answer_variable);
    if (!resolved)
        return false;
    if (machine->count_only_emission) {
        bindings_init(environment);
        *answer = resolved;
        machine->pending_answer_weight = 0u;
        machine->last_answer_weight = answer_weight;
        machine->stats.answers += answer_weight;
        return true;
    }
    resolved = petta_machine_materialize_answer(
        machine, resolved);
    Atom *promoted = petta_machine_copy_atom(
        machine, machine->answer_arena, resolved);
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
        value = petta_machine_materialize_answer(
            machine, value);
        if (!value)
            return false;
        Atom *promoted_variable = petta_machine_copy_atom(
            machine, machine->answer_arena, variable);
        Atom *promoted_value = petta_machine_copy_atom(
            machine, machine->answer_arena, value);
        if (!promoted_variable || !promoted_value ||
            !bindings_add_var(
                environment, promoted_variable, promoted_value)) {
            bindings_free(environment);
            bindings_init(environment);
            return false;
        }
    }
    *answer = promoted;
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
    PettaTableShared *table_shared,
    bool owns_table_shared, bool bypass_root_table,
    size_t table_generator) {
    if (machine)
        machine->impl = NULL;
    if (!machine || !space || !answer_arena || !query ||
        !table_shared)
        return false;

    PettaMachineImpl *impl = cetta_malloc(sizeof(*impl));
    memset(impl, 0, sizeof(*impl));
    impl->space = space;
    impl->answer_arena = answer_arena;
    impl->table_shared = table_shared;
    impl->owns_table_shared = owns_table_shared;
    impl->bypass_root_table = bypass_root_table;
    impl->table_generator = table_generator;
    if (host)
        impl->host = *host;
    petta_machine_heap_arena_init(&impl->heap);
    petta_machine_heap_arena_init(&impl->tenured);
    arena_init(&impl->plan_arena);
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
        arena_free(&impl->plan_arena);
        if (impl->owns_table_shared)
            petta_table_shared_free(impl->table_shared);
        free(impl);
        return false;
    }
    impl->query = petta_machine_copy_atom(
        impl, &impl->heap, query);
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
    if (!machine || !space || !answer_arena || !query)
        return false;
    PettaTableShared *shared = petta_table_shared_new();
    if (!shared)
        return false;
    return petta_machine_init_internal(
        machine, space, answer_arena, query, NULL,
        base_environment, host, shared, true, false,
        PETTA_TABLE_ENTRY_NONE);
}

bool petta_machine_init_with_plan(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *plan,
    const Bindings *base_environment,
    const PettaMachineHost *host) {
    if (machine)
        machine->impl = NULL;
    if (!machine || !space || !answer_arena || !query)
        return false;
    PettaTableShared *shared = petta_table_shared_new();
    if (!shared)
        return false;
    return petta_machine_init_internal(
        machine, space, answer_arena, query, plan,
        base_environment, host, shared, true, false,
        PETTA_TABLE_ENTRY_NONE);
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
        if (impl->host.permit_transition &&
            !impl->host.permit_transition(impl->host.context)) {
            return petta_machine_finish_next(
                impl, started_ns,
                PETTA_MACHINE_STEP_SUSPENDED);
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
        if (petta_machine_dispatch_goal(impl, goal, &failure))
            continue;
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
    for (size_t index = 0u;
         index < impl->match_decision_len; index++) {
        petta_match_decision_cache_entry_free(
            &impl->match_decisions[index]);
    }
    free(impl->match_decisions);
    search_context_free(&impl->search);
    arena_free(&impl->heap);
    arena_free(&impl->tenured);
    arena_free(&impl->plan_arena);
    if (impl->owns_table_shared)
        petta_table_shared_free(impl->table_shared);
    free(impl);
    machine->impl = NULL;
}

bool petta_machine_stats(
    const PettaMachine *machine, PettaMachineStats *stats) {
    if (!machine || !machine->impl || !stats)
        return false;
    *stats = machine->impl->stats;
    return true;
}
