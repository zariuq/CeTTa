#include "petta_semantics.h"
#include "term_graph.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* An independently recursive tree walk for small generated cases. Leaf
 * order is exercised separately below; this reference has no pair memo. */
static int tree_order(const Atom *left, const Atom *right) {
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR &&
        left->expr.len && right->expr.len) {
        CettaExprLen n = left->expr.len < right->expr.len
            ? left->expr.len : right->expr.len;
        for (CettaExprIndex i = 0u; i < n; i++) {
            int order = tree_order(left->expr.elems[i], right->expr.elems[i]);
            if (order)
                return order;
        }
        return left->expr.len < right->expr.len ? -1 :
               left->expr.len > right->expr.len ? 1 : 0;
    }
    int order = 99;
    assert(petta_semantics_term_compare(left, right, &order));
    return order;
}

static void expect(const Atom *left, const Atom *right, int expected) {
    int forward = 99, backward = 99;
    assert(petta_semantics_term_compare(left, right, &forward));
    assert(petta_semantics_term_compare(right, left, &backward));
    assert(forward == expected && backward == -expected);
}

/* Graph headers use the same expression/leaf boundary as ordinary finite
 * terms. Nil precedes atoms (including the distinct symbol "[]"), while
 * nonempty expressions have compound class. */
static void expression_boundary_cases(Arena *arena) {
    const uint8_t embedded[] = {'[', ']', 0u, 'x'};
    Atom *leaves[] = {
        atom_var_with_id(arena, "x", 7u), atom_int(arena, -1),
        atom_float(arena, NAN), atom_string(arena, "[]"),
        atom_symbol(arena, "A"), atom_symbol(arena, "[]"),
        atom_symbol(arena, "z"), atom_bool(arena, false),
        atom_bool(arena, true),
        atom_symbol_id(arena, symbol_intern_bytes(g_symbols, embedded, sizeof(embedded))),
    };
    Atom *child = atom_int(arena, 7);
    Atom *expressions[] = {
        atom_expr(arena, NULL, 0u), atom_expr(arena, &child, 1u),
    };
    for (size_t e = 0; e < 2u; ++e) {
        for (size_t l = 0; l < sizeof(leaves) / sizeof(leaves[0]); ++l) {
            int ordinary = 99, boundary = 99;
            assert(petta_semantics_term_compare(expressions[e], leaves[l], &ordinary));
            assert(petta_semantics_term_compare_expression_boundary(e != 0u, leaves[l], &boundary));
            assert(boundary == ordinary);
            {
                assert((int)term_graph_value_compare(expressions[e], leaves[l]) == ordinary);
                assert((int)term_graph_value_compare(leaves[l], expressions[e]) == -ordinary);
            }
        }
    }
    int sentinel = 77;
    assert(!petta_semantics_term_compare_expression_boundary(false, NULL, &sentinel));
    assert(!petta_semantics_term_compare_expression_boundary(false, expressions[0], &sentinel));
    assert(sentinel == 77);
}

static uint32_t random_state = 1701u;
static uint32_t next_random(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 17u;
    random_state ^= random_state << 5u;
    return random_state;
}

