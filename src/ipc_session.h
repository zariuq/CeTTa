#ifndef CETTA_IPC_SESSION_H
#define CETTA_IPC_SESSION_H
#include "ipc_client.h"
typedef struct CettaIpcSession CettaIpcSession;
typedef struct CettaIpcExchange CettaIpcExchange;
typedef enum {
    CETTA_IPC_SESSION_OK,CETTA_IPC_SESSION_BUSY,CETTA_IPC_SESSION_UNAVAILABLE,
    CETTA_IPC_SESSION_NOMEM,CETTA_IPC_SESSION_INVALID
} CettaIpcSessionStatus;
/* Successful construction owns the nonblocking connected fd. Captured
 * children retain the session; releasing its parent need not disconnect. */
CettaIpcSession *cetta_ipc_session_new(int fd);
void cetta_ipc_session_retain(CettaIpcSession *);
void cetta_ipc_session_release(CettaIpcSession *);
CettaIpcSessionStatus cetta_ipc_session_begin(CettaIpcSession *,CettaIpcFraming,
    const void *,size_t,uint64_t timeout_ms,size_t max_reply,CettaIpcExchange **);
void cetta_ipc_exchange_retain(CettaIpcExchange *);
void cetta_ipc_exchange_release(CettaIpcExchange *);
void cetta_ipc_exchange_pump(CettaIpcExchange *,uint64_t work);
bool cetta_ipc_exchange_ready(CettaIpcExchange *);
bool cetta_ipc_exchange_sent(CettaIpcExchange *);
const char *cetta_ipc_exchange_error(CettaIpcExchange *);
const unsigned char *cetta_ipc_exchange_reply(CettaIpcExchange *,size_t *);
int cetta_ipc_exchange_fd(CettaIpcExchange *);
short cetta_ipc_exchange_events(CettaIpcExchange *);
uint64_t cetta_ipc_exchange_deadline(CettaIpcExchange *);
/* Validate reply identity/schema before accepting. Until then no other
 * exchange can consume this connection. Accepted reply bytes stay owned by
 * the exchange even while later exchanges use the same socket. */
bool cetta_ipc_exchange_accept(CettaIpcExchange *);
/* Cancel/reject a pending or unaccepted reply: the stream becomes unusable.
 * Cancellation after acceptance cannot revoke another exchange's socket. */
bool cetta_ipc_exchange_cancel(CettaIpcExchange *);
void cetta_ipc_session_storage(const CettaIpcSession *,struct CettaStorageCensus *);
void cetta_ipc_exchange_storage(const CettaIpcExchange *,struct CettaStorageCensus *);
#endif
