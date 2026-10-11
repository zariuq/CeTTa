#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "petta_search_machine.h"
#include "parser.h"
#include "symbol.h"
#include "producer_reads.h"
#include "producer_dependencies.h"
#include "producer_schedule.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Match the optional frame-controller and frame-array allocation requests.
 * Faults are armed only during a root's execution, after setup succeeds. */
typedef struct {
    CettaProducerReads reads;
    bool checked;
    bool admitted;
} RoundAllocation;
typedef struct {
    CettaProducerDependencies dependencies;
    CettaProducerSchedule pending;
    size_t begin, end;
    void *next;
    bool active;
} ScheduleAllocation;
static unsigned fault_kind, fault_at, fault_calls;
static bool fault_armed, fault_observed;
static bool forbid_allocations;
static size_t teardown_allocation_attempts;
static void *pull_allocation;
static bool fake_clock;
static uint64_t fake_clock_tick;
int __real_clock_gettime(clockid_t clock, struct timespec *time);
int __wrap_clock_gettime(clockid_t clock, struct timespec *time) {
    if (fake_clock && clock == CLOCK_MONOTONIC) {
        time->tv_sec = 0;
        time->tv_nsec = (long)fake_clock_tick++;
        return 0;
    }
    return __real_clock_gettime(clock, time);
}
/* Select only driver storage for the allocator refusal.  The assertions below
 * check public results, cancellation and a fresh query, not these fields. */
