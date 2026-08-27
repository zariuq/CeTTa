#include "native/radix_digit_reference_evaluator_v1.h"
#include "native/walters_zantema_da_to_radix_digit_transform_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int digit_value(unsigned char character) {
    if (character >= '0' && character <= '9')
        return (int)(character - '0');
    if (character >= 'A' && character <= 'F')
        return (int)(character - 'A') + 10;
    if (character >= 'a' && character <= 'f')
        return (int)(character - 'a') + 10;
    return -1;
}

static bool read_digits(
    const char *text,
    uint32_t radix,
    uint32_t **digits_out,
    uint32_t *digit_len_out) {
    size_t length = strlen(text);
    size_t index;
    uint32_t *digits;
    if (length == 1u && text[0] == '0') {
        *digits_out = NULL;
        *digit_len_out = 0u;
        return true;
    }
    if (length == 0u || text[0] == '0' || length > UINT32_MAX)
        return false;
    digits = malloc(length * sizeof(*digits));
    if (!digits)
        return false;
    for (index = 0u; index < length; ++index) {
        int digit = digit_value((unsigned char)text[length - index - 1u]);
        if (digit < 0 || (uint32_t)digit >= radix) {
            free(digits);
            return false;
        }
        digits[index] = (uint32_t)digit;
    }
    *digits_out = digits;
    *digit_len_out = (uint32_t)length;
    return true;
}

static char digit_character(uint32_t digit) {
    return digit < 10u ? (char)('0' + digit) : (char)('A' + digit - 10u);
}

static void print_result(
    const CettaRadixDigitV1RunResult *result,
    bool show_receipt) {
    uint32_t index;
    fputs("value:", stdout);
    if (result->digit_len == 0u)
        fputc('0', stdout);
    else
        for (index = result->digit_len; index > 0u; --index)
            fputc(digit_character(result->digits[index - 1u]), stdout);
    fputc('\n', stdout);
    if (!show_receipt)
        return;
    printf("steps:%u\n", result->steps);
    for (index = 0u; index < result->event_len; ++index) {
        const CettaRadixDigitV1Event *event = &result->events[index];
        uint32_t source_index;
        if (event->kind == CETTA_RADIX_DIGIT_V1_EVENT_EXECUTE) {
            printf("event:execute:%u\n", event->pc);
        } else if (event->kind == CETTA_RADIX_DIGIT_V1_EVENT_TABLE_ROW) {
            printf("event:table-row:%u:%u:%u:", event->pc,
                event->table_index, event->row_index);
            for (source_index = 0u;
                    source_index < event->source_rule_len; ++source_index)
                printf("%s%u", source_index == 0u ? "" : ",",
                    event->source_rule_indices[source_index]);
            fputc('\n', stdout);
        }
    }
}

int main(int argc, char **argv) {
    CettaOperationalLanguageDefV1 source;
    CettaOperationalLanguageDefV1 target;
    CettaWaltersZantemaDaRadixDigitV1Program compiled;
    CettaRadixDigitV1RunResult run;
    CettaOpLangV1Status parse_status = CETTA_OP_LANG_V1_OK;
    CettaWaltersZantemaDaRadixDigitV1Status transform_status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK;
    const CettaRadixDigitV1Program *program;
    uint32_t *first = NULL;
    uint32_t *second = NULL;
    uint32_t first_len = 0u;
    uint32_t second_len = 0u;
    bool show_receipt;
    char error[512];
    int result = 1;

    show_receipt = argc == 7 && strcmp(argv[6], "--receipt") == 0;
    if ((argc != 6 && !show_receipt) ||
            (strcmp(argv[3], "add") != 0 &&
                strcmp(argv[3], "multiply") != 0)) {
        fprintf(stderr, "usage: %s SOURCE_GSLT TARGET_GSLT "
            "add|multiply FIRST SECOND [--receipt]\n",
            argc > 0 ? argv[0] :
                "cetta-walters-zantema-da-radix-digit-reference-v1");
        return 2;
    }
    cetta_op_lang_v1_init(&source);
    cetta_op_lang_v1_init(&target);
    cetta_walters_zantema_da_radix_digit_v1_program_init(&compiled);
    cetta_radix_digit_v1_run_result_init(&run);
    if (!cetta_op_lang_v1_parse_file(&source, argv[1], 4000000u, 4000000u,
            &parse_status, error, sizeof(error)) ||
            !cetta_op_lang_v1_parse_file(&target, argv[2], 4000000u, 4000000u,
                &parse_status, error, sizeof(error))) {
        fprintf(stderr, "presentation parse failed (%s): %s\n",
            cetta_op_lang_v1_status_name(parse_status), error);
        goto done;
    }
    if (!cetta_walters_zantema_da_radix_digit_v1_transform(&compiled, &source, &target,
            &transform_status, error, sizeof(error))) {
        fprintf(stderr, "transformation failed (%s): %s\n",
            cetta_walters_zantema_da_radix_digit_v1_status_name(transform_status), error);
        goto done;
    }
    if (!read_digits(argv[4], compiled.radix, &first, &first_len) ||
            !read_digits(argv[5], compiled.radix, &second, &second_len)) {
        fputs("invalid input\n", stderr);
        result = 2;
        goto done;
    }
    program = strcmp(argv[3], "add") == 0 ?
        &compiled.addition_program : &compiled.multiplication_program;
    if (!cetta_radix_digit_v1_execute(&run, compiled.radix, &compiled.target_profile,
            program, first, first_len, second, second_len,
            first_len + second_len + 4u,
            64u * (first_len + 1u) * (second_len + 1u)) ||
            run.kind != CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE) {
        fprintf(stderr, "fault:%u\n", (unsigned)run.kind);
        result = 3;
        goto done;
    }
    print_result(&run, show_receipt);
    result = 0;

done:
    free(first);
    free(second);
    cetta_radix_digit_v1_run_result_free(&run);
    cetta_walters_zantema_da_radix_digit_v1_program_free(&compiled);
    cetta_op_lang_v1_free(&source);
    cetta_op_lang_v1_free(&target);
    return result;
}
