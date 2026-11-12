#pragma push_macro("main")
#undef main
#define main owner_component_main
#include "test_term_domain_owner.c"
#undef main
#pragma pop_macro("main")

typedef struct {
    CettaTermDomain *domain;
    Space space;
    DomainCapture capture;
    unsigned references, released, disposed, attempts;
    bool closing, space_live, reenter;
    CettaTermDomainScope *scope;
} Payload;
static void dispose_space(void *);

/* Acquisition precedes every TU read, construction and publication. */
static bool capture_try(CettaTermDomain *domain, DomainCapture *out) {
    if (!cetta_term_domain_retain(domain)) return false;
    TermUniverse *u = cetta_term_domain_universe(domain);
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(u, parse(&syntax, "(late 0 (C value))"));
    AtomId seed = key(u, parse(&syntax, "I"));
    CettaProofBuilder *builder = NULL;
    CettaProofImage *image = NULL;
    CettaProofStatus result = cetta_proof_builder_new(u, root, seed,
        (CettaProofLimits){8u, 8u, 8u, 64u, 64u}, &builder);
    if (result == CETTA_PROOF_OK) result = cetta_proof_builder_seal(builder, &image);
    cetta_proof_builder_free(builder);
    arena_free(&syntax);
    if (result != CETTA_PROOF_OK) {
        cetta_proof_image_release(image);
        cetta_term_domain_release(domain);
        return false;
    }
    *out = (DomainCapture){domain, image};
    return true;
}
static void payload_keep(void *context) { ((Payload *)context)->references++; }
static void payload_drop(void *context) {
    Payload *p = context;
    assert(p->references);
    if (--p->references) return;
    p->released++;
    p->attempts++;
    if (p->closing) {
        DomainCapture sentinel = {(void *)(uintptr_t)1u, (void *)(uintptr_t)2u};
        assert(!capture_try(p->domain, &sentinel));
        assert(sentinel.domain == (void *)(uintptr_t)1u &&
               sentinel.image == (void *)(uintptr_t)2u);
        CettaTermDomainScope rejected = {0};
        assert(!cetta_term_domain_enter(p->domain, &rejected) && !rejected.domain);
    } else {
        if (p->reenter) {
            assert(cetta_term_domain_universe(p->domain)->collection_inhibit == 1u);
            assert(!cetta_term_domain_close_if_last(p->scope, p->domain, dispose_space, NULL, p));
            assert(p->scope->domain == p->domain && p->disposed == 1u);
        }
        assert(capture_try(p->domain, &p->capture));
    }
}
static void dispose_space(void *context) {
    Payload *p = context;
    p->disposed++;
    if (p->space_live) {
        space_free(&p->space);
        p->space_live = false;
    }
}
static void row_finalizer(TermUniverseStoreFormat format, unsigned kind) {
    CettaTermDomain *domain = cetta_term_domain_new(format,
        TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    observed_universe = cetta_term_domain_universe(domain);
    observed_symbols = cetta_term_domain_symbols(domain);
    freed_universes = freed_symbols = 0u;
    Payload payload = {.domain = domain, .closing = kind == 0u, .space_live = true,
        .reenter = kind == 2u, .scope = &scope};
    space_init_with_universe(&payload.space, observed_universe);
    Arena source;
    arena_init_detached(&source);
    Atom *handle = atom_native_handle_identifier(&source, 97, &payload,
                                                  payload_keep, payload_drop);
    assert(handle && payload.references == 1u);
    space_add(&payload.space, handle);
    assert(payload.references == 2u && observed_universe->fallback_bytes);
    arena_free(&source);
    assert(payload.references == 1u && !payload.released);
    if (kind == 0u) {
        term_universe_collection_inhibit_enter(observed_universe);
        assert(!cetta_term_domain_close_if_last(&scope, domain, dispose_space, NULL, &payload));
        assert(scope.domain == domain && !payload.disposed);
        term_universe_collection_inhibit_leave(observed_universe);
        assert(cetta_term_domain_close_if_last(&scope, domain, dispose_space, NULL, &payload));
        assert(!scope.domain && !payload.space_live && payload.disposed == 1u);
        assert(payload.released == 1u && payload.attempts == 1u && !payload.capture.image);
    } else {
        if (kind == 1u) {
            assert(cetta_term_domain_retain(domain));
            assert(!cetta_term_domain_close_if_last(&scope, domain, dispose_space, NULL, &payload));
        }
        assert(scope.domain == domain && !payload.disposed && !payload.released);
        assert(observed_universe->collection_mode == TERM_UNIVERSE_COLLECTION_RECLAIM);
        dispose_space(&payload);
        assert(term_universe_collect(observed_universe, NULL));
        assert(payload.released == 1u && payload.capture.image);
        if (kind == 1u) cetta_term_domain_release(domain);
        assert(!cetta_term_domain_close_if_last(&scope, domain, dispose_space, NULL, &payload));
        assert(cetta_proof_image_matches_universe(payload.capture.image, observed_universe));
        capture_drop(&payload.capture);
        assert(cetta_term_domain_close_if_last(&scope, domain, dispose_space, NULL, &payload));
        assert(!scope.domain && payload.disposed == 2u);
    }
    assert(freed_universes == 1u && freed_symbols == 1u);
    observed_universe = NULL; observed_symbols = NULL;
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    printf("PASS: %s row-resource finalizer %s with capture publication and physical lifetime preserved\n",
        format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 ? "compact" : "wide",
        kind == 0u ? "refuses the closed owner" :
        kind == 1u ? "acquires a real lease on retained owner" : "declines reentrant close and acquires a capture");
}
int main(void) {
    for (unsigned kind = 0u; kind < 3u; kind++) {
        row_finalizer(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1, kind);
        row_finalizer(TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1, kind);
    }
    return 0;
}
