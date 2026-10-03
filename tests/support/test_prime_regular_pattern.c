#include "parser.h"
#include "prime_level.h"
#include "prime_regular_pattern.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t checks;
static size_t failures;

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void check(bool condition, const char *name) {
    checks++;
    if (condition) return;
    failures++;
    fprintf(stderr, "FAIL: %s\n", name);
}

static CettaPrimeRegularPatternElaborationV1 elaborate(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    const char *pattern_text, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    return cetta_prime_regular_pattern_elaborate_v1(
        arena, environment, parse_one(arena, pattern_text), &budget);
}

static void check_elaboration(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    const char *pattern_text, const char *expected_text, const char *name) {
    CettaPrimeRegularPatternElaborationV1 result = elaborate(
        arena, environment, pattern_text, UINT64_C(10000));
    Atom *expected = parse_one(arena, expected_text);
    check(result.status == CETTA_PRIME_REGULAR_PATTERN_OK &&
          result.term && expected && atom_eq(result.term, expected), name);
    if (environment.count == 0u && result.term) {
        CettaPrimeRegularKernelBudget budget;
        cetta_prime_regular_kernel_budget_init(
            &budget, true, UINT64_C(100000));
        CettaPrimeRegularKernelResult consumed =
            cetta_prime_regular_kernel_check_intrinsic(
                arena, atom_symbol(arena, "PrimeCtxNil"), result.term,
                atom_symbol(arena, "U1"), &budget);
        char consumed_name[160];
        snprintf(consumed_name, sizeof(consumed_name),
                 "%s is consumed by the intrinsic checker", name);
        check(consumed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
              consumed.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED ||
              consumed.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
              consumed_name);
    }
}

static void check_syntax_error(
    Arena *arena, const char *pattern_text,
    CettaPrimeRegularPatternSyntaxErrorV1 error, const char *name) {
    CettaPrimeRegularPatternElaborationV1 result = elaborate(
        arena, (CettaPrimeRegularPatternEnvironmentV1){0},
        pattern_text, UINT64_C(10000));
    check(result.status == CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR &&
          result.syntax_error == error, name);
}

static CettaPrimeRegularTermElaborationV1 lower_syntax(
    Arena *arena, const char *regular_term_text, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    return cetta_prime_regular_term_to_pattern_v1(
        arena, parse_one(arena, regular_term_text), &budget);
}

static void check_regular_term_pattern(
    Arena *arena, const char *regular_term_text,
    const char *expected_pattern_text, const char *name) {
    CettaPrimeRegularTermElaborationV1 result = lower_syntax(
        arena, regular_term_text, UINT64_C(100000));
    Atom *expected = parse_one(arena, expected_pattern_text);
    check(result.status == CETTA_PRIME_REGULAR_TERM_OK &&
          result.pattern && expected && atom_eq(result.pattern, expected), name);
}

/* The kernel term of one authored term, or NULL. */
static Atom *regular_term_intrinsic(Arena *arena, const char *syntax) {
    CettaPrimeRegularTermElaborationV1 lowered = lower_syntax(
        arena, syntax, UINT64_C(100000));
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) return NULL;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternElaborationV1 result =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, (CettaPrimeRegularPatternEnvironmentV1){0},
            lowered.pattern, &budget);
    return result.status == CETTA_PRIME_REGULAR_PATTERN_OK
        ? result.term : NULL;
}

/* The kernel term of one authored term given as an atom, within a budget of
 * the caller's, or NULL. */
static Atom *regular_atom_intrinsic_within(
    Arena *arena, Atom *syntax, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    CettaPrimeRegularTermElaborationV1 lowered =
        cetta_prime_regular_term_to_pattern_v1(arena, syntax, &budget);
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) return NULL;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    CettaPrimeRegularPatternElaborationV1 result =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, (CettaPrimeRegularPatternEnvironmentV1){0},
            lowered.pattern, &budget);
    return result.status == CETTA_PRIME_REGULAR_PATTERN_OK
        ? result.term : NULL;
}

static Atom *regular_term_intrinsic_within(
    Arena *arena, const char *syntax, uint64_t steps) {
    return regular_atom_intrinsic_within(
        arena, parse_one(arena, syntax), steps);
}

/* A universe written one way elaborates to `kernel_text`, is quoted as
 * `printed_text`, and what is quoted reads back as the same kernel term. */
static void check_universe_round_trip(
    Arena *arena, const char *written_text, const char *kernel_text,
    const char *printed_text, const char *name) {
    Atom *kernel = regular_term_intrinsic(arena, written_text);
    Atom *expected = parse_one(arena, kernel_text);
    Atom *printed = kernel
        ? cetta_prime_regular_term_quote_intrinsic_v1(arena, kernel) : NULL;
    Atom *expected_printed = parse_one(arena, printed_text);
    Atom *reread = regular_term_intrinsic(arena, printed_text);
    check(kernel && expected && atom_eq(kernel, expected) && printed &&
          expected_printed && atom_eq(printed, expected_printed) && reread &&
          atom_eq(reread, kernel), name);
}

/* A universe whose level is read incompletely, for the reason named: the
 * term is neither lowered nor refused. */
static void check_level_incomplete(
    Arena *arena, const char *syntax, const char *reason, const char *name) {
    CettaPrimeRegularTermElaborationV1 lowered = lower_syntax(
        arena, syntax, UINT64_C(100000));
    check(lowered.status == CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED &&
          lowered.pattern == NULL && lowered.reason &&
          strcmp(lowered.reason, reason) == 0 &&
          cetta_prime_regular_term_level_incomplete_v1(&lowered), name);
}

static void check_regular_term_elaboration(
    Arena *arena, const char *syntax, const char *expected_text,
    const char *name) {
    CettaPrimeRegularTermElaborationV1 lowered = lower_syntax(
        arena, syntax, UINT64_C(100000));
    check(lowered.status == CETTA_PRIME_REGULAR_TERM_OK, name);
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) return;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternElaborationV1 result =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, (CettaPrimeRegularPatternEnvironmentV1){0},
            lowered.pattern, &budget);
    Atom *expected = parse_one(arena, expected_text);
    check(result.status == CETTA_PRIME_REGULAR_PATTERN_OK && result.term &&
          expected && atom_eq(result.term, expected), name);
}

