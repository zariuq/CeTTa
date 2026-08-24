#include "prime_typed_list_relator.h"

#include "prime_typed_flow_private.h"
#include "prime_typed_list.h"

static CettaPrimeTypedValueV1 *prime_typed_list_map_rel_attach(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *constructor_name, size_t constructor_arity) {
    if (!cetta_prime_typed_value_has_application_head_private_v1(
            owner, space, value, constructor_name, constructor_arity)) {
        return NULL;
    }
    return cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, value, "map-rel", 3u, 2u);
}

/* The three family parameters are types-as-terms and an ordinary relation.
 * Specializing a universe-polymorphic declaration may therefore cross the
 * canonical `U1` / `Sort (LevelConst 0)` presentation seam.  Each step uses
 * Prime's explicit judgmental conversion and retains that conversion in the
 * typed derivation instead of requiring syntactic identity. */
static CettaPrimeTypedValueV1 *prime_typed_list_map_rel_specialize(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *family,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation) {
    const CettaPrimeTypedValueV1 *arguments[] = {
        source_type, target_type, relation,
    };
    const CettaPrimeTypedValueV1 *current = family;
    CettaPrimeTypedValueV1 *result = NULL;
    for (size_t index = 0u;
         current && index < sizeof(arguments) / sizeof(arguments[0]);
         index++) {
        result = cetta_prime_typed_value_apply_converting_v1(
            owner, space, current, arguments[index]);
        current = result;
    }
    return result;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation) {
    return prime_typed_list_map_rel_attach(
        owner, space,
        prime_typed_list_map_rel_specialize(
            owner, space, nil_rule,
            source_type, target_type, relation),
        "map-rel:nil", 3u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source_head,
    const CettaPrimeTypedValueV1 *target_head,
    const CettaPrimeTypedValueV1 *source_tail,
    const CettaPrimeTypedValueV1 *target_tail,
    const CettaPrimeTypedValueV1 *head_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence) {
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_map_rel_specialize(
            owner, space, cons_rule,
            source_type, target_type, relation);
    const CettaPrimeTypedValueV1 *arguments[] = {
        source_head, target_head, source_tail, target_tail,
        head_evidence, tail_evidence,
    };
    return prime_typed_list_map_rel_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "map-rel:cons", 9u);
}

typedef struct {
    Atom *source_head;
    Atom *target_head;
    Atom *source_tail;
    Atom *target_tail;
    Atom *head_evidence;
    Atom *tail_evidence;
} PrimeTypedListMapRelStepV1;

static bool prime_typed_list_map_rel_list_cons(
    Atom *list, Atom *element_type, Atom *head, Atom *tail) {
    Atom *arguments[3] = {0};
    return cetta_prime_typed_application_spine_private_v1(
               list, "list:cons", arguments, 3u) &&
           atom_eq(arguments[0], element_type) &&
           atom_eq(arguments[1], head) &&
           atom_eq(arguments[2], tail);
}

static bool prime_typed_list_map_rel_list_nil(
    Atom *list, Atom *element_type) {
    Atom *arguments[1] = {0};
    return cetta_prime_typed_application_spine_private_v1(
               list, "list:nil", arguments, 1u) &&
           atom_eq(arguments[0], element_type);
}

