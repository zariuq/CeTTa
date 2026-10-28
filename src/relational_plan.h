#ifndef CETTA_RELATIONAL_PLAN_H
#define CETTA_RELATIONAL_PLAN_H

#include "space.h"
#include "search_machine.h"

/* A prepared finite ground relation view. Keys borrow immutable coordinates
 * from one TermUniverse generation; tuple records own their key vectors and
 * physical occurrence ordinals. Complete cardinality retains only variables
 * shared between factors; repeated private variables remain row constraints.
 * Preparation and execution require the same
 * serialized Space read contract as the ordinary native matcher. */
typedef struct CettaRelationalPlan CettaRelationalPlan;

typedef enum {
    CETTA_RELATIONAL_FINITE_EXACT_TERM_KEYS = 0,
} CettaRelationalEncoding;

typedef enum {
    CETTA_RELATIONAL_VARIABLE_INTERSECTION = 0,
} CettaRelationalAlgorithm;

typedef enum {
    CETTA_RELATIONAL_ACYCLIC_QUERY = 0,
    CETTA_RELATIONAL_CYCLIC_QUERY,
} CettaRelationalQueryClass;

typedef enum {
    CETTA_RELATIONAL_ADMIT_FULL_QUERY = 0,
    CETTA_RELATIONAL_ADMIT_CYCLIC_QUERY,
    /* Cyclic intersections, or counts with one factor covering every shared
     * coordinate, avoid enumerating factor-private Cartesian products. */
    CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT,
    /* Every query, keeping each variable, private ones included, so that a
     * leaf assigns them all: the plan enumerates whole solutions. */
    CETTA_RELATIONAL_ADMIT_ENUMERATION,
} CettaRelationalAdmission;

typedef struct {
    uint64_t space_instance;
    uint64_t space_revision;
    uint64_t universe_instance;
    uint64_t universe_epoch;
    CettaRelationalEncoding encoding;
    CettaRelationalAlgorithm algorithm;
    CettaRelationalQueryClass query_class;
    CettaObservationContract observation;
    size_t factor_count;
    size_t variable_count;
    size_t source_variable_count;
    /* SIZE_MAX when no factor contains every retained coordinate. A covering
     * factor bounds all surviving binding prefixes by its physical rows. */
    size_t covering_factor;
    /* Owned buffer payloads, including spare capacity. The preparation
     * allowance also covers overlapping reallocations and sorting buffers.
     * Borrowed space/universe storage and allocator metadata are measured
     * separately in whole-process qualification. */
    size_t retained_bytes;
    size_t preparation_peak_bytes;
    uint64_t input_occurrences;
    uint64_t scanned_rows;
} CettaRelationalPlanInfo;

typedef struct {
    uint64_t key_seeks;
    uint64_t key_comparisons;
    uint64_t candidate_prefixes;
    uint64_t leaf_assignments;
    size_t peak_bytes;
} CettaRelationalWork;

/* Flat variable positions and rigid finite trees are admitted. Float,
 * promoted-number, open subject and identity-bearing values decline: their
 * equality cannot be inferred from raw storage coordinates. Failure leaves
 * *out unchanged. The caller resolves dialect-specific binding carriers
 * before entering this representation. */
bool cetta_relational_plan_prepare(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, CettaRelationalAdmission admission,
    CettaRelationalPlan **out);
bool cetta_relational_plan_valid_for(
    const CettaRelationalPlan *plan, const Space *live_space);
const CettaRelationalPlanInfo *cetta_relational_plan_info(
    const CettaRelationalPlan *plan);
bool cetta_relational_plan_count(
    const CettaRelationalPlan *plan, const Space *live_space,
    uint64_t *count_out, CettaRelationalWork *work);
/* The plan's variables, in the order a solution's values follow. */
size_t cetta_relational_plan_variable_count(const CettaRelationalPlan *plan);
VarId cetta_relational_plan_variable_at(const CettaRelationalPlan *plan,
                                        size_t index);
/* Whether the patterns' hypergraph of shared variables is cyclic, by its
 * GYO reduction: an acyclic query joins well one pattern at a time.
 * False when a pattern is not an expression. */
bool cetta_relational_patterns_cyclic(Atom *const *patterns, size_t count,
                                      bool *cyclic);
/* The position of a pattern variable among the plan's variables, or
 * SIZE_MAX when the plan does not keep it. */
size_t cetta_relational_plan_variable_index(
    const CettaRelationalPlan *plan, VarId id);
/* One solution of an enumeration: VALUES assigns each plan variable, in the
 * plan's order, and WEIGHT counts the row combinations that give it,
 * duplicate rows included.  False stops the enumeration. */
typedef bool (*CettaRelationalVisit)(void *context, const AtomId *values,
                                     uint64_t weight);
/* Visit each solution of a plan prepared with ADMIT_ENUMERATION once, with
 * its weight; COUNT_OUT receives the sum of the weights.  The plan holds the
 * rows it was prepared from, so the visitor may change the space without
 * changing the solutions still to come.  False when the plan is stale at the
 * start, a weight overflows, or the visitor stops; the visitor may then have
 * seen some solutions. */
bool cetta_relational_plan_enumerate(
    const CettaRelationalPlan *plan, const Space *live_space,
    CettaRelationalVisit visit, void *context, uint64_t *count_out,
    CettaRelationalWork *work);
/* Retain requires an already owned live reference. It grants another owned
 * handle, not a stronger Space read contract; overflow leaves ownership
 * unchanged. Free releases one handle and needs no live Space or universe. */
bool cetta_relational_plan_retain(const CettaRelationalPlan *plan);
/* Free needs no live Space or universe. Preparation never publishes atoms,
 * rows, bindings or a user-visible prefix. */
void cetta_relational_plan_free(CettaRelationalPlan *plan);

#endif
