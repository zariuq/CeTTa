/* tcalc's x86 kernel: unify and term making on native programs over the
 * core's own store (native/kernel/tcalc_native.c). Included in the
 * ownership-checked core. */
static bool tc_kernel_ready(const TcTerms *terms,const Solver *solver) {
    return !solver->failed && !terms->failed;
}
/* unify on the native unifier (native/kernel/tcunify.metta), which reads the
 * store where it lies. It hands back what needs new terms or more room: a term
 * to close, a table to grow, a walk epoch to renew. Every table has an entry
 * and every term a walk record before a call. */
/* The native programs read every term, index and count as a 32-bit word. */
static bool tc_native_fits(const TcTerms *terms,const Solver *s) {
    return (terms->nodes|terms->items|terms->seen_cap|s->local_cap|s->unknown_cap|s->trail_cap|s->work_cap|
            s->native_out_cap|s->native_stack_cap)<((size_t)1<<31);
}
static bool tc_unify_ready(TcTerms *terms,Solver *s) {
    if (!tc_native_fits(terms,s)) return false;
    if (s->local_cap && s->unknown_cap && s->trail_cap>s->trail_len && s->work_cap>=s->work_len+4 &&
        terms->seen_cap>=terms->nodes) return true;
    return reserve(&s->local,&s->local_cap,1) && reserve(&s->unknown,&s->unknown_cap,1) &&
        reserve(&s->trail,&s->trail_cap,s->trail_len+1) && reserve(&s->work,&s->work_cap,s->work_len+4) &&
        walk_reserve(terms,terms->nodes);
}
static bool tc_kernel_unify(TcTerms *terms,Solver *solver,TcTerm a,uint32_t abase,TcTerm b,uint32_t bbase) {
    if (!tc_kernel_ready(terms,solver)) return false;
    if (!cetta_kernel_tcalc_unify) { solver->failed=true;return false; }
    size_t floor=solver->work_len;
    /* The image reads only these words before writing them; its first call
     * puts the equation on the work stack (tcunify.metta). */
    solver->native[TU_F_PHASE]=2;solver->native[TU_F_FLOOR]=floor;
    solver->native[TU_F_A]=a;solver->native[TU_F_ABASE]=abase;solver->native[TU_F_B]=b;solver->native[TU_F_BBASE]=bbase;
    for (;;) {
        if (!tc_unify_ready(terms,solver)) { solver->failed=true;solver->work_len=floor;return false; }
        solver->native[TU_F_WLEN]=solver->work_len;solver->native[TU_F_WCAP]=solver->work_cap;
        solver->native[TU_F_TLEN]=solver->trail_len;solver->native[TU_F_TCAP]=solver->trail_cap;solver->native[TU_F_EPOCH]=terms->walk;
        uint64_t answer;
        {
            TcUnifyStore store={terms->node,terms->nodes,terms->items ? terms->item : no_items,terms->items ? terms->items : 1,
                solver->local,solver->local_cap,solver->unknown,solver->unknown_cap,solver->trail,solver->trail_cap,
                solver->work,solver->work_cap,terms->seen,terms->seen_cap,solver->native,TU_FACTS};
            answer=cetta_kernel_tcalc_unify(&store);
        }
        solver->work_len=(size_t)solver->native[TU_F_WLEN];solver->trail_len=(size_t)solver->native[TU_F_TLEN];
        terms->walk=(uint32_t)solver->native[TU_F_EPOCH];
        if (answer==TU_R_SOLVED) return true;
        if (answer==TU_R_REFUTED) return false;
        if (answer==TU_R_CLOSE) {
            TcTerm closed=close_term(terms,solver,(TcTerm)solver->native[TU_F_PT],(uint32_t)solver->native[TU_F_PTBASE]);
            if (!closed) { solver->work_len=floor;return false; }
            solver->native[TU_F_CLOSED]=closed;continue;
        }
        if (answer==TU_R_ROOM && solver->native[TU_F_NEED]==TU_N_WORK) {
            if (reserve(&solver->work,&solver->work_cap,2*solver->work_cap)) continue;
        } else if (answer==TU_R_ROOM && solver->native[TU_F_NEED]==TU_N_TRAIL) {
            if (reserve(&solver->trail,&solver->trail_cap,2*solver->trail_cap)) continue;
        } else if (answer==TU_R_ROOM && solver->native[TU_F_NEED]==TU_N_EPOCH) {
            terms->walk=UINT32_MAX;walk_begin(terms);continue;   /* forgets every record */
        } else if (answer==TU_R_YIELD) continue;
        break;
    }
    solver->failed=true;solver->work_len=floor;return false;
}
/* The terms roots (at base) with the assignments applied, closed if close,
 * by the native term builder (native/kernel/tcbuild.metta): it lists what to
 * make, in the order close_from and resolve_from make it, and the list is
 * made here. Its stack holds the roots, then the entries' terms. */
