#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

/* Structural projection of the Lean-emitted authority catalog into the
 * existing native checker registry. This does not synthesize replay rules. */
#include "native/operational_language_def_v1.h"
#include "inference_checker.h"
#include "native_sha256.h"
#include "symbol.h"
#include "gslt_native_emission_v1.h"
#include <inttypes.h>
#include <stdarg.h>

typedef const CettaOpLangV1SExpr Term;
typedef struct {
    const char *catalog, *header, *source, *symbol, *include;
} Options;
typedef struct {
    const char *alias, *system_id, *revision, *digest;
    char *presentation, *goal, *proof;
} Authority;

static bool fail(char *error, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format); vsnprintf(error, size, format, args); va_end(args);
    return false;
}

static bool form(Term *term, const char *name, size_t arity) {
    return term && term->kind == CETTA_OP_LANG_V1_SEXPR_APPLICATION &&
        !strcmp(term->as.application.head, name) &&
        term->as.application.argument_len == arity;
}

static char *string_copy(Arena *arena, Term *term) {
    if (memchr(term->as.string.bytes, 0, term->as.string.len)) return NULL;
    size_t size = term->as.string.len;
    if (size == SIZE_MAX) return NULL;
    char *copy = arena_alloc(arena, size + 1u);
    memcpy(copy, term->as.string.bytes, size);
    copy[size] = 0;
    return copy;
}

static const char *text_value(Arena *arena, Term *term) {
    if (term->kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL) return term->as.symbol;
    if (term->kind == CETTA_OP_LANG_V1_SEXPR_STRING) return string_copy(arena, term);
    return NULL;
}

/* CettaWire.Term.render uses Lean String.quote: newline and tab are named,
 * other ASCII controls and DEL use lowercase two-digit hexadecimal escapes.
 * Unicode bytes are retained. The C registry itself uses C byte quoting. */
static void wire_string(FILE *out, const uint8_t *bytes, size_t size) {
    fputc('"', out);
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = bytes[i];
        if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c < 32u || c == 127u) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static bool render_term(FILE *out, Term *term) {
    switch (term->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        fputs(term->as.symbol, out); return true;
    case CETTA_OP_LANG_V1_SEXPR_NATURAL: {
        const char *number = term->as.natural;
        while (number[0] == '0' && number[1]) ++number;
        fputs(number, out); return true;
    }
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        if (memchr(term->as.string.bytes, 0, term->as.string.len)) return false;
        wire_string(out, term->as.string.bytes, term->as.string.len); return true;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        fprintf(out, "(%s", term->as.application.head);
        for (size_t i = 0; i < term->as.application.argument_len; ++i) {
            fputc(' ', out);
            if (!render_term(out, term->as.application.arguments[i])) return false;
        }
        fputc(')', out); return true;
    }
    return false;
}

static char *render(Term *term) {
    char *bytes = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&bytes, &size);
    if (!out) return NULL;
    bool ok = render_term(out, term);
    if (fclose(out)) ok = false;
    if (!ok) { free(bytes); return NULL; }
    return bytes;
}

/* Direct structure transport into the checker's existing Atom wire carrier. */
static Atom *lift(Arena *arena, Term *term) {
    switch (term->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        return atom_symbol(arena, term->as.symbol);
    case CETTA_OP_LANG_V1_SEXPR_STRING: {
        char *copy = string_copy(arena, term);
        return copy ? atom_string(arena, copy) : NULL;
    }
    case CETTA_OP_LANG_V1_SEXPR_NATURAL: {
        int64_t number;
        return cetta_bigint_text_fits_i64(term->as.natural, &number)
            ? atom_int(arena, number) : atom_bigint(arena, term->as.natural);
    }
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION: {
        size_t count = (size_t)term->as.application.argument_len + 1u;
        if (count > SIZE_MAX / sizeof(Atom *)) return NULL;
        Atom **items = malloc(count * sizeof(*items));
        if (!items) return NULL;
        items[0] = atom_symbol(arena, term->as.application.head);
        bool ok = true;
        for (size_t i = 1; i < count; ++i) {
            items[i] = lift(arena, term->as.application.arguments[i - 1u]);
            if (!items[i]) { ok = false; break; }
        }
        Atom *result = ok ? atom_expr(arena, items, count) : NULL;
        free(items); return result;
    }
    }
    return NULL;
}

static bool digest_valid(const char *digest) {
    if (!digest || strlen(digest) != 64u) return false;
    return strspn(digest, "0123456789abcdef") == 64u;
}

