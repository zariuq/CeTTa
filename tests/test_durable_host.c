#define _POSIX_C_SOURCE 200809L
#include "durable_host.h"
#include "durable_value.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static Atom *parse(const char *s) {
    size_t pos=0; Atom *a=parse_sexpr(&persistent,s,&pos);
    assert(a && pos==strlen(s)); return a;
}
static bool send_allowed(void *unused, const Atom *payload, const Atom *reply) {
    (void)unused; (void)reply;
    return payload->kind==ATOM_EXPR && payload->expr.len==3 &&
        atom_is_symbol(payload->expr.elems[0],"sendMessage") &&
        payload->expr.elems[1]->kind==ATOM_GROUNDED &&
        payload->expr.elems[1]->ground.gkind==GV_STRING &&
        !strcmp(payload->expr.elems[1]->ground.sval,"approved-chat");
}
static void write_record(const char *space, const char *key, const char *value) {
    CettaDurableSnapshot snap;
    assert(cetta_durable_snapshot(store,space,&snap)==DURABLE_OK);
    bool exists=false;
    for (size_t i=0;i<snap.count;++i) if (!strcmp(snap.records[i].key,key)) exists=true;
    unsigned char *bytes; size_t size;
    assert(cetta_durable_value_encode(parse(value),&bytes,&size)==DURABLE_OK);
    CettaDurableOp op={exists?DURABLE_REPLACE:DURABLE_INSERT,space,key,bytes,size}; int64_t rev;
    assert(cetta_durable_commit(store,snap.epoch,snap.revision,&op,1,&rev)==DURABLE_OK);
    cetta_durable_snapshot_free(&snap); free(bytes);
}
static size_t count(const char *space) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,space,&s)==DURABLE_OK);
    size_t n=s.count; cetta_durable_snapshot_free(&s); return n;
}
static const CettaHostSpaceGrant spaces[]={
    {{DURABLE_PREFIX,"state","actor/"},true},
    {{DURABLE_PREFIX,"reminders","pending/"},false}
};
static const CettaHostChannelGrant channels[]={{"telegram.send","1",send_allowed,NULL}};
static CettaHostDecisionSpec spec={"input-1","actor","program/he/1",spaces,2,channels,1};
static CettaHostDecision *begin(void) {
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    assert(cetta_host_view(d,0)->count==1 && cetta_host_view(d,3)->count==0);
    assert(!cetta_host_view(d,4)); return d;
}
static CettaHostProgram host_program={"program/he/1",&program,&context};
static const EvalOutcome *evaluate(CettaHostDecision *d, const char *expression) {
    assert(cetta_host_evaluate(d,&host_program,parse(expression),10000)==DURABLE_OK);
    return cetta_host_outcome(d);
}
static const char *proposal="(host:transition (waiting \"reply\") "
    "((host:put 0 \"actor/value\" 42)) "
    "((host:send 0 (sendMessage \"approved-chat\" \"hello\") (resume \"reply\"))))";
