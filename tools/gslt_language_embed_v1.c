#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "gslt_language_manifest_v1.h"
#include "finite_horn_gslt_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "symbol.h"
#include "gslt_native_emission_v1.h"

#include <errno.h>
#include <inttypes.h>

/* Packaging for the existing finite-horn-quote-v1 descriptor consumers.
 * The admitted source tree is serialized to their existing CGP1 wire ABI.
 * No query engine, rule optimization, BNF lowering or new semantic IR is
 * introduced here. Other target ABIs require their own explicit admission. */
typedef const FHGSLTSourceNodeV1 Node;
typedef struct {
    const char *manifest, *root, *profile, *header, *source, *symbol, *include;
} Options;
typedef struct {
    FILE *file;
    char *bytes;
    size_t length;
    uint32_t count;
} Section;
typedef struct {
    Section nodes, children, rules, bodies;
    Node **variables;
    size_t variable_count, variable_capacity;
    char *error;
    size_t error_size;
} Encoder;

static bool fail(char *error, size_t size, const char *message) {
    (void)snprintf(error, size, "%s", message);
    return false;
}

static Node *child(Node *node, size_t index) {
    return fhgslt_source_child_v1(node, index);
}

static bool named(Node *node, const char *name) {
    size_t length;
    const uint8_t *text = fhgslt_source_text_v1(node, &length);
    return fhgslt_source_kind_v1(node) == FHGSLT_SOURCE_V1_SYMBOL &&
        length == strlen(name) && !memcmp(text, name, length);
}

static void integer(FILE *out, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) fputc((int)((value >> (8u * i)) & 255u), out);
}

static bool wire_text(FILE *out, const uint8_t *text, size_t length) {
    /* The existing runtime stores decoded text as NUL-terminated strings. */
    if (length > UINT32_MAX || (length && memchr(text, 0, length))) return false;
    integer(out, length, 4);
    return (!length || fwrite(text, 1u, length, out) == length) && !ferror(out);
}

static bool wire_node_text(FILE *out, Node *node) {
    size_t length;
    const uint8_t *text = fhgslt_source_text_v1(node, &length);
    return text && wire_text(out, text, length);
}

static bool variable_slot(Encoder *e, Node *node, uint32_t *slot) {
    size_t length;
    const uint8_t *text = fhgslt_source_text_v1(node, &length);
    for (size_t i = 0; i < e->variable_count; i++) {
        size_t prior_length;
        const uint8_t *prior = fhgslt_source_text_v1(e->variables[i], &prior_length);
        if (length == prior_length && !memcmp(text, prior, length)) {
            *slot = (uint32_t)i;
            return true;
        }
    }
    if (e->variable_count == UINT32_MAX) return false;
    if (e->variable_count == e->variable_capacity) {
        size_t cap = e->variable_capacity ? e->variable_capacity * 2u : 16u;
        if (cap < e->variable_capacity || cap > SIZE_MAX / sizeof(*e->variables)) return false;
        Node **grown = realloc(e->variables, cap * sizeof(*grown));
        if (!grown) return false;
        e->variables = grown;
        e->variable_capacity = cap;
    }
    *slot = (uint32_t)e->variable_count;
    e->variables[e->variable_count++] = node;
    return true;
}

