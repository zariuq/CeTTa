#include "term_graph.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool leaf_eq(Atom *a, Atom *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ATOM_VAR: return a->var_id == b->var_id;
    case ATOM_SYMBOL: return a->sym_id == b->sym_id;
    case ATOM_GROUNDED:
        return a->ground.gkind == GV_INT && b->ground.gkind == GV_INT &&
               a->ground.ival == b->ground.ival;
    default: return false;
    }
}

static Atom *app(Arena *a, const char *name, Atom *x) {
    return atom_expr2(a, atom_symbol(a, name), x);
}

static CettaTermMatch run(Arena *a, CettaTermMatchPair *pairs, size_t count,
                          CettaTermMatchMode mode, bool succeeds) {
    Atom *sentinel = (Atom *)(uintptr_t)1u;
    CettaTermMatch result = {&sentinel, &sentinel, 777u};
    CettaTermMatchStatus status = term_graph_match_many(a, pairs, count, mode,
                                                       leaf_eq, &result);
    assert(status == (succeeds ? CETTA_TERM_MATCH_OK : CETTA_TERM_MATCH_MISMATCH));
    if (!succeeds)
        assert(result.variables == &sentinel && result.values == &sentinel &&
               result.count == 777u);
    return result;
}

static Atom *binding(CettaTermMatch result, VarId id) {
    for (uint32_t i = 0; i < result.count; ++i)
        if (result.variables[i]->var_id == id) return result.values[i];
    return NULL;
}

static void direction_and_aliases(Arena *a) {
    Atom *x = atom_var_with_id(a, "x", 1u);
    Atom *u = atom_var_with_id(a, "u", 2u);
    Atom *v = atom_var_with_id(a, "u", 3u);
    Atom *same_u = atom_var_with_id(a, "another-spelling", 2u);
    Atom *one = atom_int(a, 1), *two = atom_int(a, 2);
    CettaTermMatchPair pairs[] = {{app(a, "p", x), app(a, "p", u)},
                                 {app(a, "q", x), app(a, "q", one)}};
    run(a, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);
    CettaTermMatchPair swapped[] = {pairs[1], pairs[0]};
    run(a, swapped, 2u, CETTA_TERM_MATCH_FORWARD, false);
    pairs[1].right = app(a, "q", same_u);
    CettaTermMatch result = run(a, pairs, 2u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 1u && binding(result, 1u) == u);
    pairs[1].right = app(a, "q", v);
    run(a, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);
    pairs[0].right = app(a, "p", one);
    pairs[1].right = app(a, "q", one);
    result = run(a, pairs, 2u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 1u && binding(result, 1u) == one);
    pairs[1].right = app(a, "q", two);
    run(a, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);

    CettaTermMatchPair pair = {app(a, "p", one), app(a, "p", x)};
    run(a, &pair, 1u, CETTA_TERM_MATCH_FORWARD, false);
    result = run(a, &pair, 1u, CETTA_TERM_MATCH_REVERSE, true);
    assert(result.count == 1u && binding(result, 1u) == one);
    pair = (CettaTermMatchPair){u, x};
    result = run(a, &pair, 1u, CETTA_TERM_MATCH_REVERSE, true);
    assert(result.count == 1u && binding(result, 1u) == u);

    /* Protection comes from the whole batch, including later equations. */
    CettaTermMatchPair aliases[] = {{x, one}, {u, x}};
    run(a, aliases, 2u, CETTA_TERM_MATCH_FORWARD, false);
    aliases[0].right = x;
    result = run(a, aliases, 2u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 1u && binding(result, 2u) == x);

    pair = (CettaTermMatchPair){atom_expr2(a, x, x), atom_expr2(a, u, same_u)};
    run(a, &pair, 1u, CETTA_TERM_MATCH_FORWARD, true);
    pair.right = atom_expr2(a, u, v);
    run(a, &pair, 1u, CETTA_TERM_MATCH_FORWARD, false);
    pair = (CettaTermMatchPair){x, app(a, "p", x)};
    run(a, &pair, 1u, CETTA_TERM_MATCH_FORWARD, false);
    result = run(a, NULL, 0u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 0u && !result.variables && !result.values);
}

