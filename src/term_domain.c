#include "term_domain.h"
#include "fallible_alloc.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct CettaTermDomain {
    _Atomic uint64_t references;
    TermUniverse universe;
    SymbolTable symbols;
    BuiltinSyms builtins;
    VarInternTable variables;
    HashConsTable hashcons;
    Arena persistent;
    bool shares_atoms;
};

static CettaTermDomain *domain_new(TermUniverseStoreFormat format,
        TermUniverseCollectionMode mode, bool shares_atoms, bool configured) {
    if ((format != TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 &&
         format != TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1) ||
        mode < TERM_UNIVERSE_COLLECTION_OFF ||
        mode > TERM_UNIVERSE_COLLECTION_RECLAIM)
        return NULL;
    CettaTermDomain *domain = cetta_fallible_calloc(1u, sizeof(*domain));
    if (!domain) return NULL;
    atomic_init(&domain->references, 1u);
    if (!term_universe_init_with_store_format(&domain->universe, format)) {
        free(domain);
        return NULL;
    }
    if (!symbol_table_try_init(&domain->symbols)) {
        term_universe_free(&domain->universe);
        free(domain);
        return NULL;
    }
    if (!symbol_table_try_init_builtins(&domain->symbols, &domain->builtins) ||
        !var_intern_try_init(&domain->variables)) {
        symbol_table_free(&domain->symbols);
        term_universe_free(&domain->universe);
        free(domain);
        return NULL;
    }
    domain->shares_atoms = shares_atoms;
    if (shares_atoms) hashcons_init(&domain->hashcons);
    if ((shares_atoms && !domain->hashcons.table) ||
        !arena_try_init_detached(&domain->persistent)) {
        if (shares_atoms) hashcons_free(&domain->hashcons);
        var_intern_free(&domain->variables);
        symbol_table_free(&domain->symbols);
        term_universe_free(&domain->universe);
        free(domain);
        return NULL;
    }
    arena_set_runtime_kind(&domain->persistent,
                            CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&domain->persistent,
                       shares_atoms ? &domain->hashcons : NULL);
    term_universe_set_persistent_arena(&domain->universe, &domain->persistent);
    if (!configured)
        term_universe_set_collection_mode(&domain->universe, mode);
    return domain;
}

CettaTermDomain *cetta_term_domain_new(TermUniverseStoreFormat format,
        TermUniverseCollectionMode mode, bool shares_atoms) {
    return domain_new(format, mode, shares_atoms, false);
}

CettaTermDomain *cetta_term_domain_new_default(bool shares_atoms) {
    return domain_new(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
        TERM_UNIVERSE_COLLECTION_OFF, shares_atoms, true);
}

bool cetta_term_domain_retain(void *context) {
    CettaTermDomain *domain = context;
    if (!domain) return false;
    uint64_t value = atomic_load_explicit(&domain->references,
                                          memory_order_relaxed);
    while (value && value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&domain->references,
                &value, value + 1u, memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    return false;
}

TermUniverse *cetta_term_domain_universe(CettaTermDomain *domain) {
    return domain ? &domain->universe : NULL;
}
SymbolTable *cetta_term_domain_symbols(CettaTermDomain *domain) {
    return domain ? &domain->symbols : NULL;
}
VarInternTable *cetta_term_domain_variables(CettaTermDomain *domain) {
    return domain ? &domain->variables : NULL;
}
HashConsTable *cetta_term_domain_hashcons(CettaTermDomain *domain) {
    return domain && domain->shares_atoms ? &domain->hashcons : NULL;
}
Arena *cetta_term_domain_persistent(CettaTermDomain *domain) {
    return domain ? &domain->persistent : NULL;
}

static void domain_install(CettaTermDomain *domain) {
    g_symbols = &domain->symbols;
    g_var_intern = &domain->variables;
    g_hashcons = domain->shares_atoms ? &domain->hashcons : NULL;
    g_builtin_syms = domain->builtins;
}

bool cetta_term_domain_enter(CettaTermDomain *domain,
                            CettaTermDomainScope *scope) {
    if (!scope || scope->domain || !cetta_term_domain_retain(domain)) return false;
    *scope = (CettaTermDomainScope){.domain = domain, .symbols = g_symbols,
        .variables = g_var_intern, .hashcons = g_hashcons,
        .builtins = g_builtin_syms};
    domain_install(domain);
    return true;
}

static CettaTermDomain *domain_scope_detach(CettaTermDomainScope *scope) {
    if (!scope || !scope->domain) return NULL;
    CettaTermDomain *domain = scope->domain;
    g_symbols = scope->symbols;
    g_var_intern = scope->variables;
    g_hashcons = scope->hashcons;
    g_builtin_syms = scope->builtins;
    scope->domain = NULL;
    return domain;
}

void cetta_term_domain_leave(CettaTermDomainScope *scope) {
    CettaTermDomain *domain = domain_scope_detach(scope);
    if (domain) cetta_term_domain_release(domain);
}

static void domain_destroy(CettaTermDomain *domain,
        void (*dispose)(void *), void *context) {
    /* Resource finalizers see the domain whose objects they are retiring.
     * The final release is serialized with interpreter/session switching. */
    SymbolTable *symbols = g_symbols;
    VarInternTable *variables = g_var_intern;
    HashConsTable *hashcons = g_hashcons;
    BuiltinSyms builtins = g_builtin_syms;
    domain_install(domain);
    term_universe_set_collection_mode(&domain->universe, TERM_UNIVERSE_COLLECTION_OFF);
    if (dispose) dispose(context);
    term_universe_free(&domain->universe);
    arena_free(&domain->persistent);
    if (domain->shares_atoms) hashcons_free(&domain->hashcons);
    var_intern_free(&domain->variables);
    symbol_table_free(&domain->symbols);
    g_symbols = symbols == &domain->symbols ? NULL : symbols;
    g_var_intern = variables == &domain->variables ? NULL : variables;
    g_hashcons = hashcons == &domain->hashcons ? NULL : hashcons;
    g_builtin_syms = symbols == &domain->symbols ? (BuiltinSyms){0} : builtins;
    free(domain);
}

void cetta_term_domain_release(void *context) {
    CettaTermDomain *domain = context;
    if (!domain) return;
    uint64_t before = atomic_fetch_sub_explicit(&domain->references, 1u,
                                                memory_order_acq_rel);
    if (!before) abort();
    if (before == 1u) domain_destroy(domain, NULL, NULL);
}

bool cetta_term_domain_close_if_last(CettaTermDomainScope *scope,
        CettaTermDomain *owned, void (*dispose)(void *), void *context) {
    if (!owned || !scope || scope->domain != owned || !dispose ||
        g_symbols != &owned->symbols || g_var_intern != &owned->variables ||
        g_hashcons != (owned->shares_atoms ? &owned->hashcons : NULL) ||
        __atomic_load_n(&owned->universe.collection_inhibit, __ATOMIC_ACQUIRE) ||
        owned->universe.counted_sweep_active) return false;
    uint64_t expected = 2u;
    if (!atomic_compare_exchange_strong_explicit(&owned->references, &expected, 0u,
            memory_order_acq_rel, memory_order_relaxed)) return false;
    (void)domain_scope_detach(scope);
    domain_destroy(owned, dispose, context);
    return true;
}
