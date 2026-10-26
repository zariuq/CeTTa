#if defined(_WIN32)
#define _CRT_RAND_S
#endif
#include "crypto_primitives.h"
#include "native_sha256.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/random.h>
#endif

void cetta_crypto_wipe(void *bytes, size_t length) {
    volatile uint8_t *at = bytes;
    while (length--) *at++ = 0u;
}

bool cetta_crypto_entropy(uint8_t *out, size_t length) {
#if defined(_WIN32)
    while (length) {
        unsigned int value;
        if (rand_s(&value) != 0) return false;
        for (unsigned i = 0u; i < 4u && length; i++, length--) *out++ = (uint8_t)(value >> (8u * i));
    }
    return true;
#else
    while (length) {
        size_t chunk = length > 256u ? 256u : length;
        if (getentropy(out, chunk) != 0) {
            if (errno == EINTR) continue;
            return false;
        }
        out += chunk;
        length -= chunk;
    }
    return true;
#endif
}

void cetta_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                              const uint8_t *const *parts, const size_t *lengths,
                              size_t count, uint8_t out[32]) {
    uint8_t block[64] = {0}, pad[64], inner[32];
    CettaNativeSha256 sha;
    if (key_len > sizeof(block)) {
        cetta_native_sha256_init(&sha);
        cetta_native_sha256_update(&sha, key, key_len);
        cetta_native_sha256_finish(&sha, block);
    } else if (key_len) {
        memcpy(block, key, key_len);
    }
    for (size_t i = 0u; i < sizeof(pad); i++) pad[i] = block[i] ^ 0x36u;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, pad, sizeof(pad));
    for (size_t i = 0u; i < count; i++)
        if (lengths[i]) cetta_native_sha256_update(&sha, parts[i], lengths[i]);
    cetta_native_sha256_finish(&sha, inner);
    for (size_t i = 0u; i < sizeof(pad); i++) pad[i] = block[i] ^ 0x5cu;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, pad, sizeof(pad));
    cetta_native_sha256_update(&sha, inner, sizeof(inner));
    cetta_native_sha256_finish(&sha, out);
    cetta_crypto_wipe(block, sizeof(block));
    cetta_crypto_wipe(pad, sizeof(pad));
    cetta_crypto_wipe(inner, sizeof(inner));
    cetta_crypto_wipe(&sha, sizeof(sha));
}

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t rotl32(uint32_t value, unsigned amount) {
    return value << amount | value >> (32u - amount);
}

static void quarter(uint32_t *x, unsigned a, unsigned b, unsigned c, unsigned d) {
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16u);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12u);
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8u);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7u);
}

static void chacha_block(const uint32_t state[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, state, sizeof(x));
    for (unsigned round = 0u; round < 10u; round++) {
        quarter(x, 0u, 4u, 8u, 12u); quarter(x, 1u, 5u, 9u, 13u);
        quarter(x, 2u, 6u, 10u, 14u); quarter(x, 3u, 7u, 11u, 15u);
        quarter(x, 0u, 5u, 10u, 15u); quarter(x, 1u, 6u, 11u, 12u);
        quarter(x, 2u, 7u, 8u, 13u); quarter(x, 3u, 4u, 9u, 14u);
    }
    for (unsigned i = 0u; i < 16u; i++) {
        uint32_t word = x[i] + state[i];
        out[4u * i] = (uint8_t)word;
        out[4u * i + 1u] = (uint8_t)(word >> 8);
        out[4u * i + 2u] = (uint8_t)(word >> 16);
        out[4u * i + 3u] = (uint8_t)(word >> 24);
    }
    cetta_crypto_wipe(x, sizeof(x));
}

