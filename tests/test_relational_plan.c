#include "relational_plan.h"
#include "relational_cursor.h"
#include "relational_plan_internal.h"
#include "dense_relation.h"
#include "parser.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void same_work(CettaRelationalWork left, CettaRelationalWork right) {
    assert(left.key_seeks == right.key_seeks);
    assert(left.key_comparisons == right.key_comparisons);
    assert(left.candidate_prefixes == right.candidate_prefixes);
    assert(left.leaf_assignments == right.leaf_assignments);
    assert(left.peak_bytes == right.peak_bytes);
}

static uint64_t drain(CettaRelationalCountCursor *cursor, const Space *space,
    uint64_t quantum, CettaRelationalWork *work) {
    uint64_t steps = 0u, count = 99u;
    for (size_t slices = 0u; ; slices++) {
        assert(slices < 1000000u);
        uint64_t used = 99u;
        CettaRelationalCountStatus status = cetta_relational_count_cursor_advance(cursor, space, quantum, &used);
        assert(used <= quantum);
        steps += used;
        if (status == CETTA_RELATIONAL_COUNT_COMPLETE)
            break;
        assert(status == CETTA_RELATIONAL_COUNT_PAUSED && used != 0u);
        CettaRelationalWork unpublished = {.key_seeks = 99u};
        assert(!cetta_relational_count_cursor_result(cursor, space, &count, &unpublished));
        assert(count == 99u && unpublished.key_seeks == 99u);
    }
    assert(cetta_relational_count_cursor_result(cursor, space, &count, work));
    uint64_t used = 99u;
    assert(cetta_relational_count_cursor_advance(cursor, space, quantum, &used) == CETTA_RELATIONAL_COUNT_COMPLETE);
    assert(used == 0u);
    return steps;
}

#ifdef CETTA_RELATIONAL_FAULT_CONTROLS
static size_t allocation_budget = SIZE_MAX;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
static bool allow_allocation(void) {
    if (allocation_budget == SIZE_MAX)
        return true;
    if (!allocation_budget)
        return false;
    allocation_budget--;
    return true;
}
void *__wrap_malloc(size_t size) {
    return allow_allocation() ? __real_malloc(size) : NULL;
}
void *__wrap_calloc(size_t count, size_t size) {
    return allow_allocation() ? __real_calloc(count, size) : NULL;
}
void *__wrap_realloc(void *pointer, size_t size) {
    return allow_allocation() ? __real_realloc(pointer, size) : NULL;
}
#endif

static Atom *parse(Arena *arena, const char *text) {
    Atom **forms = NULL;
    assert(parse_metta_text(text, arena, &forms) == 1);
    Atom *value = forms[0];
    free(forms);
    return value;
}

static void add(Space *space, Arena *arena, const char *text) {
    space_add(space, parse(arena, text));
}

static uint64_t reference(Space *space, Arena *arena, Atom **patterns, size_t count, const Bindings *seed) {
    BindingSet answers;
    binding_set_init(&answers);
    space_query_conjunction(space, arena, patterns, (CettaExprLen)count, seed, &answers);
    uint64_t result = answers.len;
    binding_set_free(&answers);
    return result;
}

