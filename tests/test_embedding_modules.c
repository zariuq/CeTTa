#define _POSIX_C_SOURCE 200809L
#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CETTA_EMBED_TEST_DIRECTORY
#define CETTA_EMBED_TEST_DIRECTORY "runtime"
#endif

typedef struct {
    cetta_engine_t *engine;
    cetta_query_t *query;
    unsigned starts;
} Context;
static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
    assert(!fclose(file));
}
static cetta_atom_t *parse(cetta_engine_t *engine, const char *text) {
    cetta_atom_t *atom = NULL;
    assert(cetta_embed_parse(engine, text, strlen(text), &atom) == CETTA_OK);
    return atom;
}
static void release_step(cetta_query_step_t *step, cetta_result_t *result) {
    cetta_embed_atom_release(step->bindings);
    cetta_embed_atom_release(step->delayed);
    cetta_embed_result_release(result);
}
static cetta_status_t open_callback(void *context, cetta_engine_t *engine,
        cetta_atom_t *const *arguments, size_t arity, void **cursor) {
    (void)engine; (void)arguments; assert(!arity);
    *cursor = context;
    return CETTA_OK;
}
static cetta_status_t start(void *pointer, cetta_callback_state_t *state, cetta_atom_t **output) {
    Context *context = pointer;
    assert(++context->starts == 1u && "a completed file was replayed");
    cetta_atom_t *source = parse(context->engine, "(superpose (7 (import! &self \"./late.metta\")))");
    assert(cetta_embed_query_open(context->engine, source, NULL, &context->query) == CETTA_OK);
    cetta_embed_atom_release(source);
    bool answered = false;
    for (unsigned i = 0u; i < 100u; ++i) {
        cetta_result_t *result = NULL;
        cetta_query_step_t step = {.size = sizeof(step)};
        assert(cetta_embed_query_step(context->query, 1u, &step, &result) == CETTA_OK);
        if (cetta_embed_result_count(result)) {
            cetta_atom_t *answer = NULL;
            int64_t value;
            assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
            assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK && value == 7);
            cetta_embed_atom_release(answer);
            answered = true;
        }
        release_step(&step, result);
        if (answered) break;
    }
    assert(answered);
    *state = CETTA_CALLBACK_VALUE;
    return cetta_embed_atom_integer(context->engine, 1, output);
}
static cetta_status_t resume(void *pointer, cetta_callback_state_t *state, cetta_atom_t **output) {
    Context *context = pointer;
    bool complete = false;
    for (unsigned i = 0u; i < 100u; ++i) {
        cetta_result_t *result = NULL;
        cetta_query_step_t step = {.size = sizeof(step)};
        assert(cetta_embed_query_step(context->query, 1u, &step, &result) == CETTA_OK);
        complete = step.state == CETTA_QUERY_STATE_COMPLETE;
        release_step(&step, result);
        if (complete) break;
    }
    assert(complete);
    cetta_embed_query_release(context->query);
    context->query = NULL;
    cetta_atom_t *head = parse(context->engine, "late-value");
    cetta_result_t *result = NULL;
    assert(cetta_embed_call(context->engine, head, NULL, 0u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 1u);
    assert(cetta_embed_result_get(result, 0u, output) == CETTA_OK);
    int64_t value;
    assert(cetta_embed_atom_get_integer(*output, &value) == CETTA_OK && value == 111);
    cetta_embed_atom_release(head);
    cetta_embed_result_release(result);
    *state = CETTA_CALLBACK_VALUE;
    return CETTA_OK;
}
static void close_callback(void *cursor, cetta_callback_close_mode_t mode) { (void)cursor; (void)mode; }
int main(void) {
    char directory[] = CETTA_EMBED_TEST_DIRECTORY "/embedding-modules-XXXXXX";
    assert(mkdtemp(directory));
    char first[512], second[512], late_first[512], late_second[512], missing[512], bad[512];
    snprintf(first, sizeof(first), "%s/first", directory);
    snprintf(second, sizeof(second), "%s/second", directory);
    assert(!mkdir(first, 0700) && !mkdir(second, 0700));
    snprintf(first, sizeof(first), "%s/first/main.metta", directory);
    snprintf(second, sizeof(second), "%s/second/main.metta", directory);
    snprintf(late_first, sizeof(late_first), "%s/first/late.metta", directory);
    snprintf(late_second, sizeof(late_second), "%s/second/late.metta", directory);
    snprintf(missing, sizeof(missing), "%s/missing.metta", directory);
    snprintf(bad, sizeof(bad), "%s/bad.metta", directory);
    write_file(first, "!(host:start)\n");
    write_file(second, "!(host:resume)\n");
    write_file(late_first, "(= (late-value) 111)\n");
    write_file(late_second, "(= (late-value) 222)\n");
    write_file(bad, "!(add-atom &self must-not-run)\n(\n");
    Context context = {0};
    assert(cetta_embed_engine_new(NULL, &context.engine) == CETTA_OK);
    cetta_callback_descriptor_t descriptor = {.size = sizeof(descriptor), .kind = CETTA_CALLBACK_SINGLE,
        .context = &context, .open = open_callback, .next = start, .close = close_callback};
    cetta_atom_t *callback = NULL;
    assert(cetta_embed_register_callback(context.engine, "host:start", &descriptor, &callback) == CETTA_OK);
    cetta_embed_atom_release(callback);
    descriptor.next = resume;
    assert(cetta_embed_register_callback(context.engine, "host:resume", &descriptor, &callback) == CETTA_OK);
    cetta_embed_atom_release(callback);
    cetta_result_t *result = NULL;
    assert(cetta_embed_load(context.engine, missing, NULL, &result) == CETTA_IO_ERROR);
    assert(cetta_embed_result_system_error(result) == ENOENT && !cetta_embed_result_count(result));
    cetta_embed_result_release(result);
    assert(cetta_embed_load(context.engine, bad, NULL, &result) == CETTA_PARSE_ERROR);
    cetta_embed_result_release(result);
    cetta_atom_t *source = parse(context.engine, "(match &self must-not-run yes)");
    assert(cetta_embed_eval(context.engine, source, NULL, &result) == CETTA_OK);
    assert(!cetta_embed_result_count(result));
    cetta_embed_atom_release(source); cetta_embed_result_release(result);
    assert(cetta_embed_load(context.engine, first, NULL, &result) == CETTA_OK);
    cetta_embed_result_release(result);
    assert(cetta_embed_load(context.engine, second, NULL, &result) == CETTA_OK);
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
    cetta_embed_result_release(result);
    cetta_atom_t *spec = NULL;
    assert(cetta_embed_atom_string(context.engine, first, &spec) == CETTA_OK);
    assert(cetta_embed_import(context.engine, NULL, spec, NULL, &result) == CETTA_OK);
    cetta_embed_atom_release(spec); cetta_embed_result_release(result);
    assert(context.starts == 1u);
    cetta_embed_engine_close(context.engine);
    cetta_embed_engine_release(context.engine);
    int64_t value;
    assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK && value == 111);
    cetta_embed_atom_release(answer);
    assert(!unlink(first) && !unlink(second) && !unlink(late_first) && !unlink(late_second) && !unlink(bad));
    snprintf(first, sizeof(first), "%s/first", directory);
    snprintf(second, sizeof(second), "%s/second", directory);
    assert(!rmdir(first) && !rmdir(second) && !rmdir(directory));
    puts("embedding modules: owned file origins survive split queries; completed imports, IO, parse and retained values pass");
    return 0;
}
