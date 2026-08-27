#ifndef CETTA_RADIX_DIGIT_TARGET_PROGRAM_V1_H
#define CETTA_RADIX_DIGIT_TARGET_PROGRAM_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Exact native representation of the Program carrier in RadixDigitMachine.
 * Constructor indices are obtained from the supplied RadixDigit LanguageDef.  A
 * program therefore contains target-language instructions, not a parallel
 * private opcode stream.
 */

typedef enum {
    CETTA_RADIX_DIGIT_V1_SET = 0,
    CETTA_RADIX_DIGIT_V1_COPY,
    CETTA_RADIX_DIGIT_V1_INCREMENT,
    CETTA_RADIX_DIGIT_V1_LENGTH,
    CETTA_RADIX_DIGIT_V1_READ_OR_ZERO,
    CETTA_RADIX_DIGIT_V1_WRITE,
    CETTA_RADIX_DIGIT_V1_LOOKUP,
    CETTA_RADIX_DIGIT_V1_BRANCH_LT,
    CETTA_RADIX_DIGIT_V1_BRANCH_EQ,
    CETTA_RADIX_DIGIT_V1_JUMP,
    CETTA_RADIX_DIGIT_V1_RETURN_BUFFER,
    CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT
} CettaRadixDigitV1InstructionKind;

typedef struct {
    uint32_t instruction_term_indices[CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT];
} CettaRadixDigitV1TargetProfile;

typedef struct {
    uint32_t constructor_term_index;
    uint32_t arguments[12];
    uint32_t argument_len;
    uint32_t table_index;
} CettaRadixDigitV1Instruction;

typedef struct {
    uint32_t input_len;
    uint32_t inputs[4];
    uint32_t output_len;
    uint32_t outputs[2];
    uint32_t source_rule_len;
    uint32_t source_rule_indices[32];
} CettaRadixDigitV1TableRow;

typedef struct {
    uint32_t row_len;
    CettaRadixDigitV1TableRow *rows;
} CettaRadixDigitV1Table;

typedef struct {
    uint32_t instruction_len;
    CettaRadixDigitV1Instruction *instructions;
    uint32_t table_len;
    CettaRadixDigitV1Table *tables;
} CettaRadixDigitV1Program;

typedef struct {
    uint8_t *bytes;
    size_t len;
} CettaRadixDigitV1ProgramWire;

void cetta_radix_digit_v1_program_init(CettaRadixDigitV1Program *program);
void cetta_radix_digit_v1_program_free(CettaRadixDigitV1Program *program);
void cetta_radix_digit_v1_program_wire_init(CettaRadixDigitV1ProgramWire *wire);
void cetta_radix_digit_v1_program_wire_free(CettaRadixDigitV1ProgramWire *wire);

bool cetta_radix_digit_v1_target_profile_valid(
    const CettaRadixDigitV1TargetProfile *profile);

bool cetta_radix_digit_v1_target_constructor(
    const CettaRadixDigitV1TargetProfile *profile,
    uint32_t constructor_term_index,
    CettaRadixDigitV1InstructionKind *kind_out);

bool cetta_radix_digit_v1_instruction_valid(
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Instruction *instruction,
    uint32_t instruction_len);

bool cetta_radix_digit_v1_program_valid(
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program);

bool cetta_radix_digit_v1_program_equal(
    const CettaRadixDigitV1Program *left,
    const CettaRadixDigitV1Program *right);

const CettaRadixDigitV1TableRow *cetta_radix_digit_v1_find_row(
    const CettaRadixDigitV1Table *table,
    const uint32_t *inputs,
    uint32_t input_len,
    uint32_t *row_index);

/*
 * The wire includes the complete target-constructor profile.  Decoding under
 * a different supplied RadixDigit presentation therefore fails rather than silently
 * reinterpreting constructor positions.
 */
bool cetta_radix_digit_v1_program_encode(
    CettaRadixDigitV1ProgramWire *out,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program);

bool cetta_radix_digit_v1_program_decode(
    CettaRadixDigitV1Program *out,
    const CettaRadixDigitV1TargetProfile *profile,
    const uint8_t *bytes,
    size_t len);

#endif /* CETTA_RADIX_DIGIT_TARGET_PROGRAM_V1_H */
