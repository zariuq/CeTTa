#ifndef CETTA_NATIVE_SHA256_H
#define CETTA_NATIVE_SHA256_H

#include <stddef.h>
#include <stdint.h>

void cetta_native_sha256_hex(const uint8_t *bytes, size_t len, char out[65]);

#endif /* CETTA_NATIVE_SHA256_H */
