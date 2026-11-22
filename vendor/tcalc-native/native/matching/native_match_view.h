#ifndef TCALC_NATIVE_MATCH_VIEW_H
#define TCALC_NATIVE_MATCH_VIEW_H
#include "native_match.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque host syntax and its complete lexical/ownership context, by value. */
typedef struct { uint64_t syntax, context; } NmHostValue;
typedef enum {
    NM_VIEW_READY, NM_VIEW_DEFER, NM_VIEW_RESOURCE, NM_VIEW_FAULT, NM_VIEW_INTERRUPTED
} NmViewStatus;
typedef struct {
    NmNodeKind kind;
    uint64_t constant, children;
    NmVariableKey variable;
} NmViewNode;

/* A stable read view: callbacks neither evaluate syntax nor publish changes.
 * Constant identities must encode the caller's equality as a reflexive
 * equivalence relation. A value with another comparison policy must defer.
 * Variable keys are full identities, independent of their printed names.
 * Each key has one stable binding. NULL binding means an empty environment.
 * Child values retain their own lexical context. */
typedef struct {
    void *context;
    NmViewStatus (*inspect)(void *context, NmHostValue value, NmViewNode *node);
    NmViewStatus (*child)(void *context, NmHostValue value, uint64_t index, NmHostValue *child);
    NmViewStatus (*binding)(void *context, NmHostValue variable, bool *bound, NmHostValue *value);
} NmTermView;

typedef struct {
    size_t nodes, edges, variables, callbacks;
} NmImportLimits;
typedef struct {
    size_t inspections, children, bindings, callbacks;
} NmImportStats;
typedef struct NmPreparedMatch NmPreparedMatch;

/* Limits count owned table entries and host callback invocations, not source
 * language fuel or total CPU instructions. Source cycles and unsupported
 * shapes defer; allocation/limit exhaustion stays RESOURCE. All failures
 * return *out NULL and preserve the host. The prepared graph borrows host
 * values: retain their pinned view and owners through projection/release. */
NmViewStatus nm_match_prepare(const NmTermView *view, NmHostValue left, NmHostValue right,
                              NmImportLimits limits, NmPreparedMatch **out, NmImportStats *stats);
typedef struct { NmHostValue left, right; } NmHostConstraint;
/* Conjoin constraints in one private unification attempt. All constraints
 * share their scoped variables and initial environment. Two private tuple
 * roots and two child entries per constraint count against the import limits.
 * An empty conjunction is supported. The synthetic roots cannot be projected
 * as host values. Candidate occurrence and commit policies remain the caller's. */
NmViewStatus nm_match_prepare_constraints(const NmTermView *view,
                                          const NmHostConstraint *constraints, size_t count,
                                          NmImportLimits limits, NmPreparedMatch **out,
                                          NmImportStats *stats);
const NmUnifyInput *nm_match_input(const NmPreparedMatch *prepared);
/* Recover a source label, including valid zero-valued host encodings. Node
 * zero and the conjunction's synthetic roots have no source label. */
bool nm_match_origin(const NmPreparedMatch *prepared,uint64_t node,NmHostValue *origin);
void nm_match_prepared_free(NmPreparedMatch *prepared);

typedef struct {
    NmHostValue variable, value;
    NmVariableKey key;
    bool bound, changed;
} NmBindingProjection;

/* Projection is available only after UNIFIED. It returns the original
 * source values with their contexts; it neither constructs host syntax nor
 * modifies the host environment. Publication and retention belong to the
 * caller's binding/ownership protocol. */
bool nm_match_project(const NmPreparedMatch *prepared, const NmUnifyResult *result,
                       const NmUnifyWork *work, size_t slot, NmBindingProjection *projection);

typedef struct NmSolution NmSolution;
/* Capture a successful result's scoped binding projection. The snapshot owns
 * its copied key/value records, so the original prepared graph and workspace
 * may be released. It borrows the original host syntax owners and external
 * view context; keep those stable through release. A parent solution view is
 * retained automatically. Result/work must come from a successful call on
 * this prepared graph and stay stable during capture. No host binding is
 * published. On failure *out is NULL; failed projections are provider faults. */
NmViewStatus nm_solution_capture(const NmPreparedMatch *prepared, const NmUnifyResult *result,
                                 const NmUnifyWork *work, NmSolution **out);
/* The returned read view is borrowed until the solution is released. */
NmTermView nm_solution_view(const NmSolution *solution);
/* An additional owned reference; false on reference-count exhaustion. */
bool nm_solution_retain(NmSolution *solution);
void nm_solution_free(NmSolution *solution);
size_t nm_solution_entries(const NmSolution *solution);
size_t nm_solution_depth(const NmSolution *solution);
size_t nm_solution_changes(const NmSolution *solution);
/* Visit newly introduced bindings, newest refinement first. Each key is
 * introduced once in a valid monotone solution chain. Values retain their
 * original lexical/ownership contexts; zero-valued host encodings are valid.
 * The visitor borrows values and may stop by returning false. It neither
 * publishes nor chooses the host's grouped commit policy. Keep the solution
 * and its source owners alive throughout the visit. */
bool nm_solution_visit_changes(const NmSolution *solution,
                                bool (*visit)(void *context,NmVariableKey key,NmHostValue value),
                                void *context);

#ifdef __cplusplus
}
#endif
#endif
