#include "native/walters_zantema_da_to_radix_digit_transform_v1.h"
#include "native/radix_digit_reference_evaluator_v1.h"
#include "native/language_def_premise_free_rewriter_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(condition) do { \
    if (condition) { \
        ++passed; \
    } else { \
        ++failed; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
            fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)size + 1u);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    bytes[size] = 0u;
    *length = (size_t)size;
    return bytes;
}

static uint8_t *replace_once(
    const uint8_t *source,
    size_t source_len,
    const char *old_text,
    const char *new_text,
    size_t *result_len) {
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);
    const uint8_t *position = NULL;
    size_t index;
    uint8_t *result;
    size_t prefix_len;

    if (old_len == 0u || source_len < old_len)
        return NULL;
    for (index = 0u; index + old_len <= source_len; ++index) {
        if (memcmp(source + index, old_text, old_len) == 0) {
            position = source + index;
            break;
        }
    }
    if (!position)
        return NULL;
    prefix_len = (size_t)(position - source);
    *result_len = source_len - old_len + new_len;
    result = malloc(*result_len + 1u);
    if (!result)
        return NULL;
    memcpy(result, source, prefix_len);
    memcpy(result + prefix_len, new_text, new_len);
    memcpy(result + prefix_len + new_len,
        source + prefix_len + old_len,
        source_len - prefix_len - old_len);
    result[*result_len] = 0u;
    return result;
}

static uint8_t *replace_last_once(
    const uint8_t *source,
    size_t source_len,
    const char *old_text,
    const char *new_text,
    size_t *result_len) {
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);
    const uint8_t *position = NULL;
    size_t index;
    uint8_t *result;
    size_t prefix_len;

    if (old_len == 0u || source_len < old_len)
        return NULL;
    for (index = 0u; index + old_len <= source_len; ++index)
        if (memcmp(source + index, old_text, old_len) == 0)
            position = source + index;
    if (!position)
        return NULL;
    prefix_len = (size_t)(position - source);
    *result_len = source_len - old_len + new_len;
    result = malloc(*result_len + 1u);
    if (!result)
        return NULL;
    memcpy(result, source, prefix_len);
    memcpy(result + prefix_len, new_text, new_len);
    memcpy(result + prefix_len + new_len,
        source + prefix_len + old_len,
        source_len - prefix_len - old_len);
    result[*result_len] = 0u;
    return result;
}

static bool parse_bytes(
    CettaOperationalLanguageDefV1 *language,
    const uint8_t *bytes,
    size_t length) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_OK;
    char error[256];
    return cetta_op_lang_v1_parse_bytes(language, bytes, length,
        4000000u, 4000000u, &status, error, sizeof(error));
}

static bool tables_behave_equal(
    const CettaWaltersZantemaDaRadixDigitV1Table *left,
    const CettaWaltersZantemaDaRadixDigitV1Table *right) {
    uint32_t index;
    if (left->row_len != right->row_len)
        return false;
    for (index = 0u; index < left->row_len; ++index) {
        const CettaWaltersZantemaDaRadixDigitV1TableRow *a = &left->rows[index];
        const CettaWaltersZantemaDaRadixDigitV1TableRow *b = &right->rows[index];
        if (a->input_len != b->input_len || a->output_len != b->output_len ||
                memcmp(a->inputs, b->inputs,
                    a->input_len * sizeof(*a->inputs)) != 0 ||
                memcmp(a->outputs, b->outputs,
                    a->output_len * sizeof(*a->outputs)) != 0)
            return false;
    }
    return true;
}

static bool result_digits_are(
    const CettaRadixDigitV1RunResult *result,
    const uint32_t *digits,
    uint32_t digit_len) {
    return result->kind == CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE &&
        result->digit_len == digit_len &&
        (digit_len == 0u ||
            memcmp(result->digits, digits, digit_len * sizeof(*digits)) == 0);
}

static bool append_text(
    char *buffer,
    size_t capacity,
    size_t *length,
    const char *format,
    ...) {
    va_list arguments;
    int written;
    if (*length >= capacity)
        return false;
    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *length)
        return false;
    *length += (size_t)written;
    return true;
}