static void finite_cases(Arena *arena) {
    enum { COUNT = 256 };
    Atom *nodes[COUNT];
    for (unsigned i = 0u; i < COUNT; i++) {
        unsigned kind = next_random() % 5u;
        if (!i || kind < 2u)
            nodes[i] = atom_int(arena, (int)(next_random() % 17u) - 8);
        else if (kind == 2u)
            nodes[i] = atom_var_with_id(arena, "x", next_random() % 17u + 1u);
        else if (kind == 3u)
            nodes[i] = atom_expr2(arena, nodes[next_random() % i], nodes[next_random() % i]);
        else {
            Atom *child = nodes[next_random() % i];
            nodes[i] = atom_expr(arena, &child, 1u);
        }
    }
    for (unsigned i = 0u; i < 100000u; i++) {
        const Atom *a = nodes[next_random() % COUNT];
        const Atom *b = nodes[next_random() % COUNT];
        const Atom *c = nodes[next_random() % COUNT];
        int ab = 99, bc = 99, ac = 99;
        expect(a, b, tree_order(a, b));
        assert(petta_semantics_term_compare(a, b, &ab));
        assert(petta_semantics_term_compare(b, c, &bc));
        assert(petta_semantics_term_compare(a, c, &ac));
        if (ab <= 0 && bc <= 0)
            assert(ac <= 0);
        if (i < 1000u) {
            assert((int)term_graph_value_compare((Atom *)a, (Atom *)b) == ab);

        }
    }
    Atom *integer = atom_int(arena, 1);
    Atom *floating = atom_float(arena, 1.0);
    expect(floating, integer, -1);
    expect(atom_float(arena, -0.0), atom_float(arena, 0.0), -1);
    expect(atom_float(arena, NAN), atom_float(arena, NAN), 0);
    expect(atom_float(arena, -INFINITY), integer, -1);
    expect(atom_float(arena, INFINITY), integer, 1);
    expect(atom_int(arena, INT64_C(9007199254740993)),
           atom_float(arena, 9007199254740992.0), 1);
    Atom *x = atom_var_with_id(arena, "x", 20u);
    Atom *same_x = atom_var_with_id(arena, "renamed", 20u);
    Atom *other_x = atom_var_with_id(arena, "x", 21u);
    expect(x, same_x, 0);
    expect(x, other_x, -1);
    expect(x, integer, -1);
    expect(integer, atom_string(arena, "a"), -1);
    expect(atom_string(arena, "z"), atom_symbol(arena, "a"), -1);
    const uint8_t text_a[] = {'a', 0u, 'b'}, text_b[] = {'a', 0u, 'c'};
    expect(atom_symbol_id(arena, symbol_intern_bytes(g_symbols, text_a, 3u)),
           atom_symbol_id(arena, symbol_intern_bytes(g_symbols, text_b, 3u)), -1);
    expect(atom_symbol(arena, "a"),
           atom_symbol_id(arena, symbol_intern_bytes(g_symbols, text_a, 3u)), -1);
    expect(atom_expr(arena, NULL, 0u), atom_symbol(arena, "a"), -1);
    expect(atom_expr(arena, NULL, 0u), atom_symbol(arena, "Zebra"), -1);
    expect(atom_expr(arena, NULL, 0u), atom_symbol(arena, "[]"), -1);
    Atom *duplicate = atom_int(arena, 1);
    Atom *sorted = petta_semantics_msort(arena,
        atom_expr3(arena, integer, floating, duplicate));
    assert(sorted && sorted->expr.len == 3u);
    assert(sorted->expr.elems[0] == floating);
    assert(sorted->expr.elems[1] == integer);
    assert(sorted->expr.elems[2] == duplicate);
}

static void shared_case(size_t depth) {
    Atom *a = calloc(depth + 1u, sizeof(*a));
    Atom *b = calloc(depth + 1u, sizeof(*b));
    Atom **edges_a = calloc(2u * (depth + 1u), sizeof(*edges_a));
    Atom **edges_b = calloc(2u * (depth + 1u), sizeof(*edges_b));
    assert(a && b && edges_a && edges_b);
    a[0].kind = b[0].kind = ATOM_GROUNDED;
    a[0].ground.gkind = b[0].ground.gkind = GV_INT;
    a[0].ground.ival = b[0].ground.ival = 7;
    for (size_t i = 1u; i <= depth; i++) {
        a[i].kind = b[i].kind = ATOM_EXPR;
        a[i].expr.len = b[i].expr.len = 2u;
        a[i].expr.elems = &edges_a[2u*i];
        b[i].expr.elems = &edges_b[2u*i];
        edges_a[2u*i] = edges_a[2u*i+1u] = &a[i-1u];
        edges_b[2u*i] = edges_b[2u*i+1u] = &b[i-1u];
    }
    test_runtime_stats_reset_counters();
    int order = 99;
    assert(petta_semantics_term_compare(&a[depth], &b[depth], &order) && order == 0);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_ORDER_PAIR_VISIT) <= 4u*depth+8u);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_ORDER_FRONTIER_PEAK) == depth);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_ORDER_EQUAL_REUSE) == depth-1u);
    test_runtime_stats_reset_counters();
    assert(term_graph_value_compare(&a[depth], &b[depth]) == CETTA_TERM_ORDER_EQUAL);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_ORDER_PRODUCT_PAIR) <= 4u*depth+1u);
    assert(test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_ORDER_PRODUCT_EDGE) <= 4u*depth);
    b[0].ground.ival = 8;
    expect(&a[depth], &b[depth], -1);
    free(edges_b);
    free(edges_a);
    free(b);
    free(a);
}

