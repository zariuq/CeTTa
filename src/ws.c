#define _GNU_SOURCE
#include "ws.h"
#include "crypto_primitives.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_BUILD_HTTP_PROVIDER_CURL
#define CETTA_BUILD_HTTP_PROVIDER_CURL 0
#endif

#if !defined(__EMSCRIPTEN__)
#if CETTA_BUILD_HTTP_PROVIDER_CURL
#include <curl/curl.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define WS_CLOSE_WAIT_MS 5000u
#define WS_READ_CHUNK 16384u
/* A server connection's opening handshake may take this many bytes, which
 * is also the size of its input buffer afterwards. */
#define WS_REQUEST_MAX 16384u
#define WS_ACCEPT_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct WsText {
    struct WsText *next;
    char *bytes;
    size_t length;
} WsText;

/* A listener's count of live server connections, shared with their
 * threads, which can outlive the listener. */
typedef struct WsGate {
    pthread_mutex_t lock;
    unsigned live;          /* connections whose socket is open */
    unsigned references;    /* the listener's and those connections' */
} WsGate;

struct CettaWsConnection {
    pthread_mutex_t lock;
    int control[2];         /* the evaluator wakes the worker through this pipe */
    int wakeup_fd;          /* the runtime's; not owned, -1 once released */
    char *url;              /* a client's */
    uint32_t connect_timeout_ms;
    size_t max_message_bytes;
    unsigned references;    /* the evaluator's and the worker's */
    CettaWsState state;
    CettaWsClosed closed;
    WsText *inbox, *inbox_tail;
    size_t inbox_bytes;
    WsText *outbox, *outbox_tail;
    uint64_t queued, sent;
    /* A raw TCP client's: its queue holds lines, or text pieces. */
    bool raw, lines;
    bool close_requested;
    int close_code;
    char close_reason[124];
    /* A server connection's: the accepted socket, which its worker closes;
     * its listener's gate; the handshake, written by the worker before the
     * state becomes open and fixed afterwards; the link of the listener's
     * queues, which only the evaluator uses. */
    int fd;
    uint32_t idle_ms;
    WsGate *gate;
    char peer[80];
    CettaWsRequest request;
    char *request_text;
    const char **request_fields;
    struct CettaWsConnection *listener_next;
};

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

/* Under the lock, so that once cetta_ws_release returns no write reaches a
 * descriptor the runtime may have closed and the process reused. */
static void notify(CettaWsConnection *c) {
    pthread_mutex_lock(&c->lock);
    if (c->wakeup_fd >= 0) { ssize_t ignored = write(c->wakeup_fd, "w", 1); (void)ignored; }
    pthread_mutex_unlock(&c->lock);
}

static void poke(CettaWsConnection *c) {
    ssize_t ignored = write(c->control[1], "c", 1);
    (void)ignored;
}

static void texts_free(WsText *t) {
    while (t) {
        WsText *next = t->next;
        free(t->bytes);
        free(t);
        t = next;
    }
}

static void connection_free(CettaWsConnection *c) {
    texts_free(c->inbox);
    texts_free(c->outbox);
    close(c->control[0]);
    close(c->control[1]);
    if (c->url) {
        /* URLs may carry credentials. */
        volatile char *p = c->url;
        while (*p) *p++ = 0;
        free(c->url);
    }
    free(c->request_text);
    free(c->request_fields);
    pthread_mutex_destroy(&c->lock);
    free(c);
}

static CettaWsConnection *connection_new(size_t max_message_bytes, int wakeup_fd) {
    CettaWsConnection *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (pipe2(c->control, O_CLOEXEC | O_NONBLOCK)) {
        free(c);
        return NULL;
    }
    pthread_mutex_init(&c->lock, NULL);
    c->wakeup_fd = wakeup_fd;
    c->max_message_bytes = max_message_bytes;
    c->references = 2u;
    c->state = CETTA_WS_CONNECTING;
    c->fd = -1;
    return c;
}

/* A detached thread that holds the worker's reference.  It keeps the default
 * stack: the engine's static thread-local storage alone takes about 1 MiB of
 * it. */
static bool start_worker(CettaWsConnection *c, void *(*body)(void *)) {
    pthread_attr_t attributes;
    pthread_t thread;
    if (pthread_attr_init(&attributes)) return false;
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    bool started = !pthread_create(&thread, &attributes, body, c);
    pthread_attr_destroy(&attributes);
    return started;
}

static void unreference(CettaWsConnection *c) {
    pthread_mutex_lock(&c->lock);
    bool last = --c->references == 0;
    pthread_mutex_unlock(&c->lock);
    if (last) connection_free(c);
}

static void gate_drop(WsGate *gate, bool connection);

static void finish(CettaWsConnection *c, int code, const char *reason, size_t reason_length,
                   int transport, const char *message) {
    /* A server connection's socket closes and its slot frees before the
     * runtime hears that it ended, so a listener pumped on that news finds
     * the room.  Only the connection's own thread finishes it. */
    if (c->gate) {
        close(c->fd);
        c->fd = -1;
        gate_drop(c->gate, true);
        c->gate = NULL;
    }
    pthread_mutex_lock(&c->lock);
    if (c->state != CETTA_WS_CLOSED) {
        c->state = CETTA_WS_CLOSED;
        c->closed.code = code;
        if (reason_length >= sizeof(c->closed.reason)) reason_length = sizeof(c->closed.reason) - 1u;
        memcpy(c->closed.reason, reason, reason_length);
        c->closed.reason[reason_length] = '\0';
        c->closed.transport_code = transport;
        snprintf(c->closed.transport_message, sizeof(c->closed.transport_message), "%s", message);
    }
    pthread_mutex_unlock(&c->lock);
    notify(c);
}

static size_t close_payload(char *out, int code, const char *reason) {
    size_t length = strlen(reason);
    if (length > 123u) length = 123u;
    out[0] = (char)(code >> 8);
    out[1] = (char)code;
    memcpy(out + 2, reason, length);
    return 2u + length;
}

static bool utf8_valid(const char *bytes, size_t length) {
    uint32_t scalar, width;
    for (size_t at = 0u; at < length; at += width)
        if (!cetta_utf8_decode_one((const uint8_t *)bytes, length, at, &scalar, &width)) return false;
    return true;
}

/* Queues a received message; false when memory is exhausted.  *reading
 * says whether the queue still has room. */
static bool inbox_push(CettaWsConnection *c, const char *message, size_t length, bool *reading) {
    WsText *text = malloc(sizeof(*text));
    char *bytes = malloc(length + 1u);
    if (!text || !bytes) {
        free(text);
        free(bytes);
        return false;
    }
    if (length) memcpy(bytes, message, length);
    bytes[length] = '\0';
    *text = (WsText){NULL, bytes, length};
    pthread_mutex_lock(&c->lock);
    if (c->inbox_tail) c->inbox_tail->next = text; else c->inbox = text;
    c->inbox_tail = text;
    c->inbox_bytes += length;
    *reading = c->inbox_bytes < c->max_message_bytes;
    pthread_mutex_unlock(&c->lock);
    notify(c);
    return true;
}

/* ------------------------------------------------------------------------
 * Client connections */

#if CETTA_BUILD_HTTP_PROVIDER_CURL

static pthread_once_t ws_curl_once = PTHREAD_ONCE_INIT;
static bool ws_curl_ready;

static void ws_init_curl(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return;
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    bool ws = false;
    for (const char *const *p = v ? v->protocols : NULL; p && *p; ++p)
        if (!strcmp(*p, "wss")) ws = true;
    ws_curl_ready = ws && (v->features & CURL_VERSION_THREADSAFE);
}

/* A close frame from this side, sent once when the connection fails; the
 * peer may already be gone, so a failure to send is not reported. */
static void send_close(CURL *easy, curl_socket_t fd, int code, const char *reason) {
    char payload[125];
    size_t length = close_payload(payload, code, reason), sent = 0;
    for (unsigned attempt = 0u; attempt < 10u; ++attempt) {
        CURLcode rc = curl_ws_send(easy, payload, length, &sent, 0, CURLWS_CLOSE);
        if (rc != CURLE_AGAIN) return;
        struct pollfd p = {fd, POLLOUT, 0};
        (void)poll(&p, 1, 100);
    }
}

