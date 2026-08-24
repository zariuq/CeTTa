#include "prime_typed_relation.h"

#include "abt.h"
#include "prime_regular_kernel.h"
#include "prime_typed_flow_private.h"
#include "prime_typing_authority.h"

typedef struct {
    Atom *source_type;
    Atom *target_type;
    Atom *evidence_universe;
} PrimeTypedRelTypeViewV1;

static bool prime_typed_relation_expr(
    Atom *term, const char *head, size_t length) {
    return term && term->kind == ATOM_EXPR && term->expr.len == length &&
           atom_is_symbol(term->expr.elems[0], head);
}

static Atom *prime_typed_relation_copy(
    Arena *owner, const TermUniverse *universe, AtomId id) {
    return owner && universe && id != CETTA_ATOM_ID_NONE
        ? term_universe_copy_atom(universe, owner, id)
        : NULL;
}

static bool prime_typed_relation_id_matches(
    const TermUniverse *universe, AtomId id, Atom *term) {
    return universe && id != CETTA_ATOM_ID_NONE && term &&
           term_universe_atom_id_eq(universe, id, term);
}

static bool prime_typed_relation_closed_context(
    Arena *owner, const TermUniverse *universe,
    const CettaPrimeTypedValueV1 *const *values, size_t value_count) {
    if (!owner || !universe || !values || value_count == 0u) return false;
    Atom *empty_context = atom_symbol(owner, "PrimeCtxNil");
    for (size_t index = 0u; index < value_count; index++)
        if (!values[index] || !prime_typed_relation_id_matches(
                universe, values[index]->context_id, empty_context)) {
            return false;
        }
    return true;
}

static bool prime_typed_relation_capture_current_authority(
    Space *space, const CettaPrimeTypedValueV1 *reference,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    return space && reference && token_out && space->native.universe &&
           reference->universe == space->native.universe &&
           reference->universe_instance_id ==
               space->native.universe->instance_id &&
           reference->universe_storage_epoch ==
               space->native.universe->storage_epoch &&
           cetta_prime_typing_direct_authority_token_v1(
               space, CETTA_PRIME_TYPED_FLOW_POLICY_V1, token_out) &&
           cetta_nik_direct_authority_token_v1_equal(
               &reference->authority_token, token_out);
}

static Atom *prime_typed_relation_join_sorts(
    Arena *owner, Atom *left, Atom *right) {
    if (atom_is_symbol(left, "U1") && atom_is_symbol(right, "U1"))
        return atom_symbol(owner, "U1");
    Atom *left_level = atom_is_symbol(left, "U1")
        ? atom_expr2(
              owner, atom_symbol(owner, "LevelConst"), atom_int(owner, 0))
        : prime_typed_relation_expr(left, "Sort", 2u)
            ? left->expr.elems[1]
            : NULL;
    Atom *right_level = atom_is_symbol(right, "U1")
        ? atom_expr2(
              owner, atom_symbol(owner, "LevelConst"), atom_int(owner, 0))
        : prime_typed_relation_expr(right, "Sort", 2u)
            ? right->expr.elems[1]
            : NULL;
    return left_level && right_level
        ? atom_expr2(
              owner, atom_symbol(owner, "Sort"),
              atom_expr3(
                  owner, atom_symbol(owner, "LevelMax"),
                  left_level, right_level))
        : NULL;
}

static Atom *prime_typed_relation_sort_successor(
    Arena *owner, Atom *sort) {
    Atom *level = atom_is_symbol(sort, "U1")
        ? atom_expr2(
              owner, atom_symbol(owner, "LevelConst"), atom_int(owner, 0))
        : prime_typed_relation_expr(sort, "Sort", 2u)
            ? sort->expr.elems[1]
            : NULL;
    return level
        ? atom_expr2(
              owner, atom_symbol(owner, "Sort"),
              atom_expr2(
                  owner, atom_symbol(owner, "LevelSucc"), level))
        : NULL;
}

