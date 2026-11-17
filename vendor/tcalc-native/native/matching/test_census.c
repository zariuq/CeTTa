#include "native_census.h"
#include <assert.h>
#include <stdio.h>

typedef struct { bool pinned,defer,interrupted;size_t reads; } Source;
typedef struct { size_t validation,scan;Source *stop;bool invalidate; } Trace;
static size_t scans,advances;
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    Source *s=context;++s->reads;
    if (s->defer) return NM_VIEW_DEFER;
    if (s->interrupted) return NM_VIEW_INTERRUPTED;
    *node=value.syntax<10 ? (NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}} :
        (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};
    return NM_VIEW_READY;
}
static bool valid(void *context) { return ((Source *)context)->pinned; }
static NmQueryLimits limits(void) { return (NmQueryLimits){.import={100,100,16,1000},.pairs=32,.scan=32,.memo=128}; }
static NmQueryView source_view(Source *s) { return (NmQueryView){.terms={s,inspect,NULL,NULL},.pin=s,.valid=valid}; }
static void trace(void *context,const NmCensusEvent *event) {
    Trace *t=context;
    if (event->query.kind!=NM_QUERY_CORE_RESULT) return;
    if (event->validation) ++t->validation;else ++t->scan;
    if (t->stop && !event->validation) {
        if (t->invalidate) t->stop->pinned=false;else t->stop->interrupted=true;
    }
}
static NmCensus *open_scan(const NmUnifyProgram *program,NmExecution mode,NmQueryView view,
                            NmHostValue pattern,const NmQueryRow *rows,size_t count,uint64_t budget) {
    char error[256];NmCensus *c=NULL;++scans;
    assert(nm_census_open(program,mode,view,pattern,rows,count,limits(),budget,0,&c,error,sizeof error)==NM_CENSUS_YIELD);
    return c;
}
static NmCensusStatus next(NmCensus *c,uint64_t quantum) {
    char error[256];++advances;return nm_census_next(c,quantum,error,sizeof error);
}
static void complete(NmCensus *c) {
    size_t steps=0;NmCensusStatus status;
    do {
        status=next(c,1);assert(++steps<100);
        if (status==NM_CENSUS_YIELD) {
            assert(!nm_census_observation(c));
            assert(nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);
        }
    } while (status==NM_CENSUS_YIELD);
    assert(status==NM_CENSUS_DONE);
}
static NmSolution *tell(const NmUnifyProgram *program,NmExecution mode,NmQueryView view,NmHostValue a,NmHostValue b) {
    NmQueryRow row={0,b};NmQuery *q=NULL;NmQueryAnswer answer;NmSolution *solution=NULL;char error[256];
    assert(nm_query_open(program,mode,view,&a,1,0,&row,1,limits(),10000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
    assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_ANSWER);
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&solution)==NM_VIEW_READY);
    nm_query_free(q);return solution;
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    for (unsigned mode=0;mode<3;++mode) {
        Source s={.pinned=true};NmQueryView view=source_view(&s);
        NmHostValue a={10,77},b={11,90},x={1,9};
        NmQueryRow rows[]={{4,a},{9,b},{13,a}};
        NmCensus *c=open_scan(program,(NmExecution)mode,view,a,rows,3,100000);
        assert(next(c,0)==NM_CENSUS_YIELD && !nm_census_observation(c));
        Trace t={0};nm_census_set_trace(c,trace,&t);complete(c);
        const NmCensusObservation *o=nm_census_observation(c);
        assert(o && o->certain_count==2 && o->possible_count==2 && o->certain[0]==4 && o->certain[1]==13);
        assert(nm_census_check(c,NM_REGION_COUNT,2)==NM_REGION_ENABLED);
        assert(nm_census_check(c,NM_REGION_COUNT,1)==NM_REGION_NOT_ENTAILED);
        assert(nm_census_check(c,NM_REGION_STABLE,0)==NM_REGION_ENABLED);
        assert(nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_NOT_ENTAILED);
        NmCensusReceipt r=nm_census_receipt(c);
        assert(r.validated && r.validation.unifier_calls==1 && r.scan.unifier_calls==3 &&
            r.validation.fuel_used<r.fuel_used && r.fuel_used==r.scan.fuel_used && t.validation==1 && t.scan==3);
        s.pinned=false;assert(!nm_census_observation(c) && nm_census_check(c,NM_REGION_COUNT,2)==NM_REGION_UNAVAILABLE);
        s.pinned=true;nm_census_free(c);
        for (uint64_t missing=0;missing<=1;++missing) {
            c=open_scan(program,(NmExecution)mode,view,a,rows,3,r.fuel_used-missing);
            NmCensusStatus status=next(c,100);
            assert(status==(missing ? NM_CENSUS_EXHAUSTED : NM_CENSUS_DONE));
            assert(nm_census_receipt(c).fuel_used==r.fuel_used-missing);
            assert(nm_census_check(c,NM_REGION_COUNT,2)==(missing ? NM_REGION_UNAVAILABLE : NM_REGION_ENABLED));
            nm_census_free(c);
        }
        rows[2].value=x;
        c=open_scan(program,(NmExecution)mode,view,a,rows,3,100000);complete(c);o=nm_census_observation(c);
        assert(o->certain_count==1 && o->possible_count==2);
        assert(nm_census_check(c,NM_REGION_COUNT,1)==NM_REGION_NOT_ENTAILED &&
            nm_census_check(c,NM_REGION_STABLE,0)==NM_REGION_NOT_ENTAILED);
        nm_census_free(c);
        c=open_scan(program,(NmExecution)mode,view,x,rows,3,100000);complete(c);o=nm_census_observation(c);
        assert(o->certain_count==1 && o->possible_count==3);nm_census_free(c);

        NmSolution *solution=tell(program,(NmExecution)mode,view,x,a);
        NmQueryView refined={nm_solution_view(solution),&s,valid};
        c=open_scan(program,(NmExecution)mode,refined,a,rows,3,100000);complete(c);o=nm_census_observation(c);
        assert(o->certain_count==2 && o->possible_count==2 && o->certain[1]==13 &&
            nm_census_check(c,NM_REGION_COUNT,2)==NM_REGION_ENABLED);nm_census_free(c);nm_solution_free(solution);
        c=open_scan(program,(NmExecution)mode,view,a,rows,3,100000);complete(c);
        assert(nm_census_check(c,NM_REGION_COUNT,2)==NM_REGION_NOT_ENTAILED);nm_census_free(c);

        c=open_scan(program,(NmExecution)mode,view,a,rows+1,1,100000);complete(c);
        assert(nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_ENABLED);nm_census_free(c);
        c=open_scan(program,(NmExecution)mode,view,x,NULL,0,100000);complete(c);
        assert(nm_census_receipt(c).validated && nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_ENABLED);nm_census_free(c);
        c=open_scan(program,(NmExecution)mode,view,x,NULL,0,0);
        assert(next(c,1)==NM_CENSUS_EXHAUSTED && nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);
        nm_census_free(c);
        s.defer=true;c=open_scan(program,(NmExecution)mode,view,x,NULL,0,100000);
        assert(next(c,1)==NM_CENSUS_DEFER && nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);
        nm_census_free(c);s.defer=false;
        c=open_scan(program,(NmExecution)mode,view,a,rows,3,100000);
        assert(next(c,1)==NM_CENSUS_YIELD && nm_census_receipt(c).validated);
        assert(next(c,1)==NM_CENSUS_YIELD && nm_census_receipt(c).certain_count==1);
        s.interrupted=true;assert(next(c,1)==NM_CENSUS_INTERRUPTED && !nm_census_observation(c));
        assert(nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);s.interrupted=false;nm_census_free(c);
        c=open_scan(program,(NmExecution)mode,view,a,rows,3,100000);
        t=(Trace){.stop=&s,.invalidate=true};nm_census_set_trace(c,trace,&t);
        assert(next(c,2)==NM_CENSUS_INVALIDATED && !nm_census_observation(c));s.pinned=true;nm_census_free(c);
        NmQueryRow duplicate[]={{1,a},{1,b}};
        c=open_scan(program,(NmExecution)mode,view,a,duplicate,2,100000);
        assert(next(c,1)==NM_CENSUS_FAULT && nm_census_check(c,NM_REGION_COUNT,0)==NM_REGION_UNAVAILABLE);
        nm_census_free(c);
    }
    nm_census_free(NULL);nm_census_set_trace(NULL,NULL,NULL);
    assert(nm_census_check(NULL,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);
    nm_unify_program_close(program);
    printf("region census: %zu scans, %zu advances; certain/possible multiplicities, retained contexts, validation, incomplete guards and source faults passed\n",scans,advances);
    return 0;
}