static bool tc_build_ready(TcTerms *terms,Solver *s,size_t roots) {
    if (!tc_native_fits(terms,s)) return false;
    if (s->local_cap && s->unknown_cap && s->native_stack_cap>=roots+4 && s->native_out_cap>=2 &&
        terms->seen_cap>=terms->nodes) return true;
    return reserve(&s->local,&s->local_cap,1) && reserve(&s->unknown,&s->unknown_cap,1) &&
        reserve(&s->native_stack,&s->native_stack_cap,roots+4) && reserve(&s->native_out,&s->native_out_cap,2) &&
        walk_reserve(terms,terms->nodes);
}
/* The term a reference stands for, among the roots and the k entries made. */
static TcTerm tc_build_term(const Solver *s,size_t roots,size_t k,uint32_t ref) {
    if (!(ref & TB_ENTRY_BIT)) return ref;
    return (ref & ~TB_ENTRY_BIT)<k ? s->native_stack[roots+(ref & ~TB_ENTRY_BIT)] : 0;
}
static bool tc_kernel_output(TcTerms *terms,Solver *solver,const TcTerm *roots,size_t count,uint32_t base,
    bool close,TcTerm *result) {
    if (!tc_kernel_ready(terms,solver)) return false;
    if (!cetta_kernel_tcalc_build || count>UINT32_MAX/2) { solver->failed=true;return false; }
    uint64_t answer=TB_R_FAULT;
    for (;;) {
        if (!tc_build_ready(terms,solver,count)) { solver->failed=true;return false; }
        for (size_t i=0;i<count;++i) solver->native_stack[i]=roots[i];
        solver->native_build[TB_F_EPOCH]=terms->walk;solver->native_build[TB_F_CLOSE]=close;
        solver->native_build[TB_F_BASE]=base;solver->native_build[TB_F_ROOTS]=count;
        solver->native_build[TB_F_SCAP]=solver->native_stack_cap;solver->native_build[TB_F_OUTCAP]=solver->native_out_cap;
        {
            TcBuildStore store={terms->node,terms->nodes,terms->items ? terms->item : no_items,terms->items ? terms->items : 1,
                solver->local,solver->local_cap,solver->unknown,solver->unknown_cap,terms->seen,terms->seen_cap,
                terms->seen_value,terms->seen_cap,solver->native_out,solver->native_out_cap,
                solver->native_stack,solver->native_stack_cap,solver->native_build,TB_FACTS};
            answer=cetta_kernel_tcalc_build(&store);
        }
        terms->walk=(uint32_t)solver->native_build[TB_F_EPOCH];
        if (answer==TB_R_DONE) break;
        if (answer==TB_R_ROOM && solver->native_build[TB_F_NEED]==TB_N_OUT) {
            if (reserve(&solver->native_out,&solver->native_out_cap,2*solver->native_out_cap)) continue;
        } else if (answer==TB_R_ROOM && solver->native_build[TB_F_NEED]==TB_N_STACK) {
            if (reserve(&solver->native_stack,&solver->native_stack_cap,2*solver->native_stack_cap)) continue;
        } else if (answer==TB_R_ROOM && solver->native_build[TB_F_NEED]==TB_N_EPOCH) {
            terms->walk=UINT32_MAX;walk_begin(terms);continue;   /* forgets every record */
        }
        solver->failed=true;return false;
    }
    size_t entries=(size_t)solver->native_build[TB_F_ENTRIES],length=(size_t)solver->native_build[TB_F_OUTLEN];
    if (length>solver->native_out_cap || entries>length ||
        !reserve(&solver->native_stack,&solver->native_stack_cap,count+entries)) { solver->failed=true;return false; }
    size_t at=0;
    for (size_t k=0;k<entries;++k) {
        if (at+2>length) { solver->failed=true;return false; }
        uint32_t kind=solver->native_out[at],word=solver->native_out[at+1];
        TcTerm made=0;
        if (kind==TB_K_FRESH && word<solver->local_cap) {
            made=fresh_unknown(terms,solver);
            if (made && !assign(solver,true,word,made)) made=0;
            at+=2;
        } else if (kind==TB_K_MAKE && word<=length-at-2) {
            size_t from=solver->build_len;
            for (uint32_t j=0;j<word;++j) {
                TcTerm item=tc_build_term(solver,count,k,solver->native_out[at+2+j]);
                if (!item || !build_push(solver,item)) { solver->build_len=from;solver->failed=true;return false; }
            }
            made=build_make(terms,solver,from);
            at+=2+(size_t)word;
        }
        if (!made) { solver->failed=true;return false; }
        solver->native_stack[count+k]=made;
    }
    for (size_t i=0;i<count;++i)
        if (!(result[i]=tc_build_term(solver,count,entries,solver->native_stack[i]))) { solver->failed=true;return false; }
    return true;
}
