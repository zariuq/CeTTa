#include "gdl_positive_horn_host.h"

#include "symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaGdlPositiveHornHostV1 {
    Space *source_space;
    SpaceReadToken source_read;
    CettaIndex package_occurrence;
    AtomId package_atom_id;
    bool has_package_atom_id;
    CettaGdlPositiveHornNativeV1 *native;
    CettaNikDirectAuthorityTokenV1 token;
};

struct CettaGdlPositiveHornHostedEpisodeV1 {
    CettaGdlPositiveHornHostV1 *host;
    Space *episode_space;
    SpaceReadToken episode_read;
    size_t fact_count;
    CettaGdlPositiveHornEpisodeV1 *native;
    CettaNikDirectAuthorityTokenV1 token;
};

static Atom *gdl_positive_horn_host_expr_v1(
    Arena *arena, const char *head, Atom *const *arguments,
    size_t argument_count) {
    if (!arena || !head || !*head ||
        argument_count > (size_t)UINT32_MAX - 1u)
        return NULL;
    Atom **items = arena_alloc(
        arena, (argument_count + 1u) * sizeof(*items));
    items[0] = atom_symbol(arena, head);
    for (size_t index = 0u; index < argument_count; index++)
        items[index + 1u] = arguments[index];
    return atom_expr(arena, items, (CettaExprLen)(argument_count + 1u));
}

static Atom *gdl_positive_horn_host_u64_v1(
    Arena *arena, uint64_t value) {
    char text[17];
    int length = snprintf(text, sizeof(text), "%016" PRIx64, value);
    return length == 16 ? atom_string(arena, text) : NULL;
}

static CettaGdlPositiveHornHostAdmissionV1
gdl_positive_horn_host_admission_v1(
    CettaGdlPositiveHornHostAdmissionKindV1 kind,
    CettaGdlPositiveHornHostV1 *host) {
    return (CettaGdlPositiveHornHostAdmissionV1){
        .kind = kind,
        .host = host,
    };
}

static CettaGdlPositiveHornHostAdmissionKindV1
gdl_positive_horn_host_native_kind_v1(
    CettaGdlPositiveHornAdmissionKindV1 kind) {
    switch (kind) {
    case CETTA_GDL_POSITIVE_HORN_ADMITTED_V1:
        return CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1;
    case CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1:
        return CETTA_GDL_POSITIVE_HORN_HOST_OUTSIDE_FRAGMENT_V1;
    case CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1:
        return CETTA_GDL_POSITIVE_HORN_HOST_INCOMPLETE_V1;
    case CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1:
    default:
        return CETTA_GDL_POSITIVE_HORN_HOST_ENGINE_FAULT_V1;
    }
}

static CettaGdlPositiveHornHostAdmissionV1
gdl_positive_horn_host_admit_mode_v1(
    Space *source_space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlPositiveHornLimitsV1 limits,
    bool finite_view) {
    if (!source_space || !expected_revision || !*expected_revision ||
        space_instance_id(source_space) == 0u)
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_ENGINE_FAULT_V1, NULL);
    SpaceReadToken read = space_read_token(source_space);
    if (!space_read_token_matches_live_space(read, source_space))
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_STALE_V1, NULL);
    if (package_occurrence >= space_length64(source_space))
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_OUTSIDE_FRAGMENT_V1, NULL);

    Atom *package = space_get_at64(source_space, package_occurrence);
    AtomId package_atom_id = space_get_atom_id_at64(
        source_space, package_occurrence);
    CettaGdlPositiveHornAdmissionV1 native_admission = finite_view
        ? cetta_gdl_finite_view_native_admit_v1(package, limits)
        : cetta_gdl_positive_horn_native_admit_v1(package, limits);
    if (native_admission.kind != CETTA_GDL_POSITIVE_HORN_ADMITTED_V1 ||
        !native_admission.native) {
        CettaGdlPositiveHornHostAdmissionKindV1 kind =
            native_admission.kind == CETTA_GDL_POSITIVE_HORN_ADMITTED_V1
                ? CETTA_GDL_POSITIVE_HORN_HOST_ENGINE_FAULT_V1
                : gdl_positive_horn_host_native_kind_v1(
                    native_admission.kind);
        cetta_gdl_positive_horn_native_destroy_v1(
            native_admission.native);
        return gdl_positive_horn_host_admission_v1(kind, NULL);
    }

    const char *source_sha256 = NULL;
    const char *profile_sha256 = NULL;
    const char *revision = NULL;
    CettaNikDirectAuthorityTokenV1 token = {0};
    bool identified = cetta_gdl_positive_horn_native_identity_v1(
            native_admission.native, &source_sha256,
            &profile_sha256, &revision) &&
        source_sha256 && profile_sha256 && revision &&
        strcmp(revision, expected_revision) == 0 &&
        cetta_gdl_positive_horn_native_token_v1(
            native_admission.native, &token);
    AtomId live_package_atom_id =
        package_occurrence < space_length64(source_space)
            ? space_get_atom_id_at64(source_space, package_occurrence)
            : CETTA_ATOM_ID_NONE;
    Atom *live_package = package_occurrence < space_length64(source_space)
        ? space_get_at64(source_space, package_occurrence)
        : NULL;
    bool same_package = package_atom_id != CETTA_ATOM_ID_NONE
        ? live_package_atom_id == package_atom_id
        : live_package && atom_eq(live_package, package);
    if (!identified) {
        cetta_gdl_positive_horn_native_destroy_v1(
            native_admission.native);
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_OUTSIDE_FRAGMENT_V1, NULL);
    }
    if (!space_read_token_matches_live_space(read, source_space) ||
        !same_package) {
        cetta_gdl_positive_horn_native_destroy_v1(
            native_admission.native);
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_STALE_V1, NULL);
    }

    CettaGdlPositiveHornHostV1 *host = malloc(sizeof(*host));
    if (!host) {
        cetta_gdl_positive_horn_native_destroy_v1(
            native_admission.native);
        return gdl_positive_horn_host_admission_v1(
            CETTA_GDL_POSITIVE_HORN_HOST_ENGINE_FAULT_V1, NULL);
    }
    *host = (CettaGdlPositiveHornHostV1){
        .source_space = source_space,
        .source_read = read,
        .package_occurrence = package_occurrence,
        .package_atom_id = package_atom_id,
        .has_package_atom_id = package_atom_id != CETTA_ATOM_ID_NONE,
        .native = native_admission.native,
        .token = token,
    };
    return gdl_positive_horn_host_admission_v1(
        CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1, host);
}

