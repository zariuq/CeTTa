#include "petta_compiled_reader.h"

#include "generated/petta_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "symbol.h"
#include "parser.h"
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

static bool host_begin(void *p) { return parser_host_projection_v1_begin_form(p); }
static AtomId host_word(void *p, const uint8_t *b, size_t n) { return parser_host_projection_v1_word_bytes(p, b, n); }
static AtomId host_string(void *p, const uint8_t *b, size_t n) { return parser_host_projection_v1_string_bytes(p, b, n); }
static AtomId host_variable(void *p, const uint8_t *b, size_t n, bool anonymous) {
    return anonymous ? parser_host_projection_v1_anonymous_variable(p) : parser_host_projection_v1_variable_bytes(p, b, n);
}
static AtomId host_dollar(void *p) { return host_word(p, (const uint8_t *)"$", 1u); }
static AtomId host_expression(void *p, const AtomId *items, size_t n) { return parser_host_projection_v1_expression(p, items, n); }
static bool host_finish(void *p, bool runnable, AtomId value, AtomId out[2], uint32_t *n) {
    (void)p;
    if (runnable || value == CETTA_ATOM_ID_NONE) return false;
    out[0] = value; *n = 1u; return true;
}

static void body_codec_respects_grammar(TestCounts *counts, TermUniverse *universe) {
    GSLTDirectPeTTaReaderV1Plan plan = petta_reader_direct_v1_plan;
    uint8_t ascii[256];
    memcpy(ascii, plan.string_plain->ascii, sizeof ascii);
    ascii['4'] = 0u;
    const uint32_t excluded[] = {34u, 52u, 92u};
    GSLTDirectScalarClassV1 plain = {ascii, excluded, 3u, true};
    plan.string_plain = &plain;
    char error[256];
    expect(counts, gslt_direct_petta_reader_v1_plan_validate(&plan, error, sizeof error),
           "a narrower authored string class is a valid reader plan");
    const char *inputs[] = {"\"\\x41\"", "\"\\4\""};
    for (size_t i = 0; i < 2u; ++i) {
        ParserHostProjectionV1 *host = parser_host_projection_v1_new(universe);
        GSLTDirectPeTTaProjectionV1 projection = {
            .context = host, .begin_form = host_begin, .token_bytes = host_word,
            .variable_bytes = host_variable, .string_bytes = host_string,
            .dollar_symbol = host_dollar, .expression = host_expression, .finish_form = host_finish};
        AtomId value = CETTA_ATOM_ID_NONE;
        int result = gslt_direct_petta_reader_v1_parse_term_bytes_id(&plan,
            (const uint8_t *)inputs[i], strlen(inputs[i]), &projection, &value, error, sizeof error);
        expect(counts, i == 0u ? result < 0 :
            (result == 0 && tu_string_len(universe, value) == 2u &&
             !memcmp(tu_string_cstr(universe, value), "\\4", 2u)),
            i == 0u ? "decoding does not bypass the declared lexical class" :
                      "an escaped scalar remains admitted by the escape grammar");
        parser_host_projection_v1_free(host);
    }
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

    body_codec_respects_grammar(&counts, &universe);
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
        mutation.string_body_metta = false;
        expect(&counts,
               !gslt_direct_petta_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "PeTTa plan requires an explicit string interpretation");
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

    {
        static const uint8_t source[] = "(a) (b (c d)) !(f 1) (z)";
        AtomId *whole = NULL;
        int whole_len = parse(reader, &universe, source, sizeof(source) - 1u,
                               &whole, &receipt, error, sizeof(error));
        const uint32_t allowances[] = {1u, 5u, 64u};
        bool same = whole_len == 5;
        for (size_t trial = 0u; same && trial < 3u; trial++) {
            PeTTaCompiledDocumentV1 document = {
                .fd = -1, .bytes = source, .len = sizeof(source) - 1u,
                .slice_tokens = allowances[trial],
            };
            int copied = 0;
            while (same && document.pos < document.len) {
                size_t before = document.pos;
                AtomId *slice = NULL;
                int amount = petta_compiled_document_v1_next(
                    reader, &document, UINT32_MAX, &universe, &slice,
                    error, sizeof(error));
                same = amount > 0 && document.pos > before &&
                    copied + amount <= whole_len;
                if (same && trial == 0u && copied == 0)
                    same = amount == 1;
                for (int i = 0; same && i < amount; i++)
                    same = slice[i] == whole[copied + i];
                if (amount > 0)
                    copied += amount;
                free(slice);
            }
            same = same && copied == whole_len;
        }
        expect(&counts, same,
               "node-bounded slices preserve whole forms and runnable pairs");
        free(whole);
    }

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
                   var_id_eq(tu_var_id(&universe, x1), tu_var_id(&universe, x2)),
               "named PeTTa variables co-refer within one form");
        expect(&counts,
               tu_kind(&universe, a1) == ATOM_VAR &&
                   tu_kind(&universe, a2) == ATOM_VAR &&
                   !var_id_eq(tu_var_id(&universe, a1), tu_var_id(&universe, a2)),
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
                      "a\\q") == 0,
           "PeTTa declared literal codec preserves unknown escapes");
    free(ids);
    ids = NULL;

    const struct { const char *body; const char *value; size_t len; } literal_cases[] = {
        {"\\x00", "\0", 1u}, {"\\u{3bb}", "λ", 2u},
        {"\\u03bb", "λ", 2u}, {"\\uD83D\\uDE00", "😀", 4u},
        {"\\uD800", "\\uD800", 6u}, {"\\u{zz}", "\\u{zz}", 6u},
        {"\\x4", "\\x4", 3u}, {"\\b\\f", "\b\f", 2u}
    };
    for (size_t i = 0; i < sizeof literal_cases / sizeof literal_cases[0]; ++i) {
        char source[96];
        snprintf(source, sizeof source, "(\"%s\")", literal_cases[i].body);
        len = parse(reader, &universe, (const uint8_t *)source, strlen(source),
                    &ids, &receipt, error, sizeof error);
        AtomId value = len == 1 && ids && tu_arity(&universe, ids[0]) == 1u
            ? tu_child(&universe, ids[0], 0u) : CETTA_ATOM_ID_NONE;
        expect(&counts, value != CETTA_ATOM_ID_NONE &&
            receipt.input_byte_len == strlen(source) && receipt.shift_len == 2u * strlen(source) &&
            tu_kind(&universe, value) == ATOM_GROUNDED &&
            tu_ground_kind(&universe, value) == GV_STRING &&
            tu_string_len(&universe, value) == literal_cases[i].len &&
            !memcmp(tu_string_cstr(&universe, value), literal_cases[i].value, literal_cases[i].len),
            "generated reader decodes canonical literals without losing unknown spellings");
        free(ids); ids = NULL;
    }

    /* ("a\qb\") leaves its string open: the splitter keeps an escaped
     * quote inside the string, as PeTTa's filereader does. */
    len = parse(reader, &universe, quoted_token_document,
                sizeof(quoted_token_document), &ids, &receipt,
                error, sizeof(error));
    expect(&counts, len < 0 && !ids,
           "PeTTa splitter keeps an escaped quote inside its string");
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
           len == 1 && ids && tu_kind(&universe, ids[0]) == ATOM_EXPR &&
               tu_arity(&universe, ids[0]) == 1u &&
               tu_kind(&universe, tu_child(&universe, ids[0], 0u)) ==
                   ATOM_GROUNDED &&
               tu_ground_kind(&universe,
                              tu_child(&universe, ids[0], 0u)) == GV_STRING &&
               tu_string_len(&universe,
                             tu_child(&universe, ids[0], 0u)) == 3u &&
               memcmp(tu_string_cstr(&universe,
                                    tu_child(&universe, ids[0], 0u)),
                      "a\0b", 3u) == 0 &&
               tu_child(&universe, ids[0], 0u) !=
                   tu_intern_string(&universe, "a"),
           "PeTTa strings preserve NUL and its suffix");
    free(ids);
    ids = NULL;

    {
        const size_t widths[] = {65535u, 65536u, 100000u};
        for (size_t w = 0u; w < sizeof(widths) / sizeof(*widths); w++) {
            size_t width = widths[w], bytes = width * 2u + 2u;
            uint8_t *document = malloc(bytes);
            expect(&counts, document != NULL, "wide expression allocation");
            if (!document)
                break;
            document[0] = '(';
            for (size_t i = 0u; i < width; i++) {
                document[1u + i * 2u] = 'v';
                document[2u + i * 2u] = ' ';
            }
            document[bytes - 1u] = ')';
            len = parse(reader, &universe, document, bytes, &ids, &receipt,
                        error, sizeof(error));
            expect(&counts,
                   len == 1 && ids && tu_arity(&universe, ids[0]) == width &&
                       tu_child(&universe, ids[0], 0u) ==
                           tu_child(&universe, ids[0], width - 1u),
                   "wide expressions retain every occurrence across the header summary boundary");
            free(ids);
            ids = NULL;
            document[bytes - 1u] = ']';
            len = parse(reader, &universe, document, bytes, &ids, &receipt,
                        error, sizeof(error));
            expect(&counts, len < 0 && !ids,
                   "malformed wide expression does not publish a partial form");
            free(document);
        }
    }

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
