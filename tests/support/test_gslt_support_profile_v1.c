#include "gslt_support_profile_v1.h"
#include "symbol.h"
#include "native/operational_language_def_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const CettaGsltSupportTransformProfileV1 cetta_mm2_gslt_profile_v1;

static unsigned checks, failures;
#define CHECK(test, label) do { checks++; if (!(test)) { \
    fprintf(stderr, "FAIL: %s\n", label); failures++; } } while (0)

static char *read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    long size;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        (uintmax_t)size >= SIZE_MAX || fseek(file, 0, SEEK_SET)) {
        fclose(file); return NULL;
    }
    char *source = malloc((size_t)size + 1);
    bool ok = source && fread(source, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file)) ok = false;
    if (!ok) { free(source); return NULL; }
    source[size] = 0;
    return source;
}

static char *mutate(const char *source, const char *before, const char *after) {
    const char *at = strstr(source, before);
    if (!at) return NULL;
    size_t prefix = (size_t)(at - source), suffix = strlen(at + strlen(before));
    char *changed = malloc(prefix + strlen(after) + suffix + 1);
    if (changed) {
        memcpy(changed, source, prefix);
        memcpy(changed + prefix, after, strlen(after));
        memcpy(changed + prefix + strlen(after), at + strlen(before), suffix + 1);
    }
    return changed;
}

static bool decode(Arena *arena, const char *source, CettaGsltSupportTransformProfileV1 *p,
                   char *error, size_t size) {
    return source && cetta_gslt_support_profile_from_source_v1(arena,
        (const uint8_t *)source, strlen(source), cetta_mm2_gslt_profile_v1.compiler_sha256,
        p, error, size);
}

