#include "native_match_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    NmHostValue value;
    NmViewNode node;
    NmHostValue *children;
    NmHostValue binding;
    bool bound;
    NmViewStatus status;
} Source;
typedef struct {
    Source *sources;
    size_t count, reads;
    NmViewStatus child_status, binding_status;
} Fixture;
typedef struct {
    NmUnifyWork work;
} Storage;

static size_t attempts, executions, projections;
static bool equal(NmHostValue a, NmHostValue b) {
    return a.syntax==b.syntax && a.context==b.context;
}
static Source *lookup(Fixture *f, NmHostValue value) {
    for (size_t i=0;i<f->count;++i) if (equal(f->sources[i].value,value)) return f->sources+i;
    return NULL;
}
static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    Fixture *f=context;Source *source=lookup(f,value);++f->reads;
    if (!source) return NM_VIEW_FAULT;
    if (source->status!=NM_VIEW_READY) return source->status;
    *node=source->node;return NM_VIEW_READY;
}
static NmViewStatus child(void *context, NmHostValue value, uint64_t index, NmHostValue *out) {
    Fixture *f=context;Source *source=lookup(f,value);++f->reads;
    if (f->child_status!=NM_VIEW_READY) return f->child_status;
    if (!source || index>=source->node.children) return NM_VIEW_FAULT;
    *out=source->children[index];return NM_VIEW_READY;
}
static NmViewStatus binding(void *context, NmHostValue variable, bool *bound, NmHostValue *out) {
    Fixture *f=context;Source *source=lookup(f,variable);++f->reads;
    if (f->binding_status!=NM_VIEW_READY) return f->binding_status;
    if (!source || source->node.kind!=NM_NODE_VARIABLE) return NM_VIEW_FAULT;
    *bound=source->bound;*out=source->binding;return NM_VIEW_READY;
}
static NmTermView view(Fixture *f) {
    return (NmTermView){f,inspect,child,binding};
}
static NmImportLimits limits(void) {
    return (NmImportLimits){10000,20000,10000,100000};
}
static NmPreparedMatch *prepare(Fixture *f, NmHostValue a, NmHostValue b,
                                NmImportLimits limit, NmViewStatus expected, NmImportStats *stats) {
    NmPreparedMatch *prepared=(NmPreparedMatch *)(uintptr_t)1;
    NmTermView v=view(f);size_t before=f->reads;++attempts;
    NmImportStats actual;
    assert(nm_match_prepare(&v,a,b,limit,&prepared,&actual)==expected);
    assert(actual.callbacks==actual.inspections+actual.children+actual.bindings);
    assert(actual.callbacks==f->reads-before && actual.callbacks<=limit.callbacks);
    if (stats) *stats=actual;
    if (expected!=NM_VIEW_READY) { assert(!prepared);return NULL; }
    const NmUnifyInput *input=nm_match_input(prepared);
    assert(input && input->left && input->right && input->keys_count==input->variables_count);
    for (size_t i=1;i<input->nodes_count;++i) {
        NmNode node=input->nodes[i];
        if (node.kind==NM_NODE_TUPLE) for (size_t j=0;j<node.b;++j)
            assert(input->children[node.a+j] && input->children[node.a+j]<i);
        if (node.kind==NM_NODE_VARIABLE) {
            assert(node.a<input->keys_count);
            assert(node.b==input->keys[node.a].variable && node.c==input->keys[node.a].scope);
        }
    }
    for (size_t i=1;i<input->keys_count;++i)
        assert(input->keys[i-1].scope<input->keys[i].scope ||
            (input->keys[i-1].scope==input->keys[i].scope &&
             input->keys[i-1].variable<input->keys[i].variable));
    return prepared;
}
static Storage storage(const NmUnifyProgram *program, const NmUnifyInput *input) {
    Storage s={0};NmUnifyWork *w=&s.work;
    w->bindings_words=input->variables_count;
    w->pairs_count=input->children_count+16;
    w->scan_words=input->children_count+input->nodes_count+16;
    w->state_words=nm_unify_state_words(program);
    size_t memo=16;
    while (memo<2*input->nodes_count) memo*=2;
    w->marks_words=input->variables_count+input->nodes_count+memo;
    w->bindings=calloc(w->bindings_words ? w->bindings_words : 1,sizeof *w->bindings);
    w->pairs=calloc(w->pairs_count,sizeof *w->pairs);
    w->scan=calloc(w->scan_words,sizeof *w->scan);
    w->state=calloc(w->state_words,sizeof *w->state);
    w->marks=calloc(w->marks_words,sizeof *w->marks);
    assert(w->bindings && w->pairs && w->scan && w->state && w->marks);
    return s;
}
static void storage_free(Storage *s) {
    free(s->work.bindings);free(s->work.pairs);free(s->work.scan);
    free(s->work.state);free(s->work.marks);
}
static NmUnifyResult run(const NmUnifyProgram *program, NmExecution mode,
                         NmPreparedMatch *prepared, Storage *s, uint64_t fuel) {
    char error[256];NmUnifyResult result;
    bool ok=nm_unify_run(program,mode,nm_match_input(prepared),&s->work,
        fuel,0,&result,error,sizeof error);
    if (!ok) fprintf(stderr,"view run: %s\n",error);
    assert(ok);++executions;return result;
}
static NmBindingProjection project(NmPreparedMatch *prepared, NmUnifyResult result,
                                   Storage *s, size_t slot) {
    NmBindingProjection p;
    assert(nm_match_project(prepared,&result,&s->work,slot,&p));++projections;
    return p;
}
static void outcomes(const NmUnifyProgram *program, NmPreparedMatch *prepared,
                      NmOutcome outcome, uint64_t payload) {
    for (unsigned mode=0;mode<3;++mode) {
        Storage s=storage(program,nm_match_input(prepared));
        NmUnifyResult r=run(program,(NmExecution)mode,prepared,&s,1000000);
        assert(r.outcome==outcome && r.payload==payload);
        NmConstraintTruth expected=outcome==NM_UNIFIED ? (payload ? NM_TRUTH_OPEN : NM_TRUTH_ENTAILED) :
            outcome==NM_NO_MATCH ? NM_TRUTH_REFUTED : NM_TRUTH_UNAVAILABLE;
        assert(nm_constraint_truth(NM_CALL_OK,&r)==expected);
        NmBindingProjection projection;
        if (outcome!=NM_UNIFIED)
            assert(!nm_match_project(prepared,&r,&s.work,0,&projection));
        storage_free(&s);
    }
}

