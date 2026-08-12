#include "gslt_literal_hole_program_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { CETTA_GSLT_LITERAL_HOLE_INITIAL_CAPACITY_V1 = 8u };

static bool cetta_gslt_literal_hole_reserve_v1(
    void **items, uint32_t *capacity, uint32_t required,
    size_t item_size) {
    uint32_t cap;
    void *grown;

    if (!items || !capacity || item_size == 0u)
        return false;
    if (required <= *capacity)
        return true;
    cap = *capacity != 0u
              ? *capacity
              : CETTA_GSLT_LITERAL_HOLE_INITIAL_CAPACITY_V1;
    while (cap < required) {
        if (cap > UINT32_MAX / 2u) {
            cap = required;
            break;
        }
        cap *= 2u;
    }
    if ((size_t)cap > SIZE_MAX / item_size)
        return false;
    grown = realloc(*items, (size_t)cap * item_size);
    if (!grown)
        return false;
    *items = grown;
    *capacity = cap;
    return true;
}

void cetta_gslt_literal_hole_program_init_v1(
    CettaGsltLiteralHoleProgramV1 *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_gslt_literal_hole_program_free_v1(
    CettaGsltLiteralHoleProgramV1 *program) {
    if (!program)
        return;
    free(program->parts);
    free(program->literal_items);
    cetta_gslt_literal_hole_program_init_v1(program);
}

static bool cetta_gslt_literal_hole_push_part_v1(
    CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltLiteralHolePartV1 part) {
    if (!program || program->part_len == UINT32_MAX ||
        !cetta_gslt_literal_hole_reserve_v1(
            (void **)&program->parts, &program->part_cap,
            program->part_len + 1u, sizeof(*program->parts)))
        return false;
    program->parts[program->part_len++] = part;
    return true;
}

static bool cetta_gslt_literal_hole_push_literal_item_v1(
    CettaGsltLiteralHoleProgramV1 *program, uint32_t literal) {
    if (!program || program->literal_len == UINT32_MAX ||
        !cetta_gslt_literal_hole_reserve_v1(
            (void **)&program->literal_items, &program->literal_cap,
            program->literal_len + 1u, sizeof(*program->literal_items)))
        return false;
    program->literal_items[program->literal_len++] = literal;
    return true;
}

bool cetta_gslt_literal_hole_program_append_literal_v1(
    CettaGsltLiteralHoleProgramV1 *program, uint32_t literal,
    bool coalesce) {
    CettaGsltLiteralHolePartV1 *last;

    if (!program || program->cell_len == UINT32_MAX)
        return false;
    last = program->part_len != 0u
               ? &program->parts[program->part_len - 1u]
               : NULL;
    if (!coalesce || !last || last->literal_len == 0u) {
        if (!cetta_gslt_literal_hole_push_part_v1(
                program, (CettaGsltLiteralHolePartV1){literal, 1u}))
            return false;
    } else if (last->literal_len == 1u) {
        uint32_t offset = program->literal_len;
        if (!cetta_gslt_literal_hole_push_literal_item_v1(
                program, last->value) ||
            !cetta_gslt_literal_hole_push_literal_item_v1(
                program, literal)) {
            program->literal_len = offset;
            return false;
        }
        last = &program->parts[program->part_len - 1u];
        last->value = offset;
        last->literal_len = 2u;
    } else {
        if (last->literal_len == UINT32_MAX ||
            last->value > program->literal_len ||
            last->literal_len != program->literal_len - last->value ||
            !cetta_gslt_literal_hole_push_literal_item_v1(
                program, literal))
            return false;
        last = &program->parts[program->part_len - 1u];
        last->literal_len++;
    }
    program->cell_len++;
    return true;
}

bool cetta_gslt_literal_hole_program_append_hole_v1(
    CettaGsltLiteralHoleProgramV1 *program, uint32_t hole) {
    if (!program || program->cell_len == UINT32_MAX ||
        !cetta_gslt_literal_hole_push_part_v1(
            program, (CettaGsltLiteralHolePartV1){hole, 0u}))
        return false;
    program->cell_len++;
    return true;
}

bool cetta_gslt_literal_hole_program_validate_v1(
    const CettaGsltLiteralHoleProgramV1 *program) {
    uint32_t cells = 0u;
    uint32_t literal_cursor = 0u;
    uint32_t index;

    if (!program || (program->part_len != 0u && !program->parts) ||
        (program->literal_len != 0u && !program->literal_items) ||
        program->part_len > program->part_cap ||
        program->literal_len > program->literal_cap)
        return false;
    for (index = 0u; index < program->part_len; index++) {
        const CettaGsltLiteralHolePartV1 *part = &program->parts[index];
        uint32_t width = part->literal_len == 0u ? 1u : part->literal_len;
        if (width > UINT32_MAX - cells)
            return false;
        cells += width;
        if (part->literal_len > 1u) {
            if (part->value != literal_cursor ||
                part->value > program->literal_len ||
                part->literal_len > program->literal_len - part->value)
                return false;
            literal_cursor += part->literal_len;
        }
    }
    return cells == program->cell_len &&
           literal_cursor == program->literal_len;
}

static bool cetta_gslt_literal_hole_part_items_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    const CettaGsltLiteralHolePartV1 *part,
    const uint32_t **items_out, uint32_t *len_out) {
    if (!program || !part || !items_out || !len_out ||
        part->literal_len == 0u)
        return false;
    if (part->literal_len == 1u) {
        *items_out = &part->value;
        *len_out = 1u;
        return true;
    }
    if (!program->literal_items || part->value > program->literal_len ||
        part->literal_len > program->literal_len - part->value)
        return false;
    *items_out = &program->literal_items[part->value];
    *len_out = part->literal_len;
    return true;
}

