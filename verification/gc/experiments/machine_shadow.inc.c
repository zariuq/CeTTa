/* Included only in a generated experimental translation unit. These are
 * reviewed diagnostic visitors, not a complete production root scanner. */
#include "eval.h"
#include "shadow_probe.h"

static void gc_probe_visit_goal(const PettaGoal *goal, const char *owner) {
    gc_probe_visit_atom(goal->first, owner);
    gc_probe_visit_atom(goal->second, owner);
    gc_probe_visit_atom(goal->third, owner);
    gc_probe_visit_atom(goal->fourth, owner);
    gc_probe_visit_atom(goal->equation_query_source, owner);
}
static bool gc_probe_delay_atom(void *unused, Atom *atom) {
    (void)unused; gc_probe_visit_atom(atom, "delays"); return true;
}
static bool gc_probe_delay_goal(void *unused, const CettaDelayGoal *goal) {
    return gc_probe_delay_atom(unused, goal->goal);
}
static void gc_probe_machine_boundary(PettaMachineImpl *machine) {
    if (gc_probe_guest_depth != 1 || !gc_probe_shadow_begin(machine->choice_len)) return;
    gc_probe_visit_atom(machine->query, "query");
    gc_probe_visit_atom(machine->answer_variable, "query");
    gc_probe_visit_atom(machine->raised_error, "query");
    gc_probe_visit_space(machine->space, "spaces");
    Registry *registry = eval_current_registry();
    if (registry) for (uint32_t i = 0; i < registry->len; i++) {
        gc_probe_visit_id(registry->entries[i].key, "registry");
        gc_probe_visit_atom(registry_entry_name_key(registry, i), "registry");
        gc_probe_visit_atom(registry->entries[i].value, "registry");
    }
    gc_probe_visit_named_states(registry);
    const Bindings *bindings = bindings_builder_bindings(
        search_context_builder(&machine->search));
    gc_probe_visit_bindings(bindings, "bindings");
    size_t height = machine->goal_len > machine->protected_goal_height
        ? machine->goal_len : machine->protected_goal_height;
    assert(height <= machine->goal_initialized_len);
    for (size_t i = 0; i < height; i++) gc_probe_visit_goal(&machine->goals[i], "goals");
    for (size_t i = 0; i < machine->goal_trail_len; i++)
        gc_probe_visit_goal(&machine->goal_trail[i].previous, "goal-trail");
    for (size_t i = 0; i < machine->visible_len; i++)
        gc_probe_visit_atom(machine->visible[i].variable, "visible");
    for (size_t i = 0; i < machine->type_obligation_len; i++) {
        PettaTypeObligation *o = &machine->type_obligations[i];
        gc_probe_visit_atom(o->value, "type-obligations");
        gc_probe_visit_atom(o->formal, "type-obligations");
        gc_probe_visit_atom(o->checked_value, "type-obligations");
        gc_probe_visit_atom(o->checked_formal, "type-obligations");
    }
    if (machine->delay) {
        if (!cetta_delay_visit_live(machine->delay, gc_probe_delay_goal, NULL) ||
            !cetta_delay_visit_owned_roots(machine->delay, gc_probe_delay_atom, NULL))
            gc_probe_opaque("delay-visitor-failure");
    }
    for (size_t i = 0; i < machine->choice_len; i++) {
        const PettaChoice *c = &machine->choices[i];
        switch (c->kind) {
        case PETTA_CHOICE_EQUATION:
            gc_probe_visit_atom(c->as.equation.query, "choices");
            gc_probe_visit_atom(c->as.equation.query_head, "choices");
            gc_probe_visit_atom(c->as.equation.query_source, "choices");
            gc_probe_visit_atom(c->as.equation.expected, "choices");
            for (CettaExprIndex j = 0; j < c->as.equation.query_arity; j++)
                gc_probe_visit_atom(c->as.equation.query_values[j].skeleton, "choices");
            break;
        case PETTA_CHOICE_MATCH:
            gc_probe_visit_space(c->as.match.space, "spaces");
            gc_probe_visit_atom(c->as.match.pattern, "choices");
            gc_probe_visit_atom(c->as.match.resolved_pattern, "choices");
            gc_probe_visit_atom(c->as.match.template, "choices");
            gc_probe_visit_atom(c->as.match.expected, "choices");
            for (CettaCount j = 0; j < c->as.match.snapshot_len; j++)
                gc_probe_visit_atom(c->as.match.snapshot[j], "choices");
            for (CettaCount j = 0; j < c->as.match.binding_snapshot.len; j++)
                gc_probe_visit_bindings(&c->as.match.binding_snapshot.items[j], "choices");
            break;
        case PETTA_CHOICE_SUPERPOSE:
            gc_probe_visit_atom(c->as.superpose.items, "choices");
            gc_probe_visit_atom(c->as.superpose.expected, "choices");
            break;
        case PETTA_CHOICE_ANSWERS:
            gc_probe_visit_atom(c->as.answers.expected, "choices");
            if (c->as.answers.open) gc_probe_opaque("open-cursor");
            else gc_probe_visit_prepared(c->as.answers.cursor);
            break;
        case PETTA_CHOICE_ONCE: break;
        default: gc_probe_opaque("unreviewed-choice"); break;
        }
    }
    /* The prototype does not infer that AtomId encodings, trail history,
     * table producers, host caches or other threads are absent from C. */
    gc_probe_shadow_end();
}
