#include "prime_typed_finite_relation.h"

#include "prime_typed_flow_private.h"
#include "prime_typed_list.h"

typedef struct {
    CettaPrimeTypedValueV1 *source;
    CettaPrimeTypedValueV1 *target;
    CettaPrimeTypedValueV1 *evidence;
} PrimeTypedFiniteRelationOccurrenceV1;

struct CettaPrimeTypedFiniteRelationV1 {
    CettaPrimeTypedValueV1 *source_type;
    CettaPrimeTypedValueV1 *target_type;
    CettaPrimeTypedValueV1 *relation;
    PrimeTypedFiniteRelationOccurrenceV1 *occurrences;
    size_t occurrence_count;
};

static bool prime_typed_finite_relation_occurrence_valid(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrence) {
    if (!owner || !space || !relation || !source_type || !target_type ||
        !occurrence || !occurrence->source || !occurrence->target ||
        !occurrence->evidence) {
        return false;
    }
    CettaPrimeTypedValueMetadataV1 source_metadata = {0};
    CettaPrimeTypedValueMetadataV1 target_metadata = {0};
    CettaPrimeTypedValueMetadataV1 evidence_metadata = {0};
    if (!cetta_prime_typed_value_v1_metadata(
            occurrence->source, &source_metadata) ||
        !cetta_prime_typed_value_v1_metadata(
            occurrence->target, &target_metadata) ||
        !cetta_prime_typed_value_v1_metadata(
            occurrence->evidence, &evidence_metadata) ||
        source_metadata.type_id != source_type->term_id ||
        target_metadata.type_id != target_type->term_id) {
        return false;
    }
    CettaPrimeTypedValueV1 *relation_at_source =
        cetta_prime_typed_value_apply_v1(
            owner, space, relation, occurrence->source);
    CettaPrimeTypedValueV1 *evidence_type = relation_at_source
        ? cetta_prime_typed_value_apply_v1(
              owner, space, relation_at_source, occurrence->target)
        : NULL;
    CettaPrimeTypedValueMetadataV1 evidence_type_metadata = {0};
    return evidence_type &&
        cetta_prime_typed_value_v1_metadata(
            evidence_type, &evidence_type_metadata) &&
        evidence_metadata.type_id == evidence_type_metadata.term_id;
}

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_create_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences,
    size_t occurrence_count,
    CettaPrimeTypedFiniteRelationV1 **provider_out) {
    if (provider_out) *provider_out = NULL;
    const CettaPrimeTypedValueV1 *header[] = {
        source_type, target_type, relation,
    };
    if (!owner || !space || !provider_out ||
        (occurrence_count != 0u && !occurrences) ||
        occurrence_count >
            SIZE_MAX / sizeof(PrimeTypedFiniteRelationOccurrenceV1) ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, header, sizeof(header) / sizeof(header[0]))) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    CettaPrimeTypedRelationViewV1 relation_view = {0};
    CettaPrimeTypedValueMetadataV1 source_type_metadata = {0};
    CettaPrimeTypedValueMetadataV1 target_type_metadata = {0};
    if (!cetta_prime_typed_relation_v1_view(
            owner, space, relation, &relation_view) ||
        !cetta_prime_typed_value_v1_metadata(
            source_type, &source_type_metadata) ||
        !cetta_prime_typed_value_v1_metadata(
            target_type, &target_type_metadata) ||
        relation_view.source_type_id != source_type_metadata.term_id ||
        relation_view.target_type_id != target_type_metadata.term_id) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    for (size_t index = 0u; index < occurrence_count; index++)
        if (!prime_typed_finite_relation_occurrence_valid(
                owner, space, relation, source_type, target_type,
                &occurrences[index])) {
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        }

    CettaPrimeTypedFiniteRelationV1 *provider =
        arena_alloc(owner, sizeof(*provider));
    PrimeTypedFiniteRelationOccurrenceV1 *retained = occurrence_count == 0u
        ? NULL
        : arena_alloc(owner, occurrence_count * sizeof(*retained));
    if (!provider || (occurrence_count != 0u && !retained))
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
    provider->source_type = cetta_prime_typed_value_retain_private_v1(
        owner, space, source_type);
    provider->target_type = cetta_prime_typed_value_retain_private_v1(
        owner, space, target_type);
    provider->relation = cetta_prime_typed_value_retain_private_v1(
        owner, space, relation);
    provider->occurrences = retained;
    provider->occurrence_count = occurrence_count;
    if (!provider->source_type || !provider->target_type ||
        !provider->relation) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
    }
    for (size_t index = 0u; index < occurrence_count; index++) {
        retained[index] = (PrimeTypedFiniteRelationOccurrenceV1){
            .source = cetta_prime_typed_value_retain_private_v1(
                owner, space, occurrences[index].source),
            .target = cetta_prime_typed_value_retain_private_v1(
                owner, space, occurrences[index].target),
            .evidence = cetta_prime_typed_value_retain_private_v1(
                owner, space, occurrences[index].evidence),
        };
        if (!retained[index].source || !retained[index].target ||
            !retained[index].evidence) {
            return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
        }
    }
    *provider_out = provider;
    return CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1;
}