int main(void) {
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    bool universal_names_old =
        parser_set_universal_name_syntax_enabled(true);

    CettaPrimeRegularPatternEnvironmentV1 empty = {0};
    check_elaboration(
        &arena, empty, "(PApp \"U0\" LNil)", "U0", "elaborate U0");
    check_elaboration(
        &arena, empty, "(PApp \"U1\" LNil)", "U1", "elaborate U1");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons "
        " (PApp \"LevelSucc\" (LCons (PApp \"LevelZero\" LNil) LNil)) "
        " LNil))",
        "(Sort (LevelSucc (LevelConst 0)))",
        "elaborate closed universe through structural level syntax");
    check_elaboration(
        &arena, empty,
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (Var 0)) LNil)))",
        "(Pi U0 (idx 0))", "elaborate Pi binder");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sigma\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (PApp \"U0\" LNil)) LNil)))",
        "(Sigma U0 U0)", "elaborate Sigma binder");
    check_elaboration(
        &arena, empty,
        "(PApp \"Id\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PApp \"U0\" LNil) "
        " (LCons (PApp \"U0\" LNil) LNil))))",
        "(Id U0 U0 U0)", "elaborate Id");
    check_elaboration(
        &arena, empty,
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "(Lam (idx 0))", "elaborate intrinsic Lam");
    check_elaboration(
        &arena, empty,
        "(PApp \"App\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PApp \"U1\" LNil) LNil)))",
        "(App U0 U1)", "elaborate App");
    check_elaboration(
        &arena, empty,
        "(PApp \"Pair\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PApp \"U1\" LNil) LNil)))",
        "(Pair U0 U1)", "elaborate Pair");
    check_elaboration(
        &arena, empty,
        "(PApp \"Fst\" (LCons (PApp \"U0\" LNil) LNil))",
        "(Fst U0)", "elaborate Fst");
    check_elaboration(
        &arena, empty,
        "(PApp \"Snd\" (LCons (PApp \"U0\" LNil) LNil))",
        "(Snd U0)", "elaborate Snd");
    check_elaboration(
        &arena, empty,
        "(PApp \"Refl\" (LCons (PApp \"U0\" LNil) LNil))",
        "(Refl U0)", "elaborate Refl");
    check_elaboration(
        &arena, empty,
        "(PApp \"Lam\" (LCons (PLam BNone "
        " (PApp \"Lam\" (LCons (PLam BNone "
        "  (PApp \"App\" (LCons (Var 1) (LCons (Var 0) LNil)))) "
        " LNil))) LNil))",
        "(Lam (Lam (App (idx 1) (idx 0))))", "nested binder indices");

    Atom *x = atom_string(&arena, "x");
    const Atom *open_names[] = {x};
    CettaPrimeRegularPatternEnvironmentV1 open = {
        .names = open_names,
        .count = 1u,
    };
    check_elaboration(&arena, open, "(FVar \"x\")", "(idx 0)",
                      "resolve contextual free variable");
    check_elaboration(
        &arena, open,
        "(PApp \"Lam\" (LCons (PLam BNone (FVar \"x\")) LNil))",
        "(Lam (idx 1))", "context index shifts beneath binder");

    Atom *ab_name = atom_symbol(&arena, "ab");
    Atom *a_name = atom_symbol(&arena, "a");
    const Atom *declaration_names[] = {ab_name, a_name};
    CettaPrimeRegularTermEnvironmentV1 declarations = {
        .names = declaration_names,
        .count = 2u,
    };
    CettaPrimeRegularKernelBudget declaration_budget;
    cetta_prime_regular_kernel_budget_init(
        &declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 declared_application =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, declarations, parse_one(&arena, "(ab a)"),
            &declaration_budget);
    Atom *declared_pattern = parse_one(
        &arena,
        "(PApp \"App\" (LCons (FVar \"ab\") "
        " (LCons (FVar \"a\") LNil)))");
    check(declared_application.status == CETTA_PRIME_REGULAR_TERM_OK &&
          atom_eq(declared_application.pattern, declared_pattern),
          "declared authored names lower to contextual free variables");

    Atom *ab_pattern_name = atom_string(&arena, "ab");
    Atom *a_pattern_name = atom_string(&arena, "a");
    const Atom *declaration_pattern_names[] = {
        ab_pattern_name, a_pattern_name,
    };
    CettaPrimeRegularPatternEnvironmentV1 declaration_pattern_environment = {
        .names = declaration_pattern_names,
        .count = 2u,
    };
    CettaPrimeRegularPatternElaborationV1 declared_intrinsic = elaborate(
        &arena, declaration_pattern_environment,
        "(PApp \"App\" (LCons (FVar \"ab\") "
        " (LCons (FVar \"a\") LNil)))",
        UINT64_C(100000));
    check(declared_intrinsic.status == CETTA_PRIME_REGULAR_PATTERN_OK &&
          atom_eq(declared_intrinsic.term,
                  parse_one(&arena, "(App (idx 0) (idx 1))")),
          "declaration order agrees with intrinsic context indices");

    cetta_prime_regular_kernel_budget_init(
        &declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 declaration_shadowing =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, declarations,
            parse_one(&arena, "(lam ab (ab a))"), &declaration_budget);
    check(declaration_shadowing.status == CETTA_PRIME_REGULAR_TERM_OK &&
          atom_eq(
              declaration_shadowing.pattern,
              parse_one(
                  &arena,
                  "(PApp \"Lam\" (LCons (PLam BNone "
                  " (PApp \"App\" (LCons (Var 0) "
                  "  (LCons (FVar \"a\") LNil)))) LNil))")),
          "lexical binders shadow same-named declarations");

    cetta_prime_regular_kernel_budget_init(
        &declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 unresolved_declaration =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, declarations, atom_symbol(&arena, "missing"),
            &declaration_budget);
    check(unresolved_declaration.status ==
              CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS &&
          atom_is_symbol(unresolved_declaration.unresolved_name, "missing"),
          "missing declaration is reported without semantic refutation");

    Atom *clause = parse_one(&arena, "(scope $A $x (lam z $x))");
    const Atom *clause_names[] = {clause->expr.elems[2], clause->expr.elems[1]};
    CettaPrimeRegularTermEnvironmentV1 clause_environment = {
        .local_names = clause_names, .local_count = 2u,
    };
    cetta_prime_regular_kernel_budget_init(&declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 clause_lowered =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, clause_environment, clause->expr.elems[3], &declaration_budget);
    check(clause_lowered.status == CETTA_PRIME_REGULAR_TERM_OK && clause_lowered.pattern &&
          atom_eq(clause_lowered.pattern, parse_one(&arena,
              "(PApp \"Lam\" (LCons (PLam BNone (Var 1)) LNil))")),
          "clause alias weakens under an actual lexical binder");
    CettaPrimeRegularPatternEnvironmentV1 clause_pattern_environment = {.bound_count = 2u};
    CettaPrimeRegularPatternElaborationV1 clause_intrinsic =
        cetta_prime_regular_pattern_elaborate_v1(
            &arena, clause_pattern_environment, clause_lowered.pattern, &declaration_budget);
    check(clause_intrinsic.status == CETTA_PRIME_REGULAR_PATTERN_OK && clause_intrinsic.term &&
          atom_eq(clause_intrinsic.term, parse_one(&arena, "(Lam (idx 1))")),
          "open clause scope survives the shared Pattern elaborator");
    check_syntax_error(&arena, "(Var 1)",
                      CETTA_PRIME_REGULAR_PATTERN_DANGLING_BOUND_VARIABLE,
                      "open-clause support does not admit dangling closed variables");

    Atom *invalid_binder = parse_one(&arena, "(scope $x (lam $x $x))");
    const Atom *invalid_names[] = {invalid_binder->expr.elems[1]};
    cetta_prime_regular_kernel_budget_init(&declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 clause_matcher_binder =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, (CettaPrimeRegularTermEnvironmentV1){
                .local_names = invalid_names, .local_count = 1u},
            invalid_binder->expr.elems[2], &declaration_budget);
    check(clause_matcher_binder.status != CETTA_PRIME_REGULAR_TERM_OK && !clause_matcher_binder.pattern,
          "a clause alias still cannot be used as a lexical binder");
    cetta_prime_regular_kernel_budget_init(&declaration_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 dangling_matcher =
        cetta_prime_regular_term_to_pattern_in_environment_v1(
            &arena, clause_environment, parse_one(&arena, "$missing"), &declaration_budget);
    check(dangling_matcher.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS && !dangling_matcher.pattern,
          "an unbound clause matcher remains unresolved");

    const Atom *named_constants[] = {a_name};
    CettaPrimeRegularPatternEnvironmentV1 constant_environment = {
        .bound_count = 2u, .declaration_names = named_constants, .declaration_count = 1u,
    };
    CettaPrimeRegularPatternElaborationV1 named_constant = elaborate(
        &arena, constant_environment,
        "(PApp \"Lam\" (LCons (PLam BNone (FVar \"a\")) LNil))", UINT64_C(100000));
    check(named_constant.status == CETTA_PRIME_REGULAR_PATTERN_OK && named_constant.term &&
          atom_eq(named_constant.term, parse_one(&arena, "(Lam (DeclConst a))")),
          "a named declaration is not captured by clause or lexical binders");
    named_constant = elaborate(&arena, constant_environment, "(FVar \"missing\")", UINT64_C(100000));
    check(named_constant.status == CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR && !named_constant.term,
          "named-declaration support does not admit unknown constants");
    named_constant = elaborate(&arena, constant_environment, "(FVar \"a\")", 0u);
    check(named_constant.status == CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED && !named_constant.term,
          "declaration environment validation consumes the same budget");
    const Atom *private_spelling[] = {atom_symbol(&arena, "__pk_0")};
    named_constant = elaborate(&arena, (CettaPrimeRegularPatternEnvironmentV1){
            .declaration_names = private_spelling, .declaration_count = 1u},
        "(PApp \"Lam\" (LCons (PLam BNone (FVar \"__pk_0\")) LNil))", UINT64_C(100000));
    check(named_constant.status == CETTA_PRIME_REGULAR_PATTERN_OK && named_constant.term &&
          atom_eq(named_constant.term, parse_one(&arena, "(Lam (DeclConst __pk_0))")),
          "a declaration's spelling cannot alias an internal generated binder");

    check_syntax_error(
        &arena, "(Var 0)",
        CETTA_PRIME_REGULAR_PATTERN_DANGLING_BOUND_VARIABLE,
        "reject dangling bound variable");
    check_syntax_error(
        &arena, "(FVar \"missing\")",
        CETTA_PRIME_REGULAR_PATTERN_UNKNOWN_FREE_VARIABLE,
        "reject unknown free variable");
    check_syntax_error(
        &arena,
        "(PApp \"Lam\" (LCons (PLam BNone (FVar \"__pk_0\")) LNil))",
        CETTA_PRIME_REGULAR_PATTERN_BINDER_NAME_COLLISION,
        "reject binder-name collision");
    check_syntax_error(
        &arena, "(PApp \"App\" LNil)",
        CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
        "reject malformed constructor arity");
    check_syntax_error(
        &arena,
        "(PApp \"Sort\" (LCons (PApp \"U0\" LNil) LNil))",
        CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
        "reject a non-level payload under Sort");
    check_syntax_error(
        &arena, "(PLam BNone (Var 0))",
        CETTA_PRIME_REGULAR_PATTERN_UNEXPECTED_BINDER,
        "reject unexpected binder");
    check_syntax_error(
        &arena, "(PMultiLam 2 LNil (Var 0))",
        CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_MULTI_BINDER,
        "reject multi binder");
    check_syntax_error(
        &arena, "(PSubst (Var 0) (Var 0))",
        CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_EXPLICIT_SUBSTITUTION,
        "reject explicit substitution");
    check_syntax_error(
        &arena, "(PCollection CSet LNil RNone)",
        CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_COLLECTION,
        "reject collection");

    Atom *duplicate_names[] = {x, x};
    CettaPrimeRegularPatternElaborationV1 duplicate_environment = elaborate(
        &arena,
        (CettaPrimeRegularPatternEnvironmentV1){
            .names = (const Atom *const *)duplicate_names,
            .count = 2u,
        },
        "(FVar \"x\")", UINT64_C(10000));
    check(duplicate_environment.status ==
              CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT,
          "reject duplicate quote environment");
    Atom *reserved = atom_string(&arena, "__pk_0");
    const Atom *reserved_names[] = {reserved};
    CettaPrimeRegularPatternElaborationV1 reserved_environment = elaborate(
        &arena,
        (CettaPrimeRegularPatternEnvironmentV1){
            .names = reserved_names,
            .count = 1u,
        },
        "(FVar \"__pk_0\")", UINT64_C(10000));
    check(reserved_environment.status ==
              CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT,
          "reject future binder name in quote environment");

    CettaPrimeRegularPatternElaborationV1 exhausted = elaborate(
        &arena, empty, "(PApp \"U0\" LNil)", 0u);
    check(exhausted.status == CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
          "elaboration budget exhaustion is explicit");
    CettaPrimeRegularPatternElaborationV1 malformed_at_budget_edge = elaborate(
        &arena, empty, "(PApp \"U0\" Bogus)", 2u);
    check(malformed_at_budget_edge.status ==
              CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
          "last budget unit may establish malformed list syntax");
    CettaPrimeRegularPatternElaborationV1 exhausted_before_list = elaborate(
        &arena, empty, "(PApp \"U0\" Bogus)", 1u);
    check(exhausted_before_list.status ==
              CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
          "budget exhaustion before list inspection stays explicit");

    const char *identity_pattern =
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))";
    const char *function_type_pattern =
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (PApp \"U0\" LNil)) LNil)))";
    CettaPrimeRegularKernelBudget check_budget;
    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternCheckV1 identity_check =
        cetta_prime_regular_pattern_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), empty,
            parse_one(&arena, identity_pattern),
            parse_one(&arena, function_type_pattern), &check_budget);
    check(identity_check.phase == CETTA_PRIME_REGULAR_PATTERN_PHASE_NONE &&
          identity_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "proved intrinsic lambda is consumed by regular checker");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternCheckV1 expected_first =
        cetta_prime_regular_pattern_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), empty,
            parse_one(&arena, "(PCollection CSet LNil RNone)"),
            parse_one(
                &arena,
                "(PApp \"Id\" (LCons (PApp \"U0\" LNil) "
                " (LCons (PApp \"U0\" LNil) "
                " (LCons (PApp \"U0\" LNil) LNil))))"),
            &check_budget);
    check(expected_first.phase ==
              CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_FORMATION &&
          expected_first.judgment.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "expected formation precedes term syntax");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternCheckV1 term_syntax =
        cetta_prime_regular_pattern_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), empty,
            parse_one(&arena, "(PCollection CSet LNil RNone)"),
            parse_one(&arena, "(PApp \"U1\" LNil)"), &check_budget);
    check(term_syntax.phase == CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_SYNTAX,
          "term syntax follows prepared expected type");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternCheckV1 typing_failure =
        cetta_prime_regular_pattern_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), empty,
            parse_one(&arena, "(PApp \"U0\" LNil)"),
            parse_one(&arena, "(PApp \"U0\" LNil)"), &check_budget);
    check(typing_failure.phase == CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_TYPING &&
          typing_failure.judgment.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "term typing is the final diagnostic phase");

    check_regular_term_pattern(
        &arena, "u0", "(PApp \"U0\" LNil)",
        "lower-case universe syntax");
    check_regular_term_pattern(
        &arena, "(u 0)",
        "(PApp \"Sort\" (LCons (PApp \"LevelZero\" LNil) LNil))",
        "closed tower zero lowers through ordinary Pattern structure");
    check_regular_term_pattern(
        &arena, "(u 2)",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons 2 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "a numeral universe lowers to the constant of its number");
    check_regular_term_pattern(
        &arena, "(u 12345678901234567890123456789012345678901234567890)",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) "
        " (LCons 12345678901234567890123456789012345678901234567890 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "a numeral universe of fifty digits lowers to one constant");
    check_universe_round_trip(
        &arena, "(u 2)", "(Sort (LevelConst 2))", "(u 2)",
        "a numeral universe reads, elaborates and prints");
    /* The sorts above every written universe are written by name. */
    check_regular_term_pattern(
        &arena, "set",
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" (LCons 0 LNil)) LNil))",
        "the sort of all sets lowers to the universe at the first level "
        "above the written ones");
    check_universe_round_trip(
        &arena, "set", "(Sort (LevelAbove 0))", "set",
        "the sort of all sets reads, elaborates and prints by its name");
    check_universe_round_trip(
        &arena, "class", "(Sort (LevelAbove 1))", "class",
        "the sort of the sort of all sets prints by its name");
    check_universe_round_trip(
        &arena, "(class 0)", "(Sort (LevelAbove 0))", "set",
        "the 0-th sort above the written universes is set");
    check_universe_round_trip(
        &arena, "(class 12345678901234567890)",
        "(Sort (LevelAbove 12345678901234567890))",
        "(class 12345678901234567890)",
        "a sort above the written universes has a number of any length");
    check_regular_term_elaboration(
        &arena, "(-> set set)",
        "(Pi (Sort (LevelAbove 0)) (Sort (LevelAbove 0)))",
        "set is a type in a function type");
    check_regular_term_elaboration(
        &arena, "(lam (set : (u 0)) set)",
        "(Lam (Sort (LevelConst 0)) (idx 0))",
        "a binder named set is the binder, not the sort");
    check(lower_syntax(&arena, "(class omega)", UINT64_C(100000)).status !=
              CETTA_PRIME_REGULAR_TERM_OK &&
              lower_syntax(&arena, "(class -1)", UINT64_C(100000)).status !=
                  CETTA_PRIME_REGULAR_TERM_OK &&
              lower_syntax(&arena, "(u set)", UINT64_C(100000)).status !=
                  CETTA_PRIME_REGULAR_TERM_OK,
          "a sort above is numbered by a numeral, and is no level");
    check_universe_round_trip(
        &arena, "(u 100000)", "(Sort (LevelConst 100000))", "(u 100000)",
        "a numeral universe is one constant whatever its number");
    check_universe_round_trip(
        &arena, "(u 12345678901234567890123456789012345678901234567890)",
        "(Sort (LevelConst "
        " 12345678901234567890123456789012345678901234567890))",
        "(u 12345678901234567890123456789012345678901234567890)",
        "a numeral universe of fifty digits reads, elaborates and prints");
    Atom *fifty_digits =
        regular_term_intrinsic(
            &arena, "(u 12345678901234567890123456789012345678901234567890)");
    Atom *fifty_digits_and_one =
        regular_term_intrinsic(
            &arena, "(u 12345678901234567890123456789012345678901234567891)");
    Atom *fifty_digits_cut =
        regular_term_intrinsic(
            &arena, "(u 1234567890123456789012345678901234567890123456789)");
    check(fifty_digits && fifty_digits_and_one && fifty_digits_cut &&
          !atom_eq(fifty_digits, fifty_digits_and_one) &&
          !atom_eq(fifty_digits, fifty_digits_cut) &&
          atom_eq(
              fifty_digits,
              regular_term_intrinsic(
                  &arena,
                  "(u 0012345678901234567890123456789012345678901234567890)")),
          "numeral universes of fifty digits differ where their numerals do");
    Atom *private_level_marker =
        cetta_prime_regular_level_parameter_marker_v1(&arena, 7u);
    Atom *private_level_syntax = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"), private_level_marker)
        : NULL;
    CettaPrimeRegularKernelBudget private_level_budget;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 private_level_lowered =
        cetta_prime_regular_term_to_pattern_v1(
            &arena, private_level_syntax, &private_level_budget);
    Atom *private_level_pattern = parse_one(
        &arena,
        "(PApp \"Sort\" (LCons "
        " (PApp \"LevelParam\" (LCons 7 LNil)) LNil))");
    check(private_level_lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
              private_level_lowered.pattern && private_level_pattern &&
              atom_eq(private_level_lowered.pattern, private_level_pattern),
          "private declaration level marker lowers to a schematic level");
    CettaPrimeRegularKernelBudget nested_level_budget;
    cetta_prime_regular_kernel_budget_init(
        &nested_level_budget, true, UINT64_C(100000));
    Atom *nested_level_pattern = parse_one(
        &arena,
        "(PApp \"Pi\" (LCons (PApp \"U1\" LNil) (LCons "
        " (PLam BNone (PApp \"Pi\" (LCons "
        "  (PApp \"Sort\" (LCons "
        "   (PApp \"LevelParam\" (LCons 7 LNil)) LNil)) "
        "  (LCons (PLam BNone (Var 1)) LNil)))) LNil)))");
    CettaPrimeRegularPatternElaborationV1 nested_level_elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            &arena, empty, nested_level_pattern, &nested_level_budget);
    Atom *nested_level_intrinsic = parse_one(
        &arena, "(Pi U1 (Pi (Sort (LevelParam 7)) (idx 1)))");
    check(nested_level_elaborated.status ==
              CETTA_PRIME_REGULAR_PATTERN_OK &&
              nested_level_elaborated.term && nested_level_intrinsic &&
              atom_eq(nested_level_elaborated.term, nested_level_intrinsic),
          "binder freshness traverses nested schematic-level literals");
    CettaPrimeRegularKernelBudget declaration_constant_budget;
    cetta_prime_regular_kernel_budget_init(
        &declaration_constant_budget, true, UINT64_C(100000));
    Atom *declaration_constant = parse_one(
        &arena,
        "(DeclConst list (LevelParam 7) "
        "  (LevelMax (LevelParam 8) (LevelConst 1)))");
    CettaPrimeRegularPatternElaborationV1 declaration_elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            &arena, empty, declaration_constant,
            &declaration_constant_budget);
    check(declaration_elaborated.status ==
              CETTA_PRIME_REGULAR_PATTERN_OK &&
              declaration_elaborated.term &&
              atom_eq(
                  declaration_elaborated.term,
                  declaration_constant),
          "private declaration constants retain their name and explicit universe arguments");
    cetta_prime_regular_kernel_budget_init(
        &declaration_constant_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternElaborationV1 malformed_declaration =
        cetta_prime_regular_pattern_elaborate_v1(
            &arena, empty,
            parse_one(&arena, "(DeclConst list (LevelParam -1))"),
            &declaration_constant_budget);
    check(malformed_declaration.status ==
              CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
          "malformed private declaration levels cannot cross the Pattern boundary");
    CettaPrimeRegularTermElaborationV1 public_level_variable = lower_syntax(
        &arena, "(u $level)", UINT64_C(100000));
    check(public_level_variable.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
              public_level_variable.syntax_error ==
                  CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
          "an unscoped matcher variable cannot forge a level parameter");
    check_regular_term_pattern(
        &arena, "(lam x x)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "named lambda lowers through Pattern");
    check_regular_term_pattern(
        &arena, "(lam (x y) (x y))",
        "(PApp \"Lam\" (LCons (PLam BNone "
        " (PApp \"Lam\" (LCons (PLam BNone "
        "  (PApp \"App\" (LCons (Var 1) (LCons (Var 0) LNil)))) "
        " LNil))) LNil))",
        "multivariate lambda is nested unary sugar");
    check_regular_term_pattern(
        &arena, "(lam (x x) x)",
        "(PApp \"Lam\" (LCons (PLam BNone "
        " (PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))) LNil))",
        "duplicate binder names use lexical shadowing");
    check_regular_term_pattern(
        &arena, "(lam (_ x) x)",
        "(PApp \"Lam\" (LCons (PLam BNone "
        " (PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))) LNil))",
        "anonymous binder still shifts inner indices");
    check_regular_term_pattern(
        &arena, "(lam (_ x) (idx 1))",
        "(PApp \"Lam\" (LCons (PLam BNone "
        " (PApp \"Lam\" (LCons (PLam BNone (Var 1)) LNil))) LNil))",
        "anonymous binder remains addressable only by idx");
    check_regular_term_pattern(
        &arena, "(lam @1 *@1)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "quoted numeral is a lexical name, not index sugar");
    check_regular_term_pattern(
        &arena, "(lam @(mm-var \"ph\") *@(mm-var \"ph\"))",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "closed structural name binds lexically");
    check_regular_term_pattern(
        &arena, "(lam @(: x u0) *@(: x u0))",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "quoted structural colon is a name, not typed-binder syntax");
    check_regular_term_pattern(
        &arena, "(lam x *@x)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "bare binder name equals its explicit quote");
    CettaPrimeRegularTermElaborationV1 sealed_name = lower_syntax(
        &arena, "(lam x @x)", UINT64_C(100000));
    check(sealed_name.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
          "a quoted name is a sealed literal, not a reference to the binder");
    check_regular_term_pattern(
        &arena, "(lam @_ *@_)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "quoted underscore remains an ordinary name");
    check_regular_term_pattern(
        &arena, "(lam λ λ)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "Unicode lexical name lowers without normalization");
    check_regular_term_pattern(
        &arena, "(lam u0 u0)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "lexical binding takes precedence over reserved syntax spelling");
    check_regular_term_pattern(
        &arena, "(-> u0 u0)",
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (PApp \"U0\" LNil)) LNil)))",
        "lower-case nondependent arrow lowers to Pi");
    check_regular_term_pattern(
        &arena, "(-> u0 u0 u0)",
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone "
        "  (PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        "   (LCons (PLam BNone (PApp \"U0\" LNil)) LNil)))) LNil)))",
        "multi-arrow is right-associated Pi sugar");
    const char *binary_function_pattern =
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone "
        "  (PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        "   (LCons (PLam BNone (PApp \"U0\" LNil)) LNil)))) LNil)))";
    check_regular_term_pattern(
        &arena, "(-> ((x : u0) (y : u0)) u0)",
        binary_function_pattern,
        "per-binder typed telescope groups lower to nested Pi");
    check_regular_term_pattern(
        &arena, "(-> (x y : u0) u0)",
        binary_function_pattern,
        "shared typed telescope group lowers to nested Pi");
    check_regular_term_pattern(
        &arena, "(-> (x y : u0 u0) u0)",
        binary_function_pattern,
        "zipped typed telescope group lowers to nested Pi");
    check_regular_term_pattern(
        &arena, "(-> (: x u0) (: y u0) u0)",
        binary_function_pattern,
        "prefix unary binder ascriptions remain accepted input");
    check_regular_term_pattern(
        &arena, "(-> (x : u0) x)",
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (Var 0)) LNil)))",
        "dependent arrow body resolves its lexical binder");
    check_regular_term_pattern(
        &arena, "(-> (x : u0) (y : x) x)",
        "(PApp \"Pi\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone "
        "  (PApp \"Pi\" (LCons (Var 0) "
        "   (LCons (PLam BNone (Var 1)) LNil)))) LNil)))",
        "later telescope groups see earlier lexical binders");
    check_regular_term_pattern(
        &arena, "((lam x x) u0)",
        "(PApp \"App\" (LCons "
        " (PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil)) "
        " (LCons (PApp \"U0\" LNil) LNil)))",
        "ordinary MeTTa application lowers to regular App");

    const char *shared_dependent_type =
        "(Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (idx 1) (idx 2))))";
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (x y : A) A)", shared_dependent_type,
        "shared dependent group weakens its second domain");
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (x : A) (y : A) A)", shared_dependent_type,
        "separate groups agree with correctly expanded shared domains");
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (x y : (idx 0)) A)", shared_dependent_type,
        "explicit old-context indices receive the same group weakening");
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (x _ : A) A)", shared_dependent_type,
        "anonymous siblings still occupy a telescope position");
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (B : (u 0)) (x y : A B) A)",
        "(Pi (Sort (LevelConst 0)) (Pi (Sort (LevelConst 0)) "
        " (Pi (idx 1) (Pi (idx 1) (idx 3)))))",
        "zipped annotations share the old context before placement");
    check_regular_term_elaboration(
        &arena, "(-> (A : (u 0)) (f g : (-> (z : A) (id A z z))) A)",
        "(Pi (Sort (LevelConst 0)) "
        " (Pi (Pi (idx 0) (Id (idx 1) (idx 0) (idx 0))) "
        "  (Pi (Pi (idx 1) (Id (idx 2) (idx 0) (idx 0))) (idx 2))))",
        "group weakening moves old variables but protects nested binders");
    check_regular_term_elaboration(
        &arena, "(sigma (A : (u 0)) (x y : A) A)",
        "(Sigma (Sort (LevelConst 0)) (Sigma (idx 0) (Sigma (idx 1) (idx 2))))",
        "dependent Sigma groups use the same placement action");
    CettaPrimeRegularTermElaborationV1 sibling_dependency = lower_syntax(
        &arena, "(-> (A : (u 0)) (x y : A (idx 1)) A)", UINT64_C(100000));
    check(sibling_dependency.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
          "a group annotation cannot index its not-yet-introduced sibling");

    Atom *grouped_syntax = parse_one(&arena,
        "(-> (A : (u 0)) (f g : (-> (z : A) (id A z z))) A)");
    CettaPrimeRegularKernelBudget group_budget;
    cetta_prime_regular_kernel_budget_init(&group_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 grouped_complete =
        cetta_prime_regular_term_to_pattern_v1(&arena, grouped_syntax, &group_budget);
    uint64_t group_spent = group_budget.spent;
    check(grouped_complete.status == CETTA_PRIME_REGULAR_TERM_OK && group_spent > 0u,
          "dependent group lowering retains an actual budget receipt");
    cetta_prime_regular_kernel_budget_init(&group_budget, true, group_spent);
    CettaPrimeRegularTermElaborationV1 grouped_exact =
        cetta_prime_regular_term_to_pattern_v1(&arena, grouped_syntax, &group_budget);
    check(grouped_exact.status == CETTA_PRIME_REGULAR_TERM_OK &&
          group_budget.remaining == 0u &&
          atom_eq(grouped_exact.pattern, grouped_complete.pattern),
          "the exact recorded lowering budget reproduces the same scoped term");
    cetta_prime_regular_kernel_budget_init(&group_budget, true, group_spent - 1u);
    CettaPrimeRegularTermElaborationV1 grouped_short =
        cetta_prime_regular_term_to_pattern_v1(&arena, grouped_syntax, &group_budget);
    check(grouped_short.status == CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
          "one less lowering step is exhaustion rather than a malformed domain");

    CettaPrimeRegularTermElaborationV1 matcher_binder = lower_syntax(
        &arena, "(lam $x $x)", UINT64_C(100000));
    check(matcher_binder.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          matcher_binder.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER,
          "$ matcher variables cannot become lexical binders");
    CettaPrimeRegularTermElaborationV1 numeric_binder = lower_syntax(
        &arena, "(lam 1 1)", UINT64_C(100000));
    check(numeric_binder.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          numeric_binder.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME,
          "bare numeric literals cannot become binders");
    CettaPrimeRegularTermElaborationV1 empty_binders = lower_syntax(
        &arena, "(lam () u0)", UINT64_C(100000));
    check(empty_binders.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          empty_binders.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_EMPTY_BINDER_LIST,
          "empty multibinder is rejected");
    check_regular_term_pattern(
        &arena, "(lam (x : u0) x)",
        "(PApp \"Lam\" (LCons (PApp \"U0\" LNil) "
        " (LCons (PLam BNone (Var 0)) LNil)))",
        "typed lambda keeps its written domain");
    CettaPrimeRegularTermElaborationV1 malformed_index = lower_syntax(
        &arena, "(idx -1)", UINT64_C(100000));
    check(malformed_index.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          malformed_index.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_INVALID_INDEX,
          "negative direct index is rejected");
    CettaPrimeRegularTermElaborationV1 malformed_level = lower_syntax(
        &arena, "(u -1)", UINT64_C(100000));
    check(malformed_level.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          malformed_level.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
          "negative universe level is rejected as authored syntax");
    /* A universe costs one step and its level one step for each numeral,
     * `omega` and operation written: a budget that does not cover them is
     * exhausted, and that is not the reading of a level that had no
     * memory. */
    CettaPrimeRegularTermElaborationV1 level_budget = lower_syntax(
        &arena, "(u 2)", 1u);
    check(level_budget.status ==
              CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED &&
          !cetta_prime_regular_term_level_incomplete_v1(&level_budget),
          "reading a universe level preserves explicit budget exhaustion");
    level_budget = lower_syntax(&arena, "(u (+ omega 2))", 3u);
    check(level_budget.status ==
              CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED &&
          !cetta_prime_regular_term_level_incomplete_v1(&level_budget),
          "reading a computed universe level preserves explicit budget "
          "exhaustion");
    level_budget = lower_syntax(&arena, "(u 2)", 2u);
    check(level_budget.status == CETTA_PRIME_REGULAR_TERM_OK,
          "a numeral universe is read in the steps of its syntax");
    level_budget = lower_syntax(
        &arena, "(u 99999999999999999999999999)", 2u);
    check(level_budget.status == CETTA_PRIME_REGULAR_TERM_OK,
          "a long numeral universe is read in the same steps");
    CettaPrimeRegularTermElaborationV1 loose_index = lower_syntax(
        &arena, "(idx 0)", UINT64_C(100000));
    check(loose_index.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
          "unscoped direct index makes the syntax authority abstain");
    CettaPrimeRegularTermElaborationV1 escaping_index = lower_syntax(
        &arena, "(lam x (idx 1))", UINT64_C(100000));
    check(escaping_index.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
          "index beyond authored binders makes the syntax authority abstain");
    CettaPrimeRegularTermElaborationV1 typed_count_mismatch = lower_syntax(
        &arena, "(-> (x y : u0 u0 u0) u0)", UINT64_C(100000));
    check(typed_count_mismatch.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          typed_count_mismatch.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_BINDER_TYPE_ARITY_MISMATCH,
          "zipped binder names and types must have matching counts");
    CettaPrimeRegularTermElaborationV1 typed_matcher = lower_syntax(
        &arena, "(-> ($x : u0) u0)", UINT64_C(100000));
    check(typed_matcher.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
          typed_matcher.syntax_error ==
              CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER,
          "typed telescope also rejects matcher binders");
    CettaPrimeRegularTermElaborationV1 private_constructor = lower_syntax(
        &arena, "(Lam U0 (idx 0))", UINT64_C(100000));
    check(private_constructor.status == CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX,
          "private upper-case constructor is not public regular syntax");
    CettaPrimeRegularTermElaborationV1 regular_term_exhausted = lower_syntax(
        &arena, "(lam x x)", 0u);
    check(regular_term_exhausted.status ==
              CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
          "syntax elaboration budget exhaustion is explicit");

    CettaPrimeRegularTermElaborationV1 regular_term_identity = lower_syntax(
        &arena, "(lam x x)", UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 regular_term_function_type = lower_syntax(
        &arena, "(-> u0 u0)", UINT64_C(100000));
    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularPatternCheckV1 regular_term_identity_check =
        cetta_prime_regular_pattern_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"), empty,
            regular_term_identity.pattern, regular_term_function_type.pattern,
            &check_budget);
    check(regular_term_identity.status == CETTA_PRIME_REGULAR_TERM_OK &&
          regular_term_function_type.status == CETTA_PRIME_REGULAR_TERM_OK &&
          regular_term_identity_check.phase ==
              CETTA_PRIME_REGULAR_PATTERN_PHASE_NONE &&
          regular_term_identity_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "public named lambda reaches the proved checker end to end");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 public_identity_check =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(lam x x)"),
            parse_one(&arena, "(-> u0 u0)"), &check_budget);
    check(public_identity_check.phase ==
              CETTA_PRIME_REGULAR_TERM_PHASE_NONE &&
          public_identity_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
          atom_eq(public_identity_check.term,
                  parse_one(&arena, "(Lam (idx 0))")) &&
          atom_eq(public_identity_check.expected,
                  parse_one(&arena, "(Pi U0 U0)")),
          "syntax checker returns canonical intrinsic terms");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 public_universe_synth =
        cetta_prime_regular_term_synth_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u 0)"), &check_budget);
    Atom *quoted_universe = public_universe_synth.judgment.type
        ? cetta_prime_regular_term_quote_intrinsic_v1(
              &arena, public_universe_synth.judgment.type)
        : NULL;
    check(public_universe_synth.phase == CETTA_PRIME_REGULAR_TERM_PHASE_NONE &&
          public_universe_synth.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
          atom_eq(
              public_universe_synth.judgment.type,
              parse_one(&arena, "(Sort (LevelSucc (LevelConst 0)))")) &&
          quoted_universe && atom_eq(
              quoted_universe, parse_one(&arena, "(u 1)")),
          "closed universe synthesis quotes to round-trippable lowercase syntax");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 public_universe_check =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u 0)"),
            parse_one(&arena, "(u 1)"), &check_budget);
    check(public_universe_check.phase ==
              CETTA_PRIME_REGULAR_TERM_PHASE_NONE &&
          public_universe_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
          "quoted universe answer feeds back through type checking");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 public_expected_first =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(lam () u0)"),
            parse_one(&arena, "(id u0 u0 u1)"), &check_budget);
    check(public_expected_first.phase ==
              CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION &&
          public_expected_first.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "expected formation precedes subject syntax");

    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 public_term_syntax =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(lam () u0)"),
            parse_one(&arena, "u1"), &check_budget);
    check(public_term_syntax.phase ==
              CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX &&
          public_term_syntax.syntax.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR,
          "subject syntax follows the prepared expected type");

    /* Closed levels beyond the numerals: ordinal notations below
     * epsilon-zero in Cantor normal form.  The Pattern of one is `LevelZero`
     * or `LevelCantor` over an exponent, a coefficient and a remainder. */
    check_regular_term_pattern(
        &arena, "(u omega)",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) (LCons 1 "
        "  (LCons (PApp \"LevelZero\" LNil) LNil)))) (LCons 1 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "omega lowers to one Cantor constant whose exponent is one");
    check_universe_round_trip(
        &arena, "(u omega)",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
        "(u omega)", "omega reads, elaborates and prints");
    check_universe_round_trip(
        &arena, "(u \xcf\x89)",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
        "(u omega)", "the Greek letter is the level omega");
    check_universe_round_trip(
        &arena, "(u (+ omega 1))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1)))",
        "(u (+ omega 1))", "omega plus one");
    check_universe_round_trip(
        &arena, "(u (* omega 2))",
        "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0)))",
        "(u (* omega 2))", "omega times two");
    check_universe_round_trip(
        &arena, "(u (+ (* omega 2) 3))",
        "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 3)))",
        "(u (+ (* omega 2) 3))", "omega times two plus three");
    check_universe_round_trip(
        &arena, "(u (^ omega 2))",
        "(Sort (LevelCantor (LevelConst 2) 1 (LevelConst 0)))",
        "(u (^ omega 2))", "omega squared");
    check_universe_round_trip(
        &arena, "(u (^ omega omega))",
        "(Sort (LevelCantor "
        " (LevelCantor (LevelConst 1) 1 (LevelConst 0)) 1 (LevelConst 0)))",
        "(u (^ omega omega))", "omega to the omega");
    check_universe_round_trip(
        &arena, "(u (+ (^ omega 2) (* omega 3) 5))",
        "(Sort (LevelCantor (LevelConst 2) 1 "
        " (LevelCantor (LevelConst 1) 3 (LevelConst 5))))",
        "(u (+ (^ omega 2) (* omega 3) 5))",
        "a sum of three terms with decreasing exponents");
    check_universe_round_trip(
        &arena, "(u (+ (+ (^ omega 2) omega) 1))",
        "(Sort (LevelCantor (LevelConst 2) 1 "
        " (LevelCantor (LevelConst 1) 1 (LevelConst 1))))",
        "(u (+ (^ omega 2) omega 1))",
        "a sum nested on the left is the same notation");
    check_universe_round_trip(
        &arena, "(u (+ (^ omega 2) (+ omega 1)))",
        "(Sort (LevelCantor (LevelConst 2) 1 "
        " (LevelCantor (LevelConst 1) 1 (LevelConst 1))))",
        "(u (+ (^ omega 2) omega 1))",
        "a sum nested on the right is the same notation");
    check_universe_round_trip(
        &arena, "(u (* (^ omega (+ omega 1)) 4))",
        "(Sort (LevelCantor "
        " (LevelCantor (LevelConst 1) 1 (LevelConst 1)) 4 (LevelConst 0)))",
        "(u (* (^ omega (+ omega 1)) 4))",
        "an exponent is itself a notation");
    check_universe_round_trip(
        &arena, "(u (* (^ omega 0) 2))", "(Sort (LevelConst 2))",
        "(u 2)", "a natural number written as a power is that numeral");

    /* Among natural numbers the sum, product and power of levels are those
     * of the numbers, and the result stands where a numeral stands. */
    check_universe_round_trip(
        &arena, "(u (+ 1 2))", "(Sort (LevelConst 3))",
        "(u 3)", "a sum of numerals is that number");
    check_universe_round_trip(
        &arena, "(u (* 2 3))", "(Sort (LevelConst 6))",
        "(u 6)", "a product of numerals is that number");
    check_universe_round_trip(
        &arena, "(u (^ 2 3))", "(Sort (LevelConst 8))",
        "(u 8)", "a power of numerals is that number");
    check_universe_round_trip(
        &arena, "(u (+ 1 2 3))", "(Sort (LevelConst 6))",
        "(u 6)", "a sum of several numerals is their sum");
    check_universe_round_trip(
        &arena, "(u (+ (* 2 2) (^ 0 0)))", "(Sort (LevelConst 5))",
        "(u 5)", "arithmetic of numerals nests; zero to the zero is one");
    check_universe_round_trip(
        &arena, "(u (^ 0 3))", "(Sort (LevelConst 0))", "(u 0)",
        "zero to a positive power is zero");
    check_universe_round_trip(
        &arena, "(u (* 7 0))", "(Sort (LevelConst 0))", "(u 0)",
        "a product with zero is zero");
    check_universe_round_trip(
        &arena, "(u (+ omega (+ 1 2)))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 3)))",
        "(u (+ omega 3))", "a number computed from numerals is a last term");
    check_universe_round_trip(
        &arena, "(u (* omega (* 2 3)))",
        "(Sort (LevelCantor (LevelConst 1) 6 (LevelConst 0)))",
        "(u (* omega 6))", "a number computed from numerals is a coefficient");
    check_universe_round_trip(
        &arena, "(u (^ omega (+ 1 1)))",
        "(Sort (LevelCantor (LevelConst 2) 1 (LevelConst 0)))",
        "(u (^ omega 2))", "a number computed from numerals is an exponent");
    /* A number beyond the machine integers is the number it is: the sum,
     * product and power of numerals have any number of digits. */
    static const struct {
        const char *written;
        const char *kernel;
        const char *printed;
    } beyond_machine_integers[] = {
        {"(u (^ 2 64))", "(Sort (LevelConst 18446744073709551616))",
         "(u 18446744073709551616)"},
        {"(u (+ 9223372036854775807 1))",
         "(Sort (LevelConst 9223372036854775808))",
         "(u 9223372036854775808)"},
        {"(u (* 4294967296 4294967296))",
         "(Sort (LevelConst 18446744073709551616))",
         "(u 18446744073709551616)"},
        {"(u (^ 7 100))",
         "(Sort (LevelConst 3234476509624757991344647769100216810857203198"
         "904625400933895331391691459636928060001))",
         "(u 3234476509624757991344647769100216810857203198904625400933895"
         "331391691459636928060001)"},
        {"(u (+ omega (^ 2 64)))",
         "(Sort (LevelCantor (LevelConst 1) 1 "
         " (LevelConst 18446744073709551616)))",
         "(u (+ omega 18446744073709551616))"},
        {"(u (* omega (^ 2 64)))",
         "(Sort (LevelCantor (LevelConst 1) 18446744073709551616 "
         " (LevelConst 0)))",
         "(u (* omega 18446744073709551616))"},
        {"(u (^ omega (^ 2 64)))",
         "(Sort (LevelCantor (LevelConst 18446744073709551616) 1 "
         " (LevelConst 0)))",
         "(u (^ omega 18446744073709551616))"},
    };
    for (size_t i = 0u;
         i < sizeof beyond_machine_integers /
                 sizeof beyond_machine_integers[0];
         i++)
        check_universe_round_trip(
            &arena, beyond_machine_integers[i].written,
            beyond_machine_integers[i].kernel,
            beyond_machine_integers[i].printed,
            "a number beyond the machine integers is that number");
    Atom *two_to_sixty_four = regular_term_intrinsic(&arena, "(u (^ 2 64))");
    Atom *one_less = regular_term_intrinsic(
        &arena, "(u 18446744073709551615)");
    check(two_to_sixty_four && one_less &&
          !atom_eq(two_to_sixty_four, one_less) &&
          !atom_eq(two_to_sixty_four, regular_term_intrinsic(&arena, "(u 0)")),
          "a number beyond the machine integers is not a number near it");
    /* A power whose value no memory holds is incomplete, not refused: a
     * number above one to the power 2^100 has more than 2^100 binary
     * digits. */
    static const char *const beyond_memory[] = {
        "(u (^ 2 (^ 2 100)))", "(u (^ 3 (^ 10 30)))",
        "(u (+ omega (^ 2 (^ 2 100))))",
    };
    for (size_t i = 0u; i < sizeof beyond_memory / sizeof beyond_memory[0];
         i++)
        check_level_incomplete(
            &arena, beyond_memory[i], "level-out-of-memory",
            "a number no memory holds is incomplete, not refused");

    /* The maximum of two closed levels is the larger one. */
    check_universe_round_trip(
        &arena, "(u (max omega 1))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))",
        "(u omega)", "the maximum of omega and one is omega");
    check_universe_round_trip(
        &arena, "(u (max 2 3))", "(Sort (LevelConst 3))",
        "(u 3)", "the maximum of two numerals is the larger");
    check_universe_round_trip(
        &arena, "(u (max 3 2))", "(Sort (LevelConst 3))",
        "(u 3)", "the maximum does not depend on the order written");
    check_universe_round_trip(
        &arena,
        "(u (max 99999999999999999999999999 100000000000000000000000000))",
        "(Sort (LevelConst 100000000000000000000000000))",
        "(u 100000000000000000000000000)",
        "the maximum of two long numerals is the larger");
    check_universe_round_trip(
        &arena, "(u (max (* omega 2) (+ omega 7)))",
        "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0)))",
        "(u (* omega 2))", "the maximum compares notations, not their size");
    check_universe_round_trip(
        &arena, "(u (+ (max omega 3) 1))",
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1)))",
        "(u (+ omega 1))", "a maximum is a summand like any closed level");

    /* Beyond the natural numbers the sum, product and power are those of
     * the ordinals, and the level read is the normal form of the value:
     * 1 + omega is omega, omega + omega is omega * 2, 2 * omega is omega. */
    static const char *const omega_kernel =
        "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))";
    static const struct {
        const char *written;
        const char *kernel;
        const char *printed;
    } computed[] = {
        {"(u (+ 1 omega))", NULL, "(u omega)"},
        {"(u (+ omega 0))", NULL, "(u omega)"},
        {"(u (+ 0 omega))", NULL, "(u omega)"},
        {"(u (* 2 omega))", NULL, "(u omega)"},
        {"(u (^ 2 omega))", NULL, "(u omega)"},
        {"(u (max omega (+ 1 omega)))", NULL, "(u omega)"},
        {"(u (+ 9223372036854775807 1 omega))", NULL, "(u omega)"},
        {"(u (+ omega omega))",
         "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0)))",
         "(u (* omega 2))"},
        {"(u (* (max omega 1) 2))",
         "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0)))",
         "(u (* omega 2))"},
        {"(u (+ (* omega 2) omega))",
         "(Sort (LevelCantor (LevelConst 1) 3 (LevelConst 0)))",
         "(u (* omega 3))"},
        {"(u (* (* omega 2) 3))",
         "(Sort (LevelCantor (LevelConst 1) 6 (LevelConst 0)))",
         "(u (* omega 6))"},
        {"(u (+ omega 1 2))",
         "(Sort (LevelCantor (LevelConst 1) 1 (LevelConst 3)))",
         "(u (+ omega 3))"},
        {"(u (* (+ omega 1) 2))",
         "(Sort (LevelCantor (LevelConst 1) 2 (LevelConst 1)))",
         "(u (+ (* omega 2) 1))"},
        {"(u (* omega 0))", "(Sort (LevelConst 0))", "(u 0)"},
        {"(u (* omega omega))",
         "(Sort (LevelCantor (LevelConst 2) 1 (LevelConst 0)))",
         "(u (^ omega 2))"},
        {"(u (+ omega (^ omega 2)))",
         "(Sort (LevelCantor (LevelConst 2) 1 (LevelConst 0)))",
         "(u (^ omega 2))"},
        {"(u (^ (^ omega 2) 2))",
         "(Sort (LevelCantor (LevelConst 4) 1 (LevelConst 0)))",
         "(u (^ omega 4))"},
        {"(u (^ 2 (+ (* omega 2) 3)))",
         "(Sort (LevelCantor (LevelConst 2) 8 (LevelConst 0)))",
         "(u (* (^ omega 2) 8))"},
        {"(u (+ (^ omega 2) (+ 1 omega)))",
         "(Sort (LevelCantor (LevelConst 2) 1 "
         "(LevelCantor (LevelConst 1) 1 (LevelConst 0))))",
         "(u (+ (^ omega 2) omega))"},
        {"(u (^ (+ omega 1) 2))",
         "(Sort (LevelCantor (LevelConst 2) 1 "
         "(LevelCantor (LevelConst 1) 1 (LevelConst 1))))",
         "(u (+ (^ omega 2) omega 1))"},
        {"(u (^ omega (+ 1 omega)))",
         "(Sort (LevelCantor "
         "(LevelCantor (LevelConst 1) 1 (LevelConst 0)) 1 (LevelConst 0)))",
         "(u (^ omega omega))"},
        {"(u (^ (+ omega 1) omega))",
         "(Sort (LevelCantor "
         "(LevelCantor (LevelConst 1) 1 (LevelConst 0)) 1 (LevelConst 0)))",
         "(u (^ omega omega))"},
        {"(u (^ omega 5000))",
         "(Sort (LevelCantor (LevelConst 5000) 1 (LevelConst 0)))",
         "(u (^ omega 5000))"},
    };
    for (size_t i = 0u; i < sizeof computed / sizeof computed[0]; i++)
        check_universe_round_trip(
            &arena, computed[i].written,
            computed[i].kernel ? computed[i].kernel : omega_kernel,
            computed[i].printed,
            "a closed level is read as the normal form of its value");
    /* The order of summands and factors matters: omega + 1 is above
     * 1 + omega, and omega * 2 is above 2 * omega. */
    Atom *one_plus_omega = regular_term_intrinsic(&arena, "(u (+ 1 omega))");
    Atom *omega_plus_one = regular_term_intrinsic(&arena, "(u (+ omega 1))");
    Atom *two_times_omega = regular_term_intrinsic(&arena, "(u (* 2 omega))");
    Atom *omega_times_two = regular_term_intrinsic(&arena, "(u (* omega 2))");
    check(one_plus_omega && omega_plus_one && two_times_omega &&
          omega_times_two && !atom_eq(one_plus_omega, omega_plus_one) &&
          !atom_eq(two_times_omega, omega_times_two) &&
          atom_eq(one_plus_omega, two_times_omega),
          "the sum and the product of levels are not commutative");
    /* A notation has any number of terms: the power of omega + 1 to n has
     * n + 1 of them.  The universe at one with 5001 terms reads, elaborates
     * and prints like any other, and what is printed reads back. */
    Atom *long_power = regular_term_intrinsic_within(
        &arena, "(u (^ (+ omega 1) 5000))", UINT64_C(10000000));
    Atom *long_power_printed = long_power
        ? cetta_prime_regular_term_quote_intrinsic_v1(&arena, long_power)
        : NULL;
    Atom *long_power_reread = long_power_printed
        ? regular_atom_intrinsic_within(
              &arena, long_power_printed, UINT64_C(10000000))
        : NULL;
    check(long_power &&
          cetta_prime_regular_kernel_term_is_universe_sort_v1(long_power) &&
          long_power_printed && long_power_printed->kind == ATOM_EXPR &&
          long_power_printed->expr.len == 2u &&
          long_power_printed->expr.elems[1]->kind == ATOM_EXPR &&
          long_power_printed->expr.elems[1]->expr.len == 5002u &&
          atom_is_symbol(
              long_power_printed->expr.elems[1]->expr.elems[0], "+") &&
          long_power_reread && atom_eq(long_power_reread, long_power),
          "a level of 5001 terms reads, elaborates, prints and reads back");
    Atom *long_power_by_parts = regular_term_intrinsic_within(
        &arena, "(u (^ (^ (+ omega 1) 50) 100))", UINT64_C(10000000));
    Atom *shorter_power = regular_term_intrinsic_within(
        &arena, "(u (^ (+ omega 1) 4999))", UINT64_C(10000000));
    check(long_power && long_power_by_parts && shorter_power &&
          atom_eq(long_power, long_power_by_parts) &&
          !atom_eq(long_power, shorter_power) &&
          !atom_eq(
              long_power,
              regular_term_intrinsic(&arena, "(u (^ omega 5000))")),
          "a power of a power is the power to the product, and a shorter "
          "power is another level");
    CettaPrimeRegularKernelBudget long_power_budget;
    cetta_prime_regular_kernel_budget_init(&long_power_budget, false, 0u);
    CettaPrimeRegularTermCheckV1 shorter_in_longer =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u (^ (+ omega 1) 4999))"),
            parse_one(&arena, "(u (^ (+ omega 1) 5000))"),
            &long_power_budget);
    CettaPrimeRegularTermCheckV1 longer_in_shorter =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u (^ (+ omega 1) 5000))"),
            parse_one(&arena, "(u (^ (+ omega 1) 4999))"),
            &long_power_budget);
    check(shorter_in_longer.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
          longer_in_shorter.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "universes at levels of thousands of terms are ordered as the "
          "levels are");
    /* A power with more terms than memory addresses is incomplete, not
     * refused. */
    static const char *const beyond_terms[] = {
        "(u (^ (+ omega 1) 18446744073709551615))",
        "(u (^ (+ omega 1) 99999999999999999999999999))",
        "(u (* 2 (^ (+ omega 1) 99999999999999999999999999)))",
    };
    for (size_t i = 0u; i < sizeof beyond_terms / sizeof beyond_terms[0]; i++)
        check_level_incomplete(
            &arena, beyond_terms[i], "level-out-of-memory",
            "a level with more terms than memory holds is incomplete");
    /* What is no level at all is an invalid level, also inside a sum,
     * product or power. */
    static const char *const not_a_level[] = {
        "(u (+ omega banana))", "(u (^ omega))", "(u (max omega banana))",
        "(u (+ omega -1))", "(u (^ omega 1 2))", "(u banana)", "(u (+ omega))",
        "(u (+ 1 banana))", "(u (* 2 3 4))", "(u (max omega))",
        "(u (max 1 2 3))", "(u (min omega 1))",
    };
    for (size_t i = 0u; i < sizeof not_a_level / sizeof not_a_level[0]; i++) {
        CettaPrimeRegularTermElaborationV1 refused = lower_syntax(
            &arena, not_a_level[i], UINT64_C(100000));
        check(refused.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
              refused.syntax_error == CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
              "what is no level notation is an invalid level");
    }

    /* A numeral has any number of digits, as a level, a coefficient, an
     * exponent and a last term. */
    static const struct {
        const char *written;
        const char *kernel;
        const char *printed;
    } long_numerals[] = {
        {"(u 99999999999999999999999999)",
         "(Sort (LevelConst 99999999999999999999999999))",
         "(u 99999999999999999999999999)"},
        {"(u (* omega 99999999999999999999999999))",
         "(Sort (LevelCantor (LevelConst 1) 99999999999999999999999999 "
         " (LevelConst 0)))",
         "(u (* omega 99999999999999999999999999))"},
        {"(u (+ (^ omega 99999999999999999999999999) 1))",
         "(Sort (LevelCantor (LevelConst 99999999999999999999999999) 1 "
         " (LevelConst 1)))",
         "(u (+ (^ omega 99999999999999999999999999) 1))"},
        {"(u (+ omega 99999999999999999999999999))",
         "(Sort (LevelCantor (LevelConst 1) 1 "
         " (LevelConst 99999999999999999999999999)))",
         "(u (+ omega 99999999999999999999999999))"},
        {"(u (+ 1 (* omega 99999999999999999999999999)))",
         "(Sort (LevelCantor (LevelConst 1) 99999999999999999999999999 "
         " (LevelConst 0)))",
         "(u (* omega 99999999999999999999999999))"},
        {"(u (+ 99999999999999999999999999 1))",
         "(Sort (LevelConst 100000000000000000000000000))",
         "(u 100000000000000000000000000)"},
    };
    for (size_t i = 0u; i < sizeof long_numerals / sizeof long_numerals[0];
         i++)
        check_universe_round_trip(
            &arena, long_numerals[i].written, long_numerals[i].kernel,
            long_numerals[i].printed,
            "a numeral of any number of digits is the number it writes");
    check(!atom_eq(
              regular_term_intrinsic(
                  &arena, "(u 99999999999999999999999999)"),
              regular_term_intrinsic(
                  &arena, "(u 99999999999999999999999998)")) &&
          !atom_eq(
              regular_term_intrinsic(
                  &arena, "(u (* omega 99999999999999999999999999))"),
              regular_term_intrinsic(
                  &arena, "(u (* omega 9999999999999999999999999))")),
          "long numerals that differ write different levels");
    /* A tower omega^omega^...^omega of any height is a level, and a taller
     * tower is a larger one. */
    static const unsigned tower_heights[] = {31u, 32u, 33u, 400u};
    const CettaPrimeLevelNotationV1 *lower_tower = NULL;
    for (size_t i = 0u; i < sizeof tower_heights / sizeof tower_heights[0];
         i++) {
        Atom *tower = atom_symbol(&arena, "omega");
        for (unsigned level = 1u; level < tower_heights[i]; level++)
            tower = atom_expr3(
                &arena, atom_symbol(&arena, "^"),
                atom_symbol(&arena, "omega"), tower);
        Atom *universe = atom_expr2(&arena, atom_symbol(&arena, "u"), tower);
        CettaPrimeRegularKernelBudget tower_budget;
        cetta_prime_regular_kernel_budget_init(
            &tower_budget, true, UINT64_C(100000));
        const CettaPrimeLevelNotationV1 *notation = NULL;
        CettaPrimeRegularTermElaborationV1 read =
            cetta_prime_regular_term_closed_level_v1(
                &arena, tower, &tower_budget, &notation);
        int order = 0;
        check(read.status == CETTA_PRIME_REGULAR_TERM_OK && notation &&
              cetta_prime_level_notation_compare_v1(
                  lower_tower, notation, &order) ==
                  CETTA_PRIME_LEVEL_OK_V1 &&
              order < 0,
              "a tower of omegas of any height is a level above the "
              "shorter ones");
        lower_tower = notation;
        Atom *kernel = regular_atom_intrinsic_within(
            &arena, universe, UINT64_C(100000));
        Atom *printed = kernel
            ? cetta_prime_regular_term_quote_intrinsic_v1(&arena, kernel)
            : NULL;
        check(kernel && printed && atom_eq(printed, universe) &&
              cetta_prime_regular_kernel_term_is_universe_sort_v1(kernel),
              "the universe at a tower of omegas elaborates and prints as "
              "written");
    }
    /* No author writes a level above the Cantor normal forms: inside
     * `(u ...)` the spellings of the wire are no level syntax. */
    static const char *const no_source_spelling[] = {
        "(u (LevelAbove 0))", "(u (above 0))", "(u LevelAbove)",
        "(u (+ omega (LevelAbove 0)))", "(u (max omega (LevelAbove 1)))",
        "(u (LevelConst 3))", "(u epsilon)", "(u epsilon0)",
    };
    for (size_t i = 0u;
         i < sizeof no_source_spelling / sizeof no_source_spelling[0]; i++) {
        CettaPrimeRegularTermElaborationV1 refused = lower_syntax(
            &arena, no_source_spelling[i], UINT64_C(100000));
        check(refused.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
              refused.syntax_error == CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
              "a level above the Cantor normal forms has no source spelling");
    }

    /* Over a level parameter, `(+ level k)` is the level k successors up. */
    Atom *offset_level_syntax = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"),
              atom_expr3(
                  &arena, atom_symbol(&arena, "+"),
                  atom_expr3(
                      &arena, atom_symbol(&arena, "+"), private_level_marker,
                      atom_int(&arena, 1)),
                  atom_int(&arena, 1)))
        : NULL;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 offset_level_lowered =
        cetta_prime_regular_term_to_pattern_v1(
            &arena, offset_level_syntax, &private_level_budget);
    Atom *offset_level_pattern = parse_one(
        &arena,
        "(PApp \"Sort\" (LCons (PApp \"LevelSucc\" (LCons "
        " (PApp \"LevelSucc\" (LCons "
        "  (PApp \"LevelParam\" (LCons 7 LNil)) LNil)) LNil)) LNil))");
    check(offset_level_lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
              offset_level_lowered.pattern && offset_level_pattern &&
              atom_eq(offset_level_lowered.pattern, offset_level_pattern),
          "an offset over a level parameter lowers to successors");
    Atom *omega_offset_syntax = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"),
              atom_expr3(
                  &arena, atom_symbol(&arena, "+"), private_level_marker,
                  atom_symbol(&arena, "omega")))
        : NULL;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 omega_offset_lowered =
        cetta_prime_regular_term_to_pattern_v1(
            &arena, omega_offset_syntax, &private_level_budget);
    check(omega_offset_lowered.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
              omega_offset_lowered.syntax_error ==
                  CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
          "only a numeral of successors applies to a level parameter");
    /* Over a parameter the level k successors up is one counted form: the
     * level itself for no successor, `LevelSucc` for one, and `LevelOffset`
     * with the numeral for more, whatever the length of the numeral.  The
     * lowering takes the same few steps for every count. */
    static const struct {
        const char *count;
        const char *pattern;
    } counted_offsets[] = {
        {"0", "(PApp \"Sort\" (LCons (PApp \"LevelParam\" (LCons 7 LNil)) "
              " LNil))"},
        {"1", "(PApp \"Sort\" (LCons (PApp \"LevelSucc\" (LCons "
              " (PApp \"LevelParam\" (LCons 7 LNil)) LNil)) LNil))"},
        {"2", "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
              " (PApp \"LevelParam\" (LCons 7 LNil)) (LCons 2 LNil))) "
              " LNil))"},
        {"20000", "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
                  " (PApp \"LevelParam\" (LCons 7 LNil)) "
                  " (LCons 20000 LNil))) LNil))"},
        {"99999999999999999999999999",
         "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
         " (PApp \"LevelParam\" (LCons 7 LNil)) "
         " (LCons 99999999999999999999999999 LNil))) LNil))"},
    };
    uint64_t uncounted_steps = 0u;
    uint64_t counted_offset_steps = 0u;
    for (size_t i = 0u;
         i < sizeof counted_offsets / sizeof counted_offsets[0]; i++) {
        Atom *counted_syntax = private_level_marker
            ? atom_expr2(
                  &arena, atom_symbol(&arena, "u"),
                  atom_expr3(
                      &arena, atom_symbol(&arena, "+"), private_level_marker,
                      parse_one(&arena, counted_offsets[i].count)))
            : NULL;
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularTermElaborationV1 counted_lowered =
            cetta_prime_regular_term_to_pattern_v1(
                &arena, counted_syntax, &private_level_budget);
        Atom *counted_pattern = parse_one(&arena, counted_offsets[i].pattern);
        check(counted_lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
                  counted_lowered.pattern && counted_pattern &&
                  atom_eq(counted_lowered.pattern, counted_pattern),
              "a count of successors over a level parameter lowers to one "
              "counted form");
        /* A count of one or more takes one step more than no count, and no
         * more for being long. */
        if (i == 0u) uncounted_steps = private_level_budget.spent;
        if (i == 1u) counted_offset_steps = private_level_budget.spent;
        check(i == 0u ||
                  (counted_offset_steps == uncounted_steps + 1u &&
                   private_level_budget.spent == counted_offset_steps),
              "a count of successors takes one step, however long it is");
    }
    static const char *const refused_counts[] = {
        "-1", "-99999999999999999999999999", "banana", "(+ 1 1)", "1.5",
    };
    for (size_t i = 0u;
         i < sizeof refused_counts / sizeof refused_counts[0]; i++) {
        Atom *refused_syntax = private_level_marker
            ? atom_expr2(
                  &arena, atom_symbol(&arena, "u"),
                  atom_expr3(
                      &arena, atom_symbol(&arena, "+"), private_level_marker,
                      parse_one(&arena, refused_counts[i])))
            : NULL;
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularTermElaborationV1 refused_count =
            cetta_prime_regular_term_to_pattern_v1(
                &arena, refused_syntax, &private_level_budget);
        check(refused_count.status == CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
                  refused_count.syntax_error ==
                      CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL &&
                  refused_count.pattern == NULL,
              "a count of successors is a numeral that is not negative");
    }

    /* Over a level parameter, `(max a b)` is the maximum of the two levels,
     * with the parameter on either side and a closed level on the other. */
    static const struct {
        const char *closed;
        bool parameter_first;
        unsigned successors;
        const char *pattern;
    } open_maxima[] = {
        {"1", true, 0u,
         "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
         " (PApp \"LevelParam\" (LCons 7 LNil)) (LCons "
         " (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
         "  (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) "
         " LNil))) LNil))"},
        {"99999999999999999999999999", true, 0u,
         "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
         " (PApp \"LevelParam\" (LCons 7 LNil)) (LCons "
         " (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
         "  (LCons 99999999999999999999999999 "
         "  (LCons (PApp \"LevelZero\" LNil) LNil)))) "
         " LNil))) LNil))"},
        {"omega", false, 0u,
         "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
         " (PApp \"LevelCantor\" (LCons "
         "  (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
         "   (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) "
         "  (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) (LCons "
         " (PApp \"LevelParam\" (LCons 7 LNil)) LNil))) LNil))"},
        {"(+ 1 omega)", false, 0u,
         "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
         " (PApp \"LevelCantor\" (LCons "
         "  (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
         "   (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) "
         "  (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) (LCons "
         " (PApp \"LevelParam\" (LCons 7 LNil)) LNil))) LNil))"},
        {"(max 0 (+ 0 0))", true, 1u,
         "(PApp \"Sort\" (LCons (PApp \"LevelSucc\" (LCons "
         " (PApp \"LevelMax\" (LCons "
         "  (PApp \"LevelParam\" (LCons 7 LNil)) (LCons "
         "  (PApp \"LevelZero\" LNil) LNil))) LNil)) LNil))"},
    };
    for (size_t i = 0u; i < sizeof open_maxima / sizeof open_maxima[0]; i++) {
        Atom *closed = parse_one(&arena, open_maxima[i].closed);
        Atom *level = private_level_marker && closed
            ? atom_expr3(
                  &arena, atom_symbol(&arena, "max"),
                  open_maxima[i].parameter_first
                      ? private_level_marker : closed,
                  open_maxima[i].parameter_first
                      ? closed : private_level_marker)
            : NULL;
        if (level && open_maxima[i].successors != 0u)
            level = atom_expr3(
                &arena, atom_symbol(&arena, "+"), level,
                atom_int(&arena, (int64_t)open_maxima[i].successors));
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularTermElaborationV1 lowered =
            cetta_prime_regular_term_to_pattern_v1(
                &arena,
                level ? atom_expr2(&arena, atom_symbol(&arena, "u"), level)
                      : NULL,
                &private_level_budget);
        Atom *expected = parse_one(&arena, open_maxima[i].pattern);
        check(lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
                  lowered.pattern && expected &&
                  atom_eq(lowered.pattern, expected),
              "a maximum over a level parameter lowers to the level maximum");
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularPatternElaborationV1 elaborated =
            cetta_prime_regular_pattern_elaborate_v1(
                &arena, (CettaPrimeRegularPatternEnvironmentV1){0},
                lowered.pattern, &private_level_budget);
        check(elaborated.status == CETTA_PRIME_REGULAR_PATTERN_OK &&
                  elaborated.term &&
                  cetta_prime_regular_kernel_term_maybe_syntax(
                      elaborated.term),
              "the level maximum of the wire is a level of the kernel");
    }
    /* A closed side with a number beyond the machine integers is the level
     * it writes. */
    Atom *open_maximum_long = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"),
              atom_expr3(
                  &arena, atom_symbol(&arena, "max"), private_level_marker,
                  parse_one(&arena, "(+ omega (^ 2 64))")))
        : NULL;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermElaborationV1 open_maximum_lowered =
        cetta_prime_regular_term_to_pattern_v1(
            &arena, open_maximum_long, &private_level_budget);
    Atom *open_maximum_long_pattern = parse_one(
        &arena,
        "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
        " (PApp \"LevelParam\" (LCons 7 LNil)) (LCons "
        " (PApp \"LevelCantor\" (LCons "
        "  (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
        "   (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) "
        "  (LCons 1 (LCons "
        "  (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
        "   (LCons 18446744073709551616 "
        "   (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil)))) "
        " LNil))) LNil))");
    check(open_maximum_lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
              open_maximum_lowered.pattern && open_maximum_long_pattern &&
              atom_eq(open_maximum_lowered.pattern, open_maximum_long_pattern),
          "a closed side beyond the machine integers is a side of a "
          "maximum like any other");
    /* A closed side no memory holds leaves the maximum incomplete. */
    Atom *open_maximum_incomplete = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"),
              atom_expr3(
                  &arena, atom_symbol(&arena, "max"), private_level_marker,
                  parse_one(&arena, "(+ omega (^ 2 (^ 2 100)))")))
        : NULL;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    open_maximum_lowered = cetta_prime_regular_term_to_pattern_v1(
        &arena, open_maximum_incomplete, &private_level_budget);
    check(open_maximum_lowered.status ==
              CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED &&
              cetta_prime_regular_term_level_incomplete_v1(
                  &open_maximum_lowered),
          "a closed side no memory holds leaves a maximum incomplete");
    Atom *open_maximum_invalid = private_level_marker
        ? atom_expr2(
              &arena, atom_symbol(&arena, "u"),
              atom_expr3(
                  &arena, atom_symbol(&arena, "max"), private_level_marker,
                  atom_symbol(&arena, "banana")))
        : NULL;
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(100000));
    open_maximum_lowered = cetta_prime_regular_term_to_pattern_v1(
        &arena, open_maximum_invalid, &private_level_budget);
    check(open_maximum_lowered.status ==
              CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR &&
              open_maximum_lowered.syntax_error ==
                  CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL,
          "what is no level is refused on either side of a maximum");
    static const char *const malformed_maximum[] = {
        "(PApp \"LevelMax\" (LCons (PApp \"LevelZero\" LNil) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelMax\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons (PApp \"U1\" LNil) LNil))) "
        " LNil))",
    };
    for (size_t i = 0u;
         i < sizeof malformed_maximum / sizeof malformed_maximum[0]; i++) {
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularPatternElaborationV1 refused =
            cetta_prime_regular_pattern_elaborate_v1(
                &arena, (CettaPrimeRegularPatternEnvironmentV1){0},
                parse_one(&arena, malformed_maximum[i]),
                &private_level_budget);
        check(refused.status != CETTA_PRIME_REGULAR_PATTERN_OK &&
                  refused.term == NULL,
              "a level maximum of the wire takes two levels");
    }

    /* The counted successor of the wire, `LevelOffset` over a level and a
     * numeral of any length, is the kernel's `(LevelOffset level count)`. */
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelParam\" (LCons 7 LNil)) "
        " (LCons 99999999999999999999999999 LNil))) LNil))",
        "(Sort (LevelOffset (LevelParam 7) 99999999999999999999999999))",
        "a counted successor of the wire has a numeral of any length");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelMax\" (LCons (PApp \"LevelParam\" (LCons 7 LNil)) "
        "  (LCons (PApp \"LevelZero\" LNil) LNil))) "
        " (LCons 20000 LNil))) LNil))",
        "(Sort (LevelOffset (LevelMax (LevelParam 7) (LevelConst 0)) 20000))",
        "a counted successor of the wire stands over any level");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelSucc\" (LCons "
        " (PApp \"LevelOffset\" (LCons (PApp \"LevelZero\" LNil) "
        "  (LCons 0 LNil))) LNil)) LNil))",
        "(Sort (LevelSucc (LevelOffset (LevelConst 0) 0)))",
        "the one-step successor of the wire stands over a counted one");
    check_elaboration(
        &arena, empty,
        "(DeclConst list (LevelOffset (LevelParam 7) "
        " 99999999999999999999999999) (LevelSucc (LevelParam 7)))",
        "(DeclConst list (LevelOffset (LevelParam 7) "
        " 99999999999999999999999999) (LevelSucc (LevelParam 7)))",
        "a declaration occurrence carries counted successors");
    static const char *const malformed_counted[] = {
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelZero\" LNil) LNil)) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons banana LNil))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons -1 LNil))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelZero\" LNil) "
        " (LCons (PApp \"LevelZero\" LNil) LNil))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons 3 "
        " (LCons 1 LNil))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelOffset\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons 1 (LCons 1 LNil)))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelOffset\" (LCons (PApp \"LevelZero\" LNil) "
        "  (LCons 1 LNil))) (LCons 1 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "(DeclConst list (LevelOffset (LevelParam 7) banana))",
        "(DeclConst list (LevelOffset (LevelParam 7) -1))",
        "(DeclConst list (LevelOffset (LevelParam 7)))",
        "(DeclConst list (LevelOffset banana 1))",
    };
    for (size_t i = 0u;
         i < sizeof malformed_counted / sizeof malformed_counted[0]; i++) {
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularPatternElaborationV1 refused =
            cetta_prime_regular_pattern_elaborate_v1(
                &arena, (CettaPrimeRegularPatternEnvironmentV1){0},
                parse_one(&arena, malformed_counted[i]),
                &private_level_budget);
        check((refused.status == CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR ||
               refused.status == CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE) &&
                  refused.term == NULL,
              "a counted successor of the wire takes a level and a numeral "
              "that is not negative");
    }

    /* The level constants of the wire carry numerals of any length, and the
     * wire has the levels above the Cantor normal forms as `LevelAbove` over
     * a numeral: closed levels of the kernel like the others. */
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons 99999999999999999999999999 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "(Sort (LevelConst 99999999999999999999999999))",
        "a natural constant of the wire has a numeral of any length");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) (LCons 1 "
        "  (LCons (PApp \"LevelZero\" LNil) LNil)))) "
        " (LCons 99999999999999999999999999 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "(Sort (LevelCantor (LevelConst 1) 99999999999999999999999999 "
        " (LevelConst 0)))",
        "a coefficient of the wire has a numeral of any length");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" (LCons 0 LNil)) LNil))",
        "(Sort (LevelAbove 0))",
        "the level of the sort of all sets is spelled in the wire");
    check_elaboration(
        &arena, empty,
        "(PApp \"Sort\" (LCons (PApp \"LevelSucc\" (LCons "
        " (PApp \"LevelAbove\" (LCons 99999999999999999999999999 LNil)) "
        " LNil)) LNil))",
        "(Sort (LevelSucc (LevelAbove 99999999999999999999999999)))",
        "a level above the Cantor normal forms stands under a successor");
    check_elaboration(
        &arena, empty,
        "(DeclConst list (LevelAbove 1) "
        " (LevelCantor (LevelConst 1) 99999999999999999999999999 "
        "  (LevelConst 0)) (LevelConst 99999999999999999999999999))",
        "(DeclConst list (LevelAbove 1) "
        " (LevelCantor (LevelConst 1) 99999999999999999999999999 "
        "  (LevelConst 0)) (LevelConst 99999999999999999999999999))",
        "a declaration occurrence carries level constants of every "
        "spelling");
    static const char *const malformed_level_constants[] = {
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" LNil) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" "
        " (LCons banana LNil)) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" "
        " (LCons -1 LNil)) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelAbove\" "
        " (LCons 0 (LCons 1 LNil))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons 0 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons -99999999999999999999999999 "
        " (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil))",
        "(PApp \"Sort\" (LCons (PApp \"LevelCantor\" (LCons "
        " (PApp \"LevelZero\" LNil) (LCons 3 "
        " (LCons (PApp \"LevelCantor\" (LCons (PApp \"LevelZero\" LNil) "
        "  (LCons 1 (LCons (PApp \"LevelZero\" LNil) LNil)))) LNil)))) "
        " LNil))",
        "(DeclConst list (LevelAbove banana))",
        "(DeclConst list (LevelAbove))",
        "(DeclConst list (LevelConst -99999999999999999999999999))",
        "(DeclConst list (LevelCantor (LevelConst 1) banana (LevelConst 0)))",
        "(DeclConst list (LevelCantor (LevelParam 1) 1 (LevelConst 0)))",
    };
    for (size_t i = 0u;
         i < sizeof malformed_level_constants /
                 sizeof malformed_level_constants[0];
         i++) {
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, UINT64_C(100000));
        CettaPrimeRegularPatternElaborationV1 refused =
            cetta_prime_regular_pattern_elaborate_v1(
                &arena, (CettaPrimeRegularPatternEnvironmentV1){0},
                parse_one(&arena, malformed_level_constants[i]),
                &private_level_budget);
        check((refused.status == CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR ||
               refused.status == CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE) &&
                  refused.term == NULL,
              "a level constant of the wire that is not one is refused");
    }
    /* A level constant of the wire of five thousand terms elaborates within
     * a budget, and a budget it exceeds is exhausted. */
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(10000000));
    CettaPrimeRegularTermElaborationV1 long_wire_lowered =
        cetta_prime_regular_term_to_pattern_v1(
            &arena, parse_one(&arena, "(u (^ (+ omega 1) 5000))"),
            &private_level_budget);
    cetta_prime_regular_kernel_budget_init(
        &private_level_budget, true, UINT64_C(1000));
    CettaPrimeRegularPatternElaborationV1 long_wire_exhausted =
        cetta_prime_regular_pattern_elaborate_v1(
            &arena, (CettaPrimeRegularPatternEnvironmentV1){0},
            long_wire_lowered.pattern, &private_level_budget);
    check(long_wire_lowered.status == CETTA_PRIME_REGULAR_TERM_OK &&
              long_wire_exhausted.status ==
                  CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED &&
              long_wire_exhausted.term == NULL,
          "a long level constant of the wire exhausts a small budget");

    /* The arithmetic of a level counts against the budget of the lowering.
     * A level whose value takes more steps than the budget has is read
     * incompletely for lack of budget: it is a level, it is not refused, and
     * the budget is spent.  Within a budget its value takes the level is
     * read. */
    static const struct {
        const char *level;
        uint64_t steps;
        bool complete;
    } budgeted_levels[] = {
        {"(u (^ (+ omega 1) 1000000000))", UINT64_C(100000), false},
        {"(u (^ (+ omega 1) 5000))", UINT64_C(20000), false},
        {"(u (^ 2 (^ 2 40)))", UINT64_C(1000000), false},
        {"(u (* (^ 7 100000) (^ 7 100000)))", UINT64_C(1000), false},
        {"(u (+ (^ (+ omega 1) 1000000000) 1))", UINT64_C(100000), false},
        {"(u (^ (+ omega 1) 5000))", UINT64_C(10000000), true},
        {"(u (^ (+ omega 1) 50))", UINT64_C(100000), true},
        {"(u (^ 2 (^ 2 7)))", UINT64_C(100000), true},
        {"(u (+ omega 1))", UINT64_C(100), true},
    };
    for (size_t i = 0u;
         i < sizeof budgeted_levels / sizeof budgeted_levels[0]; i++) {
        cetta_prime_regular_kernel_budget_init(
            &private_level_budget, true, budgeted_levels[i].steps);
        CettaPrimeRegularTermElaborationV1 budgeted =
            cetta_prime_regular_term_to_pattern_v1(
                &arena, parse_one(&arena, budgeted_levels[i].level),
                &private_level_budget);
        if (budgeted_levels[i].complete)
            check(budgeted.status == CETTA_PRIME_REGULAR_TERM_OK &&
                      budgeted.pattern != NULL &&
                      private_level_budget.remaining != 0u &&
                      private_level_budget.spent ==
                          budgeted_levels[i].steps -
                              private_level_budget.remaining,
                  "a level is read within a budget its arithmetic takes");
        else
            check(budgeted.status ==
                      CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED &&
                      budgeted.pattern == NULL &&
                      !cetta_prime_regular_term_level_incomplete_v1(
                          &budgeted) &&
                      budgeted.reason &&
                      strcmp(budgeted.reason,
                             "universe-level-elaboration-budget") == 0 &&
                      private_level_budget.remaining == 0u &&
                      private_level_budget.spent == budgeted_levels[i].steps,
                  "a level whose arithmetic exceeds the budget is read "
                  "incompletely, for lack of budget");
    }

    /* The universe at omega is a term of the next one, in the notation an
     * author writes, and that answer feeds back through checking. */
    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 omega_universe_synth =
        cetta_prime_regular_term_synth_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u omega)"), &check_budget);
    Atom *quoted_omega_universe = omega_universe_synth.judgment.type
        ? cetta_prime_regular_term_quote_intrinsic_v1(
              &arena, omega_universe_synth.judgment.type)
        : NULL;
    check(omega_universe_synth.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
          quoted_omega_universe && atom_eq(
              quoted_omega_universe, parse_one(&arena, "(u (+ omega 1))")),
          "the universe at omega synthesizes its successor universe");
    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 omega_universe_check =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u omega)"), quoted_omega_universe,
            &check_budget);
    cetta_prime_regular_kernel_budget_init(
        &check_budget, true, UINT64_C(100000));
    CettaPrimeRegularTermCheckV1 omega_universe_self_check =
        cetta_prime_regular_term_elaborate_and_check_v1(
            &arena, atom_symbol(&arena, "PrimeCtxNil"),
            parse_one(&arena, "(u omega)"), parse_one(&arena, "(u omega)"),
            &check_budget);
    check(omega_universe_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
          omega_universe_self_check.judgment.status ==
              CETTA_PRIME_REGULAR_KERNEL_REFUTED,
          "the universe at omega is in its successor and not in itself");

    printf("(PrimeRegularPatternSummary checks=%zu failures=%zu)\n",
           checks, failures);

    parser_set_universal_name_syntax_enabled(universal_names_old);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    arena_free(&arena);
    return failures == 0u ? 0 : 1;
}
