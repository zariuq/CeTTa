#ifndef CETTA_GSLT2PARSE_RELATIONAL_STATE_PROGRAM_V1_H
#define CETTA_GSLT2PARSE_RELATIONAL_STATE_PROGRAM_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "parser_occurrence_fold_v1.h"
#include "gslt_header_hypothesis_policy_v1.h"
#include "parser_occurrence_source_resolver_v1.h"
#include "relational_store_v1.h"
#include "relational_stack_proof_v1.h"

enum { PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY = 4u };

typedef enum {
    PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT = 0,
    PPRELATIONAL_STATE_LIFETIME_V1_SCOPED = 1,
    PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL = 2
} PPRelationalStateLifetimeV1;

typedef enum {
    PPRELATIONAL_STATE_WRITE_V1_REQUIRE_KEY_ABSENT = 0,
    PPRELATIONAL_STATE_WRITE_V1_INSERT_OR_REQUIRE_EQUAL = 1
} PPRelationalStateWriteV1;

typedef enum {
    PPRELATIONAL_STATE_OPERAND_V1_INPUT = 0,
    PPRELATIONAL_STATE_OPERAND_V1_LITERAL = 1,
    PPRELATIONAL_STATE_OPERAND_V1_ROLE_AT = 2,
    PPRELATIONAL_STATE_OPERAND_V1_ROLE_LIST = 3,
    PPRELATIONAL_STATE_OPERAND_V1_SOURCE_COLUMN = 4,
    PPRELATIONAL_STATE_OPERAND_V1_SOURCE_ROW_INDEX = 5,
    PPRELATIONAL_STATE_OPERAND_V1_SOURCE_MATCH_INDEX = 6
} PPRelationalStateOperandKindV1;

typedef enum {
    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_DEPTH = 0,
    PPRELATIONAL_STATE_ACTION_V1_PUSH_SCOPE = 1,
    PPRELATIONAL_STATE_ACTION_V1_POP_SCOPE = 2,
    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH = 3,
    PPRELATIONAL_STATE_ACTION_V1_NOOP = 4,
    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH = 5,
    PPRELATIONAL_STATE_ACTION_V1_INSERT_UNORDERED_PAIRS = 6,
    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_EACH_KEY_ABSENT = 7,
    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW = 8,
    PPRELATIONAL_STATE_ACTION_V1_REQUIRE_ROW_KEY_ABSENT = 9,
    PPRELATIONAL_STATE_ACTION_V1_INSERT_ROW = 10,
    PPRELATIONAL_STATE_ACTION_V1_INSERT_EACH_MATCHING = 11,
    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW = 12,
    PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_NORMAL = 13,
    PPRELATIONAL_STATE_ACTION_V1_CHECK_PROOF_COMPRESSED = 14,
    PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE = 15,
    PPRELATIONAL_STATE_ACTION_V1_COPY_EACH_ROW_MATCHING = 16
} PPRelationalStateActionKindV1;

typedef enum {
    PPRELATIONAL_STATE_FAILURE_V1_NONE = 0,
    PPRELATIONAL_STATE_FAILURE_V1_REJECTED = 1,
    PPRELATIONAL_STATE_FAILURE_V1_UNSUPPORTED = 2,
    PPRELATIONAL_STATE_FAILURE_V1_RESOURCE = 3,
    PPRELATIONAL_STATE_FAILURE_V1_INVALID = 4
} PPRelationalStateFailureV1;

typedef struct {
    char *name;
    uint32_t arity;
    uint32_t key_arity;
    PPRelationalStateLifetimeV1 lifetime;
} PPRelationalStateTableV1;

typedef struct {
    PPRelationalStateOperandKindV1 kind;
    uint32_t role_id;
    uint32_t input_index;
    uint8_t *literal;
    uint32_t literal_len;
} PPRelationalStateOperandV1;

typedef struct {
    uint8_t *bytes;
    uint32_t len;
} PPRelationalStateLiteralV1;

typedef struct {
    char *name;
    uint32_t label_kind_table;
    uint32_t formula_table;
    uint32_t binder_variable_table;
    uint32_t mandatory_variable_table;
    uint32_t assertion_hypothesis_table;
    uint32_t assertion_disjoint_table;
    uint32_t active_hypothesis_table;
    uint32_t active_disjoint_table;
    uint32_t symbol_kind_table;
    PPRelationalStateLiteralV1 binder_hypothesis_kind;
    PPRelationalStateLiteralV1 matching_hypothesis_kind;
    PPRelationalStateLiteralV1 rule_kind_first;
    PPRelationalStateLiteralV1 rule_kind_second;
    PPRelationalStateLiteralV1 variable_symbol_kind;
    PPRelationalStateLiteralV1 unknown_token;
    uint8_t terminal_low;
    uint8_t terminal_high;
    uint8_t continuation_low;
    uint8_t continuation_high;
    uint8_t save_byte;
    uint8_t unknown_byte;
    uint32_t terminal_radix;
    uint32_t terminal_digit_bias;
    uint32_t continuation_radix;
    uint32_t continuation_digit_bias;
    PPRelationalStackProofV1UnknownPolicy unknown_policy;
    CettaGsltIndexedSavePlacementV1 save_placement;
    CettaGsltHeaderHypothesisPolicyV1 header_hypothesis_policy;
} PPRelationalStateProofMachineV1;

