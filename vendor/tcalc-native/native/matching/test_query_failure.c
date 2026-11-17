#include "native_query.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t fail_at, allocation;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool fail(void) { return fail_at && ++allocation==fail_at; }
void *__wrap_malloc(size_t n) { return fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t w) { return fail() ? NULL : __real_calloc(n,w); }
void *__wrap_realloc(void *p,size_t n) { return fail() ? NULL : __real_realloc(p,n); }

static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    (void)context;
    *node=value.syntax==1 ? (NmViewNode){.kind=NM_NODE_VARIABLE,.variable={42,value.context}} :
        (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=7};
    return NM_VIEW_READY;
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    size_t failures=0;
    for (unsigned mode=0;mode<3;++mode) for (fail_at=1;;++fail_at) {
        NmQueryView view={.terms={NULL,inspect,NULL,NULL}};
        NmQueryLimits limits={.import={10,10,10,100},.pairs=16,.scan=16,.memo=16};
        NmHostValue pattern={1,93};NmQueryRow row={14,{2,71}};NmQuery *q=NULL;allocation=0;
        NmQueryStatus status=nm_query_open(program,(NmExecution)mode,view,&pattern,1,0,&row,1,
            limits,10000,0,&q,error,sizeof error);
        NmQueryAnswer answer;
        if (status==NM_QUERY_YIELD) { assert(q);status=nm_query_next(q,1,&answer,error,sizeof error); }
        else assert(!q);
        size_t calls=allocation;
        if (status==NM_QUERY_ANSWER) {
            assert(calls<fail_at && answer.result.outcome==NM_UNIFIED);nm_query_free(q);break;
        }
        if (status!=NM_QUERY_RESOURCE)
            fprintf(stderr,"mode %u allocation %zu: status %u, calls %zu: %s\n",mode,fail_at,(unsigned)status,calls,error);
        assert(status==NM_QUERY_RESOURCE && calls>=fail_at);
        if (q) assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_RESOURCE && !answer.prepared);
        nm_query_free(q);++failures;
    }
    fail_at=0;
    NmQueryView view={.terms={NULL,inspect,NULL,NULL}};
    NmQueryLimits limits={.import={10,10,10,100},.pairs=16,.scan=16,.memo=16};
    NmHostValue pattern={1,93};NmQueryRow row={14,{2,71}};NmQuery *q=NULL;NmQueryAnswer answer;
    assert(nm_query_open(program,NM_NATIVE,view,&pattern,1,0,&row,1,limits,10000,0,
        &q,error,sizeof error)==NM_QUERY_YIELD);
    assert(nm_query_next(q,1,&answer,error,sizeof error)==NM_QUERY_ANSWER);
    fail_at=1;allocation=0;NmSolution *solution=NULL;
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&solution)==NM_VIEW_RESOURCE && !solution);
    fail_at=0;
    assert(nm_solution_capture(answer.prepared,&answer.result,answer.work,&solution)==NM_VIEW_READY && solution);
    nm_query_free(q);nm_solution_free(solution);
    nm_unify_program_close(program);
    printf("native query allocation: %zu injected failures preserve RESOURCE and release partial sessions across all modes; snapshot allocation recovers\n",failures);
    return 0;
}
