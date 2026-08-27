#include "native/exact_arithmetic_to_external_call_transform_v1.h"
#include "native/operational_language_def_v1.h"

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

static bool text_equal(const CettaLdTextV1 *left,
                       const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         memcmp(left->bytes, right->bytes, left->len) == 0);
}

static bool text_is(const CettaLdTextV1 *text, const char *literal) {
    size_t len = literal ? strlen(literal) : 0u;
    return text && literal && text->len == len &&
        (len == 0u || memcmp(text->bytes, literal, len) == 0);
}

static bool pattern_equal(const CettaLdPatternV1 *left,
                          const CettaLdPatternV1 *right) {
    uint32_t index;

    if (!left || !right || left->kind != right->kind)
        return false;
    if (left->kind == CETTA_LD_PATTERN_FVAR_V1)
        return text_equal(&left->as.fvar, &right->as.fvar);
    if (left->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        !text_equal(&left->as.apply.head, &right->as.apply.head) ||
        left->as.apply.arguments.len != right->as.apply.arguments.len)
        return false;
    for (index = 0u; index < left->as.apply.arguments.len; index++) {
        if (!pattern_equal(&left->as.apply.arguments.items[index],
                           &right->as.apply.arguments.items[index]))
            return false;
    }
    return true;
}

static CettaLdPatternV1 *find_head(CettaLdPatternV1 *pattern,
                                   const char *head) {
    uint32_t index;
    CettaLdPatternV1 *found;

    if (!pattern || pattern->kind != CETTA_LD_PATTERN_APPLY_V1)
        return NULL;
    if (text_is(&pattern->as.apply.head, head))
        return pattern;
    for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
        found = find_head(&pattern->as.apply.arguments.items[index], head);
        if (found)
            return found;
    }
    return NULL;
}

static bool load_language(const char *path,
                          CettaOperationalLanguageDefV1 *wire,
                          CettaLanguageDefCoreV1 *core,
                          char *error,
                          size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;

    return cetta_op_lang_v1_parse_file(
               wire, path, 32000000u, 64000000u, &wire_status,
               error, error_size) &&
        cetta_language_def_core_v1_decode(
            core, wire, 400000u, &core_status, error, error_size);
}

int main(void) {
    TestCounts counts = {0};
    CettaOperationalLanguageDefV1 source_wire;
    CettaOperationalLanguageDefV1 target_wire;
    CettaLanguageDefCoreV1 source;
    CettaLanguageDefCoreV1 target;
    CettaExactArithmeticExternalCallTransformV1 baseline;
    CettaExactArithmeticExternalCallTransformV1 candidate;
    CettaExactArithmeticExternalCallTransformStatusV1 status =
        CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1;
    CettaExactArithmeticExternalCallProgramViewV1 view;
    CettaLdPatternV1 *mutated_call;
    char error[512] = {0};
    uint32_t index;
    bool loaded;

    cetta_op_lang_v1_init(&source_wire);
    cetta_op_lang_v1_init(&target_wire);
    cetta_language_def_core_v1_init(&source);
    cetta_language_def_core_v1_init(&target);
    cetta_exact_arithmetic_external_call_transform_v1_init(&baseline);
    cetta_exact_arithmetic_external_call_transform_v1_init(&candidate);
    loaded = load_language("langdef/arithmetic/exact_arithmetic_v1.metta",
                           &source_wire, &source, error, sizeof(error)) &&
        load_language("langdef/machines/external_call_machine_v1.metta",
                      &target_wire, &target, error, sizeof(error));
    (void)expect(&counts, loaded,
                 error[0] ? error : "load source and target GSLTs");
    if (loaded) {
        (void)expect(
        &counts,
        cetta_exact_arithmetic_to_external_call_transform_v1(
            &baseline, &source, &target, &status, error, sizeof(error)) &&
            baseline.entry_len == CETTA_EXACT_ARITHMETIC_OP_COUNT_V1,
        error[0] ? error : "transform supplied GSLTs into external-call Patterns");
    for (index = 0u; index < baseline.entry_len; index++) {
        bool partial = index >= CETTA_EXACT_ARITHMETIC_OP_TQUOT_V1;
        (void)expect(
            &counts,
            baseline.entries[index].source_operation.kind ==
                CETTA_LD_PATTERN_APPLY_V1 &&
                cetta_exact_arithmetic_external_call_program_v1_inspect(
                    &target, &baseline.entries[index].target_program, &view) &&
                view.guarded == partial,
            "each authored operation yields an admitted total/guarded external-call Pattern");
    }

    memcpy(source.terms[0].label.bytes + 6u, "sum", 3u);
    memcpy(source.rewrites[0].left.as.apply.arguments.items[0]
               .as.apply.head.bytes + 6u,
           "sum", 3u);
    (void)expect(
        &counts,
        cetta_exact_arithmetic_to_external_call_transform_v1(
            &candidate, &source, &target, &status, error, sizeof(error)) &&
            text_is(&candidate.entries[0].source_operation.as.apply.head,
                    "arith:sum") &&
            pattern_equal(&candidate.entries[0].target_program,
                          &baseline.entries[0].target_program),
        "source constructor rename changes provenance but not arithmetic ExternalCall");
    cetta_exact_arithmetic_external_call_transform_v1_free(&candidate);
    memcpy(source.terms[0].label.bytes + 6u, "add", 3u);
    memcpy(source.rewrites[0].left.as.apply.arguments.items[0]
               .as.apply.head.bytes + 6u,
           "add", 3u);

    memcpy(source.rewrites[0].premises.items[0]
               .as.relation_query.relation.bytes + 12u,
           "Sub", 3u);
    (void)expect(
        &counts,
        !cetta_exact_arithmetic_to_external_call_transform_v1(
            &candidate, &source, &target, &status, error, sizeof(error)) &&
            status ==
                CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_SOURCE_V1,
        "source semantic mutation is rejected rather than running old behavior");
    memcpy(source.rewrites[0].premises.items[0]
               .as.relation_query.relation.bytes + 12u,
           "Add", 3u);

    mutated_call = find_head(
        &target.rewrites[3].premises.items[1]
             .as.relation_query.arguments.items[2],
        "external-call:call-binary");
    (void)expect(&counts, mutated_call != NULL,
                 "locate target call semantics for mutation control");
    if (mutated_call) {
        mutated_call->as.apply.head.bytes[0] = 'x';
        (void)expect(
            &counts,
            !cetta_exact_arithmetic_to_external_call_transform_v1(
                &candidate, &source, &target, &status,
                error, sizeof(error)) &&
                status ==
                    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_TARGET_V1,
            "target semantic mutation is rejected rather than ignored");
        mutated_call->as.apply.head.bytes[0] = 'c';
    }

    baseline.entries[0].target_program.as.apply.head.bytes[0] = 'x';
    (void)expect(
        &counts,
        !cetta_exact_arithmetic_external_call_program_v1_inspect(
            &target, &baseline.entries[0].target_program, &view),
        "mutated target Pattern is not accepted by the C emitter view");
        baseline.entries[0].target_program.as.apply.head.bytes[0] = 'c';
    }

    cetta_exact_arithmetic_external_call_transform_v1_free(&candidate);
    cetta_exact_arithmetic_external_call_transform_v1_free(&baseline);
    cetta_language_def_core_v1_free(&target);
    cetta_language_def_core_v1_free(&source);
    cetta_op_lang_v1_free(&target_wire);
    cetta_op_lang_v1_free(&source_wire);
    printf("(ExactArithmeticExternalCallTransformSummary passed=%u failed=%u)\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
