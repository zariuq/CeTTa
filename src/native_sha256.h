#ifndef CETTA_NATIVE_SHA256_H
#define CETTA_NATIVE_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t block[64];
    uint32_t block_len;
} CettaNativeSha256;

void cetta_native_sha256_init(CettaNativeSha256 *sha);
void cetta_native_sha256_update(CettaNativeSha256 *sha,
                                const uint8_t *bytes,
                                size_t len);
void cetta_native_sha256_finish_hex(CettaNativeSha256 *sha, char out[65]);
void cetta_native_sha256_hex(const uint8_t *bytes, size_t len, char out[65]);

#endif /* CETTA_NATIVE_SHA256_H */
