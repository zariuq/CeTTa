#ifndef CETTA_PARSER_TOKEN_AST_V1_H
#define CETTA_PARSER_TOKEN_AST_V1_H

#include "parser_token_profile_v1.h"
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

#endif
