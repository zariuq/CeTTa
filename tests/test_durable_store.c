#define _POSIX_C_SOURCE 200809L
#include "durable_store.h"
#include "native_journal.h"
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *crash_at;
static CettaDurableStore *concurrent_writer;
static pthread_t writer_thread;
static void *write_during_read(void *context);
static bool fail_sync;
static unsigned sync_failures;
static int64_t write_allowance=-1;
ssize_t __real_write(int fd,const void *bytes,size_t size);
ssize_t __wrap_write(int fd,const void *bytes,size_t size) {
    if (write_allowance<0) return __real_write(fd,bytes,size);
    if (!write_allowance) { errno=EIO; return -1; }
    if (size>(uint64_t)write_allowance) size=(size_t)write_allowance;
    ssize_t n=__real_write(fd,bytes,size); if (n>0) write_allowance-=n; return n;
}
int __real_fsync(int fd);
int __wrap_fsync(int fd) {
    if (fail_sync) { ++sync_failures; errno=EIO; return -1; }
    return __real_fsync(fd);
}
void cetta_durable_test_boundary(const char *boundary) {
    if (concurrent_writer && !strcmp(boundary,"read_meta")) {
        CettaDurableStore *writer=concurrent_writer; concurrent_writer=NULL;
        assert(!pthread_create(&writer_thread,NULL,write_during_read,writer));
    }
    if (crash_at && !strcmp(crash_at, boundary)) kill(getpid(), SIGKILL);
}

static void check(CettaDurableStatus actual, CettaDurableStatus expected) {
    if (actual != expected) {
        fprintf(stderr, "expected %s, got %s\n", cetta_durable_status_name(expected),
                cetta_durable_status_name(actual)); abort();
    }
}

static CettaDurableStore *open_store(const char *path, CettaDurableLimits *limits) {
    CettaDurableStore *s = NULL;
    check(cetta_durable_open(path, limits, &s), DURABLE_OK); return s;
}

static CettaDurableSnapshot snapshot(CettaDurableStore *s) {
    CettaDurableSnapshot v;
    check(cetta_durable_snapshot(s, NULL, &v), DURABLE_OK); return v;
}

static CettaDurableOp put(CettaDurableOpKind kind, const char *space,
        const char *key, const char *value) {
    return (CettaDurableOp){kind,space,key,value,value ? strlen(value) : 0};
}

static int64_t commit(CettaDurableStore *s, CettaDurableOp *ops, size_t n) {
    CettaDurableSnapshot v = snapshot(s); int64_t next = -1;
    check(cetta_durable_commit(s, v.epoch, v.revision, ops, n, &next), DURABLE_OK);
    assert(next == v.revision + 1);
    cetta_durable_snapshot_free(&v); return next;
}

static void *write_during_read(void *context) {
    CettaDurableOp op=put(DURABLE_INSERT,"state","one","x");
    commit(context,&op,1); return NULL;
}

static const CettaDurableRecord *record(CettaDurableSnapshot *v,
        const char *space, const char *key) {
    for (size_t i=0; i<v->count; ++i)
        if (!strcmp(v->records[i].space,space) && !strcmp(v->records[i].key,key)) return &v->records[i];
    return NULL;
}

static void value(CettaDurableSnapshot *v, const char *space, const char *key, const char *text) {
    const CettaDurableRecord *r = record(v, space, key);
    assert(r && r->size == strlen(text) && !memcmp(r->data,text,r->size));
}

static void replay_equal(CettaDurableStore *s) {
    CettaDurableSnapshot a = snapshot(s), b;
    check(cetta_durable_recover(s,&b), DURABLE_OK);
    assert(!strcmp(a.epoch,b.epoch) && a.revision==b.revision && a.count==b.count);
    for (size_t i=0; i<a.count; ++i) {
        CettaDurableRecord *x=&a.records[i];
        const CettaDurableRecord *y=record(&b,x->space,x->key);
        assert(y && x->revision==y->revision && x->position==y->position);
        assert(x->size==y->size && !memcmp(x->data,y->data,x->size));
    }
    cetta_durable_snapshot_free(&a); cetta_durable_snapshot_free(&b);
}

