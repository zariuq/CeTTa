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
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOperationalLanguageDefV1 ast_wire;
    CettaLanguageDefCoreV1 ast_language;
    CettaOpLangV1Document profile_document;
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 compiled;
    PPNativeV1Prepared prepared;
    CettaDeterministicEquationPlanV1 *ast_projection;
} ExtendedBnfRuntime;

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

static void runtime_init(ExtendedBnfRuntime *runtime) {
    cetta_op_lang_v1_init(&runtime->wire);
    cetta_language_def_core_v1_init(&runtime->language);
    cetta_op_lang_v1_init(&runtime->ast_wire);
    cetta_language_def_core_v1_init(&runtime->ast_language);
    cetta_op_lang_v1_document_init(&runtime->profile_document);
    cetta_ld_parser_profile_v1_init(&runtime->profile);
    cetta_ld_parser_pack_v1_init(&runtime->compiled);
    ppnative_v1_prepared_init(&runtime->prepared);
}

static void runtime_free(ExtendedBnfRuntime *runtime) {
    cetta_deterministic_equation_plan_v1_free(runtime->ast_projection);
    ppnative_v1_prepared_free(&runtime->prepared);
    cetta_ld_parser_pack_v1_free(&runtime->compiled);
    cetta_ld_parser_profile_v1_free(&runtime->profile);
    cetta_op_lang_v1_document_free(&runtime->profile_document);
    cetta_language_def_core_v1_free(&runtime->ast_language);
    cetta_op_lang_v1_free(&runtime->ast_wire);
    cetta_language_def_core_v1_free(&runtime->language);
    cetta_op_lang_v1_free(&runtime->wire);
}

static bool runtime_prepare(
    ExtendedBnfRuntime *runtime, char *error, size_t error_size) {
    CettaOpLangV1Status wire_status =
        CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status =
        CETTA_LD_CORE_V1_BAD_ARGUMENT;
    CettaLdParserPackV1Status pack_status =
        CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    CettaDeterministicEquationStatusV1 equation_status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    const char *projection_sources[] = {
        "langdef/tptp/official_extended_bnf_ast_projection_v1.metta",
    };

    if (!cetta_op_lang_v1_parse_file(
            &runtime->wire,
            "langdef/tptp/official_extended_bnf_source_v1.metta",
            4000000u, 8000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF wire parse status: %s",
                cetta_op_lang_v1_status_name(wire_status));
        }
        return false;
    }
    if (!cetta_language_def_core_v1_decode(
            &runtime->language, &runtime->wire, 500000u,
            &core_status, error, error_size) ||
        core_status != CETTA_LD_CORE_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF LanguageDef status: %s",
                cetta_ld_core_v1_status_name(core_status));
        }
        return false;
    }
    if (!cetta_op_lang_v1_parse_document_file(
            &runtime->profile_document,
            "langdef/tptp/official_extended_bnf_parser_profile_v1.metta",
            1000000u, 2000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF profile parse status: %s",
                cetta_op_lang_v1_status_name(wire_status));
        }
        return false;
    }
    if (!cetta_ld_parser_profile_v1_decode(
            &runtime->profile, &runtime->profile_document, 100000u,
            &pack_status, error, error_size) ||
        pack_status != CETTA_LD_PARSER_PACK_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF profile status: %s",
                cetta_ld_parser_pack_v1_status_name(pack_status));
        }
        return false;
    }
    if (!cetta_language_def_parser_pack_v1_compile(
            &runtime->compiled, &runtime->language,
            runtime->wire.source_sha256, &runtime->profile,
            1000000u, &pack_status, error, error_size) ||
        pack_status != CETTA_LD_PARSER_PACK_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF parser compile status: %s",
                cetta_ld_parser_pack_v1_status_name(pack_status));
        }
        return false;
    }
    if (!ppnative_v1_prepare(
            &runtime->prepared, &runtime->compiled.pack,
            runtime->compiled.start_state, error, error_size)) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size,
                "failed to prepare Extended-BNF native parser tables");
        }
        return false;
    }
    if (!cetta_op_lang_v1_parse_file(
            &runtime->ast_wire,
            "langdef/tptp/official_extended_bnf_ast_v1.metta",
            1000000u, 2000000u, &wire_status, error, error_size) ||
        wire_status != CETTA_OP_LANG_V1_OK ||
        !cetta_language_def_core_v1_decode(
            &runtime->ast_language, &runtime->ast_wire, 200000u,
            &core_status, error, error_size) ||
        core_status != CETTA_LD_CORE_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size,
                "Extended-BNF AST LanguageDef could not be decoded");
        }
        return false;
    }
    if (!cetta_deterministic_equation_plan_v1_load(
            projection_sources, 1u, &runtime->ast_projection,
            &equation_status, error, error_size) ||
        equation_status != CETTA_DETERMINISTIC_EQUATION_V1_OK) {
        if (error[0] == '\0') {
            (void)snprintf(
                error, error_size, "Extended-BNF AST projection status: %s",
                cetta_deterministic_equation_status_name_v1(
                    equation_status));
        }
        return false;
    }
    return true;
}

