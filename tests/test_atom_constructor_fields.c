/*
 * Every atom constructor writes every field of the atom it returns.
 *
 * Properties under test, on arena memory that still holds an earlier
 * allocation's bytes:
 *   - a grounded leaf's name_key is NULL (only a variable carries one; a
 *     collection reads the field as its mark and as its forwarding copy);
 *   - its hash cache is clear and its arena is the one it was built in;
 *   - a variable built with a name key keeps it (the positive control);
 *   - the structural fact for NotReducible holds of an atom exactly when the
 *     symbol occurs in it: the symbol, its canonical atom, an expression and
 *     a suffix view that keep it, and none that lose it or never had it.
 */
#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

static void check(bool condition, const char *what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

/* The next allocations reuse memory filled with the given byte. */
static void dirty(Arena *a, ArenaMark mark, unsigned char byte) {
    arena_reset(a, mark);
    void *used = arena_alloc(a, 4096u);
    memset(used, byte, 4096u);
    arena_reset(a, mark);
}

static void check_leaf(Atom *atom, Arena *a, const char *what) {
    char text[128];
    snprintf(text, sizeof text, "%s: name_key is NULL", what);
    check(atom && atom->name_key == NULL, text);
    snprintf(text, sizeof text, "%s: hash cache is clear", what);
    check(atom && atom->hash_cache == 0u &&
              (atom->flags & ATOM_FLAG_HASH_VALID) == 0u, text);
    snprintf(text, sizeof text, "%s: arena identity", what);
    check(atom && atom->arena_id == a->identity, text);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    VarInternTable var_intern;
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;

    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    ArenaMark mark = arena_mark(&arena);
    static int payload;
    /* A foreign record begins with its hold; this one asks for none. */
    static CettaForeignHold foreign_record;

    for (unsigned pass = 0u; pass < 2u; pass++) {
        unsigned char byte = pass ? 0xffu : 0xbeu;
        dirty(&arena, mark, byte);
        check_leaf(atom_space(&arena, &payload), &arena, "space");
        dirty(&arena, mark, byte);
        check_leaf(atom_state(&arena, (StateCell *)&payload), &arena, "state");
        dirty(&arena, mark, byte);
        check_leaf(atom_capture(&arena, (CaptureClosure *)&payload), &arena,
                   "capture");
        dirty(&arena, mark, byte);
        check_leaf(atom_foreign(&arena, (CettaForeignValue *)&foreign_record),
                   &arena, "foreign");
        dirty(&arena, mark, byte);
        check_leaf(atom_bigint(&arena, "123456789012345678901234567890"),
                   &arena, "big integer from text");
        dirty(&arena, mark, byte);
        check_leaf(atom_float(&arena, 2.5), &arena, "float");
        dirty(&arena, mark, byte);
        check_leaf(atom_bool(&arena, true), &arena, "bool");
        dirty(&arena, mark, byte);
        check_leaf(atom_string_n(&arena, "text", 4u), &arena, "string");
#if CETTA_BUILD_WITH_GMP
        mpz_t big;
        mpz_init_set_str(big, "123456789012345678901234567890", 10);
        dirty(&arena, mark, byte);
        check_leaf(atom_bigint_from_mpz(&arena, big), &arena,
                   "big integer from mpz");
        dirty(&arena, mark, byte);
        check_leaf(atom_bigint_take_mpz(&arena, big), &arena,
                   "big integer taking mpz");
        mpq_t ratio;
        mpq_init(ratio);
        mpq_set_str(ratio, "2/3", 10);
        dirty(&arena, mark, byte);
        check_leaf(atom_rational_from_mpq(&arena, ratio), &arena, "rational");
        mpq_clear(ratio);
#endif
    }

    /* NotReducible's structural fact, its positive and negative cases. */
    arena_reset(&arena, mark);
    Atom *marker = atom_symbol_id(&arena, g_builtin_syms.not_reducible);
    Atom *other = atom_symbol(&arena, "Reducible");
    Atom *head = atom_symbol(&arena, "f");
    check(atom_structural_may_have_not_reducible(marker),
          "NotReducible holds NotReducible");
    check(atom_structural_may_have_not_reducible(
              atom_symbol_canonical(g_builtin_syms.not_reducible)),
          "the canonical NotReducible holds NotReducible");
    check(!atom_structural_may_have_not_reducible(other),
          "another symbol holds no NotReducible");
    check(!atom_structural_may_have_not_reducible(
              atom_var(&arena, "NotReducible")),
          "a variable spelled NotReducible holds no NotReducible");
    Atom *holding = atom_expr(&arena, (Atom *[]){head, marker, other}, 3u);
    Atom *free_of = atom_expr(&arena, (Atom *[]){head, other, other}, 3u);
    check(atom_structural_may_have_not_reducible(holding),
          "an expression with NotReducible holds it");
    check(atom_structural_may_have_not_reducible(
              atom_expr(&arena, (Atom *[]){head, holding}, 2u)),
          "an expression holds what its children hold");
    check(!atom_structural_may_have_not_reducible(free_of),
          "an expression without NotReducible holds none");
    check(atom_structural_may_have_not_reducible(
              atom_expr_suffix(&arena, holding, 1u)),
          "a suffix that keeps NotReducible holds it");
    check(!atom_structural_may_have_not_reducible(
              atom_expr_suffix(&arena, holding, 2u)),
          "a suffix past NotReducible holds none");

    /* The positive control: a variable's name key is kept. */
    dirty(&arena, mark, 0xbeu);
    Atom *key = atom_symbol(&arena, "spelling");
    Atom *variable = atom_var_with_name_key(&arena, key, 7u);
    check(variable && variable->name_key == key,
          "a variable keeps its name key");

    arena_free(&arena);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);

    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: atom constructors write every field, NotReducible's fact "
           "among them (%d checks)\n", g_checks);
    return 0;
}
