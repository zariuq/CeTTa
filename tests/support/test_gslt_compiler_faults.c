#include "gslt_petta_direct_v1.h"
#include "atom_walk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

/* Fault only the compiler's working storage and the cursor resource boundary.
 * Source arenas and the runtime allocator keep their normal contract. */
static bool fault_scope, fault_steps;
static size_t fault_at, fault_calls;
static bool compiler_refuses(void) {
    return fault_scope && ++fault_calls == fault_at;
}
static void *compiler_malloc(size_t n) { return compiler_refuses() ? NULL : malloc(n); }
static void *compiler_calloc(size_t n, size_t width) { return compiler_refuses() ? NULL : calloc(n, width); }
static void *compiler_realloc(void *p, size_t n) { return compiler_refuses() ? NULL : realloc(p, n); }
static AtomWalk *compiler_walk_new(const Atom *term, bool unique) {
    return compiler_refuses() ? NULL : atom_walk_new(term, unique);
}
static AtomWalkStep compiler_walk_next(AtomWalk *walk, AtomWalkEvent *event) {
    if (fault_steps && compiler_refuses()) return ATOM_WALK_RESOURCE;
    return atom_walk_next(walk, event);
}
#define malloc compiler_malloc
#define calloc compiler_calloc
#define realloc compiler_realloc
#define atom_walk_new compiler_walk_new
#define atom_walk_next compiler_walk_next
#include "native/gslt_petta_direct_v1.c"
#undef malloc
#undef calloc
#undef realloc
#undef atom_walk_new
#undef atom_walk_next

static Atom *chain(Arena *arena, Atom *leaf, size_t depth) {
    Atom *head = atom_symbol(arena, "Box");
    for (size_t i = 0; i < depth; ++i) leaf = atom_expr2(arena, head, leaf);
    return leaf;
}
static bool covers(const DirectFactorNodeV1 *factor, const Atom *source) {
    if (factor->slot) return true;
    if (factor->constant) return direct_terms_equal_v1(factor->constant, source);
    if (source->kind != ATOM_EXPR || source->expr.len != factor->child_count) return false;
    for (size_t i = 0; i < factor->child_count; ++i)
        if (!factor->children[i] || !covers(factor->children[i], source->expr.elems[i])) return false;
    return true;
}
static void arm(size_t at, bool steps) {
    fault_at = at; fault_calls = 0; fault_steps = steps; fault_scope = true;
}
static size_t disarm(void) { fault_scope = false; return fault_calls; }

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena; arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x"), *y = atom_var(&arena, "y");
    Atom *vx = chain(&arena, x, 96), *vy = chain(&arena, y, 96);
    Atom *one = chain(&arena, atom_int(&arena, 1), 96);
    Atom *two = chain(&arena, atom_int(&arena, 2), 96);
    size_t total = 0;
    DirectCompareV1 comparison = {.kind = DIRECT_COMPARE_EQUAL};
    arm(0, true);
    assert(direct_compare_terms_v1(one, two, comparison) == DIRECT_PAIR_DIFFERENT);
    size_t calls = disarm(); assert(calls > 32);
    for (size_t at = 1; at <= calls; ++at) {
        arm(at, true);
        DirectPairResultV1 result = direct_compare_terms_v1(one, two, comparison);
        disarm(); assert(result == DIRECT_PAIR_UNKNOWN); ++total;
    }
    DirectVariablePairsV1 pairs = {0}; char error[512] = {0};
    arm(0, true);
    assert(direct_align_input_patterns_v1(vx, vy, &pairs, error, sizeof error));
    calls = disarm(); free(pairs.items);
    for (size_t at = 1; at <= calls; ++at) {
        pairs = (DirectVariablePairsV1){0};
        arm(at, true);
        bool ok = direct_align_input_patterns_v1(vx, vy, &pairs, error, sizeof error);
        disarm(); assert(!ok); free(pairs.items); ++total;
    }
    CettaLdTextV1 sort = {(uint8_t *)"Expr", 4};
    CettaLdTypeDeclV1 type = {.name = sort, .carrier = CETTA_LD_CARRIER_AST_V1};
    CettaLdTermParamV1 param = {.kind = CETTA_LD_PARAM_SIMPLE_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = sort}};
    CettaLdGrammarRuleV1 constructor = {.label = {(uint8_t *)"Box", 3},
        .category = sort, .params = &param, .param_len = 1};
    CettaLanguageDefCoreV1 language = {.types = &type, .type_len = 1,
        .terms = &constructor, .term_len = 1};
    DirectDomainEnvironmentV1 env = {0};
    arm(0, false);
    assert(direct_domain_pattern_v1(&language, vx, 1, &env));
    calls = disarm(); free(env.items); free(env.buckets); assert(calls > 4);
    for (size_t at = 1; at <= calls; ++at) {
        env = (DirectDomainEnvironmentV1){0};
        arm(at, false);
        bool ok = direct_domain_pattern_v1(&language, vx, 1, &env);
        disarm(); assert(!ok); free(env.items); free(env.buckets); ++total;
    }
    DirectVariablePairV1 binding = {x, atom_int(&arena, 1)};
    DirectVariablePairsV1 assigned = {.items = &binding, .len = 1};
    arm(0, false);
    assert(direct_guard_terms_equal_after_binding_v1(vx, one, &assigned));
    calls = disarm(); assert(calls >= 2);
    for (size_t at = 1; at <= calls; ++at) {
        arm(at, false);
        bool ok = direct_guard_terms_equal_after_binding_v1(vx, one, &assigned);
        disarm(); assert(!ok); ++total;
    }
    const Atom *group[] = {one, two}; size_t slots = 0; bool aborted = false;
    arm(0, false);
    DirectFactorNodeV1 *factor = direct_factor_scan_v1(group, 2, false, &slots, &aborted);
    calls = disarm(); assert(factor && !aborted && slots == 1 && calls > 100);
    direct_factor_node_free_v1(factor);
    size_t fallbacks = 0;
    for (size_t at = 1; at <= calls; ++at) {
        slots = 0; aborted = false;
        arm(at, false);
        factor = direct_factor_scan_v1(group, 2, false, &slots, &aborted);
        disarm();
        if (factor) {
            assert(covers(factor, one) && covers(factor, two));
            direct_factor_node_free_v1(factor);
        } else ++fallbacks;
        ++total;
    }
    assert(fallbacks > 100);
    DirectBufferV1 expected = {0};
    assert(direct_render_data_pattern_v1(&expected, vx, &arena));
    DirectBufferV1 actual = {0};
    arm(0, true);
    assert(direct_render_data_pattern_v1(&actual, vx, &arena));
    calls = disarm(); free(actual.bytes);
    for (size_t at = 1; at <= calls; ++at) {
        actual = (DirectBufferV1){0};
        arm(at, true);
        bool ok = direct_render_data_pattern_v1(&actual, vx, &arena);
        disarm();
        assert(!ok);
        free(actual.bytes); ++total;
    }
    free(expected.bytes);
    arena_free(&arena); g_symbols = NULL; symbol_table_free(&symbols);
    printf("PASS: %zu compiler storage/cursor refusals preserve unknown comparisons, admission and factoring meaning\n", total);
    return 0;
}
