#define _GNU_SOURCE
#include "source_owner.h"
#include "atom_wire.h"
#include "shared_transition.h"
#include "space_source_overlay.h"
#include "storage_census.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

struct CettaOccurrenceSource {
    atomic_size_t references;
    const SpaceSourceCursorOps *ops;
    void *prototype;
    uint64_t count;
    size_t maximum;
    char domain[33];
};
typedef struct {
    atomic_size_t references;
    unsigned char *body,*identity;
    size_t body_size,identity_size;
} Packet;
struct CettaCandidateCursor {
    CettaOccurrenceSource *source;
    void *scan;
    Packet *packet;
    uint64_t next,packet_sequence,seen;
    bool full,terminal;
    CettaCandidateStatus completion;
};
static CettaCandidateStatus failure(SpaceOccurrenceCursorStep status) {
    switch(status) {
    case SPACE_OCCURRENCE_CURSOR_CAPACITY:return CETTA_CANDIDATE_CAPACITY;
    case SPACE_OCCURRENCE_CURSOR_INVALIDATED:return CETTA_CANDIDATE_INVALIDATED;
    default:return CETTA_CANDIDATE_INCOMPLETE;
    }
}
static CettaCandidateStatus wire_failure(CettaAtomWireStatus status) {
    return status==ATOM_WIRE_NOMEM ? CETTA_CANDIDATE_CAPACITY
        : status==ATOM_WIRE_LIMIT ? CETTA_CANDIDATE_LIMIT
        : status==ATOM_WIRE_UNSUPPORTED ? CETTA_CANDIDATE_UNSUPPORTED : CETTA_CANDIDATE_INCOMPLETE;
}
static void packet_release(Packet *p) {
    if (p && atomic_fetch_sub(&p->references,1)==1) {free(p->body);free(p->identity);free(p);}
}
void cetta_occurrence_source_retain(CettaOccurrenceSource *s) {if(s)atomic_fetch_add(&s->references,1);}
void cetta_occurrence_source_release(CettaOccurrenceSource *s) {
    if (!s || atomic_fetch_sub(&s->references,1)!=1) return;
    if (s->prototype) s->ops->free(s->prototype);
    free(s);
}
uint64_t cetta_occurrence_source_count(const CettaOccurrenceSource *s) {return s ? s->count : 0;}
size_t cetta_occurrence_source_limit(const CettaOccurrenceSource *s) {return s ? s->maximum : 0;}
CettaCandidateStatus cetta_occurrence_source_open(Space *space,size_t maximum,CettaOccurrenceSource **out) {
    if (!out) return CETTA_CANDIDATE_INVALID;
    *out=NULL;
    if (!space || !maximum) return CETTA_CANDIDATE_INVALID;
    CettaOccurrenceSource *s=calloc(1,sizeof(*s));if (!s) return CETTA_CANDIDATE_CAPACITY;
    atomic_init(&s->references,1);s->maximum=maximum;
    CettaCandidateStatus status=CETTA_CANDIDATE_OK;
    {
        CETTA_SCOPED_SHARED_TRANSITION(capture);
        const SpaceMatchBackendOps *ops=space->match_backend.ops;
        if (!space_owner_is_current(space) || space->match_backend.kind!=SPACE_ENGINE_SOURCE ||
            !ops || !ops->occurrence_open || !ops->occurrence_ops || !ops->occurrence_ops->clone ||
            !ops->occurrence_ops->next || !ops->occurrence_ops->atom || !ops->occurrence_ops->free) status=CETTA_CANDIDATE_UNSUPPORTED;
        else {
            s->ops=ops->occurrence_ops;s->count=space_length64(space);
            SpaceOccurrenceCursorStep reason=SPACE_OCCURRENCE_CURSOR_CAPACITY;
            if (!ops->occurrence_open(space,NULL,&s->prototype,&reason)) status=failure(reason);
            else if (s->ops->value_byte_limit) {
                size_t limit=s->ops->value_byte_limit(s->prototype);
                if (!limit) status=CETTA_CANDIDATE_INVALID;
                else if (limit<s->maximum) s->maximum=limit;
            }
        }
    }
    if (status==CETTA_CANDIDATE_OK && !s->ops->identity) {
        unsigned char nonce[16];size_t used=0;
        while (used<sizeof(nonce)) {
            ssize_t n=getrandom(nonce+used,sizeof(nonce)-used,0);
            if(n<0 && errno==EINTR) continue;
            if(n<=0) {status=CETTA_CANDIDATE_INCOMPLETE;break;}used+=(size_t)n;
        }
        if (status==CETTA_CANDIDATE_OK) {
            static const char hex[]="0123456789abcdef";
            for (size_t i=0;i<16;i++) {s->domain[2*i]=hex[nonce[i]>>4];s->domain[2*i+1]=hex[nonce[i]&15];}
        }
    }
    if(status!=CETTA_CANDIDATE_OK) {cetta_occurrence_source_release(s);return status;}
    *out=s;return status;
}
static CettaCandidateCursor *cursor_allocate(CettaOccurrenceSource *s) {
    CettaCandidateCursor *c=calloc(1,sizeof(*c));if (!c) return NULL;
    c->source=s;cetta_occurrence_source_retain(s);return c;
}
CettaCandidateStatus cetta_candidate_cursor_open(CettaOccurrenceSource *s,Atom *pattern,CettaCandidateCursor **out) {
    if (!out) return CETTA_CANDIDATE_INVALID;
    *out=NULL;if (!s) return CETTA_CANDIDATE_INVALID;
    CettaCandidateCursor *c=cursor_allocate(s);if (!c) return CETTA_CANDIDATE_CAPACITY;
    SpaceOccurrenceCursorStep reason=SPACE_OCCURRENCE_CURSOR_CAPACITY;
    c->full=!pattern || !s->ops->select;
    bool opened=!c->full ? s->ops->select(s->prototype,pattern,&c->scan,&reason)
        : s->ops->clone(s->prototype,true,&c->scan);
    if (!opened) {cetta_candidate_cursor_close(c);return failure(reason);}
    *out=c;return CETTA_CANDIDATE_OK;
}
CettaCandidateStatus cetta_candidate_cursor_clone(const CettaCandidateCursor *source,bool rewind,CettaCandidateCursor **out) {
    if (!out) return CETTA_CANDIDATE_INVALID;
    *out=NULL;if (!source || !source->source) return CETTA_CANDIDATE_INVALID;
    if(source->terminal && source->completion==CETTA_CANDIDATE_CANCELLED) return CETTA_CANDIDATE_CANCELLED;
    CettaCandidateCursor *c=cursor_allocate(source->source);if (!c) return CETTA_CANDIDATE_CAPACITY;
    c->full=source->full;
    if (source->scan && !source->source->ops->clone(source->scan,rewind,&c->scan)) {cetta_candidate_cursor_close(c);return CETTA_CANDIDATE_CAPACITY;}
    if (!rewind) {
        c->next=source->next;c->packet_sequence=source->packet_sequence;c->seen=source->seen;
        c->terminal=source->terminal;c->completion=source->completion;c->packet=source->packet;
        if(c->packet)atomic_fetch_add(&c->packet->references,1);
    } else if (!source->scan && !source->source->ops->clone(source->source->prototype,true,&c->scan)) {
        cetta_candidate_cursor_close(c);return CETTA_CANDIDATE_CAPACITY;
    }
    *out=c;return CETTA_CANDIDATE_OK;
}
static CettaCandidateStatus stop(CettaCandidateCursor *c,CettaCandidateStatus status) {
    c->terminal=true;c->completion=status;return status;
}
static CettaCandidateStatus row(CettaCandidateCursor *c,CettaCandidateRow *out) {
    *out=(CettaCandidateRow){c->packet_sequence,c->packet->body_size,c->packet->identity_size};return CETTA_CANDIDATE_ROW;
}
CettaCandidateStatus cetta_candidate_cursor_next(CettaCandidateCursor *c,uint64_t sequence,uint64_t work,CettaCandidateRow *out) {
    if (!c || !out) return CETTA_CANDIDATE_INVALID;
    *out=(CettaCandidateRow){0};
    if(c->terminal && c->completion==CETTA_CANDIDATE_CANCELLED) return CETTA_CANDIDATE_CANCELLED;
    if (c->packet && sequence==c->packet_sequence) return row(c,out);
    if (sequence!=c->next) return CETTA_CANDIDATE_STALE;
    if (c->terminal) return c->completion;
    if (!work) return CETTA_CANDIDATE_PROGRESS;
    packet_release(c->packet);c->packet=NULL;
    CettaIndex index=0;SpaceOccurrenceCursorStep step=c->source->ops->next(c->scan,&index);
    if (step==SPACE_OCCURRENCE_CURSOR_PROGRESS) return CETTA_CANDIDATE_PROGRESS;
    if (step==SPACE_OCCURRENCE_CURSOR_END)
        return stop(c,c->full && c->seen!=c->source->count ? CETTA_CANDIDATE_INCOMPLETE : CETTA_CANDIDATE_END);
    if (step!=SPACE_OCCURRENCE_CURSOR_ITEM) return stop(c,failure(step));
    if (c->next==UINT64_MAX || c->seen==UINT64_MAX) return stop(c,CETTA_CANDIDATE_LIMIT);
    Atom *atom=c->source->ops->atom(c->scan,index);
    if (!atom) return stop(c,CETTA_CANDIDATE_INCOMPLETE);
    Packet *p=calloc(1,sizeof(*p));if (!p) return stop(c,CETTA_CANDIDATE_CAPACITY);
    atomic_init(&p->references,1);
    CettaAtomWireStatus status=cetta_atom_wire_encode_bounded(atom,c->source->maximum,&p->body,&p->body_size);
    Arena scratch;arena_init_detached(&scratch);
    Atom *identity=NULL;
    if (status==ATOM_WIRE_OK) {
        if(c->source->ops->identity) identity=c->source->ops->identity(c->scan,index);
        else if(index<=INT64_MAX) identity=atom_expr3(&scratch,atom_symbol(&scratch,"SpaceOccurrenceIdV1"),
            atom_string(&scratch,c->source->domain),atom_int(&scratch,(int64_t)index));
        if(!identity || atom_has_vars(identity)) status=ATOM_WIRE_INVALID;
        else status=cetta_atom_wire_encode_bounded(identity,c->source->maximum,&p->identity,&p->identity_size);
    }
    arena_free(&scratch);
    if(status!=ATOM_WIRE_OK) {packet_release(p);return stop(c,wire_failure(status));}
    c->packet=p;c->packet_sequence=c->next++;++c->seen;return row(c,out);
}
CettaCandidateStatus cetta_candidate_cursor_read(const CettaCandidateCursor *c,uint64_t sequence,bool identity,
        uint64_t offset,size_t maximum,const unsigned char **bytes,size_t *size) {
    if (!bytes || !size) return CETTA_CANDIDATE_INVALID;
    *bytes=NULL;*size=0;if(!c)return CETTA_CANDIDATE_INVALID;
    if(c->terminal && c->completion==CETTA_CANDIDATE_CANCELLED) return CETTA_CANDIDATE_CANCELLED;
    if (!c->packet || c->packet_sequence!=sequence) return CETTA_CANDIDATE_STALE;
    size_t length=identity ? c->packet->identity_size : c->packet->body_size;
    if(offset>length) return CETTA_CANDIDATE_INVALID;
    *size=length-(size_t)offset;if(*size>maximum)*size=maximum;
    *bytes=(identity ? c->packet->identity : c->packet->body)+(size_t)offset;return CETTA_CANDIDATE_OK;
}
CettaCandidateStatus cetta_candidate_cursor_cancel(CettaCandidateCursor *c) {
    if(!c)return CETTA_CANDIDATE_INVALID;
    packet_release(c->packet);c->packet=NULL;
    if(c->scan){c->source->ops->free(c->scan);c->scan=NULL;}
    return stop(c,CETTA_CANDIDATE_CANCELLED);
}
uint64_t cetta_candidate_cursor_sequence(const CettaCandidateCursor *c) {return c ? c->next : 0;}
void cetta_candidate_cursor_close(CettaCandidateCursor *c) {
    if(!c)return;
    cetta_candidate_cursor_cancel(c);cetta_occurrence_source_release(c->source);free(c);
}
void cetta_occurrence_source_storage(const void *raw,CettaStorageCensus *census) {
    const CettaOccurrenceSource *s=raw;
    if(s && cetta_storage_object(census,s,sizeof(*s),CETTA_STORAGE_BLOCK) && s->ops->storage) s->ops->storage(s->prototype,census);
}
void cetta_candidate_cursor_storage(const void *raw,CettaStorageCensus *census) {
    const CettaCandidateCursor *c=raw;
    if(!c || !cetta_storage_object(census,c,sizeof(*c),CETTA_STORAGE_BLOCK))return;
    cetta_occurrence_source_storage(c->source,census);
    if(c->scan && c->source->ops->storage)c->source->ops->storage(c->scan,census);
    Packet *p=c->packet;
    if(p && cetta_storage_object(census,p,sizeof(*p),CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census,p->body,p->body_size,1,0);cetta_storage_array(census,p->identity,p->identity_size,1,0);
    }
}
const char *cetta_candidate_status_name(CettaCandidateStatus s) {
    static const char *names[]={"ok","row","progress","end","cancelled","stale","invalid","invalidated","incomplete","capacity","limit","unsupported"};
    return (unsigned)s<sizeof(names)/sizeof(*names) ? names[s] : "invalid";
}

