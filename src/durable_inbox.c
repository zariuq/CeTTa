#define _POSIX_C_SOURCE 200809L
#include "durable_inbox.h"
#include "durable_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INBOX_BATCH 100
#define INBOX_BYTES (4u*1024u*1024u)
struct CettaInboxWindow {
    CettaDurableStore *store;
    CettaDurableObservation *cursor;
    char source[65];
    int64_t offset, failures, clock_ms;
    bool spent, pending_poll, recovery_recorded;
};
static bool component(const char *s) {
    if (!s || !*s || strnlen(s,65)>64) return false;
    for (;*s;++s)
        if (!((*s>='A'&&*s<='Z') || (*s>='a'&&*s<='z') || (*s>='0'&&*s<='9') ||
              *s=='.' || *s=='_' || *s=='-')) return false;
    return true;
}
CettaDurableStatus cetta_inbox_keys(const char *source, const char *lane, int64_t id,
        char ledger[96], char input[168]) {
    if (!ledger || !input || !component(source) || !component(lane) || id<0 || id==INT64_MAX) return DURABLE_INVALID;
    snprintf(ledger,96,"%s/%lld",source,(long long)id);
    snprintf(input,168,"%s/%s/%lld",source,lane,(long long)id); return DURABLE_OK;
}
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static bool text_is(const Atom *a, const char *s) {
    return a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING && !strcmp(a->ground.sval,s);
}
static bool number(const Atom *a, int64_t *n) {
    if (a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *n=a->ground.ival; return true;
}
static bool recovery_decision(const Atom *a, int64_t *due) {
    int64_t schema;
    bool retry=form(a,"telegram:retry-at",4);
    *due=-1;
    return (retry || form(a,"telegram:hold",3)) && number(a->expr.elems[1],&schema) && schema==1 &&
        (!retry || (number(a->expr.elems[2],due) && *due>=0));
}
static bool program_name(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING &&
        *a->ground.sval && strnlen(a->ground.sval,256)<=255;
}
static const CettaDurableSnapshot *view(const CettaDurableObservation *o, size_t i) {
    return cetta_durable_observation_view(o,i);
}
static CettaDurableStatus begin(CettaDurableStore *store, const char *source,
                               bool pending, int64_t now, CettaInboxWindow **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!store || !component(source)) return DURABLE_INVALID;
    CettaInboxWindow *w=calloc(1,sizeof(*w)); if (!w) return DURABLE_NOMEM;
    w->store=store; w->pending_poll=pending; w->clock_ms=-1; strcpy(w->source,source);
    CettaDurableScope q[]={{DURABLE_KEY,"host.cursors",source},{DURABLE_KEY,"host.polls",source},
        {DURABLE_KEY,"host.poll-control",source},{DURABLE_KEY,"host.poll-clock",source}};
    CettaDurableStatus s=cetta_durable_observe(store,q,4,&w->cursor);
    if (s!=DURABLE_OK) goto fail;
    if ((view(w->cursor,1)->count!=0)!=pending) { s=DURABLE_PRECONDITION; goto fail; }
    const CettaDurableSnapshot *v=view(w->cursor,0);
    if (v->count) {
        Arena a; arena_init(&a); Atom *p=NULL; int64_t schema=0, offset=0;
        s=cetta_durable_value_decode(&a,v->records[0].data,v->records[0].size,&p);
        if (s==DURABLE_OK && (!form(p,"host:cursor",4) || !number(p->expr.elems[1],&schema) ||
            schema!=1 || !text_is(p->expr.elems[2],source) || !number(p->expr.elems[3],&offset) || offset<0)) s=DURABLE_CORRUPT;
        if (s==DURABLE_OK) w->offset=offset;
        arena_free(&a); if (s!=DURABLE_OK) goto fail;
    }
    const CettaDurableSnapshot *control=view(w->cursor,2), *clock=view(w->cursor,3);
    Arena a; arena_init(&a); Atom *p=NULL; int64_t schema, offset, poll_revision, at, due, decided_due;
    if (control->count) {
        s=cetta_durable_value_decode(&a,control->records[0].data,control->records[0].size,&p);
        if (s==DURABLE_OK && (!form(p,"host:poll-control",11) || !number(p->expr.elems[1],&schema) || schema!=1 ||
            !text_is(p->expr.elems[2],source) || !number(p->expr.elems[3],&offset) || offset!=w->offset ||
            !number(p->expr.elems[4],&poll_revision) || poll_revision<0 ||
            !number(p->expr.elems[5],&w->failures) || w->failures<=0 ||
            !number(p->expr.elems[6],&at) || at<0 || !number(p->expr.elems[7],&due) || due < -1 ||
            (due>=0 && due<=at) || !program_name(p->expr.elems[8]) ||
            !recovery_decision(p->expr.elems[9],&decided_due) || decided_due!=due)) s=DURABLE_CORRUPT;
        if (s==DURABLE_OK) {
            w->recovery_recorded=pending && poll_revision==view(w->cursor,1)->records[0].revision;
            if (!pending && (due<0 || now<due)) s=DURABLE_PRECONDITION;
        }
    }
    if (s==DURABLE_OK && clock->count) {
        s=cetta_durable_value_decode(&a,clock->records[0].data,clock->records[0].size,&p);
        if (s==DURABLE_OK && (!pending || !form(p,"host:poll-clock",5) ||
            !number(p->expr.elems[1],&schema) || schema!=1 || !text_is(p->expr.elems[2],source) ||
            !number(p->expr.elems[3],&poll_revision) || poll_revision!=view(w->cursor,1)->records[0].revision ||
            !number(p->expr.elems[4],&w->clock_ms) || w->clock_ms<0)) s=DURABLE_CORRUPT;
    }
    arena_free(&a); if (s!=DURABLE_OK) goto fail;
    *out=w; return DURABLE_OK;
fail:
    cetta_inbox_window_free(w); return s;
}
CettaDurableStatus cetta_inbox_begin(CettaDurableStore *store, const char *source, CettaInboxWindow **out) {
    return begin(store,source,false,-1,out);
}
CettaDurableStatus cetta_inbox_recover_poll(CettaDurableStore *store, const char *source, CettaInboxWindow **out) {
    return begin(store,source,true,-1,out);
}
CettaDurableStatus cetta_inbox_begin_at(CettaDurableStore *store, const char *source,
                                      int64_t now, CettaInboxWindow **out) {
    if (now<0) { if (out) *out=NULL; return DURABLE_INVALID; }
    return begin(store,source,false,now,out);
}
int64_t cetta_inbox_poll_failures(const CettaInboxWindow *w) { return w?w->failures:-1; }
bool cetta_inbox_poll_held(const CettaInboxWindow *w) { return w && w->pending_poll && w->recovery_recorded; }
int64_t cetta_inbox_offset(const CettaInboxWindow *w) { return w?w->offset:-1; }
const char *cetta_inbox_source(const CettaInboxWindow *w) { return w?w->source:NULL; }
const CettaDurableRecord *cetta_inbox_poll_response(const CettaInboxWindow *w) {
    return w && w->pending_poll?&view(w->cursor,1)->records[0]:NULL;
}
CettaDurableStatus cetta_inbox_record_poll(CettaInboxWindow *w, const void *data, size_t size) {
    if (!w || !data || !size || w->pending_poll) return DURABLE_INVALID;
    if (w->spent) return DURABLE_CONFLICT;
    CettaDurableOp op={DURABLE_INSERT,"host.polls",w->source,data,size}; int64_t revision;
    CettaDurableStatus s=cetta_durable_commit_observed(w->store,w->cursor,&op,1,&revision);
    if (s==DURABLE_OK) w->spent=true;
    return s;
}
void cetta_inbox_window_free(CettaInboxWindow *w) {
    if (w) { cetta_durable_observation_free(w->cursor); free(w); }
}
static bool same_cursor(const CettaDurableSnapshot *a, const CettaDurableSnapshot *b) {
    return !strcmp(a->epoch,b->epoch) && a->count==b->count &&
        (!a->count || a->records[0].revision==b->records[0].revision);
}
static bool same_record(const CettaDurableRecord *r, const CettaDurableOp *o) {
    return r->size==o->size && !memcmp(r->data,o->data,o->size);
}
static CettaDurableStatus encoded(CettaDurableOp *op, const Atom *value) {
    unsigned char *bytes=NULL; size_t n=0;
    CettaDurableStatus s=cetta_durable_value_encode(value,&bytes,&n);
    if (s==DURABLE_OK) { op->data=bytes; op->size=n; }
    return s;
}
CettaDurableStatus cetta_inbox_commit(CettaInboxWindow *w,
        const CettaInboxItem *items, size_t count, CettaInboxCommit *out) {
    if (!out) return DURABLE_INVALID;
    *out=(CettaInboxCommit){.revision=-1,.next_offset=-1};
    if (!w || (count && !items)) return DURABLE_INVALID;
    if (w->spent) return DURABLE_CONFLICT;
    if (count>INBOX_BATCH) return DURABLE_LIMIT;
    if (!count && !w->pending_poll) { w->spent=true; out->next_offset=w->offset; return DURABLE_OK; }
    Arena a; arena_init(&a);
    char ledger[INBOX_BATCH][96], input[INBOX_BATCH][168];
    int64_t ids[INBOX_BATCH];
    CettaDurableOp candidates[2*INBOX_BATCH+1]={0}, ops[2*INBOX_BATCH+4];
    CettaDurableScope scopes[2*INBOX_BATCH+4]={{DURABLE_KEY,"host.cursors",w->source}};
    CettaDurableObservation *o=NULL;
    size_t unique=0, nops=0, inserted=0, bytes=0;
    int64_t offset=w->offset;
    CettaDurableStatus s=DURABLE_INVALID;
    for (size_t i=0;i<count;++i) {
        s=DURABLE_INVALID;
        const CettaInboxItem *item=&items[i];
        if (item->disposition<INBOX_ROUTED || item->disposition>INBOX_UNAUTHORIZED ||
            !item->value || !component(item->lane) || item->id<0 || item->id==INT64_MAX) goto done;
        const char *kind=item->disposition==INBOX_ROUTED?"routed":item->disposition==INBOX_UNSUPPORTED?"unsupported":"unauthorized";
        size_t k=0;
        for (;k<unique;++k) if (item->id==ids[k]) break;
        bool duplicate=k<unique;
        if (!duplicate) {
            s=cetta_inbox_keys(w->source,item->lane,item->id,ledger[k],input[k]); if (s!=DURABLE_OK) goto done;
            scopes[1+2*k]=(CettaDurableScope){DURABLE_KEY,"host.received",ledger[k]};
            scopes[2+2*k]=(CettaDurableScope){DURABLE_KEY,"host.inbox",input[k]};
        }
        Atom *record[]={atom_symbol(&a,"host:received"),atom_int(&a,1),atom_string(&a,w->source),
            atom_int(&a,item->id),atom_string(&a,item->lane),atom_symbol(&a,kind),(Atom *)item->value};
        CettaDurableOp op={DURABLE_INSERT,"host.received",ledger[k],NULL,0};
        s=encoded(&op,atom_expr(&a,record,7)); if (s!=DURABLE_OK) goto done;
        if (duplicate) {
            bool equal=op.size==candidates[1+2*k].size && !memcmp(op.data,candidates[1+2*k].data,op.size);
            free((void *)op.data);
            if (!equal) { s=DURABLE_CORRUPT; goto done; }
            continue;
        }
        candidates[1+2*k]=op; ids[k]=item->id; ++unique;
        Atom *reference[]={atom_symbol(&a,"host:input"),atom_int(&a,1),atom_string(&a,w->source),
            atom_int(&a,item->id),atom_string(&a,ledger[k]),atom_string(&a,item->lane),atom_symbol(&a,kind)};
        candidates[2+2*k]=(CettaDurableOp){DURABLE_INSERT,"host.inbox",input[k],NULL,0};
        s=encoded(&candidates[2+2*k],atom_expr(&a,reference,7)); if (s!=DURABLE_OK) goto done;
        bytes+=op.size+candidates[2+2*k].size;
        if (bytes>INBOX_BYTES) { s=DURABLE_LIMIT; goto done; }
        if (offset<=item->id) offset=item->id+1;
    }
    scopes[1+2*unique]=(CettaDurableScope){DURABLE_KEY,"host.polls",w->source};
    scopes[2+2*unique]=(CettaDurableScope){DURABLE_KEY,"host.poll-control",w->source};
    scopes[3+2*unique]=(CettaDurableScope){DURABLE_KEY,"host.poll-clock",w->source};
    s=cetta_durable_observe(w->store,scopes,4+2*unique,&o); if (s!=DURABLE_OK) goto done;
    if (!same_cursor(view(w->cursor,0),view(o,0)) ||
        !same_cursor(view(w->cursor,1),view(o,1+2*unique)) ||
        !same_cursor(view(w->cursor,2),view(o,2+2*unique)) ||
        !same_cursor(view(w->cursor,3),view(o,3+2*unique))) { s=DURABLE_CONFLICT; goto done; }
    Atom *cursor[]={atom_symbol(&a,"host:cursor"),atom_int(&a,1),atom_string(&a,w->source),atom_int(&a,offset)};
    candidates[0]=(CettaDurableOp){view(o,0)->count?DURABLE_REPLACE:DURABLE_INSERT,"host.cursors",w->source,NULL,0};
    s=encoded(&candidates[0],atom_expr(&a,cursor,4)); if (s!=DURABLE_OK) goto done;
    ops[nops++]=candidates[0];
    for (size_t i=0;i<unique;++i) {
        const CettaDurableSnapshot *seen=view(o,1+2*i), *queued=view(o,2+2*i);
        if (seen->count) {
            if (!same_record(&seen->records[0],&candidates[1+2*i]) ||
                (queued->count && !same_record(&queued->records[0],&candidates[2+2*i]))) { s=DURABLE_CORRUPT; goto done; }
        } else {
            if (queued->count) { s=DURABLE_CORRUPT; goto done; }
            ops[nops++]=candidates[1+2*i]; ops[nops++]=candidates[2+2*i]; ++inserted;
        }
    }
    if (w->pending_poll) ops[nops++]=(CettaDurableOp){DURABLE_REMOVE,"host.polls",w->source,NULL,0};
    if (view(w->cursor,2)->count) ops[nops++]=(CettaDurableOp){DURABLE_REMOVE,"host.poll-control",w->source,NULL,0};
    if (view(w->cursor,3)->count) ops[nops++]=(CettaDurableOp){DURABLE_REMOVE,"host.poll-clock",w->source,NULL,0};
    w->spent=true;
    s=cetta_durable_commit_observed(w->store,o,ops,nops,&out->revision);
    if (s==DURABLE_OK) { out->next_offset=offset; out->inserted=inserted; }
done:
    cetta_durable_observation_free(o);
    for (size_t i=0;i<1+2*unique;++i) free((void *)candidates[i].data);
    arena_free(&a); return s;
}

