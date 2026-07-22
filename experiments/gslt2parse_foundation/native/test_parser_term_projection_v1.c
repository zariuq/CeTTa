#include "parser_term_projection_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                                         \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL: %s\n", (message));                    \
            return false;                                                 \
        }                                                                 \
    } while (0)

static const char PLAN[] =
    "(ptp-plan-v1 node document Term Ref pair cons nil cp "
    "(ptp-cons text (ptp-cons var (ptp-cons bind ptp-nil))) "
    "(ptp-cons var ptp-nil) bind "
    "(ptp-cons (ptp-symbol-rule Z z Term) "
    "(ptp-cons (ptp-fixed-rule U use Term "
    "(ptp-cons 0 ptp-nil) (ptp-cons Ref ptp-nil)) "
    "(ptp-cons (ptp-quote-rule Q quote Ref 0 Term) "
    "(ptp-cons (ptp-monoid-rule J join Term z Term) "
    "(ptp-cons (ptp-fixed-rule S seq Term "
    "(ptp-cons 1 (ptp-cons 0 ptp-nil)) "
    "(ptp-cons Ref (ptp-cons Term ptp-nil))) "
    "(ptp-cons (ptp-binder-rule L let Term 1 0 2 Ref Ref Term) "
    "ptp-nil)))))))";

static Atom *parse(Arena *arena, const char *text) {
    Atom *term = NULL;
    char error[512] = {0};

    if (!fh_ground_term_v1_parse(
            arena, (const uint8_t *)text, strlen(text),
            &term, error, sizeof(error))) {
        fprintf(stderr, "FAIL: ground-term parse: %s\n", error);
        return NULL;
    }
    return term;
}

static bool is_app(Atom *atom, const char *head, CettaExprLen arguments) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == arguments + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool project(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    const char *semantic,
    uint32_t depth_limit,
    Atom **out,
    char *error,
    size_t error_cap) {
    Atom *term = parse(arena, semantic);

    return term && ppterm_projection_v1_project_result(
        plan, term, arena, depth_limit, out, error, error_cap);
}

static bool fixed_order_and_unicode_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    const char *semantic =
        "(result (node document "
        "(node S (pair (node Z (cp 122)) "
        "(node var (node text (cons (cp 955) nil)))))) nil)";
    Atom *output = NULL;
    char error[512] = {0};

    CHECK(project(plan, arena, semantic, 128u,
                  &output, error, sizeof(error)), error);
    CHECK(is_app(output, "seq", 2u), "fixed projection head/arity");
    CHECK(output->expr.elems[1]->kind == ATOM_VAR,
          "fixed projection did not move the reference child");
    CHECK(strcmp(atom_name_cstr(output->expr.elems[1]), "\xce\xbb") == 0,
          "Unicode variable spelling changed");
    CHECK(atom_is_symbol(output->expr.elems[2], "z"),
          "fixed projection did not move the term child");
    (*passed) += 3u;
    return true;
}

static bool binder_scope_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    const char *semantic =
        "(result (node document "
        "(node L (pair "
        "(node bind (node text (cons (cp 120) nil))) "
        "(pair "
        "(node var (node text (cons (cp 120) nil))) "
        "(node U (node var (node text (cons (cp 120) nil)))))))) nil)";
    Atom *output = NULL;
    Atom *channel;
    Atom *binder;
    Atom *body_use;
    char error[512] = {0};

    CHECK(project(plan, arena, semantic, 128u,
                  &output, error, sizeof(error)), error);
    CHECK(is_app(output, "let", 3u), "binder projection head/arity");
    channel = output->expr.elems[1];
    binder = output->expr.elems[2];
    body_use = output->expr.elems[3];
    CHECK(channel->kind == ATOM_VAR && binder->kind == ATOM_VAR,
          "binder/channel did not materialize as variables");
    CHECK(channel != binder && channel->var_id != binder->var_id,
          "binder captured the channel before entering scope");
    CHECK(is_app(body_use, "use", 1u) &&
          body_use->expr.elems[1] == binder,
          "bound occurrence did not reuse binder identity");
    (*passed) += 4u;
    return true;
}