static void hash_u64(CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    for (size_t i = 0; i < 8; ++i) bytes[7u - i] = (uint8_t)(value >> (i * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void catalog_digest(const Authority *authorities, size_t count, char digest[65]) {
    static const uint8_t identity[] = "NIKAuthorityCatalogV1";
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, identity, sizeof(identity));
    hash_u64(&sha, count);
    for (size_t i = 0; i < count; ++i) {
        const char *fields[] = {authorities[i].alias, authorities[i].system_id,
            authorities[i].revision, authorities[i].digest};
        for (size_t j = 0; j < 4; ++j) {
            size_t size = strlen(fields[j]);
            hash_u64(&sha, size);
            cetta_native_sha256_update(&sha, (const uint8_t *)fields[j], size);
        }
    }
    cetta_native_sha256_finish_hex(&sha, digest);
}

static bool authority_read(Arena *arena, Term *term, Authority *authority,
                           char *error, size_t size) {
    if (!form(term, "authority", 6)) return fail(error, size, "expected authority with six fields");
    CettaOpLangV1SExpr **args = term->as.application.arguments;
    if (args[0]->kind != CETTA_OP_LANG_V1_SEXPR_SYMBOL)
        return fail(error, size, "authority alias must be a symbol");
    authority->alias = args[0]->as.symbol;
    authority->system_id = text_value(arena, args[1]);
    authority->revision = text_value(arena, args[2]);
    authority->digest = text_value(arena, args[3]);
    if (!*authority->alias || !authority->system_id || !*authority->system_id ||
        !authority->revision || !*authority->revision || !digest_valid(authority->digest))
        return fail(error, size, "invalid authority identity fields");
    if (!form(args[4], "GPresentationV1", 5) || !form(args[5], "positive", 2))
        return fail(error, size, "expected version-one presentation and positive specimen");
    authority->presentation = render(args[4]);
    authority->goal = render(args[5]->as.application.arguments[0]);
    authority->proof = render(args[5]->as.application.arguments[1]);
    if (!authority->presentation || !authority->goal || !authority->proof)
        return fail(error, size, "unsupported embedded NUL or allocation failure");
    char actual[65];
    cetta_native_sha256_hex((const uint8_t *)authority->presentation,
        strlen(authority->presentation), actual);
    if (strcmp(actual, authority->digest))
        return fail(error, size, "authority digest does not identify its canonical presentation");
    Atom *presentation = lift(arena, args[4]);
    if (!presentation) return fail(error, size, "cannot transport presentation structure");
    CettaInferenceChecker *checker = NULL;
    CettaInferenceStatus status = cetta_inference_checker_create(presentation, &checker, error, size);
    cetta_inference_checker_destroy(checker);
    return status == CETTA_INFERENCE_OK;
}

static bool header_include_valid(const char *name) {
    if (!name || !*name || name[0] == '/') return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == '/')) return false;
    return !strstr(name, "//") && !strstr(name, "..");
}

static void c_field_string(FILE *out, const char *value) {
    size_t size = strlen(value);
    while (size > 72u) {
        char chunk[73];
        memcpy(chunk, value, 72u);
        chunk[72] = 0;
        c_string(out, chunk);
        fputs("\n            ", out);
        value += 72u;
        size -= 72u;
    }
    c_string(out, value);
}

static bool emit(const Options *options, const Authority *authorities, size_t count,
                 const char digest[65], const char source_digest[65]) {
    char *header = NULL, *source = NULL;
    size_t header_size = 0, source_size = 0;
    FILE *h = open_memstream(&header, &header_size), *c = open_memstream(&source, &source_size);
    if (!h || !c) {
        if (h) fclose(h);
        if (c) fclose(c);
        free(header); free(source); return false;
    }
    fprintf(h, "#ifndef CETTA_GENERATED_%s_H\n#define CETTA_GENERATED_%s_H\n\n", options->symbol, options->symbol);
    fputs("#include <stddef.h>\n\ntypedef struct {\n"
        "    const char *alias;\n    const char *system_id;\n    const char *revision;\n"
        "    const char *digest;\n    const char *presentation_metta;\n"
        "    const char *positive_goal_metta;\n    const char *positive_proof_metta;\n"
        "} CettaNikAuthorityV1;\n\n", h);
    fprintf(h, "extern const CettaNikAuthorityV1 %s[];\nextern const size_t %s_count;\n"
        "extern const char %s_catalog_sha256[];\n\n#endif\n", options->symbol, options->symbol, options->symbol);
    fputs("/* Native projection of the authority catalog; no replay program is generated.\n", c);
    fprintf(c, " * Source SHA-256: %s\n */\n#include ", source_digest);
    c_string(c, options->include); fputs("\n\n", c);
    fprintf(c, "const CettaNikAuthorityV1 %s[] = {\n", options->symbol);
    for (size_t i = 0; i < count; ++i) {
        const char *names[] = {"alias", "system_id", "revision", "digest",
            "presentation_metta", "positive_goal_metta", "positive_proof_metta"};
        const char *values[] = {authorities[i].alias, authorities[i].system_id,
            authorities[i].revision, authorities[i].digest, authorities[i].presentation,
            authorities[i].goal, authorities[i].proof};
        fputs("    {\n", c);
        for (size_t j = 0; j < 7; ++j) {
            fprintf(c, "        .%s = ", names[j]); c_field_string(c, values[j]); fputs(",\n", c);
        }
        fputs("    },\n", c);
    }
    fprintf(c, "};\n\nconst size_t %s_count =\n    sizeof(%s) / sizeof(%s[0]);\n\n"
        "const char %s_catalog_sha256[] = ", options->symbol, options->symbol, options->symbol, options->symbol);
    c_string(c, digest); fputs(";\n", c);
    bool ok = fclose(h) == 0;
    if (fclose(c)) ok = false;
    if (ok) ok = write_changed(options->header, header, header_size) &&
        write_changed(options->source, source, source_size);
    free(header); free(source); return ok;
}

