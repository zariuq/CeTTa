#ifndef CETTA_GSLT_FINITE_FACT_PROVIDER_V1_H
#define CETTA_GSLT_FINITE_FACT_PROVIDER_V1_H

#include "gslt_provider_runtime.h"

#include <stddef.h>
#include <stdint.h>

typedef struct CettaGsltFiniteFactProviderSetV1
    CettaGsltFiniteFactProviderSetV1;

typedef struct {
    Atom *const *rows;
    size_t row_count;
} CettaGsltFiniteFactSpanV1;

typedef struct {
    const CettaGsltProviderRequirementV1 *requirement;
    Atom *const *rows;
    size_t row_count;
} CettaGsltFiniteFactRelationViewV1;

typedef struct {
    uint64_t queries;
    uint64_t indexed_queries;
    uint64_t rows_considered;
    uint64_t rows_skipped;
    size_t indexed_relations;
} CettaGsltFiniteFactProviderStatsV1;

/*
 * Build one physical provider registry over a finite bag of immutable ground
 * rows.  The requirement inventory supplies every relation, arity, and
 * semantic identity; the implementation contains no fixed relation list.
 *
 * Rows and requirement strings are borrowed and must outlive the provider
 * set.  Row occurrences are preserved, including duplicates.  A row outside
 * the declared inventory, an open row, or a duplicate dispatch/semantic
 * identity rejects the whole construction transactionally.
 */
CettaGsltFiniteFactProviderSetV1 *
cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
    const CettaGsltProviderRequirementV1 *requirements,
    size_t requirement_count,
    const CettaGsltFiniteFactSpanV1 *spans,
    size_t span_count,
    char *error,
    size_t error_size);

void cetta_gslt_finite_fact_provider_set_free_v1(
    CettaGsltFiniteFactProviderSetV1 *set);

const CettaGsltProviderRegistryV1 *
cetta_gslt_finite_fact_provider_set_registry_v1(
    const CettaGsltFiniteFactProviderSetV1 *set);

size_t cetta_gslt_finite_fact_provider_set_row_count_v1(
    const CettaGsltFiniteFactProviderSetV1 *set);

/* Borrow a relation inventory view from the validated provider set.  The
 * requirement and rows remain owned by their original inputs and the set;
 * the view is valid only while both remain alive. */
size_t cetta_gslt_finite_fact_provider_set_relation_count_v1(
    const CettaGsltFiniteFactProviderSetV1 *set);

bool cetta_gslt_finite_fact_provider_set_relation_view_v1(
    const CettaGsltFiniteFactProviderSetV1 *set,
    size_t relation_index,
    CettaGsltFiniteFactRelationViewV1 *view);

/* Read cumulative, thread-safe discrimination-index counters.  Counters are
 * observational only; overflow saturates and never affects provider meaning. */
void cetta_gslt_finite_fact_provider_set_stats_v1(
    const CettaGsltFiniteFactProviderSetV1 *set,
    CettaGsltFiniteFactProviderStatsV1 *stats);

#endif /* CETTA_GSLT_FINITE_FACT_PROVIDER_V1_H */