static uint64_t check(Space *space, Arena *arena, Atom **patterns, size_t count, const Bindings *seed) {
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(space, arena, patterns, count, seed, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    assert(cetta_relational_plan_valid_for(plan, space));
    const CettaRelationalPlanInfo *info = cetta_relational_plan_info(plan);
    assert(info->factor_count == count);
    assert(info->encoding == CETTA_RELATIONAL_FINITE_EXACT_TERM_KEYS);
    assert(info->observation.demand.completion == CETTA_OBSERVATION_COMPLETE_BAG);
    uint64_t actual = 99u;
    CettaRelationalWork work;
    assert(cetta_relational_plan_count(plan, space, &actual, &work));
    assert(actual == reference(space, arena, patterns, count, seed));
    assert(work.peak_bytes >= info->retained_bytes);
    assert(info->preparation_peak_bytes >= info->retained_bytes);
    uint64_t comparison_bound = 0u;
    for (uint64_t width = space->native.len; width; width >>= 1u)
        comparison_bound++;
    assert(work.key_comparisons <= work.key_seeks * comparison_bound);
    CettaRelationalCountCursor *cursor = NULL;
    assert(cetta_relational_count_cursor_open(plan, space, &cursor));
    assert(atomic_load(&plan->references) == 2u);
    uint64_t used = 99u;
    CettaRelationalCountStatus initial = cetta_relational_count_cursor_status(cursor);
    assert(cetta_relational_count_cursor_advance(cursor, space, 0u, &used) == initial && used == 0u);
    CettaRelationalWork sliced;
    (void)drain(cursor, space, 3u, &sliced);
    uint64_t sliced_count = 99u;
    assert(cetta_relational_count_cursor_result(cursor, space, &sliced_count, NULL));
    assert(sliced_count == actual);
    same_work(work, sliced);
    cetta_relational_count_cursor_free(cursor);
    cetta_relational_plan_free(plan);
    return actual;
}

static void plan_ownership(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    add(&space, arena, "(owned a)");
    Atom *pattern = parse(arena, "(owned $x)");
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, &pattern, 1u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    CettaRelationalCountCursor *cursor = NULL;
    assert(cetta_relational_count_cursor_open(plan, &space, &cursor));
    assert(atomic_load(&plan->references) == 2u);
    CettaRelationalCountCursor *copy = NULL;
    assert(cetta_relational_count_cursor_clone(cursor, &space, &copy));
    assert(atomic_load(&plan->references) == 3u);
    cetta_relational_plan_free(plan);
    cetta_relational_count_cursor_free(cursor);
    (void)drain(copy, &space, 1u, NULL);
    uint64_t count = 99u;
    assert(cetta_relational_count_cursor_result(copy, &space, &count, NULL) && count == 1u);
    space_free(&space);
    cetta_relational_count_cursor_free(copy);
    puts("PASS: shared plan references outlive independently released handles and spaces");
}

static void resumption(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    const char *heads[] = {"r", "s", "t"};
    for (size_t f = 0u; f < 3u; f++) {
        for (size_t x = 0u; x < 3u; x++) {
            for (size_t y = 0u; y < 3u; y++) {
                Atom *row[] = {atom_symbol(arena, heads[f]), atom_int(arena, x), atom_int(arena, y)};
                space_add(&space, atom_expr(arena, row, 3u));
            }
        }
    }
    Arena syntax;
    arena_init(&syntax);
    Atom *query = parse(&syntax, "(, (r $x $y) (s $y $z) (t $z $x))");
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, &syntax, query->expr.elems + 1u, 3u,
        NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    arena_free(&syntax);
    CettaRelationalWork expected;
    uint64_t count = 99u;
    assert(cetta_relational_plan_count(plan, &space, &count, &expected) && count == 27u);
    CettaRelationalCountCursor *source = NULL, *whole = NULL;
    assert(cetta_relational_count_cursor_open(plan, &space, &source));
    assert(cetta_relational_count_cursor_clone(source, &space, &whole));
    uint64_t total_steps = drain(whole, &space, UINT64_MAX, NULL), steps = 0u;
    cetta_relational_count_cursor_free(whole);
    size_t copies = 0u;
    while (cetta_relational_count_cursor_status(source) == CETTA_RELATIONAL_COUNT_PAUSED) {
        uint64_t used = 99u;
        (void)cetta_relational_count_cursor_advance(source, &space, 1u, &used);
        assert(used == 1u);
        steps++;
        CettaRelationalCountCursor *copy = NULL;
        assert(cetta_relational_count_cursor_clone(source, &space, &copy));
        CettaContinuationStorage original_storage, copy_storage;
        assert(cetta_relational_count_cursor_storage(source, &original_storage));
        assert(cetta_relational_count_cursor_storage(copy, &copy_storage));
        assert(original_storage.shared_identity == copy_storage.shared_identity);
        assert(original_storage.exclusive_bytes == copy_storage.exclusive_bytes);
        CettaRelationalWork actual;
        assert(steps + drain(copy, &space, 7u, &actual) == total_steps);
        same_work(actual, expected);
        count = 99u;
        assert(cetta_relational_count_cursor_result(copy, &space, &count, NULL) && count == 27u);
        cetta_relational_count_cursor_free(copy);
        copies++;
    }
    assert(steps == total_steps && copies > 100u);
    cetta_relational_count_cursor_free(source);

    /* Controller occurrences own independent progress while sharing one
     * physical prepared plan. Selection and cancellation do not copy it. */
    assert(cetta_relational_count_cursor_open(plan, &space, &source));
    CettaRelationalCountEndpoint endpoint = {&source, &space};
    CettaContinuationMachine machine = cetta_relational_count_continuation_machine(&endpoint);
    CettaOwnedContinuation images[3];
    CettaSelectionAutomaton fifo, ratio;
    assert(cetta_selection_automaton_fifo(&fifo));
    assert(cetta_selection_automaton_ratio(2u, &ratio));
    CettaControlPlan control;
    assert(cetta_control_plan_derive((CettaObservationDemand){CETTA_OBSERVATION_COMPLETE_BAG, 0u},
        CETTA_CONTROL_BRANCH_GENERAL, CETTA_CONTROL_BATCH_SINGLETON_ONLY, &control));
    CettaContinuationHub hub = {0};
    assert(cetta_continuation_hub_init(&hub, &fifo, CETTA_SELECTION_DUTY_RECURRENT_OLDEST, &control));
    CettaContinuationStorage storage;
    assert(cetta_relational_count_cursor_storage(source, &storage));
    for (size_t i = 0u; i < 3u; i++) {
        uint64_t used;
        assert(cetta_relational_count_cursor_advance(source, &space, 8u, &used) == CETTA_RELATIONAL_COUNT_PAUSED);
        cetta_owned_continuation_init(&images[i]);
        assert(cetta_continuation_capture(machine, &images[i]) == CETTA_CONTINUATION_READY);
        assert(cetta_continuation_hub_append(&hub, &images[i]));
        assert(!images[i].payload);
    }
    size_t shared = 0u, exclusive = 0u;
    assert(cetta_continuation_hub_storage(&hub, &shared, &exclusive));
    assert(shared == storage.shared_bytes && exclusive == 3u * storage.exclusive_bytes);
    uint64_t first_id = cetta_continuation_hub_at(&hub, 0u)->occurrence_id;
    uint64_t second_id = cetta_continuation_hub_at(&hub, 1u)->occurrence_id;
    assert(first_id && second_id && first_id != second_id);
    assert(cetta_continuation_hub_switch_schedule(&hub, &ratio));
    assert(cetta_continuation_hub_at(&hub, 0u)->occurrence_id == first_id);
    cetta_owned_continuation_init(&images[2]);
    assert(cetta_continuation_hub_take(&hub, 2u, &images[2]));
    cetta_owned_continuation_destroy(&images[2]);
    cetta_relational_plan_free(plan);
    cetta_relational_count_cursor_free(source);
    source = NULL;
    for (size_t i = 0u; i < 2u; i++) {
        CettaSelectionLane lane;
        size_t index;
        assert(cetta_continuation_hub_select(&hub, &lane, &index));
        cetta_owned_continuation_init(&images[i]);
        assert(cetta_continuation_hub_take(&hub, index, &images[i]));
        Space wrong;
        space_init_with_universe(&wrong, universe);
        CettaRelationalCountEndpoint other_endpoint = {&source, &wrong};
        void *payload = images[i].payload;
        CettaRelationalCountCursor *target_before = source;
        assert(cetta_continuation_restore(cetta_relational_count_continuation_machine(&other_endpoint),
            &images[i]) == CETTA_CONTINUATION_INVALIDATED);
        assert(images[i].payload == payload && source == target_before);
        space_free(&wrong);
        assert(cetta_continuation_restore(machine, &images[i]) == CETTA_CONTINUATION_READY);
        assert(!images[i].payload);
        CettaRelationalWork actual;
        (void)drain(source, &space, 11u, &actual);
        same_work(actual, expected);
        count = 99u;
        assert(cetta_relational_count_cursor_result(source, &space, &count, NULL) && count == 27u);
        /* Retain this target through the next successful restore. */
    }
    cetta_continuation_hub_destroy(&hub);
    cetta_relational_count_cursor_free(source);
    source = NULL;
    query = parse(arena, "(, (r $x $y) (s $y $z) (t $z $x))");
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 3u,
        NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    assert(cetta_relational_count_cursor_open(plan, &space, &source));
    cetta_relational_plan_free(plan);
    assert(cetta_relational_count_cursor_advance(source, &space, 9u, NULL) == CETTA_RELATIONAL_COUNT_PAUSED);
    CettaOwnedContinuation stale;
    cetta_owned_continuation_init(&stale);
    assert(cetta_continuation_capture(machine, &stale) == CETTA_CONTINUATION_READY);
    add(&space, arena, "(unrelated changed)");
    void *payload = stale.payload;
    CettaRelationalCountCursor *old_target = source;
    assert(cetta_continuation_restore(machine, &stale) == CETTA_CONTINUATION_INVALIDATED);
    assert(stale.payload == payload && source == old_target);
    uint64_t used = 99u;
    assert(cetta_relational_count_cursor_advance(source, &space, 100u, &used) == CETTA_RELATIONAL_COUNT_INVALIDATED);
    assert(used == 0u);
    count = 99u;
    assert(!cetta_relational_count_cursor_result(source, &space, &count, NULL) && count == 99u);
    space_free(&space);
    cetta_owned_continuation_destroy(&stale);
    cetta_relational_count_cursor_free(source);
    printf("PASS: %zu suspension points retain exact work, independent consumers, shared plan storage and transactional hub restore\n", copies);
}

static void controls(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    add(&space, arena, "(r a b)");
    add(&space, arena, "(r a b)");
    add(&space, arena, "(r a c)");
    add(&space, arena, "(s b c)");
    add(&space, arena, "(s c c)");
    add(&space, arena, "(t c a)");
    add(&space, arena, "(t c a)");
    /* An unrelated open equation is not part of these rigid-head rows. */
    add(&space, arena, "(= (id $x) $x)");
    Atom *family = parse(arena,"(, (r $x $y) (s $y $z) (t $z $x))");
    Atom **triangle = family->expr.elems + 1u;
    assert(check(&space, arena, triangle, 3u, NULL) == 6u);
    CettaRelationalPlan *cyclic = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, triangle, 3u, NULL,
        CETTA_RELATIONAL_ADMIT_CYCLIC_QUERY, &cyclic));
    assert(cetta_relational_plan_info(cyclic)->query_class == CETTA_RELATIONAL_CYCLIC_QUERY);
    assert(cetta_relational_plan_info(cyclic)->covering_factor == SIZE_MAX);
    cetta_relational_plan_free(cyclic);
    cyclic = NULL;
    Atom *star_family = parse(arena,"(, (r $x $y) (r $x $z) (r $x $w))");
    Atom **star = star_family->expr.elems + 1u;
    assert(check(&space, arena, star, 3u, NULL) == 27u);
    assert(!cetta_relational_plan_prepare(&space, arena, star, 3u, NULL,
        CETTA_RELATIONAL_ADMIT_CYCLIC_QUERY, &cyclic) && cyclic == NULL);
    assert(cetta_relational_plan_prepare(&space, arena, star, 3u, NULL,
        CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &cyclic));
    assert(cetta_relational_plan_info(cyclic)->variable_count == 1u);
    assert(cetta_relational_plan_info(cyclic)->covering_factor == 0u);
    assert(cetta_relational_plan_info(cyclic)->source_variable_count == 4u);
    uint64_t star_count = 99u;
    CettaRelationalWork star_work;
    assert(cetta_relational_plan_count(cyclic, &space, &star_count, &star_work));
    assert(star_count == 27u && star_work.leaf_assignments == 1u);
    cetta_relational_plan_free(cyclic);
    Atom *repeated[] = {parse(arena,"(s $x $x)")};
    assert(check(&space, arena, repeated, 1u, NULL) == 1u);
    Atom *rigid[] = {parse(arena,"(r a b)"),parse(arena,"(t c a)")};
    assert(check(&space, arena, rigid, 2u, NULL) == 4u);
    Atom *empty_head[] = {atom_symbol(arena,"r"), atom_symbol(arena,"absent"), triangle[0]->expr.elems[2]};
    Atom *empty[] = {atom_expr(arena, empty_head, 3u),triangle[1],triangle[2]};
    assert(check(&space, arena, empty, 3u, NULL) == 0u);
    Bindings seed;
    bindings_init(&seed);
    assert(bindings_add_var(&seed, triangle[0]->expr.elems[2], parse(arena,"b")));
    assert(check(&space, arena, triangle, 3u, &seed) == 4u);
    bindings_free(&seed);

    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, triangle, 3u, NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    add(&space, arena, "(r c b)");
    assert(!cetta_relational_plan_valid_for(plan, &space));
    uint64_t sentinel = 99u;
    assert(!cetta_relational_plan_count(plan, &space, &sentinel, NULL) && sentinel == 99u);
    cetta_relational_plan_free(plan);
    assert(check(&space, arena, triangle, 3u, NULL) == 6u);

    /* Losing one closing occurrence changes multiplicity, not support. */
    assert(space_remove(&space, parse(arena,"(t c a)")));
    assert(check(&space, arena, triangle, 3u, NULL) == 3u);

    assert(cetta_relational_plan_prepare(&space, arena, triangle, 3u, NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    space_free(&space);
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    assert(!cetta_relational_plan_valid_for(plan, &space));
    cetta_relational_plan_free(plan);
    add(&space, arena, "(r (key 1) \"inside\")");
    add(&space, arena, "(r (key 1) \"inside\")");
    Atom *nested[] = {parse(arena,"(r (key 1) $v)"),parse(arena,"(r $k \"inside\")")};
    assert(check(&space, arena, nested, 2u, NULL) == 4u);
    add(&space, arena, "(r 1.0 \"inside\")");
    plan = NULL;
    assert(!cetta_relational_plan_prepare(&space, arena, nested, 2u, NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan) && plan == NULL);
    space_free(&space);

    /* Unlike an unrelated open equation, a whole-atom variable may match
     * any factor. Removing it restores the exact finite-ground fragment. */
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    add(&space, arena, "(r a b)");
    add(&space, arena, "(s b c)");
    add(&space, arena, "(t c a)");
    Atom *whole = parse(arena, "$whole");
    space_add(&space, whole);
    assert(reference(&space, arena, triangle, 3u, NULL) > 1u);
    assert(!cetta_relational_plan_prepare(&space, arena, triangle, 3u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan) && plan == NULL);
    assert(space_remove(&space, whole));
    assert(check(&space, arena, triangle, 3u, NULL) == 1u);
    space_free(&space);
}

static bool add_weights(void *context, const AtomId *values, uint64_t weight) {
    (void)values;
    *(uint64_t *)context += weight;
    return true;
}

/* A plan over rows holding stored variables counts as nested loops do; its
 * enumeration answers the same weights, or declines when a solution would
 * keep a stored variable unbound. */
static void check_open(Space *space, Arena *arena, Atom **patterns,
                       size_t count, bool enumerable) {
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(space, arena, patterns, count, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    uint64_t actual = 99u;
    assert(cetta_relational_plan_count(plan, space, &actual, NULL));
    assert(actual == reference(space, arena, patterns, count, NULL));
    CettaRelationalCountCursor *cursor = NULL;
    assert(!cetta_relational_count_cursor_open(plan, space, &cursor) && !cursor);
    cetta_relational_plan_free(plan);
    plan = NULL;
    bool prepared = cetta_relational_plan_prepare(space, arena, patterns, count,
        NULL, CETTA_RELATIONAL_ADMIT_ENUMERATION, &plan);
    assert(prepared == enumerable);
    if (!prepared)
        return;
    uint64_t weights = 0u, enumerated = 0u;
    assert(cetta_relational_plan_enumerate(plan, space, add_weights, &weights,
                                           &enumerated, NULL));
    assert(weights == actual && enumerated == actual);
    cetta_relational_plan_free(plan);
}

/* A stored variable standing for a whole column acts as a wildcard: its
 * row is joined on its own, with the factors before it reading only rows
 * without stored variables, so each row combination is met once. */
static void stored_variables(TermUniverse *universe, Arena *arena) {
    Atom *family = parse(arena, "(, (r $x $y) (s $y $z) (t $z $x))");
    Atom **triangle = family->expr.elems + 1u;
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    const char *facts[] = {
        "(r a b)", "(r b c)", "(r $u b)",
        "(s b c)", "(s c a)", "(s $v $v)",
        "(t c a)", "(t a b)", "(t c c)",
    };
    for (size_t i = 0u; i < sizeof(facts) / sizeof(*facts); i++)
        add(&space, arena, facts[i]);
    check_open(&space, arena, triangle, 3u, true);
    /* A row leaving x to the stored variables of two factors at once has
     * solutions that keep it unbound: the enumeration declines. */
    add(&space, arena, "(t c $w)");
    check_open(&space, arena, triangle, 3u, false);
    /* One pattern: the open row is a solution of its own. */
    Atom *single[] = {parse(arena, "(r $x b)")};
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, single, 1u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    uint64_t count = 99u;
    assert(cetta_relational_plan_count(plan, &space, &count, NULL));
    assert(count == reference(&space, arena, single, 1u, NULL) && count == 2u);
    cetta_relational_plan_free(plan);
    space_free(&space);
}

/* A conjunction over dense binary relations is counted by rows of bits:
 * the count equals nested loops', with and without a seed.  A repeated row
 * leaves it to the plan, which keeps the row's multiplicity. */
static void dense_counts(TermUniverse *universe, Arena *arena) {
    Atom *family = parse(arena, "(, (edge $x $y) (edge $y $z) (edge $z $x))");
    Atom **triangle = family->expr.elems + 1u;
    Space space;
    space_init_with_universe(&space, universe);
    char row[32];
    for (int u = 0; u < 8; u++) {
        for (int v = 0; v < 8; v++) {
            if ((u * 7 + v * 3) % 5 < 2) {
                snprintf(row, sizeof(row), "(edge %d %d)", u, v);
                add(&space, arena, row);
            }
        }
    }
    CettaDenseQuery *query = NULL;
    assert(cetta_dense_query_prepare(&space, arena, triangle, 3u, &query));
    cetta_dense_query_free(query);
    uint64_t dense = 99u;
    assert(space_native_relational_count(&space, arena, triangle, 3u, NULL, &dense));
    assert(dense == reference(&space, arena, triangle, 3u, NULL) && dense > 0u);
    /* With x bound, y, z and w still close a cycle: the seeded query is
     * not a chain, and rows of bits count it. */
    Atom *tailed = parse(arena,
        "(, (edge $x $y) (edge $y $z) (edge $z $w) (edge $w $y))");
    Atom **lollipop = tailed->expr.elems + 1u;
    uint64_t dense_all = 99u;
    assert(space_native_relational_count(&space, arena, lollipop, 4u, NULL, &dense_all));
    assert(dense_all == reference(&space, arena, lollipop, 4u, NULL));
    Bindings seed;
    bindings_init(&seed);
    assert(bindings_add_var(&seed, lollipop[0]->expr.elems[1], parse(arena, "1")));
    uint64_t dense_seeded = 99u;
    assert(space_native_relational_count(&space, arena, lollipop, 4u, &seed, &dense_seeded));
    assert(dense_seeded == reference(&space, arena, lollipop, 4u, &seed) && dense_seeded > 0u &&
           dense_seeded < dense_all);
    bindings_free(&seed);
    add(&space, arena, "(edge 0 0)");
    query = NULL;
    assert(!cetta_dense_query_prepare(&space, arena, triangle, 3u, &query) && !query);
    uint64_t repeated = 99u;
    assert(space_native_relational_count(&space, arena, triangle, 3u, NULL, &repeated));
    assert(repeated == reference(&space, arena, triangle, 3u, NULL) && repeated > dense);
    space_free(&space);
}

static void private_projection(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    const char *facts[] = {
        "(r a b 10)", "(r a b 11)", "(r a c 12)",
        "(s b c 20)", "(s c c 21)", "(t c a 30)", "(t c a 31)",
        "(u a b c)",
    };
    for (size_t i = 0u; i < sizeof(facts) / sizeof(*facts); i++)
        add(&space, arena, facts[i]);
    Atom *query = parse(arena, "(, (r $x $y $or) (s $y $z $os) (t $z $x $ot))");
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 3u,
        NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    const CettaRelationalPlanInfo *info = cetta_relational_plan_info(plan);
    assert(info->source_variable_count == 6u && info->variable_count == 3u);
    CettaRelationalWork work;
    uint64_t count = 99u;
    assert(cetta_relational_plan_count(plan, &space, &count, &work));
    assert(count == 6u && work.leaf_assignments == 2u);
    assert(count == reference(&space, arena, query->expr.elems + 1u, 3u, NULL));
    /* A changed universe generation invalidates borrowed coordinates even
     * when the Space revision and address have not changed. */
    universe->storage_epoch++;
    assert(!cetta_relational_plan_valid_for(plan, &space));
    count = 99u;
    assert(!cetta_relational_plan_count(plan, &space, &count, NULL) && count == 99u);
    universe->storage_epoch--;
    cetta_relational_plan_free(plan);
    query = parse(arena, "(, (r $x $y $or) (s $y $z $os) (t $z $x $ot) (u $x $y $z))");
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 4u,
        NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    assert(cetta_relational_plan_info(plan)->query_class == CETTA_RELATIONAL_ACYCLIC_QUERY);
    assert(cetta_relational_plan_info(plan)->covering_factor == 3u);
    assert(cetta_relational_plan_count(plan, &space, &count, NULL) && count == 4u);
    cetta_relational_plan_free(plan);
    plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 4u,
        NULL, CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan));
    assert(cetta_relational_plan_count(plan, &space, &count, NULL) && count == 4u);
    cetta_relational_plan_free(plan);
    query = parse(arena, "(, (r $a $b $or) (s $b $c $os) (t $c $d $ot) (u $d $e $_))");
    plan = NULL;
    assert(!cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 4u,
        NULL, CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan) && plan == NULL);
    assert(check(&space, arena, query->expr.elems + 1u, 4u, NULL) == 6u);
    space_free(&space);

    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    add(&space, arena, "(r a 0 0)");
    add(&space, arena, "(r b 1 1)");
    add(&space, arena, "(r c 1 2)");
    for (size_t i = 0u; i < 4u; i++) {
        Atom *items[] = {atom_symbol(arena, "s"), atom_int(arena, i), atom_int(arena, i)};
        space_add(&space, atom_expr(arena, items, 3u));
    }
    query = parse(arena, "(, (r $x $a $a) (s $y $b))");
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 2u,
        NULL, CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan));
    info = cetta_relational_plan_info(plan);
    assert(info->source_variable_count == 4u && info->variable_count == 0u);
    assert(cetta_relational_plan_count(plan, &space, &count, &work));
    assert(count == 8u && work.leaf_assignments == 1u);
    assert(count == reference(&space, arena, query->expr.elems + 1u, 2u, NULL));
    cetta_relational_plan_free(plan);
    space_free(&space);
}

