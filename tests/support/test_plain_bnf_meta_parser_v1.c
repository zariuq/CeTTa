#include "native/language_def_parser_pack_v1.h"
#include "native/language_def_ground_term_v1.h"
#include "native/deterministic_equation_plan_v1.h"

#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"
#include "parser_pack_native_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

typedef struct {
    CettaOperationalLanguageDefV1 source_wire;
    CettaLanguageDefCoreV1 source_language;
    CettaOpLangV1Document profile_document;
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 parser;
    PPNativeV1Prepared prepared;
    CettaOperationalLanguageDefV1 grammar_wire;
    CettaLanguageDefCoreV1 grammar_language;
    CettaDeterministicEquationPlanV1 *projection;
} BnfRuntime;

static bool expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
    return false;
}

static uint8_t *read_file(const char *path, size_t *length_out) {
    FILE *file;
    long length;
    uint8_t *bytes;
    if (!path || !length_out || !(file = fopen(path, "rb")))
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc(length ? (size_t)length : 1u);
    if (!bytes ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *length_out = (size_t)length;
    return bytes;
}

static void runtime_init(BnfRuntime *runtime) {
    memset(runtime, 0, sizeof(*runtime));
    cetta_op_lang_v1_init(&runtime->source_wire);
    cetta_language_def_core_v1_init(&runtime->source_language);
    cetta_op_lang_v1_document_init(&runtime->profile_document);
    cetta_ld_parser_profile_v1_init(&runtime->profile);
    cetta_ld_parser_pack_v1_init(&runtime->parser);
    ppnative_v1_prepared_init(&runtime->prepared);
    cetta_op_lang_v1_init(&runtime->grammar_wire);
    cetta_language_def_core_v1_init(&runtime->grammar_language);
}

static void runtime_free(BnfRuntime *runtime) {
    cetta_deterministic_equation_plan_v1_free(runtime->projection);
    cetta_language_def_core_v1_free(&runtime->grammar_language);
    cetta_op_lang_v1_free(&runtime->grammar_wire);
    ppnative_v1_prepared_free(&runtime->prepared);
    cetta_ld_parser_pack_v1_free(&runtime->parser);
    cetta_ld_parser_profile_v1_free(&runtime->profile);
    cetta_op_lang_v1_document_free(&runtime->profile_document);
    cetta_language_def_core_v1_free(&runtime->source_language);
    cetta_op_lang_v1_free(&runtime->source_wire);
}

static bool stage_error(
    char *error, size_t error_size, const char *stage) {
    char detail[512];
    (void)snprintf(
        detail, sizeof(detail), "%s", error && error[0] ? error : "failed");
    if (error && error_size > 0u)
        (void)snprintf(error, error_size, "%.64s: %.400s", stage, detail);
    return false;
}

static bool runtime_prepare(
    BnfRuntime *runtime, char *error, size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    CettaLdParserPackV1Status parser_status =
        CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    CettaDeterministicEquationStatusV1 projection_status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    const char *projection_sources[] = {
        "langdef/bnf/plain_bnf_cst_projection_v1.metta",
    };

    if (!cetta_op_lang_v1_parse_file(
            &runtime->source_wire,
            "langdef/bnf/plain_bnf_source_v1.metta",
            1000000u, 2000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK)
        return stage_error(error, error_size, "source LanguageDef parse");
    if (!cetta_language_def_core_v1_decode(
            &runtime->source_language, &runtime->source_wire, 200000u,
            &core_status, error, error_size) ||
        core_status != CETTA_LD_CORE_V1_OK)
        return stage_error(error, error_size, "source LanguageDef decode");
    if (!cetta_op_lang_v1_parse_document_file(
            &runtime->profile_document,
            "langdef/bnf/plain_bnf_parser_profile_v1.metta",
            200000u, 400000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK)
        return stage_error(error, error_size, "lexical profile parse");
    if (!cetta_ld_parser_profile_v1_decode(
            &runtime->profile, &runtime->profile_document, 100000u,
            &parser_status, error, error_size) ||
        parser_status != CETTA_LD_PARSER_PACK_V1_OK)
        return stage_error(error, error_size, "lexical profile decode");
    if (!cetta_language_def_parser_pack_v1_compile(
            &runtime->parser, &runtime->source_language,
            runtime->source_wire.authority_sha256, &runtime->profile,
            500000u, &parser_status, error, error_size) ||
        parser_status != CETTA_LD_PARSER_PACK_V1_OK)
        return stage_error(error, error_size, "ParserPack compile");
    if (!ppnative_v1_prepare(
            &runtime->prepared, &runtime->parser.pack,
            runtime->parser.start_state, error, error_size))
        return stage_error(error, error_size, "ParserPack prepare");
    if (!cetta_op_lang_v1_parse_file(
            &runtime->grammar_wire,
            "langdef/bnf/plain_bnf_grammar_v1.metta",
            500000u, 1000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK)
        return stage_error(error, error_size, "grammar LanguageDef parse");
    if (!cetta_language_def_core_v1_decode(
            &runtime->grammar_language, &runtime->grammar_wire, 200000u,
            &core_status, error, error_size) ||
        core_status != CETTA_LD_CORE_V1_OK)
        return stage_error(error, error_size, "grammar LanguageDef decode");
    if (!cetta_deterministic_equation_plan_v1_load(
            projection_sources, 1u, &runtime->projection,
            &projection_status, error, error_size) ||
        projection_status != CETTA_DETERMINISTIC_EQUATION_V1_OK)
        return stage_error(error, error_size, "CST projection load");
    return true;
}

static bool parse_pair(
    const BnfRuntime *runtime, const uint8_t *source, size_t source_len,
    PPNativeV1Result *gll, PPNativeV1Result *glr,
    char *error, size_t error_size) {
    if (!ppgll_v1_prepared_parse(
            &runtime->prepared, source, source_len,
            5000000u, 2048u, 4096u, gll, error, error_size))
        return false;
    error[0] = '\0';
    return ppglr_v1_prepared_parse(
        &runtime->prepared, source, source_len,
        5000000u, 2048u, 4096u, glr, error, error_size);
}

static bool results_agree(
    const PPNativeV1Result *left, const PPNativeV1Result *right) {
    uint32_t index;
    if (!left || !right || left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0)
        return false;
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index]))
            return false;
    }
    return true;
}

