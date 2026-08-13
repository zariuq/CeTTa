#define _XOPEN_SOURCE 700

#include "native/langdef_module.h"

#include "native_sha256.h"
#include "parser.h"

#include "parser_pack_abi_stream_v1.h"
#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
#include "library.h"
#include "native_handle.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"
#endif
#if !defined(CETTA_LANGDEF_ARTIFACT_ONLY) && !defined(CETTA_NO_STDLIB)
#define CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME 1
#include "native/langdef_compiled_cursor_v1.h"
#include "parser_occurrence_file_resolver_v1.h"
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
#include "first_order_frame_decoder_v1.h"
#endif
#include "oslf_native_type_plan_v1.h"
#include "oslf_native_type_vm_v1.h"
#include "proof_gslt_plan_v1.h"
#include "proof_gslt_relational_assertion_v1.h"
#include "proof_gslt_relational_machine_v1.h"
#include "proof_gslt_relational_runtime_v1.h"
#include "proof_gslt_sequence_evidence_v1.h"
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
#define LANGDEF_MINIMUM_WORK_LIMIT UINT64_C(4000000)
#define LANGDEF_WORK_PER_SOURCE_BYTE UINT64_C(64)
#define LANGDEF_DEFAULT_REPLAY_DEPTH 4096u
#define LANGDEF_DEFAULT_RESULT_LIMIT 65536u
#define LANGDEF_DEFAULT_INCLUDE_DEPTH 4096u
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
    char *compiled_cursor_path;
    char *proof_machine_native_types_path;
    char *proof_generated_runtime_path;
    char manifest_sha256[65];
    char pack_file_sha256[65];
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
    void *compiled_cursor_handle;
    const CettaLangDefCompiledCursorV1 *compiled_cursor;
    PPGuardedLexCursorV1Program compiled_program;
    PPOccurrenceFoldV1Plan compiled_fold;
    PPOccurrenceSpanMaskV1Plan compiled_span_mask;
    PPRelationalStateProgramV1Plan compiled_state;
    PPProofGSLTPlanV1 proof_plan;
    PPProofGSLTSequenceEvidenceABIV1 proof_evidence;
    PPProofGSLTRelationalAssertionPlanV1 proof_relational;
    PPOSLFNativeTypePlanV1 proof_native_types;
    PPOSLFNativeTypePlanV1 proof_machine_native_types;
    PPOSLFNativeTypeVMV1 proof_machine_vm;
    PPProofGSLTRelationalRuntimeV1 proof_generated_runtime;
    PPProofStoragePlanV1 proof_storage_plan;
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    PPFirstOrderFrameDecoderV1 *proof_frame_decoders;
    PPRelationalStackProofV1CacheAdmission *proof_frame_cache_admissions;
    uint32_t proof_frame_decoder_len;
#endif
    bool compiled_program_ready;
    bool compiled_fold_ready;
    bool compiled_span_mask_ready;
    bool compiled_state_ready;
    bool proof_plan_ready;
    bool proof_evidence_ready;
    bool proof_relational_ready;
    bool proof_native_types_ready;
    bool proof_machine_native_types_ready;
    bool proof_machine_vm_ready;
    bool proof_generated_runtime_ready;
    bool proof_storage_plan_ready;
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    bool proof_frame_decoders_ready;
    bool proof_frame_cache_admissions_ready;
#endif
    bool proof_extension_ready;
#endif
} CettaLangDefV1;

#ifndef CETTA_LANGDEF_ARTIFACT_ONLY
static Atom *langdef_error(Arena *arena, Atom *source, const char *message) {
    return atom_error(arena, source, atom_string(arena, message));
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
        PPProofGSLTRelationalRuntimeV1Profile profile;
        if (profile_requested && profile_requested[0] != '\0' &&
            ppproof_gslt_relational_runtime_v1_profile(
                &resource->proof_generated_runtime, &profile)) {
            fprintf(
                stderr,
                "langdef-oslf-profile-v1 queries=%llu goals=%llu "
                "attempts=%llu matches=%llu continuations=%llu "
                "tail-reuses=%llu collections=%llu goal-roots=%llu "
                "view-goals=%llu view-attempts=%llu "
                "view-matches=%llu view-fallbacks=%llu "
                "binding-collections=%llu binding-roots=%llu "
                "binding-items-discarded=%llu trail-discarded=%llu "
                "copied=%llu reclaimed=%llu materialize=%llu "
                "match-bytes=%llu expand-bytes=%llu nodes=%llu "
                "rollback-reclaimed=%llu dense-nodes=%llu "
                "dense-materializations=%llu dense-reused=%llu "
                "dense-view-nodes=%llu dense-view-resolutions=%llu "
                "dense-view-deferrals=%llu "
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
                profile.stats.maximum_search_frame_depth,
                profile.stats.maximum_goal_depth);
        }
        ppproof_gslt_relational_runtime_v1_free(
            &resource->proof_generated_runtime);
    }
    if (resource->proof_machine_vm_ready)
        pposlf_native_type_vm_v1_free(&resource->proof_machine_vm);
    if (resource->proof_machine_native_types_ready)
        pposlf_native_type_plan_v1_free(
            &resource->proof_machine_native_types);
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    if (resource->proof_frame_cache_admissions_ready)
        free(resource->proof_frame_cache_admissions);
    if (resource->proof_frame_decoders_ready)
        free(resource->proof_frame_decoders);