static void active_pair_is_not_equal(void) {
    Atom a = {.kind = ATOM_EXPR}, b = {.kind = ATOM_EXPR};
    Atom zero = {.kind = ATOM_GROUNDED}, one = {.kind = ATOM_GROUNDED};
    zero.ground.gkind = one.ground.gkind = GV_INT;
    one.ground.ival = 1;
    Atom *a_children[] = {&a, &zero};
    Atom *b_children[] = {&b, &one};
    a.expr.len = b.expr.len = 2u;
    a.expr.elems = a_children;
    b.expr.elems = b_children;
    int order = 99;
    assert(!petta_semantics_term_compare(&a, &b, &order));
    assert(!petta_semantics_term_compare(&b, &a, &order));
    expect(&a, &a, 0);
    assert(term_graph_value_compare(&a, &b) ==
           CETTA_TERM_ORDER_INCOMPARABLE);
}

static Atom *cyclic_value(Arena *arena, bool unrolled, int leaf) {
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    uint32_t root = term_graph_add_compound(graph, symbol_intern_cstr(g_symbols, "f"), 2u);
    uint32_t number = term_graph_add_leaf(graph, atom_int(arena, leaf));
    uint32_t inner = unrolled
        ? term_graph_add_compound(graph, symbol_intern_cstr(g_symbols, "f"), 2u) : root;
    assert(term_graph_set_child(graph, root, 0u, number));
    assert(term_graph_set_child(graph, root, 1u, inner));
    if (unrolled) {
        assert(term_graph_set_child(graph, inner, 0u, number));
        assert(term_graph_set_child(graph, inner, 1u, root));
    }
    assert(term_graph_seal(graph));
    Atom *value = term_graph_node_value(arena, graph, root, NULL);
    assert(value);
    term_graph_release(graph);
    return value;
}

static void rational_cases(Arena *arena) {
    Atom *a = cyclic_value(arena, false, 1);
    Atom *b = cyclic_value(arena, true, 1);
    Atom *c = cyclic_value(arena, false, 2);
    {
        assert(term_graph_value_compare(a, b) == CETTA_TERM_ORDER_EQUAL);
        assert(term_graph_value_compare(a, c) == CETTA_TERM_ORDER_LESS);
        assert(term_graph_value_compare(c, b) == CETTA_TERM_ORDER_GREATER);
    }
    Atom *zero = atom_int(arena, 0), *one = atom_int(arena, 1);
    assert(term_graph_value_compare(atom_expr2(arena, a, zero),
        atom_expr2(arena, b, one)) == CETTA_TERM_ORDER_LESS);

    CettaTermGraph *crossed = term_graph_new();
    assert(crossed);
    SymbolId name = symbol_intern_cstr(g_symbols, "s");
    uint32_t x = term_graph_add_compound(crossed, name, 2u);
    uint32_t y = term_graph_add_compound(crossed, name, 2u);
    uint32_t z = term_graph_add_leaf(crossed, zero);
    uint32_t o = term_graph_add_leaf(crossed, one);
    assert(term_graph_set_child(crossed, x, 0u, y));
    assert(term_graph_set_child(crossed, x, 1u, z));
    assert(term_graph_set_child(crossed, y, 0u, x));
    assert(term_graph_set_child(crossed, y, 1u, o));
    assert(term_graph_seal(crossed));
    Atom *cx = term_graph_node_value(arena, crossed, x, NULL);
    Atom *cy = term_graph_node_value(arena, crossed, y, NULL);
    assert(term_graph_value_compare(cx, cy) ==
           CETTA_TERM_ORDER_INCOMPARABLE);
    assert(term_graph_value_compare(cy, cx) ==
           CETTA_TERM_ORDER_INCOMPARABLE);
    term_graph_release(crossed);

    Atom *deep_a = atom_expr2(arena, atom_expr2(arena, atom_int(arena, 9), zero), zero);
    Atom *deep_b = atom_expr2(arena, atom_expr2(arena, one, zero), zero);
    Atom *left = atom_expr2(arena, deep_a, zero);
    Atom *right = atom_expr2(arena, deep_b, atom_int(arena, 8));
    assert(term_graph_value_compare(left, right) ==
           CETTA_TERM_ORDER_GREATER);
}

