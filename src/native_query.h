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
    /* An addition whose data is a variable: each instance is checked
     * before any change is made, and one headed by a symbol the dialect
     * refuses here (its rules and declarations) declines the run. */
    SymbolId refused_heads[4];
    uint8_t refused_head_count;
    /* An addition made once for each distinct instance its target does
     * not hold: the effect of adding a row only where it is absent. */
    bool insert;
} CettaNativeQueryEffect;

/* Make an effect for instances, with the dialect's semantics; the dialect
 * has already judged its shape.  Each instance is its own alpha key: ground,
 * or with its variables numbered as an alpha key numbers them.  False when
 * the effect fails. */
typedef bool (*CettaNativeQueryApply)(void *context,
                                      const CettaNativeQueryEffect *effect,
                                      const AtomId *ids, size_t count);
/* Add the instances of an addition whose data is (h X1 ... Xk), into a set
 * that may keep h in a relation: h as its symbol atom and id, each instance
 * as the k ids of its columns in TUPLES.  The instances need no records. */
typedef bool (*CettaNativeQueryApplyTuples)(
    void *context, const CettaNativeQueryEffect *effect, Atom *head,
    AtomId head_id, uint32_t arity, const AtomId *tuples, size_t count);

/* Add the instances of an addition into a set keeping a relation of deep
 * facts for HEAD, by their holes under SKELETON, the relation's spine when
 * the query began: COUNT instances of SKELETON->holes ids each, which need
 * no records. */
typedef bool (*CettaNativeQueryApplyHoles)(
    void *context, const CettaNativeQueryEffect *effect, SymbolId head,
    const CettaSkeleton *skeleton, const AtomId *holes, size_t count);

/* How a filter compares its operands' instances: by id, alike or apart,
 * or as integers, LEFT less than RIGHT or not. */
typedef enum {
    CETTA_NATIVE_FILTER_ALIKE,
    CETTA_NATIVE_FILTER_APART,
    CETTA_NATIVE_FILTER_LESS,
    CETTA_NATIVE_FILTER_NOT_LESS,
} CettaNativeFilterOp;

/* A comparison a filtered query's solutions meet (CettaNativeQuery.filters):
 * the instances of LEFT and RIGHT, a variable or an atom each, compared as
 * OP says. */
typedef struct {
    Atom *left;
    Atom *right;
    CettaNativeFilterOp op;
} CettaNativeQueryFilter;

/* The most comparisons a query's solutions meet. */
#define CETTA_NATIVE_QUERY_FILTERS 4u

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
    /* Optional: additions into relations, by the columns of each fact. */
    CettaNativeQueryApplyTuples apply_tuples;
    /* Optional: additions into relations of deep facts, by holes. */
    CettaNativeQueryApplyHoles apply_holes;
    void *apply_context;
    /* Only the number of answers is wanted. */
    bool count_only;
    /* The source reads everything before making anything: every effect
     * waits for the end of the run. */
    bool snapshot;
    /* When set, the one pattern meets the elements of this expression, all
     * ground, in place of the source's rows; the source then names only the
     * term universe. */
    Atom *rows;
    /* When set, the space each pattern reads, in order, all over the
     * source's term universe; otherwise every pattern reads the source. */
    Space *const *sources;
    /* When set (EFFECTS only), each solution first asks whether a row of
     * guard_space is the instance of guard_pattern, which the solution
     * makes ground: with none it makes the effects and answers
     * template_atom; with one it makes none and answers else_atom, or
     * nothing when else_empty.  The rows asked are those the query began
     * with: no effect changes a row the guard reads, but an insert
     * (CettaNativeQueryEffect.insert), which adds the guard's own instance,
     * and a solution meeting an instance an earlier one inserted reads it
     * held. */
    Space *guard_space;
    Atom *guard_pattern;
    Atom *else_atom;
    bool else_empty;
    /* When set, each solution compares the instances of each filter's left
     * and right by id: one that meets every filter goes on to the guard,
     * when there is one, and the effects, then answers; one that does not
     * answers else_atom, or nothing when else_empty, and reads no guard. */
    const CettaNativeQueryFilter *filters;
    size_t filter_count;
    /* When set, the template's applications headed by these variables
     * answer themselves where their values cannot be applied: each
     * solution asks head_inert of each value, and a value that can be
     * applied declines the run, which must then have made no effect: it
     * collects, or its effects wait for the end. */
    Atom *const *inert_heads;
    size_t inert_head_count;
    bool (*head_inert)(void *context, const TermUniverse *universe,
                       AtomId value);
    void *head_inert_context;
    /* When set, the solutions answer once for each distinct instance of
     * this template over the patterns' variables, as unique-atom keeps one
     * element of each: a solution whose instance an earlier one made
     * answers nothing and makes no effect.  An instance holding a variable
     * declines the run, which must then have made no effect: it collects,
     * or its effects wait for the end. */
    Atom *distinct_key;
    /* EFFECTS over the space's rows: the patterns the dialect reads afresh
     * for each solution of the patterns before them, after the effects
     * those solutions made, where an effect may change a row they read
     * (bit i for pattern i).  The run reads them with the patterns before
     * each read in their order, and makes its effects only if no instance
     * of one meets such a pattern under bindings it was read with: then
     * every read saw the rows the query began with.  Otherwise it
     * declines, having made none. */
    uint64_t afresh;
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

/* The run of a query a leg of which reads a relation of deep facts:
 * joins over facts holding variables, read by ids (native_open_query.c).
 * DECLINED, having made nothing, where it is not admitted. */
CettaNativeQueryStatus cetta_native_open_query_run(
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
