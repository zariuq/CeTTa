#include "parser.h"
#include "gslt_horn_runtime.h"
#include "generated/prime_typing_open_regular_kernel_source_binding_v1.generated.h"
#include "prime_level.h"
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

static void check_computed_intrinsic_forms(Arena *arena) {
    Atom *context = parse_one(arena, "(PrimeCtxCons U0 PrimeCtxNil)");
    const char *sources[] = {
        "(App (Lam U0 (Refl (idx 0))) (idx 0))",
        "(App (Lam (Sigma U0 (Id U0 (idx 0) (idx 0))) (Snd (idx 0))) "
          "(Pair (idx 0) (Refl (idx 0))))",
        "(App (Lam U0 (Lam U0 (idx 1))) (idx 0))"
    };
    const char *expected[] = {
        "(Refl (idx 0))", "(Refl (idx 0))", "(Lam U0 (idx 1))"
    };
    for (size_t i = 0u; i < sizeof sources / sizeof sources[0]; i++) {
        Atom *term = parse_one(arena, sources[i]);
        CettaPrimeRegularKernelBudget budget;
        cetta_prime_regular_kernel_budget_init(&budget, true, UINT64_MAX);
        CettaPrimeRegularKernelNormalFormV1 result =
            cetta_prime_regular_kernel_normalize_intrinsic_v1(
                arena, context, term, &budget);
        if (result.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            fprintf(stderr, "normalization case %zu: status=%d reason=%s\n",
                    i, result.status, result.reason ? result.reason : "none");
        CHECK(result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                  result.term && atom_eq(result.term, parse_one(arena, expected[i])),
              "normalization computes dependent beta/projection without capture");
        uint64_t work = budget.spent;
        CHECK(work > 0u && result.type && result.source_type,
              "normalization retains its source type and accounts for work");
        if (i == 1u) {
            CHECK(result.source_type && result.type &&
                      atom_eq(result.source_type, parse_one(arena,
                      "(Id U0 (Fst (Pair (idx 0) (Refl (idx 0)))) "
                        "(Fst (Pair (idx 0) (Refl (idx 0)))))")) &&
                      atom_eq(result.type, parse_one(arena,
                        "(Id U0 (idx 0) (idx 0))")),
                  "dependent projection retains the displayed index and computes its type");
        }
        cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
        CettaPrimeRegularKernelResult independently_checked =
            cetta_prime_regular_kernel_check_intrinsic(
                arena, context, result.term, result.type, &budget);
        CHECK(independently_checked.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
              "computed dependent result independently checks at the computed type");
        for (uint64_t limit = 0u; limit <= work; limit++) {
            cetta_prime_regular_kernel_budget_init(&budget, true, limit);
            CettaPrimeRegularKernelNormalFormV1 bounded =
                cetta_prime_regular_kernel_normalize_intrinsic_v1(
                    arena, context, term, &budget);
            if (limit < work) {
                CHECK(bounded.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED &&
                          !bounded.term && !bounded.type && !bounded.source_type,
                      "every short normalization budget returns no certified partial value");
            } else {
                CHECK(bounded.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                          bounded.term && bounded.type && bounded.source_type &&
                          result.term && result.type && result.source_type &&
                          atom_eq(bounded.term, result.term) && atom_eq(bounded.type, result.type) &&
                          atom_eq(bounded.source_type, result.source_type),
                      "the exact measured budget reproduces the computed result");
            }
        }
    }
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    CettaPrimeRegularKernelNormalFormV1 bad =
        cetta_prime_regular_kernel_normalize_intrinsic_v1(arena, context,
            parse_one(arena, "(App (idx 0) (idx 0))"), &budget);
    CHECK(bad.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED && !bad.term && !bad.type,
          "normalization does not execute an ill-typed source application");
}

/* Computation rules supplied as data.  A small theory: numbers N with z and
 * s, streams St of numbers with cons, hd and tl, the iteration it whose rule
 * waits for an observation, fr n = it s n, a route-2 constant g whose rule is
 * guarded at a closed argument, dbl by structural recursion, and p2 with a
 * nested pattern.  The context declares them innermost first. */
static const char RULE_CONTEXT[] =
    "(PrimeCtxDecl (DeclConst fr) (Pi (DeclConst N) (DeclConst St)) "
    "(PrimeCtxDecl (DeclConst g) (Pi (DeclConst N) (DeclConst N)) "
    "(PrimeCtxDecl (DeclConst p2) (Pi (DeclConst N) (DeclConst N)) "
    "(PrimeCtxDecl (DeclConst dbl) (Pi (DeclConst N) (DeclConst N)) "
    "(PrimeCtxDecl (DeclConst it) "
    "  (Pi (Pi (DeclConst N) (DeclConst N)) (Pi (DeclConst N) (DeclConst St))) "
    "(PrimeCtxDecl (DeclConst tl) (Pi (DeclConst St) (DeclConst St)) "
    "(PrimeCtxDecl (DeclConst hd) (Pi (DeclConst St) (DeclConst N)) "
    "(PrimeCtxDecl (DeclConst cons) (Pi (DeclConst N) (Pi (DeclConst St) (DeclConst St))) "
    "(PrimeCtxDecl (DeclConst St) (Sort (LevelConst 0)) "
    "(PrimeCtxDecl (DeclConst s) (Pi (DeclConst N) (DeclConst N)) "
    "(PrimeCtxDecl (DeclConst z) (DeclConst N) "
    "(PrimeCtxDecl (DeclConst N) (Sort (LevelConst 0)) "
    "PrimeCtxNil))))))))))))";

static const char RULE_TABLE[] =
    "(LCons (PrimeRule hd 1 ((App (App (DeclConst cons) (PVar 0)) (PVar 1))) (PVar 0)) "
    "(LCons (PrimeRule tl 1 ((App (App (DeclConst cons) (PVar 0)) (PVar 1))) (PVar 1)) "
    "(LCons (PrimeRule it (PObserved 2) ((PVar 0) (PVar 1)) "
    "  (App (App (DeclConst cons) (PVar 1)) "
    "       (App (App (DeclConst it) (PVar 0)) (App (PVar 0) (PVar 1))))) "
    "(LCons (PrimeRule fr 1 ((PVar 0)) (App (App (DeclConst it) (DeclConst s)) (PVar 0))) "
    "(LCons (PrimeRule g 1 ((PGuard (PVar 0))) (App (DeclConst s) (PVar 0))) "
    "(LCons (PrimeRule dbl 1 ((DeclConst z)) (DeclConst z)) "
    "(LCons (PrimeRule dbl 1 ((App (DeclConst s) (PVar 0))) "
    "  (App (DeclConst s) (App (DeclConst s) (App (DeclConst dbl) (PVar 0))))) "
    "(LCons (PrimeRule p2 1 ((App (DeclConst s) (App (DeclConst s) (PVar 0)))) (PVar 0)) "
    "LNil))))))))";

/* The normal form of `term_text` under the rule table, or NULL; every run
 * has a finite allowance, so a rule that never stops shows as no result. */
static Atom *rule_normal_form(Arena *arena, Atom *context, const char *term_text) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, 20000u);
    CettaPrimeRegularKernelNormalFormV1 result =
        cetta_prime_regular_kernel_normalize_intrinsic_v1(
            arena, context, parse_one(arena, term_text), &budget);
    return result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ? result.term : NULL;
}

static bool rule_normalizes_to(Arena *arena, Atom *context, const char *term_text,
                               const char *expected_text) {
    Atom *normal = rule_normal_form(arena, context, term_text);
    Atom *expected = parse_one(arena, expected_text);
    return normal && expected && atom_eq(normal, expected);
}

static CettaPrimeRegularKernelStatus rule_conversion(
    Arena *arena, Atom *context, const char *left_text, const char *right_text) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, 20000u);
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
            arena, context, parse_one(arena, left_text),
            parse_one(arena, right_text), &budget);
    return decision.status;
}

static bool rule_contractum_is(Arena *arena, const char *term_text,
                               const char *expected_text, uint64_t firings) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, 20000u);
    uint64_t counted = 99u;
    Atom *stepped = cetta_prime_regular_kernel_rule_contractum_counted_v1(
        arena, parse_one(arena, term_text), &budget, &counted);
    if (!expected_text) return !stepped && counted == firings;
    Atom *expected = parse_one(arena, expected_text);
    return stepped && expected && atom_eq(stepped, expected) && counted == firings;
}

