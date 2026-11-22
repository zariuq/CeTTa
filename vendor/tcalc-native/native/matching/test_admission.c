#include "native_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { bool pinned,interrupted,defer;size_t reads; } Source;
typedef struct { size_t proposed,core;Source *invalidate;NmQueryEventKind invalidate_kind; } Trace;
static size_t plans,transitions;
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    Source *source=context;++source->reads;
    if (source->interrupted) return NM_VIEW_INTERRUPTED;
    if (source->defer) return NM_VIEW_DEFER;
    if (value.syntax<10) *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}};
    else *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};
    return NM_VIEW_READY;
}
static bool valid(void *context) { return ((Source *)context)->pinned; }
static NmQueryLimits limits(void) {
    return (NmQueryLimits){.import={100,100,16,1000},.pairs=32,.scan=32,.memo=128};
}
static NmSolution *initial(const NmUnifyProgram *program,NmExecution mode,Source *source) {
    NmQueryView view={.terms={source,inspect,NULL,NULL},.pin=source,.valid=valid};
    NmQuery *q=NULL;char error[256];NmQueryAnswer answer;NmSolution *solution=NULL;
    assert(nm_query_open(program,mode,view,NULL,0,0,NULL,0,limits(),10000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
    assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_ANSWER);
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&solution)==NM_VIEW_READY);
    nm_query_free(q);return solution;
}
static NmAdmission *open_plan(const NmUnifyProgram *program,NmExecution mode,NmSolution *context,Source *source,
                              NmAsk ask,const NmHostConstraint *tells,size_t ntells,
                              const NmHostConstraint *guards,size_t nguards,NmQueryLimits l,
                              uint64_t budget,uint64_t used) {
    char error[256];NmAdmission *a=NULL;++plans;
    NmAdmissionStatus s=nm_admission_open(program,mode,context,ask,tells,ntells,guards,nguards,l,
        source,valid,budget,used,&a,error,sizeof error);
    if (s!=NM_ADMISSION_YIELD) fprintf(stderr,"admission open: %u %s\n",(unsigned)s,error);
    assert(s==NM_ADMISSION_YIELD && a);return a;
}
static NmAdmissionStatus next(NmAdmission *a,uint64_t quantum) {
    char error[256];++transitions;
    return nm_admission_next(a,quantum,error,sizeof error);
}
static void trace(void *context,const NmAdmissionEvent *event) {
    Trace *t=context;
    if (event->query.kind==NM_QUERY_PROPOSED) ++t->proposed;
    if (event->query.kind==NM_QUERY_CORE_RESULT) ++t->core;
    if (t->invalidate && event->query.kind==t->invalidate_kind) t->invalidate->pinned=false;
}
static void probe_context(const NmUnifyProgram *program,NmExecution mode,const NmSolution *context,
                           Source *source,NmHostValue variable,NmHostValue value,NmConstraintTruth truth) {
    NmQueryView view={nm_solution_view(context),source,valid};NmQueryRow row={0,value};NmQuery *q=NULL;
    char error[256];NmQueryAnswer answer;
    assert(nm_query_open(program,mode,view,&variable,1,0,&row,1,limits(),10000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
    if (truth==NM_TRUTH_REFUTED) assert(nm_query_next(q,2,&answer,error,sizeof error)==NM_QUERY_END);
    else {
        assert(nm_query_next(q,2,&answer,error,sizeof error)==NM_QUERY_ANSWER);
        assert(nm_constraint_truth(NM_CALL_OK,&answer.result)==truth);
    }
    nm_query_free(q);
}
static void policies(const NmUnifyProgram *program,NmExecution mode) {
    Source source={.pinned=true};NmSolution *start=initial(program,mode,&source);
    NmHostValue x={1,8},y={2,8},a={10,77},b={11,90};
    NmHostConstraint tell[]={{x,a},{y,x}},guard[]={{x,a},{y,a}};
    NmAdmission *before=open_plan(program,mode,start,&source,NM_ASK_BEFORE_TELLS,tell,1,guard,1,limits(),100000,17);
    assert(next(before,1)==NM_GUARD_OPEN && !nm_admission_result(before));
    NmAdmissionReceipt r=nm_admission_receipt(before);
    assert(r.activations==1 && !r.tells_done && !r.guards_done && !r.snapshots && r.last_is_guard &&
        r.has_core && r.truth==NM_TRUTH_OPEN && r.fuel_used>18);
    uint64_t fuel=r.fuel_used;assert(next(before,1)==NM_GUARD_OPEN && nm_admission_receipt(before).fuel_used==fuel);
    probe_context(program,mode,start,&source,x,a,NM_TRUTH_OPEN);nm_admission_free(before);

    NmAdmission *after=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,tell,2,guard,2,limits(),100000,17);
    tell[0].right=b; /* The compiled call plan owns its vector. */
    Trace t={0};nm_admission_set_trace(after,trace,&t);
    assert(next(after,0)==NM_ADMISSION_YIELD && nm_admission_receipt(after).fuel_used==17);
    for (size_t step=1;step<=4;++step) {
        assert(next(after,1)==(step==4 ? NM_ADMITTED : NM_ADMISSION_YIELD));
        r=nm_admission_receipt(after);assert(r.activations==step && r.fuel_used<=100000);
        if (step<4) assert(!nm_admission_result(after));
    }
    assert(r.tells_done==2 && r.guards_done==2 && r.snapshots==2 && r.peak_depth==3 &&
        r.truth==NM_TRUTH_ENTAILED && t.proposed==4 && t.core==4 && r.work.unifier_calls==4);
    const NmSolution *result=nm_admission_result(after);assert(result);
    probe_context(program,mode,result,&source,x,a,NM_TRUTH_ENTAILED);
    probe_context(program,mode,result,&source,y,a,NM_TRUTH_ENTAILED);
    probe_context(program,mode,start,&source,x,a,NM_TRUTH_OPEN);
    source.pinned=false;assert(!nm_admission_result(after) && !nm_admission_take_result(after));source.pinned=true;
    NmSolution *moved=nm_admission_take_result(after);assert(moved==result && !nm_admission_result(after));
    assert(!nm_admission_take_result(after));nm_admission_free(after);
    probe_context(program,mode,moved,&source,y,a,NM_TRUTH_ENTAILED);nm_solution_free(moved);

    tell[0]=(NmHostConstraint){x,a};tell[1]=(NmHostConstraint){x,b};
    NmAdmission *contradiction=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,tell,2,NULL,0,limits(),100000,0);
    assert(next(contradiction,2)==NM_TELL_REFUSED && !nm_admission_result(contradiction));
    r=nm_admission_receipt(contradiction);
    assert(r.activations==2 && r.tells_done==1 && r.snapshots==1 && !r.last_is_guard && r.last_index==1 &&
        r.has_core && r.core.outcome==NM_NO_MATCH && r.fuel_used>0);
    probe_context(program,mode,start,&source,x,b,NM_TRUTH_OPEN);nm_admission_free(contradiction);
    guard[0]=(NmHostConstraint){x,b};
    NmAdmission *refuted=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,tell,1,guard,1,limits(),100000,0);
    assert(next(refuted,2)==NM_GUARD_REFUTED && !nm_admission_result(refuted));
    r=nm_admission_receipt(refuted);assert(r.tells_done==1 && !r.guards_done && r.truth==NM_TRUTH_REFUTED);
    nm_admission_free(refuted);
    NmAdmission *empty=open_plan(program,mode,start,&source,NM_ASK_BEFORE_TELLS,NULL,0,NULL,0,limits(),0,0);
    nm_solution_free(start); /* The admission owns its own reference. */
    assert(next(empty,0)==NM_ADMITTED && nm_admission_result(empty));
    assert(!nm_admission_receipt(empty).activations);nm_admission_free(empty);
}
static void failures(const NmUnifyProgram *program,NmExecution mode) {
    Source source={.pinned=true};NmSolution *start=initial(program,mode,&source);
    NmHostConstraint tell={{1,8},{10,77}};
    NmQueryView direct_view={nm_solution_view(start),&source,valid};
    NmQueryRow direct_row={0,tell.right};NmQuery *direct=NULL;NmQueryAnswer direct_answer;char direct_error[256];
    assert(nm_query_open(program,mode,direct_view,&tell.left,1,0,&direct_row,1,limits(),10000,0,
        &direct,direct_error,sizeof direct_error)==NM_QUERY_YIELD);
    assert(nm_query_next(direct,1,&direct_answer,direct_error,sizeof direct_error)==NM_QUERY_ANSWER);
    uint64_t body_fuel=direct_answer.result.fuel_used;assert(body_fuel>1);nm_query_free(direct);
    for (uint64_t extra=0;extra<=1;++extra) {
        NmAdmission *exact=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,
            limits(),body_fuel+extra,0);
        assert(next(exact,1)==(extra ? NM_ADMITTED : NM_ADMISSION_EXHAUSTED));
        assert(nm_admission_receipt(exact).fuel_used==body_fuel+extra);
        nm_admission_free(exact);
    }
    for (uint64_t budget=0;budget<=1;++budget) {
        NmAdmission *a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,limits(),budget,0);
        assert(next(a,1)==NM_ADMISSION_EXHAUSTED && !nm_admission_result(a));
        NmAdmissionReceipt r=nm_admission_receipt(a);
        assert(r.fuel_used==budget && r.activations==budget && r.has_core==(budget!=0));
        nm_admission_free(a);
    }
    NmQueryLimits l=limits();l.pairs=0;
    NmAdmission *a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,l,10000,0);
    assert(next(a,1)==NM_ADMISSION_RESOURCE && nm_admission_receipt(a).fuel_used==1 && !nm_admission_result(a));
    nm_admission_free(a);
    a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,limits(),10000,0);
    source.interrupted=true;assert(next(a,1)==NM_ADMISSION_INTERRUPTED);source.interrupted=false;nm_admission_free(a);
    a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,limits(),10000,0);
    source.defer=true;assert(next(a,1)==NM_ADMISSION_DEFER);source.defer=false;nm_admission_free(a);
    for (unsigned kind=0;kind<3;++kind) {
        source.pinned=true;
        a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,NULL,0,limits(),10000,0);
        Trace t={.invalidate=&source,.invalidate_kind=(NmQueryEventKind)kind};nm_admission_set_trace(a,trace,&t);
        assert(next(a,1)==NM_ADMISSION_INVALIDATED && !nm_admission_result(a));
        assert(!nm_admission_receipt(a).tells_done);nm_admission_free(a);
    }
    source.pinned=true;
    NmHostConstraint guards[]={{tell.left,tell.right}};
    a=open_plan(program,mode,start,&source,NM_ASK_AFTER_TELLS,&tell,1,guards,1,limits(),10000,0);
    assert(next(a,1)==NM_ADMISSION_YIELD && nm_admission_receipt(a).tells_done==1);
    source.pinned=false;assert(next(a,1)==NM_ADMISSION_INVALIDATED && !nm_admission_result(a));nm_admission_free(a);
    source.pinned=true;
    char error[256];a=(NmAdmission *)(uintptr_t)1;
    assert(nm_admission_open(program,mode,start,(NmAsk)99,&tell,1,NULL,0,limits(),&source,valid,1000,0,
        &a,error,sizeof error)==NM_ADMISSION_FAULT && !a);
    a=(NmAdmission *)(uintptr_t)1;
    assert(nm_admission_open(program,mode,start,NM_ASK_AFTER_TELLS,&tell,SIZE_MAX,NULL,0,limits(),&source,valid,1000,0,
        &a,error,sizeof error)==NM_ADMISSION_RESOURCE && !a);
    nm_solution_free(start);
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    for (unsigned mode=0;mode<3;++mode) { policies(program,(NmExecution)mode);failures(program,(NmExecution)mode); }
    assert(!nm_solution_retain(NULL));nm_admission_free(NULL);nm_admission_set_trace(NULL,NULL,NULL);
    nm_unify_program_close(program);
    printf("equational admission: %zu plans, %zu controller calls; ask order, open/refuted guards, retained contexts, fuel/prefix accounting and invalidation passed\n",plans,transitions);
    return 0;
}
