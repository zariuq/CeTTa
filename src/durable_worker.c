#define _GNU_SOURCE
#include "durable_worker.h"
#include "durable_worker_host.h"
#include "durable_value.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { PEERS=16, TASKS=128 };
/* A ready index contains no observation body; enumerating pending work must
 * not copy every large observation on each fetch. CDV1 true is the marker. */
static const unsigned char ready_marker[]={'C','D','V','1','Y'};
typedef struct { int fd; unsigned char *reply; size_t size; uint64_t touched; } Peer;
struct CettaWorkerEndpoint {
    CettaDurableStore *store;
    int listener;
    uid_t uid;
    char worker[65], prefix[66];
    Peer peers[PEERS];
    unsigned next;
};
static bool component(const char *s) {
    size_t n=s?strnlen(s,65):0;
    if (!n || n>64) return false;
    for (size_t i=0;i<n;++i) if (!((s[i]>='A' && s[i]<='Z') || (s[i]>='a' && s[i]<='z') ||
        (s[i]>='0' && s[i]<='9') || s[i]=='_' || s[i]=='-' || s[i]=='.')) return false;
    return true;
}
static bool text_valid(const unsigned char *s, size_t size) {
    if (!s || !size || size>CETTA_WORKER_BODY_MAX) return false;
    for (size_t i=0;i<size;) {
        unsigned c=s[i++], count, min;
        if (c<128) { if (!c) return false; continue; }
        if (c>=0xc2 && c<=0xdf) { count=1; min=0x80; c&=0x1f; }
        else if (c>=0xe0 && c<=0xef) { count=2; min=0x800; c&=0xf; }
        else if (c>=0xf0 && c<=0xf4) { count=3; min=0x10000; c&=7; }
        else return false;
        if (count>size-i) return false;
        while (count--) { unsigned d=s[i++]; if ((d&0xc0)!=0x80) return false; c=(c<<6)|(d&0x3f); }
        if (c<min || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return false;
    }
    return true;
}
static void key(char *out, const char *worker, const char *id) {
    size_t n=strlen(worker); memcpy(out,worker,n); out[n++]='/'; strcpy(out+n,id);
}
static const CettaDurableSnapshot *view(const CettaDurableObservation *o, size_t i) {
    return cetta_durable_observation_view(o,i);
}
static const CettaDurableRecord *find(const CettaDurableSnapshot *v, const char *name) {
    for (size_t i=0;i<v->count;++i) if (!strcmp(v->records[i].key,name)) return &v->records[i];
    return NULL;
}
static CettaDurableStatus encoded_text(const void *body, size_t size, unsigned char **data, size_t *n) {
    CettaDurableField f={.kind=DURABLE_FIELD_TEXT,.text={body,size}};
    return cetta_durable_fields_encode(&f,data,n);
}
/* Tasks are CDV1 strings, not programs. Read the fixed string envelope without
 * entering the evaluator or interning any external name. */
static bool task_body(const CettaDurableRecord *r, const unsigned char **body, size_t *size) {
    if (r->size<9 || memcmp(r->data,"CDV1T",5)) return false;
    uint32_t n=0; for (unsigned i=0;i<4;++i) n|=(uint32_t)r->data[5+i]<<(8*i);
    if (n!=r->size-9 || !text_valid(r->data+9,n)) return false;
    *body=r->data+9; *size=n; return true;
}
static CettaDurableStatus publish(CettaDurableStore *store, const char *worker,
        const char *id, const void *body, size_t size,
        const CettaDurableRecord *intent, const char *epoch) {
    if (!store || !component(worker) || !component(id) || !text_valid(body,size)) return DURABLE_INVALID;
    char name[130], prefix[66]; key(name,worker,id); key(prefix,worker,"");
    CettaDurableScope q[]={{DURABLE_KEY,"host.worker-tasks",name},
        {DURABLE_KEY,"host.worker-results",name},{DURABLE_PREFIX,"host.worker-ready",prefix},
        {DURABLE_KEY,"host.outbox",intent?intent->key:""},
        {DURABLE_KEY,"host.worker-origins",name}};
    CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,q,intent?5:3,&o); if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *v=view(o,0), *ready=view(o,2);
    const CettaDurableRecord *old=v->count?&v->records[0]:NULL;
    unsigned char *data=NULL, *origin=NULL; size_t n=0, on=0;
    if (intent) {
        const CettaDurableSnapshot *seen=view(o,3);
        const CettaDurableRecord *r=seen->count?&seen->records[0]:NULL;
        if (strcmp(seen->epoch,epoch) || !r || r->revision!=intent->revision ||
            r->position!=intent->position || r->size!=intent->size ||
            memcmp(r->data,intent->data,intent->size)) { s=DURABLE_CONFLICT; goto done; }
        CettaDurableField f[]={
            {.kind=DURABLE_FIELD_SYMBOL,.text={"host:worker-origin",18}},
            {.kind=DURABLE_FIELD_INT,.integer=1},
            {.kind=DURABLE_FIELD_TEXT,.text={epoch,strlen(epoch)}},
            {.kind=DURABLE_FIELD_TEXT,.text={intent->key,strlen(intent->key)}},
            {.kind=DURABLE_FIELD_INT,.integer=intent->revision},
            {.kind=DURABLE_FIELD_INT,.integer=intent->position}};
        CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={f,6}};
        s=cetta_durable_fields_encode(&value,&origin,&on); if (s!=DURABLE_OK) goto done;
        const CettaDurableSnapshot *prior=view(o,4);
        if ((old && (!prior->count || prior->records[0].size!=on || memcmp(prior->records[0].data,origin,on))) ||
            (!old && prior->count)) { s=DURABLE_CONFLICT; goto done; }
    }
    s=encoded_text(body,size,&data,&n);
    if (s==DURABLE_OK && old) s=old->size==n && !memcmp(old->data,data,n)?DURABLE_OK:DURABLE_CONFLICT;
    else if (s==DURABLE_OK && (view(o,1)->count || find(ready,name))) s=DURABLE_CONFLICT;
    else if (s==DURABLE_OK && ready->count>=TASKS) s=DURABLE_LIMIT;
    else if (s==DURABLE_OK) {
        CettaDurableOp ops[]={{DURABLE_INSERT,"host.worker-tasks",name,data,n},
            {DURABLE_INSERT,"host.worker-ready",name,ready_marker,sizeof(ready_marker)},
            {DURABLE_INSERT,"host.worker-origins",name,origin,on}}; int64_t rev;
        s=cetta_durable_commit_observed(store,o,ops,intent?3:2,&rev);
    }
