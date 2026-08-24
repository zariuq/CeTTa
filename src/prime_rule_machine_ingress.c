#include "prime_rule_machine_ingress.h"

#include "prime_typed_flow_private.h"
#include "prime_typed_iteration.h"
#include "prime_typed_list_relations.h"

#include <string.h>

enum {
    PRIME_RULE_MACHINE_NATIVE_MAX_DEPTH = 4096,
};

typedef enum {
    PRIME_RULE_MACHINE_NATIVE_DECLINED = 0,
    PRIME_RULE_MACHINE_NATIVE_BUILT,
    PRIME_RULE_MACHINE_NATIVE_FAULT,
} PrimeRuleMachineNativeBuildV1;

static bool prime_rule_machine_head(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static bool prime_rule_machine_raw_spine(
    Atom *term, const char *head_name,
    Atom **arguments, size_t argument_count) {
    if (!term || !head_name || !arguments || argument_count == 0u)
        return false;
    if (term->kind == ATOM_EXPR &&
        term->expr.len == argument_count + 1u &&
        atom_is_symbol(term->expr.elems[0], head_name)) {
        for (size_t index = 0u; index < argument_count; index++)
            arguments[index] = term->expr.elems[index + 1u];
        return true;
    }
    return cetta_prime_typed_application_spine_private_v1(
        term, head_name, arguments, argument_count);
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_at_depth(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out,
    bool *contains_native_constructor_out);

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_import_leaf(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypedValueV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!owner || !space || !term || !value_out)
        return PRIME_RULE_MACHINE_NATIVE_FAULT;
    CettaPrimeTypingSynthesisObservationV1 observation;
    if (!cetta_prime_typed_value_import_term_v1(
            owner, space, term, steps_limited, steps,
            &observation, value_out)) {
        return PRIME_RULE_MACHINE_NATIVE_FAULT;
    }
    return *value_out
        ? PRIME_RULE_MACHINE_NATIVE_BUILT
        : PRIME_RULE_MACHINE_NATIVE_DECLINED;
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_or_import(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out,
    bool *contains_native_constructor_out) {
    if (value_out) *value_out = NULL;
    if (contains_native_constructor_out)
        *contains_native_constructor_out = false;
    if (!value_out || !contains_native_constructor_out)
        return PRIME_RULE_MACHINE_NATIVE_FAULT;
    PrimeRuleMachineNativeBuildV1 built =
        prime_rule_machine_native_construct_at_depth(
            owner, space, term, steps_limited, steps, depth,
            value_out, contains_native_constructor_out);
    if (built != PRIME_RULE_MACHINE_NATIVE_DECLINED) return built;
    *contains_native_constructor_out = false;
    return prime_rule_machine_native_import_leaf(
        owner, space, term, steps_limited, steps, value_out);
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_list_relation(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out) {
    typedef enum {
        PRIME_LIST_RELATION_ALL_NIL = 0,
        PRIME_LIST_RELATION_ALL_CONS,
        PRIME_LIST_RELATION_MEMBER_HERE,
        PRIME_LIST_RELATION_MEMBER_THERE,
        PRIME_LIST_RELATION_ANY_HERE,
        PRIME_LIST_RELATION_ANY_THERE,
        PRIME_LIST_RELATION_CASE_NIL,
        PRIME_LIST_RELATION_CASE_CONS,
        PRIME_LIST_RELATION_FOLD_NIL,
        PRIME_LIST_RELATION_FOLD_CONS,
    } PrimeListRelationConstructorV1;
    typedef struct {
        PrimeListRelationConstructorV1 constructor;
        const char *name;
        size_t argument_count;
    } PrimeListRelationConstructorSpecV1;
    static const PrimeListRelationConstructorSpecV1 constructors[] = {
        {PRIME_LIST_RELATION_ALL_NIL, "rel:all:nil", 2u},
        {PRIME_LIST_RELATION_ALL_CONS, "rel:all:cons", 6u},
        {PRIME_LIST_RELATION_MEMBER_HERE,
         "rel:list:member-here", 3u},
        {PRIME_LIST_RELATION_MEMBER_THERE,
         "rel:list:member-there", 5u},
        {PRIME_LIST_RELATION_ANY_HERE, "rel:any:here", 6u},
        {PRIME_LIST_RELATION_ANY_THERE, "rel:any:there", 7u},
        {PRIME_LIST_RELATION_CASE_NIL, "rel:case-list:nil", 6u},
        {PRIME_LIST_RELATION_CASE_CONS, "rel:case-list:cons", 8u},
        {PRIME_LIST_RELATION_FOLD_NIL, "rel:fold:nil", 4u},
        {PRIME_LIST_RELATION_FOLD_CONS, "rel:fold:cons", 10u},
    };
    enum { PRIME_LIST_RELATION_MAX_ARGUMENTS = 10 };

    const PrimeListRelationConstructorSpecV1 *matched = NULL;
    Atom *arguments[PRIME_LIST_RELATION_MAX_ARGUMENTS] = {0};
    for (size_t index = 0u;
         index < sizeof(constructors) / sizeof(constructors[0]); index++) {
        const PrimeListRelationConstructorSpecV1 *candidate =
            &constructors[index];
        if (prime_rule_machine_raw_spine(
                term, candidate->name, arguments,
                candidate->argument_count)) {
            matched = candidate;
            break;
        }
    }
    if (!matched) return PRIME_RULE_MACHINE_NATIVE_DECLINED;

    CettaPrimeTypedValueV1 *rule = NULL;
    PrimeRuleMachineNativeBuildV1 imported =
        prime_rule_machine_native_import_leaf(
            owner, space, atom_symbol(owner, matched->name),
            steps_limited, steps, &rule);
    if (imported != PRIME_RULE_MACHINE_NATIVE_BUILT) return imported;

    CettaPrimeTypedValueV1 *typed[PRIME_LIST_RELATION_MAX_ARGUMENTS] = {0};
    for (size_t index = 0u; index < matched->argument_count; index++) {
        bool contains_native_constructor = false;
        imported = prime_rule_machine_native_construct_or_import(
            owner, space, arguments[index], steps_limited, steps,
            depth + 1u, &typed[index], &contains_native_constructor);
        if (imported != PRIME_RULE_MACHINE_NATIVE_BUILT) return imported;
    }

    switch (matched->constructor) {
    case PRIME_LIST_RELATION_ALL_NIL:
        *value_out = cetta_prime_typed_list_all_nil_v1(
            owner, space, rule, typed[0], typed[1]);
        break;
    case PRIME_LIST_RELATION_ALL_CONS:
        *value_out = cetta_prime_typed_list_all_cons_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5]);
        break;
    case PRIME_LIST_RELATION_MEMBER_HERE:
        *value_out = cetta_prime_typed_list_member_here_v1(
            owner, space, rule, typed[0], typed[1], typed[2]);
        break;
    case PRIME_LIST_RELATION_MEMBER_THERE:
        *value_out = cetta_prime_typed_list_member_there_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4]);
        break;
    case PRIME_LIST_RELATION_ANY_HERE:
        *value_out = cetta_prime_typed_list_any_here_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5]);
        break;
    case PRIME_LIST_RELATION_ANY_THERE:
        *value_out = cetta_prime_typed_list_any_there_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5], typed[6]);
        break;
    case PRIME_LIST_RELATION_CASE_NIL:
        *value_out = cetta_prime_typed_list_case_nil_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5]);
        break;
    case PRIME_LIST_RELATION_CASE_CONS:
        *value_out = cetta_prime_typed_list_case_cons_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5], typed[6], typed[7]);
        break;
    case PRIME_LIST_RELATION_FOLD_NIL:
        *value_out = cetta_prime_typed_list_fold_nil_v1(
            owner, space, rule, typed[0], typed[1], typed[2], typed[3]);
        break;
    case PRIME_LIST_RELATION_FOLD_CONS:
        *value_out = cetta_prime_typed_list_fold_cons_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5], typed[6], typed[7],
            typed[8], typed[9]);
        break;
    }
    return *value_out
        ? PRIME_RULE_MACHINE_NATIVE_BUILT
        : PRIME_RULE_MACHINE_NATIVE_DECLINED;
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_iteration(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out) {
    typedef enum {
        PRIME_ITERATION_ZERO = 0,
        PRIME_ITERATION_STEP,
    } PrimeIterationConstructorV1;
    typedef struct {
        PrimeIterationConstructorV1 constructor;
        const char *name;
        size_t argument_count;
    } PrimeIterationConstructorSpecV1;
    static const PrimeIterationConstructorSpecV1 constructors[] = {
        {PRIME_ITERATION_ZERO, "rel:iterate:zero", 6u},
        {PRIME_ITERATION_STEP, "rel:iterate:step", 13u},
    };
    enum { PRIME_ITERATION_MAX_ARGUMENTS = 13 };

    const PrimeIterationConstructorSpecV1 *matched = NULL;
    Atom *arguments[PRIME_ITERATION_MAX_ARGUMENTS] = {0};
    for (size_t index = 0u;
         index < sizeof(constructors) / sizeof(constructors[0]); index++) {
        const PrimeIterationConstructorSpecV1 *candidate =
            &constructors[index];
        if (prime_rule_machine_raw_spine(
                term, candidate->name, arguments,
                candidate->argument_count)) {
            matched = candidate;
            break;
        }
    }
    if (!matched) return PRIME_RULE_MACHINE_NATIVE_DECLINED;

    CettaPrimeTypedValueV1 *rule = NULL;
    PrimeRuleMachineNativeBuildV1 imported =
        prime_rule_machine_native_import_leaf(
            owner, space, atom_symbol(owner, matched->name),
            steps_limited, steps, &rule);
    if (imported != PRIME_RULE_MACHINE_NATIVE_BUILT) return imported;

    CettaPrimeTypedValueV1 *typed[PRIME_ITERATION_MAX_ARGUMENTS] = {0};
    for (size_t index = 0u; index < matched->argument_count; index++) {
        bool contains_native_constructor = false;
        imported = prime_rule_machine_native_construct_or_import(
            owner, space, arguments[index], steps_limited, steps,
            depth + 1u, &typed[index], &contains_native_constructor);
        if (imported != PRIME_RULE_MACHINE_NATIVE_BUILT) return imported;
    }

    switch (matched->constructor) {
    case PRIME_ITERATION_ZERO:
        *value_out = cetta_prime_typed_iteration_zero_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5]);
        break;
    case PRIME_ITERATION_STEP:
        *value_out = cetta_prime_typed_iteration_step_v1(
            owner, space, rule, typed[0], typed[1], typed[2],
            typed[3], typed[4], typed[5], typed[6], typed[7],
            typed[8], typed[9], typed[10], typed[11], typed[12]);
        break;
    }
    return *value_out
        ? PRIME_RULE_MACHINE_NATIVE_BUILT
        : PRIME_RULE_MACHINE_NATIVE_DECLINED;
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_application(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out,
    bool *contains_native_constructor_out) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u ||
        !value_out || !contains_native_constructor_out) {
        return PRIME_RULE_MACHINE_NATIVE_DECLINED;
    }

    Atom *function_term = NULL;
    Atom **arguments = NULL;
    size_t argument_count = 0u;
    if (term->expr.len == 3u &&
        atom_is_symbol(term->expr.elems[0], "App")) {
        function_term = term->expr.elems[1];
        arguments = &term->expr.elems[2];
        argument_count = 1u;
    } else if (term->expr.elems[0]->kind == ATOM_SYMBOL) {
        function_term = term->expr.elems[0];
        arguments = &term->expr.elems[1];
        argument_count = (size_t)term->expr.len - 1u;
    } else {
        return PRIME_RULE_MACHINE_NATIVE_DECLINED;
    }

    bool contains_native_constructor = false;
    CettaPrimeTypedValueV1 *current = NULL;
    PrimeRuleMachineNativeBuildV1 built =
        prime_rule_machine_native_construct_or_import(
            owner, space, function_term, steps_limited, steps,
            depth + 1u, &current, &contains_native_constructor);
    if (built != PRIME_RULE_MACHINE_NATIVE_BUILT) return built;
    for (size_t index = 0u; index < argument_count; index++) {
        CettaPrimeTypedValueV1 *argument = NULL;
        bool argument_contains_native_constructor = false;
        built = prime_rule_machine_native_construct_or_import(
            owner, space, arguments[index], steps_limited, steps,
            depth + 1u, &argument,
            &argument_contains_native_constructor);
        if (built != PRIME_RULE_MACHINE_NATIVE_BUILT) return built;
        current = cetta_prime_typed_value_apply_converting_v1(
            owner, space, current, argument);
        if (!current) return PRIME_RULE_MACHINE_NATIVE_DECLINED;
        contains_native_constructor = contains_native_constructor ||
            argument_contains_native_constructor;
    }
    *value_out = current;
    *contains_native_constructor_out = contains_native_constructor;
    return PRIME_RULE_MACHINE_NATIVE_BUILT;
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct_at_depth(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps, uint32_t depth,
    CettaPrimeTypedValueV1 **value_out,
    bool *contains_native_constructor_out) {
    if (value_out) *value_out = NULL;
    if (contains_native_constructor_out)
        *contains_native_constructor_out = false;
    if (!owner || !space || !term || !value_out ||
        !contains_native_constructor_out ||
        depth > PRIME_RULE_MACHINE_NATIVE_MAX_DEPTH || atom_has_vars(term)) {
        return PRIME_RULE_MACHINE_NATIVE_DECLINED;
    }

    PrimeRuleMachineNativeBuildV1 built =
        prime_rule_machine_native_construct_list_relation(
            owner, space, term, steps_limited, steps, depth, value_out);
    if (built == PRIME_RULE_MACHINE_NATIVE_BUILT) {
        *contains_native_constructor_out = true;
        return built;
    }
    if (built == PRIME_RULE_MACHINE_NATIVE_FAULT) return built;

    built = prime_rule_machine_native_construct_iteration(
        owner, space, term, steps_limited, steps, depth, value_out);
    if (built == PRIME_RULE_MACHINE_NATIVE_BUILT) {
        *contains_native_constructor_out = true;
        return built;
    }
    if (built == PRIME_RULE_MACHINE_NATIVE_FAULT) return built;

    return prime_rule_machine_native_construct_application(
        owner, space, term, steps_limited, steps, depth,
        value_out, contains_native_constructor_out);
}

