#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "gslt_language_manifest_v1.h"
#include "gslt_provider_runtime.h"
#include "native/gslt_composition_v1.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"
#include "gslt_native_emission_v1.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Metadata projection for existing provider catalogs. This does not compile
 * or execute their semantics, and does not choose a language's evaluator. */
typedef struct {
    const char *catalog, *manifest, *root, *header, *source, *symbol, *include;
} Options;

static bool fail(char *error, size_t size, const char *message) {
    (void)snprintf(error, size, "%s", message);
    return false;
}


static Atom *one_form(Arena *arena, const unsigned char *bytes, size_t length) {
    if (!length || memchr(bytes, 0, length)) return NULL;
    Atom **forms = NULL;
    int count = parse_metta_text((const char *)bytes, arena, &forms);
    Atom *root = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return root;
}


static bool head(const Atom *term, const char *name) {
    return term && term->kind == ATOM_EXPR && term->expr.len &&
        atom_is_symbol(term->expr.elems[0], name);
}

static bool application(const Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len &&
        term->expr.elems[0]->kind == ATOM_SYMBOL &&
        !cetta_gslt_source_variable_v1(term->expr.elems[0]);
}

static bool selected_source_admission(const CettaGsltCompositionV1 *composition,
                                      char *error, size_t size) {
    if (composition->equation_count)
        return fail(error, size, "provider source admission does not admit equations");
    for (size_t p = 0; p < composition->presentation_count; p++) {
        const CettaGsltPresentationV1 *source = &composition->presentations[p];
        for (size_t i = 0; i < source->operator_count; i++) {
            const CettaGsltOperatorV1 *op = &composition->operators[source->operator_begin + i];
            for (size_t j = 0; j < i; j++) {
                const CettaGsltOperatorV1 *prior = &composition->operators[source->operator_begin + j];
                if (op->arity == prior->arity && !strcmp(op->name, prior->name))
                    return fail(error, size, "duplicate operator in one presentation");
            }
        }
    }
    for (size_t i = 0; i < composition->rewrite_count; i++) {
        const CettaGsltRewriteV1 *rule = &composition->rewrites[i];
        if (!application(rule->head) ||
            !cetta_gslt_composition_validate_term_v1(composition, rule->head,
                                                   SIZE_MAX, error, size))
            return fail(error, size, "invalid authored rule head");
        for (CettaExprIndex j = 1; j < rule->body->expr.len; j++) {
            const Atom *goal = rule->body->expr.elems[j];
            if (!application(goal) ||
                !cetta_gslt_composition_validate_term_v1(composition, goal,
                                                       SIZE_MAX, error, size))
                return fail(error, size, "invalid authored rule premise");
        }
    }
    return true;
}

static bool source_requirements(Arena *arena, const char *manifest_path,
                               const GsltLanguageManifest *manifest,
                               const CettaGsltProviderCatalogV1 *catalog,
                               const Options *options,
                               char *error, size_t size) {
    char *directory = strdup(manifest_path);
    if (!directory) return fail(error, size, "cannot allocate source directory");
    char *slash = strrchr(directory, '/');
    if (!slash) { free(directory); return fail(error, size, "manifest path is not resolved"); }
    slash[1] = 0;
    Atom *sources[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES];
    bool ok = true;
    for (uint32_t i = 0; i < manifest->semantic_source_count; i++) {
        const char *relative = manifest->semantic_sources[i];
        size_t cap = strlen(directory) + strlen(relative) + 1u;
        char *path = malloc(cap);
        if (!path) { ok = false; break; }
        (void)snprintf(path, cap, "%s%s", relative[0] == '/' ? "" : directory, relative);
        if (same_file(path, options->header) || same_file(path, options->source)) {
            free(path); free(directory);
            return fail(error, size, "generated output aliases a semantic source");
        }
        size_t length = 0;
        unsigned char *bytes = read_bytes(path, &length);
        sources[i] = bytes ? one_form(arena, bytes, length) : NULL;
        free(bytes);
        free(path);
        if (!sources[i]) { ok = false; break; }
    }
    free(directory);
    if (!ok) return fail(error, size, "cannot read one complete semantic source");
    CettaGsltCompositionV1 composition = {0};
    ok = cetta_gslt_composition_build_v1(sources, manifest->semantic_source_count,
                                        &composition, error, size);
    if (ok) ok = selected_source_admission(&composition, error, size);
    for (size_t i = 0; ok && i < catalog->requirement_count; i++) {
        const CettaGsltProviderRequirementV1 *required = &catalog->requirements[i];
        if (!cetta_gslt_composition_has_operator_v1(&composition, required->relation,
                                                  required->arity)) {
            ok = fail(error, size, "provider is absent from authored signature");
            break;
        }
        for (size_t j = 0; j < composition.rewrite_count; j++) {
            const Atom *term = composition.rewrites[j].head;
            if (term->expr.len - 1u == required->arity && head(term, required->relation)) {
                ok = fail(error, size, "provider would override an authored rule");
                break;
            }
        }
    }
    cetta_gslt_composition_free_v1(&composition);
    return ok;
}