static bool parse_pair(
    const ExtendedBnfRuntime *runtime,
    const uint8_t *source, size_t source_len,
    uint32_t recognizer_limit, uint32_t replay_depth,
    PPNativeV1Result *gll, PPNativeV1Result *glr,
    char *error, size_t error_size) {
    if (!ppgll_v1_prepared_parse(
            &runtime->prepared, source, source_len,
            recognizer_limit, replay_depth, 8u,
            gll, error, error_size)) {
        return false;
    }
    error[0] = '\0';
    return ppglr_v1_prepared_parse(
        &runtime->prepared, source, source_len,
        recognizer_limit, replay_depth, 8u,
        glr, error, error_size);
}

static bool parser_results_agree(
    const PPNativeV1Result *left, const PPNativeV1Result *right);

static bool parse_pair_rejects(
    const ExtendedBnfRuntime *runtime,
    const uint8_t *source, size_t source_len) {
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    char error[512] = {0};
    bool parsed;
    bool rejected;
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    parsed = parse_pair(
        runtime, source, source_len, 1000000u, 512u,
        &gll, &glr, error, sizeof(error));
    rejected = parsed && !gll.accepted && !glr.accepted &&
        gll.semantic_result_len == 0u &&
        glr.semantic_result_len == 0u &&
        parser_results_agree(&gll, &glr);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    return rejected;
}

