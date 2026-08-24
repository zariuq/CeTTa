#ifndef CETTA_PRIME_LEVEL_H
#define CETTA_PRIME_LEVEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/* A Prime universe level is stored only in canonical form: a constant and a
 * sorted, key-distinct collection of parameter-plus-offset atoms.  Source
 * syntax may have const/parameter/successor/maximum structure, but clients
 * cannot manufacture a noncanonical admitted level value. */
typedef struct CettaPrimeLevelV1 CettaPrimeLevelV1;

typedef struct {
    uint64_t parameter;
    uint64_t offset;
} CettaPrimeLevelParameterOffsetV1;

typedef struct {
    uint64_t constant;
    const CettaPrimeLevelParameterOffsetV1 *parameters;
    size_t parameter_count;
} CettaPrimeLevelViewV1;

typedef enum {
    CETTA_PRIME_LEVEL_OK_V1 = 0,
    CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
    /* The mathematical level exists, but this finite C representation cannot
     * hold its natural number or working set.  Authorities must treat this as
     * incomplete computation, never as a semantic refutation. */
    CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1,
} CettaPrimeLevelStatusV1;

typedef CettaPrimeLevelStatusV1 (*CettaPrimeLevelSubstitutionV1)(
    void *context, uint64_t parameter,
    const CettaPrimeLevelV1 **replacement_out);

typedef CettaPrimeLevelStatusV1 (*CettaPrimeLevelValuationV1)(
    void *context, uint64_t parameter, uint64_t *value_out);

CettaPrimeLevelStatusV1 cetta_prime_level_constant_v1(
    Arena *owner, uint64_t constant, const CettaPrimeLevelV1 **level_out);

CettaPrimeLevelStatusV1 cetta_prime_level_parameter_v1(
    Arena *owner, uint64_t parameter, const CettaPrimeLevelV1 **level_out);

CettaPrimeLevelStatusV1 cetta_prime_level_successor_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelV1 **successor_out);

/* Add a natural offset in one operation.  This is successor iteration at the
 * admitted level, without constructing an offset-deep syntax tree. */
CettaPrimeLevelStatusV1 cetta_prime_level_offset_v1(
    Arena *owner, const CettaPrimeLevelV1 *level, uint64_t offset,
    const CettaPrimeLevelV1 **shifted_out);

CettaPrimeLevelStatusV1 cetta_prime_level_maximum_v1(
    Arena *owner, const CettaPrimeLevelV1 *left,
    const CettaPrimeLevelV1 *right,
    const CettaPrimeLevelV1 **maximum_out);

/* Simultaneous substitution of admitted levels for parameters.  The callback
 * is invoked exactly once for every distinct parameter in the source. */
CettaPrimeLevelStatusV1 cetta_prime_level_substitute_v1(
    Arena *owner, const CettaPrimeLevelV1 *source,
    CettaPrimeLevelSubstitutionV1 substitution, void *context,
    const CettaPrimeLevelV1 **result_out);

bool cetta_prime_level_equal_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right);

/* Semantic order under every valuation, decided by the finite canonical
 * criterion: constants at the zero valuation and same-parameter offsets. */
bool cetta_prime_level_le_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right);

CettaPrimeLevelStatusV1 cetta_prime_level_evaluate_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelValuationV1 valuation,
    void *context, uint64_t *value_out);

bool cetta_prime_level_view_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelViewV1 *view_out);

#endif /* CETTA_PRIME_LEVEL_H */