typedef struct {
    PettaMachine *machine, *requested_child, *completed_child;
    Atom *answer;
    Bindings environment;
    PettaMachineStep step;
    uint64_t started_ns;
} PullAllocation;
void *__real_malloc(size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_malloc(size_t size) {
    if (forbid_allocations) {
        teardown_allocation_attempts++;
        return NULL;
    }
    if (fault_armed && fault_kind == 6u && size == 16u * sizeof(PullAllocation) &&
        ++fault_calls == fault_at) {
        fault_observed = true;
        return NULL;
    }
    void *result = __real_malloc(size);
    if (fault_armed && fault_kind == 7u && size == 16u * sizeof(PullAllocation))
        pull_allocation = result;
    return result;
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (forbid_allocations) {
        teardown_allocation_attempts++;
        return NULL;
    }
    if (fault_armed && fault_kind == 7u && pull_allocation &&
        pointer == pull_allocation && size == 32u * sizeof(PullAllocation) &&
        ++fault_calls == fault_at) {
        fault_observed = true;
        return NULL;
    }
    return __real_realloc(pointer, size);
}
void *__real_calloc(size_t count, size_t width);
void *__wrap_calloc(size_t count, size_t width) {
    if (forbid_allocations) {
        teardown_allocation_attempts++;
        return NULL;
    }
    bool controller = count == 1u && width == 2u * sizeof(void *) + 2u * sizeof(size_t);
    bool frames = count >= 16u && width == sizeof(RoundAllocation);
    bool schedule = count == 1u && width == sizeof(ScheduleAllocation);
    bool queue_bitmap = count == 3u && width == 2u * sizeof(uint64_t);
    bool edge_pool = count == 16u && width == 6u * sizeof(uint32_t);
    if (fault_armed && ((fault_kind == 1u && controller) || (fault_kind == 2u && frames) ||
            (fault_kind == 3u && schedule) || (fault_kind == 4u && queue_bitmap) ||
            (fault_kind == 5u && edge_pool)) &&
        ++fault_calls == fault_at) {
        fault_observed = true;
        return NULL;
    }
    return __real_calloc(count, width);
}

typedef struct {
    SymbolId head;
    bool licensed;
    size_t remaining;
    size_t admissions;
    size_t change_on_admission;
    Space *change_space;
    Arena *change_owner;
    SymbolId denied_head;
    size_t denied_admissions;
} Probe;

static Atom *parse(Arena *arena, const char *source);

static bool contains(void *context, SymbolId head, CettaExprLen arity) {
    Probe *probe = context;
    return (head == probe->head || (probe->denied_head && head == probe->denied_head)) &&
        arity == 1u;
}

static bool reusable(void *context, Space *space, SymbolId head, CettaExprLen arity) {
    (void)space;
    Probe *probe = context;
    if (probe->licensed && probe->denied_head && head == probe->denied_head) {
        probe->denied_admissions++;
        return false;
    }
    bool admitted = probe->licensed && contains(context, head, arity);
    if (admitted && probe->change_on_admission &&
        ++probe->admissions == probe->change_on_admission)
        space_add(probe->change_space, parse(probe->change_owner,
            "(= (node 0) (seed changed))"));
    return admitted;
}

static bool permit(void *context) {
    Probe *probe = context;
    if (!probe->remaining)
        return false;
    probe->remaining--;
    return true;
}

static Atom *parse(Arena *arena, const char *source) {
    Atom **forms = NULL;
    assert(parse_metta_text(source, arena, &forms) == 1);
    Atom *result = forms[0];
    free(forms);
    return result;
}

static PettaMachineStats query_once(Space *space, Arena *owner, const char *query,
                                   PettaMachineHost *host, bool sliced, bool initial,
                                   size_t expected, Atom **answers) {
    Probe *probe = host->context;
    PettaMachine machine;
    assert(petta_machine_init(&machine, space, owner, parse(owner, query), NULL, host));
    fault_armed = initial && fault_kind;
    size_t length = 0u, pauses = 0u;
    for (;;) {
        probe->remaining = sliced ? 3u : SIZE_MAX;
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep status = petta_machine_next(&machine, &answer, &environment);
        if (status == PETTA_MACHINE_STEP_ANSWER) {
            assert(length < expected);
            answers[length++] = atom_deep_copy(owner, answer);
        }
        bindings_free(&environment);
        if (status == PETTA_MACHINE_STEP_EXHAUSTED)
            break;
        if (status == PETTA_MACHINE_STEP_SUSPENDED)
            pauses++;
        else
            assert(status == PETTA_MACHINE_STEP_ANSWER);
    }
    assert(length == expected);
    fault_armed = false;
    if (initial && sliced)
        assert(pauses);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    petta_machine_destroy(&machine);
    PettaMachineTableStorage storage;
    assert(petta_machine_table_storage(host->shared_table, &storage));
    assert(storage.dependency_bytes == 0u);
    assert(storage.arena_live_bytes <= storage.arena_reserved_bytes);
    return stats;
}

static PettaMachineStats run_queries(Space *space, Arena *owner, bool licensed,
                                    bool sliced, PettaTableMutationPolicy policy,
                                    const char *const *queries, size_t query_count,
                                    size_t expected, Atom **answers, SymbolId denied_head) {
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), licensed, SIZE_MAX};
    probe.denied_head = denied_head;
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    assert(petta_machine_table_set_mutation_policy(table, policy));
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .permit_transition = sliced ? permit : NULL,
        .shared_table = table,
    };
    PettaMachineStats stats = {0};
    for (size_t subscriber = 0u; subscriber < query_count; subscriber++) {
        PettaMachineStats current = query_once(space, owner, queries[subscriber], &host,
            sliced, !subscriber, expected, answers + subscriber * expected);
        if (!subscriber)
            stats = current;
        else
            assert(current.table_generator_rounds == 0u);
    }
    if (!licensed)
        assert(stats.table_generator_rounds_reused == 0u);
    else if (denied_head)
        assert(probe.denied_admissions > 0u);
    petta_machine_table_free(table);
    return stats;
}

static PettaMachineStats run(Space *space, Arena *owner, bool licensed,
                            bool sliced, PettaTableMutationPolicy policy,
                            size_t expected, Atom **answers) {
    const char *queries[] = {"(node 0)", "(node 1)"};
    return run_queries(space, owner, licensed, sliced, policy, queries, 2u, expected, answers, 0u);
}

static void add_ring(Space *space, Arena *owner, size_t nodes, unsigned seeds) {
    for (size_t i = 0u; i < nodes; i++) {
        char text[128];
        snprintf(text, sizeof(text), "(= (node %zu) (node %zu))", i, (i + 1u) % nodes);
        space_add(space, parse(owner, text));
        if (seeds == 2u || (seeds == 1u && !i)) {
            snprintf(text, sizeof(text), "(= (node %zu) (seed %zu))", i, i);
            space_add(space, parse(owner, text));
        }
    }
}