static bool prime_typed_relation_type_term_view(
    Arena *owner, Atom *relation_type_term,
    PrimeTypedRelTypeViewV1 *view_out) {
    if (view_out) *view_out = (PrimeTypedRelTypeViewV1){0};
    if (!owner || !view_out ||
        !prime_typed_relation_expr(relation_type_term, "Pi", 3u)) {
        return false;
    }
    Atom *inner = relation_type_term->expr.elems[2];
    if (!prime_typed_relation_expr(inner, "Pi", 3u) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(
            inner->expr.elems[2])) {
        return false;
    }

    /* The second domain of `rel A B` is `B` weakened through the source
     * binder.  Removing exactly that weakening rejects a genuinely dependent
     * second domain while recovering the authored target fibre. */
    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, owner)) {
        abt_signature_free(&signature);
        return false;
    }
    Atom *target_type = abt_shift(
        &signature, owner, -1, 0u, inner->expr.elems[1]);
    abt_signature_free(&signature);
    if (!target_type) return false;

    *view_out = (PrimeTypedRelTypeViewV1){
        .source_type = relation_type_term->expr.elems[1],
        .target_type = target_type,
        .evidence_universe = inner->expr.elems[2],
    };
    return true;
}

static bool prime_typed_relation_value_view(
    Arena *owner, const TermUniverse *universe,
    const CettaPrimeTypedValueV1 *relation,
    PrimeTypedRelTypeViewV1 *view_out) {
    if (!owner || !universe || !relation ||
        relation->type_id == CETTA_ATOM_ID_NONE) {
        return false;
    }
    Atom *relation_type = prime_typed_relation_copy(
        owner, universe, relation->type_id);
    return prime_typed_relation_type_term_view(
        owner, relation_type, view_out);
}

bool cetta_prime_typed_relation_v1_view(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    CettaPrimeTypedRelationViewV1 *view_out) {
    if (view_out) {
        *view_out = (CettaPrimeTypedRelationViewV1){
            .source_type_id = CETTA_ATOM_ID_NONE,
            .target_type_id = CETTA_ATOM_ID_NONE,
            .evidence_universe_id = CETTA_ATOM_ID_NONE,
        };
    }
    const CettaPrimeTypedValueV1 *premises[] = {relation};
    if (!owner || !space || !view_out ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0]))) {
        return false;
    }

    TermUniverse *universe = space->native.universe;
    PrimeTypedRelTypeViewV1 view;
    if (!prime_typed_relation_value_view(
            owner, universe, relation, &view)) {
        return false;
    }
    AtomId source_type_id = term_universe_store_atom_id(
        universe, owner, view.source_type);
    AtomId target_type_id = term_universe_store_atom_id(
        universe, owner, view.target_type);
    AtomId evidence_universe_id = term_universe_store_atom_id(
        universe, owner, view.evidence_universe);
    if (source_type_id == CETTA_ATOM_ID_NONE ||
        target_type_id == CETTA_ATOM_ID_NONE ||
        evidence_universe_id == CETTA_ATOM_ID_NONE) {
        return false;
    }
    *view_out = (CettaPrimeTypedRelationViewV1){
        .source_type_id = source_type_id,
        .target_type_id = target_type_id,
        .evidence_universe_id = evidence_universe_id,
    };
    return true;
}

