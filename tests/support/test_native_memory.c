#include "native_memory.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static void *release_elsewhere(void *pointer) {
    cetta_native_free(pointer);
    return NULL;
}
static void *account_concurrently(void *pointer) {
    CettaNativeMemory *owner = pointer;
    CettaNativeMemoryScope scope;
    cetta_native_memory_enter(&scope, owner);
    for (size_t i = 0u; i < 4000u; i++) {
        char *data = cetta_native_malloc(31u + i % 73u);
        assert(data);
        data[0] = 23;
        char *grown = cetta_native_realloc(data, 257u + i % 101u);
        assert(grown && grown[0] == 23);
        cetta_native_free(grown);
    }
    cetta_native_memory_leave(&scope);
    return NULL;
}
int main(void) {
    CettaNativeMemory *first = cetta_native_memory_new(512u);
    CettaNativeMemory *second = cetta_native_memory_new(512u);
    assert(first && second);
    CettaNativeMemoryScope outer, inner;
    cetta_native_memory_enter(&outer, first);
    char *data = cetta_native_calloc(128u, 1u);
    assert(data && !data[0] && cetta_native_memory_used(first) >= 128u);
    assert(!cetta_native_malloc(512u));
    data[0] = 19;
    assert(!cetta_native_realloc(data, 1024u) && data[0] == 19);
    char *larger = cetta_native_realloc(data, 256u);
    assert(larger && larger[0] == 19 && cetta_native_memory_used(first) >= 256u);
    data = cetta_native_realloc(larger, 32u);
    assert(data && data[0] == 19 && cetta_native_memory_used(first) < 256u);
    cetta_native_memory_enter(&inner, second);
    void *other = cetta_native_malloc(64u);
    assert(other && cetta_native_memory_used(second) >= 64u);
    cetta_native_free(data);
    assert(cetta_native_memory_used(first) == 0u);
    assert(cetta_native_memory_used(second) >= 64u);
    char *foreign = malloc(16u);
    assert(foreign);
    foreign[0] = 11;
    char *adopted = cetta_native_realloc(foreign, 48u);
    assert(adopted && adopted[0] == 11);
    cetta_native_free(adopted);
    cetta_native_memory_leave(&inner);
    assert(cetta_native_memory_current() == first);
    cetta_native_memory_leave(&outer);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, release_elsewhere, other));
    assert(!pthread_join(thread, NULL));
    assert(cetta_native_memory_used(second) == 0u);
    cetta_native_memory_enter(&outer, first);
    other = cetta_native_malloc(16u);
    cetta_native_memory_leave(&outer);
    cetta_native_memory_release(first);
    cetta_native_free(other);
    cetta_native_memory_release(second);
    foreign = malloc(16u);
    cetta_native_free(foreign);
    CettaNativeMemory *shared = cetta_native_memory_new(1024u * 1024u);
    assert(shared);
    pthread_t concurrent[4];
    for (size_t i = 0u; i < 4u; i++)
        assert(!pthread_create(&concurrent[i], NULL, account_concurrently, shared));
    for (size_t i = 0u; i < 4u; i++) assert(!pthread_join(concurrent[i], NULL));
    cetta_native_memory_enter(&outer, shared);
    assert(cetta_native_memory_used(shared) == 0u && !cetta_native_memory_failed());
    assert(!cetta_native_memory_abandoned(shared));
    cetta_native_memory_abandon();
    cetta_native_memory_clear_failure(shared);
    assert(cetta_native_memory_abandoned(shared));
    cetta_native_memory_leave(&outer);
    cetta_native_memory_release(shared);
    puts("NativeMemory budget, refusal, resizing, nested owners and cross-thread release pass");
    return 0;
}
