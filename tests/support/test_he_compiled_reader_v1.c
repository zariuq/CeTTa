#include "he_compiled_reader.h"

#include "atom.h"
#include "generated/he_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

typedef struct {
    HECompiledReaderV1 *reader;
    unsigned text_calls;
    unsigned file_calls;
    HECompiledReaderV1Receipt last_receipt;
} RoutedReader;

static bool expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
    return false;
}

static bool is_sha256_text(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        unsigned char byte = (unsigned char)text[index];
        if (!isdigit(byte) && !(byte >= 'a' && byte <= 'f'))
            return false;
    }
    return true;
}

static bool expect_plan_rejected(
    TestCounts *counts, const GSLTDirectReaderV1Plan *plan,
    const char *label) {
    char error[256] = {0};
    return expect(counts,
                  !gslt_direct_reader_v1_plan_validate(
                      plan, error, sizeof(error)) &&
                      error[0] != '\0',
                  label);
}

static int routed_parse_text(void *context, const char *text,
                             TermUniverse *universe, AtomId **out_ids) {
    RoutedReader *route = context;
    char error[512] = {0};
    route->text_calls++;
    return he_compiled_reader_v1_parse_text_ids(
        route->reader, text, universe, out_ids, &route->last_receipt,
        error, sizeof(error));
}

static int routed_parse_file(void *context, const char *filename,
                             TermUniverse *universe, AtomId **out_ids) {
    RoutedReader *route = context;
    char error[512] = {0};
    route->file_calls++;
    return he_compiled_reader_v1_parse_file_ids(
        route->reader, filename, universe, out_ids, &route->last_receipt,
        error, sizeof(error));
}

