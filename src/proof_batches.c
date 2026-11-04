#include "proof_batches.h"
#include "fallible_alloc.h"
#include "u64_index.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_ITEMS 32u
#define MAX_RANK CETTA_PROOF_ROW_LABEL_LIMIT
#define MEMBER(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))

typedef struct Link { struct Link *left, *right; uint32_t height; } Link;
typedef int (*Compare)(const Link *, const Link *);
static uint32_t height(Link *p) { return p ? p->height : 0u;
     }
static void fix(Link *p) { uint32_t a=height(p->left),b=height(p->right);p->height=1u+(a>b?a:b); }
static Link *rotate_right(Link *p) { Link *q=p->left;p->left=q->right;q->right=p;fix(p);fix(q);return q;
     }
static Link *rotate_left(Link *p) { Link *q=p->right;p->right=q->left;q->left=p;fix(p);fix(q);return q;
     }
static Link *insert(Link *root,Link *item,Compare compare) {
    if(!root){item->left=item->right=NULL;item->height=1u;return item;}
    int c=compare(item,root);assert(c);
    if(c<0)root->left=insert(root->left,item,compare);else root->right=insert(root->right,item,compare);
    fix(root);
    if(height(root->left)>height(root->right)+1u){if(height(root->left->right)>height(root->left->left))root->left=rotate_left(root->left);return rotate_right(root);}
    if(height(root->right)>height(root->left)+1u){if(height(root->right->left)>height(root->right->right))root->right=rotate_right(root->right);return rotate_left(root);}
    return root;
}
static Link *find(Link *root,const Link *key,Compare compare) {
    while(root){int c=compare(key,root);if(!c)return root;
    root=c<0?root->left:root->right;}return NULL;
}
static int u64_compare(uint64_t a,uint64_t b) { return (a>b)-(a<b);
     }
static bool retain(_Atomic uint64_t *refs) {
    uint64_t n=atomic_load_explicit(refs,memory_order_relaxed);
    while(n && n!=UINT64_MAX){if(atomic_compare_exchange_weak_explicit(refs,&n,n+1u,memory_order_relaxed,memory_order_relaxed))return true;}return false;
}
static bool drop(_Atomic uint64_t *refs) { uint64_t old=atomic_fetch_sub_explicit(refs,1u,memory_order_acq_rel);assert(old);return old==1u;
     }