static bool render_da_numeral(
    char *buffer,
    size_t capacity,
    size_t *length,
    uint32_t value) {
    if (value == 0u)
        return append_text(buffer, capacity, length, "(da:empty)");
    return append_text(buffer, capacity, length, "(da:digit:%u ", value % 2u) &&
        render_da_numeral(buffer, capacity, length, value / 2u) &&
        append_text(buffer, capacity, length, ")");
}

static bool render_da_operation(
    char *buffer,
    size_t capacity,
    const char *operation,
    uint32_t first,
    uint32_t second) {
    size_t length = 0u;
    return append_text(buffer, capacity, &length, "(%s ", operation) &&
        render_da_numeral(buffer, capacity, &length, first) &&
        append_text(buffer, capacity, &length, " ") &&
        render_da_numeral(buffer, capacity, &length, second) &&
        append_text(buffer, capacity, &length, ")");
}

static bool decode_da_numeral(
    const CettaOpLangV1SExpr *term,
    uint64_t *value) {
    uint64_t body;
    uint32_t digit;
    if (cetta_op_lang_v1_application_is(term, "da:empty", 0u)) {
        *value = 0u;
        return true;
    }
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
            term->as.application.argument_len != 1u ||
            !term->as.application.head)
        return false;
    if (strcmp(term->as.application.head, "da:digit:0") == 0)
        digit = 0u;
    else if (strcmp(term->as.application.head, "da:digit:1") == 0)
        digit = 1u;
    else
        return false;
    if (!decode_da_numeral(term->as.application.arguments[0], &body) ||
            body > (UINT64_MAX - digit) / 2u)
        return false;
    *value = body * 2u + digit;
    return true;
}

static uint32_t encode_radix_digit_digits(
    uint32_t value,
    uint32_t digits[32]) {
    uint32_t length = 0u;
    while (value != 0u) {
        digits[length++] = value % 2u;
        value /= 2u;
    }
    return length;
}

static bool decode_radix_digit_digits(
    const CettaRadixDigitV1RunResult *result,
    uint64_t *value) {
    uint64_t decoded = 0u;
    uint32_t index;
    if (result->kind != CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE || result->digit_len > 63u)
        return false;
    for (index = result->digit_len; index > 0u; --index) {
        if (result->digits[index - 1u] > 1u ||
                decoded > (UINT64_MAX - result->digits[index - 1u]) / 2u)
            return false;
        decoded = decoded * 2u + result->digits[index - 1u];
    }
    *value = decoded;
    return true;
}

static bool parse_term_document(
    CettaOpLangV1Document *document,
    const char *source) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_OK;
    char error[256];
    return cetta_op_lang_v1_parse_document_bytes(document,
        (const uint8_t *)source, strlen(source), 1000000u, 2000000u,
        &status, error, sizeof(error));
}

static bool cross_qualify_case(
    const CettaLdPfrV1Program *rewriter,
    const CettaWaltersZantemaDaRadixDigitV1Program *program,
    bool multiply,
    uint32_t first,
    uint32_t second,
    uint64_t *rewrite_steps,
    uint64_t *machine_steps) {
    char term_source[4096];
    CettaOpLangV1Document term;
    CettaLdPfrV1Result normalized;
    CettaLdPfrV1Status rewrite_status = CETTA_LD_PFR_V1_OK;
    CettaRadixDigitV1RunResult run;
    const CettaRadixDigitV1Program *target_program = multiply ?
        &program->multiplication_program : &program->addition_program;
    uint32_t first_digits[32];
    uint32_t second_digits[32];
    uint32_t first_len = encode_radix_digit_digits(first, first_digits);
    uint32_t second_len = encode_radix_digit_digits(second, second_digits);
    uint64_t expected = multiply ?
        (uint64_t)first * second : (uint64_t)first + second;
    uint64_t rewrite_value = 0u;
    uint64_t machine_value = 0u;
    char error[256];
    bool ok = false;

    cetta_op_lang_v1_document_init(&term);
    cetta_ld_pfr_v1_result_init(&normalized);
    cetta_radix_digit_v1_run_result_init(&run);
    if (!render_da_operation(term_source, sizeof(term_source),
            multiply ? "da:mul" : "da:add", first, second) ||
            !parse_term_document(&term, term_source) ||
            !cetta_ld_pfr_v1_normalize(rewriter, term.root, 200000u,
                &normalized, &rewrite_status, error, sizeof(error)) ||
            !decode_da_numeral(normalized.normal_form, &rewrite_value) ||
            !cetta_radix_digit_v1_execute(&run, program->radix,
                &program->target_profile, target_program,
                first_digits, first_len, second_digits, second_len,
                32u, 200000u) ||
            !decode_radix_digit_digits(&run, &machine_value))
        goto done;
    *rewrite_steps += normalized.rule_len;
    *machine_steps += run.steps;
    ok = rewrite_value == expected && machine_value == expected &&
        run.event_len >= run.steps && run.steps > 0u;

done:
    cetta_radix_digit_v1_run_result_free(&run);
    cetta_ld_pfr_v1_result_free(&normalized);
    cetta_op_lang_v1_document_free(&term);
    return ok;
}

