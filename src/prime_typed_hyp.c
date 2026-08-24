#include "prime_typed_hyp.h"

#include "prime_typed_flow_private.h"

static CettaPrimeTypedValueV1 *prime_typed_hyp_attach_indexed_view(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *constructor_name, size_t constructor_arity) {
    if (!cetta_prime_typed_value_has_application_head_private_v1(
            owner, space, value, constructor_name, constructor_arity)) {
        return NULL;
    }
    return cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, value, "hyp", 2u, 2u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_hyp_primitive_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *primitive_rule,
    const CettaPrimeTypedValueV1 *source_sort,
    const CettaPrimeTypedValueV1 *target_sort,
    const CettaPrimeTypedValueV1 *primitive_symbol) {
    const CettaPrimeTypedValueV1 *arguments[] = {
        source_sort, target_sort, primitive_symbol,
    };
    return prime_typed_hyp_attach_indexed_view(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, primitive_rule, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "hyp:primitive", 5u);
}

CettaPrimeTypedValueV1 *cetta_prime_typed_hyp_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *chain_rule,
    const CettaPrimeTypedValueV1 *source_sort,
    const CettaPrimeTypedValueV1 *middle_sort,
    const CettaPrimeTypedValueV1 *target_sort,
    const CettaPrimeTypedValueV1 *earlier,
    const CettaPrimeTypedValueV1 *later) {
    const CettaPrimeTypedValueV1 *arguments[] = {
        source_sort, middle_sort, target_sort, earlier, later,
    };
    return prime_typed_hyp_attach_indexed_view(
        owner, space,
        cetta_prime_typed_value_apply_many_v1(
            owner, space, chain_rule, arguments,
            sizeof(arguments) / sizeof(arguments[0])),
        "hyp:chain", 7u);
}
