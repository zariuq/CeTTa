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
static bool send_allowed(void *context, const Atom *payload, const Atom *reply) {
    (void)reply;
    return payload->kind==ATOM_EXPR && payload->expr.len==3 &&
        atom_is_symbol(payload->expr.elems[0],"sendMessage") &&
        payload->expr.elems[1]->kind==ATOM_GROUNDED &&
        payload->expr.elems[1]->ground.gkind==GV_STRING &&
        !strcmp(payload->expr.elems[1]->ground.sval,context?context:"approved-chat");
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

static const CettaHostPlanLimits plan_limits={.quantum=1,.work_limit=10000,
    .preparation_inspections=100000,.preparation_bytes=8u*1024u*1024u,.results=4,
    .return_slice={.preparation=1u,.transport=1u,
        .limit_preparation=true,.limit_transport=true}};
static void retained_program(Space *source,CettaLibraryContext *ctx,CettaHostProgram *out) {
    space_init(source);
    cetta_library_context_init_for_language_profile(ctx,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(ctx);
    space_add(source,parse("(= (plan $n) (if (== $n 0) "
        "(match &self (host:record 0 $key (received $body)) "
        "(host:transition (done $body) ((host:put 0 \"actor/native\" $body)) "
        "((host:send 0 (sendMessage \"approved-chat\" $body) reply)))) "
        "(plan (- $n 1))))"));
    space_add(source,parse("(= (spin $n) (spin (+ $n 1)))"));
    *out=(CettaHostProgram){"program/prime/1",source,ctx};
}
static CettaHostDecision *retained_begin(const char *input,const char *actor,bool writes) {
    const CettaHostSpaceGrant grant={{DURABLE_PREFIX,"native.state","actor/"},true};
    const CettaHostDecisionSpec request={input,actor,"program/prime/1",writes?&grant:NULL,
        writes?1u:0u,writes?channels:NULL,writes?1u:0u};
    CettaHostDecision *d=NULL;assert(cetta_host_begin(store,&request,&d)==DURABLE_OK);return d;
}
static void retained_open(CettaHostDecision *d,const CettaHostProgram *p,const char *expression,
        CettaHostPlanLimits limits) {
    int fuel=p->context->session.options.fuel_limit;
    CettaDurableStatus s=cetta_host_plan_open(d,p,parse(expression),&limits);
    if(s!=DURABLE_OK)fprintf(stderr,"retained admission: %s\n",cetta_durable_status_name(s));
    assert(s==DURABLE_OK && p->context->session.options.fuel_limit==fuel &&
        eval_current_library_context()==p->context);
    const EvalOutcome *out=cetta_host_outcome(d);
    assert(out && !out->steps_spent && !out->results.len && out->budget_remaining==limits.work_limit);
}
static void retained_run(CettaHostDecision *d) {
    CettaOwnedTask *task=cetta_host_plan_task(d);assert(task);
    CettaReadyQueue queue={.capacity=1};assert(cetta_ready_admit(&queue,&task->ready)==READY_ADMITTED);
    for(unsigned i=0;i<10000 && queue.count;i++)cetta_owned_task_visit(&queue,true);
    const EvalOutcome *out=cetta_host_outcome(d);
    if(queue.count || out->completion!=CETTA_EVAL_COMPLETE)
        fprintf(stderr,"retained completion: queued=%zu stop=%d completion=%d rows=%llu work=%llu\n",
            queue.count,(int)task->execution.stop,(int)out->completion,
            (unsigned long long)out->results.len,(unsigned long long)out->steps_spent);
    assert(!queue.count && out->completion==CETTA_EVAL_COMPLETE);
}
static void retained_slot_account(const CettaHostProgram *p) {
    write_record("host.inbox","native/slot-account","(received \"accounted\")");
    CettaHostDecision *d=retained_begin(
        "native/slot-account","native/slot-account",false);
    CettaHostPlanLimits limits=plan_limits;
    limits.quantum=7u;
    CettaHostPlanLimits invalid=limits;
    invalid.return_slice.limit_transport=false;
    assert(cetta_host_plan_open(d,p,parse("(plan 8)"),&invalid)==DURABLE_INVALID &&
        !cetta_host_plan_task(d));
    invalid=limits;invalid.return_slice.transport=UINT64_MAX;
    assert(cetta_host_plan_open(d,p,parse("(plan 8)"),&invalid)==DURABLE_INVALID &&
        !cetta_host_plan_task(d));
    invalid=limits;invalid.preparation_inspections=2u;
    assert(cetta_host_plan_open(d,p,parse("(plan 8)"),&invalid)==DURABLE_INVALID &&
        !cetta_host_plan_task(d));
    invalid=limits;invalid.preparation_inspections=3u;
    assert(cetta_host_plan_open(d,p,parse("(plan 8)"),&invalid)==DURABLE_LIMIT &&
        !cetta_host_plan_task(d));
    invalid=limits;invalid.preparation_bytes=64u;
    assert(cetta_host_plan_open(d,p,parse("(plan 8)"),&invalid)==DURABLE_LIMIT &&
        !cetta_host_plan_task(d));
    retained_open(d,p,"(plan 8)",limits);
    CettaOwnedTask *task=cetta_host_plan_task(d);assert(task);
    CettaReadyQueue queue={.capacity=1u};
    assert(cetta_ready_admit(&queue,&task->ready)==READY_ADMITTED);
    CettaOwnedTaskAccount total={0};
    bool dispatch_cost_separated=false,receipt_refusals_checked=false;
    uint64_t receipt_events=0u,receipt_units=0u,last_identity=0u;
    uint64_t setup_work=0u,setup_bytes=0u;
    for(unsigned slot=0u;slot<10000u && !task->execution.terminal;slot++) {
        CettaHostPlanPollAccount poll={0};
        assert(cetta_host_plan_poll_accounted(d,true,&poll)==DURABLE_OK &&
            poll.visit.task==task && poll.source_work<=limits.quantum &&
            poll.returns.preparation<=limits.return_slice.preparation &&
            poll.returns.transport<=limits.return_slice.transport);
        CettaOwnedTaskAccount account=poll.protocol;
        uint64_t slot_units=0u;
        CettaHostPlanReceiptSummary summary={0};
        assert(!account.phase_overflow && account.phase_count==
            account.admission+account.producer+account.consumer+
                account.final_is_current+account.rotations+account.retirements &&
            cetta_host_plan_receipt_summarize(&poll.receipt,&summary) &&
            cetta_host_plan_receipt_total(&poll.receipt,&slot_units) &&
            summary.initial_pending==poll.receipt.initial_pending &&
            summary.step==poll.visit.step &&
            summary.setup_work==poll.setup.inspections &&
            summary.last_admission==poll.visit.admission &&
            summary.admission==account.admission &&
            summary.producer==account.producer &&
            summary.consumer==account.consumer &&
            summary.final_is_current==account.final_is_current &&
            summary.retirements==account.retirements &&
            summary.source_work==poll.source_work &&
            summary.returns.preparation==poll.returns.preparation &&
            summary.returns.transport==poll.returns.transport &&
            slot_units==poll.setup.inspections+account.phase_count+poll.source_work+
                poll.returns.preparation+poll.returns.transport);
        assert(poll.setup.inspections<=limits.preparation_inspections &&
            poll.setup.arena_bytes<=limits.preparation_bytes);
        setup_work+=poll.setup.inspections;setup_bytes+=poll.setup.arena_bytes;
        uint64_t origin=0u,occurrence=0u;
        for(uint8_t event=0u;event<poll.receipt.count;event++) {
            const CettaNativeCostEvent *native=
                &poll.receipt.occurrences[event].event;
            assert(native->identity>last_identity && native->origin &&
                native->occurrence>occurrence &&
                (!origin || native->origin==origin));
            last_identity=native->identity;origin=native->origin;
            occurrence=native->occurrence;
        }
        if(!receipt_refusals_checked && poll.receipt.count) {
            CettaHostPlanReceipt forged=poll.receipt;
            forged.complete=false;
            assert(!cetta_host_plan_receipt_summarize(&forged,&summary));
            forged=poll.receipt;
            forged.occurrences[0].event.identity=0u;
            assert(!cetta_host_plan_receipt_summarize(&forged,&summary));
            forged=poll.receipt;
            forged.occurrences[0].tag=CETTA_HOST_PLAN_SOURCE_WORK;
            assert(!cetta_host_plan_receipt_summarize(&forged,&summary));
            receipt_refusals_checked=true;
        }
        receipt_events+=poll.receipt.count;receipt_units+=slot_units;
        assert(account.admission<=2u && account.producer<=1u &&
            account.consumer<=1u && account.final_is_current<=1u &&
            !account.rotations && account.retirements<=1u);
        if(poll.source_work>account.producer+account.consumer)
            dispatch_cost_separated=true;
        total.admission+=account.admission;total.producer+=account.producer;
        total.consumer+=account.consumer;
        total.final_is_current+=account.final_is_current;
        total.rotations+=account.rotations;total.retirements+=account.retirements;
    }
    const EvalOutcome *out=cetta_host_outcome(d);
    CettaPrimeReturnWork work={0};
    bool qualified=task->execution.terminal && !queue.count &&
        out->completion==CETTA_EVAL_COMPLETE && out->results.len==1u &&
        cetta_host_plan_return_work(d,&work) && work.preparation>0u &&
        work.transport>0u && total.admission>0u && total.producer>0u &&
        setup_work>0u && setup_work<=limits.preparation_inspections &&
        setup_bytes>0u && setup_bytes<=limits.preparation_bytes &&
        total.consumer>0u && total.final_is_current<=1u &&
        total.retirements==1u && dispatch_cost_separated &&
        receipt_refusals_checked;
    if(!qualified)fprintf(stderr,"slot account terminal=%u queue=%zu completion=%u rows=%llu "
        "setup=%llu setup_bytes=%llu preparation=%llu transport=%llu admission=%llu producer=%llu consumer=%llu "
        "final=%llu retired=%llu separated=%u\n",task->execution.terminal?1u:0u,queue.count,
        (unsigned)out->completion,(unsigned long long)out->results.len,
        (unsigned long long)setup_work,(unsigned long long)setup_bytes,
        (unsigned long long)work.preparation,(unsigned long long)work.transport,
        (unsigned long long)total.admission,(unsigned long long)total.producer,
        (unsigned long long)total.consumer,(unsigned long long)total.final_is_current,
        (unsigned long long)total.retirements,dispatch_cost_separated?1u:0u);
    assert(qualified);
    uint64_t bounded_source=out->steps_spent;
    uint64_t bounded_preparation=work.preparation,bounded_transport=work.transport;
    cetta_host_plan_cancel(d);
    assert(!cetta_host_plan_return_work(d,&work));
    cetta_host_decision_free(d);
    write_record("host.inbox","native/slot-unlimited","(received \"unlimited\")");
    d=retained_begin("native/slot-unlimited","native/slot-unlimited",false);
    limits=plan_limits;limits.return_slice=(CettaPrimeReturnSlice){0};
    retained_open(d,p,"(host:transition unlimited () ())",limits);
    retained_run(d);
    assert(cetta_host_plan_return_work(d,&work) && !work.preparation &&
        work.transport>0u);
    uint64_t unlimited_transport=work.transport;
    cetta_host_plan_cancel(d);cetta_host_decision_free(d);
    printf("PASS: retained host slot setup=%llu setup_bytes=%llu source=%llu preparation=%llu transport=%llu "
        "admission=%llu producer=%llu consumer=%llu final=%llu retired=%llu "
        "receipt_events=%llu receipt_units=%llu unlimited_transport=%llu\n",
        (unsigned long long)setup_work,(unsigned long long)setup_bytes,
        (unsigned long long)bounded_source,
        (unsigned long long)bounded_preparation,(unsigned long long)bounded_transport,
        (unsigned long long)total.admission,(unsigned long long)total.producer,
        (unsigned long long)total.consumer,(unsigned long long)total.final_is_current,
        (unsigned long long)total.retirements,(unsigned long long)receipt_events,
        (unsigned long long)receipt_units,(unsigned long long)unlimited_transport);
}
static void cleanup_receipt(const CettaHostPlanPollAccount *poll,uint64_t used) {
    CettaHostPlanReceiptSummary summary={0};
    assert(cetta_host_plan_receipt_summarize(&poll->receipt,&summary) && summary.cleanup &&
        summary.cleanup_status==poll->cleanup_status &&
        summary.cleanup_work+summary.return_cleanup+summary.decision_release==used &&
        summary.total==used+summary.cleanup_polls+summary.retirements+summary.setup_work &&
        !summary.admission && !summary.producer && !summary.consumer && !summary.final_is_current &&
        !summary.source_work && !summary.returns.preparation && !summary.returns.transport);
    CettaHostPlanReceipt forged=poll->receipt;forged.cleanup_status=(CettaOwnedReleaseStep)-1;
    assert(!cetta_host_plan_receipt_summarize(&forged,&summary));
    if (forged.count) {
        forged=poll->receipt;forged.occurrences[0].tag=(CettaHostPlanTag)-1;
        assert(!cetta_host_plan_receipt_summarize(&forged,&summary));
    }
}
static void retained_cleanup_cases(const CettaHostProgram *p) {
    for (unsigned queued=0u;queued<2u;queued++) for (unsigned copy=0u;copy<2u;copy++) {
        char input[80];snprintf(input,sizeof(input),"native/cleanup/%u/%u",queued,copy);
        write_record("host.inbox",input,"(received \"cleanup\")");
        CettaHostDecision *d=retained_begin(input,input,false);
        CettaHostPlanLimits limits=plan_limits;limits.quantum=7u;
        CettaWorkCredit *payer=cetta_work_credit_new(limits.work_limit+64u);assert(payer);
        limits.credit=payer;
        retained_open(d,p,copy?"(host:transition (Pair (a b c d e f) (g h i j k l)) () ())":"(spin 0)",limits);
        CettaOwnedTask *task=cetta_host_plan_task(d);assert(task);
        CettaReadyQueue queue={.capacity=1u};
        if (queued) assert(cetta_ready_admit(&queue,&task->ready)==READY_ADMITTED);
        for (unsigned n=0u;n<10000u;n++) {
            CettaHostPlanPollAccount poll={0};CettaPrimeReturnWork work={0};
            assert(cetta_host_plan_poll_accounted(d,true,&poll)==DURABLE_OK);
            assert(cetta_host_plan_return_work(d,&work));
            if (!copy || (task->execution.has_pending && work.transport)) break;
        }
        if (copy) assert(task->execution.has_pending && !cetta_host_outcome(d)->results.len);
        uint64_t paid=cetta_host_outcome(d)->steps_spent,used=88u;
        CettaWorkCreditView credit={0};
        assert(cetta_work_credit_inspect(payer,&credit) && credit.children==1u &&
            credit.balance==64u && credit.root_spent==paid);
        size_t inbox=count("host.inbox"),commits=count("host.commits");
        CettaOwnedExecution before;memcpy(&before,&task->execution,sizeof(before));
        CettaHostDecision *same=d;CettaHostPlanPollAccount poll={0};
        assert(cetta_host_decision_release_poll(&d,0u,true,&used,&poll)==CETTA_OWNED_RELEASE_PENDING &&
            !used && d==same && !memcmp(&before,&task->execution,sizeof(before)) && queue.count==queued);
        cleanup_receipt(&poll,used);
        assert(cetta_host_plan_cancel_poll(d,1u,false,&used,&poll)==CETTA_OWNED_RELEASE_PENDING &&
            used==1u && !queue.count && task->execution.retiring && task->execution.computation &&
            !task->execution.has_pending && poll.protocol.retirements==queued &&
            cetta_host_outcome(d)->completion==CETTA_EVAL_INCOMPLETE_CANCELLED &&
            cetta_host_outcome(d)->steps_spent==paid && !cetta_host_plan_task(d));
        cleanup_receipt(&poll,used);
        CettaHostCommit commit;
        assert(cetta_host_accept(d,0u,&commit)==DURABLE_PRECONDITION &&
            cetta_host_plan_poll_accounted(d,true,&poll)==DURABLE_PRECONDITION &&
            cetta_owned_task_poll(task,true).step==CETTA_OWNED_EXECUTION_INTERRUPTED);
        assert(cetta_host_plan_cancel_poll(d,99u,false,&used,&poll)==CETTA_OWNED_RELEASE_ATOMIC &&
            !used && task->execution.computation);
        cleanup_receipt(&poll,used);
        assert(cetta_work_credit_inspect(payer,&credit) && credit.children==1u &&
            credit.balance==64u && credit.root_spent==paid);
        if (copy) {
            assert(cetta_host_plan_cancel_poll(d,1u,true,&used,&poll)==CETTA_OWNED_RELEASE_PENDING &&
                used==1u && poll.return_cleanup==1u && !poll.protocol.cleanup_polls &&
                task->execution.computation && !task->execution.release_ready);
            cleanup_receipt(&poll,used);
            assert(cetta_work_credit_inspect(payer,&credit) && credit.children==1u &&
                credit.balance==64u && credit.root_spent==paid);
        }
        assert(cetta_host_plan_cancel_poll(d,1u,true,&used,&poll)==CETTA_OWNED_RELEASE_COMPLETE &&
            used==1u && !task->execution.computation && !poll.return_cleanup);
        cleanup_receipt(&poll,used);
        assert(cetta_work_credit_inspect(payer,&credit) && !credit.children &&
            credit.balance==limits.work_limit+64u-paid && credit.root_spent==paid);
        assert(cetta_host_decision_release_poll(&d,1u,false,&used,&poll)==CETTA_OWNED_RELEASE_ATOMIC &&
            !used && d==same && cetta_host_outcome(d)->steps_spent==paid);
        cleanup_receipt(&poll,used);
        assert(cetta_host_decision_release_poll(&d,1u,true,&used,&poll)==CETTA_OWNED_RELEASE_COMPLETE &&
            used==1u && !d && poll.decision_release==1u && !poll.visit.task);
        cleanup_receipt(&poll,used);
        assert(cetta_host_decision_release_poll(&d,1u,true,&used,&poll)==CETTA_OWNED_RELEASE_COMPLETE &&
            !used && !poll.receipt.count && !poll.protocol.cleanup_polls);
        cleanup_receipt(&poll,used);
        assert(count("host.inbox")==inbox && count("host.commits")==commits);
        assert(cetta_work_credit_inspect(payer,&credit) && !credit.children &&
            credit.balance==limits.work_limit+64u-paid && credit.root_spent==paid);
        cetta_work_credit_release(payer);
    }
    puts("PASS: actual retained host cleanup stops publication, disposes dependent copies before sources and preserves exact phase receipts");
}

static void retained_credit_cases(const CettaHostProgram *p) {
    write_record("host.inbox","native/credit","(received \"funded\")");
    CettaHostDecision *first=retained_begin("native/credit","native/credit-first",false);
    CettaHostDecision *loop=retained_begin("native/credit","native/credit-loop",false);
    CettaHostDecision *refused=retained_begin("native/credit","native/credit-refused",false);
    CettaWorkCredit *root=cetta_work_credit_new(2048u);assert(root);
    CettaWorkCredit *caller=cetta_work_credit_reserve(root,2048u);assert(caller);
    CettaHostPlanLimits limits=plan_limits;limits.credit=caller;limits.work_limit=1024u;
    retained_open(first,p,"(host:transition funded () ())",limits);
    limits.work_limit=23u;limits.quantum=7u;
    retained_open(loop,p,"(spin 0)",limits);
    CettaWorkCreditView initial;
    assert(cetta_work_credit_inspect(caller,&initial) && initial.balance==1001u &&
        initial.children==2u && !initial.root_spent);
    limits.work_limit=1024u;
    assert(cetta_host_plan_open(refused,p,parse("(spin 0)"),&limits)==DURABLE_LIMIT);
    CettaWorkCreditView view;
    assert(cetta_work_credit_inspect(caller,&view) && view.balance==initial.balance &&
        view.children==initial.children && !view.root_spent && !cetta_host_plan_task(refused));
    assert(!cetta_work_credit_close(caller) && !cetta_work_credit_close(root));
    CettaOwnedTask *tasks[]={cetta_host_plan_task(first),cetta_host_plan_task(loop)};
    for(unsigned i=0u;i<10000u;i++) {
        for(unsigned task=0u;task<2u;task++)cetta_owned_task_poll(tasks[task],true);
        const EvalOutcome *a=cetta_host_outcome(first),*b=cetta_host_outcome(loop);
        assert(cetta_work_credit_inspect(root,&view));
        assert(view.root_spent==a->steps_spent+b->steps_spent &&
            view.root_remaining==2048u-view.root_spent && b->steps_spent<=23u);
        if(a->completion==CETTA_EVAL_COMPLETE && b->steps_spent==23u)break;
    }
    const EvalOutcome *a=cetta_host_outcome(first),*b=cetta_host_outcome(loop);
    assert(a->completion==CETTA_EVAL_COMPLETE && a->steps_spent>0u && a->results.len==1u);
    assert(b->steps_spent==23u && !b->results.len && !tasks[1]->execution.terminal);
    uint64_t spent=a->steps_spent+b->steps_spent;
    for(unsigned i=0u;i<16u;i++)cetta_owned_task_poll(tasks[1],true);
    assert(cetta_work_credit_inspect(root,&view) && view.root_spent==spent);
    CettaHostCommit commit;assert(cetta_host_accept(first,0,&commit)==DURABLE_OK);
    cetta_host_plan_cancel(first);cetta_host_plan_cancel(loop);
    assert(cetta_work_credit_inspect(caller,&view) && !view.children &&
        view.balance==2048u-spent && view.root_spent==spent);
    cetta_host_plan_cancel(first);cetta_host_plan_cancel(loop);
    assert(cetta_work_credit_close(caller) && !cetta_work_credit_close(caller));
    assert(cetta_work_credit_inspect(root,&view) && !view.children &&
        view.balance==2048u-spent && view.root_spent==spent);
    cetta_host_decision_free(first);cetta_host_decision_free(loop);cetta_host_decision_free(refused);
    cetta_work_credit_release(caller);cetta_work_credit_release(root);
    puts("PASS: sibling retained plans conserve actual caller credit, retain failed work and refund only unused leases");
}
static void retained_host_cases(void) {
    CettaLibraryContext ctx;Space source;CettaHostProgram p;retained_program(&source,&ctx,&p);
    write_record("host.inbox","native/shared","(received \"first\")");
    write_record("host.inbox","native/spin","(received \"loop\")");
    CettaHostDecision *first=retained_begin("native/shared","native/actor",true);
    CettaHostDecision *second=retained_begin("native/shared","native/actor",true);
    CettaHostDecision *loop=retained_begin("native/spin","native/loop",false);
    retained_open(first,&p,"(plan 8)",plan_limits);
    CettaHostPlanLimits seven=plan_limits;seven.quantum=7;
    retained_open(second,&p,"(plan 8)",seven);
    retained_open(loop,&p,"(spin 0)",plan_limits);
    CettaHostCommit commit;assert(cetta_host_accept(first,0,&commit)==DURABLE_PRECONDITION);
    CettaReadyQueue queue={.capacity=4};
    CettaHostDecision *tasks[]={loop,first,second};
    for(unsigned i=0;i<3;i++)assert(cetta_ready_admit(&queue,&cetta_host_plan_task(tasks[i])->ready)==READY_ADMITTED);
    size_t receipts=count("host.commits"),effects=count("host.outbox");
    CettaHostDecision *winner=NULL,*late=NULL;bool late_committed=false;
    unsigned slots=0;
    for(;slots<10000 && (!cetta_host_plan_task(first)->execution.terminal ||
            !cetta_host_plan_task(second)->execution.terminal || !late_committed);slots++) {
        if(slots==3) {
            write_record("host.inbox","native/unrelated","(received \"late\")");
            late=retained_begin("native/unrelated","native/late",false);
            retained_open(late,&p,"(host:transition late () ())",plan_limits);
            assert(cetta_ready_admit(&queue,&cetta_host_plan_task(late)->ready)==READY_ADMITTED);
        }
        uint64_t before[3];
        for(unsigned i=0;i<3;i++)before[i]=cetta_host_outcome(tasks[i])->steps_spent;
        CettaOwnedVisit visit=cetta_owned_task_visit(&queue,true);
        assert(visit.task);
        for(unsigned i=0;i<3;i++) {
            const EvalOutcome *out=cetta_host_outcome(tasks[i]);
            assert(out->steps_spent<=plan_limits.work_limit);
            assert(out->steps_spent-before[i]<=(i==2?seven.quantum:plan_limits.quantum));
            if(i && out->completion==CETTA_EVAL_COMPLETE && !winner) {
                assert(out->results.len==1);
                assert(cetta_host_accept(tasks[i],0,&commit)==DURABLE_OK && commit.effects==1);
                winner=tasks[i];
            }
        }
        if(late && !late_committed && cetta_host_outcome(late)->completion==CETTA_EVAL_COMPLETE) {
            assert(cetta_host_accept(late,0,&commit)==DURABLE_OK && !commit.effects);
            late_committed=true;
        }
    }
    assert(winner && late_committed && queue.count==1 && !cetta_host_plan_task(loop)->execution.terminal);
    CettaHostDecision *loser=winner==first?second:first;
    const EvalOutcome *out=cetta_host_outcome(loser);
    assert(out->completion==CETTA_EVAL_COMPLETE && out->results.len==1);
    /* The old immutable planning snapshot is still a valid computation. Its
     * live commit loses, and that attempted decision is permanently spent. */
    assert(cetta_host_accept(loser,0,&commit)==DURABLE_CONFLICT);
    assert(cetta_host_accept(loser,0,&commit)==DURABLE_CONFLICT);
    assert(cetta_host_plan_open(loser,&p,parse("(plan 0)"),&plan_limits)==DURABLE_CONFLICT);
    assert(count("host.commits")==receipts+2 && count("host.outbox")==effects+1);
    assert(atom_eq(cetta_host_outcome(first)->results.items[0],cetta_host_outcome(second)->results.items[0]) &&
        cetta_host_outcome(first)->steps_spent==cetta_host_outcome(second)->steps_spent);
    printf("retained host service: %u visits, finite work=%llu at quanta 1 and 7, late input committed, continuing sibling retained\n",
        slots,(unsigned long long)cetta_host_outcome(first)->steps_spent);
    CettaCount rows=cetta_host_outcome(winner)->results.len;
    Atom *earned=cetta_host_outcome(winner)->results.items[0];
    uint64_t paid=cetta_host_outcome(loop)->steps_spent;
    cetta_host_plan_cancel(loop);assert(!queue.count && !cetta_host_plan_task(loop));
    assert(cetta_host_outcome(loop)->completion==CETTA_EVAL_INCOMPLETE_CANCELLED &&
        cetta_host_outcome(loop)->steps_spent==paid);
    cetta_host_plan_cancel(winner);
    assert(cetta_host_outcome(winner)->results.len==rows &&
        atom_eq(cetta_host_outcome(winner)->results.items[0],earned));
    for(unsigned i=0;i<3;i++)cetta_host_decision_free(tasks[i]);
    cetta_host_decision_free(late);
    /* Cancellation did not consume this input, so another decision can use it. */
    loop=retained_begin("native/spin","native/loop",false);
    retained_open(loop,&p,"(host:transition stopped () ())",plan_limits);retained_run(loop);
    assert(cetta_host_accept(loop,0,&commit)==DURABLE_OK);cetta_host_decision_free(loop);

    for(unsigned kind=0;kind<4;kind++) {
        char input[48],actor[48];snprintf(input,sizeof(input),"native/limit/%u",kind);
        snprintf(actor,sizeof(actor),"native/limit-actor/%u",kind);
        write_record("host.inbox",input,"(received \"limited\")");
        CettaHostDecision *d=retained_begin(input,actor,false);
        CettaHostPlanLimits limits=plan_limits;
        if(kind<2)limits.work_limit=kind;
        else if(kind==2)limits.results=0;
        else limits.return_slice.transport=0u;
        retained_open(d,&p,kind<2?"(spin 0)":"(host:transition stopped () ())",limits);
        CettaOwnedTask *task=cetta_host_plan_task(d);
        CettaReadyQueue single={.capacity=1};assert(cetta_ready_admit(&single,&task->ready)==READY_ADMITTED);
        CettaOwnedReturn loan={0};uint64_t held=0;
        for(unsigned i=0;i<32;i++) {
            cetta_owned_task_visit(&single,true);out=cetta_host_outcome(d);
            if(kind<2)assert(out->steps_spent<=kind && !out->results.len);
            if(kind>=2 && task->execution.has_pending) {
                if(!loan.outcome.term) {loan=task->execution.pending;held=out->steps_spent;}
                else assert(task->execution.pending.outcome.term==loan.outcome.term && out->steps_spent==held);
            }
        }
        assert(!task->execution.terminal && cetta_host_accept(d,0,&commit)==DURABLE_PRECONDITION);
        assert(kind>=2?task->execution.has_pending:out->budget_remaining==0);
        if(kind==3) {
            CettaPrimeReturnWork work={0};
            assert(cetta_host_plan_return_work(d,&work) && !work.transport);
        }
        cetta_host_plan_cancel(d);assert(!single.count);cetta_host_decision_free(d);
    }
    /* Validation refusal precedes spending; an admitted replacement plan can
     * still use the same observation. Failed atomic attempts cannot do that. */
    write_record("host.inbox","native/retry","(received \"retry\")");
    CettaHostDecision *retry=retained_begin("native/retry","native/retry",false);
    retained_open(retry,&p,"(host:transition bad ((host:put 9 \"forbidden\" 1)) ())",plan_limits);
    retained_run(retry);assert(cetta_host_accept(retry,0,&commit)==DURABLE_INVALID);
    retained_open(retry,&p,"(host:transition good () ())",plan_limits);retained_run(retry);
    assert(cetta_host_accept(retry,0,&commit)==DURABLE_OK);cetta_host_decision_free(retry);
    retained_slot_account(&p);
    retained_credit_cases(&p);
    retained_cleanup_cases(&p);
    eval_set_library_context(&context);cetta_library_context_free(&ctx);space_free(&source);
    puts("PASS: retained Prime plans share the actual ready queue, finish beside a yielding loop, keep captured views, bound total work, retain refused loans and use one atomic durable acceptance");
}
static void retained_recovery(const char *binary,const char *path) {
    const char *crashes[]={"--prime-crash-during-plan","--prime-crash-before-accept","--prime-crash-after-accept"};
    write_record("host.inbox","native/crash","(received \"restart\")");
    size_t inputs=count("host.inbox"),receipts=count("host.commits"),effects=count("host.outbox");
    for(unsigned i=0;i<3;i++) {
        int descriptors[2];assert(!pipe(descriptors));
        cetta_durable_close(store);store=NULL;
        pid_t child=fork();assert(child>=0);
        if(!child) {
            close(descriptors[0]);char output[32];snprintf(output,sizeof(output),"%d",descriptors[1]);
            execl(binary,binary,crashes[i],path,output,(char *)NULL);_exit(127);
        }
        close(descriptors[1]);char key[33];size_t received=0;
        while(received<sizeof(key)) {
            ssize_t n=read(descriptors[0],key+received,sizeof(key)-received);
            assert(n>0);received+=(size_t)n;
        }
        close(descriptors[0]);int status;
        assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
        assert(count("host.inbox")==inputs-(i==2) && count("host.commits")==receipts+(i==2) &&
            count("host.outbox")==effects+(i==2));
        CettaDurableSnapshot saved;assert(cetta_durable_snapshot(store,"host.commits",&saved)==DURABLE_OK);
        bool found=false;
        for(size_t j=0;j<saved.count;j++)if(!strcmp(saved.records[j].key,key)) {
            Atom *receipt=NULL;
            assert(cetta_durable_value_decode(&persistent,saved.records[j].data,saved.records[j].size,&receipt)==DURABLE_OK);
            assert(atom_eq(receipt,parse("(host:commit 1 \"program/prime/1\" \"native/crash\" \"native/crash\" 0)")));
            found=true;
        }
        assert(found==(i==2));cetta_durable_snapshot_free(&saved);
    }
    CettaHostDecision *again=NULL;
    CettaHostDecisionSpec retry={"native/crash","native/crash","program/prime/1",NULL,0,NULL,0};
    assert(cetta_host_begin(store,&retry,&again)==DURABLE_PRECONDITION && !again);
    puts("PASS: native Prime restart during planning and before acceptance leaves input; lost acceptance acknowledgment recovers exactly one receipt and intent");
}

static void dependent_policy_plan(CettaHostDecision *decision,const CettaHostProgram *p,
        const char *expression,bool retained) {
    if(retained) { retained_open(decision,p,expression,plan_limits); retained_run(decision); }
    else assert(cetta_host_evaluate(decision,p,parse(expression),10000)==DURABLE_OK);
}

static void authority_dependencies(void) {
    CettaLibraryContext ctx;Space source;CettaHostProgram native;retained_program(&source,&ctx,&native);
    const char *send="(host:transition done () ((host:send 0 (sendMessage \"approved-chat\" \"allowed\") reply)))";
    for(unsigned mode=0;mode<2;mode++) for(unsigned change=0;change<4;change++) {
        char input[64],key[64],prefix[64],denial[80];
        snprintf(input,sizeof(input),"authority/%u/%u",mode,change);
        snprintf(key,sizeof(key),"channel/%u/%u",mode,change);
        snprintf(prefix,sizeof(prefix),"deny/%u/%u/",mode,change);
        snprintf(denial,sizeof(denial),"%snew",prefix);
        write_record("host.inbox",input,"(received \"guarded\")");
        write_record("host.policy",key,"\"approved-chat\"");
        CettaDurableScope scopes[]={{DURABLE_KEY,"host.policy",key},{DURABLE_PREFIX,"host.policy",prefix}};
        CettaDurableObservation *required=NULL;
        assert(cetta_durable_observe(store,scopes,2,&required)==DURABLE_OK);
        const CettaDurableSnapshot *policy=cetta_durable_observation_view(required,0);
        assert(policy->count==1 && !cetta_durable_observation_view(required,1)->count);
        Atom *destination=NULL;
        assert(cetta_durable_value_decode(&persistent,policy->records[0].data,
            policy->records[0].size,&destination)==DURABLE_OK);
        assert(destination->kind==ATOM_GROUNDED && destination->ground.gkind==GV_STRING);
        CettaHostChannelGrant channel={"telegram.send","1",send_allowed,(void *)destination->ground.sval};
        const CettaHostProgram *p=mode?&native:&host_program;
        CettaHostDecisionSpec request={input,input,p->version,NULL,0,&channel,1};
        CettaHostDecision *decision=NULL;
        assert(cetta_host_begin_with_dependencies(store,&request,required,&decision)==DURABLE_OK);
        assert(!cetta_host_view(decision,2)); /* policy reads create no extra grants */
        cetta_durable_observation_free(required);
        dependent_policy_plan(decision,p,"(host:transition bad ((host:put 0 \"grant\" stolen)) ())",mode);
        CettaHostCommit accepted;assert(cetta_host_accept(decision,0,&accepted)==DURABLE_INVALID);
        dependent_policy_plan(decision,p,send,mode);
        CettaDurableWatch *watch=NULL;assert(cetta_host_watch(decision,65536,&watch)==DURABLE_OK);
        size_t inputs=count("host.inbox"),effects=count("host.outbox"),receipts=count("host.commits");
        if(change==0) write_record("host.policy","unrelated","\"elsewhere\"");
        if(change==1 || change==2) write_record("host.policy",key,"\"different-chat\"");
        if(change==2) write_record("host.policy",key,"\"approved-chat\"");
        if(change==3) write_record("host.policy",denial,"revoked");
        assert(send_allowed(channel.context,parse("(sendMessage \"approved-chat\" \"allowed\")"),NULL));
        assert(cetta_durable_watch_current(store,watch)==(change?DURABLE_CONFLICT:DURABLE_OK));
        assert(cetta_host_accept(decision,0,&accepted)==(change?DURABLE_CONFLICT:DURABLE_OK));
        assert(count("host.inbox")==inputs-(change==0) && count("host.outbox")==effects+(change==0) &&
            count("host.commits")==receipts+(change==0));
        assert(cetta_host_accept(decision,0,&accepted)==DURABLE_CONFLICT);
        cetta_durable_watch_free(watch);cetta_host_decision_free(decision);
    }
    CettaDurableScope scope={DURABLE_KEY,"host.policy","channel/0/0"};
    CettaDurableObservation *old=NULL;assert(cetta_durable_observe(store,&scope,1,&old)==DURABLE_OK);
    write_record("host.inbox","authority/stale","(received \"stale\")");
    write_record("host.policy",scope.key,"\"new-policy\"");
    CettaHostDecisionSpec request={"authority/stale","authority/stale",host_program.version,NULL,0,channels,1};
    CettaHostDecision *decision=(void *)1;
    assert(cetta_host_begin_with_dependencies(store,&request,old,&decision)==DURABLE_CONFLICT && !decision);
    decision=(void *)1;
    assert(cetta_host_begin_with_dependencies(store,&request,NULL,&decision)==DURABLE_INVALID && !decision);
    cetta_durable_observation_free(old);
    eval_set_library_context(&context);cetta_library_context_free(&ctx);space_free(&source);
    puts("PASS: observed policy dependencies guard HE and retained Prime acceptance, watches and fresh-version revocation without granting writes or retargeting captured policy");
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
    if(argc==4) {
        assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
        CettaLibraryContext ctx;Space source;CettaHostProgram p;retained_program(&source,&ctx,&p);
        CettaHostDecision *d=retained_begin("native/crash","native/crash",true);
        retained_open(d,&p,"(plan 8)",plan_limits);
        int output=atoi(argv[3]);assert(output>2);
        assert(write(output,cetta_host_decision_key(d),33)==33);close(output);
        if(!strcmp(argv[1],"--prime-crash-during-plan")) {
            CettaReadyQueue queue={.capacity=1};CettaOwnedTask *task=cetta_host_plan_task(d);
            assert(cetta_ready_admit(&queue,&task->ready)==READY_ADMITTED);
            cetta_owned_task_visit(&queue,true);
            assert(!task->execution.terminal && !cetta_host_outcome(d)->results.len);
        } else {
            retained_run(d);
            if(!strcmp(argv[1],"--prime-crash-after-accept")) {
                CettaHostCommit c;assert(cetta_host_accept(d,0,&c)==DURABLE_OK);
            } else assert(!strcmp(argv[1],"--prime-crash-before-accept"));
        }
        _exit(0);
    }
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
    retained_host_cases();
    retained_recovery(argv[0],path);
    authority_dependencies();
    cetta_durable_close(store);
    assert(!unlink(path));
    char sidecar[300]; snprintf(sidecar,sizeof(sidecar),"%s.lock",path); unlink(sidecar);
    snprintf(sidecar,sizeof(sidecar),"%s.checkpoint",path); unlink(sidecar); assert(!rmdir(directory));
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("durable host: exact scopes, atomic consume/state/outbox, authority, denied branches, alternatives, conflicts, recovery and rho COMM passed");
}
