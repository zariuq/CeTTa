#include "gslt_language_manifest_v1.h"
#include "gslt_provider_runtime.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024 };

static unsigned checks;
static unsigned failures;
static const char manifest_sha[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char generator_sha[] =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

#define CHECK(condition, label)                                      \
    do {                                                             \
        checks++;                                                    \
        if (!(condition)) {                                          \
            fprintf(stderr, "FAIL: %s\n", (label));                  \
            failures++;                                              \
        }                                                            \
    } while (0)

#define MANIFEST_FIELDS                                               \
    "(name example) (syntax-backend he-reader-direct-v1) "             \
    "(term-abi finite-horn-quote-v1) (semantic-source \"base.metta\") " \
    "(observation bag) "

static const char catalog_source[] =
    "(gslt-provider-catalog-v1 (name example-providers) "
    "(language example) (profile extension) "
    "(provider z-last 2 \"example.z.v1\") "
    "(provider a-first 0 \"example.a.v1\"))";

static bool catalog_decode(Arena *arena, const char *source,
                           CettaGsltProviderCatalogV1 *catalog,
                           char error[ERROR_CAP]) {
    return cetta_gslt_provider_catalog_from_source_v1(
        arena, (const uint8_t *)source, strlen(source),
        "example/catalog.metta", manifest_sha, generator_sha,
        catalog, error, ERROR_CAP);
}

static bool manifest_decode(Arena *arena, const char *source,
                            const char *profile, GsltLanguageManifest *manifest,
                            char error[ERROR_CAP]) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    bool ok = count == 1 && forms &&
        cetta_gslt_language_manifest_parse_v1(
            forms[0], profile, manifest, error, ERROR_CAP);
    if (count != 1)
        (void)snprintf(error, ERROR_CAP, "expected one complete manifest");
    free(forms);
    return ok;
}

static void catalog_positives(void) {
    Arena arena;
    CettaGsltProviderCatalogV1 catalog;
    char error[ERROR_CAP] = {0};
    char digest[65];
    arena_init(&arena);
    bool ok = catalog_decode(&arena, catalog_source, &catalog, error);
    CHECK(ok, "catalog constructs from authored source");
    if (ok) {
        CHECK(cetta_gslt_provider_catalog_validate_v1(
                  &catalog, error, sizeof(error)),
              "constructed catalog agrees with source validator");
        CHECK(strcmp(catalog.name, "example-providers") == 0 &&
                  strcmp(catalog.language_name, "example") == 0 &&
                  catalog.profile_name &&
                  strcmp(catalog.profile_name, "extension") == 0,
              "catalog preserves name, language and selected profile");
        CHECK(catalog.requirement_count == 2u &&
                  strcmp(catalog.requirements[0].relation, "z-last") == 0 &&
                  catalog.requirements[0].arity == 2u &&
                  strcmp(catalog.requirements[0].semantic_id,
                         "example.z.v1") == 0 &&
                  strcmp(catalog.requirements[1].relation, "a-first") == 0 &&
                  catalog.requirements[1].arity == 0u,
              "requirements retain source order and zero arity");
        cetta_native_sha256_hex((const uint8_t *)catalog_source,
                                strlen(catalog_source), digest);
        CHECK(strcmp(catalog.source_sha256, digest) == 0 &&
                  catalog.source_length == strlen(catalog_source) &&
                  memcmp(catalog.source_bytes, catalog_source,
                         catalog.source_length) == 0,
              "catalog retains actual source bytes and their digest");
        CHECK(strcmp(catalog.language_manifest_sha256, manifest_sha) == 0 &&
                  strcmp(catalog.generator_sha256, generator_sha) == 0,
              "catalog retains supplied manifest and generator identities");

        CettaGsltProviderCatalogV1 changed = catalog;
        changed.source_sha256 = manifest_sha;
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses a wrong source digest");
        changed = catalog;
        changed.source_length--;
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses truncated source bytes");
        changed = catalog;
        changed.name = "other";
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses descriptor name differing from source");
        changed = catalog;
        changed.profile_name = NULL;
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses an omitted source profile");
        changed = catalog;
        changed.requirement_count--;
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses an omitted requirement");
        CettaGsltProviderRequirementV1 reversed[] = {
            catalog.requirements[1], catalog.requirements[0]};
        changed = catalog;
        changed.requirements = reversed;
        CHECK(!cetta_gslt_provider_catalog_validate_v1(
                  &changed, error, sizeof(error)),
              "validator refuses reordered requirements");
    }

    ok = catalog_decode(&arena,
        "(gslt-provider-catalog-v1 (name empty) (language example))",
        &catalog, error);
    CHECK(ok && catalog.requirement_count == 0u &&
              catalog.profile_name == NULL &&
              cetta_gslt_provider_catalog_validate_v1(
                  &catalog, error, sizeof(error)),
          "native catalog contract admits no declared providers");
    ok = catalog_decode(&arena,
        "(gslt-provider-catalog-v1 (name symbolic) (language example) "
        "(provider external 4294967295 example-symbolic-v1))",
        &catalog, error);
    CHECK(ok && catalog.requirement_count == 1u &&
              catalog.requirements[0].arity == UINT32_MAX &&
              strcmp(catalog.requirements[0].semantic_id,
                     "example-symbolic-v1") == 0 &&
              cetta_gslt_provider_catalog_validate_v1(
                  &catalog, error, sizeof(error)),
          "native catalog contract admits symbolic identity and uint32 arity");

    char owned_source[] =
        "(gslt-provider-catalog-v1 (name owned) (language example) "
        "(profile extension) (provider external 1 \"owned.identity\"))";
    char owned_name[] = "owned/catalog.metta";
    char owned_manifest[65], owned_generator[65];
    memcpy(owned_manifest, manifest_sha, sizeof(owned_manifest));
    memcpy(owned_generator, generator_sha, sizeof(owned_generator));
    ok = cetta_gslt_provider_catalog_from_source_v1(
        &arena, (const uint8_t *)owned_source, strlen(owned_source), owned_name,
        owned_manifest, owned_generator, &catalog, error, sizeof(error));
    memset(owned_source, 'x', sizeof(owned_source) - 1u);
    memset(owned_name, 'x', sizeof(owned_name) - 1u);
    memset(owned_manifest, '0', 64u);
    memset(owned_generator, '0', 64u);
    CHECK(ok && strcmp(catalog.name, "owned") == 0 &&
              strcmp(catalog.source_name, "owned/catalog.metta") == 0 &&
              strcmp(catalog.language_manifest_sha256, manifest_sha) == 0 &&
              strcmp(catalog.generator_sha256, generator_sha) == 0 &&
              cetta_gslt_provider_catalog_validate_v1(
                  &catalog, error, sizeof(error)),
          "catalog owns source, metadata and requirements in caller arena");
    arena_free(&arena);
}

