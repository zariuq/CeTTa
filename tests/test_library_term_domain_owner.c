#include "library.h"
#include "eval.h"
#define main domain_component_main
#include "test_term_domain_owner.c"
#undef main

static void empty_roots(const TermUniverse *u, TermUniverseRootVisit visit,
                         void *visit_context, void *context) {
    (void)u; (void)visit; (void)visit_context; (void)context;
}

int main(void) {
    CettaTermDomain *domain = cetta_term_domain_new_default(false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    observed_universe = cetta_term_domain_universe(domain);
    observed_symbols = cetta_term_domain_symbols(domain);
    freed_universes = freed_symbols = 0u;
    Witness witness = {.symbols = observed_symbols};
    assert(arena_retain_resource_sized(cetta_term_domain_persistent(domain),
        &witness, witness_keep, witness_drop, sizeof(witness), sizeof(witness)));
    CettaLibraryContext library, dirty;
    cetta_library_context_init_for_language_profile(&library, CETTA_LANGUAGE_PETTA, NULL);
    cetta_library_context_init_for_language_profile(&dirty, CETTA_LANGUAGE_PETTA, NULL);
    assert(cetta_library_context_universe(&library) == &library.term_universe);
    assert(!cetta_library_context_term_domain(&library));
    Arena dirty_arena;
    arena_init_detached(&dirty_arena);
    term_universe_set_persistent_arena(&dirty.term_universe, &dirty_arena);
    assert(tu_intern_int(&dirty.term_universe, 5) != CETTA_ATOM_ID_NONE);
    assert(!cetta_library_context_set_term_domain(&dirty, domain));
    eval_set_library_context(&library);
    cetta_library_context_free(&dirty);
    assert(eval_current_library_context() == &library);
    arena_free(&dirty_arena);
    CettaTermDomain *other = cetta_term_domain_new_default(false);
    assert(other && !cetta_library_context_set_term_domain(&library, other));
    cetta_term_domain_release(other);
    assert(g_symbols == observed_symbols);
    library.term_universe.resource_scope = &library;
    assert(!cetta_library_context_set_term_domain(&library, domain));
    library.term_universe.resource_scope = NULL;
    term_universe_collection_inhibit_enter(&library.term_universe);
    assert(!cetta_library_context_set_term_domain(&library, domain));
    term_universe_collection_inhibit_leave(&library.term_universe);
    assert(term_universe_add_root_provider(&library.term_universe, empty_roots, &library));
    term_universe_remove_root_provider(&library.term_universe, empty_roots, &library);
    assert(library.term_universe.root_provider_cap != 0u);
    assert(cetta_library_context_set_term_domain(&library, domain));
    assert(!library.term_universe.root_providers && !library.term_universe.root_provider_cap);
    assert(cetta_library_context_set_term_domain(&library, domain));
    assert(cetta_library_context_universe(&library) == observed_universe);
    assert(cetta_library_context_term_domain(&library) == domain);
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(observed_universe, parse(&syntax, "(rows 2 (C (pair $x $x)))"));
    AtomId seed = key(observed_universe, parse(&syntax, "I"));
    DomainCapture old = capture_new(domain, root, seed);
    Space space;
    space_init_with_universe(&space, cetta_library_context_universe(&library));
    space_add(&space, parse(&syntax, "(ordinary 7)"));
    assert(space_length64(&space) == 1u);
    cetta_term_domain_release(domain);
    cetta_library_context_free(&library);
    assert(eval_current_library_context() == NULL);
    assert(!cetta_library_context_term_domain(&library));
    assert(!freed_universes && !freed_symbols && !witness.finalized);
    /* Match actual main's cleanup order: context ends before root Space. */
    space_free(&space);
    assert(cetta_proof_image_matches_universe(old.image, observed_universe));
    arena_free(&syntax);
    cetta_term_domain_leave(&scope);
    assert(!freed_universes && !freed_symbols && !witness.finalized);
    assert(cetta_term_domain_enter(domain, &scope));
    assert(observed_universe->collection_mode == TERM_UNIVERSE_COLLECTION_RECLAIM);
    for (unsigned i = 0u; i < 4096u; i++)
        assert(tu_intern_int(observed_universe, INT64_C(8000000) + i) != CETTA_ATOM_ID_NONE);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(observed_universe, &report) && report.reclaimed >= 4096u);
    assert(tu_hdr(observed_universe, root));
    cetta_term_domain_leave(&scope);
    capture_drop(&old);
    assert(freed_universes == 1u && freed_symbols == 1u && witness.finalized == 1u);
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    puts("PASS: actual library adopts domain once, rejects incompatible/dirty adoption, tears down before root Space, and leaves an independent captured view alive");
    return 0;
}
