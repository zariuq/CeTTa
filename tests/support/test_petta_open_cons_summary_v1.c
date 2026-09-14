#include "atom.h"
#include "petta_semantics.h"
#include "symbol.h"
#include <stdio.h>

static unsigned passed, failed;
static void check(bool condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
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
    Atom *box = atom_symbol(&arena, "Box");
    Atom *item = atom_int(&arena, 7);
    Atom *empty = atom_expr(&arena, NULL, 0u);
    Atom *tag = atom_internal_tag(&arena, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
    Atom *closed = atom_expr3(&arena, tag, item, empty);
    Atom *expected = atom_expr(&arena, &item, 1u);
    Atom *plain = atom_expr3(&arena, box, item, empty);
    check(!atom_structural_may_have_internal_tag(plain), "ordinary data has certified absence");
    check(petta_semantics_flatten_closed_open_cons(&arena, plain) == plain,
          "ordinary data is returned unchanged");
    check(atom_structural_may_have_internal_tag(closed), "closed private cons retains its tag");
    check(atom_eq(petta_semantics_flatten_closed_open_cons(&arena, closed), expected),
          "closed private cons becomes an ordinary list");
    Atom *nested = atom_expr3(&arena, box, closed, closed);
    Atom *nested_expected = atom_expr3(&arena, box, expected, expected);
    check(atom_eq(petta_semantics_flatten_closed_open_cons(&arena, nested), nested_expected),
          "nested shared private lists are normalized at both occurrences");
    Atom unknown = *nested;
    unknown.structural_facts = 0u;
    check(atom_structural_may_have_internal_tag(&unknown), "unknown metadata requests traversal");
    check(atom_eq(petta_semantics_flatten_closed_open_cons(&arena, &unknown), nested_expected),
          "unknown metadata cannot conceal a nested private list");
    Atom *unknown_parent = atom_expr2(&arena, box, &unknown);
    check(atom_structural_may_have_internal_tag(unknown_parent), "unknown child poisons absence proof");
    check(atom_eq(petta_semantics_flatten_closed_open_cons(&arena, unknown_parent),
                  atom_expr2(&arena, box, nested_expected)), "unknown child is still traversed");
    Atom *malformed = atom_expr2(&arena, tag, item);
    check(petta_semantics_flatten_closed_open_cons(&arena, malformed) == malformed,
          "wrong-arity tagged expression is not flattened");
    Atom *other_tag = atom_internal_tag(&arena, CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND);
    Atom *other = atom_expr3(&arena, other_tag, item, empty);
    check(petta_semantics_flatten_closed_open_cons(&arena, other) == other,
          "a different internal tag is not an open cons");
    Atom *variable = atom_var(&arena, "$tail");
    Atom *open = atom_expr3(&arena, tag, item, variable);
    check(petta_semantics_flatten_closed_open_cons(&arena, open) == open,
          "an unbound tail cannot be flattened into a closed list");
    check(petta_semantics_flatten_closed_open_cons(NULL, closed) == closed,
          "an absent allocation arena leaves the value unchanged");
    check(petta_semantics_flatten_closed_open_cons(&arena, variable) == variable,
          "a variable is not treated as an expression to flatten");
    Atom *copied = atom_deep_copy(&survivor, nested);
    check(atom_structural_may_have_internal_tag(copied) &&
          atom_eq(petta_semantics_flatten_closed_open_cons(&survivor, copied), nested_expected),
          "survivor copy retains tagged-list normalization");
    Atom *deep = plain;
    for (unsigned depth = 0u; depth < 16384u; ++depth)
        deep = atom_expr2(&arena, box, deep);
    check(!atom_structural_may_have_internal_tag(deep) &&
          petta_semantics_flatten_closed_open_cons(&arena, deep) == deep,
          "deep tag-free data does not require recursive normalization");
    arena_free(&survivor);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(PeTTaOpenConsSummaryV1 %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