static bool parser_results_agree(
    const PPNativeV1Result *left, const PPNativeV1Result *right) {
    uint32_t index;
    if (!left || !right ||
        left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(
                left->semantic_results[index],
                right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static bool project_ast(
    const ExtendedBnfRuntime *runtime, const Atom *cst,
    Arena *arena, Atom **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    Atom *elements[2];
    Atom *call;
    if (!runtime || !runtime->ast_projection || !cst || !arena || !out)
        return false;
    if (cst->kind == ATOM_EXPR && cst->expr.len == 3u &&
        atom_is_symbol(cst->expr.elems[0], "result") &&
        atom_is_symbol(cst->expr.elems[2], "nil")) {
        cst = cst->expr.elems[1];
    }
    elements[0] = atom_symbol(arena, "tptp-ebnf-v1:project");
    elements[1] = (Atom *)cst;
    call = atom_expr(arena, elements, 2u);
    return call && cetta_deterministic_equation_plan_v1_run(
        runtime->ast_projection, call, NULL, NULL, arena,
        16384u, UINT64_C(100000000), out, status, error, error_size);
}

static uint32_t ast_head_count(const Atom *term, const char *head) {
    uint32_t count = 0u;
    if (!term || term->kind != ATOM_EXPR)
        return 0u;
    if (term->expr.len > 0u && atom_is_symbol(term->expr.elems[0], head))
        count++;
    for (CettaExprLen index = 0u; index < term->expr.len; index++)
        count += ast_head_count(term->expr.elems[index], head);
    return count;
}

static bool ast_has_no_cst(const Atom *term) {
    if (!term)
        return false;
    if (term->kind != ATOM_EXPR)
        return true;
    if (term->expr.len > 0u &&
        atom_is_symbol(term->expr.elems[0], "CstRuleV1")) {
        return false;
    }
    for (CettaExprLen index = 0u; index < term->expr.len; index++) {
        if (!ast_has_no_cst(term->expr.elems[index]))
            return false;
    }
    return true;
}

static bool ast_admitted(
    const ExtendedBnfRuntime *runtime, const Atom *ast,
    char *error, size_t error_size) {
    CettaLdGroundTermV1Status status =
        CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    return cetta_language_def_ground_term_v1_admit(
        &runtime->ast_language, "Document", ast,
        32768u, UINT64_C(100000000), &status, error, error_size) &&
        status == CETTA_LD_GROUND_TERM_V1_OK;
}

static uint32_t cst_label_count(
    const Atom *term, const char *label) {
    CettaExprLen index;
    CettaExprLen first_child = 0u;
    uint32_t count = 0u;
    if (!term || term->kind != ATOM_EXPR)
        return 0u;
    if (term->expr.len >= 4u &&
        atom_is_symbol(term->expr.elems[0], "CstRuleV1")) {
        const Atom *actual = term->expr.elems[1];
        first_child = 4u;
        if (actual && actual->kind == ATOM_GROUNDED &&
            actual->ground.gkind == GV_STRING &&
            actual->ground.sval &&
            strcmp(actual->ground.sval, label) == 0) {
            count++;
        }
    }
    for (index = first_child; index < term->expr.len; index++)
        count += cst_label_count(term->expr.elems[index], label);
    return count;
}

static void report_first_rejected_prefix(
    const ExtendedBnfRuntime *runtime,
    const uint8_t *source, size_t source_len) {
    size_t *ends;
    size_t line_count = 0u;
    size_t index;
    size_t low = 0u;
    size_t high;

    ends = (size_t *)malloc(
        (source_len + 1u) * sizeof(*ends));
    if (!ends)
        return;
    for (index = 0u; index < source_len; index++) {
        if (source[index] == '\n')
            ends[line_count++] = index + 1u;
    }
    if (line_count == 0u) {
        free(ends);
        return;
    }
    high = line_count;
    while (low + 1u < high) {
        size_t middle = low + (high - low) / 2u;
        PPNativeV1Result result;
        char error[512] = {0};
        bool parsed;
        ppnative_v1_result_init(&result);
        parsed = ppgll_v1_prepared_parse(
            &runtime->prepared, source, ends[middle - 1u],
            120000000u, 4096u, 2u,
            &result, error, sizeof(error));
        if (parsed && result.outcome == PPNATIVE_V1_COMPLETED &&
            result.accepted && result.semantic_result_len == 1u) {
            low = middle;
        } else {
            high = middle;
        }
        ppnative_v1_result_free(&result);
    }
    if (high > 0u && high <= line_count) {
        size_t left = high == 1u ? 0u : ends[high - 2u];
        size_t right = ends[high - 1u];
        fprintf(
            stderr, "first rejected prefix ends at physical line %zu: %.*s",
            high, (int)(right - left), (const char *)(source + left));
    }
    free(ends);
}

static void official_source_gate(
    TestCounts *counts, const ExtendedBnfRuntime *runtime,
    const uint8_t *source, size_t source_len) {
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    char error[512] = {0};
    bool parsed;
    bool projected = false;
    Atom *ast = NULL;
    Arena ast_arena;
    CettaDeterministicEquationStatusV1 equation_status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;

    arena_init(&ast_arena);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    parsed = parse_pair(
        runtime, source, source_len, 120000000u, 4096u,
        &gll, &glr, error, sizeof(error));
    (void)expect(
        counts, parsed,
        error[0] ? error : "parse the pinned official SyntaxBNF source");
    if (!parsed || gll.outcome != PPNATIVE_V1_COMPLETED ||
        glr.outcome != PPNATIVE_V1_COMPLETED || !gll.accepted ||
        !glr.accepted || gll.semantic_result_len != 1u ||
        glr.semantic_result_len != 1u ||
        !parser_results_agree(&gll, &glr)) {
        fprintf(
            stderr,
            "official parse receipt: parsed=%u "
            "gll=(outcome=%u accepted=%u results=%u digest=%s) "
            "glr=(outcome=%u accepted=%u results=%u digest=%s)\n",
            parsed ? 1u : 0u,
            (unsigned)gll.outcome, gll.accepted ? 1u : 0u,
            gll.semantic_result_len, gll.forest_digest,
            (unsigned)glr.outcome, glr.accepted ? 1u : 0u,
            glr.semantic_result_len, glr.forest_digest);
        report_first_rejected_prefix(runtime, source, source_len);
    }
    (void)expect(
        counts,
        parsed && gll.outcome == PPNATIVE_V1_COMPLETED &&
            gll.accepted && gll.semantic_result_len == 1u &&
            parser_results_agree(&gll, &glr),
        "GLL and GLR agree on one complete official SyntaxBNF CST");
    (void)expect(
        counts, parsed && gll.semantic_result_len == 1u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-alternatives-cons") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-reference") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-repetition-zero-or-more") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-atom-group") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-atom-brace-reference") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-class-item-range") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-class-escape-octal-two") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-class-escape-octal-three") > 0u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-atom-wildcard") > 0u,
        "official RHS values are structured grammar and regular-expression trees");
    error[0] = '\0';
    projected = parsed && gll.semantic_result_len == 1u &&
        project_ast(
            runtime, gll.semantic_results[0], &ast_arena, &ast,
            &equation_status, error, sizeof(error));
    if (!projected && parsed && gll.semantic_result_len == 1u) {
        const Atom *root = gll.semantic_results[0];
        fprintf(stderr, "official CST root kind=%u len=%zu head=", root->kind,
                root->kind == ATOM_EXPR ? (size_t)root->expr.len : 0u);
        if (root->kind == ATOM_EXPR && root->expr.len > 0u)
            atom_print(root->expr.elems[0], stderr);
        fprintf(stderr, " label=");
        if (root->kind == ATOM_EXPR && root->expr.len > 1u)
            atom_print(root->expr.elems[1], stderr);
        fprintf(stderr, "\n");
    }
    (void)expect(
        counts, projected,
        error[0] ? error :
            "project the official CST through authored equations");
    error[0] = '\0';
    (void)expect(
        counts,
        projected && ast_has_no_cst(ast) && ast_admitted(
            runtime, ast, error, sizeof(error)),
        error[0] ? error :
            "official projection is pure structured MeTTa data admitted as Document");
    (void)expect(
        counts,
        projected &&
            ast_head_count(ast, "tptp-ebnf-v1:syntax-row") == 232u &&
            ast_head_count(ast, "tptp-ebnf-v1:semantic-row") == 67u &&
            ast_head_count(ast, "tptp-ebnf-v1:token-row") == 18u &&
            ast_head_count(ast, "tptp-ebnf-v1:character-row") == 38u &&
            ast_head_count(ast, "tptp-ebnf-v1:grammar-reference") > 0u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-class-range") > 0u,
        "official MeTTa AST retains every row kind and structured RHS family");
    arena_free(&ast_arena);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
}

