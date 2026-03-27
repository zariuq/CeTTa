#ifndef CETTA_FOREIGN_H
#define CETTA_FOREIGN_H

#include "atom.h"
#include "eval.h"
#include "space.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct CettaForeignRuntime CettaForeignRuntime;

CettaForeignRuntime *cetta_foreign_runtime_new(void);
void cetta_foreign_runtime_free(CettaForeignRuntime *rt);

const char *cetta_module_format_name(CettaModuleFormatKind kind);
const char *cetta_foreign_backend_name(CettaForeignBackendKind kind);

bool cetta_foreign_resolve_candidate(const char *candidate,
                                     char *out, size_t out_sz,
                                     CettaModuleFormat *format_out,
                                     char *reason, size_t reason_sz);

bool cetta_foreign_load_module(CettaForeignRuntime *rt,
                               const char *canonical_path,
                               Space *target_space,
                               Arena *persistent_arena,
                               Atom **error_out);

bool cetta_foreign_is_callable_atom(Atom *atom);

bool cetta_foreign_call(CettaForeignRuntime *rt,
                        Space *space,
                        Arena *a,
                        Atom *callable,
                        Atom **args,
                        uint32_t nargs,
                        ResultSet *rs,
                        Atom **error_out);

Atom *cetta_foreign_dispatch_native(CettaForeignRuntime *rt,
                                    Space *space,
                                    Arena *a,
                                    Atom *head,
                                    Atom **args,
                                    uint32_t nargs);

#endif /* CETTA_FOREIGN_H */