static bool cross_qualify_presentations(
    const CettaOperationalLanguageDefV1 *source,
    const CettaWaltersZantemaDaRadixDigitV1Program *program,
    uint32_t *case_count,
    uint64_t *rewrite_steps,
    uint64_t *machine_steps) {
    static const uint32_t boundaries[][2] = {
        {0u, 0u}, {0u, 1u}, {1u, 0u}, {1u, 1u},
        {3u, 1u}, {7u, 1u}, {15u, 1u}, {31u, 1u},
        {31u, 31u}, {42u, 17u}
    };
    CettaLdPfrV1Program rewriter;
    CettaLdPfrV1Status status = CETTA_LD_PFR_V1_OK;
    char error[256];
    uint32_t state = 0x5eed1234u;
    uint32_t index;
    bool ok = true;

    cetta_ld_pfr_v1_program_init(&rewriter);
    if (!cetta_ld_pfr_v1_compile(&rewriter, source,
            &status, error, sizeof(error))) {
        cetta_ld_pfr_v1_program_free(&rewriter);
        return false;
    }
    for (index = 0u; index < sizeof(boundaries) / sizeof(boundaries[0]); ++index) {
        ok = cross_qualify_case(&rewriter, program, false,
                boundaries[index][0], boundaries[index][1],
                rewrite_steps, machine_steps) && ok;
        ok = cross_qualify_case(&rewriter, program, true,
                boundaries[index][0], boundaries[index][1],
                rewrite_steps, machine_steps) && ok;
        *case_count += 2u;
    }
    for (index = 0u; index < 128u; ++index) {
        uint32_t first;
        uint32_t second;
        state = state * 1664525u + 1013904223u;
        first = (state >> 8u) & 63u;
        state = state * 1664525u + 1013904223u;
        second = (state >> 8u) & 63u;
        ok = cross_qualify_case(&rewriter, program, false, first, second,
                rewrite_steps, machine_steps) && ok;
        ok = cross_qualify_case(&rewriter, program, true, first, second,
                rewrite_steps, machine_steps) && ok;
        *case_count += 2u;
    }
    cetta_ld_pfr_v1_program_free(&rewriter);
    return ok;
}