static void basic(const char *path) {
    CettaDurableStore *s = open_store(path,NULL), *other = NULL;
    check(cetta_durable_open(path,NULL,&other), DURABLE_BUSY);
    assert(!other);
    CettaDurableOp init[] = {put(DURABLE_INSERT,"inbox","update:1","hello"),
        put(DURABLE_INSERT,"state","worker","waiting")};
    commit(s,init,2);
    CettaDurableSnapshot before = snapshot(s);
    CettaDurableOp transition[] = {put(DURABLE_REMOVE,"inbox","update:1",NULL),
        put(DURABLE_REPLACE,"state","worker","continuation:2"),
        put(DURABLE_INSERT,"outbox","send:1","accepted")};
    int64_t next;
    check(cetta_durable_commit(s,before.epoch,before.revision,transition,3,&next),DURABLE_OK);
    assert(next==2);
    value(&before,"state","worker","waiting"); /* no premature publication */
    /* A read of absence in another named space still conflicts. */
    CettaDurableOp stale=put(DURABLE_INSERT,"reminders","r","duplicate");
    check(cetta_durable_commit(s,before.epoch,before.revision,&stale,1,&next),DURABLE_CONFLICT);
    assert(next==-1);
    CettaDurableSnapshot now=snapshot(s);
    assert(!record(&now,"inbox","update:1"));
    value(&now,"state","worker","continuation:2"); value(&now,"outbox","send:1","accepted");
    /* Late precondition failure rolls back earlier writes and log entries. */
    CettaDurableOp bad[]={put(DURABLE_REPLACE,"state","worker","must-not-appear"),
        put(DURABLE_REMOVE,"inbox","missing",NULL)};
    check(cetta_durable_commit(s,now.epoch,now.revision,bad,2,&next),DURABLE_PRECONDITION);
    assert(next==-1);
    CettaDurableOp duplicate[]={put(DURABLE_REPLACE,"state","worker","first"),
        put(DURABLE_REPLACE,"state","worker","second")};
    check(cetta_durable_commit(s,now.epoch,now.revision,duplicate,2,&next),DURABLE_PRECONDITION);
    assert(next==-1);
    check(cetta_durable_commit(s,"00000000000000000000000000000000",now.revision,&stale,1,&next),DURABLE_CONFLICT);
    replay_equal(s);
    check(cetta_durable_checkpoint(s),DURABLE_OK);
    replay_equal(s);
    CettaDurableOp delta[]={put(DURABLE_REMOVE,"outbox","send:1",NULL),
        put(DURABLE_INSERT,"outbox","send:2","uncertain"),
        put(DURABLE_INSERT,"outbox","send:3","uncertain")};
    commit(s,delta,3); replay_equal(s); /* identical values retain two occurrences */
    cetta_durable_close(s);
    s=open_store(path,NULL); replay_equal(s);
    CettaDurableSnapshot reopened=snapshot(s);
    assert(!strcmp(reopened.epoch,now.epoch) && reopened.revision==3 && reopened.count==3);
    value(&reopened,"state","worker","continuation:2");
    value(&reopened,"outbox","send:2","uncertain");
    cetta_durable_snapshot_free(&before); cetta_durable_snapshot_free(&now);
    cetta_durable_snapshot_free(&reopened); cetta_durable_close(s);
}

static void crashes(const char *path) {
    const char *boundaries[]={"operation","before_commit","after_commit","checkpoint"};
    for (size_t i=0; i<4; ++i) {
        CettaDurableStore *s=open_store(path,NULL);
        char id[32]; snprintf(id,sizeof(id),"crash:%zu",i);
        CettaDurableOp initial=put(DURABLE_INSERT,"inbox",id,"pending");
        commit(s,&initial,1);
        CettaDurableSnapshot old=snapshot(s);
        cetta_durable_close(s);
        pid_t pid=fork(); assert(pid>=0);
        if (!pid) {
            s=open_store(path,NULL); crash_at=boundaries[i];
            if (i==3) cetta_durable_checkpoint(s);
            else {
                CettaDurableOp ops[]={put(DURABLE_REMOVE,"inbox",id,NULL),
                    put(DURABLE_INSERT,"outbox",id,"accepted")};
                int64_t next;
                cetta_durable_commit(s,old.epoch,old.revision,ops,2,&next);
            }
            _exit(90); /* the injected crash must have fired */
        }
        int w; assert(waitpid(pid,&w,0)==pid && WIFSIGNALED(w) && WTERMSIG(w)==SIGKILL);
        s=open_store(path,NULL); CettaDurableSnapshot now=snapshot(s);
        bool committed=i==2;
        assert(now.revision==old.revision+(committed?1:0));
        assert((record(&now,"inbox",id)!=NULL)==!committed);
        assert((record(&now,"outbox",id)!=NULL)==committed);
        replay_equal(s); cetta_durable_close(s);
        cetta_durable_snapshot_free(&old); cetta_durable_snapshot_free(&now);
    }
}

