#include "constructor.h"
#include "constructor_layout.generated.h"
#include "kernel_private.h"
#include "reference.h"
#include "../../vendor/tcalc-native/native/matching/native_component.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(NmNode)==32 && offsetof(NmNode,a)==8*NC_N_A &&
    offsetof(NmNode,b)==8*NC_N_B && offsetof(NmNode,c)==8*NC_N_C,"constructor node layout");
_Static_assert(sizeof(NmEmitNode)==40 && offsetof(NmEmitNode,source)==32,"constructor output layout");
_Static_assert((int)NM_NODE_CONSTANT==(int)NC_K_CONSTANT && (int)NM_NODE_VARIABLE==(int)NC_K_VARIABLE && (int)NM_NODE_TUPLE==(int)NC_K_TUPLE,"constructor tags");
struct CettaKernelConstructorProgram { NmComponent component; };
extern const uint8_t ck_constructor_entry[],ck_constructor_image[],ck_constructor_image_end[],ck_constructor_expected[],ck_constructor_expected_end[];
extern const char ck_constructor_text[],ck_constructor_text_end[];
static const NmComponentImage image={"native_constructor","native constructor image differs from its expected bytes",
    "native constructor has the wrong region profile",ck_constructor_entry,ck_constructor_image,ck_constructor_image_end,
    ck_constructor_expected,ck_constructor_expected_end,ck_constructor_text,ck_constructor_text_end,8,{1,1,1,2,2,2,2,1}};
CettaKernelConstructorProgram *cetta_kernel_constructor_program(char *error,size_t size) {
    CettaKernelConstructorProgram *p=calloc(1,sizeof *p);if (!p) return NULL;
    if (!nm_component_init(&p->component,&image,NULL,true,error,size)) { free(p);return NULL; }
    return p;
}
void cetta_kernel_constructor_program_free(CettaKernelConstructorProgram *p) {
    if (p) { nm_component_dispose(&p->component);free(p); }
}
struct CettaKernelConstructorPlan {
    NmPreparedMatch *prepared;
    NmQueryView source;
    CettaKernelConstructorKey key;
    NmHostValue *ports;
    size_t port_count,roots_count,nodes_count;
};
static bool valid(NmQueryView view) { return !view.valid || view.valid(view.pin); }
static CettaKernelStatus imported(NmViewStatus status) {
    return status==NM_VIEW_READY ? CETTA_KERNEL_SOLVED : status==NM_VIEW_RESOURCE ? CETTA_KERNEL_RESOURCE :
        status==NM_VIEW_DEFER ? CETTA_KERNEL_DEFER : status==NM_VIEW_INTERRUPTED ? CETTA_KERNEL_INTERRUPTED : CETTA_KERNEL_FAULT;
}
void cetta_kernel_constructor_free(CettaKernelConstructorPlan *p) {
    if (p) { nm_match_prepared_free(p->prepared);free(p->ports);free(p); }
}
size_t cetta_kernel_constructor_ports(const CettaKernelConstructorPlan *p) { return p ? p->port_count : 0; }
bool cetta_kernel_constructor_port(const CettaKernelConstructorPlan *p,size_t at,NmVariableKey *key,NmHostValue *source) {
    if (!p || !key || !source || at>=p->port_count || !valid(p->source)) return false;
    *key=nm_match_input(p->prepared)->keys[at];*source=p->ports[at];return true;
}
CettaKernelStatus cetta_kernel_constructor_compile(CettaKernelSession *s,const NmHostValue *roots,size_t count,
        CettaKernelConstructorKey key,CettaKernelConstructorPlan **out,char *error,size_t size) {
    (void)error;(void)size;if (out) *out=NULL;
    if (!s || !out || (count && !roots)) return CETTA_KERNEL_FAULT;
    if (!valid(s->view)) return CETTA_KERNEL_INVALIDATED;
    if (count>SIZE_MAX/sizeof(NmHostConstraint)) return CETTA_KERNEL_RESOURCE;
    CettaKernelConstructorPlan *p=calloc(1,sizeof *p);NmHostConstraint *cs=calloc(count ? count : 1,sizeof *cs);
    if (!p || !cs) { free(p);free(cs);return CETTA_KERNEL_RESOURCE; }
    for (size_t i=0;i<count;++i) cs[i]=(NmHostConstraint){roots[i],roots[i]};
    NmTermView view=s->view.terms;view.binding=NULL;NmImportStats stats;
    CettaKernelStatus status=imported(nm_match_prepare_constraints(&view,cs,count,s->limits.import,&p->prepared,&stats));
    free(cs);p->source=s->view;p->key=key;p->roots_count=count;
    if (status!=CETTA_KERNEL_SOLVED) goto done;
    const NmUnifyInput *in=nm_match_input(p->prepared);
    if (in->nodes_count>UINT32_MAX || in->left>=SIZE_MAX) { status=CETTA_KERNEL_RESOURCE;goto done; }
    p->nodes_count=(size_t)in->left+1;p->port_count=in->variables_count;
    p->ports=calloc(p->port_count ? p->port_count : 1,sizeof *p->ports);
    if (!p->ports) { status=CETTA_KERNEL_RESOURCE;goto done; }
    for (size_t i=1;i<p->nodes_count;++i) if (in->nodes[i].kind==NM_NODE_VARIABLE) {
        if (in->nodes[i].a>=p->port_count || !nm_match_origin(p->prepared,i,&p->ports[in->nodes[i].a])) { status=CETTA_KERNEL_FAULT;goto done; }
    }
    if (!valid(s->view)) { status=CETTA_KERNEL_INVALIDATED;goto done; }
    *out=p;p=NULL;
done:
    cetta_kernel_constructor_free(p);return status;
}
static bool key_equal(CettaKernelConstructorKey a,CettaKernelConstructorKey b) {
    return a.owner_generation==b.owner_generation && a.semantic_policy==b.semantic_policy && a.lexical_context==b.lexical_context;
}
static bool charge(uint64_t budget,uint64_t *fuel) { if (*fuel>=budget) return false;++*fuel;return true; }
/* Independent C realization of the linear plan. Shape compilation is tested
 * separately against the generic graph constructor, not against this loop. */
