#define _GNU_SOURCE
#include "durable_worker_host.h"
#include "durable_host.h"
#include "prime_ordinary_owner.h"
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

static Atom *record_value(const char *space,const char *key) {
    CettaDurableScope scope={DURABLE_KEY,space,key};CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&scope,1,&o)==DURABLE_OK);
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);Atom *value=NULL;
    if(v->count)assert(v->count==1 && cetta_durable_value_decode(&persistent,
        v->records[0].data,v->records[0].size,&value)==DURABLE_OK);
    cetta_durable_observation_free(o);return value;
}
static Atom *field(Atom *value,size_t index) {
    assert(value && value->kind==ATOM_EXPR && index<value->expr.len);
    return value->expr.elems[index];
}
typedef struct {
    CettaHostProgram program;
    char actor[64],nonce[33],policy[80],result[96],task[80],outbox[64],evidence[96];
    unsigned sequence;
    bool retained;
} DutyClient;
static Atom *duty_state(const DutyClient *c) {
    Atom *actor=record_value("host.actors",c->actor);
    assert(actor && atom_is_symbol(field(actor,0),"host:actor") &&
        atom_eq(field(actor,2),parse("\"activity/1\"")));
    Atom *state=field(actor,3);
    assert(state->expr.len==12 && atom_is_symbol(field(state,0),"activity:duty") &&
        atom_eq(field(state,2),atom_string(&persistent,c->nonce)));
    return state;
}
static void duty_grant(const DutyClient *c,const char *principal,const char *right,bool live) {
    char key[128],value[256];
    snprintf(key,sizeof(key),"%s%s/%s",c->policy,principal,right);
    snprintf(value,sizeof(value),"(activity:grant 1 %s 7 %s)",principal,right);
    put("duty.policy",key,live?value:"revoked");
}
static CettaHostDecision *duty_begin(DutyClient *c,const char *input,const char *body) {
    char automatic[96];
    if(!input) {
        snprintf(automatic,sizeof(automatic),"%.63s/%u",c->actor,++c->sequence);input=automatic;
    }
    if(body)put("host.inbox",input,body);
    CettaHostSpaceGrant views[]={
        {{DURABLE_KEY,"duty.results",c->result},true},
        {{DURABLE_KEY,"host.worker-tasks",c->task},false},
        {{DURABLE_KEY,"host.worker-origins",c->task},false},
        {{DURABLE_KEY,"host.outbox",c->outbox},false},
        {{DURABLE_PREFIX,"duty.policy",c->policy},false},
        {{DURABLE_KEY,"escrow.evidence",c->evidence},false}};
    CettaHostChannelGrant channel={"worker.request","1",cetta_worker_validate,"brain"};
    CettaHostDecisionSpec spec={input,c->actor,c->program.version,views,6,&channel,1};
    CettaHostDecision *d=NULL;assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);return d;
}
static const EvalOutcome *duty_plan(DutyClient *c,CettaHostDecision *d,const char *expression);
static const EvalOutcome *duty_evaluate(DutyClient *c,CettaHostDecision *d,const char *expression) {
    if (c->retained) return duty_plan(c,d,expression);
    assert(cetta_host_evaluate(d,&c->program,parse(expression),100000)==DURABLE_OK);
    const EvalOutcome *o=cetta_host_outcome(d);
    if(o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials || o->results.len!=1) {
        fprintf(stderr,"duty %s: %s completion=%d denials=%zu results=%zu\n",c->actor,expression,
            o->completion,(size_t)o->effect_denials,(size_t)o->results.len);
        for(size_t i=0;i<o->results.len;i++){atom_print(o->results.items[i],stderr);fputc('\n',stderr);}
    }
    assert(o->completion==CETTA_EVAL_COMPLETE && !o->effect_denials && o->results.len==1);return o;
}
static const EvalOutcome *duty_plan(DutyClient *c,CettaHostDecision *d,const char *expression) {
    CettaHostPlanLimits limits={.quantum=7,.work_limit=1000000,
        .preparation_inspections=1000000,.preparation_bytes=32u*1024u*1024u,.results=2,
        .return_slice={.preparation=8,.transport=8,.limit_preparation=true,.limit_transport=true}};
    CettaDurableStatus status=cetta_host_plan_open(d,&c->program,parse(expression),&limits);
    if(status!=DURABLE_OK)fprintf(stderr,"imported duty planning: %s\n",cetta_durable_status_name(status));
    assert(status==DURABLE_OK);
    CettaOwnedTask *task=cetta_host_plan_task(d);assert(task);
    CettaReadyQueue queue={.capacity=1u};assert(cetta_ready_admit(&queue,&task->ready)==READY_ADMITTED);
    size_t commits=count("host.commits"),effects=count("host.outbox");
    for(unsigned i=0u;i<100000u && queue.count;i++) {
        CettaHostPlanPollAccount poll={0};
        assert(cetta_host_plan_poll_accounted(d,true,&poll)==DURABLE_OK &&
            poll.source_work<=limits.quantum &&
            poll.returns.preparation<=limits.return_slice.preparation &&
            poll.returns.transport<=limits.return_slice.transport);
    }
    const EvalOutcome *o=cetta_host_outcome(d);
    if(queue.count || !o || o->completion!=CETTA_EVAL_COMPLETE || o->results.len!=1) {
        fprintf(stderr,"imported duty plan %s: queued=%zu stop=%d completion=%d rows=%zu\n",
            expression,queue.count,(int)task->execution.stop,o?(int)o->completion:-1,o?(size_t)o->results.len:0u);
        if(o)for(size_t i=0u;i<o->results.len;i++){atom_print(o->results.items[i],stderr);fputc('\n',stderr);}
    }
    assert(!queue.count && o && o->completion==CETTA_EVAL_COMPLETE && !o->effect_denials &&
        o->results.len==1u && count("host.commits")==commits && count("host.outbox")==effects);
    return o;
}
typedef struct { bool accept; unsigned calls; Atom *value; } NativeDelivery;
static bool native_service_delivery(void *raw,const CettaOwnedReturn *returned) {
    NativeDelivery *delivery=raw;delivery->calls++;
    assert(returned->outcome.kind==CETTA_CALL_VALUE && returned->outcome.term &&
        returned->outcome.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE && returned->final);
    delivery->value=returned->outcome.term;return delivery->accept;
}
static void native_service_controls(CettaLibraryContext *ctx,Space *space,Registry *names) {
    const char *calls[]={
        "(__cetta_lib_rhometta_values (rho:par (rho:val 1) (rho:val (if True 11 12)) (rho:val 2) (rho:val 1)))",
        "(__cetta_lib_rhometta_run_canonical (rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) (rho:send (rho:quote rho:nil) (rho:val 9))))"};
    const char *expected[]={"(1 (if True 11 12) 2 1)","(rho:val 9)"};
    for (unsigned i=0u;i<2u;i++) {
        CettaOwnedExecution execution={0};NativeDelivery delivery={0};
        assert(cetta_library_native_execution_open(ctx,space,&persistent,parse(calls[i]),
            eval_prime_payload_provider(),&execution));
        void *owner=execution.computation;
        assert(cetta_owned_execution_sequence(&execution,0u,native_service_delivery,&delivery)==
            CETTA_OWNED_EXECUTION_PAUSED && !delivery.calls && execution.computation==owner);
        for(unsigned poll=0u;!execution.has_pending && poll<10000u;poll++)
            assert(cetta_owned_execution_sequence(&execution,1u,native_service_delivery,&delivery)==
                CETTA_OWNED_EXECUTION_PAUSED);
        assert(execution.has_pending && execution.accepted==0u && delivery.calls==1u &&
            atom_eq(delivery.value,parse(expected[i])));
        Atom *pending=execution.pending.outcome.term;
        assert(cetta_owned_execution_sequence(&execution,1u,native_service_delivery,&delivery)==
            CETTA_OWNED_EXECUTION_PAUSED && execution.has_pending && execution.pending.outcome.term==pending &&
            execution.computation==owner && delivery.calls==2u && !execution.accepted);
        delivery.accept=true;
        assert(cetta_owned_execution_sequence(&execution,1u,native_service_delivery,&delivery)==
            CETTA_OWNED_EXECUTION_COMPLETE && execution.accepted==1u && !execution.has_pending &&
            delivery.value==pending && delivery.calls==3u);
        assert(cetta_owned_execution_sequence(&execution,1u,native_service_delivery,&delivery)==
            CETTA_OWNED_EXECUTION_COMPLETE && delivery.calls==3u);
        cetta_owned_execution_cancel(&execution);
        assert(!execution.computation);

        CettaOwnedExecution cancelled={0};NativeDelivery abandoned={0};
        assert(cetta_library_native_execution_open(ctx,space,&persistent,parse(calls[i]),
            eval_prime_payload_provider(),&cancelled) && cancelled.release_poll);
        for(unsigned poll=0u;poll<2u;poll++)
            assert(cetta_owned_execution_sequence(&cancelled,1u,native_service_delivery,&abandoned)==
                CETTA_OWNED_EXECUTION_PAUSED);
        void *held=cancelled.computation;
        CettaOwnedExecution before;
        memcpy(&before,&cancelled,sizeof(before));
        uint64_t used=19u;
        assert(cetta_owned_execution_cancel_poll(&cancelled,0u,false,&used)==
            CETTA_OWNED_RELEASE_PENDING && !used && !memcmp(&before,&cancelled,sizeof(before)));
        assert(cetta_owned_execution_cancel_poll(&cancelled,1u,false,&used)==
            CETTA_OWNED_RELEASE_PENDING && used==1u && cancelled.retiring &&
            !cancelled.has_pending && cancelled.computation==held);
        CettaOwnedReleaseStep cleanup;
        do {
            cleanup=cetta_owned_execution_cancel_poll(&cancelled,7u,false,&used);
            assert(used<=7u && cancelled.computation==held &&
                cleanup!=CETTA_OWNED_RELEASE_COMPLETE);
        } while(cleanup==CETTA_OWNED_RELEASE_PENDING);
        assert(cleanup==CETTA_OWNED_RELEASE_ATOMIC && !cancelled.accepted);
        unsigned deliveries=abandoned.calls;
        assert(cetta_owned_execution_sequence(&cancelled,1u,native_service_delivery,&abandoned)==
            CETTA_OWNED_EXECUTION_INTERRUPTED && abandoned.calls==deliveries);
        assert(cetta_owned_execution_cancel_poll(&cancelled,64u,true,&used)==
            CETTA_OWNED_RELEASE_COMPLETE && !cancelled.computation && used==2u);
        cetta_owned_execution_cancel(&cancelled);
        assert(!cancelled.computation && !cancelled.accepted);
    }
    CettaOwnedExecution invalid={0};NativeDelivery refused={0};
    assert(cetta_library_native_execution_open(ctx,space,&persistent,
        parse("(__cetta_lib_rhometta_run_canonical (not-a-process))"),
        eval_prime_payload_provider(),&invalid));
    CettaOwnedExecutionStep verdict=CETTA_OWNED_EXECUTION_PAUSED;
    for(unsigned poll=0u;verdict==CETTA_OWNED_EXECUTION_PAUSED && poll<100u;poll++)
        verdict=cetta_owned_execution_sequence(&invalid,1u,native_service_delivery,&refused);
    assert(verdict==CETTA_OWNED_EXECUTION_INTERRUPTED &&
        invalid.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && !refused.calls && !invalid.accepted);
    cetta_owned_execution_cancel(&invalid);
    Arena destination;arena_init(&destination);ArenaMark before=arena_mark(&destination);
    assert(cetta_library_native_execution_open(ctx,space,&destination,
        parse("(__cetta_lib_rhometta_values (rho:val 1))"),eval_prime_payload_provider(),&invalid));
    assert(cetta_owned_execution_sequence(&invalid,1u,native_service_delivery,&refused)==
        CETTA_OWNED_EXECUTION_PAUSED && !refused.calls);
    arena_reset(&destination,before);
    assert(cetta_owned_execution_sequence(&invalid,1u,native_service_delivery,&refused)==
        CETTA_OWNED_EXECUTION_INTERRUPTED && invalid.completion==CETTA_EVAL_INCOMPLETE_INVALIDATED &&
        !refused.calls && !invalid.accepted);
    cetta_owned_execution_cancel(&invalid);arena_free(&destination);
    const char *data[]={"(Data Nat)","(:< Nat Number)","(lam x x)"};
    for(unsigned i=0u;i<sizeof(data)/sizeof(*data);i++) {
        CettaPrimeSourceOwner *source=NULL;
        assert(eval_prime_source_owner_capture_context(ctx,space,names,parse(data[i]),
            CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,1000000u,32u*1024u*1024u,&source)==
            CETTA_CONTINUATION_READY && source && eval_prime_source_owner_is_current(source));
        eval_prime_source_owner_release(source);
    }
    puts("PASS: owned Rho services retain one completed data occurrence across refused delivery and exact retry");
    puts("PASS: actual native Rho and value adapters retain cancelled owners until explicitly admitted cleanup");
}

