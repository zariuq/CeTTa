#define _XOPEN_SOURCE 700

#include "native/langdef_module.h"

#include "native_sha256.h"
#include "parser.h"

#include "parser_pack_abi_stream_v1.h"
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
#include "library.h"
#include "native/language_def_contextual_runner_v1.h"
#include "native/language_def_core_v1.h"
#include "native/language_def_ground_term_v1.h"
#include "native/language_def_parser_pack_v1.h"
#include "native/language_def_pattern_atom_v1.h"
#include "native/deterministic_equation_plan_v1.h"
#include "native/operational_language_def_v1.h"
#include "native/structural_tree_relabel_v1.h"
#include "native_handle.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"
#endif
#if !defined(CETTA_LANGDEF_ARTIFACT_ONLY) && !defined(CETTA_NO_STDLIB)
#define CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME 1
#include "native/langdef_compiled_cursor_v1.h"
#include "finite_horn_answer_stream_v1.h"
#include "parser_pack_guard_evidence_stream_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"
#include "parser_occurrence_file_resolver_v1.h"
#include "parser_occurrence_source_composition_v1.h"
#include "first_order_frame_decoder_v1.h"
#include "oslf_native_type_plan_v1.h"
#include "oslf_native_type_vm_v1.h"
#include "certificate_gslt_plan_v1.h"
#include "certificate_gslt_relational_assertion_v1.h"
#include "certificate_gslt_relational_machine_v1.h"
#include "certificate_gslt_relational_runtime_v1.h"
#include "certificate_gslt_sequence_evidence_v1.h"
#include "proof_storage_plan_v1.h"
#include <dlfcn.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LANGDEF_HANDLE_KIND "cetta.langdef.v1"
#define LANGUAGE_DEF_TERM_HANDLE_KIND "cetta.language-def-term.v1"
#define AUTHORED_PARSER_HANDLE_KIND "cetta.authored-parser.v1"
#define STRUCTURAL_TREE_RELABEL_HANDLE_KIND \
    "cetta.structural-tree-relabel.v1"
#define DETERMINISTIC_EQUATION_HANDLE_KIND \
    "cetta.deterministic-equations.v1"
#define LANGDEF_MINIMUM_WORK_LIMIT UINT64_C(4000000)
#define LANGDEF_WORK_PER_SOURCE_BYTE UINT64_C(64)
#define LANGDEF_DEFAULT_REPLAY_DEPTH 4096u
#define LANGDEF_DEFAULT_RESULT_LIMIT 65536u
#define LANGDEF_DEFAULT_INCLUDE_DEPTH 4096u
#define LANGDEF_DEFAULT_EQUATION_CONTINUATIONS 65536u
#define LANGDEF_DEFAULT_PROOF_RULE_ATTEMPTS UINT64_C(100000000)
#define LANGDEF_GENERATED_PROOF_GOAL_DEPTH UINT32_MAX

typedef struct {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    char *name;
    char *manifest_path;
    char *pack_path;
    char *program_path;
    char *import_entry;
    char *source_category;
    char *result_category;
    char *compiled_cursor_path;
    char *proof_machine_native_types_path;
    char *proof_generated_runtime_path;
    char manifest_sha256[65];
    char pack_file_sha256[65];
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
    CettaOperationalLanguageDefV1 source_language_wire;
    CettaLanguageDefCoreV1 source_language;
    bool source_language_ready;
    CettaOperationalLanguageDefV1 result_language_wire;
    CettaLanguageDefCoreV1 result_language;
    bool result_language_ready;
#endif
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    void *compiled_cursor_handle;
    const CettaLangDefCompiledCursorV1 *compiled_cursor;
    PPGuardedLexCursorV1Program compiled_program;
    PPOccurrenceFoldV1Plan compiled_fold;
    PPOccurrenceSpanMaskV1Plan compiled_span_mask;
    PPRelationalStateProgramV1Plan compiled_state;
    PPSourceResolutionControlV1Plan compiled_source_control;
    FHAnswerStreamV1 parser_lexical_answers;
    FHAnswerStreamV1 parser_guard_answers;
    FHAnswerStreamV1 parser_guarded_answers;
    RSNFAV1Plan parser_lexical_nfa;
    RSNFAV1Plan parser_guard_nfa;
    PPLexV1Plan parser_lexical_plan;
    PPGuardEvidenceWireV1 parser_guard_evidence;
    PPGuardPlanV1 parser_guard_plan;
    PPGuardedLexV1Plan parser_guarded_plan;
    PPGuardedLexExecV1Plan parser_guarded_exec;
    PPCertificateGSLTPlanV1 proof_plan;
    PPCertificateGSLTSequenceEvidenceABIV1 proof_evidence;
    PPCertificateGSLTRelationalAssertionPlanV1 proof_relational;
    PPOSLFNativeTypePlanV1 proof_native_types;
    PPOSLFNativeTypePlanV1 proof_machine_native_types;
    PPOSLFNativeTypeVMV1 proof_machine_vm;
    PPCertificateGSLTRelationalRuntimeV1 proof_generated_runtime;
    PPProofStoragePlanV1 proof_storage_plan;
    PPCertificateGSLTRelationalMachineV1Workspace proof_workspace;
    const PPProofPreparedActionCaseV1 *proof_exact_action_cases;
    uint32_t proof_exact_action_case_len;
    PPFirstOrderFrameDecoderV1 *proof_frame_decoders;
    PPRelationalStackProofV1CacheAdmission *proof_frame_cache_admissions;
    uint32_t proof_frame_decoder_len;
    bool compiled_program_ready;
    bool compiled_fold_ready;
    bool compiled_span_mask_ready;
    bool compiled_state_ready;
    bool compiled_source_control_ready;
    bool parser_guarded_exec_initialized;
    bool parser_guarded_exec_ready;
    bool proof_plan_ready;
    bool proof_evidence_ready;
    bool proof_relational_ready;
    bool proof_native_types_ready;
    bool proof_machine_native_types_ready;
    bool proof_machine_vm_ready;
    bool proof_generated_runtime_ready;
    bool proof_storage_plan_ready;
    bool proof_workspace_ready;
    bool proof_frame_decoders_ready;
    bool proof_frame_cache_admissions_ready;
    bool proof_extension_ready;
#endif
} CettaLangDefV1;

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
typedef struct {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program contextual_program;
    CettaLdCrV1Status contextual_status;
    bool contextual_ready;
    char contextual_error[256];
} CettaLanguageDefTermV1;

typedef struct {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Document profile_document;
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 compiled;
    PPNativeV1Prepared prepared;
    char *name;
} CettaAuthoredParserV1;

typedef struct {
    CettaLdTextV1 relation;
    uint64_t receipt_id;
    CettaLdPatternV1 *tuple;
    uint32_t tuple_len;
} CettaLanguageDefRelationRowV1;

typedef struct {
    CettaLanguageDefRelationRowV1 *rows;
    uint32_t len;
} CettaLanguageDefRelationEnvV1;
#endif

static bool langdef_set_error(char *buffer, size_t size,
                              const char *format, ...);

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
static Atom *langdef_error(Arena *arena, Atom *source, const char *message) {
    return atom_error(arena, source, atom_string(arena, message));
}

static void language_def_term_resource_free(void *opaque) {
    CettaLanguageDefTermV1 *resource = opaque;

    if (!resource)
        return;
    cetta_language_def_core_v1_free(&resource->language);
    cetta_op_lang_v1_free(&resource->wire);
    free(resource);
}

static void authored_parser_resource_free(void *opaque) {
    CettaAuthoredParserV1 *resource = opaque;

    if (!resource)
        return;
    free(resource->name);
    ppnative_v1_prepared_free(&resource->prepared);
    cetta_ld_parser_pack_v1_free(&resource->compiled);
    cetta_ld_parser_profile_v1_free(&resource->profile);
    cetta_op_lang_v1_document_free(&resource->profile_document);
    cetta_language_def_core_v1_free(&resource->language);
    cetta_op_lang_v1_free(&resource->wire);
    free(resource);
}

static void language_def_relation_env_free(
    CettaLanguageDefRelationEnvV1 *environment) {
    uint32_t row_index;

    if (!environment)
        return;
    for (row_index = 0u; row_index < environment->len; row_index++) {
        CettaLanguageDefRelationRowV1 *row = &environment->rows[row_index];
        uint32_t item_index;
        free(row->relation.bytes);
        for (item_index = 0u; item_index < row->tuple_len; item_index++)
            cetta_ld_pattern_v1_free(&row->tuple[item_index]);
        free(row->tuple);
    }
    free(environment->rows);
    memset(environment, 0, sizeof(*environment));
}

static void structural_tree_relabel_resource_free(void *opaque) {
    cetta_structural_tree_relabel_v1_free(opaque);
}

static bool structural_tree_relabel_resource_load_composition(
    const char *composition_path, const char *entry_operator,
    const char *label_operator, CettaStructuralTreeRelabelV1 **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error, size_t error_size) {
    Arena composition_arena;
    Atom *root;
    char resolved[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX];
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_count = 0u;
    bool saw_name = false;
    bool ok = false;

    if (out)
        *out = NULL;
    if (status)
        *status = CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
    if (!composition_path || !entry_operator || !label_operator || !out) {
        langdef_set_error(
            error, error_size,
            "invalid structural tree relabel composition request");
        return false;
    }

    arena_init(&composition_arena);
    root = cetta_langdef_read_single_form(
        composition_path, &composition_arena, error, error_size);
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 3u ||
        !atom_is_symbol(root->expr.elems[0], "gslt-composition-v1")) {
        if (root)
            langdef_set_error(
                error, error_size,
                "structural tree relabel composition is malformed");
        if (status)
            *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
        goto done;
    }
    for (CettaExprIndex index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char *value = NULL;

        if (cetta_langdef_expr_head(field, "name", 1u)) {
            if (saw_name ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0') {
                langdef_set_error(
                    error, error_size,
                    "structural tree relabel composition has an invalid name");
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
                goto done;
            }
            saw_name = true;
        } else if (cetta_langdef_expr_head(field, "source", 1u)) {
            if (source_count >= CETTA_LANGDEF_MAX_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0') {
                langdef_set_error(
                    error, error_size,
                    "structural tree relabel composition has an invalid source");
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
                goto done;
            }
            if (!cetta_langdef_path_join(
                    composition_path, value, resolved[source_count],
                    sizeof(resolved[source_count]), error, error_size)) {
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
                goto done;
            }
            for (size_t prior = 0u; prior < source_count; prior++) {
                if (strcmp(resolved[prior], resolved[source_count]) == 0) {
                    langdef_set_error(
                        error, error_size,
                        "structural tree relabel composition repeats a source");
                    if (status)
                        *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
                    goto done;
                }
            }
            sources[source_count] = resolved[source_count];
            source_count++;
        } else {
            langdef_set_error(
                error, error_size,
                "structural tree relabel composition has an unknown field");
            if (status)
                *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
            goto done;
        }
    }
    if (!saw_name || source_count == 0u) {
        langdef_set_error(
            error, error_size,
            "structural tree relabel composition requires a name and sources");
        if (status)
            *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
        goto done;
    }
    ok = cetta_structural_tree_relabel_v1_load_paths(
        sources, source_count, entry_operator, label_operator,
        out, status, error, error_size);

done:
    arena_free(&composition_arena);
    return ok;
}

static void deterministic_equation_resource_free(void *opaque) {
    cetta_deterministic_equation_plan_v1_free(opaque);
}

static CettaLanguageDefTermV1 *language_def_term_resource_load(
    const char *path, char *error, size_t error_size) {
    CettaLanguageDefTermV1 *resource;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;

    if (!path) {
        langdef_set_error(error, error_size,
                          "language definition path is missing");
        return NULL;
    }
    resource = calloc(1u, sizeof(*resource));
    if (!resource) {
        langdef_set_error(error, error_size,
                          "language definition allocation failed");
        return NULL;
    }
    cetta_op_lang_v1_init(&resource->wire);
    cetta_language_def_core_v1_init(&resource->language);
    cetta_ld_cr_v1_program_init(&resource->contextual_program);
    if (!cetta_op_lang_v1_parse_file(
            &resource->wire, path, 4000000u, 8000000u,
            &wire_status, error, error_size) ||
        !cetta_language_def_core_v1_decode(
            &resource->language, &resource->wire, 200000u,
            &core_status, error, error_size)) {
        language_def_term_resource_free(resource);
        return NULL;
    }
    resource->contextual_status = CETTA_LD_CR_V1_OK;
    resource->contextual_ready = cetta_ld_cr_v1_compile(
        &resource->contextual_program, &resource->language,
        &resource->contextual_status, resource->contextual_error,
        sizeof(resource->contextual_error));
    return resource;
}

static CettaAuthoredParserV1 *authored_parser_resource_load(
    const char *language_path, const char *profile_path,
    char *error, size_t error_size) {
    CettaAuthoredParserV1 *resource;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    CettaLdParserPackV1Status parser_status =
        CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;

    if (!language_path || !profile_path) {
        langdef_set_error(
            error, error_size,
            "authored parser language and profile paths are required");
        return NULL;
    }
    resource = calloc(1u, sizeof(*resource));
    if (!resource) {
        langdef_set_error(error, error_size,
                          "authored parser allocation failed");
        return NULL;
    }
    cetta_op_lang_v1_init(&resource->wire);
    cetta_language_def_core_v1_init(&resource->language);
    cetta_op_lang_v1_document_init(&resource->profile_document);
    cetta_ld_parser_profile_v1_init(&resource->profile);
    cetta_ld_parser_pack_v1_init(&resource->compiled);
    ppnative_v1_prepared_init(&resource->prepared);

    if (!cetta_op_lang_v1_parse_file(
            &resource->wire, language_path, 4000000u, 8000000u,
            &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size, "authored LanguageDef parse status: %s",
                cetta_op_lang_v1_status_name(wire_status));
        }
        goto failed;
    }
    if (!cetta_language_def_core_v1_decode(
            &resource->language, &resource->wire, 500000u,
            &core_status, error, error_size) ||
        core_status != CETTA_LD_CORE_V1_OK) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size, "authored LanguageDef status: %s",
                cetta_ld_core_v1_status_name(core_status));
        }
        goto failed;
    }
    if (!cetta_op_lang_v1_parse_document_file(
            &resource->profile_document, profile_path,
            1000000u, 2000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size, "authored parser profile status: %s",
                cetta_op_lang_v1_status_name(wire_status));
        }
        goto failed;
    }
    if (!cetta_ld_parser_profile_v1_decode(
            &resource->profile, &resource->profile_document, 100000u,
            &parser_status, error, error_size) ||
        parser_status != CETTA_LD_PARSER_PACK_V1_OK) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size, "authored parser profile decode: %s",
                cetta_ld_parser_pack_v1_status_name(parser_status));
        }
        goto failed;
    }
    if (!cetta_language_def_parser_pack_v1_compile(
            &resource->compiled, &resource->language,
            resource->wire.source_sha256, &resource->profile,
            2000000u, &parser_status, error, error_size) ||
        parser_status != CETTA_LD_PARSER_PACK_V1_OK) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size, "authored parser compile status: %s",
                cetta_ld_parser_pack_v1_status_name(parser_status));
        }
        goto failed;
    }
    if (!ppnative_v1_prepare(
            &resource->prepared, &resource->compiled.pack,
            resource->compiled.start_state, error, error_size)) {
        goto failed;
    }
    resource->name = malloc((size_t)resource->language.name.len + 1u);
    if (!resource->name) {
        langdef_set_error(error, error_size,
                          "authored parser name allocation failed");
        goto failed;
    }
    if (resource->language.name.len > 0u) {
        memcpy(resource->name, resource->language.name.bytes,
               resource->language.name.len);
    }
    resource->name[resource->language.name.len] = '\0';
    return resource;

failed:
    authored_parser_resource_free(resource);
    return NULL;
}
#endif

static bool langdef_set_error(char *buffer, size_t size,
                              const char *format, ...) {
    va_list arguments;
    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
static uint64_t langdef_source_work_limit(size_t source_len) {
    if (source_len >
        (UINT64_MAX - LANGDEF_MINIMUM_WORK_LIMIT) /
            LANGDEF_WORK_PER_SOURCE_BYTE)
        return UINT64_MAX;
    return LANGDEF_MINIMUM_WORK_LIMIT +
           (uint64_t)source_len * LANGDEF_WORK_PER_SOURCE_BYTE;
}

static uint32_t langdef_source_work_limit_u32(size_t source_len) {
    uint64_t limit = langdef_source_work_limit(source_len);
    return limit > UINT32_MAX ? UINT32_MAX : (uint32_t)limit;
}
#endif

bool cetta_langdef_text_arg(Atom *atom, const char **out) {
    if (!atom || !out)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        *out = atom->ground.sval;
        return true;
    }
    if (atom->kind == ATOM_SYMBOL) {
        *out = atom_name_cstr(atom);
        return *out != NULL;
    }
    return false;
}

bool cetta_langdef_expr_head(const Atom *atom, const char *head,
                             CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static char *langdef_text_dup(const char *text) {
    size_t len;
    char *copy;
    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text, len + 1u);
    return copy;
}

bool cetta_langdef_slurp(const char *path, uint8_t **bytes_out,
                         size_t *len_out, char *error, size_t error_size) {
    FILE *stream = NULL;
    uint8_t *bytes = NULL;
    long end;
    size_t len;
    bool ok = false;

    if (!path || !bytes_out || !len_out)
        return langdef_set_error(error, error_size, "invalid file request");
    stream = fopen(path, "rb");
    if (!stream)
        return langdef_set_error(error, error_size,
                                 "cannot open %s: %s", path,
                                 strerror(errno));
    if (fseek(stream, 0, SEEK_END) != 0 || (end = ftell(stream)) < 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        langdef_set_error(error, error_size, "cannot size %s", path);
        goto done;
    }
    len = (size_t)end;
    if ((long)len != end) {
        langdef_set_error(error, error_size, "file is too large: %s", path);
        goto done;
    }
    bytes = malloc(len + 1u);
    if (!bytes) {
        langdef_set_error(error, error_size, "out of memory reading %s", path);
        goto done;
    }
    if (len > 0u && fread(bytes, 1u, len, stream) != len) {
        langdef_set_error(error, error_size, "cannot read %s", path);
        goto done;
    }
    bytes[len] = 0u;
    *bytes_out = bytes;
    *len_out = len;
    bytes = NULL;
    ok = true;

done:
    free(bytes);
    fclose(stream);
    return ok;
}

bool cetta_langdef_sha256_file(const char *path, char digest[65],
                               char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    bool ok = cetta_langdef_slurp(path, &bytes, &len, error, error_size);
    if (ok)
        cetta_native_sha256_hex(bytes, len, digest);
    free(bytes);
    return ok;
}

static bool langdef_digest_text(Atom *atom, const char **out) {
    const char *text;
    size_t index;
    if (!cetta_langdef_text_arg(atom, &text) || strlen(text) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return false;
    }
    *out = text;
    return true;
}

bool cetta_langdef_path_join(const char *base_file, const char *relative,
                             char output[], size_t output_size,
                             char *error, size_t error_size) {
    const char *slash;
    size_t directory_len;
    int written;
    char candidate[PATH_MAX];
    char *resolved;

    if (!base_file || !relative)
        return langdef_set_error(error, error_size, "invalid relative path");
    if (relative[0] == '/') {
        written = snprintf(candidate, sizeof(candidate), "%s", relative);
    } else {
        slash = strrchr(base_file, '/');
        directory_len = slash ? (size_t)(slash - base_file) : 1u;
        if (slash) {
            written = snprintf(candidate, sizeof(candidate), "%.*s/%s",
                               (int)directory_len, base_file, relative);
        } else {
            written = snprintf(candidate, sizeof(candidate), "./%s", relative);
        }
    }
    if (written < 0 || (size_t)written >= sizeof(candidate))
        return langdef_set_error(error, error_size, "resolved path is too long");
    resolved = realpath(candidate, NULL);
    if (!resolved)
        return langdef_set_error(error, error_size,
                                 "cannot resolve %s: %s", candidate,
                                 strerror(errno));
    if (strlen(resolved) >= output_size) {
        free(resolved);
        return langdef_set_error(error, error_size, "resolved path is too long");
    }
    strcpy(output, resolved);
    free(resolved);
    return true;
}

Atom *cetta_langdef_read_single_form(const char *path, Arena *arena,
                                     char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    Atom **forms = NULL;
    Atom *result = NULL;
    int count;

    if (!cetta_langdef_slurp(path, &bytes, &len, error, error_size))
        return NULL;
    if (memchr(bytes, 0, len) != NULL) {
        langdef_set_error(error, error_size, "%s contains a NUL byte", path);
        goto done;
    }
    count = parse_metta_text((const char *)bytes, arena, &forms);
    if (count != 1) {
        langdef_set_error(error, error_size,
                          "%s must contain exactly one form", path);
        goto done;
    }
    result = forms[0];

done:
    free(forms);
    free(bytes);
    return result;
}

