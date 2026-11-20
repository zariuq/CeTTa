#ifndef CETTA_LANGDEF_MODULE_H
#define CETTA_LANGDEF_MODULE_H

#include "atom.h"
#include "space.h"
#include "native/deterministic_equation_plan_v1.h"

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
/* The primitives LangDef programs run with: the structural data primitives
 * and the str: operations, each with the class of its value. */
extern const CettaDeterministicVocabularyV1
    cetta_langdef_deterministic_vocabulary_v1;
/* The deterministic equations a digest manifest lists, loaded with the
 * LangDef vocabulary.  Each line of the manifest is "<sha256> <file>", a file
 * beside the manifest, in load order.  NULL with the reason when there is no
 * manifest, it lists no file or more than CETTA_LANGDEF_MAX_EXTENSION_SOURCES,
 * a listed file no longer has its digest (it is stale), or the runner does
 * not admit the equations.  A language module ships its prepared equations
 * this way. */
CettaDeterministicEquationPlanV1 *
cetta_langdef_deterministic_equations_load_manifest_v1(
    const char *manifest_path, char *reason, size_t reason_size);
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

/* A native's value in each dialect's return protocol: HE and Prime take it
 * as (return value) in their function bodies; PeTTa, which has no return,
 * takes the value itself.  LangDef's natives answer by it, and so do those of
 * a language module whose library wraps them as it wraps LangDef's. */
Atom *cetta_langdef_return(Arena *arena, Atom *value);

Atom *cetta_langdef_module_dispatch(struct CettaLibraryContext *ctx,
                                    Space *space, Arena *arena,
                                    Atom *head, Atom **args,
                                    uint32_t nargs);

/* A native composition that consumes a parse observation. The observation
 * and its replayed values are borrowed for the duration of the call; the
 * consumer returns its own result, allocated in arena. */
typedef Atom *(*CettaLangdefParseConsumerV1)(void *context, Arena *arena, Atom *observation);
/* Parse with the named baked parser exactly as langdef's selected and
 * forest-selected text observations do (unbounded work, optional selection,
 * optional packed forest), handing the observation to consume. The context
 * retains one restored instance of each baked parser it uses. NULL reports
 * that the parser itself could not be obtained. */
Atom *cetta_langdef_baked_parse_consume_v1(
    struct CettaLibraryContext *ctx, const char *name,
    const uint8_t *bytes, size_t len, Atom *selection, bool include_forest,
    Arena *arena, CettaLangdefParseConsumerV1 consume, void *consume_context,
    char *error, size_t error_size);

#endif /* CETTA_LANGDEF_MODULE_H */
