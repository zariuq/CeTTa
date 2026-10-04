#define _POSIX_C_SOURCE 200809L
#include "durable_host.h"
#include "durable_eval.h"
#include "durable_value.h"
#include "library.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HOST_GRANTS 64
#define HOST_ACTIONS 128
#define HOST_VALUE_BYTES (1024u*1024u)

struct CettaHostDecision {
    CettaDurableStore *store;
    CettaDurableObservation *observation;
    char *input, *actor, *program;
    char nonce[33], prefix[34];
    CettaHostSpaceGrant *spaces;
    CettaHostChannelGrant *channels;
    size_t space_count, channel_count;
    bool spent;
    CettaHostEvaluation *evaluation;
};

void cetta_host_eval_free(CettaHostEvaluation *e) {
    if (!e) return;
    eval_outcome_free(&e->outcome);
    registry_free(&e->registry); space_free(&e->space);
    arena_free(&e->scratch); arena_free(&e->persistent);
    free(e);
}
static void clear_evaluation(CettaHostDecision *d) {
    cetta_host_eval_free(d->evaluation); d->evaluation=NULL;
}

static bool name(const char *s) { return s && *s && strnlen(s,256)<=255; }
static bool reserved(const char *s) { return !strncmp(s,"host.",5); }
static bool nonce(char out[33]) {
    unsigned char bytes[16]; size_t used=0;
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if (fd<0) return false;
    while (used<sizeof(bytes)) {
        ssize_t n=read(fd,bytes+used,sizeof(bytes)-used);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { close(fd); return false; }
        used+=(size_t)n;
    }
    close(fd);
    for (size_t i=0;i<sizeof(bytes);++i) {
        out[2*i]="0123456789abcdef"[bytes[i]>>4];
        out[2*i+1]="0123456789abcdef"[bytes[i]&15];
    }
    out[32]=0; return true;
}
void cetta_host_decision_free(CettaHostDecision *d) {
    if (!d) return;
    clear_evaluation(d);
    cetta_durable_observation_free(d->observation);
    for (size_t i=0;d->spaces && i<d->space_count;++i) {
        free((char *)d->spaces[i].scope.space); free((char *)d->spaces[i].scope.key);
    }
    for (size_t i=0;d->channels && i<d->channel_count;++i) {
        free((char *)d->channels[i].name); free((char *)d->channels[i].version);
    }
    free(d->input); free(d->actor); free(d->program);
    free(d->spaces); free(d->channels); free(d);
}
const CettaDurableSnapshot *cetta_host_view(const CettaHostDecision *d, size_t i) {
    return d && i<2+d->space_count ? cetta_durable_observation_view(d->observation,i) : NULL;
}
const char *cetta_host_decision_key(const CettaHostDecision *d) { return d ? d->nonce : NULL; }
CettaDurableStatus cetta_host_begin(CettaDurableStore *store,
    const CettaHostDecisionSpec *s, CettaHostDecision **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !s || !name(s->input_key) || !name(s->actor_key) || !name(s->program_version) ||
        (s->space_count && !s->spaces) || (s->channel_count && !s->channels)) return DURABLE_INVALID;
    if (s->space_count>HOST_GRANTS || s->channel_count>HOST_GRANTS) return DURABLE_LIMIT;
    for (size_t i=0;i<s->space_count;++i) {
        const CettaDurableScope *q=&s->spaces[i].scope;
        if (!name(q->space) || (s->spaces[i].writable && reserved(q->space)) ||
            q->kind<DURABLE_KEY || q->kind>DURABLE_SPACE ||
            (q->kind==DURABLE_KEY && !name(q->key)) ||
            (q->kind==DURABLE_PREFIX && (!q->key || strnlen(q->key,256)>255))) return DURABLE_INVALID;
    }
    for (size_t i=0;i<s->channel_count;++i)
        if (!name(s->channels[i].name) || !name(s->channels[i].version) || !s->channels[i].validate)
            return DURABLE_INVALID;
    CettaHostDecision *d=calloc(1,sizeof(*d));
    if (!d) return DURABLE_NOMEM;
    d->store=store; d->space_count=s->space_count; d->channel_count=s->channel_count;
    d->input=strdup(s->input_key); d->actor=strdup(s->actor_key); d->program=strdup(s->program_version);
    d->spaces=calloc(s->space_count+1,sizeof(*d->spaces));
    d->channels=calloc(s->channel_count+1,sizeof(*d->channels));
    CettaDurableStatus status=DURABLE_NOMEM;
    if (!d->input || !d->actor || !d->program || !d->spaces || !d->channels) goto fail;
    CettaDurableScope scopes[HOST_GRANTS+4]={
        {DURABLE_KEY,"host.inbox",d->input}, {DURABLE_KEY,"host.actors",d->actor}
    };
    for (size_t i=0;i<s->space_count;++i) {
        d->spaces[i]=s->spaces[i];
        d->spaces[i].scope.space=strdup(s->spaces[i].scope.space);
        d->spaces[i].scope.key=strdup(s->spaces[i].scope.kind==DURABLE_SPACE?"":s->spaces[i].scope.key);
        if (!d->spaces[i].scope.space || !d->spaces[i].scope.key) goto fail;
        scopes[2+i]=d->spaces[i].scope;
    }
    for (size_t i=0;i<s->channel_count;++i) {
        d->channels[i]=s->channels[i];
        d->channels[i].name=strdup(s->channels[i].name);
        d->channels[i].version=strdup(s->channels[i].version);
        if (!d->channels[i].name || !d->channels[i].version) goto fail;
    }
    if (!nonce(d->nonce)) { status=DURABLE_IO; goto fail; }
    snprintf(d->prefix,sizeof(d->prefix),"%s/",d->nonce);
    scopes[2+s->space_count]=(CettaDurableScope){DURABLE_KEY,"host.commits",d->nonce};
    scopes[3+s->space_count]=(CettaDurableScope){DURABLE_PREFIX,"host.outbox",d->prefix};
    status=cetta_durable_observe(store,scopes,4+s->space_count,&d->observation);
    if (status!=DURABLE_OK) goto fail;
    if (!cetta_host_view(d,0)->count) { status=DURABLE_PRECONDITION; goto fail; }
    if (cetta_durable_observation_view(d->observation,2+s->space_count)->count ||
        cetta_durable_observation_view(d->observation,3+s->space_count)->count) {
        status=DURABLE_CONFLICT; goto fail;
    }
    *out=d; return DURABLE_OK;