static _Atomic uint64_t next_identity=1u;
static uint64_t fresh_identity(void) {
    uint64_t n=atomic_load(&next_identity);
    while(n!=UINT64_MAX) if(atomic_compare_exchange_weak(&next_identity,&n,n+1u))return n;
    return 0u;
}
static _Atomic uint64_t live_stores,live_transactions,live_views,live_cursors,live_chunks,live_languages,live_roots,live_bytes;
static void *allocate(size_t bytes,_Atomic uint64_t *counter) {
    void *p=cetta_fallible_calloc(1u,bytes);if(p){atomic_fetch_add(&live_bytes,bytes);if(counter)atomic_fetch_add(counter,1u);}return p;
}
static void dispose(void *p,size_t bytes,_Atomic uint64_t *counter) {
    if(!p)return;
    atomic_fetch_sub(&live_bytes,bytes);if(counter)atomic_fetch_sub(counter,1u);free(p);
}
CettaProofBatchDiagnostics cetta_proof_batch_diagnostics(void) {
    return (CettaProofBatchDiagnostics){atomic_load(&live_stores),atomic_load(&live_transactions),atomic_load(&live_views),atomic_load(&live_cursors),atomic_load(&live_chunks),atomic_load(&live_languages),atomic_load(&live_roots),atomic_load(&live_bytes)};
}
typedef struct { TermUniverse *universe; } Ownership;
static bool enter(Ownership *g,TermUniverse *u) {
    g->universe=NULL;uint32_t n=__atomic_load_n(&u->collection_inhibit,__ATOMIC_RELAXED);
    while(n!=UINT32_MAX)if(__atomic_compare_exchange_n(&u->collection_inhibit,&n,n+1u,true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){g->universe=u;return true;}
    return false;
}
static void leave(Ownership *g) { if(g->universe){term_universe_collection_inhibit_leave(g->universe);g->universe=NULL;} }

typedef struct Pool Pool;
typedef struct Root {
    _Atomic uint64_t refs;
    Pool *pool; AtomId id;
    Link index;
    struct Root *pool_previous,*pool_next,*all_next;
} Root;
struct Pool {
    _Atomic uint64_t refs;
    TermUniverse *universe;
    uint64_t instance,epoch;
    CettaProofBatchLifetime lifetime;
    Root *roots;
    bool registered;
};
static bool current(const Pool *p) { return p && p->universe->instance_id==p->instance && p->universe->storage_epoch==p->epoch;
     }
static void pool_roots(const TermUniverse *u,TermUniverseRootVisit visit,void *visitor,void *context) {
    (void)u;Pool *p=context;if(!current(p))return;
    for(Root *r=p->roots;r;r=r->pool_next)visit(visitor,r->id);
}
static void pool_release(Pool *p) {
    if(!p || !drop(&p->refs))return;
    assert(!p->roots);
    Ownership g={0};bool same=p->universe->instance_id==p->instance;
    if(same){(void)enter(&g,p->universe);if(p->registered)term_universe_remove_root_provider(p->universe,pool_roots,p);}
    CettaProofBatchLifetime life=p->lifetime;leave(&g);dispose(p,sizeof(*p),NULL);life.release(life.owner);
}
static Root *root_new(Pool *p,AtomId id) {
    Root *r=allocate(sizeof(*r),&live_roots);if(!r)return NULL;
    if(!retain(&p->refs)){dispose(r,sizeof(*r),&live_roots);return NULL;}
    Ownership g={0};if(!current(p)||!enter(&g,p->universe)){pool_release(p);dispose(r,sizeof(*r),&live_roots);return NULL;}
    atomic_init(&r->refs,1u);r->pool=p;r->id=id;
    if(p->universe->counting)term_universe_hold_counted(p->universe,id);
    r->pool_next=p->roots;if(p->roots)p->roots->pool_previous=r;p->roots=r;leave(&g);return r;
}
static void root_release(Root *r) {
    if(!r || !drop(&r->refs))return;
    Pool *p=r->pool;Ownership g={0};(void)enter(&g,p->universe);
    if(r->pool_previous)r->pool_previous->pool_next=r->pool_next;else p->roots=r->pool_next;
    if(r->pool_next)r->pool_next->pool_previous=r->pool_previous;
    if(current(p)&&p->universe->counting)term_universe_release_counted(p->universe,r->id);
    leave(&g);dispose(r,sizeof(*r),&live_roots);pool_release(p);
}
static int root_compare(const Link *a,const Link *b) { return u64_compare(MEMBER(a,Root,index)->id,MEMBER(b,Root,index)->id);
     }

struct CettaProofBatchLanguage {
    _Atomic uint64_t refs;
    Root *label;
    struct CettaProofBatchLanguage *previous,*parent,*retire_next;
    uint64_t support;
    uint32_t rank,walk_depth;
    bool seed;
};
typedef CettaProofBatchLanguage Language;
static void language_release(Language *p) {
    if(!p || !drop(&p->refs))return;
    p->retire_next=NULL;Language *pending=p;
    while(pending){p=pending;pending=p->retire_next;
        Language *edges[2]={p->previous,p->parent};
        for(unsigned i=0u;i<2u;i++)if(edges[i]&&drop(&edges[i]->refs)){edges[i]->retire_next=pending;pending=edges[i];}
        Root *label=p->label;dispose(p,sizeof(*p),&live_languages);root_release(label);
    }
}
static Language *language_new(Root *label,Language *previous,Language *parent,uint64_t count,uint32_t rank) {
    Language *p=allocate(sizeof(*p),&live_languages);if(!p)return NULL;
    atomic_init(&p->refs,1u);
    if(!retain(&label->refs)){dispose(p,sizeof(*p),&live_languages);return NULL;}p->label=label;
    if(previous && !retain(&previous->refs)){language_release(p);return NULL;}p->previous=previous;
    if(parent && !retain(&parent->refs)){language_release(p);return NULL;}p->parent=parent;
    p->seed=!parent;p->rank=rank;p->support=count;
    if(!parent){p->walk_depth=1u;return p;}
    uint32_t a=previous?previous->walk_depth:0u,b=parent?parent->walk_depth:0u;
    if(a==UINT32_MAX||b>=UINT32_MAX-1u){language_release(p);return NULL;}
    p->walk_depth=1u+(a>b+1u?a:b+1u);return p;
}
typedef struct {
    Root *key;
    Language *language;
    uint32_t state,rank;
    uint64_t first,count;
    bool seed;
} Entry;
typedef struct Chunk {
    _Atomic uint64_t refs;
    struct Chunk *previous;
    uint32_t used,count,max_walk;
    uint64_t past;
    Entry entries[CHUNK_ITEMS];
} Chunk;
static void chunk_release(Chunk *p) {
    while(p && drop(&p->refs)){
        Chunk *previous=p->previous;
        for(uint32_t i=0u;i<p->used;i++){root_release(p->entries[i].key);language_release(p->entries[i].language);}
        dispose(p,sizeof(*p),&live_chunks);p=previous;
    }
}
static Chunk *chunk_new(Chunk *previous) {
    if(previous&&previous->count==UINT32_MAX)return NULL;
    Chunk *p=allocate(sizeof(*p),&live_chunks);if(!p)return NULL;
    atomic_init(&p->refs,1u);
    if(previous&&!retain(&previous->refs)){dispose(p,sizeof(*p),&live_chunks);return NULL;}
    if(previous){p->previous=previous;p->count=previous->count+1u;p->past=previous->past;p->max_walk=previous->max_walk;}else p->count=1u;
    return p;
}
static bool entry_add(Chunk *p,Root *key,Language *language,uint32_t state,uint32_t rank,uint64_t first,uint64_t count,bool seed) {
    assert(p->used<CHUNK_ITEMS);
    if(!retain(&key->refs))return false;
    if(!retain(&language->refs)){root_release(key);return false;}
    p->entries[p->used++]=(Entry){key,language,state,rank,first,count,seed};p->past=first+count;
    uint32_t walk=seed?1u:language->parent->walk_depth+1u;if(walk>p->max_walk)p->max_walk=walk;return true;
}
typedef struct State {
    Link key_link,id_link;
    Root *key;Language *language;
    uint32_t id,rank;
    struct State *all_next,*rank_next;
} State;
typedef struct Edge {
    Link index;State *parent,*child;Root *label;
    struct Edge *all_next;
} Edge;
typedef struct PendingState {
    Link index;
    State *target;Language *language;
    bool fresh;
    struct PendingState *next;
} PendingState;
struct CettaProofBatchStore {
    _Atomic uint64_t refs;
    Pool *pool;CettaProofRowLayout layout;CettaProofLimits limits;
    CettaProofBatchStats stats;
    uint64_t identity;
    size_t retained_bytes;
    CettaProofFieldPath counter_path;
    int64_t root_counter;
    bool counter_bound;
    Link *states,*ids,*edges,*atoms;
    State *state_list,*rank_first[MAX_RANK+1u],*rank_last[MAX_RANK+1u];
    Edge *edge_list;Root *atom_list;
    bool closed[MAX_RANK+1u];
    CettaProofBatchView *sealed[MAX_RANK+1u];
    Chunk *tail;
};
struct CettaProofBatchTransaction {
    CettaProofBatchStore *store;
    uint64_t version;
    CettaProofBatchStatus error;
    CettaProofBatchResult result;
    Link *states,*edges,*atoms;
    PendingState *state_list,*state_last;Edge *edge_list;Root *atom_list;
    Chunk *tail;
    bool committed;
};
typedef struct { Root *key;Language *language;uint32_t state,rank; } Group;
struct CettaProofBatchView {
    _Atomic uint64_t refs;
    Pool *pool;CettaProofRowLayout layout;
    uint64_t store_identity,count;
    Chunk *tail;
    Group *groups;
    uint32_t group_count,group_capacity,rank,max_walk;
    bool ranked,sealed;
};
static int state_compare(const Link *a,const Link *b) {
    const State *x=MEMBER(a,State,key_link),*y=MEMBER(b,State,key_link);
    int c=u64_compare(x->rank,y->rank);return c?c:u64_compare(x->key->id,y->key->id);
}
static int id_compare(const Link *a,const Link *b) { return u64_compare(MEMBER(a,State,id_link)->id,MEMBER(b,State,id_link)->id);
     }
static int edge_compare(const Link *a,const Link *b) {
    const Edge *x=MEMBER(a,Edge,index),*y=MEMBER(b,Edge,index);int c=u64_compare(x->parent->id,y->parent->id);return c?c:u64_compare(x->label->id,y->label->id);
}
static int pending_compare(const Link *a,const Link *b) {
    const PendingState *x=MEMBER(a,PendingState,index),*y=MEMBER(b,PendingState,index);
    return state_compare(&x->target->key_link,&y->target->key_link);
}
static State *state_find(CettaProofBatchStore *s,uint32_t rank,AtomId key) {
    Root r={.id=key};State probe={.rank=rank,.key=&r};Link *p=find(s->states,&probe.key_link,state_compare);return p?MEMBER(p,State,key_link):NULL;
}
static State *id_find(CettaProofBatchStore *s,uint32_t id) { State probe={.id=id};Link *p=find(s->ids,&probe.id_link,id_compare);return p?MEMBER(p,State,id_link):NULL;
     }
static Root *root_find(Link *tree,AtomId id) { Root probe={.id=id};Link *p=find(tree,&probe.index,root_compare);return p?MEMBER(p,Root,index):NULL;
     }
static bool exact_label(TermUniverse *u,AtomId id) {
    const CettaTermHdr *h=tu_hdr(u,id);return h&&(h->tag==ATOM_SYMBOL||(h->tag==ATOM_GROUNDED&&h->subtag==GV_INT));
}
static bool valid_layout(TermUniverse *u,const CettaProofRowLayout *l,AtomId key,uint32_t rank) {
    if(!l||!l->proof.length||l->proof.length>CETTA_PROOF_PATH_MAX||rank>MAX_RANK||u->instance_id!=l->universe_instance||u->storage_epoch!=l->storage_epoch)return false;
    uint32_t slots=2u*rank;
    for(uint32_t i=0u;i<l->proof.length;i++){
        uint32_t n=l->arity[i],c=l->proof.child[i];bool last=i+1u==l->proof.length;
        if(n<2u||n>CETTA_PROOF_ROW_CHILD_LIMIT||!c||c>=n||n>CETTA_PROOF_ROW_CHILD_LIMIT-slots||tu_kind(u,key)!=ATOM_EXPR||tu_arity(u,key)!=n-(last?1u:0u))return false;
        AtomId h=tu_child(u,key,0u);if(tu_kind(u,h)!=ATOM_SYMBOL||tu_sym(u,h)!=l->head[i])return false;
    slots+=n;if(!last)key=tu_child(u,key,c);
    }return true;
}
/* Follow an original field path through the fixed structural omission. The
 * one deleted proof child shifts later siblings only at its own parent. */
static bool rank_integer(const CettaProofBatchStore *s, AtomId key,
    const CettaProofFieldPath *path, int64_t *out) {
    if (!path || !path->length || path->length > CETTA_PROOF_PATH_MAX) return false;
    bool proof_prefix = true;
    const CettaProofFieldPath *proof = &s->layout.proof;
    TermUniverse *u = s->pool->universe;
    for (uint32_t i = 0u; i < path->length; i++) {
        uint32_t original = path->child[i], child = original;
        if (!original || tu_kind(u, key) != ATOM_EXPR) return false;
        if (proof_prefix && i + 1u == proof->length) {
            if (original == proof->child[i]) return false;
            if (original > proof->child[i]) child--;
        }
        if (child >= tu_arity(u, key)) return false;
        key = tu_child(u, key, child);
        proof_prefix = proof_prefix && i < proof->length && original == proof->child[i];
    }
    const CettaTermHdr *h = tu_hdr(u, key);
    if (!h || h->tag != ATOM_GROUNDED || h->subtag != GV_INT) return false;
    *out = tu_int_from_header(h); return true;
}
static bool same_path(const CettaProofFieldPath *a, const CettaProofFieldPath *b) {
    return a && b && a->length == b->length && a->length <= CETTA_PROOF_PATH_MAX &&
        !memcmp(a->child, b->child, (size_t)a->length * sizeof(*a->child));
}
static bool counter_key(const CettaProofBatchStore *s, AtomId key, uint32_t generation) {
    int64_t value;
    return !s->counter_bound || (s->root_counter >= (int64_t)generation &&
        rank_integer(s, key, &s->counter_path, &value) && value == s->root_counter - generation);
}
CettaProofBatchStatus cetta_proof_batch_store_bind_rank(CettaProofBatchStore *s,
    const CettaProofFieldPath *path, int64_t *root_counter) {
    if (!s || !current(s->pool)) return CETTA_PROOF_BATCH_INVALIDATED;
    if (s->counter_bound) {
        if (!same_path(path, &s->counter_path)) return CETTA_PROOF_BATCH_DECLINED;
        if (root_counter) *root_counter = s->root_counter;
        return CETTA_PROOF_BATCH_OK;
    }
    int64_t value;
    if (s->stats.states != 1u || s->stats.batches || !s->rank_first[0] ||
        !rank_integer(s, s->rank_first[0]->key->id, path, &value) || value < 0)
        return CETTA_PROOF_BATCH_DECLINED;
    if (s->stats.version == UINT64_MAX) return CETTA_PROOF_BATCH_CAPACITY;
    s->counter_path = *path; s->root_counter = value; s->counter_bound = true; s->stats.version++;
    if (root_counter) *root_counter = value;
    return CETTA_PROOF_BATCH_OK;
}
bool cetta_proof_batch_store_rank_generation(const CettaProofBatchStore *s,
    const CettaProofFieldPath *path, int64_t source_counter, int64_t target_counter,
    uint32_t *generation) {
    if (!s || !current(s->pool) || !generation || !s->counter_bound ||
        !same_path(path, &s->counter_path) || source_counter <= 0 ||
        source_counter > s->root_counter || target_counter != source_counter - 1)
        return false;
    uint64_t rank = (uint64_t)(s->root_counter - source_counter);
    if (rank >= s->limits.max_rank) return false;
    *generation = (uint32_t)rank; return true;
}

static CettaProofBatchStatus pure_key(TermUniverse *u,AtomId id,CettaU64Index *seen,uint32_t depth) {
    uint32_t ignored;if(cetta_u64_index_find(seen,id,&ignored))return CETTA_PROOF_BATCH_OK;
    const CettaTermHdr *h=tu_hdr(u,id);if(!h||depth>512u)return CETTA_PROOF_BATCH_DECLINED;
    if(h->tag==ATOM_VAR){if(tu_var_name_key_id(u,id)!=CETTA_ATOM_ID_NONE)return CETTA_PROOF_BATCH_DECLINED;}
    else if(h->tag==ATOM_GROUNDED){if(h->subtag!=GV_INT)return CETTA_PROOF_BATCH_DECLINED;}
    else if(h->tag==ATOM_EXPR){for(uint32_t i=0u;i<tu_arity(u,id);i++){CettaProofBatchStatus r=pure_key(u,tu_child(u,id,i),seen,depth+1u);if(r!=CETTA_PROOF_BATCH_OK)return r;}}
    else if(h->tag!=ATOM_SYMBOL)return CETTA_PROOF_BATCH_DECLINED;
    return cetta_u64_index_insert(seen,id,0u)==CETTA_GSLT_U32_INDEX_INSERTED_V1?CETTA_PROOF_BATCH_OK:CETTA_PROOF_BATCH_CAPACITY;
}
static CettaProofBatchStatus check_key(CettaProofBatchStore *s,AtomId key,uint32_t rank) {
    if(!valid_layout(s->pool->universe,&s->layout,key,rank))return CETTA_PROOF_BATCH_DECLINED;
    CettaU64Index seen;cetta_u64_index_init(&seen);CettaProofBatchStatus r=pure_key(s->pool->universe,key,&seen,0u);cetta_u64_index_free(&seen);return r;
}
static Root *store_root(CettaProofBatchStore *s,AtomId id) {
    Root *r=root_find(s->atoms,id);if(r)return r;
    r=root_new(s->pool,id);if(!r)return NULL;
    s->atoms=insert(s->atoms,&r->index,root_compare);r->all_next=s->atom_list;s->atom_list=r;return r;
}
static void add_bytes(CettaProofBatchStore *,size_t,size_t);
void cetta_proof_batch_store_release(CettaProofBatchStore *s) {
    if(!s||!drop(&s->refs))return;
    for(uint32_t i=0u;i<=MAX_RANK;i++)cetta_proof_batch_view_release(s->sealed[i]);
    for(State *p=s->state_list;p;){State *next=p->all_next;language_release(p->language);dispose(p,sizeof(*p),NULL);p=next;}
    for(Edge *p=s->edge_list;p;){Edge *next=p->all_next;dispose(p,sizeof(*p),NULL);p=next;}
    chunk_release(s->tail);
    for(Root *p=s->atom_list;p;){Root *next=p->all_next;root_release(p);p=next;}
    Pool *pool=s->pool;dispose(s,sizeof(*s),&live_stores);pool_release(pool);
}
CettaProofBatchStatus cetta_proof_batch_store_new(TermUniverse *u,const CettaProofRowLayout *layout,AtomId key,AtomId seed,CettaProofLimits limits,CettaProofBatchLifetime life,CettaProofBatchStore **out) {
    if(!out||!u||!layout||!life.owner||!life.retain||!life.release||!limits.max_states||!limits.max_support||limits.max_rank>MAX_RANK||!exact_label(u,seed))return CETTA_PROOF_BATCH_DECLINED;
    Ownership guard={0};if(!enter(&guard,u))return CETTA_PROOF_BATCH_CAPACITY;
    if(!life.retain(life.owner)){leave(&guard);return CETTA_PROOF_BATCH_CAPACITY;}
    Pool *pool=allocate(sizeof(*pool),NULL);if(!pool){leave(&guard);life.release(life.owner);return CETTA_PROOF_BATCH_CAPACITY;}
    atomic_init(&pool->refs,1u);pool->universe=u;pool->instance=u->instance_id;pool->epoch=u->storage_epoch;pool->lifetime=life;
    if(!term_universe_add_counted_root_provider(u,pool_roots,pool)){leave(&guard);pool_release(pool);return CETTA_PROOF_BATCH_CAPACITY;}pool->registered=true;
    CettaProofBatchStore *s=allocate(sizeof(*s),&live_stores);if(!s){leave(&guard);pool_release(pool);return CETTA_PROOF_BATCH_CAPACITY;}
    atomic_init(&s->refs,1u);s->pool=pool;s->layout=*layout;s->limits=limits;
    s->identity=fresh_identity();CettaProofBatchStatus status=check_key(s,key,limits.max_rank);
    if(!s->identity)status=CETTA_PROOF_BATCH_CAPACITY;
    Root *k=NULL,*r=NULL;State *state=NULL;Language *lang=NULL;
    if(status==CETTA_PROOF_BATCH_OK){k=store_root(s,key);r=store_root(s,seed);if(!k||!r)status=CETTA_PROOF_BATCH_CAPACITY;}
    if(status==CETTA_PROOF_BATCH_OK){state=allocate(sizeof(*state),NULL);lang=language_new(r,NULL,NULL,1u,0u);if(!state||!lang)status=CETTA_PROOF_BATCH_CAPACITY;}
    if(status==CETTA_PROOF_BATCH_OK){s->tail=chunk_new(NULL);if(!s->tail||!entry_add(s->tail,k,lang,0u,0u,0u,1u,true))status=CETTA_PROOF_BATCH_CAPACITY;}
    if(status!=CETTA_PROOF_BATCH_OK){language_release(lang);dispose(state,sizeof(*state),NULL);leave(&guard);cetta_proof_batch_store_release(s);return status;}
    state->key=k;state->language=lang;s->states=insert(NULL,&state->key_link,state_compare);s->ids=insert(NULL,&state->id_link,id_compare);s->state_list=s->rank_first[0]=s->rank_last[0]=state;
    s->retained_bytes=sizeof(*s)+sizeof(*pool)+sizeof(*state)+sizeof(*lang)+sizeof(*s->tail);
    for(Root *p=s->atom_list;p;p=p->all_next)add_bytes(s,1u,sizeof(*p));
    s->stats=(CettaProofBatchStats){.states=1u,.stored_rows=1u,.version=1u};leave(&guard);*out=s;return CETTA_PROOF_BATCH_OK;
}
static void add_bytes(CettaProofBatchStore *s,size_t count,size_t width) {
    size_t n;
    if(__builtin_mul_overflow(count,width,&n)||__builtin_add_overflow(s->retained_bytes,n,&s->retained_bytes))s->retained_bytes=SIZE_MAX;
}
size_t cetta_proof_batch_store_retained_bytes(const CettaProofBatchStore *s) { return s?s->retained_bytes:0u; }
bool cetta_proof_batch_store_matches_universe(const CettaProofBatchStore *s,const TermUniverse *u) { return s&&s->pool->universe==u&&current(s->pool); }
const CettaProofRowLayout *cetta_proof_batch_store_layout(const CettaProofBatchStore *s) { return s?&s->layout:NULL; }
bool cetta_proof_batch_transaction_for_store(const CettaProofBatchTransaction *t,const CettaProofBatchStore *s) { return t&&t->store==s&&!t->committed; }
CettaProofBatchStats cetta_proof_batch_store_stats(const CettaProofBatchStore *s) { return s?s->stats:(CettaProofBatchStats){0};
     }
bool cetta_proof_batch_view_current(const CettaProofBatchView *v) { return v&&current(v->pool);
     }
bool cetta_proof_batch_view_retain(CettaProofBatchView *v) { return v&&retain(&v->refs);
     }
void cetta_proof_batch_view_release(CettaProofBatchView *v) {
    if(!v||!drop(&v->refs))return;
    for(uint32_t i=0u;i<v->group_count;i++){root_release(v->groups[i].key);language_release(v->groups[i].language);}
    dispose(v->groups,(size_t)v->group_capacity*sizeof(*v->groups),NULL);chunk_release(v->tail);Pool *pool=v->pool;dispose(v,sizeof(*v),&live_views);pool_release(pool);
}
static CettaProofBatchView *view_new(CettaProofBatchStore *s) {
    CettaProofBatchView *v=allocate(sizeof(*v),&live_views);if(!v)return NULL;
    if(!retain(&s->pool->refs)){dispose(v,sizeof(*v),&live_views);return NULL;}
    atomic_init(&v->refs,1u);v->pool=s->pool;v->layout=s->layout;v->store_identity=s->identity;return v;
}
CettaProofBatchStatus cetta_proof_batch_capture(CettaProofBatchStore *s,CettaProofBatchView **out) {
    if(!s||!out||!current(s->pool))return CETTA_PROOF_BATCH_INVALIDATED;
    CettaProofBatchView *v=view_new(s);if(!v)return CETTA_PROOF_BATCH_CAPACITY;
    if(!retain(&s->tail->refs)){cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}
    v->tail=s->tail;v->count=s->stats.stored_rows;v->max_walk=s->tail->max_walk;*out=v;return CETTA_PROOF_BATCH_OK;
}
static CettaProofBatchStatus capture_rank(CettaProofBatchStore *s,uint32_t rank,bool seal,CettaProofBatchView **out) {
    if(!s||!out||!current(s->pool))return CETTA_PROOF_BATCH_INVALIDATED;
    if(rank>s->limits.max_rank)return CETTA_PROOF_BATCH_DECLINED;
    if(s->closed[rank]){if(!cetta_proof_batch_view_retain(s->sealed[rank]))return CETTA_PROOF_BATCH_CAPACITY;
    *out=s->sealed[rank];return CETTA_PROOF_BATCH_OK;}
    CettaProofBatchView *v=view_new(s);if(!v)return CETTA_PROOF_BATCH_CAPACITY;
    v->ranked=true;v->rank=rank;v->sealed=seal;
    uint32_t n=0u;for(State *p=s->rank_first[rank];p;p=p->rank_next)n++;
    if((size_t)n>SIZE_MAX/sizeof(*v->groups)){cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}
    if(n){v->group_capacity=n;v->groups=allocate((size_t)n*sizeof(*v->groups),NULL);if(!v->groups){cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}}
    for(State *p=s->rank_first[rank];p;p=p->rank_next){
        if(!retain(&p->key->refs)){cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}
        if(!retain(&p->language->refs)){root_release(p->key);cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}
        v->groups[v->group_count++]=(Group){p->key,p->language,p->id,p->rank};v->count+=p->language->support;if(p->language->walk_depth>v->max_walk)v->max_walk=p->language->walk_depth;
    }
    if(seal){if(s->stats.version==UINT64_MAX||!retain(&v->refs)){cetta_proof_batch_view_release(v);return CETTA_PROOF_BATCH_CAPACITY;}s->closed[rank]=true;s->sealed[rank]=v;s->stats.version++;add_bytes(s,1u,sizeof(*v));add_bytes(s,v->group_capacity,sizeof(*v->groups));}
    *out=v;return CETTA_PROOF_BATCH_OK;
}
CettaProofBatchStatus cetta_proof_batch_capture_rank(CettaProofBatchStore *s,uint32_t rank,CettaProofBatchView **out) { return capture_rank(s,rank,false,out);
     }
CettaProofBatchStatus cetta_proof_batch_seal_rank(CettaProofBatchStore *s,uint32_t rank,CettaProofBatchView **out) { return capture_rank(s,rank,true,out);
     }
uint64_t cetta_proof_batch_view_count(const CettaProofBatchView *v) { return v?v->count:0u;
     }
uint32_t cetta_proof_batch_view_groups(const CettaProofBatchView *v) { return v&&v->ranked?v->group_count:0u;
     }
bool cetta_proof_batch_view_group(const CettaProofBatchView *v,uint32_t i,CettaProofBatchGroup *out) {
    if(!out||!cetta_proof_batch_view_current(v)||!v->ranked||i>=v->group_count)return false;
    const Group *g=&v->groups[i];*out=(CettaProofBatchGroup){g->state,g->rank,g->key->id,g->language->support,g->language};return true;
}
const CettaProofRowLayout *cetta_proof_batch_view_layout(const CettaProofBatchView *v) { return v?&v->layout:NULL;
     }

static CettaProofBatchStatus transaction_error(CettaProofBatchTransaction *t,CettaProofBatchStatus status) {
    if(t->error==CETTA_PROOF_BATCH_OK)t->error=status;
    return t->error;
}
CettaProofBatchStatus cetta_proof_batch_begin(CettaProofBatchStore *s,CettaProofBatchTransaction **out) {
    if(!s||!out||!current(s->pool))return CETTA_PROOF_BATCH_INVALIDATED;
    CettaProofBatchTransaction *t=allocate(sizeof(*t),&live_transactions);if(!t)return CETTA_PROOF_BATCH_CAPACITY;
    if(!retain(&s->refs)){dispose(t,sizeof(*t),&live_transactions);return CETTA_PROOF_BATCH_CAPACITY;}
    t->store=s;t->version=s->stats.version;*out=t;return CETTA_PROOF_BATCH_OK;
}
static Root *transaction_root(CettaProofBatchTransaction *t,AtomId id) {
    Root *r=root_find(t->store->atoms,id);if(r)return r;
    r=root_find(t->atoms,id);if(r)return r;
    r=root_new(t->store->pool,id);if(!r)return NULL;
    t->atoms=insert(t->atoms,&r->index,root_compare);r->all_next=t->atom_list;t->atom_list=r;return r;
}
static PendingState *pending_find(CettaProofBatchTransaction *t,uint32_t rank,AtomId key) {
    Root r={.id=key};State s={.rank=rank,.key=&r};PendingState probe={.target=&s};Link *p=find(t->states,&probe.index,pending_compare);return p?MEMBER(p,PendingState,index):NULL;
}
static PendingState *pending_new(CettaProofBatchTransaction *t,uint32_t rank,AtomId key) {
    PendingState *p=pending_find(t,rank,key);if(p)return p;
    CettaProofBatchStore *s=t->store;State *target=state_find(s,rank,key);
    p=allocate(sizeof(*p),NULL);if(!p)return NULL;
    if(!target){
        if(t->result.new_states>=s->limits.max_states-s->stats.states){dispose(p,sizeof(*p),NULL);return NULL;}
        Root *root=transaction_root(t,key);if(!root){dispose(p,sizeof(*p),NULL);return NULL;}
        target=allocate(sizeof(*target),NULL);if(!target){dispose(p,sizeof(*p),NULL);return NULL;}
        target->id=s->stats.states+t->result.new_states;target->rank=rank;target->key=root;p->fresh=true;t->result.new_states++;
    }else if(!retain(&target->language->refs)){dispose(p,sizeof(*p),NULL);return NULL;}
    p->target=target;p->language=target->language;
    if(t->state_last)t->state_last->next=p;else t->state_list=p;
    t->state_last=p;t->states=insert(t->states,&p->index,pending_compare);return p;
}
static Edge *edge_find(Link *tree,State *parent,AtomId label) {
    Root r={.id=label};Edge probe={.parent=parent,.label=&r};Link *p=find(tree,&probe.index,edge_compare);return p?MEMBER(p,Edge,index):NULL;
}
static bool add_u64(uint64_t a,uint64_t b,uint64_t maximum,uint64_t *out) { if(a>maximum||b>maximum-a)return false;
    *out=a+b;return true;
     }
CettaProofBatchStatus cetta_proof_batch_offer(CettaProofBatchTransaction *t,const CettaProofBatchView *view,uint32_t group,AtomId label,AtomId key,CettaProofBatchOffer *out) {
    if(!t||!out||t->committed)return CETTA_PROOF_BATCH_DECLINED;
    if(t->error!=CETTA_PROOF_BATCH_OK)return t->error;
    CettaProofBatchStore *s=t->store;
    if(!current(s->pool)||!cetta_proof_batch_view_current(view))return transaction_error(t,CETTA_PROOF_BATCH_INVALIDATED);
    if(s->stats.version!=t->version)return transaction_error(t,CETTA_PROOF_BATCH_STALE);
    if(!view->ranked||!view->sealed||view->store_identity!=s->identity||group>=view->group_count)return transaction_error(t,CETTA_PROOF_BATCH_DECLINED);
    const Group *g=&view->groups[group];State *parent=id_find(s,g->state);
    if(!parent||parent->rank!=g->rank||!s->closed[parent->rank]||parent->language!=g->language||parent->key!=g->key)return transaction_error(t,CETTA_PROOF_BATCH_STALE);
    if(parent->rank>=s->limits.max_rank||!exact_label(s->pool->universe,label))return transaction_error(t,CETTA_PROOF_BATCH_DECLINED);
    uint64_t offers,occurrences,weight=parent->language->support;
    if(!add_u64(s->stats.producer_offers,t->result.producer_offers,UINT64_MAX,&offers)||offers==UINT64_MAX||
       !add_u64(s->stats.producer_occurrences,t->result.producer_occurrences,s->limits.max_producer_occurrences,&occurrences)||
       !add_u64(occurrences,weight,s->limits.max_producer_occurrences,&occurrences))return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);
    Edge *existing=edge_find(s->edges,parent,label);if(!existing)existing=edge_find(t->edges,parent,label);
    if(existing){
        if(existing->child->rank!=parent->rank+1u||existing->child->key->id!=key)return transaction_error(t,CETTA_PROOF_BATCH_CONFLICT);
        t->result.producer_offers++;t->result.producer_occurrences+=weight;
        *out=(CettaProofBatchOffer){existing->child->id,0u,weight,true};return CETTA_PROOF_BATCH_DUPLICATE;
    }
    uint32_t rank=parent->rank+1u;
    if(!counter_key(s,key,rank))return transaction_error(t,CETTA_PROOF_BATCH_DECLINED);
    if(s->closed[rank])return transaction_error(t,CETTA_PROOF_BATCH_SEALED);
    uint64_t rows;if(!add_u64(s->stats.stored_rows,t->result.appended_rows,s->limits.max_support,&rows)||
       !add_u64(rows,weight,s->limits.max_support,&rows)||t->result.new_batches>=s->limits.max_edges-s->stats.batches)return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);
    Ownership guard={0};if(!enter(&guard,s->pool->universe))return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);
    CettaProofBatchStatus status=check_key(s,key,s->limits.max_rank);
    if(status!=CETTA_PROOF_BATCH_OK){leave(&guard);return transaction_error(t,status);}
    PendingState *pending=pending_new(t,rank,key);Root *label_root=transaction_root(t,label);
    if(!pending||!label_root){leave(&guard);return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);}
    uint64_t support;
    if(!add_u64(pending->language?pending->language->support:0u,weight,s->limits.max_support,&support)){leave(&guard);return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);}
    Language *language=language_new(label_root,pending->language,parent->language,support,rank);
    Edge *edge=allocate(sizeof(*edge),NULL);
    if(!language||!edge){language_release(language);dispose(edge,sizeof(*edge),NULL);leave(&guard);return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);}
    if(!t->tail||t->tail->used==CHUNK_ITEMS){
        Chunk *chunk=chunk_new(t->tail?t->tail:s->tail);
        if(!chunk){language_release(language);dispose(edge,sizeof(*edge),NULL);leave(&guard);return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);}
        chunk_release(t->tail);t->tail=chunk;
    }
    if(!entry_add(t->tail,pending->target->key,language,pending->target->id,rank,rows-weight,weight,false)){
        language_release(language);dispose(edge,sizeof(*edge),NULL);leave(&guard);return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);
    }
    edge->parent=parent;edge->child=pending->target;edge->label=label_root;edge->all_next=t->edge_list;t->edge_list=edge;t->edges=insert(t->edges,&edge->index,edge_compare);
    language_release(pending->language);pending->language=language;
    t->result.new_batches++;t->result.appended_rows+=weight;t->result.producer_offers++;t->result.producer_occurrences+=weight;
    leave(&guard);*out=(CettaProofBatchOffer){pending->target->id,weight,weight,false};return CETTA_PROOF_BATCH_OK;
}
bool cetta_proof_batch_transaction_result(const CettaProofBatchTransaction *t,CettaProofBatchResult *out) {
    if(!t||!out||t->error!=CETTA_PROOF_BATCH_OK)return false;
    *out=t->result;return true;
}
CettaProofBatchStatus cetta_proof_batch_commit(CettaProofBatchTransaction *t) {
    if(!t||t->committed)return CETTA_PROOF_BATCH_DECLINED;
    if(t->error!=CETTA_PROOF_BATCH_OK)return t->error;
    CettaProofBatchStore *s=t->store;if(!current(s->pool))return transaction_error(t,CETTA_PROOF_BATCH_INVALIDATED);
    if(s->stats.version!=t->version)return transaction_error(t,CETTA_PROOF_BATCH_STALE);
    if(t->result.producer_offers&&s->stats.version==UINT64_MAX)return transaction_error(t,CETTA_PROOF_BATCH_CAPACITY);
    for(Root *p=t->atom_list;p;p=p->all_next){s->atoms=insert(s->atoms,&p->index,root_compare);add_bytes(s,1u,sizeof(*p));}
    if(t->atom_list){Root *last=t->atom_list;while(last->all_next)last=last->all_next;last->all_next=s->atom_list;s->atom_list=t->atom_list;t->atom_list=NULL;}
    for(PendingState *p=t->state_list;p;p=p->next){
        State *state=p->target;
        if(p->fresh){
            s->states=insert(s->states,&state->key_link,state_compare);s->ids=insert(s->ids,&state->id_link,id_compare);
            state->all_next=s->state_list;s->state_list=state;
            if(s->rank_last[state->rank])s->rank_last[state->rank]->rank_next=state;else s->rank_first[state->rank]=state;s->rank_last[state->rank]=state;
        }else language_release(state->language);
        state->language=p->language;p->language=NULL;p->fresh=false;
    }
    for(Edge *p=t->edge_list;p;p=p->all_next)s->edges=insert(s->edges,&p->index,edge_compare);
    if(t->edge_list){Edge *last=t->edge_list;while(last->all_next)last=last->all_next;last->all_next=s->edge_list;s->edge_list=t->edge_list;t->edge_list=NULL;}
    if(t->tail){Chunk *old=s->tail;add_bytes(s,t->tail->count-old->count,sizeof(*t->tail));s->tail=t->tail;t->tail=NULL;chunk_release(old);}
    add_bytes(s,t->result.new_states,sizeof(State));add_bytes(s,t->result.new_batches,sizeof(Edge)+sizeof(Language));
    s->stats.states+=t->result.new_states;s->stats.batches+=t->result.new_batches;s->stats.stored_rows+=t->result.appended_rows;s->stats.producer_offers+=t->result.producer_offers;s->stats.producer_occurrences+=t->result.producer_occurrences;
    for(PendingState *p=t->state_list;p;p=p->next)if(p->target->rank>s->stats.max_rank)s->stats.max_rank=p->target->rank;
    if(t->result.producer_offers)s->stats.version++;
    t->committed=true;return CETTA_PROOF_BATCH_OK;
}
void cetta_proof_batch_transaction_release(CettaProofBatchTransaction *t) {
    if(!t)return;
    for(PendingState *p=t->state_list;p;){PendingState *next=p->next;language_release(p->language);if(p->fresh)dispose(p->target,sizeof(*p->target),NULL);dispose(p,sizeof(*p),NULL);p=next;}
    for(Edge *p=t->edge_list;p;){Edge *next=p->all_next;dispose(p,sizeof(*p),NULL);p=next;}
    chunk_release(t->tail);for(Root *p=t->atom_list;p;){Root *next=p->all_next;root_release(p);p=next;}
    CettaProofBatchStore *s=t->store;dispose(t,sizeof(*t),&live_transactions);cetta_proof_batch_store_release(s);
}

