#define _POSIX_C_SOURCE 200809L
#define CETTA_TEST_HOOKS 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* Keep the common backing owner inside this test's allocation/free hooks,
 * including when eval.h imports the memo interface before the Rho owner. */
static void *block_test_calloc(size_t,size_t);
static void resource_test_free(void *);
#define calloc block_test_calloc
#define free resource_test_free
#include "segmented_array.h"
#undef calloc
#undef free

#include "parser.h"
#include "eval.h"

#include "atom.h"
#include "rhocalc_core.h"
#include "symbol.h"
#include "abt.h"
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"

#if CETTA_RHO_WORK_CENSUS
typedef struct {
    uint64_t entries, views, grammar, normalization, process_normalization, keys, frees, realloc_bytes, malloc_bytes, calloc_bytes, allocation_calls;
} RhoWorkSample;
static _Thread_local bool rho_work_active;
static _Thread_local RhoWorkSample rho_work_sample;
#endif

/* Compile the real key owner so allocation controls can reach its failure
 * boundaries without adding hooks to production code. Other allocators keep
 * their ordinary implementation. The counter is local to this test thread. */
static _Thread_local size_t key_allocations;
static _Thread_local size_t key_fail_at;
static _Thread_local bool key_failure_injected;
static void *key_test_realloc(void *pointer, size_t size) {
#if CETTA_RHO_WORK_CENSUS
    if (rho_work_active) rho_work_sample.realloc_bytes += size;
#endif
    if (key_fail_at && ++key_allocations == key_fail_at) {
        key_failure_injected = true;
        return NULL;
    }
    return realloc(pointer, size);
}
static _Thread_local size_t block_allocation_calls, block_fail_at, block_last_request;
static _Thread_local bool block_failure_injected;
static void *block_test_calloc(size_t count, size_t size) {
#if CETTA_RHO_WORK_CENSUS
    if (rho_work_active) {
        rho_work_sample.calloc_bytes += count*size;
        rho_work_sample.allocation_calls++;
    }
#endif
    block_allocation_calls++;
    block_last_request=count*size;
    if (block_fail_at && block_allocation_calls == block_fail_at) {
        block_failure_injected=true;
        return NULL;
    }
    return calloc(count,size);
}
#if CETTA_RHO_WORK_CENSUS
static void *block_test_malloc(size_t size) {
    if (rho_work_active) {
        rho_work_sample.malloc_bytes += size;
        rho_work_sample.allocation_calls++;
    }
    return malloc(size);
}
#endif
#ifndef CETTA_RHO_TEST_CORE_SOURCE
#define CETTA_RHO_TEST_CORE_SOURCE "../../src/rhocalc_core.c"
#endif
static _Thread_local bool resource_tracking_refusal;
static _Thread_local StateCell *resource_refused_cell;
static _Thread_local unsigned resource_refused_frees;
bool resource_test_track_state(StateCell *cell) {
    if (resource_tracking_refusal) {
        resource_tracking_refusal=false;
        resource_refused_cell=cell;
        return false;
    }
    return eval_payload_track_scratch_state(cell);
}
static _Thread_local bool key_retirement_tracking;
static _Thread_local char *retirement_keys[257];
static _Thread_local unsigned retirement_key_count, retirement_key_frees[257];
static _Thread_local size_t retirement_nonnull_frees;
static void resource_test_free(void *pointer) {
    if (key_retirement_tracking && pointer) {
        retirement_nonnull_frees++;
        for (unsigned i=0u;i<retirement_key_count;i++)
            if (pointer==retirement_keys[i]) retirement_key_frees[i]++;
    }
    if (pointer && pointer==resource_refused_cell) {
        resource_refused_cell=NULL;
        resource_refused_frees++;
    }
    free(pointer);
}
#define eval_payload_track_scratch_state resource_test_track_state
#define free resource_test_free
static _Thread_local bool resource_space_active, resource_space_tracking_refusal;
static _Thread_local bool resource_space_storage_refusal, resource_space_storage_failed;
static _Thread_local Space *resource_owned_spaces[8], *resource_partial_space;
static _Thread_local unsigned resource_owned_count, resource_owned_frees;
bool __real_eval_track_new_space(Space *);
bool __wrap_eval_track_new_space(Space *space) {
    if (resource_space_active && resource_space_tracking_refusal) {
        resource_space_tracking_refusal=false;
        resource_partial_space=space;
        return false;
    }
    bool accepted=__real_eval_track_new_space(space);
    if (resource_space_active && accepted && resource_owned_count<8u)
        resource_owned_spaces[resource_owned_count++]=space;
    return accepted;
}
void __real_space_free(Space *);
void __wrap_space_free(Space *space) {
    if (resource_space_active && space) {
        if (space==resource_partial_space) resource_owned_frees++;
        else for (unsigned i=0;i<resource_owned_count;i++)
            if (space==resource_owned_spaces[i]) {resource_owned_frees++;break;}
    }
    __real_space_free(space);
}
static bool resource_test_admit_atom(Space *space,Arena *arena,Atom *atom) {
    if (resource_space_active && resource_space_storage_refusal &&
        atom && atom->kind==ATOM_GROUNDED && atom->ground.gkind==GV_INT &&
        atom->ground.ival==22) {
        resource_space_storage_refusal=false;
        resource_partial_space=space;
        return false;
    }
    return space_admit_atom(space,arena,atom);
}
static Atom *resource_test_store_atom(Space *space,Arena *arena,Atom *atom) {
    if (resource_space_active && space==resource_partial_space &&
        atom && atom->kind==ATOM_GROUNDED && atom->ground.gkind==GV_INT &&
        atom->ground.ival==22) {resource_space_storage_failed=true;return NULL;}
    return space_store_atom(space,arena,atom);
}
#define space_admit_atom resource_test_admit_atom
#define space_store_atom resource_test_store_atom
#define realloc key_test_realloc
#define calloc block_test_calloc
#if CETTA_RHO_WORK_CENSUS
#define malloc block_test_malloc
#endif
#include CETTA_RHO_TEST_CORE_SOURCE
#undef realloc
#undef calloc
#if CETTA_RHO_WORK_CENSUS
#undef malloc
#endif
#undef space_admit_atom
#undef space_store_atom
#undef free
#undef eval_payload_track_scratch_state

#if CETTA_RHO_WORK_CENSUS
/* Compiler instrumentation observes calls to the actual included owner.
 * Linked providers are outside this count. Allocation request sizes are
 * reported separately: a bounded number of calls does not bound their work. */
void __cyg_profile_func_enter(void *, void *) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *, void *) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *function, void *caller) {
    (void)caller;
    if (!rho_work_active) return;
    rho_work_sample.entries++;
    rho_work_sample.views += function == (void *)rho_view;
    rho_work_sample.grammar += function == (void *)rho_check_proc ||
        function == (void *)rho_check_name;
    rho_work_sample.normalization += function == (void *)rho_normalize_proc ||
        function == (void *)rho_normalize_name;
    rho_work_sample.process_normalization += function == (void *)rho_normalize_proc;
    rho_work_sample.keys += function == (void *)rho_key_proc_into ||
        function == (void *)rho_key_name_into;
    rho_work_sample.frees += function == (void *)resource_test_free;
}
void __cyg_profile_func_exit(void *function, void *caller) {
    (void)function; (void)caller;
}

static void rho_work_max(RhoWorkSample *maximum, RhoWorkSample sample) {
#define RHO_WORK_MAX(field) if (sample.field > maximum->field) maximum->field = sample.field
    RHO_WORK_MAX(entries); RHO_WORK_MAX(views); RHO_WORK_MAX(grammar);
    RHO_WORK_MAX(normalization); RHO_WORK_MAX(keys); RHO_WORK_MAX(frees);
    RHO_WORK_MAX(process_normalization);
    RHO_WORK_MAX(realloc_bytes); RHO_WORK_MAX(malloc_bytes);
    RHO_WORK_MAX(calloc_bytes); RHO_WORK_MAX(allocation_calls);
#undef RHO_WORK_MAX
}

static int retained_work_census(void) {
    const unsigned widths[] = {8u, 128u, 2048u, 32768u};
    const char *phases[] = {"LOAD", "VALIDATE", "NORMALIZE", "SELECT", "SELECTOR_RETIRE", "COMMIT",
        "COMPACT", "COMPACT_COPY", "COMPACT_COMMIT", "EXPORT", "EXPORT_COPY", "EXPORT_NORMALIZE",
        "CANCEL_RELEASE_POLL", "CANCEL_ATOMIC_TAIL", "OPEN", "CLOSE", "CANCEL_CLOSE"};
    enum { phase_count = RHO_RUN_EXPORT_NORMALIZE + 6u };
    _Static_assert(sizeof(phases)/sizeof(*phases) == phase_count, "census phase inventory");
    RhoWorkSample samples[2][4][phase_count] = {0};
    for (unsigned atomic = 0u; atomic < 2u; atomic++) {
        for (size_t wi = 0u; wi < sizeof(widths)/sizeof(*widths); wi++) {
            Arena source, result;
            arena_init_detached(&source); arena_init_detached(&result);
            Atom *leaf = rho_binary(&source, "rho:send",
                rho_unary(&source, "rho:quote", rho_nil(&source)), rho_nil(&source));
            Atom **children = malloc(((size_t)widths[wi]+1u)*sizeof(*children));
            if (!children) return 1;
            children[0] = atom_symbol(&source, "rho:par");
            for (unsigned i = 0u; i < widths[wi]; i++) children[i+1u] = leaf;
            Atom *input = atom_expr(&source, children, widths[wi]+1u);
            free(children);
            RhoRuntimeProfile profile = {RHO_SCHEDULER_CANONICAL, 1u, 1u, false};
            rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
            RhocalcRun *run = rho_run_open(&result, input, &profile, NULL, atomic, true);
            rho_work_active = false;
            if (!run) return 1;
            samples[atomic][wi][phase_count-3u] = rho_work_sample;
            uint64_t polls = 0u;
            while (run->status == RHOCALC_RUN_PENDING) {
                unsigned phase = run->phase;
                rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
                uint64_t used = 0u;
                RhocalcRunStatus status = rhocalc_run_poll(run, 1u, &used);
                rho_work_active = false;
                if (phase > RHO_RUN_EXPORT_NORMALIZE || status == RHOCALC_RUN_FAILED ||
                    used > 1u || ++polls > 100000000u) return 1;
                rho_work_max(&samples[atomic][wi][phase], rho_work_sample);
            }
            RhoReductionResult answer = {0};
            if (!rhocalc_run_result(run, &answer)) return 1;
            RhoView view = rho_view(answer.residual);
            if (view.kind != RHO_PAR || view.nargs != widths[wi]) return 1;
            for (unsigned i = 0u; i < view.nargs; i++)
                if (!atom_eq(view.args[i], leaf)) return 1;
            rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
            rhocalc_run_close(run);
            rho_work_active = false;
            samples[atomic][wi][phase_count-2u] = rho_work_sample;
            if (!atomic) {
                run = rho_run_open(&result, input, &profile, NULL, false, true);
                if (!run) return 1;
                while (run->status == RHOCALC_RUN_PENDING &&
                    !(run->phase == RHO_RUN_NORMALIZE && run->normalization.frames.length &&
                      rho_normalize_frame_at(&run->normalization,0)->stage == 5u)) {
                    if (rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_FAILED) return 1;
                }
                if (run->status != RHOCALC_RUN_PENDING || run->machine.current) return 1;
                rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
                rhocalc_run_close(run);
                rho_work_active = false;
                samples[atomic][wi][phase_count-1u] = rho_work_sample;
                run = rho_run_open(&result, input, &profile, NULL, false, true);
                if (!run) return 1;
                while (run->status == RHOCALC_RUN_PENDING &&
                    !(run->phase == RHO_RUN_NORMALIZE && run->normalization.frames.length &&
                      rho_normalize_frame_at(&run->normalization,0)->stage == 5u)) {
                    if (rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_FAILED) return 1;
                }
                if (run->status != RHOCALC_RUN_PENDING || run->machine.current) return 1;
                CettaOwnedReleaseStep released;
                do {
                    uint64_t used = 0u;
                    rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
                    released = rhocalc_run_release_poll(&run, 1u, false, &used);
                    rho_work_active = false;
                    if (!run || used > 1u || released == CETTA_OWNED_RELEASE_COMPLETE) return 1;
                    rho_work_max(&samples[atomic][wi][RHO_RUN_EXPORT_NORMALIZE+1u], rho_work_sample);
                } while (released == CETTA_OWNED_RELEASE_PENDING);
                if (released != CETTA_OWNED_RELEASE_ATOMIC) return 1;
                rho_work_sample = (RhoWorkSample){0}; rho_work_active = true;
                released = rhocalc_run_release_poll(&run, 1u, true, NULL);
                rho_work_active = false;
                if (run || released != CETTA_OWNED_RELEASE_COMPLETE) return 1;
                samples[atomic][wi][RHO_RUN_EXPORT_NORMALIZE+2u] = rho_work_sample;
            }
            for (unsigned phase = 0u; phase < phase_count; phase++) {
                RhoWorkSample sample = samples[atomic][wi][phase];
                if (!sample.entries) continue;
                printf("{\"profile\":\"%s\",\"width\":%u,\"phase\":\"%s\","
                    "\"max_owner_entries\":%" PRIu64 ",\"max_views\":%" PRIu64 ","
                    "\"max_recursive_grammar\":%" PRIu64 ",\"max_recursive_normalization\":%" PRIu64 ","
                    "\"max_recursive_process_normalization\":%" PRIu64 ","
                    "\"max_recursive_keys\":%" PRIu64 ",\"max_free_calls\":%" PRIu64 ","
                    "\"max_realloc_request_bytes\":%" PRIu64 ","
                    "\"max_malloc_request_bytes\":%" PRIu64 ","
                    "\"max_calloc_request_bytes\":%" PRIu64 ","
                    "\"max_allocation_calls\":%" PRIu64 "}\n",
                    atomic ? "atomic" : "retained", widths[wi], phases[phase],
                    sample.entries, sample.views, sample.grammar, sample.normalization, sample.process_normalization,
                    sample.keys, sample.frees, sample.realloc_bytes, sample.malloc_bytes,
                    sample.calloc_bytes, sample.allocation_calls);
            }
            arena_free(&result); arena_free(&source);
        }
    }
    unsigned growing = 0u;
    for (unsigned atomic = 0u; atomic < 2u; atomic++)
        for (unsigned phase = 0u; phase < phase_count; phase++) {
            RhoWorkSample small = samples[atomic][0][phase], large = samples[atomic][3][phase];
            if (large.entries > 2u*small.entries ||
                large.realloc_bytes > 2u*small.realloc_bytes ||
                large.malloc_bytes > 2u*small.malloc_bytes ||
                large.calloc_bytes > 2u*small.calloc_bytes) growing++;
        }
    printf("{\"width_growing_phase_observations\":%u,\"universal_bound_proved\":false,"
        "\"linked_provider_work_measured\":false}\n", growing);
    return samples[1][2][RHO_RUN_LOAD].normalization > widths[2] &&
        !samples[0][2][RHO_RUN_LOAD].normalization ? 0 : 1;
}
#endif

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
        a.substitution_steps==b.substitution_steps &&
        a.substitution_apply_steps==b.substitution_apply_steps &&
        a.substitution_memo_steps==b.substitution_memo_steps &&
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
        account->substitution_memo_steps<=account->substitution_steps &&
        account->selector_memo_steps<=account->preparation_steps &&
        account->selector_memo_steps<=account->selector_steps);
    RETAIN_CHECK(account->substitution_memo_steps<=account->validation_memo_steps ||
        account->substitution_memo_steps-account->validation_memo_steps<=account->normalization_memo_steps);
    uint64_t rest=account->driver_steps;
    RETAIN_CHECK(account->validation_memo_steps<=rest);rest-=account->validation_memo_steps;
    RETAIN_CHECK(account->normalization_memo_steps<=rest);rest-=account->normalization_memo_steps;
    RETAIN_CHECK(account->selector_memo_steps<=rest);rest-=account->selector_memo_steps;
    account->driver_steps=rest;
    account->validation_steps-=account->validation_memo_steps;
    account->normalization_steps-=account->normalization_memo_steps;
    account->selector_steps-=account->selector_memo_steps;
    account->preparation_steps-=account->selector_memo_steps;
    RETAIN_CHECK(account->substitution_memo_steps<=account->selector_steps &&
        account->substitution_memo_steps<=account->preparation_steps);
    account->selector_steps-=account->substitution_memo_steps;
    account->preparation_steps-=account->substitution_memo_steps;
    account->substitution_steps-=account->substitution_memo_steps;
    account->substitution_memo_steps=0u;
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
    RhoAtomVec components={0};RhoComponentKey key_rows[13]={0};RhoSortBuffer keys={.linear=key_rows,.capacity=13};
    uint32_t send=UINT32_MAX,recv=UINT32_MAX;
    for (uint32_t i=0u;i<source_view.nargs;i++) {
        RETAIN_CHECK(rho_vec_push(&components,source_view.args[i]));
        RhoProcessKeyCursor job={0};
        RETAIN_CHECK(rho_process_key_cursor_begin(&job,NULL,source_view.args[i],false));
        while (!job.done) RETAIN_CHECK(rho_process_key_cursor_advance(&job,NULL));
        bool owned=false;rho_component_key_row(&keys,i)->product=rho_process_key_cursor_take(&job,&owned);
        RETAIN_CHECK(rho_component_key_row(&keys,i)->product && owned);rho_process_key_cursor_free(&job);
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
        RETAIN_CHECK(rho_reaction_view_cursor_begin(&job,&views[i],&components,&keys,send,recv,body));
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
    for (uint32_t i=0u;i<components.len;i++) rho_key_product_free(rho_component_key_row(&keys,i)->product);
    rho_vec_free(&components);arena_free(&arena);
    puts("PASS: retained reaction products, quote counters, merge bytes, asymmetric pending replies and contextual comparison oracle");
    return true;
}

