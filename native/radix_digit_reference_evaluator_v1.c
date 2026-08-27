#include "radix_digit_reference_evaluator_v1.h"

#include <stdlib.h>
#include <string.h>

#define RADIX_DIGIT_V1_BUFFER_COUNT 3u
#define RADIX_DIGIT_V1_REGISTER_COUNT 16u

typedef struct {
    const uint32_t *read_only;
    uint32_t *owned;
    uint32_t len;
    uint32_t capacity;
    bool writable;
} RadixDigitBuffer;

typedef struct {
    RadixDigitBuffer buffers[RADIX_DIGIT_V1_BUFFER_COUNT];
    uint32_t registers[RADIX_DIGIT_V1_REGISTER_COUNT];
    uint32_t pc;
    uint32_t fuel;
    uint32_t output_limit;
    CettaRadixDigitV1Event *events;
    uint32_t event_len;
    uint32_t event_capacity;
} RadixDigitState;

void cetta_radix_digit_v1_run_result_init(CettaRadixDigitV1RunResult *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

void cetta_radix_digit_v1_run_result_free(CettaRadixDigitV1RunResult *result) {
    if (!result)
        return;
    free(result->digits);
    free(result->events);
    cetta_radix_digit_v1_run_result_init(result);
}

static const uint32_t *buffer_data(const RadixDigitBuffer *buffer) {
    return buffer->writable ? buffer->owned : buffer->read_only;
}

static bool append_event(RadixDigitState *state, const CettaRadixDigitV1Event *event) {
    if (state->event_len == state->event_capacity) {
        uint32_t next_capacity = state->event_capacity == 0u ? 16u :
            state->event_capacity * 2u;
        CettaRadixDigitV1Event *next;
        if (next_capacity < state->event_capacity)
            return false;
        next = realloc(state->events,
            (size_t)next_capacity * sizeof(*state->events));
        if (!next)
            return false;
        state->events = next;
        state->event_capacity = next_capacity;
    }
    state->events[state->event_len++] = *event;
    return true;
}

static void set_fault(
    CettaRadixDigitV1RunResult *result,
    CettaRadixDigitV1OutcomeKind kind,
    CettaRadixDigitV1FaultKind fault,
    uint32_t first,
    uint32_t second) {
    result->kind = kind;
    result->fault = fault;
    result->fault_first = first;
    result->fault_second = second;
}

static void append_fault_event(
    RadixDigitState *state,
    CettaRadixDigitV1FaultKind fault,
    uint32_t first,
    uint32_t second) {
    CettaRadixDigitV1Event event;
    memset(&event, 0, sizeof(event));
    event.kind = CETTA_RADIX_DIGIT_V1_EVENT_FAULT;
    event.pc = state->pc;
    event.fault = fault;
    event.fault_first = first;
    event.fault_second = second;
    (void)append_event(state, &event);
}

static void halt_language(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    CettaRadixDigitV1FaultKind fault,
    uint32_t first,
    uint32_t second) {
    set_fault(result, CETTA_RADIX_DIGIT_V1_OUTCOME_LANGUAGE_FAULT,
        fault, first, second);
    append_fault_event(state, fault, first, second);
}

static void halt_engine(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    CettaRadixDigitV1FaultKind fault,
    uint32_t first,
    uint32_t second) {
    set_fault(result, CETTA_RADIX_DIGIT_V1_OUTCOME_ENGINE_FAULT,
        fault, first, second);
    append_fault_event(state, fault, first, second);
}

static void halt_resource(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    CettaRadixDigitV1FaultKind fault,
    uint32_t first,
    uint32_t second) {
    set_fault(result, CETTA_RADIX_DIGIT_V1_OUTCOME_RESOURCE_FAULT,
        fault, first, second);
    append_fault_event(state, fault, first, second);
}

static RadixDigitBuffer *get_buffer(RadixDigitState *state, uint32_t index) {
    return index < RADIX_DIGIT_V1_BUFFER_COUNT ? &state->buffers[index] : NULL;
}

static bool get_register(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    uint32_t index,
    uint32_t *value) {
    if (index >= RADIX_DIGIT_V1_REGISTER_COUNT) {
        halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_REGISTER,
            index, 0u);
        return false;
    }
    *value = state->registers[index];
    return true;
}

