#define _POSIX_C_SOURCE 200809L

#include "parser_pack_abi_stream_v1.h"

#include "finite_horn_ground_term_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void ppabi_v1_wire_set_error(char *buf,
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

void ppabi_v1_wire_init(PPABIV1Wire *wire) {
    if (!wire)
        return;
    memset(wire, 0, sizeof(*wire));
    arena_init(&wire->arena);
}

void ppabi_v1_wire_free(PPABIV1Wire *wire) {
    if (!wire)
        return;
    free(wire->productions);
    free(wire->classes);
    free(wire->derivations);
    arena_free(&wire->arena);
    memset(wire, 0, sizeof(*wire));
}

static bool ppabi_v1_wire_digest_valid(const char *digest) {
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

static bool ppabi_v1_wire_copy_digest(char target[65],
                                      const char *value,
                                      const char *name,
                                      char *error_buf,
                                      size_t error_buf_size) {
    if (!ppabi_v1_wire_digest_valid(value)) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "invalid %s", name);
        return false;
    }
    memcpy(target, value, 65u);
    return true;
}

static bool ppabi_v1_wire_atom_push(Atom ***data,
                                    size_t *len,
                                    size_t *cap,
                                    Atom *term) {
    Atom **next;
    size_t next_cap;

    if (*len == *cap) {
        next_cap = *cap ? *cap * 2u : 128u;
        if (next_cap < *cap || next_cap > SIZE_MAX / sizeof(**data))
            return false;
        next = realloc(*data, next_cap * sizeof(**data));
        if (!next)
            return false;
        *data = next;
        *cap = next_cap;
    }
    (*data)[(*len)++] = term;
    return true;
}

static bool ppabi_v1_wire_derivation_push(
    PPABIV1Wire *wire,
    PPABIV1EvidenceKind kind,
    Atom *artifact,
    Atom *answer,
    Atom *certificate) {
    PPABIV1DerivationInput *next;
    size_t next_cap;

    if (wire->derivation_len == wire->derivation_cap) {
        next_cap = wire->derivation_cap
            ? wire->derivation_cap * 2u
            : 128u;
        if (next_cap < wire->derivation_cap ||
            next_cap > SIZE_MAX / sizeof(*wire->derivations)) {
            return false;
        }
        next = realloc(wire->derivations,
                       next_cap * sizeof(*wire->derivations));
        if (!next)
            return false;
        wire->derivations = next;
        wire->derivation_cap = next_cap;
    }
    wire->derivations[wire->derivation_len++] =
        (PPABIV1DerivationInput){
            .kind = kind,
            .artifact = artifact,
            .answer = answer,
            .certificate = certificate,
        };
    return true;
}

static Atom *ppabi_v1_wire_parse_term(PPABIV1Wire *wire,
                                      const char *field,
                                      size_t line_number,
                                      char *error_buf,
                                      size_t error_buf_size) {
    Atom *term = NULL;
    char detail[512] = {0};

    if (!fh_ground_term_v1_parse(
            &wire->arena, (const uint8_t *)field, strlen(field),
            &term, detail, sizeof(detail))) {
        ppabi_v1_wire_set_error(
            error_buf, error_buf_size, "line %zu: %s", line_number,
            detail[0] ? detail : "invalid canonical finite-Horn term");
        return NULL;
    }
    return term;
}

