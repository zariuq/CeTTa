#include "atom.h"
#include "effect_policy.h"
#include "eval.h"
#include "foreign.h"
#include "library.h"
#include "parser.h"
#include "space.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Arena persistent, scratch;
static Space space;
static Registry registry;
static CettaLibraryContext *context;

static Atom *parse(const char *text) {
    size_t position=0;
    Atom *a=parse_sexpr(&persistent,text,&position);
    assert(a && position==strlen(text)); return a;
}
static void check_denials(const char *text, const char *expected, uint64_t denials) {
    EvalOutcome result; eval_outcome_init(&result);
    int fuel=context->session.options.fuel_limit;
    eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse(text),10000,&result);
    assert(!context->session.speculative && !cetta_speculative_active());
    assert(eval_current_library_context()==context);
    assert(context->session.options.fuel_limit==fuel);
    if (result.results.len!=1 || !atom_eq(result.results.items[0],parse(expected))) {
        fprintf(stderr,"unexpected speculative result for %s: ",text);
        for (CettaCount i=0;i<result.results.len;++i) atom_print(result.results.items[i],stderr);
        fputc('\n',stderr); abort();
    }
    assert(result.effect_denials==denials);
    assert(result.completion==(denials ? CETTA_EVAL_INCOMPLETE_EFFECT_DENIED : CETTA_EVAL_COMPLETE));
    assert(result.budget_limited && result.budget_initial==10000);
    eval_outcome_free(&result);
}
static void check(const char *text, const char *expected) {
    check_denials(text,expected,0);
}
static void denied(const char *text) {
    check_denials(text,"(Error speculative-evaluation EffectNotAllowed)",1);
}
int main(int argc,char **argv) {
    assert(argc==4);
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&space);
    registry_init(&registry); registry_bind(&registry,"&self",atom_space(&persistent,&space));
    context=calloc(1,sizeof(*context)); assert(context);
    cetta_library_context_init(context); cetta_library_context_set_exec_path(context,argv[0]);
    cetta_eval_session_init_he_extended(&context->session);
    eval_set_library_context(context);
    const char *modules[]={"io","fs","system","str","rhometta","durable","proc"};
    for (size_t i=0;i<sizeof(modules)/sizeof(*modules);++i) {
        Atom *error=NULL;
        if (!cetta_library_import(context,modules[i],&space,&scratch,&persistent,&registry,10000,&error)) {
            fprintf(stderr,"import %s: ",modules[i]); if (error) atom_print(error,stderr); abort();
        }
    }
#if CETTA_BUILD_WITH_PYTHON
    Atom *foreign_error=NULL;
    if (!cetta_foreign_load_module(context->foreign_runtime,argv[3],&space,&persistent,&foreign_error)) {
        if (foreign_error) atom_print(foreign_error,stderr);
        abort();
    }
#endif
    space_add(&space,parse("(= (computed $op) ($op \"SHOULD-NOT-PRINT\"))"));
    space_add(&space,parse("(record 42)"));
    check("(+ 1 2)","3");
    check("(match &self (record $x) (proposal $x))","(proposal 42)");
    check("(quote (io:submit (http:request GET private () body 1000 32)))",
          "(quote (io:submit (http:request GET private () body 1000 32)))");
    check("(if True (quote (telegram:send chat text)) (println! wrong))","(quote (telegram:send chat text))");
    check("(str:concat \"ab\" \"cd\")","\"abcd\"");
    denied("(println! secret)");
    denied("(computed println!)");
    denied("(let $op println! ($op secret))");
    denied("(metta (println! secret) %Undefined% &self)");
    denied("(evalc (println! secret) &self)");
    denied("(let $f capture ($f (println! secret)))");
    denied("(case key ((key (println! secret)) ($x unused)))");
    check_denials("(collapse (superpose ((println! first) kept (println! second))))", "(kept)",2);
    check_denials("(collapse (superpose ((Error speculative-evaluation EffectNotAllowed) kept)))", "(kept)",0);
    denied("(import! &self fs)");
    denied("(pragma! fuel 1)");
    denied("(add-atom &self (record 43))");
    check("(collapse (match &self (record $x) $x))","(42)");
    denied("(new-state 1)");
    denied("(system:monotonic-ns)");
    denied("(__cetta_lib_system_monotonic_ns ())");
    denied("(__cetta_lib_durable_read \"dummy\" inbox)");
    denied("(__cetta_lib_future_operation ())");
    denied("(py-call anything ())");
#if CETTA_BUILD_WITH_PYTHON
    denied("(speculative-write)");
    denied("(let $fn speculative-write ($fn))");
