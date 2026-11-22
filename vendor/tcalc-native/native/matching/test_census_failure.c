#include "native_census.h"
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
        (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};return NM_VIEW_READY;
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    NmQueryView view={.terms={NULL,inspect,NULL,NULL}};
    NmQueryLimits limits={.import={100,100,16,1000},.pairs=32,.scan=32,.memo=128};
    NmHostValue pattern={1,9};const NmQueryRow rows[]={{0,{10,77}},{1,{11,90}},{2,{1,9}}};
    size_t failures=0;
    for (unsigned mode=0;mode<3;++mode) for (fail_at=1;;++fail_at) {
        allocation=0;NmCensus *c=NULL;
        NmCensusStatus status=nm_census_open(program,(NmExecution)mode,view,pattern,rows,3,limits,100000,0,
            &c,error,sizeof error);
        if (status==NM_CENSUS_YIELD) {
            assert(c);size_t steps=0;
            do { status=nm_census_next(c,1,error,sizeof error);assert(++steps<=5); }
            while (status==NM_CENSUS_YIELD);
        } else assert(!c);
        size_t calls=allocation;
        if (status==NM_CENSUS_DONE) {
            assert(calls<fail_at && nm_census_observation(c));nm_census_free(c);break;
        }
        if (status!=NM_CENSUS_RESOURCE)
            fprintf(stderr,"mode %u allocation %zu: status %u, calls %zu: %s\n",mode,fail_at,(unsigned)status,calls,error);
        assert(status==NM_CENSUS_RESOURCE && calls>=fail_at);
        if (c) {
            assert(!nm_census_observation(c) && nm_census_check(c,NM_REGION_ABSENT,0)==NM_REGION_UNAVAILABLE);
            assert(nm_census_receipt(c).fuel_used<=100000);
            assert(nm_census_next(c,1,error,sizeof error)==NM_CENSUS_RESOURCE);
        }
        nm_census_free(c);++failures;
    }
    fail_at=0;nm_unify_program_close(program);
    printf("region census allocation: %zu injected failures preserve RESOURCE and cannot establish absence or a stable region\n",failures);
    return 0;
}
