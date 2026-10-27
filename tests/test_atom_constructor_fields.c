/*
 * Every atom constructor writes every field of the atom it returns.
 *
 * Properties under test, on arena memory that still holds an earlier
 * allocation's bytes:
 *   - a grounded leaf's name_key is NULL (only a variable carries one; a
 *     collection reads the field as its mark and as its forwarding copy);
 *   - its hash cache is clear and its arena is the one it was built in;
 *   - a variable built with a name key keeps it (the positive control);
 *   - when the hash-cons table can hold no more, a constructor answers an
 *     atom of its own arena, never its temporary header, and a table with
 *     room still shares its representative (cetta-ours a4ab5acd).
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
    check(atom && atom_hash_cache_load(atom) == 0u, text);
    snprintf(text, sizeof text, "%s: arena identity", what);
    check(atom && atom->arena_id == a->identity, text);
}

static void check_expression_assembly(void) {
    Arena arena, other;
    arena_init_detached(&arena);
    arena_init_detached(&other);
    Atom *one = atom_var(&arena, "one");
    Atom *two = atom_var(&other, "two");
    Atom *children[] = {atom_symbol(&arena, "head"), one, one, two,
                        atom_int(&arena, 9), atom_symbol(&other, "foreign")};
    for (unsigned length = 0u; length <= 6u; length++) {
        AtomExprAssembly *assembly = atom_expr_assembly_new(&arena, length);
        check(assembly != NULL, "assembly opens without publishing a draft");
        for (unsigned i = 0u; i < length; i++) {
            check(!atom_expr_assembly_finish(assembly),
                  "unfinished assembly has no result");
            check(atom_expr_assembly_append(assembly, children[i]),
                  "one append accepts exactly the next child");
        }
        Atom *expected = atom_expr_builder_begin(&arena, length);
        for (unsigned i = 0u; i < length; i++) expected->expr.elems[i] = children[i];
        expected = atom_expr_builder_finish(&arena, expected);
        Atom *actual = atom_expr_assembly_finish(assembly);
        check(actual && atom_eq(actual, expected) && actual->flags == expected->flags &&
              actual->structural_facts == expected->structural_facts &&
              var_id_eq(actual->var_id, expected->var_id),
              "incremental assembly agrees with the ordinary summary and order");
        check(!atom_expr_assembly_append(assembly, one) &&
              atom_expr_assembly_finish(assembly) == actual,
              "published assembly is immutable and repeat finish is stable");
        atom_expr_assembly_free(assembly);
    }
    AtomExprAssembly *cancel = atom_expr_assembly_new(&arena, 4096u);
    check(cancel && atom_expr_assembly_append(cancel, one) && !atom_expr_assembly_finish(cancel),
          "missing children cannot publish a wide draft");
    atom_expr_assembly_free(cancel);
    cancel = atom_expr_assembly_new(&arena, 1u);
    check(cancel && !atom_expr_assembly_append(cancel, NULL) &&
          !atom_expr_assembly_append(cancel, one) && !atom_expr_assembly_finish(cancel),
          "a refused null child cannot be hidden by later valid input");
    atom_expr_assembly_free(cancel);
    cancel = atom_expr_assembly_new(&arena, 1u);
    check(cancel && atom_expr_assembly_append(cancel, one) &&
          !atom_expr_assembly_append(cancel, two) && !atom_expr_assembly_finish(cancel),
          "excess children refuse before publication rather than truncating");
    atom_expr_assembly_free(cancel);
    ArenaMark mark = arena_mark(&arena);
    cancel = atom_expr_assembly_new(&arena, 1u);
    check(cancel && atom_expr_assembly_append(cancel, one), "reset control opens");
    arena_reset(&arena, mark);
    check(!atom_expr_assembly_finish(cancel) && !atom_expr_assembly_append(cancel, one),
          "arena reset refuses before touching the discarded draft");
    atom_expr_assembly_free(cancel);
    arena_set_older_generation(&arena, &other);
    Atom *special_heads[] = {atom_symbol(&arena, "resolve-name"),
                            atom_symbol_id(&arena, g_builtin_syms.native_handle)};
    for (unsigned i = 0u; i < 2u; i++) {
        AtomExprAssembly *assembly = atom_expr_assembly_new(&arena, 2u);
        check(assembly && atom_expr_assembly_append(assembly, special_heads[i]) &&
              atom_expr_assembly_append(assembly, two),
              "special-head assembly accepts an older-generation child");
        Atom *actual = atom_expr_assembly_finish(assembly);
        Atom *expected = atom_expr2(&arena, special_heads[i], two);
        check(actual && expected && actual->flags == expected->flags &&
              actual->structural_facts == expected->structural_facts &&
              var_id_eq(actual->var_id, expected->var_id),
              "head adjustments and generation closure agree with the ordinary constructor");
        atom_expr_assembly_free(assembly);
    }
    arena_free(&other);
    arena_free(&arena);
}

static void check_full_hashcons_fallback(Arena *arena) {
    HashConsTable hc;
    hashcons_init_compact(&hc);
    /* Fill every slot with a borrowed atom, so the table holds no more
     * without growing; the fixtures are cleared before the table is freed. */
    Atom occupied;
    atom_scalar_leaf_bool(&occupied, true);
    for (uint32_t i = 0; i < hc.size; ++i)
        hc.table[i] = &occupied;
    hc.used = hc.size;
    arena_set_hashcons(arena, &hc);
    check(!atom_expr_assembly_new(arena, 1u),
          "retained assembly refuses an interning arena");
    Atom *fallback = atom_bool(arena, false);
    bool owned = arena_owns_ptr(arena, fallback);
    check(owned, "a full hashcons table falls back to an arena-owned atom");
    if (owned) {
        check(fallback->arena_id == arena->identity && fallback->name_key == NULL,
              "the fallback keeps its arena identity and leaf fields");
        check(fallback->kind == ATOM_GROUNDED &&
                  fallback->ground.gkind == GV_BOOL && !fallback->ground.bval,
              "the fallback preserves the requested value");
    }
    for (uint32_t i = 0; i < hc.size; ++i)
        hc.table[i] = NULL;
    hc.used = 0;
    /* With room, interning shares its published representative. */
    Atom *published = atom_bool(arena, false);
    check(published && atom_bool(arena, false) == published,
          "successful hashconsing preserves the published representative");
    arena_set_hashcons(arena, NULL);
    hashcons_free(&hc);
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

    /* The positive control: a variable's name key is kept. */
    dirty(&arena, mark, 0xbeu);
    Atom *key = atom_symbol(&arena, "spelling");
    Atom *variable = atom_var_with_name_key(&arena, key, var_id_from_base(7u));
    check(variable && variable->name_key == key,
          "a variable keeps its name key");

    check_full_hashcons_fallback(&arena);
    check_expression_assembly();

    arena_free(&arena);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);

    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: atom constructors write every field (%d checks)\n",
           g_checks);
    return 0;
}
