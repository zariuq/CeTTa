#define _GNU_SOURCE
#include "durable_worker_host.h"
#include "durable_host.h"
#include "durable_value.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static CettaHostProgram trusted={"worker-test/1",&program,&context};
static Atom *parse(const char *s) {
    size_t pos=0; Atom *a=parse_sexpr(&persistent,s,&pos); assert(a && pos==strlen(s)); return a;
}
static void put_value(const char *space, const char *key, const Atom *value) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    unsigned char *data; size_t size; int64_t rev;
    assert(cetta_durable_value_encode(value,&data,&size)==DURABLE_OK);
    CettaDurableOp op={cetta_durable_observation_view(o,0)->count?DURABLE_REPLACE:DURABLE_INSERT,space,key,data,size};
    assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK);
    free(data); cetta_durable_observation_free(o);
}
static void put(const char *space, const char *key, const char *value) { put_value(space,key,parse(value)); }
static size_t count(const char *space) {
    CettaDurableSnapshot v; assert(cetta_durable_snapshot(store,space,&v)==DURABLE_OK);
    size_t n=v.count; cetta_durable_snapshot_free(&v); return n;
}
static bool allowed_send(void *unused, const Atom *payload, const Atom *reply) {
    (void)unused; (void)reply;
    return payload->kind==ATOM_EXPR && payload->expr.len==3 &&
        atom_is_symbol(payload->expr.elems[0],"sendMessage") &&
        payload->expr.elems[1]->kind==ATOM_GROUNDED && payload->expr.elems[1]->ground.gkind==GV_STRING &&
        !strcmp(payload->expr.elems[1]->ground.sval,"allowed") &&
        payload->expr.elems[2]->kind==ATOM_GROUNDED && payload->expr.elems[2]->ground.gkind==GV_STRING;
}
static void accept_request(char outbox[64]) {
    CettaHostSpaceGrant state={{DURABLE_KEY,"state","route"},false};
    CettaHostChannelGrant channel={"worker.request","1",cetta_worker_validate,"brain"};
    CettaHostDecisionSpec spec={"chat/1","actor",trusted.version,&state,1,&channel,1};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    assert(cetta_host_evaluate(d,&trusted,parse("(prepare)"),10000)==DURABLE_OK);
    CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_OK && c.effects==1);
    snprintf(outbox,64,"%s/0",c.commit_key); cetta_host_decision_free(d);
}
static void remove_input(const char *key) {
    CettaDurableScope q={DURABLE_KEY,"host.inbox",key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    CettaDurableOp op={DURABLE_REMOVE,"host.inbox",key,NULL,0}; int64_t rev;
    assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK);
    cetta_durable_observation_free(o);
}
static void rpc(CettaWorkerEndpoint *e, int fd, int code, const char *id, const char *body,
                int expected, const char *reply_body) {
    unsigned char packet[CETTA_WORKER_PACKET_MAX]; size_t n=strlen(id), bytes=strlen(body);
    memcpy(packet,"CWP1",4); packet[4]=(unsigned char)code; packet[5]=(unsigned char)n;
    memcpy(packet+6,id,n); memcpy(packet+6+n,body,bytes);
    assert(send(fd,packet,6+n+bytes,MSG_NOSIGNAL)==(ssize_t)(6+n+bytes));
    ssize_t got=-1;
    for (unsigned i=0;i<1000;++i) {
        size_t requests; assert(cetta_worker_endpoint_step(e,1,&requests)==DURABLE_OK);
        got=recv(fd,packet,sizeof(packet),MSG_DONTWAIT);
        if (got>=0) break;
        assert(errno==EAGAIN || errno==EWOULDBLOCK);
    }
    size_t answer=strlen(reply_body);
    assert(got>=6 && packet[4]==expected && (size_t)got==6u+packet[5]+answer);
    assert(!memcmp(packet+6+packet[5],reply_body,answer));
    if (*id) assert(packet[5]==n && !memcmp(packet+6,id,n));
}
static CettaHostDecision *resume(const char *id, const char *outbox) {
    char key[130], input[140]; snprintf(key,sizeof(key),"brain/%s",id);
    snprintf(input,sizeof(input),"worker/%s",key);
    CettaHostSpaceGrant scopes[]={
        {{DURABLE_KEY,"host.worker-tasks",key},false},
        {{DURABLE_KEY,"host.worker-origins",key},false},
        {{DURABLE_KEY,"host.outbox",outbox},false},
        {{DURABLE_KEY,"state","route"},false},
        {{DURABLE_PREFIX,"host.inbox","chat/"},false}};
    CettaHostChannelGrant channel={"telegram.send","1",allowed_send,NULL};
    CettaHostDecisionSpec spec={input,"actor",trusted.version,scopes,5,&channel,1};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    assert(cetta_host_evaluate(d,&trusted,parse("(resume)"),10000)==DURABLE_OK);
    const EvalOutcome *o=cetta_host_outcome(d);
    if (o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials || o->results.len!=1) {
        fprintf(stderr,"resume completion=%d denials=%zu results=%zu\n",o->completion,
            (size_t)o->effect_denials,(size_t)o->results.len);
        for (size_t i=0;i<o->results.len;++i) { atom_print(o->results.items[i],stderr); fputc('\n',stderr); }
    }
    assert(o->completion==CETTA_EVAL_COMPLETE && !o->effect_denials && o->results.len==1);
    return d;
}
int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    assert(he_library_tables_init());
    stdlib_load(&program,&persistent);
    assert(telegram_he_prepare_library(&program,&persistent,true));
    assert(space_mark_imported_prefix(&program));
    Atom *error=NULL;
    assert(cetta_library_import_module(&context,"durable:worker",&program,false,&scratch,&persistent,&registry,10000,NULL,&error));
    if (argc==4) {
        assert(!strcmp(argv[1],"--register"));
        assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
        char id[65]; assert(cetta_worker_register(store,argv[3],"brain",id)==DURABLE_OK); _exit(0);
    }
    assert(argc==1);
    /* Projected records belong to this decision's context. &self inside an
     * authored equation is its lexical module, not the new snapshot. Atom
     * parameters keep the match template unevaluated until its variables bind. */
    space_add(&program,parse("(: test:match (-> Atom Atom %Undefined%))"));
    space_add(&program,parse("(= (test:match $pattern $template) (chain (context-space) $space (match $space $pattern $template)))"));
    space_add(&program,parse("(= (prepare) "
        "(test:match (host:record 0 \"chat/1\" (request $observation)) "
        "(test:match (host:record-version 2 \"route\" $revision $position) "
        "(host:transition (waiting \"chat/1\" $revision) () "
        "((durable:worker:request 0 \"brain\" $observation (resume \"chat/1\")))))))"));
    /* All policy comes from this trusted program. Worker text only supplies
     * the eventual message string, after exact task/origin/continuation joins.
     * The route revision captured in the continuation detects an old draft. */
    space_add(&program,parse("(= (resume) "
        "(test:match (host:record 0 $input (host:worker-result 1 \"brain\" $id $taskrev $text)) "
        "(test:match (host:record-version 2 $taskkey $taskrev $taskpos) "
        "(test:match (host:record 3 $taskkey (host:worker-origin 1 $epoch $outkey $outrev $outpos)) "
        "(test:match (host:record-version 4 $outkey $outrev $outpos) "
        "(test:match (host:record 4 $outkey (host:intent 1 \"worker.request\" \"1\" "
            "(worker:request 1 \"brain\" $observation) (resume $token))) "
        "(test:match (host:record 2 $taskkey $observation) "
        "(test:match (host:record 1 \"actor\" (host:actor 1 \"worker-test/1\" (waiting $token $saved))) "
        "(test:match (host:record-version 5 \"route\" $current $position) "
        "(if (and (== $saved $current) (== (collapse (test:match (host:record 6 $key $value) True)) ())) "
            "(host:transition (done $text) () ((host:send 0 (sendMessage \"allowed\" $text) finished))) "
            "(host:transition (stale $token) () ())))))))))))"));
    assert(!cetta_worker_validate("brain",parse("(worker:request 1 \"other\" \"x\")"),parse("reply")));
    assert(!cetta_worker_validate("brain",parse("(worker:request 2 \"brain\" \"x\")"),parse("reply")));
    assert(!cetta_worker_validate("brain",parse("(worker:request 1 \"brain\" \"x\")"),parse("$reply")));
    char directory[]="/tmp/cetta-worker-host-XXXXXX"; assert(mkdtemp(directory));
    char db[256]; snprintf(db,sizeof(db),"%s/journal.db",directory);
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    char id[65], outbox[64];
    assert(cetta_worker_register(store,"absent","brain",id)==DURABLE_PRECONDITION && !*id);
    put("state","route","allowed"); put("host.inbox","chat/1","(request \"the exact observation\")");
    accept_request(outbox); assert(count("host.inbox")==0 && count("host.actors")==1 && !count("host.worker-tasks"));
    assert(cetta_worker_register(store,outbox,"other",id)==DURABLE_INVALID && !*id);
    /* Crash after acceptance but before publication. Then crash after publication
     * without returning to the caller. Both reopen paths derive the same task. */
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.records=(size_t)count(NULL)+2;
    cetta_durable_close(store); store=NULL;
    assert(cetta_durable_open(db,&limits,&store)==DURABLE_OK);
    assert(cetta_worker_register(store,outbox,"brain",id)==DURABLE_LIMIT && !*id);
    assert(!count("host.worker-tasks") && !count("host.worker-ready") && !count("host.worker-origins"));
    assert(count("host.outbox")==1 && count("host.actors")==1);
    cetta_durable_close(store); store=NULL;
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        execl(argv[0],argv[0],"--register",db,outbox,(char *)NULL); _exit(127);
    }
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    assert(cetta_worker_register(store,outbox,"brain",id)==DURABLE_OK && strlen(id)==64);
    assert(count("host.worker-tasks")==1 && count("host.worker-ready")==1 && count("host.worker-origins")==1);
    char again[65]; assert(cetta_worker_register(store,outbox,"brain",again)==DURABLE_OK && !strcmp(again,id));
    CettaDurableSnapshot origins; assert(cetta_durable_snapshot(store,"host.worker-origins",&origins)==DURABLE_OK);
    Atom *origin=NULL;
    assert(cetta_durable_value_decode(&persistent,origins.records[0].data,origins.records[0].size,&origin)==DURABLE_OK);
    put("host.worker-origins",origins.records[0].key,"corrupt");
    assert(cetta_worker_register(store,outbox,"brain",again)==DURABLE_CONFLICT && !*again);
    put_value("host.worker-origins",origins.records[0].key,origin); cetta_durable_snapshot_free(&origins);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    snprintf(address.sun_path,sizeof(address.sun_path),"%s/worker.sock",directory);
    int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); assert(listener>=0);
    assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)) && !listen(listener,4));
    CettaWorkerEndpoint *endpoint=NULL;
    assert(cetta_worker_endpoint_new(store,listener,getuid(),"brain",&endpoint)==DURABLE_OK);
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); assert(fd>=0);
    assert(!connect(fd,(struct sockaddr *)&address,sizeof(address)));
    rpc(endpoint,fd,WORKER_NEXT,"","",WORKER_TASK,"the exact observation");
    const char *text="(host:transition cannot-execute () ((host:send 99 forged reply)))";
    rpc(endpoint,fd,WORKER_RESULT,id,text,WORKER_STORED,"");
    CettaHostDecision *d=resume(id,outbox); CettaHostCommit committed;
    /* New input in the chat invalidates an already evaluated continuation;
     * unrelated inbox traffic does not. Nothing sends from the failed ticket. */
    put("host.inbox","chat/2","new-input");
    assert(cetta_host_accept(d,0,&committed)==DURABLE_CONFLICT); cetta_host_decision_free(d);
    assert(count("host.outbox")==1);
    d=resume(id,outbox);
    assert(cetta_host_outcome(d)->results.items[0]->expr.elems[3]->expr.len==0);
    cetta_host_decision_free(d); remove_input("chat/2");
    d=resume(id,outbox); put("host.inbox","other/1","unrelated");
    assert(cetta_host_accept(d,0,&committed)==DURABLE_OK && committed.effects==1); cetta_host_decision_free(d);
    assert(count("host.outbox")==2);
    char send[64]; snprintf(send,sizeof(send),"%s/0",committed.commit_key);
    assert(cetta_worker_register(store,send,"brain",again)==DURABLE_VERSION && !*again);
    /* Consumption and another registration never reinject the worker input. */
    size_t inbox=count("host.inbox");
    assert(cetta_worker_register(store,outbox,"brain",again)==DURABLE_OK && !strcmp(again,id));
    rpc(endpoint,fd,WORKER_RESULT,id,text,WORKER_STORED,""); assert(count("host.inbox")==inbox);
    rpc(endpoint,fd,WORKER_NEXT,"","",WORKER_IDLE,"");
    /* A draft computed under old relevant state, or superseded by new chat
     * input during cognition, is durably retired without an outgoing send. */
    for (unsigned i=0;i<2;++i) {
        put("host.inbox","chat/1","(request \"second observation\")"); accept_request(outbox);
        assert(cetta_worker_register(store,outbox,"brain",id)==DURABLE_OK);
        if (i) put("host.inbox","chat/3","newer"); else put("state","route","changed");
        rpc(endpoint,fd,WORKER_RESULT,id,"old draft",WORKER_STORED,"");
        d=resume(id,outbox); assert(cetta_host_accept(d,0,&committed)==DURABLE_OK && !committed.effects);
        cetta_host_decision_free(d);
    }
    put("host.outbox","wrong-version","(host:intent 1 \"worker.request\" \"2\" (worker:request 1 \"brain\" \"x\") reply)");
    assert(cetta_worker_register(store,"wrong-version","brain",again)==DURABLE_VERSION && !*again);
    close(fd); cetta_worker_endpoint_free(endpoint); unlink(address.sun_path); cetta_durable_close(store);
    unlink(db); char side[300]; snprintf(side,sizeof(side),"%s-wal",db); unlink(side);
    snprintf(side,sizeof(side),"%s-shm",db); unlink(side); assert(!rmdir(directory));
    cetta_library_context_free(&context); eval_set_library_context(NULL);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
    puts("durable worker host: accepted request recovery, immutable origin, authority, recorded response selection and stale-draft retirement passed");
}
