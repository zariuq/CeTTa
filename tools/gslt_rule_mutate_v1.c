#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t start;
    size_t end;
    size_t name_start;
    size_t name_len;
} RuleSpan;

typedef struct {
    RuleSpan *items;
    size_t count;
    size_t capacity;
} RuleSpans;

typedef struct {
    size_t start;
    size_t end;
    size_t head_start;
    size_t head_len;
    size_t last_argument_start;
    size_t last_argument_end;
    bool has_argument;
} FormSpan;

typedef struct {
    FormSpan *items;
    size_t count;
    size_t capacity;
} FormSpans;

static void usage(const char *program) {
    fprintf(stderr,
            "usage:\n"
            "  %s list --source PATH\n"
            "  %s mutate --source PATH --out PATH --rule NAME "
            "--mode delete|falsify [--replacement TERM]\n"
            "  %s list-forms --source PATH\n"
            "  %s mutate-form --source PATH --out PATH --index N "
            "--mode delete|falsify [--replacement TERM "
            "--alternate TERM]\n",
            program, program, program, program);
}

static bool is_space(unsigned char byte) {
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' ||
           byte == '\f';
}

static bool is_token_boundary(unsigned char byte) {
    return is_space(byte) || byte == '(' || byte == ')' || byte == ';' ||
           byte == '"';
}

static bool skip_layout(const unsigned char *bytes,
                        size_t length,
                        size_t *cursor) {
    while (*cursor < length) {
        if (is_space(bytes[*cursor])) {
            *cursor += 1U;
            continue;
        }
        if (bytes[*cursor] == ';') {
            while (*cursor < length && bytes[*cursor] != '\n') {
                *cursor += 1U;
            }
            continue;
        }
        break;
    }
    return *cursor < length;
}

static bool token_equals(const unsigned char *bytes,
                         size_t length,
                         size_t start,
                         const char *expected) {
    size_t expected_len = strlen(expected);
    return length == expected_len &&
           memcmp(bytes + start, expected, expected_len) == 0;
}

static bool read_token(const unsigned char *bytes,
                       size_t length,
                       size_t *cursor,
                       size_t *start,
                       size_t *token_length) {
    if (!skip_layout(bytes, length, cursor) ||
        is_token_boundary(bytes[*cursor])) {
        return false;
    }
    *start = *cursor;
    while (*cursor < length && !is_token_boundary(bytes[*cursor])) {
        *cursor += 1U;
    }
    *token_length = *cursor - *start;
    return *token_length > 0U;
}

static bool find_form_end(const unsigned char *bytes,
                          size_t length,
                          size_t start,
                          size_t *end) {
    size_t cursor = start;
    size_t depth = 0U;
    bool in_string = false;
    bool escaped = false;
    bool in_comment = false;

    while (cursor < length) {
        unsigned char byte = bytes[cursor++];
        if (in_comment) {
            if (byte == '\n') {
                in_comment = false;
            }
            continue;
        }
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                in_string = false;
            }
            continue;
        }
        if (byte == ';') {
            in_comment = true;
        } else if (byte == '"') {
            in_string = true;
        } else if (byte == '(') {
            depth += 1U;
        } else if (byte == ')') {
            if (depth == 0U) {
                return false;
            }
            depth -= 1U;
            if (depth == 0U) {
                *end = cursor;
                return true;
            }
        }
    }
    return false;
}

static bool append_span(RuleSpans *spans, RuleSpan span) {
    if (spans->count == spans->capacity) {
        size_t next_capacity = spans->capacity == 0U ? 32U : spans->capacity * 2U;
        if (next_capacity < spans->capacity ||
            next_capacity > SIZE_MAX / sizeof(*spans->items)) {
            return false;
        }
        RuleSpan *next = realloc(spans->items,
                                 next_capacity * sizeof(*spans->items));
        if (next == NULL) {
            return false;
        }
        spans->items = next;
        spans->capacity = next_capacity;
    }
    spans->items[spans->count++] = span;
    return true;
}

static bool append_form_span(FormSpans *spans, FormSpan span) {
    if (spans->count == spans->capacity) {
        size_t next_capacity = spans->capacity == 0U
            ? 32U
            : spans->capacity * 2U;
        if (next_capacity < spans->capacity ||
            next_capacity > SIZE_MAX / sizeof(*spans->items)) {
            return false;
        }
        FormSpan *next = realloc(
            spans->items, next_capacity * sizeof(*spans->items));
        if (next == NULL) {
            return false;
        }
        spans->items = next;
        spans->capacity = next_capacity;
    }
    spans->items[spans->count++] = span;
    return true;
}

