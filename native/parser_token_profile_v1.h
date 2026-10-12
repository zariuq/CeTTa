#ifndef CETTA_PARSER_TOKEN_PROFILE_V1_H
#define CETTA_PARSER_TOKEN_PROFILE_V1_H

#include "operational_language_def_v1.h"
#include "language_def_core_v1.h"
#include "src/atom.h"

typedef enum {
    CETTA_BNFC_DOCUMENTED_V1 = 0,
    CETTA_BNFC_C_FLEX_2_9_6_3_V1
} CettaBnfcLexicalPolicyV1;

/* The regular-token extension of a LanguageDef parser profile. All fields
 * are owned structural data, never evaluated terms or borrowed documents.
 * The five lists retain authored order and production occurrences. */
typedef struct CettaParserTokenProfileV1 {
    Arena arena;
    Atom *document;
    Atom *policy;
    Atom *declarations;
    Atom *comments;
    Atom *productions;
    Atom *entrypoints;
    CettaBnfcLexicalPolicyV1 policy_kind;
    uint32_t declaration_len;
    uint32_t comment_len;
    uint32_t production_len;
    uint32_t entrypoint_len;
} CettaParserTokenProfileV1;

void cetta_parser_token_profile_v1_init(CettaParserTokenProfileV1 *profile);
void cetta_parser_token_profile_v1_free(CettaParserTokenProfileV1 *profile);

/* Decode ParserTokenLayerV1(policy, declarations, comments, production
 * metadata, entrypoints) from the existing structural ingress. Failure is
 * atomic, including a refused policy or malformed declaration. */
bool cetta_parser_token_profile_v1_decode(
    CettaParserTokenProfileV1 *out, const CettaOpLangV1SExpr *root,
    uint32_t work_limit, char *error, size_t error_size);

/* Constructor skeletons use the declared BNFC category normalization;
 * production identities are checked independently of those skeletons. */
bool cetta_parser_token_profile_v1_validate_language(
    const CettaParserTokenProfileV1 *profile,
    const CettaLanguageDefCoreV1 *language, char *error, size_t error_size);

Atom *cetta_parser_token_category_v1_normalize(Arena *arena, const CettaLdTextV1 *text);

#endif
