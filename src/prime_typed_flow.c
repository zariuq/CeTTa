#include "prime_typed_flow.h"

#include "abt.h"
#include "prime_regular_kernel.h"
#include "prime_typed_flow_private.h"
#include "prime_typing_authority.h"
#include "term_universe.h"

#include <stdatomic.h>
#include <stdlib.h>

static _Atomic uint64_t g_prime_typed_next_occurrence = 1u;

static uint64_t prime_typed_fresh_occurrence(void) {
    uint64_t occurrence = atomic_fetch_add_explicit(
        &g_prime_typed_next_occurrence, 1u, memory_order_relaxed);
    if (occurrence == 0u || occurrence == UINT64_MAX) {
        fputs("CeTTa: exhausted Prime typed occurrence identities\n", stderr);
        abort();
    }
    return occurrence;
}

static bool size_add_ok(size_t left, size_t right, size_t *sum_out) {
    if (!sum_out || left > SIZE_MAX - right) return false;
    *sum_out = left + right;
    return true;
}

bool cetta_prime_typed_application_spine_private_v1(
    Atom *term, const char *head_name,
    Atom **arguments, size_t argument_count) {
    if (!term || !head_name || head_name[0] == '\0' || !arguments ||
        argument_count == 0u) {
        return false;
    }
    Atom *cursor = term;
    for (size_t offset = argument_count; offset > 0u; offset--) {
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !atom_is_symbol(cursor->expr.elems[0], "App")) {
            return false;
        }
        arguments[offset - 1u] = cursor->expr.elems[2];
        cursor = cursor->expr.elems[1];
    }
    return atom_is_symbol(cursor, head_name);
}

Atom *cetta_prime_typed_application_term_private_v1(
    Arena *owner, Atom *function,
    Atom *const *arguments, size_t argument_count) {
    if (!owner || !function || !arguments || argument_count == 0u)
        return NULL;
    Atom *result = function;
    for (size_t index = 0u; result && index < argument_count; index++) {
        if (!arguments[index]) return NULL;
        result = atom_expr3(
            owner, atom_symbol(owner, "App"), result, arguments[index]);
    }
    return result;
}

static bool derivation_contains_occurrence(
    const CettaPrimeTypedDerivationNodeV1 *nodes, size_t count,
    uint64_t occurrence) {
    for (size_t index = 0u; index < count; index++)
        if (nodes[index].occurrence_identity == occurrence) return true;
    return false;
}

