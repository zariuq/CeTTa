#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdbool.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

/* Refuse allocations in the host adapter only. Core arena allocation has
 * a separate contract and must not be mistaken for an adapter refusal. */
static bool refuse_malloc, refuse_calloc;
static size_t malloc_index, malloc_failure = (size_t)-1;
static void *adapter_malloc(size_t size) {
    return refuse_malloc || malloc_index++ == malloc_failure ? NULL : malloc(size);
}
static void *adapter_calloc(size_t count, size_t size) {
    return refuse_calloc ? NULL : calloc(count, size);
}
#define malloc adapter_malloc
#define calloc adapter_calloc
#include "../../src/embedding.c"
#undef malloc
#undef calloc

int main(void) {
    cetta_engine_t *engine = NULL;
    refuse_calloc = true;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OUT_OF_MEMORY && !engine);
    refuse_calloc = false;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    cetta_space_t *space = NULL, *alias = NULL;
    cetta_atom_t *resource = NULL, *source = NULL;
    assert(cetta_embed_space_new(engine, &space) == CETTA_OK);
    assert(cetta_embed_space_atom(space, &resource) == CETTA_OK);
    unsigned references = atomic_load(&engine->references);
    refuse_malloc = true;
    assert(cetta_embed_space_from_atom(engine, resource, &alias) == CETTA_OUT_OF_MEMORY && !alias);
    refuse_malloc = false;
    assert(atomic_load(&engine->references) == references);
    const char text[] = "(superpose (11 22))";
    assert(cetta_embed_parse(engine, text, sizeof(text) - 1u, &source) == CETTA_OK);
    references = atomic_load(&engine->references);
    cetta_query_t *query = NULL;
    refuse_calloc = true;
    assert(cetta_embed_query_open(engine, source, NULL, &query) == CETTA_OUT_OF_MEMORY && !query);
    refuse_calloc = false;
    assert(atomic_load(&engine->references) == references);
    refuse_malloc = true;
    assert(cetta_embed_query_open(engine, source, NULL, &query) == CETTA_OUT_OF_MEMORY && !query);
    refuse_malloc = false;
    assert(atomic_load(&engine->references) == references);
    assert(cetta_embed_query_open(engine, source, NULL, &query) == CETTA_OK);
    cetta_query_step_t step = {.size = sizeof(step)};
    cetta_result_t *result = NULL;
    refuse_calloc = true;
    assert(cetta_embed_query_step(query, UINT64_MAX, &step, &result) == CETTA_OUT_OF_MEMORY && !result);
    refuse_calloc = false;
    step = (cetta_query_step_t){.size = sizeof(step)};
    assert(cetta_embed_query_step(query, UINT64_MAX, &step, &result) == CETTA_OK);
    assert(step.state == CETTA_QUERY_STATE_ANSWER && cetta_embed_result_count(result) == 1u);
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
    int64_t integer = 0;
    assert(cetta_embed_atom_get_integer(answer, &integer) == CETTA_OK && integer == 11);
    cetta_embed_atom_release(step.bindings);
    cetta_embed_atom_release(step.delayed);
    cetta_embed_result_release(result);
    cetta_embed_query_release(query);
    cetta_buffer_t *keys = NULL, *values = NULL;
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "index",
        .allocation = (uint64_t[]){0, 2}, .allocation_bytes = 16,
        .shape = (size_t[]){2}, .rank = 1};
    assert(cetta_embed_buffer_copy(&spec, &keys) == CETTA_OK);
    spec.dtype = "f64"; spec.allocation = (double[]){0, 7};
    assert(cetta_embed_buffer_copy(&spec, &values) == CETTA_OK);
    cetta_sparse_descriptor_t sparse = {.size = sizeof(sparse), .layout = CETTA_LAYOUT_COO,
        .shape = (size_t[]){4}, .rank = 1, .coordinates = keys, .values = values};
    cetta_atom_t *tensor = NULL;
    references = atomic_load(&engine->references);
    refuse_malloc = true;
    assert(cetta_embed_tensor_from_sparse(engine, &sparse, CETTA_DUPLICATES_REJECT, CETTA_COPY_NEVER, &tensor) == CETTA_OUT_OF_MEMORY && !tensor);
    refuse_malloc = false; malloc_index = 0; malloc_failure = 1;
    assert(cetta_embed_tensor_from_sparse(engine, &sparse, CETTA_DUPLICATES_REJECT, CETTA_COPY_NEVER, &tensor) == CETTA_OUT_OF_MEMORY && !tensor);
    malloc_failure = (size_t)-1;
    assert(atomic_load(&engine->references) == references);
    assert(atomic_load(&keys->references) == 1u && atomic_load(&values->references) == 1u);
    assert(cetta_embed_tensor_from_sparse(engine, &sparse, CETTA_DUPLICATES_REJECT, CETTA_COPY_NEVER, &tensor) == CETTA_OK);
    assert(atomic_load(&keys->references) == 2u && atomic_load(&values->references) == 2u);
    cetta_sparse_components_t parts = {.size = sizeof(parts)};
    refuse_malloc = true;
    assert(cetta_embed_tensor_components(tensor, &parts) == CETTA_OUT_OF_MEMORY && !parts.values && !parts.coordinates);
    refuse_malloc = false;
    cetta_embed_atom_release(tensor);
    assert(atomic_load(&keys->references) == 1u && atomic_load(&values->references) == 1u);
    cetta_embed_buffer_release(keys); cetta_embed_buffer_release(values);
    cetta_embed_atom_release(source);
    cetta_embed_atom_release(resource);
    cetta_embed_space_release(space);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    assert(cetta_embed_atom_get_integer(answer, &integer) == CETTA_OK && integer == 11);
    cetta_embed_atom_release(answer);
    puts("embedding owner refusals: no leaked engine references or skipped first answer");
    return 0;
}
