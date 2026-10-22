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
    bool same=a.driver_steps==b.driver_steps && a.selector_steps==b.selector_steps &&
        a.matching_pairs==b.matching_pairs && a.body_candidates==b.body_candidates &&
        a.completed_scans==b.completed_scans && a.reductions_taken==b.reductions_taken &&
        a.comparison_component_probes==b.comparison_component_probes &&
        a.comparison_text_probes==b.comparison_text_probes &&
        a.preparation_steps==b.preparation_steps && a.channel_scan_probes==b.channel_scan_probes &&
        a.validation_steps==b.validation_steps && a.normalization_steps==b.normalization_steps &&
        a.key_steps==b.key_steps && a.key_bytes==b.key_bytes &&
        a.construction_children==b.construction_children && a.construction_bytes==b.construction_bytes &&
        a.validation_memo_steps==b.validation_memo_steps &&
        a.normalization_memo_steps==b.normalization_memo_steps &&
        a.selector_memo_steps==b.selector_memo_steps &&
        a.reaction_build_steps==b.reaction_build_steps &&
        a.reaction_copy_steps==b.reaction_copy_steps &&
        a.reaction_merge_steps==b.reaction_merge_steps &&
        a.reaction_children==b.reaction_children && a.reaction_bytes==b.reaction_bytes &&
        a.compaction_copy_steps==b.compaction_copy_steps && a.export_copy_steps==b.export_copy_steps;
    if (!same) fprintf(stderr,"account mismatch: driver=%" PRIu64 "/%" PRIu64
        " selector=%" PRIu64 "/%" PRIu64 " prepare=%" PRIu64 "/%" PRIu64
        " validate=%" PRIu64 "/%" PRIu64 " normalize=%" PRIu64 "/%" PRIu64
        " components=%" PRIu64 "/%" PRIu64 " text=%" PRIu64 "/%" PRIu64
        " memo=%" PRIu64 "/%" PRIu64 ",%" PRIu64 "/%" PRIu64 ",%" PRIu64 "/%" PRIu64 "\n",
        a.driver_steps,b.driver_steps,a.selector_steps,b.selector_steps,
        a.preparation_steps,b.preparation_steps,a.validation_steps,b.validation_steps,
        a.normalization_steps,b.normalization_steps,a.comparison_component_probes,b.comparison_component_probes,
        a.comparison_text_probes,b.comparison_text_probes,
        a.validation_memo_steps,b.validation_memo_steps,
        a.normalization_memo_steps,b.normalization_memo_steps,a.selector_memo_steps,b.selector_memo_steps);
    return same;
}

/* Fresh runs allocate different normalized nodes. Keep every actual memo
 * action in their invoice, but compare the explicitly non-memo projection;
 * semantic equality does not imply equal physical hash placement. */
static bool nonmemo_account(RhocalcRunAccount *account) {
    RETAIN_CHECK(account->validation_memo_steps<=account->validation_steps &&
        account->normalization_memo_steps<=account->normalization_steps &&
        account->selector_memo_steps<=account->preparation_steps &&
        account->selector_memo_steps<=account->selector_steps);
    uint64_t rest=account->driver_steps;
    RETAIN_CHECK(account->validation_memo_steps<=rest);rest-=account->validation_memo_steps;
    RETAIN_CHECK(account->normalization_memo_steps<=rest);rest-=account->normalization_memo_steps;
    RETAIN_CHECK(account->selector_memo_steps<=rest);rest-=account->selector_memo_steps;
    account->driver_steps=rest;
    account->validation_steps-=account->validation_memo_steps;
    account->normalization_steps-=account->normalization_memo_steps;
    account->selector_steps-=account->selector_memo_steps;
    account->preparation_steps-=account->selector_memo_steps;
    account->validation_memo_steps=account->normalization_memo_steps=account->selector_memo_steps=0u;
    return true;
}

static bool same_nonmemo_account(RhocalcRunAccount a, RhocalcRunAccount b) {
    return nonmemo_account(&a) && nonmemo_account(&b) && same_account(a,b);
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
    /* The eager opener survives teardown of its source/context stack.
     * Hash probes depend on node addresses, so compare its answers here;
     * further private normalization also allocates nodes. Fixed prepared
     * selector controls separately compare the complete physical account. */
    RhocalcRun *copied=open_detached(&result,process,&profile,context);
    RETAIN_CHECK(copied);
    while (rhocalc_run_poll(copied,7u,NULL)==RHOCALC_RUN_PENDING) {}
    RhoReductionResult copied_result={0};
    RETAIN_CHECK(rhocalc_run_result(copied,&copied_result) &&
        copied_result.reductions_taken==oracle.reductions_taken &&
        copied_result.status==oracle.status && atom_eq(copied_result.residual,oracle.residual));
    rhocalc_run_close(copied);
    RhocalcRunAccount reference={0};
    uint64_t reference_callbacks=0u;
    const uint64_t allowances[]={1u,2u,7u,31u,UINT64_MAX};
    for (size_t cut=0u;cut<sizeof(allowances)/sizeof(*allowances);cut++) {
        RhocalcRun *run=rhocalc_run_open_held(&result,process,&profile,context);
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
        else RETAIN_CHECK(same_nonmemo_account(reference,account) && payload_callbacks==reference_callbacks);
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
            RETAIN_CHECK(same_nonmemo_account(total,rhocalc_run_account(run)));
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
    RhocalcRun *run=rhocalc_run_open_held(&arena,process,&profile,NULL);RETAIN_CHECK(run);
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
        run=rhocalc_run_open_held(&arena,process,&profile,NULL);RETAIN_CHECK(run);
        RETAIN_CHECK(rhocalc_run_poll(run,cuts[i],NULL)==RHOCALC_RUN_PENDING);
        if (i%2u) {
            RETAIN_CHECK(rhocalc_run_poll(run,UINT64_MAX,NULL)==RHOCALC_RUN_DONE);
            RETAIN_CHECK(same_nonmemo_account(total,rhocalc_run_account(run)));
            RETAIN_CHECK(rhocalc_run_result(run,&result) && atom_eq(result.residual,selected));
        }
        rhocalc_run_close(run);
    }
    RETAIN_CHECK(retained_case(text,NULL,1u,RHO_SCHEDULER_CANONICAL));
    arena_free(&arena);free(text);retained_current=NULL;
    return true;
}

/* Complete key products carry their counter displacement separately from
 * their reset-context bytes. Quote-local binders must not advance the parent. */
static bool retained_product_case(Atom *atom,bool name,uint32_t binders) {
    RhoProcessKeyCursor job={0};
    RETAIN_CHECK(rho_process_key_cursor_begin(&job,NULL,atom,name));
    uint64_t steps=0u;
    while (!job.done) {
        RETAIN_CHECK(!job.result && rho_process_key_cursor_advance(&job,NULL) && ++steps<100000u);
    }
    bool owned=false;
    RhoKeyProduct *product=rho_process_key_cursor_take(&job,&owned);
    rho_process_key_cursor_free(&job);
    RhoAlphaEnv env={0};RhoStr reference={0};
    if (name) rho_key_name_into(atom,&env,&reference); else rho_key_proc_into(atom,&env,&reference);
    RETAIN_CHECK(product && owned && reference.data && !reference.failed &&
        !strcmp(product->text,reference.data) && product->binders==binders && env.next_index==binders);
    free(reference.data);rho_alpha_free(&env);
    const uint32_t starts[]={0u,9u,99u,999u};
    for (size_t i=0u;i<sizeof(starts)/sizeof(*starts);i++) {
        env=(RhoAlphaEnv){.next_index=starts[i]};reference=(RhoStr){0};
        if (name) rho_key_name_into(atom,&env,&reference); else rho_key_proc_into(atom,&env,&reference);
        RhoKeyCursor *key=rho_key_cursor_new_at(atom,name,starts[i]);RETAIN_CHECK(key);
        while (!key->done) RETAIN_CHECK(rho_key_cursor_advance(key));
        RETAIN_CHECK(reference.data && key->text.data && !strcmp(key->text.data,reference.data) &&
            key->env.next_index==env.next_index && key->env.next_index-starts[i]==product->binders);
        free(reference.data);rho_alpha_free(&env);rho_key_cursor_free(key);
    }
    rho_key_product_free(product);
    return true;
}