static void carrier_environments(Arena *arena) {
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    uint32_t root = term_graph_add_compound(graph, symbol_intern_cstr(g_symbols, "f"), 2u);
    uint32_t parameter = term_graph_add_param(graph, 0u);
    assert(term_graph_set_child(graph, root, 0u, parameter));
    assert(term_graph_set_child(graph, root, 1u, root));
    assert(term_graph_seal(graph));
    Atom *zero[] = {atom_int(arena, 0)}, *one[] = {atom_int(arena, 1)};
    Atom *a = term_graph_node_value(arena, graph, root, zero);
    Atom *b = term_graph_node_value(arena, graph, root, one);
    Atom *left = atom_expr2(arena, a, a);
    Atom *right = atom_expr2(arena, a, b);
    {
        assert(term_graph_value_compare(left, right) == CETTA_TERM_ORDER_LESS);
        assert(term_graph_value_compare(right, left) == CETTA_TERM_ORDER_GREATER);
        /* An unbound graph node is not a closed value, even compared to itself. */
        Atom *unbound = atom_term_graph(arena, graph, parameter);
        assert(term_graph_value_compare(unbound, unbound) == CETTA_TERM_ORDER_INVALID);
    }
    term_graph_release(graph);
}

static void rational_order_laws(Arena *arena) {
    enum { COUNT = 48 };
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    for (unsigned i = 0u; i < COUNT; i++) {
        uint32_t node = i % 5u == 0u
            ? term_graph_add_leaf(graph, atom_int(arena, i % 3u))
            : term_graph_add_compound(graph, symbol_intern_cstr(g_symbols,
                i % 3u == 0u ? "f" : "g"), 2u);
        assert(node == i);
    }
    for (unsigned i = 0u; i < COUNT; i++) {
        if (i % 5u) {
            assert(term_graph_set_child(graph, i, 0u, next_random() % COUNT));
            assert(term_graph_set_child(graph, i, 1u, next_random() % COUNT));
        }
    }
    assert(term_graph_seal(graph));
    Atom *values[COUNT];
    for (unsigned i = 0u; i < COUNT; i++)
        values[i] = term_graph_node_value(arena, graph, i, NULL);
    for (unsigned i = 0u; i < 1200u; i++) {
        Atom *a = values[next_random() % COUNT];
        Atom *b = values[next_random() % COUNT];
        Atom *c = values[next_random() % COUNT];
        int ab = term_graph_value_compare(a, b);
        int ba = term_graph_value_compare(b, a);
        int bc = term_graph_value_compare(b, c);
        int ac = term_graph_value_compare(a, c);
        assert(ab >= -1 && ab <= 2);
        assert(ab == 2 ? ba == 2 : ab == -ba);
        if (ab <= 0 && bc <= 0)
            assert(ac <= 0);
        assert((ab == 0) == term_graph_value_eq(a, b, atom_eq));
    }
    term_graph_release(graph);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
    expression_boundary_cases(&arena);
    finite_cases(&arena);
    shared_case(24u);
    shared_case(100000u);
    active_pair_is_not_equal();
    rational_cases(&arena);
    carrier_environments(&arena);
    rational_order_laws(&arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: finite and rational term orders preserve sharing, depth, equality and cyclic policy");
    return 0;
}