static void variants(Arena *a) {
    Atom *x = atom_var_with_id(a, "x", 1u), *y = atom_var_with_id(a, "y", 2u);
    Atom *u = atom_var_with_id(a, "u", 3u), *v = atom_var_with_id(a, "v", 4u);
    CettaTermMatchPair pairs[] = {{app(a, "p", x), app(a, "p", u)},
                                 {app(a, "q", x), app(a, "q", v)}};
    run(a, pairs, 2u, CETTA_TERM_MATCH_VARIANT, false);
    pairs[1].right = app(a, "q", u);
    CettaTermMatch result = run(a, pairs, 2u, CETTA_TERM_MATCH_VARIANT, true);
    assert(result.count == 1u && binding(result, 1u) == u);
    pairs[1].left = app(a, "q", y);
    run(a, pairs, 2u, CETTA_TERM_MATCH_VARIANT, false);
    pairs[1].right = app(a, "q", v);
    result = run(a, pairs, 2u, CETTA_TERM_MATCH_VARIANT, true);
    assert(result.count == 2u && binding(result, 1u) == u && binding(result, 2u) == v);

    /* A permutation is simultaneous, not recursive substitution. */
    CettaTermMatchPair pair = {atom_expr2(a, x, y), atom_expr2(a, y, x)};
    result = run(a, &pair, 1u, CETTA_TERM_MATCH_VARIANT, true);
    assert(binding(result, 1u) == y && binding(result, 2u) == x);
    pair.right = atom_expr2(a, atom_int(a, 1), x);
    run(a, &pair, 1u, CETTA_TERM_MATCH_VARIANT, false);
    pair = (CettaTermMatchPair){atom_int(a, 1), atom_int(a, 1)};
    result = run(a, &pair, 1u, CETTA_TERM_MATCH_VARIANT, true);
    assert(result.count == 0u);
}

/* Small finite-tree reference: collect all protected identities first, then
 * recursively interpret equations with a flat substitution. It does not use
 * graph views, native matcher indexing, memoization or native graph equality. */
enum { REF_VARS = 32 };
typedef struct {
    bool rigid[REF_VARS];
    Atom *values[REF_VARS];
    Atom *reverse[REF_VARS];
} Reference;

static void protect(Reference *r, Atom *term) {
    if (term->kind == ATOM_VAR) {
        assert(term->var_id < REF_VARS);
        r->rigid[term->var_id] = true;
    } else if (term->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0; i < term->expr.len; ++i)
            protect(r, term->expr.elems[i]);
}

static bool tree_eq(Atom *a, Atom *b) {
    if (a->kind != ATOM_EXPR || b->kind != ATOM_EXPR)
        return a->kind != ATOM_EXPR && b->kind != ATOM_EXPR && leaf_eq(a, b);
    if (a->expr.len != b->expr.len) return false;
    for (CettaExprIndex i = 0; i < a->expr.len; ++i)
        if (!tree_eq(a->expr.elems[i], b->expr.elems[i])) return false;
    return true;
}

static bool reference_pair(Reference *r, Atom *pattern, Atom *subject, bool variant) {
    if (pattern->kind == ATOM_VAR && !r->rigid[pattern->var_id]) {
        if (variant) {
            if (subject->kind != ATOM_VAR) return false;
            Atom *previous = r->reverse[subject->var_id];
            if (previous && previous->var_id != pattern->var_id) return false;
            r->reverse[subject->var_id] = pattern;
        }
        Atom *previous = r->values[pattern->var_id];
        if (previous) return tree_eq(previous, subject);
        r->values[pattern->var_id] = subject;
        return true;
    }
    if (pattern->kind != ATOM_EXPR || subject->kind != ATOM_EXPR)
        return pattern->kind != ATOM_EXPR && subject->kind != ATOM_EXPR &&
               leaf_eq(pattern, subject);
    if (pattern->expr.len != subject->expr.len) return false;
    for (CettaExprIndex i = 0; i < pattern->expr.len; ++i)
        if (!reference_pair(r, pattern->expr.elems[i], subject->expr.elems[i], variant))
            return false;
    return true;
}

static uint32_t random_state = 71933u;
static uint32_t random_next(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 17u;
    random_state ^= random_state << 5u;
    return random_state;
}

