#define _POSIX_C_SOURCE 200809L
#include "../native/kernel/constructor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { bool valid,captured,ports_bound,large;NmHostValue arguments[2]; } Source;
static bool valid(void *raw) { return ((Source *)raw)->valid; }
static NmViewStatus inspect(void *raw,NmHostValue v,NmViewNode *n) {
    Source *s=raw;
    if (!v.syntax) *n=(NmViewNode){.kind=NM_NODE_TUPLE};
    else if (v.syntax<10) *n=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={v.syntax,v.context}};
    else if (v.syntax<200) *n=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=v.syntax};
    else if (v.syntax<=202 || (s->large && v.syntax<=230)) *n=(NmViewNode){.kind=NM_NODE_TUPLE,.children=v.syntax==200 ? 3 : 2};
    else return NM_VIEW_DEFER;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *raw,NmHostValue v,uint64_t i,NmHostValue *out) {
    Source *s=raw;
    if (s->large && v.syntax>202 && v.syntax<=230 && i<2) { *out=(NmHostValue){v.syntax-1,v.context};return NM_VIEW_READY; }
    static const uint64_t children[][3]={{100,1,2},{101,2,0},{200,200,0}};
    if (v.syntax<200 || v.syntax>202 || i>=(v.syntax==200 ? 3u : 2u)) return NM_VIEW_FAULT;
    *out=(NmHostValue){children[v.syntax-200][i],v.context};return NM_VIEW_READY;
}
static NmViewStatus binding(void *raw,NmHostValue v,bool *bound,NmHostValue *out) {
    Source *s=raw;
    if (s->ports_bound && v.context==70 && v.syntax>=1 && v.syntax<=2) { *bound=true;*out=s->arguments[v.syntax-1];return NM_VIEW_READY; }
    *bound=s->captured && v.syntax==3;
    *out=(NmHostValue){105,v.context};return NM_VIEW_READY;
}
static uint64_t mix(uint64_t a,uint64_t b) { return (a^b)*UINT64_C(0x9e3779b97f4a7c15)+(a>>23); }
static uint64_t digest(const CettaKernelOutput *o) {
    const NmOutputGraph *g=cetta_kernel_output_graph(o);assert(g);
    uint64_t *memo=calloc(g->nodes_count,sizeof *memo);assert(memo);
    for (size_t i=1;i<g->nodes_count;++i) {
        NmNode n=g->nodes[i].node;uint64_t h=n.kind+1;
        if (n.kind==NM_NODE_CONSTANT) h=mix(h,n.a);
        else if (n.kind==NM_NODE_VARIABLE) h=mix(mix(h,n.b),n.c);
        else { h=mix(h,n.b);assert(n.a<=g->children_count && n.b<=g->children_count-n.a);
            for (uint64_t j=0;j<n.b;++j) { assert(g->children[n.a+j]<i);h=mix(h,memo[g->children[n.a+j]]); }
        }
        memo[i]=h;
    }
    uint64_t h=g->roots_count;for (size_t i=0;i<g->roots_count;++i) h=mix(h,memo[g->roots[i]]);
    free(memo);return h;
}
static double seconds(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static void benchmark(bool complex) {
    Source source={.valid=true,.ports_bound=true,.large=true};
    NmQueryView view={{&source,inspect,child,binding},&source,valid};
    CettaKernelLimits limits={.import={4096,8192,128,50000},.pairs=4096,.scan=4096,.memo=4096,
        .output_nodes=4096,.output_edges=8192,.output_stack=4096,.fuel_budget=UINT64_MAX};
    CettaKernelConstructorKey key={1,7,70};NmHostValue roots[16];
    size_t root_count=complex ? 16u : 1u;
    for (size_t i=0;i<root_count;++i) roots[i]=(NmHostValue){complex ? 228u : 200u,70};
    char error[256]={0};double generic[3],planned[3],cold[3];
    for (unsigned mode=0;mode<3;++mode) {
        CettaKernelSession *s=NULL;assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
        CettaKernelConstructorPlan *p=NULL;double begin=seconds();
        assert(cetta_kernel_constructor_compile(s,roots,root_count,key,&p,error,sizeof error)==CETTA_KERNEL_SOLVED);cold[mode]=seconds()-begin;
        CettaKernelConstructorArgument arguments[2];
        for (size_t i=0;i<2;++i) assert(cetta_kernel_constructor_port(p,i,&arguments[i].port,&arguments[i].value));
        cetta_kernel_session_free(s);s=NULL;
        for (unsigned route=0;route<2;++route) {
            begin=seconds();
            for (unsigned n=0;n<300;++n) {
                for (size_t i=0;i<2;++i) arguments[i].value=source.arguments[i]=(NmHostValue){3,UINT64_C(0xf000000000000000)+2*n+i};
                CettaKernelOutput *o=NULL;
                assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
                if (route) { CettaKernelConstructorReceipt r;assert(cetta_kernel_constructor_instantiate(s,p,key,arguments,2,&o,&r,error,sizeof error)==CETTA_KERNEL_SOLVED); }
                else { NmOutputReceipt r;assert(cetta_kernel_instantiate(s,roots,root_count,&o,&r,error,sizeof error)==CETTA_KERNEL_SOLVED); }
                assert(cetta_kernel_output_graph(o)->roots_count==root_count);cetta_kernel_output_free(o);
                cetta_kernel_session_free(s);s=NULL;
            }
            if (route) planned[mode]=(seconds()-begin)/300;else generic[mode]=(seconds()-begin)/300;
        }
        cetta_kernel_constructor_free(p);cetta_kernel_session_free(s);
    }
    printf("{\"family\":\"%s\",\"runs\":300,\"seconds_per_bundle\":{",complex ? "16 outputs over a shared fixed constructor DAG" : "one small output");
    for (unsigned i=0;i<3;++i) printf("%s\"%s\":{\"generic\":%.9f,\"planned\":%.9f,\"compile\":%.9f}",i ? "," : "",cetta_kernel_name((CettaKernelMode)i),generic[i],planned[i],cold[i]);
    puts("}}");cetta_kernel_shutdown();
}
int main(int argc,char **argv) {
    if (argc==2 && (!strcmp(argv[1],"--bench") || !strcmp(argv[1],"--bench-small"))) { benchmark(!strcmp(argv[1],"--bench"));return 0; }

    Source source={.valid=true};char error[256];size_t checks=0;
    NmQueryView view={{&source,inspect,child,binding},&source,valid};
    CettaKernelLimits limits={.import={1024,2048,128,10000},.pairs=1024,.scan=1024,.memo=2048,
        .output_nodes=1024,.output_edges=2048,.output_stack=1024,.fuel_budget=UINT64_MAX};
    CettaKernelConstructorKey key={1,7,70};NmHostValue roots[]={{202,70},{201,70},{202,70},{0,0}};
    for (unsigned mode=0;mode<3;++mode) {
        CettaKernelSession *s=NULL;assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
        CettaKernelConstructorPlan *plan=NULL;
        assert(cetta_kernel_constructor_compile(s,roots,4,key,&plan,error,sizeof error)==CETTA_KERNEL_SOLVED);
        assert(cetta_kernel_constructor_ports(plan)==2);
        cetta_kernel_session_free(s);s=NULL;
        for (unsigned specimen=0;specimen<24;++specimen) {
            source.captured=(specimen%3)==0;
            assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelConstructorArgument arguments[2];
            for (size_t i=0;i<2;++i) {
                NmHostValue original;assert(cetta_kernel_constructor_port(plan,i,&arguments[i].port,&original));
                arguments[i].value=specimen%2 ? (NmHostValue){100+specimen+i,0} :
                    (NmHostValue){3,UINT64_C(0xf000000000000000)+2*specimen+i};
            }
            CettaKernelSession *oracle=NULL;assert(cetta_kernel_session_open(CETTA_KERNEL_C,view,limits,0,&oracle,error,sizeof error)==CETTA_KERNEL_SOLVED);
            NmHostConstraint cs[]={{{1,70},arguments[0].value},{{2,70},arguments[1].value}};CettaKernelReceipt solved;
            assert(cetta_kernel_solve(oracle,cs,2,false,&solved,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelOutput *expected=NULL,*actual=NULL;NmOutputReceipt generic;CettaKernelConstructorReceipt receipt;
            assert(cetta_kernel_instantiate(oracle,roots,4,&expected,&generic,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelStatus status=cetta_kernel_constructor_instantiate(s,plan,key,arguments,2,&actual,&receipt,error,sizeof error);
            if (status!=CETTA_KERNEL_SOLVED) { fprintf(stderr,"constructor failed mode=%u specimen=%u status=%u error=%s\n",mode,specimen,(unsigned)status,error);abort(); }
            assert(digest(actual)==digest(expected));
            const NmOutputGraph *g=cetta_kernel_output_graph(actual);assert(g->roots[0]==g->roots[2]);
            NmHostValue origin;assert(cetta_kernel_output_origin(actual,g->roots[0],&origin) && origin.syntax==202 && origin.context==70);
            cetta_kernel_output_free(actual);cetta_kernel_output_free(expected);cetta_kernel_session_free(oracle);cetta_kernel_session_free(s);s=NULL;++checks;
        }
        assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&s,error,sizeof error)==CETTA_KERNEL_SOLVED);
        CettaKernelConstructorArgument arguments[2];
        for (size_t i=0;i<2;++i) assert(cetta_kernel_constructor_port(plan,i,&arguments[i].port,&arguments[i].value));
        CettaKernelCheckpoint *empty=cetta_kernel_checkpoint(s);assert(empty);
        uint64_t prior_hash=0;
        for (unsigned revision=0;revision<2;++revision) {
            assert(cetta_kernel_restore(s,empty));
            NmHostConstraint constraint={{1,70},{110+revision,0}};CettaKernelReceipt solved;
            assert(cetta_kernel_solve(s,&constraint,1,false,&solved,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelOutput *o=NULL;CettaKernelConstructorReceipt r;
            assert(cetta_kernel_constructor_instantiate(s,plan,key,arguments,2,&o,&r,error,sizeof error)==CETTA_KERNEL_SOLVED);
            uint64_t actual=digest(o);if (revision) assert(actual!=prior_hash);prior_hash=actual;
            cetta_kernel_output_free(o);++checks;
        }
        assert(cetta_kernel_restore(s,empty));cetta_kernel_checkpoint_free(empty);
        CettaKernelOutput *full=NULL;CettaKernelConstructorReceipt full_receipt;
        assert(cetta_kernel_constructor_instantiate(s,plan,key,arguments,2,&full,&full_receipt,error,sizeof error)==CETTA_KERNEL_SOLVED);
        cetta_kernel_output_free(full);
        CettaKernelSession *fresh=NULL;
        assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limits,0,&fresh,error,sizeof error)==CETTA_KERNEL_SOLVED);
        assert(cetta_kernel_constructor_instantiate(fresh,plan,key,arguments,2,&full,&full_receipt,error,sizeof error)==CETTA_KERNEL_SOLVED);
        uint64_t exact_fuel=full_receipt.fuel_used;size_t exact_nodes=full_receipt.constructor.nodes_written;
        size_t exact_edges=full_receipt.constructor.children_written;
        cetta_kernel_output_free(full);cetta_kernel_session_free(fresh);
        for (unsigned boundary=0;boundary<3;++boundary) {
            CettaKernelLimits limited=limits;
            if (!boundary) limited.fuel_budget=exact_fuel-1;
            else if (boundary==1) limited.output_nodes=exact_nodes-1;
            else limited.output_edges=exact_edges-1;
            assert(cetta_kernel_session_open((CettaKernelMode)mode,view,limited,0,&fresh,error,sizeof error)==CETTA_KERNEL_SOLVED);
            CettaKernelConstructorReceipt r;CettaKernelOutput *o=(CettaKernelOutput *)(uintptr_t)1;
            CettaKernelStatus status=cetta_kernel_constructor_instantiate(fresh,plan,key,arguments,2,&o,&r,error,sizeof error);
            assert(status==(boundary ? CETTA_KERNEL_RESOURCE : CETTA_KERNEL_EXHAUSTED) && !o);
            assert(r.executed && !cetta_kernel_solution(fresh));
            cetta_kernel_session_free(fresh);++checks;
        }
        CettaKernelOutput *out=(CettaKernelOutput *)(uintptr_t)1;CettaKernelConstructorReceipt receipt;
        assert(cetta_kernel_constructor_instantiate(s,plan,(CettaKernelConstructorKey){2,7,70},arguments,2,&out,&receipt,error,sizeof error)==CETTA_KERNEL_INVALIDATED && !out);
        assert(cetta_kernel_constructor_instantiate(s,plan,(CettaKernelConstructorKey){1,8,70},arguments,2,&out,&receipt,error,sizeof error)==CETTA_KERNEL_INVALIDATED && !out);
        assert(cetta_kernel_constructor_instantiate(s,plan,(CettaKernelConstructorKey){1,7,71},arguments,2,&out,&receipt,error,sizeof error)==CETTA_KERNEL_INVALIDATED && !out);
        checks+=2;
        arguments[0].port.scope^=1;
        assert(cetta_kernel_constructor_instantiate(s,plan,key,arguments,2,&out,&receipt,error,sizeof error)==CETTA_KERNEL_DEFER && !out);
        source.valid=false;assert(cetta_kernel_constructor_instantiate(s,plan,key,arguments,2,&out,&receipt,error,sizeof error)==CETTA_KERNEL_INVALIDATED && !out);source.valid=true;
        cetta_kernel_constructor_free(plan);cetta_kernel_session_free(s);checks+=3;
    }
    cetta_kernel_shutdown();printf("PASS: %zu compiled constructor comparisons and controls preserve full contexts, repeated roots, captures and generation/policy guards\n",checks);
}