#ifdef CETTA_RELATIONAL_FAULT_CONTROLS
static void allocation_faults(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (size_t i = 0u; i < 32u; i++) {
        const char *heads[] = {"r", "s", "t"};
        for (size_t f = 0u; f < 3u; f++) {
            Atom *items[] = {atom_symbol(arena, heads[f]), atom_int(arena, i), atom_int(arena, i)};
            space_add(&space, atom_expr(arena, items, 3u));
        }
    }
    Atom *query = parse(arena, "(, (r $x $y) (s $y $z) (t $z $x))");
    CettaRelationalPlan *existing = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u, 3u,
        NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &existing));
    size_t faults = 0u;
    bool completed = false;
    for (size_t budget = 0u; budget < 256u; budget++) {
        CettaRelationalPlan *out = existing;
        allocation_budget = budget;
        bool prepared = cetta_relational_plan_prepare(&space, arena, query->expr.elems + 1u,
            3u, NULL, CETTA_RELATIONAL_ADMIT_FULL_QUERY, &out);
        allocation_budget = SIZE_MAX;
        if (prepared) {
            assert(out != existing);
            cetta_relational_plan_free(out);
            completed = true;
            break;
        }
        assert(out == existing && cetta_relational_plan_valid_for(existing, &space));
        faults++;
    }
    assert(completed && faults >= 12u);
    completed = false;
    for (size_t budget = 0u; budget < 16u; budget++) {
        uint64_t count = 99u;
        CettaRelationalWork work = {.key_seeks = 99u};
        allocation_budget = budget;
        bool counted = cetta_relational_plan_count(existing, &space, &count, &work);
        allocation_budget = SIZE_MAX;
        if (counted) {
            assert(count == 32u);
            completed = true;
            break;
        }
        assert(count == 99u && work.key_seeks == 99u);
        faults++;
    }
    assert(completed);
    CettaRelationalCountCursor *source = NULL;
    assert(cetta_relational_count_cursor_open(existing, &space, &source));
    assert(cetta_relational_count_cursor_advance(source, &space, 17u, NULL) == CETTA_RELATIONAL_COUNT_PAUSED);
    CettaRelationalCountEndpoint endpoint = {&source, &space};
    for (size_t budget = 0u; budget < 1u; budget++) {
        CettaRelationalCountCursor *out = source;
        allocation_budget = budget;
        bool copied = cetta_relational_count_cursor_clone(source, &space, &out);
        allocation_budget = SIZE_MAX;
        assert(!copied && out == source);
        CettaOwnedContinuation image;
        cetta_owned_continuation_init(&image);
        allocation_budget = budget;
        CettaContinuationStatus status = cetta_continuation_capture(
            cetta_relational_count_continuation_machine(&endpoint), &image);
        allocation_budget = SIZE_MAX;
        assert(status == CETTA_CONTINUATION_CAPACITY && !image.payload && !image.provider);
        faults += 2u;
    }
    (void)drain(source, &space, 13u, NULL);
    uint64_t count = 99u;
    assert(cetta_relational_count_cursor_result(source, &space, &count, NULL) && count == 32u);
    cetta_relational_count_cursor_free(source);
    cetta_relational_plan_free(existing);
    space_free(&space);
    printf("PASS: %zu indexed-plan allocation failures preserve existing plans and unpublished outputs\n", faults);
}
#endif

