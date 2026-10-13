#define _POSIX_C_SOURCE 200809L
#include "telegram_agent.h"
#include "telegram_control.h"
#include "durable_eval.h"
#include "durable_inbox.h"
#include "durable_worker_host.h"
#include "durable_value.h"
#include "durable_timer.h"
#include "library.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *text(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool component(const char *s) {
    size_t n=s?strnlen(s,65):0;
    if (!n || n>64) return false;
    for (size_t i=0;i<n;++i) if (!((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') ||
        (s[i]>='0' && s[i]<='9') || s[i]=='.' || s[i]=='-' || s[i]=='_')) return false;
    return true;
}
static bool form(const Atom *p, const char *head, size_t n) {
    return p && p->kind==ATOM_EXPR && p->expr.len==n && atom_is_symbol(p->expr.elems[0],head) &&
        p->expr.elems[1]->kind==ATOM_GROUNDED && p->expr.elems[1]->ground.gkind==GV_INT && p->expr.elems[1]->ground.ival==1;
}
static bool same(const CettaDurableSnapshot *a, const CettaDurableSnapshot *b) {
    if (!a || !b || a->count!=1 || b->count!=1 || strcmp(a->epoch,b->epoch)) return false;
    const CettaDurableRecord *x=a->records,*y=b->records;
    return x->revision==y->revision && x->position==y->position && x->size==y->size &&
        !strcmp(x->key,y->key) && !memcmp(x->data,y->data,x->size);
}
static CettaDurableStatus read(CettaDurableStore *store, const char *space, const char *key,
                              Arena *a, CettaDurableObservation **o, Atom **value) {
    if (!key || !*key || strnlen(key,256)>255) return DURABLE_INVALID;
    CettaDurableScope scope={DURABLE_KEY,space,key};
    CettaDurableStatus s=cetta_durable_observe(store,&scope,1,o); if (s!=DURABLE_OK) return s;
    const CettaDurableSnapshot *v=cetta_durable_observation_view(*o,0);
    if (v->count!=1) return DURABLE_PRECONDITION;
    return cetta_durable_value_decode(a,v->records[0].data,v->records[0].size,value);
}
/* Admission permissions are native, immutable capabilities. A syntactically
 * valid batch rejected by that authority follows a fixed policy branch in
 * the SAME ticket. Never replace an incomplete/denied evaluation. Malformed
 * policy output still fails acceptance rather than becoming a rejection. */
static bool forbidden_batch(const CettaHostDecision *d, const CettaTelegramActionPolicy *policy) {
    const EvalOutcome *o=cetta_host_outcome(d);
    if (!o || o->completion!=CETTA_EVAL_COMPLETE || o->effect_denials || o->results.len!=1) return false;
    const Atom *p=o->results.items[0];
    if (p->kind!=ATOM_EXPR || p->expr.len!=4 || !atom_is_symbol(p->expr.elems[0],"host:transition")) return false;
    const Atom *effects=p->expr.elems[3];
    if (effects->kind!=ATOM_EXPR || effects->expr.len>64) return false;
    bool forbidden=false;
    for (CettaCount i=0;i<effects->expr.len;++i) {
        const Atom *e=effects->expr.elems[i];
        if (e->kind!=ATOM_EXPR || e->expr.len!=4 || !atom_is_symbol(e->expr.elems[0],"host:send") ||
            e->expr.elems[1]->kind!=ATOM_GROUNDED || e->expr.elems[1]->ground.gkind!=GV_INT || e->expr.elems[1]->ground.ival!=0)
            return false;
        if (!cetta_telegram_action_validate((void *)policy,e->expr.elems[2],e->expr.elems[3])) forbidden=true;
    }
    return forbidden;
}
bool cetta_telegram_agent_valid(const CettaTelegramAgent *c) {
    return c && c->program && c->program->version &&
        (!strcmp(c->program->version,"telegram-agent/1") || !strcmp(c->program->version,"telegram-channel/1")) &&
        c->program->context && c->program->space && component(c->source) && component(c->worker) &&
        c->actions && c->actions->chats && c->actions->chat_count && c->actions->chat_count<=128 &&
        !(c->actions->methods&~15u) && c->fuel>0 && c->fuel<=2000000 &&
        c->program->context->session.language_id==CETTA_LANGUAGE_HE && c->program->context->session.profile &&
        c->program->context->session.profile->id==CETTA_PROFILE_HE_EXTENDED;
}
static bool channel_mode(const CettaTelegramAgent *c) {
    return !strcmp(c->program->version,"telegram-channel/1");
}
/* Canonical CHAT.THREAD, as the intake policy writes lanes. */
static bool lane_numbers(const char *lane, int64_t *chat, int64_t *thread) {
    if (!component(lane)) return false;
    char *end; errno=0; *chat=strtoll(lane,&end,10);
    if (errno || end==lane || *end!='.' || !*chat) return false;
    const char *part=end+1; errno=0; *thread=strtoll(part,&end,10);
    if (errno || end==part || *end || *thread<0) return false;
    char canonical[64]; snprintf(canonical,sizeof(canonical),"%" PRId64 ".%" PRId64,*chat,*thread);
    return !strcmp(lane,canonical);
}
/* A submission key is LANE.SEQUENCE with a fixed 20-digit sequence, so key
 * order within a lane is submission order. */
