#include "gslt_two_phase_frame_machine_v1.h"

#include <limits.h>

typedef struct {
    const CettaGsltEpochSlotsV1 *slots;
    const CettaGsltU32SliceArenaV1 *arena;
} CettaGsltTwoPhaseEnvironmentV1;

static bool two_phase_slice_items(
    const CettaGsltU32SliceArenaV1 *arena,
    CettaGsltU32SliceV1 slice,
    const uint32_t **items_out) {
    if (!arena || !items_out || slice.offset > arena->len ||
        slice.len > arena->len - slice.offset ||
        (slice.len != 0u && !arena->items))
        return false;
    *items_out = slice.len != 0u ? &arena->items[slice.offset] : NULL;
    return true;
}

static bool two_phase_stack_offset_unique(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    uint32_t offset, bool skip_bind, uint32_t skip_index) {
    uint32_t index;

    for (index = 0u; index < program->bind_len; index++) {
        if ((!skip_bind || index != skip_index) &&
            program->binds[index].stack_offset == offset)
            return false;
    }
    for (index = 0u; index < program->match_len; index++) {
        if ((skip_bind || index != skip_index) &&
            program->matches[index].stack_offset == offset)
            return false;
    }
    return true;
}

static bool two_phase_slot_unique(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    uint32_t slot, uint32_t skip_index) {
    uint32_t index;

    for (index = 0u; index < program->bind_len; index++) {
        if (index != skip_index && program->binds[index].slot == slot)
            return false;
    }
    return true;
}

bool cetta_gslt_two_phase_frame_program_validate_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program) {
    uint32_t index;

    if (!program || !program->conclusion ||
        (program->bind_len != 0u && !program->binds) ||
        (program->match_len != 0u && !program->matches) ||
        (program->apart_len != 0u && !program->apart) ||
        program->bind_len != program->slot_len ||
        program->bind_len > UINT32_MAX - program->match_len ||
        program->bind_len + program->match_len != program->stack_arity ||
        !cetta_gslt_literal_head_program_validate_v1(
            program->conclusion))
        return false;
    for (index = 0u; index < program->bind_len; index++) {
        const CettaGsltTwoPhaseBindV1 *bind = &program->binds[index];
        if (bind->stack_offset >= program->stack_arity ||
            bind->slot >= program->slot_len ||
            !two_phase_stack_offset_unique(
                program, bind->stack_offset, true, index) ||
            !two_phase_slot_unique(program, bind->slot, index))
            return false;
    }
    for (index = 0u; index < program->match_len; index++) {
        const CettaGsltTwoPhaseMatchV1 *match = &program->matches[index];
        if (match->stack_offset >= program->stack_arity ||
            !two_phase_stack_offset_unique(
                program, match->stack_offset, false, index) ||
            !cetta_gslt_literal_head_program_validate_v1(
                &match->pattern))
            return false;
    }
    for (index = 0u; index < program->apart_len; index++) {
        const CettaGsltTwoPhaseApartV1 *pair = &program->apart[index];
        if (pair->left_slot >= program->slot_len ||
            pair->right_slot >= program->slot_len ||
            pair->left_slot == pair->right_slot)
            return false;
    }
    return true;
}

bool cetta_gslt_two_phase_frame_program_admit_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    CettaGsltTwoPhaseFrameAdmissionV1 *admission_out) {
    if (!admission_out ||
        !cetta_gslt_two_phase_frame_program_validate_v1(program))
        return false;
    *admission_out = (CettaGsltTwoPhaseFrameAdmissionV1){
        .binds = program->binds,
        .bind_len = program->bind_len,
        .matches = program->matches,
        .match_len = program->match_len,
        .apart = program->apart,
        .apart_len = program->apart_len,
        .conclusion = *program->conclusion,
        .stack_arity = program->stack_arity,
        .slot_len = program->slot_len,
        .ready = true,
    };
    return true;
}

static bool two_phase_hole_lookup(
    void *raw_context, uint32_t hole,
    const uint32_t **items_out, uint32_t *len_out) {
    const CettaGsltTwoPhaseEnvironmentV1 *environment = raw_context;
    const CettaGsltU32SliceV1 *image;
    const uint32_t *items = NULL;

    if (!environment || !environment->slots || !environment->arena ||
        !items_out || !len_out ||
        !(image = cetta_gslt_epoch_slots_get_const_v1(
              environment->slots, hole)) ||
        !two_phase_slice_items(environment->arena, *image, &items))
        return false;
    *items_out = image->len != 0u ? items : NULL;
    *len_out = image->len;
    return true;
}

