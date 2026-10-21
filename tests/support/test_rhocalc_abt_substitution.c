#define _POSIX_C_SOURCE 200809L
#define CETTA_TEST_HOOKS 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "parser.h"
#include "eval.h"

#include "atom.h"
#include "rhocalc_core.h"
#include "symbol.h"
#include "abt.h"
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"

/* Compile the real key owner so allocation controls can reach its failure
 * boundaries without adding hooks to production code. Other allocators keep
 * their ordinary implementation. The counter is local to this test thread. */
static _Thread_local size_t key_allocations;
static _Thread_local size_t key_fail_at;
static _Thread_local bool key_failure_injected;
static void *key_test_realloc(void *pointer, size_t size) {
    if (key_fail_at && ++key_allocations == key_fail_at) {
        key_failure_injected = true;
        return NULL;
    }
    return realloc(pointer, size);
}
#ifndef CETTA_RHO_TEST_CORE_SOURCE
#define CETTA_RHO_TEST_CORE_SOURCE "../../src/rhocalc_core.c"
#endif
#define realloc key_test_realloc
#include CETTA_RHO_TEST_CORE_SOURCE
#undef realloc

static uint64_t payload_callbacks;
CettaEvalCompletion __real_eval_top_with_registry_completion(
    Space *, Arena *, Arena *, Registry *, Atom *, ResultSet *);
CettaEvalCompletion __wrap_eval_top_with_registry_completion(Space *space, Arena *arena, Arena *persistent,
                                  Registry *registry, Atom *expression, ResultSet *results) {
    payload_callbacks++;
    return __real_eval_top_with_registry_completion(space,arena,persistent,registry,expression,results);
}

static unsigned retained_checks;
static const char *retained_current;
#define RETAIN_CHECK(condition) do { \
    retained_checks++; \
    if (!(condition)) { \
        fprintf(stderr,"retained rho check %u failed at line %u: %s\n", \
            retained_checks,(unsigned)__LINE__,#condition); \
        const char *diagnostic=rhocalc_last_validation_error(); \
        fprintf(stderr,"case: %s; diagnostic: %s\n",retained_current ? retained_current : "none",diagnostic ? diagnostic : "none"); \
        return false; \
    } \
} while (0)

static Atom *read_process(Arena *arena, const char *text) {
    size_t position=0u;
    Atom *process=parse_sexpr(arena,text,&position);
    return process && position==strlen(text) ? process : NULL;
}

typedef enum {
    KEY_RHO_NORMALIZE, KEY_RHO_FRONTIER, KEY_RHO_QUIESCENT, KEY_RHO_RUN,
    KEY_COST_NORMALIZE, KEY_COST_FRONTIER, KEY_COST_TRACE, KEY_COST_RUN,
    KEY_RHO_MACRO
} KeyFaultOperation;

static bool key_fault_operation(Arena *arena, Atom *process,
                                KeyFaultOperation operation,
                                const RhocalcEvalContext *context) {
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,8u,1u,false};
    RhoReductionResult reduced={0};
    switch (operation) {
    case KEY_RHO_NORMALIZE: return rho_normalize_proc(arena,process)!=NULL;
    case KEY_RHO_FRONTIER:
        return rhocalc_successor_frontier_expr_with_eval_context(arena,process,context)!=NULL;
    case KEY_RHO_QUIESCENT:
        return rhocalc_quiescent_frontier_expr_with_eval_context(arena,process,context)!=NULL;
    case KEY_RHO_RUN:
        return rhocalc_reduce_to_quiescence_with_eval_context(arena,process,&profile,context,&reduced);
    case KEY_COST_NORMALIZE: return rhocost_normalize_term(arena,process)!=NULL;
    case KEY_COST_FRONTIER: return rhocalc_cost_step_frontier_expr(arena,process)!=NULL;
    case KEY_COST_TRACE: return rhocalc_cost_causal_trace_expr(arena,process)!=NULL;
    case KEY_COST_RUN:
        return rhocalc_reduce_to_quiescence_with_semantic_profile(
            arena,process,RHOCALC_SEMANTIC_PROFILE_COST,&profile,&reduced);
    case KEY_RHO_MACRO: {
        RhoQuietMacroMode mode;
        RhoSuccessorSetAcc successors;
        rho_successor_set_acc_init(&successors);
        bool ok=rhocalc_try_quiet_macro_step(arena,process,context,&mode,&successors);
        rho_successor_set_acc_free(&successors);
        return ok;
    }
    }
    return false;
}

