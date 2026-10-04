#define _POSIX_C_SOURCE 200809L
#include "durable_timer.h"
#include "durable_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { FIRE_ONCE, SKIP_MISSED, CATCH_UP_ALL };
typedef struct { int64_t due, period, grace; int policy; const Atom *value; } Timer;
static bool form(const Atom *a, const char *head, size_t count) {
    return a && a->kind==ATOM_EXPR && a->expr.len==count && atom_is_symbol(a->expr.elems[0],head);
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool number(const Atom *a, int64_t *n) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *n=a->ground.ival; return true;
}
static bool version(const Atom *a) { int64_t n; return number(a,&n) && n==1; }
static bool timer(const Atom *p, Timer *t) {
    if (!form(p,"timer:at",7) || !version(p->expr.elems[1]) ||
        !number(p->expr.elems[2],&t->due) || t->due<0 ||
        !number(p->expr.elems[3],&t->period) || t->period<0 ||
        !number(p->expr.elems[5],&t->grace) || t->grace<0) return false;
    Atom *policy=p->expr.elems[4];
    if (atom_is_symbol(policy,"FireOnce")) t->policy=FIRE_ONCE;
    else if (atom_is_symbol(policy,"SkipMissed")) t->policy=SKIP_MISSED;
    else if (atom_is_symbol(policy,"CatchUpAll")) t->policy=CATCH_UP_ALL;
    else return false;
    t->value=p->expr.elems[6]; return true;
}
static bool bounded(const Atom *a) {
    unsigned char *bytes=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_value_encode(a,&bytes,&size); free(bytes);
    return s==DURABLE_OK && size<=65536;
}
bool cetta_timer_validate(void *unused, const Atom *payload, const Atom *reply) {
    (void)unused; Timer t;
    return timer(payload,&t) && bounded(payload) && bounded(reply);
}
static CettaDurableStatus encoded(CettaDurableOp *op, const Atom *a) {
    unsigned char *bytes=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_value_encode(a,&bytes,&size);
    if (s==DURABLE_OK) { op->data=bytes; op->size=size; }
    return s;
}
static const CettaDurableSnapshot *view(CettaDurableObservation *o, size_t i) {
    return cetta_durable_observation_view(o,i);
}
static bool same(const CettaDurableRecord *a, const CettaDurableRecord *b) {
    return a->revision==b->revision && a->position==b->position && a->size==b->size && !memcmp(a->data,b->data,a->size);
}
/* One timer transition. Unknown commit acknowledgment is never retried here;
 * a later observation/reopen sees either the old cursor or the whole advance. */