int main(void) {
    static const char document[] =
        "#foo ($x $x) \"A\\n\\u{03bb}\" 2/4";
    static const uint8_t invalid_utf8[] = {UINT8_C(0xff)};
    static const uint8_t projected_string[] = {
        UINT8_C('A'), UINT8_C('\n'), UINT8_C(0xce), UINT8_C(0xbb)};
    static const uint8_t embedded_nul[] = {
        UINT8_C('A'), UINT8_C(0), UINT8_C('B')};
    TestCounts counts = {0};
    SymbolTable symbols;
    TermUniverse universe;
    Arena persistent;
    HECompiledReaderV1 *reader = NULL;
    ParserHostProjectionV1 *host_projection = NULL;
    HECompiledReaderV1Receipt receipt;
    RoutedReader route = {0};
    ParserDocumentIdsBackend backend;
    AtomId *ids = NULL;
    char error[512] = {0};
    int atom_len;
    bool old_rationals;
    AtomId host_word = CETTA_ATOM_ID_NONE;
    AtomId host_var_left = CETTA_ATOM_ID_NONE;
    AtomId host_var_right = CETTA_ATOM_ID_NONE;
    AtomId host_expr = CETTA_ATOM_ID_NONE;
    AtomId host_string = CETTA_ATOM_ID_NONE;
    AtomId host_next_var = CETTA_ATOM_ID_NONE;
    bool host_next_form = false;

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    arena_init(&persistent);
    arena_set_runtime_kind(&persistent,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    reader = he_compiled_reader_v1_new();
    expect(&counts,
           reader && he_compiled_reader_v1_prepare(
                         reader, error, sizeof(error)),
           "generated HE reader prepares");
    expect(&counts,
           !he_compiled_reader_v1_prepare(reader, error, sizeof(error)),
           "prepared reader rejects accidental replacement");
    {
        GSLTDirectAtomProjectionV1 incomplete_projection = {
            .context = reader,
        };
        atom_len = he_reader_direct_v1_parse_bytes_ids(
            (const uint8_t *)"#foo", 4u, &incomplete_projection,
            &ids, NULL, error, sizeof(error));
        expect(&counts, atom_len < 0 && !ids,
               "shared runtime rejects an incomplete language projection");
    }

    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        mutation.syntax_digest = "not-a-sha256-digest";
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects malformed derivation digests");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        mutation.fragment = "general-gll-v1";
        expect_plan_rejected(
            &counts, &mutation,
            "direct runtime rejects a mismatched fragment classifier");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        mutation.word_boundary.literals = NULL;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects absent boundary storage");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        GSLTDirectScalarClassV1 word_start = *mutation.word_start;
        uint8_t ascii[256];
        memcpy(ascii, word_start.ascii, sizeof(ascii));
        ascii[(uint8_t)'#'] = ascii[(uint8_t)'#'] ? 0u : 1u;
        word_start.ascii = ascii;
        mutation.word_start = &word_start;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects stale scalar-class caches");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        GSLTDirectCodepointMapV1 duplicate_map[2] = {
            mutation.simple_map[0], mutation.simple_map[0]};
        mutation.simple_map = duplicate_map;
        mutation.simple_map_len = 2u;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects duplicate escape projections");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        uint8_t *accepting = malloc(mutation.unicode_state_len);
        if (!accepting) {
            fprintf(stderr, "FAIL: allocate Unicode acceptance mutation\n");
            return 1;
        }
        memcpy(accepting, mutation.unicode_accepting,
               mutation.unicode_state_len);
        accepting[0] = 2u;
        mutation.unicode_accepting = accepting;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects non-Boolean DFA acceptance markers");
        free(accepting);
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        uint8_t *accepting = calloc(mutation.unicode_state_len, 1u);
        if (!accepting) {
            fprintf(stderr, "FAIL: allocate empty Unicode language mutation\n");
            return 1;
        }
        mutation.unicode_accepting = accepting;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects an empty Unicode escape language");
        free(accepting);
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        size_t transition_len =
            (size_t)mutation.unicode_state_len * 256u;
        int16_t *transitions =
            malloc(transition_len * sizeof(*transitions));
        if (!transitions) {
            fprintf(stderr, "FAIL: allocate Unicode transition mutation\n");
            return 1;
        }
        memcpy(transitions, mutation.unicode_transitions,
               transition_len * sizeof(*transitions));
        transitions[0] = (int16_t)mutation.unicode_state_len;
        mutation.unicode_transitions = transitions;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects out-of-range DFA transitions");
        free(transitions);
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        const uint32_t surrogate_prefix[] = {UINT32_C(0xd800)};
        mutation.simple_prefix = surrogate_prefix;
        mutation.simple_prefix_len = 1u;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects non-scalar escape prefixes");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        GSLTDirectCodepointMapV1 colliding_map = {
            mutation.byte_prefix[mutation.simple_prefix_len],
            (uint32_t)'x'};
        mutation.simple_map = &colliding_map;
        mutation.simple_map_len = 1u;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects fallback escape ambiguity");
    }
    {
        GSLTDirectReaderV1Plan mutation = he_reader_direct_v1_plan;
        const uint32_t overlapping_boundaries[] = {
            (uint32_t)'!', (uint32_t)')', (uint32_t)';'};
        mutation.word_boundary.literals = overlapping_boundaries;
        expect_plan_rejected(
            &counts, &mutation,
            "plan validation rejects consumed boundary characters");
    }

    host_projection = parser_host_projection_v1_new(&universe);
    expect(&counts, host_projection != NULL,
           "compiled readers can bind one host-projection context");
    expect(&counts,
           parser_host_projection_v1_begin_form(host_projection),
           "host projection starts an explicit top-level variable scope");
    host_word = parser_host_projection_v1_word_bytes(
        host_projection, (const uint8_t *)"42", 2u);
    expect(&counts,
           host_word != CETTA_ATOM_ID_NONE &&
               tu_kind(&universe, host_word) == ATOM_GROUNDED &&
               tu_ground_kind(&universe, host_word) == GV_INT &&
               tu_int(&universe, host_word) == 42,
           "direct word projection reuses CeTTa token grounding");
    host_var_left = parser_host_projection_v1_variable_bytes(
        host_projection, (const uint8_t *)"x", 1u);
    host_var_right = parser_host_projection_v1_variable_bytes(
        host_projection, (const uint8_t *)"x", 1u);
    expect(&counts,
           host_var_left != CETTA_ATOM_ID_NONE &&
               host_var_right != CETTA_ATOM_ID_NONE &&
               tu_var_id(&universe, host_var_left) ==
                   tu_var_id(&universe, host_var_right),
           "direct variable projection shares identity within one form");
    {
        AtomId children[2] = {host_var_left, host_var_right};
        host_expr = parser_host_projection_v1_expression(
            host_projection, children, 2u);
    }
    expect(&counts,
           host_expr != CETTA_ATOM_ID_NONE &&
               tu_kind(&universe, host_expr) == ATOM_EXPR &&
               tu_arity(&universe, host_expr) == 2u,
           "direct expression projection constructs canonical AtomIds");
    host_string = parser_host_projection_v1_string_bytes(
        host_projection, projected_string, sizeof(projected_string));
    expect(&counts,
           host_string != CETTA_ATOM_ID_NONE &&
               tu_kind(&universe, host_string) == ATOM_GROUNDED &&
               tu_ground_kind(&universe, host_string) == GV_STRING &&
               strcmp(tu_string_cstr(&universe, host_string),
                      "A\n\xce\xbb") == 0,
           "direct string projection is byte-length aware");
    expect(&counts,
           parser_host_projection_v1_string_bytes(
               host_projection, embedded_nul, sizeof(embedded_nul)) ==
               CETTA_ATOM_ID_NONE,
           "direct string projection rejects an unrepresentable NUL");
    expect(&counts,
           parser_host_projection_v1_variable_bytes(
               host_projection, NULL, 0u) == CETTA_ATOM_ID_NONE,
           "direct variable projection rejects an empty spelling");
    host_next_form = parser_host_projection_v1_begin_form(host_projection);
    if (host_next_form) {
        host_next_var = parser_host_projection_v1_variable_bytes(
            host_projection, (const uint8_t *)"x", 1u);
    }
    expect(&counts,
           host_next_form && host_next_var != CETTA_ATOM_ID_NONE &&
               host_var_left != CETTA_ATOM_ID_NONE &&
               tu_var_id(&universe, host_next_var) !=
                   tu_var_id(&universe, host_var_left),
           "top-level form boundaries refresh variable identity");

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, document, &universe, &ids, &receipt,
        error, sizeof(error));
    if (atom_len < 0)
        fprintf(stderr, "positive parse error: %s\n", error);
    expect(&counts, atom_len == 4 && ids,
           "compiled LanguageDef parses the positive document");
    expect(&counts,
           receipt.source_pass_count == 1u &&
               receipt.input_byte_len == strlen(document) &&
               receipt.token_len > 0u && receipt.shift_len > 0u &&
               receipt.reduce_len > 0u &&
               is_sha256_text(receipt.program_digest),
           "compiled reader emits a one-pass program receipt");
    expect(&counts,
           strcmp(receipt.profile, "cetta-he-v1") == 0,
           "host projection profile remains distinct and explicit");
    expect(&counts,
           strcmp(receipt.fragment, GSLT_DIRECT_READER_V1_FRAGMENT) == 0,
           "receipt records the selected parser fragment");
    expect(&counts,
           is_sha256_text(receipt.syntax_digest) &&
               is_sha256_text(receipt.scalar_class_digest) &&
               is_sha256_text(receipt.projection_digest) &&
               is_sha256_text(receipt.compiler_digest),
           "compiled reader receipt retains its derivation digests");
    if (atom_len == 4 && ids) {
        AtomId expression = ids[1];
        AtomId left = tu_kind(&universe, expression) == ATOM_EXPR &&
                              tu_arity(&universe, expression) == 2u
                          ? tu_child(&universe, expression, 0u)
                          : CETTA_ATOM_ID_NONE;
        AtomId right = tu_kind(&universe, expression) == ATOM_EXPR &&
                               tu_arity(&universe, expression) == 2u
                           ? tu_child(&universe, expression, 1u)
                           : CETTA_ATOM_ID_NONE;
        expect(&counts,
               tu_kind(&universe, ids[0]) == ATOM_SYMBOL &&
                   symbol_eq_cstr(g_symbols, tu_sym(&universe, ids[0]),
                                  "#foo"),
               "hash-leading word comes from the ratified GSLT");
        expect(&counts,
               left != CETTA_ATOM_ID_NONE &&
                   right != CETTA_ATOM_ID_NONE &&
                   tu_kind(&universe, left) == ATOM_VAR &&
                   tu_kind(&universe, right) == ATOM_VAR &&
                   tu_var_id(&universe, left) == tu_var_id(&universe, right),
               "variable identity is shared within one top-level form");
        expect(&counts,
               tu_kind(&universe, ids[2]) == ATOM_GROUNDED &&
                   tu_ground_kind(&universe, ids[2]) == GV_STRING &&
                   strcmp(tu_string_cstr(&universe, ids[2]),
                          "A\n\xce\xbb") == 0,
               "simple and Unicode escapes reach the host string projection");
        expect(&counts,
               tu_kind(&universe, ids[3]) == ATOM_GROUNDED &&
                   tu_ground_kind(&universe, ids[3]) == GV_RATIONAL &&
                   strcmp(tu_rational_cstr(&universe, ids[3]), "1/2") == 0,
               "formal HE host projection grounds rational literals");
    } else {
        expect(&counts, false, "hash-leading word comes from the ratified GSLT");
        expect(&counts, false,
               "variable identity is shared within one top-level form");
        expect(&counts, false,
               "simple and Unicode escapes reach the host string projection");
        expect(&counts, false,
               "formal HE host projection grounds rational literals");
    }
    free(ids);
    ids = NULL;

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "{\"query\":\"Write_Hello_World_in_C#\"}",
        &universe, &ids, &receipt, error, sizeof(error));
    expect(&counts,
           atom_len == 1 && ids &&
               tu_kind(&universe, ids[0]) == ATOM_SYMBOL &&
               symbol_eq_cstr(
                   g_symbols, tu_sym(&universe, ids[0]),
                   "{\"query\":\"Write_Hello_World_in_C#\"}"),
           "quotes and hash remain valid after a word has started");
    free(ids);
    ids = NULL;

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "\"\\x1b\"", &universe, &ids, &receipt,
        error, sizeof(error));
    expect(&counts,
           atom_len == 1 && ids &&
               tu_kind(&universe, ids[0]) == ATOM_GROUNDED &&
               tu_ground_kind(&universe, ids[0]) == GV_STRING &&
               strcmp(tu_string_cstr(&universe, ids[0]), "\x1b") == 0,
           "byte escapes follow the ratified HE string projection");
    free(ids);
    ids = NULL;

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "\"\\q\"", &universe, &ids, &receipt,
        error, sizeof(error));
    expect(&counts, atom_len < 0 && !ids,
           "unknown string escapes fail closed");

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "\"\\u{d800}\"", &universe, &ids, &receipt,
        error, sizeof(error));
    expect(&counts, atom_len < 0 && !ids,
           "Unicode surrogate escapes fail closed");

    old_rationals = parser_set_rational_literals_enabled(false);
    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "2/4", &universe, &ids, &receipt,
        error, sizeof(error));
    if (atom_len < 0)
        fprintf(stderr, "he-compat projection error: %s\n", error);
    expect(&counts,
           atom_len == 1 && ids &&
               tu_kind(&universe, ids[0]) == ATOM_SYMBOL &&
               symbol_eq_cstr(g_symbols, tu_sym(&universe, ids[0]), "2/4"),
           "he-compat host policy leaves rational spelling symbolic");
    parser_set_rational_literals_enabled(old_rationals);
    free(ids);
    ids = NULL;

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "$", &universe, &ids, &receipt,
        error, sizeof(error));
    expect(&counts,
           atom_len < 0 && !ids && strstr(error, "LanguageDef"),
           "ratified GSLT rejects a bare variable marker");

    atom_len = he_compiled_reader_v1_parse_bytes_ids(
        reader, invalid_utf8, sizeof(invalid_utf8), &universe, &ids,
        &receipt, error, sizeof(error));
    expect(&counts, atom_len < 0 && !ids,
           "compiled scalar boundary rejects malformed UTF-8");

    atom_len = he_compiled_reader_v1_parse_text_ids(
        reader, "\"\\x00\"", &universe, &ids, &receipt,
        error, sizeof(error));
    expect(&counts,
           atom_len < 0 && !ids && strstr(error, "embedded NUL"),
           "host projection rejects an unrepresentable embedded NUL");

    route.reader = reader;
    backend = (ParserDocumentIdsBackend){
        .context = &route,
        .parse_text_ids = routed_parse_text,
        .parse_file_ids = routed_parse_file,
    };
    expect(&counts, parser_set_document_ids_backend(&backend),
           "compiled reader installs at the document-reader boundary");
    expect(&counts, !parser_set_document_ids_backend(&backend),
           "document-reader boundary has one active authority");

    atom_len = parse_metta_text_ids("#foo", &universe, &ids);
    expect(&counts,
           atom_len == 1 && ids && route.text_calls == 1u &&
               route.last_receipt.source_pass_count == 1u,
           "ordinary text API routes through the compiled reader");
    free(ids);
    ids = NULL;

    atom_len = parse_metta_file_ids(
        "tests/he_a1_symbols.metta", &universe, &ids);
    expect(&counts,
           atom_len > 0 && ids && route.file_calls == 1u &&
               route.last_receipt.source_pass_count == 1u,
           "ordinary file API routes through the compiled reader");
    free(ids);
    ids = NULL;

    parser_clear_document_ids_backend(&route);
    parser_host_projection_v1_free(host_projection);
    he_compiled_reader_v1_free(reader);
    term_universe_free(&universe);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("(HECompiledReaderV1Summary %u %u %u routed-text %u "
           "routed-file %u)\n",
           counts.passed + counts.failed, counts.passed, counts.failed,
           route.text_calls, route.file_calls);
    return counts.failed == 0u ? 0 : 1;
}