/* Literal variable-free bodies retain the complete prepared source graph,
 * so each fresh cursor probes the same addresses. Variable-bearing bodies
 * allocate real substitution replies: their fresh physical hash placement
 * is not fixed by holding only the machine's input graph. */
static bool retained_selector_batching_case(bool fixed_graph) {
    retained_current=fixed_graph ? "exact invoice over the fixed closed-body graph"
        : "fresh semantic-substitution graphs across selector batching";
    Arena arena;arena_init(&arena);
    const char *text=fixed_graph
        ? "(rho:par (rho:send $c rho:nil) (rho:send $c (rho:send $payload rho:nil)) "
          "(rho:recv $c $x (rho:send (rho:quote (rho:send (rho:quote rho:nil) rho:nil)) rho:nil)) "
          "(rho:recv $c $x (rho:par (rho:send (rho:quote rho:nil) rho:nil) "
          "(rho:send (rho:quote (rho:send (rho:quote rho:nil) rho:nil)) rho:nil))))"
        : "(rho:par (rho:send $c rho:nil) (rho:send $c (rho:send $payload rho:nil)) "
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
        else RETAIN_CHECK((fixed_graph ? same_account(reference,account)
                            : same_nonmemo_account(reference,account)) && atom_eq(answer,cursor.next) &&
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
    return true;
}

static bool retained_fixed_selector_tests(void) {
    RETAIN_CHECK(retained_selector_batching_case(true) &&
        retained_selector_batching_case(false));
    puts("PASS: fixed prepared graph retains the full memo invoice; fresh substitution graphs preserve semantics and the non-memo projection");
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
    RhoComponentKey *key_rows=calloc(initial.nargs,sizeof(*key_rows));
    RhoSortBuffer keys={.linear=key_rows,.capacity=initial.nargs};RETAIN_CHECK(key_rows);
    for (uint32_t i=0u;i<initial.nargs;i++) {
        RETAIN_CHECK(rho_vec_push(&components,initial.args[i]));
        if (initial.args[i]==send) send_index=i;
        if (initial.args[i]==recv) recv_index=i;
        RhoProcessKeyCursor key={0};
        RETAIN_CHECK(rho_process_key_cursor_begin(&key,NULL,initial.args[i],false));
        while (!key.done) RETAIN_CHECK(rho_process_key_cursor_advance(&key,NULL));
        bool owned=false;rho_component_key_row(&keys,i)->product=rho_process_key_cursor_take(&key,&owned);
        RETAIN_CHECK(rho_component_key_row(&keys,i)->product && owned);rho_process_key_cursor_free(&key);
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
    RETAIN_CHECK(rho_reaction_view_cursor_begin(&preparation,&view,&components,&keys,
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
    if (width>1u && !duplicate) RETAIN_CHECK(rho_keyed_row(&view.body_items,0u)->ordinal!=0u);
    rho_reaction_key_view_free(&view);
    for (uint32_t i=0u;i<components.len;i++) rho_key_product_free(rho_component_key_row(&keys,i)->product);
    free(key_rows);rho_vec_free(&components);arena_free(&source);
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
    RhoComponentKey key_rows[3]={0};RhoSortBuffer keys={.linear=key_rows,.capacity=3};
    RETAIN_CHECK(base!=first && first!=second && atom_eq(base,first) && atom_eq(first,second));
    for (uint32_t i=0u;i<3u;i++) {
        RhoProcessKeyCursor key={0};bool owned=false;
        RETAIN_CHECK(rho_process_key_cursor_begin(&key,NULL,items[i],false));
        while (!key.done) RETAIN_CHECK(rho_process_key_cursor_advance(&key,NULL));
        rho_component_key_row(&keys,i)->product=rho_process_key_cursor_take(&key,&owned);RETAIN_CHECK(rho_component_key_row(&keys,i)->product && owned);
        rho_process_key_cursor_free(&key);
    }
    RhoReactionKeyView view={0};RhoReactionViewCursor preparation={0};
    RETAIN_CHECK(rho_reaction_view_cursor_begin(&preparation,&view,&components,&keys,0u,1u,body));
    while (!preparation.done) RETAIN_CHECK(rho_reaction_view_cursor_advance(&preparation));
    RETAIN_CHECK(rho_reaction_view_cursor_take(&preparation));rho_reaction_view_cursor_free(&preparation);
    RETAIN_CHECK(!strcmp(rho_component_key_row(&keys,2)->product->text,rho_keyed_row(&view.body_items,0u)->key) &&
        !strcmp(rho_keyed_row(&view.body_items,0u)->key,rho_keyed_row(&view.body_items,1u)->key));
    RhoReactionComponentCursor merge={0};rho_reaction_component_cursor_init(&merge,&view);
    for (uint32_t i=0u;i<3u;i++) {
        RhoReactionRow row={0};RhoKeyStep status;
        do status=rho_reaction_component_cursor_next(&merge,&row);while(status==RHO_KEY_PENDING);
        RETAIN_CHECK(status==RHO_KEY_VALUE && row.from_body==(i!=0u) &&
            row.ordinal==(i ? i-1u : 2u) && row.atom==(i ? i==1u ? first : second : base));
    }
    RhoReactionRow end={0};RETAIN_CHECK(rho_reaction_component_cursor_next(&merge,&end)==RHO_KEY_END);
    rho_reaction_key_view_free(&view);
    for (uint32_t i=0u;i<3u;i++) rho_key_product_free(rho_component_key_row(&keys,i)->product);
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

static size_t storage_blocks(const CettaSegmentedArray *array);

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
        clearing|=cursor.grown.blocks && cursor.clear_position && cursor.clear_position<cursor.grow_cap;
        moving|=cursor.grown.blocks && cursor.copy_position && cursor.copy_position<memo.cap;
        collision|=cursor.grown.blocks && cursor.inspected>1u;
    }
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_STORED && clearing && moving && collision && memo.cap==128u);
    cetta_node_memo_cancel(&cursor); cetta_node_memo_free(&memo);
    for (uint64_t cut=0u;cut<=total;cut++) {
        RETAIN_CHECK(memo_fill(&memo,keys,40u));
        cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,SIZE_MAX);
        RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_PENDING && !cursor.steps && !cursor.probes && !cursor.grown.blocks);
        for (uint64_t step=0u;step<cut;step++) {
            uint64_t before=cursor.probes;
            cetta_node_memo_advance(&cursor);
            RETAIN_CHECK(cursor.probes-before<=1u && cursor.steps==step+1u);
            if (cursor.grown.blocks && memo.cap!=cursor.grow_cap) RETAIN_CHECK(memo.cap==64u);
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
    size_t old_backing=memo.slots.live_bytes;
    RETAIN_CHECK(old_backing>64u*sizeof(CettaNodeMemoSlot));
    cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,old_backing);
    while (cursor.status==CETTA_NODE_MEMO_PENDING) {
        cetta_node_memo_advance(&cursor);
        RETAIN_CHECK(memo.slots.live_bytes+cursor.grown.live_bytes<=old_backing);
    }
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_REFUSED && memo.cap==64u);
    cetta_node_memo_cancel(&cursor);
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
    /* Two 64-row pages plus four directories constitute the replacement.
     * Admission counts both table generations until the old one retires. */
    size_t replacement_bytes=4u*(sizeof(CettaSegmentedBlock)+128u*sizeof(CettaSegmentedBlock *))+
        2u*(sizeof(CettaSegmentedBlock)+64u*sizeof(CettaNodeMemoSlot));
    for(unsigned tight=0u;tight<2u;tight++) {
        RETAIN_CHECK(memo_fill(&memo,keys,40u));
        size_t budget=memo.slots.live_bytes+replacement_bytes-tight;
        cetta_node_memo_store_begin(&cursor,&memo,keys[40],0u,99u,budget);
        while(cursor.status==CETTA_NODE_MEMO_PENDING) {
            size_t before_blocks=storage_blocks(&cursor.grown);
            bool retiring=memo.cap==cursor.grow_cap && cursor.grown.blocks;
            cetta_node_memo_advance(&cursor);
            RETAIN_CHECK(memo.slots.live_bytes+cursor.grown.live_bytes<=budget &&
                cursor.allocated_bytes<=replacement_bytes);
            if(retiring) RETAIN_CHECK(before_blocks-storage_blocks(&cursor.grown)<=1u);
            for(size_t i=0u;i<40u;i++) {
                uintptr_t kept=999u;
                RETAIN_CHECK(cetta_node_memo_find(&memo,keys[i],0u,&kept) && kept==i%7u);
            }
        }
        RETAIN_CHECK(tight ? cursor.status==CETTA_NODE_MEMO_REFUSED && memo.cap==64u :
            cursor.status==CETTA_NODE_MEMO_STORED && memo.cap==128u &&
                cursor.allocated_bytes==replacement_bytes);
        cetta_node_memo_cancel(&cursor);
        size_t blocks=storage_blocks(&memo.slots),released=0u;
        while(memo.slots.blocks) {
            size_t before=memo.slots.live_bytes;
            bool done=cetta_node_memo_release_one(&memo);released++;
            RETAIN_CHECK(memo.slots.live_bytes<before && storage_blocks(&memo.slots)+released==blocks);
            if(!done) {
                CettaNodeMemoCursor rejected={0};
                cetta_node_memo_find_begin(&rejected,&memo,keys[0],0u);
                RETAIN_CHECK(rejected.status==CETTA_NODE_MEMO_REFUSED);
            }
        }
        RETAIN_CHECK(!memo.retiring && !memo.cap && !memo.len);
    }
    /* A lower-half collision cluster cannot detect publication after half a
     * table scan. Pin a real upper-half occupied slot as an independent case;
     * the reference checks every previously committed lookup after each cut. */
    const void *upper_keys[41];size_t upper_count=0u;
    for(size_t i=0u;i<sizeof(storage) && upper_count<41u;i++)
        if(memo_collision_key(&storage[i])==63u) upper_keys[upper_count++]=&storage[i];
    RETAIN_CHECK(upper_count==41u && memo_fill(&memo,upper_keys,40u) && memo.cap==64u);
    RETAIN_CHECK(cetta_node_memo_entry(&memo,63u)->node!=NULL);
    cetta_node_memo_store_begin(&cursor,&memo,upper_keys[40],0u,99u,SIZE_MAX);
    while(cursor.status==CETTA_NODE_MEMO_PENDING) {
        cetta_node_memo_advance(&cursor);
        for(unsigned i=0u;i<40u;i++) {
            uintptr_t kept=999u;
            RETAIN_CHECK(cetta_node_memo_find(&memo,upper_keys[i],0u,&kept) && kept==i%7u);
        }
    }
    RETAIN_CHECK(cursor.status==CETTA_NODE_MEMO_STORED && memo.cap==128u);
    cetta_node_memo_cancel(&cursor);cetta_node_memo_free(&memo);
    printf("PASS: retained source/role memo preserves committed entries across %" PRIu64 " growth/collision cuts, exact backing-budget thresholds and one-block replacement/disposal steps\n",total+1u);
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
                if (quantum == 1u && run->normalization.frames.length) {
                    RhoNormalizeFrame *frame = rho_normalize_frame_at(&run->normalization,run->normalization.frames.length - 1u);
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
        if (deep_run->normalization.frames.length > 16u) {
            RhoNormalizeFrame *frame = rho_normalize_frame_at(&deep_run->normalization,deep_run->normalization.frames.length-1u);
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
        deep_run->normalization.frames.length > 16u && !deep_run->machine.current);
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

static bool semantic_substitution_case(Arena *arena, Atom *source,
        VarId variable, Atom *replacement, Atom *expected) {
    RhoSubstitutionCursor cursor={0};
    RETAIN_CHECK(rho_substitution_begin(&cursor,arena,source,variable,replacement));
    while (!cursor.done) {
        uint64_t before=cursor.account.substitution_steps;
        RETAIN_CHECK(rho_substitution_advance(&cursor) &&
            cursor.account.substitution_steps==before+1u);
    }
    RETAIN_CHECK(cursor.returned && atom_eq(cursor.returned,expected));
    uint64_t paid=cursor.account.substitution_steps;
    RETAIN_CHECK(rho_substitution_advance(&cursor) && cursor.account.substitution_steps==paid);
    rho_substitution_cursor_clear(&cursor);
    RETAIN_CHECK(!cursor.returned && !cursor.arena && !cursor.frames.length &&
        cursor.account.substitution_steps==paid);
    return true;
}

static bool retained_payload_substitution_tests(void) {
    retained_current="deferred result classification distinguishes data from refused preparation";
    Arena source,work;arena_init_detached(&source);arena_init_detached(&work);
    Atom *x=atom_var_with_id(&source,"payload",fresh_var_id());
    Atom *y=atom_var_with_id(&source,"bound",fresh_var_id());
    Atom *nil=rho_nil(&source);
    Atom *channel=rho_unary(&source,"rho:quote",nil);
    Atom *drop=rho_unary(&source,"rho:drop",x);
    Atom *seven=rho_value_proc(&source,atom_int(&source,7));
    Atom *par_children[]={nil,seven};
    Atom *valid=rho_call(&source,"rho:par",par_children,2u);
    Atom *ordinary=atom_expr2(&source,atom_symbol(&source,"ordinary"),atom_int(&source,11));
    Atom *bad_send=rho_binary(&source,"rho:send",channel,ordinary);
    Atom *bad_receive=rho_ternary(&source,"rho:recv",channel,atom_int(&source,0),nil);
    Atom *persistent=rho_ternary(&source,"rho:recv-persistent",channel,y,rho_unary(&source,"rho:drop",y));
    Atom *payloads[]={valid,ordinary,bad_send,bad_receive,channel,persistent,atom_int(&source,5)};
    Atom *expected[]={seven,rho_value_proc(&source,ordinary),rho_value_proc(&source,bad_send),
        rho_value_proc(&source,bad_receive),rho_value_proc(&source,channel),persistent,
        rho_value_proc(&source,payloads[6])};
    bool saw_check=false,saw_reply=false;
    for (size_t i=0u;i<sizeof(payloads)/sizeof(*payloads);i++) {
        RhoSubstitutionCursor cursor={0};
        RETAIN_CHECK(rho_substitution_begin_payload(&cursor,&work,drop,x->var_id,payloads[i]));
        while (!cursor.done) {
            const char *other=cursor.account.substitution_steps%2u
                ? "odd invocation diagnostic" : "even invocation diagnostic";
            rho_validation_clear();rho_validation_set("%s",other);
            RETAIN_CHECK(rho_substitution_advance(&cursor));
            const char *diagnostic=rhocalc_last_validation_error();
            RETAIN_CHECK(diagnostic && strcmp(diagnostic,other)==0);
            saw_check=saw_check || cursor.check.frames.length>0u;
            if (cursor.frames.length==1u && rho_substitution_frame_at(&cursor,0)->role==RHO_SUBST_COMM &&
                    rho_substitution_frame_at(&cursor,0)->stage==2u) {
                Atom *reply=cursor.returned;
                uint64_t normalized=cursor.account.normalization_steps;
                RETAIN_CHECK(reply && rho_substitution_advance(&cursor));
                RhoView quote=rho_view(rho_substitution_frame_at(&cursor,0)->replacement);
                RETAIN_CHECK(rho_substitution_frame_at(&cursor,0)->stage==3u &&
                    quote.kind==RHO_QUOTE && quote.nargs==1u && quote.args[0]==reply &&
                    cursor.account.normalization_steps==normalized);
                saw_reply=true;
            }
        }
        RETAIN_CHECK(cursor.returned && atom_eq(cursor.returned,expected[i]) &&
            cursor.account.validation_steps>0u);
        rho_substitution_cursor_clear(&cursor);
    }
    RETAIN_CHECK(saw_check && saw_reply);

    /* A real memo storage refusal after a checked prefix supplies no evidence
     * that the whole process is incompatible. It must not become val(data). */
    Atom *many[12];
    for (size_t i=0u;i<12u;i++) many[i]=rho_value_proc(&source,atom_int(&source,(int64_t)i));
    Atom *wide=rho_call(&source,"rho:par",many,12u);
    RhoSubstitutionCursor cursor={0};
    RETAIN_CHECK(rho_substitution_begin_payload(&cursor,&work,drop,x->var_id,wide));
    bool refused=false,injected=false;
    while (!cursor.done && !refused) {
        if (!injected && cursor.check.memo.inline_len>=8u && cursor.check.memo_cursor.writer) {
            cursor.check.memo_cursor.byte_limit=0u;injected=true;
        }
        refused=!rho_substitution_advance(&cursor);
    }
    RETAIN_CHECK(injected && refused && !cursor.done && !cursor.returned &&
        cursor.check.status==RHO_CHECK_REFUSED && cursor.account.validation_steps>0u &&
        cursor.account.substitution_memo_steps>0u);
    rho_substitution_cursor_clear(&cursor);rho_validation_clear();
    arena_free(&work);arena_free(&source);
    puts("PASS: retained deferred-result admission preserves raw data, normalizes actual process replies and refuses incomplete grammar checking");
    return true;
}

static bool retained_semantic_substitution_tests(void) {
    retained_current="actual semantic substitution replies, scope, opacity and fresh binders";
    Arena source;arena_init_detached(&source);
    Atom *x=atom_var_with_id(&source,"same",fresh_var_id());
    Atom *other=atom_var_with_id(&source,"same",fresh_var_id());
    Atom *same=atom_var_with_id(&source,"other-spelling",x->var_id);
    Atom *name_key=atom_expr2(&source,atom_symbol(&source,"Name"),atom_int(&source,17));
    Atom *y=atom_var_with_name_key(&source,name_key,fresh_var_id());
    Atom *nil=rho_nil(&source);
    Atom *channel=rho_unary(&source,"rho:quote",nil);
    Atom *seven=rho_value_proc(&source,atom_int(&source,7));
    Atom *replacement=rho_unary(&source,"rho:quote",seven);
    Atom *drop=rho_unary(&source,"rho:drop",x);
    Atom *inert=rho_unary(&source,"rho:drop",replacement);
    Atom *normalized_match=rho_unary(&source,"rho:drop",rho_unary(&source,"rho:quote",drop));
    Atom *quoted=rho_unary(&source,"rho:quote",rho_binary(&source,"rho:send",x,nil));
    Atom *opaque=rho_binary(&source,"rho:send",quoted,nil);
    Atom *self=rho_unary(&source,"rho:quote",drop);
    Atom *children[]={nil,drop};
    Atom *shadow_body=rho_call(&source,"rho:par",children,2u);
    Atom *pair=atom_expr3(&source,atom_symbol(&source,"Pair"),x,x);
    Atom *applied_pair=atom_expr3(&source,atom_symbol(&source,"Pair"),replacement,replacement);
    for (unsigned interning=0u;interning<2u;interning++) {
        HashConsTable intern;hashcons_init_compact(&intern);
        Arena arena;arena_init_detached(&arena);
        if (interning) arena_set_hashcons(&arena,&intern);
        RETAIN_CHECK(semantic_substitution_case(&arena,drop,x->var_id,replacement,seven));
        RETAIN_CHECK(semantic_substitution_case(&arena,inert,x->var_id,replacement,inert));
        RETAIN_CHECK(semantic_substitution_case(&arena,normalized_match,x->var_id,replacement,seven));
        RETAIN_CHECK(semantic_substitution_case(&arena,opaque,x->var_id,replacement,opaque));
        RETAIN_CHECK(semantic_substitution_case(&arena,drop,other->var_id,replacement,drop));
        RETAIN_CHECK(semantic_substitution_case(&arena,rho_unary(&source,"rho:drop",same),
            x->var_id,replacement,seven));
        RETAIN_CHECK(semantic_substitution_case(&arena,drop,x->var_id,self,drop));
        RETAIN_CHECK(semantic_substitution_case(&arena,drop,x->var_id,quoted,
            rho_binary(&source,"rho:send",x,nil)));
        RETAIN_CHECK(semantic_substitution_case(&arena,rho_value_proc(&source,pair),
            x->var_id,replacement,rho_value_proc(&source,applied_pair)));
        for (unsigned persistent=0u;persistent<2u;persistent++) {
            const char *head=persistent ? "rho:recv-persistent" : "rho:recv";
            Atom *receiver=rho_ternary(&source,head,x,x,shadow_body);
            Atom *expected=rho_ternary(&source,head,replacement,x,drop);
            RETAIN_CHECK(semantic_substitution_case(&arena,receiver,x->var_id,replacement,expected));
        }
        arena_free(&arena);hashcons_free(&intern);
    }

    /* Free-variable queries inspect quotations; substitution leaves their
     * surviving syntax opaque. Each colliding binder is minted once. */
    Atom *drop_y=rho_unary(&source,"rho:drop",y);
    Atom *open_replacement=rho_unary(&source,"rho:quote",drop_y);
    Atom *inner=rho_ternary(&source,"rho:recv",y,y,drop);
    Atom *outer=rho_ternary(&source,"rho:recv",channel,y,inner);
    Arena arena;arena_init_detached(&arena);
    RhoSubstitutionCursor cursor={0};
    RETAIN_CHECK(rho_substitution_begin(&cursor,&arena,outer,x->var_id,open_replacement));
    bool held_fresh=false;
    while (!cursor.done) {
        size_t saved=SIZE_MAX;Atom *saved_source=NULL;VarId saved_id=VAR_ID_NONE;
        for (size_t i=0u;i<cursor.frames.length;i++) {
            RhoSubstitutionFrame *frame=rho_substitution_frame_at(&cursor,i);
            if (frame->role==RHO_SUBST_PROC && frame->binder &&
                    (frame->view.kind==RHO_RECV || frame->view.kind==RHO_RECV_PERSISTENT) &&
                    !var_id_eq(frame->binder->var_id,frame->view.args[1]->var_id)) {
                saved=i;saved_source=frame->source;saved_id=frame->binder->var_id;break;
            }
        }
        RETAIN_CHECK(rho_substitution_advance(&cursor));
        if (saved<cursor.frames.length && rho_substitution_frame_at(&cursor,saved)->source==saved_source) {
            RETAIN_CHECK(rho_substitution_frame_at(&cursor,saved)->binder &&
                var_id_eq(rho_substitution_frame_at(&cursor,saved)->binder->var_id,saved_id));
            held_fresh=true;
        }
    }
    RETAIN_CHECK(held_fresh);
    RhoView first=rho_view(cursor.returned);
    RETAIN_CHECK(first.kind==RHO_RECV && first.nargs==3u && first.args[1]->kind==ATOM_VAR);
    RhoView second=rho_view(first.args[2]);
    RETAIN_CHECK(second.kind==RHO_RECV && second.nargs==3u &&
        second.args[0]->kind==ATOM_VAR && second.args[1]->kind==ATOM_VAR);
    RhoView exposed=rho_view(second.args[2]);
    RETAIN_CHECK(exposed.kind==RHO_DROP && exposed.nargs==1u && exposed.args[0]->kind==ATOM_VAR &&
        !var_id_eq(first.args[1]->var_id,y->var_id) &&
        !var_id_eq(second.args[1]->var_id,y->var_id) &&
        !var_id_eq(first.args[1]->var_id,second.args[1]->var_id) &&
        var_id_eq(second.args[0]->var_id,first.args[1]->var_id) &&
        var_id_eq(exposed.args[0]->var_id,y->var_id) &&
        atom_eq(first.args[1]->name_key,name_key) && atom_eq(second.args[1]->name_key,name_key));
    rho_substitution_cursor_clear(&cursor);

    /* One source pointer has different meaning outside and inside a binder. */
    Atom *scoped[]={drop,rho_ternary(&source,"rho:recv",channel,x,drop)};
    Atom *scoped_expected[]={seven,rho_ternary(&source,"rho:recv",channel,x,drop)};
    Atom *expected=rho_normalize_proc(&source,rho_call(&source,"rho:par",scoped_expected,2u));
    RETAIN_CHECK(semantic_substitution_case(&arena,rho_call(&source,"rho:par",scoped,2u),
        x->var_id,replacement,expected));

    /* Payload substitution uses the binding interpreter even when raw data
     * has receive-shaped syntax. It is not a Rho lexical binder there. */
    Atom *raw=rho_ternary(&source,"rho:recv",channel,x,drop);
    Bindings bindings;bindings_init(&bindings);
    RETAIN_CHECK(bindings_add_id(&bindings,x->var_id,SYMBOL_ID_NONE,replacement));
    Atom *raw_expected=bindings_apply_if_vars(&bindings,&source,raw);
    RETAIN_CHECK(raw_expected && semantic_substitution_case(&arena,rho_value_proc(&source,raw),
        x->var_id,replacement,rho_value_proc(&source,raw_expected)));
    bindings_free(&bindings);

    Atom *composite_children[]={outer,rho_value_proc(&source,pair)};
    Atom *composite=rho_call(&source,"rho:par",composite_children,2u);
    /* A free variable in raw payload head slot zero is also a capture risk. */
    Atom *head_data=atom_expr2(&source,y,atom_int(&source,29));
    Atom *head_replacement=rho_unary(&source,"rho:quote",rho_value_proc(&source,head_data));
    cursor=(RhoSubstitutionCursor){0};
    RETAIN_CHECK(rho_substitution_begin(&cursor,&arena,composite,x->var_id,head_replacement));
    while (!cursor.done) RETAIN_CHECK(rho_substitution_advance(&cursor));
    uint64_t complete_steps=cursor.account.substitution_steps;
    RETAIN_CHECK(cursor.account.substitution_apply_steps>0u &&
        cursor.account.normalization_steps>0u && cursor.account.construction_children>0u);
    RhoView combined=rho_view(cursor.returned);
    RETAIN_CHECK(combined.kind==RHO_PAR && combined.nargs==2u);
    Atom *capture_checked=NULL;
    for (size_t i=0u;i<combined.nargs;i++) {
        RhoView member=rho_view(combined.args[i]);
        if (member.kind==RHO_RECV) capture_checked=combined.args[i];
    }
    RETAIN_CHECK(capture_checked);
    RhoView captured=rho_view(capture_checked);
    RETAIN_CHECK(captured.nargs==3u && captured.args[1]->kind==ATOM_VAR &&
        !var_id_eq(captured.args[1]->var_id,y->var_id));
    rho_substitution_cursor_clear(&cursor);
    arena_free(&arena);

    bool roles[RHO_SUBST_ROLE_COUNT]={0};bool renamed=false,application=false;
    uint64_t unfinished_cuts=0u;
    for (uint64_t cut=0u;cut<complete_steps;cut++) {
        Arena work;arena_init_detached(&work);
        cursor=(RhoSubstitutionCursor){0};
        RETAIN_CHECK(rho_substitution_begin(&cursor,&work,composite,x->var_id,head_replacement));
        while (!cursor.done && cursor.account.substitution_steps<cut)
            RETAIN_CHECK(rho_substitution_advance(&cursor));
        for (size_t i=0u;i<cursor.frames.length;i++) {
            roles[rho_substitution_frame_at(&cursor,i)->role]=true;
            renamed=renamed || rho_substitution_frame_at(&cursor,i)->rename;
        }
        if (!cursor.done) unfinished_cuts++;
        application=application || cursor.application!=NULL;
        uint64_t paid=cursor.account.substitution_steps;
        VarId before=fresh_var_id();
        rho_substitution_cursor_clear(&cursor);
        VarId after=fresh_var_id();
        RETAIN_CHECK(!cursor.returned && !cursor.arena && !cursor.application &&
            !cursor.frames.length && cursor.account.substitution_steps==paid && after.base>before.base);
        arena_free(&work);
    }
    RETAIN_CHECK(roles[RHO_SUBST_PROC] && roles[RHO_SUBST_NAME] &&
        roles[RHO_SUBST_FREE_PROC] && roles[RHO_SUBST_FREE_NAME] &&
        roles[RHO_SUBST_FREE_ATOM] && unfinished_cuts>0u &&
        roles[RHO_SUBST_NORMAL_PROC] && roles[RHO_SUBST_NORMAL_NAME] &&
        roles[RHO_SUBST_APPLY] && renamed && application);
    for (unsigned profile=0u;profile<2u;profile++) {
        Arena work;arena_init_detached(&work);
        ArenaMark mark=arena_mark(&work);
        cursor=(RhoSubstitutionCursor){0};
        RETAIN_CHECK(rho_substitution_begin(&cursor,&work,composite,x->var_id,head_replacement) &&
            rho_substitution_advance(&cursor) && !cursor.done);
        uint64_t paid=cursor.account.substitution_steps;
        HashConsTable intern;hashcons_init_compact(&intern);
        if (profile) arena_set_hashcons(&work,&intern);
        else arena_reset(&work,mark);
        RETAIN_CHECK(!rho_substitution_advance(&cursor) && !cursor.done &&
            cursor.account.substitution_steps==paid);
        rho_substitution_cursor_clear(&cursor);
        arena_set_hashcons(&work,NULL);arena_free(&work);hashcons_free(&intern);
    }
    arena_free(&source);
    puts("PASS: semantic substitution retains actual normalization/capture replies, distinct fresh binders, opaque quotes, payload interpretation, paid work and cancellation");
    return true;
}

static bool payload_resource_cycle_tests(void) {
    retained_current="pending resource discovery, cyclic state images and affine export owners";
    Arena source,owner;arena_init_detached(&source);arena_init_detached(&owner);
    StateCell self={0},left={0},right={0};
    Atom *self_ref=atom_state(&source,&self);
    Atom *left_ref=atom_state(&source,&left),*right_ref=atom_state(&source,&right);
    self.value=self_ref;self.content_type=atom_symbol(&source,"SelfType");
    left.value=atom_expr3(&source,atom_symbol(&source,"Pair"),right_ref,right_ref);
    left.content_type=self_ref;
    right.value=left_ref;right.content_type=atom_symbol(&source,"MutualType");
    RhoStateCloneMap states={0};RhoOverlayMap spaces={0};
    bool inserted=false;
    RETAIN_CHECK(rho_state_clone_map_note(&states,&self,&inserted) && inserted &&
        states.len==1 && !states.clone[0]);
    RETAIN_CHECK(rho_state_clone_map_note(&states,&self,&inserted) && !inserted &&
        states.len==1 && !states.clone[0]);
    RETAIN_CHECK(rho_collect_state_ptrs_into_map(&states,self_ref) && states.len==1);
    RETAIN_CHECK(rho_collect_state_ptrs_into_map(&states,left_ref) && states.len==3);
    RETAIN_CHECK(rho_collect_state_ptrs_into_map(&states,right_ref) && states.len==3);
    CettaEvalPayloadFrame *frame=eval_payload_frame_open(&owner);
    CettaEvalPayloadScope scope={0};
    RETAIN_CHECK(frame && eval_payload_frame_enter(frame,&scope));
    uint64_t epoch=eval_next_payload_owner_epoch();
    uint64_t previous=eval_set_payload_owner_epoch(epoch);
    RETAIN_CHECK(rho_state_clone_map_materialize(&states,&owner,&spaces,epoch));
    StateCell *self_image=rho_state_clone_for(&states,&self);
    StateCell *left_image=rho_state_clone_for(&states,&left);
    StateCell *right_image=rho_state_clone_for(&states,&right);
    RETAIN_CHECK(self_image && left_image && right_image &&
        self_image!=&self && left_image!=&left && right_image!=&right &&
        self_image!=left_image && left_image!=right_image);
    RETAIN_CHECK(self_image->value->kind==ATOM_GROUNDED &&
        self_image->value->ground.gkind==GV_STATE && self_image->value->ground.ptr==self_image);
    RETAIN_CHECK(left_image->value->kind==ATOM_EXPR && left_image->value->expr.len==3u &&
        left_image->value->expr.elems[1]->ground.ptr==right_image &&
        left_image->value->expr.elems[2]->ground.ptr==right_image &&
        right_image->value->ground.ptr==left_image && left_image->content_type->ground.ptr==self_image);
    RETAIN_CHECK(self_image->payload_owner_epoch==epoch && left_image->payload_owner_epoch==epoch &&
        right_image->payload_owner_epoch==epoch && self.value==self_ref && right.value==left_ref);
    RhoPromotedSpaceMap promoted_spaces={0};RhoPromotedStateMap promoted_states={0};
    bool alias=false;
    Atom *first=rho_promote_payload_atom(&owner,&owner,self_ref,&spaces,&states,
        &promoted_spaces,&promoted_states,epoch,1u,&alias);
    Atom *second=rho_promote_payload_atom(&owner,&owner,left_ref,&spaces,&states,
        &promoted_spaces,&promoted_states,epoch,1u,&alias);
    Atom *same=rho_promote_payload_atom(&owner,&owner,left_ref,&spaces,&states,
        &promoted_spaces,&promoted_states,epoch,1u,&alias);
    RETAIN_CHECK(first && second && same && !alias && promoted_states.len==3u &&
        second->ground.ptr==same->ground.ptr && first->ground.ptr!=self_image);
    StateCell *stable_self=first->ground.ptr,*stable_left=second->ground.ptr;
    StateCell *stable_right=stable_left->value->expr.elems[1]->ground.ptr;
    RETAIN_CHECK(stable_self->value->ground.ptr==stable_self &&
        stable_right->value->ground.ptr==stable_left &&
        stable_left->value->expr.elems[2]->ground.ptr==stable_right &&
        stable_left->content_type->ground.ptr==stable_self);
    RETAIN_CHECK(!rho_promote_payload_atom(&owner,&owner,left_ref,&spaces,&states,
        &promoted_spaces,&promoted_states,epoch,2u,&alias) && alias);
    rho_promoted_space_map_free(&promoted_spaces);rho_promoted_state_map_free(&promoted_states);
    rho_state_clone_map_free(&states);rho_overlay_map_free(&spaces);
    eval_set_payload_owner_epoch(previous);eval_payload_frame_leave(&scope);eval_payload_frame_close(frame);
    /* Only completed exported images are read after the scratch frame dies. */
    RETAIN_CHECK(stable_self->value->ground.ptr==stable_self &&
        stable_right->value->ground.ptr==stable_left &&
        stable_self->payload_owner_epoch==0u && stable_left->payload_export_owner_epoch==1u);
    arena_free(&source);arena_free(&owner);
    puts("PASS: pending resource discovery terminates self/mutual state cycles and preserves private images and affine export ownership");
    return true;
}

static bool payload_resource_view_tests(void) {
    retained_current="raw and captured-visible state aliases share one isolated child image";
    Arena source,owner;arena_init_detached(&source);arena_init_detached(&owner);
    StateCell base={.value=atom_symbol(&source,"Base")};
    StateCell visible={.value=atom_symbol(&source,"Parent")};
    StateCell other={.value=atom_symbol(&source,"Other")};
    Atom *raw=atom_state(&source,&base),*alias=atom_state(&source,&visible);
    Registry registry;registry_init(&registry);
    registry_bind(&registry,"&resource-raw",raw);registry_bind(&registry,"&resource-visible",alias);
    Space world;space_init(&world);
    RhocalcEvalContext context={.space=&world,.registry=&registry,.persistent_arena=&owner};
    bool transactional=eval_set_payload_transactional(true);
    CettaEvalPayloadFrame *parent=eval_payload_frame_open(&owner);
    CettaEvalPayloadScope parent_scope={0},caller_scope={0},child_scope={0};
    RETAIN_CHECK(parent && eval_payload_frame_enter(parent,&parent_scope) &&
        eval_payload_note_state_redirect(&base,&visible));
    RhoPayloadJob *job=rho_payload_open(&owner,&context,
        atom_expr2(&source,atom_symbol(&source,"quote"),atom_symbol(&source,"payload-data")));
    RETAIN_CHECK(job);
    eval_payload_frame_leave(&parent_scope);eval_payload_frame_close(parent);
    CettaEvalPayloadFrame *caller=eval_payload_frame_open(&owner);
    RETAIN_CHECK(caller && eval_payload_frame_enter(caller,&caller_scope) &&
        eval_payload_note_state_redirect(&base,&other));
    uint64_t callbacks=payload_callbacks;
    RETAIN_CHECK(rho_payload_step(job,0u)==RHOCALC_PAYLOAD_PENDING &&
        job->phase==RHO_PAYLOAD_PREPARE && payload_callbacks==callbacks);
    RETAIN_CHECK(rho_payload_step(job,1u)==RHOCALC_PAYLOAD_PENDING &&
        job->phase==RHO_PAYLOAD_EVALUATE && payload_callbacks==callbacks &&
        eval_resolve_state_read(&base)==&other);
    StateCell *image=rho_state_clone_for(&job->state_map,&base);
    RETAIN_CHECK(job->state_map.len==2 && image && image!=&base && image!=&visible &&
        image==rho_state_clone_for(&job->state_map,&visible) &&
        atom_is_symbol(image->value,"Parent"));
    Atom *first=registry_lookup(&job->local_registry,"&resource-raw");
    Atom *second=registry_lookup(&job->local_registry,"&resource-visible");
    RETAIN_CHECK(first && second && first->ground.ptr==image && second->ground.ptr==image);
    RETAIN_CHECK(eval_payload_frame_enter(job->frame,&child_scope) &&
        eval_resolve_state_read(&base)==image && eval_resolve_state_read(&visible)==image);
    image->value=atom_symbol(&owner,"Child");
    RETAIN_CHECK(atom_is_symbol(visible.value,"Parent") && atom_is_symbol(base.value,"Base"));
    eval_payload_frame_leave(&child_scope);
    rho_payload_close(job);
    RETAIN_CHECK(eval_resolve_state_read(&base)==&other);
    eval_payload_frame_leave(&caller_scope);eval_payload_frame_close(caller);
    eval_set_payload_transactional(transactional);
    registry_free(&registry);space_free(&world);arena_free(&source);arena_free(&owner);
    puts("PASS: payload preparation uses the captured parent view and coalesces raw/visible state aliases without changing its caller");
    return true;
}

static bool payload_resource_tracking_refusal_test(void) {
    retained_current="refused scratch tracking releases the unowned state shell";
    Arena owner;arena_init_detached(&owner);
    StateCell original={0};
    RhoStateCloneMap states={0};RhoOverlayMap spaces={0};bool inserted=false;
    RETAIN_CHECK(rho_state_clone_map_note(&states,&original,&inserted) && inserted);
    CettaEvalPayloadFrame *frame=eval_payload_frame_open(&owner);
    CettaEvalPayloadScope scope={0};
    RETAIN_CHECK(frame && eval_payload_frame_enter(frame,&scope));
    resource_tracking_refusal=true;resource_refused_frees=0u;resource_refused_cell=NULL;
    bool materialized=rho_state_clone_map_materialize(&states,&owner,&spaces,1u);
    bool released=!materialized && !resource_tracking_refusal &&
        resource_refused_frees==1u && !resource_refused_cell && !states.clone[0];
    /* Clean a deliberately regressed producer before reporting its failed
     * ownership assertion; a fixture leak is not evidence of this contract. */
    if (resource_refused_cell) {free(resource_refused_cell);resource_refused_cell=NULL;}
    rho_state_clone_map_free(&states);rho_overlay_map_free(&spaces);
    eval_payload_frame_leave(&scope);eval_payload_frame_close(frame);arena_free(&owner);
    RETAIN_CHECK(released);
    puts("PASS: refused state tracking releases the unowned shell before returning");
    return true;
}

static bool payload_space_cleanup_owner_test(bool cycle,bool refuse_tracking) {
    retained_current=refuse_tracking ? "refused space tracking precedes recursive exposure" :
        cycle ? "refused cyclic space graph retains one lifetime owner per shell" :
                "refused partial space has a lifetime owner";
    Arena owner;arena_init_detached(&owner);
    Space parent,child;space_init(&parent);space_init(&child);
    parent.payload_owner_epoch=child.payload_owner_epoch=1u;
    if (cycle) {
        space_add(&parent,atom_space(&owner,&child));
        space_add(&child,atom_space(&owner,&parent));
    } else space_add(&parent,atom_int(&owner,11));
    space_add(&parent,atom_int(&owner,22));
    RhoOverlayMap overlays={0};RhoStateCloneMap states={0};
    RhoPromotedSpaceMap promoted_spaces={0};RhoPromotedStateMap promoted_states={0};
    bool alias=false;
    memset(resource_owned_spaces,0,sizeof(resource_owned_spaces));
    resource_owned_count=resource_owned_frees=0u;resource_partial_space=NULL;
    resource_space_active=true;resource_space_tracking_refusal=refuse_tracking;
    resource_space_storage_refusal=!refuse_tracking;resource_space_storage_failed=false;
    Space *image=rho_promote_payload_space(&owner,&parent,&overlays,&states,
        &promoted_spaces,&promoted_states,1u,1u,&alias);
    unsigned expected=refuse_tracking ? 0u : cycle ? 2u : 1u;
    bool failed=!image && !alias && !resource_space_tracking_refusal &&
        resource_owned_count==expected &&
        (refuse_tracking || (!resource_space_storage_refusal && resource_space_storage_failed));
    if (!refuse_tracking && resource_partial_space)
        failed=failed && resource_partial_space->instance_id!=0u &&
            space_length64(resource_partial_space)==1u;
    eval_cleanup_owned_new_spaces(NULL,NULL);
    bool retired=resource_owned_frees==(refuse_tracking ? 1u : expected) &&
        resource_partial_space && resource_partial_space->instance_id==0u;
    for (unsigned i=0;i<resource_owned_count;i++)
        retired=retired && resource_owned_spaces[i]->instance_id==0u;
    /* Release a deliberately unowned baseline after collecting its assertion.
     * Cleanup failures are tested directly, not counted as sanitizer crashes. */
    if (resource_partial_space && resource_partial_space->instance_id)
        space_free(resource_partial_space);
    resource_space_active=false;
    rho_promoted_space_map_free(&promoted_spaces);rho_promoted_state_map_free(&promoted_states);
    rho_overlay_map_free(&overlays);rho_state_clone_map_free(&states);
    bool sources_intact=space_length64(&parent)==2u && space_length64(&child)==(cycle ? 1u : 0u);
    space_free(&parent);space_free(&child);arena_free(&owner);
    RETAIN_CHECK(failed && retired && sources_intact);
    return true;
}

typedef struct {
    RhocalcRun *run;
    unsigned *releases, *producer_calls;
} RetainedRhoOwner;

static CettaOwnedReleaseStep retained_rho_owner_release_poll(void *raw,
    uint64_t allowance, bool atomic, uint64_t *used) {
    RetainedRhoOwner *owner = raw;
    return rhocalc_run_release_poll(&owner->run, allowance, atomic, used);
}

static void retained_rho_owner_release(void *raw) {
    RetainedRhoOwner *owner = raw;
    rhocalc_run_close(owner->run);
    ++*owner->releases;
    free(owner);
}

static CettaOwnedExecutionStep retained_rho_owner_next(void *raw,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    (void)returned; (void)completion;
    RetainedRhoOwner *owner = raw;
    ++*owner->producer_calls;
    return CETTA_OWNED_EXECUTION_PAUSED;
}

static size_t dense_storage_blocks(uint32_t length) {
    if (!length) return 0u;
    return 1u + ((uint64_t)length+63u)/64u +
        ((uint64_t)length+8191u)/8192u +
        ((uint64_t)length+1048575u)/1048576u +
        ((uint64_t)length+134217727u)/134217728u;
}

static size_t storage_blocks(const CettaSegmentedArray *array) {
    size_t count=0u;
    for (CettaSegmentedBlock *block=array->blocks;block;block=block->next) count++;
    return count;
}

static bool segmented_storage_tests(void) {
    retained_current="bounded indexed storage: high indices, failure/retry, revocation and retirement";
    CettaSegmentedArray array={0};
    RETAIN_CHECK(!cetta_segmented_array_init(&array,0u));
    RETAIN_CHECK(!cetta_segmented_array_init(&array,CETTA_SEGMENTED_MAX_ELEMENT_SIZE+1u));
    RETAIN_CHECK(cetta_segmented_array_init(&array,sizeof(uint64_t)));
    const uint32_t indices[]={0u,1u,63u,64u,8191u,8192u,1048576u,134217728u,UINT32_MAX};
    uint64_t *addresses[sizeof(indices)/sizeof(*indices)]={0};
    for (size_t i=0u;i<sizeof(indices)/sizeof(*indices);i++) {
        unsigned pending=0u;
        CettaSegmentedStep status;
        do {
            size_t before=storage_blocks(&array),calls=block_allocation_calls;
            status=cetta_segmented_array_prepare(&array,indices[i]);
            RETAIN_CHECK(status!=CETTA_SEGMENTED_REFUSED && block_allocation_calls-calls<=1u &&
                storage_blocks(&array)-before==(status==CETTA_SEGMENTED_PENDING));
            pending+=status==CETTA_SEGMENTED_PENDING;
        } while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(pending<=5u);
        addresses[i]=cetta_segmented_array_at(&array,indices[i]);
        RETAIN_CHECK(addresses[i] && *addresses[i]==0u);
        *addresses[i]=(uint64_t)i+17u;
        for(size_t j=0u;j<=i;j++)
            RETAIN_CHECK(cetta_segmented_array_at(&array,indices[j])==addresses[j] &&
                *addresses[j]==j+17u);
    }
    size_t blocks=storage_blocks(&array),retired=0u;
    while (array.blocks) {
        bool done=cetta_segmented_array_release_one(&array);
        retired++;
        RETAIN_CHECK(!cetta_segmented_array_at(&array,0u) &&
            storage_blocks(&array)+retired==blocks && done==!array.blocks);
        if (!done) RETAIN_CHECK(cetta_segmented_array_prepare(&array,0u)==CETTA_SEGMENTED_REFUSED);
    }
    RETAIN_CHECK(!array.element_size && !array.retiring && !array.root);
    for(size_t fail=1u;fail<=5u;fail++) {
        RETAIN_CHECK(cetta_segmented_array_init(&array,sizeof(uint64_t)));
        block_allocation_calls=0u;block_fail_at=fail;block_failure_injected=false;
        CettaSegmentedStep status;
        do { status=cetta_segmented_array_prepare(&array,UINT32_MAX); }
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected &&
            storage_blocks(&array)==fail-1u && !cetta_segmented_array_at(&array,UINT32_MAX));
        CettaSegmentedBlock *held=array.blocks;
        block_fail_at=0u;
        unsigned resumed=0u;
        do {status=cetta_segmented_array_prepare(&array,UINT32_MAX);resumed++;}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_READY && resumed==7u-fail &&
            storage_blocks(&array)==5u);
        bool retained=!held;
        for(CettaSegmentedBlock *block=array.blocks;block;block=block->next)
            retained|=block==held;
        RETAIN_CHECK(retained);
        uint64_t *value=cetta_segmented_array_at(&array,UINT32_MAX);
        RETAIN_CHECK(value);*value=99u;
        RETAIN_CHECK(cetta_segmented_array_prepare(&array,UINT32_MAX)==CETTA_SEGMENTED_READY && *value==99u);
        cetta_segmented_array_free(&array);
    }
    /* A failed extension must preserve initialized cells, not merely the
     * empty prefix. The high index takes a disjoint route below one root. */
    for(size_t fail=1u;fail<=4u;fail++) {
        RETAIN_CHECK(cetta_segmented_array_init(&array,sizeof(uint64_t)));
        while(cetta_segmented_array_prepare(&array,0u)==CETTA_SEGMENTED_PENDING) {}
        uint64_t *first=cetta_segmented_array_at(&array,0u);
        RETAIN_CHECK(first);*first=731u;
        block_allocation_calls=0u;block_fail_at=fail;block_failure_injected=false;
        CettaSegmentedStep status;
        do {status=cetta_segmented_array_prepare(&array,UINT32_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected &&
            cetta_segmented_array_at(&array,0u)==first && *first==731u);
        block_fail_at=0u;
        do {status=cetta_segmented_array_prepare(&array,UINT32_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_READY &&
            cetta_segmented_array_at(&array,0u)==first && *first==731u);
        cetta_segmented_array_free(&array);
    }
    RETAIN_CHECK(cetta_segmented_array_init(&array,sizeof(uint64_t)));
    size_t request=sizeof(CettaSegmentedBlock)+CETTA_SEGMENTED_DIRECTORY_CELLS*sizeof(CettaSegmentedBlock *);
    size_t calls=block_allocation_calls;
    RETAIN_CHECK(cetta_segmented_array_prepare_limit(&array,0u,request-1u)==CETTA_SEGMENTED_REFUSED &&
        !array.blocks && !array.live_bytes && block_allocation_calls==calls);
    RETAIN_CHECK(cetta_segmented_array_prepare_limit(&array,0u,request)==CETTA_SEGMENTED_PENDING &&
        array.live_bytes==request);
    CettaSegmentedBlock *held=array.blocks;
    RETAIN_CHECK(cetta_segmented_array_prepare_limit(&array,0u,2u*request-1u)==CETTA_SEGMENTED_REFUSED &&
        array.blocks==held && array.live_bytes==request);
    RETAIN_CHECK(cetta_segmented_array_prepare_limit(&array,0u,2u*request)==CETTA_SEGMENTED_PENDING &&
        array.live_bytes==2u*request);
    size_t previous=array.live_bytes;
    RETAIN_CHECK(!cetta_segmented_array_release_one(&array) && array.live_bytes==previous-request);
    RETAIN_CHECK(cetta_segmented_array_release_one(&array) && !array.live_bytes);
    CettaSegmentedArray64 wide={0};
    RETAIN_CHECK(cetta_segmented_array64_init(&wide,sizeof(uint64_t)));
    const uint64_t wide_indices[]={0u,63u,64u,UINT32_MAX,
        (uint64_t)UINT32_MAX+1u,UINT64_C(1)<<63,UINT64_MAX};
    uint64_t *wide_addresses[sizeof(wide_indices)/sizeof(*wide_indices)]={0};
    for(size_t i=0u;i<sizeof(wide_indices)/sizeof(*wide_indices);i++) {
        unsigned pending=0u;
        CettaSegmentedStep status;
        do {
            size_t before=storage_blocks(&wide.backing),calls=block_allocation_calls;
            status=cetta_segmented_array64_prepare(&wide,wide_indices[i]);
            RETAIN_CHECK(status!=CETTA_SEGMENTED_REFUSED && block_allocation_calls-calls<=1u &&
                storage_blocks(&wide.backing)-before==(status==CETTA_SEGMENTED_PENDING));
            pending+=status==CETTA_SEGMENTED_PENDING;
        } while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(pending<=10u);
        wide_addresses[i]=cetta_segmented_array64_at(&wide,wide_indices[i]);
        RETAIN_CHECK(wide_addresses[i] && !*wide_addresses[i]);
        *wide_addresses[i]=(uint64_t)i+31u;
        for(size_t j=0u;j<=i;j++)
            RETAIN_CHECK(cetta_segmented_array64_at(&wide,wide_indices[j])==wide_addresses[j] &&
                *wide_addresses[j]==j+31u);
    }
    blocks=storage_blocks(&wide.backing);retired=0u;
    while(wide.backing.blocks) {
        bool done=cetta_segmented_array64_release_one(&wide);retired++;
        RETAIN_CHECK(!cetta_segmented_array64_at(&wide,0u) &&
            storage_blocks(&wide.backing)+retired==blocks && done==!wide.backing.blocks);
        if(!done)RETAIN_CHECK(cetta_segmented_array64_prepare(&wide,0u)==CETTA_SEGMENTED_REFUSED);
    }
    for(size_t fail=1u;fail<=10u;fail++) {
        RETAIN_CHECK(cetta_segmented_array64_init(&wide,sizeof(uint64_t)));
        block_allocation_calls=0u;block_fail_at=fail;block_failure_injected=false;
        CettaSegmentedStep status;
        do {status=cetta_segmented_array64_prepare(&wide,UINT64_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected &&
            storage_blocks(&wide.backing)==fail-1u &&
            !cetta_segmented_array64_at(&wide,UINT64_MAX));
        CettaSegmentedBlock *held=wide.backing.blocks;
        block_fail_at=0u;
        do {status=cetta_segmented_array64_prepare(&wide,UINT64_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_READY && storage_blocks(&wide.backing)==10u);
        bool retained=!held;
        for(CettaSegmentedBlock *block=wide.backing.blocks;block;block=block->next)
            retained|=block==held;
        RETAIN_CHECK(retained);
        uint64_t *value=cetta_segmented_array64_at(&wide,UINT64_MAX);
        RETAIN_CHECK(value);*value=117u;
        RETAIN_CHECK(cetta_segmented_array64_prepare(&wide,UINT64_MAX)==CETTA_SEGMENTED_READY &&
            *value==117u);
        cetta_segmented_array64_free(&wide);
    }
    for(size_t fail=1u;fail<=9u;fail++) {
        RETAIN_CHECK(cetta_segmented_array64_init(&wide,sizeof(uint64_t)));
        while(cetta_segmented_array64_prepare(&wide,0u)==CETTA_SEGMENTED_PENDING) {}
        uint64_t *first=cetta_segmented_array64_at(&wide,0u);
        RETAIN_CHECK(first);*first=731u;
        block_allocation_calls=0u;block_fail_at=fail;block_failure_injected=false;
        CettaSegmentedStep status;
        do {status=cetta_segmented_array64_prepare(&wide,UINT64_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected &&
            cetta_segmented_array64_at(&wide,0u)==first && *first==731u);
        block_fail_at=0u;
        do {status=cetta_segmented_array64_prepare(&wide,UINT64_MAX);}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_READY &&
            cetta_segmented_array64_at(&wide,0u)==first && *first==731u);
        cetta_segmented_array64_free(&wide);
    }
    RETAIN_CHECK(cetta_segmented_array64_init(&wide,sizeof(uint64_t)));
    calls=block_allocation_calls;
    RETAIN_CHECK(cetta_segmented_array64_prepare_limit(&wide,0u,request-1u)==CETTA_SEGMENTED_REFUSED &&
        !wide.backing.blocks && block_allocation_calls==calls);
    RETAIN_CHECK(cetta_segmented_array64_prepare_limit(&wide,0u,request)==CETTA_SEGMENTED_PENDING &&
        wide.backing.live_bytes==request);
    cetta_segmented_array64_free(&wide);
    puts("PASS: bounded indexed storage preserves addresses and initialized values across 32- and 64-bit indices, retries allocation boundaries, enforces backing budgets and retires one block per step");
    return true;
}

typedef struct {
    uint64_t serial, progress;
    unsigned char payload[352];
} TestFixedRecord;

static bool prepare_test_record(CettaRecordStack *stack,TestFixedRecord *pending) {
    CettaSegmentedStep status;
    do {
        size_t before=block_allocation_calls;
        status=cetta_record_stack_prepare(stack,pending);
        RETAIN_CHECK(status!=CETTA_SEGMENTED_REFUSED && block_allocation_calls-before<=1u);
        if(block_allocation_calls!=before)
            RETAIN_CHECK(block_last_request<=sizeof(CettaSegmentedBlock)+128u*sizeof(void *));
    } while(status==CETTA_SEGMENTED_PENDING);
    return true;
}

static bool retained_record_stack_tests(void) {
    retained_current="fixed-record stack request, failure preservation, pointer stability and LIFO ownership";
    CettaRecordStack stack={0};TestFixedRecord pending={0};
    RETAIN_CHECK(sizeof(pending)>CETTA_SEGMENTED_MAX_ELEMENT_SIZE &&
        !cetta_record_stack_request(&stack,0u) && !stack.length && !stack.record_size);
    TestFixedRecord *addresses[129]={0};
    for(unsigned i=0u;i<129u;i++) {
        pending=(TestFixedRecord){.serial=i,.progress=3u*i};
        pending.payload[sizeof(pending.payload)-1u]=(unsigned char)(0xa5u^i);
        if(i) addresses[i-1u]->progress++;
        size_t calls=block_allocation_calls;
        RETAIN_CHECK(cetta_record_stack_request(&stack,sizeof(pending)) && stack.length==i+1u &&
            stack.pending && block_allocation_calls==calls &&
            cetta_record_stack_at(&stack,i,&pending)==&pending);
        CettaRecordStack held=stack;
        RETAIN_CHECK(!cetta_record_stack_request(&stack,sizeof(pending)) &&
            !memcmp(&held,&stack,sizeof(stack)) &&
            cetta_record_stack_release_storage_one(&stack)==CETTA_SEGMENTED_REFUSED &&
            !memcmp(&held,&stack,sizeof(stack)));
        RETAIN_CHECK(prepare_test_record(&stack,&pending));
        addresses[i]=cetta_record_stack_at(&stack,i,&pending);
        RETAIN_CHECK(addresses[i] && addresses[i]!=&pending && addresses[i]->serial==i &&
            addresses[i]->progress==3u*i && !stack.pending && !memcmp(addresses[i],&pending,sizeof(pending)));
        for(unsigned j=0u;j<i;j++)
            RETAIN_CHECK(cetta_record_stack_at(&stack,j,&pending)==addresses[j] &&
                addresses[j]->serial==j && addresses[j]->progress==3u*j+1u);
    }
    CettaRecordStack held=stack;
    RETAIN_CHECK(!cetta_record_stack_request(&stack,sizeof(pending)+1u) &&
        !memcmp(&held,&stack,sizeof(stack)));
    for(unsigned i=129u;i>0u;i--) {
        TestFixedRecord *top=cetta_record_stack_at(&stack,i-1u,&pending);
        RETAIN_CHECK(top==addresses[i-1u] && top->serial==i-1u);
        retirement_key_count=0u;retirement_nonnull_frees=0u;key_retirement_tracking=true;
        bool popped=cetta_record_stack_pop(&stack);
        key_retirement_tracking=false;
        RETAIN_CHECK(popped && retirement_nonnull_frees==1u && stack.length==i-1u &&
            !cetta_record_stack_at(&stack,i-1u,&pending));
    }
    RETAIN_CHECK(!cetta_record_stack_pop(&stack));
    /* Reuse reads an actually cleared slot, not a stale freed record. */
    pending=(TestFixedRecord){.serial=733u};
    RETAIN_CHECK(cetta_record_stack_request(&stack,sizeof(pending)) && prepare_test_record(&stack,&pending) &&
        ((TestFixedRecord *)cetta_record_stack_at(&stack,0u,&pending))->serial==733u &&
        cetta_record_stack_pop(&stack));
    size_t blocks=storage_blocks(&stack.slots),releases=0u;
    do {
        size_t before=storage_blocks(&stack.slots);
        CettaSegmentedStep status=cetta_record_stack_release_storage_one(&stack);releases++;
        RETAIN_CHECK(status!=CETTA_SEGMENTED_REFUSED && before-storage_blocks(&stack.slots)<=1u);
        if(status==CETTA_SEGMENTED_READY) break;
    } while(true);
    RETAIN_CHECK(releases==blocks && !stack.slots.blocks);
    /* Four directories, one pointer page, then one record allocation. Every
     * failure retains the accepted typed value, including partially installed backing. */
    for(unsigned failure=1u;failure<=6u;failure++) {
        stack=(CettaRecordStack){0};pending=(TestFixedRecord){.serial=991u,.progress=42u};
        RETAIN_CHECK(cetta_record_stack_request(&stack,sizeof(pending)));
        block_fail_at=block_allocation_calls+failure;block_failure_injected=false;
        CettaSegmentedStep status;
        do {status=cetta_record_stack_prepare(&stack,&pending);} while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected && stack.pending &&
            stack.length==1u && cetta_record_stack_at(&stack,0u,&pending)==&pending &&
            pending.serial==991u && pending.progress==42u);
        block_fail_at=0u;
        RETAIN_CHECK(prepare_test_record(&stack,&pending) &&
            ((TestFixedRecord *)cetta_record_stack_at(&stack,0u,&pending))->serial==991u &&
            cetta_record_stack_pop(&stack) && cetta_record_stack_free_storage(&stack));
    }
    /* A failed next-page install or fixed record leaves all committed frames
     * and their already-earned progress unchanged. */
    for(unsigned failure=1u;failure<=2u;failure++) {
        stack=(CettaRecordStack){0};
        for(unsigned i=0u;i<64u;i++) {
            pending=(TestFixedRecord){.serial=i,.progress=i+11u};
            RETAIN_CHECK(cetta_record_stack_request(&stack,sizeof(pending)) && prepare_test_record(&stack,&pending));
            addresses[i]=cetta_record_stack_at(&stack,i,&pending);
        }
        pending=(TestFixedRecord){.serial=64u,.progress=123u};
        RETAIN_CHECK(cetta_record_stack_request(&stack,sizeof(pending)));
        block_fail_at=block_allocation_calls+failure;block_failure_injected=false;
        CettaSegmentedStep status;
        do {status=cetta_record_stack_prepare(&stack,&pending);} while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_REFUSED && block_failure_injected && stack.pending && stack.length==65u);
        for(unsigned i=0u;i<64u;i++)
            RETAIN_CHECK(cetta_record_stack_at(&stack,i,&pending)==addresses[i] &&
                addresses[i]->serial==i && addresses[i]->progress==i+11u);
        block_fail_at=0u;
        /* Cancellation may drop the pending value without first allocating it. */
        size_t calls=block_allocation_calls;
        RETAIN_CHECK(cetta_record_stack_pop(&stack) && !stack.pending && stack.length==64u &&
            block_allocation_calls==calls);
        while(stack.length) RETAIN_CHECK(cetta_record_stack_pop(&stack));
        RETAIN_CHECK(cetta_record_stack_free_storage(&stack));
    }
    CettaRecordStack exhausted={.record_size=sizeof(pending),.length=UINT32_MAX};held=exhausted;
    RETAIN_CHECK(!cetta_record_stack_request(&exhausted,sizeof(pending)) && !memcmp(&held,&exhausted,sizeof(held)));
    puts("PASS: fixed-record stacks preserve queued requests and earlier addresses, retry eight real allocation failures, reject live backing disposal and drain cleared LIFO slots/blocks");
    return true;
}

static bool retained_frame_provider_refusal_tests(void) {
    retained_current="all four actual typed frame providers retain refused push ownership";
    for(unsigned kind=0u;kind<4u;kind++) for(unsigned failure=1u;failure<=6u;failure++) {
        Arena source,result;arena_init_detached(&source);arena_init_detached(&result);
        Atom *nil=rho_nil(&source),*replacement=rho_unary(&source,"rho:quote",nil);
        RhoKeyCursor *key=NULL;RhoCheckCursor check={0};RhoNormalizeCursor normalize={.arena=&result};
        RhoSubstitutionCursor substitution={0};CettaRecordStack *stack=NULL;void *pending=NULL;
        if(kind==0u) {
            key=rho_key_cursor_new(nil,false);RETAIN_CHECK(key);
            stack=&key->frames;pending=&key->pending_frame;
        } else if(kind==1u) {
            RETAIN_CHECK(rho_check_cursor_push(&check,nil,false));stack=&check.frames;pending=&check.pending_frame;
        } else if(kind==2u) {
            RETAIN_CHECK(rho_normalize_cursor_push(&normalize,nil,false));stack=&normalize.frames;pending=&normalize.pending_frame;
        } else {
            RETAIN_CHECK(rho_substitution_begin(&substitution,&result,nil,VAR_ID_NONE,replacement));
            stack=&substitution.frames;pending=&substitution.pending_frame;
        }
        RETAIN_CHECK(stack->pending && stack->length==1u && cetta_record_stack_at(stack,0u,pending)==pending);
        block_fail_at=block_allocation_calls+failure;block_failure_injected=false;
        bool failed=false;RhocalcRunAccount account={0};
        for(unsigned steps=0u;steps<16u && !failed;steps++) {
            if(kind==0u) failed=!rho_key_cursor_advance(key);
            else if(kind==1u) failed=rho_check_cursor_advance(&check,&account)==RHO_CHECK_REFUSED;
            else if(kind==2u) failed=!rho_normalize_cursor_advance(&normalize,&account);
            else failed=!rho_substitution_advance(&substitution);
        }
        RETAIN_CHECK(failed && block_failure_injected && stack->pending && stack->length==1u &&
            cetta_record_stack_at(stack,0u,pending)==pending && !normalize.returned && !substitution.returned &&
            !normalize.done && !substitution.done && (!key || !key->done));
        block_fail_at=0u;
        rho_key_cursor_free(key);rho_check_cursor_free(&check);rho_normalize_cursor_free(&normalize);
        rho_substitution_cursor_clear(&substitution);arena_free(&result);arena_free(&source);
    }
    puts("PASS: check/key/normalization/substitution retain exact pending frames through 24 actual backing/record allocation refusals without publishing results");
    return true;
}

static bool retained_rejected_payload_retirement_test(void) {
    retained_current="deep rejected payload retains checker cleanup before its caller advances";
    Arena source,result;arena_init_detached(&source);arena_init_detached(&result);
    Atom *nil=rho_nil(&source),*name=rho_unary(&source,"rho:quote",nil);
    Atom *payload=atom_int(&source,17);
    for(unsigned i=0u;i<128u;i++) payload=rho_binary(&source,"rho:send",name,payload);
    RhoSubstitutionCursor cursor={0};
    RETAIN_CHECK(rho_substitution_begin_payload(&cursor,&result,nil,VAR_ID_NONE,payload));
    bool retained_rejection=false;
    for(unsigned polls=0u;polls<200000u && !cursor.done;polls++) {
        retirement_key_count=0u;retirement_nonnull_frees=0u;key_retirement_tracking=true;
        bool okay=rho_substitution_advance(&cursor);
        key_retirement_tracking=false;
        RETAIN_CHECK(okay && retirement_nonnull_frees<=1u);
        if(cursor.check.status==RHO_CHECK_INCOMPATIBLE && cursor.check.frames.length) {
            retained_rejection=true;
            RETAIN_CHECK(!cursor.returned && rho_substitution_frame_at(&cursor,0u)->stage==1u);
        }
    }
    RETAIN_CHECK(cursor.done && retained_rejection && atom_eq(cursor.returned,nil));
    rho_substitution_cursor_clear(&cursor);
    /* Non-quiet errors survive intervening peer diagnostics; quiet shape
     * interpretation leaves the current caller diagnostic alone. */
    for(unsigned quiet=0u;quiet<2u;quiet++) {
        RhoCheckCursor check={.quiet_shape=quiet};RhocalcRunAccount account={0};
        RETAIN_CHECK(rho_check_cursor_push(&check,payload,false));
        rho_validation_clear();if(quiet) rho_validation_set("earlier diagnostic");
        bool saw_retirement=false;
        char expected[256]={0};RhoCheckStatus status=RHO_CHECK_PENDING;
        for(unsigned polls=0u;polls<200000u && status==RHO_CHECK_PENDING;polls++) {
            status=rho_check_cursor_advance(&check,&account);
            if(check.status==RHO_CHECK_INCOMPATIBLE && check.frames.length && !saw_retirement) {
                saw_retirement=true;snprintf(expected,sizeof(expected),"%s",rhocalc_last_validation_error());
                rho_validation_clear();rho_validation_set("peer diagnostic");
            }
        }
        RETAIN_CHECK(status==RHO_CHECK_INCOMPATIBLE && saw_retirement && !check.frames.length &&
            !check.frames.slots.blocks && !check.memo.slots.blocks && !check.memo_cursor.grown.blocks &&
            !strcmp(rhocalc_last_validation_error(),quiet ? "peer diagnostic" : expected));
        rho_check_cursor_free(&check);
    }
    arena_free(&result);arena_free(&source);
    puts("PASS: deep incompatible payloads finish retained checker disposal before body interpretation and preserve quiet/error diagnostics");
    return true;
}

static bool retained_nested_key_retirement_test(void) {
    retained_current="actual normalization cancellation retires a deep in-flight key stack one action at a time";
    Arena source,result;arena_init_detached(&source);arena_init_detached(&result);
    Atom *nil=rho_nil(&source),*name=rho_unary(&source,"rho:quote",nil),*deep=nil;
    for(unsigned i=0u;i<128u;i++) deep=rho_binary(&source,"rho:send",name,deep);
    Atom *input=rho_binary(&source,"rho:par",deep,rho_binary(&source,"rho:send",name,nil));
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
    RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,NULL);RETAIN_CHECK(run);
    bool reached=false;
    for(unsigned polls=0u;polls<200000u && !reached;polls++) {
        if(run->phase==RHO_RUN_NORMALIZE && run->normalization.frames.length==1u) {
            RhoNormalizeFrame *frame=rho_normalize_frame_at(&run->normalization,0u);
            reached=frame->key && frame->key->frames.length>=64u;
        }
        if(!reached) RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
    }
    RETAIN_CHECK(reached && !run->machine.current);
    CettaOwnedReleaseStep status;uint64_t used=0u;
    do {
        retirement_key_count=0u;retirement_nonnull_frees=0u;key_retirement_tracking=true;
        status=rhocalc_run_release_poll(&run,1u,false,&used);
        key_retirement_tracking=false;
        RETAIN_CHECK(run && used<=1u && retirement_nonnull_frees<=1u && status!=CETTA_OWNED_RELEASE_COMPLETE);
    } while(status==CETTA_OWNED_RELEASE_PENDING);
    RETAIN_CHECK(status==CETTA_OWNED_RELEASE_ATOMIC && !run->normalization.frames.slots.blocks &&
        !run->normalization.frames.length && !run->machine.current);
    RETAIN_CHECK(rhocalc_run_release_poll(&run,1u,true,NULL)==CETTA_OWNED_RELEASE_COMPLETE && !run);
    arena_free(&result);arena_free(&source);
    puts("PASS: deep pending key frames, text and pointer backing retire before their enclosing normalization record");
    return true;
}

static bool retained_frame_pop_admission_tests(void) {
    retained_current="actual normalization frame payload inventory precedes record/backing release";
    for(unsigned committed=0u;committed<2u;committed++) {
        Arena source;arena_init_detached(&source);RhoNormalizeCursor cursor={0};
        RETAIN_CHECK(rho_normalize_cursor_push(&cursor,rho_nil(&source),false));
        RhoNormalizeFrame *frame=rho_normalize_frame_at(&cursor,0u);
        CettaSegmentedStep status;
        do {status=rho_sort_buffer_prepare(&frame->items,0u,sizeof(RhoKeyedAtom));}
        while(status==CETTA_SEGMENTED_PENDING);
        RETAIN_CHECK(status==CETTA_SEGMENTED_READY);
        char *key=strdup("owned-frame-key");RETAIN_CHECK(key);
        *rho_keyed_row(&frame->items,0u)=(RhoKeyedAtom){.key=key,.ordinal=7u};frame->length=1u;
        if(committed) {
            do {status=cetta_record_stack_prepare(&cursor.frames,&cursor.pending_frame);}
            while(status==CETTA_SEGMENTED_PENDING);
            RETAIN_CHECK(status==CETTA_SEGMENTED_READY);
            frame=rho_normalize_frame_at(&cursor,0u);
            RETAIN_CHECK(frame!=&cursor.pending_frame && rho_keyed_row(&frame->items,0u)->key==key);
        }
        RETAIN_CHECK(!rho_normalize_record_pop(&cursor) && cursor.frames.length==1u &&
            rho_keyed_row(&frame->items,0u)->key==key &&
            cetta_record_stack_release_storage_one(&cursor.frames)==CETTA_SEGMENTED_REFUSED);
        retirement_key_count=1u;retirement_keys[0]=key;retirement_key_frees[0]=0u;
        key_retirement_tracking=true;
        while(cursor.frames.length) {
            retirement_nonnull_frees=0u;rho_normalize_cursor_release_one(&cursor);
            RETAIN_CHECK(retirement_nonnull_frees<=1u);
        }
        key_retirement_tracking=false;
        RETAIN_CHECK(retirement_key_frees[0]==1u && cetta_record_stack_free_storage(&cursor.frames));
        rho_normalize_cursor_free(&cursor);arena_free(&source);
    }
    retirement_key_count=0u;
    puts("PASS: actual typed pending/installed frame pop refuses owned keys/backing and retires the moved inventory exactly once");
    return true;
}

static bool retained_sort_retirement_tests(void) {
    retained_current="actual partial merge and block-retirement cancellation";
    enum { width=257u };
    for(unsigned cut=0u;cut<7u;cut++) {
        Arena source,result;
        arena_init_detached(&source);arena_init_detached(&result);
        Atom *leaf=rho_binary(&source,"rho:send",
            rho_unary(&source,"rho:quote",rho_nil(&source)),rho_nil(&source));
        Atom *children[width+1u];children[0]=atom_symbol(&source,"rho:par");
        for(unsigned i=0u;i<width;i++) children[i+1u]=leaf;
        Atom *input=atom_expr(&source,children,width+1u);
        RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,NULL);
        RETAIN_CHECK(run);
        bool reached=false;
        for(uint64_t steps=0u;steps<200000u && !reached;steps++) {
            RhoNormalizeFrame *frame=run->normalization.frames.length ? rho_normalize_frame_at(&run->normalization,0) : NULL;
            if(frame && frame->stage==5u) {
                RhoStableSort *sort=&frame->sort;
                reached=cut==0u ? !sort->scratch.pages.blocks && !sort->output :
                    cut==1u ? sort->scratch.pages.blocks && !sort->output && sort->width==1u :
                    cut==2u ? sort->output>64u && sort->output<width && sort->width==1u :
                    cut==3u ? sort->output==width && !sort->range_ready && !sort->done :
                    cut==4u ? sort->width==2u && !sort->output :
                    cut==5u ? sort->scratch.pages.retiring : false;
            } else if(frame && cut==6u)
                reached=frame->stage==10u && frame->items.pages.retiring;
            if(!reached) RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
        }
        RETAIN_CHECK(reached && !run->machine.current);
        RhoNormalizeFrame *frame=rho_normalize_frame_at(&run->normalization,0);
        retirement_key_count=0u;memset(retirement_key_frees,0,sizeof(retirement_key_frees));
        for(uint32_t i=0u;i<frame->length;i++) {
            char *key=rho_keyed_row(&frame->items,i)->key;
            if(key) retirement_keys[retirement_key_count++]=key;
        }
        RETAIN_CHECK(retirement_key_count==(cut==6u ? 0u : width));
        key_retirement_tracking=true;
        CettaOwnedReleaseStep status;
        do {
            uint64_t used=0u;retirement_nonnull_frees=0u;
            status=rhocalc_run_release_poll(&run,1u,false,&used);
            RETAIN_CHECK(run && used<=1u && retirement_nonnull_frees<=1u &&
                status!=CETTA_OWNED_RELEASE_COMPLETE);
        } while(status==CETTA_OWNED_RELEASE_PENDING);
        key_retirement_tracking=false;
        RETAIN_CHECK(status==CETTA_OWNED_RELEASE_ATOMIC);
        for(unsigned i=0u;i<retirement_key_count;i++) RETAIN_CHECK(retirement_key_frees[i]==1u);
        RETAIN_CHECK(rhocalc_run_release_poll(&run,1u,true,NULL)==CETTA_OWNED_RELEASE_COMPLETE && !run);
        arena_free(&result);arena_free(&source);
    }
    puts("PASS: actual normalization cancellation retires each key once across scratch installation, partial/full merge passes, owner swap and incremental scratch/primary disposal");
    return true;
}

