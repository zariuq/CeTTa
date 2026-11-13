#ifndef CETTA_TRANSFORM_CORE_H
#define CETTA_TRANSFORM_CORE_H

#include "atom.h"
#include "space.h"
#include "native_query.h"
#include "symbol.h"

#include <stddef.h>

/* The public operation and its evaluator-private names.  Shape recognition
 * is separate from profile/library admission and never evaluates a call. */
typedef enum {
    CETTA_TRANSFORM_NAME_TRANSFORM = 0,
    CETTA_TRANSFORM_NAME_SPLIT,
    CETTA_TRANSFORM_NAME_PROPOSE,
    CETTA_TRANSFORM_NAME_INST,
    CETTA_TRANSFORM_NAME_FIRE,
    CETTA_TRANSFORM_NAME_FIRE_INTO,
    CETTA_TRANSFORM_NAME_RUN,
} CettaTransformName;

/* Symbol ids are cached per thread and per symbol-table incarnation.  Builtin
 * spellings and impossible arities never enter that cache. */
SymbolId cetta_transform_name_id(CettaTransformName name);
bool cetta_transform_call_head_slow(SymbolId head, CettaExprLen arity);
bool cetta_transform_native_call_head_slow(SymbolId head, CettaExprLen arity);
bool cetta_transform_run_call_head_slow(SymbolId head, CettaExprLen arity);
bool cetta_transform_name_head_slow(SymbolId head);
/* PeTTa code: alpha-rename its parameters and bind the renamed variables to
 * VALUES as data. Body syntax is preserved at every argument demand. */
Atom *cetta_transform_bind_code(Arena *arena, Atom *parameters,
                                 Atom *values, Atom *body);

static inline bool cetta_transform_call_head(SymbolId head, CettaExprLen arity) {
    return arity >= 2u && arity <= 4u && head != SYMBOL_ID_NONE &&
        !symbol_id_is_builtin(head) &&
        !(symbol_flags(g_symbols, head) & CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME) &&
        cetta_transform_call_head_slow(head, arity);
}

static inline bool cetta_transform_native_call_head(SymbolId head,
                                                    CettaExprLen arity) {
    return arity >= 2u && arity <= 4u && head != SYMBOL_ID_NONE &&
        !symbol_id_is_builtin(head) &&
        !(symbol_flags(g_symbols, head) & CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME) &&
        cetta_transform_native_call_head_slow(head, arity);
}

static inline bool cetta_transform_run_call_head(SymbolId head,
                                                 CettaExprLen arity) {
    return arity == 4u && head != SYMBOL_ID_NONE &&
        !symbol_id_is_builtin(head) &&
        !(symbol_flags(g_symbols, head) & CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME) &&
        cetta_transform_run_call_head_slow(head, arity);
}

static inline bool cetta_transform_name_head(SymbolId head) {
    return head != SYMBOL_ID_NONE && !symbol_id_is_builtin(head) &&
        !(symbol_flags(g_symbols, head) & CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME) &&
        cetta_transform_name_head_slow(head);
}

/* One firing of a transform: commit the first kernel candidate of the
 * sources, in proposal order, whose footprint validates, remove its takes and
 * answer (tf:fired INSTANCES).  Proposals use the spaces' occurrence cursors
 * and one trail-backed binding environment.  The native query engine joins
 * eligible legs.  There is no authored fallback: a declined call has no
 * second implementation to run. */

/* The library a firing realizes: the two dialects' libraries differ in how a
 * commit removes a taken atom and in what an error a mutation answers does. */
typedef enum {
    /* lib/petta/lib_transform.metta: takes consume the required equal
     * occurrences; a mutation's error is raised. */
    CETTA_TRANSFORM_DIALECT_PETTA = 0,
    /* lib/lib_transform.metta: remove-atom removes one copy, so a take removes
     * exactly the copies it takes; mutation errors propagate as HE values. */
    CETTA_TRANSFORM_DIALECT_HE,
} CettaTransformDialect;

