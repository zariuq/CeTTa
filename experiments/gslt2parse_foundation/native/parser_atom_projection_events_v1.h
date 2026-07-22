#ifndef CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_EVENTS_V1_H
#define CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_EVENTS_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_atom_projection_v1.h"

/*
 * Dense projection events are the deforested execution form of a compiled
 * semantic projection.  Rule and wrapper operands index the immutable
 * projection plan; scalar operands are Unicode scalar values.  Structural
 * events have operand zero.
 */
typedef enum {
    PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN = 0,
    PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END = 1,
    PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN = 2,
    PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END = 3,
    PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN = 4,
    PPATOM_PROJECTION_EVENTS_V1_LEAF_END = 5,
    PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN = 6,
    PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END = 7,
    PPATOM_PROJECTION_EVENTS_V1_SCALAR = 8
} PPAtomProjectionEventsV1Kind;

typedef struct {
    PPAtomProjectionEventsV1Kind kind;
    uint32_t operand;
} PPAtomProjectionEventsV1Event;

typedef struct {
    void *implementation;
} PPAtomProjectionEventsV1Builder;

void ppatom_projection_events_v1_builder_init(
    PPAtomProjectionEventsV1Builder *builder);

/* An unfinished build is rolled back before its resources are released. */
void ppatom_projection_events_v1_builder_free(
    PPAtomProjectionEventsV1Builder *builder);

/*
 * Bind starts one atomic document build at the current arena mark.  The plan
 * must remain loaded until finish or failure.  Rebinding aborts an unfinished
 * build first.
 */
bool ppatom_projection_events_v1_builder_bind(
    PPAtomProjectionEventsV1Builder *builder,
    const PPAtomProjectionV1Plan *plan,
    Arena *output_arena,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size);

/* Any invalid event aborts the build and rolls the arena back atomically. */
bool ppatom_projection_events_v1_builder_emit(
    PPAtomProjectionEventsV1Builder *builder,
    PPAtomProjectionEventsV1Event event,
    char *error_buf,
    size_t error_buf_size);

/*
 * Finish succeeds only after exactly one closed document.  The returned
 * array and every Atom belong to the bound arena.  A successful finish
 * commits the arena allocations; freeing the builder will not roll them back.
 */
bool ppatom_projection_events_v1_builder_finish(
    PPAtomProjectionEventsV1Builder *builder,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Reference bridge from the canonical neutral semantic result to the same
 * dense event ABI.  This pins event meaning against ordinary action replay;
 * specialized cursors may emit the events directly and compare at this seam.
 */
bool ppatom_projection_events_v1_project_document(
    const PPAtomProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size);

#endif
