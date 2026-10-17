#ifndef CETTA_REGEX_SELECTION_V1_H
#define CETTA_REGEX_SELECTION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ordered programs retain the regular Pike realization when their
 * instructions do not inspect captures or invoke nested observations. */
typedef enum {
    CETTA_RE_V1_FAIL, CETTA_RE_V1_MATCH, CETTA_RE_V1_CHAR,
    CETTA_RE_V1_CLASS, CETTA_RE_V1_ANY, CETTA_RE_V1_JUMP,
    CETTA_RE_V1_SPLIT, CETTA_RE_V1_SAVE, CETTA_RE_V1_ASSERT,
    CETTA_RE_V1_BACKREF, CETTA_RE_V1_LOOKAHEAD, CETTA_RE_V1_LOOKBEHIND
} CettaReOpcodeV1;
typedef enum {
    CETTA_RE_V1_TEXT_START, CETTA_RE_V1_TEXT_END,
    CETTA_RE_V1_LINE_START, CETTA_RE_V1_LINE_END,
    CETTA_RE_V1_WORD_BOUNDARY, CETTA_RE_V1_NOT_WORD_BOUNDARY,
    CETTA_RE_V1_ASCII_WORD_BOUNDARY, CETTA_RE_V1_NOT_ASCII_WORD_BOUNDARY
} CettaReAssertionV1;
typedef struct { uint32_t low, high; } CettaReRangeV1;
typedef struct { uint32_t scalar, canonical; } CettaReFoldPairV1;
/* BACKREF: argument is a nonzero group, second is exact/ASCII-simple/
 * provider-simple comparison (0/1/2). An open or unmatched group fails.
 * LOOK*: second is its body entry, next its outer continuation; argument
 * bit zero negates the observation, bit one consumes the body backwards.
 * A positive observation atomically commits its winner's capture snapshot;
 * a negative observation publishes no body captures. Neither moves the
 * outer cursor. Forward LOOKBEHIND selects written branch priority among
 * all bodies ending at that cursor, using nearest start only for ties.
 * Reverse LOOKBEHIND consumes from that cursor using an explicitly
 * reverse-compiled body. It is not an implicit JavaScript profile. */
typedef struct { uint32_t opcode, next, second, argument; } CettaReInstructionV1;
typedef struct {
    const CettaReInstructionV1 *instructions;
    uint32_t instruction_len, entry, group_len;
    const CettaReRangeV1 *ranges;
    uint32_t range_len;
    const CettaReRangeV1 *word_ranges;
    uint32_t word_range_len;
    const char *const *group_names;
    /* A look body has strictly lower rank than its invocation; ordinary
     * edges never increase rank. This finite certificate is checked once. */
    const uint32_t *ranks;
    /* Sorted scalar-to-equivalence-representative map for simple folding.
     * Scalars absent from this immutable provider map represent themselves. */
    const CettaReFoldPairV1 *fold_pairs;
    uint32_t fold_pair_len;
} CettaReProgramSpecV1;
typedef struct CettaReProgramV1 CettaReProgramV1;

typedef enum {
    CETTA_RE_V1_FOUND, CETTA_RE_V1_NO_MATCH, CETTA_RE_V1_INCOMPLETE,
    CETTA_RE_V1_INVALID_UTF8, CETTA_RE_V1_INVALID_ARGUMENT,
    CETTA_RE_V1_ALLOCATION_FAILURE, CETTA_RE_V1_CANCELLED
} CettaReOutcomeV1;
typedef enum { CETTA_RE_V1_SEARCH, CETTA_RE_V1_PREFIX, CETTA_RE_V1_FULL } CettaReModeV1;
typedef struct { bool participated; size_t start, end; } CettaReCaptureV1;
typedef struct {
    CettaReOutcomeV1 outcome;
    CettaReCaptureV1 *groups;
    uint32_t group_len;
    uint64_t state_visits;
} CettaReResultV1;
typedef struct {
    uint64_t work_budget;
    bool (*cancelled)(void *context);
    void *context;
} CettaReExecutionPolicyV1;

/* Ingress validates once and copies all arrays. Replacement is atomic. Group
 * zero is the whole match; SAVE slots 2g and 2g+1 address explicit groups. */
bool cetta_re_v1_program(const CettaReProgramSpecV1 *, CettaReProgramV1 **,
                         char *error, size_t error_size);