/* The independently sorted flat array is an occurrence/key oracle. During
 * every retained pass only the complete primary owns keys; scratch borrows. */
static bool retained_endpoint_storage_tests(void) {
    retained_current="paged endpoint sorting, complete-primary ownership and incremental scratch retirement";
    const unsigned widths[]={0u,1u,2u,3u,63u,64u,65u,129u};
    for (unsigned trial=0u;trial<sizeof(widths)/sizeof(*widths);trial++) {
        unsigned width=widths[trial];
        RhoEndpointVec vec={.storage={.paged=true}};
        RhoEndpoint oracle[129]={0};char *keys[129]={0};
        for (unsigned i=0u;i<width;i++) {
            char text[32];snprintf(text,sizeof(text),"channel-%03u",(i*17u)%11u);
            keys[i]=strdup(text);RETAIN_CHECK(keys[i]);
            CettaSegmentedStep step;
            do {
                size_t calls=block_allocation_calls;
                step=rho_sort_buffer_prepare(&vec.storage,vec.len,sizeof(RhoEndpoint));
                RETAIN_CHECK(step!=CETTA_SEGMENTED_REFUSED && block_allocation_calls-calls<=1u && vec.len==i);
            } while(step==CETTA_SEGMENTED_PENDING);
            RETAIN_CHECK(rho_endpoint_vec_push(&vec,i,(RhoView){0},keys[i],true));
            oracle[i]=(RhoEndpoint){.component_index=i,.key=keys[i],.key_owned=true};
        }
        qsort(oracle,width,sizeof(*oracle),rho_endpoint_cmp_key);
        RhoEndpointSort sort={0};RETAIN_CHECK(rho_endpoint_sort_init(&sort,&vec));
        uint64_t steps=0u;
        while(!sort.done) {
            bool seen[129]={0};
            for(unsigned i=0u;i<width;i++) {
                const RhoEndpoint *row=rho_endpoint_at(&vec,i);
                RETAIN_CHECK(row && row->component_index<width && !seen[row->component_index] &&
                    row->key==keys[row->component_index] && row->key_owned);
                seen[row->component_index]=true;
            }
            size_t calls=block_allocation_calls;retirement_nonnull_frees=0u;key_retirement_tracking=true;
            RETAIN_CHECK(rho_endpoint_sort_advance(&sort) && ++steps<50000u);
            key_retirement_tracking=false;
            RETAIN_CHECK(block_allocation_calls-calls<=1u && retirement_nonnull_frees<=1u);
        }
        RETAIN_CHECK(!sort.sort.scratch.pages.blocks);
        for(unsigned i=0u;i<width;i++)
            RETAIN_CHECK(rho_endpoint_at(&vec,i)->component_index==oracle[i].component_index &&
                rho_endpoint_at(&vec,i)->key==oracle[i].key);
        rho_endpoint_sort_free(&sort);
        retirement_key_count=width;memset(retirement_key_frees,0,sizeof(retirement_key_frees));
        for(unsigned i=0u;i<width;i++) retirement_keys[i]=keys[i];
        key_retirement_tracking=true;
        for(;;) {
            retirement_nonnull_frees=0u;
            bool pending=rho_endpoint_vec_release_one(&vec);
            RETAIN_CHECK(retirement_nonnull_frees<=1u);
            if(!pending) break;
        }
        key_retirement_tracking=false;
        for(unsigned i=0u;i<width;i++) RETAIN_CHECK(retirement_key_frees[i]==1u);
        rho_endpoint_vec_free(&vec);
    }
    retirement_key_count=0u;
    puts("PASS: paged endpoint passes retain every key occurrence, agree with the flat ordinal oracle at eight boundary widths, and retire scratch/keys one action at a time");
    return true;
}

