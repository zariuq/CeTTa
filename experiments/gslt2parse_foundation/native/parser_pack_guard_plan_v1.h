#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARD_PLAN_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARD_PLAN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_guard_relation_v1.h"
#include "parser_pack_lexical_v1.h"

typedef struct {
    Atom *answer;
    Atom *certificate;
} PPGuardPlanV1DerivationInput;

typedef struct {
    const char *source_digest;
    const char *pre_reflection_digest;
    const char *environment_digest;
    const char *answer_set_digest;
    const char *regular_compiler_digest;
    const char *guard_nfa_answer_digest;
    Atom *const *guard_nfa_tags;
    size_t guard_nfa_tag_len;
    const PPGuardPlanV1DerivationInput *derivations;
    size_t derivation_len;
} PPGuardPlanV1ProvenanceInput;

typedef struct {
    uint32_t state_id;
    uint32_t terminal_id;
    uint32_t production_id;
    bool state_is_extension;
    Atom *owner;
    Atom *state;
    Atom *tag;
    Atom *body;
    Atom *production;
    char *state_canonical;
    uint32_t evidence_begin;
    uint32_t evidence_len;
} PPGuardPlanV1Entry;

typedef struct {
    uint32_t entry_index;
    Atom *answer;
    Atom *certificate;
    char *canonical;
} PPGuardPlanV1Derivation;

typedef struct {
    Arena arena;
    PPGuardPlanV1Entry *entries;
    PPGuardPlanV1Derivation *derivations;
    PPNativeV1StateExtension *states;
    PPNativeV1TerminalExtension *terminals;
    PPNativeV1ProductionExtension *productions;
    PPABIV1Item *production_items;
    uint32_t *guard_terminal_ids;
    uint32_t entry_len;
    uint32_t derivation_len;
    uint32_t state_len;
    uint32_t terminal_len;
    uint32_t production_len;
    uint32_t lexical_terminal_len;
    bool has_lexical_plan;
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char source_digest[65];
    char pre_reflection_digest[65];
    char environment_digest[65];
    char answer_set_digest[65];
    char regular_compiler_digest[65];
    char guard_nfa_answer_digest[65];
    char plan_digest[65];
    char evidence_digest[65];
} PPGuardPlanV1;

typedef struct {
    char plan_digest[65];
    char evidence_digest[65];
    char relation_digest[65];
    char forest_digest[65];
    char execution_digest[65];
    uint32_t source_pass_count;
    uint32_t work_item_len;
    PPNativeV1Outcome outcome;
    bool accepted;
} PPGuardPlanV1Receipt;

void ppguard_plan_v1_init(PPGuardPlanV1 *plan);
void ppguard_plan_v1_free(PPGuardPlanV1 *plan);

bool ppguard_plan_v1_validate(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1ProvenanceInput *provenance,
    PPGuardPlanV1 *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_grammar_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_gll_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_glr_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_gll_parse_prepared(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    const CettaLpNativeGllPrepared *prepared,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_plan_v1_glr_parse_prepared(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    const CettaLpNativeGlrPrepared *prepared,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

#endif
