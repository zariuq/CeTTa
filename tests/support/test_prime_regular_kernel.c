#include "parser.h"
#include "gslt_horn_runtime.h"
#include "generated/prime_typing_open_regular_kernel_source_binding_v1.generated.h"
#include "prime_regular_kernel.h"
#include "prime_regular_kernel_admission.h"
#include "prime_typed_flow.h"
#include "prime_typed_flow_boundary.h"
#include "prime_semantics.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                             \
    do {                                                                    \
        checks++;                                                           \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static CettaPrimeRegularKernelResult synth(
    Arena *arena, const char *scoped_text, bool limited, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, limited, steps);
    Atom *scoped = parse_one(arena, scoped_text);
    CHECK(scoped != NULL, "synthesis fixture parses");
    return cetta_prime_regular_kernel_synth(arena, scoped, &budget);
}

static CettaPrimeRegularKernelResult check_term(
    Arena *arena, const char *scoped_text, const char *expected_text) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    Atom *scoped = parse_one(arena, scoped_text);
    Atom *expected = parse_one(arena, expected_text);
    CHECK(scoped && expected, "checking fixture parses");
    return cetta_prime_regular_kernel_check(arena, scoped, expected, &budget);
}

static CettaPrimeRegularKernelResult convert(
    Arena *arena, const char *left_text, const char *right_text) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    Atom *left = parse_one(arena, left_text);
    Atom *right = parse_one(arena, right_text);
    CHECK(left && right, "conversion fixture parses");
    return cetta_prime_regular_kernel_convert(arena, left, right, &budget);
}

static bool result_type_is(
    Arena *arena, const CettaPrimeRegularKernelResult *result,
    const char *expected_text) {
    Atom *expected = parse_one(arena, expected_text);
    return result && result->status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
           result->type && expected && atom_eq(result->type, expected);
}

static bool verdict_status(Atom *verdict, const char *status) {
    return verdict && verdict->kind == ATOM_EXPR &&
           verdict->expr.len == 4u &&
           atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
           atom_is_symbol(verdict->expr.elems[1], status);
}

static bool checking_route_is_regular(
    CettaPrimeTypingRouteV1 route) {
    return route == CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
}

static const char *checking_route_name(
    CettaPrimeTypingRouteV1 route) {
    switch (route) {
    case CETTA_PRIME_TYPING_ROUTE_NONE: return "none";
    case CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR:
        return "scoped-regular";
    case CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR:
        return "authored-regular";
    case CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR:
        return "declared-regular";
    case CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR:
        return "closed-regular";
    case CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION:
        return "ambient-formation";
    case CETTA_PRIME_TYPING_ROUTE_LEGACY_HE: return "legacy-he";
    }
    return "invalid";
}

static CettaGsltHornLimits horn_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 1000000u,
        .max_answers = 16u,
        .max_depth = 512u,
    };
}

