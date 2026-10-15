#include "parser.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { WORD_SYMBOL, WORD_BOOL, WORD_INT, WORD_FLOAT,
               WORD_BIGINT, WORD_RATIONAL } WordKind;

typedef struct {
    const char *source;
    WordKind kind;
    const char *text;
    int64_t integer;
    double floating;
} WordCase;

#define SYMBOL(source, spelling) {source, WORD_SYMBOL, spelling, 0, 0}
#define INTEGER(source, value) {source, WORD_INT, NULL, value, 0}
#define FLOAT(source, value) {source, WORD_FLOAT, NULL, 0, value}
#define BOOLEAN(source, value) {source, WORD_BOOL, NULL, value, 0}
#define BIGINT(source, canonical) {source, WORD_BIGINT, canonical, 0, 0}
#define RATIONAL(source, canonical) {source, WORD_RATIONAL, canonical, 0, 0}

/* Expected kinds pin the existing reader, not another parser's number rules. */
static const WordCase cases[] = {
    BOOLEAN("True", true), BOOLEAN("False", false),
    SYMBOL("true", "true"), SYMBOL("false", "false"),
    SYMBOL("Trueish", "Trueish"), SYMBOL("Falsehood", "Falsehood"),
    FLOAT("PI", 3.14159265358979323846),
    FLOAT("EXP", 2.71828182845904523536),
    SYMBOL("PIx", "PIx"), SYMBOL("EXPx", "EXPx"),
    SYMBOL("pi", "pi"), SYMBOL("exp", "exp"),
    INTEGER("0", 0), INTEGER("-0", 0), INTEGER("+0", 0),
    INTEGER("42", 42), INTEGER("-42", -42), INTEGER("+42", 42),
    INTEGER("00042", 42),
    INTEGER("9223372036854775807", INT64_MAX),
    INTEGER("-9223372036854775808", INT64_MIN),
    BIGINT("9223372036854775808", "9223372036854775808"),
    BIGINT("-9223372036854775809", "-9223372036854775809"),
    BIGINT("+0009223372036854775808", "9223372036854775808"),
    SYMBOL("9223372036854775808x", "9223372036854775808x"),
    RATIONAL("2/4", "1/2"), RATIONAL("-2/4", "-1/2"),
    INTEGER("4/2", 2), INTEGER("0/3", 0),
    SYMBOL("1/0", "1/0"), SYMBOL("1//2", "1//2"),
    SYMBOL("1/2x", "1/2x"), SYMBOL("1.0/2", "1.0/2"),
    FLOAT("0.0", 0.0), FLOAT("-0.0", -0.0),
    FLOAT(".5", 0.5), FLOAT("+.5", 0.5), FLOAT("5.", 5.0),
    FLOAT("1.25e2", 125.0), FLOAT("1.e400", INFINITY),
    FLOAT("-1.e400", -INFINITY), FLOAT("1.e-400", 0.0),
    FLOAT("0x1.0p2", 4.0),
    SYMBOL("1e0", "1e0"), SYMBOL("1e400", "1e400"),
    SYMBOL("1e-400", "1e-400"), SYMBOL("0x10", "0x10"),
    SYMBOL("NaN", "NaN"), SYMBOL("inf", "inf"),
    SYMBOL("-inf", "-inf"), SYMBOL("1.2x", "1.2x"),
    SYMBOL(".", "."), SYMBOL("+", "+"), SYMBOL("-", "-"),
    SYMBOL("math.min", "math:min"), SYMBOL("np.pi", "np:pi"),
    SYMBOL("a.b.c", "a:b:c"), SYMBOL("a:b", "a:b"),
    SYMBOL("a..b", "a..b"), SYMBOL("a.b.", "a.b."),
    SYMBOL(".hidden", ".hidden"), SYMBOL("sub/a.b", "sub/a.b"),
    SYMBOL("file.metta", "file.metta"), SYMBOL("file.mm2", "file.mm2"),
    SYMBOL("file.act", "file.act"),
    SYMBOL("λ", "λ"), SYMBOL("雪", "雪"), SYMBOL("😀", "😀"),
    SYMBOL("λ.雪", "λ.雪"), SYMBOL("x.雪", "x.雪"),
    SYMBOL("１２", "１２"), SYMBOL("١٢", "١٢"),
};

static unsigned checks, failures;

static void expect(bool condition, const char *label, const char *word,
                   bool rational, bool namespaces) {
    checks++;
    if (condition)
        return;
    failures++;
    fprintf(stderr, "FAIL: %s: %s (rationals=%d namespaces=%d)\n",
            label, word, rational, namespaces);
}

static bool expected_value(const Atom *atom, const WordCase *test,
                           bool rational, bool namespaces) {
    bool rational_values = rational && CETTA_BUILD_WITH_GMP;
    WordKind kind = !rational_values && strchr(test->source, '/')
                        ? WORD_SYMBOL : test->kind;
    const char *text = namespaces ? test->text : test->source;
    if (kind == WORD_SYMBOL) {
        if (!rational_values && strchr(test->source, '/'))
            text = test->source;
        return atom && atom->kind == ATOM_SYMBOL &&
               atom_symbol_equals_cstr(atom, text);
    }
    if (!atom || atom->kind != ATOM_GROUNDED)
        return false;
    switch (kind) {
    case WORD_BOOL:
        return atom->ground.gkind == GV_BOOL &&
               atom->ground.bval == (bool)test->integer;
    case WORD_INT:
        return atom->ground.gkind == GV_INT &&
               atom->ground.ival == test->integer;
    case WORD_FLOAT:
        return atom->ground.gkind == GV_FLOAT &&
               atom->ground.fval == test->floating &&
               !!signbit(atom->ground.fval) == !!signbit(test->floating);
    case WORD_BIGINT:
        return atom->ground.gkind == GV_BIGINT &&
               strcmp(atom_bigint_cstr(atom), test->text) == 0;
    case WORD_RATIONAL:
        return atom->ground.gkind == GV_RATIONAL &&
               strcmp(atom_rational_cstr(atom), test->text) == 0;
    case WORD_SYMBOL:
        break;
    }
    return false;
}

