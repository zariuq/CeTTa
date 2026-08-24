#ifndef CETTA_PRIME_TYPING_AUTHORITY_H
#define CETTA_PRIME_TYPING_AUTHORITY_H

#include <stdbool.h>
#include <stdint.h>

#include "nik_direct_authority.h"
#include "space.h"

/* Prime's language-owned contribution to a NIK authority token.  This narrow
 * interface exposes identity and currentness without importing the checking
 * and observation services that live at the raw language boundary. */
bool cetta_prime_typing_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_prime_typing_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity);

#endif /* CETTA_PRIME_TYPING_AUTHORITY_H */
