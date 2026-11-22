#include "../native/kernel/observe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t reference(const CettaKernelObserveFacts *f) {
    const uint64_t *v=f->word;
    switch (v[NO_F_PHASE]) {
    case 0:
        if (v[NO_F_READY] || !v[NO_F_HAS_VARS]) return NO_A_SHARED;
        return v[NO_F_KIND]==NO_K_VARIABLE ? NO_A_VARIABLE : v[NO_F_KIND]==NO_K_EXPRESSION ? NO_A_EXPRESSION : NO_A_SHARED;
    case 1:
        return v[NO_F_MEMO] ? NO_A_MEMO : v[NO_F_SEEN] || !v[NO_F_BOUND] ? NO_A_UNBOUND : NO_A_FOLLOW;
    case 2:
        if (v[NO_F_VARIABLE_FRAME]) return NO_A_PUT_MEMO;
        if (v[NO_F_RESULT]) return !v[NO_F_DRAFT] && v[NO_F_CHANGED] ? NO_A_NEW_DRAFT : NO_A_WRITE_CHILD;
        return v[NO_F_NEXT]<v[NO_F_LENGTH] ? NO_A_NEXT_CHILD : NO_A_FINISH;
    default:return UINT64_MAX;
    }
}
int main(void) {
    char error[256];CettaKernelObserveProgram *p=cetta_kernel_observe_program(error,sizeof error);assert(p);
    size_t checks=0;
    for (uint64_t phase=0;phase<3;++phase) for (uint64_t kind=0;kind<5;++kind)
    for (uint64_t bits=0;bits<512;++bits) for (uint64_t order=0;order<3;++order) {
        CettaKernelObserveFacts f={0};f.word[NO_F_PHASE]=phase;f.word[NO_F_KIND]=kind;
        for (unsigned i=0;i<9;++i) f.word[NO_F_HAS_VARS+i]=(bits>>i)&1;
        f.word[NO_F_NEXT]=order ? UINT64_MAX : 0;f.word[NO_F_LENGTH]=order==1 ? UINT64_MAX : 1;
        CettaKernelObserveFacts before=f;uint64_t action=UINT64_MAX,fuel=UINT64_MAX;
        assert(cetta_kernel_observe_call(p,&f,100,4,&action,&fuel) && action==reference(&f) && fuel==5);
        assert(!memcmp(&f,&before,sizeof f));++checks;
    }
    CettaKernelObserveFacts f={0};uint64_t action=77,fuel=88;
    assert(!cetta_kernel_observe_call(p,&f,0,0,&action,&fuel) && action==77 && fuel==88);
    f.word[NO_F_PHASE]=99;
    assert(!cetta_kernel_observe_call(p,&f,10,0,&action,&fuel) && action==77 && fuel==88);
    cetta_kernel_observe_program_free(p);
    printf("native contextual observation: %zu x86 action comparisons against the independent C oracle; immutable inputs and failure controls passed\n",checks);
}
