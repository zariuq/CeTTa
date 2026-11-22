/* tptp_module.h - the native TPTP module: the reader of the official TPTP
 * grammar, the compact form of what it reads, and that form printed back.
 * Registration (native/native_modules.c, g_native_modules[]):
 *
 *     {"tptp", 0u, NULL, "__cetta_lib_tptp_", ...},
 *
 * keyed by name, so it answers once lib/tptp.metta is imported;
 * lib/lib_tptp.metta imports it and gives the public names (tptp:...).
 * A reader reads by the tables of the official grammar's snapshot
 * (native/tptp_official_snapshot_v1.h); the prepared equations beside them
 * load and run with the LangDef vocabulary (native/langdef_module.h). */
#ifndef CETTA_TPTP_MODULE_H
#define CETTA_TPTP_MODULE_H

#include "atom.h"
#include "space.h"
#include "native/deterministic_equation_plan_v1.h"
#include "native/tptp_official_snapshot_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CettaLibraryContext;

/* The module's operations, each a __cetta_lib_tptp_ head; NULL for any
 * other head. */
Atom *cetta_tptp_module_dispatch(struct CettaLibraryContext *ctx,
                                 Space *space, Arena *arena,
                                 Atom *head, Atom **args, uint32_t nargs);

/* The prepared equations of a reader's compact-v1 directory beside
 * snapshot_path, listed with their SHA-256 digests in manifest_name, or NULL
 * with the reason: no manifest, a listed file whose digest differs, or
 * equations the runner does not admit. */
CettaDeterministicEquationPlanV1 *cetta_tptp_compact_manifest_plan_v1(
    const char *snapshot_path, const char *manifest_name,
    char *reason, size_t reason_size);

/* The two stages after a TPTP read, as tptp:read and tptp:print run them:
 * the compact records of a canonical file term under a leaf policy
 * (tagged, mixed or native), and those records printed back to text.  The
 * corpus tools call the same functions.  Printing uses the given canonical
 * table of the reader's grammar, or with NULL the reader's own. */
typedef enum {
    CETTA_TPTP_COMPACT_PRINT_INVERSE_V1 = 0,
    CETTA_TPTP_COMPACT_PRINT_NOT_RECORDS_V1,
    CETTA_TPTP_COMPACT_PRINT_INPUT_V1,
    CETTA_TPTP_COMPACT_PRINT_MEMORY_V1
} CettaTptpCompactPrintFailureV1;
bool cetta_tptp_compact_records_v1(
    const CettaDeterministicEquationPlanV1 *compact, const char *policy,
    Atom *canonical, Arena *arena, Atom **records_out,
    uint64_t *work_used_out, CettaDeterministicEquationStatusV1 *status_out,
    char *error, size_t error_size);
/* The constructors the compact morphism maps to their own child's value,
 * which printing puts back.  The caller frees *wrappers_out. */
bool cetta_tptp_compact_wrappers_v1(
    const CettaDeterministicEquationPlanV1 *compact,
    SymbolId **wrappers_out, uint32_t *len_out);
bool cetta_tptp_compact_print_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const CettaGrammarCanonicalTableV1 *table,
    const CettaDeterministicEquationPlanV1 *print,
    const SymbolId *wrappers, uint32_t wrapper_len,
    const Atom *records, Arena *arena,
    char **text_out, size_t *len_out,
    CettaTptpCompactPrintFailureV1 *failure_out, uint64_t *index_out,
    Atom **culprit_out, CettaDeterministicEquationStatusV1 *status_out,
    uint64_t *work_used_out, char *error, size_t error_size);

#endif /* CETTA_TPTP_MODULE_H */
