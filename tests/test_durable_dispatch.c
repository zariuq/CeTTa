#define _POSIX_C_SOURCE 200809L
#include "durable_dispatch.h"
#include "durable_inbox.h"
#include "durable_host.h"
#include "durable_value.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static atomic_uint health;
static Atom *parse(const char *s) {
    size_t pos=0; Atom *a=parse_sexpr(&persistent,s,&pos);
    assert(a && pos==strlen(s)); return a;
}
static const char *string(const Atom *a) {
    assert(a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING); return a->ground.sval;
}
static bool allowed(void *ctx, const Atom *payload, const Atom *reply) {
    (void)ctx; (void)reply;
    return payload->kind==ATOM_EXPR && payload->expr.len==3 && atom_is_symbol(payload->expr.elems[0],"sendMessage") &&
        payload->expr.elems[1]->kind==ATOM_GROUNDED && payload->expr.elems[1]->ground.gkind==GV_STRING &&
        !strcmp(payload->expr.elems[1]->ground.sval,"approved-chat") &&
        payload->expr.elems[2]->kind==ATOM_GROUNDED && payload->expr.elems[2]->ground.gkind==GV_INT;
}
static bool plan(void *ctx, Arena *a, const Atom *payload, const Atom *reply, CettaTelegramPlan *out) {
    if (!allowed(ctx,payload,reply)) return false;
    char *body=arena_alloc(a,128);
    int n=snprintf(body,128,"{\"chat_id\":\"approved-chat\",\"request\":%lld}",(long long)payload->expr.elems[2]->ground.ival);
    assert(n>0 && n<128);
    *out=(CettaTelegramPlan){"sendMessage","application/json",body,(size_t)n}; return true;
}
static void degraded(void *ctx, const char *key, CettaDurableStatus s) {
    (void)ctx; (void)key; (void)s; atomic_fetch_add(&health,1);
}
static void write_record(const char *space, const char *key, const Atom *value) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    unsigned char *bytes; size_t n; assert(cetta_durable_value_encode(value,&bytes,&n)==DURABLE_OK);
    CettaDurableOp op={cetta_durable_observation_view(o,0)->count?DURABLE_REPLACE:DURABLE_INSERT,space,key,bytes,n};
    int64_t rev; assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK);
    free(bytes); cetta_durable_observation_free(o);
}
static Atom *get(const char *space, const char *key) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
    Atom *a=NULL;
    if (v->count) assert(cetta_durable_value_decode(&persistent,v->records[0].data,v->records[0].size,&a)==DURABLE_OK);
    cetta_durable_observation_free(o); return a;
}
static CettaHostProgram trusted={"test/he/1",&program,&context};
static void accept(unsigned id, const char *version, char key[64]) {
    char input[64], expr[2048]; snprintf(input,sizeof(input),"input-%u",id);
    write_record("host.inbox",input,parse("(received \"mock\")"));
    CettaHostChannelGrant grant={"telegram.send",version,allowed,NULL};
    CettaHostDecisionSpec spec={input,"actor","test/he/1",NULL,0,&grant,1};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    /* A rho COMM produces the selected intent, rather than directly writing
     * a transport request or performing network I/O during evaluation. */
    snprintf(expr,sizeof(expr),"(let $values (rhometta:values (rhometta:run-canonical (rho:par "
        "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)) "
        "(rho:send (rho:quote rho:nil) (rho:val (host:transition waiting () "
        "((host:send 0 (sendMessage \"approved-chat\" %u) reply)))))))) (superpose $values))",id);
    assert(cetta_host_evaluate(d,&trusted,parse(expr),10000)==DURABLE_OK);
    CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_OK);
    snprintf(key,64,"%s/0",c.commit_key); cetta_host_decision_free(d);
}
static Atom *wait_outcome(CettaDurableDispatch *d, const char *key) {
    uint64_t generation=0;
    for (size_t i=0;i<100;++i) {
        Atom *a=get("host.outcomes",key); if (a) return a;
        generation=cetta_dispatch_wait(d,generation,100);
    }
    abort();
}
static void check_kind(Atom *a, const char *kind) {
    assert(a && a->kind==ATOM_EXPR && a->expr.len==7 && atom_is_symbol(a->expr.elems[0],"host:outcome"));
    assert(atom_is_symbol(a->expr.elems[4],kind));
}
static void consume(const char *key, Atom *outcome) {
    char input[120], expr[2048]; snprintf(input,sizeof(input),"effect/%s",string(outcome->expr.elems[2]));
    assert(get("host.inbox",input));
    CettaHostSpaceGrant scope={{DURABLE_KEY,"host.outcomes",key},false};
    CettaHostDecisionSpec s={input,"actor","test/he/1",&scope,1,NULL,0};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&s,&d)==DURABLE_OK);
    snprintf(expr,sizeof(expr),"(match &self (host:record 2 \"%s\" $fact) "
        "(let $values (rhometta:values (rhometta:run-canonical (rho:par "
        "(rho:recv (rho:quote rho:nil) $x (rho:val (host:transition resumed () ()))) "
        "(rho:send (rho:quote rho:nil) (rho:val $fact))))) (superpose $values)))",key);
    assert(cetta_host_evaluate(d,&trusted,parse(expr),10000)==DURABLE_OK);
    CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_OK && c.effects==0);
    cetta_host_decision_free(d); assert(!get("host.inbox",input));
}
int main(int argc, char **argv) {
    assert(argc==6);
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    Atom *error=NULL; assert(cetta_library_import(&context,"rhometta",&program,&scratch,&persistent,&registry,10000,&error));
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.record_bytes=1024;
    bool pressure=!strcmp(argv[1],"pressure");
    if (pressure) limits.records=4; /* Acceptance + claim fit; outcomes do not. */
    assert(cetta_durable_open(argv[2],&limits,&store)==DURABLE_OK);
    int fd=open(argv[3],O_RDONLY); assert(fd>=0);
    CettaTelegramCredentialConfig cc={argv[4],strcmp(argv[5],"-")?argv[5]:NULL,true};
    CettaTelegramCredential *credential=NULL;
    assert(cetta_telegram_credential_read(fd,&cc,&credential)==TELEGRAM_CREDENTIAL_OK); close(fd);
    CettaDispatchChannel channel={"telegram.send","1",credential,NULL,plan};
    CettaDispatchConfig config={&channel,1,3000,65536,NULL,degraded};
    CettaDurableDispatch *d=NULL;
    assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
    CettaDurableDispatch *second=(void *)1;
    assert(cetta_dispatch_new(store,&config,&second)==DURABLE_BUSY && !second);
    assert(cetta_durable_detach_runtime(store,&config)==DURABLE_INVALID);
    if (!strcmp(argv[1],"recover")) {
        CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,"host.outbox",&s)==DURABLE_OK && s.count==1);
        const char *key=s.records[0].key;
        Atom *a=get("host.outcomes",key); check_kind(a,"uncertain");
        assert(atom_is_symbol(a->expr.elems[5]->expr.elems[0],"unknown"));
        assert(cetta_dispatch_submit(d,key)==DURABLE_PRECONDITION);
        consume(key,a);
        assert(cetta_dispatch_free(d)==0); d=NULL;
        assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
        char input[120]; snprintf(input,sizeof(input),"effect/%s",string(a->expr.elems[2]));
        assert(!get("host.inbox",input));
        assert(cetta_dispatch_submit(d,key)==DURABLE_PRECONDITION);
        cetta_durable_snapshot_free(&s);
    } else if (pressure) {
        char key[64]; accept(12,"1",key); assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
        uint64_t generation=0;
        for (unsigned i=0;!atomic_load(&health) && i<150;++i) generation=cetta_dispatch_wait(d,generation,100);
        assert(atomic_load(&health) && get("host.attempts",key) && !get("host.outcomes",key));
        assert(cetta_dispatch_free(d)==1); d=NULL;
    } else if (!strcmp(argv[1],"crash")) {
        char key[64]; accept(7,"1",key); assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
        for (;;) pause(); /* Harness SIGKILLs after the mock fully received it. */
    } else {
        assert(!strcmp(argv[1],"normal"));
        CettaTelegramPoll poll={credential,"bot",0,1,NULL,0};
        assert(cetta_dispatch_poll(d,&poll)==DURABLE_OK);
        for (unsigned i=1;i<=3;++i) {
            char key[64]; accept(i,"1",key);
            assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
            CettaDurableStatus dup=cetta_dispatch_submit(d,key);
            assert(dup==DURABLE_BUSY || dup==DURABLE_PRECONDITION);
            Atom *a=wait_outcome(d,key);
            check_kind(a,i==1?"observed":i==2?"uncertain":"privacy-suppressed");
            assert(get("host.attempts",key));
            if (i==3) assert(!*string(a->expr.elems[6]));
            consume(key,a);
            assert(cetta_dispatch_submit(d,key)==DURABLE_PRECONDITION);
        }
        CettaInboxWindow *window=NULL; uint64_t poll_generation=0;
        for (unsigned i=0;!window && i<100;++i) {
            CettaDurableStatus status=cetta_inbox_recover_poll(store,"bot",&window);
            assert(status==DURABLE_OK || status==DURABLE_PRECONDITION);
            if (!window) poll_generation=cetta_dispatch_wait(d,poll_generation,100);
        }
        assert(window && cetta_inbox_offset(window)==0);
        const CettaDurableRecord *r=cetta_inbox_poll_response(window); Atom *response=NULL;
        assert(cetta_durable_value_decode(&persistent,r->data,r->size,&response)==DURABLE_OK);
        assert(!strcmp(string(response->expr.elems[6]),"{\"ok\":true,\"result\":[]}"));
        CettaInboxCommit received;
        assert(cetta_inbox_commit(window,NULL,0,&received)==DURABLE_OK && received.next_offset==0);
        cetta_inbox_window_free(window);
        char key[64]; accept(4,"2",key);
        assert(cetta_dispatch_submit(d,key)==DURABLE_VERSION && !get("host.attempts",key));
        assert(cetta_dispatch_cancel(d,key)==DURABLE_OK);
        check_kind(get("host.outcomes",key),"not-started");
        accept(5,"1",key); assert(cetta_dispatch_cancel(d,key)==DURABLE_OK);
        assert(cetta_dispatch_submit(d,key)==DURABLE_PRECONDITION && !get("host.attempts",key));
        /* Oversized recording switches to an explicit minimal fact. No resend. */
        accept(8,"1",key); assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
        Atom *a=wait_outcome(d,key); check_kind(a,"unrecordable"); assert(!*string(a->expr.elems[6]));
        consume(key,a);
        accept(9,"1",key); assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
        a=wait_outcome(d,key); check_kind(a,"observed"); consume(key,a);
        /* Cancellation after a durable claim is a request, not remote undo. */
        accept(10,"1",key); assert(cetta_dispatch_submit(d,key)==DURABLE_OK);
        uint64_t generation=0;
        for (unsigned i=0;!get("host.attempts",key) && i<100;++i) generation=cetta_dispatch_wait(d,generation,10);
        assert(get("host.attempts",key));
        assert(cetta_dispatch_cancel(d,key)==DURABLE_OK);
        a=wait_outcome(d,key);
        assert(atom_is_symbol(a->expr.elems[4],"not-started") || atom_is_symbol(a->expr.elems[4],"uncertain"));
        assert(get("host.cancellations",key)); consume(key,a);
        assert(cetta_dispatch_cancel(d,key)==DURABLE_OK);
        assert(cetta_dispatch_free(d)==0); d=NULL;
        assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
        char input[120]; snprintf(input,sizeof(input),"effect/%s",string(a->expr.elems[2]));
        assert(!get("host.inbox",input));
    }
    assert(pressure || !atomic_load(&health));
    assert(cetta_dispatch_free(d)==0);
    cetta_telegram_credential_free(credential); cetta_durable_close(store);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("durable dispatch: rho acceptance, claim, screened outcome/completion, cancellation and recovery passed");
}
