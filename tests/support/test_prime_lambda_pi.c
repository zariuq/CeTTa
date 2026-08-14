#include "parser.h"
#include "gslt_horn_runtime.h"
#include "generated/prime_typing_open_lambda_pi_core_source_binding_v1.generated.h"
#include "prime_lambda_pi.h"
#include "prime_semantics.h"
#include "space.h"
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

static CettaPrimeLambdaPiResult synth(
    Arena *arena, const char *scoped_text, bool limited, uint64_t steps) {
    CettaPrimeLambdaPiBudget budget;
    cetta_prime_lambda_pi_budget_init(&budget, limited, steps);
    Atom *scoped = parse_one(arena, scoped_text);
    CHECK(scoped != NULL, "synthesis fixture parses");
    return cetta_prime_lambda_pi_synth(arena, scoped, &budget);
}

static CettaPrimeLambdaPiResult check_term(
    Arena *arena, const char *scoped_text, const char *expected_text) {
    CettaPrimeLambdaPiBudget budget;
    cetta_prime_lambda_pi_budget_init(&budget, false, 0u);
    Atom *scoped = parse_one(arena, scoped_text);
    Atom *expected = parse_one(arena, expected_text);
    CHECK(scoped && expected, "checking fixture parses");
    return cetta_prime_lambda_pi_check(arena, scoped, expected, &budget);
}

static CettaPrimeLambdaPiResult convert(
    Arena *arena, const char *left_text, const char *right_text) {
    CettaPrimeLambdaPiBudget budget;
    cetta_prime_lambda_pi_budget_init(&budget, false, 0u);
    Atom *left = parse_one(arena, left_text);
    Atom *right = parse_one(arena, right_text);
    CHECK(left && right, "conversion fixture parses");
    return cetta_prime_lambda_pi_convert(arena, left, right, &budget);
}

static bool result_type_is(
    Arena *arena, const CettaPrimeLambdaPiResult *result,
    const char *expected_text) {
    Atom *expected = parse_one(arena, expected_text);
    return result && result->status == CETTA_PRIME_LP_ESTABLISHED &&
           result->type && expected && atom_eq(result->type, expected);
}