static bool retained_selector_storage_tests(void) {
    /* Five allocation boundaries per first page; cuts include a partially
     * installed path, a partially written pass, a swapped pass and retirement. */
    for(unsigned trial=0u;trial<10u;trial++) {
        retained_current="actual selector storage refusal/cancellation, borrowed source generation and delayed publication";
        Arena source,result;arena_init_detached(&source);arena_init_detached(&result);
        enum {width=129u};
        Atom *leaf=rho_binary(&source,"rho:send",
            rho_unary(&source,"rho:quote",rho_nil(&source)),rho_nil(&source));
        Atom *children[width+1u];children[0]=atom_symbol(&source,"rho:par");
        for(unsigned i=0u;i<width;i++) children[i+1u]=leaf;
        Atom *input=atom_expr(&source,children,width+1u);
        RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,NULL);RETAIN_CHECK(run);
        bool reached=false;
        for(unsigned actions=0u;actions<200000u && !reached;actions++) {
            RhoCanonicalCursor *cursor=&run->cursor;
            if(run->cursor_live) {
                RhoStableSort *sort=&cursor->endpoint_sort.sort;
                if(trial<2u) reached=cursor->phase==RHO_CANONICAL_KEYS &&
                    cursor->preparation_position==0u &&
                    (trial ? cursor->component_keys.pages.blocks!=NULL : !cursor->component_keys.pages.blocks);
                else if(trial<4u) reached=cursor->phase==RHO_CANONICAL_ENDPOINTS &&
                    (trial==2u ? !run->machine.sends.storage.pages.blocks : run->machine.sends.len==65u);
                else if(trial<9u && cursor->phase==RHO_CANONICAL_SORT_SENDS)
                    reached=trial==4u ? sort->range_ready && !sort->scratch.pages.blocks && !sort->output :
                        trial==5u ? sort->scratch.pages.blocks && !sort->output :
                        trial==6u ? sort->width==1u && sort->output>64u && sort->output<width :
                        trial==7u ? sort->width==2u && !sort->output : sort->scratch.pages.retiring;
                else if(trial==9u) reached=run->phase==RHO_RUN_SELECTOR_RETIRE &&
                    cursor->component_keys.pages.retiring;
            }
            if(!reached) RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
        }
        RETAIN_CHECK(reached && run->machine.current && run->machine.components.borrowed &&
            run->machine.components.items==rho_view(run->machine.current).args);
        RhoReductionResult unpublished;RETAIN_CHECK(!rhocalc_run_result(run,&unpublished));
        if(trial==0u || trial==2u || trial==4u) {
            Atom *held=run->machine.current;unsigned position=run->cursor.preparation_position;
            block_fail_at=block_allocation_calls+1u;block_failure_injected=false;
            RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_FAILED && block_failure_injected &&
                run->machine.current==held && run->cursor.preparation_position==position &&
                !rhocalc_run_result(run,&unpublished));
            block_fail_at=0u;
        }
        RhocalcRun identity;memcpy(&identity,run,sizeof(identity));uint64_t used=99u;
        RETAIN_CHECK(rhocalc_run_release_poll(&run,0u,false,&used)==CETTA_OWNED_RELEASE_PENDING &&
            !used && !memcmp(run,&identity,sizeof(identity)));
        CettaOwnedReleaseStep status;
        do {
            retirement_key_count=0u;retirement_nonnull_frees=0u;key_retirement_tracking=true;
            status=rhocalc_run_release_poll(&run,1u,false,&used);
            key_retirement_tracking=false;
            RETAIN_CHECK(run && used<=1u && retirement_nonnull_frees<=1u && status!=CETTA_OWNED_RELEASE_COMPLETE);
        } while(status==CETTA_OWNED_RELEASE_PENDING);
        RETAIN_CHECK(status==CETTA_OWNED_RELEASE_ATOMIC && !run->machine.sends.len &&
            !run->machine.recvs.len && !run->machine.sends.storage.pages.blocks &&
            !run->machine.recvs.storage.pages.blocks && !run->cursor.component_key_count &&
            !run->cursor.component_keys.pages.blocks && !run->cursor.endpoint_sort.sort.scratch.pages.blocks);
        RETAIN_CHECK(rhocalc_run_release_poll(&run,1u,true,NULL)==CETTA_OWNED_RELEASE_COMPLETE && !run);
        arena_free(&result);arena_free(&source);
    }
    puts("PASS: actual selector cancellation at ten retained storage cuts preserves source borrowing, refuses three provider boundaries without publication, and drains indexed backing before opaque cleanup");
    return true;
}

