#ifndef CETTA_GSLT2PARSE_RELATIONAL_STACK_PROOF_V1_H
#define CETTA_GSLT2PARSE_RELATIONAL_STACK_PROOF_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "relational_store_v1.h"
#include "relational_value_list_v1.h"

typedef enum {
    PPRELATIONAL_STACK_PROOF_V1_OK = 0,
    PPRELATIONAL_STACK_PROOF_V1_REJECTED = 1,
    PPRELATIONAL_STACK_PROOF_V1_RESOURCE = 2,
    PPRELATIONAL_STACK_PROOF_V1_INVALID = 3,
    PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE = 4
} PPRelationalStackProofV1Result;

typedef enum {
    PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_REJECT = 0,
    PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM = 1
} PPRelationalStackProofV1UnknownPolicy;

typedef struct {
    uint32_t label_kind_table;
    uint32_t formula_table;
    uint32_t binder_variable_table;
    uint32_t mandatory_variable_table;
    uint32_t assertion_hypothesis_table;
    uint32_t assertion_disjoint_table;
    uint32_t active_hypothesis_table;
    uint32_t active_disjoint_table;
    uint32_t symbol_kind_table;
    uint32_t binder_hypothesis_kind;
    uint32_t matching_hypothesis_kind;
    uint32_t rule_kind_first;
    uint32_t rule_kind_second;
    uint32_t variable_symbol_kind;
    uint32_t unknown_token;
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
} PPRelationalStackProofV1Machine;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *steps;
    uint32_t step_len;
} PPRelationalStackProofV1NormalInput;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *header;
    uint32_t header_len;
    const PPRelationalValueV1Slice *code;
    uint32_t code_len;
} PPRelationalStackProofV1CompressedInput;

/* A cache admission is produced only after a generated native-type/storage
 * decoder has established that the frame-defining tables are persistent and
 * agree with the compiled state program.  The proof runtime validates the
 * table identities again before accepting the cache. */
typedef struct {
    uint32_t label_kind_table;
    uint32_t formula_table;
    uint32_t binder_variable_table;
    uint32_t mandatory_variable_table;
    uint32_t assertion_hypothesis_table;
    uint32_t assertion_disjoint_table;
    uint32_t symbol_kind_table;
    /* Derived from a generated proof-call region whose only exported
     * observation is a value verdict.  Without this admission, transaction
     * scratch must be released rather than retained for reuse. */
    bool scratch_reuse_admitted;
    /* Derived from the complete recursive support/apartness fragment in the
     * generated proof semantics.  It licenses dense support and apartness
     * carriers; absence selects the structural source check. */
    bool finite_support_admitted;
    /* Derived from a generated compressed-proof action whose finite header is
     * available before its indexed instruction stream.  Absence preserves the
     * source machine but forbids the cached prepared-table realization. */
    bool indexed_values_admitted;
    /* Derived from a flat literal-or-slot template whose literal runs are
     * immutable for the lifetime of the compiled frame. */
    bool literal_hole_admitted;
    char native_type_digest[65];
    char storage_plan_digest[65];
} PPRelationalStackProofV1CacheAdmission;

typedef struct {
    void *implementation;
} PPRelationalStackProofV1Cache;

typedef struct {
    uint64_t hit_len;
    uint64_t miss_len;
    uint64_t retained_source_len;
    uint64_t source_reuse_len;
    uint64_t template_cell_len;
    uint64_t template_part_len;
    uint64_t template_head_len;
    uint64_t scratch_acquire_len;
    uint64_t scratch_reuse_len;
    uint32_t entry_len;
    uint32_t lookup_index_capacity;
    uint32_t scratch_formula_capacity;
    uint32_t scratch_stack_capacity;
    uint32_t scratch_heap_capacity;
    uint32_t scratch_substitution_capacity;
    uint32_t scratch_support_width;
    uint32_t scratch_support_slot_capacity;
    uint64_t scratch_support_compute_len;
    uint64_t scratch_support_hit_len;
} PPRelationalStackProofV1CacheStats;

bool pprelational_stack_proof_v1_machine_validate(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    char *error_buf,
    size_t error_buf_size);

bool pprelational_stack_proof_v1_cache_init(
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1CacheAdmission *admission,
    char *error_buf,
    size_t error_buf_size);

void pprelational_stack_proof_v1_cache_free(
    PPRelationalStackProofV1Cache *cache);

bool pprelational_stack_proof_v1_cache_stats(
    const PPRelationalStackProofV1Cache *cache,
    PPRelationalStackProofV1CacheStats *stats_out);

PPRelationalStackProofV1Result pprelational_stack_proof_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1NormalInput *input,
    char *error_buf,
    size_t error_buf_size);

PPRelationalStackProofV1Result pprelational_stack_proof_v1_normal_cached(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1NormalInput *input,
    char *error_buf,
    size_t error_buf_size);

PPRelationalStackProofV1Result pprelational_stack_proof_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    const PPRelationalStackProofV1CompressedInput *input,
    char *error_buf,
    size_t error_buf_size);

PPRelationalStackProofV1Result pprelational_stack_proof_v1_compressed_cached(
    const PPRelationalStoreV1 *store,
    const PPRelationalStackProofV1Machine *machine,
    PPRelationalStackProofV1Cache *cache,
    const PPRelationalStackProofV1CompressedInput *input,
    char *error_buf,
    size_t error_buf_size);

#endif
