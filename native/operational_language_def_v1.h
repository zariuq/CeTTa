#ifndef CETTA_OPERATIONAL_LANGUAGE_DEF_V1_H
#define CETTA_OPERATIONAL_LANGUAGE_DEF_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Stage-zero source ingress for the five-field GSLT LanguageDef wire.
 *
 * This layer is deliberately operationally neutral.  It parses ordinary
 * MeTTa data, retains ordered source structure, and identifies the five core
 * fields.  The physical constructor vocabulary remains a bootstrap candidate
 * until its independent encoder/decoder laws are established.  This layer
 * does not interpret a rewrite system as Horn clauses and it does not decide
 * which native calculus or implementation may host the language.
 */

typedef enum {
    CETTA_OP_LANG_V1_SEXPR_SYMBOL = 0,
    CETTA_OP_LANG_V1_SEXPR_STRING = 1,
    CETTA_OP_LANG_V1_SEXPR_NATURAL = 2,
    CETTA_OP_LANG_V1_SEXPR_APPLICATION = 3
} CettaOpLangV1SExprKind;

typedef struct CettaOpLangV1SExpr CettaOpLangV1SExpr;

struct CettaOpLangV1SExpr {
    CettaOpLangV1SExprKind kind;
    uint32_t byte_left;
    uint32_t byte_right;
    union {
        char *symbol;
        struct {
            uint8_t *bytes;
            uint32_t len;
        } string;
        char *natural;
        struct {
            char *head;
            uint32_t head_byte_left;
            uint32_t head_byte_right;
            CettaOpLangV1SExpr **arguments;
            uint32_t argument_len;
        } application;
    } as;
};

typedef struct {
    uint32_t node_len;
    uint32_t choice_len;
    uint32_t work_item_len;
    uint32_t graph_node_len;
    uint32_t stack_node_len;
    /* The parser consumes a derived token lattice, not the source bytes. */
    uint32_t source_pass_count;
    uint32_t decoded_byte_len;
    /* Source decoding and lexical projection are accounted separately. */
    uint32_t source_decode_pass_count;
    uint32_t lexical_projection_pass_count;
    uint64_t derivation_fingerprint;
} CettaOpLangV1ParserReceipt;

/*
 * Parser-neutral physical document.  This is the common native ingress for
 * every typed operational layer that uses the canonical CettaTerm carrier;
 * it deliberately knows nothing about LanguageDef envelopes or denotation.
 */
typedef struct {
    CettaOpLangV1SExpr *root;
    /* Exact raw-source identity; semantic fingerprints remain parser-local. */
    char source_sha256[65];
    CettaOpLangV1ParserReceipt gll;
    CettaOpLangV1ParserReceipt glr;
} CettaOpLangV1Document;

typedef enum {
    CETTA_OP_LANG_V1_OK = 0,
    CETTA_OP_LANG_V1_BAD_ARGUMENT,
    CETTA_OP_LANG_V1_INVALID_UTF8,
    CETTA_OP_LANG_V1_GLL_RESOURCE_LIMIT,
    CETTA_OP_LANG_V1_GLR_RESOURCE_LIMIT,
    CETTA_OP_LANG_V1_SYNTAX_REJECTED,
    CETTA_OP_LANG_V1_AMBIGUOUS,
    CETTA_OP_LANG_V1_BACKEND_DISAGREEMENT,
    CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF,
    CETTA_OP_LANG_V1_IO_FAILURE,
    CETTA_OP_LANG_V1_ALLOCATION_FAILURE,
    CETTA_OP_LANG_V1_INTERNAL_FAILURE
} CettaOpLangV1Status;

typedef struct {
    CettaOpLangV1SExpr *root;
    /* Exact raw-source identity; semantic fingerprints remain parser-local. */
    char source_sha256[65];
    const uint8_t *name_bytes;
    uint32_t name_len;
    const CettaOpLangV1SExpr *types_field;
    const CettaOpLangV1SExpr *terms_field;
    const CettaOpLangV1SExpr *equations_field;
    const CettaOpLangV1SExpr *rewrites_field;
    CettaOpLangV1ParserReceipt gll;
    CettaOpLangV1ParserReceipt glr;
} CettaOperationalLanguageDefV1;

void cetta_op_lang_v1_document_init(CettaOpLangV1Document *document);
void cetta_op_lang_v1_document_free(CettaOpLangV1Document *document);

/*
 * Parse one canonical CettaTerm document independently with the complete-
 * forest native GLL and GLR engines.  Success requires a unique derivation in
 * each forest and exact equality of the projected physical trees.
 * Replacement is atomic: failure leaves an existing value in out unchanged.
 */
bool cetta_op_lang_v1_parse_document_bytes(
    CettaOpLangV1Document *out,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_op_lang_v1_parse_document_file(
    CettaOpLangV1Document *out,
    const char *path,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size);

void cetta_op_lang_v1_init(CettaOperationalLanguageDefV1 *language);
void cetta_op_lang_v1_free(CettaOperationalLanguageDefV1 *language);

/*
 * Parse the same bytes independently with the native complete-forest GLL and
 * GLR engines.  Success requires one derivation in each forest and exact
 * equality of the two projected source trees.  Replacement is atomic: a
 * failure leaves an existing value in out unchanged.
 */
bool cetta_op_lang_v1_parse_bytes(
    CettaOperationalLanguageDefV1 *out,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_op_lang_v1_parse_file(
    CettaOperationalLanguageDefV1 *out,
    const char *path,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_op_lang_v1_sexpr_equal(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right);

const CettaOpLangV1SExpr *cetta_op_lang_v1_field_entry(
    const CettaOpLangV1SExpr *field,
    uint32_t index);

uint32_t cetta_op_lang_v1_field_len(const CettaOpLangV1SExpr *field);

bool cetta_op_lang_v1_symbol_is(
    const CettaOpLangV1SExpr *expression,
    const char *text);

bool cetta_op_lang_v1_string_is(
    const CettaOpLangV1SExpr *expression,
    const uint8_t *bytes,
    size_t len);

bool cetta_op_lang_v1_application_is(
    const CettaOpLangV1SExpr *expression,
    const char *head,
    uint32_t argument_len);

const char *cetta_op_lang_v1_status_name(CettaOpLangV1Status status);

#endif /* CETTA_OPERATIONAL_LANGUAGE_DEF_V1_H */
