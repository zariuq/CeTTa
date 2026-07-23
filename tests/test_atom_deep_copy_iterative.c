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

    Atom *shared = atom_expr2(
        &source, atom_symbol(&source, "shared"),
        atom_int(&source, 11));
    Atom *left = atom_expr2(
        &source, atom_symbol(&source, "left"), shared);
    Atom *right = atom_expr2(
        &source, atom_symbol(&source, "right"), shared);
    Atom *equal_left = atom_expr2(
        &source, atom_symbol(&source, "equal"),
        atom_int(&source, 12));
    Atom *equal_right = atom_expr2(
        &source, atom_symbol(&source, "equal"),
        atom_int(&source, 12));
    assert(equal_left != equal_right);

    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    Atom *left_copy = atom_deep_copy_session_copy(session, left);
    Atom *right_copy = atom_deep_copy_session_copy(session, right);
    Atom *equal_left_copy =
        atom_deep_copy_session_copy(session, equal_left);
    Atom *equal_right_copy =
        atom_deep_copy_session_copy(session, equal_right);
    assert(left_copy != NULL && right_copy != NULL);
    assert(left_copy->expr.elems[1] == right_copy->expr.elems[1]);
    assert(equal_left_copy != equal_right_copy);
    assert(atom_deep_copy_session_copy(session, left_copy) == left_copy);
    atom_deep_copy_session_free(session);

    arena_free(&destination);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: iterative and multi-root atom deep copy");
    return 0;
}