static void catalog_negatives(void) {
    static const struct { const char *source; const char *label; } cases[] = {
        {"", "empty catalog source"},
        {"(other (name n) (language l))", "wrong catalog root"},
        {"(gslt-provider-catalog-v1 (name n))", "missing catalog language"},
        {"(gslt-provider-catalog-v1 (language l))", "missing catalog name"},
        {"(gslt-provider-catalog-v1 (name n) (name m) (language l))",
         "duplicate catalog name"},
        {"(gslt-provider-catalog-v1 (name n) (language l) (language m))",
         "duplicate catalog language"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(profile p) (profile q))", "duplicate catalog profile"},
        {"(gslt-provider-catalog-v1 (name n) (language l) (unknown x))",
         "unknown catalog field"},
        {"(gslt-provider-catalog-v1 (name n) (language l) extra)",
         "non-expression catalog field"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p 1 \"a\") (provider p 1 \"b\"))",
         "duplicate provider dispatch key"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p 1 \"a\") (provider q 2 \"a\"))",
         "duplicate provider semantic identity"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p -1 \"a\"))", "negative provider arity"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p 4294967296 \"a\"))", "overflowed provider arity"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p 1.5 \"a\"))", "noninteger provider arity"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider p 1 \"\"))", "empty provider identity"},
        {"(gslt-provider-catalog-v1 (name n) (language l) "
         "(provider \"\" 1 \"a\"))", "empty provider relation"},
        {"(gslt-provider-catalog-v1 (name n) (language l) (provider p 1))",
         "incomplete provider declaration"},
        {"(gslt-provider-catalog-v1 (name n) (language l)))",
         "malformed trailing parenthesis"},
        {"(gslt-provider-catalog-v1 (name n) (language l)) (extra)",
         "multiple catalog roots"},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Arena arena;
        CettaGsltProviderCatalogV1 catalog;
        char error[ERROR_CAP] = {0};
        arena_init(&arena);
        CHECK(!catalog_decode(&arena, cases[i].source, &catalog, error) &&
                  error[0] != '\0', cases[i].label);
        arena_free(&arena);
    }
    Arena arena;
    CettaGsltProviderCatalogV1 catalog;
    char error[ERROR_CAP] = {0};
    char nul_source[sizeof(catalog_source)];
    memcpy(nul_source, catalog_source, sizeof(nul_source));
    nul_source[10] = '\0';
    arena_init(&arena);
    CHECK(!cetta_gslt_provider_catalog_from_source_v1(
              &arena, (const uint8_t *)nul_source, sizeof(nul_source) - 1u,
              "catalog.metta", manifest_sha, generator_sha, &catalog,
              error, sizeof(error)), "embedded NUL source is refused");
    CHECK(!cetta_gslt_provider_catalog_from_source_v1(
              &arena, (const uint8_t *)catalog_source, strlen(catalog_source),
              "catalog.metta", "invalid", generator_sha, &catalog,
              error, sizeof(error)), "invalid manifest identity is refused");
    CHECK(!cetta_gslt_provider_catalog_from_source_v1(
              &arena, (const uint8_t *)catalog_source, strlen(catalog_source),
              "catalog.metta", manifest_sha, "invalid", &catalog,
              error, sizeof(error)), "invalid generator identity is refused");
    arena_free(&arena);
}

