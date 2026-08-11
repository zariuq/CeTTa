#include "generated/zero_exp_language_v1.generated.h"
#include "generated/zero_emit_language_v1.generated.h"
#include "generated/zero_interact_language_v1.generated.h"
#include "generated/zero_language_v1.generated.h"
#include "gslt_language_runtime.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    uint8_t *bytes;
    size_t length;
} FileBytes;

static const uint8_t compiler_domain[] =
    "CettaGsltLanguageCompilerV1";
static const uint8_t selected_source_domain[] =
    "CettaGsltSelectedSourceV1";
static const uint8_t admission_domain[] =
    "CettaGsltAdmissionV1";
static const uint8_t artifact_domain[] =
    "CettaGsltEmbeddedArtifactV1";

static bool fail(char *error, size_t error_size, const char *format, ...) {
    if (error && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static void file_bytes_free(FileBytes *file) {
    if (!file)
        return;
    free(file->bytes);
    file->bytes = NULL;
    file->length = 0u;
}

static bool read_file(const char *path, FileBytes *file,
                      char *error, size_t error_size) {
    memset(file, 0, sizeof(*file));
    FILE *stream = fopen(path, "rb");
    if (!stream)
        return fail(error, error_size, "cannot open %s: %s",
                    path, strerror(errno));
    bool ok = false;
    if (fseek(stream, 0, SEEK_END) != 0)
        fail(error, error_size, "cannot seek %s", path);
    else {
        long end = ftell(stream);
        if (end < 0)
            fail(error, error_size, "cannot size %s", path);
        else if ((uintmax_t)end > SIZE_MAX)
            fail(error, error_size, "%s exceeds the host size ABI", path);
        else if (fseek(stream, 0, SEEK_SET) != 0)
            fail(error, error_size, "cannot rewind %s", path);
        else {
            file->length = (size_t)end;
            file->bytes = malloc(file->length + 1u);
            if (!file->bytes)
                fail(error, error_size, "cannot allocate %s", path);
            else if (file->length > 0u &&
                     fread(file->bytes, 1u, file->length, stream) !=
                         file->length)
                fail(error, error_size, "cannot read %s", path);
            else {
                file->bytes[file->length] = 0u;
                ok = true;
            }
        }
    }
    if (fclose(stream) != 0 && ok)
        ok = fail(error, error_size, "cannot close %s", path);
    if (!ok)
        file_bytes_free(file);
    return ok;
}

static void sha_update_u64_be(CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    for (uint32_t index = 0u; index < 8u; index++)
        bytes[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void sha_update_blob(CettaNativeSha256 *sha,
                            const uint8_t *bytes, size_t length) {
    sha_update_u64_be(sha, (uint64_t)length);
    if (length > 0u)
        cetta_native_sha256_update(sha, bytes, length);
}

static void sha_update_text(CettaNativeSha256 *sha, const char *text) {
    sha_update_blob(sha, (const uint8_t *)text, strlen(text));
}

static bool atom_expr_named(const Atom *atom, const char *name,
                            CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), name) == 0;
}

static const char *atom_string_value(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING ? atom->ground.sval : NULL;
}

static bool atom_size_value(const Atom *atom, size_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *value = (size_t)atom->ground.ival;
    return true;
}

static bool is_sha256(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++)
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return false;
    return true;
}

static bool string_equal(const char *actual, const char *expected,
                         const char *label,
                         char *error, size_t error_size) {
    if (!actual || strcmp(actual, expected) != 0)
        return fail(error, error_size, "%s differs", label);
    return true;
}

static bool digest_bytes(const uint8_t *bytes, size_t length,
                         const char *expected, const char *label,
                         char *error, size_t error_size) {
    char actual[65];
    cetta_native_sha256_hex(bytes, length, actual);
    if (!is_sha256(expected) || strcmp(actual, expected) != 0)
        return fail(error, error_size, "%s digest differs", label);
    return true;
}

static bool file_matches_embedded(
    const char *path, const uint8_t *embedded, size_t embedded_length,
    const char *expected_digest, const char *label,
    char *error, size_t error_size) {
    FileBytes file;
    if (!read_file(path, &file, error, error_size))
        return false;
    bool matches = file.length == embedded_length &&
        (file.length == 0u || memcmp(file.bytes, embedded, file.length) == 0u) &&
        digest_bytes(file.bytes, file.length, expected_digest, label,
                     error, error_size);
    if (!matches && (!error || error_size == 0u || error[0] == '\0'))
        fail(error, error_size, "%s bytes differ", label);
    file_bytes_free(&file);
    return matches;
}

static bool join_path(const char *directory, const char *relative,
                      char output[PATH_MAX],
                      char *error, size_t error_size) {
    if (!directory || !relative || relative[0] == '/')
        return fail(error, error_size, "certificate path is not relative");
    int written = snprintf(output, PATH_MAX, "%s/%s", directory, relative);
    if (written < 0 || written >= PATH_MAX)
        return fail(error, error_size, "certificate path is too long");
    char *resolved = realpath(output, NULL);
    if (!resolved)
        return fail(error, error_size, "cannot resolve %s: %s",
                    output, strerror(errno));
    if (strlen(resolved) >= PATH_MAX) {
        free(resolved);
        return fail(error, error_size, "resolved certificate path is too long");
    }
    strcpy(output, resolved);
    free(resolved);
    return true;
}

static bool directory_of(const char *path, char output[PATH_MAX],
                         char *error, size_t error_size) {
    size_t length = strlen(path);
    if (length >= PATH_MAX)
        return fail(error, error_size, "manifest path is too long");
    strcpy(output, path);
    char *slash = strrchr(output, '/');
    if (!slash)
        strcpy(output, ".");
    else if (slash == output)
        slash[1] = '\0';
    else
        *slash = '\0';
    return true;
}

static void selected_source_sha(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    const char *profile, char output[65]) {
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, selected_source_domain, sizeof(selected_source_domain));
    sha_update_text(&sha, profile);
    sha_update_text(&sha, descriptor->manifest.input.source);
    sha_update_blob(&sha, descriptor->manifest.input.bytes,
                    descriptor->manifest.input.length);
    uint8_t count[8];
    uint64_t count_value = descriptor->semantic_source_count;
    for (uint32_t index = 0u; index < 8u; index++)
        count[7u - index] = (uint8_t)(count_value >> (index * 8u));
    sha_update_blob(&sha, count, sizeof(count));
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++) {
        const CettaGsltEmbeddedSourceV1 *source =
            &descriptor->semantic_sources[index];
        sha_update_text(&sha, source->input.source);
        sha_update_blob(&sha, source->input.bytes, source->input.length);
    }
    cetta_native_sha256_finish_hex(&sha, output);
}

