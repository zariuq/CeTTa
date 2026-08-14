#ifndef CETTA_GSLT2PARSE_FIRST_ORDER_FRAME_DECODER_V1_H
#define CETTA_GSLT2PARSE_FIRST_ORDER_FRAME_DECODER_V1_H

#include "oslf_native_type_plan_v1.h"
#include "proof_storage_plan_v1.h"
#include "relational_state_program_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *machine;
    const char *owner;
    const char *base;
    const char *provable;
    const char *sequence_cons;
    const char *sequence_nil;
    const PPProofFiniteSupportPlanV1 *finite_support;
    const PPProofIndexedValuePlanV1 *indexed_value;
    const PPProofLiteralHolePlanV1 *literal_hole;
    bool two_phase_frame_admitted;
    const PPProofStoragePlanV1 *storage_plan;
    char native_type_digest[65];
    char storage_plan_digest[65];
} PPFirstOrderFrameDecoderV1;

bool ppfirst_order_frame_decoder_v1_admit(
    const PPProofStoragePlanV1 *storage_plan,
    const PPOSLFNativeTypePlanV1 *native_types,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    PPFirstOrderFrameDecoderV1 *decoder_out,
    char *error_buf,
    size_t error_buf_size);

bool ppfirst_order_frame_decoder_v1_validate_state_program(
    const PPFirstOrderFrameDecoderV1 *decoder,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    char *error_buf,
    size_t error_buf_size);

bool ppfirst_order_frame_decoder_v1_cache_admission(
    const PPFirstOrderFrameDecoderV1 *decoder,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    PPRelationalStackProofV1CacheAdmission *admission_out,
    char *error_buf,
    size_t error_buf_size);

#endif
