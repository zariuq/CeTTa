#include "atom.h"
#include "match.h"
#include "symbol.h"
#include <stdio.h>

static unsigned passed, failed;
static void check(bool condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}

static bool renamed_pair(Atom *result, Atom *x, Atom *y) {
    return result && result->kind == ATOM_EXPR && result->expr.len == 2u &&
        result->expr.elems[0]->kind == ATOM_VAR &&
        result->expr.elems[0]->var_id != x->var_id &&
        result->expr.elems[1]->var_id == y->var_id;
}

int __wrap_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena, survivor;
    arena_init(&arena);
    arena_init(&survivor);
    Atom *x = atom_var(&arena, "$x");
    Atom *y = atom_var(&arena, "$y");
    Atom *z = atom_var(&arena, "$z");
    Atom *pair = atom_expr2(&arena, x, y);
    Atom *box = atom_symbol(&arena, "Box");
    Atom *ground = atom_expr2(&arena, box, atom_int(&arena, 7));
    check(atom_eq(rename_vars_only(&arena, pair, ground), pair),
          "ground selection leaves both variables unchanged");
    check(renamed_pair(rename_vars_only(&arena, pair, x), x, y),
          "one selected variable leaves the other identity unchanged");

    Atom *deep = x;
    for (unsigned i = 0u; i < 16384u; ++i)
        deep = atom_expr2(&arena, box, deep);
    check(renamed_pair(rename_vars_only(&arena, pair, deep), x, y),
          "deep singleton support selects exactly its variable");
    Atom *duplicated = atom_expr2(&arena, deep, deep);
    Atom *xx = rename_vars_only(&arena, atom_expr2(&arena, x, x), duplicated);
    check(xx && xx->kind == ATOM_EXPR &&
          xx->expr.elems[0]->var_id == xx->expr.elems[1]->var_id &&
          xx->expr.elems[0]->var_id != x->var_id,
          "repeated occurrences receive one fresh identity");

    Atom *dag = x;
    for (unsigned i = 0u; i < 20u; ++i)
        dag = atom_expr2(&arena, dag, dag);
    check(renamed_pair(rename_vars_only(&arena, pair, dag), x, y),
          "shared singleton DAG selects exactly its variable");
    Atom *mixed = atom_expr3(&arena, duplicated, y, duplicated);
    Atom *xyz = atom_expr3(&arena, x, y, z);
    Atom *renamed = rename_vars_only(&arena, xyz, mixed);
    check(renamed && renamed->kind == ATOM_EXPR &&
          renamed->expr.elems[0]->var_id != x->var_id &&
          renamed->expr.elems[1]->var_id != y->var_id &&
          renamed->expr.elems[2]->var_id == z->var_id &&
          renamed->expr.elems[0]->var_id != renamed->expr.elems[1]->var_id,
          "multi-variable support is not confused with singleton support");
    Atom *except = rename_vars_except(&arena, pair, duplicated);
    check(except && except->kind == ATOM_EXPR &&
          except->expr.elems[0]->var_id == x->var_id &&
          except->expr.elems[1]->var_id != y->var_id,
          "the complementary renaming operation preserves selected identities");

    Atom *tag = atom_internal_tag(&arena, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
    Atom *unstable = atom_expr2(&arena, tag, duplicated);
    check((unstable->flags & ATOM_FLAG_HASH_STABLE) == 0u &&
          renamed_pair(rename_vars_only(&arena, pair, unstable), x, y),
          "stable singleton children work under a non-hash-stable parent");
    Atom *copied = atom_deep_copy(&survivor, duplicated);
    check(renamed_pair(rename_vars_only(&arena, pair, copied), x, y),
          "survivor copying preserves the selected variable identity");

    /* Unpublished cyclic input must still take the cycle-detecting walk,
     * even when a non-authoritative singleton field happens to be present. */
    Atom *cyclic = atom_expr2(&arena, x, x);
    cyclic->flags = ATOM_FLAG_HAS_VARS;
    cyclic->var_id = x->var_id;
    cyclic->expr.elems[1] = cyclic;
    check(rename_vars_only(&arena, pair, cyclic) == NULL,
          "cyclic non-stable input is refused despite a singleton field");
    check(rename_vars_except(&arena, pair, cyclic) == NULL,
          "complementary renaming also refuses cyclic selection");

    Bindings source, projected;
    bindings_init(&source);
    check(bindings_add_var(&source, z, atom_int(&arena, 8)) &&
          bindings_add_var(&source, x, y) &&
          bindings_add_var(&source, y, atom_int(&arena, 7)),
          "independent binding chain is admitted");
    Atom *roots[] = { duplicated };
    bool ok = bindings_project_reachable(&source, roots, 1u, &projected);
    check(ok && projected.len == 2u &&
          projected.entries[0].var_id == x->var_id &&
          projected.entries[1].var_id == y->var_id,
          "projection retains the transitive chain in source binding order");
    if (ok) bindings_free(&projected);
    roots[0] = ground;
    ok = bindings_project_reachable(&source, roots, 1u, &projected);
    check(ok && projected.len == 0u,
          "ground roots retain no unrelated bindings");
    if (ok) bindings_free(&projected);
    bindings_free(&source);
    arena_free(&survivor);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(VariableSupportCollectionV1 %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
