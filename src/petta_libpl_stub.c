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
    bool *recognized) {
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
