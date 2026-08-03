#include "parser.h"
#include "petta_search_machine.h"
#include "symbol.h"
#include "variant_shape.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void test_private_variant_summary(Arena *arena) {
    Atom *ordinary = atom_var_with_id(
        arena, "ordinary", fresh_var_id());
    Atom *private_slot = atom_var_with_id(
        arena, "$_slot", variant_shape_slot_id(7u));
    Atom *plain_items[] = {
        atom_symbol(arena, "plain"),
        ordinary,
    };
    Atom *private_items[] = {
        atom_symbol(arena, "nested"),
        private_slot,
    };
    Atom *plain = atom_expr(arena, plain_items, 2u);
    Atom *nested_private =
        atom_expr(arena, private_items, 2u);
    assert(ordinary && private_slot && plain && nested_private);
    assert(!atom_has_private_variant_vars(ordinary));
    assert(!atom_has_private_variant_vars(plain));
    assert(atom_has_private_variant_vars(private_slot));
    assert(atom_has_private_variant_vars(nested_private));

    Bindings clean;
    Bindings private_value;
    bindings_init(&clean);
    bindings_init(&private_value);
    assert(bindings_add_id(
        &clean, fresh_var_id(), SYMBOL_ID_NONE, plain));
    assert(bindings_add_id(
        &private_value, fresh_var_id(), SYMBOL_ID_NONE,
        nested_private));
    assert(!bindings_contains_private_variant_slots(&clean));
    assert(bindings_contains_private_variant_slots(&private_value));
    bindings_free(&clean);
    bindings_free(&private_value);
}

static void add_clause(Space *space, Arena *arena, const char *source) {
    Atom *clause = parse_one(arena, source);
    assert(clause);
    space_add(space, clause);
}

static void expect_answers(
    Space *space, Arena *arena, const char *query_source,
    const char *const *expected_sources, size_t expected_count) {
    Atom *query = parse_one(arena, query_source);
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, arena, query, NULL, NULL));

    for (size_t index = 0u; index < expected_count; index++) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step =
            petta_machine_next(&machine, &answer, &environment);
        if (step != PETTA_MACHINE_STEP_ANSWER) {
            fprintf(stderr,
                    "PeTTa machine stopped at step %d while solving ",
                    (int)step);
            atom_print(query, stderr);
            fputc('\n', stderr);
            abort();
        }
        Atom *expected = parse_one(arena, expected_sources[index]);
        assert(expected);
        if (!atom_alpha_eq(answer, expected)) {
            fputs("unexpected PeTTa machine answer: ", stderr);
            atom_print(answer, stderr);
            fputs(" expected ", stderr);
            atom_print(expected, stderr);
            fputc('\n', stderr);
            abort();
        }
        bindings_free(&environment);
    }

    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.answers == expected_count);
    assert(stats.transitions >= expected_count);
    petta_machine_destroy(&machine);
}

typedef struct {
    size_t remaining;
} TransitionPermit;

typedef struct {
    SymbolId head;
    size_t calls;
    uint32_t observed_entries;
} HostProjectionProbe;

static PettaMachineHostMode host_projection_classify(
    void *context, Space *space, Atom *expression) {
    (void)space;
    HostProjectionProbe *probe = context;
    if (!probe || !expression ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len != 2u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL ||
        expression->expr.elems[0]->sym_id != probe->head) {
        return PETTA_MACHINE_HOST_NONE;
    }
    return PETTA_MACHINE_HOST_READY_APPLICATION;
}

static bool host_projection_evaluate(
    void *context, Space *space, Arena *arena, Atom *expression,
    const Bindings *environment, OutcomeSet *outcomes) {
    (void)space;
    HostProjectionProbe *probe = context;
    if (!probe || !arena || !expression || !environment ||
        !outcomes || expression->kind != ATOM_EXPR ||
        expression->expr.len != 2u) {
        return false;
    }
    probe->calls++;
    probe->observed_entries = environment->len;
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(
        outcomes, expression->expr.elems[1], &empty);
    return true;
}

static void test_host_environment_projection(
    Space *space, Arena *arena) {
    HostProjectionProbe probe = {
        .head = symbol_intern_cstr(
            g_symbols, "host-projection-probe"),
    };
    PettaMachineHost host = {
        .context = &probe,
        .classify = host_projection_classify,
        .evaluate = host_projection_evaluate,
    };

    Atom *live = atom_var_with_id(
        arena, "live-host", fresh_var_id());
    Atom *kept = atom_symbol(arena, "kept-host-value");
    Atom *head = atom_symbol_id(arena, probe.head);
    Atom *query_items[] = {head, live};
    Atom *query = atom_expr(arena, query_items, 2u);
    assert(live && kept && head && query);

    Bindings base;
    bindings_init(&base);
    assert(bindings_add_var(&base, live, kept));
    enum { DEAD_BINDING_COUNT = 16 };
    for (size_t index = 0u;
         index < DEAD_BINDING_COUNT; index++) {
        Atom *dead = atom_var_with_id(
            arena, "dead-host", fresh_var_id());
        assert(dead);
        assert(bindings_add_var(&base, dead, kept));
    }

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, arena, query, &base, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, kept));
    bindings_free(&environment);
    assert(probe.calls == 1u);
    assert(probe.observed_entries == 0u);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.host_environment_entries_observed ==
           DEAD_BINDING_COUNT + 1u);
    assert(stats.host_environment_entries_forwarded == 0u);
    assert(stats.maximum_host_environment_entries_forwarded == 0u);
    assert(stats.singleton_outcome_choices_elided == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    bindings_free(&base);
}

