#include "prime_typed_list.h"

#include "prime_typed_flow_private.h"

static CettaPrimeTypedValueV1 *prime_typed_list_attach(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *constructor_name, size_t constructor_arity) {
    if (!cetta_prime_typed_value_has_application_head_private_v1(
            owner, space, value, constructor_name, constructor_arity)) {
        return NULL;
    }
    return cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, value, "list", 1u, 0u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type) {
    return prime_typed_list_attach(
        owner, space,
        cetta_prime_typed_value_apply_converting_v1(
            owner, space, nil_rule, element_type),
        "list:nil", 1u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail) {
    CettaPrimeTypedValueV1 *specialized =
        cetta_prime_typed_value_apply_converting_v1(
            owner, space, cons_rule, element_type);
    const CettaPrimeTypedValueV1 *arguments[] = {head, tail};
    return prime_typed_list_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "list:cons", 3u);
}

static bool prime_typed_list_spine_length(
    Atom *term, Atom *element_type, size_t *length_out) {
    if (!term || !element_type || !length_out) return false;
    size_t length = 0u;
    Atom *cursor = term;
    for (;;) {
        Atom *nil_arguments[1] = {0};
        if (cetta_prime_typed_application_spine_private_v1(
                cursor, "list:nil", nil_arguments, 1u)) {
            if (!atom_eq(nil_arguments[0], element_type)) return false;
            *length_out = length;
            return true;
        }
        Atom *cons_arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", cons_arguments, 3u) ||
            !atom_eq(cons_arguments[0], element_type) ||
            length == SIZE_MAX) {
            return false;
        }
        length++;
        cursor = cons_arguments[2];
    }
}

static bool prime_typed_list_index(Atom *term, uint64_t expected) {
    return term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
           atom_is_symbol(term->expr.elems[0], "idx") &&
           term->expr.elems[1] &&
           term->expr.elems[1]->kind == ATOM_GROUNDED &&
           term->expr.elems[1]->ground.gkind == GV_INT &&
           term->expr.elems[1]->ground.ival >= 0 &&
           (uint64_t)term->expr.elems[1]->ground.ival == expected;
}

static bool prime_typed_list_lambda_body(Atom *term, Atom **body_out) {
    if (body_out) *body_out = NULL;
    if (!term || !body_out || term->kind != ATOM_EXPR ||
        term->expr.len != 2u ||
        !atom_is_symbol(term->expr.elems[0], "Lam")) {
        return false;
    }
    *body_out = term->expr.elems[1];
    return true;
}

/* Exact intrinsic image of the theorem-side definition
 *
 *   fun A B f xs =>
 *     list:eliminate A (fun _ => list B) (list:nil B)
 *       (fun head _ induction => list:cons B (f head) induction) xs
 *
 * after lexical names have been lowered to de Bruijn indices. */
