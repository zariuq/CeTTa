/* lib/crypto: the primitives of Nostr agent mail.  BIP-340 Schnorr keys and
 * signatures (NIP-01) and NIP-44 v2 encryption, with every operand and
 * result as text: keys, digests and signatures in lowercase hex, payloads in
 * base64, plaintexts as UTF-8.  Binary data never enters a MeTTa string.
 *
 * The curve operations are the vendored libsecp256k1's; NIP-44 itself is in
 * crypto_primitives.c.
 *
 * (__cetta_lib_crypto OPERATION (OPERANDS...)) answers a string, True or
 * False, or (crypto:failed KIND DETAIL).  Signature verification answers
 * False for any text that is not a valid signature. */
#include "library_crypto.h"
#include "crypto_primitives.h"
#include "library.h"
#include "petta_semantics.h"
#include "utf8.h"
#include "vendor/secp256k1/include/secp256k1.h"
#include "vendor/secp256k1/include/secp256k1_ecdh.h"
#include "vendor/secp256k1/include/secp256k1_extrakeys.h"
#include "vendor/secp256k1/include/secp256k1_schnorrsig.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define RANDOM_BYTES_MAX 65536u

/* ── text ──────────────────────────────────────────────────────────────── */

static bool text(Atom *x, const uint8_t **bytes, size_t *length) {
    if (!x) return false;
    if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING) {
        *bytes = (const uint8_t *)x->ground.sval;
        *length = atom_string_len(x);
        return true;
    }
    if (x->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(x);
        if (!name) return false;
        *bytes = (const uint8_t *)name;
        *length = strlen(name);
        return true;
    }
    return false;
}

static int hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_into(const uint8_t *s, size_t length, uint8_t *out) {
    if (length % 2u) return false;
    for (size_t i = 0u; i < length / 2u; i++) {
        int high = hex_digit(s[2u * i]), low = hex_digit(s[2u * i + 1u]);
        if (high < 0 || low < 0) return false;
        out[i] = (uint8_t)(high << 4 | low);
    }
    return true;
}

/* Hex text of exactly `want` bytes. */
static bool hex_exact(Atom *x, uint8_t *out, size_t want) {
    const uint8_t *s;
    size_t length;
    return text(x, &s, &length) && length == 2u * want && hex_into(s, length, out);
}

/* Hex text of any length, decoded into the arena. */
static bool hex_any(Arena *a, Atom *x, uint8_t **out, size_t *length) {
    const uint8_t *s;
    size_t n;
    if (!text(x, &s, &n) || n % 2u) return false;
    *out = arena_alloc(a, n / 2u + 1u);
    *length = n / 2u;
    return hex_into(s, n, *out);
}

static Atom *hex_atom(Arena *a, const uint8_t *bytes, size_t length) {
    static const char digits[] = "0123456789abcdef";
    char *out = arena_alloc(a, 2u * length + 1u);
    for (size_t i = 0u; i < length; i++) {
        out[2u * i] = digits[bytes[i] >> 4];
        out[2u * i + 1u] = digits[bytes[i] & 0x0fu];
    }
    out[2u * length] = '\0';
    return atom_string_n(a, out, 2u * length);
}

static bool utf8_valid(const uint8_t *bytes, size_t length) {
    uint32_t scalar, width;
    for (size_t at = 0u; at < length; at += width)
        if (!cetta_utf8_decode_one(bytes, length, at, &scalar, &width)) return false;
    return true;
}

/* ── the curve ─────────────────────────────────────────────────────────── */

static secp256k1_context *crypto_context;
static pthread_once_t crypto_context_once = PTHREAD_ONCE_INIT;

/* One context for the process, randomized once against side channels and
 * read-only afterwards, which libsecp256k1 allows from any thread. */
static void crypto_context_create(void) {
    uint8_t seed[32];
    secp256k1_context *context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (context && (!cetta_crypto_entropy(seed, sizeof(seed)) ||
                    !secp256k1_context_randomize(context, seed))) {
        secp256k1_context_destroy(context);
        context = NULL;
    }
    cetta_crypto_wipe(seed, sizeof(seed));
    crypto_context = context;
}