bool cetta_prime_typed_finite_relation_is_current_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const Space *space) {
    if (!provider || !space ||
        !cetta_prime_typed_value_v1_is_current(
            provider->source_type, space) ||
        !cetta_prime_typed_value_v1_is_current(
            provider->target_type, space) ||
        !cetta_prime_typed_value_v1_is_current(
            provider->relation, space) ||
        (provider->occurrence_count != 0u && !provider->occurrences)) {
        return false;
    }
    for (size_t index = 0u; index < provider->occurrence_count; index++) {
        const PrimeTypedFiniteRelationOccurrenceV1 *occurrence =
            &provider->occurrences[index];
        if (!cetta_prime_typed_value_v1_is_current(
                occurrence->source, space) ||
            !cetta_prime_typed_value_v1_is_current(
                occurrence->target, space) ||
            !cetta_prime_typed_value_v1_is_current(
                occurrence->evidence, space)) {
            return false;
        }
    }
    return true;
}

CettaPrimeTypedFiniteRelationV1 *
cetta_prime_typed_finite_relation_retain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider) {
    if (!owner || !space ||
        !cetta_prime_typed_finite_relation_is_current_v1(provider, space) ||
        provider->occurrence_count >
            SIZE_MAX /
                sizeof(CettaPrimeTypedFiniteRelationOccurrenceInputV1)) {
        return NULL;
    }
    CettaPrimeTypedFiniteRelationV1 *retained =
        arena_alloc(owner, sizeof(*retained));
    PrimeTypedFiniteRelationOccurrenceV1 *occurrences =
        provider->occurrence_count == 0u
            ? NULL
            : arena_alloc(
                  owner,
                  provider->occurrence_count * sizeof(*occurrences));
    if (!retained ||
        (provider->occurrence_count != 0u && !occurrences)) {
        return NULL;
    }
    retained->source_type = cetta_prime_typed_value_retain_private_v1(
        owner, space, provider->source_type);
    retained->target_type = cetta_prime_typed_value_retain_private_v1(
        owner, space, provider->target_type);
    retained->relation = cetta_prime_typed_value_retain_private_v1(
        owner, space, provider->relation);
    retained->occurrences = occurrences;
    retained->occurrence_count = provider->occurrence_count;
    if (!retained->source_type || !retained->target_type ||
        !retained->relation) {
        return NULL;
    }
    for (size_t index = 0u; index < provider->occurrence_count; index++) {
        const PrimeTypedFiniteRelationOccurrenceV1 *source =
            &provider->occurrences[index];
        occurrences[index] = (PrimeTypedFiniteRelationOccurrenceV1){
            .source = cetta_prime_typed_value_retain_private_v1(
                owner, space, source->source),
            .target = cetta_prime_typed_value_retain_private_v1(
                owner, space, source->target),
            .evidence = cetta_prime_typed_value_retain_private_v1(
                owner, space, source->evidence),
        };
        if (!occurrences[index].source || !occurrences[index].target ||
            !occurrences[index].evidence) {
            return NULL;
        }
    }
    return retained;
}

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_source_type_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider) {
    return provider ? provider->source_type : NULL;
}

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_target_type_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider) {
    return provider ? provider->target_type : NULL;
}

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_relation_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider) {
    return provider ? provider->relation : NULL;
}