static void contextual(const NmUnifyProgram *program) {
    /* Syntax ids are deliberately unrelated to child order. Equal syntax in
     * distinct lexical contexts denotes distinct variables here. */
    NmHostValue lc[]={{700,9},{700,3},{17,12}},rc[]={{1,90},{1,91},{1,90}};
    Source sources[]={
        {.value={1,90},.node={.kind=NM_NODE_CONSTANT,.constant=77}},
        {.value={8,2},.node={.kind=NM_NODE_TUPLE,.children=3},.children=lc},
        {.value={700,3},.node={.kind=NM_NODE_VARIABLE,.variable={41,3}}},
        {.value={1,91},.node={.kind=NM_NODE_CONSTANT,.constant=77}},
        {.value={3,4},.node={.kind=NM_NODE_TUPLE,.children=3},.children=rc},
        {.value={700,9},.node={.kind=NM_NODE_VARIABLE,.variable={41,9}}},
        {.value={17,12},.node={.kind=NM_NODE_VARIABLE,.variable={41,9}}}};
    Source before[sizeof sources/sizeof *sources];memcpy(before,sources,sizeof sources);
    Fixture f={.sources=sources,.count=sizeof sources/sizeof *sources};NmImportStats stats;
    NmPreparedMatch *p=prepare(&f,sources[1].value,sources[4].value,limits(),NM_VIEW_READY,&stats);
    const NmUnifyInput *input=nm_match_input(p);
    assert(input->variables_count==2 && stats.bindings==2 && stats.inspections==7);
    assert(input->keys[0].scope==3 && input->keys[1].scope==9);
    for (unsigned mode=0;mode<3;++mode) {
        Storage s=storage(program,input);NmUnifyResult r=run(program,(NmExecution)mode,p,&s,100000);
        assert(r.outcome==NM_UNIFIED && r.payload==2);
        NmBindingProjection y=project(p,r,&s,0),x=project(p,r,&s,1);
        assert(equal(y.variable,(NmHostValue){700,3}) && equal(y.value,(NmHostValue){1,91}));
        assert(equal(x.variable,(NmHostValue){700,9}) && equal(x.value,(NmHostValue){1,90}));
        assert(y.bound && y.changed && x.bound && x.changed);
        assert(input->initial[0]==0 && input->initial[1]==0);
        NmBindingProjection untouched={.key={912,313}};
        assert(!nm_match_project(p,&r,&s.work,2,&untouched) && untouched.key.variable==912);
        s.work.bindings[0]=input->nodes_count;
        assert(!nm_match_project(p,&r,&s.work,0,&untouched) && untouched.key.variable==912);
        storage_free(&s);
        s=storage(program,input);r=run(program,(NmExecution)mode,p,&s,0);
        assert(r.outcome==NM_EXHAUSTED && !nm_match_project(p,&r,&s.work,0,&untouched));
        storage_free(&s);
    }
    assert(!memcmp(sources,before,sizeof sources));
    NmImportLimits exact={input->nodes_count,input->children_count,input->variables_count,stats.callbacks};
    NmPreparedMatch *q=prepare(&f,sources[1].value,sources[4].value,exact,NM_VIEW_READY,NULL);
    nm_match_prepared_free(q);
    for (unsigned field=0;field<4;++field) {
        NmImportLimits limited=exact;
        if (field==0) --limited.nodes;
        if (field==1) --limited.edges;
        if (field==2) --limited.variables;
        if (field==3) --limited.callbacks;
        prepare(&f,sources[1].value,sources[4].value,limited,NM_VIEW_RESOURCE,NULL);
    }
    nm_match_prepared_free(p);
}

