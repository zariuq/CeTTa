#define _POSIX_C_SOURCE 200809L
#include "durable_host.h"
#include "durable_eval.h"
#include "durable_value.h"
#include "library.h"
#include "prime_ordinary_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HOST_GRANTS 64
#define HOST_ACTIONS 128
#define HOST_VALUE_BYTES (1024u*1024u)
/* Setup, two admissions, five lifecycle callbacks and one source/preparation/
 * transport settlement each are the complete first-poll occurrence bound. */
_Static_assert(CETTA_HOST_PLAN_RECEIPT_CAPACITY>=10u,
    "retained host poll receipt is too small");

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
    CettaOwnedTask planner;
};

static void host_return_copy_clear(CettaHostEvaluation *e,bool discard) {
    if (!e) return;
    atom_deep_copy_session_free(e->return_copy);
    if (discard && e->return_marked) {
        arena_reset(&e->scratch,e->return_mark);
        arena_release_spare(&e->scratch,e->return_spare);
    }
    e->return_copy=NULL;e->return_source=NULL;
    e->return_marked=false;
}

void cetta_host_eval_free(CettaHostEvaluation *e) {
    if (!e) return;
    host_return_copy_clear(e,true);
    eval_outcome_free(&e->outcome);
    registry_free(&e->registry); space_free(&e->space);
    arena_free(&e->scratch); arena_free(&e->persistent);
    free(e);
}
static void clear_evaluation(CettaHostDecision *d) {
    cetta_host_plan_cancel(d);
    cetta_host_eval_free(d->evaluation); d->evaluation=NULL;
}

static bool name(const char *s) { return s && *s && strnlen(s,256)<=255; }
static bool reserved(const char *s) { return !strncmp(s,"host.",5); }
static bool return_slice_valid(const CettaPrimeReturnSlice *slice) {
    return slice &&
        (slice->limit_preparation
            ? slice->preparation!=UINT64_MAX : !slice->preparation) &&
        (slice->limit_transport
            ? slice->transport!=UINT64_MAX : !slice->transport);
}
static void add_work(uint64_t *total,uint64_t amount) {
    *total=*total>UINT64_MAX-amount?UINT64_MAX:*total+amount;
}
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
static CettaDurableStatus host_begin(CettaDurableStore *store,
    const CettaHostDecisionSpec *s, const CettaDurableObservation *required, CettaHostDecision **out) {
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
    status=required?cetta_durable_observe_joined(store,required,scopes,4+s->space_count,&d->observation):
        cetta_durable_observe(store,scopes,4+s->space_count,&d->observation);
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

CettaDurableStatus cetta_host_begin(CettaDurableStore *store,
    const CettaHostDecisionSpec *s, CettaHostDecision **out) {
    return host_begin(store,s,NULL,out);
}

CettaDurableStatus cetta_host_begin_with_dependencies(CettaDurableStore *store,
    const CettaHostDecisionSpec *s, const CettaDurableObservation *required, CettaHostDecision **out) {
    if (!required) { if (out) *out=NULL; return DURABLE_INVALID; }
    return host_begin(store,s,required,out);
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
    if (!d || !d->evaluation) return NULL;
    CettaHostEvaluation *e=d->evaluation;
    if (e->retained && !e->cancelled) {
        uint64_t work;
        if (!eval_prime_source_execution_work(&d->planner.execution,&work))
            e->outcome.completion=CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        else {
            e->outcome.steps_spent=work;
            e->outcome.budget_remaining=work<e->limits.work_limit?e->limits.work_limit-work:0u;
            const CettaOwnedExecution *execution=&d->planner.execution;
            e->outcome.completion=execution->terminal?execution->completion:
                !e->outcome.budget_remaining?CETTA_EVAL_INCOMPLETE_FUEL:
                execution->completion==CETTA_EVAL_COMPLETE?CETTA_EVAL_INCOMPLETE_FUEL:
                execution->completion;
            if (e->plan_refusal!=DURABLE_OK)
                e->outcome.completion=e->plan_refusal==DURABLE_INVALID?
                    CETTA_EVAL_INCOMPLETE_HOST_FAILURE:CETTA_EVAL_INCOMPLETE_CAPACITY;
            if (execution->terminal && execution->stop!=CETTA_OWNED_EXECUTION_COMPLETE &&
                e->outcome.completion==CETTA_EVAL_COMPLETE)
                e->outcome.completion=CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        }
    }
    return &e->outcome;
}
CettaDurableStatus cetta_host_watch(const CettaHostDecision *d, size_t limit, CettaDurableWatch **out) {
    return cetta_durable_watch_new(d?d->observation:NULL,limit,out);
}
static CettaDurableStatus host_eval_allocate(const CettaHostProgram *program,
        CettaHostEvaluation **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!program || !program->space || !program->context || !name(program->version))
        return DURABLE_INVALID;
    CettaHostEvaluation *e=calloc(1,sizeof(*e));
    if (!e) return DURABLE_NOMEM;
    arena_init(&e->persistent); arena_init(&e->scratch); space_init(&e->space);
    registry_init(&e->registry); eval_outcome_init(&e->outcome);
    *out=e;return DURABLE_OK;
}