static bool materialized_forests_agree(
    const PPNativeV1Result *left, const PPNativeV1Result *right) {
    return left && right &&
        left->canonical_forest_materialized &&
        right->canonical_forest_materialized &&
        left->canonical_forest && right->canonical_forest &&
        atom_eq(left->canonical_forest, right->canonical_forest);
}

static bool unwrap_single_cst(
    const PPNativeV1Result *result, const Atom **cst_out) {
    const Atom *answer;
    if (!result || !cst_out || result->semantic_result_len != 1u)
        return false;
    answer = result->semantic_results[0];
    if (!answer || answer->kind != ATOM_EXPR || answer->expr.len != 3u ||
        !atom_is_symbol(answer->expr.elems[0], "result") ||
        !atom_is_symbol(answer->expr.elems[2], "nil"))
        return false;
    *cst_out = answer->expr.elems[1];
    return true;
}

static bool project(
    const BnfRuntime *runtime, const Atom *cst, Arena *arena, Atom **out,
    char *error, size_t error_size) {
    Atom *elements[2];
    Atom *call;
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    elements[0] = atom_symbol(arena, "bnf-v1:project");
    elements[1] = (Atom *)cst;
    call = atom_expr(arena, elements, 2u);
    return call && cetta_deterministic_equation_plan_v1_run(
        runtime->projection, call, NULL, NULL, arena,
        32768u, UINT64_C(100000000), out, &status, error, error_size) &&
        status == CETTA_DETERMINISTIC_EQUATION_V1_OK;
}

static bool ast_admitted(
    const BnfRuntime *runtime, const Atom *ast,
    char *error, size_t error_size) {
    CettaLdGroundTermV1Status status =
        CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    return cetta_language_def_ground_term_v1_admit(
        &runtime->grammar_language, "BnfGrammarDocument", ast,
        32768u, UINT64_C(100000000), &status, error, error_size) &&
        status == CETTA_LD_GROUND_TERM_V1_OK;
}

static uint32_t head_count(const Atom *term, const char *head) {
    uint32_t count = 0u;
    CettaExprLen index;
    if (!term || term->kind != ATOM_EXPR)
        return 0u;
    if (term->expr.len > 0u && atom_is_symbol(term->expr.elems[0], head))
        count++;
    for (index = 0u; index < term->expr.len; index++)
        count += head_count(term->expr.elems[index], head);
    return count;
}

static bool contains_no_cst(const Atom *term) {
    CettaExprLen index;
    if (!term)
        return false;
    if (term->kind != ATOM_EXPR)
        return true;
    if (term->expr.len > 0u &&
        atom_is_symbol(term->expr.elems[0], "CstRuleV1"))
        return false;
    for (index = 0u; index < term->expr.len; index++) {
        if (!contains_no_cst(term->expr.elems[index]))
            return false;
    }
    return true;
}

static bool parse_rejects(
    const BnfRuntime *runtime, const uint8_t *source, size_t source_len) {
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    char error[512] = {0};
    bool ran;
    bool rejected;
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    ran = parse_pair(runtime, source, source_len, &gll, &glr,
                     error, sizeof(error));
    rejected = ran && gll.outcome == PPNATIVE_V1_COMPLETED &&
        glr.outcome == PPNATIVE_V1_COMPLETED &&
        !gll.accepted && !glr.accepted && results_agree(&gll, &glr);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    return rejected;
}

