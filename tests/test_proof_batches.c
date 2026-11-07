#include "proof_batches.h"
#include "term_domain.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_budget=SIZE_MAX,allocation_calls;
static void *watched_domain;
static unsigned domain_frees;
void *__real_cetta_fallible_calloc(size_t,size_t);
void *__wrap_cetta_fallible_calloc(size_t n,size_t size) {
    allocation_calls++;
    if(allocation_budget!=SIZE_MAX){if(!allocation_budget)return NULL;allocation_budget--;}
    return __real_cetta_fallible_calloc(n,size);
}
void __real_free(void *);
void __wrap_free(void *p) { if(p&&p==watched_domain){domain_frees++;watched_domain=NULL;}__real_free(p); }
static Atom *parse(Arena *a,const char *text) { Atom **forms=NULL;assert(parse_metta_text(text,a,&forms)==1);Atom *p=forms[0];free(forms);return p; }
static AtomId key(TermUniverse *u,Arena *a,const char *text) { AtomId id=term_universe_store_alpha_key(u,parse(a,text));assert(id!=CETTA_ATOM_ID_NONE);return id; }
static CettaProofRowLayout layout(TermUniverse *u,Arena *a) {
    return (CettaProofRowLayout){.proof={.length=2u,.child={2u,2u}},.arity={3u,3u},.head={atom_symbol_current_id(parse(a,"rows")),atom_symbol_current_id(parse(a,"C"))},.universe_instance=u->instance_id,.storage_epoch=u->storage_epoch};
}
static CettaTermDomain *domain_new(bool counted,CettaTermDomainScope *scope) {
    CettaTermDomain *d=cetta_term_domain_new(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,TERM_UNIVERSE_COLLECTION_RECLAIM,false);assert(d&&cetta_term_domain_enter(d,scope));
    if(counted)assert(term_universe_collect(cetta_term_domain_universe(d),NULL));
    return d;
}
static CettaProofBatchStore *store_new(CettaTermDomain *d,Arena *a,CettaProofLimits limits) {
    TermUniverse *u=cetta_term_domain_universe(d);CettaProofRowLayout l=layout(u,a);CettaProofBatchStore *s=NULL;
    assert(cetta_proof_batch_store_new(u,&l,key(u,a,"(rows 2 (C (pair $x $x)))"),key(u,a,"I"),limits,
        (CettaProofBatchLifetime){d,cetta_term_domain_retain,cetta_term_domain_release},&s)==CETTA_PROOF_BATCH_OK);return s;
}
static CettaProofLimits limits(void) { return (CettaProofLimits){20000u,30000u,64u,UINT64_MAX,UINT64_MAX}; }
static bool same_stats(CettaProofBatchStats a,CettaProofBatchStats b) {
    return a.states==b.states&&a.batches==b.batches&&a.max_rank==b.max_rank&&a.stored_rows==b.stored_rows&&a.producer_offers==b.producer_offers&&a.producer_occurrences==b.producer_occurrences&&a.version==b.version;
}
static void offer_one(CettaProofBatchStore *s,CettaProofBatchView *parent,uint32_t group,AtomId label,AtomId child,CettaProofBatchStatus expected,uint64_t added,uint64_t occurrences) {
    CettaProofBatchTransaction *t=NULL;assert(cetta_proof_batch_begin(s,&t)==CETTA_PROOF_BATCH_OK);CettaProofBatchOffer out;
    assert(cetta_proof_batch_offer(t,parent,group,label,child,&out)==expected);
    if(expected==CETTA_PROOF_BATCH_OK||expected==CETTA_PROOF_BATCH_DUPLICATE){
        assert(out.appended_rows==added&&out.producer_occurrences==occurrences&&out.duplicate==(expected==CETTA_PROOF_BATCH_DUPLICATE));
        assert(cetta_proof_batch_commit(t)==CETTA_PROOF_BATCH_OK);
    }else assert(cetta_proof_batch_commit(t)==expected);
    cetta_proof_batch_transaction_release(t);
}
typedef struct { AtomId *ids;size_t len,cap;bool refuse; } Rows;
static void row_roots(const TermUniverse *u,TermUniverseRootVisit visit,void *visitor,void *context) {
    (void)u;Rows *r=context;for(size_t i=0u;i<r->len;i++)visit(visitor,r->ids[i]);
}
static bool retain_row(void *context,TermUniverse *u,AtomId id) {
    Rows *r=context;assert(u->collection_inhibit);
    if(r->refuse){r->refuse=false;return false;}
    if(r->len==r->cap){size_t cap=r->cap?r->cap*2u:8u;AtomId *ids=realloc(r->ids,cap*sizeof(*ids));if(!ids)return false;r->ids=ids;r->cap=cap;}
    r->ids[r->len++]=id;if(u->counting)term_universe_hold_counted(u,id);return true;
}
static void rows_clear(Rows *r,TermUniverse *u) {
    while(r->len){AtomId id=r->ids[--r->len];if(u->counting)term_universe_release_counted(u,id);}
    term_universe_remove_root_provider(u,row_roots,r);free(r->ids);
}
static void drain(CettaProofBatchView *v,uint32_t selected,TermUniverse *u,Arena *a,const char *const *expected,size_t n,bool ordinals) {
    CettaProofBatchCursor *c=NULL;assert(cetta_proof_batch_cursor_new(v,selected,&c)==CETTA_PROOF_BATCH_OK);
    Rows rows={0};assert(term_universe_add_counted_root_provider(u,row_roots,&rows));
    size_t count=0u;uint64_t used=0u;
    for(size_t attempts=0u;;attempts++){
        assert(attempts<1000000u);CettaProofBudget b={1u,0u};CettaProofBatchWord word;
        CettaProofBatchNext next=cetta_proof_batch_cursor_peek(c,&b,&word);assert(b.visited<=1u&&b.remaining+b.visited==1u);used+=b.visited;
        if(next==CETTA_PROOF_BATCH_PAUSED)continue;
        if(next==CETTA_PROOF_BATCH_END)break;
        assert(next==CETTA_PROOF_BATCH_ROW&&count<n&&word.has_ordinal==ordinals);
        if(ordinals)assert(word.ordinal==count);
        AtomId id=CETTA_ATOM_ID_NONE;CettaProofRowWork work;
        if(!count){
            rows.refuse=true;assert(cetta_proof_row_materialize(u,cetta_proof_batch_view_layout(v),&word.word,retain_row,&rows,&id,&work)==CETTA_PROOF_ROW_CAPACITY);
            assert(id==CETTA_ATOM_ID_NONE&&rows.len==0u);
            CettaProofBudget zero={0u,0u};CettaProofBatchWord same;assert(cetta_proof_batch_cursor_peek(c,&zero,&same)==CETTA_PROOF_BATCH_ROW&&same.word.token==word.word.token);
            assert(!cetta_proof_batch_cursor_commit(c,word.word.token+1u));
        }
        assert(cetta_proof_row_materialize(u,cetta_proof_batch_view_layout(v),&word.word,retain_row,&rows,&id,&work)==CETTA_PROOF_ROW_READY);
        assert(atom_alpha_eq(term_universe_get_atom(u,id),parse(a,expected[count])));
        assert(cetta_proof_batch_cursor_commit(c,word.word.token));count++;
    }
    assert(count==n&&used>n);cetta_proof_batch_cursor_release(c);rows_clear(&rows,u);
}
static void basic(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofBatchStore *s=store_new(d,&a,limits());CettaProofBatchView *parent=NULL,*again=NULL,*old=NULL;
    assert(cetta_proof_batch_store_retained_bytes(s)==cetta_proof_batch_diagnostics().bytes);
    assert(cetta_proof_batch_seal_rank(s,0u,&parent)==CETTA_PROOF_BATCH_OK);
    allocation_budget=0u;
    assert(cetta_proof_batch_seal_rank(s,0u,&again)==CETTA_PROOF_BATCH_OK&&again==parent);
    allocation_budget=SIZE_MAX;cetta_proof_batch_view_release(again);
    CettaProofBatchGroup g;assert(cetta_proof_batch_view_group(parent,0u,&g)&&g.state==0u&&g.rank==0u&&g.support==1u);
    AtomId child=key(u,&a,"(rows 1 (C (pair $x $y)))"),m=key(u,&a,"M"),one=key(u,&a,"1"),three=key(u,&a,"3");
    offer_one(s,parent,0u,m,child,CETTA_PROOF_BATCH_OK,1u,1u);
    assert(cetta_proof_batch_capture(s,&old)==CETTA_PROOF_BATCH_OK);
    CettaProofBatchView *old_group=NULL;assert(cetta_proof_batch_capture_rank(s,1u,&old_group)==CETTA_PROOF_BATCH_OK);
    offer_one(s,parent,0u,one,child,CETTA_PROOF_BATCH_OK,1u,1u);
    const char *old_rows[]={"(rows 2 (C (pair $x $x) I))","(rows 1 (C (pair $x $y) (M I)))"};
    drain(old,CETTA_PROOF_BATCH_ALL_STATES,u,&a,old_rows,2u,true);
    drain(old_group,CETTA_PROOF_BATCH_ALL_STATES,u,&a,old_rows+1u,1u,false);
    CettaProofBatchView *child_parent=NULL;assert(cetta_proof_batch_seal_rank(s,1u,&child_parent)==CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_view_group(child_parent,0u,&g)&&g.support==2u);
    offer_one(s,parent,0u,three,child,CETTA_PROOF_BATCH_SEALED,0u,0u);
    offer_one(s,parent,0u,m,key(u,&a,"(rows 1 (C wrong))"),CETTA_PROOF_BATCH_CONFLICT,0u,0u);
    offer_one(s,parent,0u,m,child,CETTA_PROOF_BATCH_DUPLICATE,0u,1u);
    offer_one(s,child_parent,0u,three,key(u,&a,"(rows 0 (C (> (f $x $y) (g $y $x))))"),CETTA_PROOF_BATCH_OK,2u,2u);
    CettaProofBatchStats stats=cetta_proof_batch_store_stats(s);assert(stats.stored_rows==5u&&stats.batches==3u&&stats.producer_offers==4u&&stats.producer_occurrences==5u);
    const char *rows[]={old_rows[0],old_rows[1],"(rows 1 (C (pair $x $y) (1 I)))","(rows 0 (C (> (f $x $y) (g $y $x)) (3 (M I))))","(rows 0 (C (> (f $x $y) (g $y $x)) (3 (1 I))))"};
    CettaProofBatchView *v=NULL;assert(cetta_proof_batch_capture(s,&v)==CETTA_PROOF_BATCH_OK);drain(v,CETTA_PROOF_BATCH_ALL_STATES,u,&a,rows,5u,true);
    assert(term_universe_collect(u,NULL));drain(v,CETTA_PROOF_BATCH_ALL_STATES,u,&a,rows,5u,true);
    cetta_proof_batch_view_release(v);cetta_proof_batch_view_release(old);cetta_proof_batch_view_release(old_group);cetta_proof_batch_view_release(child_parent);cetta_proof_batch_view_release(parent);
    assert(cetta_proof_batch_store_retained_bytes(s)==cetta_proof_batch_diagnostics().bytes);
    cetta_proof_batch_store_release(s);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);
    assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: incremental authored-order proof bags, alias keys, sealed rank cache, duplicate counts and conflicts");
}

