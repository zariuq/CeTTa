#define _POSIX_C_SOURCE 200809L
#include "telegram_intake.h"
#include "durable_eval.h"
#include "durable_value.h"
#include <stdlib.h>
#include <string.h>

struct CettaTelegramIntake {
    CettaDurableStore *store;
    CettaInboxWindow *window;
    char *version;
    CettaHostEvaluation *evaluation;
    bool spent, recovery;
};
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool integer(const Atom *a, int64_t *value) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *value=a->ground.ival; return true;
}
static bool number_is(const Atom *a, int64_t value) {
    int64_t actual; return integer(a,&actual) && actual==value;
}
static bool config_valid(const Atom *a) {
    if (!form(a,"telegram:policy",6) || !number_is(a->expr.elems[1],1)) return false;
    const Atom *b=a->expr.elems[3];
    if (b->kind!=ATOM_GROUNDED || b->ground.gkind!=GV_BOOL) return false;
    const unsigned indexes[]={2,4,5};
    for (unsigned i=0;i<3;++i) {
        const Atom *list=a->expr.elems[indexes[i]];
        if (list->kind!=ATOM_EXPR || list->expr.len>256) return false;
        for (CettaExprIndex j=0;j<list->expr.len;++j) {
            int64_t id;
            if (!integer(list->expr.elems[j],&id) || !id || (i && id<0)) return false;
        }
    }
    return true;
}
void cetta_telegram_intake_free(CettaTelegramIntake *t) {
    if (!t) return;
    cetta_host_eval_free(t->evaluation); cetta_inbox_window_free(t->window);
    free(t->version); free(t);
}
CettaDurableStatus cetta_telegram_intake_begin(CettaDurableStore *store,
    const char *source, const char *version, CettaTelegramIntake **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!version || !*version || strnlen(version,256)>255) return DURABLE_INVALID;
    CettaTelegramIntake *t=calloc(1,sizeof(*t)); if (!t) return DURABLE_NOMEM;
    t->store=store; t->version=strdup(version);
    if (!t->version) { free(t); return DURABLE_NOMEM; }
    CettaDurableStatus s=cetta_inbox_recover_poll(store,source,&t->window);
    if (s!=DURABLE_OK) { cetta_telegram_intake_free(t); return s; }
    *out=t; return DURABLE_OK;
}
const EvalOutcome *cetta_telegram_intake_outcome(const CettaTelegramIntake *t) {
    return t && t->evaluation?&t->evaluation->outcome:NULL;
}
bool cetta_telegram_intake_held(const CettaTelegramIntake *t) { return t && cetta_inbox_poll_held(t->window); }
int64_t cetta_telegram_intake_revision(const CettaTelegramIntake *t) {
    const CettaDurableRecord *r=t?cetta_inbox_poll_response(t->window):NULL;
    return r?r->revision:-1;
}
static CettaDurableStatus evaluate(CettaTelegramIntake *t,
    const CettaHostProgram *program, const Atom *config, int64_t now, bool recovery, int fuel) {
    if (!t) return DURABLE_INVALID;
    if (t->spent) return DURABLE_CONFLICT;
    cetta_host_eval_free(t->evaluation); t->evaluation=NULL; t->recovery=false;
    if (!program || !program->version || !config_valid(config) || fuel<=0) return DURABLE_INVALID;
    if (strcmp(t->version,program->version)) return DURABLE_VERSION;
    CettaDurableStatus s;
    if (recovery) {
        s=cetta_inbox_poll_clock(t->window,now,&now);
        if (s!=DURABLE_OK) return s;
    }
    s=cetta_host_eval_create(program,&t->evaluation);
    if (s!=DURABLE_OK) return s;
    Arena *a=&t->evaluation->persistent; Atom *poll=NULL;
    const CettaDurableRecord *record=cetta_inbox_poll_response(t->window);
    s=cetta_durable_value_decode(a,record->data,record->size,&poll);
    if (s!=DURABLE_OK) goto fail;
    if (!form(poll,"host:poll",7) || !number_is(poll->expr.elems[1],1) ||
        !string(poll->expr.elems[2]) || strcmp(string(poll->expr.elems[2]),cetta_inbox_source(t->window)) ||
        !number_is(poll->expr.elems[3],cetta_inbox_offset(t->window)) || !string(poll->expr.elems[6])) {
        s=DURABLE_CORRUPT; goto fail;
    }
    /* A fixed host entry, never caller-supplied source/expression/response. */
    Atom *args[]={atom_symbol(a,recovery?"telegram:recover-poll":"telegram:poll"),poll,(Atom *)config,
        atom_int(a,cetta_inbox_poll_failures(t->window)),atom_int(a,now)};
    s=cetta_host_eval_run(t->evaluation,program,atom_expr(a,args,recovery?5:3),fuel);
    t->recovery=recovery;
    if (s==DURABLE_OK) return s;
fail:
    cetta_host_eval_free(t->evaluation); t->evaluation=NULL; return s;
}
CettaDurableStatus cetta_telegram_intake_evaluate(CettaTelegramIntake *t,
    const CettaHostProgram *program, const Atom *config, int fuel) {
    return evaluate(t,program,config,-1,false,fuel);
}
CettaDurableStatus cetta_telegram_intake_evaluate_recovery(CettaTelegramIntake *t,
    const CettaHostProgram *program, const Atom *config, int64_t now, int fuel) {
    return evaluate(t,program,config,now,true,fuel);
}
static bool complete(const EvalOutcome *e) {
    return e && e->completion==CETTA_EVAL_COMPLETE && !e->effect_denials &&
        e->budget_limited && e->budget_initial && e->results.len==1;
}
CettaDurableStatus cetta_telegram_intake_commit_recovery(CettaTelegramIntake *t, int64_t *revision) {
    if (!revision) return DURABLE_INVALID;
    *revision=-1;
    if (!t) return DURABLE_INVALID;
    if (t->spent) return DURABLE_CONFLICT;
    const EvalOutcome *e=cetta_telegram_intake_outcome(t);
    if (!t->recovery || !complete(e)) return DURABLE_PRECONDITION;
    const Atom *decision=e->results.items[0];
    if (!form(decision,"telegram:retry-at",4) && !form(decision,"telegram:hold",3)) return DURABLE_PRECONDITION;
    t->spent=true;
    return cetta_inbox_resolve_poll(t->window,t->version,decision,revision);
}
static const Atom *provider_value(const Atom *a) {
    if (form(a,"telegram:input",8) && number_is(a->expr.elems[1],1)) return a->expr.elems[7];
    return form(a,"JsonObjectV1",2)?a:NULL;
}
static bool disposition(const Atom *a, CettaInboxDisposition *out) {
    if (atom_is_symbol((Atom *)a,"routed")) *out=INBOX_ROUTED;
    else if (atom_is_symbol((Atom *)a,"unsupported")) *out=INBOX_UNSUPPORTED;
    else if (atom_is_symbol((Atom *)a,"unauthorized")) *out=INBOX_UNAUTHORIZED;
    else return false;
    return true;
}
CettaDurableStatus cetta_telegram_intake_commit(CettaTelegramIntake *t, CettaInboxCommit *out) {
    if (!out) return DURABLE_INVALID;
    *out=(CettaInboxCommit){.revision=-1,.next_offset=-1};
    if (!t) return DURABLE_INVALID;
    if (t->spent) return DURABLE_CONFLICT;
    const EvalOutcome *e=cetta_telegram_intake_outcome(t);
    if (t->recovery || !complete(e)) return DURABLE_PRECONDITION;
    const Atom *batch=e->results.items[0];
    if (!form(batch,"telegram:batch",3) || !number_is(batch->expr.elems[1],1) ||
        batch->expr.elems[2]->kind!=ATOM_EXPR) return DURABLE_PRECONDITION;
    const Atom *list=batch->expr.elems[2]; size_t count=list->expr.len;
    if (count>100) return DURABLE_LIMIT;
    CettaInboxItem items[100]; CettaDurableScope scopes[100];
    char keys[100][96], unused[168];
    for (size_t i=0;i<count;++i) {
        const Atom *item=list->expr.elems[i];
        if (!form(item,"tg:item",5) || !integer(item->expr.elems[1],&items[i].id) ||
            !(items[i].lane=string(item->expr.elems[2])) || !disposition(item->expr.elems[3],&items[i].disposition) ||
            !provider_value(item->expr.elems[4])) return DURABLE_INVALID;
        items[i].value=item->expr.elems[4];
        CettaDurableStatus s=cetta_inbox_keys(cetta_inbox_source(t->window),items[i].lane,items[i].id,keys[i],unused);
        if (s!=DURABLE_OK) return s;
        scopes[i]=(CettaDurableScope){DURABLE_KEY,"host.received",keys[i]};
    }
    CettaDurableObservation *seen=NULL; CettaDurableStatus s=DURABLE_OK;
    if (count) s=cetta_durable_observe(t->store,scopes,count,&seen);
    if (s!=DURABLE_OK) return s;
    for (size_t i=0;i<count;++i) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(seen,i);
        if (!v->count) continue;
        Atom *old=NULL;
        s=cetta_durable_value_decode(&t->evaluation->persistent,v->records[0].data,v->records[0].size,&old);
        if (s!=DURABLE_OK) goto done;
        const Atom *raw;
        if (!form(old,"host:received",7) || !number_is(old->expr.elems[1],1) ||
            !string(old->expr.elems[2]) || strcmp(string(old->expr.elems[2]),cetta_inbox_source(t->window)) ||
            !number_is(old->expr.elems[3],items[i].id) || !(raw=provider_value(old->expr.elems[6])) ||
            !atom_eq((Atom *)raw,(Atom *)provider_value(items[i].value)) || !(items[i].lane=string(old->expr.elems[4])) ||
            !disposition(old->expr.elems[5],&items[i].disposition)) { s=DURABLE_CORRUPT; goto done; }
        items[i].value=old->expr.elems[6];
    }
    t->spent=true;
    /* inbox_commit revalidates exact retained records/cursor/response. A
     * concurrent rewrite cannot be concealed by first-classification reuse. */
    s=cetta_inbox_commit(t->window,items,count,out);
done:
    cetta_durable_observation_free(seen); return s;
}
