#ifndef CETTA_GSLT_CLASSIFIED_VALUE_V1_H
#define CETTA_GSLT_CLASSIFIED_VALUE_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Compact physical carrier for a value paired with one admitted, nonzero
 * finite classification.  Classification zero is reserved for invalid or
 * unclassified input. */
typedef struct {
    uint32_t value;
    uint32_t classification;
} CettaGsltClassifiedValueV1;

typedef struct {
    uint32_t source;
    uint32_t classification;
} CettaGsltClassificationCaseV1;

bool cetta_gslt_classified_value_init_v1(
    CettaGsltClassifiedValueV1 *out,
    uint32_t value,
    uint32_t classification,
    uint32_t maximum_classification);

bool cetta_gslt_classified_value_validate_v1(
    const CettaGsltClassifiedValueV1 *classified,
    uint32_t maximum_classification);

/* Classify only when exactly one generated finite case matches the source.
 * Missing and duplicate cases are both rejected. */
bool cetta_gslt_classified_value_from_cases_v1(
    CettaGsltClassifiedValueV1 *out,
    uint32_t value,
    uint32_t source,
    const CettaGsltClassificationCaseV1 *cases,
    uint32_t case_len,
    uint32_t maximum_classification);

#endif