static void ring(size_t nodes, unsigned seeds, PettaTableMutationPolicy policy) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    add_ring(&space, &owner, nodes, seeds);
    size_t count = seeds == 2u ? nodes : seeds;
    Atom **reference = calloc(count ? 2u * count : 1u, sizeof(*reference));
    Atom **actual = calloc(count ? 2u * count : 1u, sizeof(*actual));
    assert(reference && actual);
    PettaMachineStats before = run(&space, &owner, false, false, policy, count, reference);
    PettaMachineStats after = run(&space, &owner, true, true, policy, count, actual);
    for (size_t i = 0u; i < 2u * count; i++)
        assert(atom_alpha_eq(reference[i], actual[i]));
    assert(after.table_generator_rounds_reused > 0u);
    assert(after.table_generator_rounds < before.table_generator_rounds);
    printf("PASS: recursive ring %zu/%zu/%s preserves ordered support and suspension; rounds=%"
        PRIu64 " -> %" PRIu64 ", reused=%" PRIu64 "\n", nodes, count,
        policy == PETTA_TABLE_MUTATION_STATIC_WORLD ? "static" : "guarded",
        before.table_generator_rounds, after.table_generator_rounds,
        after.table_generator_rounds_reused);
    free(actual);
    free(reference);
    space_free(&space);
    arena_free(&owner);
}

static void components(const char *name, const char *const *rules, size_t rule_count,
                       const char *later_query, size_t expected,
                       PettaTableMutationPolicy policy, const char *denied_head) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    for (size_t i = 0u; i < rule_count; i++)
        space_add(&space, parse(&owner, rules[i]));
    const char *queries[] = {"(node 0)", later_query};
    SymbolId denied = denied_head ? symbol_intern_cstr(g_symbols, denied_head) : 0u;
    Atom *reference[4] = {0}, *actual[4] = {0};
    assert(expected <= 2u);
    PettaMachineStats before = run_queries(&space, &owner, false, false, policy,
        queries, 2u, expected, reference, denied);
    PettaMachineStats after = run_queries(&space, &owner, true, true, policy,
        queries, 2u, expected, actual, denied);
    for (size_t i = 0u; i < 2u * expected; i++)
        assert(atom_alpha_eq(reference[i], actual[i]));
    assert(after.table_generator_rounds_reused > 0u);
    assert(after.table_generator_rounds <= before.table_generator_rounds);
    space_free(&space);
    arena_free(&owner);
    printf("PASS: %s/%s preserves later subscriber answers after component retirement\n",
        name, policy == PETTA_TABLE_MUTATION_STATIC_WORLD ? "static" : "guarded");
}

static void component_lifetimes(PettaTableMutationPolicy policy) {
    const char *diamond[] = {
        "(= (node 0) (node 1))", "(= (node 0) (node 2))",
        "(= (node 1) (node 3))", "(= (node 2) (node 3))",
        "(= (node 3) (node 0))", "(= (node 2) (seed 1))", "(= (node 3) (seed 0))",
    };
    components("cyclic diamond", diamond, sizeof(diamond) / sizeof(*diamond),
        "(node 1)", 2u, policy, NULL);
    const char *reused[] = {
        "(= (node 0) (node 1))", "(= (node 0) (node 3))",
        "(= (node 1) (node 2))", "(= (node 2) (node 1))",
        "(= (node 3) (node 4))", "(= (node 4) (node 3))", "(= (node 3) (seed 0))",
    };
    components("empty then seeded cycles", reused, sizeof(reused) / sizeof(*reused),
        "(node 4)", 1u, policy, NULL);
    const char *nested[] = {
        "(= (node 0) (node 1))", "(= (node 1) (node 0))", "(= (node 0) (node 2))",
        "(= (node 2) (node 3))", "(= (node 3) (node 2))", "(= (node 2) (node 4))",
        "(= (node 4) (node 5))", "(= (node 5) (node 4))", "(= (node 5) (seed 0))",
    };
    components("nested cycles", nested, sizeof(nested) / sizeof(*nested),
        "(node 1)", 1u, policy, NULL);
    const char *denied[] = {
        "(= (node 0) (node 1))", "(= (node 1) (node 2))", "(= (node 2) (node 1))",
        "(= (node 0) (blocked 3))", "(= (blocked 3) (blocked 4))",
        "(= (blocked 4) (blocked 3))", "(= (node 0) (seed 0))",
    };
    components("retired license then denied relation", denied, sizeof(denied) / sizeof(*denied),
        "(node 0)", 1u, policy, "blocked");
    const char *switching[] = {
        "(= (node 0) (case (collapse (node 1)) ((() (node 2)) ($items (node 3)))))",
        "(= (node 1) (node 0))", "(= (node 2) (node 0))", "(= (node 2) (seed 2))",
        "(= (node 3) (seed 3))",
    };
    components("adaptive empty-read branch", switching, sizeof(switching) / sizeof(*switching),
        "(node 1)", 2u, policy, NULL);
    const char *growing[] = {
        "(= (node 0) (case (collapse (node 1)) ((() (node 2)) ($items (node 3)))))",
        "(= (node 1) (node 0))", "(= (node 2) (node 0))", "(= (node 2) (seed 2))",
        "(= (node 3) (node 0))", "(= (node 3) (seed 3))",
    };
    components("adaptive component growth", growing, sizeof(growing) / sizeof(*growing),
        "(node 1)", 2u, policy, NULL);
}

