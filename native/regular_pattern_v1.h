#ifndef CETTA_REGULAR_PATTERN_V1_H
#define CETTA_REGULAR_PATTERN_V1_H
#include "regular_token_compiler_v1.h"

typedef struct CettaRegularPatternV1 CettaRegularPatternV1;

/* Immutable single-pattern regular-language decisions (accept tag zero).
 * This is not a prioritized-match or capture program. Compiler, pattern and
 * per-input cursor lifetimes are independent. */
bool cetta_regular_pattern_v1_compile(CettaRegularTokenCompilerV1 *, Arena *, Atom *regex,
    CettaRegularPatternV1 **out, char *error, size_t error_size);
CettaRegularPatternV1 *cetta_regular_pattern_v1_retain(CettaRegularPatternV1 *);
void cetta_regular_pattern_v1_release(CettaRegularPatternV1 *);
const RSDFAV1Program *cetta_regular_pattern_v1_program(const CettaRegularPatternV1 *);

/* Restore accepts either file bytes or a baked const byte array; it owns its
 * decoded arrays and borrows neither. No determinization, source admission,
 * language analysis or recognizer-table construction takes place. */
bool cetta_regular_pattern_v1_store(const CettaRegularPatternV1 *, uint8_t **bytes, size_t *len,
    char *error, size_t error_size);
bool cetta_regular_pattern_v1_restore(const uint8_t *, size_t,
    CettaRegularPatternV1 **out, char *error, size_t error_size);
#endif