static bool retained_reaction_preparation_tests(void) {
    Arena arena;arena_init(&arena);
    const char *products[]={
        "rho:nil",
        "(rho:recv $a $x (rho:recv $x $y (rho:send $y rho:nil)))",
        "(rho:send (rho:quote (rho:recv $q $z rho:nil)) (rho:recv $b $x rho:nil))",
        "(rho:par (rho:recv $b $x rho:nil) (rho:recv $a $y rho:nil))"};
    const uint32_t counts[]={0u,2u,1u,2u};
    for (size_t i=0u;i<sizeof(counts)/sizeof(*counts);i++)
        RETAIN_CHECK(retained_product_case(read_process(&arena,products[i]),false,counts[i]));
    RETAIN_CHECK(retained_product_case(read_process(&arena,
        "(rho:quote (rho:recv $a $x (rho:drop $x)))"),true,0u));

    /* More than nine retained binders crosses decimal alpha-key boundaries.
     * Body rows merge by RESET keys; global numbering is applied afterwards. */
    Atom *parts[13];
    parts[0]=read_process(&arena,"(rho:send $fire rho:nil)");
    parts[1]=read_process(&arena,"(rho:recv $fire $take rho:nil)");
    for (uint32_t i=0u;i<11u;i++) {
        char text[160];snprintf(text,sizeof(text),
            "(rho:recv $chan%02u $bound%02u (rho:send $bound%02u rho:nil))",i,i,i);
        parts[i+2u]=read_process(&arena,text);
    }
    RhoAtomVec source_parts={.items=parts,.len=13u,.cap=13u};
    Atom *source=rho_normalize_proc(&arena,rho_par_from_vec_in_order(&arena,&source_parts));
    RETAIN_CHECK(source);
    RhoView source_view=rho_view(source);
    RhoAtomVec components={0};RhoKeyProduct *keys[13]={0};
    uint32_t send=UINT32_MAX,recv=UINT32_MAX;
    for (uint32_t i=0u;i<source_view.nargs;i++) {
        RETAIN_CHECK(rho_vec_push(&components,source_view.args[i]));
        RhoProcessKeyCursor job={0};
        RETAIN_CHECK(rho_process_key_cursor_begin(&job,NULL,source_view.args[i],false));
        while (!job.done) RETAIN_CHECK(rho_process_key_cursor_advance(&job,NULL));
        bool owned=false;keys[i]=rho_process_key_cursor_take(&job,&owned);
        RETAIN_CHECK(keys[i] && owned);rho_process_key_cursor_free(&job);
        RhoView part=rho_view(source_view.args[i]);
        if (part.kind==RHO_SEND) send=i;
        if (part.kind==RHO_RECV && atom_eq(part.args[0],rho_view(parts[1]).args[0])) recv=i;
    }
    RETAIN_CHECK(components.len==13u && send<13u && recv<13u);
    const char *bodies[]={
        "rho:nil", "(rho:send $z rho:nil)",
        "(rho:recv $chan05 $x (rho:send $x rho:nil))",
        "(rho:par (rho:recv $chan05 $x (rho:send $x rho:nil)) (rho:recv $chan05 $y (rho:send $y rho:nil)))",
        "(rho:recv $chan10 $x (rho:recv $x $y (rho:drop $y)))",
        "(rho:recv $chan10 $x (rho:send $x (rho:recv $z $y rho:nil)))",
        "(rho:send (rho:quote (rho:recv $q $local rho:nil)) (rho:recv $outer $x rho:nil))",
        "(rho:par (rho:send $a rho:nil) (rho:send $ab rho:nil))"};
    enum { N=sizeof(bodies)/sizeof(*bodies) };
    RhoReactionKeyView views[N];memset(views,0,sizeof(views));
    for (size_t i=0u;i<N;i++) {
        Atom *body=rho_normalize_proc(&arena,read_process(&arena,bodies[i]));RETAIN_CHECK(body);
        RhoReactionViewCursor job={0};
        RETAIN_CHECK(rho_reaction_view_cursor_begin(&job,&views[i],&components,keys,send,recv,body));
        while (!job.done) RETAIN_CHECK(rho_reaction_view_cursor_advance(&job));
        RETAIN_CHECK(rho_reaction_view_cursor_take(&job));rho_reaction_view_cursor_free(&job);
    }
    /* Cut between two provider replies: a real produced byte is retained
     * while the peer has not yet been polled. Check both orientations. */
    for (unsigned ready_side=0u;ready_side<2u;ready_side++) {
        RhoReactionCompare comparison={.left=&views[0],.right=&views[0],
            .phase=RHO_COMPARE_TEXT,.text_live=true};
        RETAIN_CHECK(rho_reaction_text_cursor_init_whole(&comparison.left_text,&views[0]) &&
            rho_reaction_text_cursor_init_whole(&comparison.right_text,&views[0]));
        RhoReactionTextCursor *ready=ready_side ? &comparison.right_text : &comparison.left_text;
        unsigned char byte=0u;
        RhoKeyStep step=rho_reaction_text_cursor_next_byte(ready,&byte);
        RETAIN_CHECK(step==RHO_KEY_VALUE && byte=='p' && ready->prefix_pos==1u);
        if (ready_side) {comparison.right_status=step;comparison.right_byte=byte;}
        else {comparison.left_status=step;comparison.left_byte=byte;}
        RETAIN_CHECK(rho_reaction_compare_advance(&comparison) && ready->prefix_pos==1u &&
            comparison.phase==RHO_COMPARE_TEXT && comparison.left_status==RHO_KEY_PENDING &&
            comparison.right_status==RHO_KEY_PENDING);
        while (comparison.phase!=RHO_COMPARE_DONE) RETAIN_CHECK(rho_reaction_compare_advance(&comparison));
        RETAIN_CHECK(!comparison.order);rho_reaction_compare_free(&comparison);
    }
    bool saw_component_wait=false,saw_byte_wait=false,saw_shift=false,saw_merge=false;
    for (size_t i=0u;i<N;i++) for (size_t j=0u;j<N;j++) {
        int reference=0;RETAIN_CHECK(rho_reaction_key_view_text_compare(&views[i],&views[j],&reference));
        RhoReactionCompare comparison={0};
        RETAIN_CHECK(rho_reaction_compare_init(&comparison,&views[i],&views[j]));
        uint64_t steps=0u;
        while (comparison.phase!=RHO_COMPARE_DONE) {
            bool held=comparison.phase==RHO_COMPARE_TEXT && comparison.left_status==RHO_KEY_VALUE &&
                comparison.right_status==RHO_KEY_PENDING;
            size_t held_position=comparison.left_text.current_key_pos;
            Atom *held_current=comparison.left_text.current;
            unsigned char held_byte=comparison.left_byte;
            RETAIN_CHECK(rho_reaction_compare_advance(&comparison) && ++steps<200000u);
            if (held) RETAIN_CHECK(comparison.left_text.current_key_pos==held_position &&
                comparison.left_text.current==held_current && comparison.left_byte==held_byte);
            if (comparison.phase==RHO_COMPARE_COMPONENT &&
                (comparison.left_status==RHO_KEY_VALUE || comparison.right_status==RHO_KEY_VALUE))
                saw_component_wait=true;
            if (comparison.phase==RHO_COMPARE_TEXT &&
                ((comparison.left_status==RHO_KEY_VALUE && comparison.right_status==RHO_KEY_PENDING) ||
                 (comparison.right_status==RHO_KEY_VALUE && comparison.left_status==RHO_KEY_PENDING)))
                saw_byte_wait=true;
            saw_shift|=comparison.left_text.shifted || comparison.right_text.shifted;
            saw_merge|=comparison.left_components.merge_live || comparison.right_components.merge_live ||
                comparison.left_text.components.merge_live || comparison.right_text.components.merge_live;
        }
        RETAIN_CHECK(comparison.order==reference);
        rho_reaction_compare_free(&comparison);
    }
    RETAIN_CHECK(saw_component_wait && saw_byte_wait && saw_shift && saw_merge);
    for (size_t i=0u;i<N;i++) rho_reaction_key_view_free(&views[i]);
    for (uint32_t i=0u;i<components.len;i++) rho_key_product_free(keys[i]);
    rho_vec_free(&components);arena_free(&arena);
    puts("PASS: retained reaction products, quote counters, merge bytes, asymmetric pending replies and contextual comparison oracle");
    return true;
}

/* Hold the entire prepared machine graph fixed, not merely its original
 * input. With a fresh empty cache over those same addresses, batching the
 * selector preserves its FULL account including every physical memo probe. */