static void limits(const char *path) {
    CettaDurableLimits l=cetta_durable_default_limits();
    l.record_bytes=128; l.batch_bytes=256; l.live_bytes=256; l.history_bytes=256; l.records=2;
    CettaDurableStore *s=open_store(path,&l);
    CettaDurableOp op=put(DURABLE_INSERT,"outbox","one","unresolved");
    commit(s,&op,1);
    for (int i=0;i<100;++i) {
        op=put(DURABLE_REPLACE,"outbox","one",i%2?"uncertain":"accepted");
        commit(s,&op,1); replay_equal(s); /* automatic history compaction */
    }
    CettaDurableSnapshot old=snapshot(s);
    CettaDurableOp overflow[]={put(DURABLE_INSERT,"outbox","two","accepted"),
        put(DURABLE_INSERT,"outbox","three","accepted")};
    int64_t next;
    check(cetta_durable_commit(s,old.epoch,old.revision,overflow,2,&next),DURABLE_LIMIT);
    assert(next==-1); replay_equal(s);
    CettaDurableSnapshot now=snapshot(s);
    assert(now.count==1 && now.revision==old.revision);
    cetta_durable_snapshot_free(&old); cetta_durable_snapshot_free(&now); cetta_durable_close(s);
}

static void disk_full(const char *path) {
    CettaDurableLimits l=cetta_durable_default_limits(); l.storage_pages=64;
    CettaDurableStore *s=open_store(path,&l);
    CettaDurableOp first=put(DURABLE_INSERT,"state","s","before"); commit(s,&first,1);
    CettaDurableSnapshot old=snapshot(s);
    size_t n=512*1024; char *large=malloc(n); assert(large); memset(large,'x',n);
    CettaDurableOp ops[]={put(DURABLE_REPLACE,"state","s","after"),
        {DURABLE_INSERT,"outbox","large",large,n}};
    int64_t next;
    check(cetta_durable_commit(s,old.epoch,old.revision,ops,2,&next),DURABLE_LIMIT);
    assert(next==-1); free(large); cetta_durable_close(s);
    s=open_store(path,&l); replay_equal(s); CettaDurableSnapshot now=snapshot(s);
    assert(now.revision==old.revision && now.count==1); value(&now,"state","s","before");
    cetta_durable_snapshot_free(&old); cetta_durable_snapshot_free(&now); cetta_durable_close(s);
}

static void wrong_version(const char *path) {
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); assert(fd>=0);
    assert(write(fd,"foreign-format",14)==14); assert(!close(fd)); CettaDurableStore *s=NULL;
    check(cetta_durable_open(path,NULL,&s),DURABLE_VERSION); assert(!s);
}

static void *increment(void *context) {
    CettaDurableStore *s=context;
    for (int i=0;i<100;) {
        CettaDurableSnapshot v=snapshot(s);
        const CettaDurableRecord *r=record(&v,"state","counter");
        assert(r && r->size==sizeof(uint64_t));
        uint64_t x; memcpy(&x,r->data,sizeof(x)); ++x;
        CettaDurableOp op={DURABLE_REPLACE,"state","counter",&x,sizeof(x)};
        int64_t next;
        CettaDurableStatus result=cetta_durable_commit(s,v.epoch,v.revision,&op,1,&next);
        assert(result==DURABLE_OK || result==DURABLE_CONFLICT);
        if (result==DURABLE_OK) ++i;
        cetta_durable_snapshot_free(&v);
    }
    return NULL;
}

static void concurrent(const char *path) {
    CettaDurableStore *s=open_store(path,NULL); uint64_t zero=0;
    CettaDurableOp op={DURABLE_INSERT,"state","counter",&zero,sizeof(zero)};
    commit(s,&op,1);
    pthread_t workers[4];
    for (int i=0;i<4;++i) assert(!pthread_create(&workers[i],NULL,increment,s));
    for (int i=0;i<4;++i) assert(!pthread_join(workers[i],NULL));
    CettaDurableSnapshot v=snapshot(s);
    uint64_t result; memcpy(&result,record(&v,"state","counter")->data,sizeof(result));
    assert(result==400 && v.revision==401);
    replay_equal(s); cetta_durable_snapshot_free(&v); cetta_durable_close(s);
}