static bool put_register(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    uint32_t index,
    uint32_t value) {
    if (index >= RADIX_DIGIT_V1_REGISTER_COUNT) {
        halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_REGISTER,
            index, 0u);
        return false;
    }
    state->registers[index] = value;
    return true;
}

static bool record_execute(RadixDigitState *state) {
    CettaRadixDigitV1Event event;
    memset(&event, 0, sizeof(event));
    event.kind = CETTA_RADIX_DIGIT_V1_EVENT_EXECUTE;
    event.pc = state->pc;
    return append_event(state, &event);
}

static bool record_table_row(
    RadixDigitState *state,
    uint32_t table_index,
    uint32_t row_index,
    const CettaRadixDigitV1TableRow *row) {
    CettaRadixDigitV1Event event;
    memset(&event, 0, sizeof(event));
    event.kind = CETTA_RADIX_DIGIT_V1_EVENT_TABLE_ROW;
    event.pc = state->pc;
    event.table_index = table_index;
    event.row_index = row_index;
    event.source_rule_len = row->source_rule_len;
    memcpy(event.source_rule_indices, row->source_rule_indices,
        row->source_rule_len * sizeof(*event.source_rule_indices));
    return append_event(state, &event);
}

static bool write_digit(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    uint32_t buffer_index,
    uint32_t index,
    uint32_t digit) {
    RadixDigitBuffer *buffer = get_buffer(state, buffer_index);
    uint32_t next_capacity;
    uint32_t *next;

    if (!buffer) {
        halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_BUFFER,
            buffer_index, 0u);
        return false;
    }
    if (!buffer->writable) {
        halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_INCONSISTENT_BUFFER_WRITE,
            buffer_index, index);
        return false;
    }
    if (index > buffer->len) {
        halt_language(state, result, CETTA_RADIX_DIGIT_V1_FAULT_SPARSE_WRITE,
            buffer_index, index);
        return false;
    }
    if (index == buffer->len) {
        if (buffer->len >= state->output_limit) {
            halt_resource(state, result, CETTA_RADIX_DIGIT_V1_FAULT_OUTPUT_LIMIT,
                buffer_index, 0u);
            return false;
        }
        if (buffer->len == buffer->capacity) {
            next_capacity = buffer->capacity == 0u ? 8u : buffer->capacity * 2u;
            if (next_capacity < buffer->capacity ||
                    next_capacity > state->output_limit)
                next_capacity = state->output_limit;
            if (next_capacity <= buffer->capacity) {
                halt_resource(state, result, CETTA_RADIX_DIGIT_V1_FAULT_OUTPUT_LIMIT,
                    buffer_index, 0u);
                return false;
            }
            next = realloc(buffer->owned,
                (size_t)next_capacity * sizeof(*buffer->owned));
            if (!next) {
                halt_resource(state, result, CETTA_RADIX_DIGIT_V1_FAULT_ALLOCATION,
                    buffer_index, next_capacity);
                return false;
            }
            buffer->owned = next;
            buffer->capacity = next_capacity;
        }
        buffer->owned[buffer->len++] = digit;
    } else {
        buffer->owned[index] = digit;
    }
    return true;
}