static void replay_depth_is_not_cycle_gate(
    TestCounts *counts, const BnfRuntime *runtime) {
    static const uint8_t source[] = "<s> ::= \"x\"\n";
    for (unsigned backend = 0u; backend < 2u; backend++) {
        PPNativeV1Result limited;
        PPNativeV1Result completed;
        char error[512] = {0};
        bool (*parse)(const PPNativeV1Prepared *, const uint8_t *, size_t,
                      uint32_t, uint32_t, uint32_t, PPNativeV1Result *,
                      char *, size_t) = backend == 0u
            ? ppgll_v1_prepared_parse : ppglr_v1_prepared_parse;
        ppnative_v1_result_init(&limited);
        ppnative_v1_result_init(&completed);
        bool ran = parse(&runtime->prepared, source, sizeof(source) - 1u,
                         5000000u, 1u, 4096u, &limited, error, sizeof(error));
        (void)expect(counts, ran && limited.accepted &&
            limited.outcome == PPNATIVE_V1_REPLAY_DEPTH &&
            limited.semantic_result_len == 0u,
            backend == 0u ? "GLL finite replay depth exhaustion is not a cycle"
                          : "GLR finite replay depth exhaustion is not a cycle");
        ran = parse(&runtime->prepared, source, sizeof(source) - 1u,
                    5000000u, 2048u, 4096u, &completed, error, sizeof(error));
        (void)expect(counts, ran && completed.accepted &&
            completed.outcome == PPNATIVE_V1_COMPLETED &&
            completed.semantic_result_len == 1u &&
            completed.forest_digest[0] != '\0' &&
            strcmp(limited.forest_digest, completed.forest_digest) == 0,
            backend == 0u ? "GLL more replay depth completes the same finite forest"
                          : "GLR more replay depth completes the same finite forest");
        ppnative_v1_result_free(&completed);
        ppnative_v1_result_free(&limited);
    }
}

static void report_first_ambiguous_prefix(
    const BnfRuntime *runtime, const uint8_t *source, size_t source_len) {
    size_t offset;
    uint32_t line = 0u;
    for (offset = 0u; offset < source_len; offset++) {
        PPNativeV1Result result;
        char error[512] = {0};
        bool ran;
        if (source[offset] != '\n')
            continue;
        line++;
        ppnative_v1_result_init(&result);
        ran = ppgll_v1_prepared_parse(
            &runtime->prepared, source, offset + 1u,
            5000000u, 2048u, 4096u,
            &result, error, sizeof(error));
        if (ran && result.outcome == PPNATIVE_V1_COMPLETED &&
            result.accepted && result.semantic_result_len != 1u) {
            fprintf(stderr,
                    "first non-unique BNF prefix: line=%u results=%u\n",
                    line, result.semantic_result_len);
            ppnative_v1_result_free(&result);
            return;
        }
        ppnative_v1_result_free(&result);
    }
}