static void check_horn_positive(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers,
    const char *query_text, const char *expected_text, const char *label) {
    Atom *query = parse_one(queries, query_text);
    Atom *expected = parse_one(queries, expected_text);
    CettaGsltHornResult result = {0};
    char error[1024] = {0};
    bool ran = query && expected && cetta_gslt_horn_query(
        program, answers, query, horn_limits(), &result,
        error, sizeof error);
    CHECK(ran, label);
    if (ran) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "lambda-pi Horn query completes within its semantic budget");
        bool exact = result.answer_count == 1u && result.answers[0] &&
                     atom_eq(result.answers[0], expected);
        CHECK(exact, "lambda-pi Horn query has one expected derivation");
        if (!exact) {
            fprintf(stderr, "%s: expected ", label);
            atom_print(expected, stderr);
            fprintf(stderr, "; answers=%zu", result.answer_count);
            for (size_t index = 0u; index < result.answer_count; index++) {
                fprintf(stderr, "\n  answer[%zu] = ", index);
                atom_print(result.answers[index], stderr);
            }
            fputc('\n', stderr);
        }
    } else {
        fprintf(stderr, "%s Horn diagnostic: %s\n", label, error);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_horn_negative(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers,
    const char *query_text, const char *label) {
    Atom *query = parse_one(queries, query_text);
    CettaGsltHornResult result = {0};
    char error[1024] = {0};
    bool ran = query && cetta_gslt_horn_query(
        program, answers, query, horn_limits(), &result,
        error, sizeof error);
    CHECK(ran, label);
    if (ran) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                  result.answer_count == 0u,
              "negative lambda-pi Horn query has no derivation");
    } else {
        fprintf(stderr, "%s Horn diagnostic: %s\n", label, error);
    }
    cetta_gslt_horn_result_free(&result);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(
            stderr,
            "usage: %s OPEN_LAMBDA_PI_LANGDEF OPEN_REGULAR_KERNEL_LANGDEF\n",
            argv[0]);
        return 2;
    }
    Arena arena;
    Arena queries;
    Arena answers;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;

    arena_init(&arena);
    arena_init(&queries);
    arena_init(&answers);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    CHECK(CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE,
          "production build has native Prime admission permanently active");

    CettaGsltHornProgram *program = NULL;
    char horn_error[1024] = {0};
    const char *paths[] = {argv[1]};
    bool loaded = cetta_gslt_horn_program_load_paths(
        paths, 1u, &program, horn_error, sizeof horn_error);
    CHECK(loaded, "open lambda-pi langdef loads in the generic oracle");
    if (!loaded)
        fprintf(stderr, "lambda-pi langdef load diagnostic: %s\n", horn_error);
    CHECK(program && cetta_gslt_horn_program_rule_count(program) == 92u,
          "all 92 ordered lambda-pi rules are executable");
    CettaGsltHornProgram *regular_program = NULL;
    char regular_horn_error[1024] = {0};
    const char *regular_paths[] = {argv[2]};
    bool regular_loaded = cetta_gslt_horn_program_load_paths(
        regular_paths, 1u, &regular_program,
        regular_horn_error, sizeof regular_horn_error);
    CHECK(regular_loaded,
          "open regular-kernel langdef loads in the generic oracle");
    if (!regular_loaded)
        fprintf(
            stderr, "regular-kernel langdef load diagnostic: %s\n",
            regular_horn_error);
    CHECK(
        regular_program &&
            cetta_gslt_horn_program_rule_count(regular_program) == 194u,
        "all 194 ordered regular-kernel rules are executable");
    const CettaNikDirectSourceBindingV1 *binding =
        &prime_typing_open_regular_kernel_source_binding_v1;
    CHECK(cetta_nik_direct_source_binding_v1_is_valid(binding) &&
              binding->authority == &cetta_prime_typing_direct_authority_v1 &&
              strcmp(binding->presentation_id,
                     "prime-open-regular-kernel-v1") == 0 &&
              strcmp(binding->semantic_scope,
                     "prime.typing.open-regular-kernel") == 0 &&
              binding->coverage ==
                  CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
          "open regular-kernel source is bound to the direct Prime authority");

    CettaPrimeRegularKernelResult open_variable = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0))",
        false, 0u);
    CHECK(result_type_is(&arena, &open_variable, "U0"),
          "open variable synthesizes its context type");

    Atom *deep_context = atom_symbol(&arena, "PrimeCtxNil");
    for (uint64_t index = 0u; index < 1024u; index++) {
        deep_context = atom_expr3(
            &arena, atom_symbol(&arena, "PrimeCtxCons"),
            atom_symbol(&arena, "U0"), deep_context);
    }
    Atom *deep_scoped = atom_expr3(
        &arena, atom_symbol(&arena, "PrimeScoped"), deep_context,
        atom_expr2(
            &arena, atom_symbol(&arena, "idx"), atom_int(&arena, 1023)));
    CettaPrimeRegularKernelBudget deep_budget;
    cetta_prime_regular_kernel_budget_init(&deep_budget, false, 0u);
    CettaPrimeRegularKernelResult deep_variable = cetta_prime_regular_kernel_synth(
        &arena, deep_scoped, &deep_budget);
    CHECK(result_type_is(&arena, &deep_variable, "U0"),
          "large open telescope is validated and searched linearly");

    CettaPrimeRegularKernelResult identity = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &identity, "(Pi U0 U0)"),
          "annotated identity synthesizes a dependent function type");

    Atom *identity_scoped = parse_one(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))");
    CettaPrimeRegularKernelBudget measured_budget;
    cetta_prime_regular_kernel_budget_init(&measured_budget, true, 1000000u);
    CettaPrimeRegularKernelResult measured_identity =
        cetta_prime_regular_kernel_synth(
            &arena, identity_scoped, &measured_budget);
    uint64_t identity_steps = measured_budget.spent;
    CHECK(measured_identity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              identity_steps > 0u,
          "bounded synthesis reports its exact structural work");
    CettaPrimeRegularKernelBudget exact_budget;
    cetta_prime_regular_kernel_budget_init(
        &exact_budget, true, identity_steps);
    CettaPrimeRegularKernelResult exact_identity = cetta_prime_regular_kernel_synth(
        &arena, identity_scoped, &exact_budget);
    CHECK(result_type_is(&arena, &exact_identity, "(Pi U0 U0)"),
          "the exact measured budget preserves an established verdict");
    CettaPrimeRegularKernelBudget short_budget;
    cetta_prime_regular_kernel_budget_init(
        &short_budget, true, identity_steps - 1u);
    CettaPrimeRegularKernelResult short_identity = cetta_prime_regular_kernel_synth(
        &arena, identity_scoped, &short_budget);
    CHECK(short_identity.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "one fewer structural step reports budget exhaustion, never refutation");

    CettaPrimeRegularKernelResult application = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (App (Lam U0 (idx 0)) (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &application, "U0"),
          "open application substitutes its result type");

    CettaPrimeRegularKernelResult pi_formation = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Pi U0 U0))", false, 0u);
    CHECK(result_type_is(&arena, &pi_formation, "U1"),
          "dependent function formation synthesizes the upper sort");

    CettaPrimeRegularKernelResult sigma_formation = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Sigma U0 U0))", false, 0u);
    CHECK(result_type_is(&arena, &sigma_formation, "U1"),
          "dependent pair formation synthesizes the upper sort");

    CettaPrimeRegularKernelResult pair_check = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (idx 0)))",
        "(Sigma U0 U0)");
    CHECK(pair_check.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "pair introduction checks both components against a Sigma type");

    CettaPrimeRegularKernelResult dependent_pair_check = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (Refl (idx 0))))",
        "(Sigma U0 (Id U0 (idx 0) (idx 0)))");
    CHECK(dependent_pair_check.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "dependent pair checking instantiates the second-component type");

    CettaPrimeRegularKernelResult first_projection = synth(
        &arena,
        "(PrimeScoped "
        "  (PrimeCtxCons (Sigma U0 U0) PrimeCtxNil) "
        "  (Fst (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &first_projection, "U0"),
          "first projection synthesizes the Sigma domain");

    CettaPrimeRegularKernelResult second_projection = synth(
        &arena,
        "(PrimeScoped "
        "  (PrimeCtxCons "
        "    (Sigma U0 (Id U0 (idx 0) (idx 0))) PrimeCtxNil) "
        "  (Snd (idx 0)))",
        false, 0u);
    CHECK(result_type_is(
              &arena, &second_projection,
              "(Id U0 (Fst (idx 0)) (Fst (idx 0)))"),
          "second projection substitutes the first projection into its codomain");

    CettaPrimeRegularKernelResult identity_type = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Id U0 (idx 0) (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &identity_type, "U1"),
          "identity-type formation checks both endpoints");

    CettaPrimeRegularKernelResult dependent_assumption = synth(
        &arena,
        "(PrimeScoped "
        "  (PrimeCtxCons (Id U0 (idx 0) (idx 0)) "
        "    (PrimeCtxCons U0 PrimeCtxNil)) "
        "  (idx 0))",
        false, 0u);
    CHECK(result_type_is(
              &arena, &dependent_assumption,
              "(Id U0 (idx 1) (idx 1))"),
          "a declaration domain may depend on an earlier small value");

    CettaPrimeRegularKernelResult reflexivity = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (Refl (idx 0)))",
        false, 0u);
    CHECK(result_type_is(
              &arena, &reflexivity, "(Id U0 (idx 0) (idx 0))"),
          "reflexivity synthesizes identity at the inferred carrier");

    Atom *dependent_pair_scoped = parse_one(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (Refl (idx 0))))");
    Atom *dependent_pair_expected = parse_one(
        &arena, "(Sigma U0 (Id U0 (idx 0) (idx 0)))");
    CettaPrimeRegularKernelBudget measured_pair_budget;
    cetta_prime_regular_kernel_budget_init(
        &measured_pair_budget, true, 1000000u);
    CettaPrimeRegularKernelResult measured_pair =
        cetta_prime_regular_kernel_check(
            &arena, dependent_pair_scoped, dependent_pair_expected,
            &measured_pair_budget);
    uint64_t pair_steps = measured_pair_budget.spent;
    CHECK(measured_pair.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              pair_steps > 0u,
          "dependent-pair checking reports its exact structural work");
    CettaPrimeRegularKernelBudget short_pair_budget;
    cetta_prime_regular_kernel_budget_init(
        &short_pair_budget, true, pair_steps - 1u);
    CettaPrimeRegularKernelResult short_pair =
        cetta_prime_regular_kernel_check(
            &arena, dependent_pair_scoped, dependent_pair_expected,
            &short_pair_budget);
    CHECK(short_pair.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "one fewer dependent-pair step reports exhaustion, never refutation");

    CettaPrimeRegularKernelResult identity_check = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))",
        "(Pi U0 U0)");
    CHECK(identity_check.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "annotated identity checks against its function type");

    CettaPrimeRegularKernelResult synthesized_type_formed = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Pi U0 U0))", false, 0u);
    CHECK(result_type_is(&arena, &synthesized_type_formed, "U1"),
          "non-top synthesized type is formed by construction");

    CettaPrimeRegularKernelResult beta = convert(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (App (Lam U0 (idx 0)) (idx 0)))",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0))");
    CHECK(beta.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "beta conversion is decided under an open context");

    Atom *bounded_reflexive = parse_one(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))");
    for (uint64_t steps = 0u; steps < 64u; steps++) {
        CettaPrimeRegularKernelBudget bounded_conversion_budget;
        cetta_prime_regular_kernel_budget_init(
            &bounded_conversion_budget, true, steps);
        CettaPrimeRegularKernelResult bounded_conversion =
            cetta_prime_regular_kernel_convert(
                &arena, bounded_reflexive, bounded_reflexive,
                &bounded_conversion_budget);
        CHECK(bounded_conversion.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED,
              "bounded reflexive conversion never turns exhaustion into refutation");
    }

    CettaPrimeRegularKernelResult eta = convert(
        &arena,
        "(PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
        "  (Lam U0 (App (idx 1) (idx 0))))",
        "(PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) (idx 0))");
    CHECK(eta.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "function eta conversion is decided under an open context");

    CettaPrimeRegularKernelResult loose = synth(
        &arena, "(PrimeScoped PrimeCtxNil (idx 0))", false, 0u);
    CHECK(loose.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
              loose.reason &&
              strcmp(loose.reason, "outside-regular-scoped-term") == 0,
          "loose index is outside the scoped authority class, not refuted");

    CettaPrimeRegularKernelResult last_step_loose = synth(
        &arena, "(PrimeScoped PrimeCtxNil (idx 0))", true, 2u);
    CHECK(last_step_loose.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "a final-step membership failure is not called exhaustion or refutation");

    Atom *in_class_scoped = parse_one(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))");
    Atom *out_of_class_scoped = parse_one(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 Foo))");
    CettaPrimeRegularKernelBudget class_budget;
    cetta_prime_regular_kernel_budget_init(&class_budget, false, 0u);
    CettaPrimeRegularKernelResult in_class =
        cetta_prime_regular_kernel_classify_scoped_syntax(
            in_class_scoped, &class_budget);
    CHECK(in_class.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "scoped recognizer accepts constructor-closed well-scoped syntax");
    cetta_prime_regular_kernel_budget_init(&class_budget, false, 0u);
    CettaPrimeRegularKernelResult out_of_class =
        cetta_prime_regular_kernel_classify_scoped_syntax(
            out_of_class_scoped, &class_budget);
    CHECK(out_of_class.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "scoped recognizer rejects an unknown nested constructor");
    cetta_prime_regular_kernel_budget_init(&class_budget, false, 0u);
    CettaPrimeRegularKernelResult context_out_of_class =
        cetta_prime_regular_kernel_classify_scoped_syntax(
            parse_one(
                &arena,
                "(PrimeScoped (PrimeCtxCons Foo PrimeCtxNil) (idx 0))"),
            &class_budget);
    CHECK(context_out_of_class.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "scoped recognizer checks every context domain recursively");
    cetta_prime_regular_kernel_budget_init(&class_budget, false, 0u);
    CettaPrimeRegularKernelResult expected_out_of_class =
        cetta_prime_regular_kernel_classify_scoped_check_syntax(
            parse_one(&arena, "(PrimeScoped PrimeCtxNil U0)"),
            parse_one(&arena, "Foo"), &class_budget);
    CHECK(expected_out_of_class.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "checking recognizer includes the expected type in its class");
    cetta_prime_regular_kernel_budget_init(&class_budget, true, 0u);
    CettaPrimeRegularKernelResult class_exhausted =
        cetta_prime_regular_kernel_classify_scoped_syntax(
            in_class_scoped, &class_budget);
    CHECK(class_exhausted.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "recognition budget exhaustion is not a semantic verdict");

    CettaPrimeRegularKernelResult exhausted = synth(
        &arena, "(PrimeScoped PrimeCtxNil U0)", true, 0u);
    CHECK(exhausted.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "budget exhaustion is never a typing verdict");

    CettaPrimeRegularKernelResult universe_context = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U1 PrimeCtxNil) (idx 0))",
        false, 0u);
    CHECK(universe_context.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              universe_context.type &&
              atom_is_symbol(universe_context.type, "U1"),
          "a universe-valued context variable is native in the tower");

    CettaPrimeRegularKernelResult small_term_as_type = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (Pi (idx 0) U0))",
        false, 0u);
    CHECK(small_term_as_type.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
              small_term_as_type.reason &&
              strcmp(small_term_as_type.reason, "expected-formed-type") == 0,
          "a small term used as a type declines pending the universe ruling");

    CettaPrimeRegularKernelResult upper_sort = synth(
        &arena, "(PrimeScoped PrimeCtxNil U1)", false, 0u);
    CHECK(upper_sort.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              upper_sort.type &&
              upper_sort.type->kind == ATOM_EXPR &&
              atom_is_symbol(upper_sort.type->expr.elems[0], "Sort"),
          "the embedded legacy marker inhabits its successor universe");

    CettaPrimeRegularKernelResult embedded_sort_conversion = convert(
        &arena, "(PrimeScoped PrimeCtxNil U1)",
        "(PrimeScoped PrimeCtxNil (Sort (LevelConst 0)))");
    CettaPrimeRegularKernelResult normalized_level_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelMax (LevelConst 0) (LevelConst 0))))",
        "(PrimeScoped PrimeCtxNil U1)");
    CHECK(embedded_sort_conversion.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              normalized_level_conversion.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "sort conversion uses level semantics rather than source spelling");

    CettaPrimeRegularKernelResult distinct_level_parameters = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelParam 0)))",
        "(PrimeScoped PrimeCtxNil (Sort (LevelParam 1)))");
    CHECK(distinct_level_parameters.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "distinct schematic level parameters retain a checked obstruction");

    Atom *schematic_sort = parse_one(
        &arena, "(Sort (LevelParam 0))");
    CettaPrimeRegularKernelBudget schematic_class_budget;
    cetta_prime_regular_kernel_budget_init(
        &schematic_class_budget, false, 0u);
    CettaPrimeRegularKernelResult schematic_closed_class =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            schematic_sort, &schematic_class_budget);
    CHECK(schematic_closed_class.status ==
              CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED &&
              schematic_closed_class.reason &&
              strcmp(
                  schematic_closed_class.reason,
                  "schematic-level-outside-closed-fragment") == 0,
          "schematic levels remain outside closed source-bound admissions");
    CettaPrimeRegularKernelBudget schematic_synthesis_budget;
    cetta_prime_regular_kernel_budget_init(
        &schematic_synthesis_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult schematic_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, schematic_sort,
            &schematic_synthesis_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(schematic_synthesis.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              schematic_synthesis.synthesis == NULL,
          "closed synthesis never borrows authority for a schematic level");
    CettaPrimeRegularKernelBudget schematic_conversion_budget;
    cetta_prime_regular_kernel_budget_init(
        &schematic_conversion_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult schematic_conversion =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, schematic_sort, schematic_sort,
            &schematic_conversion_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(schematic_conversion.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              schematic_conversion.conversion == NULL,
          "closed conversion never borrows authority for a schematic level");

    CettaPrimeRegularKernelResult cumulative_promotion = check_term(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(Sort (LevelConst 1))");
    CettaPrimeRegularKernelResult no_universe_lowering = check_term(
        &arena, "(PrimeScoped PrimeCtxNil U1)", "U1");
    CettaPrimeRegularKernelResult no_term_level_erasure = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0))",
        "U1");
    CHECK(cumulative_promotion.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              no_universe_lowering.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              no_term_level_erasure.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "cumulativity raises formed types without lowering universes or erasing terms");

    CettaPrimeRegularKernelResult polymorphic_join = synth(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Pi (Sort (LevelParam 1)) (Sort (LevelParam 0))))",
        false, 0u);
    CHECK(polymorphic_join.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              polymorphic_join.type &&
              polymorphic_join.type->kind == ATOM_EXPR &&
              atom_is_symbol(polymorphic_join.type->expr.elems[0], "Sort"),
          "Pi formation joins schematic universe levels natively");

    Atom *rigid_level_context = parse_one(
        &arena,
        "(PrimeCtxCons (Sort (LevelParam 99)) PrimeCtxNil)");
    Atom *instantiated_index = parse_one(&arena, "(idx 0)");
    Atom *fresh_level_expected = parse_one(
        &arena, "(Sort (LevelParam 7))");
    uint64_t fresh_level_parameter[] = {7u};
    CettaPrimeRegularKernelBudget rigid_level_budget;
    cetta_prime_regular_kernel_budget_init(
        &rigid_level_budget, false, 0u);
    CettaPrimeRegularKernelResult rigid_level_instantiation =
        cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
            &arena, rigid_level_context, instantiated_index,
            fresh_level_expected, fresh_level_parameter, 1u,
            &rigid_level_budget);
    CHECK(rigid_level_instantiation.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "a fresh declaration level may instantiate to an enclosing rigid schema level");

    Atom *cyclic_level_context = parse_one(
        &arena,
        "(PrimeCtxCons "
        "  (Sort (LevelSucc (LevelParam 7))) PrimeCtxNil)");
    CettaPrimeRegularKernelBudget cyclic_level_budget;
    cetta_prime_regular_kernel_budget_init(
        &cyclic_level_budget, false, 0u);
    CettaPrimeRegularKernelResult cyclic_level_instantiation =
        cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
            &arena, cyclic_level_context, instantiated_index,
            fresh_level_expected, fresh_level_parameter, 1u,
            &cyclic_level_budget);
    CHECK(cyclic_level_instantiation.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
              cyclic_level_instantiation.reason &&
              strcmp(
                  cyclic_level_instantiation.reason,
                  "level-instantiation-lower-bound-mentions-solved-parameter") ==
                  0,
          "a cyclic declaration-level lower bound remains outside the solved fragment");

    Atom *schema_context = parse_one(
        &arena,
        "(PrimeCtxDecl "
        "  (DeclConst list (LevelParam 0)) "
        "  (Pi (Sort (LevelParam 0)) (Sort (LevelParam 0))) "
        "  PrimeCtxNil)");
    Atom *closed_list_constant = parse_one(
        &arena, "(DeclConst list (LevelConst 0))");
    CettaPrimeRegularKernelBudget schema_lookup_budget;
    cetta_prime_regular_kernel_budget_init(
        &schema_lookup_budget, false, 0u);
    CettaPrimeRegularKernelResult schema_lookup =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            &arena, schema_context, closed_list_constant,
            &schema_lookup_budget);
    CHECK(result_type_is(
              &arena, &schema_lookup,
              "(Pi (Sort (LevelConst 0)) "
              "    (Sort (LevelConst 0)))"),
          "one global declaration schema instantiates from an occurrence's explicit universe argument");

    CettaPrimeRegularKernelBudget schema_artifact_budget;
    cetta_prime_regular_kernel_budget_init(
        &schema_artifact_budget, false, 0u);
    Atom *dependent_schema = parse_one(
        &arena,
        "(Pi (Sort (LevelParam 99)) "
        "  (App (DeclConst list (LevelParam 7)) (idx 0)))");
    CettaPrimeRegularKernelFormedSchemaV1 formed_schema =
        cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
            &arena, schema_context, dependent_schema,
            fresh_level_parameter, 1u, &schema_artifact_budget);
    Atom *expected_formed_schema = parse_one(
        &arena,
        "(Pi (Sort (LevelParam 99)) "
        "  (App (DeclConst list (LevelParam 99)) (idx 0)))");
    CHECK(formed_schema.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              formed_schema.term && expected_formed_schema &&
              atom_eq(formed_schema.term, expected_formed_schema),
          "schema formation returns the solved dependency-level artifact instead of discarding its equation");

    CettaPrimeRegularKernelBudget malformed_schema_budget;
    cetta_prime_regular_kernel_budget_init(
        &malformed_schema_budget, false, 0u);
    CettaPrimeRegularKernelResult malformed_schema_key =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            &arena,
            parse_one(
                &arena,
                "(PrimeCtxDecl "
                "  (DeclConst list (LevelParam 1)) "
                "  (Pi (Sort (LevelParam 0)) "
                "      (Sort (LevelParam 0))) PrimeCtxNil)"),
            closed_list_constant, &malformed_schema_budget);
    CHECK(malformed_schema_key.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "a declaration schema key must enumerate its local universe parameters canonically");

    CettaPrimeRegularKernelBudget duplicate_schema_budget;
    cetta_prime_regular_kernel_budget_init(
        &duplicate_schema_budget, false, 0u);
    CettaPrimeRegularKernelResult duplicate_schema =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            &arena,
            parse_one(
                &arena,
                "(PrimeCtxDecl "
                "  (DeclConst list (LevelParam 0)) "
                "  (Pi (Sort (LevelParam 0)) (Sort (LevelParam 0))) "
                "  (PrimeCtxDecl "
                "    (DeclConst list (LevelParam 0)) "
                "    (Pi (Sort (LevelParam 0)) "
                "        (Sort (LevelParam 0))) PrimeCtxNil))"),
            closed_list_constant, &duplicate_schema_budget);
    CHECK(duplicate_schema.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
              duplicate_schema.reason &&
              strcmp(
                  duplicate_schema.reason,
                  "duplicate-declaration-constant") == 0,
          "a global source name has one declaration schema rather than cloned context bindings");

    CettaPrimeRegularKernelBudget foreign_constant_budget;
    cetta_prime_regular_kernel_budget_init(
        &foreign_constant_budget, false, 0u);
    CettaPrimeRegularKernelResult foreign_constant =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            &arena, schema_context,
            parse_one(
                &arena, "(DeclConst other (LevelConst 0))"),
            &foreign_constant_budget);
    CHECK(foreign_constant.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              foreign_constant.reason &&
              strcmp(foreign_constant.reason, "undeclared-constant") == 0,
          "equal universe arguments never make different global names interchangeable");

    CettaPrimeRegularKernelResult malformed_level = synth(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelSucc LevelBogus)))",
        false, 0u);
    CHECK(malformed_level.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
          "malformed level data never enters the admitted level algebra");

    CettaPrimeRegularKernelResult bad_application = synth(
        &arena, "(PrimeScoped PrimeCtxNil (App U0 U0))", false, 0u);
    CHECK(bad_application.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "application requires a dependent function type");

    CettaPrimeRegularKernelResult bad_sigma = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Sigma U1 U0))", false, 0u);
    CHECK(bad_sigma.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              bad_sigma.type && bad_sigma.type->kind == ATOM_EXPR &&
              atom_is_symbol(bad_sigma.type->expr.elems[0], "Sort"),
          "Sigma formation joins the universe levels of domain and body");

    CettaPrimeRegularKernelResult pair_needs_expected = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (idx 0)))",
        false, 0u);
    CHECK(pair_needs_expected.status ==
              CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
              pair_needs_expected.reason &&
              strcmp(pair_needs_expected.reason, "pair-needs-pair-type") == 0,
          "pair introduction without an expected type abstains from synthesis");

    CettaPrimeRegularKernelResult pair_wrong_expected = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (idx 0)))",
        "(Pi U0 U0)");
    CHECK(pair_wrong_expected.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              pair_wrong_expected.reason &&
              strcmp(pair_wrong_expected.reason, "pair-needs-pair-type") == 0,
          "pair introduction rejects a non-Sigma expected type");

    CettaPrimeRegularKernelResult dependent_pair_mismatch = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Pair (idx 0) (idx 0)))",
        "(Sigma U0 (Id U0 (idx 0) (idx 0)))");
    CHECK(dependent_pair_mismatch.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "dependent pair checking rejects a mistyped second component");

    CettaPrimeRegularKernelResult bad_first_projection = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (Fst (idx 0)))",
        false, 0u);
    CHECK(bad_first_projection.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              bad_first_projection.reason &&
              strcmp(bad_first_projection.reason, "expected-pair-type") == 0,
          "first projection requires a Sigma-typed operand");

    CettaPrimeRegularKernelResult bad_second_projection = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (Snd (idx 0)))",
        false, 0u);
    CHECK(bad_second_projection.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              bad_second_projection.reason &&
              strcmp(bad_second_projection.reason, "expected-pair-type") == 0,
          "second projection requires a Sigma-typed operand");

    CettaPrimeRegularKernelResult bad_identity_endpoint = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Id U0 U0 (idx 0)))",
        false, 0u);
    CHECK(bad_identity_endpoint.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "identity formation rejects an endpoint outside its carrier");

    CettaPrimeRegularKernelResult type_reflexivity = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Refl U0))", false, 0u);
    CHECK(type_reflexivity.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              type_reflexivity.type &&
              type_reflexivity.type->kind == ATOM_EXPR &&
              type_reflexivity.type->expr.len == 4u &&
              atom_is_symbol(type_reflexivity.type->expr.elems[0], "Id"),
          "identity introduction applies to universe-formed types");

    CettaPrimeRegularKernelResult bad_lambda = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil (Lam (Pi U0 U0) (idx 0)))",
        "(Pi U0 U0)");
    CHECK(bad_lambda.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "lambda annotation must agree with the expected domain");

    CettaPrimeRegularKernelResult unformed_annotation = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Lam (App (Lam U0 U0) U1) (idx 0)))",
        "(Pi U0 U0)");
    CHECK(unformed_annotation.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "a type-family annotation retains the checked argument mismatch");

    CettaPrimeRegularKernelResult distinct = convert(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(PrimeScoped PrimeCtxNil (Pi U0 U0))");
    CHECK(distinct.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED && distinct.reason &&
              strcmp(distinct.reason, "distinct-normal-forms") == 0,
          "distinct same-typed normal forms are rejected");

    CettaPrimeRegularKernelResult context_mismatch = convert(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) U0)");
    CHECK(context_mismatch.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              context_mismatch.reason &&
              strcmp(context_mismatch.reason,
                     "conversion-context-mismatch") == 0,
          "conversion never compares terms from different contexts");

    Atom *closed_beta_left = parse_one(
        &arena,
        "(App (Lam (Pi U0 U0) (idx 0)) (Lam U0 (idx 0)))");
    Atom *closed_beta_right = parse_one(&arena, "(Lam U0 (idx 0))");
    CHECK(cetta_prime_regular_kernel_term_maybe_syntax(closed_beta_left) &&
              cetta_prime_regular_kernel_term_maybe_syntax(closed_beta_right) &&
              !cetta_prime_regular_kernel_term_maybe_syntax(
                  parse_one(&arena, "outside-regular-kernel")),
          "regular-kernel root prefilter accepts candidates without claiming all terms");

    CettaPrimeRegularKernelBudget admission_budget;
    cetta_prime_regular_kernel_budget_init(
        &admission_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult admitted_beta =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, closed_beta_left, closed_beta_right,
            &admission_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(admitted_beta.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              admitted_beta.conversion,
          "closed well-typed beta conversion constructs an admitted judgment");
    bool admitted_equal = false;
    const char *admitted_reason = NULL;
    CHECK(cetta_prime_regular_kernel_admitted_conversion_v1_decision(
              admitted_beta.conversion, &space,
              cetta_prime_regular_kernel_closed_conversion_profile_v1,
              &admitted_equal, &admitted_reason) &&
              admitted_equal && admitted_reason == NULL,
          "current admitted beta judgment executes without checker replay");
    CettaPrimeRegularKernelBudget cached_beta_budget;
    cetta_prime_regular_kernel_budget_init(&cached_beta_budget, true, 0u);
    CettaPrimeRegularKernelAdmissionResult cached_beta =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, closed_beta_left, closed_beta_right,
            &cached_beta_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    bool cached_equal = false;
    CHECK(cached_beta.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              cached_beta.conversion && cached_beta_budget.spent == 0u &&
              cetta_prime_regular_kernel_admitted_conversion_v1_decision(
                  cached_beta.conversion, &space,
                  cetta_prime_regular_kernel_closed_conversion_profile_v1,
                  &cached_equal, NULL) && cached_equal,
          "revision-keyed admission cache reuses exact evidence with zero checking work");
    cetta_prime_regular_kernel_admitted_conversion_v1_free(
        cached_beta.conversion);
    CettaPrimeRegularKernelAdmissionMetadataV1 admitted_metadata;
    CHECK(cetta_prime_regular_kernel_admitted_conversion_v1_metadata(
              admitted_beta.conversion, &admitted_metadata) &&
              admitted_metadata.universe_instance_id == universe.instance_id &&
              admitted_metadata.universe_storage_epoch == universe.storage_epoch &&
              admitted_metadata.context_id != CETTA_ATOM_ID_NONE &&
              admitted_metadata.left_term_id != CETTA_ATOM_ID_NONE &&
              admitted_metadata.right_term_id != CETTA_ATOM_ID_NONE &&
              admitted_metadata.left_type_id != CETTA_ATOM_ID_NONE &&
              admitted_metadata.right_type_id != CETTA_ATOM_ID_NONE,
          "admitted judgment retains universe-bound term and type identities");
    Atom *erased_left = NULL;
    Atom *erased_right = NULL;
    CHECK(cetta_prime_regular_kernel_admitted_conversion_v1_erase(
              admitted_beta.conversion, &universe, &arena,
              &erased_left, &erased_right) &&
              atom_eq(erased_left, closed_beta_left) &&
              atom_eq(erased_right, closed_beta_right),
          "admitted judgment erases back to the exact raw operands");

    CettaPrimeRegularKernelBudget synthesis_budget;
    cetta_prime_regular_kernel_budget_init(
        &synthesis_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult admitted_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, closed_beta_right, &synthesis_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CettaPrimeRegularKernelStatus synthesis_status =
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    AtomId synthesis_type_id = CETTA_ATOM_ID_NONE;
    CHECK(admitted_synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              admitted_synthesis.synthesis &&
              cetta_prime_regular_kernel_admitted_synthesis_v1_decision(
                  admitted_synthesis.synthesis, &space,
                  cetta_prime_regular_kernel_closed_synthesis_profile_v1,
                  &synthesis_status, &synthesis_type_id, NULL) &&
              synthesis_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              synthesis_type_id != CETTA_ATOM_ID_NONE,
          "closed identity constructs an admitted synthesis judgment");
    Atom *synthesis_type = term_universe_copy_atom(
        &universe, &arena, synthesis_type_id);
    CHECK(synthesis_type && atom_eq(
              synthesis_type, parse_one(&arena, "(Pi U0 U0)")),
          "admitted synthesis retains the exact inferred type");
    CettaPrimeRegularKernelSynthesisMetadataV1 synthesis_metadata;
    CHECK(cetta_prime_regular_kernel_admitted_synthesis_v1_metadata(
              admitted_synthesis.synthesis, &synthesis_metadata) &&
              synthesis_metadata.universe_instance_id == universe.instance_id &&
              synthesis_metadata.universe_storage_epoch == universe.storage_epoch &&
              synthesis_metadata.context_id != CETTA_ATOM_ID_NONE &&
              synthesis_metadata.term_id != CETTA_ATOM_ID_NONE &&
              synthesis_metadata.type_id == synthesis_type_id,
          "admitted synthesis exposes universe-bound metadata");
    Atom *erased_synthesis_term = NULL;
    CHECK(cetta_prime_regular_kernel_admitted_synthesis_v1_erase(
              admitted_synthesis.synthesis, &universe, &arena,
              &erased_synthesis_term) &&
              atom_eq(erased_synthesis_term, closed_beta_right),
          "admitted synthesis erases to its exact raw term");

    CettaPrimeTypedValueV1 *typed_identity =
        cetta_prime_typed_value_import_synthesis_v1(
            &arena, &space, admitted_synthesis.synthesis);
    CettaPrimeTypedValueMetadataV1 typed_identity_metadata;
    CHECK(typed_identity &&
              cetta_prime_typed_value_v1_is_current(
                  typed_identity, &space) &&
              cetta_prime_typed_value_v1_metadata(
                  typed_identity, &typed_identity_metadata) &&
              typed_identity_metadata.construction ==
                  CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1 &&
              typed_identity_metadata.term_id == synthesis_metadata.term_id &&
              typed_identity_metadata.type_id == synthesis_metadata.type_id,
          "a checked raw boundary enters Prime as a current opaque typed value");

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeTypedValueV1 *typed_reflexivity =
        cetta_prime_typed_value_refl_v1(&arena, &space, typed_identity);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats typed_construction_stats;
    cetta_runtime_stats_snapshot(&typed_construction_stats);
    cetta_runtime_stats_disable();
    CHECK(typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ATTEMPT] ==
                  0u &&
              typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_CHECK] ==
                  0u &&
              typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ATTEMPT] ==
                  0u &&
              typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_CHECK] ==
                  0u &&
              typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ATTEMPT] ==
                  0u &&
              typed_construction_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_CHECK] ==
                  0u,
          "typed identity introduction performs no post-hoc judgment demand");