static bool execute_lookup(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    const CettaRadixDigitV1Program *program,
    const CettaRadixDigitV1Instruction *cell) {
    uint32_t cursor = 0u;
    uint32_t input_len;
    uint32_t output_len;
    uint32_t inputs[4];
    uint32_t output_registers[2];
    uint32_t row_index;
    uint32_t index;
    uint32_t next_pc;
    const CettaRadixDigitV1Table *table;
    const CettaRadixDigitV1TableRow *row;

    if (cell->argument_len == 0u)
        goto malformed;
    input_len = cell->arguments[cursor++];
    if (input_len > 4u || cursor + input_len >= cell->argument_len)
        goto malformed;
    for (index = 0u; index < input_len; ++index)
        if (!get_register(state, result, cell->arguments[cursor++], &inputs[index]))
            return false;
    output_len = cell->arguments[cursor++];
    if (output_len > 2u || cursor + output_len + 1u != cell->argument_len)
        goto malformed;
    for (index = 0u; index < output_len; ++index)
        output_registers[index] = cell->arguments[cursor++];
    next_pc = cell->arguments[cursor];
    if (cell->table_index >= program->table_len)
        goto malformed;
    table = &program->tables[cell->table_index];
    row = cetta_radix_digit_v1_find_row(table, inputs, input_len, &row_index);
    if (!row) {
        halt_language(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_TABLE_ROW,
            input_len > 0u ? inputs[0] : 0u,
            input_len > 1u ? inputs[1] : 0u);
        return false;
    }
    if (row->output_len != output_len || row->source_rule_len > 32u)
        goto malformed;
    if (!record_table_row(state, cell->table_index, row_index, row)) {
        halt_resource(state, result, CETTA_RADIX_DIGIT_V1_FAULT_ALLOCATION, 0u, 0u);
        return false;
    }
    for (index = 0u; index < output_len; ++index)
        if (!put_register(state, result, output_registers[index], row->outputs[index]))
            return false;
    state->pc = next_pc;
    return true;

malformed:
    halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MALFORMED_TABLE_ROW, 0u, 0u);
    return false;
}

static bool execute_cell(
    RadixDigitState *state,
    CettaRadixDigitV1RunResult *result,
    uint32_t radix,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program,
    const CettaRadixDigitV1Instruction *cell,
    bool *halted) {
    uint32_t first;
    uint32_t second;
    uint32_t third;
    RadixDigitBuffer *buffer;
    CettaRadixDigitV1InstructionKind kind;

    *halted = false;
    if (!cetta_radix_digit_v1_target_constructor(profile,
            cell->constructor_term_index, &kind))
        goto malformed_instruction;
    switch (kind) {
        case CETTA_RADIX_DIGIT_V1_SET:
            if (cell->argument_len != 3u ||
                    !put_register(state, result,
                        cell->arguments[0], cell->arguments[1]))
                return false;
            state->pc = cell->arguments[2];
            return true;
        case CETTA_RADIX_DIGIT_V1_COPY:
            if (cell->argument_len != 3u ||
                    !get_register(state, result, cell->arguments[0], &first) ||
                    !put_register(state, result, cell->arguments[1], first))
                return false;
            state->pc = cell->arguments[2];
            return true;
        case CETTA_RADIX_DIGIT_V1_INCREMENT:
            if (cell->argument_len != 2u ||
                    !get_register(state, result, cell->arguments[0], &first))
                return false;
            if (first == UINT32_MAX) {
                halt_resource(state, result, CETTA_RADIX_DIGIT_V1_FAULT_OUTPUT_LIMIT,
                    cell->arguments[0], first);
                return false;
            }
            if (!put_register(state, result, cell->arguments[0], first + 1u))
                return false;
            state->pc = cell->arguments[1];
            return true;
        case CETTA_RADIX_DIGIT_V1_LENGTH:
            if (cell->argument_len != 3u)
                goto malformed_instruction;
            buffer = get_buffer(state, cell->arguments[0]);
            if (!buffer) {
                halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_BUFFER,
                    cell->arguments[0], 0u);
                return false;
            }
            if (!put_register(state, result, cell->arguments[1], buffer->len))
                return false;
            state->pc = cell->arguments[2];
            return true;
        case CETTA_RADIX_DIGIT_V1_READ_OR_ZERO:
            if (cell->argument_len != 4u ||
                    !get_register(state, result, cell->arguments[1], &first))
                return false;
            buffer = get_buffer(state, cell->arguments[0]);
            if (!buffer) {
                halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_BUFFER,
                    cell->arguments[0], 0u);
                return false;
            }
            second = first < buffer->len ? buffer_data(buffer)[first] : 0u;
            if (second >= radix) {
                halt_language(state, result, CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT,
                    second, 0u);
                return false;
            }
            if (!put_register(state, result, cell->arguments[2], second))
                return false;
            state->pc = cell->arguments[3];
            return true;
        case CETTA_RADIX_DIGIT_V1_WRITE:
            if (cell->argument_len != 4u ||
                    !get_register(state, result, cell->arguments[1], &first) ||
                    !get_register(state, result, cell->arguments[2], &second))
                return false;
            if (second >= radix) {
                halt_language(state, result, CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT,
                    second, 0u);
                return false;
            }
            if (!write_digit(state, result, cell->arguments[0], first, second))
                return false;
            state->pc = cell->arguments[3];
            return true;
        case CETTA_RADIX_DIGIT_V1_LOOKUP:
            return execute_lookup(state, result, program, cell);
        case CETTA_RADIX_DIGIT_V1_BRANCH_LT:
            if (cell->argument_len != 4u ||
                    !get_register(state, result, cell->arguments[0], &first) ||
                    !get_register(state, result, cell->arguments[1], &second))
                return false;
            state->pc = first < second ? cell->arguments[2] : cell->arguments[3];
            return true;
        case CETTA_RADIX_DIGIT_V1_BRANCH_EQ:
            if (cell->argument_len != 4u ||
                    !get_register(state, result, cell->arguments[0], &first))
                return false;
            state->pc = first == cell->arguments[1] ?
                cell->arguments[2] : cell->arguments[3];
            return true;
        case CETTA_RADIX_DIGIT_V1_JUMP:
            if (cell->argument_len != 1u)
                goto malformed_instruction;
            state->pc = cell->arguments[0];
            return true;
        case CETTA_RADIX_DIGIT_V1_RETURN_BUFFER:
            if (cell->argument_len != 1u)
                goto malformed_instruction;
            buffer = get_buffer(state, cell->arguments[0]);
            if (!buffer) {
                halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_BUFFER,
                    cell->arguments[0], 0u);
                return false;
            }
            for (third = 0u; third < buffer->len; ++third) {
                first = buffer_data(buffer)[third];
                if (first >= radix) {
                    halt_language(state, result, CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT,
                        first, 0u);
                    return false;
                }
            }
            result->kind = CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE;
            result->digits = buffer->owned;
            result->digit_len = buffer->len;
            buffer->owned = NULL;
            buffer->len = 0u;
            buffer->capacity = 0u;
            *halted = true;
            return true;
        default:
            break;
    }