static CettaDurableStatus host_eval_copy_program(CettaHostEvaluation *e,
        const CettaHostProgram *program,uint64_t inspection_limit,
        uint64_t byte_limit,CettaHostSetupWork *work) {
    if (!e || !program || !program->space || !inspection_limit || !byte_limit)
        return DURABLE_INVALID;
    AtomDeepCopySession *copy=atom_deep_copy_session_new(&e->persistent);
    if (!copy) return DURABLE_NOMEM;
    CettaDurableStatus status=DURABLE_INVALID;
    uint64_t inspections=0u;
    for (CettaCount i=0;i<space_length64(program->space);++i) {
        Atom *a=space_get_at64(program->space,i);
        if (!a || (a->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED) ||
            (a->kind==ATOM_EXPR && a->expr.len &&
                (atom_is_symbol(a->expr.elems[0],"host:record") ||
                 atom_is_symbol(a->expr.elems[0],"host:record-version")))) goto fail;
        if (inspections>=inspection_limit) {status=DURABLE_LIMIT;goto fail;}
        Atom *image=NULL;uint64_t used=0u;
        AtomDeepCopyStep step=atom_deep_copy_session_advance(
            copy,a,inspection_limit-inspections,&image,&used);
        if (used>inspection_limit-inspections) {status=DURABLE_CORRUPT;goto fail;}
        inspections+=used;
        if (step==ATOM_DEEP_COPY_PAUSED) {status=DURABLE_LIMIT;goto fail;}
        if (step!=ATOM_DEEP_COPY_COMPLETE || !image) {status=DURABLE_NOMEM;goto fail;}
        space_add(&e->space,image);
        size_t bytes=arena_accounted_live_bytes(&e->persistent);
        if (bytes==SIZE_MAX || bytes>byte_limit) {status=DURABLE_LIMIT;goto fail;}
    }
    if (!space_copy_module_view(&e->space,program->space)) {
        status=DURABLE_NOMEM;
        goto fail;
    }
    registry_bind(&e->registry,"&self",atom_space(&e->persistent,&e->space));
    {
        size_t bytes=arena_accounted_live_bytes(&e->persistent);
        if (bytes==SIZE_MAX || bytes>byte_limit) {status=DURABLE_LIMIT;goto fail;}
        if (work) *work=(CettaHostSetupWork){inspections,(uint64_t)bytes};
    }
    atom_deep_copy_session_free(copy);return DURABLE_OK;
fail:
    atom_deep_copy_session_free(copy);return status;
}

