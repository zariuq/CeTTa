#include "prime_typed_flow_boundary.h"

#include "prime_regular_pattern.h"
#include "prime_typed_flow_private.h"
#include "prime_typing_authority.h"

enum {
    PRIME_TYPED_BOUNDARY_STAGING_MAX_DEPTH = 4096,
};

static bool prime_typed_boundary_head(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static Atom *prime_typed_boundary_splice_explicit_at_depth(
    Arena *owner, Atom *term, uint32_t depth) {
    if (!owner || !term || depth > PRIME_TYPED_BOUNDARY_STAGING_MAX_DEPTH)
        return NULL;
    if (term->kind != ATOM_EXPR)
        return atom_deep_copy(owner, term);

    if (prime_typed_boundary_head(term, "quote", 2u))
        return atom_deep_copy(owner, term);

    if (term->expr.len != 0u &&
        atom_is_symbol(term->expr.elems[0], "unquote")) {
        if (!prime_typed_boundary_head(term, "unquote", 2u) ||
            !prime_typed_boundary_head(
                term->expr.elems[1], "quote", 2u)) {
            return NULL;
        }
        return prime_typed_boundary_splice_explicit_at_depth(
            owner, term->expr.elems[1]->expr.elems[1], depth + 1u);
    }

    Atom **items = term->expr.len == 0u ? NULL :
        arena_alloc(owner, sizeof(*items) * term->expr.len);
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        items[index] = prime_typed_boundary_splice_explicit_at_depth(
            owner, term->expr.elems[index], depth + 1u);
        if (!items[index]) return NULL;
    }
    return atom_expr(owner, items, term->expr.len);
}

Atom *cetta_prime_typed_boundary_splice_explicit_v1(
    Arena *owner, Atom *term) {
    return prime_typed_boundary_splice_explicit_at_depth(owner, term, 0u);
}

static bool prime_typed_boundary_native_route(
    CettaPrimeTypingRouteV1 route) {
    return route == CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR ||
           route == CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR;
}

static Atom *prime_typed_boundary_synthesis_type(
    const CettaPrimeTypingSynthesisObservationV1 *observation) {
    if (!observation ||
        observation->authority.result.kind != CETTA_NIK_RESULT_OUTCOME ||
        observation->authority.result.value.outcome !=
            CETTA_NIK_OUTCOME_ESTABLISHED ||
        !prime_typed_boundary_native_route(
            observation->authority.route)) {
        return NULL;
    }
    Atom *payload = observation->authority.payload;
    if (!payload || payload->kind != ATOM_EXPR || payload->expr.len != 2u ||
        payload->expr.elems[0]->kind != ATOM_SYMBOL ||
        (!atom_is_symbol(payload->expr.elems[0], "PrimeRegularSynthesis") &&
         !atom_is_symbol(
             payload->expr.elems[0], "PrimeRegularDeclaredSynthesis"))) {
        return NULL;
    }
    return payload->expr.elems[1];
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_import_synthesis_v1(
    Arena *owner, Space *space,
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis) {
    if (!owner || !space || !space->native.universe || !synthesis)
        return NULL;

    CettaPrimeRegularKernelStatus status =
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    AtomId type_id = CETTA_ATOM_ID_NONE;
    if (!cetta_prime_regular_kernel_admitted_synthesis_v1_decision(
            synthesis, space,
            cetta_prime_regular_kernel_closed_synthesis_profile_v1,
            &status, &type_id, NULL) ||
        status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        return NULL;
    }

    CettaPrimeRegularKernelSynthesisMetadataV1 metadata;
    CettaNikDirectAuthorityTokenV1 token;
    if (!cetta_prime_regular_kernel_admitted_synthesis_v1_metadata(
            synthesis, &metadata) ||
        metadata.universe_instance_id !=
            space->native.universe->instance_id ||
        metadata.universe_storage_epoch !=
            space->native.universe->storage_epoch ||
        metadata.type_id != type_id ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token)) {
        return NULL;
    }

    AtomId witnesses[] = {metadata.term_id, metadata.type_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "typed:boundary",
        .construction = CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1,
        .witness_ids = witnesses,
        .witness_count = 2u,
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, space->native.universe, metadata.context_id,
        metadata.term_id, metadata.type_id, &token, &build);
}

