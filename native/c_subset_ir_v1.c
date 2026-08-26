#include "c_subset_ir_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void c_subset_set_error(char *buf,
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

static void c_subset_validate_status(CettaCSubsetV1ValidateStatus *status,
                                     CettaCSubsetV1ValidateStatus value) {
    if (status)
        *status = value;
}

static void c_subset_execute_status(CettaCSubsetV1ExecuteStatus *status,
                                    CettaCSubsetV1ExecuteStatus value) {
    if (status)
        *status = value;
}

void cetta_c_subset_v1_program_init(CettaCSubsetV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_c_subset_v1_program_free(CettaCSubsetV1Program *program) {
    uint32_t index;

    if (!program)
        return;
    free(program->semantic_name);
    free(program->entry_link_name);
    for (index = 0u; index < program->external_len; index++)
        free(program->externals[index].link_name);
    free(program->externals);
    free(program->slots);
    free(program->instructions);
    memset(program, 0, sizeof(*program));
}

void cetta_c_subset_v1_module_init(CettaCSubsetV1Module *module) {
    if (module)
        memset(module, 0, sizeof(*module));
}

void cetta_c_subset_v1_module_free(CettaCSubsetV1Module *module) {
    uint32_t index;

    if (!module)
        return;
    for (index = 0u; index < module->program_len; index++)
        cetta_c_subset_v1_program_free(&module->programs[index]);
    free(module->programs);
    memset(module, 0, sizeof(*module));
}

void cetta_c_subset_v1_execution_receipt_init(
    CettaCSubsetV1ExecutionReceipt *receipt) {
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
}

void cetta_c_subset_v1_execution_receipt_free(
    CettaCSubsetV1ExecutionReceipt *receipt) {
    if (!receipt)
        return;
    free(receipt->events);
    memset(receipt, 0, sizeof(*receipt));
}

static bool c_subset_target_valid(uint32_t target,
                                  uint32_t instruction_len) {
    return target < instruction_len;
}

static bool c_subset_c_identifier_valid(const char *identifier) {
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

static bool c_subset_validate_instruction(
    const CettaCSubsetV1Program *program,
    const CettaCSubsetV1Instruction *instruction,
    CettaCSubsetV1ValidateStatus *status,
    char *error_buf,
    size_t error_buf_size) {
    if (!program || !instruction)
        return false;
    switch (instruction->kind) {
    case CETTA_C_SUBSET_V1_BRANCH_ZERO:
        if (instruction->as.branch_zero.slot >= program->slot_len ||
            program->slots[instruction->as.branch_zero.slot].type !=
                CETTA_C_SUBSET_V1_EXACT_INTEGER ||
            !c_subset_target_valid(
                instruction->as.branch_zero.zero_target,
                program->instruction_len) ||
            !c_subset_target_valid(
                instruction->as.branch_zero.nonzero_target,
                program->instruction_len)) {
            goto malformed;
        }
        return true;
    case CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL: {
        const CettaCSubsetV1Instruction *call = instruction;
        if (call->as.call_binary.external >= program->external_len ||
            !c_subset_target_valid(
                call->as.call_binary.value_target,
                program->instruction_len) ||
            !c_subset_target_valid(
                call->as.call_binary.language_fault_target,
                program->instruction_len) ||
            !c_subset_target_valid(
                call->as.call_binary.engine_fault_target,
                program->instruction_len) ||
            !c_subset_target_valid(
                call->as.call_binary.resource_fault_target,
                program->instruction_len)) {
            goto malformed;
        }
        return true;
    }
    case CETTA_C_SUBSET_V1_RETURN_VALUE:
    case CETTA_C_SUBSET_V1_RETURN_DECLINED:
    case CETTA_C_SUBSET_V1_RETURN_LANGUAGE_FAULT:
    case CETTA_C_SUBSET_V1_RETURN_ENGINE_FAULT:
    case CETTA_C_SUBSET_V1_RETURN_RESOURCE_FAULT:
        return true;
    }

malformed:
    c_subset_validate_status(
        status, CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM);
    c_subset_set_error(
        error_buf, error_buf_size,
        "C-subset instruction has an invalid slot, external, or target");
    return false;
}

bool cetta_c_subset_v1_validate_program(
    const CettaCSubsetV1Program *program,
    uint32_t work_limit,
    CettaCSubsetV1ValidateStatus *status,
    char *error_buf,
    size_t error_buf_size) {
    bool *reachable = NULL;
    uint32_t *queue = NULL;
    uint32_t queue_head = 0u;
    uint32_t queue_len = 0u;
    uint32_t work = 0u;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    c_subset_validate_status(status, CETTA_C_SUBSET_V1_VALIDATE_OK);
    if (!program || work_limit == 0u) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_BAD_ARGUMENT);
        c_subset_set_error(
            error_buf, error_buf_size,
            "bad C-subset program validation arguments");
        return false;
    }
    if (!program->semantic_name ||
        !c_subset_c_identifier_valid(program->entry_link_name) ||
        program->slot_len != 3u ||
        !program->slots || program->external_len != 1u ||
        !program->externals || program->instruction_len == 0u ||
        !program->instructions ||
        program->entry_instruction >= program->instruction_len ||
        program->slots[0].type != CETTA_C_SUBSET_V1_EXACT_INTEGER ||
        program->slots[1].type != CETTA_C_SUBSET_V1_EXACT_INTEGER ||
        program->slots[2].type != CETTA_C_SUBSET_V1_EXACT_INTEGER ||
        program->slots[0].mode != CETTA_C_SUBSET_V1_BORROWED_INPUT ||
        program->slots[1].mode != CETTA_C_SUBSET_V1_BORROWED_INPUT ||
        program->slots[2].mode !=
            CETTA_C_SUBSET_V1_BORROWED_MUTABLE_OUTPUT ||
        !c_subset_c_identifier_valid(program->externals[0].link_name) ||
        program->externals[0].first_input_slot != 0u ||
        program->externals[0].second_input_slot != 1u ||
        program->externals[0].output_slot != 2u) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM);
        c_subset_set_error(
            error_buf, error_buf_size,
            "C-subset binary program violates its exact-integer ABI shape");
        return false;
    }
    reachable = calloc(program->instruction_len, sizeof(*reachable));
    queue = malloc(sizeof(*queue) * program->instruction_len);
    if (!reachable || !queue) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_ALLOCATION_FAILURE);
        c_subset_set_error(
            error_buf, error_buf_size,
            "failed to allocate C-subset validation reachability state");
        goto done;
    }
    for (index = 0u; index < program->instruction_len; index++) {
        if (work++ >= work_limit) {
            c_subset_validate_status(
                status, CETTA_C_SUBSET_V1_VALIDATE_RESOURCE_LIMIT);
            c_subset_set_error(
                error_buf, error_buf_size,
                "C-subset validation work limit reached");
            goto done;
        }
        if (!c_subset_validate_instruction(
                program, &program->instructions[index],
                status, error_buf, error_buf_size)) {
            goto done;
        }
    }
    reachable[program->entry_instruction] = true;
    queue[queue_len++] = program->entry_instruction;
    while (queue_head < queue_len) {
        const CettaCSubsetV1Instruction *instruction;
        uint32_t targets[4];
        uint32_t target_len = 0u;

        if (work++ >= work_limit) {
            c_subset_validate_status(
                status, CETTA_C_SUBSET_V1_VALIDATE_RESOURCE_LIMIT);
            c_subset_set_error(
                error_buf, error_buf_size,
                "C-subset reachability work limit reached");
            goto done;
        }
        instruction = &program->instructions[queue[queue_head++]];
        if (instruction->kind == CETTA_C_SUBSET_V1_BRANCH_ZERO) {
            targets[target_len++] = instruction->as.branch_zero.zero_target;
            targets[target_len++] = instruction->as.branch_zero.nonzero_target;
        } else if (instruction->kind ==
                   CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL) {
            targets[target_len++] = instruction->as.call_binary.value_target;
            targets[target_len++] =
                instruction->as.call_binary.language_fault_target;
            targets[target_len++] =
                instruction->as.call_binary.engine_fault_target;
            targets[target_len++] =
                instruction->as.call_binary.resource_fault_target;
        }
        for (index = 0u; index < target_len; index++) {
            uint32_t target = targets[index];
            if (!reachable[target]) {
                reachable[target] = true;
                queue[queue_len++] = target;
            }
        }
    }
    for (index = 0u; index < program->instruction_len; index++) {
        if (!reachable[index]) {
            c_subset_validate_status(
                status, CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM);
            c_subset_set_error(
                error_buf, error_buf_size,
                "C-subset program contains unreachable instructions");
            goto done;
        }
    }
    ok = true;

