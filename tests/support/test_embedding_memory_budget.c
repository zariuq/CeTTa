#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *language) {
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION, language, NULL, NULL};
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new_limited(&config, 0u, &engine) == CETTA_OUT_OF_MEMORY && !engine);
    assert(cetta_embed_engine_new_limited(&config, 16u * 1024u * 1024u, &engine) == CETTA_OK);
    cetta_atom_t *values[256] = {0};
    char bytes[256u * 1024u];
    memset(bytes, 'x', sizeof(bytes));
    size_t count = 0u;
    cetta_status_t status = CETTA_OK;
    while (count < 256u && status == CETTA_OK) {
        status = cetta_embed_atom_string_n(engine, bytes, sizeof(bytes), &values[count]);
        if (status == CETTA_OK) count++;
    }
    assert(status == CETTA_OUT_OF_MEMORY && count > 0u && count < 256u);
    size_t length;
    assert(cetta_embed_atom_get_text(values[0], NULL, 0u, &length) == CETTA_OK && length == sizeof(bytes));
    for (size_t i = 1u; i <= count; i++) cetta_embed_atom_release(values[i]);
    cetta_result_t *result = NULL;
    const char source[] = "!(+ 20 22)";
    assert(cetta_embed_run(engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK);
    cetta_atom_t *answer = NULL;
    int64_t integer;
    assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
    assert(cetta_embed_atom_get_integer(answer, &integer) == CETTA_OK && integer == 42);
    cetta_embed_atom_release(answer); cetta_embed_result_release(result);
    cetta_embed_engine_close(engine); cetta_embed_engine_release(engine);
    assert(cetta_embed_atom_get_text(values[0], bytes, sizeof(bytes), &length) == CETTA_OK);
    assert(length == sizeof(bytes) && bytes[0] == 'x' && bytes[length - 1u] == 'x');
    cetta_embed_atom_release(values[0]);
    printf("EmbeddingMemoryBudget %s: refusal, retained storage, recovery and close pass\n", language);
}
int main(void) { check("petta"); check("he"); return 0; }