static size_t key_failure_sites;
static bool key_fault_sweep(const char *text, KeyFaultOperation operation,
                            const RhocalcEvalContext *context) {
    size_t total=0u;
    uint64_t ordinary_callbacks=0u;
    bool failed_after_payload=false;
    retained_current=text;
    for (size_t fail=0u;fail<=total;fail++) {
        Arena arena;
        arena_init(&arena);
        Atom *process=read_process(&arena,text);
        RETAIN_CHECK(process);
        key_allocations=0u;
        key_failure_injected=false;
        key_fail_at=fail ? fail : SIZE_MAX;
        payload_callbacks=0u;
        bool success=key_fault_operation(&arena,process,operation,context);
        key_fail_at=0u;
        if (!fail) {
            total=key_allocations;
            key_failure_sites+=total;
            ordinary_callbacks=payload_callbacks;
            RETAIN_CHECK(success && total>0u && !key_failure_injected);
            if (operation==KEY_RHO_MACRO) RETAIN_CHECK(ordinary_callbacks==1u);
        } else {
            if (success || !key_failure_injected)
                fprintf(stderr,"key allocation sweep: operation=%u failure=%zu/%zu\n",
                    (unsigned)operation,fail,total);
            RETAIN_CHECK(key_failure_injected && !success);
            if (payload_callbacks) failed_after_payload=true;
        }
        if (context && payload_callbacks>ordinary_callbacks)
            fprintf(stderr,"payload allocation sweep: operation=%u failure=%zu/%zu callbacks=%llu\n",
                (unsigned)operation,fail,total,(unsigned long long)payload_callbacks);
        if (context) RETAIN_CHECK(payload_callbacks<=ordinary_callbacks);
        arena_free(&arena);
    }
    if (context) RETAIN_CHECK(failed_after_payload);
    return true;
}