bool cetta_langdef_manifest_parse(Atom *root, CettaLangDefManifestV1 *out,
                                  char *error, size_t error_size) {
    CettaExprIndex index;
    memset(out, 0, sizeof(*out));
    out->parser_pack_expected_closed = true;
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 2u ||
        !atom_is_symbol(root->expr.elems[0], "gslt-langdef-v1")) {
        return langdef_set_error(error, error_size,
                                 "manifest root must be gslt-langdef-v1");
    }
    for (index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char *value;
        if (cetta_langdef_expr_head(field, "name", 1u)) {
            if (out->name || !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid name");
            out->name = value;
        } else if (cetta_langdef_expr_head(field, "start", 1u)) {
            if (out->start)
                return langdef_set_error(error, error_size,
                                         "manifest duplicates start");
            out->start = field->expr.elems[1];
        } else if (cetta_langdef_expr_head(field, "parser-pack", 1u)) {
            if (out->pack_relative ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid parser-pack");
            out->pack_relative = value;
        } else if (cetta_langdef_expr_head(
                       field, "parser-pack-closure", 1u)) {
            Atom *closure = field->expr.elems[1];
            if (out->parser_pack_closure_set || !closure ||
                closure->kind != ATOM_SYMBOL ||
                !(value = atom_name_cstr(closure)) ||
                (strcmp(value, "closed") != 0 &&
                 strcmp(value, "partial") != 0))
                return langdef_set_error(
                    error, error_size,
                    "manifest has invalid parser-pack-closure");
            out->parser_pack_expected_closed =
                strcmp(value, "closed") == 0;
            out->parser_pack_closure_set = true;
        } else if (cetta_langdef_expr_head(field, "lock", 1u)) {
            if (out->lock_relative ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid lock");
            out->lock_relative = value;
        } else if (cetta_langdef_expr_head(field, "program-source", 1u)) {
            if (out->program_source_relative ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid program-source");
            out->program_source_relative = value;
        } else if (cetta_langdef_expr_head(field, "program", 1u)) {
            if (out->program_relative ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid program");
            out->program_relative = value;
        } else if (cetta_langdef_expr_head(field, "import-entry", 1u)) {
            Atom *entry = field->expr.elems[1];
            if (out->import_entry || !entry || entry->kind != ATOM_SYMBOL ||
                !(value = atom_name_cstr(entry)))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid import-entry");
            out->import_entry = value;
        } else if (cetta_langdef_expr_head(field, "result-category", 1u)) {
            if (out->result_category ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return langdef_set_error(
                    error, error_size,
                    "manifest has invalid result-category");
            out->result_category = value;
        } else if (cetta_langdef_expr_head(field, "source-category", 1u)) {
            if (out->source_category ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return langdef_set_error(
                    error, error_size,
                    "manifest has invalid source-category");
            out->source_category = value;
        } else if (cetta_langdef_expr_head(field, "compiled-cursor", 1u)) {
            if (out->compiled_cursor_relative ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(
                    error, error_size,
                    "manifest has invalid compiled-cursor");
            out->compiled_cursor_relative = value;
        } else if (cetta_langdef_expr_head(field, "source", 1u)) {
            if (out->source_len >= CETTA_LANGDEF_MAX_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value))
                return langdef_set_error(error, error_size,
                                         "manifest has invalid source list");
            out->sources[out->source_len++] = value;
        } else if (cetta_langdef_expr_head(
                       field, "extension-source", 1u)) {
            if (out->extension_source_len >=
                    CETTA_LANGDEF_MAX_EXTENSION_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return langdef_set_error(
                    error, error_size,
                    "manifest has invalid extension-source list");
            for (uint32_t source_index = 0u;
                 source_index < out->extension_source_len;
                 source_index++) {
                if (strcmp(out->extension_sources[source_index], value) == 0)
                    return langdef_set_error(
                        error, error_size,
                        "manifest repeats an extension source");
            }
            out->extension_sources[out->extension_source_len++] = value;
        } else if (cetta_langdef_expr_head(
                       field, "extension-artifact", 2u)) {
            const char *role;
            const char *relative;
            if (out->extension_artifact_len >=
                    CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS ||
                !cetta_langdef_text_arg(field->expr.elems[1], &role) ||
                !cetta_langdef_text_arg(field->expr.elems[2], &relative) ||
                role[0] == '\0' || relative[0] == '\0')
                return langdef_set_error(
                    error, error_size,
                    "manifest has an invalid extension artifact");
            for (uint32_t artifact_index = 0u;
                 artifact_index < out->extension_artifact_len;
                 artifact_index++) {
                if (strcmp(out->extension_artifact_roles[artifact_index],
                           role) == 0)
                    return langdef_set_error(
                        error, error_size,
                        "manifest repeats an extension artifact role");
            }
            out->extension_artifact_roles[
                out->extension_artifact_len] = role;
            out->extension_artifact_relatives[
                out->extension_artifact_len] = relative;
            out->extension_artifact_len++;
        }
    }
    if (!out->name || !out->start || !out->pack_relative ||
        !out->lock_relative || out->source_len == 0u) {
        return langdef_set_error(error, error_size,
                                 "manifest omits name, start, parser-pack, lock, or sources");
    }
    if ((out->program_source_relative == NULL) !=
        (out->program_relative == NULL))
        return langdef_set_error(
            error, error_size,
            "manifest must pair program-source with program");
    if ((out->program_relative == NULL) != (out->import_entry == NULL))
        return langdef_set_error(
            error, error_size,
            "manifest must pair a generated program with import-entry");
    {
        bool has_source_language = false;
        bool has_result_language = false;
        uint32_t artifact_index;
        for (artifact_index = 0u;
             artifact_index < out->extension_artifact_len;
             artifact_index++) {
            if (strcmp(out->extension_artifact_roles[artifact_index],
                       "source-language-def-v1") == 0) {
                has_source_language = true;
            }
            if (strcmp(out->extension_artifact_roles[artifact_index],
                       "result-language-def-v1") == 0) {
                has_result_language = true;
                break;
            }
        }
        if (has_source_language != (out->source_category != NULL))
            return langdef_set_error(
                error, error_size,
                "manifest must pair source-language-def-v1 with source-category");
        if (has_source_language && !out->import_entry)
            return langdef_set_error(
                error, error_size,
                "source-language-def-v1 requires a generated import entry");
        if (has_result_language != (out->result_category != NULL))
            return langdef_set_error(
                error, error_size,
                "manifest must pair result-language-def-v1 with result-category");
    }
    return true;
}

bool cetta_langdef_lock_parse(Atom *root, CettaLangDefLockV1 *out,
                              char *error, size_t error_size) {
    CettaExprIndex index;
    memset(out, 0, sizeof(*out));
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 2u ||
        !atom_is_symbol(root->expr.elems[0], "gslt-langdef-lock-v1")) {
        return langdef_set_error(error, error_size,
                                 "lock root must be gslt-langdef-lock-v1");
    }
    for (index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char **slot = NULL;
        if (cetta_langdef_expr_head(field, "manifest-sha256", 1u))
            slot = &out->manifest_sha256;
        else if (cetta_langdef_expr_head(field, "parser-pack-sha256", 1u))
            slot = &out->pack_file_sha256;
        else if (cetta_langdef_expr_head(field, "source-digest", 1u))
            slot = &out->source_digest;
        else if (cetta_langdef_expr_head(field, "compiler-digest", 1u))
            slot = &out->compiler_digest;
        else if (cetta_langdef_expr_head(field, "environment-digest", 1u))
            slot = &out->environment_digest;
        else if (cetta_langdef_expr_head(field, "pack-digest", 1u))
            slot = &out->pack_digest;
        else if (cetta_langdef_expr_head(field, "program-sha256", 1u))
            slot = &out->program_sha256;
        else if (cetta_langdef_expr_head(field,
                                         "compiled-cursor-sha256", 1u))
            slot = &out->compiled_cursor_sha256;
        else if (cetta_langdef_expr_head(field, "program-source-sha256", 2u)) {
            if (out->program_source_path || out->program_source_sha256 ||
                !cetta_langdef_text_arg(field->expr.elems[1],
                                        &out->program_source_path) ||
                !langdef_digest_text(field->expr.elems[2],
                                     &out->program_source_sha256))
                return langdef_set_error(
                    error, error_size,
                    "lock has an invalid program-source digest");
            continue;
        }
        else if (cetta_langdef_expr_head(field, "source-sha256", 2u)) {
            const char *source_path;
            const char *source_digest;
            if (out->source_len >= CETTA_LANGDEF_MAX_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &source_path) ||
                !langdef_digest_text(field->expr.elems[2], &source_digest)) {
                return langdef_set_error(error, error_size,
                                         "lock has an invalid source digest");
            }
            out->source_paths[out->source_len] = source_path;
            out->source_sha256s[out->source_len] = source_digest;
            out->source_len++;
            continue;
        }
        else if (cetta_langdef_expr_head(
                     field, "extension-source-sha256", 2u)) {
            const char *source_path;
            const char *source_digest;
            if (out->extension_source_len >=
                    CETTA_LANGDEF_MAX_EXTENSION_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1],
                                        &source_path) ||
                !langdef_digest_text(field->expr.elems[2],
                                     &source_digest))
                return langdef_set_error(
                    error, error_size,
                    "lock has an invalid extension-source digest");
            for (uint32_t source_index = 0u;
                 source_index < out->extension_source_len;
                 source_index++) {
                if (strcmp(out->extension_source_paths[source_index],
                           source_path) == 0)
                    return langdef_set_error(
                        error, error_size,
                        "lock repeats an extension-source digest");
            }
            out->extension_source_paths[out->extension_source_len] =
                source_path;
            out->extension_source_sha256s[out->extension_source_len] =
                source_digest;
            out->extension_source_len++;
            continue;
        }
        else if (cetta_langdef_expr_head(
                     field, "extension-artifact-sha256", 3u)) {
            const char *role;
            const char *artifact_path;
            const char *artifact_digest;
            if (out->extension_artifact_len >=
                    CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS ||
                !cetta_langdef_text_arg(field->expr.elems[1], &role) ||
                !cetta_langdef_text_arg(field->expr.elems[2],
                                        &artifact_path) ||
                !langdef_digest_text(field->expr.elems[3],
                                     &artifact_digest))
                return langdef_set_error(
                    error, error_size,
                    "lock has an invalid extension-artifact digest");
            for (uint32_t artifact_index = 0u;
                 artifact_index < out->extension_artifact_len;
                 artifact_index++) {
                if (strcmp(out->extension_artifact_roles[artifact_index],
                           role) == 0)
                    return langdef_set_error(
                        error, error_size,
                        "lock repeats an extension-artifact role");
            }
            out->extension_artifact_roles[
                out->extension_artifact_len] = role;
            out->extension_artifact_paths[
                out->extension_artifact_len] = artifact_path;
            out->extension_artifact_sha256s[
                out->extension_artifact_len] = artifact_digest;
            out->extension_artifact_len++;
            continue;
        }
        if (slot) {
            if (*slot || !langdef_digest_text(field->expr.elems[1], slot))
                return langdef_set_error(error, error_size,
                                         "lock has an invalid digest field");
        }
    }
    if (!out->manifest_sha256 || !out->pack_file_sha256 ||
        !out->source_digest || !out->compiler_digest ||
        !out->environment_digest || !out->pack_digest ||
        out->source_len == 0u) {
        return langdef_set_error(error, error_size,
                                 "lock omits a required digest");
    }
    if ((out->program_source_path == NULL) !=
            (out->program_source_sha256 == NULL) ||
        (out->program_source_path == NULL) !=
            (out->program_sha256 == NULL))
        return langdef_set_error(
            error, error_size,
            "lock has an incomplete program digest closure");
    return true;
}

static void langdef_resource_free(void *raw_resource) {
    CettaLangDefV1 *resource = raw_resource;
    if (!resource)
        return;
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    if (resource->proof_generated_runtime_ready) {
        const char *profile_requested =
            getenv("CETTA_LANGDEF_OSLF_PROFILE_V1");
        PPCertificateGSLTRelationalRuntimeV1Profile profile;
        if (profile_requested && profile_requested[0] != '\0' &&
            ppcertificate_gslt_relational_runtime_v1_profile(
                &resource->proof_generated_runtime, &profile)) {
            fprintf(
                stderr,
                "langdef-oslf-profile-v1 queries=%llu goals=%llu "
                "attempts=%llu matches=%llu continuations=%llu "
                "tail-reuses=%llu collections=%llu goal-roots=%llu "
                "view-goals=%llu view-attempts=%llu "
                "view-matches=%llu view-fallbacks=%llu "
                "ground-attempts=%llu ground-matches=%llu "
                "linear-attempts=%llu linear-fallbacks=%llu "
                "epoch-materializations=%llu "
                "epoch-not-admitted=%llu epoch-stale=%llu "
                "epoch-open-producer=%llu epoch-unsafe-consumer=%llu "
                "non-epoch-materialization-attempts=%llu "
                "shape-attempts=%llu shape-rejections=%llu "
                "rigid-dispatches=%llu rigid-rejections=%llu "
                "indexed-visits=%llu full-scan-visits=%llu "
                "raw-tail=%llu proven-tail=%llu "
                "deferred-shape=%llu "
                "binding-collections=%llu binding-roots=%llu "
                "binding-items-discarded=%llu trail-discarded=%llu "
                "copied=%llu reclaimed=%llu materialize=%llu "
                "match-bytes=%llu expand-bytes=%llu nodes=%llu "
                "rollback-reclaimed=%llu dense-nodes=%llu "
                "dense-materializations=%llu dense-reused=%llu "
                "dense-view-nodes=%llu dense-view-resolutions=%llu "
                "dense-view-deferrals=%llu "
                "compiled-relation-dispatches=%llu "
                "compiled-relation-matches=%llu "
                "compiled-relation-deferrals=%llu "
                "max-frames=%u max-goal-depth=%u\n",
                (unsigned long long)profile.query_executions,
                (unsigned long long)profile.stats.goals_entered,
                (unsigned long long)profile.stats.rule_attempts,
                (unsigned long long)profile.stats.rule_matches,
                (unsigned long long)profile.stats.generated_continuations,
                (unsigned long long)profile.stats.generated_tail_frame_reuses,
                (unsigned long long)profile.stats.deterministic_tail_collections,
                (unsigned long long)profile.stats.deterministic_goal_roots_scanned,
                (unsigned long long)
                    profile.stats.activation_view_goal_admissions,
                (unsigned long long)
                    profile.stats.activation_view_rule_attempts,
                (unsigned long long)
                    profile.stats.activation_view_rule_matches,
                (unsigned long long)
                    profile.stats.activation_view_fallback_materializations,
                (unsigned long long)
                    profile.stats.ground_pattern_rule_attempts,
                (unsigned long long)
                    profile.stats.ground_pattern_rule_matches,
                (unsigned long long)
                    profile.stats.positional_linear_rule_attempts,
                (unsigned long long)
                    profile.stats.positional_linear_rule_fallbacks,
                (unsigned long long)
                    profile.stats.deferred_epoch_goal_materializations,
                (unsigned long long)
                    profile.stats.epoch_goal_materializations_not_admitted,
                (unsigned long long)
                    profile.stats.epoch_goal_materializations_stale,
                (unsigned long long)
                    profile.stats
                        .epoch_goal_materializations_not_range_restricted,
                (unsigned long long)
                    profile.stats.epoch_goal_materializations_consumer_unsafe,
                (unsigned long long)
                    profile.stats.non_epoch_goal_materialization_attempts,
                (unsigned long long)
                    profile.stats.structural_shape_guard_attempts,
                (unsigned long long)
                    profile.stats.structural_shape_guard_rejections,
                (unsigned long long)
                    profile.stats.rigid_coordinate_dispatches,
                (unsigned long long)
                    profile.stats.rigid_coordinate_rejections,
                (unsigned long long)
                    profile.stats.indexed_candidate_visits,
                (unsigned long long)
                    profile.stats.full_scan_candidate_visits,
                (unsigned long long)
                    profile.stats.generated_raw_tail_deterministic_continuations,
                (unsigned long long)
                    profile.stats.generated_tail_deterministic_continuations,
                (unsigned long long)
                    profile.stats.deferred_shape_guard_attempts,
                (unsigned long long)profile.stats.deterministic_binding_collections,
                (unsigned long long)profile.stats.deterministic_binding_roots_scanned,
                (unsigned long long)profile.stats.deterministic_binding_items_discarded,
                (unsigned long long)profile.stats.deterministic_trail_entries_discarded,
                (unsigned long long)profile.stats.deterministic_arena_bytes_copied,
                (unsigned long long)profile.stats.deterministic_arena_bytes_reclaimed,
                (unsigned long long)profile.stats.goal_materialization_arena_bytes,
                (unsigned long long)profile.stats.generated_match_arena_bytes,
                (unsigned long long)profile.stats.body_expansion_arena_bytes,
                (unsigned long long)profile.stats.pending_goal_node_arena_bytes,
                (unsigned long long)profile.stats.rollback_arena_bytes_reclaimed,
                (unsigned long long)profile.stats.ground_dense_match_nodes,
                (unsigned long long)
                    profile.stats.ground_dense_expression_materializations,
                (unsigned long long)
                    profile.stats.ground_dense_rigid_subtrees_reused,
                (unsigned long long)
                    profile.stats.ground_dense_view_nodes,
                (unsigned long long)
                    profile.stats.ground_dense_view_variable_resolutions,
                (unsigned long long)
                    profile.stats.ground_dense_view_deferrals,
                (unsigned long long)
                    profile.stats.compiled_relation_dispatches,
                (unsigned long long)
                    profile.stats.compiled_relation_matches,
                (unsigned long long)
                    profile.stats.compiled_relation_deferrals,
                profile.stats.maximum_search_frame_depth,
                profile.stats.maximum_goal_depth);
        }
        ppcertificate_gslt_relational_runtime_v1_free(
            &resource->proof_generated_runtime);
    }
    if (resource->proof_machine_vm_ready)
        pposlf_native_type_vm_v1_free(&resource->proof_machine_vm);
    if (resource->proof_machine_native_types_ready)
        pposlf_native_type_plan_v1_free(
            &resource->proof_machine_native_types);
    if (resource->proof_frame_cache_admissions_ready)
        free(resource->proof_frame_cache_admissions);
    if (resource->proof_frame_decoders_ready)
        free(resource->proof_frame_decoders);
    if (resource->proof_workspace_ready)
        ppcertificate_gslt_relational_machine_v1_workspace_free(
            &resource->proof_workspace);
    if (resource->proof_storage_plan_ready)
        ppproof_storage_plan_v1_free(&resource->proof_storage_plan);
    if (resource->proof_native_types_ready)
        pposlf_native_type_plan_v1_free(&resource->proof_native_types);
    if (resource->proof_relational_ready)
        ppcertificate_gslt_relational_assertion_v1_free(
            &resource->proof_relational);
    if (resource->proof_evidence_ready)
        ppcertificate_gslt_sequence_evidence_abi_v1_free(
            &resource->proof_evidence);
    if (resource->proof_plan_ready)
        ppcertificate_gslt_plan_v1_free(&resource->proof_plan);
    if (resource->parser_guarded_exec_initialized) {
        ppguarded_lex_exec_v1_plan_free(
            &resource->parser_guarded_exec);
        ppguarded_lex_v1_plan_free(&resource->parser_guarded_plan);
        ppguard_plan_v1_free(&resource->parser_guard_plan);
        ppguard_evidence_wire_v1_free(
            &resource->parser_guard_evidence);
        pplex_v1_plan_free(&resource->parser_lexical_plan);
        rsnfa_v1_plan_free(&resource->parser_guard_nfa);
        rsnfa_v1_plan_free(&resource->parser_lexical_nfa);
        fh_answer_stream_v1_free(
            &resource->parser_guarded_answers);
        fh_answer_stream_v1_free(&resource->parser_guard_answers);
        fh_answer_stream_v1_free(
            &resource->parser_lexical_answers);
    }
    if (resource->compiled_state_ready)
        pprelational_state_program_v1_plan_free(&resource->compiled_state);
    if (resource->compiled_span_mask_ready)
        ppoccurrence_span_mask_v1_plan_free(
            &resource->compiled_span_mask);
    if (resource->compiled_fold_ready)
        ppoccurrence_fold_v1_plan_free(&resource->compiled_fold);
    if (resource->compiled_program_ready)
        ppguarded_lex_cursor_v1_program_free(&resource->compiled_program);
    if (resource->compiled_cursor_handle)
        (void)dlclose(resource->compiled_cursor_handle);
#endif
    ppabi_v1_pack_free(&resource->pack);
    ppabi_v1_wire_free(&resource->wire);
    free(resource->name);
    free(resource->manifest_path);
    free(resource->pack_path);
    free(resource->program_path);
    free(resource->import_entry);
    free(resource->source_category);
    free(resource->result_category);
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
    cetta_language_def_core_v1_free(&resource->source_language);
    cetta_op_lang_v1_free(&resource->source_language_wire);
    cetta_language_def_core_v1_free(&resource->result_language);
    cetta_op_lang_v1_free(&resource->result_language_wire);
#endif
    free(resource->compiled_cursor_path);
    free(resource->proof_machine_native_types_path);
    free(resource->proof_generated_runtime_path);
    free(resource);
}

#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
static bool langdef_compiled_cursor_load(CettaLangDefV1 *resource,
                                         const char *path,
                                         char *error,
                                         size_t error_size) {
    CettaLangDefCompiledCursorEntryV1 entry;
    const char *descriptor_program_digest;
    const char *descriptor_fold_digest;
    const char *descriptor_span_mask_digest = NULL;
    const char *descriptor_state_digest = NULL;
    const char *descriptor_source_control_digest = NULL;
    const char *dynamic_error;

    if (!resource || !path)
        return langdef_set_error(error, error_size,
                                 "invalid compiled cursor request");
    resource->compiled_cursor_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!resource->compiled_cursor_handle) {
        dynamic_error = dlerror();
        return langdef_set_error(
            error, error_size, "cannot load compiled cursor: %s",
            dynamic_error ? dynamic_error : "dynamic loader failure");
    }
    (void)dlerror();
    entry = (CettaLangDefCompiledCursorEntryV1)dlsym(
        resource->compiled_cursor_handle,
        CETTA_LANGDEF_COMPILED_CURSOR_V1_SYMBOL);
    dynamic_error = dlerror();
    if (dynamic_error || !entry)
        return langdef_set_error(
            error, error_size, "compiled cursor omits its descriptor: %s",
            dynamic_error ? dynamic_error : "missing entry point");
    resource->compiled_cursor = entry();
    if (!resource->compiled_cursor ||
        resource->compiled_cursor->abi_version !=
            CETTA_LANGDEF_COMPILED_CURSOR_V1_ABI ||
        !resource->compiled_cursor->program_init ||
        !resource->compiled_cursor->program_digest ||
        !resource->compiled_cursor->occurrence_fold_init ||
        !resource->compiled_cursor->occurrence_fold_digest ||
        ((resource->compiled_cursor->occurrence_span_mask_init == NULL) !=
         (resource->compiled_cursor->occurrence_span_mask_digest == NULL)) ||
        ((resource->compiled_cursor->relational_state_init == NULL) !=
         (resource->compiled_cursor->relational_state_digest == NULL)) ||
        ((resource->compiled_cursor->source_control_init == NULL) !=
         (resource->compiled_cursor->source_control_digest == NULL)))
        return langdef_set_error(error, error_size,
                                 "compiled cursor has an invalid descriptor");

    ppguarded_lex_cursor_v1_program_init(&resource->compiled_program);
    resource->compiled_program_ready = true;
    ppoccurrence_fold_v1_plan_init(&resource->compiled_fold);
    resource->compiled_fold_ready = true;
    if (!resource->compiled_cursor->program_init(
            &resource->compiled_program, error, error_size) ||
        !resource->compiled_cursor->occurrence_fold_init(
            &resource->compiled_program, &resource->compiled_fold,
            error, error_size))
        return false;
    descriptor_program_digest =
        resource->compiled_cursor->program_digest();
    descriptor_fold_digest =
        resource->compiled_cursor->occurrence_fold_digest();
    if (!descriptor_program_digest || !descriptor_fold_digest ||
        strcmp(descriptor_program_digest,
               resource->compiled_program.program_digest) != 0 ||
        strcmp(descriptor_fold_digest,
               resource->compiled_fold.plan_digest) != 0 ||
        strcmp(resource->compiled_program.base_pack_digest,
               resource->pack.pack_digest) != 0 ||
        strcmp(resource->compiled_fold.base_pack_digest,
               resource->pack.pack_digest) != 0 ||
        !ppoccurrence_fold_v1_plan_validate_program(
            &resource->compiled_program, &resource->compiled_fold,
            error, error_size))
        return langdef_set_error(
            error, error_size,
            "compiled cursor disagrees with its parser pack or fold plan");
    if (resource->compiled_cursor->occurrence_span_mask_init) {
        ppoccurrence_span_mask_v1_plan_init(
            &resource->compiled_span_mask);
        resource->compiled_span_mask_ready = true;
        if (!resource->compiled_cursor->occurrence_span_mask_init(
                &resource->compiled_program, &resource->compiled_fold,
                &resource->compiled_span_mask, error, error_size)) {
            return false;
        }
        descriptor_span_mask_digest =
            resource->compiled_cursor->occurrence_span_mask_digest();
        if (!descriptor_span_mask_digest ||
            strcmp(descriptor_span_mask_digest,
                   resource->compiled_span_mask.plan_digest) != 0 ||
            !ppoccurrence_span_mask_v1_plan_validate(
                &resource->compiled_program, &resource->compiled_fold,
                &resource->compiled_span_mask, error, error_size)) {
            return langdef_set_error(
                error, error_size,
                "compiled occurrence span mask disagrees with its fold plan");
        }
    }
    if (resource->compiled_cursor->relational_state_init) {
        pprelational_state_program_v1_plan_init(&resource->compiled_state);
        resource->compiled_state_ready = true;
        if (!resource->compiled_cursor->relational_state_init(
                &resource->compiled_fold, &resource->compiled_state,
                error, error_size))
            return false;
        descriptor_state_digest =
            resource->compiled_cursor->relational_state_digest();
        if (!descriptor_state_digest ||
            strcmp(descriptor_state_digest,
                   resource->compiled_state.plan_digest) != 0 ||
            !pprelational_state_program_v1_plan_validate(
                &resource->compiled_fold, &resource->compiled_state,
                error, error_size))
            return langdef_set_error(
                error, error_size,
                "compiled state program disagrees with its fold plan");
    }
    if (resource->compiled_cursor->source_control_init) {
        ppsource_resolution_control_v1_plan_init(
            &resource->compiled_source_control);
        resource->compiled_source_control_ready = true;
        if (!resource->compiled_cursor->source_control_init(
                &resource->compiled_source_control, error, error_size))
            return false;
        descriptor_source_control_digest =
            resource->compiled_cursor->source_control_digest();
        if (!descriptor_source_control_digest ||
            strcmp(descriptor_source_control_digest,
                   resource->compiled_source_control.plan_digest) != 0 ||
            !ppsource_resolution_control_v1_plan_validate(
                &resource->compiled_source_control, error, error_size)) {
            return langdef_set_error(
                error, error_size,
                "compiled source control disagrees with its generated plan");
        }
    }
    return true;
}

static int32_t langdef_extension_artifact_find(
    const CettaLangDefManifestV1 *manifest, const char *role) {
    uint32_t index;
    if (!manifest || !role)
        return -1;
    for (index = 0u; index < manifest->extension_artifact_len; index++) {
        if (strcmp(manifest->extension_artifact_roles[index], role) == 0)
            return (int32_t)index;
    }
    return -1;
}

static PPGuardedLexExecV1Limits langdef_guarded_parser_limits(void) {
    return (PPGuardedLexExecV1Limits){
        .dfa_state_limit = UINT32_C(65536),
        .dfa_transition_limit = UINT32_C(2000000),
        .scan_work_limit = UINT64_C(20000000),
        .scan_token_limit = UINT32_C(2000000),
        .witness_work_limit = UINT32_C(10000000),
        .parse_work_limit = UINT32_C(50000000),
        .replay_depth = LANGDEF_DEFAULT_REPLAY_DEPTH,
        .result_limit = UINT32_C(1000000),
    };
}

static bool langdef_guarded_parser_extension_load(
    CettaLangDefV1 *resource,
    const CettaLangDefManifestV1 *manifest,
    const char artifact_paths[][PATH_MAX],
    char *error, size_t error_size) {
    PPGuardPlanV1ProvenanceInput provenance;
    PPGuardedLexExecV1Limits limits;
    char regular_compiler_digest[65];
    char guarded_compiler_digest[65];
    int32_t lexical_index;
    int32_t guard_index;
    int32_t evidence_index;
    int32_t guarded_index;
    int32_t regular_compiler_index;
    int32_t guarded_compiler_index;

    lexical_index = langdef_extension_artifact_find(
        manifest, "parser-lexical-nfa-v1");
    guard_index = langdef_extension_artifact_find(
        manifest, "parser-guard-nfa-v1");
    evidence_index = langdef_extension_artifact_find(
        manifest, "parser-positive-guard-evidence-v1");
    guarded_index = langdef_extension_artifact_find(
        manifest, "parser-guarded-nfa-v1");
    regular_compiler_index = langdef_extension_artifact_find(
        manifest, "parser-regular-span-compiler-v1");
    guarded_compiler_index = langdef_extension_artifact_find(
        manifest, "parser-guarded-span-compiler-v1");

    if (lexical_index < 0 && guard_index < 0 && evidence_index < 0 &&
        guarded_index < 0 && regular_compiler_index < 0 &&
        guarded_compiler_index < 0) {
        if (!manifest->parser_pack_expected_closed)
            return langdef_set_error(
                error, error_size,
                "partial parser pack lacks a guarded execution bundle");
        return true;
    }
    if (manifest->parser_pack_expected_closed || lexical_index < 0 ||
        guard_index < 0 || evidence_index < 0 || guarded_index < 0 ||
        regular_compiler_index < 0 || guarded_compiler_index < 0) {
        return langdef_set_error(
            error, error_size,
            "langdef declares an incomplete guarded parser bundle");
    }

    fh_answer_stream_v1_init(&resource->parser_lexical_answers);
    fh_answer_stream_v1_init(&resource->parser_guard_answers);
    fh_answer_stream_v1_init(&resource->parser_guarded_answers);
    rsnfa_v1_plan_init(&resource->parser_lexical_nfa);
    rsnfa_v1_plan_init(&resource->parser_guard_nfa);
    pplex_v1_plan_init(&resource->parser_lexical_plan);
    ppguard_evidence_wire_v1_init(&resource->parser_guard_evidence);
    ppguard_plan_v1_init(&resource->parser_guard_plan);
    ppguarded_lex_v1_plan_init(&resource->parser_guarded_plan);
    ppguarded_lex_exec_v1_plan_init(&resource->parser_guarded_exec);
    resource->parser_guarded_exec_initialized = true;

    if (!cetta_langdef_sha256_file(
            artifact_paths[regular_compiler_index],
            regular_compiler_digest, error, error_size) ||
        !cetta_langdef_sha256_file(
            artifact_paths[guarded_compiler_index],
            guarded_compiler_digest, error, error_size) ||
        !fh_answer_stream_v1_read(
            &resource->parser_lexical_answers,
            artifact_paths[lexical_index],
            error, error_size) ||
        resource->parser_lexical_answers.len == 0u ||
        resource->parser_lexical_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &resource->pack, resource->parser_lexical_answers.terms,
            resource->parser_lexical_answers.len,
            &resource->parser_lexical_nfa, error, error_size) ||
        !pplex_v1_plan_build(
            &resource->pack, resource->parser_lexical_nfa.tags,
            resource->parser_lexical_nfa.nfa.tag_len,
            regular_compiler_digest,
            resource->parser_lexical_answers.digest,
            &resource->parser_lexical_plan,
            error, error_size) ||
        !fh_answer_stream_v1_read(
            &resource->parser_guard_answers,
            artifact_paths[guard_index],
            error, error_size) ||
        resource->parser_guard_answers.len == 0u ||
        resource->parser_guard_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &resource->pack, resource->parser_guard_answers.terms,
            resource->parser_guard_answers.len,
            &resource->parser_guard_nfa, error, error_size) ||
        !ppguard_evidence_wire_v1_read(
            &resource->parser_guard_evidence,
            artifact_paths[evidence_index],
            error, error_size) ||
        !fh_answer_stream_v1_read(
            &resource->parser_guarded_answers,
            artifact_paths[guarded_index],
            error, error_size)) {
        return false;
    }
    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = resource->parser_guard_evidence.source_digest,
        .pre_reflection_digest =
            resource->parser_guard_evidence.pre_reflection_digest,
        .environment_digest =
            resource->parser_guard_evidence.environment_digest,
        .answer_set_digest =
            resource->parser_guard_evidence.answer_set_digest,
        .regular_compiler_digest = regular_compiler_digest,
        .guard_nfa_answer_digest =
            resource->parser_guard_answers.digest,
        .guard_nfa_tags = resource->parser_guard_nfa.tags,
        .guard_nfa_tag_len = resource->parser_guard_nfa.nfa.tag_len,
        .derivations = resource->parser_guard_evidence.derivations,
        .derivation_len =
            resource->parser_guard_evidence.derivation_len,
    };
    limits = langdef_guarded_parser_limits();
    if (!ppguard_plan_v1_build(
            &resource->pack, &resource->parser_lexical_plan,
            &provenance, &resource->parser_guard_plan,
            error, error_size) ||
        !ppguarded_lex_v1_plan_build(
            &resource->pack, &resource->parser_lexical_plan,
            &resource->parser_guard_plan,
            resource->parser_guarded_answers.terms,
            resource->parser_guarded_answers.len,
            guarded_compiler_digest,
            resource->parser_guarded_answers.digest,
            &resource->parser_guarded_plan,
            error, error_size) ||
        !ppguarded_lex_exec_v1_plan_build(
            &resource->pack, resource->wire.start,
            &resource->parser_lexical_plan,
            &resource->parser_guard_plan,
            &resource->parser_guarded_plan,
            &resource->parser_lexical_nfa,
            &resource->parser_guard_nfa, &limits,
            &resource->parser_guarded_exec,
            error, error_size)) {
        return false;
    }
    resource->parser_guarded_exec_ready = true;
    return true;
}

