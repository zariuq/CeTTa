#include "native_admission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NmAdmission {
    const NmUnifyProgram *program;
    NmExecution execution;
    NmSolution *current;
    NmAsk ask;
    NmHostConstraint *tells,*guards;
    size_t ntells,nguards,index;
    NmQueryLimits limits;
    void *pin;
    bool (*valid)(void *);
    uint64_t budget;
    NmAdmissionStatus status;
    NmAdmissionReceipt receipt;
    NmAdmissionTrace trace;
    void *trace_context;
};
static NmAdmissionStatus fail(char *error,size_t size,NmAdmissionStatus status,const char *message) {
    if (error && size) snprintf(error,size,"%s",message);
    return status;
}
void nm_admission_free(NmAdmission *a) {
    if (!a) return;
    nm_solution_free(a->current);free(a->tells);free(a->guards);free(a);
}
NmAdmissionStatus nm_admission_open(const NmUnifyProgram *program,NmExecution execution,
                                    NmSolution *initial,NmAsk ask,
                                    const NmHostConstraint *tells,size_t ntells,
                                    const NmHostConstraint *guards,size_t nguards,
                                    NmQueryLimits limits,void *pin,bool (*valid)(void *),
                                    uint64_t fuel_budget,uint64_t fuel_used,
                                    NmAdmission **out,char *error,size_t error_size) {
    if (out) *out=NULL;
    if (error && error_size) error[0]=0;
    if (!out || !program || !initial || (ntells && !tells) || (nguards && !guards) ||
        (ask!=NM_ASK_BEFORE_TELLS && ask!=NM_ASK_AFTER_TELLS) ||
        (execution!=NM_REFERENCE && execution!=NM_NATIVE && execution!=NM_COMPARE) || fuel_used>fuel_budget)
        return fail(error,error_size,NM_ADMISSION_FAULT,"invalid equational admission arguments");
    if (execution!=NM_REFERENCE && !nm_unify_native_bytes(program,NULL))
        return fail(error,error_size,NM_ADMISSION_FAULT,"admission requires a native program");
    if (!limits.memo || (limits.memo&(limits.memo-1)))
        return fail(error,error_size,NM_ADMISSION_FAULT,"admission memo capacity must be a positive power of two");
    if (valid && !valid(pin)) return NM_ADMISSION_INVALIDATED;
    if (ntells>SIZE_MAX/sizeof *tells || nguards>SIZE_MAX/sizeof *guards || ntells>SIZE_MAX-nguards)
        return fail(error,error_size,NM_ADMISSION_RESOURCE,"admission vector size overflow");
    NmAdmission *a=calloc(1,sizeof *a);
    if (!a) return fail(error,error_size,NM_ADMISSION_RESOURCE,"admission allocation failed");
    a->program=program;a->execution=execution;a->ask=ask;a->ntells=ntells;a->nguards=nguards;
    a->limits=limits;a->pin=pin;a->valid=valid;a->budget=fuel_budget;a->status=NM_ADMISSION_YIELD;
    a->receipt.fuel_used=fuel_used;a->receipt.work.fuel_used=fuel_used;
    a->receipt.peak_depth=nm_solution_depth(initial);a->receipt.truth=NM_TRUTH_UNAVAILABLE;
    if (ntells) {
        a->tells=malloc(ntells*sizeof *tells);
        if (!a->tells) { nm_admission_free(a);return NM_ADMISSION_RESOURCE; }
        memcpy(a->tells,tells,ntells*sizeof *tells);
    }
    if (nguards) {
        a->guards=malloc(nguards*sizeof *guards);
        if (!a->guards) { nm_admission_free(a);return NM_ADMISSION_RESOURCE; }
        memcpy(a->guards,guards,nguards*sizeof *guards);
    }
    if (!nm_solution_retain(initial)) { nm_admission_free(a);return NM_ADMISSION_RESOURCE; }
    a->current=initial;*out=a;return NM_ADMISSION_YIELD;
}
static NmAdmissionStatus terminal(NmAdmission *a,NmAdmissionStatus status) {
    a->status=status;return status;
}
static void observe(void *context,const NmQueryEvent *event) {
    NmAdmission *a=context;
    if (event->kind==NM_QUERY_CORE_RESULT) {
        a->receipt.core=event->result;a->receipt.has_core=true;a->receipt.truth=event->truth;
    }
    if (a->trace) {
        NmAdmissionEvent wrapped={a->receipt.last_index,a->receipt.last_is_guard,*event};
        a->trace(a->trace_context,&wrapped);
    }
}
static bool add(uint64_t *sum,uint64_t value) {
    if (value>UINT64_MAX-*sum) return false;
    *sum+=value;return true;
}
static bool collect(NmAdmission *a,const NmQuery *query) {
    NmQueryStats s=nm_query_stats(query);NmQueryStats *work=&a->receipt.work;
    a->receipt.fuel_used=s.fuel_used;work->fuel_used=s.fuel_used;
    return add(&work->candidates,s.candidates) && add(&work->unifier_calls,s.unifier_calls) &&
        add(&work->inspections,s.inspections) && add(&work->children,s.children) && add(&work->bindings,s.bindings);
}
static NmAdmissionStatus map_status(NmQueryStatus status) {
    switch (status) {
    case NM_QUERY_EXHAUSTED:return NM_ADMISSION_EXHAUSTED;
    case NM_QUERY_RESOURCE:return NM_ADMISSION_RESOURCE;
    case NM_QUERY_INTERRUPTED:return NM_ADMISSION_INTERRUPTED;
    case NM_QUERY_INVALIDATED:return NM_ADMISSION_INVALIDATED;
    case NM_QUERY_DEFER:return NM_ADMISSION_DEFER;
    default:return NM_ADMISSION_FAULT;
    }
}
static NmAdmissionStatus equation(NmAdmission *a,bool guard,size_t index,char *error,size_t size) {
    a->receipt.last_index=index;a->receipt.last_is_guard=guard;
    a->receipt.has_query=false;a->receipt.has_core=false;a->receipt.truth=NM_TRUTH_UNAVAILABLE;
    if (a->receipt.fuel_used>=a->budget) return NM_ADMISSION_EXHAUSTED;
    if (a->receipt.activations==UINT64_MAX) return NM_ADMISSION_RESOURCE;
    ++a->receipt.activations;++a->receipt.fuel_used;
    a->receipt.work.fuel_used=a->receipt.fuel_used;
    NmHostConstraint c=guard ? a->guards[index] : a->tells[index];
    NmQueryView view={nm_solution_view(a->current),a->pin,a->valid};
    NmQueryRow row={0,c.right};NmQuery *q=NULL;
    NmQueryStatus status=nm_query_open(a->program,a->execution,view,&c.left,1,0,&row,1,a->limits,
        a->budget,a->receipt.fuel_used,&q,error,size);
    a->receipt.has_query=true;a->receipt.last_query=status;
    if (status!=NM_QUERY_YIELD) return map_status(status);
    nm_query_set_trace(q,observe,a);NmQueryAnswer answer;
    status=nm_query_next(q,2,&answer,error,size);a->receipt.last_query=status;
    if (!collect(a,q)) { nm_query_free(q);return NM_ADMISSION_RESOURCE; }
    NmAdmissionStatus result=NM_ADMISSION_YIELD;
    if (status==NM_QUERY_END) result=guard ? NM_GUARD_REFUTED : NM_TELL_REFUSED;
    else if (status!=NM_QUERY_ANSWER) result=map_status(status);
    else if (guard) {
        if (a->receipt.truth==NM_TRUTH_ENTAILED) ++a->receipt.guards_done;
        else result=a->receipt.truth==NM_TRUTH_OPEN ? NM_GUARD_OPEN : NM_ADMISSION_FAULT;
    } else {
        NmSolution *next=NULL;
        NmViewStatus captured=nm_solution_capture(answer.prepared,&answer.result,answer.work,&next);
        if (captured!=NM_VIEW_READY) result=captured==NM_VIEW_RESOURCE ? NM_ADMISSION_RESOURCE : NM_ADMISSION_FAULT;
        else {
            nm_solution_free(a->current);a->current=next;
            ++a->receipt.tells_done;++a->receipt.snapshots;
            size_t depth=nm_solution_depth(next);
            if (depth>a->receipt.peak_depth) a->receipt.peak_depth=depth;
        }
    }
    nm_query_free(q);return result;
}
NmAdmissionStatus nm_admission_next(NmAdmission *a,uint64_t quantum,char *error,size_t size) {
    if (error && size) error[0]=0;
    if (!a) return fail(error,size,NM_ADMISSION_FAULT,"missing admission");
    if (a->status!=NM_ADMISSION_YIELD) return a->status;
    if (a->valid && !a->valid(a->pin)) return terminal(a,NM_ADMISSION_INVALIDATED);
    size_t total=a->ntells+a->nguards;
    for (uint64_t step=0;step<quantum && a->index<total;++step) {
        bool guard;size_t index;
        if (a->ask==NM_ASK_BEFORE_TELLS) {
            guard=a->index<a->nguards;index=guard ? a->index : a->index-a->nguards;
        } else {
            guard=a->index>=a->ntells;index=guard ? a->index-a->ntells : a->index;
        }
        NmAdmissionStatus status=equation(a,guard,index,error,size);
        if (status!=NM_ADMISSION_YIELD) return terminal(a,status);
        ++a->index;
        if (a->valid && !a->valid(a->pin)) return terminal(a,NM_ADMISSION_INVALIDATED);
    }
    return a->index==total ? terminal(a,NM_ADMITTED) : NM_ADMISSION_YIELD;
}
NmAdmissionReceipt nm_admission_receipt(const NmAdmission *a) {
    return a ? a->receipt : (NmAdmissionReceipt){0};
}
const NmSolution *nm_admission_result(const NmAdmission *a) {
    return a && a->status==NM_ADMITTED && (!a->valid || a->valid(a->pin)) ? a->current : NULL;
}
NmSolution *nm_admission_take_result(NmAdmission *a) {
    if (!nm_admission_result(a)) return NULL;
    NmSolution *result=a->current;a->current=NULL;return result;
}
void nm_admission_set_trace(NmAdmission *a,NmAdmissionTrace trace,void *context) {
    if (a) { a->trace=trace;a->trace_context=context; }
}
