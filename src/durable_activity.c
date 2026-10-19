#define _POSIX_C_SOURCE 200809L
#include "durable_activity.h"
#include "durable_timer.h"
#include "durable_value.h"
#include "durable_worker_host.h"
#include "library.h"
#include "ready_queue.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    CettaReadyLink link;
    char key[256];
    CettaDurableWatch *watch;
    CettaHostDecision *decision;
    int64_t revision, position;
    bool seen;
} Candidate;
struct CettaActivityRuntime {
    CettaDurableStore *store;
    CettaActivityConfig config;
    CettaWorkerEndpoint *endpoint;
    CettaTimerCursor timers;
    Candidate *candidates;
    CettaReadyQueue queue;
    CettaDurableStatus failed;
    int64_t next_deadline_ms;
    bool output_pending;
    CettaActivityStats stats;
};
static bool component(const char *s) {
    size_t n=s?strnlen(s,65):0;
    if (!n || n>64) return false;
    for (size_t i=0;i<n;++i)
        if (!((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') ||
            (s[i]>='0' && s[i]<='9') || s[i]=='_' || s[i]=='-' || s[i]=='.')) return false;
    return true;
}
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static const char *text(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool integer(const Atom *a, int64_t *out) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *out=a->ground.ival; return true;
}
static bool version(const Atom *a) { int64_t n; return integer(a,&n) && n==1; }
static bool config_valid(const CettaActivityConfig *c) {
    if (!c || !c->program || !c->program->context || !c->program->context->session.profile)
        return false;
    const CettaEvalSession *session=&c->program->context->session;
    bool admitted=c->planning.quantum?
        session->language_id==CETTA_LANGUAGE_PRIME && session->profile->id==CETTA_PROFILE_PRIME_DEFAULT &&
            c->planning.quantum!=UINT64_MAX && c->planning.work_limit!=UINT64_MAX &&
            c->planning.preparation_inspections && c->planning.preparation_bytes && c->planning.results:
        c->fuel>0 && session->language_id==CETTA_LANGUAGE_HE && session->profile->id==CETTA_PROFILE_HE_EXTENDED;
    return admitted && c->program->version && !strcmp(c->program->version,"activity/1") &&
        c->program->space && c->program->context && component(c->worker) &&
        c->ready_capacity<=4096;
}
static CettaDurableStatus read(CettaDurableStore *store, const char *space,
        const char *key, Arena *arena, CettaDurableObservation **o, Atom **value) {
    CettaDurableScope scope={DURABLE_KEY,space,key}; *value=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,&scope,1,o);
    if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *v=cetta_durable_observation_view(*o,0);
    return v->count?cetta_durable_value_decode(arena,v->records[0].data,v->records[0].size,value):DURABLE_PRECONDITION;
}
static bool same(const CettaDurableRecord *a, const CettaDurableRecord *b) {
    return a->size==b->size && !memcmp(a->data,b->data,a->size);
}
CettaDurableStatus cetta_activity_record(CettaDurableStore *store,
        const char *id, const char *owner, int64_t gen, CettaActivityKind kind,
        const char *body, int64_t due, int64_t period) {
    if (!store || !component(id) || !component(owner) || gen<=0 || !body ||
        kind<ACTIVITY_WORK || kind>ACTIVITY_CANCEL || due<0 || period<0 ||
        (kind!=ACTIVITY_TIMER && (due || period))) return DURABLE_INVALID;
    Arena a; arena_init(&a);
    Atom *request[]={atom_symbol(&a,"worker:request"),atom_int(&a,1),atom_string(&a,"input"),atom_string(&a,body)};
    if (!cetta_worker_validate("input",atom_expr(&a,request,4),atom_symbol(&a,"reply"))) {
        arena_free(&a); return DURABLE_INVALID;
    }
    const char *kinds[]={"work","notice","timer","cancel"};
    Atom *fields[]={atom_symbol(&a,"activity:input"),atom_int(&a,1),atom_string(&a,id),
        atom_string(&a,owner),atom_int(&a,gen),atom_symbol(&a,kinds[kind]),atom_string(&a,body),
        atom_int(&a,due),atom_int(&a,period)};
    unsigned char *bytes=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_value_encode(atom_expr(&a,fields,9),&bytes,&size);
    char input[80]; snprintf(input,sizeof(input),"activity/%s",id);
    CettaDurableScope q[]={{DURABLE_KEY,"activity.inputs",id},{DURABLE_KEY,"host.inbox",input}};
    CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=cetta_durable_observe(store,q,2,&o);
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *old=cetta_durable_observation_view(o,0);
        const CettaDurableSnapshot *pending=cetta_durable_observation_view(o,1);
        if (old->count) s=old->records[0].size==size && !memcmp(old->records[0].data,bytes,size)?DURABLE_OK:DURABLE_CONFLICT;
        else if (pending->count) s=DURABLE_CORRUPT;
        else {
            CettaDurableOp ops[]={{DURABLE_INSERT,"activity.inputs",id,bytes,size},
                {DURABLE_INSERT,"host.inbox",input,bytes,size}}; int64_t revision;
            s=cetta_durable_commit_observed(store,o,ops,2,&revision);
        }
    }
    cetta_durable_observation_free(o); free(bytes); arena_free(&a); return s;
}
CettaDurableStatus cetta_activity_decide(CettaDurableStore *store,
        const CettaActivityConfig *c, const char *input_key, CettaHostDecision **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !config_valid(c) || !input_key || !*input_key || strnlen(input_key,256)>255) return DURABLE_INVALID;
    Arena a; arena_init(&a);
    CettaDurableObservation *input=NULL, *origin=NULL, *intent=NULL;
    CettaHostDecision *d=NULL; Atom *value=NULL, *orig=NULL, *effect=NULL, *call=NULL;
    CettaDurableStatus s=read(store,"host.inbox",input_key,&a,&input,&value);
    if (s!=DURABLE_OK) goto done;
    char actor[160], result_key[160], task_key[140], expected[180], timer_key[256];
    const char *owner=NULL, *id=NULL; int64_t gen=0;
    CettaHostSpaceGrant scopes[5]; size_t count=0;
    CettaHostChannelGrant channels[]={{"worker.request","1",cetta_worker_validate,(void *)c->worker},
        {"timer.after","1",cetta_timer_validate,NULL}};
    bool ledger_check=false, worker_ledger=false;
    const CettaDurableRecord *timer_intent=NULL;
    if (form(value,"activity:input",9) && version(value->expr.elems[1])) {
        id=text(value->expr.elems[2]); owner=text(value->expr.elems[3]);
        if (!component(id) || !component(owner) || !integer(value->expr.elems[4],&gen) || gen<=0) { s=DURABLE_CORRUPT; goto done; }
        snprintf(expected,sizeof(expected),"activity/%s",id);
        if (strcmp(input_key,expected)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(result_key,sizeof(result_key),"%s",id);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"activity.results",result_key},true};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"activity.inputs",id},false};
        ledger_check=true;
        bool cancel=atom_is_symbol(value->expr.elems[5],"cancel");
        Atom *args[]={atom_symbol(&a,cancel?"activity:cancel":"activity:receive"),atom_string(&a,c->worker)};
        call=atom_expr(&a,args,cancel?1:2);
    } else if (form(value,"host:worker-result",6) && version(value->expr.elems[1])) {
        const char *worker=text(value->expr.elems[2]), *task=text(value->expr.elems[3]);
        if (!worker || strcmp(worker,c->worker)) { s=DURABLE_PRECONDITION; goto done; }
        if (!component(task)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(task_key,sizeof(task_key),"%s/%s",worker,task);
        snprintf(expected,sizeof(expected),"worker/%s",task_key);
        if (strcmp(input_key,expected)) { s=DURABLE_CORRUPT; goto done; }
        s=read(store,"host.worker-origins",task_key,&a,&origin,&orig); if (s!=DURABLE_OK) goto done;
        if (!form(orig,"host:worker-origin",6) || !version(orig->expr.elems[1]) || !text(orig->expr.elems[3])) { s=DURABLE_CORRUPT; goto done; }
        const char *outkey=text(orig->expr.elems[3]);
        s=read(store,"host.outbox",outkey,&a,&intent,&effect); if (s!=DURABLE_OK) goto done;
        if (!form(effect,"host:intent",6) || !form(effect->expr.elems[5],"activity:return",5)) { s=DURABLE_PRECONDITION; goto done; }
        Atom **reply=effect->expr.elems[5]->expr.elems;
        id=text(reply[2]); owner=text(reply[3]);
        if (!version(reply[1]) || !component(id) || !component(owner) || !integer(reply[4],&gen) || gen<=0) { s=DURABLE_CORRUPT; goto done; }
        snprintf(result_key,sizeof(result_key),"%s",id);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"activity.results",result_key},true};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-tasks",task_key},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-origins",task_key},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-results",task_key},false};
        worker_ledger=true;
        Atom *args[]={atom_symbol(&a,"activity:reply"),atom_string(&a,c->worker),
            atom_string(&a,result_key),atom_string(&a,task_key),atom_string(&a,outkey)};
        call=atom_expr(&a,args,5);
    } else if (form(value,"host:timer-event",10) && version(value->expr.elems[1])) {
        const char *effect_id=text(value->expr.elems[2]); int64_t sequence;
        Atom *reply=value->expr.elems[9];
        if (!effect_id || !integer(value->expr.elems[3],&sequence) || sequence<0 || !form(reply,"activity:tick",5)) { s=DURABLE_PRECONDITION; goto done; }
        id=text(reply->expr.elems[2]); owner=text(reply->expr.elems[3]);
        if (!version(reply->expr.elems[1]) || !component(id) || !component(owner) ||
            !integer(reply->expr.elems[4],&gen) || gen<=0) { s=DURABLE_CORRUPT; goto done; }
        snprintf(expected,sizeof(expected),"timer/%s/%" PRId64,effect_id,sequence);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        CettaDurableScope q={DURABLE_SPACE,"host.outbox",NULL};
        s=cetta_durable_observe(store,&q,1,&intent); if (s!=DURABLE_OK) goto done;
        const CettaDurableSnapshot *intents=cetta_durable_observation_view(intent,0);
        for (size_t i=0;i<intents->count;++i) {
            char identity[96]; const CettaDurableRecord *record=&intents->records[i];
            snprintf(identity,sizeof(identity),"%s/%" PRId64 "/%" PRId64,intents->epoch,record->revision,record->position);
            if (!strcmp(effect_id,identity)) { timer_intent=record; break; }
        }
        if (!timer_intent) { s=DURABLE_CORRUPT; goto done; }
        snprintf(timer_key,sizeof(timer_key),"%s",timer_intent->key);
        snprintf(result_key,sizeof(result_key),"%s/%" PRId64,id,sequence);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"activity.results",result_key},true};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",timer_key},false};
        Atom *args[]={atom_symbol(&a,"activity:timer-reply"),atom_string(&a,result_key),
            atom_string(&a,timer_key),atom_string(&a,effect_id)};
        call=atom_expr(&a,args,4);
    } else { s=DURABLE_PRECONDITION; goto done; }
    snprintf(actor,sizeof(actor),"activity/%s/%" PRId64,owner,gen);
    CettaHostDecisionSpec spec={input_key,actor,"activity/1",scopes,count,channels,2};
    s=cetta_host_begin(store,&spec,&d);
    if (s==DURABLE_OK && ledger_check) {
        const CettaDurableSnapshot *ledger=cetta_host_view(d,3), *pending=cetta_host_view(d,0);
        if (ledger->count!=1 || !same(&ledger->records[0],&pending->records[0])) s=DURABLE_CORRUPT;
    }
    if (s==DURABLE_OK && worker_ledger) {
        const CettaDurableSnapshot *ledger=cetta_host_view(d,6), *pending=cetta_host_view(d,0);
        if (ledger->count!=1 || !same(&ledger->records[0],&pending->records[0])) s=DURABLE_CORRUPT;
    }
    if (s==DURABLE_OK && timer_intent) {
        const CettaDurableSnapshot *fresh=cetta_host_view(d,3);
        if (fresh->count!=1 || fresh->records[0].revision!=timer_intent->revision ||
            fresh->records[0].position!=timer_intent->position || !same(&fresh->records[0],timer_intent)) s=DURABLE_CONFLICT;
    }
    if (s==DURABLE_OK) s=c->planning.quantum?
        cetta_host_plan_open(d,c->program,call,&c->planning):
        cetta_host_evaluate(d,c->program,call,c->fuel);
    if (s==DURABLE_OK) { *out=d; d=NULL; }
