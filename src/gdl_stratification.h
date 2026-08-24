#ifndef CETTA_GDL_STRATIFICATION_H
#define CETTA_GDL_STRATIFICATION_H

#include "gdl_source_presentation.h"

#include <stdbool.h>
#include <stddef.h>

/* Language-owned evidence that an authored GDL dependency graph is
 * stratified.  Predicate identity is the authored top-level name and arity;
 * nested term constructors are never flattened into it.  This is semantic
 * presentation data, not a parser-side authority or an execution mode. */
typedef struct CettaGdlStratificationV1 CettaGdlStratificationV1;

typedef struct {
    size_t max_relations;
    size_t max_edges;
    size_t max_logical_depth;
} CettaGdlStratificationLimitsV1;

typedef enum {
    CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 = 1,
    CETTA_GDL_STRATIFICATION_REFUTED_NEGATIVE_CYCLE_V1,
    CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_STRATIFICATION_INCOMPLETE_V1,
    CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1,
} CettaGdlStratificationKindV1;

typedef struct {
    CettaGdlStratificationKindV1 kind;
    /* Present for Established and Refuted.  A refuted analysis retains the
     * exact negative-cycle edge occurrence witness. */
    CettaGdlStratificationV1 *analysis;
} CettaGdlStratificationResultV1;

typedef struct {
    const char *name;
    size_t arity;
    size_t stratum;
    bool defined;
} CettaGdlStratifiedRelationViewV1;

typedef struct {
    size_t source_form_ordinal;
    size_t source_start_line;
    size_t source_end_line;
    const size_t *path;
    size_t path_length;
    size_t head_relation;
    size_t body_relation;
    bool negative;
} CettaGdlDependencyEdgeViewV1;

/* Construct the least assignment satisfying head >= body for positive edges
 * and head > body for negative edges.  An update after |relations| passes
 * yields a checked negative-cycle obstruction. */
CettaGdlStratificationResultV1 cetta_gdl_stratification_construct_v1(
    const GdlSourceRawFormsV1 *forms,
    CettaGdlStratificationLimitsV1 limits);

void cetta_gdl_stratification_destroy_v1(
    CettaGdlStratificationV1 *analysis);

size_t cetta_gdl_stratification_relation_count_v1(
    const CettaGdlStratificationV1 *analysis);

size_t cetta_gdl_stratification_edge_count_v1(
    const CettaGdlStratificationV1 *analysis);

size_t cetta_gdl_stratification_maximum_stratum_v1(
    const CettaGdlStratificationV1 *analysis);

bool cetta_gdl_stratification_relation_view_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t index,
    CettaGdlStratifiedRelationViewV1 *view_out);

bool cetta_gdl_stratification_edge_view_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t index,
    CettaGdlDependencyEdgeViewV1 *view_out);

size_t cetta_gdl_stratification_negative_cycle_length_v1(
    const CettaGdlStratificationV1 *analysis);

bool cetta_gdl_stratification_negative_cycle_edge_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t cycle_index,
    CettaGdlDependencyEdgeViewV1 *view_out);

#endif /* CETTA_GDL_STRATIFICATION_H */
