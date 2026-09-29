#ifndef CETTA_LANGDEF_MODULE_H
#define CETTA_LANGDEF_MODULE_H

#include "atom.h"
#include "space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_LANGDEF_MAX_SOURCES 64u
#define CETTA_LANGDEF_MAX_EXTENSION_SOURCES 64u
#define CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS 16u

typedef struct {
    const char *name;
    Atom *start;
    const char *pack_relative;
    bool parser_pack_expected_closed;
    bool parser_pack_closure_set;
    const char *lock_relative;
    const char *program_source_relative;
    const char *program_relative;
    const char *import_entry;
    const char *source_category;
    const char *result_category;
    const char *compiled_cursor_relative;
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    uint32_t source_len;
    const char *extension_sources[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES];
    uint32_t extension_source_len;
    const char *extension_artifact_roles[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS];
    const char *extension_artifact_relatives[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS];
    uint32_t extension_artifact_len;
} CettaLangDefManifestV1;

typedef struct {
    const char *manifest_sha256;
    const char *pack_file_sha256;
    const char *source_digest;
    const char *compiler_digest;
    const char *environment_digest;
    const char *pack_digest;
    const char *program_source_path;
    const char *program_source_sha256;
    const char *program_sha256;
    const char *compiled_cursor_sha256;
    const char *source_paths[CETTA_LANGDEF_MAX_SOURCES];
    const char *source_sha256s[CETTA_LANGDEF_MAX_SOURCES];
    uint32_t source_len;
    const char *extension_source_paths[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES];
    const char *extension_source_sha256s[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES];
    uint32_t extension_source_len;
    const char *extension_artifact_roles[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS];
    const char *extension_artifact_paths[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS];
    const char *extension_artifact_sha256s[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS];
    uint32_t extension_artifact_len;
} CettaLangDefLockV1;

struct CettaLibraryContext;

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
typedef enum {
    CETTA_LANGDEF_RUN_V1_ERROR = 0,
    CETTA_LANGDEF_RUN_V1_ACCEPTED,
    CETTA_LANGDEF_RUN_V1_ACCEPTED_INCOMPLETE,
    CETTA_LANGDEF_RUN_V1_REJECTED,
    CETTA_LANGDEF_RUN_V1_INCOMPLETE,
    CETTA_LANGDEF_RUN_V1_UNSUPPORTED
} CettaLangDefRunStatusV1;

typedef enum {
    CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY = 0,
    CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC = 1,
    CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT = 2
} CettaLangDefProofExecutionV1;

typedef struct {
    CettaLangDefRunStatusV1 status;
    Atom *result;
} CettaLangDefRunReceiptV1;
#endif

bool cetta_langdef_text_arg(Atom *atom, const char **out);
bool cetta_langdef_expr_head(const Atom *atom, const char *head,
                             CettaExprLen arity);
bool cetta_langdef_slurp(const char *path, uint8_t **bytes_out,
                         size_t *len_out, char *error, size_t error_size);
typedef struct CettaDeterministicEquationPlanV1
    CettaDeterministicEquationPlanV1;
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
#include "native/deterministic_equation_plan_v1.h"
#include "native/tptp_official_snapshot_v1.h"
/* The primitives LangDef programs run with: the structural data primitives
 * and the str: operations, each with the class of its value. */
extern const CettaDeterministicVocabularyV1
    cetta_langdef_deterministic_vocabulary_v1;
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
bool cetta_langdef_sha256_file(const char *path, char digest[65],
                               char *error, size_t error_size);
bool cetta_langdef_path_join(const char *base_file, const char *relative,
                             char output[], size_t output_size,
                             char *error, size_t error_size);
Atom *cetta_langdef_read_single_form(const char *path, Arena *arena,
                                     char *error, size_t error_size);
bool cetta_langdef_manifest_parse(Atom *root, CettaLangDefManifestV1 *out,
                                  char *error, size_t error_size);
bool cetta_langdef_lock_parse(Atom *root, CettaLangDefLockV1 *out,
                              char *error, size_t error_size);
bool cetta_langdef_validate_manifest_v1(const char *manifest_path,
                                        char *error, size_t error_size);

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
bool cetta_langdef_resolve_named_manifest_v1(
    const char *exec_path, const char *name,
    char *output, size_t output_size,
    char *error, size_t error_size);
bool cetta_langdef_run_bytes_v1(
    const char *manifest_path,
    const uint8_t *bytes, size_t len,
    const char *source_path,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size);
bool cetta_langdef_run_bytes_with_proof_execution_v1(
    const char *manifest_path,
    const uint8_t *bytes, size_t len,
    const char *source_path,
    CettaLangDefProofExecutionV1 proof_execution,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size);
bool cetta_langdef_run_file_v1(
    const char *manifest_path, const char *source_path,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size);
bool cetta_langdef_run_file_with_proof_execution_v1(
    const char *manifest_path, const char *source_path,
    CettaLangDefProofExecutionV1 proof_execution,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size);
#endif

Atom *cetta_langdef_module_dispatch(struct CettaLibraryContext *ctx,
                                    Space *space, Arena *arena,
                                    Atom *head, Atom **args,
                                    uint32_t nargs);

#endif /* CETTA_LANGDEF_MODULE_H */