static void admission_sha(const char *selected_digest,
                          size_t presentation_count, size_t rule_count,
                          char output[65]) {
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, admission_domain, sizeof(admission_domain));
    sha_update_text(&sha, selected_digest);
    uint8_t count[8];
    uint64_t values[] = {presentation_count, rule_count};
    for (uint32_t value_index = 0u; value_index < 2u; value_index++) {
        for (uint32_t index = 0u; index < 8u; index++)
            count[7u - index] =
                (uint8_t)(values[value_index] >> (index * 8u));
        sha_update_blob(&sha, count, sizeof(count));
    }
    cetta_native_sha256_finish_hex(&sha, output);
}

static void artifact_sha(const FileBytes *header, const FileBytes *source,
                         char output[65]) {
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, artifact_domain, sizeof(artifact_domain));
    sha_update_blob(&sha, header->bytes, header->length);
    sha_update_blob(&sha, source->bytes, source->length);
    cetta_native_sha256_finish_hex(&sha, output);
}

static bool compiler_sha(const char *generator_path, const char *schema_path,
                         char output[65], char *error, size_t error_size) {
    FileBytes generator;
    FileBytes schema;
    if (!read_file(generator_path, &generator, error, error_size))
        return false;
    if (!read_file(schema_path, &schema, error, error_size)) {
        file_bytes_free(&generator);
        return false;
    }
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, compiler_domain, sizeof(compiler_domain));
    sha_update_blob(&sha, generator.bytes, generator.length);
    sha_update_blob(&sha, schema.bytes, schema.length);
    cetta_native_sha256_finish_hex(&sha, output);
    file_bytes_free(&schema);
    file_bytes_free(&generator);
    return true;
}