static void reject(CettaHostDecision *d, const char *expression, CettaDurableStatus expected) {
    size_t before=count("host.inbox"); CettaHostCommit c;
    evaluate(d,expression);
    assert(cetta_host_accept(d,0,&c)==expected);
    assert(c.revision<0 && count("host.inbox")==before && count("host.outbox")==0);
}
int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    Atom *error=NULL;
    assert(cetta_library_import(&context,"rhometta",&program,&scratch,&persistent,&registry,10000,&error));
    assert(cetta_library_import_module(&context,"durable:rho",&program,false,&scratch,&persistent,&registry,10000,NULL,&error));
    if (argc==3) {
        assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
        spec.input_key="input-3"; spec.space_count=1;
        CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
        evaluate(d,proposal);
        if (!strcmp(argv[1],"--crash-after-accept")) {
            CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_OK);
        } else assert(!strcmp(argv[1],"--crash-before-accept"));
        _exit(0); /* No store/context close and no caller acknowledgment. */
    }
    assert(argc==1);
    char directory[]="/tmp/cetta-host-XXXXXX"; assert(mkdtemp(directory));
    char path[256]; snprintf(path,sizeof(path),"%s/journal.db",directory);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    write_record("host.inbox","input-1","(received \"first\")");
    CettaHostDecision *d=begin();
    CettaHostDecision *unevaluated=begin();
    CettaHostCommit refused;
    evaluate(d,proposal);
    assert(cetta_host_accept(unevaluated,0,&refused)==DURABLE_PRECONDITION);
    cetta_host_decision_free(unevaluated);
    CettaHostProgram wrong_version=host_program; wrong_version.version="program/he/other";
    assert(cetta_host_evaluate(d,&wrong_version,parse(proposal),10000)==DURABLE_VERSION);
    assert(!cetta_host_outcome(d) && cetta_host_accept(d,0,&refused)==DURABLE_PRECONDITION);
    evaluate(d,proposal);
    assert(cetta_host_evaluate(d,&host_program,parse(proposal),0)==DURABLE_INVALID);
    assert(!cetta_host_outcome(d) && cetta_host_accept(d,0,&refused)==DURABLE_PRECONDITION);
    Space cached; space_init(&cached); space_add(&cached,parse("(host:record 0 \"old\" secret)"));
    CettaHostProgram stale_program=host_program; stale_program.space=&cached;
    assert(cetta_host_evaluate(d,&stale_program,parse(proposal),10000)==DURABLE_INVALID);
    assert(!cetta_host_outcome(d)); space_free(&cached);
    space_init(&cached); space_add(&cached,parse("(host:record-version 0 \"old\" 1 0)"));
    assert(cetta_host_evaluate(d,&stale_program,parse(proposal),10000)==DURABLE_INVALID);
    assert(!cetta_host_outcome(d)); space_free(&cached);
    /* Reusing the trusted library context does not reuse decision data. */
    write_record("host.inbox","input-1","(received \"changed\")");
    CettaHostDecision *newer=begin();
    const char *lookup="(match &self (host:record 0 \"input-1\" (received $text)) $text)";
    const EvalOutcome *view=evaluate(newer,lookup);
    assert(view->results.len==1 && atom_eq(view->results.items[0],parse("\"changed\"")));
    view=evaluate(d,lookup);
    assert(view->results.len==1 && atom_eq(view->results.items[0],parse("\"first\"")));
    cetta_host_decision_free(d); cetta_host_decision_free(newer);
    write_record("host.inbox","input-1","(received \"first\")"); d=begin();
    const EvalOutcome *projected=evaluate(d,"(durable:rho:request 0 (sendMessage \"approved-chat\" \"hello\") (resume \"reply\"))");
    assert(projected->completion==CETTA_EVAL_COMPLETE && projected->results.len==1 &&
        atom_eq(projected->results.items[0],parse("(host:send 0 (sendMessage \"approved-chat\" \"hello\") (resume \"reply\"))")));
    projected=evaluate(d,"(match &self (host:record 0 \"input-1\" (received $text)) $text)");
    assert(projected->completion==CETTA_EVAL_COMPLETE && projected->results.len==1 &&
        atom_eq(projected->results.items[0],parse("\"first\"")));
    CettaHostSpaceGrant bad={{DURABLE_SPACE,"host.outbox",NULL},true};
    CettaHostDecisionSpec badspec=spec; badspec.spaces=&bad; badspec.space_count=1;
    CettaHostDecision *other=(void *)1;
    assert(cetta_host_begin(store,&badspec,&other)==DURABLE_INVALID && !other);
    reject(d,"(collapse (superpose (kept (println! hidden))))",DURABLE_PRECONDITION);
    reject(d,"(host:transition $unbound () ())",DURABLE_INVALID);
    reject(d,"(host:transition done ((host:put 0 \"other/key\" 1)) ())",DURABLE_INVALID);
    reject(d,"(host:transition done ((host:put 1 \"pending/x\" 1)) ())",DURABLE_INVALID);
    reject(d,"(host:transition done ((host:remove 0 \"actor/value\") (host:put 0 \"actor/value\" 1)) ())",DURABLE_INVALID);
    reject(d,"(host:transition done ((host:put 0 \"actor/value\" 1) (host:remove 0 \"actor/value\")) ())",DURABLE_INVALID);
    reject(d,"(host:transition done () ((host:send 99 x y)))",DURABLE_INVALID);
    reject(d,"(host:transition done () ((host:send 0 (sendMessage \"unapproved\" \"hello\") reply)))",DURABLE_INVALID);
    reject(d,"(host:transition done () ((host:send 0 (setWebhook \"approved-chat\" \"url\") reply)))",DURABLE_INVALID);
    char expr[2048];
    snprintf(expr,sizeof(expr),"(let $choices (collapse (superpose (%s (println! hidden)))) (superpose $choices))",proposal);
    reject(d,expr,DURABLE_PRECONDITION);
    /* A precondition fails after input removal/state operations have started:
     * the transaction must roll all of them back. The attempted ticket is spent. */
    CettaHostDecision *failed=begin();
    reject(failed,"(host:transition done ((host:put 0 \"actor/value\" 7) (host:remove 0 \"actor/absent\")) ())",DURABLE_PRECONDITION);
    assert(count("host.actors")==0 && count("state")==0 && count("host.commits")==0);
    cetta_host_decision_free(failed);
    /* Live completions and unrelated inputs do not invalidate a slow decision. */
    write_record("host.inbox","input-2","(received \"second\")");
    write_record("host.outcomes","old-effect","(observed 200)");
    snprintf(expr,sizeof(expr),"(superpose (%s (host:transition discarded () ((host:send 0 (sendMessage \"approved-chat\" \"wrong\") unused)))))",proposal);
    const EvalOutcome *out=evaluate(d,expr); assert(out->results.len==2);
    CettaHostCommit accepted;
    assert(cetta_host_accept(d,0,&accepted)==DURABLE_OK && accepted.effects==1);
    assert(!strcmp(accepted.commit_key,cetta_host_decision_key(d)));
    assert(count("host.inbox")==1 && count("host.outbox")==1 && count("host.actors")==1 && count("state")==1);
    CettaHostCommit duplicate;
    assert(cetta_host_accept(d,0,&duplicate)==DURABLE_CONFLICT);
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,"host.outbox",&s)==DURABLE_OK);
    assert(s.records[0].revision==accepted.revision && s.records[0].position==4);
    Atom *intent=NULL; assert(cetta_durable_value_decode(&persistent,s.records[0].data,s.records[0].size,&intent)==DURABLE_OK);
    assert(atom_eq(intent,parse("(host:intent 1 \"telegram.send\" \"1\" (sendMessage \"approved-chat\" \"hello\") (resume \"reply\"))")));
    cetta_durable_snapshot_free(&s); cetta_host_decision_free(d);
    /* Missing input cannot be consumed again after recovery. */
    cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    assert(cetta_host_begin(store,&spec,&other)==DURABLE_PRECONDITION && !other);
    assert(count("host.outbox")==1 && count("host.commits")==1);
    spec.input_key="input-2"; d=begin();
    evaluate(d,proposal);
    /* Negative-prefix reads participate in conflict detection. */
    write_record("reminders","pending/new","(reminder 1)");
    assert(cetta_host_accept(d,0,&accepted)==DURABLE_CONFLICT);
    assert(count("host.inbox")==1 && count("host.outbox")==1);
    cetta_host_decision_free(d);
    /* A real rho COMM returns a proposal; no transport is involved in COMM. */
    spec.space_count=1;
    assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    snprintf(expr,sizeof(expr),"(let $values (rhometta:values (rhometta:run-canonical (rho:par "
        "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)) "
        "(rho:send (rho:quote rho:nil) (rho:val %s))))) (superpose $values))",proposal);
    out=evaluate(d,expr);
    if (out->completion!=CETTA_EVAL_COMPLETE || out->results.len!=1) {
        fprintf(stderr,"rho completion=%d results=%llu\n",out->completion,(unsigned long long)out->results.len);
        for (CettaCount i=0;i<out->results.len;++i) { atom_print(out->results.items[i],stderr); fputc('\n',stderr); }
    }
    assert(out->completion==CETTA_EVAL_COMPLETE && out->results.len==1);
    assert(cetta_host_accept(d,0,&accepted)==DURABLE_OK);
    assert(count("host.inbox")==0 && count("host.outbox")==2);
    cetta_host_decision_free(d);
    assert(cetta_durable_checkpoint(store)==DURABLE_OK);
    cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    assert(count("host.outbox")==2 && count("host.commits")==2);
    write_record("host.inbox","input-3","(received \"third\")");
    cetta_durable_close(store);
    const char *crashes[]={"--crash-before-accept","--crash-after-accept"};
    for (size_t i=0;i<2;++i) {
        pid_t child=fork(); assert(child>=0);
        if (!child) { execl(argv[0],argv[0],crashes[i],path,(char *)NULL); _exit(127); }
        int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
        assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
        assert(count("host.inbox")==1-i && count("host.outbox")==2+i && count("host.commits")==2+i);
        cetta_durable_close(store);
    }
    /* New input in the same chat invalidates an unaccepted draft. A different
     * chat does not. This is a read-only prefix dependency, not cancellation. */
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    CettaHostSpaceGrant lane={{DURABLE_PREFIX,"host.inbox","chat-a/"},false};
    CettaHostDecisionSpec chat={"chat-a/1","chat-actor","program/he/1",&lane,1,NULL,0};
    write_record("host.inbox","chat-a/1","(received \"a\")");
    assert(cetta_host_begin(store,&chat,&d)==DURABLE_OK); evaluate(d,"(host:transition done () ())");
    write_record("host.inbox","chat-b/1","(received \"unrelated\")");
    assert(cetta_host_accept(d,0,&accepted)==DURABLE_OK); cetta_host_decision_free(d);
    chat.input_key="chat-a/2";
    write_record("host.inbox","chat-a/2","(received \"draft\")");
    assert(cetta_host_begin(store,&chat,&d)==DURABLE_OK); evaluate(d,"(host:transition done () ())");
    write_record("host.inbox","chat-a/3","(received \"newer\")");
    assert(cetta_host_accept(d,0,&accepted)==DURABLE_CONFLICT); cetta_host_decision_free(d);
    cetta_durable_close(store);
    assert(!unlink(path));
    char sidecar[300]; snprintf(sidecar,sizeof(sidecar),"%s-wal",path); unlink(sidecar);
    snprintf(sidecar,sizeof(sidecar),"%s-shm",path); unlink(sidecar); assert(!rmdir(directory));
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("durable host: exact scopes, atomic consume/state/outbox, authority, denied branches, alternatives, conflicts, recovery and rho COMM passed");
}
