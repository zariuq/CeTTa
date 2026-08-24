#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "native/langdef_module.h"
#include "native/langdef_metta_equation_compiler_v1.h"
#include "native/gslt_petta_direct_v1.h"
#include "native/gslt_rhometta_direct_v1.h"

#include "finite_horn_gslt_v1.h"
#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_transparent_inline_native_v1.h"
#include "symbol.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LANGDEF_PATH_PARTS 1024u

static bool set_error(char *buffer, size_t size, const char *format, ...) {
    if (buffer != NULL && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool output_path(const char *base_file, const char *relative,
                        char output[PATH_MAX], char *error,
                        size_t error_size) {
    const char *slash;
    int written;
    if (base_file == NULL || relative == NULL || relative[0] == '\0')
        return set_error(error, error_size, "invalid output path");
    if (relative[0] == '/') {
        written = snprintf(output, PATH_MAX, "%s", relative);
    } else {
        slash = strrchr(base_file, '/');
        if (slash != NULL) {
            written = snprintf(output, PATH_MAX, "%.*s/%s",
                               (int)(slash - base_file), base_file, relative);
        } else {
            written = snprintf(output, PATH_MAX, "./%s", relative);
        }
    }
    if (written < 0 || written >= PATH_MAX)
        return set_error(error, error_size, "output path is too long");
    return true;
}

static bool selected_output_path(const char *base_file,
                                 const char *relative,
                                 const char *override,
                                 char output[PATH_MAX],
                                 char *error, size_t error_size) {
    int written;
    if (override == NULL)
        return output_path(base_file, relative, output, error, error_size);
    written = snprintf(output, PATH_MAX, "%s", override);
    if (written < 0 || written >= PATH_MAX)
        return set_error(error, error_size, "output path is too long");
    return true;
}

static bool split_path(char *path, char **parts, size_t *count,
                       char *error, size_t error_size) {
    char *save = NULL;
    char *part;
    *count = 0u;
    for (part = strtok_r(path, "/", &save);
         part != NULL;
         part = strtok_r(NULL, "/", &save)) {
        if (*count >= LANGDEF_PATH_PARTS)
            return set_error(error, error_size,
                             "path has too many components");
        parts[(*count)++] = part;
    }
    return true;
}

static char *relative_path(const char *base_directory,
                           const char *target,
                           char *error, size_t error_size) {
    char *base = strdup(base_directory);
    char *destination = strdup(target);
    char *base_parts[LANGDEF_PATH_PARTS];
    char *target_parts[LANGDEF_PATH_PARTS];
    size_t base_len = 0u;
    size_t target_len = 0u;
    size_t common = 0u;
    size_t result_len = 1u;
    char *result = NULL;
    char *cursor;

    if (base == NULL || destination == NULL)
        goto oom;
    if (!split_path(base, base_parts, &base_len, error, error_size) ||
        !split_path(destination, target_parts, &target_len,
                    error, error_size))
        goto done;
    while (common < base_len && common < target_len &&
           strcmp(base_parts[common], target_parts[common]) == 0)
        common++;
    for (size_t index = common; index < base_len; index++)
        result_len += 3u;
    for (size_t index = common; index < target_len; index++)
        result_len += strlen(target_parts[index]) + 1u;
    result = malloc(result_len);
    if (result == NULL)
        goto oom;
    cursor = result;
    for (size_t index = common; index < base_len; index++) {
        memcpy(cursor, "../", 3u);
        cursor += 3u;
    }
    for (size_t index = common; index < target_len; index++) {
        size_t len = strlen(target_parts[index]);
        memcpy(cursor, target_parts[index], len);
        cursor += len;
        if (index + 1u < target_len)
            *cursor++ = '/';
    }
    if (cursor == result)
        *cursor++ = '.';
    *cursor = '\0';
    goto done;

oom:
    set_error(error, error_size, "out of memory resolving source path");
    free(result);
    result = NULL;
done:
    free(destination);
    free(base);
    return result;
}

static bool read_child_stderr(int descriptor, char *error,
                              size_t error_size) {
    char chunk[4096];
    size_t used = 0u;
    ssize_t amount;
    bool ok = true;
    while ((amount = read(descriptor, chunk, sizeof(chunk))) > 0) {
        if (error != NULL && error_size > used + 1u) {
            size_t available = error_size - used - 1u;
            size_t copied = (size_t)amount < available
                ? (size_t)amount : available;
            memcpy(error + used, chunk, copied);
            used += copied;
        }
    }
    if (amount < 0)
        ok = false;
    if (error != NULL && error_size > 0u)
        error[used] = '\0';
    return ok;
}

static bool compile_answer_stream(const char *chart,
                                  const char *const *sources,
                                  uint32_t source_len,
                                  const char *const *reflected_sources,
                                  uint32_t reflected_source_len,
                                  const char *query,
                                  const char *timeout,
                                  const char *output_path_value,
                                  size_t *answer_len,
                                  char answer_digest[65],
                                  char *error, size_t error_size) {
    enum { FIXED_ARGUMENTS = 10 };
    char **arguments = NULL;
    char temporary[PATH_MAX] = {0};
    char diagnostics_text[4096] = {0};
    char stream_error[4096] = {0};
    int descriptor = -1;
    int diagnostics[2] = {-1, -1};
    pid_t child = -1;
    int status = 0;
    size_t argument_cap;
    size_t argument_len = 0u;
    FHAnswerStreamV1 stream;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    if (error && error_size > 0u)
        error[0] = '\0';
    if (!chart || !sources || source_len == 0u || !query || !timeout ||
        !output_path_value || !answer_len || !answer_digest ||
        source_len + reflected_source_len > CETTA_LANGDEF_MAX_SOURCES) {
        set_error(error, error_size, "invalid finite-Horn query request");
        goto done;
    }
    argument_cap = (size_t)source_len +
                   (size_t)reflected_source_len * 2u + FIXED_ARGUMENTS;
    arguments = calloc(argument_cap, sizeof(*arguments));
    if (!arguments) {
        set_error(error, error_size,
                  "out of memory constructing finite-Horn query");
        goto done;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX",
                 output_path_value) >= (int)sizeof(temporary)) {
        set_error(error, error_size,
                  "finite-Horn answer path is too long");
        goto done;
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        set_error(error, error_size,
                  "cannot create finite-Horn answer temporary: %s",
                  strerror(errno));
        goto done;
    }
    {
        const uint8_t incomplete = 0u;
        if (write(descriptor, &incomplete, 1u) != 1 ||
            close(descriptor) != 0) {
            descriptor = -1;
            set_error(error, error_size,
                      "cannot seal finite-Horn answer temporary: %s",
                      strerror(errno));
            goto done;
        }
        descriptor = -1;
    }
    arguments[argument_len++] = (char *)chart;
    for (uint32_t index = 0u; index < source_len; index++)
        arguments[argument_len++] = (char *)sources[index];
    for (uint32_t index = 0u; index < reflected_source_len; index++) {
        arguments[argument_len++] = (char *)"--reflect-source";
        arguments[argument_len++] = (char *)reflected_sources[index];
    }
    arguments[argument_len++] = (char *)"--query-text";
    arguments[argument_len++] = (char *)query;
    arguments[argument_len++] = (char *)"--summary";
    arguments[argument_len++] = (char *)"--timeout";
    arguments[argument_len++] = (char *)timeout;
    arguments[argument_len++] = (char *)"--answer-out";
    arguments[argument_len++] = temporary;
    arguments[argument_len] = NULL;
    if (argument_len + 1u > argument_cap) {
        set_error(error, error_size,
                  "internal finite-Horn argument overflow");
        goto done;
    }
    if (pipe(diagnostics) != 0) {
        set_error(error, error_size,
                  "cannot create finite-Horn diagnostics pipe: %s",
                  strerror(errno));
        goto done;
    }
    child = fork();
    if (child < 0) {
        set_error(error, error_size,
                  "cannot spawn finite-Horn compiler: %s",
                  strerror(errno));
        goto done;
    }
    if (child == 0) {
        if (dup2(diagnostics[1], STDOUT_FILENO) < 0 ||
            dup2(diagnostics[1], STDERR_FILENO) < 0)
            _exit(126);
        close(diagnostics[0]);
        close(diagnostics[1]);
        execv(chart, arguments);
        _exit(127);
    }
    close(diagnostics[1]);
    diagnostics[1] = -1;
    if (!read_child_stderr(diagnostics[0], diagnostics_text,
                           sizeof(diagnostics_text))) {
        set_error(error, error_size,
                  "cannot read finite-Horn compiler diagnostics");
        goto done;
    }
    close(diagnostics[0]);
    diagnostics[0] = -1;
    if (waitpid(child, &status, 0) < 0) {
        set_error(error, error_size,
                  "cannot wait for finite-Horn compiler: %s",
                  strerror(errno));
        goto done;
    }
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (diagnostics_text[0] != '\0')
            set_error(error, error_size, "%s", diagnostics_text);
        else
            set_error(error, error_size,
                      "finite-Horn compiler exited unsuccessfully");
        goto done;
    }
    if (!fh_answer_stream_v1_read(&stream, temporary,
                                  stream_error, sizeof(stream_error))) {
        if (diagnostics_text[0] != '\0')
            set_error(error, error_size,
                      "finite-Horn answer stream is invalid (%s); "
                      "compiler receipt: %s",
                      stream_error, diagnostics_text);
        else
            set_error(error, error_size, "%s", stream_error);
        goto done;
    }
    if (rename(temporary, output_path_value) != 0) {
        set_error(error, error_size,
                  "cannot publish finite-Horn answer stream: %s",
                  strerror(errno));
        goto done;
    }
    temporary[0] = '\0';
    *answer_len = stream.len;
    memcpy(answer_digest, stream.digest, sizeof(stream.digest));
    ok = true;

done:
    if (child > 0)
        (void)waitpid(child, &status, 0);
    if (diagnostics[0] >= 0)
        close(diagnostics[0]);
    if (diagnostics[1] >= 0)
        close(diagnostics[1]);
    if (descriptor >= 0)
        close(descriptor);
    if (temporary[0] != '\0')
        (void)unlink(temporary);
    fh_answer_stream_v1_free(&stream);
    free(arguments);
    return ok;
}

static bool run_exporter(const char *compiler_root,
                         const char *exporter,
                         const char *presentation_root,
                         const char *start_name,
                         const char *expected_closure,
                         char *const *relative_sources,
                         uint32_t source_len,
                         const char *output_path_value,
                         char *error, size_t error_size) {
    char **arguments = NULL;
    char temporary[PATH_MAX];
    int output = -1;
    int diagnostics[2] = {-1, -1};
    pid_t child = -1;
    int status = 0;
    bool ok = false;
    size_t argument_len = (size_t)source_len + 9u;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX",
                 output_path_value) >= (int)sizeof(temporary))
        return set_error(error, error_size,
                         "parser-pack temporary path is too long");
    output = mkstemp(temporary);
    if (output < 0)
        return set_error(error, error_size,
                         "cannot create parser-pack temporary file: %s",
                         strerror(errno));
    if (pipe(diagnostics) != 0) {
        set_error(error, error_size,
                  "cannot create compiler diagnostics pipe: %s",
                  strerror(errno));
        goto done;
    }
    arguments = calloc(argument_len, sizeof(*arguments));
    if (arguments == NULL) {
        set_error(error, error_size,
                  "out of memory constructing compiler invocation");
        goto done;
    }
    arguments[0] = (char *)"swipl";
    arguments[1] = (char *)"-q";
    arguments[2] = (char *)"-f";
    arguments[3] = (char *)exporter;
    arguments[4] = (char *)"--";
    arguments[5] = (char *)presentation_root;
    arguments[6] = (char *)start_name;
    arguments[7] = (char *)expected_closure;
    for (uint32_t index = 0u; index < source_len; index++)
        arguments[8u + index] = relative_sources[index];
    arguments[8u + source_len] = NULL;

    child = fork();
    if (child < 0) {
        set_error(error, error_size, "cannot spawn GSLT compiler: %s",
                  strerror(errno));
        goto done;
    }
    if (child == 0) {
        if (chdir(compiler_root) != 0 ||
            dup2(output, STDOUT_FILENO) < 0 ||
            dup2(diagnostics[1], STDERR_FILENO) < 0)
            _exit(126);
        close(output);
        close(diagnostics[0]);
        close(diagnostics[1]);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    close(output);
    output = -1;
    close(diagnostics[1]);
    diagnostics[1] = -1;
    if (!read_child_stderr(diagnostics[0], error, error_size)) {
        set_error(error, error_size,
                  "cannot read GSLT compiler diagnostics");
        goto done;
    }
    close(diagnostics[0]);
    diagnostics[0] = -1;
    if (waitpid(child, &status, 0) < 0) {
        set_error(error, error_size, "cannot wait for GSLT compiler: %s",
                  strerror(errno));
        goto done;
    }
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error == NULL || error[0] == '\0')
            set_error(error, error_size,
                      "GSLT compiler exited unsuccessfully");
        goto done;
    }
    if (rename(temporary, output_path_value) != 0) {
        set_error(error, error_size, "cannot publish parser pack: %s",
                  strerror(errno));
        goto done;
    }
    temporary[0] = '\0';
    ok = true;

done:
    if (child > 0) {
        (void)waitpid(child, &status, 0);
    }
    if (diagnostics[0] >= 0)
        close(diagnostics[0]);
    if (diagnostics[1] >= 0)
        close(diagnostics[1]);
    if (output >= 0)
        close(output);
    if (temporary[0] != '\0')
        (void)unlink(temporary);
    free(arguments);
    return ok;
}

static bool print_quoted(FILE *stream, const char *text) {
    if (fputc('"', stream) == EOF)
        return false;
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != 0u; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            if (fputc('\\', stream) == EOF ||
                fputc(*cursor, stream) == EOF)
                return false;
        } else if (*cursor == '\n') {
            if (fputs("\\n", stream) == EOF)
                return false;
        } else if (*cursor == '\r') {
            if (fputs("\\r", stream) == EOF)
                return false;
        } else if (*cursor == '\t') {
            if (fputs("\\t", stream) == EOF)
                return false;
        } else if (*cursor < 0x20u || *cursor == 0x7fu) {
            return false;
        } else if (fputc(*cursor, stream) == EOF) {
            return false;
        }
    }
    return fputc('"', stream) != EOF;
}

static bool write_digest_field(FILE *stream, const char *name,
                               const char *digest) {
    return fprintf(stream, "  (%s ", name) >= 0 &&
           print_quoted(stream, digest) &&
           fputs(")\n", stream) != EOF;
}

static bool write_lock(const char *path,
                       const CettaLangDefManifestV1 *manifest,
                       const char *manifest_digest,
                       const char *pack_file_digest,
                       const PPABIV1Pack *pack,
                       const char source_digests[][65],
                       const char extension_source_digests[][65],
                       const char extension_artifact_digests[][65],
                       const char *program_source_digest,
                       const char *program_digest,
                       const char *compiled_cursor_digest,
                       char *error, size_t error_size) {
    char temporary[PATH_MAX];
    int descriptor;
    FILE *stream = NULL;
    bool ok = false;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >=
        (int)sizeof(temporary))
        return set_error(error, error_size,
                         "lock temporary path is too long");
    descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return set_error(error, error_size,
                         "cannot create lock temporary file: %s",
                         strerror(errno));
    stream = fdopen(descriptor, "wb");
    if (stream == NULL) {
        close(descriptor);
        set_error(error, error_size, "cannot open lock stream: %s",
                  strerror(errno));
        goto done;
    }
    ok = fputs("(gslt-langdef-lock-v1\n", stream) != EOF &&
         write_digest_field(stream, "manifest-sha256", manifest_digest) &&
         write_digest_field(stream, "parser-pack-sha256", pack_file_digest) &&
         write_digest_field(stream, "source-digest", pack->source_digest) &&
         write_digest_field(stream, "compiler-digest", pack->compiler_digest) &&
         write_digest_field(stream, "environment-digest",
                            pack->environment_digest) &&
         write_digest_field(stream, "pack-digest", pack->pack_digest);
    for (uint32_t index = 0u; ok && index < manifest->source_len; index++) {
        ok = fputs("  (source-sha256 ", stream) != EOF &&
             print_quoted(stream, manifest->sources[index]) &&
             fputc(' ', stream) != EOF &&
             print_quoted(stream, source_digests[index]) &&
             fputs(")\n", stream) != EOF;
    }
    for (uint32_t index = 0u;
         ok && index < manifest->extension_source_len; index++) {
        ok = fputs("  (extension-source-sha256 ", stream) != EOF &&
             print_quoted(stream, manifest->extension_sources[index]) &&
             fputc(' ', stream) != EOF &&
             print_quoted(stream, extension_source_digests[index]) &&
             fputs(")\n", stream) != EOF;
    }
    for (uint32_t index = 0u;
         ok && index < manifest->extension_artifact_len; index++) {
        ok = fputs("  (extension-artifact-sha256 ", stream) != EOF &&
             print_quoted(
                 stream, manifest->extension_artifact_roles[index]) &&
             fputc(' ', stream) != EOF &&
             print_quoted(
                 stream, manifest->extension_artifact_relatives[index]) &&
             fputc(' ', stream) != EOF &&
             print_quoted(stream, extension_artifact_digests[index]) &&
             fputs(")\n", stream) != EOF;
    }
    if (ok && manifest->program_relative != NULL) {
        ok = fputs("  (program-source-sha256 ", stream) != EOF &&
             print_quoted(stream, manifest->program_source_relative) &&
             fputc(' ', stream) != EOF &&
             print_quoted(stream, program_source_digest) &&
             fputs(")\n", stream) != EOF &&
             write_digest_field(stream, "program-sha256", program_digest);
    }
    if (ok && manifest->compiled_cursor_relative != NULL)
        ok = write_digest_field(
            stream, "compiled-cursor-sha256",
            compiled_cursor_digest);
    ok = ok && fputs(")\n", stream) != EOF && fclose(stream) == 0;
    stream = NULL;
    if (!ok) {
        set_error(error, error_size, "cannot write langdef lock");
        goto done;
    }
    if (rename(temporary, path) != 0) {
        set_error(error, error_size, "cannot publish langdef lock: %s",
                  strerror(errno));
        goto done;
    }
    temporary[0] = '\0';
    ok = true;

done:
    if (stream != NULL)
        (void)fclose(stream);
    if (temporary[0] != '\0')
        (void)unlink(temporary);
    return ok;
}

static bool compile_parser_pack(const char *manifest_argument,
                                const char *compiler_root_argument,
                                const char *presentation_root_argument,
                                const char *pack_output_override,
                                const char *lock_output_override,
                                char *error, size_t error_size) {
    char *manifest_path = NULL;
    char *compiler_root = NULL;
    char *presentation_root = NULL;
    char exporter[PATH_MAX];
    char pack_path[PATH_MAX];
    char lock_path[PATH_MAX];
    char source_paths[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX];
    char source_digests[CETTA_LANGDEF_MAX_SOURCES][65];
    char extension_source_paths[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES][PATH_MAX];
    char extension_source_digests[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES][65];
    char extension_artifact_paths[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS][PATH_MAX];
    char extension_artifact_digests[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS][65];
    char *relative_sources[CETTA_LANGDEF_MAX_SOURCES] = {0};
    char manifest_digest[65];
    char pack_file_digest[65];
    char program_source_path[PATH_MAX];
    char program_path[PATH_MAX];
    char compiled_cursor_path[PATH_MAX];
    char program_source_digest[65] = {0};
    char program_digest[65] = {0};
    char compiled_cursor_digest[65] = {0};
    Arena arena;
    Atom *root;
    CettaLangDefManifestV1 manifest;
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    const char *start_name;
    bool ok = false;

    arena_init(&arena);
    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    manifest_path = realpath(manifest_argument, NULL);
    compiler_root = realpath(compiler_root_argument, NULL);
    presentation_root = realpath(presentation_root_argument, NULL);
    if (manifest_path == NULL || compiler_root == NULL ||
        presentation_root == NULL) {
        set_error(error, error_size, "cannot resolve compiler input path: %s",
                  strerror(errno));
        goto done;
    }
    if (snprintf(exporter, sizeof(exporter),
                 "%s/parser_pack_abi_export_v1.pl", compiler_root) >=
        (int)sizeof(exporter) || access(exporter, R_OK) != 0) {
        set_error(error, error_size,
                  "compiler root lacks parser_pack_abi_export_v1.pl");
        goto done;
    }
    root = cetta_langdef_read_single_form(manifest_path, &arena,
                                          error, error_size);
    if (root == NULL ||
        !cetta_langdef_manifest_parse(root, &manifest, error, error_size) ||
        !cetta_langdef_expr_head(manifest.start, "pp-def", 1u) ||
        manifest.start->expr.elems[1]->kind != ATOM_SYMBOL ||
        (start_name = atom_name_cstr(manifest.start->expr.elems[1])) == NULL ||
        !selected_output_path(manifest_path, manifest.pack_relative,
                              pack_output_override, pack_path,
                              error, error_size) ||
        !selected_output_path(manifest_path, manifest.lock_relative,
                              lock_output_override, lock_path,
                              error, error_size)) {
        if (error[0] == '\0')
            set_error(error, error_size,
                      "manifest start must have form (pp-def name)");
        goto done;
    }
    for (uint32_t index = 0u; index < manifest.source_len; index++) {
        if (!cetta_langdef_path_join(manifest_path, manifest.sources[index],
                                     source_paths[index],
                                     sizeof(source_paths[index]),
                                     error, error_size) ||
            !cetta_langdef_sha256_file(source_paths[index],
                                       source_digests[index],
                                       error, error_size) ||
            (relative_sources[index] = relative_path(
                 presentation_root, source_paths[index],
                 error, error_size)) == NULL)
            goto done;
    }
    for (uint32_t index = 0u;
         index < manifest.extension_source_len; index++) {
        if (!cetta_langdef_path_join(
                manifest_path, manifest.extension_sources[index],
                extension_source_paths[index],
                sizeof(extension_source_paths[index]), error, error_size) ||
            !cetta_langdef_sha256_file(
                extension_source_paths[index],
                extension_source_digests[index], error, error_size))
            goto done;
    }
    for (uint32_t index = 0u;
         index < manifest.extension_artifact_len; index++) {
        if (!cetta_langdef_path_join(
                manifest_path, manifest.extension_artifact_relatives[index],
                extension_artifact_paths[index],
                sizeof(extension_artifact_paths[index]), error, error_size) ||
            !cetta_langdef_sha256_file(
                extension_artifact_paths[index],
                extension_artifact_digests[index], error, error_size))
            goto done;
    }
    if (manifest.program_relative != NULL &&
        (!cetta_langdef_path_join(manifest_path,
                                  manifest.program_source_relative,
                                  program_source_path,
                                  sizeof(program_source_path),
                                  error, error_size) ||
         !cetta_langdef_path_join(manifest_path, manifest.program_relative,
                                  program_path, sizeof(program_path),
                                  error, error_size) ||
         !cetta_langdef_sha256_file(program_source_path,
                                    program_source_digest,
                                    error, error_size) ||
         !cetta_langdef_sha256_file(program_path, program_digest,
                                    error, error_size)))
        goto done;
    if (manifest.compiled_cursor_relative != NULL &&
        (!cetta_langdef_path_join(
             manifest_path, manifest.compiled_cursor_relative,
             compiled_cursor_path, sizeof(compiled_cursor_path),
             error, error_size) ||
         !cetta_langdef_sha256_file(
             compiled_cursor_path, compiled_cursor_digest,
             error, error_size)))
        goto done;
    if (!run_exporter(compiler_root, exporter, presentation_root,
                      start_name,
                      manifest.parser_pack_expected_closed
                          ? "closed" : "partial",
                      relative_sources, manifest.source_len,
                      pack_path, error, error_size) ||
        !ppabi_v1_wire_read(&wire, pack_path, error, error_size) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, error_size) ||
        wire.expected_closed != manifest.parser_pack_expected_closed ||
        !atom_eq(wire.start, manifest.start) ||
        !cetta_langdef_sha256_file(manifest_path, manifest_digest,
                                   error, error_size) ||
        !cetta_langdef_sha256_file(pack_path, pack_file_digest,
                                   error, error_size)) {
        if (error[0] == '\0')
            set_error(error, error_size,
                      "compiled parser pack disagrees with its manifest");
        goto done;
    }
    ok = write_lock(lock_path, &manifest, manifest_digest,
                    pack_file_digest, &pack, source_digests,
                    extension_source_digests,
                    extension_artifact_digests,
                    program_source_digest, program_digest,
                    compiled_cursor_digest,
                    error, error_size);

done:
    for (uint32_t index = 0u; index < CETTA_LANGDEF_MAX_SOURCES; index++)
        free(relative_sources[index]);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    arena_free(&arena);
    free(presentation_root);
    free(compiler_root);
    free(manifest_path);
    return ok;
}

