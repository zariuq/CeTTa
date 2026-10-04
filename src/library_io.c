#define _POSIX_C_SOURCE 200809L

#include "library_io.h"
#include "http_worker.h"

#include "symbol.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef CETTA_BUILD_HTTP_PROVIDER_CURL
#define CETTA_BUILD_HTTP_PROVIDER_CURL 0
#endif
#ifndef CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
#define CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN 0
#endif

#if CETTA_BUILD_HTTP_PROVIDER_CURL
#include <curl/curl.h>
#include <time.h>
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
#include <emscripten/fetch.h>
#include <emscripten/eventloop.h>
#endif

typedef struct CettaIoHeader {
    char *key;
    char *value;
    char *credential_secret; /* host bytes only; never an evaluator Atom */
    struct CettaIoHeader *next;
} CettaIoHeader;

typedef struct CettaIoRequest {
    uint64_t id;
    char *method;
    char *url;
    CettaIoHeader *headers;
    char *body;
    int64_t timeout_ms;
    size_t max_bytes;
    bool follow_redirects;
    char *response;
    size_t response_len;
    size_t response_cap;
    long status;
    int transport_code;
    char transport_message[256];
    bool response_too_large;
    bool response_budget_exceeded;
    bool response_has_nul;
    bool ready;
    struct CettaIoRuntime *runtime;
#if CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
    emscripten_fetch_t *fetch;
    const char **fetch_headers;
    bool provider_closing;
    bool abort_scheduled;
    int abort_immediate;
#endif
    struct CettaIoRequest *next;
    struct CettaIoRequest *ready_next;
} CettaIoRequest;

struct CettaIoRuntime {
    uint64_t next_id;
    CettaIoRequest *requests;
    CettaIoRequest *ready_head;
    CettaIoRequest *ready_tail;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    CettaHttpWorker *worker;
    uint64_t generation;
#endif
};

static char *io_strdup(const char *text) {
    size_t len = strlen(text);
    char *copy = cetta_malloc(len + 1u);
    memcpy(copy, text, len + 1u);
    return copy;
}

static const char *io_text_arg(Atom *arg) {
    if (!arg) return NULL;
    if (arg->kind == ATOM_SYMBOL) return atom_name_cstr(arg);
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING)
        return memchr(arg->ground.sval, '\0', arg->ground.slen)
            ? NULL : arg->ground.sval;
    return NULL;
}

static bool io_nonnegative_int_arg(Atom *arg, int64_t *out) {
    if (!arg || !out || arg->kind != ATOM_GROUNDED ||
        arg->ground.gkind != GV_INT || arg->ground.ival < 0)
        return false;
    *out = arg->ground.ival;
    return true;
}

static bool io_zero_arg_ok(Atom **args, uint32_t nargs) {
    return nargs == 0u ||
           (nargs == 1u && args[0] && args[0]->kind == ATOM_EXPR &&
            args[0]->expr.len == 0u);
}

static Atom *io_public_head(Arena *arena, Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL) return head;
    SymbolId id = atom_symbol_current_id(head);
    if (id == g_builtin_syms.lib_io_capabilities)
        return atom_symbol(arena, "io:capabilities");
    if (id == g_builtin_syms.lib_io_submit)
        return atom_symbol(arena, "io:submit");
    if (id == g_builtin_syms.lib_io_poll)
        return atom_symbol(arena, "io:poll");
    if (id == g_builtin_syms.lib_io_wait)
        return atom_symbol(arena, "io:wait");
    if (id == g_builtin_syms.lib_io_cancel)
        return atom_symbol(arena, "io:cancel");
    return head;
}

static Atom *io_error(Arena *arena, Atom *head, Atom **args,
                      uint32_t nargs, const char *message) {
    /* Arguments can contain credentials, including tokens embedded in URLs. */
    (void)args; (void)nargs;
    Atom *operation = io_public_head(arena, head);
    return atom_error(arena, atom_expr(arena, &operation, 1u),
                      atom_string(arena, message));
}

