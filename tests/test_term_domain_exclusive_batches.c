#include "proof_batches.h"
#define main basic_close_component_main
#include "test_term_domain_exclusive_close.c"
#undef main

typedef struct {
    CettaTermDomain *domain;
    CettaProofRowLayout layout;
    AtomId root, seed;
    unsigned called;
} ClosedFactory;
static void closed_factory(void *context) {
    ClosedFactory *f = context;
    f->called++;
    CettaProofBatchStore *sentinel = (void *)(uintptr_t)1u;
    CettaProofBatchStatus status = cetta_proof_batch_store_new(
        cetta_term_domain_universe(f->domain), &f->layout, f->root, f->seed,
        (CettaProofLimits){8u, 8u, 8u, 64u, 64u},
        (CettaProofBatchLifetime){f->domain, cetta_term_domain_retain,
            cetta_term_domain_release}, &sentinel);
    assert(status == CETTA_PROOF_BATCH_CAPACITY && sentinel == (void *)(uintptr_t)1u);
    assert(!cetta_proof_batch_diagnostics().bytes);
}
static void batch_case(TermUniverseStoreFormat format) {
    CettaTermDomain *domain = cetta_term_domain_new(format, TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    observed_universe = cetta_term_domain_universe(domain);
    observed_symbols = cetta_term_domain_symbols(domain);
    freed_universes = freed_symbols = 0u;
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(observed_universe, parse(&syntax, "(rows 2 (C a))"));
    AtomId seed = key(observed_universe, parse(&syntax, "I"));
    CettaProofRowLayout layout = {.proof = {.length = 2u, .child = {2u, 2u}},
        .arity = {3u, 3u}, .head = {parse(&syntax, "rows")->sym_id, parse(&syntax, "C")->sym_id},
        .universe_instance = observed_universe->instance_id, .storage_epoch = observed_universe->storage_epoch};
    Payload payload = {.domain = domain, .space_live = true};
    space_init_with_universe(&payload.space, observed_universe);
    payload.space.kind = SPACE_KIND_SET;
    space_add(&payload.space, parse(&syntax, "(rows 2 (C a I))"));
    assert(space_promote_proof_seed(&payload.space, &layout, root, seed,
        (CettaProofLimits){8u, 8u, 8u, 64u, 64u},
        (CettaProofBatchLifetime){domain, cetta_term_domain_retain,
            cetta_term_domain_release}) == SPACE_PROOF_IMAGE_READY);
    CettaProofBatchStore *store = space_proof_batch_store_borrow(&payload.space, layout.head[0]);
    CettaProofBatchView *view = NULL;
    assert(store && cetta_proof_batch_capture(store, &view) == CETTA_PROOF_BATCH_OK);
    assert(!cetta_term_domain_close_if_last(&scope, domain, dispose_space, &payload));
    assert(!payload.disposed && scope.domain == domain);
    dispose_space(&payload);
    assert(cetta_proof_batch_view_count(view) == 1u && cetta_proof_batch_view_current(view));
    assert(!cetta_term_domain_close_if_last(&scope, domain, dispose_space, &payload));
    assert(payload.disposed == 1u && scope.domain == domain);
    cetta_proof_batch_view_release(view);
    assert(!cetta_proof_batch_diagnostics().bytes);
    arena_free(&syntax);
    assert(cetta_term_domain_close_if_last(&scope, domain, dispose_space, &payload));
    assert(!scope.domain && payload.disposed == 2u && freed_universes == 1u && freed_symbols == 1u);
    observed_universe = NULL; observed_symbols = NULL;

    domain = cetta_term_domain_new(format, TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    assert(domain && cetta_term_domain_enter(domain, &scope));
    arena_init_detached(&syntax);
    TermUniverse *u = cetta_term_domain_universe(domain);
    ClosedFactory f = {.domain = domain,
        .root = key(u, parse(&syntax, "(rows 2 (C a))")), .seed = key(u, parse(&syntax, "I"))};
    f.layout = (CettaProofRowLayout){.proof = {.length = 2u, .child = {2u, 2u}},
        .arity = {3u, 3u}, .head = {parse(&syntax, "rows")->sym_id, parse(&syntax, "C")->sym_id},
        .universe_instance = u->instance_id, .storage_epoch = u->storage_epoch};
    arena_free(&syntax);
    assert(cetta_term_domain_close_if_last(&scope, domain, closed_factory, &f));
    assert(f.called == 1u && !scope.domain && !g_symbols && !g_var_intern && !g_hashcons);
    printf("PASS: %s actual batch Pool/view prevents exclusive close; closed-owner batch factory refuses without publication\n",
        format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 ? "compact" : "wide");
}
int main(void) {
    batch_case(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1);
    batch_case(TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1);
    return 0;
}
