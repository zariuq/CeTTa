#include "ordinal_priority.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>

static Atom *ordinal(Arena *arena, SymbolId tag, const int64_t *pairs, size_t length) {
    Atom *rows[8];
    assert(length <= 8u);
    for (size_t i = 0u; i < length; i++)
        rows[i] = atom_expr2(arena, atom_int(arena, pairs[2u * i]),
                             atom_int(arena, pairs[2u * i + 1u]));
    return atom_expr2(arena, atom_symbol_id(arena, tag), atom_expr(arena, rows, length));
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
    SymbolId tag = symbol_intern_cstr(&symbols, "Ordinal");
    const int64_t finite[] = {0, 99}, omega[] = {1, 1}, omega3[] = {1, 3};
    const int64_t square[] = {2, 1}, tail[] = {2, 1, 1, 4, 0, 7};
    Atom *values[] = {ordinal(&arena, tag, NULL, 0u), ordinal(&arena, tag, finite, 1u),
        ordinal(&arena, tag, omega, 1u), ordinal(&arena, tag, omega3, 1u),
        ordinal(&arena, tag, square, 1u), ordinal(&arena, tag, tail, 3u)};
    for (size_t i = 0u; i < 6u; i++)
        for (size_t j = 0u; j < 6u; j++) {
            int ordering;
            assert(cetta_ordinal_cnf_compare(values[i], values[j], tag, &ordering));
            assert((ordering > 0) - (ordering < 0) == (i > j) - (i < j));
        }
    const int64_t negative[] = {-1, 2}, zero[] = {1, 0}, repeated[] = {1, 2, 1, 3};
    const int64_t increasing[] = {0, 1, 1, 1};
    assert(!cetta_ordinal_cnf_valid(ordinal(&arena, tag, negative, 1u), tag));
    assert(!cetta_ordinal_cnf_valid(ordinal(&arena, tag, zero, 1u), tag));
    assert(!cetta_ordinal_cnf_valid(ordinal(&arena, tag, repeated, 2u), tag));
    assert(!cetta_ordinal_cnf_valid(ordinal(&arena, tag, increasing, 2u), tag));
    assert(!cetta_ordinal_cnf_valid(atom_int(&arena, 0), tag));
    assert(!cetta_ordinal_cnf_valid(values[0], symbol_intern_cstr(&symbols, "Other")));
    int ordering = 17;
    assert(!cetta_ordinal_cnf_compare(values[0], atom_float(&arena, 0), tag, &ordering));
    assert(ordering == 17);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: normalized ordinal CNF comparison and malformed-key controls");
    return 0;
}