static void document_gate(
    TestCounts *counts, const BnfRuntime *runtime,
    const uint8_t *source, size_t source_len,
    uint32_t expected_rules, uint32_t expected_literals,
    const char *label) {
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    const Atom *cst = NULL;
    Atom *ast = NULL;
    Arena arena;
    char error[512] = {0};
    bool ran;
    bool projected;

    arena_init(&arena);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    ran = parse_pair(runtime, source, source_len, &gll, &glr,
                     error, sizeof(error));
    if (ran &&
        (!ppnative_v1_materialize_canonical_forest(
             &gll, &runtime->parser.pack, runtime->parser.start_state,
             10000u, error, sizeof(error)) ||
         !ppnative_v1_materialize_canonical_forest(
             &glr, &runtime->parser.pack, runtime->parser.start_state,
             10000u, error, sizeof(error)))) {
        ran = false;
    }
    if (!ran || gll.outcome != PPNATIVE_V1_COMPLETED ||
        glr.outcome != PPNATIVE_V1_COMPLETED ||
        !gll.accepted || !glr.accepted ||
        gll.semantic_result_len != 1u ||
        glr.semantic_result_len != 1u ||
        gll.forest.root_len == 0u || glr.forest.root_len == 0u ||
        !results_agree(&gll, &glr)) {
        fprintf(
            stderr,
            "BNF parse receipt: bytes=%zu "
            "gll=(outcome=%u accepted=%u results=%u farthest=%u digest=%s) "
            "glr=(outcome=%u accepted=%u results=%u farthest=%u digest=%s)\n",
            source_len, (unsigned)gll.outcome, gll.accepted ? 1u : 0u,
            gll.semantic_result_len, gll.forest.farthest_byte,
            gll.forest_digest, (unsigned)glr.outcome,
            glr.accepted ? 1u : 0u, glr.semantic_result_len,
            glr.forest.farthest_byte, glr.forest_digest);
        if (gll.forest.farthest_byte < source_len) {
            size_t offset = gll.forest.farthest_byte;
            size_t available = source_len - offset;
            fprintf(stderr, "BNF rejected near byte %zu: %.*s\n", offset,
                    (int)(available < 80u ? available : 80u),
                    (const char *)(source + offset));
        }
        if (ran && gll.accepted && gll.semantic_result_len != 1u)
            report_first_ambiguous_prefix(runtime, source, source_len);
    }
    (void)expect(
        counts,
        ran && gll.outcome == PPNATIVE_V1_COMPLETED &&
            glr.outcome == PPNATIVE_V1_COMPLETED &&
            gll.accepted && glr.accepted &&
            gll.semantic_result_len == 1u &&
            glr.semantic_result_len == 1u &&
            gll.forest.root_len > 0u && glr.forest.root_len > 0u &&
            results_agree(&gll, &glr),
        error[0] ? error : label);
    if (!ran || !materialized_forests_agree(&gll, &glr)) {
        fprintf(
            stderr,
            "BNF canonical forest mismatch for %s: "
            "gll=(nodes=%u choices=%u materialized=%u) "
            "glr=(nodes=%u choices=%u materialized=%u)\n",
            label, gll.forest.node_len, gll.forest.choice_len,
            gll.canonical_forest_materialized ? 1u : 0u,
            glr.forest.node_len, glr.forest.choice_len,
            glr.canonical_forest_materialized ? 1u : 0u);
    }
    (void)expect(
        counts, ran && materialized_forests_agree(&gll, &glr),
        "GLL and GLR expose the same complete canonical forest");
    projected = ran && unwrap_single_cst(&gll, &cst) &&
        project(runtime, cst, &arena, &ast, error, sizeof(error));
    (void)expect(
        counts,
        projected && contains_no_cst(ast) &&
            ast_admitted(runtime, ast, error, sizeof(error)),
        error[0] ? error : "project and admit structured BNF data");
    (void)expect(
        counts,
        projected && head_count(ast, "bnf-v1:rule") == expected_rules &&
            head_count(ast, "bnf-v1:literal") == expected_literals,
        "structured BNF data preserves rule and literal occurrences");
    if (projected &&
        (head_count(ast, "bnf-v1:rule") != expected_rules ||
         head_count(ast, "bnf-v1:literal") != expected_literals)) {
        fprintf(stderr,
                "BNF structured counts: rules=%u literals=%u "
                "expected-rules=%u expected-literals=%u\n",
                head_count(ast, "bnf-v1:rule"),
                head_count(ast, "bnf-v1:literal"),
                expected_rules, expected_literals);
    }
    arena_free(&arena);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
}