static bool seal_langdef(const char *manifest_argument,
                         const char *pack_override,
                         const char *compiled_cursor_override,
                         const char *lock_output_override,
                         char *error, size_t error_size) {
    char *manifest_path = NULL;
    char pack_path[PATH_MAX];
    char lock_path[PATH_MAX];
    char source_path[PATH_MAX];
    char source_digests[CETTA_LANGDEF_MAX_SOURCES][65];
    char extension_source_path[PATH_MAX];
    char extension_source_digests[
        CETTA_LANGDEF_MAX_EXTENSION_SOURCES][65];
    char extension_artifact_path[PATH_MAX];
    char extension_artifact_digests[
        CETTA_LANGDEF_MAX_EXTENSION_ARTIFACTS][65];
    char manifest_digest[65];
    char pack_file_digest[65];
    char program_source_path[PATH_MAX];
    char program_path[PATH_MAX];
    char program_source_digest[65] = {0};
    char program_digest[65] = {0};
    char compiled_cursor_path[PATH_MAX];
    char compiled_cursor_digest[65] = {0};
    Arena arena;
    Atom *root;
    CettaLangDefManifestV1 manifest;
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    bool ok = false;

    arena_init(&arena);
    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    manifest_path = realpath(manifest_argument, NULL);
    if (!manifest_path) {
        set_error(error, error_size, "cannot resolve manifest path: %s",
                  strerror(errno));
        goto done;
    }
    root = cetta_langdef_read_single_form(
        manifest_path, &arena, error, error_size);
    if (!root ||
        !cetta_langdef_manifest_parse(root, &manifest, error, error_size) ||
        !selected_output_path(manifest_path, manifest.pack_relative,
                              pack_override, pack_path,
                              error, error_size) ||
        !selected_output_path(manifest_path, manifest.lock_relative,
                              lock_output_override, lock_path,
                              error, error_size))
        goto done;
    for (uint32_t index = 0u; index < manifest.source_len; index++) {
        if (!cetta_langdef_path_join(
                manifest_path, manifest.sources[index],
                source_path, sizeof(source_path), error, error_size) ||
            !cetta_langdef_sha256_file(
                source_path, source_digests[index], error, error_size))
            goto done;
    }
    for (uint32_t index = 0u;
         index < manifest.extension_source_len; index++) {
        if (!cetta_langdef_path_join(
                manifest_path, manifest.extension_sources[index],
                extension_source_path, sizeof(extension_source_path),
                error, error_size) ||
            !cetta_langdef_sha256_file(
                extension_source_path, extension_source_digests[index],
                error, error_size))
            goto done;
    }
    for (uint32_t index = 0u;
         index < manifest.extension_artifact_len; index++) {
        if (!cetta_langdef_path_join(
                manifest_path, manifest.extension_artifact_relatives[index],
                extension_artifact_path, sizeof(extension_artifact_path),
                error, error_size) ||
            !cetta_langdef_sha256_file(
                extension_artifact_path, extension_artifact_digests[index],
                error, error_size))
            goto done;
    }
    if (manifest.program_relative != NULL &&
        (!cetta_langdef_path_join(
             manifest_path, manifest.program_source_relative,
             program_source_path, sizeof(program_source_path),
             error, error_size) ||
         !cetta_langdef_path_join(
             manifest_path, manifest.program_relative,
             program_path, sizeof(program_path), error, error_size) ||
         !cetta_langdef_sha256_file(
             program_source_path, program_source_digest,
             error, error_size) ||
         !cetta_langdef_sha256_file(
             program_path, program_digest, error, error_size)))
        goto done;
    if (manifest.compiled_cursor_relative != NULL &&
        (!selected_output_path(
             manifest_path, manifest.compiled_cursor_relative,
             compiled_cursor_override, compiled_cursor_path,
             error, error_size) ||
         !cetta_langdef_sha256_file(
             compiled_cursor_path, compiled_cursor_digest,
             error, error_size)))
        goto done;
    if (!ppabi_v1_wire_read(&wire, pack_path, error, error_size) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, error_size) ||
        wire.expected_closed != manifest.parser_pack_expected_closed ||
        !atom_eq(wire.start, manifest.start) ||
        !cetta_langdef_sha256_file(
            manifest_path, manifest_digest, error, error_size) ||
        !cetta_langdef_sha256_file(
            pack_path, pack_file_digest, error, error_size)) {
        if (!error[0])
            set_error(error, error_size,
                      "parser pack disagrees with its manifest");
        goto done;
    }
    ok = write_lock(
        lock_path, &manifest, manifest_digest, pack_file_digest,
        &pack, source_digests, extension_source_digests,
        extension_artifact_digests,
        program_source_digest, program_digest,
        compiled_cursor_digest, error, error_size);

done:
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    arena_free(&arena);
    free(manifest_path);
    return ok;
}

static bool write_atomic(const char *path, const uint8_t *bytes, size_t len,
                         char *error, size_t error_size) {
    char temporary[PATH_MAX];
    int descriptor;
    size_t written = 0u;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >=
        (int)sizeof(temporary))
        return set_error(error, error_size,
                         "generated-program temporary path is too long");
    descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return set_error(error, error_size,
                         "cannot create generated-program temporary: %s",
                         strerror(errno));
    while (written < len) {
        ssize_t amount = write(descriptor, bytes + written, len - written);
        if (amount <= 0) {
            close(descriptor);
            (void)unlink(temporary);
            return set_error(error, error_size,
                             "cannot write generated program: %s",
                             strerror(errno));
        }
        written += (size_t)amount;
    }
    if (close(descriptor) != 0 || rename(temporary, path) != 0) {
        (void)unlink(temporary);
        return set_error(error, error_size,
                         "cannot publish generated program: %s",
                         strerror(errno));
    }
    return true;
}

static bool read_file_bytes_v1(const char *path,
                               uint8_t **out,
                               size_t *out_len,
                               char *error,
                               size_t error_size) {
    FILE *file = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;
    if (!path || !out || !out_len)
        return set_error(error, error_size, "invalid file-read request");
    *out = NULL;
    *out_len = 0u;
    file = fopen(path, "rb");
    if (!file)
        return set_error(error, error_size,
                         "cannot open %s: %s", path, strerror(errno));
    for (;;) {
        if (len == cap) {
            size_t next = cap == 0u ? 16384u : cap * 2u;
            if (next < cap) {
                set_error(error, error_size, "%s is too large", path);
                goto done;
            }
            uint8_t *grown = realloc(bytes, next);
            if (!grown) {
                set_error(error, error_size,
                          "out of memory reading %s", path);
                goto done;
            }
            bytes = grown;
            cap = next;
        }
        size_t amount = fread(bytes + len, 1u, cap - len, file);
        len += amount;
        if (amount == 0u) {
            if (ferror(file)) {
                set_error(error, error_size,
                          "cannot read %s: %s", path, strerror(errno));
                goto done;
            }
            break;
        }
    }
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = true;

done:
    if (fclose(file) != 0 && ok) {
        free(*out);
        *out = NULL;
        *out_len = 0u;
        ok = set_error(error, error_size,
                       "cannot close %s: %s", path, strerror(errno));
    }
    free(bytes);
    return ok;
}

typedef struct {
    char *text;
    size_t len;
} LangDefCanonicalAnswerV1;

typedef struct {
    char *name;
    size_t name_len;
    CettaExprLen arity;
    SymbolId symbol_id;
} LangDefOperatorV1;

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} LangDefByteBufferV1;

typedef struct {
    char *name;
    size_t name_len;
    uint32_t arity;
} LangDefSemanticOperatorV1;

typedef struct {
    char *id;
    size_t id_len;
    char *text;
    size_t text_len;
} LangDefSemanticRuleV1;

static int canonical_answer_compare(const void *left, const void *right) {
    const LangDefCanonicalAnswerV1 *lhs = left;
    const LangDefCanonicalAnswerV1 *rhs = right;
    return strcmp(lhs->text, rhs->text);
}

static bool canonical_answer_push(LangDefCanonicalAnswerV1 **answers,
                                  size_t *len, size_t *cap,
                                  char *text, size_t text_len,
                                  char *error, size_t error_size) {
    LangDefCanonicalAnswerV1 *next;
    size_t next_cap;
    if (*len < *cap) {
        (*answers)[*len] = (LangDefCanonicalAnswerV1){text, text_len};
        (*len)++;
        return true;
    }
    next_cap = *cap ? *cap : 128u;
    while (next_cap <= *len) {
        if (next_cap > SIZE_MAX / 2u)
            return set_error(error, error_size,
                             "finite-Horn answer family is too large");
        next_cap *= 2u;
    }
    if (next_cap > SIZE_MAX / sizeof(**answers))
        return set_error(error, error_size,
                         "finite-Horn answer family is too large");
    next = realloc(*answers, next_cap * sizeof(**answers));
    if (!next)
        return set_error(error, error_size,
                         "out of memory collecting finite-Horn answers");
    *answers = next;
    *cap = next_cap;
    (*answers)[*len] = (LangDefCanonicalAnswerV1){text, text_len};
    (*len)++;
    return true;
}

static int langdef_operator_compare(const void *left, const void *right) {
    const LangDefOperatorV1 *lhs = left;
    const LangDefOperatorV1 *rhs = right;
    int names = strcmp(lhs->name, rhs->name);
    if (names != 0)
        return names;
    return lhs->arity < rhs->arity ? -1 : lhs->arity > rhs->arity;
}

enum { LANGDEF_SEMANTIC_QUOTE_DEPTH_V1 = 4096u };

static void langdef_byte_buffer_v1_free(LangDefByteBufferV1 *buffer) {
    if (!buffer)
        return;
    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

static bool langdef_byte_buffer_v1_reserve(
    LangDefByteBufferV1 *buffer, size_t additional,
    char *error, size_t error_size) {
    size_t required;
    size_t next_cap;
    uint8_t *next;

    if (!buffer || additional > SIZE_MAX - buffer->len)
        return set_error(error, error_size,
                         "semantic GSLT output is too large");
    required = buffer->len + additional;
    if (required <= buffer->cap)
        return true;
    next_cap = buffer->cap ? buffer->cap : 4096u;
    while (next_cap < required) {
        if (next_cap > SIZE_MAX / 2u) {
            next_cap = required;
            break;
        }
        next_cap *= 2u;
    }
    next = realloc(buffer->bytes, next_cap);
    if (!next)
        return set_error(error, error_size,
                         "out of memory constructing semantic GSLT");
    buffer->bytes = next;
    buffer->cap = next_cap;
    return true;
}

static bool langdef_byte_buffer_v1_append(
    LangDefByteBufferV1 *buffer, const void *bytes, size_t len,
    char *error, size_t error_size) {
    if ((len > 0u && !bytes) ||
        !langdef_byte_buffer_v1_reserve(
            buffer, len, error, error_size))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool langdef_byte_buffer_v1_literal(
    LangDefByteBufferV1 *buffer, const char *text,
    char *error, size_t error_size) {
    return text && langdef_byte_buffer_v1_append(
        buffer, text, strlen(text), error, error_size);
}

static bool langdef_semantic_q_nat_v1(
    const Atom *term, uint32_t *value,
    char *error, size_t error_size) {
    uint32_t count = 0u;
    const Atom *cursor = term;

    if (!value)
        return set_error(error, error_size,
                         "invalid quoted natural destination");
    while (cursor && cursor->kind == ATOM_EXPR &&
           cursor->expr.len == 2u &&
           atom_is_symbol(cursor->expr.elems[0], "q-succ")) {
        if (count == UINT32_MAX)
            return set_error(error, error_size,
                             "quoted natural exceeds uint32");
        count++;
        cursor = cursor->expr.elems[1];
    }
    if (!cursor || cursor->kind != ATOM_SYMBOL ||
        !atom_is_symbol((Atom *)cursor, "q-zero"))
        return set_error(error, error_size,
                         "quoted natural has the wrong shape");
    *value = count;
    return true;
}

static bool langdef_semantic_q_symbol_v1(
    const Atom *term, const Atom **symbol,
    char *error, size_t error_size) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 2u ||
        !atom_is_symbol(term->expr.elems[0], "q-sym") ||
        !term->expr.elems[1] ||
        term->expr.elems[1]->kind != ATOM_SYMBOL || !symbol)
        return set_error(error, error_size,
                         "quoted symbol has the wrong shape");
    *symbol = term->expr.elems[1];
    return true;
}

static bool langdef_semantic_symbol_text_v1(
    const Atom *symbol, char **text, size_t *text_len,
    char *error, size_t error_size) {
    uint8_t *rendered = NULL;
    size_t rendered_len = 0u;

    if (!symbol || symbol->kind != ATOM_SYMBOL || !text || !text_len ||
        !fh_ground_term_v1_render(
            symbol, &rendered, &rendered_len, error, error_size))
        return false;
    *text = (char *)rendered;
    *text_len = rendered_len;
    return true;
}

static bool langdef_semantic_q_list_next_v1(
    const Atom **cursor, const Atom **item, bool *done,
    char *error, size_t error_size) {
    const Atom *term;

    if (!cursor || !*cursor || !item || !done)
        return set_error(error, error_size,
                         "invalid quoted list cursor");
    term = *cursor;
    if (term->kind == ATOM_SYMBOL &&
        atom_is_symbol((Atom *)term, "q-nil")) {
        *item = NULL;
        *done = true;
        return true;
    }
    if (term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], "q-cons"))
        return set_error(error, error_size,
                         "quoted list has the wrong shape");
    *item = term->expr.elems[1];
    *cursor = term->expr.elems[2];
    *done = false;
    return true;
}

static bool langdef_semantic_render_q_term_v1(
    LangDefByteBufferV1 *buffer, const Atom *term, uint32_t depth,
    char *error, size_t error_size) {
    const Atom *head_symbol;
    const Atom *arguments;
    const Atom *argument;
    bool done;

    if (depth > LANGDEF_SEMANTIC_QUOTE_DEPTH_V1)
        return set_error(error, error_size,
                         "quoted semantic term exceeds its depth limit");
    if (term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
        atom_is_symbol(term->expr.elems[0], "q-var")) {
        uint32_t index;
        char variable[32];
        int variable_len;
        if (!langdef_semantic_q_nat_v1(
                term->expr.elems[1], &index, error, error_size))
            return false;
        variable_len = snprintf(variable, sizeof(variable), "?v%" PRIu32,
                                index);
        return variable_len > 0 &&
               (size_t)variable_len < sizeof(variable) &&
               langdef_byte_buffer_v1_append(
                   buffer, variable, (size_t)variable_len,
                   error, error_size);
    }
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], "q-app") ||
        !langdef_semantic_q_symbol_v1(
            term->expr.elems[1], &head_symbol, error, error_size))
        return false;
    arguments = term->expr.elems[2];
    if (!langdef_semantic_q_list_next_v1(
            &arguments, &argument, &done, error, error_size) ||
        (!done && !langdef_byte_buffer_v1_literal(
                      buffer, "(", error, error_size)))
        return false;
    {
        char *head = NULL;
        size_t head_len = 0u;
        bool ok = langdef_semantic_symbol_text_v1(
            head_symbol, &head, &head_len, error, error_size) &&
            langdef_byte_buffer_v1_append(
                buffer, head, head_len, error, error_size);
        free(head);
        if (!ok)
            return false;
    }
    if (done)
        return true;
    for (;;) {
        if (!langdef_byte_buffer_v1_literal(
                buffer, " ", error, error_size) ||
            !langdef_semantic_render_q_term_v1(
                buffer, argument, depth + 1u, error, error_size))
            return false;
        if (!langdef_semantic_q_list_next_v1(
                &arguments, &argument, &done, error, error_size))
            return false;
        if (done)
            break;
    }
    return langdef_byte_buffer_v1_literal(
        buffer, ")", error, error_size);
}

static int langdef_semantic_operator_compare_v1(
    const void *left, const void *right) {
    const LangDefSemanticOperatorV1 *lhs = left;
    const LangDefSemanticOperatorV1 *rhs = right;
    int names = strcmp(lhs->name, rhs->name);
    if (names != 0)
        return names;
    return lhs->arity < rhs->arity ? -1 : lhs->arity > rhs->arity;
}

static int langdef_semantic_rule_compare_v1(
    const void *left, const void *right) {
    const LangDefSemanticRuleV1 *lhs = left;
    const LangDefSemanticRuleV1 *rhs = right;
    return strcmp(lhs->id, rhs->id);
}

static bool langdef_semantic_rule_build_v1(
    const Atom *quoted_rule, LangDefSemanticRuleV1 *rule,
    char *error, size_t error_size) {
    const Atom *id_symbol;
    const Atom *body;
    const Atom *premise;
    LangDefByteBufferV1 output = {0};
    bool done;
    bool ok = false;

    if (!quoted_rule || quoted_rule->kind != ATOM_EXPR ||
        quoted_rule->expr.len != 4u ||
        !atom_is_symbol(quoted_rule->expr.elems[0], "q-rule") ||
        !langdef_semantic_q_symbol_v1(
            quoted_rule->expr.elems[1], &id_symbol,
            error, error_size) || !rule)
        goto done;
    memset(rule, 0, sizeof(*rule));
    if (!langdef_semantic_symbol_text_v1(
            id_symbol, &rule->id, &rule->id_len,
            error, error_size) ||
        !langdef_byte_buffer_v1_literal(
            &output, "    (rule ", error, error_size) ||
        !langdef_byte_buffer_v1_append(
            &output, rule->id, rule->id_len, error, error_size) ||
        !langdef_byte_buffer_v1_literal(
            &output, "\n      (head ", error, error_size) ||
        !langdef_semantic_render_q_term_v1(
            &output, quoted_rule->expr.elems[2], 0u,
            error, error_size) ||
        !langdef_byte_buffer_v1_literal(
            &output, ")\n      (body", error, error_size))
        goto done;
    body = quoted_rule->expr.elems[3];
    for (;;) {
        if (!langdef_semantic_q_list_next_v1(
                &body, &premise, &done, error, error_size))
            goto done;
        if (done)
            break;
        if (!langdef_byte_buffer_v1_literal(
                &output, "\n        ", error, error_size) ||
            !langdef_semantic_render_q_term_v1(
                &output, premise, 0u, error, error_size))
            goto done;
    }
    if (!langdef_byte_buffer_v1_literal(
            &output, "))\n", error, error_size))
        goto done;
    rule->text = (char *)output.bytes;
    rule->text_len = output.len;
    output.bytes = NULL;
    output.len = 0u;
    output.cap = 0u;
    ok = true;

done:
    if (!ok && rule) {
        free(rule->id);
        memset(rule, 0, sizeof(*rule));
    }
    langdef_byte_buffer_v1_free(&output);
    return ok;
}

static bool langdef_operator_collect(
    const Atom *root, LangDefOperatorV1 **operators,
    size_t *operator_len, size_t *operator_cap,
    char *error, size_t error_size) {
    const Atom **work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    bool ok = false;

#define PUSH_TERM(value) do {                                            \
        if (work_len == work_cap) {                                      \
            size_t next_cap = work_cap ? work_cap * 2u : 128u;           \
            const Atom **next;                                           \
            if (next_cap < work_cap ||                                   \
                next_cap > SIZE_MAX / sizeof(*work)) {                   \
                set_error(error, error_size,                             \
                          "ground fact term is too large");             \
                goto done;                                               \
            }                                                            \
            next = realloc(work, next_cap * sizeof(*work));              \
            if (!next) {                                                 \
                set_error(error, error_size,                             \
                          "out of memory scanning ground facts");       \
                goto done;                                               \
            }                                                            \
            work = next;                                                 \
            work_cap = next_cap;                                         \
        }                                                                \
        work[work_len++] = (value);                                      \
    } while (0)

    PUSH_TERM(root);
    while (work_len > 0u) {
        const Atom *term = work[--work_len];
        if (!term || term->kind != ATOM_EXPR)
            continue;
        if (term->expr.len == 0u ||
            !term->expr.elems[0] ||
            term->expr.elems[0]->kind != ATOM_SYMBOL) {
            set_error(error, error_size,
                      "ground fact has a non-symbol operator");
            goto done;
        }
        {
            uint8_t *name = NULL;
            size_t name_len = 0u;
            LangDefOperatorV1 *next;
            size_t next_cap;
            SymbolId symbol_id = term->expr.elems[0]->sym_id;
            CettaExprLen arity = term->expr.len - 1u;
            bool already_collected = false;
            for (size_t index = 0u; index < *operator_len; index++) {
                if ((*operators)[index].symbol_id == symbol_id &&
                    (*operators)[index].arity == arity) {
                    already_collected = true;
                    break;
                }
            }
            if (already_collected)
                goto push_arguments;
            if (!fh_ground_term_v1_render(
                    term->expr.elems[0], &name, &name_len,
                    error, error_size))
                goto done;
            if (*operator_len == *operator_cap) {
                next_cap = *operator_cap ? *operator_cap * 2u : 128u;
                if (next_cap < *operator_cap ||
                    next_cap > SIZE_MAX / sizeof(**operators)) {
                    free(name);
                    set_error(error, error_size,
                              "ground-fact operator inventory is too large");
                    goto done;
                }
                next = realloc(*operators,
                               next_cap * sizeof(**operators));
                if (!next) {
                    free(name);
                    set_error(error, error_size,
                              "out of memory collecting operators");
                    goto done;
                }
                *operators = next;
                *operator_cap = next_cap;
            }
            (*operators)[(*operator_len)++] = (LangDefOperatorV1){
                .name = (char *)name,
                .name_len = name_len,
                .arity = arity,
                .symbol_id = symbol_id,
            };
        }
push_arguments:
        for (CettaExprIndex index = 1u;
             index < term->expr.len; index++)
            PUSH_TERM(term->expr.elems[index]);
    }
    ok = true;

done:
#undef PUSH_TERM
    free(work);
    return ok;
}

typedef struct {
    char digest[65];
    Atom *label;
} LangDefSourceLabelV1;

static int source_label_compare(const void *left, const void *right) {
    const LangDefSourceLabelV1 *lhs = left;
    const LangDefSourceLabelV1 *rhs = right;
    return strcmp(lhs->digest, rhs->digest);
}

static Atom *pack_production_label(const PPABIV1Production *production) {
    if (!production ||
        !cetta_langdef_expr_head(
            production->identity, "pp-production", 4u))
        return NULL;
    return production->identity->expr.elems[1];
}

static bool atom_digest_symbol(const Atom *term, char out[65]) {
    const char *bytes;
    uint32_t len;
    if (!term || term->kind != ATOM_SYMBOL)
        return false;
    bytes = symbol_bytes(g_symbols, term->sym_id);
    len = symbol_len(g_symbols, term->sym_id);
    if (!bytes || len != 64u)
        return false;
    for (uint32_t index = 0u; index < 64u; index++) {
        char ch = bytes[index];
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f')))
            return false;
    }
    memcpy(out, bytes, 64u);
    out[64] = '\0';
    return true;
}

static bool decode_digest_byte(char high, char low, uint8_t *out) {
    int left;
    int right;
    if (high >= '0' && high <= '9')
        left = high - '0';
    else if (high >= 'a' && high <= 'f')
        left = high - 'a' + 10;
    else
        return false;
    if (low >= '0' && low <= '9')
        right = low - '0';
    else if (low >= 'a' && low <= 'f')
        right = low - 'a' + 10;
    else
        return false;
    *out = (uint8_t)((left << 4) | right);
    return true;
}

static Atom *source_label_find(const LangDefSourceLabelV1 *labels,
                               size_t label_len,
                               const char digest[65]) {
    size_t begin = 0u;
    size_t end = label_len;
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2u;
        int order = strcmp(digest, labels[middle].digest);
        if (order < 0)
            end = middle;
        else if (order > 0)
            begin = middle + 1u;
        else
            return labels[middle].label;
    }
    return NULL;
}