fail:
    cetta_host_decision_free(d); return status;
}

static bool form(const Atom *a, const char *head, size_t count) {
    return a && a->kind==ATOM_EXPR && a->expr.len==count && atom_is_symbol(a->expr.elems[0],head);
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING ? a->ground.sval : NULL;
}
static bool index_of(const Atom *a, size_t count, size_t *index) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT ||
        a->ground.ival<0 || (uint64_t)a->ground.ival>=count) return false;
    *index=(size_t)a->ground.ival; return true;
}
static bool covers(const CettaDurableScope *q, const char *key) {
    return q->kind==DURABLE_SPACE || (q->kind==DURABLE_KEY ? !strcmp(q->key,key) :
        !strncmp(q->key,key,strlen(q->key)));
}
static bool present(const CettaDurableSnapshot *v, const char *key) {
    for (size_t i=0;i<v->count;++i) if (!strcmp(v->records[i].key,key)) return true;
    return false;
}
static CettaDurableStatus encoded(CettaDurableOp *op, const Atom *value) {
    unsigned char *bytes=NULL; size_t size=0;
    CettaDurableStatus status=cetta_durable_value_encode(value,&bytes,&size);
    if (status==DURABLE_OK) { op->data=bytes; op->size=size; }
    return status;
}
const EvalOutcome *cetta_host_outcome(const CettaHostDecision *d) {
    return d && d->evaluation ? &d->evaluation->outcome : NULL;
}
CettaDurableStatus cetta_host_watch(const CettaHostDecision *d, size_t limit, CettaDurableWatch **out) {
    return cetta_durable_watch_new(d?d->observation:NULL,limit,out);
}
CettaDurableStatus cetta_host_eval_create(const CettaHostProgram *program,
                                         CettaHostEvaluation **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!program || !program->space || !program->context || !name(program->version))
        return DURABLE_INVALID;
    CettaHostEvaluation *e=calloc(1,sizeof(*e));
    if (!e) return DURABLE_NOMEM;
    arena_init(&e->persistent); arena_init(&e->scratch); space_init(&e->space);
    registry_init(&e->registry); eval_outcome_init(&e->outcome);
    CettaDurableStatus status=DURABLE_INVALID;
    for (CettaCount i=0;i<space_length64(program->space);++i) {
        Atom *a=space_get_at64(program->space,i);
        if (!a || (a->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED) ||
            (a->kind==ATOM_EXPR && a->expr.len &&
                (atom_is_symbol(a->expr.elems[0],"host:record") ||
                 atom_is_symbol(a->expr.elems[0],"host:record-version")))) goto fail;
        Atom *copy=atom_deep_copy(&e->persistent,a);
        if (!copy) { status=DURABLE_NOMEM; goto fail; }
        space_add(&e->space,copy);
    }
    if (!space_copy_module_view(&e->space,program->space)) {
        status=DURABLE_NOMEM;
        goto fail;
    }
    registry_bind(&e->registry,"&self",atom_space(&e->persistent,&e->space));
    *out=e; return DURABLE_OK;