static void allocation_failure(unsigned kind, unsigned occurrence) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    /* The short back-edge creates the first frame array; the long branch
     * grows it while that prefix is still active. */
    space_add(&space, parse(&owner, "(= (node 0) (node 1))"));
    space_add(&space, parse(&owner, "(= (node 1) (node 0))"));
    space_add(&space, parse(&owner, "(= (node 0) (node 2))"));
    size_t nodes = kind == 4u ? 82u : 21u;
    if (kind == 4u)
        space_add(&space, parse(&owner, "(= (node 1) (node 81))"));
    for (size_t node = 2u; node < nodes; node++) {
        char text[96];
        snprintf(text, sizeof(text), "(= (node %zu) (node %zu))", node,
            node + 1u == nodes ? 0u : node + 1u);
        space_add(&space, parse(&owner, text));
    }
    char seed[96];
    snprintf(seed, sizeof(seed), "(= (node %zu) (seed 0))", nodes - 1u);
    space_add(&space, parse(&owner, seed));
    Atom *reference[2] = {0}, *actual[2] = {0}, *recovered[2] = {0};
    run(&space, &owner, false, false, PETTA_TABLE_MUTATION_REVISION_GUARDED, 1u, reference);
    fault_kind = kind;
    fault_at = occurrence;
    fault_calls = 0u;
    fault_observed = false;
    PettaMachineStats failed = run(&space, &owner, true, false,
        PETTA_TABLE_MUTATION_REVISION_GUARDED, 1u, actual);
    assert(fault_observed);
    if (kind <= 2u)
        assert(failed.table_generator_rounds_reused == 0u);
    else
        assert(failed.table_generator_rounds_reused > 0u);
    fault_kind = 0u;
    PettaMachineStats retry = run(&space, &owner, true, false,
        PETTA_TABLE_MUTATION_REVISION_GUARDED, 1u, recovered);
    assert(retry.table_generator_rounds_reused > 0u);
    for (size_t i = 0u; i < 2u; i++) {
        assert(atom_alpha_eq(reference[i], actual[i]));
        assert(atom_alpha_eq(reference[i], recovered[i]));
    }
    space_free(&space);
    arena_free(&owner);
    printf("PASS: producer metadata allocation failure %u/%u preserves answers, retires storage and permits retry\n",
        kind, occurrence);
}

