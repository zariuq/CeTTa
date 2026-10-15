/* SPDX-License-Identifier: MIT */
#include "siwc_crypto.h"
#include <cjose/cjose.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

void cetta_siwc_secret_free(char *text) {
    if (text) { OPENSSL_cleanse(text, strlen(text)); free(text); }
}
static void base64url32(const unsigned char bytes[32], char out[44]) {
    unsigned char encoded[45];
    EVP_EncodeBlock(encoded, bytes, 32);
    for (size_t i = 0; i < 43; ++i)
        out[i] = encoded[i] == '+' ? '-' : encoded[i] == '/' ? '_' : (char)encoded[i];
    out[43] = 0;
}
bool cetta_siwc_random(char out[44]) {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) return false;
    base64url32(bytes, out); OPENSSL_cleanse(bytes, sizeof(bytes)); return true;
}
bool cetta_siwc_challenge(const char *verifier, char out[44]) {
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned size = 0;
    if (!verifier || EVP_Digest(verifier, strlen(verifier), digest, &size, EVP_sha256(), NULL) != 1 || size != 32)
        return false;
    base64url32(digest, out); OPENSSL_cleanse(digest, sizeof(digest)); return true;
}
static json_t *protected_header(const char *jwt) {
    const char *dot = strchr(jwt, '.');
    if (!dot || dot == jwt || (size_t)(dot - jwt) > 8192) return NULL;
    size_t n = (size_t)(dot - jwt), padded = (n + 3) / 4 * 4;
    if (n % 4 == 1) return NULL;
    unsigned char *input = malloc(padded + 1), *decoded = malloc(padded + 1);
    if (!input || !decoded) { free(input); free(decoded); return NULL; }
    for (size_t i = 0; i < padded; ++i) {
        char ch = i < n ? jwt[i] : '=';
        if (i < n && !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
            free(input); free(decoded); return NULL;
        }
        input[i] = ch == '-' ? '+' : ch == '_' ? '/' : (unsigned char)ch;
    }
    input[padded] = 0;
    int got = EVP_DecodeBlock(decoded, input, (int)padded);
    json_t *header = got < (int)(padded - n) ? NULL :
        json_loadb((const char *)decoded, (size_t)got - (padded - n), JSON_REJECT_DUPLICATES, NULL);
    free(input); free(decoded); return header;
}
static const char *text(const json_t *obj, const char *key) {
    json_t *value = json_object_get(obj, key);
    const char *out = json_string_value(value);
    return out && strlen(out) == json_string_length(value) ? out : NULL;
}
static bool equal(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static bool audience(const json_t *claims, const char *client) {
    json_t *aud = json_object_get(claims, "aud");
    const char *scalar = json_string_value(aud);
    bool found = scalar && strlen(scalar) == json_string_length(aud) && equal(scalar, client);
    if (json_is_array(aud)) {
        found = false;
        for (size_t i = 0; i < json_array_size(aud); ++i) {
            json_t *item = json_array_get(aud, i);
            if (!json_is_string(item) || strlen(json_string_value(item)) != json_string_length(item)) return false;
            found |= equal(json_string_value(item), client);
        }
        if (json_array_size(aud) > 1 && !equal(text(claims, "azp"), client)) return false;
    }
    json_t *azp = json_object_get(claims, "azp");
    return found && (!azp || equal(text(claims, "azp"), client));
}
json_t *cetta_siwc_verify(const char *jwt, const json_t *jwks, const char *issuer,
                         const char *client, const char *nonce, int64_t now,
                         const char **error) {
    *error = "jwt-format";
    if (!jwt || strlen(jwt) > 65536 || !issuer || !client || now < 0 || now > INT64_MAX - 5) return NULL;
    json_t *header = protected_header(jwt), *claims = NULL;
    cjose_err err = {0}; cjose_jwk_t *key = NULL; cjose_jws_t *jws = NULL;
    if (!json_is_object(header)) goto done;
    *error = "jwt-alg-rejected";
    if (!equal(text(header, "alg"), "RS256") || json_object_get(header, "b64") ||
        (json_object_get(header, "crit") && (!json_is_array(json_object_get(header, "crit")) ||
          json_array_size(json_object_get(header, "crit"))))) goto done;
    const char *kid = text(header, "kid");
    if (!kid || !*kid || strlen(kid) > 512) goto done;
    *error = "jwks-key-not-found";
    json_t *keys = json_object_get(jwks, "keys"), *selected = NULL; size_t i; json_t *candidate;
    if (!json_is_array(keys)) goto done;
    json_array_foreach(keys, i, candidate) {
        if (!equal(text(candidate, "kid"), kid) || !equal(text(candidate, "kty"), "RSA")) continue;
        const char *use = text(candidate, "use"), *alg = text(candidate, "alg");
        if ((json_object_get(candidate, "use") && !equal(use, "sig")) ||
            (json_object_get(candidate, "alg") && !equal(alg, "RS256"))) continue;
        json_t *ops = json_object_get(candidate, "key_ops");
        if (ops) {
            bool verify = false; size_t k; json_t *op;
            if (!json_is_array(ops)) continue;
            json_array_foreach(ops, k, op) {
                const char *name = json_string_value(op);
                verify |= name && strlen(name) == json_string_length(op) && equal(name, "verify");
            }
            if (!verify) continue;
        }
        if (selected) { *error = "jwks-ambiguous-key"; goto done; }
        selected = candidate;
    }
    if (!selected) goto done;
    char *jwk = json_dumps(selected, JSON_COMPACT);
    if (!jwk) goto done;
    key = cjose_jwk_import(jwk, strlen(jwk), &err); free(jwk);
    *error = "jwt-signature-invalid";
    jws = cjose_jws_import(jwt, strlen(jwt), &err);
    if (!key || !jws || !cjose_jws_verify(jws, key, &err)) goto done;
    uint8_t *payload = NULL; size_t size = 0;
    if (!cjose_jws_get_plaintext(jws, &payload, &size, &err)) goto done;
    claims = json_loadb((const char *)payload, size, JSON_REJECT_DUPLICATES, NULL);
    *error = "jwt-claims-invalid";
    if (!json_is_object(claims) || !text(claims, "sub") || !*text(claims, "sub")) goto invalid;
    *error = "issuer-mismatch";
    if (!equal(text(claims, "iss"), issuer)) goto invalid;
    *error = "audience-mismatch";
    if (!audience(claims, client)) goto invalid;
    *error = "token-expired";
    json_t *exp = json_object_get(claims, "exp"), *iat = json_object_get(claims, "iat"), *nbf = json_object_get(claims, "nbf");
    if (!json_is_integer(exp) || json_integer_value(exp) <= now - 5) goto invalid;
    *error = "jwt-time-invalid";
    if (!json_is_integer(iat) || json_integer_value(iat) < 0 || json_integer_value(iat) > now + 5 ||
        (nbf && (!json_is_integer(nbf) || json_integer_value(nbf) > now + 5))) goto invalid;
    *error = "nonce-mismatch";
    const char *received = text(claims, "nonce");
    if (nonce && (!received || strlen(received) != strlen(nonce) || CRYPTO_memcmp(received, nonce, strlen(nonce)))) goto invalid;
    *error = NULL; goto done;
invalid:
    json_decref(claims); claims = NULL;
done:
    if (jws) cjose_jws_release(jws);
    if (key) cjose_jwk_release(key);
    json_decref(header); return claims;
}
