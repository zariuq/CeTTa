#include "native_emit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t before;NmEmitNode nodes[16];uint64_t after_nodes;
    uint64_t edges[64],after_edges,memo[32],after_memo;
    NmEmitFrame stack[16];uint64_t after_stack,state[40],after;
} Storage;
static NmEmitWork work(Storage *s) {
    return (NmEmitWork){s->nodes,16,s->edges,64,s->memo,9,s->stack,16,s->state,40};
}
static void reset(Storage *s) { memset(s,0x5A,sizeof *s); }
static void guards(const Storage *s) {
    uint64_t guard=UINT64_C(0x5A5A5A5A5A5A5A5A);
    assert(s->before==guard && s->after_nodes==guard && s->after_edges==guard &&
        s->after_memo==guard && s->after_stack==guard && s->after==guard);
    for (size_t i=9;i<32;++i) assert(s->memo[i]==guard);
}
static bool same(NmEmitResult a,NmEmitResult b) {
    return a.outcome==b.outcome && a.root==b.root && a.reason==b.reason && a.fuel_used==b.fuel_used &&
        a.nodes_written==b.nodes_written && a.children_written==b.children_written &&
        a.peak_stack==b.peak_stack && a.expanded==b.expanded;
}
int main(int argc,char **argv) {
    assert(argc==2);char error[256];
    NmEmitProgram *program=nm_emit_program_default(true,error,sizeof error);assert(program);
    NmEmitProgram *file=nm_emit_program_open(argv[1],true,error,sizeof error);assert(file);
    NmEmitProgram *reference=nm_emit_program_default(false,error,sizeof error);assert(reference);
    assert(nm_emit_state_words(program)==40 && !nm_emit_native_bytes(reference,NULL));
    size_t bytes=0;assert(nm_emit_native_bytes(program,&bytes) && bytes>4);
    const NmNode nodes[]={{0,0,0,0},{1,0,41,9},{1,1,41,10},{0,77,0,0},
        {1,0,41,9},{2,0,2,0},{2,2,3,0}};
    const uint64_t edges[]={1,2,5,5,4};const NmVariableKey keys[]={{41,9},{41,10}};
    uint64_t bindings[]={3,0};NmEmitInput input={nodes,7,edges,5,bindings,2,keys,2,6};
    const NmEmitNode expected[]={{{0,0,0,0},0},{{0,77,0,0},3},{{1,1,41,10},2},
        {{2,3,2,0},5},{{2,0,3,0},6}};
    const uint64_t out_edges[]={3,3,1,1,2};size_t calls=0;
    Storage baseline;reset(&baseline);NmEmitWork bw=work(&baseline);NmEmitResult br;
    assert(nm_emit_call(reference,NM_REFERENCE,&input,&bw,10000,0,&br,error,sizeof error)==NM_CALL_OK);
    assert(br.outcome==NM_EMIT_FIRED && br.root==4 && br.nodes_written==5 && br.children_written==5 &&
        !memcmp(baseline.nodes,expected,sizeof expected) && !memcmp(baseline.edges,out_edges,sizeof out_edges));
    for (unsigned mode=0;mode<3;++mode) {
        Storage actual;reset(&actual);NmEmitWork w=work(&actual);NmEmitResult r;
        assert(nm_emit_call(mode%2 ? file : program,(NmExecution)mode,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK);
        assert(same(r,br) && !memcmp(&baseline,&actual,sizeof actual));guards(&actual);++calls;
        for (unsigned capacity=0;capacity<4;++capacity) {
            reset(&actual);w=work(&actual);
            if (capacity==0) w.nodes_count=2;
            if (capacity==1) w.children_count=1;
            if (capacity==2) w.stack_count=1;
            if (capacity==3) w.memo_words=1;
            assert(nm_emit_call(program,(NmExecution)mode,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK);
            const uint64_t reason[]={4,5,7,6};assert(r.outcome==NM_EMIT_UNSUPPORTED && r.reason==reason[capacity]);
            guards(&actual);++calls;
        }
        reset(&actual);w=work(&actual);
        assert(nm_emit_call(program,(NmExecution)mode,&input,&w,0,0,&r,error,sizeof error)==NM_CALL_OK);
        assert(r.outcome==NM_EMIT_EXHAUSTED && !r.fuel_used && r.nodes_written==1);guards(&actual);++calls;
        w.state_words=39;Storage saved=actual;
        assert(nm_emit_call(program,(NmExecution)mode,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK);
        assert(r.outcome==NM_EMIT_INVALID && !memcmp(&actual,&saved,sizeof actual));++calls;
        reset(&actual);w=work(&actual);w.nodes=(NmEmitNode *)(uintptr_t)nodes;w.nodes_count=2;
        assert(nm_emit_call(program,(NmExecution)mode,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK);
        assert(r.outcome==NM_EMIT_INVALID && bindings[0]==3 && !bindings[1]);++calls;
        reset(&actual);w=work(&actual);bindings[0]=2;bindings[1]=1;
        assert(nm_emit_call(program,(NmExecution)mode,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK);
        assert(r.outcome==NM_EMIT_UNSUPPORTED && r.reason==8 && bindings[0]==2 && bindings[1]==1);++calls;
        bindings[0]=3;bindings[1]=0;
    }
    Storage data;reset(&data);Storage saved=data;NmEmitWork w=work(&data);NmEmitResult r;
    assert(nm_emit_call(reference,NM_NATIVE,&input,&w,10000,0,&r,error,sizeof error)==NM_CALL_BAD_ARGUMENT);
    assert(!memcmp(&data,&saved,sizeof data));
    nm_emit_program_close(program);nm_emit_program_close(file);nm_emit_program_close(reference);
    nm_emit_program_close(NULL);assert(!nm_emit_program_open(NULL,true,error,sizeof error));
    printf("native output API: %zu calls across all modes; complete graph/origins, sharing, scope, cycles, capacities, aliases and receipts passed\n",calls);
    return 0;
}
