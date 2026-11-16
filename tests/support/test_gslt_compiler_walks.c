#include "native/gslt_petta_direct_v1.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>

static Atom *chain(Arena *arena, Atom *leaf, size_t depth) {
    Atom *head = atom_symbol(arena, "Box");
    for (size_t i = 0u; i < depth; ++i) {
        Atom *items[] = {head, leaf};
        leaf = atom_expr(arena, items, 2u); assert(leaf);
    }
    return leaf;
}
int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena; arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x"), *y = atom_var(&arena, "y");
    Atom *vx = chain(&arena, x, 6000u), *vy = chain(&arena, y, 6000u);
    Atom *one = chain(&arena, atom_int(&arena, 1), 6000u);
    Atom *two = chain(&arena, atom_int(&arena, 2), 6000u);
    DirectVariablePairsV1 pairs = {0}; char error[512] = {0};
    assert(direct_patterns_may_unify_v1(vx, one));
    assert(!direct_patterns_may_unify_v1(one, two));
    assert(direct_patterns_overlap_v1(one, vx));
    assert(!direct_patterns_overlap_v1(one, two));
    assert(direct_align_input_patterns_v1(vx, vy, &pairs,  error, sizeof error));
    assert(pairs.len == 1u);
    assert(direct_terms_correspond_v1(vx, vy, &pairs));
    assert(!direct_terms_correspond_v1(vx, one, &pairs));
    assert(direct_align_overlapping_input_patterns_v1(vx, one, &pairs,  error, sizeof error));
    assert(direct_terms_equal_v1(one, one));
    assert(!direct_terms_equal_v1(one, two));
    assert(direct_source_equal_v1(one, one, &arena));
    assert(!direct_source_equal_v1(one, two, &arena));
    assert(!direct_source_equal_v1(atom_int(&arena, 1), atom_float(&arena, 1.0), &arena));
    DirectVariablePairsV1 bindings = {0};
    assert(direct_pattern_subsumes_v1(vx, one, &bindings,  error, sizeof error));
    assert(bindings.len == 1u && direct_terms_equal_v1(bindings.items[0].right, atom_int(&arena, 1)));
    assert(!direct_pattern_subsumes_v1(vx, two, &bindings,  error, sizeof error));
    free(bindings.items); free(pairs.items);
    DirectVariablePairsV1 mapping = {0}, equalities = {0};
    assert(direct_collect_nonlinear_equalities_v1(vx, vy, &mapping, &equalities,  error, sizeof error));
    assert(mapping.len == 1u && equalities.len == 0u);
    Atom *z = atom_var(&arena, "z");
    assert(direct_collect_nonlinear_equalities_v1(x, z, &mapping, &equalities,  error, sizeof error));
    assert(equalities.len == 1u && direct_same_variable_v1(equalities.items[0].left, y));
    free(mapping.items); free(equalities.items);
    DirectVariablePairV1 assignment = {x, atom_int(&arena, 1)};
    DirectVariablePairsV1 assigned = {.items = &assignment, .len = 1u};
    assert(direct_guard_terms_equal_after_binding_v1(vx, one, &assigned));
    assert(!direct_guard_terms_equal_after_binding_v1(vx, two, &assigned));
    assignment.right = chain(&arena, x, 1u);
    assert(!direct_guard_terms_equal_after_binding_v1(x, x, &assigned));
    Atom *nan = atom_float(&arena, NAN);
    assert(direct_terms_equal_v1(nan, nan) == atom_eq(nan, nan));
    Atom *nanbox = chain(&arena, nan, 1u);
    assert(direct_terms_equal_v1(nanbox, nanbox) == atom_eq(nanbox, nanbox));
    assert(direct_factor_term_ground_v1(one));
    assert(!direct_factor_term_ground_v1(vx));
    assert(!direct_petta_pattern_requires_body_match_v1(one));
    Atom *cons_items[] = {atom_symbol(&arena, "cons"), atom_int(&arena, 0)};
    assert(direct_petta_pattern_requires_body_match_v1(chain(&arena,
        atom_expr(&arena, cons_items, 2u), 6000u)));
    CettaLdTextV1 sort = {(uint8_t *)"Expr", 4u};
    CettaLdTypeDeclV1 type = {.name = sort, .carrier = CETTA_LD_CARRIER_AST_V1};
    CettaLdTermParamV1 param = {.kind = CETTA_LD_PARAM_SIMPLE_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = sort}};
    CettaLdGrammarRuleV1 constructor = {.label = {(uint8_t *)"Box", 3u},
        .category = sort, .params = &param, .param_len = 1u};
    CettaLanguageDefCoreV1 language = {.types = &type, .type_len = 1u,
        .terms = &constructor, .term_len = 1u};
    DirectDomainEnvironmentV1 env = {0};
    assert(direct_domain_pattern_v1(&language, vx, 1u, &env));
    assert(env.len == 6001u && !env.inconsistent);
    assert(direct_domain_term_v1(&language, &env, vx) == 1u);
    assert(direct_domain_term_v1(&language, &env, atom_var(&arena, "x")) == 1u);
    assert(direct_domain_term_v1(&language, &env, vy) == 0u);
    assert(direct_domain_bind_v1(&env, x, 2u, 0u) && env.inconsistent);
    assert(direct_domain_term_v1(&language, &env, vx) == 0u);
    free(env.items); free(env.buckets);
    const Atom *group[] = {one, two};
    size_t slots = 0u; bool aborted = false;
    DirectFactorNodeV1 *factor = direct_factor_scan_v1(group, 2u, false, &slots, &aborted);
    assert(factor && slots == 1u && !aborted);
    DirectBufferV1 pattern = {0};
    assert(direct_factor_render_template_v1(&pattern, factor, &arena, true));
    assert(pattern.len > 6000u * 20u); free(pattern.bytes);
    DirectBufferV1 residues = {0}; bool first = true;
    assert(direct_factor_render_residues_v1(&residues, factor, one, false, &first, &arena));
    assert(direct_factor_render_residues_v1(&residues, factor, two, false, &first, &arena));
    assert(residues.len == 3u && !memcmp(residues.bytes, "1 2", 3u)); free(residues.bytes);
    direct_factor_node_free_v1(factor);
    arena_free(&arena);
    g_symbols = NULL; symbol_table_free(&symbols);
    puts("PASS: 6000-deep compiler comparisons preserve matching, aliases and source value kinds");
    return 0;
}
