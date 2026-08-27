#ifndef CETTA_LANGUAGE_DEF_CORE_V1_H
#define CETTA_LANGUAGE_DEF_CORE_V1_H

#include "operational_language_def_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Owned C representation of the five-field GSLT LanguageDef core.
 *
 * This is a decoding target, not an execution engine.  It retains authored
 * order, multiplicity, binder metadata, and arbitrary Unicode strings.  It
 * neither gives the core a Horn meaning nor admits any implementation as a
 * native calculus.
 */

typedef struct {
    uint8_t *bytes;
    uint32_t len;
} CettaLdTextV1;

typedef struct {
    bool present;
    CettaLdTextV1 value;
} CettaLdOptionalTextV1;

typedef struct {
    CettaLdTextV1 *items;
    uint32_t len;
} CettaLdTextListV1;

typedef enum {
    CETTA_LD_CARRIER_AST_V1 = 0,
    CETTA_LD_CARRIER_TOKEN_LABEL_V1,
    CETTA_LD_CARRIER_TOKEN_RAW_V1,
    CETTA_LD_CARRIER_TOKEN_PROOF_V1,
    CETTA_LD_CARRIER_TOKEN_PATH_V1,
    CETTA_LD_CARRIER_BUILTIN_INT_V1,
    CETTA_LD_CARRIER_BUILTIN_STRING_V1,
    CETTA_LD_CARRIER_BUILTIN_BOOL_V1
} CettaLdCarrierKindV1;

typedef enum {
    CETTA_LD_COLLECTION_VEC_V1 = 0,
    CETTA_LD_COLLECTION_HASH_BAG_V1,
    CETTA_LD_COLLECTION_HASH_SET_V1
} CettaLdCollectionTypeV1;

typedef struct {
    CettaLdTextV1 name;
    CettaLdCarrierKindV1 carrier;
} CettaLdTypeDeclV1;

typedef enum {
    CETTA_LD_TYPE_BASE_V1 = 0,
    CETTA_LD_TYPE_ARROW_V1,
    CETTA_LD_TYPE_MULTI_BINDER_V1,
    CETTA_LD_TYPE_COLLECTION_V1
} CettaLdTypeExprKindV1;

typedef struct CettaLdTypeExprV1 CettaLdTypeExprV1;

struct CettaLdTypeExprV1 {
    CettaLdTypeExprKindV1 kind;
    union {
        CettaLdTextV1 base;
        struct {
            CettaLdTypeExprV1 *domain;
            CettaLdTypeExprV1 *codomain;
        } arrow;
        CettaLdTypeExprV1 *multi_binder_body;
        struct {
            CettaLdCollectionTypeV1 collection_type;
            CettaLdTypeExprV1 *element_type;
        } collection;
    } as;
};

typedef enum {
    CETTA_LD_PARAM_SIMPLE_V1 = 0,
    CETTA_LD_PARAM_ABSTRACTION_NAMED_V1,
    CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1
} CettaLdTermParamKindV1;

typedef struct {
    CettaLdTermParamKindV1 kind;
    CettaLdTextV1 body_name;
    CettaLdTypeExprV1 type;
    union {
        CettaLdOptionalTextV1 binder;
        CettaLdTextListV1 binders;
    } names;
} CettaLdTermParamV1;

typedef struct CettaLdSyntaxItemV1 CettaLdSyntaxItemV1;
typedef struct CettaLdSyntaxPatternOpV1 CettaLdSyntaxPatternOpV1;

typedef struct {
    CettaLdSyntaxItemV1 *items;
    uint32_t len;
} CettaLdSyntaxItemListV1;

typedef enum {
    CETTA_LD_SYNTAX_TERMINAL_V1 = 0,
    CETTA_LD_SYNTAX_NONTERMINAL_V1,
    CETTA_LD_SYNTAX_SEPARATOR_V1,
    CETTA_LD_SYNTAX_DELIMITER_V1,
    CETTA_LD_SYNTAX_OP_V1
} CettaLdSyntaxItemKindV1;

struct CettaLdSyntaxItemV1 {
    CettaLdSyntaxItemKindV1 kind;
    union {
        CettaLdTextV1 text;
        struct {
            CettaLdTextV1 left;
            CettaLdTextV1 right;
        } delimiter;
        CettaLdSyntaxPatternOpV1 *op;
    } as;
};

typedef enum {
    CETTA_LD_SYNTAX_OP_VAR_V1 = 0,
    CETTA_LD_SYNTAX_OP_SEP_V1,
    CETTA_LD_SYNTAX_OP_ZIP_V1,
    CETTA_LD_SYNTAX_OP_MAP_V1,
    CETTA_LD_SYNTAX_OP_OPT_V1
} CettaLdSyntaxPatternOpKindV1;

struct CettaLdSyntaxPatternOpV1 {
    CettaLdSyntaxPatternOpKindV1 kind;
    union {
        CettaLdTextV1 variable;
        struct {
            CettaLdTextV1 collection;
            CettaLdTextV1 separator;
            CettaLdSyntaxPatternOpV1 *source;
        } sep;
        struct {
            CettaLdTextV1 left;
            CettaLdTextV1 right;
        } zip;
        struct {
            CettaLdSyntaxPatternOpV1 *source;
            CettaLdTextListV1 binders;
            CettaLdSyntaxItemListV1 body;
        } map;
        CettaLdSyntaxItemListV1 opt;
    } as;
};

typedef enum {
    CETTA_LD_EVAL_REWRITE_V1 = 0,
    CETTA_LD_EVAL_FOLD_V1,
    CETTA_LD_EVAL_ORACLE_V1
} CettaLdTermEvalPolicyV1;