/* A local Space loan uses the same captured provider without re-encoding
 * candidates. Per-cursor wrappers retain the selected policy and positional
 * identity domain even after the source handle or Space header closes. */
typedef struct {
    CettaOccurrenceSource *source;void *scan;
    Arena ids;ArenaMark empty;Atom *identity;
    CettaIndex last;bool seen;
} Loan;
static const SpaceMatchBackendOps loan_source_ops;
static Loan *loan_new(CettaOccurrenceSource *s) {
    Loan *l=calloc(1,sizeof(*l));if(!l)return NULL;
    l->source=s;cetta_occurrence_source_retain(s);arena_init_detached(&l->ids);l->empty=arena_mark(&l->ids);return l;
}
static void loan_free(void *raw) {
    Loan *l=raw;if(!l)return;
    if(l->scan)l->source->ops->free(l->scan);
    arena_free(&l->ids);cetta_occurrence_source_release(l->source);free(l);
}
static bool loan_clone(const void *raw,bool rewind,void **out) {
    const Loan *p=raw;*out=NULL;Loan *l=loan_new(p->source);if(!l)return false;
    if(!p->source->ops->clone(p->scan,rewind,&l->scan)) {loan_free(l);return false;}
    if(!rewind) {
        l->last=p->last;l->seen=p->seen;
        if(p->identity && !(l->identity=atom_deep_copy(&l->ids,p->identity))) {loan_free(l);return false;}
    }
    *out=l;return true;
}
static bool loan_open(Space *space,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    CettaOccurrenceSource *s=space->match_backend.source;*out=NULL;Loan *l=loan_new(s);if(!l)return false;
    bool ok=pattern && s->ops->select ? s->ops->select(s->prototype,pattern,&l->scan,failure)
        : s->ops->clone(s->prototype,true,&l->scan);
    if(!ok) {loan_free(l);return false;}*out=l;return true;
}
static bool loan_select(const void *raw,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    const Loan *l=raw;Space space={0};space.match_backend.source=l->source;return loan_open(&space,pattern,out,failure);
}
static SpaceOccurrenceCursorStep loan_next(void *raw,CettaIndex *index) {
    Loan *l=raw;l->seen=false;l->identity=NULL;arena_reset(&l->ids,l->empty);
    SpaceOccurrenceCursorStep step=l->source->ops->next(l->scan,index);
    if(step==SPACE_OCCURRENCE_CURSOR_ITEM) {l->seen=true;l->last=*index;}
    return step;
}
static Atom *loan_atom(const void *raw,CettaIndex index) {
    const Loan *l=raw;return l->seen && l->last==index ? l->source->ops->atom(l->scan,index) : NULL;
}
static Atom *loan_identity(const void *raw,CettaIndex index) {
    Loan *l=(Loan *)raw;
    if(!l->seen || l->last!=index)return NULL;
    if(l->source->ops->identity)return l->source->ops->identity(l->scan,index);
    if(index>INT64_MAX)return NULL;
    if(!l->identity) l->identity=atom_expr3(&l->ids,atom_symbol(&l->ids,"SpaceOccurrenceIdV1"),
        atom_string(&l->ids,l->source->domain),atom_int(&l->ids,(int64_t)index));
    return l->identity;
}
static size_t loan_limit(const void *raw) {return ((const Loan *)raw)->source->maximum;}
static bool loan_pending_wait(const void *raw,SpaceSourceWait *out) {
    const Loan *l=raw;*out=(SpaceSourceWait){0};
    return l->source->ops->pending_wait && l->source->ops->pending_wait(l->scan,out);
}
static void loan_storage(const void *raw,CettaStorageCensus *census) {
    const Loan *l=raw;if(!l || !cetta_storage_object(census,l,sizeof(*l),CETTA_STORAGE_BLOCK))return;
    arena_storage_census(&l->ids,census);cetta_occurrence_source_storage(l->source,census);
    if(l->source->ops->storage)l->source->ops->storage(l->scan,census);
}
static const SpaceSourceCursorOps loan_ops={.clone=loan_clone,.select=loan_select,.next=loan_next,.atom=loan_atom,
    .identity=loan_identity,.value_byte_limit=loan_limit,.pending_wait=loan_pending_wait,.free=loan_free,.storage=loan_storage};
