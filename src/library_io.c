#define _GNU_SOURCE

#include "library_io.h"
#include "http_worker.h"
#include "library_proc_run.h"
#include "proc_owner.h"
#include "ipc_client.h"
#include "library_cwp.h"
#include "atom_wire.h"
#include "host_notice.h"
#include "siwc.h"

#include "symbol.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif
#endif

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
    enum { IO_HTTP, IO_PROCESS, IO_TIMER, IO_CWP, IO_SPACE, IO_NOTICE, IO_FILE, IO_SIWC } provider;
    CettaSiwcOperation *siwc;
    int file_fd;
    char *file_directory, *file_name;
    char **file_names;
    size_t file_name_count;
    uint32_t file_mask;
    uint64_t notice_generation;
    CettaIpcClient *ipc;
    char *space_request_id;
    size_t space_request_id_size;
    CettaProcChild *process;
    CettaProcSnapshot process_status;
    char *process_out, *process_err;
    uint64_t timer_started, timer_due;
    uint64_t id;
    char *method;
    char *url;
    CettaIoHeader *headers;
    char *body;
    int64_t timeout_ms;
    uint32_t connect_timeout_ms, first_byte_timeout_ms, idle_timeout_ms;
    size_t max_bytes;
    bool follow_redirects;
    char *response;
    size_t response_len;
    size_t response_cap;
    long status;
    int64_t retry_after;
    bool response_metadata;
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
#ifndef __EMSCRIPTEN__
    int wakeup[2];
    CettaProcOwner *process_owner;
    CettaHostNoticeRegistration *notice_registration;
    uint64_t observation_sequence;
#endif
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
    if (!arg || !out || arg->kind != ATOM_GROUNDED) return false;
    if (arg->ground.gkind == GV_INT) {
        if (arg->ground.ival < 0) return false;
        *out = arg->ground.ival;
    } else if (arg->ground.gkind == GV_FLOAT) {
        double value = arg->ground.fval;
        if (!isfinite(value) || value < 0 || value >= 0x1p63 || trunc(value) != value) return false;
        *out = (int64_t)value;
    } else return false;
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
    if (id == g_builtin_syms.lib_io_status)
        return atom_symbol(arena, "io:status");
    if (id == g_builtin_syms.lib_io_wait)
        return atom_symbol(arena, "io:wait");
    if (id == g_builtin_syms.lib_io_wait_ready)
        return atom_symbol(arena, "io:wait-ready");
    if (id == g_builtin_syms.lib_io_cancel)
        return atom_symbol(arena, "io:cancel");
    if (id == g_builtin_syms.lib_io_signal)
        return atom_symbol(arena, "io:signal");
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
    cetta_siwc_free(request->siwc);
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (runtime && runtime->worker && request->id && request->provider == IO_HTTP)
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
#ifndef __EMSCRIPTEN__
    cetta_proc_free(request->process);
    cetta_ipc_client_free(request->ipc);
    if (runtime && request->provider == IO_PROCESS && request->id)
        cetta_proc_owner_abandon(runtime->process_owner, request->id);
#endif
    free(request->process_out); free(request->process_err);
    free(request->space_request_id);
#ifndef __EMSCRIPTEN__
    if (request->provider == IO_FILE && request->file_fd >= 0) close(request->file_fd);
#endif
    free(request->file_directory); free(request->file_name);
    for (size_t i = 0; i < request->file_name_count; ++i) free(request->file_names[i]);
    free(request->file_names);
    free(request);
}

