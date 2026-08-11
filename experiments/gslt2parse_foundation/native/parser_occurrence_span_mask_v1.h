#ifndef CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SPAN_MASK_V1_H
#define CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SPAN_MASK_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "parser_occurrence_fold_v1.h"
#include "semantic_mask_nfa_v1.h"

enum {
    PPOCCURRENCE_SPAN_MASK_V1_DROP = 0,
    PPOCCURRENCE_SPAN_MASK_V1_RETAIN = 1,
    PPOCCURRENCE_SPAN_MASK_V1_ACTION_LEN = 2
};

/*
 * A compiled quotient of semantic-mask actions for occurrence folds.  The
 * complete semantic transducer distinguishes wrapper identities; this plan
 * observes only retained versus discarded source scalars.  The exact target
 * production remains independently bound by the occurrence-fold plan.
 */
typedef struct PPOccurrenceSpanMaskV1Plan {
    PPSemanticMaskDfaV1Program retention;
    uint32_t *terminal_tags;
    uint32_t *terminal_value_production_labels;
    uint32_t terminal_len;
    uint32_t bound_terminal_len;
    char base_pack_digest[65];
    char cursor_program_digest[65];
    char occurrence_fold_plan_digest[65];
    char compiler_digest[65];
    char answer_set_digest[65];
    char plan_digest[65];
} PPOccurrenceSpanMaskV1Plan;

typedef enum {
    PPOCCURRENCE_SPAN_MASK_V1_UNHANDLED = 0,
    PPOCCURRENCE_SPAN_MASK_V1_ACCEPTED = 1,
    PPOCCURRENCE_SPAN_MASK_V1_WORK_LIMIT = 2
} PPOccurrenceSpanMaskV1Outcome;

typedef struct {
    PPOccurrenceSpanMaskV1Outcome outcome;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint32_t value_production_label;
    uint64_t work_item_len;
} PPOccurrenceSpanMaskV1Result;

void ppoccurrence_span_mask_v1_plan_init(
    PPOccurrenceSpanMaskV1Plan *plan);

void ppoccurrence_span_mask_v1_plan_free(
    PPOccurrenceSpanMaskV1Plan *plan);

bool ppoccurrence_span_mask_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    Atom *const *answer_terms,
    size_t answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    uint32_t state_limit,
    uint32_t transition_limit,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool ppoccurrence_span_mask_v1_plan_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    const PPOccurrenceSpanMaskV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

/*
 * Project one selected terminal span.  scratch_actions needs at least
 * right-left entries.  The returned interval is the unique nonempty,
 * contiguous retained block; a selected bound terminal that violates this
 * property is a structural failure rather than an unhandled fallback.
 */
bool ppoccurrence_span_mask_v1_project_prevalidated(
    const PPOccurrenceSpanMaskV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t terminal_index,
    uint32_t left,
    uint32_t right,
    uint64_t work_limit,
    uint32_t *scratch_actions,
    uint32_t scratch_capacity,
    PPOccurrenceSpanMaskV1Result *out,
    char *error_buf,
    size_t error_buf_size);

bool ppoccurrence_span_mask_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    const PPOccurrenceSpanMaskV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

#endif