static void cancelled(bool mutate, bool queued) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    add_ring(&space, &owner, 8u, 1u);
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, 0u};
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .permit_transition = permit,
        .shared_table = table,
    };
    Atom *query = parse(&owner, "(node 0)");
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    PettaMachineTableStorage storage;
    size_t transitions = 0u;
    size_t capture_only = 2u * sizeof(void *) + 2u * sizeof(size_t) +
        16u * sizeof(RoundAllocation);
    for (;;) {
        assert(++transitions < 100000u);
        probe.remaining = 1u;
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_SUSPENDED);
        bindings_free(&environment);
        assert(petta_machine_table_storage(table, &storage));
        if (storage.dependency_records && (!queued || storage.dependency_bytes > capture_only))
            break;
    }
    if (mutate) {
        space_add(&space, parse(&owner, "(= (node 7) (seed changed))"));
        probe.remaining = SIZE_MAX;
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_INVALIDATED);
        bindings_free(&environment);
    }
    petta_machine_destroy(&machine);
    assert(petta_machine_table_storage(table, &storage));
    assert(storage.dependency_bytes == 0u);

    /* The abandoned table's partial entries must not be replayed by the
     * next root, whether or not the space changed during cancellation. */
    size_t expected = mutate ? 2u : 1u;
    Atom *reference[4] = {0};
    run(&space, &owner, false, false, PETTA_TABLE_MUTATION_REVISION_GUARDED,
        expected, reference);
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    size_t length = 0u;
    for (;;) {
        probe.remaining = SIZE_MAX;
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            assert(length < expected);
            assert(atom_alpha_eq(answer, reference[length]));
            length++;
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_EXHAUSTED)
            break;
        assert(step == PETTA_MACHINE_STEP_ANSWER);
    }
    assert(length == expected);
    petta_machine_destroy(&machine);
    assert(petta_machine_table_storage(table, &storage));
    assert(storage.dependency_bytes == 0u);
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    printf("PASS: cancelled recursive producers retire reads%s and recompute%s\n",
        queued ? " and queued notifications" : "",
        mutate ? " after space mutation" : " without stale partial entries");
}

static void changed_static_world(void) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    add_ring(&space, &owner, 8u, 0u);
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, 0u};
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    assert(petta_machine_table_set_mutation_policy(table, PETTA_TABLE_MUTATION_STATIC_WORLD));
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .permit_transition = permit,
        .shared_table = table,
    };
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &owner, parse(&owner, "(node 0)"),
        NULL, &host));
    /* Withdraw the Space version after the last producer's admission, while
     * its initial round still uses the earlier source selection. No callee
     * has gained an answer: the Space guard is the necessary dependency. */
    probe.change_on_admission = 8u;
    probe.change_space = &space;
    probe.change_owner = &owner;
    probe.remaining = SIZE_MAX;
    size_t length = 0u;
    for (;;) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep status = petta_machine_next(&machine, &answer, &environment);
        if (status == PETTA_MACHINE_STEP_ANSWER) {
            assert(atom_eq(answer, parse(&owner, "(seed changed)")));
            length++;
        }
        bindings_free(&environment);
        if (status == PETTA_MACHINE_STEP_EXHAUSTED)
            break;
        assert(status == PETTA_MACHINE_STEP_ANSWER);
    }
    assert(probe.admissions == 8u);
    assert(length == 1u);
    petta_machine_destroy(&machine);
    PettaMachineTableStorage storage;
    assert(petta_machine_table_storage(table, &storage));
    assert(storage.dependency_bytes == 0u);
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    puts("PASS: changed static-world input withdraws unfinished producer reuse");
}

static void root_read_windows(void) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    add_ring(&space, &owner, 8u, 1u);
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, SIZE_MAX};
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    assert(petta_machine_table_set_mutation_policy(table, PETTA_TABLE_MUTATION_STATIC_WORLD));
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .shared_table = table,
    };
    Atom *answers[2] = {0};
    query_once(&space, &owner, "(node 0)", &host, false, true, 1u, answers);
    space_add(&space, parse(&owner, "(= (node 0) (seed 1))"));
    PettaMachineStats retained = query_once(&space, &owner, "(node 0)", &host,
        false, false, 1u, answers);
    assert(retained.table_generator_rounds == 0u);
    assert(atom_eq(answers[0], parse(&owner, "(seed 0)")));
    space_add(&space, parse(&owner, "(= (node 8) (node 9))"));
    space_add(&space, parse(&owner, "(= (node 9) (node 8))"));
    space_add(&space, parse(&owner, "(= (node 8) (seed 2))"));
    PettaMachineStats fresh = query_once(&space, &owner, "(node 8)", &host,
        false, false, 1u, answers);
    assert(fresh.table_generator_rounds_reused > 0u);
    assert(atom_eq(answers[0], parse(&owner, "(seed 2)")));
    Atom *reference[4] = {0};
    run(&space, &owner, false, false, PETTA_TABLE_MUTATION_REVISION_GUARDED, 2u, reference);
    assert(petta_machine_table_set_mutation_policy(table, PETTA_TABLE_MUTATION_REVISION_GUARDED));
    query_once(&space, &owner, "(node 0)", &host, false, false, 2u, answers);
    for (size_t i = 0u; i < 2u; i++)
        assert(atom_alpha_eq(answers[i], reference[i]));
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    puts("PASS: fresh root read windows preserve static answers and revalidate after policy changes");
}