done:
    free(queue);
    free(reachable);
    return ok;
}

bool cetta_c_subset_v1_validate_module(
    const CettaCSubsetV1Module *module,
    uint32_t work_limit,
    CettaCSubsetV1ValidateStatus *status,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t per_program_limit;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    c_subset_validate_status(status, CETTA_C_SUBSET_V1_VALIDATE_OK);
    if (!module || work_limit == 0u ||
        (module->program_len > 0u && !module->programs)) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_BAD_ARGUMENT);
        c_subset_set_error(
            error_buf, error_buf_size,
            "bad C-subset module validation arguments");
        return false;
    }
    if (module->program_len == 0u) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM);
        c_subset_set_error(
            error_buf, error_buf_size,
            "C-subset module must contain at least one program");
        return false;
    }
    per_program_limit = work_limit / module->program_len;
    if (per_program_limit == 0u) {
        c_subset_validate_status(
            status, CETTA_C_SUBSET_V1_VALIDATE_RESOURCE_LIMIT);
        c_subset_set_error(
            error_buf, error_buf_size,
            "C-subset module validation work limit reached");
        return false;
    }
    for (index = 0u; index < module->program_len; index++) {
        if (!cetta_c_subset_v1_validate_program(
                &module->programs[index], per_program_limit,
                status, error_buf, error_buf_size)) {
            return false;
        }
    }
    return true;
}

