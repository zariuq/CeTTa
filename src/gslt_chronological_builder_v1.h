#ifndef CETTA_GSLT_CHRONOLOGICAL_BUILDER_V1_H
#define CETTA_GSLT_CHRONOLOGICAL_BUILDER_V1_H

#include "gslt_u32_index_v1.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1 = 0,
    CETTA_GSLT_CHRONOLOGICAL_NODE_REF_V1 = 1
} CettaGsltChronologicalRefKindV1;

typedef struct {
    CettaGsltChronologicalRefKindV1 kind;
    uint32_t value;
} CettaGsltChronologicalRefV1;

typedef bool (*CettaGsltChronologicalApplyV1)(
    void *context,
    uint32_t action,
    const uintptr_t *inputs,
    uint32_t input_len,
    uintptr_t *value_out);

typedef bool (*CettaGsltChronologicalEqualV1)(
    void *context,
    uintptr_t left,
    uintptr_t right);

typedef void (*CettaGsltChronologicalDiscardV1)(
    void *context,
    uintptr_t value);

typedef enum {
    CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1 = 0,
    CETTA_GSLT_CHRONOLOGICAL_DUPLICATE_V1 = 1,
    CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_ACTION_V1 = 2,
    CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_REFERENCE_V1 = 3,
    CETTA_GSLT_CHRONOLOGICAL_ACTION_REJECTED_V1 = 4,
    CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1 = 5,
    CETTA_GSLT_CHRONOLOGICAL_INVALID_V1 = 6
} CettaGsltChronologicalAppendResultV1;

typedef struct {
    uint32_t node_len;
    uint32_t root_id;
    uintptr_t root_value;
} CettaGsltChronologicalReceiptV1;

/* An action callback denotes a pure finite relation: failure rejects the
 * candidate, and success returns its unique value.  It must not mutate the
 * semantic inputs on which a later application depends. */
typedef struct {
    CettaGsltU32IndexV1 node_index;
    uintptr_t *premise_values;
    uint32_t premise_len;
    uint32_t premise_cap;
    uint32_t *node_ids;
    uint32_t node_id_cap;
    uintptr_t *node_values;
    uint32_t node_len;
    uint32_t node_value_cap;
    uintptr_t *input_scratch;
    uint32_t input_cap;
    CettaGsltChronologicalApplyV1 apply;
    CettaGsltChronologicalEqualV1 equal;
    CettaGsltChronologicalDiscardV1 discard;
    void *apply_context;
    bool begun;
} CettaGsltChronologicalBuilderV1;

void cetta_gslt_chronological_builder_init_v1(
    CettaGsltChronologicalBuilderV1 *builder);
void cetta_gslt_chronological_builder_free_v1(
    CettaGsltChronologicalBuilderV1 *builder);

bool cetta_gslt_chronological_builder_begin_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    const uintptr_t *premise_values,
    uint32_t premise_len,
    CettaGsltChronologicalApplyV1 apply,
    CettaGsltChronologicalEqualV1 equal,
    CettaGsltChronologicalDiscardV1 discard,
    void *apply_context);

/* Append one immutable premise value.  Existing premise coordinates are
 * stable, and failure leaves the premise sequence unchanged.  The builder
 * borrows the value; its owner must keep it valid until the next begin/free. */
bool cetta_gslt_chronological_builder_append_premise_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    uintptr_t premise_value,
    uint32_t *premise_index_out);

CettaGsltChronologicalAppendResultV1
cetta_gslt_chronological_builder_append_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    uint32_t node_id,
    uint32_t action,
    const CettaGsltChronologicalRefV1 *inputs,
    uint32_t input_len,
    uintptr_t *value_out);

/* Select one admitted action by exact key, then append it through the same
 * chronological boundary.  The immutable selector is constructed with the
 * append-only unique-index compiler. */
CettaGsltChronologicalAppendResultV1
cetta_gslt_chronological_builder_append_selected_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    const CettaGsltU32IndexV1 *selector,
    uint32_t selector_key,
    uint32_t node_id,
    const CettaGsltChronologicalRefV1 *inputs,
    uint32_t input_len,
    uintptr_t *value_out);

bool cetta_gslt_chronological_builder_finish_v1(
    const CettaGsltChronologicalBuilderV1 *builder,
    uint32_t root_id,
    uintptr_t target_value,
    CettaGsltChronologicalReceiptV1 *receipt_out);

bool cetta_gslt_chronological_builder_validate_v1(
    const CettaGsltChronologicalBuilderV1 *builder);

#endif
