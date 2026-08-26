#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_PROJECTION_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_PROJECTION_V1_H

#include "proof_gslt_article_v1.h"
#include "relational_state_program_v1.h"

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPProofGSLTRelationalProjectionValueV1;

typedef struct {
    PPProofGSLTNameV1 role;
    PPProofGSLTNameV1 table;
    uint32_t table_id;
    uint32_t arity;
    uint32_t key_arity;
} PPProofGSLTRelationalProjectionTableV1;

typedef struct {
    PPProofGSLTNameV1 role;
    PPProofGSLTRelationalProjectionValueV1 value;
} PPProofGSLTRelationalProjectionSelectorV1;

/*
 * A language-neutral, variable-length view of a generated relational
 * projection descriptor.  Table and selector roles remain uninterpreted
 * values.  Repeated selector roles are retained because a generated
 * projection relation may assign several admitted literals to one role.
 */
typedef struct {
    PPProofGSLTNameV1 owner;
    PPProofGSLTNameV1 base;
    PPProofGSLTRelationalProjectionTableV1 *tables;
    PPProofGSLTRelationalProjectionSelectorV1 *selectors;
    uint32_t table_len;
    uint32_t selector_len;
    char artifact_digest[65];
    char state_plan_digest[65];
    void *storage;
} PPProofGSLTRelationalProjectionV1;

void ppproof_gslt_relational_projection_v1_init(
    PPProofGSLTRelationalProjectionV1 *projection);

void ppproof_gslt_relational_projection_v1_free(
    PPProofGSLTRelationalProjectionV1 *projection);

/* Read and validate only the generated descriptor schema and owner identity. */
PPProofGSLTArticleV1Result ppproof_gslt_relational_projection_v1_read(
    PPProofGSLTRelationalProjectionV1 *projection,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size);

/*
 * Resolve every declared table against one generated state plan.  Binding is
 * transactional: a failed attempt does not alter an earlier valid binding.
 */
PPProofGSLTArticleV1Result ppproof_gslt_relational_projection_v1_bind_state(
    PPProofGSLTRelationalProjectionV1 *projection,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size);

#endif