static bool prime_typed_list_is_map_program(Atom *term) {
    Atom *body = term;
    for (size_t binder = 0u; binder < 4u; binder++)
        if (!prime_typed_list_lambda_body(body, &body)) return false;

    Atom *eliminate_arguments[5] = {0};
    if (!cetta_prime_typed_application_spine_private_v1(
            body, "list:eliminate", eliminate_arguments, 5u) ||
        !prime_typed_list_index(eliminate_arguments[0], 3u) ||
        !prime_typed_list_index(eliminate_arguments[4], 0u)) {
        return false;
    }

    Atom *motive_body = NULL;
    Atom *motive_list_arguments[1] = {0};
    if (!prime_typed_list_lambda_body(
            eliminate_arguments[1], &motive_body) ||
        !cetta_prime_typed_application_spine_private_v1(
            motive_body, "list", motive_list_arguments, 1u) ||
        !prime_typed_list_index(motive_list_arguments[0], 3u)) {
        return false;
    }

    Atom *nil_arguments[1] = {0};
    if (!cetta_prime_typed_application_spine_private_v1(
            eliminate_arguments[2], "list:nil", nil_arguments, 1u) ||
        !prime_typed_list_index(nil_arguments[0], 2u)) {
        return false;
    }

    Atom *cons_body = eliminate_arguments[3];
    for (size_t binder = 0u; binder < 3u; binder++)
        if (!prime_typed_list_lambda_body(cons_body, &cons_body)) return false;
    Atom *cons_arguments[3] = {0};
    if (!cetta_prime_typed_application_spine_private_v1(
            cons_body, "list:cons", cons_arguments, 3u) ||
        !prime_typed_list_index(cons_arguments[0], 5u) ||
        !prime_typed_list_index(cons_arguments[2], 0u)) {
        return false;
    }
    Atom *mapped_head = cons_arguments[1];
    return mapped_head && mapped_head->kind == ATOM_EXPR &&
           mapped_head->expr.len == 3u &&
           atom_is_symbol(mapped_head->expr.elems[0], "App") &&
           prime_typed_list_index(mapped_head->expr.elems[1], 4u) &&
           prime_typed_list_index(mapped_head->expr.elems[2], 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_eliminate_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *eliminate_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *motive,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *list) {
    const CettaPrimeTypedValueV1 *inputs[] = {
        eliminate_rule, element_type, motive, nil_case, cons_case, list,
    };
    if (!owner || !space || !space->native.universe ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, inputs, sizeof(inputs) / sizeof(inputs[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    if (!term_universe_atom_id_eq(
            universe, eliminate_rule->term_id,
            atom_symbol(owner, "list:eliminate"))) {
        return NULL;
    }

    CettaPrimeTypedIndexedViewV1 indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(list, &indexed) ||
        indexed.parameter_count != 1u || indexed.index_count != 0u ||
        indexed.parameter_ids[0] != element_type->term_id ||
        !term_universe_atom_id_eq(
            universe, indexed.family_head_id,
            atom_symbol(owner, "list"))) {
        return NULL;
    }

    Atom *element_term = term_universe_copy_atom(
        universe, owner, element_type->term_id);
    Atom *list_term = term_universe_copy_atom(
        universe, owner, list->term_id);
    size_t length = 0u;
    if (!element_term || !list_term ||
        !prime_typed_list_spine_length(
            list_term, element_term, &length) ||
        length > SIZE_MAX / sizeof(Atom *) ||
        length > (SIZE_MAX - 4u) / 2u) {
        return NULL;
    }

    Atom **heads = length == 0u ? NULL :
        arena_alloc(owner, length * sizeof(*heads));
    Atom **tails = length == 0u ? NULL :
        arena_alloc(owner, length * sizeof(*tails));
    size_t witness_count = 2u * length + 4u;
    if (witness_count > SIZE_MAX / sizeof(AtomId)) return NULL;
    AtomId *witness_ids = arena_alloc(
        owner, witness_count * sizeof(*witness_ids));

    Atom *cursor = list_term;
    for (size_t index = 0u; index < length; index++) {
        Atom *arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", arguments, 3u) ||
            !atom_eq(arguments[0], element_term)) {
            return NULL;
        }
        heads[index] = arguments[1];
        tails[index] = arguments[2];
        cursor = arguments[2];
    }
    Atom *nil_arguments[1] = {0};
    if (!cetta_prime_typed_application_spine_private_v1(
            cursor, "list:nil", nil_arguments, 1u) ||
        !atom_eq(nil_arguments[0], element_term)) {
        return NULL;
    }

    const CettaPrimeTypedValueV1 *redex_arguments[] = {
        element_type, motive, nil_case, cons_case, list,
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
        size_t index = offset - 1u;
        Atom *arguments[] = {
            heads[index], tails[index], reduct_term,
        };
        reduct_term = cetta_prime_typed_application_term_private_v1(
            owner, cons_case_term, arguments,
            sizeof(arguments) / sizeof(arguments[0]));
        if (!reduct_term) return NULL;
    }

    size_t witness_index = 0u;
    witness_ids[witness_index++] = list->term_id;
    witness_ids[witness_index++] = term_universe_store_atom_id(
        universe, owner, cursor);
    witness_ids[witness_index++] = nil_case->term_id;
    for (size_t index = 0u; index < length; index++) {
        witness_ids[witness_index++] = term_universe_store_atom_id(
            universe, owner, heads[index]);
        witness_ids[witness_index++] = term_universe_store_atom_id(
            universe, owner, tails[index]);
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
        element_type, motive, nil_case, cons_case, list,
    };
    CettaPrimeTypedValueBuildPrivateV1 fold_build = {
        .rule_name = "list:fold",
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
        redex->term_id, reduct->term_id, list->term_id,
    };
    return cetta_prime_typed_value_compute_private_v1(
        owner, space, "list:iota-fold", redex, reduct,
        iota_witnesses,
        sizeof(iota_witnesses) / sizeof(iota_witnesses[0]));
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *map_program,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *list) {
    const CettaPrimeTypedValueV1 *inputs[] = {
        map_program, source_type, target_type, function, list,
    };
    if (!owner || !space || !space->native.universe ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, inputs, sizeof(inputs) / sizeof(inputs[0]))) {
        return NULL;
    }

    TermUniverse *universe = space->native.universe;
    Atom *program_term = term_universe_copy_atom(
        universe, owner, map_program->term_id);
    if (!program_term || !prime_typed_list_is_map_program(program_term))
        return NULL;

    CettaPrimeTypedIndexedViewV1 indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(list, &indexed) ||
        indexed.parameter_count != 1u || indexed.index_count != 0u ||
        indexed.parameter_ids[0] != source_type->term_id ||
        !term_universe_atom_id_eq(
            universe, indexed.family_head_id,
            atom_symbol(owner, "list"))) {
        return NULL;
    }

    const CettaPrimeTypedValueV1 *redex_arguments[] = {
        source_type, target_type, function, list,
    };
    CettaPrimeTypedValueV1 *redex =
        cetta_prime_typed_value_apply_many_v1(
            owner, space, map_program, redex_arguments,
            sizeof(redex_arguments) / sizeof(redex_arguments[0]));
    Atom *redex_type = redex ? term_universe_copy_atom(
        universe, owner, redex->type_id) : NULL;
    Atom *result_type_arguments[1] = {0};
    if (!redex_type ||
        !cetta_prime_typed_application_spine_private_v1(
            redex_type, "list", result_type_arguments, 1u) ||
        !term_universe_atom_id_eq(
            universe, target_type->term_id, result_type_arguments[0])) {
        return NULL;
    }

    Atom *source_term = term_universe_copy_atom(
        universe, owner, source_type->term_id);
    Atom *target_term = term_universe_copy_atom(
        universe, owner, target_type->term_id);
    Atom *function_term = term_universe_copy_atom(
        universe, owner, function->term_id);
    Atom *list_term = term_universe_copy_atom(
        universe, owner, list->term_id);
    size_t length = 0u;
    if (!source_term || !target_term || !function_term || !list_term ||
        !prime_typed_list_spine_length(list_term, source_term, &length) ||
        length > SIZE_MAX / sizeof(Atom *) ||
        length > (SIZE_MAX - 5u) / 2u) {
        return NULL;
    }

    Atom **heads = length == 0u ? NULL :
        arena_alloc(owner, length * sizeof(*heads));
    Atom **tails = length == 0u ? NULL :
        arena_alloc(owner, length * sizeof(*tails));
    Atom *cursor = list_term;
    for (size_t index = 0u; index < length; index++) {
        Atom *arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", arguments, 3u) ||
            !atom_eq(arguments[0], source_term)) {
            return NULL;
        }
        heads[index] = arguments[1];
        tails[index] = arguments[2];
        cursor = arguments[2];
    }

    Atom *nil_arguments[] = {target_term};
    Atom *reduct_term = cetta_prime_typed_application_term_private_v1(
        owner, atom_symbol(owner, "list:nil"), nil_arguments,
        sizeof(nil_arguments) / sizeof(nil_arguments[0]));
    for (size_t offset = length; reduct_term && offset > 0u; offset--) {
        size_t index = offset - 1u;
        Atom *mapped_head = atom_expr3(
            owner, atom_symbol(owner, "App"), function_term, heads[index]);
        Atom *cons_arguments[] = {target_term, mapped_head, reduct_term};
        reduct_term = cetta_prime_typed_application_term_private_v1(
            owner, atom_symbol(owner, "list:cons"), cons_arguments,
            sizeof(cons_arguments) / sizeof(cons_arguments[0]));
    }
    if (!reduct_term) return NULL;

    size_t witness_count = 2u * length + 5u;
    if (witness_count > SIZE_MAX / sizeof(AtomId)) return NULL;
    AtomId *witness_ids = arena_alloc(
        owner, witness_count * sizeof(*witness_ids));
    size_t witness_index = 0u;
    witness_ids[witness_index++] = map_program->term_id;
    witness_ids[witness_index++] = function->term_id;
    witness_ids[witness_index++] = list->term_id;
    witness_ids[witness_index++] = term_universe_store_atom_id(
        universe, owner, cursor);
    for (size_t index = 0u; index < length; index++) {
        witness_ids[witness_index++] = term_universe_store_atom_id(
            universe, owner, heads[index]);
        witness_ids[witness_index++] = term_universe_store_atom_id(
            universe, owner, tails[index]);
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

    AtomId family_head_id = term_universe_store_atom_id(
        universe, owner, atom_symbol(owner, "list"));
    AtomId parameters[] = {target_type->term_id};
    CettaPrimeTypedValueBuildPrivateV1 fold_build = {
        .rule_name = "list:map-fold",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = inputs,
        .premise_count = sizeof(inputs) / sizeof(inputs[0]),
        .witness_ids = witness_ids,
        .witness_count = witness_count,
        .family_head_id = family_head_id,
        .parameter_ids = parameters,
        .parameter_count = sizeof(parameters) / sizeof(parameters[0]),
    };
    if (family_head_id == CETTA_ATOM_ID_NONE) return NULL;
    CettaPrimeTypedValueV1 *reduct =
        cetta_prime_typed_value_allocate_private_v1(
            owner, universe, redex->context_id,
            reduct_id, redex->type_id, &redex->authority_token,
            &fold_build);
    if (!reduct) return NULL;

    AtomId computation_witnesses[] = {
        map_program->term_id, redex->term_id, reduct->term_id,
    };
    return cetta_prime_typed_value_compute_private_v1(
        owner, space, "list:map-fusion", redex, reduct,
        computation_witnesses,
        sizeof(computation_witnesses) /
            sizeof(computation_witnesses[0]));
}