static bool cetta_gslt_literal_hole_resolve_part_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    const CettaGsltLiteralHolePartV1 *part,
    CettaGsltHoleLookupV1 lookup, void *context,
    const uint32_t **items_out, uint32_t *len_out) {
    if (part && part->literal_len != 0u)
        return cetta_gslt_literal_hole_part_items_v1(
            program, part, items_out, len_out);
    return part && lookup && lookup(
        context, part->value, items_out, len_out) &&
        (*len_out == 0u || *items_out != NULL);
}

bool cetta_gslt_literal_hole_program_match_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    const uint32_t *actual, uint32_t actual_len) {
    uint32_t read = 0u;
    uint32_t index;

    if (!program || (actual_len != 0u && !actual))
        return false;
    for (index = 0u; index < program->part_len; index++) {
        const uint32_t *items = NULL;
        uint32_t len = 0u;
        if (!cetta_gslt_literal_hole_resolve_part_v1(
                program, &program->parts[index], lookup, context,
                &items, &len) ||
            read > actual_len || len > actual_len - read ||
            (len != 0u &&
             memcmp(items, &actual[read],
                    (size_t)len * sizeof(*items)) != 0))
            return false;
        read += len;
    }
    return read == actual_len;
}

bool cetta_gslt_literal_hole_program_measure_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *len_out) {
    uint32_t total = 0u;
    uint32_t index;

    if (!program || !len_out)
        return false;
    for (index = 0u; index < program->part_len; index++) {
        const uint32_t *items = NULL;
        uint32_t len = 0u;
        if (!cetta_gslt_literal_hole_resolve_part_v1(
                program, &program->parts[index], lookup, context,
                &items, &len) ||
            len > UINT32_MAX - total)
            return false;
        total += len;
    }
    *len_out = total;
    return true;
}

bool cetta_gslt_literal_hole_program_write_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *output, uint32_t output_len) {
    uint32_t write = 0u;
    uint32_t index;

    if (!program || (output_len != 0u && !output))
        return false;
    for (index = 0u; index < program->part_len; index++) {
        const uint32_t *items = NULL;
        uint32_t len = 0u;
        if (!cetta_gslt_literal_hole_resolve_part_v1(
                program, &program->parts[index], lookup, context,
                &items, &len) ||
            write > output_len || len > output_len - write)
            return false;
        if (len != 0u) {
            memcpy(&output[write], items,
                   (size_t)len * sizeof(*items));
            write += len;
        }
    }
    return write == output_len;
}

void cetta_gslt_literal_head_program_init_v1(
    CettaGsltLiteralHeadProgramV1 *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_gslt_literal_head_program_free_v1(
    CettaGsltLiteralHeadProgramV1 *program) {
    if (!program)
        return;
    cetta_gslt_literal_hole_program_free_v1(&program->residual);
    cetta_gslt_literal_head_program_init_v1(program);
}

bool cetta_gslt_literal_head_program_set_head_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t head) {
    if (!program || program->has_head ||
        program->residual.cell_len != 0u ||
        program->residual.part_len != 0u ||
        program->residual.literal_len != 0u)
        return false;
    program->head = head;
    program->has_head = true;
    return true;
}

bool cetta_gslt_literal_head_program_append_literal_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t literal,
    bool coalesce) {
    return program &&
           cetta_gslt_literal_hole_program_append_literal_v1(
               &program->residual, literal, coalesce);
}

bool cetta_gslt_literal_head_program_append_hole_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t hole) {
    return program &&
           cetta_gslt_literal_hole_program_append_hole_v1(
               &program->residual, hole);
}

bool cetta_gslt_literal_head_program_validate_v1(
    const CettaGsltLiteralHeadProgramV1 *program) {
    return program &&
           cetta_gslt_literal_hole_program_validate_v1(
               &program->residual) &&
           (!program->has_head ||
            program->residual.cell_len != UINT32_MAX);
}

bool cetta_gslt_literal_head_program_match_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    const uint32_t *actual, uint32_t actual_len) {
    if (!cetta_gslt_literal_head_program_validate_v1(program) ||
        (actual_len != 0u && !actual))
        return false;
    if (!program->has_head)
        return cetta_gslt_literal_hole_program_match_v1(
            &program->residual, lookup, context, actual, actual_len);
    return actual_len != 0u && actual[0] == program->head &&
           cetta_gslt_literal_hole_program_match_v1(
               &program->residual, lookup, context,
               actual_len > 1u ? &actual[1] : NULL,
               actual_len - 1u);
}

bool cetta_gslt_literal_head_program_measure_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *len_out) {
    uint32_t residual_len = 0u;

    if (!len_out ||
        !cetta_gslt_literal_head_program_validate_v1(program) ||
        !cetta_gslt_literal_hole_program_measure_v1(
            &program->residual, lookup, context, &residual_len) ||
        (program->has_head && residual_len == UINT32_MAX))
        return false;
    *len_out = residual_len + (program->has_head ? 1u : 0u);
    return true;
}

bool cetta_gslt_literal_head_program_write_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *output, uint32_t output_len) {
    if (!cetta_gslt_literal_head_program_validate_v1(program) ||
        (output_len != 0u && !output))
        return false;
    if (!program->has_head)
        return cetta_gslt_literal_hole_program_write_v1(
            &program->residual, lookup, context, output, output_len);
    if (output_len == 0u)
        return false;
    output[0] = program->head;
    return cetta_gslt_literal_hole_program_write_v1(
        &program->residual, lookup, context,
        output_len > 1u ? &output[1] : NULL, output_len - 1u);
}