static bool prime_typed_list_map_rel_spine_length(
    Atom *evidence, Atom *source_type, Atom *target_type,
    Atom *relation, Atom *source_list, Atom *target_list,
    size_t *length_out) {
    if (!evidence || !source_type || !target_type || !relation ||
        !source_list || !target_list || !length_out) {
        return false;
    }
    size_t length = 0u;
    Atom *evidence_cursor = evidence;
    Atom *source_cursor = source_list;
    Atom *target_cursor = target_list;
    for (;;) {
        Atom *nil_arguments[3] = {0};
        if (cetta_prime_typed_application_spine_private_v1(
                evidence_cursor, "map-rel:nil", nil_arguments, 3u)) {
            if (!atom_eq(nil_arguments[0], source_type) ||
                !atom_eq(nil_arguments[1], target_type) ||
                !atom_eq(nil_arguments[2], relation) ||
                !prime_typed_list_map_rel_list_nil(
                    source_cursor, source_type) ||
                !prime_typed_list_map_rel_list_nil(
                    target_cursor, target_type)) {
                return false;
            }
            *length_out = length;
            return true;
        }

        Atom *arguments[9] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                evidence_cursor, "map-rel:cons", arguments, 9u) ||
            !atom_eq(arguments[0], source_type) ||
            !atom_eq(arguments[1], target_type) ||
            !atom_eq(arguments[2], relation) ||
            !prime_typed_list_map_rel_list_cons(
                source_cursor, source_type,
                arguments[3], arguments[5]) ||
            !prime_typed_list_map_rel_list_cons(
                target_cursor, target_type,
                arguments[4], arguments[6]) ||
            length == SIZE_MAX) {
            return false;
        }
        length++;
        source_cursor = arguments[5];
        target_cursor = arguments[6];
        evidence_cursor = arguments[8];
    }
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_eliminate_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *eliminate_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *motive,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *source_list,
    const CettaPrimeTypedValueV1 *target_list,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *inputs[] = {
        eliminate_rule, source_type, target_type, relation, motive,
        nil_case, cons_case, source_list, target_list, evidence,
    };
    if (!owner || !space || !space->native.universe ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, inputs, sizeof(inputs) / sizeof(inputs[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    if (!term_universe_atom_id_eq(
            universe, eliminate_rule->term_id,
            atom_symbol(owner, "map-rel:eliminate"))) {
        return NULL;
    }

    CettaPrimeTypedIndexedViewV1 indexed = {0};
    CettaPrimeTypedIndexedViewV1 source_indexed = {0};
    CettaPrimeTypedIndexedViewV1 target_indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(evidence, &indexed) ||
        indexed.parameter_count != 3u || indexed.index_count != 2u ||
        indexed.parameter_ids[0] != source_type->term_id ||
        indexed.parameter_ids[1] != target_type->term_id ||
        indexed.parameter_ids[2] != relation->term_id ||
        indexed.index_ids[0] != source_list->term_id ||
        indexed.index_ids[1] != target_list->term_id ||
        !term_universe_atom_id_eq(
            universe, indexed.family_head_id,
            atom_symbol(owner, "map-rel")) ||
        !cetta_prime_typed_value_v1_indexed_view(
            source_list, &source_indexed) ||
        source_indexed.parameter_count != 1u ||
        source_indexed.index_count != 0u ||
        source_indexed.parameter_ids[0] != source_type->term_id ||
        !term_universe_atom_id_eq(
            universe, source_indexed.family_head_id,
            atom_symbol(owner, "list")) ||
        !cetta_prime_typed_value_v1_indexed_view(
            target_list, &target_indexed) ||
        target_indexed.parameter_count != 1u ||
        target_indexed.index_count != 0u ||
        target_indexed.parameter_ids[0] != target_type->term_id ||
        !term_universe_atom_id_eq(
            universe, target_indexed.family_head_id,
            atom_symbol(owner, "list"))) {
        return NULL;
    }

    Atom *source_type_term = term_universe_copy_atom(
        universe, owner, source_type->term_id);
    Atom *target_type_term = term_universe_copy_atom(
        universe, owner, target_type->term_id);
    Atom *relation_term = term_universe_copy_atom(
        universe, owner, relation->term_id);
    Atom *source_list_term = term_universe_copy_atom(
        universe, owner, source_list->term_id);
    Atom *target_list_term = term_universe_copy_atom(
        universe, owner, target_list->term_id);
    Atom *evidence_term = term_universe_copy_atom(
        universe, owner, evidence->term_id);
    size_t length = 0u;
    if (!source_type_term || !target_type_term || !relation_term ||
        !source_list_term || !target_list_term || !evidence_term ||
        !prime_typed_list_map_rel_spine_length(
            evidence_term, source_type_term, target_type_term,
            relation_term, source_list_term, target_list_term,
            &length) ||
        length > SIZE_MAX / sizeof(PrimeTypedListMapRelStepV1) ||
        length > (SIZE_MAX - 4u) / 6u) {
        return NULL;
    }

    PrimeTypedListMapRelStepV1 *steps = length == 0u ? NULL :
        arena_alloc(owner, length * sizeof(*steps));
    size_t witness_count = 6u * length + 4u;
    if (witness_count > SIZE_MAX / sizeof(AtomId)) return NULL;
    AtomId *witness_ids = arena_alloc(
        owner, witness_count * sizeof(*witness_ids));

    Atom *evidence_cursor = evidence_term;
    for (size_t index = 0u; index < length; index++) {
        Atom *arguments[9] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                evidence_cursor, "map-rel:cons", arguments, 9u)) {
            return NULL;
        }
        steps[index] = (PrimeTypedListMapRelStepV1){
            .source_head = arguments[3],
            .target_head = arguments[4],
            .source_tail = arguments[5],
            .target_tail = arguments[6],
            .head_evidence = arguments[7],
            .tail_evidence = arguments[8],
        };
        evidence_cursor = arguments[8];
    }

    const CettaPrimeTypedValueV1 *redex_arguments[] = {
        source_type, target_type, relation, motive, nil_case, cons_case,
        source_list, target_list, evidence,
    };
    CettaPrimeTypedValueV1 *redex =
        cetta_prime_typed_value_apply_many_v1(
            owner, space, eliminate_rule, redex_arguments,
            sizeof(redex_arguments) / sizeof(redex_arguments[0]));
    Atom *nil_case_term = term_universe_copy_atom(
        universe, owner, nil_case->term_id);
    Atom *cons_case_term = term_universe_copy_atom(
        universe, owner, cons_case->term_id);
    if (!redex || !nil_case_term || !cons_case_term) return NULL;

    Atom *reduct_term = nil_case_term;
    for (size_t offset = length; offset > 0u; offset--) {
        const PrimeTypedListMapRelStepV1 *step = &steps[offset - 1u];
        Atom *arguments[] = {
            step->source_head, step->target_head,
            step->source_tail, step->target_tail,
            step->head_evidence, step->tail_evidence, reduct_term,
        };
        reduct_term = cetta_prime_typed_application_term_private_v1(
            owner, cons_case_term, arguments,
            sizeof(arguments) / sizeof(arguments[0]));
        if (!reduct_term) return NULL;
    }

    size_t witness_index = 0u;
    witness_ids[witness_index++] = evidence->term_id;
    witness_ids[witness_index++] = term_universe_store_atom_id(
        universe, owner, evidence_cursor);
    witness_ids[witness_index++] = nil_case->term_id;
    for (size_t index = 0u; index < length; index++) {
        const PrimeTypedListMapRelStepV1 *step = &steps[index];
        Atom *terms[] = {
            step->source_head, step->target_head,
            step->source_tail, step->target_tail,
            step->head_evidence, step->tail_evidence,
        };
        for (size_t field = 0u;
             field < sizeof(terms) / sizeof(terms[0]); field++) {
            witness_ids[witness_index++] = term_universe_store_atom_id(
                universe, owner, terms[field]);
        }
    }
    AtomId reduct_id = term_universe_store_atom_id(
        universe, owner, reduct_term);
    witness_ids[witness_index++] = reduct_id;
    if (witness_index != witness_count ||
        reduct_id == CETTA_ATOM_ID_NONE) {
        return NULL;
    }
    for (size_t index = 0u; index < witness_count; index++)
        if (witness_ids[index] == CETTA_ATOM_ID_NONE) return NULL;

    const CettaPrimeTypedValueV1 *fold_premises[] = {
        source_type, target_type, relation, motive, nil_case, cons_case,
        source_list, target_list, evidence,
    };
    CettaPrimeTypedValueBuildPrivateV1 fold_build = {
        .rule_name = "map-rel:fold",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = fold_premises,
        .premise_count =
            sizeof(fold_premises) / sizeof(fold_premises[0]),
        .witness_ids = witness_ids,
        .witness_count = witness_count,
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    CettaPrimeTypedValueV1 *reduct =
        cetta_prime_typed_value_allocate_private_v1(
            owner, universe, redex->context_id,
            reduct_id, redex->type_id, &redex->authority_token,
            &fold_build);
    if (!reduct) return NULL;

    AtomId iota_witnesses[] = {
        redex->term_id, reduct->term_id, evidence->term_id,
    };
    return cetta_prime_typed_value_compute_private_v1(
        owner, space, "map-rel:iota-fold", redex, reduct,
        iota_witnesses,
        sizeof(iota_witnesses) / sizeof(iota_witnesses[0]));
}