bool cetta_c_subset_v1_execute_binary(
    const CettaCSubsetV1Program *program,
    const CettaCSubsetV1ExecutionProvider *provider,
    const void *first,
    const void *second,
    void *output,
    uint32_t step_limit,
    CettaCSubsetV1ExecutionReceipt *receipt,
    CettaCSubsetV1ExecuteStatus *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaCSubsetV1ValidateStatus validation_status;
    CettaCSubsetV1ExecutionReceipt candidate = {0};
    uint32_t event_capacity;
    uint32_t pc;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    c_subset_execute_status(status, CETTA_C_SUBSET_V1_EXECUTE_OK);
    if (!program || !provider || !provider->is_zero ||
        !provider->call_binary || !first || !second || !output ||
        !receipt || step_limit == 0u) {
        c_subset_execute_status(
            status, CETTA_C_SUBSET_V1_EXECUTE_BAD_ARGUMENT);
        c_subset_set_error(
            error_buf, error_buf_size,
            "bad C-subset binary execution arguments");
        return false;
    }
    if (!cetta_c_subset_v1_validate_program(
            program, 100000u, &validation_status,
            error_buf, error_buf_size)) {
        c_subset_execute_status(
            status, CETTA_C_SUBSET_V1_EXECUTE_INVALID_PROGRAM);
        return false;
    }
    if (step_limit > UINT32_MAX / 2u ||
        (size_t)(step_limit * 2u) >
            SIZE_MAX / sizeof(*candidate.events)) {
        c_subset_execute_status(
            status, CETTA_C_SUBSET_V1_EXECUTE_ALLOCATION_FAILURE);
        c_subset_set_error(
            error_buf, error_buf_size,
            "C-subset execution receipt is too large");
        return false;
    }
    event_capacity = step_limit * 2u;
    candidate.events = calloc(event_capacity, sizeof(*candidate.events));
    if (!candidate.events) {
        c_subset_execute_status(
            status, CETTA_C_SUBSET_V1_EXECUTE_ALLOCATION_FAILURE);
        c_subset_set_error(
            error_buf, error_buf_size,
            "failed to allocate C-subset execution receipt");
        return false;
    }
    pc = program->entry_instruction;
    while (candidate.step_count < step_limit) {
        const CettaCSubsetV1Instruction *instruction =
            &program->instructions[pc];
        candidate.final_instruction = pc;
        candidate.step_count++;
        candidate.events[candidate.event_count].kind =
            CETTA_C_SUBSET_V1_EVENT_STEP;
        candidate.events[candidate.event_count].instruction = pc;
        candidate.event_count++;
        switch (instruction->kind) {
        case CETTA_C_SUBSET_V1_BRANCH_ZERO:
            if (instruction->as.branch_zero.slot != 1u) {
                goto provider_failure;
            }
            pc = provider->is_zero(provider->context, second)
                ? instruction->as.branch_zero.zero_target
                : instruction->as.branch_zero.nonzero_target;
            break;
        case CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL: {
            const CettaCSubsetV1BinaryExternal *external =
                &program->externals[instruction->as.call_binary.external];
            CettaCSubsetV1ExternalOutcome external_outcome;

            candidate.external_call_count++;
            external_outcome = provider->call_binary(
                provider->context, external->link_name,
                first, second, output);
            switch (external_outcome) {
            case CETTA_C_SUBSET_V1_EXTERNAL_VALUE:
                pc = instruction->as.call_binary.value_target;
                break;
            case CETTA_C_SUBSET_V1_EXTERNAL_LANGUAGE_FAULT:
                pc = instruction->as.call_binary.language_fault_target;
                break;
            case CETTA_C_SUBSET_V1_EXTERNAL_ENGINE_FAULT:
                pc = instruction->as.call_binary.engine_fault_target;
                break;
            case CETTA_C_SUBSET_V1_EXTERNAL_RESOURCE_FAULT:
                pc = instruction->as.call_binary.resource_fault_target;
                break;
            default:
                goto provider_failure;
            }
            candidate.events[candidate.event_count].kind =
                CETTA_C_SUBSET_V1_EVENT_EXTERNAL;
            candidate.events[candidate.event_count].instruction =
                candidate.final_instruction;
            candidate.events[candidate.event_count].external =
                instruction->as.call_binary.external;
            candidate.events[candidate.event_count].external_outcome =
                external_outcome;
            candidate.event_count++;
            break;
        }
        case CETTA_C_SUBSET_V1_RETURN_VALUE:
            candidate.outcome = CETTA_C_SUBSET_V1_OUTCOME_VALUE;
            goto success;
        case CETTA_C_SUBSET_V1_RETURN_DECLINED:
            candidate.outcome = CETTA_C_SUBSET_V1_OUTCOME_DECLINED;
            goto success;
        case CETTA_C_SUBSET_V1_RETURN_LANGUAGE_FAULT:
            candidate.outcome = CETTA_C_SUBSET_V1_OUTCOME_LANGUAGE_FAULT;
            goto success;
        case CETTA_C_SUBSET_V1_RETURN_ENGINE_FAULT:
            candidate.outcome = CETTA_C_SUBSET_V1_OUTCOME_ENGINE_FAULT;
            goto success;
        case CETTA_C_SUBSET_V1_RETURN_RESOURCE_FAULT:
            candidate.outcome = CETTA_C_SUBSET_V1_OUTCOME_RESOURCE_FAULT;
            goto success;
        }
    }
    c_subset_execute_status(
        status, CETTA_C_SUBSET_V1_EXECUTE_STEP_LIMIT);
    c_subset_set_error(
        error_buf, error_buf_size,
        "C-subset binary execution step limit reached");
    cetta_c_subset_v1_execution_receipt_free(&candidate);
    return false;

provider_failure:
    c_subset_execute_status(
        status, CETTA_C_SUBSET_V1_EXECUTE_PROVIDER_FAILURE);
    c_subset_set_error(
        error_buf, error_buf_size,
        "C-subset execution provider returned an invalid result");
    cetta_c_subset_v1_execution_receipt_free(&candidate);
    return false;

success:
    cetta_c_subset_v1_execution_receipt_free(receipt);
    *receipt = candidate;
    c_subset_execute_status(status, CETTA_C_SUBSET_V1_EXECUTE_OK);
    return true;
}

const char *cetta_c_subset_v1_validate_status_name(
    CettaCSubsetV1ValidateStatus status) {
    switch (status) {
    case CETTA_C_SUBSET_V1_VALIDATE_OK:
        return "ok";
    case CETTA_C_SUBSET_V1_VALIDATE_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM:
        return "malformed-program";
    case CETTA_C_SUBSET_V1_VALIDATE_RESOURCE_LIMIT:
        return "resource-limit";
    case CETTA_C_SUBSET_V1_VALIDATE_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}

const char *cetta_c_subset_v1_execute_status_name(
    CettaCSubsetV1ExecuteStatus status) {
    switch (status) {
    case CETTA_C_SUBSET_V1_EXECUTE_OK:
        return "ok";
    case CETTA_C_SUBSET_V1_EXECUTE_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_C_SUBSET_V1_EXECUTE_INVALID_PROGRAM:
        return "invalid-program";
    case CETTA_C_SUBSET_V1_EXECUTE_STEP_LIMIT:
        return "step-limit";
    case CETTA_C_SUBSET_V1_EXECUTE_PROVIDER_FAILURE:
        return "provider-failure";
    case CETTA_C_SUBSET_V1_EXECUTE_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}