fail:
    cetta_host_eval_free(e); return status;
}
CettaDurableStatus cetta_host_eval_run(CettaHostEvaluation *e,
    const CettaHostProgram *program, Atom *expression, int fuel) {
    if (!e) return DURABLE_INVALID;
    eval_outcome_free(&e->outcome); eval_outcome_init(&e->outcome);
    if (!program || !program->context || !expression || fuel<=0 ||
        (expression->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED)) return DURABLE_INVALID;
    Atom *copy=atom_deep_copy(&e->persistent,expression);
    if (!copy) return DURABLE_NOMEM;
    eval_top_speculative(program->context,&e->space,&e->scratch,&e->persistent,&e->registry,
        copy,fuel,&e->outcome);
    return DURABLE_OK;
}
CettaDurableStatus cetta_host_evaluate(CettaHostDecision *d,
    const CettaHostProgram *program, Atom *expression, int fuel) {
    if (!d) return DURABLE_INVALID;
    if (d->spent) return DURABLE_CONFLICT;
    clear_evaluation(d);
    if (!program || !program->space || !program->context || !name(program->version) ||
        !expression || fuel<=0 || (expression->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED)) return DURABLE_INVALID;
    if (strcmp(d->program,program->version)) return DURABLE_VERSION;
    CettaDurableStatus status=cetta_host_eval_create(program,&d->evaluation);
    if (status!=DURABLE_OK) return status;
    CettaHostEvaluation *e=d->evaluation;
    const CettaDurableSnapshot *v;
    for (size_t i=0;(v=cetta_host_view(d,i));++i) {
        for (size_t j=0;j<v->count;++j) {
            Atom *value=NULL;
            status=cetta_durable_value_decode(&e->persistent,v->records[j].data,v->records[j].size,&value);
            if (status!=DURABLE_OK) goto fail;
            Atom *fact[]={atom_symbol(&e->persistent,"host:record"),atom_int(&e->persistent,(int64_t)i),
                atom_string(&e->persistent,v->records[j].key),value};
            space_add(&e->space,atom_expr(&e->persistent,fact,4));
            Atom *meta[]={atom_symbol(&e->persistent,"host:record-version"),
                atom_int(&e->persistent,(int64_t)i),atom_string(&e->persistent,v->records[j].key),
                atom_int(&e->persistent,v->records[j].revision),atom_int(&e->persistent,v->records[j].position)};
            space_add(&e->space,atom_expr(&e->persistent,meta,5));
        }
    }
    status=cetta_host_eval_run(e,program,expression,fuel);
    if (status==DURABLE_OK) return status;
fail:
    clear_evaluation(d); return status;
}
CettaDurableStatus cetta_host_accept(CettaHostDecision *d,
    size_t selected, CettaHostCommit *commit) {
    if (!commit) return DURABLE_INVALID;
    memset(commit,0,sizeof(*commit));
    commit->revision=-1;
    if (!d) return DURABLE_INVALID;
    if (d->spent) return DURABLE_CONFLICT;
    const EvalOutcome *outcome=cetta_host_outcome(d);
    if (!outcome) return DURABLE_PRECONDITION;
    if (outcome->completion!=CETTA_EVAL_COMPLETE || outcome->effect_denials ||
        !outcome->budget_limited || !outcome->budget_initial) return DURABLE_PRECONDITION;
    if (selected>=outcome->results.len) return DURABLE_INVALID;
    const Atom *p=outcome->results.items[selected];
    if (!form(p,"host:transition",4)) return DURABLE_INVALID;
    const Atom *writes=p->expr.elems[2], *effects=p->expr.elems[3];
    if (writes->kind!=ATOM_EXPR || effects->kind!=ATOM_EXPR) return DURABLE_INVALID;
    size_t nw=writes->expr.len, ne=effects->expr.len;
    if (nw>HOST_ACTIONS || ne>HOST_ACTIONS || nw+ne>HOST_ACTIONS) return DURABLE_LIMIT;
    /* Validate the entire closed value and bound expansion before inspecting
     * policy payloads or copying subtrees (also rejects cyclic/native data). */
    unsigned char *bytes=NULL; size_t length=0;
    CettaDurableStatus status=cetta_durable_value_encode(p,&bytes,&length);
    free(bytes);
    if (status!=DURABLE_OK) return status;
    if (length>HOST_VALUE_BYTES) return DURABLE_LIMIT;
    CettaDurableOp ops[HOST_ACTIONS+3]={0};
    char keys[HOST_ACTIONS][64];
    Arena a; arena_init(&a);
    ops[0]=(CettaDurableOp){DURABLE_REMOVE,"host.inbox",d->input,NULL,0};
    ops[1]=(CettaDurableOp){cetta_host_view(d,1)->count?DURABLE_REPLACE:DURABLE_INSERT,
        "host.actors",d->actor,NULL,0};
    Atom *actor[]={atom_symbol(&a,"host:actor"),atom_int(&a,1),atom_string(&a,d->program),p->expr.elems[1]};
    status=encoded(&ops[1],atom_expr(&a,actor,4)); if (status!=DURABLE_OK) goto done;
    ops[2]=(CettaDurableOp){DURABLE_INSERT,"host.commits",d->nonce,NULL,0};
    Atom *receipt[]={atom_symbol(&a,"host:commit"),atom_int(&a,1),atom_string(&a,d->program),
        atom_string(&a,d->input),atom_string(&a,d->actor),atom_int(&a,(int64_t)selected)};
    status=encoded(&ops[2],atom_expr(&a,receipt,6)); if (status!=DURABLE_OK) goto done;
    for (size_t i=0;i<nw;++i) {
        const Atom *w=writes->expr.elems[i]; size_t grant;
        bool put=form(w,"host:put",4);
        status=DURABLE_INVALID;
        if (!put && !form(w,"host:remove",3)) goto done;
        const char *key=string(w->expr.elems[2]);
        if (!index_of(w->expr.elems[1],d->space_count,&grant) || !name(key)) goto done;
        const CettaHostSpaceGrant *g=&d->spaces[grant];
        if (!g->writable || !covers(&g->scope,key)) goto done;
        bool exists=present(cetta_host_view(d,2+grant),key);
        ops[3+i]=(CettaDurableOp){put?(exists?DURABLE_REPLACE:DURABLE_INSERT):DURABLE_REMOVE,
            g->scope.space,key,NULL,0};
        for (size_t j=0;j<i;++j)
            if (!strcmp(ops[3+j].space,g->scope.space) && !strcmp(ops[3+j].key,key)) goto done;
        if (put) { status=encoded(&ops[3+i],w->expr.elems[3]); if (status!=DURABLE_OK) goto done; }
    }
    for (size_t i=0;i<ne;++i) {
        const Atom *e=effects->expr.elems[i]; size_t grant;
        status=DURABLE_INVALID;
        if (!form(e,"host:send",4) || !index_of(e->expr.elems[1],d->channel_count,&grant)) goto done;
        const CettaHostChannelGrant *g=&d->channels[grant];
        if (!g->validate(g->context,e->expr.elems[2],e->expr.elems[3])) goto done;
        snprintf(keys[i],sizeof(keys[i]),"%s%zu",d->prefix,i);
        ops[3+nw+i]=(CettaDurableOp){DURABLE_INSERT,"host.outbox",keys[i],NULL,0};
        Atom *intent[]={atom_symbol(&a,"host:intent"),atom_int(&a,1),
            atom_string(&a,g->name),atom_string(&a,g->version),e->expr.elems[2],e->expr.elems[3]};
        status=encoded(&ops[3+nw+i],atom_expr(&a,intent,6)); if (status!=DURABLE_OK) goto done;
    }
    /* No application code past this point. Acknowledgment is not a second
     * commit: outbox rows get the acceptance revision and operation position. */
    d->spent=true;
    status=cetta_durable_commit_observed(d->store,d->observation,ops,3+nw+ne,&commit->revision);
    if (status==DURABLE_OK) {
        memcpy(commit->epoch,cetta_host_view(d,0)->epoch,sizeof(commit->epoch));
        memcpy(commit->commit_key,d->nonce,sizeof(commit->commit_key)); commit->effects=ne;
    }
done:
    for (size_t i=0;i<3+nw+ne;++i) free((void *)ops[i].data);
    arena_free(&a); return status;
}