static bool collect_rules(const unsigned char *bytes,
                          size_t length,
                          RuleSpans *spans) {
    size_t cursor = 0U;
    bool in_string = false;
    bool escaped = false;
    bool in_comment = false;

    while (cursor < length) {
        unsigned char byte = bytes[cursor];
        if (in_comment) {
            in_comment = byte != '\n';
            cursor += 1U;
            continue;
        }
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                in_string = false;
            }
            cursor += 1U;
            continue;
        }
        if (byte == ';') {
            in_comment = true;
            cursor += 1U;
            continue;
        }
        if (byte == '"') {
            in_string = true;
            cursor += 1U;
            continue;
        }
        if (byte != '(') {
            cursor += 1U;
            continue;
        }

        size_t form_start = cursor;
        size_t probe = cursor + 1U;
        size_t head_start = 0U;
        size_t head_len = 0U;
        if (!read_token(bytes, length, &probe, &head_start, &head_len) ||
            !token_equals(bytes, head_len, head_start, "rule")) {
            cursor += 1U;
            continue;
        }
        size_t name_start = 0U;
        size_t name_len = 0U;
        size_t form_end = 0U;
        if (!read_token(bytes, length, &probe, &name_start, &name_len) ||
            !find_form_end(bytes, length, form_start, &form_end)) {
            fprintf(stderr, "malformed rule form at byte %zu\n", form_start);
            return false;
        }
        RuleSpan span = {
            .start = form_start,
            .end = form_end,
            .name_start = name_start,
            .name_len = name_len,
        };
        if (!append_span(spans, span)) {
            fprintf(stderr, "rule inventory is too large\n");
            return false;
        }
        cursor = form_end;
    }
    if (in_string) {
        fprintf(stderr, "unterminated string in source\n");
        return false;
    }
    return true;
}

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        fprintf(stderr, "cannot seek %s: %s\n", path, strerror(errno));
        fclose(stream);
        return NULL;
    }
    long end = ftell(stream);
    if (end < 0L || (unsigned long)end > SIZE_MAX) {
        fprintf(stderr, "cannot size %s\n", path);
        fclose(stream);
        return NULL;
    }
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "cannot rewind %s: %s\n", path, strerror(errno));
        fclose(stream);
        return NULL;
    }
    *length = (size_t)end;
    unsigned char *bytes = malloc(*length == 0U ? 1U : *length);
    if (bytes == NULL) {
        fprintf(stderr, "cannot allocate source buffer\n");
        fclose(stream);
        return NULL;
    }
    if (*length > 0U && fread(bytes, 1U, *length, stream) != *length) {
        fprintf(stderr, "cannot read %s: %s\n", path, strerror(errno));
        free(bytes);
        fclose(stream);
        return NULL;
    }
    if (fclose(stream) != 0) {
        fprintf(stderr, "cannot close %s: %s\n", path, strerror(errno));
        free(bytes);
        return NULL;
    }
    return bytes;
}

