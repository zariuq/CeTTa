typedef struct {
    PressureProbe base;
    SymbolId publish, idle, finish;
    unsigned publications, idles, refused, completed, attempts;
    bool refuse_once, refuse_always;
    size_t finished_image_bytes, refused_debt;
    uint64_t refused_accepted;
    PettaMachineNativeObligation finished;
    PettaMachineNativePressure pressure;
} CoalescedPressureProbe;

static void *coalesced_pressure_publish(void *context) {
    CoalescedPressureProbe *probe = context;
    void *parent_pin = state_cell_identity_pin(probe->base.cell);
    assert(parent_pin);
    StateCell *child = state_cell_managed_new(NULL);
    assert(child);
    Arena input;
    arena_init_detached(&input);
    char *payload = malloc(PAYLOAD_BYTES);
    assert(payload);
    memset(payload, 'a' + probe->publications % 26u, PAYLOAD_BYTES);
    Atom *self = atom_state(&input, child);
    Atom *text = atom_string_n(&input, payload, PAYLOAD_BYTES);
    assert(self && text);
    free(payload);
    StateCellImage *cycle = state_cell_image_prepare(
        atom_expr2(&input, self, text), atom_symbol(&input, "Atom"), false);
    assert(cycle && state_cell_publish_prepared(child, cycle));
    arena_free(&input);
    assert(state_cell_publish_prepared(probe->base.cell, edge_image(child, probe->publications)));
    assert(state_cell_publish_prepared(probe->base.cell, scalar_image()));
    state_cell_creator_release(child);
    state_cell_identity_unpin(parent_pin);
    return NULL;
}

static PettaMachineHostMode coalesced_pressure_classify(void *context,
        Space *space, Atom *expression) {
    CoalescedPressureProbe *probe = context;
    SymbolId head = atom_head_symbol_id(expression);
    return head == probe->publish || head == probe->idle || head == probe->finish
        ? PETTA_MACHINE_HOST_READY_APPLICATION : classify(&probe->base, space, expression);
}

static PettaMachineNativeCollectionResult coalesced_pressure_reclaim(void *context,
        TermUniverse *universe) {
    CoalescedPressureProbe *probe = context;
    bool pending = false;
    if (probe->publications) {
        PettaMachineNativeObligation obligation;
        PettaMachineNativePressure pressure;
        assert(petta_machine_native_obligation(probe->base.machine, &obligation));
        assert(petta_machine_native_pressure(probe->base.machine, &pressure));
        /* The actual self-cycle, not the scheduler's claimed watermark,
         * determines whether this fixture still has garbage to reclaim. */
        pending = state_cell_diagnostics().nodes > 1u;
        if (pending)
            assert(obligation.current_epoch != obligation.accepted_epoch);
        if (pending)
            probe->attempts++;
        if (pending && probe->refused) {
            /* No later native publication/allocation pays a byte window:
             * this retry must progress from operation opportunities alone. */
            assert(pressure.outstanding_bytes >= probe->refused_debt &&
                pressure.outstanding_bytes < pressure.next_offer_bytes);
        }
        if (pending && (probe->refuse_always || (probe->refuse_once && !probe->refused))) {
            if (!probe->refused) {
                probe->refused_accepted = obligation.accepted_epoch;
                probe->refused_debt = pressure.outstanding_bytes;
            }
            assert(obligation.accepted_epoch == probe->refused_accepted);
            assert(pressure.outstanding_bytes >= probe->refused_debt);
            probe->refused++;
            return PETTA_MACHINE_NATIVE_COLLECTION_DEFERRED;
        }
    }
    assert(universe && universe->counting && space_reclaim_resource_cycles);
    space_reclaim_resource_cycles(universe);
    drain();
    /* COMPLETE certifies real cycle cleanup, not just an attempted callback. */
    assert(state_cell_diagnostics().nodes == 1u);
    if (pending)
        probe->completed++;
    return PETTA_MACHINE_NATIVE_COLLECTION_COMPLETE;
}