static uint64_t tail_lifetime(unsigned count) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    watched_domain=d;unsigned before=domain_frees;
    CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *old=NULL,*parent=NULL;
    assert(cetta_proof_batch_capture(store,&old)==CETTA_PROOF_BATCH_OK);assert(cetta_proof_batch_seal_rank(store,0u,&parent)==CETTA_PROOF_BATCH_OK);
    AtomId later=CETTA_ATOM_ID_NONE;
    for(unsigned i=0u;i<count;i++){
        char text[96];snprintf(text,sizeof(text),"(rows 1 (C %u))",i);later=key(u,&a,text);snprintf(text,sizeof(text),"%u",i);
        offer_one(store,parent,0u,key(u,&a,text),later,CETTA_PROOF_BATCH_OK,1u,1u);
    }
    assert(cetta_proof_batch_store_stats(store).stored_rows==1u+count);cetta_proof_batch_view_release(parent);arena_free(&a);
    cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(domain_frees==before);
    cetta_proof_batch_store_release(store);assert(domain_frees==before);
    CettaProofBatchDiagnostics stats=cetta_proof_batch_diagnostics();
    assert(!stats.stores&&!stats.transactions&&!stats.cursors&&stats.views==1u&&stats.chunks==1u&&stats.languages==1u&&stats.roots==2u);
    assert(cetta_term_domain_enter(d,&scope));assert(term_universe_collect(u,NULL));if(count)assert(!tu_hdr(u,later));
    arena_init_detached(&a);const char *expected[]={"(rows 2 (C (pair $x $x) I))"};drain(old,CETTA_PROOF_BATCH_ALL_STATES,u,&a,expected,1u,true);arena_free(&a);cetta_term_domain_leave(&scope);
    cetta_proof_batch_view_release(old);assert(domain_frees==before+1u);assert(!cetta_proof_batch_diagnostics().bytes);
    return stats.bytes;
}
static void lifetime_test(void) {
    uint64_t small=tail_lifetime(0u),large=tail_lifetime(4096u);assert(small==large);
    printf("PASS: tiny old capture retains exactly %llu module bytes after4096 later batches retire; real domain freed last\n",(unsigned long long)small);
}
static CettaProofBatchWord next_word(CettaProofBatchCursor *c) {
    for(unsigned i=0u;i<100000u;i++){CettaProofBudget budget={1u,0u};CettaProofBatchWord word;CettaProofBatchNext next=cetta_proof_batch_cursor_peek(c,&budget,&word);assert(budget.visited<=1u);if(next==CETTA_PROOF_BATCH_ROW)return word;assert(next==CETTA_PROOF_BATCH_PAUSED);}
    abort();
}
static void restoration(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(false,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *parent=NULL,*view=NULL;
    assert(cetta_proof_batch_seal_rank(store,0u,&parent)==CETTA_PROOF_BATCH_OK);AtomId child=key(u,&a,"(rows 1 (C $x))"),m=key(u,&a,"M");
    offer_one(store,parent,0u,m,child,CETTA_PROOF_BATCH_OK,1u,1u);assert(cetta_proof_batch_capture(store,&view)==CETTA_PROOF_BATCH_OK);
    CettaProofBatchCursor *cursor=NULL,*snapshot=NULL;assert(cetta_proof_batch_cursor_new(view,CETTA_PROOF_BATCH_ALL_STATES,&cursor)==CETTA_PROOF_BATCH_OK);
    CettaProofBatchWord first=next_word(cursor);assert(first.ordinal==0u);
    assert(cetta_proof_batch_cursor_clone(cursor,&snapshot)==CETTA_PROOF_BATCH_OK);assert(cetta_proof_batch_cursor_commit(cursor,first.word.token));CettaProofBatchWord second=next_word(cursor);assert(second.ordinal==1u&&second.word.label_count==1u);
    CettaProofBatchDiagnostics checkpoint=cetta_proof_batch_diagnostics();
    for(size_t refusal=0u;refusal<4u;refusal++){
        allocation_budget=refusal;assert(cetta_proof_batch_cursor_restore(cursor,snapshot)==CETTA_PROOF_BATCH_CAPACITY);allocation_budget=SIZE_MAX;
        CettaProofBatchDiagnostics now=cetta_proof_batch_diagnostics();assert(!memcmp(&checkpoint,&now,sizeof(now)));
    }
    CettaProofBudget zero={0u,0u};CettaProofBatchWord unchanged;assert(cetta_proof_batch_cursor_peek(cursor,&zero,&unchanged)==CETTA_PROOF_BATCH_ROW&&unchanged.word.token==second.word.token&&unchanged.ordinal==1u);
    assert(cetta_proof_batch_cursor_restore(cursor,snapshot)==CETTA_PROOF_BATCH_OK);CettaProofBatchWord again=next_word(cursor);assert(again.word.token==first.word.token&&again.ordinal==0u);
    assert(cetta_proof_batch_cursor_commit(cursor,again.word.token));again=next_word(cursor);assert(again.ordinal==1u);
    cetta_proof_batch_view_release(parent);cetta_proof_batch_view_release(view);cetta_proof_batch_store_release(store);
    assert(term_universe_collect(u,NULL));CettaProofBatchWord independently=next_word(snapshot);assert(independently.ordinal==0u);
    u->storage_epoch++;CettaProofBudget budget={8u,0u};assert(cetta_proof_batch_cursor_peek(cursor,&budget,&unchanged)==CETTA_PROOF_BATCH_CURSOR_INVALIDATED);assert(!cetta_proof_batch_cursor_commit(cursor,again.word.token));u->storage_epoch--;
    cetta_proof_batch_cursor_release(snapshot);cetta_proof_batch_cursor_release(cursor);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: pending clone/restore/refusal, quota1, collected snapshots and epoch invalidation");
}
static void transaction_faults(void) {
    unsigned failures=0u;
    for(size_t refusal=0u;refusal<64u;refusal++){
        CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
        CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,0u,&parent)==CETTA_PROOF_BATCH_OK);
        AtomId child=key(u,&a,"(rows 1 (C (pair $x $y)))"),m=key(u,&a,"M");CettaProofBatchStats before=cetta_proof_batch_store_stats(store);CettaProofBatchDiagnostics baseline=cetta_proof_batch_diagnostics();
        allocation_budget=refusal;CettaProofBatchTransaction *tx=NULL;CettaProofBatchStatus status=cetta_proof_batch_begin(store,&tx);CettaProofBatchOffer offer;
        if(status==CETTA_PROOF_BATCH_OK)status=cetta_proof_batch_offer(tx,parent,0u,m,child,&offer);
        allocation_budget=SIZE_MAX;
        if(status==CETTA_PROOF_BATCH_OK){assert(cetta_proof_batch_commit(tx)==CETTA_PROOF_BATCH_OK);cetta_proof_batch_transaction_release(tx);cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(store);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);break;}
        assert(status==CETTA_PROOF_BATCH_CAPACITY);failures++;if(tx){assert(cetta_proof_batch_commit(tx)==CETTA_PROOF_BATCH_CAPACITY);cetta_proof_batch_transaction_release(tx);}
        CettaProofBatchStats after=cetta_proof_batch_store_stats(store);assert(same_stats(before,after));CettaProofBatchDiagnostics now=cetta_proof_batch_diagnostics();assert(!memcmp(&baseline,&now,sizeof(now)));
        offer_one(store,parent,0u,m,child,CETTA_PROOF_BATCH_OK,1u,1u);assert(term_universe_collect(u,NULL));
        cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(store);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    }
    assert(failures>=8u);printf("PASS: %u allocation refusals leave store/leases unchanged and retry successfully\n",failures);
}
static void stale_and_overflow(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofLimits l=limits();l.max_support=3u;CettaProofBatchStore *store=store_new(d,&a,l);CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,0u,&parent)==CETTA_PROOF_BATCH_OK);
    AtomId child=key(u,&a,"(rows 1 (C $x))"),m=key(u,&a,"M"),one=key(u,&a,"1"),two=key(u,&a,"2");
    offer_one(store,parent,0u,m,child,CETTA_PROOF_BATCH_OK,1u,1u);
    CettaProofBatchTransaction *tx=NULL;assert(cetta_proof_batch_begin(store,&tx)==CETTA_PROOF_BATCH_OK);CettaProofBatchOffer out;assert(cetta_proof_batch_offer(tx,parent,0u,m,child,&out)==CETTA_PROOF_BATCH_DUPLICATE);
    offer_one(store,parent,0u,one,child,CETTA_PROOF_BATCH_OK,1u,1u);CettaProofBatchStats before=cetta_proof_batch_store_stats(store);assert(cetta_proof_batch_commit(tx)==CETTA_PROOF_BATCH_STALE);cetta_proof_batch_transaction_release(tx);CettaProofBatchStats after=cetta_proof_batch_store_stats(store);assert(same_stats(before,after));
    offer_one(store,parent,0u,two,child,CETTA_PROOF_BATCH_CAPACITY,0u,0u);assert(cetta_proof_batch_store_stats(store).stored_rows==3u);
    cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(store);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: stale prepared commit and row allowance refuse before publication");
}