static CettaDurableStatus advance(CettaDurableStore *store, const char *epoch,
        const CettaDurableRecord *accepted, const Atom *intent, Timer t, int64_t now,
        size_t budget, bool cancel, size_t *emitted, int64_t *deadline, bool *more) {
    char effect[96], prefix[104], keys[8][128];
    snprintf(effect,sizeof(effect),"%s/%lld/%lld",epoch,(long long)accepted->revision,(long long)accepted->position);
    snprintf(prefix,sizeof(prefix),"timer/%s/",effect);
    CettaDurableScope q[]={{DURABLE_KEY,"host.outbox",accepted->key},
        {DURABLE_KEY,"host.timer-state",accepted->key},{DURABLE_KEY,"host.outcomes",accepted->key},
        {DURABLE_PREFIX,"host.inbox",prefix}};
    CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,q,4,&o);
    Arena a; arena_init(&a); CettaDurableOp ops[10]={0}; size_t nops=0, n=0;
    if (s!=DURABLE_OK) goto done;
    if (strcmp(view(o,0)->epoch,epoch) || view(o,0)->count!=1 || !same(&view(o,0)->records[0],accepted)) { s=DURABLE_CONFLICT; goto done; }
    if (view(o,2)->count) goto done;
    int64_t initial_due=t.due;
    int64_t seq=0;
    if (view(o,1)->count) {
        Atom *state=NULL; int64_t last;
        const CettaDurableRecord *r=&view(o,1)->records[0];
        s=cetta_durable_value_decode(&a,r->data,r->size,&state); if (s!=DURABLE_OK) goto done;
        if (!form(state,"host:timer-state",6) || !version(state->expr.elems[1]) ||
            !string(state->expr.elems[2]) || strcmp(string(state->expr.elems[2]),effect) ||
            !number(state->expr.elems[3],&t.due) || t.due<initial_due || !t.period ||
            !number(state->expr.elems[4],&seq) || seq<=0 ||
            !number(state->expr.elems[5],&last) || last<0) { s=DURABLE_CORRUPT; goto done; }
    }
    if (!cancel && now<t.due) { *deadline=t.due; goto done; }
    uint64_t due=(uint64_t)t.due, step=(uint64_t)t.period, represented=1;
    const char *kind=cancel?"cancelled":"fired";
    n=1;
    if (cancel) represented=0;
    else if (t.policy==SKIP_MISSED && now-t.due>t.grace) {
        kind="skipped";
        represented=step?((uint64_t)(now-t.due-t.grace-1)/step+1):1;
    } else if (t.policy==FIRE_ONCE) represented=step?((uint64_t)(now-t.due)/step+1):1;
    else if (t.policy==CATCH_UP_ALL && step) {
        uint64_t available=(uint64_t)(now-t.due)/step+1;
        n=available<budget?(size_t)available:budget;
        if (n>8) n=8;
    }
    if (represented>INT64_MAX || seq>INT64_MAX-(int64_t)n) { s=DURABLE_LIMIT; goto done; }
    uint64_t next=due;
    for (size_t i=0;i<n;++i) {
        snprintf(keys[i],sizeof(keys[i]),"%s%lld",prefix,(long long)(seq+(int64_t)i));
        ops[nops]=(CettaDurableOp){DURABLE_INSERT,"host.inbox",keys[i],NULL,0};
        Atom *event[]={atom_symbol(&a,"host:timer-event"),atom_int(&a,1),atom_string(&a,effect),
            atom_int(&a,seq+(int64_t)i),atom_symbol(&a,kind),atom_int(&a,(int64_t)next),
            atom_int(&a,now),atom_int(&a,(int64_t)represented),(Atom *)t.value,intent->expr.elems[5]};
        s=encoded(&ops[nops++],atom_expr(&a,event,10)); if (s!=DURABLE_OK) goto done;
        if (step && !cancel) next+=step*represented;
    }
    bool terminal=cancel || !step || next>INT64_MAX;
    if (terminal) {
        if (view(o,1)->count) ops[nops++]=(CettaDurableOp){DURABLE_REMOVE,"host.timer-state",accepted->key,NULL,0};
        ops[nops]=(CettaDurableOp){DURABLE_INSERT,"host.outcomes",accepted->key,NULL,0};
        Atom *outcome[]={atom_symbol(&a,"host:timer-outcome"),atom_int(&a,1),atom_string(&a,effect),
            atom_symbol(&a,(!cancel && step)?"exhausted":kind),atom_int(&a,seq+(int64_t)n),atom_int(&a,now)};
        s=encoded(&ops[nops++],atom_expr(&a,outcome,6));
    } else {
        ops[nops]=(CettaDurableOp){view(o,1)->count?DURABLE_REPLACE:DURABLE_INSERT,"host.timer-state",accepted->key,NULL,0};
        Atom *state[]={atom_symbol(&a,"host:timer-state"),atom_int(&a,1),atom_string(&a,effect),
            atom_int(&a,(int64_t)next),atom_int(&a,seq+(int64_t)n),atom_int(&a,now)};
        s=encoded(&ops[nops++],atom_expr(&a,state,6));
    }
    int64_t revision;
    if (s==DURABLE_OK) s=cetta_durable_commit_observed(store,o,ops,nops,&revision);
    if (s==DURABLE_OK) {
        *emitted=n;
        if (!terminal) { *deadline=(int64_t)next; *more=next<=(uint64_t)now; }
    }
