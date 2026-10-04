#define _POSIX_C_SOURCE 200809L
#include "telegram_scheduler.h"
#include "durable_worker_host.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char key[256];
    int64_t revision;
    CettaDurableWatch *watch;
    bool seen, reported;
} Entry;
struct CettaTelegramScheduler {
    CettaDurableStore *store;
    CettaDurableService *service;
    CettaTelegramSchedulerConfig config;
    Entry *inputs, *effects;
    size_t input_count, effect_count, next_input, next_effect, watch_bytes;
    CettaDurableWatch *inbox;
    bool output_turn, effects_behind, workers_waiting;
    /* Commit revisions of the oldest effect left untracked by a full queue,
     * and of the oldest request waiting for the agent to read its tasks. */
    int64_t behind_from, waiting_from;
    CettaDurableWatch *ready; /* the agent's unread tasks, while requests wait */
    CettaDurableStatus failed;
    CettaTelegramSchedulerStats stats;
};
static void increment(uint64_t *n) { if (*n<UINT64_MAX) ++*n; }
static void drop_watch(CettaTelegramScheduler *s, CettaDurableWatch **w) {
    s->watch_bytes-=cetta_durable_watch_bytes(*w); cetta_durable_watch_free(*w); *w=NULL;
}
static void adopt(CettaTelegramScheduler *s, CettaDurableWatch *w) { s->watch_bytes+=cetta_durable_watch_bytes(w); }
static size_t remaining(CettaTelegramScheduler *s) { return s->config.watch_bytes-s->watch_bytes; }
static bool fatal(CettaDurableStatus r) {
    return r==DURABLE_IO || r==DURABLE_POISONED || r==DURABLE_UNKNOWN ||
        r==DURABLE_NOMEM || r==DURABLE_LIMIT || r==DURABLE_CORRUPT;
}
static void report(CettaTelegramScheduler *s, const char *component, const char *key, CettaDurableStatus r) {
    increment(&s->stats.faults); s->config.fault(s->config.context,component,key,r);
}
static CettaDurableStatus failed(CettaTelegramScheduler *s, CettaDurableStatus r) {
    if (s->failed==DURABLE_OK) { s->failed=r; report(s,"scheduler","",r); }
    return s->failed;
}
static void remove_entry(CettaTelegramScheduler *s, Entry *entries, size_t *count, size_t i) {
    drop_watch(s,&entries[i].watch);
    memmove(entries+i,entries+i+1,(*count-i-1)*sizeof(*entries)); --*count;
}
static CettaDurableStatus append(Entry *entries, size_t *count, size_t limit, const char *key, int64_t revision) {
    if (!key || !*key || strnlen(key,256)>255) return DURABLE_INVALID;
    for (size_t i=0;i<*count;++i) if (!strcmp(entries[i].key,key)) return DURABLE_OK;
    if (*count==limit) return DURABLE_LIMIT;
    Entry *e=&entries[(*count)++]; *e=(Entry){.revision=revision}; strcpy(e->key,key); return DURABLE_OK;
}
static CettaDurableStatus key_watch(CettaTelegramScheduler *s, const char *space, Entry *e) {
    CettaDurableScope q={DURABLE_KEY,space,e->key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus r=cetta_durable_observe(s->store,&q,1,&o);
    if (r==DURABLE_OK) r=cetta_durable_watch_new(o,remaining(s),&e->watch);
    cetta_durable_observation_free(o); if (r==DURABLE_OK) adopt(s,e->watch); return r;
}
static CettaDurableStatus refresh_inputs(CettaTelegramScheduler *s) {
    CettaDurableStatus r=s->inbox?cetta_durable_watch_current(s->store,s->inbox):DURABLE_CONFLICT;
    if (r!=DURABLE_CONFLICT) return r;
    drop_watch(s,&s->inbox);
    CettaDurableScope q={DURABLE_SPACE,"host.inbox",NULL}; CettaDurableObservation *o=NULL;
    r=cetta_durable_observe(s->store,&q,1,&o); if (r!=DURABLE_OK) return r;
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
    for (size_t i=0;i<s->input_count;++i) s->inputs[i].seen=false;
    /* Remove consumed entries before adding arrivals, so a full queue can
     * make progress without a transient extra slot. */
    for (size_t i=0;i<s->input_count;++i)
        for (size_t j=0;j<v->count;++j) if (!strcmp(s->inputs[i].key,v->records[j].key)) {
            Entry *e=&s->inputs[i]; e->seen=true;
            if (e->revision!=v->records[j].revision) { drop_watch(s,&e->watch); e->reported=false; e->revision=v->records[j].revision; }
            break;
        }
    for (size_t i=s->input_count;i>0;--i) if (!s->inputs[i-1].seen) remove_entry(s,s->inputs,&s->input_count,i-1);
    /* A full queue is backpressure, not a fault: inputs past its capacity stay
     * in the inbox and are added as consumed entries leave it, since every
     * consumption changes the inbox this refresh watches. */
    for (size_t j=0;j<v->count && r==DURABLE_OK;++j)
        r=append(s->inputs,&s->input_count,s->config.pending_inputs,v->records[j].key,v->records[j].revision);
    if (r==DURABLE_LIMIT) r=DURABLE_OK;
    if (r==DURABLE_OK) r=cetta_durable_watch_new(o,remaining(s),&s->inbox);
    if (r==DURABLE_OK) adopt(s,s->inbox);
    cetta_durable_observation_free(o); return r;
}
static CettaDurableStatus input(CettaTelegramScheduler *s, Entry *e) {
    CettaHostDecision *d=NULL;
    CettaDurableStatus r=cetta_telegram_agent_decide(s->store,&s->config.agent,e->key,&d);
    if (r==DURABLE_CONFLICT || r==DURABLE_BUSY) return DURABLE_OK;
    if (fatal(r)) { cetta_host_decision_free(d); return r; }
    if (r==DURABLE_OK) {
        increment(&s->stats.evaluations);
        const EvalOutcome *o=cetta_host_outcome(d);
        bool empty=o && o->completion==CETTA_EVAL_COMPLETE && !o->effect_denials &&
            (!o->results.len || (o->results.len==1 && atom_is_symbol(o->results.items[0],"Empty")));
        if (empty) { increment(&s->stats.blocked); r=DURABLE_PRECONDITION; }
        else if (!o || o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials || o->results.len!=1) r=DURABLE_INVALID;
        else {
            CettaHostCommit c; r=cetta_host_accept(d,0,&c);
            if (r==DURABLE_OK) {
                increment(&s->stats.accepted); cetta_service_changed(s->service);
                e->revision=-1;
                for (size_t i=0;i<c.effects && r==DURABLE_OK;++i) {
                    char key[64]; snprintf(key,sizeof(key),"%s/%zu",c.commit_key,i);
                    r=append(s->effects,&s->effect_count,s->config.pending_effects,key,c.revision);
                }
                /* Accepted effects are durable in the outbox; past the queue's
                 * capacity they are picked up later by catch_up. */
                if (r==DURABLE_LIMIT) {
                    if (!s->effects_behind || c.revision<s->behind_from) s->behind_from=c.revision;
                    s->effects_behind=true; r=DURABLE_OK;
                }
                cetta_host_decision_free(d); return r;
            }
        }
        if (r==DURABLE_CONFLICT || r==DURABLE_BUSY) { cetta_host_decision_free(d); return DURABLE_OK; }
        if (!fatal(r)) {
            CettaDurableStatus w=cetta_host_watch(d,remaining(s),&e->watch);
            if (w!=DURABLE_OK) r=w; else adopt(s,e->watch);
        }
        if (!empty && !fatal(r) && !e->reported) { report(s,"input-policy",e->key,r); e->reported=true; }
    } else if (!e->reported) { report(s,"input-route",e->key,r); e->reported=true; }
    cetta_host_decision_free(d);
    if (fatal(r)) return r;
    if (!e->watch) return key_watch(s,"host.inbox",e);
    return DURABLE_OK;
}
static CettaDurableStatus output(CettaTelegramScheduler *s, size_t index) {
    Entry *e=&s->effects[index]; CettaTelegramAdmission a;
    CettaDurableStatus r=cetta_telegram_agent_admit(s->store,&s->config.agent,e->key,remaining(s),&a,&e->watch);
    adopt(s,e->watch);
    if (r==DURABLE_CONFLICT || r==DURABLE_BUSY) { drop_watch(s,&e->watch); return DURABLE_OK; }
    if (fatal(r)) return r;
    if (r==DURABLE_OK && (a==TELEGRAM_DONE || a==TELEGRAM_FOREIGN)) {
        remove_entry(s,s->effects,&s->effect_count,index); return DURABLE_OK;
    }
    if (r==DURABLE_OK && a==TELEGRAM_WORKER) {
        char id[65]; r=cetta_worker_register(s->store,e->key,s->config.agent.worker,id);
        if (r==DURABLE_OK) {
            increment(&s->stats.published); remove_entry(s,s->effects,&s->effect_count,index); return DURABLE_OK;
        }
        /* The agent has as many unread tasks as it may. The request stays in
         * the outbox and leaves this queue, which belongs to work that can
         * proceed; it is offered again when the agent reads a task. An absent
         * agent never stops the service or delays its replies. */
        if (r==DURABLE_LIMIT) {
            if (!s->workers_waiting || e->revision<s->waiting_from) s->waiting_from=e->revision;
            s->workers_waiting=true; remove_entry(s,s->effects,&s->effect_count,index); return DURABLE_OK;
        }
    } else if (r==DURABLE_OK && a==TELEGRAM_SEND) {
        r=cetta_service_submit(s->service,e->key);
        if (r==DURABLE_OK) increment(&s->stats.admissions);
        /* The transport queue being full is transient. Its capacity is not
         * permission to forget accepted work or resubmit a claimed effect. */
        if (r==DURABLE_LIMIT || r==DURABLE_BUSY) { drop_watch(s,&e->watch); return DURABLE_OK; }
        if (r==DURABLE_PRECONDITION) return DURABLE_OK;
    }
    if (r==DURABLE_BUSY || r==DURABLE_CONFLICT) { drop_watch(s,&e->watch); return DURABLE_OK; }
    if (r!=DURABLE_OK && !fatal(r) && !e->reported) { report(s,"output-policy",e->key,r); e->reported=true; }
    if (fatal(r)) return r;
    if (!e->watch) return key_watch(s,"host.outbox",e);
    return DURABLE_OK;
}
/* Track the outbox's effects committed at or after revision FROM, up to
 * the queue's capacity. Completed ones retire at once, so history never
 * fills the queue; older ones were tracked already and are skipped. FROM is
 * at most the oldest untracked or waiting effect, so both are recomputed. */
static CettaDurableStatus catch_up(CettaTelegramScheduler *s, int64_t from) {
    CettaDurableSnapshot v={0};
    CettaDurableStatus r=cetta_durable_snapshot(s->store,"host.outbox",&v);
    if (r==DURABLE_OK) s->effects_behind=s->workers_waiting=false;
    for (size_t i=0;r==DURABLE_OK && i<v.count;++i) {
        if (v.records[i].revision<from) continue;
        size_t before=s->effect_count;
        r=append(s->effects,&s->effect_count,s->config.pending_effects,v.records[i].key,v.records[i].revision);
        if (r==DURABLE_LIMIT) {
            if (!s->effects_behind || v.records[i].revision<s->behind_from) s->behind_from=v.records[i].revision;
            s->effects_behind=true; r=DURABLE_OK; continue;
        }
        if (r==DURABLE_OK && s->effect_count>before) r=output(s,s->effect_count-1);
    }
    cetta_durable_snapshot_free(&v); return r;
}
static CettaDurableStatus watch_ready(CettaTelegramScheduler *s) {
    char prefix[80]; snprintf(prefix,sizeof(prefix),"%s/",s->config.agent.worker);
    CettaDurableScope q={DURABLE_PREFIX,"host.worker-ready",prefix}; CettaDurableObservation *o=NULL;
    CettaDurableStatus r=cetta_durable_observe(s->store,&q,1,&o);
    if (r==DURABLE_OK) r=cetta_durable_watch_new(o,remaining(s),&s->ready);
    cetta_durable_observation_free(o); if (r==DURABLE_OK) adopt(s,s->ready); return r;
}
CettaDurableStatus cetta_telegram_scheduler_new(CettaDurableStore *store, CettaDurableService *service,
        const CettaTelegramSchedulerConfig *c, CettaTelegramScheduler **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !service || !c || !cetta_telegram_agent_valid(&c->agent) || !c->fault || !c->pending_inputs || c->pending_inputs>4096 ||
        !c->pending_effects || c->pending_effects>4096 || c->watch_bytes<1024 || c->watch_bytes>64*1024*1024) return DURABLE_INVALID;
    CettaDispatchChannel expected={"telegram.action","1",NULL,(void *)c->agent.actions,cetta_telegram_action_plan};
    if (!cetta_service_binding(service,c->agent.source,c->agent.worker,&expected)) return DURABLE_INVALID;
    CettaTelegramScheduler *s=calloc(1,sizeof(*s)); if (!s) return DURABLE_NOMEM;
    s->store=store; s->service=service; s->config=*c;
    s->inputs=calloc(c->pending_inputs,sizeof(*s->inputs)); s->effects=calloc(c->pending_effects,sizeof(*s->effects));
    if (!s->inputs || !s->effects) { cetta_telegram_scheduler_free(s); return DURABLE_NOMEM; }
    CettaDurableStatus r=catch_up(s,0);
    if (r==DURABLE_OK && s->workers_waiting) r=watch_ready(s);
    if (r!=DURABLE_OK) { cetta_telegram_scheduler_free(s); return r; }
    *out=s; return DURABLE_OK;
}
CettaDurableStatus cetta_telegram_scheduler_step(CettaTelegramScheduler *s, unsigned visits) {
    if (!s || !visits || visits>64) return DURABLE_INVALID;
    if (s->failed!=DURABLE_OK) return s->failed;
    CettaDurableStatus r=refresh_inputs(s);
    if (r==DURABLE_BUSY) return DURABLE_OK;
    if (r!=DURABLE_OK) return failed(s,r);
    /* Effects left untracked by a full queue, once a quarter of it is free,
     * and requests waiting for the agent, once it has read a task. */
    bool room=s->effects_behind && s->effect_count<=s->config.pending_effects*3/4, read=false;
    if (s->workers_waiting) {
        r=s->ready?cetta_durable_watch_current(s->store,s->ready):DURABLE_CONFLICT;
        if (r==DURABLE_BUSY) return DURABLE_OK;
        if (r==DURABLE_CONFLICT) { if (s->ready) drop_watch(s,&s->ready); read=true; }
        else if (r!=DURABLE_OK) return failed(s,r);
    }
    if (room || read) {
        int64_t from=INT64_MAX;
        if (s->effects_behind && s->behind_from<from) from=s->behind_from;
        if (s->workers_waiting && s->waiting_from<from) from=s->waiting_from;
        r=catch_up(s,from);
        if (r==DURABLE_OK && s->workers_waiting && !s->ready) r=watch_ready(s);
        if (r==DURABLE_OK && !s->workers_waiting && s->ready) drop_watch(s,&s->ready);
        if (r==DURABLE_BUSY) return DURABLE_OK;
        if (r!=DURABLE_OK) return failed(s,r);
    }
    while (visits--) {
        s->output_turn=!s->output_turn;
        bool send=s->output_turn;
        Entry *entries=send?s->effects:s->inputs;
        size_t count=send?s->effect_count:s->input_count, *next=send?&s->next_effect:&s->next_input;
        if (!count) continue;
        size_t i=*next%count; *next=(i+1)%count; Entry *e=&entries[i];
        if (e->watch) {
            r=cetta_durable_watch_current(s->store,e->watch);
            if (r==DURABLE_BUSY) continue;
            if (r==DURABLE_OK) continue;
            if (r!=DURABLE_CONFLICT) return failed(s,r);
            drop_watch(s,&e->watch); e->reported=false;
        }
        r=send?output(s,i):input(s,e);
        if (r==DURABLE_BUSY) continue;
        if (r!=DURABLE_OK) return failed(s,r);
        if (!send && e->revision==-1) remove_entry(s,s->inputs,&s->input_count,i);
    }
    return DURABLE_OK;
}
void cetta_telegram_scheduler_stats(const CettaTelegramScheduler *s, CettaTelegramSchedulerStats *out) {
    if (out) *out=s?s->stats:(CettaTelegramSchedulerStats){0};
}
void cetta_telegram_scheduler_free(CettaTelegramScheduler *s) {
    if (!s) return;
    for (size_t i=0;i<s->input_count;++i) cetta_durable_watch_free(s->inputs[i].watch);
    for (size_t i=0;i<s->effect_count;++i) cetta_durable_watch_free(s->effects[i].watch);
    cetta_durable_watch_free(s->inbox); cetta_durable_watch_free(s->ready); free(s->inputs); free(s->effects); free(s);
}
