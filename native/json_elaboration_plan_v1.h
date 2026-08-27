#ifndef CETTA_JSON_ELABORATION_PLAN_V1_H
#define CETTA_JSON_ELABORATION_PLAN_V1_H

#include "language_def_core_v1.h"
#include "language_def_parser_pack_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_JSON_ELAB_TEXT_V1 = 0,
    CETTA_JSON_ELAB_WS_EMPTY_V1,
    CETTA_JSON_ELAB_WS_CONS_V1,
    CETTA_JSON_ELAB_VALUE_FALSE_V1,
    CETTA_JSON_ELAB_VALUE_NULL_V1,
    CETTA_JSON_ELAB_VALUE_TRUE_V1,
    CETTA_JSON_ELAB_VALUE_OBJECT_V1,
    CETTA_JSON_ELAB_VALUE_ARRAY_V1,
    CETTA_JSON_ELAB_VALUE_NUMBER_V1,
    CETTA_JSON_ELAB_VALUE_STRING_V1,
    CETTA_JSON_ELAB_OBJECT_V1,
    CETTA_JSON_ELAB_MEMBERS_NONE_V1,
    CETTA_JSON_ELAB_MEMBERS_SOME_V1,
    CETTA_JSON_ELAB_MEMBERS_V1,
    CETTA_JSON_ELAB_MEMBER_TAIL_EMPTY_V1,
    CETTA_JSON_ELAB_MEMBER_TAIL_CONS_V1,
    CETTA_JSON_ELAB_MEMBER_V1,
    CETTA_JSON_ELAB_ARRAY_V1,
    CETTA_JSON_ELAB_ELEMENTS_NONE_V1,
    CETTA_JSON_ELAB_ELEMENTS_SOME_V1,
    CETTA_JSON_ELAB_ELEMENTS_V1,
    CETTA_JSON_ELAB_ELEMENT_TAIL_EMPTY_V1,
    CETTA_JSON_ELAB_ELEMENT_TAIL_CONS_V1,
    CETTA_JSON_ELAB_STRING_V1,
    CETTA_JSON_ELAB_STRING_CHARS_EMPTY_V1,
    CETTA_JSON_ELAB_STRING_CHARS_CONS_V1,
    CETTA_JSON_ELAB_STRING_CHAR_PLAIN_V1,
    CETTA_JSON_ELAB_STRING_CHAR_ESCAPE_V1,
    CETTA_JSON_ELAB_ESCAPE_SIMPLE_V1,
    CETTA_JSON_ELAB_ESCAPE_UNICODE_V1,
    CETTA_JSON_ELAB_NUMBER_V1,
    CETTA_JSON_ELAB_MINUS_NONE_V1,
    CETTA_JSON_ELAB_MINUS_SOME_V1,
    CETTA_JSON_ELAB_INT_ZERO_V1,
    CETTA_JSON_ELAB_INT_NONZERO_V1,
    CETTA_JSON_ELAB_DIGITS_EMPTY_V1,
    CETTA_JSON_ELAB_DIGITS_CONS_V1,
    CETTA_JSON_ELAB_FRAC_NONE_V1,
    CETTA_JSON_ELAB_FRAC_SOME_V1,
    CETTA_JSON_ELAB_FRAC_V1,
    CETTA_JSON_ELAB_EXP_NONE_V1,
    CETTA_JSON_ELAB_EXP_SOME_V1,
    CETTA_JSON_ELAB_EXP_V1,
    CETTA_JSON_ELAB_SIGN_NONE_V1,
    CETTA_JSON_ELAB_SIGN_SOME_V1,
    CETTA_JSON_ELAB_LEXICAL_SCALAR_V1,
    CETTA_JSON_ELAB_OP_COUNT_V1
} CettaJsonElaborationOpV1;

typedef enum {
    CETTA_JSON_TARGET_NULL_V1 = 0,
    CETTA_JSON_TARGET_BOOL_V1,
    CETTA_JSON_TARGET_STRING_V1,
    CETTA_JSON_TARGET_NUMBER_V1,
    CETTA_JSON_TARGET_ARRAY_V1,
    CETTA_JSON_TARGET_OBJECT_V1,
    CETTA_JSON_TARGET_MEMBER_V1,
    CETTA_JSON_TARGET_SOURCE_SPAN_V1,
    CETTA_JSON_TARGET_NO_SOURCE_SPAN_V1,
    CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1
} CettaJsonTargetConstructorV1;

typedef struct {
    char *label;
    uint32_t source_term_index;
    uint32_t child_len;
    CettaJsonElaborationOpV1 op;
} CettaJsonElaborationPlanEntryV1;

typedef struct {
    CettaJsonElaborationPlanEntryV1 *entries;
    uint32_t entry_len;
    char *target_names[CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1];
    uint32_t target_term_indices[CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1];
    char source_sha256[65];
    char profile_sha256[65];
    char target_sha256[65];
} CettaJsonElaborationPlanV1;

typedef enum {
    CETTA_JSON_ELAB_PLAN_V1_OK = 0,
    CETTA_JSON_ELAB_PLAN_V1_BAD_ARGUMENT,
    CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_SOURCE,
    CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_PROFILE,
    CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET,
    CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE
} CettaJsonElaborationPlanV1Status;

void cetta_json_elaboration_plan_v1_init(
    CettaJsonElaborationPlanV1 *plan);
void cetta_json_elaboration_plan_v1_free(
    CettaJsonElaborationPlanV1 *plan);

bool cetta_json_elaboration_plan_v1_compile(
    CettaJsonElaborationPlanV1 *out,
    const CettaLanguageDefCoreV1 *source,
    const char source_sha256[65],
    const CettaLdParserProfileV1 *profile,
    const CettaLanguageDefCoreV1 *target,
    const char target_sha256[65],
    CettaJsonElaborationPlanV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const CettaJsonElaborationPlanEntryV1 *
cetta_json_elaboration_plan_v1_find(
    const CettaJsonElaborationPlanV1 *plan,
    const char *label);

const char *cetta_json_elaboration_plan_v1_target_name(
    const CettaJsonElaborationPlanV1 *plan,
    CettaJsonTargetConstructorV1 constructor);

const char *cetta_json_elaboration_plan_v1_status_name(
    CettaJsonElaborationPlanV1Status status);

#endif /* CETTA_JSON_ELABORATION_PLAN_V1_H */