static void overflow(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    add(&space, arena, "(duplicate)");
    add(&space, arena, "(duplicate)");
    Atom *pattern = parse(arena,"(duplicate)");
    Atom *patterns[65];
    for (size_t i = 0u; i < 65u; i++)
        patterns[i] = pattern;
    CettaRelationalPlan *plan = NULL;
    assert(cetta_relational_plan_prepare(&space, arena, patterns, 63u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    uint64_t count = 99u;
    assert(cetta_relational_plan_count(plan, &space, &count, NULL));
    assert(count == (UINT64_C(1) << 63u));
    cetta_relational_plan_free(plan);
    assert(cetta_relational_plan_prepare(&space, arena, patterns, 65u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    count = 99u;
    assert(!cetta_relational_plan_count(plan, &space, &count, NULL));
    assert(count == 99u);
    CettaRelationalCountCursor *cursor = NULL;
    assert(cetta_relational_count_cursor_open(plan, &space, &cursor));
    assert(cetta_relational_count_cursor_advance(cursor, &space, 1u, NULL) == CETTA_RELATIONAL_COUNT_OVERFLOW);
    assert(!cetta_relational_count_cursor_result(cursor, &space, &count, NULL) && count == 99u);
    assert(cetta_relational_count_cursor_advance(cursor, &space, 100u, NULL) == CETTA_RELATIONAL_COUNT_OVERFLOW);
    cetta_relational_count_cursor_free(cursor);
    cetta_relational_plan_free(plan);
    add(&space, arena, "(d 0)");
    add(&space, arena, "(d 0)");
    add(&space, arena, "(d 1)");
    add(&space, arena, "(d 1)");
    pattern = parse(arena,"(d $x)");
    for (size_t i = 0u; i < 63u; i++)
        patterns[i] = pattern;
    assert(cetta_relational_plan_prepare(&space, arena, patterns, 63u, NULL,
        CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
    count = 99u;
    assert(!cetta_relational_plan_count(plan, &space, &count, NULL) && count == 99u);
    assert(cetta_relational_count_cursor_open(plan, &space, &cursor));
    CettaRelationalWork unpublished = {.key_seeks = 99u};
    size_t steps = 0u;
    CettaRelationalCountStatus status;
    do {
        uint64_t used = 99u;
        status = cetta_relational_count_cursor_advance(cursor, &space, 1u, &used);
        assert(used == 1u && ++steps < 1000u);
        assert(!cetta_relational_count_cursor_result(cursor, &space, &count, &unpublished));
        assert(count == 99u && unpublished.key_seeks == 99u);
    } while (status == CETTA_RELATIONAL_COUNT_PAUSED);
    assert(status == CETTA_RELATIONAL_COUNT_OVERFLOW);
    uint64_t used = 99u;
    assert(cetta_relational_count_cursor_advance(cursor, &space, 100u, &used) ==
        CETTA_RELATIONAL_COUNT_OVERFLOW && used == 0u);
    cetta_relational_count_cursor_free(cursor);
    cetta_relational_plan_free(plan);
    space_free(&space);
    puts("PASS: product and accumulated-count overflow refuse publication across every slice");
}

static uint32_t random_word(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state >> 16u;
}

static void sparse_trail_retention(TermUniverse *universe, Arena *arena) {
    size_t earlier_bytes = 0u;
    for (size_t length = 16u; length <= 128u; length *= 2u) {
        Space space;
        space_init_with_universe(&space, universe);
        space.kind = SPACE_KIND_STACK;
        Atom *patterns[128];
        Atom *names[129];
        char name[64];
        for (size_t v = 0u; v <= length; v++) {
            assert(snprintf(name, sizeof(name), "trail_%zu", v) > 0);
            names[v] = atom_var(arena, name);
        }
        for (size_t f = 0u; f < length; f++) {
            assert(snprintf(name, sizeof(name), "trail-edge-%zu", f) > 0);
            Atom *head = atom_symbol(arena, name);
            Atom *query[] = {head, names[f], names[f + 1u]};
            patterns[f] = atom_expr(arena, query, 3u);
            for (size_t value = 0u; value < 2u; value++) {
                Atom *row[] = {head, atom_int(arena, value), atom_int(arena, value)};
                space_add(&space, atom_expr(arena, row, 3u));
            }
        }
        CettaRelationalPlan *plan = NULL;
        assert(cetta_relational_plan_prepare(&space, arena, patterns, length, NULL,
            CETTA_RELATIONAL_ADMIT_FULL_QUERY, &plan));
        const CettaRelationalPlanInfo *info = cetta_relational_plan_info(plan);
        assert(info->source_variable_count == length + 1u);
        assert(info->variable_count == length - 1u);
        assert(plan->incidence_count == 2u * (length - 1u));
        CettaRelationalCountCursor *cursor = NULL;
        assert(cetta_relational_count_cursor_open(plan, &space, &cursor));
        CettaContinuationStorage storage;
        assert(cetta_relational_count_cursor_storage(cursor, &storage));
        /* Doubling a bounded-degree query cannot quadruple its bookmark. */
        assert(!earlier_bytes || storage.exclusive_bytes <= 2u * earlier_bytes);
        earlier_bytes = storage.exclusive_bytes;
        CettaRelationalWork sliced, direct;
        (void)drain(cursor, &space, 7u, &sliced);
        uint64_t count = 99u;
        assert(cetta_relational_count_cursor_result(cursor, &space, &count, NULL) && count == 2u);
        assert(cetta_relational_plan_count(plan, &space, &count, &direct) && count == 2u);
        same_work(sliced, direct);
        assert(count == reference(&space, arena, patterns, length, NULL));
        cetta_relational_count_cursor_free(cursor);
        cetta_relational_plan_free(plan);
        space_free(&space);
    }
    puts("PASS: sparse query bookmarks grow linearly through 128 premises and restore both diagonal paths");
}

static void randomized(TermUniverse *universe, Arena *arena) {
    uint32_t state = 7919u;
    for (size_t trial = 0u; trial < 32u; trial++) {
        Space space;
        space_init_with_universe(&space, universe);
        space.kind = SPACE_KIND_STACK;
        const char *heads[] = {"r", "s", "t", "u"};
        for (size_t f = 0u; f < 4u; f++) {
            for (size_t row = 0u; row < 7u; row++) {
                Atom *items[] = {atom_symbol(arena, heads[f]),
                    atom_int(arena, random_word(&state) % 4u),
                    atom_int(arena, random_word(&state) % 4u)};
                space_add(&space, atom_expr(arena, items, 3u));
            }
        }
        const char *queries[][4] = {
            {"(r $x $y)","(s $y $z)","(t $z $x)","(u $x $z)"},
            {"(r $x $y)","(s $x $z)","(t $x $w)","(u $w $y)"},
            {"(r $x $y)","(s $y $z)","(t $z $w)","(u $w $v)"},
            {"(r $x $x)","(s $y $y)","(t $z $z)","(u $w $w)"},
        };
        for (size_t q = 0u; q < 4u; q++) {
            char source[256];
            assert(snprintf(source, sizeof(source), "(, %s %s %s %s)",
                queries[q][0], queries[q][1], queries[q][2], queries[q][3]) > 0);
            Atom *query = parse(arena, source);
            (void)check(&space, arena, query->expr.elems + 1u, 4u, NULL);
        }
        space_free(&space);
    }
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena persistent, arena;
    arena_init(&persistent);
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    plan_ownership(&universe, &arena);
    controls(&universe, &arena);
    private_projection(&universe, &arena);
    resumption(&universe, &arena);
    randomized(&universe, &arena);
    sparse_trail_retention(&universe, &arena);
    overflow(&universe, &arena);
    stored_variables(&universe, &arena);
    dense_counts(&universe, &arena);
#ifdef CETTA_RELATIONAL_FAULT_CONTROLS
    allocation_faults(&universe, &arena);
#endif
    term_universe_free(&universe);
    arena_free(&arena);
    arena_free(&persistent);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: prepared relational intersections preserve ground occurrence counts, aliases, seeds and revision boundaries, join rows holding stored variables as wildcards, and count dense binary relations by rows of bits");
    return 0;
}
