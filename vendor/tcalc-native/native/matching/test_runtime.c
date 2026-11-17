#include "native_match.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { PAIRS=64, SCANS=128, MARKS=256, STATE=40 };
typedef struct {
    uint64_t before, bindings[8], after_bindings;
    NmNodePair pairs[PAIRS]; uint64_t after_pairs;
    uint64_t scans[SCANS], after_scans;
    uint64_t state[STATE], after_state;
    uint64_t marks[MARKS], after;
} Storage;

static void storage_reset(Storage *s) { memset(s,0x5A,sizeof *s); }
static NmUnifyWork storage_view(Storage *s, size_t variables, size_t nodes) {
    NmUnifyWork work={s->bindings,8,s->pairs,PAIRS,s->scans,SCANS,s->state,STATE,
        s->marks,variables+nodes+128};
    return work;
}
static void guards(const Storage *s) {
    const uint64_t guard=UINT64_C(0x5A5A5A5A5A5A5A5A);
    assert(s->before==guard && s->after_bindings==guard && s->after_pairs==guard &&
        s->after_scans==guard && s->after_state==guard && s->after==guard);
}
static bool result_equal(NmUnifyResult a, NmUnifyResult b) {
    return a.outcome==b.outcome && a.payload==b.payload && a.fuel_used==b.fuel_used;
}

int main(int argc, char **argv) {
    assert(argc==2);
    char error[256];
    NmUnifyProgram *program=nm_unify_program_open(argv[1],true,error,sizeof error);
    if (!program) { fprintf(stderr,"%s\n",error); return 1; }
    NmUnifyProgram *packaged=nm_unify_program_default(true,error,sizeof error);
    if (!packaged) { fprintf(stderr,"%s\n",error); return 1; }
    assert(nm_unify_state_words(program)==STATE);
    size_t bytes=0;
    assert(nm_unify_native_bytes(program,&bytes) && bytes>0);
    NmUnifyProgram *reference=nm_unify_program_open(argv[1],false,error,sizeof error);
    assert(reference && !nm_unify_native_bytes(reference,NULL));
    assert(!nm_unify_program_open(NULL,true,error,sizeof error));

    const NmNode nodes[]={{0,0,0,0},{1,0,42,7},{1,1,42,8},{0,10,0,0},
        {2,0,2,0},{2,2,2,0},{1,0,42,7},{2,4,1,0}};
    const uint64_t children[]={1,2,3,3,1};
    const NmVariableKey keys[]={{42,7},{42,8}};
    uint64_t initial[]={0,0};
    NmUnifyInput input={nodes,8,children,5,initial,2,keys,2,4,5};
    unsigned calls=0;
    for (unsigned trial=0;trial<64;++trial) {
        Storage expected;
        storage_reset(&expected);
        NmUnifyWork ew=storage_view(&expected,2,8);
        NmUnifyResult er;
        assert(nm_unify_run(reference,NM_REFERENCE,&input,&ew,10000,trial,&er,error,sizeof error));
        assert(er.outcome==NM_UNIFIED && er.payload==2 && expected.bindings[0]==3 && expected.bindings[1]==3);
        guards(&expected);
        for (unsigned mode=0;mode<3;++mode) {
            Storage actual;storage_reset(&actual);
            NmUnifyWork work=storage_view(&actual,2,8);
            NmUnifyResult result;
            assert(nm_unify_run(trial%2 ? program : packaged,(NmExecution)mode,&input,&work,
                10000,trial,&result,error,sizeof error));
            assert(result_equal(result,er) && !memcmp(&actual,&expected,sizeof actual));
            assert(initial[0]==0 && initial[1]==0);
            guards(&actual);
            ++calls;
        }
    }
    const struct { uint64_t left,right;NmOutcome outcome;uint64_t payload; } cases[]={
        {1,7,NM_NO_MATCH,0},{1,6,NM_UNIFIED,0},{0,3,NM_UNSUPPORTED,2}};
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i) {
        input.left=cases[i].left;input.right=cases[i].right;
        for (unsigned mode=0;mode<3;++mode) {
            Storage data;storage_reset(&data);
            NmUnifyWork work=storage_view(&data,2,8);NmUnifyResult result;
            assert(nm_unify_run(program,(NmExecution)mode,&input,&work,10000,0,&result,error,sizeof error));
            assert(result.outcome==cases[i].outcome && result.payload==cases[i].payload);
            assert(initial[0]==0 && initial[1]==0);guards(&data);++calls;
        }
    }
    input.left=4;input.right=5;
    for (unsigned mode=0;mode<3;++mode) {
        Storage data;storage_reset(&data);
        NmUnifyWork work=storage_view(&data,2,8);NmUnifyResult result;
        assert(nm_unify_run(program,(NmExecution)mode,&input,&work,0,0,&result,error,sizeof error));
        assert(result.outcome==NM_EXHAUSTED && result.fuel_used==0);
        guards(&data);++calls;
        work.state_words=STATE-1;
        Storage saved=data;
        assert(nm_unify_run(program,(NmExecution)mode,&input,&work,10000,0,&result,error,sizeof error));
        assert(result.outcome==NM_INVALID && !memcmp(&saved,&data,sizeof data));++calls;
        work.state_words=STATE;work.bindings_words=0;
        assert(nm_unify_run(program,(NmExecution)mode,&input,&work,10000,0,&result,error,sizeof error));
        assert(result.outcome==NM_UNSUPPORTED && result.payload==6);++calls;
        work.bindings=initial;work.bindings_words=2;
        assert(nm_unify_run(program,(NmExecution)mode,&input,&work,10000,0,&result,error,sizeof error));
        assert(result.outcome==NM_INVALID && initial[0]==0 && initial[1]==0);++calls;
    }
    Storage data;storage_reset(&data);Storage saved=data;
    NmUnifyWork work=storage_view(&data,2,8);NmUnifyResult result;
    assert(!nm_unify_run(reference,NM_NATIVE,&input,&work,10000,0,&result,error,sizeof error));
    assert(!memcmp(&saved,&data,sizeof data));
    assert(!nm_unify_run(program,(NmExecution)99,&input,&work,10000,0,&result,error,sizeof error));
    NmUnifyInput bad=input;bad.nodes_count=SIZE_MAX;
    assert(!nm_unify_run(program,NM_NATIVE,&bad,&work,10000,0,&result,error,sizeof error));
    bad=input;bad.initial=NULL;
    assert(!nm_unify_run(program,NM_COMPARE,&bad,&work,10000,0,&result,error,sizeof error));
    assert(!memcmp(&saved,&data,sizeof data));
    nm_unify_program_close(reference);nm_unify_program_close(program);
    nm_unify_program_close(packaged);nm_unify_program_close(NULL);
    printf("native matching API: %u calls across reference/native/compare; guards, receipts, aliases and setup refusals passed\n",calls);
    return 0;
}