static void check_rules_as_data(Arena *arena) {
    Atom *context = parse_one(arena, RULE_CONTEXT);
    char open_text[sizeof RULE_CONTEXT + 64];
    snprintf(open_text, sizeof open_text, "(PrimeCtxCons (DeclConst N) %s)", RULE_CONTEXT);
    Atom *open_context = parse_one(arena, open_text);
    Atom *rules = parse_one(arena, RULE_TABLE);
    CHECK(context && open_context && rules, "the rule fixtures parse");
    cetta_prime_regular_kernel_rules_set(rules);

    /* An observed rule: a stream is never unfolded by itself; under hd and
     * tl it unfolds once per observation, also below a binder. */
    CHECK(rule_normalizes_to(arena, context,
              "(App (App (DeclConst it) (DeclConst s)) (DeclConst z))",
              "(App (App (DeclConst it) (DeclConst s)) (DeclConst z))"),
          "a stream is not unfolded where nothing observes it");
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst hd) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(DeclConst z)"),
          "the head of an iteration unfolds it once");
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst tl) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(App (App (DeclConst it) (DeclConst s)) (App (DeclConst s) (DeclConst z)))"),
          "the tail of an iteration stops at the next iteration");
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst hd) (App (DeclConst tl) (App (DeclConst tl) "
              "  (App (DeclConst fr) (DeclConst z)))))",
              "(App (DeclConst s) (App (DeclConst s) (DeclConst z)))"),
          "an observation at position two of the numbers from zero is two");
    CHECK(rule_normalizes_to(arena, context,
              "(Lam (DeclConst N) (App (DeclConst hd) "
              "  (App (App (DeclConst it) (DeclConst s)) (idx 0))))",
              "(Lam (DeclConst N) (idx 0))"),
          "an observation below a binder unfolds at an open seed");
    CHECK(rule_normalizes_to(arena, context,
              "(Lam (DeclConst N) (App (App (DeclConst it) (DeclConst s)) (idx 0)))",
              "(Lam (DeclConst N) (App (App (DeclConst it) (DeclConst s)) (idx 0)))"),
          "a stream below a binder is not unfolded");
    CHECK(rule_conversion(arena, context,
              "(App (DeclConst tl) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(App (App (DeclConst it) (DeclConst s)) (App (DeclConst s) (DeclConst z)))") ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "an observed tail converts with the next iteration");
    CHECK(rule_conversion(arena, context,
              "(App (App (DeclConst it) (DeclConst s)) (DeclConst z))",
              "(App (App (DeclConst cons) (DeclConst z)) "
              "  (App (App (DeclConst it) (DeclConst s)) (App (DeclConst s) (DeclConst z))))") ==
              CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
          "a stream and its unfolding are not compared by conversion");

    /* The counted step: an observed argument unfolds within the step. */
    CHECK(rule_contractum_is(arena,
              "(App (DeclConst hd) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(DeclConst z)", 2u),
          "the head of an iteration is one step of two firings");
    CHECK(rule_contractum_is(arena,
              "(App (DeclConst tl) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(App (App (DeclConst it) (DeclConst s)) (App (DeclConst s) (DeclConst z)))", 2u),
          "the tail of an iteration is one step of two firings");
    CHECK(rule_contractum_is(arena,
              "(App (DeclConst hd) (App (App (DeclConst cons) (DeclConst z)) "
              "  (App (App (DeclConst it) (DeclConst s)) (DeclConst z))))",
              "(DeclConst z)", 1u),
          "the head of a cons is one firing");
    CHECK(rule_contractum_is(arena,
              "(App (App (DeclConst it) (DeclConst s)) (DeclConst z))", NULL, 0u),
          "an iteration alone takes no step");
    {
        CettaPrimeRegularKernelBudget budget;
        cetta_prime_regular_kernel_budget_init(&budget, true, 20000u);
        uint64_t firings = 0u;
        Atom *normal = cetta_prime_regular_kernel_rule_normal_form_v1(
            arena, parse_one(arena,
                "(App (DeclConst hd) (App (DeclConst tl) (App (DeclConst tl) "
                "  (App (DeclConst fr) (DeclConst z)))))"),
            &budget, &firings);
        CHECK(normal && atom_eq(normal, parse_one(arena,
                  "(App (DeclConst s) (App (DeclConst s) (DeclConst z)))")) &&
                  firings == 7u,
              "the observation at position two takes seven firings");
    }

    /* A guarded pattern: the rule fires at a closed argument only. */
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst g) (DeclConst z))", "(App (DeclConst s) (DeclConst z))"),
          "a guarded rule fires at a closed argument");
    CHECK(rule_normalizes_to(arena, open_context,
              "(App (DeclConst g) (idx 0))", "(App (DeclConst g) (idx 0))"),
          "a guarded rule waits at an open argument");
    CHECK(rule_conversion(arena, context,
              "(App (DeclConst g) (DeclConst z))", "(App (DeclConst s) (DeclConst z))") ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "a guarded rule converts at a closed argument");
    CHECK(rule_conversion(arena, open_context,
              "(App (DeclConst g) (idx 0))", "(App (DeclConst s) (idx 0))") ==
              CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
          "a guarded rule at an open argument leaves the comparison undecided");

    /* A nested pattern reads inside an argument after reducing it. */
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst p2) (App (DeclConst s) "
              "  (App (DeclConst dbl) (App (DeclConst s) (DeclConst z)))))",
              "(App (DeclConst s) (DeclConst z))"),
          "a nested pattern matches once the inspected part is reduced");
    CHECK(rule_conversion(arena, context,
              "(App (DeclConst p2) (App (DeclConst s) "
              "  (App (DeclConst dbl) (App (DeclConst s) (DeclConst z)))))",
              "(App (DeclConst s) (DeclConst z))") ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "conversion reduces the part a nested pattern inspects");
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst p2) (App (DeclConst s) (DeclConst z)))",
              "(App (DeclConst p2) (App (DeclConst s) (DeclConst z)))"),
          "a nested pattern that does not match leaves the call");

    /* Not convertible: refuted without a guarded constant, undecided with
     * one, also when it is reached through the rules of another. */
    CHECK(rule_conversion(arena, context,
              "(App (DeclConst dbl) (App (DeclConst s) (DeclConst z)))", "(App (DeclConst s) (DeclConst z))") ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "two different numbers without a guarded constant are refuted");
    CHECK(rule_conversion(arena, context,
              "(App (App (DeclConst it) (DeclConst s)) (DeclConst z))",
              "(App (App (DeclConst it) (DeclConst s)) (App (DeclConst s) (DeclConst z)))") ==
              CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
          "two different streams are undecided, never refuted");
    CHECK(rule_conversion(arena, context,
              "(App (DeclConst fr) (DeclConst z))",
              "(App (DeclConst fr) (App (DeclConst s) (DeclConst z)))") ==
              CETTA_PRIME_REGULAR_KERNEL_UNDECIDED,
          "a constant whose rule leads to a guarded one is undecided too");

    cetta_prime_regular_kernel_rules_set(NULL);
    CHECK(rule_normalizes_to(arena, context,
              "(App (DeclConst hd) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))",
              "(App (DeclConst hd) (App (App (DeclConst it) (DeclConst s)) (DeclConst z)))"),
          "without its rules nothing computes");
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

    check_computed_intrinsic_forms(&arena);
    check_rules_as_data(&arena);

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

    const char *raised_reflexivity_source =
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (Refl (idx 0)))";
    const char *raised_reflexivity_type =
        "(Id (Sort (LevelConst 1)) (idx 0) (idx 0))";
    CettaPrimeRegularKernelResult raised_reflexivity = check_term(
        &arena, raised_reflexivity_source, raised_reflexivity_type);
    CHECK(raised_reflexivity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "reflexivity checks at the expected cumulatively raised carrier");
    CettaPrimeRegularKernelResult raised_reflexive_pair = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (Pair (idx 0) (Refl (idx 0))))",
        "(Sigma (Sort (LevelConst 1)) "
        "  (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))");
    CHECK(raised_reflexive_pair.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "dependent pair checking supplies the raised reflexivity carrier");
    CettaPrimeRegularKernelResult raised_reflexive_application = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (App (Lam (Pair (idx 0) (Refl (idx 0)))) (idx 0)))",
        "(Sigma (Sort (LevelConst 1)) "
        "  (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))");
    CHECK(raised_reflexive_application.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "an applied lambda returns a checked pair at the raised universe");
    const char *annotated_pair_sources[] = {
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (App (Lam (Sort (LevelConst 0)) "
        "    (Pair (idx 0) (Refl (idx 0)))) (idx 0)))",
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (App (Lam (Sort (LevelConst 1)) "
        "    (Pair (idx 0) (Refl (idx 0)))) (idx 0)))",
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "  (App (Lam (App (Lam (Sort (LevelConst 2)) (idx 0)) "
        "                  (Sort (LevelConst 1))) "
        "    (Pair (idx 0) (Refl (idx 0)))) (idx 0)))"
    };
    for (size_t i = 0u; i < sizeof annotated_pair_sources /
                                sizeof annotated_pair_sources[0]; ++i) {
        CettaPrimeRegularKernelResult pair_application = check_term(
            &arena, annotated_pair_sources[i],
            "(Sigma (Sort (LevelConst 1)) "
            "  (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))");
        if (pair_application.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            fprintf(stderr, "annotated pair case %zu: %s\n", i,
                    pair_application.reason ? pair_application.reason : "no reason");
        CHECK(pair_application.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
              "direct pair-producing application retains its annotated domain");
    }
    CettaPrimeRegularKernelResult higher_order_pair_application = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (App (Lam (Pi U0 U0) (Pair (idx 0) (Refl (idx 0)))) "
        "       (Lam (idx 0))))",
        "(Sigma (Pi U0 U0) (Id (Pi U0 U0) (idx 0) (idx 0)))");
    CHECK(higher_order_pair_application.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "annotated application checks both its lambda argument and pair result");
    CettaPrimeRegularKernelResult lower_domain_pair_application = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (App (Lam (Sort (LevelConst 0)) (Pair (idx 0) (Refl (idx 0)))) "
        "       (Sort (LevelConst 0))))",
        "(Sigma (Sort (LevelConst 1)) "
        "  (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))");
    CHECK(lower_domain_pair_application.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "annotated application cannot raise its domain to admit a bad argument");
    CettaPrimeRegularKernelResult ignored_bad_pair_argument = check_term(
        &arena,
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (App (Lam U1 (Pair U0 (Refl U0))) (idx 0)))",
        "(Sigma U1 (Id U1 (idx 0) (idx 0)))");
    CHECK(ignored_bad_pair_argument.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "pair-producing application checks even an unused argument at its domain");
    CettaPrimeRegularKernelResult lambda_reflexivity = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Refl (Lam (idx 0))))",
        "(Id (Pi U0 U0) (Lam (idx 0)) (Lam (idx 0)))");
    CHECK(lambda_reflexivity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "expected identity carrier checks an unsynthesizable lambda subject");
    CettaPrimeRegularKernelResult converted_reflexivity = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Refl U0))",
        "(Id U1 (App (Lam U1 (idx 0)) U0) "
        "       (App (Lam U1 (idx 0)) U0))");
    CHECK(converted_reflexivity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "reflexivity compares its subject with both converted endpoints");
    CettaPrimeRegularKernelResult reduced_identity_reflexivity = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Refl U0))",
        "(App (Lam U1 (Id U1 (idx 0) (idx 0))) U0)");
    CHECK(reduced_identity_reflexivity.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "reflexivity exposes an identity type through expected-type conversion");

    const char *reflexivity_negative_sources[] = {
        "(PrimeScoped (PrimeCtxCons U1 (PrimeCtxCons U1 PrimeCtxNil)) "
        "  (Refl (idx 0)))",
        "(PrimeScoped (PrimeCtxCons U1 (PrimeCtxCons U1 PrimeCtxNil)) "
        "  (Refl (idx 0)))",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (Refl (idx 0)))",
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 1)) PrimeCtxNil) "
        "  (Refl (idx 0)))",
        "(PrimeScoped PrimeCtxNil (Refl U0))",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) "
        "  (Refl (App (Lam U1 U0) (idx 0))))"
    };
    const char *reflexivity_negative_types[] = {
        "(Id (Sort (LevelConst 1)) (idx 1) (idx 0))",
        "(Id (Sort (LevelConst 1)) (idx 0) (idx 1))",
        "(Id U1 U0 U0)",
        "(Id U1 U0 U0)",
        "(Sort (LevelConst 1))",
        "(Id U1 U0 U0)"
    };
    const char *reflexivity_negative_labels[] = {
        "reflexivity rejects a different left endpoint",
        "reflexivity rejects a different right endpoint",
        "reflexivity does not erase a ground inhabitant's carrier",
        "reflexivity does not lower the subject's universe",
        "reflexivity rejects a non-identity expected type",
        "reflexivity checks an unused bad argument before conversion"
    };
    for (size_t i = 0u;
         i < sizeof reflexivity_negative_sources /
                 sizeof reflexivity_negative_sources[0]; ++i) {
        CettaPrimeRegularKernelResult rejected = check_term(
            &arena, reflexivity_negative_sources[i],
            reflexivity_negative_types[i]);
        CHECK(rejected.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
              reflexivity_negative_labels[i]);
    }

    Atom *raised_reflexivity_term = parse_one(&arena, raised_reflexivity_source);
    Atom *raised_reflexivity_expected = parse_one(&arena, raised_reflexivity_type);
    CettaPrimeRegularKernelBudget reflexivity_measure;
    cetta_prime_regular_kernel_budget_init(
        &reflexivity_measure, true, UINT64_MAX);
    CettaPrimeRegularKernelResult measured_reflexivity =
        cetta_prime_regular_kernel_check(
            &arena, raised_reflexivity_term, raised_reflexivity_expected,
            &reflexivity_measure);
    uint64_t reflexivity_steps = reflexivity_measure.spent;
    CHECK(measured_reflexivity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              reflexivity_steps > 0u,
          "expected-carrier reflexivity reports its structural checking cost");
    if (measured_reflexivity.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        reflexivity_steps > 0u) {
        for (uint64_t fuel = 0u; fuel <= reflexivity_steps; ++fuel) {
            CettaPrimeRegularKernelBudget budget;
            cetta_prime_regular_kernel_budget_init(&budget, true, fuel);
            CettaPrimeRegularKernelResult result = cetta_prime_regular_kernel_check(
                &arena, raised_reflexivity_term, raised_reflexivity_expected,
                &budget);
            CHECK(result.status == (fuel < reflexivity_steps
                      ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                      : CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED),
                  "every shorter reflexivity budget exhausts without refutation");
        }
    }

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

    /* Closed levels beyond the natural numbers are written
     * `(LevelCantor exponent coefficient remainder)`, in Cantor normal
     * form.  Below: omega, omega + 1, omega * 2 and omega^2 + omega * 3 + 5. */
    const char *omega_universe =
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))";
    const char *omega_successor_universe =
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1)))";
    const char *omega_double_universe =
        "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0)))";
    const char *scoped_omega_universe =
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))))";
    CettaPrimeRegularKernelResult omega_synthesis = synth(
        &arena, scoped_omega_universe, false, 0u);
    CHECK(result_type_is(
              &arena, &omega_synthesis,
              "(Sort (LevelSucc "
              "  (LevelCantor (LevelConst 1) 1 (LevelConst 0))))"),
          "the universe at omega inhabits its successor universe");
    CettaPrimeRegularKernelResult omega_successor_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelSucc "
        "  (LevelCantor (LevelConst 1) 1 (LevelConst 0)))))",
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1))))");
    CettaPrimeRegularKernelResult omega_maximum_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelMax (LevelConst 7) "
        "  (LevelCantor (LevelConst 1) 1 (LevelConst 0)))))",
        scoped_omega_universe);
    CettaPrimeRegularKernelResult omega_distinct_conversion = convert(
        &arena, scoped_omega_universe,
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1))))");
    CHECK(omega_successor_conversion.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              omega_maximum_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              omega_distinct_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "successor and maximum of a level beyond the natural numbers are "
          "its notations, and distinct notations are distinct universes");
    CettaPrimeRegularKernelResult finite_below_omega = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Sort (LevelConst 3)))",
        omega_universe);
    CettaPrimeRegularKernelResult omega_below_double = check_term(
        &arena, scoped_omega_universe, omega_double_universe);
    CettaPrimeRegularKernelResult omega_in_successor = check_term(
        &arena, scoped_omega_universe, omega_successor_universe);
    CettaPrimeRegularKernelResult omega_not_in_itself = check_term(
        &arena, scoped_omega_universe, omega_universe);
    CettaPrimeRegularKernelResult omega_not_finite = check_term(
        &arena, scoped_omega_universe, "(Sort (LevelConst 7))");
    CHECK(finite_below_omega.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              omega_below_double.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              omega_in_successor.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              omega_not_in_itself.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              omega_not_finite.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "universe membership follows the order of the notations");
    Atom *omega_sum_universe = parse_one(
        &arena,
        "(Sort (LevelCantor (LevelConst 2) 1 "
        "  (LevelCantor (LevelConst 1) 3 (LevelConst 5))))");
    Atom *omega_sum_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, omega_sum_universe);
    Atom *omega_tower_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(
                &arena,
                "(Sort (LevelSucc (LevelCantor "
                "  (LevelCantor (LevelConst 1) 1 (LevelConst 1)) 2 "
                "  (LevelConst 0))))"));
    CHECK(cetta_prime_regular_kernel_term_is_universe_sort_v1(
              omega_sum_universe) &&
              omega_sum_quoted &&
              atom_eq(
                  omega_sum_quoted,
                  parse_one(
                      &arena, "(u (+ (^ omega 2) (* omega 3) 5))")) &&
              omega_tower_quoted &&
              atom_eq(
                  omega_tower_quoted,
                  parse_one(
                      &arena, "(u (+ (* (^ omega (+ omega 1)) 2) 1))")),
          "a closed universe is quoted in the notation an author writes");
    static const char *const malformed_constants[] = {
        /* 1 + omega: the exponents increase. */
        "(Sort (LevelCantor (LevelConst 0) 1 "
        "  (LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
        /* omega + omega: the exponents do not decrease. */
        "(Sort (LevelCantor (LevelConst 1) 1 "
        "  (LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
        /* A natural number is written `LevelConst`. */
        "(Sort (LevelCantor (LevelConst 0) 3 (LevelConst 0)))",
        /* A coefficient is positive. */
        "(Sort (LevelCantor (LevelConst 1) 0 (LevelConst 0)))",
        /* Exponent and remainder are constants. */
        "(Sort (LevelCantor (LevelParam 0) 1 (LevelConst 0)))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelSucc (LevelConst 0))))",
    };
    bool malformed_constants_declined = true;
    for (size_t index = 0u;
         index < sizeof malformed_constants / sizeof malformed_constants[0];
         index++) {
        Atom *malformed = parse_one(&arena, malformed_constants[index]);
        CettaPrimeRegularKernelBudget malformed_budget;
        cetta_prime_regular_kernel_budget_init(&malformed_budget, false, 0u);
        CettaPrimeRegularKernelResult malformed_synthesis =
            cetta_prime_regular_kernel_synth_intrinsic_v1(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), malformed,
                &malformed_budget);
        if (!malformed ||
            cetta_prime_regular_kernel_term_is_universe_sort_v1(malformed) ||
            cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
                &arena, malformed) ||
            malformed_synthesis.status !=
                CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS)
            malformed_constants_declined = false;
    }
    CHECK(malformed_constants_declined,
          "a constant that is not a Cantor normal form is no level");
    /* A tower of omegas of any height is a level: the kernel reads its
     * constant, however deep, without recursing on it, and judges with its
     * value.  Forty omegas, four thousand and a hundred thousand. */
    static const unsigned tower_heights[] = {40u, 4000u, 100000u};
    Atom *zero_level_constant = parse_one(&arena, "(LevelConst 0)");
    for (size_t index = 0u;
         index < sizeof tower_heights / sizeof tower_heights[0]; index++) {
        Atom *tall_tower = parse_one(&arena, "(LevelConst 1)");
        for (unsigned height = 0u;
             height < tower_heights[index] && tall_tower; height++) {
            Atom *items[4] = {
                atom_symbol(&arena, "LevelCantor"), tall_tower,
                atom_int(&arena, 1), zero_level_constant,
            };
            tall_tower = atom_expr(&arena, items, 4u);
        }
        Atom *tall_universe = tall_tower
            ? atom_expr2(&arena, atom_symbol(&arena, "Sort"), tall_tower)
            : NULL;
        Atom *taller_items[4] = {
            atom_symbol(&arena, "LevelCantor"), tall_tower,
            atom_int(&arena, 1), zero_level_constant,
        };
        Atom *taller_universe = tall_tower
            ? atom_expr2(
                  &arena, atom_symbol(&arena, "Sort"),
                  atom_expr(&arena, taller_items, 4u))
            : NULL;
        CettaPrimeRegularKernelBudget tall_budget;
        cetta_prime_regular_kernel_budget_init(&tall_budget, false, 0u);
        CettaPrimeRegularKernelResult small_in_tall =
            cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"),
                parse_one(&arena, "(Sort (LevelConst 0))"), tall_universe,
                &tall_budget);
        CettaPrimeRegularKernelResult tall_in_small =
            cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), tall_universe,
                parse_one(
                    &arena,
                    "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))"),
                &tall_budget);
        CettaPrimeRegularKernelResult tall_in_taller =
            cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), tall_universe,
                taller_universe, &tall_budget);
        CettaPrimeRegularKernelResult tall_in_itself =
            cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), tall_universe,
                tall_universe, &tall_budget);
        CHECK(tall_universe &&
                  cetta_prime_regular_kernel_term_is_universe_sort_v1(
                      tall_universe) &&
                  small_in_tall.status ==
                      CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                  tall_in_taller.status ==
                      CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
              "a universe at a tower of omegas of any height holds the "
              "universes below it");
        CHECK(tall_in_small.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
                  tall_in_itself.status ==
                      CETTA_PRIME_REGULAR_KERNEL_REFUTED,
              "a universe at a tower of omegas is in no universe at or below "
              "it");
    }

    /* A numeral of the wire has any number of digits: as a natural level,
     * as a coefficient and inside an exponent. */
    static const char *const long_level =
        "(Sort (LevelConst 99999999999999999999999999))";
    static const char *const long_level_before =
        "(Sort (LevelConst 99999999999999999999999998))";
    static const char *const long_coefficient =
        "(Sort (LevelCantor (LevelConst 1) 99999999999999999999999999 "
        " (LevelConst 0)))";
    static const char *const long_exponents =
        "(Sort (LevelCantor (LevelConst 99999999999999999999999999) 1 "
        " (LevelCantor (LevelConst 99999999999999999999999998) 2 "
        "  (LevelCantor (LevelConst 5) 1 "
        "   (LevelConst 18446744073709551616)))))";
    Atom *long_level_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, long_level));
    Atom *long_successor_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena,
            parse_one(
                &arena,
                "(Sort (LevelSucc (LevelConst 99999999999999999999999999)))"));
    Atom *long_coefficient_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, long_coefficient));
    Atom *long_exponents_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, long_exponents));
    CHECK(cetta_prime_regular_kernel_term_is_universe_sort_v1(
              parse_one(&arena, long_level)) &&
              cetta_prime_regular_kernel_term_is_universe_sort_v1(
                  parse_one(&arena, long_coefficient)) &&
              cetta_prime_regular_kernel_term_is_universe_sort_v1(
                  parse_one(&arena, long_exponents)) &&
              long_level_quoted &&
              atom_eq(
                  long_level_quoted,
                  parse_one(&arena, "(u 99999999999999999999999999)")) &&
              long_successor_quoted &&
              atom_eq(
                  long_successor_quoted,
                  parse_one(&arena, "(u 100000000000000000000000000)")) &&
              long_coefficient_quoted &&
              atom_eq(
                  long_coefficient_quoted,
                  parse_one(
                      &arena, "(u (* omega 99999999999999999999999999))")) &&
              long_exponents_quoted &&
              atom_eq(
                  long_exponents_quoted,
                  parse_one(
                      &arena,
                      "(u (+ (^ omega 99999999999999999999999999) "
                      "      (* (^ omega 99999999999999999999999998) 2) "
                      "      (^ omega 5) 18446744073709551616))")),
          "a closed universe with long numerals is quoted with every digit");
    char scoped_long_level[160];
    char scoped_long_before[160];
    snprintf(scoped_long_level, sizeof scoped_long_level,
             "(PrimeScoped PrimeCtxNil %s)", long_level);
    snprintf(scoped_long_before, sizeof scoped_long_before,
             "(PrimeScoped PrimeCtxNil %s)", long_level_before);
    CettaPrimeRegularKernelResult long_synthesis = synth(
        &arena, scoped_long_level, false, 0u);
    CettaPrimeRegularKernelResult long_before_in_long = check_term(
        &arena, scoped_long_before, long_level);
    CettaPrimeRegularKernelResult long_in_before = check_term(
        &arena, scoped_long_level, long_level_before);
    CettaPrimeRegularKernelResult long_in_itself = check_term(
        &arena, scoped_long_level, long_level);
    CettaPrimeRegularKernelResult long_in_omega = check_term(
        &arena, scoped_long_level, omega_universe);
    CettaPrimeRegularKernelResult long_in_small = check_term(
        &arena, scoped_long_level, "(Sort (LevelConst 9223372036854775807))");
    CettaPrimeRegularKernelResult long_successor_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelSucc (LevelConst 99999999999999999999999999))))",
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelConst 100000000000000000000000000)))");
    CettaPrimeRegularKernelResult long_distinct_conversion = convert(
        &arena, scoped_long_level, scoped_long_before);
    CettaPrimeRegularKernelResult long_maximum_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelMax "
        "  (LevelConst 99999999999999999999999998) "
        "  (LevelConst 99999999999999999999999999))))",
        scoped_long_level);
    CHECK(result_type_is(
              &arena, &long_synthesis,
              "(Sort (LevelSucc (LevelConst 99999999999999999999999999)))") &&
              long_before_in_long.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              long_in_omega.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              long_successor_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              long_maximum_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "universes at long numerals are ordered, raised and joined as "
          "their numbers are");
    CHECK(long_in_before.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              long_in_itself.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              long_in_small.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              long_distinct_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "a universe at a long numeral is in no universe at or below it, "
          "and is not the universe at the numeral before");
    static const char *const malformed_long_constants[] = {
        /* A numeral is not negative, however long. */
        "(Sort (LevelConst -99999999999999999999999999))",
        "(Sort (LevelCantor (LevelConst 1) -99999999999999999999999999 "
        "  (LevelConst 0)))",
        /* The exponents do not decrease: both long, and one long. */
        "(Sort (LevelCantor (LevelConst 99999999999999999999999998) 1 "
        "  (LevelCantor (LevelConst 99999999999999999999999999) 1 "
        "   (LevelConst 0))))",
        "(Sort (LevelCantor (LevelConst 5) 1 "
        "  (LevelCantor (LevelConst 99999999999999999999999999) 1 "
        "   (LevelConst 0))))",
        "(Sort (LevelCantor (LevelConst 99999999999999999999999999) 1 "
        "  (LevelCantor (LevelConst 99999999999999999999999999) 1 "
        "   (LevelConst 0))))",
        /* A natural number is written `LevelConst`, however long. */
        "(Sort (LevelCantor (LevelConst 0) 99999999999999999999999999 "
        "  (LevelConst 0)))",
    };
    bool malformed_long_declined = true;
    for (size_t index = 0u;
         index < sizeof malformed_long_constants /
                     sizeof malformed_long_constants[0];
         index++) {
        Atom *malformed = parse_one(&arena, malformed_long_constants[index]);
        CettaPrimeRegularKernelBudget malformed_budget;
        cetta_prime_regular_kernel_budget_init(&malformed_budget, false, 0u);
        CettaPrimeRegularKernelResult malformed_synthesis =
            cetta_prime_regular_kernel_synth_intrinsic_v1(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), malformed,
                &malformed_budget);
        if (!malformed ||
            cetta_prime_regular_kernel_term_is_universe_sort_v1(malformed) ||
            cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
                &arena, malformed) ||
            malformed_synthesis.status !=
                CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS)
            malformed_long_declined = false;
    }
    CHECK(malformed_long_declined,
          "a constant with long numerals that is not a Cantor normal form "
          "is no level");
    /* A constant of thousands of terms: the kernel reads it without
     * recursing on its length.  omega^6000 + ... + omega^2 + omega. */
    Atom *long_constant = zero_level_constant;
    for (int64_t exponent = 1; exponent <= 6000 && long_constant;
         exponent++) {
        Atom *items[4] = {
            atom_symbol(&arena, "LevelCantor"),
            atom_expr2(
                &arena, atom_symbol(&arena, "LevelConst"),
                atom_int(&arena, exponent)),
            atom_int(&arena, 1), long_constant,
        };
        long_constant = atom_expr(&arena, items, 4u);
    }
    Atom *long_universe = long_constant
        ? atom_expr2(&arena, atom_symbol(&arena, "Sort"), long_constant)
        : NULL;
    Atom *long_universe_quoted = long_universe
        ? cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
              &arena, long_universe)
        : NULL;
    CettaPrimeRegularKernelBudget long_constant_budget;
    cetta_prime_regular_kernel_budget_init(&long_constant_budget, false, 0u);
    CettaPrimeRegularKernelResult long_constant_below =
        cetta_prime_regular_kernel_check_intrinsic(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), long_universe,
            parse_one(
                &arena, "(Sort (LevelCantor (LevelConst 6001) 1 "
                        " (LevelConst 0)))"),
            &long_constant_budget);
    CettaPrimeRegularKernelResult long_constant_not_below =
        cetta_prime_regular_kernel_check_intrinsic(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), long_universe,
            parse_one(
                &arena, "(Sort (LevelCantor (LevelConst 6000) 1 "
                        " (LevelConst 0)))"),
            &long_constant_budget);
    CHECK(long_universe &&
              cetta_prime_regular_kernel_term_is_universe_sort_v1(
                  long_universe) &&
              long_universe_quoted &&
              long_universe_quoted->expr.elems[1]->kind == ATOM_EXPR &&
              long_universe_quoted->expr.elems[1]->expr.len == 6001u &&
              long_constant_below.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              long_constant_not_below.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "a constant of six thousand terms is a level the kernel judges "
          "with");
    /* The kernel pays one step for each constant it reads.  A budget the
     * constant exceeds leaves the judgment incomplete: it is neither
     * established nor refuted. */
    CettaPrimeRegularKernelBudget long_constant_short_budget;
    cetta_prime_regular_kernel_budget_init(&long_constant_short_budget, true, 500u);
    CettaPrimeRegularKernelResult long_constant_unfinished =
        cetta_prime_regular_kernel_check_intrinsic(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), long_universe,
            parse_one(
                &arena, "(Sort (LevelCantor (LevelConst 6001) 1 "
                        " (LevelConst 0)))"),
            &long_constant_short_budget);
    cetta_prime_regular_kernel_budget_init(&long_constant_short_budget, true, 500u);
    CettaPrimeRegularKernelResult long_constant_unfinished_refutation =
        cetta_prime_regular_kernel_check_intrinsic(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), long_universe,
            parse_one(
                &arena, "(Sort (LevelCantor (LevelConst 6000) 1 "
                        " (LevelConst 0)))"),
            &long_constant_short_budget);
    CHECK(long_constant_unfinished.status ==
              CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED &&
              long_constant_unfinished_refutation.status ==
                  CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "a budget a long constant exceeds leaves its judgment incomplete");

    /* The levels above every level an author writes, `(LevelAbove n)`: the
     * level of the sort of all sets (n = 0), of its sort (n = 1), and so on.
     * They are closed levels of the wire like the others: a universe at one
     * is a universe, the universe at a written level is a member of it, and
     * it is equal to itself. */
    static const char *const above_zero_universe = "(Sort (LevelAbove 0))";
    static const char *const above_one_universe = "(Sort (LevelAbove 1))";
    static const char *const scoped_above_zero =
        "(PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))";
    static const char *const scoped_above_one =
        "(PrimeScoped PrimeCtxNil (Sort (LevelAbove 1)))";
    static const char *const scoped_long_above =
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelAbove 99999999999999999999999999)))";
    CettaPrimeRegularKernelResult above_first_check = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Sort (LevelConst 0)))",
        above_zero_universe);
    CettaPrimeRegularKernelResult above_first_conversion = convert(
        &arena, scoped_above_zero, scoped_above_zero);
    CHECK(above_first_check.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              above_first_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "a level above every written level is a closed level of the wire");
    CettaPrimeRegularKernelResult above_synthesis = synth(
        &arena, scoped_above_zero, false, 0u);
    CettaPrimeRegularKernelResult above_successor_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelSucc (LevelAbove 0))))",
        scoped_above_one);
    CettaPrimeRegularKernelResult above_maximum_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil (Sort (LevelMax "
        "  (LevelCantor (LevelCantor (LevelConst 1) 1 (LevelConst 0)) 7 "
        "   (LevelConst 3)) (LevelAbove 0))))",
        scoped_above_zero);
    CettaPrimeRegularKernelResult above_long_successor_conversion = convert(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelSucc (LevelAbove 99999999999999999999999999))))",
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelAbove 100000000000000000000000000)))");
    CettaPrimeRegularKernelResult above_distinct_conversion = convert(
        &arena, scoped_above_zero, scoped_above_one);
    CettaPrimeRegularKernelResult above_not_omega_conversion = convert(
        &arena, scoped_above_zero, scoped_omega_universe);
    CHECK(cetta_prime_regular_kernel_term_is_universe_sort_v1(
              parse_one(&arena, above_zero_universe)) &&
              result_type_is(
                  &arena, &above_synthesis,
                  "(Sort (LevelSucc (LevelAbove 0)))") &&
              above_successor_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              above_maximum_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              above_long_successor_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "the sort above every written level inhabits the next one, and "
          "is the maximum with any written level");
    CHECK(above_distinct_conversion.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              above_not_omega_conversion.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "distinct levels above every written level are distinct "
          "universes, and none is a written one");
    static const char *const below_above[] = {
        "(PrimeScoped PrimeCtxNil (Sort (LevelConst 0)))",
        "(PrimeScoped PrimeCtxNil (Sort (LevelConst 7)))",
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelConst 99999999999999999999999999)))",
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
        "(PrimeScoped PrimeCtxNil (Sort (LevelCantor "
        "  (LevelCantor (LevelCantor (LevelConst 1) 1 (LevelConst 0)) 1 "
        "   (LevelConst 0)) 3 (LevelConst 2))))",
    };
    bool written_in_above = true;
    bool above_in_written = false;
    for (size_t index = 0u;
         index < sizeof below_above / sizeof below_above[0]; index++) {
        Atom *written = NULL;
        cetta_prime_regular_kernel_unwrap_scoped(
            parse_one(&arena, below_above[index]), NULL, &written);
        if (check_term(&arena, below_above[index], above_zero_universe)
                    .status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
            check_term(&arena, below_above[index], above_one_universe)
                    .status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            written_in_above = false;
        CettaPrimeRegularKernelBudget above_budget;
        cetta_prime_regular_kernel_budget_init(&above_budget, false, 0u);
        if (cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"),
                parse_one(&arena, above_zero_universe), written,
                &above_budget).status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            above_in_written = true;
    }
    CHECK(written_in_above,
          "every universe at a written level is a member of the sorts above "
          "them all");
    CHECK(!above_in_written,
          "the sort above every written level is a member of no universe at "
          "a written level");
    CettaPrimeRegularKernelResult above_in_next = check_term(
        &arena, scoped_above_zero, above_one_universe);
    CettaPrimeRegularKernelResult above_in_long = check_term(
        &arena, scoped_above_one,
        "(Sort (LevelAbove 99999999999999999999999999))");
    CettaPrimeRegularKernelResult above_in_itself = check_term(
        &arena, scoped_above_zero, above_zero_universe);
    CettaPrimeRegularKernelResult above_next_in_above = check_term(
        &arena, scoped_above_one, above_zero_universe);
    CettaPrimeRegularKernelResult above_long_in_next = check_term(
        &arena, scoped_long_above, above_one_universe);
    CHECK(above_in_next.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              above_in_long.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "the levels above every written level are ordered by their "
          "number");
    CHECK(above_in_itself.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              above_next_in_above.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              above_long_in_next.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "a sort above every written level is in no sort at or below it");
    /* No author writes a level above every written level; the sorts at
     * those levels are written by name: the sort of all sets is `set`, its
     * sort is `class`, and the n-th is `(class n)`. */
    Atom *quoted_above_zero =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, above_zero_universe));
    Atom *quoted_above_succ =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, "(Sort (LevelSucc (LevelAbove 0)))"));
    Atom *quoted_above_long =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena,
            parse_one(&arena, "(Sort (LevelAbove 99999999999999999999999999))"));
    Atom *quoted_above_max =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena,
            parse_one(&arena,
                      "(Sort (LevelMax (LevelConst 7) (LevelAbove 2)))"));
    CHECK(quoted_above_zero &&
              atom_eq(quoted_above_zero, parse_one(&arena, "set")) &&
              quoted_above_succ &&
              atom_eq(quoted_above_succ, parse_one(&arena, "class")) &&
              quoted_above_long &&
              atom_eq(quoted_above_long,
                      parse_one(&arena,
                                "(class 99999999999999999999999999)")) &&
              quoted_above_max &&
              atom_eq(quoted_above_max, parse_one(&arena, "(class 2)")),
          "a sort above every written level is written by its name");
    Atom *quoted_omega =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena, parse_one(&arena, omega_universe));
    CHECK(quoted_omega && atom_eq(quoted_omega, parse_one(&arena, "(u omega)")) &&
              cetta_prime_regular_kernel_quote_sort_above_v1(
                  &arena, parse_one(&arena, omega_universe)) == NULL &&
              cetta_prime_regular_kernel_quote_sort_above_v1(
                  &arena, parse_one(&arena, "(Sort (LevelParam 0))")) == NULL &&
              cetta_prime_regular_kernel_quote_sort_above_v1(
                  &arena, parse_one(&arena, "set")) == NULL,
          "a universe at a written level keeps the notation an author "
          "writes, and has no name");
    /* The names read back: `set`, `class` and `(class n)` are the sorts
     * they name; other symbols and malformed indices are not. */
    Atom *numeral = NULL;
    CHECK(cetta_prime_regular_kernel_sort_above_word_v1(
              parse_one(&arena, "set")) &&
              cetta_prime_regular_kernel_sort_above_word_v1(
                  parse_one(&arena, "class")) &&
              !cetta_prime_regular_kernel_sort_above_word_v1(
                  parse_one(&arena, "sets")) &&
              !cetta_prime_regular_kernel_sort_above_word_v1(
                  parse_one(&arena, "(class 2)")),
          "the names of the sorts above the written universes are two "
          "words");
    CHECK(cetta_prime_regular_kernel_sort_above_spelling_v1(
              &arena, parse_one(&arena, "class"), &numeral) &&
              numeral && atom_eq(numeral, parse_one(&arena, "1")) &&
              cetta_prime_regular_kernel_sort_above_spelling_v1(
                  &arena, parse_one(&arena, "(class 99999999999999999999999999)"),
                  &numeral) &&
              numeral &&
              atom_eq(numeral, parse_one(&arena, "99999999999999999999999999")) &&
              !cetta_prime_regular_kernel_sort_above_spelling_v1(
                  &arena, parse_one(&arena, "(class -1)"), &numeral) &&
              numeral == NULL &&
              !cetta_prime_regular_kernel_sort_above_spelling_v1(
                  &arena, parse_one(&arena, "(class omega)"), NULL) &&
              !cetta_prime_regular_kernel_sort_above_spelling_v1(
                  &arena, parse_one(&arena, "(class 1 2)"), NULL),
          "a spelling of a sort above names its number, of any length");
    Atom *above_term = cetta_prime_regular_kernel_sort_above_term_v1(
        &arena, parse_one(&arena, "0"));
    CHECK(above_term && atom_eq(above_term, parse_one(&arena, above_zero_universe)) &&
              cetta_prime_regular_kernel_sort_above_term_v1(
                  &arena, parse_one(&arena, "omega")) == NULL,
          "the kernel term of the sort of all sets is the universe at the "
          "first level above the written ones");
    static const char *const malformed_above[] = {
        /* No part of a Cantor normal form. */
        "(Sort (LevelCantor (LevelAbove 0) 1 (LevelConst 0)))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelAbove 0)))",
        "(Sort (LevelCantor (LevelConst 1) (LevelAbove 0) (LevelConst 0)))",
        /* Its number is a numeral that is not negative, and it has one. */
        "(Sort (LevelAbove banana))",
        "(Sort (LevelAbove -1))",
        "(Sort (LevelAbove -99999999999999999999999999))",
        "(Sort (LevelAbove (LevelConst 0)))",
        "(Sort (LevelAbove 0 1))",
        "(Sort (LevelAbove))",
        "(Sort LevelAbove)",
    };
    bool malformed_above_declined = true;
    for (size_t index = 0u;
         index < sizeof malformed_above / sizeof malformed_above[0];
         index++) {
        Atom *malformed = parse_one(&arena, malformed_above[index]);
        CettaPrimeRegularKernelBudget malformed_budget;
        cetta_prime_regular_kernel_budget_init(&malformed_budget, false, 0u);
        CettaPrimeRegularKernelResult malformed_synthesis =
            cetta_prime_regular_kernel_synth_intrinsic_v1(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), malformed,
                &malformed_budget);
        if (!malformed ||
            cetta_prime_regular_kernel_term_is_universe_sort_v1(malformed) ||
            malformed_synthesis.status !=
                CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS)
            malformed_above_declined = false;
    }
    CHECK(malformed_above_declined,
          "a level above every written level is a whole constant with a "
          "numeral, and no part of a Cantor normal form");
    /* One raise of a parameter being solved.  Under a successor a bound
     * steps back by one, and under a counted successor by its count, as the
     * level library says, whatever the length of the numerals.  A parameter
     * stands for a level an author writes, so a bound above every such
     * level is reached by no assignment: the raise is refuted and assigns
     * nothing.  A closed side of a maximum that reaches such a bound
     * demands nothing of the parameter. */
    static const struct {
        const char *level;
        const char *bound;
        CettaPrimeRegularKernelStatus status;
        const char *held;
    } raises[] = {
        {"(LevelSucc (LevelParam 7))",
         "(LevelConst 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "(LevelConst 99999999999999999999999999)"},
        {"(LevelOffset (LevelParam 7) 2)",
         "(LevelConst 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "(LevelConst 99999999999999999999999998)"},
        {"(LevelOffset (LevelParam 7) 99999999999999999999999999)",
         "(LevelConst 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, "(LevelConst 1)"},
        {"(LevelOffset (LevelParam 7) 100000000000000000000000001)",
         "(LevelConst 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, "(LevelConst 0)"},
        {"(LevelOffset (LevelParam 7) 20000)",
         "(LevelCantor (LevelConst 1) 1 (LevelConst 5))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "(LevelCantor (LevelConst 1) 1 (LevelConst 0))"},
        {"(LevelOffset (LevelParam 7) 3)",
         "(LevelCantor (LevelConst 1) 1 (LevelConst 5))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "(LevelCantor (LevelConst 1) 1 (LevelConst 2))"},
        {"(LevelOffset (LevelSucc (LevelParam 7)) 2)", "(LevelConst 9)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, "(LevelConst 6)"},
        {"(LevelOffset (LevelParam 7) 0)", "(LevelConst 9)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, "(LevelConst 9)"},
        {"(LevelParam 7)", "(LevelAbove 0)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelSucc (LevelParam 7))", "(LevelAbove 0)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelSucc (LevelParam 7))", "(LevelAbove 1)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelOffset (LevelParam 7) 99999999999999999999999999)",
         "(LevelAbove 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelOffset (LevelParam 7) 100000000000000000000000001)",
         "(LevelAbove 100000000000000000000000000)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelMax (LevelParam 7) (LevelConst 5))", "(LevelAbove 0)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelMax (LevelAbove 1) (LevelParam 7))", "(LevelAbove 2)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelSucc (LevelParam 7))",
         "(LevelMax (LevelParam 99) (LevelAbove 0))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelParam 7)", "(LevelSucc (LevelAbove 0))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelMax (LevelParam 7) (LevelConst 5))",
         "(LevelMax (LevelParam 99) (LevelAbove 0))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL},
        {"(LevelMax (LevelParam 7) (LevelAbove 3))", "(LevelAbove 2)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL},
        {"(LevelMax (LevelParam 7) (LevelAbove 3))",
         "(LevelMax (LevelParam 99) (LevelAbove 2))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL},
        {"(LevelSucc (LevelMax (LevelAbove 5) (LevelParam 7)))",
         "(LevelAbove 6)", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL},
    };
    for (size_t index = 0u; index < sizeof raises / sizeof raises[0];
         index++) {
        uint64_t raised_parameter[1] = {7u};
        Atom *raised_assignment[1] = {NULL};
        bool outside = true;
        CettaPrimeRegularKernelBudget raise_budget;
        cetta_prime_regular_kernel_budget_init(&raise_budget, false, 0u);
        CettaPrimeRegularKernelStatus raise_status =
            cetta_prime_regular_kernel_raise_level_parameters_v1(
                &arena, parse_one(&arena, raises[index].level),
                parse_one(&arena, raises[index].bound), raised_parameter,
                raised_assignment, 1u, &raise_budget, &outside);
        Atom *held = raises[index].held
            ? parse_one(&arena, raises[index].held) : NULL;
        bool as_expected = raise_status == raises[index].status &&
            !outside &&
            (held ? raised_assignment[0] != NULL &&
                        atom_eq(raised_assignment[0], held)
                  : raised_assignment[0] == NULL);
        if (!as_expected)
            fprintf(
                stderr, "the raise of %s to %s is not as expected\n",
                raises[index].level, raises[index].bound);
        CHECK(as_expected,
              "a parameter is raised to a written bound by the least "
              "assignment, and to a bound above every written level by "
              "none");
    }
    /* A level parameter stands for a level an author writes, so the
     * universe at a parameter, raised by any count, is a member of the sort
     * above every written level; and that sort is a member of no universe
     * at a parameter.  The parameter 99 here is a rigid one: nothing solves
     * it. */
    static const struct {
        const char *term;
        const char *expected;
        CettaPrimeRegularKernelStatus status;
        const char *name;
    } parameter_memberships[] = {
        {"(Sort (LevelParam 99))", "(Sort (LevelAbove 0))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "the universe at a level parameter is a set"},
        {"(Sort (LevelSucc (LevelParam 99)))", "(Sort (LevelAbove 0))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "the universe at the successor of a level parameter is a set"},
        {"(Sort (LevelOffset (LevelParam 99) 99999999999999999999999999))",
         "(Sort (LevelAbove 0))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "the universe at a level parameter raised by a long count is a "
         "set"},
        {"(Sort (LevelMax (LevelParam 99) "
         "  (LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
         "(Sort (LevelAbove 0))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "the universe at a maximum of a level parameter and omega is a "
         "set"},
        {"(Sort (LevelMax (LevelParam 99) (LevelAbove 0)))",
         "(Sort (LevelAbove 1))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a maximum with the level of the sets is the level of the sets"},
        {"(Sort (LevelAbove 0))", "(Sort (LevelParam 99))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "the sort of all sets is a member of no universe at a level "
         "parameter"},
        {"(Sort (LevelAbove 0))",
         "(Sort (LevelOffset (LevelParam 99) 99999999999999999999999999))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "the sort of all sets is a member of no universe at a level "
         "parameter raised by a long count"},
        {"(Sort (LevelMax (LevelParam 99) (LevelAbove 0)))",
         "(Sort (LevelAbove 0))", CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "a maximum with the level of the sets is no member of the sort of "
         "all sets"},
        {"(Sort (LevelParam 99))",
         "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "the universe at a level parameter is not below a written level: "
         "the parameter may stand for a higher one"},
    };
    for (size_t index = 0u;
         index < sizeof parameter_memberships /
                     sizeof parameter_memberships[0];
         index++) {
        CettaPrimeRegularKernelBudget membership_budget;
        cetta_prime_regular_kernel_budget_init(&membership_budget, false, 0u);
        CettaPrimeRegularKernelResult membership =
            cetta_prime_regular_kernel_check_intrinsic(
                &arena, atom_symbol(&arena, "PrimeCtxNil"),
                parse_one(&arena, parameter_memberships[index].term),
                parse_one(&arena, parameter_memberships[index].expected),
                &membership_budget);
        CHECK(membership.status == parameter_memberships[index].status,
              parameter_memberships[index].name);
    }
    /* The counted successor `(LevelOffset level count)` is the level as
     * many successors up as its numeral counts: over a constant it is the
     * constant raised, whatever the length of the count, and it is equal to
     * that many `LevelSucc`. */
    static const struct {
        const char *left;
        const char *right;
        CettaPrimeRegularKernelStatus status;
        const char *name;
    } offset_conversions[] = {
        {"(Sort (LevelOffset (LevelConst 3) 4))", "(Sort (LevelConst 7))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "three raised by a count of four is seven"},
        {"(Sort (LevelOffset (LevelConst 3) 0))", "(Sort (LevelConst 3))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a count of zero raises nothing"},
        {"(Sort (LevelOffset (LevelConst 3) 1))",
         "(Sort (LevelSucc (LevelConst 3)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a count of one is the successor"},
        {"(Sort (LevelOffset (LevelConst 3) 2))",
         "(Sort (LevelSucc (LevelSucc (LevelConst 3))))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a count of two is two successors"},
        {"(Sort (LevelOffset (LevelConst 1) 99999999999999999999999999))",
         "(Sort (LevelConst 100000000000000000000000000))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a count of twenty-six digits raises a constant by that number"},
        {"(Sort (LevelOffset (LevelCantor (LevelConst 1) 1 (LevelConst 0)) "
         "  20000))",
         "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 20000)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "omega raised by a count of twenty thousand is omega + 20000"},
        {"(Sort (LevelOffset (LevelOffset (LevelConst 0) 20000) 5))",
         "(Sort (LevelConst 20005))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "counts add"},
        {"(Sort (LevelOffset (LevelAbove 0) 2))", "(Sort (LevelAbove 2))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "above every written level a count adds to the number"},
        {"(Sort (LevelMax (LevelOffset (LevelConst 3) 4) (LevelConst 6)))",
         "(Sort (LevelConst 7))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a counted successor is read under a maximum"},
        {"(Sort (LevelOffset (LevelConst 3) 4))", "(Sort (LevelConst 8))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "three raised by a count of four is not eight"},
        {"(Sort (LevelOffset (LevelConst 1) 99999999999999999999999999))",
         "(Sort (LevelConst 99999999999999999999999999))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "one raised by a long count is not the count"},
        {"(Sort (LevelOffset (LevelCantor (LevelConst 1) 1 (LevelConst 0)) "
         "  20000))",
         "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "omega raised by a count is not omega"},
    };
    for (size_t index = 0u;
         index < sizeof offset_conversions / sizeof offset_conversions[0];
         index++) {
        char left_scoped[512];
        char right_scoped[512];
        snprintf(
            left_scoped, sizeof left_scoped, "(PrimeScoped PrimeCtxNil %s)",
            offset_conversions[index].left);
        snprintf(
            right_scoped, sizeof right_scoped, "(PrimeScoped PrimeCtxNil %s)",
            offset_conversions[index].right);
        CettaPrimeRegularKernelResult offset_conversion =
            convert(&arena, left_scoped, right_scoped);
        CHECK(offset_conversion.status == offset_conversions[index].status,
              offset_conversions[index].name);
    }
    CettaPrimeRegularKernelResult offset_member = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Sort (LevelConst 5)))",
        "(Sort (LevelOffset (LevelConst 3) 3))");
    CettaPrimeRegularKernelResult offset_not_member = check_term(
        &arena, "(PrimeScoped PrimeCtxNil (Sort (LevelConst 5)))",
        "(Sort (LevelOffset (LevelConst 3) 2))");
    CettaPrimeRegularKernelResult offset_sort_type = synth(
        &arena,
        "(PrimeScoped PrimeCtxNil "
        "  (Sort (LevelOffset (LevelConst 3) 99999999999999999999999999)))",
        false, 0u);
    CHECK(offset_member.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
              offset_not_member.status ==
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              result_type_is(
                  &arena, &offset_sort_type,
                  "(Sort (LevelSucc (LevelOffset (LevelConst 3) "
                  " 99999999999999999999999999)))"),
          "a universe at a counted successor has the members and the type "
          "of the universe at that level");
    Atom *offset_quoted =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            &arena,
            parse_one(
                &arena,
                "(Sort (LevelOffset (LevelConst 3) "
                " 99999999999999999999999999))"));
    CHECK(offset_quoted &&
              atom_eq(
                  offset_quoted,
                  parse_one(&arena, "(u 100000000000000000000000002)")),
          "a closed counted successor is written as the level it is");
    static const char *const malformed_offsets[] = {
        "(Sort (LevelOffset (LevelConst 3) banana))",
        "(Sort (LevelOffset (LevelConst 3) -1))",
        "(Sort (LevelOffset (LevelConst 3) -99999999999999999999999999))",
        "(Sort (LevelOffset (LevelConst 3) (LevelConst 1)))",
        "(Sort (LevelOffset (LevelConst 3)))",
        "(Sort (LevelOffset (LevelConst 3) 1 1))",
        "(Sort (LevelOffset 3 1))",
        "(Sort (LevelOffset banana 1))",
        "(Sort (LevelCantor (LevelOffset (LevelConst 1) 1) 1 (LevelConst 0)))",
    };
    bool malformed_offsets_declined = true;
    for (size_t index = 0u;
         index < sizeof malformed_offsets / sizeof malformed_offsets[0];
         index++) {
        Atom *malformed = parse_one(&arena, malformed_offsets[index]);
        CettaPrimeRegularKernelBudget malformed_budget;
        cetta_prime_regular_kernel_budget_init(&malformed_budget, false, 0u);
        CettaPrimeRegularKernelResult malformed_synthesis =
            cetta_prime_regular_kernel_synth_intrinsic_v1(
                &arena, atom_symbol(&arena, "PrimeCtxNil"), malformed,
                &malformed_budget);
        if (!malformed ||
            malformed_synthesis.status !=
                CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS) {
            fprintf(
                stderr, "the malformed level %s is not declined\n",
                malformed_offsets[index]);
            malformed_offsets_declined = false;
        }
    }
    CHECK(malformed_offsets_declined,
          "a counted successor takes a level and a numeral that is not "
          "negative, and is no part of a Cantor normal form");
    /* A declaration has no instance at a level above every level an author
     * writes: its level parameter stands for a written level.  Written with
     * such a level, the declared constant has no type, and the reason says
     * why.  At a written level, however long, it has its type. */
    static const struct {
        const char *term;
        const char *type;
    } declared_instances[] = {
        {"(DeclConst list (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
         "(Pi (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))) "
         "    (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))))"},
        {"(DeclConst list (LevelOffset (LevelConst 3) "
         "  99999999999999999999999999))",
         "(Pi (Sort (LevelOffset (LevelConst 3) "
         "      99999999999999999999999999)) "
         "    (Sort (LevelOffset (LevelConst 3) "
         "      99999999999999999999999999)))"},
        {"(DeclConst list (LevelParam 99))",
         "(Pi (Sort (LevelParam 99)) (Sort (LevelParam 99)))"},
        {"(DeclConst list (LevelAbove 0))", NULL},
        {"(DeclConst list (LevelAbove 99999999999999999999999999))", NULL},
        {"(DeclConst list (LevelSucc (LevelAbove 0)))", NULL},
        {"(DeclConst list (LevelOffset (LevelAbove 0) 2))", NULL},
        {"(DeclConst list (LevelMax (LevelConst 3) (LevelAbove 1)))", NULL},
        {"(DeclConst list (LevelMax (LevelAbove 0) (LevelParam 99)))", NULL},
        {"(App (DeclConst list (LevelAbove 0)) (Sort (LevelConst 0)))", NULL},
    };
    for (size_t index = 0u;
         index < sizeof declared_instances / sizeof declared_instances[0];
         index++) {
        CettaPrimeRegularKernelBudget instance_budget;
        cetta_prime_regular_kernel_budget_init(&instance_budget, false, 0u);
        CettaPrimeRegularKernelResult instance =
            cetta_prime_regular_kernel_synth_intrinsic_v1(
                &arena, schema_context,
                parse_one(&arena, declared_instances[index].term),
                &instance_budget);
        if (declared_instances[index].type)
            CHECK(result_type_is(
                      &arena, &instance, declared_instances[index].type),
                  "a declaration is used at a written level");
        else
            CHECK(instance.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
                      instance.type == NULL && instance.reason &&
                      strcmp(instance.reason,
                             "level-instantiation-above-written-levels") ==
                          0,
                  "a declaration has no instance above every written level");
    }

    /* A run of one-step successors written by hand is decoded without
     * recursing on its length: twenty thousand nested `LevelSucc` over a
     * parameter are the counted successor `(LevelOffset l 20000)`, and over
     * a constant they raise it by that much.  A run may mix the two forms.
     * Each successor is still a step of the budget. */
    enum { nested_successors = 20000 };
    Atom *nested_over_parameter = parse_one(&arena, "(LevelParam 7)");
    Atom *nested_over_constant = parse_one(&arena, "(LevelConst 3)");
    Atom *nested_mixed = parse_one(&arena, "(LevelOffset (LevelParam 7) 10000)");
    for (unsigned i = 0u; i < nested_successors; i++) {
        nested_over_parameter = atom_expr2(
            &arena, atom_symbol(&arena, "LevelSucc"), nested_over_parameter);
        nested_over_constant = atom_expr2(
            &arena, atom_symbol(&arena, "LevelSucc"), nested_over_constant);
        if (i < nested_successors / 2u)
            nested_mixed = atom_expr2(
                &arena, atom_symbol(&arena, "LevelSucc"), nested_mixed);
    }
    static const struct {
        int left;
        const char *right;
        CettaPrimeRegularKernelStatus status;
        const char *name;
    } nested_runs[] = {
        {0, "(LevelOffset (LevelParam 7) 20000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "twenty thousand nested successors of a parameter are the counted "
         "successor"},
        {0, "(LevelOffset (LevelParam 7) 19999)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "twenty thousand nested successors are not one fewer"},
        {0, "(LevelOffset (LevelParam 8) 20000)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "nested successors of one parameter are not those of another"},
        {1, "(LevelConst 20003)", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "twenty thousand nested successors raise a constant by that much"},
        {1, "(LevelConst 20004)", CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "and not by more"},
        {2, "(LevelOffset (LevelParam 7) 20000)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a run that mixes one-step and counted successors adds them up"},
    };
    for (size_t index = 0u;
         index < sizeof nested_runs / sizeof nested_runs[0]; index++) {
        Atom *left = nested_runs[index].left == 0 ? nested_over_parameter
                   : nested_runs[index].left == 1 ? nested_over_constant
                   : nested_mixed;
        CettaPrimeRegularKernelBudget nested_budget;
        cetta_prime_regular_kernel_budget_init(&nested_budget, false, 0u);
        CettaPrimeRegularKernelConversionDecision nested =
            cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
                &arena, atom_symbol(&arena, "PrimeCtxNil"),
                atom_expr2(&arena, atom_symbol(&arena, "Sort"), left),
                atom_expr2(
                    &arena, atom_symbol(&arena, "Sort"),
                    parse_one(&arena, nested_runs[index].right)),
                &nested_budget);
        CHECK(nested.status == nested_runs[index].status,
              nested_runs[index].name);
    }
    CettaPrimeRegularKernelBudget nested_short_budget;
    cetta_prime_regular_kernel_budget_init(
        &nested_short_budget, true, nested_successors / 2u);
    CettaPrimeRegularKernelConversionDecision nested_short =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            atom_expr2(
                &arena, atom_symbol(&arena, "Sort"), nested_over_parameter),
            atom_expr2(
                &arena, atom_symbol(&arena, "Sort"),
                parse_one(&arena, "(LevelOffset (LevelParam 7) 20000)")),
            &nested_short_budget);
    CHECK(nested_short.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
          "a budget shorter than a run of successors leaves the judgment "
          "incomplete");

    /* A declaration is used at its least level instance.  `list` below is
     * applied to a type of the universe written as the domain, at a level
     * written over the parameter being solved; the formed schema shows the
     * instance the kernel assigned.  Under a successor the bound steps back
     * by one where it is a successor and stays where it is zero or a limit;
     * under a maximum with a closed side the parameter is raised only for
     * what that side does not reach. */
    static const struct {
        const char *domain_level;
        const char *occurrence_level;
        const char *solved_level;
        const char *name;
    } least_instances[] = {
        {"(LevelConst 3)", "(LevelSucc (LevelParam 7))",
         "(LevelSucc (LevelConst 2))",
         "under a successor a successor bound steps back by one"},
        {"(LevelCantor (LevelConst 1) 1 (LevelConst 0))",
         "(LevelSucc (LevelParam 7))",
         "(LevelSucc (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
         "under a successor a limit bound stays"},
        {"(LevelConst 0)", "(LevelSucc (LevelParam 7))",
         "(LevelSucc (LevelConst 0))",
         "a bound already reached raises nothing"},
        {"(LevelCantor (LevelConst 1) 1 (LevelConst 1))",
         "(LevelSucc (LevelSucc (LevelParam 7)))",
         "(LevelSucc (LevelSucc "
         " (LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
         "under two successors omega + 1 steps back once and then stays"},
        {"(LevelConst 5)", "(LevelSucc (LevelSucc (LevelParam 7)))",
         "(LevelSucc (LevelSucc (LevelConst 3)))",
         "under two successors a natural bound steps back twice"},
        {"(LevelSucc (LevelParam 99))", "(LevelSucc (LevelParam 7))",
         "(LevelSucc (LevelParam 99))",
         "the level under the successor of a parameter is the parameter"},
        {"(LevelConst 3)", "(LevelMax (LevelParam 7) (LevelConst 5))",
         "(LevelMax (LevelConst 0) (LevelConst 5))",
         "a closed side that reaches the bound demands nothing"},
        {"(LevelCantor (LevelConst 1) 1 (LevelConst 0))",
         "(LevelMax (LevelParam 7) (LevelConst 5))",
         "(LevelMax (LevelCantor (LevelConst 1) 1 (LevelConst 0)) "
         " (LevelConst 5))",
         "a closed side that does not reach the bound leaves it to the parameter"},
        {"(LevelConst 7)",
         "(LevelSucc (LevelMax (LevelConst 5) (LevelParam 7)))",
         "(LevelSucc (LevelMax (LevelConst 5) (LevelConst 6)))",
         "a successor over a maximum steps back before the maximum is read"},
        /* A counted successor steps the bound back by its count at once,
         * whatever the length of the count. */
        {"(LevelConst 5)", "(LevelOffset (LevelParam 7) 2)",
         "(LevelOffset (LevelConst 3) 2)",
         "under a count of two a natural bound steps back twice"},
        {"(LevelCantor (LevelConst 1) 1 (LevelConst 1))",
         "(LevelOffset (LevelParam 7) 2)",
         "(LevelOffset (LevelCantor (LevelConst 1) 1 (LevelConst 0)) 2)",
         "under a count of two omega + 1 steps back once and then stays"},
        {"(LevelConst 3)", "(LevelOffset (LevelParam 7) 20000)",
         "(LevelOffset (LevelConst 0) 20000)",
         "a count of twenty thousand that reaches the bound raises nothing"},
        {"(LevelConst 20005)", "(LevelOffset (LevelParam 7) 20000)",
         "(LevelOffset (LevelConst 5) 20000)",
         "under a count of twenty thousand a bound steps back that far"},
        {"(LevelConst 100000000000000000000000000)",
         "(LevelOffset (LevelParam 7) 99999999999999999999999999)",
         "(LevelOffset (LevelConst 1) 99999999999999999999999999)",
         "under a count of twenty-six digits a long bound steps back by "
         "it"},
        {"(LevelConst 3)",
         "(LevelOffset (LevelParam 7) 99999999999999999999999999)",
         "(LevelOffset (LevelConst 0) 99999999999999999999999999)",
         "a count of twenty-six digits that reaches the bound raises "
         "nothing"},
        {"(LevelOffset (LevelParam 99) 5)", "(LevelOffset (LevelParam 7) 2)",
         "(LevelOffset (LevelOffset (LevelParam 99) 3) 2)",
         "under a count a counted bound keeps what is left of its count"},
        {"(LevelOffset (LevelParam 99) 5)", "(LevelOffset (LevelParam 7) 4)",
         "(LevelOffset (LevelSucc (LevelParam 99)) 4)",
         "a count of one that is left is written as the successor"},
        {"(LevelOffset (LevelParam 99) 5)", "(LevelOffset (LevelParam 7) 5)",
         "(LevelOffset (LevelParam 99) 5)",
         "a count that removes all of a counted bound leaves the level"},
        {"(LevelSucc (LevelSucc (LevelParam 99)))",
         "(LevelOffset (LevelParam 7) 2)",
         "(LevelOffset (LevelParam 99) 2)",
         "a count of two removes two successors"},
        {"(LevelConst 9)",
         "(LevelOffset (LevelMax (LevelConst 5) (LevelParam 7)) 2)",
         "(LevelOffset (LevelMax (LevelConst 5) (LevelConst 7)) 2)",
         "a count over a maximum steps back before the maximum is read"},
    };
    for (size_t index = 0u;
         index < sizeof least_instances / sizeof least_instances[0];
         index++) {
        char schema_text[512];
        char solved_text[512];
        snprintf(
            schema_text, sizeof schema_text,
            "(Pi (Sort %s) (App (DeclConst list %s) (idx 0)))",
            least_instances[index].domain_level,
            least_instances[index].occurrence_level);
        snprintf(
            solved_text, sizeof solved_text,
            "(Pi (Sort %s) (App (DeclConst list %s) (idx 0)))",
            least_instances[index].domain_level,
            least_instances[index].solved_level);
        CettaPrimeRegularKernelBudget least_budget;
        cetta_prime_regular_kernel_budget_init(&least_budget, false, 0u);
        CettaPrimeRegularKernelFormedSchemaV1 least =
            cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
                &arena, schema_context, parse_one(&arena, schema_text),
                fresh_level_parameter, 1u, &least_budget);
        Atom *solved = parse_one(&arena, solved_text);
        CHECK(least.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                  least.term && solved && atom_eq(least.term, solved),
              least_instances[index].name);
    }
    /* Where no least instance exists, or it is no level expression, the
     * kernel assigns none and abstains: the level under a bare parameter is
     * none, and a maximum with parameters on both sides is reached by
     * raising either. */
    uint64_t two_level_parameters[] = {7u, 8u};
    static const struct {
        const char *schema;
        size_t parameter_count;
        const char *name;
    } no_least_instance[] = {
        {"(Pi (Sort (LevelParam 99)) "
         "  (App (DeclConst list (LevelSucc (LevelParam 7))) (idx 0)))",
         1u, "the level under a bare parameter is no level expression"},
        {"(Pi (Sort (LevelConst 3)) "
         "  (App (DeclConst list "
         "    (LevelMax (LevelParam 7) (LevelParam 8))) (idx 0)))",
         2u, "a maximum of two parameters has no least instance"},
        {"(Pi (Sort (LevelSucc (LevelParam 99))) "
         "  (App (DeclConst list "
         "    (LevelMax (LevelParam 7) (LevelConst 5))) (idx 0)))",
         1u, "an open bound against a maximum with a closed side is not decided"},
        {"(Pi (Sort (LevelSucc (LevelParam 99))) "
         "  (App (DeclConst list (LevelOffset (LevelParam 7) 2)) (idx 0)))",
         1u, "the level two under the successor of a parameter is no level "
             "expression"},
        {"(Pi (Sort (LevelOffset (LevelParam 99) 5)) "
         "  (App (DeclConst list "
         "    (LevelOffset (LevelParam 7) 99999999999999999999999999)) "
         "   (idx 0)))",
         1u, "the level a long count under a parameter raised by five is no "
             "level expression"},
    };
    for (size_t index = 0u;
         index < sizeof no_least_instance / sizeof no_least_instance[0];
         index++) {
        CettaPrimeRegularKernelBudget abstain_budget;
        cetta_prime_regular_kernel_budget_init(&abstain_budget, false, 0u);
        CettaPrimeRegularKernelFormedSchemaV1 abstained =
            cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
                &arena, schema_context,
                parse_one(&arena, no_least_instance[index].schema),
                two_level_parameters,
                no_least_instance[index].parameter_count, &abstain_budget);
        CHECK(abstained.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS &&
                  abstained.term == NULL && abstained.reason &&
                  strcmp(abstained.reason,
                         "level-instantiation-outside-fragment") == 0,
              no_least_instance[index].name);
    }
    /* A level that still has a parameter nothing has bounded is compared at
     * the least value of that parameter, zero: the type of `idx 0` below is
     * the universe at max(p, 1), which is usable at the universe at 1 and at
     * every higher one, and at no lower one. */
    Atom *unbounded_context = parse_one(
        &arena,
        "(PrimeCtxCons "
        "  (Sort (LevelMax (LevelParam 7) (LevelConst 1))) PrimeCtxNil)");
    static const struct {
        const char *expected;
        CettaPrimeRegularKernelStatus status;
        const char *name;
    } unbounded_checks[] = {
        {"(Sort (LevelConst 1))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "an unbounded parameter is at zero where that makes the order hold"},
        {"(Sort (LevelConst 2))", CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "an order that holds at zero holds at a higher universe"},
        {"(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "an unbounded parameter is at zero below a limit level"},
        {"(Sort (LevelConst 0))", CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "an order that fails at zero fails at every instance"},
    };
    for (size_t index = 0u;
         index < sizeof unbounded_checks / sizeof unbounded_checks[0];
         index++) {
        CettaPrimeRegularKernelBudget unbounded_budget;
        cetta_prime_regular_kernel_budget_init(&unbounded_budget, false, 0u);
        CettaPrimeRegularKernelResult unbounded =
            cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
                &arena, unbounded_context, instantiated_index,
                parse_one(&arena, unbounded_checks[index].expected),
                fresh_level_parameter, 1u, &unbounded_budget);
        CHECK(unbounded.status == unbounded_checks[index].status,
              unbounded_checks[index].name);
    }
    /* The instance is the least assignment at which the whole judgment
     * holds, whatever the order of the uses.  What a parameter holds during
     * the search is a lower bound on it, so an equation raises it and is
     * not refuted from it; usability of a type asks for no equation; a
     * parameter identified with another one is raised with it; and a raise
     * that arrives after a comparison was made repeats the search
     * (Mettapedia, TypeTheory/UniverseLevel/LeastInstance.lean). */
    static const struct {
        const char *context;
        const char *term;
        const char *expected;
        size_t parameter_count;
        CettaPrimeRegularKernelStatus status;
        const char *reason;
        const char *name;
    } level_searches[] = {
        {"(PrimeCtxCons (Pi (Sort (LevelConst 5)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 3)) PrimeCtxNil))",
         "(Pair (idx 1) (idx 0))",
         "(Sigma (Sort (LevelParam 7)) (Pi (Sort (LevelParam 7)) U0))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a bound of 3 and then an equation at 5 have the instance 5"},
        {"(PrimeCtxCons (Pi (Sort (LevelConst 5)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 3)) PrimeCtxNil))",
         "(Pair (idx 0) (idx 1))",
         "(Sigma (Pi (Sort (LevelParam 7)) U0) (Sort (LevelParam 7)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "an equation at 5 and then a bound of 3 have the instance 5"},
        {"(PrimeCtxCons (Pi (Sort (LevelConst 5)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 7)) PrimeCtxNil))",
         "(Pair (idx 1) (idx 0))",
         "(Sigma (Sort (LevelParam 7)) (Pi (Sort (LevelParam 7)) U0))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED, "type-mismatch",
         "a bound of 7 and then an equation at 5 have no instance"},
        {"(PrimeCtxCons (Pi (Sort (LevelConst 5)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 7)) PrimeCtxNil))",
         "(Pair (idx 0) (idx 1))",
         "(Sigma (Pi (Sort (LevelParam 7)) U0) (Sort (LevelParam 7)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED, "type-mismatch",
         "an equation at 5 and then a bound of 7 have no instance"},
        {"(PrimeCtxCons "
         " (Sigma (Sort (LevelParam 7)) (Sort (LevelParam 7))) PrimeCtxNil)",
         "(idx 0)",
         "(Sigma (Sort (LevelConst 5)) (Sort (LevelConst 2)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "usability at two upper bounds raises nothing"},
        {"(PrimeCtxCons "
         " (Sigma (Sort (LevelSucc (LevelParam 7))) (Sort (LevelParam 7))) "
         " PrimeCtxNil)",
         "(idx 0)",
         "(Sigma (Sort (LevelConst 0)) (Sort (LevelConst 2)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED, "type-mismatch",
         "an upper bound that fails at zero fails at every instance"},
        {"(PrimeCtxCons (Pi (Sort (LevelParam 7)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 5)) PrimeCtxNil))",
         "(Pair (idx 0) (idx 1))",
         "(Sigma (Pi (Sort (LevelParam 8)) U0) (Sort (LevelParam 8)))",
         2u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a parameter identified with another one is raised with it"},
        {"(PrimeCtxCons (Pi (Sort (LevelParam 7)) U0) "
         " (PrimeCtxCons (Sort (LevelConst 5)) "
         "  (PrimeCtxCons (Sort (LevelConst 3)) PrimeCtxNil)))",
         "(Pair (idx 2) (Pair (idx 0) (idx 1)))",
         "(Sigma (Sort (LevelParam 7)) "
         " (Sigma (Pi (Sort (LevelParam 8)) U0) (Sort (LevelParam 8))))",
         2u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a raise after an equation was read repeats the search"},
        {"(PrimeCtxCons (Sort (LevelSucc (LevelParam 7))) "
         " (PrimeCtxCons (Sort (LevelParam 8)) PrimeCtxNil))",
         "(Pair (idx 0) (idx 1))",
         "(Sigma (Sort (LevelParam 8)) (Sort (LevelParam 7)))",
         2u, CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
         "level-instantiation-does-not-settle",
         "two levels that must exceed each other are not refuted from a raise"},
        /* A level parameter stands for a level an author writes.  A use
         * that would need an instance above every such level has none: the
         * judgment is refuted, and the reason says so.  The variable below
         * is a set, a member of the sort above every written level; where
         * it is a type of a written universe instead, the instance is that
         * universe. */
        {"(PrimeCtxCons (Sort (LevelAbove 0)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelParam 7))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "a set is a type of no universe at a level parameter"},
        {"(PrimeCtxCons "
         " (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))) PrimeCtxNil)",
         "(idx 0)", "(Sort (LevelParam 7))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a type of the universe at omega is a type of the universe at a "
         "level parameter, at omega"},
        {"(PrimeCtxCons (Sort (LevelAbove 1)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelSucc (LevelParam 7)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "under a successor a bound above every written level stays above"},
        {"(PrimeCtxCons (Sort (LevelAbove 0)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelOffset (LevelParam 7) 99999999999999999999999999))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "no count raises a level parameter to the sort of all sets"},
        {"(PrimeCtxCons (Sort (LevelAbove 0)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelMax (LevelParam 7) (LevelConst 5)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "a written side of a maximum does not reach the sort of all sets"},
        {"(PrimeCtxCons (Sort (LevelAbove 0)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelMax (LevelParam 7) (LevelAbove 1)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a side of a maximum above every written level reaches the sort "
         "of all sets by itself"},
        {"(PrimeCtxCons (Pi (Sort (LevelAbove 0)) U0) PrimeCtxNil)",
         "(idx 0)", "(Pi (Sort (LevelParam 7)) U0)",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "an equation of a level parameter with the level of the sets has "
         "no instance"},
        {"(PrimeCtxCons (Pi (Sort (LevelParam 7)) U0) PrimeCtxNil)",
         "(idx 0)", "(Pi (Sort (LevelAbove 0)) U0)",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "level-instantiation-above-written-levels",
         "and none with the two sides exchanged"},
        {"(PrimeCtxCons (Sort (LevelParam 7)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelAbove 0))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a type of the universe at a level parameter is a set"},
        {"(PrimeCtxCons (Sort (LevelOffset (LevelParam 7) 20000)) "
         " PrimeCtxNil)",
         "(idx 0)", "(Sort (LevelAbove 0))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a type of the universe at a level parameter raised by twenty "
         "thousand is a set"},
        {"(PrimeCtxCons (Sort (LevelConst 20005)) PrimeCtxNil)", "(idx 0)",
         "(Sort (LevelOffset (LevelParam 7) 20000))",
         1u, CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL,
         "a count of twenty thousand is judged as a small count is"},
        {"(PrimeCtxCons (Sort (LevelConst 20005)) PrimeCtxNil)", "(idx 0)",
         "(Sigma (Sort (LevelOffset (LevelParam 7) 20000)) "
         "       (Sort (LevelConst 4)))",
         1u, CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL,
         "and a universe is no pair type under any count"},
    };
    for (size_t index = 0u;
         index < sizeof level_searches / sizeof level_searches[0];
         index++) {
        CettaPrimeRegularKernelBudget search_budget;
        cetta_prime_regular_kernel_budget_init(&search_budget, false, 0u);
        CettaPrimeRegularKernelResult searched =
            cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
                &arena, parse_one(&arena, level_searches[index].context),
                parse_one(&arena, level_searches[index].term),
                parse_one(&arena, level_searches[index].expected),
                two_level_parameters,
                level_searches[index].parameter_count, &search_budget);
        CHECK(searched.status == level_searches[index].status &&
                  (!level_searches[index].reason ||
                   (searched.reason &&
                    strcmp(searched.reason,
                           level_searches[index].reason) == 0)),
              level_searches[index].name);
    }
    /* The conversion question with level parameters asks for an instance at
     * which the two terms are equal at a common type.  Their synthesized
     * types need a common upper bound, not equality, so a universe in result
     * position asks nothing of a parameter, and a parameter that an equation
     * of the typing fixes is not raised past it.  The first context
     * variable takes a family over the universe at parameter 7 and returns a
     * type of the next universe; applied to a family over the universe at 3
     * it is typed at 3 only. */
    static const char *const level_conversion_context =
        "(PrimeCtxCons "
        " (Pi (Pi (Sort (LevelParam 7)) U0) (Sort (LevelSucc (LevelParam 7)))) "
        " (PrimeCtxCons (Pi (Sort (LevelConst 3)) U0) "
        "  (PrimeCtxCons "
        "   (Pi (Pi (Sort (LevelParam 7)) U0) "
        "       (Pi U0 (Sort (LevelSucc (LevelParam 7))))) "
        "   (PrimeCtxCons "
        "    (Pi (Pi (Sort (LevelConst 3)) U0) (Sort (LevelConst 4))) "
        "    PrimeCtxNil))))";
    static const struct {
        const char *left;
        const char *right;
        CettaPrimeRegularKernelStatus status;
        const char *name;
    } level_conversions[] = {
        {"(App (idx 0) (idx 1))",
         "(App (Lam (Sort (LevelConst 6)) (idx 0)) (App (idx 0) (idx 1)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a term typed at 3 only equals itself seen in a larger universe"},
        {"(App (Lam (Sort (LevelConst 6)) (idx 0)) (App (idx 0) (idx 1)))",
         "(App (idx 0) (idx 1))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "and with the two sides exchanged"},
        {"(App (idx 2) (idx 1))",
         "(App (Lam (Pi U0 (Sort (LevelConst 6))) (idx 0)) "
         " (App (idx 2) (idx 1)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a family into a universe equals itself seen as a family into a "
         "larger one"},
        {"(App (idx 3) (idx 1))",
         "(App (Lam (Sort (LevelConst 6)) (idx 0)) (App (idx 3) (idx 1)))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "the same question with the instance written out"},
        {"(App (idx 0) (idx 1))", "(App (idx 3) (idx 1))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "different variables are equal at no instance"},
        {"(App (idx 0) (idx 1))", "(idx 1)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "a universe and a function type have no common type"},
    };
    for (size_t index = 0u;
         index < sizeof level_conversions / sizeof level_conversions[0];
         index++) {
        CettaPrimeRegularKernelBudget conversion_budget;
        cetta_prime_regular_kernel_budget_init(&conversion_budget, false, 0u);
        CettaPrimeRegularKernelConversionDecision converted =
            cetta_prime_regular_kernel_decide_intrinsic_conversion_instantiating_levels_v1(
                &arena, parse_one(&arena, level_conversion_context),
                parse_one(&arena, level_conversions[index].left),
                parse_one(&arena, level_conversions[index].right),
                two_level_parameters, 1u, &conversion_budget);
        CHECK(converted.status == level_conversions[index].status &&
                  converted.equal ==
                      (level_conversions[index].status ==
                       CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED),
              level_conversions[index].name);
    }

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
    CHECK(bad_lambda.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              bad_lambda.reason &&
              strcmp(bad_lambda.reason, "lambda-domain-mismatch") == 0,
          "a written domain that is another type former than the expected domain is refuted");

    /* Both domains are dependent function types: the mismatch lies inside
     * one former, which only complete conversion could refute. */
    CettaPrimeRegularKernelResult open_lambda_mismatch = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil (Lam (Pi U0 U0) (idx 0)))",
        "(Pi (Pi U0 (Pi U0 U0)) (Pi U0 (Pi U0 U0)))");
    CHECK(open_lambda_mismatch.status == CETTA_PRIME_REGULAR_KERNEL_UNDECIDED &&
              open_lambda_mismatch.reason &&
              strcmp(open_lambda_mismatch.reason,
                     "lambda-domain-mismatch") == 0,
          "a written domain differing inside the same former stays undecided");

    /* Universes with distinct closed levels are distinct types. */
    CettaPrimeRegularKernelResult universe_lambda_mismatch = check_term(
        &arena,
        "(PrimeScoped PrimeCtxNil (Lam (Sort (LevelConst 0)) (idx 0)))",
        "(Pi (Sort (LevelConst 1)) (Sort (LevelConst 1)))");
    CHECK(universe_lambda_mismatch.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED &&
              universe_lambda_mismatch.reason &&
              strcmp(universe_lambda_mismatch.reason,
                     "lambda-domain-mismatch") == 0,
          "a written universe of another level than the expected domain is refuted");

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
              strcmp(distinct.reason, "not-convertible") == 0,
          "distinct same-typed normal forms are rejected");

    CettaPrimeRegularKernelResult context_mismatch = convert(
        &arena, "(PrimeScoped PrimeCtxNil U0)",
        "(PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) U0)");
    CHECK(context_mismatch.status == CETTA_PRIME_REGULAR_KERNEL_UNDECIDED &&
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

    Atom *redex_context = parse_one(&arena,
        "(PrimeCtxCons (idx 0) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))");
    const struct {
        const char *term;
        const char *expected;
        CettaPrimeRegularKernelStatus status;
        const char *label;
    } retained_redex_cases[] = {
        {"(App (Lam (idx 0)) (idx 0))", "(idx 1)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "a retained beta-redex gets its domain from its argument"},
        {"(App (Lam (Refl (idx 0))) (idx 0))", "(Id (idx 1) (idx 0) (idx 0))",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "redex synthesis substitutes in the dependent identity result"},
        {"(App (App (Lam (Lam (idx 1))) (idx 0)) (idx 0))", "(idx 1)",
         CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
         "expected types check a retained curried lambda spine"},
        {"(App (Lam (idx 1)) (App (idx 0) (idx 0)))", "(idx 1)",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "an unused ill-typed argument cannot hide behind beta conversion"},
        {"(App (App (Lam (Lam (idx 1))) (idx 0)) (idx 42))", "(idx 1)",
         CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
         "scope admission declines a discarded loose argument"},
        {"(App (Lam (idx 0)) (idx 0))", "(Sort (LevelConst 0))",
         CETTA_PRIME_REGULAR_KERNEL_REFUTED,
         "retained beta checking rejects the wrong result type"},
    };
    for (size_t i = 0u; i < sizeof retained_redex_cases / sizeof *retained_redex_cases; i++) {
        CettaPrimeRegularKernelBudget redex_budget;
        cetta_prime_regular_kernel_budget_init(&redex_budget, false, 0u);
        CettaPrimeRegularKernelResult redex = cetta_prime_regular_kernel_check_intrinsic(
            &arena, redex_context, parse_one(&arena, retained_redex_cases[i].term),
            parse_one(&arena, retained_redex_cases[i].expected), &redex_budget);
        if (redex.status != retained_redex_cases[i].status)
            fprintf(stderr, "retained redex %zu: status=%d reason=%s\n", i,
                    redex.status, redex.reason ? redex.reason : "none");
        CHECK(redex.status == retained_redex_cases[i].status, retained_redex_cases[i].label);
    }

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
              strcmp(distinct_reason, "not-convertible") == 0,
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
        CHECK(verdict_status(reflexive_verdict, "Undetermined"),
              "out-of-class scoped reflexivity is neither established nor refuted");
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
              strstr(closed_distinct_text, "not-convertible") &&
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
              strstr(declared_distinct_text, "not-convertible") &&
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

    /* Recognizing declarations is not charged to the producer budget, so at
     * every budget a declared conversion completes or reports exhaustion. */
    bool declared_budget_honest = true;
    for (uint64_t steps = 1u; steps <= 16u; steps++) {
        Atom *limited = prime_semantics_judge_typing_direct(
            &arena, &space, declared_reflexive_conversion, true, steps);
        if (!verdict_status(limited, "Incomplete") &&
            !verdict_status(limited, "Established"))
            declared_budget_honest = false;
        limited = prime_semantics_judge_typing_direct(
            &arena, &space, declared_distinct_conversion, true, steps);
        if (!verdict_status(limited, "Incomplete") &&
            !verdict_status(limited, "Refuted"))
            declared_budget_honest = false;
    }
    CHECK(declared_budget_honest,
          "declared conversion under any producer budget completes or reports resource exhaustion");

    /* A name whose declared type lies outside the kernel's class is
     * recognized as outside at every budget, never as exhaustion. */
    Atom *outside_declaration = parse_one(
        &arena, "(: outside-operation (-> Number Number Number))");
    if (outside_declaration) space_add(&space, outside_declaration);
    Atom *outside_conversion = parse_one(
        &arena, "(type:eq (outside-operation 1 1) 2)");
    bool outside_budget_stable = outside_declaration && outside_conversion;
    for (uint64_t steps = 1u; outside_budget_stable && steps <= 16u; steps++) {
        Atom *limited = prime_semantics_judge_typing_direct(
            &arena, &space, outside_conversion, true, steps);
        if (!verdict_status(limited, "Undetermined"))
            outside_budget_stable = false;
    }
    CHECK(outside_budget_stable &&
              verdict_status(prime_semantics_judge_typing_direct(
                  &arena, &space, outside_conversion, false, 0u),
                  "Undetermined"),
          "a term outside the declared fragment is reported outside at every producer budget");

    /* The arithmetic of a level is work of the judgment, in whatever route
     * the level is read: a judgment whose level takes more steps than the
     * producer budget has is incomplete, with and without declared
     * constants, and it is neither refuted nor left open for another reason.
     * Within a budget its level takes it is answered. */
    static const struct {
        const char *judgment;
        uint64_t steps;
        const char *status;
    } level_budgets[] = {
        {"(type:formed (u (^ (+ omega 1) 1000000000)))", 100000u,
         "Incomplete"},
        {"(type:formed (u (^ 2 (^ 2 40))))", 100000u, "Incomplete"},
        {"(type:of (u (^ (+ omega 1) 1000000000)))", 100000u, "Incomplete"},
        {"(type:check (u 0) (u (^ (+ omega 1) 1000000000)))", 100000u,
         "Incomplete"},
        {"(type:eq (u (^ (+ omega 1) 1000000000)) (u 0))", 100000u,
         "Incomplete"},
        {"(type:check declared-small (u (^ (+ omega 1) 1000000000)))",
         100000u, "Incomplete"},
        {"(type:eq declared-identity (u (^ (+ omega 1) 1000000000)))",
         100000u, "Incomplete"},
        {"(type:formed (-> declared-small (u (^ (+ omega 1) 1000000000))))",
         100000u, "Incomplete"},
        {"(type:formed (u (^ (+ omega 1) 5000)))", 20000u, "Incomplete"},
        {"(type:formed (u (^ (+ omega 1) 5000)))", 10000000u, "Established"},
        {"(type:formed (u (^ (+ omega 1) 50)))", 100000u, "Established"},
        {"(type:check (u 5) (u (^ (+ omega 1) 50)))", 100000u,
         "Established"},
        {"(type:check (u (^ (+ omega 1) 50)) (u 5))", 100000u, "Refuted"},
        {"(type:check declared-small (u (^ (+ omega 1) 50)))", 100000u,
         "Refuted"},
        {"(type:eq declared-identity (u (^ (+ omega 1) 50)))", 100000u,
         "Refuted"},
    };
    for (size_t index = 0u;
         index < sizeof level_budgets / sizeof level_budgets[0]; index++) {
        Atom *budgeted = prime_semantics_judge_typing_direct(
            &arena, &space, parse_one(&arena, level_budgets[index].judgment),
            true, level_budgets[index].steps);
        if (!verdict_status(budgeted, level_budgets[index].status))
            fprintf(
                stderr, "%s within %llu steps: %s\n",
                level_budgets[index].judgment,
                (unsigned long long)level_budgets[index].steps,
                budgeted ? atom_to_string(&arena, budgeted) : "no verdict");
        CHECK(verdict_status(budgeted, level_budgets[index].status),
              "the arithmetic of a level counts against the producer "
              "budget");
    }

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
    CHECK(declared_dependent_form_obs.authority.canonical_term &&
              atom_eq(declared_dependent_form_obs.authority.canonical_term,
                  parse_one(&arena, "(Id U0 declared-point declared-point)")),
          "declared formation retains the checked value-indexed type with its actual constant identities");
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
    CHECK(formation_observations[0].authority.canonical_term &&
              atom_eq(formation_observations[0].authority.canonical_term,
                  closed_identity_type) &&
              !formation_observations[1].authority.canonical_term &&
              !formation_observations[2].authority.canonical_term &&
              !formation_observations[3].authority.canonical_term,
          "only established native formation retains an intrinsic type, never an obstruction or exhausted candidate");
    CettaPrimeTypingFormationCandidateV1 alpha_formation_candidates[] = {
        {parse_one(&arena, "(-> (A : (u 0)) (x : A) A)"), false, 0u},
        {parse_one(&arena, "(-> (B : (u 0)) (y : B) B)"), false, 0u},
    };
    CettaPrimeTypingFormationObservationV1 alpha_formation_observations[2] = {0};
    bool alpha_formed = cetta_prime_typing_observe_formation_v1(
              &arena, &space, &alpha_formation_candidates[0],
              &alpha_formation_observations[0]) &&
              cetta_prime_typing_observe_formation_v1(
              &arena, &space, &alpha_formation_candidates[1],
              &alpha_formation_observations[1]);
    bool alpha_retained = alpha_formed &&
              alpha_formation_observations[0].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              alpha_formation_observations[1].authority.result.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              alpha_formation_observations[0].authority.canonical_term &&
              alpha_formation_observations[1].authority.canonical_term &&
              atom_eq(alpha_formation_observations[0].authority.canonical_term,
                      parse_one(&arena, "(Pi (Sort (LevelConst 0)) (Pi (idx 0) (idx 1)))")) &&
              atom_eq(alpha_formation_observations[0].authority.canonical_term,
                      alpha_formation_observations[1].authority.canonical_term);
    CHECK(alpha_retained,
          "alpha variants of the dependent identity telescope retain the same scoped type without a second compiler");
    if (!alpha_retained) {
        fprintf(stderr, "dependent formation retained:");
        for (size_t index = 0u; index < 2u; index++) {
            fputc(' ', stderr);
            if (alpha_formation_observations[index].authority.canonical_term)
                atom_print(alpha_formation_observations[index].authority.canonical_term, stderr);
            else
                fprintf(stderr, "outcome=%d",
                        alpha_formation_observations[index].authority.result.value.outcome);
            if (alpha_formation_observations[index].authority.payload) {
                fputc(' ', stderr);
                atom_print(alpha_formation_observations[index].authority.payload, stderr);
            }
        }
        fputc('\n', stderr);
    }
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
    CHECK(scoped_formation_observations[0].authority.canonical_term &&
              atom_eq(scoped_formation_observations[0].authority.canonical_term,
                  scoped_formation_candidates[0].type) &&
              !scoped_formation_observations[1].authority.canonical_term,
          "a scoped formation receipt retains its context instead of exporting a loose de Bruijn index");

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