static void commit_ioerr(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableOp op=put(DURABLE_INSERT,"state","one","before"); commit(s,&op,1);
    CettaDurableSnapshot old=snapshot(s); int64_t next;
    op=put(DURABLE_REPLACE,"state","one","after");
    fail_sync=true;
    check(cetta_durable_commit(s,old.epoch,old.revision,&op,1,&next),DURABLE_UNKNOWN);
    assert(next==-1 && sync_failures>0);
    fail_sync=false; /* Even a healthy filesystem must not revive the handle. */
    CettaDurableSnapshot v;
    check(cetta_durable_snapshot(s,NULL,&v),DURABLE_POISONED);
    check(cetta_durable_recover(s,&v),DURABLE_POISONED);
    check(cetta_durable_checkpoint(s),DURABLE_POISONED);
    check(cetta_durable_commit(s,old.epoch,old.revision,&op,1,&next),DURABLE_POISONED);
    CettaDurableUsage usage; check(cetta_durable_usage(s,&usage),DURABLE_POISONED);
    cetta_durable_close(s);
    s=open_store(path,NULL); replay_equal(s);
    v=snapshot(s);
    assert(v.revision==old.revision || v.revision==old.revision+1);
    value(&v,"state","one",v.revision==old.revision ? "before" : "after");
    cetta_durable_snapshot_free(&v); cetta_durable_snapshot_free(&old); cetta_durable_close(s);
}

static void degraded(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    char text[4096]; memset(text,'x',sizeof(text)); text[sizeof(text)-1]=0;
    CettaDurableOp ops[]={put(DURABLE_INSERT,"state","one",text),put(DURABLE_INSERT,"state","two",text)};
    commit(s,ops,2); cetta_durable_close(s);
    CettaDurableLimits l=cetta_durable_default_limits();
    l.record_bytes=16; l.batch_bytes=128; l.live_bytes=32; l.records=1; l.history_bytes=128;
    s=open_store(path,&l); replay_equal(s);
    CettaDurableUsage u; check(cetta_durable_usage(s,&u),DURABLE_OK); assert(u.limits_exceeded);
    CettaDurableSnapshot v=snapshot(s); int64_t next;
    CettaDurableOp growth=put(DURABLE_INSERT,"state","three","x");
    check(cetta_durable_commit(s,v.epoch,v.revision,&growth,1,&next),DURABLE_LIMIT);
    cetta_durable_snapshot_free(&v);
    CettaDurableOp shrink=put(DURABLE_REPLACE,"state","two","small"); commit(s,&shrink,1);
    CettaDurableOp remove=put(DURABLE_REMOVE,"state","one",NULL); commit(s,&remove,1);
    check(cetta_durable_checkpoint(s),DURABLE_OK); replay_equal(s);
    check(cetta_durable_usage(s,&u),DURABLE_OK); assert(!u.limits_exceeded);
    cetta_durable_close(s);
}

static void recovery_mismatch(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableOp op=put(DURABLE_INSERT,"state","one","original"); commit(s,&op,1);
    cetta_durable_close(s);
    int fd=open(path,O_RDWR); assert(fd>=0); unsigned char byte;
    off_t end=lseek(fd,-1,SEEK_END); assert(end>0 && read(fd,&byte,1)==1); byte^=1;
    assert(pwrite(fd,&byte,1,end)==1); assert(!close(fd));
    check(cetta_durable_open(path,NULL,&s),DURABLE_CORRUPT); assert(!s);
}

static void removal_reserve(const char *path) {
    CettaDurableLimits l=cetta_durable_default_limits();
    l.record_bytes=12*1024; l.batch_bytes=13*1024; l.history_bytes=13*1024; l.storage_pages=64;
    CettaDurableStore *s=open_store(path,&l);
    char data[12*1024]; memset(data,'x',sizeof(data));
    /* Each growth batch compacts. Stop when copying the next checkpoint no
     * longer fits at the page cap; the deletion must still make progress. */
    unsigned inserted=0;
    for (;inserted<100;++inserted) {
        char key[32]; snprintf(key,sizeof(key),"%u",inserted);
        CettaDurableOp op={DURABLE_INSERT,"data",key,data,sizeof(data)};
        CettaDurableSnapshot v=snapshot(s); int64_t next;
        CettaDurableStatus r=cetta_durable_commit(s,v.epoch,v.revision,&op,1,&next);
        cetta_durable_snapshot_free(&v);
        if (r==DURABLE_LIMIT) break;
        check(r,DURABLE_OK);
    }
    assert(inserted>1 && inserted<100);
    /* Lower history budget to ensure every removal takes the reserve path. */
    cetta_durable_close(s); l.history_bytes=256; l.batch_bytes=l.record_bytes=1;
    s=open_store(path,&l);
    for (unsigned i=0;i<inserted;++i) {
        char key[32]; snprintf(key,sizeof(key),"%u",i);
        CettaDurableOp remove=put(DURABLE_REMOVE,"data",key,NULL); commit(s,&remove,1);
    }
    replay_equal(s); CettaDurableSnapshot v=snapshot(s); assert(!v.count);
    cetta_durable_snapshot_free(&v); check(cetta_durable_checkpoint(s),DURABLE_OK); cetta_durable_close(s);
}