static bool retained_memo_refusal_tests(void) {
    retained_current="grammar/normalization memo refusal retains partial backing until admitted cleanup";
    for(unsigned normalize=0u;normalize<2u;normalize++) {
        Arena source,result;
        arena_init_detached(&source);arena_init_detached(&result);
        enum {width=129u};
        Atom *children[width+1u];children[0]=atom_symbol(&source,"rho:par");
        for(unsigned i=0u;i<width;i++) children[i+1u]=rho_binary(&source,"rho:send",
            rho_unary(&source,"rho:quote",rho_nil(&source)),rho_nil(&source));
        Atom *input=atom_expr(&source,children,width+1u);
        RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,1u,1u,false};
        RhocalcRun *run=rhocalc_run_open_held(&result,input,&profile,NULL);
        RETAIN_CHECK(run);
        CettaNodeMemoCursor *pending=normalize ? &run->normalization.memo_cursor : &run->check.memo_cursor;
        bool reached=false;
        for(unsigned steps=0u;steps<200000u && !reached;steps++) {
            reached=run->phase==(normalize ? RHO_RUN_NORMALIZE : RHO_RUN_VALIDATE) &&
                pending->grow_cap==128u && pending->clear_position==64u;
            if(!reached) RETAIN_CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
        }
        RETAIN_CHECK(reached && pending->writer && pending->grown.blocks &&
            !run->machine.current);
        CettaSegmentedBlock *held=pending->grown.blocks;
        size_t backing=pending->grown.live_bytes;
        pending->byte_limit=pending->writer->slots.live_bytes+backing;
        uint64_t used=0u;
        RETAIN_CHECK(rhocalc_run_poll(run,1u,&used)==RHOCALC_RUN_FAILED && used==1u &&
            pending->status==CETTA_NODE_MEMO_REFUSED && pending->grown.blocks==held &&
            pending->grown.live_bytes==backing && !run->machine.current);
        RhoReductionResult absent={0};
        RETAIN_CHECK(!rhocalc_run_result(run,&absent));
        CettaOwnedReleaseStep status;
        do {
            status=rhocalc_run_release_poll(&run,1u,false,&used);
            RETAIN_CHECK(run && used<=1u && status!=CETTA_OWNED_RELEASE_COMPLETE);
        } while(status==CETTA_OWNED_RELEASE_PENDING);
        RETAIN_CHECK(status==CETTA_OWNED_RELEASE_ATOMIC);
        if(!normalize) RETAIN_CHECK(run->check.memo_cursor.grown.blocks==held);
        RETAIN_CHECK(rhocalc_run_release_poll(&run,1u,true,NULL)==CETTA_OWNED_RELEASE_COMPLETE && !run);
        arena_free(&result);arena_free(&source);
    }
    puts("PASS: real grammar/normalization memo refusal retains partial replacement backing and no published answer until cleanup is admitted");
    return true;
}