static void io_header_list_free(CettaIoHeader *header) {
    while (header) {
        CettaIoHeader *next = header->next;
        free(header->key);
        if (header->value) {
            volatile char *p = header->value;
            size_t n = strlen(header->value);
            while (n--) *p++ = 0;
        }
        free(header->value);
        if (header->credential_secret) {
            volatile char *p = header->credential_secret;
            size_t n = strlen(header->credential_secret);
            while (n--) *p++ = 0;
            free(header->credential_secret);
        }
        free(header);
        header = next;
    }
}

static void io_request_free(CettaIoRuntime *runtime, CettaIoRequest *request) {
    if (!request) return;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (runtime && runtime->worker && request->id)
        cetta_http_worker_abandon(runtime->worker, request->id);
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
    if (request->abort_scheduled) {
        emscripten_clear_immediate(request->abort_immediate);
        request->abort_scheduled = false;
    }
    if (request->fetch) {
        request->provider_closing = true;
        (void)emscripten_fetch_close(request->fetch);
        request->fetch = NULL;
    }
    free(request->fetch_headers);
#else
    (void)runtime;
#endif
    free(request->method);
    free(request->url);
    io_header_list_free(request->headers);
    free(request->body);
    free(request->response);
    free(request);
}

#if CETTA_BUILD_WITH_HTTP || defined(CETTA_IO_MUTATION_REPLAY_COMPLETION)
static void io_ready_append(CettaIoRuntime *runtime, CettaIoRequest *request) {
    request->ready = true;
    request->ready_next = NULL;
    if (runtime->ready_tail)
        runtime->ready_tail->ready_next = request;
    else
        runtime->ready_head = request;
    runtime->ready_tail = request;
}
#endif

static void io_ready_remove(CettaIoRuntime *runtime,
                            CettaIoRequest *request) {
    CettaIoRequest *previous = NULL;
    CettaIoRequest *cursor = runtime->ready_head;
    while (cursor && cursor != request) {
        previous = cursor;
        cursor = cursor->ready_next;
    }
    if (!cursor) return;
    if (previous)
        previous->ready_next = cursor->ready_next;
    else
        runtime->ready_head = cursor->ready_next;
    if (runtime->ready_tail == cursor) runtime->ready_tail = previous;
    cursor->ready_next = NULL;
    cursor->ready = false;
}

static void io_request_unlink(CettaIoRuntime *runtime,
                              CettaIoRequest *request) {
    CettaIoRequest **cursor = &runtime->requests;
    while (*cursor && *cursor != request) cursor = &(*cursor)->next;
    if (*cursor == request) *cursor = request->next;
    request->next = NULL;
}

static CettaIoRequest *io_request_find(CettaIoRuntime *runtime, uint64_t id) {
    for (CettaIoRequest *request = runtime ? runtime->requests : NULL;
         request; request = request->next) {
        if (request->id == id) return request;
    }
    return NULL;
}

static bool io_valid_http_method(const char *method) {
    if (!method || !method[0] || strlen(method) >= 32u) return false;
    for (const unsigned char *p = (const unsigned char *)method; *p; p++) {
        if (*p <= 32u || *p >= 127u || strchr("()<>@,;:\\\"/[]?={}", *p))
            return false;
    }
    return true;
}

static bool io_valid_header_key(const char *key) {
    return io_valid_http_method(key);
}

static bool io_valid_header_value(const char *value) {
    return value && !strchr(value, '\r') && !strchr(value, '\n');
}

static bool io_http_url(const char *url) {
    return url &&
           (strncasecmp(url, "http://", 7u) == 0 ||
            strncasecmp(url, "https://", 8u) == 0);
}

/* Host configuration authorizes a credential reference for one URL prefix.
 * Guest code selects an alias, never an environment variable or a token.
 * Prefixes include the complete authority and a trailing '/'; requests with
 * credentials never follow redirects. No secret reaches an Atom or error. */