CettaDurableStatus cetta_host_eval_create(const CettaHostProgram *program,
                                         CettaHostEvaluation **out) {
    CettaHostEvaluation *e=NULL;
    CettaDurableStatus status=host_eval_allocate(program,&e);
    if (status==DURABLE_OK)
        status=host_eval_copy_program(e,program,UINT64_MAX,SIZE_MAX,NULL);
    if (status!=DURABLE_OK) {cetta_host_eval_free(e);return status;}
    *out=e;return DURABLE_OK;
}
CettaDurableStatus cetta_host_eval_run(CettaHostEvaluation *e,
    const CettaHostProgram *program, Atom *expression, int fuel) {
    if (!e) return DURABLE_INVALID;
    eval_outcome_free(&e->outcome); eval_outcome_init(&e->outcome);
    if (!program || !program->context ||
        program->context->session.language_id!=CETTA_LANGUAGE_HE || !expression || fuel<=0 ||
        (expression->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED)) return DURABLE_INVALID;
    Atom *copy=atom_deep_copy(&e->persistent,expression);
    if (!copy) return DURABLE_NOMEM;
    eval_top_speculative(program->context,&e->space,&e->scratch,&e->persistent,&e->registry,
        copy,fuel,&e->outcome);
    return DURABLE_OK;
}
static CettaDurableStatus host_projection(CettaHostDecision *d,
        const CettaHostProgram *program,const CettaHostPlanLimits *limits) {
    CettaDurableStatus status=host_eval_allocate(program,&d->evaluation);
    if (status!=DURABLE_OK) return status;
    CettaHostEvaluation *e=d->evaluation;
    status=host_eval_copy_program(e,program,
        limits?limits->preparation_inspections:UINT64_MAX,
        limits?limits->preparation_bytes:SIZE_MAX,
        limits?&e->setup_work:NULL);
    if (status!=DURABLE_OK) goto fail;
    const CettaDurableSnapshot *v;
    for (size_t i=0;(v=cetta_host_view(d,i));++i) {
        for (size_t j=0;j<v->count;++j) {
            if (limits && (e->setup_work.inspections>
                    limits->preparation_inspections-2u ||
                v->records[j].size>limits->preparation_bytes-
                    (size_t)e->setup_work.arena_bytes)) {
                status=DURABLE_LIMIT;goto fail;
            }
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
            if (limits) {
                size_t bytes=arena_accounted_live_bytes(&e->persistent);
                if (bytes==SIZE_MAX || bytes>limits->preparation_bytes) {
                    status=DURABLE_LIMIT;goto fail;
                }
                e->setup_work.inspections+=2u;
                e->setup_work.arena_bytes=(uint64_t)bytes;
            }
        }
    }
    return DURABLE_OK;
fail:
    clear_evaluation(d); return status;
}
CettaDurableStatus cetta_host_evaluate(CettaHostDecision *d,
    const CettaHostProgram *program, Atom *expression, int fuel) {
    if (!d) return DURABLE_INVALID;
    if (d->spent) return DURABLE_CONFLICT;
    clear_evaluation(d);
    if (!program || !program->space || !program->context ||
        program->context->session.language_id!=CETTA_LANGUAGE_HE || !name(program->version) ||
        !expression || fuel<=0 || (expression->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED)) return DURABLE_INVALID;
    if (strcmp(d->program,program->version)) return DURABLE_VERSION;
    CettaDurableStatus status=host_projection(d,program,NULL);
    if (status==DURABLE_OK) status=cetta_host_eval_run(d->evaluation,program,expression,fuel);
    if (status!=DURABLE_OK) clear_evaluation(d);
    return status;
}