static CettaDurableObservation *observe(CettaDurableStore *s, const CettaDurableScope *q, size_t n) {
    CettaDurableObservation *o=NULL; check(cetta_durable_observe(s,q,n,&o),DURABLE_OK); return o;
}

static void dependencies(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableOp op=put(DURABLE_INSERT,"state","chat:A","draft"); commit(s,&op,1);
    CettaDurableScope reads[]={ {DURABLE_KEY,"state","chat:A"},
        {DURABLE_PREFIX,"inbox","A:"}, {DURABLE_KEY,"outbox","send:A"} };
    CettaDurableObservation *o=observe(s,reads,3);
    assert(cetta_durable_observation_view(o,0)->count==1);
    assert(cetta_durable_observation_view(o,1)->count==0);
    CettaDurableWatch *watch=NULL;
    check(cetta_durable_watch_new(o,1,&watch),DURABLE_LIMIT); assert(!watch);
    check(cetta_durable_watch_new(o,4096,&watch),DURABLE_OK);
    size_t watch_bytes=cetta_durable_watch_bytes(watch); assert(watch_bytes>0 && watch_bytes<=4096);
    CettaDurableWatch *exact=NULL;
    check(cetta_durable_watch_new(o,watch_bytes-1,&exact),DURABLE_LIMIT);
    check(cetta_durable_watch_new(o,watch_bytes,&exact),DURABLE_OK); cetta_durable_watch_free(exact);
    /* Model a long decision while unrelated input and outcomes keep arriving. */
    for (int i=0;i<200;++i) {
        char key[32]; snprintf(key,sizeof(key),"B:%d",i);
        op=put(DURABLE_INSERT,i%2?"inbox":"outcomes",key,"event"); commit(s,&op,1);
    }
    CettaDurableOp changes[]={put(DURABLE_REPLACE,"state","chat:A","accepted"),
        put(DURABLE_INSERT,"outbox","send:A","reply")};
    int64_t next;
    check(cetta_durable_watch_current(s,watch),DURABLE_OK);
    check(cetta_durable_commit_observed(s,o,changes,2,&next),DURABLE_OK); assert(next==202);
    check(cetta_durable_watch_current(s,watch),DURABLE_CONFLICT); cetta_durable_watch_free(watch);
    check(cetta_durable_commit_observed(s,o,changes,2,&next),DURABLE_CONFLICT);
    cetta_durable_observation_free(o); o=observe(s,reads,3);
    check(cetta_durable_watch_new(o,4096,&watch),DURABLE_OK);
    op=put(DURABLE_INSERT,"inbox","A:1","new human input"); commit(s,&op,1);
    check(cetta_durable_watch_current(s,watch),DURABLE_CONFLICT); cetta_durable_watch_free(watch);
    check(cetta_durable_commit_observed(s,o,changes,1,&next),DURABLE_CONFLICT);
    cetta_durable_observation_free(o); o=observe(s,reads,3);
    op=put(DURABLE_REMOVE,"state","chat:A",NULL); commit(s,&op,1);
    op=put(DURABLE_INSERT,"state","chat:A","accepted"); commit(s,&op,1);
    check(cetta_durable_commit_observed(s,o,changes,1,&next),DURABLE_CONFLICT); /* ABA */
    cetta_durable_observation_free(o);
    CettaDurableScope all={DURABLE_SPACE,"inbox",NULL}; o=observe(s,&all,1);
    assert(cetta_durable_observation_view(o,0)->count==101);
    check(cetta_durable_commit_observed(s,o,changes,1,&next),DURABLE_INVALID); /* undeclared write */
    op=put(DURABLE_REMOVE,"inbox","A:1",NULL); commit(s,&op,1);
    check(cetta_durable_commit_observed(s,o,&op,1,&next),DURABLE_CONFLICT);
    cetta_durable_observation_free(o);
    CettaDurableScope prefix={DURABLE_PREFIX,"inbox","B:1"}; o=observe(s,&prefix,1);
    assert(cetta_durable_observation_view(o,0)->count==56); /* 1,11..19,101..199 odd */
    cetta_durable_observation_free(o);
    prefix.key=""; o=observe(s,&prefix,1); assert(cetta_durable_observation_view(o,0)->count==100);
    check(cetta_durable_watch_new(o,16384,&watch),DURABLE_OK);
    cetta_durable_observation_free(o); replay_equal(s); cetta_durable_close(s);
    s=open_store(path,NULL); check(cetta_durable_watch_current(s,watch),DURABLE_OK); cetta_durable_close(s);
    char other[512]; snprintf(other,sizeof(other),"%s.other",path);
    s=open_store(other,NULL); check(cetta_durable_watch_current(s,watch),DURABLE_CONFLICT);
    cetta_durable_close(s); unlink(other);
    char lock[600]; snprintf(lock,sizeof(lock),"%s.lock",other); unlink(lock); cetta_durable_watch_free(watch);
}

