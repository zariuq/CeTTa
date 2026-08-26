#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_PROJECTION_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_PROJECTION_V1_H

#include "certificate_gslt_article_v1.h"
#include "relational_state_program_v1.h"

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPCertificateGSLTRelationalProjectionValueV1;

typedef struct {
    PPCertificateGSLTNameV1 role;
    PPCertificateGSLTNameV1 table;
    uint32_t table_id;
    uint32_t arity;
    uint32_t key_arity;
} PPCertificateGSLTRelationalProjectionTableV1;

typedef struct {
    PPCertificateGSLTNameV1 role;
    PPCertificateGSLTRelationalProjectionValueV1 value;
} PPCertificateGSLTRelationalProjectionSelectorV1;

/*
 * A language-neutral, variable-length view of a generated relational
 * projection descriptor.  Table and selector roles remain uninterpreted
 * values.  Repeated selector roles are retained because a generated
 * projection relation may assign several admitted literals to one role.
 */
typedef struct {
    PPCertificateGSLTNameV1 owner;
    PPCertificateGSLTNameV1 base;
    PPCertificateGSLTRelationalProjectionTableV1 *tables;
    PPCertificateGSLTRelationalProjectionSelectorV1 *selectors;
    uint32_t table_len;
    uint32_t selector_len;
    char artifact_digest[65];
    char state_plan_digest[65];
    void *storage;
} PPCertificateGSLTRelationalProjectionV1;

void ppcertificate_gslt_relational_projection_v1_init(
    PPCertificateGSLTRelationalProjectionV1 *projection);

void ppcertificate_gslt_relational_projection_v1_free(
    PPCertificateGSLTRelationalProjectionV1 *projection);

/* Read and validate only the generated descriptor schema and owner identity. */
PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_projection_v1_read(
    PPCertificateGSLTRelationalProjectionV1 *projection,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size);

/*
 * Resolve every declared table against one generated state plan.  Binding is
 * transactional: a failed attempt does not alter an earlier valid binding.
 */
PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_projection_v1_bind_state(
    PPCertificateGSLTRelationalProjectionV1 *projection,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size);

#endif