static bool retained_fixed_selector_tests(void) {
    Arena arena;arena_init(&arena);
    const char *text="(rho:par (rho:send $c rho:nil) (rho:send $c (rho:send $payload rho:nil)) "
        "(rho:recv $c $x (rho:recv $a $y (rho:send $y rho:nil))) "
        "(rho:recv $c $x (rho:par (rho:send $b rho:nil) (rho:recv $a $y rho:nil))))";
    Atom *input=read_process(&arena,text);RETAIN_CHECK(input);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
    RhoMachine machine={0};rho_machine_init(&machine,&arena,&profile,NULL);
    RETAIN_CHECK(rho_machine_load_process(&machine,input));
    const uint64_t quanta[]={1u,7u,UINT64_MAX};
    RhocalcRunAccount reference={0};Atom *answer=NULL;uint64_t ordinal=UINT64_MAX;
    bool saw_copy=false,saw_remap=false,saw_discard=false,saw_replacement=false;
    uint64_t cuts[5]={0};
    for (size_t i=0u;i<sizeof(quanta)/sizeof(*quanta);i++) {
        rho_process_key_cache_clear(&machine.process_key_cache,machine.arena);
        RhoCanonicalCursor cursor;rho_canonical_cursor_init(&cursor,&machine);
        uint64_t actions=0u;
        while (!cursor.done) {
            for (uint64_t used=0u;used<quanta[i] && !cursor.done;used++) {
                if (!i) {
                    unsigned cut=cursor.phase==RHO_CANONICAL_BODY_PREPARE &&
                            cursor.view_job.key.key && cursor.view_job.key.key->text.len>4u ? 0u :
                        cursor.phase==RHO_CANONICAL_CHOOSE_COPY && cursor.best_copy ? 1u :
                        cursor.phase==RHO_CANONICAL_CHOOSE_REMAP && cursor.remap_position ? 2u :
                        cursor.phase==RHO_CANONICAL_DISCARD && cursor.candidate_ready &&
                            cursor.candidate_view.body_len ? 3u : 4u;
                    if (cut<4u && !cuts[cut]) cuts[cut]=actions;
                    if (cursor.phase==RHO_CANONICAL_CHOOSE_COPY && cursor.best_copy && cursor.found && !cuts[4]) cuts[4]=actions;
                }
                saw_copy|=cursor.phase==RHO_CANONICAL_CHOOSE_COPY && cursor.best_copy;
                saw_replacement|=cursor.phase==RHO_CANONICAL_CHOOSE_COPY && cursor.found;
                saw_remap|=cursor.phase==RHO_CANONICAL_CHOOSE_REMAP;
                saw_discard|=cursor.phase==RHO_CANONICAL_DISCARD;
                if (cursor.phase==RHO_CANONICAL_CHOOSE_COPY && cursor.found)
                    RETAIN_CHECK(cursor.best_body && cursor.best_view.body==cursor.best_body);
                RETAIN_CHECK(rho_canonical_cursor_advance(&cursor) && ++actions<100000u);
            }
        }
        RhocalcRunAccount account={.driver_steps=actions,.selector_steps=actions};
        rho_canonical_cursor_account(&cursor,&account);
        RETAIN_CHECK(cursor.found && cursor.next && account.selector_memo_steps);
        if (!i) {reference=account;answer=cursor.next;ordinal=cursor.best_ordinal;}
        else RETAIN_CHECK(same_account(reference,account) && atom_eq(answer,cursor.next) &&
            ordinal==cursor.best_ordinal);
        rho_canonical_cursor_free(&cursor);
    }
    RETAIN_CHECK(saw_copy && saw_remap && saw_discard && saw_replacement);
    Atom *held_source=machine.current;
    for (size_t i=0u;i<sizeof(cuts)/sizeof(*cuts);i++) {
        RETAIN_CHECK(cuts[i]);
        rho_process_key_cache_clear(&machine.process_key_cache,machine.arena);
        RhoCanonicalCursor cursor;rho_canonical_cursor_init(&cursor,&machine);
        for (uint64_t step=0u;step<cuts[i];step++)
            RETAIN_CHECK(!cursor.done && rho_canonical_cursor_advance(&cursor));
        RETAIN_CHECK(!cursor.done && !cursor.next && machine.current==held_source);
        rho_canonical_cursor_free(&cursor);
        RETAIN_CHECK(machine.current==held_source);
    }
    rho_machine_free(&machine);arena_free(&arena);
    puts("PASS: fixed prepared graph retains exact complete selector/memo account across poll batching");
    return true;
}

static bool retained_reaction_build_case(uint32_t kept,uint32_t width,bool duplicate,bool persistent) {
    Arena arena,source;arena_init_detached(&arena);arena_init_detached(&source);
    RhoAtomVec parts={0},components={0};
    Atom *send=read_process(&arena,"(rho:send $buildFire rho:nil)");
    Atom *recv=read_process(&arena,persistent
        ? "(rho:recv-persistent $buildFire $take rho:nil)"
        : "(rho:recv $buildFire $take rho:nil)");
    RETAIN_CHECK(send && recv && rho_vec_push(&parts,send) && rho_vec_push(&parts,recv));
    for (uint32_t i=0u;i<kept;i++) {
        char text[96];snprintf(text,sizeof(text),"(rho:send $keep%04u rho:nil)",i);
        RETAIN_CHECK(rho_vec_push(&parts,read_process(&arena,text)));
    }
    Atom *original=rho_par_from_vec(&arena,&parts);rho_vec_free(&parts);RETAIN_CHECK(original);
    RhoView initial=rho_view(original);uint32_t send_index=UINT32_MAX,recv_index=UINT32_MAX;
    RhoKeyProduct **keys=calloc(initial.nargs,sizeof(*keys));RETAIN_CHECK(keys);
    for (uint32_t i=0u;i<initial.nargs;i++) {
        RETAIN_CHECK(rho_vec_push(&components,initial.args[i]));
        if (initial.args[i]==send) send_index=i;
        if (initial.args[i]==recv) recv_index=i;
        RhoProcessKeyCursor key={0};
        RETAIN_CHECK(rho_process_key_cursor_begin(&key,NULL,initial.args[i],false));
        while (!key.done) RETAIN_CHECK(rho_process_key_cursor_advance(&key,NULL));
        bool owned=false;keys[i]=rho_process_key_cursor_take(&key,&owned);
        RETAIN_CHECK(keys[i] && owned);rho_process_key_cursor_free(&key);
    }
    RETAIN_CHECK(send_index<components.len && recv_index<components.len);
    Atom *leaf=read_process(&source,"(rho:send (rho:quote rho:nil) rho:nil)");
    Atom **body_parts=width ? malloc((size_t)width*sizeof(*body_parts)) : NULL;
    RETAIN_CHECK(!width || body_parts);
    for (uint32_t i=0u;i<width;i++) {
        char text[96];snprintf(text,sizeof(text),"(rho:send $body%04u rho:nil)",width-i-1u);
        body_parts[i]=duplicate ? leaf : read_process(&source,text);
        RETAIN_CHECK(body_parts[i]);
    }
    Atom *body=width>1u ? rho_call(&source,"rho:par",body_parts,width)
        : width ? body_parts[0] : rho_nil(&source);
    free(body_parts);RETAIN_CHECK(body);
    RhoReactionKeyView view={0};RhoReactionViewCursor preparation={0};
    RETAIN_CHECK(rho_reaction_view_cursor_begin(&preparation,&view,&components,keys,
        send_index,recv_index,body));
    while (!preparation.done) RETAIN_CHECK(rho_reaction_view_cursor_advance(&preparation));
    RETAIN_CHECK(rho_reaction_view_cursor_take(&preparation));rho_reaction_view_cursor_free(&preparation);
    /* This eager oracle copies and re-sorts from syntax. It does not consume
     * the prepared row keys, row ordinals or retained merge. */
    Atom *oracle=rho_rebuild_reaction(&arena,&components,send_index,recv_index,
        atom_deep_copy(&arena,body));RETAIN_CHECK(oracle);
    uint32_t total=kept+width+(persistent?1u:0u);
    RETAIN_CHECK(view.total==total);
    uint64_t cuts[4]={0};RhocalcRunAccount reference={0};Atom *saved=NULL;
    const uint64_t quanta[]={1u,7u,UINT64_MAX};
    for (size_t trial=0u;trial<sizeof(quanta)/sizeof(*quanta);trial++) {
        RhoReactionBuildCursor builder={0};
        RETAIN_CHECK(rho_reaction_build_cursor_begin(&builder,&arena,&view));
        while (!builder.done) {
            for (uint64_t used=0u;used<quanta[trial] && !builder.done;used++) {
                if (!trial) {
                    unsigned cut=builder.phase==RHO_BUILD_COPY && builder.copy ? 0u :
                        builder.phase==RHO_BUILD_ROWS && builder.merge.merge_live ? 1u :
                        builder.phase==RHO_BUILD_ROWS && builder.assembly && builder.written ? 2u :
                        builder.phase==RHO_BUILD_FINISH ? 3u : 4u;
                    if (cut<4u && !cuts[cut]) cuts[cut]=builder.steps;
                }
                uint64_t before=builder.steps,copy_before=builder.copy_steps;
                uint64_t merge_before=builder.merge_steps,writes_before=builder.children;
                RETAIN_CHECK(!builder.result && rho_reaction_build_cursor_advance(&builder));
                RETAIN_CHECK(builder.steps==before+1u && builder.copy_steps-copy_before<=1u &&
                    builder.merge_steps-merge_before<=1u && builder.children-writes_before<=1u &&
                    builder.steps<1000000u && view.body==body &&
                    rho_view(original).args==initial.args && rho_view(original).nargs==initial.nargs);
            }
        }
        RETAIN_CHECK(builder.result && atom_eq(builder.result,oracle) && builder.written==total &&
            builder.merge.emitted==total && builder.children==(total>1u ? (uint64_t)total+1u : 0u) &&
            builder.bytes==(total>1u ? sizeof(Atom)+((uint64_t)total+1u)*sizeof(Atom *) : 0u));
        RhocalcRunAccount actual={.driver_steps=builder.steps,.selector_steps=builder.steps,
            .preparation_steps=builder.steps,.reaction_build_steps=builder.steps,
            .reaction_copy_steps=builder.copy_steps,.reaction_merge_steps=builder.merge_steps,
            .reaction_children=builder.children,.reaction_bytes=builder.bytes};
        if (!trial) {reference=actual;saved=builder.result;}
        else RETAIN_CHECK(same_account(reference,actual));
        uint64_t paid=builder.steps;
        RETAIN_CHECK(rho_reaction_build_cursor_advance(&builder) && builder.steps==paid);
        rho_reaction_build_cursor_free(&builder);RETAIN_CHECK(atom_eq(saved,oracle));
    }
    for (size_t cut=0u;cut<sizeof(cuts)/sizeof(*cuts);cut++) if (cuts[cut]) {
        RhoReactionBuildCursor builder={0};RETAIN_CHECK(rho_reaction_build_cursor_begin(&builder,&arena,&view));
        for (uint64_t used=0u;used<cuts[cut];used++)
            RETAIN_CHECK(!builder.done && rho_reaction_build_cursor_advance(&builder));
        RETAIN_CHECK(!builder.done && !builder.result && view.body==body);
        rho_reaction_build_cursor_free(&builder);
        RETAIN_CHECK(atom_eq(saved,oracle) && atom_eq(original,rho_par_from_vec(&arena,&components)));
    }
    if (width) RETAIN_CHECK(cuts[0]);
    RETAIN_CHECK(cuts[3]);
    if (width && kept) RETAIN_CHECK(cuts[1]);
    if (total>1u) RETAIN_CHECK(cuts[2]);
    if (width>1u && !duplicate) RETAIN_CHECK(view.body_items[0].ordinal!=0u);
    rho_reaction_key_view_free(&view);
    for (uint32_t i=0u;i<components.len;i++) rho_key_product_free(keys[i]);
    free(keys);rho_vec_free(&components);arena_free(&source);
    /* Complete results own copied body storage and retain original base
     * resources. Dropping the source body and all key/control owners is safe. */
    RETAIN_CHECK(atom_eq(saved,oracle));
    if (duplicate && width>1u) {
        RhoView result=rho_view(saved);RETAIN_CHECK(result.kind==RHO_PAR && result.nargs==total);
        Atom *image=NULL;uint32_t count=0u;
        for (uint32_t i=0u;i<result.nargs;i++) {
            RhoView row=rho_view(result.args[i]);
            if (row.kind==RHO_SEND && rho_view(row.args[0]).kind==RHO_QUOTE) {
                if (!image) image=result.args[i];
                RETAIN_CHECK(result.args[i]==image);count++;
            }
        }
        RETAIN_CHECK(count==width);
    }
    arena_free(&arena);return true;
}