static bool io_credential_header(const char *alias, const char *url,
                                 bool follow, CettaIoHeader *copy) {
    if (!alias || !*alias || strlen(alias) > 48u || follow) return false;
    for (const unsigned char *p = (const unsigned char *)alias; *p; ++p)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    char name[128];
    snprintf(name, sizeof(name), "CETTA_IO_AUTH_%s_URL_PREFIX", alias);
    const char *scope = getenv(name);
    if (!scope || !*scope) return false;
    size_t length = strlen(scope);
    const char *authority = strstr(scope, "://");
    if (!authority || (strncmp(scope, "https://", 8) && strncmp(scope, "http://", 7)) ||
        scope[length - 1] != '/' || strchr(authority + 3, '@') ||
        strncmp(url, scope, length) || strlen(url) <= length ||
        strstr(url, "/../") || strstr(url, "/./") || strchr(url, '\\')) return false;
    /* Do not let URL normalization escape a path-scoped credential. Encoded
     * dot/slash/backslash components have the same ambiguity at a server. */
    for (const char *p = url; *p; p++) {
        if (*p == '%' && p[1] && p[2] &&
            ((p[1] == '2' && (p[2] == 'e' || p[2] == 'E' || p[2] == 'f' || p[2] == 'F')) ||
             (p[1] == '5' && (p[2] == 'c' || p[2] == 'C')))) return false;
    }
    snprintf(name, sizeof(name), "CETTA_IO_AUTH_%s_HEADER", alias);
    const char *key = getenv(name);
    snprintf(name, sizeof(name), "CETTA_IO_AUTH_%s_ENV", alias);
    const char *variable = getenv(name);
    if (!key || !io_valid_header_key(key) || !variable || !*variable) return false;
    const char *secret = getenv(variable);
    snprintf(name, sizeof(name), "CETTA_IO_AUTH_%s_PREFIX", alias);
    const char *prefix = getenv(name);
    if (!prefix) prefix = "";
    if (!secret || !*secret || !io_valid_header_value(secret) || !io_valid_header_value(prefix) ||
        strlen(key) + strlen(prefix) + strlen(secret) + 2u > 8192u) return false;
    copy->key = io_strdup(key);
    copy->credential_secret = io_strdup(secret);
    copy->value = cetta_malloc(strlen(prefix) + strlen(secret) + 1u);
    strcpy(copy->value, prefix);
    strcat(copy->value, secret);
    return true;
}

static bool io_parse_headers(Atom *atom, const char *url, bool follow,
                             CettaIoHeader **headers_out, char *error, size_t error_size) {
    CettaIoHeader *head = NULL;
    CettaIoHeader *tail = NULL;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len > 256u) {
        snprintf(error, error_size, "expected at most 256 http:header values");
        return false;
    }
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        Atom *item = atom->expr.elems[i];
        if (item && item->kind == ATOM_EXPR && item->expr.len == 2u &&
            atom_is_symbol(item->expr.elems[0], "http:credential")) {
            CettaIoHeader *copy = cetta_malloc(sizeof(*copy));
            memset(copy, 0, sizeof(*copy));
            if (!io_credential_header(io_text_arg(item->expr.elems[1]), url, follow, copy)) {
                io_header_list_free(copy);
                io_header_list_free(head);
                snprintf(error, error_size, "credential reference is unavailable or outside its host scope");
                return false;
            }
            if (tail) tail->next = copy;
            else head = copy;
            tail = copy;
            continue;
        }
        const char *key;
        const char *value;
        if (!item || item->kind != ATOM_EXPR || item->expr.len != 3u ||
            !atom_is_symbol(item->expr.elems[0], "http:header") ||
            !(key = io_text_arg(item->expr.elems[1])) ||
            !(value = io_text_arg(item->expr.elems[2])) ||
            !io_valid_header_key(key) || !io_valid_header_value(value) ||
            strlen(key) + strlen(value) + 2u > 8192u) {
            io_header_list_free(head);
            snprintf(error, error_size,
                     "expected (http:header key value) entries without control characters");
            return false;
        }
        CettaIoHeader *copy = cetta_malloc(sizeof(*copy));
        memset(copy, 0, sizeof(*copy));
        copy->key = io_strdup(key);
        copy->value = io_strdup(value);
        copy->next = NULL;
        if (tail) tail->next = copy;
        else head = copy;
        tail = copy;
    }
    for (CettaIoHeader *a = head; a; a = a->next) {
        for (CettaIoHeader *b = a->next; b; b = b->next) {
            if ((a->credential_secret || b->credential_secret) &&
                strcasecmp(a->key, b->key) == 0) {
                io_header_list_free(head);
                snprintf(error, error_size, "credential headers must not be overridden or duplicated");
                return false;
            }
        }
    }
    *headers_out = head;
    return true;
}