static bool write_bytes(FILE *stream,
                        const unsigned char *bytes,
                        size_t length,
                        const char *path) {
    if (length > 0U && fwrite(bytes, 1U, length, stream) != length) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

static bool read_term_span(const unsigned char *bytes,
                           size_t length,
                           size_t *cursor,
                           size_t *start,
                           size_t *end) {
    if (!skip_layout(bytes, length, cursor)) {
        return false;
    }
    *start = *cursor;
    if (bytes[*cursor] == '(') {
        if (!find_form_end(bytes, length, *cursor, end)) {
            return false;
        }
        *cursor = *end;
        return true;
    }
    if (bytes[*cursor] == '"') {
        bool escaped = false;
        *cursor += 1U;
        while (*cursor < length) {
            unsigned char byte = bytes[(*cursor)++];
            if (escaped) {
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                *end = *cursor;
                return true;
            }
        }
        return false;
    }
    while (*cursor < length && !is_token_boundary(bytes[*cursor])) {
        *cursor += 1U;
    }
    *end = *cursor;
    return *end > *start;
}

static bool collect_forms(const unsigned char *bytes,
                          size_t length,
                          FormSpans *spans) {
    size_t cursor = 0U;

    while (skip_layout(bytes, length, &cursor)) {
        size_t form_start = cursor;
        size_t form_end = 0U;
        size_t probe;
        size_t head_start = 0U;
        size_t head_len = 0U;
        FormSpan span = {0};

        if (bytes[cursor] != '(' ||
            !find_form_end(bytes, length, cursor, &form_end)) {
            fprintf(stderr, "expected a balanced top-level form at byte %zu\n",
                    cursor);
            return false;
        }
        probe = cursor + 1U;
        if (!read_token(bytes, form_end, &probe,
                        &head_start, &head_len)) {
            fprintf(stderr, "top-level form lacks a symbolic head at byte %zu\n",
                    cursor);
            return false;
        }
        span.start = form_start;
        span.end = form_end;
        span.head_start = head_start;
        span.head_len = head_len;

        for (;;) {
            size_t argument_start = 0U;
            size_t argument_end = 0U;
            if (!skip_layout(bytes, form_end, &probe) ||
                bytes[probe] == ')') {
                break;
            }
            if (!read_term_span(bytes, form_end, &probe,
                                &argument_start, &argument_end)) {
                fprintf(stderr,
                        "malformed argument in top-level form at byte %zu\n",
                        form_start);
                return false;
            }
            span.last_argument_start = argument_start;
            span.last_argument_end = argument_end;
            span.has_argument = true;
        }
        if (!append_form_span(spans, span)) {
            fprintf(stderr, "form inventory is too large\n");
            return false;
        }
        cursor = form_end;
    }
    return true;
}

static bool find_headed_form(const unsigned char *bytes,
                             size_t start,
                             size_t end,
                             const char *head,
                             size_t *form_start,
                             size_t *form_end,
                             size_t *after_head) {
    size_t cursor = start;
    bool in_string = false;
    bool escaped = false;
    bool in_comment = false;

    while (cursor < end) {
        unsigned char byte = bytes[cursor];
        if (in_comment) {
            in_comment = byte != '\n';
            cursor += 1U;
            continue;
        }
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                in_string = false;
            }
            cursor += 1U;
            continue;
        }
        if (byte == ';') {
            in_comment = true;
            cursor += 1U;
            continue;
        }
        if (byte == '"') {
            in_string = true;
            cursor += 1U;
            continue;
        }
        if (byte != '(') {
            cursor += 1U;
            continue;
        }
        size_t probe = cursor + 1U;
        size_t token_start = 0U;
        size_t token_len = 0U;
        if (read_token(bytes, end, &probe, &token_start, &token_len) &&
            token_equals(bytes, token_len, token_start, head)) {
            size_t close = 0U;
            if (!find_form_end(bytes, end, cursor, &close)) {
                return false;
            }
            *form_start = cursor;
            *form_end = close;
            *after_head = probe;
            return true;
        }
        cursor += 1U;
    }
    return false;
}

static bool find_state_action_operand(const unsigned char *bytes,
                                      const RuleSpan *rule,
                                      size_t *action_start,
                                      size_t *action_end) {
    static const struct {
        const char *head;
        size_t preceding_arguments;
    } forms[] = {
        {"source-state-action", 2U},
        {"source-state-final-action", 1U},
    };

    for (size_t form_index = 0U;
         form_index < sizeof(forms) / sizeof(forms[0]); ++form_index) {
        size_t form_start = 0U;
        size_t form_end = 0U;
        size_t cursor = 0U;
        if (!find_headed_form(bytes, rule->start, rule->end,
                              forms[form_index].head, &form_start,
                              &form_end, &cursor)) {
            continue;
        }
        (void)form_start;
        for (size_t argument = 0U;
             argument < forms[form_index].preceding_arguments; ++argument) {
            size_t ignored_start = 0U;
            size_t ignored_end = 0U;
            if (!read_term_span(bytes, form_end, &cursor,
                                &ignored_start, &ignored_end)) {
                return false;
            }
        }
        return read_term_span(bytes, form_end, &cursor,
                              action_start, action_end);
    }
    return false;
}

static bool emit_span_replacement(const char *out_path,
                                  const unsigned char *bytes,
                                  size_t length,
                                  size_t replacement_start,
                                  size_t replacement_end,
                                  const char *replacement) {
    FILE *stream = fopen(out_path, "wb");
    if (stream == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", out_path, strerror(errno));
        return false;
    }
    bool ok = write_bytes(stream, bytes, replacement_start, out_path) &&
              write_bytes(stream, (const unsigned char *)replacement,
                          strlen(replacement), out_path) &&
              write_bytes(stream, bytes + replacement_end,
                          length - replacement_end, out_path);
    if (fclose(stream) != 0) {
        fprintf(stderr, "cannot close %s: %s\n", out_path, strerror(errno));
        ok = false;
    }
    return ok;
}