static bool retained_closed_reaction_tests(void) {
    const uint32_t widths[]={8u,128u};
    for (size_t trial=0u;trial<sizeof(widths)/sizeof(*widths);trial++) {
        retained_current="closed receiver body keeps shared syntax through actual COMM and export";
        Arena source,result;arena_init_detached(&source);arena_init_detached(&result);
        Atom *leaf=read_process(&source,"(rho:send (rho:quote rho:nil) rho:nil)");
        Atom **items=malloc((size_t)widths[trial]*sizeof(*items));RETAIN_CHECK(leaf && items);
        for (uint32_t i=0u;i<widths[trial];i++) items[i]=leaf;
        Atom *body=rho_call(&source,"rho:par",items,widths[trial]);free(items);
        Atom *channel=atom_var(&source,"closedFire"),*binder=atom_var(&source,"unused");
        Atom *send=rho_binary(&source,"rho:send",channel,rho_nil(&source));
        Atom *recv=rho_ternary(&source,"rho:recv",channel,binder,body);
        Atom *input=rho_binary(&source,"rho:par",send,recv);
        RETAIN_CHECK(input && !atom_has_vars(body));
        RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,NULL);RETAIN_CHECK(run);
        RhocalcRunStatus status=RHOCALC_RUN_PENDING;
        while (status==RHOCALC_RUN_PENDING) {
            RhocalcRunAccount before=rhocalc_run_account(run);
            uint64_t used=99u;
            RETAIN_CHECK(rhocalc_run_poll(run,0u,&used)==RHOCALC_RUN_PENDING && !used &&
                same_account(before,rhocalc_run_account(run)));
            status=rhocalc_run_poll(run,trial ? 7u : 1u,&used);
            RETAIN_CHECK(status!=RHOCALC_RUN_FAILED && used &&
                rhocalc_run_account(run).driver_steps<1000000u);
            RhoReductionResult absent={0};
            if (status==RHOCALC_RUN_PENDING) RETAIN_CHECK(!rhocalc_run_result(run,&absent));
        }
        RhoReductionResult completed={0};RhocalcRunAccount bill=rhocalc_run_account(run);
        RETAIN_CHECK(rhocalc_run_result(run,&completed) && completed.reductions_taken==1u &&
            completed.status==RHOCALC_REDUCTION_QUIESCENT && bill.body_candidates==1u &&
            bill.reaction_children==(uint64_t)widths[trial]+1u);
        RhoView output=rho_view(completed.residual);
        RETAIN_CHECK(output.kind==RHO_PAR && output.nargs==widths[trial]);
        for (uint32_t i=0u;i<output.nargs;i++)
            RETAIN_CHECK(atom_eq(output.args[i],leaf) && output.args[i]==output.args[0]);
        rhocalc_run_close(run);arena_free(&source);
        RETAIN_CHECK(rho_view(completed.residual).nargs==widths[trial] &&
            rho_view(output.args[0]).kind==RHO_SEND);
        arena_free(&result);
    }
    /* An unused binder does not license suppressing a deferred producer.
     * Every producer occurrence remains a candidate. Without an evaluator,
     * the unsupported input refuses instead of fabricating a continuation. */
    Space world;Registry registry;Arena persistent;
    space_init(&world);registry_init(&registry);arena_init(&persistent);
    RhocalcEvalContext context={.space=&world,.registry=&registry,.persistent_arena=&persistent};
    const char *deferred="(rho:par (rho:send (rho:quote rho:nil) "
        "(rho:eval-payload (quote (superpose (9 2 2))))) "
        "(rho:recv (rho:quote rho:nil) $unused (rho:send (rho:quote rho:nil) rho:nil)))";
    RETAIN_CHECK(exact_occurrence_case(deferred,&context,1u,3u,1u,0u,0u));
    Arena arena;arena_init_detached(&arena);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
    RhocalcRun *run=rhocalc_run_open_held(&arena,read_process(&arena,deferred),&profile,NULL);
    RETAIN_CHECK(run && rhocalc_run_poll(run,UINT64_MAX,NULL)==RHOCALC_RUN_FAILED);
    RhocalcRunAccount bill=rhocalc_run_account(run);RhoReductionResult completed={0};
    RETAIN_CHECK(!bill.body_candidates && !bill.reductions_taken &&
        !rhocalc_run_result(run,&completed));
    rhocalc_run_close(run);arena_free(&arena);
    registry_free(&registry);space_free(&world);arena_free(&persistent);
    puts("PASS: actual closed-body COMM preserves shared syntax and occurrences; unused deferred payloads still execute and branch");
    return true;
}

static bool retained_reaction_build_tests(void) {
    const struct {uint32_t kept,width;bool duplicate,persistent;} cases[]={
        {0,0,false,false},{0,1,false,false},{0,8,false,false},
        {3,0,false,false},{3,1,false,false},{3,8,false,false},
        {0,128,true,false},{3,128,true,false},{0,0,false,true},{2,64,true,true}};
    for (size_t i=0u;i<sizeof(cases)/sizeof(*cases);i++)
        RETAIN_CHECK(retained_reaction_build_case(cases[i].kept,cases[i].width,
            cases[i].duplicate,cases[i].persistent));
    puts("PASS: retained final reaction construction preserves row origins, complete invoices, duplicate sharing, zero/single/wide outputs and every live cancellation phase");
    return true;
}