static CettaIoRequest *io_parse_http_request(Atom *atom, char *error,
                                             size_t error_size) {
    const char *method;
    const char *url;
    const char *body;
    int64_t timeout_ms;
    int64_t max_bytes;
    if (!atom || atom->kind != ATOM_EXPR || (atom->expr.len != 7u && atom->expr.len != 8u) ||
        !atom_is_symbol(atom->expr.elems[0], "http:request") ||
        !(method = io_text_arg(atom->expr.elems[1])) ||
        !(url = io_text_arg(atom->expr.elems[2])) ||
        !(body = io_text_arg(atom->expr.elems[4])) ||
        !io_nonnegative_int_arg(atom->expr.elems[5], &timeout_ms) ||
        !io_nonnegative_int_arg(atom->expr.elems[6], &max_bytes) ||
        (uint64_t)timeout_ms > UINT32_MAX ||
        (uint64_t)max_bytes > SIZE_MAX - 1u ||
        strlen(body) > (size_t)LONG_MAX) {
        snprintf(error, error_size,
                 "expected (http:request method url headers body nonnegative-timeout-ms nonnegative-max-bytes)");
        return NULL;
    }
    bool follow_redirects=false;
    if (atom->expr.len==8u) {
        Atom *follow=atom->expr.elems[7];
        if (follow && follow->kind==ATOM_GROUNDED && follow->ground.gkind==GV_BOOL)
            follow_redirects=follow->ground.bval;
        else if (atom_is_symbol(follow,"True") || atom_is_symbol(follow,"true"))
            follow_redirects=true;
        else if (atom_is_symbol(follow,"False") || atom_is_symbol(follow,"false"))
            follow_redirects=false;
        else {
            snprintf(error, error_size, "follow-redirects must be Bool");
            return NULL;
        }
    }
    if (!io_valid_http_method(method)) {
        snprintf(error, error_size, "invalid HTTP method");
        return NULL;
    }
    if (!io_http_url(url)) {
        snprintf(error, error_size, "only http and https URLs are supported");
        return NULL;
    }
    if (strlen(url) > 8192u || strlen(body) > 8u * 1024u * 1024u ||
        timeout_ms > INT_MAX || max_bytes >= INT_MAX) {
        snprintf(error, error_size, "HTTP request exceeds admission limits");
        return NULL;
    }
    CettaIoRequest *request = cetta_malloc(sizeof(*request));
    memset(request, 0, sizeof(*request));
    request->method = io_strdup(method);
    request->url = io_strdup(url);
    request->body = io_strdup(body);
    request->timeout_ms = timeout_ms;
    request->max_bytes = (size_t)max_bytes;
    request->follow_redirects = follow_redirects;
    if (!io_parse_headers(atom->expr.elems[3], url, follow_redirects, &request->headers,
                          error, error_size)) {
        io_request_free(NULL, request);
        return NULL;
    }
    return request;
}

static Atom *io_http_source(Arena *arena, const CettaIoRequest *request) {
    return atom_expr2(arena, atom_symbol(arena, "io:request"),
                      atom_int(arena, (int64_t)request->id));
}

#if CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
static bool io_response_append(CettaIoRequest *request,
                               const char *data, size_t total) {
    if (total > 0u && memchr(data, '\0', total)) {
        request->response_has_nul = true;
        return false;
    }
    if (request->response_len > request->max_bytes ||
        total > request->max_bytes - request->response_len) {
        request->response_too_large = true;
        return false;
    }
    size_t needed = request->response_len + total + 1u;
    if (needed > request->response_cap) {
        size_t capacity = request->response_cap ? request->response_cap : 256u;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2u) {
                capacity = needed;
                break;
            }
            capacity *= 2u;
        }
        request->response = cetta_realloc(request->response, capacity);
        request->response_cap = capacity;
    }
    if (total > 0u)
        memcpy(request->response + request->response_len, data, total);
    request->response_len += total;
    request->response[request->response_len] = '\0';
    return true;
}
#endif

