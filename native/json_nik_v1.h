#ifndef CETTA_JSON_NIK_V1_H
#define CETTA_JSON_NIK_V1_H

#include "json_runtime_v1.h"
#include "nik_hosted_calculus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_JSON_NIK_V1_CAPABILITY_EXACT_FOREST UINT64_C(100)
#define CETTA_JSON_NIK_V1_CAPABILITY_EXACT_NUMBER_LEXEME UINT64_C(200)
#define CETTA_JSON_NIK_V1_CAPABILITY_UNICODE_SCALARS UINT64_C(300)
#define CETTA_JSON_NIK_V1_CAPABILITY_OCCURRENCE_IDENTITY UINT64_C(400)
#define CETTA_JSON_NIK_V1_CAPABILITY_SOURCE_SPANS UINT64_C(500)

#define CETTA_JSON_NIK_V1_OPERATION_PARSE UINT64_C(1000)

typedef struct CettaJsonNikV1 CettaJsonNikV1;

typedef struct {
    Arena *arena;
    const uint8_t *json_bytes;
    size_t json_byte_len;
    const CettaJsonRuntimeV1Limits *limits;
} CettaJsonNikV1Request;

typedef struct {
    CettaJsonRuntimeV1Status status;
    CettaJsonKernelV1 kernel;
    Atom *value;
} CettaJsonNikV1LanguageReceipt;

void cetta_json_nik_v1_language_receipt_init(
    CettaJsonNikV1LanguageReceipt *receipt);

typedef struct {
    CettaNikHostAdmissionKindV1 kind;
    CettaJsonNikV1 *host;
} CettaJsonNikV1Admission;

/* The three authored sources are borrowed currentness scopes and must remain
 * alive and immutable while the host is used. Admission compiles and prepares
 * the actual supplied JSON syntax, lexical profile, and value target. */
CettaJsonNikV1Admission cetta_json_nik_v1_admit(
    const uint8_t *language_source,
    size_t language_source_len,
    const uint8_t *profile_source,
    size_t profile_source_len,
    const uint8_t *target_source,
    size_t target_source_len,
    char *error_buf,
    size_t error_buf_size);

void cetta_json_nik_v1_destroy(CettaJsonNikV1 *host);

bool cetta_json_nik_v1_is_current(const CettaJsonNikV1 *host);

/*
 * Hot-path entry for an already admitted realization.  Admission compiles the
 * supplied presentations, prepares the selected GLL/GLR kernel, and binds its
 * revision.  This function executes that prepared kernel directly: it does
 * not rehash the sources or construct a generic NIK receipt per document.
 *
 * The caller must keep the admitted source scope immutable.  Mutable scopes
 * must be checked with cetta_json_nik_v1_is_current when they are activated or
 * revised; the audited run entry below remains available when a per-call
 * hosted receipt is explicitly required.
 *
 * The admitted host and its selected runtime are immutable after admission.
 * Concurrent prepared parses are safe with distinct Arenas and output
 * storage.  Destruction must not race with parsing or borrowed runtime access.
 */
bool cetta_json_nik_v1_parse_prepared(
    const CettaJsonNikV1 *host,
    Arena *arena,
    const uint8_t *json_bytes,
    size_t json_byte_len,
    const CettaJsonRuntimeV1Limits *limits,
    Atom **out,
    CettaJsonRuntimeV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Borrow the selected prepared runtime for non-calculus observations such as
 * capability metadata and JSON value round-trip validation.  Ownership stays
 * with the admitted host. */
const CettaJsonRuntimeV1 *cetta_json_nik_v1_borrow_selected_runtime(
    const CettaJsonNikV1 *host);

CettaJsonKernelV1 cetta_json_nik_v1_production_kernel(
    const CettaJsonNikV1 *host);

CettaNikHostedNativeCallKindV1 cetta_json_nik_v1_run(
    CettaJsonNikV1 *host,
    CettaNikNativeOperationIdV1 operation_identity,
    const CettaJsonNikV1Request *request,
    CettaJsonNikV1LanguageReceipt *language_receipt,
    CettaNikHostedNativeReceiptV1 *hosted_receipt);

const CettaNikNativeCalculusV1 *cetta_json_nik_v1_calculus(void);

#endif /* CETTA_JSON_NIK_V1_H */
