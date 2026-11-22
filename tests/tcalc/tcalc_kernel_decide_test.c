#include "../../native/kernel/kernel.h"
#include <assert.h>
#include <time.h>
#define main tcalc_reference_decide_tests
#include "tcalc_decide_test.c"
#undef main

#ifdef CETTA_BATCH_ALLOCATION_TEST
static long allocation_failure=-1;
static bool allocation_failed;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool refuse_allocation(void) {
    if (allocation_failure<0) return false;
    if (!allocation_failure--) { allocation_failure=-1;allocation_failed=true;return true; }
    return false;
}
void *__wrap_malloc(size_t n) { return refuse_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t width) { return refuse_allocation() ? NULL : __real_calloc(n,width); }
void *__wrap_realloc(void *p,size_t n) { return refuse_allocation() ? NULL : __real_realloc(p,n); }

static int resident_allocation_checks(void) {
    size_t controls=0,recovered=0;
    for (long nth=0;nth<4096;++nth) {
        allocation_failure=-1;allocation_failed=false;
        char text[1024]={0};
        for (unsigned i=0;i<9;++i) {
            size_t n=strlen(text);snprintf(text+n,sizeof text-n,"(p v%u) ",i);
        }
        strcat(text,"(rule product (read (p $x) (p $y)) (make (out $x $y)))");
        TcTerms *t=tc_terms_new();assert(t);TcDeclarations decl;TcSyntaxError error;
        TcProgram *p=tc_read_program(t,text,strlen(text),&decl,&error);assert(p);free(decl.region);
        size_t count=0;const TcTerm *rows=tc_program_rows(p,&count);assert(count==9);
        TcInterpretation interpretation=how();interpretation.shortcuts=TC_SHORTCUTS_TAKEN;
        interpretation.budget=100000;
        allocation_failure=nth;
        TcOutcomes *out=tc_successors(p,&interpretation,rows,count);
        allocation_failure=-1;
        if (out && tc_outcomes_finished(out)) {
            assert(tc_outcomes_complete_memory(out) && tc_outcomes_count(out)==81 &&
                !tc_outcomes_shortcuts_disagreed(out));
            if (allocation_failed) ++recovered;
        } else if (out) {
            /* An allocation failure is incomplete work, never quiescence. */
            assert(allocation_failed && !tc_outcomes_complete_memory(out));
        }
        if (!allocation_failed) assert(out && tc_outcomes_finished(out));
        bool finished=!allocation_failed;
        if (allocation_failed) ++controls;
        tc_outcomes_free(out);tc_program_free(p);tc_terms_free(t);
        if (finished) {
            printf("PASS: %zu resident allocation failures; %zu complete recoveries; incomplete work never reports quiescence\n",controls,recovered);
            return 0;
        }
    }
    assert(false);return 1;
}
#endif

static void constraint_bundle_checks(void) {
    /* More than the inline pending capacity, with one scoped solution. */
    char row[1024]="(row",pattern[1024]="(row",answer[1024]="(out",output[1024]="(out";
    for (unsigned i=0;i<48;++i) {
        size_t n=strlen(row);snprintf(row+n,sizeof row-n," %u",i+1);
        n=strlen(pattern);snprintf(pattern+n,sizeof pattern-n," $v%u",i);
        n=strlen(answer);snprintf(answer+n,sizeof answer-n," %u",i+1);
        n=strlen(output);snprintf(output+n,sizeof output-n," $v%u",i);
    }
    strcat(row,")");strcat(pattern,")");strcat(answer,")");strcat(output,")");
    char text[4096];snprintf(text,sizeof text,"%s (rule bundle (take %s) (make %s))",row,pattern,output);
    TcTerms *terms;Store store;size_t made;
    TcProgram *p=program_of(text,&terms,&store);
    if (p) {
        TcOccurrence selected[]={find(terms,&store,row)};
        check(decide(p,0,&store,selected,1,terms,&made)==TC_DECIDED_ADMITTED && made==1,
              "48 unresolved equations are admitted together");
        check(store.commits==1 && live_count(&store,1) && find(terms,&store,answer)!=999,
              "the complete binding bundle constructs one output and commits once");
        tc_program_free(p);tc_terms_free(terms);
    }
    p=program_of("(row 1 2) (rule repeated (take (row $x $x)) (make bad)) "
                 "(rule retry (take (row $x $y)) (make (out $x $y)))",&terms,&store);
    if (p) {
        TcOccurrence selected[]={find(terms,&store,"(row 1 2)")};
        check(decide(p,0,&store,selected,1,terms,&made)==TC_DECIDED_DECLINED && !store.commits,
              "incompatible equations for one variable refuse without publication");
        check(decide(p,1,&store,selected,1,terms,&made)==TC_DECIDED_ADMITTED &&
              store.commits==1 && find(terms,&store,"(out 1 2)")!=999,
              "a refused bundle does not poison a subsequent firing");
        tc_program_free(p);tc_terms_free(terms);
    }
    p=program_of("(token) (rule cycle (take (token)) "
                 "(tell (= (pair $x $y) (pair (f $y) (f $x)))) (make bad))",&terms,&store);
    if (p) {
        TcOccurrence selected[]={0};
        check(decide(p,0,&store,selected,1,terms,&made)==TC_DECIDED_DECLINED &&
              !store.commits && live_count(&store,1),
              "the joint occurs check refuses a cycle between pending equations");
        tc_program_free(p);tc_terms_free(terms);
    }
}

