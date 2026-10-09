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

/* A Python module providing an HE module. Executing the file and running its
 * register functions are separate events: upstream executes the file when it
 * finds the module and again when it loads it, runs the register functions
 * once for the module itself and once more for every importer. */
typedef struct CettaForeignHEModule CettaForeignHEModule;

/* One exported name with the atom a reader gives for it. */
typedef struct {
    char *name;
    Atom *atom;
} CettaForeignHEExport;

/* Execute the module file once. With `source`, these bytes are executed as
 * the file's content; without, Python's own import machinery reads the file.
 * `out` may be NULL when only the execution matters. */
bool cetta_foreign_he_module_execute(CettaForeignRuntime *rt,
                                     const char *canonical_path,
                                     const uint8_t *source, size_t length,
                                     Arena *arena,
                                     CettaForeignHEModule **out,
                                     Atom **error_out);
void cetta_foreign_he_module_release(CettaForeignHEModule *module);

/* Run every register function of the module once, in name order. All or
 * nothing: when one of them fails, no export is returned. Atoms are built in
 * `arena`. A name that is a pattern rather than a word is not exported. */
bool cetta_foreign_he_module_exports(CettaForeignHEModule *module, Arena *arena,
                                     CettaForeignHEExport **exports_out,
                                     size_t *count_out, Atom **error_out);
void cetta_foreign_he_exports_free(CettaForeignHEExport *exports, size_t count);

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
