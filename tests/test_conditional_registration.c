/* Exercise the live registration owner, including malformed producer packets
 * that cannot be authored as ordinary MeTTa terms. */
#include "../src/petta_search_machine.c"

#include <assert.h>
#include <stdio.h>

static bool refuse_service;
static unsigned suspend_attempts, refuse_suspend;

CettaDelayService *__real_cetta_delay_service_new(void);
CettaDelayService *__wrap_cetta_delay_service_new(void) {
    return refuse_service ? NULL : __real_cetta_delay_service_new();
}
bool __real_cetta_delay_suspend(CettaDelayService *, uint32_t, CettaDelayClient,
    Atom *, const VarId *, uint32_t, bool);
bool __wrap_cetta_delay_suspend(CettaDelayService *service, uint32_t mark,
        CettaDelayClient client, Atom *goal, const VarId *vars,
        uint32_t count, bool bound) {
    suspend_attempts++;
    if (refuse_suspend && suspend_attempts == refuse_suspend)
        return false;
    return __real_cetta_delay_suspend(service, mark, client, goal, vars, count, bound);
}

static unsigned checks;
static bool count_goal(void *context, const CettaDelayGoal *goal) {
    assert(goal && goal->goal);
    (*(unsigned *)context)++;
    return true;
}
static unsigned live_goals(CettaDelayService *service) {
    unsigned count = 0u;
    assert(cetta_delay_visit_live(service, count_goal, &count));
    return count;
}
static Atom *one(Arena *arena, Atom *item) {
    return atom_expr(arena, &item, 1u);
}
static Atom *packet(Arena *arena, Atom *var) {
    Atom *component = atom_expr2(arena, atom_symbol(arena, "check"), var);
    return atom_expr2(arena, one(arena, var), one(arena, component));
}

/* A retained owner must not be observed or reset by failed preflight. */
typedef struct { unsigned observations, resets; } Owner;
static void *owner_clone(const void *state) {
    Owner *copy = malloc(sizeof(*copy));
    if (copy) *copy = *(const Owner *)state;
    return copy;
}
static bool owner_detach(void *state) { (void)state; return true; }
static bool owner_frontier(void *state, const uint32_t *kept, uint32_t count) {
    (void)state; (void)kept; (void)count; return true;
}
static void owner_rollback(void *state, uint32_t mark) { (void)state; (void)mark; }
static void owner_commit(void *state) { (void)state; }
static void owner_rebase(void *state, const uint32_t *kept, uint32_t count) {
    (void)state; (void)kept; (void)count;
}
static bool owner_watches(const void *state, VarId var) {
    (void)state; (void)var; return false;
}
static bool owner_bound(void *state, uint32_t mark, VarId var) {
    (void)state; (void)mark; (void)var; return true;
}
static bool owner_reset(void *state, uint32_t mark) {
    (void)mark; ((Owner *)state)->resets++; return true;
}
static bool owner_pending(const void *state) { (void)state; return false; }
static CettaDelaySyncStatus owner_sync(void *state, uint32_t mark,
        const Bindings *bindings, Arena *arena, Bindings *delta, Atom **raised) {
    (void)state; (void)mark; (void)bindings; (void)arena; (void)delta;
    *raised = NULL; return CETTA_DELAY_SYNC_READY;
}
static bool owner_roots(const void *state,
        bool (*visit)(void *, Atom *), void *context) {
    (void)state; (void)visit; (void)context; return true;
}
static bool owner_goals(const void *state,
        bool (*visit)(void *, const CettaDelayGoal *), void *context) {
    (void)visit; (void)context; ((Owner *)state)->observations++; return true;
}
static const CettaDelayOwnerOps owner_ops = {
    .client = CETTA_DELAY_CLIENT_PROLOG, .clone = owner_clone, .free = free,
    .detach = owner_detach, .frontier = owner_frontier,
    .rollback = owner_rollback, .commit = owner_commit, .rebase = owner_rebase,
    .watches = owner_watches, .bound = owner_bound, .withdraw = owner_bound,
    .reset = owner_reset, .pending = owner_pending, .sync = owner_sync,
    .roots = owner_roots, .goals = owner_goals,
};

