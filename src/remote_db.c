#define _POSIX_C_SOURCE 200809L
#include "remote_db.h"
#include "atom_wire.h"
#include "space_source_overlay.h"
#include "storage_census.h"
#include "shared_transition.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct Resource Resource;
typedef struct DiscardedRpc {
    CettaValueRpc *rpc;
    struct DiscardedRpc *next;
} DiscardedRpc;
struct CettaRemoteDb {
    atomic_size_t references;
    pthread_mutex_t gate;
    CettaValueChannel *channel;
    Arena metadata;
    Atom *owner,*incarnation,*kind;
    size_t maximum;
    bool writable;
    Resource *retired,*retired_last,*dropping;
    CettaValueRpc *cleanup;
    DiscardedRpc *discarded;
};
struct Resource {
    atomic_size_t references;
    CettaRemoteDb *db;
    char *token;
    CettaValueRpc *token_reply;
    bool linear,ended;
    Resource *next;
};
struct CettaRemoteCapture {
    atomic_size_t references;
    Resource *resource;
    Arena metadata;
    Atom *collection;
    uint64_t revision,count;
    size_t maximum;
};
static bool tagged(const Atom *a,const char *head,size_t n) {return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);}
static bool natural(const Atom *a,uint64_t *n) {if(!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT || a->ground.ival<0)return false;*n=(uint64_t)a->ground.ival;return true;}
static const char *text(const Atom *a) {return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING && !memchr(a->ground.sval,0,atom_string_len(a)) ? a->ground.sval : NULL;}
static void resource_free(Resource *r){if(r){if(!r->token_reply)free(r->token);cetta_value_rpc_release(r->token_reply);free(r);}}
void cetta_remote_db_retain(CettaRemoteDb *d){if(d)atomic_fetch_add(&d->references,1);}
void cetta_remote_db_release(CettaRemoteDb *d) {
    if(!d || atomic_fetch_sub(&d->references,1)!=1)return;
    cetta_value_rpc_release(d->cleanup);resource_free(d->dropping);
    DiscardedRpc *pending=d->discarded;
    while(pending){DiscardedRpc *next=pending->next;cetta_value_rpc_release(pending->rpc);free(pending);pending=next;}
    Resource *r=d->retired;while(r){Resource *next=r->next;resource_free(r);r=next;}
    cetta_value_channel_release(d->channel);arena_free(&d->metadata);pthread_mutex_destroy(&d->gate);free(d);
}
static Resource *resource_new(CettaRemoteDb *d,Atom *token) {
    const char *name=text(token);if(!name || !*name)return NULL;
    Resource *r=calloc(1,sizeof(*r));if(!r)return NULL;
    r->token=strdup(name);if(!r->token){free(r);return NULL;}
    atomic_init(&r->references,1);r->db=d;cetta_remote_db_retain(d);return r;
}
static void resource_retain(Resource *r){if(r)atomic_fetch_add(&r->references,1);}
static void resource_release(Resource *r) {
    if(!r || atomic_fetch_sub(&r->references,1)!=1)return;
    CettaRemoteDb *d=r->db;
    if(r->ended){resource_free(r);cetta_remote_db_release(d);return;}
    pthread_mutex_lock(&d->gate);
    if(d->retired_last)d->retired_last->next=r;else d->retired=r;
    d->retired_last=r;pthread_mutex_unlock(&d->gate);cetta_remote_db_release(d);
}
/* Retire one remote resource before submitting new work. The descriptor was
 * allocated at admission; a destructor never allocates or performs socket IO.
 * Final channel close lets the server collect any remaining connection state. */
