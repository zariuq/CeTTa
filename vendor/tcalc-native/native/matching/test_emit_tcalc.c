#include "native_emit.h"
#include "native_match_view.h"
#include "native_tcalc_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tcalc_core.export.c"

typedef struct { NmTcalcTermView base;NmTermView raw;Solver *solver; } View;
typedef struct { uint64_t total,prepare,execute,adopt;size_t nodes,edges,expanded; } Measure;
static volatile uint64_t sink;
static uint64_t now(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static NmViewStatus inspect(void *context,NmHostValue value,NmViewNode *node) {
    View *v=context;return v->raw.inspect(v->raw.context,value,node);
}
static NmViewStatus child(void *context,NmHostValue value,uint64_t index,NmHostValue *out) {
    View *v=context;return v->raw.child(v->raw.context,value,index,out);
}
static NmViewStatus binding(void *context,NmHostValue value,bool *bound,NmHostValue *out) {
    View *v=context;TcTerm term=(TcTerm)value.syntax;TcKind kind=tc_kind(v->base.terms,term);uint32_t i=tc_index(v->base.terms,term);
    TcTerm target=0;
    if (kind==TC_LOCAL) {
        if (value.context!=600 || i>=v->solver->local_cap) return NM_VIEW_DEFER;
        target=local_at(v->solver,i);
    } else if (kind==TC_UNKNOWN) {
        if (i>=v->solver->unknown_cap) return NM_VIEW_DEFER;
        target=unknown_at(v->solver,i);
    } else return NM_VIEW_FAULT;
    *bound=target!=0;*out=(NmHostValue){target,value.context};return NM_VIEW_READY;
}
static NmEmitWork allocate_work(const NmEmitProgram *p,const NmUnifyInput *in) {
    NmEmitWork w={.nodes_count=in->nodes_count+1,.children_count=in->children_count+1,
        .memo_words=in->nodes_count+in->variables_count,.stack_count=in->nodes_count+1,
        .state_words=nm_emit_state_words(p)};
    w.nodes=calloc(w.nodes_count,sizeof *w.nodes);w.children=calloc(w.children_count,sizeof *w.children);
    w.memo=calloc(w.memo_words,sizeof *w.memo);w.stack=calloc(w.stack_count,sizeof *w.stack);w.state=calloc(w.state_words,sizeof *w.state);
    assert(w.nodes && w.children && w.memo && w.stack && w.state);return w;
}
static void release_work(NmEmitWork *w) {
    free(w->nodes);free(w->children);free(w->memo);free(w->stack);free(w->state);
}
static void bundle(const NmEmitProgram *program,NmExecution mode,TcTerms *terms,Solver *solver,
                    const TcTerm *roots,size_t count,TcTerm *result,Measure *measure) {
    uint64_t start=now();
    View v={.base={terms,(TcTerm)(terms->nodes-1),900},.solver=solver};v.raw=nm_tcalc_term_view(&v.base);
    NmTermView view={&v,inspect,child,binding};NmHostConstraint *constraints=calloc(count ? count : 1,sizeof *constraints);assert(constraints);
    for (size_t i=0;i<count;++i) constraints[i]=(NmHostConstraint){{roots[i],600},{roots[i],600}};
    NmPreparedMatch *prepared=NULL;
    assert(nm_match_prepare_constraints(&view,constraints,count,(NmImportLimits){1000000,2000000,10000,10000000},
        &prepared,NULL)==NM_VIEW_READY);
    const NmUnifyInput *in=nm_match_input(prepared);NmEmitWork work=allocate_work(program,in);
    NmEmitInput input={in->nodes,in->nodes_count,in->children,in->children_count,in->initial,in->variables_count,
        in->keys,in->keys_count,in->left};
    uint64_t entered=now();NmEmitResult emitted;char error[256];
    NmCallStatus called=nm_emit_call(program,mode,&input,&work,UINT64_C(1000000000),0,&emitted,error,sizeof error);
    if (called!=NM_CALL_OK) fprintf(stderr,"native bundle: %s\n",error);
    assert(called==NM_CALL_OK && emitted.outcome==NM_EMIT_FIRED);
    uint64_t finished=now();
    assert(work.nodes[emitted.root].node.kind==NM_NODE_TUPLE && work.nodes[emitted.root].node.b==count);
    TcTerm *map=calloc(emitted.nodes_written,sizeof *map);
    TcTerm *items=malloc((emitted.children_written ? emitted.children_written : 1)*sizeof *items);assert(map && items);
    for (size_t i=1;i<emitted.nodes_written;++i) {
        NmNode n=work.nodes[i].node;
        if (i==emitted.root) continue;
        if (n.kind==NM_NODE_CONSTANT) map[i]=(TcTerm)n.a;
        else if (n.kind==NM_NODE_VARIABLE) {
            assert(n.c==600 || n.c==900);map[i]=(TcTerm)n.b;
        } else {
            assert(n.kind==NM_NODE_TUPLE);
            for (size_t j=0;j<n.b;++j) {
                uint64_t id=work.children[n.a+j];assert(id && id<i && map[id]);items[j]=map[id];
            }
            map[i]=tc_expression(terms,items,(uint32_t)n.b);assert(map[i]);
        }
    }
    NmNode top=work.nodes[emitted.root].node;
    for (size_t i=0;i<count;++i) { result[i]=map[work.children[top.a+i]];assert(result[i]); }
    free(map);free(items);release_work(&work);nm_match_prepared_free(prepared);free(constraints);
    uint64_t end=now();
    if (measure) *measure=(Measure){end-start,entered-start,finished-entered,end-finished,
        emitted.nodes_written,emitted.children_written,emitted.expanded};
}
static void correctness(const NmEmitProgram *program) {
    TcTerms *terms=tc_terms_new();assert(terms);Solver s={0};
    assert(reserve(&s.local,&s.local_cap,16) && reserve_unknowns(&s,16));
    TcTerm a=tc_symbol(terms,"a",1),b=tc_symbol(terms,"b",1),f=tc_symbol(terms,"f",1);
    TcTerm x=tc_local(terms,0),y=tc_local(terms,1),u=tc_unknown(terms,0),v=tc_unknown(terms,1);
    TcTerm pool[]={a,b,f,x,y,u,v,tc_string(terms,"a\0b",3),tc_integer(terms,INT64_MIN),
        tc_expression(terms,(TcTerm[]){f,x,u},3),tc_expression(terms,(TcTerm[]){f,y,v},3),tc_expression(terms,NULL,0)};
    size_t calls=0;
    for (unsigned environment=0;environment<4;++environment) {
        Mark before=mark(&s);
        if (environment&1) assert(assign(&s,true,0,a) && assign(&s,false,0,b));
        if (environment&2) assert(assign(&s,true,1,u) && assign(&s,false,1,tc_expression(terms,(TcTerm[]){f,u},2)));
        for (size_t i=0;i<sizeof pool/sizeof *pool;++i) {
            TcTerm roots[]={pool[i],pool[(i+1)%(sizeof pool/sizeof *pool)],pool[i]};TcTerm expected[3];
            for (size_t j=0;j<3;++j) expected[j]=resolve(terms,&s,roots[j],0);
            for (unsigned mode=0;mode<3;++mode) {
                TcTerm actual[3];bundle(program,(NmExecution)mode,terms,&s,roots,3,actual,NULL);
                assert(!memcmp(expected,actual,sizeof expected));++calls;
            }
        }
        undo(&s,before);
    }
    solver_free(&s);tc_terms_free(terms);
    printf("native output tcalc client: %zu complete captured-template bundles agree with the actual C resolver across all modes\n",calls);
}
static void benchmark(const NmEmitProgram *program,size_t depth,size_t roots,size_t repeats) {
    TcTerms *terms=tc_terms_new();assert(terms);Solver s={0};assert(reserve_unknowns(&s,16));
    TcTerm unknown=tc_unknown(terms,0),a=tc_symbol(terms,"a",1),root=unknown;
    for (size_t i=0;i<depth;++i) root=tc_expression(terms,(TcTerm[]){root,root},2);
    assert(assign(&s,false,0,a));
    TcTerm *input=malloc(roots*sizeof *input),*expected=malloc(roots*sizeof *expected),*out=malloc(roots*sizeof *out);
    assert(input && expected && out);
    for (size_t i=0;i<roots;++i) { input[i]=root;expected[i]=close_term(terms,&s,root,0); }
    Measure warm;bundle(program,NM_NATIVE,terms,&s,input,roots,out,&warm);
    assert(!memcmp(out,expected,roots*sizeof *out));
    uint64_t ctime=0,cbatch=0,ntime=0,prep=0,execute=0,adopt=0;
    for (size_t repeat=0;repeat<repeats;++repeat) {
        for (size_t choice=0;choice<3;++choice) {
            size_t variant=(repeat+choice)%3;
            if (!variant) {
                uint64_t start=now();
                for (size_t i=0;i<roots;++i) { TcTerm result=close_term(terms,&s,root,0);assert(result==expected[i]);sink+=result; }
                ctime+=now()-start;
            } else if (variant==1) {
                uint64_t start=now();walk_begin(terms);
                for (size_t i=0;i<roots;++i) { TcTerm result=close_from(terms,&s,root,0);assert(result==expected[i]);sink+=result; }
                cbatch+=now()-start;
            } else {
                Measure m;bundle(program,NM_NATIVE,terms,&s,input,roots,out,&m);
                assert(!memcmp(out,expected,roots*sizeof *out));sink+=out[roots-1];
                ntime+=m.total;prep+=m.prepare;execute+=m.execute;adopt+=m.adopt;
            }
        }
    }
    printf("BENCH depth=%zu roots=%zu repeats=%zu C_ns=%llu C_batched_ns=%llu native_total_ns=%llu prepare_ns=%llu execute_ns=%llu adopt_cleanup_ns=%llu vs_current=%.3f vs_batched=%.3f nodes=%zu edges=%zu expanded=%zu\n",
        depth,roots,repeats,(unsigned long long)ctime,(unsigned long long)cbatch,(unsigned long long)ntime,(unsigned long long)prep,
        (unsigned long long)execute,(unsigned long long)adopt,(double)ctime/(double)ntime,(double)cbatch/(double)ntime,
        warm.nodes,warm.edges,warm.expanded);
    free(input);free(expected);free(out);solver_free(&s);tc_terms_free(terms);
}
int main(int argc,char **argv) {
    char error[256];NmEmitProgram *program=nm_emit_program_default(true,error,sizeof error);assert(program);
    correctness(program);
    if (argc==2 && !strcmp(argv[1],"--bench")) {
        benchmark(program,1,1,100);benchmark(program,128,1,100);benchmark(program,128,256,100);
        benchmark(program,256,512,50);
    }
    nm_emit_program_close(program);return 0;
}