#if CETTA_BUILD_HTTP_PROVIDER_CURL

static bool io_http_start(CettaIoRuntime *runtime, CettaIoRequest *request,
                          char *error, size_t error_size) {
    if (!runtime || !runtime->worker) {
        snprintf(error, error_size, "HTTP provider is unavailable");
        return false;
    }
    size_t count = 0;
    for (CettaIoHeader *h = request->headers; h; h = h->next) ++count;
    char **headers = cetta_malloc((count + 1) * sizeof(*headers));
    size_t i = 0;
    for (CettaIoHeader *h = request->headers; h; h = h->next) {
        size_t n = strlen(h->key) + strlen(h->value) + 3;
        headers[i] = cetta_malloc(n);
        snprintf(headers[i++], n, "%s: %s", h->key, h->value);
    }
    CettaHttpRequest input = {
        .id=request->id, .method=request->method, .url=request->url,
        .headers=(const char *const *)headers, .header_count=count,
        .body=request->body, .body_size=strlen(request->body),
        .timeout_ms=(uint32_t)request->timeout_ms,
        .max_response_bytes=request->max_bytes, .follow_redirects=request->follow_redirects
    };
    CettaHttpWorkerStatus status = cetta_http_worker_submit(runtime->worker, &input);
    for (i = 0; i < count; ++i) {
        volatile char *p = headers[i];
        size_t n = strlen(headers[i]);
        while (n--) *p++ = 0;
        free(headers[i]);
    }
    free(headers);
    if (status != HTTP_WORKER_OK) {
        snprintf(error, error_size, "%s", status == HTTP_WORKER_FULL
            ? "HTTP queue capacity exceeded" : "HTTP request could not be admitted");
        return false;
    }
    return true;
}

static void io_http_pump(CettaIoRuntime *runtime) {
    if (!runtime || !runtime->worker || runtime->ready_head) return;
    CettaHttpResult result;
    /* Transfer only the completion consumed by this poll. Unconsumed bodies
     * remain charged to the worker's aggregate budget. */
    while (cetta_http_worker_take(runtime->worker, &result)) {
        CettaIoRequest *request = io_request_find(runtime, result.id);
        if (!request) { cetta_http_result_free(&result); continue; }
        request->transport_code = result.transport_code;
        snprintf(request->transport_message, sizeof(request->transport_message),
                 "%s", curl_easy_strerror((CURLcode)result.transport_code));
        request->status = result.status;
        request->response_too_large = result.response_too_large;
        request->response_budget_exceeded = result.response_budget_exceeded;
        request->response_has_nul = result.body_size && memchr(result.body, 0, result.body_size);
        request->response = (char *)result.body;
        request->response_len = result.body_size;
        io_ready_append(runtime, request);
        return;
    }
}

#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN

static void io_fetch_abort(void *user_data) {
    CettaIoRequest *request = user_data;
    if (!request) return;
    request->abort_scheduled = false;
    emscripten_fetch_t *fetch = request->fetch;
    if (!fetch || request->provider_closing || request->ready) return;
    request->provider_closing = true;
    request->fetch = NULL;
    (void)emscripten_fetch_close(fetch);
    request->provider_closing = false;
    io_ready_append(request->runtime, request);
}

static void io_fetch_schedule_abort(CettaIoRequest *request) {
    if (!request || request->abort_scheduled || !request->fetch) return;
    request->abort_immediate = emscripten_set_immediate(
        io_fetch_abort, request);
    request->abort_scheduled = true;
}

