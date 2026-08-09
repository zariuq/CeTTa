#include "atom.h"
#include "gslt_horn_runtime.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024 };

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool answers_equal(const CettaGsltHornResult *result,
                          Atom *expected) {
    for (size_t index = 0u; index < result->answer_count; index++)
        if (!atom_eq(result->answers[index], expected))
            return false;
    return true;
}

static CettaGsltHornLimits generous_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 100u,
        .max_depth = 1000u,
    };
}

static void test_renamed_canary(Arena *queries, Arena *answers,
                                const char *path) {
    const char *paths[] = {path};
    char error[ERROR_CAP] = {0};
    CettaGsltHornProgram *program = NULL;
    CHECK(cetta_gslt_horn_program_load_paths(
              paths, 1u, &program, error, sizeof(error)),
          "renamed canary presentation loads");
    if (!program) {
        fprintf(stderr, "canary load diagnostic: %s\n", error);
        return;
    }
    CHECK(cetta_gslt_horn_program_rule_count(program) == 4u,
          "all canary rule occurrences are retained");

    Atom *query = parse_one(queries, "(canary-route $from $to)");
    Atom *expected = parse_one(answers, "(canary-route alpha gamma)");
    CettaGsltHornResult result;
    memset(error, 0, sizeof(error));
    CHECK(query && expected && cetta_gslt_horn_query(
              program, answers, query, generous_limits(),
              &result, error, sizeof(error)),
          "renamed canary query executes");
    CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
          "renamed canary query completes");
    CHECK(result.answer_count == 2u,
          "duplicate source occurrences produce duplicate proofs");
    CHECK(answers_equal(&result, expected),
          "both occurrence proofs derive the same route");
    cetta_gslt_horn_result_free(&result);

    CettaGsltHornLimits bounded = generous_limits();
    bounded.max_answers = 1u;
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_horn_query(
              program, answers, query, bounded,
              &result, error, sizeof(error)),
          "bounded canary query reports an execution outcome");
    CHECK(result.outcome == CETTA_GSLT_HORN_ANSWER_LIMIT &&
              result.answer_count == 1u,
          "answer exhaustion is incomplete rather than completion");
    cetta_gslt_horn_result_free(&result);

    Atom *unknown = parse_one(queries, "(canary-unknown datum)");
    memset(error, 0, sizeof(error));
    CHECK(unknown && cetta_gslt_horn_query(
              program, answers, unknown, generous_limits(),
              &result, error, sizeof(error)),
          "unknown relation query remains a valid host execution");
    CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u,
          "unknown relation is inert rather than an error");
    cetta_gslt_horn_result_free(&result);
    cetta_gslt_horn_program_free(program);
}

static void test_subzero_core(Arena *queries, Arena *answers,
                                const char *core_path) {
    const char *paths[] = {core_path};
    char error[ERROR_CAP] = {0};
    CettaGsltHornProgram *program = NULL;
    CHECK(cetta_gslt_horn_program_load_paths(
              paths, 1u, &program, error, sizeof(error)),
          "authored Subzero semantic core loads");
    if (!program) {
        fprintf(stderr, "Subzero load diagnostic: %s\n", error);
        return;
    }
    CHECK(cetta_gslt_horn_program_rule_count(program) == 35u,
          "every authored Subzero core rule is executable");

    Atom *empty = parse_one(queries, "()");
    Atom *quoted_empty = cetta_gslt_quote_atom_v1(answers, empty);
    Atom *roundtrip_empty = cetta_gslt_unquote_atom_v1(
        answers, quoted_empty);
    CHECK(empty && quoted_empty && roundtrip_empty &&
              atom_eq(empty, roundtrip_empty),
          "empty expression round-trips through the generic quotation ABI");
    Atom *ground = atom_bool(queries, true);
    Atom *quoted_ground = cetta_gslt_quote_atom_v1(answers, ground);
    Atom *roundtrip_ground = cetta_gslt_unquote_atom_v1(
        answers, quoted_ground);
    CHECK(ground && quoted_ground && roundtrip_ground &&
              atom_eq(ground, roundtrip_ground),
          "opaque ground round-trips without guest interpretation");

    const char *program_text =
        "(subzero-program-cons rule-one "
        "  (q-app (q-sym (q-str \"=\")) "
        "    (q-cons (q-sym (q-str \"a\")) "
        "      (q-cons (q-sym (q-str \"b\")) q-nil))) "
        "  subzero-program-nil)";
    char query_text[4096];
    char expected_text[4096];
    (void)snprintf(
        query_text, sizeof(query_text),
        "(subzero-step %s (q-sym (q-str \"a\")) $occurrence $result)",
        program_text);
    (void)snprintf(
        expected_text, sizeof(expected_text),
        "(subzero-step %s (q-sym (q-str \"a\")) "
        "  (subzero-at-root rule-one subzero-env-nil) "
        "  (q-sym (q-str \"b\")))",
        program_text);
    Atom *query = parse_one(queries, query_text);
    Atom *expected = parse_one(answers, expected_text);
    CettaGsltHornResult result;
    memset(error, 0, sizeof(error));
    CHECK(query && expected && cetta_gslt_horn_query(
              program, answers, query, generous_limits(),
              &result, error, sizeof(error)),
          "authored Subzero root-step query executes");
    CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 1u,
          "Subzero root step has one occurrence proof");
    CHECK(answers_equal(&result, expected),
          "Subzero result and occurrence are authored-rule derived");
    cetta_gslt_horn_result_free(&result);
    cetta_gslt_horn_program_free(program);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s CANARY_PRESENTATION ZERO_CORE\n", argv[0]);
        return 2;
    }
    SymbolTable symbols;
    Arena queries;
    Arena answers;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&queries);
    arena_init(&answers);

    test_renamed_canary(&queries, &answers, argv[1]);
    test_subzero_core(&queries, &answers, argv[2]);

    arena_free(&answers);
    arena_free(&queries);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(stderr, "GsltHornRuntimeSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(GsltHornRuntimeSummary checks=%u failures=0)\n", checks);
    return 0;
}
