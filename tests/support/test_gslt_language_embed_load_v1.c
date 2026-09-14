#include "gslt_language_runtime.h"
#include "symbol.h"
#include <stdio.h>

#define LANGUAGES(X) \
    X(petta_typecheck_v3_core_v1) X(gslt_il_language_v1) X(metta_interact_language_v1) \
    X(subzero_language_v1) X(zero_language_v1) X(zero_exp_language_v1) \
    X(zero_emit_language_v1) X(zero_interact_language_v1) X(zerouv_language_v1)
#define DECLARE(name) extern const CettaGsltEmbeddedLanguageV1 cetta_##name;
LANGUAGES(DECLARE)
#undef DECLARE

int main(void) {
#define ADDRESS(name) &cetta_##name,
    const CettaGsltEmbeddedLanguageV1 *descriptors[] = {LANGUAGES(ADDRESS)};
#undef ADDRESS
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    unsigned checks = 0, failures = 0;
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(*descriptors); i++) {
        for (unsigned variant = 0; variant < 3; variant++) {
            CettaGsltEmbeddedLanguageV1 descriptor = *descriptors[i];
            if (variant == 1) descriptor.compiled_plan.sha256 =
                "0000000000000000000000000000000000000000000000000000000000000000";
            if (variant == 2) descriptor.semantic_source_count = 0;
            CettaGsltLanguage *loaded = NULL;
            char error[1024] = {0};
            bool accepted = cetta_gslt_language_load_embedded_for_realization(
                &descriptor, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
                &loaded, error, sizeof(error));
            checks++;
            if ((variant == 0 && (!accepted || !loaded ||
                    cetta_gslt_language_semantic_rule_count(loaded) == 0)) ||
                (variant != 0 && (accepted || loaded || !*error))) {
                fprintf(stderr, "FAIL: %s variant %u: %s\n", descriptor.name, variant, error);
                failures++;
            }
            cetta_gslt_language_free(loaded);
        }
    }
    g_var_intern = NULL; var_intern_free(&variables);
    g_symbols = NULL; symbol_table_free(&symbols);
    printf("GsltLanguageNativeLoadV1: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