static bool encode_term(Encoder *e, Node *term, unsigned depth, uint32_t *id) {
    if (!term || depth > 4096u || e->nodes.count == UINT32_MAX)
        return fail(e->error, e->error_size, "CGP1 node/depth capacity exceeded");
    unsigned kind = 0;
    uint32_t offset = 0, count = 0, slot = 0;
    int64_t number = 0;
    const uint8_t *text = (const uint8_t *)"";
    size_t length = 0;
    switch (fhgslt_source_kind_v1(term)) {
    case FHGSLT_SOURCE_V1_SYMBOL: kind = 1; break;
    case FHGSLT_SOURCE_V1_VARIABLE:
        kind = 2;
        if (!variable_slot(e, term, &slot))
            return fail(e->error, e->error_size, "cannot allocate rule-local variable slots");
        break;
    case FHGSLT_SOURCE_V1_STRING: kind = 3; break;
    case FHGSLT_SOURCE_V1_INTEGER: {
        kind = 4;
        text = fhgslt_source_text_v1(term, &length);
        if (!text || !length || length > 20u)
            return fail(e->error, e->error_size, "CGP1 integer is outside signed 64-bit range");
        char buffer[21], *end;
        memcpy(buffer, text, length); buffer[length] = 0;
        errno = 0;
        intmax_t value = strtoimax(buffer, &end, 10);
        if (errno || end != buffer + length || value < INT64_MIN || value > INT64_MAX)
            return fail(e->error, e->error_size, "CGP1 integer is outside signed 64-bit range");
        number = (int64_t)value;
        break;
    }
    case FHGSLT_SOURCE_V1_LIST: {
        kind = 5;
        size_t arity = fhgslt_source_child_count_v1(term);
        if (!arity || arity - 1u > UINT32_MAX ||
            arity - 1u > SIZE_MAX / sizeof(uint32_t) ||
            fhgslt_source_kind_v1(child(term, 0)) != FHGSLT_SOURCE_V1_SYMBOL)
            return fail(e->error, e->error_size, "CGP1 requires symbol-headed applications");
        count = (uint32_t)(arity - 1u);
        uint32_t *ids = count ? malloc(count * sizeof(*ids)) : NULL;
        if (count && !ids) return fail(e->error, e->error_size, "cannot allocate application children");
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; i++)
            ok = encode_term(e, child(term, (size_t)i + 1u), depth + 1u, &ids[i]);
        if (ok && count > UINT32_MAX - e->children.count)
            ok = fail(e->error, e->error_size, "CGP1 child capacity exceeded");
        if (ok) {
            offset = e->children.count;
            for (uint32_t i = 0; i < count; i++) integer(e->children.file, ids[i], 4);
            e->children.count += count;
        }
        free(ids);
        if (!ok) return false;
        text = fhgslt_source_text_v1(child(term, 0), &length);
        break;
    }
    default: return fail(e->error, e->error_size, "invalid admitted source node");
    }
    if (kind == 1 || kind == 3) text = fhgslt_source_text_v1(term, &length);
    if (kind == 4) { text = (const uint8_t *)""; length = 0; }
    if (e->nodes.count == UINT32_MAX)
        return fail(e->error, e->error_size, "CGP1 node capacity exceeded");
    *id = e->nodes.count++;
    fputc((int)kind, e->nodes.file);
    integer(e->nodes.file, offset, 4); integer(e->nodes.file, count, 4);
    integer(e->nodes.file, (uint64_t)number, 8); integer(e->nodes.file, slot, 4);
    return wire_text(e->nodes.file, text, length) ||
        fail(e->error, e->error_size, "unrepresentable CGP1 text or output failure");
}