bool cetta_prime_typed_values_cohere_private_v1(
    const Space *space,
    const CettaPrimeTypedValueV1 *const *values,
    size_t value_count) {
    if (!space || !values || value_count == 0u || !values[0] ||
        !cetta_prime_typed_value_v1_is_current(values[0], space)) {
        return false;
    }
    const CettaPrimeTypedValueV1 *first = values[0];
    for (size_t index = 1u; index < value_count; index++) {
        const CettaPrimeTypedValueV1 *value = values[index];
        if (!value || !cetta_prime_typed_value_v1_is_current(value, space) ||
            value->universe != first->universe ||
            value->universe_instance_id != first->universe_instance_id ||
            value->universe_storage_epoch != first->universe_storage_epoch ||
            value->context_id != first->context_id ||
            !cetta_nik_direct_authority_token_v1_equal(
                &value->authority_token, &first->authority_token)) {
            return false;
        }
    }
    return true;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_retain_private_v1(
    Arena *owner, const Space *space,
    const CettaPrimeTypedValueV1 *value) {
    if (!owner || !value ||
        !cetta_prime_typed_value_v1_is_current(value, space) ||
        value->derivation_node_count == 0u ||
        !value->derivation_nodes ||
        (value->premise_occurrence_count != 0u &&
         !value->premise_occurrences) ||
        (value->witness_count != 0u && !value->witness_ids) ||
        (value->parameter_count != 0u && !value->parameter_ids) ||
        (value->index_count != 0u && !value->index_ids) ||
        value->derivation_node_count >
            SIZE_MAX / sizeof(CettaPrimeTypedDerivationNodeV1) ||
        value->premise_occurrence_count > SIZE_MAX / sizeof(uint64_t) ||
        value->witness_count > SIZE_MAX / sizeof(AtomId) ||
        value->parameter_count > SIZE_MAX / sizeof(AtomId) ||
        value->index_count > SIZE_MAX / sizeof(AtomId)) {
        return NULL;
    }
    for (size_t index = 0u;
         index < value->derivation_node_count; index++) {
        const CettaPrimeTypedDerivationNodeV1 *node =
            &value->derivation_nodes[index];
        if (node->premise_offset > value->premise_occurrence_count ||
            node->premise_count >
                value->premise_occurrence_count - node->premise_offset ||
            node->witness_offset > value->witness_count ||
            node->witness_count >
                value->witness_count - node->witness_offset) {
            return NULL;
        }
    }

    CettaPrimeTypedDerivationNodeV1 *nodes = arena_alloc(
        owner, value->derivation_node_count * sizeof(*nodes));
    uint64_t *premises = value->premise_occurrence_count == 0u ? NULL :
        arena_alloc(
            owner,
            value->premise_occurrence_count * sizeof(*premises));
    AtomId *witnesses = value->witness_count == 0u ? NULL :
        arena_alloc(owner, value->witness_count * sizeof(*witnesses));
    AtomId *parameters = value->parameter_count == 0u ? NULL :
        arena_alloc(owner, value->parameter_count * sizeof(*parameters));
    AtomId *indices = value->index_count == 0u ? NULL :
        arena_alloc(owner, value->index_count * sizeof(*indices));
    for (size_t index = 0u;
         index < value->derivation_node_count; index++)
        nodes[index] = value->derivation_nodes[index];
    for (size_t index = 0u;
         index < value->premise_occurrence_count; index++)
        premises[index] = value->premise_occurrences[index];
    for (size_t index = 0u; index < value->witness_count; index++)
        witnesses[index] = value->witness_ids[index];
    for (size_t index = 0u; index < value->parameter_count; index++)
        parameters[index] = value->parameter_ids[index];
    for (size_t index = 0u; index < value->index_count; index++)
        indices[index] = value->index_ids[index];

    CettaPrimeTypedValueV1 *retained = arena_alloc(
        owner, sizeof(*retained));
    *retained = *value;
    retained->derivation_nodes = nodes;
    retained->premise_occurrences = premises;
    retained->witness_ids = witnesses;
    retained->parameter_ids = parameters;
    retained->index_ids = indices;
    return retained;
}

bool cetta_prime_typed_value_has_application_head_private_v1(
    Arena *scratch, const Space *space,
    const CettaPrimeTypedValueV1 *value,
    const char *head_name, size_t argument_count) {
    if (!scratch || !space || !space->native.universe || !value ||
        !head_name || head_name[0] == '\0' || argument_count == 0u ||
        !cetta_prime_typed_value_v1_is_current(value, space)) {
        return false;
    }
    Atom *cursor = term_universe_copy_atom(
        space->native.universe, scratch, value->term_id);
    size_t remaining = argument_count;
    while (remaining > 0u && cursor && cursor->kind == ATOM_EXPR &&
           cursor->expr.len == 3u &&
           atom_is_symbol(cursor->expr.elems[0], "App")) {
        cursor = cursor->expr.elems[1];
        remaining--;
    }
    if (remaining == 0u) return atom_is_symbol(cursor, head_name);
    return remaining != SIZE_MAX && cursor && cursor->kind == ATOM_EXPR &&
           cursor->expr.len == remaining + 1u &&
           atom_is_symbol(cursor->expr.elems[0], head_name);
}

CettaPrimeTypedValueV1 *
cetta_prime_typed_value_attach_indexed_application_private_v1(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *family_name,
    size_t parameter_count, size_t index_count) {
    size_t argument_count = 0u;
    if (!owner || !space || !space->native.universe || !value ||
        !family_name || family_name[0] == '\0' ||
        !cetta_prime_typed_value_v1_is_current(value, space) ||
        !size_add_ok(parameter_count, index_count, &argument_count) ||
        argument_count == 0u ||
        argument_count > SIZE_MAX / sizeof(Atom *)) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *type = term_universe_copy_atom(universe, owner, value->type_id);
    Atom **arguments = arena_alloc(
        owner, argument_count * sizeof(*arguments));
    Atom *cursor = type;
    for (size_t offset = argument_count; offset > 0u; offset--) {
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !atom_is_symbol(cursor->expr.elems[0], "App")) {
            return NULL;
        }
        arguments[offset - 1u] = cursor->expr.elems[2];
        cursor = cursor->expr.elems[1];
    }
    if (!atom_is_symbol(cursor, family_name)) return NULL;

    AtomId family_head_id = term_universe_store_atom_id(
        universe, owner, cursor);
    AtomId *parameter_ids = parameter_count == 0u ? NULL :
        arena_alloc(owner, parameter_count * sizeof(*parameter_ids));
    AtomId *index_ids = index_count == 0u ? NULL :
        arena_alloc(owner, index_count * sizeof(*index_ids));
    if (family_head_id == CETTA_ATOM_ID_NONE) return NULL;
    for (size_t index = 0u; index < parameter_count; index++) {
        parameter_ids[index] = term_universe_store_atom_id(
            universe, owner, arguments[index]);
        if (parameter_ids[index] == CETTA_ATOM_ID_NONE) return NULL;
    }
    for (size_t index = 0u; index < index_count; index++) {
        index_ids[index] = term_universe_store_atom_id(
            universe, owner, arguments[parameter_count + index]);
        if (index_ids[index] == CETTA_ATOM_ID_NONE) return NULL;
    }

    value->family_head_id = family_head_id;
    value->parameter_ids = parameter_ids;
    value->parameter_count = parameter_count;
    value->index_ids = index_ids;
    value->index_count = index_count;
    return value;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_allocate_private_v1(
    Arena *owner, TermUniverse *universe, AtomId context_id,
    AtomId term_id, AtomId type_id,
    const CettaNikDirectAuthorityTokenV1 *authority_token,
    const CettaPrimeTypedValueBuildPrivateV1 *build) {
    if (!owner || !universe || context_id == CETTA_ATOM_ID_NONE ||
        term_id == CETTA_ATOM_ID_NONE || type_id == CETTA_ATOM_ID_NONE ||
        !authority_token || !build || !build->rule_name ||
        build->rule_name[0] == '\0' ||
        build->construction < CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1 ||
        build->construction > CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 ||
        (build->premise_count != 0u && !build->premises) ||
        (build->witness_count != 0u && !build->witness_ids) ||
        (build->parameter_count != 0u && !build->parameter_ids) ||
        (build->index_count != 0u && !build->index_ids) ||
        (build->family_head_id == CETTA_ATOM_ID_NONE &&
         (build->parameter_count != 0u || build->index_count != 0u))) {
        return NULL;
    }

    size_t maximum_nodes = 1u;
    size_t maximum_premises = build->premise_count;
    size_t maximum_witnesses = build->witness_count;
    for (size_t index = 0u; index < build->premise_count; index++) {
        const CettaPrimeTypedValueV1 *premise = build->premises[index];
        if (!premise || premise->derivation_node_count == 0u ||
            !premise->derivation_nodes ||
            (premise->premise_occurrence_count != 0u &&
             !premise->premise_occurrences) ||
            (premise->witness_count != 0u && !premise->witness_ids) ||
            !size_add_ok(
                maximum_nodes, premise->derivation_node_count,
                &maximum_nodes) ||
            !size_add_ok(
                maximum_premises, premise->premise_occurrence_count,
                &maximum_premises) ||
            !size_add_ok(
                maximum_witnesses, premise->witness_count,
                &maximum_witnesses)) {
            return NULL;
        }
    }
    if (maximum_nodes > SIZE_MAX / sizeof(CettaPrimeTypedDerivationNodeV1) ||
        maximum_premises > SIZE_MAX / sizeof(uint64_t) ||
        maximum_witnesses > SIZE_MAX / sizeof(AtomId) ||
        build->parameter_count > SIZE_MAX / sizeof(AtomId) ||
        build->index_count > SIZE_MAX / sizeof(AtomId)) {
        return NULL;
    }

    CettaPrimeTypedDerivationNodeV1 *nodes = arena_alloc(
        owner, maximum_nodes * sizeof(*nodes));
    uint64_t *premise_occurrences = maximum_premises == 0u ? NULL :
        arena_alloc(owner, maximum_premises * sizeof(*premise_occurrences));
    AtomId *witness_ids = maximum_witnesses == 0u ? NULL :
        arena_alloc(owner, maximum_witnesses * sizeof(*witness_ids));
    size_t node_count = 0u;
    size_t premise_occurrence_count = 0u;
    size_t witness_count = 0u;
    for (size_t premise_index = 0u;
         premise_index < build->premise_count; premise_index++) {
        const CettaPrimeTypedValueV1 *premise = build->premises[premise_index];
        for (size_t node_index = 0u;
             node_index < premise->derivation_node_count; node_index++) {
            const CettaPrimeTypedDerivationNodeV1 *source =
                &premise->derivation_nodes[node_index];
            if (derivation_contains_occurrence(
                    nodes, node_count, source->occurrence_identity)) {
                continue;
            }
            if (source->premise_offset > premise->premise_occurrence_count ||
                source->premise_count >
                    premise->premise_occurrence_count - source->premise_offset ||
                source->witness_offset > premise->witness_count ||
                source->witness_count >
                    premise->witness_count - source->witness_offset) {
                return NULL;
            }
            CettaPrimeTypedDerivationNodeV1 copied = *source;
            copied.premise_offset = premise_occurrence_count;
            copied.witness_offset = witness_count;
            for (size_t edge = 0u; edge < source->premise_count; edge++)
                premise_occurrences[premise_occurrence_count++] =
                    premise->premise_occurrences[
                        source->premise_offset + edge];
            for (size_t witness = 0u; witness < source->witness_count; witness++)
                witness_ids[witness_count++] = premise->witness_ids[
                    source->witness_offset + witness];
            nodes[node_count++] = copied;
        }
    }

    AtomId rule_id = term_universe_store_atom_id(
        universe, owner, atom_symbol(owner, build->rule_name));
    if (rule_id == CETTA_ATOM_ID_NONE) return NULL;
    uint64_t occurrence_identity = prime_typed_fresh_occurrence();
    size_t root_premise_offset = premise_occurrence_count;
    for (size_t index = 0u; index < build->premise_count; index++)
        premise_occurrences[premise_occurrence_count++] =
            build->premises[index]->occurrence_identity;
    size_t root_witness_offset = witness_count;
    for (size_t index = 0u; index < build->witness_count; index++)
        witness_ids[witness_count++] = build->witness_ids[index];
    nodes[node_count++] = (CettaPrimeTypedDerivationNodeV1){
        .occurrence_identity = occurrence_identity,
        .rule_id = rule_id,
        .construction = build->construction,
        .premise_offset = root_premise_offset,
        .premise_count = build->premise_count,
        .witness_offset = root_witness_offset,
        .witness_count = build->witness_count,
    };

    AtomId *parameter_ids = build->parameter_count == 0u ? NULL :
        arena_alloc(owner, build->parameter_count * sizeof(*parameter_ids));
    AtomId *index_ids = build->index_count == 0u ? NULL :
        arena_alloc(owner, build->index_count * sizeof(*index_ids));
    for (size_t index = 0u; index < build->parameter_count; index++)
        parameter_ids[index] = build->parameter_ids[index];
    for (size_t index = 0u; index < build->index_count; index++)
        index_ids[index] = build->index_ids[index];

    CettaPrimeTypedValueV1 *value = arena_alloc(owner, sizeof(*value));
    *value = (CettaPrimeTypedValueV1){
        .universe = universe,
        .universe_instance_id = universe->instance_id,
        .universe_storage_epoch = universe->storage_epoch,
        .context_id = context_id,
        .term_id = term_id,
        .type_id = type_id,
        .authority_token = *authority_token,
        .construction = build->construction,
        .occurrence_identity = occurrence_identity,
        .rule_id = rule_id,
        .derivation_nodes = nodes,
        .derivation_node_count = node_count,
        .premise_occurrences = premise_occurrences,
        .premise_occurrence_count = premise_occurrence_count,
        .witness_ids = witness_ids,
        .witness_count = witness_count,
        .family_head_id = build->family_head_id,
        .parameter_ids = parameter_ids,
        .parameter_count = build->parameter_count,
        .index_ids = index_ids,
        .index_count = build->index_count,
    };
    return value;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_compute_private_v1(
    Arena *owner, Space *space, const char *rule_name,
    const CettaPrimeTypedValueV1 *redex,
    const CettaPrimeTypedValueV1 *reduct,
    const AtomId *witness_ids, size_t witness_count) {
    const CettaPrimeTypedValueV1 *premises[] = {redex, reduct};
    if (!owner || !space || !space->native.universe || !rule_name ||
        rule_name[0] == '\0' ||
        (witness_count != 0u && !witness_ids) ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        redex->type_id != reduct->type_id) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = rule_name,
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witness_ids,
        .witness_count = witness_count,
        .family_head_id = reduct->family_head_id,
        .parameter_ids = reduct->parameter_ids,
        .parameter_count = reduct->parameter_count,
        .index_ids = reduct->index_ids,
        .index_count = reduct->index_count,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, space->native.universe, reduct->context_id,
        reduct->term_id, reduct->type_id, &reduct->authority_token,
        &build);
}

bool cetta_prime_typed_value_v1_is_current(
    const CettaPrimeTypedValueV1 *value, const Space *space) {
    return value && space && space->native.universe &&
           value->universe == space->native.universe &&
           value->universe_instance_id == space->native.universe->instance_id &&
           value->universe_storage_epoch ==
               space->native.universe->storage_epoch &&
           cetta_prime_typing_direct_authority_token_v1_is_current(
               &value->authority_token, space,
               CETTA_PRIME_TYPED_FLOW_POLICY_V1);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_refl_v1(
    Arena *owner, Space *space, const CettaPrimeTypedValueV1 *value) {
    if (!owner || !cetta_prime_typed_value_v1_is_current(value, space))
        return NULL;

    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    Atom *term = term_universe_copy_atom(universe, owner, value->term_id);
    Atom *type = term_universe_copy_atom(universe, owner, value->type_id);
    if (!term || !type) return NULL;

    Atom *refl = atom_expr2(owner, atom_symbol(owner, "Refl"), term);
    Atom *id_elements[4] = {
        atom_symbol(owner, "Id"), type, term, term,
    };
    Atom *id_type = atom_expr(owner, id_elements, 4u);
    AtomId refl_id = term_universe_store_atom_id(universe, owner, refl);
    AtomId id_type_id = term_universe_store_atom_id(universe, owner, id_type);
    AtomId id_family_id = term_universe_store_atom_id(
        universe, owner, atom_symbol(owner, "Id"));
    CettaNikDirectAuthorityTokenV1 token;
    if (refl_id == CETTA_ATOM_ID_NONE || id_type_id == CETTA_ATOM_ID_NONE ||
        id_family_id == CETTA_ATOM_ID_NONE ||
        universe != space->native.universe ||
        universe->instance_id != instance_before ||
        universe->storage_epoch != epoch_before ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &value->authority_token, &token)) {
        return NULL;
    }

    const CettaPrimeTypedValueV1 *premises[] = {value};
    AtomId parameters[] = {value->type_id};
    AtomId indices[] = {value->term_id, value->term_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "refl",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = 1u,
        .family_head_id = id_family_id,
        .parameter_ids = parameters,
        .parameter_count = 1u,
        .index_ids = indices,
        .index_count = 2u,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, value->context_id, refl_id, id_type_id, &token,
        &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *argument) {
    const CettaPrimeTypedValueV1 *premises[] = {function, argument};
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *function_type = term_universe_copy_atom(
        universe, owner, function->type_id);
    Atom *function_term = term_universe_copy_atom(
        universe, owner, function->term_id);
    Atom *argument_term = term_universe_copy_atom(
        universe, owner, argument->term_id);
    if (!function_type || !function_term || !argument_term ||
        function_type->kind != ATOM_EXPR ||
        function_type->expr.len != 3u ||
        !atom_is_symbol(function_type->expr.elems[0], "Pi") ||
        !term_universe_atom_id_eq(
            universe, argument->type_id, function_type->expr.elems[1])) {
        return NULL;
    }

    AbtSignature signature;
    abt_signature_init(&signature);
    bool signature_ready = abt_signature_add_defaults(&signature, owner);
    Atom *result_type = signature_ready
        ? abt_subst(
              &signature, owner, 0u, argument_term,
              function_type->expr.elems[2])
        : NULL;
    abt_signature_free(&signature);
    if (!result_type) return NULL;

    Atom *application = atom_expr3(
        owner, atom_symbol(owner, "App"), function_term, argument_term);
    AtomId application_id = term_universe_store_atom_id(
        universe, owner, application);
    AtomId result_type_id = term_universe_store_atom_id(
        universe, owner, result_type);
    CettaNikDirectAuthorityTokenV1 token;
    if (application_id == CETTA_ATOM_ID_NONE ||
        result_type_id == CETTA_ATOM_ID_NONE ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &function->authority_token, &token) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &argument->authority_token, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "app",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, function->context_id,
        application_id, result_type_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_converting_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *argument) {
    CettaPrimeTypedValueV1 *exact = cetta_prime_typed_value_apply_v1(
        owner, space, function, argument);
    if (exact) return exact;

    const CettaPrimeTypedValueV1 *premises[] = {argument, function};
    if (!owner || !space || !space->native.universe ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *context = term_universe_copy_atom(
        universe, owner, function->context_id);
    Atom *function_type = term_universe_copy_atom(
        universe, owner, function->type_id);
    Atom *source_type = term_universe_copy_atom(
        universe, owner, argument->type_id);
    if (!context || !function_type || !source_type ||
        function_type->kind != ATOM_EXPR ||
        function_type->expr.len != 3u ||
        !atom_is_symbol(function_type->expr.elems[0], "Pi")) {
        return NULL;
    }
    Atom *domain = function_type->expr.elems[1];
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
            owner, context, source_type, domain, &budget);
    if (decision.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !decision.equal) {
        return NULL;
    }

    AtomId domain_id = term_universe_store_atom_id(
        universe, owner, domain);
    CettaNikDirectAuthorityTokenV1 token;
    if (domain_id == CETTA_ATOM_ID_NONE ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, &token) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &argument->authority_token, &token) ||
        !cetta_nik_direct_authority_token_v1_equal(
            &function->authority_token, &token)) {
        return NULL;
    }
    AtomId witnesses[] = {argument->type_id, domain_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "conv:judgmental",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    CettaPrimeTypedValueV1 *converted =
        cetta_prime_typed_value_allocate_private_v1(
            owner, universe, argument->context_id,
            argument->term_id, domain_id, &token, &build);
    return converted
        ? cetta_prime_typed_value_apply_v1(
              owner, space, function, converted)
        : NULL;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_many_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *const *arguments,
    size_t argument_count) {
    if (!owner || !space || !function || !arguments ||
        argument_count == 0u) {
        return NULL;
    }
    const CettaPrimeTypedValueV1 *current = function;
    CettaPrimeTypedValueV1 *result = NULL;
    for (size_t index = 0u; current && index < argument_count; index++) {
        result = cetta_prime_typed_value_apply_v1(
            owner, space, current, arguments[index]);
        current = result;
    }
    return result;
}

static Atom *prime_typed_beta_whnf(
    const AbtSignature *signature, Arena *owner, Atom *term,
    bool *reduced_out) {
    if (reduced_out) *reduced_out = false;
    if (!signature || !owner || !term || !reduced_out) return NULL;

    Atom *current = term;
    bool reduced = false;
    for (;;) {
        if (!current || current->kind != ATOM_EXPR ||
            current->expr.len != 3u ||
            !atom_is_symbol(current->expr.elems[0], "App")) {
            *reduced_out = reduced;
            return current;
        }

        bool head_reduced = false;
        Atom *head = prime_typed_beta_whnf(
            signature, owner, current->expr.elems[1], &head_reduced);
        if (!head) return NULL;
        Atom *argument = current->expr.elems[2];
        if (head_reduced) {
            current = atom_expr3(
                owner, atom_symbol(owner, "App"), head, argument);
            reduced = true;
        }
        if (head->kind != ATOM_EXPR || head->expr.len != 2u ||
            !atom_is_symbol(head->expr.elems[0], "Lam")) {
            *reduced_out = reduced;
            return current;
        }

        current = abt_subst(
            signature, owner, 0u, argument, head->expr.elems[1]);
        if (!current) return NULL;
        reduced = true;
    }
}

CettaPrimeTypedValueV1 *cetta_prime_typed_value_convert_beta_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *value,
    const CettaPrimeTypedValueV1 *target_type) {
    const CettaPrimeTypedValueV1 *premises[] = {value, target_type};
    if (!owner || !space || !space->native.universe ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *source = term_universe_copy_atom(
        universe, owner, value->type_id);
    Atom *target = term_universe_copy_atom(
        universe, owner, target_type->term_id);
    Atom *target_sort = term_universe_copy_atom(
        universe, owner, target_type->type_id);
    if (!source || !target || !target_sort ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(target_sort)) {
        return NULL;
    }

    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, owner)) {
        abt_signature_free(&signature);
        return NULL;
    }
    bool source_reduced = false;
    bool target_reduced = false;
    Atom *source_normal = prime_typed_beta_whnf(
        &signature, owner, source, &source_reduced);
    Atom *target_normal = prime_typed_beta_whnf(
        &signature, owner, target, &target_reduced);
    abt_signature_free(&signature);
    if (!source_normal || !target_normal ||
        (!source_reduced && !target_reduced) ||
        !atom_eq(source_normal, target_normal)) {
        return NULL;
    }

    AtomId source_normal_id = term_universe_store_atom_id(
        universe, owner, source_normal);
    AtomId target_normal_id = term_universe_store_atom_id(
        universe, owner, target_normal);
    if (source_normal_id == CETTA_ATOM_ID_NONE ||
        target_normal_id == CETTA_ATOM_ID_NONE) {
        return NULL;
    }
    AtomId witnesses[] = {
        value->type_id, target_type->term_id,
        source_normal_id, target_normal_id,
    };
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "conv:beta",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, value->context_id, value->term_id,
        target_type->term_id, &value->authority_token, &build);
}