static bool retained_reaction_tie_tests(void) {
    retained_current="equal-key rows preserve distinct base/body origins and original body ordinals";
    Arena arena,source;arena_init_detached(&arena);arena_init_detached(&source);
    Atom *send=read_process(&arena,"(rho:send $tieFire rho:nil)");
    Atom *recv=read_process(&arena,"(rho:recv $tieFire $unused rho:nil)");
    Atom *base=read_process(&arena,"(rho:send (rho:quote rho:nil) rho:nil)");
    Atom *first=read_process(&source,"(rho:send (rho:quote rho:nil) rho:nil)");
    Atom *second=read_process(&source,"(rho:send (rho:quote rho:nil) rho:nil)");
    Atom *body=rho_binary(&source,"rho:par",first,second);
    Atom *items[]={send,recv,base};RhoAtomVec components={.items=items,.len=3u,.cap=3u};
    RhoKeyProduct *keys[3]={0};
    RETAIN_CHECK(base!=first && first!=second && atom_eq(base,first) && atom_eq(first,second));
    for (uint32_t i=0u;i<3u;i++) {
        RhoProcessKeyCursor key={0};bool owned=false;
        RETAIN_CHECK(rho_process_key_cursor_begin(&key,NULL,items[i],false));
        while (!key.done) RETAIN_CHECK(rho_process_key_cursor_advance(&key,NULL));
        keys[i]=rho_process_key_cursor_take(&key,&owned);RETAIN_CHECK(keys[i] && owned);
        rho_process_key_cursor_free(&key);
    }
    RhoReactionKeyView view={0};RhoReactionViewCursor preparation={0};
    RETAIN_CHECK(rho_reaction_view_cursor_begin(&preparation,&view,&components,keys,0u,1u,body));
    while (!preparation.done) RETAIN_CHECK(rho_reaction_view_cursor_advance(&preparation));
    RETAIN_CHECK(rho_reaction_view_cursor_take(&preparation));rho_reaction_view_cursor_free(&preparation);
    RETAIN_CHECK(!strcmp(keys[2]->text,view.body_items[0].key) &&
        !strcmp(view.body_items[0].key,view.body_items[1].key));
    RhoReactionComponentCursor merge={0};rho_reaction_component_cursor_init(&merge,&view);
    for (uint32_t i=0u;i<3u;i++) {
        RhoReactionRow row={0};RhoKeyStep status;
        do status=rho_reaction_component_cursor_next(&merge,&row);while(status==RHO_KEY_PENDING);
        RETAIN_CHECK(status==RHO_KEY_VALUE && row.from_body==(i!=0u) &&
            row.ordinal==(i ? i-1u : 2u) && row.atom==(i ? i==1u ? first : second : base));
    }
    RhoReactionRow end={0};RETAIN_CHECK(rho_reaction_component_cursor_next(&merge,&end)==RHO_KEY_END);
    rho_reaction_key_view_free(&view);
    for (uint32_t i=0u;i<3u;i++) rho_key_product_free(keys[i]);
    arena_free(&source);arena_free(&arena);
    puts("PASS: equal reset keys keep the base occurrence first and distinct body origins in original order");
    return true;
}

static bool retained_generation_tests(void) {
    retained_current="retained generation copy, two compactions, ambient interning and private export";
    HashConsTable intern;hashcons_init_compact(&intern);
    HashConsTable *saved_intern=g_hashcons;g_hashcons=&intern;
    /* The ordinary synchronous constructor admits interning; retained draft
     * assembly admits only private noninterning storage. Both providers must
     * agree with the exhaustive process oracle. */
    const char *interned[]={
        "(rho:par (rho:send $fire rho:nil) (rho:recv $fire $x rho:nil) "
            "(rho:send $keepA rho:nil) (rho:send $keepB rho:nil))",
        "(rho:par (rho:send (rho:quote rho:nil) "
            "(rho:send (rho:quote (rho:send (rho:quote rho:nil) rho:nil)) rho:nil)) "
            "(rho:recv (rho:quote rho:nil) $y (rho:drop $y)) "
            "(rho:recv (rho:quote (rho:send (rho:quote rho:nil) rho:nil)) $r rho:nil))"};
    for (size_t i=0u;i<sizeof(interned)/sizeof(*interned);i++)
        RETAIN_CHECK(retained_case(interned[i],NULL,3u,RHO_SCHEDULER_CANONICAL));
    Arena source,result;arena_init_detached(&source);arena_init(&result);
    Atom *input=read_process(&source,
        "(rho:par (rho:send (rho:quote rho:nil) rho:nil) "
        "(rho:recv-persistent (rho:quote rho:nil) $unused (rho:send (rho:quote rho:nil) rho:nil)) "
        "(rho:send (rho:quote (rho:val 1)) rho:nil) (rho:send (rho:quote (rho:val 1)) rho:nil))");
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,129u,1u,false};
    RhocalcEvalContext context={.persistent_arena=&result};
    RETAIN_CHECK(input && result.hashcons==&intern);
    Atom *oracle=rho_normalize_proc(&result,input);RETAIN_CHECK(oracle);
    for (unsigned trial=0u;trial<2u;trial++) {
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,&context);RETAIN_CHECK(run);
        unsigned commits=0u;bool pending_copy=false,pending_export=false;
        RhocalcRunStatus status=RHOCALC_RUN_PENDING;
        while (status==RHOCALC_RUN_PENDING) {
            Atom *current=run->machine.current;
            uintptr_t source_address=(uintptr_t)current;
            uint32_t active=run->machine.active_process_arena;
            RhoRunPhase phase=run->phase;RhocalcRunAccount before=rhocalc_run_account(run);
            uint64_t used=99u;
            RETAIN_CHECK(rhocalc_run_poll(run,0u,&used)==RHOCALC_RUN_PENDING && !used &&
                same_account(before,rhocalc_run_account(run)));
            status=rhocalc_run_poll(run,1u,&used);
            RhocalcRunAccount after=rhocalc_run_account(run);
            RETAIN_CHECK(status!=RHOCALC_RUN_FAILED && used==1u &&
                after.compaction_copy_steps-before.compaction_copy_steps<=1u &&
                after.export_copy_steps-before.export_copy_steps<=1u && after.driver_steps<1000000u);
            if (phase==RHO_RUN_COMPACT_COPY) {
                pending_copy=true;
                RETAIN_CHECK(run->machine.current==current && run->machine.active_process_arena==active &&
                    run->machine.process_arenas[active].hashcons==NULL && atom_eq(current,oracle));
            }
            if (phase==RHO_RUN_COMPACT_COMMIT) {
                commits++;
                RETAIN_CHECK(run->machine.active_process_arena==1u-active &&
                    !run->machine.process_arenas[active].hashcons &&
                    (uintptr_t)run->machine.current!=source_address);
                /* current belongs to the retired arena. Only the new root
                 * may be inspected from this point onward. */
                RETAIN_CHECK(atom_eq(run->machine.current,oracle));
            }
            if (phase==RHO_RUN_EXPORT_COPY && status==RHOCALC_RUN_PENDING) pending_export=true;
            RhoReductionResult absent={0};
            if (status==RHOCALC_RUN_PENDING) RETAIN_CHECK(!rhocalc_run_result(run,&absent));
        }
        RhocalcRunAccount bill=rhocalc_run_account(run);RhoReductionResult completed={0};
        RETAIN_CHECK(commits==2u && pending_copy && pending_export && bill.compaction_copy_steps &&
            bill.export_copy_steps && rhocalc_run_result(run,&completed) &&
            completed.reductions_taken==129u && completed.status==RHOCALC_REDUCTION_LIMIT_EXHAUSTED &&
            atom_eq(completed.residual,oracle));
        rhocalc_run_close(run);RETAIN_CHECK(atom_eq(completed.residual,oracle));
    }
    const RhoRunPhase cuts[]={RHO_RUN_COMPACT_COPY,RHO_RUN_COMPACT_COMMIT,RHO_RUN_EXPORT_COPY};
    for (size_t cut=0u;cut<sizeof(cuts)/sizeof(*cuts);cut++) {
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,&context);RETAIN_CHECK(run);
        while (run->phase!=cuts[cut]) RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
        RhoReductionResult absent={0};
        RETAIN_CHECK(!rhocalc_run_result(run,&absent) && atom_eq(run->machine.current,oracle));
        rhocalc_run_close(run);RETAIN_CHECK(rhocalc_process_well_formed_with_eval_payloads(input));
    }
    arena_free(&source);arena_free(&result);g_hashcons=saved_intern;hashcons_free(&intern);
    puts("PASS: retained compaction/export keep sources alive, preserve the allocation profile across two generations and publish no cancelled transfer");
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
    RETAIN_CHECK(bad && rhocalc_run_poll(bad,UINT64_MAX,NULL)==RHOCALC_RUN_FAILED);
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

