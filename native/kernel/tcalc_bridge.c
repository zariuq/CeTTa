#include "tcalc_bridge.h"
#include "constructor.h"
#include "pair.h"
#include <stdlib.h>

static CettaKernelLimits limits(void) {
    return (CettaKernelLimits){.import={UINT32_MAX,SIZE_MAX,UINT32_MAX-1u,SIZE_MAX},
        .output_nodes=UINT32_MAX,.output_edges=SIZE_MAX,.output_stack=UINT32_MAX,.fuel_budget=UINT64_MAX};
}
typedef struct { TcTerms *terms;TcKernelSolver *solver;const CettaTcKernelOps *ops;bool close; } Source;
static NmViewStatus inspect(void *p,NmHostValue v,NmViewNode *out) {
    Source *s=p;return s->ops->inspect(s->terms,s->solver,v,out);
}
static NmViewStatus child(void *p,NmHostValue v,uint64_t i,NmHostValue *out) {
    Source *s=p;return s->ops->child(s->terms,s->solver,v,i,out);
}
static NmViewStatus binding(void *p,NmHostValue v,bool *bound,NmHostValue *out) {
    Source *s=p;return s->ops->binding(s->terms,s->solver,s->close,v,bound,out);
}
static CettaKernelStatus view_failure(NmViewStatus status) {
    return status==NM_VIEW_DEFER ? CETTA_KERNEL_DEFER : status==NM_VIEW_RESOURCE ? CETTA_KERNEL_RESOURCE :
        status==NM_VIEW_INTERRUPTED ? CETTA_KERNEL_INTERRUPTED : CETTA_KERNEL_FAULT;
}
static bool apply(void *p,NmVariableKey key,NmHostValue value) {
    Source *s=p;return s->ops->apply(s->terms,s->solver,key,value);
}
static CettaKernelStatus solve_graph(Source *source,CettaKernelMode mode,const NmHostConstraint *constraints,size_t count) {
    CettaKernelSession *s=NULL;char error[256];NmTermView view={source,inspect,child,binding};
    CettaKernelStatus status=cetta_kernel_session_open(mode,(NmQueryView){.terms=view},limits(),0,&s,error,sizeof error);
    if (status!=CETTA_KERNEL_SOLVED) return status;
    CettaKernelReceipt receipt;status=cetta_kernel_solve(s,constraints,count,false,&receipt,error,sizeof error);
    if (status==CETTA_KERNEL_SOLVED && !cetta_kernel_visit_delta(s,apply,source)) status=CETTA_KERNEL_RESOURCE;
    cetta_kernel_session_free(s);return status;
}
CettaKernelStatus cetta_kernel_tcalc_solve(TcTerms *terms,TcKernelSolver *solver,const CettaTcKernelOps *ops,CettaKernelMode mode,
    TcTerm a,uint32_t abase,TcTerm b,uint32_t bbase) {
    if (!terms || !solver || !ops || !ops->inspect || !ops->child || !ops->binding || !ops->apply) return CETTA_KERNEL_FAULT;
    Source source={terms,solver,ops,false};NmHostConstraint initial={{a,abase},{b,bbase}};
    if (mode==CETTA_KERNEL_C || mode==CETTA_KERNEL_HYBRID) return solve_graph(&source,mode,&initial,1);
    NmHostConstraint inline_work[32],*work=inline_work;size_t length=1,capacity=32,expanded=0;
    inline_work[0]=initial;CettaKernelPairScope *scope=cetta_kernel_pair_begin(mode);
    if (!scope) return CETTA_KERNEL_RESOURCE;
    CettaKernelStatus verdict=CETTA_KERNEL_SOLVED;
    NmHostConstraint inline_pending[16],*pending=inline_pending;size_t pending_count=0,pending_capacity=16;
    while (length) {
        NmHostConstraint pair=work[--length];unsigned dereferences=0;
again: ;
        NmViewNode left,right;NmViewStatus ls=ops->inspect(terms,solver,pair.left,&left),rs=ops->inspect(terms,solver,pair.right,&right);
        if (ls!=NM_VIEW_READY || rs!=NM_VIEW_READY) { verdict=view_failure(ls!=NM_VIEW_READY ? ls : rs);break; }
        if ((unsigned)left.kind>NM_NODE_TUPLE || (unsigned)right.kind>NM_NODE_TUPLE) { verdict=CETTA_KERNEL_FAULT;break; }
        bool lb=false,rb=false;NmHostValue lv={0},rv={0};
        if (left.kind==NM_NODE_VARIABLE) ls=ops->binding(terms,solver,false,pair.left,&lb,&lv);
        if (right.kind==NM_NODE_VARIABLE) rs=ops->binding(terms,solver,false,pair.right,&rb,&rv);
        if (ls!=NM_VIEW_READY || rs!=NM_VIEW_READY) { verdict=view_failure(ls!=NM_VIEW_READY ? ls : rs);break; }
        if (pair.left.context==pair.right.context) {
            bool equal=false;
            if (!cetta_kernel_pair_word(scope,pair.left.syntax,pair.right.syntax,&equal)) { verdict=CETTA_KERNEL_FAULT;break; }
            if (equal) continue;
        }
        CettaKernelPairFacts facts={0};facts.word[NP_F_PHASE]=1;
        facts.word[NP_F_LEFT_KIND]=left.kind==NM_NODE_VARIABLE ? NP_K_VARIABLE : left.kind==NM_NODE_TUPLE ? NP_K_EXPRESSION : NP_K_SYMBOL;
        facts.word[NP_F_RIGHT_KIND]=right.kind==NM_NODE_VARIABLE ? NP_K_VARIABLE : right.kind==NM_NODE_TUPLE ? NP_K_EXPRESSION : NP_K_SYMBOL;
        facts.word[NP_F_LEFT_VARIABLE]=left.variable.variable;facts.word[NP_F_LEFT_SCOPE]=left.variable.scope;
        facts.word[NP_F_RIGHT_VARIABLE]=right.variable.variable;facts.word[NP_F_RIGHT_SCOPE]=right.variable.scope;
        facts.word[NP_F_LEFT_BOUND]=lb;facts.word[NP_F_RIGHT_BOUND]=rb;
        facts.word[NP_F_LEFT_SYMBOL]=left.constant;facts.word[NP_F_RIGHT_SYMBOL]=right.constant;
        facts.word[NP_F_LEFT_LENGTH]=left.children;facts.word[NP_F_RIGHT_LENGTH]=right.children;
        CettaKernelPairPlan plan;
        if (!cetta_kernel_pair_next(scope,&facts,&plan)) { verdict=CETTA_KERNEL_FAULT;break; }
        if (plan.action==NP_A_SKIP) continue;
        if (plan.action==NP_A_REJECT) { verdict=CETTA_KERNEL_REFUTED;break; }
        if (plan.action==NP_A_DEREFERENCE_LEFT || plan.action==NP_A_DEREFERENCE_RIGHT) {
            if (plan.action==NP_A_DEREFERENCE_LEFT) pair.left=lv;else pair.right=rv;
            if (++dereferences<16) goto again;
            goto defer_constraint;
        }
        if (plan.action==NP_A_BIND_LEFT || plan.action==NP_A_BIND_RIGHT) {
            goto defer_constraint;
        }
        if (plan.action!=NP_A_EXPRESSION) { verdict=CETTA_KERNEL_FAULT;break; }
        /* Preserve the complete DAG solver's memoization on larger
         * structural traversals without making it a term-size cutoff. */
        if (expanded++>=64) {
            goto defer_constraint;
        }
        if (left.children>SIZE_MAX-length) { verdict=CETTA_KERNEL_RESOURCE;break; }
        size_t needed=length+(size_t)left.children;
        if (needed>capacity) {
            size_t next=capacity;
            while (next<needed && next<=SIZE_MAX/2) next*=2;
            if (next<needed || next>SIZE_MAX/sizeof *work) { verdict=CETTA_KERNEL_RESOURCE;break; }
            NmHostConstraint *grown=work==inline_work ? malloc(next*sizeof *grown) : realloc(work,next*sizeof *grown);
            if (!grown) { verdict=CETTA_KERNEL_RESOURCE;break; }
            if (work==inline_work) for (size_t i=0;i<length;++i) grown[i]=work[i];
            work=grown;capacity=next;
        }
        for (uint64_t i=left.children;i>0;--i) {
            NmHostConstraint child_pair;
            NmViewStatus lc=ops->child(terms,solver,pair.left,i-1,&child_pair.left),rc=ops->child(terms,solver,pair.right,i-1,&child_pair.right);
            if (lc!=NM_VIEW_READY || rc!=NM_VIEW_READY) { verdict=view_failure(lc!=NM_VIEW_READY ? lc : rc);break; }
            work[length++]=child_pair;
        }
        if (verdict!=CETTA_KERNEL_SOLVED) break;
        continue;
defer_constraint:
        /* The caller owns the failed-attempt rollback boundary. Collect the
         * unresolved equations into one conjunction, sharing graph import,
         * occurs checking and the private binding snapshot for this attempt. */
        if (pending_count==pending_capacity) {
            if (pending_capacity>SIZE_MAX/2/sizeof *pending) { verdict=CETTA_KERNEL_RESOURCE;break; }
            size_t next=pending_capacity*2;
            NmHostConstraint *grown=pending==inline_pending ? malloc(next*sizeof *grown) : realloc(pending,next*sizeof *grown);
            if (!grown) { verdict=CETTA_KERNEL_RESOURCE;break; }
            if (pending==inline_pending) for (size_t i=0;i<pending_count;++i) grown[i]=pending[i];
            pending=grown;pending_capacity=next;
        }
        pending[pending_count++]=pair;
    }
    if (verdict==CETTA_KERNEL_SOLVED && pending_count) verdict=solve_graph(&source,mode,pending,pending_count);
    if (work!=inline_work) free(work);
    if (pending!=inline_pending) free(pending);
    cetta_kernel_pair_end(scope);return verdict;
}

