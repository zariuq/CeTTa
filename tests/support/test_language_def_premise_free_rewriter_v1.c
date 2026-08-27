#include "native/language_def_premise_free_rewriter_v1.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static const char peano_source[] =
    "(GSLTLanguageDefWireV1 \"PeanoAdd\" "
    "(LCons (TypeDecl \"Nat\" CarrierAst) LNil) "
    "(LCons (GrammarRule \"p:z\" \"Nat\" LNil "
      "(LCons (SyntaxTerminal \"p:z\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:s\" \"Nat\" "
      "(LCons (TermSimple \"body\" (TBase \"Nat\")) LNil) "
      "(LCons (SyntaxTerminal \"p:s\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:add\" \"Nat\" "
      "(LCons (TermSimple \"left\" (TBase \"Nat\")) "
       "(LCons (TermSimple \"right\" (TBase \"Nat\")) LNil)) "
      "(LCons (SyntaxTerminal \"p:add\") LNil) "
      "(EvalSome EvalRewrite)) LNil))) "
    "LNil "
    "(LCons (RewriteRule \"r:left-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (PApp \"p:z\" LNil) "
        "(LCons (FVar \"x\") LNil))) (FVar \"x\")) "
    "(LCons (RewriteRule \"r:right-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (FVar \"x\") "
        "(LCons (PApp \"p:z\" LNil) LNil))) (FVar \"x\")) "
    "(LCons (RewriteRule \"r:succ-add\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) "
       "(LCons (TypeBinding \"y\" (TBase \"Nat\")) LNil)) LNil "
      "(PApp \"p:add\" "
        "(LCons (PApp \"p:s\" (LCons (FVar \"x\") LNil)) "
         "(LCons (FVar \"y\") LNil))) "
      "(PApp \"p:s\" "
        "(LCons (PApp \"p:add\" "
          "(LCons (FVar \"x\") (LCons (FVar \"y\") LNil))) "
         "LNil))) LNil))))";

static const char peano_deleted_rule_source[] =
    "(GSLTLanguageDefWireV1 \"PeanoAddDeleted\" "
    "(LCons (TypeDecl \"Nat\" CarrierAst) LNil) "
    "(LCons (GrammarRule \"p:z\" \"Nat\" LNil "
      "(LCons (SyntaxTerminal \"p:z\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:s\" \"Nat\" "
      "(LCons (TermSimple \"body\" (TBase \"Nat\")) LNil) "
      "(LCons (SyntaxTerminal \"p:s\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:add\" \"Nat\" "
      "(LCons (TermSimple \"left\" (TBase \"Nat\")) "
       "(LCons (TermSimple \"right\" (TBase \"Nat\")) LNil)) "
      "(LCons (SyntaxTerminal \"p:add\") LNil) "
      "(EvalSome EvalRewrite)) LNil))) "
    "LNil "
    "(LCons (RewriteRule \"r:left-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (PApp \"p:z\" LNil) "
        "(LCons (FVar \"x\") LNil))) (FVar \"x\")) "
    "(LCons (RewriteRule \"r:right-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (FVar \"x\") "
        "(LCons (PApp \"p:z\" LNil) LNil))) (FVar \"x\")) "
    "LNil)))";