static CettaGsltTwoPhaseFrameResultV1 two_phase_execute_admitted(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    const CettaGsltTwoPhaseFrameAlgebraV1 *algebra,
    CettaGsltEpochSlotsV1 *slots,
    CettaGsltU32SliceArenaV1 *arena,
    const CettaGsltU32SliceV1 *stack,
    uint32_t stack_len,
    uint32_t *stack_base_out,
    CettaGsltU32SliceV1 *result_out) {
    CettaGsltTwoPhaseEnvironmentV1 environment = {slots, arena};
    uint32_t base;
    uint32_t result_len = 0u;
    uint32_t watermark;
    uint32_t index;

    if (!program || !slots || !arena || !stack_base_out || !result_out ||
        (stack_len != 0u && !stack) || slots->epoch == 0u ||
        slots->width != program->slot_len ||
        slots->value_size != sizeof(CettaGsltU32SliceV1) ||
        (program->apart_len != 0u &&
         (!algebra || !algebra->check_apart)))
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    if (stack_len < program->stack_arity)
        return CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1;
    base = stack_len - program->stack_arity;
    for (index = 0u; index < program->bind_len; index++) {
        const CettaGsltTwoPhaseBindV1 *bind = &program->binds[index];
        CettaGsltU32SliceV1 actual = stack[base + bind->stack_offset];
        const uint32_t *items = NULL;
        CettaGsltU32SliceV1 *image;

        if (!two_phase_slice_items(arena, actual, &items) ||
            actual.len == 0u || items[0] != bind->type_head)
            return CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1;
        image = cetta_gslt_epoch_slots_set_v1(slots, bind->slot);
        if (!image)
            return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
        *image = (CettaGsltU32SliceV1){
            .offset = actual.offset + 1u,
            .len = actual.len - 1u,
        };
    }
    for (index = 0u; index < program->match_len; index++) {
        const CettaGsltTwoPhaseMatchV1 *match = &program->matches[index];
        CettaGsltU32SliceV1 actual = stack[base + match->stack_offset];
        const uint32_t *items = NULL;

        if (!two_phase_slice_items(arena, actual, &items) ||
            !cetta_gslt_literal_head_program_match_v1(
                &match->pattern, two_phase_hole_lookup, &environment,
                items, actual.len))
            return CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1;
    }
    for (index = 0u; index < program->apart_len; index++) {
        const CettaGsltTwoPhaseApartV1 *pair = &program->apart[index];
        const CettaGsltU32SliceV1 *left =
            cetta_gslt_epoch_slots_get_const_v1(slots, pair->left_slot);
        const CettaGsltU32SliceV1 *right =
            cetta_gslt_epoch_slots_get_const_v1(slots, pair->right_slot);
        const uint32_t *left_items = NULL;
        const uint32_t *right_items = NULL;
        CettaGsltTwoPhaseFrameResultV1 checked;

        if (!left || !right ||
            !two_phase_slice_items(arena, *left, &left_items) ||
            !two_phase_slice_items(arena, *right, &right_items))
            return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
        checked = algebra->check_apart(
            algebra->context,
            pair->left_slot, left->len != 0u ? left_items : NULL,
            left->len,
            pair->right_slot, right->len != 0u ? right_items : NULL,
            right->len);
        if (checked != CETTA_GSLT_TWO_PHASE_FRAME_OK_V1)
            return checked;
    }
    if (!cetta_gslt_literal_head_program_measure_v1(
            program->conclusion, two_phase_hole_lookup, &environment,
            &result_len) || result_len == 0u)
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    watermark = cetta_gslt_u32_slice_arena_watermark_v1(arena);
    if (!cetta_gslt_u32_slice_arena_reserve_v1(
            arena, result_len, result_out))
        return CETTA_GSLT_TWO_PHASE_FRAME_RESOURCE_V1;
    environment.arena = arena;
    if (!cetta_gslt_literal_head_program_write_v1(
            program->conclusion, two_phase_hole_lookup, &environment,
            &arena->items[result_out->offset], result_len)) {
        (void)cetta_gslt_u32_slice_arena_reset_v1(arena, watermark);
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    }
    *stack_base_out = base;
    return CETTA_GSLT_TWO_PHASE_FRAME_OK_V1;
}

CettaGsltTwoPhaseFrameResultV1
cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
    const CettaGsltTwoPhaseFrameProgramV1 *program,
    const CettaGsltTwoPhaseFrameAlgebraV1 *algebra,
    CettaGsltEpochSlotsV1 *slots,
    CettaGsltU32SliceArenaV1 *arena,
    const CettaGsltU32SliceV1 *stack,
    uint32_t stack_len,
    uint32_t *stack_base_out,
    CettaGsltU32SliceV1 *result_out) {
    if (!cetta_gslt_two_phase_frame_program_validate_v1(program))
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    return two_phase_execute_admitted(
        program, algebra, slots, arena, stack, stack_len,
        stack_base_out, result_out);
}

CettaGsltTwoPhaseFrameResultV1
cetta_gslt_two_phase_frame_machine_execute_admitted_v1(
    const CettaGsltTwoPhaseFrameAdmissionV1 *admission,
    const CettaGsltTwoPhaseFrameAlgebraV1 *algebra,
    CettaGsltEpochSlotsV1 *slots,
    CettaGsltU32SliceArenaV1 *arena,
    const CettaGsltU32SliceV1 *stack,
    uint32_t stack_len,
    uint32_t *stack_base_out,
    CettaGsltU32SliceV1 *result_out) {
    CettaGsltTwoPhaseFrameProgramV1 program;

    if (!admission || !admission->ready)
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    program = (CettaGsltTwoPhaseFrameProgramV1){
        .binds = admission->binds,
        .bind_len = admission->bind_len,
        .matches = admission->matches,
        .match_len = admission->match_len,
        .apart = admission->apart,
        .apart_len = admission->apart_len,
        .conclusion = &admission->conclusion,
        .stack_arity = admission->stack_arity,
        .slot_len = admission->slot_len,
    };
    return two_phase_execute_admitted(
        &program, algebra, slots, arena, stack, stack_len,
        stack_base_out, result_out);
}