CettaKernelStatus cetta_kernel_tcalc_construct(TcTerms *terms,TcKernelSolver *solver,const CettaTcKernelOps *ops,CettaKernelMode mode,
    const TcTerm *roots,size_t count,uint32_t base,bool close,TcTerm *result) {
    if (!terms || !solver || !ops || !ops->make || (count && (!roots || !result)) || count>SIZE_MAX/sizeof(NmHostValue)) return CETTA_KERNEL_FAULT;
    CettaKernelSession *s=NULL;char error[256];
    Source bridge={terms,solver,ops,close};NmTermView source_view={&bridge,inspect,child,binding};
    CettaKernelStatus status=cetta_kernel_session_open(mode,(NmQueryView){.terms=source_view},limits(),0,&s,error,sizeof error);
    if (status!=CETTA_KERNEL_SOLVED) return status;
    NmHostValue *source=calloc(count ? count : 1,sizeof *source);
    if (!source) { cetta_kernel_session_free(s);return CETTA_KERNEL_RESOURCE; }
    for (size_t i=0;i<count;++i) source[i]=(NmHostValue){roots[i],base};
    CettaKernelOutput *output=NULL;
    if (count<=1) {
        NmOutputReceipt receipt;
        status=cetta_kernel_instantiate(s,source,count,&output,&receipt,error,sizeof error);
    }
    CettaKernelConstructorPlan *plan=NULL;
    CettaKernelConstructorArgument *arguments=NULL;
    CettaKernelConstructorKey key={(uint64_t)(uintptr_t)terms,close ? 2u : 1u,base};
    if (count>1) status=cetta_kernel_constructor_compile(s,source,count,key,&plan,error,sizeof error);
    size_t ports=cetta_kernel_constructor_ports(plan);
    if (count>1 && status==CETTA_KERNEL_SOLVED) {
        arguments=calloc(ports ? ports : 1,sizeof *arguments);
        if (!arguments) status=CETTA_KERNEL_RESOURCE;
    }
    for (size_t i=0;status==CETTA_KERNEL_SOLVED && i<ports;++i)
        if (!cetta_kernel_constructor_port(plan,i,&arguments[i].port,&arguments[i].value)) status=CETTA_KERNEL_INVALIDATED;
    CettaKernelConstructorReceipt receipt;
    if (count>1 && status==CETTA_KERNEL_SOLVED)
        status=cetta_kernel_constructor_instantiate(s,plan,key,arguments,ports,&output,&receipt,error,sizeof error);
    free(arguments);cetta_kernel_constructor_free(plan);
    const NmOutputGraph *graph=status==CETTA_KERNEL_SOLVED ? cetta_kernel_output_graph(output) : NULL;
    TcTerm *map=NULL,*items=NULL;
    if (graph) {
        map=calloc(graph->nodes_count,sizeof *map);items=calloc(graph->children_count ? graph->children_count : 1,sizeof *items);
        if (!map || !items) status=CETTA_KERNEL_RESOURCE;
    } else if (status==CETTA_KERNEL_SOLVED) status=CETTA_KERNEL_INVALIDATED;
    if (status==CETTA_KERNEL_SOLVED) for (size_t i=1;i+1<graph->nodes_count;++i) {
        NmNode n=graph->nodes[i].node;
        if (n.kind==NM_NODE_CONSTANT) map[i]=(TcTerm)n.a;
        else if (n.kind==NM_NODE_VARIABLE) map[i]=(TcTerm)n.b;
        else if (n.kind==NM_NODE_TUPLE && n.b<=UINT32_MAX) {
            for (size_t j=0;j<n.b;++j) items[j]=map[graph->children[n.a+j]];
            map[i]=ops->make(terms,items,(uint32_t)n.b);
        }
        if (!map[i]) { status=CETTA_KERNEL_RESOURCE;break; }
    }
    if (status==CETTA_KERNEL_SOLVED) for (size_t i=0;i<count;++i) result[i]=map[graph->roots[i]];
    free(map);free(items);free(source);cetta_kernel_output_free(output);cetta_kernel_session_free(s);return status;
}