static bool langdef_proof_extension_load(
    CettaLangDefV1 *resource,
    const CettaLangDefManifestV1 *manifest,
    const char artifact_paths[][PATH_MAX],
    char *error, size_t error_size) {
    PPCertificateGSLTArticleV1Limits limits;
    int32_t plan_index;
    int32_t evidence_index;
    int32_t relational_index;
    int32_t native_types_index;
    int32_t machine_native_types_index;
    int32_t generated_runtime_index;
    int32_t storage_plan_index;
    uint32_t proof_machine_index;
    uint32_t exact_action_machine_matches = 0u;

    if (!resource || !manifest)
        return langdef_set_error(error, error_size,
                                 "invalid langdef extension request");
    if (manifest->extension_artifact_len == 0u) {
        if (manifest->extension_source_len != 0u)
            return langdef_set_error(
                error, error_size,
                "langdef has extension sources but no executable artifacts");
        return true;
    }
    plan_index = langdef_extension_artifact_find(
        manifest, "proof-plan-v1");
    evidence_index = langdef_extension_artifact_find(
        manifest, "proof-evidence-v1");
    relational_index = langdef_extension_artifact_find(
        manifest, "proof-relational-v1");
    native_types_index = langdef_extension_artifact_find(
        manifest, "proof-semantic-ntt-v1");
    machine_native_types_index = langdef_extension_artifact_find(
        manifest, "proof-machine-ntt-v1");
    generated_runtime_index = langdef_extension_artifact_find(
        manifest, "proof-relational-runtime-v1");
    storage_plan_index = langdef_extension_artifact_find(
        manifest, "proof-storage-plan-v1");
    if (plan_index < 0 && evidence_index < 0 && relational_index < 0 &&
        native_types_index < 0 && machine_native_types_index < 0 &&
        generated_runtime_index < 0 && storage_plan_index < 0)
        return true;
    if (manifest->extension_source_len == 0u || plan_index < 0 ||
        evidence_index < 0 || relational_index < 0)
        return langdef_set_error(
            error, error_size,
            "langdef declares an unsupported or incomplete extension bundle");
    if ((native_types_index < 0) != (storage_plan_index < 0))
        return langdef_set_error(
            error, error_size,
            "langdef declares an incomplete native frame bundle");
    if ((machine_native_types_index < 0) != (generated_runtime_index < 0))
        return langdef_set_error(
            error, error_size,
            "langdef declares an incomplete generated proof runtime bundle");
    if (!resource->compiled_state_ready)
        return langdef_set_error(
            error, error_size,
            "proof extension requires a compiled relational state program");

    limits = ppcertificate_gslt_article_v1_default_limits();
    ppcertificate_gslt_plan_v1_init(&resource->proof_plan);
    resource->proof_plan_ready = true;
    if (ppcertificate_gslt_plan_v1_load(
            &resource->proof_plan, artifact_paths[plan_index], &limits,
            error, error_size) != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return false;
    ppcertificate_gslt_sequence_evidence_abi_v1_init(
        &resource->proof_evidence);
    resource->proof_evidence_ready = true;
    if (ppcertificate_gslt_sequence_evidence_abi_v1_load(
            &resource->proof_evidence, artifact_paths[evidence_index],
            &resource->proof_plan, error, error_size) !=
        PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return false;
    ppcertificate_gslt_relational_assertion_v1_init(
        &resource->proof_relational);
    resource->proof_relational_ready = true;
    if (ppcertificate_gslt_relational_assertion_v1_load(
            &resource->proof_relational,
            artifact_paths[relational_index], &resource->proof_plan,
            &resource->compiled_state, error, error_size) !=
        PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return false;
    if (native_types_index >= 0) {
        pposlf_native_type_plan_v1_init(&resource->proof_native_types);
        resource->proof_native_types_ready = true;
        if (!pposlf_native_type_plan_v1_load(
                &resource->proof_native_types,
                artifact_paths[native_types_index], error, error_size))
            return false;
        ppproof_storage_plan_v1_init(&resource->proof_storage_plan);
        resource->proof_storage_plan_ready = true;
        if (!ppproof_storage_plan_v1_load(
                &resource->proof_storage_plan,
                artifact_paths[storage_plan_index], error, error_size))
            return false;
        if (resource->proof_storage_plan.machine_len !=
            resource->compiled_state.proof_machine_len)
            return langdef_set_error(
                error, error_size,
                "native frame plan and compiled state name different machines");
        for (proof_machine_index = 0u;
             proof_machine_index <
                 resource->compiled_state.proof_machine_len;
             proof_machine_index++) {
            const PPProofPreparedActionCaseV1 *action_cases = NULL;
            uint32_t action_case_len = 0u;
            if (!ppproof_storage_plan_v1_exact_action_selector(
                    &resource->proof_storage_plan,
                    &resource->compiled_state, proof_machine_index,
                    &action_cases, &action_case_len,
                    error, error_size))
                return false;
            if (resource->proof_relational.execution.machine &&
                resource->compiled_state.proof_machines[
                    proof_machine_index].name &&
                strcmp(resource->proof_relational.execution.machine,
                       resource->compiled_state.proof_machines[
                           proof_machine_index].name) == 0) {
                if (exact_action_machine_matches != 0u)
                    return langdef_set_error(
                        error, error_size,
                        "proof runtime names more than one exact action selector");
                resource->proof_exact_action_cases = action_cases;
                resource->proof_exact_action_case_len = action_case_len;
                exact_action_machine_matches++;
            }
        }
        if (exact_action_machine_matches != 1u ||
            !resource->proof_exact_action_cases ||
            resource->proof_exact_action_case_len == 0u)
            return langdef_set_error(
                error, error_size,
                "proof runtime lacks its admitted exact action selector");
    }
    if (machine_native_types_index >= 0) {
        resource->proof_machine_native_types_path = langdef_text_dup(
            artifact_paths[machine_native_types_index]);
        resource->proof_generated_runtime_path = langdef_text_dup(
            artifact_paths[generated_runtime_index]);
        if (!resource->proof_machine_native_types_path ||
            !resource->proof_generated_runtime_path)
            return langdef_set_error(
                error, error_size,
                "cannot retain generated proof runtime artifact paths");
    }
    if (!ppcertificate_gslt_relational_machine_v1_workspace_init(
            &resource->proof_workspace))
        return langdef_set_error(
            error, error_size,
            "cannot initialize proof-call workspace");
    if (resource->proof_storage_plan_ready &&
        resource->proof_storage_plan.repetition_cache_len != 0u &&
        !ppcertificate_gslt_relational_machine_v1_workspace_set_repetition_policy(
            &resource->proof_workspace,
            CETTA_GSLT_REPETITION_POLICY_SECOND_OCCURRENCE_V1)) {
        ppcertificate_gslt_relational_machine_v1_workspace_free(
            &resource->proof_workspace);
        return langdef_set_error(
            error, error_size,
            "cannot apply generated proof repetition-cache policy");
    }
    resource->proof_workspace_ready = true;
    resource->proof_extension_ready = true;
    return true;
}

static bool langdef_generated_proof_prepare(
    CettaLangDefV1 *resource, char *error, size_t error_size) {
    PPOSLFNativeTypePlanV1 native_types;
    PPOSLFNativeTypeVMV1 vm;
    PPCertificateGSLTRelationalRuntimeV1 runtime;
    const PPOSLFNativeVMLimitsV1 vm_limits = {
        .maximum_rule_attempts = LANGDEF_DEFAULT_PROOF_RULE_ATTEMPTS,
        .maximum_goal_depth = LANGDEF_GENERATED_PROOF_GOAL_DEPTH,
    };
    bool native_types_ready = false;
    bool vm_ready = false;
    bool runtime_ready = false;
    bool ok = false;

    if (!resource || !resource->compiled_state_ready ||
        !resource->proof_machine_native_types_path ||
        !resource->proof_generated_runtime_path)
        return langdef_set_error(
            error, error_size,
            "generated relational proof backend lacks a sealed program");
    if (resource->proof_machine_native_types_ready ||
        resource->proof_machine_vm_ready ||
        resource->proof_generated_runtime_ready) {
        if (!resource->proof_machine_native_types_ready ||
            !resource->proof_machine_vm_ready ||
            !resource->proof_generated_runtime_ready)
            return langdef_set_error(
                error, error_size,
                "generated relational proof backend is partially prepared");
        return true;
    }

    pposlf_native_type_plan_v1_init(&native_types);
    native_types_ready = true;
    if (!pposlf_native_type_plan_v1_load(
            &native_types, resource->proof_machine_native_types_path,
            error, error_size))
        goto done;
    pposlf_native_type_vm_v1_init(&vm);
    vm_ready = true;
    if (!pposlf_native_type_vm_v1_prepare(
            &vm, &native_types, error, error_size))
        goto done;
    ppcertificate_gslt_relational_runtime_v1_init(&runtime);
    runtime_ready = true;
    if (!ppcertificate_gslt_relational_runtime_v1_prepare(
            &runtime, resource->proof_generated_runtime_path,
            &resource->compiled_state, &native_types, &vm, vm_limits,
            error, error_size))
        goto done;

    resource->proof_machine_native_types = native_types;
    resource->proof_machine_vm = vm;
    resource->proof_generated_runtime = runtime;
    resource->proof_machine_native_types_ready = true;
    resource->proof_machine_vm_ready = true;
    resource->proof_generated_runtime_ready = true;
    native_types_ready = false;
    vm_ready = false;
    runtime_ready = false;
    ok = true;

done:
    if (runtime_ready)
        ppcertificate_gslt_relational_runtime_v1_free(&runtime);
    if (vm_ready)
        pposlf_native_type_vm_v1_free(&vm);
    if (native_types_ready)
        pposlf_native_type_plan_v1_free(&native_types);
    return ok;
}

static bool langdef_proof_frame_prepare(
    CettaLangDefV1 *resource, char *error, size_t error_size) {
    PPFirstOrderFrameDecoderV1 *decoders = NULL;
    PPRelationalStackProofV1CacheAdmission *admissions = NULL;
    uint32_t machine_len;
    uint32_t machine_index;
    bool ok = false;

    if (!resource || !resource->compiled_state_ready ||
        !resource->proof_native_types_ready ||
        !resource->proof_storage_plan_ready)
        return langdef_set_error(
            error, error_size,
            "direct frame machine lacks generated analysis");
    machine_len = resource->compiled_state.proof_machine_len;
    if (resource->proof_frame_decoders_ready ||
        resource->proof_frame_cache_admissions_ready) {
        if (!resource->proof_frame_decoders_ready ||
            !resource->proof_frame_cache_admissions_ready ||
            resource->proof_frame_decoder_len != machine_len)
            return langdef_set_error(
                error, error_size,
                "direct frame machine has inconsistent admission");
        return true;
    }
    decoders = calloc(
        machine_len ? machine_len : 1u, sizeof(*decoders));
    admissions = calloc(
        machine_len ? machine_len : 1u, sizeof(*admissions));
    if (!decoders || !admissions) {
        langdef_set_error(
            error, error_size,
            "cannot allocate direct frame-machine admission");
        goto done;
    }
    for (machine_index = 0u; machine_index < machine_len; machine_index++) {
        if (!ppfirst_order_frame_decoder_v1_admit(
                &resource->proof_storage_plan,
                &resource->proof_native_types,
                &resource->compiled_state, machine_index,
                &decoders[machine_index], error, error_size) ||
            !ppfirst_order_frame_decoder_v1_validate_state_program(
                &decoders[machine_index], &resource->compiled_state,
                machine_index, error, error_size) ||
            !ppfirst_order_frame_decoder_v1_cache_admission(
                &decoders[machine_index], &resource->compiled_state,
                machine_index, &admissions[machine_index],
                error, error_size))
            goto done;
    }
    resource->proof_frame_decoders = decoders;
    resource->proof_frame_cache_admissions = admissions;
    resource->proof_frame_decoder_len = machine_len;
    resource->proof_frame_decoders_ready = true;
    resource->proof_frame_cache_admissions_ready = true;
    decoders = NULL;
    admissions = NULL;
    ok = true;

done:
    free(admissions);
    free(decoders);
    return ok;
}
#endif

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
static int32_t langdef_result_language_artifact_index(
    const CettaLangDefManifestV1 *manifest) {
    uint32_t index;

    if (!manifest)
        return -1;
    for (index = 0u; index < manifest->extension_artifact_len; index++) {
        if (strcmp(manifest->extension_artifact_roles[index],
                   "result-language-def-v1") == 0) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t langdef_source_language_artifact_index(
    const CettaLangDefManifestV1 *manifest) {
    uint32_t index;

    if (!manifest)
        return -1;
    for (index = 0u; index < manifest->extension_artifact_len; index++) {
        if (strcmp(manifest->extension_artifact_roles[index],
                   "source-language-def-v1") == 0) {
            return (int32_t)index;
        }
    }
    return -1;
}

static bool langdef_core_has_unique_type(
    const CettaLanguageDefCoreV1 *language,
    const char *name) {
    size_t name_len;
    uint32_t matches = 0u;
    uint32_t index;

    if (!language || !name)
        return false;
    name_len = strlen(name);
    if (name_len == 0u || name_len > UINT32_MAX)
        return false;
    for (index = 0u; index < language->type_len; index++) {
        const CettaLdTextV1 *candidate = &language->types[index].name;
        if (candidate->len == (uint32_t)name_len &&
            candidate->bytes &&
            memcmp(candidate->bytes, name, name_len) == 0) {
            matches++;
        }
    }
    return matches == 1u;
}

static bool langdef_result_language_load(
    CettaLangDefV1 *resource,
    const CettaLangDefManifestV1 *manifest,
    char paths[][PATH_MAX],
    char *error,
    size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    int32_t artifact_index;

    if (!resource || !manifest)
        return langdef_set_error(
            error, error_size,
            "invalid result-language load arguments");
    artifact_index = langdef_result_language_artifact_index(manifest);
    if (artifact_index < 0)
        return manifest->result_category == NULL;
    if (!manifest->result_category)
        return langdef_set_error(
            error, error_size,
            "result language has no declared result category");
    if (!cetta_op_lang_v1_parse_file(
            &resource->result_language_wire,
            paths[(uint32_t)artifact_index],
            4000000u, 8000000u, &wire_status,
            error, error_size) ||
        !cetta_language_def_core_v1_decode(
            &resource->result_language,
            &resource->result_language_wire,
            200000u, &core_status, error, error_size)) {
        return false;
    }
    if (!langdef_core_has_unique_type(
            &resource->result_language, manifest->result_category)) {
        return langdef_set_error(
            error, error_size,
            "result category is not uniquely declared by the result LanguageDef");
    }
    resource->result_language_ready = true;
    return true;
}

static bool langdef_source_language_load(
    CettaLangDefV1 *resource,
    const CettaLangDefManifestV1 *manifest,
    char paths[][PATH_MAX],
    char *error,
    size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    int32_t artifact_index;

    if (!resource || !manifest)
        return langdef_set_error(
            error, error_size,
            "invalid source-language load arguments");
    artifact_index = langdef_source_language_artifact_index(manifest);
    if (artifact_index < 0)
        return manifest->source_category == NULL;
    if (!manifest->source_category)
        return langdef_set_error(
            error, error_size,
            "source language has no declared source category");
    if (!cetta_op_lang_v1_parse_file(
            &resource->source_language_wire,
            paths[(uint32_t)artifact_index],
            4000000u, 8000000u, &wire_status,
            error, error_size) ||
        !cetta_language_def_core_v1_decode(
            &resource->source_language,
            &resource->source_language_wire,
            200000u, &core_status, error, error_size)) {
        return false;
    }
    if (!langdef_core_has_unique_type(
            &resource->source_language, manifest->source_category)) {
        return langdef_set_error(
            error, error_size,
            "source category is not uniquely declared by the source LanguageDef");
    }
    resource->source_language_ready = true;
    return true;
}
#endif

static CettaLangDefV1 *langdef_load_resource(const char *manifest_argument,
                                             char *error,
                                             size_t error_size) {
    char *manifest_path = NULL;
    char pack_path[PATH_MAX];
    char lock_path[PATH_MAX];
    char program_source_path[PATH_MAX];
    char program_path[PATH_MAX];
    char compiled_cursor_path[PATH_MAX];
    char extension_artifact_paths[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS][PATH_MAX];
    Arena manifest_arena;
    Arena lock_arena;
    Atom *manifest_root = NULL;
    Atom *lock_root = NULL;
    CettaLangDefManifestV1 manifest;
    CettaLangDefLockV1 lock;
    CettaLangDefV1 *resource = NULL;
    char manifest_sha256[65];
    char pack_file_sha256[65];
    char source_path[PATH_MAX];
    char source_sha256[65];
    char program_source_sha256[65];
    char program_sha256[65];
    char compiled_cursor_sha256[65];
    char extension_artifact_sha256[65];
    uint32_t source_index;
    bool manifest_arena_ready = false;
    bool lock_arena_ready = false;

    manifest_path = realpath(manifest_argument, NULL);
    if (!manifest_path) {
        langdef_set_error(error, error_size, "cannot resolve %s: %s",
                          manifest_argument, strerror(errno));
        goto fail;
    }
    arena_init(&manifest_arena);
    manifest_arena_ready = true;
    arena_init(&lock_arena);
    lock_arena_ready = true;
    manifest_root = cetta_langdef_read_single_form(
        manifest_path, &manifest_arena, error, error_size);
    if (!manifest_root ||
        !cetta_langdef_manifest_parse(manifest_root, &manifest,
                                      error, error_size) ||
        !cetta_langdef_path_join(manifest_path, manifest.pack_relative,
                                 pack_path, sizeof(pack_path),
                                 error, error_size) ||
        !cetta_langdef_path_join(manifest_path, manifest.lock_relative,
                                 lock_path, sizeof(lock_path),
                                 error, error_size)) {
        goto fail;
    }
    lock_root = cetta_langdef_read_single_form(
        lock_path, &lock_arena, error, error_size);
    if (!lock_root ||
        !cetta_langdef_lock_parse(lock_root, &lock, error, error_size) ||
        !cetta_langdef_sha256_file(manifest_path, manifest_sha256,
                                   error, error_size) ||
        !cetta_langdef_sha256_file(pack_path, pack_file_sha256,
                                   error, error_size)) {
        goto fail;
    }
    if (strcmp(lock.manifest_sha256, manifest_sha256) != 0 ||
        strcmp(lock.pack_file_sha256, pack_file_sha256) != 0) {
        langdef_set_error(error, error_size,
                          "langdef artifact hash does not match its lock");
        goto fail;
    }
    if (manifest.source_len != lock.source_len) {
        langdef_set_error(error, error_size,
                          "langdef source list disagrees with its lock");
        goto fail;
    }
    if (manifest.extension_source_len != lock.extension_source_len ||
        manifest.extension_artifact_len != lock.extension_artifact_len) {
        langdef_set_error(
            error, error_size,
            "langdef extension composition disagrees with its lock");
        goto fail;
    }
    if ((manifest.program_relative == NULL) !=
        (lock.program_sha256 == NULL)) {
        langdef_set_error(error, error_size,
                          "langdef program list disagrees with its lock");
        goto fail;
    }
    if ((manifest.compiled_cursor_relative == NULL) !=
        (lock.compiled_cursor_sha256 == NULL)) {
        langdef_set_error(
            error, error_size,
            "langdef compiled cursor disagrees with its lock");
        goto fail;
    }
    for (source_index = 0u; source_index < manifest.source_len; source_index++) {
        if (strcmp(manifest.sources[source_index],
                   lock.source_paths[source_index]) != 0 ||
            !cetta_langdef_path_join(manifest_path,
                                     manifest.sources[source_index],
                                     source_path, sizeof(source_path),
                                     error, error_size) ||
            !cetta_langdef_sha256_file(source_path, source_sha256,
                                       error, error_size) ||
            strcmp(source_sha256, lock.source_sha256s[source_index]) != 0) {
            if (!error[0])
                langdef_set_error(error, error_size,
                                  "langdef source hash does not match its lock");
            goto fail;
        }
    }
    for (source_index = 0u;
         source_index < manifest.extension_source_len; source_index++) {
        if (strcmp(manifest.extension_sources[source_index],
                   lock.extension_source_paths[source_index]) != 0 ||
            !cetta_langdef_path_join(
                manifest_path, manifest.extension_sources[source_index],
                source_path, sizeof(source_path), error, error_size) ||
            !cetta_langdef_sha256_file(
                source_path, source_sha256, error, error_size) ||
            strcmp(source_sha256,
                   lock.extension_source_sha256s[source_index]) != 0) {
            if (!error[0])
                langdef_set_error(
                    error, error_size,
                    "langdef extension-source hash does not match its lock");
            goto fail;
        }
    }
    for (source_index = 0u;
         source_index < manifest.extension_artifact_len; source_index++) {
        if (strcmp(manifest.extension_artifact_roles[source_index],
                   lock.extension_artifact_roles[source_index]) != 0 ||
            strcmp(manifest.extension_artifact_relatives[source_index],
                   lock.extension_artifact_paths[source_index]) != 0 ||
            !cetta_langdef_path_join(
                manifest_path,
                manifest.extension_artifact_relatives[source_index],
                extension_artifact_paths[source_index],
                sizeof(extension_artifact_paths[source_index]),
                error, error_size) ||
            !cetta_langdef_sha256_file(
                extension_artifact_paths[source_index],
                extension_artifact_sha256, error, error_size) ||
            strcmp(extension_artifact_sha256,
                   lock.extension_artifact_sha256s[source_index]) != 0) {
            if (!error[0])
                langdef_set_error(
                    error, error_size,
                    "langdef extension artifact does not match its lock");
            goto fail;
        }
    }
    if (manifest.program_relative != NULL &&
        (strcmp(manifest.program_source_relative,
                lock.program_source_path) != 0 ||
         !cetta_langdef_path_join(manifest_path,
                                  manifest.program_source_relative,
                                  program_source_path,
                                  sizeof(program_source_path),
                                  error, error_size) ||
         !cetta_langdef_path_join(manifest_path,
                                  manifest.program_relative,
                                  program_path, sizeof(program_path),
                                  error, error_size) ||
         !cetta_langdef_sha256_file(program_source_path,
                                    program_source_sha256,
                                    error, error_size) ||
         !cetta_langdef_sha256_file(program_path, program_sha256,
                                    error, error_size) ||
         strcmp(program_source_sha256,
                lock.program_source_sha256) != 0 ||
         strcmp(program_sha256, lock.program_sha256) != 0)) {
        if (!error[0])
            langdef_set_error(
                error, error_size,
                "langdef program hash does not match its lock");
        goto fail;
    }
    if (manifest.compiled_cursor_relative != NULL &&
        (!cetta_langdef_path_join(
             manifest_path, manifest.compiled_cursor_relative,
             compiled_cursor_path, sizeof(compiled_cursor_path),
             error, error_size) ||
         !cetta_langdef_sha256_file(
             compiled_cursor_path, compiled_cursor_sha256,
             error, error_size) ||
         strcmp(compiled_cursor_sha256,
                lock.compiled_cursor_sha256) != 0)) {
        if (!error[0])
            langdef_set_error(
                error, error_size,
                "compiled cursor hash does not match its lock");
        goto fail;
    }

    resource = calloc(1u, sizeof(*resource));
    if (!resource) {
        langdef_set_error(error, error_size, "out of memory loading langdef");
        goto fail;
    }
    ppabi_v1_wire_init(&resource->wire);
    ppabi_v1_pack_init(&resource->pack);
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
    cetta_op_lang_v1_init(&resource->source_language_wire);
    cetta_language_def_core_v1_init(&resource->source_language);
    cetta_op_lang_v1_init(&resource->result_language_wire);
    cetta_language_def_core_v1_init(&resource->result_language);
#endif
    if (!ppabi_v1_wire_read(&resource->wire, pack_path,
                            error, error_size) ||
        !ppabi_v1_wire_load_pack(&resource->wire, &resource->pack,
                                 error, error_size)) {
        goto fail;
    }
    if (!atom_eq(resource->wire.start, manifest.start) ||
        resource->wire.expected_closed !=
            manifest.parser_pack_expected_closed ||
        strcmp(resource->pack.source_digest, lock.source_digest) != 0 ||
        strcmp(resource->pack.compiler_digest, lock.compiler_digest) != 0 ||
        strcmp(resource->pack.environment_digest,
               lock.environment_digest) != 0 ||
        strcmp(resource->pack.pack_digest, lock.pack_digest) != 0) {
        langdef_set_error(error, error_size,
                          "langdef lock disagrees with the compiled parser pack");
        goto fail;
    }
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
    if (!langdef_source_language_load(
            resource, &manifest, extension_artifact_paths,
            error, error_size)) {
        goto fail;
    }
    if (!langdef_result_language_load(
            resource, &manifest, extension_artifact_paths,
            error, error_size)) {
        goto fail;
    }
#endif
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    if (!langdef_guarded_parser_extension_load(
            resource, &manifest, extension_artifact_paths,
            error, error_size))
        goto fail;
    if (manifest.compiled_cursor_relative != NULL &&
        !langdef_compiled_cursor_load(
            resource, compiled_cursor_path, error, error_size))
        goto fail;
    if (!langdef_proof_extension_load(
            resource, &manifest, extension_artifact_paths,
            error, error_size))
        goto fail;
#endif
    resource->name = langdef_text_dup(manifest.name);
    resource->manifest_path = langdef_text_dup(manifest_path);
    resource->pack_path = langdef_text_dup(pack_path);
    resource->program_path = manifest.program_relative
        ? langdef_text_dup(program_path) : NULL;
    resource->import_entry = langdef_text_dup(manifest.import_entry);
    resource->source_category = langdef_text_dup(manifest.source_category);
    resource->result_category = langdef_text_dup(manifest.result_category);
    resource->compiled_cursor_path = manifest.compiled_cursor_relative
        ? langdef_text_dup(compiled_cursor_path) : NULL;
    if (!resource->name || !resource->manifest_path || !resource->pack_path ||
        (manifest.program_relative && !resource->program_path) ||
        (manifest.import_entry && !resource->import_entry) ||
        (manifest.source_category && !resource->source_category) ||
        (manifest.result_category && !resource->result_category) ||
        (manifest.compiled_cursor_relative &&
         !resource->compiled_cursor_path)) {
        langdef_set_error(error, error_size, "out of memory loading langdef");
        goto fail;
    }
    strcpy(resource->manifest_sha256, manifest_sha256);
    strcpy(resource->pack_file_sha256, pack_file_sha256);
    free(manifest_path);
    arena_free(&lock_arena);
    arena_free(&manifest_arena);
    return resource;

fail:
    langdef_resource_free(resource);
    if (lock_arena_ready)
        arena_free(&lock_arena);
    if (manifest_arena_ready)
        arena_free(&manifest_arena);
    free(manifest_path);
    return NULL;
}

bool cetta_langdef_validate_manifest_v1(const char *manifest_path,
                                        char *error, size_t error_size) {
    CettaLangDefV1 *resource = langdef_load_resource(
        manifest_path, error, error_size);
    if (!resource)
        return false;
    langdef_resource_free(resource);
    return true;
}

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
static bool langdef_registry_name_valid(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;
    if (!cursor || !*cursor)
        return false;
    while (*cursor) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_')
            return false;
        cursor++;
    }
    return true;
}

bool cetta_langdef_resolve_named_manifest_v1(
    const char *exec_path, const char *name,
    char *output, size_t output_size,
    char *error, size_t error_size) {
    const char *registry_root = getenv("CETTA_LANGDEF_ROOT");
    char install_root[PATH_MAX];
    struct stat status;
    int written;

    if (!output || output_size == 0u ||
        !langdef_registry_name_valid(name))
        return langdef_set_error(error, error_size,
                                 "invalid language-definition registry name");
    output[0] = '\0';
    if (registry_root && registry_root[0]) {
        written = snprintf(output, output_size, "%s/%s/langdef.metta",
                           registry_root, name);
    } else {
        if (!cetta_library_root_for_exec_path(
                exec_path, install_root, sizeof(install_root)))
            return langdef_set_error(
                error, error_size,
                "cannot locate the language-definition registry");
        written = snprintf(output, output_size,
                           "%s/langdef/%s/langdef.metta",
                           install_root, name);
    }
    if (written < 0 || (size_t)written >= output_size) {
        output[0] = '\0';
        return langdef_set_error(error, error_size,
                                 "language-definition path is too long");
    }
    if (stat(output, &status) != 0 || !S_ISREG(status.st_mode)) {
        output[0] = '\0';
        return langdef_set_error(error, error_size,
                                 "language definition is not registered");
    }
    return true;
}

static bool langdef_activate_program(CettaLangDefV1 *resource,
                                     Space *space, Arena *arena,
                                     char *error, size_t error_size) {
    Atom **forms = NULL;
    Space *work = NULL;
    int form_count;
    int index;
    bool ok = false;

    if (!resource->program_path)
        return true;
    form_count = parse_metta_file(resource->program_path, arena, &forms);
    if (form_count <= 0) {
        langdef_set_error(error, error_size,
                          "generated langdef program is empty or malformed");
        goto done;
    }
    for (index = 0; index < form_count; index++) {
        if (!cetta_langdef_expr_head(forms[index], "=", 2u)) {
            langdef_set_error(
                error, error_size,
                "generated langdef program contains a non-equation form");
            goto done;
        }
    }
    work = space_heap_clone_shallow(space);
    if (!work) {
        langdef_set_error(error, error_size,
                          "langdef program transaction allocation failed");
        goto done;
    }
    for (index = 0; index < form_count; index++) {
        if (!space_admit_atom(work, arena, forms[index])) {
            langdef_set_error(error, error_size,
                              "langdef program transaction failed");
            goto done;
        }
    }
    space_replace_contents(space, work);
    ok = true;

done:
    if (work) {
        space_free(work);
        free(work);
    }
    free(forms);
    return ok;
}

static CettaLangDefV1 *langdef_handle_resource(CettaLibraryContext *ctx,
                                               Atom *argument) {
    uint64_t id;
    if (!cetta_native_handle_arg(argument, LANGDEF_HANDLE_KIND, &id))
        return NULL;
    return cetta_native_handle_get(ctx, LANGDEF_HANDLE_KIND, id);
}

static Atom *langdef_expr(Arena *arena, const char *head,
                          Atom **arguments, uint32_t argument_len) {
    Atom **elements;
    Atom *result;
    uint32_t index;
    elements = arena_alloc(arena, sizeof(*elements) * (argument_len + 1u));
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    return result;
}

static Atom *langdef_return(Arena *arena, Atom *value);

static bool langdef_context_text_copy(CettaLdTextV1 *out,
                                      const char *source) {
    CettaLdTextV1 result = {0};
    size_t len;

    if (!out || !source)
        return false;
    len = strlen(source);
    if (len > UINT32_MAX)
        return false;
    if (len > 0u) {
        result.bytes = malloc(len);
        if (!result.bytes)
            return false;
        memcpy(result.bytes, source, len);
    }
    result.len = (uint32_t)len;
    *out = result;
    return true;
}

static bool langdef_context_text_equal(const CettaLdTextV1 *left,
                                       const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         (left->bytes && right->bytes &&
          memcmp(left->bytes, right->bytes, left->len) == 0));
}

static bool langdef_context_relation_env_decode(
    Atom *source, CettaLanguageDefRelationEnvV1 *out,
    char *error, size_t error_size) {
    CettaLanguageDefRelationEnvV1 result = {0};
    CettaExprLen row_count;
    CettaExprLen row_index;

    if (!source || !out || source->kind != ATOM_EXPR ||
        !source->expr.elems || source->expr.len < 1u ||
        !atom_is_symbol(source->expr.elems[0], "LangDef:RelationRows"))
        return langdef_set_error(
            error, error_size,
            "contextual relation provider must be a LangDef:RelationRows envelope");
    row_count = source->expr.len - 1u;
    if (row_count > UINT32_MAX)
        return langdef_set_error(
            error, error_size,
            "contextual relation provider has too many rows");
    if (row_count > 0u) {
        result.rows = calloc((size_t)row_count, sizeof(*result.rows));
        if (!result.rows)
            return langdef_set_error(
                error, error_size,
                "contextual relation provider allocation failed");
    }
    result.len = (uint32_t)row_count;
    for (row_index = 0u; row_index < row_count; row_index++) {
        Atom *wire = source->expr.elems[row_index + 1u];
        CettaLanguageDefRelationRowV1 *row = &result.rows[row_index];
        const char *relation;
        Atom *receipt;
        CettaExprLen tuple_len;
        CettaExprLen tuple_index;

        if (!wire || wire->kind != ATOM_EXPR || !wire->expr.elems ||
            wire->expr.len < 3u ||
            !atom_is_symbol(wire->expr.elems[0],
                            "LangDef:RelationRow") ||
            !cetta_langdef_text_arg(wire->expr.elems[1], &relation)) {
            (void)langdef_set_error(
                error, error_size,
                "contextual relation row is malformed");
            goto fail;
        }
        receipt = wire->expr.elems[2];
        if (!receipt || receipt->kind != ATOM_GROUNDED ||
            receipt->ground.gkind != GV_INT || receipt->ground.ival < 0) {
            (void)langdef_set_error(
                error, error_size,
                "contextual relation receipt must be a nonnegative integer");
            goto fail;
        }
        if (!langdef_context_text_copy(&row->relation, relation)) {
            (void)langdef_set_error(
                error, error_size,
                "contextual relation name allocation failed");
            goto fail;
        }
        row->receipt_id = (uint64_t)receipt->ground.ival;
        tuple_len = wire->expr.len - 3u;
        if (tuple_len > UINT32_MAX) {
            (void)langdef_set_error(
                error, error_size,
                "contextual relation tuple is too large");
            goto fail;
        }
        if (tuple_len > 0u) {
            row->tuple = calloc((size_t)tuple_len, sizeof(*row->tuple));
            if (!row->tuple) {
                (void)langdef_set_error(
                    error, error_size,
                    "contextual relation tuple allocation failed");
                goto fail;
            }
        }
        row->tuple_len = (uint32_t)tuple_len;
        for (tuple_index = 0u; tuple_index < tuple_len; tuple_index++) {
            CettaLdPatternAtomV1Status pattern_status =
                CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT;
            if (!cetta_ld_pattern_atom_v1_decode(
                    &row->tuple[tuple_index],
                    wire->expr.elems[tuple_index + 3u],
                    LANGDEF_DEFAULT_REPLAY_DEPTH,
                    LANGDEF_DEFAULT_PROOF_RULE_ATTEMPTS,
                    &pattern_status, error, error_size))
                goto fail;
        }
    }
    *out = result;
    return true;

fail:
    language_def_relation_env_free(&result);
    return false;
}

static bool langdef_context_relation_query(
    void *opaque, const CettaLdTextV1 *relation,
    const CettaLdPatternV1 *applied_arguments,
    uint32_t argument_len, uint32_t row_index,
    const CettaLdPatternV1 **row, uint32_t *row_len,
    uint64_t *receipt_id, bool *present,
    char *error, size_t error_size) {
    CettaLanguageDefRelationEnvV1 *environment = opaque;
    uint32_t index;
    uint32_t matching_index = 0u;
    (void)applied_arguments;
    (void)argument_len;

    if (!environment || !relation || !row || !row_len ||
        !receipt_id || !present)
        return langdef_set_error(
            error, error_size,
            "contextual relation provider received an invalid request");
    *row = NULL;
    *row_len = 0u;
    *receipt_id = 0u;
    *present = false;
    for (index = 0u; index < environment->len; index++) {
        CettaLanguageDefRelationRowV1 *candidate =
            &environment->rows[index];
        if (!langdef_context_text_equal(&candidate->relation, relation))
            continue;
        if (matching_index == row_index) {
            *row = candidate->tuple;
            *row_len = candidate->tuple_len;
            *receipt_id = candidate->receipt_id;
            *present = true;
            return true;
        }
        matching_index++;
    }
    return true;
}

static Atom *langdef_context_text_atom(Arena *arena,
                                       const CettaLdTextV1 *text) {
    char *copy;
    Atom *result;

    if (!arena || !text || (text->len > 0u && !text->bytes))
        return NULL;
    copy = malloc((size_t)text->len + 1u);
    if (!copy)
        return NULL;
    if (text->len > 0u)
        memcpy(copy, text->bytes, text->len);
    copy[text->len] = '\0';
    result = atom_string(arena, copy);
    free(copy);
    return result;
}

static Atom *langdef_context_trace_atom(
    Arena *arena, const CettaLanguageDefCoreV1 *language,
    const CettaLdCrV1Trace *trace, uint32_t depth) {
    Atom **arguments;
    Atom *result;
    uint32_t premise_index;

    if (!arena || !language || !trace ||
        trace->rule_index >= language->rewrite_len ||
        depth > LANGDEF_DEFAULT_REPLAY_DEPTH)
        return NULL;
    arguments = arena_alloc(
        arena, sizeof(*arguments) * ((size_t)trace->premise_len + 2u));
    arguments[0] = atom_int(arena, trace->rule_index);
    arguments[1] = langdef_context_text_atom(
        arena, &language->rewrites[trace->rule_index].name);
    if (!arguments[1])
        return NULL;
    for (premise_index = 0u; premise_index < trace->premise_len;
         premise_index++) {
        const CettaLdCrV1PremiseEvidence *evidence =
            &trace->premises[premise_index];
        Atom *evidence_arguments[5];

        evidence_arguments[0] = atom_int(arena, evidence->premise_index);
        switch (evidence->kind) {
        case CETTA_LD_PREMISE_FRESHNESS_V1:
            arguments[premise_index + 2u] = langdef_expr(
                arena, "LangDef:FreshnessEvidence",
                evidence_arguments, 1u);
            break;
        case CETTA_LD_PREMISE_CONGRUENCE_V1:
            evidence_arguments[1] = langdef_context_trace_atom(
                arena, language, evidence->as.congruence.step, depth + 1u);
            if (!evidence_arguments[1])
                return NULL;
            arguments[premise_index + 2u] = langdef_expr(
                arena, "LangDef:CongruenceEvidence",
                evidence_arguments, 2u);
            break;
        case CETTA_LD_PREMISE_RELATION_QUERY_V1:
            if (evidence->as.relation_query.receipt_id > INT64_MAX)
                return NULL;
            evidence_arguments[1] = atom_symbol(
                arena,
                evidence->as.relation_query.source ==
                        CETTA_LD_CR_V1_RELATION_BUILTIN
                    ? "LangDef:BuiltInRelation"
                    : "LangDef:ExternalRelation");
            evidence_arguments[2] = atom_int(
                arena, evidence->as.relation_query.row_index);
            evidence_arguments[3] = atom_int(
                arena,
                (int64_t)evidence->as.relation_query.receipt_id);
            arguments[premise_index + 2u] = langdef_expr(
                arena, "LangDef:RelationEvidence",
                evidence_arguments, 4u);
            break;
        case CETTA_LD_PREMISE_FOR_ALL_V1:
            return NULL;
        }
    }
    result = langdef_expr(arena, "LangDef:RuleTrace", arguments,
                          trace->premise_len + 2u);
    return result;
}

static Atom *langdef_context_results_atom(
    Arena *arena, const CettaLanguageDefCoreV1 *language,
    const CettaLdCrV1Results *results, char *error, size_t error_size) {
    Atom **arguments;
    uint32_t index;

    if (!arena || !language || !results)
        return NULL;
    arguments = arena_alloc(
        arena, sizeof(*arguments) * ((size_t)results->len + 1u));
    arguments[0] = atom_symbol(
        arena, results->context_fuel_exhausted
            ? "LangDef:ContextFuelExhausted"
            : "LangDef:ContextComplete");
    for (index = 0u; index < results->len; index++) {
        Atom *item_arguments[2];
        CettaLdPatternAtomV1Status pattern_status =
            CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT;
        item_arguments[0] = cetta_ld_pattern_atom_v1_encode(
            arena, &results->items[index].term,
            LANGDEF_DEFAULT_REPLAY_DEPTH,
            LANGDEF_DEFAULT_PROOF_RULE_ATTEMPTS,
            &pattern_status, error, error_size);
        item_arguments[1] = langdef_context_trace_atom(
            arena, language, results->items[index].trace, 0u);
        if (!item_arguments[0] || !item_arguments[1]) {
            if (error && error_size > 0u && error[0] == '\0')
                (void)snprintf(
                    error, error_size,
                    "contextual result trace could not be encoded");
            return NULL;
        }
        arguments[index + 1u] = langdef_expr(
            arena, "LangDef:ContextualResult", item_arguments, 2u);
    }
    return langdef_expr(arena, "LangDef:ContextualResults",
                        arguments, results->len + 1u);
}

static Atom *langdef_context_term_results_atom(
    Arena *arena, const CettaLanguageDefCoreV1 *language,
    const char *target_category, const CettaLdCrV1Results *results,
    uint64_t codec_work_limit,
    CettaLdGroundTermV1Status *codec_status,
    bool *target_decode_failed,
    char *error, size_t error_size) {
    Atom **arguments;
    uint32_t index;

    if (target_decode_failed)
        *target_decode_failed = false;
    if (!arena || !language || !target_category || !results ||
        !codec_status || !target_decode_failed)
        return NULL;
    arguments = arena_alloc(
        arena, sizeof(*arguments) * ((size_t)results->len + 1u));
    arguments[0] = atom_symbol(
        arena, results->context_fuel_exhausted
            ? "LangDef:ContextFuelExhausted"
            : "LangDef:ContextComplete");
    for (index = 0u; index < results->len; index++) {
        Atom *item_arguments[2];
        item_arguments[0] =
            cetta_language_def_ground_term_v1_from_pattern(
                arena, language, target_category,
                &results->items[index].term,
                LANGDEF_DEFAULT_REPLAY_DEPTH, codec_work_limit,
                codec_status, error, error_size);
        if (!item_arguments[0]) {
            *target_decode_failed = true;
            return NULL;
        }
        item_arguments[1] = langdef_context_trace_atom(
            arena, language, results->items[index].trace, 0u);
        if (!item_arguments[1]) {
            if (error && error_size > 0u && error[0] == '\0')
                (void)snprintf(
                    error, error_size,
                    "contextual result trace could not be encoded");
            return NULL;
        }
        arguments[index + 1u] = langdef_expr(
            arena, "LangDef:ContextualTermResult",
            item_arguments, 2u);
    }
    return langdef_expr(
        arena, "LangDef:ContextualTermResults",
        arguments, results->len + 1u);
}

static Atom *langdef_context_fault(Arena *arena, const char *status,
                                   const char *message) {
    Atom *arguments[2] = {
        atom_symbol(arena, status ? status : "ContextualUnknownStatus"),
        atom_string(arena, message ? message : "contextual execution failed"),
    };
    return langdef_return(
        arena, langdef_expr(arena, "LangDef:ContextualFault",
                            arguments, 2u));
}

static Atom *langdef_context_codec_fault(
    Arena *arena, const char *phase,
    CettaLdGroundTermV1Status status, const char *message) {
    Atom *arguments[3] = {
        atom_symbol(arena, phase ? phase : "LangDef:UnknownTerm"),
        atom_symbol(arena, cetta_ld_ground_term_v1_status_name(status)),
        atom_string(arena, message ? message : "typed Pattern codec failed"),
    };
    return langdef_return(
        arena, langdef_expr(arena, "LangDef:ContextualCodecFault",
                            arguments, 3u));
}

static Atom *langdef_return(Arena *arena, Atom *value) {
    return atom_expr2(arena, atom_symbol(arena, "return"), value);
}

/* Publication is a transactional boundary whose rejection must remain an
 * inspectable value.  Seal both successful and failed native results before
 * the evaluator can treat an Error payload as control flow. */
static Atom *langdef_publish_result(Arena *arena, Atom *result) {
    Atom *payload = result;
    Atom *arguments[1];
    if (cetta_langdef_expr_head(result, "Error", 2u)) {
        Atom *failure_arguments[2] = {
            result->expr.elems[1],
            result->expr.elems[2],
        };
        return langdef_return(
            arena,
            langdef_expr(
                arena, "LangDef:PublishFailure", failure_arguments, 2u));
    }
    if (cetta_langdef_expr_head(result, "return", 1u))
        payload = result->expr.elems[1];
    arguments[0] = payload;
    return langdef_return(
        arena, langdef_expr(arena, "LangDef:PublishResult", arguments, 1u));
}

static bool langdef_result_value(Atom *result, Atom **value_out) {
    if (!cetta_langdef_expr_head(result, "result", 2u) ||
        !atom_is_symbol(result->expr.elems[2], "nil"))
        return false;
    *value_out = result->expr.elems[1];
    return true;
}

static uint32_t authored_parser_work_limit(size_t source_len) {
    const uint64_t minimum = UINT64_C(4000000);
    const uint64_t per_byte = UINT64_C(1024);
    uint64_t limit;

    if (source_len > (UINT64_MAX - minimum) / per_byte)
        return UINT32_MAX;
    limit = minimum + (uint64_t)source_len * per_byte;
    return limit > UINT32_MAX ? UINT32_MAX : (uint32_t)limit;
}

static Atom *authored_parser_expected(
    const CettaAuthoredParserV1 *resource,
    const CettaLpNativeUtf8Forest *forest, Arena *arena) {
    Atom **items = NULL;
    uint32_t index;

    if (!resource || !forest || !arena)
        return NULL;
    if (forest->expected_terminal_len > 0u) {
        items = arena_alloc(
            arena, sizeof(*items) * forest->expected_terminal_len);
        if (!items)
            return NULL;
    }
    for (index = 0u; index < forest->expected_terminal_len; index++) {
        uint32_t terminal = forest->expected_terminal_ids[index];
        if (terminal >= resource->compiled.pack.terminal_len)
            return NULL;
        items[index] = atom_deep_copy(
            arena, resource->compiled.pack.terminals[terminal].identity);
        if (!items[index])
            return NULL;
    }
    return atom_expr(arena, items, forest->expected_terminal_len);
}

static Atom *authored_parser_parse_bytes(
    const CettaAuthoredParserV1 *resource,
    const uint8_t *bytes, size_t len,
    Arena *arena, Atom *source) {
    PPNativeV1Result parsed;
    Atom *result = NULL;
    Atom **values = NULL;
    char input_sha256[65];
    char error[512] = {0};
    uint32_t index;

    if (!resource || (!bytes && len > 0u) || !arena || !source)
        return NULL;
    cetta_native_sha256_hex(
        bytes ? bytes : (const uint8_t *)"", len, input_sha256);
    ppnative_v1_result_init(&parsed);
    if (!ppgll_v1_prepared_parse(
            &resource->prepared, bytes, len,
            authored_parser_work_limit(len),
            LANGDEF_DEFAULT_REPLAY_DEPTH,
            LANGDEF_DEFAULT_RESULT_LIMIT,
            &parsed, error, sizeof(error))) {
        result = langdef_error(
            arena, source,
            error[0] ? error : "authored parser execution failed");
        goto done;
    }
    if (parsed.outcome != PPNATIVE_V1_COMPLETED) {
        Atom *arguments[5] = {
            atom_symbol(arena, resource->name),
            atom_string(arena, resource->compiled.binding_sha256),
            atom_string(arena, input_sha256),
            atom_int(arena, parsed.outcome),
            atom_string(arena, parsed.detail),
        };
        result = langdef_expr(
            arena, "LangDef:AuthoredParseIncomplete", arguments, 5u);
        goto done;
    }
    if (!parsed.accepted) {
        Atom *expected = authored_parser_expected(
            resource, &parsed.forest, arena);
        Atom *arguments[5];
        if (!expected) {
            result = langdef_error(
                arena, source,
                "authored parser expected-set construction failed");
            goto done;
        }
        arguments[0] = atom_symbol(arena, resource->name);
        arguments[1] = atom_string(
            arena, resource->compiled.binding_sha256);
        arguments[2] = atom_string(arena, input_sha256);
        arguments[3] = atom_int(arena, parsed.forest.farthest_byte);
        arguments[4] = expected;
        result = langdef_expr(
            arena, "LangDef:AuthoredParseRejected", arguments, 5u);
        goto done;
    }
    if (parsed.semantic_result_len > 0u) {
        values = arena_alloc(
            arena, sizeof(*values) * parsed.semantic_result_len);
        if (!values) {
            result = langdef_error(
                arena, source,
                "authored parser result allocation failed");
            goto done;
        }
    }
    for (index = 0u; index < parsed.semantic_result_len; index++) {
        Atom *value;
        if (!langdef_result_value(parsed.semantic_results[index], &value)) {
            result = langdef_error(
                arena, source,
                "authored parser returned an open semantic result");
            goto done;
        }
        values[index] = atom_deep_copy(arena, value);
        if (!values[index]) {
            result = langdef_error(
                arena, source,
                "authored parser result copy failed");
            goto done;
        }
    }
    {
        Atom *value_list = atom_expr(
            arena, values, parsed.semantic_result_len);
        Atom *arguments[4] = {
            atom_symbol(arena, resource->name),
            atom_string(arena, resource->compiled.binding_sha256),
            atom_string(arena, input_sha256),
            value_list,
        };
        result = langdef_expr(
            arena, "LangDef:AuthoredParseAccepted", arguments, 4u);
    }

done:
    ppnative_v1_result_free(&parsed);
    return result;
}

#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
typedef struct {
    Arena *arena;
    const PPOccurrenceFoldV1Plan *plan;
    Atom **values;
    uint32_t value_len;
    uint32_t value_cap;
    bool materialize_values;
    bool committed;
} CettaLangDefCompiledFoldV1;

static bool langdef_compiled_fold_apply(
    void *raw,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    CettaLangDefCompiledFoldV1 *fold = raw;
    Atom **elements;
    Atom *operation;
    const char *operation_name;
    uint32_t index;

    if (!fold || !fold->arena || !fold->plan || !step || fold->committed)
        return langdef_set_error(error_buf, error_buf_size,
                                 "invalid compiled fold step");
    operation_name = ppoccurrence_fold_v1_operation_name(
        fold->plan, step->operation_id);
    if (!operation_name)
        return langdef_set_error(error_buf, error_buf_size,
                                 "compiled fold step has no operation");
    if (!fold->materialize_values) {
        if (fold->value_len == UINT32_MAX)
            return langdef_set_error(error_buf, error_buf_size,
                                     "compiled fold result is too large");
        fold->value_len++;
        return true;
    }
    elements = arena_alloc(
        fold->arena, sizeof(*elements) * ((size_t)step->value_len + 1u));
    elements[0] = atom_symbol(fold->arena, operation_name);
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        const char *role_name = ppoccurrence_fold_v1_role_name(
            fold->plan, value->role_id);
        char *text;
        if (!role_name || memchr(value->bytes, 0, value->byte_len) != NULL)
            return langdef_set_error(
                error_buf, error_buf_size,
                "compiled fold value cannot be represented as text");
        text = arena_alloc(fold->arena, value->byte_len + 1u);
        memcpy(text, value->bytes, value->byte_len);
        text[value->byte_len] = '\0';
        elements[index + 1u] = atom_expr2(
            fold->arena, atom_symbol(fold->arena, role_name),
            atom_string(fold->arena, text));
    }
    operation = atom_expr(
        fold->arena, elements, (CettaExprLen)(step->value_len + 1u));
    if (fold->value_len == fold->value_cap) {
        uint32_t next_cap = fold->value_cap ? fold->value_cap * 2u : 32u;
        Atom **next;
        if (next_cap < fold->value_cap)
            return langdef_set_error(error_buf, error_buf_size,
                                     "compiled fold result is too large");
        next = realloc(fold->values, sizeof(*next) * next_cap);
        if (!next)
            return langdef_set_error(error_buf, error_buf_size,
                                     "cannot allocate compiled fold result");
        fold->values = next;
        fold->value_cap = next_cap;
    }
    fold->values[fold->value_len++] = operation;
    return true;
}

static bool langdef_compiled_fold_commit(void *raw,
                                         char *error_buf,
                                         size_t error_buf_size) {
    CettaLangDefCompiledFoldV1 *fold = raw;
    if (!fold || fold->committed)
        return langdef_set_error(error_buf, error_buf_size,
                                 "invalid compiled fold commit");
    fold->committed = true;
    return true;
}

static void langdef_compiled_fold_abort(void *raw) {
    CettaLangDefCompiledFoldV1 *fold = raw;
    if (!fold)
        return;
    fold->value_len = 0u;
    fold->committed = false;
}

typedef struct {
    uint32_t role_id;
    uint8_t *bytes;
    size_t byte_len;
} CettaLangDefRuntimeValueV1;

typedef struct {
    uint32_t source_id;
    uint32_t operation_id;
    uint32_t production_index;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint32_t left_byte;
    uint32_t right_byte;
    CettaLangDefRuntimeValueV1 *values;
    uint32_t value_len;
} CettaLangDefRuntimeOccurrenceV1;

typedef struct {
    const PPOccurrenceFoldV1Plan *plan;
    CettaLangDefRuntimeOccurrenceV1 *occurrences;
    uint32_t occurrence_len;
    uint32_t occurrence_cap;
    bool committed;
} CettaLangDefRuntimeOccurrenceCollectorV1;

static void langdef_runtime_occurrence_collector_clear(
    CettaLangDefRuntimeOccurrenceCollectorV1 *collector) {
    uint32_t occurrence_index;
    if (!collector)
        return;
    for (occurrence_index = 0u;
         occurrence_index < collector->occurrence_len;
         occurrence_index++) {
        CettaLangDefRuntimeOccurrenceV1 *occurrence =
            &collector->occurrences[occurrence_index];
        uint32_t value_index;
        for (value_index = 0u;
             value_index < occurrence->value_len;
             value_index++) {
            free(occurrence->values[value_index].bytes);
        }
        free(occurrence->values);
    }
    free(collector->occurrences);
    collector->occurrences = NULL;
    collector->occurrence_len = 0u;
    collector->occurrence_cap = 0u;
    collector->committed = false;
}

static bool langdef_runtime_occurrence_apply(
    void *raw,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    CettaLangDefRuntimeOccurrenceCollectorV1 *collector = raw;
    CettaLangDefRuntimeOccurrenceV1 staged = {0};
    uint32_t value_index;

    if (!collector || !collector->plan || !step || collector->committed ||
        step->operation_id >= collector->plan->operation_len) {
        return langdef_set_error(
            error_buf, error_buf_size,
            "runtime occurrence collector received an invalid step");
    }
    if (step->value_len > 0u) {
        staged.values = calloc(step->value_len, sizeof(*staged.values));
        if (!staged.values) {
            return langdef_set_error(
                error_buf, error_buf_size,
                "cannot allocate runtime occurrence values");
        }
    }
    staged.source_id = step->source_id;
    staged.operation_id = step->operation_id;
    staged.production_index = step->production_index;
    staged.left_scalar = step->left_scalar;
    staged.right_scalar = step->right_scalar;
    staged.left_byte = step->left_byte;
    staged.right_byte = step->right_byte;
    staged.value_len = step->value_len;
    for (value_index = 0u; value_index < step->value_len; value_index++) {
        const PPOccurrenceFoldV1Value *source = &step->values[value_index];
        CettaLangDefRuntimeValueV1 *target = &staged.values[value_index];
        if (source->role_id >= collector->plan->role_len ||
            source->byte_len > UINT32_MAX) {
            langdef_set_error(
                error_buf, error_buf_size,
                "runtime occurrence value exceeds its admitted domain");
            goto fail;
        }
        target->role_id = source->role_id;
        target->byte_len = source->byte_len;
        if (source->byte_len > 0u) {
            target->bytes = malloc(source->byte_len);
            if (!target->bytes) {
                langdef_set_error(
                    error_buf, error_buf_size,
                    "cannot copy a runtime occurrence value");
                goto fail;
            }
            memcpy(target->bytes, source->bytes, source->byte_len);
        }
    }
    if (collector->occurrence_len == collector->occurrence_cap) {
        uint32_t next_cap = collector->occurrence_cap
            ? collector->occurrence_cap * 2u : 64u;
        CettaLangDefRuntimeOccurrenceV1 *next;
        if (next_cap <= collector->occurrence_cap) {
            langdef_set_error(
                error_buf, error_buf_size,
                "runtime occurrence stream is too large");
            goto fail;
        }
        next = realloc(
            collector->occurrences, sizeof(*next) * next_cap);
        if (!next) {
            langdef_set_error(
                error_buf, error_buf_size,
                "cannot grow the runtime occurrence stream");
            goto fail;
        }
        collector->occurrences = next;
        collector->occurrence_cap = next_cap;
    }
    collector->occurrences[collector->occurrence_len++] = staged;
    return true;

fail:
    for (value_index = 0u; value_index < staged.value_len; value_index++)
        free(staged.values[value_index].bytes);
    free(staged.values);
    return false;
}

static bool langdef_runtime_occurrence_commit(
    void *raw, char *error_buf, size_t error_buf_size) {
    CettaLangDefRuntimeOccurrenceCollectorV1 *collector = raw;
    if (!collector || collector->committed) {
        return langdef_set_error(
            error_buf, error_buf_size,
            "runtime occurrence collector cannot commit");
    }
    collector->committed = true;
    return true;
}

static void langdef_runtime_occurrence_abort(void *raw) {
    langdef_runtime_occurrence_collector_clear(raw);
}

static bool langdef_runtime_role_admitted(
    const PPOccurrenceFoldV1Plan *plan,
    const CettaLangDefRuntimeOccurrenceV1 *occurrence,
    uint32_t role_id) {
    uint32_t binding_index;
    const PPOccurrenceFoldV1ProductionBinding *binding;
    const PPOccurrenceFoldV1Contract *contract;
    uint32_t transition_index;

    if (!plan || !occurrence ||
        occurrence->production_index >= plan->cursor_production_len)
        return false;
    binding_index =
        plan->production_binding_by_index[occurrence->production_index];
    if (binding_index == UINT32_MAX || binding_index >= plan->production_len)
        return false;
    binding = &plan->productions[binding_index];
    if (binding->contract_index >= plan->contract_len)
        return false;
    contract = &plan->contracts[binding->contract_index];
    for (transition_index = 0u;
         transition_index < contract->transition_len;
         transition_index++) {
        const PPOccurrenceFoldV1Transition *transition =
            &plan->transitions[
                contract->transition_begin + transition_index];
        if (transition->kind == PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
            transition->role_id == role_id)
            return true;
    }
    return false;
}

static Atom *langdef_runtime_byte_values(
    Arena *arena, const uint8_t *bytes, size_t len) {
    Atom *result = atom_symbol(arena, "ByteValuesNilV1");
    while (len > 0u) {
        Atom *arguments[2];
        len--;
        arguments[0] = atom_int(arena, bytes[len]);
        arguments[1] = result;
        result = langdef_expr(arena, "ByteValuesConsV1", arguments, 2u);
    }
    return result;
}

static Atom *langdef_runtime_values_tree(
    Arena *arena, Atom *const *values, uint32_t begin, uint32_t end) {
    Atom *arguments[2];
    uint32_t middle;
    if (begin == end)
        return atom_symbol(arena, "ValuesTreeEmptyV1");
    if (end - begin == 1u) {
        arguments[0] = values[begin];
        return langdef_expr(arena, "ValuesTreeLeafV1", arguments, 1u);
    }
    middle = begin + (end - begin) / 2u;
    arguments[0] = langdef_runtime_values_tree(
        arena, values, begin, middle);
    arguments[1] = langdef_runtime_values_tree(
        arena, values, middle, end);
    return langdef_expr(arena, "ValuesTreeNodeV1", arguments, 2u);
}

static Atom *langdef_runtime_values_sequence(
    Arena *arena, Atom *const *values, uint32_t len) {
    Atom *arguments[2];
    arguments[0] = langdef_runtime_values_tree(arena, values, 0u, len);
    arguments[1] = atom_symbol(arena, "ValuesTreeStackNilV1");
    return langdef_expr(arena, "ValuesSequenceV1", arguments, 2u);
}

static Atom *langdef_runtime_digest_atom(Arena *arena, const char *digest) {
    char identity[72];
    if (!digest)
        return NULL;
    if (strncmp(digest, "sha256-", 7u) == 0)
        return atom_symbol(arena, digest);
    if (strlen(digest) != 64u)
        return NULL;
    snprintf(identity, sizeof(identity), "sha256-%s", digest);
    return atom_symbol(arena, identity);
}

static Atom *langdef_runtime_roles(
    Arena *arena,
    const PPOccurrenceFoldV1Plan *plan,
    const CettaLangDefRuntimeOccurrenceV1 *occurrence) {
    Atom *roles = atom_symbol(arena, "StateRuntimeRolesNilV1");
    uint32_t role_id;
    for (role_id = plan->role_len; role_id > 0u; role_id--) {
        uint32_t selected_role = role_id - 1u;
        uint32_t count = 0u;
        uint32_t value_index;
        Atom **values;
        Atom *value_sequence;
        Atom *arguments[4];
        bool admitted = langdef_runtime_role_admitted(
            plan, occurrence, selected_role);
        for (value_index = 0u;
             value_index < occurrence->value_len;
             value_index++) {
            if (occurrence->values[value_index].role_id == selected_role)
                count++;
        }
        if (!admitted && count == 0u)
            continue;
        values = count ? malloc(sizeof(*values) * count) : NULL;
        if (count && !values) {
            free(values);
            return NULL;
        }
        count = 0u;
        for (value_index = 0u;
             value_index < occurrence->value_len;
             value_index++) {
            const CettaLangDefRuntimeValueV1 *value =
                &occurrence->values[value_index];
            Atom *source_arguments[2];
            Atom *runtime_source_value;
            Atom *state_source_arguments[1];
            SymbolId symbol;
            if (value->role_id != selected_role)
                continue;
            symbol = symbol_intern_bytes(
                g_symbols, value->bytes, (uint32_t)value->byte_len);
            source_arguments[0] = atom_symbol_id(arena, symbol);
            source_arguments[1] = langdef_runtime_byte_values(
                arena, value->bytes, value->byte_len);
            runtime_source_value = langdef_expr(
                arena, "StateRuntimeSourceValueV1",
                source_arguments, 2u);
            state_source_arguments[0] = runtime_source_value;
            values[count] = langdef_expr(
                arena, "StateSourceValueV1",
                state_source_arguments, 1u);
            count++;
        }
        value_sequence = langdef_runtime_values_sequence(
            arena, values, count);
        arguments[0] = atom_symbol(arena, plan->roles[selected_role]);
        arguments[1] = value_sequence;
        arguments[2] = value_sequence;
        arguments[3] = roles;
        roles = langdef_expr(
            arena, "StateRuntimeRolesConsV1", arguments, 4u);
        free(values);
    }
    return roles;
}

static Atom *langdef_runtime_occurrence_stream(
    CettaLangDefV1 *langdef,
    CettaLangDefRuntimeOccurrenceCollectorV1 *collector,
    const char *source_digest,
    Arena *arena) {
    Atom *stream_identity_arguments[3];
    Atom *stream_identity;
    Atom *package_arguments[1];
    Atom *package;
    Atom *tail = atom_symbol(arena, "StateOccurrenceLastTailV1");
    Atom *first = atom_symbol(arena, "StateRuntimeOccurrenceEmptyV1");
    uint32_t index;

    package_arguments[0] = langdef_runtime_digest_atom(
        arena, langdef->compiled_state.compiler_answer_digest);
    if (!package_arguments[0])
        return NULL;
    package = langdef_expr(
        arena, "StateProgramDigestV1", package_arguments, 1u);
    stream_identity_arguments[0] = langdef_runtime_digest_atom(
        arena, langdef->compiled_fold.plan_digest);
    stream_identity_arguments[1] = langdef_runtime_digest_atom(
        arena, source_digest);
    if (!stream_identity_arguments[0] || !stream_identity_arguments[1])
        return NULL;
    stream_identity_arguments[2] = package;
    stream_identity = langdef_expr(
        arena, "StateOccurrenceStreamIdentityV1",
        stream_identity_arguments, 3u);

    for (index = collector->occurrence_len; index > 0u; index--) {
        const CettaLangDefRuntimeOccurrenceV1 *occurrence =
            &collector->occurrences[index - 1u];
        const char *operation_name = ppoccurrence_fold_v1_operation_name(
            &langdef->compiled_fold, occurrence->operation_id);
        Atom *identity_arguments[2];
        Atom *source_identity_arguments[2];
        Atom *position_arguments[5];
        Atom *occurrence_arguments[5];
        Atom *roles;
        Atom *current;
        if (!operation_name)
            return NULL;
        identity_arguments[0] = stream_identity;
        identity_arguments[1] = atom_int(arena, index - 1u);
        source_identity_arguments[0] = langdef_runtime_digest_atom(
            arena, source_digest);
        if (!source_identity_arguments[0])
            return NULL;
        source_identity_arguments[1] = atom_int(
            arena, occurrence->source_id);
        position_arguments[0] = langdef_expr(
            arena, "SourceIdentityV1", source_identity_arguments, 2u);
        position_arguments[1] = atom_int(arena, occurrence->left_scalar);
        position_arguments[2] = atom_int(arena, occurrence->right_scalar);
        position_arguments[3] = atom_int(arena, occurrence->left_byte);
        position_arguments[4] = atom_int(arena, occurrence->right_byte);
        roles = langdef_runtime_roles(
            arena, &langdef->compiled_fold, occurrence);
        if (!roles)
            return NULL;
        occurrence_arguments[0] = langdef_expr(
            arena, "StateOccurrenceIdentityV1",
            identity_arguments, 2u);
        occurrence_arguments[1] = atom_symbol(arena, operation_name);
        occurrence_arguments[2] = langdef_expr(
            arena, "SourcePositionV1", position_arguments, 5u);
        occurrence_arguments[3] = roles;
        occurrence_arguments[4] = tail;
        current = langdef_expr(
            arena, "StateRuntimeOccurrenceV1",
            occurrence_arguments, 5u);
        first = current;
        {
            Atom *next_arguments[1] = {current};
            tail = langdef_expr(
                arena, "StateOccurrenceNextTailV1",
                next_arguments, 1u);
        }
    }
    {
        Atom *stream_arguments[3] = {package, stream_identity, first};
        return langdef_expr(
            arena, "StateRuntimeOccurrenceStreamV1",
            stream_arguments, 3u);
    }
}

typedef struct {
    PPOccurrenceFoldV1Backend state;
    PPOccurrenceFoldV1Backend fold;
    const PPRelationalStateProgramV1Plan *state_plan;
} CettaLangDefCompiledStagesV1;

static bool langdef_compiled_stages_apply(
    void *raw,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    CettaLangDefCompiledStagesV1 *stages = raw;
    if (!stages ||
        !stages->state.apply(stages->state.context, step,
                             error_buf, error_buf_size))
        return false;
    if (pprelational_state_program_v1_operation_resolves_source(
            stages->state_plan, step->operation_id))
        return true;
    return stages->fold.apply(stages->fold.context, step,
                              error_buf, error_buf_size);
}

static bool langdef_compiled_stages_commit(
    void *raw, char *error_buf, size_t error_buf_size) {
    CettaLangDefCompiledStagesV1 *stages = raw;
    return stages &&
           stages->state.commit(stages->state.context,
                                error_buf, error_buf_size) &&
           stages->fold.commit(stages->fold.context,
                               error_buf, error_buf_size);
}

static void langdef_compiled_stages_abort(void *raw) {
    CettaLangDefCompiledStagesV1 *stages = raw;
    if (!stages)
        return;
    stages->state.abort(stages->state.context);
    stages->fold.abort(stages->fold.context);
}

static bool langdef_compiled_stages_nested_commit(
    void *raw, char *error_buf, size_t error_buf_size) {
    (void)raw;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    return true;
}

static void langdef_compiled_stages_nested_abort(void *raw) {
    (void)raw;
}

static Atom *langdef_compiled_state_failure(
    CettaLangDefV1 *langdef,
    const PPRelationalStateProgramV1Run *state_run,
    Arena *arena,
    Atom *source,
    const char *detail) {
    const char *head;
    Atom *arguments[4];

    if (!state_run)
        return langdef_error(arena, source, "compiled state run is absent");
    if (state_run->receipt.failure ==
            PPRELATIONAL_STATE_FAILURE_V1_REJECTED)
        head = "LangDef:StageRejected";
    else if (state_run->receipt.failure ==
                 PPRELATIONAL_STATE_FAILURE_V1_UNSUPPORTED)
        head = "LangDef:StageUnsupported";
    else if (state_run->receipt.failure ==
                 PPRELATIONAL_STATE_FAILURE_V1_RESOURCE)
        head = "LangDef:StageIncomplete";
    else
        return langdef_error(
            arena, source,
            detail && detail[0] ? detail : "compiled state execution failed");
    arguments[0] = atom_symbol(arena, langdef->name);
    arguments[1] = atom_string(arena, langdef->pack.pack_digest);
    arguments[2] = atom_string(arena, langdef->compiled_state.plan_digest);
    arguments[3] = atom_string(
        arena, detail && detail[0] ? detail : "state stage failed");
    return langdef_expr(arena, head, arguments, 4u);
}

static PPRelationalStateProofV1Result langdef_proof_extension_execute(
    void *raw_context,
    const PPRelationalStateProofV1Request *request,
    char *error_buf,
    size_t error_buf_size) {
    CettaLangDefV1 *resource = raw_context;
    PPCertificateGSLTRelationalMachineV1Receipt receipt;
    PPCertificateGSLTRelationalMachineV1Result result;
    PPCertificateGSLTArticleV1Limits limits;
    PPCertificateGSLTRelationalMachineV1Workspace *workspace = NULL;

    if (!resource || !resource->proof_extension_ready || !request ||
        !request->store || !request->state_plan) {
        langdef_set_error(error_buf, error_buf_size,
                          "invalid proof-extension execution request");
        return PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
    limits = ppcertificate_gslt_article_v1_default_limits();
    if (resource->proof_storage_plan_ready &&
        resource->proof_workspace_ready && request->operation &&
        resource->proof_relational.execution.machine) {
        const PPProofStorageCallV1 *call =
            ppproof_storage_plan_v1_call(
                &resource->proof_storage_plan, request->operation,
                request->action_index);
        if (call) {
            const PPProofWorkspacePlanV1 *workspace_plan =
                ppproof_storage_plan_v1_workspace(
                    &resource->proof_storage_plan, request->operation,
                    request->action_index);
            const PPProofRepetitionCachePlanV1 *repetition_cache_plan =
                ppproof_storage_plan_v1_repetition_cache(
                    &resource->proof_storage_plan, request->operation,
                    request->action_index);
            const char *machine =
                resource->proof_relational.execution.machine;
            const char *workspace_carrier =
                request->compressed
                    ? "indexed-stack-proof-call-workspace-v1"
                    : "stack-proof-call-workspace-v1";
            if (!ppproof_storage_call_v1_admits_reusable_workspace(
                    call, request->operation, request->action_index,
                    machine, "proof-call-region-v1") ||
                !ppproof_workspace_plan_v1_admits(
                    workspace_plan, request->operation,
                    request->action_index, machine, workspace_carrier,
                    "proof-call-region-v1") ||
                (resource->proof_storage_plan.repetition_cache_len != 0u &&
                 !ppproof_repetition_cache_plan_v1_admits(
                     repetition_cache_plan, request->operation,
                     request->action_index, machine,
                     "proof-call-region-v1"))) {
                langdef_set_error(
                    error_buf, error_buf_size,
                    "proof workspace realization does not match its generated call site");
                return PPRELATIONAL_STATE_PROOF_V1_INVALID;
            }
            workspace = &resource->proof_workspace;
        }
    }
    if (request->compressed) {
        const PPProofIndexedProgramPlanV1 *indexed_program_plan;
        const PPProofFrameIndexPlanV1 *frame_index_plan;
        PPProofIndexedEffectUnknownV1 indexed_unknown;
        const char *machine;
        PPCertificateGSLTRelationalCompressedInputV1 input = {
            .label = request->label,
            .claim = request->claim,
            .claim_len = request->claim_len,
            .header = request->proof,
            .header_len = request->proof_len,
            .code = request->code,
            .code_len = request->code_len,
        };
        if (!resource->proof_storage_plan_ready ||
            !resource->proof_relational.execution.machine) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated indexed-value admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        indexed_program_plan = ppproof_storage_plan_v1_indexed_program(
            &resource->proof_storage_plan,
            resource->proof_relational.execution.machine);
        frame_index_plan = ppproof_storage_plan_v1_frame_index(
            &resource->proof_storage_plan,
            resource->proof_relational.execution.machine);
        machine = resource->proof_relational.execution.machine;
        if (!indexed_program_plan) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated indexed-program admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        if (!frame_index_plan) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated frame-index admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        indexed_unknown =
            resource->proof_relational.execution.unknown_policy ==
                                  PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM
                              ? PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE
                              : PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
        if (!ppproof_indexed_program_plan_v1_admits(
                indexed_program_plan, request->operation,
                request->action_index, machine,
                request->header_role, request->code_role,
                indexed_program_plan->region, indexed_unknown,
                resource->proof_relational.execution.save_placement,
                resource->proof_relational.execution
                    .header_hypothesis_policy)) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof indexed-program admission does not match its generated call site");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        if (!ppproof_frame_index_plan_v1_admits(
                frame_index_plan, request->operation,
                request->action_index, machine,
                indexed_program_plan->region)) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof frame-index admission does not match its generated call site");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        result =
            ppcertificate_gslt_relational_machine_v1_compressed_with_workspace(
            request->store, request->state_plan, &resource->proof_plan,
            &resource->proof_evidence, &resource->proof_relational,
            indexed_program_plan, frame_index_plan,
            workspace, &input, &limits, &receipt,
            error_buf, error_buf_size);
    } else {
        PPCertificateGSLTRelationalNormalInputV1 input = {
            .label = request->label,
            .claim = request->claim,
            .claim_len = request->claim_len,
            .steps = request->proof,
            .step_len = request->proof_len,
        };
        if (!resource->proof_storage_plan_ready ||
            !resource->proof_relational.execution.machine ||
            !resource->proof_exact_action_cases ||
            resource->proof_exact_action_case_len == 0u) {
            langdef_set_error(
                error_buf, error_buf_size,
                "normal proof lacks generated action admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        result = ppcertificate_gslt_relational_machine_v1_normal_with_workspace(
            request->store, request->state_plan, &resource->proof_plan,
            &resource->proof_evidence, &resource->proof_relational,
            resource->proof_exact_action_cases,
            resource->proof_exact_action_case_len, workspace,
            &input, &limits, &receipt,
            error_buf, error_buf_size);
    }
    switch (result) {
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_OK:
        return PPRELATIONAL_STATE_PROOF_V1_VERIFIED;
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE:
        return PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE;
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_REJECTED:
        return PPRELATIONAL_STATE_PROOF_V1_REJECTED;
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_RESOURCE:
        return PPRELATIONAL_STATE_PROOF_V1_RESOURCE;
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED:
        return PPRELATIONAL_STATE_PROOF_V1_UNSUPPORTED;
    case PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_INVALID:
    default:
        return PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
}

#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
static Atom *langdef_recognize_compiled_bytes(
    CettaLangDefV1 *langdef,
    const uint8_t *bytes,
    size_t len,
    Arena *arena,
    Atom *source,
    uint32_t *value_len_out) {
    PPGuardedLexCursorV1Receipt receipt = {0};
    char error[512] = {0};

    if (!langdef->compiled_program_ready ||
        !ppguarded_lex_cursor_v1_program_run_bytes(
            &langdef->compiled_program, bytes, len,
            langdef_source_work_limit(len), &receipt,
            error, sizeof(error))) {
        return langdef_error(
            arena, source,
            error[0] ? error : "compiled langdef recognition failed");
    }
    if (receipt.outcome == PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, receipt.outcome),
            atom_string(arena, "compiled cursor work limit"),
        };
        return langdef_expr(
            arena, "LangDef:ParseIncomplete", arguments, 4u);
    }
    if (receipt.outcome != PPGUARDED_LEX_CURSOR_V1_ACCEPTED) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, receipt.farthest_byte),
            atom_expr(arena, NULL, 0u),
        };
        return langdef_expr(
            arena, "LangDef:ParseRejected", arguments, 4u);
    }
    {
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, 1u),
        };
        if (value_len_out)
            *value_len_out = 1u;
        return langdef_expr(
            arena, "LangDef:RunAccepted", arguments, 3u);
    }
}
#endif

