/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "siwc.h"
#include "siwc_crypto.h"
#include "siwc_store.h"
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define SIWC_ISSUER "https://auth.openai.com"
#define SIWC_DISCOVERY SIWC_ISSUER "/.well-known/openid-configuration"
#define SIWC_API "https://api.openai.com/v1/"
#define SIWC_RESOURCE "https://api.openai.com/v1"
#define SIWC_SCOPES "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"
struct CettaSiwcOperation {
    pthread_t thread; pthread_mutex_t mutex;
    atomic_bool cancelled, ready;
    bool committed;
    int stop[2], wakeup;
    uint64_t deadline;
    unsigned port;
    long http_status;
    int transport_code;
    char *kind, id[46], *store, *issuer, *discovery, *api;
    char *authorization_url, *result, *phase;
};
static uint64_t milliseconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static const char *s(const json_t *o, const char *key) {
    json_t *value = json_object_get(o, key); const char *out = json_string_value(value);
    return out && strlen(out) == json_string_length(value) ? out : NULL;
}
static bool same(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static char *record_name(const char *id) {
    char *name = NULL;
    if (cetta_siwc_id(id) && asprintf(&name, "%s.json", id) < 0) return NULL;
    return name;
}
static char *store_path(void) {
    const char *override = getenv("CETTA_SIWC_STORE");
    if (override && *override) return strdup(override);
    const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME"); char *out = NULL;
    if (config && *config) { if (asprintf(&out, "%s/cetta/siwc", config) < 0) out = NULL; }
    else if (home && *home) { if (asprintf(&out, "%s/.config/cetta/siwc", home) < 0) out = NULL; }
    return out;
}
static void notify(CettaSiwcOperation *op) {
    if (op->wakeup >= 0) { char ch = 1; ssize_t n = write(op->wakeup, &ch, 1); (void)n; }
}
static void phase(CettaSiwcOperation *op, const char *value) {
    pthread_mutex_lock(&op->mutex); free(op->phase); op->phase = strdup(value);
    pthread_mutex_unlock(&op->mutex); notify(op);
}
static bool stopped(CettaSiwcOperation *op) {
    return atomic_load(&op->cancelled) || (op->deadline && milliseconds() >= op->deadline);
}
static const char *stop_error(CettaSiwcOperation *op) {
    return atomic_load(&op->cancelled) ? "cancelled" : "deadline";
}
static void finish(CettaSiwcOperation *op, const char *error, json_t *value) {
    json_t *out = json_pack("{s:s,s:s}", "status", error ? "error" : "ok", "registration_id", op->id);
    json_object_set_new(out, "api_base", json_string(op->api));
    if (error) json_object_set_new(out, "code", json_string(error));
    if (error) {
        json_object_set_new(out, "http_status", json_integer(op->http_status));
        json_object_set_new(out, "transport_code", json_integer(op->transport_code));
    }
    if (value) json_object_set(out, "value", value);
    pthread_mutex_lock(&op->mutex);
    op->result = json_dumps(out, JSON_COMPACT); free(op->authorization_url); op->authorization_url = NULL;
    free(op->phase); op->phase = strdup(error ? "failed" : "completed");
    pthread_mutex_unlock(&op->mutex); json_decref(out);
    atomic_store(&op->ready, true); notify(op);
}
static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static void init_curl(void) { (void)curl_global_init(CURL_GLOBAL_DEFAULT); }
typedef struct { char *bytes; size_t size; } Body;
static size_t receive(void *bytes, size_t width, size_t count, void *context) {
    Body *body = context;
    if (width && count > SIZE_MAX / width) return 0;
    size_t n = width * count;
    if (n > 1048576 - body->size) return 0;
    char *next = realloc(body->bytes, body->size + n + 1);
    if (!next) return 0;
    body->bytes = next; memcpy(next + body->size, bytes, n); body->size += n; next[body->size] = 0;
    return n;
}
static int progress(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a; (void)b; (void)c; (void)d; return stopped(context);
}
static json_t *http(CettaSiwcOperation *op, const char *url, const char *form, long *status) {
    *status = 0; Body body = {0}; CURL *curl = curl_easy_init();
    if (!curl || stopped(op)) { if (curl) curl_easy_cleanup(curl); return NULL; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "godelclaw-cetta-siwc/1");
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#ifdef CETTA_SIWC_TEST
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
    if (op->deadline) {
        uint64_t now = milliseconds(), remaining = op->deadline > now ? op->deadline - now : 1;
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, remaining > LONG_MAX ? LONG_MAX : (long)(remaining ? remaining : 1));
    }
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, op);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    if (form) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form);
    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status); curl_easy_cleanup(curl);
    op->http_status = *status; op->transport_code = (int)code;
    if (code != CURLE_OK) *status = 0;
    json_t *out = code == CURLE_OK ? json_loadb(body.bytes ? body.bytes : "", body.size, JSON_REJECT_DUPLICATES, NULL) : NULL;
    if (body.bytes) { OPENSSL_cleanse(body.bytes, body.size); free(body.bytes); }
    return out;
}
static char *form_pairs(const char *const *pairs) {
    CURL *curl = curl_easy_init(); char *out = strdup("");
    if (!curl || !out) { if (curl) curl_easy_cleanup(curl); free(out); return NULL; }
    for (size_t i = 0; pairs[i]; i += 2) {
        char *key = curl_easy_escape(curl, pairs[i], 0), *value = curl_easy_escape(curl, pairs[i+1], 0), *next = NULL;
        if (key && value && asprintf(&next, "%s%s%s=%s", out, i ? "&" : "", key, value) < 0) next = NULL;
        curl_free(key); curl_free(value); cetta_siwc_secret_free(out); out = next;
        if (!out) break;
    }
    curl_easy_cleanup(curl); return out;
}
static bool scope(const char *value) {
    const char *wanted = "chatgpt.tokens.use.direct";
    if (!value) return false;
    while (*value) {
        while (*value == ' ') ++value;
        size_t n = strcspn(value, " ");
        if (n == strlen(wanted) && !strncmp(value, wanted, n)) return true;
        value += n;
    }
    return false;
}
static bool endpoint(CettaSiwcOperation *op, const char *url) {
    if (!url) return false;
    size_t n = strlen(op->issuer);
    return !strncmp(url, op->issuer, n) && url[n] == '/' && !strchr(url, '\\') && !strchr(url, '#');
}
static json_t *discovery(CettaSiwcOperation *op, const char **error) {
    phase(op, "discovery"); long status;
    json_t *d = http(op, op->discovery, NULL, &status);
    if (status != 200 || !json_is_object(d) || !same(s(d, "issuer"), op->issuer) ||
        !endpoint(op, s(d, "authorization_endpoint")) || !endpoint(op, s(d, "token_endpoint")) ||
        !endpoint(op, s(d, "jwks_uri")) || !endpoint(op, s(d, "revocation_endpoint"))) {
        json_decref(d); *error = "discovery-invalid"; return NULL;
    }
    return d;
}
static int wait_fd(CettaSiwcOperation *op, int fd, int timeout) {
    if (stopped(op)) return -1;
    if (op->deadline) {
        uint64_t now = milliseconds(), remaining = op->deadline > now ? op->deadline - now : 0;
        if (remaining < (uint64_t)timeout) timeout = (int)remaining;
    }
    struct pollfd fds[] = {{fd, POLLIN, 0}, {op->stop[0], POLLIN, 0}};
    int n = poll(fds, 2, timeout);
    if (n < 0 && errno != EINTR) return -1;
    if (stopped(op) || fds[1].revents) return -1;
    return fds[0].revents ? 1 : 0;
}
static bool take_lock(CettaSiwcOperation *op, int dir, CettaSiwcLock *lock) {
    phase(op, "credential-lock");
    while (!stopped(op)) {
        int got = cetta_siwc_store_lock(dir, op->id, lock);
        if (got) return got == 1;
        if (wait_fd(op, -1, 50) < 0) return false;
    }
    return false;
}
static bool host_id(int dir, char id[44]) {
    /* Link a complete private file without replacing an existing host ID.
     * Concurrent creators therefore all read the same complete winner. */
    int fd = openat(dir, "host-id", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        char random[44], temporary[64];
        if (!cetta_siwc_random(id) || !cetta_siwc_random(random)) return false;
        snprintf(temporary, sizeof(temporary), ".host-%s", random);
        int created = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (created < 0) return false;
        bool ok = write(created, id, 43) == 43 && !fsync(created);
        if (close(created)) ok = false;
        if (ok && linkat(dir, temporary, dir, "host-id", 0) && errno != EEXIST) ok = false;
        if (ok && fsync(dir)) ok = false;
        unlinkat(dir, temporary, 0);
        if (!ok) return false;
        fd = openat(dir, "host-id", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    }
    struct stat st;
    bool ok = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 077);
    if (ok) {
        ok = read(fd, id, 44) == 43; id[43] = 0;
    }
    if (fd >= 0) close(fd);
    return ok;
}
typedef struct { char *code, *state, *client, *error; bool duplicate, duplicate_state; } Callback;
static void callback_clear(Callback *v) {
    cetta_siwc_secret_free(v->code); cetta_siwc_secret_free(v->state);
    free(v->client); free(v->error); memset(v, 0, sizeof(*v));
}
static char *unescape(CURL *curl, const char *value) {
    for (const char *p = value; *p; ++p) if (*p == '%' &&
        (!p[1] || !p[2] || !strchr("0123456789abcdefABCDEF", p[1]) || !strchr("0123456789abcdefABCDEF", p[2]))) return NULL;
    char *copy = strdup(value);
    if (!copy) return NULL;
    for (char *p = copy; *p; ++p) if (*p == '+') *p = ' ';
    int size = 0; char *decoded = curl_easy_unescape(curl, copy, 0, &size); free(copy);
    char *out = decoded && !memchr(decoded, 0, (size_t)size) ? strndup(decoded, (size_t)size) : NULL;
    curl_free(decoded); return out;
}
static bool callback_query(char *query, Callback *out) {
    CURL *curl = curl_easy_init(); char *save = NULL;
    if (!curl) return false;
    bool ok = true;
    for (char *part = strtok_r(query, "&", &save); part && ok; part = strtok_r(NULL, "&", &save)) {
        char *equals = strchr(part, '=');
        if (!equals) { ok = false; break; }
        *equals++ = 0; char *key = unescape(curl, part), *value = unescape(curl, equals);
        if (!key || !value) ok = false;
        else {
            char **field = same(key, "code") ? &out->code : same(key, "state") ? &out->state :
                           same(key, "client_id") ? &out->client : same(key, "error") ? &out->error : NULL;
            if (field && *field) { out->duplicate = true; if (same(key, "state")) out->duplicate_state = true; }
            else if (field) { *field = value; value = NULL; }
        }
        free(key); cetta_siwc_secret_free(value);
    }
    curl_easy_cleanup(curl); return ok;
}
static void browser_reply(int fd, int code) {
    const char *message = code == 200 ? "Authorization received. Return to godelclaw." : "Invalid callback. Continue sign-in in your browser.";
    char body[512]; int n = snprintf(body, sizeof(body),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s",
        code, code == 200 ? "OK" : "Bad Request", strlen(message), message);
    (void)send(fd, body, (size_t)n, MSG_NOSIGNAL);
}
static bool callback(CettaSiwcOperation *op, int server, const char *state, Callback *result, const char **error) {
    phase(op, "awaiting-browser");
    while (!stopped(op)) {
        if (wait_fd(op, server, 250) < 0) break;
        int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) continue;
        char request[16385]; size_t used = 0; bool headers = false;
        uint64_t child_due = milliseconds() + 5000;
        while (!stopped(op) && milliseconds() < child_due && used < sizeof(request) - 1) {
            ssize_t got = recv(fd, request + used, sizeof(request) - 1 - used, 0);
            if (got > 0) { used += (size_t)got; request[used] = 0; if (strstr(request, "\r\n\r\n")) { headers = true; break; } }
            else if (!got || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
            else if (wait_fd(op, fd, 100) < 0) break;
        }
        bool accepted = false; Callback value = {0};
        char *line = headers ? strstr(request, "\r\n") : NULL;
        if (line) {
            char *end_headers = strstr(request, "\r\n\r\n");
            bool valid = end_headers && end_headers + 4 == request + used && !memchr(request, 0, used);
            bool length_seen = false;
            for (char *h = line + 2; valid && h < end_headers;) {
                char *next = strstr(h, "\r\n");
                if (!next || h[0] == ' ' || h[0] == '\t' || !memchr(h, ':', (size_t)(next-h))) { valid = false; break; }
                if (!strncasecmp(h, "Transfer-Encoding:", 18)) valid = false;
                if (!strncasecmp(h, "Content-Length:", 15)) {
                    char *tail; errno = 0; long length = strtol(h+15, &tail, 10);
                    while (tail < next && (*tail == ' ' || *tail == '\t')) ++tail;
                    if (length_seen || errno || tail == h+15 || tail != next || length != 0) valid = false;
                    length_seen = true;
                }
                h = next+2;
            }
            if (!valid) line = NULL;
        }
        if (line) {
            *line = 0;
            char *end = strchr(request + (used >= 4 ? 4 : 0), ' ');
            if (!strncmp(request, "GET /auth/callback?", 19) && end &&
                (!strcmp(end + 1, "HTTP/1.1") || !strcmp(end + 1, "HTTP/1.0"))) {
                *end = 0;
                bool parsed = callback_query(request + 19, &value);
                bool state_ok = parsed && value.state && strlen(state) == strlen(value.state) &&
                    !CRYPTO_memcmp(state, value.state, strlen(state)) && !value.duplicate_state;
                if (state_ok && (value.code || value.error)) {
                    accepted = true;
                    if (value.duplicate || (value.code && value.error) || (!value.code && !value.error)) *error = "callback-protocol-error";
                    else if (value.error) *error = same(value.error, "access_denied") ? "authorization-denied" : "authorization-error";
                    else if (!*value.code) *error = "callback-protocol-error";
                    else { *result = value; memset(&value, 0, sizeof(value)); }
                }
            }
        }
        browser_reply(fd, accepted ? 200 : 400); close(fd); callback_clear(&value);
        OPENSSL_cleanse(request, sizeof(request));
        if (accepted) return !*error;
    }
    *error = stop_error(op); return false;
}
static bool fresh(json_t *record, int margin) {
    const char *token = s(record, "access_token");
    json_t *expiry = json_object_get(record, "access_expires_at");
    return token && *token && json_is_integer(expiry) && json_integer_value(expiry) > (int64_t)time(NULL) + margin;
}
static void clear_tokens(json_t *record) {
    const char *keys[] = {"access_token", "refresh_token", "id_token", "nonce", NULL};
    for (size_t i = 0; keys[i]; ++i) json_object_del(record, keys[i]);
}
static bool token_update(CettaSiwcOperation *op, int dir, const char *filename, json_t *record,
    json_t *tokens, json_t *d, const char *nonce, bool refresh, const char **error) {
    const char *access = s(tokens, "access_token"), *renew = s(tokens, "refresh_token"), *jwt = s(tokens, "id_token");
    json_t *lifetime = json_object_get(tokens, "expires_in");
    *error = "token-response-invalid";
    if (!access || !*access || strlen(access) > 8000 || strpbrk(access, "\r\n") || !renew || !*renew ||
        (!refresh && !jwt) || (json_object_get(tokens, "id_token") && !jwt) ||
        (json_object_get(tokens, "scope") && !s(tokens, "scope")) ||
        !s(tokens, "token_type") || strcasecmp(s(tokens, "token_type"), "Bearer") ||
        !json_is_integer(lifetime) || json_integer_value(lifetime) <= 0 || json_integer_value(lifetime) > INT64_MAX - (int64_t)time(NULL)) return false;
    const char *scopes = s(tokens, "scope");
    if (!scopes && refresh) scopes = s(record, "scopes");
    *error = "scope-missing";
    if (!scope(scopes)) return false;
    json_t *claims = NULL;
    if (jwt) {
        phase(op, "verifying-identity"); long status;
        json_t *jwks = http(op, s(d, "jwks_uri"), NULL, &status);
        if (status != 200 || !jwks) { json_decref(jwks); *error = "jwks-unavailable"; return false; }
        claims = cetta_siwc_verify(jwt, jwks, op->issuer, s(record, "client_id"), nonce, time(NULL), error);
        json_decref(jwks);
        if (!claims) return false;
        if (s(record, "sub") && !same(s(record, "sub"), s(claims, "sub"))) { *error = "identity-mismatch"; json_decref(claims); return false; }
        if (refresh && json_object_get(claims, "nonce") && !same(s(claims, "nonce"), s(record, "nonce"))) { *error = "nonce-mismatch"; json_decref(claims); return false; }
    } else if (!refresh) return false;
    if (stopped(op)) { *error = stop_error(op); json_decref(claims); return false; }
    json_t *next = json_deep_copy(record);
    if (claims) {
        const char *names[] = {"sub", "email", "name", NULL};
        for (size_t i = 0; names[i]; ++i) if (json_object_get(claims, names[i])) json_object_set(next, names[i], json_object_get(claims, names[i]));
        json_object_set_new(next, "id_token", json_string(jwt));
    }
    json_object_set_new(next, "access_token", json_string(access));
    json_object_set_new(next, "refresh_token", json_string(renew));
    json_object_set_new(next, "scopes", json_string(scopes));
    json_object_set_new(next, "access_expires_at", json_integer((json_int_t)time(NULL) + json_integer_value(lifetime)));
    json_object_set_new(next, "renewal_state", json_string("ready"));
    json_object_set_new(next, "generation", json_integer(json_integer_value(json_object_get(record, "generation")) + 1));
    if (nonce) json_object_set_new(next, "nonce", json_string(nonce));
    if (json_object_get(tokens, "earliest_refresh_at")) json_object_set(next, "earliest_refresh_at", json_object_get(tokens, "earliest_refresh_at"));
    pthread_mutex_lock(&op->mutex);
    bool ok = !stopped(op) && cetta_siwc_store_write(dir, filename, next);
    if (ok) op->committed = true;
    pthread_mutex_unlock(&op->mutex);
    json_decref(next); json_decref(claims); *error = ok ? NULL : "credential-store-error"; return ok;
}
static void *run(void *data) {
    CettaSiwcOperation *op = data; const char *error = NULL;
    int dir = cetta_siwc_store_open(op->store), server = -1;
    CettaSiwcLock lock = {.file = -1}; json_t *record = NULL, *d = NULL, *tokens = NULL, *value = NULL;
    char *filename = record_name(op->id), *form = NULL, *selected_client = NULL;
    char verifier[44] = {0}, nonce[44] = {0}, state[44] = {0}; Callback cb = {0};
    if (dir < 0) { error = "credential-store-error"; goto done; }
    if (same(op->kind, "accounts")) { value = cetta_siwc_store_accounts(dir); if (!value) error = "credential-store-error"; goto done; }
    if (!filename) { error = "registration-invalid"; goto done; }
    record = cetta_siwc_store_read(dir, filename);
    struct stat entry;
    if (!record && !fstatat(dir, filename, &entry, AT_SYMLINK_NOFOLLOW)) {
        error = "credential-record-unreadable"; goto done;
    }
    if (record && (!same(s(record, "issuer"), op->issuer) || !same(s(record, "registration_id"), op->id))) {
        error = "credential-record-invalid"; goto done;
    }
    if (same(op->kind, "ensure") && fresh(record, 60)) goto success;
    if (!same(op->kind, "sign-in")) {
        if (!record || !s(record, "client_id") || !s(record, "sub")) { error = "sign-in-required"; goto done; }
        if (!take_lock(op, dir, &lock)) { error = stopped(op) ? stop_error(op) : "credential-store-error"; goto done; }
        json_decref(record); record = cetta_siwc_store_read(dir, filename);
        if (!record) { error = "credential-store-error"; goto done; }
        if (same(op->kind, "ensure") && fresh(record, 60)) goto success;
        if (!s(record, "refresh_token")) {
            if (same(op->kind, "sign-out")) {
                clear_tokens(record); json_object_set_new(record, "renewal_state", json_string("signed-out"));
                if (!cetta_siwc_store_write(dir, filename, record)) error = "credential-store-error";
                goto success;
            }
            error = "sign-in-required"; goto done;
        }
        if (same(op->kind, "ensure") && !same(s(record, "renewal_state"), "ready")) {
            if (fresh(record, 0)) goto success;
            error = "refresh-outcome-unknown"; goto done;
        }
        if (same(op->kind, "sign-out")) {
            json_object_del(record, "access_token");
            json_object_set_new(record, "renewal_state", json_string("signing-out"));
            if (!cetta_siwc_store_write(dir, filename, record)) { error = "credential-store-error"; goto done; }
        }
    }
    d = discovery(op, &error);
    if (!d) {
        if (same(op->kind, "sign-out")) {
            clear_tokens(record); json_object_set_new(record, "renewal_state", json_string("signed-out"));
            error = cetta_siwc_store_write(dir, filename, record) ? "revocation-unconfirmed" : "credential-store-error";
            goto success;
        }
        goto done;
    }
    if (same(op->kind, "sign-in")) {
        char host[44], challenge[44];
        if (!host_id(dir, host) || !cetta_siwc_random(verifier) || !cetta_siwc_random(nonce) ||
            !cetta_siwc_random(state) || !cetta_siwc_challenge(verifier, challenge)) { error = "random-unavailable"; goto done; }
        server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(op->port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        socklen_t addr_size = sizeof(addr);
        if (server < 0 || bind(server, (struct sockaddr *)&addr, sizeof(addr)) || listen(server, 8) ||
            getsockname(server, (struct sockaddr *)&addr, &addr_size)) { error = "callback-bind-failed"; goto done; }
        char redirect[96]; snprintf(redirect, sizeof(redirect), "http://127.0.0.1:%u/auth/callback", ntohs(addr.sin_port));
        selected_client = s(record, "client_id") ? strdup(s(record, "client_id")) : NULL;
        const char *client = selected_client; bool returning = client && *client;
        const char *pairs[] = {"client_id", returning ? client : "dynamic_agent_client", "ext_agent_host_id", host,
            "response_type", "code", "redirect_uri", redirect, "scope", SIWC_SCOPES, "resource", SIWC_RESOURCE,
            "state", state, "nonce", nonce, "code_challenge", challenge, "code_challenge_method", "S256",
            returning ? NULL : "agent_name_hint", "godelclaw", NULL};
        form = form_pairs(pairs); if (!form) { error = "allocation-error"; goto done; }
        pthread_mutex_lock(&op->mutex);
        if (asprintf(&op->authorization_url, "%s?%s", s(d, "authorization_endpoint"), form) < 0) op->authorization_url = NULL;
        pthread_mutex_unlock(&op->mutex); cetta_siwc_secret_free(form); form = NULL;
        if (!op->authorization_url) { error = "allocation-error"; goto done; }
        if (!callback(op, server, state, &cb, &error)) goto done;
        close(server); server = -1;
        if ((!returning && (!cb.client || !*cb.client || same(cb.client, "dynamic_agent_client"))) ||
            (returning && cb.client && !same(cb.client, client))) { error = returning ? "client-id-mismatch" : "client-id-missing"; goto done; }
        if (!take_lock(op, dir, &lock)) { error = stopped(op) ? stop_error(op) : "credential-store-error"; goto done; }
        json_decref(record); record = cetta_siwc_store_read(dir, filename);
        if (!record) record = json_pack("{s:i,s:s,s:s,s:s}", "version", 1, "registration_id", op->id,
                                      "issuer", op->issuer, "client_id", cb.client);
        if (!same(s(record, "client_id"), cb.client ? cb.client : client)) { error = "client-id-mismatch"; goto done; }
        /* Retain the issued registration on invalid_grant, without marking an
         * unverified identity or any new credentials as signed in. */
        if (!cetta_siwc_store_write(dir, filename, record)) { error = "credential-store-error"; goto done; }
        const char *exchange[] = {"grant_type", "authorization_code", "client_id", s(record, "client_id"),
            "code", cb.code, "code_verifier", verifier, "redirect_uri", redirect, "resource", SIWC_RESOURCE, NULL};
        form = form_pairs(exchange);
    } else if (same(op->kind, "ensure")) {
        phase(op, "refreshing");
        json_object_set_new(record, "renewal_state", json_string("unknown"));
        if (!cetta_siwc_store_write(dir, filename, record)) { error = "credential-store-error"; goto done; }
        const char *refresh[] = {"grant_type", "refresh_token", "client_id", s(record, "client_id"),
            "refresh_token", s(record, "refresh_token"), "resource", SIWC_RESOURCE, NULL};
        form = form_pairs(refresh);
    } else if (same(op->kind, "sign-out")) {
        const char *revoke[] = {"token", s(record, "refresh_token"), "token_type_hint", "refresh_token", "client_id", s(record, "client_id"), NULL};
        form = form_pairs(revoke); long status = 0;
        if (form) tokens = http(op, s(d, "revocation_endpoint"), form, &status);
        clear_tokens(record); json_object_set_new(record, "renewal_state", json_string("signed-out"));
        if (!cetta_siwc_store_write(dir, filename, record)) error = "credential-store-error";
        else if (status != 200) error = "revocation-unconfirmed";
        goto success;
    } else { error = "operation-invalid"; goto done; }
    if (!form) { error = "allocation-error"; goto done; }
    phase(op, "token-exchange"); long status;
    tokens = http(op, s(d, "token_endpoint"), form, &status);
    if (status != 200 || !json_is_object(tokens)) {
        if (same(s(tokens, "error"), "invalid_grant")) {
            error = "invalid-grant";
            if (same(op->kind, "ensure")) {
                clear_tokens(record); json_object_set_new(record, "renewal_state", json_string("sign-in-required"));
                if (!cetta_siwc_store_write(dir, filename, record)) error = "credential-store-error";
            }
        } else error = same(op->kind, "ensure") ? "refresh-outcome-unknown" : "token-http-error";
        goto done;
    }
    if (!token_update(op, dir, filename, record, tokens, d,
                       same(op->kind, "sign-in") ? nonce : NULL, same(op->kind, "ensure"), &error)) goto done;
    json_decref(record); record = cetta_siwc_store_read(dir, filename);
success:
    value = cetta_siwc_public_account(record);
done:
    if (stopped(op)) error = stop_error(op);
    finish(op, error, value);
    cetta_siwc_store_unlock(&lock);
    if (server >= 0) close(server);
    if (dir >= 0) close(dir);
    json_decref(record); json_decref(d); json_decref(tokens); json_decref(value);
    free(filename); free(selected_client); cetta_siwc_secret_free(form); callback_clear(&cb);
    OPENSSL_cleanse(verifier, sizeof(verifier)); OPENSSL_cleanse(nonce, sizeof(nonce)); OPENSSL_cleanse(state, sizeof(state));
    return NULL;
}
bool cetta_siwc_available(void) { return true; }
CettaSiwcOperation *cetta_siwc_start(const char *kind, const char *id, unsigned port, uint64_t deadline, int wakeup) {
    if (!kind || port > 65535 || (strcmp(kind, "accounts") && strcmp(kind, "ensure") && strcmp(kind, "sign-in") && strcmp(kind, "sign-out"))) return NULL;
    if ((!id || !*id || same(id, "new")) && !same(kind, "sign-in") && !same(kind, "accounts")) return NULL;
    if (id && *id && !same(id, "new") && !cetta_siwc_id(id)) return NULL;
    pthread_once(&curl_once, init_curl);
    CettaSiwcOperation *op = calloc(1, sizeof(*op));
    if (!op) return NULL;
    op->stop[0] = op->stop[1] = -1; op->wakeup = wakeup; op->port = port;
    pthread_mutex_init(&op->mutex, NULL); atomic_init(&op->ready, false); atomic_init(&op->cancelled, false);
    op->kind = strdup(kind); op->store = store_path(); op->issuer = strdup(SIWC_ISSUER);
    op->discovery = strdup(SIWC_DISCOVERY); op->api = strdup(SIWC_API); op->phase = strdup("starting");
#ifdef CETTA_SIWC_TEST
    const char *test = getenv("CETTA_SIWC_TEST_ISSUER");
    if (test && !strncmp(test, "http://127.0.0.1:", 17)) {
        free(op->issuer); op->issuer = strdup(test); free(op->discovery); free(op->api);
        op->discovery = NULL; op->api = NULL;
        if (asprintf(&op->discovery, "%s/.well-known/openid-configuration", test) < 0) op->discovery = NULL;
        if (asprintf(&op->api, "%s/v1/", test) < 0) op->api = NULL;
    }
#endif
    if (same(kind, "accounts")) op->id[0] = 0;
    else if (id && cetta_siwc_id(id)) strcpy(op->id, id);
    else { char random[44]; if (!cetta_siwc_random(random)) goto fail; snprintf(op->id, sizeof(op->id), "r_%s", random); }
    if (!op->kind || !op->store || !op->issuer || !op->discovery || !op->api || !op->phase || pipe2(op->stop, O_CLOEXEC | O_NONBLOCK)) goto fail;
    op->deadline = deadline ? milliseconds() + deadline : 0;
    if (pthread_create(&op->thread, NULL, run, op)) goto fail;
    return op;
fail:
    if (op->stop[0] >= 0) close(op->stop[0]);
    if (op->stop[1] >= 0) close(op->stop[1]);
    free(op->kind); free(op->store); free(op->issuer); free(op->discovery); free(op->api); free(op->phase);
    pthread_mutex_destroy(&op->mutex); free(op); return NULL;
}
bool cetta_siwc_ready(CettaSiwcOperation *op) { return op && atomic_load(&op->ready); }
char *cetta_siwc_status(CettaSiwcOperation *op) {
    pthread_mutex_lock(&op->mutex);
    json_t *out = json_pack("{s:s,s:s}", "phase", op->phase, "registration_id", op->id);
    json_object_set_new(out, "ready", json_boolean(atomic_load(&op->ready)));
    if (op->authorization_url) json_object_set_new(out, "authorization_url", json_string(op->authorization_url));
    char *value = json_dumps(out, JSON_COMPACT); json_decref(out);
    pthread_mutex_unlock(&op->mutex); return value;
}
char *cetta_siwc_result(CettaSiwcOperation *op) {
    if (!cetta_siwc_ready(op)) return NULL;
    pthread_mutex_lock(&op->mutex); char *out = op->result ? strdup(op->result) : NULL;
    pthread_mutex_unlock(&op->mutex); return out;
}
void cetta_siwc_free(CettaSiwcOperation *op) {
    if (!op) return;
    pthread_mutex_lock(&op->mutex);
    if (!op->committed) atomic_store(&op->cancelled, true);
    pthread_mutex_unlock(&op->mutex);
    char ch = 1; ssize_t n = write(op->stop[1], &ch, 1); (void)n;
    pthread_join(op->thread, NULL); close(op->stop[0]); close(op->stop[1]);
    free(op->kind); free(op->store); free(op->issuer); free(op->discovery); free(op->api);
    free(op->authorization_url); free(op->result); free(op->phase);
    pthread_mutex_destroy(&op->mutex); free(op);
}
char *cetta_siwc_bearer(const char *alias, const char *url) {
    if (!alias || strncmp(alias, "SIWC:", 5) || !cetta_siwc_id(alias + 5) || !url) return NULL;
    const char *api = SIWC_API;
#ifdef CETTA_SIWC_TEST
    char test_api[1024]; const char *test = getenv("CETTA_SIWC_TEST_ISSUER");
    if (test && !strncmp(test, "http://127.0.0.1:", 17)) { snprintf(test_api, sizeof(test_api), "%s/v1/", test); api = test_api; }
#endif
    size_t n = strlen(api);
    if (strncmp(url, api, n) || !url[n] || strchr(url, '\\') || strchr(url, '#') || strstr(url, "/../") || strstr(url, "/./")) return NULL;
    for (const char *p = url; *p && *p != '?'; ++p) if (*p == '%' && p[1] && p[2] &&
        ((p[1] == '2' && strchr("eEfF", p[2])) || (p[1] == '5' && strchr("cC", p[2])))) return NULL;
    char *path = store_path(); int dir = cetta_siwc_store_open(path); free(path);
    char *name = record_name(alias + 5);
    json_t *record = dir >= 0 && name ? cetta_siwc_store_read(dir, name) : NULL;
    char *token = same(s(record, "registration_id"), alias + 5) && scope(s(record, "scopes")) && fresh(record, 0) ? strdup(s(record, "access_token")) : NULL;
    json_decref(record); free(name); if (dir >= 0) close(dir); return token;
}