static const char peano_mutated_rule_source[] =
    "(GSLTLanguageDefWireV1 \"PeanoAddMutated\" "
    "(LCons (TypeDecl \"Nat\" CarrierAst) LNil) "
    "(LCons (GrammarRule \"p:z\" \"Nat\" LNil "
      "(LCons (SyntaxTerminal \"p:z\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:s\" \"Nat\" "
      "(LCons (TermSimple \"body\" (TBase \"Nat\")) LNil) "
      "(LCons (SyntaxTerminal \"p:s\") LNil) EvalNone) "
    "(LCons (GrammarRule \"p:add\" \"Nat\" "
      "(LCons (TermSimple \"left\" (TBase \"Nat\")) "
       "(LCons (TermSimple \"right\" (TBase \"Nat\")) LNil)) "
      "(LCons (SyntaxTerminal \"p:add\") LNil) "
      "(EvalSome EvalRewrite)) LNil))) "
    "LNil "
    "(LCons (RewriteRule \"r:left-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (PApp \"p:z\" LNil) "
        "(LCons (FVar \"x\") LNil))) (FVar \"x\")) "
    "(LCons (RewriteRule \"r:right-zero\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) LNil) LNil "
      "(PApp \"p:add\" (LCons (FVar \"x\") "
        "(LCons (PApp \"p:z\" LNil) LNil))) (FVar \"x\")) "
    "(LCons (RewriteRule \"r:succ-add-mutated\" "
      "(LCons (TypeBinding \"x\" (TBase \"Nat\")) "
       "(LCons (TypeBinding \"y\" (TBase \"Nat\")) LNil)) LNil "
      "(PApp \"p:add\" "
        "(LCons (PApp \"p:s\" (LCons (FVar \"x\") LNil)) "
         "(LCons (FVar \"y\") LNil))) "
      "(FVar \"x\")) LNil))))";

static bool parse_language(CettaOperationalLanguageDefV1 *language,
                           const char *source) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    if (cetta_op_lang_v1_parse_bytes(
            language, (const uint8_t *)source, strlen(source),
            4000000u, 8000000u, &status, error, sizeof(error))) {
        return true;
    }
    fprintf(stderr, "language parse status=%s detail=%s\n",
            cetta_op_lang_v1_status_name(status), error);
    return false;
}

static bool parse_language_file(CettaOperationalLanguageDefV1 *language,
                                const char *path) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    if (cetta_op_lang_v1_parse_file(
            language, path, 16000000u, 32000000u,
            &status, error, sizeof(error))) {
        return true;
    }
    fprintf(stderr, "language file parse status=%s detail=%s\n",
            cetta_op_lang_v1_status_name(status), error);
    return false;
}

static bool parse_term(CettaOpLangV1Document *document, const char *source) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    if (cetta_op_lang_v1_parse_document_bytes(
            document, (const uint8_t *)source, strlen(source),
            1000000u, 2000000u, &status, error, sizeof(error))) {
        return true;
    }
    fprintf(stderr, "term parse status=%s detail=%s\n",
            cetta_op_lang_v1_status_name(status), error);
    return false;
}

static bool compile_program(CettaLdPfrV1Program *program,
                            const CettaOperationalLanguageDefV1 *language) {
    CettaLdPfrV1Status status = CETTA_LD_PFR_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    if (cetta_ld_pfr_v1_compile(
            program, language, &status, error, sizeof(error))) {
        return true;
    }
    fprintf(stderr, "profile compile status=%s detail=%s\n",
            cetta_ld_pfr_v1_status_name(status), error);
    return false;
}

static bool normalize(CettaLdPfrV1Result *result,
                      const CettaLdPfrV1Program *program,
                      const CettaOpLangV1SExpr *term,
                      uint32_t limit) {
    CettaLdPfrV1Status status = CETTA_LD_PFR_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    if (cetta_ld_pfr_v1_normalize(
            program, term, limit, result,
            &status, error, sizeof(error))) {
        return true;
    }
    fprintf(stderr, "normalize status=%s detail=%s\n",
            cetta_ld_pfr_v1_status_name(status), error);
    return false;
}

static bool trace_has_prefix(const CettaLdPfrV1Program *program,
                             const CettaLdPfrV1Result *result,
                             const char *prefix) {
    uint32_t index;
    size_t prefix_len = strlen(prefix);

    for (index = 0u; index < result->rule_len; index++) {
        const uint8_t *name = NULL;
        uint32_t name_len = 0u;
        if (cetta_ld_pfr_v1_rule_name(
                program, result->rule_indices[index],
                &name, &name_len) &&
            name_len >= prefix_len &&
            memcmp(name, prefix, prefix_len) == 0) {
            return true;
        }
    }
    return false;
}