static PrimeRuleMachineNativeBuildV1
prime_rule_machine_native_construct(
    Arena *owner, Space *space, Atom *term,
    const CettaPrimeTypedValueV1 *expected_type,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypedValueV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!owner || !space || !term || !expected_type || !value_out ||
        !cetta_prime_typed_value_v1_is_current(expected_type, space)) {
        return PRIME_RULE_MACHINE_NATIVE_FAULT;
    }
    bool contains_native_constructor = false;
    CettaPrimeTypedValueV1 *value = NULL;
    PrimeRuleMachineNativeBuildV1 built =
        prime_rule_machine_native_construct_at_depth(
            owner, space, term, steps_limited, steps, 0u,
            &value, &contains_native_constructor);
    if (built != PRIME_RULE_MACHINE_NATIVE_BUILT) return built;
    if (!contains_native_constructor || !value ||
        value->type_id != expected_type->term_id) {
        return PRIME_RULE_MACHINE_NATIVE_DECLINED;
    }
    *value_out = value;
    return PRIME_RULE_MACHINE_NATIVE_BUILT;
}

static bool prime_rule_machine_result_view(
    Atom *run_result,
    CettaPrimeRuleMachineRunCompletionV1 *completion_out,
    Atom **reason_out, Atom **occurrences_out,
    Atom **metrics_out, Atom **revision_out) {
    if (completion_out)
        *completion_out = CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1;
    if (reason_out) *reason_out = NULL;
    if (occurrences_out) *occurrences_out = NULL;
    if (metrics_out) *metrics_out = NULL;
    if (revision_out) *revision_out = NULL;
    if (!run_result || !completion_out || !reason_out || !occurrences_out ||
        !metrics_out || !revision_out) {
        return false;
    }

    Atom *occurrences = NULL;
    Atom *metrics = NULL;
    Atom *revision = NULL;
    if (prime_rule_machine_head(run_result, "compile-result", 5u) &&
        atom_is_symbol(run_result->expr.elems[1], "proof-occurrence-bag")) {
        *completion_out = CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1;
        occurrences = run_result->expr.elems[2];
        metrics = run_result->expr.elems[3];
        revision = run_result->expr.elems[4];
    } else if (
        prime_rule_machine_head(run_result, "compile-incomplete", 6u) &&
        atom_is_symbol(run_result->expr.elems[2], "proof-occurrence-bag")) {
        *completion_out = CETTA_PRIME_RULE_MACHINE_RUN_INCOMPLETE_V1;
        *reason_out = run_result->expr.elems[1];
        occurrences = run_result->expr.elems[3];
        metrics = run_result->expr.elems[4];
        revision = run_result->expr.elems[5];
    } else {
        return false;
    }
    if (!occurrences || occurrences->kind != ATOM_EXPR ||
        occurrences->expr.len == 0u ||
        !atom_is_symbol(occurrences->expr.elems[0], "occurrences") ||
        revision->kind != ATOM_SYMBOL) {
        return false;
    }
    for (CettaExprIndex index = 1u; index < occurrences->expr.len; index++)
        if (!prime_rule_machine_head(
                occurrences->expr.elems[index], "occurrence", 2u)) {
            return false;
        }
    *occurrences_out = occurrences;
    *metrics_out = metrics;
    *revision_out = revision;
    return true;
}