#endif
    CettaPrimeTypedValueMetadataV1 typed_reflexivity_metadata;
    Atom *typed_reflexivity_term = NULL;
    Atom *typed_reflexivity_type = NULL;
    CHECK(typed_reflexivity &&
              cetta_prime_typed_value_v1_is_current(
                  typed_reflexivity, &space) &&
              cetta_prime_typed_value_v1_metadata(
                  typed_reflexivity, &typed_reflexivity_metadata) &&
              typed_reflexivity_metadata.construction ==
                  CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 &&
              cetta_prime_typed_value_v1_erase(
                  typed_reflexivity, &universe, &arena,
                  &typed_reflexivity_term, &typed_reflexivity_type) &&
              atom_eq(
                  typed_reflexivity_term,
                  parse_one(&arena, "(Refl (Lam U0 (idx 0)))")) &&
              atom_eq(
                  typed_reflexivity_type,
                  parse_one(
                      &arena,
                      "(Id (Pi U0 U0) (Lam U0 (idx 0)) "
                      "    (Lam U0 (idx 0)))")),
          "Prime refl constructs its exact dependent judgment from a typed premise");
    CettaPrimeRegularKernelBudget typed_reflexivity_check_budget;
    cetta_prime_regular_kernel_budget_init(
        &typed_reflexivity_check_budget, false, 0u);
    CettaPrimeRegularKernelResult typed_reflexivity_calibration =
        cetta_prime_regular_kernel_check_intrinsic(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            typed_reflexivity_term, typed_reflexivity_type,
            &typed_reflexivity_check_budget);
    CHECK(typed_reflexivity_calibration.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "the independent Prime checker agrees with the native refl constructor");

    Atom *closed_identity_type = parse_one(&arena, "(Pi U0 U0)");
    Atom *intrinsic_identity = parse_one(&arena, "(Lam (idx 0))");
    CettaPrimeRegularKernelBudget intrinsic_class_budget;
    cetta_prime_regular_kernel_budget_init(
        &intrinsic_class_budget, false, 0u);
    CettaPrimeRegularKernelResult intrinsic_class =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            intrinsic_identity, &intrinsic_class_budget);
    CHECK(!cetta_prime_regular_kernel_term_maybe_syntax(intrinsic_identity) &&
              cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(
                  intrinsic_identity) &&
              intrinsic_class.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "intrinsic recognition adds exactly the unannotated Pattern lambda");

    CettaPrimeRegularKernelBudget intrinsic_checking_budget;
    cetta_prime_regular_kernel_budget_init(
        &intrinsic_checking_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult intrinsic_checking =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space, intrinsic_identity, closed_identity_type,
            &intrinsic_checking_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CettaPrimeRegularKernelStatus intrinsic_checking_status =
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    CHECK(intrinsic_checking.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              intrinsic_checking.checking &&
              cetta_prime_regular_kernel_admitted_checking_v1_decision(
                  intrinsic_checking.checking, &space,
                  cetta_prime_regular_kernel_closed_checking_profile_v1,
                  &intrinsic_checking_status, NULL) &&
              intrinsic_checking_status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "unannotated Pattern lambda receives revision-bound checking authority");

    CettaPrimeRegularKernelBudget intrinsic_synthesis_budget;
    cetta_prime_regular_kernel_budget_init(
        &intrinsic_synthesis_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult intrinsic_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, intrinsic_identity,
            &intrinsic_synthesis_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(intrinsic_synthesis.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              intrinsic_synthesis.synthesis == NULL,
          "unannotated lambda synthesis abstains instead of minting a refutation");

    CettaPrimeRegularKernelBudget checking_budget;
    cetta_prime_regular_kernel_budget_init(&checking_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult admitted_checking =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space, closed_beta_right, closed_identity_type,
            &checking_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CettaPrimeRegularKernelStatus checking_status = CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    CHECK(admitted_checking.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              admitted_checking.checking &&
              cetta_prime_regular_kernel_admitted_checking_v1_decision(
                  admitted_checking.checking, &space,
                  cetta_prime_regular_kernel_closed_checking_profile_v1,
                  &checking_status, NULL) &&
              checking_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "closed identity constructs an admitted checking judgment");
    CettaPrimeRegularKernelCheckingMetadataV1 checking_metadata;
    CHECK(cetta_prime_regular_kernel_admitted_checking_v1_metadata(
              admitted_checking.checking, &checking_metadata) &&
              checking_metadata.universe_instance_id == universe.instance_id &&
              checking_metadata.universe_storage_epoch == universe.storage_epoch &&
              checking_metadata.context_id != CETTA_ATOM_ID_NONE &&
              checking_metadata.term_id != CETTA_ATOM_ID_NONE &&
              checking_metadata.expected_type_id != CETTA_ATOM_ID_NONE,
          "admitted checking exposes universe-bound judgment metadata");
    Atom *erased_checking_term = NULL;
    Atom *erased_expected_type = NULL;
    CHECK(cetta_prime_regular_kernel_admitted_checking_v1_erase(
              admitted_checking.checking, &universe, &arena,
              &erased_checking_term, &erased_expected_type) &&
              atom_eq(erased_checking_term, closed_beta_right) &&
              atom_eq(erased_expected_type, closed_identity_type),
          "admitted checking erases to its exact term and expected type");

    CettaPrimeRegularKernelBudget mismatch_checking_budget;
    cetta_prime_regular_kernel_budget_init(
        &mismatch_checking_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult admitted_mismatch_checking =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space, closed_beta_right,
            parse_one(&arena, "U0"), &mismatch_checking_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CettaPrimeRegularKernelStatus mismatch_checking_status =
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    CHECK(admitted_mismatch_checking.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              cetta_prime_regular_kernel_admitted_checking_v1_decision(
                  admitted_mismatch_checking.checking, &space,
                  cetta_prime_regular_kernel_closed_checking_profile_v1,
                  &mismatch_checking_status, NULL) &&
              mismatch_checking_status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "type mismatch produces admitted negative checking evidence");

    CettaPrimeRegularKernelBudget loose_checking_budget;
    cetta_prime_regular_kernel_budget_init(
        &loose_checking_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult loose_checking =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space, parse_one(&arena, "(idx 0)"),
            closed_identity_type, &loose_checking_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(loose_checking.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              loose_checking.checking == NULL,
          "loose term stays outside the closed checking class");

    CettaPrimeRegularKernelBudget exhausted_checking_budget;
    cetta_prime_regular_kernel_budget_init(
        &exhausted_checking_budget, true, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult exhausted_checking =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space,
            parse_one(&arena, "(Lam (Pi U0 U0) (idx 0))"),
            parse_one(&arena, "(Pi (Pi U0 U0) (Pi U0 U0))"),
            &exhausted_checking_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(exhausted_checking.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED,
          "exhausted closed checking reports budget loss without legacy evidence");

    Atom *checking_candidate_mismatch = parse_one(&arena, "U0");
    Atom *checking_candidate_loose = parse_one(&arena, "(idx 0)");
    Atom *checking_candidate_large =
        parse_one(&arena, "(Lam (Pi U0 U0) (idx 0))");
    Atom *checking_candidate_large_type =
        parse_one(&arena, "(Pi (Pi U0 U0) (Pi U0 U0))");
    CettaPrimeRegularKernelCheckingCandidateV1 checking_candidates[] = {
        {closed_beta_right, closed_identity_type},
        {closed_beta_right, closed_identity_type},
        {closed_beta_right, checking_candidate_mismatch},
        {checking_candidate_loose, closed_identity_type},
        {checking_candidate_large, checking_candidate_large_type},
    };
    CettaPrimeRegularKernelBudget checking_candidate_budgets[
        sizeof checking_candidates / sizeof checking_candidates[0]];
    for (size_t index = 0u;
         index < sizeof checking_candidate_budgets /
                     sizeof checking_candidate_budgets[0];
         index++) {
        cetta_prime_regular_kernel_budget_init(
            &checking_candidate_budgets[index], index == 4u, 0u);
    }
    CettaPrimeRegularKernelCheckingBagV1 checking_bag;
    CHECK(cetta_prime_regular_kernel_observe_closed_checking_bag_v1(
              &arena, &space, checking_candidates,
              checking_candidate_budgets,
              sizeof checking_candidates / sizeof checking_candidates[0],
              cetta_prime_regular_kernel_closed_checking_profile_v1,
              &prime_typing_open_regular_kernel_source_binding_v1,
              &checking_bag),
          "checking bag observes each candidate occurrence without evaluation");
    CHECK(checking_bag.established_count == 2u &&
              checking_bag.refuted_count == 1u &&
              checking_bag.undetermined_count == 1u &&
              checking_bag.incomplete_count == 1u,
          "checking bag keeps Decision and Coverage outcomes distinct");
    CHECK(checking_bag.occurrences[0].result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              checking_bag.occurrences[1].result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              checking_bag.occurrences[2].result.value.outcome ==
                  CETTA_NIK_OUTCOME_REFUTED &&
              checking_bag.occurrences[3].result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
              checking_bag.occurrences[4].result.value.outcome ==
                  CETTA_NIK_OUTCOME_INCOMPLETE,
          "checking bag records established, refuted, abstained, and incomplete occurrences");
    CHECK(!cetta_prime_regular_kernel_checking_bag_v1_is_decision_complete(
              &checking_bag),
          "open coverage makes the candidate bag honestly incomplete");

    CettaPrimeRegularKernelCheckingCandidateV1 reordered_candidates[] = {
        checking_candidates[4], checking_candidates[0],
        checking_candidates[3], checking_candidates[2],
        checking_candidates[1],
    };
    CettaPrimeRegularKernelCheckingCandidateV1 missing_duplicate_candidates[] = {
        checking_candidates[4], checking_candidates[0],
        checking_candidates[3], checking_candidates[2],
    };
    CHECK(cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
              &arena, checking_candidates,
              sizeof checking_candidates / sizeof checking_candidates[0],
              reordered_candidates,
              sizeof reordered_candidates / sizeof reordered_candidates[0]),
          "candidate bag equality ignores order while preserving occurrence identity");
    CHECK(!cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
              &arena, checking_candidates,
              sizeof checking_candidates / sizeof checking_candidates[0],
              missing_duplicate_candidates,
              sizeof missing_duplicate_candidates /
                  sizeof missing_duplicate_candidates[0]),
          "candidate bag equality rejects a missing duplicate occurrence");

    CettaPrimeRegularKernelCheckingCandidateV1 *erased_established = NULL;
    size_t erased_established_count = 0u;
    CHECK(cetta_prime_regular_kernel_checking_bag_v1_erase_established(
              &checking_bag, &space,
              cetta_prime_regular_kernel_closed_checking_profile_v1,
              &arena, &erased_established, &erased_established_count) &&
              erased_established_count == 2u,
          "established checking certificates erase to two exact candidate occurrences");
    CettaPrimeRegularKernelCheckingCandidateV1 typed_producer_candidates[] = {
        checking_candidates[0], checking_candidates[1],
    };
    CHECK(cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
              &arena, erased_established, erased_established_count,
              typed_producer_candidates,
              sizeof typed_producer_candidates /
                  sizeof typed_producer_candidates[0]),
          "proof-carrying erasure reproduces the typed producer candidate bag");
    CHECK(!cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
              &arena, erased_established, erased_established_count,
              typed_producer_candidates, 1u),
          "proof-carrying erasure cannot hide duplicate loss");

    CettaPrimeRegularKernelBudget complete_candidate_budgets[3];
    for (size_t index = 0u; index < 3u; index++)
        cetta_prime_regular_kernel_budget_init(
            &complete_candidate_budgets[index], false, 0u);
    CettaPrimeRegularKernelCheckingBagV1 complete_checking_bag;
    CHECK(cetta_prime_regular_kernel_observe_closed_checking_bag_v1(
              &arena, &space, checking_candidates,
              complete_candidate_budgets, 3u,
              cetta_prime_regular_kernel_closed_checking_profile_v1,
              &prime_typing_open_regular_kernel_source_binding_v1,
              &complete_checking_bag) &&
              cetta_prime_regular_kernel_checking_bag_v1_is_decision_complete(
                  &complete_checking_bag),
          "a closed established/refuted bag is decision-complete");

    CettaPrimeRegularKernelBudget upper_sort_budget;
    cetta_prime_regular_kernel_budget_init(
        &upper_sort_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult upper_sort_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, parse_one(&arena, "U1"),
            &upper_sort_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(upper_sort_synthesis.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              upper_sort_synthesis.synthesis != NULL,
          "upper-sort synthesis carries native tower evidence at admission");

    CettaPrimeRegularKernelBudget loose_synthesis_budget;
    cetta_prime_regular_kernel_budget_init(
        &loose_synthesis_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult loose_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, parse_one(&arena, "(idx 0)"),
            &loose_synthesis_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(loose_synthesis.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              loose_synthesis.synthesis == NULL,
          "loose index stays outside the closed synthesis class");

    CettaPrimeRegularKernelBudget exhausted_synthesis_budget;
    cetta_prime_regular_kernel_budget_init(
        &exhausted_synthesis_budget, true, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult exhausted_synthesis =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, parse_one(&arena, "(Pi U0 U0)"),
            &exhausted_synthesis_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(exhausted_synthesis.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED,
          "exhausted closed synthesis reports budget loss without legacy evidence");

    CettaPrimeRegularKernelConversionProfileV1 foreign_profile =
        cetta_prime_regular_kernel_closed_conversion_profile_v1;
    foreign_profile.conversion_profile++;
    CHECK(!cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
              admitted_beta.conversion, &space, foreign_profile),
          "a different conversion profile cannot consume an admitted judgment");
    CettaPrimeRegularKernelSynthesisProfileV1 foreign_synthesis_profile =
        cetta_prime_regular_kernel_closed_synthesis_profile_v1;
    foreign_synthesis_profile.synthesis_profile++;
    CHECK(!cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
              admitted_synthesis.synthesis, &space,
              foreign_synthesis_profile),
          "a different synthesis profile cannot consume an admitted judgment");
    CettaPrimeRegularKernelCheckingProfileV1 foreign_checking_profile =
        cetta_prime_regular_kernel_closed_checking_profile_v1;
    foreign_checking_profile.checking_profile++;
    CHECK(!cetta_prime_regular_kernel_admitted_checking_v1_is_current(
              admitted_checking.checking, &space,
              foreign_checking_profile),
          "a different checking profile cannot consume an admitted judgment");

    CettaNikDirectSourceBindingV1 copied_binding =
        prime_typing_open_regular_kernel_source_binding_v1;
    CettaPrimeRegularKernelBudget copied_binding_budget;
    cetta_prime_regular_kernel_budget_init(
        &copied_binding_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult copied_binding_result =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, closed_beta_left, closed_beta_right,
            &copied_binding_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &copied_binding);
    CHECK(copied_binding_result.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID &&
              copied_binding_result.conversion == NULL,
          "a copied provenance receipt cannot mint native authority");
    CettaPrimeRegularKernelBudget copied_synthesis_binding_budget;
    cetta_prime_regular_kernel_budget_init(
        &copied_synthesis_binding_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult copied_synthesis_binding =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &space, closed_beta_right,
            &copied_synthesis_binding_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &copied_binding);
    CHECK(copied_synthesis_binding.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID &&
              copied_synthesis_binding.synthesis == NULL,
          "a copied receipt cannot mint synthesis authority");
    CettaPrimeRegularKernelBudget copied_checking_binding_budget;
    cetta_prime_regular_kernel_budget_init(
        &copied_checking_binding_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult copied_checking_binding =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &space, closed_beta_right, closed_identity_type,
            &copied_checking_binding_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &copied_binding);
    CHECK(copied_checking_binding.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID &&
              copied_checking_binding.checking == NULL,
          "a copied receipt cannot mint checking authority");

    CettaPrimeRegularKernelBudget unsupported_profile_budget;
    cetta_prime_regular_kernel_budget_init(
        &unsupported_profile_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult unsupported_profile =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, closed_beta_left, closed_beta_right,
            &unsupported_profile_budget, foreign_profile,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(unsupported_profile.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID,
          "unsupported conversion profiles are rejected at construction");

    CettaPrimeRegularKernelBudget exhausted_admission_budget;
    cetta_prime_regular_kernel_budget_init(
        &exhausted_admission_budget, true, 0u);
    CettaPrimeRegularKernelAdmissionResult exhausted_admission =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, parse_one(&arena, "(Pi U0 U0)"),
            parse_one(&arena, "(Pi U0 U0)"),
            &exhausted_admission_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(exhausted_admission.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED,
          "exhausted native admission reports budget loss rather than fallback evidence");

    Atom *loose_closed = parse_one(&arena, "(idx 0)");
    CettaPrimeRegularKernelBudget loose_admission_budget;
    cetta_prime_regular_kernel_budget_init(
        &loose_admission_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult loose_admission =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, loose_closed, loose_closed,
            &loose_admission_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(loose_admission.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT &&
              loose_admission.conversion == NULL,
          "a loose index remains outside the closed admitted class");

    Atom *distinct_left = parse_one(&arena, "U0");
    Atom *distinct_right = parse_one(&arena, "(Pi U0 U0)");
    CettaPrimeRegularKernelBudget distinct_admission_budget;
    cetta_prime_regular_kernel_budget_init(
        &distinct_admission_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult admitted_distinct =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &space, distinct_left, distinct_right,
            &distinct_admission_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    bool distinct_equal = true;
    const char *distinct_reason = NULL;
    CHECK(admitted_distinct.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              cetta_prime_regular_kernel_admitted_conversion_v1_decision(
                  admitted_distinct.conversion, &space,
                  cetta_prime_regular_kernel_closed_conversion_profile_v1,
                  &distinct_equal, &distinct_reason) &&
              !distinct_equal && distinct_reason &&
              strcmp(distinct_reason, "distinct-normal-forms") == 0,
          "unequal same-typed operands produce admitted negative evidence");

    TermUniverse foreign_universe;
    Space foreign_space;
    Arena foreign_persistent;
    arena_init(&foreign_persistent);
    arena_set_runtime_kind(
        &foreign_persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&foreign_universe);
    term_universe_set_persistent_arena(&foreign_universe, &arena);
    space_init_with_universe(&foreign_space, &foreign_universe);
    CHECK(!cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
              admitted_beta.conversion, &foreign_space,
              cetta_prime_regular_kernel_closed_conversion_profile_v1),
          "a foreign universe generation cannot consume an admitted judgment");
    CHECK(!cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
              admitted_synthesis.synthesis, &foreign_space,
              cetta_prime_regular_kernel_closed_synthesis_profile_v1),
          "a foreign universe cannot consume admitted synthesis evidence");
    CHECK(!cetta_prime_regular_kernel_admitted_checking_v1_is_current(
              admitted_checking.checking, &foreign_space,
              cetta_prime_regular_kernel_closed_checking_profile_v1),
          "a foreign universe cannot consume admitted checking evidence");
    CettaPrimeRegularKernelBudget generation_budget;
    cetta_prime_regular_kernel_budget_init(&generation_budget, false, 0u);
    CettaPrimeRegularKernelAdmissionResult generation_admission =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            &arena, &foreign_space, distinct_left, distinct_left,
            &generation_budget,
            cetta_prime_regular_kernel_closed_conversion_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(generation_admission.status == CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              generation_admission.conversion,
          "generation invalidation fixture first admits a current judgment");
    CettaPrimeRegularKernelBudget synthesis_generation_budget;
    cetta_prime_regular_kernel_budget_init(
        &synthesis_generation_budget, false, 0u);
    CettaPrimeRegularKernelSynthesisAdmissionResult synthesis_generation_admission =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            &arena, &foreign_space, distinct_left,
            &synthesis_generation_budget,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(synthesis_generation_admission.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              synthesis_generation_admission.synthesis,
          "synthesis generation fixture first admits a current judgment");
    CettaPrimeRegularKernelBudget checking_generation_budget;
    cetta_prime_regular_kernel_budget_init(
        &checking_generation_budget, false, 0u);
    CettaPrimeRegularKernelCheckingAdmissionResult checking_generation_admission =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            &arena, &foreign_space, distinct_left,
            parse_one(&arena, "U1"), &checking_generation_budget,
            cetta_prime_regular_kernel_closed_checking_profile_v1,
            &prime_typing_open_regular_kernel_source_binding_v1);
    CHECK(checking_generation_admission.status ==
              CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED &&
              checking_generation_admission.checking,
          "checking generation fixture first admits a current judgment");
    term_universe_set_persistent_arena(
        &foreign_universe, &foreign_persistent);
    CHECK(!cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
              generation_admission.conversion, &foreign_space,
              cetta_prime_regular_kernel_closed_conversion_profile_v1),
          "a replaced TermUniverse storage generation invalidates admission");
    CHECK(!cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
              synthesis_generation_admission.synthesis, &foreign_space,
              cetta_prime_regular_kernel_closed_synthesis_profile_v1),
          "replaced TermUniverse storage generation invalidates synthesis");
    CHECK(!cetta_prime_regular_kernel_admitted_checking_v1_is_current(
              checking_generation_admission.checking, &foreign_space,
              cetta_prime_regular_kernel_closed_checking_profile_v1),
          "replaced TermUniverse storage generation invalidates checking");
    cetta_prime_regular_kernel_admitted_conversion_v1_free(
        generation_admission.conversion);
    cetta_prime_regular_kernel_admitted_synthesis_v1_free(
        synthesis_generation_admission.synthesis);
    cetta_prime_regular_kernel_admitted_checking_v1_free(
        checking_generation_admission.checking);
    space_free(&foreign_space);
    term_universe_free(&foreign_universe);
    arena_free(&foreign_persistent);

    space_add(&space, atom_symbol(&arena, "AdmissionRevisionMutation"));
    CHECK(!cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
              admitted_beta.conversion, &space,
              cetta_prime_regular_kernel_closed_conversion_profile_v1),
          "a stale Prime authority revision cannot execute an admitted judgment");
    CHECK(!cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
              admitted_synthesis.synthesis, &space,
              cetta_prime_regular_kernel_closed_synthesis_profile_v1),
          "a stale Prime authority revision invalidates admitted synthesis");
    CHECK(!cetta_prime_typed_value_v1_is_current(
              typed_reflexivity, &space) &&
              cetta_prime_typed_value_refl_v1(
                  &arena, &space, typed_reflexivity) == NULL,
          "a stale NIK-hosted Prime typed flow cannot construct another value");
    CHECK(!cetta_prime_regular_kernel_admitted_checking_v1_is_current(
              admitted_checking.checking, &space,
              cetta_prime_regular_kernel_closed_checking_profile_v1),
          "a stale Prime authority revision invalidates admitted checking");
    CettaPrimeRegularKernelCheckingCandidateV1 *stale_erased_candidates = NULL;
    size_t stale_erased_candidate_count = 0u;
    CHECK(!cetta_prime_regular_kernel_checking_bag_v1_erase_established(
              &checking_bag, &space,
              cetta_prime_regular_kernel_closed_checking_profile_v1,
              &arena, &stale_erased_candidates,
              &stale_erased_candidate_count),
          "typed candidate erasure refuses stale checking authority");
    erased_left = NULL;
    erased_right = NULL;
    CHECK(cetta_prime_regular_kernel_admitted_conversion_v1_erase(
              admitted_beta.conversion, &universe, &arena,
              &erased_left, &erased_right) &&
              atom_eq(erased_left, closed_beta_left) &&
              atom_eq(erased_right, closed_beta_right),
          "raw erasure survives authority staleness within the same storage generation");
    cetta_prime_regular_kernel_admitted_conversion_v1_free(
        admitted_distinct.conversion);
    cetta_prime_regular_kernel_admitted_synthesis_v1_free(
        upper_sort_synthesis.synthesis);
    cetta_prime_regular_kernel_admitted_checking_v1_free(
        admitted_mismatch_checking.checking);
    cetta_prime_regular_kernel_admitted_checking_v1_free(
        admitted_checking.checking);
    cetta_prime_regular_kernel_admitted_synthesis_v1_free(
        admitted_synthesis.synthesis);
    cetta_prime_regular_kernel_admitted_conversion_v1_free(
        admitted_beta.conversion);

    Atom *judgment = parse_one(
        &arena, "(type:of (PrimeScoped PrimeCtxNil (Lam U0 (idx 0))))");
    Atom *verdict = judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, judgment, false, 0u)
        : NULL;
    char *verdict_text = verdict ? atom_to_string(&arena, verdict) : NULL;
    CHECK(verdict_status(verdict, "Established"),
          "Prime direct authority routes scoped lambda-pi synthesis");
    CHECK(verdict_text && strstr(verdict_text, "PrimeRegularSynthesis") &&
              strstr(verdict_text, "Certificate") == NULL,
          "ordinary lambda-pi synthesis allocates no proof certificate");

    const char *out_of_class_reflexive_terms[] = {
        "(PrimeScoped PrimeCtxNil Foo)",
        "(PrimeScoped PrimeCtxNil (SomeUserSymbol))",
        "(PrimeScoped PrimeCtxNil (Cons 1 Nil))",
        "(PrimeScoped PrimeCtxNil 42)",
        "(PrimeScoped PrimeCtxNil (Lam U0 Foo))",
        "(PrimeScoped PrimeCtxNil (Pi Foo U0))",
        "(PrimeScoped (PrimeCtxCons Foo PrimeCtxNil) (idx 0))",
        "(PrimeScoped NotAContext (idx 0))",
        "(PrimeScoped PrimeCtxNil (idx 0))",
    };
    for (size_t index = 0u;
         index < sizeof out_of_class_reflexive_terms /
                     sizeof out_of_class_reflexive_terms[0];
         index++) {
        Atom *term = parse_one(&arena, out_of_class_reflexive_terms[index]);
        Atom *items[3] = {atom_symbol(&arena, "type:eq"), term, term};
        Atom *reflexive_judgment = atom_expr(&arena, items, 3u);
        Atom *reflexive_verdict = prime_semantics_judge_typing_direct(
            &arena, &space, reflexive_judgment, false, 0u);
        CHECK(verdict_status(reflexive_verdict, "Established"),
              "out-of-class scoped reflexivity routes without native refutation");
    }

    Atom *mixed_scoped_judgment = parse_one(
        &arena, "(type:eq (PrimeScoped PrimeCtxNil U0) U0)");
    Atom *mixed_scoped_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, mixed_scoped_judgment, false, 0u);
    char *mixed_scoped_text = mixed_scoped_verdict
        ? atom_to_string(&arena, mixed_scoped_verdict) : NULL;
    CHECK(verdict_status(mixed_scoped_verdict, "Undetermined") &&
              mixed_scoped_text &&
              strstr(mixed_scoped_text, "mixed-regular-presentation") &&
              strstr(mixed_scoped_text, "Certificate") == NULL,
          "mixed presentation abstains when no cross-presentation bridge exists");

    Atom *ordinary_judgment = parse_one(&arena, "(type:of 7)");
    Atom *ordinary_verdict = ordinary_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, ordinary_judgment, false, 0u)
        : NULL;
    CHECK(ordinary_verdict &&
              !cetta_prime_regular_kernel_unwrap_scoped(
                  ordinary_judgment->expr.elems[1], NULL, NULL),
          "ordinary non-scoped synthesis retains its established route");

    Atom *formed_type_judgment = parse_one(&arena, "(type:formed (Pi U0 U0))");
    Atom *formed_type_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, formed_type_judgment, false, 0u);
    char *formed_type_text = formed_type_verdict
        ? atom_to_string(&arena, formed_type_verdict) : NULL;
    CHECK(verdict_status(formed_type_verdict, "Established") &&
              formed_type_text &&
              strstr(formed_type_text, "PrimeRegularTypeFormation"),
          "Form consumes admitted synthesis for a closed dependent type");
    Atom *scoped_formed_type_judgment = parse_one(
        &arena, "(type:formed (PrimeScoped PrimeCtxNil U0))");
    Atom *scoped_formed_type_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, scoped_formed_type_judgment, false, 0u);
    char *scoped_formed_type_text = scoped_formed_type_verdict
        ? atom_to_string(&arena, scoped_formed_type_verdict) : NULL;
    CHECK(verdict_status(scoped_formed_type_verdict, "Established") &&
              scoped_formed_type_text &&
              strstr(scoped_formed_type_text,
                     "PrimeRegularTypeFormation") &&
              strstr(scoped_formed_type_text, "Certificate") == NULL,
          "Form directly establishes a contextual regular type");
    Atom *scoped_small_term_judgment = parse_one(
        &arena,
        "(type:formed "
        "  (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0)))");
    Atom *scoped_small_term_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, scoped_small_term_judgment, false, 0u);
    char *scoped_small_term_text = scoped_small_term_verdict
        ? atom_to_string(&arena, scoped_small_term_verdict) : NULL;
    CHECK(verdict_status(scoped_small_term_verdict, "Undetermined") &&
              scoped_small_term_text &&
              strstr(scoped_small_term_text,
                     "PrimeRegularExpectedTypeBoundary"),
          "Form abstains when a contextual small inhabitant is used as a type");
    Atom *upper_sort_form_judgment = parse_one(&arena, "(type:formed U1)");
    Atom *upper_sort_form_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, upper_sort_form_judgment, false, 0u);
    char *upper_sort_form_text = upper_sort_form_verdict
        ? atom_to_string(&arena, upper_sort_form_verdict) : NULL;
    CHECK(verdict_status(upper_sort_form_verdict, "Established"),
          "Form establishes the embedded marker in the cumulative tower");
    if (!verdict_status(upper_sort_form_verdict, "Established") &&
        upper_sort_form_text)
        fprintf(stderr, "upper sort formation: %s\n", upper_sort_form_text);
    Atom *limited_form_judgment = parse_one(
        &arena, "(type:formed (Pi (Pi U0 U0) (Pi U0 U0)))");
    Atom *limited_form_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, limited_form_judgment, true, 1u);
    CHECK(verdict_status(limited_form_verdict, "Incomplete"),
          "native Form budget exhaustion does not retry through legacy");

    space_add(
        &space, atom_symbol(&arena, "ProductionCounterRevision"));

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif

    Atom *closed_synth_judgment = parse_one(
        &arena, "(type:of (Lam U0 (idx 0)))");
    Atom *closed_synth_verdict = closed_synth_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, closed_synth_judgment, false, 0u)
        : NULL;
    char *closed_synth_text = closed_synth_verdict
        ? atom_to_string(&arena, closed_synth_verdict) : NULL;
    CHECK(verdict_status(closed_synth_verdict, "Established") &&
              closed_synth_text &&
              strstr(closed_synth_text,
                     "PrimeRegularSynthesis (Pi U0 U0)") &&
              strstr(closed_synth_text, "Certificate") == NULL,
          "ordinary closed Synth consumes native admitted evidence");
    Atom *closed_synth_cached = prime_semantics_judge_typing_direct(
        &arena, &space, closed_synth_judgment, false, 0u);
    CHECK(verdict_status(closed_synth_cached, "Established"),
          "repeated closed Synth consumes cached evidence");

    Atom *upper_sort_judgment = parse_one(&arena, "(type:of U1)");
    Atom *upper_sort_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, upper_sort_judgment, false, 0u);
    CHECK(verdict_status(upper_sort_verdict, "Established"),
          "native closed Synth returns the successor universe of U1");

    Atom *loose_synth_judgment = parse_one(
        &arena, "(type:of (idx 0))");
    Atom *loose_synth_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, loose_synth_judgment, false, 0u);
    CHECK(verdict_status(loose_synth_verdict, "Undetermined"),
          "loose Synth fails open to the unchanged legacy route");

    Atom *ordinary_synth_judgment = parse_one(&arena, "(type:of 7)");
    Atom *ordinary_synth_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, ordinary_synth_judgment, false, 0u);
    CHECK(verdict_status(ordinary_synth_verdict, "Established"),
          "non-lambda-pi Synth retains its legacy result");

    Atom *limited_synth_judgment = parse_one(
        &arena, "(type:of (Pi U0 U0))");
    Atom *limited_synth_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, limited_synth_judgment, true, 1u);
    CHECK(verdict_status(limited_synth_verdict, "Incomplete"),
          "native Synth budget exhaustion does not retry through legacy");

    Atom *closed_check_judgment = parse_one(
        &arena, "(type:check (Lam U0 (idx 0)) (Pi U0 U0))");
    Atom *closed_check_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, closed_check_judgment, false, 0u);
    char *closed_check_text = closed_check_verdict
        ? atom_to_string(&arena, closed_check_verdict) : NULL;
    CHECK(verdict_status(closed_check_verdict, "Established") &&
              closed_check_text &&
              strstr(closed_check_text, "PrimeRegularChecked") &&
              strstr(closed_check_text, "Certificate") == NULL,
          "ordinary closed Check consumes native admitted evidence");
    Atom *closed_check_cached = prime_semantics_judge_typing_direct(
        &arena, &space, closed_check_judgment, false, 0u);
    CHECK(verdict_status(closed_check_cached, "Established"),
          "repeated closed Check consumes cached admitted evidence");

    Atom *mismatch_check_judgment = parse_one(
        &arena, "(type:check (Lam U0 (idx 0)) U0)");
    Atom *mismatch_check_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, mismatch_check_judgment, false, 0u);
    CHECK(verdict_status(mismatch_check_verdict, "Refuted"),
          "native closed Check retains decisive negative evidence");

    Atom *closed_analyze_judgment = parse_one(
        &arena, "(type:analyze (Lam U0 (idx 0)) (Pi U0 U0))");
    Atom *closed_analyze_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, closed_analyze_judgment, false, 0u);
    char *closed_analyze_text = closed_analyze_verdict
        ? atom_to_string(&arena, closed_analyze_verdict) : NULL;
    CHECK(verdict_status(closed_analyze_verdict, "Established") &&
              closed_analyze_text &&
              strstr(closed_analyze_text, "PrimeRegularAnalyzed"),
          "Analyze consumes the same exact native judgment on the closed fragment");

    Atom *limited_check_judgment = parse_one(
        &arena,
        "(type:check (Lam (Pi U0 U0) (idx 0)) "
        "       (Pi (Pi U0 U0) (Pi U0 U0)))");
    Atom *limited_check_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, limited_check_judgment, true, 1u);
    CHECK(verdict_status(limited_check_verdict, "Incomplete"),
          "native Check budget exhaustion does not retry through legacy");

    Atom *loose_check_judgment = parse_one(
        &arena, "(type:check (idx 0) U0)");
    Atom *loose_check_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, loose_check_judgment, false, 0u);
    CHECK(verdict_status(loose_check_verdict, "Undetermined"),
          "loose Check fails open to the unchanged legacy route");

    Atom *ordinary_check_judgment = parse_one(
        &arena, "(type:check 7 Number)");
    Atom *ordinary_check_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, ordinary_check_judgment, false, 0u);
    CHECK(verdict_status(ordinary_check_verdict, "Established"),
          "non-lambda-pi Check retains its legacy result");

    Atom *closed_beta_judgment = parse_one(
        &arena,
        "(type:eq "
        "  (App (Lam (Pi U0 U0) (idx 0)) (Lam U0 (idx 0))) "
        "  (Lam U0 (idx 0)))");
    Atom *closed_beta_verdict = closed_beta_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, closed_beta_judgment, false, 0u)
        : NULL;
    char *closed_beta_verdict_text = closed_beta_verdict
        ? atom_to_string(&arena, closed_beta_verdict) : NULL;
    CHECK(verdict_status(closed_beta_verdict, "Established") &&
              closed_beta_verdict_text &&
              strstr(closed_beta_verdict_text,
                     "PrimeBetaEtaEqual") &&
              strstr(closed_beta_verdict_text, "Certificate") == NULL,
          "ordinary closed lambda-pi Convert consumes native admission without a certificate");
    Atom *closed_beta_cached_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, closed_beta_judgment, false, 0u);
    CHECK(verdict_status(closed_beta_cached_verdict, "Established"),
          "a repeated closed Convert consumes its cached admitted judgment");

    Atom *closed_distinct_judgment = parse_one(
        &arena, "(type:eq U0 (Pi U0 U0))");
    Atom *closed_distinct_verdict = closed_distinct_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, closed_distinct_judgment, false, 0u)
        : NULL;
    char *closed_distinct_text = closed_distinct_verdict
        ? atom_to_string(&arena, closed_distinct_verdict) : NULL;
    CHECK(verdict_status(closed_distinct_verdict, "Refuted") &&
              closed_distinct_text &&
              strstr(closed_distinct_text, "distinct-normal-forms") &&
              strstr(closed_distinct_text, "Certificate") == NULL,
          "ordinary unequal native operands use admitted negative evidence");

    Atom *loose_convert_judgment = parse_one(
        &arena, "(type:eq (idx 0) (idx 0))");
    Atom *loose_convert_verdict = loose_convert_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, loose_convert_judgment, false, 0u)
        : NULL;
    char *loose_convert_text = loose_convert_verdict
        ? atom_to_string(&arena, loose_convert_verdict) : NULL;
    CHECK(verdict_status(loose_convert_verdict, "Undetermined") &&
              loose_convert_text &&
              strstr(loose_convert_text, "PrimeConversionCertificateV1") == NULL,
          "ill-scoped regular operands abstain without importing legacy equality");

    Atom *ordinary_convert_judgment = parse_one(&arena, "(type:eq 7 7)");
    Atom *ordinary_convert_verdict = ordinary_convert_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, ordinary_convert_judgment, false, 0u)
        : NULL;
    char *ordinary_convert_text = ordinary_convert_verdict
        ? atom_to_string(&arena, ordinary_convert_verdict) : NULL;
    CHECK(verdict_status(ordinary_convert_verdict, "Established") &&
              ordinary_convert_text &&
              strstr(ordinary_convert_text,
                     "PrimeConversionCertificateV1"),
          "non-lambda-pi Convert retains byte-compatible legacy evidence");