done:
    cetta_host_decision_free(d); cetta_durable_observation_free(input);
    cetta_durable_observation_free(origin); cetta_durable_observation_free(intent); arena_free(&a); return s;
}
static void remove_candidate(CettaActivityRuntime *r, Candidate *entry) {
    cetta_host_decision_free(entry->decision);
    cetta_durable_watch_free(entry->watch); cetta_ready_remove(&r->queue,&entry->link);
    *entry=(Candidate){0};
}
static CettaDurableStatus refresh(CettaActivityRuntime *r) {
    CettaDurableSnapshot inbox={0};
    CettaDurableStatus s=cetta_durable_snapshot(r->store,"host.inbox",&inbox);
    if (s!=DURABLE_OK) return s;
    for (CettaReadyLink *p=r->queue.first;p;p=p->next) {
        Candidate *entry=(Candidate *)p; entry->seen=false;
        for (size_t i=0;i<inbox.count;++i) if (!strcmp(entry->key,inbox.records[i].key)) {
            const CettaDurableRecord *record=&inbox.records[i];entry->seen=true;
            if(entry->revision!=record->revision || entry->position!=record->position) {
                cetta_host_decision_free(entry->decision);entry->decision=NULL;
                cetta_durable_watch_free(entry->watch);entry->watch=NULL;
                entry->revision=record->revision;entry->position=record->position;
            }
            break;
        }
    }
    for (CettaReadyLink *p=r->queue.first,*next;p;p=next) {
        next=p->next; if (!((Candidate *)p)->seen) remove_candidate(r,(Candidate *)p);
    }
    for (size_t i=0;i<inbox.count && r->queue.count<r->queue.capacity;++i) {
        char worker_prefix[80]; snprintf(worker_prefix,sizeof(worker_prefix),"worker/%s/",r->config.worker);
        const char *key=inbox.records[i].key;
        if (strncmp(key,"activity/",9) && strncmp(key,worker_prefix,strlen(worker_prefix)) &&
            strncmp(key,"timer/",6)) continue;
        bool present=false;
        for (CettaReadyLink *p=r->queue.first;p;p=p->next) if (!strcmp(((Candidate *)p)->key,inbox.records[i].key)) { present=true; break; }
        if (present) continue;
        for (size_t j=0;j<r->queue.capacity;++j) if (!r->candidates[j].link.owner) {
            Candidate *entry=&r->candidates[j];
            snprintf(entry->key,sizeof(entry->key),"%s",inbox.records[i].key);
            entry->revision=inbox.records[i].revision;entry->position=inbox.records[i].position;
            if (cetta_ready_admit(&r->queue,&entry->link)!=READY_ADMITTED) s=DURABLE_CORRUPT;
            break;
        }
    }
    cetta_durable_snapshot_free(&inbox); return s;
}
static CettaDurableStatus visit(CettaActivityRuntime *r, Candidate *entry) {
    if (entry->watch) {
        CettaDurableStatus s=cetta_durable_watch_current(r->store,entry->watch);
        if (s==DURABLE_OK) return DURABLE_OK;
        if (s!=DURABLE_CONFLICT) return s;
        cetta_durable_watch_free(entry->watch); entry->watch=NULL;
        cetta_host_decision_free(entry->decision);entry->decision=NULL;
    }
    CettaHostDecision *d=entry->decision;
    CettaDurableStatus s=d?DURABLE_OK:cetta_activity_decide(r->store,&r->config,entry->key,&d);
    entry->decision=d;
    if (s==DURABLE_OK) {
        if (r->config.planning.quantum) {
            CettaOwnedTask *task=cetta_host_plan_task(d);
            if (!task) return DURABLE_CORRUPT;
            uint64_t before=cetta_host_outcome(d)->steps_spent;
            cetta_owned_task_poll(task,true);
            const EvalOutcome *pending=cetta_host_outcome(d);
            ++r->stats.plan_polls;
            r->stats.plan_work+=pending->steps_spent-before;
            if (!task->execution.terminal) {
                if (!pending || pending->completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE)
                    return DURABLE_INVALID;
                if (pending->completion==CETTA_EVAL_INCOMPLETE_CAPACITY && task->execution.has_pending)
                    return DURABLE_LIMIT;
                if (!pending->budget_remaining)
                    return cetta_host_watch(d,65536,&entry->watch);
                return DURABLE_OK;
            }
        }
        /* Terminal publication below may retire this candidate. The local
         * owner is released once, after acceptance or refusal. */
        entry->decision=NULL;
        const EvalOutcome *o=cetta_host_outcome(d);
        if (!o || o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials) s=DURABLE_INVALID;
        else if (!o->results.len || (o->results.len==1 && atom_is_symbol(o->results.items[0],"Empty"))) {
            ++r->stats.blocked; s=cetta_host_watch(d,65536,&entry->watch);
        } else if (o->results.len!=1) s=DURABLE_INVALID;
        else {
            CettaHostCommit commit; s=cetta_host_accept(d,0,&commit);
            if (s==DURABLE_OK) {
                ++r->stats.accepted; remove_candidate(r,entry);
                if (commit.effects) r->next_deadline_ms=0; /* recheck newly accepted timers before sleeping */
            }
        }
    } else if (s==DURABLE_PRECONDITION) {
        CettaDurableScope scope={DURABLE_KEY,"host.inbox",entry->key};
        CettaDurableObservation *o=NULL;
        s=cetta_durable_observe(r->store,&scope,1,&o);
        if (s==DURABLE_OK) s=cetta_durable_watch_new(o,65536,&entry->watch);
        cetta_durable_observation_free(o);
    }
    cetta_host_decision_free(d);
    return s==DURABLE_CONFLICT || s==DURABLE_BUSY?DURABLE_OK:s;
}
static CettaDurableStatus publish_one(CettaActivityRuntime *r) {
    CettaDurableScope scopes[]={{DURABLE_SPACE,"host.outbox",NULL},
        {DURABLE_SPACE,"activity.publications",NULL}};
    CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(r->store,scopes,2,&o);
    if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *intents=cetta_durable_observation_view(o,0), *published=cetta_durable_observation_view(o,1);
    Arena a; arena_init(&a);
    r->output_pending=false;
    for (size_t i=0;i<intents->count;++i) {
        const CettaDurableRecord *record=&intents->records[i]; bool done=false;
        for (size_t j=0;j<published->count;++j) if (!strcmp(record->key,published->records[j].key)) { done=true; break; }
        if (done) continue;
        Atom *intent=NULL; s=cetta_durable_value_decode(&a,record->data,record->size,&intent); if (s!=DURABLE_OK) break;
        if (!form(intent,"host:intent",6) || !form(intent->expr.elems[5],"activity:return",5)) continue;
        char id[65]; s=cetta_worker_register(r->store,record->key,r->config.worker,id);
        if (s==DURABLE_LIMIT) { s=DURABLE_OK; break; }
        if (s!=DURABLE_OK) break;
        r->output_pending=true;
        /* Publication is independently idempotent. If this acknowledgment
         * commit is interrupted, repeating register cannot repeat the task. */
        CettaDurableScope q={DURABLE_KEY,"activity.publications",record->key};
        CettaDurableObservation *seen=NULL; s=cetta_durable_observe(r->store,&q,1,&seen);
        if (s==DURABLE_OK && !cetta_durable_observation_view(seen,0)->count) {
            static const unsigned char marker[]={'C','D','V','1','Y'}; int64_t revision;
            CettaDurableOp op={DURABLE_INSERT,"activity.publications",record->key,marker,sizeof(marker)};
            s=cetta_durable_commit_observed(r->store,seen,&op,1,&revision);
        }
        cetta_durable_observation_free(seen); if (s==DURABLE_OK) ++r->stats.published; break;
    }
    arena_free(&a); cetta_durable_observation_free(o); return s;
}
static void timer_fault(void *context, const char *key, CettaDurableStatus s) {
    (void)key; CettaActivityRuntime *r=context; r->failed=s;
}
static CettaDurableStatus retire_one_timer(CettaActivityRuntime *r, int64_t now) {
    /* Cancellation first revokes the authored continuation atomically. This
     * idempotent native cleanup can follow after any crash: a cancelled actor
     * generation cannot regain authority, and timer_cancel records a terminal
     * outcome and its final observation together. No timer thread feeds it. */
    CettaDurableScope scopes[]={{DURABLE_SPACE,"host.outbox",NULL},
        {DURABLE_SPACE,"host.outcomes",NULL}};
    CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(r->store,scopes,2,&o);
    if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *intents=cetta_durable_observation_view(o,0),
        *outcomes=cetta_durable_observation_view(o,1);
    Arena a; arena_init(&a);
    for (size_t i=0;i<intents->count;++i) {
        const CettaDurableRecord *record=&intents->records[i]; bool terminal=false;
        for (size_t j=0;j<outcomes->count;++j)
            if (!strcmp(record->key,outcomes->records[j].key)) { terminal=true; break; }
        if (terminal) continue;
        Atom *intent=NULL;
        s=cetta_durable_value_decode(&a,record->data,record->size,&intent);
        if (s!=DURABLE_OK) break;
        if (!form(intent,"host:intent",6) || !form(intent->expr.elems[5],"activity:tick",5)) continue;
        Atom **reply=intent->expr.elems[5]->expr.elems;
        const char *id=text(reply[2]), *owner=text(reply[3]); int64_t gen;
        if (!version(reply[1]) || !component(id) || !component(owner) ||
            !integer(reply[4],&gen) || gen<=0) { s=DURABLE_CORRUPT; break; }
        char actor[160]; snprintf(actor,sizeof(actor),"activity/%s/%" PRId64,owner,gen);
        CettaDurableObservation *state_view=NULL; Atom *actor_value=NULL;
        s=read(r->store,"host.actors",actor,&a,&state_view,&actor_value);
        cetta_durable_observation_free(state_view);
        if (s==DURABLE_PRECONDITION) { s=DURABLE_OK; continue; }
        if (s!=DURABLE_OK) break;
        if (!form(actor_value,"host:actor",4) || !version(actor_value->expr.elems[1]) ||
            !text(actor_value->expr.elems[2]) || strcmp(text(actor_value->expr.elems[2]),"activity/1")) {
            s=DURABLE_CORRUPT; break;
        }
        Atom *state=actor_value->expr.elems[3]; int64_t saved;
        if (!form(state,"activity:cancelled",5) || !version(state->expr.elems[1])) continue;
        if (!text(state->expr.elems[2]) || !text(state->expr.elems[3]) ||
            strcmp(text(state->expr.elems[2]),id) || strcmp(text(state->expr.elems[3]),owner) ||
            !integer(state->expr.elems[4],&saved) || saved!=gen) { s=DURABLE_CORRUPT; break; }
        s=cetta_timer_cancel(r->store,record->key,now);
        if (s==DURABLE_OK) ++r->stats.timer_events;
        break;
    }
    arena_free(&a); cetta_durable_observation_free(o); return s;
}
CettaDurableStatus cetta_activity_new(CettaDurableStore *store,
        const CettaActivityConfig *c, CettaActivityRuntime **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !config_valid(c) || !c->ready_capacity || c->ready_capacity>4096) return DURABLE_INVALID;
    CettaActivityRuntime *r=calloc(1,sizeof(*r)); if (!r) return DURABLE_NOMEM;
    r->store=store; r->config=*c; r->queue.capacity=c->ready_capacity;
    r->next_deadline_ms=-1;
    r->candidates=calloc(c->ready_capacity,sizeof(*r->candidates));
    if (!r->candidates) { free(r); return DURABLE_NOMEM; }
    CettaDurableStatus s=cetta_durable_attach_runtime(store,r);
    if (s!=DURABLE_OK) { free(r->candidates); free(r); return s; }
    s=cetta_worker_endpoint_new(store,c->listener,c->worker_uid,c->worker,&r->endpoint);
    if (s!=DURABLE_OK) { cetta_durable_detach_runtime(store,r); free(r->candidates); free(r); return s; }
    *out=r; return DURABLE_OK;
}
static uint64_t microseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
    return (uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_nsec/1000u;
}
CettaDurableStatus cetta_activity_step(CettaActivityRuntime *r, unsigned visits) {
    if (!r || !visits || visits>64) return DURABLE_INVALID;
    if (r->failed!=DURABLE_OK) return r->failed;
    uint64_t started=microseconds(); ++r->stats.steps;
    size_t requests=0;
    CettaDurableStatus s=cetta_worker_endpoint_step(r->endpoint,0,&requests);
    r->stats.worker_requests+=requests;
    CettaClockSample now; CettaTimerTick tick;
    if (s==DURABLE_OK && !cetta_clock_sample(&now)) s=DURABLE_IO;
    if (s==DURABLE_OK) s=retire_one_timer(r,now.utc_ms);
    if (s==DURABLE_OK) s=cetta_timer_tick(r->store,&r->timers,now.utc_ms,1,timer_fault,r,&tick);
    if (s==DURABLE_OK && r->failed!=DURABLE_OK) s=r->failed;
    if (s==DURABLE_OK) {
        r->stats.timer_events+=tick.emitted; r->next_deadline_ms=tick.more_due?0:tick.next_deadline_ms;
        s=publish_one(r);
    }
    if (s==DURABLE_OK) s=refresh(r);
    for (unsigned i=0;s==DURABLE_OK && i<visits && r->queue.count;++i)
        s=visit(r,(Candidate *)cetta_ready_rotate(&r->queue));
    uint64_t elapsed=microseconds()-started;
    if (elapsed>r->stats.max_step_us) r->stats.max_step_us=elapsed;
    if (s!=DURABLE_OK) r->failed=s;
    return s;
}
CettaDurableStatus cetta_activity_wait(CettaActivityRuntime *r, int fd, unsigned milliseconds) {
    return cetta_activity_wait_io(r,fd,-1,milliseconds);
}
CettaDurableStatus cetta_activity_wait_io(CettaActivityRuntime *r, int fd, int output_fd, unsigned milliseconds) {
    if (!r || fd < -1 || output_fd < -1 || milliseconds>1000) return DURABLE_INVALID;
    if (r->failed!=DURABLE_OK) return r->failed;
    if (r->output_pending) milliseconds=0;
    for (CettaReadyLink *p=r->queue.first;p;p=p->next)
        if (!((Candidate *)p)->watch) { milliseconds=0; break; }
    CettaClockSample sample;
    if (!cetta_clock_sample(&sample)) return DURABLE_IO;
    if (r->next_deadline_ms>=0) {
        uint32_t timer_wait=cetta_timer_wait_ms(r->next_deadline_ms,&sample,sample.monotonic_ms);
        if (timer_wait<milliseconds) milliseconds=timer_wait;
    }
    return cetta_worker_endpoint_wait_io(r->endpoint,fd,output_fd,milliseconds);
}
void cetta_activity_stats(const CettaActivityRuntime *r, CettaActivityStats *out) {
    if (!out) return;
    *out=r?r->stats:(CettaActivityStats){0};
    if (r) {
        out->ready=r->queue.count;
        for (CettaReadyLink *p=r->queue.first;p;p=p->next) out->watch_bytes+=cetta_durable_watch_bytes(((Candidate *)p)->watch);
    }
}
void cetta_activity_free(CettaActivityRuntime *r) {
    if (!r) return;
    cetta_worker_endpoint_free(r->endpoint);
    for (size_t i=0;i<r->queue.capacity;++i) {
        cetta_host_decision_free(r->candidates[i].decision);
        cetta_durable_watch_free(r->candidates[i].watch);
    }
    cetta_ready_clear(&r->queue); cetta_durable_detach_runtime(r->store,r);
    free(r->candidates); free(r);
}
