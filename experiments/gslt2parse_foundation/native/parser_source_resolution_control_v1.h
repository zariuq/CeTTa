#ifndef CETTA_GSLT2PARSE_PARSER_SOURCE_RESOLUTION_CONTROL_V1_H
#define CETTA_GSLT2PARSE_PARSER_SOURCE_RESOLUTION_CONTROL_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"

typedef enum {
    PPSOURCE_CONTROL_PROBE_V1_FRESH = 0,
    PPSOURCE_CONTROL_PROBE_V1_COMPLETED = 1,
    PPSOURCE_CONTROL_PROBE_V1_ACTIVE = 2,
    PPSOURCE_CONTROL_PROBE_V1_MISSING = 3,
    PPSOURCE_CONTROL_PROBE_V1_RESOURCE = 4,
    PPSOURCE_CONTROL_PROBE_V1_INVALID = 5,
    PPSOURCE_CONTROL_PROBE_V1_COUNT = 6
} PPSourceControlProbeV1;

typedef enum {
    PPSOURCE_CONTROL_DECISION_V1_EXPAND_FRESH = 0,
    PPSOURCE_CONTROL_DECISION_V1_SKIP_COMPLETED = 1,
    PPSOURCE_CONTROL_DECISION_V1_SKIP_ACTIVE = 2,
    PPSOURCE_CONTROL_DECISION_V1_REFUSE_COMPLETED = 3,
    PPSOURCE_CONTROL_DECISION_V1_REFUSE_ACTIVE = 4,
    PPSOURCE_CONTROL_DECISION_V1_REFUSE_MISSING = 5,
    PPSOURCE_CONTROL_DECISION_V1_RESOURCE = 6,
    PPSOURCE_CONTROL_DECISION_V1_INVALID = 7,
    PPSOURCE_CONTROL_DECISION_V1_COUNT = 8
} PPSourceControlDecisionV1;

typedef enum {
    PPSOURCE_CONTROL_CHILD_V1_ACCEPTED = 0,
    PPSOURCE_CONTROL_CHILD_V1_REFUSED = 1,
    PPSOURCE_CONTROL_CHILD_V1_RESOURCE = 2,
    PPSOURCE_CONTROL_CHILD_V1_INVALID = 3,
    PPSOURCE_CONTROL_CHILD_V1_NONE = 4,
    PPSOURCE_CONTROL_CHILD_V1_COUNT = 5
} PPSourceControlChildV1;

typedef enum {
    PPSOURCE_CONTROL_OUTCOME_V1_ACCEPTED = 0,
    PPSOURCE_CONTROL_OUTCOME_V1_REFUSED = 1,
    PPSOURCE_CONTROL_OUTCOME_V1_RESOURCE = 2,
    PPSOURCE_CONTROL_OUTCOME_V1_INVALID = 3,
    PPSOURCE_CONTROL_OUTCOME_V1_COUNT = 4
} PPSourceControlOutcomeV1;

enum {
    PPSOURCE_CONTROL_POLICY_V1_COUNT = 2,
    PPSOURCE_CONTROL_DECISION_CELL_V1_COUNT =
        PPSOURCE_CONTROL_PROBE_V1_COUNT *
        PPSOURCE_CONTROL_POLICY_V1_COUNT *
        PPSOURCE_CONTROL_POLICY_V1_COUNT,
    PPSOURCE_CONTROL_FINISH_CELL_V1_COUNT =
        PPSOURCE_CONTROL_DECISION_V1_COUNT *
        PPSOURCE_CONTROL_CHILD_V1_COUNT,
    PPSOURCE_CONTROL_ABSENT_V1 = 255
};

typedef struct {
    uint8_t decisions[PPSOURCE_CONTROL_DECISION_CELL_V1_COUNT];
    uint8_t outcomes[PPSOURCE_CONTROL_FINISH_CELL_V1_COUNT];
    char compiler_digest[65];
    char answer_set_digest[65];
    char plan_digest[65];
} PPSourceResolutionControlV1Plan;

void ppsource_resolution_control_v1_plan_init(
    PPSourceResolutionControlV1Plan *plan);

bool ppsource_resolution_control_v1_plan_build(
    Atom *const *answer_terms,
    size_t answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPSourceResolutionControlV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool ppsource_resolution_control_v1_plan_validate(
    const PPSourceResolutionControlV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

bool ppsource_resolution_control_v1_decide(
    const PPSourceResolutionControlV1Plan *plan,
    PPSourceControlProbeV1 probe,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    PPSourceControlDecisionV1 *out,
    char *error_buf,
    size_t error_buf_size);

bool ppsource_resolution_control_v1_finish(
    const PPSourceResolutionControlV1Plan *plan,
    PPSourceControlDecisionV1 decision,
    PPSourceControlChildV1 child,
    PPSourceControlOutcomeV1 *out,
    char *error_buf,
    size_t error_buf_size);

bool ppsource_resolution_control_v1_emit_c(
    const PPSourceResolutionControlV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

#endif