static size_t ppabi_v1_wire_split_fields(char *line,
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

static bool ppabi_v1_wire_read_metadata(
    PPABIV1Wire *wire,
    size_t index,
    char **fields,
    size_t field_count,
    size_t line_number,
    char *error_buf,
    size_t error_buf_size) {
    static const char *const names[6] = {
        "source-digest",
        "compiler-digest",
        "environment-digest",
        "pack-digest",
        "start",
        "closure",
    };

    if (field_count != 2u || strcmp(fields[0], names[index]) != 0) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "line %zu: expected %s metadata",
                                line_number, names[index]);
        return false;
    }
    switch (index) {
    case 0u:
        return ppabi_v1_wire_copy_digest(
            wire->source_digest, fields[1], names[index],
            error_buf, error_buf_size);
    case 1u:
        return ppabi_v1_wire_copy_digest(
            wire->compiler_digest, fields[1], names[index],
            error_buf, error_buf_size);
    case 2u:
        return ppabi_v1_wire_copy_digest(
            wire->environment_digest, fields[1], names[index],
            error_buf, error_buf_size);
    case 3u:
        return ppabi_v1_wire_copy_digest(
            wire->expected_pack_digest, fields[1], names[index],
            error_buf, error_buf_size);
    case 4u:
        wire->start = ppabi_v1_wire_parse_term(
            wire, fields[1], line_number, error_buf, error_buf_size);
        return wire->start != NULL;
    case 5u:
        if (strcmp(fields[1], "closed") == 0) {
            wire->expected_closed = true;
            return true;
        }
        if (strcmp(fields[1], "partial") == 0) {
            wire->expected_closed = false;
            return true;
        }
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "line %zu: invalid closure expectation",
                                line_number);
        return false;
    }
    return false;
}

static bool ppabi_v1_wire_read_record(
    PPABIV1Wire *wire,
    char **fields,
    size_t field_count,
    unsigned *phase,
    bool *ended,
    size_t line_number,
    char *error_buf,
    size_t error_buf_size) {
    Atom *artifact;
    Atom *answer;
    Atom *certificate;

    if (strcmp(fields[0], "production") == 0) {
        Atom *term;
        if (field_count != 2u || *phase > 1u)
            goto malformed;
        *phase = 1u;
        term = ppabi_v1_wire_parse_term(
            wire, fields[1], line_number, error_buf, error_buf_size);
        return term && ppabi_v1_wire_atom_push(
            &wire->productions, &wire->production_len,
            &wire->production_cap, term);
    }
    if (strcmp(fields[0], "class-clause") == 0) {
        Atom *term;
        if (field_count != 2u || *phase > 2u)
            goto malformed;
        *phase = 2u;
        term = ppabi_v1_wire_parse_term(
            wire, fields[1], line_number, error_buf, error_buf_size);
        return term && ppabi_v1_wire_atom_push(
            &wire->classes, &wire->class_len,
            &wire->class_cap, term);
    }
    if (strcmp(fields[0], "production-evidence") == 0) {
        if (field_count != 4u || *phase > 3u)
            goto malformed;
        *phase = 3u;
        artifact = ppabi_v1_wire_parse_term(
            wire, fields[1], line_number, error_buf, error_buf_size);
        answer = ppabi_v1_wire_parse_term(
            wire, fields[2], line_number, error_buf, error_buf_size);
        certificate = ppabi_v1_wire_parse_term(
            wire, fields[3], line_number, error_buf, error_buf_size);
        return artifact && answer && certificate &&
            ppabi_v1_wire_derivation_push(
                wire, PPABI_V1_EVIDENCE_PRODUCTION,
                artifact, answer, certificate);
    }
    if (strcmp(fields[0], "class-evidence") == 0) {
        if (field_count != 4u || *phase > 4u)
            goto malformed;
        *phase = 4u;
        artifact = ppabi_v1_wire_parse_term(
            wire, fields[1], line_number, error_buf, error_buf_size);
        answer = ppabi_v1_wire_parse_term(
            wire, fields[2], line_number, error_buf, error_buf_size);
        certificate = ppabi_v1_wire_parse_term(
            wire, fields[3], line_number, error_buf, error_buf_size);
        return artifact && answer && certificate &&
            ppabi_v1_wire_derivation_push(
                wire, PPABI_V1_EVIDENCE_CLASS,
                artifact, answer, certificate);
    }
    if (strcmp(fields[0], "end") == 0) {
        if (field_count != 1u)
            goto malformed;
        *ended = true;
        return true;
    }

malformed:
    ppabi_v1_wire_set_error(
        error_buf, error_buf_size,
        "line %zu: malformed or out-of-order ABI record", line_number);
    return false;
}

