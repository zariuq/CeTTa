#define _POSIX_C_SOURCE 200809L
#include "http_worker.h"
#include "durable_store.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void wait_flag(atomic_uint *value, unsigned target) {
    uint64_t deadline=now_ms()+5000;
    while (atomic_load(value)<target && now_ms()<deadline) {
        struct timespec pause={0,1000000}; nanosleep(&pause,NULL);
    }
    assert(atomic_load(value)>=target);
}
static CettaHttpWorker *worker(const CettaHttpWorkerLimits *limits, const CettaHttpWorkerHooks *hooks) {
    CettaHttpWorker *w=NULL;
    assert(cetta_http_worker_new(limits,hooks,&w)==HTTP_WORKER_OK);
    return w;
}
static CettaHttpRequest request(uint64_t id, const char *url) {
    return (CettaHttpRequest){.id=id,.method="GET",.url=url,.timeout_ms=2000,.max_response_bytes=1024};
}
static CettaHttpResult take(CettaHttpWorker *w) {
    CettaHttpResult r={0}; uint64_t gen=0,deadline=now_ms()+5000;
    while (!cetta_http_worker_take(w,&r)) {
        assert(now_ms()<deadline);
        gen=cetta_http_worker_wait(w,gen,100);
    }
    return r;
}
static void body(CettaHttpResult *r, const void *bytes, size_t n) {
    assert(r->started && !r->cancelled && r->transport_code==0 && r->status==200);
    assert(r->body_size==n && (!n || !memcmp(r->body,bytes,n)));
}
static void url(char *out, const char *base, const char *path) {
    assert(snprintf(out,512,"%s%s",base,path)<512);
}
static void ephemeral(const char *base) {
    CettaHttpWorkerLimits limits=cetta_http_worker_default_limits(); limits.jobs=2;
    CettaHttpWorker *w=worker(&limits,NULL);
    char one[512],two[512]; url(one,base,"/one"); url(two,base,"/two");
    CettaHttpRequest a=request(1,one), b=request(2,two), extra=request(3,one);
    a.proxy="http://proxy\r\ninvalid";
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_INVALID);
    a.proxy=NULL; a.ca_file="ca\ninvalid";
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_INVALID);
    char too_long[4098]; memset(too_long,'a',sizeof(too_long)-1); too_long[4097]=0;
    a.ca_file=too_long;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_INVALID);
    a.ca_file=NULL; a.proxy=too_long;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_INVALID);
    a.proxy=NULL;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_DUPLICATE);
    assert(cetta_http_worker_submit(w,&b)==HTTP_WORKER_OK);
    assert(cetta_http_worker_submit(w,&extra)==HTTP_WORKER_FULL);
    for (int i=0;i<2;++i) {
        CettaHttpResult r=take(w);
        body(&r,r.id==1?"one":"two",3); cetta_http_result_free(&r);
    }
    CettaHttpResult r; assert(!cetta_http_worker_take(w,&r));
    uint64_t gen=cetta_http_worker_wait(w,0,0), before=now_ms();
    assert(cetta_http_worker_wait(w,gen,30)==gen && now_ms()-before>=25);
    char echo[512]; url(echo,base,"/echo");
    unsigned char bytes[]={0,'a',255,0};
    a=request(4,echo); a.method="POST"; a.body=bytes; a.body_size=sizeof(bytes);
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    memset(bytes,1,sizeof(bytes)); memset(echo,'x',strlen(echo));
    r=take(w); const unsigned char expected[]={0,'a',255,0}; body(&r,expected,sizeof(expected)); cetta_http_result_free(&r);
    url(one,base,"/bytes/0"); a=request(5,one); a.max_response_bytes=0;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    r=take(w); body(&r,"",0); cetta_http_result_free(&r);
    url(one,base,"/one"); a=request(6,one); a.max_response_bytes=2;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    r=take(w); assert(r.response_too_large && r.transport_code!=0); cetta_http_result_free(&r);
    url(one,base,"/slow"); a=request(7,one); a.timeout_ms=10;
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    r=take(w); assert(r.started && r.transport_code!=0); cetta_http_result_free(&r);
    for (unsigned i=0;i<100;++i) {
        a=request(100+i,one);
        CettaHttpWorkerStatus s=cetta_http_worker_submit(w,&a);
        if (s==HTTP_WORKER_FULL) { --i; struct timespec pause={0,1000000}; nanosleep(&pause,NULL); continue; }
        assert(s==HTTP_WORKER_OK && cetta_http_worker_abandon(w,a.id));
    }
    assert(cetta_http_worker_free(w)==0);
    limits.jobs=1; limits.response_bytes=2;
    w=worker(&limits,NULL); url(one,base,"/one"); a=request(1,one);
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_OK);
    r=take(w); assert(r.response_budget_exceeded && r.body_size<=2); cetta_http_result_free(&r);
    assert(cetta_http_worker_free(w)==0);
    limits.request_bytes=1;
    w=worker(&limits,NULL);
    a.body_size=1024*1024; a.body="x";
    /* Deliberately too short a pointer: rejecting the budget must precede copy. */
    assert(cetta_http_worker_submit(w,&a)==HTTP_WORKER_FULL);
    assert(cetta_http_worker_free(w)==0);
}