static bool emit_mutation(const char *out_path,
                          const unsigned char *bytes,
                          size_t length,
                          const RuleSpan *span,
                          bool falsify,
                          const char *replacement) {
    if (falsify) {
        if (replacement) {
            size_t form_start = 0U;
            size_t form_end = 0U;
            size_t cursor = 0U;
            size_t head_start = 0U;
            size_t head_end = 0U;
            if (!find_headed_form(bytes, span->start, span->end,
                                  "head", &form_start, &form_end,
                                  &cursor) ||
                !read_term_span(bytes, form_end, &cursor,
                                &head_start, &head_end)) {
                fprintf(stderr, "rule has no replaceable head\n");
                return false;
            }
            (void)form_start;
            return emit_span_replacement(out_path, bytes, length,
                                         head_start, head_end,
                                         replacement);
        }
        size_t action_start = 0U;
        size_t action_end = 0U;
        if (find_state_action_operand(bytes, span,
                                      &action_start, &action_end)) {
            static const char noop[] = "state-noop-v1";
            bool already_noop =
                action_end - action_start == sizeof(noop) - 1U &&
                memcmp(bytes + action_start, noop, sizeof(noop) - 1U) == 0;
            const char *replacement = already_noop
                ? "(state-require-depth-v1 4294967295)"
                : noop;
            return emit_span_replacement(out_path, bytes, length,
                                         action_start, action_end,
                                         replacement);
        }
    }
    FILE *stream = fopen(out_path, "wb");
    if (stream == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", out_path, strerror(errno));
        return false;
    }
    bool ok = write_bytes(stream, bytes, span->start, out_path);
    if (ok && falsify) {
        static const char prefix[] = "(rule ";
        static const char suffix[] =
            " (head (gslt-mutation-sentinel-v1)) (body))";
        ok = write_bytes(stream, (const unsigned char *)prefix,
                         sizeof(prefix) - 1U, out_path) &&
             write_bytes(stream, bytes + span->name_start,
                         span->name_len, out_path) &&
             write_bytes(stream, (const unsigned char *)suffix,
                         sizeof(suffix) - 1U, out_path);
    }
    if (ok) {
        ok = write_bytes(stream, bytes + span->end,
                         length - span->end, out_path);
    }
    if (fclose(stream) != 0) {
        fprintf(stderr, "cannot close %s: %s\n", out_path, strerror(errno));
        ok = false;
    }
    return ok;
}

static bool emit_form_mutation(const char *out_path,
                               const unsigned char *bytes,
                               size_t length,
                               const FormSpan *span,
                               bool falsify,
                               const char *replacement,
                               const char *alternate) {
    if (!falsify) {
        return emit_span_replacement(out_path, bytes, length,
                                     span->start, span->end, "");
    }
    if (span->has_argument) {
        const char *selected = replacement
            ? replacement
            : "(gslt-mutation-sentinel-v1)";
        if (alternate && replacement &&
            span->last_argument_end - span->last_argument_start ==
                strlen(replacement) &&
            memcmp(bytes + span->last_argument_start,
                   replacement, strlen(replacement)) == 0) {
            selected = alternate;
        }
        return emit_span_replacement(
            out_path, bytes, length,
            span->last_argument_start, span->last_argument_end,
            selected);
    }
    return emit_span_replacement(
        out_path, bytes, length, span->end - 1U, span->end - 1U,
        " (gslt-mutation-sentinel-v1)");
}