static bool retained_release_tests(void) {
    retained_current = "actual normalizer cleanup, atomic tail and retained owner";
    const unsigned widths[] = {8u, 128u, 2048u, 32768u};
    for (unsigned trial = 0u; trial < sizeof(widths)/sizeof(*widths); trial++) {
        Arena source, result;
        arena_init_detached(&source); arena_init_detached(&result);
        Atom *leaf = rho_binary(&source, "rho:send",
            rho_unary(&source, "rho:quote", rho_nil(&source)), rho_nil(&source));
        Atom **children = malloc(((size_t)widths[trial] + 1u)*sizeof(*children));
        RETAIN_CHECK(children);
        children[0] = atom_symbol(&source, "rho:par");
        for (unsigned i = 0u; i < widths[trial]; i++) children[i+1u] = leaf;
        Atom *input = atom_expr(&source, children, widths[trial]+1u);
        free(children);
        RhoRuntimeProfile profile = {RHO_SCHEDULER_CANONICAL, 1u, 1u, false};
        RhocalcRun *run = rhocalc_run_open_held(&result, input, &profile, NULL);
        RETAIN_CHECK(run);
        while (!(run->phase == RHO_RUN_NORMALIZE && run->normalization.frames.length &&
            rho_normalize_frame_at(&run->normalization,0)->stage == 5u))
            RETAIN_CHECK(rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_PENDING);
        RETAIN_CHECK(run->normalization.frames.length == 1u && !run->machine.current);
        size_t blocks=dense_storage_blocks(widths[trial]);
        RETAIN_CHECK(storage_blocks(&rho_normalize_frame_at(&run->normalization,0)->items.pages)==blocks);
        RhocalcRun *identity = run;
        RhocalcRun before;
        memcpy(&before, run, sizeof(before));
        uint64_t used = 37u;
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 0u, false, &used) ==
            CETTA_OWNED_RELEASE_PENDING && !used && run == identity &&
            !memcmp(&before, run, sizeof(before)));
        RhocalcRunAccount paid = rhocalc_run_account(run);
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 1u, false, &used) ==
            CETTA_OWNED_RELEASE_PENDING && used == 1u && run == identity &&
            run->retiring && rho_normalize_frame_at(&run->normalization,0)->length == widths[trial]);
        uint64_t retirement_work = used;
        RhoReductionResult unpublished;
        RETAIN_CHECK(rhocalc_run_poll(run, 1u, &used) == RHOCALC_RUN_FAILED &&
            !used && !rhocalc_run_result(run, &unpublished));
        CettaOwnedReleaseStep status;
        do {
            unsigned grant = trial & 1u ? 7u : 1u;
            status = rhocalc_run_release_poll(&run, grant, false, &used);
            RETAIN_CHECK(run == identity && used <= grant &&
                status != CETTA_OWNED_RELEASE_COMPLETE &&
                same_account(paid, rhocalc_run_account(run)));
            retirement_work += used;
        } while (status == CETTA_OWNED_RELEASE_PENDING);
        RETAIN_CHECK(status == CETTA_OWNED_RELEASE_ATOMIC &&
            retirement_work == (uint64_t)widths[trial] + blocks + 8u &&
            !run->normalization.frames.slots.blocks && run->normalization_retired);
        memcpy(&before, run, sizeof(before));
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 19u, false, &used) ==
            CETTA_OWNED_RELEASE_ATOMIC && !used && run == identity &&
            !memcmp(&before, run, sizeof(before)));
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 1u, true, &used) ==
            CETTA_OWNED_RELEASE_COMPLETE && used == 1u && !run);
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 0u, false, &used) ==
            CETTA_OWNED_RELEASE_COMPLETE && !used &&
            rhocalc_process_well_formed(input));

        run = rhocalc_run_open_held(&result, input, &profile, NULL);
        RETAIN_CHECK(run);
        while (!(run->phase == RHO_RUN_NORMALIZE && run->normalization.frames.length &&
            rho_normalize_frame_at(&run->normalization,0)->stage == 5u))
            RETAIN_CHECK(rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_PENDING);
        unsigned releases = 0u, producer_calls = 0u;
        RetainedRhoOwner *shell = malloc(sizeof(*shell));
        RETAIN_CHECK(shell);
        *shell = (RetainedRhoOwner){run, &releases, &producer_calls};
        CettaOwnedExecution execution;
        cetta_owned_execution_init(&execution, shell, retained_rho_owner_next,
            retained_rho_owner_release);
        RETAIN_CHECK(cetta_owned_execution_set_release_poll(&execution,
            retained_rho_owner_release_poll));
        uint64_t coupled_work = 0u;
        do {
            status = cetta_owned_execution_cancel_poll(&execution, 1u, false, &used);
            RETAIN_CHECK(used <= 1u && execution.computation == shell &&
                status != CETTA_OWNED_RELEASE_COMPLETE && !producer_calls && !releases);
            coupled_work += used;
        } while (status == CETTA_OWNED_RELEASE_PENDING);
        RETAIN_CHECK(status == CETTA_OWNED_RELEASE_ATOMIC &&
            coupled_work == (uint64_t)widths[trial] + blocks + 9u && shell->run == run);
        RETAIN_CHECK(cetta_owned_execution_cancel_poll(&execution, 1u, true, &used) ==
            CETTA_OWNED_RELEASE_PENDING && used == 1u && !shell->run &&
            execution.release_ready && execution.computation == shell && !releases);
        coupled_work += used;
        RETAIN_CHECK(cetta_owned_execution_cancel_poll(&execution, 1u, false, &used) ==
            CETTA_OWNED_RELEASE_COMPLETE && used == 1u && !execution.computation && releases == 1u);
        coupled_work += used;
        RETAIN_CHECK(coupled_work == (uint64_t)widths[trial] + blocks + 11u && !producer_calls);
        cetta_owned_execution_cancel(&execution);
        RETAIN_CHECK(releases == 1u && rhocalc_process_well_formed(input));
        arena_free(&result); arena_free(&source);
    }
    const unsigned depths[] = {16u, 1024u};
    for (unsigned trial = 0u; trial < sizeof(depths)/sizeof(*depths); trial++) {
        Arena source, result;
        arena_init_detached(&source); arena_init_detached(&result);
        Atom *nil = rho_nil(&source), *name = rho_unary(&source, "rho:quote", nil);
        Atom *input = nil;
        for (unsigned i = 0u; i < depths[trial]; i++)
            input = rho_binary(&source, "rho:send", name, input);
        RhoRuntimeProfile profile = {RHO_SCHEDULER_CANONICAL, 1u, 1u, false};
        RhocalcRun *run = rhocalc_run_open_held(&result, input, &profile, NULL);
        RETAIN_CHECK(run);
        while (!(run->phase == RHO_RUN_NORMALIZE && run->normalization.frames.length >= depths[trial]))
            RETAIN_CHECK(rhocalc_run_poll(run, 1u, NULL) == RHOCALC_RUN_PENDING);
        uint64_t frames=run->normalization.frames.length,work=0u,used=0u;
        size_t pointer_blocks=dense_storage_blocks((size_t)frames-run->normalization.frames.pending);
        RETAIN_CHECK(storage_blocks(&run->normalization.frames.slots)==pointer_blocks);
        CettaOwnedReleaseStep status;
        do {
            status = rhocalc_run_release_poll(&run, 1u, false, &used);
            RETAIN_CHECK(run && used <= 1u && status != CETTA_OWNED_RELEASE_COMPLETE);
            work += used;
        } while (status == CETTA_OWNED_RELEASE_PENDING);
        RETAIN_CHECK(status == CETTA_OWNED_RELEASE_ATOMIC && work == frames + pointer_blocks + 2u);
        RETAIN_CHECK(rhocalc_run_release_poll(&run, 1u, true, &used) ==
            CETTA_OWNED_RELEASE_COMPLETE && !run && used == 1u);
        Atom *current = input;
        for (unsigned i = 0u; i < depths[trial]; i++) {
            RhoView view = rho_view(current);
            RETAIN_CHECK(view.kind == RHO_SEND && view.args[0] == name);
            current = view.args[1];
        }
        RETAIN_CHECK(current == nil);
        arena_free(&result); arena_free(&source);
    }
    printf("PASS: retained normalizer retirement preserves owner, paid prefix and atomic tail at four widths and two depths\n");
    printf("PASS: actual coupled owner/Rho cleanup invoice is width+row-backing-blocks+11, with separate begin, acknowledgement and wrapper release\n");
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