done:
    free(origin); free(data); cetta_durable_observation_free(o); return s;
}
CettaDurableStatus cetta_worker_publish(CettaDurableStore *store, const char *worker,
                                      const char *id, const void *body, size_t size) {
    return publish(store,worker,id,body,size,NULL,NULL);
}
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool version(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_INT && a->ground.ival==1;
}
bool cetta_worker_validate(void *context, const Atom *p, const Atom *reply_value) {
    const char *worker=context, *who, *body;
    if (!component(worker) || !form(p,"worker:request",4) || !version(p->expr.elems[1]) ||
        !(who=string(p->expr.elems[2])) || strcmp(who,worker) || !(body=string(p->expr.elems[3])) ||
        !text_valid((const unsigned char *)body,strnlen(body,CETTA_WORKER_BODY_MAX+1))) return false;
    unsigned char *bytes=NULL; size_t n=0;
    CettaDurableStatus s=cetta_durable_value_encode(reply_value,&bytes,&n); free(bytes);
    return s==DURABLE_OK && n<=CETTA_WORKER_BODY_MAX;
}
CettaDurableStatus cetta_worker_register(CettaDurableStore *store,
        const char *outbox_key, const char *worker, char task_id[65]) {
    if (!task_id) return DURABLE_INVALID;
    task_id[0]=0;
    if (!store || !component(worker) || !outbox_key || !*outbox_key || strnlen(outbox_key,256)>255)
        return DURABLE_INVALID;
    CettaDurableScope q={DURABLE_KEY,"host.outbox",outbox_key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,&q,1,&o); if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *v=view(o,0); Arena a; arena_init(&a); Atom *p=NULL;
    if (!v->count) { s=DURABLE_PRECONDITION; goto done; }
    const CettaDurableRecord *r=&v->records[0];
    s=cetta_durable_value_decode(&a,r->data,r->size,&p); if (s!=DURABLE_OK) goto done;
    if (!form(p,"host:intent",6) || !version(p->expr.elems[1]) || r->revision<0 || r->position<0) {
        s=DURABLE_CORRUPT; goto done;
    }
    if (!string(p->expr.elems[2]) || strcmp(string(p->expr.elems[2]),"worker.request") ||
        !string(p->expr.elems[3]) || strcmp(string(p->expr.elems[3]),"1")) { s=DURABLE_VERSION; goto done; }
    if (!cetta_worker_validate((void *)worker,p->expr.elems[4],p->expr.elems[5])) { s=DURABLE_INVALID; goto done; }
    char id[65];
    snprintf(id,sizeof(id),"%.32s%016" PRIx64 "%016" PRIx64,v->epoch,(uint64_t)r->revision,(uint64_t)r->position);
    const char *body=string(p->expr.elems[4]->expr.elems[3]);
    s=publish(store,worker,id,body,strlen(body),r,v->epoch);
    if (s==DURABLE_OK) memcpy(task_id,id,sizeof(id));
done:
    arena_free(&a); cetta_durable_observation_free(o); return s;
}
static CettaWorkerCode status_code(CettaDurableStatus s) {
    switch (s) {
    case DURABLE_OK: return WORKER_STORED;
    case DURABLE_CONFLICT: return WORKER_CONFLICT;
    case DURABLE_LIMIT: return WORKER_LIMIT;
    case DURABLE_INVALID: return WORKER_INVALID;
    default: return WORKER_UNAVAILABLE;
    }
}
static CettaDurableStatus reply(Peer *p, CettaWorkerCode code, const char *id,
                                const void *body, size_t size) {
    size_t n=strlen(id); if (n>64 || size>CETTA_WORKER_BODY_MAX) return DURABLE_INVALID;
    unsigned char *b=malloc(6+n+size); if (!b) return DURABLE_NOMEM;
    memcpy(b,"CWP1",4); b[4]=(unsigned char)code; b[5]=(unsigned char)n;
    memcpy(b+6,id,n); if (size) memcpy(b+6+n,body,size);
    free(p->reply); p->reply=b; p->size=6+n+size; return DURABLE_OK;
}
static CettaDurableStatus next_task(CettaWorkerEndpoint *e, Peer *p, const char *after) {
    CettaDurableScope q={DURABLE_PREFIX,"host.worker-ready",e->prefix};
    CettaDurableObservation *o=NULL; CettaDurableStatus s=cetta_durable_observe(e->store,&q,1,&o);
    const CettaDurableRecord *next=NULL;
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *tasks=view(o,0);
        const CettaDurableRecord *cut=NULL;
        if (after) {
            char name[130]; key(name,e->worker,after); cut=find(tasks,name);
            /* Answered meanwhile: the batch it began is stale. */
            if (!cut) { cetta_durable_observation_free(o); return reply(p,WORKER_UNKNOWN,after,NULL,0); }
        }
        if (tasks->count>TASKS) s=DURABLE_LIMIT;
        else for (size_t i=0;i<tasks->count;++i) {
            const CettaDurableRecord *r=&tasks->records[i];
            if (cut && (r->revision<cut->revision ||
                (r->revision==cut->revision && r->position<=cut->position))) continue;
            if (!next || r->revision<next->revision ||
                (r->revision==next->revision && r->position<next->position)) next=r;
        }
    }
    CettaDurableObservation *selected=NULL;
    CettaDurableStatus sent;
    if (s==DURABLE_OK && next) {
        const unsigned char *body; size_t size;
        const char *id=next->key+strlen(e->prefix);
        CettaDurableScope keys[]={{DURABLE_KEY,"host.worker-tasks",next->key},
            {DURABLE_KEY,"host.worker-ready",next->key}};
        if (!component(id)) s=DURABLE_CORRUPT;
        else s=cetta_durable_observe(e->store,keys,2,&selected);
        if (s==DURABLE_OK && !view(selected,1)->count) {
            /* Completed between enumeration and this snapshot. The next
             * fetch sees the new queue; no task has been consumed here. */
            sent=reply(p,WORKER_IDLE,"",NULL,0);
        } else {
            if (s==DURABLE_OK) {
                const CettaDurableSnapshot *task=view(selected,0), *ready=view(selected,1);
                if (!task->count || ready->records[0].size!=sizeof(ready_marker) ||
                    memcmp(ready->records[0].data,ready_marker,sizeof(ready_marker)) ||
                    !task_body(&task->records[0],&body,&size)) s=DURABLE_CORRUPT;
            }
            sent=s==DURABLE_OK?reply(p,WORKER_TASK,id,body,size):reply(p,WORKER_UNAVAILABLE,"",NULL,0);
        }
    } else sent=reply(p,s==DURABLE_OK?WORKER_IDLE:status_code(s),"",NULL,0);
    cetta_durable_observation_free(selected);
    cetta_durable_observation_free(o); return s==DURABLE_OK?sent:s;
}
static CettaDurableStatus result(CettaWorkerEndpoint *e, Peer *p, const char *id,
                                const unsigned char *body, size_t size, bool receipt) {
    char name[130], input[138]; key(name,e->worker,id);
    memcpy(input,"worker/",7); strcpy(input+7,name);
    CettaDurableScope q[]={{DURABLE_KEY,"host.worker-tasks",name},
        {DURABLE_KEY,"host.worker-results",name},{DURABLE_KEY,"host.inbox",input},
        {DURABLE_KEY,"host.worker-ready",name}};
    CettaDurableObservation *o=NULL; CettaDurableStatus s=cetta_durable_observe(e->store,q,4,&o);
    CettaWorkerCode code=WORKER_UNAVAILABLE; unsigned char *data=NULL; size_t n=0;
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *task=view(o,0), *old=view(o,1), *inbox=view(o,2), *ready=view(o,3);
        if (receipt) code=old->count?WORKER_STORED:task->count?WORKER_PENDING:WORKER_UNKNOWN;
        else if (!task->count) code=WORKER_UNKNOWN;
        else {
            const unsigned char *ignored; size_t ignored_size;
            if (!task_body(&task->records[0],&ignored,&ignored_size)) s=DURABLE_CORRUPT;
            CettaDurableField f[]={
                {.kind=DURABLE_FIELD_SYMBOL,.text={"host:worker-result",18}},
                {.kind=DURABLE_FIELD_INT,.integer=1},
                {.kind=DURABLE_FIELD_TEXT,.text={e->worker,strlen(e->worker)}},
                {.kind=DURABLE_FIELD_TEXT,.text={id,strlen(id)}},
                {.kind=DURABLE_FIELD_INT,.integer=task->records[0].revision},
                {.kind=DURABLE_FIELD_TEXT,.text={(const char *)body,size}}};
            CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={f,6}};
            if (s==DURABLE_OK) s=cetta_durable_fields_encode(&value,&data,&n);
            if (s==DURABLE_OK && old->count) {
                const CettaDurableRecord *r=&old->records[0];
                s=r->size==n && !memcmp(r->data,data,n)?DURABLE_OK:DURABLE_CONFLICT;
            } else if (s==DURABLE_OK) {
                if (inbox->count || !ready->count || ready->records[0].size!=sizeof(ready_marker) ||
                    memcmp(ready->records[0].data,ready_marker,sizeof(ready_marker))) s=DURABLE_CORRUPT;
                else {
                    CettaDurableOp ops[]={{DURABLE_INSERT,"host.worker-results",name,data,n},
                        {DURABLE_INSERT,"host.inbox",input,data,n},{DURABLE_REMOVE,"host.worker-ready",name,NULL,0}}; int64_t rev;
                    s=cetta_durable_commit_observed(e->store,o,ops,3,&rev);
                }
            }
            code=status_code(s);
        }
    }
    CettaDurableStatus sent=reply(p,code,id,NULL,0);
    free(data); cetta_durable_observation_free(o);
    return s==DURABLE_OK || s==DURABLE_CONFLICT || s==DURABLE_LIMIT?sent:s;
}
static CettaDurableStatus submit(CettaWorkerEndpoint *e, Peer *p, const char *id,
                                const unsigned char *body, size_t size) {
    char name[130], input[142], prefix[80]; key(name,e->worker,id);
    memcpy(input,"submission/",11); strcpy(input+11,name);
    snprintf(prefix,sizeof(prefix),"submission/%s/",e->worker);
    CettaDurableScope q[]={{DURABLE_KEY,"host.worker-submissions",name},
        {DURABLE_KEY,"host.inbox",input},{DURABLE_PREFIX,"host.inbox",prefix}};
    CettaDurableObservation *o=NULL; CettaDurableStatus s=cetta_durable_observe(e->store,q,3,&o);
    CettaWorkerCode code=WORKER_UNAVAILABLE; unsigned char *data=NULL; size_t n=0;
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *old=view(o,0), *inbox=view(o,1), *pending=view(o,2);
        CettaDurableField f[]={
            {.kind=DURABLE_FIELD_SYMBOL,.text={"host:worker-submission",22}},
            {.kind=DURABLE_FIELD_INT,.integer=1},
            {.kind=DURABLE_FIELD_TEXT,.text={e->worker,strlen(e->worker)}},
            {.kind=DURABLE_FIELD_TEXT,.text={id,strlen(id)}},
            {.kind=DURABLE_FIELD_TEXT,.text={(const char *)body,size}}};
        CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={f,5}};
        s=cetta_durable_fields_encode(&value,&data,&n);
        if (s==DURABLE_OK && old->count) {
            const CettaDurableRecord *r=&old->records[0];
            s=r->size==n && !memcmp(r->data,data,n)?DURABLE_OK:DURABLE_CONFLICT;
        } else if (s==DURABLE_OK) {
            if (inbox->count) s=DURABLE_CORRUPT;
            else if (pending->count>=TASKS) s=DURABLE_LIMIT;
            else {
                CettaDurableOp ops[]={{DURABLE_INSERT,"host.worker-submissions",name,data,n},
                    {DURABLE_INSERT,"host.inbox",input,data,n}}; int64_t rev;
                s=cetta_durable_commit_observed(e->store,o,ops,2,&rev);
            }
        }
        code=status_code(s);
    }
    CettaDurableStatus sent=reply(p,code,id,NULL,0);
    free(data); cetta_durable_observation_free(o);
    return s==DURABLE_OK || s==DURABLE_CONFLICT || s==DURABLE_LIMIT?sent:s;
}
static CettaDurableStatus request(CettaWorkerEndpoint *e, Peer *p, const unsigned char *packet, size_t size) {
    if (size<6 || size>CETTA_WORKER_PACKET_MAX || memcmp(packet,"CWP1",4) || packet[5]>64 || 6u+packet[5]>size)
        return reply(p,WORKER_INVALID,"",NULL,0);
    char id[65]; size_t n=packet[5]; memcpy(id,packet+6,n); id[n]=0;
    const unsigned char *body=packet+6+n; size_t len=size-6-n;
    if (packet[4]==WORKER_NEXT && !n && !len) return next_task(e,p,NULL);
    if (!n || memchr(id,0,n) || !component(id)) return reply(p,WORKER_INVALID,"",NULL,0);
    if (packet[4]==WORKER_NEXT && !len) return next_task(e,p,id);
    if (packet[4]==WORKER_SUBMIT && text_valid(body,len)) return submit(e,p,id,body,len);
    if (packet[4]==WORKER_RECEIPT && !len) return result(e,p,id,NULL,0,true);
    if (packet[4]==WORKER_RESULT && text_valid(body,len)) return result(e,p,id,body,len,false);
    return reply(p,WORKER_INVALID,"",NULL,0);
}
static uint64_t monotonic_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void close_peer(Peer *p) {
    if (p->fd>=0) close(p->fd);
    free(p->reply); *p=(Peer){.fd=-1};
}
CettaDurableStatus cetta_worker_endpoint_new(CettaDurableStore *store, int listener,
        uid_t uid, const char *worker, CettaWorkerEndpoint **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    int type=0, accept=0; socklen_t len=sizeof(type);
    struct sockaddr_storage address; socklen_t address_size=sizeof(address);
    if (!store || listener<0 || !component(worker) ||
        getsockopt(listener,SOL_SOCKET,SO_TYPE,&type,&len) || type!=SOCK_SEQPACKET ||
        getsockopt(listener,SOL_SOCKET,SO_ACCEPTCONN,&accept,&len) || !accept ||
        getsockname(listener,(struct sockaddr *)&address,&address_size) || address.ss_family!=AF_UNIX) return DURABLE_INVALID;
    CettaWorkerEndpoint *e=calloc(1,sizeof(*e)); if (!e) return DURABLE_NOMEM;
    int flags=fcntl(listener,F_GETFL);
    if (flags<0 || fcntl(listener,F_SETFL,flags|O_NONBLOCK)<0 || fcntl(listener,F_SETFD,FD_CLOEXEC)<0) {
        free(e); return DURABLE_IO;
    }
    e->store=store; e->listener=listener; e->uid=uid; strcpy(e->worker,worker); key(e->prefix,worker,"");
    for (unsigned i=0;i<PEERS;++i) e->peers[i].fd=-1;
    *out=e; return DURABLE_OK;
}
void cetta_worker_endpoint_free(CettaWorkerEndpoint *e) {
    if (!e) return;
    for (unsigned i=0;i<PEERS;++i) close_peer(&e->peers[i]);
    close(e->listener); free(e);
}
CettaDurableStatus cetta_worker_endpoint_step(CettaWorkerEndpoint *e, unsigned timeout, size_t *requests) {
    if (!e || !requests || timeout>1000) return DURABLE_INVALID;
    *requests=0;
    struct pollfd fds[PEERS+1]={{e->listener,POLLIN,0}};
    uint64_t now=monotonic_ms(); if (!now) return DURABLE_IO;
    for (unsigned i=0;i<PEERS;++i) {
        Peer *p=&e->peers[i];
        if (p->fd>=0 && now-p->touched>=30000) close_peer(p);
        fds[i+1]=(struct pollfd){p->fd,p->reply?POLLOUT:POLLIN,0};
    }
    int ready=poll(fds,PEERS+1,(int)timeout);
    if (ready<0) return errno==EINTR?DURABLE_OK:DURABLE_IO;
    if (fds[0].revents&(POLLERR|POLLHUP|POLLNVAL)) return DURABLE_IO;
    if (fds[0].revents&POLLIN) {
        int fd=accept4(e->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if (fd>=0) {
            struct ucred peer; socklen_t n=sizeof(peer); unsigned slot=0;
            while (slot<PEERS && e->peers[slot].fd>=0) ++slot;
            if (slot==PEERS || getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n) || peer.uid!=e->uid) close(fd);
            else e->peers[slot]=(Peer){.fd=fd,.touched=now};
        } else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return DURABLE_IO;
    }
    CettaDurableStatus status=DURABLE_OK;
    unsigned char packet[CETTA_WORKER_PACKET_MAX];
    for (unsigned i=0;i<PEERS;++i) {
        unsigned index=(e->next+i)%PEERS; Peer *p=&e->peers[index];
        short revents=fds[index+1].revents;
        if (p->fd<0 || !revents) continue;
        if (revents&(POLLERR|POLLNVAL)) { close_peer(p); continue; }
        if (!p->reply && (revents&POLLIN)) {
            ssize_t n=recv(p->fd,packet,sizeof(packet),MSG_DONTWAIT|MSG_TRUNC);
            if (n<=0) { if (!n || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) close_peer(p); continue; }
            ++*requests; p->touched=now;
            CettaDurableStatus s=request(e,p,packet,(size_t)n);
            if (s!=DURABLE_OK) status=s;
        }
        if (p->reply) {
            ssize_t n=send(p->fd,p->reply,p->size,MSG_DONTWAIT|MSG_NOSIGNAL);
            if (n==(ssize_t)p->size) { free(p->reply); p->reply=NULL; p->size=0; p->touched=now; }
            else if (n>=0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) close_peer(p);
        }
        if (p->fd>=0 && (revents&POLLHUP) && !p->reply) close_peer(p);
    }
    e->next=(e->next+1)%PEERS;
    return status;
}
