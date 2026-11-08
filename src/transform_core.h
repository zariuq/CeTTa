#ifndef CETTA_TRANSFORM_CORE_H
#define CETTA_TRANSFORM_CORE_H

#include "atom.h"
#include "space.h"

#include <stddef.h>

/* tf:fire (lib/petta/lib_transform.metta) is one firing of a transform: it
 * commits the first kernel candidate of the sources, in proposal order, whose
 * footprint validates, removes its takes and answers (tf:fired INSTANCES).
 * This realization proposes the same rows in the same order, directly over
 * the spaces' occurrence cursors and one trail-backed binding environment,
 * and stops at the first validating candidate when no `let` conjunct can run
 * code.  It validates the footprint by the multiplicity each taken atom
 * needs, and removes the takes through the caller's remove-atom and add-atom in the
 * library's order, so the store it leaves and its answer are the authored
 * firing's.
 *
 * It is admitted only while the live equations of every operation the
 * library defines, and the declarations of `transform`, are the library's
 * own; otherwise it declines and the authored definition runs.  A call whose
 * conjuncts it cannot read the library's way also declines. */

/* The library a firing realizes: the two dialects' libraries differ in how a
 * commit removes a taken atom and in what an error a mutation answers does. */
typedef enum {
    /* lib/petta/lib_transform.metta: remove-atom removes every copy, so a take
     * removes them all and stores back the copies the firing does not take;
     * a mutation's error is raised. */
    CETTA_TRANSFORM_DIALECT_PETTA = 0,
    /* lib/lib_transform.metta: remove-atom removes one copy, so a take removes
     * exactly the copies it takes; a mutation's error is a value the library
     * binds and goes on from, and the instances routed are counted as added. */
    CETTA_TRANSFORM_DIALECT_HE,
} CettaTransformDialect;

typedef struct {
    void *context;
    CettaTransformDialect dialect;
    /* The space a conjunct's space reference names.  False for an operand
     * that names no space; true with *space_out NULL for a location that
     * holds no space yet, which reads as empty. */
    bool (*resolve_space)(void *context, Atom *space_ref, Space **space_out);
    /* (remove-atom SPACE ATOM) and (add-atom SPACE ATOM) through the caller's
     * contracts: NULL, or the error the builtin answers. */
    Atom *(*remove_atom)(void *context, Atom *space_ref, Atom *atom);
    Atom *(*add_atom)(void *context, Atom *space_ref, Atom *atom);
    /* Every result of evaluating a `let` conjunct's code, in order.  Its
     * variables' values arrive quoted, as the library passes them.  False
     * with *error_out set when evaluation answers an error. */
    bool (*evaluate)(void *context, Arena *a, Atom *expression,
                     Atom ***results_out, size_t *len_out, Atom **error_out);
} CettaTransformHost;

typedef enum {
    CETTA_TRANSFORM_DECLINED = 0, /* the authored definition runs */
    CETTA_TRANSFORM_FIRED,        /* *answer_out is (tf:fired INSTANCES),
                                   * or the count tf:fire-into added */
    CETTA_TRANSFORM_NONE,         /* no candidate validates: no answer */
    CETTA_TRANSFORM_ERROR,        /* *answer_out is the error */
} CettaTransformOutcome;

/* Read-only: whether the live presentation is the dialect's library at
 * `library_path`.  The verdict is cached per program revision. */
bool cetta_transform_presentation_admitted(Space *program,
                                           const char *library_path,
                                           CettaTransformDialect dialect);

/* `call` is (tf:fire SPACE CONJUNCTS TEMPLATE), or (tf:fire-into SPACE
 * CONJUNCTS TEMPLATE DESTINATION), whose instances are added to DESTINATION
 * after the commit and whose answer is how many, with its arguments as data.
 * Nothing is evaluated or removed before the call is admitted. */
CettaTransformOutcome cetta_transform_fire(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

/* `call` is (tf:proposals SPACE CONJUNCTS TEMPLATE): the kernel's matches
 * against the store at the call, *answer_out the expression of each
 * (tf:kmatch GUIDE), its read, take and unique entries (MODE SPACE ATOM) in
 * conjunct order.  The order of the proposals is free, as a space's answers
 * are.  FIRED, ERROR when code a `let` ran answered an error, or DECLINED. */
CettaTransformOutcome cetta_transform_propose(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

/* `call` is (tf:guided CONJUNCTS GUIDE): a branch's sources, each kernel read
 * or take pinned to the atom its proposal matched.  Data only: false when the
 * call is not of the library's shape. */
bool cetta_transform_guided(Arena *a, Atom *call, Atom **answer_out);

/* `call` is (transform SPACE SOURCES TEMPLATE) or (transform SPACE SOURCES
 * TEMPLATE DESTINATION) with DESTINATION a space, as tf:run runs it under an
 * observer that collects every answer: each branch in turn fires on the store
 * the branches before it left.  FIRED with *collection_out the expression of
 * the answers: TEMPLATE's instances, as data, or each branch's routed count;
 * NONE never; ERROR with *collection_out the error; DECLINED before any
 * branch fired. */
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
 * DECLINED, having read nothing. */
CettaTransformOutcome cetta_transform_reads(
    const CettaTransformHost *host, Arena *a, Atom *space, Atom *pattern,
    Atom *template_atom, CettaStepReads mode, Atom **collection_out,
    uint64_t *count_out);

#endif /* CETTA_TRANSFORM_CORE_H */