static void *client_worker(void *argument) {
    CettaWsConnection *c = argument;
    char *buffer = malloc(WS_READ_CHUNK), *message = NULL;
    size_t length = 0, capacity = 0, offset = 0;
    bool assembling = false, binary = false, close_sent = false;
    uint64_t close_deadline = 0;
    int sent_code = 0;
    char sent_reason[124] = "", pong[125];
    size_t pong_length = 0;
    bool pong_due = false;
    curl_socket_t fd = CURL_SOCKET_BAD;
    CURL *easy = buffer ? curl_easy_init() : NULL;
    if (!easy) {
        finish(c, 1006, "", 0, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
        goto done;
    }
    curl_easy_setopt(easy, CURLOPT_URL, c->url);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "ws,wss");
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 2L);
    /* Connect-only mode would queue an automatic pong until the next send;
     * pongs are sent here instead, between whole frames. */
    curl_easy_setopt(easy, CURLOPT_WS_OPTIONS, (long)CURLWS_NOAUTOPONG);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, (long)c->connect_timeout_ms);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)c->connect_timeout_ms);
    CURLcode code = curl_easy_perform(easy);
    if (code == CURLE_OK && (curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &fd) != CURLE_OK ||
                             fd == CURL_SOCKET_BAD))
        code = CURLE_COULDNT_CONNECT;
    if (code != CURLE_OK) {
        finish(c, 1006, "", 0, code, curl_easy_strerror(code));
        goto done;
    }
    pthread_mutex_lock(&c->lock);
    if (c->state == CETTA_WS_CONNECTING) c->state = CETTA_WS_OPEN;
    pthread_mutex_unlock(&c->lock);
    notify(c);

    for (;;) {
        bool want_write = false;
        /* Answer a ping between frames, write queued frames in order, then
         * the close frame if one is due. */
        if (pong_due && offset == 0) {
            size_t sent = 0;
            code = curl_ws_send(easy, pong, pong_length, &sent, 0, CURLWS_PONG);
            if (code == CURLE_OK) pong_due = false;
            else if (code == CURLE_AGAIN) want_write = true;
            else {
                finish(c, 1006, "", 0, code, curl_easy_strerror(code));
                goto done;
            }
        }
        for (; !want_write;) {
            pthread_mutex_lock(&c->lock);
            WsText *frame = c->outbox;
            bool close_due = !frame && c->close_requested && !close_sent;
            int close_code = c->close_code;
            char close_reason[124];
            memcpy(close_reason, c->close_reason, sizeof(close_reason));
            pthread_mutex_unlock(&c->lock);
            if (frame) {
                size_t sent = 0;
                code = curl_ws_send(easy, frame->bytes + offset, frame->length - offset, &sent, 0, CURLWS_TEXT);
                if (code == CURLE_AGAIN) { want_write = true; break; }
                if (code != CURLE_OK) {
                    finish(c, 1006, "", 0, code, curl_easy_strerror(code));
                    goto done;
                }
                offset += sent;
                if (offset < frame->length) continue;
                pthread_mutex_lock(&c->lock);
                c->outbox = frame->next;
                if (!c->outbox) c->outbox_tail = NULL;
                c->sent++;
                pthread_mutex_unlock(&c->lock);
                free(frame->bytes);
                free(frame);
                offset = 0;
                notify(c);
                continue;
            }
            if (close_due) {
                char payload[125];
                size_t n = close_payload(payload, close_code, close_reason), sent = 0;
                code = curl_ws_send(easy, payload, n, &sent, 0, CURLWS_CLOSE);
                if (code == CURLE_AGAIN) { want_write = true; break; }
                close_sent = true;
                sent_code = close_code;
                memcpy(sent_reason, close_reason, sizeof(sent_reason));
                if (code != CURLE_OK) {
                    finish(c, sent_code, sent_reason, strlen(sent_reason), 0, "");
                    goto done;
                }
                close_deadline = now_ms() + WS_CLOSE_WAIT_MS;
            }
            break;
        }

        /* Read whatever is there, unless the queue is full. */
        pthread_mutex_lock(&c->lock);
        bool reading = c->inbox_bytes < c->max_message_bytes;
        pthread_mutex_unlock(&c->lock);
        while (reading) {
            size_t got = 0;
            const struct curl_ws_frame *meta = NULL;
            code = curl_ws_recv(easy, buffer, WS_READ_CHUNK, &got, &meta);
            if (code == CURLE_AGAIN) break;
            if (code != CURLE_OK) {
                if (close_sent) finish(c, sent_code, sent_reason, strlen(sent_reason), 0, "");
                else finish(c, 1006, "", 0, code, curl_easy_strerror(code));
                goto done;
            }
            if (!meta) continue;
            if (meta->flags & CURLWS_CLOSE) {
                int peer = got >= 2u ? ((unsigned char)buffer[0] << 8 | (unsigned char)buffer[1]) : 1005;
                if (!close_sent) send_close(easy, fd, peer == 1005 ? 1000 : peer, "");
                finish(c, peer, got > 2u ? buffer + 2 : "", got > 2u ? got - 2u : 0u, 0, "");
                goto done;
            }
            if (meta->flags & CURLWS_PING) {
                /* Answer the latest ping; a control payload is one chunk. */
                pong_length = got < sizeof(pong) ? got : sizeof(pong);
                memcpy(pong, buffer, pong_length);
                pong_due = true;
                continue;
            }
            if (meta->flags & CURLWS_PONG) continue;
            if (!assembling) {
                assembling = true;
                binary = (meta->flags & CURLWS_BINARY) != 0;
                length = 0;
            }
            if (got > c->max_message_bytes - length) {
                send_close(easy, fd, 1009, "message too big");
                finish(c, 1009, "message too big", 15u, 0, "");
                goto done;
            }
            if (length + got + 1u > capacity) {
                size_t grown = capacity ? capacity : 4096u;
                while (grown < length + got + 1u) grown *= 2u;
                char *bigger = realloc(message, grown);
                if (!bigger) {
                    send_close(easy, fd, 1011, "out of memory");
                    finish(c, 1011, "out of memory", 13u, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
                    goto done;
                }
                message = bigger;
                capacity = grown;
            }
            memcpy(message + length, buffer, got);
            length += got;
            if (meta->bytesleft != 0 || (meta->flags & CURLWS_CONT)) continue;
            assembling = false;
            if (binary) {
                send_close(easy, fd, 1003, "binary messages are not accepted");
                finish(c, 1003, "binary messages are not accepted", 32u, 0, "");
                goto done;
            }
            if (!utf8_valid(message, length)) {
                send_close(easy, fd, 1007, "text is not UTF-8");
                finish(c, 1007, "text is not UTF-8", 17u, 0, "");
                goto done;
            }
            if (!inbox_push(c, message, length, &reading)) {
                send_close(easy, fd, 1011, "out of memory");
                finish(c, 1011, "out of memory", 13u, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
                goto done;
            }
        }

        if (pong_due && !want_write) continue;
        uint64_t now = now_ms();
        if (close_sent && now >= close_deadline) {
            finish(c, sent_code, sent_reason, strlen(sent_reason), 0, "");
            goto done;
        }
        /* Wait for the evaluator, the socket when there is room to read or
         * data to write, or the close deadline.  A full queue leaves the
         * socket out, so a hung-up peer cannot spin this loop. */
        struct pollfd fds[2] = {
            {c->control[0], POLLIN, 0},
            {fd, (short)((reading ? POLLIN : 0) | (want_write ? POLLOUT : 0)), 0},
        };
        int timeout = close_sent ? (int)(close_deadline - now) : -1;
        if (poll(fds, reading || want_write ? 2u : 1u, timeout) < 0 && errno != EINTR) {
            finish(c, 1006, "", 0, CURLE_RECV_ERROR, "waiting on the connection failed");
            goto done;
        }
        if (fds[0].revents & POLLIN) {
            char drain[64];
            while (read(c->control[0], drain, sizeof(drain)) > 0) {}
        }
    }
done:
    free(message);
    free(buffer);
    if (easy) curl_easy_cleanup(easy);
    unreference(c);
    return NULL;
}

CettaWsConnection *cetta_ws_open(const char *url, uint32_t connect_timeout_ms,
                                 size_t max_message_bytes, int wakeup_fd) {
    pthread_once(&ws_curl_once, ws_init_curl);
    if (!ws_curl_ready || !url || max_message_bytes == 0u ||
        (strncmp(url, "ws://", 5) && strncmp(url, "wss://", 6)))
        return NULL;
    CettaWsConnection *c = connection_new(max_message_bytes, wakeup_fd);
    if (!c) return NULL;
    c->url = strdup(url);
    c->connect_timeout_ms = connect_timeout_ms;
    if (!c->url || !start_worker(c, client_worker)) {
        connection_free(c);
        return NULL;
    }
    return c;
}

/* ------------------------------------------------------------------------
 * Raw TCP and TLS client connections */

/* The text of these bytes, each byte that is not part of a UTF-8 scalar
 * read as U+FFFD; *out_length is its length.  NULL when memory is short. */
static char *tcp_text(const char *bytes, size_t length, size_t *out_length) {
    char *text = malloc(length * 3u + 1u), *at = text;
    if (!text) return NULL;
    uint32_t scalar, width;
    for (size_t i = 0u; i < length; i += width) {
        if (cetta_utf8_decode_one((const uint8_t *)bytes, length, i, &scalar, &width)) {
            memcpy(at, bytes + i, width);
            at += width;
        } else {
            memcpy(at, "\xEF\xBF\xBD", 3u);
            at += 3;
            width = 1u;
        }
    }
    *at = '\0';
    *out_length = (size_t)(at - text);
    return text;
}

/* Queues these bytes as text; false when memory is exhausted. */
static bool tcp_push(CettaWsConnection *c, const char *bytes, size_t length, bool *reading) {
    size_t text_length = 0u;
    char *text = tcp_text(bytes, length, &text_length);
    if (!text) return false;
    bool pushed = inbox_push(c, text, text_length, reading);
    free(text);
    return pushed;
}

/* How many of the received bytes end on a whole UTF-8 scalar: all of them,
 * unless they end inside a scalar whose lead byte has arrived. */
static size_t tcp_whole(const char *bytes, size_t length) {
    for (size_t back = 1u; back <= 4u && back <= length; ++back) {
        unsigned char b = (unsigned char)bytes[length - back];
        if ((b & 0xC0u) == 0x80u) continue;
        size_t need = b >= 0xF0u ? 4u : b >= 0xE0u ? 3u : b >= 0xC0u ? 2u : 1u;
        return need > back ? length - back : length;
    }
    return length;
}

static void tcp_out_of_memory(CettaWsConnection *c) {
    finish(c, 1011, "out of memory", 13u, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
}

static void *tcp_worker(void *argument) {
    CettaWsConnection *c = argument;
    char *buffer = malloc(WS_READ_CHUNK), *pending = NULL;
    size_t pending_length = 0u, capacity = 0u, offset = 0u;
    curl_socket_t fd = CURL_SOCKET_BAD;
    CURL *easy = buffer ? curl_easy_init() : NULL;
    if (!easy) {
        finish(c, 1006, "", 0, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
        goto done;
    }
    curl_easy_setopt(easy, CURLOPT_URL, c->url);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    /* Connect, and for https also complete TLS, then hand over the stream.
     * HTTP/1.1 keeps TLS from negotiating HTTP/2, whose framing libcurl would
     * then expect on what is a raw stream. */
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, (long)c->connect_timeout_ms);
    CURLcode code = curl_easy_perform(easy);
    if (code == CURLE_OK && (curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &fd) != CURLE_OK ||
                             fd == CURL_SOCKET_BAD))
        code = CURLE_COULDNT_CONNECT;
    if (code != CURLE_OK) {
        finish(c, 1006, "", 0, code, curl_easy_strerror(code));
        goto done;
    }
    pthread_mutex_lock(&c->lock);
    if (c->state == CETTA_WS_CONNECTING) c->state = CETTA_WS_OPEN;
    pthread_mutex_unlock(&c->lock);
    notify(c);

    for (;;) {
        /* Write queued text in order; a close takes effect once it is out. */
        bool want_write = false;
        for (;;) {
            pthread_mutex_lock(&c->lock);
            WsText *piece = c->outbox;
            bool close_due = !piece && c->close_requested;
            pthread_mutex_unlock(&c->lock);
            if (close_due) {
                finish(c, 1000, "", 0, 0, "");
                goto done;
            }
            if (!piece) break;
            size_t sent = 0u;
            code = curl_easy_send(easy, piece->bytes + offset, piece->length - offset, &sent);
            if (code == CURLE_AGAIN) { want_write = true; break; }
            if (code != CURLE_OK) {
                finish(c, 1006, "", 0, code, curl_easy_strerror(code));
                goto done;
            }
            offset += sent;
            if (offset < piece->length) continue;
            pthread_mutex_lock(&c->lock);
            c->outbox = piece->next;
            if (!c->outbox) c->outbox_tail = NULL;
            c->sent++;
            pthread_mutex_unlock(&c->lock);
            free(piece->bytes);
            free(piece);
            offset = 0u;
            notify(c);
        }

        /* Read whatever is there, unless the queue is full. */
        pthread_mutex_lock(&c->lock);
        bool reading = c->inbox_bytes < c->max_message_bytes;
        pthread_mutex_unlock(&c->lock);
        while (reading) {
            size_t got = 0u;
            code = curl_easy_recv(easy, buffer, WS_READ_CHUNK, &got);
            if (code == CURLE_AGAIN) break;
            if (code != CURLE_OK) {
                finish(c, 1006, "", 0, code, curl_easy_strerror(code));
                goto done;
            }
            if (got == 0u) {
                /* The peer ended the stream: what is left is the last piece. */
                if (pending_length && !tcp_push(c, pending, pending_length, &reading)) {
                    tcp_out_of_memory(c);
                    goto done;
                }
                finish(c, 1000, "", 0, 0, "");
                goto done;
            }
            if (pending_length + got > capacity) {
                size_t grown = capacity ? capacity : 4096u;
                while (grown < pending_length + got) grown *= 2u;
                char *bigger = realloc(pending, grown);
                if (!bigger) {
                    tcp_out_of_memory(c);
                    goto done;
                }
                pending = bigger;
                capacity = grown;
            }
            memcpy(pending + pending_length, buffer, got);
            pending_length += got;
            size_t used = 0u;
            if (c->lines) {
                for (char *end; (end = memchr(pending + used, '\n', pending_length - used)); ) {
                    size_t length = (size_t)(end - (pending + used));
                    if (length && pending[used + length - 1u] == '\r') length--;
                    if (length > c->max_message_bytes) {
                        finish(c, 1009, "line longer than max-bytes", 26u, 0, "");
                        goto done;
                    }
                    if (!tcp_push(c, pending + used, length, &reading)) {
                        tcp_out_of_memory(c);
                        goto done;
                    }
                    used = (size_t)(end - pending) + 1u;
                }
                if (pending_length - used > c->max_message_bytes) {
                    finish(c, 1009, "line longer than max-bytes", 26u, 0, "");
                    goto done;
                }
            } else {
                used = tcp_whole(pending, pending_length);
                if (used && !tcp_push(c, pending, used, &reading)) {
                    tcp_out_of_memory(c);
                    goto done;
                }
            }
            memmove(pending, pending + used, pending_length - used);
            pending_length -= used;
        }

        /* Wait for the evaluator, or the socket when there is room to read
         * or text to write.  A full queue leaves the socket out, so a peer
         * that hung up cannot spin this loop. */
        struct pollfd fds[2] = {
            {c->control[0], POLLIN, 0},
            {fd, (short)((reading ? POLLIN : 0) | (want_write ? POLLOUT : 0)), 0},
        };
        if (poll(fds, reading || want_write ? 2u : 1u, -1) < 0 && errno != EINTR) {
            finish(c, 1006, "", 0, CURLE_RECV_ERROR, "waiting on the connection failed");
            goto done;
        }
        if (fds[0].revents & POLLIN) {
            char drain[64];
            while (read(c->control[0], drain, sizeof(drain)) > 0) {}
        }
    }
done:
    free(pending);
    free(buffer);
    if (easy) curl_easy_cleanup(easy);
    unreference(c);
    return NULL;
}

CettaWsConnection *cetta_tcp_open(const char *url, uint32_t connect_timeout_ms,
                                  size_t max_bytes, bool lines, int wakeup_fd) {
    pthread_once(&ws_curl_once, ws_init_curl);
    bool tls = url && !strncmp(url, "tls://", 6);
    if (!ws_curl_ready || !url || max_bytes == 0u || (!tls && strncmp(url, "tcp://", 6)))
        return NULL;
    CettaWsConnection *c = connection_new(max_bytes, wakeup_fd);
    if (!c) return NULL;
    c->raw = true;
    c->lines = lines;
    /* libcurl's connect-only mode reaches a host and port through these
     * schemes without sending a request; https completes TLS first. */
    size_t length = strlen(url + 6) + 9u;
    c->url = malloc(length);
    if (c->url) snprintf(c->url, length, "%s://%s", tls ? "https" : "http", url + 6);
    c->connect_timeout_ms = connect_timeout_ms;
    if (!c->url || !start_worker(c, tcp_worker)) {
        connection_free(c);
        return NULL;
    }
    return c;
}

#else

CettaWsConnection *cetta_ws_open(const char *url, uint32_t connect_timeout_ms,
                                 size_t max_message_bytes, int wakeup_fd) {
    (void)url; (void)connect_timeout_ms; (void)max_message_bytes; (void)wakeup_fd;
    return NULL;
}
CettaWsConnection *cetta_tcp_open(const char *url, uint32_t connect_timeout_ms,
                                  size_t max_bytes, bool lines, int wakeup_fd) {
    (void)url; (void)connect_timeout_ms; (void)max_bytes; (void)lines; (void)wakeup_fd;
    return NULL;
}

#endif

/* ------------------------------------------------------------------------
 * Server connections */

static const char *errno_text(int code, char *buffer, size_t size) {
#if defined(__GLIBC__)
    return strerror_r(code, buffer, size);
#else
    return strerror_r(code, buffer, size) ? "unknown error" : buffer;
#endif
}

static void gate_drop(WsGate *gate, bool connection) {
    pthread_mutex_lock(&gate->lock);
    if (connection) --gate->live;
    bool last = --gate->references == 0;
    pthread_mutex_unlock(&gate->lock);
    if (last) {
        pthread_mutex_destroy(&gate->lock);
        free(gate);
    }
}

typedef enum { WIRE_NONE, WIRE_TEXT, WIRE_PONG, WIRE_PING, WIRE_CLOSE } WsWireKind;

/* The frame being written: its header, then its payload, which is a queued
 * text's bytes or a control payload copied here.  Server frames are not
 * masked. */
typedef struct {
    WsWireKind kind;
    uint8_t header[10];
    size_t header_length;
    const uint8_t *payload;
    size_t payload_length, written;
    uint8_t control[125];
} WsWire;

/* A server connection's reading and writing state, its worker's alone. */
typedef struct {
    uint8_t *in;            /* WS_REQUEST_MAX bytes; unread ones are in[start, length) */
    size_t start, length;
    bool in_frame, fin;     /* the frame being read */
    unsigned opcode;
    uint64_t left;
    uint8_t mask[4];
    unsigned mask_at;
    char *message;          /* the text message being assembled */
    size_t message_length, capacity;
    bool assembling;
    WsWire wire;
    bool pong_due, ping_due, ping_sent, close_sent;
    uint8_t pong[125];
    size_t pong_length;
    int sent_code;
    char sent_reason[124];
    uint64_t close_deadline, heard;
} WsServer;

static void wire_start(WsWire *w, WsWireKind kind, unsigned opcode, const uint8_t *payload, size_t length) {
    w->kind = kind;
    w->header[0] = (uint8_t)(0x80u | opcode);
    if (length < 126u) {
        w->header[1] = (uint8_t)length;
        w->header_length = 2u;
    } else if (length <= 0xffffu) {
        w->header[1] = 126u;
        w->header[2] = (uint8_t)(length >> 8);
        w->header[3] = (uint8_t)length;
        w->header_length = 4u;
    } else {
        w->header[1] = 127u;
        for (unsigned i = 0u; i < 8u; ++i) w->header[2u + i] = (uint8_t)((uint64_t)length >> (56u - 8u * i));
        w->header_length = 10u;
    }
    if (kind != WIRE_TEXT && length) {
        memcpy(w->control, payload, length);
        payload = w->control;
    }
    w->payload = payload;
    w->payload_length = length;
    w->written = 0u;
}

/* Writes what the socket takes: 1 once the frame is out, 0 when the socket
 * is full, -1 on failure, with errno. */
static int wire_write(int fd, WsWire *w) {
    size_t total = w->header_length + w->payload_length;
    while (w->written < total) {
        struct iovec parts[2];
        size_t count = 0u;
        if (w->written < w->header_length) {
            parts[count++] = (struct iovec){w->header + w->written, w->header_length - w->written};
            if (w->payload_length) parts[count++] = (struct iovec){(void *)(uintptr_t)w->payload, w->payload_length};
        } else {
            size_t at = w->written - w->header_length;
            parts[count++] = (struct iovec){(void *)(uintptr_t)(w->payload + at), w->payload_length - at};
        }
        struct msghdr message = {.msg_iov = parts, .msg_iovlen = count};
        ssize_t n = sendmsg(fd, &message, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) continue;
            return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
        }
        w->written += (size_t)n;
    }
    return 1;
}

