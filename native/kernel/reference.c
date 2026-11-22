#include "reference.h"
#include <stdlib.h>
#include <string.h>

static bool charge(uint64_t budget,uint64_t *used) {
    if (*used>=budget) return false;
    ++*used;return true;
}
static CettaKernelStatus dereference(const NmUnifyInput *in,const uint64_t *bindings,
        uint64_t *node,uint64_t budget,uint64_t *fuel) {
    size_t depth=0;
    for (;;) {
        if (!*node || *node>=in->nodes_count) return CETTA_KERNEL_FAULT;
        NmNode n=in->nodes[*node];
        if (n.kind!=NM_NODE_VARIABLE) return CETTA_KERNEL_SOLVED;
        if (n.a>=in->variables_count) return CETTA_KERNEL_FAULT;
        if (!bindings[n.a]) return CETTA_KERNEL_SOLVED;
        if (depth++>=in->variables_count) return CETTA_KERNEL_DEFER;
        if (!charge(budget,fuel)) return CETTA_KERNEL_EXHAUSTED;
        *node=bindings[n.a];
    }
}
static CettaKernelStatus occurs(const NmUnifyInput *in,NmUnifyWork *w,uint64_t slot,
        uint64_t root,uint64_t budget,uint64_t *fuel,bool *found) {
    *found=false;size_t length=0;
    if (!w->scan_words || w->marks_words<in->nodes_count) return CETTA_KERNEL_RESOURCE;
    memset(w->marks,0,in->nodes_count*sizeof *w->marks);
    w->scan[length++]=root;
    while (length) {
        if (!charge(budget,fuel)) return CETTA_KERNEL_EXHAUSTED;
        uint64_t id=w->scan[--length];
        CettaKernelStatus status=dereference(in,w->bindings,&id,budget,fuel);
        if (status!=CETTA_KERNEL_SOLVED) return status;
        if (w->marks[id]) continue;
        w->marks[id]=1;NmNode n=in->nodes[id];
        if (n.kind==NM_NODE_VARIABLE) {
            if (n.a==slot) { *found=true;return CETTA_KERNEL_SOLVED; }
        } else if (n.kind==NM_NODE_TUPLE) {
            if (n.a>in->children_count || n.b>in->children_count-n.a) return CETTA_KERNEL_FAULT;
            if (n.b>w->scan_words-length) return CETTA_KERNEL_RESOURCE;
            for (uint64_t i=0;i<n.b;++i) w->scan[length++]=in->children[n.a+i];
        } else if (n.kind!=NM_NODE_CONSTANT) return CETTA_KERNEL_FAULT;
    }
    return CETTA_KERNEL_SOLVED;
}
CettaKernelStatus cetta_kernel_c_solve(const NmUnifyInput *in,NmUnifyWork *w,
        uint64_t budget,uint64_t used,NmUnifyResult *result) {
    uint64_t changes=0;CettaKernelStatus status=CETTA_KERNEL_SOLVED;
    if (result) *result=(NmUnifyResult){NM_INVALID,0,used};
    if (!in || !w || !result || !w->bindings || w->bindings_words<in->variables_count || !w->pairs_count)
        return CETTA_KERNEL_FAULT;
    if (in->variables_count) memcpy(w->bindings,in->initial,in->variables_count*sizeof *w->bindings);
    size_t prefix=in->nodes_count+in->variables_count;
    size_t memo_capacity=w->marks_words>prefix ? w->marks_words-prefix : 0;
    uint64_t *memo=w->marks+prefix;
    if (memo_capacity) memset(memo,0,memo_capacity*sizeof *memo);
    size_t length=1;w->pairs[0]=(NmNodePair){in->left,in->right};
    while (length) {
        if (!charge(budget,&used)) { status=CETTA_KERNEL_EXHAUSTED;break; }
        NmNodePair pair=w->pairs[--length];
        status=dereference(in,w->bindings,&pair.left,budget,&used);
        if (status!=CETTA_KERNEL_SOLVED) break;
        status=dereference(in,w->bindings,&pair.right,budget,&used);
        if (status!=CETTA_KERNEL_SOLVED) break;
        if (pair.left==pair.right) continue;
        NmNode left=in->nodes[pair.left],right=in->nodes[pair.right];
        if (left.kind==NM_NODE_VARIABLE || right.kind==NM_NODE_VARIABLE) {
            uint64_t slot=left.kind==NM_NODE_VARIABLE ? left.a : right.a;
            uint64_t value=left.kind==NM_NODE_VARIABLE ? pair.right : pair.left;
            bool found=false;status=occurs(in,w,slot,value,budget,&used,&found);
            if (status!=CETTA_KERNEL_SOLVED) break;
            if (found) { status=CETTA_KERNEL_REFUTED;break; }
            w->bindings[slot]=value;++changes;continue;
        }
        if (left.kind!=right.kind) { status=CETTA_KERNEL_REFUTED;break; }
        if (left.kind==NM_NODE_CONSTANT) {
            if (left.a!=right.a) { status=CETTA_KERNEL_REFUTED;break; }
        } else if (left.kind==NM_NODE_TUPLE) {
            if (pair.left<=UINT32_MAX && pair.right<=UINT32_MAX && memo_capacity && !(memo_capacity&(memo_capacity-1))) {
                uint64_t key=(pair.left<<32)|pair.right;
                size_t slot=(size_t)((key^(key>>29))*UINT64_C(0x9e3779b97f4a7c15))&(memo_capacity-1),probes=0;
                while (memo[slot] && memo[slot]!=key && probes++<memo_capacity) slot=(slot+1)&(memo_capacity-1);
                if (memo[slot]==key) continue;
                if (probes<memo_capacity) memo[slot]=key;
            }
            if (left.b!=right.b) { status=CETTA_KERNEL_REFUTED;break; }
            if (left.a>in->children_count || left.b>in->children_count-left.a ||
                right.a>in->children_count || right.b>in->children_count-right.a) { status=CETTA_KERNEL_FAULT;break; }
            if (left.b>w->pairs_count-length) { status=CETTA_KERNEL_RESOURCE;break; }
            for (uint64_t i=left.b;i>0;--i)
                w->pairs[length++]=(NmNodePair){in->children[left.a+i-1],in->children[right.a+i-1]};
        } else { status=CETTA_KERNEL_FAULT;break; }
    }
    NmOutcome outcome=status==CETTA_KERNEL_SOLVED ? NM_UNIFIED : status==CETTA_KERNEL_REFUTED ? NM_NO_MATCH :
        status==CETTA_KERNEL_EXHAUSTED ? NM_EXHAUSTED : status==CETTA_KERNEL_FAULT ? NM_INVALID : NM_UNSUPPORTED;
    *result=(NmUnifyResult){outcome,changes,used};return status;
}

