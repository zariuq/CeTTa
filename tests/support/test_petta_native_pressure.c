#define _GNU_SOURCE
#include "petta_search_machine.h"
#include "parser.h"
#include "state_cell.h"
#include "symbol.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PAYLOAD_BYTES = 2u * 1024u * 1024u, WARM_BYTES = 1100u * 1024u };
typedef enum { OP_NEW, OP_WARM, OP_TOUCH, OP_WRITE, OP_FOREIGN, OP_READER,
    OP_READER_CHECK, OP_EDGE, OP_SAME_IMAGE, OP_NESTED, OP_FINISH, OP_COUNT } Operation;
static const char *operation_names[OP_COUNT] = {
    "pressure-new", "pressure-warm", "pressure-touch", "pressure-write",
    "pressure-foreign", "pressure-reader", "pressure-reader-check", "pressure-edge",
    "pressure-same-image", "pressure-nested", "pressure-finish"};
typedef struct {
    PettaMachine *machine;
    PettaMachine *nested_child;
    StateCell *cell;
    StateCell *edge_child;
    SymbolId heads[OP_COUNT];
    unsigned calls[OP_COUNT];
    unsigned collections, deferred, graph_depth;
    bool refuse_cleanup, expect_empty, ordinary, graph, nested, suspend_after_finish;
    bool mutate_cleanup, mutated, wrapper_only;
    uint64_t mutation_censuses_before;
    PettaMachineNativeDiscoveryStats mutation_discovery_before;
    size_t first_small_threshold, refused_threshold, refused_debt;
    PettaMachineStats before_write, after_write;
    PettaMachineNativePressure at_touch, at_finish;
} PressureProbe;

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    assert(count == 1 && forms && forms[0]);
    Atom *result = forms[0];
    free(forms);
    return result;
}
static void drain(void) {
    while (state_cell_reclamation_due())
        assert(state_cell_reclaim_bounded(1024u));
}
static StateCellImage *scalar_image(void) {
    Arena input;
    arena_init_detached(&input);
    StateCellImage *image = state_cell_image_prepare(atom_int(&input, 7),
        atom_symbol(&input, "Atom"), false);
    assert(image);
    arena_free(&input);
    return image;
}
static StateCellImage *large_image(void) {
    Arena input;
    arena_init_detached(&input);
    char *bytes = malloc(PAYLOAD_BYTES);
    assert(bytes);
    memset(bytes, 'x', PAYLOAD_BYTES);
    StateCellImage *image = state_cell_image_prepare(
        atom_string_n(&input, bytes, PAYLOAD_BYTES), atom_symbol(&input, "Atom"), false);
    assert(image);
    free(bytes);
    arena_free(&input);
    return image;
}
static StateCellImage *edge_image(StateCell *child, unsigned label) {
    Arena input;
    arena_init_detached(&input);
    Atom *shared = atom_state(&input, child);
    assert(shared);
    StateCellImage *image = state_cell_image_prepare(atom_expr3(&input,
        atom_int(&input, label), shared, shared), atom_symbol(&input, "Atom"), false);
    assert(image);
    arena_free(&input);
    return image;
}
static StateCell *shared_graph(PressureProbe *probe) {
    StateCell *child = state_cell_managed_new(large_image());
    assert(child);
    for (unsigned depth = 0u; depth < (probe->graph_depth ? probe->graph_depth : 2048u); depth++) {
        StateCell *parent = state_cell_managed_new(edge_image(child, depth));
        assert(parent);
        state_cell_creator_release(child);
        child = parent;
    }
    probe->edge_child = child;
    StateCell *root = state_cell_managed_new(edge_image(child, 0u));
    assert(root);
    state_cell_creator_release(child);
    return root;
}
static Operation operation(const PressureProbe *probe, const Atom *expression) {
    if (!expression || expression->kind != ATOM_EXPR)
        return OP_COUNT;
    SymbolId head = atom_head_symbol_id(expression);
    for (Operation op = 0; op < OP_COUNT; op++)
        if (head == probe->heads[op])
            return op;
    return OP_COUNT;
}
static PettaMachineHostMode classify(void *context, Space *space, Atom *expression) {
    (void)space;
    return operation(context, expression) == OP_COUNT
        ? PETTA_MACHINE_HOST_NONE : PETTA_MACHINE_HOST_READY_APPLICATION;
}
static PettaMachineNativeCollectionResult reclaim(void *context, TermUniverse *universe) {
    (void)universe;
    PressureProbe *probe = context;
    probe->collections++;
    if (probe->refuse_cleanup && probe->calls[OP_FOREIGN] &&
            (!probe->mutate_cleanup || probe->mutated)) {
        PettaMachineNativePressure pressure;
        assert(petta_machine_native_pressure(probe->machine, &pressure));
        assert(pressure.reclamation_outstanding);
        probe->deferred++;
        probe->refused_threshold = pressure.accepted_survivor_threshold;
        probe->refused_debt = pressure.outstanding_bytes;
        return PETTA_MACHINE_NATIVE_COLLECTION_DEFERRED;
    }
    drain();
    if (probe->mutate_cleanup && probe->calls[OP_FOREIGN] && !probe->mutated) {
        /* A real publication after the scheduler's census must invalidate
         * even a COMPLETE host return; it was not part of that census. */
        assert(state_cell_publish_prepared(probe->cell, scalar_image()));
        probe->mutated = true;
    }
    return PETTA_MACHINE_NATIVE_COLLECTION_COMPLETE;
}
static void *foreign_publish(void *context) {
    StateCell *cell = context;
    void *pin = state_cell_identity_pin(cell);
    assert(pin);
    StateCellImage *image = large_image();
    assert(state_cell_publish_prepared(cell, image));
    state_cell_identity_unpin(pin);
    return NULL;
}
static Atom *resolved(const Bindings *environment, Arena *arena, Atom *atom) {
    Bindings copy;
    bindings_init(&copy);
    assert(bindings_clone(&copy, environment));
    Atom *result = bindings_apply(&copy, arena, atom);
    bindings_free(&copy);
    return result;
}
typedef struct { unsigned wrappers, state_owners; } OwnerKinds;
static void owner_kind(void *context, void *owner, void (*release)(void *)) {
    OwnerKinds *kinds = context;
    StateCell *cell = NULL;
    StateCellImage *image = NULL;
    if (state_cell_classify_owner(owner, release, &cell, &image))
        kinds->state_owners++;
    TermUniverseResourceImageView wrapper = {0};
    if (term_universe_resource_image_owner_view(owner, release, &wrapper))
        kinds->wrappers++;
}
static bool evaluate(void *context, Space *space, Arena *arena, Atom *expression,
        const Bindings *environment, OutcomeSet *outcomes, CettaCallOutcome *end,
        const CettaDelayView *delay) {
    (void)space; (void)delay;
    PressureProbe *probe = context;
    Operation op = operation(probe, expression);
    assert(op != OP_COUNT);
    probe->calls[op]++;
    Atom *value = atom_int(arena, 0);
    switch (op) {
    case OP_NEW: {
        assert(!probe->cell);
        probe->cell = probe->graph ? shared_graph(probe) : state_cell_managed_new(scalar_image());
        assert(probe->cell);
        if (probe->wrapper_only) {
            Arena source;
            TermUniverse temporary;
            arena_init_detached(&source);
            term_universe_init(&temporary);
            term_universe_set_persistent_arena(&temporary, &source);
            Atom *raw = atom_state(&source, probe->cell);
            assert(raw);
            AtomId id = term_universe_store_atom_id(&temporary, &source, raw);
            assert(id != CETTA_ATOM_ID_NONE &&
                term_universe_view_atom(&temporary, arena, id));
            term_universe_free(&temporary);
            arena_free(&source);
            OwnerKinds kinds = {0};
            arena_visit_retained_owners(arena, owner_kind, &kinds);
            assert(kinds.wrappers && !kinds.state_owners);
            assert(arena->state_root_acquisition_generation);
            /* The only State lease is inside the TU fallback wrapper. The
             * ordinary return adds no direct State root to this machine. */
            value = atom_int(arena, 0);
        } else
            value = atom_state(arena, probe->cell);
        assert(value);
        state_cell_creator_release(probe->cell);
        break;
    }
    case OP_WARM:
        assert(arena_alloc(arena, WARM_BYTES));
        break;
    case OP_TOUCH: {
        Atom *argument = resolved(environment, arena, expression->expr.elems[1]);
        assert(argument && argument->kind == ATOM_GROUNDED &&
            argument->ground.gkind == GV_STATE && argument->ground.ptr == probe->cell);
        if (probe->calls[OP_TOUCH] == 1u) {
            assert(petta_machine_stats(probe->machine, &probe->before_write));
            assert(probe->before_write.deterministic_minor_heap_collections > 0u);
            assert(petta_machine_native_pressure(probe->machine, &probe->at_touch));
            assert(probe->at_touch.active);
            if (!probe->graph)
                assert(probe->at_touch.last_owned_bytes < PAYLOAD_BYTES);
            probe->first_small_threshold = probe->at_touch.accepted_survivor_threshold;
        }
        break;
    }
    case OP_WRITE:
        assert(state_cell_publish_prepared(probe->cell, large_image()));
        break;
    case OP_FOREIGN: {
        assert(state_cell_is_managed(probe->cell));
        pthread_t writer;
        assert(!pthread_create(&writer, NULL, foreign_publish, probe->cell));
        assert(!pthread_join(writer, NULL));
        break;
    }
    case OP_READER: {
        StateCellSnapshot snapshot = {0};
        assert(state_cell_snapshot_into(arena, probe->cell, &snapshot) == STATE_CELL_HELD);
        assert(!snapshot.image && snapshot.value);
        value = snapshot.value;
        break;
    }
    case OP_READER_CHECK: {
        Atom *argument = resolved(environment, arena, expression->expr.elems[1]);
        assert(argument && argument->kind == ATOM_GROUNDED &&
            argument->ground.gkind == GV_STRING && atom_string_len(argument) == PAYLOAD_BYTES);
        for (size_t i = 0; i < PAYLOAD_BYTES; i++)
            assert(argument->ground.sval[i] == 'x');
        break;
    }
    case OP_EDGE: case OP_SAME_IMAGE: {
        if (probe->calls[OP_EDGE] + probe->calls[OP_SAME_IMAGE] == 1u) {
            PettaMachineStats stats;
            assert(petta_machine_stats(probe->machine, &stats));
            probe->mutation_censuses_before = stats.native_pressure_censuses;
            assert(petta_machine_native_discovery_stats(probe->machine,
                &probe->mutation_discovery_before));
        }
        if (op == OP_EDGE)
            assert(state_cell_publish_prepared(probe->cell,
                edge_image(probe->edge_child, probe->calls[op])));
        else {
            StateCellSnapshot snapshot = {0};
            assert(state_cell_snapshot_acquire(probe->cell, &snapshot));
            assert(state_cell_publish_prepared(probe->cell, snapshot.image));
            snapshot.image = NULL; /* Publication consumed this exact pin. */
            state_cell_snapshot_release(&snapshot);
        }
        break;
    }
    case OP_NESTED: {
        Atom *answer = NULL;
        Bindings bindings;
        assert(probe->nested_child);
        assert(petta_machine_next(probe->nested_child, &answer, &bindings) ==
            PETTA_MACHINE_STEP_SUSPENDED);
        assert(!answer);
        bindings_free(&bindings);
        value = atom_int(arena, 42);
        break;
    }
    case OP_FINISH:
        assert(petta_machine_stats(probe->machine, &probe->after_write));
        assert(petta_machine_native_pressure(probe->machine, &probe->at_finish));
        if (probe->expect_empty && !probe->refuse_cleanup && !probe->nested)
            assert(!state_cell_diagnostics().nodes);
        value = atom_int(arena, 42);
        break;
    default:
        abort();
    }
    Bindings empty;
    bindings_init(&empty);
    assert(value);
    outcome_set_add(outcomes, value, &empty);
    bindings_free(&empty);
    *end = cetta_call_failure();
    return true;
}
static bool permit(void *context, const CettaSourceStep *source) {
    (void)source;
    PressureProbe *probe = context;
    return !probe->suspend_after_finish || !probe->calls[OP_FINISH];
}
static void run_case(const char *name, const char *query, PressureProbe *probe) {
    Arena persistent, answers;
    TermUniverse universe;
    Space space;
    PettaMachine machine;
    arena_init_detached(&persistent);
    arena_init_detached(&answers);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&space, &universe);
    probe->machine = &machine;
    for (Operation op = 0; op < OP_COUNT; op++)
        probe->heads[op] = symbol_intern_cstr(g_symbols, operation_names[op]);
    PettaMachineHost host = {.context = probe, .classify = classify,
        .evaluate = evaluate, .permit_transition = permit,
        .collect_native_images = reclaim, .measure_stats = true};
    /* The public query is closed; the lexical State belongs to the fresh
     * equation activation rather than a retained root-query substitution. */
    char *definition = NULL;
    assert(asprintf(&definition, "(= (pressure-entry) %s)", query) > 0);
    space_add(&space, parse_one(&persistent, definition));
    free(definition);
    assert(petta_machine_init(&machine, &space, &answers,
        parse_one(&persistent, "(pressure-entry)"), NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
    if (step != PETTA_MACHINE_STEP_ANSWER)
        fprintf(stderr, "%s step=%d\n", name, step);
    assert(step == PETTA_MACHINE_STEP_ANSWER && atom_eq(answer, atom_int(&answers, 42)));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    if (probe->ordinary) {
        assert(!stats.native_pressure_censuses && !stats.native_pressure_major_requests);
        assert(!probe->at_finish.active);
    } else {
        assert(stats.native_pressure_censuses);
        assert(stats.maximum_owned_native_image_bytes >= PAYLOAD_BYTES);
        if (probe->expect_empty) {
            assert(stats.native_pressure_major_requests);
            assert(stats.deterministic_major_heap_collections >
                probe->before_write.deterministic_major_heap_collections);
            if (probe->refuse_cleanup) {
                assert(probe->deferred && probe->at_finish.reclamation_outstanding);
                assert(probe->at_finish.accepted_survivor_threshold == probe->first_small_threshold);
                assert(probe->at_finish.outstanding_bytes >= probe->refused_debt);
                assert(probe->at_finish.next_offer_bytes > probe->at_finish.accepted_survivor_threshold);
            } else {
                assert(!probe->at_finish.reclamation_outstanding);
                assert(probe->at_finish.last_owned_bytes < PAYLOAD_BYTES);
                assert(stats.native_pressure_reclamations);
            }
        }
    }
    if (probe->mutate_cleanup) {
        assert(probe->mutated && probe->deferred);
        assert(probe->at_finish.reclamation_outstanding);
        assert(probe->at_finish.accepted_survivor_threshold == probe->first_small_threshold);
        assert(probe->at_finish.outstanding_bytes >= PAYLOAD_BYTES);
        assert(stats.native_pressure_reclamation_deferrals >= 2u);
    }
    if (probe->graph) {
        uint64_t mutation_censuses = stats.native_pressure_censuses - probe->mutation_censuses_before;
        assert(!mutation_censuses);
        PettaMachineNativeDiscoveryStats discovery;
        assert(petta_machine_native_discovery_stats(&machine, &discovery));
        uint64_t images = discovery.state_images - probe->mutation_discovery_before.state_images;
        uint64_t nodes = discovery.node_versions - probe->mutation_discovery_before.node_versions;
        uint64_t owners = discovery.owner_entries - probe->mutation_discovery_before.owner_entries;
        if (probe->calls[OP_EDGE]) {
            assert(images == probe->calls[OP_EDGE]);
            assert(nodes == probe->calls[OP_EDGE]);
            assert(owners <= 4u * probe->calls[OP_EDGE]);
        } else {
            assert(!images && nodes == probe->calls[OP_SAME_IMAGE]);
        }
        if (probe->graph_depth > 4096u)
            assert(probe->mutation_discovery_before.node_versions == 4096u);
        printf("CONTROL %s shared-depth=%u updates=%u mutation-censuses=%llu images=%llu nodes=%llu owners=%llu\n",
            name, probe->graph_depth ? probe->graph_depth : 2048u,
            probe->calls[OP_EDGE] + probe->calls[OP_SAME_IMAGE],
            (unsigned long long)mutation_censuses, (unsigned long long)images,
            (unsigned long long)nodes, (unsigned long long)owners);
    }
    printf("PASS %s census=%llu majors=%llu native-major=%llu reclaimed=%llu deferred=%llu max=%zu owned=%zu debt=%zu threshold=%zu\n",
        name, (unsigned long long)stats.native_pressure_censuses,
        (unsigned long long)stats.deterministic_major_heap_collections,
        (unsigned long long)stats.native_pressure_major_requests,
        (unsigned long long)stats.native_pressure_reclamations,
        (unsigned long long)stats.native_pressure_reclamation_deferrals,
        stats.maximum_owned_native_image_bytes, probe->at_finish.last_owned_bytes,
        probe->at_finish.outstanding_bytes, probe->at_finish.accepted_survivor_threshold);
    petta_machine_destroy(&machine);
    if (probe->ordinary && probe->cell)
        state_cell_creator_release(probe->cell);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&persistent);
    drain();
    assert(!state_cell_diagnostics().nodes && !state_cell_diagnostics().images);
}
static void nested_control(void) {
    Arena persistent[2], answers[2];
    TermUniverse universes[2];
    Space spaces[2];
    PettaMachine machines[2];
    PressureProbe probes[2] = {{.nested_child = &machines[1]},
        {.expect_empty = true, .nested = true, .suspend_after_finish = true}};
    const char *queries[2] = {"(pressure-nested)",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $last (pressure-touch $s) (let $write (pressure-foreign) (pressure-finish)))))"};
    for (unsigned i = 0u; i < 2u; i++) {
        arena_init_detached(&persistent[i]);
        arena_init_detached(&answers[i]);
        term_universe_init(&universes[i]);
        term_universe_set_persistent_arena(&universes[i], &persistent[i]);
        space_init_with_universe(&spaces[i], &universes[i]);
        probes[i].machine = &machines[i];
        for (Operation op = 0; op < OP_COUNT; op++)
            probes[i].heads[op] = symbol_intern_cstr(g_symbols, operation_names[op]);
        PettaMachineHost host = {.context = &probes[i], .classify = classify,
            .evaluate = evaluate, .permit_transition = permit,
            .collect_native_images = reclaim, .measure_stats = true};
        char *definition = NULL;
        assert(asprintf(&definition, "(= (pressure-entry) %s)", queries[i]) > 0);
        space_add(&spaces[i], parse_one(&persistent[i], definition));
        free(definition);
        assert(petta_machine_init(&machines[i], &spaces[i], &answers[i],
            parse_one(&persistent[i], "(pressure-entry)"), NULL, &host));
    }
    Atom *answer = NULL;
    Bindings bindings;
    assert(petta_machine_next(&machines[0], &answer, &bindings) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, atom_int(&answers[0], 42)));
    bindings_free(&bindings);
    assert(!probes[1].collections && probes[1].calls[OP_FOREIGN] == 1u);
    PettaMachineNativePressure pressure;
    assert(petta_machine_native_pressure(&machines[1], &pressure));
    assert(pressure.reclamation_outstanding && pressure.outstanding_bytes >= PAYLOAD_BYTES);
    assert(pressure.accepted_survivor_threshold == probes[1].first_small_threshold);
    probes[1].suspend_after_finish = false;
    assert(petta_machine_next(&machines[1], &answer, &bindings) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, atom_int(&answers[1], 42)));
    bindings_free(&bindings);
    assert(probes[1].collections);
    assert(petta_machine_native_pressure(&machines[1], &pressure));
    assert(!pressure.reclamation_outstanding && pressure.last_owned_bytes < PAYLOAD_BYTES);
    assert(!state_cell_diagnostics().nodes);
    printf("PASS nested-cleanup-admission before-callbacks=0 after-callbacks=%u debt=%zu threshold=%zu\n",
        probes[1].collections, pressure.outstanding_bytes, pressure.accepted_survivor_threshold);
    for (unsigned i = 0u; i < 2u; i++) {
        petta_machine_destroy(&machines[i]);
        space_free(&spaces[i]);
        term_universe_free(&universes[i]);
        arena_free(&answers[i]);
        arena_free(&persistent[i]);
    }
    drain();
    assert(!state_cell_diagnostics().nodes && !state_cell_diagnostics().images);
}
static char *mutation_query(const char *operation_name, unsigned repetitions) {
    char *query = strdup("(let $last (pressure-touch $s) (pressure-finish))");
    assert(query);
    for (unsigned i = 0u; i < repetitions; i++) {
        char *next = NULL;
        assert(asprintf(&next, "(let $update%u (%s) %s)", i, operation_name, query) > 0);
        free(query);
        query = next;
    }
    char *result = NULL;
    assert(asprintf(&result, "(let $s (pressure-new) (let $warm (pressure-warm) (let $first (pressure-touch $s) %s)))", query) > 0);
    free(query);
    return result;
}
static StateCell *restore_refused_child;
static unsigned restore_child_ensures;
static bool restore_refuse_once;
bool __real_state_cell_pressure_observer_ensure(StateCellPressureObserver *, StateCell *, bool *);
bool __wrap_state_cell_pressure_observer_ensure(StateCellPressureObserver *observer,
        StateCell *cell, bool *fresh) {
    if (cell == restore_refused_child) {
        restore_child_ensures++;
        if (restore_refuse_once) {
            restore_refuse_once = false;
            if (fresh) *fresh = false;
            return false; /* Explicit discovery refusal at the real interface. */
        }
    }
    return __real_state_cell_pressure_observer_ensure(observer, cell, fresh);
}
typedef struct { bool allowed; } RestorePressureProbe;
static bool restore_permit(void *context, const CettaSourceStep *source) {
    (void)source;
    return ((RestorePressureProbe *)context)->allowed;
}
static CettaBranchAdmission restore_admission(void *context, Space *space) {
    (void)context;
    return (CettaBranchAdmission){.status = CETTA_BRANCH_ADMISSION_AVAILABLE,
        .capacity = CETTA_BRANCH_CAPTURE_MULTI_SHOT,
        .authority = {.length = 2u, .words = {
            UINT64_C(0x5354415445525354), space_instance_id(space)}}};
}
static void restore_fixture_start(Arena *persistent, Arena *answers,
        TermUniverse *universe, Space *space) {
    arena_init_detached(persistent);
    arena_init_detached(answers);
    term_universe_init(universe);
    term_universe_set_persistent_arena(universe, persistent);
    space_init_with_universe(space, universe);
}
static void restore_fixture_finish(PettaMachine *machine, Space *space,
        TermUniverse *universe, Arena *persistent, Arena *answers) {
    petta_machine_destroy(machine);
    space_free(space);
    term_universe_free(universe);
    arena_free(answers);
    arena_free(persistent);
    drain();
    assert(!state_cell_diagnostics().nodes && !state_cell_diagnostics().images);
}
static void restore_partial_discovery_control(void) {
    Arena persistent, answers, input;
    TermUniverse universe;
    Space space;
    restore_fixture_start(&persistent, &answers, &universe, &space);
    arena_init_detached(&input);
    StateCell *child = state_cell_managed_new(scalar_image());
    assert(child);
    StateCellImage *parent_image = state_cell_image_prepare(atom_state(&input, child),
        atom_symbol(&input, "Atom"), true);
    assert(parent_image);
    StateCell *parent = state_cell_managed_new(parent_image);
    assert(parent);
    state_cell_creator_release(child);
    Atom *query = atom_state(&input, parent);
    assert(query);
    state_cell_creator_release(parent);
    RestorePressureProbe probe = {.allowed = true};
    PettaMachineHost host = {.context = &probe, .permit_transition = restore_permit,
        .admit_branch_capture = restore_admission, .measure_stats = true};
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &answers, query, NULL, &host));
    CettaOwnedContinuation saved;
    cetta_owned_continuation_init(&saved);
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine),
        &saved) == CETTA_CONTINUATION_READY);
    arena_free(&input);
    restore_refused_child = child;
    restore_child_ensures = 0u;
    restore_refuse_once = true;
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_CAPACITY);
    bindings_free(&environment);
    assert(restore_child_ensures == 1u);
    PettaMachineNativePressure before, after;
    assert(petta_machine_native_pressure(&machine, &before));
    probe.allowed = false;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_CAPACITY);
    bindings_free(&environment);
    assert(restore_child_ensures == 1u);
    assert(cetta_continuation_restore(petta_machine_continuation_machine(&machine),
        &saved) == CETTA_CONTINUATION_READY);
    assert(petta_machine_native_pressure(&machine, &after));
    assert(after.accepted_survivor_threshold == before.accepted_survivor_threshold &&
        after.outstanding_bytes >= before.outstanding_bytes);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    probe.allowed = true;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    assert(answer && answer->kind == ATOM_GROUNDED &&
        answer->ground.gkind == GV_STATE && answer->ground.ptr == parent);
    assert(restore_child_ensures >= 2u);
    printf("PASS restore-partial-discovery capacity-terminal=2 allowance-resume=1 child-ensures=%u debt=%zu threshold=%zu\n",
        restore_child_ensures, after.outstanding_bytes, after.accepted_survivor_threshold);
    restore_refused_child = NULL;
    cetta_owned_continuation_destroy(&saved);
    restore_fixture_finish(&machine, &space, &universe, &persistent, &answers);
}
static void restore_before_first_next_control(void) {
    Arena persistent, answers, input;
    TermUniverse universe;
    Space space;
    restore_fixture_start(&persistent, &answers, &universe, &space);
    arena_init_detached(&input);
    StateCell *cell = state_cell_managed_new(scalar_image());
    assert(cell);
    Atom *query = atom_state(&input, cell);
    assert(query);
    state_cell_creator_release(cell);
    RestorePressureProbe probe = {.allowed = true};
    PettaMachineHost host = {.context = &probe, .permit_transition = restore_permit,
        .admit_branch_capture = restore_admission, .measure_stats = true};
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &answers, query, NULL, &host));
    CettaOwnedContinuation saved;
    cetta_owned_continuation_init(&saved);
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine),
        &saved) == CETTA_CONTINUATION_READY);
    assert(cetta_continuation_restore(petta_machine_continuation_machine(&machine),
        &saved) == CETTA_CONTINUATION_READY);
    arena_free(&input);
    PettaMachineNativePressure before, after;
    assert(petta_machine_native_pressure(&machine, &before) && !before.active);
    pthread_t writer;
    assert(!pthread_create(&writer, NULL, foreign_publish, cell));
    assert(!pthread_join(writer, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    assert(answer && answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_STATE &&
        answer->ground.ptr == cell);
    assert(petta_machine_native_pressure(&machine, &after));
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(after.active && after.last_owned_bytes >= PAYLOAD_BYTES &&
        after.outstanding_bytes >= PAYLOAD_BYTES && after.reclamation_outstanding &&
        after.accepted_survivor_threshold == before.accepted_survivor_threshold &&
        stats.native_pressure_major_requests > 0u);
    printf("PASS restore-before-first-next foreign-bytes=%u observed=%zu debt=%zu threshold=%zu requests=%llu\n",
        PAYLOAD_BYTES, after.last_owned_bytes, after.outstanding_bytes,
        after.accepted_survivor_threshold, (unsigned long long)stats.native_pressure_major_requests);
    cetta_owned_continuation_destroy(&saved);
    restore_fixture_finish(&machine, &space, &universe, &persistent, &answers);
}

#include "petta_native_coalesced_pressure.inc"

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    run_case("foreign-writer-tenured-drop",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $last (pressure-touch $s) (let $write (pressure-foreign) (pressure-finish)))))",
        &(PressureProbe){.expect_empty = true});
    run_case("wrapper-only-foreign-publication",
        "(let $unused (pressure-new) (let $write (pressure-foreign) (pressure-finish)))",
        &(PressureProbe){.expect_empty = true, .wrapper_only = true});
    run_case("direct-writer-tenured-drop",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $last (pressure-touch $s) (let $write (pressure-write) (pressure-finish)))))",
        &(PressureProbe){.expect_empty = true});
    run_case("deferred-keeps-debt",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $last (pressure-touch $s) (let $write (pressure-foreign) (pressure-finish)))))",
        &(PressureProbe){.expect_empty = true, .refuse_cleanup = true});
    run_case("publication-between-census-and-ack",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $first (pressure-touch $s) (let $write (pressure-foreign) (let $last (pressure-touch $s) (pressure-finish))))))",
        &(PressureProbe){.refuse_cleanup = true, .mutate_cleanup = true});
    run_case("retained-reader",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $first (pressure-touch $s) (let $write (pressure-write) (let $old (pressure-reader $s) (let $again (pressure-write) (let $read (pressure-reader-check $old) (let $last (pressure-touch $s) (pressure-finish)))))))))",
        &(PressureProbe){0});
    run_case("overwrite-only",
        "(let $s (pressure-new) (let $warm (pressure-warm) (let $first (pressure-touch $s) (let $w1 (pressure-write) (let $w2 (pressure-write) (let $w3 (pressure-write) (let $w4 (pressure-write) (let $last (pressure-touch $s) (pressure-finish)))))))))",
        &(PressureProbe){0});
    run_case("no-resource-ordinary", "(let $warm (pressure-warm) (pressure-finish))",
        &(PressureProbe){.ordinary = true});
    run_case("unrelated-foreign-writer", "(let $write (pressure-foreign) (pressure-finish))",
        &(PressureProbe){.ordinary = true, .cell = state_cell_managed_new(scalar_image())});
    nested_control();
    for (unsigned mode = 0u; mode < 2u; mode++) {
        char *query = mutation_query(mode ? "pressure-edge" : "pressure-same-image", 32u);
        run_case(mode ? "stable-shared-graph-edge-replacements" : "same-image-publications",
            query, &(PressureProbe){.graph = true});
        free(query);
    }
    char *quantum_query = mutation_query("pressure-same-image", 1u);
    run_case("finite-node-capture-quantum", quantum_query,
        &(PressureProbe){.graph = true, .graph_depth = 8192u});
    free(quantum_query);
    restore_partial_discovery_control();
    restore_before_first_next_control();
    coalesced_pressure_control("coalesced-foreign-cycles", 8u, false, false);
    coalesced_pressure_control("coalesced-transient-refusal-idle-retry", 1u, true, false);
    coalesced_pressure_control("coalesced-permanent-refusal-backoff-restore", 1u, false, true);
    unadapted_pressure_controls();
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS native State/Image pressure controls");
    return 0;
}