typedef struct {
    CettaDurableStore *store;
    pthread_t caller;
    atomic_uint prepare_calls, observations, acknowledged;
    atomic_bool allow_prepare, allow_observe;
    uint64_t id;
    const unsigned char *retained;
    size_t retained_size;
    bool expected_started;
} Journal;

static void commit(CettaDurableStore *s, CettaDurableOp *ops, size_t n) {
    CettaDurableSnapshot v;
    assert(cetta_durable_snapshot(s,NULL,&v)==DURABLE_OK);
    int64_t next;
    assert(cetta_durable_commit(s,v.epoch,v.revision,ops,n,&next)==DURABLE_OK);
    cetta_durable_snapshot_free(&v);
}
static CettaDurableSnapshot snapshot(CettaDurableStore *s, const char *space) {
    CettaDurableSnapshot v;
    assert(cetta_durable_snapshot(s,space,&v)==DURABLE_OK); return v;
}
static CettaHttpPrepare prepare(void *ctx, uint64_t id) {
    Journal *j=ctx;
    assert(id==j->id && !pthread_equal(j->caller,pthread_self()));
    atomic_fetch_add(&j->prepare_calls,1);
    if (!atomic_load(&j->allow_prepare)) return HTTP_PREPARE_DEFER;
    CettaDurableOp claim={DURABLE_REPLACE,"pending","one","attempted",9};
    commit(j->store,&claim,1);
    return HTTP_PREPARE_READY;
}
static CettaHttpRecord observe(void *ctx, const CettaHttpResult *r, CettaHttpRecordMode mode) {
    Journal *j=ctx;
    assert(mode==HTTP_RECORD_FULL);
    assert(r->id==j->id && r->started==j->expected_started);
    if (j->expected_started) assert(!r->cancelled && r->status==200 && !r->transport_code);
    else assert(r->cancelled);
    assert(!pthread_equal(j->caller,pthread_self()));
    if (atomic_load(&j->observations)) {
        assert(r->body==j->retained && r->body_size==j->retained_size);
    }
    j->retained=r->body; j->retained_size=r->body_size;
    atomic_fetch_add(&j->observations,1);
    if (!atomic_load(&j->allow_observe)) return HTTP_RECORD_RETRY;
    CettaDurableSnapshot v=snapshot(j->store,"pending");
    assert(v.count==1);
    if (r->started) assert(v.records[0].size==9 && !memcmp(v.records[0].data,"attempted",9));
    cetta_durable_snapshot_free(&v);
    CettaDurableOp ops[]={
        {DURABLE_REMOVE,"pending","one",NULL,0},
        {DURABLE_INSERT,"completion","one",r->body_size?r->body:(const unsigned char *)"cancelled",r->body_size?r->body_size:9}
    };
    commit(j->store,ops,2);
    atomic_fetch_add(&j->acknowledged,1);
    return HTTP_RECORD_ACK;
}
static void unexpected_stall(void *ctx, uint64_t id, CettaHttpRecordMode mode, unsigned attempts) {
    (void)ctx; (void)id; (void)mode; (void)attempts;
    assert(!"unexpected recording stall");
}
static unsigned remote_count(const char *base) {
    char path[512]; url(path,base,"/counted-total");
    CettaHttpWorker *w=worker(NULL,NULL); CettaHttpRequest r=request(1,path);
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    CettaHttpResult result=take(w); assert(result.transport_code==0);
    unsigned count=(unsigned)strtoul((char *)result.body,NULL,10);
    cetta_http_result_free(&result); cetta_http_worker_free(w); return count;
}
static void durable(const char *base, const char *path) {
    Journal j={.caller=pthread_self(),.id=1,.expected_started=true};
    assert(cetta_durable_open(path,NULL,&j.store)==DURABLE_OK);
    CettaDurableOp accepted={DURABLE_INSERT,"pending","one","accepted",8};
    commit(j.store,&accepted,1);
    CettaHttpWorkerHooks hooks={.context=&j,.prepare=prepare,.observe=observe,
                               .recording_stalled=unexpected_stall};
    CettaHttpWorker *w=worker(NULL,&hooks);
    char endpoint[512]; url(endpoint,base,"/counted"); CettaHttpRequest r=request(1,endpoint);
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    assert(!cetta_http_worker_abandon(w,1));
    wait_flag(&j.prepare_calls,2); assert(remote_count(base)==0);
    atomic_store(&j.allow_prepare,true);
    wait_flag(&j.observations,3);
    assert(remote_count(base)==1);
    /* Cancelling after a response was observed cannot erase that fact, even
     * while its durable recording is being retried. */
    assert(cetta_http_worker_cancel(w,1)==HTTP_CANCEL_TOO_LATE);
    CettaHttpResult unavailable; assert(!cetta_http_worker_take(w,&unavailable));
    CettaDurableSnapshot v=snapshot(j.store,"completion"); assert(v.count==0); cetta_durable_snapshot_free(&v);
    atomic_store(&j.allow_observe,true); wait_flag(&j.acknowledged,1);
    assert(cetta_http_worker_free(w)==0);
    assert(remote_count(base)==1); /* Persistence retries did not repeat HTTP. */
    cetta_durable_close(j.store);
    assert(cetta_durable_open(path,NULL,&j.store)==DURABLE_OK);
    v=snapshot(j.store,"completion");
    assert(v.count==1 && v.records[0].size==1 && v.records[0].data[0]=='1');
    cetta_durable_snapshot_free(&v);
    CettaDurableOp reset[]={ {DURABLE_REMOVE,"completion","one",NULL,0}, accepted };
    commit(j.store,reset,2);
    j.expected_started=false; j.retained=NULL; j.retained_size=0;
    atomic_store(&j.prepare_calls,0); atomic_store(&j.observations,0); atomic_store(&j.acknowledged,0);
    atomic_store(&j.allow_prepare,false);
    w=worker(NULL,&hooks); assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    wait_flag(&j.prepare_calls,1); assert(cetta_http_worker_cancel(w,1)==HTTP_CANCEL_NOT_STARTED);
    wait_flag(&j.acknowledged,1); assert(cetta_http_worker_free(w)==0);
    assert(remote_count(base)==1);
    commit(j.store,reset,2);
    j.expected_started=true; j.retained=NULL; j.retained_size=0;
    atomic_store(&j.observations,0); atomic_store(&j.allow_prepare,true); atomic_store(&j.allow_observe,false);
    w=worker(NULL,&hooks); assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    wait_flag(&j.observations,1);
    assert(cetta_http_worker_free(w)==1); /* Known response could not be journaled. */
    v=snapshot(j.store,"pending");
    assert(v.count==1 && v.records[0].size==9 && !memcmp(v.records[0].data,"attempted",9));
    cetta_durable_snapshot_free(&v);
    assert(remote_count(base)==2);
    cetta_durable_close(j.store);
}

