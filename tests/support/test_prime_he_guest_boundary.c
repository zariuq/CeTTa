/* Exercise the private fragment/checker boundary on admitted and rejected
 * signatures. This translation unit replaces prime_semantics.o. */
#include "../../src/prime_semantics.c"
#include <assert.h>

static Atom *read_one(Arena *arena, const char *source) {
    size_t position = 0u;
    Atom *term = parse_sexpr(arena, source, &position);
    assert(term);
    return term;
}

int main(int argc, char **argv) {
    (void)argc;
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    CettaLibraryContext first, second;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    arena_init(&arena);
    cetta_library_context_init_for_language_profile(
        &first, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_library_context_init_for_language_profile(
        &second, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_library_context_set_exec_path(&first, argv[0]);
    cetta_library_context_set_exec_path(&second, argv[0]);
    eval_set_library_context(&first);

    Atom *signature = read_one(&arena,
        "(SCons (DConst N (Srt type)) (SCons (DConst z (Con N)) SNil))");
    Atom *proof = read_one(&arena, "(Con z)");
    Atom *type = read_one(&arena, "(Con N)");
    Atom *reason = NULL;
    assert(prime_lf_check_admitted(&arena, signature, proof, type, &reason) ==
           PRIME_HOL_ACCEPTED);
    assert(first.prime_lf_guest && first.prime_lf_guest_free);
    assert(prime_lf_check_admitted(&arena, signature, proof,
                                   read_one(&arena, "(Srt type)"), &reason) ==
           PRIME_HOL_REFUSED);
    Atom *duplicate = read_one(&arena,
        "(SCons (DConst N (Srt type)) (SCons (DConst N (Srt type)) SNil))");
    assert(prime_lf_check_admitted(&arena, duplicate, proof, type, &reason) ==
           PRIME_HOL_UNAVAILABLE);

    /* Independent hosts select independent checker instances. */
    eval_set_library_context(&second);
    assert(prime_lf_check_admitted(&arena, signature, proof, type, &reason) ==
           PRIME_HOL_ACCEPTED);
    assert(second.prime_lf_guest && second.prime_lf_guest_free);
    assert(first.prime_lf_guest != second.prime_lf_guest);

    /* The other hosted checker remains independently usable. */
    Atom *answer = NULL;
    Atom *request = read_one(&arena, "(== 1 1)");
    assert(eval_prime_hol_guest_evaluate(&arena, request, &answer, &reason));
    assert(prime_hol_true(answer));

    eval_set_library_context(NULL);
    cetta_library_context_free(&second);
    cetta_library_context_free(&first);
    assert(!first.prime_lf_guest && !second.prime_lf_guest);
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: hosted checker admission and ownership boundaries");
    return 0;
}