#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats conversion_stats;
    cetta_runtime_stats_snapshot(&conversion_stats);
    cetta_runtime_stats_disable();
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ATTEMPT] ==
              3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_CHECK] == 2u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ACCEPTED] ==
              3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_DECLINED] ==
              0u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_BUDGET_EXHAUSTED] ==
              0u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ENGINE_FAILURE] ==
              0u,
          "Convert accounting separates admission, decline, budget, and engine paths");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_EXECUTION] == 3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_INTERIOR_CHECK] == 0u,
          "admitted Convert executes three times with zero interior checks");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_CACHE_MISS] == 2u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_CACHE_HIT] == 1u,
          "repeated Convert records one revision-keyed zero-check cache hit");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION] == 1u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_CONVERSION_CERTIFICATE_CONSTRUCTION] ==
              1u,
          "only the non-lambda-pi control reaches HE and certificate construction");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ATTEMPT] ==
              4u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_CHECK] == 2u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ACCEPTED] ==
              3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_DECLINED] ==
              0u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_BUDGET_EXHAUSTED] ==
              1u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ENGINE_FAILURE] ==
              0u,
          "Synth accounting separates admitted, declined, budget, and engine paths");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_EXECUTION] == 3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_INTERIOR_CHECK] == 0u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_HIT] == 1u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_MISS] == 3u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_SYNTHESIS] == 1u,
          "Synth cache executes without interior checks and preserves fallback counts");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ATTEMPT] ==
              6u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_CHECK] == 2u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ACCEPTED] ==
              4u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_DECLINED] ==
              1u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_BUDGET_EXHAUSTED] ==
              1u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ENGINE_FAILURE] ==
              0u,
          "Check accounting separates admitted, declined, budget, and engine paths");
    CHECK(conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_EXECUTION] == 4u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_INTERIOR_CHECK] == 0u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_CACHE_HIT] == 2u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_CACHE_MISS] == 4u &&
              conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING] == 1u,
          "Check/Analyze/May/Must share cached authority without interior checks");