static void denotation_nullary_gate(TestCounts *counts) {
    const char *sources[] = {"langdef/bnf/plain_bnf_denotation_v1.metta"};
    static const char mutation_target[] =
        "(bnf-v1:suffix-start)\n"
        "        (bnf-v1:text-cons 35 (bnf-v1:text-nil))";
    static const uint8_t symbol_source[] =
        "(gslt-presentation-v1 QuotedSourceSymbols "
        "(signature (operator metta-equation 2) "
        "(operator source-symbols 0) (operator symbols 5)) "
        "(equations) (rewrites "
        "(rule symbols (head (metta-equation (source-symbols) "
        "(symbols True true False false \"True\"))) (body))))";
    CettaDeterministicEquationPlanV1 *plan = NULL;
    CettaDeterministicEquationPlanV1 *input_plan = NULL;
    CettaDeterministicEquationPlanV1 *mutant_plan = NULL;
    CettaDeterministicEquationPlanV1 *refused_plan = NULL;
    CettaDeterministicEquationPlanV1 *symbol_plan = NULL;
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    char error[512] = {0};
    Arena arena;
    Atom *out = NULL;
    Atom *name;
    Atom *call;
    Atom *nil_head;
    Atom *nil;
    Atom *expected;
    Atom *mutated_expected;
    uint8_t *source_bytes = NULL;
    size_t source_length = 0u;
    size_t mutation_offset = 0u;
    size_t mutation_count = 0u;
    uint64_t work_used = 0u;
    CettaDeterministicEquationInputV1 input;
    bool loaded;

    arena_init(&arena);
    loaded = cetta_deterministic_equation_plan_v1_load(
        sources, 1u, &plan, &status, error, sizeof(error));
    if (!expect(counts, loaded && status == CETTA_DETERMINISTIC_EQUATION_V1_OK,
                "load authored denotation for symbol/call distinction"))
        goto cleanup;

    name = atom_symbol(&arena, "bnf-v1:suffix-start");
    call = atom_expr(&arena, &name, 1u);
    nil_head = atom_symbol(&arena, "bnf-v1:text-nil");
    nil = atom_expr(&arena, &nil_head, 1u);
    expected = atom_expr3(&arena, atom_symbol(&arena, "bnf-v1:text-cons"),
                         atom_int(&arena, 35), nil);

    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            plan, name, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_OK && atom_eq(out, name),
        "a denotation function symbol remains data until called");
    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            plan, call, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_OK &&
            atom_eq(out, expected) && !atom_eq(out, name),
        "the authored zero-argument call evaluates to its structured suffix");

    {
        Atom *empty = atom_expr(&arena, NULL, 0u);
        Atom *boxed_fields[2] = {atom_symbol(&arena, "data-box"), empty};
        Atom *boxed = atom_expr(&arena, boxed_fields, 2u);
        (void)expect(counts,
            cetta_deterministic_equation_plan_v1_run(
                plan, empty, NULL, NULL, &arena, 128u, 10000u,
                &out, &status, error, sizeof(error)) &&
                status == CETTA_DETERMINISTIC_EQUATION_V1_OK &&
                out->kind == ATOM_EXPR && out->expr.len == 0u &&
                !atom_eq(out, nil),
            "empty tuple is data and remains distinct from a nullary constructor");
        (void)expect(counts,
            cetta_deterministic_equation_plan_v1_run(
                plan, boxed, NULL, NULL, &arena, 128u, 10000u,
                &out, &status, error, sizeof(error)) && atom_eq(out, boxed),
            "ordinary data constructors preserve nested empty tuples");
    }

    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run_counted(
            plan, call, NULL, NULL, &arena, 128u, 10000u, &work_used,
            &out, &status, error, sizeof(error)) &&
            atom_eq(out, expected) && work_used > 0u && work_used <= 10000u,
        "counted execution preserves the result and reports consumed work");
    (void)expect(counts,
        !cetta_deterministic_equation_plan_v1_run_counted(
            plan, call, NULL, NULL, &arena, 128u, 1u, &work_used,
            &out, &status, error, sizeof(error)) && out == NULL &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT &&
            work_used == 1u,
        "exhausted execution reports spent work without an answer");
    work_used = UINT64_MAX;
    (void)expect(counts,
        !cetta_deterministic_equation_plan_v1_run_counted(
            NULL, call, NULL, NULL, &arena, 128u, 10000u, &work_used,
            &out, &status, error, sizeof(error)) && out == NULL &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT &&
            work_used == 0u,
        "invalid counted execution reports zero consumed work");

    source_bytes = read_file(sources[0], &source_length);
    if (!expect(counts, source_bytes != NULL,
                "read the exact authored denotation bytes for input loading"))
        goto cleanup;
    input = (CettaDeterministicEquationInputV1){
        .bytes = source_bytes,
        .length = source_length,
        .source = sources[0],
    };
    if (!expect(counts,
                cetta_deterministic_equation_plan_v1_load_inputs(
                    &input, 1u, &input_plan, &status,
                    error, sizeof(error)) &&
                    status == CETTA_DETERMINISTIC_EQUATION_V1_OK,
                "load the actual authored denotation through source buffers"))
        goto cleanup;
    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            input_plan, call, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_OK &&
            atom_eq(out, expected),
        "path and buffer loaders execute the same actual source equation");

    for (size_t index = 0u;
         index + sizeof(mutation_target) - 1u <= source_length; index++) {
        if (memcmp(source_bytes + index, mutation_target,
                   sizeof(mutation_target) - 1u) == 0) {
            mutation_offset = index + strlen(
                "(bnf-v1:suffix-start)\n        (bnf-v1:text-cons 3");
            mutation_count++;
        }
    }
    if (!expect(counts, mutation_count == 1u,
                "locate exactly one source suffix scalar for mutation"))
        goto cleanup;
    source_bytes[mutation_offset] = '6';
    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            input_plan, call, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) && atom_eq(out, expected),
        "loaded equations own their source independently of input buffers");
    if (!expect(counts,
                cetta_deterministic_equation_plan_v1_load_inputs(
                    &input, 1u, &mutant_plan, &status,
                    error, sizeof(error)),
                "load a same-length mutation of the actual authored source"))
        goto cleanup;
    mutated_expected = atom_expr3(
        &arena, atom_symbol(&arena, "bnf-v1:text-cons"),
        atom_int(&arena, 36), nil);
    free(source_bytes);
    source_bytes = NULL;
    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            mutant_plan, call, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) &&
            atom_eq(out, mutated_expected) && !atom_eq(out, expected),
        "source-buffer mutation changes execution after its bytes are released");

    input.bytes = (const uint8_t *)")";
    input.length = 1u;
    (void)expect(counts,
        !cetta_deterministic_equation_plan_v1_load_inputs(
            &input, 1u, &refused_plan, &status, error, sizeof(error)) &&
            refused_plan == NULL &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION &&
            error[0] != '\0',
        "source-buffer loading rejects malformed source without a plan");
    (void)expect(counts,
        !cetta_deterministic_equation_plan_v1_load_inputs(
            &input, 0u, &refused_plan, &status, error, sizeof(error)) &&
            refused_plan == NULL &&
            status == CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT &&
            error[0] != '\0',
        "source-buffer loading rejects an empty input request");

    input.bytes = symbol_source;
    input.length = sizeof(symbol_source) - 1u;
    input.source = "quoted-source-symbol-control";
    if (!expect(counts,
                cetta_deterministic_equation_plan_v1_load_inputs(
                    &input, 1u, &symbol_plan, &status, error, sizeof(error)),
                "quoted source preserves Boolean-shaped symbol spellings"))
        goto cleanup;
    name = atom_symbol(&arena, "source-symbols");
    call = atom_expr(&arena, &name, 1u);
    (void)expect(counts,
        cetta_deterministic_equation_plan_v1_run(
            symbol_plan, call, NULL, NULL, &arena, 128u, 10000u,
            &out, &status, error, sizeof(error)) &&
            out && out->kind == ATOM_EXPR && out->expr.len == 6u &&
            atom_is_symbol(out->expr.elems[1], "True") &&
            atom_is_symbol(out->expr.elems[2], "true") &&
            atom_is_symbol(out->expr.elems[3], "False") &&
            atom_is_symbol(out->expr.elems[4], "false") &&
            atom_eq(out->expr.elems[5], atom_string(&arena, "True")) &&
            !atom_eq(out->expr.elems[1], out->expr.elems[2]) &&
            !atom_eq(out->expr.elems[1], out->expr.elems[5]) &&
            !atom_eq(out->expr.elems[1], atom_bool(&arena, true)) &&
            !atom_eq(out->expr.elems[3], atom_bool(&arena, false)),
        "source symbols stay distinct from aliases, strings, and host Booleans");