typedef struct {
    void *context;
    CettaTransformDialect dialect;
    /* Pure lookup of the space named by a conjunct. It may be queried during
     * planning and again after earlier bindings/effects. False for an operand
     * that names no space; true with *space_out NULL for a location that
     * holds no space yet, which reads as empty. */
    bool (*resolve_space)(void *context, Atom *space_ref, Space **space_out);
    /* (remove-atom SPACE ATOM) and (add-atom SPACE ATOM) through the caller's
     * contracts: NULL, or the error the builtin answers. */
    Atom *(*remove_atom)(void *context, Atom *space_ref, Atom *atom);
    Atom *(*add_atom)(void *context, Atom *space_ref, Atom *atom);
    /* A firing's exact take, independent of the public pattern selector:
     * COPIES stored atoms that are ATOM
     * leave SPACE, the first by position.  A stored atom is ATOM when it is
     * the same atom or, for an atom with variables, the same but for their
     * names; one ATOM only unifies with stays.  NULL, or the error answered
     * when SPACE holds fewer, with nothing removed. */
    Atom *(*take_atom)(void *context, Atom *space_ref, Atom *atom,
                       size_t copies);
    /* Optional prepared take.  True retains a completed selection of COPIES
     * stored atoms that are ATOM, with how many SPACE holds, through an
     * uninterrupted first publication; false declines reuse and keeps the
     * ordinary validate/take path.  Commit consumes the handle; discard
     * releases an unused or invalid candidate. */
    bool (*prepare_take)(void *context, Atom *space_ref, Atom *atom,
                         size_t copies, void **prepared_out,
                         size_t *stored_out);
    Atom *(*commit_take)(void *context, void *prepared);
    void (*discard_take)(void *context, void *prepared);
    /* Results of a let's code, in order. HE Error values stay in the list
     * for its normal-value preference and error multiplicity; a PeTTa raise
     * or interrupted host evaluation returns false with *error_out set.
     * Bound source variables arrive protected, as the dialect requires. */
    bool (*evaluate)(void *context, Arena *a, Atom *expression,
                     Atom ***results_out, size_t *len_out, Atom **error_out);
    /* Optional.  The same results for the code as the transform holds it,
     * its variables read from BINDINGS, where evaluate is given the code
     * with their values written in.  The code is then one term for every
     * candidate of a firing, which the host need translate once.
     * VARIABLES, when not NULL, is the tuple of the variables the code holds
     * that BINDINGS leaves unbound, which its evaluation may bind: then
     * *givens_out holds, for each result, that tuple as its evaluation left
     * it. */
    bool (*evaluate_bound)(void *context, Arena *a, Atom *expression,
                           const Bindings *bindings, Atom *variables,
                           Atom ***results_out, Atom ***givens_out,
                           size_t *len_out, Atom **error_out);
    /* Optional.  A let that is a test of integers, as the comparison a
     * join of the conjuncts before it can make while it joins
     * (CettaNativeQueryFilter): the let passes exactly where the
     * comparison is met, wherever every value compared is an integer the
     * join can read; for any other value the join declines and the let is
     * evaluated.  False for a let that is no such test. */
    bool (*guard_filter)(void *context, Arena *a, Atom *pattern,
                         Atom *expression, CettaNativeQueryFilter *filter);
    /* Optional native-work meter. Refusal records an incomplete episode in
     * the host; it is never an empty answer or permission to retry elsewhere.
     * A firing reserves its commit step before any store mutation. */
    bool (*spend)(void *context, uint64_t work);
} CettaTransformHost;

typedef enum {
    CETTA_TRANSFORM_DECLINED = 0, /* no native answer; never an authored fallback */
    CETTA_TRANSFORM_FIRED,        /* *answer_out is (tf:fired INSTANCES),
                                   * or the count tf:fire-into added */
    CETTA_TRANSFORM_NONE,         /* no candidate validates: no answer */
    CETTA_TRANSFORM_ERROR,        /* *answer_out is the error */
    CETTA_TRANSFORM_INTERRUPTED,  /* host budget/cancellation; no fallback */
} CettaTransformOutcome;

/* `call` is (tf:fire SPACE CONJUNCTS TEMPLATE), or (tf:fire-into SPACE
 * CONJUNCTS TEMPLATE DESTINATION), whose instances are added to DESTINATION
 * after the commit and whose answer is how many, with its arguments as data.
 * Nothing is evaluated or removed before the call is admitted. */
