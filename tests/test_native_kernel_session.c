#include "../native/kernel/kernel.h"
#include <assert.h>
#include <stdio.h>

typedef struct { bool valid; } Source;
static NmViewStatus inspect(void *ctx,NmHostValue v,NmViewNode *out) {
    (void)ctx;
    if (!v.syntax) *out=(NmViewNode){.kind=NM_NODE_TUPLE};
    else if (v.syntax<100) *out=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={v.syntax,v.context}};
    else if (v.syntax<200) *out=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=v.syntax};
    else if (v.syntax==200) *out=(NmViewNode){.kind=NM_NODE_TUPLE,.children=2};
    else return NM_VIEW_DEFER;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *ctx,NmHostValue v,uint64_t index,NmHostValue *out) {
    (void)ctx;
    if (v.syntax!=200 || index>1) return NM_VIEW_FAULT;
    *out=(NmHostValue){index+1,v.context};return NM_VIEW_READY;
}
static bool valid(void *ctx) { return ((Source *)ctx)->valid; }
int main(void) {
    cetta_kernel_enable_stats();
    char error[256];Source source={true};NmQueryView view={{&source,inspect,child,NULL},&source,valid};
    CettaKernelLimits limits={.import={256,512,32,4096},.pairs=128,.scan=128,.memo=256,
        .output_nodes=256,.output_edges=512,.output_stack=256,.fuel_budget=1000000};
    size_t checks=0;
    for (unsigned mode=0;mode<3;++mode) {
        assert(cetta_kernel_select((CettaKernelMode)mode,error,sizeof error));
        CettaKernelSession *s=NULL;
        assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED && s);
        assert(!cetta_kernel_select(mode==0 ? CETTA_KERNEL_X86 : CETTA_KERNEL_C,error,sizeof error));
        cetta_kernel_shutdown(); /* active sessions retain their loaded images */
        CettaKernelCheckpoint *empty=cetta_kernel_checkpoint(s);assert(empty);
        NmHostConstraint x={{1,9},{100,0}},y={{2,9},{101,0}},bad={{1,9},{101,0}};CettaKernelReceipt r;
        assert(cetta_kernel_solve(s,&x,1,false,&r,error,sizeof error)==CETTA_KERNEL_SOLVED && r.truth==NM_TRUTH_OPEN);
        uint64_t cost=r.fuel_used;assert(cost);
        CettaKernelCheckpoint *first=cetta_kernel_checkpoint(s);assert(first);
        assert(cetta_kernel_solve(s,&bad,1,true,&r,error,sizeof error)==CETTA_KERNEL_REFUTED && r.truth==NM_TRUTH_REFUTED);
        assert(r.fuel_used>cost);
        assert(cetta_kernel_solve(s,&x,1,true,&r,error,sizeof error)==CETTA_KERNEL_SOLVED && r.truth==NM_TRUTH_ENTAILED);
        assert(cetta_kernel_solve(s,&y,1,false,&r,error,sizeof error)==CETTA_KERNEL_SOLVED);
        NmHostValue roots[]={{200,9},{200,9},{0,0}};CettaKernelOutput *o=NULL;NmOutputReceipt receipt;
        assert(cetta_kernel_instantiate(s,roots,3,&o,&receipt,error,sizeof error)==CETTA_KERNEL_SOLVED);
        const NmOutputGraph *g=cetta_kernel_output_graph(o);assert(g && g->roots_count==3 && g->roots[0]==g->roots[1]);
        NmNode t=g->nodes[g->roots[0]].node;assert(t.kind==NM_NODE_TUPLE && t.b==2);
        assert(g->nodes[g->children[t.a]].node.a==100 && g->nodes[g->children[t.a+1]].node.a==101);
        cetta_kernel_output_free(o);
        cost=receipt.fuel_used;
        assert(cetta_kernel_restore(s,first));
        assert(cetta_kernel_solve(s,&y,1,true,&r,error,sizeof error)==CETTA_KERNEL_SOLVED && r.truth==NM_TRUTH_OPEN && r.fuel_used>cost);
        assert(cetta_kernel_restore(s,empty));
        assert(cetta_kernel_solve(s,&x,1,true,&r,error,sizeof error)==CETTA_KERNEL_SOLVED && r.truth==NM_TRUTH_OPEN);
        source.valid=false;assert(cetta_kernel_solve(s,&x,1,false,&r,error,sizeof error)==CETTA_KERNEL_INVALIDATED);
        assert(!cetta_kernel_solution(s));source.valid=true;
        cetta_kernel_session_free(s);
        assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
        assert(!cetta_kernel_restore(s,first));
        cetta_kernel_checkpoint_free(first);cetta_kernel_checkpoint_free(empty);cetta_kernel_session_free(s);checks+=12;
    }
    CettaKernelStats stats=cetta_kernel_stats();assert(stats.x86_calls && stats.c_calls);
    assert(cetta_kernel_select(CETTA_KERNEL_C,error,sizeof error));cetta_kernel_shutdown();
    printf("native kernel sessions: %zu checks; C/native solve, probe, scoped bundles, rollback, stale checkpoints and monotone fuel passed\n",checks);
    return 0;
}