static Atom *langdef_parse_compiled_bytes(
    CettaLangDefV1 *langdef,
    const uint8_t *bytes,
    size_t len,
    const char *source_path,
    Arena *arena,
    Atom *source,
    bool execute_state,
    CettaLangDefProofExecutionV1 proof_execution,
    bool materialize_values,
    Atom ***values_out,
    uint32_t *value_len_out) {
    CettaLangDefCompiledFoldV1 fold = {
        .arena = arena,
        .plan = &langdef->compiled_fold,
        .materialize_values = materialize_values,
    };
    PPOccurrenceFoldV1Backend backend = {
        .context = &fold,
        .apply = langdef_compiled_fold_apply,
        .commit = langdef_compiled_fold_commit,
        .abort = langdef_compiled_fold_abort,
    };
    PPRelationalStateProgramV1Run state_run = {0};
    PPRelationalStateProofV1Backend proof_backend = {0};
    CettaLangDefCompiledStagesV1 stages = {0};
    PPOccurrenceFileResolverV1 source_resolver;
    PPOccurrenceSourceResolverV1 source_resolver_interface = {0};
    PPOccurrenceFoldV1Backend nested_backend = {0};
    PPOccurrenceFoldV1Receipt receipt = {0};
    Atom *result = NULL;
    char error[512] = {0};
    bool state_run_ready = false;
    PPGuardedLexCursorV1Observation parser_observation =
        materialize_values
            ? PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE
            : PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY;
    PPRelationalStateObservationV1 state_observation =
        materialize_values
            ? PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT
            : PPRELATIONAL_STATE_OBSERVATION_V1_COUNTERS_ONLY;
    bool use_direct_frame_machine = false;

    ppoccurrence_file_resolver_v1_init(&source_resolver);

#if !CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    if (proof_execution ==
        CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC) {
        result = langdef_error(
            arena, source,
            "diagnostic proof backend is absent from this build");
        goto done;
    }
