#ifndef CETTA_WS_CLIENT_H
#define CETTA_WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WebSocket client connections for the io runtime (RFC 6455 over libcurl's
 * connect-only WebSockets).  Native bytes only: no arena atoms or evaluator
 * callbacks cross this API.  Each connection has one thread that owns its
 * curl handle, reads complete text messages into a queue, writes queued text
 * frames in order, and writes one byte to the runtime's wakeup descriptor
 * whenever its state, queue or send count changes.
 *
 * Reading stops while the queue holds max_message_bytes or more, so a peer
 * that sends faster than the evaluator consumes is held back by TCP flow
 * control instead of by unbounded memory.  A message longer than
 * max_message_bytes closes the connection with code 1009, a binary message
 * with 1003, text that is not UTF-8 with 1007. */
typedef struct CettaWsConnection CettaWsConnection;

typedef enum { CETTA_WS_CONNECTING, CETTA_WS_OPEN, CETTA_WS_CLOSED } CettaWsState;

typedef struct {
    /* The close code received from the peer, or sent by this side; 1006 when
     * the connection ended without a close frame. */
    int code;
    char reason[124];
    /* A CURLcode when the transport failed, else 0, with curl's text. */
    int transport_code;
    char transport_message[160];
} CettaWsClosed;

/* Starts connecting.  NULL when the provider is unavailable or out of memory. */
CettaWsConnection *cetta_ws_open(const char *url, uint32_t connect_timeout_ms,
                                 size_t max_message_bytes, int wakeup_fd);
CettaWsState cetta_ws_state(CettaWsConnection *connection, CettaWsClosed *closed);
/* Removes the oldest received message; the caller frees *text. */
bool cetta_ws_take(CettaWsConnection *connection, char **text, size_t *length);
/* Returns a taken message to the front of the queue, as when the request
 * it was assigned to is cancelled; the connection takes ownership of text. */
void cetta_ws_untake(CettaWsConnection *connection, char *text, size_t length);
/* Queues one text frame and returns its position in the send order, or 0
 * when the connection is closed or memory is exhausted. */
uint64_t cetta_ws_send(CettaWsConnection *connection, const char *text, size_t length);
/* Whether the frame at this position has been written. */
bool cetta_ws_sent(CettaWsConnection *connection, uint64_t position);
/* Sends a close frame once queued frames are written, then waits up to five
 * seconds for the peer's. */
void cetta_ws_close(CettaWsConnection *connection, int code, const char *reason);
/* Drops the caller's reference; an open connection is closed with 1001. */
void cetta_ws_release(CettaWsConnection *connection);

#endif