static bool quote_scope_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    const char *semantic =
        "(result (node document "
        "(node L (pair "
        "(node bind (node text (cons (cp 120) nil))) "
        "(pair "
        "(node var (node text (cons (cp 120) nil))) "
        "(node S (pair "
        "(node U (node var (node text (cons (cp 120) nil)))) "
        "(node Q (node L (pair "
        "(node bind (node text (cons (cp 121) nil))) "
        "(pair "
        "(node var (node text (cons (cp 120) nil))) "
        "(node U (node var (node text (cons (cp 121) nil)))))))))))))) nil)";
    Atom *output = NULL;
    Atom *channel;
    Atom *binder;
    Atom *body_seq;
    Atom *quote;
    Atom *inner_let;
    Atom *inner_binder;
    Atom *inner_use;
    Atom *later_use;
    char error[512] = {0};

    CHECK(project(plan, arena, semantic, 128u,
                  &output, error, sizeof(error)), error);
    CHECK(is_app(output, "let", 3u), "quote fixture binder");
    channel = output->expr.elems[1];
    binder = output->expr.elems[2];
    body_seq = output->expr.elems[3];
    CHECK(is_app(body_seq, "seq", 2u), "quote fixture sequence");
    quote = body_seq->expr.elems[1];
    CHECK(is_app(quote, "quote", 1u), "quote projection head");
    inner_let = quote->expr.elems[1];
    CHECK(is_app(inner_let, "let", 3u), "quote fixture inner binder");
    CHECK(inner_let->expr.elems[1] == channel,
          "quote did not reset outer scope to the shared free name");
    inner_binder = inner_let->expr.elems[2];
    inner_use = inner_let->expr.elems[3];
    CHECK(is_app(inner_use, "use", 1u) &&
          inner_use->expr.elems[1] == inner_binder,
          "binder inside quote did not retain its identity");
    later_use = body_seq->expr.elems[2];
    CHECK(is_app(later_use, "use", 1u) &&
          later_use->expr.elems[1] == binder,
          "quote-local binder overwrote the restored outer binder");
    CHECK(inner_binder != binder && channel != binder,
          "quote/binder identities collapsed");
    (*passed) += 8u;
    return true;
}

static bool monoid_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    const char *semantic =
        "(result (node document "
        "(node J (cons "
        "(node Z nil) "
        "(cons (node U (node var (node text (cons (cp 98) nil)))) "
        "(cons (node J (cons "
        "(node U (node var (node text (cons (cp 97) nil)))) "
        "(cons (node U (node var (node text (cons (cp 97) nil)))) "
        "nil))) nil))))) nil)";
    Atom *output = NULL;
    Atom *left;
    Atom *middle;
    Atom *right;
    char error[512] = {0};

    CHECK(project(plan, arena, semantic, 256u,
                  &output, error, sizeof(error)), error);
    CHECK(is_app(output, "join", 3u),
          "monoid did not flatten nested collection and remove unit");
    left = output->expr.elems[1];
    middle = output->expr.elems[2];
    right = output->expr.elems[3];
    CHECK(is_app(left, "use", 1u) &&
          is_app(middle, "use", 1u) &&
          is_app(right, "use", 1u),
          "monoid children changed shape");
    CHECK(strcmp(atom_name_cstr(left->expr.elems[1]), "a") == 0 &&
          strcmp(atom_name_cstr(middle->expr.elems[1]), "a") == 0 &&
          strcmp(atom_name_cstr(right->expr.elems[1]), "b") == 0,
          "monoid did not sort by alpha-invariant scoped keys");
    CHECK(left->expr.elems[1] == middle->expr.elems[1],
          "repeated free name did not share one variable identity");
    CHECK(left != middle,
          "monoid incorrectly collapsed multiplicity");
    (*passed) += 5u;
    return true;
}