static bool submission_lane(const char *key, char lane[65], int64_t *chat, int64_t *thread) {
    size_t n=key?strnlen(key,65):0;
    if (n<22 || n>64 || key[n-21]!='.') return false;
    for (size_t i=n-20;i<n;++i) if (key[i]<'0' || key[i]>'9') return false;
    memcpy(lane,key,n-21); lane[n-21]=0;
    return lane_numbers(lane,chat,thread);
}
/* An operator command is keyed by its update ID. */
static bool command_key(const char *key) {
    size_t n=key?strnlen(key,21):0;
    if (!n || n>20) return false;
    for (size_t i=0;i<n;++i) if (key[i]<'0' || key[i]>'9') return false;
    return true;
}
/* telegram-channel/1: the client reads deliveries and receipts as tasks and
 * acts only through keyed submissions. The sending state machine and its
 * admission are telegram-agent/1's; see cetta_telegram_agent_admit. */
static CettaDurableStatus channel_decide(CettaDurableStore *store,
        const CettaTelegramAgent *c, const char *input_key, CettaHostDecision **out) {
    Arena a; arena_init(&a);
    CettaDurableObservation *initial=NULL, *origin=NULL, *intent=NULL;
    Atom *input=NULL, *prior=NULL, *request=NULL, *call=NULL;
    CettaHostDecision *decision=NULL;
    CettaDurableStatus s=read(store,"host.inbox",input_key,&a,&initial,&input);
    if (s!=DURABLE_OK) goto done;
    char actor[160], ledger[96], expected[168], task[130], pending[160], lane[65], command[32];
    CettaHostSpaceGrant scopes[4]; size_t count=0;
    CettaHostChannelGrant channels[3]; size_t channel_count=0;
    int64_t submit_chat=0, submit_thread=0;
    const CettaHostChannelGrant worker={"worker.request","1",cetta_worker_validate,(void *)c->worker};
    const CettaHostChannelGrant actions={"telegram.action","1",cetta_telegram_action_validate,(void *)c->actions};
    const CettaHostChannelGrant timer={"timer.after","1",cetta_timer_validate,NULL};
    bool submission=false, oldest_needed=false;
    if (form(input,"host:input",7)) {
        Atom **v=input->expr.elems;
        if (!text(v[2]) || strcmp(text(v[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        const char *routed_lane=text(v[5]);
        if (!component(routed_lane) || v[3]->kind!=ATOM_GROUNDED || v[3]->ground.gkind!=GV_INT || !text(v[4]) ||
            cetta_inbox_keys(c->source,routed_lane,v[3]->ground.ival,ledger,expected)!=DURABLE_OK ||
            strcmp(expected,input_key) || strcmp(ledger,text(v[4]))) { s=DURABLE_CORRUPT; goto done; }
        bool routed=atom_is_symbol(v[6],"routed");
        if (!routed && !atom_is_symbol(v[6],"unauthorized") && !atom_is_symbol(v[6],"unsupported")) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,routed?"intake":"ignored");
        /* An operator command is decided here, on its update: its record,
         * the lifecycle latch, its reply and its deadline. The clock sample
         * is recorded in the command's record and its timer. */
        snprintf(command,sizeof(command),"%" PRId64,v[3]->ground.ival);
        CettaClockSample now; if (!cetta_clock_sample(&now)) { s=DURABLE_IO; goto done; }
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.received",ledger},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.commands",command},true};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.lifecycle","latch"},true};
        channels[channel_count++]=worker; channels[channel_count++]=actions; channels[channel_count++]=timer;
        Atom *args[]={atom_symbol(&a,"tg-channel:receive"),atom_string(&a,c->worker),atom_string(&a,c->source),
            atom_string(&a,routed_lane),atom_int(&a,now.utc_ms),
            atom_int(&a,c->command_deadline_ms?c->command_deadline_ms:2000)};
        call=atom_expr(&a,args,6);
    } else if (form(input,"host:worker-result",6)) {
        if (!text(input->expr.elems[2]) || strcmp(text(input->expr.elems[2]),c->worker)) { s=DURABLE_PRECONDITION; goto done; }
        const char *id=text(input->expr.elems[3]);
        if (!component(id)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(task,sizeof(task),"%s/%s",c->worker,id);
        snprintf(expected,sizeof(expected),"worker/%s",task);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        s=read(store,"host.worker-origins",task,&a,&origin,&prior); if (s!=DURABLE_OK) goto done;
        if (!form(prior,"host:worker-origin",6) || !text(prior->expr.elems[2]) ||
            strcmp(text(prior->expr.elems[2]),cetta_durable_observation_view(initial,0)->epoch)) { s=DURABLE_CORRUPT; goto done; }
        const char *outkey=text(prior->expr.elems[3]);
        s=read(store,"host.outbox",outkey,&a,&intent,&request); if (s!=DURABLE_OK) goto done;
        if (!form(request,"host:intent",6)) { s=DURABLE_VERSION; goto done; }
        Atom *reply=request->expr.elems[5];
        bool asked=form(reply,"tg-channel:asked",4);
        if (!text(request->expr.elems[2]) || strcmp(text(request->expr.elems[2]),"worker.request") ||
            !text(request->expr.elems[3]) || strcmp(text(request->expr.elems[3]),"1") ||
            !(asked || form(reply,"tg-channel:delivered",5) || form(reply,"tg-channel:notified",4))) { s=DURABLE_VERSION; goto done; }
        if (!text(reply->expr.elems[2]) || strcmp(text(reply->expr.elems[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-tasks",task},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-origins",task},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
        if (asked) {
            /* The agent's answer to a command races its deadline on the
             * command's own actor and record, never on a chat lane. */
            const char *key=text(reply->expr.elems[3]);
            if (!command_key(key)) { s=DURABLE_CORRUPT; goto done; }
            snprintf(command,sizeof(command),"%s",key);
            snprintf(actor,sizeof(actor),"telegram/%s/command/%s",c->source,command);
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.commands",command},true};
            channels[channel_count++]=actions;
            call=atom_expr2(&a,atom_symbol(&a,"tg-channel:answer"),atom_string(&a,c->worker));
        } else {
            snprintf(actor,sizeof(actor),"telegram/%s/intake",c->source);
            call=atom_expr2(&a,atom_symbol(&a,"tg-channel:ack"),atom_string(&a,c->worker));
        }
    } else if (form(input,"host:worker-submission",5)) {
        Atom **v=input->expr.elems; int64_t chat=0, thread=0;
        const char *key=text(v[3]);
        if (!text(v[2]) || strcmp(text(v[2]),c->worker)) { s=DURABLE_PRECONDITION; goto done; }
        if (!component(key) || !text(v[4])) { s=DURABLE_CORRUPT; goto done; }
        snprintf(expected,sizeof(expected),"submission/%s/%s",c->worker,key);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        submission=true;
        channels[channel_count++]=actions; channels[channel_count++]=worker;
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.decisions",key},true};
        if (submission_lane(key,lane,&chat,&thread)) {
            snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,lane);
            snprintf(pending,sizeof(pending),"submission/%s/%s.",c->worker,lane);
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_PREFIX,"host.inbox",pending},false};
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.lifecycle","latch"},false};
            oldest_needed=true; submit_chat=chat; submit_thread=thread;
        } else {
            /* No lane can be named: reject without touching any chat actor. */
            snprintf(actor,sizeof(actor),"telegram/%s/ignored",c->source);
            Atom *args[]={atom_symbol(&a,"tg-channel:reject"),atom_string(&a,c->worker),atom_string(&a,c->source),
                atom_string(&a,key),atom_symbol(&a,"malformed-key")};
            call=atom_expr(&a,args,5);
        }
    } else if (form(input,"host:telegram-control",7)) {
        /* An operator decision about a lane whose last delivery is uncertain.
         * Separately authorized; the reaction may only notify the client. */
        Atom **v=input->expr.elems;
        const char *id=text(v[3]), *control_lane=text(v[4]), *batch=text(v[6]);
        if (!text(v[2]) || strcmp(text(v[2]),c->source) || !component(id) || !component(batch) ||
            !cetta_telegram_control_lane(c,control_lane) || !text(v[5]) || strcmp(text(v[5]),"release-worker")) {
            s=DURABLE_PRECONDITION; goto done;
        }
        snprintf(expected,sizeof(expected),"control/%s",id);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,control_lane);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.controls",id},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.control-results",id},true};
        channels[channel_count++]=worker;
        call=atom_expr2(&a,atom_symbol(&a,"tg-channel:control"),atom_string(&a,c->worker));
    } else if (form(input,"host:timer-event",10)) {
        /* A command's deadline. The reply names the command; the command's
         * own record decides whether a notice is still due. */
        Atom *reply=input->expr.elems[9];
        if (!form(reply,"tg-channel:deadline",4) || !text(reply->expr.elems[2]) ||
            strcmp(text(reply->expr.elems[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        const char *key=text(reply->expr.elems[3]);
        if (!command_key(key)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(command,sizeof(command),"%s",key);
        snprintf(actor,sizeof(actor),"telegram/%s/command/%s",c->source,command);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.commands",command},true};
        channels[channel_count++]=actions;
        Atom *args[]={atom_symbol(&a,"tg-channel:deadline"),atom_string(&a,c->source),atom_string(&a,command)};
        call=atom_expr(&a,args,3);
    } else if (form(input,"host:completion",4)) {
        const char *outkey=text(input->expr.elems[3]);
        s=read(store,"host.outbox",outkey,&a,&intent,&request); if (s!=DURABLE_OK) goto done;
        if (!form(request,"host:intent",6) || !text(request->expr.elems[2]) || strcmp(text(request->expr.elems[2]),"telegram.action") ||
            !text(request->expr.elems[3]) || strcmp(text(request->expr.elems[3]),"1")) { s=DURABLE_PRECONDITION; goto done; }
        Atom *sent=request->expr.elems[5];
        if (form(sent,"tg-channel:command-sent",5)) {
            if (!text(sent->expr.elems[2]) || strcmp(text(sent->expr.elems[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
            const char *key=text(sent->expr.elems[3]);
            if (!command_key(key)) { s=DURABLE_CORRUPT; goto done; }
            snprintf(command,sizeof(command),"%s",key);
            snprintf(actor,sizeof(actor),"telegram/%s/command/%s",c->source,command);
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outcomes",outkey},false};
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.commands",command},true};
            channels[channel_count++]=actions;
            Atom *head=atom_symbol(&a,"tg-channel:command-complete"); call=atom_expr(&a,&head,1);
        } else {
            if (!form(sent,"tg-agent:sent",7)) { s=DURABLE_PRECONDITION; goto done; }
            Atom **reply=sent->expr.elems;
            if (!text(reply[2]) || strcmp(text(reply[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
            const char *sent_lane=text(reply[3]); if (!component(sent_lane)) { s=DURABLE_CORRUPT; goto done; }
            snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,sent_lane);
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outcomes",outkey},false};
            scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.deliveries",outkey},true};
            channels[channel_count++]=worker;
            call=atom_expr2(&a,atom_symbol(&a,"tg-channel:complete"),atom_string(&a,c->worker));
        }
    } else { s=DURABLE_PRECONDITION; goto done; }
    CettaHostDecisionSpec spec={input_key,actor,c->program->version,scopes,count,channels,channel_count};
    s=cetta_host_begin(store,&spec,&decision); if (s!=DURABLE_OK) goto done;
    if (!same(cetta_durable_observation_view(initial,0),cetta_host_view(decision,0)) ||
        (origin && !same(cetta_durable_observation_view(origin,0),cetta_host_view(decision,3))) ||
        (intent && !same(cetta_durable_observation_view(intent,0),cetta_host_view(decision,origin?4:2)))) {
        s=DURABLE_CONFLICT; goto done;
    }
    if (oldest_needed) {
        /* Computed from this ticket's own snapshot, which the commit validates:
         * an older pending submission for the lane makes this one wait. */
        const CettaDurableSnapshot *lane_inputs=cetta_host_view(decision,3);
        bool oldest=true;
        for (size_t i=0;i<lane_inputs->count;++i)
            if (strcmp(lane_inputs->records[i].key,input_key)<0) { oldest=false; break; }
        Atom *args[]={atom_symbol(&a,"tg-channel:submit"),atom_string(&a,c->worker),atom_string(&a,c->source),
            atom_string(&a,lane),atom_int(&a,submit_chat),atom_int(&a,submit_thread),
            atom_symbol(&a,oldest?"True":"False")};
        call=atom_expr(&a,args,7);
    }
    call=atom_expr2(&a,atom_symbol(&a,"tg-agent:reaction"),call);
    s=cetta_host_evaluate(decision,c->program,call,c->fuel);
    if (s==DURABLE_OK && submission && oldest_needed && forbidden_batch(decision,c->actions)) {
        Atom *args[]={atom_symbol(&a,"tg-channel:reject-invalid-submission"),atom_string(&a,c->worker),atom_string(&a,c->source)};
        call=atom_expr2(&a,atom_symbol(&a,"tg-agent:reaction"),atom_expr(&a,args,3));
        s=cetta_host_evaluate(decision,c->program,call,c->fuel);
    }
    if (s==DURABLE_OK) { *out=decision; decision=NULL; }
done:
    cetta_host_decision_free(decision);
    cetta_durable_observation_free(initial); cetta_durable_observation_free(origin); cetta_durable_observation_free(intent);
    arena_free(&a); return s;
}
CettaDurableStatus cetta_telegram_agent_decide(CettaDurableStore *store,
        const CettaTelegramAgent *c, const char *input_key, CettaHostDecision **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !cetta_telegram_agent_valid(c)) return DURABLE_INVALID;
    if (channel_mode(c)) return channel_decide(store,c,input_key,out);
    Arena a; arena_init(&a);
    CettaDurableObservation *initial=NULL, *origin=NULL, *intent=NULL;
    Atom *input=NULL, *prior=NULL, *request=NULL, *call=NULL;
    CettaHostDecision *decision=NULL;
    CettaDurableStatus s=read(store,"host.inbox",input_key,&a,&initial,&input);
    if (s!=DURABLE_OK) goto done;
    const char *lane=NULL,*outkey=NULL,*batch=NULL;
    char actor[160], prefix[140], ledger[96], expected[168], task[130];
    CettaHostSpaceGrant scopes[5]; size_t count=0;
    CettaHostChannelGrant channel={0}; size_t channels=0;
    if (form(input,"host:input",7)) {
        Atom **v=input->expr.elems;
        if (!text(v[2]) || strcmp(text(v[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        lane=text(v[5]);
        if (!component(lane) || v[3]->kind!=ATOM_GROUNDED || v[3]->ground.gkind!=GV_INT || !text(v[4]) ||
            cetta_inbox_keys(c->source,lane,v[3]->ground.ival,ledger,expected)!=DURABLE_OK ||
            strcmp(expected,input_key) || strcmp(ledger,text(v[4]))) { s=DURABLE_CORRUPT; goto done; }
        bool routed=atom_is_symbol(v[6],"routed");
        if (!routed && !atom_is_symbol(v[6],"unauthorized") && !atom_is_symbol(v[6],"unsupported")) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,routed?lane:"ignored");
        snprintf(prefix,sizeof(prefix),"%s/%s/",c->source,lane);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.received",ledger},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_PREFIX,"host.inbox",prefix},false};
        channel=(CettaHostChannelGrant){"worker.request","1",cetta_worker_validate,(void *)c->worker}; channels=1;
        Atom *args[]={atom_symbol(&a,"tg-agent:receive"),atom_string(&a,c->worker),atom_string(&a,c->source),atom_string(&a,lane)};
        call=atom_expr(&a,args,4);
    } else if (form(input,"host:telegram-control",7)) {
        Atom **v=input->expr.elems;
        const char *id=text(v[3]); lane=text(v[4]); batch=text(v[6]);
        if (!text(v[2]) || strcmp(text(v[2]),c->source) || !component(id) || !component(batch) ||
            !cetta_telegram_control_lane(c,lane) || !text(v[5]) || strcmp(text(v[5]),"release-worker")) {
            s=DURABLE_PRECONDITION; goto done;
        }
        snprintf(expected,sizeof(expected),"control/%s",id);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,lane);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.controls",id},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.control-results",id},true};
        Atom *head=atom_symbol(&a,"tg-agent:control"); call=atom_expr(&a,&head,1);
    } else if (form(input,"host:worker-result",6)) {
        if (!text(input->expr.elems[2]) || strcmp(text(input->expr.elems[2]),c->worker)) { s=DURABLE_PRECONDITION; goto done; }
        batch=text(input->expr.elems[3]);
        if (!component(batch)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(task,sizeof(task),"%s/%s",c->worker,batch);
        snprintf(expected,sizeof(expected),"worker/%s",task);
        if (strcmp(expected,input_key)) { s=DURABLE_CORRUPT; goto done; }
        s=read(store,"host.worker-origins",task,&a,&origin,&prior); if (s!=DURABLE_OK) goto done;
        if (!form(prior,"host:worker-origin",6) || !text(prior->expr.elems[2]) ||
            strcmp(text(prior->expr.elems[2]),cetta_durable_observation_view(initial,0)->epoch)) { s=DURABLE_CORRUPT; goto done; }
        outkey=text(prior->expr.elems[3]);
        s=read(store,"host.outbox",outkey,&a,&intent,&request); if (s!=DURABLE_OK) goto done;
        if (!form(request,"host:intent",6) || !text(request->expr.elems[2]) || strcmp(text(request->expr.elems[2]),"worker.request") ||
            !text(request->expr.elems[3]) || strcmp(text(request->expr.elems[3]),"1") ||
            !form(request->expr.elems[5],"tg-agent:return",7)) { s=DURABLE_VERSION; goto done; }
        Atom **reply=request->expr.elems[5]->expr.elems;
        if (!text(reply[2]) || strcmp(text(reply[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        lane=text(reply[3]); if (!component(lane)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,lane);
        snprintf(prefix,sizeof(prefix),"%s/%s/",c->source,lane);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-tasks",task},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.worker-origins",task},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_PREFIX,"host.inbox",prefix},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.decisions",batch},true};
        channel=(CettaHostChannelGrant){"telegram.action","1",cetta_telegram_action_validate,(void *)c->actions}; channels=1;
        call=atom_expr2(&a,atom_symbol(&a,"tg-agent:reply"),atom_string(&a,c->worker));
    } else if (form(input,"host:completion",4)) {
        outkey=text(input->expr.elems[3]);
        s=read(store,"host.outbox",outkey,&a,&intent,&request); if (s!=DURABLE_OK) goto done;
        if (!form(request,"host:intent",6) || !text(request->expr.elems[2]) || strcmp(text(request->expr.elems[2]),"telegram.action") ||
            !text(request->expr.elems[3]) || strcmp(text(request->expr.elems[3]),"1") ||
            !form(request->expr.elems[5],"tg-agent:sent",7)) { s=DURABLE_PRECONDITION; goto done; }
        Atom **reply=request->expr.elems[5]->expr.elems;
        if (!text(reply[2]) || strcmp(text(reply[2]),c->source)) { s=DURABLE_PRECONDITION; goto done; }
        lane=text(reply[3]); if (!component(lane)) { s=DURABLE_CORRUPT; goto done; }
        snprintf(actor,sizeof(actor),"telegram/%s/%s",c->source,lane);
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outbox",outkey},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"host.outcomes",outkey},false};
        scopes[count++]=(CettaHostSpaceGrant){{DURABLE_KEY,"telegram.deliveries",outkey},true};
        Atom *head=atom_symbol(&a,"tg-agent:complete"); call=atom_expr(&a,&head,1);
    } else { s=DURABLE_PRECONDITION; goto done; }
    CettaHostDecisionSpec spec={input_key,actor,c->program->version,scopes,count,&channel,channels};
    s=cetta_host_begin(store,&spec,&decision); if (s!=DURABLE_OK) goto done;
    if (!same(cetta_durable_observation_view(initial,0),cetta_host_view(decision,0)) ||
        (origin && !same(cetta_durable_observation_view(origin,0),cetta_host_view(decision,3))) ||
        (intent && !same(cetta_durable_observation_view(intent,0),cetta_host_view(decision,origin?4:2)))) {
        s=DURABLE_CONFLICT; goto done;
    }
    call=atom_expr2(&a,atom_symbol(&a,"tg-agent:reaction"),call);
    s=cetta_host_evaluate(decision,c->program,call,c->fuel);
    if (s==DURABLE_OK && origin && forbidden_batch(decision,c->actions)) {
        Atom *head=atom_symbol(&a,"tg-agent:reject-invalid-batch");
        call=atom_expr2(&a,atom_symbol(&a,"tg-agent:reaction"),atom_expr(&a,&head,1));
        s=cetta_host_evaluate(decision,c->program,call,c->fuel);
    }
    if (s==DURABLE_OK) { *out=decision; decision=NULL; }
done:
    cetta_host_decision_free(decision);
    cetta_durable_observation_free(initial); cetta_durable_observation_free(origin); cetta_durable_observation_free(intent);
    arena_free(&a); return s;
}

CettaDurableStatus cetta_telegram_agent_admit(CettaDurableStore *store,
        const CettaTelegramAgent *c, const char *key, size_t budget,
        CettaTelegramAdmission *admission, CettaDurableWatch **watch) {
    if (!admission || !watch) return DURABLE_INVALID;
    *admission=TELEGRAM_WAIT; *watch=NULL;
    if (!store || !cetta_telegram_agent_valid(c)) return DURABLE_INVALID;
    Arena a; arena_init(&a); Atom *intent=NULL;
    CettaDurableObservation *initial=NULL, *o=NULL; CettaHostEvaluation *e=NULL;
    CettaDurableStatus s=read(store,"host.outbox",key,&a,&initial,&intent);
    if (s!=DURABLE_OK) goto done;
    if (!form(intent,"host:intent",6) || !text(intent->expr.elems[2]) || !text(intent->expr.elems[3])) {
        s=DURABLE_CORRUPT; goto done;
    }
    const char *channel=text(intent->expr.elems[2]), *version=text(intent->expr.elems[3]);
    Atom *payload=intent->expr.elems[4], *reply=intent->expr.elems[5];
    if (!strcmp(channel,"worker.request")) {
        bool ours=channel_mode(c)
            ? form(reply,"tg-channel:delivered",5) || form(reply,"tg-channel:notified",4) || form(reply,"tg-channel:asked",4)
            : form(reply,"tg-agent:return",7);
        if (!ours || !text(reply->expr.elems[2]) || strcmp(text(reply->expr.elems[2]),c->source)) {
            *admission=TELEGRAM_FOREIGN; goto done;
        }
        if (strcmp(version,"1")) { s=DURABLE_VERSION; goto done; }
        if (!cetta_worker_validate((void *)c->worker,payload,reply)) { s=DURABLE_INVALID; goto done; }
        *admission=TELEGRAM_WORKER; goto done;
    }
    if (strcmp(channel,"telegram.action")) { *admission=TELEGRAM_FOREIGN; goto done; }
    /* A command's own message is never ordered behind a chat lane or held
     * by the lifecycle latch; a lane's actions are, and wait while stopped. */
    bool command_send=channel_mode(c) && form(reply,"tg-channel:command-sent",5);
    if (strcmp(version,"1") || (!command_send && !form(reply,"tg-agent:sent",7)) || !text(reply->expr.elems[2]) ||
        strcmp(text(reply->expr.elems[2]),c->source) || !component(text(reply->expr.elems[3])) ||
        (command_send && !command_key(text(reply->expr.elems[3])))) {
        s=DURABLE_VERSION; goto done;
    }
    char actor[160]; snprintf(actor,sizeof(actor),"telegram/%s/%s%s",c->source,command_send?"command/":"",
        text(reply->expr.elems[3]));
    CettaDurableScope q[]={{DURABLE_KEY,"host.outbox",key},{DURABLE_KEY,"host.actors",actor},
        {DURABLE_KEY,"host.attempts",key},{DURABLE_KEY,"host.outcomes",key},
        {DURABLE_KEY,"telegram.lifecycle","latch"}};
    size_t views=channel_mode(c) && !command_send?5:4;
    s=cetta_durable_observe(store,q,views,&o); if (s!=DURABLE_OK) goto done;
    if (!same(cetta_durable_observation_view(initial,0),cetta_durable_observation_view(o,0))) { s=DURABLE_CONFLICT; goto done; }
    if (cetta_durable_observation_view(o,3)->count) { *admission=TELEGRAM_DONE; goto done; }
    s=cetta_durable_watch_new(o,budget,watch); if (s!=DURABLE_OK) goto done;
    if (cetta_durable_observation_view(o,2)->count) goto done;
    if (!cetta_telegram_action_validate((void *)c->actions,payload,reply)) { s=DURABLE_INVALID; goto done; }
    if (command_send) { *admission=TELEGRAM_SEND; goto done; }
    s=cetta_host_eval_create(c->program,&e); if (s!=DURABLE_OK) goto done;
    /* Records 0 = intent, 1 = actor and, for the channel, 2 = latch. */
    static const size_t record_view[]={0,1,4};
    for (size_t i=0;i<(views==5?3u:2u);++i) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(o,record_view[i]);
        if (!v->count) continue;
        Atom *value=NULL; s=cetta_durable_value_decode(&e->persistent,v->records[0].data,v->records[0].size,&value);
        if (s!=DURABLE_OK) goto done;
        Atom *fact[]={atom_symbol(&e->persistent,"host:record"),atom_int(&e->persistent,(int64_t)i),
            atom_string(&e->persistent,v->records[0].key),value};
        space_add(&e->space,atom_expr(&e->persistent,fact,4));
    }
    Atom *head=atom_symbol(&a,channel_mode(c)?"tg-channel:dispatchable":"tg-agent:dispatchable");
    s=cetta_host_eval_run(e,c->program,atom_expr(&a,&head,1),c->fuel); if (s!=DURABLE_OK) goto done;
    const EvalOutcome *out=&e->outcome;
    if (out->completion!=CETTA_EVAL_COMPLETE || out->effect_denials || out->results.len!=1 ||
        out->results.items[0]->kind!=ATOM_GROUNDED || out->results.items[0]->ground.gkind!=GV_BOOL) {
        s=DURABLE_PRECONDITION; goto done;
    }
    if (out->results.items[0]->ground.bval) *admission=TELEGRAM_SEND;
done:
    cetta_host_eval_free(e); cetta_durable_observation_free(o); cetta_durable_observation_free(initial);
    arena_free(&a); return s;
}