static bool collect_source_occurrence_facts(
    const PPABIV1Wire *normalized_wire,
    const PPABIV1Wire *source_wire,
    const PPABIV1Pack *source_pack,
    LangDefCanonicalAnswerV1 **facts,
    size_t *facts_len, size_t *facts_cap,
    LangDefOperatorV1 **operators,
    size_t *operator_len, size_t *operator_cap,
    char *error, size_t error_size) {
    static const uint8_t trace_domain[] =
        "ParserPackTransparentInlineTraceV1\0";
    LangDefSourceLabelV1 *labels = NULL;
    Arena arena;
    bool ok = false;

    arena_init(&arena);
    labels = calloc(
        source_pack->production_len ? source_pack->production_len : 1u,
        sizeof(*labels));
    if (!labels) {
        set_error(error, error_size,
                  "out of memory indexing ParserPack source labels");
        goto done;
    }
    for (uint32_t index = 0u;
         index < source_pack->production_len; index++) {
        Atom *label = pack_production_label(
            &source_pack->productions[index]);
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!label ||
            !fh_ground_term_v1_render(
                label, &canonical, &canonical_len, error, error_size)) {
            free(canonical);
            if (error && error[0] == '\0')
                set_error(error, error_size,
                          "source ParserPack production has no label");
            goto done;
        }
        cetta_native_sha256_hex(
            canonical, canonical_len, labels[index].digest);
        labels[index].label = label;
        free(canonical);
    }
    qsort(labels, source_pack->production_len,
          sizeof(*labels), source_label_compare);
    for (uint32_t index = 1u;
         index < source_pack->production_len; index++) {
        if (strcmp(labels[index - 1u].digest,
                   labels[index].digest) == 0) {
            set_error(error, error_size,
                      "source ParserPack label digest is not unique");
            goto done;
        }
    }
    for (size_t derivation_index = 0u;
         derivation_index < normalized_wire->derivation_len;
         derivation_index++) {
        PPABIV1DerivationInput *derivation =
            &normalized_wire->derivations[derivation_index];
        Atom *artifact;
        Atom *normalized_label;
        Atom *certificate;
        Atom *trace;
        const Atom **work = NULL;
        size_t work_len = 0u;
        size_t work_cap = 0u;
        uint32_t occurrence_index = 0u;
        CettaNativeSha256 trace_sha;
        char expected_trace_digest[65];
        char certificate_trace_digest[65];
        char certificate_source_digest[65];
        char certificate_compiler_digest[65];
        char certificate_environment_digest[65];
        char certificate_source_pack[65];
        char normalized_label_digest[65];
        int64_t certified_len;

        if (derivation->kind != PPABI_V1_EVIDENCE_PRODUCTION)
            continue;
        artifact = derivation->artifact;
        certificate = derivation->certificate;
        if (!cetta_langdef_expr_head(artifact, "pp-production", 4u) ||
            !cetta_langdef_expr_head(
                certificate,
                "pp-transparent-inline-certificate-v1", 7u) ||
            !atom_digest_symbol(
                certificate->expr.elems[1],
                certificate_source_digest) ||
            strcmp(certificate_source_digest,
                   source_wire->source_digest) != 0 ||
            !atom_digest_symbol(
                certificate->expr.elems[2],
                certificate_compiler_digest) ||
            strcmp(certificate_compiler_digest,
                   source_wire->compiler_digest) != 0 ||
            !atom_digest_symbol(
                certificate->expr.elems[3],
                certificate_environment_digest) ||
            strcmp(certificate_environment_digest,
                   source_wire->environment_digest) != 0 ||
            !atom_digest_symbol(
                certificate->expr.elems[4],
                certificate_source_pack) ||
            strcmp(certificate_source_pack,
                   source_pack->pack_digest) != 0 ||
            certificate->expr.elems[5]->kind != ATOM_GROUNDED ||
            certificate->expr.elems[5]->ground.gkind != GV_INT ||
            certificate->expr.elems[5]->ground.ival <= 0 ||
            !atom_digest_symbol(
                certificate->expr.elems[6],
                certificate_trace_digest)) {
            set_error(error, error_size,
                      "normalized ParserPack has an invalid source fiber");
            free(work);
            goto done;
        }
        certified_len = certificate->expr.elems[5]->ground.ival;
        normalized_label = artifact->expr.elems[1];
        if (!cetta_langdef_expr_head(
                normalized_label, "pp-inline-label", 1u) ||
            !atom_digest_symbol(
                normalized_label->expr.elems[1],
                normalized_label_digest) ||
            strcmp(normalized_label_digest,
                   certificate_trace_digest) != 0) {
            set_error(error, error_size,
                      "normalized label differs from its source fiber");
            free(work);
            goto done;
        }
        trace = certificate->expr.elems[7];
        cetta_native_sha256_init(&trace_sha);
        cetta_native_sha256_update(
            &trace_sha, trace_domain, sizeof(trace_domain) - 1u);

#define PUSH_TRACE(value) do {                                           \
            if (work_len == work_cap) {                                  \
                size_t next_cap = work_cap ? work_cap * 2u : 32u;        \
                const Atom **next;                                       \
                if (next_cap < work_cap ||                               \
                    next_cap > SIZE_MAX / sizeof(*work)) {               \
                    set_error(error, error_size,                         \
                              "source fiber is too large");              \
                    free(work);                                          \
                    goto done;                                           \
                }                                                        \
                next = realloc(work, next_cap * sizeof(*work));          \
                if (!next) {                                             \
                    set_error(error, error_size,                         \
                              "out of memory reading source fiber");     \
                    free(work);                                          \
                    goto done;                                           \
                }                                                        \
                work = next;                                             \
                work_cap = next_cap;                                     \
            }                                                            \
            work[work_len++] = (value);                                  \
        } while (0)

        PUSH_TRACE(trace);
        while (work_len > 0u) {
            const Atom *part = work[--work_len];
            if (atom_is_symbol((Atom *)part, "pp-inline-trace-nil"))
                continue;
            if (cetta_langdef_expr_head(
                    part, "pp-inline-trace-append", 2u)) {
                PUSH_TRACE(part->expr.elems[2]);
                PUSH_TRACE(part->expr.elems[1]);
                continue;
            }
            if (cetta_langdef_expr_head(
                    part, "pp-inline-trace-one", 1u) &&
                cetta_langdef_expr_head(
                    part->expr.elems[1],
                    "pp-inline-source-occurrence", 1u)) {
                char source_digest[65];
                uint8_t binary[32];
                const uint8_t digest_len[8] =
                    {0u, 0u, 0u, 0u, 0u, 0u, 0u, 32u};
                Atom *source_label;
                Atom *fact;
                uint8_t *canonical = NULL;
                size_t canonical_len = 0u;
                if (!atom_digest_symbol(
                        part->expr.elems[1]->expr.elems[1],
                        source_digest)) {
                    set_error(error, error_size,
                              "source occurrence has an invalid digest");
                    free(work);
                    goto done;
                }
                for (uint32_t byte = 0u; byte < 32u; byte++) {
                    if (!decode_digest_byte(
                            source_digest[byte * 2u],
                            source_digest[byte * 2u + 1u],
                            &binary[byte])) {
                        set_error(error, error_size,
                                  "source occurrence digest is malformed");
                        free(work);
                        goto done;
                    }
                }
                cetta_native_sha256_update(
                    &trace_sha, digest_len, sizeof(digest_len));
                cetta_native_sha256_update(
                    &trace_sha, binary, sizeof(binary));
                source_label = source_label_find(
                    labels, source_pack->production_len, source_digest);
                fact = source_label ? atom_expr(
                    &arena,
                    (Atom *[]){
                        atom_symbol(
                            &arena,
                            "compile-pack-source-occurrence"),
                        normalized_label,
                        source_label,
                        atom_int(&arena, occurrence_index),
                    },
                    4u) : NULL;
                if (!source_label || !fact ||
                    !fh_ground_term_v1_render(
                        fact, &canonical, &canonical_len,
                        error, error_size) ||
                    !langdef_operator_collect(
                        fact, operators, operator_len, operator_cap,
                        error, error_size) ||
                    !canonical_answer_push(
                        facts, facts_len, facts_cap,
                        (char *)canonical, canonical_len,
                        error, error_size)) {
                    free(canonical);
                    if (error && error[0] == '\0')
                        set_error(
                            error, error_size,
                            "source occurrence has no source label");
                    free(work);
                    goto done;
                }
                occurrence_index++;
                continue;
            }
            set_error(error, error_size,
                      "normalized ParserPack has a malformed source trace");
            free(work);
            goto done;
        }
#undef PUSH_TRACE
        free(work);
        cetta_native_sha256_finish_hex(
            &trace_sha, expected_trace_digest);
        if ((int64_t)occurrence_index != certified_len ||
            strcmp(expected_trace_digest,
                   certificate_trace_digest) != 0) {
            set_error(error, error_size,
                      "normalized ParserPack source fiber changed");
            goto done;
        }
    }
    ok = true;

done:
    free(labels);
    arena_free(&arena);
    return ok;
}

static bool compile_pack_facts(const char *abi_path,
                               const char *source_abi_path,
                               const char *output_path_value,
                               size_t *fact_len,
                               char pack_digest[65],
                               char *error, size_t error_size) {
    static const char header_prefix[] =
        "; generated ParserPack facts for staged compiler GSLTs\n"
        "(gslt-presentation-v1 GeneratedParserPackFactsV1-";
    static const char header_suffix[] =
        "\n  (signature\n"
        "  )\n"
        "  (equations)\n"
        "  (rewrites\n";
    static const char footer[] = "  ))\n";
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    PPABIV1Wire source_wire;
    PPABIV1Pack source_pack;
    LangDefCanonicalAnswerV1 *facts = NULL;
    size_t facts_len = 0u;
    size_t facts_cap = 0u;
    size_t production_fact_len = 0u;
    LangDefOperatorV1 *operators = NULL;
    size_t operator_len = 0u;
    size_t operator_cap = 0u;
    size_t payload_len;
    size_t offset;
    uint8_t *payload = NULL;
    FHGSLTPackage *checked = NULL;
    FHGSLTInput input;
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    ppabi_v1_wire_init(&source_wire);
    ppabi_v1_pack_init(&source_pack);
    if (!abi_path || !output_path_value || !fact_len || !pack_digest ||
        !ppabi_v1_wire_read(&wire, abi_path, error, error_size) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, error_size))
        goto done;
    for (size_t index = 0u; index < wire.derivation_len; index++) {
        PPABIV1DerivationInput *derivation = &wire.derivations[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (derivation->kind != PPABI_V1_EVIDENCE_PRODUCTION)
            continue;
        if (!cetta_langdef_expr_head(
                derivation->answer, "compile-pack-production", 2u) ||
            !atom_eq(derivation->answer->expr.elems[2],
                     derivation->artifact) ||
            !fh_ground_term_v1_render(
                derivation->answer, &canonical, &canonical_len,
                error, error_size) ||
            !langdef_operator_collect(
                derivation->answer, &operators,
                &operator_len, &operator_cap,
                error, error_size) ||
            !canonical_answer_push(
                &facts, &facts_len, &facts_cap,
                (char *)canonical, canonical_len,
                error, error_size)) {
            free(canonical);
            if (error && error[0] == '\0')
                set_error(error, error_size,
                          "ParserPack has invalid production evidence");
            goto done;
        }
    }
    production_fact_len = facts_len;
    if (production_fact_len != pack.production_len ||
        production_fact_len == 0u) {
        set_error(error, error_size,
                  "ParserPack facts do not cover every production");
        goto done;
    }
    if (source_abi_path &&
        (!ppabi_v1_wire_read(
             &source_wire, source_abi_path, error, error_size) ||
         !ppabi_v1_wire_load_pack(
             &source_wire, &source_pack, error, error_size) ||
         !collect_source_occurrence_facts(
             &wire, &source_wire, &source_pack,
             &facts, &facts_len, &facts_cap,
             &operators, &operator_len, &operator_cap,
             error, error_size)))
        goto done;
    qsort(facts, facts_len, sizeof(*facts), canonical_answer_compare);
    qsort(operators, operator_len, sizeof(*operators),
          langdef_operator_compare);
    {
        size_t write = 0u;
        for (size_t read = 0u; read < operator_len; read++) {
            if (write > 0u &&
                strcmp(operators[write - 1u].name,
                       operators[read].name) == 0 &&
                operators[write - 1u].arity == operators[read].arity) {
                free(operators[read].name);
                continue;
            }
            if (write != read)
                operators[write] = operators[read];
            write++;
        }
        operator_len = write;
    }
    payload_len = sizeof(header_prefix) - 1u + 64u +
                  sizeof(header_suffix) - 1u + sizeof(footer) - 1u;
    for (size_t index = 0u; index < operator_len; index++) {
        char arity[32];
        int arity_len = snprintf(
            arity, sizeof(arity), "%" PRIu64,
            (uint64_t)operators[index].arity);
        size_t framing_len =
            sizeof("    (operator  )\n") - 1u;
        if (arity_len < 0 || (size_t)arity_len >= sizeof(arity) ||
            operators[index].name_len >
                SIZE_MAX - payload_len - framing_len - (size_t)arity_len) {
            set_error(error, error_size,
                      "ParserPack signature is too large");
            goto done;
        }
        payload_len += framing_len + operators[index].name_len +
                       (size_t)arity_len;
    }
    for (size_t index = 0u; index < facts_len; index++) {
        static const size_t framing_len =
            sizeof("    (rule parser-pack-fact-") - 1u + 64u +
            sizeof("\n      (head ") - 1u +
            sizeof(")\n      (body))\n") - 1u;
        if ((index > 0u &&
             strcmp(facts[index - 1u].text, facts[index].text) == 0) ||
            facts[index].len > SIZE_MAX - payload_len - framing_len) {
            set_error(error, error_size,
                      index > 0u &&
                      strcmp(facts[index - 1u].text,
                             facts[index].text) == 0
                          ? "ParserPack repeats a compiled production fact"
                          : "ParserPack fact presentation is too large");
            goto done;
        }
        payload_len += framing_len + facts[index].len;
    }
    payload = malloc(payload_len + 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory serializing ParserPack facts");
        goto done;
    }
    offset = 0u;
#define APPEND_LITERAL(value) do {                                      \
        const char *append_text = (value);                              \
        size_t append_len = strlen(append_text);                        \
        memcpy(payload + offset, append_text, append_len);              \
        offset += append_len;                                           \
    } while (0)
    APPEND_LITERAL(header_prefix);
    memcpy(payload + offset, pack.pack_digest, 64u);
    offset += 64u;
    APPEND_LITERAL("\n  (signature\n");
    for (size_t index = 0u; index < operator_len; index++) {
        char arity[32];
        int arity_len = snprintf(
            arity, sizeof(arity), "%" PRIu64,
            (uint64_t)operators[index].arity);
        APPEND_LITERAL("    (operator ");
        memcpy(payload + offset, operators[index].name,
               operators[index].name_len);
        offset += operators[index].name_len;
        APPEND_LITERAL(" ");
        memcpy(payload + offset, arity, (size_t)arity_len);
        offset += (size_t)arity_len;
        APPEND_LITERAL(")\n");
    }
    APPEND_LITERAL("  )\n  (equations)\n  (rewrites\n");
    for (size_t index = 0u; index < facts_len; index++) {
        char digest[65];
        APPEND_LITERAL("    (rule parser-pack-fact-");
        cetta_native_sha256_hex(
            (const uint8_t *)facts[index].text,
            facts[index].len, digest);
        memcpy(payload + offset, digest, 64u);
        offset += 64u;
        APPEND_LITERAL("\n      (head ");
        memcpy(payload + offset, facts[index].text, facts[index].len);
        offset += facts[index].len;
        APPEND_LITERAL(")\n      (body))\n");
    }
    APPEND_LITERAL(footer);
#undef APPEND_LITERAL
    if (offset != payload_len) {
        set_error(error, error_size,
                  "ParserPack fact serializer changed its size");
        goto done;
    }
    payload[payload_len] = '\0';
    input = (FHGSLTInput){
        .bytes = payload,
        .len = payload_len,
        .source = "generated ParserPack facts",
    };
    if (!fhgslt_package_from_inputs(
            &input, 1u, &checked, error, error_size) ||
        fhgslt_package_presentation_count(checked) != 1u ||
        fhgslt_package_rule_count(checked) != facts_len ||
        !write_atomic(output_path_value, payload, payload_len,
                      error, error_size)) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "generated ParserPack facts failed validation");
        goto done;
    }
    *fact_len = facts_len;
    memcpy(pack_digest, pack.pack_digest, 65u);
    ok = true;

done:
    for (size_t index = 0u; index < facts_len; index++)
        free(facts[index].text);
    for (size_t index = 0u; index < operator_len; index++)
        free(operators[index].name);
    free(facts);
    free(operators);
    free(payload);
    fhgslt_package_free(checked);
    ppabi_v1_pack_free(&source_pack);
    ppabi_v1_wire_free(&source_wire);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

static bool compile_answer_facts_presentation(
    const char *source_path,
    const char *output_path_value,
    size_t *fact_len,
    char answer_digest[65],
    char *error,
    size_t error_size) {
    static const char header_prefix[] =
        "; generated finite-Horn facts from a canonical answer stream\n"
        "(gslt-presentation-v1 GeneratedAnswerFactsV1_";
    static const char header_suffix[] =
        "\n  (signature\n";
    static const char body_prefix[] =
        "  )\n"
        "  (equations)\n"
        "  (rewrites\n";
    static const char footer[] = "  ))\n";
    FHAnswerStreamV1 stream;
    LangDefCanonicalAnswerV1 *facts = NULL;
    LangDefOperatorV1 *operators = NULL;
    size_t operator_len = 0u;
    size_t operator_cap = 0u;
    size_t payload_len;
    size_t offset = 0u;
    uint8_t *payload = NULL;
    FHGSLTPackage *checked = NULL;
    FHGSLTInput input;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    if (!source_path || !output_path_value || !fact_len || !answer_digest ||
        !fh_answer_stream_v1_read(
            &stream, source_path, error, error_size) ||
        stream.len == 0u)
        goto done;
    if (stream.len > SIZE_MAX / sizeof(*facts)) {
        set_error(error, error_size, "answer fact family is too large");
        goto done;
    }
    facts = calloc(stream.len, sizeof(*facts));
    if (!facts) {
        set_error(error, error_size,
                  "out of memory reifying finite-Horn answers");
        goto done;
    }
    for (size_t index = 0u; index < stream.len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!fh_ground_term_v1_render(
                stream.terms[index], &canonical, &canonical_len,
                error, error_size) ||
            !langdef_operator_collect(
                stream.terms[index], &operators,
                &operator_len, &operator_cap,
                error, error_size)) {
            free(canonical);
            goto done;
        }
        facts[index] = (LangDefCanonicalAnswerV1){
            .text = (char *)canonical,
            .len = canonical_len,
        };
    }
    qsort(operators, operator_len, sizeof(*operators),
          langdef_operator_compare);
    {
        size_t write = 0u;
        for (size_t read = 0u; read < operator_len; read++) {
            if (write > 0u &&
                strcmp(operators[write - 1u].name,
                       operators[read].name) == 0 &&
                operators[write - 1u].arity == operators[read].arity) {
                free(operators[read].name);
                continue;
            }
            if (write != read)
                operators[write] = operators[read];
            write++;
        }
        operator_len = write;
    }

    payload_len = sizeof(header_prefix) - 1u + 64u +
                  sizeof(header_suffix) - 1u +
                  sizeof(body_prefix) - 1u + sizeof(footer) - 1u;
    for (size_t index = 0u; index < operator_len; index++) {
        char arity[32];
        int arity_len = snprintf(
            arity, sizeof(arity), "%" PRIu64,
            (uint64_t)operators[index].arity);
        size_t framing_len = sizeof("    (operator  )\n") - 1u;
        if (arity_len < 0 || (size_t)arity_len >= sizeof(arity) ||
            operators[index].name_len >
                SIZE_MAX - payload_len - framing_len - (size_t)arity_len) {
            set_error(error, error_size,
                      "answer-fact signature is too large");
            goto done;
        }
        payload_len += framing_len + operators[index].name_len +
                       (size_t)arity_len;
    }
    for (size_t index = 0u; index < stream.len; index++) {
        static const size_t framing_len =
            sizeof("    (rule answer-fact-") - 1u + 64u +
            sizeof("\n      (head ") - 1u +
            sizeof(")\n      (body))\n") - 1u;
        if (facts[index].len > SIZE_MAX - payload_len - framing_len) {
            set_error(error, error_size,
                      "answer-fact presentation is too large");
            goto done;
        }
        payload_len += framing_len + facts[index].len;
    }
    payload = malloc(payload_len + 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory serializing answer facts");
        goto done;
    }

#define APPEND_ANSWER_LITERAL(value) do {                               \
        const char *append_text = (value);                              \
        size_t append_len = strlen(append_text);                        \
        memcpy(payload + offset, append_text, append_len);              \
        offset += append_len;                                           \
    } while (0)
    APPEND_ANSWER_LITERAL(header_prefix);
    memcpy(payload + offset, stream.digest, 64u);
    offset += 64u;
    APPEND_ANSWER_LITERAL(header_suffix);
    for (size_t index = 0u; index < operator_len; index++) {
        char arity[32];
        int arity_len = snprintf(
            arity, sizeof(arity), "%" PRIu64,
            (uint64_t)operators[index].arity);
        APPEND_ANSWER_LITERAL("    (operator ");
        memcpy(payload + offset, operators[index].name,
               operators[index].name_len);
        offset += operators[index].name_len;
        APPEND_ANSWER_LITERAL(" ");
        memcpy(payload + offset, arity, (size_t)arity_len);
        offset += (size_t)arity_len;
        APPEND_ANSWER_LITERAL(")\n");
    }
    APPEND_ANSWER_LITERAL(body_prefix);
    for (size_t index = 0u; index < stream.len; index++) {
        char digest[65];
        APPEND_ANSWER_LITERAL("    (rule answer-fact-");
        cetta_native_sha256_hex(
            (const uint8_t *)facts[index].text,
            facts[index].len, digest);
        memcpy(payload + offset, digest, 64u);
        offset += 64u;
        APPEND_ANSWER_LITERAL("\n      (head ");
        memcpy(payload + offset, facts[index].text, facts[index].len);
        offset += facts[index].len;
        APPEND_ANSWER_LITERAL(")\n      (body))\n");
    }
    APPEND_ANSWER_LITERAL(footer);
#undef APPEND_ANSWER_LITERAL
    if (offset != payload_len) {
        set_error(error, error_size,
                  "answer-fact serializer changed its size");
        goto done;
    }
    payload[payload_len] = '\0';
    input = (FHGSLTInput){
        .bytes = payload,
        .len = payload_len,
        .source = "generated finite-Horn answer facts",
    };
    if (!fhgslt_package_from_inputs(
            &input, 1u, &checked, error, error_size) ||
        fhgslt_package_presentation_count(checked) != 1u ||
        fhgslt_package_rule_count(checked) != stream.len ||
        !write_atomic(output_path_value, payload, payload_len,
                      error, error_size)) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "generated answer facts failed validation");
        goto done;
    }
    *fact_len = stream.len;
    memcpy(answer_digest, stream.digest, sizeof(stream.digest));
    ok = true;

done:
    if (facts) {
        for (size_t index = 0u; index < stream.len; index++)
            free(facts[index].text);
    }
    for (size_t index = 0u; index < operator_len; index++)
        free(operators[index].name);
    free(facts);
    free(operators);
    free(payload);
    fhgslt_package_free(checked);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

