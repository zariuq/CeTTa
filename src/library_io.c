#define _POSIX_C_SOURCE 200809L

#include "library_io.h"

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
#include <pthread.h>
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
#include <emscripten/fetch.h>
#include <emscripten/eventloop.h>
#endif

typedef struct CettaIoHeader {
    char *key;
    char *value;
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
    char *response;
    size_t response_len;
    size_t response_cap;
    long status;
    int transport_code;
    char transport_message[256];
    bool response_too_large;
    bool response_has_nul;
    bool ready;
    struct CettaIoRuntime *runtime;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    CURL *easy;
    struct curl_slist *curl_headers;
    bool in_multi;
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
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
    CURLM *multi;
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
        return arg->ground.sval;
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
    SymbolId id = head->sym_id;
    if (id == g_builtin_syms.lib_io_capabilities)
        return atom_symbol(arena, "io:capabilities");
    if (id == g_builtin_syms.lib_io_submit)
        return atom_symbol(arena, "io:submit");
    if (id == g_builtin_syms.lib_io_poll)
        return atom_symbol(arena, "io:poll");
    if (id == g_builtin_syms.lib_io_cancel)
        return atom_symbol(arena, "io:cancel");
    return head;
}

static Atom *io_call(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (nargs + 1u));
    items[0] = io_public_head(arena, head);
    for (uint32_t i = 0u; i < nargs; i++) items[i + 1u] = args[i];
    return atom_expr(arena, items, nargs + 1u);
}

static Atom *io_error(Arena *arena, Atom *head, Atom **args,
                      uint32_t nargs, const char *message) {
    return atom_error(arena, io_call(arena, head, args, nargs),
                      atom_string(arena, message));
}

static void io_header_list_free(CettaIoHeader *header) {
    while (header) {
        CettaIoHeader *next = header->next;
        free(header->key);
        free(header->value);
        free(header);
        header = next;
    }
}