typedef struct { const Language *language; uint8_t phase; } Frame;
struct CettaProofBatchCursor {
    CettaProofBatchView *view;
    Chunk **chunks,*build_next;
    Frame *frames;
    AtomId *labels;
    uint32_t chunk_capacity,build_remaining,chunk_index,entry_index,group_index;
    uint32_t frame_capacity,depth,label_count,selected;
    uint64_t token,entry_count,entry_done,entry_first;
    AtomId key;
    uint32_t state;
    CettaProofBatchWord pending_word;
    bool indexed,active,pending,finished;
};
static void cursor_parts_free(CettaProofBatchCursor *c) {
    dispose(c->chunks,(size_t)c->chunk_capacity*sizeof(*c->chunks),NULL);
    dispose(c->frames,(size_t)c->frame_capacity*sizeof(*c->frames),NULL);
    dispose(c->labels,(MAX_RANK+1u)*sizeof(*c->labels),NULL);
    cetta_proof_batch_view_release(c->view);
}
void cetta_proof_batch_cursor_release(CettaProofBatchCursor *c) {
    if(!c)return;
    cursor_parts_free(c);dispose(c,sizeof(*c),&live_cursors);
}
CettaProofBatchStatus cetta_proof_batch_cursor_new(CettaProofBatchView *v,uint32_t selected,CettaProofBatchCursor **out) {
    if(!out||!cetta_proof_batch_view_current(v))return CETTA_PROOF_BATCH_INVALIDATED;
    if(v->max_walk==UINT32_MAX)return CETTA_PROOF_BATCH_CAPACITY;
    CettaProofBatchCursor *c=allocate(sizeof(*c),&live_cursors);if(!c)return CETTA_PROOF_BATCH_CAPACITY;
    if(!cetta_proof_batch_view_retain(v)){dispose(c,sizeof(*c),&live_cursors);return CETTA_PROOF_BATCH_CAPACITY;}c->view=v;c->selected=selected;
    c->frame_capacity=v->max_walk+1u;
    if((size_t)c->frame_capacity>SIZE_MAX/sizeof(*c->frames)){cetta_proof_batch_cursor_release(c);return CETTA_PROOF_BATCH_CAPACITY;}
    c->frames=allocate((size_t)c->frame_capacity*sizeof(*c->frames),NULL);
    c->labels=allocate((MAX_RANK+1u)*sizeof(*c->labels),NULL);
    if(!v->ranked){
        c->chunk_capacity=v->tail->count;
        if((size_t)c->chunk_capacity>SIZE_MAX/sizeof(*c->chunks)){cetta_proof_batch_cursor_release(c);return CETTA_PROOF_BATCH_CAPACITY;}
        c->chunks=allocate((size_t)c->chunk_capacity*sizeof(*c->chunks),NULL);c->build_remaining=c->chunk_capacity;c->build_next=v->tail;
    }else c->indexed=true;
    if(!c->frames||!c->labels||(!v->ranked&&!c->chunks)){cetta_proof_batch_cursor_release(c);return CETTA_PROOF_BATCH_CAPACITY;}
    *out=c;return CETTA_PROOF_BATCH_OK;
}
static bool cursor_push(CettaProofBatchCursor *c,const Language *language) {
    if(!language||c->depth==c->frame_capacity)return false;
    c->frames[c->depth++]=(Frame){language,0u};return true;
}
static bool start_entry(CettaProofBatchCursor *c,const Entry *e) {
    c->key=e->key->id;c->state=e->state;c->entry_count=e->count;c->entry_done=0u;c->entry_first=e->first;c->label_count=0u;c->depth=0u;c->active=true;
    if(e->seed)return cursor_push(c,e->language);
    c->labels[c->label_count++]=e->language->label->id;return cursor_push(c,e->language->parent);
}
CettaProofBatchNext cetta_proof_batch_cursor_peek(CettaProofBatchCursor *c,CettaProofBudget *budget,CettaProofBatchWord *out) {
    if(!c||!budget||!out||!cetta_proof_batch_view_current(c->view))return CETTA_PROOF_BATCH_CURSOR_INVALIDATED;
    if(c->pending){*out=c->pending_word;return CETTA_PROOF_BATCH_ROW;}
    if(c->finished)return CETTA_PROOF_BATCH_END;
    while(budget->remaining){
        if(budget->visited==UINT64_MAX)return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
        budget->remaining--;budget->visited++;
        if(!c->indexed){
            if(!c->build_next||!c->build_remaining)return CETTA_PROOF_BATCH_CURSOR_INVALIDATED;
            c->chunks[--c->build_remaining]=c->build_next;c->build_next=c->build_next->previous;
            if(!c->build_remaining){if(c->build_next)return CETTA_PROOF_BATCH_CURSOR_INVALIDATED;
    c->indexed=true;}
            continue;
        }
        if(!c->active){
            if(c->view->ranked){
                if(c->group_index==c->view->group_count){c->finished=true;return CETTA_PROOF_BATCH_END;}
                const Group *g=&c->view->groups[c->group_index++];
                if(c->selected!=CETTA_PROOF_BATCH_ALL_STATES&&g->state!=c->selected)continue;
                c->key=g->key->id;c->state=g->state;c->entry_count=g->language->support;c->entry_done=0u;c->entry_first=0u;c->label_count=0u;c->depth=0u;c->active=true;
                if(!cursor_push(c,g->language))return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
            }else{
                if(c->chunk_index==c->chunk_capacity){c->finished=true;return CETTA_PROOF_BATCH_END;}
                const Chunk *chunk=c->chunks[c->chunk_index];
                if(c->entry_index==chunk->used){c->chunk_index++;c->entry_index=0u;continue;}
                const Entry *e=&chunk->entries[c->entry_index++];
                if(c->selected!=CETTA_PROOF_BATCH_ALL_STATES&&e->state!=c->selected)continue;
                if(!start_entry(c,e))return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
            }
            continue;
        }
        if(!c->depth){if(c->entry_done!=c->entry_count)return CETTA_PROOF_BATCH_CURSOR_INVALIDATED;
    c->active=false;continue;}
        Frame *frame=&c->frames[c->depth-1u];const Language *language=frame->language;
        if(language->seed){
            if(frame->phase){c->depth--;continue;}
            if(c->token==UINT64_MAX)return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
            frame->phase=1u;c->token++;c->pending=true;
            c->pending_word=(CettaProofBatchWord){
                .word={.state_key=c->key,.seed=language->label->id,.outer_labels=c->labels,.label_count=c->label_count,.state=c->state,.token=c->token},
                .ordinal=c->entry_first+c->entry_done,.has_ordinal=!c->view->ranked};
            *out=c->pending_word;return CETTA_PROOF_BATCH_ROW;
        }
        if(!frame->phase){frame->phase=1u;if(language->previous&&!cursor_push(c,language->previous))return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
    continue;}
        if(frame->phase==1u){
            if(c->label_count==MAX_RANK)return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
            frame->phase=2u;c->labels[c->label_count++]=language->label->id;
            if(!cursor_push(c,language->parent))return CETTA_PROOF_BATCH_CURSOR_CAPACITY;
            continue;
        }
        if(!c->label_count)return CETTA_PROOF_BATCH_CURSOR_INVALIDATED;
        c->label_count--;c->depth--;
    }
    return CETTA_PROOF_BATCH_PAUSED;
}
bool cetta_proof_batch_cursor_commit(CettaProofBatchCursor *c,uint64_t token) {
    if(!c||!c->pending||token!=c->token||!cetta_proof_batch_view_current(c->view))return false;
    c->pending=false;c->entry_done++;return true;
}
CettaProofBatchStatus cetta_proof_batch_cursor_clone(const CettaProofBatchCursor *source,CettaProofBatchCursor **out) {
    if(!source||!out||!cetta_proof_batch_view_current(source->view))return CETTA_PROOF_BATCH_INVALIDATED;
    CettaProofBatchCursor *c=NULL;CettaProofBatchStatus status=cetta_proof_batch_cursor_new(source->view,source->selected,&c);
    if(status!=CETTA_PROOF_BATCH_OK)return status;
    Chunk **chunks=c->chunks;Frame *frames=c->frames;AtomId *labels=c->labels;CettaProofBatchView *view=c->view;
    *c=*source;c->chunks=chunks;c->frames=frames;c->labels=labels;c->view=view;
    if(source->chunk_capacity)memcpy(chunks,source->chunks,(size_t)source->chunk_capacity*sizeof(*chunks));
    if(source->depth)memcpy(frames,source->frames,(size_t)source->depth*sizeof(*frames));
    if(source->label_count)memcpy(labels,source->labels,(size_t)source->label_count*sizeof(*labels));
    c->pending_word.word.outer_labels=c->labels;*out=c;return CETTA_PROOF_BATCH_OK;
}
CettaProofBatchStatus cetta_proof_batch_cursor_restore(CettaProofBatchCursor *destination,const CettaProofBatchCursor *snapshot) {
    if(!destination||!snapshot)return CETTA_PROOF_BATCH_DECLINED;
    CettaProofBatchCursor *copy=NULL;CettaProofBatchStatus status=cetta_proof_batch_cursor_clone(snapshot,&copy);if(status!=CETTA_PROOF_BATCH_OK)return status;
    CettaProofBatchCursor old=*destination;*destination=*copy;dispose(copy,sizeof(*copy),&live_cursors);cursor_parts_free(&old);return CETTA_PROOF_BATCH_OK;
}