#endif
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    if (execute_state && proof_execution ==
            CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC &&
        !langdef->compiled_state_ready) {
        result = langdef_error(
            arena, source,
            "diagnostic frame-cache backend requires a compiled state program");
        goto done;
    }
#endif
    if (execute_state && proof_execution ==
            CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT &&
        !langdef_generated_proof_prepare(
            langdef, error, sizeof(error))) {
        result = langdef_error(
            arena, source,
            error[0] ? error
                     : "generated relational proof backend preparation failed");
        goto done;
    }
    if (execute_state && proof_execution ==
            CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY &&
        langdef->proof_extension_ready &&
        langdef->proof_native_types_ready &&
        langdef->proof_storage_plan_ready) {
        if (!langdef_proof_frame_prepare(
                langdef, error, sizeof(error))) {
            result = langdef_error(
                arena, source,
                error[0] ? error
                         : "direct frame-machine admission failed");
            goto done;
        }
        use_direct_frame_machine = true;
    }

    if (execute_state && langdef->compiled_state_ready) {
        stages.fold = backend;
        stages.state_plan = &langdef->compiled_state;
        if (source_path) {
            if (!ppoccurrence_file_resolver_v1_configure(
                    &source_resolver,
                    &langdef->compiled_program, &langdef->compiled_fold,
                    source_path, bytes, len,
                    parser_observation,
                    LANGDEF_MINIMUM_WORK_LIMIT,
                    LANGDEF_WORK_PER_SOURCE_BYTE,
                    LANGDEF_DEFAULT_INCLUDE_DEPTH,
                    error, sizeof(error))) {
                result = langdef_error(
                    arena, source,
                    error[0] ? error
                             : "source resolver initialization failed");
                goto done;
            }
            source_resolver_interface =
                ppoccurrence_file_resolver_v1_interface(
                    &source_resolver);
            nested_backend = (PPOccurrenceFoldV1Backend){
                .context = &stages,
                .apply = langdef_compiled_stages_apply,
                .commit = langdef_compiled_stages_nested_commit,
                .abort = langdef_compiled_stages_nested_abort,
            };
        }
        if (proof_execution ==
            CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY &&
            langdef->proof_extension_ready &&
            !use_direct_frame_machine) {
            proof_backend.context = langdef;
            proof_backend.execute = langdef_proof_extension_execute;
        }
        if (proof_execution ==
                CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT &&
            langdef->proof_generated_runtime_ready) {
            proof_backend = ppcertificate_gslt_relational_runtime_v1_backend(
                &langdef->proof_generated_runtime);
        }
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
        if (proof_execution ==
                CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC &&
            !langdef_proof_frame_prepare(
                langdef, error, sizeof(error))) {
            result = langdef_error(
                arena, source,
                error[0] ? error
                         : "diagnostic frame-cache admission failed");
            goto done;
        }
        bool state_initialized;
        if (use_direct_frame_machine || proof_execution ==
                CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC) {
            state_initialized =
                pprelational_state_program_v1_run_init_with_frame_cache(
                  &state_run, &langdef->compiled_fold,
                  &langdef->compiled_state,
                  source_path ? &source_resolver_interface : NULL,
                  source_path ? &nested_backend : NULL,
                  langdef->proof_frame_cache_admissions,
                  langdef->proof_frame_decoder_len,
                  state_observation,
                  error, sizeof(error));
        } else
#else
        bool state_initialized;
#endif
        {
            if (use_direct_frame_machine) {
                state_initialized =
                    pprelational_state_program_v1_run_init_with_frame_cache(
                      &state_run, &langdef->compiled_fold,
                      &langdef->compiled_state,
                      source_path ? &source_resolver_interface : NULL,
                      source_path ? &nested_backend : NULL,
                      langdef->proof_frame_cache_admissions,
                      langdef->proof_frame_decoder_len,
                      state_observation,
                      error, sizeof(error));
            } else {
                state_initialized =
                    pprelational_state_program_v1_run_init_with_proof_backend(
                      &state_run, &langdef->compiled_fold,
                      &langdef->compiled_state,
                      source_path ? &source_resolver_interface : NULL,
                      source_path ? &nested_backend : NULL,
                      proof_backend.execute ? &proof_backend : NULL,
                      state_observation,
                      error, sizeof(error));
            }
        }
        if (!state_initialized) {
            result = langdef_error(
                arena, source,
                error[0] ? error : "compiled state initialization failed");
            goto done;
        }
        state_run_ready = true;
        stages.state = pprelational_state_program_v1_backend(&state_run);
        backend = (PPOccurrenceFoldV1Backend){
            .context = &stages,
            .apply = langdef_compiled_stages_apply,
            .commit = langdef_compiled_stages_commit,
            .abort = langdef_compiled_stages_abort,
        };
    }

    bool fold_ok = langdef->compiled_span_mask_ready
        ? ppoccurrence_fold_v1_run_bytes_with_span_mask_prevalidated(
              &langdef->compiled_program, &langdef->compiled_fold,
              &langdef->compiled_span_mask,
              bytes, len, &backend, parser_observation,
              langdef_source_work_limit(len), &receipt,
              error, sizeof(error))
        : ppoccurrence_fold_v1_run_bytes_prevalidated(
              &langdef->compiled_program, &langdef->compiled_fold,
              bytes, len, &backend, parser_observation,
              langdef_source_work_limit(len), &receipt,
              error, sizeof(error));
    if (!fold_ok) {
        if (receipt.parser_receipt.outcome ==
            PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT) {
            Atom *arguments[4] = {
                atom_symbol(arena, langdef->name),
                atom_string(arena, langdef->pack.pack_digest),
                atom_int(arena, receipt.parser_receipt.outcome),
                atom_string(arena, "compiled cursor work limit"),
            };
            result = langdef_expr(arena, "LangDef:ParseIncomplete",
                                  arguments, 4u);
        } else {
            result = state_run_ready &&
                         state_run.receipt.failure !=
                             PPRELATIONAL_STATE_FAILURE_V1_NONE
                         ? langdef_compiled_state_failure(
                               langdef, &state_run, arena, source, error)
                         : langdef_error(
                               arena, source,
                               error[0] ? error
                                        : "compiled langdef parser failed");
        }
        goto done;
    }
    if (receipt.parser_receipt.outcome ==
        PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, receipt.parser_receipt.outcome),
            atom_string(arena, "compiled cursor work limit"),
        };
        result = langdef_expr(arena, "LangDef:ParseIncomplete",
                              arguments, 4u);
        goto done;
    }
    if (receipt.parser_receipt.outcome !=
            PPGUARDED_LEX_CURSOR_V1_ACCEPTED ||
        !receipt.committed || !fold.committed) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, receipt.parser_receipt.farthest_byte),
            atom_expr(arena, NULL, 0u),
        };
        result = langdef_expr(arena, "LangDef:ParseRejected",
                              arguments, 4u);
        goto done;
    }
    if (state_run_ready && !state_run.committed) {
        result = langdef_compiled_state_failure(
            langdef, &state_run, arena, source, error);
        goto done;
    }
    if (state_run_ready &&
        state_run.receipt.incomplete_proof_len != 0u) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_string(arena, langdef->compiled_state.plan_digest),
            atom_string(arena, "proof stage contains an incomplete proof"),
        };
        result = langdef_expr(
            arena, "LangDef:StageAcceptedIncomplete", arguments, 4u);
        goto done;
    }
    if (!materialize_values) {
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, fold.value_len),
        };
        result = langdef_expr(
            arena, "LangDef:RunAccepted", arguments, 3u);
        if (value_len_out)
            *value_len_out = fold.value_len;
    } else {
        Atom **published = NULL;
        Atom *value_list;
        Atom *arguments[3];
        if (fold.value_len > 0u) {
            published = arena_alloc(arena,
                                    sizeof(*published) * fold.value_len);
            memcpy(published, fold.values,
                   sizeof(*published) * fold.value_len);
        }
        value_list = atom_expr(arena, published, fold.value_len);
        arguments[0] = atom_symbol(arena, langdef->name);
        arguments[1] = atom_string(arena, langdef->pack.pack_digest);
        arguments[2] = value_list;
        result = langdef_expr(arena, "LangDef:ParseAccepted", arguments, 3u);
        if (values_out)
            *values_out = published;
        if (value_len_out)
            *value_len_out = fold.value_len;
    }