static CettaHostCommit duty_accept(CettaHostDecision *d) {
    CettaHostCommit committed;CettaDurableStatus s=cetta_host_accept(d,0,&committed);
    if(s!=DURABLE_OK) {
        fprintf(stderr,"duty accept: %s\n",cetta_durable_status_name(s));
        const EvalOutcome *outcome=cetta_host_outcome(d);
        if(outcome) for(size_t i=0u;i<outcome->results.len;i++) {
            Atom *value=outcome->results.items[i];
            atom_print(value,stderr);fputc('\n',stderr);
        }
    }
    CettaHostCommit duplicate;
    assert(s==DURABLE_OK && cetta_host_accept(d,0,&duplicate)==DURABLE_CONFLICT);
    cetta_host_decision_free(d);return committed;
}
static void duty_refuse(DutyClient *c,const char *body,const char *expression,const char *reason) {
    Atom *before=duty_state(c);size_t outbox=count("host.outbox"),receipts=count("host.commits");
    CettaHostDecision *d=duty_begin(c,NULL,body);const EvalOutcome *o=duty_evaluate(c,d,expression);
    char expected[128];snprintf(expected,sizeof(expected),"(activity:refused %s)",reason);
    assert(atom_eq(o->results.items[0],parse(expected)));
    CettaHostCommit commit;assert(cetta_host_accept(d,0,&commit)==DURABLE_INVALID &&
        atom_eq(before,duty_state(c)) && count("host.outbox")==outbox && count("host.commits")==receipts);
    cetta_host_decision_free(d);
}