static void io_fetch_progress(emscripten_fetch_t *fetch) {
    CettaIoRequest *request = fetch ? fetch->userData : NULL;
    if (!request || request->provider_closing ||
        request->response_too_large || request->response_has_nul ||
        request->transport_code != 0)
        return;
    if (fetch->dataOffset != (uint64_t)request->response_len ||
        fetch->numBytes > SIZE_MAX) {
        request->transport_code = 1;
        snprintf(request->transport_message,
                 sizeof(request->transport_message),
                 "noncontiguous browser response stream");
        io_fetch_schedule_abort(request);
        return;
    }
    if (!io_response_append(request, fetch->data, (size_t)fetch->numBytes))
        io_fetch_schedule_abort(request);
}

static void io_fetch_finish(emscripten_fetch_t *fetch, bool failed) {
    CettaIoRequest *request = fetch ? fetch->userData : NULL;
    if (!request || request->provider_closing) return;
    if (request->abort_scheduled) {
        emscripten_clear_immediate(request->abort_immediate);
        request->abort_scheduled = false;
    }
    request->status = fetch->status;
    if (failed && fetch->status == 0u && !request->response_too_large &&
        !request->response_has_nul && request->transport_code == 0) {
        request->transport_code = 1;
        snprintf(request->transport_message,
                 sizeof(request->transport_message), "%s",
                 "browser fetch failed");
    }
    request->fetch = NULL;
    (void)emscripten_fetch_close(fetch);
    io_ready_append(request->runtime, request);
}

static void io_fetch_success(emscripten_fetch_t *fetch) {
    io_fetch_finish(fetch, false);
}

static void io_fetch_error(emscripten_fetch_t *fetch) {
    io_fetch_finish(fetch, true);
}

static bool io_fetch_header_array(CettaIoRequest *request) {
    size_t count = 0u;
    for (CettaIoHeader *header = request->headers; header;
         header = header->next)
        count++;
    if (count > (SIZE_MAX - 1u) / 2u) return false;
    request->fetch_headers = cetta_malloc(sizeof(char *) * (2u * count + 1u));
    size_t index = 0u;
    for (CettaIoHeader *header = request->headers; header;
         header = header->next) {
        request->fetch_headers[index++] = header->key;
        request->fetch_headers[index++] = header->value;
    }
    request->fetch_headers[index] = NULL;
    return true;
}

static bool io_http_start(CettaIoRuntime *runtime, CettaIoRequest *request,
                          char *error, size_t error_size) {
    /* Emscripten's XHR backend cannot suppress redirects. Refuse before any
     * networking unless the caller has explicitly opted into following. */
    if (!request->follow_redirects) {
        snprintf(error, error_size, "browser HTTP requires explicit follow-redirects=true");
        return false;
    }
    if (!runtime) {
        snprintf(error, error_size, "HTTP provider is unavailable");
        return false;
    }
    if (!io_fetch_header_array(request)) {
        snprintf(error, error_size, "cannot allocate HTTP headers");
        return false;
    }
    emscripten_fetch_attr_t attributes;
    emscripten_fetch_attr_init(&attributes);
    snprintf(attributes.requestMethod, sizeof(attributes.requestMethod), "%s",
             request->method);
    attributes.userData = request;
    attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY |
                            EMSCRIPTEN_FETCH_STREAM_DATA;
    attributes.timeoutMSecs = (uint32_t)request->timeout_ms;
    attributes.onsuccess = io_fetch_success;
    attributes.onerror = io_fetch_error;
    attributes.onprogress = io_fetch_progress;
    attributes.requestHeaders = request->fetch_headers;
    if (request->body[0]) {
        attributes.requestData = request->body;
        attributes.requestDataSize = strlen(request->body);
    }
    request->runtime = runtime;
    request->fetch = emscripten_fetch(&attributes, request->url);
    if (!request->fetch) {
        snprintf(error, error_size, "browser fetch could not start");
        return false;
    }
    return true;
}

static void io_http_pump(CettaIoRuntime *runtime) {
    (void)runtime;
}

#else

static bool io_http_start(CettaIoRuntime *runtime, CettaIoRequest *request,
                          char *error, size_t error_size) {
    (void)runtime;
    (void)request;
    snprintf(error, error_size,
             "cetta built without HTTP support (rebuild with ENABLE_HTTP=1)");
    return false;
}

