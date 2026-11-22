#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "subst_tree.h"
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

/* The substitution tree's variable edges are slots, numbered by a row's
 * first occurrences.  Rows that differ only in their variables' names share
 * one path.  A query may name a stored row's own variables and still meets
 * the row (the walk keeps no row variable).  A slot met again must meet a
 * subterm that unifies with the one its first occurrence met: a walk that
 * ignored repeated slots would still select every row that unifies, and the
 * checks below of rows that do not unify tell it apart. */

static Arena g_arena;
static int g_failures;

static Atom *sym(const char *name) { return atom_symbol(&g_arena, name); }
static Atom *var(const char *name) { return atom_var(&g_arena, name); }
static Atom *num(int64_t value) { return atom_int(&g_arena, value); }
static Atom *flo(double value) { return atom_float(&g_arena, value); }
static Atom *ex(int n, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_expr(&g_arena, elems, (CettaExprLen)n);
}

static size_t node_count(const SubstNode *n) {
    if (!n)
        return 0u;
    size_t count = 1u;
    if (n->sym_hashed) {
        for (uint32_t i = 0u; i <= n->sym_ht.mask; i++)
            if (n->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                count += node_count(n->sym_ht.entries[i].child);
    } else {
        for (uint32_t i = 0u; i < n->nsym; i++)
            count += node_count(n->sym[i].child);
    }
    for (uint32_t i = 0u; i < n->nslots; i++)
        count += node_count(n->slots[i].child);
    for (uint32_t i = 0u; i < n->nexpr; i++)
        count += node_count(n->expr[i].child);
    if (n->int_hashed) {
        for (uint32_t i = 0u; i <= n->int_ht.mask; i++)
            if (n->int_ht.entries[i].child)
                count += node_count(n->int_ht.entries[i].child);
    } else {
        for (uint32_t i = 0u; i < n->nints; i++)
            count += node_count(n->ints[i].child);
    }
    return count;
}

/* Whether QUERY selects the row at POSITION from BUCKET. */
static bool selects(SubstBucket *bucket, Atom *query, CettaIndex position) {
    CettaIndex *rows = NULL, nrows = 0u, crows = 0u;
    stree_query_bucket(bucket, &g_arena, query, &rows, &nrows, &crows);
    bool found = false;
    for (CettaIndex i = 0u; i < nrows; i++)
        found = found || rows[i] == position;
    free(rows);
    return found;
}

static void expect(bool ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

static void check_selection(Atom *row, Atom *query, bool selected,
                            const char *what) {
    SubstBucket bucket;
    stree_bucket_init(&bucket);
    stree_bucket_insert(&bucket, row, 7u);
    expect(selects(&bucket, query, 7u) == selected, what);
    stree_bucket_free(&bucket);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    arena_init(&g_arena);

    /* Sharing: 40 rows (q $xI $yI $zI rI), each with its own variables,
     * file under one path of slots as rows that share their names do. */
    enum { ROWS = 40 };
    SubstBucket own, shared;
    stree_bucket_init(&own);
    stree_bucket_init(&shared);
    for (int i = 0; i < ROWS; i++) {
        char x[16], y[16], z[16], r[16];
        snprintf(x, sizeof x, "x%d", i);
        snprintf(y, sizeof y, "y%d", i);
        snprintf(z, sizeof z, "z%d", i);
        snprintf(r, sizeof r, "r%d", i);
        stree_bucket_insert(&own, ex(5, sym("q"), var(x), var(y), var(z), sym(r)),
                            (CettaIndex)i);
        stree_bucket_insert(&shared, ex(5, sym("q"), var("x"), var("y"), var("z"),
                                        sym(r)), (CettaIndex)i);
    }
    /* root, arity 5, q, three new slots, then one node per row's symbol */
    expect(node_count(own.root) == 6u + ROWS,
           "rows that differ in their variables' names share a path");
    expect(node_count(own.root) == node_count(shared.root),
           "the rows' names do not change the tree");
    stree_bucket_free(&own);
    stree_bucket_free(&shared);

    /* (q $x $y $x) and (q $a $b $a) are one path; (q $x $y $y) another. */
    SubstBucket repeats;
    stree_bucket_init(&repeats);
    stree_bucket_insert(&repeats, ex(4, sym("q"), var("x"), var("y"), var("x")), 0u);
    stree_bucket_insert(&repeats, ex(4, sym("q"), var("a"), var("b"), var("a")), 1u);
    stree_bucket_insert(&repeats, ex(4, sym("q"), var("x"), var("y"), var("y")), 2u);
    expect(node_count(repeats.root) == 7u,
           "a repeated slot is part of the path, its first occurrence's number");
    stree_bucket_free(&repeats);

    /* D2: a query that names the row's own variables still meets the row. */
    check_selection(ex(5, sym("q"), var("v"), var("w"), var("w"), sym("m")),
                    ex(5, sym("q"), sym("kc"), ex(2, sym("fz"), var("v")),
                       ex(2, sym("fz"), sym("kb")), var("m")),
                    true, "a query naming the row's variables meets the row");
    check_selection(ex(3, sym("fq"), var("u"),
                       ex(4, sym("h2"), var("v"), var("w"), var("w"))),
                    ex(3, sym("fq"), var("o"),
                       ex(4, sym("h2"), sym("kc"), ex(2, sym("fz"), var("v")),
                          ex(2, sym("fz"), sym("kb")))),
                    true, "an equation's left side meets a call that names its variables");
    check_selection(ex(3, sym("p"), var("v"), var("v")),
                    ex(3, sym("p"), var("v"), ex(2, sym("f"), var("v"))),
                    true, "the row's variable met as a query variable is no binding");

    /* A slot met again must meet a subterm that unifies with the first. */
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), sym("a"), sym("a")), true, "(p a a) meets (p $x $x)");
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), sym("a"), sym("b")), false, "(p a b) misses (p $x $x)");
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), var("q"), sym("b")), true, "(p $q b) meets (p $x $x)");
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), ex(2, sym("f"), var("z")), ex(2, sym("f"), sym("c"))),
                    true, "(p (f $z) (f c)) meets (p $x $x)");
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), ex(2, sym("f"), sym("a")), ex(2, sym("f"), sym("c"))),
                    false, "(p (f a) (f c)) misses (p $x $x)");
    check_selection(ex(4, sym("p"), var("x"), var("x"), var("x")),
                    ex(4, sym("p"), var("q"), sym("a"), sym("b")),
                    false, "a third occurrence is compared too");
    check_selection(ex(3, sym("p"), var("x"), var("x")),
                    ex(3, sym("p"), num(1), num(2)), false, "(p 1 2) misses (p $x $x)");

    /* A first occurrence under a query variable met nothing: the slot
     * keeps the next subterm it meets, and later ones are compared. */
    check_selection(ex(3, sym("r"), ex(2, sym("g"), var("x")), var("x")),
                    ex(3, sym("r"), var("q"), sym("a")), true,
                    "a slot first met under a query variable is free");
    check_selection(ex(3, sym("r"), ex(2, sym("g"), var("x")), var("x")),
                    ex(3, sym("r"), ex(2, sym("g"), sym("b")), sym("a")), false,
                    "(r (g b) a) misses (r (g $x) $x)");
    check_selection(ex(4, sym("r"), ex(2, sym("g"), var("x")), var("x"), var("x")),
                    ex(4, sym("r"), var("q"), sym("a"), sym("b")), false,
                    "a slot first met under a query variable compares its later meetings");

    /* A row with more slots than the walk keeps inline: the slot 17 met
     * again is compared after the table has grown. */
    {
        Atom *row_items[12], *hit_items[12], *miss_items[12];
        Atom *row_args[12], *hit_args[12], *miss_args[12];
        int n = 0;
        row_items[n] = hit_items[n] = miss_items[n] = sym("w");
        n++;
        /* (w (t $v0 .. $v4) (t $v5 .. $v9) (t $v10 .. $v14) (t $v15 .. $v19) $v17) */
        for (int g = 0; g < 4; g++) {
            for (int j = 0; j < 5; j++) {
                char name[8], value[8];
                snprintf(name, sizeof name, "v%d", g * 5 + j);
                snprintf(value, sizeof value, "a%d", g * 5 + j);
                row_args[j] = var(name);
                hit_args[j] = sym(value);
                miss_args[j] = sym(value);
            }
            row_items[n] = ex(6, sym("t"), row_args[0], row_args[1], row_args[2],
                              row_args[3], row_args[4]);
            hit_items[n] = ex(6, sym("t"), hit_args[0], hit_args[1], hit_args[2],
                              hit_args[3], hit_args[4]);
            miss_items[n] = ex(6, sym("t"), miss_args[0], miss_args[1], miss_args[2],
                               miss_args[3], miss_args[4]);
            n++;
        }
        row_items[n] = var("v17");
        hit_items[n] = sym("a17");
        miss_items[n] = sym("b");
        n++;
        Atom *row = atom_expr(&g_arena, row_items, (CettaExprLen)n);
        check_selection(row, atom_expr(&g_arena, hit_items, (CettaExprLen)n), true,
                        "the slot 17 met again agrees past the inline table");
        check_selection(row, atom_expr(&g_arena, miss_items, (CettaExprLen)n), false,
                        "the slot 17 met again is compared past the inline table");
    }

    /* Placeholders bind nothing: two values the tree does not discriminate
     * do not constrain each other. */
    check_selection(ex(3, sym("s"), flo(1.5), flo(2.5)),
                    ex(3, sym("s"), flo(1.5), flo(2.5)), true,
                    "two placeholders meet two different values");
    check_selection(ex(3, sym("s"), flo(1.5), flo(2.5)),
                    ex(3, sym("s"), var("a"), var("b")), true,
                    "placeholders meet query variables");

    if (g_failures) {
        printf("FAIL: %d substitution-tree slot checks\n", g_failures);
        return 1;
    }
    printf("PASS: substitution-tree slots share paths, keep a query's names apart "
           "from rows and compare repeated slots\n");
    return 0;
}
