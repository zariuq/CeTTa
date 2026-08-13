#ifndef CETTA_PRIME_SEMANTICS_H
#define CETTA_PRIME_SEMANTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "he_typing_authority.h"
#include "nik_direct_authority.h"
#include "space.h"

/* Certificate-free NIK face for Prime's native type judgments.  It excludes
 * May/Must evaluation and the four-argument Check form used to import proof
 * objects from external authorities. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    const CettaHeTypingCoreDirectServiceV1 *he_typing_core_backend;
    Atom *(*judge)(Arena *arena, Space *space, Atom *judgment,
                   bool steps_limited, uint64_t steps);
} CettaPrimeTypingDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_prime_typing_direct_authority_v1;
extern const CettaPrimeTypingDirectServiceV1
    cetta_prime_typing_direct_service_v1;

bool cetta_prime_typing_direct_service_v1_is_valid(
    const CettaPrimeTypingDirectServiceV1 *service);

/* Returns NULL when the claim is not one of Prime's native type judgments. */
Atom *prime_semantics_judge_typing_direct(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

bool cetta_prime_typing_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_prime_typing_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity);

/* MeTTa-Prime is a language package, not an HE profile.  The weak hooks keep
 * standalone evaluator test binaries linkable when this module is omitted. */
Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) __attribute__((weak));
bool prime_semantics_is_op(const char *name) __attribute__((weak));
bool prime_semantics_is_op_id(SymbolId id) __attribute__((weak));
bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

/* Internal package boundary used by the Prime gate.  Construction returns
 * NULL if the resulting schema does not validate. */
Atom *prime_semantics_package_atom(Arena *a);
bool prime_semantics_validate_package(Atom *package);
/* Neutral named-telescope elaboration.  A non-NULL result is a closed
 * canonical Pi/Var ABT, not evidence that the source type is well formed. */
Atom *prime_semantics_canonicalize_type(Arena *a, Atom *type);
bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out);

#endif /* CETTA_PRIME_SEMANTICS_H */