static CettaOwnedTaskAdmission host_plan_admit(void *raw) {
    CettaHostDecision *d=raw;
    CettaHostEvaluation *e=d->evaluation;
    uint64_t work;
    if (d->spent || !e || !e->retained || e->cancelled ||
        !eval_prime_source_execution_work(&d->planner.execution,&work) ||
        !eval_prime_source_execution_is_current(&d->planner.execution))
        return CETTA_OWNED_TASK_ADMISSION_INVALIDATED;
    uint64_t remaining=work<e->limits.work_limit?e->limits.work_limit-work:0u;
    CettaPrimeReturnSlice source=e->limits.return_slice;
    source.transport=0u;source.limit_transport=false;
    return eval_prime_source_execution_set_return_slice(&d->planner.execution,&source) &&
        eval_prime_source_execution_set_quantum(&d->planner.execution,
            remaining<e->limits.quantum?remaining:e->limits.quantum)?
        CETTA_OWNED_TASK_ADMISSION_READY:CETTA_OWNED_TASK_ADMISSION_INVALIDATED;
}
static bool host_plan_collect(void *raw,const CettaOwnedReturn *returned) {
    CettaHostDecision *d=raw;
    CettaHostEvaluation *e=d->evaluation;
    e->plan_refusal=DURABLE_INVALID;
    Atom *term=returned->outcome.term;
    if (returned->outcome.kind!=CETTA_CALL_VALUE || returned->outcome.delayed ||
        returned->outcome.result_form!=CETTA_CALL_RESULT_COMPLETED_VALUE || !term ||
        (term->flags&(ATOM_FLAG_HAS_VARS|ATOM_FLAG_HAS_IDENTITY_GROUNDED))) return false;
    if (e->outcome.results.len>=e->limits.results) { e->plan_refusal=DURABLE_LIMIT;return false; }
    if (e->return_copy && e->return_source!=term) return false;
    if (!e->return_copy) {
        e->return_mark=arena_mark(&e->scratch);
        e->return_spare=e->scratch.spare_bytes;
        e->return_copy=atom_deep_copy_session_new(&e->scratch);
        if (!e->return_copy) { e->plan_refusal=DURABLE_NOMEM;return false; }
        e->return_source=term;e->return_marked=true;
    }
    uint64_t used=0u;
    Atom *copy=NULL;
    uint64_t allowance=e->limits.return_slice.limit_transport?
        e->limits.return_slice.transport:UINT64_MAX;
    AtomDeepCopyStep step=atom_deep_copy_session_advance(
        e->return_copy,term,allowance,&copy,&used);
    add_work(&e->return_work.transport,used);
    if (step==ATOM_DEEP_COPY_PAUSED) { e->plan_refusal=DURABLE_OK;return false; }
    if (step!=ATOM_DEEP_COPY_COMPLETE || !copy) {
        e->plan_refusal=DURABLE_NOMEM;return false;
    }
    CettaCount slot=e->outcome.results.len;
    result_set_add(&e->outcome.results,NULL);
    if (e->outcome.results.len==slot) { e->plan_refusal=DURABLE_NOMEM;return false; }
    e->outcome.results.items[slot]=copy;
    host_return_copy_clear(e,false);e->plan_refusal=DURABLE_OK;
    return true;
}
CettaDurableStatus cetta_host_plan_open(CettaHostDecision *d,
    const CettaHostProgram *program,Atom *expression,const CettaHostPlanLimits *limits) {
    if (!d) return DURABLE_INVALID;
    if (d->spent) return DURABLE_CONFLICT;
    clear_evaluation(d);
    if (!program || !program->space || !program->context || !name(program->version) ||
        !expression || (expression->flags&ATOM_FLAG_HAS_IDENTITY_GROUNDED) ||
        !limits || !limits->quantum || limits->quantum==UINT64_MAX ||
        limits->work_limit==UINT64_MAX ||
        !return_slice_valid(&limits->return_slice)) return DURABLE_INVALID;
    if (strcmp(d->program,program->version)) return DURABLE_VERSION;
    if (limits->preparation_inspections<3u || !limits->preparation_bytes)
        return DURABLE_INVALID;
    CettaDurableStatus status=host_projection(d,program,limits);
    if (status!=DURABLE_OK) return status;
    CettaHostEvaluation *e=d->evaluation;
    CettaPrimeSourceOwner *source=NULL;
    CettaOwnedExecution execution={0};
    size_t remaining_inspections=limits->preparation_inspections-
        (size_t)e->setup_work.inspections;
    size_t remaining_bytes=limits->preparation_bytes-
        (size_t)e->setup_work.arena_bytes;
    CettaContinuationStatus captured=eval_prime_source_owner_capture_context(program->context,
        &e->space,&e->registry,expression,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,
        remaining_inspections,remaining_bytes,&source);
    if (captured==CETTA_CONTINUATION_READY)
        captured=eval_prime_source_execution_open(source,limits->quantum,&execution);
    eval_prime_source_owner_release(source);
    if (captured==CETTA_CONTINUATION_READY && limits->credit)
        captured=eval_prime_source_execution_fund(&execution,limits->credit,limits->work_limit);
    if (captured!=CETTA_CONTINUATION_READY) {
        status=captured==CETTA_CONTINUATION_CAPACITY?DURABLE_LIMIT:DURABLE_PRECONDITION;
        goto fail;
    }
    e->limits=*limits;e->retained=true;
    e->outcome.budget_limited=true;e->outcome.budget_initial=limits->work_limit;
    e->outcome.budget_remaining=limits->work_limit;
    e->outcome.completion=CETTA_EVAL_INCOMPLETE_FUEL;
    if (!cetta_owned_task_init(&d->planner,&execution,host_plan_admit,host_plan_collect,d)) {
        status=DURABLE_PRECONDITION;goto fail;
    }
    return DURABLE_OK;
fail:
    cetta_owned_execution_cancel(&execution);clear_evaluation(d);return status;
}
CettaOwnedTask *cetta_host_plan_task(CettaHostDecision *d) {
    return d && d->evaluation && d->evaluation->retained && !d->evaluation->cancelled?
        &d->planner:NULL;
}
bool cetta_host_plan_return_work(
    const CettaHostDecision *d,CettaPrimeReturnWork *out) {
    if (!out) return false;
    *out=(CettaPrimeReturnWork){0};
    if (!d || !d->evaluation || !d->evaluation->retained ||
        d->evaluation->cancelled ||
        !eval_prime_source_execution_return_work(&d->planner.execution,out))
        return false;
    out->transport=d->evaluation->return_work.transport;
    return true;
}
typedef struct {
    CettaHostDecision *decision;
    CettaHostPlanPollAccount *out;
    CettaNativeCostEventSource events;
    uint64_t source_seen;
    uint64_t issued_total;
    CettaPrimeReturnWork returns_seen;
    bool complete;
} HostPlanReceiptBuilder;