static CettaOpLangV1SExpr *mutable_list_cons_at(
    const CettaOpLangV1SExpr *list, uint32_t index) {
    CettaOpLangV1SExpr *cursor = (CettaOpLangV1SExpr *)list;
    uint32_t position = 0u;

    while (cursor &&
           cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
        if (position == index)
            return cursor;
        cursor = cursor->as.application.arguments[1];
        position++;
    }
    return NULL;
}

static void presentation_drives_execution(TestCounts *counts) {
    static const char input_source[] =
        "(p:add (p:s (p:s (p:z))) (p:s (p:z)))";
    static const char expected_source[] =
        "(p:s (p:s (p:s (p:z))))";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document input;
    CettaOpLangV1Document expected;
    CettaLdPfrV1Result result;
    const uint8_t *last_name = NULL;
    uint32_t last_name_len = 0u;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_op_lang_v1_document_init(&expected);
    cetta_ld_pfr_v1_result_init(&result);

    (void)expect(counts, parse_language(&language, peano_source),
                 "parse supplied premise-free presentation");
    (void)expect(counts, compile_program(&program, &language),
                 "compile supplied premise-free presentation");
    (void)expect(counts, program.constructor_len == 3u &&
                         program.rule_len == 3u,
                 "compiled profile retains constructor and rule counts");
    (void)expect(counts, parse_term(&input, input_source) &&
                         parse_term(&expected, expected_source),
                 "parse closed input and expected terms");
    (void)expect(counts,
                 normalize(&result, &program, input.root, 16u),
                 "execute supplied rewrite rules contextually");
    (void)expect(counts,
                 result.rule_len == 3u &&
                 cetta_ld_pfr_v1_term_equal(
                     result.normal_form, expected.root),
                 "supplied rules determine exact normal form and trace length");
    (void)expect(
        counts,
        cetta_ld_pfr_v1_rule_name(
            &program, result.rule_indices[0],
            &last_name, &last_name_len) &&
            last_name_len == strlen("r:succ-add") &&
            memcmp(last_name, "r:succ-add", last_name_len) == 0,
        "ordered receipt identifies the actual first rule occurrence");

    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&expected);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void deleted_rule_has_no_fallback(TestCounts *counts) {
    static const char input_source[] =
        "(p:add (p:s (p:z)) (p:s (p:z)))";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document input;
    CettaLdPfrV1Result result;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_ld_pfr_v1_result_init(&result);

    (void)expect(counts,
                 parse_language(&language, peano_deleted_rule_source) &&
                     compile_program(&program, &language) &&
                     parse_term(&input, input_source) &&
                     normalize(&result, &program, input.root, 16u),
                 "deleted-rule presentation remains structurally executable");
    (void)expect(counts,
                 result.rule_len == 0u &&
                     cetta_ld_pfr_v1_term_equal(
                         result.normal_form, input.root),
                 "deleted semantics stays deleted without a fixed fallback");

    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void changed_rule_changes_result(TestCounts *counts) {
    static const char input_source[] =
        "(p:add (p:s (p:s (p:z))) (p:s (p:z)))";
    static const char wrong_source[] = "(p:s (p:z))";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document input;
    CettaOpLangV1Document wrong;
    CettaLdPfrV1Result result;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_op_lang_v1_document_init(&wrong);
    cetta_ld_pfr_v1_result_init(&result);

    (void)expect(counts,
                 parse_language(&language, peano_mutated_rule_source) &&
                     compile_program(&program, &language) &&
                     parse_term(&input, input_source) &&
                     parse_term(&wrong, wrong_source) &&
                     normalize(&result, &program, input.root, 16u),
                 "changed presentation remains structurally executable");
    (void)expect(counts,
                 result.rule_len == 1u &&
                     cetta_ld_pfr_v1_term_equal(
                         result.normal_form, wrong.root),
                 "changed rule content changes the computed result");

    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&wrong);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void resource_and_atomicity_gates(TestCounts *counts) {
    static const char input_source[] =
        "(p:add (p:s (p:s (p:z))) (p:s (p:z)))";
    static const char sentinel_source[] = "(p:z)";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document input;
    CettaOpLangV1Document sentinel;
    CettaLdPfrV1Result result;
    CettaLdPfrV1Status status = CETTA_LD_PFR_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_op_lang_v1_document_init(&sentinel);
    cetta_ld_pfr_v1_result_init(&result);

    (void)expect(counts,
                 parse_language(&language, peano_source) &&
                     compile_program(&program, &language) &&
                     parse_term(&input, input_source) &&
                     parse_term(&sentinel, sentinel_source) &&
                     normalize(&result, &program, sentinel.root, 0u),
                 "prepare atomic replacement sentinel");
    (void)expect(
        counts,
        !cetta_ld_pfr_v1_normalize(
            &program, input.root, 2u, &result,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_PFR_V1_STEP_LIMIT &&
            result.rule_len == 0u &&
            cetta_ld_pfr_v1_term_equal(
                result.normal_form, sentinel.root),
        "step exhaustion is explicit and replacement remains atomic");

    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&sentinel);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void walters_zantema_da_executes_supplied_rules(TestCounts *counts) {
    static const char source_path[] =
        "langdef/arithmetic/walters_zantema_da_radix2_v1.metta";
    static const char seven_plus_one[] =
        "(da:add "
          "(da:digit:1 (da:digit:1 (da:digit:1 (da:empty)))) "
          "(da:digit:1 (da:empty)))";
    static const char eight[] =
        "(da:digit:0 (da:digit:0 (da:digit:0 "
          "(da:digit:1 (da:empty)))))";
    static const char seven_times_six[] =
        "(da:mul "
          "(da:digit:1 (da:digit:1 (da:digit:1 (da:empty)))) "
          "(da:digit:0 (da:digit:1 (da:digit:1 (da:empty)))))";
    static const char forty_two[] =
        "(da:digit:0 (da:digit:1 (da:digit:0 "
          "(da:digit:1 (da:digit:0 (da:digit:1 (da:empty)))))))";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document add_input;
    CettaOpLangV1Document add_expected;
    CettaOpLangV1Document mul_input;
    CettaOpLangV1Document mul_expected;
    CettaLdPfrV1Result add_result;
    CettaLdPfrV1Result mul_result;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&add_input);
    cetta_op_lang_v1_document_init(&add_expected);
    cetta_op_lang_v1_document_init(&mul_input);
    cetta_op_lang_v1_document_init(&mul_expected);
    cetta_ld_pfr_v1_result_init(&add_result);
    cetta_ld_pfr_v1_result_init(&mul_result);

    (void)expect(
        counts,
        parse_language_file(&language, source_path) &&
            compile_program(&program, &language),
        "compile generated Walters-Zantema DA wire structurally");
    (void)expect(counts,
                 program.constructor_len == 8u &&
                     program.rule_len == 19u,
                 "generated radix-two wire retains 8 constructors and 19 rules");
    (void)expect(
        counts,
        parse_term(&add_input, seven_plus_one) &&
            parse_term(&add_expected, eight) &&
            normalize(&add_result, &program, add_input.root, 4096u),
        "normalize full-ripple radix-two addition through supplied DA rules");
    (void)expect(
        counts,
        cetta_ld_pfr_v1_term_equal(
            add_result.normal_form, add_expected.root) &&
            trace_has_prefix(&program, &add_result, "wz-da:4[") &&
            trace_has_prefix(&program, &add_result, "wz-da:8["),
        "7 + 1 reaches canonical 8 with carry-rule provenance");
    (void)expect(
        counts,
        parse_term(&mul_input, seven_times_six) &&
            parse_term(&mul_expected, forty_two) &&
            normalize(&mul_result, &program, mul_input.root, 16384u),
        "normalize radix-two multiplication through supplied DA rules");
    (void)expect(
        counts,
        cetta_ld_pfr_v1_term_equal(
            mul_result.normal_form, mul_expected.root) &&
            trace_has_prefix(&program, &mul_result, "wz-da:6[") &&
            trace_has_prefix(&program, &mul_result, "wz-da:10["),
        "7 * 6 reaches canonical 42 with shift-and-add provenance");

    cetta_ld_pfr_v1_result_free(&mul_result);
    cetta_ld_pfr_v1_result_free(&add_result);
    cetta_op_lang_v1_document_free(&mul_expected);
    cetta_op_lang_v1_document_free(&mul_input);
    cetta_op_lang_v1_document_free(&add_expected);
    cetta_op_lang_v1_document_free(&add_input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void walters_zantema_da_semantic_sensitivity(TestCounts *counts) {
    static const char source_path[] =
        "langdef/arithmetic/walters_zantema_da_radix2_v1.metta";
    static const char one_plus_one[] =
        "(da:add (da:digit:1 (da:empty)) "
          "(da:digit:1 (da:empty)))";
    static const char two[] =
        "(da:digit:0 (da:digit:1 (da:empty)))";
    static const char zero[] = "(da:empty)";
    static const char dropped_carry_pattern[] =
        "(PApp \"da:digit:0\" "
          "(LCons (PApp \"da:add\" "
            "(LCons (FVar \"x\") (LCons (FVar \"y\") LNil))) "
           "LNil))";
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaOpLangV1Document input;
    CettaOpLangV1Document expected_two;
    CettaOpLangV1Document expected_zero;
    CettaOpLangV1Document mutation;
    CettaLdPfrV1Result result;
    CettaOpLangV1SExpr *rule_cons;
    CettaOpLangV1SExpr *rule_entry;
    CettaOpLangV1SExpr *original_right = NULL;
    bool ready;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_op_lang_v1_document_init(&expected_two);
    cetta_op_lang_v1_document_init(&expected_zero);
    cetta_op_lang_v1_document_init(&mutation);
    cetta_ld_pfr_v1_result_init(&result);

    ready = parse_language_file(&language, source_path) &&
        parse_term(&input, one_plus_one) &&
        parse_term(&expected_two, two) &&
        parse_term(&expected_zero, zero) &&
        parse_term(&mutation, dropped_carry_pattern);
    (void)expect(counts, ready,
                 "prepare structural DA carry mutation");
    if (!ready)
        goto carry_cleanup;
    rule_cons = mutable_list_cons_at(language.rewrites_field, 6u);
    rule_entry = rule_cons
        ? rule_cons->as.application.arguments[0] : NULL;
    ready = cetta_op_lang_v1_application_is(
        rule_entry, "RewriteRule", 5u);
    (void)expect(counts, ready,
                 "locate generated rule-4 carry instance structurally");
    if (!ready)
        goto carry_cleanup;
    original_right = rule_entry->as.application.arguments[4];
    rule_entry->as.application.arguments[4] = mutation.root;
    mutation.root = NULL;
    ready = compile_program(&program, &language) &&
        normalize(&result, &program, input.root, 256u);
    (void)expect(counts, ready,
                 "compile and execute structurally mutated DA presentation");
    if (ready) {
        (void)expect(
            counts,
            cetta_ld_pfr_v1_term_equal(
                result.normal_form, expected_zero.root) &&
            !cetta_ld_pfr_v1_term_equal(
                result.normal_form, expected_two.root),
            "dropping the supplied carry changes 1 + 1 instead of falling back");
    }
    cetta_ld_pfr_v1_program_free(&program);
    cetta_ld_pfr_v1_result_free(&result);
    mutation.root = rule_entry->as.application.arguments[4];
    rule_entry->as.application.arguments[4] = original_right;

carry_cleanup:
    cetta_ld_pfr_v1_result_free(&result);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_document_free(&mutation);
    cetta_op_lang_v1_document_free(&expected_zero);
    cetta_op_lang_v1_document_free(&expected_two);
    cetta_op_lang_v1_document_free(&input);
    cetta_op_lang_v1_free(&language);

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_ld_pfr_v1_result_init(&result);
    ready = parse_language_file(&language, source_path) &&
        parse_term(&input, one_plus_one);
    (void)expect(counts, ready,
                 "prepare structural DA rule deletion");
    if (!ready)
        goto deletion_cleanup;
    {
        CettaOpLangV1SExpr *previous =
            mutable_list_cons_at(language.rewrites_field, 5u);
        CettaOpLangV1SExpr *removed = previous
            ? previous->as.application.arguments[1] : NULL;
        CettaOpLangV1SExpr *after =
            removed && cetta_op_lang_v1_application_is(
                removed, "LCons", 2u)
                ? removed->as.application.arguments[1] : NULL;

        ready = previous && removed && after;
        (void)expect(counts, ready,
                     "locate rule-4 carry occurrence for deletion");
        if (!ready)
            goto deletion_cleanup;
        previous->as.application.arguments[1] = after;
        ready = compile_program(&program, &language);
        previous->as.application.arguments[1] = removed;
        (void)expect(counts, ready && program.rule_len == 18u,
                     "deleted DA rule produces an 18-rule executable profile");
        if (ready) {
            ready = normalize(&result, &program, input.root, 256u);
            (void)expect(
                counts,
                ready && result.rule_len == 0u &&
                    cetta_ld_pfr_v1_term_equal(
                        result.normal_form, input.root),
                "deleted carry rule removes the corresponding capability");
        }
    }

deletion_cleanup:
    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&input);
    cetta_op_lang_v1_document_init(&expected_two);
    cetta_ld_pfr_v1_result_init(&result);
    ready = parse_language_file(&language, source_path) &&
        parse_term(&input, one_plus_one) &&
        parse_term(&expected_two, two);
    (void)expect(counts, ready,
                 "prepare rename-only DA mutation");
    if (ready) {
        const uint8_t *trace_name = NULL;
        uint32_t trace_name_len = 0u;
        uint8_t original_byte;

        rule_cons = mutable_list_cons_at(language.rewrites_field, 6u);
        rule_entry = rule_cons
            ? rule_cons->as.application.arguments[0] : NULL;
        ready = cetta_op_lang_v1_application_is(
            rule_entry, "RewriteRule", 5u) &&
            rule_entry->as.application.arguments[0]->kind ==
                CETTA_OP_LANG_V1_SEXPR_STRING &&
            rule_entry->as.application.arguments[0]->as.string.len > 0u;
        (void)expect(counts, ready,
                     "locate carry rule display identity for rename");
        if (ready) {
            original_byte =
                rule_entry->as.application.arguments[0]->as.string.bytes[0];
            rule_entry->as.application.arguments[0]->as.string.bytes[0] = 'x';
            ready = compile_program(&program, &language) &&
                normalize(&result, &program, input.root, 256u);
            (void)expect(
                counts,
                ready && cetta_ld_pfr_v1_term_equal(
                    result.normal_form, expected_two.root) &&
                result.rule_len > 0u &&
                cetta_ld_pfr_v1_rule_name(
                    &program, result.rule_indices[0],
                    &trace_name, &trace_name_len) &&
                trace_name_len > 0u && trace_name[0] == 'x',
                "rename changes provenance but not DA behavior");
            rule_entry->as.application.arguments[0]->as.string.bytes[0] =
                original_byte;
        }
    }
    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&expected_two);
    cetta_op_lang_v1_document_free(&input);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

static void unsupported_and_inconsistent_profiles_reject(TestCounts *counts) {
    CettaOperationalLanguageDefV1 language;
    CettaLdPfrV1Program program;
    CettaLdPfrV1Status status = CETTA_LD_PFR_V1_INTERNAL_FAILURE;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaOpLangV1Document unbound;
    CettaOpLangV1Document unknown;
    CettaLdPfrV1Result result;
    CettaOpLangV1SExpr *rule_cons;
    CettaOpLangV1SExpr *rule_entry;
    CettaOpLangV1SExpr *original_right;
    char error[512] = {0};
    bool ready;

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    (void)expect(
        counts,
        cetta_op_lang_v1_parse_file(
            &language, "langdef/arithmetic/exact_arithmetic_v1.metta",
            16000000u, 32000000u, &wire_status,
            error, sizeof(error)),
        "parse a premise-bearing control presentation");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_ld_pfr_v1_compile(
            &program, &language, &status, error, sizeof(error)) &&
            status == CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION,
        "premise-bearing presentation rejects instead of invoking an oracle");
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&unbound);
    ready = parse_language(&language, peano_source) &&
        parse_term(&unbound, "(FVar \"z\")");
    (void)expect(counts, ready,
                 "prepare unbound-result mutation");
    if (ready) {
        rule_cons = mutable_list_cons_at(language.rewrites_field, 2u);
        rule_entry = rule_cons
            ? rule_cons->as.application.arguments[0] : NULL;
        ready = cetta_op_lang_v1_application_is(
            rule_entry, "RewriteRule", 5u);
        if (ready) {
            original_right = rule_entry->as.application.arguments[4];
            rule_entry->as.application.arguments[4] = unbound.root;
            unbound.root = NULL;
            error[0] = '\0';
            (void)expect(
                counts,
                !cetta_ld_pfr_v1_compile(
                    &program, &language, &status,
                    error, sizeof(error)) &&
                    status == CETTA_LD_PFR_V1_MALFORMED_PRESENTATION,
                "unbound result variable rejects during structural compilation");
            unbound.root = rule_entry->as.application.arguments[4];
            rule_entry->as.application.arguments[4] = original_right;
        } else {
            (void)expect(counts, false,
                         "locate rule for unbound-result mutation");
        }
    }
    cetta_op_lang_v1_document_free(&unbound);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);

    cetta_op_lang_v1_init(&language);
    cetta_ld_pfr_v1_program_init(&program);
    cetta_op_lang_v1_document_init(&unknown);
    cetta_ld_pfr_v1_result_init(&result);
    ready = parse_language(&language, peano_source) &&
        compile_program(&program, &language) &&
        parse_term(&unknown, "(p:unknown)");
    (void)expect(counts, ready,
                 "prepare undeclared-constructor input");
    if (ready) {
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_ld_pfr_v1_normalize(
                &program, unknown.root, 16u, &result,
                &status, error, sizeof(error)) &&
                status == CETTA_LD_PFR_V1_MALFORMED_TERM,
            "undeclared input constructor rejects before rewriting");
    }
    cetta_ld_pfr_v1_result_free(&result);
    cetta_op_lang_v1_document_free(&unknown);
    cetta_ld_pfr_v1_program_free(&program);
    cetta_op_lang_v1_free(&language);
}

int main(void) {
    TestCounts counts = {0u, 0u};

    presentation_drives_execution(&counts);
    deleted_rule_has_no_fallback(&counts);
    changed_rule_changes_result(&counts);
    resource_and_atomicity_gates(&counts);
    walters_zantema_da_executes_supplied_rules(&counts);
    walters_zantema_da_semantic_sensitivity(&counts);
    unsupported_and_inconsistent_profiles_reject(&counts);

    printf("language-def-premise-free-rewriter-v1: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed ? 1 : 0;
}