static void invalid_packets(Space *space, Arena *arena) {
    for (unsigned retained = 0u; retained < 3u; retained++)
    for (unsigned shape = 0u; shape < 18u; shape++) {
        PettaMachine machine;
        assert(petta_machine_init(&machine, space, arena,
            atom_symbol(arena, "unentered"), NULL, NULL));
        PettaMachineImpl *impl = machine.impl;
        Atom *var = atom_var(arena, "$condition");
        Atom *payload = packet(arena, var);
        Atom *component = payload->expr.elems[1]->expr.elems[0];
        Atom *old = atom_symbol(arena, "old-obligation");
        Owner *owner = NULL;
        if (retained) {
            CettaDelayService *service = petta_machine_delay_service(impl);
            assert(service && cetta_delay_suspend(service, 0u,
                CETTA_DELAY_CLIENT_PROLOG, old, &var->var_id, 1u, false));
            if (retained == 2u) {
                owner = calloc(1u, sizeof(*owner));
                assert(owner && cetta_delay_owner_install(service, &owner_ops, owner));
            }
        }
        switch (shape) {
        case 0: payload = NULL; break;
        case 1: payload = old; break;
        case 2: payload = atom_unit(arena); break;
        case 3: payload->expr.elems[0] = NULL; break;
        case 4: payload->expr.elems[1] = NULL; break;
        case 5: payload->expr.elems[0] = old; break;
        case 6: payload->expr.elems[1] = old; break;
        case 7: payload->expr.elems[0]->expr.elems[0] = NULL; break;
        case 8: payload->expr.elems[0]->expr.elems[0] = old; break;
        case 9: payload->expr.elems[1]->expr.elems[0] = NULL; break;
        case 10: payload->expr.elems[1]->expr.elems[0] = old; break;
        case 11: payload->expr.elems[1]->expr.elems[0] = atom_unit(arena); break;
        case 12: component->expr.elems[0] = NULL; break;
        case 13: component->expr.elems[1] = NULL; break;
        case 14: component->expr.elems[1] = old; break;
        case 15: payload->expr.elems[1] = atom_expr2(arena, component, old); break;
        case 16: payload->expr.elems[0] = atom_expr2(arena, var, old); break;
        case 17: payload->expr.elems = NULL; break;
        }
        BindingsBuilder *builder = search_context_builder(&impl->search);
        uint32_t trail = builder->trail_len;
        size_t heap = impl->heap.live_bytes;
        CettaDelayService *service = impl->delay;
        PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
        suspend_attempts = 0u;
        assert(!petta_machine_delay_install(impl, payload, &failure));
        assert(failure == PETTA_MACHINE_STEP_HOST_ERROR);
        assert(impl->delay == service && builder->trail_len == trail);
        assert(impl->heap.live_bytes == heap && suspend_attempts == 0u);
        assert(!owner || (!owner->observations && !owner->resets));
        assert(live_goals(service) == (retained ? 1u : 0u));
        if (retained) assert(cetta_delay_watches(service, var->var_id));
        petta_machine_destroy(&machine);
        checks++;
    }
}

static void registration_history(Space *space, Arena *arena) {
    for (unsigned bound = 0u; bound < 2u; bound++)
    for (unsigned copies = 1u; copies <= 3u; copies++)
    for (unsigned duplicate_vars = 0u; duplicate_vars < 2u; duplicate_vars++) {
        PettaMachine machine;
        assert(petta_machine_init(&machine, space, arena,
            atom_symbol(arena, "unentered"), NULL, NULL));
        PettaMachineImpl *impl = machine.impl;
        Atom *x = atom_var(arena, "$x"), *y = atom_var(arena, "$y");
        BindingsBuilder *builder = search_context_builder(&impl->search);
        CettaDelayService *service = petta_machine_delay_service(impl);
        Atom *old = atom_symbol(arena, "old-obligation");
        assert(service && cetta_delay_suspend(service, 0u,
            CETTA_DELAY_CLIENT_PROLOG, old, &x->var_id, 1u, false));
        if (bound) assert(petta_machine_unify(impl, y, atom_int(arena, 7)));
        bindings_builder_commit(builder);
        uint32_t mark = bindings_builder_history_mark(builder);
        assert(mark != UINT32_MAX);
        Atom *components[3];
        for (unsigned at = 0u; at < copies; at++) {
            Atom *args[] = {atom_int(arena, at), x, y, x};
            components[at] = atom_expr(arena, args, duplicate_vars ? 4u : 3u);
        }
        Atom *payload = atom_expr2(arena, one(arena, x),
            atom_expr(arena, components, copies));
        PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
        assert(petta_machine_delay_install(impl, payload, &failure));
        assert(failure == PETTA_MACHINE_STEP_EXHAUSTED && live_goals(service) == copies);
        assert(cetta_delay_has_woken(service) == (bound != 0u));
        if (!bound) assert(cetta_delay_bound(service,
            bindings_builder_history_mark(builder), x->var_id));
        for (unsigned at = 0u; at < copies; at++) {
            CettaDelayGoal goal;
            assert(cetta_delay_take_woken(service,
                bindings_builder_history_mark(builder), &goal));
            assert(atom_eq(goal.goal, atom_int(arena, at)));
            assert(goal.var_len == (duplicate_vars ? 3u : 2u));
            assert(var_id_eq(goal.vars[0], x->var_id) && var_id_eq(goal.vars[1], y->var_id));
            if (duplicate_vars) assert(var_id_eq(goal.vars[2], x->var_id));
        }
        assert(!cetta_delay_has_woken(service));
        assert(cetta_delay_bound(service, bindings_builder_history_mark(builder), y->var_id));
        assert(!cetta_delay_has_woken(service));
        bindings_builder_rollback(builder, mark);
        assert(live_goals(service) == 1u && cetta_delay_watches(service, x->var_id));
        assert(cetta_delay_bound(service, bindings_builder_history_mark(builder), x->var_id));
        CettaDelayGoal restored;
        assert(cetta_delay_take_woken(service,
            bindings_builder_history_mark(builder), &restored));
        assert(atom_eq(restored.goal, old) && !cetta_delay_has_woken(service));
        petta_machine_destroy(&machine);
        checks++;
    }
}