static bool compile_semantic_gslt_presentation_v1(
    const char *source_path,
    const char *output_path_value,
    size_t *operator_count,
    size_t *rule_count,
    char output_digest[65],
    char *error,
    size_t error_size) {
    static const char header_prefix[] =
        "; generated executable finite-Horn GSLT from a checked quotation\n"
        "; quotation-answer-set-sha256 ";
    static const char presentation_prefix[] =
        "\n(gslt-presentation-v1 GeneratedSemanticGSLTV1_";
    static const char signature_prefix[] = "\n  (signature\n";
    static const char rewrite_prefix[] =
        "  )\n"
        "  (equations)\n"
        "  (rewrites\n";
    static const char footer[] = "  ))\n";
    FHAnswerStreamV1 stream;
    LangDefSemanticOperatorV1 *operators = NULL;
    LangDefSemanticRuleV1 *rules = NULL;
    size_t operators_len = 0u;
    size_t emitted_operator_len = 0u;
    size_t rules_len = 0u;
    Atom *owner = NULL;
    LangDefByteBufferV1 payload = {0};
    FHGSLTPackage *checked = NULL;
    FHGSLTInput input;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    if (!source_path || !output_path_value || !operator_count ||
        !rule_count || !output_digest ||
        !fh_answer_stream_v1_read(
            &stream, source_path, error, error_size) ||
        stream.len == 0u ||
        stream.len > SIZE_MAX / sizeof(*operators) ||
        stream.len > SIZE_MAX / sizeof(*rules)) {
        if (error && error_size > 0u && error[0] == '\0')
            set_error(error, error_size,
                      "semantic quotation must be a nonempty answer stream");
        goto done;
    }
    operators = calloc(stream.len, sizeof(*operators));
    rules = calloc(stream.len, sizeof(*rules));
    if (!operators || !rules) {
        set_error(error, error_size,
                  "out of memory decoding semantic quotation");
        goto done;
    }
    for (size_t index = 0u; index < stream.len; index++) {
        Atom *record = stream.terms[index];
        Atom *record_owner;

        if (!record || record->kind != ATOM_EXPR ||
            record->expr.len < 2u) {
            set_error(error, error_size,
                      "semantic quotation record %zu has the wrong shape",
                      index + 1u);
            goto done;
        }
        record_owner = record->expr.elems[1];
        if (!record_owner || record_owner->kind != ATOM_SYMBOL) {
            set_error(error, error_size,
                      "semantic quotation record %zu has an invalid owner",
                      index + 1u);
            goto done;
        }
        if (!owner)
            owner = record_owner;
        else if (!atom_eq(owner, record_owner)) {
            set_error(error, error_size,
                      "semantic quotation contains multiple owners");
            goto done;
        }
        if (atom_is_symbol(record->expr.elems[0], "source-operator")) {
            const Atom *quoted_name;
            const Atom *name;
            uint32_t arity;
            if (record->expr.len != 4u) {
                set_error(
                    error, error_size,
                    "semantic operator record %zu has the wrong arity",
                    index + 1u);
                goto done;
            }
            quoted_name = record->expr.elems[2];
            if (!langdef_semantic_q_symbol_v1(
                    quoted_name, &name, error, error_size) ||
                !langdef_semantic_q_nat_v1(
                    record->expr.elems[3], &arity,
                    error, error_size) ||
                !langdef_semantic_symbol_text_v1(
                    name, &operators[operators_len].name,
                    &operators[operators_len].name_len,
                    error, error_size))
                goto done;
            operators[operators_len].arity = arity;
            operators_len++;
        } else if (atom_is_symbol(
                       record->expr.elems[0], "source-rule")) {
            if (record->expr.len != 3u ||
                !langdef_semantic_rule_build_v1(
                    record->expr.elems[2], &rules[rules_len],
                    error, error_size)) {
                if (error && error_size > 0u && error[0] == '\0')
                    set_error(
                        error, error_size,
                        "semantic rule record %zu has the wrong shape",
                        index + 1u);
                goto done;
            }
            rules_len++;
        } else {
            set_error(error, error_size,
                      "semantic quotation record %zu has an unknown kind",
                      index + 1u);
            goto done;
        }
    }
    if (operators_len == 0u) {
        set_error(error, error_size,
                  "semantic quotation declares no operators");
        goto done;
    }
    qsort(operators, operators_len, sizeof(*operators),
          langdef_semantic_operator_compare_v1);
    qsort(rules, rules_len, sizeof(*rules),
          langdef_semantic_rule_compare_v1);
    for (size_t index = 1u; index < operators_len; index++) {
        if (strcmp(operators[index - 1u].name,
                   operators[index].name) == 0) {
            set_error(
                error, error_size,
                operators[index - 1u].arity == operators[index].arity
                    ? "semantic quotation repeats an operator"
                    : "semantic quotation assigns two arities to an operator");
            goto done;
        }
    }
    for (size_t index = 1u; index < rules_len; index++) {
        if (strcmp(rules[index - 1u].id, rules[index].id) == 0) {
            set_error(error, error_size,
                      "semantic quotation repeats a rule identifier");
            goto done;
        }
    }
    if (!langdef_byte_buffer_v1_literal(
            &payload, header_prefix, error, error_size) ||
        !langdef_byte_buffer_v1_append(
            &payload, stream.digest, 64u, error, error_size) ||
        !langdef_byte_buffer_v1_literal(
            &payload, presentation_prefix, error, error_size) ||
        !langdef_byte_buffer_v1_append(
            &payload, stream.digest, 64u, error, error_size) ||
        !langdef_byte_buffer_v1_literal(
            &payload, signature_prefix, error, error_size))
        goto done;
    for (size_t index = 0u; index < operators_len; index++) {
        char arity[32];
        if (operators[index].arity == 0u)
            continue;
        int arity_len = snprintf(
            arity, sizeof(arity), "%" PRIu32, operators[index].arity);
        if (arity_len <= 0 || (size_t)arity_len >= sizeof(arity) ||
            !langdef_byte_buffer_v1_literal(
                &payload, "    (operator ", error, error_size) ||
            !langdef_byte_buffer_v1_append(
                &payload, operators[index].name,
                operators[index].name_len, error, error_size) ||
            !langdef_byte_buffer_v1_literal(
                &payload, " ", error, error_size) ||
            !langdef_byte_buffer_v1_append(
                &payload, arity, (size_t)arity_len,
                error, error_size) ||
            !langdef_byte_buffer_v1_literal(
                &payload, ")\n", error, error_size))
            goto done;
        emitted_operator_len++;
    }
    if (!langdef_byte_buffer_v1_literal(
            &payload, rewrite_prefix, error, error_size))
        goto done;
    for (size_t index = 0u; index < rules_len; index++) {
        if (!langdef_byte_buffer_v1_append(
                &payload, rules[index].text, rules[index].text_len,
                error, error_size))
            goto done;
    }
    if (!langdef_byte_buffer_v1_literal(
            &payload, footer, error, error_size))
        goto done;

    input = (FHGSLTInput){
        .bytes = payload.bytes,
        .len = payload.len,
        .source = "generated executable semantic GSLT",
    };
    if (!fhgslt_package_from_inputs(
            &input, 1u, &checked, error, error_size) ||
        fhgslt_package_presentation_count(checked) != 1u ||
        fhgslt_package_operator_count(checked) != emitted_operator_len ||
        fhgslt_package_rule_count(checked) != rules_len) {
        if (error && error_size > 0u && error[0] == '\0')
            set_error(error, error_size,
                      "generated semantic GSLT failed validation");
        goto done;
    }
    cetta_native_sha256_hex(
        payload.bytes, payload.len, output_digest);
    if (!write_atomic(
            output_path_value, payload.bytes, payload.len,
            error, error_size))
        goto done;
    *operator_count = emitted_operator_len;
    *rule_count = rules_len;
    ok = true;

done:
    if (operators) {
        for (size_t index = 0u; index < operators_len; index++)
            free(operators[index].name);
    }
    if (rules) {
        for (size_t index = 0u; index < rules_len; index++) {
            free(rules[index].id);
            free(rules[index].text);
        }
    }
    free(operators);
    free(rules);
    fhgslt_package_free(checked);
    langdef_byte_buffer_v1_free(&payload);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

static bool compile_lexical_answer_family(
    const char *chart,
    const char *const *sources, uint32_t source_len,
    const char *const *reflected_sources, uint32_t reflected_source_len,
    const char *root_answer_path, const char *timeout,
    const char *output_path_value,
    size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    static const char query_prefix[] = "(compile-span-nfa ";
    static const char query_suffix[] = " ?edge)";
    FHAnswerStreamV1 roots;
    FHAnswerStreamV1 family;
    FHAnswerStreamV1 published;
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    uint8_t *payload = NULL;
    size_t payload_len = 0u;
    char part_path[PATH_MAX] = {0};
    int part_descriptor = -1;
    bool ok = false;

    fh_answer_stream_v1_init(&roots);
    fh_answer_stream_v1_init(&family);
    fh_answer_stream_v1_init(&published);
    if (error && error_size > 0u)
        error[0] = '\0';
    if (!chart || !sources || source_len == 0u || !root_answer_path ||
        !timeout || !output_path_value || !answer_len || !answer_digest ||
        !fh_answer_stream_v1_read(&roots, root_answer_path,
                                  error, error_size) ||
        roots.len == 0u) {
        if (error && error_size > 0u && error[0] == '\0')
            set_error(error, error_size,
                      "lexical projection has no compiled roots");
        goto done;
    }
    for (size_t root_index = 0u; root_index < roots.len; root_index++) {
        Atom *record = roots.terms[root_index];
        Atom *tag;
        uint8_t *tag_text = NULL;
        size_t tag_len = 0u;
        char *query = NULL;
        size_t query_len;
        size_t family_len = 0u;
        char family_digest[65];

        if (!cetta_langdef_expr_head(
                record, "compile-lexical-root", 1u)) {
            set_error(error, error_size,
                      "lexical-root compiler emitted an invalid record");
            goto root_done;
        }
        tag = record->expr.elems[1];
        if (!fh_ground_term_v1_render(tag, &tag_text, &tag_len,
                                      error, error_size))
            goto root_done;
        if (tag_len > SIZE_MAX - sizeof(query_prefix) -
                      sizeof(query_suffix)) {
            set_error(error, error_size,
                      "lexical-root query is too large");
            goto root_done;
        }
        query_len = sizeof(query_prefix) - 1u + tag_len +
                    sizeof(query_suffix) - 1u;
        query = malloc(query_len + 1u);
        if (!query) {
            set_error(error, error_size,
                      "out of memory constructing lexical-root query");
            goto root_done;
        }
        memcpy(query, query_prefix, sizeof(query_prefix) - 1u);
        memcpy(query + sizeof(query_prefix) - 1u, tag_text, tag_len);
        memcpy(query + sizeof(query_prefix) - 1u + tag_len,
               query_suffix, sizeof(query_suffix));
        if (snprintf(part_path, sizeof(part_path), "%s.part.XXXXXX",
                     output_path_value) >= (int)sizeof(part_path)) {
            set_error(error, error_size,
                      "lexical answer path is too long");
            goto root_done;
        }
        part_descriptor = mkstemp(part_path);
        if (part_descriptor < 0 || close(part_descriptor) != 0) {
            part_descriptor = -1;
            set_error(error, error_size,
                      "cannot create lexical answer part: %s",
                      strerror(errno));
            goto root_done;
        }
        part_descriptor = -1;
        if (!compile_answer_stream(
                chart, sources, source_len,
                reflected_sources, reflected_source_len,
                query, timeout, part_path,
                &family_len, family_digest,
                error, error_size) || family_len == 0u ||
            !fh_answer_stream_v1_read(&family, part_path,
                                      error, error_size)) {
            if (error && error_size > 0u && error[0] == '\0')
                set_error(error, error_size,
                          "lexical root compiled to no NFA records");
            goto root_done;
        }
        for (size_t answer_index = 0u;
             answer_index < family.len; answer_index++) {
            Atom *answer = family.terms[answer_index];
            uint8_t *canonical = NULL;
            size_t canonical_len = 0u;
            if (!cetta_langdef_expr_head(
                    answer, "compile-span-nfa", 2u) ||
                !atom_eq(answer->expr.elems[1], tag) ||
                !fh_ground_term_v1_render(
                    answer, &canonical, &canonical_len,
                    error, error_size) ||
                !canonical_answer_push(
                    &answers, &answers_len, &answers_cap,
                    (char *)canonical, canonical_len,
                    error, error_size)) {
                free(canonical);
                if (error && error_size > 0u && error[0] == '\0')
                    set_error(error, error_size,
                              "lexical compiler emitted a foreign record");
                goto root_done;
            }
        }
        free(query);
        free(tag_text);
        query = NULL;
        tag_text = NULL;
        fh_answer_stream_v1_free(&family);
        fh_answer_stream_v1_init(&family);
        (void)unlink(part_path);
        part_path[0] = '\0';
        continue;

root_done:
        free(query);
        free(tag_text);
        goto done;
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    for (size_t index = 0u; index < answers_len; index++) {
        if ((index > 0u &&
             strcmp(answers[index - 1u].text, answers[index].text) == 0) ||
            answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      index > 0u &&
                      strcmp(answers[index - 1u].text,
                             answers[index].text) == 0
                          ? "lexical answer families overlap"
                          : "lexical answer family is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory serializing lexical answers");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text,
                   answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output_path_value, payload, payload_len,
                      error, error_size) ||
        !fh_answer_stream_v1_read(&published, output_path_value,
                                  error, error_size) ||
        published.len != answers_len) {
        if (error && error_size > 0u && error[0] == '\0')
            set_error(error, error_size,
                      "published lexical answer stream changed");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, sizeof(published.digest));
    ok = true;

done:
    if (part_descriptor >= 0)
        close(part_descriptor);
    if (part_path[0] != '\0')
        (void)unlink(part_path);
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(answers);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&family);
    fh_answer_stream_v1_free(&roots);
    return ok;
}

static bool compile_equations(const char *source, const char *output,
                              char *error, size_t error_size) {
    const char *paths[1] = {source};
    FHGSLTPackage *package = NULL;
    uint8_t *equations = NULL;
    size_t equation_len = 0u;
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    char package_digest[65];
    char header[256];
    int header_len;
    uint8_t *program = NULL;
    bool ok = false;

    if (!fhgslt_package_from_paths(paths, 1u, &package,
                                   error, error_size) ||
        fhgslt_package_presentation_count(package) != 1u ||
        !fhgslt_package_digest(package, package_digest,
                               error, error_size) ||
        !fhgslt_package_canonical_presentation(
            package, 0u, &canonical, &canonical_len,
            error, error_size) ||
        !cetta_langdef_metta_equations_v1(
            canonical, canonical_len, &equations, &equation_len,
            error, error_size)) {
        if (error[0] == '\0')
            set_error(error, error_size,
                      "equation source must contain one presentation");
        goto done;
    }
    header_len = snprintf(
        header, sizeof(header),
        "; generated from a compositional GSLT equation presentation\n"
        "; source-package-sha256 %s\n"
        "; compiler c-finite-horn-metta-equation-v1\n\n",
        package_digest);
    if (header_len < 0 || (size_t)header_len >= sizeof(header) ||
        equation_len > SIZE_MAX - (size_t)header_len) {
        set_error(error, error_size, "generated program is too large");
        goto done;
    }
    program = malloc((size_t)header_len + equation_len);
    if (program == NULL) {
        set_error(error, error_size,
                  "out of memory constructing generated program");
        goto done;
    }
    memcpy(program, header, (size_t)header_len);
    memcpy(program + (size_t)header_len, equations, equation_len);
    ok = write_atomic(output, program,
                      (size_t)header_len + equation_len,
                      error, error_size);

done:
    free(canonical);
    free(program);
    free(equations);
    fhgslt_package_free(package);
    return ok;
}

static bool direct_sources_from_composition_v1(
    const char *composition_path, Arena *arena,
    char resolved[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX],
    const char **sources, size_t *source_count,
    const char *command, char *error, size_t error_size) {
    Atom *root = cetta_langdef_read_single_form(
        composition_path, arena, error, error_size);
    bool saw_name = false;
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 3u ||
        !atom_is_symbol(root->expr.elems[0], "gslt-composition-v1"))
        return set_error(error, error_size,
                         "%s composition is malformed", command);
    *source_count = 0u;
    for (CettaExprIndex index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char *value = NULL;
        if (cetta_langdef_expr_head(field, "name", 1u)) {
            if (saw_name ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return set_error(
                    error, error_size,
                    "%s composition has an invalid name", command);
            saw_name = true;
        } else if (cetta_langdef_expr_head(field, "source", 1u)) {
            if (*source_count >= CETTA_LANGDEF_MAX_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0' ||
                !output_path(composition_path, value,
                             resolved[*source_count], error, error_size))
                return false;
            for (size_t prior = 0u; prior < *source_count; prior++) {
                if (strcmp(resolved[prior], resolved[*source_count]) == 0)
                    return set_error(
                        error, error_size,
                        "%s composition repeats a source", command);
            }
            sources[*source_count] = resolved[*source_count];
            (*source_count)++;
        } else {
            return set_error(
                error, error_size,
                "%s composition has an unknown field", command);
        }
    }
    if (!saw_name || *source_count == 0u)
        return set_error(
            error, error_size,
            "%s composition requires a name and sources", command);
    return true;
}

static bool compile_petta_direct_command_v1(
    int argc, char **argv, size_t *rule_count,
    char source_digest[65], char artifact_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_count = 0u;
    const char *composition = NULL;
    const char *epilogue = NULL;
    const char *output = NULL;
    const char *entry_modes[CETTA_LANGDEF_MAX_SOURCES];
    size_t entry_mode_count = 0u;
    bool closed_entry_residual = false;
    char resolved_sources[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX];
    Atom *presentations[CETTA_LANGDEF_MAX_SOURCES];
    Arena arena;
    uint8_t *program = NULL;
    size_t program_len = 0u;
    uint8_t *epilogue_bytes = NULL;
    size_t epilogue_len = 0u;
    bool ok = false;

    arena_init(&arena);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "petta-direct option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0) {
            if (source_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "petta-direct has too many sources");
                goto done;
            }
            sources[source_count++] = value;
        } else if (strcmp(option, "--composition") == 0 &&
                   composition == NULL) {
            composition = value;
        } else if (strcmp(option, "--epilogue") == 0 &&
                   epilogue == NULL) {
            epilogue = value;
        } else if (strcmp(option, "--entry-mode") == 0) {
            if (closed_entry_residual) {
                set_error(error, error_size,
                          "petta-direct cannot mix open and closed entry modes");
                goto done;
            }
            if (entry_mode_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "petta-direct has too many entry modes");
                goto done;
            }
            entry_modes[entry_mode_count++] = value;
        } else if (strcmp(option, "--closed-entry-mode") == 0) {
            if (entry_mode_count > 0u && !closed_entry_residual) {
                set_error(error, error_size,
                          "petta-direct cannot mix open and closed entry modes");
                goto done;
            }
            closed_entry_residual = true;
            if (entry_mode_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "petta-direct has too many entry modes");
                goto done;
            }
            entry_modes[entry_mode_count++] = value;
        } else if (strcmp(option, "--out") == 0 && output == NULL) {
            output = value;
        } else {
            set_error(error, error_size,
                      "invalid or repeated petta-direct option");
            goto done;
        }
    }
    if ((source_count == 0u) == (composition == NULL) || output == NULL) {
        set_error(error, error_size,
                  "petta-direct requires exactly one source list or composition and an output");
        goto done;
    }
    if (composition != NULL &&
        !direct_sources_from_composition_v1(
            composition, &arena, resolved_sources, sources, &source_count,
            "petta-direct", error, error_size))
        goto done;
    for (size_t index = 0u; index < source_count; index++) {
        presentations[index] = cetta_langdef_read_single_form(
            sources[index], &arena, error, error_size);
        if (presentations[index] == NULL)
            goto done;
    }
    if (!(closed_entry_residual
              ? cetta_gslt_petta_direct_closed_v1(
                    presentations, source_count,
                    entry_modes, entry_mode_count, &program, &program_len,
                    rule_count, source_digest, error, error_size)
              : cetta_gslt_petta_direct_selected_v1(
                    presentations, source_count,
                    entry_modes, entry_mode_count, &program, &program_len,
                    rule_count, source_digest, error, error_size)))
        goto done;
    if (epilogue != NULL) {
        uint8_t *combined;
        if (!read_file_bytes_v1(
                epilogue, &epilogue_bytes, &epilogue_len,
                error, error_size))
            goto done;
        if (program_len > SIZE_MAX - 1u ||
            program_len + 1u > SIZE_MAX - epilogue_len) {
            set_error(error, error_size,
                      "petta-direct program is too large");
            goto done;
        }
        combined = realloc(program, program_len + 1u + epilogue_len);
        if (combined == NULL) {
            set_error(error, error_size,
                      "out of memory appending petta-direct epilogue");
            goto done;
        }
        program = combined;
        program[program_len++] = '\n';
        if (epilogue_len > 0u) {
            memcpy(program + program_len, epilogue_bytes, epilogue_len);
            program_len += epilogue_len;
        }
    }
    cetta_native_sha256_hex(program, program_len, artifact_digest);
    ok = write_atomic(output, program, program_len, error, error_size);

done:
    free(epilogue_bytes);
    free(program);
    arena_free(&arena);
    return ok;
}

static bool compile_rhometta_direct_command_v1(
    int argc, char **argv, size_t *rule_count, size_t *relation_count,
    char source_digest[65], char artifact_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_count = 0u;
    const char *targets[CETTA_LANGDEF_MAX_SOURCES];
    size_t target_count = 0u;
    const char *composition = NULL;
    const char *epilogue = NULL;
    const char *output = NULL;
    const char *entry_rules[CETTA_LANGDEF_MAX_SOURCES];
    size_t entry_rule_count = 0u;
    char resolved_sources[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX];
    Atom *presentations[CETTA_LANGDEF_MAX_SOURCES];
    FHGSLTPackage *target_package = NULL;
    char target_package_digest[65] = {0};
    Arena arena;
    uint8_t *program = NULL;
    size_t program_len = 0u;
    uint8_t *epilogue_bytes = NULL;
    size_t epilogue_len = 0u;
    bool ok = false;

    arena_init(&arena);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "rhometta-direct option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0) {
            if (source_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "rhometta-direct has too many sources");
                goto done;
            }
            sources[source_count++] = value;
        } else if (strcmp(option, "--target") == 0) {
            if (target_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "rhometta-direct has too many target presentations");
                goto done;
            }
            targets[target_count++] = value;
        } else if (strcmp(option, "--composition") == 0 &&
                   composition == NULL) {
            composition = value;
        } else if (strcmp(option, "--epilogue") == 0 &&
                   epilogue == NULL) {
            epilogue = value;
        } else if (strcmp(option, "--entry-rule") == 0) {
            if (entry_rule_count >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "rhometta-direct has too many entry rules");
                goto done;
            }
            entry_rules[entry_rule_count++] = value;
        } else if (strcmp(option, "--out") == 0 && output == NULL) {
            output = value;
        } else {
            set_error(error, error_size,
                      "invalid or repeated rhometta-direct option");
            goto done;
        }
    }
    if ((source_count == 0u) == (composition == NULL) ||
        target_count == 0u || output == NULL) {
        set_error(
            error, error_size,
            "rhometta-direct requires exactly one source list or composition, a target package, and an output");
        goto done;
    }
    if (!fhgslt_package_from_paths(
            targets, target_count, &target_package, error, error_size) ||
        !fhgslt_package_digest(
            target_package, target_package_digest, error, error_size))
        goto done;
    if (strcmp(target_package_digest,
               CETTA_GSLT_RHOMETTA_TARGET_PACKAGE_DIGEST_V1) != 0) {
        set_error(
            error, error_size,
            "rhometta-direct target package digest mismatch: expected %s, got %s",
            CETTA_GSLT_RHOMETTA_TARGET_PACKAGE_DIGEST_V1,
            target_package_digest);
        goto done;
    }
    if (composition != NULL &&
        !direct_sources_from_composition_v1(
            composition, &arena, resolved_sources, sources, &source_count,
            "rhometta-direct", error, error_size))
        goto done;
    for (size_t index = 0u; index < source_count; index++) {
        presentations[index] = cetta_langdef_read_single_form(
            sources[index], &arena, error, error_size);
        if (presentations[index] == NULL)
            goto done;
    }
    if (!cetta_gslt_rhometta_direct_selected_v1(
            presentations, source_count, target_package_digest,
            entry_rules, entry_rule_count,
            &program, &program_len,
            rule_count, relation_count, source_digest,
            error, error_size))
        goto done;
    if (epilogue != NULL) {
        uint8_t *combined;
        if (!read_file_bytes_v1(
                epilogue, &epilogue_bytes, &epilogue_len,
                error, error_size))
            goto done;
        if (program_len > SIZE_MAX - 1u ||
            program_len + 1u > SIZE_MAX - epilogue_len) {
            set_error(error, error_size,
                      "rhometta-direct program is too large");
            goto done;
        }
        combined = realloc(program, program_len + 1u + epilogue_len);
        if (combined == NULL) {
            set_error(error, error_size,
                      "out of memory appending rhometta-direct epilogue");
            goto done;
        }
        program = combined;
        program[program_len++] = '\n';
        if (epilogue_len > 0u) {
            memcpy(program + program_len, epilogue_bytes, epilogue_len);
            program_len += epilogue_len;
        }
    }
    cetta_native_sha256_hex(program, program_len, artifact_digest);
    ok = write_atomic(output, program, program_len, error, error_size);

done:
    fhgslt_package_free(target_package);
    free(epilogue_bytes);
    free(program);
    arena_free(&arena);
    return ok;
}

static bool finite_horn_oracle_shape_command_v1(
    int argc, char **argv,
    FHGSLTStructuralShapeV1 *shape,
    char package_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_len = 0u;
    FHGSLTPackage *package = NULL;
    bool ok = false;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "oracle-horn-shape option lacks a value");
            goto done;
        }
        const char *value = argv[++index];
        if (strcmp(option, "--source") != 0) {
            set_error(error, error_size,
                      "invalid oracle-horn-shape option");
            goto done;
        }
        if (source_len >= CETTA_LANGDEF_MAX_SOURCES) {
            set_error(error, error_size,
                      "oracle-horn-shape has too many sources");
            goto done;
        }
        sources[source_len++] = value;
    }
    if (source_len == 0u) {
        set_error(error, error_size,
                  "oracle-horn-shape requires at least one source");
        goto done;
    }
    if (!fhgslt_package_from_paths(sources, source_len, &package,
                                   error, error_size) ||
        !fhgslt_package_digest(package, package_digest,
                               error, error_size) ||
        !fhgslt_package_structural_shape_v1(
            package, shape, error, error_size))
        goto done;
    ok = true;

done:
    fhgslt_package_free(package);
    return ok;
}

static bool assemble_petta_horn_program_v1(
    const uint8_t *runtime_bytes, size_t runtime_len,
    const uint8_t *clauses, size_t clause_len,
    const uint8_t *epilogue_bytes, size_t epilogue_len,
    bool has_epilogue,
    uint8_t **out, size_t *out_len,
    char *error, size_t error_size) {
    if (!out || !out_len ||
        (!runtime_bytes && runtime_len != 0u) ||
        (!clauses && clause_len != 0u) ||
        (!epilogue_bytes && epilogue_len != 0u) ||
        (!has_epilogue && epilogue_len != 0u))
        return set_error(error, error_size,
                         "invalid PeTTa Horn program assembly request");
    *out = NULL;
    *out_len = 0u;

    size_t separator_len = runtime_len > 0u &&
                           runtime_bytes[runtime_len - 1u] == (uint8_t)'\n'
                               ? 1u : 2u;
    size_t epilogue_separator_len = has_epilogue ? 1u : 0u;
    if (runtime_len > SIZE_MAX - separator_len ||
        runtime_len + separator_len > SIZE_MAX - clause_len ||
        runtime_len + separator_len + clause_len >
            SIZE_MAX - epilogue_separator_len ||
        runtime_len + separator_len + clause_len +
            epilogue_separator_len > SIZE_MAX - epilogue_len)
        return set_error(error, error_size,
                         "generated PeTTa Horn program is too large");

    size_t program_len = runtime_len + separator_len + clause_len +
                         epilogue_separator_len + epilogue_len;
    uint8_t *program = malloc(program_len ? program_len : 1u);
    if (!program)
        return set_error(error, error_size,
                         "out of memory constructing PeTTa Horn program");

    size_t offset = 0u;
    if (runtime_len > 0u) {
        memcpy(program, runtime_bytes, runtime_len);
        offset = runtime_len;
    }
    if (separator_len == 2u)
        program[offset++] = (uint8_t)'\n';
    program[offset++] = (uint8_t)'\n';
    if (clause_len > 0u) {
        memcpy(program + offset, clauses, clause_len);
        offset += clause_len;
    }
    if (has_epilogue) {
        program[offset++] = (uint8_t)'\n';
        if (epilogue_len > 0u) {
            memcpy(program + offset, epilogue_bytes, epilogue_len);
            offset += epilogue_len;
        }
    }
    if (offset != program_len) {
        free(program);
        return set_error(error, error_size,
                         "generated PeTTa Horn program length changed");
    }
    *out = program;
    *out_len = program_len;
    return true;
}

/* Independent audit projection only.  Direct target compilation must not
 * route source or runtime inputs through this Horn encoding. */