int main(int argc, char **argv) {
    if (argc != 2) { fputs("usage: test-gslt-support-profile-v1 SOURCE\n", stderr); return 2; }
    char *source = read_source(argv[1]);
    if (!source) return 2;
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols; g_var_intern = &variables; g_hashcons = NULL;
    Arena arena;
    arena_init(&arena);
    CettaGsltSupportTransformProfileV1 p = {0};
    char error[512] = {0};
    CHECK(decode(&arena, source, &p, error, sizeof(error)), "actual source decodes");
    if (!p.physical_profile_packet) { fprintf(stderr, "%s\n", error); return 1; }
    CHECK(cetta_gslt_support_profile_packet_matches_v1(&p, error, sizeof(error)), "new packet agrees");
    CHECK(cetta_gslt_support_profile_packet_matches_v1(&cetta_mm2_gslt_profile_v1, error, sizeof(error)),
          "retained packet agrees with retained descriptor");
    CHECK(p.physical_profile_packet_size == cetta_mm2_gslt_profile_v1.physical_profile_packet_size &&
          !memcmp(p.physical_profile_packet, cetta_mm2_gslt_profile_v1.physical_profile_packet,
                  p.physical_profile_packet_size), "source decoding equals independent retained CSTP");
    CHECK(p.source_declaration_count == 3 && p.sink_declaration_count == 6,
          "core and declared extension entries are retained");
    const char commented[] = ";前\n(root name; comment\r\n \"a;\\\"b\") ; final comment";
    CettaOpLangV1Document document;
    cetta_op_lang_v1_document_init(&document);
    CettaOpLangV1Status status;
    CHECK(cetta_op_lang_v1_parse_commented_document_bytes(&document,
              (const uint8_t *)commented, strlen(commented), UINT32_MAX, UINT32_MAX,
              &status, error, sizeof(error)), "line, inline, CRLF and EOF comments parse");
    CHECK(document.root && document.root->as.application.argument_len == 2 &&
          cetta_op_lang_v1_symbol_is(document.root->as.application.arguments[0], "name") &&
          document.root->as.application.arguments[0]->byte_left ==
              (uint32_t)(strstr(commented, "name") - commented) &&
          cetta_op_lang_v1_string_is(document.root->as.application.arguments[1],
              (const uint8_t *)"a;\"b", 4), "comments preserve original byte positions and quoted semicolons");
    cetta_op_lang_v1_document_free(&document);
    const char semicolon[] = "(root a;b\n)";
    CHECK(cetta_op_lang_v1_parse_document_bytes(&document, (const uint8_t *)semicolon,
              strlen(semicolon), UINT32_MAX, UINT32_MAX, &status, error, sizeof(error)) &&
          cetta_op_lang_v1_symbol_is(document.root->as.application.arguments[0], "a;b"),
          "canonical entry still retains unquoted semicolons");
    cetta_op_lang_v1_document_free(&document);
    CHECK(cetta_op_lang_v1_parse_commented_document_bytes(&document, (const uint8_t *)semicolon,
              strlen(semicolon), UINT32_MAX, UINT32_MAX, &status, error, sizeof(error)) &&
          cetta_op_lang_v1_symbol_is(document.root->as.application.arguments[0], "a"),
          "only commented entry interprets semicolon as trivia");
    cetta_op_lang_v1_document_free(&document);
    const char *const before[] = {
        "(source BTM 1", "(source == 2", "(source != 2", "(sink + 1", "(sink - 1",
        "(sink head 2", "(sink tail 2", "(sink count 3", "(sink pure 3",
        "(carrier support)", "(scheduler least-mork-compact-expression-key-v1)",
        "(unsupported leave-inert)", "consume-selected", "snapshot-includes-selected",
        "relational-product-may-reuse-support", "stage-all-matches-per-sink",
        "finalize-sinks-left-to-right", "(fuel exact-upper-bound)",
        "(completion no-supported-work)", "(observation support)",
        "(work-shell exec 3 0 1 2)", "(name mm2)", "(profile gslt)",
        "(input-explicit I", "(input-compat \",\" support.snapshot-match.v1)",
        "(source BTM 1 support.snapshot-match.v1)", "(source BTM 1", "(name mm2)", "(name mm2)"};
    const char *const after[] = {
        "(source BTM 0", "(source == 1", "(source != 1", "(sink + 0", "(sink - 0",
        "(sink head 1", "(sink tail 1", "(sink count 1", "(sink pure 1",
        "(carrier bag)", "(scheduler first)", "(unsupported consume)",
        "retain-selected", "snapshot-excludes-selected", "linear-support-use",
        "stream-matches", "finalize-sinks-right-to-left", "(fuel approximate)",
        "(completion no-work)", "(observation bag)", "(work-shell exec 3 0 0 2)",
        "(name mm2) (name other)", "", "(input-explicit",
        "(input-compat \",\" support.remove.v1)",
        "(source BTM 1 support.snapshot-match.v1) (source BTM 1 support.snapshot-match.v1)",
        "(source BTM 256", "(name mm2) (unrecognized data)", "(name \"prefix\\x00suffix\")"};
    for (size_t i = 0; i < sizeof(before) / sizeof(before[0]); i++) {
        char *changed = mutate(source, before[i], after[i]);
        CHECK(changed != NULL, "source mutation locates its intended field");
        CettaGsltSupportTransformProfileV1 refused = p;
        CHECK(!decode(&arena, changed, &refused, error, sizeof(error)), before[i]);
        CHECK(!refused.language_name && !refused.physical_profile_packet,
              "failed source decoding clears the result");
        free(changed);
    }
    char *changed = mutate(source, "(source BTM 1 support.snapshot-match.v1)",
                           "(source future 255 future.source.v1)");
    CettaGsltSupportTransformProfileV1 extension = {0};
    CHECK(decode(&arena, changed, &extension, error, sizeof(error)),
          "unknown provider declaration retains the u8 wire range");
    CHECK(extension.source_declarations && extension.source_declarations[0].argument_count == 255,
          "unknown provider arity survives unchanged");
    free(changed);
    changed = mutate(source, "(work-shell exec 3 0 1 2)", "(work-shell work 3 2 0 1)");
    CettaGsltSupportTransformProfileV1 renamed = {0};
    CHECK(decode(&arena, changed, &renamed, error, sizeof(error)), "renamed C vocabulary and permuted positions decode");
    CHECK(renamed.work_symbol && !strcmp(renamed.work_symbol, "work") && renamed.location_position == 2,
          "work shell data is not replaced with defaults");
    free(changed);
    changed = mutate(source, "(name mm2)", "(name \"日本語 \\\" \\\\ ?\")");
    CettaGsltSupportTransformProfileV1 unicode = {0};
    CHECK(decode(&arena, changed, &unicode, error, sizeof(error)), "Unicode and escaped text decode");
    free(changed);

    for (size_t size = 0; size < p.physical_profile_packet_size; size++) {
        CettaGsltSupportTransformProfileV1 shortened = p;
        shortened.physical_profile_packet_size = size;
        CHECK(!cetta_gslt_support_profile_packet_matches_v1(&shortened, error, sizeof(error)),
              "every packet truncation is refused");
    }
    uint8_t *packet = malloc(p.physical_profile_packet_size + 1);
    memcpy(packet, p.physical_profile_packet, p.physical_profile_packet_size);
    packet[p.physical_profile_packet_size] = 0;
    CettaGsltSupportTransformProfileV1 altered = p;
    altered.physical_profile_packet = packet;
    altered.physical_profile_packet_size++;
    CHECK(!cetta_gslt_support_profile_packet_matches_v1(&altered, error, sizeof(error)), "trailing packet byte refused");
    altered.physical_profile_packet_size--;
    for (size_t i = 0; i < p.physical_profile_packet_size; i++) {
        packet[i] ^= 0x80;
        CHECK(!cetta_gslt_support_profile_packet_matches_v1(&altered, error, sizeof(error)),
              "changed packet byte cannot preserve descriptor agreement");
        packet[i] ^= 0x80;
    }
    free(packet);
    altered = p;
    altered.language_name = "different";
    CHECK(cetta_gslt_support_transform_profile_validate_v1(&altered, error, sizeof(error)) &&
          !cetta_gslt_support_profile_packet_matches_v1(&altered, error, sizeof(error)),
          "C-only descriptor validation is distinct from packet agreement");
    char *long_name = malloc(65537);
    memset(long_name, 'a', 65536); long_name[65536] = 0;
    altered = p; altered.language_name = long_name;
    const uint8_t *bounded_packet = NULL;
    size_t bounded_size = 0;
    CHECK(!cetta_gslt_support_profile_encode_packet_v1(&arena, &altered,
              &bounded_packet, &bounded_size, error, sizeof(error)) && !bounded_packet && !bounded_size,
          "65536-byte field exceeds u16 wire length");
    long_name[65535] = 0;
    CHECK(cetta_gslt_support_profile_encode_packet_v1(&arena, &altered,
              &bounded_packet, &bounded_size, error, sizeof(error)), "65535-byte field fits u16 wire length");
    altered.physical_profile_packet = bounded_packet; altered.physical_profile_packet_size = bounded_size;
    CHECK(cetta_gslt_support_profile_packet_matches_v1(&altered, error, sizeof(error)),
          "maximum-length field retains exact packet agreement");
    free(long_name);

    CettaGsltSupportOperatorDeclV1 bad = {"BTM", 0, "support.snapshot-match.v1"};
    altered = p; altered.source_declarations = &bad; altered.source_declaration_count = 1;
    CHECK(!cetta_gslt_support_transform_profile_validate_v1(&altered, error, sizeof(error)),
          "malformed known-provider arity refused before native operand indexing");
    const uint8_t invalid_utf8[] = {0xc0, 0x80};
    CHECK(!cetta_gslt_support_profile_from_source_v1(&arena, invalid_utf8, sizeof(invalid_utf8),
              p.compiler_sha256, &altered, error, sizeof(error)), "overlong UTF-8 source refused");
    CHECK(!cetta_gslt_support_profile_from_source_v1(&arena, (const uint8_t *)source, strlen(source) + 1,
              p.compiler_sha256, &altered, error, sizeof(error)), "embedded NUL source refused");

    free(source);
    g_var_intern = NULL; var_intern_free(&variables);
    g_symbols = NULL; symbol_table_free(&symbols);
    CHECK(unicode.language_name && !strcmp(unicode.language_name, "日本語 \" \\ ?"),
          "descriptor text outlives its input buffer and symbol table");
    CHECK(cetta_gslt_support_profile_packet_matches_v1(&p, error, sizeof(error)),
          "full descriptor remains valid while owning arena lives");
    arena_free(&arena);
    printf("Native support profile: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