static bool retained_key_case(Atom *process, bool name) {
    char *oracle = name ? rho_key_name(process) : rho_key_proc(process);
    RhoKeyCursor *cursor = rho_key_cursor_new(process, name);
    RETAIN_CHECK(oracle && cursor);
    size_t steps = 0u;
    while (!cursor->done) {
        RETAIN_CHECK(rho_key_cursor_advance(cursor));
        RETAIN_CHECK(++steps < 10000000u);
    }
    char *actual = rho_str_take(&cursor->text, "");
    RETAIN_CHECK(actual && !strcmp(actual, oracle));
    free(actual); free(oracle); rho_key_cursor_free(cursor);
    return true;
}

static uint32_t preparation_seed = 0x3129a718u;
static uint32_t preparation_random(void) {
    preparation_seed = preparation_seed * 1664525u + 1013904223u;
    return preparation_seed;
}
static Atom *preparation_process(Arena *arena, unsigned depth);
static Atom *preparation_name(Arena *arena, unsigned depth) {
    if (!depth || preparation_random()%3u == 0u)
        return atom_var_with_id(arena, "name", var_id_from_base(1u + preparation_random()%5u));
    return rho_unary(arena, "rho:quote", preparation_process(arena, depth - 1u));
}
static Atom *preparation_process(Arena *arena, unsigned depth) {
    unsigned choice = depth ? preparation_random()%7u : preparation_random()%2u;
    if (choice == 0u) return rho_nil(arena);
    if (choice == 1u) {
        Atom *payload = preparation_random()%2u ? atom_int(arena, preparation_random()%19u)
            : atom_string(arena, "quoted\n\"payload");
        return rho_value_proc(arena, payload);
    }
    if (choice == 2u) {
        uint32_t length = preparation_random()%5u;
        Atom *parts[4];
        for (uint32_t i = 0u; i < length; i++)
            parts[i] = i && preparation_random()%3u == 0u ? parts[i-1u]
                : preparation_process(arena, depth - 1u);
        return rho_call(arena, "rho:par", parts, length);
    }
    if (choice == 3u) return rho_binary(arena, "rho:send",
        preparation_name(arena, depth - 1u), preparation_process(arena, depth - 1u));
    if (choice == 4u) return rho_unary(arena, "rho:drop", preparation_name(arena, depth - 1u));
    Atom *name = preparation_name(arena, depth - 1u);
    Atom *binder = atom_var_with_id(arena, "name", var_id_from_base(1u + preparation_random()%5u));
    return rho_ternary(arena, choice == 5u ? "rho:recv" : "rho:recv-persistent",
        name, binder, preparation_process(arena, depth - 1u));
}

static bool retained_generated_preparation_tests(Arena *source, Arena *result) {
    RhocalcEvalContext context = {0};
    RhoRuntimeProfile profile = {RHO_SCHEDULER_CANONICAL, 1u, 1u, false};
    for (unsigned trial = 0u; trial < 128u; trial++) {
        Atom *input = preparation_process(source, 5u);
        RETAIN_CHECK(input && rhocalc_process_well_formed_with_eval_payloads(input));
        Atom *oracle = rho_normalize_proc(result, input);
        RETAIN_CHECK(oracle && retained_key_case(oracle, false));
        RhocalcRun *run = rhocalc_run_open_held(result, input, &profile, &context);
        RETAIN_CHECK(run);
        while (run->phase != RHO_RUN_SELECT) {
            RETAIN_CHECK(!run->machine.current &&
                rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_PENDING);
        }
        RETAIN_CHECK(atom_eq(run->machine.current, oracle));
        rhocalc_run_close(run);
    }
    return true;
}

static size_t memo_collision_key(const void *node) {
    uint64_t key=(uint64_t)(uintptr_t)node;
    key^=key>>33u; key*=UINT64_C(0xff51afd7ed558ccd); key^=key>>29u;
    return (size_t)key&63u;
}

static bool memo_fill(CettaNodeMemo *memo, const void *const *keys, size_t count) {
    for (size_t i=0u;i<count;i++)
        if (!cetta_node_memo_store_checked(memo,keys[i],0u,i%7u,NULL,NULL,SIZE_MAX)) return false;
    return true;
}

static bool retained_node_memo_tests(void) {
    retained_current="source/role memo, resumable collision growth and cancellation";
    unsigned char storage[8192];
    const void *keys[48]; size_t count=0u;
    for (size_t i=0u;i<sizeof(storage) && count<48u;i++)
        if (!memo_collision_key(&storage[i])) keys[count++]=&storage[i];
    RETAIN_CHECK(count==48u);
    CettaNodeMemo memo={0};
    RETAIN_CHECK(memo_fill(&memo,keys,40u) && memo.cap==64u && memo.len==32u);
    CettaNodeMemoCursor cursor={0};
    cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,SIZE_MAX);
    uint64_t total=0u; bool clearing=false, moving=false, collision=false;
    while (cursor.status==CETTA_NODE_MEMO_PENDING) {
        uint64_t before=cursor.probes;
        cetta_node_memo_advance(&cursor);
        RETAIN_CHECK(cursor.probes-before<=1u && ++total<10000u);
        clearing|=cursor.grown && cursor.clear_position && cursor.clear_position<cursor.grow_cap;
        moving|=cursor.grown && cursor.copy_position && cursor.copy_position<memo.cap;
        collision|=cursor.grown && cursor.inspected>1u;
    }
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_STORED && clearing && moving && collision && memo.cap==128u);
    cetta_node_memo_cancel(&cursor); cetta_node_memo_free(&memo);
    for (uint64_t cut=0u;cut<=total;cut++) {
        RETAIN_CHECK(memo_fill(&memo,keys,40u));
        cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,SIZE_MAX);
        RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_PENDING && !cursor.steps && !cursor.probes && !cursor.grown);
        for (uint64_t step=0u;step<cut;step++) {
            uint64_t before=cursor.probes;
            cetta_node_memo_advance(&cursor);
            RETAIN_CHECK(cursor.probes-before<=1u && cursor.steps==step+1u);
            if (cursor.grown) RETAIN_CHECK(memo.cap==64u);
        }
        bool inserted=cursor.status==CETTA_NODE_MEMO_STORED;
        cetta_node_memo_cancel(&cursor);
        for (size_t i=0u;i<40u;i++) {
            uintptr_t value=999u;
            RETAIN_CHECK(cetta_node_memo_find(&memo,keys[i],0u,&value) && value==i%7u);
        }
        uintptr_t value=0u;
        RETAIN_CHECK(cetta_node_memo_find(&memo,keys[40],0u,&value)==inserted && (!inserted || value==99u));
        cetta_node_memo_free(&memo);
    }
    RETAIN_CHECK(memo_fill(&memo,keys,40u));
    cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,64u*sizeof(CettaNodeMemoSlot));
    while (cursor.status==CETTA_NODE_MEMO_PENDING) cetta_node_memo_advance(&cursor);
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_REFUSED && memo.cap==64u && memo.len==32u);
    cetta_node_memo_cancel(&cursor);
    uintptr_t value=999u;
    RETAIN_CHECK(cetta_node_memo_find(&memo,keys[0],0u,&value) && !value);
    RETAIN_CHECK(cetta_node_memo_store_checked(&memo,keys[0],1u,123u,NULL,NULL,SIZE_MAX));
    RETAIN_CHECK(cetta_node_memo_find(&memo,keys[0],1u,&value) && value==123u);
    RETAIN_CHECK(cetta_node_memo_find(&memo,keys[0],0u,&value) && !value);
    cetta_node_memo_find_begin(&cursor,&memo,keys[40],0u);
    RETAIN_CHECK(cetta_node_memo_advance(&cursor)==CETTA_NODE_MEMO_PENDING);
    RETAIN_CHECK(cetta_node_memo_store_checked(&memo,keys[41],0u,456u,NULL,NULL,SIZE_MAX));
    RETAIN_CHECK(cetta_node_memo_advance(&cursor)==CETTA_NODE_MEMO_REFUSED);
    cetta_node_memo_cancel(&cursor);
    cetta_node_memo_find_begin(&cursor,&memo,NULL,0u);
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_REFUSED);
    cetta_node_memo_cancel(&cursor);
    memo.revision=UINT64_MAX;
    RETAIN_CHECK(!cetta_node_memo_store_checked(&memo,keys[42],0u,9u,NULL,NULL,SIZE_MAX));
    RETAIN_CHECK(cetta_node_memo_find(&memo,keys[41],0u,&value) && value==456u);
    cetta_node_memo_free(&memo);
    printf("PASS: retained source/role memo preserves committed entries across %" PRIu64 " growth/collision cuts\n",total+1u);
    return true;
}

