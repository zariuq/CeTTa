#include "native_match_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t fail_at, allocation;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool fail(void) { return fail_at && ++allocation==fail_at; }
void *__wrap_malloc(size_t n) { return fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t width) { return fail() ? NULL : __real_calloc(n,width); }
void *__wrap_realloc(void *p,size_t n) { return fail() ? NULL : __real_realloc(p,n); }

static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    (void)context;
    if (value.syntax==0) *node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=65};
    else if (value.syntax<=65)
        *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={65-value.syntax,3}};
    else *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=7};
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context, NmHostValue value, uint64_t index, NmHostValue *out) {
    (void)context;(void)value;*out=(NmHostValue){index+1,9};return NM_VIEW_READY;
}
static NmViewStatus binding(void *context, NmHostValue variable, bool *bound, NmHostValue *out) {
    (void)context;*bound=true;*out=(NmHostValue){66,variable.context};return NM_VIEW_READY;
}
int main(void) {
    NmTermView view={NULL,inspect,child,binding};NmImportLimits limits={1000,1000,1000,1000};
    size_t failures=0;
    for (fail_at=1;;++fail_at) {
        allocation=0;NmPreparedMatch *p=NULL;
        NmViewStatus status=nm_match_prepare(&view,(NmHostValue){0,1},(NmHostValue){66,9},limits,&p,NULL);
        size_t calls=allocation;
        if (status==NM_VIEW_READY) {
            assert(p && calls<fail_at);nm_match_prepared_free(p);break;
        }
        if (status!=NM_VIEW_RESOURCE || p || calls<fail_at)
            fprintf(stderr,"injection %zu: status %u, calls %zu\n",fail_at,(unsigned)status,calls);
        assert(status==NM_VIEW_RESOURCE && !p && calls>=fail_at);
        ++failures;
        size_t injection=fail_at;fail_at=0;
        assert(nm_match_prepare(&view,(NmHostValue){0,1},(NmHostValue){66,9},limits,&p,NULL)==NM_VIEW_READY);
        assert(p);nm_match_prepared_free(p);fail_at=injection;
    }
    printf("host-view allocation: %zu individual failures refused and recovered\n",failures);
    return 0;
}