typedef struct {
    bool present;
    CettaLdTermEvalPolicyV1 value;
} CettaLdOptionalTermEvalPolicyV1;

typedef struct {
    CettaLdTextV1 label;
    CettaLdTextV1 category;
    CettaLdTermParamV1 *params;
    uint32_t param_len;
    CettaLdSyntaxItemListV1 syntax_pattern;
    CettaLdOptionalTermEvalPolicyV1 eval_policy;
} CettaLdGrammarRuleV1;

typedef struct CettaLdPatternV1 CettaLdPatternV1;

typedef struct {
    CettaLdPatternV1 *items;
    uint32_t len;
} CettaLdPatternListV1;

typedef enum {
    CETTA_LD_PATTERN_BVAR_V1 = 0,
    CETTA_LD_PATTERN_FVAR_V1,
    CETTA_LD_PATTERN_APPLY_V1,
    CETTA_LD_PATTERN_LAMBDA_V1,
    CETTA_LD_PATTERN_MULTI_LAMBDA_V1,
    CETTA_LD_PATTERN_SUBST_V1,
    CETTA_LD_PATTERN_COLLECTION_V1
} CettaLdPatternKindV1;

struct CettaLdPatternV1 {
    CettaLdPatternKindV1 kind;
    union {
        char *bvar_decimal;
        CettaLdTextV1 fvar;
        struct {
            CettaLdTextV1 head;
            CettaLdPatternListV1 arguments;
        } apply;
        struct {
            CettaLdOptionalTextV1 binder;
            CettaLdPatternV1 *body;
        } lambda;
        struct {
            char *arity_decimal;
            CettaLdTextListV1 binders;
            CettaLdPatternV1 *body;
        } multi_lambda;
        struct {
            CettaLdPatternV1 *body;
            CettaLdPatternV1 *replacement;
        } subst;
        struct {
            CettaLdCollectionTypeV1 collection_type;
            CettaLdPatternListV1 elements;
            CettaLdOptionalTextV1 rest;
        } collection;
    } as;
};

/*
 * Public lifecycle for standalone canonical Pattern values.
 *
 * LanguageDef decoding owns patterns nested inside relation rules.  Compiler
 * stages may also construct an ordinary target-language Pattern directly;
 * these functions give that value the same ownership discipline without
 * introducing a second target IR.
 */
void cetta_ld_pattern_v1_init(CettaLdPatternV1 *pattern);
void cetta_ld_pattern_v1_free(CettaLdPatternV1 *pattern);

typedef struct CettaLdPremiseV1 CettaLdPremiseV1;

typedef struct {
    CettaLdPremiseV1 *items;
    uint32_t len;
} CettaLdPremiseListV1;

typedef enum {
    CETTA_LD_PREMISE_FRESHNESS_V1 = 0,
    CETTA_LD_PREMISE_CONGRUENCE_V1,
    CETTA_LD_PREMISE_RELATION_QUERY_V1,
    CETTA_LD_PREMISE_FOR_ALL_V1
} CettaLdPremiseKindV1;

struct CettaLdPremiseV1 {
    CettaLdPremiseKindV1 kind;
    union {
        struct {
            CettaLdTextV1 variable;
            CettaLdPatternV1 term;
        } freshness;
        struct {
            CettaLdPatternV1 left;
            CettaLdPatternV1 right;
        } congruence;
        struct {
            CettaLdTextV1 relation;
            CettaLdPatternListV1 arguments;
        } relation_query;
        struct {
            CettaLdTextV1 collection;
            CettaLdTextV1 parameter;
            CettaLdPremiseV1 *body;
        } for_all;
    } as;
};

typedef struct {
    CettaLdTextV1 name;
    CettaLdTypeExprV1 type;
} CettaLdTypeBindingV1;

typedef struct {
    CettaLdTextV1 name;
    CettaLdTypeBindingV1 *type_context;
    uint32_t type_context_len;
    CettaLdPremiseListV1 premises;
    CettaLdPatternV1 left;
    CettaLdPatternV1 right;
} CettaLdRelationRuleV1;

typedef struct {
    CettaLdTextV1 name;
    CettaLdTypeDeclV1 *types;
    uint32_t type_len;
    CettaLdGrammarRuleV1 *terms;
    uint32_t term_len;
    CettaLdRelationRuleV1 *equations;
    uint32_t equation_len;
    CettaLdRelationRuleV1 *rewrites;
    uint32_t rewrite_len;
} CettaLanguageDefCoreV1;

typedef enum {
    CETTA_LD_CORE_V1_OK = 0,
    CETTA_LD_CORE_V1_BAD_ARGUMENT,
    CETTA_LD_CORE_V1_MALFORMED_WIRE,
    CETTA_LD_CORE_V1_RESOURCE_LIMIT,
    CETTA_LD_CORE_V1_ALLOCATION_FAILURE
} CettaLdCoreV1Status;

void cetta_language_def_core_v1_init(CettaLanguageDefCoreV1 *language);
void cetta_language_def_core_v1_free(CettaLanguageDefCoreV1 *language);

/* Atomic replacement: failure leaves an existing output value unchanged. */
bool cetta_language_def_core_v1_decode(
    CettaLanguageDefCoreV1 *out,
    const CettaOperationalLanguageDefV1 *wire,
    uint32_t work_limit,
    CettaLdCoreV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_ld_core_v1_status_name(CettaLdCoreV1Status status);

#endif /* CETTA_LANGUAGE_DEF_CORE_V1_H */
