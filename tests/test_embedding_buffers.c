#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct { atomic_uint references; int64_t values[6]; } Storage;
static atomic_uint retained, released, freed;
static void count_retain(void *pointer) { (void)pointer; atomic_fetch_add(&retained, 1u); }
static void count_release(void *pointer) { (void)pointer; atomic_fetch_add(&released, 1u); }
static void retain(void *pointer) {
    Storage *storage = pointer;
    atomic_fetch_add(&storage->references, 1u);
    atomic_fetch_add(&retained, 1u);
}
static void release(void *pointer) {
    Storage *storage = pointer;
    atomic_fetch_add(&released, 1u);
    if (atomic_fetch_sub(&storage->references, 1u) == 1u) {
        atomic_fetch_add(&freed, 1u);
        free(storage);
    }
}
static int64_t at(const cetta_buffer_t *buffer, size_t i) {
    cetta_buffer_descriptor_t info = {.size = sizeof(info)};
    assert(cetta_embed_buffer_describe(buffer, &info) == CETTA_OK);
    assert(info.rank == 1 && i < info.shape[0]);
    ptrdiff_t offset = (ptrdiff_t)info.offset + (ptrdiff_t)i * info.strides[0];
    assert(offset >= 0 && (size_t)offset + 8u <= info.allocation_bytes);
    int64_t value;
    memcpy(&value, (const unsigned char *)info.allocation + offset, 8u);
    return value;
}
static void *reader(void *pointer) {
    cetta_buffer_t *buffer = pointer;
    for (unsigned i = 0; i < 1000u; ++i) {
        cetta_embed_buffer_retain(buffer);
        assert(at(buffer, 0u) == 3 && at(buffer, 2u) == 1);
        cetta_embed_buffer_release(buffer);
    }
    return NULL;
}
int main(void) {
    Storage *storage = malloc(sizeof(*storage));
    assert(storage);
    atomic_init(&storage->references, 1u);
    memcpy(storage->values, (int64_t[]){1, 2, 3, 4, 5, 6}, sizeof(storage->values));
    const size_t shape[] = {2, 3};
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "i64",
        .allocation = storage->values, .allocation_bytes = sizeof(storage->values),
        .rank = 2, .shape = shape};
    cetta_buffer_owner_t owner = {storage, retain, release};
    cetta_buffer_t *buffer = NULL, *view = NULL, *escaped = NULL;
    assert(cetta_embed_buffer_borrow(&spec, &owner, &buffer) == CETTA_OK);
    assert(atomic_load(&retained) == 1u && atomic_load(&storage->references) == 2u);
    release(storage);
    cetta_buffer_descriptor_t info = {.size = sizeof(info)};
    assert(cetta_embed_buffer_describe(buffer, &info) == CETTA_OK);
    assert(info.allocation == storage->values && info.rank == 2u);
    assert(info.strides[0] == 24 && info.strides[1] == 8);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){3}, (ptrdiff_t[]){-8}, 1, 16, &view) == CETTA_OK);
    assert(at(view, 0) == 3 && at(view, 1) == 2 && at(view, 2) == 1);
    cetta_embed_buffer_release(buffer);
    assert(atomic_load(&freed) == 0u);
    pthread_t threads[3];
    for (size_t i = 0; i < 3u; ++i) assert(!pthread_create(&threads[i], NULL, reader, view));
    for (size_t i = 0; i < 3u; ++i) assert(!pthread_join(threads[i], NULL));
    cetta_engine_t *engine = NULL, *other = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    assert(cetta_embed_engine_new(NULL, &other) == CETTA_OK);
    cetta_atom_t *atom = NULL, *alias = NULL;
    assert(cetta_embed_atom_buffer(engine, view, &atom) == CETTA_OK);
    assert(cetta_embed_atom_buffer(other, view, &alias) == CETTA_OK);
    assert(cetta_embed_buffer_from_atom(atom, &escaped) == CETTA_OK);
    const char import[] = "!(import! &self (library tensor))";
    cetta_result_t *result = NULL;
    assert(cetta_embed_run(engine, import, sizeof(import) - 1u, NULL, &result) == CETTA_OK);
    cetta_embed_result_release(result);
    cetta_atom_t *head = NULL, *sum = NULL;
    assert(cetta_embed_atom_symbol(engine, "tensor:sum", &head) == CETTA_OK);
    assert(cetta_embed_call(engine, head, &atom, 1u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_get(result, 0u, &sum) == CETTA_OK);
    int64_t total;
    assert(cetta_embed_atom_get_integer(sum, &total) == CETTA_OK && total == 6);
    cetta_embed_atom_release(sum);
    cetta_embed_atom_release(head);
    cetta_embed_result_release(result);
    cetta_embed_buffer_release(view);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_close(other);
    cetta_embed_engine_release(engine);
    cetta_embed_engine_release(other);
    cetta_embed_atom_release(atom);
    cetta_embed_atom_release(alias);
    assert(at(escaped, 0u) == 3 && atomic_load(&freed) == 0u);
    cetta_embed_buffer_release(escaped);
    assert(atomic_load(&freed) == 1u && atomic_load(&released) == 2u);

    int64_t source[] = {10, 20, 30};
    spec = (cetta_buffer_descriptor_t){.size = sizeof(spec), .dtype = "i64",
        .allocation = source, .allocation_bytes = sizeof(source), .rank = 1, .shape = (size_t[]){3}};
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    source[0] = 999;
    assert(at(buffer, 0u) == 10);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){4}, (ptrdiff_t[]){0}, 1, 8, &view) == CETTA_OK);
    assert(at(view, 3u) == 20);
    cetta_embed_buffer_release(view);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){4}, (ptrdiff_t[]){8}, 1, 0, &view) == CETTA_INVALID_ARGUMENT && !view);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){3}, (ptrdiff_t[]){-8}, 1, 0, &view) == CETTA_INVALID_ARGUMENT && !view);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){3}, (ptrdiff_t[]){3}, 1, 0, &view) == CETTA_BUFFER_INCOMPATIBLE && !view);
    cetta_embed_buffer_release(buffer);
    unsigned before_retained = atomic_load(&retained), before_released = atomic_load(&released);
    cetta_buffer_owner_t counted = {NULL, count_retain, count_release};
    spec.shape = (size_t[]){4};
    assert(cetta_embed_buffer_borrow(&spec, &counted, &buffer) == CETTA_INVALID_ARGUMENT && !buffer);
    spec.shape = (size_t[]){3};
    spec.strides = (ptrdiff_t[]){-8};
    assert(cetta_embed_buffer_borrow(&spec, &counted, &buffer) == CETTA_INVALID_ARGUMENT && !buffer);
    spec.strides = NULL;
    _Alignas(int64_t) unsigned char unaligned[25];
    memcpy(unaligned + 1u, source, sizeof(source));
    spec.allocation = unaligned + 1u;
    assert(cetta_embed_buffer_borrow(&spec, &counted, &buffer) == CETTA_BUFFER_INCOMPATIBLE && !buffer);
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    assert(at(buffer, 0u) == 999);
    cetta_embed_buffer_release(buffer);
    assert(atomic_load(&retained) == before_retained && atomic_load(&released) == before_released);
    spec.allocation = source;
    spec.dtype = "complex128";
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_BUFFER_INCOMPATIBLE && !buffer);
    spec.dtype = "i64";
    uint16_t one = 1;
    spec.byte_order = *(unsigned char *)&one ? CETTA_BYTE_ORDER_BIG : CETTA_BYTE_ORDER_LITTLE;
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    assert(cetta_embed_buffer_describe(buffer, &info) == CETTA_OK && info.byte_order == spec.byte_order);
    assert(!memcmp(info.allocation, source, sizeof(source)));
    cetta_embed_buffer_release(buffer);
    spec.byte_order = (cetta_byte_order_t)3;
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_INVALID_ARGUMENT && !buffer);
    spec = (cetta_buffer_descriptor_t){.size = sizeof(spec), .dtype = "u8",
        .allocation = NULL, .allocation_bytes = 0, .rank = 2, .shape = (size_t[]){2, 0}};
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    assert(cetta_embed_buffer_describe(buffer, &info) == CETTA_OK && info.rank == 2u && info.shape[1] == 0u);
    cetta_embed_buffer_release(buffer);
    spec = (cetta_buffer_descriptor_t){.size = sizeof(spec), .dtype = "bool",
        .allocation = (uint8_t[]){0, 2}, .allocation_bytes = 2u, .rank = 1u, .shape = (size_t[]){2}};
    assert(cetta_embed_buffer_borrow(&spec, &counted, &buffer) == CETTA_INVALID_ARGUMENT && !buffer);
    assert(atomic_load(&retained) == before_retained && atomic_load(&released) == before_released);
    spec.shape = (size_t[]){1};
    assert(cetta_embed_buffer_borrow(&spec, &counted, &buffer) == CETTA_OK);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){1}, (ptrdiff_t[]){1}, 1u, 1u, &view) == CETTA_INVALID_ARGUMENT && !view);
    cetta_embed_buffer_release(buffer);
    assert(atomic_load(&retained) == before_retained + 1u && atomic_load(&released) == before_released + 1u);
    spec = (cetta_buffer_descriptor_t){.size = sizeof(spec), .dtype = "u8",
        .allocation = (uint8_t[]){7}, .allocation_bytes = 1u, .rank = 1u, .shape = (size_t[]){1}};
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    assert(cetta_embed_buffer_view(buffer, (size_t[]){SIZE_MAX}, (ptrdiff_t[]){0}, 1u, 0u, &view) == CETTA_OK);
    assert(cetta_embed_buffer_describe(view, &info) == CETTA_OK && info.shape[0] == SIZE_MAX && info.strides[0] == 0);
    cetta_embed_buffer_release(view);
    cetta_embed_buffer_release(buffer);
    puts("buffers: retained immutable views, exact ownership, cross-engine tensors, bounds and copies pass");
    return 0;
}
