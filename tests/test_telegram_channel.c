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
static CettaHostProgram trusted={"telegram-channel/1",&program,&context};
static const int64_t chats[]={42,84};
static CettaTelegramActionPolicy actions={chats,2,15};
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
static void admission(const char *key,CettaTelegramAdmission expected) {
    CettaTelegramAdmission value; CettaDurableWatch *w=NULL;
    CettaDurableStatus s=cetta_telegram_agent_admit(store,&agent,key,1024*1024,&value,&w);
    if (s!=DURABLE_OK || value!=expected) {
        fprintf(stderr,"admission %s: status %s value %d expected %d\nintent: ",key,cetta_durable_status_name(s),(int)value,(int)expected);
        Atom *i=record("host.outbox",key); if (i) atom_print(i,stderr); fputc('\n',stderr); abort();
    }
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
static void blocked(const char *key) { CettaHostDecision *d=decide(key,0); cetta_host_decision_free(d); }
static void submitted(const char *key,const char *body,char input[180]) {
    Atom *parts[]={atom_symbol(&persistent,"host:worker-submission"),atom_int(&persistent,1),
        atom_string(&persistent,"brain"),atom_string(&persistent,key),atom_string(&persistent,body)};
    Atom *value=atom_expr(&persistent,parts,5); char ledger[160];
    snprintf(ledger,sizeof(ledger),"brain/%s",key); snprintf(input,180,"submission/brain/%s",key);
    put("host.worker-submissions",ledger,value); put("host.inbox",input,value);
}
/* Accept and return the commit key, whose effects are KEY/0, KEY/1, ... */
static void accept_batch(CettaHostDecision *d,size_t effects,char commit[33]) {
    CettaHostCommit c; CettaDurableStatus s=cetta_host_accept(d,0,&c);
    if (s!=DURABLE_OK) { fprintf(stderr,"accept: %s\n",cetta_durable_status_name(s)); atom_print(cetta_host_outcome(d)->results.items[0],stderr); abort(); }
    assert(c.effects==effects); memcpy(commit,c.commit_key,33); cetta_host_decision_free(d);
}
static void lane(const char *name,const char *expected) {
    char key[64]; snprintf(key,sizeof(key),"telegram/bot/%s",name); Atom *v=record("host.actors",key);
    if (!v || v->kind!=ATOM_EXPR || v->expr.len!=4 || !atom_eq(v->expr.elems[3],parse(expected))) {
        fprintf(stderr,"lane %s: ",name); if (v) atom_print(v,stderr); fprintf(stderr,"\nexpected: %s\n",expected); abort();
    }
}
/* Publish an accepted worker.request and check its exact observation text. */
static void published(const char *request,const char *expected,char task[65]) {
    admission(request,TELEGRAM_WORKER);
    assert(cetta_worker_register(store,request,"brain",task)==DURABLE_OK);
    char key[140]; snprintf(key,sizeof(key),"brain/%s",task); Atom *v=record("host.worker-tasks",key);
    if (!v || v->kind!=ATOM_GROUNDED || v->ground.gkind!=GV_STRING || strcmp(v->ground.sval,expected)) {
        fprintf(stderr,"observation: "); if (v) atom_print(v,stderr); fprintf(stderr,"\nexpected: %s\n",expected); abort();
    }
}
static void decided(const char *key,const char *expected) {
    Atom *v=record("telegram.decisions",key);
    if (!v || !atom_eq(v,parse(expected))) { fprintf(stderr,"decision %s: ",key); if (v) atom_print(v,stderr); fprintf(stderr,"\nexpected %s\n",expected); abort(); }
}
static const char *ok_message="{\"ok\":true,\"result\":{\"message_id\":9,\"chat\":{\"id\":42}}}";
/* A normalized update, as the intake stores it. */
static Atom *json_update(const char *json) {
    char escaped[1100],expr[1200],*o=escaped;
    for (const char *p=json;*p;++p) { if (*p=='"' || *p=='\\') *o++='\\'; *o++=*p; } *o=0;
    snprintf(expr,sizeof(expr),"(telegram:normalize (json:parse \"%s\") 0)",escaped);
    EvalOutcome r; eval_outcome_init(&r);
    eval_top_speculative(&context,&program,&scratch,&persistent,&registry,parse(expr),2000000,&r);
    assert(r.completion==CETTA_EVAL_COMPLETE && r.results.len==1);
    Atom *ok=r.results.items[0];
    assert(ok->kind==ATOM_EXPR && ok->expr.len==2 && atom_is_symbol(ok->expr.elems[0],"tg:ok"));
    Atom *v=atom_deep_copy(&persistent,ok->expr.elems[1]); eval_outcome_free(&r); return v;
}
static Atom *text_update(int message,int chat,const char *text) {
    char json[512];
    snprintf(json,sizeof(json),"{\"message\":{\"message_id\":%d,\"chat\":{\"id\":%d,\"type\":\"private\"},\"text\":\"%s\"}}",message,chat,text);
    return json_update(json);
}
/* A tap on a button of message MESSAGE; a NULL data leaves the data out. */
static Atom *tap_update(int id,int message,int chat,const char *data) {
    char json[512],field[160]="";
    if (data) snprintf(field,sizeof(field),",\"data\":\"%s\"",data);
    snprintf(json,sizeof(json),"{\"callback_query\":{\"id\":\"q%d\",\"from\":{\"id\":7},\"message\":{\"message_id\":%d,\"chat\":{\"id\":%d,\"type\":\"private\"}}%s}}",id,message,chat,field);
    return json_update(json);
}
static void incoming_update(int id,int chat,const char *kind,const char *role,Atom *update,char key[168]) {
    CettaInboxWindow *w=NULL; assert(cetta_inbox_begin(store,"bot",&w)==DURABLE_OK);
    char lane[32],ledger[96]; snprintf(lane,sizeof(lane),"%d.0",chat);
    Atom *parts[]={atom_symbol(&persistent,"telegram:input"),atom_int(&persistent,1),atom_string(&persistent,kind),
        atom_int(&persistent,chat),atom_int(&persistent,0),atom_int(&persistent,7),atom_symbol(&persistent,role),update};
    CettaInboxItem item={id,lane,INBOX_ROUTED,atom_expr(&persistent,parts,8)}; CettaInboxCommit c;
    assert(cetta_inbox_commit(w,&item,1,&c)==DURABLE_OK && c.inserted==1); cetta_inbox_window_free(w);
    assert(cetta_inbox_keys("bot",lane,id,ledger,key)==DURABLE_OK);
}
static void incoming_text(int id,int chat,const char *role,const char *text,char key[168]) {
    incoming_update(id,chat,"message",role,text_update(id+100,chat,text),key);
}
static void incoming_tap(int id,int chat,const char *role,const char *data,char key[168]) {
    incoming_update(id,chat,"callback_query",role,tap_update(id,55,chat,data),key);
}
static void command_is(const char *id,const char *head) {
    Atom *v=record("telegram.commands",id);
    if (!v || v->kind!=ATOM_EXPR || !atom_is_symbol(v->expr.elems[0],head)) {
        fprintf(stderr,"command %s: ",id); if (v) atom_print(v,stderr); fprintf(stderr,"\nexpected %s\n",head); abort();
    }
}
static void intent_is(const char *key,const char *payload,const char *reply) {
    Atom *v=record("host.outbox",key);
    if (!v || v->kind!=ATOM_EXPR || v->expr.len!=6 || !atom_eq(v->expr.elems[4],parse(payload)) ||
        !atom_eq(v->expr.elems[5],parse(reply))) {
        fprintf(stderr,"intent %s: ",key); if (v) atom_print(v,stderr); fprintf(stderr,"\nexpected %s %s\n",payload,reply); abort();
    }
}
static void deadline(const char *id,char input[64]) {
    snprintf(input,64,"timer/deadline-%s",id);
    char value[256]; snprintf(value,sizeof(value),
        "(host:timer-event 1 \"timer-%s\" 0 fired 0 0 1 \"%s\" (tg-channel:deadline 1 \"bot\" \"%s\"))",id,id,id);
    put("host.inbox",input,parse(value));
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
    bool imported=cetta_library_import_module(&context,"durable:telegram_channel",
        &program,false,&scratch,&persistent,&registry,1000000,NULL,&error);
    if (!imported) {
        if (error) atom_print(error,stderr);
        abort();
    }
    Space *commands_space=NULL;
    assert(cetta_library_import_module(&context,"durable:telegram_commands",&program,true,
                                       &scratch,&persistent,&registry,1000000,
                                       &commands_space,&error) && commands_space);
    const char *declarations[]={"(tg-cmd:command \"/help\" help \"list these commands\")",
        "(tg-cmd:command \"/stop\" stop \"stop the agent's sends\")",
        "(tg-cmd:command \"/start\" start \"resume the agent's sends\")",
        "(tg-cmd:command \"/engine\" delegated \"show or switch the engine\")",
        "(tg-cmd:agent \"Ada\")","(tg-cmd:bot \"AdaTestBot\")"};
    for (size_t i=0;i<sizeof(declarations)/sizeof(*declarations);++i) space_add(commands_space,parse(declarations[i]));
    space_add(&program,parse("(= (test:count $rows) (if (== $rows ()) 0 (let ($h $t) (decons-atom $rows) (+ (size-atom $h) (test:count $t)))))"));
    space_add(&program,parse("(= (test:buttons $r) (case $r (((tg:some (tg-cmd:reply $a $b (telegram:keyboard $rows))) (test:count $rows)) ($other none))))"));
    /* Commands: a declared name, optionally addressed to this bot, with its
     * arguments. Anything else is ordinary input. */
    pure(parse("(tg-cmd:parse \"/engine cetta\" True)"),"(tg-cmd:call \"/engine\" delegated \"cetta\")");
    pure(parse("(tg-cmd:parse \"  /help  \" True)"),"(tg-cmd:call \"/help\" help \"\")");
    /* In a group every bot reads the message: only an addressed command counts. */
    pure(parse("(tg-cmd:parse \"/help\" False)"),"tg:none");
    pure(parse("(tg-cmd:parse \"/help@AdaTestBot\" False)"),"(tg-cmd:call \"/help\" help \"\")");
    pure(parse("(tg-cmd:parse \"/engine@AdaTestBot  a   b\" True)"),"(tg-cmd:call \"/engine\" delegated \"a b\")");
    pure(parse("(tg-cmd:parse \"/engine@OtherBot cetta\" True)"),"tg:none");
    pure(parse("(tg-cmd:parse \"/unknown\" True)"),"tg:none");
    pure(parse("(tg-cmd:parse \"please /help\" True)"),"tg:none");
    pure(parse("(tg-cmd:parse \"\" True)"),"tg:none");
    pure(parse("(tg-cmd:answer-text \"[\\\"answer\\\",\\\"engine: cetta\\\"]\")"),"(tg:some \"engine: cetta\")");
    pure(parse("(tg-cmd:answer-text \"[\\\"answer\\\",\\\"\\\"]\")"),"(tg:some \"(empty answer)\")");
    pure(parse("(tg-cmd:answer-text \"[]\")"),"tg:none");
    {   /* A long non-ASCII answer is cut in Unicode scalars, never inside one. */
        char *answer=malloc(3801*2+256), *p=answer;
        p+=sprintf(p,"(str:char-length (let (tg:some $t) (tg-cmd:answer-text \"[\\\"answer\\\",\\\"");
        for (int i=0;i<3801;++i) { *p++=(char)0xc4; *p++=(char)0x8d; }
        strcpy(p,"\\\"]\") $t))");
        pure(parse(answer),"3800"); free(answer);
    }
    /* Menus: labels and data needing no escape, 1..8 buttons a row, at most
     * 100 buttons; empty rows mean no buttons; anything else is no answer. */
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"set\\\",\\\"modes\\\",[[[\\\"● iter\\\",\\\"mode:iter\\\"],[\\\"agent\\\",\\\"mode:agent\\\"]]]]\")"),
        "(tg:some (tg-cmd:reply \"set\" \"modes\" (telegram:keyboard (((telegram:button \"● iter\" \"mode:iter\") (telegram:button \"agent\" \"mode:agent\"))))))");
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[]]\")"),"(tg:some (tg-cmd:reply \"\" \"t\" ()))");
    pure(parse("(tg-cmd:reply \"[\\\"answer\\\",\\\"t\\\"]\")"),"(tg:some (tg-cmd:reply \"\" \"t\" ()))");
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[[[\\\"a\\\\\\\"b\\\",\\\"x\\\"]]]]\")"),"tg:none");
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[[]]]\")"),"tg:none");
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[[[\\\"\\\",\\\"x\\\"]]]]\")"),"tg:none");
    pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[[[\\\"a\\\",\\\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\\"]]]]\")"),"tg:none");
    {   /* 8 to a row and 100 in all are the limits. */
        char *menu=malloc(8192), *p;
        for (int rows=12;rows<=13;++rows) for (int extra=4;extra<=5;++extra) {
            p=menu+sprintf(menu,"(test:buttons (tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[");
            for (int r=0;r<rows;++r) {
                p+=sprintf(p,"%s[",r?",":"");
                for (int b=0;b<(r==rows-1?extra:8);++b) p+=sprintf(p,"%s[\\\"b\\\",\\\"d\\\"]",b?",":"");
                *p++=']';
            }
            strcpy(p,"]]\"))");
            const char *expect=rows==12?(extra==4?"92":"93"):(extra==4?"100":"none");
            pure(parse(menu),expect);
        }
        free(menu);
        pure(parse("(tg-cmd:reply \"[\\\"menu\\\",\\\"\\\",\\\"t\\\",[[[\\\"a\\\",\\\"1\\\"],[\\\"a\\\",\\\"2\\\"],[\\\"a\\\",\\\"3\\\"],[\\\"a\\\",\\\"4\\\"],[\\\"a\\\",\\\"5\\\"],[\\\"a\\\",\\\"6\\\"],[\\\"a\\\",\\\"7\\\"],[\\\"a\\\",\\\"8\\\"],[\\\"a\\\",\\\"9\\\"]]]]\")"),"tg:none");
    }
    /* The bot's own username, from an answer about this bot only. */
    pure(parse("(tg-cmd:identity \"{\\\"ok\\\":true,\\\"result\\\":{\\\"id\\\":123456789,\\\"is_bot\\\":true,\\\"username\\\":\\\"AdaTestBot\\\"}}\" \"123456789\")"),"(tg:some \"AdaTestBot\")");
    pure(parse("(tg-cmd:identity \"{\\\"ok\\\":true,\\\"result\\\":{\\\"id\\\":987654321,\\\"is_bot\\\":true,\\\"username\\\":\\\"AdaTestBot\\\"}}\" \"123456789\")"),"tg:none");
    pure(parse("(tg-cmd:identity \"{\\\"ok\\\":false,\\\"result\\\":{\\\"id\\\":123456789,\\\"username\\\":\\\"AdaTestBot\\\"}}\" \"123456789\")"),"tg:none");
    pure(parse("(tg-cmd:identity \"{\\\"ok\\\":true,\\\"result\\\":{\\\"id\\\":123456789}}\" \"123456789\")"),"tg:none");
    pure(parse("(tg-cmd:identity \"not json\" \"123456789\")"),"tg:none");
    pure(parse("(tg-cmd:observation \"11\" \"/engine\" \"cetta\" \"42.0\")"),"\"[\\\"command\\\",\\\"11\\\",\\\"/engine\\\",\\\"cetta\\\",\\\"42.0\\\"]\"");
    pure(parse("(tg-channel:result-json (tg-agent:delivered 9))"),"\"[\\\"delivered\\\",9]\"");
    pure(parse("(tg-channel:result-json (tg-agent:failed 400))"),"\"[\\\"failed\\\",400]\"");
    pure(parse("(tg-channel:result-json (tg-agent:not-sent))"),"\"[\\\"not-sent\\\"]\"");
    pure(parse("(tg-channel:result-json (tg-agent:uncertain transport))"),"\"[\\\"uncertain\\\",\\\"transport\\\"]\"");
    pure(parse("(tg-channel:advances (tg-agent:failed 400))"),"True");
    pure(parse("(tg-channel:advances (tg-agent:not-sent))"),"True");
    pure(parse("(tg-channel:advances (tg-agent:uncertain http-status))"),"True");
    const char *send="(telegram:send-text 1 42 0 0 \"x\" \"plain\")";
    char check[512];
    /* Refused connection, nothing issued: provably not sent. */
    snprintf(check,sizeof(check),"(tg-channel:classify (host:outcome 1 \"e\" \"a\" uncertain (True False 7 0 True 0 False False False) \"\") %s)",send);
    pure(parse(check),"(tg-agent:not-sent)");
    snprintf(check,sizeof(check),"(tg-channel:classify (host:outcome 1 \"e\" \"a\" uncertain (True False 35 0 True 0 False False False) \"\") %s)",send);
    pure(parse(check),"(tg-agent:not-sent)");
    /* Request bytes issued, or a timeout, or an unknown size: uncertain. */
    snprintf(check,sizeof(check),"(tg-channel:classify (host:outcome 1 \"e\" \"a\" uncertain (True False 7 0 True 120 False False False) \"\") %s)",send);
    pure(parse(check),"(tg-agent:uncertain transport)");
    snprintf(check,sizeof(check),"(tg-channel:classify (host:outcome 1 \"e\" \"a\" uncertain (True False 28 0 True 0 False False False) \"\") %s)",send);
    pure(parse(check),"(tg-agent:uncertain transport)");
    snprintf(check,sizeof(check),"(tg-channel:classify (host:outcome 1 \"e\" \"a\" uncertain (True False 7 0 False 0 False False False) \"\") %s)",send);
    pure(parse(check),"(tg-agent:uncertain transport)");
    char dir[]="/tmp/cetta-telegram-channel-XXXXXX"; assert(mkdtemp(dir));
    char db[256]; snprintf(db,sizeof(db),"%s/state.db",dir); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    char input[168],request[64],task[65],worker_input[140],commit[33],sub[180],sub2[180],sub3[180],effect[140],key[64],receipt[65];

    /* Reading a chat never touches its sending actor or waits for cognition. */
    incoming(1,42,input); accept(decide(input,1),1,request);
    assert(!record("host.actors","telegram/bot/42.0") && !record("host.inbox",input));
    published(request,"[\"input\",\"42.0\",\"message\",\"ordinary\",{}]",task);
    char input2[168],request2[64],task2[65];
    incoming(2,42,input2); accept(decide(input2,1),1,request2);
    published(request2,"[\"input\",\"42.0\",\"message\",\"ordinary\",{}]",task2);
    assert(strcmp(task,task2));
    /* An answer only acknowledges its task; it can never act. */
    size_t outbox=count("host.outbox");
    result(task,"[]",worker_input); accept(decide(worker_input,1),0,NULL);
    result(task2,"[[\"send\",\"ignored\",\"plain\"]]",worker_input); accept(decide(worker_input,1),0,NULL);
    assert(count("host.outbox")==outbox && !record("host.inbox",worker_input));

    /* A keyed submission is accepted into its lane's sending state machine. */
    submitted("42.0.00000000000000000001","[[\"send\",\"hello\",\"plain\"],[\"send\",\"second\",\"plain\"]]",sub);
    accept_batch(decide(sub,1),2,commit);
    lane("42.0","(tg-agent:sending \"42.0.00000000000000000001\" 0 2)");
    decided("42.0.00000000000000000001","(tg-channel:decision 1 accepted 2)");
    snprintf(key,sizeof(key),"%s/0",commit); admission(key,TELEGRAM_SEND);
    char second[64]; snprintf(second,sizeof(second),"%s/1",commit); admission(second,TELEGRAM_WAIT);
    /* Submissions to one lane are accepted strictly in key order. */
    submitted("42.0.00000000000000000003","[[\"send\",\"fourth\",\"plain\"]]",sub3);
    submitted("42.0.00000000000000000002","[[\"send\",\"third\",\"plain\"]]",sub2);
    blocked(sub3); blocked(sub2);
    /* Delivery reports back and advances the batch. */
    completed(key,"observed",ok_message,effect); accept_batch(decide(effect,1),1,commit);
    lane("42.0","(tg-agent:sending \"42.0.00000000000000000001\" 1 2)");
    char receipt_request[64]; snprintf(receipt_request,sizeof(receipt_request),"%s/0",commit);
    published(receipt_request,"[\"delivery\",\"42.0.00000000000000000001\",0,2,[\"delivered\",9]]",receipt);
    admission(second,TELEGRAM_SEND);
    /* A definitive API failure is reported and does not wedge the chat. */
    completed(second,"observed","{\"ok\":false,\"error_code\":400,\"description\":\"Bad Request\"}",effect);
    accept_batch(decide(effect,1),1,commit); lane("42.0","(tg-agent:idle)");
    snprintf(receipt_request,sizeof(receipt_request),"%s/0",commit);
    published(receipt_request,"[\"delivery\",\"42.0.00000000000000000001\",1,2,[\"failed\",400]]",receipt);
    blocked(sub3); accept_batch(decide(sub2,1),1,commit);
    lane("42.0","(tg-agent:sending \"42.0.00000000000000000002\" 0 1)");
    /* Uncertain delivery is retained without blocking the next message. */
    snprintf(key,sizeof(key),"%s/0",commit);
    completed(key,"observed","not json",effect); accept_batch(decide(effect,1),1,commit);
    lane("42.0","(tg-agent:idle)");
    snprintf(receipt_request,sizeof(receipt_request),"%s/0",commit);
    published(receipt_request,"[\"delivery\",\"42.0.00000000000000000002\",0,1,[\"uncertain\",\"malformed-api-result\"]]",receipt);
    accept_batch(decide(sub3,1),1,commit); lane("42.0","(tg-agent:sending \"42.0.00000000000000000003\" 0 1)");
    char lane_action[64]; snprintf(lane_action,sizeof(lane_action),"%s/0",commit);

    /* A chat outside the native policy is rejected with a receipt. */
    submitted("99.0.00000000000000000001","[[\"send\",\"x\",\"plain\"]]",sub);
    accept_batch(decide(sub,1),1,commit);
    decided("99.0.00000000000000000001","(tg-channel:decision 1 rejected action-not-permitted)");
    snprintf(receipt_request,sizeof(receipt_request),"%s/0",commit);
    published(receipt_request,"[\"rejected\",\"99.0.00000000000000000001\",\"action-not-permitted\"]",receipt);
    /* A key that names no lane, and a body that is not a command batch. */
    submitted("no-lane","[]",sub); accept_batch(decide(sub,1),1,commit);
    decided("no-lane","(tg-channel:decision 1 rejected malformed-key)");
    submitted("84.0.00000000000000000001","{\"not\":\"a batch\"}",sub); accept_batch(decide(sub,1),1,commit);
    decided("84.0.00000000000000000001","(tg-channel:decision 1 rejected invalid-command-batch)");
    lane("84.0","(tg-agent:idle)");
    /* An empty batch is accepted and leaves the lane idle. */
    submitted("84.0.00000000000000000002","[]",sub); accept_batch(decide(sub,1),0,commit);
    decided("84.0.00000000000000000002","(tg-channel:decision 1 accepted 0)"); lane("84.0","(tg-agent:idle)");
    /* A lane actor written by another program version is never reinterpreted. */
    put("host.actors","telegram/bot/84.0",parse("(host:actor 1 \"telegram-agent/1\" (tg-agent:idle))"));
    submitted("84.0.00000000000000000003","[[\"send\",\"x\",\"plain\"]]",sub); blocked(sub);
    /* Operator commands. The text of a command quotes nothing, so the update
     * needs no escapes; its update ID keys the command. */
    char cmd[168],timer_input[64],reply_key[64];
    size_t commands_before=count("telegram.commands");
    assert(!commands_before);
    /* /help is answered by the service at once, ahead of a busy chat lane. */
    incoming_text(10,42,"operator","/help",cmd); accept_batch(decide(cmd,1),1,commit);
    command_is("10","tg-cmd:served");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:send-text 1 42 0 110 \"/help — list these commands\\n/stop — stop the agent's sends\\n/start — resume the agent's sends\\n/engine — show or switch the engine\" \"plain\")",
        "(tg-channel:command-sent 1 \"bot\" \"10\" reply)");
    admission(lane_action,TELEGRAM_SEND); admission(reply_key,TELEGRAM_SEND);
    /* A delegated command asks the agent and starts the deadline. */
    incoming_text(11,42,"operator","/engine cetta",cmd); accept_batch(decide(cmd,1),2,commit);
    command_is("11","tg-cmd:waiting");
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"11\",\"/engine\",\"cetta\",\"42.0\"]",task);
    snprintf(key,sizeof(key),"%s/1",commit); admission(key,TELEGRAM_FOREIGN);
    /* Answered before the deadline: the answer is the reply; the deadline
     * then finds nothing to do. */
    result(task,"[\"answer\",\"engine: cetta\"]",worker_input); accept_batch(decide(worker_input,1),1,commit);
    command_is("11","tg-cmd:answered");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:send-text 1 42 0 111 \"engine: cetta\" \"plain\")","(tg-channel:command-sent 1 \"bot\" \"11\" reply)");
    admission(reply_key,TELEGRAM_SEND);
    deadline("11",timer_input); accept_batch(decide(timer_input,1),0,commit); command_is("11","tg-cmd:answered");
    /* The deadline first: the service's notice is the reply, and the answer
     * that arrives later edits it. */
    incoming_text(12,42,"operator","/engine",cmd); accept_batch(decide(cmd,1),2,commit);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"12\",\"/engine\",\"\",\"42.0\"]",task);
    deadline("12",timer_input); accept_batch(decide(timer_input,1),1,commit);
    command_is("12","tg-cmd:fell-back");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:send-text 1 42 0 112 \"No answer from Ada yet. This message will be updated when it arrives.\" \"plain\")",
        "(tg-channel:command-sent 1 \"bot\" \"12\" fallback)");
    completed(reply_key,"observed",ok_message,effect); accept_batch(decide(effect,1),0,commit);
    command_is("12","tg-cmd:fell-back"); assert(record("telegram.commands","12")->expr.elems[5]->ground.ival==9);
    result(task,"[\"answer\",\"engine: petta\"]",worker_input); accept_batch(decide(worker_input,1),1,commit);
    command_is("12","tg-cmd:edited");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:edit-text 1 42 9 \"engine: petta\" \"plain\")","(tg-channel:command-sent 1 \"bot\" \"12\" edit)");
    /* The answer arrives while the notice is still being sent: it is kept,
     * and the notice's delivery edits it in. */
    incoming_text(13,42,"operator","/engine",cmd); accept_batch(decide(cmd,1),2,commit);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"13\",\"/engine\",\"\",\"42.0\"]",task);
    deadline("13",timer_input); accept_batch(decide(timer_input,1),1,commit);
    char notice[64]; snprintf(notice,sizeof(notice),"%s/0",commit);
    result(task,"[\"answer\",\"late\"]",worker_input); accept_batch(decide(worker_input,1),0,commit);
    command_is("13","tg-cmd:edit-pending");
    completed(notice,"observed",ok_message,effect); accept_batch(decide(effect,1),1,commit);
    command_is("13","tg-cmd:edited");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:edit-text 1 42 9 \"late\" \"plain\")","(tg-channel:command-sent 1 \"bot\" \"13\" edit)");
    /* /stop holds the agent's sends at once and asks the agent to stop: a
     * chat's pending action waits and a new submission is rejected. The
     * agent's answer is the reply, so the reply says what the agent did.
     * /start lets the action go and is answered by the agent the same way. */
    incoming_text(14,42,"operator","/stop",cmd); accept_batch(decide(cmd,1),2,commit);
    command_is("14","tg-cmd:waiting");
    assert(atom_eq(record("telegram.lifecycle","latch"),parse("(tg-cmd:latch stopped)")));
    admission(lane_action,TELEGRAM_WAIT);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"14\",\"/stop\",\"\",\"42.0\"]",task);
    submitted("84.1.00000000000000000001","[[\"send\",\"x\",\"plain\"]]",sub); accept_batch(decide(sub,1),1,commit);
    decided("84.1.00000000000000000001","(tg-channel:decision 1 rejected stopped)");
    result(task,"[\"answer\",\"Stopped. Cognition is paused.\"]",worker_input); accept_batch(decide(worker_input,1),1,commit);
    command_is("14","tg-cmd:answered");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:send-text 1 42 0 114 \"Stopped. Cognition is paused.\" \"plain\")",
        "(tg-channel:command-sent 1 \"bot\" \"14\" reply)");
    admission(reply_key,TELEGRAM_SEND);
    deadline("14",timer_input); accept_batch(decide(timer_input,1),0,commit); command_is("14","tg-cmd:answered");
    incoming_text(15,42,"operator","/start",cmd); accept_batch(decide(cmd,1),2,commit);
    command_is("15","tg-cmd:waiting");
    assert(atom_eq(record("telegram.lifecycle","latch"),parse("(tg-cmd:latch running)")));
    admission(lane_action,TELEGRAM_SEND);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"15\",\"/start\",\"\",\"42.0\"]",task);
    result(task,"[\"answer\",\"Started.\"]",worker_input); accept_batch(decide(worker_input,1),1,commit);
    command_is("15","tg-cmd:answered");
    /* The same text from someone who is not an operator is ordinary input. */
    incoming_text(16,42,"ordinary","/help",cmd); accept_batch(decide(cmd,1),1,commit);
    assert(!record("telegram.commands","16"));
    snprintf(request,sizeof(request),"%s/0",commit); admission(request,TELEGRAM_WORKER);
    /* Taps. An operator's tap asks the agent and starts the deadline; the
     * answer answers the tap and redraws its menu. */
    const char *menu="[\"menu\",\"mode: iter\",\"modes\",[[[\"● iter\",\"mode:iter\"],[\"agent\",\"mode:agent\"]]]]";
    const char *drawn="(telegram:keyboard (((telegram:button \"● iter\" \"mode:iter\") (telegram:button \"agent\" \"mode:agent\"))))";
    char expected[512];
    incoming_tap(17,42,"operator","mode:iter",cmd); accept_batch(decide(cmd,1),2,commit);
    command_is("17","tg-cmd:tapped");
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"callback\",\"17\",\"mode:iter\",\"42.0\",55]",task);
    result(task,menu,worker_input); accept_batch(decide(worker_input,1),2,commit);
    command_is("17","tg-cmd:answered");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:answer-callback 1 42 \"q17\" \"mode: iter\")","(tg-channel:command-sent 1 \"bot\" \"17\" acknowledge)");
    admission(reply_key,TELEGRAM_SEND);
    snprintf(reply_key,sizeof(reply_key),"%s/1",commit);
    snprintf(expected,sizeof(expected),"(telegram:edit-text 1 42 55 \"modes\" \"plain\" %s)",drawn);
    intent_is(reply_key,expected,"(tg-channel:command-sent 1 \"bot\" \"17\" redraw)");
    admission(reply_key,TELEGRAM_SEND);
    deadline("17",timer_input); accept_batch(decide(timer_input,1),0,commit); command_is("17","tg-cmd:answered");
    /* The deadline first: the service answers the tap, and the late answer
     * only redraws the menu. */
    incoming_tap(18,42,"operator","mode:agent",cmd); accept_batch(decide(cmd,1),2,commit);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"callback\",\"18\",\"mode:agent\",\"42.0\",55]",task);
    deadline("18",timer_input); accept_batch(decide(timer_input,1),1,commit);
    command_is("18","tg-cmd:tap-late");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:answer-callback 1 42 \"q18\" \"No answer from Ada yet.\")","(tg-channel:command-sent 1 \"bot\" \"18\" late)");
    admission(reply_key,TELEGRAM_SEND);
    result(task,menu,worker_input); accept_batch(decide(worker_input,1),1,commit);
    command_is("18","tg-cmd:edited");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,expected,"(tg-channel:command-sent 1 \"bot\" \"18\" redraw)");
    /* Anyone else's tap is answered at once and never reaches the agent. */
    size_t tasks_before=count("host.worker-tasks");
    incoming_tap(19,42,"ordinary","mode:iter",cmd); accept_batch(decide(cmd,1),1,commit);
    command_is("19","tg-cmd:served");
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    intent_is(reply_key,"(telegram:answer-callback 1 42 \"q19\" \"These buttons are for operators.\")","(tg-channel:command-sent 1 \"bot\" \"19\" refusal)");
    admission(reply_key,TELEGRAM_SEND);
    /* A tap without data is ignored. */
    incoming_tap(20,42,"operator",NULL,cmd); accept_batch(decide(cmd,1),0,commit);
    assert(!record("telegram.commands","20") && count("host.worker-tasks")==tasks_before);
    /* A command answered with a menu is sent with its buttons. */
    incoming_text(21,42,"operator","/engine",cmd); accept_batch(decide(cmd,1),2,commit);
    snprintf(request,sizeof(request),"%s/0",commit); published(request,"[\"command\",\"21\",\"/engine\",\"\",\"42.0\"]",task);
    result(task,menu,worker_input); accept_batch(decide(worker_input,1),1,commit);
    snprintf(reply_key,sizeof(reply_key),"%s/0",commit);
    snprintf(expected,sizeof(expected),"(telegram:send-text 1 42 0 121 \"modes\" \"plain\" %s)",drawn);
    intent_is(reply_key,expected,"(tg-channel:command-sent 1 \"bot\" \"21\" reply)");
    admission(reply_key,TELEGRAM_SEND);
    /* Committed work survives a reopen. */
    cetta_durable_close(store); assert(cetta_durable_open(db,NULL,&store)==DURABLE_OK);
    lane("42.0","(tg-agent:sending \"42.0.00000000000000000003\" 0 1)");
    printf("Telegram channel: %u pure policy checks; deliveries without waiting, acknowledgment-only answers, keyed submissions, lane order, receipts, definitive failure progress, uncertainty without replay or lane blocking, rejections, commands answered by the service or raced against their deadline, menus, taps raced against their deadline, and stop/start at dispatch passed\n",policy_checks);
    cetta_durable_close(store); unlink(db); char path[300]; snprintf(path,sizeof(path),"%s.lock",db); unlink(path); snprintf(path,sizeof(path),"%s.checkpoint",db); unlink(path); rmdir(dir);
    cetta_library_context_free(&context); eval_set_library_context(NULL); registry_free(&registry); space_free(&program);
    arena_free(&scratch); arena_free(&persistent); var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
}
