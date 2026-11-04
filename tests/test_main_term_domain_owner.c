#include "library.h"
#include "eval.h"
#define main domain_component_main
#include "test_term_domain_owner.c"
#undef main

static CettaTermDomain *escaped_domain;
static DomainCapture escaped_capture;
static unsigned closed_contexts;
int cetta_term_domain_cli_entry(int, char **);
void __real_cetta_library_context_free(CettaLibraryContext *);
void __wrap_cetta_library_context_free(CettaLibraryContext *ctx) {
    CettaTermDomain *domain = cetta_library_context_term_domain(ctx);
    assert(domain && !escaped_domain && !escaped_capture.image);
    assert(cetta_term_domain_retain(domain));
    escaped_domain = domain;
    observed_universe = cetta_term_domain_universe(domain);
    observed_symbols = cetta_term_domain_symbols(domain);
    freed_universes = freed_symbols = 0u;
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(observed_universe, parse(&syntax, "(audit 0 (C a))"));
    AtomId seed = key(observed_universe, parse(&syntax, "I"));
    escaped_capture = capture_new(domain, root, seed);
    arena_free(&syntax);
    __real_cetta_library_context_free(ctx);
    closed_contexts++;
}

int main(int argc, char **arguments) {
    assert(argc == 2);
    char *argv[] = {arguments[1],
        "--lang", "petta", "--profile", "extended", "-e",
        "!(bind! &later (new-space set)) !(add-atom &later (later 99)) !(+ 1 2)", NULL};
    assert(cetta_term_domain_cli_entry(7, argv) == 0);
    assert(closed_contexts == 1u && escaped_domain && escaped_capture.image);
    assert(eval_current_library_context() == NULL);
    assert(!freed_universes && !freed_symbols);
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    CettaTermDomainScope scope = {0};
    assert(cetta_term_domain_enter(escaped_domain, &scope));
    TermUniverse *u = cetta_term_domain_universe(escaped_domain);
    assert(u->collection_mode == TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(cetta_proof_image_matches_universe(escaped_capture.image, u));
    for (unsigned i = 0u; i < 4096u; i++)
        assert(tu_intern_int(u, INT64_C(9000000) + i) != CETTA_ATOM_ID_NONE);
    TermUniverseCollectionReport report;
    assert(term_universe_collect_at_safe_point(u, &report) && report.reclaimed >= 4096u);
    assert(u->root_provider_len == 1u && !u->root_providers[0].resource_owner);
    assert(cetta_proof_image_stats(escaped_capture.image).support == 1u);
    cetta_term_domain_leave(&scope);
    capture_drop(&escaped_capture);
    assert(!freed_universes && !freed_symbols);
    cetta_term_domain_release(escaped_domain);
    escaped_domain = NULL;
    assert(freed_universes == 1u && freed_symbols == 1u);
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    puts("PASS: actual main returns with an independent captured view/domain; later allocation still collects under the preserved policy");
    return 0;
}
