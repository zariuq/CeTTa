#ifndef CETTA_LANGDEF_COMPILED_CURSOR_V1_H
#define CETTA_LANGDEF_COMPILED_CURSOR_V1_H

#include "parser_occurrence_fold_v1.h"
#include "parser_occurrence_span_mask_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"
#include "relational_state_program_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_LANGDEF_COMPILED_CURSOR_V1_ABI UINT32_C(6)
#define CETTA_LANGDEF_COMPILED_CURSOR_V1_SYMBOL \
    "cetta_langdef_compiled_cursor_v1"

typedef bool (*CettaLangDefCompiledProgramInitV1)(
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size);

typedef const char *(*CettaLangDefCompiledDigestV1)(void);

typedef bool (*CettaLangDefCompiledFoldInitV1)(
    const PPGuardedLexCursorV1Program *program,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*CettaLangDefCompiledStateInitV1)(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPRelationalStateProgramV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*CettaLangDefCompiledOccurrenceSpanMaskInitV1)(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    uint32_t abi_version;
    CettaLangDefCompiledProgramInitV1 program_init;
    CettaLangDefCompiledDigestV1 program_digest;
    CettaLangDefCompiledFoldInitV1 occurrence_fold_init;
    CettaLangDefCompiledDigestV1 occurrence_fold_digest;
    CettaLangDefCompiledOccurrenceSpanMaskInitV1 occurrence_span_mask_init;
    CettaLangDefCompiledDigestV1 occurrence_span_mask_digest;
    CettaLangDefCompiledStateInitV1 relational_state_init;
    CettaLangDefCompiledDigestV1 relational_state_digest;
} CettaLangDefCompiledCursorV1;

typedef const CettaLangDefCompiledCursorV1 *
    (*CettaLangDefCompiledCursorEntryV1)(void);

#endif /* CETTA_LANGDEF_COMPILED_CURSOR_V1_H */