static bool compile_petta_horn_oracle_command_v1(
    int argc, char **argv, size_t *rule_len,
    char package_digest[65], char artifact_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_len = 0u;
    const char *runtime = NULL;
    const char *epilogue = NULL;
    const char *output = NULL;
    const char *receipt_output = NULL;
    FHGSLTPackage *package = NULL;
    uint8_t *runtime_bytes = NULL;
    size_t runtime_len = 0u;
    uint8_t *clauses = NULL;
    size_t clause_len = 0u;
    uint8_t *epilogue_bytes = NULL;
    size_t epilogue_len = 0u;
    uint8_t *program = NULL;
    size_t program_len = 0u;
    char runtime_digest[65];
    char epilogue_digest[65] = {0};
    bool ok = false;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "oracle-petta-horn option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0) {
            if (source_len >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "oracle-petta-horn has too many sources");
                goto done;
            }
            sources[source_len++] = value;
        } else if (strcmp(option, "--runtime") == 0 && !runtime) {
            runtime = value;
        } else if (strcmp(option, "--epilogue") == 0 && !epilogue) {
            epilogue = value;
        } else if (strcmp(option, "--out") == 0 && !output) {
            output = value;
        } else if (strcmp(option, "--receipt-out") == 0 &&
                   !receipt_output) {
            receipt_output = value;
        } else {
            set_error(error, error_size,
                      "invalid or repeated oracle-petta-horn option");
            goto done;
        }
    }
    if (source_len == 0u || !runtime || !output || !receipt_output) {
        set_error(error, error_size,
                  "oracle-petta-horn omits a required input");
        goto done;
    }
    if (!fhgslt_package_from_paths(sources, source_len, &package,
                                   error, error_size) ||
        !fhgslt_package_digest(package, package_digest,
                               error, error_size) ||
        !fhgslt_package_horn_clause_ir_v1(
            package, &clauses, &clause_len, error, error_size) ||
        !read_file_bytes_v1(runtime, &runtime_bytes, &runtime_len,
                            error, error_size))
        goto done;
    if (epilogue &&
        !read_file_bytes_v1(epilogue, &epilogue_bytes, &epilogue_len,
                            error, error_size))
        goto done;
    cetta_native_sha256_hex(runtime_bytes, runtime_len, runtime_digest);
    if (epilogue)
        cetta_native_sha256_hex(
            epilogue_bytes, epilogue_len, epilogue_digest);
    if (!assemble_petta_horn_program_v1(
            runtime_bytes, runtime_len,
            clauses, clause_len,
            epilogue_bytes, epilogue_len,
            epilogue != NULL,
            &program, &program_len,
            error, error_size))
        goto done;
    cetta_native_sha256_hex(program, program_len, artifact_digest);
    if (!write_atomic(output, program, program_len, error, error_size))
        goto done;
    *rule_len = fhgslt_package_rule_count(package);
    {
        char receipt[768];
        int receipt_len;
        if (epilogue) {
            receipt_len = snprintf(
                receipt, sizeof(receipt),
                "(petta-horn-receipt-v1\n"
                "  (source-package-sha256 \"%s\")\n"
                "  (runtime-sha256 \"%s\")\n"
                "  (epilogue-sha256 \"%s\")\n"
                "  (artifact-sha256 \"%s\"))\n",
                package_digest, runtime_digest,
                epilogue_digest, artifact_digest);
        } else {
            receipt_len = snprintf(
                receipt, sizeof(receipt),
                "(petta-horn-receipt-v1\n"
                "  (source-package-sha256 \"%s\")\n"
                "  (runtime-sha256 \"%s\")\n"
                "  (artifact-sha256 \"%s\"))\n",
                package_digest, runtime_digest, artifact_digest);
        }
        if (receipt_len < 0 || (size_t)receipt_len >= sizeof(receipt) ||
            !write_atomic(receipt_output,
                          (const uint8_t *)receipt,
                          (size_t)receipt_len,
                          error,
                          error_size))
            goto done;
    }
    ok = true;

done:
    free(program);
    free(epilogue_bytes);
    free(clauses);
    free(runtime_bytes);
    fhgslt_package_free(package);
    return ok;
}

typedef struct {
    const char *source_package_digest;
    const char *runtime_digest;
    const char *epilogue_digest;
    const char *artifact_digest;
} PeTTaHornReceiptV1;

static bool lowercase_sha256_v1(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return false;
    }
    return true;
}

static bool petta_horn_receipt_digest_v1(
    Atom *field, const char *name, const char **slot,
    char *error, size_t error_size) {
    const char *digest = NULL;
    if (*slot || !cetta_langdef_expr_head(field, name, 1u) ||
        !cetta_langdef_text_arg(field->expr.elems[1], &digest) ||
        !lowercase_sha256_v1(digest))
        return set_error(error, error_size,
                         "PeTTa Horn receipt has an invalid %s", name);
    *slot = digest;
    return true;
}

static bool petta_horn_receipt_parse_v1(
    Atom *root, PeTTaHornReceiptV1 *receipt,
    char *error, size_t error_size) {
    memset(receipt, 0, sizeof(*receipt));
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 4u ||
        root->expr.len > 5u ||
        !atom_is_symbol(root->expr.elems[0], "petta-horn-receipt-v1"))
        return set_error(error, error_size,
                         "receipt root must be petta-horn-receipt-v1");
    for (CettaExprIndex index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        if (cetta_langdef_expr_head(field,
                                    "source-package-sha256", 1u)) {
            if (!petta_horn_receipt_digest_v1(
                    field, "source-package-sha256",
                    &receipt->source_package_digest,
                    error, error_size))
                return false;
        } else if (cetta_langdef_expr_head(field, "runtime-sha256", 1u)) {
            if (!petta_horn_receipt_digest_v1(
                    field, "runtime-sha256", &receipt->runtime_digest,
                    error, error_size))
                return false;
        } else if (cetta_langdef_expr_head(field, "epilogue-sha256", 1u)) {
            if (!petta_horn_receipt_digest_v1(
                    field, "epilogue-sha256", &receipt->epilogue_digest,
                    error, error_size))
                return false;
        } else if (cetta_langdef_expr_head(field, "artifact-sha256", 1u)) {
            if (!petta_horn_receipt_digest_v1(
                    field, "artifact-sha256", &receipt->artifact_digest,
                    error, error_size))
                return false;
        } else {
            return set_error(error, error_size,
                             "PeTTa Horn receipt has an unknown field");
        }
    }
    if (!receipt->source_package_digest || !receipt->runtime_digest ||
        !receipt->artifact_digest)
        return set_error(error, error_size,
                         "PeTTa Horn receipt omits a required digest");
    return true;
}

static bool bytes_contain_v1(const uint8_t *bytes,
                             size_t len,
                             const uint8_t *needle,
                             size_t needle_len) {
    if (needle_len == 0u)
        return true;
    if (!bytes || !needle || needle_len > len)
        return false;
    for (size_t offset = 0u; offset <= len - needle_len; offset++) {
        if (memcmp(bytes + offset, needle, needle_len) == 0)
            return true;
    }
    return false;
}

/* Validate an artifact produced by the optional Horn oracle above. */
static bool check_petta_horn_oracle_command_v1(
    int argc, char **argv, size_t *rule_len, char artifact_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    size_t source_len = 0u;
    const char *runtime = NULL;
    const char *epilogue = NULL;
    const char *program = NULL;
    const char *receipt_path = NULL;
    FHGSLTPackage *package = NULL;
    char package_digest[65];
    char runtime_digest[65];
    char epilogue_digest[65] = {0};
    Arena arena;
    PeTTaHornReceiptV1 receipt;
    uint8_t *runtime_bytes = NULL;
    size_t runtime_len = 0u;
    uint8_t *clauses = NULL;
    size_t clause_len = 0u;
    uint8_t *epilogue_bytes = NULL;
    size_t epilogue_len = 0u;
    uint8_t *program_bytes = NULL;
    size_t program_len = 0u;
    uint8_t *expected_program = NULL;
    size_t expected_program_len = 0u;
    bool ok = false;

    arena_init(&arena);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "oracle-petta-horn-check option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0) {
            if (source_len >= CETTA_LANGDEF_MAX_SOURCES) {
                set_error(error, error_size,
                          "oracle-petta-horn-check has too many sources");
                goto done;
            }
            sources[source_len++] = value;
        } else if (strcmp(option, "--runtime") == 0 && !runtime) {
            runtime = value;
        } else if (strcmp(option, "--epilogue") == 0 && !epilogue) {
            epilogue = value;
        } else if (strcmp(option, "--program") == 0 && !program) {
            program = value;
        } else if (strcmp(option, "--receipt") == 0 && !receipt_path) {
            receipt_path = value;
        } else {
            set_error(error, error_size,
                      "invalid or repeated oracle-petta-horn-check option");
            goto done;
        }
    }
    if (source_len == 0u || !runtime || !program || !receipt_path) {
        set_error(error, error_size,
                  "oracle-petta-horn-check omits a required input");
        goto done;
    }
    Atom *root = cetta_langdef_read_single_form(
        receipt_path, &arena, error, error_size);
    if (!root ||
        !petta_horn_receipt_parse_v1(root, &receipt, error, error_size) ||
        !fhgslt_package_from_paths(sources, source_len, &package,
                                   error, error_size) ||
        !fhgslt_package_digest(package, package_digest,
                               error, error_size) ||
        !fhgslt_package_horn_clause_ir_v1(
            package, &clauses, &clause_len, error, error_size) ||
        !read_file_bytes_v1(runtime, &runtime_bytes, &runtime_len,
                            error, error_size) ||
        (epilogue &&
         !read_file_bytes_v1(epilogue, &epilogue_bytes, &epilogue_len,
                             error, error_size)) ||
        !read_file_bytes_v1(program, &program_bytes, &program_len,
                            error, error_size) ||
        !assemble_petta_horn_program_v1(
            runtime_bytes, runtime_len,
            clauses, clause_len,
            epilogue_bytes, epilogue_len,
            epilogue != NULL,
            &expected_program, &expected_program_len,
            error, error_size))
        goto done;
    cetta_native_sha256_hex(runtime_bytes, runtime_len, runtime_digest);
    if (epilogue)
        cetta_native_sha256_hex(
            epilogue_bytes, epilogue_len, epilogue_digest);
    cetta_native_sha256_hex(program_bytes, program_len, artifact_digest);
    if (strcmp(receipt.source_package_digest, package_digest) != 0 ||
        strcmp(receipt.runtime_digest, runtime_digest) != 0 ||
        strcmp(receipt.artifact_digest, artifact_digest) != 0 ||
        (epilogue &&
         (!receipt.epilogue_digest ||
          strcmp(receipt.epilogue_digest, epilogue_digest) != 0)) ||
        (!epilogue && receipt.epilogue_digest)) {
        set_error(error, error_size,
                  "PeTTa Horn program disagrees with its source-bound receipt");
        goto done;
    }
    if (program_len != expected_program_len ||
        (program_len > 0u &&
         memcmp(program_bytes, expected_program, program_len) != 0)) {
        set_error(
            error, error_size,
            "PeTTa Horn program is not the canonical rendering of its declared inputs");
        goto done;
    }
    char identity[192];
    int identity_len = snprintf(
        identity, sizeof(identity),
        "(HornClauseV1 (FiniteHornPackageDigestV1 sha256-%s) "
        "HornBodyNilV1)",
        package_digest);
    if (identity_len < 0 || (size_t)identity_len >= sizeof(identity) ||
        !bytes_contain_v1(program_bytes,
                          program_len,
                          (const uint8_t *)identity,
                          (size_t)identity_len)) {
        set_error(error, error_size,
                  "PeTTa Horn program omits its admitted source identity");
        goto done;
    }
    *rule_len = fhgslt_package_rule_count(package);
    ok = true;

done:
    free(expected_program);
    free(program_bytes);
    free(epilogue_bytes);
    free(clauses);
    free(runtime_bytes);
    fhgslt_package_free(package);
    arena_free(&arena);
    return ok;
}

static const char *option_value(int argc, char **argv, const char *name) {
    for (int index = 2; index + 1 < argc; index++) {
        if (strcmp(argv[index], name) == 0)
            return argv[index + 1];
    }
    return NULL;
}

static bool compile_answers_command(int argc, char **argv,
                                    size_t *answer_len,
                                    char answer_digest[65],
                                    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    const char *reflected_sources[CETTA_LANGDEF_MAX_SOURCES];
    const char *chart = NULL;
    const char *query = NULL;
    const char *timeout = "30";
    const char *output = NULL;
    uint32_t source_len = 0u;
    uint32_t reflected_source_len = 0u;
    bool timeout_set = false;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "finite-Horn query option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--chart") == 0 && !chart)
            chart = value;
        else if (strcmp(option, "--source") == 0 &&
                 source_len < CETTA_LANGDEF_MAX_SOURCES)
            sources[source_len++] = value;
        else if (strcmp(option, "--reflect-source") == 0 &&
                 reflected_source_len < CETTA_LANGDEF_MAX_SOURCES)
            reflected_sources[reflected_source_len++] = value;
        else if (strcmp(option, "--query") == 0 && !query)
            query = value;
        else if (strcmp(option, "--timeout") == 0 && !timeout_set) {
            timeout = value;
            timeout_set = true;
        }
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else
            return set_error(error, error_size,
                             "invalid or repeated finite-Horn query option");
    }
    if (!chart || source_len == 0u || !query || !output ||
        source_len + reflected_source_len > CETTA_LANGDEF_MAX_SOURCES)
        return set_error(error, error_size,
                         "finite-Horn query omits a required input");
    {
        char *end = NULL;
        double seconds;
        errno = 0;
        seconds = strtod(timeout, &end);
        if (errno != 0 || !end || *end != '\0' ||
            !isfinite(seconds) || seconds <= 0.0)
            return set_error(error, error_size,
                             "finite-Horn query timeout must be positive");
    }
    return compile_answer_stream(
        chart, sources, source_len,
        reflected_sources, reflected_source_len,
        query, timeout, output, answer_len, answer_digest,
        error, error_size);
}

typedef struct {
    const char *name;
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    uint32_t source_len;
} GSLTCompositionV1;

static bool gslt_composition_parse_v1(Atom *root, GSLTCompositionV1 *out,
                                      char *error, size_t error_size) {
    CettaExprIndex index;

    if (!out)
        return set_error(error, error_size,
                         "invalid GSLT composition destination");
    memset(out, 0, sizeof(*out));
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 3u ||
        !atom_is_symbol(root->expr.elems[0], "gslt-composition-v1"))
        return set_error(error, error_size,
                         "composition root must be gslt-composition-v1");
    for (index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char *value = NULL;

        if (cetta_langdef_expr_head(field, "name", 1u)) {
            if (out->name ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return set_error(error, error_size,
                                 "composition has an invalid name");
            out->name = value;
        } else if (cetta_langdef_expr_head(field, "source", 1u)) {
            if (out->source_len >= CETTA_LANGDEF_MAX_SOURCES ||
                !cetta_langdef_text_arg(field->expr.elems[1], &value) ||
                value[0] == '\0')
                return set_error(error, error_size,
                                 "composition has an invalid source list");
            for (uint32_t source_index = 0u;
                 source_index < out->source_len; source_index++) {
                if (strcmp(out->sources[source_index], value) == 0)
                    return set_error(error, error_size,
                                     "composition repeats a source");
            }
            out->sources[out->source_len++] = value;
        } else {
            return set_error(error, error_size,
                             "composition has an unknown field");
        }
    }
    if (!out->name || out->source_len == 0u)
        return set_error(error, error_size,
                         "composition requires a name and sources");
    return true;
}

static bool compile_oslf_native_type_command(
    int argc, char **argv,
    uint32_t *composition_source_len,
    size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    static const char query[] = "(compile-oslf-native-type-v1 ?fact)";
    const char *composition_argument = NULL;
    const char *chart_argument = NULL;
    const char *reflection_argument = NULL;
    const char *compiler_argument = NULL;
    const char *output = NULL;
    const char *timeout = "30";
    bool timeout_set = false;
    char *composition_path = NULL;
    char *chart_path = NULL;
    char *reflection_path = NULL;
    char *compiler_path = NULL;
    char reflected_paths[CETTA_LANGDEF_MAX_SOURCES][PATH_MAX];
    const char *sources[2];
    const char *reflected_sources[CETTA_LANGDEF_MAX_SOURCES];
    Arena arena;
    Atom *root = NULL;
    GSLTCompositionV1 composition;
    FHAnswerStreamV1 stream;
    bool ok = false;

    arena_init(&arena);
    fh_answer_stream_v1_init(&stream);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "OSLF native-type option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--composition") == 0 &&
            !composition_argument)
            composition_argument = value;
        else if (strcmp(option, "--chart") == 0 && !chart_argument)
            chart_argument = value;
        else if (strcmp(option, "--reflection") == 0 &&
                 !reflection_argument)
            reflection_argument = value;
        else if (strcmp(option, "--compiler") == 0 &&
                 !compiler_argument)
            compiler_argument = value;
        else if (strcmp(option, "--timeout") == 0 && !timeout_set) {
            timeout = value;
            timeout_set = true;
        } else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated OSLF native-type option");
            goto done;
        }
    }
    if (!composition_argument || !chart_argument ||
        !reflection_argument || !compiler_argument || !output) {
        set_error(error, error_size,
                  "OSLF native-type compilation omits a required input");
        goto done;
    }
    {
        char *end = NULL;
        double seconds;
        errno = 0;
        seconds = strtod(timeout, &end);
        if (errno != 0 || !end || *end != '\0' ||
            !isfinite(seconds) || seconds <= 0.0) {
            set_error(error, error_size,
                      "OSLF native-type timeout must be positive");
            goto done;
        }
    }
    composition_path = realpath(composition_argument, NULL);
    chart_path = realpath(chart_argument, NULL);
    reflection_path = realpath(reflection_argument, NULL);
    compiler_path = realpath(compiler_argument, NULL);
    if (!composition_path || !chart_path || !reflection_path ||
        !compiler_path) {
        set_error(error, error_size,
                  "cannot resolve OSLF native-type input path: %s",
                  strerror(errno));
        goto done;
    }
    root = cetta_langdef_read_single_form(
        composition_path, &arena, error, error_size);
    if (!root ||
        !gslt_composition_parse_v1(root, &composition,
                                   error, error_size))
        goto done;
    for (uint32_t index = 0u; index < composition.source_len; index++) {
        if (!cetta_langdef_path_join(
                composition_path, composition.sources[index],
                reflected_paths[index], sizeof(reflected_paths[index]),
                error, error_size))
            goto done;
        reflected_sources[index] = reflected_paths[index];
    }
    sources[0] = reflection_path;
    sources[1] = compiler_path;
    if (!compile_answer_stream(
            chart_path, sources, 2u,
            reflected_sources, composition.source_len,
            query, timeout, output, answer_len, answer_digest,
            error, error_size) ||
        !fh_answer_stream_v1_read(&stream, output,
                                  error, error_size) ||
        stream.len == 0u)
        goto done;
    for (size_t index = 0u; index < stream.len; index++) {
        if (!cetta_langdef_expr_head(
                stream.terms[index], "compile-oslf-native-type-v1", 1u)) {
            set_error(error, error_size,
                      "OSLF compiler emitted a foreign answer");
            goto done;
        }
    }
    *composition_source_len = composition.source_len;
    ok = true;

done:
    fh_answer_stream_v1_free(&stream);
    arena_free(&arena);
    free(compiler_path);
    free(reflection_path);
    free(chart_path);
    free(composition_path);
    return ok;
}

static bool compile_lexical_answers_command(
    int argc, char **argv,
    size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    const char *reflected_sources[CETTA_LANGDEF_MAX_SOURCES];
    const char *chart = NULL;
    const char *roots = NULL;
    const char *timeout = "30";
    const char *output = NULL;
    uint32_t source_len = 0u;
    uint32_t reflected_source_len = 0u;
    bool timeout_set = false;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "lexical compiler option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--chart") == 0 && !chart)
            chart = value;
        else if (strcmp(option, "--source") == 0 &&
                 source_len < CETTA_LANGDEF_MAX_SOURCES)
            sources[source_len++] = value;
        else if (strcmp(option, "--reflect-source") == 0 &&
                 reflected_source_len < CETTA_LANGDEF_MAX_SOURCES)
            reflected_sources[reflected_source_len++] = value;
        else if (strcmp(option, "--roots") == 0 && !roots)
            roots = value;
        else if (strcmp(option, "--timeout") == 0 && !timeout_set) {
            timeout = value;
            timeout_set = true;
        } else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else
            return set_error(error, error_size,
                             "invalid or repeated lexical compiler option");
    }
    if (!chart || source_len == 0u || !roots || !output ||
        source_len + reflected_source_len > CETTA_LANGDEF_MAX_SOURCES)
        return set_error(error, error_size,
                         "lexical compiler omits a required input");
    {
        char *end = NULL;
        double seconds;
        errno = 0;
        seconds = strtod(timeout, &end);
        if (errno != 0 || !end || *end != '\0' ||
            !isfinite(seconds) || seconds <= 0.0)
            return set_error(error, error_size,
                             "lexical compiler timeout must be positive");
    }
    return compile_lexical_answer_family(
        chart, sources, source_len,
        reflected_sources, reflected_source_len,
        roots, timeout, output,
        answer_len, answer_digest,
        error, error_size);
}

static bool parse_positive_u32(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;
    if (!text || !out || text[0] == '\0')
        return false;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
        value == 0ul || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool compile_transparent_inline_command(
    int argc, char **argv,
    PPTransparentInlineNativeV1Summary *summary,
    char *error, size_t error_size) {
    const char *abi = NULL;
    const char *output = NULL;
    uint32_t max_paths = 10000u;
    uint32_t max_productions = 100000u;
    bool max_paths_set = false;
    bool max_productions_set = false;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "transparent-inline option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--abi") == 0 && !abi)
            abi = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else if (strcmp(option, "--max-paths") == 0 && !max_paths_set) {
            if (!parse_positive_u32(value, &max_paths))
                return set_error(
                    error, error_size,
                    "transparent-inline max-paths must be positive");
            max_paths_set = true;
        } else if (strcmp(option, "--max-productions") == 0 &&
                   !max_productions_set) {
            if (!parse_positive_u32(value, &max_productions))
                return set_error(
                    error, error_size,
                    "transparent-inline max-productions must be positive");
            max_productions_set = true;
        } else {
            return set_error(
                error, error_size,
                "invalid or repeated transparent-inline option");
        }
    }
    if (!abi || !output)
        return set_error(
            error, error_size,
            "transparent-inline compiler omits a required input");
    return pp_transparent_inline_native_v1_compile_file(
        abi, output, max_paths, max_productions,
        summary, error, error_size);
}

static bool compile_pack_facts_command(
    int argc, char **argv, size_t *fact_len, char pack_digest[65],
    char *error, size_t error_size) {
    const char *abi = NULL;
    const char *source_abi = NULL;
    const char *output = NULL;
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "pack-facts option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--abi") == 0 && !abi)
            abi = value;
        else if (strcmp(option, "--source-abi") == 0 && !source_abi)
            source_abi = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else
            return set_error(
                error, error_size,
                "invalid or repeated pack-facts option");
    }
    if (!abi || !output)
        return set_error(
            error, error_size,
            "pack-facts compiler omits a required input");
    return compile_pack_facts(
        abi, source_abi, output, fact_len, pack_digest,
        error, error_size);
}

static bool compile_answer_facts_command(
    int argc, char **argv, size_t *fact_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *source = NULL;
    const char *output = NULL;
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "answer-facts option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--source") == 0 && !source)
            source = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else
            return set_error(
                error, error_size,
                "invalid or repeated answer-facts option");
    }
    if (!source || !output)
        return set_error(
            error, error_size,
            "answer-facts compiler omits a required input");
    return compile_answer_facts_presentation(
        source, output, fact_len, answer_digest,
        error, error_size);
}

static bool compile_semantic_gslt_command_v1(
    int argc, char **argv,
    size_t *operator_count, size_t *rule_count,
    char output_digest[65],
    char *error, size_t error_size) {
    const char *source = NULL;
    const char *output = NULL;

    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "semantic-gslt option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--source") == 0 && !source)
            source = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else
            return set_error(
                error, error_size,
                "invalid or repeated semantic-gslt option");
    }
    if (!source || !output)
        return set_error(
            error, error_size,
            "semantic-gslt compiler omits a required input");
    return compile_semantic_gslt_presentation_v1(
        source, output, operator_count, rule_count,
        output_digest, error, error_size);
}

static bool merge_answer_streams_command(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *sources[CETTA_LANGDEF_MAX_SOURCES];
    uint32_t source_len = 0u;
    const char *output = NULL;
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    uint8_t *payload = NULL;
    size_t payload_len = 0u;
    FHAnswerStreamV1 stream;
    FHAnswerStreamV1 published;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    fh_answer_stream_v1_init(&published);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "merge-answers option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0 &&
            source_len < CETTA_LANGDEF_MAX_SOURCES)
            sources[source_len++] = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated merge-answers option");
            goto done;
        }
    }
    if (source_len == 0u || !output) {
        set_error(error, error_size,
                  "merge-answers omits a required input");
        goto done;
    }
    for (uint32_t source_index = 0u;
         source_index < source_len; source_index++) {
        if (!fh_answer_stream_v1_read(
                &stream, sources[source_index], error, error_size))
            goto done;
        for (size_t index = 0u; index < stream.len; index++) {
            uint8_t *canonical = NULL;
            size_t canonical_len = 0u;
            if (!fh_ground_term_v1_render(
                    stream.terms[index], &canonical, &canonical_len,
                    error, error_size) ||
                !canonical_answer_push(
                    &answers, &answers_len, &answers_cap,
                    (char *)canonical, canonical_len,
                    error, error_size)) {
                free(canonical);
                goto done;
            }
        }
        fh_answer_stream_v1_free(&stream);
        fh_answer_stream_v1_init(&stream);
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    for (size_t index = 0u; index < answers_len; index++) {
        if ((index > 0u &&
             strcmp(answers[index - 1u].text,
                    answers[index].text) == 0) ||
            answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      index > 0u &&
                      strcmp(answers[index - 1u].text,
                             answers[index].text) == 0
                          ? "merged answer streams overlap"
                          : "merged answer stream is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory merging answer streams");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text,
                   answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output, payload, payload_len, error, error_size) ||
        !fh_answer_stream_v1_read(
            &published, output, error, error_size) ||
        published.len != answers_len) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "merged answer stream changed during publication");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, 65u);
    ok = true;