static bool permit_transition(void *context) {
    TransitionPermit *permit = context;
    if (!permit || permit->remaining == 0u)
        return false;
    permit->remaining--;
    return true;
}

typedef struct {
    SymbolId heads[8];
    CettaExprLen arities[8];
    size_t relation_len;
    size_t remaining;
    bool meter_transitions;
} TableProbe;

static bool table_probe_contains(
    void *context, SymbolId head, CettaExprLen arity) {
    TableProbe *probe = context;
    if (!probe)
        return false;
    for (size_t index = 0u;
         index < probe->relation_len; index++) {
        if (probe->heads[index] == head &&
            probe->arities[index] == arity) {
            return true;
        }
    }
    return false;
}

static bool table_probe_permit(void *context) {
    TableProbe *probe = context;
    if (!probe || !probe->meter_transitions)
        return true;
    if (probe->remaining == 0u)
        return false;
    probe->remaining--;
    return true;
}

static void test_ground_slg_tables(
    Space *space, Arena *persistent, Arena *answers) {
    TableProbe probe = {
        .heads = {
            symbol_intern_cstr(g_symbols, "f"),
            symbol_intern_cstr(g_symbols, "table-loop"),
            symbol_intern_cstr(g_symbols, "table-left"),
            symbol_intern_cstr(g_symbols, "table-right"),
            symbol_intern_cstr(g_symbols, "table-dag"),
        },
        .arities = {1u, 1u, 1u, 1u, 1u},
        .relation_len = 5u,
    };
    PettaMachineHost host = {
        .context = &probe,
        .permit_transition = table_probe_permit,
        .tabled_relation_contains = table_probe_contains,
    };

    Atom *query = parse_one(answers, "(f 1)");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    const char *expected[] = {"one", "uno", "uno"};
    for (size_t index = 0u; index < 3u; index++) {
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer, parse_one(answers, expected[index])));
        bindings_free(&environment);
    }
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_lookups == 1u);
    assert(stats.table_generator_rounds == 1u);
    assert(stats.table_scc_completions == 1u);
    assert(stats.table_answer_replays == 3u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-alpha-pair)"
        "   (superpose ((f $left) (f $right))))");
    query = parse_one(answers, "(table-alpha-pair)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    const char *alpha_expected[] = {
        "one", "uno", "uno", "one", "uno", "uno",
    };
    for (size_t index = 0u; index < 6u; index++) {
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer,
            parse_one(answers, alpha_expected[index])));
        bindings_free(&environment);
    }
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_lookups == 2u);
    assert(stats.table_hits == 1u);
    assert(stats.table_generator_rounds == 1u);
    assert(stats.table_answer_replays == 6u);
    petta_machine_destroy(&machine);

    probe.meter_transitions = true;
    probe.remaining = 2u;
    query = parse_one(answers, "(f 1)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    probe.remaining = 1024u;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "one")));
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    probe.meter_transitions = false;

    add_clause(
        space, persistent,
        "(= (table-loop $x) (table-loop $x))");
    query = parse_one(answers, "(table-loop a)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds == 2u);
    assert(stats.table_scc_completions == 1u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-left $x) (table-right $x))");
    add_clause(
        space, persistent,
        "(= (table-right $x) (table-left $x))");
    query = parse_one(answers, "(table-left a)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds >= 4u);
    assert(stats.table_scc_completions == 1u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-dag root)"
        "   (superpose ((table-dag left)"
        "               (table-dag right))))");
    add_clause(
        space, persistent,
        "(= (table-dag left) (table-dag leaf))");
    add_clause(
        space, persistent,
        "(= (table-dag right) (table-dag leaf))");
    add_clause(
        space, persistent,
        "(= (table-dag leaf) done)");
    query = parse_one(answers, "(table-dag root)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    for (size_t index = 0u; index < 2u; index++) {
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer, parse_one(answers, "done")));
        bindings_free(&environment);
    }
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds == 4u);
    assert(stats.table_hits >= 1u);
    assert(stats.table_answer_replays >= 5u);
    petta_machine_destroy(&machine);

    query = parse_one(answers, "(f 1)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    add_clause(
        space, persistent, "(= (unrelated) mutation)");
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_INVALIDATED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

