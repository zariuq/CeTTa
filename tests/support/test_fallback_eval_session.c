#include "atom.h"
#include "eval.h"
#include "grounded.h"
#include "library.h"
#include "parser.h"
#include "session.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool grounded_profile_contract(void) {
    CettaLibraryContext he_prime = {0};
    CettaLibraryContext prime = {0};
    CettaLibraryContext petta = {0};
    cetta_eval_session_init(
        &he_prime.session, CETTA_LANGUAGE_HE, cetta_profile_he_prime());
    cetta_eval_session_init(
        &prime.session, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_eval_session_init(
        &petta.session, CETTA_LANGUAGE_PETTA, cetta_profile_petta_extended());

    SymbolId typing = symbol_intern_cstr(g_symbols, "normalize-type");
    SymbolId repra = symbol_intern_cstr(g_symbols, "repra");
    SymbolId ordinary = symbol_intern_cstr(g_symbols, "ordinary-constructor");

    eval_set_library_context(NULL);
    bool fallback_ok =
        is_grounded_op(g_builtin_syms.op_plus) &&
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) &&
        !is_grounded_op(ordinary);

    eval_set_library_context(&he_prime);
    bool he_prime_ok =
        is_grounded_op(typing) && is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);

    eval_set_library_context(&prime);
    bool prime_ok =
        is_grounded_op(typing) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);

    eval_set_library_context(&petta);
    bool petta_ok =
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        is_grounded_op(repra) && is_grounded_op(repra) &&
        !is_grounded_op(ordinary);

    eval_set_library_context(&prime);
    bool restored_prime_ok =
        is_grounded_op(typing) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra);

    eval_set_library_context(NULL);
    bool restored_fallback_ok =
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);
    return fallback_ok && he_prime_ok && prime_ok && petta_ok &&
           restored_prime_ok && restored_fallback_ok;
}

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

    if (!grounded_profile_contract()) {
        fprintf(stderr, "grounded operator profile contract failure\n");
        goto cleanup;
    }

    expr = parse_sexpr(&arena, "(once (superpose (1 2)))", &pos);
    if (!expr) {
        fprintf(stderr, "parse failure\n");
        goto cleanup;
    }

    result_set_init(&rs);
    eval_top(&space, &eval_arena, expr, &rs);
    if (rs.len != 2) {
        fprintf(stderr, "unexpected result count: %" PRIu64 "\n", rs.len);
        result_set_free(&rs);
        goto cleanup;
    }

    char *rendered = atom_to_string(&arena, rs.items[0]);
    char *rendered2 = atom_to_string(&arena, rs.items[1]);
    if (!rendered || !rendered2) {
        fprintf(stderr, "render failure\n");
        result_set_free(&rs);
        goto cleanup;
    }
    if (!((strcmp(rendered, "(once 1)") == 0 &&
           strcmp(rendered2, "(once 2)") == 0) ||
          (strcmp(rendered, "(once 2)") == 0 &&
           strcmp(rendered2, "(once 1)") == 0))) {
        fprintf(stderr, "unexpected fallback results: [%s, %s]\n",
                rendered, rendered2);
        result_set_free(&rs);
        goto cleanup;
    }

    printf("[%s, %s]", rendered, rendered2);
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
