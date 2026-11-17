#include "native_admission.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t fail_at,allocation;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool fail(void) { return fail_at && ++allocation==fail_at; }
void *__wrap_malloc(size_t n) { return fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t w) { return fail() ? NULL : __real_calloc(n,w); }
void *__wrap_realloc(void *p,size_t n) { return fail() ? NULL : __real_realloc(p,n); }
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    (void)context;
    *node=value.syntax<10 ? (NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}} :
        (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};
    return NM_VIEW_READY;
}
static NmQueryLimits limits(void) {
    return (NmQueryLimits){.import={100,100,16,1000},.pairs=32,.scan=32,.memo=128};
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    NmQueryView view={.terms={NULL,inspect,NULL,NULL}};NmQuery *q=NULL;NmQueryAnswer answer;NmSolution *start=NULL;
    assert(nm_query_open(program,NM_NATIVE,view,NULL,0,0,NULL,0,limits(),10000,0,&q,error,sizeof error)==NM_QUERY_YIELD);
    assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_ANSWER);
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&start)==NM_VIEW_READY);
    nm_query_free(q);
    NmHostConstraint tells[]={{{1,9},{10,77}},{{2,9},{1,9}}};
    NmHostConstraint guards[]={{{1,9},{10,77}},{{2,9},{10,77}}};
    size_t failures=0;
    for (unsigned mode=0;mode<3;++mode) for (fail_at=1;;++fail_at) {
        allocation=0;NmAdmission *admission=NULL;
        NmAdmissionStatus status=nm_admission_open(program,(NmExecution)mode,start,NM_ASK_AFTER_TELLS,
            tells,2,guards,2,limits(),NULL,NULL,100000,0,&admission,error,sizeof error);
        if (status==NM_ADMISSION_YIELD) {
            assert(admission);
            size_t steps=0;
            do { status=nm_admission_next(admission,1,error,sizeof error);assert(++steps<=4); }
            while (status==NM_ADMISSION_YIELD);
        } else assert(!admission);
        size_t calls=allocation;
        if (status==NM_ADMITTED) {
            assert(calls<fail_at && nm_admission_result(admission));nm_admission_free(admission);break;
        }
        if (status!=NM_ADMISSION_RESOURCE)
            fprintf(stderr,"mode %u allocation %zu: status %u, calls %zu: %s\n",mode,fail_at,(unsigned)status,calls,error);
        assert(status==NM_ADMISSION_RESOURCE && calls>=fail_at);
        if (admission) {
            assert(!nm_admission_result(admission));
            assert(nm_admission_receipt(admission).fuel_used<=100000);
            assert(nm_admission_next(admission,1,error,sizeof error)==NM_ADMISSION_RESOURCE);
        }
        nm_admission_free(admission);++failures;
        assert(!nm_solution_entries(start) && nm_solution_depth(start)==1);
    }
    fail_at=0;nm_solution_free(start);nm_unify_program_close(program);
    printf("equational admission allocation: %zu injected failures retain RESOURCE, prefix/fuel accounting and input context across all modes\n",failures);
    return 0;
}
