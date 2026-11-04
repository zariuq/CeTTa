#ifndef CETTA_RUNTIME_NAMES_H
#define CETTA_RUNTIME_NAMES_H

#include "atom.h"
#include <errno.h>
#include <stdlib.h>

/* An engine owns spellings and variable names. Execution borrows that domain
 * on its current thread; nested entry restores the previous domain. */
typedef struct {
    SymbolTable symbols;
    VarInternTable variables;
    BuiltinSyms builtins;
} CettaNameDomain;

typedef struct {
    SymbolTable *symbols;
    VarInternTable *variables;
    BuiltinSyms builtins;
} CettaNameContext;

static inline CettaNameContext cetta_name_context_current(void) {
    return (CettaNameContext){g_symbols, g_var_intern, g_builtin_syms};
}

static inline void cetta_name_context_install(const CettaNameContext *context) {
    g_symbols = context->symbols;
    g_var_intern = context->variables;
    g_builtin_syms = context->builtins;
}

static inline CettaNameContext cetta_name_domain_context(CettaNameDomain *domain) {
    return (CettaNameContext){&domain->symbols, &domain->variables, domain->builtins};
}

/* Failure releases the completed stages. A failed domain owns no storage;
 * successful domains retire through cetta_name_domain_free. */
static inline bool cetta_name_domain_try_init(CettaNameDomain *domain) {
    if (!symbol_table_try_init(&domain->symbols)) return false;
    if (!symbol_table_try_init_builtins(&domain->symbols, &domain->builtins) ||
        !var_intern_try_init(&domain->variables)) {
        int error = errno;
        symbol_table_free(&domain->symbols);
        memset(&domain->builtins, 0, sizeof(domain->builtins));
        errno = error;
        return false;
    }
    return true;
}

static inline void cetta_name_domain_init(CettaNameDomain *domain) {
    if (!cetta_name_domain_try_init(domain)) {
        perror("fatal: runtime name initialization");
        abort();
    }
}

static inline void cetta_name_domain_free(CettaNameDomain *domain) {
    var_intern_free(&domain->variables);
    symbol_table_free(&domain->symbols);
}

#endif