/* A close frame sent once as the connection ends: the client may be gone,
 * so a failure is not reported. */
static void server_send_close(int fd, const uint8_t *payload, size_t length) {
    WsWire wire;
    wire_start(&wire, WIRE_CLOSE, 8u, payload, length);
    for (unsigned attempt = 0u; attempt < 10u; ++attempt) {
        if (wire_write(fd, &wire)) return;
        struct pollfd p = {fd, POLLOUT, 0};
        (void)poll(&p, 1, 100);
    }
}

/* Fails the connection (RFC 6455 section 7.1.7): a close frame with the code
 * unless a frame is half written or ours was sent, then the socket closes. */
static void server_fail(CettaWsConnection *c, WsServer *s, int code, const char *reason) {
    if (!s->close_sent && (s->wire.kind == WIRE_NONE || !s->wire.written)) {
        char payload[125];
        size_t n = close_payload(payload, code, reason);
        server_send_close(c->fd, (const uint8_t *)payload, n);
    }
    finish(c, code, reason, strlen(reason), 0, "");
}

static bool close_code_valid(int code) {
    return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) ||
           (code >= 3000 && code <= 4999);
}

static int poll_timeout(uint64_t due, uint64_t now) {
    if (!due) return -1;
    uint64_t remaining = due > now ? due - now : 0u;
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

/* Waits for the socket to be ready for events: false when the deadline (if
 * any) passes first or the connection was released. */
static bool server_wait(CettaWsConnection *c, short events, uint64_t deadline) {
    for (;;) {
        pthread_mutex_lock(&c->lock);
        bool released = c->close_requested;
        pthread_mutex_unlock(&c->lock);
        uint64_t now = now_ms();
        if (released || (deadline && now >= deadline)) return false;
        struct pollfd fds[2] = {{c->control[0], POLLIN, 0}, {c->fd, events, 0}};
        if (poll(fds, 2u, poll_timeout(deadline, now)) < 0 && errno != EINTR) return false;
        if (fds[0].revents & POLLIN) {
            char drain[64];
            while (read(c->control[0], drain, sizeof(drain)) > 0) {}
        }
        if (fds[1].revents) return true;
    }
}

static bool server_send_all(CettaWsConnection *c, const char *bytes, size_t length, uint64_t deadline) {
    while (length) {
        ssize_t n = send(c->fd, bytes, length, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            bytes += n;
            length -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && server_wait(c, POLLOUT, deadline)) continue;
        return false;
    }
    return true;
}

/* An HTTP answer that refuses the handshake; the connection then closes. */
static void server_refuse(CettaWsConnection *c, const char *status, const char *fields, uint64_t deadline) {
    char response[256];
    int n = snprintf(response, sizeof(response),
                     "HTTP/1.1 %s\r\n%sConnection: close\r\nContent-Length: 0\r\n\r\n", status, fields);
    if (n > 0 && (size_t)n < sizeof(response)) (void)server_send_all(c, response, (size_t)n, deadline);
}

static bool token_char(unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch && strchr("!#$%&'*+-.^_`|~", ch));
}

