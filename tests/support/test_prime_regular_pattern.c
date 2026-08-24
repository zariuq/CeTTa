#include "parser.h"
#include "prime_regular_pattern.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

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
        "(PApp \"Sort\" (LCons "
        " (PApp \"LevelSucc\" (LCons "
        "  (PApp \"LevelSucc\" (LCons "
        "   (PApp \"LevelZero\" LNil) LNil)) LNil)) LNil))",
        "numeric universe sugar lowers to structural successors");
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
        &arena, "(lam @1 @1)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "quoted numeral is a lexical name, not index sugar");
    check_regular_term_pattern(
        &arena, "(lam @(mm-var \"ph\") @(mm-var \"ph\"))",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "closed structural name binds lexically");
    check_regular_term_pattern(
        &arena, "(lam @(: x u0) @(: x u0))",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "quoted structural colon is a name, not typed-binder syntax");
    check_regular_term_pattern(
        &arena, "(lam x @x)",
        "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
        "bare binder name equals its explicit quote");
    check_regular_term_pattern(
        &arena, "(lam @_ @_)",
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
    CettaPrimeRegularTermElaborationV1 typed_binder = lower_syntax(
        &arena, "(lam (x : u0) x)", UINT64_C(100000));
    check(typed_binder.status == CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
          "typed lambda waits for annotation-preserving authority");
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
    CettaPrimeRegularTermElaborationV1 level_budget = lower_syntax(
        &arena, "(u 2)", 2u);
    check(level_budget.status ==
              CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
          "universe sugar expansion preserves explicit budget exhaustion");
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