typedef struct {
    size_t remaining;
    size_t begins;
    size_t commits;
    size_t rollbacks;
} TransactionProbe;

static bool transaction_probe_permit(void *context) {
    TransactionProbe *probe = context;
    if (!probe || probe->remaining == 0u)
        return false;
    probe->remaining--;
    return true;
}

static bool transaction_probe_begin(
    void *context, Space *space, Arena *arena,
    void **transaction, Space **transaction_space) {
    (void)arena;
    TransactionProbe *probe = context;
    if (!probe || !space || !transaction ||
        !transaction_space) {
        return false;
    }
    probe->begins++;
    *transaction = probe;
    *transaction_space = space;
    return true;
}

static bool transaction_probe_commit(
    void *context, void *transaction) {
    TransactionProbe *probe = context;
    if (!probe || transaction != probe)
        return false;
    probe->commits++;
    return true;
}

static void transaction_probe_rollback(
    void *context, void *transaction) {
    TransactionProbe *probe = context;
    assert(probe && transaction == probe);
    probe->rollbacks++;
}

static void test_reachable_binding_projection(Arena *arena) {
    Atom *live = atom_var_with_id(
        arena, "live", fresh_var_id());
    Atom *middle = atom_var_with_id(
        arena, "middle", fresh_var_id());
    Atom *dead = atom_var_with_id(
        arena, "dead", fresh_var_id());
    Atom *kept_value = atom_symbol(arena, "kept");
    Atom *dead_value = atom_symbol(arena, "discarded");
    Atom *wrapper_items[] = {
        atom_symbol(arena, "wrapper"), middle,
    };
    Atom *wrapper = atom_expr(arena, wrapper_items, 2u);
    assert(live && middle && dead && kept_value &&
           dead_value && wrapper);

    Bindings full;
    bindings_init(&full);
    assert(bindings_add_var(&full, live, wrapper));
    assert(bindings_add_var(&full, middle, kept_value));
    assert(bindings_add_var(&full, dead, dead_value));

    Atom *roots[] = {live};
    Bindings projected;
    assert(bindings_project_reachable(
        &full, roots, 1u, &projected));
    assert(projected.len == 2u);
    assert(bindings_lookup_id(&projected, live->var_id));
    assert(bindings_lookup_id(&projected, middle->var_id));
    assert(!bindings_lookup_id(&projected, dead->var_id));
    bindings_free(&projected);

    Atom *ground_roots[] = {kept_value};
    assert(bindings_project_reachable(
        &full, ground_roots, 1u, &projected));
    assert(projected.len == 0u);
    assert(projected.eq_len == 0u);
    bindings_free(&projected);
    bindings_free(&full);

    /*
     * A closure over two variables must not copy a large unrelated logical
     * history.  The projected entries also retain their authoritative
     * relative order even when reachability discovers the later entry
     * transitively.
     */
    Bindings large;
    bindings_init(&large);
    Atom *large_live = atom_var_with_id(
        arena, "large-live", fresh_var_id());
    Atom *large_middle = atom_var_with_id(
        arena, "large-middle", fresh_var_id());
    assert(large_live && large_middle);
    assert(bindings_add_var(
        &large, large_live, large_middle));
    enum { UNRELATED_BINDING_COUNT = 256 };
    for (size_t index = 0u;
         index < UNRELATED_BINDING_COUNT; index++) {
        Atom *unrelated = atom_var_with_id(
            arena, "large-unrelated", fresh_var_id());
        assert(unrelated);
        assert(bindings_add_var(
            &large, unrelated, dead_value));
    }
    assert(bindings_add_var(
        &large, large_middle, kept_value));
    Atom *large_roots[] = {large_live};
    assert(bindings_project_reachable(
        &large, large_roots, 1u, &projected));
    assert(projected.len == 2u);
    assert(projected.entries[0].var_id ==
           large_live->var_id);
    assert(projected.entries[1].var_id ==
           large_middle->var_id);
    assert(bindings_lookup_id(
        &projected, large_live->var_id) == large_middle);
    assert(bindings_lookup_id(
        &projected, large_middle->var_id) == kept_value);
    bindings_free(&projected);
    bindings_free(&large);

    Atom *left_var = atom_var_with_id(
        arena, "left", fresh_var_id());
    Atom *right_var = atom_var_with_id(
        arena, "right", fresh_var_id());
    Atom *left_items[] = {
        atom_symbol(arena, "left-shape"), left_var,
    };
    Atom *right_items[] = {
        atom_symbol(arena, "right-shape"), right_var,
    };
    Atom *left = atom_expr(arena, left_items, 2u);
    Atom *right = atom_expr(arena, right_items, 2u);
    assert(left_var && right_var && left && right);

    Bindings constrained;
    bindings_init(&constrained);
    assert(bindings_add_constraint(&constrained, left, right));
    assert(constrained.eq_len == 1u);
    assert(bindings_add_var(
        &constrained, right_var, kept_value));
    Atom *constraint_roots[] = {left_var};
    assert(bindings_project_reachable(
        &constrained, constraint_roots, 1u, &projected));
    assert(projected.eq_len == 1u);
    assert(bindings_lookup_id(
        &projected, right_var->var_id));
    bindings_free(&projected);

    assert(bindings_project_reachable(
        &constrained, ground_roots, 1u, &projected));
    assert(projected.eq_len == 0u);
    assert(projected.len == 0u);
    bindings_free(&projected);
    bindings_free(&constrained);
}