static bool host_plan_receipt_append(HostPlanReceiptBuilder *builder,
    CettaHostPlanTag tag,CettaNativeCostKind kind,uint64_t units) {
    if (!units) return true;
    CettaHostPlanReceipt *receipt=&builder->out->receipt;
    if (!builder->complete || receipt->count>=CETTA_HOST_PLAN_RECEIPT_CAPACITY ||
        tag>CETTA_HOST_PLAN_SETUP || (unsigned)kind>=CETTA_COST_KIND_COUNT) {
        builder->complete=false;return false;
    }
    CettaNativeCostEvent event={0};
    if (!cetta_native_cost_event_source_issue(
            &builder->events,kind,units,&event) ||
        builder->issued_total>UINT64_MAX-units) {
        builder->complete=false;return false;
    }
    builder->issued_total+=units;
    if (!event.identity || !event.origin || !event.occurrence ||
        event.repetitions!=1u || event.units!=units || event.kind!=kind) {
        builder->complete=false;return false;
    }
    receipt->occurrences[receipt->count++]=(CettaHostPlanOccurrence){tag,event};
    return true;
}

static bool host_plan_receipt_progress(HostPlanReceiptBuilder *builder,
    bool source,bool transport) {
    uint64_t current_source=0u;
    CettaPrimeReturnWork current_returns={0};
    if (!eval_prime_source_execution_work(
            &builder->decision->planner.execution,&current_source) ||
        !cetta_host_plan_return_work(builder->decision,&current_returns) ||
        current_source<builder->source_seen ||
        current_returns.preparation<builder->returns_seen.preparation ||
        current_returns.transport<builder->returns_seen.transport) {
        builder->complete=false;return false;
    }
    uint64_t source_delta=current_source-builder->source_seen;
    uint64_t preparation_delta=
        current_returns.preparation-builder->returns_seen.preparation;
    uint64_t transport_delta=current_returns.transport-builder->returns_seen.transport;
    if ((!source && (source_delta || preparation_delta)) ||
        (!transport && transport_delta)) {
        builder->complete=false;return false;
    }
    if (source) {
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_SOURCE_WORK,
            CETTA_COST_QUANTUM,source_delta);
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_PREPARATION,
            CETTA_COST_ANALYSIS_STEP,preparation_delta);
    }
    if (transport)
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_TRANSPORT,
            CETTA_COST_CAPTURE,transport_delta);
    builder->source_seen=current_source;
    builder->returns_seen=current_returns;
    return builder->complete;
}

static void host_plan_receipt_observe(void *raw,CettaOwnedTaskPhase phase,
    uint64_t detail) {
    HostPlanReceiptBuilder *builder=raw;
    switch (phase) {
    case CETTA_OWNED_TASK_PHASE_ADMISSION: {
        CettaHostPlanTag tag;
        switch ((CettaOwnedTaskAdmission)detail) {
        case CETTA_OWNED_TASK_ADMISSION_READY:
            tag=CETTA_HOST_PLAN_ADMISSION_READY;break;
        case CETTA_OWNED_TASK_ADMISSION_PAUSED:
            tag=CETTA_HOST_PLAN_ADMISSION_PAUSED;break;
        case CETTA_OWNED_TASK_ADMISSION_REFUSED:
            tag=CETTA_HOST_PLAN_ADMISSION_REFUSED;break;
        case CETTA_OWNED_TASK_ADMISSION_INVALIDATED:
            tag=CETTA_HOST_PLAN_ADMISSION_INVALIDATED;break;
        default:builder->complete=false;return;
        }
        (void)host_plan_receipt_append(builder,tag,CETTA_COST_ANALYSIS_STEP,1u);
        break;
    }
    case CETTA_OWNED_TASK_PHASE_PRODUCER:
        (void)host_plan_receipt_progress(builder,true,false);
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_PRODUCER,
            CETTA_COST_ANALYSIS_STEP,1u);
        break;
    case CETTA_OWNED_TASK_PHASE_CONSUMER:
        (void)host_plan_receipt_progress(builder,false,true);
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_CONSUMER,
            CETTA_COST_ANALYSIS_STEP,1u);
        break;
    case CETTA_OWNED_TASK_PHASE_FINAL_CURRENT:
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_FINAL_CURRENT,
            CETTA_COST_ANALYSIS_STEP,1u);
        break;
    case CETTA_OWNED_TASK_PHASE_RETIREMENT:
        (void)host_plan_receipt_append(builder,CETTA_HOST_PLAN_RETIREMENT,
            CETTA_COST_FRAME_RETIRE,1u);
        break;
    case CETTA_OWNED_TASK_PHASE_ROTATION:
    default:
        builder->complete=false;
    }
}