static bool options_read(int argc, char **argv, Options *options) {
    const char *names[] = {"--catalog", "--header", "--source", "--symbol", "--header-include"};
    const char **fields[] = {&options->catalog, &options->header, &options->source,
        &options->symbol, &options->include};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) return false;
        size_t j = 0; while (j < 5 && strcmp(argv[i], names[j])) ++j;
        if (j == 5 || *fields[j] || !*argv[i + 1]) return false;
        *fields[j] = argv[i + 1];
    }
    for (size_t i = 0; i < 5; ++i) if (!*fields[i]) return false;
    return identifier(options->symbol) && header_include_valid(options->include);
}

int main(int argc, char **argv) {
    Options options = {0};
    if (!options_read(argc, argv, &options)) {
        fputs("usage: nik-authority-catalog-v1 --catalog FILE --header FILE --source FILE --symbol C_NAME --header-include NAME\n", stderr);
        return 2;
    }
    char *header = output_path(options.header), *source = output_path(options.source);
    if (!header || !source || same_file(header, source) || same_file(header, options.catalog) ||
        same_file(source, options.catalog) || same_file(header, "/proc/self/exe") ||
        same_file(source, "/proc/self/exe")) {
        free(header); free(source); fputs("error: unavailable or aliased output\n", stderr); return 1;
    }
    options.header = header; options.source = source;
    size_t input_size = 0;
    uint8_t *input = read_bytes(options.catalog, &input_size);
    CettaOpLangV1Document document; cetta_op_lang_v1_document_init(&document);
    CettaOpLangV1Status status;
    char error[512] = {0}, digest[65], source_digest[65];
    bool ok = input && cetta_op_lang_v1_parse_commented_document_bytes(&document,
        input, input_size, UINT32_MAX, UINT32_MAX, &status, error, sizeof(error));
    if (!input) fail(error, sizeof(error), "cannot read catalog");
    Term *root = document.root;
    if (ok && (!root || root->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
        strcmp(root->as.application.head, "nik-authority-catalog-v1") || !root->as.application.argument_len))
        ok = fail(error, sizeof(error), "expected a nonempty authority catalog");
    size_t count = ok ? root->as.application.argument_len : 0;
    Authority *authorities = count ? calloc(count, sizeof(*authorities)) : NULL;
    if (ok && !authorities) ok = fail(error, sizeof(error), "cannot allocate registry");
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    Arena arena; arena_init(&arena); g_hashcons = NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = authority_read(&arena, root->as.application.arguments[i], &authorities[i], error, sizeof(error));
        for (size_t j = 0; ok && j < i; ++j)
            if (!strcmp(authorities[i].alias, authorities[j].alias) ||
                (!strcmp(authorities[i].system_id, authorities[j].system_id) &&
                 !strcmp(authorities[i].revision, authorities[j].revision) &&
                 !strcmp(authorities[i].digest, authorities[j].digest)))
                ok = fail(error, sizeof(error), "duplicate authority alias or identity");
    }
    if (ok) {
        catalog_digest(authorities, count, digest);
        cetta_native_sha256_hex(input, input_size, source_digest);
        ok = emit(&options, authorities, count, digest, source_digest);
        if (!ok) fail(error, sizeof(error), "cannot write registry outputs");
    }
    if (!ok) fprintf(stderr, "NikAuthorityCatalogProjectionError: %s\n", error);
    if (authorities) for (size_t i = 0; i < count; ++i) {
        free(authorities[i].presentation); free(authorities[i].goal); free(authorities[i].proof);
    }
    free(authorities); arena_free(&arena);
    var_intern_free(&variables); symbol_table_free(&symbols);
    g_symbols = NULL; g_var_intern = NULL;
    cetta_op_lang_v1_document_free(&document);
    free(input); free(header); free(source);
    return ok ? 0 : 1;
}