static void finite_reference(Arena *a) {
    enum { N = 128, TRIALS = 12000 };
    Atom *terms[N];
    unsigned depth[N] = {0};
    for (unsigned i = 0; i < N; ++i) {
        unsigned choice = random_next() % 4u;
        if (i < 12u || choice == 0u)
            terms[i] = atom_var_with_id(a, "v", random_next() % 12u);
        else if (choice == 1u)
            terms[i] = atom_int(a, random_next() % 3u);
        else {
            unsigned left = random_next() % i, right = random_next() % i;
            if (depth[left] > 4u || depth[right] > 4u) left = right = 0u;
            terms[i] = atom_expr2(a, terms[left], terms[right]);
            depth[i] = 1u + (depth[left] > depth[right] ? depth[left] : depth[right]);
        }
    }
    for (unsigned trial = 0; trial < TRIALS; ++trial) {
        CettaTermMatchPair pairs[3];
        for (unsigned i = 0; i < 3u; ++i)
            pairs[i] = (CettaTermMatchPair){terms[random_next() % N], terms[random_next() % N]};
        size_t count = trial % 3u + 1u;
        for (CettaTermMatchMode mode = CETTA_TERM_MATCH_FORWARD;
             mode <= CETTA_TERM_MATCH_VARIANT; ++mode) {
            Reference ref = {0};
            if (mode != CETTA_TERM_MATCH_VARIANT)
                for (size_t i = 0; i < count; ++i)
                    protect(&ref, mode == CETTA_TERM_MATCH_FORWARD ? pairs[i].right : pairs[i].left);
            bool expected = true;
            for (size_t i = 0; i < count && expected; ++i)
                expected = reference_pair(&ref,
                    mode == CETTA_TERM_MATCH_REVERSE ? pairs[i].right : pairs[i].left,
                    mode == CETTA_TERM_MATCH_REVERSE ? pairs[i].left : pairs[i].right,
                    mode == CETTA_TERM_MATCH_VARIANT);
            CettaTermMatch result = run(a, pairs, count, mode, expected);
            if (expected) {
                unsigned vars = 0u;
                for (VarId id = 0; id < REF_VARS; ++id) {
                    Atom *value = binding(result, id);
                    assert((value != NULL) == (ref.values[id] != NULL));
                    if (value) { assert(tree_eq(value, ref.values[id])); ++vars; }
                }
                assert(vars == result.count);
            }
        }
    }
}

static void shared_depth(Arena *arena, size_t depth) {
    Atom *left = calloc(depth + 1u, sizeof(*left));
    Atom *right = calloc(depth + 1u, sizeof(*right));
    Atom **le = calloc(2u * (depth + 1u), sizeof(*le));
    Atom **re = calloc(2u * (depth + 1u), sizeof(*re));
    assert(left && right && le && re);
    left[0].kind = ATOM_VAR; left[0].var_id = 1u;
    right[0].kind = ATOM_VAR; right[0].var_id = 2u;
    for (size_t i = 1u; i <= depth; ++i) {
        left[i].kind = right[i].kind = ATOM_EXPR;
        left[i].expr.len = right[i].expr.len = 2u;
        left[i].expr.elems = &le[2u * i]; right[i].expr.elems = &re[2u * i];
        le[2u * i] = le[2u * i + 1u] = &left[i - 1u];
        re[2u * i] = re[2u * i + 1u] = &right[i - 1u];
    }
    CettaTermMatchPair pair = {&left[depth], &right[depth]};
    test_runtime_stats_reset_counters();
    CettaTermMatch result = run(arena, &pair, 1u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 1u && binding(result, 1u) == &right[0]);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_PAIR_VISIT) <= 3u * depth + 4u);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_SUBJECT_VIEW) <= 3u * depth + 4u);
    run(arena, &pair, 1u, CETTA_TERM_MATCH_VARIANT, true);
    free(le); free(re); free(left); free(right);
}

