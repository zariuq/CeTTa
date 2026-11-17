#include "native_query.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bool pinned;
    size_t reads, invalidate_at;
    bool defer;
    bool interrupted;
} Source;
typedef struct {
    size_t proposed, imported, results, denied, matches;
    uint64_t fuel;
    NmOutcome last_outcome;
    NmViewStatus last_import;
    Source *invalidate;
    NmQueryEventKind invalidate_kind;
} Trace;
static size_t queries, answers, yields;

/* Atoms 1..3 are three query variables, 10 is edge, 20..23 are
 * vertices; 100..104 are graph edges, 200/201 are the joined patterns. */
static bool readable(Source *s) {
    ++s->reads;
    if (s->invalidate_at && s->reads==s->invalidate_at) s->pinned=false;
    return s->pinned;
}
static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    Source *s=context;
    if (!readable(s)) return NM_VIEW_FAULT;
    if (s->interrupted) return NM_VIEW_INTERRUPTED;
    if (s->defer) return NM_VIEW_DEFER;
    if (value.syntax>=1 && value.syntax<=3)
        *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={value.syntax,value.context}};
    else if (value.syntax==10 || (value.syntax>=20 && value.syntax<=23))
        *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=value.syntax};
    else if ((value.syntax>=100 && value.syntax<=104) || value.syntax==200 || value.syntax==201)
        *node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=3};
    else return NM_VIEW_FAULT;
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context, NmHostValue value, uint64_t index, NmHostValue *out) {
    Source *s=context;
    if (!readable(s)) return NM_VIEW_FAULT;
    if (index>2) return NM_VIEW_FAULT;
    const uint64_t edges[][2]={{20,21},{21,22},{20,23},{23,22},{20,21}};
    if (!index) *out=(NmHostValue){10,value.context};
    else if (value.syntax>=100 && value.syntax<=104)
        *out=(NmHostValue){edges[value.syntax-100][index-1],value.context};
    else if (value.syntax==200 || value.syntax==201)
        *out=(NmHostValue){index+(value.syntax==201),value.context};
    else return NM_VIEW_FAULT;
    return NM_VIEW_READY;
}
static bool valid(void *context) { return ((Source *)context)->pinned; }
static NmQueryView view(Source *s) {
    return (NmQueryView){.terms={s,inspect,child,NULL},.pin=s,.valid=valid};
}
static NmQueryLimits limits(void) {
    return (NmQueryLimits){.import={1000,2000,100,10000},.pairs=64,.scan=128,.memo=128};
}
static void trace(void *context, const NmQueryEvent *e) {
    Trace *t=context;
    assert(e->candidate>0 && (!e->count || e->selected));
    if (e->kind==NM_QUERY_PROPOSED) { ++t->proposed;if (!e->allowed) ++t->denied; }
    if (e->kind==NM_QUERY_IMPORTED) { ++t->imported;t->last_import=e->imported; }
    if (e->kind==NM_QUERY_CORE_RESULT) {
        ++t->results;t->last_outcome=e->result.outcome;
        assert(e->result.fuel_used>=t->fuel);t->fuel=e->result.fuel_used;
        if (e->result.outcome==NM_UNIFIED) ++t->matches;
        /* Every success in these trace specimens binds a free query variable. */
        assert(e->truth==(e->result.outcome==NM_UNIFIED ? NM_TRUTH_OPEN :
            e->result.outcome==NM_NO_MATCH ? NM_TRUTH_REFUTED : NM_TRUTH_UNAVAILABLE));
    } else assert(e->truth==NM_TRUTH_UNAVAILABLE);
    if (t->invalidate && e->kind==t->invalidate_kind) t->invalidate->pinned=false;
}
static NmQuery *open_query(const NmUnifyProgram *program, NmExecution mode, Source *source,
                           const NmHostValue *patterns, size_t count, size_t takes,
                           const NmQueryRow *rows, size_t row_count, NmQueryLimits l,
                           uint64_t budget, uint64_t used) {
    char error[256];NmQuery *q=NULL;++queries;
    NmQueryStatus s=nm_query_open(program,mode,view(source),patterns,count,takes,rows,row_count,
        l,budget,used,&q,error,sizeof error);
    if (s!=NM_QUERY_YIELD) fprintf(stderr,"query open: %s (%u)\n",error,(unsigned)s);
    assert(s==NM_QUERY_YIELD && q);return q;
}
static NmQueryStatus next(NmQuery *q, uint64_t quantum, NmQueryAnswer *answer) {
    char error[256];NmQueryStatus s=nm_query_next(q,quantum,answer,error,sizeof error);
    if (s==NM_QUERY_FAULT) fprintf(stderr,"query next: %s\n",error);
    if (s==NM_QUERY_ANSWER) ++answers;
    else { assert(!answer->prepared && !answer->work && !answer->selected);if (s==NM_QUERY_YIELD) ++yields; }
    return s;
}
static bool same_stats(NmQueryStats a, NmQueryStats b) {
    return a.candidates==b.candidates && a.unifier_calls==b.unifier_calls && a.fuel_used==b.fuel_used &&
        a.inspections==b.inspections && a.children==b.children && a.bindings==b.bindings;
}
static void graph(const NmUnifyProgram *program) {
    NmQueryStats reference={0};
    for (unsigned mode=0;mode<3;++mode) {
        Source source={.pinned=true};NmHostValue patterns[]={{200,500},{201,500}};
        NmQueryRow rows[5];for (size_t i=0;i<5;++i) rows[i]=(NmQueryRow){11+i,{100+i,800+i}};
        NmQuery *q=open_query(program,(NmExecution)mode,&source,patterns,2,0,rows,5,limits(),1000000,19);
        Trace t={0};nm_query_set_trace(q,trace,&t);NmQueryAnswer a;
        assert(next(q,0,&a)==NM_QUERY_YIELD && !nm_query_stats(q).candidates);
        unsigned seen=0;
        for (;;) {
            NmQueryStatus s=next(q,1,&a);
            if (s==NM_QUERY_YIELD) continue;
            if (s==NM_QUERY_END) break;
            assert(s==NM_QUERY_ANSWER && a.count==2 && a.takes==0);
            if (a.selected[0]==11 && a.selected[1]==12) seen|=1;
            else if (a.selected[0]==13 && a.selected[1]==14) seen|=2;
            else if (a.selected[0]==15 && a.selected[1]==12) seen|=4;
            else assert(!"unexpected joined graph path");
            const NmUnifyInput *in=nm_match_input(a.prepared);assert(in->variables_count==3);
            NmBindingProjection x,z;
            assert(nm_match_project(a.prepared,&a.result,a.work,0,&x));
            assert(nm_match_project(a.prepared,&a.result,a.work,2,&z));
            assert(x.key.variable==1 && x.value.syntax==20 && x.value.context==800+a.selected[0]-11);
            assert(z.key.variable==3 && z.value.syntax==22 && z.value.context==800+a.selected[1]-11);
            assert(x.changed && z.changed);
        }
        assert(seen==7 && source.pinned && t.proposed==25 && t.imported==25 && t.results==25 && t.matches==3);
        NmQueryStats stats=nm_query_stats(q);
        assert(stats.candidates==25 && stats.unifier_calls==25 && stats.fuel_used==t.fuel && stats.fuel_used>19);
        assert(next(q,1,&a)==NM_QUERY_END && same_stats(stats,nm_query_stats(q)));
        if (!mode) reference=stats;else assert(same_stats(reference,stats));
        nm_query_free(q);
    }
}
static void occurrences(const NmUnifyProgram *program) {
    for (unsigned mode=0;mode<3;++mode) for (size_t takes=0;takes<=2;++takes) {
        Source source={.pinned=true};NmHostValue patterns[]={{1,9},{2,9}};
        NmQueryRow rows[]={{41,{20,100}},{42,{20,101}}};
        NmQuery *q=open_query(program,(NmExecution)mode,&source,patterns,2,takes,rows,2,limits(),1000000,0);
        Trace t={0};nm_query_set_trace(q,trace,&t);NmQueryAnswer a;size_t found=0;
        for (;;) {
            NmQueryStatus s=next(q,3,&a);
            if (s==NM_QUERY_END) break;
            if (s==NM_QUERY_YIELD) continue;
            assert(s==NM_QUERY_ANSWER && a.count==2 && a.takes==takes);++found;
            if (takes) assert(a.selected[0]!=a.selected[1]);
        }
        assert(found==(takes ? 2u : 4u));
        assert(t.proposed==4 && t.denied==(takes ? 2u : 0u));nm_query_free(q);
    }
    for (unsigned mode=0;mode<3;++mode) {
        Source source={.pinned=true};NmQueryAnswer a;
        NmQuery *q=open_query(program,(NmExecution)mode,&source,NULL,0,0,NULL,0,limits(),1000000,7);
        assert(next(q,1,&a)==NM_QUERY_ANSWER && !a.count && a.result.outcome==NM_UNIFIED);
        assert(!source.reads && next(q,1,&a)==NM_QUERY_END);nm_query_free(q);
        NmHostValue pattern={1,3};
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,NULL,0,limits(),0,0);
        assert(next(q,1,&a)==NM_QUERY_END && !nm_query_stats(q).candidates && !source.reads);nm_query_free(q);
    }
}
static void boundaries(const NmUnifyProgram *program) {
    for (unsigned mode=0;mode<3;++mode) {
        Source source={.pinned=true};NmHostValue pattern={1,8};NmQueryRow row={99,{20,70}};NmQueryAnswer a;
        NmQuery *q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,limits(),1,0);
        Trace t={0};nm_query_set_trace(q,trace,&t);
        assert(next(q,1,&a)==NM_QUERY_EXHAUSTED && t.last_outcome==NM_EXHAUSTED);
        NmQueryStats stats=nm_query_stats(q);assert(stats.fuel_used==1);
        assert(next(q,100,&a)==NM_QUERY_EXHAUSTED && same_stats(stats,nm_query_stats(q)));nm_query_free(q);
        source.defer=true;
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,limits(),10000,0);
        t=(Trace){0};nm_query_set_trace(q,trace,&t);
        assert(next(q,1,&a)==NM_QUERY_DEFER && t.last_import==NM_VIEW_DEFER && !t.results);nm_query_free(q);
        source.defer=false;
        source.interrupted=true;
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,limits(),10000,0);
        assert(next(q,1,&a)==NM_QUERY_INTERRUPTED);nm_query_free(q);source.interrupted=false;
        NmQueryLimits l=limits();l.import.nodes=1;
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,l,10000,0);
        assert(next(q,1,&a)==NM_QUERY_RESOURCE);nm_query_free(q);
        l=limits();l.pairs=0;
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,l,10000,0);
        assert(next(q,1,&a)==NM_QUERY_RESOURCE);nm_query_free(q);
        for (unsigned kind=0;kind<3;++kind) {
            source=(Source){.pinned=true};
            q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,limits(),10000,0);
            t=(Trace){.invalidate=&source,.invalidate_kind=(NmQueryEventKind)kind};nm_query_set_trace(q,trace,&t);
            assert(next(q,1,&a)==NM_QUERY_INVALIDATED);
            stats=nm_query_stats(q);assert(next(q,1,&a)==NM_QUERY_INVALIDATED && same_stats(stats,nm_query_stats(q)));
            nm_query_free(q);
        }
        source=(Source){.pinned=true};
        q=open_query(program,(NmExecution)mode,&source,&pattern,1,0,&row,1,limits(),10000,0);
        source.invalidate_at=2;
        assert(next(q,1,&a)==NM_QUERY_INVALIDATED && !nm_query_stats(q).unifier_calls);nm_query_free(q);
    }
    Source source={.pinned=true};NmHostValue pattern={1,8};
    NmQueryRow rows[]={{41,{20,100}},{41,{21,101}}};NmQuery *q=NULL;char error[256];
    assert(nm_query_open(program,NM_NATIVE,view(&source),&pattern,1,0,rows,2,limits(),10000,0,
        &q,error,sizeof error)==NM_QUERY_FAULT && !q);
    rows[1].occurrence=42;
    assert(nm_query_open(program,NM_NATIVE,view(&source),&pattern,1,2,rows,2,limits(),10000,0,
        &q,error,sizeof error)==NM_QUERY_FAULT && !q);
    assert(nm_query_open(program,NM_NATIVE,view(&source),&pattern,1,0,rows,2,limits(),1,2,
        &q,error,sizeof error)==NM_QUERY_FAULT && !q);
    source.pinned=false;
    assert(nm_query_open(program,NM_NATIVE,view(&source),&pattern,1,0,rows,2,limits(),10000,0,
        &q,error,sizeof error)==NM_QUERY_INVALIDATED && !q);
    nm_query_free(NULL);nm_query_set_trace(NULL,NULL,NULL);
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);
    assert(program);graph(program);occurrences(program);boundaries(program);nm_unify_program_close(program);
    printf("native query: %zu sessions, %zu answers, %zu yields; bag joins, exclusive takes, pinned views, traces and terminal resource/fuel outcomes passed\n",
        queries,answers,yields);
    return 0;
}
