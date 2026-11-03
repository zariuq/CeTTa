#define main existing_batch_suite_main
#include "test_proof_batches.c"
#undef main
#include "../src/proof_batches.c"
int main(void) {
    CettaTermDomainScope scope={0};CettaTermDomain *d=domain_new(true,&scope);TermUniverse *u=cetta_term_domain_universe(d);Arena a;arena_init_detached(&a);
    uint64_t saved=atomic_load(&next_identity);atomic_store(&next_identity,UINT64_MAX-1u);
    CettaProofBatchStore *store=store_new(d,&a,limits());CettaProofBatchView *old=NULL;
    assert(cetta_proof_batch_seal_rank(store,0u,&old)==CETTA_PROOF_BATCH_OK);
    assert(old->store_identity==UINT64_MAX-1u&&atomic_load(&next_identity)==UINT64_MAX);
    CettaProofRowLayout l=layout(u,&a);AtomId root=key(u,&a,"(rows 2 (C (pair $x $x)))"),seed=key(u,&a,"I");
    for(unsigned i=0u;i<4u;i++){
        CettaProofBatchStore *out=(void *)(uintptr_t)1u;
        assert(cetta_proof_batch_store_new(u,&l,root,seed,limits(),(CettaProofBatchLifetime){d,cetta_term_domain_retain,cetta_term_domain_release},&out)==CETTA_PROOF_BATCH_CAPACITY);
        assert(out==(void *)(uintptr_t)1u&&atomic_load(&next_identity)==UINT64_MAX);
        CettaProofBatchGroup g;assert(cetta_proof_batch_view_group(old,0u,&g)&&g.support==1u&&g.state==0u);
    }
    cetta_proof_batch_store_release(store);assert(cetta_proof_batch_view_current(old));cetta_proof_batch_view_release(old);atomic_store(&next_identity,saved);
    arena_free(&a);cetta_term_domain_leave(&scope);cetta_term_domain_release(d);assert(!cetta_proof_batch_diagnostics().bytes);
    puts("PASS: actual store-identity counter reserves exhaustion without wrapping while old view remains valid");return 0;
}