static CettaKernelStatus execute_c(const NmUnifyInput *in,size_t count,const uint64_t *holes,
        size_t ports,NmEmitNode *nodes,size_t node_cap,uint64_t *edges,size_t edge_cap,uint64_t *map,
        uint64_t budget,uint64_t *fuel,uint64_t *state) {
    size_t nn=(size_t)state[NC_F_OUT_NODES],ne=(size_t)state[NC_F_OUT_EDGES];
    CettaKernelStatus status=CETTA_KERNEL_SOLVED;
    for (size_t i=1;i<count;++i) {
        if (!charge(budget,fuel)) { status=CETTA_KERNEL_EXHAUSTED;break; }
        NmNode n=in->nodes[i];state[NC_F_EXPANDED]++;
        if (n.kind==NM_NODE_VARIABLE) {
            if (n.a>=ports || n.b!=in->keys[n.a].variable || n.c!=in->keys[n.a].scope ||
                !holes[n.a] || holes[n.a]>=state[NC_F_SEED_NODES]) { status=CETTA_KERNEL_FAULT;break; }
            map[i]=holes[n.a];continue;
        }
        if (n.kind==NM_NODE_TUPLE) {
            if (n.c || n.a>in->children_count || n.b>in->children_count-n.a) { status=CETTA_KERNEL_FAULT;break; }
            if (n.b>edge_cap-ne) { status=CETTA_KERNEL_RESOURCE;break; }
            size_t first=ne;ne+=(size_t)n.b;state[NC_F_OUT_EDGES]=ne;
            for (uint64_t j=0;j<n.b;++j) {
                if (!charge(budget,fuel)) { status=CETTA_KERNEL_EXHAUSTED;break; }
                uint64_t child=in->children[n.a+j];
                if (!child || child>=i || !map[child] || map[child]>=nn) { status=CETTA_KERNEL_FAULT;break; }
                edges[first+j]=map[child];
            }
            if (status!=CETTA_KERNEL_SOLVED) break;
            if (!charge(budget,fuel)) { status=CETTA_KERNEL_EXHAUSTED;break; }
            n.a=first;
        } else if (n.kind!=NM_NODE_CONSTANT || n.b || n.c) { status=CETTA_KERNEL_FAULT;break; }
        if (nn>=node_cap) { status=CETTA_KERNEL_RESOURCE;break; }
        nodes[nn]=(NmEmitNode){n,i};map[i]=nn++;state[NC_F_OUT_NODES]=nn;
    }
    if (status==CETTA_KERNEL_SOLVED && !charge(budget,fuel)) status=CETTA_KERNEL_EXHAUSTED;
    return status;
}
CettaKernelStatus cetta_kernel_constructor_instantiate(CettaKernelSession *s,const CettaKernelConstructorPlan *p,
        CettaKernelConstructorKey key,const CettaKernelConstructorArgument *args,size_t count,CettaKernelOutput **out,
        CettaKernelConstructorReceipt *receipt,char *error,size_t size) {
    CettaKernelConstructorReceipt r={.status=CETTA_KERNEL_FAULT,.fuel_used=s ? s->fuel : 0};
    CettaKernelOutput *holes_output=NULL,*o=NULL;NmHostValue *values=NULL;uint64_t *map=NULL,*holes=NULL;
    uint64_t state[NC_FACTS]={0},core_before=s ? s->fuel : 0;const NmUnifyInput *in=NULL;
    size_t seed_nodes=0,seed_edges=0,node_cap=0,edge_cap=0;bool accounted=false;
    if (out) *out=NULL;
    if (!s || !p || !out || (count && !args)) goto done;
    if (!key_equal(key,p->key) || !valid(s->view) || !valid(p->source) ||
        s->view.terms.context!=p->source.terms.context || s->view.terms.inspect!=p->source.terms.inspect ||
        s->view.terms.child!=p->source.terms.child || s->view.pin!=p->source.pin || s->view.valid!=p->source.valid) {
        r.status=CETTA_KERNEL_INVALIDATED;goto done;
    }
    if (count!=p->port_count) { r.status=CETTA_KERNEL_DEFER;goto done; }
    in=nm_match_input(p->prepared);
    for (size_t i=0;i<count;++i) if (args[i].port.variable!=in->keys[i].variable || args[i].port.scope!=in->keys[i].scope) {
        r.status=CETTA_KERNEL_DEFER;goto done;
    }
    if (!charge(s->limits.fuel_budget,&s->fuel)) { r.status=CETTA_KERNEL_EXHAUSTED;goto done; }
    r.activated=true;cetta_kernel_constructor_account(s->mode,false,1,0,0,CETTA_KERNEL_SOLVED);
    values=calloc(count ? count : 1,sizeof *values);if (!values) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    for (size_t i=0;i<count;++i) values[i]=args[i].value;
    r.status=cetta_kernel_instantiate(s,values,count,&holes_output,&r.holes,error,size);
    if (r.status!=CETTA_KERNEL_SOLVED) goto done;
    const NmOutputGraph *g=cetta_kernel_output_graph(holes_output);
    if (!g || !g->nodes_count) { r.status=CETTA_KERNEL_INVALIDATED;goto done; }
    seed_nodes=g->nodes_count-1;seed_edges=g->children_count;
    if (seed_nodes>SIZE_MAX-p->nodes_count || seed_edges>SIZE_MAX-in->children_count) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    node_cap=seed_nodes+p->nodes_count;if (node_cap>s->limits.output_nodes) node_cap=s->limits.output_nodes;
    if (node_cap>UINT32_MAX) node_cap=UINT32_MAX;
    edge_cap=seed_edges+in->children_count;if (edge_cap>s->limits.output_edges) edge_cap=s->limits.output_edges;
    if (!node_cap || node_cap<seed_nodes || edge_cap<seed_edges) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    o=calloc(1,sizeof *o);map=calloc(p->nodes_count,sizeof *map);holes=calloc(count ? count : 1,sizeof *holes);
    if (!o || !map || !holes) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    o->pin=s->view.pin;o->valid=s->view.valid;
    o->nodes=calloc(node_cap,sizeof *o->nodes);o->children=calloc(edge_cap ? edge_cap : 1,sizeof *o->children);
    o->origins=calloc(node_cap,sizeof *o->origins);o->origin_present=calloc(node_cap,sizeof *o->origin_present);
    if (!o->nodes || !o->children || !o->origins || !o->origin_present) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    memcpy(o->nodes,g->nodes,seed_nodes*sizeof *o->nodes);
    if (seed_edges) memcpy(o->children,g->children,seed_edges*sizeof *o->children);
    for (size_t i=1;i<seed_nodes;++i) o->origin_present[i]=cetta_kernel_output_origin(holes_output,i,&o->origins[i]);
    for (size_t i=0;i<count;++i) holes[i]=g->roots[i];
    r.prepared_nodes=seed_nodes;r.prepared_edges=seed_edges;
    state[NC_F_OUT_NODES]=seed_nodes;state[NC_F_SEED_NODES]=seed_nodes;state[NC_F_OUT_EDGES]=seed_edges;
    core_before=s->fuel;r.executed=true;
    if (s->mode==CETTA_KERNEL_C || s->mode==CETTA_KERNEL_HYBRID) {
        r.status=execute_c(in,p->nodes_count,holes,count,o->nodes,node_cap,o->children,edge_cap,map,s->limits.fuel_budget,&s->fuel,state);
    } else {
        NmComponentSpan spans[NM_COMPONENT_REGIONS]={0};
        bool admitted=nm_component_span(spans,in->nodes,p->nodes_count,sizeof *in->nodes,false) &&
            nm_component_span(spans+1,in->children,in->children_count,sizeof *in->children,false) &&
            nm_component_span(spans+2,holes,count,sizeof *holes,false) &&
            nm_component_span(spans+3,o->nodes,node_cap,sizeof *o->nodes,true) &&
            nm_component_span(spans+4,o->children,edge_cap,sizeof *o->children,true) &&
            nm_component_span(spans+5,map,p->nodes_count,sizeof *map,true) &&
            nm_component_span(spans+6,state,NC_FACTS,sizeof *state,true) &&
            nm_component_span(spans+7,in->keys,count,sizeof *in->keys,false);
        NmComponentResult core={0};NmCallStatus status=admitted ? nm_component_call(&s->constructor->component,
            NM_NATIVE,spans,in->left,node_cap,s->limits.fuel_budget,s->fuel,&core,error,size) : NM_CALL_BAD_ARGUMENT;
        if (status!=NM_CALL_OK) r.status=status==NM_CALL_RESOURCE ? CETTA_KERNEL_RESOURCE : CETTA_KERNEL_FAULT;
        else {
            s->fuel=core.fuel_used;
            r.status=core.outcome==TISA_OUTCOME_FIRED ? CETTA_KERNEL_SOLVED : core.outcome==TISA_OUTCOME_EXHAUSTED ? CETTA_KERNEL_EXHAUSTED :
                core.payload==2 ? CETTA_KERNEL_RESOURCE : CETTA_KERNEL_FAULT;
            if (r.status==CETTA_KERNEL_SOLVED && core.payload!=map[in->left]) r.status=CETTA_KERNEL_FAULT;
        }
    }
    r.constructor=(NmEmitResult){r.status==CETTA_KERNEL_SOLVED ? NM_EMIT_FIRED : r.status==CETTA_KERNEL_EXHAUSTED ? NM_EMIT_EXHAUSTED : NM_EMIT_UNSUPPORTED,
        map[in->left],0,s->fuel,(size_t)state[NC_F_OUT_NODES],(size_t)state[NC_F_OUT_EDGES],0,(size_t)state[NC_F_EXPANDED]};
    cetta_kernel_constructor_account(s->mode,true,s->fuel-core_before,r.constructor.nodes_written,r.constructor.children_written,r.status);accounted=true;
    if (r.status!=CETTA_KERNEL_SOLVED) goto done;
    if (!valid(s->view) || !valid(p->source)) { r.status=CETTA_KERNEL_INVALIDATED;goto done; }
    uint64_t root=map[in->left];
    if (!root || root>=state[NC_F_OUT_NODES] || o->nodes[root].node.kind!=NM_NODE_TUPLE ||
        o->nodes[root].node.b!=p->roots_count) { r.status=CETTA_KERNEL_FAULT;goto done; }
    for (size_t i=seed_nodes;i<state[NC_F_OUT_NODES];++i)
        o->origin_present[i]=nm_match_origin(p->prepared,o->nodes[i].source,&o->origins[i]);
    NmNode top=o->nodes[root].node;
    if (top.a>state[NC_F_OUT_EDGES] || top.b>state[NC_F_OUT_EDGES]-top.a) { r.status=CETTA_KERNEL_FAULT;goto done; }
    o->graph=(NmOutputGraph){o->nodes,(size_t)state[NC_F_OUT_NODES],o->children,(size_t)state[NC_F_OUT_EDGES],o->children+top.a,p->roots_count};
    *out=o;o=NULL;
done:
    if (s) r.fuel_used=s->fuel;
    if (!accounted && r.status!=CETTA_KERNEL_SOLVED && s) cetta_kernel_constructor_account(s->mode,false,0,0,0,r.status);
    if (receipt) *receipt=r;
    free(values);free(map);free(holes);cetta_kernel_output_free(holes_output);cetta_kernel_output_free(o);return r.status;
}
