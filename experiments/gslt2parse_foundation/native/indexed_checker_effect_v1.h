#ifndef CETTA_GSLT2PARSE_INDEXED_CHECKER_EFFECT_V1_H
#define CETTA_GSLT2PARSE_INDEXED_CHECKER_EFFECT_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "parser_occurrence_fold_v1.h"

/*
 * Object-logic-neutral effects understood by an indexed inference checker.
 * A source language selects these effects through a compiled presentation;
 * parser occurrence identities never select native behavior by convention.
 */
typedef enum {
    PPINDEXED_CHECKER_EFFECT_V1_OPEN_SCOPE = 0,
    PPINDEXED_CHECKER_EFFECT_V1_CLOSE_SCOPE = 1,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_CONSTANTS = 2,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_VARIABLES = 3,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_DISTINCT = 4,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_FLOATING = 5,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_ESSENTIAL = 6,
    PPINDEXED_CHECKER_EFFECT_V1_DECLARE_RULE = 7,
    PPINDEXED_CHECKER_EFFECT_V1_CHECK_NORMAL = 8,
    PPINDEXED_CHECKER_EFFECT_V1_CHECK_COMPRESSED = 9,
    PPINDEXED_CHECKER_EFFECT_V1_RESOLVE_SOURCE = 10,
    PPINDEXED_CHECKER_EFFECT_V1_COMPLETE_SCOPE = 11,
    PPINDEXED_CHECKER_EFFECT_V1_COUNT = 12
} PPIndexedCheckerEffectV1Kind;

typedef struct {
    bool allow_inner_constants;
    bool allow_duplicate_floating;
    bool allow_toplevel_essential;
    bool reject_unknown_steps;
    bool skip_completed_sources;
    bool reject_active_source_cycles;
} PPIndexedCheckerPolicyV1;

typedef struct {
    PPIndexedCheckerEffectV1Kind *effect_by_operation;
    PPIndexedCheckerPolicyV1 policy;
    uint32_t operation_len;
    char occurrence_fold_plan_digest[65];
    char compiler_plan_digest[65];
    char plan_digest[65];
} PPIndexedCheckerEffectV1Plan;

void ppindexed_checker_effect_v1_plan_init(
    PPIndexedCheckerEffectV1Plan *plan);

void ppindexed_checker_effect_v1_plan_free(
    PPIndexedCheckerEffectV1Plan *plan);

const char *ppindexed_checker_effect_v1_kind_name(
    PPIndexedCheckerEffectV1Kind kind);

/*
 * Records have the compiler normal forms
 *
 *   (indexed-checker-effect-v1 OPERATION EFFECT)
 *   (indexed-checker-policy-v1 POLICY-KEY POLICY-VALUE)
 *
 * They must cover every operation in the occurrence plan and every generic
 * policy key exactly once.  Both operation behavior and checker policy are
 * therefore selected by the authored presentation rather than by a native
 * driver.
 * compiler_plan_digest is SHA-256 over the strictly ordered canonical
 * records, each followed by LF.  `out` must have been initialized.
 */
bool ppindexed_checker_effect_v1_plan_build(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    Atom *const *records,
    size_t record_len,
    const char *compiler_plan_digest,
    PPIndexedCheckerEffectV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool ppindexed_checker_effect_v1_plan_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

bool ppindexed_checker_effect_v1_lookup(
    const PPIndexedCheckerEffectV1Plan *plan,
    uint32_t operation_id,
    PPIndexedCheckerEffectV1Kind *out);

/*
 * Append an owning C initializer.  The generated initializer validates the
 * dense effect table against the generated occurrence plan before publishing
 * it.
 */
bool ppindexed_checker_effect_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

#endif
