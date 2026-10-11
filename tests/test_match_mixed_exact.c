#include "library.h"
#include "session.h"
#include "shared_transition.h"
#include "space_match_backend.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *row(Arena *arena, Atom *value) {
    return atom_expr2(arena, atom_symbol(arena, "row"), value);
}

static void assert_rows(Space *space, Arena *arena, Atom *query,
                        const CettaIndex *expected, size_t count) {
    SubstMatchSet matches;
    space_subst_query(space, arena, query, &matches);
    assert(matches.len == count);
    for (size_t index = 0u; index < count; index++) {
        assert(matches.items[index].atom_idx == expected[index]);
        Bindings bindings;
        assert(space_subst_match_with_seed(space, query, &matches.items[index],
                                           NULL, arena, &bindings));
        bindings_free(&bindings);
    }
    smset_free(&matches);
}

static void test_frontier(CettaLanguageId language, SpaceEngine engine,
                          bool indexed) {
    Arena source, output;
    arena_init(&source);
    arena_init(&output);
    CettaLibraryContext *context = calloc(1u, sizeof(*context));
    assert(context);
    cetta_library_context_init_for_language_profile(context, language,
        language == CETTA_LANGUAGE_HE ? cetta_profile_he_extended()
                                     : cetta_profile_petta_extended());
    eval_set_library_context(context);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    assert(space_match_backend_try_set(&space, engine));
    Atom *value = atom_symbol(&source, "K");
    Atom *query = row(&source, value);
    Atom *subject_variable = atom_var(&source, "subject");
    space_add(&space, row(&source, subject_variable));
    space_add(&space, query);
    space_add(&space, row(&source, atom_symbol(&source, "different")));
    space_add(&space, query);
    if (indexed) {
        for (unsigned index = 0u; index < 40u; index++) {
            space_add(&space, atom_expr2(&source,
                atom_symbol(&source, "unrelated"), atom_int(&source, index)));
        }
    }
    /* Literal duplicates do not disqualify the stored variable's match. */
    const CettaIndex expected[] = {0u, 1u, 3u};
    assert_rows(&space, &output, query, expected, 3u);
    assert(subject_variable->kind == ATOM_VAR);
    bool found = true;
    assert(!space_match_backend_ground_exact_exists_frontier(
        &space, query, &found));
    assert(!found);
    /* The same complete occurrence list crosses a concurrent boundary. */
    cetta_shared_transition_scope_enter();
    assert_rows(&space, &output, query, expected, 3u);
    cetta_shared_transition_scope_leave();
    space_free(&space);

    space_init_with_universe(&space, &universe);
    assert(space_match_backend_try_set(&space, engine));
    space_add(&space, query);
    space_add(&space, query);
    for (unsigned index = 0u; index < 40u; index++) {
        space_add(&space, atom_expr2(&source,
            atom_symbol(&source, "unrelated"), atom_int(&source, index)));
    }
    space_add(&space, atom_expr2(&source,
        atom_symbol(&source, "unrelated-open"), subject_variable));
    /* The cold scan conservatively retains every open row. Once the
     * discrimination index is warm, an unrelated open head is excluded. */
    assert(!space_contains_only_exact_atoms(&space));
    const CettaIndex closed[] = {0u, 1u};
    for (unsigned pass = 0u; pass < 5u; pass++)
        assert_rows(&space, &output, query, closed, 2u);
    assert(space_match_backend_ground_exact_exists_frontier(
        &space, query, &found) && found);
    Atom *absent = row(&source, atom_symbol(&source, "absent"));
    assert_rows(&space, &output, absent, NULL, 0u);
    space_free(&space);
    term_universe_free(&universe);
    eval_set_library_context(NULL);
    cetta_library_context_free(context);
    free(context);
    arena_free(&output);
    arena_free(&source);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_var_intern = &variables;
    const CettaLanguageId languages[] = {CETTA_LANGUAGE_HE, CETTA_LANGUAGE_PETTA};
    const SpaceEngine engines[] = {SPACE_ENGINE_NATIVE,
                                   SPACE_ENGINE_NATIVE_CANDIDATE_EXACT};
    for (unsigned language = 0u; language < 2u; language++) {
        for (unsigned engine = 0u; engine < 2u; engine++) {
            test_frontier(languages[language], engines[engine], false);
            test_frontier(languages[language], engines[engine], true);
        }
    }
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    puts("PASS: exact shortcuts preserve open rows and ordered duplicates");
    return 0;
}