static bool key_failure_tests(void) {
    retained_current="failed key prefixes and exact allocation boundaries";
    char long_text[1025];
    memset(long_text,'k',sizeof(long_text)-1u);
    long_text[sizeof(long_text)-1u]='\0';
    RhoStr text={0};
    RETAIN_CHECK(rho_str_append(&text,"allocated prefix"));
    size_t before=text.len;
    key_allocations=0u;key_failure_injected=false;key_fail_at=1u;
    bool appended=rho_str_append(&text,long_text);
    bool resumed=rho_str_append(&text,"suffix");
    key_fail_at=0u;
    RETAIN_CHECK(!appended && !resumed && key_failure_injected &&
        text.failed && text.len==before);
    RETAIN_CHECK(rho_str_take(&text,"")==NULL);
    RhoStr overflow={.len=SIZE_MAX};
    RETAIN_CHECK(!rho_str_reserve(&overflow,1u) && overflow.failed);
    RETAIN_CHECK(rho_str_take(&overflow,"")==NULL);

    Arena arena;
    arena_init(&arena);
    Atom *value=rho_value_proc(&arena,atom_string(&arena,long_text));
    for (size_t fail=1u;fail<=2u;fail++) {
        key_allocations=0u;key_failure_injected=false;key_fail_at=fail;
        char *key=rho_key_proc(value);
        key_fail_at=0u;
        RETAIN_CHECK(key_failure_injected && !key);
    }
    Atom *receive=read_process(&arena,"(rho:recv (rho:quote rho:nil) $x (rho:drop $x))");
    RETAIN_CHECK(receive);
    RhoAlphaEnv env;
    rho_alpha_init(&env);
    env.next_index=UINT32_MAX;
    rho_key_proc_into(receive,&env,&text);
    RETAIN_CHECK(text.failed && !env.len && env.next_index==UINT32_MAX);
    RETAIN_CHECK(rho_str_take(&text,"")==NULL);
    rho_alpha_free(&env);
    arena_free(&arena);

    const char *process="(rho:par (rho:send (rho:quote rho:nil) rho:nil) "
        "(rho:send (rho:quote rho:nil) (rho:send (rho:quote rho:nil) rho:nil)) "
        "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)))";
    const char *cost="(rho:cost:par "
        "(rho:cost:signed (rho:recv alice $x (rho:cost:signed rho:nil result)) alice) "
        "(rho:cost:signed (rho:send alice (rho:cost:signed rho:nil payload)) bob) "
        "(rho:cost:purse alice (rho:cost:stack-cons "
        "(rho:cost:sig-mul alice bob) rho:cost:stack-empty)))";
    for (unsigned operation=KEY_RHO_NORMALIZE;operation<=KEY_COST_RUN;operation++)
        RETAIN_CHECK(key_fault_sweep(operation<KEY_COST_NORMALIZE ? process : cost,
                                    (KeyFaultOperation)operation,NULL));
    Space world;Registry registry;Arena persistent;
    space_init(&world);registry_init(&registry);arena_init(&persistent);
    RhocalcEvalContext context={.space=&world,.registry=&registry,.persistent_arena=&persistent};
    const char *payload="(rho:par (rho:send (rho:quote rho:nil) "
        "(rho:eval-payload (quote (quote (rho:par (rho:val a) (rho:val b)))))) "
        "(rho:recv (rho:quote rho:nil) $x (rho:par (rho:val $x) (rho:val 8))))";
    for (unsigned operation=KEY_RHO_FRONTIER;operation<=KEY_RHO_RUN;operation++)
        RETAIN_CHECK(key_fault_sweep(payload,(KeyFaultOperation)operation,&context));
    RETAIN_CHECK(key_fault_sweep(payload,KEY_RHO_MACRO,&context));
    registry_free(&registry);space_free(&world);arena_free(&persistent);
    printf("PASS: key allocation failure never publishes a prefix or empty frontier "
           "(%zu failure sites, no added payload executions)\n",key_failure_sites);
    return true;
}

static bool same_account(RhocalcRunAccount a, RhocalcRunAccount b) {
    return a.driver_steps==b.driver_steps && a.selector_steps==b.selector_steps &&
        a.matching_pairs==b.matching_pairs && a.body_candidates==b.body_candidates &&
        a.completed_scans==b.completed_scans && a.reductions_taken==b.reductions_taken &&
        a.comparison_component_probes==b.comparison_component_probes &&
        a.comparison_text_probes==b.comparison_text_probes &&
        a.preparation_steps==b.preparation_steps && a.channel_scan_probes==b.channel_scan_probes;
}

/* Neither a stack context nor the input arena remains alive after opening.
 * The context's pointed-to world/registry/persistent owners remain held. */
static RhocalcRun *open_detached(Arena *result, Atom *input,
                                const RhoRuntimeProfile *profile,
                                const RhocalcEvalContext *context) {
    Arena source;
    arena_init(&source);
    Atom *process=atom_deep_copy(&source,input);
    _Atomic(RhocalcPayloadFailure) diagnostic;
    atomic_init(&diagnostic,RHOCALC_PAYLOAD_FAILURE_NONE);
    RhocalcEvalContext temporary=context ? *context : (RhocalcEvalContext){0};
    temporary.payload_failure=&diagnostic;
    RhocalcRun *run=process ? rhocalc_run_open(result,process,profile,
        context ? &temporary : NULL) : NULL;
    arena_free(&source);
    return run;
}

