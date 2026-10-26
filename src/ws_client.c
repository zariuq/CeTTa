#define _GNU_SOURCE
#include "ws_client.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_BUILD_HTTP_PROVIDER_CURL
#define CETTA_BUILD_HTTP_PROVIDER_CURL 0
#endif

#if CETTA_BUILD_HTTP_PROVIDER_CURL && !defined(__EMSCRIPTEN__)
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define WS_CLOSE_WAIT_MS 5000u
#define WS_READ_CHUNK 16384u

typedef struct WsText {
    struct WsText *next;
    char *bytes;
    size_t length;
} WsText;

struct CettaWsConnection {
    pthread_mutex_t lock;
    int control[2];         /* the evaluator wakes the worker through this pipe */
    int wakeup_fd;          /* the runtime's; not owned */
    char *url;
    uint32_t connect_timeout_ms;
    size_t max_message_bytes;
    unsigned references;    /* the evaluator's and the worker's */
    CettaWsState state;
    CettaWsClosed closed;
    WsText *inbox, *inbox_tail;
    size_t inbox_bytes;
    WsText *outbox, *outbox_tail;
    uint64_t queued, sent;
    bool close_requested;
    int close_code;
    char close_reason[124];
};

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

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static void notify(CettaWsConnection *c) {
    if (c->wakeup_fd >= 0) { ssize_t ignored = write(c->wakeup_fd, "w", 1); (void)ignored; }
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
    pthread_mutex_destroy(&c->lock);
    free(c);
}

static void unreference(CettaWsConnection *c) {
    pthread_mutex_lock(&c->lock);
    bool last = --c->references == 0;
    pthread_mutex_unlock(&c->lock);
    if (last) connection_free(c);
}

static void finish(CettaWsConnection *c, int code, const char *reason, size_t reason_length,
                   int transport, const char *message) {
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

static bool utf8_valid(const char *bytes, size_t length) {
    uint32_t scalar, width;
    for (size_t at = 0u; at < length; at += width)
        if (!cetta_utf8_decode_one((const uint8_t *)bytes, length, at, &scalar, &width)) return false;
    return true;
}

static void *worker(void *argument) {
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
            WsText *text = malloc(sizeof(*text));
            char *bytes = malloc(length + 1u);
            if (!text || !bytes) {
                free(text);
                free(bytes);
                send_close(easy, fd, 1011, "out of memory");
                finish(c, 1011, "out of memory", 13u, CURLE_OUT_OF_MEMORY, curl_easy_strerror(CURLE_OUT_OF_MEMORY));
                goto done;
            }
            memcpy(bytes, message, length);
            bytes[length] = '\0';
            *text = (WsText){NULL, bytes, length};
            pthread_mutex_lock(&c->lock);
            if (c->inbox_tail) c->inbox_tail->next = text; else c->inbox = text;
            c->inbox_tail = text;
            c->inbox_bytes += length;
            reading = c->inbox_bytes < c->max_message_bytes;
            pthread_mutex_unlock(&c->lock);
            notify(c);
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
    CettaWsConnection *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (pipe2(c->control, O_CLOEXEC | O_NONBLOCK)) {
        free(c);
        return NULL;
    }
    pthread_mutex_init(&c->lock, NULL);
    c->wakeup_fd = wakeup_fd;
    c->url = strdup(url);
    c->connect_timeout_ms = connect_timeout_ms;
    c->max_message_bytes = max_message_bytes;
    c->references = 2u;
    c->state = CETTA_WS_CONNECTING;
    pthread_attr_t attributes;
    pthread_t thread;
    bool started = c->url && !pthread_attr_init(&attributes);
    if (started) {
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        started = !pthread_create(&thread, &attributes, worker, c);
        pthread_attr_destroy(&attributes);
    }
    if (!started) {
        connection_free(c);
        return NULL;
    }
    return c;
}

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
    cetta_ws_close(c, 1001, "");
    unreference(c);
}

#else

CettaWsConnection *cetta_ws_open(const char *url, uint32_t connect_timeout_ms,
                                 size_t max_message_bytes, int wakeup_fd) {
    (void)url; (void)connect_timeout_ms; (void)max_message_bytes; (void)wakeup_fd;
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

#endif