static void cycles(Arena *arena) {
    Atom a = {.kind = ATOM_EXPR}, b = {.kind = ATOM_EXPR}, c = {.kind = ATOM_EXPR};
    Atom *x = atom_var_with_id(arena, "x", 1u), *u = atom_var_with_id(arena, "u", 2u);
    Atom *ac[] = {x, &a}, *bc[] = {u, &c}, *cc[] = {u, &b};
    a.expr.elems = ac; b.expr.elems = bc; c.expr.elems = cc;
    a.expr.len = b.expr.len = c.expr.len = 2u;
    CettaTermMatchPair pair = {&a, &b};
    CettaTermMatch result = run(arena, &pair, 1u, CETTA_TERM_MATCH_FORWARD, true);
    assert(result.count == 1u && binding(result, 1u) == u);
    run(arena, &pair, 1u, CETTA_TERM_MATCH_VARIANT, true);
    cc[0] = atom_int(arena, 7);
    run(arena, &pair, 1u, CETTA_TERM_MATCH_FORWARD, false);
    run(arena, &pair, 1u, CETTA_TERM_MATCH_VARIANT, false);
}

static void carrier_environments(Arena *arena) {
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    uint32_t parameter = term_graph_add_param(graph, 0u);
    uint32_t root = term_graph_add_compound(graph, atom_symbol(arena, "cycle")->sym_id, 2u);
    assert(term_graph_set_child(graph, root, 0u, parameter));
    assert(term_graph_set_child(graph, root, 1u, root));
    assert(term_graph_seal(graph));
    Atom *x = atom_var_with_id(arena, "x", 1u);
    Atom *y = atom_var_with_id(arena, "y", 2u);
    Atom *u = atom_var_with_id(arena, "u", 3u);
    Atom *v = atom_var_with_id(arena, "v", 4u);
    Atom *xe[] = {x}, *ye[] = {y}, *ue[] = {u}, *ve[] = {v};
    Atom *ae[] = {atom_int(arena, 9)};
    Atom *cx = term_graph_node_value(arena, graph, root, xe);
    Atom *cy = term_graph_node_value(arena, graph, root, ye);
    Atom *cu = term_graph_node_value(arena, graph, root, ue);
    Atom *cv = term_graph_node_value(arena, graph, root, ve);
    Atom *ca = term_graph_node_value(arena, graph, root, ae);
    assert(cx && cy && cu && cv && ca);

    /* The same graph node under two environments is two term views. */
    CettaTermMatchPair pairs[] = {{cx, cu}, {cx, cv}};
    run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);
    run(arena, pairs, 2u, CETTA_TERM_MATCH_VARIANT, false);
    pairs[1].left = cy;
    CettaTermMatch result = run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, true);
    assert(binding(result, x->var_id) == u && binding(result, y->var_id) == v);
    run(arena, pairs, 2u, CETTA_TERM_MATCH_VARIANT, true);
    pairs[1].right = cu;
    run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, true);
    run(arena, pairs, 2u, CETTA_TERM_MATCH_VARIANT, false);

    /* Capturing a carrier does not free the variables in its environment. */
    pairs[0] = (CettaTermMatchPair){x, cu};
    pairs[1] = (CettaTermMatchPair){u, ae[0]};
    run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);
    pairs[1] = (CettaTermMatchPair){x, ca};
    run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, false);
    pairs[1] = (CettaTermMatchPair){x, cu};
    result = run(arena, pairs, 2u, CETTA_TERM_MATCH_FORWARD, true);
    assert(term_graph_value_eq(binding(result, x->var_id), cu, leaf_eq));

    /* A cyclic variant's environment renaming is still simultaneous. */
    pairs[0] = (CettaTermMatchPair){cx, cy};
    pairs[1] = (CettaTermMatchPair){cy, cx};
    result = run(arena, pairs, 2u, CETTA_TERM_MATCH_VARIANT, true);
    assert(binding(result, x->var_id) == y && binding(result, y->var_id) == x);
    term_graph_release(graph);
}

static void failures(Arena *arena) {
    Atom *x = atom_int(arena, 1);
    CettaTermMatchPair pair = {x, x};
    CettaTermMatch out = {NULL, NULL, 77u};
    assert(term_graph_match_many(NULL, &pair, 1u, CETTA_TERM_MATCH_FORWARD, leaf_eq, &out) == CETTA_TERM_MATCH_INVALID);
    assert(term_graph_match_many(arena, NULL, 1u, CETTA_TERM_MATCH_FORWARD, leaf_eq, &out) == CETTA_TERM_MATCH_INVALID);
    assert(term_graph_match_many(arena, &pair, 1u, (CettaTermMatchMode)99, leaf_eq, &out) == CETTA_TERM_MATCH_INVALID);
    assert(term_graph_match_many(arena, &pair, SIZE_MAX, CETTA_TERM_MATCH_FORWARD, leaf_eq, &out) == CETTA_TERM_MATCH_NO_MEMORY);
    pair.right = NULL;
    assert(term_graph_match_many(arena, &pair, 1u, CETTA_TERM_MATCH_FORWARD, leaf_eq, &out) == CETTA_TERM_MATCH_INVALID);
    assert(out.count == 77u && !out.variables && !out.values);
}