static void test_deterministic_clause_elision(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (only-clause $x) $x)");
    Atom *query = parse_one(answers, "(only-clause token)");
    assert(query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "token")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.deterministic_clause_choices_elided == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_cons_shape_clause_index(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (shape-class ()) shape-empty)");
    add_clause(
        space, persistent,
        "(= (shape-class (cons $head $tail)) shape-nonempty)");

    /*
     * A rigid empty input proves that the cons clause is impossible.  It is
     * removed before selection, so the base clause is a WAM `trust` case
     * and no continuation prefix is retained.
     */
    Atom *empty_query = parse_one(
        answers, "(shape-class ())");
    assert(empty_query);
    PettaMachine empty_machine;
    assert(petta_machine_init(
        &empty_machine, space, answers,
        empty_query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &empty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-empty")));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&empty_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.maximum_choice_continuation_trail == 0u);
    assert(petta_machine_next(
               &empty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&empty_machine);

    /*
     * Conversely, a closed non-empty flat list cannot match the rigid empty
     * argument.  Literal indexing removes the first clause, leaving the cons
     * clause as a WAM `trust` case.
     */
    Atom *nonempty_query = parse_one(
        answers, "(shape-class (item))");
    assert(nonempty_query);
    PettaMachine nonempty_machine;
    assert(petta_machine_init(
        &nonempty_machine, space, answers,
        nonempty_query, NULL, NULL));
    assert(petta_machine_next(
               &nonempty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-nonempty")));
    bindings_free(&environment);
    assert(petta_machine_stats(&nonempty_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.maximum_choice_continuation_trail == 0u);
    assert(petta_machine_next(
               &nonempty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&nonempty_machine);

    /*
     * An unbound input proves nothing.  Both declaration-ordered clauses
     * must remain observable, demonstrating that the accelerator cannot
     * specialize a relational query from its first answer.
     */
    Atom *open_query = parse_one(
        answers, "(shape-class $items)");
    assert(open_query);
    PettaMachine open_machine;
    assert(petta_machine_init(
        &open_machine, space, answers,
        open_query, NULL, NULL));
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-empty")));
    bindings_free(&environment);
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-nonempty")));
    bindings_free(&environment);
    assert(petta_machine_stats(&open_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 0u);
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&open_machine);
}

static void test_choice_continuation_trail(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (trail-pick) (superpose (trail-a trail-b)))");
    add_clause(
        space, persistent,
        "(= (trail-accept trail-b) trail-ok)");
    Atom *query = parse_one(
        answers,
        "(chain (trail-pick) $x (trail-accept $x))");
    assert(query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "trail-ok")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.choice_continuation_snapshots > 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(stats.choice_continuation_items_trailed > 0u);
    assert(stats.maximum_choice_continuation_trail > 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_deterministic_heap_collection(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (drain ()) done)");
    add_clause(
        space, persistent,
        "(= (drain (cons $head $tail))"
        "   (drain $tail))");

    enum {
        ITEM_COUNT = 8192,
        BOUNDED_BINDING_COLLECTION_WINDOW = 4096,
    };
    Atom **items = cetta_malloc(
        ITEM_COUNT * sizeof(*items));
    Atom *item = atom_symbol(answers, "item");
    assert(item);
    for (size_t i = 0u; i < ITEM_COUNT; i++)
        items[i] = item;
    Atom *list = atom_expr(answers, items, ITEM_COUNT);
    free(items);
    Atom *query_items[] = {
        atom_symbol(answers, "drain"), list,
    };
    Atom *query = atom_expr(answers, query_items, 2u);
    assert(list && query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "done")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.deterministic_heap_collections > 0u);
    assert(stats.deterministic_minor_heap_collections > 0u);
    assert(stats.deterministic_heap_bytes_promoted > 0u);
    assert(stats.deterministic_heap_bytes_reclaimed > 0u);
    assert(stats.deterministic_binding_entries_discarded > 0u);
    assert(stats.maximum_nursery_live_bytes > 0u);
    assert(stats.maximum_tenured_live_bytes > 0u);
    /*
     * The two-clause relation retains one semantically live choice point per
     * input cell until its first answer.  Binding storage may therefore grow
     * linearly with ITEM_COUNT, plus at most one bounded collection window,
     * but not with the quadratic total size of all list suffixes.
     */
    assert(
        stats.maximum_binding_entries <=
        (size_t)ITEM_COUNT * 2u +
            BOUNDED_BINDING_COLLECTION_WINDOW);

    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_choice_binding_compaction(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (choose-drain $items) (drain $items))");
    add_clause(
        space, persistent,
        "(= (choose-drain $items) fallback)");

    enum {
        ITEM_COUNT = 4096,
        BOUNDED_BINDING_COLLECTION_WINDOW = 4096,
    };
    Atom **items = cetta_malloc(
        ITEM_COUNT * sizeof(*items));
    Atom *item = atom_symbol(answers, "choice-item");
    assert(item);
    for (size_t i = 0u; i < ITEM_COUNT; i++)
        items[i] = item;
    Atom *list = atom_expr(answers, items, ITEM_COUNT);
    free(items);
    Atom *query_items[] = {
        atom_symbol(answers, "choose-drain"), list,
    };
    Atom *query = atom_expr(answers, query_items, 2u);
    assert(list && query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "done")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.choice_binding_collections > 0u);
    assert(stats.choice_binding_items_discarded > 0u);
    assert(stats.choice_trail_entries_discarded > 0u);
    assert(
        stats.maximum_binding_entries <=
        (size_t)ITEM_COUNT * 2u +
            BOUNDED_BINDING_COLLECTION_WINDOW);

    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "fallback")));
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_choice_heap_instant_reclaiming(
    Space *space, Arena *persistent, Arena *answers) {
    enum {
        ROW_COUNT = 512,
        VARIABLE_ROW_COUNT = 32,
        UNRELATED_COUNT = 1024,
    };
    Atom *head = atom_symbol(persistent, "heap-row");
    Atom *unrelated_head =
        atom_symbol(persistent, "unrelated-heap-row");
    assert(head);
    assert(unrelated_head);
    for (int64_t index = 0; index < ROW_COUNT; index++) {
        Atom *items[] = {
            head,
            atom_int(persistent, index),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    for (int64_t index = 0; index < VARIABLE_ROW_COUNT; index++) {
        Atom *items[] = {
            head,
            atom_var(persistent, "stored-heap-value"),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    for (int64_t index = 0; index < UNRELATED_COUNT; index++) {
        Atom *items[] = {
            unrelated_head,
            atom_int(persistent, index),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    {
        Atom *duplicate_items[] = {
            head,
            atom_int(persistent, 7),
        };
        Atom *duplicate =
            atom_expr(persistent, duplicate_items, 2u);
        assert(duplicate);
        space_add(space, duplicate);
    }
    add_clause(
        space, persistent,
        "(not-heap-row ignored)");

    Atom *query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self (heap-row $x) $x)))");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(
        answer->ground.ival ==
        ROW_COUNT + VARIABLE_ROW_COUNT + 1);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(
        stats.count_aggregate_answers ==
        ROW_COUNT + VARIABLE_ROW_COUNT + 1u);
    assert(
        stats.match_candidates <
        (uint64_t)(ROW_COUNT + UNRELATED_COUNT));
    assert(
        stats.choice_heap_resets >=
        ROW_COUNT + VARIABLE_ROW_COUNT);
    assert(stats.choice_heap_bytes_reclaimed > 0u);
    petta_machine_destroy(&machine);
}

static void test_terminal_match_count_fold(
    Space *space, Arena *answers) {
    enum {
        EXPECTED_ROWS = 512 + 32 + 1,
    };
    PettaPlanNode template_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
    };
    PettaPlanNode template_plan = {
        .role = PETTA_PLAN_DATA,
        .child_count = 2u,
        .children = template_children,
    };
    PettaPlanNode match_children[4] = {
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
        template_plan,
    };
    PettaPlanNode match_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 4u,
        .children = match_children,
    };
    PettaPlanNode collapse_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        match_plan,
    };
    PettaPlanNode collapse_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 2u,
        .children = collapse_children,
    };
    PettaPlanNode length_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        collapse_plan,
    };
    PettaPlanNode length_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 2u,
        .children = length_children,
    };

    Atom *query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self"
        "      (heap-row $x)"
        "      (heap-row $x))))");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == EXPECTED_ROWS);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 1u);
    assert(
        stats.count_aggregate_match_answers ==
        EXPECTED_ROWS);
    assert(stats.count_aggregate_answers == EXPECTED_ROWS);
    petta_machine_destroy(&machine);

    add_clause(
        space, answers,
        "(dynamic-count-left unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-left unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-right unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-right other-count-marker unique-count-tail)");
    match_children[3].role = PETTA_PLAN_DYNAMIC_CALL;
    query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self"
        "      ($relation unique-count-marker unique-count-tail)"
        "      ($relation unique-count-marker unique-count-tail))))");
    assert(query);
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == 3);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 1u);
    assert(stats.count_aggregate_match_answers == 3u);
    assert(stats.count_aggregate_answers == 3u);
    petta_machine_destroy(&machine);

    PettaPlanNode empty_children[1] = {
        {.role = PETTA_PLAN_VALUE},
    };
    PettaPlanNode empty_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 1u,
        .children = empty_children,
    };
    match_children[3] = empty_plan;
    query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self (heap-row $x) (empty))))");
    assert(query);
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == 0);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 0u);
    assert(stats.count_aggregate_match_answers == 0u);
    petta_machine_destroy(&machine);
}

