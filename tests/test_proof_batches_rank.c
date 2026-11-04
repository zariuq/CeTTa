#define main existing_batch_suite_main
#include "test_proof_batches.c"
#undef main

static CettaProofBatchStore *new_rank_store(CettaTermDomain *domain, Arena *a,
    CettaProofRowLayout l, const char *root) {
    CettaProofBatchStore *s = NULL; TermUniverse *u = cetta_term_domain_universe(domain);
    assert(cetta_proof_batch_store_new(u, &l, key(u,a,root), key(u,a,"I"), limits(),
        (CettaProofBatchLifetime){domain,cetta_term_domain_retain,cetta_term_domain_release}, &s) == CETTA_PROOF_BATCH_OK);
    return s;
}
static CettaProofRowLayout state_layout(TermUniverse *u, Arena *a) {
    return (CettaProofRowLayout){.proof={.length=2u,.child={3u,2u}},.arity={4u,3u},
        .head={parse(a,"state")->sym_id,parse(a,"C")->sym_id},.universe_instance=u->instance_id,.storage_epoch=u->storage_epoch};
}
static void fixed_counter(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofRowLayout l=state_layout(u,&a);CettaProofBatchStore *s=new_rank_store(d,&a,l,"(state 4 2 (C (pair $x $y)))");
    CettaProofFieldPath depth={.length=1u,.child={1u}},hyp={.length=1u,.child={2u}};
    CettaProofBatchTransaction *stale=NULL;assert(cetta_proof_batch_begin(s,&stale)==CETTA_PROOF_BATCH_OK);
    int64_t root=-1;assert(cetta_proof_batch_store_bind_rank(s,&depth,&root)==CETTA_PROOF_BATCH_OK&&root==4);
    assert(cetta_proof_batch_commit(stale)==CETTA_PROOF_BATCH_STALE);cetta_proof_batch_transaction_release(stale);
    uint64_t version=cetta_proof_batch_store_stats(s).version;
    assert(cetta_proof_batch_store_bind_rank(s,&depth,&root)==CETTA_PROOF_BATCH_OK&&root==4&&cetta_proof_batch_store_stats(s).version==version);
    assert(cetta_proof_batch_store_bind_rank(s,&hyp,&root)==CETTA_PROOF_BATCH_DECLINED);
    uint32_t generation=99u;
    assert(cetta_proof_batch_store_rank_generation(s,&depth,4,3,&generation)&&generation==0u);
    assert(cetta_proof_batch_store_rank_generation(s,&depth,3,2,&generation)&&generation==1u);
    assert(!cetta_proof_batch_store_rank_generation(s,&hyp,2,1,&generation));
    assert(!cetta_proof_batch_store_rank_generation(s,&depth,4,2,&generation));
    assert(!cetta_proof_batch_store_rank_generation(s,&depth,5,4,&generation));
    assert(!cetta_proof_batch_store_rank_generation(s,&depth,0,-1,&generation));
    CettaProofBatchView *parents=NULL;assert(cetta_proof_batch_seal_rank(s,0u,&parents)==CETTA_PROOF_BATCH_OK);
    offer_one(s,parents,0u,key(u,&a,"A"),key(u,&a,"(state 3 1 (C (pair $x $y)))"),CETTA_PROOF_BATCH_OK,1u,1u);
    CettaProofBatchStats before=cetta_proof_batch_store_stats(s);
    offer_one(s,parents,0u,key(u,&a,"Bad"),key(u,&a,"(state 2 1 (C (pair $x $y)))"),CETTA_PROOF_BATCH_DECLINED,0u,0u);
    assert(same_stats(before,cetta_proof_batch_store_stats(s)));
    cetta_proof_batch_view_release(parents);cetta_proof_batch_store_release(s);
    s=new_rank_store(d,&a,l,"(state 4 2 (C (pair $x $y)))");
    assert(cetta_proof_batch_store_bind_rank(s,&hyp,&root)==CETTA_PROOF_BATCH_OK&&root==2);
    assert(cetta_proof_batch_seal_rank(s,0u,&parents)==CETTA_PROOF_BATCH_OK);
    offer_one(s,parents,0u,key(u,&a,"A"),key(u,&a,"(state 3 1 (C (pair $x $y)))"),CETTA_PROOF_BATCH_OK,1u,1u);
    cetta_proof_batch_view_release(parents);parents=NULL;
    assert(!cetta_proof_batch_store_rank_generation(s,&depth,3,2,&generation));
    assert(cetta_proof_batch_seal_rank(s,1u,&parents)==CETTA_PROOF_BATCH_OK);
    before=cetta_proof_batch_store_stats(s);
    offer_one(s,parents,0u,key(u,&a,"M"),key(u,&a,"(state 2 1 (C (pair $x $y)))"),CETTA_PROOF_BATCH_DECLINED,0u,0u);
    assert(same_stats(before,cetta_proof_batch_store_stats(s))&&before.stored_rows==2u);
    cetta_proof_batch_view_release(parents);cetta_proof_batch_store_release(s);
    arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: fixed source counter prevents AX/hyp versus MP/depth remapping, stales old transactions and rejects wrong child rank before effects");
}
static void erased_paths(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    CettaProofRowLayout l={.proof={.length=2u,.child={1u,1u}},.arity={2u,3u},.head={parse(&a,"rows")->sym_id,parse(&a,"C")->sym_id},.universe_instance=u->instance_id,.storage_epoch=u->storage_epoch};
    CettaProofBatchStore *s=new_rank_store(d,&a,l,"(rows (C 7))");
    CettaProofFieldPath after={.length=2u,.child={1u,2u}},deleted={.length=2u,.child={1u,1u}};int64_t root=-1;
    assert(cetta_proof_batch_store_bind_rank(s,&deleted,&root)==CETTA_PROOF_BATCH_DECLINED&&root==-1);
    assert(cetta_proof_batch_store_bind_rank(s,&after,&root)==CETTA_PROOF_BATCH_OK&&root==7);
    CettaProofBatchView *parent=NULL;assert(cetta_proof_batch_seal_rank(s,0u,&parent)==CETTA_PROOF_BATCH_OK);
    offer_one(s,parent,0u,key(u,&a,"M"),key(u,&a,"(rows (C 6))"),CETTA_PROOF_BATCH_OK,1u,1u);
    offer_one(s,parent,0u,key(u,&a,"Bad"),key(u,&a,"(rows (C 5))"),CETTA_PROOF_BATCH_DECLINED,0u,0u);
    cetta_proof_batch_view_release(parent);cetta_proof_batch_store_release(s);
    s=new_rank_store(d,&a,l,"(rows (C -1))");root=42;
    assert(cetta_proof_batch_store_bind_rank(s,&after,&root)==CETTA_PROOF_BATCH_DECLINED&&root==42);cetta_proof_batch_store_release(s);
    s=new_rank_store(d,&a,l,"(rows (C 9223372036854775807))");
    assert(cetta_proof_batch_store_bind_rank(s,&after,&root)==CETTA_PROOF_BATCH_OK&&root==INT64_MAX);
    uint32_t rank=100;assert(cetta_proof_batch_store_rank_generation(s,&after,INT64_MAX-63,INT64_MAX-64,&rank)&&rank==63u);
    assert(!cetta_proof_batch_store_rank_generation(s,&after,INT64_MAX-64,INT64_MAX-65,&rank));
    cetta_proof_batch_store_release(s);
    arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: erased-child index shift, deleted-proof rejection, negative root and INT64_MAX boundary preserve the fixed counter law");
}
int main(void) { existing_batch_suite_main(); fixed_counter(); erased_paths(); return 0; }