/* Whether a comma-separated field value lists the token, in any case. */
static bool lists_token(const char *value, const char *token) {
    size_t n = strlen(token);
    for (const char *at = value; *at;) {
        while (*at == ' ' || *at == '\t' || *at == ',') ++at;
        const char *end = at;
        while (*end && *end != ',') ++end;
        const char *last = end;
        while (last > at && (last[-1] == ' ' || last[-1] == '\t')) --last;
        if ((size_t)(last - at) == n && !strncasecmp(at, token, n)) return true;
        at = end;
    }
    return false;
}

/* Reads the opening handshake and answers it.  Bytes after the request stay
 * in the input: a client may send frames right behind it. */
static bool server_handshake(CettaWsConnection *c, WsServer *s) {
    uint64_t deadline = c->idle_ms ? now_ms() + c->idle_ms : 0u;
    char *text = (char *)s->in;
    size_t head;
    for (;;) {
        char *end = s->length >= 4u ? memmem(text, s->length, "\r\n\r\n", 4u) : NULL;
        if (end) {
            head = (size_t)(end - text) + 4u;
            break;
        }
        if (s->length == WS_REQUEST_MAX) {
            server_refuse(c, "431 Request Header Fields Too Large", "", deadline);
            return false;
        }
        ssize_t n = recv(c->fd, s->in + s->length, WS_REQUEST_MAX - s->length, MSG_DONTWAIT);
        if (n > 0) {
            s->length += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && server_wait(c, POLLIN, deadline)) continue;
        return false;
    }
    /* Lines end in CR LF; a NUL, or a CR or LF elsewhere, is refused.  Each
     * line ending becomes two NULs. */
    for (size_t i = 0u; i < head; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (!ch || (ch == '\r' && text[i + 1u] != '\n') || (ch == '\n' && (!i || text[i - 1u] != '\r'))) {
            server_refuse(c, "400 Bad Request", "", deadline);
            return false;
        }
    }
    for (size_t i = 0u; i + 1u < head; ++i)
        if (text[i] == '\r') text[i] = text[i + 1u] = '\0';
    char *line = text, *fields = line + strlen(line) + 2u;
    size_t count = 0u;
    for (char *field = fields; *field; field += strlen(field) + 2u) ++count;
    char *target = strchr(line, ' '), *version = target ? strchr(target + 1, ' ') : NULL;
    if (!version) {
        server_refuse(c, "400 Bad Request", "", deadline);
        return false;
    }
    *target++ = '\0';
    *version++ = '\0';
    if (strcmp(line, "GET")) {
        server_refuse(c, "405 Method Not Allowed", "Allow: GET\r\n", deadline);
        return false;
    }
    bool valid = !strcmp(version, "HTTP/1.1") && *target;
    for (const unsigned char *p = (const unsigned char *)target; valid && *p; ++p)
        valid = *p > 0x20u && *p < 0x7fu;
    const char **pairs = valid ? malloc((count ? count : 1u) * 2u * sizeof(*pairs)) : NULL;
    if (!pairs) {
        server_refuse(c, valid ? "503 Service Unavailable" : "400 Bad Request", "", deadline);
        return false;
    }
    const char *key = NULL, *websocket_version = NULL;
    unsigned keys = 0u, versions = 0u, hosts = 0u;
    bool upgrade = false, connection = false;
    size_t size = strlen(target) + 1u, index = 0u;
    for (char *field = fields; valid && *field; ++index) {
        char *next = field + strlen(field) + 2u, *colon = strchr(field, ':');
        /* A folded line starts with a space or tab, which no name holds. */
        valid = colon && colon != field;
        for (char *p = field; valid && p < colon; ++p) {
            valid = token_char((unsigned char)*p);
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        }
        if (!valid) break;
        *colon = '\0';
        char *value = colon + 1, *end;
        while (*value == ' ' || *value == '\t') ++value;
        for (end = value + strlen(value); end > value && (end[-1] == ' ' || end[-1] == '\t');) *--end = '\0';
        for (const unsigned char *p = (const unsigned char *)value; valid && *p; ++p)
            valid = (*p >= 0x20u || *p == '\t') && *p != 0x7fu;
        pairs[index] = field;
        pairs[count + index] = value;
        size += strlen(field) + strlen(value) + 2u;
        if (!strcmp(field, "upgrade") && lists_token(value, "websocket")) upgrade = true;
        else if (!strcmp(field, "connection") && lists_token(value, "upgrade")) connection = true;
        else if (!strcmp(field, "host")) ++hosts;
        else if (!strcmp(field, "sec-websocket-key")) { ++keys; key = value; }
        else if (!strcmp(field, "sec-websocket-version")) { ++versions; websocket_version = value; }
        field = next;
    }
    uint8_t nonce[18];
    size_t nonce_length = 0u;
    const char *status = NULL, *answer = "";
    if (!valid || hosts != 1u || versions != 1u) status = "400 Bad Request";
    else if (!upgrade || !connection) {
        status = "426 Upgrade Required";
        answer = "Upgrade: websocket\r\n";
    } else if (strcmp(websocket_version, "13")) {
        status = "426 Upgrade Required";
        answer = "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n";
    } else if (keys != 1u || strlen(key) != 24u ||
               !cetta_crypto_base64_decode((const uint8_t *)key, 24u, nonce, &nonce_length) ||
               nonce_length != 16u)
        status = "400 Bad Request";
    char *stored = status ? NULL : malloc(size);
    if (!status && !stored) status = "503 Service Unavailable";
    if (status) {
        free(pairs);
        server_refuse(c, status, answer, deadline);
        return false;
    }
    uint8_t digest[20];
    const uint8_t *parts[2] = {(const uint8_t *)key, (const uint8_t *)WS_ACCEPT_GUID};
    size_t lengths[2] = {24u, sizeof(WS_ACCEPT_GUID) - 1u};
    cetta_crypto_sha1(parts, lengths, 2u, digest);
    char accept[32], response[160];
    cetta_crypto_base64_encode(digest, sizeof(digest), accept);
    int n = snprintf(response, sizeof(response),
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    /* The request moves out of the input, which keeps only what follows it. */
    char *at = stored;
    size_t length = strlen(target) + 1u;
    memcpy(at, target, length);
    c->request.target = at;
    at += length;
    for (size_t i = 0u; i < 2u * count; ++i) {
        length = strlen(pairs[i]) + 1u;
        memcpy(at, pairs[i], length);
        pairs[i] = at;
        at += length;
    }
    c->request.peer = c->peer;
    c->request.field_count = count;
    c->request.names = pairs;
    c->request.values = pairs + count;
    c->request_text = stored;
    c->request_fields = pairs;
    memmove(s->in, s->in + head, s->length - head);
    s->length -= head;
    s->start = 0u;
    if (!server_send_all(c, response, (size_t)n, deadline)) return false;
    pthread_mutex_lock(&c->lock);
    if (c->state == CETTA_WS_CONNECTING) c->state = CETTA_WS_OPEN;
    pthread_mutex_unlock(&c->lock);
    notify(c);
    return true;
}

static bool message_room(WsServer *s, size_t needed) {
    if (needed <= s->capacity) return true;
    size_t grown = s->capacity ? s->capacity : 4096u;
    while (grown < needed) grown = grown > SIZE_MAX / 2u ? needed : grown * 2u;
    char *bigger = realloc(s->message, grown);
    if (!bigger) return false;
    s->message = bigger;
    s->capacity = grown;
    return true;
}

/* Takes the frames the input holds.  Returns 1 when it needs more bytes, 0
 * when the queue filled, -1 when the connection ended. */
static int server_frames(CettaWsConnection *c, WsServer *s) {
    for (;;) {
        size_t available = s->length - s->start;
        const uint8_t *at = s->in + s->start;
        if (!s->in_frame) {
            if (available < 2u) return 1;
            unsigned opcode = at[0] & 0x0fu, length7 = at[1] & 0x7fu;
            bool fin = (at[0] & 0x80u) != 0u;
            const char *violation =
                at[0] & 0x70u ? "reserved bits are set" :
                !(at[1] & 0x80u) ? "a client's frames are masked" :
                (opcode >= 3u && opcode <= 7u) || opcode >= 11u ? "unknown opcode" :
                opcode >= 8u && !fin ? "a control frame is fragmented" :
                opcode >= 8u && length7 > 125u ? "a control frame carries at most 125 bytes" :
                opcode == 0u && !s->assembling ? "a continuation frame outside a message" :
                (opcode == 1u || opcode == 2u) && s->assembling ? "a new message inside a fragmented one" : NULL;
            if (violation) {
                server_fail(c, s, 1002, violation);
                return -1;
            }
            if (opcode == 2u) {
                server_fail(c, s, 1003, "binary messages are not accepted");
                return -1;
            }
            size_t header = 6u + (length7 == 126u ? 2u : length7 == 127u ? 8u : 0u);
            if (available < header) return 1;
            uint64_t length = length7;
            if (length7 == 126u) length = (uint64_t)at[2] << 8 | at[3];
            else if (length7 == 127u) {
                length = 0u;
                for (unsigned i = 0u; i < 8u; ++i) length = length << 8 | at[2u + i];
                if (length >> 63) {
                    server_fail(c, s, 1002, "frame length out of range");
                    return -1;
                }
            }
            if (opcode < 8u && length > c->max_message_bytes - s->message_length) {
                server_fail(c, s, 1009, "message too big");
                return -1;
            }
            memcpy(s->mask, at + header - 4u, 4u);
            s->start += header;
            s->in_frame = true;
            s->fin = fin;
            s->opcode = opcode;
            s->left = length;
            s->mask_at = 0u;
            continue;
        }
        if (s->opcode >= 8u) {
            /* A control frame is handled whole. */
            if (available < s->left) return 1;
            uint8_t payload[125];
            size_t n = (size_t)s->left;
            for (size_t i = 0u; i < n; ++i) payload[i] = at[i] ^ s->mask[i & 3u];
            s->start += n;
            s->in_frame = false;
            if (s->opcode == 9u) {
                if (!s->close_sent) {
                    memcpy(s->pong, payload, n);
                    s->pong_length = n;
                    s->pong_due = true;
                }
                continue;
            }
            if (s->opcode == 10u) continue;
            int code = 1005;
            if (n == 1u) {
                server_fail(c, s, 1002, "a close frame's payload is one byte");
                return -1;
            }
            if (n >= 2u) {
                code = payload[0] << 8 | payload[1];
                if (!close_code_valid(code)) {
                    server_fail(c, s, 1002, "invalid close code");
                    return -1;
                }
                if (!utf8_valid((const char *)payload + 2, n - 2u)) {
                    server_fail(c, s, 1007, "the close reason is not UTF-8");
                    return -1;
                }
            }
            /* The client's close frame is echoed, then the socket closes. */
            if (!s->close_sent && (s->wire.kind == WIRE_NONE || !s->wire.written))
                server_send_close(c->fd, payload, n);
            finish(c, code, n > 2u ? (const char *)payload + 2 : "", n > 2u ? n - 2u : 0u, 0, "");
            return -1;
        }
        size_t take = s->left < available ? (size_t)s->left : available;
        if (!take && s->left) return 1;
        if (!message_room(s, s->message_length + take + 1u)) {
            server_fail(c, s, 1011, "out of memory");
            return -1;
        }
        for (size_t i = 0u; i < take; ++i)
            s->message[s->message_length + i] = (char)(at[i] ^ s->mask[(s->mask_at + i) & 3u]);
        s->message_length += take;
        s->start += take;
        s->left -= take;
        s->mask_at = (unsigned)((s->mask_at + take) & 3u);
        if (s->left) continue;
        s->in_frame = false;
        if (!s->fin) {
            s->assembling = true;
            continue;
        }
        s->assembling = false;
        if (!utf8_valid(s->message, s->message_length)) {
            server_fail(c, s, 1007, "text is not UTF-8");
            return -1;
        }
        bool reading;
        if (!inbox_push(c, s->message, s->message_length, &reading)) {
            server_fail(c, s, 1011, "out of memory");
            return -1;
        }
        s->message_length = 0u;
        if (!reading) return 0;
    }
}

static void server_serve(CettaWsConnection *c, WsServer *s) {
    char why[96];
    for (;;) {
        /* Write the frame in progress, then a pong, queued frames in order,
         * the close frame when it is due, or a ping. */
        bool want_write = false;
        while (!s->close_sent) {
            if (s->wire.kind == WIRE_NONE) {
                pthread_mutex_lock(&c->lock);
                WsText *frame = c->outbox;
                bool close_due = !frame && c->close_requested;
                int close_code = c->close_code;
                char close_reason[124];
                memcpy(close_reason, c->close_reason, sizeof(close_reason));
                pthread_mutex_unlock(&c->lock);
                if (s->pong_due) {
                    wire_start(&s->wire, WIRE_PONG, 10u, s->pong, s->pong_length);
                    s->pong_due = false;
                } else if (frame) {
                    wire_start(&s->wire, WIRE_TEXT, 1u, (const uint8_t *)frame->bytes, frame->length);
                } else if (close_due) {
                    char payload[125];
                    size_t n = close_payload(payload, close_code, close_reason);
                    wire_start(&s->wire, WIRE_CLOSE, 8u, (const uint8_t *)payload, n);
                    s->sent_code = close_code;
                    memcpy(s->sent_reason, close_reason, sizeof(s->sent_reason));
                } else if (s->ping_due) {
                    wire_start(&s->wire, WIRE_PING, 9u, NULL, 0u);
                    s->ping_due = false;
                    s->ping_sent = true;
                } else break;
            }
            int written = wire_write(c->fd, &s->wire);
            if (!written) {
                want_write = true;
                break;
            }
            if (written < 0) {
                if (s->wire.kind == WIRE_CLOSE) finish(c, s->sent_code, s->sent_reason, strlen(s->sent_reason), 0, "");
                else {
                    const char *text = errno_text(errno, why, sizeof(why));
                    finish(c, 1006, text, strlen(text), 0, "");
                }
                return;
            }
            if (s->wire.kind == WIRE_TEXT) {
                pthread_mutex_lock(&c->lock);
                WsText *frame = c->outbox;
                c->outbox = frame->next;
                if (!c->outbox) c->outbox_tail = NULL;
                c->sent++;
                pthread_mutex_unlock(&c->lock);
                free(frame->bytes);
                free(frame);
                notify(c);
            } else if (s->wire.kind == WIRE_CLOSE) {
                s->close_sent = true;
                s->close_deadline = now_ms() + WS_CLOSE_WAIT_MS;
            }
            s->wire.kind = WIRE_NONE;
        }

        /* Read and take frames while the queue has room. */
        pthread_mutex_lock(&c->lock);
        bool reading = c->inbox_bytes < c->max_message_bytes;
        pthread_mutex_unlock(&c->lock);
        while (reading) {
            int taken = server_frames(c, s);
            if (taken < 0) return;
            if (!taken) {
                reading = false;
                break;
            }
            if (s->start) {
                memmove(s->in, s->in + s->start, s->length - s->start);
                s->length -= s->start;
                s->start = 0u;
            }
            if (s->length == WS_REQUEST_MAX) {
                /* Unreachable: a header or control frame is far shorter. */
                server_fail(c, s, 1011, "input overflow");
                return;
            }
            ssize_t n = recv(c->fd, s->in + s->length, WS_REQUEST_MAX - s->length, MSG_DONTWAIT);
            if (n > 0) {
                s->length += (size_t)n;
                s->heard = now_ms();
                s->ping_sent = false;
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (s->close_sent) finish(c, s->sent_code, s->sent_reason, strlen(s->sent_reason), 0, "");
            else {
                const char *text = n ? errno_text(errno, why, sizeof(why)) : "the client left without a close frame";
                finish(c, 1006, text, strlen(text), 0, "");
            }
            return;
        }

        if ((s->pong_due || s->ping_due) && !want_write && !s->close_sent) continue;
        uint64_t now = now_ms();
        /* A full queue is the evaluator's delay, not the client's silence. */
        if (!reading) s->heard = now;
        if (s->close_sent && now >= s->close_deadline) {
            finish(c, s->sent_code, s->sent_reason, strlen(s->sent_reason), 0, "");
            return;
        }
        uint64_t due = s->close_sent ? s->close_deadline : 0u;
        if (c->idle_ms && !s->close_sent) {
            uint64_t idle = c->idle_ms, quiet = now - s->heard;
            if (quiet >= 2u * idle) {
                static const char silent[] = "the client did not answer a ping";
                finish(c, 1006, silent, sizeof(silent) - 1u, 0, "");
                return;
            }
            if (!s->ping_sent && quiet >= idle) {
                s->ping_due = true;
                continue;
            }
            due = s->heard + (s->ping_sent ? 2u * idle : idle);
        }
        /* As for a client: a full queue leaves the socket out of the wait. */
        struct pollfd fds[2] = {
            {c->control[0], POLLIN, 0},
            {c->fd, (short)((reading ? POLLIN : 0) | (want_write ? POLLOUT : 0)), 0},
        };
        if (poll(fds, reading || want_write ? 2u : 1u, poll_timeout(due, now)) < 0 && errno != EINTR) {
            static const char failed[] = "waiting on the connection failed";
            finish(c, 1006, failed, sizeof(failed) - 1u, 0, "");
            return;
        }
        if (fds[0].revents & POLLIN) {
            char drain[64];
            while (read(c->control[0], drain, sizeof(drain)) > 0) {}
        }
    }
}

static void *server_worker(void *argument) {
    CettaWsConnection *c = argument;
    WsServer s;
    memset(&s, 0, sizeof(s));
    s.in = malloc(WS_REQUEST_MAX);
    if (s.in && server_handshake(c, &s)) {
        s.heard = now_ms();
        server_serve(c, &s);
    } else finish(c, 1006, "", 0u, 0, "");
    free(s.message);
    free(s.in);
    unreference(c);
    return NULL;
}

struct CettaWsListener {
    int fd;
    unsigned port;
    size_t max_message_bytes;
    unsigned max_connections;
    uint32_t idle_ms;
    int wakeup_fd;
    WsGate *gate;
    CettaWsConnection *handshaking;  /* linked by listener_next */
    CettaWsConnection *ready, *ready_tail;
    uint64_t resume_ms;
};

CettaWsListener *cetta_ws_listen(const char *host, unsigned port, size_t max_message_bytes,
                                 unsigned max_connections, uint32_t idle_ms, int wakeup_fd,
                                 char *error, size_t error_size) {
    char service[8], why[96];
    if (!host || !*host || port > 65535u || !max_message_bytes || !max_connections) {
        snprintf(error, error_size, "expected a host, a port up to 65535 and positive limits");
        return NULL;
    }
    snprintf(service, sizeof(service), "%u", port);
    struct addrinfo hints, *found = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    int status = getaddrinfo(host, service, &hints, &found);
    if (status) {
        snprintf(error, error_size, "%s", gai_strerror(status));
        return NULL;
    }
    int fd = -1, failure = EADDRNOTAVAIL;
    for (struct addrinfo *a = found; a && fd < 0; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) {
            failure = errno;
            continue;
        }
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, a->ai_addr, a->ai_addrlen) || listen(fd, 128)) {
            failure = errno;
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(found);
    struct sockaddr_storage bound;
    socklen_t length = sizeof(bound);
    if (fd >= 0 && getsockname(fd, (struct sockaddr *)&bound, &length)) {
        failure = errno;
        close(fd);
        fd = -1;
    }
    if (fd < 0) {
        snprintf(error, error_size, "%s", errno_text(failure, why, sizeof(why)));
        return NULL;
    }
    CettaWsListener *l = calloc(1, sizeof(*l));
    WsGate *gate = calloc(1, sizeof(*gate));
    if (!l || !gate) {
        free(l);
        free(gate);
        close(fd);
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    pthread_mutex_init(&gate->lock, NULL);
    gate->references = 1u;
    *l = (CettaWsListener){.fd = fd, .max_message_bytes = max_message_bytes,
                           .max_connections = max_connections, .idle_ms = idle_ms,
                           .wakeup_fd = wakeup_fd, .gate = gate};
    l->port = bound.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&bound)->sin6_port)
                                          : ntohs(((struct sockaddr_in *)&bound)->sin_port);
    return l;
}

