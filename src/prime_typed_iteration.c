#include "prime_typed_iteration.h"

#include "prime_typed_flow_private.h"

static CettaPrimeTypedValueV1 *prime_typed_iteration_specialize(
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

static CettaPrimeTypedValueV1 *prime_typed_iteration_attach(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *constructor_name, size_t constructor_arity) {
    if (!cetta_prime_typed_value_has_application_head_private_v1(
            owner, space, value, constructor_name, constructor_arity)) {
        return NULL;
    }
    return cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, value, "rel:iterate", 5u, 3u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_iteration_zero_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *zero_rule,
    const CettaPrimeTypedValueV1 *value_type,
    const CettaPrimeTypedValueV1 *counter_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *predecessor,
    const CettaPrimeTypedValueV1 *zero,
    const CettaPrimeTypedValueV1 *source) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        value_type, counter_type, step, predecessor, zero,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_iteration_specialize(
            owner, space, zero_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {source};
    return prime_typed_iteration_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:iterate:zero", 6u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_iteration_step_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *step_rule,
    const CettaPrimeTypedValueV1 *value_type,
    const CettaPrimeTypedValueV1 *counter_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *predecessor,
    const CettaPrimeTypedValueV1 *zero,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *next,
    const CettaPrimeTypedValueV1 *later,
    const CettaPrimeTypedValueV1 *earlier,
    const CettaPrimeTypedValueV1 *target,
    const CettaPrimeTypedValueV1 *predecessor_evidence,
    const CettaPrimeTypedValueV1 *step_evidence,
    const CettaPrimeTypedValueV1 *recursive_evidence) {
    const CettaPrimeTypedValueV1 *parameters[] = {
        value_type, counter_type, step, predecessor, zero,
    };
    CettaPrimeTypedValueV1 *specialized =
        prime_typed_iteration_specialize(
            owner, space, step_rule, parameters,
            sizeof(parameters) / sizeof(parameters[0]));
    const CettaPrimeTypedValueV1 *arguments[] = {
        source, next, later, earlier, target,
        predecessor_evidence, step_evidence, recursive_evidence,
    };
    return prime_typed_iteration_attach(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, specialized, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "rel:iterate:step", 13u);
}
