#ifndef CETTA_STRUCTURED_C_PROFILE_V1_H
#define CETTA_STRUCTURED_C_PROFILE_V1_H

#include "language_def_core_v1.h"

#include <stdbool.h>

/*
 * Non-owning constructor view of the exact StructuredC v1 presentation.
 * Admission checks every carrier, constructor row, and operational rule.
 * Rule display names are deliberately irrelevant; rule structure is not.
 */
typedef struct {
    const CettaLdTextV1 *identifier;
    const CettaLdTextV1 *function_name;
    const CettaLdTextV1 *external_name;
    const CettaLdTextV1 *type_named;
    const CettaLdTextV1 *type_pointer;
    const CettaLdTextV1 *type_const;
    const CettaLdTextV1 *value_integer;
    const CettaLdTextV1 *value_symbol;
    const CettaLdTextV1 *expression_variable;
    const CettaLdTextV1 *expression_constant;
    const CettaLdTextV1 *expression_call;
    const CettaLdTextV1 *expressions_nil;
    const CettaLdTextV1 *expressions_cons;
    const CettaLdTextV1 *declare;
    const CettaLdTextV1 *effect;
    const CettaLdTextV1 *if_statement;
    const CettaLdTextV1 *switch_statement;
    const CettaLdTextV1 *return_statement;
    const CettaLdTextV1 *statements_nil;
    const CettaLdTextV1 *statements_cons;
    const CettaLdTextV1 *case_statement;
    const CettaLdTextV1 *cases_nil;
    const CettaLdTextV1 *cases_cons;
    const CettaLdTextV1 *parameter;
    const CettaLdTextV1 *parameters_nil;
    const CettaLdTextV1 *parameters_cons;
    const CettaLdTextV1 *function;
    const CettaLdTextV1 *external_function;
    const CettaLdTextV1 *external_functions_nil;
    const CettaLdTextV1 *external_functions_cons;
    const CettaLdTextV1 *functions_nil;
    const CettaLdTextV1 *functions_cons;
    const CettaLdTextV1 *program;
} CettaStructuredCProfileV1;

bool cetta_structured_c_profile_v1_admit(
    const CettaLanguageDefCoreV1 *language,
    CettaStructuredCProfileV1 *profile);

#endif /* CETTA_STRUCTURED_C_PROFILE_V1_H */