typedef struct { CettaHttpWorker *worker; const char *url; unsigned start; } Producer;
static void *produce(void *context) {
    Producer *p=context;
    for (unsigned i=0;i<25;++i) {
        CettaHttpRequest r=request(p->start+i,p->url);
        uint64_t generation=0;
        for (;;) {
            CettaHttpWorkerStatus s=cetta_http_worker_submit(p->worker,&r);
            if (s==HTTP_WORKER_OK) break;
            assert(s==HTTP_WORKER_FULL);
            generation=cetta_http_worker_wait(p->worker,generation,100);
        }
    }
    return NULL;
}
static void concurrency(const char *base) {
    CettaHttpWorkerLimits limits=cetta_http_worker_default_limits(); limits.jobs=8;
    CettaHttpWorker *w=worker(&limits,NULL);
    char one[512]; url(one,base,"/one");
    Producer producers[4]; pthread_t threads[4]; bool seen[101]={false};
    for (unsigned i=0;i<4;++i) {
        producers[i]=(Producer){w,one,1+i*25};
        assert(!pthread_create(&threads[i],NULL,produce,&producers[i]));
    }
    for (unsigned i=0;i<100;++i) {
        CettaHttpResult r=take(w);
        assert(r.id>=1 && r.id<=100 && !seen[r.id]); seen[r.id]=true;
        body(&r,"one",3); cetta_http_result_free(&r);
    }
    for (unsigned i=0;i<4;++i) pthread_join(threads[i],NULL);
    assert(cetta_http_worker_free(w)==0);
}