static void catalog_symbol_ownership(void) {
    SymbolTable temporary_symbols;
    SymbolTable *saved_symbols = g_symbols;
    BuiltinSyms saved_builtins = g_builtin_syms;
    Arena arena;
    CettaGsltProviderCatalogV1 catalog;
    char error[ERROR_CAP] = {0};
    arena_init(&arena);
    symbol_table_init(&temporary_symbols);
    symbol_table_init_builtins(&temporary_symbols, &g_builtin_syms);
    g_symbols = &temporary_symbols;
    bool ok = catalog_decode(&arena,
        "(gslt-provider-catalog-v1 (name independent-name) "
        "(language independent-language) (profile independent-profile) "
        "(provider independent-relation 1 independent-identity))",
        &catalog, error);
    g_symbols = saved_symbols;
    g_builtin_syms = saved_builtins;
    symbol_table_free(&temporary_symbols);
    CHECK(ok && strcmp(catalog.name, "independent-name") == 0 &&
              strcmp(catalog.language_name, "independent-language") == 0 &&
              strcmp(catalog.profile_name, "independent-profile") == 0 &&
              catalog.requirement_count == 1u &&
              strcmp(catalog.requirements[0].relation,
                     "independent-relation") == 0 &&
              strcmp(catalog.requirements[0].semantic_id,
                     "independent-identity") == 0,
          "constructed descriptor text outlives its source symbol table");
    CHECK(ok && cetta_gslt_provider_catalog_validate_v1(
                    &catalog, error, sizeof(error)),
          "owned descriptor validates with the original symbol table restored");
    arena_free(&arena);
}

static void manifest_positives(void) {
    static const char source[] =
        "(gslt-language-v1 " MANIFEST_FIELDS
        "(semantic-source \"second.metta\") (query-entry ask 2) "
        "(profile extension (semantic-source \"ext-a.metta\") "
        "(semantic-source \"ext-b.metta\")) "
        "(profile unused (semantic-source \"unused.metta\")))";
    Arena arena;
    GsltLanguageManifest manifest;
    char error[ERROR_CAP] = {0};
    arena_init(&arena);
    bool ok = manifest_decode(&arena, source, NULL, &manifest, error);
    CHECK(ok && manifest.semantic_source_count == 2u &&
              strcmp(manifest.semantic_sources[0], "base.metta") == 0 &&
              strcmp(manifest.semantic_sources[1], "second.metta") == 0 &&
              manifest.profile_count == 2u && manifest.profile_name == NULL,
          "base manifest preserves source order without profile extension");
    CHECK(ok && manifest.has_query_entry && !manifest.has_entry &&
              !manifest.has_request_pipeline && manifest.query_arity == 2u &&
              strcmp(manifest.query_relation, "ask") == 0,
          "query manifest retains its selected interface");
    ok = manifest_decode(&arena, source, "extension", &manifest, error);
    CHECK(ok && manifest.semantic_source_count == 4u &&
              strcmp(manifest.semantic_sources[0], "base.metta") == 0 &&
              strcmp(manifest.semantic_sources[1], "second.metta") == 0 &&
              strcmp(manifest.semantic_sources[2], "ext-a.metta") == 0 &&
              strcmp(manifest.semantic_sources[3], "ext-b.metta") == 0 &&
              strcmp(manifest.profile_name, "extension") == 0,
          "selected profile retains ordered base and extension sources");
    ok = manifest_decode(&arena,
        "(gslt-language-v1 " MANIFEST_FIELDS
        "(profile extension (semantic-source \"ext.metta\")) "
        "(semantic-source \"last-base.metta\") (query-entry ask 2))",
        "extension", &manifest, error);
    CHECK(ok && manifest.semantic_source_count == 3u &&
              strcmp(manifest.semantic_sources[0], "base.metta") == 0 &&
              strcmp(manifest.semantic_sources[1], "last-base.metta") == 0 &&
              strcmp(manifest.semantic_sources[2], "ext.metta") == 0,
          "profile extends all base sources even when fields are interleaved");
    ok = manifest_decode(&arena,
        "(gslt-language-v1 " MANIFEST_FIELDS
        "(program-carrier nil cons) (entry run 3 0 2))",
        NULL, &manifest, error);
    CHECK(ok && manifest.has_entry && !manifest.has_query_entry &&
              manifest.entry_arity == 3u && manifest.program_position == 0u &&
              manifest.result_position == 2u &&
              strcmp(manifest.program_cons, "cons") == 0,
          "entry manifest retains carrier and argument positions");
    ok = manifest_decode(&arena,
        "(gslt-language-v1 " MANIFEST_FIELDS
        "(program-carrier nil cons) "
        "(request-pipeline classify produce observe results-nil results-cons))",
        NULL, &manifest, error);
    CHECK(ok && manifest.has_request_pipeline && !manifest.has_entry &&
              !manifest.has_query_entry &&
              strcmp(manifest.classify_relation, "classify") == 0 &&
              strcmp(manifest.produce_relation, "produce") == 0 &&
              strcmp(manifest.observe_relation, "observe") == 0 &&
              strcmp(manifest.produced_nil, "results-nil") == 0 &&
              strcmp(manifest.produced_cons, "results-cons") == 0,
          "request-pipeline manifest retains all declared stages");
    arena_free(&arena);
}