static void io_http_pump(CettaIoRuntime *runtime) {
    (void)runtime;
}

#endif

CettaIoRuntime *cetta_io_runtime_new(void) {
    CettaIoRuntime *runtime = cetta_malloc(sizeof(*runtime));
    memset(runtime, 0, sizeof(*runtime));
    runtime->next_id = 1u;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    (void)cetta_http_worker_new(NULL, NULL, &runtime->worker);
#endif
    return runtime;
}

void cetta_io_runtime_free(CettaIoRuntime *runtime) {
    if (!runtime) return;
    CettaIoRequest *request = runtime->requests;
    while (request) {
        CettaIoRequest *next = request->next;
        io_request_free(runtime, request);
        request = next;
    }
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    cetta_http_worker_free(runtime->worker);
#endif
    free(runtime);
}

static Atom *io_capabilities(CettaIoRuntime *runtime, Arena *arena,
                             Atom *head, Atom **args, uint32_t nargs) {
    if (!io_zero_arg_ok(args, nargs))
        return io_error(arena, head, args, nargs,
                        "expected: (io:capabilities)");
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (runtime && runtime->worker) {
        Atom *http = atom_symbol(arena, "http");
        return atom_expr(arena, &http, 1u);
    }
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
    if (runtime) {
        Atom *http = atom_symbol(arena, "http");
        return atom_expr(arena, &http, 1u);
    }
#else
    (void)runtime;
#endif
    return atom_expr(arena, NULL, 0u);
}

static Atom *io_submit(CettaIoRuntime *runtime, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs) {
    char error[256] = {0};
    if (nargs != 1u)
        return io_error(arena, head, args, nargs,
                        "expected: (io:submit request)");
    size_t pending = 0;
    for (CettaIoRequest *r = runtime ? runtime->requests : NULL; r; r = r->next) ++pending;
    if (pending >= cetta_http_worker_default_limits().jobs)
        return io_error(arena, head, args, nargs, "HTTP queue capacity exceeded");
    CettaIoRequest *request =
        io_parse_http_request(args[0], error, sizeof(error));
    if (!request)
        return io_error(arena, head, args, nargs,
                        error[0] ? error : "unsupported I/O request");
    if (!runtime) {
        io_request_free(runtime, request);
        return io_error(arena, head, args, nargs,
                        "I/O provider unavailable");
    }
    if (runtime->next_id == 0u ||
        runtime->next_id > (uint64_t)INT64_MAX) {
        io_request_free(runtime, request);
        return io_error(arena, head, args, nargs,
                        "I/O request ID space exhausted");
    }
    request->id = runtime->next_id++;
    request->runtime = runtime;
    request->next = runtime->requests;
    runtime->requests = request;
    if (!io_http_start(runtime, request, error, sizeof(error))) {
        io_request_unlink(runtime, request);
        io_request_free(runtime, request);
        return io_error(arena, head, args, nargs,
                        error[0] ? error : "I/O provider unavailable");
    }
    return atom_expr2(arena, atom_symbol(arena, "io:pending"),
                      atom_int(arena, (int64_t)request->id));
}

static Atom *io_http_result(Arena *arena, CettaIoRequest *request) {
    for (CettaIoHeader *h = request->headers; h; h = h->next) {
        if (h->credential_secret && request->response &&
            strstr(request->response, h->credential_secret))
            return atom_error(arena, io_http_source(arena, request),
                atom_expr2(arena, atom_symbol(arena, "http:error"),
                           atom_symbol(arena, "credential-in-response")));
    }
    if (request->response_budget_exceeded)
        return atom_error(arena, io_http_source(arena, request),
            atom_expr2(arena, atom_symbol(arena, "http:error"),
                       atom_symbol(arena, "response-budget-exceeded")));
    if (request->response_too_large) {
        Atom *reason = atom_expr3(
            arena, atom_symbol(arena, "http:error"),
            atom_symbol(arena, "response-too-large"),
            atom_int(arena, (int64_t)request->max_bytes));
        return atom_error(arena, io_http_source(arena, request), reason);
    }
    if (request->response_has_nul) {
        Atom *reason = atom_expr2(
            arena, atom_symbol(arena, "http:error"),
            atom_symbol(arena, "non-text-response"));
        return atom_error(arena, io_http_source(arena, request), reason);
    }
    if (request->transport_code != 0) {
        Atom *reason = atom_expr(
            arena,
            (Atom *[]){atom_symbol(arena, "http:error"),
                       atom_symbol(arena, "transport"),
                       atom_int(arena, request->transport_code),
                       atom_string(arena, request->transport_message)},
            4u);
        return atom_error(arena, io_http_source(arena, request), reason);
    }
    return atom_expr3(
        arena, atom_symbol(arena, "http:response"),
        atom_int(arena, request->status),
        atom_string(arena, request->response ? request->response : ""));
}