static uint64_t loan_count(const Space *space) {return ((const CettaOccurrenceSource *)space->match_backend.source)->count;}
static void loan_source_free(Space *space) {cetta_occurrence_source_release(space->match_backend.source);space->match_backend.source=NULL;}
static void loan_source_storage(const Space *space,CettaStorageCensus *census) {cetta_occurrence_source_storage(space->match_backend.source,census);}
static bool loan_overlay(Space *target,const Space *base);
static const SpaceMatchBackendOps loan_source_ops={.name="captured-occurrence-source",.occurrence_open=loan_open,
    .occurrence_ops=&loan_ops,.logical_len=loan_count,.free=loan_source_free,.storage=loan_source_storage,.overlay_local=loan_overlay};
bool cetta_occurrence_source_space(CettaOccurrenceSource *s,Space *target) {
    if(!s || !target || target->match_backend.kind!=SPACE_ENGINE_NATIVE || space_length64(target) || target->overlay_base || space_has_dependencies(target))return false;
    cetta_occurrence_source_retain(s);target->match_backend.kind=SPACE_ENGINE_SOURCE;
    target->match_backend.ops=&loan_source_ops;target->match_backend.source=s;space_read_view_invalidate(target);return true;
}

static bool loan_overlay(Space *target,const Space *base) {
    CettaOccurrenceSource *s=base->match_backend.source;Space *copy=malloc(sizeof(*copy));if(!copy)return false;
    space_init_with_universe(copy,target->native.universe);
    if(cetta_occurrence_source_space(s,copy) && space_source_overlay_init_owned(target,copy,s->maximum))return true;
    space_free(copy);free(copy);return false;
}