done:
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(answers);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

static bool project_answer_stream_command(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *source = NULL;
    const char *head = NULL;
    const char *argument_text = NULL;
    const char *output = NULL;
    uint32_t argument = 0u;
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    size_t payload_len = 0u;
    uint8_t *payload = NULL;
    FHAnswerStreamV1 stream;
    FHAnswerStreamV1 published;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    fh_answer_stream_v1_init(&published);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "project-answers option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0 && !source)
            source = value;
        else if (strcmp(option, "--head") == 0 && !head)
            head = value;
        else if (strcmp(option, "--arg") == 0 && !argument_text)
            argument_text = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated project-answers option");
            goto done;
        }
    }
    if (!source || !head || head[0] == '\0' || !argument_text || !output) {
        set_error(error, error_size,
                  "project-answers omits a required input");
        goto done;
    }
    {
        char *end = NULL;
        unsigned long value;
        errno = 0;
        value = strtoul(argument_text, &end, 10);
        if (errno != 0 || !end || *end != '\0' || value > UINT32_MAX) {
            set_error(error, error_size,
                      "project-answers arg must be a nonnegative integer");
            goto done;
        }
        argument = (uint32_t)value;
    }
    if (!fh_answer_stream_v1_read(&stream, source, error, error_size))
        goto done;
    for (size_t index = 0u; index < stream.len; index++) {
        Atom *record = stream.terms[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!record || record->kind != ATOM_EXPR ||
            record->expr.len < 2u ||
            !atom_is_symbol(record->expr.elems[0], head) ||
            (uint64_t)argument + 1u >= record->expr.len) {
            set_error(error, error_size,
                      "project-answers record %zu has the wrong shape",
                      index + 1u);
            goto done;
        }
        if (!fh_ground_term_v1_render(
                record->expr.elems[(CettaExprIndex)argument + 1u],
                &canonical, &canonical_len, error, error_size) ||
            !canonical_answer_push(
                &answers, &answers_len, &answers_cap,
                (char *)canonical, canonical_len,
                error, error_size)) {
            free(canonical);
            goto done;
        }
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    for (size_t index = 0u; index < answers_len; index++) {
        if ((index > 0u &&
             strcmp(answers[index - 1u].text, answers[index].text) == 0) ||
            answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      index > 0u &&
                      strcmp(answers[index - 1u].text,
                             answers[index].text) == 0
                          ? "projected answer keys are not unique"
                          : "projected answer stream is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory projecting answer stream");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text,
                   answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output, payload, payload_len, error, error_size) ||
        !fh_answer_stream_v1_read(
            &published, output, error, error_size) ||
        published.len != answers_len) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "projected answer stream changed during publication");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, 65u);
    ok = true;

done:
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(answers);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

static bool select_answer_stream_command(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *source = NULL;
    const char *head = NULL;
    const char *output = NULL;
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    size_t payload_len = 0u;
    uint8_t *payload = NULL;
    FHAnswerStreamV1 stream;
    FHAnswerStreamV1 published;
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    fh_answer_stream_v1_init(&published);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "select-answers option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0 && !source)
            source = value;
        else if (strcmp(option, "--head") == 0 && !head)
            head = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated select-answers option");
            goto done;
        }
    }
    if (!source || !head || head[0] == '\0' || !output) {
        set_error(error, error_size,
                  "select-answers omits a required input");
        goto done;
    }
    if (!fh_answer_stream_v1_read(&stream, source, error, error_size))
        goto done;
    for (size_t index = 0u; index < stream.len; index++) {
        Atom *record = stream.terms[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!record || record->kind != ATOM_EXPR ||
            record->expr.len == 0u ||
            !atom_is_symbol(record->expr.elems[0], head))
            continue;
        if (!fh_ground_term_v1_render(
                record, &canonical, &canonical_len,
                error, error_size) ||
            !canonical_answer_push(
                &answers, &answers_len, &answers_cap,
                (char *)canonical, canonical_len,
                error, error_size)) {
            free(canonical);
            goto done;
        }
    }
    if (answers_len == 0u) {
        set_error(error, error_size,
                  "select-answers found no matching records");
        goto done;
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    for (size_t index = 0u; index < answers_len; index++) {
        if ((index > 0u &&
             strcmp(answers[index - 1u].text,
                    answers[index].text) == 0) ||
            answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      index > 0u &&
                      strcmp(answers[index - 1u].text,
                             answers[index].text) == 0
                          ? "selected answer records are not unique"
                          : "selected answer stream is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory selecting answer stream");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text,
                   answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output, payload, payload_len, error, error_size) ||
        !fh_answer_stream_v1_read(
            &published, output, error, error_size) ||
        published.len != answers_len) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "selected answer stream changed during publication");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, 65u);
    ok = true;

done:
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(answers);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

typedef struct {
    uint32_t *values;
    size_t len;
    size_t cap;
} StateTransitionNumeralsV1;

typedef struct {
    char *operation;
    size_t operation_len;
    uint32_t index;
} StateTransitionActionIndexV1;

typedef struct {
    StateTransitionActionIndexV1 *values;
    size_t len;
    size_t cap;
} StateTransitionActionsV1;

static int state_transition_u32_compare(const void *left,
                                        const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs;
}

static int state_transition_action_compare(const void *left,
                                           const void *right) {
    const StateTransitionActionIndexV1 *lhs = left;
    const StateTransitionActionIndexV1 *rhs = right;
    int operation = strcmp(lhs->operation, rhs->operation);
    if (operation != 0)
        return operation;
    return lhs->index < rhs->index ? -1 : lhs->index > rhs->index;
}

static bool state_transition_action_push(
    StateTransitionActionsV1 *actions, char *operation,
    size_t operation_len, uint32_t index,
    char *error, size_t error_size) {
    StateTransitionActionIndexV1 *next;
    size_t next_cap;
    if (actions->len >= actions->cap) {
        next_cap = actions->cap ? actions->cap * 2u : 32u;
        if (next_cap < actions->cap ||
            next_cap > SIZE_MAX / sizeof(*actions->values))
            return set_error(error, error_size,
                             "state-transition action inventory is too large");
        next = realloc(actions->values,
                       next_cap * sizeof(*actions->values));
        if (!next)
            return set_error(error, error_size,
                             "out of memory collecting state-transition actions");
        actions->values = next;
        actions->cap = next_cap;
    }
    actions->values[actions->len++] =
        (StateTransitionActionIndexV1){operation, operation_len, index};
    return true;
}

static bool state_transition_numeral_push(
    StateTransitionNumeralsV1 *numerals, uint32_t value,
    char *error, size_t error_size) {
    uint32_t *next;
    size_t next_cap;
    if (numerals->len < numerals->cap) {
        numerals->values[numerals->len++] = value;
        return true;
    }
    next_cap = numerals->cap ? numerals->cap * 2u : 32u;
    if (next_cap < numerals->cap ||
        next_cap > SIZE_MAX / sizeof(*numerals->values))
        return set_error(error, error_size,
                         "state-transition numeral set is too large");
    next = realloc(numerals->values,
                   next_cap * sizeof(*numerals->values));
    if (!next)
        return set_error(error, error_size,
                         "out of memory collecting state-transition numerals");
    numerals->values = next;
    numerals->cap = next_cap;
    numerals->values[numerals->len++] = value;
    return true;
}

static bool state_transition_symbol_u32(const Atom *term,
                                        uint32_t *value_out) {
    const char *text;
    char canonical[32];
    char *end = NULL;
    unsigned long value;
    int written;
    if (!term || !value_out)
        return false;
    if (term->kind == ATOM_GROUNDED && term->ground.gkind == GV_INT &&
        term->ground.ival >= 0 &&
        (uint64_t)term->ground.ival <= UINT32_MAX) {
        *value_out = (uint32_t)term->ground.ival;
        return true;
    }
    if (term->kind != ATOM_SYMBOL)
        return false;
    text = atom_name_cstr((Atom *)term);
    if (!text || text[0] == '\0')
        return false;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > UINT32_MAX)
        return false;
    written = snprintf(canonical, sizeof(canonical), "%lu", value);
    if (written < 0 || (size_t)written >= sizeof(canonical) ||
        strcmp(canonical, text) != 0)
        return false;
    *value_out = (uint32_t)value;
    return true;
}

static bool state_transition_collect_numerals(
    const Atom *term, StateTransitionNumeralsV1 *numerals,
    uint32_t depth, char *error, size_t error_size) {
    uint32_t value;
    if (!term || depth > 4096u)
        return set_error(error, error_size,
                         "state-transition action term is too deep");
    if (term->kind == ATOM_SYMBOL) {
        if (!state_transition_symbol_u32(term, &value))
            return true;
        return state_transition_numeral_push(
            numerals, value, error, error_size);
    }
    if (term->kind == ATOM_GROUNDED) {
        if (!state_transition_symbol_u32(term, &value))
            return set_error(error, error_size,
                             "state-transition action contains a non-u32 grounded value");
        return state_transition_numeral_push(
            numerals, value, error, error_size);
    }
    if (term->kind != ATOM_EXPR)
        return set_error(error, error_size,
                         "state-transition action is not first-order");
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!state_transition_collect_numerals(
                term->expr.elems[index], numerals, depth + 1u,
                error, error_size))
            return false;
    }
    return true;
}

static char *state_transition_peano(uint32_t value,
                                    char *error, size_t error_size) {
    static const char successor[] = "(NatSuccV1 ";
    static const char zero[] = "NatZeroV1";
    size_t successor_len = sizeof(successor) - 1u;
    size_t zero_len = sizeof(zero) - 1u;
    size_t length;
    size_t offset = 0u;
    char *text;
    if ((size_t)value > (SIZE_MAX - zero_len - 1u) /
                            (successor_len + 1u)) {
        set_error(error, error_size,
                  "state-transition Peano numeral is too large");
        return NULL;
    }
    length = (size_t)value * (successor_len + 1u) + zero_len;
    text = malloc(length + 1u);
    if (!text) {
        set_error(error, error_size,
                  "out of memory reifying state-transition numeral");
        return NULL;
    }
    for (uint32_t index = 0u; index < value; index++) {
        memcpy(text + offset, successor, successor_len);
        offset += successor_len;
    }
    memcpy(text + offset, zero, zero_len);
    offset += zero_len;
    for (uint32_t index = 0u; index < value; index++)
        text[offset++] = ')';
    text[offset] = '\0';
    return text;
}

static bool state_transition_answer_pushf(
    LangDefCanonicalAnswerV1 **answers, size_t *len, size_t *cap,
    char *error, size_t error_size, const char *format, ...) {
    va_list arguments;
    va_list copied;
    int needed;
    char *text;
    va_start(arguments, format);
    va_copy(copied, arguments);
    needed = vsnprintf(NULL, 0u, format, arguments);
    va_end(arguments);
    if (needed < 0) {
        va_end(copied);
        return set_error(error, error_size,
                         "cannot format state-transition binding");
    }
    text = malloc((size_t)needed + 1u);
    if (!text) {
        va_end(copied);
        return set_error(error, error_size,
                         "out of memory formatting state-transition binding");
    }
    if (vsnprintf(text, (size_t)needed + 1u, format, copied) != needed) {
        va_end(copied);
        free(text);
        return set_error(error, error_size,
                         "state-transition binding changed while formatting");
    }
    va_end(copied);
    if (!canonical_answer_push(
            answers, len, cap, text, (size_t)needed,
            error, error_size)) {
        free(text);
        return false;
    }
    return true;
}

static bool state_transition_bindings_command(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *source = NULL;
    const char *output = NULL;
    LangDefCanonicalAnswerV1 *tables = NULL;
    size_t table_len = 0u;
    size_t table_cap = 0u;
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    StateTransitionNumeralsV1 numerals = {0};
    StateTransitionActionsV1 actions = {0};
    StateTransitionNumeralsV1 final_actions = {0};
    FHAnswerStreamV1 stream;
    FHAnswerStreamV1 published;
    uint8_t *payload = NULL;
    size_t payload_len = 0u;
    char package[128];
    bool ok = false;

    fh_answer_stream_v1_init(&stream);
    fh_answer_stream_v1_init(&published);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "state-transition-bindings option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--source") == 0 && !source)
            source = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated state-transition-bindings option");
            goto done;
        }
    }
    if (!source || !output) {
        set_error(error, error_size,
                  "state-transition-bindings omits a required input");
        goto done;
    }
    if (!fh_answer_stream_v1_read(&stream, source, error, error_size))
        goto done;
    {
        int written = snprintf(
            package, sizeof(package),
            "(StateProgramDigestV1 sha256-%s)", stream.digest);
        if (written < 0 || (size_t)written >= sizeof(package)) {
            set_error(error, error_size,
                      "state-transition package identity is too large");
            goto done;
        }
    }
    for (size_t index = 0u; index < stream.len; index++) {
        Atom *record = stream.terms[index];
        if (!record || record->kind != ATOM_EXPR ||
            record->expr.len == 0u)
            continue;
        if (atom_is_symbol(record->expr.elems[0], "state-table-v1")) {
            uint8_t *canonical = NULL;
            size_t canonical_len = 0u;
            if (record->expr.len != 5u ||
                record->expr.elems[1]->kind != ATOM_SYMBOL) {
                set_error(error, error_size,
                          "state-transition table record %zu has the wrong shape",
                          index + 1u);
                goto done;
            }
            if (!fh_ground_term_v1_render(
                    record->expr.elems[1], &canonical, &canonical_len,
                    error, error_size) ||
                !canonical_answer_push(
                    &tables, &table_len, &table_cap,
                    (char *)canonical, canonical_len,
                    error, error_size)) {
                free(canonical);
                goto done;
            }
            if (!state_transition_collect_numerals(
                    record->expr.elems[2], &numerals, 0u,
                    error, error_size) ||
                !state_transition_collect_numerals(
                    record->expr.elems[3], &numerals, 0u,
                    error, error_size))
                goto done;
        } else if (atom_is_symbol(record->expr.elems[0],
                                  "state-action-v1")) {
            uint8_t *operation = NULL;
            size_t operation_len = 0u;
            uint32_t action_index;
            if (record->expr.len != 4u) {
                set_error(error, error_size,
                          "state-transition action record %zu has the wrong shape",
                          index + 1u);
                goto done;
            }
            if (!fh_ground_term_v1_render(
                    record->expr.elems[1], &operation, &operation_len,
                    error, error_size) ||
                !state_transition_symbol_u32(
                    record->expr.elems[2], &action_index)) {
                free(operation);
                if (error && error[0] == '\0')
                    set_error(error, error_size,
                              "state-transition action record %zu has a non-u32 index",
                              index + 1u);
                goto done;
            }
            if (!state_transition_action_push(
                    &actions, (char *)operation, operation_len,
                    action_index, error, error_size)) {
                free(operation);
                goto done;
            }
            if (!state_transition_collect_numerals(
                    record->expr.elems[3], &numerals, 0u,
                    error, error_size))
                goto done;
        } else if (atom_is_symbol(record->expr.elems[0],
                                  "state-final-action-v1")) {
            uint32_t action_index;
            if (record->expr.len != 3u) {
                set_error(error, error_size,
                          "state-transition final-action record %zu has the wrong shape",
                          index + 1u);
                goto done;
            }
            if (!state_transition_symbol_u32(
                    record->expr.elems[1], &action_index) ||
                !state_transition_numeral_push(
                    &final_actions, action_index, error, error_size)) {
                if (error && error[0] == '\0')
                    set_error(error, error_size,
                              "state-transition final-action record %zu has a non-u32 index",
                              index + 1u);
                goto done;
            }
            if (!state_transition_collect_numerals(
                    record->expr.elems[2], &numerals, 0u,
                    error, error_size))
                goto done;
        }
    }
    if (table_len == 0u) {
        set_error(error, error_size,
                  "state-transition program declares no tables");
        goto done;
    }
    qsort(tables, table_len, sizeof(*tables), canonical_answer_compare);
    for (size_t index = 1u; index < table_len; index++) {
        if (strcmp(tables[index - 1u].text, tables[index].text) == 0) {
            set_error(error, error_size,
                      "state-transition table identities are not unique");
            goto done;
        }
    }
    qsort(numerals.values, numerals.len,
          sizeof(*numerals.values), state_transition_u32_compare);
    qsort(actions.values, actions.len,
          sizeof(*actions.values), state_transition_action_compare);
    qsort(final_actions.values, final_actions.len,
          sizeof(*final_actions.values), state_transition_u32_compare);
    if (!state_transition_answer_pushf(
            &answers, &answers_len, &answers_cap, error, error_size,
            "(StateTransitionPackageV1 %s)", package))
        goto done;
    for (size_t index = 0u; index < table_len; index++) {
        char *peano;
        char *next_peano = NULL;
        if (index > UINT32_MAX) {
            set_error(error, error_size,
                      "state-transition table inventory is too large");
            goto done;
        }
        peano = state_transition_peano(
            (uint32_t)index, error, error_size);
        if (!peano ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateDenseIdentityV1 %s StateTableKindV1 %s %s)",
                package, tables[index].text, peano)) {
            free(peano);
            goto done;
        }
        if (index == 0u &&
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap,
                error, error_size,
                "(StateTableFirstV1 %s %s)", package, peano)) {
            free(peano);
            goto done;
        }
        if (index + 1u < table_len) {
            if (index + 1u > UINT32_MAX) {
                free(peano);
                set_error(error, error_size,
                          "state-transition table inventory is too large");
                goto done;
            }
            next_peano = state_transition_peano(
                (uint32_t)(index + 1u), error, error_size);
            if (!next_peano ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(StateTableNextV1 %s %s %s)",
                    package, peano, next_peano)) {
                free(next_peano);
                free(peano);
                goto done;
            }
            free(next_peano);
        } else if (!state_transition_answer_pushf(
                       &answers, &answers_len, &answers_cap,
                       error, error_size,
                       "(StateTableLastV1 %s %s)", package, peano)) {
            free(peano);
            goto done;
        }
        free(peano);
    }
    for (size_t index = 0u; index < numerals.len; index++) {
        char *peano;
        if (index > 0u &&
            numerals.values[index - 1u] == numerals.values[index])
            continue;
        peano = state_transition_peano(
            numerals.values[index], error, error_size);
        if (!peano ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateNatBindingV1 %s %" PRIu32 " %s)",
                package, numerals.values[index], peano)) {
            free(peano);
            goto done;
        }
        free(peano);
    }
    for (size_t first = 0u; first < actions.len;) {
        size_t last = first + 1u;
        while (last < actions.len &&
               strcmp(actions.values[first].operation,
                      actions.values[last].operation) == 0)
            last++;
        for (size_t index = first; index < last; index++) {
            size_t expected = index - first;
            if (expected > UINT32_MAX ||
                actions.values[index].index != (uint32_t)expected) {
                set_error(
                    error, error_size,
                    "state-transition operation %s has non-dense action index %" PRIu32,
                    actions.values[first].operation,
                    actions.values[index].index);
                goto done;
            }
        }
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateActionFirstV1 %s %s 0)",
                package, actions.values[first].operation))
            goto done;
        for (size_t index = first; index + 1u < last; index++) {
            if (!state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(StateActionNextV1 %s %s %" PRIu32 " %" PRIu32 ")",
                    package, actions.values[first].operation,
                    actions.values[index].index,
                    actions.values[index + 1u].index))
                goto done;
        }
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateActionLastV1 %s %s %" PRIu32 ")",
                package, actions.values[first].operation,
                actions.values[last - 1u].index))
            goto done;
        first = last;
    }
    for (size_t index = 0u; index < final_actions.len; index++) {
        if (index > UINT32_MAX ||
            final_actions.values[index] != (uint32_t)index) {
            set_error(
                error, error_size,
                "state-transition final actions have non-dense index %" PRIu32,
                final_actions.values[index]);
            goto done;
        }
    }
    if (final_actions.len > 0u) {
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateFinalActionFirstV1 %s 0)", package))
            goto done;
        for (size_t index = 0u; index + 1u < final_actions.len; index++) {
            if (!state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(StateFinalActionNextV1 %s %" PRIu32 " %" PRIu32 ")",
                    package, final_actions.values[index],
                    final_actions.values[index + 1u]))
                goto done;
        }
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateFinalActionLastV1 %s %" PRIu32 ")",
                package, final_actions.values[final_actions.len - 1u]))
            goto done;
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    for (size_t index = 0u; index < answers_len; index++) {
        if (answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      "state-transition binding stream is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory publishing state-transition bindings");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text,
                   answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output, payload, payload_len, error, error_size) ||
        !fh_answer_stream_v1_read(
            &published, output, error, error_size) ||
        published.len != answers_len) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "state-transition bindings changed during publication");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, 65u);
    ok = true;

done:
    for (size_t index = 0u; index < table_len; index++)
        free(tables[index].text);
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(tables);
    free(answers);
    free(numerals.values);
    for (size_t index = 0u; index < actions.len; index++)
        free(actions.values[index].operation);
    free(actions.values);
    free(final_actions.values);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&stream);
    return ok;
}

typedef struct {
    const char *role;
    uint32_t value_id;
    const char *bytes_hex;
} StateOccurrenceValueV1;

typedef struct {
    char *operation;
    char *role;
    uint32_t index;
} StateOccurrenceRoleListRequestV1;

typedef struct {
    StateOccurrenceRoleListRequestV1 *values;
    size_t len;
    size_t cap;
} StateOccurrenceRoleListRequestsV1;

typedef struct {
    uint32_t *values;
    size_t len;
} StateOccurrenceListIdentityV1;

typedef struct {
    StateOccurrenceListIdentityV1 *values;
    size_t len;
    size_t cap;
} StateOccurrenceListIdentitiesV1;

typedef struct {
    const char **values;
    size_t len;
    size_t cap;
} StateOccurrenceEmptyRolesV1;

static int state_occurrence_text_pointer_compare_v1(
    const void *left, const void *right) {
    const char *const *left_text = left;
    const char *const *right_text = right;
    return strcmp(*left_text, *right_text);
}

static char *state_occurrence_render_v1(
    const Atom *term, char *error, size_t error_size) {
    uint8_t *text = NULL;
    size_t text_len = 0u;
    if (!fh_ground_term_v1_render(
            term, &text, &text_len, error, error_size))
        return NULL;
    return (char *)text;
}

static bool state_occurrence_collect_literals_v1(
    const Atom *term, LangDefCanonicalAnswerV1 **literals,
    size_t *literal_len, size_t *literal_cap, uint32_t depth,
    char *error, size_t error_size) {
    if (!term || depth > 4096u)
        return set_error(error, error_size,
                         "state occurrence literal term is too deep");
    if (term->kind != ATOM_EXPR)
        return true;
    if (term->expr.len == 2u &&
        atom_is_symbol(term->expr.elems[0], "state-literal-v1")) {
        char *literal = state_occurrence_render_v1(
            term->expr.elems[1], error, error_size);
        size_t literal_size;
        if (!literal)
            return false;
        literal_size = strlen(literal);
        for (size_t index = 0u; index < *literal_len; index++) {
            if (strcmp((*literals)[index].text, literal) == 0) {
                free(literal);
                return true;
            }
        }
        if (!canonical_answer_push(
                literals, literal_len, literal_cap,
                literal, literal_size, error, error_size)) {
            free(literal);
            return false;
        }
        return true;
    }
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!state_occurrence_collect_literals_v1(
                term->expr.elems[index], literals,
                literal_len, literal_cap, depth + 1u,
                error, error_size))
            return false;
    }
    return true;
}

static bool state_occurrence_request_push_v1(
    StateOccurrenceRoleListRequestsV1 *requests,
    const char *operation, const char *role, uint32_t index,
    char *error, size_t error_size) {
    StateOccurrenceRoleListRequestV1 *next;
    size_t next_cap;
    for (size_t prior = 0u; prior < requests->len; prior++) {
        if (requests->values[prior].index == index &&
            strcmp(requests->values[prior].operation, operation) == 0 &&
            strcmp(requests->values[prior].role, role) == 0)
            return true;
    }
    if (requests->len == requests->cap) {
        next_cap = requests->cap ? requests->cap * 2u : 16u;
        if (next_cap < requests->cap ||
            next_cap > SIZE_MAX / sizeof(*requests->values))
            return set_error(error, error_size,
                             "state occurrence role-list inventory is too large");
        next = realloc(requests->values, next_cap * sizeof(*next));
        if (!next)
            return set_error(error, error_size,
                             "out of memory collecting state occurrence role lists");
        requests->values = next;
        requests->cap = next_cap;
    }
    requests->values[requests->len].operation = strdup(operation);
    requests->values[requests->len].role = strdup(role);
    requests->values[requests->len].index = index;
    if (!requests->values[requests->len].operation ||
        !requests->values[requests->len].role) {
        free(requests->values[requests->len].operation);
        free(requests->values[requests->len].role);
        return set_error(error, error_size,
                         "out of memory recording state occurrence role list");
    }
    requests->len++;
    return true;
}

