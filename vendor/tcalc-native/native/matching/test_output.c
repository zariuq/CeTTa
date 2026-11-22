#include "native_output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bool pinned,bind,interrupted,flip_on_inspect;
    size_t pin_calls,invalid_at;
} Source;
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    Source *s=context;
    if (s->flip_on_inspect) s->pinned=false;
    if (s->interrupted) return NM_VIEW_INTERRUPTED;
    if (!value.syntax) *node=(NmViewNode){.kind=NM_NODE_TUPLE};
    else if (value.syntax<10) *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}};
    else if (value.syntax<100) *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax-10};
    else if (value.syntax==100) *node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=3};
    else return NM_VIEW_DEFER;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context,NmHostValue value,uint64_t index,NmHostValue *out) {
    (void)context;if (value.syntax!=100 || index>=3) return NM_VIEW_FAULT;
    *out=index==1 ? (NmHostValue){1,12} : (NmHostValue){1,11};return NM_VIEW_READY;
}
static NmViewStatus binding(void *context,NmHostValue value,bool *bound,NmHostValue *out) {
    Source *s=context;*bound=s->bind && value.syntax==1 && value.context==11;
    *out=(NmHostValue){10,0};return NM_VIEW_READY;
}
static bool valid(void *context) {
    Source *s=context;++s->pin_calls;
    return s->pinned && (!s->invalid_at || s->pin_calls<s->invalid_at);
}
static const NmOutputLimits limits={.import={200,300,32,2000},.nodes=200,.children=300,.stack=200};
static NmSolution *capture(const NmUnifyProgram *p,NmTermView view) {
    NmPreparedMatch *g=NULL;
    assert(nm_match_prepare(&view,(NmHostValue){1,11},(NmHostValue){10,0},limits.import,&g,NULL)==NM_VIEW_READY);
    const NmUnifyInput *in=nm_match_input(g);
    uint64_t bindings[8],scan[32],state[40],marks[256];NmNodePair pairs[32];
    assert(in->nodes_count+in->variables_count+128<=256);
    NmUnifyWork w={bindings,8,pairs,32,scan,32,state,40,marks,in->nodes_count+in->variables_count+128};
    NmUnifyResult r;char error[256];
    assert(nm_unify_call(p,NM_COMPARE,in,&w,10000,0,&r,error,sizeof error)==NM_CALL_OK && r.outcome==NM_UNIFIED);
    NmSolution *solution=NULL;assert(nm_solution_capture(g,&r,&w,&solution)==NM_VIEW_READY);
    nm_match_prepared_free(g);return solution;
}
static void shape(const NmOutput *o,bool bound) {
    const NmOutputGraph *g=nm_output_graph(o);assert(g && g->roots_count==4);
    uint64_t a=g->roots[0];assert(a==g->roots[1] && a<g->nodes_count);
    NmNode n=g->nodes[a].node;assert(n.kind==NM_NODE_TUPLE && n.b==3);
    uint64_t x=g->children[n.a],y=g->children[n.a+1];assert(x==g->children[n.a+2] && x!=y);
    NmNode vx=g->nodes[x].node,vy=g->nodes[y].node;
    if (bound) assert(vx.kind==NM_NODE_CONSTANT && vx.a==0);
    else assert(vx.kind==NM_NODE_VARIABLE && vx.b==1 && vx.c==11);
    assert(vy.kind==NM_NODE_VARIABLE && vy.b==1 && vy.c==12);
    assert(g->nodes[g->roots[2]].node.kind==NM_NODE_TUPLE && !g->nodes[g->roots[2]].node.b);
    assert(g->nodes[g->roots[3]].node.kind==NM_NODE_CONSTANT && !g->nodes[g->roots[3]].node.a);
    NmHostValue origin={999,999};assert(nm_output_origin(o,g->roots[2],&origin) && !origin.syntax && !origin.context);
    assert(nm_output_origin(o,a,&origin) && origin.syntax==100 && origin.context==11);
    assert(!nm_output_origin(o,0,&origin) && !nm_output_origin(o,g->nodes_count,&origin));
    assert(!nm_output_origin(o,g->nodes_count-1,&origin));
    for (size_t i=1;i<g->nodes_count;++i) if (g->nodes[i].node.kind==NM_NODE_TUPLE) {
        n=g->nodes[i].node;
        for (size_t j=0;j<n.b;++j) assert(g->children[n.a+j] && g->children[n.a+j]<i);
    }
}
int main(void) {
    char error[256];NmEmitProgram *program=nm_emit_program_default(true,error,sizeof error);assert(program);
    NmHostValue templates[]={{100,11},{100,11},{0,0},{10,0}};size_t calls=0;
    for (unsigned mode=0;mode<3;++mode) for (unsigned bound=0;bound<2;++bound) {
        Source s={.pinned=true,.bind=bound};
        NmQueryView view={.terms={&s,inspect,child,binding},.pin=&s,.valid=valid};
        NmOutput *o=NULL;NmOutputReceipt r;
        assert(nm_output_build(program,(NmExecution)mode,view,templates,4,limits,10000,7,&o,&r,error,sizeof error)==NM_OUTPUT_READY);
        assert(o && r.activated && r.has_core && r.fuel_used>8 && r.core.outcome==NM_EMIT_FIRED);
        uint64_t cost=r.fuel_used-7;shape(o,bound);nm_output_free(o);++calls;
        for (int delta=-1;delta<=1;++delta) {
            s.pin_calls=0;uint64_t budget=7+cost+(uint64_t)delta;
            NmOutputStatus status=nm_output_build(program,(NmExecution)mode,view,templates,4,limits,budget,7,&o,&r,error,sizeof error);
            if (delta<0) assert(status==NM_OUTPUT_EXHAUSTED && !o && r.fuel_used==budget && r.has_core);
            else { assert(status==NM_OUTPUT_READY && o && r.fuel_used==7+cost);shape(o,bound);nm_output_free(o); }
            ++calls;
        }
        assert(nm_output_build(program,(NmExecution)mode,view,NULL,0,limits,10000,0,&o,&r,error,sizeof error)==NM_OUTPUT_READY);
        assert(nm_output_graph(o)->roots_count==0);nm_output_free(o);++calls;
        for (size_t failure=0;failure<10;++failure) {
            NmOutputLimits cap=limits;s.pin_calls=0;s.invalid_at=0;s.pinned=true;s.interrupted=false;s.flip_on_inspect=false;
            NmOutputStatus expected=NM_OUTPUT_RESOURCE;uint64_t budget=10000;
            if (failure==0) cap.nodes=1;
            if (failure==1) cap.children=1;
            if (failure==2) cap.stack=1;
            if (failure==3) cap.import.callbacks=1;
            if (failure==4) { budget=0;expected=NM_OUTPUT_EXHAUSTED; }
            if (failure==5) { s.pinned=false;expected=NM_OUTPUT_INVALIDATED; }
            if (failure==6) { s.flip_on_inspect=true;expected=NM_OUTPUT_INVALIDATED; }
            if (failure==7) { s.invalid_at=3;expected=NM_OUTPUT_INVALIDATED; }
            if (failure==8) { s.interrupted=true;expected=NM_OUTPUT_INTERRUPTED; }
            NmHostValue unsupported={999,0};
            if (failure==9) expected=NM_OUTPUT_DEFER;
            NmOutputStatus got=nm_output_build(program,(NmExecution)mode,view,failure==9 ? &unsupported : templates,
                failure==9 ? 1 : 4,cap,budget,0,&o,&r,error,sizeof error);
            if (got!=expected) fprintf(stderr,"case %zu mode %u: %u != %u: %s\n",failure,mode,got,expected,error);
            assert(got==expected && !o && r.status==got && r.fuel_used<=budget);++calls;
            if (failure==3 || failure==9) assert(r.activated && !r.has_core && r.fuel_used==1);
            if (failure==5) assert(!r.activated && !r.fuel_used);
        }
        s=(Source){.pinned=true,.bind=bound};
        assert(nm_output_build(program,(NmExecution)mode,view,templates,4,limits,10000,0,&o,NULL,NULL,0)==NM_OUTPUT_READY);
        shape(o,bound);s.pinned=false;
        NmHostValue origin;assert(!nm_output_graph(o) && !nm_output_origin(o,1,&origin));nm_output_free(o);++calls;
    }
    NmUnifyProgram *unifier=nm_unify_program_default(true,error,sizeof error);assert(unifier);
    Source source={.pinned=true};NmTermView source_view={&source,inspect,child,binding};
    NmSolution *solution=capture(unifier,source_view);
    NmQueryView solved={.terms=nm_solution_view(solution),.pin=&source,.valid=valid};
    NmOutput *retained=NULL;NmOutputReceipt receipt;
    assert(nm_output_build(program,NM_COMPARE,solved,templates,4,limits,10000,0,&retained,&receipt,error,sizeof error)==NM_OUTPUT_READY);
    nm_solution_free(solution);source.interrupted=true;nm_unify_program_close(unifier);
    shape(retained,true);nm_output_free(retained);++calls;
    nm_output_free(NULL);assert(!nm_output_graph(NULL));nm_emit_program_close(program);
    printf("native output bundle: %zu constructions; duplicate roots, scopes, origins, complete graphs, fuel boundaries and invalidated views passed\n",calls);
    return 0;
}
