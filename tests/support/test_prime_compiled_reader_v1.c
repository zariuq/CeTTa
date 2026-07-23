#include "generated/prime_reader_direct_v1.generated.h"
#include "match.h"
#include "parser.h"
#include "prime_compiled_reader.h"
#include "symbol.h"
#include "term_universe.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *source;
    bool accepted;
} PrimeReaderCaseV1;

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

static const PrimeReaderCaseV1 CASES[] = {
    {"empty document", "", true},
    {"comment at eof", " \t; no newline", true},
    {"quote symbol", "@doc", true},
    {"quote string", "@\"moo\"", true},
    {"quote expression", "@(agent Max Botnick)", true},
    {"quote open syntax", "@($x)", true},
    {"unquote symbol", "*@doc", true},
    {"unquote expression", "*@(agent Max Botnick)", true},
    {"unquote direct expression", "*(agent Max Botnick)", true},
    {"unquote direct string", "*\"moo\"", true},
    {"anonymous variable", "$", true},
    {"independent anonymous variables", "(P $ $)", true},
    {"ordinary variable", "$x", true},
    {"structural variable symbol", "$@foo", true},
    {"structural variable string", "$@\"moo\"", true},
    {"structural variable expression", "$@(mm-var \"ph\")", true},
    {"ordinary space", "&self", true},
    {"structural reference symbol", "&@doc", true},
    {"structural reference expression", "&@(agent Max Botnick)", true},
    {"ordinary star head", "(* a b)", true},
    {"ordinary star before space", "* (a)", true},
    {"structural lambda name",
     "(lam @(mm-var \"ph\") (wff @(mm-var \"ph\")))", true},
    {"at inside symbol", "foo@bar", true},
    {"at inside variable", "$foo@bar", true},
    {"escaped string", "\"a\\\"b\"", true},
    {"empty string", "\"\"", true},
    {"non-ASCII symbol", "λ牛", true},
    {"non-ASCII name", "@\"κόσμος\"", true},
    {"adjacent atoms", "(a(b)c)", true},
    {"canonical numbers", "42 -3 1.5 3/4", true},
    {"missing close", "(a", false},
    {"stray close", ")", false},
    {"unterminated string", "\"unterminated", false},
    {"quote without payload", "@", false},
    {"structural variable without payload", "$@", false},
    {"structural reference without payload", "&@", false},
    {"open structural variable", "$@(open $x)", false},
    {"open structural reference", "&@(open $x)", false},
    {"unquote missing quote payload", "*@", false},
};

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

static bool ids_alpha_equal(
    TermUniverse *left_universe, const AtomId *left, int left_len,
    TermUniverse *right_universe, const AtomId *right, int right_len) {
    Arena left_arena;
    Arena right_arena;
    bool equal = left_len == right_len;
    arena_init(&left_arena);
    arena_init(&right_arena);
    for (int index = 0; equal && index < left_len; index++) {
        Atom *left_atom = term_universe_copy_atom(
            left_universe, &left_arena, left[index]);
        Atom *right_atom = term_universe_copy_atom(
            right_universe, &right_arena, right[index]);
        equal = left_atom && right_atom &&
                atom_alpha_eq(left_atom, right_atom);
    }
    arena_free(&right_arena);
    arena_free(&left_arena);
    return equal;
}

