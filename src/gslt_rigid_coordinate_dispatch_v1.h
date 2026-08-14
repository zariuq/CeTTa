#ifndef CETTA_GSLT_RIGID_COORDINATE_DISPATCH_V1_H
#define CETTA_GSLT_RIGID_COORDINATE_DISPATCH_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_RIGID_KEY_SYMBOL_V1 = 1,
    CETTA_GSLT_RIGID_KEY_STRING_V1 = 2,
    CETTA_GSLT_RIGID_KEY_INTEGER_V1 = 3,
    CETTA_GSLT_RIGID_KEY_APPLICATION_V1 = 4,
} CettaGsltRigidKeyKindV1;

typedef struct {
    CettaGsltRigidKeyKindV1 kind;
    uint32_t symbol;
    uint32_t arity;
    int64_t integer;
    const char *text;
} CettaGsltRigidKeyV1;

typedef bool (*CettaGsltRigidCoordinateKeyAtV1)(
    void *context, uint32_t occurrence, uint32_t coordinate,
    CettaGsltRigidKeyV1 *key_out);

typedef struct {
    CettaGsltRigidKeyV1 key;
    uint32_t *positions;
    uint32_t position_count;
    uint32_t position_capacity;
} CettaGsltRigidCoordinateGroupV1;

typedef struct {
    uint32_t coordinate;
    uint32_t occurrence_count;
    uint32_t *wildcard_positions;
    uint32_t wildcard_count;
    uint32_t wildcard_capacity;
    CettaGsltRigidCoordinateGroupV1 *groups;
    uint32_t group_count;
    uint32_t group_capacity;
    bool admitted;
} CettaGsltRigidCoordinateIndexV1;

/* One resettable count table may serve every independently compiled outer
 * bucket.  Its contents are private; capacity and allocation count remain
 * visible so generated-runtime tests can enforce reuse. */
typedef struct {
    void *slots;
    size_t slot_capacity;
    size_t allocation_count;
} CettaGsltRigidCoordinateScratchV1;

void cetta_gslt_rigid_coordinate_scratch_init_v1(
    CettaGsltRigidCoordinateScratchV1 *scratch);

void cetta_gslt_rigid_coordinate_scratch_free_v1(
    CettaGsltRigidCoordinateScratchV1 *scratch);

void cetta_gslt_rigid_coordinate_index_init_v1(
    CettaGsltRigidCoordinateIndexV1 *index);

void cetta_gslt_rigid_coordinate_index_free_v1(
    CettaGsltRigidCoordinateIndexV1 *index);

/* Choose the earliest positive maximum coordinate, then compile exact-key
 * groups plus wildcard occurrences in original source order. */
bool cetta_gslt_rigid_coordinate_index_build_v1(
    CettaGsltRigidCoordinateIndexV1 *index,
    CettaGsltRigidCoordinateScratchV1 *scratch,
    uint32_t arity, uint32_t occurrence_count,
    CettaGsltRigidCoordinateKeyAtV1 key_at, void *context);

/* `true` means the admitted index answered the lookup, including an empty
 * answer.  `false` requests the caller's complete source-order fallback. */
bool cetta_gslt_rigid_coordinate_index_positions_v1(
    const CettaGsltRigidCoordinateIndexV1 *index,
    const CettaGsltRigidKeyV1 *query_key,
    const uint32_t **positions_out, uint32_t *position_count_out);

/* Closed generic key vocabulary shared by compiled rule machines.  A
 * variable or an application with a non-symbol head has no key. */
bool cetta_gslt_rigid_key_from_atom_v1(
    const Atom *atom, CettaGsltRigidKeyV1 *key_out);

#endif /* CETTA_GSLT_RIGID_COORDINATE_DISPATCH_V1_H */