typedef struct {
    PPRelationalStateActionKindV1 kind;
    uint32_t operation_id;
    uint32_t role_id;
    uint32_t table_id;
    uint32_t source_table_id;
    uint32_t condition_table_id;
    PPRelationalStateWriteV1 write_policy;
    uint32_t required_depth;
    uint32_t proof_machine_id;
    uint32_t proof_label_role_id;
    uint32_t proof_formula_role_id;
    uint32_t proof_step_role_id;
    uint32_t proof_code_role_id;
    bool skip_completed_sources;
    bool reject_active_source_cycles;
    uint32_t operand_len;
    PPRelationalStateOperandV1
        operands[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
    uint32_t condition_operand_len;
    PPRelationalStateOperandV1
        condition_operands[PPRELATIONAL_STATE_PROGRAM_V1_MAX_ARITY];
} PPRelationalStateActionV1;

typedef struct {
    uint32_t action_begin;
    uint32_t action_len;
} PPRelationalStateOperationV1;

typedef struct {
    PPRelationalStateTableV1 *tables;
    PPRelationalStateProofMachineV1 *proof_machines;
    PPRelationalStateActionV1 *actions;
    PPRelationalStateActionV1 *final_actions;
    PPRelationalStateOperationV1 *operations;
    uint32_t table_len;
    uint32_t proof_machine_len;
    uint32_t action_len;
    uint32_t final_action_len;
    uint32_t operation_len;
    char occurrence_fold_plan_digest[65];
    char compiler_answer_digest[65];
    char plan_digest[65];
} PPRelationalStateProgramV1Plan;

void pprelational_state_program_v1_plan_init(
    PPRelationalStateProgramV1Plan *plan);

void pprelational_state_program_v1_plan_free(
    PPRelationalStateProgramV1Plan *plan);

/*
 * Compiler normal forms:
 *
 *   (state-table-v1 TABLE ARITY KEY-ARITY LIFETIME)
 *   (state-proof-machine-v1 MACHINE CONFIGURATION)
 *   (state-action-v1 OPERATION INDEX ACTION)
 *   (state-final-action-v1 INDEX ACTION)
 */
bool pprelational_state_program_v1_plan_build(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    Atom *const *records,
    size_t record_len,
    const char *compiler_answer_digest,
    PPRelationalStateProgramV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool pprelational_state_program_v1_plan_validate(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

bool pprelational_state_program_v1_operation_resolves_source(
    const PPRelationalStateProgramV1Plan *plan,
    uint32_t operation_id);

bool pprelational_state_program_v1_emit_c(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    uint32_t step_len;
    uint32_t action_len;
    uint32_t input_len;
    uint32_t row_len;
    uint32_t maximum_scope_depth;
    uint32_t verified_proof_len;
    uint32_t incomplete_proof_len;
    PPRelationalStateFailureV1 failure;
    bool committed;
    char state_digest[65];
    char source_digest[65];
} PPRelationalStateProgramV1Receipt;

typedef enum {
    PPRELATIONAL_STATE_OBSERVATION_V1_COUNTERS_ONLY = 0,
    PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT = 1
} PPRelationalStateObservationV1;

typedef enum {
    PPRELATIONAL_STATE_PROOF_V1_VERIFIED = 0,
    PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE = 1,
    PPRELATIONAL_STATE_PROOF_V1_REJECTED = 2,
    PPRELATIONAL_STATE_PROOF_V1_UNSUPPORTED = 3,
    PPRELATIONAL_STATE_PROOF_V1_RESOURCE = 4,
    PPRELATIONAL_STATE_PROOF_V1_INVALID = 5
} PPRelationalStateProofV1Result;

typedef struct {
    const PPRelationalStoreV1 *store;
    const PPRelationalStateProgramV1Plan *state_plan;
    const char *operation;
    uint32_t action_index;
    const char *header_role;
    const char *code_role;
    uint32_t proof_machine_id;
    bool compressed;
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *proof;
    uint32_t proof_len;
    const PPRelationalValueV1Slice *code;
    uint32_t code_len;
} PPRelationalStateProofV1Request;

typedef PPRelationalStateProofV1Result
(*PPRelationalStateProofV1Execute)(
    void *context,
    const PPRelationalStateProofV1Request *request,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    void *context;
    PPRelationalStateProofV1Execute execute;
} PPRelationalStateProofV1Backend;

typedef struct {
    void *implementation;
    PPRelationalStateProgramV1Receipt receipt;
    bool active;
    bool committed;
    bool aborted;
} PPRelationalStateProgramV1Run;

bool pprelational_state_program_v1_run_init(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size);

bool pprelational_state_program_v1_run_init_with_proof_backend(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    const PPRelationalStateProofV1Backend *proof_backend,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size);

/* Executes the generated proof machines through admitted persistent-frame
 * caches.  Admissions must be produced by a native-type/storage decoder for
 * this exact state plan; the runtime does not infer persistence from table
 * names or guest vocabulary. */
bool pprelational_state_program_v1_run_init_with_frame_cache(
    PPRelationalStateProgramV1Run *run,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *plan,
    const PPOccurrenceSourceResolverV1 *source_resolver,
    const PPOccurrenceFoldV1Backend *nested_backend,
    const PPRelationalStackProofV1CacheAdmission *admissions,
    uint32_t admission_len,
    PPRelationalStateObservationV1 observation,
    char *error_buf,
    size_t error_buf_size);

void pprelational_state_program_v1_run_free(
    PPRelationalStateProgramV1Run *run);

PPOccurrenceFoldV1Backend pprelational_state_program_v1_backend(
    PPRelationalStateProgramV1Run *run);

/* Exposes the same generic store populated by the generated state program.
 * Consumers still obtain table identities and roles from their generated
 * plans; this function does not assign meaning to any table. */
bool pprelational_state_program_v1_store(
    PPRelationalStateProgramV1Run *run,
    PPRelationalStoreV1 *store_out);

bool pprelational_state_program_v1_frame_cache_stats(
    const PPRelationalStateProgramV1Run *run,
    uint32_t proof_machine_id,
    PPRelationalStackProofV1CacheStats *stats_out);

#endif
