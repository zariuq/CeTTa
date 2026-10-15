#include "identity_counter.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

typedef struct {
    _Atomic uint64_t *next;
    uint64_t values[16];
} Worker;

static void *take_many(void *argument) {
    Worker *worker = argument;
    for (unsigned i = 0; i < 16; i++)
        worker->values[i] = cetta_identity_try_take(worker->next);
    return NULL;
}

int main(void) {
    _Atomic uint64_t next = UINT64_MAX - 37u;
    Worker workers[4] = {{.next = &next}, {.next = &next},
                         {.next = &next}, {.next = &next}};
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++)
        assert(pthread_create(&threads[i], NULL, take_many, &workers[i]) == 0);
    for (unsigned i = 0; i < 4; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    unsigned found[37] = {0}, count = 0;
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 16; j++) {
            uint64_t value = workers[i].values[j];
            if (!value)
                continue;
            assert(value >= UINT64_MAX - 37u && value < UINT64_MAX);
            assert(found[value - (UINT64_MAX - 37u)]++ == 0);
            count++;
        }
    assert(count == 37u);
    assert(atomic_load(&next) == UINT64_MAX);
    assert(cetta_identity_try_take(&next) == 0u);
    assert(atomic_load(&next) == UINT64_MAX);
    atomic_store(&next, 0u);
    assert(cetta_identity_try_take(&next) == 0u);
    assert(atomic_load(&next) == 0u);
    puts("PASS: concurrent near-exhaustion identity allocation never wraps or duplicates");
    return 0;
}
