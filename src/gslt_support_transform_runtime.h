#ifndef CETTA_GSLT_SUPPORT_TRANSFORM_RUNTIME_H
#define CETTA_GSLT_SUPPORT_TRANSFORM_RUNTIME_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1 = 1,
} CettaGsltSupportSchedulerV1;

typedef enum {
    CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT = 1,
} CettaGsltSupportUnsupportedPolicyV1;

typedef struct {
    const char *surface_symbol;
    uint32_t argument_count;
    const char *operator_id;
} CettaGsltSupportOperatorDeclV1;

typedef struct {
    uint32_t abi_version;
    const char *language_name;
    const char *profile_name;
    const char *manifest_sha256;
    const char *compiler_sha256;

    const char *work_symbol;
    uint32_t work_arity;
    uint32_t location_position;
    uint32_t input_position;
    uint32_t output_position;

    CettaGsltSupportSchedulerV1 scheduler;
    CettaGsltSupportUnsupportedPolicyV1 unsupported_policy;

    const char *compat_input_symbol;
    const char *compat_input_operator_id;
    const char *explicit_input_symbol;
    const CettaGsltSupportOperatorDeclV1 *source_declarations;
    size_t source_declaration_count;

    const char *compat_output_symbol;
    const char *compat_output_operator_id;
    const char *explicit_output_symbol;
    const CettaGsltSupportOperatorDeclV1 *sink_declarations;
    size_t sink_declaration_count;

    const uint8_t *physical_profile_packet;
    size_t physical_profile_packet_size;
} CettaGsltSupportTransformProfileV1;

typedef enum {
    CETTA_GSLT_SUPPORT_COMPLETED = 0,
    CETTA_GSLT_SUPPORT_EXPIRED = 1,
    CETTA_GSLT_SUPPORT_FAULT = 2,
} CettaGsltSupportOutcomeV1;

typedef struct {
    CettaGsltSupportOutcomeV1 outcome;
    Atom **atoms;
    size_t atom_count;
    uint64_t steps;
} CettaGsltSupportTransformResultV1;

bool cetta_gslt_support_transform_profile_validate_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size);

bool cetta_gslt_support_transform_run_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    Arena *arena, Atom *const *forms, size_t form_count, uint64_t fuel,
    CettaGsltSupportTransformResultV1 *result,
    char *error, size_t error_size);

void cetta_gslt_support_transform_result_free_v1(
    CettaGsltSupportTransformResultV1 *result);

#endif /* CETTA_GSLT_SUPPORT_TRANSFORM_RUNTIME_H */