static void boundary_gate(
    TestCounts *counts, const ExtendedBnfRuntime *runtime) {
    static const uint8_t all_kinds[] =
        "<a> ::= !> | ?* | <=> | => | <= | <~> | -->\r\n"
        "<a> :== <b>* |\n"
        "  token\n"
        "<b> ::- (<z>|[a-z])*\n"
        "<c> ::: ({slash_char}|[\\40-\\41]|.)\n";
    static const uint8_t orphan[] = "  orphan\n";
    static const uint8_t invalid_name[] = "<bad-name> ::= x\n";
    static const uint8_t opaque_token_rhs[] = "<a> ::- raw\n";
    static const uint8_t unclosed_reference[] = "<a> ::= <b\n";
    static const uint8_t unclosed_class[] = "<a> ::: [a-z\n";
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    char error[512] = {0};
    bool parsed;
    bool projected;
    Atom *ast = NULL;
    Arena ast_arena;
    CettaDeterministicEquationStatusV1 equation_status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;

    arena_init(&ast_arena);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    parsed = parse_pair(
        runtime, all_kinds, sizeof(all_kinds) - 1u,
        1000000u, 512u, &gll, &glr, error, sizeof(error));
    (void)expect(
        counts,
        parsed && gll.accepted && glr.accepted &&
            gll.semantic_result_len == 1u &&
            parser_results_agree(&gll, &glr) &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-literal-type-forall") == 1u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-literal-type-exists") == 1u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:grammar-repetition-zero-or-more") == 1u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-atom-group") == 2u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-atom-brace-reference") == 1u &&
            cst_label_count(
                gll.semantic_results[0],
                "syntax-bnf:regex-class-item-range") == 2u,
        error[0] ? error :
            "parse structured syntax, semantic, token, and character rows");
    error[0] = '\0';
    projected = parsed && gll.semantic_result_len == 1u &&
        project_ast(
            runtime, gll.semantic_results[0], &ast_arena, &ast,
            &equation_status, error, sizeof(error));
    (void)expect(
        counts,
        projected && ast_has_no_cst(ast) &&
            ast_admitted(runtime, ast, error, sizeof(error)) &&
            ast_head_count(ast, "tptp-ebnf-v1:syntax-row") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:semantic-row") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:token-row") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:character-row") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:grammar-literal") == 12u &&
            ast_head_count(ast, "tptp-ebnf-v1:grammar-zero-or-more") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-group") == 2u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-angle-reference") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-brace-reference") == 1u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-class-range") == 2u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-endpoint-octal-escape") == 2u &&
            ast_head_count(ast, "tptp-ebnf-v1:regex-wildcard") == 1u,
        error[0] ? error :
            "authored equations expose every boundary construct as typed MeTTa data");
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);

    error[0] = '\0';
    parsed = parse_pair(
        runtime, orphan, sizeof(orphan) - 1u,
        100000u, 128u, &gll, &glr, error, sizeof(error));
    (void)expect(
        counts,
        parsed && !gll.accepted && !glr.accepted &&
            gll.semantic_result_len == 0u &&
            parser_results_agree(&gll, &glr),
        "orphan continuation is rejected by both parser kernels");
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);

    error[0] = '\0';
    parsed = parse_pair(
        runtime, invalid_name, sizeof(invalid_name) - 1u,
        100000u, 128u, &gll, &glr, error, sizeof(error));
    (void)expect(
        counts,
        parsed && !gll.accepted && !glr.accepted &&
            gll.semantic_result_len == 0u &&
            parser_results_agree(&gll, &glr),
        "rule names outside the official lexical class are rejected");
    (void)expect(
        counts,
        parse_pair_rejects(
            runtime, opaque_token_rhs, sizeof(opaque_token_rhs) - 1u),
        "token rows reject opaque unstructured right-hand sides");
    (void)expect(
        counts,
        parse_pair_rejects(
            runtime, unclosed_reference,
            sizeof(unclosed_reference) - 1u),
        "syntax rows reject unclosed grammar references");
    (void)expect(
        counts,
        parse_pair_rejects(
            runtime, unclosed_class, sizeof(unclosed_class) - 1u),
        "character rows reject unclosed regular-expression classes");

    arena_free(&ast_arena);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    ExtendedBnfRuntime runtime;
    TestCounts counts = {0u, 0u};
    uint8_t *official_source = NULL;
    size_t official_source_len = 0u;
    char error[512] = {0};
    bool prepared;

    if (argc != 2) {
        fprintf(stderr, "usage: %s PATH_TO_SYNTAX_BNF\n", argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    runtime_init(&runtime);
    prepared = runtime_prepare(&runtime, error, sizeof(error));
    (void)expect(
        &counts, prepared,
        error[0] ? error :
            "compile the authored Extended-BNF LanguageDef to ParserPack");
    official_source = read_file(argv[1], &official_source_len);
    (void)expect(
        &counts, official_source != NULL,
        "read the pinned official SyntaxBNF source");
    if (counts.failed == 0u) {
        official_source_gate(
            &counts, &runtime, official_source, official_source_len);
        boundary_gate(&counts, &runtime);
    }
    printf("(TptpExtendedBnfMetaParserV1Summary %u %u %u)\n",
           counts.passed + counts.failed, counts.passed, counts.failed);
    free(official_source);
    runtime_free(&runtime);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