static Atom *io_poll(CettaIoRuntime *runtime, Arena *arena,
                     Atom *head, Atom **args, uint32_t nargs) {
    if (!io_zero_arg_ok(args, nargs))
        return io_error(arena, head, args, nargs, "expected: (io:poll)");
    io_http_pump(runtime);
    CettaIoRequest *request = runtime ? runtime->ready_head : NULL;
    if (!request) {
        Atom *idle = atom_symbol(arena, "io:idle");
        return atom_expr(arena, &idle, 1u);
    }
    io_ready_remove(runtime, request);
    io_request_unlink(runtime, request);
    Atom *result = io_http_result(arena, request);
    Atom *event = atom_expr3(
        arena, atom_symbol(arena, "io:event"),
        atom_int(arena, (int64_t)request->id), result);
#ifndef CETTA_IO_MUTATION_REPLAY_COMPLETION
    io_request_free(runtime, request);
#else
    request->next = runtime->requests;
    runtime->requests = request;
    io_ready_append(runtime, request);
#endif
    return event;
}

static Atom *io_wait(CettaIoRuntime *runtime, Arena *arena,
                     Atom *head, Atom **args, uint32_t nargs) {
    int64_t timeout;
    if (nargs != 1u || !io_nonnegative_int_arg(args[0], &timeout) || timeout > INT_MAX)
        return io_error(arena, head, args, nargs, "expected a nonnegative timeout in milliseconds");
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (!runtime || !runtime->worker)
        return io_error(arena, head, args, nargs, "HTTP provider is unavailable");
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t deadline = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u + timeout;
    for (;;) {
        io_http_pump(runtime);
        if (runtime->ready_head) return io_poll(runtime, arena, head, NULL, 0);
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t current = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
        if (current >= deadline) return io_poll(runtime, arena, head, NULL, 0);
        runtime->generation = cetta_http_worker_wait(runtime->worker,
            runtime->generation, (uint32_t)(deadline-current));
    }
#else
    (void)runtime;
    return io_error(arena, head, args, nargs, "blocking I/O wait requires the native HTTP provider");
#endif
}

static Atom *io_cancel(CettaIoRuntime *runtime, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs) {
    int64_t signed_id;
    if (nargs != 1u || !io_nonnegative_int_arg(args[0], &signed_id) ||
        signed_id == 0)
        return io_error(arena, head, args, nargs,
                        "expected a positive request ID");
    CettaIoRequest *request =
        io_request_find(runtime, (uint64_t)signed_id);
    if (!request)
        return io_error(arena, head, args, nargs,
                        "unknown or already-consumed request ID");
    if (request->ready) io_ready_remove(runtime, request);
    io_request_unlink(runtime, request);
    io_request_free(runtime, request);
    return atom_unit(arena);
}

Atom *cetta_io_dispatch(CettaIoRuntime *runtime, Arena *arena,
                        Atom *head, Atom **args, uint32_t nargs) {
    if (!arena || !head || head->kind != ATOM_SYMBOL) return NULL;
    SymbolId id = atom_symbol_current_id(head);
    if (id == g_builtin_syms.lib_io_capabilities)
        return io_capabilities(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_submit)
        return io_submit(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_poll)
        return io_poll(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_wait)
        return io_wait(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_cancel)
        return io_cancel(runtime, arena, head, args, nargs);
    return NULL;
}