static void resident_pool_checks(CettaKernelMode mode) {
    char text[4096]={0};
    for (unsigned i=0;i<20;++i) {
        size_t n=strlen(text);snprintf(text+n,sizeof text-n,"(p v%u) ",i);
    }
    strcat(text,"(rule product (read (p $x) (p $y)) (make (out $x $y)))");
    TcTerms *t=tc_terms_new();assert(t);TcDeclarations decl;TcSyntaxError error;
    TcProgram *p=tc_read_program(t,text,strlen(text),&decl,&error);assert(p);free(decl.region);
    size_t count=0;const TcTerm *rows=tc_program_rows(p,&count);assert(count==20);
    TcInterpretation interpretation=how();interpretation.shortcuts=TC_SHORTCUTS_TAKEN;
    CettaKernelStats before=cetta_kernel_stats();
    TcOutcomes *out=tc_successors(p,&interpretation,rows,count);assert(out && tc_outcomes_finished(out));
    CettaKernelStats after=cetta_kernel_stats();
    check(tc_outcomes_count(out)==400,"resident query enumerates all 400 ordered read pairs");
    TcTerm head=tc_symbol(t,"out",3),values[20];bool seen[400]={0};
    for (unsigned i=0;i<20;++i) { char name[16];int n=snprintf(name,sizeof name,"v%u",i);values[i]=tc_symbol(t,name,(size_t)n); }
    for (size_t i=0;i<tc_outcomes_count(out);++i) {
        size_t n=0;const TcTerm *state=tc_outcome_rows(out,i,&n);assert(n==21);
        unsigned found=0;
        for (size_t j=0;j<n;++j) if (tc_kind(t,state[j])==TC_EXPRESSION && tc_length(t,state[j])==3 && tc_item(t,state[j],0)==head) {
            unsigned a=0,b=0;
            while (a<20 && values[a]!=tc_item(t,state[j],1)) ++a;
            while (b<20 && values[b]!=tc_item(t,state[j],2)) ++b;
            assert(a<20 && b<20 && !seen[20*a+b]);seen[20*a+b]=true;++found;
        }
        assert(found==1);
    }
    for (size_t i=0;i<400;++i) assert(seen[i]);
    check(true,"source pools may grow between result blocks without stale borrowed pointers");
    if (mode==CETTA_KERNEL_X86) {
        check(after.x86_calls>before.x86_calls+1 && after.imports==before.imports,
              "resident x86 executes multiple result blocks with zero graph imports");
    }
    tc_outcomes_free(out);
    TcTerm local=tc_local(t,0),phead=tc_symbol(t,"p",1),extra=tc_symbol(t,"extra",5);
    TcTerm pattern_items[]={phead,local},made_items[]={extra,local};
    TcTerm pattern=tc_expression(t,pattern_items,2),made=tc_expression(t,made_items,2);
    TcRule added={.name="added",.read=&pattern,.reads=1,.make=&made,.makes=1,.locals=1};
    assert(tc_program_add_rule(p,&added));rows=tc_program_rows(p,&count);
    out=tc_successors(p,&interpretation,rows,count);assert(out);
    check(tc_outcomes_finished(out) && tc_outcomes_count(out)==420,
          "appending a rule extends the prepared prefix without a stale plan");
    tc_outcomes_free(out);tc_program_free(p);tc_terms_free(t);
}

