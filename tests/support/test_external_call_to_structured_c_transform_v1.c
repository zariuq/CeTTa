#include "native/external_call_to_structured_c_transform_v1.h"
#include "native/operational_language_def_v1.h"
#include "native/structured_c_emitter_v1.h"

#include <stdio.h>
#include <stdlib.h>
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

static bool text_is(const CettaLdTextV1 *text, const char *literal) {
    size_t len = literal ? strlen(literal) : 0u;

    return text && literal && text->len == len &&
        (len == 0u || memcmp(text->bytes, literal, len) == 0);
}

static bool replace_text_fragment(CettaLdTextV1 *text,
                                  const char *old_fragment,
                                  const char *new_fragment) {
    size_t old_len = strlen(old_fragment);
    size_t new_len = strlen(new_fragment);
    uint32_t index;

    if (!text || old_len != new_len || old_len > text->len)
        return false;
    for (index = 0u; index + old_len <= text->len; index++) {
        if (memcmp(text->bytes + index, old_fragment, old_len) == 0) {
            memcpy(text->bytes + index, new_fragment, new_len);
            return true;
        }
    }
    return false;
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

static char *emit_program(const CettaLanguageDefCoreV1 *language,
                          const CettaLdPatternV1 *program,
                          CettaStructuredCEmitStatusV1 *status,
                          char *error,
                          size_t error_size) {
    FILE *stream = tmpfile();
    long length;
    char *text;

    if (!stream)
        return NULL;
    if (!cetta_structured_c_emit_v1(
            stream, language, program,
            "native/external_call_generated_abi_v1.h",
            status, error, error_size) ||
        fflush(stream) != 0 || fseek(stream, 0L, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    length = ftell(stream);
    if (length < 0 || fseek(stream, 0L, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    text = malloc((size_t)length + 1u);
    if (!text) {
        fclose(stream);
        return NULL;
    }
    if (fread(text, 1u, (size_t)length, stream) != (size_t)length) {
        free(text);
        fclose(stream);
        return NULL;
    }
    text[length] = '\0';
    fclose(stream);
    return text;
}

static CettaLdGrammarRuleV1 *find_term(CettaLanguageDefCoreV1 *language,
                                       const char *label) {
    uint32_t index;

    for (index = 0u; index < language->term_len; index++) {
        if (text_is(&language->terms[index].label, label))
            return &language->terms[index];
    }
    return NULL;
}

static CettaLdPatternV1 *find_apply_head(CettaLdPatternV1 *pattern,
                                        const char *head) {
    uint32_t index;
    CettaLdPatternV1 *found;

    if (!pattern || !head || pattern->kind != CETTA_LD_PATTERN_APPLY_V1)
        return NULL;
    if (text_is(&pattern->as.apply.head, head))
        return pattern;
    for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
        found = find_apply_head(&pattern->as.apply.arguments.items[index], head);
        if (found)
            return found;
    }
    return NULL;
}

int main(void) {
    TestCounts counts = {0};
    CettaOperationalLanguageDefV1 source_wire;
    CettaOperationalLanguageDefV1 external_wire;
    CettaOperationalLanguageDefV1 structured_wire;
    CettaLanguageDefCoreV1 source;
    CettaLanguageDefCoreV1 external;
    CettaLanguageDefCoreV1 structured;
    CettaExactArithmeticExternalCallTransformV1 external_transform;
    CettaExternalCallStructuredCTransformV1 baseline;
    CettaExternalCallStructuredCTransformV1 mutation;
    CettaExactArithmeticExternalCallTransformStatusV1 external_status =
        CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1;
    CettaExternalCallStructuredCTransformStatusV1 transform_status =
        CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_OK_V1;
    CettaStructuredCEmitStatusV1 emit_status = CETTA_STRUCTURED_C_EMIT_OK_V1;
    CettaExactArithmeticExternalCallProgramViewV1 view;
    CettaLdGrammarRuleV1 *program_term;
    char error[512] = {0};
    char *baseline_c = NULL;
    char *mutation_c = NULL;
    bool loaded;

    cetta_op_lang_v1_init(&source_wire);
    cetta_op_lang_v1_init(&external_wire);
    cetta_op_lang_v1_init(&structured_wire);
    cetta_language_def_core_v1_init(&source);
    cetta_language_def_core_v1_init(&external);
    cetta_language_def_core_v1_init(&structured);
    cetta_exact_arithmetic_external_call_transform_v1_init(&external_transform);
    cetta_external_call_structured_c_transform_v1_init(&baseline);
    cetta_external_call_structured_c_transform_v1_init(&mutation);
    loaded = load_language("langdef/arithmetic/exact_arithmetic_v1.metta",
                           &source_wire, &source, error, sizeof(error)) &&
        load_language("langdef/machines/external_call_machine_v1.metta",
                      &external_wire, &external, error, sizeof(error)) &&
        load_language("langdef/structured-c/structured_c_v1.metta",
                      &structured_wire, &structured, error, sizeof(error));
    (void)expect(&counts, loaded,
                 error[0] ? error : "load all three authored GSLTs");
    if (loaded) {
        bool first_stage =
            cetta_exact_arithmetic_to_external_call_transform_v1(
                &external_transform, &source, &external, &external_status,
                error, sizeof(error));
        bool second_stage = first_stage &&
            cetta_external_call_to_structured_c_transform_v1(
                &baseline, &external, &structured, &external_transform,
                &transform_status, error, sizeof(error));
        (void)expect(&counts, second_stage &&
                baseline.target_program.kind == CETTA_LD_PATTERN_APPLY_V1 &&
                text_is(&baseline.target_program.as.apply.head,
                        "structured-c:program"),
            error[0] ? error :
                "ExternalCallMachine Patterns lower to an actual StructuredC Program");
        if (second_stage) {
            baseline_c = emit_program(&structured, &baseline.target_program,
                                      &emit_status, error, sizeof(error));
            (void)expect(&counts, baseline_c &&
                    strstr(baseline_c,
                        "cetta_generated_exact_integer_add_v1") &&
                    strstr(baseline_c,
                        "cetta_external_call_exact_integer_add_v1") &&
                    strstr(baseline_c, "break;") &&
                    !strstr(baseline_c, "goto"),
                error[0] ? error :
                    "the StructuredC Program emits non-fallthrough ordinary C without goto");
        }

        if (first_stage &&
            cetta_exact_arithmetic_external_call_program_v1_inspect(
                &external, &external_transform.entries[0].target_program,
                &view) &&
            replace_text_fragment((CettaLdTextV1 *)(uintptr_t)view.provider_link,
                                  "add", "sub")) {
            bool lowered = cetta_external_call_to_structured_c_transform_v1(
                &mutation, &external, &structured, &external_transform,
                &transform_status, error, sizeof(error));
            mutation_c = lowered
                ? emit_program(&structured, &mutation.target_program,
                               &emit_status, error, sizeof(error))
                : NULL;
            (void)expect(&counts, mutation_c && baseline_c &&
                    strcmp(mutation_c, baseline_c) != 0 &&
                    !strstr(mutation_c,
                        "cetta_external_call_exact_integer_add_v1(first") &&
                    strstr(mutation_c,
                        "cetta_external_call_exact_integer_sub_v1(first"),
                "mutating a source provider Pattern changes the StructuredC artifact");
        } else {
            (void)expect(&counts, false,
                "locate and mutate the source provider Pattern");
        }

        program_term = find_term(&structured, "structured-c:program");
        if (program_term && program_term->label.len > 0u) {
            uint8_t saved = program_term->label.bytes[0];
            program_term->label.bytes[0] = (uint8_t)'X';
            (void)expect(&counts,
                !cetta_external_call_to_structured_c_transform_v1(
                    &mutation, &external, &structured, &external_transform,
                    &transform_status, error, sizeof(error)) &&
                    transform_status ==
                        CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_TARGET_V1,
                "a mutated target constructor rejects the lowering");
            program_term->label.bytes[0] = saved;
        } else {
            (void)expect(&counts, false,
                "locate the StructuredC Program constructor");
        }

        if (structured.rewrite_len > 0u) {
            CettaLdPatternV1 *outcome = find_apply_head(
                &structured.rewrites[0].right,
                "structured-c:outcome-fallthrough");

            if (outcome && outcome->as.apply.head.len > 0u) {
                uint8_t saved = outcome->as.apply.head.bytes[0];
                outcome->as.apply.head.bytes[0] = (uint8_t)'X';
                (void)expect(&counts,
                    !cetta_external_call_to_structured_c_transform_v1(
                        &mutation, &external, &structured, &external_transform,
                        &transform_status, error, sizeof(error)) &&
                    transform_status ==
                        CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_TARGET_V1,
                    "mutating StructuredC operational semantics rejects the lowering");
                outcome->as.apply.head.bytes[0] = saved;
            } else {
                (void)expect(&counts, false,
                    "locate the StructuredC fallthrough outcome in its semantics");
            }

            if (structured.rewrites[0].name.len > 0u) {
                uint8_t saved = structured.rewrites[0].name.bytes[0];
                structured.rewrites[0].name.bytes[0] = (uint8_t)'X';
                (void)expect(&counts,
                    cetta_external_call_to_structured_c_transform_v1(
                        &mutation, &external, &structured, &external_transform,
                        &transform_status, error, sizeof(error)),
                    "renaming a StructuredC rule preserves the lowering");
                structured.rewrites[0].name.bytes[0] = saved;
            } else {
                (void)expect(&counts, false,
                    "locate the StructuredC rule identity for rename control");
            }
        }

        if (baseline.target_program.kind == CETTA_LD_PATTERN_APPLY_V1 &&
            baseline.target_program.as.apply.head.len > 0u) {
            uint8_t saved = baseline.target_program.as.apply.head.bytes[0];
            FILE *sink = tmpfile();
            baseline.target_program.as.apply.head.bytes[0] = (uint8_t)'X';
            (void)expect(&counts, sink &&
                    !cetta_structured_c_emit_v1(
                        sink, &structured, &baseline.target_program,
                        "native/external_call_generated_abi_v1.h",
                        &emit_status, error, sizeof(error)) &&
                    emit_status == CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_PROGRAM_V1,
                "the emitter rejects a non-StructuredC Program Pattern");
            baseline.target_program.as.apply.head.bytes[0] = saved;
            if (sink)
                fclose(sink);
        }
    }

    free(mutation_c);
    free(baseline_c);
    cetta_external_call_structured_c_transform_v1_free(&mutation);
    cetta_external_call_structured_c_transform_v1_free(&baseline);
    cetta_exact_arithmetic_external_call_transform_v1_free(&external_transform);
    cetta_language_def_core_v1_free(&structured);
    cetta_language_def_core_v1_free(&external);
    cetta_language_def_core_v1_free(&source);
    cetta_op_lang_v1_free(&structured_wire);
    cetta_op_lang_v1_free(&external_wire);
    cetta_op_lang_v1_free(&source_wire);
    printf("(ExternalCallStructuredCTransformSummary passed=%u failed=%u)\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
