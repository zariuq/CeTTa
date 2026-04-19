#ifndef CETTA_FOREIGN_INTERNAL_H
#define CETTA_FOREIGN_INTERNAL_H

#include "foreign.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#ifndef Py_PYTHON_H
typedef struct _object PyObject;
#endif

typedef struct CettaForeignRegisteredSymbol {
    SymbolId sym_id;
    CettaForeignBackendKind backend;
    bool unwrap;
    char *name;
    struct CettaForeignRegisteredSymbol *next;
} CettaForeignRegisteredSymbol;

typedef struct {
    pid_t pid;
    FILE *to_child;
    FILE *from_child;
    bool started;
    char exec_root[PATH_MAX];
    char bridge_path[PATH_MAX];
} CettaForeignPrologSession;

typedef struct CettaForeignValue {
    CettaForeignBackendKind backend;
    bool callable;
    bool unwrap;
    union {
        PyObject *py_obj;
        char *prolog_name;
        void *ptr;
    } handle;
    struct CettaForeignValue *next;
} CettaForeignValue;

struct CettaForeignRuntime {
    CettaForeignValue *values;
    CettaForeignRegisteredSymbol *registered_symbols;
    CettaForeignPrologSession prolog;
};

void cetta_foreign_prolog_runtime_init(CettaForeignRuntime *rt);
void cetta_foreign_prolog_runtime_cleanup(CettaForeignRuntime *rt);
void cetta_foreign_prolog_set_exec_root(CettaForeignRuntime *rt,
                                        const char *root_dir);
bool cetta_foreign_prolog_is_callable_head(CettaForeignRuntime *rt, Atom *head);
bool cetta_foreign_prolog_call(CettaForeignRuntime *rt,
                               Space *space,
                               Arena *a,
                               Atom *callable,
                               Atom **args,
                               uint32_t nargs,
                               ResultSet *rs,
                               Atom **error_out,
                               bool *handled_out);
Atom *cetta_foreign_prolog_dispatch_native(CettaForeignRuntime *rt,
                                           Space *space,
                                           Arena *a,
                                           Atom *head,
                                           Atom **args,
                                           uint32_t nargs);

#endif /* CETTA_FOREIGN_INTERNAL_H */