static void coherent_read_mode(const char *path, bool recover) {
    CettaDurableStore *s=open_store(path,NULL);
    concurrent_writer=s; CettaDurableSnapshot v;
    check(recover ? cetta_durable_recover(s,&v) : cetta_durable_snapshot(s,NULL,&v),DURABLE_OK);
    assert(!concurrent_writer && v.revision==0 && v.count==0);
    assert(!pthread_join(writer_thread,NULL));
    cetta_durable_snapshot_free(&v);
    v=snapshot(s); assert(v.revision==1 && v.count==1);
    cetta_durable_snapshot_free(&v); replay_equal(s);
    cetta_durable_close(s);
}

static void coherent_read(const char *path) { coherent_read_mode(path,false); }
static void coherent_recover(const char *path) { coherent_read_mode(path,true); }

static void joined_dependencies(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableOp policy=put(DURABLE_INSERT,"policy","grant","allow"); commit(s,&policy,1);
    CettaDurableScope dependencies[]={{DURABLE_KEY,"policy","grant"},{DURABLE_PREFIX,"policy","deny/"}};
    CettaDurableScope output={DURABLE_KEY,"state","result"};
    CettaDurableObservation *required=observe(s,dependencies,2), *joined=NULL;
    check(cetta_durable_observe_joined(s,required,&output,1,&joined),DURABLE_OK);
    assert(!cetta_durable_observation_view(joined,0)->count &&
        cetta_durable_observation_view(joined,1)->count==1 &&
        !cetta_durable_observation_view(joined,2)->count && !cetta_durable_observation_view(joined,3));
    cetta_durable_observation_free(required); /* the joined owner is independent */
    CettaDurableOp other=put(DURABLE_INSERT,"policy","other","independent"); commit(s,&other,1);
    CettaDurableOp result=put(DURABLE_INSERT,"state","result","accepted"); int64_t next;
    check(cetta_durable_commit_observed(s,joined,&result,1,&next),DURABLE_OK);
    cetta_durable_observation_free(joined);

    required=observe(s,dependencies,2);
    check(cetta_durable_observe_joined(s,required,NULL,0,&joined),DURABLE_OK);
    assert(cetta_durable_observation_view(joined,0)->count==1 &&
        !cetta_durable_observation_view(joined,1)->count);
    CettaDurableWatch *watch=NULL; check(cetta_durable_watch_new(joined,4096,&watch),DURABLE_OK);
    cetta_durable_observation_free(joined);
    policy=put(DURABLE_REPLACE,"policy","grant","deny"); commit(s,&policy,1);
    joined=(void *)1;
    check(cetta_durable_observe_joined(s,required,&output,1,&joined),DURABLE_CONFLICT); assert(!joined);
    check(cetta_durable_watch_current(s,watch),DURABLE_CONFLICT);
    policy=put(DURABLE_REPLACE,"policy","grant","allow"); commit(s,&policy,1);
    check(cetta_durable_observe_joined(s,required,&output,1,&joined),DURABLE_CONFLICT); assert(!joined);
    cetta_durable_watch_free(watch); cetta_durable_observation_free(required);

    required=observe(s,dependencies,2);
    check(cetta_durable_observe_joined(s,required,&output,1,&joined),DURABLE_OK);
    CettaDurableOp denied=put(DURABLE_INSERT,"policy","deny/new","revoked"); commit(s,&denied,1);
    result=put(DURABLE_REPLACE,"state","result","must-not-appear");
    check(cetta_durable_commit_observed(s,joined,&result,1,&next),DURABLE_CONFLICT); assert(next==-1);
    CettaDurableSnapshot v=snapshot(s); value(&v,"state","result","accepted"); cetta_durable_snapshot_free(&v);
    cetta_durable_observation_free(joined); cetta_durable_observation_free(required);

    CettaDurableScope absent={DURABLE_KEY,"policy","absent"}; required=observe(s,&absent,1);
    char foreign[512]; snprintf(foreign,sizeof(foreign),"%s.foreign",path);
    CettaDurableStore *different=open_store(foreign,NULL);
    check(cetta_durable_observe_joined(different,required,&output,1,&joined),DURABLE_CONFLICT); assert(!joined);
    cetta_durable_close(different); assert(!unlink(foreign));
    char lock[600]; snprintf(lock,sizeof(lock),"%s.lock",foreign); unlink(lock);
    joined=(void *)1;
    check(cetta_durable_observe_joined(s,NULL,&output,1,&joined),DURABLE_INVALID); assert(!joined);
    cetta_durable_close(s);
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.operations=1; s=open_store(path,&limits);
    check(cetta_durable_observe_joined(s,required,&output,1,&joined),DURABLE_LIMIT); assert(!joined);
    cetta_durable_observation_free(required); cetta_durable_close(s);
}

