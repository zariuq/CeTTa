#ifndef CETTA_GSLT2PARSE_OSLF_NATIVE_TYPE_PLAN_V1_H
#define CETTA_GSLT2PARSE_OSLF_NATIVE_TYPE_PLAN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *constructor;
    uint32_t arity;
} PPOSLFNativeHeadSignatureV1;

typedef enum {
    PPOSLF_NATIVE_TERM_SYMBOL_V1,
    PPOSLF_NATIVE_TERM_INTEGER_I64_V1,
    PPOSLF_NATIVE_TERM_INTEGER_BIG_V1,
    PPOSLF_NATIVE_TERM_STRING_V1,
    PPOSLF_NATIVE_TERM_VARIABLE_V1,
    PPOSLF_NATIVE_TERM_APPLICATION_V1,
} PPOSLFNativeTermKindV1;

/*
 * Terms are a compact, ownership-neutral realization of the reflected q-term
 * carrier.  Application children are indices in the plan's term_edges vector;
 * all text remains owned by the plan until pposlf_native_type_plan_v1_free.
 */
typedef struct {
    PPOSLFNativeTermKindV1 kind;
    const char *text;
    int64_t integer;
    uint32_t variable;
    uint32_t edge_begin;
    uint32_t edge_len;
} PPOSLFNativeTermV1;

typedef struct {
    const char *owner;
    const char *rule;
    uint32_t head;
    uint32_t body_begin;
    uint32_t body_len;
    uint32_t variable_count;
} PPOSLFNativeStepSchemaV1;

/*
 * Local dataflow facts derived solely from one admitted step schema.
 * body_variables_in_head means that a ground head match makes every body
 * goal ground.  head_linear means no head variable occurs more than once.
 */
typedef struct {
    bool body_variables_in_head;
    bool head_linear;
} PPOSLFNativeStepFlowV1;

typedef struct {
    PPOSLFNativeTermKindV1 kind;
    const char *text;
    int64_t integer;
    uint32_t step_begin;
    uint32_t step_len;
} PPOSLFNativeHeadGroupV1;

/*
 * A top-level premise key for which the composed program has no defining
 * rule.  This is a structural inventory, not permission to call native code:
 * an executable composition must close the relation with facts/rules or pair
 * it with a separately admitted extensional provider.
 */
typedef struct {
    PPOSLFNativeTermKindV1 kind;
    const char *text;
    int64_t integer;
    uint32_t arity;
    uint32_t body_occurrences;
} PPOSLFNativeOpenHeadV1;

/*
 * An authored declaration that a relation is supplied as finite extensional
 * rows at the execution boundary.  The declaration carries shape and source
 * provenance only.  It never selects a callback or native implementation.
 */
typedef struct {
    const char *owner;
    const char *relation;
    uint32_t arity;
    uint32_t body_occurrences;
} PPOSLFNativeExternalRelationV1;

typedef struct {
    PPOSLFNativeHeadSignatureV1 *head_signatures;
    uint32_t head_signature_len;
    PPOSLFNativeTermV1 *terms;
    uint32_t term_len;
    uint32_t *term_edges;
    uint32_t term_edge_len;
    uint32_t *body_roots;
    uint32_t body_root_len;
    PPOSLFNativeStepSchemaV1 *step_schemas;
    uint32_t step_schema_len;
    PPOSLFNativeHeadGroupV1 *head_groups;
    uint32_t head_group_len;
    uint32_t *head_step_indices;
    uint32_t head_step_index_len;
    PPOSLFNativeExternalRelationV1 *external_relations;
    uint32_t external_relation_len;
    PPOSLFNativeOpenHeadV1 *open_heads;
    uint32_t open_head_len;
    char semantic_digest[65];
    void *storage;
} PPOSLFNativeTypePlanV1;

void pposlf_native_type_plan_v1_init(PPOSLFNativeTypePlanV1 *plan);
void pposlf_native_type_plan_v1_free(PPOSLFNativeTypePlanV1 *plan);

bool pposlf_native_type_plan_v1_load(
    PPOSLFNativeTypePlanV1 *plan,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size);

bool pposlf_native_type_plan_v1_head_arity(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *constructor,
    uint32_t *arity_out);

bool pposlf_native_type_plan_v1_step_range(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *owner,
    const char *rule,
    uint32_t *begin_out,
    uint32_t *end_out);

bool pposlf_native_type_plan_v1_step_flow(
    const PPOSLFNativeTypePlanV1 *plan,
    uint32_t step_index,
    PPOSLFNativeStepFlowV1 *flow_out);

bool pposlf_native_type_plan_v1_head_group(
    const PPOSLFNativeTypePlanV1 *plan,
    PPOSLFNativeTermKindV1 kind,
    const char *text,
    int64_t integer,
    uint32_t *group_out);

bool pposlf_native_type_plan_v1_external_relation(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *relation,
    uint32_t arity,
    uint32_t *relation_out);

#endif
