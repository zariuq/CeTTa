/* Scoped native term operations. Included in the ownership-checked core. */
static NmViewStatus tc_kernel_inspect(TcTerms *terms,Solver *solver,NmHostValue value,NmViewNode *out) {
    (void)solver;
    if (!value.syntax || value.syntax>=terms->nodes || value.context>UINT32_MAX) return NM_VIEW_FAULT;
    TcNode n=term_node(terms,(TcTerm)value.syntax);
    if (n.kind==TC_LOCAL || n.kind==TC_UNKNOWN) {
        *out=(NmViewNode){.kind=NM_NODE_VARIABLE,
            .variable={value.syntax,n.kind==TC_LOCAL ? value.context : UINT64_MAX}};
    } else if (n.kind==TC_EXPRESSION) *out=(NmViewNode){.kind=NM_NODE_TUPLE,.children=n.length};
    else if (n.kind==TC_SYMBOL || n.kind==TC_INTEGER || n.kind==TC_STRING)
        *out=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};
    else return NM_VIEW_DEFER;
    return NM_VIEW_READY;
}
static NmViewStatus tc_kernel_child(TcTerms *terms,Solver *solver,NmHostValue value,uint64_t index,NmHostValue *out) {
    (void)solver;
    if (!value.syntax || value.syntax>=terms->nodes || value.context>UINT32_MAX) return NM_VIEW_FAULT;
    TcNode n=term_node(terms,(TcTerm)value.syntax);
    if (n.kind!=TC_EXPRESSION || index>=n.length) return NM_VIEW_FAULT;
    *out=(NmHostValue){term_item(terms,n.start+(size_t)index),value.context};return NM_VIEW_READY;
}
static NmViewStatus tc_kernel_binding(TcTerms *terms,Solver *solver,bool close,NmHostValue value,bool *bound,NmHostValue *out) {
    if (!value.syntax || value.syntax>=terms->nodes || value.context>UINT32_MAX) return NM_VIEW_FAULT;
    TcNode n=term_node(terms,(TcTerm)value.syntax);TcTerm target=0;
    if (n.kind==TC_LOCAL) {
        uint64_t slot=value.context+n.value;
        if (slot>=solver->local_cap || slot>UINT32_MAX) return NM_VIEW_FAULT;
        target=local_at(solver,(size_t)slot);
        if (!target && close) {
            target=fresh_unknown(terms,solver);
            if (!target || !assign(solver,true,(uint32_t)slot,target)) return NM_VIEW_RESOURCE;
        }
    } else if (n.kind==TC_UNKNOWN) {
        if ((uint64_t)n.value>=solver->unknown_cap) return NM_VIEW_FAULT;
        target=unknown_at(solver,(size_t)n.value);
    } else return NM_VIEW_FAULT;
    *bound=target!=0;*out=(NmHostValue){target,value.context};return NM_VIEW_READY;
}
static bool tc_kernel_binding_delta(TcTerms *terms,Solver *solver,NmVariableKey key,NmHostValue value) {
    TcTerm var=(TcTerm)key.variable;
    if (!var || var>=terms->nodes || !value.syntax || value.syntax>=terms->nodes || value.context>UINT32_MAX) return false;
    TcNode n=term_node(terms,var);uint32_t base=n.kind==TC_UNKNOWN ? 0 : (uint32_t)key.scope;
    if ((n.kind!=TC_LOCAL && n.kind!=TC_UNKNOWN) || (n.kind==TC_LOCAL && key.scope>UINT32_MAX)) return false;
    /* Closing a cross-frame value may already have introduced this alias. */
    if (walk(terms,solver,var,base)!=var) return true;
    return bind(terms,solver,var,base,(TcTerm)value.syntax,(uint32_t)value.context);
}
static TcTerm tc_kernel_make(TcTerms *terms,const TcTerm *items,uint32_t count) {
    return tc_expression(terms,items,count);
}
static bool tc_kernel_ready(const TcTerms *terms,const Solver *solver) {
    return !solver->failed && !terms->failed;
}
static bool tc_kernel_unify(TcTerms *terms,Solver *solver,TcTerm a,uint32_t abase,TcTerm b,uint32_t bbase) {
    if (!tc_kernel_ready(terms,solver)) return false;
    CettaTcKernelOps ops={tc_kernel_inspect,tc_kernel_child,tc_kernel_binding,tc_kernel_binding_delta,NULL};
    if (!cetta_kernel_tcalc_solve) { solver->failed=true;return false; }
    CettaKernelStatus status=cetta_kernel_tcalc_solve(terms,solver,&ops,cetta_kernel_requested(),a,abase,b,bbase);
    if (status!=CETTA_KERNEL_SOLVED && status!=CETTA_KERNEL_REFUTED) solver->failed=true;
    return status==CETTA_KERNEL_SOLVED;
}
static bool tc_kernel_output(TcTerms *terms,Solver *solver,const TcTerm *roots,size_t count,uint32_t base,
    bool close,TcTerm *result) {
    if (!tc_kernel_ready(terms,solver)) return false;
    CettaTcKernelOps ops={tc_kernel_inspect,tc_kernel_child,tc_kernel_binding,NULL,tc_kernel_make};
    if (!cetta_kernel_tcalc_construct) { solver->failed=true;return false; }
    CettaKernelStatus status=cetta_kernel_tcalc_construct(terms,solver,&ops,cetta_kernel_requested(),roots,count,base,close,result);
    if (status!=CETTA_KERNEL_SOLVED) solver->failed=true;
    return status==CETTA_KERNEL_SOLVED;
}