static bool retained_case(const char *text, const RhocalcEvalContext *context,
                          uint32_t limit, RhoSchedulerPolicy policy) {
    retained_current=text;
    Arena result;
    arena_init(&result);
    RhoRuntimeProfile profile={policy,limit,1u,false};
    Atom *process=read_process(&result,text);
    RETAIN_CHECK(process);
    RhoReductionResult oracle={0}, synchronous={0};
    /* Materialize and sort the full frontier; this bypasses both optimized
     * selector paths. The common commit/export driver is deliberately shared. */
    RETAIN_CHECK(setenv("CETTA_RHO_CANONICAL_EXHAUSTIVE_ORACLE","1",1)==0);
    RETAIN_CHECK(rhocalc_reduce_to_quiescence_with_eval_context(
        &result,process,&profile,context,&oracle));
    RETAIN_CHECK(unsetenv("CETTA_RHO_CANONICAL_EXHAUSTIVE_ORACLE")==0);
    RETAIN_CHECK(rhocalc_reduce_to_quiescence_with_eval_context(
        &result,process,&profile,context,&synchronous));
    RETAIN_CHECK(oracle.reductions_taken==synchronous.reductions_taken &&
        oracle.status==synchronous.status && atom_eq(oracle.residual,synchronous.residual));
    RhocalcRunAccount reference={0};
    uint64_t reference_callbacks=0u;
    const uint64_t allowances[]={1u,2u,7u,31u,UINT64_MAX};
    for (size_t cut=0u;cut<sizeof(allowances)/sizeof(*allowances);cut++) {
        RhocalcRun *run=open_detached(&result,process,&profile,context);
        RETAIN_CHECK(run);
        payload_callbacks=0u;
        uint64_t total=0u,used=9u;
        RhoReductionResult observed={.residual=process,.reductions_taken=777u};
        RETAIN_CHECK(!rhocalc_run_result(run,&observed) &&
            observed.residual==process && observed.reductions_taken==777u);
        RhocalcRunStatus state;
        do {
            RhocalcRunAccount before=rhocalc_run_account(run);
            RETAIN_CHECK(rhocalc_run_poll(run,0u,&used)==RHOCALC_RUN_PENDING && !used);
            RETAIN_CHECK(same_account(before,rhocalc_run_account(run)));
            state=rhocalc_run_poll(run,allowances[cut],&used);
            RETAIN_CHECK(state!=RHOCALC_RUN_FAILED && used && used<=allowances[cut]);
            total+=used;
            RETAIN_CHECK(rhocalc_run_account(run).driver_steps==total);
            if (state==RHOCALC_RUN_PENDING)
                RETAIN_CHECK(!rhocalc_run_result(run,&observed));
        } while (state==RHOCALC_RUN_PENDING);
        RhocalcRunAccount account=rhocalc_run_account(run);
        RETAIN_CHECK(rhocalc_run_result(run,&observed));
        RETAIN_CHECK(observed.reductions_taken==oracle.reductions_taken &&
            observed.status==oracle.status && atom_eq(observed.residual,oracle.residual));
        RETAIN_CHECK(!rhocalc_run_error(run) &&
            rhocalc_run_payload_failure(run)==RHOCALC_PAYLOAD_FAILURE_NONE);
        if (policy==RHO_SCHEDULER_CANONICAL)
            RETAIN_CHECK(account.completed_scans==(uint64_t)observed.reductions_taken+1u &&
                account.selector_steps>=account.completed_scans);
        if (!cut) {reference=account;reference_callbacks=payload_callbacks;}
        else RETAIN_CHECK(same_account(reference,account) && payload_callbacks==reference_callbacks);
        RETAIN_CHECK(rhocalc_run_poll(run,9u,&used)==RHOCALC_RUN_DONE && !used &&
            same_account(account,rhocalc_run_account(run)));
        Atom *saved=observed.residual;
        rhocalc_run_close(run);
        RETAIN_CHECK(atom_eq(saved,oracle.residual));
    }
    /* Closing at every early phase must release the actual retained cursor. */
    for (uint64_t cut=0u;cut<reference.driver_steps && cut<24u;cut++) {
        RhocalcRun *run=open_detached(&result,process,&profile,context);
        RETAIN_CHECK(run);
        rhocalc_run_poll(run,cut,NULL);
        rhocalc_run_close(run);
    }
    arena_free(&result);
    return true;
}

