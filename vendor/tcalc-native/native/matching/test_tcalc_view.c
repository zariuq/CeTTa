#include "native_tcalc_view.h"
#include "native_admission.h"
#include "native_census.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Compile the unmodified host export in this test translation unit so its
 * actual solver can serve as an independent comparison oracle. The adapter
 * itself uses only the public tcalc accessors. */
#include "tcalc_core.export.c"

static size_t checked, ground_values,plan_decisions,plan_ground_values,region_decisions;

static bool same_before(const TcTerms *terms, TcTerm a, uint32_t abase, TcTerm b, uint32_t bbase) {
    TcNode x=term_node(terms,a),y=term_node(terms,b);
    if (x.kind!=y.kind) return false;
    if (x.kind==TC_LOCAL) return x.value==y.value && abase==bbase;
    if (x.kind!=TC_EXPRESSION) return a==b;
    if (x.length!=y.length) return false;
    for (uint32_t i=0;i<x.length;++i)
        if (!same_before(terms,term_item(terms,x.start+i),abase,term_item(terms,y.start+i),bbase)) return false;
    return true;
}

static TcTerm ground_value(TcTerms *terms, const NmUnifyInput *in, const NmUnifyWork *work,
                           uint64_t id, size_t depth) {
    assert(id && id<in->nodes_count && depth<in->nodes_count+in->variables_count);
    NmNode n=in->nodes[id];
    if (n.kind==NM_NODE_CONSTANT) return (TcTerm)n.a;
    if (n.kind==NM_NODE_VARIABLE)
        return work->bindings[n.a] ? ground_value(terms,in,work,work->bindings[n.a],depth+1) : 0;
    assert(n.kind==NM_NODE_TUPLE && n.b<=UINT32_MAX);
    TcTerm *children=malloc((n.b ? (size_t)n.b : 1)*sizeof *children);assert(children);
    bool ground=true;
    for (size_t i=0;i<n.b;++i) {
        children[i]=ground_value(terms,in,work,in->children[n.a+i],depth+1);
        if (!children[i]) { ground=false;break; }
    }
    TcTerm result=ground ? tc_expression(terms,children,(uint32_t)n.b) : 0;
    free(children);return result;
}
static void compare(const NmUnifyProgram *program, TcTerms *terms, TcTerm a, TcTerm b,
                     bool separate) {
    Solver s={0};
    assert(reserve(&s.local,&s.local_cap,64) && reserve_unknowns(&s,64));
    bool expected=unify(terms,&s,a,0,b,separate ? 16 : 0);assert(!s.failed);
    bool entailed=same_before(terms,a,0,b,separate ? 16 : 0);
    TcTerm ra=expected ? resolve(terms,&s,a,0) : 0;
    TcTerm rb=expected ? resolve(terms,&s,b,separate ? 16 : 0) : 0;
    NmTcalcTermView source={terms,(TcTerm)(terms->nodes-1),900};
    NmTermView view=nm_tcalc_term_view(&source);NmPreparedMatch *prepared=NULL;
    assert(nm_match_prepare(&view,(NmHostValue){a,100},(NmHostValue){b,separate ? 101 : 100},
        (NmImportLimits){1000,1000,1000,10000},&prepared,NULL)==NM_VIEW_READY);
    const NmUnifyInput *in=nm_match_input(prepared);
    for (unsigned mode=0;mode<3;++mode) {
        size_t marks=in->variables_count+in->nodes_count+128;
        NmUnifyWork work={.bindings_words=in->variables_count,.pairs_count=128,.scan_words=128,
            .state_words=nm_unify_state_words(program),.marks_words=marks};
        work.bindings=calloc(in->variables_count ? in->variables_count : 1,sizeof *work.bindings);
        work.pairs=calloc(work.pairs_count,sizeof *work.pairs);work.scan=calloc(work.scan_words,sizeof *work.scan);
        work.state=calloc(work.state_words,sizeof *work.state);work.marks=calloc(marks,sizeof *work.marks);
        assert(work.bindings && work.pairs && work.scan && work.state && work.marks);
        NmUnifyResult result;char error[256];
        assert(nm_unify_run(program,(NmExecution)mode,in,&work,100000,0,&result,error,sizeof error));
        assert(result.outcome==(expected ? NM_UNIFIED : NM_NO_MATCH));
        assert(nm_constraint_truth(NM_CALL_OK,&result)==(!expected ? NM_TRUTH_REFUTED :
            entailed ? NM_TRUTH_ENTAILED : NM_TRUTH_OPEN));
        if (expected) {
            TcTerm na=ground_value(terms,in,&work,in->left,0),nb=ground_value(terms,in,&work,in->right,0);
            assert(na==nb);
            if (term_node(terms,ra).ground) { assert(na==ra);++ground_values; }
            else assert(!na);
            if (term_node(terms,rb).ground) { assert(nb==rb);++ground_values; }
            else assert(!nb);
        }
        ++checked;free(work.bindings);free(work.pairs);free(work.scan);free(work.state);free(work.marks);
    }
    nm_match_prepared_free(prepared);solver_free(&s);
}