static bool emit(const Options *options, const CettaGsltProviderCatalogV1 *catalog) {
    char *header = NULL, *source = NULL;
    size_t header_length = 0, source_length = 0;
    FILE *h = open_memstream(&header, &header_length);
    FILE *c = open_memstream(&source, &source_length);
    if (!h || !c) {
        if (h) fclose(h);
        if (c) fclose(c);
        free(header); free(source);
        return false;
    }
    fprintf(h, "#ifndef CETTA_GENERATED_%s_H\n#define CETTA_GENERATED_%s_H\n\n",
            options->symbol, options->symbol);
    fprintf(h, "#include \"gslt_provider_runtime.h\"\n\nextern const CettaGsltProviderCatalogV1 %s;\n\n#endif\n",
            options->symbol);
    fprintf(c, "#include \"%s\"\n\n", options->include);
    fprintf(c, "static const uint8_t %s_source_v1[] = {\n", options->symbol);
    for (size_t i = 0; i < catalog->source_length; i++) {
        if (i % 16u == 0) fputs("    ", c);
        fprintf(c, "0x%02x,%s", catalog->source_bytes[i],
                i % 16u == 15u || i + 1u == catalog->source_length ? "\n" : " ");
    }
    fprintf(c, "};\n\nstatic const CettaGsltProviderRequirementV1 %s_requirements_v1[] = {\n",
            options->symbol);
    for (size_t i = 0; i < catalog->requirement_count; i++) {
        const CettaGsltProviderRequirementV1 *r = &catalog->requirements[i];
        fputs("    {.relation = ", c); c_string(c, r->relation);
        fprintf(c, ", .arity = %uu, .semantic_id = ", r->arity);
        c_string(c, r->semantic_id); fputs("},\n", c);
    }
    fprintf(c, "};\n\nconst CettaGsltProviderCatalogV1 %s = {\n", options->symbol);
#define STRING_FIELD(field) do { fputs("    ." #field " = ", c); \
    c_string(c, catalog->field); fputs(",\n", c); } while (0)
    STRING_FIELD(name); STRING_FIELD(language_name); STRING_FIELD(profile_name);
    STRING_FIELD(language_manifest_sha256);
    fprintf(c, "    .source_bytes = %s_source_v1,\n    .source_length = sizeof(%s_source_v1),\n",
            options->symbol, options->symbol);
    STRING_FIELD(source_name); STRING_FIELD(source_sha256);
    fprintf(c, "    .requirements = %s_requirements_v1,\n    .requirement_count = %zuu,\n",
            options->symbol, catalog->requirement_count);
    STRING_FIELD(generator_sha256);
#undef STRING_FIELD
    fputs("};\n", c);
    bool ok = !ferror(h) && !ferror(c);
    if (fclose(h)) ok = false;
    if (fclose(c)) ok = false;
    if (ok) ok = write_changed(options->header, header, header_length) &&
                 write_changed(options->source, source, source_length);
    free(header); free(source);
    return ok;
}


static bool options_read(int argc, char **argv, Options *options) {
    const char *names[] = {"--catalog", "--language-manifest", "--source-root",
        "--header", "--source", "--symbol", "--header-include"};
    const char **fields[] = {&options->catalog, &options->manifest, &options->root,
        &options->header, &options->source, &options->symbol, &options->include};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) return false;
        size_t j = 0;
        while (j < 7u && strcmp(argv[i], names[j])) j++;
        if (j == 7u || *fields[j] || !argv[i + 1][0]) return false;
        *fields[j] = argv[i + 1];
    }
    for (size_t j = 0; j < 7u; j++) if (!*fields[j]) return false;
    if (!identifier(options->symbol)) return false;
    for (const unsigned char *p = (const unsigned char *)options->include; *p; p++)
        if (*p > 127u || !(isalnum(*p) || strchr("_./-", *p))) return false;
    return strcmp(options->header, options->source) != 0;
}