int main(void) {
    const char *source_path =
        "langdef/arithmetic/walters_zantema_da_radix2_v1.metta";
    const char *target_path = "langdef/machines/radix_digit_machine_v1.metta";
    size_t source_len = 0u;
    size_t target_len = 0u;
    uint8_t *source_bytes = read_file(source_path, &source_len);
    uint8_t *target_bytes = read_file(target_path, &target_len);
    CettaOperationalLanguageDefV1 source;
    CettaOperationalLanguageDefV1 target;
    CettaWaltersZantemaDaRadixDigitV1Program program;
    CettaWaltersZantemaDaRadixDigitV1Status status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK;
    char error[256];
    uint32_t inputs[4];
    uint32_t row_index;
    const CettaWaltersZantemaDaRadixDigitV1TableRow *row;
    CettaRadixDigitV1RunResult run;
    uint32_t cross_case_count = 0u;
    uint64_t cross_rewrite_steps = 0u;
    uint64_t cross_machine_steps = 0u;

    cetta_radix_digit_v1_run_result_init(&run);

    cetta_op_lang_v1_init(&source);
    cetta_op_lang_v1_init(&target);
    cetta_walters_zantema_da_radix_digit_v1_program_init(&program);
    CHECK(source_bytes != NULL);
    CHECK(target_bytes != NULL);
    if (!source_bytes || !target_bytes)
        goto done;
    CHECK(parse_bytes(&source, source_bytes, source_len));
    CHECK(parse_bytes(&target, target_bytes, target_len));
    CHECK(cetta_walters_zantema_da_radix_digit_v1_transform(&program, &source, &target,
        &status, error, sizeof(error)));
    CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK);
    CHECK(program.radix == 2u);
    CHECK(program.addition_program.table_len == 1u);
    CHECK(program.multiplication_program.table_len == 1u);
    CHECK(program.addition_program.tables[0].row_len == 8u);
    CHECK(program.multiplication_program.tables[0].row_len == 16u);
    CHECK(program.addition_program.instruction_len == 17u);
    CHECK(program.multiplication_program.instruction_len == 20u);
    {
        CettaRadixDigitV1InstructionKind kind = CETTA_RADIX_DIGIT_V1_SET;
        CHECK(cetta_radix_digit_v1_target_constructor(&program.target_profile,
            program.addition_program.instructions[13].constructor_term_index,
            &kind));
        CHECK(kind == CETTA_RADIX_DIGIT_V1_LOOKUP);
        CHECK(cetta_radix_digit_v1_target_constructor(&program.target_profile,
            program.multiplication_program.instructions[11].constructor_term_index,
            &kind));
        CHECK(kind == CETTA_RADIX_DIGIT_V1_LOOKUP);
    }
    {
        CettaRadixDigitV1ProgramWire wire;
        CettaRadixDigitV1Program decoded;
        uint8_t saved;
        cetta_radix_digit_v1_program_wire_init(&wire);
        cetta_radix_digit_v1_program_init(&decoded);
        CHECK(cetta_radix_digit_v1_program_encode(&wire, &program.target_profile,
            &program.addition_program));
        CHECK(cetta_radix_digit_v1_program_decode(&decoded, &program.target_profile,
            wire.bytes, wire.len));
        CHECK(cetta_radix_digit_v1_program_equal(
            &decoded, &program.addition_program));
        if (wire.len > 52u) {
            saved = wire.bytes[52u];
            wire.bytes[52u] = 0xffu;
            CHECK(!cetta_radix_digit_v1_program_decode(&decoded,
                &program.target_profile, wire.bytes, wire.len));
            wire.bytes[52u] = saved;
            saved = wire.bytes[4u];
            wire.bytes[4u] ^= 1u;
            CHECK(!cetta_radix_digit_v1_program_decode(&decoded,
                &program.target_profile, wire.bytes, wire.len));
            wire.bytes[4u] = saved;
        }
        cetta_radix_digit_v1_program_free(&decoded);
        cetta_radix_digit_v1_program_wire_free(&wire);
    }
    {
        CettaRadixDigitV1ProgramWire wire;
        CettaRadixDigitV1Program changed;
        const uint32_t seven[] = {1u, 1u, 1u};
        const uint32_t one[] = {1u};
        const uint32_t eight[] = {0u, 0u, 0u, 1u};
        cetta_radix_digit_v1_program_wire_init(&wire);
        cetta_radix_digit_v1_program_init(&changed);
        CHECK(cetta_radix_digit_v1_program_encode(&wire, &program.target_profile,
            &program.addition_program));
        CHECK(cetta_radix_digit_v1_program_decode(&changed, &program.target_profile,
            wire.bytes, wire.len));
        changed.instructions[0].constructor_term_index =
            program.target_profile.instruction_term_indices[CETTA_RADIX_DIGIT_V1_SET];
        CHECK(cetta_radix_digit_v1_program_valid(&program.target_profile, &changed));
        CHECK(cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &changed,
            seven, 3u, one, 1u, 16u, 256u));
        CHECK(!result_digits_are(&run, eight, 4u));
        changed.instructions[0].constructor_term_index = UINT32_MAX;
        CHECK(!cetta_radix_digit_v1_program_valid(&program.target_profile, &changed));
        CHECK(!cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &changed,
            seven, 3u, one, 1u, 16u, 256u));
        cetta_radix_digit_v1_program_free(&changed);
        cetta_radix_digit_v1_program_wire_free(&wire);
    }
    CHECK(cross_qualify_presentations(&source, &program,
        &cross_case_count, &cross_rewrite_steps, &cross_machine_steps));
    CHECK(cross_case_count == 276u && cross_rewrite_steps > 0u &&
        cross_machine_steps > 0u);

    inputs[0] = 1u;
    inputs[1] = 1u;
    inputs[2] = 0u;
    row = cetta_radix_digit_v1_find_row(
        &program.addition_program.tables[0], inputs, 3u, &row_index);
    CHECK(row != NULL);
    if (row) {
        CHECK(row->outputs[0] == 0u && row->outputs[1] == 1u);
        CHECK(row->source_rule_len == 1u && row->source_rule_indices[0] == 6u);
    }
    inputs[2] = 1u;
    row = cetta_radix_digit_v1_find_row(
        &program.addition_program.tables[0], inputs, 3u, &row_index);
    CHECK(row != NULL && row->outputs[0] == 1u && row->outputs[1] == 1u);

    inputs[0] = 1u;
    inputs[1] = 1u;
    inputs[2] = 0u;
    inputs[3] = 0u;
    row = cetta_radix_digit_v1_find_row(
        &program.multiplication_program.tables[0], inputs, 4u, &row_index);
    CHECK(row != NULL && row->outputs[0] == 1u && row->outputs[1] == 0u);
    if (row) {
        CHECK(row->source_rule_len >= 2u);
        CHECK(row->source_rule_indices[0] == 9u);
        CHECK(row->source_rule_indices[1] == 18u);
    }

    {
        const uint32_t seven[] = {1u, 1u, 1u};
        const uint32_t one[] = {1u};
        const uint32_t eight[] = {0u, 0u, 0u, 1u};
        CHECK(cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &program.addition_program,
            seven, 3u, one, 1u, 16u, 256u));
        CHECK(result_digits_are(&run, eight, 4u));
        CHECK(run.steps > 0u && run.event_len > run.steps);
        CHECK(run.events[0].kind == CETTA_RADIX_DIGIT_V1_EVENT_EXECUTE);
    }
    {
        const uint32_t seven[] = {1u, 1u, 1u};
        const uint32_t six[] = {0u, 1u, 1u};
        const uint32_t forty_two[] = {0u, 1u, 0u, 1u, 0u, 1u};
        CHECK(cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &program.multiplication_program,
            seven, 3u, six, 3u, 16u, 1024u));
        CHECK(result_digits_are(&run, forty_two, 6u));
    }
    {
        const uint32_t invalid[] = {2u};
        const uint32_t one[] = {1u};
        CHECK(cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &program.addition_program,
            invalid, 1u, one, 1u, 8u, 64u));
        CHECK(run.kind == CETTA_RADIX_DIGIT_V1_OUTCOME_LANGUAGE_FAULT &&
            run.fault == CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT);
        CHECK(run.event_len > 0u &&
            run.events[run.event_len - 1u].kind == CETTA_RADIX_DIGIT_V1_EVENT_FAULT);
    }
    {
        const uint32_t one[] = {1u};
        CHECK(cetta_radix_digit_v1_execute(&run, program.radix,
            &program.target_profile, &program.addition_program,
            one, 1u, one, 1u, 8u, 1u));
        CHECK(run.kind == CETTA_RADIX_DIGIT_V1_OUTCOME_RESOURCE_FAULT &&
            run.fault == CETTA_RADIX_DIGIT_V1_FAULT_FUEL_EXHAUSTED);
    }

    {
        static const char carry_old[] =
            "(PApp \"da:succ\" (LCons (PApp \"da:add\" "
            "(LCons (FVar \"x\") (LCons (FVar \"y\") LNil))) LNil))";
        static const char carry_new[] =
            "(PApp \"da:add\" (LCons (FVar \"x\") "
            "(LCons (FVar \"y\") LNil)))";
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(source_bytes, source_len,
            carry_old, carry_new, &changed_len);
        CettaOperationalLanguageDefV1 changed_source;
        CettaWaltersZantemaDaRadixDigitV1Program changed_program;
        cetta_op_lang_v1_init(&changed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&changed_program);
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_source, changed_bytes, changed_len));
            CHECK(cetta_walters_zantema_da_radix_digit_v1_transform(&changed_program,
                &changed_source, &target, &status, error, sizeof(error)));
            inputs[0] = 1u;
            inputs[1] = 1u;
            inputs[2] = 0u;
            row = cetta_radix_digit_v1_find_row(
                &changed_program.addition_program.tables[0], inputs, 3u, NULL);
            CHECK(row != NULL && row->outputs[0] == 0u && row->outputs[1] == 0u);
            {
                const uint32_t one[] = {1u};
                const uint32_t wrong[] = {0u};
                CHECK(cetta_radix_digit_v1_execute(&run, changed_program.radix,
                    &changed_program.target_profile,
                    &changed_program.addition_program,
                    one, 1u, one, 1u, 8u, 128u));
                CHECK(result_digits_are(&run, wrong, 1u));
            }
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&changed_program);
        cetta_op_lang_v1_free(&changed_source);
        free(changed_bytes);
    }

    {
        size_t renamed_len = 0u;
        uint8_t *renamed_bytes = replace_once(source_bytes, source_len,
            "wz-da:4[radix=2,1,1]", "renamed-carry-rule", &renamed_len);
        CettaOperationalLanguageDefV1 renamed_source;
        CettaWaltersZantemaDaRadixDigitV1Program renamed_program;
        cetta_op_lang_v1_init(&renamed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&renamed_program);
        CHECK(renamed_bytes != NULL);
        if (renamed_bytes) {
            CHECK(parse_bytes(&renamed_source, renamed_bytes, renamed_len));
            CHECK(cetta_walters_zantema_da_radix_digit_v1_transform(&renamed_program,
                &renamed_source, &target, &status, error, sizeof(error)));
            CHECK(tables_behave_equal(
                &program.addition_program.tables[0],
                &renamed_program.addition_program.tables[0]));
            CHECK(tables_behave_equal(
                &program.multiplication_program.tables[0],
                &renamed_program.multiplication_program.tables[0]));
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&renamed_program);
        cetta_op_lang_v1_free(&renamed_source);
        free(renamed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(source_bytes, source_len,
            "(TypeBinding \"x\" (TBase \"Nat\"))",
            "(TypeBinding \"x\" (TBase \"Fault\"))", &changed_len);
        CettaOperationalLanguageDefV1 changed_source;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 66u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_source, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &changed_source, &target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE);
            CHECK(sentinel.radix == 66u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_source);
        free(changed_bytes);
    }

    {
        static const char product_old[] =
            "(PApp \"da:digit:1\" (LCons (PApp \"da:empty\" LNil) LNil))";
        static const char product_new[] =
            "(PApp \"da:digit:0\" (LCons (PApp \"da:empty\" LNil) LNil))";
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_last_once(source_bytes, source_len,
            product_old, product_new, &changed_len);
        CettaOperationalLanguageDefV1 changed_source;
        CettaWaltersZantemaDaRadixDigitV1Program changed_program;
        cetta_op_lang_v1_init(&changed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&changed_program);
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_source, changed_bytes, changed_len));
            CHECK(cetta_walters_zantema_da_radix_digit_v1_transform(&changed_program,
                &changed_source, &target, &status, error, sizeof(error)));
            inputs[0] = 1u;
            inputs[1] = 1u;
            inputs[2] = 0u;
            inputs[3] = 0u;
            row = cetta_radix_digit_v1_find_row(
                &changed_program.multiplication_program.tables[0], inputs, 4u, NULL);
            CHECK(row != NULL && row->outputs[0] == 0u && row->outputs[1] == 0u);
            {
                const uint32_t one[] = {1u};
                const uint32_t wrong[] = {0u};
                CHECK(cetta_radix_digit_v1_execute(&run, changed_program.radix,
                    &changed_program.target_profile,
                    &changed_program.multiplication_program,
                    one, 1u, one, 1u, 8u, 128u));
                CHECK(result_digits_are(&run, wrong, 1u));
            }
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&changed_program);
        cetta_op_lang_v1_free(&changed_source);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            "\"radix-digit:lookup\"", "\"radix-digit:lookup-removed\"", &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 77u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 77u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }


    {
        static const char outcome_old[] =
            "(PApp \"radix-digit:outcome-value\" (LCons (FVar \"digits\") LNil))";
        static const char outcome_new[] =
            "(PApp \"radix-digit:outcome-engine-fault\" (LCons (FVar \"digits\") LNil))";
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            outcome_old, outcome_new, &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 88u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 88u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }


    {
        static const char flow_old[] =
            "(LCons (FVar \"nextBuffers\") "
            "(LCons (FVar \"nextRegisters\")";
        static const char flow_new[] =
            "(LCons (FVar \"nextRegisters\") "
            "(LCons (FVar \"nextBuffers\")";
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_last_once(target_bytes, target_len,
            flow_old, flow_new, &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 99u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 99u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }


    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            "(TypeBinding \"program\" (TBase \"Program\"))",
            "(TypeBinding \"program\" (TBase \"Fault\"))", &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 101u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 101u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(source_bytes, source_len,
            "(GrammarRule \"da:add\" \"Nat\" "
            "(LCons (TermSimple \"left\" (TBase \"Nat\"))",
            "(GrammarRule \"da:add\" \"Nat\" "
            "(LCons (TermSimple \"left\" (TBase \"Fault\"))",
            &changed_len);
        CettaOperationalLanguageDefV1 changed_source;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 102u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_source, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &changed_source, &target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE);
            CHECK(sentinel.radix == 102u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_source);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(source_bytes, source_len,
            "(LCons (SyntaxTerminal \"da:add\") LNil) "
            "(EvalSome EvalRewrite)",
            "(LCons (SyntaxTerminal \"da:add\") LNil) EvalNone",
            &changed_len);
        CettaOperationalLanguageDefV1 changed_source;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_source);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 103u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_source, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &changed_source, &target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE);
            CHECK(sentinel.radix == 103u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_source);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            "(TermSimple \"inputs\" (TBase \"RegisterList\"))",
            "(TermSimple \"inputs\" (TBase \"Nat\"))", &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 104u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 104u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            "(LCons (SyntaxTerminal \"radix-digit:set\") LNil) EvalNone",
            "(LCons (SyntaxTerminal \"radix-digit:set\") LNil) "
            "(EvalSome EvalRewrite)", &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 105u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 105u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }

    {
        size_t changed_len = 0u;
        uint8_t *changed_bytes = replace_once(target_bytes, target_len,
            "(TypeDecl \"Table\" CarrierAst)",
            "(TypeDecl \"TableChanged\" CarrierAst)", &changed_len);
        CettaOperationalLanguageDefV1 changed_target;
        CettaWaltersZantemaDaRadixDigitV1Program sentinel;
        cetta_op_lang_v1_init(&changed_target);
        cetta_walters_zantema_da_radix_digit_v1_program_init(&sentinel);
        sentinel.radix = 106u;
        CHECK(changed_bytes != NULL);
        if (changed_bytes) {
            CHECK(parse_bytes(&changed_target, changed_bytes, changed_len));
            CHECK(!cetta_walters_zantema_da_radix_digit_v1_transform(&sentinel,
                &source, &changed_target, &status, error, sizeof(error)));
            CHECK(status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET);
            CHECK(sentinel.radix == 106u);
        }
        cetta_walters_zantema_da_radix_digit_v1_program_free(&sentinel);
        cetta_op_lang_v1_free(&changed_target);
        free(changed_bytes);
    }

done:
    printf("da-radix-digit-cross-v1: %u cases, %llu DA rewrites, %llu RadixDigit steps\n",
        cross_case_count,
        (unsigned long long)cross_rewrite_steps,
        (unsigned long long)cross_machine_steps);
    cetta_radix_digit_v1_run_result_free(&run);
    cetta_walters_zantema_da_radix_digit_v1_program_free(&program);
    cetta_op_lang_v1_free(&source);
    cetta_op_lang_v1_free(&target);
    free(source_bytes);
    free(target_bytes);
    printf("da-to-radix-digit-transform-v1: %u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