static bool check_stage(const Atom *field, size_t index,
                        const char *name, const char *input,
                        const char *output,
                        char *error, size_t error_size) {
    size_t submitted_index;
    if (!atom_expr_named(field, "stage", 5u) ||
        !atom_size_value(field->expr.elems[1], &submitted_index) ||
        submitted_index != index ||
        !string_equal(atom_string_value(field->expr.elems[2]), name,
                      "certificate stage name", error, error_size) ||
        !string_equal(atom_string_value(field->expr.elems[3]), input,
                      "certificate stage input", error, error_size) ||
        !string_equal(atom_string_value(field->expr.elems[4]), output,
                      "certificate stage output", error, error_size))
        return false;
    return true;
}

static bool check_certificate(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    const char *descriptor_symbol, const char *expected_profile,
    const char *certificate_path, const char *source_root,
    const char *header_path, const char *source_path,
    const char *generator_path, const char *schema_path,
    size_t *rule_count_out, char *error, size_t error_size) {
    FileBytes certificate;
    FileBytes header = {0};
    FileBytes source_artifact = {0};
    Atom **forms = NULL;
    Arena arena;
    bool arena_ready = false;
    CettaGsltLanguage *language = NULL;
    bool accepted = false;

    if (!read_file(certificate_path, &certificate, error, error_size))
        return false;
    if (memchr(certificate.bytes, 0, certificate.length)) {
        fail(error, error_size, "certificate contains a NUL byte");
        goto done;
    }
    arena_init(&arena);
    arena_ready = true;
    int form_count = parse_metta_text(
        (const char *)certificate.bytes, &arena, &forms);
    CettaExprLen expected_root_length =
        (CettaExprLen)(14u + descriptor->semantic_source_count);
    if (form_count != 1 || !forms || !forms[0] ||
        forms[0]->kind != ATOM_EXPR ||
        !atom_expr_named(forms[0], "gslt-compilation-certificate-v1",
                         expected_root_length)) {
        const Atom *submitted_head =
            forms && forms[0] && forms[0]->kind == ATOM_EXPR &&
                    forms[0]->expr.len > 0u
                ? forms[0]->expr.elems[0]
                : NULL;
        fail(error, error_size,
             "certificate has a noncanonical root "
             "(forms=%d kind=%d fields=%u expected=%u head-kind=%d "
             "head=%s)",
             form_count,
             forms && forms[0] ? (int)forms[0]->kind : -1,
             forms && forms[0] && forms[0]->kind == ATOM_EXPR
                 ? (unsigned)forms[0]->expr.len : 0u,
             (unsigned)expected_root_length,
             submitted_head ? (int)submitted_head->kind : -1,
             submitted_head && submitted_head->kind == ATOM_SYMBOL
                 ? atom_name_cstr((Atom *)submitted_head)
                 : "<not-symbol>");
        goto done;
    }
    Atom *root = forms[0];
    CettaExprIndex cursor = 1u;

    Atom *compiler = root->expr.elems[cursor++];
    const char *compiler_name = atom_expr_named(compiler, "compiler", 3u)
        ? atom_string_value(compiler->expr.elems[1]) : NULL;
    const char *submitted_compiler_sha = atom_expr_named(
        compiler, "compiler", 3u)
        ? atom_string_value(compiler->expr.elems[2]) : NULL;
    char actual_compiler_sha[65];
    if (!compiler_sha(generator_path, schema_path, actual_compiler_sha,
                      error, error_size) ||
        !string_equal(compiler_name, "CettaGsltLanguageCompilerV1",
                      "compiler name", error, error_size) ||
        !string_equal(submitted_compiler_sha, actual_compiler_sha,
                      "compiler identity", error, error_size) ||
        !string_equal(descriptor->compiler_sha256, actual_compiler_sha,
                      "embedded compiler identity", error, error_size))
        goto done;

    Atom *descriptor_field = root->expr.elems[cursor++];
    if (!atom_expr_named(descriptor_field, "descriptor", 2u) ||
        !string_equal(atom_string_value(descriptor_field->expr.elems[1]),
                      descriptor_symbol, "descriptor symbol",
                      error, error_size))
        goto done;
    Atom *language_field = root->expr.elems[cursor++];
    if (!atom_expr_named(language_field, "language", 2u) ||
        !string_equal(atom_string_value(language_field->expr.elems[1]),
                      descriptor->name, "language name", error, error_size))
        goto done;
    Atom *profile_field = root->expr.elems[cursor++];
    if (!atom_expr_named(profile_field, "profile", 2u) ||
        !string_equal(atom_string_value(profile_field->expr.elems[1]),
                      expected_profile, "profile", error, error_size) ||
        ((descriptor->profile_name == NULL && strcmp(expected_profile, "base") != 0) ||
         (descriptor->profile_name != NULL &&
          strcmp(descriptor->profile_name, expected_profile) != 0))) {
        fail(error, error_size, "descriptor profile differs");
        goto done;
    }
    Atom *observation_field = root->expr.elems[cursor++];
    if (!atom_expr_named(observation_field, "observation", 2u) ||
        !string_equal(atom_string_value(observation_field->expr.elems[1]),
                      descriptor->observation, "observation contract",
                      error, error_size))
        goto done;

    char manifest_path[PATH_MAX];
    Atom *manifest_field = root->expr.elems[cursor++];
    const char *manifest_name = atom_expr_named(
        manifest_field, "manifest", 4u)
        ? atom_string_value(manifest_field->expr.elems[1]) : NULL;
    size_t manifest_length;
    const char *manifest_digest = atom_expr_named(
        manifest_field, "manifest", 4u)
        ? atom_string_value(manifest_field->expr.elems[3]) : NULL;
    if (!atom_expr_named(manifest_field, "manifest", 4u) ||
        !atom_size_value(manifest_field->expr.elems[2], &manifest_length) ||
        !string_equal(manifest_name, descriptor->manifest.input.source,
                      "manifest source name", error, error_size) ||
        manifest_length != descriptor->manifest.input.length ||
        !string_equal(manifest_digest, descriptor->manifest.sha256,
                      "manifest identity", error, error_size) ||
        !join_path(source_root, manifest_name, manifest_path,
                   error, error_size) ||
        !file_matches_embedded(
            manifest_path, descriptor->manifest.input.bytes,
            descriptor->manifest.input.length, manifest_digest,
            "authored manifest", error, error_size))
        goto done;

    char manifest_directory[PATH_MAX];
    if (!directory_of(manifest_path, manifest_directory, error, error_size))
        goto done;
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++) {
        Atom *source_field = root->expr.elems[cursor++];
        size_t submitted_index;
        size_t submitted_length;
        const char *submitted_name = atom_expr_named(
            source_field, "semantic-source", 5u)
            ? atom_string_value(source_field->expr.elems[2]) : NULL;
        const char *submitted_digest = atom_expr_named(
            source_field, "semantic-source", 5u)
            ? atom_string_value(source_field->expr.elems[4]) : NULL;
        const CettaGsltEmbeddedSourceV1 *embedded =
            &descriptor->semantic_sources[index];
        char semantic_path[PATH_MAX];
        if (!atom_expr_named(source_field, "semantic-source", 5u) ||
            !atom_size_value(source_field->expr.elems[1], &submitted_index) ||
            submitted_index != index ||
            !atom_size_value(source_field->expr.elems[3], &submitted_length) ||
            submitted_length != embedded->input.length ||
            !string_equal(submitted_name, embedded->input.source,
                          "semantic source name", error, error_size) ||
            !string_equal(submitted_digest, embedded->sha256,
                          "semantic source identity", error, error_size) ||
            !join_path(manifest_directory, submitted_name, semantic_path,
                       error, error_size) ||
            !file_matches_embedded(
                semantic_path, embedded->input.bytes, embedded->input.length,
                submitted_digest, "authored semantic source",
                error, error_size))
            goto done;
    }

    if (!cetta_gslt_language_load_embedded_for_realization(
            descriptor, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &language, error, error_size))
        goto done;
    size_t rule_count = cetta_gslt_language_semantic_rule_count(language);
    char selected_digest[65];
    selected_source_sha(descriptor, expected_profile, selected_digest);
    char admitted_digest[65];
    admission_sha(selected_digest, descriptor->semantic_source_count,
                  rule_count, admitted_digest);

    Atom *admission = root->expr.elems[cursor++];
    size_t presentation_count;
    size_t submitted_rule_count;
    const char *submitted_admission_digest = atom_expr_named(
        admission, "admission", 4u)
        ? atom_string_value(admission->expr.elems[3]) : NULL;
    if (!atom_expr_named(admission, "admission", 4u) ||
        !atom_size_value(admission->expr.elems[1], &presentation_count) ||
        !atom_size_value(admission->expr.elems[2], &submitted_rule_count) ||
        presentation_count != descriptor->semantic_source_count ||
        submitted_rule_count != rule_count ||
        !string_equal(submitted_admission_digest, admitted_digest,
                      "admission identity", error, error_size))
        goto done;

    Atom *plan = root->expr.elems[cursor++];
    size_t plan_length;
    const char *plan_format = atom_expr_named(plan, "compiled-plan", 4u)
        ? atom_string_value(plan->expr.elems[1]) : NULL;
    const char *submitted_plan_digest = atom_expr_named(
        plan, "compiled-plan", 4u)
        ? atom_string_value(plan->expr.elems[3]) : NULL;
    if (!atom_expr_named(plan, "compiled-plan", 4u) ||
        !atom_size_value(plan->expr.elems[2], &plan_length) ||
        !string_equal(plan_format, "CGP1", "compiled plan format",
                      error, error_size) ||
        plan_length != descriptor->compiled_plan.length ||
        descriptor->compiled_plan.length < 4u ||
        memcmp(descriptor->compiled_plan.bytes, "CGP1", 4u) != 0 ||
        !string_equal(submitted_plan_digest,
                      descriptor->compiled_plan.sha256,
                      "compiled plan identity", error, error_size) ||
        !digest_bytes(descriptor->compiled_plan.bytes,
                      descriptor->compiled_plan.length,
                      submitted_plan_digest, "compiled plan",
                      error, error_size))
        goto done;

    if (!read_file(header_path, &header, error, error_size) ||
        !read_file(source_path, &source_artifact, error, error_size))
        goto done;
    char actual_header_digest[65];
    char actual_source_digest[65];
    char actual_artifact_digest[65];
    cetta_native_sha256_hex(
        header.bytes, header.length, actual_header_digest);
    cetta_native_sha256_hex(
        source_artifact.bytes, source_artifact.length,
        actual_source_digest);
    artifact_sha(&header, &source_artifact, actual_artifact_digest);
    Atom *artifact = root->expr.elems[cursor++];
    size_t header_length;
    size_t source_length;
    const char *submitted_header_digest = atom_expr_named(
        artifact, "artifact", 6u)
        ? atom_string_value(artifact->expr.elems[2]) : NULL;
    const char *submitted_source_digest = atom_expr_named(
        artifact, "artifact", 6u)
        ? atom_string_value(artifact->expr.elems[4]) : NULL;
    const char *submitted_artifact_digest = atom_expr_named(
        artifact, "artifact", 6u)
        ? atom_string_value(artifact->expr.elems[5]) : NULL;
    if (!atom_expr_named(artifact, "artifact", 6u) ||
        !atom_size_value(artifact->expr.elems[1], &header_length) ||
        !atom_size_value(artifact->expr.elems[3], &source_length) ||
        header_length != header.length || source_length != source_artifact.length ||
        !string_equal(submitted_header_digest, actual_header_digest,
                      "artifact header identity", error, error_size) ||
        !string_equal(submitted_source_digest, actual_source_digest,
                      "artifact source identity", error, error_size) ||
        !string_equal(submitted_artifact_digest, actual_artifact_digest,
                      "serialized artifact identity", error, error_size))
        goto done;

    if (!check_stage(root->expr.elems[cursor++], 0u, "source-selection",
                     "authored-gslt-v1", selected_digest,
                     error, error_size) ||
        !check_stage(root->expr.elems[cursor++], 1u, "source-admission",
                     selected_digest, admitted_digest,
                     error, error_size) ||
        !check_stage(root->expr.elems[cursor++], 2u, "plan-compilation",
                     admitted_digest, submitted_plan_digest,
                     error, error_size) ||
        !check_stage(root->expr.elems[cursor++], 3u,
                     "artifact-serialization", submitted_plan_digest,
                     actual_artifact_digest, error, error_size) ||
        cursor != root->expr.len)
        goto done;

    *rule_count_out = rule_count;
    accepted = true;

