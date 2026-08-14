#ifndef CETTA_GSLT_TWO_PHASE_FRAME_MACHINE_V1_H
#define CETTA_GSLT_TWO_PHASE_FRAME_MACHINE_V1_H

#include "gslt_epoch_slots_v1.h"
#include "gslt_literal_hole_program_v1.h"
#include "gslt_u32_slice_arena_v1.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t stack_offset;
    uint32_t slot;
    uint32_t type_head;
} CettaGsltTwoPhaseBindV1;

typedef struct {
    uint32_t stack_offset;
    CettaGsltLiteralHeadProgramV1 pattern;
} CettaGsltTwoPhaseMatchV1;

typedef struct {
    uint32_t left_slot;
    uint32_t right_slot;
} CettaGsltTwoPhaseApartV1;

typedef struct {
    const CettaGsltTwoPhaseBindV1 *binds;
    uint32_t bind_len;
    const CettaGsltTwoPhaseMatchV1 *matches;
    uint32_t match_len;
    const CettaGsltTwoPhaseApartV1 *apart;
    uint32_t apart_len;
    const CettaGsltLiteralHeadProgramV1 *conclusion;
    uint32_t stack_arity;
    uint32_t slot_len;
} CettaGsltTwoPhaseFrameProgramV1;

typedef struct {
    const CettaGsltTwoPhaseBindV1 *binds;
    uint32_t bind_len;
    const CettaGsltTwoPhaseMatchV1 *matches;
    uint32_t match_len;
    const CettaGsltTwoPhaseApartV1 *apart;
    uint32_t apart_len;
    CettaGsltLiteralHeadProgramV1 conclusion;
    uint32_t stack_arity;
    uint32_t slot_len;
    bool ready;
} CettaGsltTwoPhaseFrameAdmissionV1;

typedef enum {
    CETTA_GSLT_TWO_PHASE_FRAME_OK_V1 = 0,
    CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1 = 1,
    CETTA_GSLT_TWO_PHASE_FRAME_RESOURCE_V1 = 2,
    CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1 = 3
} CettaGsltTwoPhaseFrameResultV1;

typedef CettaGsltTwoPhaseFrameResultV1
(*CettaGsltTwoPhaseApartCheckV1)(
    void *context,
    uint32_t left_slot, const uint32_t *left, uint32_t left_len,
    uint32_t right_slot, const uint32_t *right, uint32_t right_len);

typedef struct {
    void *context;
    CettaGsltTwoPhaseApartCheckV1 check_apart;
} CettaGsltTwoPhaseFrameAlgebraV1;

bool cetta_gslt_two_phase_frame_program_validate_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program);

bool cetta_gslt_two_phase_frame_program_admit_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    CettaGsltTwoPhaseFrameAdmissionV1 *admission_out);

CettaGsltTwoPhaseFrameResultV1
cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    const CettaGsltTwoPhaseFrameAlgebraV1 *algebra,
    CettaGsltEpochSlotsV1 *slots,
    CettaGsltU32SliceArenaV1 *arena,
    const CettaGsltU32SliceV1 *stack,
    uint32_t stack_len,
    uint32_t *stack_base_out,
    CettaGsltU32SliceV1 *result_out);

CettaGsltTwoPhaseFrameResultV1
cetta_gslt_two_phase_frame_machine_execute_admitted_v1(
    const CettaGsltTwoPhaseFrameAdmissionV1 *admission,
    const CettaGsltTwoPhaseFrameAlgebraV1 *algebra,
    CettaGsltEpochSlotsV1 *slots,
    CettaGsltU32SliceArenaV1 *arena,
    const CettaGsltU32SliceV1 *stack,
    uint32_t stack_len,
    uint32_t *stack_base_out,
    CettaGsltU32SliceV1 *result_out);

#endif
