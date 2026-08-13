#include "prime_typing_publication.h"

#include "match.h"
#include "symbol.h"

const CettaNikDirectAuthorityV1
    cetta_prime_typed_publication_direct_authority_v1 = {
        .alias = "PRIME-TYPED-PUBLICATION",
        .system_id = "prime.typing.result-contract-publication",
        .authority_identity = UINT64_C(0x7072696d652e7075),
        .realization_identity = UINT64_C(0x63657474612e7075),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

bool cetta_prime_typed_publication_accepts_any_v1(
    Atom *const *actual_types, uint32_t actual_count,
    Atom *const *contracts, uint32_t contract_count) {
    if ((!actual_types && actual_count != 0u) ||
        (!contracts && contract_count != 0u)) {
        return false;
    }
    for (uint32_t contract_index = 0u;
         contract_index < contract_count; contract_index++) {
        Atom *expected = contracts[contract_index];
        if (!expected)
            continue;
        if (atom_is_symbol_id(expected, g_builtin_syms.undefined_type) ||
            atom_is_symbol_id(expected, g_builtin_syms.atom)) {
            return true;
        }
        for (uint32_t actual_index = 0u;
             actual_index < actual_count; actual_index++) {
            Atom *actual = actual_types[actual_index];
            if (!actual)
                continue;
            Bindings environment;
            bindings_init(&environment);
            bool accepted = match_types(actual, expected, &environment);
            bindings_free(&environment);
            if (accepted)
                return true;
        }
    }
    return false;
}

const CettaPrimeTypedPublicationDirectServiceV1
    cetta_prime_typed_publication_direct_service_v1 = {
        .authority =
            &cetta_prime_typed_publication_direct_authority_v1,
        .accepts_any = cetta_prime_typed_publication_accepts_any_v1,
    };

bool cetta_prime_typed_publication_direct_service_v1_is_valid(
    const CettaPrimeTypedPublicationDirectServiceV1 *service) {
    return service &&
           cetta_nik_direct_authority_v1_is_valid(service->authority) &&
           service->accepts_any;
}
