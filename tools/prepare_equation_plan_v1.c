#define _POSIX_C_SOURCE 200809L
#include "native/deterministic_equation_plan_v1.h"
#include "native/prepared_artifact_v1.h"
#include "src/symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build host only. The profile names bind these known primitive ABIs; no
 * primitive implementation is needed while admitting and serializing rules. */
static CettaDeterministicPrimitiveClassV1 classify(void *context, const char *head, uint32_t arity) {
    const char *abi = context;
    if (!strcmp(abi, "regular-token-v1") && arity == 3u && !strcmp(head, "rtv1:difference-edges"))
        return CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_STRUCTURE;
    if (!strcmp(abi, "parser-compiler-v1") && !strcmp(head, "ldpp-v1:text-equal"))
        return CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_ATOM;
    return CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
}

int main(int argc, char **argv) {
    if (argc < 4 || (strcmp(argv[1], "regular-token-v1") &&
        strcmp(argv[1], "parser-compiler-v1") && strcmp(argv[1], "bnfc-lexical-v1") &&
        strcmp(argv[1], "regex-selection-compiler-v1"))) {
        fprintf(stderr, "usage: prepare-equation-plan ABI OUTPUT SOURCE...\n"); return 2;
    }
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    CettaDeterministicVocabularyV1 vocabulary = {.classify = classify, .context = argv[1]};
    CettaDeterministicEquationPlanV1 *plan = NULL, *restored = NULL;
    CettaDeterministicEquationStatusV1 status;
    char error[512] = {0}; uint8_t *bytes = NULL; size_t len = 0;
    bool ok = cetta_deterministic_equation_plan_v1_load((const char *const *)(argv + 3), (size_t)argc - 3u,
        &vocabulary, &plan, &status, error, sizeof(error)) &&
        cetta_deterministic_equation_plan_v1_store(plan, argv[1], &bytes, &len, error, sizeof(error));
    /* A separately owned restoration is checked before publication. */
    cetta_deterministic_equation_plan_v1_free(plan);
    if (ok) ok = cetta_deterministic_equation_plan_v1_restore(bytes, len, argv[1], &vocabulary,
        true, &restored, error, sizeof(error));
    if (ok) ok = cetta_prepared_artifact_v1_publish(argv[2], bytes, len, error, sizeof(error));
    if (!ok) fprintf(stderr, "prepare-equation-plan: %s\n", error[0] ? error : "publication failed");
    cetta_deterministic_equation_plan_v1_free(restored);
    free(bytes); symbol_table_free(&symbols); g_symbols = NULL;
    return ok ? 0 : 1;
}