int main(int argc, char **argv) {
    Options options = {0};
    if (!options_read(argc, argv, &options)) {
        fputs("usage: gslt-provider-catalog-v1 --catalog FILE --language-manifest FILE --source-root DIR --header FILE --source FILE --symbol NAME --header-include NAME\n", stderr);
        return 2;
    }
    char error[1024] = {0};
    char *catalog_path = realpath(options.catalog, NULL);
    char *manifest_path = realpath(options.manifest, NULL);
    char *root_path = realpath(options.root, NULL);
    char *header_path = output_path(options.header);
    char *source_path = output_path(options.source);
    size_t root_length = root_path ? strlen(root_path) : 0;
    bool ok = catalog_path && manifest_path && root_path && root_length &&
        !strncmp(catalog_path, root_path, root_length) &&
        (root_length == 1u || catalog_path[root_length] == '/');
    if (!ok) fail(error, sizeof(error), "catalog escapes source root or an input is absent");
    if (ok && (!header_path || !source_path || same_file(header_path, source_path) ||
        same_file(header_path, catalog_path) || same_file(header_path, manifest_path) ||
        same_file(source_path, catalog_path) || same_file(source_path, manifest_path) ||
        same_file(header_path, "/proc/self/exe") || same_file(source_path, "/proc/self/exe") ||
        same_file(header_path, argv[0]) || same_file(source_path, argv[0])))
        ok = fail(error, sizeof(error), "output is absent or aliases another output or input");
    if (ok) { options.header = header_path; options.source = source_path; }
    size_t catalog_length = 0, manifest_length = 0, executable_length = 0;
    unsigned char *catalog_bytes = ok ? read_bytes(catalog_path, &catalog_length) : NULL;
    unsigned char *manifest_bytes = ok ? read_bytes(manifest_path, &manifest_length) : NULL;
    unsigned char *executable = ok ? read_bytes("/proc/self/exe", &executable_length) : NULL;
    if (ok && !executable) executable = read_bytes(argv[0], &executable_length);
    if (ok && (!catalog_bytes || !manifest_bytes || !executable))
        ok = fail(error, sizeof(error), "cannot read catalog, manifest or generator executable");
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena arena;
    arena_init(&arena);
    CettaGsltProviderCatalogV1 catalog = {0};
    GsltLanguageManifest manifest = {0};
    if (ok) {
        char manifest_sha[65], generator_sha[65];
        cetta_native_sha256_hex(manifest_bytes, manifest_length, manifest_sha);
        /* Exact generator binary identity, not a source-to-target proof or a
         * platform-independent source fingerprint. */
        cetta_native_sha256_hex(executable, executable_length, generator_sha);
        const char *source_name = catalog_path + root_length + (root_length == 1u ? 0u : 1u);
        ok = cetta_gslt_provider_catalog_from_source_v1(&arena, catalog_bytes,
            catalog_length, source_name, manifest_sha, generator_sha,
            &catalog, error, sizeof(error));
    }
    if (ok) {
        /* Preserve the existing generator envelope, independently of the
         * broader runtime catalog decoder. */
        Atom *root = one_form(&arena, catalog_bytes, catalog_length);
        ok = root && catalog.requirement_count > 0u;
        for (CettaExprIndex i = 1u; ok && i < root->expr.len; i++) {
            Atom *field = root->expr.elems[i];
            if (head(field, "provider"))
                ok = field->expr.elems[3]->kind == ATOM_GROUNDED &&
                     field->expr.elems[3]->ground.gkind == GV_STRING;
        }
        if (!ok) fail(error, sizeof(error), "generator requires providers with quoted semantic identities");
    }
    if (ok) {
        Atom *root = one_form(&arena, manifest_bytes, manifest_length);
        ok = root && cetta_gslt_language_manifest_parse_v1(root, catalog.profile_name,
                &manifest, error, sizeof(error));
        if (!root) fail(error, sizeof(error), "expected one complete manifest");
    }
    if (ok && strcmp(catalog.language_name, manifest.name))
        ok = fail(error, sizeof(error), "catalog language differs from manifest");
    if (ok) ok = source_requirements(&arena, manifest_path, &manifest, &catalog,
                                      &options, error, sizeof(error));
    if (ok && !emit(&options, &catalog))
        ok = fail(error, sizeof(error), "cannot write generated catalog");
    if (!ok) fprintf(stderr, "error: %s\n", error);
    arena_free(&arena);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    free(catalog_path); free(manifest_path); free(root_path);
    free(header_path); free(source_path);
    free(catalog_bytes); free(manifest_bytes); free(executable);
    return ok ? 0 : 1;
}