static bool coalesced_pressure_evaluate(void *context, Space *space, Arena *arena,
        Atom *expression, const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end, const CettaDelayView *delay) {
    CoalescedPressureProbe *probe = context;
    SymbolId head = atom_head_symbol_id(expression);
    if (head != probe->publish && head != probe->idle && head != probe->finish)
        return evaluate(&probe->base, space, arena, expression, environment, outcomes, end, delay);
    Atom *value = atom_int(arena, 0);
    if (head == probe->publish) {
        Atom *parent = resolved(environment, arena, expression->expr.elems[1]);
        assert(parent && parent->kind == ATOM_GROUNDED &&
            parent->ground.gkind == GV_STATE && parent->ground.ptr == probe->base.cell);
        pthread_t writer;
        assert(!pthread_create(&writer, NULL, coalesced_pressure_publish, probe));
        assert(!pthread_join(writer, NULL));
        probe->publications++;
    } else if (head == probe->idle) {
        probe->idles++;
    } else {
        assert(petta_machine_native_obligation(probe->base.machine, &probe->finished));
        assert(petta_machine_native_pressure(probe->base.machine, &probe->pressure));
        probe->finished_image_bytes = state_cell_storage_diagnostics().live_image_bytes;
        value = atom_int(arena, 42);
    }
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(outcomes, value, &empty);
    bindings_free(&empty);
    *end = cetta_call_failure();
    return true;
}

static void coalesced_pressure_control(const char *name, unsigned publications,
        bool refuse_once, bool refuse_always) {
    Arena persistent, answers;
    TermUniverse universe;
    Space space;
    PettaMachine machine;
    restore_fixture_start(&persistent, &answers, &universe, &space);
    CoalescedPressureProbe probe = {.refuse_once = refuse_once, .refuse_always = refuse_always};
    probe.base.machine = &machine;
    for (Operation op = 0; op < OP_COUNT; op++)
        probe.base.heads[op] = symbol_intern_cstr(g_symbols, operation_names[op]);
    probe.publish = symbol_intern_cstr(g_symbols, "coalesced-pressure-publish");
    probe.idle = symbol_intern_cstr(g_symbols, "coalesced-pressure-idle");
    probe.finish = symbol_intern_cstr(g_symbols, "coalesced-pressure-finish");
    char *body = strdup("(coalesced-pressure-finish $s)");
    assert(body);
    for (unsigned i = 0u; i < 256u + publications; i++) {
        char *next = NULL;
        assert(asprintf(&next, i < 256u
            ? "(let $idle%u (coalesced-pressure-idle) %s)"
            : "(let $publish%u (coalesced-pressure-publish $s) %s)", i, body) > 0);
        free(body);
        body = next;
    }
    char *definition = NULL;
    assert(asprintf(&definition, "(= (coalesced-pressure-entry) (let $s (pressure-new) (let $warm (pressure-warm) (let $touch (pressure-touch $s) %s))))", body) > 0);
    free(body);
    space_add(&space, parse_one(&persistent, definition));
    free(definition);
    term_universe_set_collection_mode(&universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(&universe, NULL) && universe.counting);
    PettaMachineHost host = {.context = &probe, .classify = coalesced_pressure_classify,
        .evaluate = coalesced_pressure_evaluate, .collect_native_images = coalesced_pressure_reclaim,
        .admit_branch_capture = restore_admission, .measure_stats = true};
    assert(petta_machine_init(&machine, &space, &answers,
        parse_one(&persistent, "(coalesced-pressure-entry)"), NULL, &host));
    CettaOwnedContinuation saved;
    cetta_owned_continuation_init(&saved);
    if (refuse_always)
        assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine), &saved) == CETTA_CONTINUATION_READY);
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, atom_int(&answers, 42)));
    bindings_free(&environment);
    assert(probe.publications == publications && probe.idles == 256u);
    if (refuse_always) {
        assert(probe.refused > 1u && probe.refused < 32u && !probe.completed);
        assert(probe.finished.current_epoch != probe.finished.accepted_epoch);
        assert(probe.pressure.reclamation_outstanding && probe.finished_image_bytes >= PAYLOAD_BYTES);
        assert(probe.finished.retry_window > 16u && probe.finished.retry_window <= 1024u &&
            probe.finished.retry_boundaries <= probe.finished.retry_window);
        PettaMachineNativeObligation before, after;
        assert(petta_machine_native_obligation(&machine, &before));
        assert(cetta_continuation_restore(petta_machine_continuation_machine(&machine), &saved) == CETTA_CONTINUATION_READY);
        assert(petta_machine_native_obligation(&machine, &after));
        assert(after.accepted_epoch == before.accepted_epoch && after.current_epoch == before.current_epoch);
    } else {
        assert(probe.completed && (!refuse_once || probe.refused == 1u));
        assert(probe.finished.current_epoch == probe.finished.accepted_epoch);
        assert(!probe.pressure.reclamation_outstanding && probe.finished_image_bytes < PAYLOAD_BYTES);
    }
    cetta_owned_continuation_destroy(&saved);
    printf("PASS %s publications=%u idle=256 cleanup=%u deferred=%u native-bytes=%zu accepted=%llu current=%llu retry=%u\n",
        name, probe.publications, probe.completed, probe.refused, probe.finished_image_bytes,
        (unsigned long long)probe.finished.accepted_epoch, (unsigned long long)probe.finished.current_epoch,
        probe.finished.retry_boundaries);
    petta_machine_destroy(&machine);
    space_free(&space);
    arena_free(&answers);
    space_reclaim_resource_cycles(&universe);
    drain();
    term_universe_free(&universe);
    arena_free(&persistent);
    assert(!state_cell_diagnostics().nodes && !state_cell_diagnostics().images);
}