static void coherent_join(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableScope absent={DURABLE_KEY,"state","one"};
    CettaDurableObservation *required=observe(s,&absent,1), *joined=NULL;
    concurrent_writer=s;
    check(cetta_durable_observe_joined(s,required,&absent,1,&joined),DURABLE_OK);
    assert(!concurrent_writer); assert(!pthread_join(writer_thread,NULL));
    for(size_t i=0;i<2;i++) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(joined,i);
        assert(v && !v->count && v->revision==0);
    }
    CettaDurableOp change=put(DURABLE_INSERT,"state","one","later"); int64_t next;
    check(cetta_durable_commit_observed(s,joined,&change,1,&next),DURABLE_CONFLICT);
    CettaDurableSnapshot v=snapshot(s); assert(v.revision==1 && v.count==1);
    cetta_durable_snapshot_free(&v); cetta_durable_observation_free(joined);
    cetta_durable_observation_free(required); cetta_durable_close(s);
}

static void physical_owner_aliases(const char *path) {
    CettaDurableStore *s=open_store(path,NULL),*alias_store=NULL;
    CettaDurableOp op=put(DURABLE_INSERT,"state","one","preserved"); commit(s,&op,1);
    char alias[600],lock[620],temporary[600];
    snprintf(alias,sizeof(alias),"%s.alias",path); snprintf(lock,sizeof(lock),"%s.lock",alias);
    assert(!link(path,alias));
    check(cetta_durable_open(alias,NULL,&alias_store),DURABLE_INVALID); assert(!alias_store);
    assert(!unlink(alias)); assert(!unlink(lock));
    snprintf(temporary,sizeof(temporary),"%s.checkpoint",path);
    assert(!link(path,temporary));
    check(cetta_durable_checkpoint(s),DURABLE_INVALID);
    CettaDurableSnapshot v=snapshot(s); value(&v,"state","one","preserved");
    replay_equal(s); assert(!unlink(temporary));
    check(cetta_durable_checkpoint(s),DURABLE_OK);
    cetta_durable_snapshot_free(&v); cetta_durable_close(s);
    s=open_store(path,NULL); v=snapshot(s); value(&v,"state","one","preserved");
    cetta_durable_snapshot_free(&v); cetta_durable_close(s);
}

static void semantic_corruption(const char *path) {
    CettaDurableStore *s=open_store(path,NULL);
    CettaDurableOp ops[]={put(DURABLE_INSERT,"state","a","x"),put(DURABLE_INSERT,"state","b","y")};
    commit(s,ops,2); cetta_durable_close(s);
    int fd=open(path,O_RDWR); assert(fd>=0); unsigned char *header=NULL,*batch=NULL;
    size_t hs=0,bs=0; off_t next=0,end=0;
    assert(cetta_native_journal_read(fd,0,0,&header,&hs,&next)==CETTA_JOURNAL_OK);
    assert(cetta_native_journal_read(fd,next,0,&batch,&bs,&end)==CETTA_JOURNAL_OK);
    /* Independently re-frame a correct-checksum but repeated key. The second
     * row cannot acquire a new occurrence merely by changing its position. */
    assert(bs==53+2*(29+5+1+1));
    unsigned char *forged=malloc(bs); assert(forged); memcpy(forged,batch,bs);
    size_t second=53+29+5+1+1; forged[second]=DURABLE_REPLACE;
    forged[second+29+5]='a';
    assert(!ftruncate(fd,0) && lseek(fd,0,SEEK_SET)==0);
    assert(cetta_native_journal_append(fd,header,hs,false,NULL)==CETTA_JOURNAL_OK);
    assert(cetta_native_journal_append(fd,forged,bs,true,NULL)==CETTA_JOURNAL_OK);
    assert(!close(fd)); check(cetta_durable_open(path,NULL,&s),DURABLE_CORRUPT); assert(!s);
    /* A discontinuous revision is corrupt despite an authentic frame digest. */
    memcpy(forged,batch,bs); forged[44]=3;
    fd=open(path,O_RDWR|O_TRUNC); assert(fd>=0);
    assert(cetta_native_journal_append(fd,header,hs,false,NULL)==CETTA_JOURNAL_OK);
    assert(cetta_native_journal_append(fd,forged,bs,true,NULL)==CETTA_JOURNAL_OK);
    assert(!close(fd)); check(cetta_durable_open(path,NULL,&s),DURABLE_CORRUPT); assert(!s);
    free(header); free(batch); free(forged);
}

