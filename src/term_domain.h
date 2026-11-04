#ifndef CETTA_TERM_DOMAIN_H
#define CETTA_TERM_DOMAIN_H
#include "atom.h"
#include "symbol.h"
#include "term_universe.h"

/* Owns physical term, symbol, variable-name and persistent syntax storage.
 * It owns no Space, producer directory or growing application store. */
typedef struct CettaTermDomain CettaTermDomain;
CettaTermDomain *cetta_term_domain_new(TermUniverseStoreFormat,
    TermUniverseCollectionMode, bool hashcons);
/* Match the ordinary runtime's configured collection policy and format. */
CettaTermDomain *cetta_term_domain_new_default(bool hashcons);
/* These signatures also serve retained-container lifetime callbacks. Each
 * successful retain owns one independent reference; overflow refuses. */
bool cetta_term_domain_retain(void *);
void cetta_term_domain_release(void *);
TermUniverse *cetta_term_domain_universe(CettaTermDomain *);
SymbolTable *cetta_term_domain_symbols(CettaTermDomain *);
VarInternTable *cetta_term_domain_variables(CettaTermDomain *);
HashConsTable *cetta_term_domain_hashcons(CettaTermDomain *);
Arena *cetta_term_domain_persistent(CettaTermDomain *);

/* Symbol, variable and current hash-consing contexts are thread-local.
 * A zero-initialized scope owns a domain reference and restores its predecessor,
 * including builtin IDs. Independent domains can be nested. */
typedef struct {
    CettaTermDomain *domain;
    SymbolTable *symbols;
    VarInternTable *variables;
    HashConsTable *hashcons;
    BuiltinSyms builtins;
} CettaTermDomainScope;
bool cetta_term_domain_enter(CettaTermDomain *, CettaTermDomainScope *);
void cetta_term_domain_leave(CettaTermDomainScope *);
/* The caller owns one reference in addition to the active scope's reference.
 * Consume both only when they are the final references. The disposer runs
 * during logical finalization, with physical storage still live; new retains
 * refuse the closed owner. An inhibited/sweeping TU declines. False leaves
 * both references and the scope intact. */
bool cetta_term_domain_close_if_last(CettaTermDomainScope *, CettaTermDomain *,
    void (*dispose)(void *), void *context);
#endif