#endif
    if (resource->proof_storage_plan_ready)
        ppproof_storage_plan_v1_free(&resource->proof_storage_plan);
    if (resource->proof_native_types_ready)
        pposlf_native_type_plan_v1_free(&resource->proof_native_types);
    if (resource->proof_relational_ready)
        ppproof_gslt_relational_assertion_v1_free(
            &resource->proof_relational);
    if (resource->proof_evidence_ready)
        ppproof_gslt_sequence_evidence_abi_v1_free(
            &resource->proof_evidence);
    if (resource->proof_plan_ready)
        ppproof_gslt_plan_v1_free(&resource->proof_plan);
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
         (resource->compiled_cursor->relational_state_digest == NULL)))
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

static bool langdef_proof_extension_load(
    CettaLangDefV1 *resource,
    const CettaLangDefManifestV1 *manifest,
    const char artifact_paths[][PATH_MAX],
    char *error, size_t error_size) {
    PPProofGSLTArticleV1Limits limits;
    int32_t plan_index;
    int32_t evidence_index;
    int32_t relational_index;
    int32_t native_types_index;
    int32_t machine_native_types_index;
    int32_t generated_runtime_index;
    int32_t storage_plan_index;

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

    limits = ppproof_gslt_article_v1_default_limits();
    ppproof_gslt_plan_v1_init(&resource->proof_plan);
    resource->proof_plan_ready = true;
    if (ppproof_gslt_plan_v1_load(
            &resource->proof_plan, artifact_paths[plan_index], &limits,
            error, error_size) != PPPROOF_GSLT_ARTICLE_V1_OK)
        return false;
    ppproof_gslt_sequence_evidence_abi_v1_init(
        &resource->proof_evidence);
    resource->proof_evidence_ready = true;
    if (ppproof_gslt_sequence_evidence_abi_v1_load(
            &resource->proof_evidence, artifact_paths[evidence_index],
            &resource->proof_plan, error, error_size) !=
        PPPROOF_GSLT_ARTICLE_V1_OK)
        return false;
    ppproof_gslt_relational_assertion_v1_init(
        &resource->proof_relational);
    resource->proof_relational_ready = true;
    if (ppproof_gslt_relational_assertion_v1_load(
            &resource->proof_relational,
            artifact_paths[relational_index], &resource->proof_plan,
            &resource->compiled_state, error, error_size) !=
        PPPROOF_GSLT_ARTICLE_V1_OK)
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
    resource->proof_extension_ready = true;
    return true;
}

