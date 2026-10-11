#ifndef CETTA_OWNED_EXECUTION_H
#define CETTA_OWNED_EXECUTION_H

#include "call_outcome.h"
#include "eval_completion.h"
#include "match.h"
#include <assert.h>

/* Intrusive owned continuation links keep dialect frames at stable addresses.
 * Push takes one frame; pop transfers it back to the adapter for publication
 * or release. The adapter owns roots and demand, so scalar values need no
 * universal heap wrapper and the link does not confer source-read authority. */
typedef struct CettaOwnedFrameLink {
    struct CettaOwnedFrameLink *next;
} CettaOwnedFrameLink;

typedef struct {
    CettaOwnedFrameLink *top;
    size_t depth;
} CettaOwnedFrameStack;

static inline void cetta_owned_frame_push(
    CettaOwnedFrameStack *stack, CettaOwnedFrameLink *frame) {
    assert(stack && frame && stack->depth < SIZE_MAX);
    frame->next = stack->top;
    stack->top = frame;
    stack->depth++;
}

static inline CettaOwnedFrameLink *cetta_owned_frame_pop(
    CettaOwnedFrameStack *stack) {
    assert(stack && stack->top && stack->depth > 0u);
    CettaOwnedFrameLink *frame = stack->top;
    stack->top = frame->next;
    frame->next = NULL;
    stack->depth--;
    return frame;
}

/* A return loans its payload and environment from the computation owner.
 * Source expressions, completed values and conditional answers retain their
 * distinct provenance. Sequencing does not interpret syntax or memoize calls. */
typedef struct {
    CettaCallOutcome outcome;
    const Bindings *environment;
} CettaOwnedReturn;

typedef enum {
    CETTA_OWNED_EXECUTION_RETURN = 0,
    CETTA_OWNED_EXECUTION_COMPLETE,
    CETTA_OWNED_EXECUTION_PAUSED,
    CETTA_OWNED_EXECUTION_HANDOFF,
    CETTA_OWNED_EXECUTION_INTERRUPTED,
} CettaOwnedExecutionStep;

typedef CettaOwnedExecutionStep (*CettaOwnedExecutionNext)(
    void *computation, CettaOwnedReturn *returned,
    CettaEvalCompletion *completion);
typedef void (*CettaOwnedExecutionRelease)(void *computation);
/* False retains the pending return and complete computation at this boundary.
 * The consumer must commit its state only when accepting the return. */
typedef bool (*CettaOwnedReturnConsumer)(
    void *consumer, const CettaOwnedReturn *returned);

typedef struct {
    void *computation;
    CettaOwnedExecutionNext next;
    CettaOwnedExecutionRelease release;
    CettaOwnedReturn pending;
    /* Completion is authoritative at a terminal boundary. PAUSED never
     * certifies exhaustion, even if no incomplete reason has been recorded. */
    CettaEvalCompletion completion;
    CettaOwnedExecutionStep stop;
    uint64_t accepted;
    bool has_pending;
    bool terminal;
} CettaOwnedExecution;

/* Takes one computation owner, whose code, captures and read authority are
 * qualified by its adapter. The boundary itself allocates no value wrapper. */
void cetta_owned_execution_init(CettaOwnedExecution *execution,
    void *computation, CettaOwnedExecutionNext next,
    CettaOwnedExecutionRelease release);

/* An allowance counts accepted returns, not producer firings or public fuel.
 * A pause keeps a pending return, its owner and the consumer's accumulated
 * state; resumption neither recomputes nor redelivers an accepted occurrence. */
CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer);

/* Releases abandoned execution without undoing accepted returns or effects.
 * The caller must publish any retained borrowed return before this call. */
void cetta_owned_execution_cancel(CettaOwnedExecution *execution);

#endif
