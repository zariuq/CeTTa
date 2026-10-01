#include "petta_libpl.h"

#include <stdlib.h>

struct CettaLibPrologRuntime {
    bool unavailable;
};

CettaLibPrologRuntime *cetta_lib_prolog_runtime_new(void) {
    CettaLibPrologRuntime *runtime =
        cetta_malloc(sizeof(*runtime));
    runtime->unavailable = true;
    return runtime;
}

void cetta_lib_prolog_runtime_free(
    CettaLibPrologRuntime *runtime) {
    free(runtime);
}

bool cetta_lib_prolog_runtime_set_working_dir(
    CettaLibPrologRuntime *runtime, const char *path) {
    return runtime && path && path[0] != '\0';
}

void cetta_lib_prolog_global_shutdown(void) {
}

bool cetta_lib_prolog_runtime_available(
    CettaLibPrologRuntime *runtime) {
    return runtime && !runtime->unavailable;
}

CettaLibPrologReadToken cetta_lib_prolog_read_token(
    CettaLibPrologRuntime *runtime) {
    (void)runtime;
    return (CettaLibPrologReadToken){0};
}

uint64_t petta_libpl_capability_revision(
    const CettaLibPrologRuntime *runtime) {
    (void)runtime;
    return 0u;
}

CettaLibPrologQueryStatus cetta_lib_prolog_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *goal, Atom *projection, Atom **answers) {
    (void)runtime;
    (void)arena;
    (void)goal;
    (void)projection;
    if (answers)
        *answers = NULL;
    return CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
}

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    (void)runtime;
    (void)head;
    (void)supplied;
    return (PeTTaNamedArity){0};
}

PeTTaNamedArity petta_libpl_named_arity_including_resolved(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    (void)runtime;
    (void)head;
    (void)supplied;
    return (PeTTaNamedArity){0};
}

bool petta_libpl_static_import(
    CettaLibPrologRuntime *runtime, Arena *arena, SymbolId space,
    const char *file, PettaLibplStaticPredicateVisit visit, void *context,
    CettaCallOutcome *end) {
    (void)runtime;
    (void)arena;
    (void)space;
    (void)file;
    (void)visit;
    (void)context;
    if (end)
        *end = cetta_call_failure();
    return false;
}

void petta_libpl_warn_redefined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity) {
    (void)runtime;
    (void)name;
    (void)arity;
}

bool petta_libpl_predicate_defined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity) {
    (void)runtime;
    (void)name;
    (void)arity;
    return false;
}

PeTTaNamedArity petta_libpl_named_arity_resolving(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    (void)runtime;
    (void)head;
    (void)supplied;
    return (PeTTaNamedArity){0};
}

bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized, CettaCallOutcome *end,
    const CettaDelayView *delay) {
    (void)delay;
    if (end)
        *end = cetta_call_failure();
    (void)runtime;
    (void)arena;
    (void)expression;
    (void)expected;
    (void)environment;
    (void)outcomes;
    if (recognized)
        *recognized = false;
    return recognized != NULL;
}