static bool prime_typed_list_map_rel_source_spine(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *source_list,
    Atom ***heads_out, size_t *length_out) {
    if (heads_out) *heads_out = NULL;
    if (length_out) *length_out = 0u;
    if (!owner || !space || !space->native.universe || !source_type ||
        !source_list || !heads_out || !length_out) {
        return false;
    }
    CettaPrimeTypedIndexedViewV1 indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(source_list, &indexed) ||
        indexed.parameter_count != 1u || indexed.index_count != 0u ||
        indexed.parameter_ids[0] != source_type->term_id ||
        !term_universe_atom_id_eq(
            space->native.universe, indexed.family_head_id,
            atom_symbol(owner, "list"))) {
        return false;
    }
    Atom *source_type_term = term_universe_copy_atom(
        space->native.universe, owner, source_type->term_id);
    Atom *source_list_term = term_universe_copy_atom(
        space->native.universe, owner, source_list->term_id);
    if (!source_type_term || !source_list_term) return false;

    size_t length = 0u;
    Atom *cursor = source_list_term;
    for (;;) {
        Atom *nil_arguments[1] = {0};
        if (cetta_prime_typed_application_spine_private_v1(
                cursor, "list:nil", nil_arguments, 1u)) {
            if (!atom_eq(nil_arguments[0], source_type_term)) return false;
            break;
        }
        Atom *cons_arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", cons_arguments, 3u) ||
            !atom_eq(cons_arguments[0], source_type_term) ||
            length == SIZE_MAX) {
            return false;
        }
        length++;
        cursor = cons_arguments[2];
    }
    if (length > SIZE_MAX / sizeof(Atom *)) return false;
    Atom **heads = length == 0u
        ? NULL
        : arena_alloc(owner, length * sizeof(*heads));
    if (length != 0u && !heads) return false;
    cursor = source_list_term;
    for (size_t index = 0u; index < length; index++) {
        Atom *arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", arguments, 3u) ||
            !atom_eq(arguments[0], source_type_term)) {
            return false;
        }
        heads[index] = arguments[1];
        cursor = arguments[2];
    }
    *heads_out = heads;
    *length_out = length;
    return true;
}

