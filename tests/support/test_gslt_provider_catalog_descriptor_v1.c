#include "gslt_provider_runtime.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

extern const CettaGsltProviderCatalogV1 metadata_native_catalog_v1;
#ifndef CATALOG_UTF8_CONTROL
extern const CettaGsltProviderCatalogV1 metadata_retained_catalog_v1;
#endif

static int same_text(const char *a, const char *b) {
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

/* Compare public descriptor observations, not generated formatting or layout. */
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: catalog-descriptor-observer GENERATOR_SHA256\n", stderr);
        return 2;
    }
    SymbolTable symbols;
    VarInternTable variables;
    char error[1024] = {0};
    const CettaGsltProviderCatalogV1 *a = &metadata_native_catalog_v1;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    int ok = cetta_gslt_provider_catalog_validate_v1(a, error, sizeof(error));
    if (!same_text(a->generator_sha256, argv[1])) {
        fputs("native descriptor differs from actual generator identity\n", stderr);
        ok = 0;
    }
#ifdef CATALOG_UTF8_CONTROL
    ok = ok && same_text(a->name, "native λ \"quote\" \\slash") &&
        same_text(a->language_name, "native-catalog-control") &&
        a->requirement_count == 1u &&
        same_text(a->requirements[0].relation, "external") &&
        a->requirements[0].arity == 1u &&
        same_text(a->requirements[0].semantic_id,
                  "control.λ.\"quote\".\\path.v1");
#else
    const CettaGsltProviderCatalogV1 *b = &metadata_retained_catalog_v1;
    ok = ok && cetta_gslt_provider_catalog_validate_v1(b, error, sizeof(error));
    ok = ok && same_text(a->name, b->name) &&
        same_text(a->language_name, b->language_name) &&
        same_text(a->profile_name, b->profile_name) &&
        same_text(a->language_manifest_sha256, b->language_manifest_sha256) &&
        same_text(a->source_name, b->source_name) &&
        same_text(a->source_sha256, b->source_sha256) &&
        a->source_length == b->source_length &&
        memcmp(a->source_bytes, b->source_bytes, a->source_length) == 0 &&
        a->requirement_count == b->requirement_count;
    for (size_t i = 0u; ok && i < a->requirement_count; i++) {
        ok = a->requirements[i].arity == b->requirements[i].arity &&
            same_text(a->requirements[i].relation, b->requirements[i].relation) &&
            same_text(a->requirements[i].semantic_id, b->requirements[i].semantic_id);
    }
#endif
    if (!ok) fprintf(stderr, "catalog descriptor observations differ: %s\n", error);
    if (ok) printf("CatalogVerified %s %zu\n", a->name, a->requirement_count);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}