static const secp256k1_context *curve(void) {
    pthread_once(&crypto_context_once, crypto_context_create);
    return crypto_context;
}

static int ecdh_x_only(unsigned char *output, const unsigned char *x32,
                       const unsigned char *y32, void *data) {
    (void)y32; (void)data;
    memcpy(output, x32, 32u);
    return 1;
}

/* ── operations ────────────────────────────────────────────────────────── */

static Atom *failure(Arena *a, const char *kind, const char *detail) {
    return atom_expr3(a, atom_symbol(a, "crypto:failed"), atom_symbol(a, kind), atom_string(a, detail));
}

static Atom *invalid_secret_key(Arena *a) {
    return failure(a, "crypto:invalid-secret-key", "expected a secret key between 1 and the curve order");
}

static Atom *boolean_value(const CettaLibraryContext *context, Arena *arena, bool value) {
    if (context && context->session.language_id == CETTA_LANGUAGE_PETTA)
        return petta_semantics_boolean_value(arena, value);
    return atom_symbol_id(arena, value ? g_builtin_syms.true_text : g_builtin_syms.false_text);
}

static Atom *xonly_public_key(Arena *a, Atom **x) {
    uint8_t secret[32], point[32];
    secp256k1_keypair keypair;
    secp256k1_xonly_pubkey key;
    Atom *value;
    if (!hex_exact(x[0], secret, 32u))
        return failure(a, "crypto:invalid-hex", "expected a 32-byte secret key in hex");
    if (!secp256k1_keypair_create(curve(), &keypair, secret)) {
        value = invalid_secret_key(a);
    } else {
        secp256k1_keypair_xonly_pub(curve(), &key, NULL, &keypair);
        secp256k1_xonly_pubkey_serialize(curve(), point, &key);
        value = hex_atom(a, point, 32u);
    }
    cetta_crypto_wipe(secret, sizeof(secret));
    cetta_crypto_wipe(&keypair, sizeof(keypair));
    return value;
}

static Atom *schnorr_sign(Arena *a, Atom **x) {
    uint8_t secret[32], aux[32], signature[64], *message;
    size_t message_length;
    secp256k1_keypair keypair;
    Atom *value;
    if (!hex_exact(x[0], secret, 32u) || !hex_any(a, x[1], &message, &message_length) ||
        !hex_exact(x[2], aux, 32u)) {
        cetta_crypto_wipe(secret, sizeof(secret));
        return failure(a, "crypto:invalid-hex",
                       "expected a 32-byte secret key, a message and 32 bytes of auxiliary randomness in hex");
    }
    secp256k1_schnorrsig_extraparams params = SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT;
    params.ndata = aux;
    if (!secp256k1_keypair_create(curve(), &keypair, secret))
        value = invalid_secret_key(a);
    else if (!secp256k1_schnorrsig_sign_custom(curve(), signature, message, message_length, &keypair, &params))
        value = failure(a, "crypto:signing-failed", "the signature could not be made");
    else
        value = hex_atom(a, signature, 64u);
    cetta_crypto_wipe(secret, sizeof(secret));
    cetta_crypto_wipe(&keypair, sizeof(keypair));
    cetta_crypto_wipe(aux, sizeof(aux));
    return value;
}

static bool schnorr_verifies(Arena *a, Atom **x) {
    uint8_t point[32], signature[64], *message;
    size_t message_length;
    secp256k1_xonly_pubkey key;
    return hex_exact(x[0], point, 32u) && hex_any(a, x[1], &message, &message_length) &&
        hex_exact(x[2], signature, 64u) && secp256k1_xonly_pubkey_parse(curve(), &key, point) &&
        secp256k1_schnorrsig_verify(curve(), signature, message, message_length, &key);
}