#endif

    Atom *declared_identity = parse_one(
        &arena, "(: declared-identity (-> u0 u0))");
    Atom *declared_other = parse_one(
        &arena, "(: declared-other (-> u0 u0))");
    Atom *declared_small = parse_one(
        &arena, "(: declared-small u0)");
    Atom *declared_point = parse_one(
        &arena, "(: declared-point u0)");
    Atom *declared_refl = parse_one(
        &arena,
        "(: declared-refl (id u0 declared-point declared-point))");
    Atom *declared_constructed_refl = parse_one(
        &arena, "(refl declared-point)");
    Atom *declared_tower_type = parse_one(
        &arena, "(: declared-tower-type u1)");
    Atom *declared_tower_value = parse_one(
        &arena, "(: declared-tower-value declared-tower-type)");
    Atom *declared_level_poly = parse_one(
        &arena,
        "(: declared-level-poly (-> (A : (u $level)) (-> A A)))");
    Atom *declared_level_pair = parse_one(
        &arena,
        "(: declared-level-pair "
        "  (-> (A : (u $left-level)) "
        "      (B : (u $right-level)) "
        "      (-> A (-> B A))))");
    Atom *declared_level_escape = parse_one(
        &arena,
        "(: declared-level-escape "
        "  (-> (A : (u $level)) (-> $level A)))");
    Atom *declared_rel = parse_one(
        &arena,
        "(: declared-rel "
        "  (-> (s : (u $carrier-level)) "
        "      (p : (-> s (-> s (u $evidence-level)))) "
        "      (source : s) (target : s) (u $evidence-level)))");
    Atom *declared_hyp_primitive = parse_one(
        &arena,
        "(: declared-hyp:primitive "
        "  (-> (s : (u $carrier-level)) "
        "      (p : (-> s (-> s (u $evidence-level)))) "
        "      (source : s) (target : s) "
        "      (evidence : (p source target)) "
        "      (declared-rel s p source target)))");
    Atom *declared_edge = parse_one(
        &arena, "(: declared-edge (-> u0 (-> u0 u1)))");
    Atom *declared_source = parse_one(
        &arena, "(: declared-source u0)");
    Atom *declared_target = parse_one(
        &arena, "(: declared-target u0)");
    Atom *declared_edge_evidence = parse_one(
        &arena,
        "(: declared-edge-evidence "
        "   (declared-edge declared-source declared-target))");
    Atom *declared_broken = parse_one(
        &arena, "(: declared-broken (id u0 missing missing))");
    Atom *declared_cycle_left = parse_one(
        &arena,
        "(: declared-cycle-left "
        "  (id u0 declared-cycle-right declared-cycle-right))");
    Atom *declared_cycle_right = parse_one(
        &arena,
        "(: declared-cycle-right "
        "  (id u0 declared-cycle-left declared-cycle-left))");
    Atom *scoped_identity_rule = parse_one(
        &arena,
        "(= (aggregate-scoped-identity) "
        "   (PrimeScoped PrimeCtxNil (Lam U0 (idx 0))))");
    Atom *declared_dependent_rule = parse_one(
        &arena,
        "(= (declared-dependent-proof) (refl declared-point))");
    CHECK(declared_identity && declared_other && declared_small &&
              declared_point && declared_refl && declared_constructed_refl &&
              declared_tower_type &&
              declared_tower_value && declared_level_poly &&
              declared_level_pair && declared_level_escape && declared_rel &&
              declared_hyp_primitive && declared_edge && declared_source &&
              declared_target && declared_edge_evidence && declared_broken &&
              declared_cycle_left && declared_cycle_right &&
              scoped_identity_rule && declared_dependent_rule,
          "producer-bound native checking fixtures parse");
    if (declared_identity) space_add(&space, declared_identity);
    if (declared_other) space_add(&space, declared_other);
    if (declared_small) space_add(&space, declared_small);
    if (declared_point) space_add(&space, declared_point);
    if (declared_refl) space_add(&space, declared_refl);
    if (declared_tower_type) space_add(&space, declared_tower_type);
    if (declared_tower_value) space_add(&space, declared_tower_value);
    if (declared_level_poly) space_add(&space, declared_level_poly);
    if (declared_level_pair) space_add(&space, declared_level_pair);
    if (declared_level_escape) space_add(&space, declared_level_escape);
    if (declared_rel) space_add(&space, declared_rel);
    if (declared_hyp_primitive)
        space_add(&space, declared_hyp_primitive);
    if (declared_edge) space_add(&space, declared_edge);
    if (declared_source) space_add(&space, declared_source);
    if (declared_target) space_add(&space, declared_target);
    if (declared_edge_evidence)
        space_add(&space, declared_edge_evidence);
    if (declared_broken) space_add(&space, declared_broken);
    if (declared_cycle_left) space_add(&space, declared_cycle_left);
    if (declared_cycle_right) space_add(&space, declared_cycle_right);
    if (scoped_identity_rule) space_add(&space, scoped_identity_rule);
    if (declared_dependent_rule) space_add(&space, declared_dependent_rule);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    Atom *declared_reflexive_conversion = parse_one(
        &arena, "(type:eq declared-identity declared-identity)");
    Atom *declared_distinct_conversion = parse_one(
        &arena, "(type:eq declared-identity declared-other)");
    Atom *declared_application_conversion = parse_one(
        &arena,
        "(type:eq (declared-identity declared-small) "
        "         (declared-identity declared-small))");
    Atom *declared_dependent_conversion = parse_one(
        &arena, "(type:eq declared-refl declared-refl)");
    Atom *declared_reflexive_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_reflexive_conversion, false, 0u);
    Atom *declared_distinct_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_distinct_conversion, false, 0u);
    Atom *declared_application_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_application_conversion, false, 0u);
    Atom *declared_dependent_conversion_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_conversion, false, 0u);
    Atom *declared_limited_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_application_conversion, true, 4u);
    char *declared_reflexive_text = declared_reflexive_verdict
        ? atom_to_string(&arena, declared_reflexive_verdict) : NULL;
    char *declared_distinct_text = declared_distinct_verdict
        ? atom_to_string(&arena, declared_distinct_verdict) : NULL;
    CHECK(verdict_status(declared_reflexive_verdict, "Established") &&
              verdict_status(declared_application_verdict, "Established") &&
              verdict_status(
                  declared_dependent_conversion_verdict, "Established") &&
              declared_reflexive_text &&
              strstr(declared_reflexive_text, "PrimeBetaEtaEqual") &&
              strstr(declared_reflexive_text, "Certificate") == NULL,
          "declared regular conversion uses native positive evidence");
    CHECK(verdict_status(declared_distinct_verdict, "Refuted") &&
              declared_distinct_text &&
              strstr(declared_distinct_text, "distinct-normal-forms") &&
              strstr(declared_distinct_text, "Certificate") == NULL,
          "declared regular conversion retains checked negative evidence");
    CHECK(verdict_status(declared_limited_verdict, "Incomplete"),
          "declared regular conversion keeps resource exhaustion native");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats declared_conversion_stats;
    cetta_runtime_stats_snapshot(&declared_conversion_stats);
    cetta_runtime_stats_disable();
    CHECK(declared_conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_DECLARED_REGULAR_CONVERSION_EXECUTION] ==
              4u &&
              declared_conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION] == 0u &&
              declared_conversion_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_CONVERSION_CERTIFICATE_CONSTRUCTION] ==
              0u,
          "declared regular conversion never consults HE or constructs a certificate");
