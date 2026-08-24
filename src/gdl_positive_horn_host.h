#ifndef CETTA_GDL_POSITIVE_HORN_HOST_H
#define CETTA_GDL_POSITIVE_HORN_HOST_H

#include "gdl_positive_horn_native.h"
#include "space.h"

/* Raw-boundary adapters for hosting one positive-Horn GDL calculus and its
 * finite typed episodes as ordinary Space data.  The parser remains
 * authority-free; these objects retain only exact Space currentness and NIK
 * realization identities around the language-owned native constructions. */
typedef struct CettaGdlPositiveHornHostV1 CettaGdlPositiveHornHostV1;
typedef struct CettaGdlPositiveHornHostedEpisodeV1
    CettaGdlPositiveHornHostedEpisodeV1;

typedef enum {
    CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1 = 1,
    CETTA_GDL_POSITIVE_HORN_HOST_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_POSITIVE_HORN_HOST_INCOMPLETE_V1,
    CETTA_GDL_POSITIVE_HORN_HOST_STALE_V1,
    CETTA_GDL_POSITIVE_HORN_HOST_ENGINE_FAULT_V1,
} CettaGdlPositiveHornHostAdmissionKindV1;

typedef struct {
    CettaGdlPositiveHornHostAdmissionKindV1 kind;
    CettaGdlPositiveHornHostV1 *host;
} CettaGdlPositiveHornHostAdmissionV1;

CettaGdlPositiveHornHostAdmissionV1
cetta_gdl_positive_horn_host_admit_v1(
    Space *source_space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlPositiveHornLimitsV1 limits);

CettaGdlPositiveHornHostAdmissionV1
cetta_gdl_finite_view_host_admit_v1(
    Space *source_space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlPositiveHornLimitsV1 limits);

void cetta_gdl_positive_horn_host_destroy_v1(
    CettaGdlPositiveHornHostV1 *host);

bool cetta_gdl_positive_horn_host_is_current_v1(
    const CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space);

typedef struct {
    SpaceReadToken source_read;
    CettaIndex package_occurrence;
    AtomId package_atom_id;
    bool has_package_atom_id;
    CettaNikDirectAuthorityTokenV1 authority;
} CettaGdlPositiveHornHostReceiptV1;

bool cetta_gdl_positive_horn_host_receipt_v1(
    const CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    CettaGdlPositiveHornHostReceiptV1 *receipt_out);

typedef struct {
    CettaGdlPositiveHornEpisodeAdmissionKindV1 kind;
    CettaGdlPositiveHornHostedEpisodeV1 *episode;
} CettaGdlPositiveHornHostedEpisodeAdmissionV1;

/* Read one finite fact occurrence bag from a live Space and construct its
 * typed episode transactionally.  Logical occurrence order and duplicate
 * occurrences are retained.  The source host and both Spaces must outlive
 * the returned hosted episode. */
CettaGdlPositiveHornHostedEpisodeAdmissionV1
cetta_gdl_positive_horn_host_admit_episode_v1(
    CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    Space *episode_space,
    Atom *episode_identity,
    const CettaIndex *fact_occurrences,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeLimitsV1 limits);

/* Construct absence evidence only from the entire current episode Space.
 * The finite-view path intentionally has no selective-occurrence variant. */
CettaGdlPositiveHornHostedEpisodeAdmissionV1
cetta_gdl_finite_view_host_admit_complete_episode_v1(
    CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    Space *episode_space,
    Atom *episode_identity,
    CettaGdlPositiveHornEpisodeLimitsV1 limits);

void cetta_gdl_positive_horn_hosted_episode_destroy_v1(
    CettaGdlPositiveHornHostedEpisodeV1 *episode);

bool cetta_gdl_positive_horn_hosted_episode_is_current_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space);

typedef struct {
    CettaGdlPositiveHornHostReceiptV1 source;
    SpaceReadToken episode_read;
    size_t fact_count;
    CettaNikDirectAuthorityTokenV1 authority;
} CettaGdlPositiveHornHostedEpisodeReceiptV1;

bool cetta_gdl_positive_horn_hosted_episode_receipt_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space,
    CettaGdlPositiveHornHostedEpisodeReceiptV1 *receipt_out);

CettaGdlPositiveHornRunV1
cetta_gdl_positive_horn_hosted_episode_run_v1(
    CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences);

bool cetta_gdl_positive_horn_hosted_episode_stats_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    CettaGdlPositiveHornEpisodeStatsV1 *stats_out);

#endif /* CETTA_GDL_POSITIVE_HORN_HOST_H */