CettaGdlPositiveHornHostAdmissionV1
cetta_gdl_positive_horn_host_admit_v1(
    Space *source_space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlPositiveHornLimitsV1 limits) {
    return gdl_positive_horn_host_admit_mode_v1(
        source_space, package_occurrence, expected_revision,
        limits, false);
}

CettaGdlPositiveHornHostAdmissionV1
cetta_gdl_finite_view_host_admit_v1(
    Space *source_space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlPositiveHornLimitsV1 limits) {
    return gdl_positive_horn_host_admit_mode_v1(
        source_space, package_occurrence, expected_revision,
        limits, true);
}

void cetta_gdl_positive_horn_host_destroy_v1(
    CettaGdlPositiveHornHostV1 *host) {
    if (!host)
        return;
    cetta_gdl_positive_horn_native_destroy_v1(host->native);
    free(host);
}

bool cetta_gdl_positive_horn_host_is_current_v1(
    const CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space) {
    if (!host || !live_source_space ||
        host->source_space != live_source_space ||
        !space_read_token_matches_live_space(
            host->source_read, live_source_space) ||
        host->package_occurrence >= space_length64(live_source_space))
        return false;
    if (host->has_package_atom_id &&
        space_get_atom_id_at64(
            live_source_space, host->package_occurrence) !=
                host->package_atom_id)
        return false;
    return cetta_gdl_positive_horn_native_token_is_current_v1(
        host->native, &host->token);
}

bool cetta_gdl_positive_horn_host_receipt_v1(
    const CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    CettaGdlPositiveHornHostReceiptV1 *receipt_out) {
    if (!receipt_out ||
        !cetta_gdl_positive_horn_host_is_current_v1(
            host, live_source_space))
        return false;
    *receipt_out = (CettaGdlPositiveHornHostReceiptV1){
        .source_read = host->source_read,
        .package_occurrence = host->package_occurrence,
        .package_atom_id = host->package_atom_id,
        .has_package_atom_id = host->has_package_atom_id,
        .authority = host->token,
    };
    return true;
}

static CettaGdlPositiveHornHostedEpisodeAdmissionV1
gdl_positive_horn_hosted_episode_admission_v1(
    CettaGdlPositiveHornEpisodeAdmissionKindV1 kind,
    CettaGdlPositiveHornHostedEpisodeV1 *episode) {
    return (CettaGdlPositiveHornHostedEpisodeAdmissionV1){
        .kind = kind,
        .episode = episode,
    };
}