static void io_ready_append(CettaIoRuntime *runtime, CettaIoRequest *request) {
    request->ready = true;
    request->ready_next = NULL;
    if (runtime->ready_tail)
        runtime->ready_tail->ready_next = request;
    else
        runtime->ready_head = request;
    runtime->ready_tail = request;
}

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
    if (alias && !strncmp(alias, "SIWC:", 5)) {
        if (follow) return false;
        char *secret = cetta_siwc_bearer(alias, url);
        if (!secret || !*secret || !io_valid_header_value(secret) || strlen(secret) > 8000u) {
            free(secret); return false;
        }
        copy->key = io_strdup("Authorization"); copy->credential_secret = secret;
        copy->value = cetta_malloc(strlen(secret) + 8u);
        strcpy(copy->value, "Bearer "); strcat(copy->value, secret); return true;
    }
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
    if (!atom || atom->kind != ATOM_EXPR || (atom->expr.len < 7u || atom->expr.len > 9u) ||
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
    if (atom->expr.len>=8u) {
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
    request->retry_after = -1;
    request->method = io_strdup(method);
    request->url = io_strdup(url);
    request->body = io_strdup(body);
    request->timeout_ms = timeout_ms;
    request->max_bytes = (size_t)max_bytes;
    request->follow_redirects = follow_redirects;
    if (atom->expr.len==9u) {
        Atom *options=atom->expr.elems[8]; int64_t connect, first, idle;
        if (!options || options->kind!=ATOM_EXPR || (options->expr.len!=4u && options->expr.len!=5u) ||
            !atom_is_symbol(options->expr.elems[0],"http:timeouts") ||
            !io_nonnegative_int_arg(options->expr.elems[1],&connect) ||
            !io_nonnegative_int_arg(options->expr.elems[2],&first) ||
            !io_nonnegative_int_arg(options->expr.elems[3],&idle) ||
            connect>INT_MAX || first>INT_MAX || idle>INT_MAX) {
            snprintf(error,error_size,"expected (http:timeouts connect-ms first-byte-ms idle-ms)");
            io_request_free(NULL,request); return NULL;
        }
#if CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
        if (connect || first || idle) {
            snprintf(error,error_size,"independent HTTP timeouts require the native provider");
            io_request_free(NULL,request); return NULL;
        }
#endif
        if (options->expr.len==5u) {
            Atom *metadata=options->expr.elems[4];
            if (!atom_is_symbol(metadata,"retry-after")) {
                snprintf(error,error_size,"expected retry-after response metadata option");
                io_request_free(NULL,request); return NULL;
            }
            request->response_metadata=true;
        }
        request->connect_timeout_ms=(uint32_t)connect;
        request->first_byte_timeout_ms=(uint32_t)first;
        request->idle_timeout_ms=(uint32_t)idle;
    }
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
        .connect_timeout_ms=request->connect_timeout_ms,
        .first_byte_timeout_ms=request->first_byte_timeout_ms,
        .idle_timeout_ms=request->idle_timeout_ms,
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

static void io_http_pump(CettaIoRuntime *runtime, uint64_t selected_id) {
    if (!runtime || !runtime->worker || (!selected_id && runtime->ready_head)) return;
    CettaIoRequest *selected = selected_id ? io_request_find(runtime, selected_id) : NULL;
    if (selected_id && (!selected || selected->ready)) return;
    CettaHttpResult result;
    /* Transfer only the completion consumed by this poll. Unconsumed bodies
     * remain charged to the worker's aggregate budget. */
    while (selected_id ? cetta_http_worker_take_id(runtime->worker, selected_id, &result)
                       : cetta_http_worker_take(runtime->worker, &result)) {
        CettaIoRequest *request = io_request_find(runtime, result.id);
        if (!request) { cetta_http_result_free(&result); continue; }
        request->transport_code = result.transport_code;
        snprintf(request->transport_message, sizeof(request->transport_message),
                 "%s", curl_easy_strerror((CURLcode)result.transport_code));
        request->status = result.status;
        request->retry_after = result.retry_after;
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

static void io_http_pump(CettaIoRuntime *runtime, uint64_t selected_id) {
    (void)runtime;
    (void)selected_id;
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

static void io_http_pump(CettaIoRuntime *runtime, uint64_t selected_id) {
    (void)runtime;
    (void)selected_id;
}

#endif

static uint64_t io_now_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

#ifdef __linux__
static void io_file_pump(CettaIoRequest *request) {
    union { struct inotify_event alignment; char bytes[4096]; } buffer;
    ssize_t size;
    while ((size = read(request->file_fd, buffer.bytes, sizeof(buffer.bytes))) > 0) {
        for (size_t offset = 0; offset + sizeof(struct inotify_event) <= (size_t)size;) {
            struct inotify_event *event = (struct inotify_event *)(buffer.bytes + offset);
            if (event->len > (size_t)size - offset - sizeof(*event)) return;
            bool selected = !request->file_name_count ||
                (event->mask & (IN_Q_OVERFLOW | IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED));
            for (size_t i = 0; !selected && i < request->file_name_count; ++i)
                if (event->len && !strcmp(request->file_names[i], event->name)) selected = true;
            if (selected) {
                request->file_mask = event->mask;
                request->file_name = io_strdup(event->len ? event->name : "");
                return;
            }
            offset += sizeof(*event) + event->len;
        }
    }
}
#endif

static void io_pump(CettaIoRuntime *runtime, uint64_t selected_id) {
    if (!runtime) return;
    io_http_pump(runtime, selected_id);
    uint64_t now = io_now_ms();
    for (CettaIoRequest *r = runtime->requests; r; r = r->next) {
        if (r->ready) continue;
        if (r->provider == IO_SIWC && cetta_siwc_ready(r->siwc)) io_ready_append(runtime, r);
#ifndef __EMSCRIPTEN__
#ifdef __linux__
        if (r->provider == IO_FILE) {
            io_file_pump(r);
            if (r->file_name) io_ready_append(runtime, r);
        }
#endif
        if (r->provider == IO_CWP || r->provider == IO_SPACE) {
            cetta_ipc_client_pump(r->ipc);
            if (cetta_ipc_client_ready(r->ipc)) io_ready_append(runtime, r);
        }
        if (r->provider == IO_NOTICE &&
            cetta_host_notice_generation() != r->notice_generation) io_ready_append(runtime, r);
        if (r->provider == IO_PROCESS &&
            cetta_proc_owner_status(runtime->process_owner, r->id, &r->process_status) &&
            r->process_status.finished) io_ready_append(runtime, r);
#endif
        if (r->provider == IO_TIMER && now >= r->timer_due) io_ready_append(runtime, r);
    }
}

CettaIoRuntime *cetta_io_runtime_new(void) {
    CettaIoRuntime *runtime = cetta_malloc(sizeof(*runtime));
    memset(runtime, 0, sizeof(*runtime));
    runtime->next_id = 1u;
#ifndef __EMSCRIPTEN__
    if (pipe2(runtime->wakeup, O_CLOEXEC | O_NONBLOCK)) { free(runtime); return NULL; }
    runtime->process_owner = cetta_proc_owner_new(runtime->wakeup[1]);
    runtime->notice_registration = cetta_host_notice_register(runtime->wakeup[1]);
#endif
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    (void)cetta_http_worker_new(NULL, NULL, &runtime->worker);
#ifndef __EMSCRIPTEN__
    cetta_http_worker_set_wakeup_fd(runtime->worker, runtime->wakeup[1]);
#endif
#endif
    return runtime;
}

void cetta_io_runtime_free(CettaIoRuntime *runtime) {
    if (!runtime) return;
#ifndef __EMSCRIPTEN__
    cetta_host_notice_unregister(runtime->notice_registration);
#endif
    CettaIoRequest *request = runtime->requests;
    while (request) {
        CettaIoRequest *next = request->next;
        io_request_free(runtime, request);
        request = next;
    }
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    cetta_http_worker_free(runtime->worker);
#endif
#ifndef __EMSCRIPTEN__
    cetta_proc_owner_free(runtime->process_owner);
    close(runtime->wakeup[0]); close(runtime->wakeup[1]);
#endif
    free(runtime);
}

static Atom *io_capabilities(CettaIoRuntime *runtime, Arena *arena,
                             Atom *head, Atom **args, uint32_t nargs) {
    if (!io_zero_arg_ok(args, nargs))
        return io_error(arena, head, args, nargs,
                        "expected: (io:capabilities)");
    Atom *providers[8]; size_t count = 0;
    if (runtime && cetta_siwc_available()) providers[count++] = atom_symbol(arena, "siwc");
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    if (runtime && runtime->worker) providers[count++] = atom_symbol(arena, "http");
#elif CETTA_BUILD_HTTP_PROVIDER_EMSCRIPTEN
    if (runtime) providers[count++] = atom_symbol(arena, "http");
#endif
#ifndef __EMSCRIPTEN__
    if (runtime) {
        if (runtime->process_owner) providers[count++] = atom_symbol(arena, "proc");
        providers[count++] = atom_symbol(arena, "timer");
        providers[count++] = atom_symbol(arena, "cwp");
        providers[count++] = atom_symbol(arena, "space");
        if (runtime->notice_registration) providers[count++] = atom_symbol(arena, "host-notice");
#ifdef __linux__
        providers[count++] = atom_symbol(arena, "file-watch");
#endif
    }
#endif
    return atom_expr(arena, providers, count);
}

#ifndef __EMSCRIPTEN__
static CettaIoRequest *io_parse_ipc(Atom *input, char *error, size_t error_size) {
    bool cwp = atom_is_symbol(input->expr.elems[0], "cwp:request");
    int64_t timeout, code = 0;
    if (input->expr.len != (cwp ? 6u : 4u) ||
        !io_nonnegative_int_arg(input->expr.elems[cwp ? 5 : 3], &timeout)) goto invalid;
    const char *path = io_text_arg(input->expr.elems[1]);
    if (!path) goto invalid;
    CettaIoRequest *request = calloc(1, sizeof(*request));
    if (!request) return NULL;
    request->provider = cwp ? IO_CWP : IO_SPACE;
    unsigned char *bytes = NULL; size_t size = 0;
    if (cwp) {
        const char *name = io_text_arg(input->expr.elems[3]), *body = io_text_arg(input->expr.elems[4]);
        if (!name || !body || !io_nonnegative_int_arg(input->expr.elems[2], &code) ||
            code < 1 || code > 63 || strlen(name) > 64 || strlen(body) > 65536) goto failed;
        for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
            if (*p < 33 || *p > 126) goto failed;
        size = 6 + strlen(name) + strlen(body); bytes = malloc(size);
        if (!bytes) goto failed;
        memcpy(bytes, "CWP1", 4); bytes[4] = (unsigned char)code; bytes[5] = (unsigned char)strlen(name);
        memcpy(bytes + 6, name, strlen(name)); memcpy(bytes + 6 + strlen(name), body, strlen(body));
    } else {
        Atom *value = input->expr.elems[2];
        if (!value || value->kind != ATOM_EXPR || value->expr.len < 3 ||
            !atom_is_symbol(value->expr.elems[0], "request")) goto failed;
        Atom *id = value->expr.elems[1];
        if (!id || id->kind != ATOM_GROUNDED || id->ground.gkind != GV_STRING) goto failed;
        request->space_request_id_size = id->ground.slen;
        request->space_request_id = malloc(id->ground.slen + 1);
        if (!request->space_request_id) goto failed;
        memcpy(request->space_request_id, id->ground.sval, id->ground.slen);
        request->space_request_id[id->ground.slen] = 0;
        if (cetta_atom_wire_encode(value, &bytes, &size) != ATOM_WIRE_OK) goto failed;
    }
    request->ipc = cetta_ipc_client_new(path, cwp ? CETTA_IPC_CWP : CETTA_IPC_CSP,
        bytes, size, (uint64_t)timeout, cwp ? 6 + 255 + 65536 : CETTA_ATOM_WIRE_BYTES);
    free(bytes);
    if (request->ipc) return request;
    io_request_free(NULL, request);
    goto invalid;
failed:
    free(bytes); io_request_free(NULL, request);
invalid:
    snprintf(error, error_size, "expected native cwp:request or space:request with valid framing and nonnegative deadline");
    return NULL;
}

static Atom *io_ipc_result(Arena *arena, CettaIoRequest *request) {
    const char *error = cetta_ipc_client_error(request->ipc);
    if (request->provider == IO_CWP) {
        if (error) return atom_expr2(arena, atom_symbol(arena, "cwp:unavailable"), atom_symbol(arena, error));
        size_t size; const unsigned char *bytes = cetta_ipc_client_reply(request->ipc, &size);
        return cetta_cwp_decode_reply(arena, bytes, size);
    }
    Atom *id = atom_string_n(arena, request->space_request_id, request->space_request_id_size);
    if (!error) {
        size_t size; const unsigned char *bytes = cetta_ipc_client_reply(request->ipc, &size);
        Atom *reply = NULL;
        if (cetta_atom_wire_decode(arena, bytes, size, &reply) == ATOM_WIRE_OK &&
            reply && reply->kind == ATOM_EXPR && reply->expr.len == 4 &&
            atom_is_symbol(reply->expr.elems[0], "reply") &&
            reply->expr.elems[1]->kind == ATOM_GROUNDED &&
            reply->expr.elems[1]->ground.gkind == GV_STRING && atom_string_equal(id, reply->expr.elems[1]) &&
            (atom_is_symbol(reply->expr.elems[2], "ok") || atom_is_symbol(reply->expr.elems[2], "error"))) return reply;
        error = "malformed-reply";
    }
    return atom_expr3(arena, atom_symbol(arena,
        cetta_ipc_client_sent(request->ipc) ? "space:uncertain" : "space:unavailable"),
        id, atom_symbol(arena, error));
}
#endif

static Atom *io_submit(CettaIoRuntime *runtime, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs) {
    char error[256] = {0};
    if (nargs != 1u)
        return io_error(arena, head, args, nargs,
                        "expected: (io:submit request)");
    if (!runtime || !runtime->next_id || runtime->next_id > (uint64_t)INT64_MAX)
        return io_error(arena, head, args, nargs, "I/O runtime or request identity is unavailable");
    CettaIoRequest *request = NULL;
#ifndef __EMSCRIPTEN__
    Atom *input = args[0];
    if (input && input->kind == ATOM_EXPR && input->expr.len && atom_is_symbol(input->expr.elems[0], "siwc:request")) {
        int64_t port, deadline;
        const char *kind = input->expr.len == 5 ? io_text_arg(input->expr.elems[1]) : NULL;
        const char *registration = input->expr.len == 5 ? io_text_arg(input->expr.elems[2]) : NULL;
        if (!kind || !registration || !io_nonnegative_int_arg(input->expr.elems[3], &port) || port > 65535 ||
            !io_nonnegative_int_arg(input->expr.elems[4], &deadline))
            return io_error(arena, head, args, nargs, "expected (siwc:request kind registration port deadline-ms)");
        request = calloc(1, sizeof(*request));
        if (request) {
            request->provider = IO_SIWC;
            request->siwc = cetta_siwc_start(kind, registration, (unsigned)port, (uint64_t)deadline, runtime->wakeup[1]);
            if (!request->siwc) { free(request); request = NULL; snprintf(error, sizeof(error), "SIWC provider unavailable or invalid operation"); }
        }
    } else
#ifdef __linux__
    if (input && input->kind == ATOM_EXPR && input->expr.len &&
        atom_is_symbol(input->expr.elems[0], "fs:changed")) {
        const char *directory = input->expr.len >= 2 ? io_text_arg(input->expr.elems[1]) : NULL;
        Atom *names = input->expr.len == 3 ? input->expr.elems[2] : NULL;
        if (!directory || input->expr.len > 3 || (names && names->kind != ATOM_EXPR))
            return io_error(arena, head, args, nargs, "expected (fs:changed directory [names])");
        request = calloc(1, sizeof(*request));
        if (!request) return io_error(arena, head, args, nargs, "file-watch allocation failed");
        request->provider = IO_FILE; request->file_fd = -1;
        request->file_directory = io_strdup(directory);
        if (names) {
            request->file_names = calloc(names->expr.len, sizeof(char *));
            if (names->expr.len && !request->file_names) {
                io_request_free(NULL, request);
                return io_error(arena, head, args, nargs, "file-watch allocation failed");
            }
            for (size_t i = 0; i < names->expr.len; ++i) {
                const char *name = io_text_arg(names->expr.elems[i]);
                if (!name) { io_request_free(NULL, request); return io_error(arena, head, args, nargs, "file-watch names must be text"); }
                request->file_names[request->file_name_count++] = io_strdup(name);
            }
        }
        request->file_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (request->file_fd < 0 || inotify_add_watch(request->file_fd, directory,
            IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF) < 0) {
            io_request_free(NULL, request);
            return io_error(arena, head, args, nargs, "file-watch directory is unavailable");
        }
    } else
#endif
    if (input && input->kind == ATOM_EXPR && input->expr.len == 1 &&
        atom_is_symbol(input->expr.elems[0], "host:notice")) {
        if (!runtime->notice_registration) return io_error(arena, head, args, nargs, "host notifier is unavailable");
        request = calloc(1, sizeof(*request));
        if (request) { request->provider = IO_NOTICE; request->notice_generation = cetta_host_notice_generation(); }
    } else if (input && input->kind == ATOM_EXPR && input->expr.len &&
        (atom_is_symbol(input->expr.elems[0], "cwp:request") ||
         atom_is_symbol(input->expr.elems[0], "space:request"))) {
        request = io_parse_ipc(input, error, sizeof(error));
    } else if (input && input->kind == ATOM_EXPR && input->expr.len &&
        atom_is_symbol(input->expr.elems[0], "timer:after")) {
        int64_t delay;
        if (input->expr.len != 2 || !io_nonnegative_int_arg(input->expr.elems[1], &delay))
            return io_error(arena, head, args, nargs, "expected (timer:after nonnegative-milliseconds)");
        request = calloc(1, sizeof(*request));
        if (request) {
            request->provider = IO_TIMER;
            request->timer_started = io_now_ms();
            request->timer_due = request->timer_started + (uint64_t)delay;
        }
    } else if (input && input->kind == ATOM_EXPR && input->expr.len &&
               atom_is_symbol(input->expr.elems[0], "proc:request")) {
        int64_t timeout;
        if (input->expr.len != 7 || !io_nonnegative_int_arg(input->expr.elems[4], &timeout))
            return io_error(arena, head, args, nargs, "expected (proc:request argv cwd env timeout-ms stdout-file stderr-file)");
        request = calloc(1, sizeof(*request));
        if (request) {
            request->provider = IO_PROCESS;
            if (!runtime->process_owner) {
                free(request);
                return io_error(arena, head, args, nargs, "native process owner is unavailable");
            }
            request->process = cetta_proc_start(input->expr.elems[1], io_text_arg(input->expr.elems[2]),
                input->expr.elems[3], (uint64_t)timeout, io_text_arg(input->expr.elems[5]),
                io_text_arg(input->expr.elems[6]), error, sizeof(error));
            if (!request->process) { free(request); request = NULL; }
            else {
                request->process_out = io_strdup(io_text_arg(input->expr.elems[5]));
                request->process_err = io_strdup(io_text_arg(input->expr.elems[6]));
            }
        }
    } else
#endif
    {
        size_t pending = 0;
        for (CettaIoRequest *r = runtime ? runtime->requests : NULL; r; r = r->next)
            if (r->provider == IO_HTTP) ++pending;
        if (pending >= cetta_http_worker_default_limits().jobs)
            return io_error(arena, head, args, nargs, "HTTP queue capacity exceeded");
        request = io_parse_http_request(args[0], error, sizeof(error));
    }
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
#ifndef __EMSCRIPTEN__
    if (request->provider == IO_PROCESS) {
        if (!cetta_proc_owner_add(runtime->process_owner, request->id, request->process)) {
            io_request_free(runtime, request);
            return io_error(arena, head, args, nargs, "could not register native process");
        }
        request->process = NULL; /* owner alone now signals, polls and frees */
    }
#endif
    request->next = runtime->requests;
    runtime->requests = request;
    if (request->provider == IO_HTTP && !io_http_start(runtime, request, error, sizeof(error))) {
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
    if (request->response_metadata) {
        Atom *items[] = {atom_symbol(arena,"http:response-with-metadata"), atom_int(arena,request->status),
            atom_string(arena,request->response?request->response:""),
            atom_expr2(arena,atom_symbol(arena,"http:retry-after"),atom_int(arena,request->retry_after))};
        return atom_expr(arena,items,4u);
    }
    return atom_expr3(
        arena, atom_symbol(arena, "http:response"),
        atom_int(arena, request->status),
        atom_string(arena, request->response ? request->response : ""));
}

#ifndef __EMSCRIPTEN__
static Atom *io_process_result(Arena *arena, CettaIoRequest *request) {
    CettaProcSnapshot *s = &request->process_status;
    const char *kind = s->error ? "failed" : s->timed_out ? "timed-out"
                     : WIFEXITED(s->status) ? "exited" : "signaled";
    int code = s->error ? s->error : s->timed_out ? 0
             : WIFEXITED(s->status) ? WEXITSTATUS(s->status) : WTERMSIG(s->status);
    return atom_expr(arena, (Atom *[]){ atom_symbol(arena, "proc:finished"),
        atom_int(arena, s->pid), atom_symbol(arena, kind), atom_int(arena, code),
        atom_string(arena, request->process_out), atom_string(arena, request->process_err) }, 6);
}
#endif

static Atom *io_poll(CettaIoRuntime *runtime, Arena *arena,
                     Atom *head, Atom **args, uint32_t nargs) {
    int64_t selected_id = 0;
    bool selected = !io_zero_arg_ok(args, nargs);
    if (selected && (nargs != 1u ||
        !io_nonnegative_int_arg(args[0], &selected_id) || selected_id == 0))
        return io_error(arena, head, args, nargs,
                        "expected: (io:poll) or (io:poll positive-id)");
    io_pump(runtime, selected ? selected_id : 0u);
    CettaIoRequest *request = selected
        ? io_request_find(runtime, (uint64_t)selected_id)
        : runtime ? runtime->ready_head : NULL;
    if (selected && !request)
        return atom_expr2(arena, atom_symbol(arena, "io:unknown"),
                          atom_int(arena, selected_id));
    if (request && !request->ready) request = NULL;
    if (!request) {
        Atom *idle = atom_symbol(arena, "io:idle");
        return atom_expr(arena, &idle, 1u);
    }
    io_ready_remove(runtime, request);
    io_request_unlink(runtime, request);
    char *siwc_result = request->provider == IO_SIWC ? cetta_siwc_result(request->siwc) : NULL;
    Atom *result = request->provider == IO_SIWC ? atom_expr2(arena, atom_symbol(arena, "siwc:result"),
        atom_string(arena, siwc_result ? siwc_result : "{\"status\":\"error\",\"code\":\"result-unavailable\"}")) :
#ifndef __EMSCRIPTEN__
                 request->provider == IO_PROCESS ? io_process_result(arena, request) :
                 request->provider == IO_CWP || request->provider == IO_SPACE ? io_ipc_result(arena, request) :
                 request->provider == IO_NOTICE ? atom_expr2(arena, atom_symbol(arena, "host:noticed"),
                    atom_int(arena, (int64_t)cetta_host_notice_generation())) :
                 request->provider == IO_FILE ? atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:changed"),
                    atom_string(arena, request->file_directory), atom_string(arena, request->file_name),
                    atom_int(arena, request->file_mask)}, 4) :
#endif
                 request->provider == IO_TIMER ? atom_expr2(arena, atom_symbol(arena, "timer:fired"),
                    atom_int(arena, (int64_t)(request->timer_due - request->timer_started)))
                 : io_http_result(arena, request);
    Atom *event = atom_expr3(
        arena, atom_symbol(arena, "io:event"),
        atom_int(arena, (int64_t)request->id), result);
    free(siwc_result);
#ifndef CETTA_IO_MUTATION_REPLAY_COMPLETION
    io_request_free(runtime, request);
#else
    request->next = runtime->requests;
    runtime->requests = request;
    io_ready_append(runtime, request);
#endif
    return event;
}

static Atom *io_status(CettaIoRuntime *runtime, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs) {
    int64_t id;
    if (nargs!=1u || !io_nonnegative_int_arg(args[0],&id) || !id)
        return io_error(arena,head,args,nargs,"expected (io:status positive-id)");
    CettaIoRequest *request=io_request_find(runtime,(uint64_t)id);
    if (!request) return atom_expr2(arena,atom_symbol(arena,"io:unknown"),atom_int(arena,id));
    if (request->provider == IO_SIWC) {
        char *status = cetta_siwc_status(request->siwc);
        Atom *out = atom_expr2(arena, atom_symbol(arena, "siwc:progress"), atom_string(arena, status ? status : "{}"));
        free(status); return out;
    }
    if (request->provider != IO_HTTP) {
        io_pump(runtime, id);
#ifndef __EMSCRIPTEN__
        if (request->provider == IO_CWP || request->provider == IO_SPACE || request->provider == IO_NOTICE || request->provider == IO_FILE)
            return atom_expr3(arena, atom_symbol(arena, "ipc:status"), atom_int(arena, id),
                atom_symbol(arena, request->ready ? "ready" : "waiting"));
        if (request->provider == IO_PROCESS)
            return request->ready ? io_process_result(arena, request)
                : atom_expr(arena, (Atom *[]){ atom_symbol(arena, "proc:running"),
                    atom_int(arena, request->process_status.pid),
                    atom_int(arena, (int64_t)request->process_status.elapsed_ms),
                    atom_string(arena, request->process_out),
                    atom_string(arena, request->process_err) }, 5);
#endif
        Atom *parts[] = {atom_symbol(arena, "timer:status"), atom_symbol(arena, request->ready ? "ready" : "waiting"),
            atom_int(arena, (int64_t)(io_now_ms() - request->timer_started))};
        return atom_expr(arena, parts, 3u);
    }
    CettaHttpProgress progress={0};
    bool available=false;
#if CETTA_BUILD_HTTP_PROVIDER_CURL
    available=cetta_http_worker_progress(runtime->worker,(uint64_t)id,&progress);
#endif
    if (!available && !request->ready)
        return io_error(arena,head,args,nargs,"HTTP progress is unavailable");
    return atom_expr(arena,(Atom *[]){atom_symbol(arena,"io:progress"),atom_int(arena,id),
        atom_symbol(arena,request->ready || progress.terminal?"ready":"waiting"),
        atom_int(arena,(int64_t)progress.elapsed_ms),
        atom_int(arena,(int64_t)(request->ready?request->response_len:progress.response_bytes)),
        atom_int(arena,(int64_t)progress.quiet_ms)},6u);
}

static bool io_selected(CettaIoRequest *request, bool selected, uint64_t id, Atom *ids) {
    if (!request) return false;
    if (ids) {
        for (size_t i = 1; i < ids->expr.len; ++i) {
            int64_t member;
            if (io_nonnegative_int_arg(ids->expr.elems[i], &member) && (uint64_t)member == request->id) return true;
        }
        return false;
    }
    return !selected || request->id == id;
}

static Atom *io_wait(CettaIoRuntime *runtime, Arena *arena,
                     Atom *head, Atom **args, uint32_t nargs, bool readiness) {
    int64_t timeout, selected_id = 0;
    const char *progress = "";
    bool selected = nargs == 3u || (nargs == 2u && !io_text_arg(args[1]));
    Atom *ids = readiness && nargs >= 2 && args[0] && args[0]->kind == ATOM_EXPR &&
        args[0]->expr.len && atom_is_symbol(args[0]->expr.elems[0], "io:ids") ? args[0] : NULL;
    if (ids) {
        for (size_t i = 1; i < ids->expr.len; ++i) {
            int64_t id;
            if (!io_nonnegative_int_arg(ids->expr.elems[i], &id) || !id)
                return io_error(arena, head, args, nargs, "readiness IDs must be positive integers");
        }
    }
    if (nargs < 1u || nargs > 3u ||
        !io_nonnegative_int_arg(args[selected ? 1u : 0u], &timeout) ||
        (selected && !ids && (!io_nonnegative_int_arg(args[0], &selected_id) || !selected_id)))
        return io_error(arena, head, args, nargs, "expected timeout-ms or id timeout-ms, with optional progress path");
    if (nargs == 3u || (nargs == 2u && !selected)) {
        progress = io_text_arg(args[nargs - 1]);
        if (!progress) return io_error(arena, head, args, nargs, "progress path must be text");
    }
#ifndef __EMSCRIPTEN__
    if (!runtime) return io_error(arena, head, args, nargs, "I/O runtime is unavailable");
    uint64_t deadline = io_now_ms() + (uint64_t)timeout, observation = 0;
    bool observed = progress[0] || getenv("NOTIFY_SOCKET");
    for (;;) {
        char wake[256];
        while (read(runtime->wakeup[0], wake, sizeof(wake)) > 0) {}
        io_pump(runtime, selected ? selected_id : 0u);
        CettaIoRequest *target = selected && !ids ? io_request_find(runtime, selected_id) : runtime->ready_head;
        if (ids) {
            while (target && !io_selected(target, selected, selected_id, ids)) target = target->ready_next;
        }
        if ((selected && !ids && !target) || (target && target->ready) || io_now_ms() >= deadline) {
            if (readiness) {
                if (selected && !ids && !target) return atom_expr2(arena, atom_symbol(arena, "io:unknown"), atom_int(arena, selected_id));
                size_t count = 0;
                for (CettaIoRequest *r = runtime->ready_head; r; r = r->ready_next)
                    if (io_selected(r, selected, selected_id, ids)) ++count;
                if (!count) return atom_expr(arena, (Atom *[]){ atom_symbol(arena, "io:idle") }, 1);
                Atom **ready_ids = cetta_malloc(count * sizeof(*ready_ids)); size_t n = 0;
                for (CettaIoRequest *r = runtime->ready_head; r; r = r->ready_next)
                    if (io_selected(r, selected, selected_id, ids)) ready_ids[n++] = atom_int(arena, (int64_t)r->id);
                Atom *result = atom_expr2(arena, atom_symbol(arena, "io:ready"), atom_expr(arena, ready_ids, count));
                free(ready_ids); return result;
            }
            return io_poll(runtime, arena, head, selected ? args : NULL, selected ? 1u : 0u);
        }
        uint64_t now = io_now_ms(), due = deadline;
        if (observed) {
            if (now >= observation) { cetta_proc_observe_wait(progress, ++runtime->observation_sequence); observation = now + 30000; }
            if (observation < due) due = observation;
        }
        size_t capacity = 1;
        for (CettaIoRequest *r = runtime->requests; r; r = r->next)
            if (!r->ready && (r->ipc || r->provider == IO_FILE)) ++capacity;
        struct pollfd *fds = calloc(capacity, sizeof(*fds));
        if (!fds) return io_error(arena, head, args, nargs, "out of memory preparing socket wait");
        size_t n = 1; fds[0] = (struct pollfd){runtime->wakeup[0], POLLIN, 0};
        for (CettaIoRequest *r = runtime->requests; r; r = r->next) {
            if (r->ready) continue;
            if (r->provider == IO_TIMER && r->timer_due < due) due = r->timer_due;
            if (r->ipc) {
                fds[n++] = (struct pollfd){cetta_ipc_client_fd(r->ipc), cetta_ipc_client_events(r->ipc), 0};
                if (cetta_ipc_client_deadline(r->ipc) < due) due = cetta_ipc_client_deadline(r->ipc);
            }
            if (r->provider == IO_FILE) fds[n++] = (struct pollfd){r->file_fd, POLLIN, 0};
        }
        uint64_t remaining = due > now ? due - now : 0;
        int status = poll(fds, n, remaining > INT_MAX ? INT_MAX : (int)remaining);
        int error = errno; free(fds);
        if (status < 0 && error != EINTR)
            return io_error(arena, head, args, nargs, "native I/O wait failed");
    }
#else
    (void)runtime; (void)selected_id; (void)progress;
    return io_error(arena, head, args, nargs, "blocking I/O wait requires a native host");
#endif
}

static Atom *io_signal(CettaIoRuntime *runtime, Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    int64_t id, number;
    if (nargs != 2 || !io_nonnegative_int_arg(args[0], &id) ||
        !io_nonnegative_int_arg(args[1], &number) || number > INT_MAX)
        return io_error(arena, head, args, nargs, "expected (io:signal process-id signal-number)");
#ifndef __EMSCRIPTEN__
    CettaIoRequest *r = io_request_find(runtime, id);
    if (!r || r->provider != IO_PROCESS || !cetta_proc_owner_signal(runtime->process_owner, r->id, (int)number))
        return io_error(arena, head, args, nargs, "process signal failed or process already ended");
    return atom_unit(arena);
#else
    (void)runtime;
    return io_error(arena, head, args, nargs, "process signals require a native host");
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
    if (id == g_builtin_syms.lib_io_status)
        return io_status(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_wait)
        return io_wait(runtime, arena, head, args, nargs, false);
    if (id == g_builtin_syms.lib_io_wait_ready)
        return io_wait(runtime, arena, head, args, nargs, true);
    if (id == g_builtin_syms.lib_io_cancel)
        return io_cancel(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_io_signal)
        return io_signal(runtime, arena, head, args, nargs);
    return NULL;
}