static bool exact_occurrence_case(const char *text, const RhocalcEvalContext *context,
                                  uint64_t pairs, uint64_t bodies, uint64_t callbacks,
                                  uint32_t body, uint64_t ordinal) {
    retained_current=text;
    Arena arena;
    arena_init(&arena);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
    RhocalcRun *run=open_detached(&arena,read_process(&arena,text),&profile,context);
    RETAIN_CHECK(run);
    RhocalcCommAddress address={0};
    RETAIN_CHECK(!rhocalc_run_last_comm(run,&address));
    payload_callbacks=0u;
    RhocalcRunStatus status;
    do {
        uint32_t reductions=rhocalc_run_account(run).reductions_taken;
        if (!reductions) RETAIN_CHECK(!rhocalc_run_last_comm(run,&address));
        status=rhocalc_run_poll(run,1u,NULL);
        RETAIN_CHECK(status!=RHOCALC_RUN_FAILED);
    } while (status==RHOCALC_RUN_PENDING);
    RhocalcRunAccount account=rhocalc_run_account(run);
    RETAIN_CHECK(account.matching_pairs==pairs && account.body_candidates==bodies &&
        payload_callbacks==callbacks);
    RETAIN_CHECK(account.reductions_taken==1u && account.completed_scans==2u);
    RETAIN_CHECK(rhocalc_run_last_comm(run,&address));
    RETAIN_CHECK(address.source_reductions==0u && address.send_component==1u &&
        address.recv_component==0u && address.body_index==body && address.candidate_ordinal==ordinal);
    rhocalc_run_close(run);
    arena_free(&arena);
    return true;
}

static bool long_key_case(const RhocalcEvalContext *context,size_t length,bool channel_key) {
    size_t capacity=3u*length+256u;
    char *prefix=malloc(length+1u),*text=malloc(capacity);
    RETAIN_CHECK(prefix && text);
    memset(prefix,'k',length);prefix[length]='\0';
    const char *format=channel_key
        ? "(rho:par (rho:send (rho:quote (rho:val \"%s\")) rho:nil) "
          "(rho:send (rho:quote (rho:val \"%s\")) rho:nil) "
          "(rho:recv (rho:quote (rho:val \"%s\")) $x rho:nil))"
        : "(rho:par (rho:send $c (rho:val \"%sa\")) "
          "(rho:send $c (rho:val \"%sb\")) (rho:recv $c $x (rho:drop $x)))";
    int written=channel_key ? snprintf(text,capacity,format,prefix,prefix,prefix)
        : snprintf(text,capacity,format,prefix,prefix);
    RETAIN_CHECK(written>=0 && (size_t)written<capacity);
    retained_current=text;
    Arena arena;arena_init(&arena);
    Atom *process=read_process(&arena,text);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
    RhocalcRun *run=open_detached(&arena,process,&profile,context);
    RETAIN_CHECK(run);
    bool saw_text=false;uint64_t middle=0u;
    RhocalcRunStatus status;
    do {
        RhocalcRunAccount before=rhocalc_run_account(run);
        status=rhocalc_run_poll(run,1u,NULL);
        RhocalcRunAccount after=rhocalc_run_account(run);
        RETAIN_CHECK(status!=RHOCALC_RUN_FAILED && after.driver_steps==before.driver_steps+1u);
        RETAIN_CHECK(after.comparison_component_probes>=before.comparison_component_probes &&
            after.comparison_text_probes>=before.comparison_text_probes);
        RETAIN_CHECK(after.channel_scan_probes>=before.channel_scan_probes);
        uint64_t probes=after.comparison_component_probes-before.comparison_component_probes+
            after.comparison_text_probes-before.comparison_text_probes+
            after.channel_scan_probes-before.channel_scan_probes;
        RETAIN_CHECK(probes<=1u);
        uint64_t earlier=channel_key ? before.channel_scan_probes : before.comparison_text_probes;
        uint64_t later=channel_key ? after.channel_scan_probes : after.comparison_text_probes;
        if (later>earlier) {
            saw_text=true;
            RETAIN_CHECK(!after.reductions_taken && after.body_candidates==(channel_key ? 0u : 2u));
            RhocalcCommAddress address;
            RETAIN_CHECK(!rhocalc_run_last_comm(run,&address));
            if (!middle && later>=length/2u+1u) middle=after.driver_steps;
        }
    } while (status==RHOCALC_RUN_PENDING);
    RhocalcRunAccount total=rhocalc_run_account(run);
    RETAIN_CHECK(saw_text && middle &&
        (channel_key ? total.channel_scan_probes : total.comparison_text_probes)>=length);
    RETAIN_CHECK(total.matching_pairs==2u && total.body_candidates==2u && total.reductions_taken==1u);
    RhoReductionResult result;
    RETAIN_CHECK(rhocalc_run_result(run,&result));
    RhocalcCommAddress address;
    RETAIN_CHECK(rhocalc_run_last_comm(run,&address) &&
        address.candidate_ordinal==(channel_key ? 0u : 1u));
    Atom *selected=result.residual;
    rhocalc_run_close(run);
    /* Cancel with live comparison cursors, or resume that same paid prefix. */
    for (unsigned finish=0u;finish<2u;finish++) {
        run=open_detached(&arena,process,&profile,context);RETAIN_CHECK(run);
        RETAIN_CHECK(rhocalc_run_poll(run,middle,NULL)==RHOCALC_RUN_PENDING);
        uint64_t used=1u;
        RhocalcRunAccount before=rhocalc_run_account(run);
        RETAIN_CHECK(rhocalc_run_poll(run,0u,&used)==RHOCALC_RUN_PENDING && !used &&
            same_account(before,rhocalc_run_account(run)));
        if (finish) {
            RETAIN_CHECK(rhocalc_run_poll(run,UINT64_MAX,NULL)==RHOCALC_RUN_DONE);
            RETAIN_CHECK(same_account(total,rhocalc_run_account(run)));
            RETAIN_CHECK(rhocalc_run_result(run,&result) && atom_eq(result.residual,selected));
        }
        rhocalc_run_close(run);
    }
    RETAIN_CHECK(retained_case(text,context,3u,RHO_SCHEDULER_CANONICAL));
    arena_free(&arena);free(prefix);free(text);retained_current=NULL;
    return true;
}