CettaReProgramV1 *cetta_re_v1_retain(CettaReProgramV1 *);
void cetta_re_v1_release(CettaReProgramV1 *);
const char *cetta_re_v1_group_name(const CettaReProgramV1 *, uint32_t group);
uint32_t cetta_re_v1_group_count(const CettaReProgramV1 *);
bool cetta_re_v1_store(const CettaReProgramV1 *, uint8_t **, size_t *, char *, size_t);
bool cetta_re_v1_restore(const uint8_t *, size_t, CettaReProgramV1 **, char *, size_t);

/* start is an absolute byte boundary in the original input, so assertions
 * retain their surrounding context. Full matching requires start zero.
 * A zero work budget means no caller-imposed bound; a reached nonzero budget
 * is incomplete, never no-match. Scratch belongs to this invocation. */
CettaReResultV1 cetta_re_v1_find(const CettaReProgramV1 *, const uint8_t *, size_t,
                               size_t start, CettaReModeV1, uint64_t work_budget);
bool cetta_re_v1_valid_utf8(const uint8_t *, size_t);
/* For repeated operations on the same already validated immutable input.
 * The caller retains the bytes; no decoding pass is repeated per match. */
CettaReResultV1 cetta_re_v1_find_valid_utf8(const CettaReProgramV1 *, const uint8_t *, size_t,
                                         size_t, CettaReModeV1, uint64_t);
/* Cancellation is cooperative and distinct from closed absence or a reached
 * work budget. The callback and its context are borrowed for this call. */
CettaReResultV1 cetta_re_v1_find_policy_valid_utf8(const CettaReProgramV1 *, const uint8_t *, size_t,
    size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);
typedef struct {
    const uint8_t *bytes;
    size_t byte_len, cursor, last_end;
    bool has_previous, finished;
} CettaReCursorV1;
/* Borrow one immutable input. Nonoverlapping iteration suppresses an empty
 * answer immediately adjoining the preceding answer's end, and advances
 * through complete scalars. End observation is stable on subsequent calls. */
bool cetta_re_v1_cursor(CettaReCursorV1 *, const uint8_t *, size_t);
CettaReResultV1 cetta_re_v1_next(const CettaReProgramV1 *, CettaReCursorV1 *, uint64_t);
CettaReResultV1 cetta_re_v1_next_policy(const CettaReProgramV1 *, CettaReCursorV1 *,
                                      const CettaReExecutionPolicyV1 *);

typedef struct CettaReRelationV1 CettaReRelationV1;
/* Enumerate accepting simple outer graph paths, not nonoverlapping matches.
 * Every occurrence is delivered, including equal spans/captures reached by
 * distinct alternatives. SEARCH visits all scalar starts in ascending order,
 * with branch-priority depth-first delivery at each start; PREFIX fixes the
 * supplied start, and FULL admits only paths ending at the original input end.
 * Regular programs cut repeated (instruction, position) ancestors; extended
 * programs also include the full capture snapshot in that key. Nested positive
 * observations remain atomic selected winners; negative ones require completed
 * absence. Their discarded body alternatives are not outer occurrences.
 *
 * Open retains the program and copies the immutable input once. Replacement of
 * *out is atomic. On failure, error (when non-NULL) names the outcome and the
 * previous owner is unchanged. All context assertions see the original input. */
bool cetta_re_v1_relation_open(CettaReProgramV1 *, const uint8_t *, size_t,
    size_t start, CettaReModeV1, CettaReRelationV1 **out, CettaReOutcomeV1 *error);
/* The work budget applies afresh to this call; zero is unbounded. INCOMPLETE or
 * CANCELLED retains the exact residual, including partially compared references
 * and nested observations. Resumption does not repeat published occurrences.
 * Work counts driver transitions (including one per compared reference scalar),
 * not CPU instructions: snapshot/hash and provider lookup costs are separate.
 * A FOUND result is caller-owned. Only NO_MATCH establishes exhaustion; the
 * exhausted observation is stable. A cursor is not concurrently advanceable. */
CettaReResultV1 cetta_re_v1_relation_next(CettaReRelationV1 *, const CettaReExecutionPolicyV1 *);
void cetta_re_v1_relation_release(CettaReRelationV1 *);
void cetta_re_v1_result_free(CettaReResultV1 *);
const char *cetta_re_v1_outcome_name(CettaReOutcomeV1);

#endif
