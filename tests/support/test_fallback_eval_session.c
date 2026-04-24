#include "atom.h"
#include "eval.h"
#include "parser.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>

int main(void) {
    int rc = 1;
    Arena arena;
    Arena eval_arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable var_intern;
    ResultSet rs;
    Atom *expr = NULL;
    size_t pos = 0;

    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&eval_arena);
    arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&var_intern);
    g_symbols = &symbols;
    g_var_intern = &var_intern;
    eval_set_library_context(NULL);

    expr = parse_sexpr(&arena, "(once (superpose (1 2)))", &pos);
    if (!expr) {
        fprintf(stderr, "parse failure\n");
        goto cleanup;
    }

    result_set_init(&rs);
    eval_top(&space, &eval_arena, expr, &rs);
    if (rs.len != 1) {
        fprintf(stderr, "unexpected result count: %u\n", rs.len);
        result_set_free(&rs);
        goto cleanup;
    }

    char *rendered = atom_to_string(&arena, rs.items[0]);
    if (!rendered) {
        fprintf(stderr, "render failure\n");
        result_set_free(&rs);
        goto cleanup;
    }

    fputs(rendered, stdout);
    result_set_free(&rs);
    rc = 0;

cleanup:
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&eval_arena);
    arena_free(&arena);
    return rc;
}