size_t cetta_prime_typed_finite_relation_occurrence_count_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider) {
    return provider ? provider->occurrence_count : 0u;
}

bool cetta_prime_typed_finite_relation_occurrence_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider,
    size_t index,
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 *occurrence_out) {
    if (occurrence_out)
        *occurrence_out =
            (CettaPrimeTypedFiniteRelationOccurrenceViewV1){0};
    if (!provider || !occurrence_out || index >= provider->occurrence_count)
        return false;
    const PrimeTypedFiniteRelationOccurrenceV1 *occurrence =
        &provider->occurrences[index];
    *occurrence_out = (CettaPrimeTypedFiniteRelationOccurrenceViewV1){
        .source = occurrence->source,
        .target = occurrence->target,
        .evidence = occurrence->evidence,
    };
    return true;
}

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *earlier,
    const CettaPrimeTypedFiniteRelationV1 *later,
    CettaPrimeTypedFiniteRelationV1 **provider_out,
    CettaPrimeTypedFiniteRelationChainOriginV1 **origins_out) {
    if (provider_out) *provider_out = NULL;
    if (origins_out) *origins_out = NULL;
    if (!owner || !space || !provider_out || !origins_out ||
        !cetta_prime_typed_finite_relation_is_current_v1(earlier, space) ||
        !cetta_prime_typed_finite_relation_is_current_v1(later, space) ||
        earlier->target_type->term_id != later->source_type->term_id) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }

    CettaPrimeTypedValueV1 *result_type =
        cetta_prime_typed_relation_chain_result_type_v1(
            owner, space, earlier->source_type, earlier->target_type,
            later->target_type, earlier->relation, later->relation);
    CettaPrimeTypedValueV1 *relation = result_type
        ? cetta_prime_typed_relation_chain_v1(
              owner, space, result_type, earlier->target_type,
              earlier->relation, later->relation)
        : NULL;
    if (!relation)
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;

    size_t occurrence_count = 0u;
    for (size_t first = 0u; first < earlier->occurrence_count; first++) {
        for (size_t second = 0u; second < later->occurrence_count; second++) {
            if (earlier->occurrences[first].target->term_id !=
                later->occurrences[second].source->term_id) {
                continue;
            }
            if (occurrence_count == SIZE_MAX)
                return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
            occurrence_count++;
        }
    }
    if (occurrence_count >
            SIZE_MAX /
                sizeof(CettaPrimeTypedFiniteRelationOccurrenceInputV1) ||
        occurrence_count >
            SIZE_MAX /
                sizeof(CettaPrimeTypedFiniteRelationChainOriginV1)) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    CettaPrimeTypedFiniteRelationOccurrenceInputV1 *inputs =
        occurrence_count == 0u
            ? NULL
            : arena_alloc(owner, occurrence_count * sizeof(*inputs));
    CettaPrimeTypedFiniteRelationChainOriginV1 *origins =
        occurrence_count == 0u
            ? NULL
            : arena_alloc(owner, occurrence_count * sizeof(*origins));
    if (occurrence_count != 0u && (!inputs || !origins))
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;

    size_t written = 0u;
    for (size_t first = 0u; first < earlier->occurrence_count; first++) {
        const PrimeTypedFiniteRelationOccurrenceV1 *left =
            &earlier->occurrences[first];
        for (size_t second = 0u; second < later->occurrence_count; second++) {
            const PrimeTypedFiniteRelationOccurrenceV1 *right =
                &later->occurrences[second];
            if (left->target->term_id != right->source->term_id) continue;
            CettaPrimeTypedValueV1 *chain_type =
                cetta_prime_typed_chain_type_v1(
                    owner, space, earlier->target_type,
                    earlier->relation, later->relation,
                    left->source, right->target);
            CettaPrimeTypedValueV1 *evidence = chain_type
                ? cetta_prime_typed_chain_v1(
                      owner, space, chain_type, left->target,
                      left->evidence, right->evidence)
                : NULL;
            CettaPrimeTypedValueV1 *relation_at_source = evidence
                ? cetta_prime_typed_value_apply_v1(
                      owner, space, relation, left->source)
                : NULL;
            CettaPrimeTypedValueV1 *relation_fibre = relation_at_source
                ? cetta_prime_typed_value_apply_v1(
                      owner, space, relation_at_source, right->target)
                : NULL;
            evidence = relation_fibre
                ? cetta_prime_typed_value_convert_beta_v1(
                      owner, space, evidence, relation_fibre)
                : NULL;
            if (!evidence || written >= occurrence_count)
                return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
            inputs[written] =
                (CettaPrimeTypedFiniteRelationOccurrenceInputV1){
                    .source = left->source,
                    .target = right->target,
                    .evidence = evidence,
                };
            origins[written] =
                (CettaPrimeTypedFiniteRelationChainOriginV1){
                    .earlier_index = first,
                    .later_index = second,
                };
            written++;
        }
    }
    if (written != occurrence_count)
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedFiniteRelationV1 *provider = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 built =
        cetta_prime_typed_finite_relation_create_v1(
            owner, space, earlier->source_type, later->target_type,
            relation, inputs, occurrence_count, &provider);
    if (built != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1)
        return built;
    *provider_out = provider;
    *origins_out = origins;
    return CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1;
}