typedef struct {
    CettaDurableStore *store;
    CettaHttpWorker *worker;
    bool minimal, full_repair, resume_inside;
    atomic_bool allow_record;
    atomic_uint full_calls, minimal_calls, stalls, acknowledgements;
    const unsigned char *retained;
    size_t retained_size;
} RecordingFailure;

static CettaHttpPrepare failure_prepare(void *ctx, uint64_t id) {
    RecordingFailure *f=ctx;
    CettaDurableOp op={DURABLE_REPLACE,"failure-pending",id==1?"one":"two","attempted",9};
    commit(f->store,&op,1);
    return HTTP_PREPARE_READY;
}
static CettaHttpRecord failure_observe(void *ctx, const CettaHttpResult *r, CettaHttpRecordMode mode) {
    RecordingFailure *f=ctx;
    assert(r->started && r->status==200 && !r->cancelled && r->transport_code==0);
    if (r->id==1) {
        if (mode==HTTP_RECORD_FULL) {
            if (atomic_load(&f->full_calls))
                assert(r->body==f->retained && r->body_size==f->retained_size);
            f->retained=r->body; f->retained_size=r->body_size;
            atomic_fetch_add(&f->full_calls,1);
            if (f->minimal && !(f->full_repair && atomic_load(&f->allow_record)))
                return HTTP_RECORD_USE_MINIMAL;
        } else {
            assert(f->minimal && !r->body && !r->body_size);
            atomic_fetch_add(&f->minimal_calls,1);
        }
        if (!atomic_load(&f->allow_record)) return HTTP_RECORD_RETRY;
    }
    char fact[128];
    int n=snprintf(fact,sizeof(fact),"OutcomeUnrecordable(%llu,%d,%ld)",
                   (unsigned long long)r->id,r->transport_code,r->status);
    const char *key=r->id==1?"one":"two";
    CettaDurableOp ops[]={
        {DURABLE_REMOVE,"failure-pending",key,NULL,0},
        {DURABLE_INSERT,"failure-outcomes",key,
            mode==HTTP_RECORD_MINIMAL?(const void *)fact:r->body,
            mode==HTTP_RECORD_MINIMAL?(size_t)n:r->body_size}
    };
    commit(f->store,ops,2);
    atomic_fetch_add(&f->acknowledgements,1);
    return HTTP_RECORD_ACK;
}
static void recording_stalled(void *ctx, uint64_t id, CettaHttpRecordMode mode, unsigned attempts) {
    RecordingFailure *f=ctx;
    assert(id==1 && attempts==3);
    assert(mode==(f->minimal?HTTP_RECORD_MINIMAL:HTTP_RECORD_FULL));
    CettaDurableSnapshot v=snapshot(f->store,"failure-pending");
    assert(v.count>=1); cetta_durable_snapshot_free(&v);
    if (f->resume_inside) {
        /* Re-enter the worker API: this deadlocks if escalation holds its lock. */
        atomic_store(&f->allow_record,true);
        assert(cetta_http_worker_resume_recording(f->worker,id));
    }
    atomic_fetch_add(&f->stalls,1);
}
static void recording_failure(const char *base, const char *path,
                              bool minimal, bool full_repair, bool resume_inside) {
    RecordingFailure f={.minimal=minimal,.full_repair=full_repair,.resume_inside=resume_inside};
    assert(cetta_durable_open(path,NULL,&f.store)==DURABLE_OK);
    CettaDurableOp ops[]={
        {DURABLE_INSERT,"failure-pending","one","accepted",8},
        {DURABLE_INSERT,"failure-pending","two","accepted",8}
    };
    commit(f.store,ops,2);
    CettaHttpWorkerHooks hooks={.context=&f,.prepare=failure_prepare,.observe=failure_observe,
        .recording_stalled=recording_stalled,.record_attempts=3};
    CettaHttpWorker *w=worker(NULL,&hooks);
    f.worker=w;
    unsigned before=remote_count(base);
    char endpoint[512]; url(endpoint,base,"/counted");
    CettaHttpRequest r=request(1,endpoint);
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    wait_flag(&f.stalls,1);
    r.id=2; assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    if (!resume_inside) {
        struct timespec pause={0,350000000}; nanosleep(&pause,NULL);
        assert(atomic_load(&f.full_calls)==(minimal?1u:3u));
        assert(atomic_load(&f.minimal_calls)==(minimal?3u:0u));
        assert(atomic_load(&f.stalls)==1 && atomic_load(&f.acknowledgements)==0);
        assert(remote_count(base)==before+1); /* blocked, with no retry spin or replay */
        assert(cetta_http_worker_cancel(w,1)==HTTP_CANCEL_TOO_LATE);
        assert(cetta_http_worker_cancel(w,999)==HTTP_CANCEL_UNKNOWN);
        atomic_store(&f.allow_record,true);
        assert(!cetta_http_worker_resume_recording(w,2));
        assert(cetta_http_worker_resume_recording(w,1));
    }
    wait_flag(&f.acknowledgements,2);
    assert(cetta_http_worker_free(w)==0 && remote_count(base)==before+2);
    assert(atomic_load(&f.full_calls)==(minimal?2u:4u));
    assert(atomic_load(&f.minimal_calls)==(minimal?(full_repair?3u:4u):0u));
    cetta_durable_close(f.store);
    assert(cetta_durable_open(path,NULL,&f.store)==DURABLE_OK);
    CettaDurableSnapshot v=snapshot(f.store,"failure-pending");
    assert(v.count==0); cetta_durable_snapshot_free(&v);
    v=snapshot(f.store,"failure-outcomes");
    assert(v.count==2);
    if (minimal && !full_repair) {
        const char fact[]="OutcomeUnrecordable(1,0,200)";
        assert(!strcmp(v.records[0].key,"one") && v.records[0].size==sizeof(fact)-1 &&
               !memcmp(v.records[0].data,fact,sizeof(fact)-1));
    } else {
        char receipt[32]; int n=snprintf(receipt,sizeof(receipt),"%u",before+1);
        assert(!strcmp(v.records[0].key,"one") && v.records[0].size==(size_t)n &&
               !memcmp(v.records[0].data,receipt,(size_t)n));
    }
    cetta_durable_snapshot_free(&v);
    CettaDurableOp cleanup[]={
        {DURABLE_REMOVE,"failure-outcomes","one",NULL,0},
        {DURABLE_REMOVE,"failure-outcomes","two",NULL,0}
    };
    commit(f.store,cleanup,2); cetta_durable_close(f.store);
}

