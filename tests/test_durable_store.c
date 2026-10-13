#define _POSIX_C_SOURCE 200809L
#include "durable_store.h"
#include <sqlite3.h>
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
static sqlite3 *concurrent_writer;
void cetta_durable_test_boundary(const char *boundary) {
    if (concurrent_writer && !strcmp(boundary,"read_meta")) {
        sqlite3 *db=concurrent_writer; concurrent_writer=NULL;
        assert(sqlite3_exec(db,
            "BEGIN IMMEDIATE; INSERT INTO records VALUES('state','one',x'78',1,0);"
            "INSERT INTO deltas VALUES(1,0,1,'state','one',x'78');"
            "UPDATE meta SET revision=1,live_bytes=9,live_count=1,history_bytes=9; COMMIT;",
            NULL,NULL,NULL)==SQLITE_OK);
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
    CettaDurableLimits l=cetta_durable_default_limits(); l.database_pages=64;
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
    sqlite3 *db=NULL; assert(sqlite3_open(path,&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"PRAGMA user_version=999",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_close(db); CettaDurableStore *s=NULL;
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


/* A VFS interposer fails the actual WAL xSync at COMMIT, not a simulated
 * return from the store. All other methods still use SQLite's normal VFS. */
static sqlite3_vfs fault_vfs;
static sqlite3_vfs *base_vfs;
static sqlite3_io_methods fault_methods;
static const sqlite3_io_methods *base_methods;
static bool fail_sync;
static unsigned sync_failures;
static int fault_sync(sqlite3_file *f, int flags) {
    if (fail_sync) { ++sync_failures; return SQLITE_IOERR_FSYNC; }
    return base_methods->xSync(f,flags);
}
static int fault_open(sqlite3_vfs *v, sqlite3_filename name, sqlite3_file *f, int flags, int *out) {
    (void)v;
    int rc=base_vfs->xOpen(base_vfs,name,f,flags,out);
    if (rc==SQLITE_OK && (flags&SQLITE_OPEN_WAL)) {
        base_methods=f->pMethods; fault_methods=*base_methods;
        fault_methods.xSync=fault_sync; f->pMethods=&fault_methods;
    }
    return rc;
}

static void commit_ioerr(const char *path) {
    base_vfs=sqlite3_vfs_find(NULL); assert(base_vfs);
    fault_vfs=*base_vfs; fault_vfs.zName="durable-test-fsync"; fault_vfs.xOpen=fault_open;
    assert(sqlite3_vfs_register(&fault_vfs,1)==SQLITE_OK);
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
    assert(sqlite3_vfs_register(base_vfs,1)==SQLITE_OK);
    assert(sqlite3_vfs_unregister(&fault_vfs)==SQLITE_OK);
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
    sqlite3 *db=NULL; assert(sqlite3_open(path,&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"UPDATE records SET value=x'636f727275707421'",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_close(db);
    check(cetta_durable_open(path,NULL,&s),DURABLE_CORRUPT); assert(!s);
}

static void removal_reserve(const char *path) {
    CettaDurableLimits l=cetta_durable_default_limits();
    l.record_bytes=12*1024; l.batch_bytes=13*1024; l.history_bytes=13*1024; l.database_pages=64;
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
    cetta_durable_close(s); unlink(other); cetta_durable_watch_free(watch);
}

static void coherent_read_mode(const char *path, bool recover) {
    CettaDurableStore *s=open_store(path,NULL);
    sqlite3 *writer=NULL; assert(sqlite3_open(path,&writer)==SQLITE_OK);
    concurrent_writer=writer; CettaDurableSnapshot v;
    check(recover ? cetta_durable_recover(s,&v) : cetta_durable_snapshot(s,NULL,&v),DURABLE_OK);
    assert(!concurrent_writer && v.revision==0 && v.count==0);
    cetta_durable_snapshot_free(&v);
    v=snapshot(s); assert(v.revision==1 && v.count==1);
    cetta_durable_snapshot_free(&v); replay_equal(s);
    sqlite3_close(writer); cetta_durable_close(s);
}

static void coherent_read(const char *path) { coherent_read_mode(path,false); }
static void coherent_recover(const char *path) { coherent_read_mode(path,true); }

int main(void) {
    char dir[]="/tmp/cetta-durable-test-XXXXXX"; assert(mkdtemp(dir));
    char path[256];
    const char *names[]={"basic","crash","limits","full","version","concurrent","ioerr","degraded","mismatch","reserve","dependencies","coherent","coherent_recover"};
    void (*tests[])(const char *)={basic,crashes,limits,disk_full,wrong_version,concurrent,commit_ioerr,degraded,recovery_mismatch,removal_reserve,dependencies,coherent_read,coherent_recover};
    for (size_t i=0;i<sizeof(tests)/sizeof(*tests);++i) {
        snprintf(path,sizeof(path),"%s/%s.db",dir,names[i]); tests[i](path);
        assert(unlink(path)==0);
        char sidecar[300]; snprintf(sidecar,sizeof(sidecar),"%s-wal",path); unlink(sidecar);
        snprintf(sidecar,sizeof(sidecar),"%s-shm",path); unlink(sidecar);
    }
    assert(rmdir(dir)==0);
    puts("durable store: atomic transitions, conflicts, checkpoint replay, four crash boundaries, disk full, limits, versions, concurrent decisions, fsync poisoning, degraded recovery, consistency checks, deletion reserve, scoped decisions passed");
    return 0;
}
