#include "../native/kernel/kernel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { unsigned bindings;bool valid; } Source;
static NmViewStatus inspect(void *raw,NmHostValue v,NmViewNode *n) {
    (void)raw;
    if (!v.syntax) *n=(NmViewNode){.kind=NM_NODE_TUPLE};
    else if (v.syntax<=8) *n=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={v.syntax,v.context}};
    else if (v.syntax>=100 && v.syntax<120) *n=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=v.syntax};
    else if (v.syntax>=200 && v.syntax<=206) *n=(NmViewNode){.kind=NM_NODE_TUPLE,.children=v.syntax==206 ? 1 : 2};
    else return NM_VIEW_DEFER;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *raw,NmHostValue v,uint64_t at,NmHostValue *n) {
    (void)raw;
    static const uint64_t children[][2]={{1,2},{2,3},{200,201},{200,200},{100,101},{1,200},{206,0}};
    if (v.syntax<200 || v.syntax>206 || at>1 || (v.syntax==206 && at)) return NM_VIEW_FAULT;
    *n=(NmHostValue){children[v.syntax-200][at],v.context};return NM_VIEW_READY;
}
static NmViewStatus binding(void *raw,NmHostValue v,bool *bound,NmHostValue *out) {
    Source *s=raw;*bound=false;
    if ((s->bindings&1) && v.syntax==1) { *bound=true;*out=(NmHostValue){100,v.context}; }
    if ((s->bindings&2) && v.syntax==3) { *bound=true;*out=(NmHostValue){4,v.context^UINT64_C(0x8000000000000000)}; }
    return NM_VIEW_READY;
}
static bool pin(void *raw) { return ((Source *)raw)->valid; }
static uint64_t mix(uint64_t a,uint64_t b) { return (a^b)*UINT64_C(0x9e3779b97f4a7c15)+(a>>23); }
static uint64_t digest(const CettaKernelOutput *output) {
    const NmOutputGraph *g=cetta_kernel_output_graph(output);assert(g);
    uint64_t *hash=calloc(g->nodes_count,sizeof *hash);assert(hash);
    for (size_t i=1;i<g->nodes_count;++i) {
        NmNode n=g->nodes[i].node;uint64_t h=n.kind+1;
        if (n.kind==NM_NODE_CONSTANT) h=mix(h,n.a);
        else if (n.kind==NM_NODE_VARIABLE) h=mix(mix(h,n.b),n.c);
        else { h=mix(h,n.b);assert(n.a<=g->children_count && n.b<=g->children_count-n.a);
            for (uint64_t j=0;j<n.b;++j) { assert(g->children[n.a+j]<i);h=mix(h,hash[g->children[n.a+j]]); }
        }
        hash[i]=h;
    }
    uint64_t result=g->roots_count;
    for (size_t i=0;i<g->roots_count;++i) result=mix(result,hash[g->roots[i]]);
    free(hash);return result;
}
static uint64_t rng=UINT64_C(0xd6e8feb86659fd93);
static uint64_t random_word(void) { rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng; }
int main(void) {
    cetta_kernel_enable_stats();size_t checks=0;char error[256];
    CettaKernelLimits limits={.import={512,2048,64,8192},.pairs=1024,.scan=1024,.memo=1024,
        .output_nodes=1024,.output_edges=4096,.output_stack=1024,.fuel_budget=UINT64_MAX};
    static const uint64_t terms[]={0,1,2,3,4,100,101,102,200,201,202,203,204,205,206};
    for (size_t specimen=0;specimen<1024;++specimen) {
        Source source={(unsigned)(specimen%4),true};NmQueryView view={{&source,inspect,child,binding},&source,pin};
        NmHostConstraint constraints[3];size_t count=1+specimen%3;
        for (size_t i=0;i<count;++i) constraints[i]=(NmHostConstraint){
            {terms[random_word()%(sizeof terms/sizeof *terms)],UINT64_C(0xffff000000000000)+(specimen%3)},
            {terms[random_word()%(sizeof terms/sizeof *terms)],UINT64_C(0xffff000000000000)+(specimen%2)}};
        NmHostValue roots[]={{1,constraints[0].left.context},{2,constraints[0].left.context},
            {3,constraints[0].right.context},{200,constraints[0].left.context},{203,constraints[0].right.context},
            {0,0},{1,constraints[0].left.context}};
        CettaKernelStatus expected=CETTA_KERNEL_FAULT;NmConstraintTruth truth=NM_TRUTH_UNAVAILABLE;uint64_t hash=0;
        for (unsigned mode=0;mode<3;++mode) {
            CettaKernelSession *session=NULL;CettaKernelReceipt receipt;
            assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&session,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelCheckpoint *checkpoint=cetta_kernel_checkpoint(session);assert(checkpoint);
            CettaKernelStatus status=cetta_kernel_solve(session,constraints,count,false,&receipt,error,sizeof error);
            if (!mode) { expected=status;truth=receipt.truth; }
            if (status!=expected || receipt.truth!=truth) {
                fprintf(stderr,"reference/native outcome difference specimen=%zu mode=%u C=%u native=%u truth=%u/%u\n",specimen,mode,(unsigned)expected,(unsigned)status,(unsigned)truth,(unsigned)receipt.truth);abort();
            }
            if (status==CETTA_KERNEL_SOLVED) {
                CettaKernelOutput *output=NULL;NmOutputReceipt emitted;
                assert(cetta_kernel_instantiate(session,roots,sizeof roots/sizeof *roots,&output,&emitted,error,sizeof error)==CETTA_KERNEL_SOLVED);
                uint64_t actual=digest(output);
                if (!mode) hash=actual;
                if (actual!=hash) { fprintf(stderr,"reference/native complete output difference specimen=%zu mode=%u\n",specimen,mode);abort(); }
                cetta_kernel_output_free(output);
            }
            assert(cetta_kernel_restore(session,checkpoint));assert(!cetta_kernel_solution(session));
            cetta_kernel_checkpoint_free(checkpoint);cetta_kernel_session_free(session);++checks;
        }
    }
    cetta_kernel_shutdown();
    printf("independent kernel reference: %zu C/x86/hybrid comparisons over scoped graphs, captures, occurs checks, duplicate roots and refused prefixes passed\n",checks);
}
