#include "petta_compiled_reader.h"

#include "generated/petta_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "symbol.h"
#include "term_universe.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

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

static bool symbol_id_is(
    const TermUniverse *universe, AtomId id, const char *text) {
    return id != CETTA_ATOM_ID_NONE &&
           tu_kind(universe, id) == ATOM_SYMBOL &&
           symbol_eq_cstr(g_symbols, tu_sym(universe, id), text);
}

static int parse(
    PeTTaCompiledReaderV1 *reader, TermUniverse *universe,
    const uint8_t *bytes, size_t len, AtomId **ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error,
    size_t error_size) {
    return petta_compiled_reader_v1_parse_bytes_ids(
        reader, bytes, len, universe, ids, receipt, error, error_size);
}

int main(void) {
    static const uint8_t basic_document[] =
        "; c\n(= (f $x) $x)\n!(f 1)\n";
    static const uint8_t variable_document[] = "(a $x $x $_ $_)";
    static const uint8_t number_document[] =
        "(1 -2 +3 1.2 .5 1e2 1_2_3 0x1a 1_000 1r2 1.0Inf True False)";
    static const uint8_t string_document[] = "(\"a\\n\\t\\r\" \"a\\q\")";
    static const uint8_t quoted_token_document[] = {
        '(', '"', 'a', '\\', 'q', 'b', '\\', '"', ')'};
    static const uint8_t nul_token_document[] = {
        '(', 'a', 0, 'b', ')', '\n'};
    static const uint8_t nul_string_document[] = {
        '(', '"', 'a', 0, 'b', '"', ')', '\n'};
    static const uint8_t unicode_document[] = {
        0xe1, 0x9a, 0x80, '(', 'a', ')', '\n'};
    static const uint8_t malformed_utf8[] = {'(', 0xff, ')'};
    static const uint8_t comment_eof[] = {'(', 'a', ')', ';', 'x'};
    static const uint8_t bare_token[] = {'a'};
    static const uint8_t unbalanced[] = {'(', 'a'};
    TestCounts counts = {0};
    SymbolTable symbols;
    Arena persistent;
    TermUniverse universe;
    PeTTaCompiledReaderV1 *reader;
    PeTTaCompiledReaderV1Receipt receipt;
    AtomId *ids = NULL;
    char error[512] = {0};
    int len;

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    arena_init(&persistent);
    arena_set_runtime_kind(&persistent,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    reader = petta_compiled_reader_v1_new();
    expect(&counts,
           reader && petta_compiled_reader_v1_prepare(
                         reader, error, sizeof(error)),
           "generated PeTTa reader prepares");
    expect(&counts,
           !petta_compiled_reader_v1_prepare(
               reader, error, sizeof(error)),
           "prepared PeTTa reader rejects accidental replacement");
    {
        GSLTDirectPeTTaReaderV1Plan mutation =
            petta_reader_direct_v1_plan;
        mutation.fragment = GSLT_DIRECT_READER_V1_FRAGMENT;
        expect(&counts,
               !gslt_direct_petta_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "PeTTa plan rejects a one-stage fragment tag");
    }
    {
        GSLTDirectPeTTaReaderV1Plan mutation =
            petta_reader_direct_v1_plan;
        mutation.form_syntax_digest = "not-a-digest";
        expect(&counts,
               !gslt_direct_petta_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "PeTTa plan rejects malformed derivation identity");
    }
    {
        GSLTDirectPeTTaReaderV1Plan mutation =
            petta_reader_direct_v1_plan;
        GSLTDirectScalarClassV1 token_first = *mutation.token_first;
        uint8_t ascii[256];
        memcpy(ascii, token_first.ascii, sizeof(ascii));
        ascii[(uint8_t)'"'] = 1u;
        token_first.ascii = ascii;
        mutation.token_first = &token_first;
        expect(&counts,
               !gslt_direct_petta_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "PeTTa plan rejects stale class caches and dispatch overlap");
    }
    {
        GSLTDirectPeTTaReaderV1Plan mutation =
            petta_reader_direct_v1_plan;
        mutation.string_escape_identity_fallback = false;
        expect(&counts,
               !gslt_direct_petta_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "PeTTa plan rejects loss of identity escape semantics");
    }

    len = parse(reader, &universe, basic_document,
                sizeof(basic_document) - 1u, &ids, &receipt,
                error, sizeof(error));
    if (len < 0)
        fprintf(stderr, "basic PeTTa parse: %s\n", error);
    expect(&counts, len == 3 && ids,
           "compiled PeTTa document emits form plus runnable prefix/value");
    expect(&counts,
           len == 3 && symbol_id_is(&universe, ids[1], "!") &&
               tu_kind(&universe, ids[0]) == ATOM_EXPR &&
               tu_kind(&universe, ids[2]) == ATOM_EXPR,
           "PeTTa-owned document projection preserves runnable encoding");
    expect(&counts,
           receipt.source_pass_count == 1u &&
               receipt.form_replay_count == 2u &&
               receipt.form_len == 2u && receipt.input_byte_len ==
                   sizeof(basic_document) - 1u &&
               receipt.token_len > 0u && receipt.shift_len > 0u &&
               receipt.reduce_len > 0u,
           "PeTTa receipt distinguishes source pass from form replay");
    expect(&counts,
           strcmp(receipt.fragment,
                  GSLT_DIRECT_PETTA_READER_V1_FRAGMENT) == 0 &&
               strcmp(receipt.profile, "cetta-petta-v1") == 0 &&
               is_sha256_text(receipt.program_digest) &&
               is_sha256_text(receipt.splitter_syntax_digest) &&
               is_sha256_text(receipt.splitter_class_digest) &&
               is_sha256_text(receipt.form_syntax_digest) &&
               is_sha256_text(receipt.form_class_digest) &&
               is_sha256_text(receipt.projection_digest) &&
               is_sha256_text(receipt.compiler_digest),
           "PeTTa receipt retains the full composed derivation identity");
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, variable_document,
                sizeof(variable_document) - 1u, &ids, &receipt,
                error, sizeof(error));
    expect(&counts,
           len == 1 && ids && tu_kind(&universe, ids[0]) == ATOM_EXPR &&
               tu_arity(&universe, ids[0]) == 5u,
           "PeTTa variable form reaches canonical AtomIds");
    if (len == 1 && ids && tu_kind(&universe, ids[0]) == ATOM_EXPR &&
        tu_arity(&universe, ids[0]) == 5u) {
        AtomId x1 = tu_child(&universe, ids[0], 1u);
        AtomId x2 = tu_child(&universe, ids[0], 2u);
        AtomId a1 = tu_child(&universe, ids[0], 3u);
        AtomId a2 = tu_child(&universe, ids[0], 4u);
        expect(&counts,
               tu_kind(&universe, x1) == ATOM_VAR &&
                   tu_kind(&universe, x2) == ATOM_VAR &&
                   tu_var_id(&universe, x1) == tu_var_id(&universe, x2),
               "named PeTTa variables co-refer within one form");
        expect(&counts,
               tu_kind(&universe, a1) == ATOM_VAR &&
                   tu_kind(&universe, a2) == ATOM_VAR &&
                   tu_var_id(&universe, a1) != tu_var_id(&universe, a2),
               "PeTTa underscore variables are independently fresh");
    } else {
        expect(&counts, false,
               "named PeTTa variables co-refer within one form");
        expect(&counts, false,
               "PeTTa underscore variables are independently fresh");
    }
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, number_document,
                sizeof(number_document) - 1u, &ids, &receipt,
                error, sizeof(error));
    expect(&counts,
           len == 1 && ids && tu_kind(&universe, ids[0]) == ATOM_EXPR &&
               tu_arity(&universe, ids[0]) == 13u,
           "PeTTa numeric/bool projection accepts the authority matrix");
    if (len == 1 && ids && tu_arity(&universe, ids[0]) == 13u) {
        AtomId dot_five = tu_child(&universe, ids[0], 4u);
        AtomId exponent = tu_child(&universe, ids[0], 5u);
        AtomId underscores = tu_child(&universe, ids[0], 6u);
        AtomId hexadecimal = tu_child(&universe, ids[0], 7u);
        AtomId grouped = tu_child(&universe, ids[0], 8u);
        AtomId rational = tu_child(&universe, ids[0], 9u);
        AtomId infinity = tu_child(&universe, ids[0], 10u);
        expect(&counts,
               symbol_id_is(&universe, dot_five, ".5") &&
                   tu_kind(&universe, exponent) == ATOM_GROUNDED &&
                   tu_ground_kind(&universe, exponent) == GV_FLOAT &&
                   fabs(tu_float(&universe, exponent) - 100.0) < 1e-12 &&
                   symbol_id_is(&universe, underscores, "1_2_3") &&
                   symbol_id_is(&universe, hexadecimal, "0x1a") &&
                   symbol_id_is(&universe, grouped, "1_000") &&
                   symbol_id_is(&universe, rational, "1r2") &&
                   symbol_id_is(&universe, infinity, "1.0Inf"),
               "PeTTa source numbers follow SWI number//1 rather than atom_number/2");
    } else {
        expect(&counts, false,
               "PeTTa source numbers follow SWI number//1 rather than atom_number/2");
    }
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, string_document,
                sizeof(string_document) - 1u, &ids, &receipt,
                error, sizeof(error));
    expect(&counts,
           len == 1 && ids && tu_arity(&universe, ids[0]) == 2u &&
               strcmp(tu_string_cstr(
                          &universe, tu_child(&universe, ids[0], 0u)),
                      "a\n\t\r") == 0 &&
               strcmp(tu_string_cstr(
                          &universe, tu_child(&universe, ids[0], 1u)),
                      "aq") == 0,
           "PeTTa named and identity escapes follow the projection GSLT");
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, quoted_token_document,
                sizeof(quoted_token_document), &ids, &receipt,
                error, sizeof(error));
    expect(&counts,
           len == 1 && ids && tu_arity(&universe, ids[0]) == 1u &&
               strcmp(tu_string_cstr(
                          &universe, tu_child(&universe, ids[0], 0u)),
                      "a\\qb\\") == 0,
           "PeTTa quoted-token fallback retains internal backslashes");
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, nul_token_document,
                sizeof(nul_token_document), &ids, &receipt,
                error, sizeof(error));
    if (len < 0)
        fprintf(stderr, "NUL token parse: %s\n", error);
    if (len == 1 && ids && tu_arity(&universe, ids[0]) == 1u) {
        AtomId token = tu_child(&universe, ids[0], 0u);
        SymbolId spelling = tu_sym(&universe, token);
        expect(&counts,
               tu_kind(&universe, token) == ATOM_SYMBOL &&
                   symbol_len(g_symbols, spelling) == 3u &&
                   memcmp(symbol_bytes(g_symbols, spelling), "a\0b", 3u) == 0,
               "PeTTa NUL-containing symbols preserve exact bytes");
    } else {
        expect(&counts, false,
               "PeTTa NUL-containing symbols preserve exact bytes");
    }
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, unicode_document,
                sizeof(unicode_document), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len == 1 && ids,
           "PeTTa Unicode blank class is compiled into the native reader");
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, comment_eof,
                sizeof(comment_eof), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len == 1 && ids,
           "PeTTa document comments may terminate at eof");
    free(ids);
    ids = NULL;

    len = parse(reader, &universe, bare_token,
                sizeof(bare_token), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len < 0 && !ids,
           "PeTTa document rejects bare top-level tokens");
    len = parse(reader, &universe, unbalanced,
                sizeof(unbalanced), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len < 0 && !ids,
           "PeTTa document rejects unbalanced expressions");
    len = parse(reader, &universe, malformed_utf8,
                sizeof(malformed_utf8), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len < 0 && !ids,
           "PeTTa scalar layer rejects malformed UTF-8");
    len = parse(reader, &universe, nul_string_document,
                sizeof(nul_string_document), &ids, &receipt,
                error, sizeof(error));
    expect(&counts,
           len < 0 && !ids && strstr(error, "embedded NUL"),
           "PeTTa WIP contract explicitly rejects unrepresentable NUL strings");

    petta_compiled_reader_v1_free(reader);
    term_universe_free(&universe);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(PeTTaCompiledReaderV1Summary %u %u %u source-pass 1 "
           "two-stage 1)\n",
           counts.passed + counts.failed, counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
