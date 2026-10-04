#define _POSIX_C_SOURCE 200809L
#include "durable_service.h"
#include "library.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaServiceSource config;
    uint64_t next_admit;
    int64_t parked_revision;
} Source;
struct CettaDurableService {
    CettaDurableStore *store;
    CettaDurableDispatch *dispatch;
    CettaWorkerEndpoint *worker;
    CettaServiceConfig config;
    Source sources[16];
    size_t next_source;
    CettaTimerCursor timers;
    uint64_t next_timer;
    CettaDurableStatus failed;
    CettaServiceStats stats;
    char timer_fault_keys[64][256];
    size_t timer_fault_count;
    bool timer_fault_overflow;
};
static void add(uint64_t *value, uint64_t n) { *value=n>UINT64_MAX-*value?UINT64_MAX:*value+n; }
static bool fatal(CettaDurableStatus s) {
    return s==DURABLE_IO || s==DURABLE_UNKNOWN || s==DURABLE_POISONED ||
        s==DURABLE_CORRUPT || s==DURABLE_NOMEM || s==DURABLE_LIMIT;
}
static void report(CettaDurableService *s, const char *component, CettaDurableStatus status) {
    add(&s->stats.faults,1); s->config.fault(s->config.context,component,status);
}
static CettaDurableStatus fail(CettaDurableService *s, const char *component, CettaDurableStatus status) {
    if (s->failed==DURABLE_OK) { s->failed=status; report(s,component,status); }
    return s->failed;
}
static void timer_fault(void *context, const char *key, CettaDurableStatus status) {
    CettaDurableService *s=context;
    if (status!=DURABLE_INVALID && status!=DURABLE_VERSION && status!=DURABLE_CORRUPT && status!=DURABLE_CONFLICT) {
        fail(s,"timer",status); return;
    }
    /* Bad timer intents stay visible without stopping other work or flooding
     * diagnostics on every scan. Unknown storage faults still stop admission. */
    for (size_t i=0;i<s->timer_fault_count;++i) if (!strcmp(s->timer_fault_keys[i],key)) return;
    if (s->timer_fault_count==64) {
        if (!s->timer_fault_overflow) { s->timer_fault_overflow=true; report(s,"timer-more-faults",DURABLE_LIMIT); }
        return;
    }
    strcpy(s->timer_fault_keys[s->timer_fault_count++],key);
    report(s,"timer-intent",status);
}
CettaDurableStatus cetta_service_new(CettaDurableStore *store,
        const CettaServiceConfig *c, CettaDurableService **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !c || !c->fault || c->worker_listener<0 || !c->worker ||
        c->source_count>16 || (c->source_count && !c->sources)) return DURABLE_INVALID;
    for (size_t i=0;i<c->source_count;++i) {
        const CettaServiceSource *p=&c->sources[i];
        const CettaHostProgram *program=p->program;
        if (!program || !program->version || !*program->version || strnlen(program->version,256)>255 ||
            !program->space || !program->context || !p->policy || p->fuel<=0 || p->fuel>2000000 ||
            program->context->session.language_id!=CETTA_LANGUAGE_HE ||
            !program->context->session.profile || program->context->session.profile->id!=CETTA_PROFILE_HE_EXTENDED ||
            !p->poll.credential || !p->poll.source || !*p->poll.source || strnlen(p->poll.source,65)>64 ||
            !p->poll.limit || p->poll.limit>100 || p->poll.update_count>64 ||
            (p->poll.update_count && !p->poll.allowed_updates) || c->dispatch.timeout_ms<1000 ||
            p->poll.wait_seconds>(c->dispatch.timeout_ms-1000)/1000) return DURABLE_INVALID;
        char unused[96], input[168];
        if (cetta_inbox_keys(p->poll.source,"validate",0,unused,input)!=DURABLE_OK) return DURABLE_INVALID;
        for (size_t k=0;k<i;++k)
            if (!strcmp(c->sources[k].poll.source,p->poll.source) ||
                c->sources[k].poll.credential==p->poll.credential) return DURABLE_INVALID;
    }
    CettaDurableService *s=calloc(1,sizeof(*s)); if (!s) return DURABLE_NOMEM;
    s->store=store; s->config=*c;
    for (size_t i=0;i<c->source_count;++i) {
        s->sources[i].config=c->sources[i]; s->sources[i].parked_revision=-1;
    }
    CettaDurableStatus status=cetta_dispatch_new(store,&c->dispatch,&s->dispatch);
    if (status==DURABLE_OK)
        status=cetta_worker_endpoint_new(store,c->worker_listener,c->worker_uid,c->worker,&s->worker);
    if (status!=DURABLE_OK) { cetta_service_free(s); return status; }
    *out=s; return DURABLE_OK;
}
static bool complete(const EvalOutcome *o) {
    return o && o->completion==CETTA_EVAL_COMPLETE && !o->effect_denials &&
        o->budget_limited && o->budget_initial && o->results.len==1;
}
static CettaDurableStatus poll_source(CettaDurableService *s, Source *source, CettaClockSample now) {
    const CettaServiceSource *c=&source->config;
    CettaTelegramIntake *t=NULL;
    CettaDurableStatus status=cetta_telegram_intake_begin(s->store,c->poll.source,c->program->version,&t);
    if (status==DURABLE_PRECONDITION) {
        if (now.monotonic_ms<source->next_admit) return DURABLE_OK;
        /* Even an instantly empty or faulty mock/provider cannot cause a tight
         * request loop. Durable retry deadlines impose their stronger bound. */
        source->next_admit=now.monotonic_ms+100;
        status=cetta_dispatch_poll_at(s->dispatch,&c->poll,now.utc_ms);
        if (status==DURABLE_BUSY || status==DURABLE_PRECONDITION) return DURABLE_OK;
        return status;
    }
    if (status!=DURABLE_OK) return status;
    int64_t revision=cetta_telegram_intake_revision(t);
    if (source->parked_revision==revision) { cetta_telegram_intake_free(t); return DURABLE_OK; }
    bool held=cetta_telegram_intake_held(t);
    if (held) status=DURABLE_PRECONDITION;
    else status=cetta_telegram_intake_evaluate(t,c->program,c->policy,c->fuel);
    if (status==DURABLE_OK && !complete(cetta_telegram_intake_outcome(t))) status=DURABLE_PRECONDITION;
    if (status==DURABLE_OK) {
        CettaInboxCommit committed;
        status=cetta_telegram_intake_commit(t,&committed);
        if (status==DURABLE_OK) { add(&s->stats.poll_batches,1); add(&s->stats.inputs,committed.inserted); }
        else if (status==DURABLE_PRECONDITION) {
            status=cetta_telegram_intake_evaluate_recovery(t,c->program,c->policy,now.utc_ms,c->fuel);
            if (status==DURABLE_OK && !complete(cetta_telegram_intake_outcome(t))) status=DURABLE_PRECONDITION;
            int64_t committed;
            if (status==DURABLE_OK) status=cetta_telegram_intake_commit_recovery(t,&committed);
            if (status==DURABLE_OK) add(&s->stats.recoveries,1);
        }
    }
    if (status!=DURABLE_OK && status!=DURABLE_CONFLICT && !fatal(status)) {
        source->parked_revision=revision; report(s,held?"poll-held":"poll-policy",status); status=DURABLE_OK;
    }
    cetta_telegram_intake_free(t);
    return status==DURABLE_CONFLICT?DURABLE_OK:status;
}
CettaDurableStatus cetta_service_step(CettaDurableService *s, unsigned max_wait) {
    if (!s || max_wait>100) return DURABLE_INVALID;
    if (s->failed!=DURABLE_OK) return s->failed;
    size_t requests=0;
    CettaDurableStatus status=cetta_worker_endpoint_step(s->worker,max_wait,&requests);
    if (status!=DURABLE_OK) return fail(s,"worker",status);
    add(&s->stats.worker_requests,requests); add(&s->stats.steps,1);
    CettaClockSample now;
    if (!cetta_clock_sample(&now)) return fail(s,"clock",DURABLE_IO);
    if (now.monotonic_ms>=s->next_timer) {
        CettaTimerTick tick;
        status=cetta_timer_tick(s->store,&s->timers,now.utc_ms,8,timer_fault,s,&tick);
        if (status!=DURABLE_OK) return fail(s,"timer",status);
        if (s->failed!=DURABLE_OK) return s->failed;
        add(&s->stats.timer_events,tick.emitted);
        s->next_timer=now.monotonic_ms+(tick.more_due?0:cetta_timer_wait_ms(tick.next_deadline_ms,&now,now.monotonic_ms));
    }
    if (s->config.source_count) {
        Source *source=&s->sources[s->next_source];
        s->next_source=(s->next_source+1)%s->config.source_count;
        status=poll_source(s,source,now);
        if (status!=DURABLE_OK) return fail(s,"poll",status);
    }
    return DURABLE_OK;
}
void cetta_service_stats(const CettaDurableService *s, CettaServiceStats *out) {
    if (out) *out=s?s->stats:(CettaServiceStats){0};
}
bool cetta_service_binding(const CettaDurableService *s, const char *source,
        const char *worker, const CettaDispatchChannel *expected) {
    if (!s || !source || !worker || strcmp(worker,s->config.worker) || !expected ||
        !expected->name || !expected->version || !expected->plan) return false;
    const CettaTelegramCredential *credential=NULL;
    for (size_t i=0;i<s->config.source_count;++i)
        if (!strcmp(source,s->sources[i].config.poll.source)) credential=s->sources[i].config.poll.credential;
    if (!credential || (expected->credential && expected->credential!=credential)) return false;
    for (size_t i=0;i<s->config.dispatch.channel_count;++i) {
        const CettaDispatchChannel *c=&s->config.dispatch.channels[i];
        if (!strcmp(c->name,expected->name) && !strcmp(c->version,expected->version) &&
            c->credential==credential && c->context==expected->context && c->plan==expected->plan) return true;
    }
    return false;
}
void cetta_service_changed(CettaDurableService *s) { if (s) s->next_timer=0; }
CettaDurableStatus cetta_service_submit(CettaDurableService *s, const char *key) {
    return !s?DURABLE_INVALID:s->failed!=DURABLE_OK?s->failed:cetta_dispatch_submit(s->dispatch,key);
}
CettaDurableStatus cetta_service_cancel(CettaDurableService *s, const char *key) {
    return !s?DURABLE_INVALID:s->failed!=DURABLE_OK?s->failed:cetta_dispatch_cancel(s->dispatch,key);
}
size_t cetta_service_free(CettaDurableService *s) {
    if (!s) return 0;
    cetta_worker_endpoint_free(s->worker);
    size_t pending=cetta_dispatch_free(s->dispatch); free(s); return pending;
}
