#include "native/external_call_to_structured_c_transform_v1.h"
#include "native/operational_language_def_v1.h"
#include "native/structured_c_emitter_v1.h"

#include <stdio.h>
#include <stdlib.h>

static bool load_language(const char *path,
                          CettaOperationalLanguageDefV1 *wire,
                          CettaLanguageDefCoreV1 *core,
                          char *error,
                          size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;

    if (!cetta_op_lang_v1_parse_file(
            wire, path, 32000000u, 64000000u, &wire_status,
            error, error_size)) {
        fprintf(stderr, "%s: parse %s: %s\n", path,
                cetta_op_lang_v1_status_name(wire_status),
                error[0] ? error : "no detail");
        return false;
    }
    if (!cetta_language_def_core_v1_decode(
            core, wire, 400000u, &core_status, error, error_size)) {
        fprintf(stderr, "%s: typed decode %s: %s\n", path,
                cetta_ld_core_v1_status_name(core_status),
                error[0] ? error : "no detail");
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    CettaOperationalLanguageDefV1 source_wire;
    CettaOperationalLanguageDefV1 target_wire;
    CettaOperationalLanguageDefV1 structured_c_wire;
    CettaLanguageDefCoreV1 source;
    CettaLanguageDefCoreV1 target;
    CettaLanguageDefCoreV1 structured_c;
    CettaExactArithmeticExternalCallTransformV1 transform;
    CettaExternalCallStructuredCTransformV1 structured_c_transform;
    CettaExactArithmeticExternalCallTransformStatusV1 transform_status =
        CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1;
    CettaExternalCallStructuredCTransformStatusV1 structured_c_status =
        CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_OK_V1;
    CettaStructuredCEmitStatusV1 emit_status = CETTA_STRUCTURED_C_EMIT_OK_V1;
    char error[512] = {0};
    bool ok = false;

    if (argc != 4) {
        fprintf(stderr,
                "usage: %s EXACT_ARITHMETIC_GSLT EXTERNAL_CALL_MACHINE_GSLT STRUCTURED_C_GSLT\n",
                argv[0]);
        return 2;
    }
    cetta_op_lang_v1_init(&source_wire);
    cetta_op_lang_v1_init(&target_wire);
    cetta_op_lang_v1_init(&structured_c_wire);
    cetta_language_def_core_v1_init(&source);
    cetta_language_def_core_v1_init(&target);
    cetta_language_def_core_v1_init(&structured_c);
    cetta_exact_arithmetic_external_call_transform_v1_init(&transform);
    cetta_external_call_structured_c_transform_v1_init(&structured_c_transform);
    ok = load_language(argv[1], &source_wire, &source,
                       error, sizeof(error)) &&
        load_language(argv[2], &target_wire, &target,
                      error, sizeof(error)) &&
        load_language(argv[3], &structured_c_wire, &structured_c,
                      error, sizeof(error));
    if (ok) {
        ok = cetta_exact_arithmetic_to_external_call_transform_v1(
            &transform, &source, &target, &transform_status,
            error, sizeof(error));
        if (!ok) {
            fprintf(stderr, "transform %s: %s\n",
                    cetta_exact_arithmetic_external_call_transform_status_v1_name(
                        transform_status),
                    error[0] ? error : "no detail");
        }
    }
    if (ok) {
        ok = cetta_external_call_to_structured_c_transform_v1(
            &structured_c_transform, &target, &structured_c, &transform,
            &structured_c_status, error, sizeof(error));
        if (!ok) {
            fprintf(stderr, "StructuredC transform %s: %s\n",
                    cetta_external_call_structured_c_transform_status_v1_name(
                        structured_c_status),
                    error[0] ? error : "no detail");
        }
    }
    if (ok) {
        ok = cetta_structured_c_emit_v1(
            stdout, &structured_c, &structured_c_transform.target_program,
            "native/external_call_generated_abi_v1.h", &emit_status,
            error, sizeof(error));
        if (!ok) {
            fprintf(stderr, "emit %s: %s\n",
                    cetta_structured_c_emit_status_v1_name(emit_status),
                    error[0] ? error : "no detail");
        }
    }
    cetta_external_call_structured_c_transform_v1_free(&structured_c_transform);
    cetta_exact_arithmetic_external_call_transform_v1_free(&transform);
    cetta_language_def_core_v1_free(&structured_c);
    cetta_language_def_core_v1_free(&target);
    cetta_language_def_core_v1_free(&source);
    cetta_op_lang_v1_free(&target_wire);
    cetta_op_lang_v1_free(&structured_c_wire);
    cetta_op_lang_v1_free(&source_wire);
    return ok ? 0 : 1;
}