static bool prime_typed_list_map_rel_matching_occurrence(
    Arena *scratch, const TermUniverse *universe,
    const CettaPrimeTypedFiniteRelationV1 *provider,
    Atom *source_head, size_t ordinal,
    size_t *provider_index_out,
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 *occurrence_out) {
    if (provider_index_out) *provider_index_out = SIZE_MAX;
    if (occurrence_out)
        *occurrence_out =
            (CettaPrimeTypedFiniteRelationOccurrenceViewV1){0};
    if (!scratch || !universe || !provider || !source_head ||
        !provider_index_out || !occurrence_out) {
        return false;
    }
    const size_t occurrence_count =
        cetta_prime_typed_finite_relation_occurrence_count_v1(provider);
    size_t matched = 0u;
    for (size_t index = 0u; index < occurrence_count; index++) {
        CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
        if (!cetta_prime_typed_finite_relation_occurrence_v1(
                provider, index, &occurrence)) {
            return false;
        }
        if (!term_universe_atom_id_eq(
                universe, occurrence.source->term_id, source_head)) {
            continue;
        }
        if (matched == ordinal) {
            *provider_index_out = index;
            *occurrence_out = occurrence;
            return true;
        }
        matched++;
    }
    return false;
}

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_list_map_rel_finite_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const CettaPrimeTypedValueV1 *source_list,
    const CettaPrimeTypedValueV1 *list_family,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    const CettaPrimeTypedValueV1 *map_rel_family,
    const CettaPrimeTypedValueV1 *map_rel_nil_rule,
    const CettaPrimeTypedValueV1 *map_rel_cons_rule,
    CettaPrimeTypedListMapRelFiniteV1 *result_out) {
    if (result_out)
        *result_out = (CettaPrimeTypedListMapRelFiniteV1){0};
    const CettaPrimeTypedValueV1 *header[] = {
        source_list, list_family, list_nil_rule, list_cons_rule,
        map_rel_family, map_rel_nil_rule, map_rel_cons_rule,
    };
    if (!owner || !space || !space->native.universe || !provider ||
        !result_out ||
        !cetta_prime_typed_finite_relation_is_current_v1(provider, space) ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, header, sizeof(header) / sizeof(header[0]))) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    const CettaPrimeTypedValueV1 *source_type =
        cetta_prime_typed_finite_relation_source_type_v1(provider);
    const CettaPrimeTypedValueV1 *target_type =
        cetta_prime_typed_finite_relation_target_type_v1(provider);
    const CettaPrimeTypedValueV1 *relation =
        cetta_prime_typed_finite_relation_relation_v1(provider);
    const CettaPrimeTypedValueV1 *all_values[] = {
        source_type, target_type, relation,
        source_list, list_family, list_nil_rule, list_cons_rule,
        map_rel_family, map_rel_nil_rule, map_rel_cons_rule,
    };
    if (!cetta_prime_typed_values_cohere_private_v1(
            space, all_values,
            sizeof(all_values) / sizeof(all_values[0]))) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }

    Atom **source_heads = NULL;
    size_t source_length = 0u;
    if (!prime_typed_list_map_rel_source_spine(
            owner, space, source_type, source_list,
            &source_heads, &source_length) ||
        source_length > SIZE_MAX / sizeof(size_t)) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }
    size_t *option_counts = source_length == 0u
        ? NULL
        : arena_alloc(owner, source_length * sizeof(*option_counts));
    size_t *strides = source_length == 0u
        ? NULL
        : arena_alloc(owner, source_length * sizeof(*strides));
    if (source_length != 0u && (!option_counts || !strides))
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;

    const size_t provider_count =
        cetta_prime_typed_finite_relation_occurrence_count_v1(provider);
    for (size_t position = 0u; position < source_length; position++) {
        option_counts[position] = 0u;
        for (size_t index = 0u; index < provider_count; index++) {
            CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
            if (!cetta_prime_typed_finite_relation_occurrence_v1(
                    provider, index, &occurrence)) {
                return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
            }
            if (term_universe_atom_id_eq(
                    space->native.universe, occurrence.source->term_id,
                    source_heads[position])) {
                if (option_counts[position] == SIZE_MAX)
                    return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
                option_counts[position]++;
            }
        }
    }

    size_t output_count = 1u;
    for (size_t offset = source_length; offset > 0u; offset--) {
        size_t position = offset - 1u;
        strides[position] = output_count;
        if (option_counts[position] == 0u) {
            output_count = 0u;
            for (size_t earlier = 0u; earlier < position; earlier++)
                strides[earlier] = 0u;
            break;
        }
        if (output_count > SIZE_MAX / option_counts[position])
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        output_count *= option_counts[position];
    }
    if (output_count > SIZE_MAX /
            sizeof(CettaPrimeTypedFiniteRelationOccurrenceInputV1) ||
        output_count > SIZE_MAX / sizeof(CettaPrimeTypedValueV1 *) ||
        (source_length != 0u && output_count > SIZE_MAX / source_length) ||
        output_count * source_length > SIZE_MAX / sizeof(size_t)) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }

    CettaPrimeTypedValueV1 *source_list_type =
        cetta_prime_typed_value_apply_converting_v1(
            owner, space, list_family, source_type);
    CettaPrimeTypedValueV1 *target_list_type =
        cetta_prime_typed_value_apply_converting_v1(
            owner, space, list_family, target_type);
    CettaPrimeTypedValueV1 *lifted_relation =
        source_list_type && target_list_type
        ? prime_typed_list_map_rel_specialize(
              owner, space, map_rel_family,
              source_type, target_type, relation)
        : NULL;
    CettaPrimeTypedRelationViewV1 lifted_view = {0};
    if (!source_list_type || !target_list_type || !lifted_relation ||
        source_list->type_id != source_list_type->term_id ||
        !cetta_prime_typed_relation_v1_view(
            owner, space, lifted_relation, &lifted_view) ||
        lifted_view.source_type_id != source_list_type->term_id ||
        lifted_view.target_type_id != target_list_type->term_id) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
    }

    CettaPrimeTypedFiniteRelationOccurrenceInputV1 *outputs =
        output_count == 0u
        ? NULL
        : arena_alloc(owner, output_count * sizeof(*outputs));
    CettaPrimeTypedValueV1 **target_lists = output_count == 0u
        ? NULL
        : arena_alloc(owner, output_count * sizeof(*target_lists));
    CettaPrimeTypedValueV1 **evidences = output_count == 0u
        ? NULL
        : arena_alloc(owner, output_count * sizeof(*evidences));
    size_t *origins = output_count == 0u || source_length == 0u
        ? NULL
        : arena_alloc(
              owner, output_count * source_length * sizeof(*origins));
    if (output_count != 0u && (!outputs || !target_lists || !evidences ||
        (source_length != 0u && !origins))) {
        return CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1;
    }

    for (size_t output = 0u; output < output_count; output++) {
        CettaPrimeTypedValueV1 *source_tail =
            cetta_prime_typed_list_nil_v1(
                owner, space, list_nil_rule, source_type);
        CettaPrimeTypedValueV1 *target_tail =
            cetta_prime_typed_list_nil_v1(
                owner, space, list_nil_rule, target_type);
        CettaPrimeTypedValueV1 *evidence =
            cetta_prime_typed_list_map_rel_nil_v1(
                owner, space, map_rel_nil_rule,
                source_type, target_type, relation);
        if (!source_tail || !target_tail || !evidence)
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;

        for (size_t offset = source_length; offset > 0u; offset--) {
            size_t position = offset - 1u;
            size_t ordinal =
                (output / strides[position]) % option_counts[position];
            size_t provider_index = SIZE_MAX;
            CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
            if (!prime_typed_list_map_rel_matching_occurrence(
                    owner, space->native.universe, provider,
                    source_heads[position], ordinal,
                    &provider_index, &occurrence)) {
                return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
            }
            CettaPrimeTypedValueV1 *next_source =
                cetta_prime_typed_list_cons_v1(
                    owner, space, list_cons_rule, source_type,
                    occurrence.source, source_tail);
            CettaPrimeTypedValueV1 *next_target =
                cetta_prime_typed_list_cons_v1(
                    owner, space, list_cons_rule, target_type,
                    occurrence.target, target_tail);
            CettaPrimeTypedValueV1 *next_evidence =
                next_source && next_target
                ? cetta_prime_typed_list_map_rel_cons_v1(
                      owner, space, map_rel_cons_rule,
                      source_type, target_type, relation,
                      occurrence.source, occurrence.target,
                      source_tail, target_tail,
                      occurrence.evidence, evidence)
                : NULL;
            if (!next_source || !next_target || !next_evidence)
                return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
            source_tail = next_source;
            target_tail = next_target;
            evidence = next_evidence;
            origins[output * source_length + position] = provider_index;
        }
        if (source_tail->term_id != source_list->term_id)
            return CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1;
        outputs[output] =
            (CettaPrimeTypedFiniteRelationOccurrenceInputV1){
                .source = source_list,
                .target = target_tail,
                .evidence = evidence,
            };
        target_lists[output] = target_tail;
        evidences[output] = evidence;
    }

    CettaPrimeTypedFiniteRelationSearchV1 search = {0};
    CettaPrimeTypedFiniteRelationBuildV1 materialized =
        cetta_prime_typed_finite_relation_materialize_fibre_v1(
            owner, space, source_list_type, target_list_type,
            lifted_relation, source_list, outputs, output_count,
            list_nil_rule, list_cons_rule, &search);
    if (materialized != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1)
        return materialized;
    *result_out = (CettaPrimeTypedListMapRelFiniteV1){
        .source_list = source_list,
        .lifted_relation = lifted_relation,
        .target_lists =
            (const CettaPrimeTypedValueV1 *const *)target_lists,
        .evidences = (const CettaPrimeTypedValueV1 *const *)evidences,
        .base_occurrence_indices = origins,
        .source_length = source_length,
        .search = search,
    };
    return CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1;
}
