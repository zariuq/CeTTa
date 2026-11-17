#include "native_match_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { bool interrupted; } Source;
typedef struct {
    uint64_t bindings[16];NmNodePair pairs[64];uint64_t scan[64],state[40],marks[256];
} Storage;
static size_t calls, captures;
typedef struct { NmVariableKey keys[128];NmHostValue values[128];size_t count;bool stop; } Changes;
static bool collect(void *context,NmVariableKey key,NmHostValue value) {
    Changes *c=context;assert(c->count<128);
    for (size_t i=0;i<c->count;++i) assert(c->keys[i].variable!=key.variable || c->keys[i].scope!=key.scope);
    c->keys[c->count]=key;c->values[c->count++]=value;return !c->stop;
}
static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    Source *source=context;
    if (source->interrupted) return NM_VIEW_INTERRUPTED;
    if (!value.syntax) *node=(NmViewNode){.kind=NM_NODE_TUPLE};
    else if (value.syntax<1024)
        *node=(NmViewNode){.kind=NM_NODE_VARIABLE,
            .variable={value.syntax==1023 ? 0 : value.syntax,value.syntax==1023 ? 0 : value.context}};
    else if (value.syntax==10000 || value.syntax==10001)
        *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax-10000};
    else if (value.syntax==20000) *node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=1};
    else return NM_VIEW_FAULT;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context, NmHostValue value, uint64_t index, NmHostValue *out) {
    Source *source=context;
    if (source->interrupted) return NM_VIEW_INTERRUPTED;
    if (value.syntax!=20000 || index) return NM_VIEW_FAULT;
    *out=(NmHostValue){1,value.context};return NM_VIEW_READY;
}
static NmUnifyResult probe(const NmUnifyProgram *program, NmExecution mode, NmTermView view,
                           NmHostValue left, NmHostValue right, NmConstraintTruth truth,
                           NmSolution **captured) {
    NmPreparedMatch *prepared=NULL;
    assert(nm_match_prepare(&view,left,right,(NmImportLimits){100,100,16,1000},
        &prepared,NULL)==NM_VIEW_READY);
    const NmUnifyInput *input=nm_match_input(prepared);assert(input->variables_count<=16);
    Storage storage;memset(&storage,0x5A,sizeof storage);
    NmUnifyWork work={storage.bindings,16,storage.pairs,64,storage.scan,64,storage.state,40,
        storage.marks,input->variables_count+input->nodes_count+128};
    NmUnifyResult result;char error[256];
    assert(nm_unify_call(program,mode,input,&work,100000,0,&result,error,sizeof error)==NM_CALL_OK);
    assert(nm_constraint_truth(NM_CALL_OK,&result)==truth);++calls;
    if (captured) {
        assert(result.outcome==NM_UNIFIED);
        assert(nm_solution_capture(prepared,&result,&work,captured)==NM_VIEW_READY && *captured);++captures;
        NmUnifyResult wrong=result;++wrong.payload;NmSolution *wrong_count=(NmSolution *)(uintptr_t)1;
        assert(nm_solution_capture(prepared,&wrong,&work,&wrong_count)==NM_VIEW_FAULT && !wrong_count);
        if (input->variables_count) {
            uint64_t original=work.bindings[0];work.bindings[0]=input->nodes_count;
            NmSolution *bad=(NmSolution *)(uintptr_t)1;
            assert(nm_solution_capture(prepared,&result,&work,&bad)==NM_VIEW_FAULT && !bad);
            work.bindings[0]=original;
        }
    }
    /* A captured solution must not borrow either of these private carriers. */
    nm_match_prepared_free(prepared);memset(&storage,0xA5,sizeof storage);
    return result;
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);assert(program);
    for (unsigned mode=0;mode<3;++mode) {
        Source source={0};NmTermView base={&source,inspect,child,NULL};
        NmHostValue x={1,9},y={2,9},a={10000,77},b={10001,91};
        probe(program,(NmExecution)mode,base,x,a,NM_TRUTH_OPEN,NULL);
        probe(program,(NmExecution)mode,base,x,x,NM_TRUTH_ENTAILED,NULL);
        probe(program,(NmExecution)mode,base,a,b,NM_TRUTH_REFUTED,NULL);
        probe(program,(NmExecution)mode,base,x,(NmHostValue){20000,9},NM_TRUTH_REFUTED,NULL);
        NmSolution *unit=NULL;NmHostValue zero_key={1023,99},unit_value={0,0};
        probe(program,(NmExecution)mode,base,zero_key,unit_value,NM_TRUTH_OPEN,&unit);
        NmTermView unit_view=nm_solution_view(unit);bool unit_bound=false;NmHostValue observed;
        assert(unit_view.binding(unit_view.context,zero_key,&unit_bound,&observed)==NM_VIEW_READY &&
            unit_bound && !observed.syntax && !observed.context);
        Changes unit_changes={0};assert(nm_solution_visit_changes(unit,collect,&unit_changes) &&
            unit_changes.count==1 && !unit_changes.keys[0].variable && !unit_changes.keys[0].scope &&
            !unit_changes.values[0].syntax && !unit_changes.values[0].context);
        probe(program,(NmExecution)mode,unit_view,zero_key,unit_value,NM_TRUTH_ENTAILED,NULL);
        nm_solution_free(unit);
        NmSolution *first=NULL,*other=NULL;
        probe(program,(NmExecution)mode,base,x,a,NM_TRUTH_OPEN,&first);
        probe(program,(NmExecution)mode,base,x,b,NM_TRUTH_OPEN,&other);
        assert(nm_solution_entries(first)==1 && nm_solution_depth(first)==1);
        NmTermView refined=nm_solution_view(first);
        probe(program,(NmExecution)mode,refined,x,a,NM_TRUTH_ENTAILED,NULL);
        probe(program,(NmExecution)mode,refined,x,b,NM_TRUTH_REFUTED,NULL);
        probe(program,(NmExecution)mode,nm_solution_view(other),x,b,NM_TRUTH_ENTAILED,NULL);
        probe(program,(NmExecution)mode,refined,(NmHostValue){1,10},a,NM_TRUTH_OPEN,NULL);
        NmSolution *second=NULL;
        probe(program,(NmExecution)mode,refined,y,x,NM_TRUTH_OPEN,&second);
        assert(nm_solution_depth(second)==2);
        nm_solution_free(first);
        refined=nm_solution_view(second);
        probe(program,(NmExecution)mode,refined,y,a,NM_TRUTH_ENTAILED,NULL);
        probe(program,(NmExecution)mode,refined,x,a,NM_TRUTH_ENTAILED,NULL);
        bool bound=false;NmHostValue value;
        assert(refined.binding(refined.context,y,&bound,&value)==NM_VIEW_READY && bound &&
            value.syntax==a.syntax && value.context==a.context);
        source.interrupted=true;
        NmPreparedMatch *prepared=NULL;
        assert(nm_match_prepare(&refined,y,a,(NmImportLimits){100,100,16,1000},&prepared,NULL)==NM_VIEW_INTERRUPTED && !prepared);
        source.interrupted=false;
        nm_solution_free(second);nm_solution_free(other);

        NmSolution *links=NULL,*grounded=NULL;
        probe(program,(NmExecution)mode,base,x,y,NM_TRUTH_OPEN,&links);
        probe(program,(NmExecution)mode,nm_solution_view(links),y,a,NM_TRUTH_OPEN,&grounded);
        nm_solution_free(links);
        Changes delta={0};assert(nm_solution_visit_changes(grounded,collect,&delta) && delta.count==2);
        assert(delta.keys[0].variable==2 && delta.values[0].syntax==a.syntax && delta.values[0].context==a.context);
        assert(delta.keys[1].variable==1 && delta.values[1].syntax==y.syntax && delta.values[1].context==y.context);
        assert(nm_solution_changes(grounded)==2);
        probe(program,(NmExecution)mode,nm_solution_view(grounded),x,a,NM_TRUTH_ENTAILED,NULL);
        nm_solution_free(grounded);

        /* Each new immutable refinement retains its ancestor. Releasing the
         * caller's old handle cannot erase earlier facts. */
        NmSolution *chain=NULL;
        for (size_t i=1;i<=96;++i) {
            NmSolution *next=NULL;
            probe(program,(NmExecution)mode,chain ? nm_solution_view(chain) : base,
                (NmHostValue){i,30},a,NM_TRUTH_OPEN,&next);
            nm_solution_free(chain);chain=next;
            assert(nm_solution_depth(chain)==i);
        }
        probe(program,(NmExecution)mode,nm_solution_view(chain),(NmHostValue){1,30},a,NM_TRUTH_ENTAILED,NULL);
        probe(program,(NmExecution)mode,nm_solution_view(chain),(NmHostValue){96,30},b,NM_TRUTH_REFUTED,NULL);
        Changes all={0};assert(nm_solution_visit_changes(chain,collect,&all) && all.count==96 && nm_solution_changes(chain)==96);
        Changes partial={.stop=true};assert(!nm_solution_visit_changes(chain,collect,&partial) && partial.count==1);
        nm_solution_free(chain);
    }
    NmSolution *bad=(NmSolution *)(uintptr_t)1;
    assert(nm_solution_capture(NULL,NULL,NULL,&bad)==NM_VIEW_FAULT && !bad);
    assert(!nm_solution_view(NULL).inspect && !nm_solution_entries(NULL) && !nm_solution_depth(NULL));
    nm_solution_free(NULL);nm_unify_program_close(program);
    printf("native solution views: %zu constraint probes, %zu immutable captures; tell/guard timing, branch isolation, retained parents, scoped values and interruption passed\n",calls,captures);
    return 0;
}