CettaPrimeTypedValueV1 *cetta_prime_typed_relation_answer_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target_type) {
    const CettaPrimeTypedValueV1 *premises[] = {
        relation, source, target_type,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    PrimeTypedRelTypeViewV1 view;
    Atom *target_type_term = NULL;
    Atom *target_type_sort = NULL;
    Atom *relation_term = NULL;
    Atom *source_term = NULL;
    if (!prime_typed_relation_value_view(
            owner, universe, relation, &view) ||
        !prime_typed_relation_id_matches(
            universe, source->type_id, view.source_type) ||
        !prime_typed_relation_id_matches(
            universe, target_type->term_id, view.target_type)) {
        return NULL;
    }
    target_type_term = prime_typed_relation_copy(
        owner, universe, target_type->term_id);
    target_type_sort = prime_typed_relation_copy(
        owner, universe, target_type->type_id);
    relation_term = prime_typed_relation_copy(
        owner, universe, relation->term_id);
    source_term = prime_typed_relation_copy(
        owner, universe, source->term_id);
    if (!target_type_term || !relation_term || !source_term ||
        !target_type_sort ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(
            target_type_sort)) {
        return NULL;
    }

    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, owner)) {
        abt_signature_free(&signature);
        return NULL;
    }
    Atom *relation_under_target = abt_shift(
        &signature, owner, 1, 0u, relation_term);
    Atom *source_under_target = abt_shift(
        &signature, owner, 1, 0u, source_term);
    abt_signature_free(&signature);
    Atom *target_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 0));
    Atom *relation_at_source = relation_under_target && source_under_target
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              relation_under_target, source_under_target)
        : NULL;
    Atom *evidence_type = relation_at_source && target_index
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              relation_at_source, target_index)
        : NULL;
    Atom *answer_type = evidence_type
        ? atom_expr3(
              owner, atom_symbol(owner, "Sigma"),
              target_type_term, evidence_type)
        : NULL;
    Atom *answer_sort = prime_typed_relation_join_sorts(
        owner, target_type_sort, view.evidence_universe);
    AtomId answer_type_id = answer_type
        ? term_universe_store_atom_id(universe, owner, answer_type)
        : CETTA_ATOM_ID_NONE;
    AtomId answer_sort_id = answer_sort
        ? term_universe_store_atom_id(universe, owner, answer_sort)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (answer_type_id == CETTA_ATOM_ID_NONE ||
        answer_sort_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, relation, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel:answer-type",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, relation->context_id,
        answer_type_id, answer_sort_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_relation_answer_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *target,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *inputs[] = {
        relation, source, target_type, target, evidence,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, inputs, sizeof(inputs) / sizeof(inputs[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, inputs,
            sizeof(inputs) / sizeof(inputs[0]))) {
        return NULL;
    }

    CettaPrimeTypedRelationViewV1 view;
    if (!cetta_prime_typed_relation_v1_view(
            owner, space, relation, &view) ||
        source->type_id != view.source_type_id ||
        target_type->term_id != view.target_type_id ||
        target->type_id != view.target_type_id) {
        return NULL;
    }
    CettaPrimeTypedValueV1 *relation_at_source =
        cetta_prime_typed_value_apply_v1(
            owner, space, relation, source);
    CettaPrimeTypedValueV1 *evidence_type = relation_at_source
        ? cetta_prime_typed_value_apply_v1(
              owner, space, relation_at_source, target)
        : NULL;
    CettaPrimeTypedValueV1 *answer_type = evidence_type
        ? cetta_prime_typed_relation_answer_type_v1(
              owner, space, relation, source, target_type)
        : NULL;
    if (!evidence_type || !answer_type ||
        evidence->type_id != evidence_type->term_id) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *target_term = prime_typed_relation_copy(
        owner, universe, target->term_id);
    Atom *evidence_term = prime_typed_relation_copy(
        owner, universe, evidence->term_id);
    Atom *answer_term = target_term && evidence_term
        ? atom_expr3(
              owner, atom_symbol(owner, "Pair"),
              target_term, evidence_term)
        : NULL;
    AtomId answer_term_id = answer_term
        ? term_universe_store_atom_id(universe, owner, answer_term)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (answer_term_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, answer_type, &token)) {
        return NULL;
    }
    const CettaPrimeTypedValueV1 *premises[] = {
        answer_type, target, evidence,
    };
    AtomId witnesses[] = {target->term_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel:answer",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, answer_type->context_id,
        answer_term_id, answer_type->term_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_rel_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *evidence_universe) {
    const CettaPrimeTypedValueV1 *premises[] = {
        source_type, target_type, evidence_universe,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *source = prime_typed_relation_copy(
        owner, universe, source_type->term_id);
    Atom *target = prime_typed_relation_copy(
        owner, universe, target_type->term_id);
    Atom *evidence = prime_typed_relation_copy(
        owner, universe, evidence_universe->term_id);
    Atom *source_sort = prime_typed_relation_copy(
        owner, universe, source_type->type_id);
    Atom *target_sort = prime_typed_relation_copy(
        owner, universe, target_type->type_id);
    Atom *evidence_sort = prime_typed_relation_copy(
        owner, universe, evidence_universe->type_id);
    if (!source || !target || !evidence || !source_sort || !target_sort ||
        !evidence_sort ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(source_sort) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(target_sort) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(evidence) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(evidence_sort)) {
        return NULL;
    }

    Atom *inner_type = atom_expr3(
        owner, atom_symbol(owner, "Pi"), target, evidence);
    Atom *relation_type = inner_type
        ? atom_expr3(owner, atom_symbol(owner, "Pi"), source, inner_type)
        : NULL;
    Atom *inner_sort = prime_typed_relation_join_sorts(
        owner, target_sort, evidence_sort);
    Atom *relation_sort = inner_sort
        ? prime_typed_relation_join_sorts(owner, source_sort, inner_sort)
        : NULL;
    AtomId relation_type_id = relation_type
        ? term_universe_store_atom_id(universe, owner, relation_type)
        : CETTA_ATOM_ID_NONE;
    AtomId relation_sort_id = relation_sort
        ? term_universe_store_atom_id(universe, owner, relation_sort)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (relation_type_id == CETTA_ATOM_ID_NONE ||
        relation_sort_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, source_type, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, source_type->context_id,
        relation_type_id, relation_sort_id, &token, &build);
}

CettaPrimeTypedValueV1 *
cetta_prime_typed_relation_chain_result_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation) {
    const CettaPrimeTypedValueV1 *premises[] = {
        source_type, middle_type, target_type,
        earlier_relation, later_relation,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *source = prime_typed_relation_copy(
        owner, universe, source_type->term_id);
    Atom *middle = prime_typed_relation_copy(
        owner, universe, middle_type->term_id);
    Atom *target = prime_typed_relation_copy(
        owner, universe, target_type->term_id);
    Atom *source_sort = prime_typed_relation_copy(
        owner, universe, source_type->type_id);
    Atom *middle_sort = prime_typed_relation_copy(
        owner, universe, middle_type->type_id);
    Atom *target_sort = prime_typed_relation_copy(
        owner, universe, target_type->type_id);
    PrimeTypedRelTypeViewV1 earlier_view;
    PrimeTypedRelTypeViewV1 later_view;
    if (!source || !middle || !target || !source_sort || !middle_sort ||
        !target_sort ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(source_sort) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(middle_sort) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(target_sort) ||
        !prime_typed_relation_value_view(
            owner, universe, earlier_relation, &earlier_view) ||
        !prime_typed_relation_value_view(
            owner, universe, later_relation, &later_view) ||
        !prime_typed_relation_id_matches(
            universe, source_type->term_id, earlier_view.source_type) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, earlier_view.target_type) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, later_view.source_type) ||
        !prime_typed_relation_id_matches(
            universe, target_type->term_id, later_view.target_type)) {
        return NULL;
    }

    Atom *premise_evidence_sort = prime_typed_relation_join_sorts(
        owner, earlier_view.evidence_universe,
        later_view.evidence_universe);
    Atom *chain_evidence_sort = premise_evidence_sort
        ? prime_typed_relation_join_sorts(
              owner, middle_sort, premise_evidence_sort)
        : NULL;
    Atom *chain_evidence_sort_type = chain_evidence_sort
        ? prime_typed_relation_sort_successor(owner, chain_evidence_sort)
        : NULL;
    Atom *inner_type = chain_evidence_sort
        ? atom_expr3(
              owner, atom_symbol(owner, "Pi"), target,
              chain_evidence_sort)
        : NULL;
    Atom *relation_type = inner_type
        ? atom_expr3(
              owner, atom_symbol(owner, "Pi"), source, inner_type)
        : NULL;
    Atom *inner_sort = chain_evidence_sort_type
        ? prime_typed_relation_join_sorts(
              owner, target_sort, chain_evidence_sort_type)
        : NULL;
    Atom *relation_sort = inner_sort
        ? prime_typed_relation_join_sorts(owner, source_sort, inner_sort)
        : NULL;
    AtomId relation_type_id = relation_type
        ? term_universe_store_atom_id(universe, owner, relation_type)
        : CETTA_ATOM_ID_NONE;
    AtomId relation_sort_id = relation_sort
        ? term_universe_store_atom_id(universe, owner, relation_sort)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (relation_type_id == CETTA_ATOM_ID_NONE ||
        relation_sort_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, source_type, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel:chain-type",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, source_type->context_id,
        relation_type_id, relation_sort_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_relation_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *result_relation_type,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation) {
    const CettaPrimeTypedValueV1 *premises[] = {
        result_relation_type, middle_type, earlier_relation, later_relation,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *result_type = prime_typed_relation_copy(
        owner, universe, result_relation_type->term_id);
    Atom *result_sort = prime_typed_relation_copy(
        owner, universe, result_relation_type->type_id);
    Atom *middle = prime_typed_relation_copy(
        owner, universe, middle_type->term_id);
    Atom *middle_sort = prime_typed_relation_copy(
        owner, universe, middle_type->type_id);
    Atom *earlier = prime_typed_relation_copy(
        owner, universe, earlier_relation->term_id);
    Atom *later = prime_typed_relation_copy(
        owner, universe, later_relation->term_id);
    PrimeTypedRelTypeViewV1 result_view;
    PrimeTypedRelTypeViewV1 earlier_view;
    PrimeTypedRelTypeViewV1 later_view;
    if (!result_type || !result_sort || !middle || !middle_sort ||
        !earlier || !later ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(result_sort) ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(middle_sort) ||
        !prime_typed_relation_type_term_view(
            owner, result_type, &result_view) ||
        !prime_typed_relation_value_view(
            owner, universe, earlier_relation, &earlier_view) ||
        !prime_typed_relation_value_view(
            owner, universe, later_relation, &later_view) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, earlier_view.target_type) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, later_view.source_type) ||
        !atom_eq(result_view.source_type, earlier_view.source_type) ||
        !atom_eq(result_view.target_type, later_view.target_type)) {
        return NULL;
    }

    Atom *premise_evidence_sort = prime_typed_relation_join_sorts(
        owner, earlier_view.evidence_universe,
        later_view.evidence_universe);
    Atom *chain_evidence_sort = premise_evidence_sort
        ? prime_typed_relation_join_sorts(
              owner, middle_sort, premise_evidence_sort)
        : NULL;
    if (!chain_evidence_sort ||
        !atom_eq(chain_evidence_sort, result_view.evidence_universe)) {
        return NULL;
    }

    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, owner)) {
        abt_signature_free(&signature);
        return NULL;
    }
    Atom *middle_under_inputs = abt_shift(
        &signature, owner, 2, 0u, middle);
    Atom *earlier_under_middle = abt_shift(
        &signature, owner, 3, 0u, earlier);
    Atom *later_under_evidence = abt_shift(
        &signature, owner, 4, 0u, later);
    abt_signature_free(&signature);
    if (!middle_under_inputs || !earlier_under_middle ||
        !later_under_evidence) {
        return NULL;
    }

    Atom *source_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 2));
    Atom *middle_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 0));
    Atom *middle_under_evidence_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 1));
    Atom *target_under_evidence_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 2));
    Atom *earlier_at_source = source_index
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              earlier_under_middle, source_index)
        : NULL;
    Atom *earlier_evidence = earlier_at_source && middle_index
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              earlier_at_source, middle_index)
        : NULL;
    Atom *later_at_middle = middle_under_evidence_index
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              later_under_evidence, middle_under_evidence_index)
        : NULL;
    Atom *later_evidence = later_at_middle && target_under_evidence_index
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              later_at_middle, target_under_evidence_index)
        : NULL;
    Atom *evidence_pair = earlier_evidence && later_evidence
        ? atom_expr3(
              owner, atom_symbol(owner, "Sigma"),
              earlier_evidence, later_evidence)
        : NULL;
    Atom *chain_fibre = evidence_pair
        ? atom_expr3(
              owner, atom_symbol(owner, "Sigma"),
              middle_under_inputs, evidence_pair)
        : NULL;
    Atom *target_lambda = chain_fibre
        ? atom_expr2(owner, atom_symbol(owner, "Lam"), chain_fibre)
        : NULL;
    Atom *chain_relation = target_lambda
        ? atom_expr2(owner, atom_symbol(owner, "Lam"), target_lambda)
        : NULL;
    AtomId chain_relation_id = chain_relation
        ? term_universe_store_atom_id(universe, owner, chain_relation)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (chain_relation_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, result_relation_type, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "rel:chain",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, result_relation_type->context_id,
        chain_relation_id, result_relation_type->term_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_chain_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target) {
    const CettaPrimeTypedValueV1 *premises[] = {
        middle_type, earlier_relation, later_relation, source, target,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *middle = prime_typed_relation_copy(
        owner, universe, middle_type->term_id);
    Atom *middle_sort = prime_typed_relation_copy(
        owner, universe, middle_type->type_id);
    if (!middle || !middle_sort ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(middle_sort)) {
        return NULL;
    }
    PrimeTypedRelTypeViewV1 earlier_view;
    PrimeTypedRelTypeViewV1 later_view;
    if (!prime_typed_relation_value_view(
            owner, universe, earlier_relation, &earlier_view) ||
        !prime_typed_relation_value_view(
            owner, universe, later_relation, &later_view) ||
        !prime_typed_relation_id_matches(
            universe, source->type_id, earlier_view.source_type) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, earlier_view.target_type) ||
        !prime_typed_relation_id_matches(
            universe, middle_type->term_id, later_view.source_type) ||
        !prime_typed_relation_id_matches(
            universe, target->type_id, later_view.target_type)) {
        return NULL;
    }

    Atom *earlier = prime_typed_relation_copy(
        owner, universe, earlier_relation->term_id);
    Atom *later = prime_typed_relation_copy(
        owner, universe, later_relation->term_id);
    Atom *source_term = prime_typed_relation_copy(
        owner, universe, source->term_id);
    Atom *target_term = prime_typed_relation_copy(
        owner, universe, target->term_id);
    Atom *middle_index = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 0));
    Atom *middle_under_evidence = atom_expr2(
        owner, atom_symbol(owner, "idx"), atom_int(owner, 1));
    if (!earlier || !later || !source_term || !target_term ||
        !middle_index || !middle_under_evidence) {
        return NULL;
    }
    Atom *earlier_at_source = atom_expr3(
        owner, atom_symbol(owner, "App"), earlier, source_term);
    Atom *earlier_evidence_type = earlier_at_source
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              earlier_at_source, middle_index)
        : NULL;
    Atom *later_at_middle = atom_expr3(
        owner, atom_symbol(owner, "App"), later,
        middle_under_evidence);
    Atom *later_evidence_type = later_at_middle
        ? atom_expr3(
              owner, atom_symbol(owner, "App"),
              later_at_middle, target_term)
        : NULL;
    Atom *evidence_pair_type =
        earlier_evidence_type && later_evidence_type
        ? atom_expr3(
              owner, atom_symbol(owner, "Sigma"),
              earlier_evidence_type, later_evidence_type)
        : NULL;
    Atom *chain_type = evidence_pair_type
        ? atom_expr3(
              owner, atom_symbol(owner, "Sigma"), middle,
              evidence_pair_type)
        : NULL;
    Atom *evidence_pair_sort = prime_typed_relation_join_sorts(
        owner, earlier_view.evidence_universe,
        later_view.evidence_universe);
    Atom *chain_sort = evidence_pair_sort
        ? prime_typed_relation_join_sorts(
              owner, middle_sort, evidence_pair_sort)
        : NULL;
    AtomId chain_type_id = chain_type
        ? term_universe_store_atom_id(universe, owner, chain_type)
        : CETTA_ATOM_ID_NONE;
    AtomId chain_sort_id = chain_sort
        ? term_universe_store_atom_id(universe, owner, chain_sort)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (chain_type_id == CETTA_ATOM_ID_NONE ||
        chain_sort_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, middle_type, &token)) {
        return NULL;
    }

    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "chain:type",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, middle_type->context_id,
        chain_type_id, chain_sort_id, &token, &build);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *chain_type,
    const CettaPrimeTypedValueV1 *middle,
    const CettaPrimeTypedValueV1 *earlier_evidence,
    const CettaPrimeTypedValueV1 *later_evidence) {
    const CettaPrimeTypedValueV1 *premises[] = {
        chain_type, middle, earlier_evidence, later_evidence,
    };
    if (!owner || !space ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0])) ||
        !prime_typed_relation_closed_context(
            owner, space->native.universe, premises,
            sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *chain_type_term = prime_typed_relation_copy(
        owner, universe, chain_type->term_id);
    Atom *middle_term = prime_typed_relation_copy(
        owner, universe, middle->term_id);
    Atom *earlier_term = prime_typed_relation_copy(
        owner, universe, earlier_evidence->term_id);
    Atom *later_term = prime_typed_relation_copy(
        owner, universe, later_evidence->term_id);
    if (!prime_typed_relation_expr(chain_type_term, "Sigma", 3u) ||
        !middle_term || !earlier_term || !later_term ||
        !prime_typed_relation_id_matches(
            universe, middle->type_id, chain_type_term->expr.elems[1])) {
        return NULL;
    }

    AbtSignature signature;
    abt_signature_init(&signature);
    if (!abt_signature_add_defaults(&signature, owner)) {
        abt_signature_free(&signature);
        return NULL;
    }
    Atom *inner_type = abt_subst(
        &signature, owner, 0u, middle_term,
        chain_type_term->expr.elems[2]);
    if (!prime_typed_relation_expr(inner_type, "Sigma", 3u) ||
        !prime_typed_relation_id_matches(
            universe, earlier_evidence->type_id,
            inner_type->expr.elems[1])) {
        abt_signature_free(&signature);
        return NULL;
    }
    Atom *final_type = abt_subst(
        &signature, owner, 0u, earlier_term, inner_type->expr.elems[2]);
    bool final_type_matches = prime_typed_relation_id_matches(
        universe, later_evidence->type_id, final_type);
    abt_signature_free(&signature);
    if (!final_type_matches) return NULL;

    Atom *evidence_pair = atom_expr3(
        owner, atom_symbol(owner, "Pair"), earlier_term, later_term);
    Atom *chain_term = evidence_pair
        ? atom_expr3(
              owner, atom_symbol(owner, "Pair"), middle_term,
              evidence_pair)
        : NULL;
    AtomId chain_term_id = chain_term
        ? term_universe_store_atom_id(universe, owner, chain_term)
        : CETTA_ATOM_ID_NONE;
    CettaNikDirectAuthorityTokenV1 token;
    if (chain_term_id == CETTA_ATOM_ID_NONE ||
        !prime_typed_relation_capture_current_authority(
            space, chain_type, &token)) {
        return NULL;
    }

    AtomId witnesses[] = {middle->term_id};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "chain",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, chain_type->context_id,
        chain_term_id, chain_type->term_id, &token, &build);
}
