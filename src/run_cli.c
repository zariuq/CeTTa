#include "run_cli.h"
#include "run_guard.h"
#include "error_presentation.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct RunCli {
    RunReportDoc *document;
    RunReport *query;
    uint64_t declared, opened, test_id;
    RunReportStopKind stop;
    bool failed, fault_detail_noted;
    uint32_t fault_code;
    struct { uint64_t query_id; CettaErrorPresentation detail; } errors[4];
    size_t errors_len;
    pthread_mutex_t mutex;
};
RunCli *run_cli_begin(void) {
    RunCli *c=calloc(1,sizeof(*c));
    if (!c) return NULL;
    c->document=run_report_doc_begin();
    if (!c->document || pthread_mutex_init(&c->mutex,NULL)) {
        run_report_doc_free(c->document); free(c); return NULL;
    }
    return c;
}
void run_cli_free(RunCli *c) {
    if (!c) return;
    run_report_doc_free(c->document); pthread_mutex_destroy(&c->mutex); free(c);
}
bool run_cli_declare_queries(RunCli *c,uint64_t count) {
    if (!c) return true;
    for (uint64_t i=0;i<count;i++) {
        if (c->declared==UINT64_MAX ||
            !run_report_doc_declare_query(c->document,++c->declared)) {
            c->failed=true; return false;
        }
    }
    return true;
}
bool run_cli_query_begin(RunCli *c) {
    if (!c) return true;
    if (c->query || c->opened==UINT64_MAX) { c->failed=true; return false; }
    cetta_run_guard_declare(c->declared);
    c->query=run_report_doc_open_query(c->document,++c->opened,
        (RunReportDemand){RUN_REPORT_DEMAND_EXHAUSTIVE,0},4);
    c->stop=RUN_REPORT_STOP_OBSERVED_COMPLETE; c->test_id=0;
    c->fault_code=CETTA_DIAGNOSTIC_EXECUTION; c->fault_detail_noted=false;
    if (!c->query) c->failed=true;
    return c->query!=NULL;
}
void run_cli_test_verdict(void *context,bool passed) {
    RunCli *c=context;
    if (!c) return;
    pthread_mutex_lock(&c->mutex);
    if (!c->query || c->test_id==UINT64_MAX ||
        !run_report_test_verdict(c->query,++c->test_id,passed)) c->failed=true;
    pthread_mutex_unlock(&c->mutex);
}
void run_cli_answers(RunCli *c,uint64_t count) {
    if (!c) return;
    if (!c->query) { c->failed=true; return; }
    for (uint64_t i=0;i<count;i++) if (!run_report_answer(c->query)) c->failed=true;
}
void run_cli_query_stop(RunCli *c,RunReportStopKind stop) {
    if (!c) return;
    c->stop=stop;
}
void run_cli_query_fault(RunCli *c,const CettaErrorPresentation *detail) {
    if (!c) return;
    if (!c->query || !detail || c->fault_detail_noted) { c->failed=true; return; }
    c->fault_detail_noted=true; c->stop=RUN_REPORT_STOP_FAULT;
    c->fault_code=(uint32_t)detail->code;
    if (c->errors_len < sizeof(c->errors)/sizeof(c->errors[0])) {
        c->errors[c->errors_len].query_id=c->opened;
        c->errors[c->errors_len++].detail=*detail;
    }
}
void run_cli_query_end(RunCli *c,bool output_ok,bool cleanup_ok) {
    if (!c || !c->query) return;
    c->failed |= !run_report_activity(c->query,output_ok
        ? RUN_REPORT_ACTIVITY_OUTPUT_COMPLETE : RUN_REPORT_ACTIVITY_OUTPUT_FAILED,0);
    c->failed |= !run_report_activity(c->query,cleanup_ok
        ? RUN_REPORT_ACTIVITY_CLEANUP_COMPLETE : RUN_REPORT_ACTIVITY_CLEANUP_FAILED,0);
    c->failed |= !run_report_terminal(c->query,c->stop,c->fault_code,c->opened,0);
    c->query=NULL;
}
static const char *boolean(bool b) { return b ? "true" : "false"; }
static bool json_text(FILE *f,const char *text) {
    if (fputc('"',f)==EOF) return false;
    for (const unsigned char *p=(const unsigned char *)text; *p; p++) {
        if (*p=='"' || *p=='\\') {
            if (fputc('\\',f)==EOF || fputc(*p,f)==EOF) return false;
        } else if (*p < 0x20u) {
            if (fprintf(f,"\\u%04x",*p)<0) return false;
        } else if (fputc(*p,f)==EOF) return false;
    }
    return fputc('"',f)!=EOF;
}
static bool publish(RunCli *c,const char *path,const RunReportDocStatus *d,
                    int dialect_exit) {
    if (!path) return true;
    FILE *f=fopen(path,"w");
    if (!f) return false;
    bool ok=fprintf(f,"{\"version\":1,\"dialect_exit\":%d,\"contract_exit\":%u,"
        "\"report_publication_acknowledged\":false,\"queries_declared\":%"PRIu64","
        "\"queries_unsettled\":%"PRIu64",\"queries\":[",dialect_exit,d->exit_code,
        d->queries_declared,d->queries_unsettled)>=0;
    for (uint64_t i=0;ok && i<d->queries_declared;i++) {
        RunReportOutcome q={0}; uint64_t id=0;
        ok=run_report_doc_query_at(c->document,(size_t)i,&id,&q);
        if (ok) ok=fprintf(f,"%s{\"id\":%"PRIu64",\"status\":%u,"
            "\"observation_complete\":%s,\"tests_passed\":%s,"
            "\"finalization_ready\":%s,\"framing_ok\":%s,"
            "\"answers\":%"PRIu64",\"tests\":%"PRIu64","
            "\"faults\":%"PRIu64",\"details_shown\":%"PRIu64","
            "\"details_omitted\":%"PRIu64"}",i ? "," : "",id,q.exit_code,
            boolean(q.observation_complete),boolean(q.tests_passed),
            boolean(q.finalization_ready),boolean(q.framing_ok),q.answer_count,
            q.test_count,q.diagnostics.total,q.diagnostics.shown,q.diagnostics.omitted)>=0;
        run_report_outcome_details_free(&q);
    }
    if (ok) ok=fputs("],\"diagnostics\":[",f)!=EOF;
    for (size_t i=0;ok && i<c->errors_len;i++) {
        CettaErrorPresentation *e=&c->errors[i].detail;
        ok=fprintf(f,"%s{\"query\":%"PRIu64",\"code\":%u,\"classification\":",
            i ? "," : "",c->errors[i].query_id,(unsigned)e->code)>=0;
        if (ok) ok=json_text(f,e->classification);
        if (ok) ok=fputs(",\"message\":",f)!=EOF && json_text(f,e->message);
        if (ok) ok=fprintf(f,",\"details_hidden\":%s,\"truncated\":%s,\"formatter_failed\":%s}",
            boolean(e->details_hidden),boolean(e->truncated),boolean(e->formatter_failed))>=0;
    }
    if (ok) ok=fputs("]}\n",f)!=EOF;
    if (fflush(f)) ok=false;
    if (fclose(f)) ok=false;
    return ok;
}
int run_cli_finish(RunCli *c,int dialect_exit,bool output_ok,
                   bool cleanup_ok,const char *path,bool strict) {
    if (!c) return dialect_exit;
    if (c->query) {
        if (c->stop==RUN_REPORT_STOP_OBSERVED_COMPLETE)
            c->stop=dialect_exit==2 ? RUN_REPORT_STOP_FAULT : RUN_REPORT_STOP_OBSERVED_INTERRUPTED;
        run_cli_query_end(c,false,cleanup_ok);
    }
    RunReport *b=run_report_doc_boundary(c->document);
    run_report_activity(b,output_ok ? RUN_REPORT_ACTIVITY_OUTPUT_COMPLETE
        : RUN_REPORT_ACTIVITY_OUTPUT_FAILED,0);
    run_report_activity(b,cleanup_ok ? RUN_REPORT_ACTIVITY_CLEANUP_COMPLETE
        : RUN_REPORT_ACTIVITY_CLEANUP_FAILED,0);
    run_report_terminal(b,c->failed || dialect_exit==1
        ? RUN_REPORT_STOP_OBSERVED_INTERRUPTED : dialect_exit==2
        ? RUN_REPORT_STOP_FAULT : RUN_REPORT_STOP_OBSERVED_COMPLETE,1,0,0);
    RunReportDocStatus d={0};
    if (!run_report_doc_verify(c->document,&d)) return 1;
    bool human_ok=fprintf(stderr,"run: status %u; queries %"PRIu64"; unsettled %"PRIu64"\n",
        d.exit_code,d.queries_declared,d.queries_unsettled)>=0;
    human_ok &= fflush(stderr)==0 && !ferror(stderr);
    bool sent=publish(c,path,&d,dialect_exit);
    if (!sent) { fputs("error: could not publish run report\n",stderr); return 1; }
    if (!human_ok) return 1;
    return strict ? (int)d.exit_code : dialect_exit;
}
