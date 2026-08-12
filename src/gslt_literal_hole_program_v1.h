#ifndef CETTA_GSLT_LITERAL_HOLE_PROGRAM_V1_H
#define CETTA_GSLT_LITERAL_HOLE_PROGRAM_V1_H

#include <stdbool.h>
#include <stdint.h>

/* A part with literal_len == 0 is a hole whose index is value.  A part with
 * literal_len == 1 carries its literal inline in value.  Longer literal runs
 * use value as an offset into literal_items. */
typedef struct {
    uint32_t value;
    uint32_t literal_len;
} CettaGsltLiteralHolePartV1;

typedef struct {
    CettaGsltLiteralHolePartV1 *parts;
    uint32_t part_len;
    uint32_t part_cap;
    uint32_t *literal_items;
    uint32_t literal_len;
    uint32_t literal_cap;
    uint32_t cell_len;
} CettaGsltLiteralHoleProgramV1;

/* A literal head may be separated only while the residual program is empty.
 * Leaving has_head false represents the exact unspecialized program. */
typedef struct {
    CettaGsltLiteralHoleProgramV1 residual;
    uint32_t head;
    bool has_head;
} CettaGsltLiteralHeadProgramV1;

typedef bool (*CettaGsltHoleLookupV1)(
    void *context, uint32_t hole,
    const uint32_t **items_out, uint32_t *len_out);

void cetta_gslt_literal_hole_program_init_v1(
    CettaGsltLiteralHoleProgramV1 *program);
void cetta_gslt_literal_hole_program_free_v1(
    CettaGsltLiteralHoleProgramV1 *program);

bool cetta_gslt_literal_hole_program_append_literal_v1(
    CettaGsltLiteralHoleProgramV1 *program, uint32_t literal,
    bool coalesce);
bool cetta_gslt_literal_hole_program_append_hole_v1(
    CettaGsltLiteralHoleProgramV1 *program, uint32_t hole);
bool cetta_gslt_literal_hole_program_validate_v1(
    const CettaGsltLiteralHoleProgramV1 *program);

bool cetta_gslt_literal_hole_program_match_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    const uint32_t *actual, uint32_t actual_len);
bool cetta_gslt_literal_hole_program_measure_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *len_out);
bool cetta_gslt_literal_hole_program_write_v1(
    const CettaGsltLiteralHoleProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *output, uint32_t output_len);

void cetta_gslt_literal_head_program_init_v1(
    CettaGsltLiteralHeadProgramV1 *program);
void cetta_gslt_literal_head_program_free_v1(
    CettaGsltLiteralHeadProgramV1 *program);
bool cetta_gslt_literal_head_program_set_head_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t head);
bool cetta_gslt_literal_head_program_append_literal_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t literal,
    bool coalesce);
bool cetta_gslt_literal_head_program_append_hole_v1(
    CettaGsltLiteralHeadProgramV1 *program, uint32_t hole);
bool cetta_gslt_literal_head_program_validate_v1(
    const CettaGsltLiteralHeadProgramV1 *program);
bool cetta_gslt_literal_head_program_match_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    const uint32_t *actual, uint32_t actual_len);
bool cetta_gslt_literal_head_program_measure_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *len_out);
bool cetta_gslt_literal_head_program_write_v1(
    const CettaGsltLiteralHeadProgramV1 *program,
    CettaGsltHoleLookupV1 lookup, void *context,
    uint32_t *output, uint32_t output_len);

#endif
