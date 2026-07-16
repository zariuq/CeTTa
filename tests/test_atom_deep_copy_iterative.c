#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"

enum { COPY_DEPTH = 100000 };

static Atom *make_deep_shared_list(Arena *arena, Atom *payload) {
    Atom *list = atom_symbol(arena, "Nil");

    for (uint32_t i = 0; i < COPY_DEPTH; i++)
        list = atom_expr3(arena, atom_symbol(arena, "Cons"), payload, list);
    return list;
}

static void check_deep_shared_list(Atom *list) {
    Atom *first_payload = NULL;
    uint32_t count = 0;

    while (!atom_is_symbol(list, "Nil")) {
        assert(list != NULL);
        assert(list->kind == ATOM_EXPR);
        assert(list->expr.len == 3);
        assert(atom_is_symbol(list->expr.elems[0], "Cons"));
        if (!first_payload)
            first_payload = list->expr.elems[1];
        else
            assert(list->expr.elems[1] == first_payload);
        list = list->expr.elems[2];
        count++;
    }
    assert(count == COPY_DEPTH);
}

int main(void) {
    SymbolTable symbols;
    Arena source;
    Arena destination;
    Atom *payload;
    Atom *list;
    Atom *copy;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&source);
    arena_init(&destination);

    payload = atom_expr2(&source, atom_symbol(&source, "payload"),
                         atom_int(&source, 7));
    list = make_deep_shared_list(&source, payload);
    copy = atom_deep_copy(&destination, list);
    assert(copy != NULL);
    assert(copy != list);
    check_deep_shared_list(copy);

    arena_free(&destination);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: iterative atom deep copy");
    return 0;
}