static bool state_occurrence_collect_requests_v1(
    const Atom *term, const char *operation,
    StateOccurrenceRoleListRequestsV1 *requests, uint32_t depth,
    char *error, size_t error_size) {
    if (!term || depth > 4096u)
        return set_error(error, error_size,
                         "state occurrence role-list term is too deep");
    if (term->kind != ATOM_EXPR)
        return true;
    if (term->expr.len == 3u &&
        atom_is_symbol(term->expr.elems[0], "state-role-list-v1")) {
        char *role;
        uint32_t index;
        bool ok;
        if (!state_transition_symbol_u32(term->expr.elems[2], &index))
            return set_error(error, error_size,
                             "state occurrence role-list index is not a u32");
        role = state_occurrence_render_v1(
            term->expr.elems[1], error, error_size);
        if (!role)
            return false;
        ok = state_occurrence_request_push_v1(
            requests, operation, role, index, error, error_size);
        free(role);
        return ok;
    }
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!state_occurrence_collect_requests_v1(
                term->expr.elems[index], operation, requests,
                depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static bool state_occurrence_parse_values_v1(
    const Atom *list, StateOccurrenceValueV1 **values,
    size_t *value_len, size_t *value_cap,
    char **bytes_by_id, bool *seen_id, uint32_t unique_value_len,
    char *error, size_t error_size) {
    const Atom *cursor = list;
    uint32_t depth = 0u;
    while (!atom_is_symbol((Atom *)cursor,
                           "ParserOccurrenceValuesNilV1")) {
        Atom *value;
        Atom *bytes;
        StateOccurrenceValueV1 *next;
        size_t next_cap;
        uint32_t value_id;
        uint32_t ignored;
        const char *role;
        const char *hex;
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !atom_is_symbol(cursor->expr.elems[0],
                            "ParserOccurrenceValuesConsV1") ||
            !(value = cursor->expr.elems[1]) ||
            value->kind != ATOM_EXPR || value->expr.len != 9u ||
            !atom_is_symbol(value->expr.elems[0],
                            "ParserOccurrenceValueV1") ||
            value->expr.elems[1]->kind != ATOM_SYMBOL ||
            !state_transition_symbol_u32(value->expr.elems[2], &value_id) ||
            value_id >= unique_value_len) {
            return set_error(error, error_size,
                             "parser occurrence has a malformed value list");
        }
        for (CettaExprIndex field = 3u; field <= 7u; field++) {
            if (!state_transition_symbol_u32(
                    value->expr.elems[field], &ignored))
                return set_error(error, error_size,
                                 "parser occurrence value has a non-u32 coordinate");
        }
        bytes = value->expr.elems[8];
        if (!bytes || bytes->kind != ATOM_EXPR || bytes->expr.len != 2u ||
            !atom_is_symbol(bytes->expr.elems[0], "SourceBytesHexV1") ||
            bytes->expr.elems[1]->kind != ATOM_SYMBOL)
            return set_error(error, error_size,
                             "parser occurrence value has malformed source bytes");
        role = atom_name_cstr(value->expr.elems[1]);
        hex = atom_name_cstr(bytes->expr.elems[1]);
        if (strncmp(hex, "hex-", 4u) != 0)
            return set_error(error, error_size,
                             "parser occurrence value lacks canonical hex bytes");
        if (seen_id[value_id] &&
            strcmp(bytes_by_id[value_id], hex) != 0)
            return set_error(error, error_size,
                             "parser occurrence value id maps to different bytes");
        if (!seen_id[value_id]) {
            bytes_by_id[value_id] = strdup(hex);
            if (!bytes_by_id[value_id])
                return set_error(error, error_size,
                                 "out of memory validating occurrence interning");
            seen_id[value_id] = true;
        }
        if (*value_len == *value_cap) {
            next_cap = *value_cap ? *value_cap * 2u : 16u;
            if (next_cap < *value_cap ||
                next_cap > SIZE_MAX / sizeof(**values))
                return set_error(error, error_size,
                                 "parser occurrence value list is too large");
            next = realloc(*values, next_cap * sizeof(*next));
            if (!next)
                return set_error(error, error_size,
                                 "out of memory collecting parser occurrence values");
            *values = next;
            *value_cap = next_cap;
        }
        (*values)[*value_len] = (StateOccurrenceValueV1){
            .role = role,
            .value_id = value_id,
            .bytes_hex = hex,
        };
        (*value_len)++;
        cursor = cursor->expr.elems[2];
        if (++depth > 16u * 1024u * 1024u)
            return set_error(error, error_size,
                             "parser occurrence value list is too deep");
    }
    return true;
}

static bool state_occurrence_parse_empty_roles_v1(
    const Atom *list, StateOccurrenceEmptyRolesV1 *roles,
    char *error, size_t error_size) {
    const Atom *cursor = list;
    uint32_t depth = 0u;
    while (!atom_is_symbol(
               (Atom *)cursor, "ParserOccurrenceEmptyRolesNilV1")) {
        const char *role;
        const char **next;
        size_t next_cap;
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !atom_is_symbol(
                cursor->expr.elems[0],
                "ParserOccurrenceEmptyRolesConsV1") ||
            cursor->expr.elems[1]->kind != ATOM_SYMBOL) {
            return set_error(
                error, error_size,
                "parser occurrence has malformed empty roles");
        }
        role = atom_name_cstr(cursor->expr.elems[1]);
        for (size_t index = 0u; index < roles->len; index++) {
            if (strcmp(roles->values[index], role) == 0) {
                return set_error(
                    error, error_size,
                    "parser occurrence repeats an empty role");
            }
        }
        if (roles->len == roles->cap) {
            next_cap = roles->cap ? roles->cap * 2u : 4u;
            if (next_cap < roles->cap ||
                next_cap > SIZE_MAX / sizeof(*roles->values)) {
                return set_error(
                    error, error_size,
                    "parser occurrence empty-role inventory is too large");
            }
            next = realloc(roles->values, next_cap * sizeof(*next));
            if (!next) {
                return set_error(
                    error, error_size,
                    "out of memory collecting parser occurrence empty roles");
            }
            roles->values = next;
            roles->cap = next_cap;
        }
        roles->values[roles->len++] = role;
        cursor = cursor->expr.elems[2];
        if (++depth > 16u * 1024u * 1024u) {
            return set_error(
                error, error_size,
                "parser occurrence empty-role list is too deep");
        }
    }
    return true;
}

static bool state_occurrence_value_tree_v1(
    FILE *output, const uint32_t *values, size_t begin, size_t end) {
    size_t length = end - begin;
    size_t middle;
    if (length == 0u)
        return fputs("ValuesTreeEmptyV1", output) >= 0;
    if (length == 1u)
        return fprintf(
            output, "(ValuesTreeLeafV1 (StateSourceValueV1 %" PRIu32 "))",
            values[begin]) >= 0;
    middle = begin + length / 2u;
    return fputs("(ValuesTreeNodeV1 ", output) >= 0 &&
           state_occurrence_value_tree_v1(output, values, begin, middle) &&
           fputc(' ', output) != EOF &&
           state_occurrence_value_tree_v1(output, values, middle, end) &&
           fputc(')', output) != EOF;
}

static char *state_occurrence_value_list_text_v1(
    const uint32_t *values, size_t value_len,
    char *error, size_t error_size) {
    char *text = NULL;
    size_t text_len = 0u;
    FILE *output = open_memstream(&text, &text_len);
    if (!output) {
        set_error(error, error_size,
                  "cannot allocate state occurrence value-list renderer");
        return NULL;
    }
    if (fputs("(ValuesSequenceV1 ", output) < 0 ||
        !state_occurrence_value_tree_v1(
            output, values, 0u, value_len) ||
        fputs(" ValuesTreeStackNilV1)", output) < 0)
        goto failed;
    if (fclose(output) != 0) {
        free(text);
        set_error(error, error_size,
                  "cannot finish state occurrence value-list rendering");
        return NULL;
    }
    return text;

failed:
    (void)fclose(output);
    free(text);
    if (!error || error[0] == '\0')
        set_error(error, error_size,
                  "cannot render state occurrence value list");
    return NULL;
}

static int state_occurrence_hex_digit_v1(char digit) {
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    return -1;
}

static char *state_occurrence_byte_list_text_v1(
    const char *hex, char *error, size_t error_size) {
    char *text = NULL;
    size_t text_len = 0u;
    size_t hex_len;
    size_t byte_len;
    FILE *output;
    if (!hex || strncmp(hex, "hex-", 4u) != 0) {
        set_error(error, error_size,
                  "state occurrence source bytes lack canonical hex prefix");
        return NULL;
    }
    hex += 4u;
    hex_len = strlen(hex);
    if ((hex_len & 1u) != 0u) {
        set_error(error, error_size,
                  "state occurrence source bytes have odd-length hex");
        return NULL;
    }
    byte_len = hex_len / 2u;
    output = open_memstream(&text, &text_len);
    if (!output) {
        set_error(error, error_size,
                  "cannot allocate state occurrence byte-list renderer");
        return NULL;
    }
    for (size_t index = 0u; index < byte_len; index++) {
        int high = state_occurrence_hex_digit_v1(hex[index * 2u]);
        int low = state_occurrence_hex_digit_v1(hex[index * 2u + 1u]);
        if (high < 0 || low < 0 ||
            fprintf(output, "(ByteValuesConsV1 %u ",
                    (unsigned)((high << 4) | low)) < 0)
            goto failed;
    }
    if (fputs("ByteValuesNilV1", output) < 0)
        goto failed;
    for (size_t index = 0u; index < byte_len; index++) {
        if (fputc(')', output) == EOF)
            goto failed;
    }
    if (fclose(output) != 0) {
        free(text);
        set_error(error, error_size,
                  "cannot finish state occurrence byte-list rendering");
        return NULL;
    }
    return text;

failed:
    (void)fclose(output);
    free(text);
    if (!error || error[0] == '\0')
        set_error(error, error_size,
                  "cannot render state occurrence byte list");
    return NULL;
}

static bool state_occurrence_list_identity_v1(
    StateOccurrenceListIdentitiesV1 *identities,
    const StateOccurrenceValueV1 *values, size_t value_len,
    const char *role, bool allow_empty, uint32_t *identity_out,
    char *error, size_t error_size) {
    uint32_t *sequence = NULL;
    size_t sequence_len = 0u;
    size_t offset = 0u;
    StateOccurrenceListIdentityV1 *next;
    size_t next_cap;
    for (size_t index = 0u; index < value_len; index++) {
        if (strcmp(values[index].role, role) == 0)
            sequence_len++;
    }
    if (sequence_len == 0u && !allow_empty)
        return set_error(error, error_size,
                         "state operation requests an absent occurrence role list");
    if (sequence_len > SIZE_MAX / sizeof(*sequence))
        return set_error(error, error_size,
                         "state occurrence role list is too large");
    if (sequence_len > 0u)
        sequence = malloc(sequence_len * sizeof(*sequence));
    if (sequence_len > 0u && !sequence)
        return set_error(error, error_size,
                         "out of memory interning state occurrence role list");
    for (size_t index = 0u; index < value_len; index++) {
        if (strcmp(values[index].role, role) == 0)
            sequence[offset++] = values[index].value_id;
    }
    for (size_t index = 0u; index < identities->len; index++) {
        if (identities->values[index].len == sequence_len &&
            (sequence_len == 0u ||
             memcmp(identities->values[index].values, sequence,
                    sequence_len * sizeof(*sequence)) == 0)) {
            free(sequence);
            if (index > UINT32_MAX)
                return set_error(error, error_size,
                                 "state occurrence list identity is too large");
            *identity_out = (uint32_t)index;
            return true;
        }
    }
    if (identities->len == identities->cap) {
        next_cap = identities->cap ? identities->cap * 2u : 16u;
        if (next_cap < identities->cap ||
            next_cap > SIZE_MAX / sizeof(*identities->values)) {
            free(sequence);
            return set_error(error, error_size,
                             "state occurrence list identity inventory is too large");
        }
        next = realloc(identities->values, next_cap * sizeof(*next));
        if (!next) {
            free(sequence);
            return set_error(error, error_size,
                             "out of memory growing state occurrence list identities");
        }
        identities->values = next;
        identities->cap = next_cap;
    }
    if (identities->len > UINT32_MAX) {
        free(sequence);
        return set_error(error, error_size,
                         "state occurrence list identity is too large");
    }
    *identity_out = (uint32_t)identities->len;
    identities->values[identities->len++] =
        (StateOccurrenceListIdentityV1){sequence, sequence_len};
    return true;
}

/* Reify a captured occurrence stream for differential inspection.  Runtime
 * source files belong on the parser-to-constructor path, not this command. */
static bool state_occurrence_projection_oracle_command_v1(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *occurrence_source = NULL;
    const char *state_source = NULL;
    const char *output = NULL;
    FHAnswerStreamV1 occurrences;
    FHAnswerStreamV1 state_program;
    FHAnswerStreamV1 published;
    Atom *header = NULL;
    Atom **ordered = NULL;
    uint32_t step_len = 0u;
    uint32_t expected_value_len = 0u;
    uint32_t unique_value_len = 0u;
    uint32_t source_len = 1u;
    uint32_t observed_value_len = 0u;
    char **bytes_by_id = NULL;
    char **sorted_bytes = NULL;
    bool *seen_id = NULL;
    LangDefCanonicalAnswerV1 *literals = NULL;
    size_t literal_len = 0u;
    size_t literal_cap = 0u;
    StateOccurrenceRoleListRequestsV1 requests = {0};
    StateOccurrenceListIdentitiesV1 list_identities = {0};
    LangDefCanonicalAnswerV1 *answers = NULL;
    size_t answers_len = 0u;
    size_t answers_cap = 0u;
    uint8_t *payload = NULL;
    size_t payload_len = 0u;
    char *plan_digest = NULL;
    char *source_program_digest = NULL;
    char *source_digest = NULL;
    char package[128];
    char *stream = NULL;
    bool source_graph = false;
    bool explicit_empty_roles = false;
    bool ok = false;

    fh_answer_stream_v1_init(&occurrences);
    fh_answer_stream_v1_init(&state_program);
    fh_answer_stream_v1_init(&published);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "state-occurrence-projection-oracle option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--occurrences") == 0 && !occurrence_source)
            occurrence_source = value;
        else if (strcmp(option, "--state-program") == 0 && !state_source)
            state_source = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated state-occurrence-projection-oracle option");
            goto done;
        }
    }
    if (!occurrence_source || !state_source || !output) {
        set_error(error, error_size,
                  "state-occurrence-projection-oracle requires occurrences, state program, and output");
        goto done;
    }
    if (!fh_answer_stream_v1_read(
            &occurrences, occurrence_source, error, error_size) ||
        !fh_answer_stream_v1_read(
            &state_program, state_source, error, error_size))
        goto done;
    for (size_t index = 0u; index < occurrences.len; index++) {
        Atom *record = occurrences.terms[index];
        bool v1 = record && record->kind == ATOM_EXPR &&
            record->expr.len == 6u &&
            atom_is_symbol(record->expr.elems[0],
                           "ParserOccurrenceStreamV1");
        bool v2 = record && record->kind == ATOM_EXPR &&
            record->expr.len == 8u &&
            atom_is_symbol(record->expr.elems[0],
                           "ParserOccurrenceStreamV2");
        bool v3 = record && record->kind == ATOM_EXPR &&
            record->expr.len == 6u &&
            atom_is_symbol(record->expr.elems[0],
                           "ParserOccurrenceStreamV3");
        bool v4 = record && record->kind == ATOM_EXPR &&
            record->expr.len == 8u &&
            atom_is_symbol(record->expr.elems[0],
                           "ParserOccurrenceStreamV4");
        if (v1 || v2 || v3 || v4) {
            if (header) {
                set_error(error, error_size,
                          "parser occurrence stream repeats its header");
                goto done;
            }
            header = record;
            source_graph = v2 || v4;
            explicit_empty_roles = v3 || v4;
        }
    }
    if (!header ||
        !state_transition_symbol_u32(
            header->expr.elems[source_graph ? 4u : 3u], &step_len) ||
        !state_transition_symbol_u32(
            header->expr.elems[source_graph ? 5u : 4u],
            &expected_value_len) ||
        !state_transition_symbol_u32(
            header->expr.elems[source_graph ? 6u : 5u],
            &unique_value_len) ||
        (source_graph &&
         (!state_transition_symbol_u32(
              header->expr.elems[7], &source_len) ||
          source_len == 0u))) {
        set_error(error, error_size,
                  "parser occurrence stream has a malformed header");
        goto done;
    }
    ordered = calloc(step_len ? step_len : 1u, sizeof(*ordered));
    bytes_by_id = calloc(
        unique_value_len ? unique_value_len : 1u, sizeof(*bytes_by_id));
    seen_id = calloc(
        unique_value_len ? unique_value_len : 1u, sizeof(*seen_id));
    if (!ordered || !bytes_by_id || !seen_id) {
        set_error(error, error_size,
                  "out of memory indexing parser occurrence stream");
        goto done;
    }
    for (size_t index = 0u; index < occurrences.len; index++) {
        Atom *record = occurrences.terms[index];
        uint32_t occurrence_index;
        if (record == header)
            continue;
        bool expected_record = record && record->kind == ATOM_EXPR &&
            ((source_graph && explicit_empty_roles &&
              record->expr.len == 13u &&
              atom_is_symbol(record->expr.elems[0],
                             "ParserOccurrenceV4")) ||
             (source_graph && !explicit_empty_roles &&
              record->expr.len == 12u &&
              atom_is_symbol(record->expr.elems[0],
                             "ParserOccurrenceV2")) ||
             (!source_graph && explicit_empty_roles &&
              record->expr.len == 12u &&
              atom_is_symbol(record->expr.elems[0],
                             "ParserOccurrenceV3")) ||
             (!source_graph && !explicit_empty_roles &&
              record->expr.len == 11u &&
              atom_is_symbol(record->expr.elems[0],
                             "ParserOccurrenceV1")));
        if (!expected_record ||
            !state_transition_symbol_u32(
                record->expr.elems[1], &occurrence_index) ||
            occurrence_index >= step_len || ordered[occurrence_index]) {
            set_error(error, error_size,
                      "parser occurrence stream has a malformed or repeated occurrence");
            goto done;
        }
        ordered[occurrence_index] = record;
    }
    for (uint32_t index = 0u; index < step_len; index++) {
        if (!ordered[index]) {
            set_error(error, error_size,
                      "parser occurrence stream indices are not dense");
            goto done;
        }
    }
    for (size_t index = 0u; index < state_program.len; index++) {
        Atom *record = state_program.terms[index];
        if (!state_occurrence_collect_literals_v1(
                record, &literals, &literal_len, &literal_cap,
                0u, error, error_size))
            goto done;
        if (record && record->kind == ATOM_EXPR && record->expr.len == 4u &&
            atom_is_symbol(record->expr.elems[0], "state-action-v1")) {
            char *operation = state_occurrence_render_v1(
                record->expr.elems[1], error, error_size);
            if (!operation ||
                !state_occurrence_collect_requests_v1(
                    record->expr.elems[3], operation, &requests,
                    0u, error, error_size)) {
                free(operation);
                goto done;
            }
            free(operation);
        }
    }
    qsort(literals, literal_len, sizeof(*literals), canonical_answer_compare);
    plan_digest = state_occurrence_render_v1(
        header->expr.elems[1], error, error_size);
    if (source_graph) {
        char expected[72];
        int written;

        source_program_digest = state_occurrence_render_v1(
            header->expr.elems[2], error, error_size);
        written = snprintf(
            expected, sizeof(expected), "sha256-%s", state_program.digest);
        if (!source_program_digest || written < 0 ||
            (size_t)written >= sizeof(expected) ||
            strcmp(source_program_digest, expected) != 0) {
            set_error(error, error_size,
                      "source composition and state program digests disagree");
            goto done;
        }
    }
    source_digest = state_occurrence_render_v1(
        header->expr.elems[source_graph ? 3u : 2u], error, error_size);
    if (!plan_digest || !source_digest)
        goto done;
    {
        int written = snprintf(
            package, sizeof(package),
            "(StateProgramDigestV1 sha256-%s)", state_program.digest);
        int stream_len;
        if (written < 0 || (size_t)written >= sizeof(package)) {
            set_error(error, error_size,
                      "state occurrence package identity is too large");
            goto done;
        }
        stream_len = snprintf(
            NULL, 0u, "(StateOccurrenceStreamIdentityV1 %s %s %s)",
            plan_digest, source_digest, package);
        if (stream_len < 0) {
            set_error(error, error_size,
                      "cannot format state occurrence stream identity");
            goto done;
        }
        stream = malloc((size_t)stream_len + 1u);
        if (!stream || snprintf(
                stream, (size_t)stream_len + 1u,
                "(StateOccurrenceStreamIdentityV1 %s %s %s)",
                plan_digest, source_digest, package) != stream_len) {
            set_error(error, error_size,
                      "out of memory formatting state occurrence stream identity");
            goto done;
        }
    }
    if (!state_transition_answer_pushf(
            &answers, &answers_len, &answers_cap, error, error_size,
            "(StateOccurrenceProgramV1 %s %s)", stream, package))
        goto done;
    if (step_len == 0u) {
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateOccurrenceEmptyV1 %s)", stream) ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateOccurrenceShapeV1 %s "
                "StateOccurrenceEmptyShapeV1)", stream))
            goto done;
    }
    for (size_t index = 0u; index < literal_len; index++) {
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StreamLiteralValueV1 %s %s (StateLiteralValueV1 %s))",
                stream, literals[index].text, literals[index].text)) {
            goto done;
        }
    }
    for (uint32_t index = 0u; index < step_len; index++) {
        Atom *record = ordered[index];
        StateOccurrenceValueV1 *values = NULL;
        StateOccurrenceEmptyRolesV1 empty_roles = {0};
        size_t value_len = 0u;
        size_t value_cap = 0u;
        char *operation = NULL;
        char *occurrence = NULL;
        char *source_identity = NULL;
        uint32_t source_id = 0u;
        uint32_t coordinates[4];
        CettaExprIndex kind_field = source_graph ? 3u : 2u;
        CettaExprIndex operation_field = source_graph ? 4u : 3u;
        CettaExprIndex coordinate_field = source_graph ? 7u : 6u;
        CettaExprIndex values_field = source_graph ? 11u : 10u;
        if ((source_graph &&
             (!state_transition_symbol_u32(
                  record->expr.elems[2], &source_id) ||
              source_id >= source_len)) ||
            record->expr.elems[operation_field]->kind != ATOM_SYMBOL ||
            record->expr.elems[kind_field]->kind != ATOM_SYMBOL ||
            (!atom_is_symbol(record->expr.elems[kind_field],
                             "ParserOccurrenceReduceV1") &&
             !atom_is_symbol(record->expr.elems[kind_field],
                             "ParserOccurrenceShiftV1"))) {
            set_error(error, error_size,
                      "parser occurrence has an invalid kind or operation");
            goto occurrence_done;
        }
        for (size_t coordinate = 0u; coordinate < 4u; coordinate++) {
            if (!state_transition_symbol_u32(
                    record->expr.elems[coordinate_field + coordinate],
                    &coordinates[coordinate])) {
                set_error(error, error_size,
                          "parser occurrence has a non-u32 source position");
                goto occurrence_done;
            }
        }
        if (!state_occurrence_parse_values_v1(
                record->expr.elems[values_field],
                &values, &value_len, &value_cap,
                bytes_by_id, seen_id, unique_value_len,
                error, error_size))
            goto occurrence_done;
        if (explicit_empty_roles &&
            !state_occurrence_parse_empty_roles_v1(
                record->expr.elems[values_field + 1u],
                &empty_roles, error, error_size)) {
            goto occurrence_done;
        }
        for (size_t empty_index = 0u;
             empty_index < empty_roles.len; empty_index++) {
            for (size_t value_index = 0u;
                 value_index < value_len; value_index++) {
                if (strcmp(
                        empty_roles.values[empty_index],
                        values[value_index].role) == 0) {
                    set_error(
                        error, error_size,
                        "parser occurrence role is both empty and populated");
                    goto occurrence_done;
                }
            }
        }
        if (value_len > UINT32_MAX - observed_value_len) {
            set_error(error, error_size,
                      "parser occurrence value inventory is too large");
            goto occurrence_done;
        }
        observed_value_len += (uint32_t)value_len;
        operation = state_occurrence_render_v1(
            record->expr.elems[operation_field], error, error_size);
        if (!operation)
            goto occurrence_done;
        if (source_graph) {
            int needed = snprintf(
                NULL, 0u, "(SourceIdentityV1 %s %" PRIu32 ")",
                source_digest, source_id);
            if (needed < 0) {
                set_error(error, error_size,
                          "cannot format occurrence source identity");
                goto occurrence_done;
            }
            source_identity = malloc((size_t)needed + 1u);
            if (!source_identity || snprintf(
                    source_identity, (size_t)needed + 1u,
                    "(SourceIdentityV1 %s %" PRIu32 ")",
                    source_digest, source_id) != needed) {
                set_error(error, error_size,
                          "out of memory formatting occurrence source identity");
                goto occurrence_done;
            }
        } else {
            source_identity = strdup(source_digest);
            if (!source_identity) {
                set_error(error, error_size,
                          "out of memory copying occurrence source identity");
                goto occurrence_done;
            }
        }
        {
            int needed = snprintf(
                NULL, 0u, "(StateOccurrenceIdentityV1 %s %" PRIu32 ")",
                stream, index);
            if (needed < 0) {
                set_error(error, error_size,
                          "cannot format state occurrence identity");
                goto occurrence_done;
            }
            occurrence = malloc((size_t)needed + 1u);
            if (!occurrence || snprintf(
                    occurrence, (size_t)needed + 1u,
                    "(StateOccurrenceIdentityV1 %s %" PRIu32 ")",
                    stream, index) != needed) {
                set_error(error, error_size,
                          "out of memory formatting state occurrence identity");
                goto occurrence_done;
            }
        }
        if (!state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(OccurrenceStreamOfV1 %s %s)", occurrence, stream) ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(StateOccurrenceOperationV1 %s %s)",
                occurrence, operation) ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap, error, error_size,
                "(OccurrencePositionV1 %s "
                "(SourcePositionV1 %s %" PRIu32 " %" PRIu32
                " %" PRIu32 " %" PRIu32 "))",
                occurrence, source_identity,
                coordinates[0], coordinates[1],
                coordinates[2], coordinates[3]))
            goto occurrence_done;
        if (index == 0u) {
            if (!state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap, error, error_size,
                    "(StateOccurrenceFirstV1 %s %s)", stream, occurrence) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap, error, error_size,
                    "(StateOccurrenceShapeV1 %s "
                    "(StateOccurrenceNonemptyShapeV1 %s))",
                    stream, occurrence))
                goto occurrence_done;
        }
        if (index + 1u < step_len) {
            if (!state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap, error, error_size,
                    "(StateOccurrenceNextV1 %s %s "
                    "(StateOccurrenceIdentityV1 %s %" PRIu32 "))",
                    stream, occurrence, stream, index + 1u) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap, error, error_size,
                    "(StateOccurrenceTailV1 %s %s "
                    "(StateOccurrenceNextTailV1 "
                    "(StateOccurrenceIdentityV1 %s %" PRIu32 ")))",
                    stream, occurrence, stream, index + 1u))
                goto occurrence_done;
        } else {
            if (!state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(StateOccurrenceLastV1 %s %s)",
                    stream, occurrence) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(StateOccurrenceTailV1 %s %s "
                    "StateOccurrenceLastTailV1)",
                    stream, occurrence))
                goto occurrence_done;
        }
        for (size_t value_index = 0u; value_index < value_len; value_index++) {
            uint32_t list_identity;
            bool seen_role = false;
            for (size_t prior = 0u; prior < value_index; prior++) {
                if (strcmp(values[prior].role, values[value_index].role) == 0) {
                    seen_role = true;
                    break;
                }
            }
            if (seen_role)
                continue;
            if (!state_occurrence_list_identity_v1(
                    &list_identities, values, value_len,
                    values[value_index].role, false, &list_identity,
                    error, error_size) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(OccurrenceRoleValuesV1 %s %s "
                    "(StateValueSequenceV1 %s %" PRIu32 "))",
                    occurrence, values[value_index].role,
                    stream, list_identity) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(OccurrenceRoleSymbolsV1 %s %s "
                    "(StateSymbolSequenceV1 %s %s))",
                    occurrence, values[value_index].role,
                    occurrence, values[value_index].role) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(InterningInjectiveOnV1 "
                    "(StateSymbolSequenceV1 %s %s) "
                    "(StateValueSequenceV1 %s %" PRIu32 "))",
                    occurrence, values[value_index].role,
                    stream, list_identity)) {
                goto occurrence_done;
            }
        }
        for (size_t empty_index = 0u;
             empty_index < empty_roles.len; empty_index++) {
            uint32_t list_identity;
            const char *role = empty_roles.values[empty_index];
            if (!state_occurrence_list_identity_v1(
                    &list_identities, values, value_len,
                    role, true, &list_identity,
                    error, error_size) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(OccurrenceRoleValuesV1 %s %s "
                    "(StateValueSequenceV1 %s %" PRIu32 "))",
                    occurrence, role, stream, list_identity) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(OccurrenceRoleSymbolsV1 %s %s "
                    "(StateSymbolSequenceV1 %s %s))",
                    occurrence, role, occurrence, role) ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(InterningInjectiveOnV1 "
                    "(StateSymbolSequenceV1 %s %s) "
                    "(StateValueSequenceV1 %s %" PRIu32 "))",
                    occurrence, role, stream, list_identity)) {
                goto occurrence_done;
            }
        }
        for (size_t request = 0u; request < requests.len; request++) {
            uint32_t list_identity;
            char *index_peano;
            if (strcmp(requests.values[request].operation, operation) != 0)
                continue;
            if (!state_occurrence_list_identity_v1(
                    &list_identities, values, value_len,
                    requests.values[request].role, false, &list_identity,
                    error, error_size))
                goto occurrence_done;
            index_peano = state_transition_peano(
                requests.values[request].index, error, error_size);
            if (!index_peano ||
                !state_transition_answer_pushf(
                    &answers, &answers_len, &answers_cap,
                    error, error_size,
                    "(OccurrenceRoleListValueV1 %s %s %s "
                    "(StateListValueV1 %" PRIu32 "))",
                    occurrence, requests.values[request].role,
                    index_peano, list_identity)) {
                free(index_peano);
                goto occurrence_done;
            }
            free(index_peano);
        }
        free(source_identity);
        free(occurrence);
        free(operation);
        free(values);
        free(empty_roles.values);
        continue;