static CettaGdlPositiveHornHostedEpisodeAdmissionV1
gdl_positive_horn_hosted_episode_wrap_v1(
    CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    Space *episode_space,
    SpaceReadToken episode_read,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeAdmissionV1 admitted) {
    if (admitted.kind != CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 ||
        !admitted.episode) {
        CettaGdlPositiveHornEpisodeAdmissionKindV1 kind =
            admitted.kind == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1
                ? CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1
                : admitted.kind;
        cetta_gdl_positive_horn_episode_destroy_v1(admitted.episode);
        return gdl_positive_horn_hosted_episode_admission_v1(kind, NULL);
    }
    if (!cetta_gdl_positive_horn_host_is_current_v1(
            host, live_source_space) ||
        !space_read_token_matches_live_space(
            episode_read, episode_space)) {
        cetta_gdl_positive_horn_episode_destroy_v1(admitted.episode);
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    }

    CettaGdlPositiveHornHostedEpisodeV1 *episode =
        malloc(sizeof(*episode));
    if (!episode) {
        cetta_gdl_positive_horn_episode_destroy_v1(admitted.episode);
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    }
    CettaNikDirectAuthorityTokenV1 token = {0};
    if (!cetta_gdl_positive_horn_episode_token_v1(
            admitted.episode, &token)) {
        free(episode);
        cetta_gdl_positive_horn_episode_destroy_v1(admitted.episode);
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    }
    *episode = (CettaGdlPositiveHornHostedEpisodeV1){
        .host = host,
        .episode_space = episode_space,
        .episode_read = episode_read,
        .fact_count = fact_count,
        .native = admitted.episode,
        .token = token,
    };
    return gdl_positive_horn_hosted_episode_admission_v1(
        CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1, episode);
}

CettaGdlPositiveHornHostedEpisodeAdmissionV1
cetta_gdl_positive_horn_host_admit_episode_v1(
    CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    Space *episode_space,
    Atom *episode_identity,
    const CettaIndex *fact_occurrences,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeLimitsV1 limits) {
    if (!host || !live_source_space || !episode_space ||
        !episode_identity || (fact_count != 0u && !fact_occurrences) ||
        space_instance_id(episode_space) == 0u)
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    if (!cetta_gdl_positive_horn_host_is_current_v1(
            host, live_source_space))
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    if (fact_count > (size_t)UINT32_MAX - 1u)
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1, NULL);

    SpaceReadToken episode_read = space_read_token(episode_space);
    if (!space_read_token_matches_live_space(
            episode_read, episode_space))
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    Atom **facts = fact_count
        ? malloc(fact_count * sizeof(*facts)) : NULL;
    if (fact_count && !facts)
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);

    Arena identity_arena;
    arena_init(&identity_arena);
    arena_set_runtime_kind(
        &identity_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    Atom **occurrence_receipts = fact_count
        ? arena_alloc(
            &identity_arena, fact_count * sizeof(*occurrence_receipts))
        : NULL;
    CettaGdlPositiveHornEpisodeAdmissionKindV1 status =
        CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1;
    for (size_t index = 0u; index < fact_count; index++) {
        if (fact_occurrences[index] >= space_length64(episode_space)) {
            status =
                CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1;
            break;
        }
        facts[index] = space_get_at64(
            episode_space, fact_occurrences[index]);
        AtomId atom_id = space_get_atom_id_at64(
            episode_space, fact_occurrences[index]);
        Atom *receipt_arguments[] = {
            gdl_positive_horn_host_u64_v1(
                &identity_arena, fact_occurrences[index]),
            atom_id == CETTA_ATOM_ID_NONE
                ? atom_symbol(&identity_arena, "no-atom-id")
                : gdl_positive_horn_host_u64_v1(
                    &identity_arena, atom_id),
        };
        occurrence_receipts[index] = receipt_arguments[0] &&
                receipt_arguments[1]
            ? gdl_positive_horn_host_expr_v1(
                &identity_arena, "gdl:space-fact-occurrence-v1",
                receipt_arguments, 2u)
            : NULL;
        if (!facts[index] || !occurrence_receipts[index]) {
            status = CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
            break;
        }
    }
    Atom *occurrence_bag = status ==
            CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1
        ? gdl_positive_horn_host_expr_v1(
            &identity_arena, "gdl:space-fact-occurrences-v1",
            occurrence_receipts, fact_count)
        : NULL;
    Atom *read_arguments[] = {
        gdl_positive_horn_host_u64_v1(
            &identity_arena, episode_read.instance_id),
        gdl_positive_horn_host_u64_v1(
            &identity_arena, episode_read.revision),
    };
    Atom *read = read_arguments[0] && read_arguments[1]
        ? gdl_positive_horn_host_expr_v1(
            &identity_arena, "gdl:space-read-v1",
            read_arguments, 2u)
        : NULL;
    Atom *identity_arguments[] = {
        atom_deep_copy(&identity_arena, episode_identity),
        read,
        occurrence_bag,
    };
    Atom *identity = status ==
                CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
            identity_arguments[0] && identity_arguments[1] &&
            identity_arguments[2]
        ? gdl_positive_horn_host_expr_v1(
            &identity_arena, "gdl:space-episode-v1",
            identity_arguments, 3u)
        : NULL;
    if (status == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
        !identity)
        status = CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;

    CettaGdlPositiveHornEpisodeAdmissionV1 admitted = {0};
    if (status == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1)
        admitted = cetta_gdl_positive_horn_native_admit_episode_v1(
            host->native, &host->token, identity,
            facts, fact_count, limits);
    else
        admitted.kind = status;
    free(facts);
    arena_free(&identity_arena);
    return gdl_positive_horn_hosted_episode_wrap_v1(
        host, live_source_space, episode_space,
        episode_read, fact_count, admitted);
}