static bool encode_package(const FHGSLTPackage *package, char **bytes, size_t *length,
                           char *error, size_t error_size) {
    Encoder e = {.error = error, .error_size = error_size};
    Section *sections[] = {&e.nodes, &e.children, &e.rules, &e.bodies};
    bool ok = true;
    for (size_t i = 0; i < 4; i++) {
        sections[i]->file = open_memstream(&sections[i]->bytes, &sections[i]->length);
        if (!sections[i]->file) ok = false;
    }
    for (size_t p = 0; ok && p < fhgslt_package_presentation_count(package); p++) {
        Node *root = fhgslt_package_source_root_v1(package, p), *rules = NULL;
        for (size_t i = 2; i < fhgslt_source_child_count_v1(root); i++)
            if (named(child(child(root, i), 0), "rewrites")) rules = child(root, i);
        if (!rules) { ok = false; break; }
        for (size_t r = 1; ok && r < fhgslt_source_child_count_v1(rules); r++) {
            Node *rule = child(rules, r), *body = child(rule, 3);
            size_t count = fhgslt_source_child_count_v1(body) - 1u;
            uint32_t head_id = 0, offset = e.bodies.count;
            e.variable_count = 0;
            if (e.rules.count == UINT32_MAX || count > UINT32_MAX - e.bodies.count) {
                ok = fail(error, error_size, "CGP1 rule/body capacity exceeded"); break;
            }
            ok = encode_term(&e, child(child(rule, 2), 1), 0, &head_id);
            for (size_t b = 0; ok && b < count; b++) {
                uint32_t id = 0;
                ok = encode_term(&e, child(body, b + 1u), 0, &id);
                if (ok) { integer(e.bodies.file, id, 4); e.bodies.count++; }
            }
            if (ok) {
                integer(e.rules.file, head_id, 4); integer(e.rules.file, offset, 4);
                integer(e.rules.file, count, 4); integer(e.rules.file, e.variable_count, 4);
                ok = wire_node_text(e.rules.file, child(rule, 1));
                e.rules.count++;
            }
        }
    }
    for (size_t i = 0; i < 4; i++) {
        if (sections[i]->file && fclose(sections[i]->file)) ok = false;
    }
    FILE *out = ok ? open_memstream(bytes, length) : NULL;
    if (out) {
        fputs("CGP1", out);
        for (size_t i = 0; i < 4; i++) integer(out, sections[i]->count, 4);
        for (size_t i = 0; i < 4; i++)
            if (sections[i]->length && fwrite(sections[i]->bytes, 1,
                    sections[i]->length, out) != sections[i]->length) ok = false;
        if (fclose(out)) ok = false;
    } else ok = false;
    for (size_t i = 0; i < 4; i++) free(sections[i]->bytes);
    free(e.variables);
    if (!ok && !*error) fail(error, error_size, "cannot encode admitted package");
    return ok;
}

static void byte_array(FILE *out, const char *symbol, const char *suffix,
                       const uint8_t *bytes, size_t length) {
    fprintf(out, "static const uint8_t %s_%s[] = {\n", symbol, suffix);
    for (size_t i = 0; i < length; i++) {
        if (i % 16u == 0) fputs("    ", out);
        fprintf(out, "0x%02x,%s", bytes[i], i % 16u == 15u || i + 1u == length ? "\n" : " ");
    }
    fputs("};\n\n", out);
}

static void embedded_source(FILE *out, const char *symbol, const char *suffix,
                            const char *name, const uint8_t *bytes, size_t length) {
    char sha[65];
    cetta_native_sha256_hex(bytes, length, sha);
    fprintf(out, "{.input = {.bytes = %s_%s, .length = sizeof(%s_%s), .source = ",
            symbol, suffix, symbol, suffix);
    c_string(out, name); fputs("}, .sha256 = ", out); c_string(out, sha); fputc('}', out);
}