static void incremental_prefixes(Arena *arena) {
    Atom *x = atom_var_with_id(arena, "x", 1u);
    Atom *y = atom_var_with_id(arena, "y", 2u);
    Atom *z = atom_var_with_id(arena, "z", 3u);
    Atom *a = atom_symbol(arena, "a"), *b = atom_symbol(arena, "b");
    Atom *terms[] = {x, y, z, a, b, app(arena, "f", x),
                    atom_expr2(arena, x, x), atom_expr2(arena, x, y)};
    enum { N = sizeof(terms) / sizeof(*terms) };
    for (unsigned mode = 0u; mode <= CETTA_TERM_MATCH_VARIANT; ++mode) {
        for (unsigned n = 0u; n < N * N * N * N; ++n) {
            CettaTermMatchPair pairs[] = {
                {terms[n % N], terms[(n / N) % N]},
                {terms[(n / (N * N)) % N], terms[n / (N * N * N)]}};
            CettaTermMatch whole;
            CettaTermMatchStatus expected = term_graph_match_many(arena, pairs,
                2u, (CettaTermMatchMode)mode, leaf_eq, &whole);
            CettaTermMatchState prefix = {.mode = (CettaTermMatchMode)mode};
            CettaTermMatchStatus status = CETTA_TERM_MATCH_OK;
            for (unsigned i = 0u; i < 2u && status == CETTA_TERM_MATCH_OK; ++i) {
                CettaTermMatchState next = {.images.count = 777u};
                status = term_graph_match_extend(arena, &prefix, &pairs[i], 1u,
                    (CettaTermMatchMode)mode, leaf_eq, &next);
                if (status == CETTA_TERM_MATCH_OK) prefix = next;
                else assert(next.images.count == 777u && !next.images.variables);
            }
            assert(status == expected);
            if (status != CETTA_TERM_MATCH_OK) continue;
            assert(prefix.images.count == whole.count);
            for (uint32_t i = 0u; i < whole.count; ++i) {
                Atom *image = binding(prefix.images, whole.variables[i]->var_id);
                assert(image && term_graph_value_eq(image, whole.values[i], leaf_eq));
            }
        }
    }
    /* Extending a prefix must not walk its earlier large subject again. */
    Atom *payload = a;
    for (unsigned i = 0u; i < 10000u; ++i)
        payload = atom_expr2(arena, payload, payload);
    CettaTermMatchPair first = {x, payload}, second = {y, b};
    CettaTermMatchState prefix, extended;
    assert(term_graph_match_extend(arena, NULL, &first, 1u,
        CETTA_TERM_MATCH_FORWARD, leaf_eq, &prefix) == CETTA_TERM_MATCH_OK);
    test_runtime_stats_reset_counters();
    assert(term_graph_match_extend(arena, &prefix, &second, 1u,
        CETTA_TERM_MATCH_FORWARD, leaf_eq, &extended) == CETTA_TERM_MATCH_OK);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_SUBJECT_VIEW) == 1u);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_PAIR_VISIT) == 1u);
    assert(binding(extended.images, x->var_id) == payload);
    assert(term_graph_match_extend(arena, &prefix, &second, 1u,
        CETTA_TERM_MATCH_REVERSE, leaf_eq, &extended) == CETTA_TERM_MATCH_INVALID);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
    direction_and_aliases(&arena);
    variants(&arena);
    finite_reference(&arena);
    shared_depth(&arena, 24u);
    shared_depth(&arena, 100000u);
    cycles(&arena);
    carrier_environments(&arena);
    failures(&arena);
    incremental_prefixes(&arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: directional graph matching, global variants, protected batches and rollback");
    return 0;
}
