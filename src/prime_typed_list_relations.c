#include "prime_typed_list_relations.h"

#include "prime_typed_flow_private.h"

static CettaPrimeTypedValueV1 *prime_typed_list_relation_specialize(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *rule,
    const CettaPrimeTypedValueV1 *const *parameters,
    size_t parameter_count) {
    const CettaPrimeTypedValueV1 *current = rule;
    CettaPrimeTypedValueV1 *result = NULL;
    for (size_t index = 0u; current && index < parameter_count; index++) {
        result = cetta_prime_typed_value_apply_converting_v1(
            owner, space, current, parameters[index]);
        current = result;
    }
    return result;
}

static CettaPrimeTypedValueV1 *prime_typed_list_relation_attach(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *constructor_name, size_t constructor_arity,
    const char *family_name, size_t parameter_count,
    size_t index_count) {
    if (!cetta_prime_typed_value_has_application_head_private_v1(
            owner, space, value, constructor_name, constructor_arity)) {
        return NULL;
    }
    return cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, value, family_name, parameter_count, index_count);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_all_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *predicate) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, predicate,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        prime_typed_list_relation_specialize(
            owner, space, nil_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0])),
        "rel:all:nil", 2u, "rel:all", 2u, 1u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_all_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *head_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, predicate,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, cons_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        head, tail, head_evidence, tail_evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:all:cons", 6u, "rel:all", 2u, 1u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_member_here_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *here_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail) {
    const CettaPrimeTypedValueV1 *parameters[] = {element_type};
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, here_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {head, tail};
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:list:member-here", 3u,
        "rel:list:member", 1u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_member_there_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *there_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *member,
    const CettaPrimeTypedValueV1 *tail_evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {element_type};
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, there_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        head, tail, member, tail_evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:list:member-there", 5u,
        "rel:list:member", 1u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_any_here_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *here_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *values,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, target_type, predicate,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, here_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        values, answer, evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:any:here", 6u, "rel:any", 3u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_any_there_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *there_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, target_type, predicate,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, there_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        head, tail, answer, evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:any:there", 7u, "rel:any", 3u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_case_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, target_type, nil_case, cons_case,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, nil_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {answer, evidence};
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:case-list:nil", 6u,
        "rel:case-list", 4u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_case_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, target_type, nil_case, cons_case,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, cons_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        head, tail, answer, evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:case-list:cons", 8u,
        "rel:case-list", 4u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_fold_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *accumulator_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *before) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, accumulator_type, step,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, nil_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {before};
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:fold:nil", 4u, "rel:fold", 3u, 3u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_list_fold_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *accumulator_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *before,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *next,
    const CettaPrimeTypedValueV1 *after,
    const CettaPrimeTypedValueV1 *step_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        element_type, accumulator_type, step,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_list_relation_specialize(
            owner, space, cons_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        before, head, tail, next, after, step_evidence, tail_evidence,
    };
    return prime_typed_list_relation_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:fold:cons", 10u, "rel:fold", 3u, 3u);
}