cleanup:
    free(source_bytes);
    cetta_deterministic_equation_plan_v1_free(refused_plan);
    cetta_deterministic_equation_plan_v1_free(mutant_plan);
    cetta_deterministic_equation_plan_v1_free(input_plan);
    cetta_deterministic_equation_plan_v1_free(symbol_plan);
    cetta_deterministic_equation_plan_v1_free(plan);
    arena_free(&arena);
}

static void source_loader_gate(TestCounts *counts) {
    static const char source[] =
        "(gslt-presentation-v1 SourceKinds "
        "(signature (operator metta-equation 2) (operator kinds 0) "
        "(operator tuple 10) (operator duplicate 1) (operator via 1) "
        "(operator pair 2) (operator let 3) (operator nothing 0)) "
        "(equations) (rewrites "
        "(rule literals (head (metta-equation (kinds) "
        "(tuple $x 1.0 1e2 +1 ? 00 -0 \"?x\" nothing (nothing)))) (body)) "
        "(rule dup (head (metta-equation (duplicate ?x) (pair ?x ?x))) (body)) "
        "(rule via (head (metta-equation (via ?x) "
        "(let ?y (duplicate ?x) (pair ?y ?x)))) (body))))";
    static const char other[] =
        "(gslt-presentation-v1 SourceOther "
        "(signature (operator metta-equation 2) (operator second 1) "
        "(operator pair 2)) (equations) (rewrites "
        "(rule second (head (metta-equation (second ?x) (pair ?x \"?x\"))) (body))))";
    struct {
        const char *signature;
        const char *equations;
        const char *rewrites;
        const char *label;
    } invalid[] = {
        {"(operator f 0) (operator f 0)", "", "",
         "duplicate operators inside one presentation are refused"},
        {"", "(same A B)", "",
         "nonempty authored equation theory is refused by this source profile"},
        {"", "", "(rule bad (head (unknown A)) (body))",
         "an undeclared head in a nonselected rule is still refused"},
        {"(operator p 1)", "", "(rule bad (head (p 9223372036854775808)) (body))",
         "overflowing integer in a nonselected rule is still refused"},
        {"(operator p 1)", "", "(rule bad (head (p A)) (body (p -9223372036854775809)))",
         "overflowing integer in a nonselected premise is still refused"},
        {"", "", "(rule extra (head (metta-equation (f) 9223372036854775808)) (body))",
         "overflowing integer in an executable equation is refused"},
        {"", "", "(rule extra (head (metta-equation (f) ?free)) (body))",
         "unbound equation output is refused"},
        {"(operator two 2)", "", "(rule extra (head (metta-equation (two ?x ?x) A)) (body))",
         "nonlinear equation patterns are refused"},
        {"", "", "(rule extra (head (metta-equation (f) B)) (body))",
         "overlapping equation patterns are refused"},
    };
    CettaDeterministicEquationPlanV1 *plan = NULL;
    CettaDeterministicEquationStatusV1 status;
    CettaDeterministicEquationInputV1 inputs[2] = {
        {(const uint8_t *)source, sizeof(source) - 1u, "source-kinds"},
        {(const uint8_t *)other, sizeof(other) - 1u, "source-other"},
    };
    char error[512] = {0};
    Arena arena;
    Atom *out = NULL;
    arena_init(&arena);
    bool loaded = cetta_deterministic_equation_plan_v1_load_inputs(
        inputs, 2u, &plan, &status, error, sizeof(error));
    if (expect(counts, loaded,
               "compose source buffers with shared operator declarations")) {
        Atom *head = atom_symbol(&arena, "kinds");
        bool ran = cetta_deterministic_equation_plan_v1_run(
            plan, atom_expr(&arena, &head, 1u), NULL, NULL, &arena,
            128u, 10000u, &out, &status, error, sizeof(error));
        (void)expect(counts, ran && out && out->kind == ATOM_EXPR &&
            out->expr.len == 11u &&
            atom_is_symbol(out->expr.elems[1], "$x") &&
            atom_is_symbol(out->expr.elems[2], "1.0") &&
            atom_is_symbol(out->expr.elems[3], "1e2") &&
            atom_is_symbol(out->expr.elems[4], "+1") &&
            atom_is_symbol(out->expr.elems[5], "?") &&
            atom_eq(out->expr.elems[6], atom_int(&arena, 0)) &&
            atom_eq(out->expr.elems[7], atom_int(&arena, 0)) &&
            atom_eq(out->expr.elems[8], atom_string(&arena, "?x")) &&
            atom_is_symbol(out->expr.elems[9], "nothing") &&
            out->expr.elems[10]->kind == ATOM_EXPR &&
            out->expr.elems[10]->expr.len == 1u,
            "source literals retain kinds, integer normalization, and nullary distinctions");
        Atom *value = atom_string(&arena, "input");
        Atom *pair = atom_expr3(&arena, atom_symbol(&arena, "pair"), value, value);
        Atom *expected = atom_expr3(&arena, atom_symbol(&arena, "pair"), pair, value);
        (void)expect(counts,
            cetta_deterministic_equation_plan_v1_run(
                plan, atom_expr2(&arena, atom_symbol(&arena, "via"), value),
                NULL, NULL, &arena, 128u, 10000u, &out, &status,
                error, sizeof(error)) && atom_eq(out, expected),
            "shared variables, nested calls, and local let bindings survive projection");
        expected = atom_expr3(&arena, atom_symbol(&arena, "pair"), value,
                             atom_string(&arena, "?x"));
        (void)expect(counts,
            cetta_deterministic_equation_plan_v1_run(
                plan, atom_expr2(&arena, atom_symbol(&arena, "second"), value),
                NULL, NULL, &arena, 128u, 10000u, &out, &status,
                error, sizeof(error)) && atom_eq(out, expected),
            "same-spelled variables bind per rule while strings remain literal");
    } else if (error[0]) fprintf(stderr, "source loader: %s\n", error);
    cetta_deterministic_equation_plan_v1_free(plan);
    plan = NULL;

    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        char text[2048];
        int length = snprintf(text, sizeof(text),
            "(gslt-presentation-v1 Invalid (signature (operator metta-equation 2) "
            "(operator f 0) %s) (equations %s) (rewrites "
            "(rule base (head (metta-equation (f) A)) (body)) %s))",
            invalid[index].signature, invalid[index].equations, invalid[index].rewrites);
        inputs[0] = (CettaDeterministicEquationInputV1){
            (const uint8_t *)text, length > 0 ? (size_t)length : 0u, "invalid-source"};
        (void)expect(counts, length > 0 && (size_t)length < sizeof(text) &&
            !cetta_deterministic_equation_plan_v1_load_inputs(
                inputs, 1u, &plan, &status, error, sizeof(error)) &&
            plan == NULL && error[0], invalid[index].label);
        cetta_deterministic_equation_plan_v1_free(plan);
        plan = NULL;
    }
    char arity_source[512];
    int length = snprintf(arity_source, sizeof(arity_source),
        "(gslt-presentation-v1 LargeUnusedArity "
        "(signature (operator metta-equation 2) (operator f 0) (operator unused %zu)) "
        "(equations) (rewrites (rule f (head (metta-equation (f) A)) (body))))",
        SIZE_MAX);
    inputs[0] = (CettaDeterministicEquationInputV1){
        (const uint8_t *)arity_source, (size_t)length, "large-unused-arity"};
    (void)expect(counts, length > 0 && (size_t)length < sizeof(arity_source) &&
        cetta_deterministic_equation_plan_v1_load_inputs(
            inputs, 1u, &plan, &status, error, sizeof(error)),
        "unused signature arity retains the complete size_t range");
    cetta_deterministic_equation_plan_v1_free(plan);
    arena_free(&arena);
}

