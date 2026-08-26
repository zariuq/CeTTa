#include "c_subset_emit_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void c_subset_emit_set_status(CettaCSubsetEmitV1Status *status,
                                     CettaCSubsetEmitV1Status value) {
    if (status)
        *status = value;
}

static void c_subset_emit_set_error(char *buf,
                                    size_t size,
                                    const char *format,
                                    ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool c_subset_emit_identifier_valid(const char *identifier) {
    const unsigned char *cursor = (const unsigned char *)identifier;

    if (!cursor ||
        !(cursor[0] == '_' ||
          (cursor[0] >= 'A' && cursor[0] <= 'Z') ||
          (cursor[0] >= 'a' && cursor[0] <= 'z'))) {
        return false;
    }
    cursor++;
    while (*cursor) {
        if (!(*cursor == '_' ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9'))) {
            return false;
        }
        cursor++;
    }
    return true;
}

static bool c_subset_emit_include_valid(const char *include) {
    const unsigned char *cursor = (const unsigned char *)include;

    if (!cursor || !*cursor)
        return false;
    while (*cursor) {
        if (!(*cursor == '_' || *cursor == '.' || *cursor == '-' ||
              *cursor == '/' ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9'))) {
            return false;
        }
        cursor++;
    }
    return true;
}

static bool c_subset_emit_write(FILE *output, const char *format, ...) {
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

static bool c_subset_emit_validate(const CettaCSubsetV1Module *module,
                                   CettaCSubsetEmitV1Status *status,
                                   char *error_buf,
                                   size_t error_buf_size) {
    CettaCSubsetV1ValidateStatus validation_status;

    if (!cetta_c_subset_v1_validate_module(
            module, 1000000u, &validation_status,
            error_buf, error_buf_size)) {
        c_subset_emit_set_status(
            status, CETTA_C_SUBSET_EMIT_V1_INVALID_TARGET);
        return false;
    }
    return true;
}

bool cetta_c_subset_emit_v1_header(
    FILE *output,
    const CettaCSubsetV1Module *module,
    const char *include_guard,
    CettaCSubsetEmitV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_OK);
    if (!output || !module || !c_subset_emit_identifier_valid(include_guard)) {
        c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_BAD_ARGUMENT);
        c_subset_emit_set_error(
            error_buf, error_buf_size,
            "bad C-subset header emission arguments");
        return false;
    }
    if (!c_subset_emit_validate(
            module, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!c_subset_emit_write(
            output,
            "#ifndef %s\n#define %s\n\n"
            "#include \"native/c_subset_generated_abi_v1.h\"\n\n",
            include_guard, include_guard)) {
        goto io_failure;
    }
    for (index = 0u; index < module->program_len; index++) {
        if (!c_subset_emit_write(
                output,
                "CettaCSubsetGeneratedOutcomeV1 %s(\n"
                "    const CettaCSubsetExactIntegerV1 *first,\n"
                "    const CettaCSubsetExactIntegerV1 *second,\n"
                "    CettaCSubsetExactIntegerV1 *output,\n"
                "    CettaCSubsetGeneratedReceiptV1 *receipt);\n\n",
                module->programs[index].entry_link_name)) {
            goto io_failure;
        }
    }
    if (!c_subset_emit_write(
            output, "#endif /* %s */\n", include_guard) ||
        fflush(output) != 0 || ferror(output)) {
        goto io_failure;
    }
    return true;

io_failure:
    c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_IO_FAILURE);
    c_subset_emit_set_error(
        error_buf, error_buf_size,
        "failed to write generated C-subset header");
    return false;
}

static bool c_subset_emit_external_declarations(
    FILE *output,
    const CettaCSubsetV1Module *module) {
    uint32_t program_index;
    uint32_t previous_index;

    for (program_index = 0u; program_index < module->program_len;
         program_index++) {
        const char *link_name =
            module->programs[program_index].externals[0].link_name;
        bool seen = false;
        for (previous_index = 0u; previous_index < program_index;
             previous_index++) {
            if (strcmp(
                    module->programs[previous_index].externals[0].link_name,
                    link_name) == 0) {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        if (!c_subset_emit_write(
                output,
                "extern CettaCSubsetGeneratedExternalV1 %s(\n"
                "    const CettaCSubsetExactIntegerV1 *first,\n"
                "    const CettaCSubsetExactIntegerV1 *second,\n"
                "    CettaCSubsetExactIntegerV1 *output);\n\n",
                link_name)) {
            return false;
        }
    }
    return true;
}

static const char *c_subset_emit_return_name(
    CettaCSubsetV1InstructionKind kind) {
    switch (kind) {
    case CETTA_C_SUBSET_V1_RETURN_VALUE:
        return "CETTA_C_SUBSET_GENERATED_VALUE_V1";
    case CETTA_C_SUBSET_V1_RETURN_DECLINED:
        return "CETTA_C_SUBSET_GENERATED_DECLINED_V1";
    case CETTA_C_SUBSET_V1_RETURN_LANGUAGE_FAULT:
        return "CETTA_C_SUBSET_GENERATED_LANGUAGE_FAULT_V1";
    case CETTA_C_SUBSET_V1_RETURN_ENGINE_FAULT:
        return "CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1";
    case CETTA_C_SUBSET_V1_RETURN_RESOURCE_FAULT:
        return "CETTA_C_SUBSET_GENERATED_RESOURCE_FAULT_V1";
    case CETTA_C_SUBSET_V1_BRANCH_ZERO:
    case CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL:
        return NULL;
    }
    return NULL;
}

static bool c_subset_emit_program(FILE *output,
                                  const CettaCSubsetV1Program *program) {
    uint32_t index;

    if (!c_subset_emit_write(
            output,
            "CettaCSubsetGeneratedOutcomeV1 %s(\n"
            "    const CettaCSubsetExactIntegerV1 *first,\n"
            "    const CettaCSubsetExactIntegerV1 *second,\n"
            "    CettaCSubsetExactIntegerV1 *output,\n"
            "    CettaCSubsetGeneratedReceiptV1 *receipt) {\n"
            "    CettaCSubsetGeneratedExternalV1 external_status;\n"
            "    receipt->event_count = 0u;\n"
            "    receipt->complete = true;\n"
            "    receipt->outcome = CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1;\n"
            "    (void)first;\n    (void)second;\n    (void)output;\n"
            "    goto instruction_%u;\n",
            program->entry_link_name, program->entry_instruction)) {
        return false;
    }
    for (index = 0u; index < program->instruction_len; index++) {
        const CettaCSubsetV1Instruction *instruction =
            &program->instructions[index];
        const char *return_name;

        if (!c_subset_emit_write(
                output,
                "instruction_%u:\n"
                "    cetta_c_subset_generated_record_event_v1(\n"
                "        receipt, CETTA_C_SUBSET_GENERATED_EVENT_STEP_V1,\n"
                "        %uu, 0u, CETTA_C_SUBSET_GENERATED_EXTERNAL_VALUE_V1);\n",
                index, index))
            return false;
        if (instruction->kind == CETTA_C_SUBSET_V1_BRANCH_ZERO) {
            if (!c_subset_emit_write(
                    output,
                    "    if (cetta_csubset_exact_integer_is_zero_v1(second))\n"
                    "        goto instruction_%u;\n"
                    "    goto instruction_%u;\n",
                    instruction->as.branch_zero.zero_target,
                    instruction->as.branch_zero.nonzero_target)) {
                return false;
            }
            continue;
        }
        if (instruction->kind ==
            CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL) {
            const char *external_name =
                program->externals[
                    instruction->as.call_binary.external].link_name;
            if (!c_subset_emit_write(
                    output,
                    "    external_status = %s(first, second, output);\n"
                    "    cetta_c_subset_generated_record_event_v1(\n"
                    "        receipt, CETTA_C_SUBSET_GENERATED_EVENT_EXTERNAL_V1,\n"
                    "        %uu, %uu, external_status);\n"
                    "    switch (external_status) {\n"
                    "    case CETTA_C_SUBSET_GENERATED_EXTERNAL_VALUE_V1:\n"
                    "        goto instruction_%u;\n"
                    "    case CETTA_C_SUBSET_GENERATED_EXTERNAL_LANGUAGE_FAULT_V1:\n"
                    "        goto instruction_%u;\n"
                    "    case CETTA_C_SUBSET_GENERATED_EXTERNAL_ENGINE_FAULT_V1:\n"
                    "        goto instruction_%u;\n"
                    "    case CETTA_C_SUBSET_GENERATED_EXTERNAL_RESOURCE_FAULT_V1:\n"
                    "        goto instruction_%u;\n"
                    "    }\n"
                    "    receipt->complete = false;\n"
                    "    receipt->outcome = CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1;\n"
                    "    return CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1;\n",
                    external_name,
                    index,
                    instruction->as.call_binary.external,
                    instruction->as.call_binary.value_target,
                    instruction->as.call_binary.language_fault_target,
                    instruction->as.call_binary.engine_fault_target,
                    instruction->as.call_binary.resource_fault_target)) {
                return false;
            }
            continue;
        }
        return_name = c_subset_emit_return_name(instruction->kind);
        if (!return_name ||
            !c_subset_emit_write(
                output,
                "    receipt->outcome = %s;\n"
                "    return %s;\n",
                return_name, return_name)) {
            return false;
        }
    }
    return c_subset_emit_write(output, "}\n\n");
}

bool cetta_c_subset_emit_v1_source(
    FILE *output,
    const CettaCSubsetV1Module *module,
    const char *generated_header_include,
    CettaCSubsetEmitV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_OK);
    if (!output || !module ||
        !c_subset_emit_include_valid(generated_header_include)) {
        c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_BAD_ARGUMENT);
        c_subset_emit_set_error(
            error_buf, error_buf_size,
            "bad C-subset source emission arguments");
        return false;
    }
    if (!c_subset_emit_validate(
            module, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!c_subset_emit_write(
            output,
            "#include \"%s\"\n\n"
            "static void cetta_c_subset_generated_record_event_v1(\n"
            "    CettaCSubsetGeneratedReceiptV1 *receipt,\n"
            "    CettaCSubsetGeneratedEventKindV1 kind,\n"
            "    uint32_t instruction,\n"
            "    uint32_t external,\n"
            "    CettaCSubsetGeneratedExternalV1 external_outcome) {\n"
            "    uint32_t index = receipt->event_count++;\n"
            "    if (!receipt->events || index >= receipt->event_capacity) {\n"
            "        receipt->complete = false;\n"
            "        return;\n"
            "    }\n"
            "    receipt->events[index].kind = kind;\n"
            "    receipt->events[index].instruction = instruction;\n"
            "    receipt->events[index].external = external;\n"
            "    receipt->events[index].external_outcome = external_outcome;\n"
            "}\n\n",
            generated_header_include) ||
        !c_subset_emit_external_declarations(output, module)) {
        goto io_failure;
    }
    for (index = 0u; index < module->program_len; index++) {
        if (!c_subset_emit_program(output, &module->programs[index]))
            goto io_failure;
    }
    if (fflush(output) != 0 || ferror(output))
        goto io_failure;
    return true;

io_failure:
    c_subset_emit_set_status(status, CETTA_C_SUBSET_EMIT_V1_IO_FAILURE);
    c_subset_emit_set_error(
        error_buf, error_buf_size,
        "failed to write generated C-subset source");
    return false;
}

const char *cetta_c_subset_emit_v1_status_name(
    CettaCSubsetEmitV1Status status) {
    switch (status) {
    case CETTA_C_SUBSET_EMIT_V1_OK:
        return "ok";
    case CETTA_C_SUBSET_EMIT_V1_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_C_SUBSET_EMIT_V1_INVALID_TARGET:
        return "invalid-target";
    case CETTA_C_SUBSET_EMIT_V1_IO_FAILURE:
        return "io-failure";
    }
    return "unknown";
}
