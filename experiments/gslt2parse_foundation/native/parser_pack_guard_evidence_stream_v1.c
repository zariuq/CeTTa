#define _POSIX_C_SOURCE 200809L

#include "parser_pack_guard_evidence_stream_v1.h"

#include "finite_horn_ground_term_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void ppguard_evidence_wire_v1_set_error(char *buf,
                                               size_t size,
                                               const char *format,
                                               ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

void ppguard_evidence_wire_v1_init(PPGuardEvidenceWireV1 *wire) {
    if (!wire)
        return;
    memset(wire, 0, sizeof(*wire));
    arena_init(&wire->arena);
}

void ppguard_evidence_wire_v1_free(PPGuardEvidenceWireV1 *wire) {
    if (!wire)
        return;
    free(wire->derivations);
    arena_free(&wire->arena);
    memset(wire, 0, sizeof(*wire));
}

static bool ppguard_evidence_wire_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char byte = digest[index];
        if (!((byte >= '0' && byte <= '9') ||
              (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppguard_evidence_wire_v1_copy_digest(
    char target[65],
    const char *value,
    const char *name,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguard_evidence_wire_v1_digest_valid(value)) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size, "invalid %s", name);
        return false;
    }
    memcpy(target, value, 65u);
    return true;
}

static size_t ppguard_evidence_wire_v1_split(char *line,
                                             char **fields,
                                             size_t field_cap) {
    size_t count = 1u;
    char *cursor;

    fields[0] = line;
    for (cursor = line; *cursor; cursor++) {
        if (*cursor != '\t')
            continue;
        if (count == field_cap)
            return field_cap + 1u;
        *cursor = '\0';
        fields[count++] = cursor + 1;
    }
    return count;
}

static Atom *ppguard_evidence_wire_v1_parse_term(
    PPGuardEvidenceWireV1 *wire,
    const char *field,
    size_t line_number,
    char *error_buf,
    size_t error_buf_size) {
    Atom *term = NULL;
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    size_t len = strlen(field);
    char detail[512] = {0};

    if (len == 0u ||
        !fh_ground_term_v1_parse(
            &wire->arena, (const uint8_t *)field, len,
            &term, detail, sizeof(detail)) ||
        !fh_ground_term_v1_render(
            term, &canonical, &canonical_len,
            detail, sizeof(detail)) ||
        canonical_len != len || memcmp(canonical, field, len) != 0) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size, "line %zu: %.448s", line_number,
            detail[0] ? detail : "non-canonical finite-Horn term");
        free(canonical);
        return NULL;
    }
    free(canonical);
    return term;
}

static bool ppguard_evidence_wire_v1_push(
    PPGuardEvidenceWireV1 *wire,
    Atom *answer,
    Atom *certificate) {
    PPGuardPlanV1DerivationInput *next;
    size_t next_cap;

    if (wire->derivation_len == wire->derivation_cap) {
        next_cap = wire->derivation_cap
            ? wire->derivation_cap * 2u
            : 16u;
        if (next_cap < wire->derivation_cap ||
            next_cap > SIZE_MAX / sizeof(*wire->derivations)) {
            return false;
        }
        next = realloc(
            wire->derivations,
            next_cap * sizeof(*wire->derivations));
        if (!next)
            return false;
        wire->derivations = next;
        wire->derivation_cap = next_cap;
    }
    wire->derivations[wire->derivation_len++] =
        (PPGuardPlanV1DerivationInput){
            .answer = answer,
            .certificate = certificate,
        };
    return true;
}