malformed_instruction:
    halt_engine(state, result, CETTA_RADIX_DIGIT_V1_FAULT_MISSING_PROGRAM_COUNTER,
        state->pc, 0u);
    return false;
}

bool cetta_radix_digit_v1_execute(
    CettaRadixDigitV1RunResult *out,
    uint32_t radix,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program,
    const uint32_t *first,
    uint32_t first_len,
    const uint32_t *second,
    uint32_t second_len,
    uint32_t output_limit,
    uint32_t fuel) {
    RadixDigitState state;
    CettaRadixDigitV1RunResult candidate;
    bool halted = false;

    if (!out || radix < 2u || !profile || !program ||
            !cetta_radix_digit_v1_program_valid(profile, program) ||
            (!first && first_len != 0u) ||
            (!second && second_len != 0u))
        return false;
    memset(&state, 0, sizeof(state));
    cetta_radix_digit_v1_run_result_init(&candidate);
    state.buffers[0].read_only = first;
    state.buffers[0].len = first_len;
    state.buffers[1].read_only = second;
    state.buffers[1].len = second_len;
    state.buffers[2].writable = true;
    state.output_limit = output_limit;
    state.fuel = fuel;

    while (!halted) {
        const CettaRadixDigitV1Instruction *cell;
        if (state.fuel == 0u) {
            halt_resource(&state, &candidate,
                CETTA_RADIX_DIGIT_V1_FAULT_FUEL_EXHAUSTED, 0u, 0u);
            break;
        }
        if (state.pc >= program->instruction_len) {
            halt_engine(&state, &candidate,
                CETTA_RADIX_DIGIT_V1_FAULT_MISSING_PROGRAM_COUNTER, state.pc, 0u);
            break;
        }
        cell = &program->instructions[state.pc];
        --state.fuel;
        ++candidate.steps;
        if (!record_execute(&state)) {
            halt_resource(&state, &candidate,
                CETTA_RADIX_DIGIT_V1_FAULT_ALLOCATION, 0u, 0u);
            break;
        }
        if (!execute_cell(&state, &candidate, radix,
                profile, program, cell, &halted))
            break;
    }

    free(state.buffers[2].owned);
    candidate.events = state.events;
    candidate.event_len = state.event_len;
    cetta_radix_digit_v1_run_result_free(out);
    *out = candidate;
    return true;
}