static void environments(const NmUnifyProgram *program) {
    NmHostValue child_x[]={{900,4}},child_a[]={{8,31}};
    Source source[]={
        {.value={900,4},.node={.kind=NM_NODE_VARIABLE,.variable={99,4}}},
        {.value={8,31},.node={.kind=NM_NODE_CONSTANT,.constant=12}},
        {.value={17,82},.node={.kind=NM_NODE_TUPLE,.children=1},.children=child_x},
        {.value={19,64},.node={.kind=NM_NODE_TUPLE,.children=1},.children=child_a},
        {.value={77,8},.node={.kind=NM_NODE_VARIABLE,.variable={6,8}}}};
    Fixture f={.sources=source,.count=5};
    NmPreparedMatch *p=prepare(&f,source[0].value,source[2].value,limits(),NM_VIEW_READY,NULL);
    outcomes(program,p,NM_NO_MATCH,0);nm_match_prepared_free(p);
    p=prepare(&f,source[0].value,source[4].value,limits(),NM_VIEW_READY,NULL);
    for (unsigned mode=0;mode<3;++mode) {
        Storage s=storage(program,nm_match_input(p));NmUnifyResult r=run(program,(NmExecution)mode,p,&s,100000);
        assert(r.outcome==NM_UNIFIED && r.payload==1);
        NmBindingProjection a=project(p,r,&s,0),b=project(p,r,&s,1);
        assert(a.bound && a.changed && equal(a.value,source[4].value) && !b.bound && !b.changed);
        storage_free(&s);
    }
    nm_match_prepared_free(p);
    source[0].bound=true;source[0].binding=source[4].value;
    source[4].bound=true;source[4].binding=source[1].value;
    p=prepare(&f,source[0].value,source[1].value,limits(),NM_VIEW_READY,NULL);
    assert(nm_match_input(p)->variables_count==2);
    for (unsigned mode=0;mode<3;++mode) {
        Storage s=storage(program,nm_match_input(p));NmUnifyResult r=run(program,(NmExecution)mode,p,&s,100000);
        assert(r.outcome==NM_UNIFIED && !r.payload);
        NmBindingProjection a=project(p,r,&s,0),b=project(p,r,&s,1);
        assert(a.bound && !a.changed && equal(a.value,source[4].value));
        assert(b.bound && !b.changed && equal(b.value,source[1].value));
        s.work.bindings[0]=0;
        assert(!nm_match_project(p,&r,&s.work,0,&a));storage_free(&s);
    }
    nm_match_prepared_free(p);
    source[4].binding=source[0].value;
    p=prepare(&f,source[0].value,source[1].value,limits(),NM_VIEW_READY,NULL);
    outcomes(program,p,NM_UNSUPPORTED,5);nm_match_prepared_free(p);
    source[0].binding=source[0].value;
    p=prepare(&f,source[0].value,source[1].value,limits(),NM_VIEW_READY,NULL);
    outcomes(program,p,NM_UNSUPPORTED,5);nm_match_prepared_free(p);
    source[0].binding=source[2].value;
    p=prepare(&f,source[0].value,source[1].value,limits(),NM_VIEW_READY,NULL);
    outcomes(program,p,NM_UNSUPPORTED,5);nm_match_prepared_free(p);
    source[0].bound=false;source[4].bound=false;
    NmTermView v=view(&f);v.binding=NULL;NmImportStats stats;++attempts;
    assert(nm_match_prepare(&v,source[0].value,source[1].value,limits(),&p,&stats)==NM_VIEW_READY);
    assert(!stats.bindings);outcomes(program,p,NM_UNIFIED,1);nm_match_prepared_free(p);
    source[4].bound=true;source[4].binding=source[1].value;
    p=prepare(&f,source[4].value,source[3].value,limits(),NM_VIEW_READY,NULL);
    outcomes(program,p,NM_NO_MATCH,0);nm_match_prepared_free(p);
}

