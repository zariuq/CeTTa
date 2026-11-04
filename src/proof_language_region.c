#include "proof_language_region.h"
#include "fallible_alloc.h"
#include "u64_index.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AtomId key;
    uint64_t support;
    uint32_t rank, incoming, outgoing, degree;
    CettaU64Index *directory;
} ProofState;
typedef struct {
    AtomId label;
    uint32_t parent, child, next_in, next_out;
} ProofEdge;
struct CettaProofImage {
    _Atomic uint64_t references;
    TermUniverse *universe;
    uint64_t universe_instance, storage_epoch;
    AtomId seed;
    ProofState *states;
    ProofEdge *edges;
    size_t state_capacity, edge_capacity;
    /* Fields borrow IDs from this image-owned unique dependency ledger. */
    AtomId *root_ids;
    size_t root_count, root_capacity;
    CettaU64Index root_index;
    CettaProofStats stats;
    CettaProofLimits limits;
    bool roots_registered, sealed;
};
struct CettaProofBuilder {
    CettaProofImage *image;
    CettaU64Index target_index;
    uint32_t rank, first, past_last;
    CettaProofStatus error;
    bool sealed;
};
typedef struct { uint32_t node, edge; } ProofFrame;
struct CettaProofCursor {
    CettaProofImage *image;
    ProofFrame *frames;
    AtomId *labels;
    uint32_t selected, next_state, current_state, length, capacity;
    uint64_t token;
    bool active, pending, pop_after_commit, finished;
};
#define NO_EDGE UINT32_MAX
#define INLINE_OUTGOING 8u

static bool same_instance(const CettaProofImage *image) {
    return image && image->universe && image->universe->instance_id==image->universe_instance;
}
static bool current(const CettaProofImage *image) {
    return same_instance(image) && image->universe->storage_epoch==image->storage_epoch;
}
static CettaProofStatus poison(CettaProofBuilder *b,CettaProofStatus status) {
    if(b && b->error==CETTA_PROOF_OK)b->error=status;
    return status;
}
static bool checked_add(uint64_t left,uint64_t right,uint64_t limit,uint64_t *out) {
    if(right>limit || left>limit-right)return false;
    *out=left+right;return true;
}
static bool reserve(void **array,size_t *capacity,size_t needed,size_t width) {
    if(needed<=*capacity)return true;
    if(*capacity>SIZE_MAX/2u)return false;
    size_t next=*capacity?*capacity*2u:16u;
    if(next<needed)next=needed;
    if(next>SIZE_MAX/width)return false;
    void *more=cetta_fallible_malloc(next*width);
    if(!more)return false;
    if(*capacity)memcpy(more,*array,*capacity*width);
    free(*array);*array=more;*capacity=next;return true;
}
static CettaProofStatus prepare_label(CettaProofImage *im,AtomId input,AtomId *out) {
    const CettaTermHdr *h=tu_hdr(im->universe,input);
    if(!h)return CETTA_PROOF_INVALIDATED;
    if(h->tag==ATOM_SYMBOL) {
        SymbolId symbol=tu_sym(im->universe,input);
        *out=tu_intern_symbol(im->universe,symbol);
    } else if(h->tag==ATOM_GROUNDED && tu_ground_kind(im->universe,input)==GV_INT) {
        int64_t value=tu_int(im->universe,input);
        *out=tu_intern_int(im->universe,value);
    } else return CETTA_PROOF_INVALIDATED;
    return *out==CETTA_ATOM_ID_NONE?CETTA_PROOF_CAPACITY:CETTA_PROOF_OK;
}
/* Inhibit collection while the physical ownership ledger and its counted
 * references are being published or retired. This does not serialize callers. */