done:
    for (size_t i=0;i<nops;++i) free((void *)ops[i].data);
    arena_free(&a); cetta_durable_observation_free(o); return s;
}
static CettaDurableStatus decode(Arena *a, const CettaDurableRecord *r, Atom **p, Timer *t, bool *is_timer) {
    *is_timer=false;
    CettaDurableStatus s=cetta_durable_value_decode(a,r->data,r->size,p); if (s!=DURABLE_OK) return s;
    if (!form(*p,"host:intent",6)) return DURABLE_CORRUPT;
    const char *channel=string((*p)->expr.elems[2]);
    if (!channel) return DURABLE_CORRUPT;
    if (strcmp(channel,"timer.after")) return DURABLE_OK;
    *is_timer=true;
    if (!version((*p)->expr.elems[1]) || !string((*p)->expr.elems[3]) || strcmp(string((*p)->expr.elems[3]),"1")) return DURABLE_VERSION;
    if (!timer((*p)->expr.elems[4],t) || !cetta_timer_validate(NULL,(*p)->expr.elems[4],(*p)->expr.elems[5])) return DURABLE_INVALID;
    return DURABLE_OK;
}
static int key_order(const void *a, const void *b) {
    return strcmp(((const CettaDurableRecord *)a)->key,((const CettaDurableRecord *)b)->key);
}
CettaDurableStatus cetta_timer_tick(CettaDurableStore *store, CettaTimerCursor *cursor,
        int64_t now, size_t budget, CettaTimerFault fault, void *context, CettaTimerTick *out) {
    if (!out) return DURABLE_INVALID;
    *out=(CettaTimerTick){.next_deadline_ms=-1};
    if (!store || !cursor || !memchr(cursor->after_key,0,sizeof(cursor->after_key)) || now<0 ||
        !budget || budget>64 || !fault) return DURABLE_INVALID;
    CettaDurableSnapshot snap;
    CettaDurableStatus s=cetta_durable_snapshot(store,"host.outbox",&snap); if (s!=DURABLE_OK) return s;
    if (snap.count>1) qsort(snap.records,snap.count,sizeof(*snap.records),key_order);
    size_t start=0;
    while (start<snap.count && strcmp(snap.records[start].key,cursor->after_key)<=0) ++start;
    if (start==snap.count) start=0;
    for (size_t i=0;i<snap.count;++i) {
        if (out->emitted==budget) { out->more_due=true; break; }
        const CettaDurableRecord *r=&snap.records[(start+i)%snap.count];
        Arena a; arena_init(&a); Atom *p=NULL; Timer t; bool is_timer;
        s=decode(&a,r,&p,&t,&is_timer);
        size_t emitted=0; int64_t deadline=-1; bool more=false;
        if (s==DURABLE_OK && is_timer) s=advance(store,snap.epoch,r,p,t,now,budget-out->emitted,false,&emitted,&deadline,&more);
        arena_free(&a); strcpy(cursor->after_key,r->key);
        if (s==DURABLE_OK) {
            out->emitted+=emitted; out->more_due|=more;
            if (deadline>=0 && (out->next_deadline_ms<0 || deadline<out->next_deadline_ms)) out->next_deadline_ms=deadline;
        } else if (s==DURABLE_INVALID || s==DURABLE_VERSION || s==DURABLE_CORRUPT || s==DURABLE_CONFLICT) {
            ++out->faults; fault(context,r->key,s); s=DURABLE_OK;
        } else { fault(context,r->key,s); break; }
    }
    cetta_durable_snapshot_free(&snap); return s;
}
CettaDurableStatus cetta_timer_cancel(CettaDurableStore *store, const char *key, int64_t now) {
    if (!store || !key || !*key || strnlen(key,256)>255 || now<0) return DURABLE_INVALID;
    CettaDurableScope q={DURABLE_KEY,"host.outbox",key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,&q,1,&o); if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *v=view(o,0);
    Arena a; arena_init(&a); Atom *p=NULL; Timer t; bool is_timer=false;
    if (!v->count) s=DURABLE_PRECONDITION;
    else s=decode(&a,&v->records[0],&p,&t,&is_timer);
    if (s==DURABLE_OK && !is_timer) s=DURABLE_INVALID;
    if (s==DURABLE_OK) {
        size_t n=0; int64_t due=-1; bool more=false;
        s=advance(store,v->epoch,&v->records[0],p,t,now,1,true,&n,&due,&more);
    }
    arena_free(&a); cetta_durable_observation_free(o); return s;
}
bool cetta_clock_sample(CettaClockSample *out) {
    if (!out) return false;
    struct timespec wall, mono;
    if (clock_gettime(CLOCK_REALTIME,&wall) || clock_gettime(CLOCK_MONOTONIC,&mono) ||
        wall.tv_sec<0 || (uint64_t)wall.tv_sec>(uint64_t)(INT64_MAX-999)/1000 || mono.tv_sec<0 ||
        (uint64_t)mono.tv_sec>(UINT64_MAX-999)/1000) return false;
    *out=(CettaClockSample){(int64_t)wall.tv_sec*1000+wall.tv_nsec/1000000,
        (uint64_t)mono.tv_sec*1000+(uint64_t)mono.tv_nsec/1000000}; return true;
}
uint32_t cetta_timer_wait_ms(int64_t deadline, const CettaClockSample *s, uint64_t mono) {
    if (!s || s->utc_ms<0 || deadline<0) return 1000;
    if (deadline<=s->utc_ms) return 0;
    uint64_t remaining=(uint64_t)(deadline-s->utc_ms), elapsed=mono>=s->monotonic_ms?mono-s->monotonic_ms:0;
    if (elapsed>=remaining) return 0;
    remaining-=elapsed; return remaining>1000?1000:(uint32_t)remaining;
}