done:
    cetta_gslt_language_free(language);
    file_bytes_free(&source_artifact);
    file_bytes_free(&header);
    free(forms);
    if (arena_ready)
        arena_free(&arena);
    file_bytes_free(&certificate);
    return accepted;
}

int main(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr,
                "usage: %s PROFILE CERTIFICATE SOURCE_ROOT HEADER SOURCE "
                "GENERATOR SCHEMA\n",
                argv[0]);
        return 2;
    }
    const CettaGsltEmbeddedLanguageV1 *descriptor = NULL;
    const char *descriptor_symbol = NULL;
    if (strcmp(argv[1], "base") == 0) {
        descriptor = &cetta_zero_language_v1;
        descriptor_symbol = "cetta_zero_language_v1";
    } else if (strcmp(argv[1], "exp") == 0) {
        descriptor = &cetta_zero_exp_language_v1;
        descriptor_symbol = "cetta_zero_exp_language_v1";
    } else if (strcmp(argv[1], "emit") == 0) {
        descriptor = &cetta_zero_emit_language_v1;
        descriptor_symbol = "cetta_zero_emit_language_v1";
    } else if (strcmp(argv[1], "interact") == 0) {
        descriptor = &cetta_zero_interact_language_v1;
        descriptor_symbol = "cetta_zero_interact_language_v1";
    } else {
        fprintf(stderr, "error: unknown Zero certificate profile %s\n", argv[1]);
        return 2;
    }
    SymbolTable symbols;
    VarInternTable variable_names;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;

    char error[512] = {0};
    size_t rule_count = 0u;
    bool accepted = check_certificate(
            descriptor, descriptor_symbol, argv[1], argv[2], argv[3],
            argv[4], argv[5], argv[6], argv[7], &rule_count,
            error, sizeof(error));

    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);

    if (!accepted) {
        fprintf(stderr, "RejectedGsltCompilationCertificateV1: %s\n",
                error[0] ? error : "certificate check failed closed");
        return 1;
    }
    printf("(GsltCompilationCertificateV1Accepted profile=%s "
           "presentations=%zu rules=%zu stages=4)\n",
           argv[1], descriptor->semantic_source_count, rule_count);
    return 0;
}