static void refusals(void) {
    NmHostValue children[]={{5,0}};
    Source sources[]={
        {.value={5,0},.node={.kind=NM_NODE_TUPLE,.children=1},.children=children},
        {.value={6,0},.node={.kind=NM_NODE_VARIABLE,.variable={1,0}}},
        {.value={7,0},.node={.kind=NM_NODE_CONSTANT,.constant=9}}};
    Fixture f={.sources=sources,.count=3};NmImportLimits limit=limits();
    prepare(&f,sources[0].value,sources[2].value,limit,NM_VIEW_DEFER,NULL);
    children[0]=sources[2].value;
    f.child_status=NM_VIEW_FAULT;
    prepare(&f,sources[0].value,sources[2].value,limit,NM_VIEW_FAULT,NULL);
    f.child_status=NM_VIEW_RESOURCE;
    prepare(&f,sources[0].value,sources[2].value,limit,NM_VIEW_RESOURCE,NULL);
    f.child_status=NM_VIEW_INTERRUPTED;
    prepare(&f,sources[0].value,sources[2].value,limit,NM_VIEW_INTERRUPTED,NULL);
    f.child_status=NM_VIEW_READY;
    f.binding_status=NM_VIEW_DEFER;
    prepare(&f,sources[1].value,sources[2].value,limit,NM_VIEW_DEFER,NULL);
    f.binding_status=(NmViewStatus)99;
    prepare(&f,sources[1].value,sources[2].value,limit,NM_VIEW_FAULT,NULL);
    f.binding_status=NM_VIEW_READY;
    sources[2].status=NM_VIEW_DEFER;
    prepare(&f,sources[2].value,sources[2].value,limit,NM_VIEW_DEFER,NULL);
    sources[2].status=NM_VIEW_READY;sources[2].node.kind=(NmNodeKind)99;
    prepare(&f,sources[2].value,sources[2].value,limit,NM_VIEW_DEFER,NULL);
    sources[2].node.kind=NM_NODE_CONSTANT;
    prepare(&f,(NmHostValue){7000,0},sources[2].value,limit,NM_VIEW_FAULT,NULL);
    limit=(NmImportLimits){0};
    prepare(&f,sources[2].value,sources[2].value,limit,NM_VIEW_RESOURCE,NULL);
    NmTermView v=view(&f);NmPreparedMatch *p=(NmPreparedMatch *)(uintptr_t)1;NmImportStats stats;
    v.child=NULL;++attempts;
    assert(nm_match_prepare(&v,sources[0].value,sources[2].value,limits(),&p,&stats)==NM_VIEW_FAULT && !p);
    v=view(&f);v.inspect=NULL;p=(NmPreparedMatch *)(uintptr_t)1;++attempts;
    assert(nm_match_prepare(&v,sources[0].value,sources[2].value,limits(),&p,&stats)==NM_VIEW_FAULT && !p);
    assert(!nm_match_input(NULL));nm_match_prepared_free(NULL);
}