static void constructor_faults(void) {
    unsigned failures=0u;
    for(size_t refusal=0u;refusal<32u;refusal++){
        CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
        watched_domain=d;unsigned freed=domain_frees;CettaProofRowLayout l=layout(u,&a);AtomId root=key(u,&a,"(rows 2 (C (pair $x $x)))"),seed=key(u,&a,"I");
        CettaProofBatchStore *sentinel=(void *)(uintptr_t)1u;
        allocation_budget=refusal;CettaProofBatchStatus status=cetta_proof_batch_store_new(u,&l,root,seed,limits(),(CettaProofBatchLifetime){d,cetta_term_domain_retain,cetta_term_domain_release},&sentinel);allocation_budget=SIZE_MAX;
        if(status==CETTA_PROOF_BATCH_OK)cetta_proof_batch_store_release(sentinel);
        else { assert(status==CETTA_PROOF_BATCH_CAPACITY&&sentinel==(void *)(uintptr_t)1u);failures++; }
        assert(!cetta_proof_batch_diagnostics().bytes);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(domain_frees==freed+1u);
        if(status==CETTA_PROOF_BATCH_OK)break;
    }
    assert(failures>=6u);printf("PASS: %u constructor refusals release real owner/provider/lease preparation\n",failures);
}
static void shared_labels_and_big_counts(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofBatchStore *store=store_new(d,&a,limits());AtomId m=key(u,&a,"M"),row=key(u,&a,"(rows 1 (C $x))");
    for(unsigned rank=0u;rank<64u;rank++){
        CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,rank,&parent)==CETTA_PROOF_BATCH_OK);
        offer_one(store,parent,0u,m,row,CETTA_PROOF_BATCH_OK,1u,1u);
        for(unsigned duplicate=0u;duplicate<16u;duplicate++)offer_one(store,parent,0u,m,row,CETTA_PROOF_BATCH_DUPLICATE,0u,1u);
        assert(u->counts[m]==1u);cetta_proof_batch_view_release(parent);
    }
    assert(cetta_proof_batch_store_stats(store).states==65u&&cetta_proof_batch_store_stats(store).stored_rows==65u);
    cetta_proof_batch_store_release(store);assert(!cetta_proof_batch_diagnostics().bytes);
    store=store_new(d,&a,limits());AtomId one=key(u,&a,"1");
    for(unsigned rank=0u;rank<63u;rank++){
        CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,rank,&parent)==CETTA_PROOF_BATCH_OK);
        uint64_t weight=UINT64_C(1)<<rank;
        offer_one(store,parent,0u,m,row,CETTA_PROOF_BATCH_OK,weight,weight);
        offer_one(store,parent,0u,one,row,CETTA_PROOF_BATCH_OK,weight,weight);
        cetta_proof_batch_view_release(parent);
    }
    assert(cetta_proof_batch_store_stats(store).stored_rows==UINT64_MAX);
    CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,63u,&parent)==CETTA_PROOF_BATCH_OK);
    CettaProofBatchStats before=cetta_proof_batch_store_stats(store);offer_one(store,parent,0u,m,row,CETTA_PROOF_BATCH_CAPACITY,0u,0u);assert(same_stats(before,cetta_proof_batch_store_stats(store)));
    cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(store);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: shared counted label lease stays1 through64 edges/1024 duplicates; real UINT64 overflow refuses");
}

