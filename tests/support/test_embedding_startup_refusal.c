#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool watching, persistent;
static size_t allocation, failure = SIZE_MAX;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static bool refuse(void) {
    if (!watching) return false;
    size_t at = allocation++;
    if (persistent ? at >= failure : at == failure) {
        errno = ENOMEM; return true;
    }
    return false;
}
void *__wrap_malloc(size_t size) { return refuse() ? NULL : __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { return refuse() ? NULL : __real_calloc(count, size); }
void *__wrap_realloc(void *pointer, size_t size) { return refuse() ? NULL : __real_realloc(pointer, size); }

static void answer_42(cetta_engine_t *engine) {
    const char source[] = "!(+ 20 22)";
    cetta_result_t *result = NULL;
    assert(cetta_embed_run(engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 1u);
    cetta_atom_t *answer = NULL;
    int64_t integer;
    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
    assert(cetta_embed_atom_get_integer(answer, &integer) == CETTA_OK && integer == 42);
    cetta_embed_atom_release(answer);
    cetta_embed_result_release(result);
}
int main(int argc, char **argv) {
    assert(argc == 5 || argc == 6);
    const char *language = argv[1], *profile = strcmp(argv[2], "-") ? argv[2] : NULL;
    failure = (size_t)strtoull(argv[3], NULL, 10);
    persistent = !strcmp(argv[4], "persistent");
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        language, profile, NULL};
    cetta_engine_t *engine = NULL;
    watching = true;
    uint64_t limit = argc == 6 ? strtoull(argv[5], NULL, 10) : UINT64_MAX;
    cetta_status_t status = cetta_embed_engine_new_limited(&config, limit, &engine);
    watching = false;
    if (failure == SIZE_MAX) {
        assert(status == CETTA_OK && engine);
        answer_42(engine);
        cetta_embed_engine_release(engine);
    } else assert(status == CETTA_OUT_OF_MEMORY && !engine);
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK && engine);
    answer_42(engine);
    cetta_embed_engine_release(engine);
    printf("{\"allocations\":%zu,\"status\":%d,\"recovered\":true}\n", allocation, (int)status);
    return 0;
}