#endif

    Atom *declared_dependent_synthesis = parse_one(
        &arena, "(type:of declared-refl)");
    Atom *declared_constructed_synthesis = parse_one(
        &arena, "(type:of (refl declared-point))");
    Atom *declared_dependent_check = parse_one(
        &arena,
        "(type:check declared-refl "
        "  (id u0 declared-point declared-point))");
    Atom *declared_constructed_check = parse_one(
        &arena,
        "(type:check (refl declared-point) "
        "  (id u0 declared-point declared-point))");
    Atom *declared_constructed_analyze = parse_one(
        &arena,
        "(type:analyze (refl declared-point) "
        "  (id u0 declared-point declared-point))");
    Atom *declared_dependent_formation = parse_one(
        &arena, "(type:formed (id u0 declared-point declared-point))");
    Atom *declared_dependent_mismatch = parse_one(
        &arena,
        "(type:check declared-refl "
        "  (id u0 declared-small declared-small))");
    Atom *declared_constructed_conversion = parse_one(
        &arena,
        "(type:eq (refl declared-point) (refl declared-point))");
    Atom *declared_tower_synthesis = parse_one(
        &arena, "(type:of declared-tower-value)");
    Atom *declared_broken_synthesis = parse_one(
        &arena, "(type:of declared-broken)");
    Atom *declared_cycle_synthesis = parse_one(
        &arena, "(type:of declared-cycle-left)");
    Atom *declared_dependent_synthesis_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_synthesis, false, 0u);
    Atom *declared_constructed_synthesis_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_synthesis, false, 0u);
    Atom *declared_dependent_check_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_check, false, 0u);
    Atom *declared_constructed_check_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_check, false, 0u);
    Atom *declared_dependent_formation_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_formation, false, 0u);
    Atom *declared_dependent_mismatch_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_mismatch, false, 0u);
    Atom *declared_constructed_conversion_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_conversion, false, 0u);
    Atom *declared_dependent_limited_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_dependent_synthesis, true, 1u);
    Atom *declared_tower_synthesis_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_tower_synthesis, false, 0u);
    Atom *declared_broken_synthesis_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_broken_synthesis, false, 0u);
    Atom *declared_cycle_synthesis_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_cycle_synthesis, false, 0u);
    char *declared_dependent_synthesis_text =
        declared_dependent_synthesis_verdict
        ? atom_to_string(&arena, declared_dependent_synthesis_verdict)
        : NULL;
    char *declared_constructed_synthesis_text =
        declared_constructed_synthesis_verdict
        ? atom_to_string(&arena, declared_constructed_synthesis_verdict)
        : NULL;
    char *declared_constructed_conversion_text =
        declared_constructed_conversion_verdict
        ? atom_to_string(&arena, declared_constructed_conversion_verdict)
        : NULL;
    char *declared_tower_synthesis_text = declared_tower_synthesis_verdict
        ? atom_to_string(&arena, declared_tower_synthesis_verdict)
        : NULL;
    char *declared_broken_synthesis_text = declared_broken_synthesis_verdict
        ? atom_to_string(&arena, declared_broken_synthesis_verdict)
        : NULL;
    char *declared_cycle_synthesis_text = declared_cycle_synthesis_verdict
        ? atom_to_string(&arena, declared_cycle_synthesis_verdict)
        : NULL;
    CHECK(verdict_status(
              declared_dependent_synthesis_verdict, "Established") &&
              verdict_status(declared_dependent_check_verdict, "Established") &&
              verdict_status(
                  declared_dependent_formation_verdict, "Established") &&
              declared_dependent_synthesis_text &&
              strstr(
                  declared_dependent_synthesis_text,
                  "(Id U0 declared-point declared-point)") &&
              strstr(
                  declared_dependent_synthesis_text,
                  "PrimeRegularDeclaredSynthesis"),
          "acyclic value-indexed declarations form, synthesize, and check natively");
    CHECK(verdict_status(
              declared_constructed_synthesis_verdict, "Established") &&
              verdict_status(
                  declared_constructed_check_verdict, "Established") &&
              verdict_status(
                  declared_constructed_conversion_verdict, "Established") &&
              declared_constructed_synthesis_text &&
              strstr(
                  declared_constructed_synthesis_text,
                  "(Id U0 declared-point declared-point)") &&
              strstr(
                  declared_constructed_synthesis_text,
                  "PrimeRegularDeclaredSynthesis") &&
              declared_constructed_conversion_text &&
              strstr(declared_constructed_conversion_text,
                     "PrimeBetaEtaEqual") &&
              strstr(declared_constructed_conversion_text,
                     "Certificate") == NULL,
          "constructed value-indexed evidence uses the declaration-aware authority before the context-free authored authority");
    CettaPrimeTypingSynthesisCandidateV1 declared_constructed_synth_candidate = {
        .term = declared_constructed_refl,
    };
    CettaPrimeTypingCheckingCandidateV1 declared_constructed_check_candidate = {
        .term = declared_constructed_refl,
        .expected_type = parse_one(
            &arena,
            "(id u0 declared-point declared-point)"),
    };
    CettaPrimeTypingFormationCandidateV1 declared_dependent_form_candidate = {
        .type = declared_constructed_check_candidate.expected_type,
    };
    CettaPrimeTypingSynthesisObservationV1 declared_constructed_synth_obs;
    CettaPrimeTypingCheckingObservationV1 declared_constructed_check_obs;
    CettaPrimeTypingFormationObservationV1 declared_dependent_form_obs;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    Atom *declared_constructed_conversion_observed =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_conversion, false, 0u);
    Atom *declared_constructed_analyze_observed =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_analyze, false, 0u);
    CHECK(cetta_prime_typing_observe_synthesis_v1(
              &arena, &space, &declared_constructed_synth_candidate,
              &declared_constructed_synth_obs) &&
              cetta_prime_typing_observe_checking_v1(
                  &arena, &space, &declared_constructed_check_candidate,
                  &declared_constructed_check_obs) &&
              cetta_prime_typing_observe_formation_v1(
                  &arena, &space, &declared_dependent_form_candidate,
                  &declared_dependent_form_obs) &&
              declared_constructed_synth_obs.authority.route ==
                  CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR &&
              declared_constructed_check_obs.authority.route ==
                  CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR &&
              declared_dependent_form_obs.authority.route ==
                  CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR &&
              verdict_status(
                  declared_constructed_conversion_observed, "Established") &&
              verdict_status(
                  declared_constructed_analyze_observed, "Established"),
          "formation, synthesis, and checking expose one declaration-aware route for constructed dependent evidence");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats declared_constructed_stats;
    cetta_runtime_stats_snapshot(&declared_constructed_stats);
    cetta_runtime_stats_disable();
    CHECK(declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_SYNTHESIS] == 0u &&
              declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING] == 0u &&
              declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_FORMATION] == 0u &&
              declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION] == 0u &&
              declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR] ==
                  2u &&
              declared_constructed_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_DECLARED_REGULAR_CONVERSION_EXECUTION] ==
                  1u,
          "constructed dependent evidence reaches no HE or ambient typing route");