typedef struct { TermUniverse *universe; } ProofOwnershipGuard;
static bool ownership_enter(ProofOwnershipGuard *guard,TermUniverse *universe) {
    guard->universe=NULL;
    if(!universe)return true;
    uint32_t count=__atomic_load_n(&universe->collection_inhibit,__ATOMIC_RELAXED);
    while(count!=UINT32_MAX) {
        if(__atomic_compare_exchange_n(&universe->collection_inhibit,&count,count+1u,
                true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
            guard->universe=universe;return true;
        }
    }
    return false;
}
static void ownership_leave(ProofOwnershipGuard *guard) {
    if(guard->universe)term_universe_collection_inhibit_leave(guard->universe);
}
#define PROOF_OWNERSHIP_GUARD(name) \
    __attribute__((cleanup(ownership_leave))) ProofOwnershipGuard name={0}

/* Counting is optional until the first reclaiming collection. That start
 * reconstructs these exact unique references from the counted provider. */
static bool acquire_root(CettaProofImage *im,AtomId id) {
    uint32_t ignored;
    if(cetta_u64_index_find(&im->root_index,id,&ignored))return true;
    if(im->root_count==SIZE_MAX ||
       !reserve((void **)&im->root_ids,&im->root_capacity,im->root_count+1u,sizeof(*im->root_ids)))return false;
    if(cetta_u64_index_insert(&im->root_index,id,0u)!=CETTA_GSLT_U32_INDEX_INSERTED_V1)return false;
    if(im->universe->counting)term_universe_hold_counted(im->universe,id);
    im->root_ids[im->root_count++]=id;
    return true;
}
#ifdef CETTA_PROOF_ROOT_DIAGNOSTICS
static uint64_t proof_root_callback_calls,proof_root_callback_ids;
#endif
static void list_roots(const TermUniverse *universe,TermUniverseRootVisit visit,
                       void *visitor,void *context) {
    CettaProofImage *im=context;(void)universe;
#ifdef CETTA_PROOF_ROOT_DIAGNOSTICS
    proof_root_callback_calls++;
#endif
    if(!current(im))return;
    for(size_t i=0u;i<im->root_count;i++) {
#ifdef CETTA_PROOF_ROOT_DIAGNOSTICS
        proof_root_callback_ids++;
#endif
        visit(visitor,im->root_ids[i]);
    }
}
static void clear_directories(CettaProofImage *im,uint32_t first,uint32_t last) {
    for(uint32_t i=first;i<last;i++)if(im->states[i].directory) {
        cetta_u64_index_free(im->states[i].directory);
        free(im->states[i].directory);im->states[i].directory=NULL;
    }
}
bool cetta_proof_image_retain(CettaProofImage *im) {
    if(!im)return false;
    uint64_t count=atomic_load_explicit(&im->references,memory_order_relaxed);
    while(count && count!=UINT64_MAX) {
        if(atomic_compare_exchange_weak_explicit(&im->references,&count,count+1u,
                memory_order_relaxed,memory_order_relaxed))return true;
    }
    return false;
}
void cetta_proof_image_release(CettaProofImage *im) {
    if(!im)return;
    uint64_t old=atomic_fetch_sub_explicit(&im->references,1u,memory_order_acq_rel);
    assert(old);
    if(old!=1u)return;
    PROOF_OWNERSHIP_GUARD(ownership);
    /* At MAX an existing inhibition already excludes collection. Retirement
     * borrows it under the serialized-TU contract without modifying it. */
    (void)ownership_enter(&ownership,same_instance(im)?im->universe:NULL);
    if(im->roots_registered && same_instance(im))term_universe_remove_root_provider(im->universe,list_roots,im);
    /* A storage reset destroys the old count domain. Never decrement its
     * reused numeric coordinates in the replacement storage. */
    if(current(im) && im->universe->counting)
        for(size_t i=0u;i<im->root_count;i++)term_universe_release_counted(im->universe,im->root_ids[i]);
    clear_directories(im,0u,im->stats.states);
    cetta_u64_index_free(&im->root_index);
    free(im->root_ids);free(im->states);free(im->edges);free(im);
}
CettaProofStatus cetta_proof_builder_new(TermUniverse *u,AtomId root_key,
        AtomId seed,CettaProofLimits limits,CettaProofBuilder **out) {
    if(out)*out=NULL;
    if(!out || !u)return CETTA_PROOF_INVALIDATED;
    PROOF_OWNERSHIP_GUARD(ownership);
    if(!ownership_enter(&ownership,u))return CETTA_PROOF_CAPACITY;
    if(!tu_hdr(u,root_key) || !limits.max_states || !limits.max_support)return CETTA_PROOF_INVALIDATED;
    CettaProofBuilder *b=cetta_fallible_calloc(1u,sizeof(*b));
    CettaProofImage *im=cetta_fallible_calloc(1u,sizeof(*im));
    if(!b || !im){free(b);free(im);return CETTA_PROOF_CAPACITY;}
    atomic_init(&im->references,1u);im->universe=u;im->universe_instance=u->instance_id;im->storage_epoch=u->storage_epoch;
    im->seed=seed;im->limits=limits;b->image=im;b->past_last=1u;
    cetta_u64_index_init(&b->target_index);cetta_u64_index_init(&im->root_index);
    CettaProofStatus label_status=prepare_label(im,seed,&im->seed);
    if(label_status!=CETTA_PROOF_OK){cetta_proof_builder_free(b);return label_status;}
    if(!reserve((void **)&im->states,&im->state_capacity,1u,sizeof(*im->states))) {
        cetta_proof_builder_free(b);return CETTA_PROOF_CAPACITY;
    }
    im->states[0]=(ProofState){.key=root_key,.support=1u,.incoming=NO_EDGE,.outgoing=NO_EDGE};
    im->stats.states=1u;im->stats.support=1u;
    if(!acquire_root(im,im->seed) || !acquire_root(im,root_key) ||
       !term_universe_add_counted_root_provider(u,list_roots,im)) {
        cetta_proof_builder_free(b);return CETTA_PROOF_CAPACITY;
    }
    im->roots_registered=true;*out=b;return CETTA_PROOF_OK;
}
static CettaProofStatus writable(CettaProofBuilder *b) {
    if(!b || !b->image || b->sealed || !current(b->image))return CETTA_PROOF_INVALIDATED;
    return b->error;
}
CettaProofStatus cetta_proof_builder_state(CettaProofBuilder *b,AtomId key,uint32_t *out) {
    if(out)*out=CETTA_PROOF_NO_STATE;
    CettaProofStatus ready=writable(b);if(ready!=CETTA_PROOF_OK)return ready;
    PROOF_OWNERSHIP_GUARD(ownership);
    if(!ownership_enter(&ownership,b->image->universe))return poison(b,CETTA_PROOF_CAPACITY);
    if(!out || !tu_hdr(b->image->universe,key))return poison(b,CETTA_PROOF_INVALIDATED);
    uint32_t found;
    if(cetta_u64_index_find(&b->target_index,key,&found)){*out=found;return CETTA_PROOF_DUPLICATE;}
    CettaProofImage *im=b->image;
    if(b->rank==UINT32_MAX || b->rank>=im->limits.max_rank || im->stats.states>=im->limits.max_states || im->stats.states==UINT32_MAX)
        return poison(b,CETTA_PROOF_CAPACITY);
    if(!reserve((void **)&im->states,&im->state_capacity,(size_t)im->stats.states+1u,sizeof(*im->states)))
        return poison(b,CETTA_PROOF_CAPACITY);
    uint32_t index=im->stats.states;
    if(cetta_u64_index_insert(&b->target_index,key,index)!=CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return poison(b,CETTA_PROOF_CAPACITY);
    if(!acquire_root(im,key))return poison(b,CETTA_PROOF_CAPACITY);
    im->states[index]=(ProofState){.key=key,.rank=b->rank+1u,.incoming=NO_EDGE,.outgoing=NO_EDGE};
    im->stats.states++;im->stats.max_rank=b->rank+1u;*out=index;return CETTA_PROOF_OK;
}
static bool edge_lookup(const CettaProofImage *im,const ProofState *parent,AtomId label,uint32_t *out) {
    if(parent->directory)return cetta_u64_index_find(parent->directory,label,out);
    uint32_t visits=0u;
    for(uint32_t e=parent->outgoing;e!=NO_EDGE;e=im->edges[e].next_out) {
        assert(visits++<INLINE_OUTGOING);
        if(im->edges[e].label==label){*out=e;return true;}
    }
    return false;
}
CettaProofStatus cetta_proof_builder_offer(CettaProofBuilder *b,uint32_t parent,
        AtomId label,uint32_t child,CettaProofOffer *out) {
    if(out)*out=(CettaProofOffer){.edge=NO_EDGE};
    CettaProofStatus ready=writable(b);if(ready!=CETTA_PROOF_OK)return ready;
    CettaProofImage *im=b->image;
    PROOF_OWNERSHIP_GUARD(ownership);
    if(!ownership_enter(&ownership,im->universe))return poison(b,CETTA_PROOF_CAPACITY);
    if(!out || parent>=im->stats.states || child>=im->stats.states || parent>=child ||
       im->states[parent].rank!=b->rank || im->states[child].rank!=b->rank+1u)
        return poison(b,CETTA_PROOF_INVALIDATED);
    CettaProofStatus label_status=prepare_label(im,label,&label);
    if(label_status!=CETTA_PROOF_OK)return poison(b,label_status);
    uint64_t occurrences,total,words;uint64_t batch=im->states[parent].support;
    if(im->stats.producer_offers==UINT64_MAX || !checked_add(im->stats.producer_occurrences,batch,im->limits.max_producer_occurrences,&occurrences))
        return poison(b,CETTA_PROOF_CAPACITY);
    uint32_t have;
    if(edge_lookup(im,&im->states[parent],label,&have)) {
        if(im->edges[have].child!=child)return poison(b,CETTA_PROOF_AMBIGUOUS);
        im->stats.producer_offers++;im->stats.producer_occurrences=occurrences;
        *out=(CettaProofOffer){.edge=have,.producer_occurrences=batch};return CETTA_PROOF_DUPLICATE;
    }
    if(im->stats.edges>=im->limits.max_edges || im->stats.edges==UINT32_MAX ||
       !checked_add(im->states[child].support,batch,im->limits.max_support,&words) ||
       !checked_add(im->stats.support,batch,im->limits.max_support,&total))return poison(b,CETTA_PROOF_CAPACITY);
    if(!reserve((void **)&im->edges,&im->edge_capacity,(size_t)im->stats.edges+1u,sizeof(*im->edges)))
        return poison(b,CETTA_PROOF_CAPACITY);
    CettaU64Index *map=im->states[parent].directory,*prepared=NULL;
    if(!map && im->states[parent].degree==INLINE_OUTGOING) {
        prepared=cetta_fallible_calloc(1u,sizeof(*prepared));if(!prepared)return poison(b,CETTA_PROOF_CAPACITY);
        cetta_u64_index_init(prepared);
        for(uint32_t e=im->states[parent].outgoing;e!=NO_EDGE;e=im->edges[e].next_out)
            if(cetta_u64_index_insert(prepared,im->edges[e].label,e)!=CETTA_GSLT_U32_INDEX_INSERTED_V1) {
                cetta_u64_index_free(prepared);free(prepared);return poison(b,CETTA_PROOF_CAPACITY);
            }
        map=prepared;
    }
    uint32_t index=im->stats.edges;
    if(map && cetta_u64_index_insert(map,label,index)!=CETTA_GSLT_U32_INDEX_INSERTED_V1) {
        if(prepared){cetta_u64_index_free(prepared);free(prepared);}
        return poison(b,CETTA_PROOF_CAPACITY);
    }
    if(!acquire_root(im,label)) {
        if(prepared){cetta_u64_index_free(prepared);free(prepared);}
        return poison(b,CETTA_PROOF_CAPACITY);
    }
    im->edges[index]=(ProofEdge){.label=label,.parent=parent,.child=child,
        .next_in=im->states[child].incoming,.next_out=im->states[parent].outgoing};
    if(prepared)im->states[parent].directory=prepared;
    im->states[child].incoming=index;im->states[child].support=words;
    im->states[parent].outgoing=index;im->states[parent].degree++;
    im->stats.edges++;im->stats.support=total;im->stats.producer_offers++;im->stats.producer_occurrences=occurrences;
    *out=(CettaProofOffer){.edge=index,.support_changed=true,.added_support=batch,.producer_occurrences=batch};return CETTA_PROOF_OK;
}
CettaProofStatus cetta_proof_builder_advance(CettaProofBuilder *b) {
    CettaProofStatus ready=writable(b);if(ready!=CETTA_PROOF_OK)return ready;
    if(b->rank==UINT32_MAX)return poison(b,CETTA_PROOF_CAPACITY);
    clear_directories(b->image,b->first,b->past_last);
    b->rank++;b->first=b->past_last;b->past_last=b->image->stats.states;
    cetta_u64_index_free(&b->target_index);cetta_u64_index_init(&b->target_index);return CETTA_PROOF_OK;
}
bool cetta_proof_builder_layer(const CettaProofBuilder *b,uint32_t *first,uint32_t *last) {
    if(!b || !first || !last || b->error!=CETTA_PROOF_OK || b->sealed || !current(b->image))return false;
    *first=b->first;*last=b->past_last;return true;
}
static bool state_view(const CettaProofImage *im,uint32_t index,CettaProofStateView *out) {
    if(!current(im) || !out || index>=im->stats.states)return false;
    const ProofState *s=&im->states[index];*out=(CettaProofStateView){index,s->rank,s->key,s->support};return true;
}
bool cetta_proof_builder_get(const CettaProofBuilder *b,uint32_t index,CettaProofStateView *out) {
    return b && b->error==CETTA_PROOF_OK && state_view(b->image,index,out);
}
CettaProofStatus cetta_proof_builder_seal(CettaProofBuilder *b,CettaProofImage **out) {
    if(out)*out=NULL;
    CettaProofStatus ready=writable(b);if(ready!=CETTA_PROOF_OK)return ready;
    if(!out)return poison(b,CETTA_PROOF_INVALIDATED);
    if(!cetta_proof_image_retain(b->image))return poison(b,CETTA_PROOF_CAPACITY);
    clear_directories(b->image,b->first,b->past_last);
    b->sealed=true;b->image->sealed=true;*out=b->image;return CETTA_PROOF_OK;
}
void cetta_proof_builder_free(CettaProofBuilder *b) {
    if(!b)return;
    cetta_u64_index_free(&b->target_index);cetta_proof_image_release(b->image);free(b);
}
CettaProofStats cetta_proof_image_stats(const CettaProofImage *im) {
    return im && im->sealed?im->stats:(CettaProofStats){0};
}
bool cetta_proof_image_state(const CettaProofImage *im,uint32_t index,CettaProofStateView *out) {
    return im && im->sealed && state_view(im,index,out);
}
bool cetta_proof_image_edge(const CettaProofImage *im,uint32_t index,CettaProofEdgeView *out) {
    if(!current(im) || !im->sealed || !out || index>=im->stats.edges)return false;
    const ProofEdge *e=&im->edges[index];*out=(CettaProofEdgeView){e->parent,e->child,e->label};return true;
}
static CettaProofStatus allocate_cursor(CettaProofImage *im,uint32_t selected,CettaProofCursor **out) {
    *out=NULL;
    if(!current(im) || !im->sealed || (selected!=CETTA_PROOF_ALL_STATES && selected>=im->stats.states))return CETTA_PROOF_INVALIDATED;
    if(im->stats.max_rank==UINT32_MAX)return CETTA_PROOF_CAPACITY;
    uint32_t capacity=im->stats.max_rank+1u;
    if((size_t)capacity>SIZE_MAX/sizeof(ProofFrame) || (size_t)capacity>SIZE_MAX/sizeof(AtomId))return CETTA_PROOF_CAPACITY;
    CettaProofCursor *cursor=cetta_fallible_calloc(1u,sizeof(*cursor));
    if(!cursor)return CETTA_PROOF_CAPACITY;
    cursor->frames=cetta_fallible_malloc((size_t)capacity*sizeof(*cursor->frames));
    cursor->labels=cetta_fallible_malloc((size_t)capacity*sizeof(*cursor->labels));
    if(!cursor->frames || !cursor->labels || !cetta_proof_image_retain(im)) {
        free(cursor->frames);free(cursor->labels);free(cursor);return CETTA_PROOF_CAPACITY;
    }
    cursor->image=im;cursor->capacity=capacity;cursor->selected=selected;
    cursor->next_state=selected==CETTA_PROOF_ALL_STATES?0u:selected;*out=cursor;return CETTA_PROOF_OK;
}
CettaProofStatus cetta_proof_cursor_new(CettaProofImage *im,uint32_t selected,CettaProofCursor **out) {
    if(!out)return CETTA_PROOF_INVALIDATED;
    return allocate_cursor(im,selected,out);
}
CettaProofStatus cetta_proof_cursor_clone(const CettaProofCursor *old,CettaProofCursor **out) {
    if(out)*out=NULL;
    if(!old || !out)return CETTA_PROOF_INVALIDATED;
    CettaProofCursor *cursor=NULL;CettaProofStatus status=allocate_cursor(old->image,old->selected,&cursor);
    if(status!=CETTA_PROOF_OK)return status;
    ProofFrame *frames=cursor->frames;AtomId *labels=cursor->labels;
    *cursor=*old;cursor->frames=frames;cursor->labels=labels;
    if(old->length)memcpy(frames,old->frames,(size_t)old->length*sizeof(*frames));
    if(old->length>1u)memcpy(labels,old->labels,(size_t)(old->length-1u)*sizeof(*labels));
    *out=cursor;return CETTA_PROOF_OK;
}
static bool tick(CettaProofBudget *budget) {
    if(!budget->remaining)return false;
    budget->remaining--;if(budget->visited!=UINT64_MAX)budget->visited++;return true;
}
static void pending_view(const CettaProofCursor *c,CettaProofWord *out) {
    *out=(CettaProofWord){.state_key=c->image->states[c->current_state].key,.seed=c->image->seed,
        .outer_labels=c->labels,.label_count=c->length-1u,.state=c->current_state,.token=c->token};
}
CettaProofNext cetta_proof_cursor_peek(CettaProofCursor *c,CettaProofBudget *budget,CettaProofWord *out) {
    if(out)*out=(CettaProofWord){0};
    if(!c || !budget || !out || !current(c->image))return CETTA_PROOF_CURSOR_INVALIDATED;
    if(c->finished)return CETTA_PROOF_END;
    if(c->pending) {
        if(!tick(budget))return CETTA_PROOF_PAUSED;
        pending_view(c,out);return CETTA_PROOF_ROW;
    }
    for(;;) {
        if(!tick(budget))return CETTA_PROOF_PAUSED;
        if(c->pop_after_commit) {
            assert(c->length);c->length--;c->pop_after_commit=false;
            if(!c->length)c->active=false;
            continue;
        }
        if(!c->active) {
            if(c->next_state>=c->image->stats.states ||
               (c->selected!=CETTA_PROOF_ALL_STATES && c->next_state!=c->selected)) {
                c->finished=true;return CETTA_PROOF_END;
            }
            uint32_t selected=c->next_state++;
            if(!c->image->states[selected].support)continue;
            c->current_state=selected;c->length=1u;
            c->frames[0]=(ProofFrame){selected,c->image->states[selected].incoming};c->active=true;continue;
        }
        assert(c->length);
        ProofFrame *top=&c->frames[c->length-1u];
        if(top->node==0u) {
            if(c->token==UINT64_MAX)return CETTA_PROOF_CURSOR_INVALIDATED;
            c->token++;c->pending=true;pending_view(c,out);return CETTA_PROOF_ROW;
        }
        if(top->edge==NO_EDGE) {
            c->length--;if(!c->length)c->active=false;continue;
        }
        uint32_t index=top->edge;const ProofEdge *e=&c->image->edges[index];top->edge=e->next_in;
        if(!c->image->states[e->parent].support)continue;
        if(c->length==c->capacity)return CETTA_PROOF_CURSOR_INVALIDATED;
        c->labels[c->length-1u]=e->label;
        c->frames[c->length++]=(ProofFrame){e->parent,c->image->states[e->parent].incoming};
    }
}
bool cetta_proof_cursor_commit(CettaProofCursor *c,uint64_t token) {
    if(!c || !c->pending || token!=c->token)return false;
    c->pending=false;c->pop_after_commit=true;return true;
}
void cetta_proof_cursor_free(CettaProofCursor *cursor) {
    if(!cursor)return;
    cetta_proof_image_release(cursor->image);free(cursor->frames);free(cursor->labels);free(cursor);
}

bool cetta_proof_image_matches_universe(const CettaProofImage *image,
                                        const TermUniverse *universe) {
    return image && universe && image->universe == universe && current(image);
}
size_t cetta_proof_image_retained_bytes(const CettaProofImage *image) {
    if (!image) return 0u;
    size_t states, edges, roots, branches, bytes;
    if (__builtin_mul_overflow(image->state_capacity, sizeof(*image->states), &states) ||
        __builtin_mul_overflow(image->edge_capacity, sizeof(*image->edges), &edges) ||
        __builtin_add_overflow(sizeof(*image), states, &bytes) ||
        __builtin_add_overflow(bytes, edges, &bytes) ||
        __builtin_mul_overflow(image->root_capacity, sizeof(*image->root_ids), &roots) ||
        __builtin_mul_overflow((size_t)image->root_index.capacity,
                               sizeof(*image->root_index.branches), &branches) ||
        __builtin_add_overflow(bytes, roots, &bytes) ||
        __builtin_add_overflow(bytes, branches, &bytes) ||
        __builtin_add_overflow(bytes,
            cetta_gslt_u32_index_bytes_v1(&image->root_index.prefixes), &bytes)) return SIZE_MAX;
    for (uint32_t i = 0u; i < image->root_index.length; i++)
        if (__builtin_add_overflow(bytes,
            cetta_gslt_u32_index_bytes_v1(&image->root_index.branches[i]), &bytes)) return SIZE_MAX;
    return bytes;
}
