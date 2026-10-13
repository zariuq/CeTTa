#define _POSIX_C_SOURCE 200809L
#include "durable_inbox.h"
#include "durable_value.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static CettaDurableStore *store;
static Arena arena;
static CettaInboxWindow *begin(const char *source, int64_t expected) {
    CettaInboxWindow *w=NULL;
    assert(cetta_inbox_begin(store,source,&w)==DURABLE_OK && cetta_inbox_offset(w)==expected);
    return w;
}
static size_t count(const char *space) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,space,&s)==DURABLE_OK);
    size_t n=s.count; cetta_durable_snapshot_free(&s); return n;
}
static Atom *get(const char *space, const char *key) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0); Atom *a=NULL;
    if (v->count) assert(cetta_durable_value_decode(&arena,v->records[0].data,v->records[0].size,&a)==DURABLE_OK);
    cetta_durable_observation_free(o); return a;
}
static void remove_input(const char *key) {
    CettaDurableScope q={DURABLE_KEY,"host.inbox",key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    CettaDurableOp op={DURABLE_REMOVE,"host.inbox",key,NULL,0}; int64_t revision;
    assert(cetta_durable_commit_observed(store,o,&op,1,&revision)==DURABLE_OK);
    cetta_durable_observation_free(o);
}
static void empty(const char *path) {
    assert(!unlink(path));
    char sidecar[300]; snprintf(sidecar,sizeof(sidecar),"%s-wal",path); unlink(sidecar);
    snprintf(sidecar,sizeof(sidecar),"%s-shm",path); unlink(sidecar);
}
int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    arena_init(&arena);
    CettaInboxItem items[]={
        {43,"unsupported",INBOX_UNSUPPORTED,atom_string(&arena,"unknown update kind, retained")},
        {7,"chat-1",INBOX_ROUTED,atom_string(&arena,"first")},
        {7,"chat-1",INBOX_ROUTED,atom_string(&arena,"first")},
        {42,"unauthorized",INBOX_UNAUTHORIZED,atom_string(&arena,"blocked by allowlist, retained")}
    };
    CettaInboxCommit committed;
    if (argc==3) {
        assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
        CettaInboxWindow *w=begin("crash-bot",0);
        if (!strcmp(argv[1],"after")) assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_OK);
        else assert(!strcmp(argv[1],"before"));
        _exit(0); /* No close, response or acknowledgment. */
    }
    assert(argc==1);
    char directory[]="/tmp/cetta-inbox-XXXXXX"; assert(mkdtemp(directory));
    char path[256]; snprintf(path,sizeof(path),"%s/journal.db",directory);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    CettaInboxWindow *bad=(void *)1;
    assert(cetta_inbox_begin(store,"bot/escape",&bad)==DURABLE_INVALID && !bad);
    CettaInboxWindow *w=begin("bot",0);
    assert(cetta_inbox_commit(w,NULL,0,&committed)==DURABLE_OK && committed.next_offset==0 && committed.revision<0);
    assert(cetta_inbox_commit(w,NULL,0,&committed)==DURABLE_CONFLICT && committed.next_offset<0);
    assert(count("host.cursors")==0); cetta_inbox_window_free(w);
    w=begin("bot",0);
    assert(cetta_inbox_commit(w,items,101,&committed)==DURABLE_LIMIT);
    assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_OK && committed.inserted==3 && committed.next_offset==44);
    assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_CONFLICT && committed.next_offset<0);
    cetta_inbox_window_free(w);
    assert(count("host.received")==3 && count("host.inbox")==3 && count("host.cursors")==1);
    CettaDurableSnapshot all; assert(cetta_durable_snapshot(store,NULL,&all)==DURABLE_OK);
    for (size_t i=0;i<all.count;++i) assert(all.records[i].revision==all.revision);
    cetta_durable_snapshot_free(&all);
    assert(atom_is_symbol(get("host.received","bot/43")->expr.elems[5],"unsupported"));
    assert(atom_is_symbol(get("host.received","bot/42")->expr.elems[5],"unauthorized"));
    remove_input("bot/chat-1/7");
    w=begin("bot",44);
    assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_OK && !committed.inserted && committed.next_offset==44);
    assert(!get("host.inbox","bot/chat-1/7") && count("host.received")==3);
    cetta_inbox_window_free(w);
    /* Conflicting identity must not advance the cursor or partially add 48. */
    CettaInboxItem conflict[]={
        {48,"chat-1",INBOX_ROUTED,atom_string(&arena,"new")},
        {43,"unsupported",INBOX_UNSUPPORTED,atom_string(&arena,"different")}
    };
    w=begin("bot",44);
    assert(cetta_inbox_commit(w,conflict,2,&committed)==DURABLE_CORRUPT && committed.next_offset<0);
    assert(!get("host.received","bot/48"));
    conflict[1]=items[0];
    assert(cetta_inbox_commit(w,conflict,2,&committed)==DURABLE_OK && committed.next_offset==49 && committed.inserted==1);
    cetta_inbox_window_free(w);
    /* Concurrent polls use the same committed offset; only one window wins. */
    w=begin("bot",49); CettaInboxWindow *stale=begin("bot",49);
    CettaInboxWindow *traffic=begin("unrelated",0);
    assert(cetta_inbox_commit(traffic,&items[1],1,&committed)==DURABLE_OK);
    cetta_inbox_window_free(traffic);
    conflict[0].id=49;
    assert(cetta_inbox_commit(w,conflict,1,&committed)==DURABLE_OK && committed.next_offset==50);
    conflict[0].id=60;
    assert(cetta_inbox_commit(stale,conflict,1,&committed)==DURABLE_CONFLICT && committed.next_offset<0);
    assert(!get("host.received","bot/60"));
    cetta_inbox_window_free(w); cetta_inbox_window_free(stale);
    /* A distinct lower ID is preserved; the cursor never moves backward. */
    w=begin("bot",50); conflict[0].id=6;
    assert(cetta_inbox_commit(w,conflict,1,&committed)==DURABLE_OK && committed.next_offset==50 && committed.inserted==1);
    cetta_inbox_window_free(w);
    w=begin("bot-b",0);
    assert(cetta_inbox_commit(w,&items[1],1,&committed)==DURABLE_OK && committed.inserted==1 && committed.next_offset==8);
    cetta_inbox_window_free(w);
    /* A late invalid item must not return success after encoding a valid one. */
    w=begin("bot",50); conflict[0].id=61; conflict[1]=items[1]; conflict[1].lane="bad/lane";
    assert(cetta_inbox_commit(w,conflict,2,&committed)==DURABLE_INVALID && committed.next_offset<0);
    assert(!get("host.received","bot/61"));
    conflict[1]=conflict[0]; conflict[1].value=atom_string(&arena,"inconsistent duplicate");
    assert(cetta_inbox_commit(w,conflict,2,&committed)==DURABLE_CORRUPT);
    conflict[0].id=INT64_MAX;
    assert(cetta_inbox_commit(w,conflict,1,&committed)==DURABLE_INVALID);
    conflict[0].id=61; conflict[0].value=atom_var(&arena,"not-closed");
    assert(cetta_inbox_commit(w,conflict,1,&committed)==DURABLE_INVALID);
    cetta_inbox_window_free(w);
    /* Poll bytes become durable before classification. Neither a stale direct
     * window nor a second poll may bypass the pending-response slot. */
    w=begin("poll-bot",0); stale=begin("poll-bot",0);
    unsigned char *raw=NULL; size_t raw_size=0;
    assert(cetta_durable_value_encode(atom_string(&arena,"screened response"),&raw,&raw_size)==DURABLE_OK);
    assert(cetta_inbox_record_poll(w,raw,raw_size)==DURABLE_OK);
    assert(cetta_inbox_record_poll(w,raw,raw_size)==DURABLE_CONFLICT);
    assert(cetta_inbox_record_poll(stale,raw,raw_size)==DURABLE_CONFLICT);
    assert(cetta_inbox_commit(stale,items,1,&committed)==DURABLE_CONFLICT);
    cetta_inbox_window_free(w); cetta_inbox_window_free(stale);
    assert(cetta_inbox_begin(store,"poll-bot",&bad)==DURABLE_PRECONDITION && !bad);
    assert(cetta_inbox_recover_poll(store,"poll-bot",&w)==DURABLE_OK);
    const CettaDurableRecord *response=cetta_inbox_poll_response(w);
    assert(response && response->size==raw_size && !memcmp(response->data,raw,raw_size));
    /* A replacement of the response alone invalidates classification even
     * though its source cursor and the bytes happen to be unchanged. */
    CettaDurableSnapshot snapshot;
    assert(cetta_durable_snapshot(store,"host.polls",&snapshot)==DURABLE_OK);
    CettaDurableOp replace={DURABLE_REPLACE,"host.polls","poll-bot",raw,raw_size}; int64_t rev;
    assert(cetta_durable_commit(store,snapshot.epoch,snapshot.revision,&replace,1,&rev)==DURABLE_OK);
    cetta_durable_snapshot_free(&snapshot); free(raw);
    assert(cetta_inbox_commit(w,NULL,0,&committed)==DURABLE_CONFLICT && committed.next_offset<0);
    cetta_inbox_window_free(w);
    assert(cetta_inbox_recover_poll(store,"poll-bot",&w)==DURABLE_OK);
    assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_OK && committed.next_offset==44);
    cetta_inbox_window_free(w);
    assert(!get("host.polls","poll-bot"));
    assert(cetta_durable_checkpoint(store)==DURABLE_OK); cetta_durable_close(store);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    w=begin("bot",50); cetta_inbox_window_free(w);
    assert(!get("host.inbox","bot/chat-1/7"));
    cetta_durable_close(store);
    for (unsigned i=0;i<2;++i) {
        pid_t pid=fork(); assert(pid>=0);
        if (!pid) { execl(argv[0],argv[0],i?"after":"before",path,(char *)NULL); _exit(127); }
        int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
        w=begin("crash-bot",i?44:0); cetta_inbox_window_free(w);
        assert((get("host.received","crash-bot/7")!=NULL)==(i!=0));
        cetta_durable_close(store);
    }
    empty(path);
    /* Cursor write is first in the transaction. A later capacity failure must
     * roll it back together with every occurrence and reference. */
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.records=3;
    assert(cetta_durable_open(path,&limits,&store)==DURABLE_OK);
    w=begin("limited",0);
    assert(cetta_inbox_commit(w,items,4,&committed)==DURABLE_LIMIT && committed.next_offset<0 && count(NULL)==0);
    cetta_inbox_window_free(w); w=begin("limited",0);
    assert(cetta_inbox_commit(w,&items[1],1,&committed)==DURABLE_OK && committed.next_offset==8);
    cetta_inbox_window_free(w); w=begin("limited",8);
    assert(cetta_inbox_commit(w,&items[0],1,&committed)==DURABLE_LIMIT && committed.next_offset<0);
    cetta_inbox_window_free(w); w=begin("limited",8); cetta_inbox_window_free(w);
    assert(!get("host.received","limited/43"));
    cetta_durable_close(store); empty(path); assert(!rmdir(directory));
    arena_free(&arena); symbol_table_free(&symbols); g_symbols=NULL;
    puts("durable inbox: atomic cursor/occurrences, gaps/order, duplicates after consumption, classification, stale windows, rollback and crash recovery passed");
}