#endif
    CHECK(verdict_status(
              declared_dependent_mismatch_verdict, "Refuted"),
          "dependent declared checking retains a checked mismatch obstruction");
    CHECK(verdict_status(declared_dependent_limited_verdict, "Incomplete"),
          "dependent declaration recognition charges its structural work");
    CHECK(verdict_status(declared_tower_synthesis_verdict, "Established") &&
              declared_tower_synthesis_text &&
              strstr(
                  declared_tower_synthesis_text,
                  "PrimeRegularDeclaredSynthesis") != NULL &&
              declared_broken_synthesis_text &&
              strstr(
                  declared_broken_synthesis_text,
                  "PrimeRegularDeclaredSynthesis") == NULL &&
              declared_cycle_synthesis_text &&
              strstr(
                  declared_cycle_synthesis_text,
                  "PrimeRegularDeclaredSynthesis") == NULL,
          "tower declarations are native while unresolved and cyclic graphs remain ambient");

    Atom *declared_level_zero_judgment = parse_one(
        &arena, "(type:of (declared-level-poly u0))");
    Atom *declared_level_one_judgment = parse_one(
        &arena, "(type:of (declared-level-poly u1))");
    Atom *declared_level_zero_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_level_zero_judgment, false, 0u);
    Atom *declared_level_one_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_level_one_judgment, false, 0u);
    char *declared_level_zero_text = declared_level_zero_verdict
        ? atom_to_string(&arena, declared_level_zero_verdict) : NULL;
    char *declared_level_one_text = declared_level_one_verdict
        ? atom_to_string(&arena, declared_level_one_verdict) : NULL;
    if (!verdict_status(declared_level_zero_verdict, "Established") ||
        !verdict_status(declared_level_one_verdict, "Established"))
        fprintf(
            stderr, "level schema verdicts: %s | %s\n",
            declared_level_zero_text ? declared_level_zero_text : "<null>",
            declared_level_one_text ? declared_level_one_text : "<null>");
    CHECK(verdict_status(declared_level_zero_verdict, "Established") &&
              verdict_status(declared_level_one_verdict, "Established") &&
              declared_level_zero_text && declared_level_one_text &&
              strstr(declared_level_zero_text, "(Pi U0 U0)") &&
              strstr(declared_level_one_text, "(Pi U1 U1)") &&
              strstr(declared_level_zero_text,
                     "PrimeRegularDeclaredSynthesis") &&
              strstr(declared_level_one_text,
                     "PrimeRegularDeclaredSynthesis"),
          "declaration-local universe schemas instantiate through the native tower");

    Atom *declared_level_pair_judgment = parse_one(
        &arena, "(type:of (declared-level-pair u0 u1))");
    Atom *declared_level_escape_judgment = parse_one(
        &arena, "(type:of declared-level-escape)");
    Atom *declared_level_pair_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_level_pair_judgment, false, 0u);
    Atom *declared_level_escape_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_level_escape_judgment, false, 0u);
    char *declared_level_pair_text = declared_level_pair_verdict
        ? atom_to_string(&arena, declared_level_pair_verdict) : NULL;
    char *declared_level_escape_text = declared_level_escape_verdict
        ? atom_to_string(&arena, declared_level_escape_verdict) : NULL;
    if (!verdict_status(declared_level_pair_verdict, "Established") ||
        !declared_level_pair_text ||
        !strstr(declared_level_pair_text, "(Pi U0 (Pi U1 U0))") ||
        !strstr(declared_level_pair_text, "PrimeRegularDeclaredSynthesis"))
        fprintf(
            stderr, "two-parameter schema verdict: %s\n",
            declared_level_pair_text ? declared_level_pair_text : "<null>");
    CHECK(verdict_status(declared_level_pair_verdict, "Established") &&
              declared_level_pair_text &&
              strstr(declared_level_pair_text, "(Pi U0 (Pi U1 U0))") &&
              strstr(
                  declared_level_pair_text,
                  "PrimeRegularDeclaredSynthesis"),
          "distinct declaration-level parameters instantiate independently");
    CHECK(declared_level_escape_text &&
              strstr(
                  declared_level_escape_text,
                  "PrimeRegularDeclaredSynthesis") == NULL,
          "a universe parameter escaping its level position remains outside the native declaration fragment");

    Atom *declared_polymorphic_dependency_judgment = parse_one(
        &arena,
        "(type:of "
        "  (declared-hyp:primitive "
        "    u0 declared-edge declared-source declared-target "
        "    declared-edge-evidence))");
    Atom *declared_polymorphic_dependency_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_polymorphic_dependency_judgment,
            false, 0u);
    char *declared_polymorphic_dependency_text =
        declared_polymorphic_dependency_verdict
        ? atom_to_string(
              &arena, declared_polymorphic_dependency_verdict)
        : NULL;
    if (!verdict_status(
            declared_polymorphic_dependency_verdict, "Established"))
        fprintf(
            stderr, "polymorphic dependency verdict: %s\n",
            declared_polymorphic_dependency_text
                ? declared_polymorphic_dependency_text : "<null>");
    CHECK(verdict_status(
              declared_polymorphic_dependency_verdict, "Established") &&
              declared_polymorphic_dependency_text &&
              strstr(
                  declared_polymorphic_dependency_text,
                  "PrimeRegularDeclaredSynthesis") &&
              strstr(
                  declared_polymorphic_dependency_text,
                  "declared-rel"),
          "a polymorphic declaration may depend parametrically on another polymorphic declaration");

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    Atom *declared_level_two_occurrences_judgment = parse_one(
        &arena,
        "(type:check "
        "  (pair (declared-level-poly u0) (declared-level-poly u1)) "
        "  (sigma (_ : (-> u0 u0)) (-> u1 u1)))");
    Atom *declared_level_two_occurrences_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_level_two_occurrences_judgment,
            false, 0u);
    CHECK(verdict_status(
              declared_level_two_occurrences_verdict, "Established"),
          "two occurrences of one universe schema choose independent native instances");
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats declared_level_occurrence_stats;
    cetta_runtime_stats_snapshot(&declared_level_occurrence_stats);
    cetta_runtime_stats_disable();
    bool declared_level_occurrence_accounting =
        declared_level_occurrence_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_POLYMORPHIC_LOOKUP] ==
            1u &&
        declared_level_occurrence_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_PARAMETER_FRESH] ==
            2u &&
        declared_level_occurrence_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_INSTANCE] == 2u &&
        declared_level_occurrence_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT] == 2u;
    if (!declared_level_occurrence_accounting)
        fprintf(
            stderr,
            "level occurrence counters: lookup=%llu fresh=%llu "
            "instance=%llu constraint=%llu\n",
            (unsigned long long)declared_level_occurrence_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_POLYMORPHIC_LOOKUP],
            (unsigned long long)declared_level_occurrence_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_PARAMETER_FRESH],
            (unsigned long long)declared_level_occurrence_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_INSTANCE],
            (unsigned long long)declared_level_occurrence_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT]);
    CHECK(declared_level_occurrence_accounting,
          "fresh polymorphic lookup, parameters, instances, and constraints are accounted exactly");
#endif

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    Atom *scoped_may_judgment = parse_one(
        &arena,
        "(type:may (PrimeEvaluate (aggregate-scoped-identity)) "
        "          (Pi U0 U0))");
    Atom *scoped_must_judgment = parse_one(
        &arena,
        "(type:must (PrimeEvaluate (aggregate-scoped-identity)) "
        "           (Pi U0 U0))");
    Atom *declared_may_judgment = parse_one(
        &arena,
        "(type:may (PrimeEvaluate declared-identity) (-> u0 u0))");
    Atom *declared_must_judgment = parse_one(
        &arena,
        "(type:must (PrimeEvaluate declared-identity) (-> u0 u0))");
    Atom *scoped_must_mismatch_judgment = parse_one(
        &arena,
        "(type:must (PrimeEvaluate (aggregate-scoped-identity)) U0)");
    Atom *declared_may_mismatch_judgment = parse_one(
        &arena,
        "(type:may (PrimeEvaluate declared-identity) u0)");
    Atom *declared_constructed_may_judgment = parse_one(
        &arena,
        "(type:may (PrimeEvaluate (declared-dependent-proof)) "
        "          (id u0 declared-point declared-point))");
    Atom *declared_constructed_must_judgment = parse_one(
        &arena,
        "(type:must (PrimeEvaluate (declared-dependent-proof)) "
        "           (id u0 declared-point declared-point))");
    Atom *scoped_may_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, scoped_may_judgment, false, 0u);
    Atom *scoped_must_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, scoped_must_judgment, false, 0u);
    Atom *declared_may_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_may_judgment, false, 0u);
    Atom *declared_must_verdict = prime_semantics_judge_typing_direct(
        &arena, &space, declared_must_judgment, false, 0u);
    Atom *scoped_must_mismatch_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, scoped_must_mismatch_judgment, false, 0u);
    Atom *declared_may_mismatch_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_may_mismatch_judgment, false, 0u);
    Atom *declared_constructed_may_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_may_judgment, false, 0u);
    Atom *declared_constructed_must_verdict =
        prime_semantics_judge_typing_direct(
            &arena, &space, declared_constructed_must_judgment, false, 0u);
    char *scoped_may_text = scoped_may_verdict
        ? atom_to_string(&arena, scoped_may_verdict) : NULL;
    char *declared_may_text = declared_may_verdict
        ? atom_to_string(&arena, declared_may_verdict) : NULL;
    CHECK(verdict_status(scoped_may_verdict, "Established") &&
              verdict_status(scoped_must_verdict, "Established") &&
              scoped_may_text &&
              strstr(scoped_may_text, "PrimeRegularChecked"),
          "producer-bound scoped values use the native checking authority");
    CHECK(verdict_status(declared_may_verdict, "Established") &&
              verdict_status(declared_must_verdict, "Established") &&
              declared_may_text &&
              strstr(declared_may_text, "PrimeRegularDeclaredChecked"),
          "producer-bound declared values use the native checking authority");
    CHECK(verdict_status(scoped_must_mismatch_verdict, "Refuted") &&
              verdict_status(declared_may_mismatch_verdict, "Refuted"),
          "producer-bound native checking retains checked negative evidence");
    CHECK(verdict_status(
              declared_constructed_may_verdict, "Established") &&
              verdict_status(
                  declared_constructed_must_verdict, "Established"),
          "producer-bound aggregates preserve declaration context for constructed dependent evidence");
    if (!verdict_status(declared_may_verdict, "Established") ||
        !verdict_status(declared_must_verdict, "Established") ||
        !declared_may_text ||
        !strstr(declared_may_text, "PrimeRegularDeclaredChecked")) {
        fprintf(stderr, "declared May: ");
        atom_print(declared_may_verdict, stderr);
        fprintf(stderr, "\ndeclared Must: ");
        atom_print(declared_must_verdict, stderr);
        fputc('\n', stderr);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats aggregate_stats;
    cetta_runtime_stats_snapshot(&aggregate_stats);
    cetta_runtime_stats_disable();
    CHECK(aggregate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR] == 3u &&
              aggregate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR] == 5u,
          "May and Must select scoped and declared native routes once per value occurrence");
    CHECK(aggregate_stats.counters[
              CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING] == 0u,
          "producer-bound native values never consult HE checking");
    if (aggregate_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR] != 3u ||
        aggregate_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR] != 5u ||
        aggregate_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING] != 0u) {
        fprintf(
            stderr,
            "producer-bound routes: scoped=%llu authored=%llu declared=%llu "
            "closed=%llu formation=%llu legacy=%llu\n",
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR],
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AUTHORED_REGULAR],
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR],
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR],
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AMBIENT_FORMATION],
            (unsigned long long)aggregate_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING]);
    }
#endif

    Atom *semantic_mismatch_type = parse_one(&arena, "U0");
    Atom *semantic_legacy_term = parse_one(&arena, "7");
    Atom *semantic_legacy_type = parse_one(&arena, "Number");
    Atom *semantic_outside_type = parse_one(
        &arena, "(PCollection CSet LNil RNone)");
    CettaPrimeTypingCheckingCandidateV1 semantic_candidates[] = {
        {closed_beta_right, closed_identity_type, false, 0u},
        {closed_beta_right, closed_identity_type, false, 0u},
        {closed_beta_right, semantic_mismatch_type, false, 0u},
        {checking_candidate_loose, semantic_mismatch_type, false, 0u},
        {checking_candidate_large, checking_candidate_large_type, true, 1u},
        {semantic_legacy_term, semantic_legacy_type, false, 0u},
        {semantic_legacy_term, semantic_outside_type, false, 0u},
    };
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif
    CettaPrimeTypingCheckingBagV1 semantic_bag;
    CHECK(cetta_prime_typing_observe_checking_bag_v1(
              &arena, &space, semantic_candidates,
              sizeof semantic_candidates / sizeof semantic_candidates[0],
              &semantic_bag),
          "semantic waist observes every candidate occurrence exactly once");
    CHECK(semantic_bag.established_count == 3u &&
              semantic_bag.refuted_count == 1u &&
              semantic_bag.undetermined_count == 2u &&
              semantic_bag.incomplete_count == 1u,
          "semantic waist preserves the four checking statuses as staged data");
    CHECK(semantic_bag.occurrences[0].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              semantic_bag.occurrences[1].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              semantic_bag.occurrences[2].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_REFUTED &&
              semantic_bag.occurrences[3].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
              semantic_bag.occurrences[4].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_INCOMPLETE &&
              semantic_bag.occurrences[5].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              semantic_bag.occurrences[6].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
          "semantic waist retains each authority outcome as a distinct constructor");
    CHECK(cetta_nik_outcome_v1_is_valid(
              CETTA_NIK_OUTCOME_ESTABLISHED) &&
              cetta_nik_outcome_v1_is_valid(
                  CETTA_NIK_OUTCOME_REFUTED) &&
              cetta_nik_outcome_v1_is_valid(
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT) &&
              cetta_nik_outcome_v1_is_valid(
                  CETTA_NIK_OUTCOME_INCOMPLETE) &&
              !cetta_nik_outcome_v1_is_valid((CettaNikOutcomeV1)99),
          "semantic outcomes form a closed sum without illegal pair states");
    CettaPrimeTypingCheckingObservationV1 fault_observation = {
        .authority = {
            .result = {
                .kind = CETTA_NIK_RESULT_ENGINE_FAULT,
                .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
            },
        },
    };
    CettaNikStatusV1 fault_status;
    CHECK(!cetta_prime_typing_authority_observation_v1_status(
              &fault_observation.authority, &fault_status) &&
              cetta_nik_result_v1_is_valid(
                  fault_observation.authority.result),
          "engine failure remains outside the semantic status readout");
    CettaPrimeTypingCheckingCandidateV1 unsupported_candidate = {
        semantic_legacy_term, semantic_outside_type, false, 0u,
    };
    CettaPrimeTypingCheckingObservationV1 unsupported_observation;
    CHECK(cetta_prime_typing_observe_checking_v1(
              &arena, &space, &unsupported_candidate,
              &unsupported_observation) &&
              unsupported_observation.authority.result.kind ==
                  CETTA_NIK_RESULT_OUTCOME &&
              unsupported_observation.authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
          "unsupported authored-pattern constructors abstain rather than becoming refutations or engine faults");
    bool semantic_routes_match =
        checking_route_is_regular(semantic_bag.occurrences[0].authority.route) &&
        checking_route_is_regular(semantic_bag.occurrences[1].authority.route) &&
        checking_route_is_regular(semantic_bag.occurrences[2].authority.route) &&
        semantic_bag.occurrences[3].authority.route ==
            CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION &&
        checking_route_is_regular(semantic_bag.occurrences[4].authority.route) &&
        semantic_bag.occurrences[5].authority.route ==
            CETTA_PRIME_TYPING_ROUTE_LEGACY_HE &&
        semantic_bag.occurrences[6].authority.route ==
            CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION;
    CHECK(semantic_routes_match,
          "each checking observation reports the authority route that owned it");
    if (!semantic_routes_match) {
        fprintf(stderr, "checking routes:");
        for (size_t index = 0u;
             index < sizeof semantic_candidates / sizeof semantic_candidates[0];
            index++) {
            fprintf(stderr, " %s",
                    checking_route_name(
                        semantic_bag.occurrences[index].authority.route));
        }
        fputc('\n', stderr);
    }
    CHECK(semantic_bag.occurrences[0].authority.payload &&
              semantic_bag.occurrences[2].authority.payload &&
              semantic_bag.occurrences[3].authority.payload &&
              semantic_bag.occurrences[4].authority.payload &&
              semantic_bag.occurrences[5].authority.payload &&
              semantic_bag.occurrences[6].authority.payload,
          "every checking status retains its evidence or obstruction detail");
    CHECK(semantic_bag.occurrences[0].authority.canonical_term &&
              semantic_bag.occurrences[1].authority.canonical_term &&
              !semantic_bag.occurrences[2].authority.canonical_term &&
              !semantic_bag.occurrences[3].authority.canonical_term &&
              !semantic_bag.occurrences[4].authority.canonical_term &&
              !semantic_bag.occurrences[5].authority.canonical_term &&
              !semantic_bag.occurrences[6].authority.canonical_term,
          "only established native checking retains the exact intrinsic term consumed by its authority");
    CHECK(semantic_bag.occurrences[4].authority.resources.limited &&
              semantic_bag.occurrences[4].authority.resources.initial == 1u &&
              semantic_bag.occurrences[4].authority.resources.remaining == 0u &&
              semantic_bag.occurrences[4].authority.resources.checking > 0u &&
              !semantic_bag.occurrences[0].authority.resources.limited,
          "per-occurrence resource receipts distinguish explicit limits from unbounded checking");
    CHECK(!cetta_prime_typing_checking_bag_v1_is_decision_complete(
              &semantic_bag),
          "undetermined and incomplete occurrences prevent a false completeness claim");

    CettaPrimeTypingFormationCandidateV1 formation_candidates[] = {
        {closed_identity_type, false, 0u},
        {semantic_legacy_term, false, 0u},
        {semantic_outside_type, false, 0u},
        {checking_candidate_large_type, true, 1u},
    };
    CettaPrimeTypingFormationObservationV1 formation_observations[
        sizeof formation_candidates / sizeof formation_candidates[0]];
    bool formation_observed = true;
    for (size_t index = 0u;
         index < sizeof formation_candidates / sizeof formation_candidates[0];
         index++) {
        formation_observed = formation_observed &&
            cetta_prime_typing_observe_formation_v1(
                &arena, &space, &formation_candidates[index],
                &formation_observations[index]);
    }
    CHECK(formation_observed,
          "formation uses the shared authority receipt without a second demand");
    bool formation_outcomes_match =
        formation_observations[0].authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_ESTABLISHED &&
        formation_observations[1].authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_REFUTED &&
        formation_observations[2].authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
        formation_observations[3].authority.result.value.outcome ==
            CETTA_NIK_OUTCOME_INCOMPLETE;
    CHECK(formation_outcomes_match,
          "formation distinguishes derivation, obstruction, boundary, and budget");
    if (!formation_outcomes_match) {
        fprintf(stderr, "formation outcomes: %d %d %d %d\n",
                formation_observations[0].authority.result.value.outcome,
                formation_observations[1].authority.result.value.outcome,
                formation_observations[2].authority.result.value.outcome,
                formation_observations[3].authority.result.value.outcome);
    }
    CettaPrimeTypingFormationCandidateV1 scoped_formation_candidates[] = {
        {scoped_formed_type_judgment->expr.elems[1], false, 0u},
        {scoped_small_term_judgment->expr.elems[1], false, 0u},
    };
    CettaPrimeTypingFormationObservationV1 scoped_formation_observations[2];
    CHECK(cetta_prime_typing_observe_formation_v1(
              &arena, &space, &scoped_formation_candidates[0],
              &scoped_formation_observations[0]) &&
              cetta_prime_typing_observe_formation_v1(
              &arena, &space, &scoped_formation_candidates[1],
              &scoped_formation_observations[1]),
          "contextual formation flows through the shared authority receipt");
    CHECK(scoped_formation_observations[0].authority.route ==
                  CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR &&
              scoped_formation_observations[0].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              scoped_formation_observations[1].authority.route ==
                  CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR &&
              scoped_formation_observations[1].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
          "contextual formation distinguishes derivation from universe boundary without HE");

    Atom *nonfunction_application = parse_one(&arena, "(App U0 U0)");
    CettaPrimeTypingSynthesisCandidateV1 synthesis_candidates[] = {
        {closed_beta_right, false, 0u},
        {nonfunction_application, false, 0u},
        {semantic_outside_type, false, 0u},
        {checking_candidate_large, true, 1u},
    };
    CettaPrimeTypingSynthesisObservationV1 synthesis_observations[
        sizeof synthesis_candidates / sizeof synthesis_candidates[0]];
    bool synthesis_observed = true;
    for (size_t index = 0u;
         index < sizeof synthesis_candidates / sizeof synthesis_candidates[0];
         index++) {
        synthesis_observed = synthesis_observed &&
            cetta_prime_typing_observe_synthesis_v1(
                &arena, &space, &synthesis_candidates[index],
                &synthesis_observations[index]);
    }
    CHECK(synthesis_observed,
          "synthesis uses the shared authority receipt without a second demand");
    CHECK(synthesis_observations[0].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              synthesis_observations[1].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_REFUTED &&
              synthesis_observations[2].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
              synthesis_observations[3].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_INCOMPLETE,
          "synthesis distinguishes derivation, obstruction, boundary, and budget");
    CHECK(formation_observations[0].authority.payload &&
              formation_observations[1].authority.payload &&
              formation_observations[2].authority.payload &&
              formation_observations[3].authority.payload &&
              synthesis_observations[0].authority.payload &&
              synthesis_observations[1].authority.payload &&
              synthesis_observations[2].authority.payload &&
              synthesis_observations[3].authority.payload,
          "formation and synthesis retain constructor-specific evidence payloads");
    CHECK(synthesis_observations[0].authority.canonical_term &&
              !synthesis_observations[1].authority.canonical_term &&
              !synthesis_observations[2].authority.canonical_term &&
              !synthesis_observations[3].authority.canonical_term,
          "only established native synthesis exposes a reusable intrinsic term");

    CettaPrimeTypingCheckingCandidateV1 semantic_reordered[] = {
        semantic_candidates[6], semantic_candidates[0],
        semantic_candidates[4], semantic_candidates[3],
        semantic_candidates[2], semantic_candidates[1],
        semantic_candidates[5],
    };
    semantic_reordered[1].steps_limited = true;
    semantic_reordered[1].steps = 100u;
    CHECK(cetta_prime_typing_checking_candidate_bag_equal_v1(
              &arena, semantic_candidates,
              sizeof semantic_candidates / sizeof semantic_candidates[0],
              semantic_reordered,
              sizeof semantic_reordered / sizeof semantic_reordered[0]),
          "candidate-bag equality ignores order and measurement budget while preserving terms and types");
    CHECK(!cetta_prime_typing_checking_candidate_bag_equal_v1(
              &arena, semantic_candidates,
              sizeof semantic_candidates / sizeof semantic_candidates[0],
              semantic_reordered,
              sizeof semantic_reordered / sizeof semantic_reordered[0] - 1u),
          "candidate-bag equality detects a missing occurrence");
    CettaPrimeTypingCheckingObservationV1 invalid_observation;
    CettaPrimeTypingCheckingCandidateV1 invalid_zero_budget = {
        closed_beta_right, closed_identity_type, true, 0u};
    CHECK(!cetta_prime_typing_observe_checking_v1(
              &arena, &space, &invalid_zero_budget, &invalid_observation),
          "a zero explicit producer budget is invalid rather than silently unbounded");