unsigned cetta_ws_listener_port(const CettaWsListener *l) { return l->port; }

static bool listener_full(const CettaWsListener *l) {
    pthread_mutex_lock(&l->gate->lock);
    bool full = l->gate->live >= l->max_connections;
    pthread_mutex_unlock(&l->gate->lock);
    return full;
}

int cetta_ws_listener_fd(const CettaWsListener *l, uint64_t *resume_ms) {
    if (l->resume_ms && now_ms() < l->resume_ms) {
        *resume_ms = l->resume_ms;
        return -1;
    }
    return listener_full(l) ? -1 : l->fd;
}

static void peer_text(const struct sockaddr *address, socklen_t length, char *out, size_t size) {
    char host[NI_MAXHOST], service[NI_MAXSERV];
    if (getnameinfo(address, length, host, sizeof(host), service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV)) {
        snprintf(out, size, "unknown");
        return;
    }
    size_t host_length = strnlen(host, size), service_length = strnlen(service, size);
    bool brackets = address->sa_family == AF_INET6;
    if (host_length + service_length + 4u > size) {
        snprintf(out, size, "unknown");
        return;
    }
    char *at = out;
    if (brackets) *at++ = '[';
    memcpy(at, host, host_length);
    at += host_length;
    if (brackets) *at++ = ']';
    *at++ = ':';
    memcpy(at, service, service_length + 1u);
}