#endif
    denied("(__cetta_lib_prolog_query anything ())");
    char expr[8192];
    snprintf(expr,sizeof(expr),"(fs:write \"%s\" \"forbidden\")",argv[2]); denied(expr);
    snprintf(expr,sizeof(expr),"(proc:run (\"/usr/bin/touch\" \"%s\") \"/\" () 1000 10)",argv[2]); denied(expr);
    snprintf(expr,sizeof(expr),"(io:submit (http:request \"GET\" \"%s/counted\" () \"\" 1000 100))",argv[1]); denied(expr);
    char branch[9000];
    snprintf(branch,sizeof(branch),"(collapse (superpose (kept %s)))",expr);
    check_denials(branch,"(kept)",1);
    snprintf(branch,sizeof(branch),"(collapse (hyperpose (kept %s)))",expr);
    check_denials(branch,"(kept)",1);
    snprintf(branch,sizeof(branch),
        "(collapse (rhometta:transitions (rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) "
        "(rho:send (rho:quote rho:nil) (rhometta:eval %s)))))",expr);
    check_denials(branch,"((rho:val (Error speculative-evaluation EffectNotAllowed)))",1);
    check("(collapse (rhometta:transitions (rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) (rho:send (rho:quote rho:nil) (rhometta:eval (+ 2 3))))))",
          "((rho:val 5))");
    /* Finite fuel selects cooperative evaluation even with two threads
     * configured. The denial count remains shared for any future worker path. */
    assert(cetta_eval_session_record_generic_setting(&context->session,"num-threads",
        CETTA_EVAL_OPTION_VALUE_INT,"2",2));
    check("(collapse (hyperpose ((+ 1 1) (+ 2 2))))","(2 4)");
    snprintf(branch,sizeof(branch),"(collapse (hyperpose (kept %s)))",expr);
    check_denials(branch,"(kept)",1);
    snprintf(branch,sizeof(branch),"(collapse (hyperpose (kept (let $op io:submit ($op (http:request \"GET\" \"%s/counted\" () \"\" 1000 100))))))",argv[1]);
    check_denials(branch,"(kept)",1);
    /* Unsupported evaluator profiles fail before starting the expression. */
    CettaLanguageId saved_language=context->session.language_id;
    context->session.language_id=CETTA_LANGUAGE_PETTA;
    EvalOutcome refused; eval_outcome_init(&refused);
    eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse("(println! secret)"),10000,&refused);
    assert(refused.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && refused.results.len==1);
    assert(atom_is_error(refused.results.items[0]) && !context->session.speculative);
    eval_outcome_free(&refused); context->session.language_id=saved_language;
    /* An unlimited/zero budget is refused without evaluating even pure code. */
    for (int fuel=-2;fuel<=0;++fuel) {
        eval_outcome_init(&refused);
        eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse("(+ 1 2)"),fuel,&refused);
        assert(refused.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        assert(refused.effect_denials==0 && refused.steps_spent==0 && !refused.budget_limited);
        assert(!context->session.speculative && context->session.options.fuel_limit==-1);
        eval_outcome_free(&refused);
    }
    /* Neither direct recursion nor a nested rho payload can renew the purse. */
    space_add(&space,parse("(= (forever $n) (forever (+ $n 1)))"));
    const char *runaways[]={"(forever 0)",
        "(collapse (hyperpose ((forever 0) kept)))",
        "(collapse (rhometta:transitions (rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) (rho:send (rho:quote rho:nil) (rhometta:eval (forever 0))))))"};
    for (size_t i=0;i<sizeof(runaways)/sizeof(*runaways);++i) {
        eval_outcome_init(&refused);
        eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse(runaways[i]),100,&refused);
        assert(refused.completion==CETTA_EVAL_INCOMPLETE_FUEL && refused.effect_denials==0);
        assert(refused.budget_limited && refused.budget_initial==100 && refused.budget_remaining==0);
        assert(refused.steps_spent==100);
        assert(!context->session.speculative && context->session.options.fuel_limit==-1);
        eval_outcome_free(&refused);
    }
    /* A denial is retained even if fuel exhaustion subsequently takes priority. */
    eval_outcome_init(&refused);
    eval_top_speculative(context,&space,&scratch,&persistent,&registry,
        parse("(collapse (superpose ((println! denied) (forever 0))))"),100,&refused);
    assert(refused.completion==CETTA_EVAL_INCOMPLETE_FUEL && refused.effect_denials==1);
    eval_outcome_free(&refused);
    check("(+ 2 3)","5"); /* A new evaluation starts with a clean counter. */
    /* A host-nested scope preserves the outer count and budget. */
    context->session.speculative=true;
    context->session.options.fuel_limit=4321;
    atomic_store(&context->session.effect_denials,7);
    eval_outcome_init(&refused);
    eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse("(println! secret)"),100,&refused);
    assert(refused.effect_denials==1 && atomic_load(&context->session.effect_denials)==8);
    assert(context->session.speculative && context->session.options.fuel_limit==4321);
    eval_outcome_free(&refused);
    atomic_store(&context->session.effect_denials,UINT64_MAX);
    eval_outcome_init(&refused);
    eval_top_speculative(context,&space,&scratch,&persistent,&registry,parse("(println! secret)"),100,&refused);
    assert(refused.effect_denials==UINT64_MAX);
    assert(refused.completion==CETTA_EVAL_INCOMPLETE_EFFECT_DENIED);
    eval_outcome_free(&refused);
    context->session.speculative=false;
    context->session.options.fuel_limit=-1;
    /* Ordinary evaluation remains enabled after the policy scope exits. */
    ResultSet ordinary; result_set_init(&ordinary);
    eval_top_with_registry(&space,&scratch,&persistent,&registry,parse("(new-state 1)"),&ordinary);
    assert(ordinary.len==1 && ordinary.items[0]->kind==ATOM_GROUNDED && !atom_is_error(ordinary.items[0]));
    result_set_free(&ordinary);
    eval_set_library_context(NULL); cetta_library_context_free(context); free(context);
    registry_free(&registry); space_free(&space); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("speculative HE: pure proposals, quoted data, computed/foreign calls, nested choices/rho, host-visible denials, bounded recursion, unsupported language and authority restoration passed");
    return 0;
}