static bool preparation_sort_case(void) {
    const unsigned channels=17u;
    size_t capacity=4096u,position=0u;
    char *text=malloc(capacity);RETAIN_CHECK(text);
    position=(size_t)snprintf(text,capacity,"(rho:par");
    for (unsigned k=0u;k<channels;k++) {
        unsigned index=channels-k-1u;
        int count=snprintf(text+position,capacity-position,
            " (rho:send $sortChannel%02u rho:nil) (rho:recv $sortChannel%02u $x%02u rho:nil)",
            index,index,index);
        RETAIN_CHECK(count>0 && (size_t)count<capacity-position);
        position+=(size_t)count;
    }
    RETAIN_CHECK(position+2u<capacity);text[position++]=')';text[position]='\0';
    retained_current=text;
    Arena arena;arena_init(&arena);
    Atom *process=read_process(&arena,text);RETAIN_CHECK(process);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
    RhocalcRun *run=open_detached(&arena,process,&profile,NULL);RETAIN_CHECK(run);
    uint64_t cuts[32]={0},cut_count=0u;
    RhocalcRunStatus status;
    do {
        RhocalcRunAccount before=rhocalc_run_account(run);
        status=rhocalc_run_poll(run,1u,NULL);
        RhocalcRunAccount after=rhocalc_run_account(run);
        RETAIN_CHECK(status!=RHOCALC_RUN_FAILED && after.driver_steps==before.driver_steps+1u);
        RETAIN_CHECK(after.preparation_steps>=before.preparation_steps &&
            after.preparation_steps<=before.preparation_steps+1u);
        if (!after.reductions_taken && after.preparation_steps>before.preparation_steps &&
                !(after.preparation_steps%64u) && cut_count<32u)
            cuts[cut_count++]=after.driver_steps;
    } while (status==RHOCALC_RUN_PENDING);
    RhocalcRunAccount total=rhocalc_run_account(run);
    RETAIN_CHECK(total.reductions_taken==1u && total.completed_scans==2u &&
        total.matching_pairs==2u*channels-1u && total.body_candidates==2u*channels-1u);
    RETAIN_CHECK(cut_count>2u);
    RhoReductionResult result;
    RETAIN_CHECK(rhocalc_run_result(run,&result) && result.status==RHOCALC_REDUCTION_LIMIT_EXHAUSTED);
    Atom *selected=result.residual;
    rhocalc_run_close(run);
    for (uint64_t i=0u;i<cut_count;i++) {
        run=open_detached(&arena,process,&profile,NULL);RETAIN_CHECK(run);
        RETAIN_CHECK(rhocalc_run_poll(run,cuts[i],NULL)==RHOCALC_RUN_PENDING);
        if (i%2u) {
            RETAIN_CHECK(rhocalc_run_poll(run,UINT64_MAX,NULL)==RHOCALC_RUN_DONE);
            RETAIN_CHECK(same_account(total,rhocalc_run_account(run)));
            RETAIN_CHECK(rhocalc_run_result(run,&result) && atom_eq(result.residual,selected));
        }
        rhocalc_run_close(run);
    }
    RETAIN_CHECK(retained_case(text,NULL,1u,RHO_SCHEDULER_CANONICAL));
    arena_free(&arena);free(text);retained_current=NULL;
    return true;
}

