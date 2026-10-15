/* SPDX-License-Identifier: MIT */
#ifndef CETTA_SIWC_CRYPTO_H
#define CETTA_SIWC_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <jansson.h>

/* All inputs and outputs here are private native bytes, never evaluator data. */
bool cetta_siwc_random(char out[44]);
bool cetta_siwc_challenge(const char *verifier, char out[44]);
json_t *cetta_siwc_verify(const char *jwt, const json_t *jwks, const char *issuer,
                         const char *client, const char *nonce, int64_t now,
                         const char **error);
void cetta_siwc_secret_free(char *secret);
#endif