/* Driven by the keep-alive fixture in test_http_worker.py. An idempotent warmup
 * leaves a pooled connection; default requests must neither use nor replenish it. */
static void connection_policy(const char *base, bool idempotent, const char *method) {
    CettaHttpWorker *w=worker(NULL,NULL);
    const char *paths[]={"/isolated-success","/isolated-after","/warm","/success","/drop-once","/after"};
    for (unsigned i=0;i<6;++i) {
        char endpoint[512]; url(endpoint,base,paths[i]);
        CettaHttpRequest r=request(i+1,endpoint);
        r.method=method;
        r.idempotent=(i==1 || i==2 || i==5)?true:idempotent;
        if (!strcmp(method,"POST")) { r.body=paths[i]; r.body_size=strlen(paths[i]); }
        assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
        CettaHttpResult result=take(w);
        if (i==4 && !idempotent)
            assert(result.started && result.transport_code!=0 && result.status==0);
        else body(&result,"ok",2);
        printf("%s: started=%d status=%ld transport=%d\n",paths[i],
               result.started,result.status,result.transport_code);
        cetta_http_result_free(&result);
    }
    assert(cetta_http_worker_free(w)==0);
}

typedef struct { atomic_uint entered, release, observed; } CancelPrepare;
static CettaHttpPrepare held_prepare(void *ctx, uint64_t id) {
    CancelPrepare *c=ctx; assert(id==1);
    atomic_store(&c->entered,1); wait_flag(&c->release,1);
    return HTTP_PREPARE_READY;
}
static CettaHttpRecord cancelled_observe(void *ctx, const CettaHttpResult *r, CettaHttpRecordMode mode) {
    CancelPrepare *c=ctx;
    assert(mode==HTTP_RECORD_FULL && r->cancelled && !r->started && !r->status);
    atomic_store(&c->observed,1); return HTTP_RECORD_ACK;
}
static void cancellation_and_redirects(const char *base) {
    unsigned before=remote_count(base);
    CancelPrepare c={0};
    CettaHttpWorkerHooks hooks={.context=&c,.prepare=held_prepare,.observe=cancelled_observe,
                               .recording_stalled=unexpected_stall};
    CettaHttpWorker *w=worker(NULL,&hooks);
    char endpoint[512]; url(endpoint,base,"/counted");
    CettaHttpRequest r=request(1,endpoint);
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    wait_flag(&c.entered,1);
    assert(cetta_http_worker_cancel(w,1)==HTTP_CANCEL_NOT_STARTED);
    atomic_store(&c.release,1); wait_flag(&c.observed,1);
    assert(cetta_http_worker_free(w)==0 && remote_count(base)==before);
    w=worker(NULL,NULL);
    url(endpoint,base,"/redirect-counted"); r=request(2,endpoint); r.method="POST";
    r.body="payload"; r.body_size=7;
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    CettaHttpResult result=take(w);
    assert(result.status==307 && !result.transport_code && result.request_size_known && result.request_size>0);
    cetta_http_result_free(&result);
    assert(remote_count(base)==before);
    r.id=3; r.follow_redirects=true;
    assert(cetta_http_worker_submit(w,&r)==HTTP_WORKER_OK);
    result=take(w); assert(result.status==200 && !result.transport_code);
    cetta_http_result_free(&result);
    assert(remote_count(base)==before+1 && cetta_http_worker_free(w)==0);
}

int main(int argc, char **argv) {
    if (argc==5 && !strcmp(argv[1],"--connection-policy")) {
        connection_policy(argv[2],!strcmp(argv[3],"idempotent"),argv[4]); return 0;
    }
    assert(argc==3);
    ephemeral(argv[1]); durable(argv[1],argv[2]); concurrency(argv[1]);
    recording_failure(argv[1],argv[2],false,false,false);
    recording_failure(argv[1],argv[2],true,false,false);
    recording_failure(argv[1],argv[2],true,true,false);
    recording_failure(argv[1],argv[2],true,true,true);
    cancellation_and_redirects(argv[1]);
    puts("HTTP worker: bounds, concurrency, durable claims, no replay, bounded recording retries, escalation/resume, minimal fact recovery, cancellation and redirect policy passed");
    return 0;
}
