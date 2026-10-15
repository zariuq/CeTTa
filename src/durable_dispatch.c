#define _POSIX_C_SOURCE 200809L
#include "durable_dispatch.h"
#include "durable_inbox.h"
#include "durable_timer.h"
#include "durable_value.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPATCH_JOBS 64
#define DISPATCH_CHANNELS 64
#define DISPATCH_RESPONSE (256u*1024u)
typedef struct Job {
    struct Job *next;
    char key[256], epoch[33], channel[256], version[256];
    char effect[96], attempt[100], completion[104];
    int64_t revision, position;
    unsigned char *intent;
    size_t size;
    uint64_t id;
    unsigned prepare_failures;
    CettaDurableStatus last_error;
    const CettaDispatchChannel *route;
    const CettaTelegramCredential *credential;
    CettaInboxWindow *poll;
} Job;
struct CettaDurableDispatch {
    CettaDurableStore *store;
    CettaDispatchConfig config;
    CettaDispatchChannel *channels;
    CettaHttpWorker *worker;
    pthread_mutex_t mutex;
    Job *jobs;
    size_t count;
    uint64_t next_id;
    bool stopping;
};
static bool name(const char *s) { return s && *s && strnlen(s,256)<256; }
static CettaDurableField text(const char *s) {
    return (CettaDurableField){.kind=DURABLE_FIELD_TEXT,.text={s,strlen(s)}};
}
static CettaDurableField symbol(const char *s) { CettaDurableField f=text(s); f.kind=DURABLE_FIELD_SYMBOL; return f; }
static CettaDurableField integer(int64_t x) { return (CettaDurableField){.kind=DURABLE_FIELD_INT,.integer=x}; }
static CettaDurableField boolean(bool x) { return (CettaDurableField){.kind=DURABLE_FIELD_BOOL,.boolean=x}; }
static void http_metadata(const CettaHttpResult *r, CettaDurableField fields[9]) {
    CettaDurableField values[]={boolean(r->started),boolean(r->cancelled),integer(r->transport_code),
        integer(r->status),boolean(r->request_size_known),integer(r->request_size),
        boolean(r->response_too_large),boolean(r->response_budget_exceeded),boolean(r->allocation_failed)};
    memcpy(fields,values,sizeof(values));
}
static CettaDurableStatus envelope(CettaDurableField *f, size_t n, CettaDurableOp *op) {
    CettaDurableField v={.kind=DURABLE_FIELD_EXPR,.expression={f,n}};
    unsigned char *data=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_fields_encode(&v,&data,&size);
    if (s==DURABLE_OK) { op->data=data; op->size=size; }
    return s;
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static void dispose(Job *j) { if (j) { cetta_inbox_window_free(j->poll); free(j->intent); free(j); } }
/* Only the main/evaluator thread decodes atoms. Worker hooks compare the exact
 * validated envelope bytes/revision, which includes channel name and version. */
static CettaDurableStatus load(const CettaDurableSnapshot *v, Job *j, Arena *a, Atom **intent) {
    if (v->count!=1) return DURABLE_PRECONDITION;
    const CettaDurableRecord *r=&v->records[0];
    CettaDurableStatus s=cetta_durable_value_decode(a,r->data,r->size,intent);
    if (s!=DURABLE_OK) return s;
    Atom *p=*intent;
    if (p->kind!=ATOM_EXPR || p->expr.len!=6 || !atom_is_symbol(p->expr.elems[0],"host:intent") ||
        p->expr.elems[1]->kind!=ATOM_GROUNDED || p->expr.elems[1]->ground.gkind!=GV_INT ||
        p->expr.elems[1]->ground.ival!=1) return DURABLE_VERSION;
    const char *channel=string(p->expr.elems[2]), *version=string(p->expr.elems[3]);
    if (!name(channel) || !name(version) || !name(r->key)) return DURABLE_CORRUPT;
    strcpy(j->key,r->key); strcpy(j->epoch,v->epoch);
    strcpy(j->channel,channel); strcpy(j->version,version);
    j->revision=r->revision; j->position=r->position;
    snprintf(j->effect,sizeof(j->effect),"%s/%lld/%lld",v->epoch,(long long)r->revision,(long long)r->position);
    snprintf(j->attempt,sizeof(j->attempt),"%s/1",j->effect);
    snprintf(j->completion,sizeof(j->completion),"effect/%s",j->effect);
    j->intent=malloc(r->size); if (!j->intent) return DURABLE_NOMEM;
    memcpy(j->intent,r->data,r->size); j->size=r->size;
    return DURABLE_OK;
}
static CettaDurableStatus observe(CettaDurableStore *store, const Job *j, CettaDurableObservation **o) {
    CettaDurableScope q[]={
        {DURABLE_KEY,"host.outbox",j->key}, {DURABLE_KEY,"host.attempts",j->key},
        {DURABLE_KEY,"host.outcomes",j->key}, {DURABLE_KEY,"host.inbox",j->completion},
        {DURABLE_KEY,"host.cancellations",j->key}
    };
    return cetta_durable_observe(store,q,5,o);
}
static const CettaDurableSnapshot *view(const CettaDurableObservation *o, size_t i) {
    return cetta_durable_observation_view(o,i);
}
static bool matches(const CettaDurableObservation *o, const Job *j) {
    const CettaDurableSnapshot *v=view(o,0);
    return v->count==1 && !strcmp(v->epoch,j->epoch) && v->records[0].revision==j->revision &&
        v->records[0].position==j->position && v->records[0].size==j->size &&
        !memcmp(v->records[0].data,j->intent,j->size);
}
static CettaDurableStatus attempt(const Job *j, CettaDurableOp *op) {
    *op=(CettaDurableOp){DURABLE_INSERT,"host.attempts",j->key,NULL,0};
    CettaDurableField f[]={symbol("host:attempt"),integer(1),text(j->effect),text(j->attempt),text(j->channel),text(j->version)};
    return envelope(f,6,op);
}
/* Outcome and completion are one transaction. An existing outcome is enough:
 * the completion may already have been consumed; never recreate it. */
static CettaDurableStatus outcome(CettaDurableStore *store, const Job *j,
        const CettaDurableObservation *o, const char *kind, const CettaHttpResult *r,
        const void *body, size_t size) {
    if (!matches(o,j)) return DURABLE_CONFLICT;
    if (view(o,2)->count) return DURABLE_OK;
    bool unknown=r==NULL;
    CettaHttpResult empty={0}; if (!r) r=&empty;
    CettaDurableOp ops[2]={{DURABLE_INSERT,"host.outcomes",j->key,NULL,0},
        {DURABLE_INSERT,"host.inbox",j->completion,NULL,0}};
    CettaDurableField metadata[9]; http_metadata(r,metadata);
    if (unknown) for (size_t i=0;i<9;++i) metadata[i]=symbol("unknown");
    CettaDurableField f[]={symbol("host:outcome"),integer(1),text(j->effect),
        text(view(o,1)->count?j->attempt:""),symbol(kind),
        {.kind=DURABLE_FIELD_EXPR,.expression={metadata,9}},
        {.kind=DURABLE_FIELD_TEXT,.text={body,size}}};
    CettaDurableStatus s=envelope(f,7,&ops[0]);
    CettaDurableField c[]={symbol("host:completion"),integer(1),text(j->effect),text(j->key)};
    if (s==DURABLE_OK) s=envelope(c,4,&ops[1]);
    int64_t revision;
    if (s==DURABLE_OK) s=cetta_durable_commit_observed(store,o,ops,2,&revision);
    free((void *)ops[0].data); free((void *)ops[1].data); return s;
}
static Job *find(CettaDurableDispatch *d, uint64_t id) {
    for (Job *j=d->jobs;j;j=j->next) if (j->id==id) return j;
    return NULL;
}
static uint64_t handle(CettaDurableDispatch *d, const char *key) {
    pthread_mutex_lock(&d->mutex);
    uint64_t id=0;
    for (Job *j=d->jobs;j;j=j->next) if (!strcmp(j->key,key)) { id=j->id; break; }
    pthread_mutex_unlock(&d->mutex); return id;
}
static void retire(CettaDurableDispatch *d, Job *j) {
    pthread_mutex_lock(&d->mutex);
    Job **p=&d->jobs; while (*p && *p!=j) p=&(*p)->next;
    if (*p) { *p=j->next; --d->count; }
    pthread_mutex_unlock(&d->mutex); dispose(j);
}
static CettaHttpPrepare prepare(void *ctx, uint64_t id) {
    CettaDurableDispatch *d=ctx;
    pthread_mutex_lock(&d->mutex); Job *j=find(d,id); pthread_mutex_unlock(&d->mutex);
    if (!j) return HTTP_PREPARE_DROP;
    /* A getUpdates offset only acknowledges already committed input. Its
     * repeat safety is host-owned; no application proposal can choose it. */
    if (j->poll) return HTTP_PREPARE_READY;
    CettaDurableObservation *o=NULL;
    CettaDurableStatus s=observe(d->store,j,&o);
    bool done=false;
    if (s==DURABLE_OK) {
        /* Registry is immutable for this owner's lifetime. The observed row
         * must still be exactly the name/version/payload we authorized. */
        if (strcmp(j->channel,j->route->name) || strcmp(j->version,j->route->version) || !matches(o,j)) s=DURABLE_VERSION;
        else if (view(o,1)->count || view(o,2)->count || view(o,4)->count) done=true;
        else {
            CettaDurableOp op={0}; s=attempt(j,&op); int64_t revision;
            if (s==DURABLE_OK) s=cetta_durable_commit_observed(d->store,o,&op,1,&revision);
            free((void *)op.data);
        }
    }
    cetta_durable_observation_free(o);
    if (s==DURABLE_OK) return done?HTTP_PREPARE_DROP:HTTP_PREPARE_READY;
    j->last_error=s;
    if (++j->prepare_failures<5 && (s==DURABLE_BUSY || s==DURABLE_CONFLICT)) return HTTP_PREPARE_DEFER;
    d->config.degraded(d->config.health_context,j->key,s);
    return HTTP_PREPARE_DROP; /* Known not started; recording still must succeed. */
}
static CettaHttpRecord record(void *ctx, const CettaHttpResult *r, CettaHttpRecordMode mode) {
    CettaDurableDispatch *d=ctx;
    pthread_mutex_lock(&d->mutex); Job *j=find(d,r->id); bool stopping=d->stopping; pthread_mutex_unlock(&d->mutex);
    if (!j) return HTTP_RECORD_RETRY;
    const char *kind=r->started?"observed":"not-started";
    const void *body=r->body; size_t size=r->body_size;
    if (r->started && (r->transport_code || r->response_too_large || r->response_budget_exceeded || r->allocation_failed)) kind="uncertain";
    if (mode==HTTP_RECORD_MINIMAL || (size && memchr(body,0,size))) {
        kind=r->started?"unrecordable":"not-started"; body=NULL; size=0;
    } else if (!cetta_telegram_response_safe(j->credential,body,size)) {
        kind="privacy-suppressed"; body=NULL; size=0;
    }
    CettaDurableStatus s;
    if (j->poll) {
        /* Poll cancellation on owner shutdown is repeat-safe at this window's
         * committed offset. Preserve its cause; never apply it to sends or
         * override privacy/minimal-fact outcomes. */
        if (stopping && r->cancelled && (!strcmp(kind,"observed") || !strcmp(kind,"uncertain") || !strcmp(kind,"not-started")))
            kind="shutdown";
        CettaDurableField metadata[9]; http_metadata(r,metadata);
        CettaDurableField f[]={symbol("host:poll"),integer(1),text(cetta_inbox_source(j->poll)),
            integer(cetta_inbox_offset(j->poll)),symbol(kind),
            {.kind=DURABLE_FIELD_EXPR,.expression={metadata,9}},
            {.kind=DURABLE_FIELD_TEXT,.text={body,size}}};
        CettaDurableOp op={0}; s=envelope(f,7,&op);
        if (s==DURABLE_OK) s=cetta_inbox_record_poll(j->poll,op.data,op.size);
        free((void *)op.data);
    } else {
        CettaDurableObservation *o=NULL; s=observe(d->store,j,&o);
        if (s==DURABLE_OK) s=outcome(d->store,j,o,kind,r,body,size);
        cetta_durable_observation_free(o);
    }
    if (s==DURABLE_OK) { retire(d,j); return HTTP_RECORD_ACK; }
    j->last_error=s;
    return mode==HTTP_RECORD_FULL && (s==DURABLE_LIMIT || s==DURABLE_INVALID)?HTTP_RECORD_USE_MINIMAL:HTTP_RECORD_RETRY;
}
static void stalled(void *ctx, uint64_t id, CettaHttpRecordMode mode, unsigned attempts) {
    (void)mode; (void)attempts;
    CettaDurableDispatch *d=ctx;
    pthread_mutex_lock(&d->mutex); Job *j=find(d,id); pthread_mutex_unlock(&d->mutex);
    if (j) d->config.degraded(d->config.health_context,j->key,j->last_error);
}
static CettaDurableStatus read_job(CettaDurableStore *store, const char *key, Job *j, Arena *a, Atom **p) {
    CettaDurableScope q={DURABLE_KEY,"host.outbox",key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,&q,1,&o);
    if (s==DURABLE_OK) s=load(view(o,0),j,a,p);
    cetta_durable_observation_free(o); return s;
}
/* Exclusive startup, before any owner thread. No replay and no HTTP. */
static CettaDurableStatus recover(CettaDurableStore *store) {
    CettaDurableSnapshot attempts;
    CettaDurableStatus s=cetta_durable_snapshot(store,"host.attempts",&attempts);
    if (s!=DURABLE_OK) return s;
    for (size_t i=0;s==DURABLE_OK && i<attempts.count;++i) {
        Job j={0}; Arena a; arena_init(&a); Atom *p=NULL;
        s=read_job(store,attempts.records[i].key,&j,&a,&p);
        CettaDurableObservation *o=NULL;
        if (s==DURABLE_OK) s=observe(store,&j,&o);
        if (s==DURABLE_OK) {
            CettaDurableOp expected={0}; s=attempt(&j,&expected);
            if (s==DURABLE_OK && (view(o,1)->count!=1 || view(o,1)->records[0].size!=expected.size ||
                memcmp(view(o,1)->records[0].data,expected.data,expected.size))) s=DURABLE_CORRUPT;
            free((void *)expected.data);
            /* No transport metadata survived. Do not fabricate non-send. */
            if (s==DURABLE_OK) s=outcome(store,&j,o,"uncertain",NULL,NULL,0);
        }
        cetta_durable_observation_free(o); free(j.intent); arena_free(&a);
    }
    cetta_durable_snapshot_free(&attempts); return s;
}
CettaDurableStatus cetta_dispatch_new(CettaDurableStore *store,
        const CettaDispatchConfig *c, CettaDurableDispatch **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !c || !c->degraded || c->channel_count>DISPATCH_CHANNELS ||
        (c->channel_count && !c->channels) || !c->timeout_ms || !c->max_response_bytes || c->max_response_bytes>DISPATCH_RESPONSE) return DURABLE_INVALID;
    for (size_t i=0;i<c->channel_count;++i) {
        const CettaDispatchChannel *r=&c->channels[i];
        if (!name(r->name) || !name(r->version) || !r->credential || !r->plan) return DURABLE_INVALID;
        for (size_t k=0;k<i;++k) if (!strcmp(c->channels[k].name,r->name)) return DURABLE_INVALID;
    }
    CettaDurableDispatch *d=calloc(1,sizeof(*d)); if (!d) return DURABLE_NOMEM;
    d->store=store; d->config=*c; d->next_id=1;
    if (pthread_mutex_init(&d->mutex,NULL)) { free(d); return DURABLE_IO; }
    d->channels=calloc(c->channel_count,sizeof(*d->channels));
    CettaDurableStatus s=cetta_durable_attach_runtime(store,d);
    if (s!=DURABLE_OK) goto fail;
    s=DURABLE_NOMEM;
    if (c->channel_count && !d->channels) goto fail;
    for (size_t i=0;i<c->channel_count;++i) {
        d->channels[i]=c->channels[i];
        d->channels[i].name=strdup(c->channels[i].name);
        d->channels[i].version=strdup(c->channels[i].version);
        if (!d->channels[i].name || !d->channels[i].version) goto fail;
    }
    d->config.channels=d->channels;
    s=recover(store); if (s!=DURABLE_OK) goto fail;
    CettaHttpWorkerHooks hooks={.context=d,.prepare=prepare,.observe=record,.recording_stalled=stalled};
    if (cetta_http_worker_new(NULL,&hooks,&d->worker)!=HTTP_WORKER_OK) { s=DURABLE_IO; goto fail; }
    *out=d; return DURABLE_OK;
fail:
    cetta_dispatch_free(d); return s;
}
CettaDurableStatus cetta_dispatch_submit(CettaDurableDispatch *d, const char *key) {
    if (!d || !name(key)) return DURABLE_INVALID;
    if (handle(d,key)) return DURABLE_BUSY;
    Job *j=calloc(1,sizeof(*j)); if (!j) return DURABLE_NOMEM;
    Arena a; arena_init(&a); Atom *p=NULL;
    CettaDurableStatus s=read_job(d->store,key,j,&a,&p);
    CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=observe(d->store,j,&o);
    if (s==DURABLE_OK && (view(o,1)->count || view(o,2)->count || view(o,4)->count)) s=DURABLE_PRECONDITION;
    cetta_durable_observation_free(o);
    if (s!=DURABLE_OK) goto done;
    for (size_t i=0;i<d->config.channel_count;++i)
        if (!strcmp(j->channel,d->channels[i].name) && !strcmp(j->version,d->channels[i].version)) j->route=&d->channels[i];
    if (!j->route) { s=DURABLE_VERSION; goto done; }
    j->credential=j->route->credential;
    CettaTelegramPlan plan={0};
    if (!j->route->plan(j->route->context,&a,p->expr.elems[4],p->expr.elems[5],&plan)) { s=DURABLE_INVALID; goto done; }
    pthread_mutex_lock(&d->mutex);
    if (d->count>=DISPATCH_JOBS || !d->next_id) { pthread_mutex_unlock(&d->mutex); s=DURABLE_LIMIT; goto done; }
    j->id=d->next_id++; j->next=d->jobs; d->jobs=j; ++d->count;
    pthread_mutex_unlock(&d->mutex);
    CettaHttpWorkerStatus h=cetta_telegram_submit_effect(j->route->credential,d->worker,j->id,
        plan.method,plan.content_type,plan.body,plan.size,d->config.timeout_ms,d->config.max_response_bytes);
    if (h==HTTP_WORKER_OK) { j=NULL; s=DURABLE_OK; }
    else { retire(d,j); j=NULL; s=h==HTTP_WORKER_FULL?DURABLE_LIMIT:h==HTTP_WORKER_NOMEM?DURABLE_NOMEM:DURABLE_INVALID; }
done:
    dispose(j); arena_free(&a); return s;
}
CettaDurableStatus cetta_dispatch_poll(CettaDurableDispatch *d, const CettaTelegramPoll *p) {
    CettaClockSample now;
    if (!cetta_clock_sample(&now)) return DURABLE_IO;
    return cetta_dispatch_poll_at(d,p,now.utc_ms);
}
CettaDurableStatus cetta_dispatch_poll_at(CettaDurableDispatch *d, const CettaTelegramPoll *p, int64_t now) {
    if (!d || !p || !p->credential || !p->limit || p->limit>100 || p->update_count>64 ||
        (p->update_count && !p->allowed_updates) || d->config.timeout_ms<1000 ||
        p->wait_seconds>(d->config.timeout_ms-1000)/1000) return DURABLE_INVALID;
    Job *j=calloc(1,sizeof(*j)); if (!j) return DURABLE_NOMEM;
    CettaDurableStatus s=cetta_inbox_begin_at(d->store,p->source,now,&j->poll);
    if (s!=DURABLE_OK) { dispose(j); return s; }
    snprintf(j->key,sizeof(j->key),"poll/%s",cetta_inbox_source(j->poll));
    if (handle(d,j->key)) { dispose(j); return DURABLE_BUSY; }
    char body[4608];
    size_t used=(size_t)snprintf(body,sizeof(body),"{\"offset\":%lld,\"limit\":%u,\"timeout\":%u,\"allowed_updates\":[",
        (long long)cetta_inbox_offset(j->poll),p->limit,p->wait_seconds);
    for (size_t i=0;i<p->update_count;++i) {
        const char *v=p->allowed_updates[i]; size_t n=v?strnlen(v,65):0;
        if (!n || n>64) { dispose(j); return DURABLE_INVALID; }
        for (size_t k=0;k<n;++k) if (!((v[k]>='a' && v[k]<='z') || v[k]=='_')) { dispose(j); return DURABLE_INVALID; }
        if (used+n+4>=sizeof(body)) { dispose(j); return DURABLE_LIMIT; }
        int added=snprintf(body+used,sizeof(body)-used,"%s\"%s\"",i?",":"",v);
        used+=(size_t)added;
    }
    memcpy(body+used,"]}",2); used+=2;
    j->credential=p->credential;
    pthread_mutex_lock(&d->mutex);
    if (d->count>=DISPATCH_JOBS || !d->next_id) { pthread_mutex_unlock(&d->mutex); dispose(j); return DURABLE_LIMIT; }
    j->id=d->next_id++; j->next=d->jobs; d->jobs=j; ++d->count;
    pthread_mutex_unlock(&d->mutex);
    CettaHttpWorkerStatus h=cetta_telegram_submit_poll(j->credential,d->worker,j->id,body,used,
        d->config.timeout_ms,d->config.max_response_bytes);
    if (h==HTTP_WORKER_OK) return DURABLE_OK;
    retire(d,j);
    return h==HTTP_WORKER_FULL?DURABLE_LIMIT:h==HTTP_WORKER_NOMEM?DURABLE_NOMEM:DURABLE_INVALID;
}
CettaDurableStatus cetta_dispatch_cancel(CettaDurableDispatch *d, const char *key) {
    if (!d || !name(key)) return DURABLE_INVALID;
    Job j={0}; Arena a; arena_init(&a); Atom *p=NULL;
    CettaDurableStatus s=read_job(d->store,key,&j,&a,&p);
    CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=observe(d->store,&j,&o);
    if (s==DURABLE_OK && !matches(o,&j)) s=DURABLE_CONFLICT;
    if (s==DURABLE_OK && !view(o,2)->count) {
        if (!view(o,1)->count) {
            CettaHttpResult r={.cancelled=true}; s=outcome(d->store,&j,o,"not-started",&r,NULL,0);
        } else if (!view(o,4)->count) {
            CettaDurableOp op={DURABLE_INSERT,"host.cancellations",j.key,NULL,0};
            CettaDurableField f[]={symbol("host:cancel"),integer(1),text(j.effect),text(j.attempt)};
            s=envelope(f,4,&op); int64_t revision;
            if (s==DURABLE_OK) s=cetta_durable_commit_observed(d->store,o,&op,1,&revision);
            free((void *)op.data);
        }
    }
    cetta_durable_observation_free(o); free(j.intent); arena_free(&a);
    if (s==DURABLE_OK) { uint64_t id=handle(d,key); if (id) cetta_http_worker_cancel(d->worker,id); }
    return s;
}
bool cetta_dispatch_resume(CettaDurableDispatch *d, const char *key) {
    if (!d || !name(key)) return false;
    uint64_t id=handle(d,key); return id && cetta_http_worker_resume_recording(d->worker,id);
}
uint64_t cetta_dispatch_wait(CettaDurableDispatch *d, uint64_t generation, uint32_t ms) {
    return d?cetta_http_worker_wait(d->worker,generation,ms):generation;
}
size_t cetta_dispatch_free(CettaDurableDispatch *d) {
    if (!d) return 0;
    pthread_mutex_lock(&d->mutex); d->stopping=true; pthread_mutex_unlock(&d->mutex);
    size_t lost=d->worker?cetta_http_worker_free(d->worker):0;
    while (d->jobs) { Job *j=d->jobs; d->jobs=j->next; dispose(j); }
    for (size_t i=0;d->channels && i<d->config.channel_count;++i) {
        free((void *)d->channels[i].name); free((void *)d->channels[i].version);
    }
    free(d->channels); cetta_durable_detach_runtime(d->store,d);
    pthread_mutex_destroy(&d->mutex); free(d); return lost;
}