/* Independent receiver fixture. The wallet, stable request key, semantic
 * receipt and historical effect commit in one native-store transaction.
 * Only this trusted receiver writes escrow.*, never the MeTTa client. */
static bool escrow_apply(const DutyClient *c,const char *purpose,bool reversible,char answer[160]) {
    char key[96],wallet[80],primary_key[96];snprintf(key,sizeof(key),"%s/%s",c->nonce,purpose);
    snprintf(wallet,sizeof(wallet),"%s/wallet",c->actor);
    snprintf(primary_key,sizeof(primary_key),"%s/primary",c->nonce);
    snprintf(answer,160,"escrow/%s/%s/7",c->nonce,purpose);
    CettaDurableScope scopes[]={{DURABLE_KEY,"escrow.wallet",wallet},
        {DURABLE_KEY,"escrow.evidence",key},{DURABLE_KEY,"escrow.accepted",c->nonce},
        {DURABLE_KEY,"escrow.effects",key},{DURABLE_KEY,"escrow.evidence",primary_key}};
    CettaDurableObservation *o=NULL;assert(cetta_durable_observe(store,scopes,5,&o)==DURABLE_OK);
    const CettaDurableSnapshot *balance=cetta_durable_observation_view(o,0),
        *old=cetta_durable_observation_view(o,1),*accepted=cetta_durable_observation_view(o,2);
    assert(balance->count==1);
    if(old->count){cetta_durable_observation_free(o);return true;}
    bool primary=!strcmp(purpose,"primary");
    if(!primary) {
        if(!accepted->count){cetta_durable_observation_free(o);return false;}
        Atom *claim=NULL;assert(cetta_durable_value_decode(&persistent,
            accepted->records[0].data,accepted->records[0].size,&claim)==DURABLE_OK);
        if(!atom_eq(claim,parse("(escrow:accepted 7 True)"))) {
            cetta_durable_observation_free(o);return false;
        }
    }
    Atom *amount=NULL;assert(cetta_durable_value_decode(&persistent,balance->records[0].data,
        balance->records[0].size,&amount)==DURABLE_OK && amount->kind==ATOM_GROUNDED);
    int64_t before=amount->ground.ival,after=primary?before-7:before+7;
    assert(before>=0 && after>=0);
    char evidence[256],claim[96];
    snprintf(evidence,sizeof(evidence),"(escrow:evidence 1 \"%s\" 7 %s \"%s\" %lld %lld %s)",
        c->nonce,purpose,answer,(long long)before,(long long)after,primary?"debited":"refunded");
    snprintf(claim,sizeof(claim),"(escrow:accepted 7 %s)",reversible?"True":"False");
    Atom *values[]={atom_int(&persistent,after),parse(evidence),parse(claim),
        parse(primary?"(escrow:debit 7)":"(escrow:refund 7)"),parse("refunded")};
    unsigned char *bytes[5]={0};size_t sizes[5];
    for(size_t i=0;i<5;i++)assert(cetta_durable_value_encode(values[i],&bytes[i],&sizes[i])==DURABLE_OK);
    CettaDurableOp ops[]={{DURABLE_REPLACE,"escrow.wallet",wallet,bytes[0],sizes[0]},
        {DURABLE_INSERT,"escrow.evidence",key,bytes[1],sizes[1]},
        {DURABLE_INSERT,"escrow.accepted",c->nonce,bytes[2],sizes[2]},
        {DURABLE_INSERT,"escrow.effects",key,bytes[3],sizes[3]},
        {DURABLE_REPLACE,"escrow.evidence",primary_key,bytes[4],sizes[4]}};
    if(!primary){ops[2]=ops[4];}
    int64_t revision;assert(cetta_durable_commit_observed(store,o,ops,4,&revision)==DURABLE_OK);
    for(size_t i=0;i<5;i++)free(bytes[i]);
    cetta_durable_observation_free(o);return true;
}

