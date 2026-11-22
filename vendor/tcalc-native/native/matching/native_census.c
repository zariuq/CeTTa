#include "native_census.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NmCensus {
    const NmUnifyProgram *program;
    NmExecution execution;
    NmQueryView view;
    NmHostValue pattern;
    NmQueryLimits limits;
    NmQueryRow *rows;
    size_t count;
    uint64_t budget;
    NmQuery *query;
    uint64_t *certain,*possible;
    NmCensusObservation observation;
    NmCensusReceipt receipt;
    NmCensusTrace trace;
    void *trace_context;
};
static NmCensusStatus fail(char *error,size_t size,NmCensusStatus status,const char *message) {
    if (error && size) snprintf(error,size,"%s",message);
    return status;
}
static NmCensusStatus mapped(NmQueryStatus status) {
    switch (status) {
    case NM_QUERY_EXHAUSTED:return NM_CENSUS_EXHAUSTED;
    case NM_QUERY_RESOURCE:return NM_CENSUS_RESOURCE;
    case NM_QUERY_INTERRUPTED:return NM_CENSUS_INTERRUPTED;
    case NM_QUERY_INVALIDATED:return NM_CENSUS_INVALIDATED;
    case NM_QUERY_DEFER:return NM_CENSUS_DEFER;
    default:return NM_CENSUS_FAULT;
    }
}
static bool valid(const NmCensus *c) { return !c->view.valid || c->view.valid(c->view.pin); }
static void observe(void *context,const NmQueryEvent *event) {
    NmCensus *c=context;c->receipt.last_validation=!c->receipt.validated;
    if (event->kind==NM_QUERY_PROPOSED) {
        c->receipt.has_core=false;c->receipt.truth=NM_TRUTH_UNAVAILABLE;
        c->receipt.has_row=c->receipt.validated && event->count==1;
        if (c->receipt.has_row) c->receipt.last_row=event->selected[0];
    }
    if (event->kind==NM_QUERY_CORE_RESULT) {
        c->receipt.has_core=true;c->receipt.core=event->result;c->receipt.truth=event->truth;
    }
    if (c->trace) {
        NmCensusEvent wrapped={!c->receipt.validated,*event};c->trace(c->trace_context,&wrapped);
    }
}
void nm_census_free(NmCensus *c) {
    if (!c) return;
    nm_query_free(c->query);free(c->rows);free(c->certain);free(c->possible);free(c);
}
NmCensusStatus nm_census_open(const NmUnifyProgram *program,NmExecution execution,NmQueryView view,
                              NmHostValue pattern,const NmQueryRow *rows,size_t count,NmQueryLimits limits,
                              uint64_t budget,uint64_t used,NmCensus **out,char *error,size_t size) {
    if (out) *out=NULL;
    if (error && size) error[0]=0;
    if (!out || (count && !rows)) return fail(error,size,NM_CENSUS_FAULT,"invalid census arguments");
    if (count>SIZE_MAX/sizeof *rows || count>SIZE_MAX/sizeof(uint64_t))
        return fail(error,size,NM_CENSUS_RESOURCE,"census vector size overflow");
    NmQueryRow self={0,pattern};NmQuery *validation=NULL;
    NmQueryStatus started=nm_query_open(program,execution,view,&pattern,1,0,&self,1,limits,budget,used,
        &validation,error,size);
    if (started!=NM_QUERY_YIELD) return mapped(started);
    NmCensus *c=calloc(1,sizeof *c);
    if (!c) { nm_query_free(validation);return NM_CENSUS_RESOURCE; }
    c->program=program;c->execution=execution;c->view=view;c->pattern=pattern;c->limits=limits;
    c->query=validation;c->count=count;c->budget=budget;c->receipt.fuel_used=used;
    nm_query_set_trace(c->query,observe,c);
    if (count) {
        c->rows=malloc(count*sizeof *rows);c->certain=malloc(count*sizeof *c->certain);
        c->possible=malloc(count*sizeof *c->possible);
        if (!c->rows || !c->certain || !c->possible) { nm_census_free(c);return NM_CENSUS_RESOURCE; }
        memcpy(c->rows,rows,count*sizeof *rows);
    }
    c->observation.certain=c->certain;c->observation.possible=c->possible;
    *out=c;return NM_CENSUS_YIELD;
}
static NmCensusStatus terminal(NmCensus *c,NmCensusStatus status) {
    c->receipt.status=status;return status;
}
NmCensusStatus nm_census_next(NmCensus *c,uint64_t quantum,char *error,size_t size) {
    if (error && size) error[0]=0;
    if (!c) return fail(error,size,NM_CENSUS_FAULT,"missing census");
    if (c->receipt.status!=NM_CENSUS_YIELD) return c->receipt.status;
    if (!valid(c)) return terminal(c,NM_CENSUS_INVALIDATED);
    for (uint64_t step=0;step<quantum;++step) {
        NmQueryAnswer answer;
        NmQueryStatus status=nm_query_next(c->query,1,&answer,error,size);
        NmQueryStats work=nm_query_stats(c->query);
        c->receipt.fuel_used=work.fuel_used;
        if (!c->receipt.validated) c->receipt.validation=work;else c->receipt.scan=work;
        if (!c->receipt.validated) {
            if (status!=NM_QUERY_ANSWER) return terminal(c,mapped(status));
            if (nm_constraint_truth(NM_CALL_OK,&answer.result)!=NM_TRUTH_ENTAILED)
                return terminal(c,NM_CENSUS_FAULT);
            c->receipt.validated=true;nm_query_free(c->query);c->query=NULL;
            if (!c->count) return terminal(c,NM_CENSUS_DONE);
            status=nm_query_open(c->program,c->execution,c->view,&c->pattern,1,0,c->rows,c->count,c->limits,
                c->budget,c->receipt.fuel_used,&c->query,error,size);
            free(c->rows);c->rows=NULL;
            if (status!=NM_QUERY_YIELD) return terminal(c,mapped(status));
            nm_query_set_trace(c->query,observe,c);
            continue;
        }
        if (status==NM_QUERY_ANSWER) {
            NmConstraintTruth truth=nm_constraint_truth(NM_CALL_OK,&answer.result);
            if (answer.count!=1 || c->observation.possible_count>=c->count ||
                (truth!=NM_TRUTH_OPEN && truth!=NM_TRUTH_ENTAILED)) return terminal(c,NM_CENSUS_FAULT);
            c->possible[c->observation.possible_count++]=answer.selected[0];
            if (truth==NM_TRUTH_ENTAILED) c->certain[c->observation.certain_count++]=answer.selected[0];
        } else if (status==NM_QUERY_END) return terminal(c,NM_CENSUS_DONE);
        else if (status!=NM_QUERY_YIELD) return terminal(c,mapped(status));
    }
    return NM_CENSUS_YIELD;
}
NmCensusReceipt nm_census_receipt(const NmCensus *c) {
    if (!c) return (NmCensusReceipt){.status=NM_CENSUS_FAULT};
    NmCensusReceipt r=c->receipt;r.view_valid=valid(c);
    r.certain_count=c->observation.certain_count;r.possible_count=c->observation.possible_count;return r;
}
const NmCensusObservation *nm_census_observation(const NmCensus *c) {
    return c && c->receipt.status==NM_CENSUS_DONE && valid(c) ? &c->observation : NULL;
}
NmRegionDecision nm_census_check(const NmCensus *c,NmRegionCheck check,uint64_t target) {
    const NmCensusObservation *o=nm_census_observation(c);
    if (!o) return NM_REGION_UNAVAILABLE;
    bool enabled;
    switch (check) {
    case NM_REGION_ABSENT:enabled=o->possible_count==0;break;
    case NM_REGION_COUNT:enabled=o->possible_count==o->certain_count && o->certain_count==target;break;
    case NM_REGION_STABLE:enabled=o->possible_count==o->certain_count;break;
    default:return NM_REGION_UNAVAILABLE;
    }
    return enabled ? NM_REGION_ENABLED : NM_REGION_NOT_ENTAILED;
}
void nm_census_set_trace(NmCensus *c,NmCensusTrace trace,void *context) {
    if (c) { c->trace=trace;c->trace_context=context; }
}