bool cetta_prime_typed_value_import_term_v1(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingSynthesisObservationV1 *observation_out,
    CettaPrimeTypedValueV1 **value_out) {
    if (observation_out)
        *observation_out = (CettaPrimeTypingSynthesisObservationV1){0};
    if (value_out) *value_out = NULL;
    if (!owner || !space || !space->native.universe || !term ||
        !observation_out || !value_out ||
        (steps_limited && steps == 0u)) {
        return false;
    }

    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    CettaNikDirectAuthorityTokenV1 token_before;
    if (!cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token_before)) {
        return false;
    }

    CettaPrimeTypingSynthesisCandidateV1 candidate = {
        .term = term,
        .steps_limited = steps_limited,
        .steps = steps,
    };
    CettaPrimeTypingSynthesisObservationV1 observation;
    if (!cetta_prime_typing_observe_synthesis_v1(
            owner, space, &candidate, &observation)) {
        return false;
    }
    *observation_out = observation;

    CettaNikDirectAuthorityTokenV1 token_after;
    if (space->native.universe != universe ||
        universe->instance_id != instance_before ||
        universe->storage_epoch != epoch_before ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token_after) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &token_before, &token_after)) {
        return false;
    }

    Atom *type = prime_typed_boundary_synthesis_type(&observation);
    Atom *canonical_term = observation.authority.canonical_term;
    if (!type || !canonical_term) return true;
    AtomId context_id = term_universe_store_atom_id(
        universe, owner, atom_symbol(owner, "PrimeCtxNil"));
    AtomId source_term_id = term_universe_store_atom_id(
        universe, owner, term);
    AtomId term_id = term_universe_store_atom_id(
        universe, owner, canonical_term);
    AtomId type_id = term_universe_store_atom_id(universe, owner, type);
    if (context_id == CETTA_ATOM_ID_NONE || term_id == CETTA_ATOM_ID_NONE ||
        source_term_id == CETTA_ATOM_ID_NONE ||
        type_id == CETTA_ATOM_ID_NONE) {
        return false;
    }
    AtomId witnesses[] = {source_term_id, term_id, type_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "typed:boundary",
        .construction = CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1,
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    *value_out = cetta_prime_typed_value_allocate_private_v1(
        owner, universe, context_id, term_id, type_id, &token_after, &build);
    return *value_out != NULL;
}

bool cetta_prime_typed_value_import_checked_term_v1(
    Arena *owner, Space *space, Atom *term,
    const CettaPrimeTypedValueV1 *expected_type,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingCheckingObservationV1 *observation_out,
    CettaPrimeTypedValueV1 **value_out) {
    if (observation_out)
        *observation_out = (CettaPrimeTypingCheckingObservationV1){0};
    if (value_out) *value_out = NULL;
    if (!owner || !space || !space->native.universe || !term ||
        !expected_type || !observation_out || !value_out ||
        (steps_limited && steps == 0u) ||
        !cetta_prime_typed_value_v1_is_current(expected_type, space)) {
        return false;
    }

    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    Atom *expected_intrinsic = term_universe_copy_atom(
        universe, owner, expected_type->term_id);
    Atom *expected = expected_intrinsic
        ? cetta_prime_regular_term_quote_intrinsic_v1(
              owner, expected_intrinsic)
        : NULL;
    CettaNikDirectAuthorityTokenV1 token_before;
    if (!expected ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token_before) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &expected_type->authority_token, &token_before)) {
        return false;
    }

    CettaPrimeTypingCheckingCandidateV1 candidate = {
        .term = term,
        .expected_type = expected,
        .steps_limited = steps_limited,
        .steps = steps,
    };
    CettaPrimeTypingCheckingObservationV1 observation;
    if (!cetta_prime_typing_observe_checking_v1(
            owner, space, &candidate, &observation)) {
        return false;
    }
    *observation_out = observation;

    CettaNikDirectAuthorityTokenV1 token_after;
    if (space->native.universe != universe ||
        universe->instance_id != instance_before ||
        universe->storage_epoch != epoch_before ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token_after) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &token_before, &token_after)) {
        return false;
    }
    if (observation.authority.result.kind != CETTA_NIK_RESULT_OUTCOME ||
        observation.authority.result.value.outcome !=
            CETTA_NIK_OUTCOME_ESTABLISHED ||
        !prime_typed_boundary_native_route(
            observation.authority.route)) {
        return true;
    }

    Atom *canonical_term = observation.authority.canonical_term;
    if (!canonical_term) return true;
    AtomId context_id = term_universe_store_atom_id(
        universe, owner, atom_symbol(owner, "PrimeCtxNil"));
    AtomId source_term_id = term_universe_store_atom_id(
        universe, owner, term);
    AtomId term_id = term_universe_store_atom_id(
        universe, owner, canonical_term);
    AtomId payload_id = term_universe_store_atom_id(
        universe, owner, observation.authority.payload);
    if (context_id == CETTA_ATOM_ID_NONE ||
        source_term_id == CETTA_ATOM_ID_NONE ||
        term_id == CETTA_ATOM_ID_NONE ||
        payload_id == CETTA_ATOM_ID_NONE) {
        return false;
    }

    const CettaPrimeTypedValueV1 *premises[] = {expected_type};
    AtomId witnesses[] = {
        source_term_id, term_id, expected_type->term_id, payload_id,
    };
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "typed:boundary-check",
        .construction = CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    *value_out = cetta_prime_typed_value_allocate_private_v1(
        owner, universe, context_id, term_id, expected_type->term_id,
        &token_after, &build);
    return *value_out != NULL;
}