typedef struct {
    unsigned references, releases;
    StateCell *child;
    void *pin;
    StateCellSnapshot snapshot;
} UnadaptedPressureOwner;
static UnadaptedPressureOwner *unadapted_owner_armed;
static void unadapted_owner_retain(void *opaque) {
    UnadaptedPressureOwner *owner = opaque;
    assert(owner->references++ == 0u);
    owner->pin = state_cell_identity_pin(owner->child);
    assert(owner->pin && state_cell_snapshot_acquire(owner->child, &owner->snapshot));
}
static void unadapted_owner_release(void *opaque) {
    UnadaptedPressureOwner *owner = opaque;
    assert(owner->references-- == 1u);
    owner->releases++;
    state_cell_snapshot_release(&owner->snapshot);
    state_cell_identity_unpin(owner->pin);
    owner->pin = NULL;
}
void __real_arena_init_detached(Arena *) __attribute__((weak));
void __wrap_arena_init_detached(Arena *arena) {
    assert(__real_arena_init_detached);
    __real_arena_init_detached(arena);
    if (unadapted_owner_armed) {
        UnadaptedPressureOwner *owner = unadapted_owner_armed;
        unadapted_owner_armed = NULL;
        assert(arena_retain_owner(arena, owner, unadapted_owner_retain,
                                  unadapted_owner_release));
    }
}