static bool add_exact(uint64_t *total,uint64_t units) {
    if (*total>UINT64_MAX-units) return false;
    *total+=units;return true;
}

bool cetta_host_plan_receipt_summarize(const CettaHostPlanReceipt *receipt,
    CettaHostPlanReceiptSummary *summary) {
    if (!receipt || !summary || !receipt->complete ||
        receipt->count>CETTA_HOST_PLAN_RECEIPT_CAPACITY ||
        receipt->step>CETTA_OWNED_EXECUTION_INTERRUPTED)
        return false;
    *summary=(CettaHostPlanReceiptSummary){
        .step=receipt->step,.initial_pending=receipt->initial_pending};
    uint64_t identity=0u,origin=0u,occurrence=0u;
    for(uint8_t i=0u;i<receipt->count;i++) {
        const CettaHostPlanOccurrence *item=&receipt->occurrences[i];
        const CettaNativeCostEvent *event=&item->event;
        if (item->tag>CETTA_HOST_PLAN_SETUP || !event->identity ||
            !event->origin || !event->occurrence || !event->units ||
            event->repetitions!=1u || (unsigned)event->kind>=CETTA_COST_KIND_COUNT ||
            event->identity<=identity || event->occurrence<=occurrence ||
            (origin && event->origin!=origin) ||
            !add_exact(&summary->total,event->units)) return false;
        identity=event->identity;origin=event->origin;occurrence=event->occurrence;
        switch(item->tag) {
        case CETTA_HOST_PLAN_SETUP:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP ||
                !add_exact(&summary->setup_work,event->units)) return false;
            break;
        case CETTA_HOST_PLAN_ADMISSION_READY:
        case CETTA_HOST_PLAN_ADMISSION_PAUSED:
        case CETTA_HOST_PLAN_ADMISSION_REFUSED:
        case CETTA_HOST_PLAN_ADMISSION_INVALIDATED:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP || event->units!=1u)
                return false;
            summary->admission++;
            summary->last_admission=(CettaOwnedTaskAdmission)(
                CETTA_OWNED_TASK_ADMISSION_READY+
                item->tag-CETTA_HOST_PLAN_ADMISSION_READY);
            break;
        case CETTA_HOST_PLAN_PRODUCER:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP || event->units!=1u)
                return false;
            summary->producer++;break;
        case CETTA_HOST_PLAN_SOURCE_WORK:
            if (event->kind!=CETTA_COST_QUANTUM ||
                !add_exact(&summary->source_work,event->units)) return false;
            break;
        case CETTA_HOST_PLAN_PREPARATION:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP ||
                !add_exact(&summary->returns.preparation,event->units)) return false;
            break;
        case CETTA_HOST_PLAN_CONSUMER:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP || event->units!=1u)
                return false;
            summary->consumer++;break;
        case CETTA_HOST_PLAN_TRANSPORT:
            if (event->kind!=CETTA_COST_CAPTURE ||
                !add_exact(&summary->returns.transport,event->units)) return false;
            break;
        case CETTA_HOST_PLAN_FINAL_CURRENT:
            if (event->kind!=CETTA_COST_ANALYSIS_STEP || event->units!=1u)
                return false;
            summary->final_is_current++;break;
        case CETTA_HOST_PLAN_RETIREMENT:
            if (event->kind!=CETTA_COST_FRAME_RETIRE || event->units!=1u)
                return false;
            summary->retirements++;break;
        }
    }
    return true;
}