static void compare_plan(const NmUnifyProgram *program,TcTerms *terms,
                          const TcTerm tells[4],size_t ntells,const TcTerm guards[4],size_t nguards,
                          NmAsk ask,NmExecution execution) {
    Solver s={0};assert(reserve(&s.local,&s.local_cap,64) && reserve_unknowns(&s,64));
    NmAdmissionStatus expected=NM_ADMITTED;size_t done_tells=0,done_guards=0;
    for (size_t step=0;step<ntells+nguards;++step) {
        bool guard=ask==NM_ASK_BEFORE_TELLS ? step<nguards : step>=ntells;
        size_t i=ask==NM_ASK_BEFORE_TELLS ? (guard ? step : step-nguards) : (guard ? step-ntells : step);
        if (!guard) {
            if (!unify(terms,&s,tells[2*i],0,tells[2*i+1],0)) { expected=NM_TELL_REFUSED;break; }
            ++done_tells;
        } else {
            Explorer check={.terms=terms,.solver=s};
            bool holds=check_holds(&check,(TcCheck){.kind=TC_CHECK_EQUAL,
                .left=guards[2*i],.right=guards[2*i+1]},0);
            s=check.solver;
            if (!holds) {
                Mark at=mark(&s);bool possible=unify(terms,&s,guards[2*i],0,guards[2*i+1],0);undo(&s,at);
                expected=possible ? NM_GUARD_OPEN : NM_GUARD_REFUTED;break;
            }
            ++done_guards;
        }
    }
    assert(!s.failed);
    TcTerm resolved[8];size_t roots=2*(ntells+nguards);
    for (size_t i=0;i<roots;++i)
        resolved[i]=expected==NM_ADMITTED ? resolve(terms,&s,i<2*ntells ? tells[i] : guards[i-2*ntells],0) : 0;
    NmTcalcTermView source={terms,(TcTerm)(terms->nodes-1),900};
    NmQueryLimits l={.import={1000,1000,1000,10000},.pairs=128,.scan=128,.memo=128};
    NmQueryView view={.terms=nm_tcalc_term_view(&source)};NmQuery *q=NULL;NmQueryAnswer answer;char error[256];
    NmSolution *initial=NULL;
    assert(nm_query_open(program,execution,view,NULL,0,0,NULL,0,l,100000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
    assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_ANSWER);
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&initial)==NM_VIEW_READY);nm_query_free(q);
    NmHostConstraint ts[2],gs[2];
    for (size_t i=0;i<ntells;++i) ts[i]=(NmHostConstraint){{tells[2*i],600},{tells[2*i+1],600}};
    for (size_t i=0;i<nguards;++i) gs[i]=(NmHostConstraint){{guards[2*i],600},{guards[2*i+1],600}};
    NmAdmission *admission=NULL;
    assert(nm_admission_open(program,execution,initial,ask,ts,ntells,gs,nguards,l,NULL,NULL,1000000,0,
        &admission,error,sizeof error)==NM_ADMISSION_YIELD);nm_solution_free(initial);
    NmAdmissionStatus status=NM_ADMISSION_YIELD;
    do { status=nm_admission_next(admission,1,error,sizeof error); } while (status==NM_ADMISSION_YIELD);
    if (status!=expected)
        fprintf(stderr,"plan mismatch mode %u ask %u: native %u expected %u\n",(unsigned)execution,(unsigned)ask,(unsigned)status,(unsigned)expected);
    assert(status==expected);
    NmAdmissionReceipt receipt=nm_admission_receipt(admission);
    assert(receipt.tells_done==done_tells && receipt.guards_done==done_guards);++plan_decisions;
    if (status==NM_ADMITTED) for (size_t i=0;i<roots;++i) if (term_node(terms,resolved[i]).ground) {
        TcTerm original=i<2*ntells ? tells[i] : guards[i-2*ntells];
        NmHostValue pattern={original,600};NmQueryRow row={0,{resolved[i],600}};
        view.terms=nm_solution_view(nm_admission_result(admission));q=NULL;
        assert(nm_query_open(program,execution,view,&pattern,1,0,&row,1,l,100000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
        assert(nm_query_next(q,2,&answer,error,sizeof error)==NM_QUERY_ANSWER);
        assert(nm_constraint_truth(NM_CALL_OK,&answer.result)==NM_TRUTH_ENTAILED);++plan_ground_values;nm_query_free(q);
    }
    if (status==NM_ADMITTED) {
        TcTerm rows[]={tc_symbol(terms,"a",1),tc_symbol(terms,"b",1),tc_symbol(terms,"a",1)};
        uint32_t nrows=3;bool ground=false;
        assert(canonical_form(terms,rows,&nrows,TC_STORE_MULTISET,&ground) && ground && nrows==3);
        TcTerm pattern=ntells ? tells[0] : nguards ? guards[0] : tc_local(terms,0);
        source.last_term=(TcTerm)(terms->nodes-1);
        NmQueryRow host_rows[3];for (size_t i=0;i<3;++i) host_rows[i]=(NmQueryRow){i,{rows[i],0}};
        NmQueryView region_view={.terms=nm_solution_view(nm_admission_result(admission))};NmCensus *census=NULL;
        assert(nm_census_open(program,execution,region_view,(NmHostValue){pattern,600},host_rows,3,l,1000000,0,
            &census,error,sizeof error)==NM_CENSUS_YIELD);
        NmCensusStatus scanned;
        do { scanned=nm_census_next(census,2,error,sizeof error); } while (scanned==NM_CENSUS_YIELD);
        assert(scanned==NM_CENSUS_DONE);
        Explorer region={.terms=terms,.solver=s,.row=rows,.rows=nrows,.ground=ground};
        for (size_t test=0;test<6;++test) {
            TcCheck check={.left=pattern};NmRegionCheck kind;
            if (!test) { check.kind=TC_CHECK_ABSENT;kind=NM_REGION_ABSENT; }
            else if (test==5) { check.kind=TC_CHECK_CENSUS;kind=NM_REGION_STABLE; }
            else { check.kind=TC_CHECK_COUNT;check.count=test-1;kind=NM_REGION_COUNT; }
            bool holds=check_holds(&region,check,0);
            NmRegionDecision actual=nm_census_check(census,kind,check.count);
            assert(actual==(holds ? NM_REGION_ENABLED : NM_REGION_NOT_ENTAILED));++region_decisions;
        }
        s=region.solver;assert(!s.failed);nm_census_free(census);
    }
    nm_admission_free(admission);solver_free(&s);
}

int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    TcTerms *terms=tc_terms_new();assert(terms);TcTerm pool[64];size_t n=0;
    pool[n++]=tc_symbol(terms,"f",1);pool[n++]=tc_symbol(terms,"a",1);pool[n++]=tc_symbol(terms,"b",1);
    pool[n++]=tc_integer(terms,INT64_MIN);pool[n++]=tc_integer(terms,INT64_MAX);
    pool[n++]=tc_string(terms,"a\0b",3);pool[n++]=tc_string(terms,"a",1);
    pool[n++]=tc_local(terms,0);pool[n++]=tc_local(terms,1);
    pool[n++]=tc_unknown(terms,0);pool[n++]=tc_unknown(terms,1);
    pool[n++]=tc_expression(terms,NULL,0);
    const TcTerm items[][3]={{pool[0],pool[7],pool[7]},{pool[0],pool[1],pool[2]},
        {pool[0],pool[7],pool[1]},{pool[0],pool[2],pool[8]},
        {pool[0],pool[9],pool[7]},{pool[0],pool[8],pool[10]}};
    for (size_t i=0;i<sizeof items/sizeof *items;++i) pool[n++]=tc_expression(terms,items[i],3);
    for (size_t i=0;i<n;++i) for (size_t j=0;j<n;++j) for (unsigned scope=0;scope<2;++scope)
        compare(program,terms,pool[i],pool[j],scope!=0);
    for (size_t seed=0;seed<96;++seed) {
        TcTerm ts[4]={pool[(7*seed+7)%n],pool[(11*seed+1)%n],pool[(13*seed+8)%n],pool[(17*seed+7)%n]};
        TcTerm gs[4]={ts[0],ts[1],pool[(19*seed+8)%n],pool[(23*seed+1)%n]};
        size_t ntells=seed%7 ? 2 : 0,nguards=seed%11 ? 2 : 0;
        if (!seed) {
            ts[0]=pool[7];ts[1]=pool[1];ts[2]=pool[8];ts[3]=pool[7];
            gs[0]=pool[7];gs[1]=pool[1];gs[2]=pool[8];gs[3]=pool[1];ntells=nguards=2;
        }
        for (unsigned mode=0;mode<3;++mode) for (unsigned ask=0;ask<2;++ask)
            compare_plan(program,terms,ts,ntells,gs,nguards,(NmAsk)ask,(NmExecution)mode);
    }
    NmTcalcTermView source={terms,(TcTerm)(terms->nodes-1),900};NmTermView view=nm_tcalc_term_view(&source);
    NmPreparedMatch *prepared=NULL;
    assert(nm_match_prepare(&view,(NmHostValue){0,0},(NmHostValue){pool[0],0},
        (NmImportLimits){100,100,100,1000},&prepared,NULL)==NM_VIEW_FAULT && !prepared);
    assert(nm_match_prepare(&view,(NmHostValue){UINT64_MAX,0},(NmHostValue){pool[0],0},
        (NmImportLimits){100,100,100,1000},&prepared,NULL)==NM_VIEW_FAULT && !prepared);
    tc_terms_free(terms);nm_unify_program_close(program);
    printf("CeTTa tcalc term client: %zu native/reference/compare decisions and constraint-truth verdicts agree with the actual C solver/scoped identity oracle; %zu ground results checked; local scopes, shared unknowns, strings and extrema passed\n",
        checked,ground_values);
    printf("CeTTa equational-plan client: %zu admission/prefix comparisons and %zu ground-result observations agree with the actual C solver and guard order\n",
        plan_decisions,plan_ground_values);
    printf("CeTTa region-guard client: %zu absence/count/census decisions agree with the actual guard implementation\n",region_decisions);
    return 0;
}
