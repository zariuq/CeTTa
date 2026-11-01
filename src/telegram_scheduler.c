#define _POSIX_C_SOURCE 200809L
#include "telegram_scheduler.h"
#include "durable_worker_host.h"
#include "ready_queue.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    int64_t revision, position;
} Cursor;
static bool earlier(Cursor a, Cursor b) {
    return a.revision<b.revision || (a.revision==b.revision && a.position<b.position);
}
typedef struct {
    CettaReadyLink ready; /* first member; entry addresses stay stable */
    char key[256];
    Cursor cursor;
    CettaDurableWatch *watch;
    bool seen, reported;
} Entry;
struct CettaTelegramScheduler {
    CettaDurableStore *store;
    CettaDurableService *service;
    CettaTelegramSchedulerConfig config;
    Entry *inputs, *effects;
    CettaReadyQueue input_ready, effect_ready;
    size_t watch_bytes;
    CettaDurableWatch *inbox;
    bool output_turn, effects_behind, workers_waiting, inputs_behind;
    Cursor input_from;
    /* Exact occurrence positions, including multiple effects in one commit. */
    Cursor behind_from, waiting_from;
    CettaDurableWatch *ready; /* the agent's unread tasks, while requests wait */
    CettaDurableStatus failed;
    CettaTelegramSchedulerStats stats;
};
static CettaDurableStatus watch_ready(CettaTelegramScheduler *s);
static void rewind_to(CettaTelegramScheduler *s, Cursor cursor) {
    if (!s->effects_behind || earlier(cursor,s->behind_from)) s->behind_from=cursor;
    s->effects_behind=true;
}
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
static void remove_entry(CettaTelegramScheduler *s, Entry *e) {
    drop_watch(s,&e->watch);
    cetta_ready_remove(e->ready.owner,&e->ready);
    *e=(Entry){0};
}
static CettaDurableStatus append(Entry *entries, CettaReadyQueue *queue,
        const char *key, Cursor cursor) {
    if (!key || !*key || strnlen(key,256)>255) return DURABLE_INVALID;
    for (CettaReadyLink *p=queue->first;p;p=p->next)
        if (!strcmp(((Entry *)p)->key,key)) return DURABLE_OK;
    if (queue->count==queue->capacity) return DURABLE_LIMIT;
    for (size_t i=0;i<queue->capacity;++i) if (!entries[i].ready.owner) {
        Entry *e=&entries[i]; *e=(Entry){.cursor=cursor}; strcpy(e->key,key);
        if (cetta_ready_admit(queue,&e->ready)!=READY_ADMITTED) return DURABLE_CORRUPT;
        return DURABLE_OK;
    }
    return DURABLE_CORRUPT;
}
static CettaDurableStatus key_watch(CettaTelegramScheduler *s, const char *space, Entry *e) {
    CettaDurableScope q={DURABLE_KEY,space,e->key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus r=cetta_durable_observe(s->store,&q,1,&o);
    if (r==DURABLE_OK) r=cetta_durable_watch_new(o,remaining(s),&e->watch);
    cetta_durable_observation_free(o); if (r==DURABLE_OK) adopt(s,e->watch); return r;
}
typedef struct { const char *key;size_t index,lane; } Submission;
static int compare_submission(const void *a,const void *b) {
    return strcmp(((const Submission *)a)->key,((const Submission *)b)->key);
}
/* Admission hint only: the decision still validates lane ordering against its
 * captured reads. Waiting successors cannot crowd out delivery completions. */
static bool *input_candidates(const CettaDurableSnapshot *v) {
    bool *eligible=calloc(v->count?v->count:1,sizeof(*eligible));
    Submission *submissions=calloc(v->count?v->count:1,sizeof(*submissions));
    if (!eligible || !submissions) {free(eligible);free(submissions);return NULL;}
    size_t count=0;
    for (size_t i=0;i<v->count;++i) {
        eligible[i]=true;const char *key=v->records[i].key;
        const char *last=strrchr(key,'.');
        if (!strncmp(key,"submission/",11) && last && last[1])
            submissions[count++]=(Submission){key,i,(size_t)(last-key)+1};
    }
    qsort(submissions,count,sizeof(*submissions),compare_submission);
    for (size_t i=1;i<count;++i) {
        const Submission *p=&submissions[i-1],*c=&submissions[i];
        if (p->lane==c->lane && !strncmp(p->key,c->key,c->lane)) eligible[c->index]=false;
    }
    free(submissions);return eligible;
}
static CettaDurableStatus refresh_inputs(CettaTelegramScheduler *s) {
    CettaDurableStatus r=s->inbox?cetta_durable_watch_current(s->store,s->inbox):DURABLE_CONFLICT;
    if (r!=DURABLE_OK && r!=DURABLE_CONFLICT) return r;
    if (r==DURABLE_OK && !s->inputs_behind) return r;
    if (!s->inputs_behind) s->input_from=(Cursor){-1,-1};
    drop_watch(s,&s->inbox);
    CettaDurableScope q={DURABLE_SPACE,"host.inbox",NULL}; CettaDurableObservation *o=NULL;
    r=cetta_durable_observe(s->store,&q,1,&o); if (r!=DURABLE_OK) return r;
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
    bool *eligible=input_candidates(v);
    if (!eligible) {cetta_durable_observation_free(o);return DURABLE_NOMEM;}
    for (CettaReadyLink *p=s->input_ready.first;p;p=p->next) ((Entry *)p)->seen=false;
    /* Remove consumed entries before adding arrivals, so a full queue can
     * make progress without a transient extra slot. */
    for (CettaReadyLink *p=s->input_ready.first;p;p=p->next)
        for (size_t j=0;j<v->count;++j) if (eligible[j] && !strcmp(((Entry *)p)->key,v->records[j].key)) {
            Entry *e=(Entry *)p; e->seen=true;
            if (e->cursor.revision!=v->records[j].revision) {
                drop_watch(s,&e->watch); e->reported=false;
                e->cursor=(Cursor){v->records[j].revision,v->records[j].position};
            }
            break;
        }
    for (CettaReadyLink *p=s->input_ready.first,*next;p;p=next) {
        next=p->next;
        if (!((Entry *)p)->seen) remove_entry(s,(Entry *)p);
    }
    /* Keep a moving admission window. Waiting entries may leave RAM, but
     * remain durable. Always restarting at the oldest prefix can exclude the
     * very completion that releases those entries. */
    s->inputs_behind=false;
    for (size_t j=0;j<v->count && r==DURABLE_OK;++j) {
        Cursor cursor={v->records[j].revision,v->records[j].position};
        if (!eligible[j] || !earlier(s->input_from,cursor)) continue;
        r=append(s->inputs,&s->input_ready,v->records[j].key,cursor);
        if (r==DURABLE_LIMIT) { s->inputs_behind=true;r=DURABLE_OK;break; }
        s->input_from=cursor;
    }
    if (r==DURABLE_OK) r=cetta_durable_watch_new(o,remaining(s),&s->inbox);
    if (r==DURABLE_OK) adopt(s,s->inbox);
    free(eligible);cetta_durable_observation_free(o); return r;
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
                e->cursor.revision=-1;
                /* Discovery gets exact journal positions on the next step;
                 * no accepted effect runs outside the candidate visit loop. */
                if (c.effects) rewind_to(s,(Cursor){c.revision,0});
                cetta_host_decision_free(d); return r;
            }
        }
        if (r==DURABLE_CONFLICT || r==DURABLE_BUSY) { cetta_host_decision_free(d); return DURABLE_OK; }
        if (!fatal(r)) {
            CettaDurableStatus w=cetta_host_watch(d,remaining(s),&e->watch);
            if (w==DURABLE_LIMIT) w=cetta_host_watch_revision(d,remaining(s),&e->watch);
            if (w!=DURABLE_OK) r=w; else adopt(s,e->watch);
        }
        if (!empty && !fatal(r) && !e->reported) { report(s,"input-policy",e->key,r); e->reported=true; }
    } else if (!e->reported) { report(s,"input-route",e->key,r); e->reported=true; }
    cetta_host_decision_free(d);
    if (fatal(r)) return r;
    if (!e->watch) return key_watch(s,"host.inbox",e);
    return DURABLE_OK;
}
static CettaDurableStatus output(CettaTelegramScheduler *s, Entry *e) {
    CettaTelegramAdmission a;
    CettaDurableStatus r=cetta_telegram_agent_admit(s->store,&s->config.agent,e->key,remaining(s),&a,&e->watch);
    adopt(s,e->watch);
    if (r==DURABLE_CONFLICT || r==DURABLE_BUSY) { drop_watch(s,&e->watch); return DURABLE_OK; }
    if (fatal(r)) return r;
    if (r==DURABLE_OK && (a==TELEGRAM_DONE || a==TELEGRAM_FOREIGN)) {
        remove_entry(s,e); return DURABLE_OK;
    }
    if (r==DURABLE_OK && (a==TELEGRAM_WORKER || a==TELEGRAM_CONTROL_WORKER)) {
        char id[65];
        r=a==TELEGRAM_CONTROL_WORKER
            ? cetta_worker_register_control(s->store,e->key,s->config.agent.worker,id)
            : cetta_worker_register(s->store,e->key,s->config.agent.worker,id);
        if (r==DURABLE_OK) {
            increment(&s->stats.published); remove_entry(s,e); return DURABLE_OK;
        }
        /* Resource pressure leaves the immutable request in
         * the outbox and leaves this queue, which belongs to work that can
         * proceed; it is offered again when the agent reads a task. An absent
         * agent never stops the service or delays its replies. */
        if (r==DURABLE_LIMIT) {
            if (!s->workers_waiting || earlier(e->cursor,s->waiting_from)) s->waiting_from=e->cursor;
            s->workers_waiting=true;
            if (!s->ready) r=watch_ready(s); else r=DURABLE_OK;
            if (r==DURABLE_OK) remove_entry(s,e);
            return r;
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
/* The store orders records by revision and operation position. Resume at the
 * first untracked occurrence, not at its entire commit: otherwise a queue of
 * one repeatedly rediscovers a completed first effect and starves its siblings.
 * Discovery performs no policy evaluation, worker publication or transport. */
static CettaDurableStatus catch_up(CettaTelegramScheduler *s) {
    CettaDurableSnapshot v={0};
    CettaDurableStatus r=cetta_durable_snapshot(s->store,"host.outbox",&v);
    if (r==DURABLE_OK) s->effects_behind=false;
    for (size_t i=0;r==DURABLE_OK && i<v.count;++i) {
        Cursor cursor={v.records[i].revision,v.records[i].position};
        if (earlier(cursor,s->behind_from)) continue;
        r=append(s->effects,&s->effect_ready,v.records[i].key,cursor);
        if (r==DURABLE_LIMIT) {
            rewind_to(s,cursor); r=DURABLE_OK; break;
        }
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
    s->input_ready.capacity=c->pending_inputs;
    s->effect_ready.capacity=c->pending_effects;
    s->inputs=calloc(c->pending_inputs,sizeof(*s->inputs)); s->effects=calloc(c->pending_effects,sizeof(*s->effects));
    if (!s->inputs || !s->effects) { cetta_telegram_scheduler_free(s); return DURABLE_NOMEM; }
    CettaDurableStatus r=catch_up(s);
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
    if (s->workers_waiting) {
        r=s->ready?cetta_durable_watch_current(s->store,s->ready):DURABLE_CONFLICT;
        if (r==DURABLE_BUSY) return DURABLE_OK;
        if (r==DURABLE_CONFLICT) {
            drop_watch(s,&s->ready); rewind_to(s,s->waiting_from); s->workers_waiting=false;
        }
        else if (r!=DURABLE_OK) return failed(s,r);
    }
    if (s->effects_behind && s->effect_ready.count<=s->config.pending_effects*3/4) {
        r=catch_up(s);
        if (r==DURABLE_BUSY) return DURABLE_OK;
        if (r!=DURABLE_OK) return failed(s,r);
    }
    while (visits--) {
        s->output_turn=!s->output_turn;
        bool send=s->output_turn;
        CettaReadyQueue *queue=send?&s->effect_ready:&s->input_ready;
        Entry *e=(Entry *)cetta_ready_rotate(queue);
        if (!e) continue;
        if (e->watch) {
            r=cetta_durable_watch_current(s->store,e->watch);
            if (r==DURABLE_BUSY) continue;
            if (r==DURABLE_OK) {
                if (!send && s->inputs_behind) remove_entry(s,e);
                continue;
            }
            if (r!=DURABLE_CONFLICT) return failed(s,r);
            drop_watch(s,&e->watch); e->reported=false;
        }
        r=send?output(s,e):input(s,e);
        if (r==DURABLE_BUSY) continue;
        if (r!=DURABLE_OK) return failed(s,r);
        if (!send && (e->cursor.revision==-1 || (e->watch && s->inputs_behind))) remove_entry(s,e);
    }
    return DURABLE_OK;
}
void cetta_telegram_scheduler_stats(const CettaTelegramScheduler *s, CettaTelegramSchedulerStats *out) {
    if (out) *out=s?s->stats:(CettaTelegramSchedulerStats){0};
}
void cetta_telegram_scheduler_free(CettaTelegramScheduler *s) {
    if (!s) return;
    while (s->input_ready.first) remove_entry(s,(Entry *)s->input_ready.first);
    while (s->effect_ready.first) remove_entry(s,(Entry *)s->effect_ready.first);
    cetta_durable_watch_free(s->inbox); cetta_durable_watch_free(s->ready); free(s->inputs); free(s->effects); free(s);
}
