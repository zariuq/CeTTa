#include "native_match.h"
#include "tisa_x86.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool flip;
static size_t fail_at, allocation;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {
    return fail_at && ++allocation==fail_at ? NULL : __real_malloc(n);
}
TcIsaOutcome __real_tisa_x86_run(const TcIsaProgram *,const TcIsaX86 *,TcIsaRuntimeFrame *,uint64_t *);
TcIsaOutcome __wrap_tisa_x86_run(const TcIsaProgram *p,const TcIsaX86 *code,
                                TcIsaRuntimeFrame *frame,uint64_t *regs) {
    TcIsaOutcome result=__real_tisa_x86_run(p,code,frame,regs);
    if (flip) frame->ret_val^=1;
    return result;
}
typedef struct {
    uint64_t bindings[1];NmNodePair pairs[16];uint64_t scan[16],state[40],marks[18];
} Storage;
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    const NmNode nodes[]={{0,0,0,0},{0,7,0,0}};
    NmUnifyInput input={.nodes=nodes,.nodes_count=2,.left=1,.right=1};
    Storage storage;memset(&storage,0x69,sizeof storage);Storage before=storage;
    NmUnifyWork work={storage.bindings,1,storage.pairs,16,storage.scan,16,storage.state,40,storage.marks,18};
    NmUnifyResult result;
    assert(nm_unify_call(NULL,NM_NATIVE,&input,&work,10000,0,&result,error,sizeof error)==NM_CALL_BAD_ARGUMENT);
    size_t failures=0;
    for (fail_at=1;;++fail_at) {
        allocation=0;
        NmCallStatus status=nm_unify_call(program,NM_COMPARE,&input,&work,10000,0,&result,error,sizeof error);
        if (status==NM_CALL_OK) { assert(allocation<fail_at && result.outcome==NM_UNIFIED);break; }
        assert(status==NM_CALL_RESOURCE && !memcmp(&storage,&before,sizeof storage));++failures;
    }
    fail_at=0;storage=before;flip=true;
    assert(nm_unify_call(program,NM_COMPARE,&input,&work,10000,0,&result,error,sizeof error)==NM_CALL_DIFFERENCE);
    assert(nm_constraint_truth(NM_CALL_DIFFERENCE,&result)==NM_TRUTH_UNAVAILABLE);
    assert(!memcmp(&storage,&before,sizeof storage));flip=false;
    assert(nm_unify_call(program,NM_NATIVE,&input,&work,0,0,&result,error,sizeof error)==NM_CALL_OK);
    assert(result.outcome==NM_EXHAUSTED);
    assert(nm_constraint_truth(NM_CALL_OK,&result)==NM_TRUTH_UNAVAILABLE);
    assert(nm_constraint_truth(NM_CALL_RESOURCE,NULL)==NM_TRUTH_UNAVAILABLE);
    nm_unify_program_close(program);
    printf("native call causes: %zu comparison-allocation failures; injected result disagreement refuses publication; declared exhaustion stays a machine outcome\n",failures);
    return 0;
}
