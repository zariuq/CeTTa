#ifndef CETTA_LANGUAGE_DEF_PARSER_PACK_V1_H
#define CETTA_LANGUAGE_DEF_PARSER_PACK_V1_H

#include "language_def_core_v1.h"
#include "parser_pack_abi_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Parser-oriented extension of the five-field LanguageDef.
 *
 * The LanguageDef core owns constructor labels, result sorts, parameters,
 * and concrete syntax rows.  This separate layer supplies only the scalar
 * classes that cannot be represented by finite literal terminals.  It does
 * not define a second grammar and it does not assign Horn meaning to the
 * LanguageDef relations.
 */

typedef enum {
    CETTA_LD_LEXICAL_POINTS_V1 = 0,
    CETTA_LD_LEXICAL_EXCEPT_V1
} CettaLdLexicalClassKindV1;

typedef struct {
    CettaLdTextV1 name;
    CettaLdLexicalClassKindV1 kind;
    uint32_t *points;
    uint32_t point_len;
} CettaLdLexicalClassV1;

typedef struct {
    CettaLdTextV1 sort;
    CettaLdTextV1 class_name;
    CettaLdTextV1 label;
} CettaLdLexicalStateV1;

typedef struct {
    CettaLdTextV1 name;
    CettaLdTextV1 start_sort;
    CettaLdLexicalClassV1 *classes;
    uint32_t class_len;
    CettaLdLexicalStateV1 *states;
    uint32_t state_len;
    char authority_sha256[65];
} CettaLdParserProfileV1;

typedef enum {
    CETTA_LD_PARSER_PACK_V1_OK = 0,
    CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT,
    CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE,
    CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT,
    CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT,
    CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE,
    CETTA_LD_PARSER_PACK_V1_INVALID_UTF8,
    CETTA_LD_PARSER_PACK_V1_OPEN_GRAMMAR,
    CETTA_LD_PARSER_PACK_V1_ABI_REJECTED,
    CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED
} CettaLdParserPackV1Status;

typedef struct {
    PPABIV1Pack pack;
    Atom *start_state;
    char language_authority_sha256[65];
    char profile_authority_sha256[65];
    char binding_sha256[65];
    char compiler_sha256[65];
    uint32_t authored_rule_len;
    uint32_t lexical_rule_len;
    uint32_t entry_rule_len;
} CettaLdParserPackV1;

void cetta_ld_parser_profile_v1_init(CettaLdParserProfileV1 *profile);
void cetta_ld_parser_profile_v1_free(CettaLdParserProfileV1 *profile);

/*
 * Decode one canonical structured parser-profile document.
 *
 * Byte-sourced callers qualify the document through the paired GLL/GLR
 * readers before decoding it.  Direct structured-value callers instead
 * carry a digest of the structure they supplied as the authority identity.
 */
bool cetta_ld_parser_profile_v1_decode(
    CettaLdParserProfileV1 *out,
    const CettaOpLangV1Document *document,
    uint32_t work_limit,
    CettaLdParserPackV1Status *status,
    char *error_buf,
    size_t error_buf_size);

void cetta_ld_parser_pack_v1_init(CettaLdParserPackV1 *compiled);
void cetta_ld_parser_pack_v1_free(CettaLdParserPackV1 *compiled);

/*
 * Compile the exact first-order terminal/nonterminal syntax fragment.
 *
 * Supported rows follow the same discipline as the Lean grammar extraction:
 * every nonterminal names one simple base-typed parameter, parameters occur
 * once and in order, and the result sort is the ParserPack left-hand state.
 * A separate compiler-derived entry production applies the whole-source EOF
 * boundary exactly once and projects the authored start value unchanged;
 * recursive occurrences of the authored start sort remain ordinary grammar
 * occurrences.
 * Unsupported syntax operators, binders, relations, or evaluation policies
 * return OUTSIDE_FRAGMENT rather than falling back to Horn execution.
 * The authored equations are executed at grammar-load time. Parser execution
 * uses the existing native GLL/GLR engines. This is source-driven compilation,
 * not specialized generated compiler machine code or a replay certificate.
 * Replacement is atomic.
 */
bool cetta_language_def_parser_pack_v1_compile(
    CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language,
    const char language_authority_sha256[65],
    const CettaLdParserProfileV1 *profile,
    uint32_t work_limit,
    CettaLdParserPackV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Same compiler boundary with explicit immutable authored source. This is
 * useful for alternative compiler families and source-mutation checks; it
 * never falls back to a handwritten lowering. Compiler identity is the hash
 * of these exact bytes. Scalar-text projection preserves literal U+0000. */
bool cetta_language_def_parser_pack_v1_compile_source(
    CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language,
    const char language_authority_sha256[65],
    const CettaLdParserProfileV1 *profile,
    const uint8_t *compiler_source, size_t compiler_source_len,
    uint32_t work_limit,
    CettaLdParserPackV1Status *status,
    char *error_buf, size_t error_buf_size);

const char *cetta_ld_parser_pack_v1_status_name(
    CettaLdParserPackV1Status status);

#endif /* CETTA_LANGUAGE_DEF_PARSER_PACK_V1_H */
