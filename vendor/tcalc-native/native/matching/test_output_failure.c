#include "native_output.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t fail_at,allocation;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
static bool fail(void) { return fail_at && ++allocation==fail_at; }
void *__wrap_malloc(size_t n) { return fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t w) { return fail() ? NULL : __real_calloc(n,w); }
void *__wrap_realloc(void *p,size_t n) { return fail() ? NULL : __real_realloc(p,n); }
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    (void)context;*node=value.syntax<10 ? (NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}} :
        (NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};return NM_VIEW_READY;
}
static NmViewStatus binding(void *context,NmHostValue value,bool *bound,NmHostValue *out) {
    (void)context;(void)value;*bound=true;*out=(NmHostValue){11,77};return NM_VIEW_READY;
}
int main(void) {
    char error[256];NmEmitProgram *program=nm_emit_program_default(true,error,sizeof error);assert(program);
    NmQueryView view={.terms={NULL,inspect,NULL,binding}};
    NmHostValue roots[]={{1,9},{1,9},{10,77}};
    NmOutputLimits limits={.import={100,100,16,1000},.nodes=100,.children=100,.stack=100};
    size_t failures=0;
    for (unsigned mode=0;mode<3;++mode) for (fail_at=1;;++fail_at) {
        allocation=0;NmOutput *o=NULL;NmOutputReceipt r;
        NmOutputStatus status=nm_output_build(program,(NmExecution)mode,view,roots,3,limits,100000,0,&o,&r,error,sizeof error);
        size_t calls=allocation;
        if (status==NM_OUTPUT_READY) {
            assert(calls<fail_at && o && nm_output_graph(o)->roots_count==3);nm_output_free(o);break;
        }
        if (status!=NM_OUTPUT_RESOURCE) fprintf(stderr,"mode %u allocation %zu: status %u calls %zu: %s\n",mode,fail_at,status,calls,error);
        assert(status==NM_OUTPUT_RESOURCE && !o && calls>=fail_at && r.fuel_used<=100000);++failures;
    }
    fail_at=0;nm_emit_program_close(program);
    printf("native output allocation: %zu injected failures leave no published graph and retain RESOURCE\n",failures);
    return 0;
}
