#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "symbol.h"

static unsigned checks;
#define CHECK(test) do { assert(test); checks++; } while (0)
enum { BYTES = 65536 };

static void retained_bytes(const char *bytes) {
    Arena source, first, second;
    arena_init_detached(&source);
    arena_init_detached(&first);
    arena_init_detached(&second);
    ArenaMark source_mark = arena_mark(&source);
    Atom *original = atom_string_n(&source, bytes, BYTES);
    CHECK(original && atom_string_storage_references_test(original) == 1u);
    CHECK(source.external_bytes >= BYTES);
    Atom *copy = atom_deep_copy(&first, original);
    CHECK(copy != original && copy->ground.sval == original->ground.sval);
    CHECK(copy->ground.slen == BYTES && copy->ground.sval[BYTES] == '\0');
    CHECK(atom_string_storage_references_test(copy) == 2u);
    CHECK(atom_eq(copy, original) && atom_hash(copy) == atom_hash(original));
    CHECK(atom_graph_is_closed_for_arena(&first, copy));
    ArenaMark mark = arena_mark(&first);
    size_t pressure = first.external_bytes;
    Atom *alias = atom_deep_copy(&first, original);
    CHECK(alias->ground.sval == copy->ground.sval);
    CHECK(atom_string_storage_references_test(copy) == 2u);
    CHECK(first.external_bytes == pressure);
    Atom *independent = atom_string_n(&source, bytes, BYTES);
    CHECK(atom_eq(original, independent));
    CHECK(independent->ground.sval != original->ground.sval);
    Atom *temporary = atom_deep_copy(&first, independent);
    CHECK(temporary && atom_string_storage_references_test(independent) == 2u);
    arena_reset(&first, mark);
    CHECK(atom_string_storage_references_test(independent) == 1u);
    CHECK(atom_string_storage_references_test(copy) == 2u);
    CHECK(atom_string_equals_bytes(copy, bytes, BYTES));
    Atom *survivor = atom_deep_copy(&second, copy);
    CHECK(survivor->ground.sval == copy->ground.sval);
    CHECK(atom_string_storage_references_test(survivor) == 3u);
    arena_reset(&source, source_mark);
    CHECK(atom_string_storage_references_test(survivor) == 2u);
    CHECK(atom_string_equals_bytes(copy, bytes, BYTES));
    arena_free(&first);
    CHECK(atom_string_storage_references_test(survivor) == 1u);
    CHECK(atom_string_equals_bytes(survivor, bytes, BYTES));
    arena_free(&source);
    arena_free(&second);
}

static void hashcons_boundary(const char *bytes) {
    Arena source, destination;
    HashConsTable intern;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    hashcons_init_compact(&intern);
    Atom *original = atom_string_n(&source, bytes, BYTES);
    Atom *canonical = hashcons_get(&intern, original);
    CHECK(canonical && canonical != original && atom_eq(original, canonical));
    CHECK(!(canonical->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE));
    CHECK(atom_string_storage_references_test(original) == 1u);
    CHECK(canonical->ground.sval != original->ground.sval);
    CHECK(hashcons_get(&intern, original) == canonical);
    Atom *copy = atom_deep_copy(&destination, canonical);
    CHECK(copy && atom_string_storage_references_test(copy) == 1u);
    CHECK(copy->ground.sval != canonical->ground.sval);
    arena_free(&source);
    hashcons_free(&intern);
    CHECK(atom_string_equals_bytes(copy, bytes, BYTES));
    arena_free(&destination);
}

static void representation_facts(const char *bytes) {
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    Atom *small = atom_string_n(&source, bytes, 4095u);
    CHECK(atom_string_storage_references_test(small) == 0u);
    Atom *small_copy = atom_deep_copy(&destination, small);
    CHECK(small_copy->ground.sval != small->ground.sval && atom_eq(small, small_copy));
    Atom *large = atom_string_n(&source, bytes, 4096u);
    CHECK(atom_string_storage_references_test(large) == 1u);
    Atom *row = atom_expr2(&source, large, small);
    CHECK(!(row->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE));
    Atom *suffix = atom_expr_suffix(&source, row, 1u);
    CHECK(!(suffix->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE));
    Atom *copy = atom_deep_copy(&destination, row);
    CHECK(copy && !(copy->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE));
    CHECK(copy->expr.elems[0]->ground.sval == large->ground.sval);
    Atom *empty = atom_string_n(&source, "", 0u);
    CHECK(empty && empty->ground.slen == 0u && atom_string_storage_references_test(empty) == 0u);
    arena_free(&source);
    CHECK(atom_string_equals_bytes(copy->expr.elems[0], bytes, 4096u));
    CHECK(atom_string_equals_bytes(copy->expr.elems[1], bytes, 4095u));
    arena_free(&destination);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    char *bytes = malloc(BYTES);
    assert(bytes);
    memset(bytes, 'x', BYTES);
    bytes[5] = '\0';
    bytes[BYTES / 2] = (char)0xff;
    retained_bytes(bytes);
    hashcons_boundary(bytes);
    representation_facts(bytes);
    free(bytes);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(AtomStringStorageSummary %u 0)\n", checks);
    return 0;
}
