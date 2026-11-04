#include "atom.h"
#include "petta_program.h"
#include "space.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    Arena persistent;
    TermUniverse universe;
} SignatureStorage;

static SignatureStorage *storage_new(void) {
    SignatureStorage *storage = calloc(1u, sizeof(*storage));
    assert(storage);
    arena_init_detached(&storage->persistent);
    term_universe_init(&storage->universe);
    term_universe_set_persistent_arena(&storage->universe,
                                      &storage->persistent);
    term_universe_set_collection_mode(&storage->universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    return storage;
}

static void storage_free(SignatureStorage *storage) {
    term_universe_free(&storage->universe);
    arena_free(&storage->persistent);
    free(storage);
}

static void expect_signature(PettaProgram *program, Space *space,
                              SymbolId head, Atom *expected) {
    Arena observed;
    arena_init_detached(&observed);
    Atom *signature = NULL;
    assert(petta_program_inferred_signature_lookup(
        program, space, head, 1u, &observed, &signature));
    assert(signature && atom_eq(signature, expected));
    arena_free(&observed);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;

    Arena syntax;
    arena_init_detached(&syntax);
    Atom *number = atom_symbol(&syntax, "Number");
    Atom *signature = atom_expr3(&syntax,
        atom_symbol(&syntax, "->"), number, number);
    SymbolId first_head = symbol_intern_cstr(&symbols, "signature-first");
    SymbolId peer_head = symbol_intern_cstr(&symbols, "signature-peer");
    SymbolId foreign_head = symbol_intern_cstr(&symbols, "signature-other");
    PettaProgram *program = petta_program_new();
    assert(program && petta_program_enable_analysis(program));

    SignatureStorage *other = storage_new();
    Space foreign;
    space_init_with_universe(&foreign, &other->universe);
    const size_t other_spaces = other->universe.root_provider_len;
    assert(petta_program_inferred_signature_put(
        program, &foreign, foreign_head, 1u, signature));
    assert(other->universe.root_provider_len == other_spaces + 1u);

    for (unsigned iteration = 0u; iteration < 4u; iteration++) {
        SignatureStorage *storage = storage_new();
        Space first, peer;
        space_init_with_universe(&first, &storage->universe);
        space_init_with_universe(&peer, &storage->universe);
        const size_t space_providers = storage->universe.root_provider_len;
        assert(petta_program_inferred_signature_put(
            program, &first, first_head, 1u, signature));
        assert(petta_program_inferred_signature_put(
            program, &peer, peer_head, 1u, signature));
        /* One program provider roots both Spaces' derived signatures. */
        assert(storage->universe.root_provider_len == space_providers + 1u);
        assert(term_universe_collect(&storage->universe, NULL));
        expect_signature(program, &first, first_head, signature);
        expect_signature(program, &peer, peer_head, signature);

        petta_program_forget_space(program, &first);
        assert(storage->universe.root_provider_len == space_providers + 1u);
        space_free(&first);
        assert(term_universe_collect(&storage->universe, NULL));
        expect_signature(program, &peer, peer_head, signature);
        const size_t peer_providers = storage->universe.root_provider_len;

        petta_program_forget_space(program, &peer);
        /* The program must release the universe before its final Space and
         * storage owner retire. Another universe's signature stays rooted. */
        assert(storage->universe.root_provider_len + 1u == peer_providers);
        space_free(&peer);
        assert(storage->universe.root_provider_len == 0u);
        storage_free(storage);
        assert(term_universe_collect(&other->universe, NULL));
        expect_signature(program, &foreign, foreign_head, signature);
    }

    /* Program teardown also releases providers for still-live Spaces. All
     * previously retired heap universes are already gone at this boundary. */
    petta_program_free(program);
    assert(other->universe.root_provider_len == other_spaces);
    space_free(&foreign);
    assert(other->universe.root_provider_len == 0u);
    storage_free(other);
    arena_free(&syntax);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS: inferred signatures root live universes and release final Space registrations before storage teardown");
    return 0;
}
