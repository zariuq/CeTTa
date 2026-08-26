#ifndef CETTA_C_SUBSET_IR_V1_H
#define CETTA_C_SUBSET_IR_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Small operational C-subset target available to future presentation-driven
 * lowering.  It models a caller-owned exact-integer ABI, guarded external
 * calls, and all observable completion classes.  It is target syntax, not a
 * source-to-target compiler, emitted C, or a claim about any concrete GMP
 * implementation.
 */

typedef enum {
    CETTA_C_SUBSET_V1_EXACT_INTEGER = 0
} CettaCSubsetV1ValueType;

typedef enum {
    CETTA_C_SUBSET_V1_BORROWED_INPUT = 0,
    CETTA_C_SUBSET_V1_BORROWED_MUTABLE_OUTPUT
} CettaCSubsetV1SlotMode;

typedef struct {
    CettaCSubsetV1ValueType type;
    CettaCSubsetV1SlotMode mode;
} CettaCSubsetV1Slot;

typedef struct {
    char *link_name;
    uint32_t first_input_slot;
    uint32_t second_input_slot;
    uint32_t output_slot;
} CettaCSubsetV1BinaryExternal;

typedef enum {
    CETTA_C_SUBSET_V1_BRANCH_ZERO = 0,
    CETTA_C_SUBSET_V1_CALL_BINARY_EXTERNAL,
    CETTA_C_SUBSET_V1_RETURN_VALUE,
    CETTA_C_SUBSET_V1_RETURN_DECLINED,
    CETTA_C_SUBSET_V1_RETURN_LANGUAGE_FAULT,
    CETTA_C_SUBSET_V1_RETURN_ENGINE_FAULT,
    CETTA_C_SUBSET_V1_RETURN_RESOURCE_FAULT
} CettaCSubsetV1InstructionKind;

typedef struct {
    CettaCSubsetV1InstructionKind kind;
    union {
        struct {
            uint32_t slot;
            uint32_t zero_target;
            uint32_t nonzero_target;
        } branch_zero;
        struct {
            uint32_t external;
            uint32_t value_target;
            uint32_t language_fault_target;
            uint32_t engine_fault_target;
            uint32_t resource_fault_target;
        } call_binary;
    } as;
} CettaCSubsetV1Instruction;

typedef struct {
    char *semantic_name;
    char *entry_link_name;
    CettaCSubsetV1Slot *slots;
    uint32_t slot_len;
    CettaCSubsetV1BinaryExternal *externals;
    uint32_t external_len;
    CettaCSubsetV1Instruction *instructions;
    uint32_t instruction_len;
    uint32_t entry_instruction;
} CettaCSubsetV1Program;

typedef struct {
    CettaCSubsetV1Program *programs;
    uint32_t program_len;
} CettaCSubsetV1Module;

typedef enum {
    CETTA_C_SUBSET_V1_VALIDATE_OK = 0,
    CETTA_C_SUBSET_V1_VALIDATE_BAD_ARGUMENT,
    CETTA_C_SUBSET_V1_VALIDATE_MALFORMED_PROGRAM,
    CETTA_C_SUBSET_V1_VALIDATE_RESOURCE_LIMIT,
    CETTA_C_SUBSET_V1_VALIDATE_ALLOCATION_FAILURE
} CettaCSubsetV1ValidateStatus;

typedef enum {
    CETTA_C_SUBSET_V1_EXTERNAL_VALUE = 0,
    CETTA_C_SUBSET_V1_EXTERNAL_LANGUAGE_FAULT,
    CETTA_C_SUBSET_V1_EXTERNAL_ENGINE_FAULT,
    CETTA_C_SUBSET_V1_EXTERNAL_RESOURCE_FAULT
} CettaCSubsetV1ExternalOutcome;

typedef enum {
    CETTA_C_SUBSET_V1_OUTCOME_VALUE = 0,
    CETTA_C_SUBSET_V1_OUTCOME_DECLINED,
    CETTA_C_SUBSET_V1_OUTCOME_LANGUAGE_FAULT,
    CETTA_C_SUBSET_V1_OUTCOME_ENGINE_FAULT,
    CETTA_C_SUBSET_V1_OUTCOME_RESOURCE_FAULT
} CettaCSubsetV1Outcome;

typedef bool (*CettaCSubsetV1IsZeroFn)(void *context, const void *value);

typedef CettaCSubsetV1ExternalOutcome (*CettaCSubsetV1BinaryExternalFn)(
    void *context,
    const char *link_name,
    const void *first,
    const void *second,
    void *output);

typedef struct {
    void *context;
    CettaCSubsetV1IsZeroFn is_zero;
    CettaCSubsetV1BinaryExternalFn call_binary;
} CettaCSubsetV1ExecutionProvider;

typedef enum {
    CETTA_C_SUBSET_V1_EVENT_STEP = 0,
    CETTA_C_SUBSET_V1_EVENT_EXTERNAL
} CettaCSubsetV1EventKind;

typedef struct {
    CettaCSubsetV1EventKind kind;
    uint32_t instruction;
    uint32_t external;
    CettaCSubsetV1ExternalOutcome external_outcome;
} CettaCSubsetV1ExecutionEvent;

typedef struct {
    CettaCSubsetV1Outcome outcome;
    uint32_t step_count;
    uint32_t external_call_count;
    uint32_t final_instruction;
    CettaCSubsetV1ExecutionEvent *events;
    uint32_t event_count;
} CettaCSubsetV1ExecutionReceipt;

typedef enum {
    CETTA_C_SUBSET_V1_EXECUTE_OK = 0,
    CETTA_C_SUBSET_V1_EXECUTE_BAD_ARGUMENT,
    CETTA_C_SUBSET_V1_EXECUTE_INVALID_PROGRAM,
    CETTA_C_SUBSET_V1_EXECUTE_STEP_LIMIT,
    CETTA_C_SUBSET_V1_EXECUTE_PROVIDER_FAILURE,
    CETTA_C_SUBSET_V1_EXECUTE_ALLOCATION_FAILURE
} CettaCSubsetV1ExecuteStatus;

void cetta_c_subset_v1_program_init(CettaCSubsetV1Program *program);
void cetta_c_subset_v1_program_free(CettaCSubsetV1Program *program);
void cetta_c_subset_v1_module_init(CettaCSubsetV1Module *module);
void cetta_c_subset_v1_module_free(CettaCSubsetV1Module *module);
void cetta_c_subset_v1_execution_receipt_init(
    CettaCSubsetV1ExecutionReceipt *receipt);
void cetta_c_subset_v1_execution_receipt_free(
    CettaCSubsetV1ExecutionReceipt *receipt);

bool cetta_c_subset_v1_validate_program(
    const CettaCSubsetV1Program *program,
    uint32_t work_limit,
    CettaCSubsetV1ValidateStatus *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_c_subset_v1_validate_module(
    const CettaCSubsetV1Module *module,
    uint32_t work_limit,
    CettaCSubsetV1ValidateStatus *status,
    char *error_buf,
    size_t error_buf_size);

/*
 * Reference interpretation of target syntax.  Successful execution atomically
 * replaces an initialized receipt with a chronological event sequence.  It
 * calls only target ABI providers and cannot establish their adequacy;
 * external correctness remains a separate realization obligation.
 */
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
    size_t error_buf_size);

const char *cetta_c_subset_v1_validate_status_name(
    CettaCSubsetV1ValidateStatus status);
const char *cetta_c_subset_v1_execute_status_name(
    CettaCSubsetV1ExecuteStatus status);

#endif /* CETTA_C_SUBSET_IR_V1_H */
