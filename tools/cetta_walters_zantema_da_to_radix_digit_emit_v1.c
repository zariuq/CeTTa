#include "native/radix_digit_c_emitter_v1.h"
#include "native/walters_zantema_da_to_radix_digit_transform_v1.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    CettaOperationalLanguageDefV1 source;
    CettaOperationalLanguageDefV1 target;
    CettaWaltersZantemaDaRadixDigitV1Program program;
    CettaOpLangV1Status parse_status = CETTA_OP_LANG_V1_OK;
    CettaWaltersZantemaDaRadixDigitV1Status transform_status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK;
    const CettaRadixDigitV1Program *target_program;
    CettaRadixDigitV1Program decoded_program;
    CettaRadixDigitV1ProgramWire wire;
    char error[512];
    int result = 1;

    if (argc != 4 || (strcmp(argv[3], "add") != 0 &&
            strcmp(argv[3], "multiply") != 0)) {
        fprintf(stderr, "usage: %s SOURCE_GSLT TARGET_GSLT add|multiply\n",
            argc > 0 ? argv[0] : "cetta-da-to-radix-digit-emit-v1");
        return 2;
    }
    cetta_op_lang_v1_init(&source);
    cetta_op_lang_v1_init(&target);
    cetta_walters_zantema_da_radix_digit_v1_program_init(&program);
    cetta_radix_digit_v1_program_init(&decoded_program);
    cetta_radix_digit_v1_program_wire_init(&wire);
    if (!cetta_op_lang_v1_parse_file(&source, argv[1], 4000000u, 4000000u,
            &parse_status, error, sizeof(error))) {
        fprintf(stderr, "source parse failed (%s): %s\n",
            cetta_op_lang_v1_status_name(parse_status), error);
        goto done;
    }
    if (!cetta_op_lang_v1_parse_file(&target, argv[2], 4000000u, 4000000u,
            &parse_status, error, sizeof(error))) {
        fprintf(stderr, "target parse failed (%s): %s\n",
            cetta_op_lang_v1_status_name(parse_status), error);
        goto done;
    }
    if (!cetta_walters_zantema_da_radix_digit_v1_transform(&program, &source, &target,
            &transform_status, error, sizeof(error))) {
        fprintf(stderr, "transformation failed (%s): %s\n",
            cetta_walters_zantema_da_radix_digit_v1_status_name(transform_status), error);
        goto done;
    }
    target_program = strcmp(argv[3], "add") == 0 ?
        &program.addition_program : &program.multiplication_program;
    if (!cetta_radix_digit_v1_program_encode(&wire, &program.target_profile,
            target_program) ||
            !cetta_radix_digit_v1_program_decode(&decoded_program,
                &program.target_profile, wire.bytes, wire.len)) {
        fputs("RadixDigit target-program round trip failed\n", stderr);
        goto done;
    }
    if (!cetta_radix_digit_v1_emit_c(stdout, program.radix, &program.target_profile,
            &decoded_program)) {
        fputs("C emission failed\n", stderr);
        goto done;
    }
    result = 0;

done:
    cetta_radix_digit_v1_program_wire_free(&wire);
    cetta_radix_digit_v1_program_free(&decoded_program);
    cetta_walters_zantema_da_radix_digit_v1_program_free(&program);
    cetta_op_lang_v1_free(&source);
    cetta_op_lang_v1_free(&target);
    return result;
}