static void shared_and_deep(const NmUnifyProgram *program) {
    /* A depth-2048 shared DAG would have 2^2048 leaves if expanded. */
    const size_t depth=2048,n=2*depth+2;
    Source *sources=calloc(n,sizeof *sources);NmHostValue *edges=calloc(4*depth,sizeof *edges);
    assert(sources && edges);
    for (size_t i=0;i<n;++i) sources[i].value=(NmHostValue){UINT64_MAX-31*i,17*i+9};
    sources[0].node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={800,9}};
    sources[depth+1].node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=37};
    for (size_t i=1;i<=depth;++i) for (size_t side=0;side<2;++side) {
        size_t k=i+side*(depth+1),edge=2*(i-1)+side*2*depth;
        sources[k].node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=2};
        sources[k].children=edges+edge;edges[edge]=edges[edge+1]=sources[k-1].value;
    }
    Fixture f={.sources=sources,.count=n};NmImportStats stats;
    NmPreparedMatch *p=prepare(&f,sources[depth].value,sources[n-1].value,limits(),NM_VIEW_READY,&stats);
    assert(stats.inspections==n && stats.children==4*depth && stats.bindings==1);
    outcomes(program,p,NM_UNIFIED,1);nm_match_prepared_free(p);
    free(edges);free(sources);

    /* Binding discovery grows both the value map and variable vector. */
    const size_t count=257;
    sources=calloc(count+1,sizeof *sources);assert(sources);
    for (size_t i=0;i<=count;++i) {
        sources[i].value=(NmHostValue){count-i,100+i};
        sources[i].node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={count-i,7}};
        sources[i].bound=i<count;
        sources[i].binding=(NmHostValue){count-i-1,101+i};
    }
    sources[count].node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=12};
    f=(Fixture){.sources=sources,.count=count+1};
    p=prepare(&f,sources[0].value,sources[count].value,limits(),NM_VIEW_READY,&stats);
    assert(nm_match_input(p)->variables_count==count && stats.bindings==count);
    outcomes(program,p,NM_UNIFIED,0);nm_match_prepared_free(p);free(sources);
}