static void duty_client_cases(const char *database,const char *directory) {
    const char *checker="(= (escrow:valid $duty $purpose $answer $evidence) "
        "(case $duty (((activity:duty 1 $id $obligor $obligee $scope $holder "
        "$primary $repair $cancelled $disposition $history) "
        "(case $evidence (((escrow:evidence 1 $id $scope $purpose $answer $before $after $status) "
        "(and (> $scope 0) (case $purpose "
        "((primary (and (== $status debited) (and (>= $before $scope) (== $after (- $before $scope))))) "
        "(repair (and (== $status refunded) (== $after (+ $before $scope)))) ($bad False))))) "
        "($bad False)))) ($bad False))))";
    for(unsigned trial=0;trial<6;trial++) {
        unsigned mode=trial>=3,scenario=trial%3;
        CettaLibraryContext ctx;Space source;space_init(&source);Registry names;registry_init(&names);
        registry_bind(&names,"&self",atom_space(&persistent,&source));
        if(mode)cetta_library_context_init_for_language_profile(&ctx,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
        else{cetta_library_context_init(&ctx);cetta_eval_session_init_he_extended(&ctx.session);}
        eval_set_library_context(&ctx);Atom *error=NULL;
        if(!mode) {
            assert(he_library_tables_init());stdlib_load(&source,&persistent);
            assert(telegram_he_prepare_library(&source,&persistent,true) && space_mark_imported_prefix(&source));
        }
        const char *module=getenv("CETTA_TEST_DUTY_SOURCE");
        assert(cetta_library_import_module(&ctx,module?module:"durable:activity",&source,false,&scratch,
            &persistent,&names,1000000,NULL,&error));
        space_add(&source,parse(checker));
        if(mode) {
            space_add(&source,parse("(= (min $x) (+ $x 1))"));
            space_add(&source,parse("(= (max $x) (+ $x 1))"));
            space_add(&source,parse("(= (member $x) (+ $x 1))"));
            space_add(&source,parse("(= (last $x) (+ $x 1))"));
            space_add(&source,parse("(= (the $x) (+ $x 1))"));
        }
        if(mode && !scenario) native_service_controls(&ctx,&source,&names);
        DutyClient client={.program={"activity/1",&source,&ctx},.retained=mode!=0u};
        DutyClient *c=&client;snprintf(c->actor,sizeof(c->actor),"duty/%s/%u",mode?"prime":"he",scenario);
        snprintf(c->policy,sizeof(c->policy),"%.63s/",c->actor);
        snprintf(c->result,sizeof(c->result),"%.63s/result",c->actor);
        strcpy(c->task,"absent");strcpy(c->outbox,"absent");strcpy(c->evidence,"absent");
        duty_grant(c,"alice","issue",true);duty_grant(c,"alice","hold",true);
        CettaHostDecision *d=duty_begin(c,NULL,"(claim-to-be mallory)");
        memcpy(c->nonce,cetta_host_decision_key(d),33);
        char expression[1024];snprintf(expression,sizeof(expression),
            "(activity:duty-issue \"%s\" alice beneficiary 7 6)",c->nonce);
        if(mode) {
            /* The imported client uses the actual retained source and fresh
             * host views. Synchronous evaluation remains the HE-only API. */
            assert(cetta_host_evaluate(d,&c->program,parse(expression),100000)==DURABLE_INVALID &&
                !cetta_host_outcome(d) && !record_value("host.actors",c->actor));
            const char *calls[]={"(min 41)","(max 41)","(member 41)","(last 41)","(the 41)"};
            for(unsigned i=0u;i<sizeof(calls)/sizeof(*calls);i++) {
                const EvalOutcome *custom=duty_plan(c,d,calls[i]);
                assert(atom_eq(custom->results.items[0],parse("42")));
            }
            const EvalOutcome *conditional=duty_plan(c,d,"(if True 11 12)");
            assert(atom_eq(conditional->results.items[0],parse("11")));
            duty_plan(c,d,expression);
            duty_grant(c,"alice","issue",false);duty_grant(c,"alice","issue",true);
            CettaHostCommit conflict;
            assert(cetta_host_accept(d,0,&conflict)==DURABLE_CONFLICT &&
                !record_value("host.actors",c->actor));
            cetta_host_decision_free(d);
            char pending[96];snprintf(pending,sizeof(pending),"%.63s/%u",c->actor,c->sequence);
            d=duty_begin(c,pending,NULL);memcpy(c->nonce,cetta_host_decision_key(d),33);
            snprintf(expression,sizeof(expression),"(activity:duty-issue \"%s\" alice beneficiary 7 6)",c->nonce);
            duty_plan(c,d,expression);duty_accept(d);
            Atom *state=duty_state(c);
            assert(atom_is_symbol(field(state,3),"alice") &&
                atom_is_symbol(field(state,4),"beneficiary") && atom_is_symbol(field(state,6),"alice"));
            puts("PASS: imported Prime duty issuance uses retained scoped code, rejects authority ABA and commits only fresh policy");
        } else {
            duty_evaluate(c,d,expression);duty_accept(d);
        }
        Atom *issued=duty_state(c);assert(atom_is_symbol(field(issued,3),"alice") &&
            atom_is_symbol(field(issued,4),"beneficiary") && atom_is_symbol(field(issued,6),"alice"));
        duty_grant(c,"alice","transfer",true);
        duty_refuse(c,"command","(activity:duty-transfer mallory bob 6)","holder-or-settled");
        duty_refuse(c,"command","(activity:duty-transfer alice bob 6)","authority");
        duty_grant(c,"bob","hold",true);d=duty_begin(c,NULL,"command");
        duty_evaluate(c,d,"(activity:duty-transfer alice bob 6)");duty_accept(d);
        Atom *transferred=duty_state(c);assert(atom_is_symbol(field(transferred,3),"alice") &&
            atom_is_symbol(field(transferred,6),"bob") && field(transferred,11)->expr.len==2);
        duty_grant(c,"bob","execute",true);duty_grant(c,"bob","compensate",true);
        duty_grant(c,"bob","adopt",true);
        duty_refuse(c,"(activity:input 1 request forged 1 work \"reserve\" 0 0)",
            "(activity:duty-receive bob primary \"brain\" 6)","request-owner");
        char request[256];snprintf(request,sizeof(request),
            "(activity:input 1 request (activity:duty-owner \"%.32s\" primary) 1 work \"reserve\" 0 0)",c->nonce);
        d=duty_begin(c,NULL,request);duty_evaluate(c,d,"(activity:duty-receive bob primary \"brain\" 6)");
        CettaHostCommit committed=duty_accept(d);snprintf(c->outbox,sizeof(c->outbox),"%s/0",committed.commit_key);
        char task[65];assert(cetta_worker_register(store,c->outbox,"brain",task)==DURABLE_OK);
        snprintf(c->task,sizeof(c->task),"brain/%s",task);
        char wallet[80];snprintf(wallet,sizeof(wallet),"%.63s/wallet",c->actor);put("escrow.wallet",wallet,"100");
        char answer[160];assert(escrow_apply(c,"primary",scenario!=2,answer));
        size_t effects=count("escrow.effects");assert(escrow_apply(c,"primary",scenario!=2,answer) && count("escrow.effects")==effects);
        assert(atom_eq(record_value("escrow.wallet",wallet),parse("93")));
        if(scenario!=1) {
            d=duty_begin(c,NULL,"cancel");duty_evaluate(c,d,"(activity:duty-cancel bob 6)");duty_accept(d);
        }
        duty_grant(c,"bob","execute",false);
        /* Crash window: the receiver has debited, but no worker completion is
         * stored. Recovery must retain the unsettled primary continuation. */
        assert(cetta_durable_checkpoint(store)==DURABLE_OK);cetta_durable_close(store);
        assert(cetta_durable_open(database,NULL,&store)==DURABLE_OK);
        assert(atom_is_symbol(field(duty_state(c),10),"activity:unsettled") &&
            atom_is_symbol(field(field(duty_state(c),7),0),"activity:waiting"));
        struct sockaddr_un address={.sun_family=AF_UNIX};
        snprintf(address.sun_path,sizeof(address.sun_path),"%s/duty-%u.sock",directory,trial);
        int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(listener>=0);
        assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)) && !listen(listener,4));
        CettaWorkerEndpoint *endpoint=NULL;assert(cetta_worker_endpoint_new(store,listener,getuid(),"brain",&endpoint)==DURABLE_OK);
        int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(fd>=0 && !connect(fd,(struct sockaddr *)&address,sizeof(address)));
        rpc(endpoint,fd,WORKER_RESULT,task,answer,WORKER_STORED,"");
        char input[96];snprintf(input,sizeof(input),"worker/%.79s",c->task);
        d=duty_begin(c,input,NULL);snprintf(expression,sizeof(expression),
            "(activity:duty-reply primary \"brain\" \"%s\" \"%s\" \"%s\")",c->result,c->task,c->outbox);
        duty_evaluate(c,d,expression);duty_accept(d);
        assert(atom_is_symbol(field(field(duty_state(c),7),0),"activity:done") &&
            atom_is_symbol(field(duty_state(c),9),scenario==1?"activity:live":"activity:cancelled") &&
            atom_is_symbol(field(duty_state(c),10),"activity:unsettled"));
        snprintf(c->evidence,sizeof(c->evidence),"%.32s/primary",c->nonce);
        snprintf(expression,sizeof(expression),
            "(activity:duty-settle bob primary 6 2 \"%s\" 7 \"%s\" escrow/v1 escrow:valid)",c->result,c->evidence);
        if(scenario==1) {
            Atom *genuine=record_value("escrow.evidence",c->evidence);
            Atom *savedResult=record_value("duty.results",c->result),*wrongParts[6];
            assert(savedResult->expr.len==6);memcpy(wrongParts,savedResult->expr.elems,sizeof(wrongParts));
            wrongParts[3]=atom_int(&persistent,2);
            put_value("duty.results",c->result,atom_expr(&persistent,wrongParts,6));
            duty_refuse(c,"settle",expression,"evidence");put_value("duty.results",c->result,savedResult);
            wrongParts[3]=field(savedResult,3);wrongParts[2]=parse("(activity:duty-owner forged primary)");
            put_value("duty.results",c->result,atom_expr(&persistent,wrongParts,6));
            duty_refuse(c,"settle",expression,"evidence");put_value("duty.results",c->result,savedResult);
            put("escrow.evidence",c->evidence,"(escrow:evidence 1 wrong-occurrence 7 primary forged 100 93 debited)");
            duty_refuse(c,"(claim-that-the-check-passed True)",expression,"meaning");
            put_value("escrow.evidence",c->evidence,genuine);
            char fake[1024];snprintf(fake,sizeof(fake),
                "(activity:duty-settle bob primary 6 2 \"%s\" 7 \"absent\" escrow/v1 escrow:valid)",c->result);
            duty_refuse(c,"(escrow:evidence 1 forged 7 primary success 100 93 debited)",fake,"evidence");
            duty_grant(c,"bob","adopt",false);duty_refuse(c,"settle",expression,"authority");
            duty_grant(c,"bob","adopt",true);d=duty_begin(c,NULL,"settle");duty_evaluate(c,d,expression);
            put_value("escrow.evidence",c->evidence,genuine);
            CettaHostCommit stale;assert(cetta_host_accept(d,0,&stale)==DURABLE_CONFLICT);cetta_host_decision_free(d);
            d=duty_begin(c,NULL,"settle");duty_evaluate(c,d,expression);
            put("duty.policy","unrelated","revoked");duty_accept(d);
            Atom *settled=duty_state(c);assert(atom_is_symbol(field(field(settled,10),0),"activity:settled") &&
                atom_is_symbol(field(field(settled,10),3),"primary") && field(settled,11)->expr.len==3 &&
                atom_eq(record_value("escrow.wallet",wallet),parse("93")) && count("escrow.effects")==effects);
            duty_refuse(c,"settle",expression,"holder-or-settled");
            goto duty_case_done;
        }
        duty_refuse(c,"settle",expression,"direction");
        snprintf(request,sizeof(request),
            "(activity:input 1 repair (activity:duty-owner \"%.32s\" repair) 1 work \"refund\" 0 0)",c->nonce);
        duty_refuse(c,request,"(activity:duty-receive bob repair \"brain\" 6)","authority");
        duty_grant(c,"bob","execute",true);d=duty_begin(c,NULL,request);
        duty_evaluate(c,d,"(activity:duty-receive bob repair \"brain\" 6)");committed=duty_accept(d);
        snprintf(c->outbox,sizeof(c->outbox),"%s/0",committed.commit_key);
        assert(cetta_worker_register(store,c->outbox,"brain",task)==DURABLE_OK);
        snprintf(c->task,sizeof(c->task),"brain/%s",task);
        bool refunded=escrow_apply(c,"repair",true,answer);
        if(scenario==2) {
            assert(!refunded && count("escrow.effects")==effects &&
                atom_eq(record_value("escrow.wallet",wallet),parse("93")));
            strcpy(answer,"irreversible-effect");
        } else {
            assert(refunded);effects=count("escrow.effects");
            assert(escrow_apply(c,"repair",true,answer) && count("escrow.effects")==effects &&
                atom_eq(record_value("escrow.wallet",wallet),parse("100")));
        }
        rpc(endpoint,fd,WORKER_RESULT,task,answer,WORKER_STORED,"");
        snprintf(input,sizeof(input),"worker/%.79s",c->task);d=duty_begin(c,input,NULL);
        snprintf(expression,sizeof(expression),"(activity:duty-reply repair \"brain\" \"%s\" \"%s\" \"%s\")",
            c->result,c->task,c->outbox);duty_evaluate(c,d,expression);duty_accept(d);
        snprintf(c->evidence,sizeof(c->evidence),"%.32s/repair",c->nonce);
        snprintf(expression,sizeof(expression),
            "(activity:duty-settle bob repair 6 2 \"%s\" 7 \"%s\" escrow/v1 escrow:valid)",c->result,c->evidence);
        if(scenario==2) {
            duty_refuse(c,"(claim-refund True)",expression,"evidence");
            assert(atom_is_symbol(field(duty_state(c),10),"activity:unsettled") &&
                count("escrow.effects")==effects && atom_eq(record_value("escrow.wallet",wallet),parse("93")));
            goto duty_case_done;
        }
        Atom *evidence=record_value("escrow.evidence",c->evidence);
        put("escrow.evidence",c->evidence,"(escrow:evidence 1 wrong-occurrence 7 repair forged 93 100 refunded)");
        duty_refuse(c,"(claim-refund True)",expression,"meaning");
        put_value("escrow.evidence",c->evidence,evidence);
        d=duty_begin(c,NULL,"settle");duty_evaluate(c,d,expression);
        duty_grant(c,"bob","compensate",false);duty_grant(c,"bob","compensate",true);
        CettaHostCommit conflict;assert(cetta_host_accept(d,0,&conflict)==DURABLE_CONFLICT);cetta_host_decision_free(d);
        d=duty_begin(c,NULL,"settle");duty_evaluate(c,d,expression);duty_accept(d);
        Atom *settled=duty_state(c);assert(atom_is_symbol(field(field(settled,10),0),"activity:settled") &&
            atom_is_symbol(field(settled,3),"alice") && atom_is_symbol(field(settled,6),"bob") &&
            field(settled,11)->expr.len==4 && count("escrow.effects")==effects);
        duty_refuse(c,"transfer","(activity:duty-transfer bob alice 6)","holder-or-settled");
        duty_refuse(c,"settle",expression,"holder-or-settled");
        rpc(endpoint,fd,WORKER_RESULT,task,answer,WORKER_STORED,"");
        assert(count("escrow.effects")==effects);
        duty_case_done:
        close(fd);cetta_worker_endpoint_free(endpoint);assert(!unlink(address.sun_path));
        registry_free(&names);space_free(&source);eval_set_library_context(&context);cetta_library_context_free(&ctx);
        printf("PASS: native %s duty %s, custody, revoked completion, receiver recovery and semantic settlement\n",
            mode?"Prime":"HE",scenario==0?"real refund":scenario==1?"primary adoption":"irreversible uncertainty");
    }
    eval_set_library_context(&context);
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
    close(fd); cetta_worker_endpoint_free(endpoint); unlink(address.sun_path);
    duty_client_cases(db,directory);
    cetta_durable_close(store);
    unlink(db); char side[300]; snprintf(side,sizeof(side),"%s.lock",db); unlink(side);
    snprintf(side,sizeof(side),"%s.checkpoint",db); unlink(side); assert(!rmdir(directory));
    cetta_library_context_free(&context); eval_set_library_context(NULL);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
    puts("durable worker host: accepted request recovery, immutable origin, authority, recorded response selection and stale-draft retirement passed");
}
