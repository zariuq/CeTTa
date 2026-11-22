#include "native_output.h"
#include <stdio.h>
#include <stdlib.h>

struct NmOutput {
    NmPreparedMatch *prepared;
    NmEmitNode *nodes;
    uint64_t *children;
    NmOutputGraph graph;
    void *pin;
    bool (*valid)(void *);
};
static bool valid(const NmOutput *o) { return !o->valid || o->valid(o->pin); }
const NmOutputGraph *nm_output_graph(const NmOutput *o) { return o && valid(o) ? &o->graph : NULL; }
bool nm_output_origin(const NmOutput *o,uint64_t node,NmHostValue *origin) {
    return o && valid(o) && node<o->graph.nodes_count &&
        nm_match_origin(o->prepared,o->nodes[node].source,origin);
}
void nm_output_free(NmOutput *o) {
    if (o) { nm_match_prepared_free(o->prepared);free(o->nodes);free(o->children);free(o); }
}
static size_t minimum(size_t a,size_t b) { return a<b ? a : b; }
static void *allocate(size_t count,size_t width) { return calloc(count ? count : 1,width); }
static NmOutputStatus imported(NmViewStatus status) {
    switch (status) {
    case NM_VIEW_RESOURCE:return NM_OUTPUT_RESOURCE;
    case NM_VIEW_DEFER:return NM_OUTPUT_DEFER;
    case NM_VIEW_INTERRUPTED:return NM_OUTPUT_INTERRUPTED;
    default:return NM_OUTPUT_FAULT;
    }
}
NmOutputStatus nm_output_build(const NmEmitProgram *program,NmExecution execution,
    NmQueryView view,const NmHostValue *templates,size_t count,NmOutputLimits limits,
    uint64_t budget,uint64_t used,NmOutput **out,NmOutputReceipt *receipt,char *error,size_t size) {
    if (out) *out=NULL;
    if (error && size) *error=0;
    NmOutputReceipt r={.status=NM_OUTPUT_FAULT,.fuel_used=used};
    NmOutput *o=NULL;NmHostConstraint *constraints=NULL;
    NmEmitWork w={0};
    if (!program || !out || !view.terms.inspect || (count && !templates) ||
        (unsigned)execution>(unsigned)NM_COMPARE || used>budget) goto finish;
    if (view.valid && !view.valid(view.pin)) { r.status=NM_OUTPUT_INVALIDATED;goto finish; }
    if (used==budget) { r.status=NM_OUTPUT_EXHAUSTED;goto finish; }
    ++r.fuel_used;r.activated=true;
    if (count>limits.import.edges/2 || count>SIZE_MAX/sizeof *constraints ||
        !limits.nodes || !limits.stack) { r.status=NM_OUTPUT_RESOURCE;goto finish; }
    o=calloc(1,sizeof *o);constraints=allocate(count,sizeof *constraints);
    if (!o || !constraints) { r.status=NM_OUTPUT_RESOURCE;goto finish; }
    o->pin=view.pin;o->valid=view.valid;
    for (size_t i=0;i<count;++i) constraints[i]=(NmHostConstraint){templates[i],templates[i]};
    NmViewStatus status=nm_match_prepare_constraints(&view.terms,constraints,count,limits.import,
        &o->prepared,&r.import);
    if (status!=NM_VIEW_READY) { r.status=imported(status);goto finish; }
    if (!valid(o)) { r.status=NM_OUTPUT_INVALIDATED;goto finish; }
    const NmUnifyInput *in=nm_match_input(o->prepared);
    if (in->nodes_count>SIZE_MAX-in->variables_count-1) { r.status=NM_OUTPUT_RESOURCE;goto finish; }
    w=(NmEmitWork){.nodes_count=minimum(limits.nodes,in->nodes_count),
        .children_count=minimum(limits.children,in->children_count),
        .memo_words=in->nodes_count+in->variables_count,
        .stack_count=minimum(limits.stack,in->nodes_count+in->variables_count+1),
        .state_words=nm_emit_state_words(program)};
    w.nodes=allocate(w.nodes_count,sizeof *w.nodes);w.children=allocate(w.children_count,sizeof *w.children);
    w.memo=allocate(w.memo_words,sizeof *w.memo);w.stack=allocate(w.stack_count,sizeof *w.stack);
    w.state=allocate(w.state_words,sizeof *w.state);
    if (!w.nodes || !w.children || !w.memo || !w.stack || !w.state) { r.status=NM_OUTPUT_RESOURCE;goto finish; }
    NmEmitInput input={in->nodes,in->nodes_count,in->children,in->children_count,in->initial,
        in->variables_count,in->keys,in->keys_count,in->left};
    NmCallStatus called=nm_emit_call(program,execution,&input,&w,budget,r.fuel_used,&r.core,error,size);
    if (called!=NM_CALL_OK) { r.status=called==NM_CALL_RESOURCE ? NM_OUTPUT_RESOURCE : NM_OUTPUT_FAULT;goto finish; }
    r.has_core=true;r.fuel_used=r.core.fuel_used;
    if (!valid(o)) { r.status=NM_OUTPUT_INVALIDATED;goto finish; }
    if (r.core.outcome==NM_EMIT_EXHAUSTED) { r.status=NM_OUTPUT_EXHAUSTED;goto finish; }
    if (r.core.outcome==NM_EMIT_UNSUPPORTED) {
        r.status=r.core.reason==2 || r.core.reason==3 || r.core.reason==8 ? NM_OUTPUT_DEFER : NM_OUTPUT_RESOURCE;
        goto finish;
    }
    if (r.core.outcome!=NM_EMIT_FIRED || !r.core.root || r.core.root>=r.core.nodes_written) goto finish;
    NmNode root=w.nodes[r.core.root].node;
    if (root.kind!=NM_NODE_TUPLE || root.b!=count || root.a>r.core.children_written ||
        root.b>r.core.children_written-root.a) goto finish;
    for (size_t i=0;i<count;++i)
        if (!w.children[root.a+i] || w.children[root.a+i]>=r.core.root) goto finish;
    o->nodes=w.nodes;o->children=w.children;
    o->graph=(NmOutputGraph){o->nodes,r.core.nodes_written,o->children,r.core.children_written,
        o->children+root.a,count};
    w.nodes=NULL;w.children=NULL;*out=o;o=NULL;r.status=NM_OUTPUT_READY;
finish:
    free(constraints);free(w.nodes);free(w.children);free(w.memo);free(w.stack);free(w.state);nm_output_free(o);
    if (r.status!=NM_OUTPUT_READY && error && size && !*error)
        snprintf(error,size,"output construction did not complete (status %u)",(unsigned)r.status);
    if (receipt) *receipt=r;
    return r.status;
}