static CettaValueRpcStatus cleanup(CettaRemoteDb *d) {
    CETTA_SCOPED_SHARED_TRANSITION(encode);
    pthread_mutex_lock(&d->gate);
    DiscardedRpc **at=&d->discarded;
    while(*at) {
        DiscardedRpc *item=*at;
        CettaValueRpcStatus state=cetta_value_rpc_status(item->rpc);
        if(state==CETTA_RPC_PENDING){at=&item->next;continue;}
        Atom *answer=cetta_value_rpc_result(item->rpc);
        if(state==CETTA_RPC_OK && tagged(answer,"ServiceResourceV1",3)) {
            const char *token=text(answer->expr.elems[1]);
            if(!token || !*token){pthread_mutex_unlock(&d->gate);return CETTA_RPC_INVALID;}
            Resource *r=calloc(1,sizeof(*r));
            if(r)r->token=strdup(token);
            if(!r || !r->token){resource_free(r);pthread_mutex_unlock(&d->gate);return CETTA_RPC_CAPACITY;}
            atomic_init(&r->references,0);r->db=d;
            if(d->retired_last)d->retired_last->next=r;else d->retired=r;
            d->retired_last=r;
        }
        *at=item->next;cetta_value_rpc_release(item->rpc);free(item);
    }
    if(d->cleanup && cetta_value_rpc_status(d->cleanup)!=CETTA_RPC_PENDING) {
        cetta_value_rpc_release(d->cleanup);d->cleanup=NULL;resource_free(d->dropping);d->dropping=NULL;
    }
    CettaValueRpcStatus status=CETTA_RPC_OK;
    if(!d->cleanup && d->retired) {
        Resource *r=d->retired;Arena a;arena_init_detached(&a);
        Atom *body=atom_expr2(&a,atom_symbol(&a,"DropV1"),atom_string(&a,r->token));
        status=cetta_value_rpc_new(d->channel,body,&d->cleanup);arena_free(&a);
        if(status==CETTA_RPC_PENDING) {d->retired=r->next;if(!d->retired)d->retired_last=NULL;r->next=NULL;d->dropping=r;status=CETTA_RPC_OK;}
    }
    pthread_mutex_unlock(&d->gate);return status;
}
static CettaValueRpcStatus request_mode(CettaRemoteDb *d,const char *token,bool linear,const char *operation,Atom *argument,CettaValueRpc **out) {
    CETTA_SCOPED_SHARED_TRANSITION(encode);
    CettaValueRpcStatus status=cleanup(d);if(status!=CETTA_RPC_OK)return status;
    Arena a;arena_init_detached(&a);
    Atom *body=token ? atom_expr(&a,(Atom *[]){atom_symbol(&a,linear ? "UseValueV1" : "UseV1"),atom_string(&a,token),atom_symbol(&a,operation),argument},4)
        : atom_expr3(&a,atom_symbol(&a,"CallV1"),atom_symbol(&a,operation),argument);
    status=cetta_value_rpc_new(d->channel,body,out);arena_free(&a);return status;
}
static CettaValueRpcStatus request(CettaRemoteDb *d,const char *token,const char *operation,Atom *argument,CettaValueRpc **out) {
    return request_mode(d,token,false,operation,argument,out);
}
static CettaValueRpcStatus call(CettaRemoteDb *d,const char *token,const char *operation,Atom *argument,Arena *arena,Atom **out) {
    *out=NULL;CettaValueRpc *rpc=NULL;CettaValueRpcStatus status=request(d,token,operation,argument,&rpc);
    if(status!=CETTA_RPC_PENDING)return status;
    status=cetta_value_rpc_await(rpc);
    if(status==CETTA_RPC_OK || status==CETTA_RPC_REMOTE_ERROR) {
        CETTA_SCOPED_SHARED_TRANSITION(receive);
        *out=atom_deep_copy(arena,cetta_value_rpc_result(rpc));if(!*out)status=CETTA_RPC_CAPACITY;
    }
    cetta_value_rpc_release(rpc);return status;
}
CettaValueRpcStatus cetta_remote_db_open(CettaIpcSession *session,size_t maximum,uint64_t timeout,CettaRemoteDb **out) {
    if(!out)return CETTA_RPC_INVALID;
    *out=NULL;CettaRemoteDb *d=calloc(1,sizeof(*d));if(!d)return CETTA_RPC_CAPACITY;
    if(pthread_mutex_init(&d->gate,NULL)){free(d);return CETTA_RPC_CAPACITY;}
    atomic_init(&d->references,1);arena_init_detached(&d->metadata);
    d->channel=cetta_value_channel_new(session,maximum,timeout);if(!d->channel){cetta_remote_db_release(d);return CETTA_RPC_CAPACITY;}
    Atom *caps=NULL,*unit=atom_expr(&d->metadata,NULL,0);
    CettaValueRpcStatus status=call(d,NULL,"DbCapabilities",unit,&d->metadata,&caps);uint64_t server_limit;
    if(status==CETTA_RPC_OK) {
        CETTA_SCOPED_SHARED_TRANSITION(capabilities);
        if(!tagged(caps,"DbCapabilitiesV1",6) || atom_has_vars(caps) || !natural(caps->expr.elems[5],&server_limit) || !server_limit || server_limit>SIZE_MAX ||
            !(atom_is_symbol(caps->expr.elems[3],"directory") || atom_is_symbol(caps->expr.elems[3],"act")) ||
            !(atom_is_symbol(caps->expr.elems[4],"ReadWrite") || atom_is_symbol(caps->expr.elems[4],"ReadOnly"))) status=CETTA_RPC_INVALID;
        else if(atom_is_symbol(caps->expr.elems[3],"directory") &&
            (!tagged(caps->expr.elems[1],"DbOwnerV1",3) || !text(caps->expr.elems[1]->expr.elems[1]) ||
             !text(caps->expr.elems[1]->expr.elems[2]) || !*text(caps->expr.elems[1]->expr.elems[2])))status=CETTA_RPC_INVALID;
        else {
            d->owner=caps->expr.elems[1];d->incarnation=caps->expr.elems[2];d->kind=caps->expr.elems[3];
            d->maximum=server_limit<maximum ? (size_t)server_limit : maximum;d->writable=atom_is_symbol(caps->expr.elems[4],"ReadWrite") && atom_is_symbol(d->kind,"directory");
        }
    }
    if(status==CETTA_RPC_OK) {
        Atom *protocol=NULL;status=call(d,NULL,"DbProtocolInfo",unit,&d->metadata,&protocol);
        CETTA_SCOPED_SHARED_TRANSITION(negotiation);
        uint64_t version=0;bool originals=false,positions=false,domains=false,retained=false;
        if(status==CETTA_RPC_OK) {
            if(!tagged(protocol,"DbProtocolV1",3) || !natural(protocol->expr.elems[1],&version) || version!=1 ||
                protocol->expr.elems[2]->kind!=ATOM_EXPR || atom_has_vars(protocol))status=CETTA_RPC_INVALID;
            else {
                Atom *features=protocol->expr.elems[2];
                for(size_t i=0;i<features->expr.len;i++) {
                    Atom *feature=features->expr.elems[i];
                    if(feature->kind!=ATOM_SYMBOL){status=CETTA_RPC_INVALID;break;}
                    originals|=atom_is_symbol(feature,"OriginalFactsV1");positions|=atom_is_symbol(feature,"ImmutablePositionsV1");
                    domains|=atom_is_symbol(feature,"PhysicalDomainsV1");
                    retained|=atom_is_symbol(feature,"RetainedSessionsV1");
                }
                if(!originals || !positions || !domains || !retained)status=CETTA_RPC_INVALID;
            }
        }
    }
    if(status==CETTA_RPC_OK)status=cetta_value_channel_negotiate_limit(d->channel);
    if(status!=CETTA_RPC_OK){cetta_remote_db_release(d);return status;}*out=d;return status;
}
bool cetta_remote_db_writable(const CettaRemoteDb *d){return d && d->writable;}
bool cetta_remote_db_same_domain(const CettaRemoteDb *a,const CettaRemoteDb *b) {
    /* Epoch/incarnation validate observations. The physical directory is the
     * commit domain, including across activations or logical epoch changes. */
    if(!a || !b || !atom_eq(a->kind,b->kind))return false;
    if(atom_is_symbol(a->kind,"directory"))
        return tagged(a->owner,"DbOwnerV1",3) && tagged(b->owner,"DbOwnerV1",3) &&
            atom_eq(a->owner->expr.elems[2],b->owner->expr.elems[2]);
    return atom_eq(a->owner,b->owner);
}
CettaRemoteDb *cetta_remote_capture_db(const CettaRemoteCapture *s){return s ? s->resource->db : NULL;}
CettaValueRpcStatus cetta_remote_capture_prepare_publish(CettaRemoteCapture *s,Atom *proposal,CettaValueRpc **out) {
    if(!s || !proposal || !out)return CETTA_RPC_INVALID;
    return request(s->resource->db,s->resource->token,"DbPublish",proposal,out);
}