static bool parse_size(const char *text, size_t *value) {
    size_t result = 0U;

    if (!text || !*text || !value) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor; ++cursor) {
        size_t digit;
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        digit = (size_t)(*cursor - '0');
        if (result > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static const char *option_value(int argc,
                                char **argv,
                                const char *option) {
    const char *value = NULL;
    for (int index = 2; index < argc; ++index) {
        if (strcmp(argv[index], option) == 0) {
            if (index + 1 >= argc || value != NULL) {
                return NULL;
            }
            value = argv[index + 1];
            index += 1;
        }
    }
    return value;
}

static bool options_well_formed(int argc, char **argv) {
    for (int index = 2; index < argc; index += 2) {
        const char *option = argv[index];
        bool known = strcmp(option, "--source") == 0 ||
            strcmp(option, "--out") == 0 ||
            strcmp(option, "--rule") == 0 ||
            strcmp(option, "--index") == 0 ||
            strcmp(option, "--mode") == 0 ||
            strcmp(option, "--replacement") == 0 ||
            strcmp(option, "--alternate") == 0;
        if (index + 1 >= argc || !known) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc < 4 || !options_well_formed(argc, argv)) {
        usage(argv[0]);
        return 2;
    }
    bool list_rules = strcmp(argv[1], "list") == 0;
    bool mutate_rule = strcmp(argv[1], "mutate") == 0;
    bool list_forms = strcmp(argv[1], "list-forms") == 0;
    bool mutate_form = strcmp(argv[1], "mutate-form") == 0;
    const char *source = option_value(argc, argv, "--source");
    const char *out = option_value(argc, argv, "--out");
    const char *rule = option_value(argc, argv, "--rule");
    const char *index_text = option_value(argc, argv, "--index");
    const char *mode = option_value(argc, argv, "--mode");
    const char *replacement = option_value(argc, argv, "--replacement");
    const char *alternate = option_value(argc, argv, "--alternate");
    bool rule_command = list_rules || mutate_rule;
    bool form_command = list_forms || mutate_form;
    bool mutation_command = mutate_rule || mutate_form;
    if ((!rule_command && !form_command) || source == NULL ||
        ((list_rules || list_forms) &&
         (argc != 4 || out != NULL || rule != NULL ||
          index_text != NULL || mode != NULL || replacement != NULL ||
          alternate != NULL)) ||
        (mutate_rule &&
         ((argc != 10 && argc != 12) || out == NULL || rule == NULL ||
          index_text != NULL || mode == NULL || alternate != NULL)) ||
        (mutate_form &&
         ((argc != 10 && argc != 12 && argc != 14) ||
          out == NULL || rule != NULL ||
          index_text == NULL || mode == NULL)) ||
        (mutation_command && strcmp(source, out) == 0) ||
        (replacement != NULL &&
         ((!mutate_form && !mutate_rule) || mode == NULL ||
          strcmp(mode, "falsify") != 0)) ||
        (alternate != NULL && replacement == NULL)) {
        usage(argv[0]);
        return 2;
    }
    bool falsify = mode != NULL && strcmp(mode, "falsify") == 0;
    if (mutation_command && !falsify && strcmp(mode, "delete") != 0) {
        usage(argv[0]);
        return 2;
    }

    size_t selected_form_index = 0U;
    if (mutate_form && !parse_size(index_text, &selected_form_index)) {
        fprintf(stderr, "invalid form index: %s\n", index_text);
        return 2;
    }

    size_t length = 0U;
    unsigned char *bytes = read_file(source, &length);
    if (bytes == NULL) {
        return 2;
    }

    int status = 0;
    if (rule_command) {
        RuleSpans spans = {0};
        if (!collect_rules(bytes, length, &spans)) {
            status = 2;
        } else if (list_rules) {
            for (size_t index = 0U; index < spans.count; ++index) {
                RuleSpan span = spans.items[index];
                if (fwrite(bytes + span.name_start, 1U,
                           span.name_len, stdout) != span.name_len ||
                    fputc('\n', stdout) == EOF) {
                    fprintf(stderr, "cannot write rule inventory\n");
                    status = 2;
                    break;
                }
            }
        } else {
            const RuleSpan *selected = NULL;
            size_t rule_len = strlen(rule);
            for (size_t index = 0U; index < spans.count; ++index) {
                RuleSpan *candidate = &spans.items[index];
                if (candidate->name_len == rule_len &&
                    memcmp(bytes + candidate->name_start,
                           rule, rule_len) == 0) {
                    if (selected != NULL) {
                        fprintf(stderr,
                                "rule name is not unique: %s\n", rule);
                        status = 2;
                        break;
                    }
                    selected = candidate;
                }
            }
            if (status == 0 && selected == NULL) {
                fprintf(stderr, "rule not found: %s\n", rule);
                status = 2;
            } else if (status == 0 &&
                       !emit_mutation(out, bytes, length,
                                      selected, falsify, replacement)) {
                status = 2;
            }
        }
        free(spans.items);
    } else {
        FormSpans spans = {0};
        if (!collect_forms(bytes, length, &spans)) {
            status = 2;
        } else if (list_forms) {
            for (size_t index = 0U; index < spans.count; ++index) {
                FormSpan span = spans.items[index];
                if (printf("%zu\t", index) < 0 ||
                    fwrite(bytes + span.head_start, 1U,
                           span.head_len, stdout) != span.head_len ||
                    fputc('\n', stdout) == EOF) {
                    fprintf(stderr, "cannot write form inventory\n");
                    status = 2;
                    break;
                }
            }
        } else if (selected_form_index >= spans.count) {
            fprintf(stderr, "form index out of range: %zu\n",
                    selected_form_index);
            status = 2;
        } else if (!emit_form_mutation(
                       out, bytes, length,
                       &spans.items[selected_form_index],
                       falsify, replacement, alternate)) {
            status = 2;
        }
        free(spans.items);
    }

    free(bytes);
    return status;
}