done:
    if (state_run_ready)
        pprelational_state_program_v1_run_free(&state_run);
    ppoccurrence_file_resolver_v1_free(&source_resolver);
    free(fold.values);
    return result;
}

static Atom *langdef_parse_runtime_occurrence_bytes(
    CettaLangDefV1 *langdef,
    const uint8_t *bytes,
    size_t len,
    const char *source_path,
    Arena *arena,
    Atom *source) {
    CettaLangDefRuntimeOccurrenceCollectorV1 collector = {
        .plan = &langdef->compiled_fold,
    };
    PPOccurrenceFoldV1Backend collector_backend = {
        .context = &collector,
        .apply = langdef_runtime_occurrence_apply,
        .commit = langdef_runtime_occurrence_commit,
        .abort = langdef_runtime_occurrence_abort,
    };
    PPOccurrenceFileResolverV1 resolver;
    PPOccurrenceSourceResolverV1 resolver_interface = {0};
    PPOccurrenceSourceCompositionV1 composition = {0};
    PPOccurrenceFoldV1Backend backend = {0};
    PPOccurrenceFoldV1Receipt receipt = {0};
    Atom *result = NULL;
    char error[512] = {0};
    bool configured = false;
    bool composition_ready = false;
    bool fold_ok;

    if (!langdef || (!bytes && len != 0u) || !source_path || !arena ||
        !langdef->compiled_program_ready ||
        !langdef->compiled_fold_ready ||
        !langdef->compiled_state_ready ||
        !langdef->compiled_source_control_ready) {
        return langdef_error(
            arena, source,
            "runtime occurrence parsing requires a fully compiled source package");
    }
    ppoccurrence_file_resolver_v1_init(&resolver);
    configured = ppoccurrence_file_resolver_v1_configure_controlled(
        &resolver,
        &langdef->compiled_program,
        &langdef->compiled_fold,
        &langdef->compiled_source_control,
        source_path,
        bytes,
        len,
        PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
        LANGDEF_MINIMUM_WORK_LIMIT,
        LANGDEF_WORK_PER_SOURCE_BYTE,
        LANGDEF_DEFAULT_INCLUDE_DEPTH,
        error,
        sizeof(error));
    if (!configured) {
        result = langdef_error(
            arena, source,
            error[0] ? error : "runtime source resolver initialization failed");
        goto done;
    }
    resolver_interface = ppoccurrence_file_resolver_v1_interface(&resolver);
    composition_ready = ppoccurrence_source_composition_v1_init(
        &composition,
        &langdef->compiled_fold,
        &langdef->compiled_state,
        &resolver_interface,
        &collector_backend,
        error,
        sizeof(error));
    if (!composition_ready) {
        result = langdef_error(
            arena, source,
            error[0] ? error : "runtime source composition initialization failed");
        goto done;
    }
    backend = ppoccurrence_source_composition_v1_backend(&composition);
    fold_ok = langdef->compiled_span_mask_ready
        ? ppoccurrence_fold_v1_run_bytes_with_span_mask_prevalidated(
              &langdef->compiled_program,
              &langdef->compiled_fold,
              &langdef->compiled_span_mask,
              bytes,
              len,
              &backend,
              PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
              langdef_source_work_limit(len),
              &receipt,
              error,
              sizeof(error))
        : ppoccurrence_fold_v1_run_bytes_prevalidated(
              &langdef->compiled_program,
              &langdef->compiled_fold,
              bytes,
              len,
              &backend,
              PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
              langdef_source_work_limit(len),
              &receipt,
              error,
              sizeof(error));
    if (!fold_ok || receipt.parser_receipt.outcome !=
            PPGUARDED_LEX_CURSOR_V1_ACCEPTED ||
        !receipt.committed || !composition.committed ||
        !collector.committed) {
        Atom *arguments[3];
        const char *head = receipt.parser_receipt.outcome ==
                PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT
            ? "LangDef:OccurrenceStreamIncomplete"
            : "LangDef:OccurrenceStreamRejected";
        arguments[0] = atom_symbol(arena, langdef->name);
        arguments[1] = atom_string(arena, langdef->pack.pack_digest);
        arguments[2] = atom_string(
            arena, error[0] ? error : "source parsing did not accept");
        result = langdef_expr(arena, head, arguments, 3u);
        goto done;
    }
    {
        Atom *stream = langdef_runtime_occurrence_stream(
            langdef, &collector, composition.source_digest, arena);
        Atom *arguments[5];
        if (!stream) {
            result = langdef_error(
                arena, source,
                "runtime occurrence stream construction failed");
            goto done;
        }
        arguments[0] = atom_symbol(arena, langdef->name);
        arguments[1] = atom_string(arena, langdef->pack.pack_digest);
        arguments[2] = atom_string(
            arena, langdef->compiled_state.plan_digest);
        arguments[3] = atom_symbol(arena, composition.source_digest);
        arguments[4] = stream;
        result = langdef_expr(
            arena, "LangDef:OccurrenceStreamAccepted", arguments, 5u);
    }

done:
    if (composition_ready && composition.active)
        backend.abort(backend.context);
    ppoccurrence_file_resolver_v1_free(&resolver);
    langdef_runtime_occurrence_collector_clear(&collector);
    return result;
}
#endif

static Atom *langdef_expected(Arena *arena, const CettaLangDefV1 *langdef,
                              const CettaLpNativeUtf8Forest *forest) {
    Atom **items = NULL;
    Atom *result;
    uint32_t index;
    if (forest->expected_terminal_len > 0u)
        items = arena_alloc(arena, sizeof(*items) * forest->expected_terminal_len);
    for (index = 0u; index < forest->expected_terminal_len; index++) {
        uint32_t terminal = forest->expected_terminal_ids[index];
        if (terminal >= langdef->pack.terminal_len)
            return NULL;
        items[index] = atom_deep_copy(
            arena, langdef->pack.terminals[terminal].identity);
    }
    result = atom_expr(arena, items, forest->expected_terminal_len);
    return result;
}

static bool langdef_forest_has_nonempty_state_span(
    const CettaLpNativeUtf8Forest *forest, uint32_t state_id) {
    uint32_t index;

    if (!forest)
        return false;
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL &&
            node->symbol_id == state_id &&
            node->byte_left < node->byte_right) {
            return true;
        }
    }
    return false;
}

static bool langdef_sequence_buffer_append(
    uint8_t **buffer, size_t *len, size_t *capacity,
    size_t limit, uint8_t byte,
    char *error, size_t error_size) {
    size_t next_capacity;
    uint8_t *next;

    if (!buffer || !len || !capacity || *len >= limit)
        return false;
    if (*len == *capacity) {
        next_capacity = *capacity ? *capacity * 2u : 4096u;
        if (next_capacity < *capacity || next_capacity > limit)
            next_capacity = limit;
        if (next_capacity <= *capacity)
            return false;
        next = realloc(*buffer, next_capacity);
        if (!next)
            return langdef_set_error(
                error, error_size,
                "out of memory growing delimited recognition buffer");
        *buffer = next;
        *capacity = next_capacity;
    }
    (*buffer)[(*len)++] = byte;
    return true;
}

/*
 * Recognize a file whose top-level language is a concatenable sequence of
 * records.  Delimiter bytes are candidate boundaries only: the unchanged
 * ParserPack must accept the complete buffered prefix, and the accepted
 * forest must contain a nonempty occurrence of record_state, before a prefix
 * is committed.  The caller remains responsible for establishing that
 * concatenation of accepted prefixes preserves its top-level language.
 */
static Atom *langdef_recognize_delimited_sequence_file(
    CettaLangDefV1 *langdef, const Atom *record_state,
    uint8_t delimiter, uint32_t candidate_limit,
    size_t buffer_limit, const char *source_path,
    Arena *arena, Atom *source) {
    static const size_t read_capacity = 65536u;
    PPNativeV1Prepared prepared;
    PPNativeV1Result recognized;
    FILE *stream = NULL;
    uint8_t *buffer = NULL;
    uint8_t read_buffer[65536];
    size_t buffer_len = 0u;
    size_t buffer_capacity = 0u;
    size_t max_buffer_len = 0u;
    size_t committed_byte_len = 0u;
    uint32_t candidate_count = 0u;
    uint32_t committed_chunk_count = 0u;
    int32_t record_state_id;
    char error[512] = {0};
    Atom *result = NULL;

    ppnative_v1_prepared_init(&prepared);
    ppnative_v1_result_init(&recognized);
    if (!langdef || !record_state || !source_path || candidate_limit == 0u ||
        buffer_limit == 0u) {
        result = langdef_error(
            arena, source, "invalid delimited sequence recognition request");
        goto done;
    }
    record_state_id = ppnative_v1_state_find(&langdef->pack, record_state);
    if (record_state_id < 0) {
        result = langdef_error(
            arena, source,
            "delimited sequence record state is absent from ParserPack");
        goto done;
    }
    if (!ppnative_v1_prepare(
            &prepared, &langdef->pack, langdef->wire.start,
            error, sizeof(error))) {
        result = langdef_error(
            arena, source,
            error[0] ? error : "failed to prepare delimited recognizer");
        goto done;
    }
    stream = fopen(source_path, "rb");
    if (!stream) {
        (void)snprintf(error, sizeof(error), "cannot open %s: %s",
                       source_path, strerror(errno));
        result = langdef_error(arena, source, error);
        goto done;
    }
    for (;;) {
        size_t read_len = fread(read_buffer, 1u, read_capacity, stream);
        size_t read_index;
        for (read_index = 0u; read_index < read_len; read_index++) {
            uint8_t byte = read_buffer[read_index];
            if (buffer_len >= buffer_limit) {
                Atom *arguments[4];
                (void)snprintf(
                    error, sizeof(error),
                    "delimited recognition buffer limit %zu at byte %zu",
                    buffer_limit, committed_byte_len + buffer_len);
                arguments[0] = atom_symbol(arena, langdef->name);
                arguments[1] = atom_string(arena, langdef->pack.pack_digest);
                arguments[2] = atom_int(arena, PPNATIVE_V1_RECOGNIZER_LIMIT);
                arguments[3] = atom_string(arena, error);
                result = langdef_expr(
                    arena, "LangDef:ParseIncomplete", arguments, 4u);
                goto done;
            }
            if (!langdef_sequence_buffer_append(
                    &buffer, &buffer_len, &buffer_capacity,
                    buffer_limit, byte, error, sizeof(error))) {
                result = langdef_error(
                    arena, source,
                    error[0] ? error
                             : "delimited recognition buffer limit reached");
                goto done;
            }
            if (buffer_len > max_buffer_len)
                max_buffer_len = buffer_len;
            if (byte != delimiter)
                continue;
            if (candidate_count == candidate_limit) {
                Atom *arguments[4];
                (void)snprintf(
                    error, sizeof(error),
                    "delimited recognition candidate limit %u at byte %zu",
                    candidate_limit, committed_byte_len + buffer_len - 1u);
                arguments[0] = atom_symbol(arena, langdef->name);
                arguments[1] = atom_string(arena, langdef->pack.pack_digest);
                arguments[2] = atom_int(arena, PPNATIVE_V1_RECOGNIZER_LIMIT);
                arguments[3] = atom_string(arena, error);
                result = langdef_expr(
                    arena, "LangDef:ParseIncomplete", arguments, 4u);
                goto done;
            }
            candidate_count++;
            ppnative_v1_result_free(&recognized);
            ppnative_v1_result_init(&recognized);
            error[0] = '\0';
            if (!ppgll_v1_prepared_recognize(
                    &prepared, buffer, buffer_len,
                    langdef_source_work_limit_u32(buffer_len),
                    &recognized, error, sizeof(error))) {
                result = langdef_error(
                    arena, source,
                    error[0] ? error : "delimited ParserPack recognition failed");
                goto done;
            }
            if (recognized.outcome != PPNATIVE_V1_COMPLETED) {
                Atom *arguments[4] = {
                    atom_symbol(arena, langdef->name),
                    atom_string(arena, langdef->pack.pack_digest),
                    atom_int(arena, recognized.outcome),
                    atom_string(arena, recognized.detail),
                };
                result = langdef_expr(
                    arena, "LangDef:ParseIncomplete", arguments, 4u);
                goto done;
            }
            if (recognized.accepted &&
                langdef_forest_has_nonempty_state_span(
                    &recognized.forest, (uint32_t)record_state_id)) {
                if (committed_chunk_count == UINT32_MAX) {
                    result = langdef_error(
                        arena, source,
                        "delimited recognition chunk count overflow");
                    goto done;
                }
                committed_chunk_count++;
                committed_byte_len += buffer_len;
                buffer_len = 0u;
            }
        }
        if (read_len < read_capacity) {
            if (ferror(stream)) {
                (void)snprintf(error, sizeof(error), "cannot read %s",
                               source_path);
                result = langdef_error(arena, source, error);
                goto done;
            }
            break;
        }
    }

    ppnative_v1_result_free(&recognized);
    ppnative_v1_result_init(&recognized);
    error[0] = '\0';
    if (!ppgll_v1_prepared_recognize(
            &prepared, buffer, buffer_len,
            langdef_source_work_limit_u32(buffer_len),
            &recognized, error, sizeof(error))) {
        result = langdef_error(
            arena, source,
            error[0] ? error : "final delimited ParserPack recognition failed");
        goto done;
    }
    if (recognized.outcome != PPNATIVE_V1_COMPLETED) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, recognized.outcome),
            atom_string(arena, recognized.detail),
        };
        result = langdef_expr(
            arena, "LangDef:ParseIncomplete", arguments, 4u);
        goto done;
    }
    if (!recognized.accepted) {
        Atom *expected = langdef_expected(
            arena, langdef, &recognized.forest);
        Atom *arguments[4];
        if (!expected) {
            result = langdef_error(
                arena, source,
                "delimited recognition expected-set construction failed");
            goto done;
        }
        arguments[0] = atom_symbol(arena, langdef->name);
        arguments[1] = atom_string(arena, langdef->pack.pack_digest);
        arguments[2] = atom_int(
            arena, committed_byte_len + recognized.forest.farthest_byte);
        arguments[3] = expected;
        result = langdef_expr(
            arena, "LangDef:ParseRejected", arguments, 4u);
        goto done;
    }
    {
        Atom *arguments[5] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, committed_chunk_count),
            atom_int(arena, candidate_count),
            atom_int(arena, max_buffer_len),
        };
        result = langdef_expr(
            arena, "LangDef:DelimitedRunAccepted", arguments, 5u);
    }

done:
    if (stream)
        (void)fclose(stream);
    free(buffer);
    ppnative_v1_result_free(&recognized);
    ppnative_v1_prepared_free(&prepared);
    return result;
}

