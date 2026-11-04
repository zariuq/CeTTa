#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool injecting;
static size_t fail_at, position;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size) {
    return injecting && position++ == fail_at ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    return injecting && position++ == fail_at ? NULL : __real_calloc(count, size);
}
static unsigned retains, releases;
static void retain(void *context) { (void)context; retains++; }
static void release(void *context) { (void)context; releases++; }

int main(void) {
    const double values[] = {1, 2, 3, 4};
    const size_t shape[] = {2, 2};
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "f64",
        .allocation = values, .allocation_bytes = sizeof(values), .rank = 2, .shape = shape};
    cetta_buffer_owner_t owner = {NULL, retain, release};
    unsigned refused = 0;
    for (unsigned copy = 0; copy < 2; ++copy) {
        bool reached_success = false;
        for (size_t fault = 0; fault < 32u; ++fault) {
            unsigned before = retains;
            cetta_buffer_t *buffer = NULL;
            fail_at = fault; position = 0u; injecting = true;
            cetta_status_t status = copy ? cetta_embed_buffer_copy(&spec, &buffer)
                : cetta_embed_buffer_borrow(&spec, &owner, &buffer);
            injecting = false;
            if (status == CETTA_OK) {
                assert(buffer);
                assert(retains == before + (copy ? 0u : 1u));
                cetta_embed_buffer_release(buffer);
                assert(retains == releases);
                reached_success = true;
                break;
            }
            assert(status == CETTA_OUT_OF_MEMORY && !buffer);
            assert(retains == before && retains == releases);
            refused++;
        }
        assert(reached_success);
    }
    assert(refused >= 12u);
    printf("Buffer allocation refusals: %u checked before external retain\n", refused);
    return 0;
}