typedef struct {
    PressureProbe base;
    Arena raw;
    StateCell legacy;
    UnadaptedPressureOwner owner;
    SymbolId publish, idle, mutate, finish;
    unsigned mode, published, idle_returns, complete_returns;
    PettaMachineNativeObligation finished;
    PettaMachineStats stats;
} UnadaptedPressureProbe;
static PettaMachineHostMode unadapted_pressure_classify(void *context,
        Space *space, Atom *expression) {
    UnadaptedPressureProbe *probe = context;
    SymbolId head = atom_head_symbol_id(expression);
    return head == probe->publish || head == probe->idle ||
           head == probe->mutate || head == probe->finish
        ? PETTA_MACHINE_HOST_READY_APPLICATION : classify(&probe->base, space, expression);
}
static PettaMachineNativeCollectionResult unadapted_pressure_reclaim(void *context,
        TermUniverse *universe) {
    (void)universe;
    UnadaptedPressureProbe *probe = context;
    if (probe->published) {
        PettaMachineNativeObligation obligation;
        PettaMachineStats stats;
        assert(petta_machine_native_obligation(probe->base.machine, &obligation));
        assert(petta_machine_stats(probe->base.machine, &stats));
        assert(stats.native_pressure_unadapted_containers && obligation.current_epoch == 1u &&
            obligation.accepted_epoch == 0u);
        probe->complete_returns++;
    }
    drain();
    /* This host completion cannot certify an owner edge it cannot inspect. */
    return PETTA_MACHINE_NATIVE_COLLECTION_COMPLETE;
}
static bool unadapted_pressure_evaluate(void *context, Space *space, Arena *arena,
        Atom *expression, const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end, const CettaDelayView *delay) {
    UnadaptedPressureProbe *probe = context;
    SymbolId head = atom_head_symbol_id(expression);
    if (head != probe->publish && head != probe->idle && head != probe->mutate && head != probe->finish)
        return evaluate(&probe->base, space, arena, expression, environment, outcomes, end, delay);
    Atom *value = atom_int(arena, 0);
    if (head == probe->publish) {
        Atom *payload;
        if (probe->mode == 0u)
            payload = atom_state(&probe->raw, &probe->legacy);
        else if (probe->mode == 1u)
            payload = atom_prime_need_capability(&probe->raw, 11u, 22u, 33u);
        else {
            probe->owner.child = state_cell_managed_new(large_image());
            assert(probe->owner.child);
            payload = atom_int(&probe->raw, 7);
            unadapted_owner_armed = &probe->owner;
        }
        assert(payload);
        StateCellImage *image = state_cell_image_prepare(payload,
            atom_symbol(&probe->raw, "Atom"), false);
        assert(image && !unadapted_owner_armed);
        assert(state_cell_publish_prepared(probe->base.cell, image));
        if (probe->mode == 2u) {
            assert(probe->owner.references == 1u && probe->owner.pin && probe->owner.snapshot.image);
            state_cell_creator_release(probe->owner.child);
        }
        probe->published++;
    } else if (head == probe->idle) {
        probe->idle_returns++;
    } else if (head == probe->mutate) {
        PettaMachineNativeObligation before, after;
        assert(petta_machine_native_obligation(probe->base.machine, &before));
        if (probe->mode == 0u) {
            probe->legacy.value = atom_int(&probe->raw, 9);
            StateCellSnapshot snapshot = {0};
            assert(state_cell_snapshot_acquire(probe->base.cell, &snapshot));
            assert(snapshot.value->ground.gkind == GV_STATE && snapshot.value->ground.ptr == &probe->legacy);
            StateCell *raw = snapshot.value->ground.ptr;
            assert(atom_eq(raw->value, atom_int(&probe->raw, 9)));
            state_cell_snapshot_release(&snapshot);
        } else if (probe->mode == 2u) {
            assert(state_cell_publish_prepared(probe->owner.child, scalar_image()));
            assert(probe->owner.snapshot.value->ground.gkind == GV_STRING &&
                probe->owner.snapshot.value->ground.slen == PAYLOAD_BYTES);
            StateCellSnapshot snapshot = {0};
            assert(state_cell_snapshot_acquire(probe->owner.child, &snapshot));
            assert(atom_eq(snapshot.value, atom_int(&probe->raw, 7)));
            state_cell_snapshot_release(&snapshot);
        } else {
            StateCellSnapshot snapshot = {0};
            assert(state_cell_snapshot_acquire(probe->base.cell, &snapshot));
            const CettaPrimeNeedCapability *capability = atom_prime_need_capability_value(snapshot.value);
            assert(capability && capability->session_id == 11u && capability->thunk_id == 22u);
            state_cell_snapshot_release(&snapshot);
        }
        assert(petta_machine_native_obligation(probe->base.machine, &after));
        assert(before.current_epoch == after.current_epoch && after.accepted_epoch == 0u);
    } else {
        assert(petta_machine_native_obligation(probe->base.machine, &probe->finished));
        assert(petta_machine_stats(probe->base.machine, &probe->stats));
        value = atom_int(arena, 42);
    }
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(outcomes, value, &empty);
    bindings_free(&empty);
    *end = cetta_call_failure();
    return true;
}

