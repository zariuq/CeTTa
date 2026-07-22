#ifndef CETTA_GSLT2PARSE_PARSER_TERM_PROJECTION_V1_H
#define CETTA_GSLT2PARSE_PARSER_TERM_PROJECTION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/*
 * A projection plan is compiled presentation data.  It identifies neutral
 * ParserPack node/list/text shapes, constructor child roles, and target Atom
 * heads.  The projector contains no guest-language names or fixed arities.
 *
 * Projection is deliberately two-stage: neutral trees first become a scoped
 * internal term, then that term is materialized into host Atoms.  Bound-name
 * indices therefore determine collection ordering before display spellings or
 * fresh runtime identities are introduced.
 */
typedef struct {
    void *implementation;
} PPTermProjectionV1Plan;

void ppterm_projection_v1_plan_init(PPTermProjectionV1Plan *plan);
void ppterm_projection_v1_plan_free(PPTermProjectionV1Plan *plan);

bool ppterm_projection_v1_plan_load(
    PPTermProjectionV1Plan *plan,
    const Atom *compiled_plan,
    char *error_buf,
    size_t error_buf_size);

bool ppterm_projection_v1_project_result(
    const PPTermProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

#endif