static bool retained_run_tests(void) {
    Space world;
    Registry registry;
    Arena persistent;
    space_init(&world);
    registry_init(&registry);
    arena_init(&persistent);
    RhocalcEvalContext context={.space=&world,.registry=&registry,.persistent_arena=&persistent};
    const char *cases[]={
        "rho:nil",
        "(rho:send $stuck rho:nil)",
        "(rho:par (rho:send $c rho:nil) (rho:recv $c $x rho:nil))",
        "(rho:par (rho:send $c rho:nil) (rho:send $c rho:nil) "
            "(rho:recv $c $x (rho:send $out rho:nil)))",
        "(rho:par (rho:send $c (rho:send $payload rho:nil)) "
            "(rho:send $c rho:nil) (rho:recv $c $x (rho:drop $x)) "
            "(rho:recv $c $y (rho:send $out (rho:drop $y))))",
        "(rho:par (rho:send $b rho:nil) (rho:recv $b $x (rho:send $outB rho:nil)) "
            "(rho:send $a rho:nil) (rho:recv $a $y (rho:send $outA rho:nil)))",
        "(rho:par (rho:send $a rho:nil) (rho:recv $a $x (rho:send $b rho:nil)) "
            "(rho:recv $b $y (rho:send $c rho:nil)) "
            "(rho:recv $c $z (rho:send $out rho:nil)))",
        "(rho:par (rho:send $c rho:nil) (rho:send $c rho:nil) "
            "(rho:recv-persistent $c $x (rho:send $out rho:nil)))",
        "(rho:par (rho:send $c rho:nil) "
            "(rho:recv-persistent $c $x (rho:send $c rho:nil)))",
        "(rho:par rho:nil (rho:par (rho:send $c rho:nil) "
            "(rho:recv $c $x (rho:drop $x))))"
    };
    for (size_t i=0u;i<sizeof(cases)/sizeof(*cases);i++)
        for (uint32_t limit=1u;limit<=3u;limit++) {
            RETAIN_CHECK(retained_case(cases[i],i==7u || i==8u ? &context : NULL,limit,RHO_SCHEDULER_CANONICAL));
            RETAIN_CHECK(retained_case(cases[i],i==7u || i==8u ? &context : NULL,limit,RHO_SCHEDULER_ROTATING));
        }
    /* Cross the process-arena compaction boundary with a persistent receiver. */
    RETAIN_CHECK(retained_case(cases[8],&context,65u,RHO_SCHEDULER_CANONICAL));
    const char *payloads[]={
        "(rho:par (rho:send (rho:quote rho:nil) (rho:val 3)) "
            "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)))",
        "(rho:par (rho:send (rho:quote rho:nil) (rho:eval-payload (quote (+ 1 2)))) "
            "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)))",
        "(rho:par (rho:send (rho:quote rho:nil) "
            "(rho:eval-payload (quote (superpose (9 2 2))))) "
            "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)))",
        "(rho:par (rho:send (rho:quote rho:nil) "
            "(rho:eval-payload (quote (superpose ())))) "
            "(rho:recv (rho:quote rho:nil) $x (rho:drop $x)))"
    };
    for (size_t i=0u;i<sizeof(payloads)/sizeof(*payloads);i++)
        RETAIN_CHECK(retained_case(payloads[i],&context,3u,RHO_SCHEDULER_CANONICAL));
    RETAIN_CHECK(exact_occurrence_case(
        "(rho:par (rho:send $c rho:nil) (rho:send $c rho:nil) (rho:recv $c $x rho:nil))",
        NULL,2u,2u,0u,0u,0u));
    RETAIN_CHECK(exact_occurrence_case(payloads[2],&context,1u,3u,1u,1u,1u));
    RETAIN_CHECK(long_key_case(&context,0u,false));
    RETAIN_CHECK(long_key_case(&context,257u,false));
    RETAIN_CHECK(long_key_case(&context,4096u,false));
    RETAIN_CHECK(long_key_case(&context,0u,true));
    RETAIN_CHECK(long_key_case(&context,257u,true));
    RETAIN_CHECK(long_key_case(&context,4096u,true));
    RETAIN_CHECK(preparation_sort_case());
    registry_free(&registry);
    space_free(&world);
    arena_free(&persistent);

    /* A failed run retains its own diagnostic while other runs use the TLS
     * validation buffer, and never publishes its malformed input as absence. */
    Arena arena;
    arena_init(&arena);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
    RhocalcRun *bad=open_detached(&arena,read_process(&arena,"(not-a-process)"),&profile,NULL);
    RETAIN_CHECK(bad && rhocalc_run_poll(bad,1u,NULL)==RHOCALC_RUN_FAILED);
    char error[256];
    RETAIN_CHECK(rhocalc_run_error(bad));
    snprintf(error,sizeof(error),"%s",rhocalc_run_error(bad));
    RhocalcRun *good=open_detached(&arena,read_process(&arena,"rho:nil"),&profile,NULL);
    RETAIN_CHECK(good && rhocalc_run_poll(good,UINT64_MAX,NULL)==RHOCALC_RUN_DONE);
    RETAIN_CHECK(!strcmp(error,rhocalc_run_error(bad)));
    RhoReductionResult absent={0};
    RETAIN_CHECK(!rhocalc_run_result(bad,&absent));
    uint64_t used=99u;
    RETAIN_CHECK(rhocalc_run_poll(bad,8u,&used)==RHOCALC_RUN_FAILED && !used);
    rhocalc_run_close(bad);
    rhocalc_run_close(good);
    arena_free(&arena);
    printf("PASS: retained rho frontier, source/context teardown, zero/split/cancel and account (%u checks)\n",retained_checks);
    return true;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    bool abt_ok = rhocalc_abt_substitution_correspondence_selftest(&arena);
    bool text_key_ok =
        rhocalc_atom_text_key_correspondence_selftest(&arena);
    bool retained_ok=key_failure_tests() && retained_run_tests();
    unsigned passed = (abt_ok ? 1u : 0u) + (text_key_ok ? 1u : 0u) + (retained_ok ? 1u : 0u);

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    printf("(RhoCoreCorrespondenceSummary 3 %u %u)\n",
           passed, 3u - passed);
    if (!abt_ok || !text_key_ok || !retained_ok) return 1;
    puts("PASS: nameful rho substitution agrees with canonical ABT substitution");
    puts("PASS: direct rho payload keys agree with ordinary atom rendering");
    return 0;
}
