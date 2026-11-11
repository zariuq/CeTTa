#include "ipc_session.h"
#include "storage_census.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
struct CettaIpcSession {
    atomic_size_t references;
    pthread_mutex_t gate;
    int fd;
    bool failed;
    CettaIpcExchange *active; /* Borrowed: the exchange retains this session. */
};
struct CettaIpcExchange {
    atomic_size_t references;
    CettaIpcSession *session;
    CettaIpcClient *client;
    bool accepted;
};
CettaIpcSession *cetta_ipc_session_new(int fd) {
    int flags=fcntl(fd,F_GETFL);if (flags<0 || !(flags&O_NONBLOCK)) return NULL;
    CettaIpcSession *s=calloc(1,sizeof(*s));if (!s) return NULL;
    if (pthread_mutex_init(&s->gate,NULL)) {free(s);return NULL;}
    atomic_init(&s->references,1);s->fd=fd;return s;
}
void cetta_ipc_session_retain(CettaIpcSession *s) {if (s) atomic_fetch_add(&s->references,1);}
void cetta_ipc_session_release(CettaIpcSession *s) {
    if (!s || atomic_fetch_sub(&s->references,1)!=1) return;
    if (s->fd>=0) close(s->fd);
    pthread_mutex_destroy(&s->gate);free(s);
}
CettaIpcSessionStatus cetta_ipc_session_begin(CettaIpcSession *s,CettaIpcFraming framing,
        const void *bytes,size_t size,uint64_t timeout,size_t maximum,CettaIpcExchange **out) {
    if (!out) return CETTA_IPC_SESSION_INVALID;
    *out=NULL;
    if (!s || !bytes || !size || !maximum || (framing!=CETTA_IPC_CSP && framing!=CETTA_IPC_CWP) ||
        (framing==CETTA_IPC_CSP && (size>UINT32_MAX || size>SIZE_MAX-8))) return CETTA_IPC_SESSION_INVALID;
    pthread_mutex_lock(&s->gate);
    CettaIpcSessionStatus status=CETTA_IPC_SESSION_OK;
    if (s->failed) status=CETTA_IPC_SESSION_UNAVAILABLE;
    else if (s->active) status=CETTA_IPC_SESSION_BUSY;
    else {
        CettaIpcExchange *x=calloc(1,sizeof(*x));
        if (!x) status=CETTA_IPC_SESSION_NOMEM;
        else {
            x->client=cetta_ipc_client_new_connected(s->fd,framing,bytes,size,timeout,maximum);
            if (!x->client) {free(x);status=CETTA_IPC_SESSION_NOMEM;}
            else {
                atomic_init(&x->references,1);x->session=s;cetta_ipc_session_retain(s);
                s->fd=-1;s->active=x;*out=x;
            }
        }
    }
    pthread_mutex_unlock(&s->gate);return status;
}
void cetta_ipc_exchange_retain(CettaIpcExchange *x) {if (x) atomic_fetch_add(&x->references,1);}
static void cancel_locked(CettaIpcExchange *x) {
    CettaIpcSession *s=x->session;
    if (s->active==x) {
        cetta_ipc_client_cancel(x->client);s->active=NULL;s->failed=true;
    }
}
void cetta_ipc_exchange_release(CettaIpcExchange *x) {
    if (!x || atomic_fetch_sub(&x->references,1)!=1) return;
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate);
    cancel_locked(x);cetta_ipc_client_free(x->client);pthread_mutex_unlock(&s->gate);
    free(x);cetta_ipc_session_release(s);
}
void cetta_ipc_exchange_pump(CettaIpcExchange *x,uint64_t work) {
    if (!x) return;
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate);
    if (s->active==x) {
        cetta_ipc_client_pump_bounded(x->client,work);
        if (cetta_ipc_client_ready(x->client) && cetta_ipc_client_error(x->client)) {s->active=NULL;s->failed=true;}
    }
    pthread_mutex_unlock(&s->gate);
}
bool cetta_ipc_exchange_accept(CettaIpcExchange *x) {
    if (!x) return false;
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate);
    bool ready=x->accepted;
    if (!ready && s->active==x && !s->failed && cetta_ipc_client_ready(x->client) && !cetta_ipc_client_error(x->client)) {
        int fd=cetta_ipc_client_take_connection(x->client);
        if (fd>=0) {s->fd=fd;s->active=NULL;x->accepted=ready=true;}
    }
    pthread_mutex_unlock(&s->gate);return ready;
}
bool cetta_ipc_exchange_cancel(CettaIpcExchange *x) {
    if (!x) return false;
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate);
    bool active=s->active==x;if (active) cancel_locked(x);
    pthread_mutex_unlock(&s->gate);return active;
}
/* A held exchange keeps its client buffers alive. Successful reply bytes are
 * immutable; their borrowed lifetime ends at the caller's exchange release. */
#define INSPECT(type,name,expression,fallback) \
type name(CettaIpcExchange *x) { \
    if (!x) return fallback; \
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate); \
    type result=(expression);pthread_mutex_unlock(&s->gate);return result; \
}
INSPECT(bool,cetta_ipc_exchange_ready,cetta_ipc_client_ready(x->client),false)
INSPECT(bool,cetta_ipc_exchange_sent,cetta_ipc_client_sent(x->client),false)
INSPECT(const char *,cetta_ipc_exchange_error,cetta_ipc_client_error(x->client),"unavailable")
INSPECT(int,cetta_ipc_exchange_fd,cetta_ipc_client_fd(x->client),-1)
INSPECT(short,cetta_ipc_exchange_events,cetta_ipc_client_events(x->client),0)
INSPECT(uint64_t,cetta_ipc_exchange_deadline,cetta_ipc_client_deadline(x->client),UINT64_MAX)
#undef INSPECT
const unsigned char *cetta_ipc_exchange_reply(CettaIpcExchange *x,size_t *size) {
    if (!x) {if (size) *size=0;return NULL;}
    CettaIpcSession *s=x->session;pthread_mutex_lock(&s->gate);
    const unsigned char *bytes=cetta_ipc_client_reply(x->client,size);
    pthread_mutex_unlock(&s->gate);return bytes;
}
/* Serialize private buffer inspection with transport progress. A consistent
 * census of the whole ownership graph still requires quiescence, as for other
 * native resources. Do not recursively lock the active exchange's session. */
void cetta_ipc_exchange_storage(const CettaIpcExchange *x,CettaStorageCensus *census) {
    if (!x || !cetta_storage_object(census,x,sizeof(*x),CETTA_STORAGE_BLOCK)) return;
    cetta_ipc_session_storage(x->session,census);
    pthread_mutex_lock(&x->session->gate);cetta_ipc_client_storage(x->client,census);
    pthread_mutex_unlock(&x->session->gate);
}
void cetta_ipc_session_storage(const CettaIpcSession *raw,CettaStorageCensus *census) {
    if (!raw || !cetta_storage_object(census,raw,sizeof(*raw),CETTA_STORAGE_BLOCK)) return;
    CettaIpcSession *s=(CettaIpcSession *)raw;pthread_mutex_lock(&s->gate);
    CettaIpcExchange *x=s->active;
    if (x && cetta_storage_object(census,x,sizeof(*x),CETTA_STORAGE_BLOCK)) cetta_ipc_client_storage(x->client,census);
    pthread_mutex_unlock(&s->gate);
}