static void conjunctions(const NmUnifyProgram *program) {
    NmHostValue fx[]={{900,1}},fa[]={{17,8}},fy[]={{800,1}};
    Source sources[]={
        {.value={900,1},.node={.kind=NM_NODE_VARIABLE,.variable={9,1}}},
        {.value={800,1},.node={.kind=NM_NODE_VARIABLE,.variable={8,1}}},
        {.value={17,8},.node={.kind=NM_NODE_CONSTANT,.constant=31}},
        {.value={19,8},.node={.kind=NM_NODE_CONSTANT,.constant=32}},
        {.value={99,7},.node={.kind=NM_NODE_TUPLE,.children=1},.children=fx},
        {.value={98,7},.node={.kind=NM_NODE_TUPLE,.children=1},.children=fa},
        {.value={97,7},.node={.kind=NM_NODE_TUPLE,.children=1},.children=fy}};
    Fixture f={.sources=sources,.count=7};NmTermView v=view(&f);NmImportStats stats;
    NmHostConstraint constraints[]={
        {sources[0].value,sources[2].value},
        {sources[1].value,sources[4].value},
        {sources[1].value,sources[5].value}};
    NmPreparedMatch *p=NULL;++attempts;
    assert(nm_match_prepare_constraints(&v,constraints,3,limits(),&p,&stats)==NM_VIEW_READY);
    for (unsigned mode=0;mode<3;++mode) {
        Storage s=storage(program,nm_match_input(p));NmUnifyResult r=run(program,(NmExecution)mode,p,&s,100000);
        assert(r.outcome==NM_UNIFIED && r.payload==2);
        NmBindingProjection y=project(p,r,&s,0),x=project(p,r,&s,1);
        assert(equal(y.value,sources[4].value) && equal(x.value,sources[2].value));
        assert(y.bound && y.changed && x.bound && x.changed);
        s.work.bindings[0]=nm_match_input(p)->left;
        assert(!nm_match_project(p,&r,&s.work,0,&y));
        storage_free(&s);
    }
    nm_match_prepared_free(p);
    constraints[1]=(NmHostConstraint){sources[0].value,sources[3].value};++attempts;
    assert(nm_match_prepare_constraints(&v,constraints,2,limits(),&p,&stats)==NM_VIEW_READY);
    outcomes(program,p,NM_NO_MATCH,0);nm_match_prepared_free(p);
    constraints[0]=(NmHostConstraint){sources[0].value,sources[6].value};
    constraints[1]=(NmHostConstraint){sources[1].value,sources[4].value};++attempts;
    assert(nm_match_prepare_constraints(&v,constraints,2,limits(),&p,&stats)==NM_VIEW_READY);
    outcomes(program,p,NM_NO_MATCH,0);nm_match_prepared_free(p);
    size_t before=f.reads;++attempts;
    assert(nm_match_prepare_constraints(&v,NULL,0,(NmImportLimits){3,0,0,0},&p,&stats)==NM_VIEW_READY);
    assert(f.reads==before && !stats.callbacks);
    outcomes(program,p,NM_UNIFIED,0);nm_match_prepared_free(p);
    ++attempts;
    assert(nm_match_prepare_constraints(&v,constraints,SIZE_MAX,limits(),&p,&stats)==NM_VIEW_RESOURCE && !p);
    ++attempts;
    assert(nm_match_prepare_constraints(&v,NULL,1,limits(),&p,&stats)==NM_VIEW_FAULT && !p);
    ++attempts;
    assert(nm_match_prepare_constraints(&v,NULL,0,(NmImportLimits){2,0,0,0},&p,&stats)==NM_VIEW_RESOURCE && !p);
}

int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);
    if (!program) { fprintf(stderr,"%s\n",error);return 1; }
    contextual(program);environments(program);refusals();shared_and_deep(program);conjunctions(program);
    nm_unify_program_close(program);
    printf("host-view matching: %zu imports, %zu reference/native/compare executions, %zu contextual projections; scope, graph order, DAG sharing, cycles and limits passed\n",
        attempts,executions,projections);
    return 0;
}