static Atom *nip44_conversation_key(Arena *a, Atom **x) {
    uint8_t secret[32], point[33] = {0x02u}, shared[32], key[32];
    secp256k1_pubkey peer;
    Atom *value;
    if (!hex_exact(x[0], secret, 32u) || !hex_exact(x[1], point + 1u, 32u)) {
        cetta_crypto_wipe(secret, sizeof(secret));
        return failure(a, "crypto:invalid-hex", "expected a 32-byte secret key and a 32-byte x-only public key in hex");
    }
    if (!secp256k1_ec_seckey_verify(curve(), secret)) {
        value = invalid_secret_key(a);
    } else if (!secp256k1_ec_pubkey_parse(curve(), &peer, point, sizeof(point))) {
        value = failure(a, "crypto:invalid-public-key", "expected the x coordinate of a curve point");
    } else if (!secp256k1_ecdh(curve(), shared, &peer, secret, ecdh_x_only, NULL)) {
        value = failure(a, "crypto:invalid-public-key", "the shared point is at infinity");
    } else {
        static const uint8_t salt[] = "nip44-v2";
        const uint8_t *parts[1] = {shared};
        const size_t lengths[1] = {sizeof(shared)};
        cetta_crypto_hmac_sha256(salt, sizeof(salt) - 1u, parts, lengths, 1u, key);
        value = hex_atom(a, key, sizeof(key));
    }
    cetta_crypto_wipe(secret, sizeof(secret));
    cetta_crypto_wipe(shared, sizeof(shared));
    cetta_crypto_wipe(key, sizeof(key));
    return value;
}

static Atom *nip44_failure(Arena *a, CettaNip44Status status) {
    switch (status) {
    case CETTA_NIP44_INVALID_PLAINTEXT_LENGTH:
        return failure(a, "crypto:invalid-plaintext-length", "expected a plaintext of 1 to 65535 bytes");
    case CETTA_NIP44_UNKNOWN_VERSION:
        return failure(a, "crypto:unknown-version", "unknown NIP-44 version");
    case CETTA_NIP44_INVALID_PAYLOAD_SIZE:
        return failure(a, "crypto:invalid-payload", "invalid NIP-44 payload size");
    case CETTA_NIP44_INVALID_BASE64:
        return failure(a, "crypto:invalid-payload", "invalid base64");
    case CETTA_NIP44_INVALID_DATA_SIZE:
        return failure(a, "crypto:invalid-payload", "invalid NIP-44 data size");
    case CETTA_NIP44_INVALID_MAC:
        return failure(a, "crypto:invalid-mac", "invalid NIP-44 MAC");
    case CETTA_NIP44_INVALID_PADDING:
        return failure(a, "crypto:invalid-padding", "invalid NIP-44 padding");
    case CETTA_NIP44_NO_MEMORY:
    case CETTA_NIP44_OK:
        break;
    }
    return failure(a, "crypto:unavailable", "out of memory");
}

static Atom *nip44_encrypt(Arena *a, Atom **x) {
    uint8_t conversation[32], nonce[32];
    const uint8_t *plaintext;
    size_t length, payload_length;
    char *payload;
    if (!hex_exact(x[0], conversation, 32u) || !hex_exact(x[2], nonce, 32u)) {
        cetta_crypto_wipe(conversation, sizeof(conversation));
        return failure(a, "crypto:invalid-hex", "expected a 32-byte conversation key and a 32-byte nonce in hex");
    }
    Atom *value;
    if (!text(x[1], &plaintext, &length))
        value = failure(a, "crypto:invalid-argument", "expected the plaintext as text");
    else if (length >= 1u && length <= CETTA_NIP44_PLAINTEXT_MAX && !utf8_valid(plaintext, length))
        value = failure(a, "crypto:invalid-utf8", "expected the plaintext as UTF-8");
    else {
        CettaNip44Status status = cetta_crypto_nip44_encrypt(conversation, nonce, plaintext, length,
                                                             &payload, &payload_length);
        if (status == CETTA_NIP44_OK) {
            value = atom_string_n(a, payload, payload_length);
            free(payload);
        } else {
            value = nip44_failure(a, status);
        }
    }
    cetta_crypto_wipe(conversation, sizeof(conversation));
    return value;
}