bool cetta_prime_typed_list_runtime_representation_v1(
    Arena *destination, Space *space,
    const CettaPrimeTypedValueV1 *list,
    Atom **representation_out) {
    if (representation_out) *representation_out = NULL;
    if (!destination || !space || !space->native.universe || !list ||
        !representation_out ||
        !cetta_prime_typed_value_v1_is_current(list, space)) {
        return false;
    }

    CettaPrimeTypedIndexedViewV1 indexed = {0};
    if (!cetta_prime_typed_value_v1_indexed_view(list, &indexed) ||
        indexed.parameter_count != 1u || indexed.index_count != 0u ||
        !term_universe_atom_id_eq(
            space->native.universe, indexed.family_head_id,
            atom_symbol(destination, "list"))) {
        return false;
    }

    Atom *term = NULL;
    Atom *type = NULL;
    Atom *element_type = term_universe_copy_atom(
        space->native.universe, destination, indexed.parameter_ids[0]);
    if (!element_type || !cetta_prime_typed_value_v1_erase(
            list, space->native.universe, destination, &term, &type)) {
        return false;
    }
    (void)type;

    size_t length = 0u;
    if (!prime_typed_list_spine_length(term, element_type, &length) ||
        length > UINT64_MAX ||
        length > SIZE_MAX / sizeof(Atom *)) {
        return false;
    }
    Atom **elements = length == 0u ? NULL :
        arena_alloc(destination, length * sizeof(*elements));
    Atom *cursor = term;
    for (size_t index = 0u; index < length; index++) {
        Atom *arguments[3] = {0};
        if (!cetta_prime_typed_application_spine_private_v1(
                cursor, "list:cons", arguments, 3u)) {
            return false;
        }
        elements[index] = arguments[1];
        cursor = arguments[2];
    }
    *representation_out = atom_expr(
        destination, elements, (CettaExprLen)length);
    return *representation_out != NULL;
}
