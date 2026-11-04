#define _POSIX_C_SOURCE 200809L
#include "cetta_embed.h"
#include "library.h"
#include "text_source.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool refuse_capture, inject_read;
static size_t allocation, failure;
void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *pointer, size_t size) {
    return inject_read && allocation++ == failure ? NULL : __real_realloc(pointer, size);
}
CettaLibraryResourceContext *__real_cetta_library_resource_capture(CettaLibraryContext *);
CettaLibraryResourceContext *__wrap_cetta_library_resource_capture(CettaLibraryContext *context) {
    return refuse_capture ? NULL : __real_cetta_library_resource_capture(context);
}

int main(void) {
    char path[] = "runtime/embedding-read-refusal-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    assert(file);
    const size_t size = 131080u;
    unsigned char *source = malloc(size);
    assert(source);
    for (size_t i = 0u; i < size; ++i) source[i] = (unsigned char)i;
    assert(fwrite(source, 1u, size, file) == size && !fclose(file));
    unsigned refused = 0u;
    for (failure = 0u; failure < 4u; ++failure) {
        uint8_t *bytes = (void *)1;
        size_t length = 123u;
        int error = 123;
        allocation = 0u; inject_read = true;
        CettaTextReadStatus status = cetta_text_read_file(path, &bytes, &length, &error);
        inject_read = false;
        if (status == CETTA_TEXT_READ_OK) {
            assert(length == size && !memcmp(bytes, source, size) && bytes[size] == 0u);
            free(bytes);
            break;
        }
        assert(status == CETTA_TEXT_READ_MEMORY && !bytes && !length && !error);
        refused++;
    }
    assert(refused == 3u);
    free(source);

    file = fopen(path, "wb");
    assert(file && fputs("!(add-atom &self loaded-once)", file) >= 0 && !fclose(file));
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    cetta_result_t *result = NULL;
    failure = allocation = 0u; inject_read = true;
    assert(cetta_embed_load(engine, path, NULL, &result) == CETTA_OUT_OF_MEMORY);
    inject_read = false;
    assert(result && !cetta_embed_result_count(result) && !cetta_embed_result_system_error(result));
    cetta_embed_result_release(result);
    assert(cetta_embed_load(engine, path, NULL, &result) == CETTA_OK);
    cetta_embed_result_release(result);

    cetta_atom_t *expression = NULL;
    const char text[] = "(match &self loaded-once 42)";
    assert(cetta_embed_parse(engine, text, sizeof(text) - 1u, &expression) == CETTA_OK);
    cetta_query_t *query = NULL;
    refuse_capture = true;
    assert(cetta_embed_query_open(engine, expression, NULL, &query) == CETTA_OUT_OF_MEMORY && !query);
    refuse_capture = false;
    assert(cetta_embed_query_open(engine, expression, NULL, &query) == CETTA_OK);
    cetta_query_step_t step = {.size = sizeof(step)};
    assert(cetta_embed_query_step(query, UINT64_MAX, &step, &result) == CETTA_OK);
    assert(step.state == CETTA_QUERY_STATE_ANSWER && cetta_embed_result_count(result) == 1u);
    cetta_embed_atom_release(step.bindings);
    cetta_embed_atom_release(step.delayed);
    cetta_embed_result_release(result);
    cetta_embed_query_release(query);
    cetta_embed_atom_release(expression);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    assert(!unlink(path));
    puts("module allocation refusals: three reader growth points, file ingress and query context retry pass");
    return 0;
}