bool ppabi_v1_wire_read(PPABIV1Wire *wire,
                        const char *path,
                        char *error_buf,
                        size_t error_buf_size) {
    FILE *input;
    char *line = NULL;
    size_t line_cap = 0u;
    ssize_t read;
    size_t line_number = 0u;
    size_t metadata = 0u;
    unsigned phase = 0u;
    bool header = false;
    bool ended = false;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!wire || !path) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "bad ABI stream arguments");
        return false;
    }
    input = fopen(path, "rb");
    if (!input) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "cannot open ABI stream");
        return false;
    }
    while ((read = getline(&line, &line_cap, input)) >= 0) {
        char *fields[4];
        size_t field_count;

        line_number++;
        if (ended) {
            ppabi_v1_wire_set_error(
                error_buf, error_buf_size,
                "line %zu: data after end record", line_number);
            goto done;
        }
        if (read == 0 || line[(size_t)read - 1u] != '\n') {
            ppabi_v1_wire_set_error(
                error_buf, error_buf_size,
                "line %zu: record lacks LF framing", line_number);
            goto done;
        }
        line[--read] = '\0';
        if (memchr(line, '\0', (size_t)read) != NULL ||
            (read > 0 && line[(size_t)read - 1u] == '\r')) {
            ppabi_v1_wire_set_error(
                error_buf, error_buf_size,
                "line %zu: invalid record bytes", line_number);
            goto done;
        }
        field_count = ppabi_v1_wire_split_fields(line, fields, 4u);
        if (!header) {
            if (field_count != 1u ||
                strcmp(fields[0], "parser-pack-abi-v1") != 0) {
                ppabi_v1_wire_set_error(
                    error_buf, error_buf_size,
                    "line %zu: invalid ABI stream header", line_number);
                goto done;
            }
            header = true;
            continue;
        }
        if (metadata < 6u) {
            if (!ppabi_v1_wire_read_metadata(
                    wire, metadata, fields, field_count, line_number,
                    error_buf, error_buf_size)) {
                goto done;
            }
            metadata++;
            continue;
        }
        if (!ppabi_v1_wire_read_record(
                wire, fields, field_count, &phase, &ended, line_number,
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (ferror(input)) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "failed while reading ABI stream");
        goto done;
    }
    if (!header || metadata != 6u || !ended || !wire->start ||
        wire->production_len == 0u || wire->derivation_len == 0u) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "incomplete ABI stream");
        goto done;
    }
    ok = true;

done:
    free(line);
    (void)fclose(input);
    return ok;
}

bool ppabi_v1_wire_load_pack(const PPABIV1Wire *wire,
                             PPABIV1Pack *out,
                             char *error_buf,
                             size_t error_buf_size) {
    PPABIV1ProvenanceInput provenance;
    bool closed;
    char closure_error[512] = {0};

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!wire || !out || !wire->start) {
        ppabi_v1_wire_set_error(error_buf, error_buf_size,
                                "bad ABI load arguments");
        return false;
    }
    provenance = (PPABIV1ProvenanceInput){
        .source_digest = wire->source_digest,
        .compiler_digest = wire->compiler_digest,
        .environment_digest = wire->environment_digest,
        .derivations = wire->derivations,
        .derivation_len = wire->derivation_len,
    };
    if (!ppabi_v1_pack_load(
            out, wire->productions, wire->production_len,
            wire->classes, wire->class_len, &provenance,
            error_buf, error_buf_size)) {
        return false;
    }
    if (strcmp(out->pack_digest, wire->expected_pack_digest) != 0) {
        ppabi_v1_wire_set_error(
            error_buf, error_buf_size,
            "ParserPack digest changed across ABI boundary");
        return false;
    }
    closed = ppabi_v1_pack_start_is_closed(
        out, wire->start, closure_error, sizeof(closure_error));
    if (closed != wire->expected_closed ||
        (!closed && closure_error[0] == '\0')) {
        ppabi_v1_wire_set_error(
            error_buf, error_buf_size,
            "ParserPack closure result changed across ABI boundary");
        return false;
    }
    return true;
}