occurrence_done:
        free(source_identity);
        free(occurrence);
        free(operation);
        free(values);
        free(empty_roles.values);
        goto done;
    }
    if (observed_value_len != expected_value_len) {
        set_error(error, error_size,
                  "parser occurrence value count disagrees with its header");
        goto done;
    }
    for (uint32_t index = 0u; index < unique_value_len; index++) {
        if (!seen_id[index]) {
            set_error(error, error_size,
                      "parser occurrence value ids are not dense");
            goto done;
        }
    }
    if (unique_value_len > SIZE_MAX / sizeof(*sorted_bytes)) {
        set_error(error, error_size,
                  "parser occurrence value inventory is too large to validate");
        goto done;
    }
    sorted_bytes = malloc(
        (unique_value_len ? unique_value_len : 1u) * sizeof(*sorted_bytes));
    if (!sorted_bytes) {
        set_error(error, error_size,
                  "out of memory validating occurrence value injectivity");
        goto done;
    }
    for (uint32_t index = 0u; index < unique_value_len; index++)
        sorted_bytes[index] = bytes_by_id[index];
    qsort(sorted_bytes, unique_value_len, sizeof(*sorted_bytes),
          state_occurrence_text_pointer_compare_v1);
    for (uint32_t index = 1u; index < unique_value_len; index++) {
        if (strcmp(sorted_bytes[index - 1u], sorted_bytes[index]) == 0) {
            set_error(error, error_size,
                      "distinct parser occurrence value ids map to equal bytes");
            goto done;
        }
    }
    for (uint32_t index = 0u; index < unique_value_len; index++) {
        char *byte_list = state_occurrence_byte_list_text_v1(
            bytes_by_id[index], error, error_size);
        if (!byte_list ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap,
                error, error_size,
                "(StreamValueBytesV1 %s (StateSourceValueV1 %" PRIu32 ") %s)",
                stream, index, byte_list)) {
            free(byte_list);
            goto done;
        }
        free(byte_list);
    }
    for (size_t index = 0u; index < list_identities.len; index++) {
        char *value_list;
        if (index > UINT32_MAX) {
            set_error(error, error_size,
                      "state occurrence list inventory is too large");
            goto done;
        }
        value_list = state_occurrence_value_list_text_v1(
            list_identities.values[index].values,
            list_identities.values[index].len,
            error, error_size);
        if (!value_list ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap,
                error, error_size,
                "(ValueSequenceRealizationV1 "
                "(StateValueSequenceV1 %s %" PRIu32 ") %s)",
                stream, (uint32_t)index, value_list) ||
            !state_transition_answer_pushf(
                &answers, &answers_len, &answers_cap,
                error, error_size,
                "(StreamListValueV1 %s (StateListValueV1 %" PRIu32 ") "
                "(StateValueSequenceV1 %s %" PRIu32 "))",
                stream, (uint32_t)index, stream, (uint32_t)index)) {
            free(value_list);
            goto done;
        }
        free(value_list);
    }
    qsort(answers, answers_len, sizeof(*answers), canonical_answer_compare);
    {
        size_t unique_len = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            if (unique_len > 0u &&
                strcmp(answers[unique_len - 1u].text,
                       answers[index].text) == 0) {
                free(answers[index].text);
                continue;
            }
            if (unique_len != index)
                answers[unique_len] = answers[index];
            unique_len++;
        }
        answers_len = unique_len;
    }
    for (size_t index = 0u; index < answers_len; index++) {
        if (answers[index].len > SIZE_MAX - payload_len - 1u) {
            set_error(error, error_size,
                      "state occurrence answer stream is too large");
            goto done;
        }
        payload_len += answers[index].len + 1u;
    }
    payload = malloc(payload_len ? payload_len : 1u);
    if (!payload) {
        set_error(error, error_size,
                  "out of memory publishing state occurrences");
        goto done;
    }
    {
        size_t offset = 0u;
        for (size_t index = 0u; index < answers_len; index++) {
            memcpy(payload + offset, answers[index].text, answers[index].len);
            offset += answers[index].len;
            payload[offset++] = (uint8_t)'\n';
        }
    }
    if (!write_atomic(output, payload, payload_len, error, error_size) ||
        !fh_answer_stream_v1_read(
            &published, output, error, error_size) ||
        published.len != answers_len) {
        if (error && error[0] == '\0')
            set_error(error, error_size,
                      "state occurrence stream changed during publication");
        goto done;
    }
    *answer_len = published.len;
    memcpy(answer_digest, published.digest, 65u);
    ok = true;

done:
    for (uint32_t index = 0u; index < unique_value_len; index++)
        free(bytes_by_id ? bytes_by_id[index] : NULL);
    for (size_t index = 0u; index < literal_len; index++)
        free(literals[index].text);
    for (size_t index = 0u; index < requests.len; index++) {
        free(requests.values[index].operation);
        free(requests.values[index].role);
    }
    for (size_t index = 0u; index < list_identities.len; index++)
        free(list_identities.values[index].values);
    for (size_t index = 0u; index < answers_len; index++)
        free(answers[index].text);
    free(plan_digest);
    free(source_program_digest);
    free(source_digest);
    free(stream);
    free(ordered);
    free(bytes_by_id);
    free(sorted_bytes);
    free(seen_id);
    free(literals);
    free(requests.values);
    free(list_identities.values);
    free(answers);
    free(payload);
    fh_answer_stream_v1_free(&published);
    fh_answer_stream_v1_free(&state_program);
    fh_answer_stream_v1_free(&occurrences);
    return ok;
}

static bool empty_answer_stream_command(
    int argc, char **argv, size_t *answer_len, char answer_digest[65],
    char *error, size_t error_size) {
    const char *output = NULL;
    FHAnswerStreamV1 stream;
    bool ok = false;
    fh_answer_stream_v1_init(&stream);
    if (argc == 4 && strcmp(argv[2], "--out") == 0)
        output = argv[3];
    if (!output) {
        set_error(error, error_size,
                  "empty-answers requires exactly --out FILE");
        goto done;
    }
    if (!write_atomic(output, NULL, 0u, error, error_size) ||
        !fh_answer_stream_v1_read(
            &stream, output, error, error_size) || stream.len != 0u)
        goto done;
    *answer_len = 0u;
    memcpy(answer_digest, stream.digest, 65u);
    ok = true;
done:
    fh_answer_stream_v1_free(&stream);
    return ok;
}

static bool empty_guard_evidence_command(
    int argc, char **argv, char *error, size_t error_size) {
    const char *abi = NULL;
    const char *output = NULL;
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    char empty_digest[65];
    char payload[512];
    int payload_len;
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    for (int index = 2; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            set_error(error, error_size,
                      "empty-guard-evidence option lacks a value");
            goto done;
        }
        value = argv[++index];
        if (strcmp(option, "--abi") == 0 && !abi)
            abi = value;
        else if (strcmp(option, "--out") == 0 && !output)
            output = value;
        else {
            set_error(error, error_size,
                      "invalid or repeated empty-guard-evidence option");
            goto done;
        }
    }
    if (!abi || !output ||
        !ppabi_v1_wire_read(&wire, abi, error, error_size) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, error_size))
        goto done;
    cetta_native_sha256_hex(
        (const uint8_t *)"FiniteHornAnswerSetV1\n",
        sizeof("FiniteHornAnswerSetV1\n") - 1u,
        empty_digest);
    payload_len = snprintf(
        payload, sizeof(payload),
        "parser-pack-positive-guard-evidence-v1\n"
        "source-digest\t%s\n"
        "pre-reflection-digest\t%s\n"
        "environment-digest\t%s\n"
        "answer-set-digest\t%s\n"
        "end\n",
        wire.source_digest, wire.compiler_digest,
        wire.environment_digest, empty_digest);
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload) ||
        !write_atomic(output, (const uint8_t *)payload,
                      (size_t)payload_len, error, error_size))
        goto done;
    ok = true;
done:
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

static void usage(const char *program) {
    fprintf(stderr,
            "usage:\n"
            "  %s equations --source FILE --out FILE\n"
            "  %s petta-direct (--source FILE... | --composition FILE) "
            "[--entry-mode RELATION:BITS... | "
            "--closed-entry-mode RELATION:BITS...] "
            "[--epilogue FILE] --out FILE\n"
            "  %s rhometta-direct (--source FILE... | --composition FILE) "
            "--target PRESENTATION... "
            "[--entry-rule NAME...] [--epilogue FILE] --out FILE\n"
            "  %s answers --chart FILE --source FILE... "
            "[--reflect-source FILE...] --query TERM --out FILE\n"
            "  %s oslf-ntt --composition FILE --chart FILE "
            "--reflection FILE --compiler FILE --out FILE "
            "[--timeout SECONDS]\n"
            "  %s lexical-answers --chart FILE --source FILE... "
            "[--reflect-source FILE...] --roots FILE --out FILE\n"
            "  %s transparent-inline --abi FILE --out FILE "
            "[--max-paths N --max-productions N]\n"
            "  %s pack-facts --abi FILE [--source-abi FILE] --out FILE\n"
            "  %s answer-facts --source FILE --out FILE\n"
            "  %s semantic-gslt --source FILE --out FILE\n"
            "  %s merge-answers --source FILE... --out FILE\n"
            "  %s project-answers --source FILE --head SYMBOL "
            "--arg INDEX --out FILE\n"
            "  %s select-answers --source FILE --head SYMBOL --out FILE\n"
            "  %s state-transition-bindings --source FILE --out FILE\n"
            "  %s state-occurrence-projection-oracle --occurrences FILE "
            "--state-program FILE --out FILE\n"
            "  %s oracle-horn-shape --source FILE...\n"
            "  %s oracle-petta-horn --runtime FILE --source FILE... "
            "[--epilogue FILE] --out FILE --receipt-out FILE\n"
            "  %s oracle-petta-horn-check --runtime FILE --source FILE... "
            "[--epilogue FILE] --program FILE --receipt FILE\n"
            "  %s empty-answers --out FILE\n"
            "  %s empty-guard-evidence --abi FILE --out FILE\n"
            "  %s parser-pack --manifest FILE --compiler-root DIR "
            "--presentation-root DIR [--pack-out FILE --lock-out FILE]\n"
            "  %s seal --manifest FILE [--pack FILE] "
            "[--compiled-cursor FILE] [--lock-out FILE]\n"
            "  %s validate --manifest FILE\n",
            program, program, program, program, program, program, program,
            program, program, program, program, program, program, program,
            program, program, program, program, program, program, program,
            program, program);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    char error[4096] = {0};
    bool ok = false;
    bool finite_horn_answers_command = false;
    bool answer_stream_command = false;
    bool select_answers_command = false;
    bool oslf_ntt_command = false;
    bool transparent_inline_command = false;
    bool pack_facts_command = false;
    bool answer_facts_command = false;
    bool semantic_gslt_command = false;
    bool petta_direct_command = false;
    bool rhometta_direct_command = false;
    bool horn_shape_command = false;
    bool petta_horn_command = false;
    bool petta_horn_check_command = false;
    size_t answer_len = 0u;
    char answer_digest[65] = {0};
    uint32_t composition_source_len = 0u;
    PPTransparentInlineNativeV1Summary inline_summary = {0};
    size_t fact_len = 0u;
    char fact_pack_digest[65] = {0};
    size_t semantic_operator_count = 0u;
    size_t semantic_rule_count = 0u;
    char semantic_gslt_digest[65] = {0};
    size_t petta_direct_rule_count = 0u;
    char petta_direct_source_digest[65] = {0};
    char petta_direct_artifact_digest[65] = {0};
    size_t rhometta_direct_rule_count = 0u;
    size_t rhometta_direct_relation_count = 0u;
    char rhometta_direct_source_digest[65] = {0};
    char rhometta_direct_artifact_digest[65] = {0};
    FHGSLTStructuralShapeV1 horn_shape = {0};
    char horn_shape_package_digest[65] = {0};
    size_t petta_horn_rule_len = 0u;
    char petta_horn_package_digest[65] = {0};
    char petta_horn_artifact_digest[65] = {0};

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    if (strcmp(argv[1], "equations") == 0) {
        const char *source = option_value(argc, argv, "--source");
        const char *output = option_value(argc, argv, "--out");
        if (source != NULL && output != NULL)
            ok = compile_equations(source, output, error, sizeof(error));
    } else if (strcmp(argv[1], "petta-direct") == 0) {
        petta_direct_command = true;
        ok = compile_petta_direct_command_v1(
            argc, argv, &petta_direct_rule_count,
            petta_direct_source_digest, petta_direct_artifact_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "rhometta-direct") == 0) {
        rhometta_direct_command = true;
        ok = compile_rhometta_direct_command_v1(
            argc, argv, &rhometta_direct_rule_count,
            &rhometta_direct_relation_count,
            rhometta_direct_source_digest,
            rhometta_direct_artifact_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "answers") == 0) {
        finite_horn_answers_command = true;
        ok = compile_answers_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "oslf-ntt") == 0) {
        oslf_ntt_command = true;
        ok = compile_oslf_native_type_command(
            argc, argv, &composition_source_len,
            &answer_len, answer_digest, error, sizeof(error));
    } else if (strcmp(argv[1], "lexical-answers") == 0) {
        finite_horn_answers_command = true;
        ok = compile_lexical_answers_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "transparent-inline") == 0) {
        transparent_inline_command = true;
        ok = compile_transparent_inline_command(
            argc, argv, &inline_summary, error, sizeof(error));
    } else if (strcmp(argv[1], "pack-facts") == 0) {
        pack_facts_command = true;
        ok = compile_pack_facts_command(
            argc, argv, &fact_len, fact_pack_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "answer-facts") == 0) {
        answer_facts_command = true;
        ok = compile_answer_facts_command(
            argc, argv, &fact_len, fact_pack_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "semantic-gslt") == 0) {
        semantic_gslt_command = true;
        ok = compile_semantic_gslt_command_v1(
            argc, argv, &semantic_operator_count, &semantic_rule_count,
            semantic_gslt_digest, error, sizeof(error));
    } else if (strcmp(argv[1], "merge-answers") == 0) {
        answer_stream_command = true;
        ok = merge_answer_streams_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "project-answers") == 0) {
        answer_stream_command = true;
        ok = project_answer_stream_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "select-answers") == 0) {
        select_answers_command = true;
        ok = select_answer_stream_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "state-transition-bindings") == 0) {
        answer_stream_command = true;
        ok = state_transition_bindings_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "state-occurrence-projection-oracle") == 0) {
        answer_stream_command = true;
        ok = state_occurrence_projection_oracle_command_v1(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "oracle-horn-shape") == 0) {
        horn_shape_command = true;
        ok = finite_horn_oracle_shape_command_v1(
            argc, argv, &horn_shape, horn_shape_package_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "oracle-petta-horn") == 0) {
        petta_horn_command = true;
        ok = compile_petta_horn_oracle_command_v1(
            argc, argv, &petta_horn_rule_len,
            petta_horn_package_digest, petta_horn_artifact_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "oracle-petta-horn-check") == 0) {
        petta_horn_check_command = true;
        ok = check_petta_horn_oracle_command_v1(
            argc, argv, &petta_horn_rule_len,
            petta_horn_artifact_digest, error, sizeof(error));
    } else if (strcmp(argv[1], "empty-answers") == 0) {
        answer_stream_command = true;
        ok = empty_answer_stream_command(
            argc, argv, &answer_len, answer_digest,
            error, sizeof(error));
    } else if (strcmp(argv[1], "empty-guard-evidence") == 0) {
        ok = empty_guard_evidence_command(
            argc, argv, error, sizeof(error));
    } else if (strcmp(argv[1], "parser-pack") == 0) {
        const char *manifest = option_value(argc, argv, "--manifest");
        const char *compiler_root = option_value(
            argc, argv, "--compiler-root");
        const char *presentation_root = option_value(
            argc, argv, "--presentation-root");
        const char *pack_output = option_value(argc, argv, "--pack-out");
        const char *lock_output = option_value(argc, argv, "--lock-out");
        if (manifest != NULL && compiler_root != NULL &&
            presentation_root != NULL)
            ok = compile_parser_pack(manifest, compiler_root,
                                     presentation_root, pack_output,
                                     lock_output,
                                     error, sizeof(error));
    } else if (strcmp(argv[1], "validate") == 0) {
        const char *manifest = option_value(argc, argv, "--manifest");
        if (manifest != NULL)
            ok = cetta_langdef_validate_manifest_v1(
                manifest, error, sizeof(error));
    } else if (strcmp(argv[1], "seal") == 0) {
        const char *manifest = option_value(argc, argv, "--manifest");
        const char *pack = option_value(argc, argv, "--pack");
        const char *compiled_cursor = option_value(
            argc, argv, "--compiled-cursor");
        const char *lock_output = option_value(argc, argv, "--lock-out");
        if (manifest != NULL)
            ok = seal_langdef(manifest, pack, compiled_cursor, lock_output,
                              error, sizeof(error));
    } else {
        usage(argv[0]);
        goto done;
    }
    if (!ok)
        fprintf(stderr, "LangDefCompileFailed: %s\n",
                error[0] != '\0' ? error : "invalid arguments");
    else if (oslf_ntt_command)
        printf("(OSLFNativeTypeV1Summary %u %zu %s)\n",
               composition_source_len, answer_len, answer_digest);
    else if (select_answers_command)
        printf("(SelectedAnswerStreamV1Summary %zu %s)\n",
               answer_len, answer_digest);
    else if (finite_horn_answers_command)
        printf("(FiniteHornAnswerStreamV1Summary %zu %s)\n",
               answer_len, answer_digest);
    else if (answer_stream_command)
        printf("(AnswerStreamV1Summary %zu %s)\n",
               answer_len, answer_digest);
    else if (transparent_inline_command)
        printf("(ParserPackTransparentInlineNativeV1Summary "
               "%u %u %u %u %s %s %s)\n",
               inline_summary.source_production_len,
               inline_summary.normalized_production_len,
               inline_summary.removed_transparent_production_len,
               inline_summary.cyclic_transparent_state_len,
               inline_summary.source_pack_digest,
               inline_summary.normalized_pack_digest,
               inline_summary.compiler_digest);
    else if (pack_facts_command)
        printf("(ParserPackFactsV1Summary %zu %s)\n",
               fact_len, fact_pack_digest);
    else if (answer_facts_command)
        printf("(AnswerFactsPresentationV1Summary %zu %s)\n",
               fact_len, fact_pack_digest);
    else if (semantic_gslt_command)
        printf("(SemanticGSLTV1Summary %zu %zu %s)\n",
               semantic_operator_count, semantic_rule_count,
               semantic_gslt_digest);
    else if (petta_direct_command)
        printf("(GSLTDirectPeTTaV1Summary %zu %s %s)\n",
               petta_direct_rule_count,
               petta_direct_source_digest,
               petta_direct_artifact_digest);
    else if (rhometta_direct_command)
        printf("(GSLTDirectRhoMettaV1Summary %zu %zu %s %s)\n",
               rhometta_direct_rule_count,
               rhometta_direct_relation_count,
               rhometta_direct_source_digest,
               rhometta_direct_artifact_digest);
    else if (horn_shape_command)
        printf("(FiniteHornStructuralShapeV1Summary "
               "%zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %s)\n",
               horn_shape.rule_count,
               horn_shape.fact_rule_count,
               horn_shape.implication_rule_count,
               horn_shape.body_goal_count,
               horn_shape.maximum_body_goal_count,
               horn_shape.left_linear_head_rule_count,
               horn_shape.nonlinear_head_rule_count,
               horn_shape.multi_body_rule_count,
               horn_shape.cross_goal_join_rule_count,
               horn_shape.body_only_variable_rule_count,
               horn_shape.head_only_variable_rule_count,
               horn_shape.direct_recursive_rule_count,
               horn_shape_package_digest);
    else if (petta_horn_command)
        printf("(PeTTaHornProgramV1Summary %zu %s %s)\n",
               petta_horn_rule_len,
               petta_horn_package_digest,
               petta_horn_artifact_digest);
    else if (petta_horn_check_command)
        printf("(PeTTaHornAdmissionV1Summary %zu %s)\n",
               petta_horn_rule_len, petta_horn_artifact_digest);

done:
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
