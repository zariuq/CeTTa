#ifndef CETTA_GENERATED_cetta_prime_nik_authorities_v1_H
#define CETTA_GENERATED_cetta_prime_nik_authorities_v1_H

#include <stddef.h>

typedef struct {
    const char *alias;
    const char *system_id;
    const char *revision;
    const char *digest;
    const char *presentation_metta;
    const char *positive_goal_metta;
    const char *positive_proof_metta;
} CettaNikAuthorityV1;

extern const CettaNikAuthorityV1 cetta_prime_nik_authorities_v1[];
extern const size_t cetta_prime_nik_authorities_v1_count;
extern const char cetta_prime_nik_authorities_v1_catalog_sha256[];

#endif
