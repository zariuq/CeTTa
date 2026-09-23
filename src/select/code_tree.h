#ifndef CETTA_SELECT_CODE_TREE_H
#define CETTA_SELECT_CODE_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Code tree over sampled coordinates: the match-decision filter, compiled.
 *
 * The decision it realizes is the candidate filter of the match-decision
 * contract: keep, in source order with multiplicity, every occurrence whose
 * pattern the sampled coordinates cannot refute against the observation.  A
 * coordinate refutes an occurrence only when both the observation and the
 * pattern carry a tag there and the tags differ.  Unknown on either side
 * refutes nothing.
 *
 * The tree branches on one coordinate per level, in coordinate order.  At a
 * node, occurrences whose pattern requires tag `v` at that coordinate live
 * under the branch for `v`; occurrences whose pattern leaves the coordinate
 * unconstrained live under the free branch.  A traversal with a known tag
 * visits the matching branch (if any) and the free branch; a traversal with
 * an unknown tag visits every branch.  Leaves hold occurrence indices.
 *
 * Selection marks the reached occurrences; the caller scans its source list
 * in order, which is exactly the contract's filter.  Occurrence indices are
 * the source positions, so they are distinct by construction.
 *
 * Laws (proved in Mettapedia, `MatchDecisionContract.CodeTree`):
 *   traverse(build(patterns), obs) marks i  <=>  no coordinate refutes pattern i
 *   selection == linear candidate filter, as a list
 *   insert adds exactly the new occurrence, when unrefuted; remove removes exactly one
 *   surviving rigid tags agree at every sampled, observed coordinate
 * This last law does not discharge variable bindings, repeated-variable
 * equalities, or any other obligation of the complete head matcher.
 *
 * Wiring: coordinates are the sampled paths a match decision already uses
 * (head symbol, arity, argument constructors, colon tag); tags are the
 * constructor identities at those paths; the observation is the query's
 * skeleton, demanded lazily through the lane's observation policy.
 */

/* Tag meaning "unconstrained" in a pattern, "unobserved" in an observation. */
#define CETTA_CODE_TREE_UNKNOWN UINT32_MAX

typedef struct CettaCodeTree CettaCodeTree;
typedef struct CettaCodeTreeCursor CettaCodeTreeCursor;

typedef struct {
    uint64_t node_visits;      /* nodes entered by traversals */
    uint64_t branch_misses;    /* known tag with no branch: an empty subtree skipped */
    uint64_t leaf_visits;
} CettaCodeTreeStats;

/* A coordinate may admit several tags (for example, expression arity and
 * expression head), all tags, and independently the unconstrained branch.
 * Tag arrays must remain valid throughout a selection. Tags are distinct.
 * The observer is read-only and returns false on failure, never an empty
 * candidate set in place of an error. */
typedef struct {
    const uint32_t *tags;
    uint32_t count;
    bool all_tags;
    bool include_free;
} CettaCodeTreeObservation;

typedef bool (*CettaCodeTreeObserveFn)(void *context, uint32_t coordinate,
                                      CettaCodeTreeObservation *observation);

/* A private cursor owns traversal scratch and source-order output. Reuse it
 * across selections of an immutable tree; mutation may require it to grow.
 * Selection uses an explicit traversal stack, independent of C stack depth.
 * Output is borrowed from the cursor until its next selection or destruction.
 * Failure sets out to NULL and count to zero. */
CettaCodeTreeCursor *cetta_code_tree_cursor_new(const CettaCodeTree *tree);
void cetta_code_tree_cursor_free(CettaCodeTreeCursor *cursor);
bool cetta_code_tree_select_observed(const CettaCodeTree *tree,
                                    CettaCodeTreeCursor *cursor,
                                    CettaCodeTreeObserveFn observe, void *context,
                                    const uint32_t **out, uint32_t *count,
                                    CettaCodeTreeStats *stats);



/*
 * Build from `occurrence_count` patterns, each an array of `coordinate_count`
 * tags (CETTA_CODE_TREE_UNKNOWN where the pattern does not constrain the
 * coordinate).  Occurrence `i` is `patterns[i]`.  Returns NULL on allocation
 * failure.  `coordinate_count` may be zero: the tree is one leaf.
 */
CettaCodeTree *cetta_code_tree_build(uint32_t coordinate_count,
                                     const uint32_t *const *patterns,
                                     uint32_t occurrence_count);

void cetta_code_tree_free(CettaCodeTree *tree);

uint32_t cetta_code_tree_coordinate_count(const CettaCodeTree *tree);
uint32_t cetta_code_tree_occurrence_capacity(const CettaCodeTree *tree);

/*
 * Traverse with an observation of `coordinate_count` tags and set
 * `marks[i] = 1` for every occurrence index reached.  `marks` must hold
 * `cetta_code_tree_occurrence_capacity(tree)` bytes and is not cleared here.
 * Returns false on argument or allocation failure; marks then remain unchanged.
 */
bool cetta_code_tree_traverse(const CettaCodeTree *tree,
                              const uint32_t *observation,
                              uint8_t *marks,
                              CettaCodeTreeStats *stats);

/*
 * Selection: traverse, then write the reached occurrence indices in ascending
 * order (source order) into `out`, which must hold the occurrence capacity.
 * Returns false on allocation or argument failure, with count set to zero.
 */
bool cetta_code_tree_select(const CettaCodeTree *tree,
                                const uint32_t *observation,
                                uint32_t *out, uint32_t *count,
                                CettaCodeTreeStats *stats);

/*
 * Add occurrence `index` with pattern `pattern` (coordinate_count tags).  The
 * index may exceed the current capacity; the tree grows.  Returns false on
 * allocation failure. Existing occurrences remain unchanged; empty interior
 * nodes may have been added. UINT32_MAX is not an occurrence index.
 */
bool cetta_code_tree_insert(CettaCodeTree *tree, uint32_t index,
                            const uint32_t *pattern);

/* Remove every leaf occurrence of `index`.  Returns how many leaves held it. */
uint32_t cetta_code_tree_remove(CettaCodeTree *tree, uint32_t index);

/*
 * The linear reference decision, for differential qualification: marks[i] = 1
 * iff no coordinate refutes pattern i against the observation.
 */
void cetta_code_tree_reference_marks(uint32_t coordinate_count,
                                     const uint32_t *const *patterns,
                                     uint32_t occurrence_count,
                                     const uint32_t *observation,
                                     uint8_t *marks);

/*
 * Residual coordinates after selection: those the tree did not decide, i.e.
 * every coordinate where the observation is unknown. This concerns rigid
 * tags only: it is not a residual variable-binding program. Writes up to
 * `coordinate_count` coordinate indices into `out`; returns the count.
 */
uint32_t cetta_code_tree_residual_coordinates(uint32_t coordinate_count,
                                              const uint32_t *observation,
                                              uint32_t *out);

#endif
