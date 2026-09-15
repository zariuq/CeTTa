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
