#include "run_report.h"
#include <assert.h>
#include <stdio.h>
static RunReport *begin(void) {
    RunReport *r=run_report_begin((RunReportDemand){RUN_REPORT_DEMAND_EXHAUSTIVE,0},1);
    assert(r); return r;
}
static void ready(RunReport *r) {
    assert(run_report_activity(r,RUN_REPORT_ACTIVITY_OUTPUT_COMPLETE,0));
    assert(run_report_activity(r,RUN_REPORT_ACTIVITY_CLEANUP_COMPLETE,0));
}
static unsigned status(RunReport *r) {
    RunReportOutcome o; assert(run_report_finalize(r,&o));
    unsigned code=o.exit_code; run_report_outcome_details_free(&o); return code;
}
int main(void) {
    RunReport *r=begin(); ready(r); assert(status(r)==1);
    assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
    assert(status(r)==0); assert(!run_report_answer(r)); assert(status(r)==1); run_report_free(r);
    r=begin(); ready(r); assert(run_report_unhandled_fault(r,1,2,3)); assert(status(r)==1);
    assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0)); assert(status(r)==2);
    assert(!run_report_terminal(r,RUN_REPORT_STOP_FAULT,2,3,4)); assert(status(r)==1); run_report_free(r);
    uint64_t ids[]={7,9};
    for (unsigned kind=0;kind<5;kind++) {
        r=begin(); assert(run_report_test_plan_declare(r,ids,2,false)==RUN_REPORT_PLAN_OK);
        assert(run_report_test_verdict(r,7,kind!=3));
        if (kind!=1) assert(run_report_test_verdict(r,9,true));
        if (kind==2) assert(run_report_test_verdict(r,7,true));
        if (kind==4) assert(run_report_test_verdict(r,10,true));
        ready(r); assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
        assert(status(r)==(kind==0 ? 0 : 1)); run_report_free(r);
    }
    for (unsigned count=0;count<4;count++) for (unsigned stop=0;stop<=RUN_REPORT_STOP_FAULT;stop++) {
        r=run_report_begin((RunReportDemand){RUN_REPORT_DEMAND_PREFIX,2},0); assert(r);
        for (unsigned i=0;i<count;i++) { assert(run_report_answer(r)); }
        ready(r); assert(run_report_terminal(r,(RunReportStopKind)stop,1,0,0));
        unsigned expected=stop==RUN_REPORT_STOP_FAULT ? 2 :
            ((stop==RUN_REPORT_STOP_OBSERVED_COMPLETE && count<=2) ||
             (stop==RUN_REPORT_STOP_DEMAND_SATISFIED && count==2)) ? 0 : 1;
        assert(status(r)==expected); run_report_free(r);
    }
    for(unsigned fresh=0;fresh<2;fresh++) {
        r=begin(); ready(r); assert(run_report_activity(r,RUN_REPORT_ACTIVITY_WORKER_STARTED,10));
        assert(run_report_activity(r,RUN_REPORT_ACTIVITY_WORKER_SETTLED,10));
        if(fresh) ready(r);
        assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
        assert(status(r)==(fresh ? 0 : 1)); run_report_free(r);
    }
    r=begin(); for(unsigned i=0;i<5;i++) { assert(run_report_unhandled_fault(r,1,0,0)); }
    ready(r); assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
    RunReportOutcome o; assert(run_report_finalize(r,&o));
    assert(o.exit_code==2 && o.diagnostics.total==5 && o.diagnostics.shown==1 && o.diagnostics.omitted==4);
    run_report_free(r); assert(o.diagnostics.details[0].occurrence==1); run_report_outcome_details_free(&o);
    RunReportDoc *d=run_report_doc_begin(); assert(d);
    assert(run_report_doc_declare_query(d,1)); assert(run_report_doc_declare_query(d,2));
    r=run_report_doc_open_query(d,1,(RunReportDemand){RUN_REPORT_DEMAND_EXHAUSTIVE,0},0); assert(r);
    ready(r); assert(run_report_terminal(r,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
    RunReport *b=run_report_doc_boundary(d); ready(b);
    assert(run_report_terminal(b,RUN_REPORT_STOP_OBSERVED_COMPLETE,0,0,0));
    RunReportDocStatus ds; assert(run_report_doc_verify(d,&ds)); assert(ds.exit_code==1 && ds.queries_unsettled==1);
    run_report_doc_free(d);
    puts("PASS: native run contracts preserve framing, exact tests, worker finalization, bounded diagnostics and declared query completion");
}
