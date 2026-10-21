#define _POSIX_C_SOURCE 200809L
#include "atom.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "durable_value.h"
#include "telegram_intake.h"
#include "eval.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static unsigned checks;
static Atom *parse(const char *text) {
    size_t pos=0; Atom *a=parse_sexpr(&persistent,text,&pos);
    assert(a && pos==strlen(text)); return a;
}
static Atom *run(Atom *call, int fuel, EvalOutcome *out) {
    eval_outcome_init(out);
    eval_top_speculative(&context,&program,&scratch,&persistent,&registry,call,fuel,out);
    if (out->completion!=CETTA_EVAL_COMPLETE || out->effect_denials || out->results.len!=1) {
        fprintf(stderr,"policy evaluation: completion %d, denials %llu, results %llu, steps %llu\n",
            out->completion,(unsigned long long)out->effect_denials,(unsigned long long)out->results.len,
            (unsigned long long)out->steps_spent);
        fprintf(stderr,"check %u call: ",checks); atom_print(call,stderr); fputc('\n',stderr);
        for (CettaCount i=0;i<out->results.len;++i) { atom_print(out->results.items[i],stderr); fputc('\n',stderr); }
        abort();
    }
    return out->results.items[0];
}
static void check(const char *expr, const char *expected) {
    EvalOutcome out; Atom *a=run(parse(expr),1000000,&out);
    if (!atom_eq(a,parse(expected))) {
        fprintf(stderr,"check %u %s\nexpected: %s\nactual: ",checks,expr,expected); atom_print(a,stderr); abort();
    }
    ++checks; eval_outcome_free(&out);
}
static Atom *decode(const char *body, const char *policy, EvalOutcome *out) {
    /* Provider text enters as a string, never as MeTTa source. */
    Atom *args[]={atom_symbol(&persistent,"telegram:decode"),atom_string(&persistent,body),parse(policy)};
    return run(atom_expr(&persistent,args,3),1000000,out);
}
static const char *policy="(telegram:policy 1 (-100 42) False (7) (8))";
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static bool text(const Atom *a, const char *s) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING && !strcmp(a->ground.sval,s);
}
static Atom *items(Atom *a, size_t n) {
    if (!form(a,"telegram:batch",3) || a->expr.elems[2]->kind!=ATOM_EXPR || a->expr.elems[2]->expr.len!=n) {
        fprintf(stderr,"expected batch of %zu: ",n); atom_print(a,stderr); abort();
    }
    return a->expr.elems[2];
}
static void route(const char *body, const char *config, const char *lane, const char *disposition,
                  const char *kind, int64_t chat, int64_t thread, int64_t sender, const char *role) {
    EvalOutcome out; Atom *a=items(decode(body,config,&out),1)->expr.elems[0];
    assert(form(a,"tg:item",5) && text(a->expr.elems[2],lane) && atom_is_symbol(a->expr.elems[3],disposition));
    a=a->expr.elems[4];
    assert(form(a,"telegram:input",8) && text(a->expr.elems[2],kind));
    assert(a->expr.elems[3]->ground.ival==chat && a->expr.elems[4]->ground.ival==thread &&
           a->expr.elems[5]->ground.ival==sender && atom_is_symbol(a->expr.elems[6],role));
    ++checks; eval_outcome_free(&out);
}
static void response(const char *body, const char *expected) {
    EvalOutcome out; Atom *a=decode(body,policy,&out);
    if (!atom_eq(a,parse(expected))) {
        fprintf(stderr,"response check %u\nexpected: %s\nactual: ",checks,expected); atom_print(a,stderr); abort();
    }
    ++checks; eval_outcome_free(&out);
}
static void long_message(void) {
    /* Production-sized text, not only tiny protocol examples. */
    char message[5000], content[4097]; memset(content,'x',4096); content[4096]=0;
    snprintf(message,sizeof(message),"{\"ok\":true,\"result\":[{\"update_id\":500,\"message\":{\"chat\":{\"id\":42,\"type\":\"private\"},\"text\":\"%s\"}}]}",content);
    EvalOutcome out; clock_t start=clock();
    Atom *large_result=decode(message,policy,&out);
    if (!form(large_result,"telegram:batch",3)) {
        EvalOutcome diagnostic; Atom *args[]={atom_symbol(&persistent,"json:inspect"),atom_string(&persistent,message)};
        Atom *why=run(atom_expr(&persistent,args,2),1000000,&diagnostic);
        if (!form(why,"JsonObjectV1",2)) atom_print(why,stderr);
        eval_outcome_free(&diagnostic);
    }
    items(large_result,1);
    printf("4096-character message: %.3f CPU s, %llu evaluator steps\n",
        (double)(clock()-start)/CLOCKS_PER_SEC,(unsigned long long)out.steps_spent);
    eval_outcome_free(&out); ++checks;
}
static void record_poll(CettaInboxWindow *w, const char *body, const char *recorded_source, int64_t status) {
    Atom *metadata[]={atom_bool(&persistent,true),atom_bool(&persistent,false),atom_int(&persistent,0),
        atom_int(&persistent,status),atom_bool(&persistent,true),atom_int(&persistent,100),
        atom_bool(&persistent,false),atom_bool(&persistent,false),atom_bool(&persistent,false)};
    Atom *fields[]={atom_symbol(&persistent,"host:poll"),atom_int(&persistent,1),
        atom_string(&persistent,recorded_source),atom_int(&persistent,cetta_inbox_offset(w)),
        atom_symbol(&persistent,"observed"),atom_expr(&persistent,metadata,9),atom_string(&persistent,body)};
    unsigned char *data=NULL; size_t size=0;
    assert(cetta_durable_value_encode(atom_expr(&persistent,fields,7),&data,&size)==DURABLE_OK);
    assert(cetta_inbox_record_poll(w,data,size)==DURABLE_OK);
    free(data); cetta_inbox_window_free(w);
}
static void poll(CettaDurableStore *store, const char *source, const char *body,
                 const char *recorded_source, int64_t status) {
    CettaInboxWindow *w=NULL;
    assert(cetta_inbox_begin(store,source,&w)==DURABLE_OK);
    record_poll(w,body,recorded_source,status);
}
static void intake_checks(const char *body, const char *config) {
    char path[]="/tmp/cetta-telegram-policy-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    CettaDurableStore *store=NULL; assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    CettaHostProgram trusted={"telegram/intake/1",&program,&context};
    CettaTelegramIntake *a=NULL,*b=NULL; CettaInboxCommit commit;
    poll(store,"bot",body,"bot",200);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION && commit.next_offset==-1);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),30)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),0)==DURABLE_INVALID);
    assert(!cetta_telegram_intake_outcome(a));
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&b)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(b,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_OK && commit.inserted==1 && commit.next_offset==11);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_CONFLICT);
    assert(cetta_telegram_intake_commit(b,&commit)==DURABLE_CONFLICT);
    cetta_telegram_intake_free(a); cetta_telegram_intake_free(b);
    /* New policy refuses this chat and revokes the sender's operator status.
     * A replay still reuses the first classification; it is not new input. */
    poll(store,"bot",body,"bot",200);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse("(telegram:policy 1 () False () ())"),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_OK && commit.inserted==0 && commit.next_offset==11);
    cetta_telegram_intake_free(a); ++checks;
    /* The same ID with a changed provider payload cannot silently replace it. */
    poll(store,"bot","{\"ok\":true,\"result\":[{\"update_id\":10,\"future\":true}]}","bot",200);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_CORRUPT && commit.next_offset==-1);
    cetta_telegram_intake_free(a); ++checks;
    poll(store,"wrong-source",body,"different",200);
    assert(cetta_telegram_intake_begin(store,"wrong-source",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_CORRUPT);
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    cetta_telegram_intake_free(a); ++checks;
    poll(store,"http-failure",body,"http-failure",500);
    assert(cetta_telegram_intake_begin(store,"http-failure",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(atom_eq(cetta_telegram_intake_outcome(a)->results.items[0],parse("(telegram:hold 1 transport-outcome)")));
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    cetta_telegram_intake_free(a); ++checks;
    poll(store,"rate","{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":3}}","rate",429);
    assert(cetta_telegram_intake_begin(store,"rate",trusted.version,&a)==DURABLE_OK);
    CettaHostProgram wrong=trusted; wrong.version="different";
    assert(cetta_telegram_intake_evaluate(a,&wrong,parse(config),1000000)==DURABLE_VERSION);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse("(telegram:policy 1 (bad) False () ())"),1000000)==DURABLE_INVALID);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(atom_eq(cetta_telegram_intake_outcome(a)->results.items[0],parse("(telegram:retry 1 3 rate-limit)")));
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    cetta_telegram_intake_free(a); ++checks;
    /* A swallowed effect denial is still host-visible and blocks intake. */
    Atom *original_rule=NULL;
    Space *rule_space=NULL;
    for (uint32_t branch=0; branch<=space_dependency_count(&program) && !original_rule; ++branch) {
        Space *candidate=branch ? space_dependency_at(&program,branch-1) : &program;
        for (CettaCount i=0;i<space_length64(candidate);++i) {
            Atom *rule=space_get_at64(candidate,i);
            if (form(rule,"=",3) && form(rule->expr.elems[1],"telegram:poll",3)) {
                original_rule=rule; rule_space=candidate; break;
            }
        }
    }
    assert(original_rule && rule_space); space_remove(rule_space,original_rule);
    Atom *denied=parse("(= (telegram:poll $x $p) (let $ignored (collapse (superpose (kept (println! denied)))) (telegram:batch 1 ())))");
    space_add(rule_space,denied);
    assert(cetta_telegram_intake_begin(store,"rate",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(config),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_outcome(a)->effect_denials>0);
    assert(cetta_telegram_intake_outcome(a)->results.len==1 &&
        atom_eq(cetta_telegram_intake_outcome(a)->results.items[0],parse("(telegram:batch 1 ())")));
    assert(cetta_telegram_intake_commit(a,&commit)==DURABLE_PRECONDITION);
    cetta_telegram_intake_free(a); space_remove(rule_space,denied); space_add(rule_space,original_rule); ++checks;
    cetta_durable_close(store);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    CettaInboxWindow *w=NULL;
    assert(cetta_inbox_begin(store,"rate",&w)==DURABLE_PRECONDITION);
    assert(cetta_inbox_recover_poll(store,"bot",&w)==DURABLE_OK && cetta_inbox_offset(w)==11);
    cetta_inbox_window_free(w); cetta_durable_close(store); unlink(path);
    char side[300]; snprintf(side,sizeof(side),"%s-wal",path); unlink(side);
    snprintf(side,sizeof(side),"%s-shm",path); unlink(side); ++checks;
}
static void recovery_response(int code, int status, bool cancelled, const char *kind,
    const char *body, int64_t failures, int64_t now, const char *expected) {
    Atom *meta[]={atom_bool(&persistent,true),atom_bool(&persistent,cancelled),atom_int(&persistent,code),
        atom_int(&persistent,status),atom_bool(&persistent,true),atom_int(&persistent,100),
        atom_bool(&persistent,false),atom_bool(&persistent,false),atom_bool(&persistent,false)};
    Atom *poll[]={atom_symbol(&persistent,"host:poll"),atom_int(&persistent,1),atom_string(&persistent,"bot"),
        atom_int(&persistent,11),atom_symbol(&persistent,kind),atom_expr(&persistent,meta,9),atom_string(&persistent,body)};
    Atom *call[]={atom_symbol(&persistent,"telegram:recover-poll"),atom_expr(&persistent,poll,7),parse(policy),
        atom_int(&persistent,failures),atom_int(&persistent,now)};
    EvalOutcome out; Atom *result=run(atom_expr(&persistent,call,5),1000000,&out);
    if (!atom_eq(result,parse(expected))) { atom_print(result,stderr); fprintf(stderr," expected %s\n",expected); abort(); }
    eval_outcome_free(&out); ++checks;
}
static CettaTelegramIntake *retry_ticket(CettaDurableStore *store, const CettaHostProgram *trusted,
                                        const char *source, int64_t now) {
    CettaTelegramIntake *t=NULL;
    assert(cetta_telegram_intake_begin(store,source,trusted->version,&t)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate_recovery(t,trusted,parse(policy),now,1000000)==DURABLE_OK);
    return t;
}
static Atom *stored(CettaDurableStore *store, const char *space, size_t count) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,space,&s)==DURABLE_OK && s.count==count);
    Atom *a=NULL;
    if (count) assert(cetta_durable_value_decode(&persistent,s.records[0].data,s.records[0].size,&a)==DURABLE_OK);
    cetta_durable_snapshot_free(&s); return a;
}
static void recovery_checks(void) {
    const char *rate="{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":3}}";
    const char *empty="{\"ok\":true,\"result\":[]}";
    recovery_response(0,429,false,"observed",rate,0,1000,"(telegram:retry-at 1 4000 rate-limit)");
    recovery_response(0,200,false,"observed",rate,0,1000,"(telegram:retry-at 1 4000 rate-limit)");
    recovery_response(0,429,false,"observed",rate,7,1000,"(telegram:retry-at 1 4000 (telegram:degraded rate-limit))");
    recovery_response(0,429,false,"observed",rate,0,INT64_MAX-1000,"(telegram:hold 1 deadline-overflow)");
    recovery_response(0,429,false,"observed",empty,0,1000,"(telegram:hold 1 invalid-retry-delay)");
    recovery_response(0,503,false,"observed","unavailable",0,1000,"(telegram:retry-at 1 2000 transient-poll)");
    recovery_response(0,503,false,"observed","unavailable",6,1000,"(telegram:retry-at 1 65000 transient-poll)");
    const int transient[]={5,6,7,16,18,28,35,52,55,56,92};
    for (size_t i=0;i<sizeof(transient)/sizeof(*transient);++i)
        recovery_response(transient[i],0,false,"observed","",1,1000,"(telegram:retry-at 1 3000 transient-poll)");
    recovery_response(52,0,false,"uncertain","",0,1000,"(telegram:retry-at 1 2000 transient-poll)");
    recovery_response(35,0,false,"uncertain","",0,1000,"(telegram:retry-at 1 2000 transient-poll)");
    recovery_response(35,0,true,"uncertain","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(42,0,true,"shutdown","",0,1000,"(telegram:retry-at 1 2000 host-restart)");
    recovery_response(42,0,true,"uncertain","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(42,0,true,"privacy-suppressed","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(28,0,false,"uncertain","",1000000,1000,"(telegram:retry-at 1 65000 (telegram:degraded transient-poll))");
    recovery_response(60,0,false,"observed","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(28,0,true,"observed","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(28,0,false,"minimal","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(0,401,false,"observed","",0,1000,"(telegram:hold 1 transport-outcome)");
    recovery_response(0,409,false,"observed","",0,1000,"(telegram:hold 1 conflicting-poller)");
    recovery_response(0,200,false,"observed","{",0,1000,"(telegram:hold 1 malformed-json)");
    recovery_response(0,200,false,"observed",empty,0,1000,"(telegram:ready 1)");

    char path[]="/tmp/cetta-poll-recovery-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd);
    CettaDurableStore *store=NULL; assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    CettaHostProgram trusted={"telegram/intake/1",&program,&context};
    CettaTelegramIntake *a=NULL,*b=NULL; CettaInboxWindow *w=NULL; CettaInboxCommit batch; int64_t revision;
    poll(store,"bot","{\"ok\":true,\"result\":[{\"update_id\":10}]}","bot",200);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(policy),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&batch)==DURABLE_OK && batch.next_offset==11); cetta_telegram_intake_free(a);
    poll(store,"bot",rate,"bot",429);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&b)==DURABLE_OK);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_PRECONDITION);
    assert(cetta_telegram_intake_evaluate_recovery(a,&trusted,parse(policy),1000,15)==DURABLE_OK);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_PRECONDITION);
    assert(stored(store,"host.poll-clock",1)->expr.elems[4]->ground.ival==1000);
    assert(cetta_telegram_intake_evaluate_recovery(b,&trusted,parse(policy),2000,1000000)==DURABLE_CONFLICT);
    cetta_telegram_intake_free(b); cetta_telegram_intake_free(a); cetta_durable_close(store);
    /* Crash after clock observation, before decision: recovery reuses it. */
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    a=retry_ticket(store,&trusted,"bot",9000);
    assert(atom_eq(cetta_telegram_intake_outcome(a)->results.items[0],parse("(telegram:retry-at 1 4000 rate-limit)")));
    assert(cetta_telegram_intake_commit(a,&batch)==DURABLE_PRECONDITION);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK);
    cetta_telegram_intake_free(a); stored(store,"host.polls",0); stored(store,"host.poll-clock",0);
    Atom *control=stored(store,"host.poll-control",1);
    assert(form(control,"host:poll-control",11) && control->expr.elems[3]->ground.ival==11 &&
        control->expr.elems[5]->ground.ival==1 && control->expr.elems[7]->ground.ival==4000);
    assert(cetta_inbox_begin(store,"bot",&w)==DURABLE_PRECONDITION);
    assert(cetta_inbox_begin_at(store,"bot",3999,&w)==DURABLE_PRECONDITION);
    /* A damaged control cannot silently substitute a different deadline. */
    CettaDurableScope control_scope={DURABLE_KEY,"host.poll-control","bot"}; CettaDurableObservation *control_view=NULL;
    assert(cetta_durable_observe(store,&control_scope,1,&control_view)==DURABLE_OK);
    control->expr.elems[7]=atom_int(&persistent,5000);
    unsigned char *bad_control=NULL; size_t bad_size=0;
    assert(cetta_durable_value_encode(control,&bad_control,&bad_size)==DURABLE_OK);
    CettaDurableOp damaged={DURABLE_REPLACE,"host.poll-control","bot",bad_control,bad_size};
    assert(cetta_durable_commit_observed(store,control_view,&damaged,1,&revision)==DURABLE_OK);
    cetta_durable_observation_free(control_view); free(bad_control);
    assert(cetta_inbox_begin_at(store,"bot",6000,&w)==DURABLE_CORRUPT && !w);
    control->expr.elems[7]=atom_int(&persistent,4000);
    assert(cetta_durable_observe(store,&control_scope,1,&control_view)==DURABLE_OK);
    assert(cetta_durable_value_encode(control,&bad_control,&bad_size)==DURABLE_OK);
    damaged.data=bad_control; damaged.size=bad_size;
    assert(cetta_durable_commit_observed(store,control_view,&damaged,1,&revision)==DURABLE_OK);
    cetta_durable_observation_free(control_view); free(bad_control); ++checks;

    assert(cetta_inbox_begin_at(store,"other",0,&w)==DURABLE_OK); cetta_inbox_window_free(w);
    cetta_durable_close(store); ++checks;
    /* A separate process records a new failure and exits immediately after the
     * complete scheduling transaction. The parent resumes the same cursor. */
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
        assert(cetta_inbox_begin_at(store,"bot",4000,&w)==DURABLE_OK && cetta_inbox_offset(w)==11);
        record_poll(w,"temporary","bot",503);
        a=retry_ticket(store,&trusted,"bot",5000);
        assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK);
        _exit(0);
    }
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    control=stored(store,"host.poll-control",1);
    assert(control->expr.elems[5]->ground.ival==2 && control->expr.elems[7]->ground.ival==7000);
    assert(cetta_inbox_begin_at(store,"bot",6999,&w)==DURABLE_PRECONDITION);
    assert(cetta_inbox_begin_at(store,"bot",7000,&w)==DURABLE_OK && cetta_inbox_offset(w)==11);
    record_poll(w,empty,"bot",200);
    a=retry_ticket(store,&trusted,"bot",7000);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_PRECONDITION);
    assert(cetta_telegram_intake_evaluate(a,&trusted,parse(policy),1000000)==DURABLE_OK);
    assert(cetta_telegram_intake_commit(a,&batch)==DURABLE_OK && batch.next_offset==11);
    cetta_telegram_intake_free(a); stored(store,"host.poll-control",0); stored(store,"host.poll-clock",0); ++checks;
    /* A hold is durable and cannot increment failures repeatedly for one poll. */
    poll(store,"bot","{","bot",200); a=retry_ticket(store,&trusted,"bot",8000);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK); cetta_telegram_intake_free(a);
    control=stored(store,"host.poll-control",1);
    assert(control->expr.elems[5]->ground.ival==1 && control->expr.elems[7]->ground.ival==-1);
    stored(store,"host.polls",1); stored(store,"host.poll-clock",0);
    assert(cetta_telegram_intake_begin(store,"bot",trusted.version,&a)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate_recovery(a,&trusted,parse(policy),9000,1000000)==DURABLE_PRECONDITION);
    assert(!cetta_telegram_intake_outcome(a)); cetta_telegram_intake_free(a);
    assert(cetta_inbox_begin_at(store,"bot",INT64_MAX,&w)==DURABLE_PRECONDITION); ++checks;
    /* A changed declared control conflicts; unrelated traffic does not. */
    assert(cetta_inbox_begin_at(store,"conflict",1000,&w)==DURABLE_OK);
    record_poll(w,rate,"conflict",429); a=retry_ticket(store,&trusted,"conflict",1000);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK); cetta_telegram_intake_free(a);
    assert(cetta_inbox_begin_at(store,"conflict",4000,&w)==DURABLE_OK);
    record_poll(w,rate,"conflict",429); a=retry_ticket(store,&trusted,"conflict",4000);
    CettaDurableScope scope={DURABLE_KEY,"host.poll-control","conflict"}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&scope,1,&o)==DURABLE_OK);
    const CettaDurableRecord *record=&cetta_durable_observation_view(o,0)->records[0];
    CettaDurableOp op={DURABLE_REPLACE,"host.poll-control","conflict",record->data,record->size};
    assert(cetta_durable_commit_observed(store,o,&op,1,&revision)==DURABLE_OK); cetta_durable_observation_free(o);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_CONFLICT); cetta_telegram_intake_free(a);
    a=retry_ticket(store,&trusted,"conflict",9000);
    poll(store,"unrelated",empty,"unrelated",200);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK); cetta_telegram_intake_free(a); ++checks;
    /* Counts survive restart; sustained outages get capped backoff and a
     * degraded status, without requiring manual revival of a safe poll. */
    int64_t now=1000;
    for (int n=1;n<=8;++n) {
        assert(cetta_inbox_begin_at(store,"exhaust",now,&w)==DURABLE_OK);
        record_poll(w,"temporary","exhaust",503); a=retry_ticket(store,&trusted,"exhaust",now);
        const Atom *decision=cetta_telegram_intake_outcome(a)->results.items[0];
        assert(form(decision,"telegram:retry-at",4));
        int64_t due=decision->expr.elems[2]->ground.ival;
        assert(due>now && due-now<=64000);
        if (n==8) assert(form(decision->expr.elems[3],"telegram:degraded",2));
        now=due;
        assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK); cetta_telegram_intake_free(a);
        cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    }
    assert(cetta_inbox_begin_at(store,"exhaust",now-1,&w)==DURABLE_PRECONDITION);
    assert(cetta_inbox_begin_at(store,"exhaust",now,&w)==DURABLE_OK); cetta_inbox_window_free(w); ++checks;
    /* A valid-looking recovery answer cannot hide a denied effect. */
    Atom *original_rule=NULL;
    Space *rule_space=NULL;
    for (uint32_t branch=0; branch<=space_dependency_count(&program) && !original_rule; ++branch) {
        Space *candidate=branch ? space_dependency_at(&program,branch-1) : &program;
        for (CettaCount i=0;i<space_length64(candidate);++i) {
            Atom *r=space_get_at64(candidate,i);
            if (form(r,"=",3) && form(r->expr.elems[1],"telegram:recover-poll",5)) {
                original_rule=r; rule_space=candidate; break;
            }
        }
    }
    assert(original_rule && rule_space); space_remove(rule_space,original_rule);
    Atom *denied=parse("(= (telegram:recover-poll $p $c $n $t) (let $ignored (collapse (superpose (kept (println! denied)))) (telegram:retry-at 1 9000 reason)))");
    space_add(rule_space,denied); poll(store,"denied-retry",rate,"denied-retry",429);
    a=retry_ticket(store,&trusted,"denied-retry",1000);
    assert(cetta_telegram_intake_outcome(a)->effect_denials>0);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_PRECONDITION);
    cetta_telegram_intake_free(a); space_remove(rule_space,denied); space_add(rule_space,original_rule); ++checks;
    cetta_durable_close(store); unlink(path);
    /* Admission failure cannot remove the pending response or its clock. */
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK); poll(store,"quota",rate,"quota",429);
    a=retry_ticket(store,&trusted,"quota",1000); cetta_telegram_intake_free(a);
    CettaDurableSnapshot snap; assert(cetta_durable_snapshot(store,"host.polls",&snap)==DURABLE_OK);
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.record_bytes=snap.records[0].size;
    cetta_durable_snapshot_free(&snap); cetta_durable_close(store);
    assert(cetta_durable_open(path,&limits,&store)==DURABLE_OK);
    a=retry_ticket(store,&trusted,"quota",9000);
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_LIMIT && revision==-1); cetta_telegram_intake_free(a);
    stored(store,"host.polls",1); stored(store,"host.poll-clock",1); stored(store,"host.poll-control",0);
    cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    a=retry_ticket(store,&trusted,"quota",9000);
    assert(atom_eq(cetta_telegram_intake_outcome(a)->results.items[0],parse("(telegram:retry-at 1 4000 rate-limit)")));
    assert(cetta_telegram_intake_commit_recovery(a,&revision)==DURABLE_OK); cetta_telegram_intake_free(a); ++checks;
    cetta_durable_close(store); unlink(path);
    char side[300]; snprintf(side,sizeof(side),"%s-wal",path); unlink(side);
    snprintf(side,sizeof(side),"%s-shm",path); unlink(side);
}
int main(int argc,char **argv) {
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--long-message")));
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
    if (!cetta_library_import_module(&context,"durable:telegram",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error)) {
        if (error) atom_print(error,stderr);
        abort();
    }
    if (argc==2) { long_message(); goto cleanup; }
    check("(telegram:integer (JsonNumberV1 \"-9223372036854775808\"))","(tg:integer -9223372036854775808)");
    check("(telegram:integer (JsonNumberV1 \"9223372036854775807\"))","(tg:integer 9223372036854775807)");
    check("(telegram:integer (JsonNumberV1 \"9223372036854775808\"))","tg:bad");
    check("(telegram:integer (JsonNumberV1 \"-9223372036854775809\"))","tg:bad");
    check("(telegram:integer (JsonNumberV1 \"7e0\"))","tg:bad");
    check("(telegram:integer (JsonNumberV1 \"1.0\"))","tg:bad");
    check("(json:inspect \"null\")","JsonNullV1");
    check("(sort-atom (b a))","(a b)");
    check("(unique-atom (a a))","(a)");
    check("(unique-atom ())","()");
    check("(sort-atom ())","()");
    check("(telegram:member-keys ((JsonMemberV1 0 k v span)))","(k)");
    check("(telegram:member k ((JsonMemberV1 0 k v span)))","(tg:some v)");
    check("(telegram:normalize (JsonBoolV1 True) 0)","(tg:ok (JsonBoolV1 True))");
    check("(telegram:object-members ((JsonMemberV1 0 k (JsonBoolV1 True) span)) (k) 0 1)","(tg:ok ((JsonMemberV1 0 k (JsonBoolV1 True) JsonNoSourceSpanV1)))");
    check("(telegram:normalize (json:inspect \"{}\") 0)","(tg:ok (JsonObjectV1 ()))");
    check("(telegram:normalize (json:inspect \"{\\\"ok\\\":true}\") 0)","(tg:ok (JsonObjectV1 ((JsonMemberV1 0 (JsonStringV1 ((cp 111) (cp 107))) (JsonBoolV1 True) JsonNoSourceSpanV1))))");
    response("{\"ok\":true,\"result\":[]}","(telegram:batch 1 ())");
    response("{\"ok\":true,\"ok\":true,\"result\":[]}","(telegram:hold 1 malformed-json)");
    response("{\"ok\":true,\"result\":{}}","(telegram:hold 1 malformed-response)");
    response("{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":3}}","(telegram:retry 1 3 rate-limit)");
    response("{\"ok\":false,\"error_code\":409}","(telegram:hold 1 conflicting-poller)");
    response("{\"ok\":false,\"error_code\":500}","(telegram:hold 1 api-error)");
    check("(telegram:route 4 \"message\" (json:inspect \"{\\\"chat\\\":{\\\"id\\\":42,\\\"type\\\":\\\"private\\\"},\\\"message_thread_id\\\":5}\") (tg:integer 7) dummy (telegram:policy 1 (42) False (7) (8)))",
        "(tg:item 4 \"42.5\" routed (telegram:input 1 \"message\" 42 5 7 operator dummy))");
    const char *group="{\"ok\":true,\"result\":[{\"update_id\":10,\"message\":{\"chat\":{\"id\":-100,\"type\":\"supergroup\"},\"message_thread_id\":12,\"from\":{\"id\":7},\"text\":\"a\\u0000b 😀\"}}]}";
    route(group,policy,"-100.12","routed","message",-100,12,7,"operator");
    route(group,"(telegram:policy 1 () True (7) ())","-100.12","unauthorized","message",-100,12,7,"operator");
    const char *priv="{\"ok\":true,\"result\":[{\"update_id\":20,\"edited_message\":{\"chat\":{\"id\":19,\"type\":\"private\"},\"from\":{\"id\":8}}}]}";
    route(priv,"(telegram:policy 1 () True (7) (8))","19.0","routed","edited_message",19,0,8,"wake-peer");
    route(priv,policy,"19.0","unauthorized","edited_message",19,0,8,"wake-peer");
    const char *callback="{\"ok\":true,\"result\":[{\"update_id\":30,\"callback_query\":{\"from\":{\"id\":8},\"message\":{\"chat\":{\"id\":42,\"type\":\"private\"},\"from\":{\"id\":7}}}}]}";
    route(callback,policy,"42.0","routed","callback_query",42,0,8,"wake-peer");
    route("{\"ok\":true,\"result\":[{\"update_id\":31,\"channel_post\":{\"chat\":{\"id\":-100,\"type\":\"channel\"}}}]}",
        policy,"-100.0","routed","channel_post",-100,0,0,"ordinary");
    const char *bad[]={
        "{\"ok\":true,\"result\":[{\"update_id\":1.0,\"message\":{}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1e0,\"message\":{}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":-1,\"message\":{}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":9223372036854775807,\"unknown\":{}}]}",
        "{\"ok\":true,\"result\":[{\"message\":{}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":{},\"edited_message\":{}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":null}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":{\"chat\":{\"id\":0,\"type\":\"private\"}}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":{\"chat\":{\"id\":42,\"type\":\"private\"},\"from\":{\"id\":-7}}}]}",
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":{\"chat\":{\"id\":42,\"type\":\"private\"},\"message_thread_id\":-1}}]}",
        /* A valid first occurrence must not mask a bad later one. */
        "{\"ok\":true,\"result\":[{\"update_id\":1,\"future\":{}},{\"update_id\":2,\"message\":{}}]}"
    };
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) response(bad[i],"(telegram:hold 1 malformed-update)");
    response("{\"ok\":true,\"result\":[{\"update_id\":1,\"message\":{\"chat\":{\"id\":42,\"id\":19}}}]}","(telegram:hold 1 malformed-json)");
    response("{\"ok\":true,\"result\":[{\"update_id\":01}]}","(telegram:hold 1 malformed-json)");
    response("{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":0}}","(telegram:hold 1 invalid-retry-delay)");
    response("{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":86401}}","(telegram:hold 1 invalid-retry-delay)");
    response("{\"ok\":false,\"error_code\":429}","(telegram:hold 1 invalid-retry-delay)");
    /* Unknown or absent optional fields and inline callbacks are retained. */
    EvalOutcome out;
    Atom *batch=items(decode("{\"ok\":true,\"result\":[{\"update_id\":7,\"new_type\":{\"data\":\"kept\"}},{\"update_id\":8},{\"update_id\":9,\"callback_query\":{\"inline_message_id\":\"abc\",\"from\":{\"id\":7}}}]}",policy,&out),3);
    for (size_t i=0;i<3;++i) {
        Atom *a=batch->expr.elems[i]; assert(form(a,"tg:item",5));
        assert(text(a->expr.elems[2],"unsupported") && atom_is_symbol(a->expr.elems[3],"unsupported"));
        assert(form(a->expr.elems[4],"JsonObjectV1",2)); ++checks;
    }
    eval_outcome_free(&out);
    /* Source spans, object order and array placement must not change dedupe.
     * NUL and non-BMP characters survive in canonical scalar strings. */
    Atom *original=items(decode(group,policy,&out),1)->expr.elems[0];
    unsigned char *encoded=NULL; size_t encoded_size=0;
    assert(cetta_durable_value_encode(original,&encoded,&encoded_size)==DURABLE_OK);
    eval_outcome_free(&out);
    const char *reordered=" {\"result\":[{\"future\":true,\"update_id\":9},{\"message\":{\"text\":\"a\\u0000b \\ud83d\\ude00\",\"from\":{\"id\":7},\"message_thread_id\":12,\"chat\":{\"type\":\"supergroup\",\"id\":-100}},\"update_id\":10}],\"ok\":true}";
    Atom *again=items(decode(reordered,policy,&out),2)->expr.elems[1];
    unsigned char *encoded2=NULL; size_t size2=0;
    assert(cetta_durable_value_encode(again,&encoded2,&size2)==DURABLE_OK);
    assert(size2==encoded_size && !memcmp(encoded,encoded2,size2)); ++checks;
    free(encoded); free(encoded2); eval_outcome_free(&out);
    /* External text/keys/numeric lexemes never become permanent symbols. */
    decode("{\"ok\":true,\"result\":[{\"update_id\":99,\"future\":{\"warm\":\"up\"}}]}",policy,&out); eval_outcome_free(&out);
    uint32_t before=atomic_load(&symbols.entry_len);
    for (unsigned i=0;i<30;++i) {
        char body[256]; snprintf(body,sizeof(body),"{\"ok\":true,\"result\":[{\"update_id\":%u,\"future-%u\":{\"key-%u\":\"text-%u\"}}]}",100+i,i,i,i);
        items(decode(body,policy,&out),1); eval_outcome_free(&out);
    }
    assert(atomic_load(&symbols.entry_len)==before); ++checks;
    /* Bound the provider document and policy walk. Oversized input is held,
     * never truncated into a smaller successful batch. */
    char *large=malloc(262146); assert(large); memset(large,' ',262145); large[262145]=0;
    response(large,"(telegram:hold 1 body-limit)"); free(large);
    char many[10000]; size_t used=(size_t)snprintf(many,sizeof(many),"{\"ok\":true,\"result\":[");
    for (unsigned i=0;i<101;++i) used+=(size_t)snprintf(many+used,sizeof(many)-used,"%s{\"update_id\":%u}",i?",":"",i);
    strcpy(many+used,"]}"); response(many,"(telegram:hold 1 batch-limit)");
    char deep[300]; memset(deep,'[',65); deep[65]='0'; memset(deep+66,']',65); deep[131]=0;
    response(deep,"(telegram:hold 1 malformed-json)");
    used=(size_t)snprintf(many,sizeof(many),"{");
    for (unsigned i=0;i<257;++i) used+=(size_t)snprintf(many+used,sizeof(many)-used,"%s\"k%u\":0",i?",":"",i);
    strcpy(many+used,"}"); response(many,"(telegram:hold 1 malformed-json)");
    many[0]='['; used=1;
    for (unsigned i=0;i<4097;++i) { if(i) many[used++]=','; many[used++]='0'; }
    strcpy(many+used,"]"); response(many,"(telegram:hold 1 malformed-json)");
    long_message();
    used=(size_t)snprintf(many,sizeof(many),"{\"ok\":true,\"result\":[");
    for (unsigned i=0;i<100;++i) used+=(size_t)snprintf(many+used,sizeof(many)-used,"%s{\"update_id\":%u}",i?",":"",i);
    strcpy(many+used,"]}");
    clock_t start=clock(); items(decode(many,policy,&out),100);
    printf("100-update batch: %.3f CPU s, %llu evaluator steps\n",
        (double)(clock()-start)/CLOCKS_PER_SEC,(unsigned long long)out.steps_spent);
    eval_outcome_free(&out); ++checks;
    /* A budget-exhausted classification cannot be mistaken for success. */
    Atom *args[]={atom_symbol(&persistent,"telegram:decode"),atom_string(&persistent,group),parse(policy)};
    eval_outcome_init(&out);
    eval_top_speculative(&context,&program,&scratch,&persistent,&registry,atom_expr(&persistent,args,3),30,&out);
    assert(out.completion==CETTA_EVAL_INCOMPLETE_FUEL && out.steps_spent==30 && !out.effect_denials);
    eval_outcome_free(&out); ++checks;
    intake_checks(group,policy);
    recovery_checks();
cleanup:
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    printf("Telegram pure policy: %u checks passed\n",checks); return 0;
}