static Atom *nip44_decrypt(Arena *a, Atom **x) {
    uint8_t conversation[32], *plaintext;
    const uint8_t *payload;
    size_t length, plaintext_length;
    if (!hex_exact(x[0], conversation, 32u)) {
        cetta_crypto_wipe(conversation, sizeof(conversation));
        return failure(a, "crypto:invalid-hex", "expected a 32-byte conversation key in hex");
    }
    Atom *value;
    if (!text(x[1], &payload, &length)) {
        value = failure(a, "crypto:invalid-argument", "expected the payload as text");
    } else {
        CettaNip44Status status = cetta_crypto_nip44_decrypt(conversation, payload, length,
                                                             &plaintext, &plaintext_length);
        if (status != CETTA_NIP44_OK) {
            value = nip44_failure(a, status);
        } else {
            value = utf8_valid(plaintext, plaintext_length)
                ? atom_string_n(a, (const char *)plaintext, plaintext_length)
                : failure(a, "crypto:invalid-utf8", "the plaintext is not UTF-8");
            cetta_crypto_wipe(plaintext, plaintext_length);
            free(plaintext);
        }
    }
    cetta_crypto_wipe(conversation, sizeof(conversation));
    return value;
}

static Atom *random_hex(Arena *a, Atom *count) {
    if (!count || count->kind != ATOM_GROUNDED || count->ground.gkind != GV_INT ||
        count->ground.ival < 0 || count->ground.ival > (int64_t)RANDOM_BYTES_MAX)
        return failure(a, "crypto:invalid-argument", "expected a byte count from 0 to 65536");
    size_t length = (size_t)count->ground.ival;
    uint8_t *bytes = malloc(length ? length : 1u);
    if (!bytes) return failure(a, "crypto:unavailable", "out of memory");
    Atom *value = cetta_crypto_entropy(bytes, length) ? hex_atom(a, bytes, length)
        : failure(a, "crypto:entropy-unavailable", "the system random source failed");
    cetta_crypto_wipe(bytes, length);
    free(bytes);
    return value;
}

bool cetta_crypto_call(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args,
                       uint32_t nargs, CettaCallOutcome *out) {
    if (!head || head->kind != ATOM_SYMBOL || atom_symbol_current_id(head) != g_builtin_syms.lib_crypto)
        return false;
    Atom *value;
    const char *operation = nargs == 2u && args[0] && args[0]->kind == ATOM_SYMBOL &&
        args[1] && args[1]->kind == ATOM_EXPR ? atom_name_cstr(args[0]) : NULL;
    Atom **x = operation ? args[1]->expr.elems : NULL;
    CettaExprLen count = operation ? args[1]->expr.len : 0u;
    if (!operation)
        value = failure(a, "crypto:invalid-argument", "expected a crypto operation and inert operand tuple");
    else if (!strcmp(operation, "random-hex") && count == 1u)
        value = random_hex(a, x[0]);
    else if (!curve())
        value = failure(a, "crypto:unavailable", "the curve context could not be created");
    else if (!strcmp(operation, "xonly-public-key") && count == 1u)
        value = xonly_public_key(a, x);
    else if (!strcmp(operation, "schnorr-sign") && count == 3u)
        value = schnorr_sign(a, x);
    else if (!strcmp(operation, "schnorr-verify") && count == 3u)
        value = boolean_value(ctx, a, schnorr_verifies(a, x));
    else if (!strcmp(operation, "nip44-conversation-key") && count == 2u)
        value = nip44_conversation_key(a, x);
    else if (!strcmp(operation, "nip44-encrypt") && count == 3u)
        value = nip44_encrypt(a, x);
    else if (!strcmp(operation, "nip44-decrypt") && count == 2u)
        value = nip44_decrypt(a, x);
    else
        value = failure(a, "crypto:invalid-argument", "expected a crypto operation and inert operand tuple");
    *out = cetta_call_completed_value(value);
    return true;
}