static bool retained_shared_preparation_tests(void) {
    retained_current="shared process/name DAG and noninjective normalization";
    Arena source,result; arena_init_detached(&source); arena_init_detached(&result);
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,3u,1u,false};
    Atom *process=rho_nil(&source);
    for (unsigned i=0u;i<18u;i++)
        process=rho_binary(&source,"rho:send",rho_unary(&source,"rho:quote",process),process);
    RhocalcRun *run=rhocalc_run_open_held(&result,process,&profile,NULL);
    RETAIN_CHECK(run);
    while (run->phase!=RHO_RUN_SELECT) {
        RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING && run->account.driver_steps<50000u);
    }
    Atom *out=run->machine.current;
    for (unsigned i=0u;i<18u;i++) {
        RhoView view=rho_view(out); RhoView name=rho_view(view.args[0]);
        RETAIN_CHECK(view.kind==RHO_SEND && name.kind==RHO_QUOTE && name.args[0]==view.args[1]);
        out=view.args[1];
    }
    RETAIN_CHECK(rho_view(out).kind==RHO_NIL);
    rhocalc_run_close(run);
    Atom *leaf=read_process(&source,"(rho:send (rho:quote rho:nil) rho:nil)");
    Atom *wrapped=rho_unary(&source,"rho:par",leaf);
    Atom *par=rho_binary(&source,"rho:par",leaf,wrapped);
    run=rhocalc_run_open_held(&result,par,&profile,NULL);
    RETAIN_CHECK(run);
    while (run->phase!=RHO_RUN_SELECT)
        RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
    RhoView view=rho_view(run->machine.current);
    RETAIN_CHECK(view.kind==RHO_PAR && view.nargs==2u && view.args[0]==view.args[1]);
    rhocalc_run_close(run);
    Atom *name=rho_unary(&source,"rho:quote",rho_nil(&source));
    Atom *invalid=rho_binary(&source,"rho:send",name,name);
    run=rhocalc_run_open_held(&result,invalid,&profile,NULL);
    RETAIN_CHECK(run);
    while (rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING) RETAIN_CHECK(!run->machine.current);
    RETAIN_CHECK(run->status==RHOCALC_RUN_FAILED && !run->machine.current);
    rhocalc_run_close(run);
    arena_free(&result); arena_free(&source);
    puts("PASS: retained normalization preserves shared syntax, noninjective images, roles and occurrence multiplicity");
    return true;
}

static bool retained_preparation_tests(void) {
    retained_current = "held input, retained grammar/normalization/key/sort/assembly";
    Arena source, result;
    arena_init_detached(&source); arena_init_detached(&result);
    const unsigned widths[] = {8u, 128u, 2048u};
    RhoRuntimeProfile profile = {RHO_SCHEDULER_CANONICAL, 3u, 1u, false};
    for (size_t test = 0u; test < sizeof(widths)/sizeof(*widths); test++) {
        unsigned width = widths[test];
        Atom *leaf = read_process(&source, "(rho:send (rho:quote rho:nil) rho:nil)");
        Atom **children = malloc(((size_t)width + 1u) * sizeof(*children));
        RETAIN_CHECK(children && leaf);
        children[0] = atom_symbol(&source, "rho:par");
        for (unsigned i = 0u; i < width; i++) children[i+1u] = leaf;
        Atom *process = atom_expr(&source, children, width + 1u);
        free(children);
        Atom *oracle = rho_normalize_proc(&result, process);
        RETAIN_CHECK(oracle);
        RhocalcRunAccount whole = {0};
        uint64_t cuts[8] = {0};
        for (unsigned trial = 0u; trial < 2u; trial++) {
            uint64_t quantum = trial ? 7u : 1u;
            size_t result_before = arena_accounted_live_bytes(&result);
            RhocalcRun *run = rhocalc_run_open_held(&result, process, &profile, NULL);
            RETAIN_CHECK(run && run->input == process && !run->machine.current &&
                !arena_accounted_live_bytes(run->machine.arena) &&
                arena_accounted_live_bytes(&result) == result_before);
            uint64_t used = 9u;
            RETAIN_CHECK(rhocalc_run_poll(run, 0u, &used) == RHOCALC_RUN_PENDING && !used &&
                !arena_accounted_live_bytes(run->machine.arena));
            RhocalcRun *peer = !trial ? rhocalc_run_open_held(&result, rho_nil(&source), &profile, NULL) : NULL;
            RETAIN_CHECK(trial || peer);
            uint64_t peer_polls = 0u;
            while (run->phase != RHO_RUN_SELECT) {
                RhocalcRunAccount before = rhocalc_run_account(run);
                RETAIN_CHECK(!run->machine.current &&
                    rhocalc_run_poll(run, 0u, &used) == RHOCALC_RUN_PENDING && !used &&
                    same_account(before, rhocalc_run_account(run)));
                if (quantum == 1u && run->normalization.length) {
                    RhoNormalizeFrame *frame = &run->normalization.frames[run->normalization.length - 1u];
                    unsigned cut = frame->key && frame->key->text.len > 1u ? 2u :
                        frame->stage == 5u && frame->sort.output ? 3u :
                        frame->assembly && frame->position > 1u ? 4u :
                        frame->stage == 9u && frame->position ? 5u : 6u;
                    if (!cuts[cut]) cuts[cut] = before.driver_steps;
                }
                RETAIN_CHECK(rhocalc_run_poll(run, quantum, &used) == RHOCALC_RUN_PENDING && used);
                RhocalcRunAccount after = rhocalc_run_account(run);
                RETAIN_CHECK(after.validation_steps - before.validation_steps <= quantum &&
                    after.normalization_steps - before.normalization_steps <= quantum &&
                    after.key_steps - before.key_steps <= quantum &&
                    after.construction_children - before.construction_children <= quantum &&
                    after.driver_steps < 5000000u);
                if (peer) {
                    RhocalcRunStatus peer_status = rhocalc_run_poll(peer, 1u, NULL);
                    RETAIN_CHECK(peer_status != RHOCALC_RUN_FAILED && ++peer_polls < 100u);
                    if (peer_status == RHOCALC_RUN_DONE) {
                        RhoReductionResult answer = {0};
                        RETAIN_CHECK(rhocalc_run_result(peer, &answer) &&
                            rho_view(answer.residual).kind == RHO_NIL && !run->machine.current);
                        rhocalc_run_close(peer); peer = NULL;
                    }
                }
                if (quantum == 1u && run->phase == RHO_RUN_VALIDATE && !cuts[0]) cuts[0] = after.driver_steps;
                if (quantum == 1u && run->phase == RHO_RUN_NORMALIZE && !cuts[1]) cuts[1] = after.driver_steps;
                if (run->phase == RHO_RUN_SELECT) break;
            }
            RhocalcRunAccount actual = rhocalc_run_account(run);
            RETAIN_CHECK(!peer);
            RETAIN_CHECK(actual.validation_steps > width && actual.normalization_steps > width &&
                actual.key_steps > width && actual.construction_children == width + 1u &&
                actual.construction_bytes == sizeof(Atom) + ((uint64_t)width + 1u)*sizeof(Atom *));
            RhoView view = rho_view(run->machine.current);
            RETAIN_CHECK(view.kind == RHO_PAR && view.nargs == width &&
                atom_eq(run->machine.current, oracle));
            for (uint32_t i = 0u; i < view.nargs; i++)
                RETAIN_CHECK(atom_eq(view.args[i], leaf) && view.args[i]==view.args[0]);
            if (!trial) whole = actual;
            /* A seven-step poll may have entered SELECT. Compare only the
             * exact retained preparation account, independent of overshoot. */
            else RETAIN_CHECK(actual.validation_steps == whole.validation_steps &&
                actual.normalization_steps == whole.normalization_steps &&
                actual.key_steps == whole.key_steps && actual.key_bytes == whole.key_bytes &&
                actual.construction_children == whole.construction_children);
            rhocalc_run_close(run);
        }
        for (unsigned cut = 0u; cut < 7u; cut++) {
            RETAIN_CHECK(cuts[cut]);
            RhocalcRun *run = rhocalc_run_open_held(&result, process, &profile, NULL);
            RETAIN_CHECK(run && rhocalc_run_poll(run, cuts[cut], NULL) == RHOCALC_RUN_PENDING);
            RhoReductionResult absent = {0};
            RETAIN_CHECK(!rhocalc_run_result(run, &absent));
            rhocalc_run_close(run);
        }
    }
    const char *keys[] = {
        "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:send $x rho:nil)) "
            "(rho:recv (rho:quote rho:nil) $y (rho:drop $y)))",
        "(rho:recv (rho:quote (rho:recv (rho:quote rho:nil) $z (rho:drop $z))) "
            "$x (rho:par (rho:recv $x $y (rho:send $x (rho:drop $y))) (rho:drop $x)))",
        "(rho:recv (rho:quote rho:nil) $x (rho:recv $x $x (rho:drop $x)))",
        "(rho:par (rho:val (a \"b\\n\\\"\" $x)) (rho:val \"raw\\ntext\") "
            "(rho:eval-payload (quote (f $x))))"
    };
    for (size_t i = 0u; i < sizeof(keys)/sizeof(*keys); i++) {
        retained_current = keys[i];
        Atom *process = read_process(&source, keys[i]);
        RETAIN_CHECK(process && retained_key_case(process, false));
        Atom *quoted = rho_unary(&source, "rho:quote", process);
        RETAIN_CHECK(retained_key_case(quoted, true));
    }
    retained_current = "deep alpha lookup and long free-name bytes";
    char long_name[4097]; memset(long_name, 'x', sizeof(long_name)-1u); long_name[4096] = 0;
    Atom *process = rho_unary(&source, "rho:drop", atom_var(&source, long_name));
    for (unsigned i = 0u; i < 128u; i++)
        process = rho_ternary(&source, "rho:recv", rho_unary(&source, "rho:quote", rho_nil(&source)),
            atom_var_with_id(&source, "bound", var_id_from_base(10000u+i)), process);
    RETAIN_CHECK(retained_key_case(process, false));
    const char *bad[] = {"(rho:quote rho:nil)", "(rho:send rho:nil)",
        "(rho:send bad rho:nil)", "(rho:recv (rho:quote rho:nil) 7 rho:nil)",
        "(rho:val x)", "(rho:par rho:nil (rho:send bad rho:nil))"};
    for (size_t i = 0u; i < sizeof(bad)/sizeof(*bad); i++) {
        retained_current = bad[i];
        Atom *input = read_process(&source, bad[i]);
        RETAIN_CHECK(input && !rhocalc_process_well_formed(input));
        char error[256]; snprintf(error, sizeof(error), "%s", rhocalc_last_validation_error());
        RhocalcRun *run = rhocalc_run_open_held(&result, input, &profile, NULL);
        RETAIN_CHECK(run);
        while (rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_PENDING)
            RETAIN_CHECK(!run->machine.current);
        RETAIN_CHECK(run->status == RHOCALC_RUN_FAILED && !run->machine.current &&
            rhocalc_run_error(run) && !strcmp(error, rhocalc_run_error(run)));
        rhocalc_run_close(run);
    }
    retained_current = "128 deterministic constructor/DAG/quotation/binder normalization cases";
    RETAIN_CHECK(retained_generated_preparation_tests(&source, &result));
    retained_current = "deep normalization and cancellation in a nested stable sort";
    Atom *leaf = read_process(&source, "(rho:send (rho:quote rho:nil) rho:nil)");
    Atom *deep = rho_binary(&source, "rho:par", leaf, leaf);
    for (unsigned i = 0u; i < 128u; i++)
        deep = rho_binary(&source, "rho:send", rho_unary(&source, "rho:quote", deep),
                          rho_nil(&source));
    Atom *deep_oracle = rho_normalize_proc(&result, deep);
    uint64_t nested_sort_cut = 0u;
    RhocalcRun *deep_run = rhocalc_run_open_held(&result, deep, &profile, NULL);
    RETAIN_CHECK(deep_run && deep_oracle);
    while (deep_run->phase != RHO_RUN_SELECT) {
        if (deep_run->normalization.length > 16u) {
            RhoNormalizeFrame *frame = &deep_run->normalization.frames[deep_run->normalization.length-1u];
            if (!nested_sort_cut && frame->stage == 5u && frame->sort.output)
                nested_sort_cut = deep_run->account.driver_steps;
        }
        RETAIN_CHECK(!deep_run->machine.current &&
            rhocalc_run_poll(deep_run, 1u, NULL) == RHOCALC_RUN_PENDING);
    }
    RETAIN_CHECK(nested_sort_cut && atom_eq(deep_run->machine.current, deep_oracle));
    rhocalc_run_close(deep_run);
    deep_run = rhocalc_run_open_held(&result, deep, &profile, NULL);
    RETAIN_CHECK(deep_run && rhocalc_run_poll(deep_run, nested_sort_cut, NULL) == RHOCALC_RUN_PENDING &&
        deep_run->normalization.length > 16u && !deep_run->machine.current);
    rhocalc_run_close(deep_run);
    arena_free(&result); arena_free(&source);
    printf("PASS: held rho input, retained validation/normalization/keys/sort/assembly, "
           "peer progress, exact occurrences and cancellation (%u cumulative checks)\n", retained_checks);
    return true;
}

