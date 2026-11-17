#include "../native/kernel/kernel.h"
#include "../native/kernel/constructor.h"
#include "../native/kernel/batch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long failure=-1;static bool failed;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
static bool refuse(void) {
    if (failure<0) return false;
    if (!failure--) { failure=-1;failed=true;return true; }
    return false;
}
void *__wrap_malloc(size_t n) { return refuse() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t w) { return refuse() ? NULL : __real_calloc(n,w); }
void *__wrap_realloc(void *p,size_t n) { return refuse() ? NULL : __real_realloc(p,n); }
static NmViewStatus inspect(void *context,NmHostValue v,NmViewNode *n) {
    (void)context;
    *n=v.syntax<10 ? (NmViewNode){.kind=NM_NODE_VARIABLE,.variable={v.syntax,v.context}} :
        v.syntax<100 ? (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=v.syntax} :
        (NmViewNode){.kind=NM_NODE_TUPLE,.children=2};
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context,NmHostValue v,uint64_t at,NmHostValue *n) {
    (void)context;if (v.syntax!=100 || at>1) return NM_VIEW_FAULT;
    *n=(NmHostValue){at+1,v.context};return NM_VIEW_READY;
}
static size_t batch_boundaries(CettaKernelMode mode) {
    TcNode nodes[3]={{0},{.kind=TC_SYMBOL,.ground=true},
        {.kind=TC_EXPRESSION,.ground=true,.length=1,.start=0}};
    uint32_t items[]={1},rows[]={2},stages[TC_BATCH_STAGE_WORDS]={1,0,0,0,1,UINT32_MAX,0,0};
    FlatPlan plan={0};uint32_t cursor[TC_BATCH_CURSOR_WORDS]={0},output[1]={0xd3d3d3d3};
    TcBatchInput in={nodes,3,items,1,rows,1,&plan,stages,1,1,0,1,0};
    TcBatchWork w={cursor,TC_BATCH_CURSOR_WORDS-1,output,1,0,0,0,0},before=w;
    assert(cetta_kernel_batch(mode,&in,&w)==TC_BATCH_RESOURCE && !memcmp(&w,&before,sizeof w));
    w.cursor_words=TC_BATCH_CURSOR_WORDS;w.output_words=0;before=w;
    assert(cetta_kernel_batch(mode,&in,&w)==TC_BATCH_RESOURCE && !memcmp(&w,&before,sizeof w));
    w.output_words=1;stages[TC_BATCH_HI1]=2;before=w;
    assert(cetta_kernel_batch(mode,&in,&w)==TC_BATCH_INVALID && !memcmp(&w,&before,sizeof w));
    stages[TC_BATCH_HI1]=1;
    assert(cetta_kernel_batch(mode,&in,&w)==TC_BATCH_EXHAUSTED && !w.produced && !w.used &&
        output[0]==0xd3d3d3d3);
    return 4;
}
int main(void) {
    char error[256];size_t controls=0;
    assert(cetta_kernel_select(CETTA_KERNEL_X86,error,sizeof error));
    NmQueryView view={.terms={NULL,inspect,child,NULL}};
    CettaKernelLimits limits={.import={128,256,32,4096},.pairs=128,.scan=128,.memo=256,
        .output_nodes=128,.output_edges=256,.output_stack=128,.fuel_budget=1000000};
    for (unsigned mode=0;mode<3;++mode) {
        controls+=batch_boundaries((CettaKernelMode)mode);
        for (unsigned phase=0;phase<5;++phase) for (long nth=0;nth<512;++nth) {
            CettaKernelSession *session=NULL;CettaKernelStatus status;
            CettaKernelConstructorPlan *plan=NULL;
            CettaKernelConstructorArgument arguments[2];
            CettaKernelConstructorKey key={1,2,7};NmHostValue template={100,7};
            failure=-1;failed=false;
            if (!phase) { failure=nth;status=cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&session,error,sizeof error); }
            else {
                assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&session,error,sizeof error)==CETTA_KERNEL_SOLVED);
                if (phase==4) {
                    assert(cetta_kernel_constructor_compile(session,&template,1,key,&plan,error,sizeof error)==CETTA_KERNEL_SOLVED);
                    for (size_t i=0;i<2;++i) assert(cetta_kernel_constructor_port(plan,i,&arguments[i].port,&arguments[i].value));
                }
                failure=nth;
                if (phase==1) {
                    NmHostConstraint constraint={{1,7},{10,0}};CettaKernelReceipt receipt;
                    status=cetta_kernel_solve(session,&constraint,1,false,&receipt,error,sizeof error);
                } else if (phase==2) {
                    NmHostValue roots[]={{100,7},{100,7}};CettaKernelOutput *output=(CettaKernelOutput *)(uintptr_t)1;NmOutputReceipt receipt;
                    status=cetta_kernel_instantiate(session,roots,2,&output,&receipt,error,sizeof error);
                    if (status!=CETTA_KERNEL_SOLVED) assert(!output);
                    if (!receipt.has_core) assert(!receipt.core.nodes_written && !receipt.core.children_written);
                    cetta_kernel_output_free(output);
                } else if (phase==3) {
                    status=cetta_kernel_constructor_compile(session,&template,1,key,&plan,error,sizeof error);
                    if (status!=CETTA_KERNEL_SOLVED) assert(!plan);
                } else {
                    CettaKernelOutput *output=(CettaKernelOutput *)(uintptr_t)1;CettaKernelConstructorReceipt receipt;
                    status=cetta_kernel_constructor_instantiate(session,plan,key,arguments,2,&output,&receipt,error,sizeof error);
                    if (status!=CETTA_KERNEL_SOLVED) assert(!output);
                    cetta_kernel_output_free(output);
                }
            }
            failure=-1;
            if (failed) {
                if (status!=CETTA_KERNEL_RESOURCE) {
                    fprintf(stderr,"allocation cause lost: mode=%u phase=%u nth=%ld status=%u\n",mode,phase,nth,(unsigned)status);abort();
                }
                if (phase==1) assert(!cetta_kernel_solution(session));
                ++controls;
            } else assert(status==CETTA_KERNEL_SOLVED);
            cetta_kernel_constructor_free(plan);cetta_kernel_session_free(session);
            if (!failed) break;
            assert(nth<511);
        }
        CettaKernelSession *session=NULL;CettaKernelLimits short_limits=limits;short_limits.fuel_budget=0;
        assert(cetta_kernel_session_open((CettaKernelMode)mode,view,short_limits,0,&session,error,sizeof error)==CETTA_KERNEL_SOLVED);
        NmHostConstraint constraint={{1,7},{10,0}};CettaKernelReceipt receipt;
        assert(cetta_kernel_solve(session,&constraint,1,false,&receipt,error,sizeof error)==CETTA_KERNEL_EXHAUSTED && !cetta_kernel_solution(session));
        CettaKernelOutput *output=(CettaKernelOutput *)(uintptr_t)1;NmOutputReceipt emitted;
        assert(cetta_kernel_instantiate(session,NULL,0,&output,&emitted,error,sizeof error)==CETTA_KERNEL_EXHAUSTED && !output);
        cetta_kernel_session_free(session);controls+=2;
    }
    cetta_kernel_shutdown();printf("PASS: %zu kernel allocation/fuel controls retain failure causes and prohibit partial publication\n",controls);
}