Atom *cetta_remote_db_context(const CettaRemoteDb *d,Arena *arena) {
    return d ? atom_expr(arena,(Atom *[]){atom_symbol(arena,"DbRemoteContextV1"),atom_deep_copy(arena,d->owner),
        atom_deep_copy(arena,d->incarnation),atom_deep_copy(arena,d->kind),atom_bool(arena,d->writable)},5) : NULL;
}
CettaValueRpcStatus cetta_remote_db_call(CettaRemoteDb *d,const char *operation,Atom *arguments,Arena *arena,Atom **out) {
    if(!d || !operation || !arguments || !arena || !out)return CETTA_RPC_INVALID;
    return call(d,NULL,operation,arguments,arena,out);
}
static CettaValueRpcStatus start(CettaRemoteDb *d,const char *token,const char *operation,Atom *arguments,
        Arena *arena,CettaRemoteResource **out,Atom **metadata) {
    if(!out || !metadata)return CETTA_RPC_INVALID;
    *out=NULL;*metadata=NULL;Atom *answer=NULL;
    CettaValueRpcStatus status=call(d,token,operation,arguments,arena,&answer);
    CETTA_SCOPED_SHARED_TRANSITION(adopt);
    if(status!=CETTA_RPC_OK){*metadata=answer;return status;}
    if(!tagged(answer,"ServiceResourceV1",3)){*metadata=answer;return CETTA_RPC_INVALID;}
    *out=resource_new(d,answer->expr.elems[1]);
    if(!*out)return CETTA_RPC_CAPACITY;
    *metadata=answer->expr.elems[2];
    if(tagged(*metadata,"DbLinearV1",2)){(*out)->linear=true;*metadata=(*metadata)->expr.elems[1];}
    return CETTA_RPC_OK;
}
CettaValueRpcStatus cetta_remote_db_start(CettaRemoteDb *d,const char *operation,Atom *arguments,
        Arena *arena,CettaRemoteResource **out,Atom **metadata) {
    return start(d,NULL,operation,arguments,arena,out,metadata);
}
CettaValueRpcStatus cetta_remote_resource_start(CettaRemoteResource *r,const char *operation,Atom *arguments,
        Arena *arena,CettaRemoteResource **out,Atom **metadata) {
    return r && !r->linear && !r->ended ? start(r->db,r->token,operation,arguments,arena,out,metadata) : CETTA_RPC_INVALID;
}
CettaValueRpcStatus cetta_remote_capture_start(CettaRemoteCapture *s,const char *operation,Atom *arguments,
        Arena *arena,CettaRemoteResource **out,Atom **metadata) {
    return s ? start(s->resource->db,s->resource->token,operation,arguments,arena,out,metadata) : CETTA_RPC_INVALID;
}
CettaValueRpcStatus cetta_remote_resource_call(CettaRemoteResource *r,const char *operation,
        Atom *arguments,Arena *arena,Atom **out) {
    if(!r || r->ended)return CETTA_RPC_INVALID;
    if(!r->linear)return call(r->db,r->token,operation,arguments,arena,out);
    *out=NULL;CettaValueRpc *rpc=NULL;
    CettaValueRpcStatus status=request_mode(r->db,r->token,true,operation,arguments,&rpc);
    if(status!=CETTA_RPC_PENDING)return status;
    status=cetta_value_rpc_await(rpc);
    CETTA_SCOPED_SHARED_TRANSITION(adopt);
    Atom *answer=cetta_value_rpc_result(rpc);
    if(status==CETTA_RPC_OK && tagged(answer,"ServiceResourceV1",3)) {
        const char *token=text(answer->expr.elems[1]);Atom *metadata=answer->expr.elems[2];
        if(!token || !*token || !tagged(metadata,"DbLinearV1",2)){r->ended=true;status=CETTA_RPC_INVALID;}
        else {
            /* Keep the accepted reply as the successor token's owner before
             * releasing its predecessor. Adoption needs no new allocation. */
            CettaValueRpc *old=r->token_reply;
            if(!old)free(r->token);
            r->token_reply=rpc;cetta_value_rpc_retain(rpc);r->token=(char *)token;
            cetta_value_rpc_release(old);answer=metadata->expr.elems[1];
        }
    } else if(status!=CETTA_RPC_REMOTE_ERROR || !tagged(answer,"ServiceRefusedV1",2)) r->ended=true;
    if(status==CETTA_RPC_OK || status==CETTA_RPC_REMOTE_ERROR) {
        *out=atom_deep_copy(arena,answer);if(!*out)status=CETTA_RPC_CAPACITY;
    }
    cetta_value_rpc_release(rpc);return status;
}
CettaRemoteDb *cetta_remote_resource_db(const CettaRemoteResource *r){return r ? r->db : NULL;}
void cetta_remote_resource_release(CettaRemoteResource *r){resource_release(r);}
void cetta_remote_capture_retain(CettaRemoteCapture *s){if(s)atomic_fetch_add(&s->references,1);}
void cetta_remote_capture_release(CettaRemoteCapture *s) {
    if(!s || atomic_fetch_sub(&s->references,1)!=1)return;
    resource_release(s->resource);arena_free(&s->metadata);free(s);
}
static CettaValueRpcStatus capture_on(CettaRemoteDb *d,CettaRemoteCapture *origin,Atom *collection,CettaRemoteCapture **out) {
    if(!out)return CETTA_RPC_INVALID;
    *out=NULL;if(!d || !collection || atom_has_vars(collection))return CETTA_RPC_INVALID;
    CettaRemoteCapture *s=calloc(1,sizeof(*s));if(!s)return CETTA_RPC_CAPACITY;
    atomic_init(&s->references,1);arena_init_detached(&s->metadata);
    Atom *answer=NULL,*argument=NULL;
    {
        CETTA_SCOPED_SHARED_TRANSITION(encode);
        s->collection=atom_deep_copy(&s->metadata,collection);
        argument=atom_expr2(&s->metadata,atom_symbol(&s->metadata,"DbCaptureV1"),s->collection);
    }
    CettaValueRpcStatus status=call(d,origin ? origin->resource->token : NULL,
        origin ? "DbCaptureRelated" : "DbCapture",argument,&s->metadata,&answer);uint64_t limit;
    if(status==CETTA_RPC_OK) {
        CETTA_SCOPED_SHARED_TRANSITION(metadata);
        Atom *meta=tagged(answer,"ServiceResourceV1",3) ? answer->expr.elems[2] : NULL;
        if(meta)s->resource=resource_new(d,answer->expr.elems[1]);
        if(!tagged(meta,"DbCapturedV1",8) || !atom_eq(meta->expr.elems[1],d->owner) || !atom_eq(meta->expr.elems[2],d->incarnation) ||
            !natural(meta->expr.elems[3],&s->revision) || (origin && s->revision!=origin->revision) ||
            !atom_eq(meta->expr.elems[4],collection) || !natural(meta->expr.elems[5],&s->count) ||
            !natural(meta->expr.elems[6],&limit) || !limit || limit>SIZE_MAX || !atom_eq(meta->expr.elems[7],d->kind))status=CETTA_RPC_INVALID;
        else {
            if(!s->resource)status=CETTA_RPC_CAPACITY;
            s->maximum=limit<d->maximum ? (size_t)limit : d->maximum;
        }
    }
    if(status!=CETTA_RPC_OK){cetta_remote_capture_release(s);return status;}*out=s;return status;
}
CettaValueRpcStatus cetta_remote_db_capture(CettaRemoteDb *d,Atom *collection,CettaRemoteCapture **out) {
    return capture_on(d,NULL,collection,out);
}
CettaValueRpcStatus cetta_remote_capture_related(CettaRemoteCapture *s,Atom *collection,CettaRemoteCapture **out) {
    return s ? capture_on(s->resource->db,s,collection,out) : CETTA_RPC_INVALID;
}
Atom *cetta_remote_capture_context(const CettaRemoteCapture *s,Arena *a) {
    if(!s || s->revision>INT64_MAX)return NULL;
    CettaRemoteDb *d=s->resource->db;
    return atom_expr(a,(Atom *[]){atom_symbol(a,"DbCapturedContextV1"),atom_deep_copy(a,d->owner),atom_deep_copy(a,d->incarnation),
        atom_int(a,(int64_t)s->revision),atom_deep_copy(a,s->collection)},5);
}
CettaValueRpcStatus cetta_remote_capture_publish(CettaRemoteCapture *s,Atom *proposal,Arena *a,Atom **out) {
    if(!s || !proposal || !a || !out)return CETTA_RPC_INVALID;
    return call(s->resource->db,s->resource->token,"DbPublish",proposal,a,out);
}

