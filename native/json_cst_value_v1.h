#ifndef CETTA_JSON_CST_VALUE_V1_H
#define CETTA_JSON_CST_VALUE_V1_H

#include "atom.h"
#include "json_elaboration_plan_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_JSON_CST_VALUE_V1_OK = 0,
    CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT,
    CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
    CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
    CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
    CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE,
    CETTA_JSON_CST_VALUE_V1_AMBIGUOUS
} CettaJsonCstValueV1Status;

/*
 * Elaborate the neutral CstRuleV1 tree produced by the authored RFC 8259
 * LanguageDef into ordinary MeTTa data:
 *
 *   JsonNullV1
 *   (JsonBoolV1 True|False)
 *   (JsonStringV1 ((cp n) ...))
 *   (JsonNumberV1 "exact-source-lexeme")
 *   (JsonArrayV1 (value ...))
 *   (JsonObjectV1
 *     ((JsonMemberV1 occurrence key value
 *        (JsonSourceSpanV1 start stop)) ...))
 *
 * Scalar lists make every JSON string representable, including U+0000.
 * Object order, duplicate occurrences, and exact half-open member spans are
 * retained.  Escaped surrogate
 * pairs are decoded; isolated surrogates are a semantic-profile rejection,
 * even though the RFC grammar admits their source spelling syntactically.
 * Failure is atomic with respect to both the output pointer and destination
 * arena.
 */
bool cetta_json_cst_value_v1_elaborate(
    const CettaJsonElaborationPlanV1 *plan,
    Arena *arena,
    Atom *json_text_cst,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonCstValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Project a nonempty family of retained syntax trees to one value only if
 * every tree elaborates successfully to that exact value. Equality includes
 * number text, member order, duplicate occurrences and source spans. Syntax
 * occurrences remain in the caller's forest; this does not select a parse.
 * All candidates share one work budget. Equivalent temporary values are
 * released after comparison. Failure preserves the output and arena. */
bool cetta_json_cst_value_v1_elaborate_family(
    const CettaJsonElaborationPlanV1 *plan,
    Arena *arena,
    Atom *const *json_text_csts,
    size_t cst_count,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonCstValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_json_cst_value_v1_status_name(
    CettaJsonCstValueV1Status status);

#endif /* CETTA_JSON_CST_VALUE_V1_H */