static Atom *langdef_parse_bytes(CettaLangDefV1 *langdef,
                                 const uint8_t *bytes, size_t len,
                                 const char *source_path,
                                 Arena *arena, Atom *source,
                                 bool execute_state,
                                 CettaLangDefProofExecutionV1 proof_execution,
                                 bool materialize_values,
                                 Atom ***values_out, uint32_t *value_len_out) {
    PPNativeV1Result parsed;
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    PPGuardedLexExecV1Result guarded;
#endif
    PPNativeV1Result *parsed_result = &parsed;
    Atom **values = NULL;
    char error[512] = {0};
    Atom *result = NULL;
    uint32_t index;
    bool recognition_only = !execute_state && !materialize_values;

#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    if (langdef->compiled_cursor && recognition_only)
        return langdef_recognize_compiled_bytes(
            langdef, bytes, len, arena, source, value_len_out);
    if (langdef->compiled_cursor)
        return langdef_parse_compiled_bytes(
            langdef, bytes, len, source_path, arena, source,
            execute_state, proof_execution, materialize_values,
            values_out, value_len_out);
#endif

    if (proof_execution !=
        CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY)
        return langdef_error(
            arena, source,
            "diagnostic proof backend requires a compiled admitted state program");

    (void)execute_state;
    (void)source_path;

    ppnative_v1_result_init(&parsed);
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    ppguarded_lex_exec_v1_result_init(&guarded);
    if (!recognition_only && langdef->parser_guarded_exec_ready) {
        PPGuardedLexExecV1Limits limits =
            langdef_guarded_parser_limits();
        if (!ppguarded_lex_exec_v1_run_bytes(
                &langdef->pack, langdef->wire.start,
                &langdef->parser_lexical_plan,
                &langdef->parser_guard_plan,
                &langdef->parser_guarded_plan,
                &langdef->parser_guarded_exec,
                bytes, len, &limits, &guarded,
                error, sizeof(error))) {
            result = langdef_error(
                arena, source,
                error[0] ? error : "guarded langdef parser failed");
            goto done;
        }
        parsed_result = &guarded.gll;
    } else
#endif
    if (!(recognition_only
              ? ppgll_v1_recognize(
                    &langdef->pack, langdef->wire.start,
                    bytes, len, langdef_source_work_limit_u32(len),
                    &parsed, error, sizeof(error))
              : ppgll_v1_parse(
                    &langdef->pack, langdef->wire.start,
                    bytes, len, langdef_source_work_limit_u32(len),
                    LANGDEF_DEFAULT_REPLAY_DEPTH,
                    LANGDEF_DEFAULT_RESULT_LIMIT, &parsed,
                    error, sizeof(error)))) {
        result = langdef_error(arena, source,
                               error[0] ? error : "langdef parser failed");
        goto done;
    }
    if (parsed_result->outcome != PPNATIVE_V1_COMPLETED) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed_result->outcome),
            atom_string(arena, parsed_result->detail),
        };
        result = langdef_expr(arena, "LangDef:ParseIncomplete", arguments, 4u);
        goto done;
    }
    if (!parsed_result->accepted) {
        Atom *expected = langdef_expected(
            arena, langdef, &parsed_result->forest);
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed_result->forest.farthest_byte),
            expected,
        };
        if (!expected) {
            result = langdef_error(arena, source,
                                   "langdef expected-set construction failed");
            goto done;
        }
        result = langdef_expr(arena, "LangDef:ParseRejected", arguments, 4u);
        goto done;
    }
    if (recognition_only) {
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, 1u),
        };
        result = langdef_expr(
            arena, "LangDef:RunAccepted", arguments, 3u);
        if (value_len_out)
            *value_len_out = 1u;
        goto done;
    }
    if (!materialize_values) {
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed_result->semantic_result_len),
        };
        result = langdef_expr(
            arena, "LangDef:RunAccepted", arguments, 3u);
        if (value_len_out)
            *value_len_out = parsed_result->semantic_result_len;
        goto done;
    }
    if (parsed_result->semantic_result_len > 0u)
        values = arena_alloc(arena,
                             sizeof(*values) *
                                 parsed_result->semantic_result_len);
    for (index = 0u; index < parsed_result->semantic_result_len; index++) {
        Atom *value;
        if (!langdef_result_value(
                parsed_result->semantic_results[index], &value)) {
            result = langdef_error(arena, source,
                                   "compiled parser returned an open result");
            goto done;
        }
        values[index] = atom_deep_copy(arena, value);
        if (!values[index]) {
            result = langdef_error(arena, source,
                                   "langdef result copy failed");
            goto done;
        }
    }
    {
        Atom *value_list = atom_expr(
            arena, values, parsed_result->semantic_result_len);
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            value_list,
        };
        result = langdef_expr(arena, "LangDef:ParseAccepted", arguments, 3u);
    }
    if (values_out)
        *values_out = values;
    if (value_len_out)
        *value_len_out = parsed_result->semantic_result_len;

done:
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    ppguarded_lex_exec_v1_result_free(&guarded);
#endif
    ppnative_v1_result_free(&parsed);
    return result;
}

static CettaLangDefRunStatusV1 langdef_run_status(Atom *result) {
    if (!result || atom_is_error(result))
        return CETTA_LANGDEF_RUN_V1_ERROR;
    if (cetta_langdef_expr_head(result, "LangDef:ParseAccepted", 3u))
        return CETTA_LANGDEF_RUN_V1_ACCEPTED;
    if (cetta_langdef_expr_head(result, "LangDef:RunAccepted", 3u))
        return CETTA_LANGDEF_RUN_V1_ACCEPTED;
    if (cetta_langdef_expr_head(
            result, "LangDef:StageAcceptedIncomplete", 4u))
        return CETTA_LANGDEF_RUN_V1_ACCEPTED_INCOMPLETE;
    if (cetta_langdef_expr_head(result, "LangDef:ParseRejected", 4u) ||
        cetta_langdef_expr_head(result, "LangDef:StageRejected", 4u))
        return CETTA_LANGDEF_RUN_V1_REJECTED;
    if (cetta_langdef_expr_head(result, "LangDef:ParseIncomplete", 4u) ||
        cetta_langdef_expr_head(result, "LangDef:StageIncomplete", 4u))
        return CETTA_LANGDEF_RUN_V1_INCOMPLETE;
    if (cetta_langdef_expr_head(result, "LangDef:StageUnsupported", 4u))
        return CETTA_LANGDEF_RUN_V1_UNSUPPORTED;
    return CETTA_LANGDEF_RUN_V1_ERROR;
}

bool cetta_langdef_run_bytes_with_proof_execution_v1(
    const char *manifest_path,
    const uint8_t *bytes, size_t len,
    const char *source_path,
    CettaLangDefProofExecutionV1 proof_execution,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size) {
    CettaLangDefV1 *resource = NULL;
    Atom *source;
    Atom *parsed;
    CettaLangDefRunStatusV1 status;
    bool ok = false;

    if (!manifest_path || (!bytes && len != 0u) || !arena || !receipt ||
        proof_execution >
            CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT)
        return langdef_set_error(error, error_size,
                                 "invalid language-definition run request");
    receipt->status = CETTA_LANGDEF_RUN_V1_ERROR;
    receipt->result = NULL;
    resource = langdef_load_resource(manifest_path, error, error_size);
    if (!resource)
        goto done;
    if (resource->program_path || resource->import_entry) {
        langdef_set_error(
            error, error_size,
            "language definition requires its generated import program");
        goto done;
    }
    if (resource->compiled_cursor_path) {
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
        if (!resource->compiled_cursor) {
            langdef_set_error(error, error_size,
                              "compiled language-definition cursor is unavailable");
            goto done;
        }
#else
        langdef_set_error(error, error_size,
                          "compiled language-definition cursor is unavailable");
        goto done;
#endif
    }
    source = atom_symbol(arena, "langdef:run");
    parsed = langdef_parse_bytes(resource, bytes, len, source_path,
                                 arena, source, true, proof_execution,
                                 false,
                                 NULL, NULL);
    status = langdef_run_status(parsed);
    if (status == CETTA_LANGDEF_RUN_V1_ACCEPTED &&
        cetta_langdef_expr_head(parsed, "LangDef:ParseAccepted", 3u)) {
        Atom *values = parsed->expr.elems[3];
        Atom *arguments[3];
        if (!values || values->kind != ATOM_EXPR) {
            langdef_set_error(error, error_size,
                              "language-definition result has no value list");
            goto done;
        }
        arguments[0] = atom_symbol(arena, resource->name);
        arguments[1] = atom_string(arena, resource->pack.pack_digest);
        arguments[2] = atom_int(arena, values->expr.len);
        parsed = langdef_expr(arena, "LangDef:RunAccepted", arguments, 3u);
    } else if (status == CETTA_LANGDEF_RUN_V1_ACCEPTED &&
               !cetta_langdef_expr_head(
                   parsed, "LangDef:RunAccepted", 3u)) {
        langdef_set_error(error, error_size,
                          "language-definition run returned an invalid acceptance receipt");
        goto done;
    } else if (status == CETTA_LANGDEF_RUN_V1_ERROR &&
               (!parsed || !atom_is_error(parsed))) {
        langdef_set_error(error, error_size,
                          "language-definition run returned an invalid receipt");
        goto done;
    }
    receipt->status = status;
    receipt->result = parsed;
    ok = parsed != NULL;

done:
    langdef_resource_free(resource);
    return ok;
}

bool cetta_langdef_run_bytes_v1(
    const char *manifest_path,
    const uint8_t *bytes, size_t len,
    const char *source_path,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size) {
    return cetta_langdef_run_bytes_with_proof_execution_v1(
        manifest_path, bytes, len, source_path,
        CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
        arena, receipt, error, error_size);
}

bool cetta_langdef_run_file_with_proof_execution_v1(
    const char *manifest_path, const char *source_path,
    CettaLangDefProofExecutionV1 proof_execution,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    bool ok;

    if (!cetta_langdef_slurp(source_path, &bytes, &len, error, error_size))
        return false;
    ok = cetta_langdef_run_bytes_with_proof_execution_v1(
        manifest_path, bytes, len, source_path, proof_execution,
        arena, receipt, error, error_size);
    free(bytes);
    return ok;
}

bool cetta_langdef_run_file_v1(
    const char *manifest_path, const char *source_path,
    Arena *arena, CettaLangDefRunReceiptV1 *receipt,
    char *error, size_t error_size) {
    return cetta_langdef_run_file_with_proof_execution_v1(
        manifest_path, source_path,
        CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
        arena, receipt, error, error_size);
}

static bool langdef_utf8_append(uint8_t **bytes, size_t *len, size_t *cap,
                                uint32_t codepoint) {
    uint8_t encoded[4];
    size_t width;
    uint8_t *next;
    if (codepoint == 0u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        return false;
    if (codepoint <= 0x7fu) {
        encoded[0] = (uint8_t)codepoint;
        width = 1u;
    } else if (codepoint <= 0x7ffu) {
        encoded[0] = (uint8_t)(0xc0u | (codepoint >> 6u));
        encoded[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        width = 2u;
    } else if (codepoint <= 0xffffu) {
        encoded[0] = (uint8_t)(0xe0u | (codepoint >> 12u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        width = 3u;
    } else {
        encoded[0] = (uint8_t)(0xf0u | (codepoint >> 18u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        width = 4u;
    }
    if (*len > SIZE_MAX - width - 1u)
        return false;
    if (*len + width + 1u > *cap) {
        size_t new_cap = *cap ? *cap : 32u;
        while (new_cap < *len + width + 1u) {
            if (new_cap > SIZE_MAX / 2u)
                return false;
            new_cap *= 2u;
        }
        next = realloc(*bytes, new_cap);
        if (!next)
            return false;
        *bytes = next;
        *cap = new_cap;
    }
    memcpy(*bytes + *len, encoded, width);
    *len += width;
    (*bytes)[*len] = 0u;
    return true;
}

static bool langdef_codepoints_collect(Atom *term, uint8_t **bytes,
                                       size_t *len, size_t *cap,
                                       uint32_t depth) {
    int64_t value;
    if (depth > 1048576u)
        return false;
    if (atom_is_symbol(term, "nil"))
        return true;
    if (atom_is_symbol(term, "none"))
        return true;
    if (cetta_langdef_expr_head(term, "some", 1u)) {
        return langdef_codepoints_collect(
            term->expr.elems[1], bytes, len, cap, depth + 1u);
    }
    if (cetta_langdef_expr_head(term, "cp", 1u)) {
        Atom *raw = term->expr.elems[1];
        if (!raw || raw->kind != ATOM_GROUNDED || raw->ground.gkind != GV_INT)
            return false;
        value = raw->ground.ival;
        return value >= 0 && value <= 0x10ffff &&
               langdef_utf8_append(bytes, len, cap, (uint32_t)value);
    }
    if (cetta_langdef_expr_head(term, "cons", 2u) ||
        cetta_langdef_expr_head(term, "pair", 2u)) {
        return langdef_codepoints_collect(term->expr.elems[1], bytes, len,
                                          cap, depth + 1u) &&
               langdef_codepoints_collect(term->expr.elems[2], bytes, len,
                                          cap, depth + 1u);
    }
    return false;
}

static bool deterministic_equation_sources(
    Atom *envelope, const char **paths, size_t *path_count,
    char *error, size_t error_size) {
    size_t count;

    if (!envelope || !paths || !path_count ||
        envelope->kind != ATOM_EXPR || !envelope->expr.elems ||
        envelope->expr.len < 2u ||
        !atom_is_symbol(
            envelope->expr.elems[0], "LangDef:EquationSources"))
        return langdef_set_error(
            error, error_size,
            "deterministic equation sources must be a nonempty LangDef:EquationSources envelope");
    count = (size_t)envelope->expr.len - 1u;
    if (count > CETTA_LANGDEF_MAX_EXTENSION_SOURCES)
        return langdef_set_error(
            error, error_size,
            "deterministic equation source count exceeds the supported bound");
    for (size_t index = 0u; index < count; index++) {
        if (!cetta_langdef_text_arg(
                envelope->expr.elems[index + 1u], &paths[index]))
            return langdef_set_error(
                error, error_size,
                "deterministic equation source path is not text");
    }
    *path_count = count;
    return true;
}

static Atom *deterministic_equation_call(
    Atom *envelope, Arena *arena, char *error, size_t error_size) {
    Atom **elements;
    Atom *call;

    if (!envelope || !arena || envelope->kind != ATOM_EXPR ||
        !envelope->expr.elems || envelope->expr.len < 2u ||
        !atom_is_symbol(
            envelope->expr.elems[0], "LangDef:DeterministicCall") ||
        !envelope->expr.elems[1] ||
        envelope->expr.elems[1]->kind != ATOM_SYMBOL) {
        (void)langdef_set_error(
            error, error_size,
            "deterministic execution expects a symbol-headed LangDef:DeterministicCall envelope");
        return NULL;
    }
    elements = calloc(
        (size_t)envelope->expr.len - 1u, sizeof(*elements));
    if (!elements) {
        (void)langdef_set_error(
            error, error_size,
            "deterministic call allocation failed");
        return NULL;
    }
    for (CettaExprIndex index = 1u;
         index < envelope->expr.len; index++)
        elements[index - 1u] = envelope->expr.elems[index];
    call = atom_expr(arena, elements, envelope->expr.len - 1u);
    free(elements);
    return call;
}

static CettaDeterministicPrimitiveResultV1
langdef_deterministic_equation_primitive(
    void *context, const char *head, Atom *const *arguments,
    uint32_t argument_count, Arena *arena, Atom **out,
    char *error, size_t error_size) {
    (void)context;
    if (!head || !arena || !out)
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
    if (strcmp(head, "+") == 0) {
        int64_t left;
        int64_t right;
        if (argument_count != 2u || !arguments || !arguments[0] ||
            !arguments[1] || arguments[0]->kind != ATOM_GROUNDED ||
            arguments[1]->kind != ATOM_GROUNDED ||
            arguments[0]->ground.gkind != GV_INT ||
            arguments[1]->ground.gkind != GV_INT) {
            (void)langdef_set_error(
                error, error_size,
                "deterministic integer addition expects two integers");
            return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
        }
        left = arguments[0]->ground.ival;
        right = arguments[1]->ground.ival;
        if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right)) {
            (void)langdef_set_error(
                error, error_size,
                "deterministic integer addition overflowed");
            return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
        }
        *out = atom_int(arena, left + right);
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED;
    }
    if (strcmp(head, "langdef:codepoints->string") == 0) {
        uint8_t *bytes = NULL;
        size_t len = 0u;
        size_t cap = 0u;
        if (argument_count != 1u || !arguments || !arguments[0] ||
            !langdef_codepoints_collect(
                arguments[0], &bytes, &len, &cap, 0u)) {
            free(bytes);
            (void)langdef_set_error(
                error, error_size,
                "deterministic codepoint decoding rejected its source tree");
            return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
        }
        *out = atom_string(arena, bytes ? (const char *)bytes : "");
        free(bytes);
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED;
    }
    return CETTA_DETERMINISTIC_PRIMITIVE_V1_NOT_HANDLED;
}

static Atom *langdef_publish_values(CettaLangDefV1 *resource,
                                    Space *space, Arena *arena,
                                    Atom *source, Atom **values,
                                    uint32_t value_len) {
    Space *work;
    uint32_t index;

    if (resource->result_language_ready) {
        for (index = 0u; index < value_len; index++) {
            CettaLdGroundTermV1Status admission_status =
                CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
            char admission_error[256] = {0};
            if (!cetta_language_def_ground_term_v1_admit(
                    &resource->result_language,
                    resource->result_category,
                    values[index], UINT32_MAX,
                    UINT64_C(100000000), &admission_status,
                    admission_error, sizeof(admission_error))) {
                char message[512];
                (void)snprintf(
                    message, sizeof(message),
                    "generated result does not inhabit its supplied LanguageDef (%s): %s",
                    cetta_ld_ground_term_v1_status_name(admission_status),
                    admission_error[0] ? admission_error :
                        "ground-term admission rejected the value");
                return langdef_error(arena, source, message);
            }
        }
    }
    work = space_heap_clone_shallow(space);
    if (!work)
        return langdef_error(
            arena, source,
            "langdef import transaction allocation failed");
    for (index = 0u; index < value_len; index++) {
        if (!space_admit_atom(work, arena, values[index])) {
            space_free(work);
            free(work);
            return langdef_error(
                arena, source, "langdef import transaction failed");
        }
    }
    space_replace_contents(space, work);
    space_free(work);
    free(work);
    {
        Atom *arguments[3] = {
            atom_symbol(arena, resource->name),
            atom_string(arena, resource->pack.pack_digest),
            atom_int(arena, value_len),
        };
        return langdef_return(
            arena, langdef_expr(arena, "LangDef:Imported",
                                arguments, 3u));
    }
}

static Atom *langdef_admit_transform_source(CettaLangDefV1 *resource,
                                            Arena *arena,
                                            Atom *source,
                                            Atom *transform_source) {
    CettaLdGroundTermV1Status admission_status =
        CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    char admission_error[256] = {0};

    if (!resource || !arena || !source)
        return NULL;
    if (!resource->source_language_ready)
        return transform_source == NULL
            ? atom_bool(arena, true)
            : langdef_error(
                  arena, source,
                  "generated import protocol supplied an undeclared source term");
    if (!transform_source ||
        !cetta_language_def_ground_term_v1_admit(
            &resource->source_language,
            resource->source_category,
            transform_source, UINT32_MAX,
            UINT64_C(100000000), &admission_status,
            admission_error, sizeof(admission_error))) {
        char message[512];
        (void)snprintf(
            message, sizeof(message),
            "generated transform source does not inhabit its supplied LanguageDef (%s): %s",
            cetta_ld_ground_term_v1_status_name(admission_status),
            admission_error[0] ? admission_error :
                "ground-term admission rejected the transform source");
        return langdef_error(arena, source, message);
    }
    return atom_bool(arena, true);
}

Atom *cetta_langdef_module_dispatch(CettaLibraryContext *ctx,
                                    Space *space, Arena *arena,
                                    Atom *head, Atom **args,
                                    uint32_t nargs) {
    char error[512] = {0};

    if (atom_is_symbol(head, "__cetta_lib_authored_parser_load")) {
        const char *language_path;
        const char *profile_path;
        CettaAuthoredParserV1 *resource;
        uint64_t id;

        if (nargs != 2u ||
            !cetta_langdef_text_arg(args[0], &language_path) ||
            !cetta_langdef_text_arg(args[1], &profile_path)) {
            return langdef_error(
                arena, head,
                "langdef:load-authored-parser expects LanguageDef and parser-profile paths");
        }
        resource = authored_parser_resource_load(
            language_path, profile_path, error, sizeof(error));
        if (!resource) {
            return langdef_error(
                arena, head,
                error[0] ? error : "authored parser load failed");
        }
        if (!cetta_native_handle_alloc(
                ctx, AUTHORED_PARSER_HANDLE_KIND, resource,
                authored_parser_resource_free, &id)) {
            authored_parser_resource_free(resource);
            return langdef_error(
                arena, head, "authored parser handle allocation failed");
        }
        return langdef_return(
            arena, cetta_native_handle_atom(
                arena, AUTHORED_PARSER_HANDLE_KIND, id));
    }

    if (atom_is_symbol(head, "__cetta_lib_authored_parser_close")) {
        uint64_t id;

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], AUTHORED_PARSER_HANDLE_KIND, &id)) {
            return langdef_error(
                arena, head,
                "langdef:close-authored-parser expects an authored parser handle");
        }
        return langdef_return(
            arena, atom_bool(
                arena, cetta_native_handle_close(
                    ctx, AUTHORED_PARSER_HANDLE_KIND, id)));
    }

    if (atom_is_symbol(head, "__cetta_lib_authored_parser_info")) {
        uint64_t id;
        CettaAuthoredParserV1 *resource;
        Atom *arguments[7];

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], AUTHORED_PARSER_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                ctx, AUTHORED_PARSER_HANDLE_KIND, id))) {
            return langdef_error(
                arena, head,
                "langdef:authored-parser-info expects a live authored parser handle");
        }
        arguments[0] = atom_symbol(arena, resource->name);
        arguments[1] = atom_string(
            arena, resource->compiled.language_source_sha256);
        arguments[2] = atom_string(
            arena, resource->compiled.profile_source_sha256);
        arguments[3] = atom_string(
            arena, resource->compiled.binding_sha256);
        arguments[4] = atom_string(
            arena, resource->compiled.pack.pack_digest);
        arguments[5] = atom_int(
            arena, resource->compiled.authored_rule_len);
        arguments[6] = atom_int(
            arena, resource->compiled.lexical_rule_len);
        return langdef_return(
            arena, langdef_expr(
                arena, "LangDef:AuthoredParserInfo", arguments, 7u));
    }

    if (atom_is_symbol(head, "__cetta_lib_authored_parser_parse_text")) {
        uint64_t id;
        CettaAuthoredParserV1 *resource;
        const char *source_text;
        Atom *parsed;

        if (nargs != 2u ||
            !cetta_native_handle_arg(
                args[0], AUTHORED_PARSER_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                ctx, AUTHORED_PARSER_HANDLE_KIND, id)) ||
            !cetta_langdef_text_arg(args[1], &source_text)) {
            return langdef_error(
                arena, head,
                "langdef:parse-authored-text expects a live authored parser handle and source text");
        }
        parsed = authored_parser_parse_bytes(
            resource, (const uint8_t *)source_text,
            strlen(source_text), arena, head);
        return parsed && !atom_is_error(parsed)
            ? langdef_return(arena, parsed)
            : parsed;
    }

    if (atom_is_symbol(head, "__cetta_lib_authored_parser_parse_file")) {
        uint64_t id;
        CettaAuthoredParserV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom *parsed;

        if (nargs != 2u ||
            !cetta_native_handle_arg(
                args[0], AUTHORED_PARSER_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                ctx, AUTHORED_PARSER_HANDLE_KIND, id)) ||
            !cetta_langdef_text_arg(args[1], &source_path)) {
            return langdef_error(
                arena, head,
                "langdef:parse-authored-file expects a live authored parser handle and source path");
        }
        if (!cetta_langdef_slurp(
                source_path, &bytes, &len, error, sizeof(error))) {
            return langdef_error(
                arena, head,
                error[0] ? error : "cannot read authored parser source");
        }
        parsed = authored_parser_parse_bytes(
            resource, bytes, len, arena, head);
        free(bytes);
        return parsed && !atom_is_error(parsed)
            ? langdef_return(arena, parsed)
            : parsed;
    }

    if (atom_is_symbol(head, "__cetta_lib_deterministic_equations_load")) {
        const char *paths[CETTA_LANGDEF_MAX_EXTENSION_SOURCES];
        size_t path_count = 0u;
        CettaDeterministicEquationPlanV1 *resource = NULL;
        CettaDeterministicEquationStatusV1 status =
            CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
        uint64_t id;

        if (nargs != 1u ||
            !deterministic_equation_sources(
                args[0], paths, &path_count, error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error :
                    "langdef:load-deterministic-equations expects source paths");
        if (!cetta_deterministic_equation_plan_v1_load(
                paths, path_count, &resource, &status,
                error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error :
                    cetta_deterministic_equation_status_name_v1(status));
        if (!cetta_native_handle_alloc(
                ctx, DETERMINISTIC_EQUATION_HANDLE_KIND, resource,
                deterministic_equation_resource_free, &id)) {
            cetta_deterministic_equation_plan_v1_free(resource);
            return langdef_error(
                arena, head,
                "deterministic equation handle allocation failed");
        }
        return langdef_return(
            arena, cetta_native_handle_atom(
                arena, DETERMINISTIC_EQUATION_HANDLE_KIND, id));
    }

    if (atom_is_symbol(head, "__cetta_lib_deterministic_equations_run")) {
        uint64_t id;
        CettaDeterministicEquationPlanV1 *resource;
        CettaDeterministicEquationStatusV1 status =
            CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
        Atom *call;
        Atom *result = NULL;

        if (nargs != 2u ||
            !cetta_native_handle_arg(
                args[0], DETERMINISTIC_EQUATION_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, DETERMINISTIC_EQUATION_HANDLE_KIND, id)))
            return langdef_error(
                arena, head,
                "langdef:run-deterministic-equations expects a live equation handle and call envelope");
        call = deterministic_equation_call(
            args[1], arena, error, sizeof(error));
        if (!call)
            return langdef_error(
                arena, head,
                error[0] ? error : "invalid deterministic call envelope");
        if (!cetta_deterministic_equation_plan_v1_run(
                resource, call, langdef_deterministic_equation_primitive,
                NULL, arena, LANGDEF_DEFAULT_EQUATION_CONTINUATIONS,
                UINT64_C(100000000),
                &result, &status, error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error :
                    cetta_deterministic_equation_status_name_v1(status));
        return langdef_return(arena, result);
    }

    if (atom_is_symbol(head, "__cetta_lib_deterministic_equations_close")) {
        uint64_t id;

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], DETERMINISTIC_EQUATION_HANDLE_KIND, &id))
            return langdef_error(
                arena, head,
                "langdef:close-deterministic-equations expects an equation handle");
        return langdef_return(
            arena, atom_bool(
                arena, cetta_native_handle_close(
                    ctx, DETERMINISTIC_EQUATION_HANDLE_KIND, id)));
    }

    if (atom_is_symbol(head, "__cetta_lib_structural_tree_relabel_load") ||
        atom_is_symbol(
            head,
            "__cetta_lib_structural_tree_relabel_load_composition")) {
        const char *path;
        const char *entry_operator;
        const char *label_operator;
        bool composition = atom_is_symbol(
            head,
            "__cetta_lib_structural_tree_relabel_load_composition");
        CettaStructuralTreeRelabelV1 *resource = NULL;
        CettaStructuralTreeRelabelV1Status status =
            CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
        uint64_t id;

        if (nargs != 3u ||
            !cetta_langdef_text_arg(args[0], &path) ||
            !cetta_langdef_text_arg(args[1], &entry_operator) ||
            !cetta_langdef_text_arg(args[2], &label_operator))
            return langdef_error(
                arena, head,
                composition ?
                    "langdef:load-structural-tree-relabel-composition expects a composition path, entry operator, and label operator" :
                    "langdef:load-structural-tree-relabel expects a presentation path, entry operator, and label operator");
        if (!(composition ?
              structural_tree_relabel_resource_load_composition(
                  path, entry_operator, label_operator, &resource,
                  &status, error, sizeof(error)) :
              cetta_structural_tree_relabel_v1_load(
                  path, entry_operator, label_operator, &resource,
                  &status, error, sizeof(error))))
            return langdef_error(
                arena, head,
                error[0] ? error :
                    cetta_structural_tree_relabel_v1_status_name(status));
        if (!cetta_native_handle_alloc(
                ctx, STRUCTURAL_TREE_RELABEL_HANDLE_KIND, resource,
                structural_tree_relabel_resource_free,
                &id)) {
            cetta_structural_tree_relabel_v1_free(resource);
            return langdef_error(
                arena, head,
                "structural tree relabel handle allocation failed");
        }
        return langdef_return(
            arena, cetta_native_handle_atom(
                arena, STRUCTURAL_TREE_RELABEL_HANDLE_KIND, id));
    }

    if (atom_is_symbol(head, "__cetta_lib_structural_tree_relabel_apply")) {
        uint64_t id;
        CettaStructuralTreeRelabelV1 *resource;
        CettaStructuralTreeRelabelV1Status status =
            CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
        Atom *result = NULL;

        if (nargs != 2u ||
            !cetta_native_handle_arg(
                args[0], STRUCTURAL_TREE_RELABEL_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, STRUCTURAL_TREE_RELABEL_HANDLE_KIND, id)))
            return langdef_error(
                arena, head,
                "langdef:structural-tree-relabel expects a live relabeler handle and a ground source term");
        if (!cetta_structural_tree_relabel_v1_apply(
                resource, args[1], arena, UINT32_C(65536),
                UINT64_C(100000000), &result, &status,
                error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error :
                    cetta_structural_tree_relabel_v1_status_name(status));
        return langdef_return(arena, result);
    }

    if (atom_is_symbol(head, "__cetta_lib_structural_tree_relabel_close")) {
        uint64_t id;

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], STRUCTURAL_TREE_RELABEL_HANDLE_KIND, &id))
            return langdef_error(
                arena, head,
                "langdef:close-structural-tree-relabel expects a relabeler handle");
        return langdef_return(
            arena, atom_bool(
                arena, cetta_native_handle_close(
                    ctx, STRUCTURAL_TREE_RELABEL_HANDLE_KIND, id)));
    }

    if (atom_is_symbol(head, "__cetta_lib_language_def_term_load")) {
        const char *path;
        CettaLanguageDefTermV1 *resource;
        uint64_t id;

        if (nargs != 1u || !cetta_langdef_text_arg(args[0], &path))
            return langdef_error(
                arena, head,
                "langdef:load-language expects one LanguageDef wire path");
        resource = language_def_term_resource_load(
            path, error, sizeof(error));
        if (!resource)
            return langdef_error(
                arena, head,
                error[0] ? error : "LanguageDef wire load failed");
        if (!cetta_native_handle_alloc(
                ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, resource,
                language_def_term_resource_free, &id)) {
            language_def_term_resource_free(resource);
            return langdef_error(
                arena, head, "LanguageDef handle allocation failed");
        }
        return langdef_return(
            arena, cetta_native_handle_atom(
                arena, LANGUAGE_DEF_TERM_HANDLE_KIND, id));
    }

    if (atom_is_symbol(head, "__cetta_lib_language_def_term_close")) {
        uint64_t id;

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], LANGUAGE_DEF_TERM_HANDLE_KIND, &id))
            return langdef_error(
                arena, head,
                "langdef:close-language expects a LanguageDef handle");
        return langdef_return(
            arena, atom_bool(
                arena, cetta_native_handle_close(
                    ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, id)));
    }

    if (atom_is_symbol(
            head, "__cetta_lib_language_def_term_wire_sha256")) {
        uint64_t id;
        CettaLanguageDefTermV1 *resource;
        Atom *arguments[1];

        if (nargs != 1u ||
            !cetta_native_handle_arg(
                args[0], LANGUAGE_DEF_TERM_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, id)))
            return langdef_error(
                arena, head,
                "langdef:language-wire-sha256 expects a live LanguageDef handle");
        arguments[0] = atom_string(
            arena, resource->wire.source_sha256);
        return langdef_return(
            arena, langdef_expr(
                arena, "LangDef:LanguageWireSHA256", arguments, 1u));
    }

    if (atom_is_symbol(head, "__cetta_lib_language_def_term_admit")) {
        uint64_t id;
        CettaLanguageDefTermV1 *resource;
        const char *category;
        CettaLdGroundTermV1Status status =
            CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
        char admission_error[256] = {0};

        if (nargs != 3u ||
            !cetta_native_handle_arg(
                args[0], LANGUAGE_DEF_TERM_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, id)) ||
            !cetta_langdef_text_arg(args[1], &category))
            return langdef_error(
                arena, head,
                "langdef:admit-term expects a live LanguageDef handle, category, and term");
        if (cetta_language_def_ground_term_v1_admit(
                &resource->language, category, args[2],
                UINT32_MAX,
                LANGDEF_DEFAULT_PROOF_RULE_ATTEMPTS,
                &status, admission_error, sizeof(admission_error))) {
            return langdef_return(
                arena, langdef_expr(
                    arena, "LangDef:TermAdmitted", NULL, 0u));
        }
        {
            Atom *arguments[2] = {
                atom_symbol(
                    arena, cetta_ld_ground_term_v1_status_name(status)),
                atom_string(
                    arena, admission_error[0] ? admission_error :
                        "ground-term admission rejected the value"),
            };
            return langdef_return(
                arena, langdef_expr(
                    arena, "LangDef:TermRejected", arguments, 2u));
        }
    }

    if (atom_is_symbol(head, "__cetta_lib_language_def_rewrite_pattern_at")) {
        uint64_t id;
        CettaLanguageDefTermV1 *resource;
        CettaLanguageDefRelationEnvV1 relation_environment = {0};
        CettaLdCrV1RelationProvider provider;
        CettaLdPatternV1 source_pattern;
        CettaLdCrV1Results results;
        CettaLdPatternAtomV1Status pattern_status =
            CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT;
        CettaLdCrV1Status runner_status = CETTA_LD_CR_V1_BAD_ARGUMENT;
        int64_t fuel;
        int64_t work_limit;
        Atom *encoded;

        cetta_ld_pattern_v1_init(&source_pattern);
        cetta_ld_cr_v1_results_init(&results);
        if (nargs != 5u ||
            !cetta_native_handle_arg(
                args[0], LANGUAGE_DEF_TERM_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, id)) ||
            !args[1] || args[1]->kind != ATOM_GROUNDED ||
            args[1]->ground.gkind != GV_INT ||
            !args[2] || args[2]->kind != ATOM_GROUNDED ||
            args[2]->ground.gkind != GV_INT)
            return langdef_error(
                arena, head,
                "langdef:rewrite-pattern-at expects a live LanguageDef handle, nonnegative contextual fuel, positive work limit, relation rows, and a Pattern wire");
        fuel = args[1]->ground.ival;
        work_limit = args[2]->ground.ival;
        if (fuel < 0 || fuel > UINT32_MAX || work_limit <= 0) {
            return langdef_error(
                arena, head,
                "langdef:rewrite-pattern-at received an invalid resource bound");
        }
        if (!resource->contextual_ready) {
            return langdef_context_fault(
                arena,
                cetta_ld_cr_v1_status_name(resource->contextual_status),
                resource->contextual_error[0]
                    ? resource->contextual_error
                    : "LanguageDef is outside the contextual runner profile");
        }
        if (!langdef_context_relation_env_decode(
                args[3], &relation_environment, error, sizeof(error))) {
            return langdef_context_fault(
                arena, "ContextualRelationProviderMalformed",
                error[0] ? error :
                    "contextual relation provider decode failed");
        }
        if (!cetta_ld_pattern_atom_v1_decode(
                &source_pattern, args[4], LANGDEF_DEFAULT_REPLAY_DEPTH,
                (uint64_t)work_limit, &pattern_status,
                error, sizeof(error))) {
            language_def_relation_env_free(&relation_environment);
            return langdef_context_fault(
                arena, cetta_ld_pattern_atom_v1_status_name(pattern_status),
                error[0] ? error : "Pattern wire decode failed");
        }
        provider.context = &relation_environment;
        provider.query = langdef_context_relation_query;
        if (!cetta_ld_cr_v1_reducts(
                &resource->contextual_program, &provider,
                (uint32_t)fuel, (uint64_t)work_limit,
                &source_pattern, &results, &runner_status,
                error, sizeof(error))) {
            cetta_ld_pattern_v1_free(&source_pattern);
            language_def_relation_env_free(&relation_environment);
            cetta_ld_cr_v1_results_free(&results);
            return langdef_context_fault(
                arena, cetta_ld_cr_v1_status_name(runner_status),
                error[0] ? error : "contextual execution failed");
        }
        encoded = langdef_context_results_atom(
            arena, &resource->language, &results, error, sizeof(error));
        cetta_ld_pattern_v1_free(&source_pattern);
        language_def_relation_env_free(&relation_environment);
        cetta_ld_cr_v1_results_free(&results);
        if (!encoded)
            return langdef_context_fault(
                arena, "ContextualResultEncodingFailure",
                error[0] ? error : "contextual result encoding failed");
        return langdef_return(arena, encoded);
    }

    if (atom_is_symbol(head, "__cetta_lib_language_def_rewrite_term_at")) {
        uint64_t id;
        CettaLanguageDefTermV1 *resource;
        CettaLanguageDefRelationEnvV1 relation_environment = {0};
        CettaLdCrV1RelationProvider provider;
        CettaLdPatternV1 source_pattern;
        CettaLdCrV1Results results;
        CettaLdGroundTermV1Status codec_status =
            CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
        CettaLdCrV1Status runner_status = CETTA_LD_CR_V1_BAD_ARGUMENT;
        const char *source_category;
        const char *target_category;
        int64_t fuel;
        int64_t work_limit;
        Atom *encoded;
        bool target_decode_failed = false;

        cetta_ld_pattern_v1_init(&source_pattern);
        cetta_ld_cr_v1_results_init(&results);
        if (nargs != 7u ||
            !cetta_native_handle_arg(
                args[0], LANGUAGE_DEF_TERM_HANDLE_KIND, &id) ||
            !(resource = cetta_native_handle_get(
                  ctx, LANGUAGE_DEF_TERM_HANDLE_KIND, id)) ||
            !cetta_langdef_text_arg(args[1], &source_category) ||
            !cetta_langdef_text_arg(args[2], &target_category) ||
            !args[3] || args[3]->kind != ATOM_GROUNDED ||
            args[3]->ground.gkind != GV_INT ||
            !args[4] || args[4]->kind != ATOM_GROUNDED ||
            args[4]->ground.gkind != GV_INT)
            return langdef_error(
                arena, head,
                "langdef:rewrite-term-at expects a live LanguageDef handle, source and target categories, nonnegative contextual fuel, positive work limit, relation rows, and a ground term");
        fuel = args[3]->ground.ival;
        work_limit = args[4]->ground.ival;
        if (fuel < 0 || fuel > UINT32_MAX || work_limit <= 0) {
            return langdef_error(
                arena, head,
                "langdef:rewrite-term-at received an invalid resource bound");
        }
        if (!resource->contextual_ready) {
            return langdef_context_fault(
                arena,
                cetta_ld_cr_v1_status_name(resource->contextual_status),
                resource->contextual_error[0]
                    ? resource->contextual_error
                    : "LanguageDef is outside the contextual runner profile");
        }
        if (!cetta_language_def_ground_term_v1_supports_pattern_codec(
                &resource->language, target_category,
                (uint64_t)work_limit, &codec_status,
                error, sizeof(error))) {
            return langdef_context_codec_fault(
                arena, "LangDef:TargetTerm", codec_status,
                error[0] ? error :
                    "target category is outside the typed Pattern codec");
        }
        if (!cetta_language_def_ground_term_v1_to_pattern(
                &resource->language, source_category, args[6],
                &source_pattern, LANGDEF_DEFAULT_REPLAY_DEPTH,
                (uint64_t)work_limit, &codec_status,
                error, sizeof(error))) {
            return langdef_context_codec_fault(
                arena, "LangDef:SourceTerm", codec_status,
                error[0] ? error :
                    "source term was rejected by the typed Pattern codec");
        }
        if (!langdef_context_relation_env_decode(
                args[5], &relation_environment, error, sizeof(error))) {
            cetta_ld_pattern_v1_free(&source_pattern);
            return langdef_context_fault(
                arena, "ContextualRelationProviderMalformed",
                error[0] ? error :
                    "contextual relation provider decode failed");
        }
        provider.context = &relation_environment;
        provider.query = langdef_context_relation_query;
        if (!cetta_ld_cr_v1_reducts(
                &resource->contextual_program, &provider,
                (uint32_t)fuel, (uint64_t)work_limit,
                &source_pattern, &results, &runner_status,
                error, sizeof(error))) {
            cetta_ld_pattern_v1_free(&source_pattern);
            language_def_relation_env_free(&relation_environment);
            cetta_ld_cr_v1_results_free(&results);
            return langdef_context_fault(
                arena, cetta_ld_cr_v1_status_name(runner_status),
                error[0] ? error : "contextual execution failed");
        }
        encoded = langdef_context_term_results_atom(
            arena, &resource->language, target_category,
            &results, (uint64_t)work_limit,
            &codec_status, &target_decode_failed,
            error, sizeof(error));
        cetta_ld_pattern_v1_free(&source_pattern);
        language_def_relation_env_free(&relation_environment);
        cetta_ld_cr_v1_results_free(&results);
        if (!encoded) {
            if (target_decode_failed)
                return langdef_context_codec_fault(
                    arena, "LangDef:TargetTerm", codec_status,
                    error[0] ? error :
                        "contextual result left its declared target category");
            return langdef_context_fault(
                arena, "ContextualResultEncodingFailure",
                error[0] ? error :
                    "contextual term result encoding failed");
        }
        return langdef_return(arena, encoded);
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_load")) {
        const char *manifest_path;
        CettaLangDefV1 *resource;
        uint64_t id;
        if (nargs != 1u || !cetta_langdef_text_arg(args[0], &manifest_path))
            return langdef_error(arena, head,
                                 "langdef:load expects one manifest path");
        resource = langdef_load_resource(manifest_path, error, sizeof(error));
        if (!resource)
            return langdef_error(arena, head,
                                 error[0] ? error : "langdef load failed");
        if (!cetta_native_handle_alloc(ctx, LANGDEF_HANDLE_KIND, resource,
                                       langdef_resource_free, &id)) {
            langdef_resource_free(resource);
            return langdef_error(arena, head,
                                 "langdef handle allocation failed");
        }
        if (!langdef_activate_program(resource, space, arena,
                                      error, sizeof(error))) {
            (void)cetta_native_handle_close(
                ctx, LANGDEF_HANDLE_KIND, id);
            return langdef_error(
                arena, head,
                error[0] ? error : "langdef program activation failed");
        }
        return langdef_return(
            arena, cetta_native_handle_atom(arena, LANGDEF_HANDLE_KIND, id));
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_close")) {
        uint64_t id;
        if (nargs != 1u ||
            !cetta_native_handle_arg(args[0], LANGDEF_HANDLE_KIND, &id))
            return langdef_error(arena, head,
                                 "langdef:close expects a langdef handle");
        return langdef_return(
            arena, atom_bool(arena, cetta_native_handle_close(
                ctx, LANGDEF_HANDLE_KIND, id)));
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_info")) {
        CettaLangDefV1 *resource;
        Atom *arguments[6];
        if (nargs != 1u ||
            !(resource = langdef_handle_resource(ctx, args[0])))
            return langdef_error(arena, head,
                                 "langdef:info expects a live langdef handle");
        arguments[0] = atom_symbol(arena, resource->name);
        arguments[1] = atom_string(arena, resource->pack.source_digest);
        arguments[2] = atom_string(arena, resource->pack.compiler_digest);
        arguments[3] = atom_string(arena, resource->pack.environment_digest);
        arguments[4] = atom_string(arena, resource->pack.pack_digest);
        arguments[5] = atom_string(arena, resource->manifest_sha256);
        return langdef_return(
            arena, langdef_expr(arena, "LangDef:Info", arguments, 6u));
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_parse_text")) {
        CettaLangDefV1 *resource;
        const char *source_text;
        Atom *parsed;
        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_text))
            return langdef_error(
                arena, head,
                "langdef:parse-text expects a live handle and source text");
        parsed = langdef_parse_bytes(resource,
                                     (const uint8_t *)source_text,
                                     strlen(source_text), NULL, arena, head,
                                     false,
                                     CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
                                     true,
                                     NULL, NULL);
        return parsed && !atom_is_error(parsed)
            ? langdef_return(arena, parsed)
            : parsed;
    }

    if (atom_is_symbol(
            head, "__cetta_lib_langdef_parse_occurrence_stream_file")) {
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom *parsed;
        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_path)) {
            return langdef_error(
                arena, head,
                "langdef:parse-occurrence-stream-file expects a live handle and source path");
        }
        if (!cetta_langdef_slurp(
                source_path, &bytes, &len, error, sizeof(error))) {
            return langdef_error(
                arena, head,
                error[0] ? error : "cannot read source file");
        }
        parsed = langdef_parse_runtime_occurrence_bytes(
            resource, bytes, len, source_path, arena, head);
        free(bytes);
        return parsed && !atom_is_error(parsed)
            ? langdef_return(arena, parsed)
            : parsed;