CettaDurableStatus cetta_inbox_poll_clock(CettaInboxWindow *w, int64_t sampled, int64_t *recorded) {
    if (!recorded) return DURABLE_INVALID;
    *recorded=-1;
    if (!w || !w->pending_poll || sampled<0) return DURABLE_INVALID;
    if (w->spent) return DURABLE_CONFLICT;
    if (w->recovery_recorded) return DURABLE_PRECONDITION;
    if (w->clock_ms>=0) { *recorded=w->clock_ms; return DURABLE_OK; }
    Arena a; arena_init(&a);
    Atom *fields[]={atom_symbol(&a,"host:poll-clock"),atom_int(&a,1),atom_string(&a,w->source),
        atom_int(&a,view(w->cursor,1)->records[0].revision),atom_int(&a,sampled)};
    CettaDurableOp op={DURABLE_INSERT,"host.poll-clock",w->source,NULL,0};
    CettaDurableStatus s=encoded(&op,atom_expr(&a,fields,5)); int64_t rev;
    if (s==DURABLE_OK) s=cetta_durable_commit_observed(w->store,w->cursor,&op,1,&rev);
    free((void *)op.data); arena_free(&a);
    if (s!=DURABLE_OK) return s;
    CettaInboxWindow *fresh=NULL;
    s=cetta_inbox_recover_poll(w->store,w->source,&fresh);
    if (s!=DURABLE_OK) return s;
    for (size_t i=0;i<3;++i) if (!same_cursor(view(w->cursor,i),view(fresh->cursor,i))) s=DURABLE_CONFLICT;
    if (s==DURABLE_OK) {
        cetta_durable_observation_free(w->cursor); w->cursor=fresh->cursor; fresh->cursor=NULL;
        w->clock_ms=fresh->clock_ms; *recorded=w->clock_ms;
    }
    cetta_inbox_window_free(fresh); return s;
}
CettaDurableStatus cetta_inbox_resolve_poll(CettaInboxWindow *w, const char *version,
                                          const Atom *decision, int64_t *revision) {
    if (!revision) return DURABLE_INVALID;
    *revision=-1;
    if (!w || !w->pending_poll || !version || !*version || strnlen(version,256)>255 || !decision)
        return DURABLE_INVALID;
    if (w->spent) return DURABLE_CONFLICT;
    if (w->clock_ms<0 || w->recovery_recorded) return DURABLE_PRECONDITION;
    int64_t due;
    if (!recovery_decision(decision,&due) || (due>=0 && due<=w->clock_ms)) return DURABLE_INVALID;
    bool retry=due>=0;
    if (w->failures==INT64_MAX) return DURABLE_LIMIT;
    Arena a; arena_init(&a); Atom *poll=NULL;
    const CettaDurableRecord *r=cetta_inbox_poll_response(w);
    CettaDurableStatus s=cetta_durable_value_decode(&a,r->data,r->size,&poll);
    CettaDurableOp ops[3]={{view(w->cursor,2)->count?DURABLE_REPLACE:DURABLE_INSERT,"host.poll-control",w->source,NULL,0},
        {DURABLE_REMOVE,"host.poll-clock",w->source,NULL,0},{DURABLE_REMOVE,"host.polls",w->source,NULL,0}};
    if (s==DURABLE_OK) {
        Atom *fields[]={atom_symbol(&a,"host:poll-control"),atom_int(&a,1),atom_string(&a,w->source),
            atom_int(&a,w->offset),atom_int(&a,r->revision),atom_int(&a,w->failures+1),atom_int(&a,w->clock_ms),
            atom_int(&a,due),atom_string(&a,version),(Atom *)decision,poll};
        s=encoded(&ops[0],atom_expr(&a,fields,11));
    }
    if (s==DURABLE_OK) {
        w->spent=true;
        s=cetta_durable_commit_observed(w->store,w->cursor,ops,retry?3:2,revision);
    }
    free((void *)ops[0].data); arena_free(&a); return s;
}