static void instantiated_call_keys(void) {
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    space_add(&space, parse(&owner, "(= (node 0) (seed zero))"));
    space_add(&space, parse(&owner, "(= (node 1) (seed one))"));
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, SIZE_MAX};
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .literal_binding_patterns = true,
        .shared_table = table,
    };
    Atom *answers[2];
    PettaMachineStats direct = query_once(&space, &owner, "(node 0)",
        &host, false, true, 1u, answers);
    assert(direct.table_generator_rounds == 1u);
    assert(atom_eq(answers[0], parse(&owner, "(seed zero)")));
    PettaMachineStats bound = query_once(&space, &owner,
        "(let $which 0 (node $which))", &host, false, false, 1u, answers);
    assert(bound.table_generator_rounds == 0u);
    assert(atom_eq(answers[0], parse(&owner, "(seed zero)")));
    PettaMachineStats distinct = query_once(&space, &owner,
        "(let $which 1 (node $which))", &host, false, false, 1u, answers);
    assert(distinct.table_generator_rounds == 1u);
    assert(atom_eq(answers[0], parse(&owner, "(seed one)")));
    PettaMachineStats open = query_once(&space, &owner, "(node $unbound)",
        &host, false, false, 2u, answers);
    assert(open.table_generator_rounds == 1u);
    assert(atom_eq(answers[0], parse(&owner, "(seed zero)")));
    assert(atom_eq(answers[1], parse(&owner, "(seed one)")));
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    puts("PASS: bound caller keys reuse the instantiated variant; distinct and open calls remain distinct");
}

static void deep_owned_producers(void) {
    const size_t depth = 4096u;
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    for (size_t index = 0u; index < depth; index++) {
        char source[128];
        snprintf(source, sizeof(source), "(= (node %zu) (node %zu))", index, index + 1u);
        space_add(&space, parse(&owner, source));
    }
    char source[128];
    snprintf(source, sizeof(source), "(= (node %zu) (seed leaf))", depth);
    space_add(&space, parse(&owner, source));
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, depth * 2u};
    PettaMachineTable *table = petta_machine_table_new();
    assert(table);
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .permit_transition = permit,
        .shared_table = table,
    };
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &owner, parse(&owner, "(node 0)"), NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    PettaMachineStats suspended;
    assert(petta_machine_stats(&machine, &suspended));
    assert(suspended.table_lookups > depth / 4u);
    petta_machine_destroy(&machine);
    PettaMachineTableStorage storage;
    assert(petta_machine_table_storage(table, &storage));
    assert(storage.dependency_bytes == 0u);
    Atom *answers[1];
    PettaMachineStats completed = query_once(&space, &owner, "(node 0)",
        &host, false, false, 1u, answers);
    assert(completed.table_generator_rounds == depth + 1u);
    assert(atom_eq(answers[0], parse(&owner, "(seed leaf)")));
    PettaMachineStats late = query_once(&space, &owner, "(node 1)",
        &host, false, false, 1u, answers);
    assert(late.table_generator_rounds == 0u);
    assert(atom_eq(answers[0], parse(&owner, "(seed leaf)")));
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    puts("PASS: deep owned producers suspend, report work, cancel, recompute and serve a later consumer");
}

