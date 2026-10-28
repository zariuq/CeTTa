#ifndef CETTA_CRYPTO_PRIMITIVES_H
#define CETTA_CRYPTO_PRIMITIVES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NIP-44 v2 and the primitives under it, on bytes, as the NIP specifies
 * them, and SHA-1 for the WebSocket opening handshake.  No engine types:
 * lib/crypto (library_crypto.c), the WebSocket listener and the native unit
 * test share these. */

void cetta_crypto_wipe(void *bytes, size_t length);
bool cetta_crypto_entropy(uint8_t *out, size_t length);

/* HMAC-SHA256 (RFC 2104) of the concatenation of `count` message parts. */
void cetta_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                              const uint8_t *const *parts, const size_t *lengths,
                              size_t count, uint8_t out[32]);
/* SHA-1 (FIPS 180-4) of the concatenation of `count` parts.  RFC 6455 names
 * it for Sec-WebSocket-Accept, which needs no collision resistance; nothing
 * else should use it. */
void cetta_crypto_sha1(const uint8_t *const *parts, const size_t *lengths, size_t count,
                       uint8_t out[20]);
/* ChaCha20 (RFC 8439) with a 32-bit block counter and a 96-bit nonce. */
void cetta_crypto_chacha20(const uint8_t key[32], const uint8_t nonce[12],
                           uint32_t counter, const uint8_t *in, uint8_t *out,
                           size_t length);
/* Standard base64 with padding.  Decoding reads what Python's b64decode
 * reads with validate=True: only the alphabet, '=' only as the last one or
 * two characters, and the unused low bits of the last digit ignored.  The
 * output holds at least length / 4 * 3 bytes. */
size_t cetta_crypto_base64_encode(const uint8_t *bytes, size_t length, char *out);
bool cetta_crypto_base64_decode(const uint8_t *text, size_t length, uint8_t *out,
                                size_t *out_length);

#define CETTA_NIP44_PLAINTEXT_MAX 65535u

size_t cetta_crypto_nip44_padded_length(size_t length);
/* The ChaCha20 key (32 bytes), ChaCha20 nonce (12) and HMAC key (32) of one
 * message: HKDF-expand of the conversation key with the nonce as info. */
void cetta_crypto_nip44_message_keys(const uint8_t conversation[32],
                                     const uint8_t nonce[32], uint8_t keys[76]);

typedef enum {
    CETTA_NIP44_OK = 0,
    CETTA_NIP44_INVALID_PLAINTEXT_LENGTH,
    CETTA_NIP44_UNKNOWN_VERSION,
    CETTA_NIP44_INVALID_PAYLOAD_SIZE,
    CETTA_NIP44_INVALID_BASE64,
    CETTA_NIP44_INVALID_DATA_SIZE,
    CETTA_NIP44_INVALID_MAC,
    CETTA_NIP44_INVALID_PADDING,
    CETTA_NIP44_NO_MEMORY
} CettaNip44Status;

/* The base64 payload of a plaintext of 1 to 65535 bytes, in malloc'd memory
 * with a terminating NUL. */
CettaNip44Status cetta_crypto_nip44_encrypt(const uint8_t conversation[32],
                                            const uint8_t nonce[32],
                                            const uint8_t *plaintext, size_t length,
                                            char **payload, size_t *payload_length);
/* The plaintext bytes of a payload, in malloc'd memory, checked in the
 * order NIP-44 decodes: version marker, payload size, base64, data size,
 * version, MAC, padding.  The bytes are not checked as UTF-8. */
CettaNip44Status cetta_crypto_nip44_decrypt(const uint8_t conversation[32],
                                            const uint8_t *payload, size_t length,
                                            uint8_t **plaintext, size_t *plaintext_length);
#endif