bool cetta_prime_typed_value_v1_metadata(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedValueMetadataV1 *metadata_out) {
    if (!value || !metadata_out) return false;
    *metadata_out = (CettaPrimeTypedValueMetadataV1){
        .universe_instance_id = value->universe_instance_id,
        .universe_storage_epoch = value->universe_storage_epoch,
        .context_id = value->context_id,
        .term_id = value->term_id,
        .type_id = value->type_id,
        .occurrence_identity = value->occurrence_identity,
        .rule_id = value->rule_id,
        .construction = value->construction,
    };
    return true;
}

bool cetta_prime_typed_value_v1_derivation(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedDerivationViewV1 *derivation_out) {
    if (!value || !derivation_out || value->derivation_node_count == 0u ||
        !value->derivation_nodes) {
        return false;
    }
    *derivation_out = (CettaPrimeTypedDerivationViewV1){
        .root_occurrence_identity = value->occurrence_identity,
        .nodes = value->derivation_nodes,
        .node_count = value->derivation_node_count,
        .premise_occurrences = value->premise_occurrences,
        .premise_occurrence_count = value->premise_occurrence_count,
        .witness_ids = value->witness_ids,
        .witness_count = value->witness_count,
    };
    return true;
}

bool cetta_prime_typed_value_v1_indexed_view(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedIndexedViewV1 *indexed_out) {
    if (!value || !indexed_out ||
        value->family_head_id == CETTA_ATOM_ID_NONE ||
        (value->parameter_count != 0u && !value->parameter_ids) ||
        (value->index_count != 0u && !value->index_ids)) {
        return false;
    }
    *indexed_out = (CettaPrimeTypedIndexedViewV1){
        .family_head_id = value->family_head_id,
        .parameter_ids = value->parameter_ids,
        .parameter_count = value->parameter_count,
        .index_ids = value->index_ids,
        .index_count = value->index_count,
    };
    return true;
}

bool cetta_prime_typed_value_v1_erase(
    const CettaPrimeTypedValueV1 *value,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out, Atom **type_out) {
    if (term_out) *term_out = NULL;
    if (type_out) *type_out = NULL;
    if (!value || !live_universe || !destination ||
        value->universe != live_universe ||
        value->universe_instance_id != live_universe->instance_id ||
        value->universe_storage_epoch != live_universe->storage_epoch) {
        return false;
    }
    Atom *term = term_universe_copy_atom(
        live_universe, destination, value->term_id);
    Atom *type = term_universe_copy_atom(
        live_universe, destination, value->type_id);
    if (!term || !type) return false;
    if (term_out) *term_out = term;
    if (type_out) *type_out = type;
    return true;
}
