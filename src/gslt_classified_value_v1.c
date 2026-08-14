#include "gslt_classified_value_v1.h"

bool cetta_gslt_classified_value_init_v1(
    CettaGsltClassifiedValueV1 *out,
    uint32_t value,
    uint32_t classification,
    uint32_t maximum_classification) {
    if (!out || classification == 0u ||
        classification > maximum_classification)
        return false;
    *out = (CettaGsltClassifiedValueV1){
        .value = value,
        .classification = classification,
    };
    return true;
}

bool cetta_gslt_classified_value_validate_v1(
    const CettaGsltClassifiedValueV1 *classified,
    uint32_t maximum_classification) {
    return classified && classified->classification != 0u &&
           classified->classification <= maximum_classification;
}

bool cetta_gslt_classified_value_from_cases_v1(
    CettaGsltClassifiedValueV1 *out,
    uint32_t value,
    uint32_t source,
    const CettaGsltClassificationCaseV1 *cases,
    uint32_t case_len,
    uint32_t maximum_classification) {
    uint32_t classification = 0u;
    uint32_t index;
    bool found = false;

    if (!out || (case_len != 0u && !cases))
        return false;
    for (index = 0u; index < case_len; index++) {
        if (cases[index].source != source)
            continue;
        if (found)
            return false;
        found = true;
        classification = cases[index].classification;
    }
    return found && cetta_gslt_classified_value_init_v1(
                        out, value, classification,
                        maximum_classification);
}