void cetta_crypto_chacha20(const uint8_t key[32], const uint8_t nonce[12],
                           uint32_t counter, const uint8_t *in, uint8_t *out,
                           size_t length) {
    uint32_t state[16] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};
    uint8_t block[64];
    for (unsigned i = 0u; i < 8u; i++) state[4u + i] = load32(key + 4u * i);
    state[12] = counter;
    for (unsigned i = 0u; i < 3u; i++) state[13u + i] = load32(nonce + 4u * i);
    for (size_t at = 0u; at < length; at += sizeof(block)) {
        size_t n = length - at < sizeof(block) ? length - at : sizeof(block);
        chacha_block(state, block);
        for (size_t i = 0u; i < n; i++) out[at + i] = in[at + i] ^ block[i];
        state[12]++;
    }
    cetta_crypto_wipe(state, sizeof(state));
    cetta_crypto_wipe(block, sizeof(block));
}

static const char base64_digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t cetta_crypto_base64_encode(const uint8_t *bytes, size_t length, char *out) {
    char *at = out;
    for (size_t i = 0u; i < length; i += 3u) {
        uint32_t group = (uint32_t)bytes[i] << 16;
        if (i + 1u < length) group |= (uint32_t)bytes[i + 1u] << 8;
        if (i + 2u < length) group |= bytes[i + 2u];
        *at++ = base64_digits[group >> 18 & 63u];
        *at++ = base64_digits[group >> 12 & 63u];
        *at++ = i + 1u < length ? base64_digits[group >> 6 & 63u] : '=';
        *at++ = i + 2u < length ? base64_digits[group & 63u] : '=';
    }
    *at = '\0';
    return (size_t)(at - out);
}