bool cetta_host_plan_receipt_total(
    const CettaHostPlanReceipt *receipt,uint64_t *total) {
    CettaHostPlanReceiptSummary summary;
    if (!total || !cetta_host_plan_receipt_summarize(receipt,&summary)) return false;
    *total=summary.total;return true;
}

CettaDurableStatus cetta_host_plan_poll_accounted(
    CettaHostDecision *d,bool poll,CettaHostPlanPollAccount *out) {
    if (!out) return DURABLE_INVALID;
    *out=(CettaHostPlanPollAccount){0};
    CettaOwnedTask *task=cetta_host_plan_task(d);
    uint64_t source_before=0u,source_after=0u;
    CettaPrimeReturnWork return_before={0},return_after={0};
    if (!task || !eval_prime_source_execution_work(
            &task->execution,&source_before) ||
        !cetta_host_plan_return_work(d,&return_before))
        return DURABLE_PRECONDITION;
    out->receipt.initial_pending=task->execution.has_pending;
    HostPlanReceiptBuilder builder={.decision=d,.out=out,
        .source_seen=source_before,.returns_seen=return_before,.complete=true};
    if (!cetta_native_cost_event_source_init(&builder.events)) builder.complete=false;
    bool report_setup=!d->evaluation->setup_reported;
    if (report_setup) {
        out->setup=d->evaluation->setup_work;
        (void)host_plan_receipt_append(&builder,CETTA_HOST_PLAN_SETUP,
            CETTA_COST_ANALYSIS_STEP,out->setup.inspections);
    }
    out->visit=cetta_owned_task_poll_observed(task,poll,&out->protocol,
        host_plan_receipt_observe,&builder);
    out->receipt.step=out->visit.step;
    if (!eval_prime_source_execution_work(&task->execution,&source_after) ||
        !cetta_host_plan_return_work(d,&return_after) ||
        source_after<source_before ||
        return_after.preparation<return_before.preparation ||
        return_after.transport<return_before.transport) {
        out->receipt.complete=false;
        return DURABLE_CORRUPT;
    }
    if (source_after!=builder.source_seen ||
        return_after.preparation!=builder.returns_seen.preparation ||
        return_after.transport!=builder.returns_seen.transport)
        builder.complete=false;
    out->source_work=source_after-source_before;
    out->returns.preparation=
        return_after.preparation-return_before.preparation;
    out->returns.transport=return_after.transport-return_before.transport;
    out->receipt.complete=builder.complete;
    CettaHostPlanReceiptSummary summary={0};
    bool exact=cetta_host_plan_receipt_summarize(&out->receipt,&summary);
    if (out->protocol.phase_overflow || out->protocol.rotations ||
        out->protocol.producer>1u || out->protocol.consumer>1u ||
        out->protocol.final_is_current>1u || out->protocol.retirements>1u ||
        out->protocol.phase_count!=out->protocol.admission+out->protocol.producer+
            out->protocol.consumer+out->protocol.final_is_current+
            out->protocol.retirements ||
        (exact && (summary.initial_pending!=out->receipt.initial_pending ||
        summary.step!=out->visit.step ||
        summary.setup_work!=out->setup.inspections ||
        summary.last_admission!=out->visit.admission ||
        summary.admission!=out->protocol.admission ||
        summary.producer!=out->protocol.producer ||
        summary.consumer!=out->protocol.consumer ||
        summary.final_is_current!=out->protocol.final_is_current ||
        summary.retirements!=out->protocol.retirements ||
        summary.source_work!=out->source_work ||
        summary.returns.preparation!=out->returns.preparation ||
        summary.returns.transport!=out->returns.transport ||
        builder.issued_total!=summary.total))) return DURABLE_CORRUPT;
    if (report_setup && exact) d->evaluation->setup_reported=true;
    return DURABLE_OK;
}
void cetta_host_plan_cancel(CettaHostDecision *d) {
    if (!d || !d->evaluation || !d->evaluation->retained || d->evaluation->cancelled) return;
    (void)cetta_host_outcome(d);
    host_return_copy_clear(d->evaluation,true);
    cetta_owned_task_cancel(&d->planner);
    d->evaluation->cancelled=true;
    d->evaluation->outcome.completion=CETTA_EVAL_INCOMPLETE_CANCELLED;
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
