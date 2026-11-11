#define _GNU_SOURCE
#include "value_rpc.h"
#include "atom_wire.h"
#include "shared_transition.h"
#include "storage_census.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

struct CettaValueChannel {
    atomic_size_t references;
    atomic_bool send_limit;
    pthread_mutex_t gate;
    CettaIpcSession *session;
    size_t maximum;
    uint64_t timeout,counter;
    char nonce[33];
    CettaValueRpc *first,*last;
};
struct CettaValueRpc {
    atomic_size_t references;
    CettaValueChannel *channel;
    CettaValueRpc *previous,*next;
    CettaIpcExchange *exchange;
    unsigned char *packet;size_t packet_size;
    Arena arena;Atom *result;
    uint64_t deadline;
    char identifier[56];
    bool linked,blocked,sent;
    CettaValueRpcStatus status;
    const char *reason;
};
static uint64_t now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static bool nonce(char out[33]) {
    unsigned char bytes[16];size_t at=0;
    while(at<sizeof(bytes)) {ssize_t n=getrandom(bytes+at,sizeof(bytes)-at,0);if(n<0 && errno==EINTR)continue;if(n<=0)return false;at+=(size_t)n;}
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<16;i++){out[2*i]=hex[bytes[i]>>4];out[2*i+1]=hex[bytes[i]&15];}out[32]=0;return true;
}
CettaValueChannel *cetta_value_channel_new(CettaIpcSession *session,size_t maximum,uint64_t timeout) {
    if(!session || !maximum)return NULL;
    CettaValueChannel *c=calloc(1,sizeof(*c));if(!c)return NULL;
    if(!nonce(c->nonce) || pthread_mutex_init(&c->gate,NULL)){free(c);return NULL;}
    atomic_init(&c->references,1);atomic_init(&c->send_limit,false);c->session=session;cetta_ipc_session_retain(session);c->maximum=maximum;c->timeout=timeout;return c;
}
void cetta_value_channel_retain(CettaValueChannel *c){if(c)atomic_fetch_add(&c->references,1);}
void cetta_value_channel_release(CettaValueChannel *c) {
    if(!c || atomic_fetch_sub(&c->references,1)!=1)return;
    cetta_ipc_session_release(c->session);pthread_mutex_destroy(&c->gate);free(c);
}
size_t cetta_value_channel_limit(const CettaValueChannel *c){return c ? c->maximum : 0;}
void cetta_value_channel_set_send_limit(CettaValueChannel *c,bool enabled){if(c)atomic_store(&c->send_limit,enabled);}
bool cetta_value_channel_send_limit(const CettaValueChannel *c){return c && atomic_load(&c->send_limit);}
static void unlink_rpc(CettaValueRpc *r) {
    if(!r->linked)return;
    CettaValueChannel *c=r->channel;
    if(r->previous)r->previous->next=r->next;else c->first=r->next;
    if(r->next)r->next->previous=r->previous;else c->last=r->previous;
    r->previous=r->next=NULL;r->linked=false;
}
static CettaValueRpcStatus finish(CettaValueRpc *r,CettaValueRpcStatus status,const char *reason) {
    r->status=status;r->reason=reason;unlink_rpc(r);return status;
}
CettaValueRpcStatus cetta_value_rpc_new(CettaValueChannel *c,Atom *body,CettaValueRpc **out) {
    if(!out)return CETTA_RPC_INVALID;
    *out=NULL;
    if(!c || !body)return CETTA_RPC_INVALID;
    uint64_t start=now_ms();CettaValueRpc *r=calloc(1,sizeof(*r));if(!r)return CETTA_RPC_CAPACITY;
    atomic_init(&r->references,1);arena_init_detached(&r->arena);r->channel=c;cetta_value_channel_retain(c);
    r->deadline=!c->timeout || c->timeout>UINT64_MAX-start ? UINT64_MAX : start+c->timeout;
    pthread_mutex_lock(&c->gate);
    if(c->counter==UINT64_MAX) {if(!nonce(c->nonce)){pthread_mutex_unlock(&c->gate);cetta_value_rpc_release(r);return CETTA_RPC_UNAVAILABLE;}c->counter=0;}
    snprintf(r->identifier,sizeof(r->identifier),"%s-%llu",c->nonce,(unsigned long long)++c->counter);
    pthread_mutex_unlock(&c->gate);
    Arena request;arena_init_detached(&request);
    CettaAtomWireStatus status;
    {
        CETTA_SCOPED_SHARED_TRANSITION(encode);
        Atom *items[]={atom_symbol(&request,"request"),atom_string(&request,r->identifier),body,
            atom_int(&request,c->maximum>INT64_MAX ? INT64_MAX : (int64_t)c->maximum)};
        Atom *packet=atom_expr(&request,items,atomic_load(&c->send_limit) ? 4 : 3);
        status=cetta_atom_wire_encode_bounded(packet,c->maximum,&r->packet,&r->packet_size);
    }
    arena_free(&request);
    if(status!=ATOM_WIRE_OK) {cetta_value_rpc_release(r);return status==ATOM_WIRE_NOMEM ? CETTA_RPC_CAPACITY : status==ATOM_WIRE_LIMIT ? CETTA_RPC_LIMIT : CETTA_RPC_INVALID;}
    pthread_mutex_lock(&c->gate);r->previous=c->last;
    if(c->last)c->last->next=r;else c->first=r;
    c->last=r;r->linked=true;pthread_mutex_unlock(&c->gate);*out=r;return CETTA_RPC_PENDING;
}
void cetta_value_rpc_retain(CettaValueRpc *r){if(r)atomic_fetch_add(&r->references,1);}
void cetta_value_rpc_release(CettaValueRpc *r) {
    if(!r || atomic_fetch_sub(&r->references,1)!=1)return;
    CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);
    unlink_rpc(r);cetta_ipc_exchange_release(r->exchange);pthread_mutex_unlock(&c->gate);
    arena_free(&r->arena);free(r->packet);free(r);cetta_value_channel_release(c);
}
static void step_locked(CettaValueRpc *r,uint64_t work) {
    if(r->status!=CETTA_RPC_PENDING || !work)return;
    if(now_ms()>=r->deadline) {
        r->sent=r->exchange && cetta_ipc_exchange_sent(r->exchange);cetta_ipc_exchange_cancel(r->exchange);
        finish(r,r->sent ? CETTA_RPC_UNCERTAIN : CETTA_RPC_UNAVAILABLE,"timeout");return;
    }
    if(!r->exchange) {
        uint64_t now=now_ms();
        CettaIpcSessionStatus begun=cetta_ipc_session_begin(r->channel->session,CETTA_IPC_CSP,r->packet,r->packet_size,
            r->deadline==UINT64_MAX ? 0 : r->deadline>now ? r->deadline-now : 1,r->channel->maximum,&r->exchange);
        r->blocked=begun==CETTA_IPC_SESSION_BUSY;
        if(r->blocked)return;
        if(begun!=CETTA_IPC_SESSION_OK) {finish(r,begun==CETTA_IPC_SESSION_NOMEM ? CETTA_RPC_CAPACITY : CETTA_RPC_UNAVAILABLE,"session");return;}
        free(r->packet);r->packet=NULL;
    }
    cetta_ipc_exchange_pump(r->exchange,work);r->sent=cetta_ipc_exchange_sent(r->exchange);
    if(!cetta_ipc_exchange_ready(r->exchange))return;
    const char *error=cetta_ipc_exchange_error(r->exchange);
    if(error){finish(r,r->sent ? CETTA_RPC_UNCERTAIN : CETTA_RPC_UNAVAILABLE,error);return;}
    const unsigned char *bytes;size_t size;bytes=cetta_ipc_exchange_reply(r->exchange,&size);Atom *reply=NULL;
    CettaAtomWireStatus status=cetta_atom_wire_decode_bounded(&r->arena,bytes,size,r->channel->maximum,&reply);
    bool valid=status==ATOM_WIRE_OK && reply && reply->kind==ATOM_EXPR && reply->expr.len==4 && atom_is_symbol(reply->expr.elems[0],"reply");
    if(valid) {
        Atom *id=reply->expr.elems[1];valid=id->kind==ATOM_GROUNDED && id->ground.gkind==GV_STRING && atom_string_len(id)==strlen(r->identifier) && !memcmp(id->ground.sval,r->identifier,strlen(r->identifier));
    }
    bool ok=valid && atom_is_symbol(reply->expr.elems[2],"ok");
    if(!valid || (!ok && !atom_is_symbol(reply->expr.elems[2],"error")) || !cetta_ipc_exchange_accept(r->exchange)) {
        cetta_ipc_exchange_cancel(r->exchange);
        finish(r,status==ATOM_WIRE_NOMEM || status==ATOM_WIRE_LIMIT ? CETTA_RPC_LIMIT : CETTA_RPC_UNCERTAIN,
            status==ATOM_WIRE_NOMEM ? "reply-capacity" : status==ATOM_WIRE_LIMIT ? "reply-limit" : "malformed-reply");
        return;
    }
    r->result=reply->expr.elems[3];finish(r,ok ? CETTA_RPC_OK : CETTA_RPC_REMOTE_ERROR,ok ? NULL : "remote-error");
}
CettaValueRpcStatus cetta_value_rpc_step(CettaValueRpc *r,uint64_t work) {
    if(!r)return CETTA_RPC_INVALID;
    CETTA_SCOPED_SHARED_TRANSITION(decode);
    CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);
    while(c->first && !atomic_load(&c->first->references)) {
        CettaValueRpc *dead=c->first;cetta_ipc_exchange_cancel(dead->exchange);finish(dead,CETTA_RPC_UNAVAILABLE,"cancelled");
    }
    if(work && c->first)step_locked(c->first,work);
    CettaValueRpcStatus status=r->status;pthread_mutex_unlock(&c->gate);return status;
}
void cetta_value_rpc_poll(CettaValueRpc *r,int *fd,short *events,uint64_t *deadline) {
    if(fd)*fd=-1;
    if(events)*events=0;
    if(deadline)*deadline=UINT64_MAX;
    if(!r)return;
    CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);CettaValueRpc *first=c->first;
    if(first && first->exchange) {
        if(fd)*fd=cetta_ipc_exchange_fd(first->exchange);
        if(events)*events=cetta_ipc_exchange_events(first->exchange);
    }
    if(deadline)*deadline=first && first->deadline<r->deadline ? first->deadline : r->deadline;
    pthread_mutex_unlock(&c->gate);
}
CettaValueRpcStatus cetta_value_rpc_await(CettaValueRpc *r) {
    CettaValueRpcStatus status;
    while((status=cetta_value_rpc_step(r,65536))==CETTA_RPC_PENDING) {
        int fd;short events;uint64_t deadline;cetta_value_rpc_poll(r,&fd,&events,&deadline);
        if(fd<0) {
            CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);
            if(c->first && c->first->blocked && !c->first->exchange)finish(r,CETTA_RPC_UNAVAILABLE,"busy-connection");
            pthread_mutex_unlock(&c->gate);continue;
        }
        uint64_t now=now_ms();if(now>=deadline)continue;
        uint64_t left=deadline-now;struct pollfd p={fd,events,0};int n=poll(&p,1,left>INT_MAX ? INT_MAX : (int)left);
        if(n<0 && errno!=EINTR){cetta_value_rpc_cancel(r);break;}
    }
    return cetta_value_rpc_status(r);
}
CettaValueRpcStatus cetta_value_channel_negotiate_limit(CettaValueChannel *c) {
    if(!c)return CETTA_RPC_INVALID;
    Arena a;arena_init_detached(&a);CettaValueRpc *rpc=NULL;
    CettaValueRpcStatus status;
    {
        CETTA_SCOPED_SHARED_TRANSITION(encode);
        Atom *body=atom_expr2(&a,atom_symbol(&a,"RequestLimitV1"),
            atom_int(&a,c->maximum>INT64_MAX ? INT64_MAX : (int64_t)c->maximum));
        status=cetta_value_rpc_new(c,body,&rpc);
    }
    arena_free(&a);
    if(status!=CETTA_RPC_PENDING)return status;
    status=cetta_value_rpc_await(rpc);
    {
        CETTA_SCOPED_SHARED_TRANSITION(decode);
        Atom *result=cetta_value_rpc_result(rpc);
        if(status==CETTA_RPC_OK) {
            if(result && result->kind==ATOM_EXPR && result->expr.len==2 &&
                atom_is_symbol(result->expr.elems[0],"ServiceRequestLimitV1") &&
                result->expr.elems[1]->kind==ATOM_GROUNDED &&
                result->expr.elems[1]->ground.gkind==GV_INT &&
                result->expr.elems[1]->ground.ival>0) {
                atomic_store(&c->send_limit,true);
            } else status=CETTA_RPC_INVALID;
        } else if(status==CETTA_RPC_REMOTE_ERROR && result && result->kind==ATOM_EXPR &&
            result->expr.len==2 && atom_is_symbol(result->expr.elems[0],"ServiceRefusedV1") &&
            atom_is_symbol(result->expr.elems[1],"request-shape")) {
            atomic_store(&c->send_limit,false);
            status=CETTA_RPC_OK;
        }
    }
    cetta_value_rpc_release(rpc);
    return status;
}
void cetta_value_rpc_cancel(CettaValueRpc *r) {
    if(!r)return;
    CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);
    if(r->status==CETTA_RPC_PENDING){r->sent=r->exchange && cetta_ipc_exchange_sent(r->exchange);cetta_ipc_exchange_cancel(r->exchange);finish(r,r->sent ? CETTA_RPC_UNCERTAIN : CETTA_RPC_UNAVAILABLE,"cancelled");}
    pthread_mutex_unlock(&c->gate);
}
bool cetta_value_rpc_cancel_queued(CettaValueRpc *r) {
    if(!r)return false;
    CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);
    bool cancelled=r->status==CETTA_RPC_PENDING && !r->exchange;
    if(cancelled)finish(r,CETTA_RPC_UNAVAILABLE,"cancelled-before-exchange");
    pthread_mutex_unlock(&c->gate);return cancelled;
}
#define INSPECT(type,name,field,fallback) \
type name(CettaValueRpc *r){if(!r)return fallback;CettaValueChannel *c=r->channel;pthread_mutex_lock(&c->gate);type value=(field);pthread_mutex_unlock(&c->gate);return value;}
INSPECT(CettaValueRpcStatus,cetta_value_rpc_status,r->status,CETTA_RPC_INVALID)
INSPECT(Atom *,cetta_value_rpc_result,r->result,NULL)
INSPECT(const char *,cetta_value_rpc_reason,r->reason,"invalid")
INSPECT(bool,cetta_value_rpc_sent,r->sent,false)
#undef INSPECT
static void rpc_storage(const CettaValueRpc *r,CettaStorageCensus *census) {
    if(!r || !cetta_storage_object(census,r,sizeof(*r),CETTA_STORAGE_BLOCK))return;
    if(r->packet)cetta_storage_array(census,r->packet,r->packet_size,1,0);
    arena_storage_census(&r->arena,census);cetta_ipc_exchange_storage(r->exchange,census);
}
void cetta_value_channel_storage(const void *raw,CettaStorageCensus *census) {
    const CettaValueChannel *raw_channel=raw;if(!raw_channel || !cetta_storage_object(census,raw_channel,sizeof(*raw_channel),CETTA_STORAGE_BLOCK))return;
    CettaValueChannel *c=(CettaValueChannel *)raw_channel;pthread_mutex_lock(&c->gate);
    cetta_ipc_session_storage(c->session,census);for(CettaValueRpc *r=c->first;r;r=r->next)rpc_storage(r,census);
    pthread_mutex_unlock(&c->gate);
}
void cetta_value_rpc_storage(const void *raw,CettaStorageCensus *census) {
    const CettaValueRpc *r=raw;if(!r)return;cetta_value_channel_storage(r->channel,census);
    pthread_mutex_lock(&r->channel->gate);rpc_storage(r,census);pthread_mutex_unlock(&r->channel->gate);
}
