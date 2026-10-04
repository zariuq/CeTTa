#ifndef CETTA_TELEGRAM_TRANSPORT_H
#define CETTA_TELEGRAM_TRANSPORT_H

#include "http_worker.h"

/* Immutable, native-only authority. Never serialize this reference, its token,
 * or a token-bearing URL into application data. No secret getter is provided. */
typedef struct CettaTelegramCredential CettaTelegramCredential;
typedef enum {
    TELEGRAM_CREDENTIAL_OK, TELEGRAM_CREDENTIAL_INVALID,
    TELEGRAM_CREDENTIAL_IO, TELEGRAM_CREDENTIAL_NOMEM
} CettaTelegramCredentialStatus;

typedef struct {
    const char *origin;             /* NULL: https://api.telegram.org */
    const char *ca_file;            /* NULL: system trust; verification stays on */
    bool allow_loopback_http;       /* native configuration for a local mock */
} CettaTelegramCredentialConfig;

/* Read a bounded regular descriptor at offset zero, without closing it or
 * changing its position. It must be the service user's (or root's) and give
 * others no access; the owner's group may read it. The caller opens it without following links.
 * Accept an optional final LF/CRLF. All errors are enums, never secret text.
 * Rotation creates a new object after outstanding users have finished. */
CettaTelegramCredentialStatus cetta_telegram_credential_read(
    int fd, const CettaTelegramCredentialConfig *config,
    CettaTelegramCredential **out);
void cetta_telegram_credential_free(CettaTelegramCredential *credential);
/* Stable native routing identity, derived from the public numeric bot id.
 * Token rotation preserves it. Never includes the secret token suffix. */
bool cetta_telegram_credential_source(const CettaTelegramCredential *credential,
                                      char source[65]);

/* Trusted host entry points, not evaluator builtins. Application authorization
 * and method policy remain the host's responsibility. Effects never opt into
 * repeat safety. getUpdates is reserved to the separate host poll entry;
 * setWebhook/deleteWebhook/logOut/close are refused (case-insensitively).
 * Origin, redirects, proxy and TLS policy cannot come from a proposal. */
CettaHttpWorkerStatus cetta_telegram_submit_effect(
    const CettaTelegramCredential *credential, CettaHttpWorker *worker,
    uint64_t id, const char *method, const char *content_type,
    const void *body, size_t size, uint32_t timeout_ms, size_t max_response);
CettaHttpWorkerStatus cetta_telegram_submit_poll(
    const CettaTelegramCredential *credential, CettaHttpWorker *worker,
    uint64_t id, const void *json, size_t size,
    uint32_t timeout_ms, size_t max_response);

/* Screen before journaling or constructing evaluator atoms. Recognizes the
 * secret suffix after the colon (also present in the full token), including
 * percent-escaped bytes and JSON ASCII Unicode escapes, in a
 * linear bounded scan. False means suppress the body and record a minimal
 * privacy outcome, not a fabricated API success/failure. This is not a general
 * information-flow guarantee against a malicious server encoding its secrets.
 * The host must also validate the provider response schema. */
bool cetta_telegram_response_safe(const CettaTelegramCredential *credential,
                                 const void *body, size_t size);

#endif
