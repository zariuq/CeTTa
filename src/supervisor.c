#define _POSIX_C_SOURCE 200809L
#include "supervisor.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define TASK_SPACE "supervisor.tasks.v1"
#define HEADER_BYTES 48u
static const unsigned char task_magic[8] = {'C','S','U','P',1,0,0,0};
struct CettaSupervisor {
    CettaDurableStore *store;
    pthread_mutex_t mutex;
    char epoch[33];
    size_t references;
    size_t record_bytes;
    bool closed;
};
struct CettaDispatchPermit {
    CettaSupervisor *owner;
    char id[256];
    int64_t generation;
    unsigned char *payload;
    size_t bytes;
    bool claimed;
};
static bool valid_id(const char *id) {
    return id && *id && strnlen(id,256) < 256;
}
static void put64(unsigned char *p, uint64_t x) {
    for (unsigned i=0; i<8; ++i) p[i]=(unsigned char)(x>>(i*8));
}
static uint64_t get64(const unsigned char *p) {
    uint64_t x=0; for (unsigned i=0;i<8;++i) x|=(uint64_t)p[i]<<(i*8); return x;
}
void cetta_supervisor_task_free(CettaSupervisorTask *t) {
    if (!t) return;
    free(t->payload); free(t->result); memset(t,0,sizeof(*t));
}
static CettaDurableStatus unpack(const CettaDurableRecord *r, CettaSupervisorTask *t) {
    memset(t,0,sizeof(*t));
    const unsigned char *p=r->data;
    if (r->size<HEADER_BYTES || memcmp(p,task_magic,8) || p[24]>SUP_UNRESOLVED ||
        p[25]>1 || get64(p+8)>INT64_MAX || get64(p+16)>INT64_MAX ||
        memcmp(p+26,"\0\0\0\0\0\0",6)) return DURABLE_CORRUPT;
    uint64_t a=get64(p+32), b=get64(p+40);
    if (a>r->size-HEADER_BYTES || b!=r->size-HEADER_BYTES-a) return DURABLE_CORRUPT;
    t->remaining=(int64_t)get64(p+8); t->generation=(int64_t)get64(p+16);
    t->phase=p[24]; t->acknowledged=p[25];
    if ((t->acknowledged && t->phase<SUP_SUCCEEDED) ||
        (t->phase==SUP_READY && !t->remaining) ||
        (t->phase!=SUP_READY && !t->generation)) return DURABLE_CORRUPT;
    t->payload=malloc(a?a:1); t->result=malloc(b?b:1);
    if (!t->payload || !t->result) { cetta_supervisor_task_free(t); return DURABLE_NOMEM; }
    memcpy(t->payload,p+HEADER_BYTES,a); memcpy(t->result,p+HEADER_BYTES+a,b);
    t->payload_bytes=a; t->result_bytes=b; return DURABLE_OK;
}
CettaDurableStatus cetta_supervisor_task_decode(const CettaDurableRecord *r,CettaSupervisorTask *t) {
    if (!r || !t || !r->space || strcmp(r->space,TASK_SPACE)) return DURABLE_INVALID;
    return unpack(r,t);
}
static CettaDurableStatus save(CettaSupervisor *s, CettaDurableObservation *obs,
        const char *id, const CettaSupervisorTask *t, CettaDurableOpKind kind) {
    if (t->payload_bytes>SIZE_MAX-HEADER_BYTES ||
        t->result_bytes>SIZE_MAX-HEADER_BYTES-t->payload_bytes) return DURABLE_LIMIT;
    size_t bytes=HEADER_BYTES+t->payload_bytes+t->result_bytes;
    if (bytes>s->record_bytes) return DURABLE_LIMIT;
    unsigned char *p=calloc(1,bytes);
    if (!p) return DURABLE_NOMEM;
    memcpy(p,task_magic,8); put64(p+8,t->remaining); put64(p+16,t->generation);
    p[24]=t->phase; p[25]=t->acknowledged;
    put64(p+32,t->payload_bytes); put64(p+40,t->result_bytes);
    if (t->payload_bytes) memcpy(p+HEADER_BYTES,t->payload,t->payload_bytes);
    if (t->result_bytes) memcpy(p+HEADER_BYTES+t->payload_bytes,t->result,t->result_bytes);
    CettaDurableOp op={kind,TASK_SPACE,id,p,bytes};
    int64_t published=-1;
    CettaDurableStatus st=cetta_durable_commit_observed(s->store,obs,&op,1,&published);
    free(p); return st;
}
static CettaDurableStatus load(CettaSupervisor *s, const char *id,
        CettaDurableObservation **obs, CettaSupervisorTask *t) {
    memset(t,0,sizeof(*t)); *obs=NULL;
    if (s->closed || !valid_id(id)) return DURABLE_INVALID;
    CettaDurableScope scope={DURABLE_KEY,TASK_SPACE,id};
    CettaDurableStatus st=cetta_durable_observe(s->store,&scope,1,obs);
    if (st!=DURABLE_OK) return st;
    const CettaDurableSnapshot *view=cetta_durable_observation_view(*obs,0);
    if (view->count!=1) return DURABLE_PRECONDITION;
    return unpack(&view->records[0],t);
}
static void destroy(CettaSupervisor *s) { pthread_mutex_destroy(&s->mutex); free(s); }
void cetta_supervisor_close(CettaSupervisor *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    if (!s->closed) {
        s->closed=true;
        cetta_durable_detach_runtime(s->store,s);
        cetta_durable_close(s->store); s->store=NULL;
    }
    bool done=--s->references==0;
    pthread_mutex_unlock(&s->mutex);
    if (done) destroy(s);
}
const char *cetta_supervisor_epoch(const CettaSupervisor *s) { return s?s->epoch:NULL; }
CettaDurableStatus cetta_supervisor_open(const char *path,
        const CettaDurableLimits *limits, CettaSupervisor **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL; CettaSupervisor *s=calloc(1,sizeof(*s));
    if (!s) return DURABLE_NOMEM;
    if (pthread_mutex_init(&s->mutex,NULL)) { free(s); return DURABLE_NOMEM; }
    s->references=1;
    s->record_bytes=limits?limits->record_bytes:cetta_durable_default_limits().record_bytes;
    CettaDurableStatus st=cetta_durable_open(path,limits,&s->store);
    if (st!=DURABLE_OK) { destroy(s); return st; }
    st=cetta_durable_attach_runtime(s->store,s);
    CettaDurableSnapshot snap={0};
    if (st==DURABLE_OK) st=cetta_durable_snapshot(s->store,TASK_SPACE,&snap);
    if (st==DURABLE_OK) memcpy(s->epoch,snap.epoch,sizeof(s->epoch));
    for (size_t i=0; st==DURABLE_OK && i<snap.count; ++i) {
        CettaSupervisorTask t={0}; st=unpack(&snap.records[i],&t);
        if (st==DURABLE_OK && t.phase==SUP_ACTIVE) {
            CettaDurableObservation *obs=NULL;
            cetta_supervisor_task_free(&t);
            st=load(s,snap.records[i].key,&obs,&t);
            if (st==DURABLE_OK) { t.phase=SUP_UNCERTAIN; st=save(s,obs,snap.records[i].key,&t,DURABLE_REPLACE); }
            cetta_durable_observation_free(obs);
        }
        cetta_supervisor_task_free(&t);
    }
    cetta_durable_snapshot_free(&snap);
    if (st!=DURABLE_OK) { cetta_supervisor_close(s); return st; }
    *out=s; return DURABLE_OK;
}
CettaDurableStatus cetta_supervisor_submit(CettaSupervisor *s, const char *id,
        int64_t budget, const void *payload, size_t bytes) {
    if (!s || !valid_id(id) || budget<=0 || (bytes && !payload)) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableObservation *obs=NULL;
    CettaDurableScope scope={DURABLE_KEY,TASK_SPACE,id};
    CettaDurableStatus st=s->closed?DURABLE_INVALID:cetta_durable_observe(s->store,&scope,1,&obs);
    if (st==DURABLE_OK) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(obs,0);
        if (v->count) st=DURABLE_PRECONDITION;
        else {
            CettaSupervisorTask t={.remaining=budget,.phase=SUP_READY,
                .payload=(unsigned char *)payload,.payload_bytes=bytes};
            st=save(s,obs,id,&t,DURABLE_INSERT);
        }
    }
    cetta_durable_observation_free(obs); pthread_mutex_unlock(&s->mutex); return st;
}
CettaDurableStatus cetta_supervisor_get(CettaSupervisor *s, const char *id, CettaSupervisorTask *out) {
    if (!s || !out) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex); CettaDurableObservation *obs=NULL;
    CettaDurableStatus st=load(s,id,&obs,out);
    cetta_durable_observation_free(obs); pthread_mutex_unlock(&s->mutex); return st;
}
CettaDurableStatus cetta_supervisor_snapshot(CettaSupervisor *s,CettaDurableSnapshot *out) {
    if (!s || !out) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus st=s->closed?DURABLE_INVALID:cetta_durable_snapshot(s->store,TASK_SPACE,out);
    pthread_mutex_unlock(&s->mutex); return st;
}
CettaDurableStatus cetta_supervisor_next(CettaSupervisor *s, CettaDispatchPermit **out) {
    if (!s || !out) return DURABLE_INVALID;
    *out=NULL; pthread_mutex_lock(&s->mutex);
    CettaDurableScope scope={DURABLE_SPACE,TASK_SPACE,NULL};
    CettaDurableObservation *obs=NULL;
    CettaDurableStatus st=s->closed?DURABLE_INVALID:cetta_durable_observe(s->store,&scope,1,&obs);
    const CettaDurableSnapshot *v=st==DURABLE_OK?cetta_durable_observation_view(obs,0):NULL;
    for (size_t i=0; v && st==DURABLE_OK && i<v->count; ++i) {
        CettaSupervisorTask t={0}; st=unpack(&v->records[i],&t);
        if (st==DURABLE_OK && t.phase==SUP_READY) {
            if (t.generation==INT64_MAX || s->references==SIZE_MAX) st=DURABLE_LIMIT;
            CettaDispatchPermit *permit=st==DURABLE_OK?calloc(1,sizeof(*permit)):NULL;
            if (st==DURABLE_OK && !permit) st=DURABLE_NOMEM;
            if (st==DURABLE_OK) {
                --t.remaining; ++t.generation; t.phase=SUP_ACTIVE; t.acknowledged=false;
                st=save(s,obs,v->records[i].key,&t,DURABLE_REPLACE);
                if (st==DURABLE_OK) {
                    permit->owner=s; ++s->references;
                    memcpy(permit->id,v->records[i].key,strlen(v->records[i].key)+1);
                    permit->generation=t.generation; permit->payload=t.payload; permit->bytes=t.payload_bytes;
                    t.payload=NULL; *out=permit;
                } else free(permit);
            }
            cetta_supervisor_task_free(&t); break;
        }
        cetta_supervisor_task_free(&t);
    }
    cetta_durable_observation_free(obs); pthread_mutex_unlock(&s->mutex); return st;
}
CettaDurableStatus cetta_dispatch_permit_claim(CettaDispatchPermit *p,bool *granted) {
    if (!p || !granted) return DURABLE_INVALID;
    *granted=false;
    CettaSupervisor *s=p->owner; pthread_mutex_lock(&s->mutex);
    CettaDurableObservation *obs=NULL; CettaSupervisorTask t={0};
    CettaDurableStatus st=DURABLE_OK;
    if (!p->claimed && !s->closed) {
        st=load(s,p->id,&obs,&t);
        if (st==DURABLE_PRECONDITION) st=DURABLE_OK;
        else if (st==DURABLE_OK && t.phase==SUP_ACTIVE && t.generation==p->generation)
            p->claimed=*granted=true;
    }
    cetta_supervisor_task_free(&t); cetta_durable_observation_free(obs);
    pthread_mutex_unlock(&s->mutex); return st;
}
const char *cetta_dispatch_permit_id(const CettaDispatchPermit *p) { return p?p->id:NULL; }
const char *cetta_dispatch_permit_epoch(const CettaDispatchPermit *p) { return p?p->owner->epoch:NULL; }
int64_t cetta_dispatch_permit_generation(const CettaDispatchPermit *p) { return p?p->generation:0; }
const void *cetta_dispatch_permit_payload(const CettaDispatchPermit *p,size_t *bytes) {
    if (bytes) *bytes=p?p->bytes:0;
    return p?p->payload:NULL;
}
void cetta_dispatch_permit_free(CettaDispatchPermit *p) {
    if (!p) return;
    CettaSupervisor *s=p->owner;
    pthread_mutex_lock(&s->mutex); bool done=--s->references==0;
    pthread_mutex_unlock(&s->mutex);
    free(p->payload); free(p); if (done) destroy(s);
}
static bool current(const CettaSupervisor *s,const char *epoch,
        int64_t generation,const CettaSupervisorTask *t) {
    return epoch && strnlen(epoch,33)==32 && !strcmp(epoch,s->epoch) && generation==t->generation;
}
CettaDurableStatus cetta_supervisor_reply(CettaSupervisor *s,const char *epoch,
        const char *id,int64_t generation,CettaSupervisorReplyKind kind,bool succeeded,
        bool retry,const void *result,size_t bytes,bool *accepted) {
    if (!s || !accepted || (bytes && !result) || (kind!=SUP_COMPLETE && kind!=SUP_RECONCILE)) return DURABLE_INVALID;
    *accepted=false; pthread_mutex_lock(&s->mutex);
    CettaDurableObservation *obs=NULL; CettaSupervisorTask t={0};
    CettaDurableStatus st=load(s,id,&obs,&t);
    if (st==DURABLE_PRECONDITION) st=DURABLE_OK;
    else if (st==DURABLE_OK && current(s,epoch,generation,&t) &&
        t.phase==(kind==SUP_COMPLETE?SUP_ACTIVE:SUP_UNCERTAIN)) {
        bool fits=s->record_bytes>=HEADER_BYTES && t.payload_bytes<=s->record_bytes-HEADER_BYTES &&
            bytes<=s->record_bytes-HEADER_BYTES-t.payload_bytes;
        unsigned char *copy=fits?malloc(bytes?bytes:1):NULL;
        if (!copy) st=fits?DURABLE_NOMEM:DURABLE_LIMIT;
        else {
            if (bytes) memcpy(copy,result,bytes);
            free(t.result); t.result=copy; t.result_bytes=bytes; t.acknowledged=false;
            t.phase=succeeded?SUP_SUCCEEDED:retry && t.remaining?SUP_READY:SUP_DEAD_LETTER;
            st=save(s,obs,id,&t,DURABLE_REPLACE); *accepted=st==DURABLE_OK;
        }
    }
    cetta_supervisor_task_free(&t); cetta_durable_observation_free(obs);
    pthread_mutex_unlock(&s->mutex); return st;
}
typedef enum { CHANGE_UNCERTAIN, CHANGE_ABANDON, CHANGE_ACK } Change;
static CettaDurableStatus change(CettaSupervisor *s,const char *epoch,const char *id,
        int64_t gen,Change action,bool *accepted) {
    if (!s || !accepted) return DURABLE_INVALID;
    *accepted=false; pthread_mutex_lock(&s->mutex);
    CettaDurableObservation *obs=NULL; CettaSupervisorTask t={0};
    CettaDurableStatus st=load(s,id,&obs,&t);
    if (st==DURABLE_PRECONDITION) st=DURABLE_OK;
    else if (st==DURABLE_OK && (action==CHANGE_ABANDON || current(s,epoch,gen,&t))) {
        bool apply=false;
        if (action==CHANGE_UNCERTAIN && t.phase==SUP_ACTIVE) { t.phase=SUP_UNCERTAIN; apply=true; }
        if (action==CHANGE_ABANDON && t.phase==SUP_UNCERTAIN) { t.phase=SUP_UNRESOLVED; apply=true; }
        if (action==CHANGE_ACK && t.phase>=SUP_SUCCEEDED) { t.acknowledged=true; apply=true; }
        if (apply) { st=save(s,obs,id,&t,DURABLE_REPLACE); *accepted=st==DURABLE_OK; }
    }
    cetta_supervisor_task_free(&t); cetta_durable_observation_free(obs);
    pthread_mutex_unlock(&s->mutex); return st;
}
CettaDurableStatus cetta_supervisor_uncertain(CettaSupervisor *s,const char *e,const char *id,int64_t g,bool *a) {
    return change(s,e,id,g,CHANGE_UNCERTAIN,a);
}
CettaDurableStatus cetta_supervisor_abandon(CettaSupervisor *s,const char *id,bool *a) {
    return change(s,NULL,id,0,CHANGE_ABANDON,a);
}
CettaDurableStatus cetta_supervisor_acknowledge(CettaSupervisor *s,const char *e,const char *id,int64_t g,bool *a) {
    return change(s,e,id,g,CHANGE_ACK,a);
}
CettaDurableStatus cetta_supervisor_checkpoint(CettaSupervisor *s) {
    if (!s) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus st=s->closed?DURABLE_INVALID:cetta_durable_checkpoint(s->store);
    pthread_mutex_unlock(&s->mutex); return st;
}