static CettaPrimeTypedValueV1 *prime_rule_machine_retain_receipt(
    Arena *owner, Space *space,
    CettaPrimeTypedValueV1 *checked,
    Atom *producer_revision, Atom *metrics,
    Atom *encoded_proof, size_t ordinal) {
    if (!owner || !space || !space->native.universe || !checked ||
        !producer_revision || !metrics || !encoded_proof ||
        !cetta_prime_typed_value_v1_is_current(checked, space) ||
        ordinal > (size_t)INT64_MAX) {
        return NULL;
    }
    TermUniverse *universe = space->native.universe;
    Atom *ordinal_atom = atom_int(owner, (int64_t)ordinal);
    AtomId witnesses[] = {
        term_universe_store_atom_id(universe, owner, producer_revision),
        term_universe_store_atom_id(universe, owner, metrics),
        term_universe_store_atom_id(universe, owner, encoded_proof),
        term_universe_store_atom_id(universe, owner, ordinal_atom),
    };
    for (size_t index = 0u;
         index < sizeof(witnesses) / sizeof(witnesses[0]); index++)
        if (witnesses[index] == CETTA_ATOM_ID_NONE) return NULL;

    const CettaPrimeTypedValueV1 *premises[] = {checked};
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "typed:rule-machine-ingress",
        .construction = CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1,
        .premises = premises,
        .premise_count = 1u,
        .witness_ids = witnesses,
        .witness_count = sizeof(witnesses) / sizeof(witnesses[0]),
        .family_head_id = checked->family_head_id,
        .parameter_ids = checked->parameter_ids,
        .parameter_count = checked->parameter_count,
        .index_ids = checked->index_ids,
        .index_count = checked->index_count,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, universe, checked->context_id, checked->term_id,
        checked->type_id, &checked->authority_token, &build);
}

