#ifndef CETTA_PRIME_TYPING_PUBLICATION_H
#define CETTA_PRIME_TYPING_PUBLICATION_H

#include "atom.h"
#include "nik_direct_authority.h"

#include <stdbool.h>
#include <stdint.h>

/* Direct decision used after Prime has evaluated one result and inferred its
 * possible types.  Contracts are alternatives for accepting that result;
 * they never introduce additional evaluation branches. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    bool (*accepts_any)(
        Atom *const *actual_types, uint32_t actual_count,
        Atom *const *contracts, uint32_t contract_count);
} CettaPrimeTypedPublicationDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_prime_typed_publication_direct_authority_v1;
extern const CettaPrimeTypedPublicationDirectServiceV1
    cetta_prime_typed_publication_direct_service_v1;

bool cetta_prime_typed_publication_accepts_any_v1(
    Atom *const *actual_types, uint32_t actual_count,
    Atom *const *contracts, uint32_t contract_count);

bool cetta_prime_typed_publication_direct_service_v1_is_valid(
    const CettaPrimeTypedPublicationDirectServiceV1 *service);

#endif /* CETTA_PRIME_TYPING_PUBLICATION_H */
