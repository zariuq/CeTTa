#ifndef CETTA_FOREIGN_H
#define CETTA_FOREIGN_H

#include "atom.h"
#include "call_outcome.h"
#include "eval.h"
#include "space.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct CettaForeignRuntime CettaForeignRuntime;

CettaForeignRuntime *cetta_foreign_runtime_new(void);
void cetta_foreign_runtime_free(CettaForeignRuntime *rt);
/* Drop the Python references of the foreign records no arena holds any more.
 * A record is freed as soon as its last hold goes, possibly during an arena
 * reset; its object is released here, at a point where running Python code
 * is safe: after each top-level form and when a foreign call begins. */
void cetta_foreign_drain_releases(void);
void cetta_foreign_global_shutdown(void);

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

/* Exact occurrence-valued dispatch for the symbolic Python convenience
 * syntax forms.  `true` means the head was recognized; `results` then contains
 * zero, one, or many occurrences, and `end` says how the call ended
 * (call_outcome.h): FAILURE once its answers are given, or RAISED with the
 * error of an operational failure, which PeTTa raises.  HE and Prime answer
 * with that error as an Error occurrence instead.  No Empty sentinel or
 * tuple encoding is introduced. */
bool cetta_foreign_dispatch_native_results(CettaForeignRuntime *rt,
                                           Space *space,
                                           Arena *a,
                                           Atom *head,
                                           Atom **args,
                                           uint32_t nargs,
                                           ResultSet *results,
                                           CettaCallOutcome *end);

/* The same dispatch with its answers collapsed into one value: `out` is
 * that VALUE, or the RAISED error.  False when `head` is not one of the
 * forms. */
bool cetta_foreign_call_native(CettaForeignRuntime *rt,
                               Space *space,
                               Arena *a,
                               Atom *head,
                               Atom **args,
                               uint32_t nargs,
                               CettaCallOutcome *out);

#endif /* CETTA_FOREIGN_H */