CettaTransformOutcome cetta_transform_fire(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

/* The names cands and pin correspond to proposals/guided in
 * Languages/MeTTa/PeTTa/LibTransform.lean.
 * `call` is (tf:propose SPACE CONJUNCTS TEMPLATE): the kernel's matches
 * against the store at the call, *answer_out the expression of each
 * (tf:cand GUIDE), its read, take and unique entries (MODE SPACE ATOM) in
 * conjunct order.  The order of the proposals is free, as a space's answers
 * are.  FIRED, ERROR when code a `let` ran answered an error, or DECLINED. */
CettaTransformOutcome cetta_transform_propose(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);
/* The explicit materializing walker, used by ineligible sources and as the
 * first-answer differential reference. */
CettaTransformOutcome cetta_transform_propose_eager(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

/* Evaluator-private resumable entry proposals.  Admission is restricted to a
 * static, effect-free run of read/take conjuncts.  Each FIRED result is one
 * independently owned (tf:cand GUIDE); NONE is exhaustion, DECLINED is a
 * pre-publication refusal, and ERROR/INTERRUPTED retain their usual meanings.
 * No cursor atom or other public language object is exposed. */
typedef struct CettaTransformCandidateCursor CettaTransformCandidateCursor;
bool cetta_transform_candidate_cursor_open(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    CettaTransformCandidateCursor **cursor_out);
CettaTransformOutcome cetta_transform_candidate_cursor_next(
    CettaTransformCandidateCursor *cursor, Atom **proposal_out);
void cetta_transform_candidate_cursor_close(
    CettaTransformCandidateCursor *cursor);

/* `call` is (tf:inst CONJUNCTS GUIDE): a branch's sources, each kernel read
 * or take pinned to the atom its proposal matched.  Data only: false when the
 * call is not of the library's shape. */
bool cetta_transform_inst(Arena *a, Atom *call, Atom **answer_out);

/* Prepare a public transform for the native step.  `work` owns the returned
 * conjuncts and template. PeTTa protects caller templates and syntax-only
 * let evaluators; bound code reads its variables from the firing's value
 * environment. HE keeps its conjuncts in the data wrapper tf:cs. */
bool cetta_transform_prepare(Arena *work, Atom *sources, Atom *template_atom,
                             CettaTransformDialect dialect, bool caller,
                             bool bound_code,
                             Atom **conjuncts_out, Atom **template_out,
                             Atom **error_out);

/* Lower (tf:run SPACE SOURCES TEMPLATE DESTINATION) to ordinary let and
 * superpose forms over native proposals, guiding and firing.  The returned
 * expression is owned by `a`; malformed HE sources produce an Error value.
 * NULL means the call is not admitted. */
Atom *cetta_transform_lower(Arena *a, Atom *call,
                            const CettaTransformHost *host);

/* (tf:split COMMA INSTANCES): the instances to superpose for routing. */
Atom *cetta_transform_split(Arena *a, Atom *call,
                            CettaTransformDialect dialect);

/* `call` is (transform SPACE SOURCES TEMPLATE) or (transform SPACE SOURCES
 * TEMPLATE DESTINATION) with DESTINATION a space, as tf:run runs it under an
 * observer that collects every answer: each branch in turn fires on the store
 * the branches before it left.  FIRED with *collection_out the expression of
 * the answers: TEMPLATE's instances, as data, or each branch's routed count;
 * NONE never; ERROR with *collection_out the error; DECLINED before any
 * branch fired. HE uses the ordinary evaluator's branch/error selection
 * over native candidates and firings instead of this PeTTa batch shortcut. */
CettaTransformOutcome cetta_transform_collect(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **collection_out, uint64_t *count_out);

/* How the read-only step reads. */
typedef enum {
    /* Occurrence order: a caller that continues each answer before the next. */
    CETTA_STEP_READS_ORDERED = 0,
    /* A bag: the caller takes every answer, so their order is free; rows the
     * native query engine declines are read by the occurrence cursors. */
    CETTA_STEP_READS_BAG,
    /* A bag the native query engine answers alone: anything it declines,
     * the step declines, for a caller whose ordinary path answers then. */
    CETTA_STEP_READS_ENGINE,
} CettaStepReads;

/* match as the step with reads only: (match SPACE PATTERN TEMPLATE)'s
 * answers, each read of PATTERN (or of each part of a conjunction) on the
 * store at the call answering TEMPLATE's instance as data.  FIRED, or
 * INTERRUPTED when its meter stops, or DECLINED before reading. */
CettaTransformOutcome cetta_transform_reads(
    const CettaTransformHost *host, Arena *a, Atom *space, Atom *pattern,
    Atom *template_atom, CettaStepReads mode, Atom **collection_out,
    uint64_t *count_out);

#endif /* CETTA_TRANSFORM_CORE_H */