static bool emit(const Options *o, const GsltLanguageManifest *m,
                 const uint8_t *manifest, size_t manifest_length, const char *manifest_name,
                 const FHGSLTInput *inputs, const char *plan, size_t plan_length,
                 const char *compiler_sha) {
    char *header = NULL, *source = NULL, sha[65];
    size_t header_length = 0, source_length = 0;
    FILE *h = open_memstream(&header, &header_length);
    FILE *c = open_memstream(&source, &source_length);
    bool ok = h && c;
    if (ok) {
        fprintf(h, "#ifndef CETTA_GENERATED_%s_H\n#define CETTA_GENERATED_%s_H\n\n",
                o->symbol, o->symbol);
        fprintf(h, "#include \"gslt_language_runtime.h\"\n\nextern const CettaGsltEmbeddedLanguageV1 %s;\n\n#endif\n", o->symbol);
        fprintf(c, "#include \"%s\"\n\n", o->include);
        byte_array(c, o->symbol, "manifest_v1", manifest, manifest_length);
        for (uint32_t i = 0; i < m->semantic_source_count; i++) {
            char suffix[40]; snprintf(suffix, sizeof(suffix), "semantic_%u", i);
            byte_array(c, o->symbol, suffix, inputs[i].bytes, inputs[i].len);
        }
        byte_array(c, o->symbol, "compiled_plan_v1", (const uint8_t *)plan, plan_length);
        fprintf(c, "static const CettaGsltEmbeddedSourceV1 %s_sources[] = {\n", o->symbol);
        for (uint32_t i = 0; i < m->semantic_source_count; i++) {
            char suffix[40]; snprintf(suffix, sizeof(suffix), "semantic_%u", i);
            fputs("    ", c);
            embedded_source(c, o->symbol, suffix, m->semantic_sources[i], inputs[i].bytes, inputs[i].len);
            fputs(",\n", c);
        }
        fprintf(c, "};\n\nconst CettaGsltEmbeddedLanguageV1 %s = {\n", o->symbol);
#define TEXT_FIELD(field) do { fputs("    ." #field " = ", c); c_string(c, m->field); fputs(",\n", c); } while (0)
        TEXT_FIELD(name); TEXT_FIELD(profile_name); TEXT_FIELD(syntax_backend); TEXT_FIELD(term_abi);
        fputs("    .manifest = ", c);
        embedded_source(c, o->symbol, "manifest_v1", manifest_name, manifest, manifest_length);
        fprintf(c, ",\n    .semantic_sources = %s_sources,\n    .semantic_source_count = %uu,\n",
                o->symbol, m->semantic_source_count);
        cetta_native_sha256_hex((const uint8_t *)plan, plan_length, sha);
        fprintf(c, "    .compiled_plan = {.bytes = %s_compiled_plan_v1, .length = sizeof(%s_compiled_plan_v1), .sha256 = \"%s\"},\n",
                o->symbol, o->symbol, sha);
        TEXT_FIELD(program_nil); TEXT_FIELD(program_cons); TEXT_FIELD(entry_relation);
        fprintf(c, "    .entry_arity = %uu, .program_position = %uu, .result_position = %uu,\n",
                m->entry_arity, m->program_position, m->result_position);
        TEXT_FIELD(query_relation);
        fprintf(c, "    .query_arity = %uu,\n", m->query_arity);
        if (m->has_request_pipeline) {
            fputs("    .request_pipeline = &(const CettaGsltRequestPipelineV1){\n", c);
            TEXT_FIELD(classify_relation); TEXT_FIELD(produce_relation); TEXT_FIELD(observe_relation);
            TEXT_FIELD(produced_nil); TEXT_FIELD(produced_cons);
            fputs("    },\n", c);
        } else fputs("    .request_pipeline = NULL,\n", c);
        TEXT_FIELD(observation);
#undef TEXT_FIELD
        cetta_native_sha256_hex(manifest, manifest_length, sha);
        fprintf(c, "    .manifest_sha256 = \"%s\",\n    .compiler_sha256 = \"%s\",\n};\n", sha, compiler_sha);
        ok = !ferror(h) && !ferror(c);
    }
    if (h && fclose(h)) ok = false;
    if (c && fclose(c)) ok = false;
    if (ok) ok = write_changed(o->header, header, header_length) && write_changed(o->source, source, source_length);
    free(header); free(source);
    return ok;
}

static bool inside(const char *path, const char *root) {
    size_t n = strlen(root);
    return !strncmp(path, root, n) && (n == 1u || path[n] == '/');
}

static bool options_read(int argc, char **argv, Options *o) {
    const char *names[] = {"--manifest", "--source-root", "--profile", "--header", "--source", "--symbol", "--header-include"};
    const char **fields[] = {&o->manifest, &o->root, &o->profile, &o->header, &o->source, &o->symbol, &o->include};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) return false;
        size_t j = 0;
        while (j < 7 && strcmp(argv[i], names[j])) j++;
        if (j == 7 || *fields[j] || !*argv[i + 1]) return false;
        *fields[j] = argv[i + 1];
    }
    if (!o->manifest || !o->header || !o->source || !o->include || !identifier(o->symbol)) return false;
    for (const unsigned char *p = (const unsigned char *)o->include; *p; p++)
        if (*p > 127u || !(isalnum(*p) || strchr("_./-", *p))) return false;
    return true;
}