int main(void) {
    Arena persistent;
    Arena answers;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;

    arena_init(&persistent);
    arena_set_runtime_kind(
        &persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&answers);
    arena_set_runtime_kind(
        &answers, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    space_init_with_universe(&space, &universe);

    test_private_variant_summary(&answers);
    test_reachable_binding_projection(&answers);
    test_host_environment_projection(&space, &answers);
    test_deterministic_clause_elision(
        &space, &persistent, &answers);
    test_cons_shape_clause_index(
        &space, &persistent, &answers);
    test_choice_continuation_trail(
        &space, &persistent, &answers);
    test_deterministic_heap_collection(
        &space, &persistent, &answers);
    test_choice_binding_compaction(
        &space, &persistent, &answers);

    add_clause(&space, &persistent, "(= (f 1) one)");
    add_clause(&space, &persistent, "(= (f 1) uno)");
    add_clause(&space, &persistent, "(= (f 1) uno)");
    test_ground_slg_tables(
        &space, &persistent, &answers);
    const char *ordered[] = {"one", "uno", "uno"};
    expect_answers(&space, &answers, "(f 1)", ordered, 3u);

    add_clause(
        &space, &persistent,
        "(= (second ($first $second $third)) $second)");
    const char *patterned[] = {"b"};
    expect_answers(
        &space, &answers, "(second (a b c))", patterned, 1u);

    add_clause(
        &space, &persistent,
        "(= (cons-second (cons $first (cons $second $rest)))"
        "   $second)");
    expect_answers(
        &space, &answers, "(cons-second (a b c))",
        patterned, 1u);
    expect_answers(
        &space, &answers, "(cons-second ())", NULL, 0u);

    add_clause(&space, &persistent, "(= (identity $x) $x)");
    const char *inverse[] = {"a"};
    expect_answers(
        &space, &answers,
        "(let (identity $x) a $x)", inverse, 1u);

    const char *appended[] = {"(a b c)"};
    expect_answers(
        &space, &answers, "(append (a) (b c))",
        appended, 1u);
    const char *inverse_append[] = {"(a (b c))"};
    expect_answers(
        &space, &answers,
        "(let (append ($head) $tail)"
        "     (a b c)"
        "     ($head $tail))",
        inverse_append, 1u);
    expect_answers(
        &space, &answers,
        "(let (append (a) $tail)"
        "     (b c)"
        "     $tail)",
        NULL, 0u);

    const char *sum_forward[] = {"5"};
    expect_answers(
        &space, &answers, "(#+ 2 3)", sum_forward, 1u);
    const char *sum_inverse_left[] = {"7"};
    expect_answers(
        &space, &answers,
        "(let (#+ $left 35) 42 $left)",
        sum_inverse_left, 1u);
    const char *sum_inverse_right[] = {"35"};
    expect_answers(
        &space, &answers,
        "(let (#+ 7 $right) 42 $right)",
        sum_inverse_right, 1u);
    expect_answers(
        &space, &answers,
        "(let (#+ 7 35) 41 impossible)",
        NULL, 0u);
    expect_answers(
        &space, &answers,
        "(#+ 9223372036854775807 1)",
        NULL, 0u);

    const char *equal_true_false[] = {"true", "false"};
    expect_answers(
        &space, &answers,
        "(= 1 (superpose (1 2)))",
        equal_true_false, 2u);
    const char *equal_open[] = {"true", "true"};
    expect_answers(
        &space, &answers,
        "(= $item (superpose (1 2)))",
        equal_open, 2u);
    add_clause(
        &space, &persistent,
        "(= (list-product $tail)"
        "   (append (append (42) (10)) $tail))");
    const char *equal_inverse[] = {"(true (40))"};
    expect_answers(
        &space, &answers,
        "((= (42 10 40) (list-product $tail)) $tail)",
        equal_inverse, 1u);

    add_clause(
        &space, &persistent,
        "(= (pair2 $left $right) ($left $right))");
    const char *partial_pair[] = {"(partial pair2 (a))"};
    expect_answers(
        &space, &answers,
        "(pair2 a)", partial_pair, 1u);
    const char *applied_pair[] = {"(a b)"};
    expect_answers(
        &space, &answers,
        "((pair2 a) b)", applied_pair, 1u);
    const char *partial_intrinsic[] = {"(partial + (1))"};
    expect_answers(
        &space, &answers,
        "(+ 1)", partial_intrinsic, 1u);
    add_clause(
        &space, &persistent,
        "(= (overloaded one) exact)");
    add_clause(
        &space, &persistent,
        "(= (overloaded one two) larger)");
    const char *exact_precedes_partial[] = {"exact"};
    expect_answers(
        &space, &answers,
        "(overloaded one)", exact_precedes_partial, 1u);
    add_clause(
        &space, &persistent,
        "(= (returns-add) (#+))");
    const char *extended_callable[] = {"5"};
    expect_answers(
        &space, &answers,
        "(returns-add 2 3)", extended_callable, 1u);
    add_clause(
        &space, &persistent,
        "(= (returns-data) 1)");
    const char *overapplied_data[] = {
        "(partial returns-data (extra))",
    };
    expect_answers(
        &space, &answers,
        "(returns-data extra)", overapplied_data, 1u);
    add_clause(
        &space, &persistent,
        "(= (mixed-arity $x) one)");
    add_clause(
        &space, &persistent,
        "(= (mixed-arity $x $y $z) three)");
    const char *gap_stays_partial[] = {
        "(partial mixed-arity (a b))",
    };
    expect_answers(
        &space, &answers,
        "(mixed-arity a b)", gap_stays_partial, 1u);
    add_clause(
        &space, &persistent,
        "(= (select-callable special) (#+ 1))");
    const char *selected_extension[] = {"3"};
    expect_answers(
        &space, &answers,
        "(select-callable special 2)",
        selected_extension, 1u);
    expect_answers(
        &space, &answers,
        "(select-callable other 2)", NULL, 0u);
    add_clause(
        &space, &persistent,
        "(: typed-map (-> Expression %Undefined%))");
    add_clause(
        &space, &persistent,
        "(= (typed-map ($f ())) ())");
    add_clause(
        &space, &persistent,
        "(= (typed-map ($f (cons $x $xs)))"
        "   (cons ($f $x) (typed-map ($f $xs))))");
    add_clause(
        &space, &persistent,
        "(= (increment $x) (#+ $x 1))");
    const char *typed_relational_head[] = {"(2 3)"};
    expect_answers(
        &space, &answers,
        "(typed-map (increment (1 2)))",
        typed_relational_head, 1u);
    expect_answers(
        &space, &answers,
        "(typed-map (increment not-a-list))", NULL, 0u);
    const char *computed_head_data[] = {"((1 2) 3)"};
    expect_answers(
        &space, &answers,
        "((1 2) 3)", computed_head_data, 1u);

    add_clause(&space, &persistent, "(= (successor b a) True)");
    add_clause(&space, &persistent, "(= (successor c b) True)");
    add_clause(
        &space, &persistent,
        "(= (later $x $y) (successor $x $y))");
    add_clause(
        &space, &persistent,
        "(= (later $x $y)"
        "   (and (successor $x $middle)"
        "        (later $middle $y)))");
    const char *successor_truth[] = {"True"};
    expect_answers(
        &space, &answers,
        "(successor c $answer)", successor_truth, 1u);
    const char *recursive_truths[] = {"True", "true"};
    expect_answers(
        &space, &answers,
        "(later c $answer)", recursive_truths, 2u);
    const char *recursive[] = {"(True b)", "(true a)"};
    expect_answers(
        &space, &answers,
        "((later c $answer) $answer)", recursive, 2u);

    add_clause(
        &space, &persistent,
        "(= (choose) (superpose (first second)))");
    const char *committed[] = {"first"};
    expect_answers(
        &space, &answers,
        "(let $answer (choose)"
        "  (let $_ (cut) $answer))",
        committed, 1u);

    add_clause(&space, &persistent, "(a b)");
    add_clause(&space, &persistent, "(a c)");
    const char *matched[] = {"(a b)", "(a c)"};
    expect_answers(
        &space, &answers,
        "(match &self (a $x) (a $x))",
        matched, 2u);
    add_clause(
        &space, &persistent,
        "(= (first-a)"
        "   (let* (($answer (match &self (a $x) (a $x)))"
        "           ($_ (cut)))"
        "          $answer))");
    const char *matched_committed[] = {"(a b)"};
    expect_answers(
        &space, &answers, "(first-a)",
        matched_committed, 1u);

    Atom *suspended_query = parse_one(&answers, "(f 1)");
    assert(suspended_query);
    TransitionPermit permit = {.remaining = 1u};
    PettaMachineHost suspended_host = {
        .context = &permit,
        .permit_transition = permit_transition,
    };
    PettaMachine suspended;
    assert(petta_machine_init(
        &suspended, &space, &answers, suspended_query, NULL,
        &suspended_host));
    Atom *suspended_answer = NULL;
    Bindings suspended_environment;
    assert(petta_machine_next(
               &suspended, &suspended_answer,
               &suspended_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&suspended_environment);
    PettaMachineStats suspended_stats;
    assert(petta_machine_stats(&suspended, &suspended_stats));
    assert(suspended_stats.transitions == 1u);
    permit.remaining = 64u;
    assert(petta_machine_next(
               &suspended, &suspended_answer,
               &suspended_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        suspended_answer, parse_one(&answers, "one")));
    bindings_free(&suspended_environment);
    petta_machine_destroy(&suspended);

    TransitionPermit collapse_permit = {.remaining = 2u};
    PettaMachineHost collapse_host = {
        .context = &collapse_permit,
        .permit_transition = permit_transition,
    };
    Atom *collapse_query =
        parse_one(&answers, "(collapse (f 1))");
    assert(collapse_query);
    PettaMachine collapse_machine;
    assert(petta_machine_init(
        &collapse_machine, &space, &answers,
        collapse_query, NULL, &collapse_host));
    Atom *collapse_answer = NULL;
    Bindings collapse_environment;
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&collapse_environment);
    collapse_permit.remaining = 128u;
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        collapse_answer,
        parse_one(&answers, "(one uno uno)")));
    bindings_free(&collapse_environment);
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&collapse_environment);
    petta_machine_destroy(&collapse_machine);

    TransactionProbe transaction_probe = {
        .remaining = 1u,
    };
    PettaMachineHost transaction_host = {
        .context = &transaction_probe,
        .permit_transition = transaction_probe_permit,
        .transaction_begin = transaction_probe_begin,
        .transaction_commit = transaction_probe_commit,
        .transaction_rollback = transaction_probe_rollback,
    };
    Atom *transaction_query =
        parse_one(&answers, "(transaction (f 1))");
    assert(transaction_query);
    PettaMachine transaction_machine;
    assert(petta_machine_init(
        &transaction_machine, &space, &answers,
        transaction_query, NULL, &transaction_host));
    Atom *transaction_answer = NULL;
    Bindings transaction_environment;
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&transaction_environment);
    assert(transaction_probe.begins == 1u);
    assert(transaction_probe.commits == 0u);
    assert(transaction_probe.rollbacks == 0u);
    transaction_probe.remaining = 64u;
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        transaction_answer, parse_one(&answers, "one")));
    bindings_free(&transaction_environment);
    assert(transaction_probe.commits == 1u);
    assert(transaction_probe.rollbacks == 0u);
    petta_machine_destroy(&transaction_machine);

    transaction_probe.remaining = 64u;
    Atom *failed_transaction_query =
        parse_one(&answers, "(transaction (empty))");
    assert(failed_transaction_query);
    assert(petta_machine_init(
        &transaction_machine, &space, &answers,
        failed_transaction_query, NULL, &transaction_host));
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&transaction_environment);
    assert(transaction_probe.begins == 2u);
    assert(transaction_probe.commits == 1u);
    assert(transaction_probe.rollbacks == 1u);
    petta_machine_destroy(&transaction_machine);

    Atom *mutable_query = parse_one(&answers, "(f 1)");
    assert(mutable_query);
    PettaMachine logical_view;
    assert(petta_machine_init(
        &logical_view, &space, &answers, mutable_query, NULL, NULL));
    Atom *first_answer = NULL;
    Bindings first_environment;
    assert(petta_machine_next(
               &logical_view, &first_answer, &first_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        first_answer, parse_one(&answers, "one")));
    bindings_free(&first_environment);
    add_clause(&space, &persistent, "(= (f 1) after)");
    const char *remaining[] = {"uno", "uno"};
    for (size_t index = 0u; index < 2u; index++) {
        Bindings remaining_environment;
        assert(petta_machine_next(
                   &logical_view, &first_answer,
                   &remaining_environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            first_answer,
            parse_one(&answers, remaining[index])));
        bindings_free(&remaining_environment);
    }
    Bindings exhausted_environment;
    assert(petta_machine_next(
               &logical_view, &first_answer,
               &exhausted_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&exhausted_environment);
    petta_machine_destroy(&logical_view);

    const char *next_call[] = {"one", "uno", "uno", "after"};
    expect_answers(
        &space, &answers, "(f 1)", next_call, 4u);
    test_choice_heap_instant_reclaiming(
        &space, &persistent, &answers);
    test_terminal_match_count_fold(&space, &answers);

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS: explicit PeTTa search machine");
    return 0;
}
