#ifndef CETTA_PRIME_TYPING_AUTHORITY_H
#define CETTA_PRIME_TYPING_AUTHORITY_H

#include <stdbool.h>
#include <stdint.h>

#include "nik_direct_authority.h"
#include "space.h"

/* Prime's language-owned contribution to a NIK authority token. The private
 * signature view is prepared before the token is pinned, so its lazy creation
 * cannot masquerade as a change of the user's scope. Admission changes are
 * recorded independently of syntax revisions: withdrawing a record need not
 * remove its stored syntax. A token does not admit a declaration or judgment. */
bool cetta_prime_typing_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_prime_typing_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity);

#endif /* CETTA_PRIME_TYPING_AUTHORITY_H */