static CettaPrimeTypedValueV1 *prime_typed_finite_relation_receipt(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *list,
    CettaPrimeTypedValueV1 *const *answers,
    const AtomId *witness_ids,
    size_t answer_count) {
    if (!owner || !space || !relation || !source || !list ||
        (answer_count != 0u && (!answers || !witness_ids)) ||
        answer_count > (SIZE_MAX - 3u) ||
        answer_count + 3u >
            SIZE_MAX / sizeof(const CettaPrimeTypedValueV1 *)) {
        return NULL;
    }
    size_t premise_count = answer_count + 3u;
    const CettaPrimeTypedValueV1 **premises = arena_alloc(
        owner, premise_count * sizeof(*premises));
    if (!premises) return NULL;
    premises[0] = list;
    premises[1] = relation;
    premises[2] = source;
    for (size_t index = 0u; index < answer_count; index++)
        premises[index + 3u] = answers[index];
    if (!cetta_prime_typed_values_cohere_private_v1(
            space, premises, premise_count)) {
        return NULL;
    }
    CettaPrimeTypedIndexedViewV1 indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(list, &indexed))
        return NULL;
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel:finite-search",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = premise_count,
        .witness_ids = witness_ids,
        .witness_count = answer_count,
        .family_head_id = indexed.family_head_id,
        .parameter_ids = indexed.parameter_ids,
        .parameter_count = indexed.parameter_count,
        .index_ids = indexed.index_ids,
        .index_count = indexed.index_count,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, space->native.universe, list->context_id,
        list->term_id, list->type_id, &list->authority_token, &build);
}

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_materialize_fibre_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences,
    size_t occurrence_count,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    CettaPrimeTypedFiniteRelationSearchV1 *search_out) {
    if (search_out) *search_out = (CettaPrimeTypedFiniteRelationSearchV1){0};
    const CettaPrimeTypedValueV1 *header[] = {
        source_type, target_type, relation, source,
        list_nil_rule, list_cons_rule,
    };
    if (!owner || !space || !search_out ||
        (occurrence_count != 0u && !occurrences) ||
        occurrence_count > SIZE_MAX / sizeof(CettaPrimeTypedValueV1 *) ||
        occurrence_count > SIZE_MAX / sizeof(size_t) ||
        occurrence_count > SIZE_MAX / sizeof(AtomId) ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, header, sizeof(header) / sizeof(header[0]))) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    CettaPrimeTypedRelationViewV1 relation_view = {0};
    if (!cetta_prime_typed_relation_v1_view(
            owner, space, relation, &relation_view) ||
        relation_view.source_type_id != source_type->term_id ||
        relation_view.target_type_id != target_type->term_id ||
        source->type_id != source_type->term_id) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    for (size_t index = 0u; index < occurrence_count; index++) {
        if (!prime_typed_finite_relation_occurrence_valid(
                owner, space, relation, source_type, target_type,
                &occurrences[index]) ||
            occurrences[index].source->term_id != source->term_id) {
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        }
    }

    CettaPrimeTypedValueV1 **answers = occurrence_count == 0u
        ? NULL
        : arena_alloc(owner, occurrence_count * sizeof(*answers));
    size_t *occurrence_indices = occurrence_count == 0u
        ? NULL
        : arena_alloc(
              owner, occurrence_count * sizeof(*occurrence_indices));
    AtomId *witness_ids = occurrence_count == 0u
        ? NULL
        : arena_alloc(owner, occurrence_count * sizeof(*witness_ids));
    if (occurrence_count != 0u &&
        (!answers || !occurrence_indices || !witness_ids)) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
    }

    CettaPrimeTypedValueV1 *answer_type =
        cetta_prime_typed_relation_answer_type_v1(
            owner, space, relation, source, target_type);
    CettaPrimeTypedValueV1 *typed_list = answer_type
        ? cetta_prime_typed_list_nil_v1(
              owner, space, list_nil_rule, answer_type)
        : NULL;
    if (!typed_list)
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    for (size_t index = 0u; index < occurrence_count; index++) {
        answers[index] = cetta_prime_typed_relation_answer_v1(
            owner, space, relation, source, target_type,
            occurrences[index].target, occurrences[index].evidence);
        occurrence_indices[index] = index;
        witness_ids[index] = occurrences[index].evidence->term_id;
        if (!answers[index])
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    for (size_t offset = occurrence_count; offset > 0u; offset--) {
        typed_list = cetta_prime_typed_list_cons_v1(
            owner, space, list_cons_rule, answer_type,
            answers[offset - 1u], typed_list);
        if (!typed_list)
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    CettaPrimeTypedValueV1 *receipt =
        prime_typed_finite_relation_receipt(
            owner, space, relation, source, typed_list,
            answers, witness_ids, occurrence_count);
    if (!receipt)
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
    *search_out = (CettaPrimeTypedFiniteRelationSearchV1){
        .source = source,
        .answer_type = answer_type,
        .answer_list = typed_list,
        .receipt = receipt,
        .answers = (const CettaPrimeTypedValueV1 *const *)answers,
        .occurrence_indices = occurrence_indices,
        .answer_count = occurrence_count,
    };
    return CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1;
}

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_search_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    CettaPrimeTypedFiniteRelationSearchV1 *search_out) {
    if (search_out) *search_out = (CettaPrimeTypedFiniteRelationSearchV1){0};
    if (!owner || !space || !source || !list_nil_rule || !list_cons_rule ||
        !search_out ||
        !cetta_prime_typed_finite_relation_is_current_v1(provider, space) ||
        source->type_id != provider->source_type->term_id) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    size_t answer_count = 0u;
    for (size_t index = 0u; index < provider->occurrence_count; index++) {
        if (provider->occurrences[index].source->term_id != source->term_id)
            continue;
        if (answer_count == SIZE_MAX)
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        answer_count++;
    }
    if (answer_count > SIZE_MAX / sizeof(CettaPrimeTypedValueV1 *) ||
        answer_count > SIZE_MAX / sizeof(size_t)) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences =
        answer_count == 0u
            ? NULL
            : arena_alloc(owner, answer_count * sizeof(*occurrences));
    size_t *provider_indices = answer_count == 0u
        ? NULL
        : arena_alloc(owner, answer_count * sizeof(*provider_indices));
    if (answer_count != 0u && (!occurrences || !provider_indices))
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;

    size_t written = 0u;
    for (size_t index = 0u; index < provider->occurrence_count; index++) {
        PrimeTypedFiniteRelationOccurrenceV1 *occurrence =
            &provider->occurrences[index];
        if (occurrence->source->term_id != source->term_id) continue;
        if (written >= answer_count)
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        occurrences[written] =
            (CettaPrimeTypedFiniteRelationOccurrenceInputV1){
                .source = occurrence->source,
                .target = occurrence->target,
                .evidence = occurrence->evidence,
            };
        provider_indices[written] = index;
        written++;
    }
    if (written != answer_count)
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    CettaPrimeTypedFiniteRelationBuildV1 materialized =
        cetta_prime_typed_finite_relation_materialize_fibre_v1(
            owner, space, provider->source_type, provider->target_type,
            provider->relation, source, occurrences, answer_count,
            list_nil_rule, list_cons_rule, search_out);
    if (materialized == CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1)
        search_out->occurrence_indices = provider_indices;
    return materialized;
}