static void partial_append(const char *path) {
    for (int64_t allowance=0;allowance<173;++allowance) {
        CettaDurableStore *s=open_store(path,NULL);
        CettaDurableSnapshot old=snapshot(s); CettaDurableOp op=put(DURABLE_INSERT,"state","partial","payload");
        int64_t next=0; write_allowance=allowance;
        check(cetta_durable_commit(s,old.epoch,old.revision,&op,1,&next),DURABLE_UNKNOWN);
        assert(next==-1); write_allowance=-1;
        CettaDurableSnapshot poisoned; check(cetta_durable_snapshot(s,NULL,&poisoned),DURABLE_POISONED);
        cetta_durable_close(s); s=open_store(path,NULL);
        CettaDurableSnapshot now=snapshot(s); assert(!now.count && now.revision==old.revision);
        assert(!strcmp(now.epoch,old.epoch)); replay_equal(s);
        cetta_durable_snapshot_free(&old); cetta_durable_snapshot_free(&now); cetta_durable_close(s);
    }
}

static void unlimited_and_binary(const char *path) {
    CettaDurableStore *s=open_store(path,NULL); size_t size=2u*1024u*1024u;
    unsigned char *data=malloc(size); assert(data);
    for (size_t i=0;i<size;++i) data[i]=(unsigned char)i;
    char long_key[4097]; memset(long_key,'k',sizeof(long_key)-1); long_key[sizeof(long_key)-1]=0;
    CettaDurableOp ops[]={{DURABLE_INSERT,"binary","a",data,size},{DURABLE_INSERT,"binary","b",data,size},
        {DURABLE_INSERT,long_key,long_key,data,size}};
    commit(s,ops,3); check(cetta_durable_checkpoint(s),DURABLE_OK);
    CettaDurableStore *second=NULL; check(cetta_durable_open(path,NULL,&second),DURABLE_BUSY); assert(!second);
    cetta_durable_close(s); s=open_store(path,NULL);
    CettaDurableSnapshot v=snapshot(s); assert(v.count==3);
    CettaDurableScope query={DURABLE_KEY,long_key,long_key};
    CettaDurableObservation *seen=observe(s,&query,1);
    assert(cetta_durable_observation_view(seen,0)->count==1); cetta_durable_observation_free(seen);
    for(size_t i=0;i<v.count;++i) assert(v.records[i].size==size && !memcmp(v.records[i].data,data,size));
    CettaDurableLimits l=cetta_durable_default_limits(); l.record_bytes=1024;
    cetta_durable_close(s); s=open_store(path,&l); int64_t next;
    CettaDurableOp denied={DURABLE_REPLACE,"binary","a",data,size};
    check(cetta_durable_commit(s,v.epoch,v.revision,&denied,1,&next),DURABLE_LIMIT);
    CettaDurableSnapshot now=snapshot(s); assert(now.revision==v.revision); replay_equal(s);
    cetta_durable_snapshot_free(&now); cetta_durable_snapshot_free(&v); cetta_durable_close(s); free(data);
}

int main(void) {
    char dir[]="/tmp/cetta-durable-test-XXXXXX"; assert(mkdtemp(dir));
    char path[256];
    const char *names[]={"basic","crash","limits","full","version","concurrent","ioerr","degraded","mismatch","reserve","dependencies","coherent","coherent_recover","joined","coherent_join","partial","binary","semantic","aliases"};
    void (*tests[])(const char *)={basic,crashes,limits,disk_full,wrong_version,concurrent,commit_ioerr,degraded,recovery_mismatch,removal_reserve,dependencies,coherent_read,coherent_recover,joined_dependencies,coherent_join,partial_append,unlimited_and_binary,semantic_corruption,physical_owner_aliases};
    for (size_t i=0;i<sizeof(tests)/sizeof(*tests);++i) {
        snprintf(path,sizeof(path),"%s/%s.db",dir,names[i]); tests[i](path);
        assert(unlink(path)==0);
        char sidecar[300]; snprintf(sidecar,sizeof(sidecar),"%s.lock",path); unlink(sidecar);
        snprintf(sidecar,sizeof(sidecar),"%s.checkpoint",path); unlink(sidecar);
    }
    assert(rmdir(dir)==0);
    puts("durable store: atomic transitions, conflicts, checkpoint replay, four crash boundaries, disk full, limits, versions, concurrent decisions, fsync poisoning, degraded recovery, consistency checks, deletion reserve, scoped decisions, 173 partial appends, unlimited binary values/long names and checkpoint lock identity and forged-frame/physical-alias rejection passed");
    return 0;
}