#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats semantic_stats;
    cetta_runtime_stats_snapshot(&semantic_stats);
    cetta_runtime_stats_disable();
    uint64_t semantic_route_count =
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR] +
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AUTHORED_REGULAR] +
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR] +
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR] +
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AMBIENT_FORMATION] +
        semantic_stats.counters[
            CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING];
    size_t semantic_observation_count =
        sizeof semantic_candidates / sizeof semantic_candidates[0] + 1u;
    if (semantic_route_count != semantic_observation_count) {
        fprintf(
            stderr,
            "semantic routes: scoped=%llu authored=%llu declared=%llu "
            "closed=%llu formation=%llu legacy=%llu total=%llu\n",
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_SCOPED_REGULAR],
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AUTHORED_REGULAR],
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_DECLARED_REGULAR],
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_CLOSED_REGULAR],
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_CHECKING_ROUTE_AMBIENT_FORMATION],
            (unsigned long long)semantic_stats.counters[
                CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CHECKING],
            (unsigned long long)semantic_route_count);
    }
    CHECK(semantic_route_count == semantic_observation_count,
          "each staged observation selects exactly one final route");
#endif

    if (program) {
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons U0 PrimeCtxNil) (idx PrimeZero)) $type) "
            "  PEstablished)",
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons U0 PrimeCtxNil) (idx PrimeZero)) U0) "
            "  PEstablished)",
            "authored open-variable synthesis agrees with the direct judge");
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil "
            "    (Lam U0 (idx PrimeZero))) $type) PEstablished)",
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil "
            "    (Lam U0 (idx PrimeZero))) (Pi U0 U0)) PEstablished)",
            "authored lambda synthesis agrees with the direct judge");
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (App (Lam U0 (idx PrimeZero)) (idx PrimeZero))) $type) "
            "  PEstablished)",
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (App (Lam U0 (idx PrimeZero)) (idx PrimeZero))) U0) "
            "  PEstablished)",
            "authored dependent application agrees with the direct judge");
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil "
            "    (Lam U0 (idx PrimeZero))) (Pi U0 U0)) PEstablished)",
            "(PrimeLpJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil "
            "    (Lam U0 (idx PrimeZero))) (Pi U0 U0)) PEstablished)",
            "authored bidirectional checking agrees with the direct judge");
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PConvert "
            "    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "      (App (Lam U0 (idx PrimeZero)) (idx PrimeZero))) "
            "    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "      (idx PrimeZero))) PEstablished)",
            "(PrimeLpJudges "
            "  (PConvert "
            "    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "      (App (Lam U0 (idx PrimeZero)) (idx PrimeZero))) "
            "    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "      (idx PrimeZero))) PEstablished)",
            "authored beta conversion agrees with the direct judge");
        check_horn_positive(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PConvert "
            "    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
            "      (Lam U0 "
            "        (App (idx (PrimeSucc PrimeZero)) (idx PrimeZero)))) "
            "    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
            "      (idx PrimeZero))) PEstablished)",
            "(PrimeLpJudges "
            "  (PConvert "
            "    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
            "      (Lam U0 "
            "        (App (idx (PrimeSucc PrimeZero)) (idx PrimeZero)))) "
            "    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
            "      (idx PrimeZero))) PEstablished)",
            "authored eta conversion agrees with the direct judge");

        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (idx PrimeZero)) $type) "
            "  PEstablished)",
            "authored synthesis rejects a loose index");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons U1 PrimeCtxNil) (idx PrimeZero)) $type) "
            "  PEstablished)",
            "authored synthesis rejects an invalid context");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil U1) $type) PEstablished)",
            "authored synthesis excludes a larger universe");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (App U0 U0)) $type) "
            "  PEstablished)",
            "authored synthesis rejects application of a sort");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil "
            "    (Lam (Pi U0 U0) (idx PrimeZero))) (Pi U0 U0)) "
            "  PEstablished)",
            "authored checking rejects a mismatched lambda domain");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil "
            "    (Lam (App (Lam U0 U0) U1) (idx PrimeZero))) "
            "    (Pi U0 U0)) PEstablished)",
            "authored checking rejects an unformed lambda annotation");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PConvert (PrimeScoped PrimeCtxNil U0) "
            "    (PrimeScoped PrimeCtxNil (Pi U0 U0))) PEstablished)",
            "authored conversion rejects distinct normal forms");
        check_horn_negative(
            program, &queries, &answers,
            "(PrimeLpJudges "
            "  (PConvert (PrimeScoped PrimeCtxNil U0) "
            "    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) U0)) "
            "  PEstablished)",
            "authored conversion rejects different contexts");
    }

    if (regular_program) {
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularLevelNormalizes "
            "  (LevelMax (LevelSucc LevelZero) LevelZero) "
            "  (LevelSucc LevelZero))",
            "(PrimeRegularLevelNormalizes "
            "  (LevelMax (LevelSucc LevelZero) LevelZero) "
            "  (LevelSucc LevelZero))",
            "authored closed-level maximum normalizes structurally");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil U1) "
            "    (Sort (LevelSucc LevelZero))) PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil U1) "
            "    (Sort (LevelSucc LevelZero))) PEstablished)",
            "authored tower synthesizes the successor universe of U1");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U1 PrimeCtxNil) "
            "    (idx PrimeZero)) U1) PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U1 PrimeCtxNil) "
            "    (idx PrimeZero)) U1) PEstablished)",
            "authored tower admits universe-valued context entries");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil U0) "
            "    (Sort (LevelSucc LevelZero))) PEstablished)",
            "(PrimeRegularJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil U0) "
            "    (Sort (LevelSucc LevelZero))) PEstablished)",
            "authored tower permits explicit cumulative promotion");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (Pi U1 U0)) "
            "    (Sort (LevelMax (LevelSucc LevelZero) LevelZero))) "
            "  PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (Pi U1 U0)) "
            "    (Sort (LevelMax (LevelSucc LevelZero) LevelZero))) "
            "  PEstablished)",
            "authored Pi formation retains its closed universe join");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PConvert (PrimeScoped PrimeCtxNil U1) "
            "    (PrimeScoped PrimeCtxNil (Sort LevelZero))) "
            "  PEstablished)",
            "(PrimeRegularJudges "
            "  (PConvert (PrimeScoped PrimeCtxNil U1) "
            "    (PrimeScoped PrimeCtxNil (Sort LevelZero))) "
            "  PEstablished)",
            "authored conversion identifies the embedded zero sort");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (Sigma U0 U0)) U1) "
            "  PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (Sigma U0 U0)) U1) "
            "  PEstablished)",
            "authored Sigma formation agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Id U0 (idx PrimeZero) (idx PrimeZero))) U1) "
            "  PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Id U0 (idx PrimeZero) (idx PrimeZero))) U1) "
            "  PEstablished)",
            "authored identity formation agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PCheck (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Pair (idx PrimeZero) (idx PrimeZero))) "
            "    (Sigma U0 U0)) PEstablished)",
            "(PrimeRegularJudges "
            "  (PCheck (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Pair (idx PrimeZero) (idx PrimeZero))) "
            "    (Sigma U0 U0)) PEstablished)",
            "authored pair checking agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons (Sigma U0 U0) PrimeCtxNil) "
            "    (Fst (idx PrimeZero))) U0) PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons (Sigma U0 U0) PrimeCtxNil) "
            "    (Fst (idx PrimeZero))) U0) PEstablished)",
            "authored first projection agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons "
            "      (Sigma U0 (Id U0 (idx PrimeZero) (idx PrimeZero))) "
            "      PrimeCtxNil) "
            "    (Snd (idx PrimeZero))) "
            "    (Id U0 (Fst (idx PrimeZero)) (Fst (idx PrimeZero)))) "
            "  PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped "
            "    (PrimeCtxCons "
            "      (Sigma U0 (Id U0 (idx PrimeZero) (idx PrimeZero))) "
            "      PrimeCtxNil) "
            "    (Snd (idx PrimeZero))) "
            "    (Id U0 (Fst (idx PrimeZero)) (Fst (idx PrimeZero)))) "
            "  PEstablished)",
            "authored dependent second projection agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Refl (idx PrimeZero))) "
            "    (Id U0 (idx PrimeZero) (idx PrimeZero))) PEstablished)",
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Refl (idx PrimeZero))) "
            "    (Id U0 (idx PrimeZero) (idx PrimeZero))) PEstablished)",
            "authored reflexivity agrees with the direct judge");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularNormalizes (Fst (Pair U0 U1)) U0)",
            "(PrimeRegularNormalizes (Fst (Pair U0 U1)) U0)",
            "authored first-projection beta rule computes");
        check_horn_positive(
            regular_program, &queries, &answers,
            "(PrimeRegularNormalizes (Snd (Pair U0 U1)) U1)",
            "(PrimeRegularNormalizes (Snd (Pair U0 U1)) U1)",
            "authored second-projection beta rule computes");
        check_horn_negative(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PCheck (PrimeScoped PrimeCtxNil U1) U1) PEstablished)",
            "authored tower forbids universe lowering");
        check_horn_negative(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped PrimeCtxNil (Sort LevelBogus)) $type) "
            "  PEstablished)",
            "authored tower excludes malformed level syntax");
        check_horn_negative(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Pair (idx PrimeZero) (idx PrimeZero))) $type) "
            "  PEstablished)",
            "authored pair introduction remains a checking form");
        check_horn_negative(
            regular_program, &queries, &answers,
            "(PrimeRegularJudges "
            "  (PSynth (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
            "    (Fst (idx PrimeZero))) $type) PEstablished)",
            "authored first projection rejects a non-Sigma operand");
    }

    cetta_gslt_horn_program_free(regular_program);
    cetta_gslt_horn_program_free(program);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&queries);
    arena_free(&arena);

    if (failures != 0u) {
        fprintf(stderr,
                "PrimeRegularKernelSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PrimeRegularKernelSummary checks=%u failures=0)\n", checks);
    return 0;
}
