#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"

enum { COPY_DEPTH = 100000 };

typedef struct {
    Atom *from;
    Atom *to;
} TestCopyResolution;

static Atom *resolve_test_atom(void *context, Atom *src) {
    TestCopyResolution *resolution = context;
    return resolution && src == resolution->from
        ? resolution->to : src;
}

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

static void check_deep_print(Atom *list, bool petta) {
    FILE *stream = tmpfile();
    assert(stream != NULL);
    if (petta)
        atom_print_petta(list, stream);
    else
        atom_print(list, stream);
    long length = ftell(stream);
    assert(length > (long)COPY_DEPTH);
    rewind(stream);
    assert(fgetc(stream) == '(');
    assert(fseek(stream, -1L, SEEK_END) == 0);
    assert(fgetc(stream) == ')');
    fclose(stream);
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
    arena_set_runtime_kind(&source, CETTA_ARENA_RUNTIME_KIND_EVAL);
    arena_set_runtime_kind(&destination, CETTA_ARENA_RUNTIME_KIND_EVAL);

    /* Immutable symbols are canonical only within their owning arena. */
    Atom *source_symbol_first = atom_symbol(&source, "arena-symbol-cache");
    Atom *source_symbol_second = atom_symbol(&source, "arena-symbol-cache");
    Atom *destination_symbol =
        atom_symbol(&destination, "arena-symbol-cache");
    assert(source_symbol_first == source_symbol_second);
    assert(source_symbol_first != destination_symbol);
    assert(atom_eq(source_symbol_first, destination_symbol));

    /* A reset invalidates cached atoms from the prior allocation epoch. */
    Arena reset_probe;
    arena_init(&reset_probe);
    arena_set_runtime_kind(&reset_probe, CETTA_ARENA_RUNTIME_KIND_EVAL);
    ArenaMark reset_origin = arena_mark(&reset_probe);
    Atom *before_reset = atom_symbol(&reset_probe, "reset-symbol-cache");
    arena_reset(&reset_probe, reset_origin);
    (void)arena_alloc(&reset_probe, sizeof(Atom));
    Atom *after_reset = atom_symbol(&reset_probe, "reset-symbol-cache");
    assert(after_reset != before_reset);
    assert(arena_owns_atom(&reset_probe, after_reset));
    assert(atom_is_symbol(after_reset, "reset-symbol-cache"));
    arena_free(&reset_probe);

    payload = atom_expr2(&source, atom_symbol(&source, "payload"),
                         atom_int(&source, 7));
    list = make_deep_shared_list(&source, payload);
    copy = atom_deep_copy(&destination, list);
    assert(copy != NULL);
    assert(copy != list);
    check_deep_shared_list(copy);
    check_deep_print(copy, false);
    check_deep_print(copy, true);

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
    assert(atom_deep_copy_session_forwarded(session, left) == NULL);
    assert(atom_deep_copy_session_forwarded(session, shared) == NULL);
    Atom *left_copy = atom_deep_copy_session_copy(session, left);
    assert(atom_deep_copy_session_forwarded(session, left) == left_copy);
    assert(atom_deep_copy_session_forwarded(session, shared) ==
           left_copy->expr.elems[1]);
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

    Atom *suspension = atom_expr2(
        &source, atom_symbol(&source, "suspended"),
        atom_int(&source, 13));
    Atom *resolved = atom_expr2(
        &source, atom_symbol(&source, "resolved"),
        atom_int(&source, 21));
    Atom *wrapped = atom_expr2(
        &source, atom_symbol(&source, "wrapped"), suspension);
    TestCopyResolution resolution = {
        .from = suspension,
        .to = resolved,
    };
    session = atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    atom_deep_copy_session_set_resolver(
        session, resolve_test_atom, &resolution);
    Atom *wrapped_copy = atom_deep_copy_session_copy(session, wrapped);
    assert(wrapped_copy != NULL);
    assert(atom_eq(wrapped_copy->expr.elems[1], resolved));
    assert(atom_deep_copy_session_forwarded(session, suspension) == NULL);
    assert(atom_deep_copy_session_forwarded(session, resolved) ==
           wrapped_copy->expr.elems[1]);
    atom_deep_copy_session_free(session);

    /*
     * A young expression may point into an immutable older generation.
     * Promotion must copy the young parent while retaining the old child
     * exactly; structural equality alone would not catch repeated copying.
     */
    Arena nursery;
    arena_init(&nursery);
    Atom *old_child = atom_expr2(
        &destination, atom_symbol(&destination, "old"),
        atom_int(&destination, 17));
    Atom *young_parent = atom_expr2(
        &nursery, atom_symbol(&nursery, "young"), old_child);
    Atom *young_child = atom_expr2(
        &nursery, atom_symbol(&nursery, "temporary"),
        atom_int(&nursery, 23));
    Atom *mixed_parent = atom_expr2(
        &destination, atom_symbol(&destination, "mixed"), young_child);
    assert(arena_owns_atom(&destination, old_child));
    assert(!arena_owns_atom(&nursery, old_child));
    assert(arena_owns_atom(&destination, mixed_parent));
    assert(!arena_owns_atom(&destination, young_child));
    session = atom_deep_copy_session_new(&destination);
    assert(session != NULL);
    Atom *promoted_parent =
        atom_deep_copy_session_copy(session, young_parent);
    Atom *closed_mixed_parent =
        atom_deep_copy_session_copy(session, mixed_parent);
    assert(promoted_parent != NULL);
    assert(promoted_parent != young_parent);
    assert(promoted_parent->expr.elems[1] == old_child);
    /*
     * Shallow ownership is not closure: a destination-owned wrapper around a
     * nursery child must be traversed and repaired before the nursery dies.
     */
    assert(closed_mixed_parent != NULL);
    assert(closed_mixed_parent != mixed_parent);
    assert(closed_mixed_parent->expr.elems[1] != young_child);
    assert(arena_owns_atom(
        &destination, closed_mixed_parent->expr.elems[1]));
    assert(atom_eq(closed_mixed_parent, mixed_parent));
    atom_deep_copy_session_free(session);
    arena_free(&nursery);
    assert(atom_is_symbol(
        closed_mixed_parent->expr.elems[1]->expr.elems[0],
        "temporary"));

    arena_free(&destination);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: iterative atom copy, printing, and multi-root closure");
    return 0;
}