typedef struct { uint64_t source,next,first;unsigned mode; } Frame;
static CettaKernelStatus imported(NmViewStatus status) {
    return status==NM_VIEW_RESOURCE ? CETTA_KERNEL_RESOURCE : status==NM_VIEW_DEFER ? CETTA_KERNEL_DEFER :
        status==NM_VIEW_INTERRUPTED ? CETTA_KERNEL_INTERRUPTED : CETTA_KERNEL_FAULT;
}
static NmOutputStatus output_status(CettaKernelStatus status) {
    switch (status) {
    case CETTA_KERNEL_SOLVED:return NM_OUTPUT_READY;
    case CETTA_KERNEL_EXHAUSTED:return NM_OUTPUT_EXHAUSTED;
    case CETTA_KERNEL_RESOURCE:return NM_OUTPUT_RESOURCE;
    case CETTA_KERNEL_DEFER:return NM_OUTPUT_DEFER;
    case CETTA_KERNEL_INVALIDATED:return NM_OUTPUT_INVALIDATED;
    case CETTA_KERNEL_INTERRUPTED:return NM_OUTPUT_INTERRUPTED;
    default:return NM_OUTPUT_FAULT;
    }
}
CettaKernelStatus cetta_kernel_c_output(NmQueryView view,const NmHostValue *roots,size_t count,
        CettaKernelLimits limits,uint64_t budget,uint64_t used,CettaKernelOutput **out,NmOutputReceipt *receipt) {
    CettaKernelStatus status=CETTA_KERNEL_FAULT;NmOutputReceipt r={.fuel_used=used};
    CettaKernelOutput *o=NULL;NmHostConstraint *constraints=NULL;Frame *stack=NULL;uint64_t *memo=NULL;
    size_t sp=0,nn=0,ne=0,peak=0,expanded=0,node_cap=0,edge_cap=0,stack_cap=0;uint64_t last=0;
    if (out) *out=NULL;
    if (!out || !view.terms.inspect || (count && !roots) || used>budget) goto done;
    if (view.valid && !view.valid(view.pin)) { status=CETTA_KERNEL_INVALIDATED;goto done; }
    if (!charge(budget,&used)) { status=CETTA_KERNEL_EXHAUSTED;goto done; }
    r.activated=true;
    if (count>SIZE_MAX/sizeof *constraints || count>limits.import.edges/2) { status=CETTA_KERNEL_RESOURCE;goto done; }
    o=calloc(1,sizeof *o);constraints=calloc(count ? count : 1,sizeof *constraints);
    if (!o || !constraints) { status=CETTA_KERNEL_RESOURCE;goto done; }
    o->pin=view.pin;o->valid=view.valid;
    for (size_t i=0;i<count;++i) constraints[i]=(NmHostConstraint){roots[i],roots[i]};
    NmViewStatus ready=nm_match_prepare_constraints(&view.terms,constraints,count,limits.import,&o->prepared,&r.import);
    if (ready!=NM_VIEW_READY) { status=imported(ready);goto done; }
    if (view.valid && !view.valid(view.pin)) { status=CETTA_KERNEL_INVALIDATED;goto done; }
    const NmUnifyInput *in=nm_match_input(o->prepared);
    node_cap=limits.output_nodes<in->nodes_count ? limits.output_nodes : in->nodes_count;
    edge_cap=limits.output_edges<in->children_count ? limits.output_edges : in->children_count;
    if (in->nodes_count>SIZE_MAX-in->variables_count-1) { status=CETTA_KERNEL_RESOURCE;goto done; }
    stack_cap=limits.output_stack<in->nodes_count+in->variables_count+1 ? limits.output_stack : in->nodes_count+in->variables_count+1;
    if (!node_cap || !stack_cap) { status=CETTA_KERNEL_RESOURCE;goto done; }
    o->nodes=calloc(node_cap,sizeof *o->nodes);o->children=calloc(edge_cap ? edge_cap : 1,sizeof *o->children);
    memo=calloc(in->nodes_count+in->variables_count,sizeof *memo);stack=calloc(stack_cap,sizeof *stack);
    if (!o->nodes || !o->children || !memo || !stack) { status=CETTA_KERNEL_RESOURCE;goto done; }
    nn=1;r.has_core=true;stack[sp++]=(Frame){.source=in->left};status=CETTA_KERNEL_SOLVED;peak=1;
    while (sp) {
        if (!charge(budget,&used)) { status=CETTA_KERNEL_EXHAUSTED;break; }
        Frame *frame=&stack[sp-1];uint64_t id=frame->source;
        if (!id || id>=in->nodes_count) { status=CETTA_KERNEL_FAULT;break; }
        NmNode n=in->nodes[id];
        if (frame->mode==3) {
            o->children[frame->first+frame->next++]=last;frame->mode=2;continue;
        }
        if (frame->mode==1) {
            memo[in->nodes_count+n.a]=last;memo[id]=last;--sp;continue;
        }
        if (frame->mode==2 && frame->next<n.b) {
            uint64_t child=in->children[n.a+frame->next];
            if (!child || child>=id) { status=CETTA_KERNEL_FAULT;break; }
            if (sp>=stack_cap) { status=CETTA_KERNEL_RESOURCE;break; }
            frame->mode=3;stack[sp++]=(Frame){.source=child};if (sp>peak) peak=sp;continue;
        }
        if (frame->mode==0) {
            if (memo[id]==UINT64_MAX) { status=CETTA_KERNEL_DEFER;break; }
            if (memo[id]) { last=memo[id];--sp;continue; }
            memo[id]=UINT64_MAX;++expanded;
            if (n.kind==NM_NODE_VARIABLE) {
                if (n.a>=in->variables_count || n.a>=in->keys_count ||
                    n.b!=in->keys[n.a].variable || n.c!=in->keys[n.a].scope) { status=CETTA_KERNEL_FAULT;break; }
                uint64_t *variable=&memo[in->nodes_count+n.a];
                if (*variable==UINT64_MAX) { status=CETTA_KERNEL_DEFER;break; }
                if (*variable) { last=memo[id]=*variable;--sp;continue; }
                *variable=UINT64_MAX;
                if (in->initial[n.a]) {
                    if (sp>=stack_cap) { status=CETTA_KERNEL_RESOURCE;break; }
                    frame->mode=1;stack[sp++]=(Frame){.source=in->initial[n.a]};if (sp>peak) peak=sp;continue;
                }
            } else if (n.kind==NM_NODE_TUPLE) {
                if (n.a>in->children_count || n.b>in->children_count-n.a) { status=CETTA_KERNEL_FAULT;break; }
                if (n.b>edge_cap-ne) { status=CETTA_KERNEL_RESOURCE;break; }
                frame->first=ne;ne+=(size_t)n.b;frame->mode=2;continue;
            } else if (n.kind!=NM_NODE_CONSTANT) { status=CETTA_KERNEL_FAULT;break; }
        }
        if (nn>=node_cap) { status=CETTA_KERNEL_RESOURCE;break; }
        if (n.kind==NM_NODE_TUPLE) n.a=frame->first;
        o->nodes[nn]=(NmEmitNode){n,id};last=nn++;memo[id]=last;
        if (n.kind==NM_NODE_VARIABLE) memo[in->nodes_count+n.a]=last;
        --sp;
    }
    if (status!=CETTA_KERNEL_SOLVED) goto done;
    if (view.valid && !view.valid(view.pin)) { status=CETTA_KERNEL_INVALIDATED;goto done; }
    if (!last || last>=nn || o->nodes[last].node.kind!=NM_NODE_TUPLE || o->nodes[last].node.b!=count) { status=CETTA_KERNEL_FAULT;goto done; }
    uint64_t first=o->nodes[last].node.a;
    o->graph=(NmOutputGraph){o->nodes,nn,o->children,ne,o->children+first,count};*out=o;o=NULL;
done:
    r.status=output_status(status);r.fuel_used=used;
    r.core=(NmEmitResult){status==CETTA_KERNEL_SOLVED ? NM_EMIT_FIRED : status==CETTA_KERNEL_EXHAUSTED ? NM_EMIT_EXHAUSTED : NM_EMIT_UNSUPPORTED,
        last,0,used,nn,ne,peak,expanded};
    if (receipt) *receipt=r;
    free(constraints);free(stack);free(memo);cetta_kernel_output_free(o);return status;
}
