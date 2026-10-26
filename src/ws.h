#ifndef CETTA_WS_H
#define CETTA_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WebSocket connections for the io runtime (RFC 6455): client connections
 * over libcurl's connect-only WebSockets, and server connections that a
 * listener accepts on a plain TCP socket.  Native bytes only: no arena atoms
 * or evaluator callbacks cross this API.  Each connection has one thread that
 * owns its transport, reads complete text messages into a queue, writes
 * queued text frames in order, and writes one byte to the runtime's wakeup
 * descriptor whenever its state, queue or send count changes.
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
    /* A CURLcode when a client's transport failed, else 0, with curl's text.
     * A server connection reports its transport failures as code 1006 with
     * a reason. */
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
/* Drops the caller's reference; an open connection is closed with 1001.  The
 * connection writes nothing to the wakeup descriptor afterwards, so the
 * runtime may close it. */
void cetta_ws_release(CettaWsConnection *connection);

/* A listener accepts TCP connections when the evaluator pumps it, and each
 * accepted connection's thread answers the opening handshake (RFC 6455
 * section 4.2) before it serves frames as above.  A server connection also
 * holds the client to the protocol: frames masked, reserved bits clear,
 * known opcodes, control frames whole and at most 125 bytes, continuation
 * frames only inside a message, valid close codes; a violation closes it
 * with 1002.  When nothing has arrived for idle_ms it sends a ping, and when
 * nothing has arrived for twice that it ends the connection (code 1006); the
 * handshake must complete within idle_ms.  idle_ms 0 means neither.  While
 * max_connections are live, the listener accepts nobody: further clients wait
 * in the kernel's queue until a connection ends. */
typedef struct CettaWsListener CettaWsListener;

/* Binds host and port (0 picks a free port) and listens.  NULL with a
 * description of the failure in error. */
CettaWsListener *cetta_ws_listen(const char *host, unsigned port, size_t max_message_bytes,
                                 unsigned max_connections, uint32_t idle_ms, int wakeup_fd,
                                 char *error, size_t error_size);
unsigned cetta_ws_listener_port(const CettaWsListener *listener);
/* The descriptor that becomes readable when a client is waiting, or -1
 * while the connections are at their limit (one that ends frees its place
 * before it wakes the runtime) or while accepting is paused, for lack of
 * descriptors, until *resume_ms on the monotonic clock. */
int cetta_ws_listener_fd(const CettaWsListener *listener, uint64_t *resume_ms);
/* Accepts waiting clients and collects finished handshakes.  Like the other
 * listener functions, only for the evaluator's thread. */
void cetta_ws_listener_pump(CettaWsListener *listener);
/* The connection whose handshake completed first among those not yet
 * taken, or NULL. */
CettaWsConnection *cetta_ws_listener_take(CettaWsListener *listener);
/* Returns a taken connection to the front, as when the request it was
 * assigned to is cancelled. */
void cetta_ws_listener_untake(CettaWsListener *listener, CettaWsConnection *connection);
/* Stops listening; connections not taken are released. */
void cetta_ws_listener_free(CettaWsListener *listener);

/* What a server connection's client sent in its opening handshake: the
 * request target, the peer's address and port, and the header fields in
 * order, names in lowercase.  NULL for a client connection. */
typedef struct {
    const char *target;
    const char *peer;
    size_t field_count;
    const char *const *names;
    const char *const *values;
} CettaWsRequest;
const CettaWsRequest *cetta_ws_request(CettaWsConnection *connection);

#endif
