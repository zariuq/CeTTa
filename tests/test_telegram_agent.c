#define _POSIX_C_SOURCE 200809L
#include "telegram_agent.h"
#include "durable_inbox.h"
#include "durable_worker_host.h"
#include "durable_value.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Arena persistent,scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static CettaHostProgram trusted={"telegram-agent/1",&program,&context};
static const int64_t chats[]={42,84};
static CettaTelegramActionPolicy actions={chats,2,7};
static CettaTelegramAgent agent={&trusted,"bot","brain",&actions,2000000};
static unsigned policy_checks;
static Atom *parse(const char *s) { size_t p=0; Atom *a=parse_sexpr(&persistent,s,&p); assert(a && p==strlen(s)); return a; }
static Atom *record(const char *space,const char *key) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0); Atom *a=NULL;
    if (v->count) assert(cetta_durable_value_decode(&persistent,v->records[0].data,v->records[0].size,&a)==DURABLE_OK);
    cetta_durable_observation_free(o); return a;
}
static size_t count(const char *space) { CettaDurableSnapshot v; assert(cetta_durable_snapshot(store,space,&v)==DURABLE_OK); size_t n=v.count; cetta_durable_snapshot_free(&v); return n; }
static void put(const char *space,const char *key,Atom *value) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    unsigned char *b; size_t n; int64_t rev; assert(cetta_durable_value_encode(value,&b,&n)==DURABLE_OK);
    CettaDurableOp op={cetta_durable_observation_view(o,0)->count?DURABLE_REPLACE:DURABLE_INSERT,space,key,b,n};
    assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK); free(b); cetta_durable_observation_free(o);
}
static void erase(const char *space,const char *key) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL; int64_t rev;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    CettaDurableOp op={DURABLE_REMOVE,space,key,NULL,0};
    assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK); cetta_durable_observation_free(o);
}
static void incoming(int id,int chat,char key[168]) {
    CettaInboxWindow *w=NULL; assert(cetta_inbox_begin(store,"bot",&w)==DURABLE_OK);
    char lane[32],value[128],ledger[96]; snprintf(lane,sizeof(lane),"%d.0",chat);
    snprintf(value,sizeof(value),"(telegram:input 1 \"message\" %d 0 7 ordinary (JsonObjectV1 ()))",chat);
    CettaInboxItem item={id,lane,INBOX_ROUTED,parse(value)}; CettaInboxCommit c;
    assert(cetta_inbox_commit(w,&item,1,&c)==DURABLE_OK && c.inserted==1); cetta_inbox_window_free(w);
    assert(cetta_inbox_keys("bot",lane,id,ledger,key)==DURABLE_OK);
}
static CettaHostDecision *decide(const char *key,size_t results) {
    CettaHostDecision *d=NULL; CettaDurableStatus s=cetta_telegram_agent_decide(store,&agent,key,&d);
    if (s!=DURABLE_OK) { fprintf(stderr,"decide %s: %s\n",key,cetta_durable_status_name(s)); abort(); }
    const EvalOutcome *o=cetta_host_outcome(d);
    bool blocked=!results && o->results.len==1 && atom_is_symbol(o->results.items[0],"Empty");
    if (o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials || (o->results.len!=results && !blocked)) {
        fprintf(stderr,"decide %s: completion=%d denials=%llu results=%llu expected=%zu\n",key,o->completion,
            (unsigned long long)o->effect_denials,(unsigned long long)o->results.len,results);
        for (CettaCount i=0;i<o->results.len;++i) { atom_print(o->results.items[i],stderr); fputc('\n',stderr); } abort();
    }
    if (blocked) { CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_INVALID); }
    return d;
}
static void accept(CettaHostDecision *d,size_t effects,char key[64]) {
    CettaHostCommit c; CettaDurableStatus s=cetta_host_accept(d,0,&c);
    if (s!=DURABLE_OK) { fprintf(stderr,"accept: %s\n",cetta_durable_status_name(s)); atom_print(cetta_host_outcome(d)->results.items[0],stderr); abort(); }
    assert(c.effects==effects); if (key) snprintf(key,64,"%s/0",c.commit_key); cetta_host_decision_free(d);
}
static void control_request(const char *id,const char *lane,const char *batch,char key[80]) {
    Atom *args[]={atom_symbol(&persistent,"host:telegram-control"),atom_int(&persistent,1),
        atom_string(&persistent,"bot"),atom_string(&persistent,id),atom_string(&persistent,lane),
        atom_string(&persistent,"release-worker"),atom_string(&persistent,batch)};
    Atom *request=atom_expr(&persistent,args,7);
    snprintf(key,80,"control/%s",id);
    put("telegram.controls",id,request); put("host.inbox",key,request);
}
static void result(const char *id,const char *body,char input[140]) {
    char task[130]; snprintf(task,sizeof(task),"brain/%s",id); snprintf(input,140,"worker/%s",task);
    CettaDurableScope q={DURABLE_KEY,"host.worker-tasks",task}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    const CettaDurableRecord *r=cetta_durable_observation_view(o,0)->records;
    Atom *parts[]={atom_symbol(&persistent,"host:worker-result"),atom_int(&persistent,1),atom_string(&persistent,"brain"),
        atom_string(&persistent,id),atom_int(&persistent,r->revision),atom_string(&persistent,body)};
    put("host.inbox",input,atom_expr(&persistent,parts,6)); cetta_durable_observation_free(o);
}
static void completed(const char *key,const char *kind,const char *body,char input[140]) {
    snprintf(input,140,"effect/%s",key);
    Atom *out[]={atom_symbol(&persistent,"host:outcome"),atom_int(&persistent,1),atom_string(&persistent,key),
        atom_string(&persistent,"attempt"),atom_symbol(&persistent,kind),parse("(True False 0 200 True 100 False False False)"),atom_string(&persistent,body)};
    Atom *in[]={atom_symbol(&persistent,"host:completion"),atom_int(&persistent,1),atom_string(&persistent,key),atom_string(&persistent,key)};
    put("host.outcomes",key,atom_expr(&persistent,out,7)); put("host.inbox",input,atom_expr(&persistent,in,4));
}
static void state(int chat,const char *expected) {
    char key[64]; snprintf(key,sizeof(key),"telegram/bot/%d.0",chat); Atom *v=record("host.actors",key);
    assert(v && v->kind==ATOM_EXPR && v->expr.len==4);
    if (!atom_eq(v->expr.elems[3],parse(expected))) { fprintf(stderr,"state: "); atom_print(v,stderr); fprintf(stderr,"\nexpected: %s\n",expected); abort(); }
}
static void admission(const char *key,CettaTelegramAdmission expected) {
    CettaTelegramAdmission value; CettaDurableWatch *w=NULL;
    assert(cetta_telegram_agent_admit(store,&agent,key,1024*1024,&value,&w)==DURABLE_OK);
    assert(value==expected);
    if (w) assert(cetta_durable_watch_current(store,w)==DURABLE_OK);
    cetta_durable_watch_free(w);
}
static void pure(Atom *expression,const char *expected) {
    EvalOutcome o; eval_outcome_init(&o);
    eval_top_speculative(&context,&program,&scratch,&persistent,&registry,expression,2000000,&o);
    if (o.completion!=CETTA_EVAL_COMPLETE || o.effect_denials || o.results.len!=1 ||
        !atom_eq(o.results.items[0],parse(expected))) {
        fprintf(stderr,"policy expected %s for ",expected); atom_print(expression,stderr); fputc('\n',stderr);
        for (CettaCount i=0;i<o.results.len;++i) { atom_print(o.results.items[i],stderr); fputc('\n',stderr); }
        abort();
    }
    ++policy_checks; eval_outcome_free(&o);
}
static void api(const char *action,const char *metadata,const char *body,const char *expected) {
    Atom *out[]={atom_symbol(&persistent,"host:outcome"),atom_int(&persistent,1),atom_string(&persistent,"effect"),
        atom_string(&persistent,"attempt"),atom_symbol(&persistent,"observed"),parse(metadata),atom_string(&persistent,body)};
    Atom *call[]={atom_symbol(&persistent,"tg-agent:outcome"),atom_expr(&persistent,out,7),parse(action)};
    pure(atom_expr(&persistent,call,3),expected);
}
static void policy_tests(void) {
    const char *send="(telegram:send-text 1 42 0 0 \"x\" \"plain\")";
    const char *edit="(telegram:edit-text 1 42 9 \"x\" \"plain\")";
    const char *del="(telegram:delete-message 1 42 9)";
    const char *ok="(True False 0 200 True 100 False False False)";
    const char *message="{\"ok\":true,\"result\":{\"message_id\":9,\"chat\":{\"id\":42}}}";
    api(send,ok,message,"(tg-agent:delivered 9)");
    api(edit,ok,message,"(tg-agent:delivered 9)");
    api(del,ok,"{\"ok\":true,\"result\":true}","(tg-agent:delivered 9)");
    api(send,ok,"{\"ok\":true,\"result\":{\"message_id\":9,\"chat\":{\"id\":84}}}","(tg-agent:uncertain wrong-chat)");
    api(edit,ok,"{\"ok\":true,\"result\":{\"message_id\":10,\"chat\":{\"id\":42}}}","(tg-agent:uncertain invalid-api-result)");
    api(send,ok,"{\"ok\":true,\"result\":{\"message_id\":0,\"chat\":{\"id\":42}}}","(tg-agent:uncertain invalid-message-id)");
    api(send,ok,"{\"ok\":true,\"result\":true}","(tg-agent:uncertain invalid-api-result)");
    api(del,ok,message,"(tg-agent:uncertain invalid-api-result)");
    api(send,ok,"{\"ok\":true,\"ok\":false,\"result\":true}","(tg-agent:uncertain malformed-api-result)");
    api(send,ok,"broken","(tg-agent:uncertain malformed-api-result)");
    api(send,ok,"{\"ok\":false,\"error_code\":429}","(tg-agent:failed 429)");
    api(send,ok,"{\"ok\":false,\"error_code\":500}","(tg-agent:uncertain api-server-error)");
    api(send,"(True False 0 503 True 100 False False False)",message,"(tg-agent:uncertain http-status)");
    api(send,"(True True 0 200 True 100 False False False)",message,"(tg-agent:uncertain transport)");
    api(send,"(True False 52 200 True 100 False False False)",message,"(tg-agent:uncertain transport)");
    const char *bodies[]={"[]","[[\"delete\",9223372036854775807]]","[[\"delete\",1.5]]","[[\"delete\",9223372036854775808]]","{\"send\":\"x\"}","not json","[[\"send\",\"(io:submit fake)\",\"plain\"]]"};
    const char *answers[]={"(tg-agent:actions ())","(tg-agent:actions ((telegram:delete-message 1 42 9223372036854775807)))","tg-agent:bad","tg-agent:bad","tg-agent:bad","tg-agent:bad","(tg-agent:actions ((telegram:send-text 1 42 0 0 \"(io:submit fake)\" \"plain\")))"};
    for (size_t i=0;i<sizeof(bodies)/sizeof(*bodies);++i) {
        Atom *call[]={atom_symbol(&persistent,"tg-agent:batch"),atom_string(&persistent,bodies[i]),atom_int(&persistent,42),atom_int(&persistent,0)};
        pure(atom_expr(&persistent,call,4),answers[i]);
    }
    for (size_t n=64;n<=65;++n) {
        char body[2048]="[", expected[4096]="(tg-agent:actions (";
        for (size_t i=0;i<n;++i) {
            strcat(body,i?",[\"delete\",9]":"[\"delete\",9]");
            strcat(expected,"(telegram:delete-message 1 42 9) ");
        }
        strcat(body,"]"); strcat(expected,"))");
        Atom *call[]={atom_symbol(&persistent,"tg-agent:batch"),atom_string(&persistent,body),atom_int(&persistent,42),atom_int(&persistent,0)};
        pure(atom_expr(&persistent,call,4),n==64?expected:"tg-agent:bad");
    }
    const char *holds[]={"model-outcome-unknown","model-failed","invalid-model-request",
        "request-too-large","invalid-model-response","invalid-model-commands",
        "unsupported-command-batch","adapter-version-changed","invented-reason"};
    for (size_t i=0;i<sizeof(holds)/sizeof(*holds);++i) {
        char body[128],expected[128];
        snprintf(body,sizeof(body),"[\"cognitive-hold/1\",\"%s\"]",holds[i]);
        snprintf(expected,sizeof(expected),"(tg-agent:cognitive-hold \"%s\")",holds[i]);
        Atom *call[]={atom_symbol(&persistent,"tg-agent:batch"),atom_string(&persistent,body),atom_int(&persistent,42),atom_int(&persistent,0)};
        pure(atom_expr(&persistent,call,4),i==8?"tg-agent:bad":expected);
    }
}
int main(int argc,char **argv) {
    (void)argc;
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
    if (!cetta_library_import_module(&context,"durable:telegram_agent",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error)) {
        if (error) atom_print(error,stderr);
        abort();
    }
    policy_tests();
    char dir[]="/tmp/cetta-telegram-agent-XXXXXX"; assert(mkdtemp(dir));
    char db[256]; snprintf(db,sizeof(db),"%s/state.db",dir); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    char input[168],other[168],request[64],other_request[64],batch[65],other_batch[65],worker_input[140],effect[140],sendkey[64];
    incoming(1,42,input); accept(decide(input,1),1,request);
    admission(request,TELEGRAM_WORKER);
    state(42,"(tg-agent:waiting \"bot\" \"42.0\" 1 42 0)");
    assert(cetta_worker_register(store,request,"brain",batch)==DURABLE_OK);
    cetta_durable_close(store); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    incoming(2,84,other); accept(decide(other,1),1,other_request);
    assert(cetta_worker_register(store,other_request,"brain",other_batch)==DURABLE_OK);
    result(batch,"[[\"send\",\"hello\",\"plain\"],[\"edit\",9,\"changed\",\"HTML\"],[\"delete\",9]]",worker_input);
    Atom *original=record("host.inbox",worker_input), *wrong=atom_deep_copy(&persistent,original);
    wrong->expr.elems[4]=atom_int(&persistent,wrong->expr.elems[4]->ground.ival+1);
    put("host.inbox",worker_input,wrong);
    CettaHostDecision *mismatch=decide(worker_input,0); cetta_host_decision_free(mismatch);
    assert(record("host.inbox",worker_input)); put("host.inbox",worker_input,original);
    CettaHostDecision *d=decide(worker_input,1);
    incoming(3,84,other); // Unrelated traffic does not invalidate this reply.
    accept(d,3,sendkey);
    char expected[180]; snprintf(expected,sizeof(expected),"(tg-agent:sending \"%s\" 0 3)",batch); state(42,expected);
    assert(record("telegram.decisions",batch));
    admission(sendkey,TELEGRAM_SEND);
    char nextkey[64]; strcpy(nextkey,sendkey); nextkey[strlen(nextkey)-1]='1';
    admission(nextkey,TELEGRAM_WAIT);
    incoming(4,42,input); d=decide(input,0); cetta_host_decision_free(d); // Accepted work survives newer input.
    d=decide(input,0); CettaDurableWatch *watch=NULL;
    assert(cetta_host_watch(d,1,&watch)==DURABLE_LIMIT && !watch);
    assert(cetta_host_watch(d,1024*1024,&watch)==DURABLE_OK);
    cetta_host_decision_free(d);
    assert(cetta_durable_watch_bytes(watch)>0 && cetta_durable_watch_bytes(watch)<1024*1024);
    put("unrelated","key",parse("value")); assert(cetta_durable_watch_current(store,watch)==DURABLE_OK);
    char third[64]; strcpy(third,sendkey); third[strlen(third)-1]='2';
    completed(third,"observed","{\"ok\":true,\"result\":true}",effect);
    d=decide(effect,0); cetta_host_decision_free(d); // A later completion cannot jump the lane.
    assert(record("host.inbox",effect) && !record("telegram.deliveries",third));
    completed(sendkey,"observed","{\"ok\":true,\"result\":{\"message_id\":9,\"chat\":{\"id\":42}}}",effect);
    accept(decide(effect,1),0,NULL);
    assert(cetta_durable_watch_current(store,watch)==DURABLE_CONFLICT); cetta_durable_watch_free(watch);
    admission(sendkey,TELEGRAM_DONE); admission(nextkey,TELEGRAM_SEND);
    CettaTelegramAdmission done;
    assert(cetta_telegram_agent_admit(store,&agent,sendkey,0,&done,&watch)==DURABLE_OK && done==TELEGRAM_DONE && !watch);
    snprintf(expected,sizeof(expected),"(tg-agent:sending \"%s\" 1 3)",batch); state(42,expected);
    char second[64]; strcpy(second,sendkey); second[strlen(second)-1]='1';
    completed(second,"uncertain","",effect); accept(decide(effect,1),0,NULL);
    snprintf(expected,sizeof(expected),"(tg-agent:held \"%s\" 1 3 (tg-agent:uncertain transport))",batch); state(42,expected);
    admission(nextkey,TELEGRAM_DONE);
    d=decide(input,0); cetta_host_decision_free(d);
    result(other_batch,"[[\"send\",\"old draft\",\"plain\"]]",worker_input);
    size_t before=count("host.outbox"); accept(decide(worker_input,1),0,NULL);
    assert(count("host.outbox")==before); state(84,"(tg-agent:idle)");
    Atom *audit=record("telegram.decisions",other_batch); assert(atom_is_symbol(audit->expr.elems[2],"stale"));
    accept(decide(other,1),1,other_request); assert(cetta_worker_register(store,other_request,"brain",other_batch)==DURABLE_OK);
    result(other_batch,"[[\"send\",\"(io:submit fake)\",\"plain\"]]",worker_input);
    d=decide(worker_input,1); incoming(5,84,other);
    CettaHostCommit commit; assert(cetta_host_accept(d,0,&commit)==DURABLE_CONFLICT); cetta_host_decision_free(d);
    accept(decide(worker_input,1),0,NULL); state(84,"(tg-agent:idle)");
    accept(decide(other,1),1,other_request); assert(cetta_worker_register(store,other_request,"brain",other_batch)==DURABLE_OK);
    result(other_batch,"[[\"setWebhook\",\"https://invalid.test\"]]",worker_input);
    accept(decide(worker_input,1),0,NULL); state(84,"(tg-agent:idle)");
    audit=record("telegram.decisions",other_batch); assert(atom_is_symbol(audit->expr.elems[2],"rejected"));
    incoming(6,84,other);
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"other-version\" (tg-agent:idle))"));
    d=decide(other,0); cetta_host_decision_free(d); // Unknown continuations are not absent actors.
    erase("host.actors","telegram/bot/84.0");
    agent.fuel=1; assert(cetta_telegram_agent_decide(store,&agent,other,&d)==DURABLE_OK);
    assert(cetta_host_accept(d,0,&commit)==DURABLE_PRECONDITION); cetta_host_decision_free(d); agent.fuel=2000000;
    assert(record("host.inbox",other));
    accept(decide(other,1),1,other_request); assert(cetta_worker_register(store,other_request,"brain",other_batch)==DURABLE_OK);
    result(other_batch,"[[\"send\",\"valid\",\"plain\"],[\"send\",\"\",\"plain\"]]",worker_input);
    d=decide(worker_input,1); before=count("host.outbox");
    accept(d,0,NULL);
    assert(count("host.outbox")==before && !record("host.inbox",worker_input));
    audit=record("telegram.decisions",other_batch); assert(atom_is_symbol(audit->expr.elems[2],"rejected"));
    state(84,"(tg-agent:idle)"); // Whole batch rejected, including the valid first action.
    incoming(7,84,other); accept(decide(other,1),1,other_request);
    assert(cetta_worker_register(store,other_request,"brain",other_batch)==DURABLE_OK);
    result(other_batch,"[]",worker_input);
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"other-version\" (tg-agent:idle))"));
    d=decide(worker_input,0); cetta_host_decision_free(d);
    assert(record("host.inbox",worker_input) && !record("telegram.decisions",other_batch));
    context.session.language_id=CETTA_LANGUAGE_PETTA;
    assert(cetta_telegram_agent_decide(store,&agent,other,&d)==DURABLE_INVALID && !d);
    context.session.language_id=CETTA_LANGUAGE_HE;
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"telegram-agent/1\" (tg-agent:waiting \"bot\" \"84.0\" 7 84 0))"));
    result(other_batch,"[\"cognitive-hold/1\",\"model-outcome-unknown\"]",worker_input);
    before=count("host.outbox"); accept(decide(worker_input,1),0,NULL);
    assert(count("host.outbox")==before && !record("host.inbox",worker_input));
    snprintf(expected,sizeof(expected),"(tg-agent:worker-held \"%s\" \"model-outcome-unknown\")",other_batch);
    state(84,expected);
    audit=record("telegram.decisions",other_batch);
    assert(atom_is_symbol(audit->expr.elems[2],"held") && atom_eq(audit->expr.elems[3],parse("(tg-agent:cognitive-hold \"model-outcome-unknown\")")));
    cetta_durable_close(store); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK); state(84,expected);
    incoming(8,84,other); d=decide(other,0); cetta_host_decision_free(d);
    assert(record("host.inbox",other)); // Later chat input remains durable, without another paid request.
    erase("host.actors","telegram/bot/42.0"); incoming(9,42,input);
    accept(decide(input,1),1,request); state(84,expected); // Independent chat still progresses.
    char control[80];
    control_request("waiting","42.0","not-held",control);
    accept(decide(control,1),0,NULL);
    assert(atom_is_symbol(record("telegram.control-results","waiting")->expr.elems[2],"refused"));
    control_request("stale","84.0","wrong-batch",control);
    accept(decide(control,1),0,NULL); state(84,expected);
    assert(atom_is_symbol(record("telegram.control-results","stale")->expr.elems[2],"refused"));
    control_request("paired","84.0",other_batch,control);
    Atom *request_copy=record("host.inbox",control);
    put("telegram.controls","paired",parse("(host:telegram-control 1 \"bot\" \"paired\" \"84.0\" \"release-worker\" \"other\")"));
    d=decide(control,0); cetta_host_decision_free(d); state(84,expected);
    put("telegram.controls","paired",request_copy);
    d=decide(control,1);
    // The exact hold is validated again at commit, not just at proposal time.
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"telegram-agent/1\" (tg-agent:worker-held \"newer\" \"provider-error\"))"));
    assert(cetta_host_accept(d,0,&commit)==DURABLE_CONFLICT); cetta_host_decision_free(d);
    Atom *held[]={atom_symbol(&persistent,"host:actor"),atom_int(&persistent,1),atom_string(&persistent,"telegram-agent/1"),parse(expected)};
    put("host.actors","telegram/bot/84.0",atom_expr(&persistent,held,4));
    before=count("host.outbox"); accept(decide(control,1),0,NULL); state(84,"(tg-agent:idle)");
    assert(count("host.outbox")==before && !record("host.inbox",control));
    assert(atom_is_symbol(record("telegram.control-results","paired")->expr.elems[2],"released"));
    put("host.inbox",control,request_copy); d=decide(control,0); cetta_host_decision_free(d);
    assert(atom_is_symbol(record("telegram.control-results","paired")->expr.elems[2],"released"));
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"telegram-agent/1\" (tg-agent:held \"delivery\" 0 1 uncertain))"));
    control_request("uncertain","84.0","delivery",control);
    accept(decide(control,1),0,NULL); state(84,"(tg-agent:held \"delivery\" 0 1 uncertain)");
    assert(atom_is_symbol(record("telegram.control-results","uncertain")->expr.elems[2],"refused"));
    put("host.actors","telegram/bot/84.0",parse("(host:actor 2 \"telegram-agent/1\" (tg-agent:worker-held \"delivery\" \"provider-error\"))"));
    control_request("version","84.0","delivery",control);
    d=decide(control,0); cetta_host_decision_free(d); assert(!record("telegram.control-results","version"));
    printf("Telegram agent: %u pure policy checks; recorded input, rho reaction, task pairing, atomic batches, chat progress, stale drafts, held uncertainty and profile/fuel checks passed\n",policy_checks);
    cetta_durable_close(store); unlink(db); char path[300]; snprintf(path,sizeof(path),"%s-wal",db); unlink(path); snprintf(path,sizeof(path),"%s-shm",db); unlink(path); rmdir(dir);
    cetta_library_context_free(&context); eval_set_library_context(NULL); registry_free(&registry); space_free(&program);
    arena_free(&scratch); arena_free(&persistent); var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
}