CettaGdlPositiveHornHostedEpisodeAdmissionV1
cetta_gdl_finite_view_host_admit_complete_episode_v1(
    CettaGdlPositiveHornHostV1 *host,
    const Space *live_source_space,
    Space *episode_space,
    Atom *episode_identity,
    CettaGdlPositiveHornEpisodeLimitsV1 limits) {
    if (!host || !live_source_space || !episode_space ||
        !episode_identity || space_instance_id(episode_space) == 0u)
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    if (!cetta_gdl_positive_horn_host_is_current_v1(
            host, live_source_space))
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    SpaceReadToken episode_read = space_read_token(episode_space);
    if (!space_read_token_matches_live_space(
            episode_read, episode_space))
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    CettaCount fact_count64 = space_length64(episode_space);
    size_t fact_count = (size_t)fact_count64;
    if ((CettaCount)fact_count != fact_count64)
        return gdl_positive_horn_hosted_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1, NULL);

    CettaGdlPositiveHornEpisodeAdmissionV1 admitted =
        cetta_gdl_finite_view_native_admit_space_episode_v1(
            host->native, &host->token, episode_space,
            episode_identity, limits);
    return gdl_positive_horn_hosted_episode_wrap_v1(
        host, live_source_space, episode_space,
        episode_read, fact_count, admitted);
}

void cetta_gdl_positive_horn_hosted_episode_destroy_v1(
    CettaGdlPositiveHornHostedEpisodeV1 *episode) {
    if (!episode)
        return;
    cetta_gdl_positive_horn_episode_destroy_v1(episode->native);
    free(episode);
}

bool cetta_gdl_positive_horn_hosted_episode_is_current_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space) {
    return episode && episode->host && live_source_space &&
        live_episode_space &&
        episode->episode_space == live_episode_space &&
        cetta_gdl_positive_horn_host_is_current_v1(
            episode->host, live_source_space) &&
        space_read_token_matches_live_space(
            episode->episode_read, live_episode_space) &&
        cetta_gdl_positive_horn_episode_token_is_current_v1(
            episode->native, &episode->token);
}

bool cetta_gdl_positive_horn_hosted_episode_receipt_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space,
    CettaGdlPositiveHornHostedEpisodeReceiptV1 *receipt_out) {
    if (!receipt_out ||
        !cetta_gdl_positive_horn_hosted_episode_is_current_v1(
            episode, live_source_space, live_episode_space) ||
        !cetta_gdl_positive_horn_host_receipt_v1(
            episode->host, live_source_space, &receipt_out->source))
        return false;
    receipt_out->episode_read = episode->episode_read;
    receipt_out->fact_count = episode->fact_count;
    receipt_out->authority = episode->token;
    return true;
}

CettaGdlPositiveHornRunV1
cetta_gdl_positive_horn_hosted_episode_run_v1(
    CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *live_source_space,
    const Space *live_episode_space,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences) {
    if (!episode || !live_source_space || !live_episode_space ||
        !result_arena || !query)
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
        };
    if (!cetta_gdl_positive_horn_hosted_episode_is_current_v1(
            episode, live_source_space, live_episode_space))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1,
        };
    CettaGdlPositiveHornRunV1 result =
        cetta_gdl_positive_horn_episode_run_v1(
            episode->native, &episode->token, result_arena,
            query, depth, max_states, max_occurrences);
    if (!cetta_gdl_positive_horn_hosted_episode_is_current_v1(
            episode, live_source_space, live_episode_space))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1,
        };
    return result;
}

bool cetta_gdl_positive_horn_hosted_episode_stats_v1(
    const CettaGdlPositiveHornHostedEpisodeV1 *episode,
    CettaGdlPositiveHornEpisodeStatsV1 *stats_out) {
    return episode && cetta_gdl_positive_horn_episode_stats_v1(
        episode->native, stats_out);
}