#else
        return langdef_error(
            arena, head,
            "runtime occurrence parsing is absent from this build");
#endif
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_recognize_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom *recognized;

        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_path))
            return langdef_error(
                arena, head,
                "langdef:recognize-file expects a live handle and source path");
        if (!cetta_langdef_slurp(source_path, &bytes, &len,
                                 error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error : "cannot read source file");
        recognized = langdef_parse_bytes(
            resource, bytes, len, source_path, arena, head, false,
            CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY, false,
            NULL, NULL);
        free(bytes);
        return recognized && !atom_is_error(recognized)
            ? langdef_return(arena, recognized)
            : recognized;
    }

    if (atom_is_symbol(
            head,
            "__cetta_lib_langdef_recognize_delimited_sequence_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        int64_t delimiter;
        int64_t candidate_limit;
        int64_t buffer_limit;
        Atom *recognized;

        if (nargs != 6u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !args[2] || args[2]->kind != ATOM_GROUNDED ||
            args[2]->ground.gkind != GV_INT ||
            !args[3] || args[3]->kind != ATOM_GROUNDED ||
            args[3]->ground.gkind != GV_INT ||
            !args[4] || args[4]->kind != ATOM_GROUNDED ||
            args[4]->ground.gkind != GV_INT ||
            !cetta_langdef_text_arg(args[5], &source_path)) {
            return langdef_error(
                arena, head,
                "langdef:recognize-delimited-sequence-file expects a live handle, record state, byte delimiter, candidate limit, buffer limit, and source path");
        }
        delimiter = args[2]->ground.ival;
        candidate_limit = args[3]->ground.ival;
        buffer_limit = args[4]->ground.ival;
        if (delimiter < 0 || delimiter > UINT8_MAX ||
            candidate_limit <= 0 || candidate_limit > UINT32_MAX ||
            buffer_limit <= 0 || (uint64_t)buffer_limit > SIZE_MAX) {
            return langdef_error(
                arena, head,
                "delimited sequence recognition limits are out of range");
        }
        recognized = langdef_recognize_delimited_sequence_file(
            resource, args[1], (uint8_t)delimiter,
            (uint32_t)candidate_limit, (size_t)buffer_limit,
            source_path, arena, head);
        return recognized && !atom_is_error(recognized)
            ? langdef_return(arena, recognized)
            : recognized;
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_prepare_import_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom *parsed;
        char source_digest[65];
        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_path))
            return langdef_error(
                arena, head,
                "langdef import preparation expects a live handle and source path");
        if (!cetta_langdef_slurp(source_path, &bytes, &len,
                                 error, sizeof(error)))
            return langdef_error(arena, head,
                                 error[0] ? error : "cannot read source file");
        cetta_native_sha256_hex(bytes, len, source_digest);
        parsed = langdef_parse_bytes(
                                     resource, bytes, len, source_path,
                                     arena, head,
                                     true,
                                     CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
                                     true,
                                     NULL, NULL);
        free(bytes);
        if (parsed && !atom_is_error(parsed) &&
            cetta_langdef_expr_head(parsed, "LangDef:ParseAccepted", 3u)) {
            Atom *arguments[4] = {
                parsed->expr.elems[1],
                parsed->expr.elems[2],
                atom_string(arena, source_digest),
                parsed->expr.elems[3],
            };
            parsed = langdef_expr(
                arena, "LangDef:ImportPrepared", arguments, 4u);
        }
        return parsed && !atom_is_error(parsed)
            ? langdef_return(arena, parsed)
            : parsed;
    }

    if (atom_is_symbol(head,
                       "__cetta_lib_langdef_import_identity_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom **values = NULL;
        uint32_t value_len = 0u;
        Atom *parsed;

        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_path))
            return langdef_error(
                arena, head,
                "identity import expects a live handle and source path");
        if (resource->import_entry)
            return langdef_error(
                arena, head,
                "identity import requires an identity import entry");
        if (!cetta_langdef_slurp(source_path, &bytes, &len,
                                 error, sizeof(error)))
            return langdef_error(
                arena, head,
                error[0] ? error : "cannot read source file");
        parsed = langdef_parse_bytes(
            resource, bytes, len, source_path, arena, head, true,
            CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
            true,
            &values, &value_len);
        free(bytes);
        if (!parsed || atom_is_error(parsed))
            return parsed;
        if (!cetta_langdef_expr_head(parsed,
                                     "LangDef:ParseAccepted", 3u))
            return langdef_return(arena, parsed);
        return langdef_publish_values(
            resource, space, arena, head, values, value_len);
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_parse_file") ||
        atom_is_symbol(head, "__cetta_lib_langdef_import_syntax_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom **values = NULL;
        uint32_t value_len = 0u;
        Atom *parsed;
        bool importing = atom_is_symbol(
            head, "__cetta_lib_langdef_import_syntax_file");
        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])) ||
            !cetta_langdef_text_arg(args[1], &source_path))
            return langdef_error(
                arena, head,
                importing
                    ? "langdef:import-file expects a live handle and source path"
                    : "langdef:parse-file expects a live handle and source path");
        if (!cetta_langdef_slurp(source_path, &bytes, &len,
                                 error, sizeof(error)))
            return langdef_error(arena, head,
                                 error[0] ? error : "cannot read source file");
        parsed = langdef_parse_bytes(
                                     resource, bytes, len, source_path,
                                     arena, head,
                                     false,
                                     CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
                                     true,
                                     importing ? &values : NULL,
                                     importing ? &value_len : NULL);
        free(bytes);
        if (!parsed || atom_is_error(parsed) || !importing)
            return parsed && !atom_is_error(parsed)
                ? langdef_return(arena, parsed)
                : parsed;
        if (!cetta_langdef_expr_head(parsed, "LangDef:ParseAccepted", 3u))
            return langdef_return(arena, parsed);
        return langdef_publish_values(
            resource, space, arena, head, values, value_len);
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_import_entry")) {
        CettaLangDefV1 *resource;
        if (nargs != 1u ||
            !(resource = langdef_handle_resource(ctx, args[0])))
            return langdef_error(
                arena, head,
                "langdef:import-entry expects a live langdef handle");
        if (!resource->import_entry)
            return langdef_return(
                arena,
                langdef_expr(arena, "LangDef:ImportIdentity", NULL, 0u));
        {
            Atom *arguments[1] = {
                atom_symbol(arena, resource->import_entry),
            };
            return langdef_return(
                arena,
                langdef_expr(arena, "LangDef:ImportEntry", arguments, 1u));
        }
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_publish")) {
        CettaLangDefV1 *resource;
        Atom *protocol;
        if (nargs != 2u ||
            !(resource = langdef_handle_resource(ctx, args[0])))
            return langdef_error(
                arena, head,
                "langdef:publish expects a live handle and import protocol");
        protocol = args[1];
        if (cetta_langdef_expr_head(protocol,
                                    "LangDef:ImportRejected", 1u)) {
            Atom *arguments[3] = {
                atom_symbol(arena, resource->name),
                atom_string(arena, resource->pack.pack_digest),
                protocol->expr.elems[1],
            };
            return langdef_publish_result(
                arena, langdef_expr(arena, "LangDef:ImportRejected",
                                    arguments, 3u));
        }
        if ((!resource->source_language_ready &&
             !cetta_langdef_expr_head(
                 protocol, "LangDef:ImportReady", 1u)) ||
            (resource->source_language_ready &&
             !cetta_langdef_expr_head(
                 protocol, "LangDef:ImportReady", 2u)))
            return langdef_publish_result(
                arena,
                langdef_error(
                    arena, head,
                    "generated import entry returned an invalid protocol"));
        {
            Atom *transform_source = resource->source_language_ready
                ? protocol->expr.elems[1] : NULL;
            Atom *values = resource->source_language_ready
                ? protocol->expr.elems[2] : protocol->expr.elems[1];
            Atom *source_admission;

            if (!values || values->kind != ATOM_EXPR)
                return langdef_publish_result(
                    arena,
                    langdef_error(
                        arena, head,
                        "generated import protocol has no result list"));
            source_admission = langdef_admit_transform_source(
                resource, arena, head, transform_source);
            if (!source_admission || atom_is_error(source_admission))
                return langdef_publish_result(arena, source_admission);
            return langdef_publish_result(
                arena,
                langdef_publish_values(
                    resource, space, arena, head, values->expr.elems,
                    values->expr.len));
        }
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_codepoints_symbol")) {
        uint8_t *bytes = NULL;
        size_t len = 0u;
        size_t cap = 0u;
        Atom *result;
        if (nargs != 1u ||
            !langdef_codepoints_collect(args[0], &bytes, &len, &cap, 0u) ||
            len == 0u) {
            free(bytes);
            return langdef_error(
                arena, head,
                "langdef:codepoints->symbol expects a nonempty cp sequence");
        }
        result = atom_symbol(arena, (const char *)bytes);
        free(bytes);
        return langdef_return(arena, result);
    }

    if (atom_is_symbol(head, "__cetta_lib_langdef_codepoints_string")) {
        uint8_t *bytes = NULL;
        size_t len = 0u;
        size_t cap = 0u;
        Atom *result;
        if (nargs != 1u ||
            !langdef_codepoints_collect(args[0], &bytes, &len, &cap, 0u)) {
            free(bytes);
            return langdef_error(
                arena, head,
                "langdef:codepoints->string expects a cp sequence");
        }
        result = atom_string(arena, bytes ? (const char *)bytes : "");
        free(bytes);
        return langdef_return(arena, result);
    }

    return NULL;
}
#endif