static bool retained_value_observation_tests(void) {
    Arena arena; arena_init(&arena);
    const char *sources[]={
        "rho:nil",
        "(rho:par (rho:val 1) (rho:par (rho:val (if True 11 12)) (rho:val 2) (rho:val 1)) (rho:send c (rho:val 9)) (rho:recv c $x (rho:val 8)))",
        "(rho:par (rho:par (rho:val 2) (rho:val 2)) (rho:par (rho:val 2) (rho:val 2)))"};
    const char *answers[]={"()","(1 (if True 11 12) 2 1)","(2 2 2 2)"};
    for (size_t i=0u;i<sizeof(sources)/sizeof(*sources);i++) {
        retained_current=sources[i];
        Atom *input=read_process(&arena,sources[i]),*expected=read_process(&arena,answers[i]);
        RETAIN_CHECK(input && expected);
        uint64_t whole=0u;
        for (uint64_t quantum=1u;quantum<=7u;quantum+=6u) {
            RhocalcValueObservation *o=rhocalc_values_open(&arena,input);
            RETAIN_CHECK(o && !rhocalc_values_result(o));
            uint64_t used=99u,total=0u;
            RETAIN_CHECK(rhocalc_values_poll(o,0u,&used)==RHOCALC_RUN_PENDING && !used);
            RhocalcRunStatus status=RHOCALC_RUN_PENDING;
            while (status==RHOCALC_RUN_PENDING) {
                status=rhocalc_values_poll(o,quantum,&used);total+=used;
                RETAIN_CHECK(used<=quantum && (status!=RHOCALC_RUN_PENDING || !rhocalc_values_result(o)));
            }
            RETAIN_CHECK(status==RHOCALC_RUN_DONE && atom_eq(rhocalc_values_result(o),expected));
            if (quantum==1u)whole=total;
            else RETAIN_CHECK(total==whole);
            RETAIN_CHECK(rhocalc_values_poll(o,9u,&used)==RHOCALC_RUN_DONE && !used);
            Atom *result=rhocalc_values_result(o);rhocalc_values_close(o);
            RETAIN_CHECK(atom_eq(result,expected));
        }
    }
    retained_current="value observer failure and cancellation";
    Atom *deep=read_process(&arena,"(rho:val 7)");
    for (unsigned i=0u;i<80u;i++)
        deep=atom_expr2(&arena,atom_symbol(&arena,"rho:par"),deep);
    RhocalcValueObservation *o=rhocalc_values_open(&arena,deep);
    RETAIN_CHECK(o);
    key_allocations=0u;key_fail_at=1u;key_failure_injected=false;
    RETAIN_CHECK(rhocalc_values_poll(o,1000u,NULL)==RHOCALC_RUN_FAILED &&
        key_failure_injected && !rhocalc_values_result(o));
    key_fail_at=0u;rhocalc_values_close(o);
    o=rhocalc_values_open(&arena,deep);
    RETAIN_CHECK(o && rhocalc_values_poll(o,7u,NULL)==RHOCALC_RUN_PENDING && !rhocalc_values_result(o));
    rhocalc_values_close(o);
    o=rhocalc_values_open(&arena,deep);
    RETAIN_CHECK(o && rhocalc_values_poll(o,UINT64_MAX,NULL)==RHOCALC_RUN_DONE &&
        atom_eq(rhocalc_values_result(o),read_process(&arena,"(7)")));
    rhocalc_values_close(o);
    Atom *cycle=atom_expr2(&arena,atom_symbol(&arena,"rho:par"),deep);
    cycle->expr.elems[1]=cycle;
    o=rhocalc_values_open(&arena,cycle);
    RETAIN_CHECK(o && rhocalc_values_poll(o,UINT64_MAX,NULL)==RHOCALC_RUN_FAILED &&
        !rhocalc_values_result(o));
    rhocalc_values_close(o);
    arena_free(&arena);
    puts("PASS: retained rho value observation preserves ordered duplicates and data, split work, cancellation and allocation refusal");
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
    bool retained_ok=key_failure_tests() && retained_node_memo_tests() &&
        retained_reaction_preparation_tests() && retained_fixed_selector_tests() &&
        retained_reaction_build_tests() && retained_reaction_tie_tests() && retained_closed_reaction_tests() &&
        retained_generation_tests() && retained_run_tests() &&
        retained_preparation_tests() && retained_shared_preparation_tests() && retained_value_observation_tests();
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
