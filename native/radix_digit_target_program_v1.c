#include "radix_digit_target_program_v1.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t radix_digit_program_magic[] = {'R', 'D', 'P', '2'};

void cetta_radix_digit_v1_program_init(CettaRadixDigitV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_radix_digit_v1_program_free(CettaRadixDigitV1Program *program) {
    uint32_t index;
    if (!program)
        return;
    free(program->instructions);
    for (index = 0u; index < program->table_len; ++index)
        free(program->tables[index].rows);
    free(program->tables);
    cetta_radix_digit_v1_program_init(program);
}

void cetta_radix_digit_v1_program_wire_init(CettaRadixDigitV1ProgramWire *wire) {
    if (wire)
        memset(wire, 0, sizeof(*wire));
}

void cetta_radix_digit_v1_program_wire_free(CettaRadixDigitV1ProgramWire *wire) {
    if (!wire)
        return;
    free(wire->bytes);
    cetta_radix_digit_v1_program_wire_init(wire);
}

bool cetta_radix_digit_v1_target_profile_valid(
    const CettaRadixDigitV1TargetProfile *profile) {
    uint32_t left;
    uint32_t right;
    if (!profile)
        return false;
    for (left = 0u; left < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++left)
        for (right = left + 1u; right < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++right)
            if (profile->instruction_term_indices[left] ==
                    profile->instruction_term_indices[right])
                return false;
    return true;
}

bool cetta_radix_digit_v1_target_constructor(
    const CettaRadixDigitV1TargetProfile *profile,
    uint32_t constructor_term_index,
    CettaRadixDigitV1InstructionKind *kind_out) {
    uint32_t index;
    if (!kind_out || !cetta_radix_digit_v1_target_profile_valid(profile))
        return false;
    for (index = 0u; index < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++index) {
        if (profile->instruction_term_indices[index] == constructor_term_index) {
            *kind_out = (CettaRadixDigitV1InstructionKind)index;
            return true;
        }
    }
    return false;
}

static bool register_index(uint32_t value) {
    return value < 16u;
}

static bool buffer_index(uint32_t value) {
    return value < 3u;
}

static bool pc_index(uint32_t value, uint32_t instruction_len) {
    return value < instruction_len;
}

static bool lookup_shape_valid(
    const CettaRadixDigitV1Instruction *instruction,
    uint32_t instruction_len) {
    uint32_t cursor = 0u;
    uint32_t input_len;
    uint32_t output_len;
    uint32_t index;
    if (instruction->argument_len == 0u)
        return false;
    input_len = instruction->arguments[cursor++];
    if (input_len > 4u || cursor + input_len >= instruction->argument_len)
        return false;
    for (index = 0u; index < input_len; ++index)
        if (!register_index(instruction->arguments[cursor++]))
            return false;
    output_len = instruction->arguments[cursor++];
    if (output_len > 2u || cursor + output_len + 1u != instruction->argument_len)
        return false;
    for (index = 0u; index < output_len; ++index)
        if (!register_index(instruction->arguments[cursor++]))
            return false;
    return pc_index(instruction->arguments[cursor], instruction_len);
}

bool cetta_radix_digit_v1_instruction_valid(
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Instruction *instruction,
    uint32_t instruction_len) {
    CettaRadixDigitV1InstructionKind kind;
    const uint32_t *a;
    if (!instruction || instruction->argument_len > 12u ||
            !cetta_radix_digit_v1_target_constructor(profile,
                instruction->constructor_term_index, &kind))
        return false;
    if (kind != CETTA_RADIX_DIGIT_V1_LOOKUP && instruction->table_index != 0u)
        return false;
    a = instruction->arguments;
    switch (kind) {
    case CETTA_RADIX_DIGIT_V1_SET:
        return instruction->argument_len == 3u && register_index(a[0]) &&
            pc_index(a[2], instruction_len);
    case CETTA_RADIX_DIGIT_V1_COPY:
        return instruction->argument_len == 3u && register_index(a[0]) &&
            register_index(a[1]) && pc_index(a[2], instruction_len);
    case CETTA_RADIX_DIGIT_V1_INCREMENT:
        return instruction->argument_len == 2u && register_index(a[0]) &&
            pc_index(a[1], instruction_len);
    case CETTA_RADIX_DIGIT_V1_LENGTH:
        return instruction->argument_len == 3u && buffer_index(a[0]) &&
            register_index(a[1]) && pc_index(a[2], instruction_len);
    case CETTA_RADIX_DIGIT_V1_READ_OR_ZERO:
        return instruction->argument_len == 4u && buffer_index(a[0]) &&
            register_index(a[1]) && register_index(a[2]) &&
            pc_index(a[3], instruction_len);
    case CETTA_RADIX_DIGIT_V1_WRITE:
        return instruction->argument_len == 4u && buffer_index(a[0]) &&
            register_index(a[1]) && register_index(a[2]) &&
            pc_index(a[3], instruction_len);
    case CETTA_RADIX_DIGIT_V1_LOOKUP:
        return lookup_shape_valid(instruction, instruction_len);
    case CETTA_RADIX_DIGIT_V1_BRANCH_LT:
        return instruction->argument_len == 4u && register_index(a[0]) &&
            register_index(a[1]) && pc_index(a[2], instruction_len) &&
            pc_index(a[3], instruction_len);
    case CETTA_RADIX_DIGIT_V1_BRANCH_EQ:
        return instruction->argument_len == 4u && register_index(a[0]) &&
            pc_index(a[2], instruction_len) &&
            pc_index(a[3], instruction_len);
    case CETTA_RADIX_DIGIT_V1_JUMP:
        return instruction->argument_len == 1u &&
            pc_index(a[0], instruction_len);
    case CETTA_RADIX_DIGIT_V1_RETURN_BUFFER:
        return instruction->argument_len == 1u && buffer_index(a[0]);
    case CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT:
        break;
    }
    return false;
}

bool cetta_radix_digit_v1_program_valid(
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program) {
    uint32_t index;
    if (!cetta_radix_digit_v1_target_profile_valid(profile) || !program ||
            program->instruction_len == 0u || !program->instructions ||
            (program->table_len != 0u && !program->tables))
        return false;
    for (index = 0u; index < program->table_len; ++index) {
        uint32_t row_index;
        const CettaRadixDigitV1Table *table = &program->tables[index];
        if (table->row_len != 0u && !table->rows)
            return false;
        for (row_index = 0u; row_index < table->row_len; ++row_index) {
            const CettaRadixDigitV1TableRow *row = &table->rows[row_index];
            if (row->input_len > 4u || row->output_len > 2u ||
                    row->source_rule_len > 32u)
                return false;
        }
    }
    for (index = 0u; index < program->instruction_len; ++index) {
        CettaRadixDigitV1InstructionKind kind;
        if (!cetta_radix_digit_v1_instruction_valid(profile,
                &program->instructions[index], program->instruction_len))
            return false;
        if (!cetta_radix_digit_v1_target_constructor(profile,
                program->instructions[index].constructor_term_index, &kind) ||
                (kind == CETTA_RADIX_DIGIT_V1_LOOKUP &&
                    program->instructions[index].table_index >= program->table_len))
            return false;
    }
    return true;
}

bool cetta_radix_digit_v1_program_equal(
    const CettaRadixDigitV1Program *left,
    const CettaRadixDigitV1Program *right) {
    uint32_t index;
    if (!left || !right || left->instruction_len != right->instruction_len ||
            left->table_len != right->table_len)
        return false;
    for (index = 0u; index < left->table_len; ++index) {
        uint32_t row_index;
        if (left->tables[index].row_len != right->tables[index].row_len)
            return false;
        for (row_index = 0u; row_index < left->tables[index].row_len; ++row_index) {
            const CettaRadixDigitV1TableRow *a = &left->tables[index].rows[row_index];
            const CettaRadixDigitV1TableRow *b = &right->tables[index].rows[row_index];
            if (a->input_len != b->input_len ||
                    a->output_len != b->output_len ||
                    a->source_rule_len != b->source_rule_len ||
                    memcmp(a->inputs, b->inputs,
                        (size_t)a->input_len * sizeof(*a->inputs)) != 0 ||
                    memcmp(a->outputs, b->outputs,
                        (size_t)a->output_len * sizeof(*a->outputs)) != 0 ||
                    memcmp(a->source_rule_indices, b->source_rule_indices,
                        (size_t)a->source_rule_len *
                            sizeof(*a->source_rule_indices)) != 0)
                return false;
        }
    }
    for (index = 0u; index < left->instruction_len; ++index) {
        const CettaRadixDigitV1Instruction *a = &left->instructions[index];
        const CettaRadixDigitV1Instruction *b = &right->instructions[index];
        if (a->constructor_term_index != b->constructor_term_index ||
                a->argument_len != b->argument_len ||
                a->table_index != b->table_index ||
                memcmp(a->arguments, b->arguments,
                    (size_t)a->argument_len * sizeof(*a->arguments)) != 0)
            return false;
    }
    return true;
}

const CettaRadixDigitV1TableRow *cetta_radix_digit_v1_find_row(
    const CettaRadixDigitV1Table *table,
    const uint32_t *inputs,
    uint32_t input_len,
    uint32_t *row_index) {
    uint32_t index;
    if (!table || (!inputs && input_len != 0u))
        return NULL;
    for (index = 0u; index < table->row_len; ++index) {
        const CettaRadixDigitV1TableRow *row = &table->rows[index];
        if (row->input_len == input_len &&
                (input_len == 0u || memcmp(row->inputs, inputs,
                    (size_t)input_len * sizeof(*inputs)) == 0)) {
            if (row_index)
                *row_index = index;
            return row;
        }
    }
    return NULL;
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

bool cetta_radix_digit_v1_program_encode(
    CettaRadixDigitV1ProgramWire *out,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program) {
    CettaRadixDigitV1ProgramWire candidate;
    size_t word_len = CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT + 2u;
    size_t cursor;
    uint32_t index;
    uint32_t argument;
    uint32_t table_index;
    uint32_t row_index;
    if (!out || !cetta_radix_digit_v1_program_valid(profile, program))
        return false;
    for (index = 0u; index < program->instruction_len; ++index) {
        size_t increment = 3u + program->instructions[index].argument_len;
        if (word_len > (SIZE_MAX - increment))
            return false;
        word_len += increment;
    }
    for (table_index = 0u; table_index < program->table_len; ++table_index) {
        if (word_len == SIZE_MAX)
            return false;
        ++word_len;
        for (row_index = 0u;
                row_index < program->tables[table_index].row_len; ++row_index) {
            const CettaRadixDigitV1TableRow *row =
                &program->tables[table_index].rows[row_index];
            size_t increment = 3u + row->input_len + row->output_len +
                row->source_rule_len;
            if (word_len > SIZE_MAX - increment)
                return false;
            word_len += increment;
        }
    }
    if (word_len > (SIZE_MAX - sizeof(radix_digit_program_magic)) / 4u)
        return false;
    cetta_radix_digit_v1_program_wire_init(&candidate);
    candidate.len = sizeof(radix_digit_program_magic) + word_len * 4u;
    candidate.bytes = malloc(candidate.len);
    if (!candidate.bytes)
        return false;
    memcpy(candidate.bytes, radix_digit_program_magic, sizeof(radix_digit_program_magic));
    cursor = sizeof(radix_digit_program_magic);
    for (index = 0u; index < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++index) {
        put_u32(candidate.bytes + cursor,
            profile->instruction_term_indices[index]);
        cursor += 4u;
    }
    put_u32(candidate.bytes + cursor, program->instruction_len);
    cursor += 4u;
    put_u32(candidate.bytes + cursor, program->table_len);
    cursor += 4u;
    for (table_index = 0u; table_index < program->table_len; ++table_index) {
        const CettaRadixDigitV1Table *table = &program->tables[table_index];
        put_u32(candidate.bytes + cursor, table->row_len);
        cursor += 4u;
        for (row_index = 0u; row_index < table->row_len; ++row_index) {
            const CettaRadixDigitV1TableRow *row = &table->rows[row_index];
            put_u32(candidate.bytes + cursor, row->input_len);
            cursor += 4u;
            for (argument = 0u; argument < row->input_len; ++argument) {
                put_u32(candidate.bytes + cursor, row->inputs[argument]);
                cursor += 4u;
            }
            put_u32(candidate.bytes + cursor, row->output_len);
            cursor += 4u;
            for (argument = 0u; argument < row->output_len; ++argument) {
                put_u32(candidate.bytes + cursor, row->outputs[argument]);
                cursor += 4u;
            }
            put_u32(candidate.bytes + cursor, row->source_rule_len);
            cursor += 4u;
            for (argument = 0u; argument < row->source_rule_len; ++argument) {
                put_u32(candidate.bytes + cursor,
                    row->source_rule_indices[argument]);
                cursor += 4u;
            }
        }
    }
    for (index = 0u; index < program->instruction_len; ++index) {
        const CettaRadixDigitV1Instruction *instruction = &program->instructions[index];
        put_u32(candidate.bytes + cursor, instruction->constructor_term_index);
        cursor += 4u;
        put_u32(candidate.bytes + cursor, instruction->table_index);
        cursor += 4u;
        put_u32(candidate.bytes + cursor, instruction->argument_len);
        cursor += 4u;
        for (argument = 0u; argument < instruction->argument_len; ++argument) {
            put_u32(candidate.bytes + cursor, instruction->arguments[argument]);
            cursor += 4u;
        }
    }
    if (cursor != candidate.len) {
        cetta_radix_digit_v1_program_wire_free(&candidate);
        return false;
    }
    cetta_radix_digit_v1_program_wire_free(out);
    *out = candidate;
    return true;
}

static bool take_u32(
    const uint8_t *bytes,
    size_t len,
    size_t *cursor,
    uint32_t *value) {
    if (*cursor > len || len - *cursor < 4u)
        return false;
    *value = get_u32(bytes + *cursor);
    *cursor += 4u;
    return true;
}

bool cetta_radix_digit_v1_program_decode(
    CettaRadixDigitV1Program *out,
    const CettaRadixDigitV1TargetProfile *profile,
    const uint8_t *bytes,
    size_t len) {
    CettaRadixDigitV1Program candidate;
    size_t cursor = sizeof(radix_digit_program_magic);
    uint32_t embedded_index;
    uint32_t index;
    uint32_t argument;
    uint32_t table_index;
    uint32_t row_index;
    if (!out || !bytes || len < sizeof(radix_digit_program_magic) ||
            memcmp(bytes, radix_digit_program_magic, sizeof(radix_digit_program_magic)) != 0 ||
            !cetta_radix_digit_v1_target_profile_valid(profile))
        return false;
    cetta_radix_digit_v1_program_init(&candidate);
    for (index = 0u; index < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++index)
        if (!take_u32(bytes, len, &cursor, &embedded_index) ||
                embedded_index != profile->instruction_term_indices[index])
            return false;
    if (!take_u32(bytes, len, &cursor, &candidate.instruction_len) ||
            candidate.instruction_len == 0u ||
            candidate.instruction_len > (len - cursor) / 12u ||
            !take_u32(bytes, len, &cursor, &candidate.table_len) ||
            candidate.table_len > (len - cursor) / 4u)
        return false;
    if (candidate.table_len != 0u) {
        candidate.tables = calloc(candidate.table_len,
            sizeof(*candidate.tables));
        if (!candidate.tables)
            return false;
    }
    for (table_index = 0u; table_index < candidate.table_len; ++table_index) {
        CettaRadixDigitV1Table *table = &candidate.tables[table_index];
        if (!take_u32(bytes, len, &cursor, &table->row_len) ||
                table->row_len > (len - cursor) / 12u) {
            cetta_radix_digit_v1_program_free(&candidate);
            return false;
        }
        if (table->row_len != 0u) {
            table->rows = calloc(table->row_len, sizeof(*table->rows));
            if (!table->rows) {
                cetta_radix_digit_v1_program_free(&candidate);
                return false;
            }
        }
        for (row_index = 0u; row_index < table->row_len; ++row_index) {
            CettaRadixDigitV1TableRow *row = &table->rows[row_index];
            if (!take_u32(bytes, len, &cursor, &row->input_len) ||
                    row->input_len > 4u)
                goto malformed;
            for (argument = 0u; argument < row->input_len; ++argument)
                if (!take_u32(bytes, len, &cursor, &row->inputs[argument]))
                    goto malformed;
            if (!take_u32(bytes, len, &cursor, &row->output_len) ||
                    row->output_len > 2u)
                goto malformed;
            for (argument = 0u; argument < row->output_len; ++argument)
                if (!take_u32(bytes, len, &cursor, &row->outputs[argument]))
                    goto malformed;
            if (!take_u32(bytes, len, &cursor, &row->source_rule_len) ||
                    row->source_rule_len > 32u)
                goto malformed;
            for (argument = 0u; argument < row->source_rule_len; ++argument)
                if (!take_u32(bytes, len, &cursor,
                        &row->source_rule_indices[argument]))
                    goto malformed;
        }
    }
    candidate.instructions = calloc(candidate.instruction_len,
        sizeof(*candidate.instructions));
    if (!candidate.instructions) {
        cetta_radix_digit_v1_program_free(&candidate);
        return false;
    }
    for (index = 0u; index < candidate.instruction_len; ++index) {
        CettaRadixDigitV1Instruction *instruction = &candidate.instructions[index];
        if (!take_u32(bytes, len, &cursor,
                &instruction->constructor_term_index) ||
                !take_u32(bytes, len, &cursor, &instruction->table_index) ||
                !take_u32(bytes, len, &cursor, &instruction->argument_len) ||
                instruction->argument_len > 12u) {
            cetta_radix_digit_v1_program_free(&candidate);
            return false;
        }
        for (argument = 0u; argument < instruction->argument_len; ++argument)
            if (!take_u32(bytes, len, &cursor,
                    &instruction->arguments[argument])) {
                cetta_radix_digit_v1_program_free(&candidate);
                return false;
            }
    }
    if (cursor != len || !cetta_radix_digit_v1_program_valid(profile, &candidate)) {
        cetta_radix_digit_v1_program_free(&candidate);
        return false;
    }
    cetta_radix_digit_v1_program_free(out);
    *out = candidate;
    return true;

malformed:
    cetta_radix_digit_v1_program_free(&candidate);
    return false;
}