#if CETTA_RHO_WORK_CENSUS
    if (getenv("CETTA_RHO_WORK_CENSUS")) {
        int status = retained_work_census();
        arena_free(&arena); symbol_table_free(&symbols); g_symbols = NULL;
        return status;
    }
#endif

    bool abt_ok = rhocalc_abt_substitution_correspondence_selftest(&arena);
    bool text_key_ok =
        rhocalc_atom_text_key_correspondence_selftest(&arena);
    bool retained_ok=payload_resource_tracking_refusal_test() &&
        payload_space_cleanup_owner_test(false,true) &&
        payload_space_cleanup_owner_test(false,false) &&
        payload_space_cleanup_owner_test(true,false) && payload_resource_view_tests() && payload_resource_cycle_tests() &&
        retained_payload_substitution_tests() && retained_semantic_substitution_tests() &&
        key_failure_tests() && retained_node_memo_tests() &&
        retained_reaction_preparation_tests() && retained_fixed_selector_tests() &&
        retained_reaction_build_tests() && retained_reaction_tie_tests() && retained_closed_reaction_tests() &&
        retained_generation_tests() && retained_run_tests() &&
        retained_preparation_tests() && retained_shared_preparation_tests() && retained_value_observation_tests() &&
        segmented_storage_tests() && retained_record_stack_tests() && retained_frame_provider_refusal_tests() && retained_rejected_payload_retirement_test() && retained_nested_key_retirement_test() && retained_frame_pop_admission_tests() && retained_endpoint_storage_tests() && retained_selector_storage_tests() && retained_memo_refusal_tests() && retained_sort_retirement_tests() && retained_release_tests();
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