bool ppguard_evidence_wire_v1_read(PPGuardEvidenceWireV1 *wire,
                                   const char *path,
                                   char *error_buf,
                                   size_t error_buf_size) {
    static const char *const metadata_names[4] = {
        "source-digest",
        "pre-reflection-digest",
        "environment-digest",
        "answer-set-digest",
    };
    PPGuardEvidenceWireV1 result;
    FILE *input = NULL;
    char *line = NULL;
    size_t line_cap = 0u;
    ssize_t raw_len;
    size_t line_number = 0u;
    size_t metadata_index = 0u;
    char *previous_answer = NULL;
    char *previous_certificate = NULL;
    bool header = false;
    bool ended = false;
    bool ok = false;

    ppguard_evidence_wire_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!wire || !path) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size,
            "bad positive guard evidence stream arguments");
        goto done;
    }
    input = fopen(path, "rb");
    if (!input) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size,
            "cannot open positive guard evidence stream");
        goto done;
    }
    while ((raw_len = getline(&line, &line_cap, input)) >= 0) {
        char *fields[3];
        size_t field_count;
        size_t len = (size_t)raw_len;

        line_number++;
        if (ended) {
            ppguard_evidence_wire_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: data after end record", line_number);
            goto done;
        }
        if (len == 0u || line[len - 1u] != '\n') {
            ppguard_evidence_wire_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: record lacks LF framing", line_number);
            goto done;
        }
        line[--len] = '\0';
        if (len == 0u || line[len - 1u] == '\r' ||
            memchr(line, '\0', len) != NULL) {
            ppguard_evidence_wire_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: invalid record bytes", line_number);
            goto done;
        }
        field_count = ppguard_evidence_wire_v1_split(
            line, fields, 3u);
        if (!header) {
            if (field_count != 1u ||
                strcmp(fields[0],
                       "parser-pack-positive-guard-evidence-v1") != 0) {
                ppguard_evidence_wire_v1_set_error(
                    error_buf, error_buf_size,
                    "line %zu: invalid evidence stream header",
                    line_number);
                goto done;
            }
            header = true;
            continue;
        }
        if (metadata_index < 4u) {
            char *target;
            if (field_count != 2u ||
                strcmp(fields[0], metadata_names[metadata_index]) != 0) {
                ppguard_evidence_wire_v1_set_error(
                    error_buf, error_buf_size,
                    "line %zu: expected %s metadata", line_number,
                    metadata_names[metadata_index]);
                goto done;
            }
            if (metadata_index == 0u)
                target = result.source_digest;
            else if (metadata_index == 1u)
                target = result.pre_reflection_digest;
            else if (metadata_index == 2u)
                target = result.environment_digest;
            else
                target = result.answer_set_digest;
            if (!ppguard_evidence_wire_v1_copy_digest(
                    target, fields[1], metadata_names[metadata_index],
                    error_buf, error_buf_size)) {
                goto done;
            }
            metadata_index++;
            continue;
        }
        if (strcmp(fields[0], "derivation") == 0) {
            Atom *answer;
            Atom *certificate;
            int answer_order;
            int certificate_order = 0;

            if (field_count != 3u)
                goto malformed;
            answer_order = previous_answer
                ? strcmp(previous_answer, fields[1])
                : -1;
            if (previous_answer && answer_order == 0) {
                certificate_order = strcmp(
                    previous_certificate, fields[2]);
            }
            if (previous_answer &&
                (answer_order > 0 ||
                 (answer_order == 0 && certificate_order >= 0))) {
                ppguard_evidence_wire_v1_set_error(
                    error_buf, error_buf_size,
                    "line %zu: derivations are not strictly ordered",
                    line_number);
                goto done;
            }
            answer = ppguard_evidence_wire_v1_parse_term(
                &result, fields[1], line_number,
                error_buf, error_buf_size);
            certificate = ppguard_evidence_wire_v1_parse_term(
                &result, fields[2], line_number,
                error_buf, error_buf_size);
            if (!answer || !certificate ||
                !ppguard_evidence_wire_v1_push(
                    &result, answer, certificate)) {
                if (error_buf && error_buf_size > 0u &&
                    error_buf[0] == '\0') {
                    ppguard_evidence_wire_v1_set_error(
                        error_buf, error_buf_size,
                        "failed to allocate positive guard derivation");
                }
                goto done;
            }
            free(previous_answer);
            free(previous_certificate);
            previous_answer = strdup(fields[1]);
            previous_certificate = strdup(fields[2]);
            if (!previous_answer || !previous_certificate)
                goto done;
            continue;
        }
        if (strcmp(fields[0], "end") == 0 && field_count == 1u) {
            ended = true;
            continue;
        }

malformed:
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size,
            "line %zu: malformed evidence record", line_number);
        goto done;
    }
    if (ferror(input)) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size,
            "failed while reading positive guard evidence stream");
        goto done;
    }
    if (!header || metadata_index != 4u || !ended) {
        ppguard_evidence_wire_v1_set_error(
            error_buf, error_buf_size,
            "incomplete positive guard evidence stream");
        goto done;
    }
    ppguard_evidence_wire_v1_free(wire);
    *wire = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(previous_answer);
    free(previous_certificate);
    free(line);
    ppguard_evidence_wire_v1_free(&result);
    return ok;
}