static void deep_owned_collections(bool counted) {
    const size_t depth = 2048u;
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    for (size_t index = 0u; index < depth; index++) {
        char source[160];
        snprintf(source, sizeof(source), counted
            ? "(= (layer %zu) (size-atom (collapse (layer %zu))))"
            : "(= (layer %zu) (let $ignored (collapse (layer %zu)) leaf))",
            index, index + 1u);
        space_add(&space, parse(&owner, source));
    }
    char source[128];
    snprintf(source, sizeof(source), "(= (layer %zu) leaf)", depth);
    space_add(&space, parse(&owner, source));
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), false, depth * 3u};
    PettaMachineHost host = {
        .context = &probe,
        .permit_transition = permit,
        .literal_binding_patterns = true,
    };
    PettaMachine machine;
    Atom *query = parse(&owner, "(layer 0)");
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    PettaMachineStats suspended;
    assert(petta_machine_stats(&machine, &suspended));
    assert(suspended.child_machine_init_successes > depth / 4u);
    petta_machine_destroy(&machine);
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    probe.remaining = depth * 3u;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    probe.remaining = SIZE_MAX;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, counted ? atom_int(&owner, 1) : parse(&owner, "leaf")));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    space_free(&space);
    arena_free(&owner);
    puts(counted
        ? "PASS: deep counted collections suspend, report work, cancel and resume without nesting native pulls"
        : "PASS: deep materialized collections suspend, report work, cancel and resume without nesting native pulls");
}

static void owned_pull_capacity(unsigned kind, unsigned form) {
    const size_t depth = 64u;
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    for (size_t index = 0u; index < depth; index++) {
        char source[192];
        const char *pattern = form == 0u
            ? "(= (node %zu) (node %zu))"
            : form == 1u
                ? "(= (node %zu) (let $ignored (collapse (node %zu)) (seed leaf)))"
                : "(= (node %zu) (size-atom (collapse (node %zu))))";
        snprintf(source, sizeof(source), pattern, index, index + 1u);
        space_add(&space, parse(&owner, source));
    }
    char source[128];
    snprintf(source, sizeof(source), "(= (node %zu) (seed leaf))", depth);
    space_add(&space, parse(&owner, source));
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, SIZE_MAX};
    PettaMachineTable *table = form == 0u ? petta_machine_table_new() : NULL;
    assert(form != 0u || table);
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = form == 0u ? contains : NULL,
        .tabled_relation_round_reusable = form == 0u ? reusable : NULL,
        .shared_table = table,
        .literal_binding_patterns = true,
    };
    PettaMachine machine;
    Atom *query = parse(&owner, "(node 0)");
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    pull_allocation = NULL;
    fault_kind = kind;
    fault_at = 1u;
    fault_calls = 0u;
    fault_observed = false;
    fault_armed = true;
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_CAPACITY);
    assert(fault_observed && answer == NULL);
    bindings_free(&environment);
    fault_armed = false;
    fault_kind = 0u;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_CAPACITY);
    assert(answer == NULL);
    bindings_free(&environment);
    teardown_allocation_attempts = 0u;
    forbid_allocations = true;
    petta_machine_destroy(&machine);
    forbid_allocations = false;
    assert(teardown_allocation_attempts == 0u);
    if (table) {
        PettaMachineTableStorage storage;
        assert(petta_machine_table_storage(table, &storage));
        assert(storage.dependency_bytes == 0u);
    }
    assert(petta_machine_init(&machine, &space, &owner, query, NULL, &host));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, form == 2u ? atom_int(&owner, 1) : parse(&owner, "(seed leaf)")));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    printf("PASS: owned child form %u reports refused %s growth, cancels without allocation and recomputes\n",
        form, kind == 6u ? "initial" : "later");
}