static bool rejection_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    static const char *const rejected[] = {
        "(result (node document (node Z nil)) trailing)",
        "(result (node document "
        "(node S (pair (node Z nil) (node Z nil)))) nil)",
        "(result (node document "
        "(node L (pair "
        "(node var (node text (cons (cp 120) nil))) "
        "(pair (node var (node text (cons (cp 120) nil))) "
        "(node Z nil))))) nil)",
    };
    size_t index;

    for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]);
         index++) {
        Atom *output = (Atom *)plan;
        char error[512] = {0};

        CHECK(!project(plan, arena, rejected[index], 128u,
                       &output, error, sizeof(error)),
              "malformed semantic result was accepted");
        CHECK(output == NULL && error[0] != '\0',
              "semantic rejection was not fail-closed");
        (*passed)++;
    }
    {
        Atom *output = (Atom *)plan;
        char error[512] = {0};

        CHECK(!project(
                  plan, arena,
                  "(result (node document (node U "
                  "(node var (node text (cons (cp 120) nil))))) nil)",
                  1u, &output, error, sizeof(error)),
              "depth-limited projection was accepted");
        CHECK(output == NULL && error[0] != '\0',
              "depth rejection was not fail-closed");
        (*passed)++;
    }
    return true;
}

static Atom *find_binder_rule(Atom *compiled_plan) {
    Atom *cursor;

    if (!compiled_plan || compiled_plan->kind != ATOM_EXPR ||
        compiled_plan->expr.len != 13u)
        return NULL;
    cursor = compiled_plan->expr.elems[12];
    while (!atom_is_symbol(cursor, "ptp-nil")) {
        Atom *rule;
        if (!is_app(cursor, "ptp-cons", 2u))
            return NULL;
        rule = cursor->expr.elems[1];
        if (rule && rule->kind == ATOM_EXPR && rule->expr.len > 0u &&
            atom_is_symbol(rule->expr.elems[0], "ptp-binder-rule"))
            return rule;
        cursor = cursor->expr.elems[2];
    }
    return NULL;
}

static bool atomic_plan_gate(
    PPTermProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    Atom *mutated = parse(arena, PLAN);
    Atom *binder_rule = find_binder_rule(mutated);
    Atom *output = NULL;
    char error[512] = {0};

    CHECK(binder_rule && binder_rule->expr.len == 10u,
          "could not locate binder rule mutation site");
    binder_rule->expr.elems[5] = binder_rule->expr.elems[4];
    CHECK(!ppterm_projection_v1_plan_load(
              plan, mutated, error, sizeof(error)) &&
          error[0] != '\0',
          "non-bijective binder plan was accepted");
    error[0] = '\0';
    CHECK(project(
              plan, arena,
              "(result (node document "
              "(node S (pair (node Z nil) "
              "(node var (node text (cons (cp 120) nil)))))) nil)",
              128u, &output, error, sizeof(error)),
          "failed plan load destroyed the prior valid plan");
    CHECK(is_app(output, "seq", 2u),
          "prior plan changed after failed replacement");
    (*passed) += 3u;
    return true;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    PPTermProjectionV1Plan plan;
    Atom *compiled_plan;
    char error[512] = {0};
    size_t passed = 0u;
    bool ok;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppterm_projection_v1_plan_init(&plan);

    compiled_plan = parse(&arena, PLAN);
    ok = compiled_plan &&
        ppterm_projection_v1_plan_load(
            &plan, compiled_plan, error, sizeof(error));
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", error);
    else
        passed++;
    ok = ok &&
        fixed_order_and_unicode_gate(&plan, &arena, &passed) &&
        binder_scope_gate(&plan, &arena, &passed) &&
        quote_scope_gate(&plan, &arena, &passed) &&
        monoid_gate(&plan, &arena, &passed) &&
        rejection_gate(&plan, &arena, &passed) &&
        atomic_plan_gate(&plan, &arena, &passed);

    ppterm_projection_v1_plan_free(&plan);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    if (!ok)
        return 1;
    printf("(ParserTermProjectionV1NativeSummary %zu 0)\n", passed);
    return 0;
}