static int base64_digit(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool cetta_crypto_base64_decode(const uint8_t *text, size_t length, uint8_t *out,
                                size_t *out_length) {
    size_t written = 0u;
    if (length % 4u) return false;
    for (size_t i = 0u; i < length; i += 4u) {
        uint32_t group = 0u;
        unsigned padding = 0u;
        for (unsigned j = 0u; j < 4u; j++) {
            int digit = 0;
            if (text[i + j] == '=') {
                if (i + 4u != length || j < 2u) return false;
                padding++;
            } else if (padding || (digit = base64_digit(text[i + j])) < 0) {
                return false;
            }
            group = group << 6 | (uint32_t)digit;
        }
        out[written++] = (uint8_t)(group >> 16);
        if (padding < 2u) out[written++] = (uint8_t)(group >> 8);
        if (padding < 1u) out[written++] = (uint8_t)group;
    }
    *out_length = written;
    return true;
}

size_t cetta_crypto_nip44_padded_length(size_t length) {
    if (length <= 32u) return 32u;
    size_t power = 1u;
    while (power < length) power <<= 1;
    size_t chunk = power <= 256u ? 32u : power / 8u;
    return chunk * ((length - 1u) / chunk + 1u);
}

void cetta_crypto_nip44_message_keys(const uint8_t conversation[32],
                                     const uint8_t nonce[32], uint8_t keys[76]) {
    uint8_t okm[96];
    for (unsigned counter = 1u; counter <= 3u; counter++) {
        const uint8_t octet = (uint8_t)counter;
        const uint8_t *previous = counter == 1u ? okm : okm + 32u * (counter - 2u);
        const uint8_t *parts[3] = {previous, nonce, &octet};
        const size_t lengths[3] = {counter == 1u ? 0u : 32u, 32u, 1u};
        cetta_crypto_hmac_sha256(conversation, 32u, parts, lengths, 3u, okm + 32u * (counter - 1u));
    }
    memcpy(keys, okm, 76u);
    cetta_crypto_wipe(okm, sizeof(okm));
}

static void nip44_mac(const uint8_t keys[76], const uint8_t nonce[32],
                      const uint8_t *ciphertext, size_t length, uint8_t out[32]) {
    const uint8_t *parts[2] = {nonce, ciphertext};
    const size_t lengths[2] = {32u, length};
    cetta_crypto_hmac_sha256(keys + 44u, 32u, parts, lengths, 2u, out);
}

static bool equal_in_constant_time(const uint8_t *a, const uint8_t *b, size_t length) {
    uint8_t difference = 0u;
    for (size_t i = 0u; i < length; i++) difference |= (uint8_t)(a[i] ^ b[i]);
    return difference == 0u;
}

CettaNip44Status cetta_crypto_nip44_encrypt(const uint8_t conversation[32],
                                            const uint8_t nonce[32],
                                            const uint8_t *plaintext, size_t length,
                                            char **payload, size_t *payload_length) {
    uint8_t keys[76];
    if (length < 1u || length > CETTA_NIP44_PLAINTEXT_MAX) return CETTA_NIP44_INVALID_PLAINTEXT_LENGTH;
    size_t padded = 2u + cetta_crypto_nip44_padded_length(length);
    size_t total = 1u + 32u + padded + 32u;
    uint8_t *data = malloc(total);
    char *text = malloc((total + 2u) / 3u * 4u + 1u);
    if (!data || !text) {
        free(data);
        free(text);
        return CETTA_NIP44_NO_MEMORY;
    }
    uint8_t *ciphertext = data + 33u;
    data[0] = 2u;
    memcpy(data + 1u, nonce, 32u);
    ciphertext[0] = (uint8_t)(length >> 8);
    ciphertext[1] = (uint8_t)length;
    memcpy(ciphertext + 2u, plaintext, length);
    memset(ciphertext + 2u + length, 0, padded - 2u - length);
    cetta_crypto_nip44_message_keys(conversation, nonce, keys);
    cetta_crypto_chacha20(keys, keys + 32u, 0u, ciphertext, ciphertext, padded);
    nip44_mac(keys, nonce, ciphertext, padded, ciphertext + padded);
    *payload_length = cetta_crypto_base64_encode(data, total, text);
    *payload = text;
    cetta_crypto_wipe(keys, sizeof(keys));
    cetta_crypto_wipe(data, total);
    free(data);
    return CETTA_NIP44_OK;
}

CettaNip44Status cetta_crypto_nip44_decrypt(const uint8_t conversation[32],
                                            const uint8_t *payload, size_t length,
                                            uint8_t **plaintext, size_t *plaintext_length) {
    uint8_t keys[76], mac[32];
    size_t data_length;
    if (length == 0u || payload[0] == '#') return CETTA_NIP44_UNKNOWN_VERSION;
    if (length < 132u || length > 87472u) return CETTA_NIP44_INVALID_PAYLOAD_SIZE;
    size_t capacity = length / 4u * 3u;
    uint8_t *data = malloc(capacity);
    if (!data) return CETTA_NIP44_NO_MEMORY;
    CettaNip44Status status = CETTA_NIP44_OK;
    if (!cetta_crypto_base64_decode(payload, length, data, &data_length)) {
        status = CETTA_NIP44_INVALID_BASE64;
    } else if (data_length < 99u || data_length > 65603u) {
        status = CETTA_NIP44_INVALID_DATA_SIZE;
    } else if (data[0] != 2u) {
        status = CETTA_NIP44_UNKNOWN_VERSION;
    } else {
        const uint8_t *nonce = data + 1u;
        uint8_t *padded = data + 33u;
        size_t padded_length = data_length - 65u;
        cetta_crypto_nip44_message_keys(conversation, nonce, keys);
        nip44_mac(keys, nonce, padded, padded_length, mac);
        if (!equal_in_constant_time(mac, data + data_length - 32u, 32u)) {
            status = CETTA_NIP44_INVALID_MAC;
        } else {
            cetta_crypto_chacha20(keys, keys + 32u, 0u, padded, padded, padded_length);
            size_t plain = (size_t)padded[0] << 8 | padded[1];
            if (plain == 0u || padded_length != 2u + cetta_crypto_nip44_padded_length(plain)) {
                status = CETTA_NIP44_INVALID_PADDING;
            } else {
                memmove(data, padded + 2u, plain);
                cetta_crypto_wipe(data + plain, capacity - plain);
                *plaintext = data;
                *plaintext_length = plain;
            }
        }
        cetta_crypto_wipe(keys, sizeof(keys));
    }
    if (status != CETTA_NIP44_OK) {
        cetta_crypto_wipe(data, capacity);
        free(data);
    }
    return status;
}
