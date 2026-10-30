#ifndef CETTA_NATIVE_QUERY_H
#define CETTA_NATIVE_QUERY_H

#include "space.h"

/*
 * The whole answer bag of a match, computed in one join loop, for an
 * observer that takes every answer.  The patterns' rows must form finite
 * ground relations (relational_plan.h); the conjunction then runs as a
 * variable-at-a-time intersection over flat bindings.  A space answers a
 * bag, so the order of the solutions is free.  Templates are data: each
 * solution instantiates them once per row combination giving that
 * solution, without evaluating them.
 */

typedef enum {
    /* The template's instances, as atoms. */
    CETTA_NATIVE_QUERY_COLLECT = 0,
    /* Each solution makes the effects in turn, then answers the template. */
    CETTA_NATIVE_QUERY_EFFECTS,
} CettaNativeQueryConsumer;

/* An addition of a data template's instance into a target space, or the
 * removal of every row equal to it.  `call` is the source of the effect,
 * the template in place of the instance, for the dialect. */
typedef struct {
    bool removal;
    Space *target;
    Atom *data;
    Atom *call;
} CettaNativeQueryEffect;

/* Make an effect for instances, with the dialect's semantics; the dialect
 * has already judged its shape.  False when the effect fails. */
typedef bool (*CettaNativeQueryApply)(void *context,
                                      const CettaNativeQueryEffect *effect,
                                      const AtomId *ids, size_t count);
/* Add the instances of an addition whose data is (h X Y), into a set that
 * keeps h as bits: h as its symbol atom and id, each instance as the two
 * ids of its columns in PAIRS.  The instances need no records. */
typedef bool (*CettaNativeQueryApplyPairs)(
    void *context, const CettaNativeQueryEffect *effect, Atom *head,
    AtomId head_id, const AtomId *pairs, size_t count);

typedef struct {
    Space *source;
    Atom *const *patterns;
    size_t pattern_count;
    /* COLLECT: the instances' template.  EFFECTS: what each solution
     * answers after its effects. */
    Atom *template_atom;
    CettaNativeQueryConsumer consumer;
    const CettaNativeQueryEffect *effects;
    size_t effect_count;
    CettaNativeQueryApply apply;
    /* Optional: additions into relations kept as bits, by pairs. */
    CettaNativeQueryApplyPairs apply_pairs;
    void *apply_context;
    /* Only the number of answers is wanted. */
    bool count_only;
    /* When set, the one pattern meets the elements of this expression, all
     * ground, in place of the source's rows; the source then names only the
     * term universe. */
    Atom *rows;
} CettaNativeQuery;

typedef enum {
    /* Not admitted: nothing happened, and the ordinary path answers. */
    CETTA_NATIVE_QUERY_DECLINED = 0,
    CETTA_NATIVE_QUERY_DONE,
    /* Admitted, then failed; some effects may have been made. */
    CETTA_NATIVE_QUERY_FAILED,
} CettaNativeQueryStatus;

typedef struct {
    /* The number of answers: one per solution and row combination. */
    uint64_t answers;
    /* Unless the query counts only, the expression of the answers,
     * allocated in the caller's arena. */
    Atom *collection;
} CettaNativeQueryResult;

/* Run the query.  Patterns and templates have the caller's bindings
 * applied; every variable of a template must occur in a pattern.  ARENA
 * holds temporary terms and the answers. */
CettaNativeQueryStatus cetta_native_query_run(
    const CettaNativeQuery *query, Arena *arena,
    CettaNativeQueryResult *result);

/* The number of answers of a conjunction over SPACE, its patterns under
 * SEED, by the native path; false when the native path declines. */
bool cetta_native_query_count(Space *space, Arena *scratch,
                              Atom *const *patterns, size_t pattern_count,
                              const Bindings *seed, uint64_t *count_out);

/* Whether native queries are enabled: CETTA_NATIVE_QUERY_REFERENCE=1 sends
 * every query down the ordinary path, the reference the native one must
 * agree with. */
bool cetta_native_query_enabled(void);

#endif