static bool verdict_status(Atom *verdict, const char *status) {
    return verdict && verdict->kind == ATOM_EXPR &&
           verdict->expr.len == 4u &&
           atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
           atom_is_symbol(verdict->expr.elems[1], status);
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
    if (argc != 2) {
        fprintf(stderr, "usage: %s OPEN_LAMBDA_PI_LANGDEF\n", argv[0]);
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
    const CettaNikDirectSourceBindingV1 *binding =
        &prime_typing_open_lambda_pi_core_source_binding_v1;
    CHECK(cetta_nik_direct_source_binding_v1_is_valid(binding) &&
              binding->authority == &cetta_prime_typing_direct_authority_v1 &&
              strcmp(binding->presentation_id,
                     "prime-open-lambda-pi-core-v1") == 0 &&
              strcmp(binding->semantic_scope,
                     "prime.typing.open-lambda-pi-core") == 0 &&
              strcmp(binding->certificate_policy, "none") == 0 &&
              binding->coverage ==
                  CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
          "open lambda-pi source is bound to the direct Prime authority");

    CettaPrimeLambdaPiResult open_variable = synth(
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
    CettaPrimeLambdaPiBudget deep_budget;
    cetta_prime_lambda_pi_budget_init(&deep_budget, false, 0u);
    CettaPrimeLambdaPiResult deep_variable = cetta_prime_lambda_pi_synth(
        &arena, deep_scoped, &deep_budget);
    CHECK(result_type_is(&arena, &deep_variable, "U0"),
          "large open telescope is validated and searched linearly");

    CettaPrimeLambdaPiResult identity = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &identity, "(Pi U0 U0)"),
          "annotated identity synthesizes a dependent function type");

    Atom *identity_scoped = parse_one(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))");
    CettaPrimeLambdaPiBudget measured_budget;
    cetta_prime_lambda_pi_budget_init(&measured_budget, true, 1000000u);
    CettaPrimeLambdaPiResult measured_identity =
        cetta_prime_lambda_pi_synth(
            &arena, identity_scoped, &measured_budget);
    uint64_t identity_steps = measured_budget.spent;
    CHECK(measured_identity.status == CETTA_PRIME_LP_ESTABLISHED &&
              identity_steps > 0u,
          "bounded synthesis reports its exact structural work");
    CettaPrimeLambdaPiBudget exact_budget;
    cetta_prime_lambda_pi_budget_init(
        &exact_budget, true, identity_steps);
    CettaPrimeLambdaPiResult exact_identity = cetta_prime_lambda_pi_synth(
        &arena, identity_scoped, &exact_budget);
    CHECK(result_type_is(&arena, &exact_identity, "(Pi U0 U0)"),
          "the exact measured budget preserves an established verdict");
    CettaPrimeLambdaPiBudget short_budget;
    cetta_prime_lambda_pi_budget_init(
        &short_budget, true, identity_steps - 1u);
    CettaPrimeLambdaPiResult short_identity = cetta_prime_lambda_pi_synth(
        &arena, identity_scoped, &short_budget);
    CHECK(short_identity.status == CETTA_PRIME_LP_INCOMPLETE,
          "one fewer structural step yields incomplete, never refuted");

    CettaPrimeLambdaPiResult application = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (App (Lam U0 (idx 0)) (idx 0)))",
        false, 0u);
    CHECK(result_type_is(&arena, &application, "U0"),
          "open application substitutes its result type");

    CettaPrimeLambdaPiResult pi_formation = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Pi U0 U0))", false, 0u);
    CHECK(result_type_is(&arena, &pi_formation, "U1"),
          "dependent function formation synthesizes the upper sort");

    CettaPrimeLambdaPiResult identity_check = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))",
        "(Pi U0 U0)");
    CHECK(identity_check.status == CETTA_PRIME_LP_ESTABLISHED,
          "annotated identity checks against its function type");

    CettaPrimeLambdaPiResult synthesized_type_formed = synth(
        &arena, "(PrimeScoped PrimeCtxNil (Pi U0 U0))", false, 0u);
    CHECK(result_type_is(&arena, &synthesized_type_formed, "U1"),
          "non-top synthesized type is formed by construction");

    CettaPrimeLambdaPiResult beta = convert(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (App (Lam U0 (idx 0)) (idx 0)))",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0))");
    CHECK(beta.status == CETTA_PRIME_LP_ESTABLISHED,
          "beta conversion is decided under an open context");

    CettaPrimeLambdaPiResult eta = convert(
        &arena,
        "(PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) "
        "  (Lam U0 (App (idx 1) (idx 0))))",
        "(PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) (idx 0))");
    CHECK(eta.status == CETTA_PRIME_LP_ESTABLISHED,
          "function eta conversion is decided under an open context");

    CettaPrimeLambdaPiResult loose = synth(
        &arena, "(PrimeScoped PrimeCtxNil (idx 0))", false, 0u);
    CHECK(loose.status == CETTA_PRIME_LP_REFUTED &&
              loose.reason && strcmp(loose.reason, "term-out-of-scope") == 0,
          "loose index is a decisive scope rejection");

    CettaPrimeLambdaPiResult last_step_loose = synth(
        &arena, "(PrimeScoped PrimeCtxNil (idx 0))", true, 2u);
    CHECK(last_step_loose.status == CETTA_PRIME_LP_REFUTED,
          "a rejection on the final budget step is not called exhaustion");

    CettaPrimeLambdaPiResult exhausted = synth(
        &arena, "(PrimeScoped PrimeCtxNil U0)", true, 0u);
    CHECK(exhausted.status == CETTA_PRIME_LP_INCOMPLETE,
          "budget exhaustion is never a typing verdict");

    CettaPrimeLambdaPiResult invalid_context = synth(
        &arena,
        "(PrimeScoped (PrimeCtxCons U1 PrimeCtxNil) (idx 0))",
        false, 0u);
    CHECK(invalid_context.status == CETTA_PRIME_LP_REFUTED,
          "an unformed context domain is rejected");

    CettaPrimeLambdaPiResult upper_sort = synth(
        &arena, "(PrimeScoped PrimeCtxNil U1)", false, 0u);
    CHECK(upper_sort.status == CETTA_PRIME_LP_REFUTED,
          "the upper sort does not synthesize a larger excluded universe");

    CettaPrimeLambdaPiResult bad_application = synth(
        &arena, "(PrimeScoped PrimeCtxNil (App U0 U0))", false, 0u);
    CHECK(bad_application.status == CETTA_PRIME_LP_REFUTED,
          "application requires a dependent function type");

    CettaPrimeLambdaPiResult bad_lambda = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil (Lam (Pi U0 U0) (idx 0)))",
        "(Pi U0 U0)");
    CHECK(bad_lambda.status == CETTA_PRIME_LP_REFUTED,
          "lambda annotation must agree with the expected domain");

    CettaPrimeLambdaPiResult unformed_annotation = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Lam (App (Lam U0 U0) U1) (idx 0)))",
        "(Pi U0 U0)");
    CHECK(unformed_annotation.status == CETTA_PRIME_LP_REFUTED,
          "lambda annotation must be a formed type before conversion");

    CettaPrimeLambdaPiResult distinct = convert(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(PrimeScoped PrimeCtxNil (Pi U0 U0))");
    CHECK(distinct.status == CETTA_PRIME_LP_REFUTED && distinct.reason &&
              strcmp(distinct.reason, "distinct-normal-forms") == 0,
          "distinct same-typed normal forms are rejected");

    CettaPrimeLambdaPiResult context_mismatch = convert(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) U0)");
    CHECK(context_mismatch.status == CETTA_PRIME_LP_REFUTED &&
              context_mismatch.reason &&
              strcmp(context_mismatch.reason,
                     "conversion-context-mismatch") == 0,
          "conversion never compares terms from different contexts");

    Atom *judgment = parse_one(
        &arena, "(Synth (PrimeScoped PrimeCtxNil (Lam U0 (idx 0))))");
    Atom *verdict = judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, judgment, false, 0u)
        : NULL;
    char *verdict_text = verdict ? atom_to_string(&arena, verdict) : NULL;
    CHECK(verdict_status(verdict, "Established"),
          "Prime direct authority routes scoped lambda-pi synthesis");
    CHECK(verdict_text && strstr(verdict_text, "PrimeLambdaPiSynthesis") &&
              strstr(verdict_text, "Certificate") == NULL,
          "ordinary lambda-pi synthesis allocates no proof certificate");

    Atom *ordinary_judgment = parse_one(&arena, "(Synth 7)");
    Atom *ordinary_verdict = ordinary_judgment
        ? prime_semantics_judge_typing_direct(
              &arena, &space, ordinary_judgment, false, 0u)
        : NULL;
    CHECK(ordinary_verdict &&
              !cetta_prime_lambda_pi_unwrap_scoped(
                  ordinary_judgment->expr.elems[1], NULL, NULL),
          "ordinary non-scoped synthesis retains its established route");

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
                "PrimeLambdaPiSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PrimeLambdaPiSummary checks=%u failures=0)\n", checks);
    return 0;
}