void cetta_ws_listener_pump(CettaWsListener *l) {
    for (CettaWsConnection **cursor = &l->handshaking; *cursor;) {
        CettaWsConnection *c = *cursor;
        CettaWsState state = cetta_ws_state(c, NULL);
        if (state == CETTA_WS_CONNECTING) {
            cursor = &c->listener_next;
            continue;
        }
        *cursor = c->listener_next;
        c->listener_next = NULL;
        if (state == CETTA_WS_OPEN) {
            if (l->ready_tail) l->ready_tail->listener_next = c; else l->ready = c;
            l->ready_tail = c;
        } else cetta_ws_release(c);
    }
    if (l->fd < 0 || (l->resume_ms && now_ms() < l->resume_ms)) return;
    l->resume_ms = 0u;
    /* A bounded batch; poll reports any clients left waiting at once.  While
     * the connections are at their limit, clients wait in the kernel's
     * queue: answering them at once would close sockets holding unread
     * requests, which resets them and can lose the answer. */
    for (unsigned accepted = 0u; accepted < 64u && !listener_full(l); ++accepted) {
        struct sockaddr_storage address;
        socklen_t length = sizeof(address);
        int fd = accept4(l->fd, (struct sockaddr *)&address, &length, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            /* Out of descriptors or memory, the client stays queued: pause
             * rather than spin on a listener that stays readable. */
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
                l->resume_ms = now_ms() + 100u;
                return;
            }
            continue; /* this client's own failure, as Linux reports it */
        }
        CettaWsConnection *c = connection_new(l->max_message_bytes, l->wakeup_fd);
        if (c) {
            c->fd = fd;
            c->idle_ms = l->idle_ms;
            c->gate = l->gate;
            peer_text((struct sockaddr *)&address, length, c->peer, sizeof(c->peer));
            pthread_mutex_lock(&l->gate->lock);
            l->gate->live++;
            l->gate->references++;
            pthread_mutex_unlock(&l->gate->lock);
            if (start_worker(c, server_worker)) {
                c->listener_next = l->handshaking;
                l->handshaking = c;
                continue;
            }
            gate_drop(l->gate, true);
            connection_free(c);
        }
        /* Without memory or a thread for it, the client is let go. */
        static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n"
                                   "Content-Length: 0\r\n\r\n";
        ssize_t ignored = send(fd, busy, sizeof(busy) - 1u, MSG_NOSIGNAL | MSG_DONTWAIT);
        (void)ignored;
        close(fd);
    }
}

