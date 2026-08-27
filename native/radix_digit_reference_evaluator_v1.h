#ifndef CETTA_RADIX_DIGIT_REFERENCE_EVALUATOR_V1_H
#define CETTA_RADIX_DIGIT_REFERENCE_EVALUATOR_V1_H

#include "radix_digit_target_program_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE = 0,
    CETTA_RADIX_DIGIT_V1_OUTCOME_LANGUAGE_FAULT,
    CETTA_RADIX_DIGIT_V1_OUTCOME_ENGINE_FAULT,
    CETTA_RADIX_DIGIT_V1_OUTCOME_RESOURCE_FAULT
} CettaRadixDigitV1OutcomeKind;

typedef enum {
    CETTA_RADIX_DIGIT_V1_FAULT_NONE = 0,
    CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT,
    CETTA_RADIX_DIGIT_V1_FAULT_SPARSE_WRITE,
    CETTA_RADIX_DIGIT_V1_FAULT_MISSING_TABLE_ROW,
    CETTA_RADIX_DIGIT_V1_FAULT_MISSING_PROGRAM_COUNTER,
    CETTA_RADIX_DIGIT_V1_FAULT_MISSING_BUFFER,
    CETTA_RADIX_DIGIT_V1_FAULT_MISSING_REGISTER,
    CETTA_RADIX_DIGIT_V1_FAULT_INCONSISTENT_BUFFER_WRITE,
    CETTA_RADIX_DIGIT_V1_FAULT_MALFORMED_TABLE_ROW,
    CETTA_RADIX_DIGIT_V1_FAULT_OUTPUT_LIMIT,
    CETTA_RADIX_DIGIT_V1_FAULT_FUEL_EXHAUSTED,
    CETTA_RADIX_DIGIT_V1_FAULT_ALLOCATION
} CettaRadixDigitV1FaultKind;

typedef enum {
    CETTA_RADIX_DIGIT_V1_EVENT_EXECUTE = 0,
    CETTA_RADIX_DIGIT_V1_EVENT_TABLE_ROW,
    CETTA_RADIX_DIGIT_V1_EVENT_FAULT
} CettaRadixDigitV1EventKind;

typedef struct {
    CettaRadixDigitV1EventKind kind;
    uint32_t pc;
    uint32_t table_index;
    uint32_t row_index;
    uint32_t source_rule_len;
    uint32_t source_rule_indices[32];
    CettaRadixDigitV1FaultKind fault;
    uint32_t fault_first;
    uint32_t fault_second;
} CettaRadixDigitV1Event;

typedef struct {
    CettaRadixDigitV1OutcomeKind kind;
    CettaRadixDigitV1FaultKind fault;
    uint32_t fault_first;
    uint32_t fault_second;
    uint32_t *digits;
    uint32_t digit_len;
    CettaRadixDigitV1Event *events;
    uint32_t event_len;
    uint32_t steps;
} CettaRadixDigitV1RunResult;

void cetta_radix_digit_v1_run_result_init(CettaRadixDigitV1RunResult *result);
void cetta_radix_digit_v1_run_result_free(CettaRadixDigitV1RunResult *result);

/*
 * Execute an owned RadixDigit graph over two least-significant-digit-first input
 * buffers.  Constructor identity is resolved through the target profile that
 * was derived from the supplied RadixDigit LanguageDef.  This is the independent
 * reference evaluator; emitted programs do not link it.
 */
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
    uint32_t fuel);

#endif /* CETTA_RADIX_DIGIT_REFERENCE_EVALUATOR_V1_H */