int main(int argc, char **argv) {
    bool benchmark = argc == 4 && strcmp(argv[1], "--bench") == 0;
    bool benchmark_id = benchmark && strcmp(argv[2], "id") == 0;
    if (argc != 1 && (!benchmark ||
        (!benchmark_id && strcmp(argv[2], "atom") != 0))) {
        fprintf(stderr, "usage: %s [--bench atom|id repetitions]\n", argv[0]);
        return 2;
    }
    unsigned long repetitions = benchmark ? strtoul(argv[3], NULL, 10) : 1u;
    if (repetitions == 0u)
        return 2;
    SymbolTable symbols;
    TermUniverse universe;
    Arena persistent, legacy;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    arena_init(&persistent);
    arena_init(&legacy);
    arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    ArenaMark legacy_root = arena_mark(&legacy);
    uint64_t checksum = 0u;

    for (unsigned policy = 0u; policy < 4u; policy++) {
        bool rational = (policy & 1u) != 0u;
        bool namespaces = (policy & 2u) != 0u;
        parser_set_rational_literals_enabled(rational);
        parser_set_namespace_sugar_enabled(namespaces);
        ParserHostProjectionV1 *projection =
            parser_host_projection_v1_new(&universe);
        if (!projection)
            return 2;
        for (unsigned long repeat = 0; repeat < repetitions; repeat++) {
            for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
                const WordCase *test = &cases[index];
                Atom *atom = NULL;
                AtomId id = CETTA_ATOM_ID_NONE;
                if (!benchmark || !benchmark_id) {
                    size_t pos = 0u;
                    arena_reset(&legacy, legacy_root);
                    atom = parse_sexpr(&legacy, test->source, &pos);
                    if (!benchmark) {
                        expect(pos == strlen(test->source), "legacy consumes word",
                               test->source, rational, namespaces);
                        expect(expected_value(atom, test, rational, namespaces),
                               "legacy expected kind and value", test->source,
                               rational, namespaces);
                    }
                    if (benchmark && atom)
                        checksum += (uint64_t)atom->kind + 1u;
                }
                if (!benchmark || benchmark_id) {
                    parser_host_projection_v1_begin_form(projection);
                    id = parser_host_projection_v1_word_bytes(
                        projection, (const uint8_t *)test->source,
                        strlen(test->source));
                    if (benchmark && id != CETTA_ATOM_ID_NONE)
                        checksum += (uint64_t)tu_kind(&universe, id) + 1u;
                }
                if (!benchmark) {
                    Atom *projected = id == CETTA_ATOM_ID_NONE ? NULL :
                        term_universe_copy_atom(&universe, &legacy, id);
                    expect(expected_value(projected, test, rational, namespaces),
                           "projection expected kind and value", test->source,
                           rational, namespaces);
                    expect(atom && projected && atom_eq(atom, projected),
                           "Atom and AtomId agree", test->source,
                           rational, namespaces);
                }
            }
        }
        if (!benchmark) {
            const uint8_t embedded_nul[] = {'a', 0, 'b'};
            expect(parser_host_projection_v1_word_bytes(projection, NULL, 1u) ==
                       CETTA_ATOM_ID_NONE, "null bytes refused", "", rational, namespaces);
            expect(parser_host_projection_v1_word_bytes(projection,
                       (const uint8_t *)"", 0u) == CETTA_ATOM_ID_NONE,
                   "empty word refused", "", rational, namespaces);
            expect(parser_host_projection_v1_word_bytes(projection,
                       embedded_nul, sizeof(embedded_nul)) == CETTA_ATOM_ID_NONE,
                   "embedded NUL refused", "", rational, namespaces);
            parser_set_namespace_sugar_enabled(!namespaces);
            parser_host_projection_v1_begin_form(projection);
            AtomId dotted = parser_host_projection_v1_word_bytes(
                projection, (const uint8_t *)"math.min", 8u);
            expect(dotted != CETTA_ATOM_ID_NONE &&
                       symbol_eq_cstr(g_symbols, tu_sym(&universe, dotted),
                                      namespaces ? "math:min" : "math.min"),
                   "projection retains its namespace policy", "math.min",
                   rational, namespaces);
        }
        parser_host_projection_v1_free(projection);
    }
    term_universe_free(&universe);
    arena_free(&legacy);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    if (benchmark)
        printf("(ParserWordGroundingV1Bench %s %lu %" PRIu64 ")\n",
               benchmark_id ? "id" : "atom", repetitions, checksum);
    else
        printf("(ParserWordGroundingV1Summary %u %u %u)\n",
               checks, checks - failures, failures);
    return failures ? 1 : 0;
}