static void io_request_free(CettaIoRuntime *runtime, CettaIoRequest *request) {
    if (!request) return;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (request->easy) {
        if (runtime && runtime->multi && request->in_multi)
            (void)curl_multi_remove_handle(runtime->multi, request->easy);
        curl_easy_cleanup(request->easy);
    }
    curl_slist_free_all(request->curl_headers);
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

static bool io_parse_headers(Atom *atom, CettaIoHeader **headers_out,
                             char *error, size_t error_size) {
    CettaIoHeader *head = NULL;
    CettaIoHeader *tail = NULL;
    if (!atom || atom->kind != ATOM_EXPR) {
        snprintf(error, error_size, "expected an expression of http:header values");
        return false;
    }
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        Atom *item = atom->expr.elems[i];
        const char *key;
        const char *value;
        if (!item || item->kind != ATOM_EXPR || item->expr.len != 3u ||
            !atom_is_symbol(item->expr.elems[0], "http:header") ||
            !(key = io_text_arg(item->expr.elems[1])) ||
            !(value = io_text_arg(item->expr.elems[2])) ||
            !io_valid_header_key(key) || !io_valid_header_value(value)) {
            io_header_list_free(head);
            snprintf(error, error_size,
                     "expected (http:header key value) entries without control characters");
            return false;
        }
        CettaIoHeader *copy = cetta_malloc(sizeof(*copy));
        copy->key = io_strdup(key);
        copy->value = io_strdup(value);
        copy->next = NULL;
        if (tail) tail->next = copy;
        else head = copy;
        tail = copy;
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
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 7u ||
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
    if (!io_valid_http_method(method)) {
        snprintf(error, error_size, "invalid HTTP method");
        return NULL;
    }
    if (!io_http_url(url)) {
        snprintf(error, error_size, "only http and https URLs are supported");
        return NULL;
    }
    CettaIoRequest *request = cetta_malloc(sizeof(*request));
    memset(request, 0, sizeof(*request));
    request->method = io_strdup(method);
    request->url = io_strdup(url);
    request->body = io_strdup(body);
    request->timeout_ms = timeout_ms;
    request->max_bytes = (size_t)max_bytes;
    if (!io_parse_headers(atom->expr.elems[3], &request->headers,
                          error, error_size)) {
        io_request_free(NULL, request);
        return NULL;
    }
    return request;
}

static Atom *io_http_source(Arena *arena, const CettaIoRequest *request) {
    uint32_t count = 0u;
    for (const CettaIoHeader *header = request->headers; header;
         header = header->next)
        count++;
    Atom **headers = arena_alloc(arena, sizeof(Atom *) * (count ? count : 1u));
    uint32_t index = 0u;
    for (const CettaIoHeader *header = request->headers; header;
         header = header->next) {
        headers[index++] = atom_expr3(
            arena, atom_symbol(arena, "http:header"),
            atom_string(arena, header->key), atom_string(arena, header->value));
    }
    Atom *items[7] = {
        atom_symbol(arena, "http:request"),
        atom_string(arena, request->method),
        atom_string(arena, request->url),
        atom_expr(arena, headers, count),
        atom_string(arena, request->body),
        atom_int(arena, request->timeout_ms),
        atom_int(arena, (int64_t)request->max_bytes),
    };
    return atom_expr(arena, items, 7u);
}

#if CETTA_BUILD_WITH_HTTP
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

static pthread_once_t io_curl_once = PTHREAD_ONCE_INIT;
static bool io_curl_ready = false;

static void io_curl_global_init_once(void) {
    io_curl_ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

static size_t io_http_write(char *data, size_t size, size_t count,
                            void *userdata) {
    CettaIoRequest *request = userdata;
    if (size != 0u && count > SIZE_MAX / size) return 0u;
    size_t total = size * count;
    return io_response_append(request, data, total) ? total : 0u;
}

static bool io_curl_header_list(CettaIoRequest *request) {
    for (CettaIoHeader *header = request->headers; header;
         header = header->next) {
        size_t key_len = strlen(header->key);
        size_t value_len = strlen(header->value);
        char *line = cetta_malloc(key_len + value_len + 3u);
        memcpy(line, header->key, key_len);
        line[key_len] = ':';
        line[key_len + 1u] = ' ';
        memcpy(line + key_len + 2u, header->value, value_len + 1u);
        struct curl_slist *next =
            curl_slist_append(request->curl_headers, line);
        free(line);
        if (!next) return false;
        request->curl_headers = next;
    }
    return true;
}

static bool io_curl_setopt(CettaIoRequest *request, char *error,
                           size_t error_size) {
#define IO_CURL_SET(option, value)                                             \
    do {                                                                       \
        CURLcode code = curl_easy_setopt(request->easy, option, value);         \
        if (code != CURLE_OK) {                                                 \
            snprintf(error, error_size, "curl option failed: %s",              \
                     curl_easy_strerror(code));                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)
    IO_CURL_SET(CURLOPT_URL, request->url);
    IO_CURL_SET(CURLOPT_WRITEFUNCTION, io_http_write);
    IO_CURL_SET(CURLOPT_WRITEDATA, request);
    IO_CURL_SET(CURLOPT_PRIVATE, request);
    IO_CURL_SET(CURLOPT_NOSIGNAL, 1L);
    IO_CURL_SET(CURLOPT_FOLLOWLOCATION, 1L);
    IO_CURL_SET(CURLOPT_MAXREDIRS, 8L);
    IO_CURL_SET(CURLOPT_PROTOCOLS_STR, "http,https");
    IO_CURL_SET(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    IO_CURL_SET(CURLOPT_ACCEPT_ENCODING, "");
    if (request->timeout_ms > 0) {
        IO_CURL_SET(CURLOPT_TIMEOUT_MS, (long)request->timeout_ms);
        IO_CURL_SET(CURLOPT_CONNECTTIMEOUT_MS, (long)request->timeout_ms);
    }
    if (request->curl_headers)
        IO_CURL_SET(CURLOPT_HTTPHEADER, request->curl_headers);
    if (strcmp(request->method, "GET") == 0) {
        IO_CURL_SET(CURLOPT_HTTPGET, 1L);
    } else {
        IO_CURL_SET(CURLOPT_CUSTOMREQUEST, request->method);
        if (request->body[0]) {
            IO_CURL_SET(CURLOPT_POSTFIELDS, request->body);
            IO_CURL_SET(CURLOPT_POSTFIELDSIZE, (long)strlen(request->body));
        }
    }
#undef IO_CURL_SET
    return true;
}

static bool io_http_start(CettaIoRuntime *runtime, CettaIoRequest *request,
                          char *error, size_t error_size) {
    if (!runtime || !runtime->multi) {
        snprintf(error, error_size, "HTTP provider is unavailable");
        return false;
    }
    request->easy = curl_easy_init();
    if (!request->easy) {
        snprintf(error, error_size, "curl_easy_init failed");
        return false;
    }
    if (!io_curl_header_list(request) ||
        !io_curl_setopt(request, error, error_size)) {
        if (!error[0]) snprintf(error, error_size, "cannot allocate HTTP headers");
        return false;
    }
    CURLMcode code = curl_multi_add_handle(runtime->multi, request->easy);
    if (code != CURLM_OK) {
        snprintf(error, error_size, "curl multi add failed: %s",
                 curl_multi_strerror(code));
        return false;
    }
    request->in_multi = true;
    return true;
}

static void io_http_pump(CettaIoRuntime *runtime) {
    if (!runtime || !runtime->multi) return;
    int running = 0;
    CURLMcode multi_code;
    do {
        multi_code = curl_multi_perform(runtime->multi, &running);
    } while (multi_code == CURLM_CALL_MULTI_PERFORM);
    if (multi_code != CURLM_OK) return;
    int remaining = 0;
    CURLMsg *message;
    while ((message = curl_multi_info_read(runtime->multi, &remaining))) {
        if (message->msg != CURLMSG_DONE) continue;
        CettaIoRequest *request = NULL;
        (void)curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &request);
        if (!request) {
            (void)curl_multi_remove_handle(runtime->multi,
                                           message->easy_handle);
            curl_easy_cleanup(message->easy_handle);
            continue;
        }
        request->transport_code = (int)message->data.result;
        snprintf(request->transport_message,
                 sizeof(request->transport_message), "%s",
                 curl_easy_strerror(message->data.result));
        (void)curl_easy_getinfo(message->easy_handle,
                                CURLINFO_RESPONSE_CODE, &request->status);
        (void)curl_multi_remove_handle(runtime->multi, request->easy);
        request->in_multi = false;
        curl_easy_cleanup(request->easy);
        request->easy = NULL;
        curl_slist_free_all(request->curl_headers);
        request->curl_headers = NULL;
        if (!request->ready) io_ready_append(runtime, request);
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
                 fetch->statusText[0] ? fetch->statusText
                                      : "browser fetch failed");
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
    (void)pthread_once(&io_curl_once, io_curl_global_init_once);
    if (io_curl_ready) runtime->multi = curl_multi_init();
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
    if (runtime->multi) curl_multi_cleanup(runtime->multi);
#endif
    free(runtime);
}

static Atom *io_capabilities(CettaIoRuntime *runtime, Arena *arena,
                             Atom *head, Atom **args, uint32_t nargs) {
    if (!io_zero_arg_ok(args, nargs))
        return io_error(arena, head, args, nargs,
                        "expected: (io:capabilities)");
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (runtime && runtime->multi) {
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
    SymbolId id = head->sym_id;
    if (id == g_builtin_syms.lib_io_capabilities)
        return io_capabilities(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_submit)
        return io_submit(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_poll)
        return io_poll(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_cancel)
        return io_cancel(runtime, arena, head, args, nargs);
    return NULL;
}
