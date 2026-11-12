#ifndef CESTA_DIGEST_H
#define CESTA_DIGEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Streaming SHA-256; no process, library, or proof-tool dependency. */
typedef struct {
    uint32_t words[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} CeDigest;

void ce_digest_init(CeDigest *digest);
bool ce_digest_update(CeDigest *digest, const void *data, size_t length);
void ce_digest_finish(CeDigest *digest, char hex[65]);

#endif
