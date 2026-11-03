#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static bool watching, persistent;
static size_t allocation, failure = SIZE_MAX, tracked, held;
static void *owned[4096];
static bool refuse(void) {
    if (!watching) return false;
    size_t at = allocation++;
    if (persistent ? at >= failure : at == failure) { errno = ENOMEM; return true; }
    return false;
}
static void *retain(void *pointer) {
    if (pointer && watching) {
        assert(tracked < sizeof(owned) / sizeof(*owned));
        owned[tracked++] = pointer; held++;
    }
    return pointer;
}
static void *decoder_malloc(size_t size) {
    return refuse() ? NULL : retain(malloc(size));
}
static void *decoder_realloc(void *pointer, size_t size) {
    if (refuse()) return NULL;
    size_t slot = tracked;
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        slot = i; break;
    }
    void *next = realloc(pointer, size);
    if (slot < tracked) {
        if (next || !size) { owned[slot] = next; if (!next) { assert(held); held--; } }
        return next;
    }
    return retain(next);
}
static void decoder_free(void *pointer) {
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        owned[i] = NULL; assert(held); held--;
    }
    free(pointer);
}
#define malloc decoder_malloc
#define realloc decoder_realloc
#define free decoder_free
#include "../../src/cetta_stdlib.c"
#undef malloc
#undef realloc
#undef free
#include "runtime_names.h"
#include "runtime_session.h"

typedef struct { CettaRuntimeSession session; Arena arena; } Fixture;
static Fixture *fixture_new(void) {
    Fixture *fixture = calloc(1u, sizeof(*fixture));
    assert(fixture && arena_try_init_detached(&fixture->arena));
    assert(cetta_runtime_session_init(&fixture->session, &fixture->arena, CETTA_LANGUAGE_HE, NULL));
    assert(cetta_runtime_session_open_root(&fixture->session, &fixture->arena));
    return fixture;
}
static void fixture_free(Fixture *fixture) {
    cetta_runtime_session_dispose(&fixture->session);
    arena_free(&fixture->arena);
    free(fixture);
}
static void begin(size_t at, bool suffix) {
    assert(!held);
    allocation = tracked = 0; failure = at; persistent = suffix; watching = true;
}
static void clean(void) {
    watching = false;
    assert(held == 0u && "failed prelude retained a decoder buffer");
}
int main(void) {
    CettaNameDomain names;
    cetta_name_domain_init(&names);
    CettaNameContext previous = cetta_name_context_current();
    CettaNameContext current = cetta_name_domain_context(&names);
    cetta_name_context_install(&current);
    Fixture *baseline = fixture_new();
    begin(SIZE_MAX, false);
    assert(stdlib_try_load(&baseline->session.space, &baseline->arena));
    size_t sites = allocation;
    CettaCount expected = space_length64(&baseline->session.space);
    assert(sites > 0u && expected > 0u);
    clean(); fixture_free(baseline);
    unsigned refusals = 0;
    for (unsigned suffix = 0; suffix < 2; ++suffix) for (size_t at = 0; at < sites; ++at) {
        Fixture *fixture = fixture_new();
        begin(at, suffix != 0);
        assert(!stdlib_try_load(&fixture->session.space, &fixture->arena) && errno == ENOMEM);
        assert(space_length64(&fixture->session.space) == 0u && "failed prelude published a declaration prefix");
        clean();
        assert(stdlib_try_load(&fixture->session.space, &fixture->arena));
        assert(space_length64(&fixture->session.space) == expected);
        fixture_free(fixture);
        refusals++;
    }
    SymbolId x = symbol_intern_cstr(g_symbols, "decoder-x");
    SymbolId y = symbol_intern_cstr(g_symbols, "decoder-y");
    BlobVarScope scope;
    blob_var_scope_init(&scope);
    begin(1u, false);
    assert(var_id_is_none(blob_var_scope_id(&scope, x)) && scope.cap == 0u && scope.len == 0u);
    watching = false;
    VarId first = blob_var_scope_id(&scope, x);
    assert(!var_id_is_none(first) && var_id_eq(first, blob_var_scope_id(&scope, x)));
    assert(!var_id_eq(first, blob_var_scope_id(&scope, y)));
    blob_var_scope_free(&scope);
    clean();
    cetta_name_context_install(&previous);
    cetta_name_domain_free(&names);
    printf("prelude refusal: %u decoder allocations rejected, no published prefix or retained buffer, retry and variable sharing pass\n", refusals);
    return 0;
}