typedef struct Selection {atomic_size_t references;unsigned char *bytes;size_t size;} Selection;
typedef enum {F_PENDING,F_POSITION,F_PAUSED,F_ROW,F_END,F_CAPACITY,F_FAILURE} FutureStatus;
typedef struct Future {
    atomic_size_t references;pthread_mutex_t gate;
    CettaRemoteCapture *capture;Selection *selection;Resource *before,*position;
    CettaValueRpc *rpc;uint64_t sequence,body_size,identity_size;size_t received[2];
    DiscardedRpc *retirement;
    unsigned char *body,*identity;
    unsigned stage;bool opening;
    FutureStatus status;
} Future;
typedef struct {
    CettaRemoteCapture *capture;Selection *selection;Resource *position;Future *future;
    Arena row;ArenaMark empty;Atom *atom,*identity;
    uint64_t sequence,seen;CettaIndex last;
    bool current,done;SpaceOccurrenceCursorStep terminal;
} Cursor;
static void selection_retain(Selection *s){if(s)atomic_fetch_add(&s->references,1);}
static void selection_release(Selection *s){if(s && atomic_fetch_sub(&s->references,1)==1){free(s->bytes);free(s);}}
static Selection *selection_new(Atom *pattern,size_t maximum) {
    if(!pattern)return NULL;
    Selection *s=calloc(1,sizeof(*s));if(!s)return NULL;
    atomic_init(&s->references,1);
    if(cetta_atom_wire_encode_bounded(pattern,maximum,&s->bytes,&s->size)!=ATOM_WIRE_OK){selection_release(s);return NULL;}return s;
}
static void future_retain(Future *f){if(f)atomic_fetch_add(&f->references,1);}
static void future_release(Future *f) {
    if(!f || atomic_fetch_sub(&f->references,1)!=1)return;
    if(f->rpc && !cetta_value_rpc_cancel_queued(f->rpc)) {
        /* A cancelled reader must not tear down another captured reader's
         * stream. Retain its exact exchange for draining, including any
         * newly returned cursor token. The descriptor predates submission;
         * destruction performs neither allocation nor IO. */
        CettaRemoteDb *d=f->capture->resource->db;
        f->retirement->rpc=f->rpc;pthread_mutex_lock(&d->gate);
        f->retirement->next=d->discarded;d->discarded=f->retirement;
        pthread_mutex_unlock(&d->gate);f->retirement=NULL;
    } else cetta_value_rpc_release(f->rpc);
    free(f->retirement);resource_release(f->before);resource_release(f->position);selection_release(f->selection);
    cetta_remote_capture_release(f->capture);free(f->body);free(f->identity);pthread_mutex_destroy(&f->gate);free(f);
}
static Future *future_new(Cursor *c) {
    Future *f=calloc(1,sizeof(*f));if(!f)return NULL;
    f->retirement=calloc(1,sizeof(*f->retirement));
    if(!f->retirement || pthread_mutex_init(&f->gate,NULL)){free(f->retirement);free(f);return NULL;}
    atomic_init(&f->references,1);f->capture=c->capture;cetta_remote_capture_retain(f->capture);
    f->selection=c->selection;selection_retain(f->selection);f->before=c->position;resource_retain(f->before);
    f->opening=!c->position;f->sequence=c->sequence;return f;
}
static void future_fail(Future *f,bool capacity) {f->status=capacity ? F_CAPACITY : F_FAILURE;}
static CettaValueRpcStatus future_request(Future *f) {
    CettaRemoteDb *d=f->capture->resource->db;Arena a;arena_init_detached(&a);Atom *argument=NULL;
    const char *operation,*token;
    CettaValueRpcStatus status=CETTA_RPC_INVALID;
    if(f->opening) {
        token=f->capture->resource->token;operation=f->selection ? "DbCandidates" : "DbScan";
        Atom *pattern=NULL;
        if(f->selection && cetta_atom_wire_decode_bounded(&a,f->selection->bytes,f->selection->size,f->capture->maximum,&pattern)!=ATOM_WIRE_OK)goto done;
        argument=pattern ? atom_expr2(&a,atom_symbol(&a,"DbCandidatesV1"),pattern) : atom_expr(&a,NULL,0);
    } else if(!f->stage) {
        token=f->before->token;operation="DbCandidateStep";
        if(f->sequence>INT64_MAX)goto done;
        argument=atom_expr3(&a,atom_symbol(&a,"DbCandidateNextV1"),atom_int(&a,(int64_t)f->sequence),atom_int(&a,1));
    } else {
        unsigned part=f->stage==1 ? 0 : 1;uint64_t length=part ? f->identity_size : f->body_size;
        size_t amount=cetta_value_channel_limit(d->channel)/4;if(amount>65536)amount=65536;
        if(amount>length-f->received[part])amount=(size_t)(length-f->received[part]);
        if(!amount || f->received[part]>INT64_MAX || f->sequence>INT64_MAX)goto done;
        token=f->position->token;operation="DbCandidateRead";
        argument=atom_expr(&a,(Atom *[]){atom_symbol(&a,"DbCandidateReadV1"),atom_int(&a,(int64_t)f->sequence),
            atom_symbol(&a,part ? "identity" : "body"),atom_int(&a,(int64_t)f->received[part]),atom_int(&a,(int64_t)amount)},5);
    }
    status=request(d,token,operation,argument,&f->rpc);
done:
    arena_free(&a);return status;
}
static void future_step(Future *f) {
    CETTA_SCOPED_SHARED_TRANSITION(receive);
    pthread_mutex_lock(&f->gate);
    if(f->status!=F_PENDING)goto done;
    if(!f->rpc) {
        CettaValueRpcStatus status=future_request(f);
        if(status!=CETTA_RPC_PENDING){future_fail(f,status==CETTA_RPC_CAPACITY);goto done;}
    }
    CettaValueRpcStatus status=cetta_value_rpc_step(f->rpc,65536);
    if(status==CETTA_RPC_PENDING)goto done;
    if(status!=CETTA_RPC_OK){future_fail(f,status==CETTA_RPC_CAPACITY);goto done;}
    Atom *answer=cetta_value_rpc_result(f->rpc);CettaRemoteDb *d=f->capture->resource->db;
    if(!f->stage) {
        if(tagged(answer,"DbCandidateStoppedV1",2)) {
            if(atom_is_symbol(answer->expr.elems[1],"StoreCandidateEnd"))f->status=F_END;else future_fail(f,false);
            goto finished;
        }
        Atom *meta=tagged(answer,"ServiceResourceV1",3) ? answer->expr.elems[2] : NULL;
        f->position=meta ? resource_new(d,answer->expr.elems[1]) : NULL;
        if(!f->position){future_fail(f,meta!=NULL);goto finished;}
        if(f->opening) {
            uint64_t sequence;
            if(!tagged(meta,"DbCandidateOpenedV1",2) || !natural(meta->expr.elems[1],&sequence) || sequence!=0)future_fail(f,false);
            else f->status=F_POSITION;
            goto finished;
        }
        if(!tagged(meta,"DbCandidateSteppedV1",2)){future_fail(f,false);goto finished;}
        Atom *row=meta->expr.elems[1];uint64_t sequence;
        if(atom_is_symbol(row,"StoreCandidatePaused")){f->status=F_PAUSED;goto finished;}
        if(!tagged(row,"StoreCandidateRow",4) || !natural(row->expr.elems[1],&sequence) || sequence!=f->sequence ||
            !natural(row->expr.elems[2],&f->body_size) || !natural(row->expr.elems[3],&f->identity_size) ||
            !f->body_size || !f->identity_size || f->body_size>f->capture->maximum || f->identity_size>f->capture->maximum) {future_fail(f,false);goto finished;}
        f->body=malloc((size_t)f->body_size);f->identity=malloc((size_t)f->identity_size);
        if(!f->body || !f->identity){future_fail(f,true);goto finished;}
        f->stage=1;
    } else {
        unsigned part=f->stage==1 ? 0 : 1;uint64_t sequence,offset;size_t length;
        if(!tagged(answer,"StoreCandidateChunk",5) || !natural(answer->expr.elems[1],&sequence) || sequence!=f->sequence ||
            !atom_is_symbol(answer->expr.elems[2],part ? "identity" : "body") || !natural(answer->expr.elems[3],&offset) ||
            offset!=f->received[part] || answer->expr.elems[4]->kind!=ATOM_GROUNDED || answer->expr.elems[4]->ground.gkind!=GV_STRING) {future_fail(f,false);goto finished;}
        length=atom_string_len(answer->expr.elems[4]);uint64_t expected=part ? f->identity_size : f->body_size;
        if(!length || length>expected-f->received[part]){future_fail(f,false);goto finished;}
        memcpy((part ? f->identity : f->body)+f->received[part],answer->expr.elems[4]->ground.sval,length);f->received[part]+=length;
        if(f->received[part]==expected){if(part)f->status=F_ROW;else f->stage=2;}
    }
finished:
    cetta_value_rpc_release(f->rpc);f->rpc=NULL;
    if(f->status!=F_PENDING){resource_release(f->before);f->before=NULL;}
done:
    pthread_mutex_unlock(&f->gate);
}
static void cursor_free(void *raw) {
    Cursor *c=raw;if(!c)return;
    future_release(c->future);resource_release(c->position);selection_release(c->selection);
    arena_free(&c->row);cetta_remote_capture_release(c->capture);free(c);
}
static Cursor *cursor_new(CettaRemoteCapture *s,Selection *selection) {
    Cursor *c=calloc(1,sizeof(*c));if(!c)return NULL;
    c->capture=s;cetta_remote_capture_retain(s);c->selection=selection;selection_retain(selection);
    arena_init_detached(&c->row);c->empty=arena_mark(&c->row);return c;
}
static bool source_open(Space *space,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    *out=NULL;CettaRemoteCapture *s=space->match_backend.source;
    Selection *selection=selection_new(pattern,s->maximum);
    if(pattern && !selection){*failure=SPACE_OCCURRENCE_CURSOR_CAPACITY;return false;}
    Cursor *c=cursor_new(s,selection);selection_release(selection);
    if(!c){*failure=SPACE_OCCURRENCE_CURSOR_CAPACITY;return false;}*out=c;return true;
}
static bool cursor_clone(const void *raw,bool rewind,void **out) {
    const Cursor *p=raw;*out=NULL;Cursor *c=cursor_new(p->capture,p->selection);if(!c)return false;
    if(!rewind) {
        c->position=p->position;resource_retain(c->position);c->future=p->future;future_retain(c->future);
        c->sequence=p->sequence;c->seen=p->seen;c->last=p->last;c->current=p->current;c->done=p->done;c->terminal=p->terminal;
        if(p->atom && !(c->atom=atom_deep_copy(&c->row,p->atom)))goto failed;
        if(p->identity && !(c->identity=atom_deep_copy(&c->row,p->identity)))goto failed;
    }
    *out=c;return true;
failed:
    cursor_free(c);return false;
}
static bool cursor_select(const void *raw,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    const Cursor *c=raw;Space s={0};s.match_backend.source=c->capture;return source_open(&s,pattern,out,failure);
}
static SpaceOccurrenceCursorStep stop_cursor(Cursor *c,SpaceOccurrenceCursorStep status) {c->done=true;c->terminal=status;return status;}
static SpaceOccurrenceCursorStep cursor_next(void *raw,CettaIndex *index) {
    Cursor *c=raw;c->current=false;c->atom=c->identity=NULL;arena_reset(&c->row,c->empty);
    if(c->done)return c->terminal;
    if(!c->future && !(c->future=future_new(c)))return stop_cursor(c,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    future_step(c->future);Future *f=c->future;
    pthread_mutex_lock(&f->gate);FutureStatus status=f->status;
    if(status==F_PENDING){pthread_mutex_unlock(&f->gate);return SPACE_OCCURRENCE_CURSOR_PROGRESS;}
    if(status==F_CAPACITY || status==F_FAILURE){pthread_mutex_unlock(&f->gate);return stop_cursor(c,status==F_CAPACITY ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);}
    if(status==F_END) {
        pthread_mutex_unlock(&f->gate);
        return stop_cursor(c,!c->selection && c->seen!=c->capture->count ? SPACE_OCCURRENCE_CURSOR_INCOMPLETE : SPACE_OCCURRENCE_CURSOR_END);
    }
    if(status==F_ROW) {
        if(c->sequence==UINT64_MAX || c->seen==UINT64_MAX ||
            cetta_atom_wire_decode_bounded(&c->row,f->body,(size_t)f->body_size,c->capture->maximum,&c->atom)!=ATOM_WIRE_OK ||
            cetta_atom_wire_decode_bounded(&c->row,f->identity,(size_t)f->identity_size,c->capture->maximum,&c->identity)!=ATOM_WIRE_OK || atom_has_vars(c->identity)) {
            pthread_mutex_unlock(&f->gate);return stop_cursor(c,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
        }
        c->last=(CettaIndex)c->seen++;++c->sequence;c->current=true;*index=c->last;
    }
    resource_release(c->position);c->position=f->position;resource_retain(c->position);
    pthread_mutex_unlock(&f->gate);c->future=NULL;future_release(f);
    return status==F_ROW ? SPACE_OCCURRENCE_CURSOR_ITEM : SPACE_OCCURRENCE_CURSOR_PROGRESS;
}
static Atom *cursor_atom(const void *raw,CettaIndex index){const Cursor *c=raw;return c->current && c->last==index ? c->atom : NULL;}
static Atom *cursor_identity(const void *raw,CettaIndex index){const Cursor *c=raw;return c->current && c->last==index ? c->identity : NULL;}
static size_t cursor_limit(const void *raw){return ((const Cursor *)raw)->capture->maximum;}
static void future_wait(void *raw) {
    Future *f=raw;pthread_mutex_lock(&f->gate);CettaValueRpc *rpc=f->rpc;
    cetta_value_rpc_retain(rpc);pthread_mutex_unlock(&f->gate);
    if(rpc)cetta_value_rpc_await(rpc);
    cetta_value_rpc_release(rpc);
}
static void future_wait_release(void *raw){future_release(raw);}
static void future_storage(const void *,CettaStorageCensus *);
static bool cursor_pending_wait(const void *raw,SpaceSourceWait *out) {
    const Cursor *c=raw;*out=(SpaceSourceWait){0};
    /* Completing an open/paused position yields before starting its next
     * exchange. That is finite local progress, with nothing to wait on yet. */
    if(!c->future)return true;
    future_retain(c->future);
    *out=(SpaceSourceWait){c->future,future_wait,future_wait_release,future_storage};return true;
}
static void cursor_storage(const void *raw,CettaStorageCensus *census);
static const SpaceSourceCursorOps cursor_ops={.clone=cursor_clone,.select=cursor_select,.next=cursor_next,.atom=cursor_atom,
    .identity=cursor_identity,.value_byte_limit=cursor_limit,.pending_wait=cursor_pending_wait,.free=cursor_free,.storage=cursor_storage};
static uint64_t source_count(const Space *space){return ((const CettaRemoteCapture *)space->match_backend.source)->count;}
static void source_free(Space *space){cetta_remote_capture_release(space->match_backend.source);space->match_backend.source=NULL;}
static void source_storage(const Space *space,CettaStorageCensus *census){cetta_remote_capture_storage(space->match_backend.source,census);}
static bool source_overlay(Space *target,const Space *base);
static const SpaceMatchBackendOps source_ops={.name="remote-captured-space",.occurrence_ops=&cursor_ops,.occurrence_open=source_open,
    .logical_len=source_count,.free=source_free,.storage=source_storage,.overlay_local=source_overlay};
CettaRemoteCapture *cetta_remote_capture_from_space(const Space *space) {
    if(!space || !space_owner_is_current(space) || space->match_backend.ops!=&source_ops)return NULL;
    CettaRemoteCapture *capture=space->match_backend.source;cetta_remote_capture_retain(capture);return capture;
}
bool cetta_remote_capture_space(CettaRemoteCapture *s,Space *target) {
    if(!s || !target || target->match_backend.kind!=SPACE_ENGINE_NATIVE || space_length64(target) || target->overlay_base || space_has_dependencies(target))return false;
    cetta_remote_capture_retain(s);target->match_backend.kind=SPACE_ENGINE_SOURCE;target->match_backend.ops=&source_ops;target->match_backend.source=s;space_read_view_invalidate(target);return true;
}
static bool source_overlay(Space *target,const Space *base) {
    CettaRemoteCapture *s=base->match_backend.source;Space *copy=malloc(sizeof(*copy));if(!copy)return false;
    space_init_with_universe(copy,target->native.universe);
    if(cetta_remote_capture_space(s,copy) && space_source_overlay_init_owned(target,copy,s->maximum))return true;
    space_free(copy);free(copy);return false;
}
static void resource_storage(const Resource *r,CettaStorageCensus *census) {
    if(r && cetta_storage_object(census,r,sizeof(*r),CETTA_STORAGE_BLOCK)) {
        if(r->token_reply)cetta_value_rpc_storage(r->token_reply,census);
        else if(r->token)cetta_storage_array(census,r->token,strlen(r->token)+1,1,0);
    }
}
void cetta_remote_resource_storage(const void *raw,CettaStorageCensus *census) {
    const Resource *r=raw;if(!r)return;
    resource_storage(r,census);cetta_remote_db_storage(r->db,census);
}
void cetta_remote_db_storage(const void *raw,CettaStorageCensus *census) {
    const CettaRemoteDb *d=raw;if(!d || !cetta_storage_object(census,d,sizeof(*d),CETTA_STORAGE_BLOCK))return;
    pthread_mutex_lock(&((CettaRemoteDb *)d)->gate);
    arena_storage_census(&d->metadata,census);cetta_value_channel_storage(d->channel,census);
    resource_storage(d->dropping,census);cetta_value_rpc_storage(d->cleanup,census);
    for(Resource *r=d->retired;r;r=r->next)resource_storage(r,census);
    for(DiscardedRpc *r=d->discarded;r;r=r->next) {
        cetta_storage_object(census,r,sizeof(*r),CETTA_STORAGE_BLOCK);
        cetta_value_rpc_storage(r->rpc,census);
    }
    pthread_mutex_unlock(&((CettaRemoteDb *)d)->gate);
}
void cetta_remote_capture_storage(const void *raw,CettaStorageCensus *census) {
    const CettaRemoteCapture *s=raw;if(!s || !cetta_storage_object(census,s,sizeof(*s),CETTA_STORAGE_BLOCK))return;
    resource_storage(s->resource,census);cetta_remote_db_storage(s->resource->db,census);arena_storage_census(&s->metadata,census);
}
static void future_storage(const void *raw,CettaStorageCensus *census) {
    const Future *f=raw;
    if(!f || !cetta_storage_object(census,f,sizeof(*f),CETTA_STORAGE_BLOCK))return;
    pthread_mutex_lock(&((Future *)f)->gate);
    cetta_remote_capture_storage(f->capture,census);resource_storage(f->before,census);resource_storage(f->position,census);
    cetta_value_rpc_storage(f->rpc,census);
    if(f->retirement)cetta_storage_object(census,f->retirement,sizeof(*f->retirement),CETTA_STORAGE_BLOCK);
    if(f->body)cetta_storage_array(census,f->body,(size_t)f->body_size,1,0);
    if(f->identity)cetta_storage_array(census,f->identity,(size_t)f->identity_size,1,0);
    pthread_mutex_unlock(&((Future *)f)->gate);
}
static void cursor_storage(const void *raw,CettaStorageCensus *census) {
    const Cursor *c=raw;if(!c || !cetta_storage_object(census,c,sizeof(*c),CETTA_STORAGE_BLOCK))return;
    cetta_remote_capture_storage(c->capture,census);resource_storage(c->position,census);future_storage(c->future,census);arena_storage_census(&c->row,census);
    if(c->selection && cetta_storage_object(census,c->selection,sizeof(*c->selection),CETTA_STORAGE_BLOCK))cetta_storage_array(census,c->selection->bytes,c->selection->size,1,0);
}