static double benchmark_seconds(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int resident_benchmark(CettaKernelMode mode,bool product,bool all) {
    char source[4096]={0};size_t size;
    if (product) {
        for (unsigned i=0;i<20;++i) {
            size_t n=strlen(source);snprintf(source+n,sizeof source-n,"(p v%u) ",i);
        }
        strcat(source,"(rule product (read (p $x) (p $y)) (make (out $x $y)))");size=strlen(source);
    } else {
        FILE *file=fopen("tests/tcalc/tiles_2x3.tcalc","rb");assert(file);
        size=fread(source,1,sizeof source,file);assert(!ferror(file) && feof(file));fclose(file);
    }
    TcTerms *t=tc_terms_new();assert(t);TcDeclarations decl;TcSyntaxError error;
    TcProgram *p=tc_read_program(t,source,size,&decl,&error);assert(p);free(decl.region);
    TcInterpretation interpretation=how();interpretation.observe=TC_OBSERVE_OUTCOMES;
    interpretation.shortcuts=TC_SHORTCUTS_TAKEN;interpretation.budget=100000;
    double samples[3][7]={{0}};char selection_error[256];
    if (all) assert(cetta_kernel_select(CETTA_KERNEL_X86,selection_error,sizeof selection_error));
    for (unsigned sample=0;sample<8;++sample) for (unsigned which=0;which<(all ? 3u : 1u);++which) {
        CettaKernelMode current=all ? (CettaKernelMode)((sample+which)%3) : mode;
        assert(cetta_kernel_select(current,selection_error,sizeof selection_error));
        double begin=benchmark_seconds();
        for (unsigned n=0;n<100;++n) {
            size_t count=0;const TcTerm *rows=tc_program_rows(p,&count);
            TcOutcomes *out=product ? tc_successors(p,&interpretation,rows,count) : tc_explore(p,&interpretation);assert(out);
            if (product) assert(tc_outcomes_count(out)==400 && tc_outcomes_finished(out));
            else assert(tc_outcomes_states(out)==360 && tc_outcomes_finished(out) && tc_outcomes_cyclic(out) && !tc_outcomes_count(out));
            assert(!tc_outcomes_shortcuts_disagreed(out));tc_outcomes_free(out);
        }
        if (sample) samples[current][sample-1]=(benchmark_seconds()-begin)/100;
    }
    for (unsigned current=0;current<3;++current) if (all || current==(unsigned)mode) {
        for (unsigned i=1;i<7;++i) for (unsigned j=i;j && samples[current][j]<samples[current][j-1];--j) {
            double v=samples[current][j];samples[current][j]=samples[current][j-1];samples[current][j-1]=v;
        }
        printf("{\"case\":\"%s\",\"mode\":\"%s\",\"median_ms\":%.6f,\"runs_per_sample\":100,\"samples\":7,\"paired_process\":%s,\"checked\":true}\n",
            product ? "400-output product; resident program and term pool" : "tile puzzle; resident program and term pool",
            cetta_kernel_name((CettaKernelMode)current),1000*samples[current][3],all ? "true" : "false");
    }
    tc_program_free(p);tc_terms_free(t);cetta_kernel_shutdown();return 0;
}

int main(int argc,char **argv) {
    assert(argc==2 || (argc==3 && (!strcmp(argv[2],"--bench") || !strcmp(argv[2],"--bench-product") || !strcmp(argv[2],"--bench-all") || !strcmp(argv[2],"--bench-product-all"))));CettaKernelMode mode;char error[256];
    assert(cetta_kernel_mode(argv[1],&mode) && cetta_kernel_select(mode,error,sizeof error));
#ifdef CETTA_BATCH_ALLOCATION_TEST
    if (argc==2) { int result=resident_allocation_checks();cetta_kernel_shutdown();return result; }
#endif
    if (argc==3) return resident_benchmark(mode,strstr(argv[2],"product")!=NULL,strstr(argv[2],"all")!=NULL);
    cetta_kernel_enable_stats();
    int result=tcalc_reference_decide_tests();
    int bundle_start=checks;constraint_bundle_checks();
    if (!failures) printf("PASS: scoped native constraint bundles: %d checks\n",checks-bundle_start);
    resident_pool_checks(mode);
    CettaKernelStats stats=cetta_kernel_stats();
    if (mode==CETTA_KERNEL_X86) assert(stats.x86_calls && !stats.c_calls);
    cetta_kernel_shutdown();return result || failures ? 1 : 0;
}