static void owned_pull_clock(unsigned form) {
    const size_t depth = 32u;
    Arena owner;
    arena_init(&owner);
    Space space;
    space_init(&space);
    for (size_t index = 0u; index < depth; index++) {
        char source[192];
        const char *pattern = form == 0u
            ? "(= (node %zu) (node %zu))"
            : form == 1u
                ? "(= (node %zu) (let $ignored (collapse (node %zu)) (seed leaf)))"
                : "(= (node %zu) (size-atom (collapse (node %zu))))";
        snprintf(source, sizeof(source), pattern, index, index + 1u);
        space_add(&space, parse(&owner, source));
    }
    char source[128];
    snprintf(source, sizeof(source), "(= (node %zu) (seed leaf))", depth);
    space_add(&space, parse(&owner, source));
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), true, SIZE_MAX};
    PettaMachineTable *table = form == 0u ? petta_machine_table_new() : NULL;
    assert(form != 0u || table);
    PettaMachineHost host = {
        .context = &probe,
        .tabled_relation_contains = form == 0u ? contains : NULL,
        .tabled_relation_round_reusable = form == 0u ? reusable : NULL,
        .shared_table = table,
        .literal_binding_patterns = true,
        .measure_stats = true,
    };
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &owner, parse(&owner, "(node 0)"), NULL, &host));
    fake_clock_tick = 1u;
    fake_clock = true;
    Atom *answer = NULL;
    Bindings environment;
    PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
    fake_clock = false;
    assert(step == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, form == 2u ? atom_int(&owner, 1) : parse(&owner, "(seed leaf)")));
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(fake_clock_tick > depth * 2u);
    assert(stats.active_elapsed_ns == fake_clock_tick - 2u);
    assert(stats.time_to_first_answer_ns == stats.active_elapsed_ns);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    if (table) {
        petta_machine_table_free(table);
        table = petta_machine_table_new();
        assert(table);
        host.shared_table = table;
    }
    host.permit_transition = permit;
    probe.remaining = 3u;
    assert(petta_machine_init(&machine, &space, &owner, parse(&owner, "(node 0)"), NULL, &host));
    uint64_t charged = 0u;
    size_t pauses = 0u;
    bool emitted = false;
    fake_clock_tick = 1u;
    for (;;) {
        uint64_t began = fake_clock_tick;
        fake_clock = true;
        step = petta_machine_next(&machine, &answer, &environment);
        fake_clock = false;
        assert(fake_clock_tick >= began + 2u);
        charged += fake_clock_tick - began - 1u;
        assert(petta_machine_stats(&machine, &stats));
        assert(stats.active_elapsed_ns == charged);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            assert(!emitted);
            emitted = true;
            assert(atom_eq(answer, form == 2u ? atom_int(&owner, 1) : parse(&owner, "(seed leaf)")));
            assert(stats.time_to_first_answer_ns == charged);
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_EXHAUSTED)
            break;
        assert(step == PETTA_MACHINE_STEP_SUSPENDED || step == PETTA_MACHINE_STEP_ANSWER);
        if (step == PETTA_MACHINE_STEP_SUSPENDED)
            pauses++;
        probe.remaining = 3u;
        /* Caller time between pulls must not enter the measured extent. */
        fake_clock_tick += 10000u;
    }
    assert(emitted && pauses > depth);
    petta_machine_destroy(&machine);
    petta_machine_table_free(table);
    space_free(&space);
    arena_free(&owner);
    printf("PASS: owned child form %u measures descendant work exactly and excludes pauses between pulls\n", form);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    for (size_t nodes = 2u; nodes <= 32u; nodes *= 2u) {
        for (unsigned seeds = 0u; seeds <= 2u; seeds++)
            for (unsigned policy = 0u; policy < 2u; policy++)
                ring(nodes, seeds, policy ? PETTA_TABLE_MUTATION_STATIC_WORLD
                                         : PETTA_TABLE_MUTATION_REVISION_GUARDED);
    }
    cancelled(false, false);
    cancelled(true, false);
    cancelled(false, true);
    cancelled(true, true);
    changed_static_world();
    component_lifetimes(PETTA_TABLE_MUTATION_REVISION_GUARDED);
    component_lifetimes(PETTA_TABLE_MUTATION_STATIC_WORLD);
    allocation_failure(1u, 1u);
    allocation_failure(2u, 1u);
    allocation_failure(2u, 2u);
    allocation_failure(3u, 1u);
    allocation_failure(4u, 1u);
    allocation_failure(5u, 1u);
    root_read_windows();
    instantiated_call_keys();
    deep_owned_producers();
    deep_owned_collections(false);
    deep_owned_collections(true);
    for (unsigned form = 0u; form < 3u; form++) {
        owned_pull_capacity(6u, form);
        owned_pull_capacity(7u, form);
        owned_pull_clock(form);
    }
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}