bool cetta_prime_rule_machine_import_run_v1(
    Arena *owner, Space *space, Atom *run_result,
    const CettaPrimeTypedValueV1 *expected_type,
    bool steps_limited, uint64_t steps,
    CettaPrimeRuleMachineIngressResultV1 *result_out) {
    if (result_out) *result_out = (CettaPrimeRuleMachineIngressResultV1){0};
    if (!owner || !space || !run_result || !expected_type || !result_out ||
        (steps_limited && steps == 0u) ||
        !cetta_prime_typed_value_v1_is_current(expected_type, space)) {
        return false;
    }

    CettaPrimeRuleMachineRunCompletionV1 completion;
    Atom *reason = NULL;
    Atom *occurrences = NULL;
    Atom *metrics = NULL;
    Atom *revision = NULL;
    if (!prime_rule_machine_result_view(
            run_result, &completion, &reason, &occurrences,
            &metrics, &revision)) {
        return false;
    }

    CettaPrimeRuleMachineIngressResultV1 result = {
        .completion = completion,
        .incomplete_reason = reason ? atom_deep_copy(owner, reason) : NULL,
        .producer_revision = atom_deep_copy(owner, revision),
        .metrics = atom_deep_copy(owner, metrics),
        .occurrence_count = (size_t)occurrences->expr.len - 1u,
    };
    if (!result.producer_revision || !result.metrics ||
        (reason && !result.incomplete_reason) ||
        result.occurrence_count >
            SIZE_MAX / sizeof(*result.occurrences)) {
        return false;
    }
    result.occurrences = result.occurrence_count == 0u ? NULL :
        arena_alloc(
            owner, result.occurrence_count * sizeof(*result.occurrences));
    if (result.occurrences)
        memset(
            result.occurrences, 0,
            result.occurrence_count * sizeof(*result.occurrences));

    for (size_t index = 0u; index < result.occurrence_count; index++) {
        Atom *encoded = occurrences->expr.elems[index + 1u]->expr.elems[1];
        CettaPrimeRuleMachineTypedOccurrenceV1 *item =
            &result.occurrences[index];
        item->encoded_proof = atom_deep_copy(owner, encoded);
        if (!item->encoded_proof) return false;
    }

    if (completion == CETTA_PRIME_RULE_MACHINE_RUN_INCOMPLETE_V1) {
        *result_out = result;
        return true;
    }

    for (size_t index = 0u; index < result.occurrence_count; index++) {
        CettaPrimeRuleMachineTypedOccurrenceV1 *item =
            &result.occurrences[index];
        if (!prime_rule_machine_head(item->encoded_proof, "quote", 2u))
            return false;
        item->elaborated_term =
            cetta_prime_typed_boundary_splice_explicit_v1(
                owner, item->encoded_proof->expr.elems[1]);
        if (!item->elaborated_term || atom_has_vars(item->elaborated_term))
            return false;

        CettaPrimeTypedValueV1 *checked = NULL;
        PrimeRuleMachineNativeBuildV1 constructed =
            prime_rule_machine_native_construct(
                owner, space, item->elaborated_term, expected_type,
                steps_limited, steps, &checked);
        if (constructed == PRIME_RULE_MACHINE_NATIVE_FAULT) return false;
        if (constructed == PRIME_RULE_MACHINE_NATIVE_BUILT) {
            item->mode =
                CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1;
        } else {
            if (!cetta_prime_typed_value_import_checked_term_v1(
                    owner, space, item->elaborated_term, expected_type,
                    steps_limited, steps, &item->checking, &checked)) {
                return false;
            }
            if (!checked) continue;
            item->mode =
                CETTA_PRIME_RULE_MACHINE_INGRESS_CHECKED_BOUNDARY_V1;
        }
        item->value = prime_rule_machine_retain_receipt(
            owner, space, checked, result.producer_revision,
            result.metrics, item->encoded_proof, index);
        if (!item->value) return false;
    }
    *result_out = result;
    return true;
}
