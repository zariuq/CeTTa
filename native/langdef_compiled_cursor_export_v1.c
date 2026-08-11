#include "native/langdef_compiled_cursor_v1.h"

#ifndef CETTA_LANGDEF_COMPILED_PREFIX
#error "CETTA_LANGDEF_COMPILED_PREFIX must name a generated cursor"
#endif

#define CETTA_LANGDEF_JOIN_RAW(left, right) left##right
#define CETTA_LANGDEF_JOIN(left, right) CETTA_LANGDEF_JOIN_RAW(left, right)
#define CETTA_LANGDEF_PROGRAM_INIT \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, _program_init)
#define CETTA_LANGDEF_PROGRAM_DIGEST \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, _program_digest)
#define CETTA_LANGDEF_FOLD_INIT \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _occurrence_fold_plan_init)
#define CETTA_LANGDEF_FOLD_DIGEST \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _occurrence_fold_plan_digest)
#define CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_INIT \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _occurrence_span_mask_plan_init)
#define CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_DIGEST \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _occurrence_span_mask_plan_digest)
#define CETTA_LANGDEF_STATE_INIT \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _state_program_plan_init)
#define CETTA_LANGDEF_STATE_DIGEST \
    CETTA_LANGDEF_JOIN(CETTA_LANGDEF_COMPILED_PREFIX, \
                       _state_program_plan_digest)

extern bool CETTA_LANGDEF_PROGRAM_INIT(
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *CETTA_LANGDEF_PROGRAM_DIGEST(void);
extern bool CETTA_LANGDEF_FOLD_INIT(
    const PPGuardedLexCursorV1Program *program,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *CETTA_LANGDEF_FOLD_DIGEST(void);
#ifdef CETTA_LANGDEF_COMPILED_HAS_OCCURRENCE_SPAN_MASK
extern bool CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_INIT(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_DIGEST(void);
#endif
#ifdef CETTA_LANGDEF_COMPILED_HAS_STATE_PROGRAM
extern bool CETTA_LANGDEF_STATE_INIT(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPRelationalStateProgramV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *CETTA_LANGDEF_STATE_DIGEST(void);
#endif

const CettaLangDefCompiledCursorV1 *cetta_langdef_compiled_cursor_v1(void) {
    static const CettaLangDefCompiledCursorV1 descriptor = {
        .abi_version = CETTA_LANGDEF_COMPILED_CURSOR_V1_ABI,
        .program_init = CETTA_LANGDEF_PROGRAM_INIT,
        .program_digest = CETTA_LANGDEF_PROGRAM_DIGEST,
        .occurrence_fold_init = CETTA_LANGDEF_FOLD_INIT,
        .occurrence_fold_digest = CETTA_LANGDEF_FOLD_DIGEST,
#ifdef CETTA_LANGDEF_COMPILED_HAS_OCCURRENCE_SPAN_MASK
        .occurrence_span_mask_init =
            CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_INIT,
        .occurrence_span_mask_digest =
            CETTA_LANGDEF_OCCURRENCE_SPAN_MASK_DIGEST,
#else
        .occurrence_span_mask_init = NULL,
        .occurrence_span_mask_digest = NULL,
#endif
#ifdef CETTA_LANGDEF_COMPILED_HAS_STATE_PROGRAM
        .relational_state_init = CETTA_LANGDEF_STATE_INIT,
        .relational_state_digest = CETTA_LANGDEF_STATE_DIGEST,
#else
        .relational_state_init = NULL,
        .relational_state_digest = NULL,
#endif
    };
    return &descriptor;
}
