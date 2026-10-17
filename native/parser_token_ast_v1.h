#ifndef CETTA_PARSER_TOKEN_AST_V1_H
#define CETTA_PARSER_TOKEN_AST_V1_H

#include "parser_token_profile_v1.h"

typedef enum {
    CETTA_PARSER_TOKEN_AST_COMPLETED_V1 = 0,
    CETTA_PARSER_TOKEN_AST_INCOMPLETE_V1,
    CETTA_PARSER_TOKEN_AST_INVALID_V1
} CettaParserTokenAstOutcomeV1;
#include "lib_parse_native_grammar.h"

/* Generic, pure projection from the source-spanned CST and the admitted
 * constructor/list/coercion metadata. No constructor is evaluated. Token
 * lexemes and decoded payloads remain lossless scalar-text data. */
bool cetta_parser_token_ast_v1_project(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size);

/* The source was produced by the owned token scanner. Its input-domain and
 * offset checks are already established, not repeated per projected tree. */
bool cetta_parser_token_ast_v1_project_owned(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size);
bool cetta_parser_token_ast_v1_project_owned_outcome(
    Arena *, const CettaParserTokenProfileV1 *, const CettaLdTextV1 *,
    const CettaLpNativeUtf8ScalarView *, Atom *, uint32_t, uint32_t, Atom **,
    CettaParserTokenAstOutcomeV1 *, char *, size_t);

#endif
