#ifndef CETTA_VALUE_RPC_H
#define CETTA_VALUE_RPC_H
#include "atom.h"
#include "ipc_session.h"
typedef struct CettaValueChannel CettaValueChannel;
typedef struct CettaValueRpc CettaValueRpc;
typedef enum {
    CETTA_RPC_PENDING,CETTA_RPC_OK,CETTA_RPC_REMOTE_ERROR,CETTA_RPC_UNAVAILABLE,
    CETTA_RPC_UNCERTAIN,CETTA_RPC_CAPACITY,CETTA_RPC_LIMIT,CETTA_RPC_INVALID
} CettaValueRpcStatus;
CettaValueChannel *cetta_value_channel_new(CettaIpcSession *,size_t maximum_bytes,uint64_t timeout_ms);
void cetta_value_channel_retain(CettaValueChannel *);
void cetta_value_channel_release(CettaValueChannel *);
size_t cetta_value_channel_limit(const CettaValueChannel *);
void cetta_value_channel_set_send_limit(CettaValueChannel *,bool enabled);
bool cetta_value_channel_send_limit(const CettaValueChannel *);
CettaValueRpcStatus cetta_value_channel_negotiate_limit(CettaValueChannel *);
/* Requests/replies are portable data. Each submitted operation owns its exact
 * request identity and a fixed deadline. One channel can finish an earlier
 * framed exchange while its consumer still retains the decoded reply. */
CettaValueRpcStatus cetta_value_rpc_new(CettaValueChannel *,Atom *body,CettaValueRpc **);
void cetta_value_rpc_retain(CettaValueRpc *);
void cetta_value_rpc_release(CettaValueRpc *);
CettaValueRpcStatus cetta_value_rpc_step(CettaValueRpc *,uint64_t work);
CettaValueRpcStatus cetta_value_rpc_await(CettaValueRpc *);
CettaValueRpcStatus cetta_value_rpc_status(CettaValueRpc *);
Atom *cetta_value_rpc_result(CettaValueRpc *);
const char *cetta_value_rpc_reason(CettaValueRpc *);
bool cetta_value_rpc_sent(CettaValueRpc *);
void cetta_value_rpc_cancel(CettaValueRpc *);
/* Cancel only an operation that has never acquired the stream. An active
 * read must drain instead, so its cancellation cannot poison sibling reads. */
bool cetta_value_rpc_cancel_queued(CettaValueRpc *);
void cetta_value_rpc_poll(CettaValueRpc *,int *fd,short *events,uint64_t *deadline);
void cetta_value_channel_storage(const void *,CettaStorageCensus *);
void cetta_value_rpc_storage(const void *,CettaStorageCensus *);
#endif