static bool langdef_generated_proof_prepare(
    CettaLangDefV1 *resource, char *error, size_t error_size) {
    PPOSLFNativeTypePlanV1 native_types;
    PPOSLFNativeTypeVMV1 vm;
    PPProofGSLTRelationalRuntimeV1 runtime;
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
    ppproof_gslt_relational_runtime_v1_init(&runtime);
    runtime_ready = true;
    if (!ppproof_gslt_relational_runtime_v1_prepare(
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
        ppproof_gslt_relational_runtime_v1_free(&runtime);
    if (vm_ready)
        pposlf_native_type_vm_v1_free(&vm);
    if (native_types_ready)
        pposlf_native_type_plan_v1_free(&native_types);
    return ok;
}

#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
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
            "diagnostic frame-cache backend lacks generated analysis");
    machine_len = resource->compiled_state.proof_machine_len;
    if (resource->proof_frame_decoders_ready ||
        resource->proof_frame_cache_admissions_ready) {
        if (!resource->proof_frame_decoders_ready ||
            !resource->proof_frame_cache_admissions_ready ||
            resource->proof_frame_decoder_len != machine_len)
            return langdef_set_error(
                error, error_size,
                "diagnostic frame-cache backend has inconsistent admission");
        return true;
    }
    decoders = calloc(
        machine_len ? machine_len : 1u, sizeof(*decoders));
    admissions = calloc(
        machine_len ? machine_len : 1u, sizeof(*admissions));
    if (!decoders || !admissions) {
        langdef_set_error(
            error, error_size,
            "cannot allocate diagnostic frame-cache admission");
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
    if (!ppabi_v1_wire_read(&resource->wire, pack_path,
                            error, error_size) ||
        !ppabi_v1_wire_load_pack(&resource->wire, &resource->pack,
                                 error, error_size)) {
        goto fail;
    }
    if (!atom_eq(resource->wire.start, manifest.start) ||
        strcmp(resource->pack.source_digest, lock.source_digest) != 0 ||
        strcmp(resource->pack.compiler_digest, lock.compiler_digest) != 0 ||
        strcmp(resource->pack.environment_digest,
               lock.environment_digest) != 0 ||
        strcmp(resource->pack.pack_digest, lock.pack_digest) != 0) {
        langdef_set_error(error, error_size,
                          "langdef lock disagrees with the compiled parser pack");
        goto fail;
    }
#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
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
    resource->compiled_cursor_path = manifest.compiled_cursor_relative
        ? langdef_text_dup(compiled_cursor_path) : NULL;
    if (!resource->name || !resource->manifest_path || !resource->pack_path ||
        (manifest.program_relative && !resource->program_path) ||
        (manifest.import_entry && !resource->import_entry) ||
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

static Atom *langdef_return(Arena *arena, Atom *value) {
    return atom_expr2(arena, atom_symbol(arena, "return"), value);
}

static bool langdef_result_value(Atom *result, Atom **value_out) {
    if (!cetta_langdef_expr_head(result, "result", 2u) ||
        !atom_is_symbol(result->expr.elems[2], "nil"))
        return false;
    *value_out = result->expr.elems[1];
    return true;
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
    PPProofGSLTRelationalMachineV1Receipt receipt;
    PPProofGSLTRelationalMachineV1Result result;
    PPProofGSLTArticleV1Limits limits;

    if (!resource || !resource->proof_extension_ready || !request ||
        !request->store || !request->state_plan) {
        langdef_set_error(error_buf, error_buf_size,
                          "invalid proof-extension execution request");
        return PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
    limits = ppproof_gslt_article_v1_default_limits();
    if (request->compressed) {
        const PPProofIndexedValuePlanV1 *indexed_value_plan;
        const PPProofFrameIndexPlanV1 *frame_index_plan;
        const char *machine;
        PPProofGSLTRelationalCompressedInputV1 input = {
            .label = request->label,
            .claim = request->claim,
            .claim_len = request->claim_len,
            .header = request->proof,
            .header_len = request->proof_len,
            .code = request->code,
            .code_len = request->code_len,
        };
        if (!resource->proof_storage_plan_ready ||
            request->proof_machine_id >=
                request->state_plan->proof_machine_len ||
            !request->state_plan->proof_machines ||
            !request->state_plan->proof_machines[
                request->proof_machine_id].name) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated indexed-value admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        indexed_value_plan = ppproof_storage_plan_v1_indexed_value(
            &resource->proof_storage_plan,
            request->state_plan->proof_machines[
                request->proof_machine_id].name);
        frame_index_plan = ppproof_storage_plan_v1_frame_index(
            &resource->proof_storage_plan,
            request->state_plan->proof_machines[
                request->proof_machine_id].name);
        machine = request->state_plan->proof_machines[
            request->proof_machine_id].name;
        if (!indexed_value_plan) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated indexed-value admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        if (!frame_index_plan) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof lacks generated frame-index admission");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        if (!ppproof_indexed_value_plan_v1_admits(
                indexed_value_plan, request->operation,
                request->action_index, machine,
                request->header_role, request->code_role)) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof indexed-value admission does not match its generated call site");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        if (!ppproof_frame_index_plan_v1_admits(
                frame_index_plan, request->operation,
                request->action_index, machine)) {
            langdef_set_error(
                error_buf, error_buf_size,
                "compressed proof frame-index admission does not match its generated call site");
            return PPRELATIONAL_STATE_PROOF_V1_INVALID;
        }
        result = ppproof_gslt_relational_machine_v1_compressed(
            request->store, request->state_plan,
            request->proof_machine_id, &resource->proof_plan,
            &resource->proof_evidence, &resource->proof_relational,
            indexed_value_plan, frame_index_plan, &input, &limits, &receipt,
            error_buf, error_buf_size);
    } else {
        PPProofGSLTRelationalNormalInputV1 input = {
            .label = request->label,
            .claim = request->claim,
            .claim_len = request->claim_len,
            .steps = request->proof,
            .step_len = request->proof_len,
        };
        result = ppproof_gslt_relational_machine_v1_normal(
            request->store, request->state_plan,
            request->proof_machine_id, &resource->proof_plan,
            &resource->proof_evidence, &resource->proof_relational,
            &input, &limits, &receipt, error_buf, error_buf_size);
    }
    switch (result) {
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK:
        return PPRELATIONAL_STATE_PROOF_V1_VERIFIED;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE:
        return PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED:
        return PPRELATIONAL_STATE_PROOF_V1_REJECTED;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE:
        return PPRELATIONAL_STATE_PROOF_V1_RESOURCE;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED:
        return PPRELATIONAL_STATE_PROOF_V1_UNSUPPORTED;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID:
    default:
        return PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
}

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
            langdef->proof_extension_ready) {
            proof_backend.context = langdef;
            proof_backend.execute = langdef_proof_extension_execute;
        }
        if (proof_execution ==
                CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT &&
            langdef->proof_generated_runtime_ready) {
            proof_backend = ppproof_gslt_relational_runtime_v1_backend(
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
        if (proof_execution ==
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

static Atom *langdef_parse_bytes(CettaLangDefV1 *langdef,
                                 const uint8_t *bytes, size_t len,
                                 const char *source_path,
                                 Arena *arena, Atom *source,
                                 bool execute_state,
                                 CettaLangDefProofExecutionV1 proof_execution,
                                 bool materialize_values,
                                 Atom ***values_out, uint32_t *value_len_out) {
    PPNativeV1Result parsed;
    Atom **values = NULL;
    char error[512] = {0};
    Atom *result = NULL;
    uint32_t index;

#ifdef CETTA_LANGDEF_COMPILED_CURSOR_RUNTIME
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
    if (!ppgll_v1_parse(&langdef->pack, langdef->wire.start,
                        bytes, len, langdef_source_work_limit_u32(len),
                        LANGDEF_DEFAULT_REPLAY_DEPTH,
                        LANGDEF_DEFAULT_RESULT_LIMIT, &parsed,
                        error, sizeof(error))) {
        result = langdef_error(arena, source,
                               error[0] ? error : "langdef parser failed");
        goto done;
    }
    if (parsed.outcome != PPNATIVE_V1_COMPLETED) {
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed.outcome),
            atom_string(arena, parsed.detail),
        };
        result = langdef_expr(arena, "LangDef:ParseIncomplete", arguments, 4u);
        goto done;
    }
    if (!parsed.accepted) {
        Atom *expected = langdef_expected(arena, langdef, &parsed.forest);
        Atom *arguments[4] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed.forest.farthest_byte),
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
    if (!materialize_values) {
        Atom *arguments[3] = {
            atom_symbol(arena, langdef->name),
            atom_string(arena, langdef->pack.pack_digest),
            atom_int(arena, parsed.semantic_result_len),
        };
        result = langdef_expr(
            arena, "LangDef:RunAccepted", arguments, 3u);
        if (value_len_out)
            *value_len_out = parsed.semantic_result_len;
        goto done;
    }
    if (parsed.semantic_result_len > 0u)
        values = arena_alloc(arena,
                             sizeof(*values) * parsed.semantic_result_len);
    for (index = 0u; index < parsed.semantic_result_len; index++) {
        Atom *value;
        if (!langdef_result_value(parsed.semantic_results[index], &value)) {
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
        Atom *value_list = atom_expr(arena, values, parsed.semantic_result_len);
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
        *value_len_out = parsed.semantic_result_len;

done:
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

static Atom *langdef_publish_values(CettaLangDefV1 *resource,
                                    Space *space, Arena *arena,
                                    Atom *source, Atom **values,
                                    uint32_t value_len) {
    Space *work = space_heap_clone_shallow(space);
    uint32_t index;

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

Atom *cetta_langdef_module_dispatch(CettaLibraryContext *ctx,
                                    Space *space, Arena *arena,
                                    Atom *head, Atom **args,
                                    uint32_t nargs) {
    char error[512] = {0};

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

    if (atom_is_symbol(head, "__cetta_lib_langdef_prepare_import_file")) {
        CettaLangDefV1 *resource;
        const char *source_path;
        uint8_t *bytes = NULL;
        size_t len = 0u;
        Atom *parsed;
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
        parsed = langdef_parse_bytes(
                                     resource, bytes, len, source_path,
                                     arena, head,
                                     true,
                                     CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY,
                                     true,
                                     NULL, NULL);
        free(bytes);
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
                arena, atom_symbol(arena, "LangDef:ImportIdentity"));
        return langdef_return(arena,
                              atom_symbol(arena, resource->import_entry));
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
            return langdef_return(
                arena, langdef_expr(arena, "LangDef:ImportRejected",
                                    arguments, 3u));
        }
        if (!cetta_langdef_expr_head(protocol, "LangDef:ImportReady", 1u) ||
            !protocol->expr.elems[1] ||
            protocol->expr.elems[1]->kind != ATOM_EXPR)
            return langdef_error(
                arena, head,
                "generated import entry returned an invalid protocol");
        {
            Atom *values = protocol->expr.elems[1];
            return langdef_publish_values(
                resource, space, arena, head, values->expr.elems,
                values->expr.len);
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

    return NULL;
}
#endif