static void terminal_packets(Space *space, Arena *arena) {
    for (unsigned mode = 0u; mode < 4u; mode++)
    for (unsigned alternatives = 1u; alternatives <= 2u; alternatives++) {
        PettaMachine machine;
        assert(petta_machine_init(&machine, space, arena,
            atom_symbol(arena, "unentered"), NULL, NULL));
        PettaMachineImpl *impl = machine.impl;
        OutcomeSet *outcomes = malloc(sizeof(*outcomes));
        assert(outcomes);
        outcome_set_init_with_owner(outcomes, &impl->heap);
        Atom *var = atom_var(&impl->heap, "$condition");
        Atom *delayed = packet(&impl->heap, var);
        if (!mode) delayed->expr.elems[1] = atom_expr2(&impl->heap,
            delayed->expr.elems[1]->expr.elems[0], atom_int(&impl->heap, 0));
        if (mode == 3u) delayed->expr.elems[1] = atom_expr2(&impl->heap,
            delayed->expr.elems[1]->expr.elems[0],
            delayed->expr.elems[1]->expr.elems[0]);
        for (unsigned at = 0u; at < alternatives; at++) {
            outcome_set_add(outcomes, atom_int(&impl->heap, at),
                search_context_bindings(&impl->search));
            outcomes->items[at].delayed = delayed;
        }
        CettaCount before = space_length64(space);
        assert(space_admit_atom(space, arena, atom_expr2(arena,
            atom_symbol(arena, "EnteredOnce"), atom_int(arena, checks))));
        Atom *expected = atom_var(&impl->heap, "$result");
        refuse_service = mode == 1u;
        refuse_suspend = mode == 2u ? 1u : mode == 3u ? 2u : 0u;
        suspend_attempts = 0u;
        assert(!petta_machine_start_outcome_choice(impl, outcomes, expected, 0u));
        refuse_service = false; refuse_suspend = 0u;
        PettaMachineStep expected_step = mode ? PETTA_MACHINE_STEP_CAPACITY
            : PETTA_MACHINE_STEP_HOST_ERROR;
        for (unsigned pull = 0u; pull < 3u; pull++) {
            Atom *answer = NULL;
            Bindings environment;
            bindings_init(&environment);
            assert(petta_machine_next(&machine, &answer, &environment) == expected_step);
            assert(!answer && space_length64(space) == before + 1u);
            bindings_free(&environment);
        }
        assert(suspend_attempts == (mode == 2u ? 1u : mode == 3u ? 2u : 0u));
        petta_machine_destroy(&machine);
        checks++;
    }
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    invalid_packets(&space, &arena);
    registration_history(&space, &arena);
    terminal_packets(&space, &arena);
    space_free(&space);
    arena_free(&arena);
    symbol_table_free(&symbols);
    printf("PASS: %u conditional-registration controls preserve preflight, multiplicity, history and terminal ownership\n", checks);
    return 0;
}