int main(int argc, char **argv) {
    Options o = {0};
    if (!options_read(argc, argv, &o)) {
        fputs("usage: gslt-language-embed-v1 --manifest FILE [--source-root DIR] [--profile NAME] --header FILE --source FILE --symbol NAME --header-include NAME\n", stderr);
        return 2;
    }
    char error[1024] = {0};
    char *manifest_path = realpath(o.manifest, NULL);
    char *directory = manifest_path ? strdup(manifest_path) : NULL;
    if (directory) strrchr(directory, '/')[1] = 0;
    char *root = directory ? realpath(o.root ? o.root : directory, NULL) : NULL;
    char *header = output_path(o.header), *source = output_path(o.source);
    bool ok = manifest_path && root && header && source && inside(manifest_path, root);
    if (!ok) fail(error, sizeof(error), "input/output missing or manifest escapes source root");
    if (ok && (same_file(header, source) || same_file(header, manifest_path) ||
        same_file(source, manifest_path) || same_file(header, "/proc/self/exe") ||
        same_file(source, "/proc/self/exe") || same_file(header, argv[0]) || same_file(source, argv[0])))
        ok = fail(error, sizeof(error), "output aliases another output or input");
    o.header = header; o.source = source;
    size_t manifest_length = 0, executable_length = 0, plan_length = 0;
    uint8_t *manifest_bytes = ok ? read_bytes(manifest_path, &manifest_length) : NULL;
    uint8_t *executable = ok ? read_bytes("/proc/self/exe", &executable_length) : NULL;
    if (ok && !executable) executable = read_bytes(argv[0], &executable_length);
    if (ok && (!manifest_bytes || !executable)) ok = fail(error, sizeof(error), "cannot read manifest or executable identity");
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols = &symbols; g_hashcons = NULL;
    Arena arena; arena_init(&arena);
    Atom *manifest_atom = NULL;
    GsltLanguageManifest m = {0};
    FHGSLTInput inputs[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES] = {{0}};
    FHGSLTPackage *package = NULL;
    char *plan = NULL;
    /* Ground data decoding does not turn source spellings into host values. */
    if (ok) ok = fh_ground_term_v1_read_source(&arena, manifest_bytes, manifest_length,
                &manifest_atom, error, sizeof(error)) &&
            cetta_gslt_language_manifest_parse_v1(manifest_atom, o.profile, &m, error, sizeof(error));
    for (uint32_t i = 0; ok && i < m.semantic_source_count; i++) {
        const char *relative = m.semantic_sources[i];
        size_t cap = strlen(directory) + strlen(relative) + 1u;
        char *joined = malloc(cap), *path = NULL;
        if (joined) {
            snprintf(joined, cap, "%s%s", *relative == '/' ? "" : directory, relative);
            path = realpath(joined, NULL);
        }
        free(joined);
        if (!path || !inside(path, root) || same_file(path, header) || same_file(path, source))
            ok = fail(error, sizeof(error), "semantic source missing, outside root, or aliases output");
        if (ok) {
            inputs[i].bytes = read_bytes(path, &inputs[i].len);
            inputs[i].source = relative;
            if (!inputs[i].bytes) ok = fail(error, sizeof(error), "cannot read semantic source");
        }
        free(path);
    }
    if (ok) ok = fhgslt_package_from_inputs(inputs, m.semantic_source_count, &package, error, sizeof(error));
    if (ok) ok = encode_package(package, &plan, &plan_length, error, sizeof(error));
    if (ok) {
        char compiler_sha[65];
        /* Exact executable identity; not a proof or a portable source hash. */
        cetta_native_sha256_hex(executable, executable_length, compiler_sha);
        ok = emit(&o, &m, manifest_bytes, manifest_length,
                  manifest_path + strlen(root) + (strlen(root) == 1u ? 0u : 1u),
                  inputs, plan, plan_length, compiler_sha);
        if (!ok) fail(error, sizeof(error), "cannot publish descriptor outputs");
    }
    if (!ok) fprintf(stderr, "error: %s\n", error);
    for (uint32_t i = 0; i < m.semantic_source_count; i++) free((void *)inputs[i].bytes);
    fhgslt_package_free(package); free(plan); arena_free(&arena);
    g_symbols = NULL; symbol_table_free(&symbols);
    free(executable); free(manifest_bytes); free(header); free(source); free(root); free(directory); free(manifest_path);
    return ok ? 0 : 1;
}