int main(void) {
    static const uint8_t minimal[] =
        "<digit> ::= \"0\" | \"1\"\n";
    static const uint8_t malformed_assignment[] =
        "<digit> := \"0\"\n";
    static const uint8_t unclosed_reference[] =
        "<digit ::= \"0\"\n";
    static const uint8_t unclosed_literal[] =
        "<digit> ::= \"0\n";
    static const uint8_t unknown_escape[] =
        "<digit> ::= \"\\q\"\n";
    SymbolTable symbols;
    BnfRuntime runtime;
    TestCounts counts = {0u, 0u};
    uint8_t *examples = NULL;
    uint8_t *self = NULL;
    uint8_t *dyck = NULL;
    uint8_t *mutual = NULL;
    uint8_t *nullable = NULL;
    uint8_t *ambiguous = NULL;
    size_t examples_len = 0u;
    size_t self_len = 0u;
    size_t dyck_len = 0u;
    size_t mutual_len = 0u;
    size_t nullable_len = 0u;
    size_t ambiguous_len = 0u;
    char error[512] = {0};
    bool prepared;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    denotation_nullary_gate(&counts);
    source_loader_gate(&counts);
    runtime_init(&runtime);
    prepared = runtime_prepare(&runtime, error, sizeof(error));
    (void)expect(
        &counts, prepared,
        error[0] ? error :
            "compile the plain BNF LanguageDef through ParserPack");
    if (prepared) {
        (void)expect(
            &counts,
            runtime.parser.authored_rule_len >= 30u &&
                runtime.parser.lexical_rule_len == 4u,
            "compiled parser is derived from authored and lexical rules");
        document_gate(
            &counts, &runtime, minimal, sizeof(minimal) - 1u,
            1u, 2u, "GLL and GLR agree on minimal BNF");
        replay_depth_is_not_cycle_gate(&counts, &runtime);

        examples = read_file(
            "tests/langdef/bnf/plain_bnf_examples_v1.bnf", &examples_len);
        (void)expect(&counts, examples != NULL, "read BNF examples");
        if (examples)
            document_gate(
                &counts, &runtime, examples, examples_len,
                4u, 7u, "GLL and GLR agree on occurrence canaries");

        self = read_file(
            "langdef/bnf/plain_bnf_self_v1.bnf", &self_len);
        (void)expect(&counts, self != NULL, "read BNF self-description");
        if (self)
            document_gate(
                &counts, &runtime, self, self_len,
                22u, 22u, "parse BNF self-description as structured data");

        dyck = read_file(
            "tests/langdef/bnf/plain_bnf_dyck_v1.bnf", &dyck_len);
        (void)expect(&counts, dyck != NULL, "read Dyck BNF specimen");
        if (dyck)
            document_gate(
                &counts, &runtime, dyck, dyck_len,
                1u, 2u, "GLL and GLR agree on recursive Dyck grammar data");

        mutual = read_file(
            "tests/langdef/bnf/plain_bnf_mutual_v1.bnf", &mutual_len);
        (void)expect(
            &counts, mutual != NULL, "read mutually recursive BNF specimen");
        if (mutual)
            document_gate(
                &counts, &runtime, mutual, mutual_len,
                2u, 4u,
                "GLL and GLR agree on mutually recursive grammar data");

        nullable = read_file(
            "tests/langdef/bnf/plain_bnf_nullable_v1.bnf", &nullable_len);
        (void)expect(
            &counts, nullable != NULL, "read nullable BNF specimen");
        if (nullable)
            document_gate(
                &counts, &runtime, nullable, nullable_len,
                2u, 2u, "GLL and GLR agree on nullable grammar data");

        ambiguous = read_file(
            "tests/langdef/bnf/plain_bnf_ambiguous_v1.bnf",
            &ambiguous_len);
        (void)expect(
            &counts, ambiguous != NULL, "read ambiguous BNF specimen");
        if (ambiguous)
            document_gate(
                &counts, &runtime, ambiguous, ambiguous_len,
                3u, 2u, "GLL and GLR agree on ambiguous grammar data");

        (void)expect(
            &counts,
            parse_rejects(
                &runtime, malformed_assignment,
                sizeof(malformed_assignment) - 1u),
            "reject malformed assignment token");
        (void)expect(
            &counts,
            parse_rejects(
                &runtime, unclosed_reference,
                sizeof(unclosed_reference) - 1u),
            "reject unclosed nonterminal reference");
        (void)expect(
            &counts,
            parse_rejects(
                &runtime, unclosed_literal,
                sizeof(unclosed_literal) - 1u),
            "reject unclosed terminal literal");
        (void)expect(
            &counts,
            parse_rejects(
                &runtime, unknown_escape,
                sizeof(unknown_escape) - 1u),
            "reject undeclared terminal escape");
    }
    printf("(PlainBnfMetaParserV1Summary %u %u %u)\n",
           counts.passed + counts.failed, counts.passed, counts.failed);
    free(ambiguous);
    free(nullable);
    free(mutual);
    free(dyck);
    free(self);
    free(examples);
    runtime_free(&runtime);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