int main(void) {
    TestCounts counts = {0};
    SymbolTable symbols;
    Arena compiled_persistent;
    Arena legacy_persistent;
    TermUniverse compiled_universe;
    TermUniverse legacy_universe;
    PrimeCompiledReaderV1 *reader;
    PrimeCompiledReaderV1Receipt receipt;
    char error[512] = {0};
    bool old_universal;

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    arena_init(&compiled_persistent);
    arena_set_runtime_kind(
        &compiled_persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&legacy_persistent);
    arena_set_runtime_kind(
        &legacy_persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&compiled_universe);
    term_universe_set_persistent_arena(
        &compiled_universe, &compiled_persistent);
    term_universe_init(&legacy_universe);
    term_universe_set_persistent_arena(
        &legacy_universe, &legacy_persistent);
    old_universal = parser_set_universal_name_syntax_enabled(true);

    reader = prime_compiled_reader_v1_new();
    expect(&counts,
           reader && prime_compiled_reader_v1_prepare(
                         reader, error, sizeof(error)),
           "generated Prime reader prepares");
    expect(&counts,
           !prime_compiled_reader_v1_prepare(
               reader, error, sizeof(error)),
           "prepared Prime reader rejects accidental replacement");
    {
        GSLTDirectPrefixReaderV1Plan mutation =
            prime_reader_direct_v1_plan;
        mutation.fragment = GSLT_DIRECT_READER_V1_FRAGMENT;
        expect(&counts,
               !gslt_direct_prefix_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "Prime plan rejects the HE fragment tag");
    }
    {
        GSLTDirectPrefixReaderV1Plan mutation =
            prime_reader_direct_v1_plan;
        mutation.syntax_digest = "not-a-digest";
        expect(&counts,
               !gslt_direct_prefix_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "Prime plan rejects malformed derivation identity");
    }
    {
        GSLTDirectPrefixReaderV1Plan mutation =
            prime_reader_direct_v1_plan;
        GSLTDirectPrefixRuleV1 rules[4];
        memcpy(rules, mutation.prefix_rules, sizeof(rules));
        rules[1].role = GSLT_DIRECT_PREFIX_ROLE_INVALID;
        mutation.prefix_rules = rules;
        expect(&counts,
               !gslt_direct_prefix_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "Prime plan rejects a missing prefix meaning");
    }
    {
        GSLTDirectPrefixReaderV1Plan mutation =
            prime_reader_direct_v1_plan;
        GSLTDirectTokenRuleV1 rules[6];
        memcpy(rules, mutation.token_rules, sizeof(rules));
        for (size_t index = 0u; index < 6u; index++) {
            if (rules[index].literal_len == 0u)
                rules[index].first = rules[index].tail;
        }
        mutation.token_rules = rules;
        expect(&counts,
               !gslt_direct_prefix_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "Prime plan rejects a generic token overlapping @ prefixes");
    }
    {
        GSLTDirectPrefixReaderV1Plan mutation =
            prime_reader_direct_v1_plan;
        GSLTDirectTokenRuleV1 rules[6];
        memcpy(rules, mutation.token_rules, sizeof(rules));
        for (size_t index = 0u; index < 6u; index++) {
            if (rules[index].literal_len == 1u &&
                rules[index].literal[0] == (uint32_t)'$' &&
                rules[index].first) {
                rules[index].first = rules[index].tail;
            }
        }
        mutation.token_rules = rules;
        expect(&counts,
               !gslt_direct_prefix_reader_v1_plan_validate(
                   &mutation, error, sizeof(error)),
               "Prime plan rejects a variable token overlapping $@ syntax");
    }

    for (size_t case_index = 0u;
         case_index < sizeof(CASES) / sizeof(CASES[0]); case_index++) {
        const PrimeReaderCaseV1 *test_case = &CASES[case_index];
        AtomId *compiled = NULL;
        AtomId *legacy = NULL;
        int compiled_len = prime_compiled_reader_v1_parse_text_ids(
            reader, test_case->source, &compiled_universe, &compiled,
            &receipt, error, sizeof(error));
        int legacy_len = parse_metta_text_ids(
            test_case->source, &legacy_universe, &legacy);
        char label[256];
        (void)snprintf(label, sizeof(label), "%s acceptance parity",
                       test_case->label);
        expect(&counts,
               (compiled_len >= 0) == test_case->accepted &&
                   (legacy_len >= 0) == test_case->accepted,
               label);
        (void)snprintf(label, sizeof(label), "%s canonical Atom parity",
                       test_case->label);
        expect(&counts,
               compiled_len < 0 ||
                   ids_alpha_equal(
                       &compiled_universe, compiled, compiled_len,
                       &legacy_universe, legacy, legacy_len),
               label);
        free(legacy);
        free(compiled);
    }

    {
        AtomId *ids = NULL;
        int len = prime_compiled_reader_v1_parse_text_ids(
            reader, "(P $ $)", &compiled_universe, &ids,
            &receipt, error, sizeof(error));
        AtomId form = len == 1 && ids ? ids[0] : CETTA_ATOM_ID_NONE;
        AtomId left = form == CETTA_ATOM_ID_NONE
                          ? CETTA_ATOM_ID_NONE
                          : tu_child(&compiled_universe, form, 1u);
        AtomId right = form == CETTA_ATOM_ID_NONE
                           ? CETTA_ATOM_ID_NONE
                           : tu_child(&compiled_universe, form, 2u);
        expect(&counts,
               len == 1 && tu_arity(&compiled_universe, form) == 3u &&
                   tu_kind(&compiled_universe, left) == ATOM_VAR &&
                   tu_kind(&compiled_universe, right) == ATOM_VAR &&
                   tu_var_id(&compiled_universe, left) !=
                       tu_var_id(&compiled_universe, right),
               "Prime bare dollars project to independent fresh variables");
        free(ids);
    }

    {
        AtomId *ids = NULL;
        int len = prime_compiled_reader_v1_parse_text_ids(
            reader, "@(goal x) $@(hole 0) &@(space main) *foo",
            &compiled_universe, &ids, &receipt, error, sizeof(error));
        expect(&counts,
               len == 4 && ids && receipt.source_pass_count == 1u &&
                   receipt.prefix_len == 4u && receipt.token_len > 4u &&
                   receipt.shift_len > 0u && receipt.reduce_len > 0u,
               "Prime receipt accounts for source and prefix occurrences");
        expect(&counts,
               strcmp(receipt.fragment,
                      GSLT_DIRECT_PREFIX_READER_V1_FRAGMENT) == 0 &&
                   strcmp(receipt.profile, "cetta-prime-v1") == 0 &&
                   is_sha256_text(receipt.program_digest) &&
                   is_sha256_text(receipt.syntax_digest) &&
                   is_sha256_text(receipt.scalar_class_digest) &&
                   is_sha256_text(receipt.projection_digest) &&
                   is_sha256_text(receipt.compiler_digest),
               "Prime receipt retains composed derivation identity");
        free(ids);
    }
    {
        static const uint8_t malformed[] = {0xff};
        AtomId *ids = NULL;
        int len = prime_compiled_reader_v1_parse_bytes_ids(
            reader, malformed, sizeof(malformed), &compiled_universe,
            &ids, &receipt, error, sizeof(error));
        expect(&counts, len < 0 && !ids,
               "Prime scalar boundary rejects malformed UTF-8");
    }

    prime_compiled_reader_v1_free(reader);
    parser_set_universal_name_syntax_enabled(old_universal);
    term_universe_free(&legacy_universe);
    term_universe_free(&compiled_universe);
    arena_free(&legacy_persistent);
    arena_free(&compiled_persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(PrimeCompiledReaderV1Summary %u %u %u cases %zu)\n",
           counts.passed + counts.failed, counts.passed, counts.failed,
           sizeof(CASES) / sizeof(CASES[0]));
    return counts.failed == 0u ? 0 : 1;
}
