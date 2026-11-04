#include "cetta_embed.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_bool watching;
static bool persistent;
static atomic_size_t calls;
static size_t failure = SIZE_MAX;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static bool refuse(void) {
    if (!watching) return false;
    size_t index = calls++;
    if (persistent ? index >= failure : index == failure) { errno = ENOMEM; return true; }
    return false;
}
void *__wrap_malloc(size_t size) { return refuse() ? NULL : __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { return refuse() ? NULL : __real_calloc(count, size); }
void *__wrap_realloc(void *pointer, size_t size) { return refuse() ? NULL : __real_realloc(pointer, size); }

static cetta_status_t run(cetta_engine_t *engine, const char *text, cetta_result_t **result) {
    return cetta_embed_run(engine, text, strlen(text), NULL, result);
}
typedef struct { cetta_engine_t *engine; unsigned opened, closed, pulls; } CallbackContext;
static cetta_status_t callback_open(void *pointer, cetta_engine_t *engine,
        cetta_atom_t *const *arguments, size_t arity, void **cursor) {
    (void)arguments; (void)arity;
    CallbackContext *context = pointer;
    context->engine = engine; context->opened++; context->pulls = 0u;
    *cursor = context;
    return CETTA_OK;
}
static cetta_status_t callback_next(void *pointer, cetta_callback_state_t *state, cetta_atom_t **output) {
    CallbackContext *context = pointer;
    if (context->pulls++) { *state = CETTA_CALLBACK_COMPLETE; return CETTA_OK; }
    *state = CETTA_CALLBACK_VALUE;
    return cetta_embed_atom_integer(context->engine, 42, output);
}
static void callback_close(void *pointer, cetta_callback_close_mode_t mode) {
    (void)mode;
    ((CallbackContext *)pointer)->closed++;
}
static void answer_42(cetta_engine_t *engine) {
    cetta_result_t *result = NULL;
    assert(run(engine, "!(+ 20 22)", &result) == CETTA_OK);
    cetta_atom_t *answer = NULL;
    int64_t value = 0;
    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
    assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK && value == 42);
    cetta_embed_atom_release(answer); cetta_embed_result_release(result);
}
static void check_stored(cetta_space_t *space, cetta_atom_t *pattern) {
    cetta_query_t *query = NULL;
    assert(cetta_embed_space_match(space, pattern, NULL, NULL, &query) == CETTA_OK);
    cetta_query_step_t step = {.size = sizeof(step)};
    cetta_result_t *result = NULL;
    assert(cetta_embed_query_step(query, 4096u, &step, &result) == CETTA_OK);
    assert(step.state == CETTA_QUERY_STATE_ANSWER && cetta_embed_result_count(result) == 1u);
    cetta_atom_t *value = NULL;
    int equal = 0;
    assert(cetta_embed_result_get(result, 0u, &value) == CETTA_OK);
    assert(cetta_embed_atom_equal(pattern, value, &equal) == CETTA_OK && equal);
    cetta_embed_atom_release(value); cetta_embed_atom_release(step.bindings);
    cetta_embed_atom_release(step.delayed); cetta_embed_result_release(result);
    cetta_embed_query_release(query);
}
static cetta_status_t collect_matches(cetta_space_t *space, cetta_atom_t *pattern, size_t *answers) {
    cetta_query_t *query = NULL;
    *answers = 0u;
    cetta_status_t status = cetta_embed_space_match(space, pattern, NULL, NULL, &query);
    while (status == CETTA_OK) {
        cetta_query_step_t step = {.size = sizeof(step)};
        cetta_result_t *batch = NULL;
        status = cetta_embed_query_step(query, 4096u, &step, &batch);
        *answers += cetta_embed_result_count(batch);
        cetta_embed_result_release(batch);
        cetta_embed_atom_release(step.bindings); cetta_embed_atom_release(step.delayed);
        if (status != CETTA_OK || step.state == CETTA_QUERY_STATE_COMPLETE ||
            step.state == CETTA_QUERY_STATE_STOPPED) break;
    }
    cetta_embed_query_release(query);
    return status;
}
int main(int argc, char **argv) {
    assert(argc == 5);
    const char *language = argv[1], *action = argv[2];
    failure = (size_t)strtoull(argv[3], NULL, 10);
    persistent = !strcmp(argv[4], "persistent");
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION, language, NULL, NULL};
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new_limited(&config, 128u * 1024u * 1024u, &engine) == CETTA_OK);
    cetta_atom_t *source = NULL, *kept = NULL;
    assert(cetta_embed_parse(engine, "(+ 20 22)", 9u, &source) == CETTA_OK);
    assert(cetta_embed_atom_integer(engine, 19, &kept) == CETTA_OK);
    cetta_space_t *space = NULL;
    assert(cetta_embed_engine_space(engine, &space) == CETTA_OK);
    cetta_atom_t *pattern = NULL;
    if (!strcmp(action, "match")) {
        for (unsigned i = 0u; i < 18u; i++) {
            char text[64];
            int n = snprintf(text, sizeof(text), "(row %u %u)", i, i + 100u);
            cetta_atom_t *fact = NULL;
            assert(cetta_embed_parse(engine, text, (size_t)n, &fact) == CETTA_OK);
            assert(cetta_embed_space_add_atom(space, fact) == CETTA_OK);
            cetta_embed_atom_release(fact);
        }
        assert(cetta_embed_parse(engine, "(row $x $y)", 11u, &pattern) == CETTA_OK);
    }
    uint64_t before = 0u;
    assert(cetta_embed_space_count(space, &before) == CETTA_OK);
    CallbackContext context = {0};
    cetta_callback_descriptor_t descriptor = {.size = sizeof(descriptor), .arity = 0u,
        .kind = CETTA_CALLBACK_SINGLE, .context = &context,
        .open = callback_open, .next = callback_next, .close = callback_close};
    cetta_atom_t *callback = NULL;
    if (!strcmp(action, "callback"))
        assert(cetta_embed_callback_new(engine, &descriptor, &callback) == CETTA_OK);
    cetta_result_t *result = NULL;
    cetta_atom_t *parsed = NULL;
    cetta_query_t *query = NULL;
    cetta_buffer_t *buffer = NULL;
    cetta_status_t status = CETTA_INVALID_ARGUMENT;
    watching = true;
    if (!strcmp(action, "parse")) {
        const char *text = "(row (a b c) (d e f) $x $x)";
        status = cetta_embed_parse(engine, text, strlen(text), &parsed);
    } else if (!strcmp(action, "eval")) status = cetta_embed_eval(engine, source, NULL, &result);
    else if (!strcmp(action, "run")) status = run(engine, "!(+ 1 2) !(add-atom &self (retained 7)) !(+ 20 22)", &result);
    else if (!strcmp(action, "hyperpose")) status = run(engine, "!(hyperpose ((+ 1 2) (+ 3 4)))", &result);
    else if (!strcmp(action, "space")) status = cetta_embed_space_add_atom(space, source);
    else if (!strcmp(action, "integer")) status = cetta_embed_atom_integer(engine, 123, &parsed);
    else if (!strcmp(action, "bigint")) status = cetta_embed_atom_integer_decimal(engine,
        "123456789012345678901234567890123456789012345678901234567890", &parsed);
    else if (!strcmp(action, "rational")) status = cetta_embed_atom_rational_decimal(engine, "12345678901234567890/7", &parsed);
    else if (!strcmp(action, "string")) status = cetta_embed_atom_string_n(engine, "a\0bcde", 6u, &parsed);
    else if (!strcmp(action, "large-string")) {
        static char bytes[8192];
        status = cetta_embed_atom_string_n(engine, bytes, sizeof(bytes), &parsed);
    }
    else if (!strcmp(action, "callback")) status = cetta_embed_call(engine, callback, NULL, 0u, NULL, &result);
    else if (!strcmp(action, "register")) status = cetta_embed_register_callback(engine, "host:refusal", &descriptor, &parsed);
    else if (!strcmp(action, "expression")) {
        cetta_atom_t *children[] = {source, kept, kept};
        status = cetta_embed_atom_expression(engine, children, 3u, &parsed);
    } else if (!strcmp(action, "format")) {
        char text[64]; size_t required = 0u;
        status = cetta_embed_atom_format(source, text, sizeof(text), &required);
        if (status == CETTA_OK) assert(required > 1u && required < sizeof(text));
    }
    else if (!strcmp(action, "buffer")) {
        int64_t data[] = {1, 2, 3, 4}; size_t shape[] = {4u};
        cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "i64",
            .allocation = data, .allocation_bytes = sizeof(data), .rank = 1u, .shape = shape};
        status = cetta_embed_buffer_copy(&spec, &buffer);
        if (status == CETTA_OK) status = cetta_embed_atom_buffer(engine, buffer, &parsed);
    }
    else if (!strcmp(action, "query")) {
        status = cetta_embed_query_open(engine, source, NULL, &query);
        if (status == CETTA_OK) {
            cetta_query_step_t step = {.size = sizeof(step)};
            status = cetta_embed_query_step(query, 4096u, &step, &result);
            cetta_embed_atom_release(step.bindings); cetta_embed_atom_release(step.delayed);
        }
    } else if (!strcmp(action, "match")) {
        size_t answers = 0u;
        status = collect_matches(space, pattern, &answers);
        if (failure == SIZE_MAX) assert(answers == 18u);
    }
    bool failed = failure < calls;
    assert(status == CETTA_OK || (failed && (status == CETTA_OUT_OF_MEMORY || status == CETTA_ENGINE_CLOSED)));
    cetta_embed_query_release(query);
    cetta_embed_result_release(result); cetta_embed_atom_release(parsed);
    cetta_embed_buffer_release(buffer);
    assert(context.opened == context.closed);
    watching = false;
    int64_t value;
    assert(cetta_embed_atom_get_integer(kept, &value) == CETTA_OK && value == 19);
    result = NULL;
    cetta_status_t reused = cetta_embed_eval(engine, source, NULL, &result);
    assert(reused == CETTA_OK || reused == CETTA_ENGINE_CLOSED);
    if (reused == CETTA_OK) {
        if (!strcmp(action, "match")) {
            size_t answers;
            assert(collect_matches(space, pattern, &answers) == CETTA_OK && answers == 18u);
        }
        cetta_atom_t *answer = NULL;
        assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
        assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK && value == 42);
        cetta_embed_atom_release(answer);
    }
    cetta_embed_result_release(result);
    if (reused == CETTA_OK) {
        uint64_t after = 0u;
        assert(cetta_embed_space_count(space, &after) == CETTA_OK);
        if (!strcmp(action, "space") || !strcmp(action, "run") || !strcmp(action, "register")) {
            assert(after == before || after == before + 1u);
            if (after > before) {
                if (!strcmp(action, "space")) check_stored(space, source);
                else if (!strcmp(action, "run")) {
                    cetta_atom_t *fact = NULL;
                    assert(cetta_embed_parse(engine, "(retained 7)", 12u, &fact) == CETTA_OK);
                    check_stored(space, fact); cetta_embed_atom_release(fact);
                } else {
                    result = NULL;
                    assert(run(engine, "!(host:refusal)", &result) == CETTA_OK);
                    cetta_atom_t *answer = NULL;
                    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
                    assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK && value == 42);
                    cetta_embed_atom_release(answer); cetta_embed_result_release(result);
                    assert(context.opened == context.closed);
                }
            }
        }
    }
    /* A fresh engine and retained pre-failure data remain independent. */
    cetta_engine_t *other = NULL;
    assert(cetta_embed_engine_new(&config, &other) == CETTA_OK);
    answer_42(other);
    cetta_embed_engine_close(other); cetta_embed_engine_release(other);
    cetta_embed_space_release(space); cetta_embed_atom_release(source);
    cetta_embed_atom_release(pattern);
    cetta_embed_atom_release(callback);
    watching = persistent && failed;
    cetta_embed_engine_close(engine);
    assert(cetta_embed_atom_get_integer(kept, &value) == CETTA_OK && value == 19);
    cetta_embed_atom_release(kept); cetta_embed_engine_release(engine);
    watching = false;
    printf("{\"calls\":%zu,\"status\":%d,\"failed\":%s,\"reused_status\":%d}\n",
        (size_t)calls, (int)status, failed ? "true" : "false", (int)reused);
    return 0;
}