static void manifest_negatives(void) {
    static const struct {
        const char *source; const char *profile; const char *label;
    } cases[] = {
        {"(other " MANIFEST_FIELDS "(query-entry ask 2))", NULL,
         "wrong manifest root"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(name duplicate) "
         "(query-entry ask 2))", NULL, "duplicate manifest field"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) "
         "(unknown x))", NULL, "unknown manifest field"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) x)",
         NULL, "malformed manifest field"},
        {"(gslt-language-v1 " MANIFEST_FIELDS ")", NULL,
         "missing manifest entry mode"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2))",
         "absent", "unknown selected profile"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) "
         "(profile p (semantic-source \"a\")) "
         "(profile p (semantic-source \"b\")))", NULL,
         "duplicate declared profile"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) "
         "(profile p (entry other 2 0 1)))", NULL,
         "unsupported profile field even when unselected"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) "
         "(program-carrier nil cons))", NULL,
         "query entry with forbidden program carrier"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2) "
         "(entry run 2 0 1) (program-carrier nil cons))", NULL,
         "conflicting manifest entry modes"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 0))", NULL,
         "zero query arity"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(entry run 4294967295 0 1) "
         "(program-carrier nil cons))", NULL, "reserved maximum entry arity"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry \"\" 2))", NULL,
         "empty query relation"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(semantic-source \"\") "
         "(query-entry ask 2))", NULL, "empty semantic source path"},
        {"(gslt-language-v1 (name n) (syntax-backend reader) "
         "(term-abi finite-horn-quote-v1) (query-entry ask 2) "
         "(observation bag) (profile only (semantic-source \"extension\")))",
         "only", "selected extension cannot replace a missing base"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(entry run 2 0 2) "
         "(program-carrier nil cons))", NULL, "out-of-range result position"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(entry run 2 0 0) "
         "(program-carrier nil cons))", NULL, "overlapping entry positions"},
        {"(gslt-language-v1 (name n) (syntax-backend reader) "
         "(term-abi unsupported) (semantic-source \"a\") "
         "(query-entry ask 2) (observation bag))", NULL,
         "unsupported term ABI"},
        {"(gslt-language-v1 (name n) (syntax-backend reader) "
         "(term-abi finite-horn-quote-v1) (semantic-source \"a\") "
         "(query-entry ask 2) (observation unsupported))", NULL,
         "unsupported observation"},
        {"(gslt-language-v1 (name n) (syntax-backend reader) "
         "(term-abi finite-horn-quote-v1) (query-entry ask 2) "
         "(observation bag))", NULL, "missing semantic source"},
        {"(gslt-language-v1 " MANIFEST_FIELDS "(query-entry ask 2)))", NULL,
         "malformed manifest tail"},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Arena arena;
        GsltLanguageManifest manifest;
        char error[ERROR_CAP] = {0};
        arena_init(&arena);
        CHECK(!manifest_decode(&arena, cases[i].source, cases[i].profile,
                               &manifest, error) && error[0] != '\0',
              cases[i].label);
        arena_free(&arena);
    }
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    catalog_positives();
    catalog_negatives();
    catalog_symbol_ownership();
    manifest_positives();
    manifest_negatives();
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("GSLT metadata decoders: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