static void batch_conflict_and_order(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(store,0u,&parent)==CETTA_PROOF_BATCH_OK);
    AtomId x=key(u,&a,"(rows 1 (C $x))"),y=key(u,&a,"(rows 1 (C (pair $x $x)))"),m=key(u,&a,"M"),one=key(u,&a,"1");
    CettaProofBatchTransaction *tx=NULL;assert(cetta_proof_batch_begin(store,&tx)==CETTA_PROOF_BATCH_OK);CettaProofBatchOffer out;
    assert(cetta_proof_batch_offer(tx,parent,0u,m,x,&out)==CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(tx,parent,0u,m,y,&out)==CETTA_PROOF_BATCH_CONFLICT);assert(cetta_proof_batch_commit(tx)==CETTA_PROOF_BATCH_CONFLICT);
    assert(cetta_proof_batch_store_stats(store).stored_rows==1u);cetta_proof_batch_transaction_release(tx);
    assert(cetta_proof_batch_begin(store,&tx)==CETTA_PROOF_BATCH_OK);
    assert(cetta_proof_batch_offer(tx,parent,0u,m,x,&out)==CETTA_PROOF_BATCH_OK&&out.child_state==1u);
    assert(cetta_proof_batch_offer(tx,parent,0u,one,y,&out)==CETTA_PROOF_BATCH_OK&&out.child_state==2u);
    assert(cetta_proof_batch_offer(tx,parent,0u,m,x,&out)==CETTA_PROOF_BATCH_DUPLICATE&&out.appended_rows==0u);
    assert(cetta_proof_batch_commit(tx)==CETTA_PROOF_BATCH_OK);cetta_proof_batch_transaction_release(tx);
    CettaProofBatchView *children=NULL;assert(cetta_proof_batch_capture_rank(store,1u,&children)==CETTA_PROOF_BATCH_OK);CettaProofBatchGroup g;
    assert(cetta_proof_batch_view_group(children,0u,&g)&&g.state==1u&&g.key==x);assert(cetta_proof_batch_view_group(children,1u,&g)&&g.state==2u&&g.key==y);
    CettaProofBatchStats stats=cetta_proof_batch_store_stats(store);assert(stats.states==3u&&stats.stored_rows==3u&&stats.producer_occurrences==3u);
    cetta_proof_batch_view_release(children);cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(store);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: late multi-offer conflict cancels all preparation; authored state/group order survives commit");
}
static void actual_storage_reset(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a,replacement;arena_init_detached(&a);arena_init_detached(&replacement);
    CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *view=NULL;assert(cetta_proof_batch_capture(store,&view)==CETTA_PROOF_BATCH_OK);
    AtomId coordinate=key(u,&a,"(rows 2 (C (pair $x $x)))");
    term_universe_set_persistent_arena(u,&replacement);assert(!cetta_proof_batch_view_current(view));
    while(u->len<=coordinate){char text[80];snprintf(text,sizeof(text),"replacement_%zu",u->len);assert(tu_intern_symbol(u,symbol_intern_cstr(g_symbols,text))!=CETTA_ATOM_ID_NONE);}
    Rows rows={.ids=malloc(sizeof(AtomId)),.len=1u,.cap=1u};assert(rows.ids);rows.ids[0]=coordinate;
    assert(term_universe_add_counted_root_provider(u,row_roots,&rows));if(u->counting)term_universe_hold_counted(u,coordinate);assert(term_universe_collect(u,NULL)&&u->counts[coordinate]==1u);
    cetta_proof_batch_store_release(store);cetta_proof_batch_view_release(view);assert(u->counts[coordinate]==1u);
    assert(term_universe_collect(u,NULL)&&tu_hdr(u,coordinate));rows_clear(&rows,u);
    term_universe_set_persistent_arena(u,cetta_term_domain_persistent(d));arena_free(&replacement);arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: actual TU reset invalidates capture and retiring old roots never drops reused coordinates");
}
int main(void) { setvbuf(stdout,NULL,_IOLBF,0);basic();batch_conflict_and_order();restoration();constructor_faults();transaction_faults();stale_and_overflow();shared_labels_and_big_counts();actual_storage_reset();lifetime_test();return 0; }
