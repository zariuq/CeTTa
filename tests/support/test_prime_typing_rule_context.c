#include "parser.h"
#include "prime_regular_kernel.h"
#include "prime_scoped_judgments.h"
#include "prime_semantics.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static unsigned failures;

static void expect(bool condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static Atom *add(Arena *arena, Space *space, const char *text) {
    Atom *form = parse_one(arena, text);
    expect(form != NULL, "authored declaration parses");
    if (form) space_add(space, form);
    return form;
}

static bool established(CettaPrimeTypingAuthorityObservationV1 authority) {
    return authority.result.kind == CETTA_NIK_RESULT_OUTCOME &&
           authority.result.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED;
}

static void check_observers(Arena *arena, Space *space, Atom *caller_rules,
                             bool should_establish) {
    CettaPrimeTypingCheckingCandidateV1 checking = {
        .term = parse_one(arena, "p"),
        .expected_type = parse_one(arena, "A"),
    };
    CettaPrimeTypingCheckingObservationV1 checked;
    bool returned = cetta_prime_typing_observe_checking_v1(
        arena, space, &checking, &checked);
    expect(returned, "checking produces an observation");
    expect(returned && established(checked.authority) == should_establish,
           "checking reads this space's admitted carrier equation");
    expect(cetta_prime_regular_kernel_rules_get() == caller_rules,
           "checking restores its caller's rule table");

    CettaPrimeTypingFormationCandidateV1 formation = {
        .type = parse_one(arena, "(-> A A)"),
    };
    CettaPrimeTypingFormationObservationV1 formed;
    returned = cetta_prime_typing_observe_formation_v1(
        arena, space, &formation, &formed);
    expect(returned, "formation produces an observation");
    expect(returned && established(formed.authority) == should_establish,
           "formation reads the admitted universe equation");
    expect(cetta_prime_regular_kernel_rules_get() == caller_rules,
           "formation restores its caller's rule table");

    CettaPrimeTypingSynthesisCandidateV1 synthesis = {
        .term = parse_one(arena, "(lam (y : A) y)"),
    };
    CettaPrimeTypingSynthesisObservationV1 synthesized;
    returned = cetta_prime_typing_observe_synthesis_v1(
        arena, space, &synthesis, &synthesized);
    expect(returned, "synthesis produces an observation");
    expect(returned && established(synthesized.authority) == should_establish,
           "synthesis checks domains with the admitted universe equation");
    expect(cetta_prime_regular_kernel_rules_get() == caller_rules,
           "synthesis restores its caller's rule table");
}

int main(void) {
    Arena arena;
    TermUniverse universe;
    SymbolTable symbols;
    VarInternTable variables;
    Space space, view;
    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    space_init_with_universe(&space, &universe);

    add(&arena, &space, "(: TypeAlias (u 1))");
    add(&arena, &space, "(: A TypeAlias)");
    add(&arena, &space, "(: BaseAlias (u 0))");
    add(&arena, &space, "(: p BaseAlias)");
    Atom *universe_rule = add(&arena, &space,
        "(type:rule TypeAlias 0 () (Sort (LevelConst 0)))");
    Atom *carrier_rule = add(&arena, &space,
        "(type:rule BaseAlias 0 () (DeclConst A))");
    /* The rule-shaped source is present before its publication is admitted.
     * An unrelated caller table cannot authorize that source equation. */
    Atom *caller_rules = parse_one(&arena,
        "(LCons (PrimeRule unrelated 0 () (Sort (LevelConst 1))) LNil)");
    cetta_prime_regular_kernel_rules_set(caller_rules);
    check_observers(&arena, &space, caller_rules, false);

    prime_scoped_judgment_admit(&arena, &space, universe_rule);
    prime_scoped_judgment_admit(&arena, &space, carrier_rule);
    check_observers(&arena, &space, caller_rules, true);

    space_init_overlay(&view, &space);
    check_observers(&arena, &view, caller_rules, true);
    space_remove(&view, universe_rule);
    check_observers(&arena, &view, caller_rules, false);
    check_observers(&arena, &space, caller_rules, true);
    space_free(&view);

    CettaPrimeTypingCheckingCandidateV1 limited = {
        .term = parse_one(&arena, "p"),
        .expected_type = parse_one(&arena, "A"),
        .steps_limited = true,
        .steps = 1u,
    };
    CettaPrimeTypingCheckingObservationV1 incomplete;
    bool returned = cetta_prime_typing_observe_checking_v1(
        &arena, &space, &limited, &incomplete);
    expect(returned && incomplete.authority.result.kind == CETTA_NIK_RESULT_OUTCOME &&
               incomplete.authority.result.value.outcome == CETTA_NIK_OUTCOME_INCOMPLETE,
           "resource exhaustion remains incomplete");
    expect(cetta_prime_regular_kernel_rules_get() == caller_rules,
           "incomplete checking restores its caller's rule table");
    limited.term = NULL;
    expect(!cetta_prime_typing_observe_checking_v1(
               &arena, &space, &limited, &incomplete),
           "invalid candidate is declined");
    expect(cetta_prime_regular_kernel_rules_get() == caller_rules,
           "invalid candidate preserves its caller's rule table");

    cetta_prime_regular_kernel_rules_set(NULL);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);
    if (failures) return 1;
    printf("Typing observation rule contexts: %u checks passed\n", checks);
    return 0;
}