CettaWsConnection *cetta_ws_listener_take(CettaWsListener *l) {
    CettaWsConnection *c = l->ready;
    if (!c) return NULL;
    l->ready = c->listener_next;
    if (!l->ready) l->ready_tail = NULL;
    c->listener_next = NULL;
    return c;
}

void cetta_ws_listener_untake(CettaWsListener *l, CettaWsConnection *c) {
    c->listener_next = l->ready;
    l->ready = c;
    if (!l->ready_tail) l->ready_tail = c;
}

void cetta_ws_listener_free(CettaWsListener *l) {
    if (!l) return;
    if (l->fd >= 0) close(l->fd);
    for (CettaWsConnection *lists[2] = {l->handshaking, l->ready}, **list = lists; list < lists + 2; ++list)
        while (*list) {
            CettaWsConnection *c = *list;
            *list = c->listener_next;
            c->listener_next = NULL;
            cetta_ws_release(c);
        }
    gate_drop(l->gate, false);
    free(l);
}

const CettaWsRequest *cetta_ws_request(CettaWsConnection *c) {
    return c && c->request.target ? &c->request : NULL;
}

/* ------------------------------------------------------------------------
 * Both kinds, for the evaluator */

CettaWsState cetta_ws_state(CettaWsConnection *c, CettaWsClosed *closed) {
    pthread_mutex_lock(&c->lock);
    CettaWsState state = c->state;
    if (closed && state == CETTA_WS_CLOSED) *closed = c->closed;
    pthread_mutex_unlock(&c->lock);
    return state;
}