void unadapted_pressure_controls(void) {
    for (unsigned mode = 0u; mode < 3u; mode++) {
        UnadaptedPressureProbe probe = {.mode = mode};
        Arena persistent, answers;
        TermUniverse universe;
        Space space;
        PettaMachine machine;
        restore_fixture_start(&persistent, &answers, &universe, &space);
        arena_init_detached(&probe.raw);
        probe.legacy = (StateCell){.value = atom_int(&probe.raw, 7),
            .content_type = atom_symbol(&probe.raw, "Atom")};
        probe.base.machine = &machine;
        for (Operation op = 0; op < OP_COUNT; op++)
            probe.base.heads[op] = symbol_intern_cstr(g_symbols, operation_names[op]);
        probe.publish = symbol_intern_cstr(g_symbols, "unadapted-publish");
        probe.idle = symbol_intern_cstr(g_symbols, "unadapted-idle");
        probe.mutate = symbol_intern_cstr(g_symbols, "unadapted-mutate");
        probe.finish = symbol_intern_cstr(g_symbols, "unadapted-finish");
        char *body = strdup("(let $read (unadapted-mutate $s) (unadapted-finish $s))");
        assert(body);
        for (unsigned i = 0u; i < 256u; i++) {
            char *next = NULL;
            assert(asprintf(&next, "(let $idle%u (unadapted-idle) %s)", i, body) > 0);
            free(body); body = next;
        }
        char *definition = NULL;
        assert(asprintf(&definition, "(= (unadapted-entry) (let $s (pressure-new) (let $warm (pressure-warm) (let $touch (pressure-touch $s) (let $write (unadapted-publish $s) %s)))))", body) > 0);
        free(body); space_add(&space, parse_one(&persistent, definition)); free(definition);
        PettaMachineHost host = {.context = &probe, .classify = unadapted_pressure_classify,
            .evaluate = unadapted_pressure_evaluate, .collect_native_images = unadapted_pressure_reclaim,
            .admit_branch_capture = restore_admission, .measure_stats = true};
        assert(petta_machine_init(&machine, &space, &answers,
            parse_one(&persistent, "(unadapted-entry)"), NULL, &host));
        CettaOwnedContinuation saved;
        cetta_owned_continuation_init(&saved);
        assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine), &saved) == CETTA_CONTINUATION_READY);
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        assert(atom_eq(answer, atom_int(&answers, 42))); bindings_free(&environment);
        assert(probe.published == 1u && probe.idle_returns == 256u &&
            probe.complete_returns > 1u && probe.complete_returns < 32u &&
            probe.finished.current_epoch == 1u && probe.finished.accepted_epoch == 0u &&
            probe.stats.native_pressure_unadapted_containers);
        assert(cetta_continuation_restore(petta_machine_continuation_machine(&machine), &saved) == CETTA_CONTINUATION_READY);
        PettaMachineNativeObligation restored;
        assert(petta_machine_native_obligation(&machine, &restored));
        assert(restored.current_epoch == 1u && restored.accepted_epoch == 0u);
        cetta_owned_continuation_destroy(&saved);
        printf("PASS unadapted-%s host-complete=%u accepted=0 current=1 unadapted=%llu raw-change-unwatched=1\n",
            mode == 0u ? "legacy-state" : mode == 1u ? "capability" : "opaque-owner",
            probe.complete_returns, (unsigned long long)probe.stats.native_pressure_unadapted_containers);
        petta_machine_destroy(&machine); space_free(&space); arena_free(&answers);
        term_universe_free(&universe); arena_free(&persistent); drain();
        if (mode == 2u) assert(!probe.owner.references && probe.owner.releases == 1u);
        arena_free(&probe.raw);
        assert(!state_cell_diagnostics().nodes && !state_cell_diagnostics().images);
    }
}
