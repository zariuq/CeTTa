#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void fh_answer_stream_v1_set_error(char *buf,
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

void fh_answer_stream_v1_init(FHAnswerStreamV1 *stream) {
    if (!stream)
        return;
    memset(stream, 0, sizeof(*stream));
    arena_init(&stream->arena);
}

void fh_answer_stream_v1_free(FHAnswerStreamV1 *stream) {
    if (!stream)
        return;
    free(stream->terms);
    arena_free(&stream->arena);
    memset(stream, 0, sizeof(*stream));
}

static bool fh_answer_stream_v1_grow(FHAnswerStreamV1 *stream,
                                     size_t needed) {
    Atom **next;
    size_t next_cap;

    if (needed <= stream->cap)
        return true;
    next_cap = stream->cap ? stream->cap : 128u;
    while (next_cap < needed) {
        if (next_cap > SIZE_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if (next_cap > SIZE_MAX / sizeof(*stream->terms))
        return false;
    next = realloc(stream->terms, next_cap * sizeof(*stream->terms));
    if (!next)
        return false;
    stream->terms = next;
    stream->cap = next_cap;
    return true;
}

bool fh_answer_stream_v1_read(FHAnswerStreamV1 *stream,
                              const char *path,
                              char *error_buf,
                              size_t error_buf_size) {
    static const uint8_t domain[] = "FiniteHornAnswerSetV1\n";
    FHAnswerStreamV1 result;
    CettaNativeSha256 sha;
    FILE *input = NULL;
    char *line = NULL;
    size_t line_cap = 0u;
    ssize_t raw_len;
    size_t line_number = 0u;
    char *previous = NULL;
    bool ok = false;

    fh_answer_stream_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!stream || !path) {
        fh_answer_stream_v1_set_error(
            error_buf, error_buf_size, "bad finite-Horn answer stream arguments");
        goto done;
    }
    input = fopen(path, "rb");
    if (!input) {
        fh_answer_stream_v1_set_error(
            error_buf, error_buf_size, "cannot open finite-Horn answer stream");
        goto done;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    while ((raw_len = getline(&line, &line_cap, input)) >= 0) {
        Atom *term = NULL;
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        size_t len = (size_t)raw_len;
        char detail[512] = {0};

        line_number++;
        if (len == 0u || line[len - 1u] != '\n') {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: answer record lacks LF framing", line_number);
            goto done;
        }
        line[--len] = '\0';
        if (len == 0u || line[len - 1u] == '\r' ||
            memchr(line, '\0', len) != NULL) {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: invalid answer record bytes", line_number);
            goto done;
        }
        if (!fh_ground_term_v1_parse(
                &result.arena, (const uint8_t *)line, len,
                &term, detail, sizeof(detail)) ||
            !fh_ground_term_v1_render(
                term, &canonical, &canonical_len,
                detail, sizeof(detail))) {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size, "line %zu: %.448s", line_number,
                detail[0] ? detail : "malformed finite-Horn answer");
            free(canonical);
            goto done;
        }
        if (canonical_len != len || memcmp(canonical, line, len) != 0) {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: finite-Horn answer is not canonical", line_number);
            free(canonical);
            goto done;
        }
        if (previous && strcmp(previous, (char *)canonical) >= 0) {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size,
                "line %zu: finite-Horn answers are not strictly ordered",
                line_number);
            free(canonical);
            goto done;
        }
        if (!fh_answer_stream_v1_grow(&result, result.len + 1u)) {
            fh_answer_stream_v1_set_error(
                error_buf, error_buf_size,
                "failed to allocate finite-Horn answer stream");
            free(canonical);
            goto done;
        }
        result.terms[result.len++] = term;
        cetta_native_sha256_update(&sha, canonical, canonical_len);
        cetta_native_sha256_update(
            &sha, (const uint8_t *)"\n", 1u);
        free(previous);
        previous = (char *)canonical;
    }
    if (ferror(input)) {
        fh_answer_stream_v1_set_error(
            error_buf, error_buf_size,
            "failed while reading finite-Horn answer stream");
        goto done;
    }
    cetta_native_sha256_finish_hex(&sha, result.digest);
    fh_answer_stream_v1_free(stream);
    *stream = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(previous);
    free(line);
    fh_answer_stream_v1_free(&result);
    return ok;
}