bool cetta_ws_take(CettaWsConnection *c, char **text, size_t *length) {
    pthread_mutex_lock(&c->lock);
    WsText *head = c->inbox;
    bool was_full = c->inbox_bytes >= c->max_message_bytes;
    if (head) {
        c->inbox = head->next;
        if (!c->inbox) c->inbox_tail = NULL;
        c->inbox_bytes -= head->length;
    }
    bool resume = head && was_full && c->inbox_bytes < c->max_message_bytes;
    pthread_mutex_unlock(&c->lock);
    if (!head) return false;
    *text = head->bytes;
    *length = head->length;
    free(head);
    if (resume) poke(c);
    return true;
}

void cetta_ws_untake(CettaWsConnection *c, char *bytes, size_t length) {
    WsText *text = malloc(sizeof(*text));
    if (!text) {
        free(bytes);
        return;
    }
    *text = (WsText){NULL, bytes, length};
    pthread_mutex_lock(&c->lock);
    text->next = c->inbox;
    c->inbox = text;
    if (!c->inbox_tail) c->inbox_tail = text;
    c->inbox_bytes += length;
    pthread_mutex_unlock(&c->lock);
}

uint64_t cetta_ws_send(CettaWsConnection *c, const char *bytes, size_t length) {
    WsText *text = malloc(sizeof(*text));
    char *copy = malloc(length + 1u);
    if (!text || !copy) {
        free(text);
        free(copy);
        return 0;
    }
    memcpy(copy, bytes, length);
    copy[length] = '\0';
    *text = (WsText){NULL, copy, length};
    uint64_t position = 0;
    pthread_mutex_lock(&c->lock);
    if (c->state != CETTA_WS_CLOSED && !c->close_requested) {
        if (c->outbox_tail) c->outbox_tail->next = text; else c->outbox = text;
        c->outbox_tail = text;
        position = ++c->queued;
    }
    pthread_mutex_unlock(&c->lock);
    if (!position) {
        free(copy);
        free(text);
        return 0;
    }
    poke(c);
    return position;
}

bool cetta_ws_sent(CettaWsConnection *c, uint64_t position) {
    pthread_mutex_lock(&c->lock);
    bool sent = c->sent >= position;
    pthread_mutex_unlock(&c->lock);
    return sent;
}

void cetta_ws_close(CettaWsConnection *c, int code, const char *reason) {
    pthread_mutex_lock(&c->lock);
    if (!c->close_requested && c->state != CETTA_WS_CLOSED) {
        c->close_requested = true;
        c->close_code = code;
        snprintf(c->close_reason, sizeof(c->close_reason), "%s", reason ? reason : "");
    }
    pthread_mutex_unlock(&c->lock);
    poke(c);
}

void cetta_ws_release(CettaWsConnection *c) {
    if (!c) return;
    pthread_mutex_lock(&c->lock);
    c->wakeup_fd = -1;
    pthread_mutex_unlock(&c->lock);
    cetta_ws_close(c, 1001, "");
    unreference(c);
}

#else

CettaWsConnection *cetta_ws_open(const char *url, uint32_t connect_timeout_ms,
                                 size_t max_message_bytes, int wakeup_fd) {
    (void)url; (void)connect_timeout_ms; (void)max_message_bytes; (void)wakeup_fd;
    return NULL;
}
CettaWsConnection *cetta_tcp_open(const char *url, uint32_t connect_timeout_ms,
                                  size_t max_bytes, bool lines, int wakeup_fd) {
    (void)url; (void)connect_timeout_ms; (void)max_bytes; (void)lines; (void)wakeup_fd;
    return NULL;
}
CettaWsState cetta_ws_state(CettaWsConnection *c, CettaWsClosed *closed) {
    (void)c; (void)closed;
    return CETTA_WS_CLOSED;
}
bool cetta_ws_take(CettaWsConnection *c, char **text, size_t *length) {
    (void)c; (void)text; (void)length;
    return false;
}
void cetta_ws_untake(CettaWsConnection *c, char *text, size_t length) {
    (void)c; (void)length;
    free(text);
}
uint64_t cetta_ws_send(CettaWsConnection *c, const char *text, size_t length) {
    (void)c; (void)text; (void)length;
    return 0;
}
bool cetta_ws_sent(CettaWsConnection *c, uint64_t position) {
    (void)c; (void)position;
    return false;
}
void cetta_ws_close(CettaWsConnection *c, int code, const char *reason) {
    (void)c; (void)code; (void)reason;
}
void cetta_ws_release(CettaWsConnection *c) { (void)c; }
CettaWsListener *cetta_ws_listen(const char *host, unsigned port, size_t max_message_bytes,
                                 unsigned max_connections, uint32_t idle_ms, int wakeup_fd,
                                 char *error, size_t error_size) {
    (void)host; (void)port; (void)max_message_bytes; (void)max_connections; (void)idle_ms;
    (void)wakeup_fd;
    snprintf(error, error_size, "WebSocket listeners require a native host");
    return NULL;
}
unsigned cetta_ws_listener_port(const CettaWsListener *l) { (void)l; return 0u; }
int cetta_ws_listener_fd(const CettaWsListener *l, uint64_t *resume_ms) {
    (void)l; (void)resume_ms;
    return -1;
}
void cetta_ws_listener_pump(CettaWsListener *l) { (void)l; }
CettaWsConnection *cetta_ws_listener_take(CettaWsListener *l) { (void)l; return NULL; }
void cetta_ws_listener_untake(CettaWsListener *l, CettaWsConnection *c) { (void)l; (void)c; }
void cetta_ws_listener_free(CettaWsListener *l) { (void)l; }
const CettaWsRequest *cetta_ws_request(CettaWsConnection *c) { (void)c; return NULL; }

#endif
