/* Exercise actual native-control refusal with its complete retained provider.
 * This translation unit replaces eval.o and table_store.o in the control executable. */
#define BR_HAS_RETURN_HELPER 1
#define _GNU_SOURCE
#include "../src/grounded.h"
#include "../src/eval.h"
#include "../src/prime_semantics.h"
#include "../src/parallel_executor.h"
#include "../src/table_store.h"
#include "../src/variant_shape.h"
#include "../src/rhocalc_core.h"
#include "../src/library.h"
#include <stdlib.h>
static unsigned br_table_variant_refusals, br_table_collect_refusals;
static unsigned br_table_key_refusals, br_table_goal_refusals;
static Atom *br_table_variant(Arena *, Atom *, const VariantInstance *);
static bool br_table_collect(QueryResults *, Atom *, Bindings *);
static Atom *br_table_key(Arena *, Atom *, CettaVarMap *, CettaVarMap *,
    const CettaVariantShapeOptions *);
static bool br_table_goal(Arena *, CettaVarMap *, const CettaVarMap *);
/* Compile the actual table service in this control unit, so refusal tests
 * exercise its walkers without adding production fault-injection state. */
#define variant_instance_materialize br_table_variant
#define query_results_push br_table_collect
#define variant_shape_canonicalize_atom br_table_key
#define cetta_var_map_clone_live br_table_goal
#include "../src/table_store.c"
#undef variant_instance_materialize
#undef query_results_push
#undef variant_shape_canonicalize_atom
#undef cetta_var_map_clone_live
static Atom *br_table_variant(Arena *arena, Atom *value, const VariantInstance *variant) {
    if (br_table_variant_refusals) {br_table_variant_refusals--;return NULL;}
    return variant_instance_materialize(arena,value,variant);
}
static bool br_table_collect(QueryResults *results, Atom *value, Bindings *environment) {
    if (br_table_collect_refusals) {br_table_collect_refusals--;return false;}
    return query_results_push(results,value,environment);
}
static Atom *br_table_key(Arena *arena, Atom *value, CettaVarMap *source,
    CettaVarMap *goal, const CettaVariantShapeOptions *options) {
    if (br_table_key_refusals) {br_table_key_refusals--;return NULL;}
    return variant_shape_canonicalize_atom(arena,value,source,goal,options);
}
static bool br_table_goal(Arena *arena, CettaVarMap *goal, const CettaVarMap *source) {
    if (br_table_goal_refusals) {br_table_goal_refusals--;return false;}
    return cetta_var_map_clone_live(arena,goal,source);
}
#define CETTA_PRIME_ORDINARY_RETURN_TEST 1
static unsigned br_ordinary_slot_refusals;
static uint64_t br_ordinary_slot_attempts;
static bool prime_ordinary_test_slot(ResultSet *results) {
    (void)results;
    br_ordinary_slot_attempts++;
    if (!br_ordinary_slot_refusals) return true;
    br_ordinary_slot_refusals--;
    return false;
}
static bool br_native_call(Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
static bool br_library_tracking;
static unsigned br_library_prepared_calls, br_library_calls;
static bool br_library_prepared_call(CettaLibraryContext *, Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
static bool br_library_call(CettaLibraryContext *, Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
static Atom *br_env_apply(Arena *, const Bindings *, Atom *);
/* Fault only this actual retained prefix allocation, not another worker's
 * allocator. The production translation unit has no injection hook. */
static __thread void *br_return_realloc_owner;
static __thread unsigned br_return_realloc_refusals;
static __thread Arena *br_return_copy_owner;
static __thread int64_t br_return_copy_value;
static __thread unsigned br_return_copy_refusals;
static __thread Arena *br_return_holder_owner;
static __thread unsigned br_return_holder_refusals;
static __thread bool br_return_worker_tracking;
static __thread unsigned br_return_worker_waves, br_return_worker_tasks;
static __thread unsigned br_service_effects_refusals;
static __thread Arena *br_weight_promote_owner;
static __thread unsigned br_weight_promote_refusals;
static __thread AtomDeepCopySession *br_weight_copy_session;
static __thread AtomDeepCopySession **br_weight_copy_slot;
static __thread unsigned br_weight_copy_refusals;
static __thread unsigned br_weight_copy_skip;
static __thread const Bindings *br_weight_clone_source;
static __thread unsigned br_weight_clone_refusals;
static unsigned br_preparation_merge_skip, br_preparation_merge_refusals;
static unsigned br_preparation_materialize_refusals;
static CettaMatchCompletion br_preparation_merge(Bindings *, const Bindings *);
static bool br_preparation_materialize(const AnswerBank *, AnswerRef, Arena *,
    const CettaVarMap *, Atom **, Bindings *, VariantInstance *);
static Atom *br_return_copy(Arena *, Atom *);
static Atom *br_return_holder(struct CettaLibraryContext *, Arena *, const char *, uint64_t);
static bool br_return_worker_run(CettaParallelExecutor *);
static SpaceExecutionAnalysisCache *br_service_effects_new(void);
static bool br_weight_promote(Bindings *, Arena *);
static Atom *br_weight_copy(AtomDeepCopySession *, Atom *);
static bool br_weight_clone(Bindings *, const Bindings *);
static void *br_return_realloc(void *pointer, size_t bytes) {
    if (br_return_realloc_refusals && pointer == br_return_realloc_owner) {
        br_return_realloc_refusals--;
        return NULL;
    }
    return realloc(pointer, bytes);
}
#define grounded_call br_native_call
#define prime_semantics_env_apply br_env_apply
#define realloc br_return_realloc
#define atom_deep_copy br_return_copy
#define cetta_native_handle_owned_atom br_return_holder
#define cetta_parallel_executor_run br_return_worker_run
#define space_execution_analysis_cache_new br_service_effects_new
#define bindings_promote_atoms_to_arena br_weight_promote
#define atom_deep_copy_session_copy br_weight_copy
#define bindings_clone br_weight_clone
#define bindings_try_merge_live_checked br_preparation_merge
#define table_store_materialize_answer_ref br_preparation_materialize
#define cetta_library_call_native_prepared_view br_library_prepared_call
#define cetta_library_call_native br_library_call
#include "../src/eval.c"
#undef cetta_library_call_native_prepared_view
#undef cetta_library_call_native
#undef grounded_call
#undef prime_semantics_env_apply
#undef realloc
#undef atom_deep_copy
#undef cetta_native_handle_owned_atom
#undef cetta_parallel_executor_run
#undef space_execution_analysis_cache_new
#undef bindings_promote_atoms_to_arena
#undef atom_deep_copy_session_copy
#undef bindings_clone
#undef bindings_try_merge_live_checked
#undef table_store_materialize_answer_ref
static bool br_library_prepared_call(CettaLibraryContext *context, Space *space,
        Arena *arena, Atom *head, Atom **arguments, uint32_t arity,
        CettaCallOutcome *outcome) {
    if (br_library_tracking) br_library_prepared_calls++;
    return cetta_library_call_native_prepared_view(
        context, space, arena, head, arguments, arity, outcome);
}
static bool br_library_call(CettaLibraryContext *context, Space *space,
        Arena *arena, Atom *head, Atom **arguments, uint32_t arity,
        CettaCallOutcome *outcome) {
    if (br_library_tracking) br_library_calls++;
    return cetta_library_call_native(context, space, arena, head, arguments, arity, outcome);
}
static CettaMatchCompletion br_preparation_merge(Bindings *to, const Bindings *from) {
    if (br_preparation_merge_refusals) {
        if (br_preparation_merge_skip) br_preparation_merge_skip--;
        else {br_preparation_merge_refusals--;return CETTA_MATCH_INCOMPLETE;}
    }
    return bindings_try_merge_live_checked(to,from);
}
static bool br_preparation_materialize(const AnswerBank *bank, AnswerRef ref, Arena *arena,
    const CettaVarMap *map, Atom **value, Bindings *env, VariantInstance *variant) {
    if (br_preparation_materialize_refusals) {
        br_preparation_materialize_refusals--;
        *value=NULL;bindings_init(env);variant_instance_init(variant);return false;
    }
    return table_store_materialize_answer_ref(bank,ref,arena,map,value,env,variant);
}
static bool br_weight_clone(Bindings *to, const Bindings *from) {
    if (from==br_weight_clone_source && br_weight_clone_refusals) {
        br_weight_clone_refusals--;
        return false;
    }
    return bindings_clone(to,from);
}
static bool br_weight_promote(Bindings *bindings, Arena *arena) {
    if (arena==br_weight_promote_owner && br_weight_promote_refusals) {
        br_weight_promote_refusals--;
        return false;
    }
    return bindings_promote_atoms_to_arena(bindings,arena);
}
static Atom *br_weight_copy(AtomDeepCopySession *session, Atom *value) {
    if ((session==br_weight_copy_session ||
        (br_weight_copy_slot && session==*br_weight_copy_slot)) && br_weight_copy_refusals) {
        if (br_weight_copy_skip) br_weight_copy_skip--;
        else {
            br_weight_copy_refusals--;
            return NULL;
        }
    }
    return atom_deep_copy_session_copy(session,value);
}
static bool br_return_worker_run(CettaParallelExecutor *executor) {
    if (br_return_worker_tracking && executor->thread_count>1u) {
        br_return_worker_waves++;
        br_return_worker_tasks+=executor->queue.queued;
    }
    return cetta_parallel_executor_run(executor);
}
static SpaceExecutionAnalysisCache *br_service_effects_new(void) {
    if (br_service_effects_refusals) { br_service_effects_refusals--; return NULL; }
    return space_execution_analysis_cache_new();
}
static Atom *br_return_holder(struct CettaLibraryContext *context, Arena *arena,
    const char *kind, uint64_t handle) {
    if (br_return_holder_refusals && arena==br_return_holder_owner &&
        strcmp(kind,"prime-search")==0) {
        br_return_holder_refusals--;
        return NULL;
    }
    return cetta_native_handle_owned_atom(context,arena,kind,handle);
}
static Atom *br_return_copy(Arena *arena, Atom *value) {
    if (br_return_copy_refusals && arena == br_return_copy_owner && value &&
        value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
        value->ground.ival == br_return_copy_value) {
        br_return_copy_refusals--;
        return NULL;
    }
    return atom_deep_copy(arena, value);
}
static __thread unsigned br_env_refusals, br_env_calls, br_env_fail_at;
static __thread bool br_env_fault_seen;
static __thread unsigned br_calls_after_env_fault;
static Atom *br_env_apply(Arena *arena, const Bindings *environment, Atom *term) {
    br_env_calls++;
    bool refused=br_env_refusals!=0u;
    if (br_env_refusals) br_env_refusals--;
    if (br_env_fail_at && !--br_env_fail_at) refused=true;
    if (refused) {
        br_env_fault_seen=true;
        return NULL;
    }
    return prime_semantics_env_apply(arena, environment, term);
}
static bool br_suspend_native;
static unsigned br_native_calls;
static bool br_native_call(Arena *arena, Atom *head, Atom **args,
                           uint32_t arity, CettaCallOutcome *out) {
    if (br_env_fault_seen) br_calls_after_env_fault++;
    if (br_suspend_native && atom_is_symbol_id(head, g_builtin_syms.op_plus)) {
        br_native_calls++;
        *out=cetta_call_suspended(atom_int(arena,7),
            atom_expr2(arena,atom_symbol(arena,"Pending"),atom_int(arena,19)));
        return true;
    }
    return grounded_call(arena,head,args,arity,out);
}
#include "parser.h"
#include <assert.h>
#include "parallel_executor.h"
#include "shared_return_producer.h"
#include "owned_return_image.h"

static _Atomic size_t checks, failures;
#define CHECK(X) do { checks++; if (!(X)) { failures++; fprintf(stderr,"FAIL: %s:%d: %s\n",__FILE__,__LINE__,#X); } } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **items=NULL;
    int count=parse_metta_text(text,arena,&items);
    assert(count==1);
    Atom *value=items[0];free(items);return value;
}

static void scoped_environment_refusal_cases(void) {
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena;arena_init(&arena);
    Bindings environment;bindings_init(&environment);
    Atom *variable=atom_var(&arena,"scoped-refusal");
    CHECK(bindings_add_var(&environment,variable,atom_int(&arena,7)));
    Atom *quoted=atom_expr2(&arena,atom_symbol(&arena,"quote"),variable);
    Atom *term=atom_expr3(&arena,atom_symbol(&arena,"Pair"),variable,quoted);
    Atom *scoped=prime_semantics_env_apply(&arena,&environment,term);
    Atom *whole=bindings_apply_if_vars(&environment,&arena,term);
    CHECK(scoped && whole && !atom_eq(scoped,whole));
    CHECK(scoped && scoped->expr.elems[2]==quoted);
    CHECK(atom_eq(eval_env_apply(&environment,&arena,term),scoped));
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
    EvalCompletionTracker *previous=g_eval_completion_tracker;
    g_eval_completion_tracker=&tracker;
    br_env_refusals=1u;
    CHECK(eval_env_apply(&environment,&arena,term)==NULL);
    CHECK(!br_env_refusals && tracker.interrupted &&
          tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
    OutcomeSet rows;outcome_set_init(&rows);
    br_env_refusals=1u;
    outcome_set_add_prefixed(&arena,&rows,term,NULL,&environment,false);
    CHECK(!rows.len && !br_env_refusals && tracker.interrupted);
    CHECK(tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    outcome_set_free(&rows);
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
    Outcome row;outcome_init(&row);row.atom=term;
    CHECK(bindings_clone(&row.env,&environment));
    br_env_refusals=1u;
    CHECK(!outcome_atom_materialize(&arena,&row));
    CHECK(row.atom==term && !row.materialized_atom &&
        bindings_lookup_value_id(&row.env,variable->var_id).skeleton->ground.ival==7);
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
    CHECK(atom_eq(outcome_atom_materialize(&arena,&row),scoped));
    CHECK(tracker.completion==CETTA_EVAL_COMPLETE && !tracker.interrupted);
    outcome_free_fields(&row);
    Space space;space_init(&space);
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
    Atom *call=atom_expr3(&arena,atom_symbol(&arena,"+"),variable,atom_int(&arena,1));
    outcome_set_init(&rows);br_env_refusals=1u;br_native_calls=0u;
    br_suspend_native=true;
    eval_with_prefix_bindings(&space,&arena,NULL,call,-1,&environment,&rows);
    br_suspend_native=false;
    CHECK(!rows.len && !br_env_refusals && !br_native_calls && tracker.interrupted);
    outcome_set_free(&rows);
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
    outcome_set_init(&rows);br_env_refusals=1u;
    Atom *prefix[3]={term->expr.elems[0]};
    interpret_tuple(&space,&arena,term->expr.elems,3u,1u,prefix,&environment,
        NULL,-1,&rows,NULL,0u,NULL,NULL);
    CHECK(!rows.len && !br_env_refusals && tracker.interrupted);
    outcome_set_free(&rows);space_free(&space);
    for(unsigned host=0u;host<2u;host++) {
        cetta_library_context_free(&context);
        cetta_library_context_init_for_language_profile(&context,
            host?CETTA_LANGUAGE_HE:CETTA_LANGUAGE_PETTA,
            host?cetta_profile_he_extended():cetta_profile_petta_extended());
        eval_set_library_context(&context);
        tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
        unsigned calls=br_env_calls;br_env_refusals=1u;
        CHECK(atom_eq(eval_env_apply(&environment,&arena,term),whole));
        CHECK(br_env_refusals==1u && br_env_calls==calls && !tracker.interrupted);
        br_env_refusals=0u;
    }
    g_eval_completion_tracker=previous;
    bindings_free(&environment);arena_free(&arena);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
    br_env_fault_seen=false;br_calls_after_env_fault=0u;
}

static bool refused_row_visit(Arena *,Atom *,const Outcome *,void *);
static void answer_reference_outer_binding_cases(void) {
    Arena arena;arena_init(&arena);
    for(unsigned variable_result=0u;variable_result<2u;variable_result++) {
        AnswerBank bank;answer_bank_init(&bank);AnswerRef reference;
        Atom *canonical=atom_var_with_id(&arena,"answer-canonical",var_id_from_base(501u));
        Atom *caller=atom_var(&arena,"answer-caller");
        Atom *seven=atom_int(&arena,7);
        Atom *result=variable_result?canonical:atom_symbol(&arena,"Done");
        CHECK(answer_bank_add(&bank,result,NULL,NULL,&reference));
        CettaVarMap map;cetta_var_map_init(&map);
        CHECK(cetta_var_map_add(&map,var_id_from_base(501u),caller));
        Bindings environment;bindings_init(&environment);
        CHECK(bindings_add_var(&environment,caller,seven));
        Outcome row;
        CHECK(outcome_init_answer_ref(&arena,&row,&bank,reference,&map,&environment));
        CHECK(bindings_lookup_value_id(&row.env,caller->var_id).skeleton==seven);
        Atom *actual=outcome_atom_materialize(&arena,&row);
        CHECK(actual && atom_eq(actual,variable_result?seven:result));
        CHECK(bindings_lookup_value_id(&row.env,caller->var_id).skeleton==seven);
        Atom *continuation=atom_expr3(&arena,atom_symbol(&arena,"Pair"),
            atom_symbol(&arena,"Done"),caller);
        Atom *continued=eval_env_apply(&row.env,&arena,continuation);
        CHECK(continued && continued->expr.elems[2]->kind==ATOM_GROUNDED &&
            continued->expr.elems[2]->ground.ival==7);
        outcome_free_fields(&row);bindings_free(&environment);
        cetta_var_map_free(&map);answer_bank_free(&bank);
    }
    {
        /* A cached answer incompatible with its caller remains a rejected
         * branch, with the original reference and environment still owned. */
        Atom *canonical=atom_var_with_id(&arena,"conflicting-canonical",var_id_from_base(501u));
        Atom *caller=atom_var(&arena,"conflicting-caller");
        Atom *seven=atom_int(&arena,7);
        Bindings bank_env,caller_env;bindings_init(&bank_env);bindings_init(&caller_env);
        CHECK(bindings_add_var(&bank_env,canonical,atom_int(&arena,8)));
        CHECK(bindings_add_var(&caller_env,caller,seven));
        AnswerBank bank;answer_bank_init(&bank);AnswerRef reference;
        CHECK(answer_bank_add(&bank,canonical,&bank_env,NULL,&reference));
        CettaVarMap map;cetta_var_map_init(&map);
        CHECK(cetta_var_map_add(&map,var_id_from_base(501u),caller));
        Outcome row;CHECK(outcome_init_answer_ref(&arena,&row,&bank,reference,&map,&caller_env));
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        EvalCompletionTracker *previous=g_eval_completion_tracker;g_eval_completion_tracker=&tracker;
        CHECK(!outcome_atom_materialize(&arena,&row));
        CHECK(!tracker.interrupted && tracker.completion==CETTA_EVAL_COMPLETE);
        CHECK(row.kind==CETTA_OUTCOME_ANSWER_REF && row.answer_ref.bank==&bank &&
            row.answer_ref.ref==reference && !row.materialized_atom &&
            bindings_lookup_value_id(&row.env,caller->var_id).skeleton==seven);
        OutcomeSet rows;outcome_set_init(&rows);
        outcome_set_add_existing(&rows,&row);
        outcome_set_add(&rows,atom_int(&arena,19),&caller_env);
        unsigned visits=0u;
        /* The rejecting materializer itself does not abort sibling search. */
        CHECK(outcome_set_visit_ordered(&arena,&rows,(CettaSearchPolicyOrder)0,
            refused_row_visit,&visits)==1u && visits==1u);
        CHECK(!tracker.interrupted && tracker.completion==CETTA_EVAL_COMPLETE);
        outcome_set_free(&rows);g_eval_completion_tracker=previous;
        outcome_free_fields(&row);answer_bank_free(&bank);cetta_var_map_free(&map);
        bindings_free(&caller_env);bindings_free(&bank_env);
    }
    arena_free(&arena);
}

typedef struct {
    Atom *items[8];
    unsigned count, stop;
} CheckedTableVisit;
static bool checked_table_visit(Atom *value,const Bindings *env,void *raw) {
    CheckedTableVisit *visit=raw;
    CHECK(value && env && visit->count<8u);
    if(visit->count<8u) visit->items[visit->count++]=value;
    return !visit->stop || visit->count<visit->stop;
}
static bool checked_table_delayed(Atom *value,const Bindings *env,
    const VariantInstance *variant,void *raw) {
    CHECK(variant);return checked_table_visit(value,env,raw);
}
static bool checked_table_ref(const AnswerBank *bank,AnswerRef ref,
    const CettaVarMap *map,void *raw) {
    CheckedTableVisit *visit=raw;
    CHECK(answer_bank_get(bank,ref) && map);
    visit->count++;
    return !visit->stop || visit->count<visit->stop;
}
static void checked_table_preparation_cases(void) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    AnswerBank bank;answer_bank_init(&bank);
    TableStore table;table_store_init(&table,CETTA_TABLE_MODE_VARIANT,&bank);
    Atom *query=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"checked-table-source")},1u);
    Atom *a=atom_symbol(&arena,"CheckedTableA"),*b=atom_symbol(&arena,"CheckedTableB");
    Atom *variable=atom_var(&arena,"checked-table-variable");
    Atom *pair=atom_expr3(&arena,atom_symbol(&arena,"CheckedPair"),variable,variable);
    Atom *values[]={a,pair,b,b};AnswerRef refs[4];
    TableQueryHandle handle={0};
    CHECK(table_store_begin_query(&table,&space,space_revision(&space),query,&handle));
    for(unsigned i=0u;i<4u;i++)
        CHECK(table_store_add_answer(&handle,values[i],NULL,&refs[i]));
    CHECK(table_store_commit_query(&handle));
    /* A canonical free variable can need no per-instance carrier. Exercise
     * the other legal representation with an owned, populated slot instead. */
    AnswerRecord *factored=&bank.items[refs[1]];
    VariantShape shape;variant_shape_init(&shape);
    shape.skeleton=factored->result;
    CHECK(bindings_add_var(&shape.slot_env,
        factored->result->expr.elems[1],variable));
    CHECK(variant_instance_from_shape(&factored->variant,&shape));
    CHECK(variant_instance_promote_atoms_to_arena(&bank.arena,&factored->variant));
    variant_shape_free(&shape);
    CHECK(variant_instance_present(&bank.items[refs[1]].variant));
    CettaEvalCompletion completion=CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
    CettaCount visited=999u;CheckedTableVisit got={0};
    Atom *missing=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"checked-table-missing")},1u);
    CHECK(!table_store_lookup_visit_checked(&table,&space,space_revision(&space),missing,
        &arena,checked_table_visit,&got,&visited,&completion));
    CHECK(completion==CETTA_EVAL_COMPLETE && !visited && !got.count);
    CHECK(table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
        &arena,checked_table_visit,&got,&visited,&completion));
    CHECK(completion==CETTA_EVAL_COMPLETE && visited==4u && got.count==4u &&
        atom_eq(got.items[0],a) && atom_eq(got.items[2],b) && atom_eq(got.items[3],b));
    got=(CheckedTableVisit){.stop=1u};
    CHECK(table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
        &arena,checked_table_visit,&got,&visited,&completion));
    CHECK(completion==CETTA_EVAL_COMPLETE && visited==1u && got.count==1u);
    for(unsigned delayed=0u;delayed<2u;delayed++) {
        Atom *saved=bank.items[refs[1]].result;bank.items[refs[1]].result=NULL;
        got=(CheckedTableVisit){0};
        bool hit=delayed?table_store_lookup_visit_delayed_checked(&table,&space,
            space_revision(&space),query,&arena,checked_table_delayed,&got,&visited,&completion):
            table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
                &arena,checked_table_visit,&got,&visited,&completion);
        CHECK(hit && completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
            visited==1u && got.count==1u && bank.len==4u);
        bank.items[refs[1]].result=saved;
        got=(CheckedTableVisit){0};
        CHECK(table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
            &arena,checked_table_visit,&got,&visited,&completion));
        CHECK(completion==CETTA_EVAL_COMPLETE && got.count==4u && visited==4u);
    }
    got=(CheckedTableVisit){0};br_table_variant_refusals=1u;
    CHECK(table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
        &arena,checked_table_visit,&got,&visited,&completion));
    CHECK(!br_table_variant_refusals && completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
        visited==1u && got.count==1u);
    QueryResults collected;query_results_init(&collected);br_table_collect_refusals=1u;
    CHECK(table_store_lookup_checked(&table,&space,space_revision(&space),query,
        &arena,&collected,&completion));
    CHECK(!br_table_collect_refusals && completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
        !collected.len);
    query_results_free(&collected);
    for(unsigned fault=0u;fault<2u;fault++) {
        if(fault)br_table_goal_refusals=1u;else br_table_key_refusals=1u;
        got=(CheckedTableVisit){0};visited=999u;
        CHECK(!table_store_lookup_visit_ref_checked(&table,&space,space_revision(&space),query,
            &arena,checked_table_ref,&got,&visited,&completion));
        CHECK(completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && !visited && !got.count &&
            !br_table_goal_refusals && !br_table_key_refusals);
    }
    got=(CheckedTableVisit){.stop=1u};
    CHECK(table_store_lookup_visit_ref_checked(&table,&space,space_revision(&space),query,
        &arena,checked_table_ref,&got,&visited,&completion));
    CHECK(completion==CETTA_EVAL_COMPLETE && visited==1u && got.count==1u);
    CHECK(!table_store_lookup_visit_checked(&table,&space,space_revision(&space),query,
        &arena,NULL,NULL,&visited,&completion) &&
        completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && !visited);
    CHECK(!table_store_lookup_checked(&table,&space,space_revision(&space),query,
        &arena,NULL,&completion) && completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    table_store_free(&table);answer_bank_free(&bank);space_free(&space);arena_free(&arena);
}

static bool refused_row_visit(Arena *arena,Atom *value,const Outcome *row,void *raw) {
    (void)arena;CHECK(value && row);(*(unsigned *)raw)++;return true;
}

static void checked_query_preparation_cases(void) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    CettaLibraryContext *library=malloc(sizeof(*library));assert(library);
    cetta_library_context_init_for_language_profile(library,CETTA_LANGUAGE_HE,
        cetta_profile_he_extended());
    CettaLibraryContext *old_library=g_library_context;
    eval_set_library_context(library);
    space_add(&space,parse_one(&arena,"(= (checked-miss CheckedA) (CheckedValue 7))"));
    Atom *marker=atom_var(&arena,"checked-miss-marker");
    Atom *query=atom_expr2(&arena,atom_symbol(&arena,"checked-miss"),marker);
    Bindings base;bindings_init(&base);
    CHECK(bindings_add_var(&base,marker,atom_symbol(&arena,"CheckedA")));
    EvalCompletionTracker *old_tracker=g_eval_completion_tracker;
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
    g_eval_completion_tracker=&tracker;
    {
        QueryMissSingleTailStreamCtx stream;
        query_miss_single_tail_stream_ctx_init(&stream,NULL,NULL,&base,&arena,
            NULL,true,NULL,NULL,NULL,&arena);
        br_weight_clone_source=&base;br_weight_clone_refusals=1u;
        CHECK(!query_miss_single_tail_store_first(&stream,query,&base,CETTA_ANSWER_REF_NONE));
        CHECK(!br_weight_clone_refusals && !stream.first_ready && !stream.count &&
            tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        br_weight_clone_source=NULL;
        query_miss_single_tail_stream_ctx_free(&stream);
        tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
        query_miss_single_tail_stream_ctx_init(&stream,NULL,NULL,&base,&arena,
            NULL,true,NULL,NULL,NULL,&arena);
        CHECK(query_miss_single_tail_store_first(&stream,query,&base,CETTA_ANSWER_REF_NONE));
        CHECK(stream.count==1u && stream.first_ready && !tracker.interrupted);
        query_miss_single_tail_stream_ctx_free(&stream);
    }
    for(unsigned fault=0u;fault<2u;fault++) {
        for(unsigned retry=0u;retry<(fault?2u:1u);retry++) {
            SearchContext context;CHECK(search_context_init(&context,&base,NULL));
            OutcomeSet rows;outcome_set_init(&rows);
            QueryEvalVisitorCtx visitor={.space=&space,.arena=&arena,.invocation=query,
                .fuel=-1,.base_env=&base,.preserve_bindings=true,.context=&context,
                .outcomes=&rows,.he_call=query};
            tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
            const bool refuse=fault && !retry;
            br_preparation_merge_refusals=refuse?1u:0u;
            Atom *next=NULL,*type=NULL;Bindings tail;bindings_init(&tail);
            bool replaces=false;
            QueryTableTailState state=query_equations_miss_single_tail_stream(
                &space,query,NULL,&arena,&visitor,false,&next,&type,&tail,&replaces);
            CHECK(!br_preparation_merge_refusals);
            if(refuse) {
                CHECK(state==QUERY_TABLE_TAIL_INCOMPLETE && tracker.interrupted &&
                    tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
                    !rows.len && !next && !replaces);
            } else {
                CHECK(state==QUERY_TABLE_TAIL_MULTI && !tracker.interrupted && rows.len==1u);
                if(rows.len==1u)CHECK(atom_eq(outcome_atom_materialize(&arena,&rows.items[0]),
                    parse_one(&arena,"(CheckedValue 7)")));
            }
            bindings_free(&tail);outcome_set_free(&rows);search_context_free(&context);
        }
    }
    {
        /* A genuine conflicting answer leaves the caller free to visit its
         * siblings; it is not an interrupted query episode. */
        Bindings conflicting;bindings_init(&conflicting);
        CHECK(bindings_add_var(&conflicting,marker,atom_symbol(&arena,"CheckedB")));
        SearchContext context;CHECK(search_context_init(&context,&base,NULL));
        OutcomeSet rows;outcome_set_init(&rows);
        QueryEvalVisitorCtx visitor={.space=&space,.arena=&arena,.invocation=query,
            .fuel=-1,.base_env=&base,.preserve_bindings=true,.context=&context,
            .outcomes=&rows};
        tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
        CHECK(query_visit_eval_for_caller_common(query,&conflicting,NULL,&visitor));
        CHECK(!tracker.interrupted && !rows.len &&
            bindings_lookup_value_id(&base,marker->var_id).skeleton->kind==ATOM_SYMBOL);
        outcome_set_free(&rows);search_context_free(&context);bindings_free(&conflicting);
    }
    for(unsigned kind=0u;kind<3u;kind++) {
        for(unsigned allow_tail=0u;allow_tail<2u;allow_tail++) {
            Bindings caller;bindings_init(&caller);
            Atom *alias=atom_var(&arena,"checked-miss-alias");
            CHECK(bindings_add_var(&caller,marker,atom_symbol(&arena,
                kind==1u?"CheckedB":"CheckedA")));
            if(kind==2u) {
                CHECK(bindings_add_var(&caller,alias,atom_symbol(&arena,"CheckedB")));
                CHECK(bindings_rewrite_value_id(&caller,marker->var_id,binding_value_from_atom(alias)) &&
                    bindings_rewrite_value_id(&caller,alias->var_id,binding_value_from_atom(marker)));
            }
            SearchContext context;CHECK(search_context_init(&context,&caller,NULL));
            OutcomeSet rows;outcome_set_init(&rows);
            QueryEvalVisitorCtx visitor={.space=&space,.arena=&arena,.invocation=query,
                .fuel=-1,.base_env=&caller,.preserve_bindings=true,.context=&context,
                .outcomes=&rows,.he_call=query};
            tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
            Atom *next=NULL,*type=NULL;Bindings tail;bindings_init(&tail);bool replaces=false;
            QueryTableTailState state=query_equations_miss_single_tail_stream(
                &space,query,NULL,&arena,&visitor,allow_tail!=0u,&next,&type,&tail,&replaces);
            if(kind==2u) {
                CHECK(state==QUERY_TABLE_TAIL_INCOMPLETE && tracker.interrupted &&
                    tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && !rows.len && !next);
                CHECK(bindings_lookup_value_id(&caller,marker->var_id).skeleton==alias &&
                    bindings_lookup_value_id(&caller,alias->var_id).skeleton==marker);
            } else {
                CHECK(state!=QUERY_TABLE_TAIL_INCOMPLETE && !tracker.interrupted);
                CHECK(kind==1u?(!rows.len && !next):(rows.len==1u || next));
            }
            bindings_free(&tail);outcome_set_free(&rows);search_context_free(&context);
            bindings_free(&caller);
        }
    }
    g_eval_completion_tracker=old_tracker;
    bindings_free(&base);eval_set_library_context(old_library);
    cetta_library_context_free(library);free(library);space_free(&space);arena_free(&arena);
}

static void partial_row_consumer_cases(void) {
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena;arena_init(&arena);
    Bindings environment;bindings_init(&environment);
    Atom *variable=atom_var(&arena,"partial-row");
    CHECK(bindings_add_var(&environment,variable,atom_int(&arena,7)));
    Atom *term=atom_expr2(&arena,atom_symbol(&arena,"Pair"),variable);
    EvalCompletionTracker *previous=g_eval_completion_tracker;
    for(unsigned mode=0u;mode<7u;mode++) {
        OutcomeSet rows;outcome_set_init_with_owner(&rows,&arena);
        Outcome *row=outcome_set_push_slot(&rows);assert(row);
        outcome_init(row);row->atom=term;
        CHECK(bindings_clone(&row->env,&environment));
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        g_eval_completion_tracker=&tracker;
        unsigned visits=0u;
        br_env_refusals=1u;
        if(mode<4u) {
            CettaSearchPolicyOrder orders[]={(CettaSearchPolicyOrder)0,
                CETTA_SEARCH_POLICY_ORDER_REVERSE,CETTA_SEARCH_POLICY_ORDER_LEX,
                CETTA_SEARCH_POLICY_ORDER_SHORTLEX};
            CHECK(!outcome_set_visit_ordered(&arena,&rows,orders[mode],refused_row_visit,&visits));
            CHECK(!visits);
            tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
            CHECK(outcome_set_visit_ordered(&arena,&rows,orders[mode],refused_row_visit,&visits)==1u);
            CHECK(visits==1u);
        } else if(mode==4u) {
            CettaCount visited=0u;
            DirectWalkVisitorCtx walk={.visitor=refused_row_visit,.ctx=&visits,.visited=&visited};
            CHECK(!direct_outcome_walk_visit_inner(&walk,&arena,&rows));
            CHECK(walk.stopped && !visited && !visits);
        } else if(mode==5u) {
            CHECK(!prime_public_measurement_values(&arena,&rows));
            CHECK(rows.len==1u && rows.items[0].atom==term);
        } else {
#if CETTA_PRIME_EVAL_STACK
            ResultSet destination;result_set_init(&destination);
            CHECK(result_set_ensure_one(&destination));
            PrimeNativeOwnedTopRequest top={.owned={.pending=&rows},
                .destination=&destination,.reserved_items=destination.items,
                .reserved_capacity=destination.cap,.normalized=true};
            prime_native_owned_top_project(&top,&arena);
            CHECK(!top.projected && !top.committed && !destination.len && rows.len==1u);
            CHECK(top.owned.status==CETTA_CONTINUATION_DEFERRED && row->atom==term &&
                !row->materialized_atom && bindings_has_bound_values(&row->env));
            tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_COMPLETE};
            prime_native_owned_top_project(&top,&arena);
            CHECK(top.committed && destination.len==1u && !rows.len &&
                top.owned.status==CETTA_CONTINUATION_READY);
            CHECK(atom_eq(destination.items[0],parse_one(&arena,"(Pair 7)")));
            result_set_free(&destination);
#else
            br_env_refusals=0u;
#endif
        }
        CHECK(!br_env_refusals);
        if(mode==4u || mode==5u)
            CHECK(tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        outcome_set_free(&rows);
    }
    g_eval_completion_tracker=previous;
    br_env_fault_seen=false;br_calls_after_env_fault=0u;
    bindings_free(&environment);arena_free(&arena);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
}

static unsigned scoped_failure_run(const char *program,unsigned refusal) {
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena;arena_init(&arena);Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
    EvalCompletionTracker *previous=g_eval_completion_tracker;
    g_eval_completion_tracker=&tracker;
    OutcomeSet rows;outcome_set_init(&rows);
    br_env_calls=0u;br_env_fail_at=refusal;br_env_fault_seen=false;br_calls_after_env_fault=0u;
    metta_eval_bind(&space,&arena,parse_one(&arena,program),-1,&rows);
    unsigned calls=br_env_calls;
    if(refusal) {
        CHECK(br_env_fault_seen && !br_env_fail_at);
        CHECK(tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        CHECK(!br_calls_after_env_fault);
    } else CHECK(!tracker.interrupted && tracker.completion==CETTA_EVAL_COMPLETE);
    for(CettaCount i=0u;i<rows.len;i++) {
        CHECK(rows.items[i].kind==CETTA_OUTCOME_ANSWER_REF || rows.items[i].atom);
        CHECK(!atom_is_error(rows.items[i].atom));
    }
    outcome_set_free(&rows);g_eval_completion_tracker=previous;
    br_env_fail_at=0u;br_env_fault_seen=false;br_calls_after_env_fault=0u;
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);space_free(&space);arena_free(&arena);
    return calls;
}

static void scoped_failure_sweep(void) {
    const char *programs[]={
        "(let $x 7 (Pair $x (quote $x)))",
        "(chain (superpose (7 8)) $x (Pair $x (quote $x)))",
        "(let $x 7 (if (== $x 7) (Pair $x (quote $x)) bad))",
        "(collapse-bind (chain (superpose (7 8)) $x (Pair $x (quote $x))))",
        "(foldl-atom (1 2) 0 $a $b (+ $a $b))",
        "(let $s (new-state 0) (chain (change-state! $s 7) $u (get-state $s)))"};
    unsigned injected=0u;
    for(size_t i=0u;i<sizeof(programs)/sizeof(programs[0]);i++) {
        unsigned calls=scoped_failure_run(programs[i],0u);
        for(unsigned fail=1u;fail<=calls;fail++) {
            scoped_failure_run(programs[i],fail);injected++;
        }
    }
    CHECK(injected>0u);
    printf("Scoped substitution refusal sweep: %u injected call boundaries\n",injected);
}

typedef struct {
    Atom *query;
    Atom *result;
    bool fail;
    unsigned instantiations, emissions;
} EquationInstantiationControl;

static Atom *equation_instantiation_control(
    const QueryEquationInstantiation *match, Arena *arena,
    Bindings *caller_bindings, void *context) {
    EquationInstantiationControl *control=context;
    control->instantiations++;
    Atom *variable=match->pattern->expr.elems[1];
    CHECK(variable->kind==ATOM_VAR);
    CHECK(bindings_logically_empty(caller_bindings));
    Atom *value=match->substitute(arena,variable,match->context);
    CHECK(value && atom_eq(value,control->query->expr.elems[1]));
    if(control->query->expr.elems[1]->kind==ATOM_VAR)
        CHECK(!var_id_eq(variable->var_id,control->query->expr.elems[1]->var_id));
    return control->fail?NULL:match->substitute(arena,match->body,match->context);
}

static bool equation_instantiation_emit(Atom *result,const Bindings *bindings,
    void *context) {
    EquationInstantiationControl *control=context;
    control->emissions++;
    control->result=result;
    CHECK(bindings_logically_empty(bindings));
    return true;
}

static void equation_instantiation_cases(void) {
    Arena arena;arena_init_detached(&arena);
    Atom *equation=parse_one(&arena,"(= (instantiate $x) (Value $x))");
    for(unsigned alias=0u;alias<2u;alias++)
        for(unsigned fail=0u;fail<2u;fail++) {
            EquationInstantiationControl control={
                .query=parse_one(&arena,alias?"(instantiate $caller)":"(instantiate 7)"),
                .fail=fail!=0u};
            CettaCount count=99u;
            CettaMatchCompletion status=query_equation_visit_instantiated(
                equation,control.query,&arena,equation_instantiation_control,&control,
                equation_instantiation_emit,&control,&count);
            CHECK(control.instantiations==1u);
            CHECK(status==(fail?CETTA_MATCH_INCOMPLETE:CETTA_MATCH_MATCHED));
            CHECK(count==(fail?0u:1u) && control.emissions==count);
            if(!fail) {
                CHECK(control.result->kind==ATOM_EXPR && control.result->expr.len==2u);
                CHECK(atom_eq(control.result->expr.elems[1],control.query->expr.elems[1]));
            }
        }
    arena_free(&arena);
}

static void match_progress_entry_case(const char *request, bool diagnostic) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,CETTA_LANGUAGE_HE,
        cetta_profile_he_extended());
    eval_set_library_context(context);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    space_add(&space,parse_one(&arena,"(Edge A B)"));
    space_add(&space,parse_one(&arena,"(Edge A B)"));
    space_add(&space,parse_one(&arena,"(Edge B C)"));
    Atom *call=parse_one(&arena,
        "(match &self (Edge A $middle) (match &self (Edge $middle $end) (Path $middle $end)))");
    MatchEvaluator saved_entry=g_match_evaluator;
    bool saved_configured=g_match_chain_observer_configured;
    bool saved_diagnostic=g_match_chain_observer_diagnostic;
    uint64_t saved_interval=g_match_chain_observer_interval;
    uint64_t saved_progress=g_match_chain_progress;
    const char *prior=getenv(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV);
    char *saved_env=prior?strdup(prior):NULL;
    prior=getenv(CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV);
    char *saved_interval_env=prior?strdup(prior):NULL;
    assert(setenv(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV,request,1)==0);
    assert(setenv(CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV,"18446744073709551615",1)==0);
    g_match_evaluator=handle_match_initial;
    g_match_chain_observer_configured=false;
    g_match_chain_progress=0u;
    prime_need_observation_policy_changed();
    CHECK(g_prime_need_rule_use_observed == diagnostic);
    space_add(&space,parse_one(&arena,"(= (observer:add $x) (+ $x 1))"));
    PreparedPureClosedCallPlan prepared;
    CHECK(prepared_pure_closed_call_plan(&space,&arena,
        parse_one(&arena,"(observer:add 7)"),-1,true,NULL,&prepared) == !diagnostic);
    for(unsigned pass=0u;pass<2u;pass++) {
        OutcomeSet out;outcome_set_init(&out);
        CHECK(handle_match(&space,&arena,call,NULL,-1,false,&out));
        CHECK(g_match_evaluator==(diagnostic?handle_match_diagnostic:handle_match_plain));
        CHECK(out.len==2u);
        Atom *expected=parse_one(&arena,"(Path B C)");
        for(CettaCount i=0u;i<out.len;i++)
            CHECK(atom_eq(outcome_atom_materialize(&arena,&out.items[i]),expected));
        CHECK(g_match_chain_progress==(diagnostic?2u*(pass+1u):0u));
        outcome_set_free(&out);
    }
    Bindings empty;bindings_init(&empty);
    OutcomeSet out;outcome_set_init(&out);
    eval_for_caller(&space,&arena,NULL,call,-1,&empty,false,&out);
    CHECK(out.len==2u);
    Atom *expected=parse_one(&arena,"(Path B C)");
    for(CettaCount i=0u;i<out.len;i++)
        CHECK(atom_eq(outcome_atom_materialize(&arena,&out.items[i]),expected));
    CHECK(g_match_chain_progress==(diagnostic?6u:0u));
    outcome_set_free(&out);bindings_free(&empty);

    Space *spaces[]={&space,&space};
    Atom *inner=expr_arg(call,2u);
    Atom *patterns[]={expr_arg(call,1u),expr_arg(inner,1u)};
    Atom *body=expr_arg(inner,2u);
    MatchVisibleVarSet visible;match_visible_var_set_init(&visible);
    assert(collect_match_visible_vars_many(patterns,2u,&visible));
    MatchChainCertificate certificate={.active=true};DeferredSpaceSet deferred={0};
    MatchResultDirectEvalScratch scratch={0};
    outcome_set_init(&out);bindings_init(&empty);
    MatchChainDfsCtx dfs={.s=&space,.a=&arena,.fuel=-1,.os=&out,
        .step_spaces=spaces,.step_patterns=patterns,.nsteps=2u,.visible=&visible,
        .certificate=&certificate,.deferred_spaces=&deferred,.direct_scratch=&scratch,
        .body=body,.query_spaces=spaces,.nquery_spaces=2u};
    (diagnostic?match_chain_dfs_diagnostic:match_chain_dfs_plain)(&dfs,0u,&empty);
    CHECK(!certificate.failed && out.len==2u);
    for(CettaCount i=0u;i<out.len;i++)
        CHECK(atom_eq(outcome_atom_materialize(&arena,&out.items[i]),expected));
    CHECK(g_match_chain_progress==(diagnostic?8u:0u));
    outcome_set_free(&out);bindings_free(&empty);

    Bindings row;bindings_init(&row);
    assert(match_atoms(patterns[1],parse_one(&arena,"(Edge B C)"),&row,&arena));
    outcome_set_init(&out);
    PathmapConjunctionStreamCtx indexed={.space=&space,.arena=&arena,.fuel=-1,
        .outcomes=&out,.visible=&visible,.body=body,.query_space=&space,
        .preserve_bindings=true,.certificate=&certificate,.deferred_spaces=&deferred,
        .direct_scratch=&scratch,.factor_count=2u};
    CHECK((diagnostic?pathmap_conjunction_stream_row_diagnostic:
        pathmap_conjunction_stream_row_plain)(&row,&indexed));
    CHECK(out.len==1u);
    if(out.len==1u)CHECK(atom_eq(outcome_atom_materialize(&arena,&out.items[0]),expected));
    CHECK(g_match_chain_progress==(diagnostic?9u:0u));
    outcome_set_free(&out);bindings_free(&row);match_visible_var_set_free(&visible);
    match_result_direct_eval_scratch_free(&scratch);deferred_space_set_free(&deferred);
    g_match_evaluator=saved_entry;
    g_match_chain_observer_configured=saved_configured;
    g_match_chain_observer_diagnostic=saved_diagnostic;
    g_match_chain_observer_interval=saved_interval;
    g_match_chain_progress=saved_progress;
    if(saved_env) {assert(setenv(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV,saved_env,1)==0);free(saved_env);}
    else assert(unsetenv(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)==0);
    if(saved_interval_env) {assert(setenv(CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV,saved_interval_env,1)==0);free(saved_interval_env);}
    else assert(unsetenv(CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV)==0);
    prime_need_observation_policy_changed();
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static void release_stack_control(PrimeNativeControl *control) {
    prime_native_control_free(control);
    outcome_set_free(&control->output);
    if (control->base_environment_initialized)
        bindings_free(&control->base_environment);
    if (control->owned_arena)
        arena_free(&control->storage);
}

typedef struct {
    size_t count;
    Atom *value;
    size_t weighted;
    size_t lengths[16];
    uint64_t factors[16][8];
    int64_t coefficients[16][8];
} Observed;
static bool observe(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    const Bindings *environment=&occurrence->env;
    (void)arena;Observed *out=raw;
    out->count++;out->value=value;
    const CettaWeightLedger *ledger=bindings_weight_view(environment);
    if(ledger->length) {
        assert(out->weighted<16u && ledger->length<=8u);
        size_t row=out->weighted++;
        out->lengths[row]=ledger->length;
        const CettaWeightFactor **ordered=cetta_weight_ledger_ordered(ledger);
        assert(ordered);
        for(size_t i=0u;i<ledger->length;i++) {
            out->factors[row][i]=ordered[i]->occurrence;
            assert(ordered[i]->value->kind==ATOM_GROUNDED &&
                   ordered[i]->value->ground.gkind==GV_INT);
            out->coefficients[row][i]=ordered[i]->value->ground.ival;
        }
        free(ordered);
    }
    return true;
}

/* Refuse actual copy/growth services of a registered ordinary nested
 * selector, then retry publication into its original owned destination. */
static void nested_return_preparation_case(unsigned workers, unsigned selection,
    bool retain, unsigned fault, bool cancel, bool existing) {
    br_return_worker_tracking=true;
    br_return_worker_waves=br_return_worker_tasks=0u;
    Arena arena, foreign;arena_init(&arena);arena_init(&foreign);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    char text[512];
    space_add(&space,parse_one(&arena,
        "(= (return:work $n $answer) (if (== $n 0) $answer "
        "(return:work (- $n 1) $answer)))"));
    for(unsigned i=12u;i;i--) {
        snprintf(text,sizeof(text),"(= (return:jobs) (return:work 4 %u))",i);
        space_add(&space,parse_one(&arena,text));
    }
    Bindings empty;bindings_init(&empty);
    if(existing) {
        PrimeNativeControl *held=cetta_malloc(sizeof(*held));
        CettaSearchPolicySpec held_policy={.native_control=true,.all=true,
            .retain=true,.workers=workers};
        assert(prime_native_control_init(held,&space,&arena,parse_one(&arena,"(return:jobs)"),-1,true,
            (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
            &held_policy,&empty,NULL,NULL));
        Atom *holder=NULL;
        assert(prime_native_control_admit_owned(&held,&arena,&holder)==CETTA_CONTINUATION_READY);
        registry_bind(&registry,"&return-owned",holder);
    }
    snprintf(text,sizeof(text),
        "(select %u (search-policy (control FIFO (workers %u) (budget 100000) "
        "(retain %s) %s)) %s)",selection?3u:12u,workers,
        retain?"True":"False",selection==1u?"(goal $xs True)":
        selection==2u?"(best $x $x)":"",existing?"&return-owned":"(return:jobs)");
    PrimeNativeControl parent;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=workers};
    assert(prime_native_control_init(&parent,&space,&arena,parse_one(&arena,text),-1,true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,&empty,NULL,NULL));
    Observed observed={0};
    PrimeEvalStackFrame *nested=NULL;
    for(unsigned slice=0u;slice<1000u && !nested;slice++) {
        prime_native_control_run(&parent,1u,observe,&observed);
        for(PrimeEvalStackFrame *f=parent.frames;f;f=f->control_next)
            if(f->kind==PRIME_EVAL_STACK_FRAME_CONTROL_NESTED) {nested=f;break;}
    }
    if (!nested || nested->control_nested_operation || nested->normalization_child_count) {
        fprintf(stderr,"nested return preparation: workers=%u selection=%u retain=%u existing=%u fault=%u nested=%p failed=%u completion=%u steps=%" PRIu64 " observed=%zu\n",
            workers,selection,retain,existing,fault,(void *)nested,parent.failed,
            (unsigned)parent.completion,parent.steps,observed.count);
        for (PrimeEvalStackFrame *f=parent.frames;f;f=f->control_next)
            fprintf(stderr,"  frame kind=%u state=%u nested-operation=%u children=%zu\n",
                (unsigned)f->kind,(unsigned)f->state,f->control_nested_operation != NULL,
                (size_t)f->normalization_child_count);
    }
    assert(nested && !nested->control_nested_operation && !nested->normalization_child_count);
    CHECK(nested->control_nested_retained==existing);
    PrimeNativeControl *child=nested->control_nested;
    br_return_copy_owner=nested->arena;
    br_return_copy_value=selection==2u?(fault?2:1):(fault?11:12);
    if(fault!=2u)br_return_copy_refusals=1u;
    bool armed=fault!=2u;
    for(unsigned slice=0u;slice<10000u &&
            nested->state!=PRIME_EVAL_STACK_FRAME_RETURN_CAPTURE;slice++) {
        if(!armed && nested->normalization_child_count) {
            br_return_realloc_owner=nested->normalization_children;
            br_return_realloc_refusals=1u;armed=true;
        }
        prime_native_control_run(&parent,workers,observe,&observed);
        assert(!prime_native_control_closed(&parent) && !parent.failed);
    }
    if (!armed || nested->state != PRIME_EVAL_STACK_FRAME_RETURN_CAPTURE)
        fprintf(stderr,"nested capture: workers=%u selection=%u retain=%u existing=%u fault=%u armed=%u state=%u child-steps=%" PRIu64 " parent-steps=%" PRIu64 " child-failed=%u child-done=%u copy-left=%u growth-left=%u prefix=%zu\n",
            workers,selection,retain,existing,fault,armed,(unsigned)nested->state,
            child->steps,parent.steps,child->failed,prime_native_control_closed(child),
            br_return_copy_refusals,br_return_realloc_refusals,(size_t)nested->normalization_child_count);
    assert(armed && nested->state==PRIME_EVAL_STACK_FRAME_RETURN_CAPTURE);
    CHECK(!br_return_copy_refusals && !br_return_realloc_refusals && !observed.count);
    CHECK(!child->failed && !child->invalidated && child->leased);
    size_t prefix=nested->normalization_child_count;
    CHECK(prefix==(fault==2u?8u:fault==0u?0u:1u));
    Atom *first=prefix?nested->normalization_children[0]:NULL;
    uint64_t paid=child->steps,budget=nested->control_nested_budget;
    CettaCount row_index=nested->index;
    const Arena *original=nested->target->payload_owner;
    nested->target->payload_owner=&foreign;
    for(unsigned refusal=0u;refusal<2u;refusal++) {
        if(fault==2u)br_return_realloc_refusals=1u;
        else br_return_copy_refusals=1u;
        for(unsigned slice=0u;slice<100u &&
                (br_return_copy_refusals || br_return_realloc_refusals);slice++)
            prime_native_control_run(&parent,workers,observe,&observed);
        CHECK(!br_return_copy_refusals && !br_return_realloc_refusals);
        CHECK(nested->state==PRIME_EVAL_STACK_FRAME_RETURN_CAPTURE && nested->index==row_index &&
            nested->normalization_child_count==prefix && child->steps==paid &&
            nested->control_nested_budget==budget && !child->failed && !observed.count);
        if(prefix)CHECK(nested->normalization_children[0]==first);
    }
    if(fault==3u) {
        br_return_holder_owner=nested->arena;br_return_holder_refusals=1u;
        for(unsigned slice=0u;slice<10000u && br_return_holder_refusals;slice++)
            prime_native_control_run(&parent,workers,observe,&observed);
        assert(!br_return_holder_refusals &&
            nested->state==PRIME_EVAL_STACK_FRAME_RETURN_ENROLLED);
        uint64_t enrolled=nested->control_nested_handle;
        uint64_t next=context.native_handle_next_id, spent=child->steps;
        CHECK(enrolled && !nested->ref && !nested->child.len && child->leased &&
            cetta_native_handle_get(&context,"prime-search",enrolled)==child);
        for(unsigned retry=0u;retry<2u;retry++) {
            br_return_holder_refusals=1u;
            for(unsigned slice=0u;slice<100u && br_return_holder_refusals;slice++)
                prime_native_control_run(&parent,workers,observe,&observed);
            CHECK(!br_return_holder_refusals && nested->control_nested_handle==enrolled &&
                context.native_handle_next_id==next && child->steps==spent &&
                !nested->ref && !nested->child.len && !observed.count);
        }
        br_return_holder_owner=NULL;
        if(cancel) {
            release_stack_control(&parent);
            CHECK(!cetta_native_handle_get(&context,"prime-search",enrolled));
            goto cleanup;
        }
    }
    for(unsigned slice=0u;slice<10000u &&
            !(nested->child_initialized && nested->child.len);slice++) {
        prime_native_control_run(&parent,workers,observe,&observed);
        assert(!prime_native_control_closed(&parent) && !parent.failed);
    }
    assert(nested->child_initialized && nested->child.len==1u && nested->child.items[0].atom);
    CHECK(nested->state==(retain && !existing?PRIME_EVAL_STACK_FRAME_RETURN_ENROLLED:
        PRIME_EVAL_STACK_FRAME_RETURN_PREPARE));
    CHECK(child->leased && !observed.count && nested->control_nested==child);
    if(selection)CHECK(child->goal_reported && child->selected_delivered==3u);
    Atom *packet=nested->child.items[0].atom;
    Atom *values=retain?expr_arg(packet,0u):packet;
    CHECK(values->kind==ATOM_EXPR && values->expr.len==(selection?3u:12u));
    if(values->kind==ATOM_EXPR)for(CettaExprIndex i=0u;i<values->expr.len;i++) {
        CHECK(values->expr.elems[i]->kind==ATOM_GROUNDED &&
            values->expr.elems[i]->ground.gkind==GV_INT &&
            values->expr.elems[i]->ground.ival==(int64_t)(selection==2u?i+1u:12u-i));
    }
    paid=child->steps;budget=nested->control_nested_budget;
    uint64_t handle=nested->control_nested_handle;
    for(unsigned retry=0u;retry<3u;retry++) {
        prime_native_control_run(&parent,workers,observe,&observed);
        CHECK(!observed.count && nested->child.len==1u && nested->child.items[0].atom==packet &&
            child->steps==paid && nested->control_nested_budget==budget && child->leased);
        CHECK(!retain || cetta_native_handle_get(&context,"prime-search",handle)==child);
    }
    if(!cancel) {
        nested->target->payload_owner=(Arena *)original;
        for(unsigned slice=0u;slice<1000u && !prime_native_control_closed(&parent);slice++)
            prime_native_control_run(&parent,10u,observe,&observed);
        CHECK(prime_native_control_closed(&parent) && observed.count==1u);
        CHECK(observed.value && atom_eq(observed.value,packet));
        CHECK(!retain || (cetta_native_handle_get(&context,"prime-search",handle)==child &&
            child->steps==paid && !child->leased));
    }
    br_return_copy_owner=NULL;br_return_realloc_owner=NULL;
    release_stack_control(&parent);
    if(cancel && retain) {
        if(existing)CHECK(cetta_native_handle_get(&context,"prime-search",handle)==child &&
            !child->leased && child->steps==paid);
        else CHECK(!cetta_native_handle_get(&context,"prime-search",handle));
    }
cleanup:
    br_return_copy_owner=NULL;br_return_realloc_owner=NULL;br_return_holder_owner=NULL;
    CHECK(workers==1u || (br_return_worker_waves && br_return_worker_tasks));
    br_return_worker_tracking=false;
    bindings_free(&empty);eval_set_library_context(NULL);cetta_library_context_free(&context);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&foreign);arena_free(&arena);
}

static void nested_return_world_case(void) {
    Arena arena;arena_init(&arena);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    Bindings caller;bindings_init(&caller);
    Outcome row;outcome_init(&row);
    Atom *variable=atom_var(&arena,"return:world");
    CHECK(bindings_add_var(&caller,variable,atom_int(&arena,99)));
    CHECK(bindings_add_var(&row.env,variable,atom_int(&arena,7)));
    PrimeNeedSnapshot world, ambient;
    prime_need_snapshot_init(&world);prime_need_snapshot_init(&ambient);
    assert(prime_need_snapshot_begin(&world) && prime_need_snapshot_begin(&ambient));
    uint64_t row_id=0u, ambient_id=0u;
    assert(prime_need_snapshot_allocate(&arena,&world,atom_int(&arena,7),&world,&row_id));
    assert(prime_need_snapshot_allocate(&arena,&ambient,atom_int(&arena,99),&ambient,&ambient_id));
    *bindings_need_mut(&row.env)=world;*bindings_need_mut(&caller)=ambient;
    row.atom=prime_need_ref(&arena,&world,row_id);
    PrimeEvalStackFrame frame={.arena=&arena};
    PrimeNeedActiveGuard active;prime_need_active_enter_captured(&active,&caller);
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
    EvalCompletionTracker *previous=g_eval_completion_tracker;
    g_eval_completion_tracker=&tracker;
    CHECK(prime_native_nested_capture_value(&frame,row.atom,&row));
    CHECK(frame.normalization_child_count==1u &&
        frame.normalization_children[0]->kind==ATOM_GROUNDED &&
        frame.normalization_children[0]->ground.ival==7);
    CHECK(g_prime_need_logical_env==&caller && g_prime_need_active.session_id==ambient.session_id &&
        g_eval_completion_tracker==&tracker &&
        !tracker.interrupted && tracker.completion==CETTA_EVAL_COMPLETE);
    /* Observation displays a private demand, but does not replace an ordinary
     * variable by a binding from either the answer or the caller. */
    CHECK(prime_native_nested_capture_value(&frame,variable,&row));
    CHECK(frame.normalization_child_count==2u &&
        atom_eq(frame.normalization_children[1],variable));
    tracker=(EvalCompletionTracker){.completion=CETTA_EVAL_INCOMPLETE_CANCELLED,.interrupted=true};
    CHECK(!prime_native_nested_capture_value(&frame,variable,&row));
    CHECK(frame.normalization_child_count==2u && g_eval_completion_tracker==&tracker &&
        tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_CANCELLED);
    g_eval_completion_tracker=previous;prime_need_active_leave(&active);
    free(frame.normalization_children);outcome_free_fields(&row);bindings_free(&caller);
    eval_set_library_context(NULL);cetta_library_context_free(&context);arena_free(&arena);
}

static void nested_return_preparation_cases(void) {
    nested_return_world_case();
    for(unsigned workers=1u;workers<=4u;workers*=2u)
        for(unsigned selection=0u;selection<3u;selection++)
            for(unsigned retain=0u;retain<2u;retain++) {
                nested_return_preparation_case(workers,selection,retain!=0u,0u,false,false);
                nested_return_preparation_case(workers,selection,retain!=0u,1u,false,false);
                nested_return_preparation_case(workers,selection,retain!=0u,1u,true,false);
            }
    for(unsigned workers=1u;workers<=4u;workers*=2u)
        nested_return_preparation_case(workers,0u,true,2u,false,false);
    for(unsigned selection=0u;selection<3u;selection++)
        for(unsigned cancel=0u;cancel<2u;cancel++)
            nested_return_preparation_case(2u,selection,true,1u,cancel!=0u,true);
    for(unsigned cancel=0u;cancel<2u;cancel++)
        nested_return_preparation_case(2u,2u,true,3u,cancel!=0u,false);
    puts("Owned nested return preparation: callback order, paid suffix, retry, publication and cancellation passed");
}

static void refusal_case(CettaLanguageId language, bool prepared, uint64_t allowance) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,
        language==CETTA_LANGUAGE_HE?cetta_profile_he_extended():cetta_profile_petta_extended());
    eval_set_library_context(context);
    space_add(&space,parse_one(&arena,"(= (lease-result) 7)"));
    PrimeNativeControl control;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.work_budget=UINT64_MAX,.workers=2u};
    assert(prime_native_control_init(&control,&space,&arena,parse_one(&arena,"(lease-result)"),
        -1,false,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    assert(native_dialect_schedule(&control,&space,parse_one(&arena,"(lease-result)"),
        -1,&control.base_environment,NULL,&control.output));
    assert(cetta_continuation_hub_length(&control.hub)==2u);
    assert(cetta_selection_automaton_ratio(2u,&control.hub.schedule));
    if(prepared) {
        PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
        uint64_t actual=0u;
        assert(prime_native_control_parallel_wave(&control,2u,&actual));
        assert(actual==2u && !control.parallel_wave_pending);
        control.steps+=actual;
        prime_native_run_leave(&guard);
    }
    CettaOwnedContinuation retained[2];
    NativeDialectWork retained_jobs[2];
    PettaMachine retained_machines[2];
    for(size_t i=0u;i<2u;i++) {
        retained[i]=*cetta_continuation_hub_at(&control.hub,i);
        NativeDialectWork *job=((PrimeNativeWork *)retained[i].payload)->dialect;
        assert(job && job->operation_pending==prepared);
        retained_jobs[i]=*job;
        retained_machines[i]=job->machine->machine;
    }
    control.policy.workers=1u;
    for(size_t i=0u;i<2u;i++) {
        NativeDialectWork *job=((PrimeNativeWork *)retained[i].payload)->dialect;
        CettaStorageCensus census={0};
        native_dialect_work_storage(job,&census);cetta_storage_finish(&census);
        if(census.incomplete)fprintf(stderr,"dialect %u prepared %u storage: %s\n",
            (unsigned)language,(unsigned)prepared,census.incomplete);
        CHECK(!census.incomplete && !census.invalidated && !census.overflow);
        CHECK(census.retained>=sizeof(*job)+sizeof(*job->machine));
        size_t retained_bytes=census.retained,objects=census.length;
        native_dialect_work_storage(job,&census);cetta_storage_finish(&census);
        CHECK(census.retained==retained_bytes && census.length==objects && census.repeated>0u);
        CHECK(memcmp(job,&retained_jobs[i],sizeof(*job))==0);
        cetta_storage_census_free(&census);
    }
    uint64_t next_lease=control.hub.store.next_lease;
    control.hub.store.next_lease=0u;
    const CettaSelectionAutomaton retained_schedule=control.hub.schedule;
    const SpaceReadToken retained_source=control.source_read;
    const uint64_t steps=control.steps;
    Observed observed={0};
    for(unsigned attempt=0;attempt<3u;attempt++) {
        prime_native_control_run(&control,allowance,observe,&observed);
        CHECK(control.steps==steps);
        CHECK(memcmp(&control.hub.schedule,&retained_schedule,sizeof(retained_schedule))==0);
        CHECK(cetta_continuation_hub_length(&control.hub)==2u);
        for(size_t i=0u;i<2u;i++) {
            CHECK(memcmp(cetta_continuation_hub_at(&control.hub,i),&retained[i],sizeof(retained[i]))==0);
            NativeDialectWork *job=((PrimeNativeWork *)retained[i].payload)->dialect;
            CHECK(memcmp(job,&retained_jobs[i],sizeof(*job))==0);
            CHECK(memcmp(&job->machine->machine,&retained_machines[i],sizeof(retained_machines[i]))==0);
        }
        CHECK(memcmp(&control.source_read,&retained_source,sizeof(retained_source))==0);
        CHECK(!observed.count && !control.output.len && !control.published);
        CHECK(!control.failed && !control.invalidated && !prime_native_control_closed(&control));
        CHECK(control.driver.capacity_blocked && control.completion==CETTA_EVAL_INCOMPLETE_CAPACITY);
        CHECK(!control.active_allowance && !control.active_spent);
        CHECK(!control.hub.store.lease_length && !control.hub.store.next_lease);
    }
    /* Restoring only the locally injected allocator state must resume the
     * same captured machine. No source step is reconstructed by the test. */
    control.hub.store.next_lease=next_lease;
    prime_native_control_run(&control,1000u,observe,&observed);
    CHECK(observed.count==2u && atom_eq(observed.value,atom_int(&arena,7)));
    CHECK(prime_native_control_closed(&control));
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static void unchanged_schedule_case(CettaLanguageId language,const CettaProfile *profile) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);
    eval_set_library_context(context);
    const char *rows[]={
        "(= (paths) (slow 5 A))", "(= (paths) (slow 2 B))",
        "(= (paths) (slow 2 B))", "(= (slow 0 $x) $x)",
        "(= (slow $n $x) (if (> $n 0) (slow (- $n 1) $x) (empty)))"};
    for(size_t i=0u;i<sizeof(rows)/sizeof(*rows);i++)
        space_add(&space,parse_one(&arena,rows[i]));
    PrimeNativeControl *control=malloc(sizeof(*control));assert(control);
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
        .priority=1,.age_share=2u,.workers=1u,.work_budget=0u};
    assert(prime_native_control_init(control,&space,&arena,parse_one(&arena,"(paths)"),
        -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    Observed observed={0};
    unsigned rounds=0u;
    while(!control->hub.schedule.state && !prime_native_control_closed(control) && rounds++<200u)
        prime_native_control_run(control,1u,observe,&observed);
    assert(control->hub.schedule.state);
    uint64_t id;assert(cetta_native_handle_alloc(context,"prime-search",control,
        prime_native_control_destroy,&id));
    Atom *handle=cetta_native_handle_owned_atom(context,&arena,"prime-search",id);
    const CettaSelectionAutomaton schedule=control->hub.schedule;
    const uint64_t steps=control->steps,delivered=control->delivered_count,published=control->published;
    const size_t output=control->output.len,length=cetta_continuation_hub_length(&control->hub);
    const SpaceReadToken source=control->source_read;
    CettaOwnedContinuation *frontier=calloc(length,sizeof(*frontier));assert(frontier);
    PrimeNativeWork *packets=calloc(length,sizeof(*packets));assert(packets);
    NativeDialectWork *dialect=calloc(length,sizeof(*dialect));assert(dialect);
    PettaMachine *machines=calloc(length,sizeof(*machines));assert(machines);
    for(size_t i=0u;i<length;i++) {
        frontier[i]=*cetta_continuation_hub_at(&control->hub,i);
        packets[i]=*(PrimeNativeWork *)frontier[i].payload;
        if(packets[i].dialect) {
            dialect[i]=*packets[i].dialect;
            machines[i]=dialect[i].machine->machine;
        }
    }
    size_t frame_count=0u;
    for(PrimeEvalStackFrame *f=control->frames;f;f=f->control_next)frame_count++;
    PrimeEvalStackFrame *frames=calloc(frame_count?frame_count:1u,sizeof(*frames));assert(frames);
    size_t at=0u;
    for(PrimeEvalStackFrame *f=control->frames;f;f=f->control_next)frames[at++]=*f;
    OutcomeSet out;outcome_set_init(&out);
    for(unsigned resume=0u;resume<3u;resume++) {
        CHECK(prime_native_control_select(&space,&arena,handle,-1,0u,&policy,&out,NULL,NULL));
        CHECK(!memcmp(&schedule,&control->hub.schedule,sizeof(schedule)));
        CHECK(control->steps==steps && control->delivered_count==delivered &&
              control->published==published && control->output.len==output);
        CHECK(!memcmp(&source,&control->source_read,sizeof(source)));
        CHECK(length==cetta_continuation_hub_length(&control->hub));
        for(size_t i=0u;i<length;i++) {
            CHECK(!memcmp(&frontier[i],cetta_continuation_hub_at(&control->hub,i),sizeof(*frontier)));
            PrimeNativeWork *work=frontier[i].payload;
            CHECK(!memcmp(&packets[i],work,sizeof(*work)));
            if(work->dialect) {
                CHECK(!memcmp(&dialect[i],work->dialect,sizeof(*dialect)));
                CHECK(!memcmp(&machines[i],&work->dialect->machine->machine,sizeof(*machines)));
            }
        }
        at=0u;
        for(PrimeEvalStackFrame *f=control->frames;f;f=f->control_next) {
            CHECK(at<frame_count && !memcmp(&frames[at],f,sizeof(*f)));at++;
        }
        CHECK(at==frame_count);
    }
    /* Worker/key changes can withdraw grants or create advice, but neither
     * changes this graph or grants permission to reset its protected phase. */
    policy.workers=2u;
    CHECK(prime_native_control_select(&space,&arena,handle,-1,0u,&policy,&out,NULL,NULL));
    CHECK(!memcmp(&schedule,&control->hub.schedule,sizeof(schedule)));
    policy.key_pattern=parse_one(&arena,"$candidate");
    policy.key_expression=atom_int(&arena,3);
    CHECK(prime_native_control_select(&space,&arena,handle,-1,0u,&policy,&out,NULL,NULL));
    CHECK(!memcmp(&schedule,&control->hub.schedule,sizeof(schedule)));
    for(size_t i=0u;i<length;i++) {
        bool retained=false;
        for(size_t j=0u;j<cetta_continuation_hub_length(&control->hub);j++) {
            const CettaOwnedContinuation *item=cetta_continuation_hub_at(&control->hub,j);
            if(item->occurrence_id==frontier[i].occurrence_id)
                retained=item->payload==frontier[i].payload && item->provider==frontier[i].provider;
        }
        CHECK(retained);
    }
    policy.age_share=4u;
    CHECK(prime_native_control_select(&space,&arena,handle,-1,0u,&policy,&out,NULL,NULL));
    CHECK(!control->hub.schedule.state && control->hub.schedule.state_count==5u);
    CHECK(!cetta_selection_automaton_same_graph(&schedule,&control->hub.schedule));
    outcome_set_free(&out);free(frontier);free(packets);free(dialect);free(machines);free(frames);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static uint64_t mixed_packet_case(CettaLanguageId language,const CettaProfile *profile,
                              bool invalidate, bool cancel, bool graded, uint64_t split) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space,jobs;space_init(&space);space_init(&jobs);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);
    eval_set_library_context(context);
    term_universe_set_persistent_arena(&context->term_universe,&arena);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    if(graded) {
        const char *rows[]={"(Edge 7)","(Edge 7)",
            "(=w (mixed-result) (match &self (Edge $x) $x (where 5)) 3)",
            "(=w (mixed-result) (match &self (Edge $x) $x (where 5)) 3)",
            "(= (accept $w) Maybe)","(=w (mixed-result) 7 11 (admit accept))"};
        for(size_t i=0u;i<sizeof(rows)/sizeof(*rows);i++)
            space_add(&space,parse_one(&arena,rows[i]));
    } else space_add(&space,parse_one(&arena,"(=w (mixed-result) 7 3)"));
    space_add(&jobs,parse_one(&arena,"Token"));
    space_add(&jobs,parse_one(&arena,"(exec Once (I (read Token)) (O (+ Finished)))"));
    CettaSearchPolicySpec policy={.native_control=true,.all=true,
        .work_budget=UINT64_MAX,.workers=1u,.costs=true,
        .resource_profile=prime_need_symbols()->resource_transaction};
    PrimeNativeControl control;
    assert(prime_native_control_init(&control,&space,&arena,atom_space(&arena,&jobs),-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    Observed observed={0};
    unsigned polls=0u;
    while(!cetta_space_agenda_match_capture(control.space_agenda)) {
        assert(polls++<1000u);
        prime_native_control_run(&control,1u,observe,&observed);
    }
    assert(!observed.count && cetta_continuation_hub_length(&control.hub)==1u);
    PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
    Atom *call=parse_one(&arena,"(mixed-result)");
    bool scheduled=language==CETTA_LANGUAGE_PRIME
        ? prime_eval_stack_schedule_normalize(&space,&arena,call,-1,
            &control.base_environment,g_prime_need_evaluator_id,&control.output)
        : native_dialect_schedule(&control,&space,call,-1,
            &control.base_environment,NULL,&control.output);
    assert(scheduled && cetta_continuation_hub_length(&control.hub)==2u);
    control.policy.workers=2u;
    CettaOwnedContinuation before[2];
    for(size_t i=0u;i<2u;i++)before[i]=*cetta_continuation_hub_at(&control.hub,i);
    CettaSelectionAutomaton schedule=control.hub.schedule;
    uint64_t used=0u;
    bool wave=prime_native_control_parallel_wave(&control,cancel?1u:2u,&used);
    CHECK(wave);assert(wave);
    CHECK(used==(cancel?1u:2u) && control.parallel_wave_pending==cancel);
    CHECK(control.parallel_inflight_count==(cancel?2u:0u) && !control.hub.store.lease_length);
    CHECK(!memcmp(&schedule,&control.hub.schedule,sizeof(schedule)));
    CHECK(cetta_continuation_hub_length(&control.hub)==2u && !control.output.len);
    for(size_t i=0u;i<2u;i++) {
        CHECK(!memcmp(&before[i],cetta_continuation_hub_at(&control.hub,i),sizeof(before[i])));
        CHECK(((PrimeNativeWork *)before[i].payload)->prepared_publication==!cancel);
    }
    CHECK(((PrimeNativeWork *)before[0].payload)->resource_step);
    CHECK(cetta_space_agenda_match_prepared(control.space_agenda));
    CHECK(language==CETTA_LANGUAGE_PRIME
        ? ((PrimeNativeWork *)before[1].payload)->parallel->child!=NULL
        : ((PrimeNativeWork *)before[1].payload)->dialect!=NULL);
    control.steps+=used;
    prime_native_run_leave(&guard);

    if(invalidate) {
        space_add(&space,parse_one(&arena,"(= (changed-source) 9)"));
        /* Resource-only work has its own revision contract. The prepared
         * authored equation result must retain its old source authority. */
        PrimeNativeWork *resource=before[0].payload;
        resource->key=atom_int(&arena,100);
        ((PrimeNativeWork *)before[1].payload)->key=atom_int(&arena,0);
        control.policy.priority=1;
        prime_native_control_run(&control,0u,observe,&observed);
        CHECK(control.invalidated && !control.output.len && !observed.count);
        CHECK(((PrimeNativeWork *)before[1].payload)->prepared_publication);
    } else if(!cancel) {
        PrimeNativeWork packets[2]={*(PrimeNativeWork *)before[0].payload,
                                  *(PrimeNativeWork *)before[1].payload};
        uint64_t lease=control.hub.store.next_lease;
        control.hub.store.next_lease=0u;
        uint64_t paid=control.steps;
        for(unsigned retry=0u;retry<3u;retry++) {
            prime_native_control_run(&control,0u,observe,&observed);
            CHECK(control.steps==paid && !control.output.len && !observed.count);
            CHECK(!memcmp(&schedule,&control.hub.schedule,sizeof(schedule)));
            for(size_t i=0u;i<2u;i++) {
                CHECK(!memcmp(&before[i],cetta_continuation_hub_at(&control.hub,i),sizeof(before[i])));
                CHECK(!memcmp(&packets[i],before[i].payload,sizeof(packets[i])));
            }
            CHECK(cetta_space_agenda_match_prepared(control.space_agenda));
        }
        control.hub.store.next_lease=lease;
        prime_native_run_enter(&control,&guard);
        uint64_t nodes=control.event_cost.total.counts[CETTA_COST_MATCH_NODE];
        CHECK(prime_native_control_step(&control,0u,false,false)==0u);
        CHECK(!cetta_space_agenda_match_prepared(control.space_agenda));
        CHECK(control.event_cost.total.counts[CETTA_COST_MATCH_NODE]==nodes);
        prime_native_run_leave(&guard);
        if(split)prime_native_control_run(&control,split,observe,&observed);
        prime_native_control_run(&control,10000u-split,observe,&observed);
        CHECK(prime_native_control_closed(&control)==!graded);
        CHECK(observed.count==(graded?5u:2u));
        CHECK(space_contains_exact(&jobs,parse_one(&arena,"Finished")));
        if(graded) {
            CHECK(observed.weighted==4u);
            uint64_t first[2]={0u,0u};unsigned uses[2]={0u,0u};
            for(size_t i=0u;i<observed.weighted;i++) {
                CHECK(observed.lengths[i]==2u);
                CHECK(observed.coefficients[i][0]==3 && observed.coefficients[i][1]==5);
                uint64_t outer=observed.factors[i][0];
                if(!first[0])first[0]=outer;
                if(outer==first[0])uses[0]++;
                else {if(!first[1])first[1]=outer;CHECK(outer==first[1]);uses[1]++;}
                for(size_t j=0u;j<i;j++)CHECK(observed.factors[j][1]!=observed.factors[i][1]);
            }
            CHECK(first[0] && first[1] && first[0]!=first[1] && uses[0]==2u && uses[1]==2u);
            CHECK(prime_native_control_waiting_verdict(&control));
            uint64_t parked=control.steps;
            size_t outputs=control.output.len,frames=control.open_frames;
            prime_native_control_run(&control,100u,observe,&observed);
            CHECK(control.steps==parked && control.output.len==outputs && control.open_frames==frames);
            CHECK(!prime_native_control_closed(&control) && !control.failed && !control.invalidated);
        }
    }
    uint64_t final_steps=control.steps;
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);space_free(&jobs);arena_free(&arena);
    return final_steps;
}

/* The first-value query has returned before mutation. Its complete-query
 * authority belongs to the waiting caller, including when an empty read
 * returned synchronously into the caller's equation continuation. */
static void implicit_first_source_case(bool empty_query, bool mutate_read) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space,kb,other;space_init(&space);space_init(&kb);space_init(&other);
    Registry registry;registry_init(&registry);g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    registry_bind(&registry,"&kb",atom_space(&arena,&kb));
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(context);
    term_universe_set_persistent_arena(&context->term_universe,&arena);
    space_add(&kb,parse_one(&arena,"(Fact 7)"));
    space_add(&space,parse_one(&arena,"(= (lag 0 $v) $v)"));
    space_add(&space,parse_one(&arena,"(= (lag $n $v) (if (> $n 0) (lag (- $n 1) $v) (empty)))"));
    space_add(&space,parse_one(&arena,"(= (wait-for-query) (wait-for-query))"));
    space_add(&space,parse_one(&arena,"(= (query-choice) (select (match &kb (Missing $x) $x)))"));
    space_add(&space,parse_one(&arena,"(= (query-choice) (weight:where 9 (wait-for-query)))"));
    const char *expression=empty_query ? "(query-choice)"
        : "(let $x (select (match &kb (Fact $n) $n)) (lag 50 $x))";
    PrimeNativeControl control;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.workers=1u};
    assert(prime_native_control_init(&control,&space,&arena,parse_one(&arena,expression),-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
    PrimeEvalStackFrame *returned=NULL;
    Observed observed={0};
    for(unsigned tick=0u;tick<1000u && !returned;tick++) {
        prime_native_control_run(&control,1u,observe,&observed);
        if(control.query_read_count==1u) {
            PrimeEvalStackFrame *first=empty_query?control.frames:control.retired_frames;
            for(PrimeEvalStackFrame *frame=first;frame;frame=frame->control_next)
                if(empty_query
                    ? frame->kind==PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH &&
                      frame->equation_search && frame->atom && frame->atom->kind==ATOM_EXPR && frame->atom->expr.len>0u &&
                      symbol_name_equals(frame->atom->expr.elems[0],"query-choice")
                    : frame->kind==PRIME_EVAL_STACK_FRAME_BIND_FINISH &&
                      frame->atom && frame->atom->kind==ATOM_EXPR && frame->atom->expr.len>0u &&
                      atom_is_symbol_id(frame->atom->expr.elems[0],g_builtin_syms.select)) {
                    returned=frame;break;
                }
        }
    }
    CHECK(returned!=NULL);assert(returned);
    /* Enumeration may be closed while its divergent child remains pending.
     * The complete-query obligation is live until notification, not merely
     * until the equation cursor has stopped issuing work. */
    CHECK(empty_query ? !returned->control_notified && returned->equation_search!=NULL &&
                        returned->control_pending>0u
                      : returned->control_closed && returned->control_notified);
    CHECK(control.query_read_count==1u && !observed.count && !control.output.len);
    CHECK(space_read_token_is_current(control.query_reads[0]));
    CHECK(!returned->control_nested && returned->target!=NULL);
    OutcomeSet *packet=returned->target;
    CHECK(packet->len==(empty_query?0u:1u));
    if(!empty_query)CHECK(atom_eq(outcome_atom_materialize(&arena,&packet->items[0]),atom_int(&arena,7)));
    const OutcomeSet packet_before=*packet;
    Outcome answer_before={0};if(!empty_query)answer_before=packet->items[0];
    const PrimeEvalStackFrame return_before=*returned;
    PrimeNeedEquationContinuation equation_before={0};
    if(empty_query)equation_before=*returned->equation_search;
    const SpaceReadToken read_before=control.query_reads[0];
    const CettaSelectionAutomaton schedule_before=control.hub.schedule;
    size_t queued=cetta_continuation_hub_length(&control.hub);
    CettaOwnedContinuation *caller_before=calloc(queued?queued:1u,sizeof(*caller_before));assert(caller_before);
    for(size_t i=0u;i<queued;i++)caller_before[i]=*cetta_continuation_hub_at(&control.hub,i);
    uint64_t paid=control.steps;
    space_add(mutate_read?&kb:&other,parse_one(&arena,empty_query?"(Missing 8)":"(Fact 8)"));
    prime_native_control_run(&control,empty_query?200u:20000u,observe,&observed);
    if(mutate_read) {
        CHECK(control.invalidated && !observed.count && !control.output.len);
        CHECK(!prime_native_control_closed(&control) && control.steps==paid);
        CHECK(!memcmp(&packet_before,packet,sizeof(*packet)));
        if(!empty_query)CHECK(!memcmp(&answer_before,&packet->items[0],sizeof(answer_before)));
        CHECK(!memcmp(&return_before,returned,sizeof(*returned)));
        if(empty_query)CHECK(!memcmp(&equation_before,returned->equation_search,sizeof(equation_before)));
        CHECK(!memcmp(&read_before,&control.query_reads[0],sizeof(read_before)));
        CHECK(!memcmp(&schedule_before,&control.hub.schedule,sizeof(schedule_before)));
        CHECK(cetta_continuation_hub_length(&control.hub)==queued);
        for(size_t i=0u;i<queued;i++)
            CHECK(!memcmp(&caller_before[i],cetta_continuation_hub_at(&control.hub,i),sizeof(caller_before[i])));
    } else {
        CHECK(!control.invalidated && prime_native_control_closed(&control)==!empty_query);
        CHECK(empty_query ? observed.count==0u :
            observed.count==1u && atom_eq(observed.value,atom_int(&arena,7)));
    }
    free(caller_before);
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;
    space_free(&space);space_free(&kb);space_free(&other);arena_free(&arena);
}

/* Own publication advances only its executing authority chain. A separately
 * suspended reader of the same space retains its external-interference guard. */
static void resume_frontier_case(unsigned mode) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena,NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    Bindings env; bindings_init(&env);
    PrimeNativeControl *control=calloc(1u,sizeof(*control)); assert(control);
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
    assert(prime_native_control_init(control,&space,&arena,
        parse_one(&arena,"(superpose (A B B))"),-1,true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,&env,NULL,NULL));
    Observed observed={0};
    prime_native_control_run(control,8u,observe,&observed);
    assert(!observed.count && cetta_continuation_hub_length(&control->hub)==3u);
    uint64_t paid=control->steps;
    CettaOwnedContinuation before[3];
    for(size_t i=0u;i<3u;i++) before[i]=*cetta_continuation_hub_at(&control->hub,i);
    PrimeNativeControl *unpublished=control;
    Atom *holder=NULL;
    CHECK(prime_native_control_admit_owned(&unpublished,&arena,&holder)==CETTA_CONTINUATION_READY);
    assert(!unpublished && holder);
    registry_bind(&registry,"&resume-held",holder);
    OutcomeSet out; outcome_set_init(&out);
    CHECK(prime_native_interaction_dispatch(&space,&arena,
        parse_one(&arena,"(interaction:choices &resume-held Resume 1)"),-1,&env,&out));
    assert(out.len==1u && out.items[0].atom->kind==ATOM_EXPR &&
        atom_is_symbol(out.items[0].atom->expr.elems[0],"InteractionChoices"));
    Atom *cursor=expr_arg(expr_arg(out.items[0].atom,4u),0u);
    PrimeInteractionDiscovery *owner=cetta_native_handle_get_owned(&context,"prime-space-discovery",cursor);
    assert(owner && owner->resume_frontier);
    registry_bind(&registry,"&resume-cursor",cursor);
    CHECK(control->leased && owner->resume_position==1u && owner->quanta==1u);
    CHECK(control->steps==paid && !control->resume_choice);
    CHECK(!prime_native_control_select(&space,&arena,holder,-1,3u,&policy,&out,&env,NULL));
    CHECK(!prime_native_interaction_dispatch(&space,&arena,
        parse_one(&arena,"(interaction:perform &resume-cursor 2 0)"),-1,&env,&out));
    CHECK(owner->resume_position==1u && control->steps==paid && !control->resume_choice);
    CHECK(prime_native_interaction_dispatch(&space,&arena,
        parse_one(&arena,"(interaction:choices &resume-cursor 1)"),-1,&env,&out));
    CHECK(owner->resume_position==2u && owner->quanta==2u && control->steps==paid);
    for(size_t i=0u;i<3u;i++)
        CHECK(!memcmp(&before[i],cetta_continuation_hub_at(&control->hub,i),sizeof(before[i])));
    if(mode==0u) {
        CHECK(prime_native_interaction_dispatch(&space,&arena,
            parse_one(&arena,"(interaction:perform &resume-cursor 2 0)"),-1,&env,&out));
        CHECK(!control->leased && !owner->resume_use.self && control->resume_choice &&
            control->resume_choice->occurrence==before[1].occurrence_id && control->steps==paid);
        for(size_t i=0u;i<3u;i++)
            CHECK(!memcmp(&before[i],cetta_continuation_hub_at(&control->hub,i),sizeof(before[i])));
        CHECK(!prime_native_interaction_dispatch(&space,&arena,
            parse_one(&arena,"(interaction:perform &resume-cursor 1 100)"),-1,&env,&out));
        prime_native_control_run(control,1u,observe,&observed);
        CHECK(control->steps==paid+1u && !control->resume_choice->occurrence);
        size_t siblings=0u, children=0u, selected=0u;
        for(size_t i=0u;i<cetta_continuation_hub_length(&control->hub);i++) {
            const CettaOwnedContinuation *item=cetta_continuation_hub_at(&control->hub,i);
            selected+=item->occurrence_id==before[1].occurrence_id;
            children+=item->parent_occurrence_id==before[1].occurrence_id;
            for(size_t j=0u;j<3u;j++) if(j!=1u && item->occurrence_id==before[j].occurrence_id) {
                CHECK(item->payload==before[j].payload); siblings++;
            }
        }
        CHECK(siblings==2u && !selected && children>0u);
        prime_native_control_run(control,10000u,observe,&observed);
        CHECK(prime_native_control_closed(control) && observed.count==3u);
        size_t as=0u, bs=0u;
        for(CettaCount i=0u;i<control->output.len;i++) {
            Atom *value=outcome_atom_materialize(control->driver.arena,&control->output.items[i]);
            as+=atom_is_symbol(value,"A"); bs+=atom_is_symbol(value,"B");
        }
        CHECK(as==1u && bs==2u);
    } else if(mode==2u) {
        CHECK(cetta_native_handle_close(&context,"prime-search",control->handle_id));
        CHECK(!prime_interaction_resume_current(owner));
        CHECK(!prime_native_interaction_dispatch(&space,&arena,
            parse_one(&arena,"(interaction:perform &resume-cursor 2 100)"),-1,&env,&out));
        CHECK(control->steps==paid);
    } else if(mode==3u) {
        space_add(&space,atom_symbol(&arena,"Outside"));
        CHECK(!prime_interaction_resume_current(owner));
        CHECK(!prime_native_interaction_dispatch(&space,&arena,
            parse_one(&arena,"(interaction:perform &resume-cursor 2 100)"),-1,&env,&out));
        CHECK(control->steps==paid && !control->resume_choice);
    }
    CHECK(prime_native_interaction_dispatch(&space,&arena,
        parse_one(&arena,"(interaction:dispose &resume-cursor)"),-1,&env,&out));
    if(mode==1u) {
        CHECK(!control->leased && control->steps==paid && !control->resume_choice);
        prime_native_control_run(control,10000u,observe,&observed);
        CHECK(prime_native_control_closed(control) && observed.count==3u);
    }
    outcome_set_free(&out); bindings_free(&env);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry=NULL; space_free(&space); arena_free(&arena);
}

/* Enter through the public snapshot API, then schedule its real retained
 * child from a different ordinary caller. The original source roots retire
 * before either discovery or resumption. */
/* Exercise public capture once, then retire all borrowed source owners.
 * Only the original public authority and copied return carrier survive. */
static PrimeNativeControl *owned_region_test_capture(CettaLibraryContext *context,
    Arena *surviving, const char *program, const char *expression,
    Atom **holder, Atom **observation) {
    Arena source,receiver;
    arena_init_detached(&source); arena_init_detached(&receiver);
    term_universe_set_persistent_arena(&context->term_universe,&source);
    Space original; space_init_with_universe(&original,&context->term_universe);
    Registry original_registry; registry_init(&original_registry); g_registry=&original_registry;
    registry_bind(&original_registry,"&self",atom_space(&source,&original));
    Atom **forms=NULL;
    int count=parse_metta_text(program,&source,&forms);
    assert(count>0);
    for(int i=0;i<count;i++) space_add(&original,forms[i]);
    free(forms);
    EvalOutcome outcome; eval_outcome_init(&outcome);
    Atom *invocation=parse_one(&source,expression);
    CHECK(prime_native_registry_observation_admits(&original,&receiver,invocation));
    CHECK(prime_native_region_baseline(context));
    /* The same policy node, placed in the program, is executable syntax. */
    Atom **parts=arena_alloc(&source,invocation->expr.len*sizeof(*parts));
    memcpy(parts,invocation->expr.elems,invocation->expr.len*sizeof(*parts));
    parts[invocation->expr.len-1u]=expr_arg(invocation,expr_nargs(invocation)-2u);
    Atom *aliased=atom_expr(&source,parts,invocation->expr.len);
    size_t handles=cetta_native_handle_count(context);
    CHECK(eval_prime_owned_region_outcome(&original,&receiver,&source,&original_registry,
        aliased,CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,
        &outcome)==CETTA_CONTINUATION_UNSUPPORTED);
    CHECK(cetta_native_handle_count(context)==handles && !outcome.prime_observation && !outcome.results.len);
    CettaContinuationStatus status=eval_prime_owned_region_outcome(&original,&receiver,
        &source,&original_registry,invocation,
        CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome);
    CHECK(!g_prime_owned_selection_request);
    if(status!=CETTA_CONTINUATION_READY)
        fprintf(stderr,"Owned capture: status=%u request=%s\n",(unsigned)status,expression);
    CHECK(status==CETTA_CONTINUATION_READY);
    CettaPrimeObservationState state;
    assert(eval_outcome_observation_state(&outcome,&state) && state.holder);
    *holder=atom_deep_copy(surviving,state.holder);
    if(observation) *observation=atom_deep_copy(surviving,state.projected);
    PrimeNativeControl *child=cetta_native_handle_get_owned(context,"prime-search",*holder);
    assert(child && child->owned_top_episode && child->owned_top_episode->owned_region);
    CHECK(!child->leased && child->owned_top_episode->owned_region->library->native_handle_registry==NULL);
    eval_outcome_free(&outcome);
    registry_free(&original_registry); space_free(&original); g_registry=NULL;
    term_universe_set_persistent_arena(&context->term_universe,surviving);
    arena_free(&source); arena_free(&receiver);
    return child;
}

/* Enter an ordinary weight call through its real external owner, retire the
 * captured roots, and separate source grants from checked row publication. */
static void owned_weighted_return_case(unsigned which, unsigned refusal, unsigned workers) {
    size_t failures_before=failures;
    static const char *queries[]={
        "(weight:where (weighted:run) 3)",
        "(weight:where (superpose ()) 3)",
        "(weight:where () 3)",
        "(weight:read (algebra 0 1 + *) (weighted:probe))",
        "(weight:where (let $x B (let $x (superpose (A A)) (Pair $x $x))) 3)",
        "(weight:where (superpose (A A B (weight:where Body 7 (admit unknown)))) 2)",
        "(weight:where (let $x (superpose (A B)) (lam z (Pair $x z))) 3)",
        "(weight:exact-rational? 7)",
        "(weight:where (let $x B (let $x (superpose (A A)) (Pair $x $x))) 3)"};
    static const size_t counts[]={3u,0u,1u,3u,2u,1u,2u,1u,0u};
    assert(which<sizeof(queries)/sizeof(*queries) && refusal<=6u);
    Arena source,receiver;arena_init_detached(&source);arena_init_detached(&receiver);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    CettaPrimeScopeProfile previous_profile=prime_scope_runtime_profile();
    CettaPrimeScopeProfile authored_profile={
        .ownership=which==8u?CETTA_PRIME_OWNERSHIP_MERCURY_IMPLICIT:CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH,
        .lifetime=CETTA_PRIME_LIFETIME_PER_CALL,.readout=CETTA_PRIME_READOUT_REFERENCE};
    prime_scope_runtime_profile_set(authored_profile);
    char threads[16];snprintf(threads,sizeof(threads),"%u",workers);
    assert(cetta_eval_session_record_generic_setting(&context.session,"num-threads",
        CETTA_EVAL_OPTION_VALUE_INT,threads,workers));
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    term_universe_set_persistent_arena(&context.term_universe,&source);
    Space space;space_init_with_universe(&space,&context.term_universe);
    Registry registry;registry_init(&registry);g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&source,&space));
    Atom **forms=NULL;int form_count=parse_metta_text(
        "(=w (weighted:probe) A 2) (=w (weighted:probe) A 2) (=w (weighted:probe) B 5) "
        "(= (weighted:run) (weighted:work 8 A)) (= (weighted:run) (weighted:work 8 B)) "
        "(= (weighted:run) (weighted:work 8 B)) "
        "(= (weighted:work $n $answer) (if (== $n 0) $answer (weighted:work (- $n 1) $answer)))",
        &source,&forms);
    assert(form_count==7);
    for(int i=0;i<form_count;i++) {
        Atom *formed=prime_semantics_elaborate_form(&source,forms[i]);
        assert(formed);space_add(&space,formed);
    }
    free(forms);
    EvalOutcome outcome;eval_outcome_init(&outcome);
    if(which==0u) {
        /* This snapshot profile declines computed heads even in a choice
         * container. Ordinary evaluation retains its existing grammar. */
        CHECK(eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,
            parse_one(&source,"(weight:where (superpose ((weighted:work 8 A) (weighted:work 8 B))) 3)"),
            CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome)
                ==CETTA_CONTINUATION_UNSUPPORTED);
        CHECK(!outcome.prime_observation && !outcome.results.len && !cetta_native_handle_count(&context));
        Atom *tag=atom_internal_tag(&source,CETTA_INTERNAL_TAG_PRIME_OWN);
        Atom *name=atom_var_with_spelling(&source,symbol_intern_cstr(g_symbols,"owned-metadata"),var_id_from_base(124901u));
        Atom *own=atom_expr3(&source,tag,atom_symbol(&source,"lexical-fresh/per-call/reference"),name);
        Atom *malformed=atom_expr3(&source,tag,atom_symbol(&source,"lexical-fresh/per-call/reference"),
            atom_int(&source,7));
        Atom *quoted_own=atom_expr2(&source,atom_symbol(&source,"quote"),own);
        Atom *quoted_tag=atom_expr2(&source,atom_symbol(&source,"quote"),tag);
        Atom *body=atom_expr3(&source,atom_symbol(&source,"Pair"),quoted_own,quoted_tag);
        Atom *shared=atom_expr3(&source,atom_symbol(&source,"weight:where"),body,atom_int(&source,3));
        /* The same private tag first occurs as validated metadata, then as
         * quoted data. DAG memo reuse must not grant the second role. */
        CHECK(prime_native_control_admits(&space,&receiver,shared,true));
        CHECK(eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,shared,
            CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome)
                ==CETTA_CONTINUATION_UNSUPPORTED);
        Atom *reverse=atom_expr3(&source,atom_symbol(&source,"weight:where"),
            atom_expr3(&source,atom_symbol(&source,"Pair"),quoted_tag,quoted_own),atom_int(&source,3));
        CHECK(eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,reverse,
            CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome)
                ==CETTA_CONTINUATION_UNSUPPORTED);
        Atom *bad_let_parts[]={atom_symbol(&source,"let"),name,atom_symbol(&source,"A"),name,malformed};
        Atom *bad=atom_expr3(&source,atom_symbol(&source,"weight:where"),
            atom_expr(&source,bad_let_parts,5u),atom_int(&source,3));
        CHECK(eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,bad,
            CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome)
                ==CETTA_CONTINUATION_UNSUPPORTED);
        Atom *other=atom_expr3(&source,atom_symbol(&source,"weight:where"),
            atom_expr2(&source,atom_symbol(&source,"quote"),
                atom_internal_tag(&source,CETTA_INTERNAL_TAG_PETTA_NO_RESULT)),atom_int(&source,3));
        CHECK(eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,other,
            CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,100000u,16u*1024u*1024u,&outcome)
                ==CETTA_CONTINUATION_UNSUPPORTED);
        CHECK(!outcome.prime_observation && !outcome.results.len && !cetta_native_handle_count(&context));
    }
    /* The reader resolves binding identities before a term enters evaluation.
     * Capture must preserve that choice, including relational reuse of $x. */
    Atom *expression=prime_semantics_elaborate_form(&source,parse_one(&source,queries[which]));
    assert(expression);
    CettaContinuationStatus status=eval_prime_owned_region_outcome(&space,&receiver,&source,&registry,
        expression,CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1,
        100000u,16u*1024u*1024u,&outcome);
    if(status!=CETTA_CONTINUATION_DEFERRED)
        fprintf(stderr,"weighted owned entry %u/%u/%u status=%u\n",which,refusal,workers,(unsigned)status);
    assert(status==CETTA_CONTINUATION_DEFERRED && outcome.prime_observation);
    PrimeNativeOwnedRequest *request=&outcome.prime_observation->top.owned;
    PrimeNativeAcquiredOperation *operation=request->acquired;
    assert(operation && operation->top_request==request);
    PrimeNativeControl *control=cetta_native_handle_use_resource(&operation->use);
    assert(control && control->owned_top_episode && control->owned_top_episode->owned_region);
    CettaNativeHandleUse inspection={0};
    assert(cetta_native_handle_use_fork_lifetime(&inspection,&operation->use));
    CHECK(control->steps==0u && !outcome.results.len && !operation->producer_complete);
    CHECK(operation->owns_lease && control->leased && operation->use.self==&operation->use);
    CHECK(control->owned_top_episode->owned_region->library->native_handle_registry==NULL);
    CHECK(request->return_mode==PRIME_NATIVE_RETURN_WEIGHTED_CALL && request->origin_authority==NULL);
    for(unsigned zero=0u;zero<2u;zero++) {
        CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_DEFERRED);
        CHECK(control->steps==0u && !operation->producer_complete && !outcome.results.len);
    }
    CHECK(eval_outcome_resume_observation(&outcome,1u)==CETTA_CONTINUATION_DEFERRED);
    CHECK(control->steps<=1u && !operation->producer_complete && !outcome.results.len);
    uint64_t initial=control->steps, id=control->handle_id;
    const void *services=control->owned_top_episode->owned_region->services;
    Atom *holder=atom_deep_copy(&receiver,request->holder);
    /* Neither the original syntax, world nor registry supplies a hidden
     * lifetime to the suspended source. */
    g_registry=NULL;registry_free(&registry);space_free(&space);
    term_universe_set_persistent_arena(&context.term_universe,&receiver);arena_free(&source);
    if(refusal==4u) {
        eval_set_library_context(NULL);cetta_library_context_free(&context);
        cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
        eval_set_library_context(&context);
        CHECK(!cetta_native_handle_use_is_current(&operation->use));
        CettaNativeHandleUse denied={0};
        CHECK(!cetta_native_handle_acquire_owned(&context,"prime-search",holder,
            CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&denied));
    }
    bool open=which==5u, fault=refusal>0u && refusal<4u && counts[which]>0u;
    if(fault) {
        if(open) {
            br_weight_copy_slot=&operation->value_copy;
            br_weight_copy_refusals=1u;br_weight_copy_skip=refusal==2u?1u:0u;
        } else if(refusal==2u) {
            br_weight_promote_owner=&receiver;br_weight_promote_refusals=1u;
        } else {
            br_weight_clone_source=&operation->pending.items[0].env;br_weight_clone_refusals=1u;
        }
    }
    br_return_worker_tracking=true;br_return_worker_waves=br_return_worker_tasks=0u;
    status=eval_outcome_resume_observation(&outcome,UINT64_MAX);
    br_return_worker_tracking=false;
    CHECK(control->steps>initial && prime_native_control_closed(control)!=open);
    CHECK(request->acquired ? operation->producer_complete : outcome.prime_observation->top.committed);
    if(request->acquired) CHECK(operation->closed_return!=open);
    CHECK(control->handle_id==id);
    CHECK(control->owned_top_episode->owned_region->services==services);
    if(fault) {
        CHECK(status==CETTA_CONTINUATION_CAPACITY && !outcome.results.len);
        CHECK(!br_weight_copy_refusals && !br_weight_clone_refusals && !br_weight_promote_refusals);
        CHECK(operation->value_cursor==(open && refusal==2u?1u:0u));
        CHECK(operation->pending.len==(!open && refusal==2u?0u:1u));
        uint64_t paid=control->steps, remaining=operation->remaining;
        size_t accepted=operation->accepted_count, cursor=operation->value_cursor;
        Atom *prepared=open?operation->reified:operation->pending.len?operation->pending.items[0].atom:NULL;
        /* Repeated refusal reuses the exact accepted source interval. */
        if(open) br_weight_copy_refusals=1u;
        else if(refusal==2u) br_weight_promote_refusals=1u;
        else br_weight_clone_refusals=1u;
        CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_CAPACITY);
        CHECK(control->steps==paid && operation->remaining==remaining && operation->accepted_count==accepted);
        CHECK(operation->value_cursor==cursor && !outcome.results.len);
        CHECK((open?operation->reified:operation->pending.len?operation->pending.items[0].atom:NULL)==prepared);
        if(refusal==3u) {
            CHECK(cetta_native_handle_close(&context,"prime-search",id));
            eval_outcome_free(&outcome);
            CHECK(cetta_native_handle_count(&context)==0u && !control->leased);
            goto cleanup;
        }
    }
    if(refusal>=5u) {
        assert(which==0u && operation->closed_return && outcome.results.len==1u);
        uint64_t paid=control->steps,remaining=operation->remaining;
        size_t cursor=operation->value_cursor,accepted=operation->accepted_count;
        Atom *first=outcome.results.items[0],*prepared=NULL;
        CHECK(cursor==1u && accepted==3u);
        for(unsigned attempt=0u;attempt<2u;attempt++) {
            if(refusal==5u) {
                br_weight_promote_owner=&receiver;br_weight_promote_refusals=1u;
            } else {
                br_weight_clone_source=&operation->pending.items[0].env;br_weight_clone_refusals=1u;
            }
            CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_CAPACITY);
            CHECK(control->steps==paid && operation->remaining==remaining &&
                operation->accepted_count==accepted && operation->value_cursor==cursor);
            CHECK(outcome.results.len==1u && outcome.results.items[0]==first && !control->failed);
            CHECK(!br_weight_promote_refusals && !br_weight_clone_refusals);
            CHECK(operation->pending.len==(refusal==5u?0u:1u));
            if(refusal==6u) {
                if(attempt) CHECK(operation->pending.items[0].atom==prepared);
                prepared=operation->pending.items[0].atom;
            }
        }
    }
    for(unsigned row=0u;row<8u && !outcome.prime_observation->top.committed;row++) {
        uint64_t paid=control->steps;
        status=eval_outcome_resume_observation(&outcome,0u);
        CHECK(control->steps==paid && control->handle_id==id);
    }
    CHECK(status==CETTA_CONTINUATION_READY && outcome.prime_observation->top.committed);
    CHECK(outcome.results.len==counts[which] && outcome.completion==CETTA_EVAL_COMPLETE);
    CHECK(!control->leased);
    if(which==0u) {
        size_t as=0u,bs=0u;
        for(CettaCount i=0u;i<outcome.results.len;i++) {
            as+=atom_is_symbol(outcome.results.items[i],"A");bs+=atom_is_symbol(outcome.results.items[i],"B");
        }
        CHECK(as==1u && bs==2u);
        if(workers>1u && refusal!=4u) CHECK(br_return_worker_waves>0u && br_return_worker_tasks>0u);
    } else if(which==2u) CHECK(atom_eq(outcome.results.items[0],parse_one(&receiver,"()")));
    else if(which==3u) {
        size_t as=0u,bs=0u;
        for(CettaCount i=0u;i<outcome.results.len;i++) {
            Atom *value=outcome.results.items[i];
            CHECK(value && value->kind==ATOM_EXPR && value->expr.len==4u &&
                atom_is_symbol(value->expr.elems[0],"Weighted"));
            bool a=atom_is_symbol(expr_arg(value,0u),"A");as+=a;bs+=!a;
            CHECK(atom_eq(expr_arg(value,1u),atom_int(&receiver,a?2:5)));
            Atom *provenance=expr_arg(value,2u);
            CHECK(provenance && provenance->kind==ATOM_EXPR && provenance->expr.len==2u &&
                atom_is_symbol(provenance->expr.elems[0],"provenance"));
        }
        CHECK(as==2u && bs==1u);
    } else if(which==4u) {
        for(CettaCount i=0u;i<outcome.results.len;i++)
            CHECK(atom_eq(outcome.results.items[i],parse_one(&receiver,"(Pair A A)")));
    } else if(open) {
        Atom *record=outcome.results.items[0];
        CHECK(record && record->kind==ATOM_EXPR && record->expr.len>1u &&
            atom_is_symbol(record->expr.elems[0],"Observation") &&
            atom_eq(expr_arg(record,0u),parse_one(&receiver,"(A A B)")));
        CHECK(!prime_native_control_closed(control) && prime_native_control_waiting_verdict(control));
    } else if(which==6u) {
        Space caller;space_init_with_universe(&caller,&context.term_universe);
        size_t as=0u,bs=0u;uint64_t paid=control->steps;
        for(CettaCount i=0u;i<outcome.results.len;i++) {
            OutcomeSet applied;outcome_set_init_with_owner(&applied,&receiver);
            metta_call(&caller,&receiver,atom_expr2(&receiver,outcome.results.items[i],
                atom_symbol(&receiver,"Z")),NULL,-1,false,&applied);
            CHECK(applied.len==1u);
            for(CettaCount j=0u;j<applied.len;j++) {
                Atom *value=outcome_observe_top_value(&receiver,&applied.items[j]);
                as+=atom_eq(value,parse_one(&receiver,"(Pair A Z)"));
                bs+=atom_eq(value,parse_one(&receiver,"(Pair B Z)"));
            }
            outcome_set_free(&applied);
        }
        CHECK(as==1u && bs==1u && control->steps==paid);
        space_free(&caller);
    } else if(which==7u) CHECK(is_true_atom(outcome.results.items[0]));
    if(failures!=failures_before) {
        fprintf(stderr,"weighted case %u refusal %u workers %u: status=%u completion=%u rows=%u\n",
            which,refusal,workers,(unsigned)status,(unsigned)outcome.completion,(unsigned)outcome.results.len);
        for(CettaCount i=0u;i<outcome.results.len;i++) {
            atom_print(outcome.results.items[i],stderr);fputc('\n',stderr);
        }
    }
    eval_outcome_free(&outcome);
cleanup:
    CHECK(cetta_native_handle_use_release(&inspection));
    br_weight_copy_slot=NULL;br_weight_copy_session=NULL;br_weight_copy_refusals=br_weight_copy_skip=0u;
    br_weight_clone_source=NULL;br_weight_clone_refusals=0u;
    br_weight_promote_owner=NULL;br_weight_promote_refusals=0u;
    prime_scope_runtime_profile_set(previous_profile);
    eval_set_library_context(NULL);cetta_library_context_free(&context);arena_free(&receiver);
}

static void owned_region_resume_case(unsigned mode, bool recording) {
    Arena arena; arena_init_detached(&arena);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    char expression[320];
    snprintf(expression,sizeof(expression),"(cost:run (search-policy (control FIFO (budget 0) %s)) %s)",
        recording?"(history (selections 512) (nodes 64) (bytes 32768) (owners 512))":"",
        mode==3u?"(resume-probe (+ 40 2))":"(resume-probe)");
    Atom *holder=NULL;
    PrimeNativeControl *child=owned_region_test_capture(&context,&arena,mode==3u?
        "(= (resume-probe $x) (Pair $x 1)) (= (resume-probe $x) (Pair $x 2)) "
        "(= (resume-probe $x) (Pair $x 2))":
        "(= (resume-probe) (superpose (A B B)))",expression,&holder,NULL);
    PrimeNativeOwnedRegion *region=child->owned_top_episode->owned_region;
    uint64_t paid=child->steps;
    CHECK(paid==0u);

    Space caller; space_init_with_universe(&caller,&context.term_universe);
    Registry registry; registry_init(&registry); g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&caller));
    registry_bind(&registry,"&region-held",holder);
    Bindings env; bindings_init(&env);
    OutcomeSet out; outcome_set_init(&out);
    CHECK(prime_native_interaction_dispatch(&caller,&arena,
        parse_one(&arena,"(interaction:choices &region-held Resume 1)"),-1,&env,&out));
    assert(out.len==1u && atom_is_symbol(out.items[0].atom->expr.elems[0],"InteractionChoices"));
    Atom *cursor=expr_arg(expr_arg(out.items[0].atom,4u),0u);
    PrimeInteractionDiscovery *owner=cetta_native_handle_get_owned(&context,"prime-space-discovery",cursor);
    assert(owner && owner->resume_frontier && owner->resume_position==1u);
    registry_bind(&registry,"&region-cursor",cursor);
    CHECK(child->leased && child->steps==paid && !child->resume_choice);
    CHECK(region->library->native_handle_registry==NULL && g_library_context==&context && g_registry==&registry);
    struct CettaNativeHandleOwner *issued=owner->resume_use.owner;
    Atom *take=parse_one(&arena,mode==1u
        ? "(interaction:perform &region-cursor 1 0)"
        : "(interaction:perform &region-cursor 1 1000)");
    /* A raw caller has no registered receiver frame. Refusal keeps the
     * exact cursor use and source queue, rather than losing its take. */
    CHECK(prime_native_interaction_dispatch(&caller,&arena,take,-1,&env,&out));
    CHECK(out.len==2u && out.items[1].atom==take);
    CHECK(owner->resume_use.self==&owner->resume_use && owner->resume_use.owner==issued);
    CHECK(child->leased && child->steps==paid && !child->resume_choice);
    if(mode==2u) {
        CHECK(cetta_native_handle_close(&context,"prime-search",child->handle_id));
        CHECK(!prime_interaction_resume_current(owner));
        CHECK(!prime_native_interaction_dispatch(&caller,&arena,take,-1,&env,&out));
        CHECK(child->steps==paid && owner->resume_use.owner==issued);
    } else {
        unsigned phases=mode==1u || mode==4u?2u:1u;
        for(unsigned phase=0u;phase<phases;phase++) {
            Atom *request=phase?parse_one(&arena,"(interaction:perform &region-held 1000)"):take;
            PrimeNativeControl parent;
            CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
            assert(prime_native_control_init(&parent,&caller,&arena,request,-1,true,
                (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
                &policy,&env,NULL,NULL));
            Observed observed={0};
            PrimeEvalStackFrame *nested=NULL;
            for(unsigned slice=0u;slice<1000u && !nested && !prime_native_control_closed(&parent);slice++) {
                prime_native_control_run(&parent,1u,observe,&observed);
                for(PrimeEvalStackFrame *frame=parent.frames;frame;frame=frame->control_next)
                    if(frame->kind==PRIME_EVAL_STACK_FRAME_CONTROL_NESTED &&
                        frame->control_nested==child) {nested=frame;break;}
            }
            if(!nested) fprintf(stderr,"Owned Resume missing frame: mode=%u recording=%u phase=%u closed=%u answers=%zu childsteps=%"PRIu64"\n",
                mode,recording,phase,prime_native_control_closed(&parent),observed.count,child->steps);
            assert(nested && nested->control_nested_operation);
            PrimeNativeAcquiredOperation *operation=nested->control_nested_operation;
            CHECK(operation->use.owner==issued && operation->use.self==&operation->use);
            CHECK(operation->use.mode==CETTA_NATIVE_HANDLE_USE_EXCLUSIVE && operation->owns_lease);
            CHECK(child->leased && child->steps==paid);
            if(!phase) CHECK(!owner->resume_use.self);
            CHECK(operation->parent_control==&parent && operation->parent_frame==nested);
            CettaNativeHandleUse competing={0};
            CHECK(!cetta_native_handle_acquire_owned(&context,"prime-search",holder,
                CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&competing));
            if(!phase) {
                CHECK(prime_native_interaction_dispatch(&caller,&arena,
                    parse_one(&arena,"(interaction:dispose &region-cursor)"),-1,&env,&out));
                CHECK(child->leased && child->steps==paid);
            }
            if(mode==4u && !phase) {
                release_stack_control(&parent);
                CHECK(!child->leased && child->steps==paid);
                CHECK(child->resume_choice && child->resume_choice->occurrence);
                continue;
            }
            /* Requested history is a potentially large data return. Its
             * caller-side structural readout uses the same resumable parent
             * rather than assuming it fits in one arbitrary grant. */
            for(unsigned slice=0u;slice<200u && !prime_native_control_closed(&parent) &&
                    !parent.failed && !parent.driver.capacity_blocked;slice++)
                prime_native_control_run(&parent,10000u,observe,&observed);
            if(!prime_native_control_closed(&parent) || observed.count!=1u || child->leased) {
                const char *refusal="None";
                for(PrimeEvalStackFrame *frame=parent.frames;frame;frame=frame->control_next)
                    if(frame->kind==PRIME_EVAL_STACK_FRAME_CONTROL_NESTED && frame->control_nested_operation &&
                        frame->control_nested_operation->return_refusal)
                        refusal=frame->control_nested_operation->return_refusal;
                fprintf(stderr,"Owned Resume mode=%u recording=%u phase=%u: parent=%u child=%u "
                    "answers=%zu work=%"PRIu64" leased=%u refusal=%s\n",mode,recording,phase,
                    (unsigned)parent.completion,(unsigned)child->completion,observed.count,child->steps,
                    child->leased,refusal);
            }
            CHECK(prime_native_control_closed(&parent) && observed.count==1u && !child->leased);
            assert(observed.value && atom_is_symbol(observed.value->expr.elems[0],"Observation"));
            Atom *values=expr_arg(observed.value,0u);
            if(mode==1u && !phase) {
                CHECK(values->kind==ATOM_EXPR && values->expr.len==0u && child->steps==paid);
                CHECK(child->resume_choice && child->resume_choice->occurrence);
            } else {
                CHECK(atom_eq(values,parse_one(&arena,mode==3u?
                    "((Pair 42 1) (Pair 42 2) (Pair 42 2))":"(A B B)")));
                CHECK(prime_native_control_closed(child) && child->steps>paid);
                CHECK(child->resume_choice && !child->resume_choice->occurrence);
            }
            CHECK(region->library->native_handle_registry==NULL && g_library_context==&context && g_registry==&registry);
            release_stack_control(&parent);
        }
    }
    outcome_set_free(&out); bindings_free(&env);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry=NULL; space_free(&caller); arena_free(&arena);
}

/* The caller runs the actual registered nested frame, in small producer
 * slices. Copy its result before releasing that caller's arena. */
static Atom *owned_region_test_request(Space *space, Arena *arena, Bindings *env,
    PrimeNativeControl *child, const char *expression, unsigned workers, bool accepted) {
    PrimeNativeControl parent;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=workers};
    assert(prime_native_control_init(&parent,space,arena,parse_one(arena,expression),-1,true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,env,NULL,NULL));
    Observed observed={0};
    PrimeEvalStackFrame *nested=NULL;
    for(unsigned slice=0u;slice<1000u && !nested && !prime_native_control_closed(&parent);slice++) {
        prime_native_control_run(&parent,1u,observe,&observed);
        for(PrimeEvalStackFrame *frame=parent.frames;frame;frame=frame->control_next)
            if(frame->kind==PRIME_EVAL_STACK_FRAME_CONTROL_NESTED && frame->control_nested==child) {
                nested=frame; break;
            }
    }
    /* A request can finish before the harness sees a suspended frame.
     * It must still return exactly one Observation and release its lease.
     * Callers below check exact answers, paid work and retained occurrences. */
    if(accepted && !nested) {
        CHECK(prime_native_control_closed(&parent) && !parent.failed &&
              !parent.driver.capacity_blocked && observed.count==1u && !child->leased);
        CHECK(observed.value && observed.value->kind==ATOM_EXPR &&
              atom_is_symbol(observed.value->expr.elems[0],"Observation"));
    } else CHECK((nested!=NULL)==accepted);
    if(nested) {
        assert(nested->control_nested_operation);
        PrimeNativeAcquiredOperation *operation=nested->control_nested_operation;
        CHECK(operation->use.self==&operation->use && operation->owns_lease && child->leased);
        CHECK(operation->parent_frame==nested && operation->parent_control==&parent);
    }
    for(unsigned slice=0u;slice<200u && !prime_native_control_closed(&parent) &&
            !parent.failed && !parent.driver.capacity_blocked;slice++)
        prime_native_control_run(&parent,10000u,observe,&observed);
    CHECK(prime_native_control_closed(&parent) && observed.count==1u && !child->leased);
    assert(observed.value);
    Atom *answer=atom_deep_copy(arena,observed.value);
    CHECK(atom_is_symbol(answer->expr.elems[0],"Observation")==accepted);
    release_stack_control(&parent);
    return answer;
}

static void owned_region_demand_case(unsigned entry, bool recording) {
    Arena arena; arena_init_detached(&arena);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    const char *history=recording?
        "(history (selections 512) (nodes 64) (bytes 32768) (owners 512))":"";
    char expression[384];
    if(entry<2u) snprintf(expression,sizeof(expression),
        "(select %u (search-policy (control FIFO (retain True) (workers 1) (budget 1000) %s)) (demand-probe (+ 40 2)))",
        entry,history);
    else snprintf(expression,sizeof(expression),
        "(%s (search-policy (control FIFO (retain True) (workers 1) (budget 1000) %s)) (demand-probe (+ 40 2)))",
        entry==2u?"once":"select",history);
    Atom *holder=NULL,*initial=NULL;
    PrimeNativeControl *child=owned_region_test_capture(&context,&arena,
        "(= (demand-probe $x) (Pair $x 1)) (= (demand-probe $x) (Pair $x 2)) "
        "(= (demand-probe $x) (Pair $x 2))",expression,&holder,&initial);
    CHECK(!child->cost_enabled && child->event_cost.identity==0u);
    CHECK(!prime_native_control_closed(child) &&
        cetta_control_plan_observation_satisfied(&child->hub.plan,child->delivered_count));
    CHECK(atom_eq(expr_arg(initial,0u),parse_one(&arena,entry?"((Pair 42 1))":"()")));
    CHECK(is_true_atom(expr_arg(expr_arg(initial,1u),0u)) &&
        is_false_atom(expr_arg(expr_arg(initial,2u),0u)));
    CHECK(child->delivered_count==(entry?1u:0u));
    if(!entry) CHECK(child->steps==0u && cetta_continuation_hub_length(&child->hub)>0u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(child->history.spec.enabled==recording);
    if(!recording) CHECK(!child->history.group && !child->history.entries);
#endif
    Space caller; space_init_with_universe(&caller,&context.term_universe);
    Registry registry; registry_init(&registry); g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&caller));
    registry_bind(&registry,"&demand-held",holder);
    Bindings env; bindings_init(&env);
    unsigned workers=1u+(entry%2u);
    if(!entry) {
        OutcomeSet out; outcome_set_init(&out);
        CHECK(prime_native_interaction_dispatch(&caller,&arena,
            parse_one(&arena,"(interaction:choices &demand-held Resume 1)"),-1,&env,&out));
        assert(out.len==1u);
        Atom *cursor=expr_arg(expr_arg(out.items[0].atom,4u),0u);
        registry_bind(&registry,"&demand-cursor",cursor);
        Atom *zero=owned_region_test_request(&caller,&arena,&env,child,
            "(interaction:perform &demand-cursor 1 0)",workers,true);
        CHECK(atom_eq(expr_arg(zero,0u),parse_one(&arena,"()")) && child->steps==0u);
        CHECK(child->resume_choice && child->resume_choice->occurrence);
        uint64_t occurrence=child->resume_choice->occurrence;
        zero=owned_region_test_request(&caller,&arena,&env,child,
            "(select 0 (search-policy (control FIFO (budget 1000) (retain True))) &demand-held)",workers,true);
        CHECK(child->steps==0u && child->resume_choice->occurrence==occurrence);
        CHECK(is_true_atom(expr_arg(expr_arg(zero,1u),0u)) && !prime_native_control_closed(child));
        CHECK(prime_native_interaction_dispatch(&caller,&arena,
            parse_one(&arena,"(interaction:dispose &demand-cursor)"),-1,&env,&out));
        outcome_set_free(&out);
    }
    uint64_t paid=child->steps;
    CettaControlPlan before=child->hub.plan;
    (void)owned_region_test_request(&caller,&arena,&env,child,
        "(select 1 (search-policy (control FIFO (costs True) (retain True) (budget 1000))) &demand-held)",workers,false);
    CHECK(child->steps==paid && child->hub.plan.prefix_limit==before.prefix_limit &&
        child->hub.plan.readout==before.readout && !child->cost_enabled);
    if(!entry) {
        Atom *first=owned_region_test_request(&caller,&arena,&env,child,
            "(select 1 (search-policy (control FIFO (retain True) (budget 1000))) &demand-held)",workers,true);
        CHECK(atom_eq(expr_arg(first,0u),parse_one(&arena,"((Pair 42 1))")));
        CHECK(child->delivered_count==1u && !prime_native_control_closed(child));
        CHECK(child->resume_choice && !child->resume_choice->occurrence);
    }
    paid=child->steps;
    Atom *same=owned_region_test_request(&caller,&arena,&env,child,
        "(select 1 (search-policy (control FIFO (retain True) (budget 1000))) &demand-held)",workers,true);
    CHECK(atom_eq(expr_arg(same,0u),parse_one(&arena,"()")) && child->steps==paid);
    CHECK(is_true_atom(expr_arg(expr_arg(same,1u),0u)) && !prime_native_control_closed(child));
    Atom *next=owned_region_test_request(&caller,&arena,&env,child,
        "(select 1 (search-policy (control FIFO (demand additional) (retain True) (budget 1000))) &demand-held)",workers,true);
    CHECK(atom_eq(expr_arg(next,0u),parse_one(&arena,"((Pair 42 2))")));
    CHECK(child->delivered_count==2u && child->hub.plan.prefix_limit==2u && !prime_native_control_closed(child));
    Atom *rest=owned_region_test_request(&caller,&arena,&env,child,
        "(select 0 (search-policy (control FIFO (demand all) (retain True) (budget 1000))) &demand-held)",workers,true);
    CHECK(atom_eq(expr_arg(rest,0u),parse_one(&arena,"((Pair 42 2))")));
    CHECK(child->delivered_count==3u && prime_native_control_closed(child));
    CHECK(is_true_atom(expr_arg(expr_arg(rest,1u),0u)) && is_true_atom(expr_arg(expr_arg(rest,2u),0u)));
    paid=child->steps;
    Atom *shortfall=owned_region_test_request(&caller,&arena,&env,child,
        "(select 4 (search-policy (control FIFO (retain True) (budget 1000))) &demand-held)",workers,true);
    CHECK(atom_eq(expr_arg(shortfall,0u),parse_one(&arena,"()")) && child->steps==paid);
    CHECK(is_false_atom(expr_arg(expr_arg(shortfall,1u),0u)) &&
        is_true_atom(expr_arg(expr_arg(shortfall,2u),0u)));
    CHECK(!child->cost_enabled && child->event_cost.identity==0u);
    CHECK(child->owned_top_episode->owned_region->library->native_handle_registry==NULL &&
        g_library_context==&context && g_registry==&registry);
    bindings_free(&env);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry=NULL; space_free(&caller); arena_free(&arena);
}

static void own_query_publication_guard_case(unsigned change) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space,facts,other;space_init(&space);space_init(&facts);space_init(&other);
    Registry registry;registry_init(&registry);g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    registry_bind(&registry,"&facts",atom_space(&arena,&facts));
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    term_universe_set_persistent_arena(&context.term_universe,&arena);
    space_add(&facts,parse_one(&arena,"(Fact A)"));
    space_add(&facts,parse_one(&arena,"(Fact A)"));
    space_add(&space,parse_one(&arena,
        "(= (own-query-write) (match &facts (Fact $x) (chain (add-atom &facts (Seen $x)) $ignored $x)))"));
    PrimeNativeControl control,peer;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.workers=1u};
    CettaObservationDemand demand={.completion=CETTA_OBSERVATION_COMPLETE_BAG};
    Observed observed={0},peer_observed={0};
    if(change==2u) {
        assert(prime_native_control_init(&peer,&space,&arena,
            parse_one(&arena,"(match &facts (Fact $x) (Peer $x))"),-1,false,demand,&policy,NULL,NULL,NULL));
        for(size_t i=0u;i<1000u && !peer.query_read_count;i++)
            prime_native_control_run(&peer,1u,observe,&peer_observed);
        assert(peer.query_read_count==1u);
        CHECK(!peer_observed.count && space_read_token_is_current(peer.query_reads[0]));
    }
    assert(prime_native_control_init(&control,&space,&arena,
        parse_one(&arena,"(own-query-write)"),-1,false,demand,&policy,NULL,NULL,NULL));
    for(size_t i=0u;i<1000u && !control.query_read_count;i++)
        prime_native_control_run(&control,1u,observe,&observed);
    assert(control.query_read_count==1u);
    CHECK(!observed.count && !space_contains_exact(&facts,parse_one(&arena,"(Seen A)")));
    uint64_t paid=control.steps;
    SpaceReadToken original=control.query_reads[0];
    if(change==1u || change==3u)
        space_add(change==1u?&facts:&other,parse_one(&arena,"(Fact Outside)"));
    prime_native_control_run(&control,10000u,observe,&observed);
    if(change==1u) {
        CHECK(control.invalidated && control.steps==paid && !observed.count);
        CHECK(!space_contains_exact(&facts,parse_one(&arena,"(Seen A)")));
        CHECK(control.query_reads[0].revision==original.revision);
    } else {
        CHECK(!control.invalidated && prime_native_control_closed(&control));
        CHECK(observed.count==2u && atom_eq(observed.value,atom_symbol(&arena,"A")));
        CHECK(space_length64(&facts)==4u);
        CHECK(control.query_reads[0].revision==original.revision+2u);
        CHECK(space_read_token_is_current(control.query_reads[0]));
        uint64_t completed=control.steps;
        prime_native_control_run(&control,100u,observe,&observed);
        CHECK(control.steps==completed && observed.count==2u && space_length64(&facts)==4u);
    }
    if(change==2u) {
        uint64_t peer_paid=peer.steps;
        prime_native_control_run(&peer,10000u,observe,&peer_observed);
        CHECK(peer.invalidated && peer.steps==peer_paid && !peer_observed.count);
        CHECK(peer.query_reads[0].revision==original.revision);
        release_stack_control(&peer);
    }
    release_stack_control(&control);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
    registry_free(&registry);g_registry=NULL;
    space_free(&space);space_free(&facts);space_free(&other);arena_free(&arena);
}

/* Grounded space observers must join the same retained dependency manifest as
 * evaluator-level match and collection reads. An unrelated mutation remains
 * admissible; changing the observed space invalidates before more work. */
static void declared_space_read_dependency_case(unsigned operation,bool mutate_read) {
    static const char *calls[]={
        "(size &facts)",
        "(_minimal-space-contains-exact &facts (Fact A))",
        "(_minimal-space-revision &facts)",
        "(space-len &facts)",
        "(space-get &facts 0)",
        "(space-peek &facts)",
        "(count-atoms &facts)",
    };
    assert(operation<sizeof(calls)/sizeof(*calls));
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space,facts,other;space_init(&space);space_init(&facts);space_init(&other);
    facts.kind=SPACE_KIND_STACK;
    Registry registry;registry_init(&registry);g_registry=&registry;
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    registry_bind(&registry,"&facts",atom_space(&arena,&facts));
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    term_universe_set_persistent_arena(&context.term_universe,&arena);
    space_add(&facts,parse_one(&arena,"(Fact A)"));
    space_add(&space,parse_one(&arena,
        "(= (manifest-hold $value) (manifest-hold $value))"));
    char expression[256];
    int written=snprintf(expression,sizeof(expression),
        "(chain %s $value (manifest-hold $value))",calls[operation]);
    assert(written>0 && (size_t)written<sizeof(expression));
    PrimeNativeControl control;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.workers=1u};
    CettaObservationDemand demand={.completion=CETTA_OBSERVATION_COMPLETE_BAG};
    assert(prime_native_control_init(&control,&space,&arena,
        parse_one(&arena,expression),-1,false,demand,&policy,NULL,NULL,NULL));
    Observed observed={0};
    for(unsigned tick=0u;tick<10000u && !control.query_read_count;tick++)
        prime_native_control_run(&control,1u,observe,&observed);
    CHECK(control.query_read_count==1u && control.query_reads[0].space==&facts &&
        space_read_token_is_current(control.query_reads[0]));
    CHECK(!control.invalidated && !control.failed && !observed.count && !control.output.len);
    uint64_t paid=control.steps;
    space_add(mutate_read?&facts:&other,parse_one(&arena,"(Fact Outside)"));
    prime_native_control_run(&control,100u,observe,&observed);
    if(mutate_read) {
        CHECK(control.invalidated && control.steps==paid &&
            !observed.count && !control.output.len);
        CHECK(!space_read_token_is_current(control.query_reads[0]));
    } else {
        CHECK(!control.invalidated && !control.failed && control.steps>paid &&
            !observed.count && !control.output.len &&
            space_read_token_is_current(control.query_reads[0]));
    }
    release_stack_control(&control);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
    registry_free(&registry);g_registry=NULL;
    space_free(&space);space_free(&facts);space_free(&other);arena_free(&arena);
}

static void native_call_boundary_case(CettaLanguageId language,const CettaProfile *profile) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);eval_set_library_context(context);
    if(language==CETTA_LANGUAGE_PRIME) {
        Atom *bounded_call=parse_one(&arena,"(Box 7)");
        CettaGsltNativeCallAdvice bounded=
            prime_native_call_advice(&space,&arena,bounded_call);
        CHECK(bounded.placement==CETTA_GSLT_PLACEMENT_LOCAL &&
            bounded.progress==CETTA_GSLT_PROGRESS_BOUNDED && bounded.bound==1u &&
            prime_native_worker_call_local(&space,&arena,bounded_call));
        space_add(&space,parse_one(&arena,"(= (yielding-call $x) $x)"));
        Atom *yielding_call=parse_one(&arena,"(yielding-call 7)");
        CettaGsltNativeCallAdvice yielding=
            prime_native_call_advice(&space,&arena,yielding_call);
        CHECK(yielding.placement==CETTA_GSLT_PLACEMENT_LOCAL &&
            yielding.progress==CETTA_GSLT_PROGRESS_YIELDING && !yielding.bound &&
            prime_native_worker_call_local(&space,&arena,yielding_call));
        CettaGsltNativeCallAdvice coordinated=prime_native_call_advice(
            &space,&arena,parse_one(&arena,
                "(select 1 (search-policy (control FIFO)) (yielding-call 7))"));
        CHECK(coordinated.placement==CETTA_GSLT_PLACEMENT_COORDINATOR &&
            coordinated.progress==CETTA_GSLT_PROGRESS_YIELDING &&
            prime_native_call_inline_at(coordinated,true) &&
            !prime_native_call_inline_at(coordinated,false));
        CettaGsltNativeCallAdvice isolated=prime_native_call_advice(
            &space,&arena,parse_one(&arena,"(readln!)"));
        CHECK(isolated.placement==CETTA_GSLT_PLACEMENT_COORDINATOR &&
            isolated.progress==CETTA_GSLT_PROGRESS_ISOLATED &&
            !prime_native_call_inline_at(isolated,true));
        CettaGsltNativeCallAdvice unresolved=prime_native_call_advice(
            &space,&arena,parse_one(&arena,"((Box 7))"));
        CHECK(unresolved.placement==CETTA_GSLT_PLACEMENT_UNRESOLVED &&
            unresolved.progress==CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS &&
            !prime_native_call_inline_at(unresolved,true));
#define CHECK_QUERY_ADVICE(field,effect) do { \
            Atom *elements[]={atom_symbol_id(&arena,g_builtin_syms.field)}; \
            CettaGsltNativeCallAdvice query=prime_native_call_advice( \
                &space,&arena,atom_expr(&arena,elements,1u)); \
            CHECK(query.placement==CETTA_GSLT_PLACEMENT_COORDINATOR); \
        } while(0);
        CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(CHECK_QUERY_ADVICE)
#undef CHECK_QUERY_ADVICE
        CHECK(!prime_native_worker_call_local(&space,&arena,parse_one(&arena,"((Box 7))")));
        const char *space_effects[]={
            "(space-push &self X)","(space-pop &self)",
            "(space-truncate &self 0)","(space-get &self 0)",
            "(space-len &self)","(space-peek &self)",
            "(size &self)","(count-atoms &self)",
            "(_minimal-space-contains-exact &self X)",
            "(_minimal-space-revision &self)",
        };
        for(size_t i=0u;i<sizeof(space_effects)/sizeof(*space_effects);i++)
            CHECK(!prime_native_worker_call_local(
                &space,&arena,parse_one(&arena,space_effects[i])));
        /* The inspection enters the pending task's authority, including a
         * branch task. A denied operation never reaches the pattern matcher. */
        PrimeEvalStackDriver *previous = g_prime_eval_stack_driver;
        for (unsigned branch = 0u; branch < 2u; branch++) {
            for (unsigned mode = 0u; mode < 4u; mode++) {
                PrimeNativeControl control = {0};
                control.driver.control = &control;
                control.driver.arena = &arena;
                control.policy.breakpoint_pattern = parse_one(&arena,
                    mode & 1u ? "(never:entered)" : "(println! $message)");
                control.policy.breakpoint_budget = UINT64_MAX;
                PrimeNativeWork pending = {.owner=&control, .task_initialized=true};
                pending.task.kind = branch ? PRIME_EVAL_STACK_TASK_BRANCH
                                           : PRIME_EVAL_STACK_TASK_CALL;
                pending.task.space = &space;
                pending.task.arena = &arena;
                pending.task.atom = parse_one(&arena, "(println! BreakpointMustNotExecute)");
                pending.task.pure_authority = mode < 2u;
                g_prime_eval_stack_driver = &control.driver;
                bool stopped = prime_native_breakpoint_stop(&control, &pending);
                CHECK(stopped == (mode != 3u));
                CHECK(control.failed == (mode < 2u));
                CHECK(control.breakpoint_checks == (mode < 2u ? 0u : 1u));
                CHECK(control.breakpoint_hit == (mode == 2u));
                CHECK(!control.breakpoint_incomplete && !control.driver.pure_authority);
                CHECK(control.steps == 0u && pending.task_initialized &&
                      pending.task.atom && !pending.prepared_publication);
            }
        }
        g_prime_eval_stack_driver = previous;
    }
    CHECK(native_dialect_owns_call(NULL,parse_one(&arena,"(select 2 (search-policy (control FIFO)) (rows))")));
    CHECK(native_dialect_owns_call(NULL,parse_one(&arena,"(select 2 (search-policy (control UnknownStrategy)) (rows))")));
    CHECK(!native_dialect_owns_call(NULL,parse_one(&arena,"(select (rows))")));
    CHECK(!native_dialect_owns_call(NULL,parse_one(&arena,"(select 2 (search-policy Unknown) (rows))")));
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    space_free(&space);arena_free(&arena);
}

/* Prepare actual sibling return packets carrying one immutable production.
 * This constructs the provider boundary directly; it does not assert that
 * ordinary sequential tuple evaluation creates competing handlers. */
static void scoped_claim_conflict_case(void) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry = &registry;
    CettaLibraryContext *context = malloc(sizeof(*context)); assert(context);
    cetta_library_context_init_for_language_profile(context, CETTA_LANGUAGE_PRIME,
                                                   cetta_profile_prime_default());
    eval_set_library_context(context);
    term_universe_set_persistent_arena(&context->term_universe, &arena);
    PrimeNativeControl source;
    CettaSearchPolicySpec policy = {.native_control=true, .all=true,
        .work_budget=UINT64_MAX, .workers=1u};
    Atom *read = parse_one(&arena, "(weight:read (algebra 0 1 + *) V)");
    assert(prime_native_control_init(&source, &space, &arena, read, -1, false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, NULL));
    PrimeNativeRunGuard guard; prime_native_run_enter(&source, &guard);
    assert(prime_eval_stack_schedule_normalize(&space, &arena, read, -1,
        &source.base_environment, g_prime_need_evaluator_id, &source.output));
    prime_native_run_leave(&guard);
    Observed observed = {0};
    PrimeEvalStackFrame *readers[2] = {0};
    unsigned ready = 0u;
    for (unsigned ticks=0u; ready<2u && ticks<100u; ticks++) {
        prime_native_control_run(&source, 1u, observe, &observed);
        ready=0u;
        for (PrimeEvalStackFrame *frame=source.frames; frame; frame=frame->control_next)
            if (!frame->control_closed && frame->kind==PRIME_EVAL_STACK_FRAME_WEIGHT_READ) {
                assert(ready<2u); readers[ready++]=frame;
            }
    }
    assert(ready==2u && !observed.count);
    CettaWeightLedger produced={0};
    uint64_t factor=prime_need_fresh_source_occurrence();
    uint64_t dependency=prime_need_fresh_source_occurrence();
    assert(cetta_weight_ledger_append(&arena, &produced, factor, dependency,
                                     atom_int(&arena, 3), &produced));
    unsigned delivered=0u;
    for (size_t i=0u; i<cetta_continuation_hub_length(&source.hub); i++) {
        PrimeNativeWork *work=cetta_continuation_hub_at(&source.hub,i)->payload;
        if (work->task_initialized &&
            (work->parent==readers[0] || work->parent==readers[1])) {
            assert(!bindings_weight_view(&work->task.dynamic_env)->length);
            *bindings_weight_mut(&work->task.dynamic_env)=produced;
            delivered++;
        }
    }
    assert(delivered==2u);
    source.policy.workers=2u;
    prime_native_run_enter(&source, &guard);
    uint64_t used=0u;
    bool wave=prime_native_control_parallel_wave(&source,2u,&used);
    CHECK(wave && used==2u);
    source.steps+=used;
    unsigned prepared=0u;
    for (size_t i=0u;i<cetta_continuation_hub_length(&source.hub);i++) {
        PrimeNativeWork *work=cetta_continuation_hub_at(&source.hub,i)->payload;
        if(work->prepared_publication && work->parallel) prepared++;
    }
    CHECK(prepared==2u);
    prime_native_run_leave(&guard);
    prime_native_control_run(&source,1000u,observe,&observed);
    assert(source.output.len==2u && observed.count==2u);
    const CettaWeightLedger *left=bindings_weight_view(&source.output.items[0].env);
    const CettaWeightLedger *right=bindings_weight_view(&source.output.items[1].env);
    uint64_t first=cetta_weight_ledger_handler(left,factor);
    uint64_t second=cetta_weight_ledger_handler(right,factor);
    assert(first && second && first!=second && left->length==1u && right->length==1u);
    CHECK(left->last->dependency==dependency && right->last->dependency==dependency);
    Bindings refused;
    CHECK(!bindings_clone_merge(&refused,&source.output.items[0].env,
                                        &source.output.items[1].env));
    Bindings unclaimed_left, unclaimed_right, compatible;
    assert(bindings_clone(&unclaimed_left,&source.output.items[0].env));
    assert(bindings_clone(&unclaimed_right,&source.output.items[1].env));
    bindings_weight_mut(&unclaimed_left)->claims=NULL;
    bindings_weight_mut(&unclaimed_left)->claim_count=0u;
    bindings_weight_mut(&unclaimed_right)->claims=NULL;
    bindings_weight_mut(&unclaimed_right)->claim_count=0u;
    /* Removing only test-local ownership maps must make this pair mergeable;
     * otherwise a distinct choice/binding incompatibility was not isolated. */
    assert(bindings_clone_merge(&compatible,&unclaimed_left,&unclaimed_right));
    bindings_free(&compatible); bindings_free(&unclaimed_left); bindings_free(&unclaimed_right);

    /* The enclosing owned consumer must distinguish ownership refusal from
     * a refuted semantic combination and retain the complete paid return. */
    PrimeNativeControl parent;
    assert(prime_native_control_init(&parent,&space,&arena,
        parse_one(&arena,"(superpose ())"),-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    Observed published={0};
    prime_native_control_run(&parent,100u,observe,&published);
    assert(!published.count && prime_native_control_closed(&parent));
    prime_native_run_enter(&parent,&guard);
    PrimeEvalStackFrame *frame=prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM);
    frame->space=&space; frame->arena=&arena; frame->target=&parent.output;
    frame->atom=atom_symbol(&arena,"V"); frame->fuel=-1;
    frame->evaluator_id=g_prime_need_evaluator_id;
    frame->state=PRIME_EVAL_STACK_FRAME_WAIT_CALL;
    assert(bindings_clone(&frame->env,&source.output.items[0].env));
    frame->env_initialized=true;
    outcome_set_init(&frame->child); frame->child_initialized=true;
    outcome_set_add(&frame->child,atom_symbol(&arena,"V"),&source.output.items[1].env);
    prime_eval_stack_push(frame);
    prime_eval_stack_stream_note(&frame->child);
    assert(cetta_continuation_hub_length(&parent.hub)>0u);
    prime_native_run_leave(&guard);
    uint64_t before_return=parent.steps;
    prime_native_control_run(&parent,100u,observe,&published);
    assert(parent.steps>before_return);
    CHECK(!prime_native_control_closed(&parent) && !published.count);
    bool retained=false;
    for (PrimeEvalStackFrame *item=parent.frames;item;item=item->control_next)
        if(item==frame) retained=!item->control_closed && item->index==0u && item->child.len==1u;
    CHECK(retained);
    if(retained) {
        CHECK(cetta_weight_ledger_handler(bindings_weight_view(&frame->env),factor)==first);
        CHECK(cetta_weight_ledger_handler(bindings_weight_view(&frame->child.items[0].env),factor)==second);
        uint64_t ticks=parent.steps;
        for(unsigned retry=0u;retry<3u;retry++) {
            prime_native_control_run(&parent,retry?100u:0u,observe,&published);
            CHECK(parent.steps==ticks && !prime_native_control_closed(&parent) && !published.count);
            CHECK(frame->index==0u && frame->child.len==1u);
        }
    }
    printf("Conflicting prepared readers: factor=%llu handlers=%llu,%llu retained=%d\n",
        (unsigned long long)factor,(unsigned long long)first,(unsigned long long)second,retained);
    prime_native_control_free(&parent); outcome_set_free(&parent.output);
    if(parent.base_environment_initialized) bindings_free(&parent.base_environment);
    prime_native_control_free(&source); outcome_set_free(&source.output);
    if(source.base_environment_initialized) bindings_free(&source.base_environment);
    cetta_library_context_free(context); free(context); eval_set_library_context(NULL);
    registry_free(&registry); g_registry=NULL; space_free(&space); arena_free(&arena);
}

static void sealed_licence_environment_case(void) {
    Arena source, retained;
    arena_init(&source); arena_init(&retained);
    arena_set_hashcons(&source,NULL); arena_set_hashcons(&retained,NULL);
    Bindings ambient, empty, captured;
    bindings_init(&ambient); bindings_init(&empty); bindings_init(&captured);
    CHECK(prime_need_snapshot_begin(bindings_need_mut(&ambient)));
    CHECK(prime_need_snapshot_begin(bindings_need_mut(&captured)));
    uint64_t captured_session=bindings_need_view(&captured)->session_id;
    CettaWeightLedger zero={0}, weighted={0};
    CHECK(cetta_weight_ledger_append(&source,&zero,101u,103u,atom_int(&source,7),&weighted));
    *bindings_weight_mut(&ambient)=weighted;
    *bindings_branch_state_mut(&ambient)=(PrimeNeedBranchState){.session_id=107u,.owner=&source};
    PrimeNeedActiveGuard outside;
    prime_need_active_enter(&outside, &ambient);
    PrimeNeedActiveGuard sealed;
    prime_need_active_enter_captured(&sealed, &empty);
    CHECK(g_prime_need_logical_env==&empty && !g_prime_need_active.session_id);
    CHECK(!g_prime_weight_active.length && !g_prime_weight_active.owner);
    CHECK(!g_prime_need_branch_state_active.owner && !g_prime_need_branch_state_active.session_id);
    prime_need_active_leave(&sealed);
    CHECK(g_prime_need_logical_env==&ambient && g_prime_weight_active.last==weighted.last);
    CHECK(g_prime_need_active.session_id==bindings_need_view(&ambient)->session_id);
    CHECK(g_prime_need_branch_state_active.owner==&source);
    prime_need_active_enter_captured(&sealed, &captured);
    CHECK(g_prime_need_active.session_id==captured_session && !g_prime_need_active.top);
    CHECK(!g_prime_weight_active.length && !g_prime_need_branch_state_active.owner);
    prime_need_active_leave(&sealed);
    prime_need_active_leave(&outside);

    /* The capture owns the selected cell and coefficient even after the
     * frame's original arena retires. Empty sessions are distinct from an
     * absent session, and promotion never assigns another cell identity. */
    uint64_t cell=0u;
    PrimeNeedSnapshot allocated;
    CHECK(prime_need_snapshot_allocate(&source,bindings_need_view(&captured),
        atom_int(&source,11),&allocated,&cell));
    *bindings_need_mut(&captured)=allocated;
    *bindings_weight_mut(&captured)=weighted;
    CHECK(bindings_promote_atoms_to_arena(&captured,&retained));
    bindings_free(&ambient); arena_free(&source);
    PrimeNeedCellView view;
    CHECK(prime_need_snapshot_lookup(bindings_need_view(&captured),cell,&view));
    CHECK(view.origin && atom_eq(view.origin,atom_int(&retained,11)));
    CHECK(bindings_need_view(&captured)->session_id==captured_session);
    CHECK(bindings_weight_view(&captured)->last->occurrence==101u &&
          bindings_weight_view(&captured)->last->dependency==103u &&
          atom_eq(bindings_weight_view(&captured)->last->value,atom_int(&retained,7)));
    bindings_free(&empty); bindings_free(&captured); arena_free(&retained);
}

static void retained_licence_case(CettaLanguageId language,const CettaProfile *profile,
                                 unsigned invalidation) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena,NULL);
    Space space, roads, unrelated;
    space_init(&space); space_init(&roads); space_init(&unrelated);
    Registry registry; registry_init(&registry); g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context)); assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);
    eval_set_library_context(context);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    registry_bind(&registry,"&licence-roads",atom_space(&arena,&roads));
    for(unsigned i=0u;i<8u;i++) space_add(&roads,parse_one(&arena,"(Road 7 1)"));
    PrimeNativeControl *control=malloc(sizeof(*control)); assert(control);
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
        .by_weight=true,.descending=true,.priority=2,.age_share=1u,
        .weight_law=prime_need_symbols()->law_unit_interval_product,
        .workers=1u,.work_budget=0u,.costs=true};
    Atom *query=parse_one(&arena,
        "(weight:read (algebra 0 1 + *) (match &licence-roads (Road $x $w) $x (where $w)))");
    if (language == CETTA_LANGUAGE_HE)
        query = eval_read_registry_words(&registry, &arena, query);
    assert(prime_native_control_init(control,&space,&arena,query,-1,true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    Observed observed={0};
    for(unsigned i=0u;i<1000u;i++) {
        PrimeWeightLicence *a=control->weight_analysis;
        PrimeNativeWork *next=prime_native_control_peek(control);
        if(a && a->phase==PRIME_LICENCE_MATCH_POLL &&
           (invalidation<2u || (next && next->licence==a))) break;
        prime_native_control_run(control,1u,observe,&observed);
    }
    PrimeWeightLicence *analysis=control->weight_analysis;
    assert(analysis && analysis->query && analysis->phase==PRIME_LICENCE_MATCH_POLL);
    CHECK(!control->weight_licensed && !analysis->refusal && analysis->steps>0u);
    CHECK(control->event_cost.total.counts[CETTA_COST_ANALYSIS_STEP]==analysis->steps);
    CHECK(analysis->environment.owners==control->weight_environment.owners);
    CHECK(control->query_read_count>0u);
    PrimeWeightLicence saved=*analysis;
    uint64_t steps=control->steps;
    uint64_t id; assert(cetta_native_handle_alloc(context,"prime-search",control,
        prime_native_control_destroy,&id));
    Atom *handle=cetta_native_handle_owned_atom(context,&arena,"prime-search",id);
    OutcomeSet result; outcome_set_init(&result);
    space_add(&unrelated,parse_one(&arena,"Unrelated"));
    for(unsigned i=0u;i<3u;i++) {
        CHECK(prime_native_control_select(&space,&arena,handle,-1,0u,&policy,&result,NULL,NULL));
        CHECK(control->weight_analysis==analysis && !memcmp(&saved,analysis,sizeof(saved)));
        CHECK(control->steps==steps && control->weight_generation==1u);
    }
    if(invalidation>=2u) {
        /* Inject a writer between the enclosing run's currentness check and
         * its selected operation. Both root and already-captured query reads
         * must refuse before taking a lease, even at zero allowance. */
        CHECK(space_read_token_is_current(control->source_read));
        CHECK(prime_native_control_query_reads_current(control));
        CHECK(space_read_view_is_current(&analysis->source));
        const CettaSelectionAutomaton schedule=control->hub.schedule;
        const size_t length=cetta_continuation_hub_length(&control->hub);
        CettaOwnedContinuation *frontier=calloc(length,sizeof(*frontier));assert(frontier);
        for(size_t i=0u;i<length;i++)frontier[i]=*cetta_continuation_hub_at(&control->hub,i);
        const uint64_t next_lease=control->hub.store.next_lease;
        const size_t leases=control->hub.store.lease_length;
        const uint64_t charged=control->event_cost.total.counts[CETTA_COST_ANALYSIS_STEP];
        const uint64_t quanta=control->event_cost.total.counts[CETTA_COST_QUANTUM];
        space_add(invalidation==2u?&space:&roads,parse_one(&arena,"(Road 8 1)"));
        for(unsigned attempt=0u;attempt<3u;attempt++) {
            if(attempt==1u)cetta_shared_transition_scope_enter();
            unsigned scope=cetta_shared_transition_scope_depth;
            PrimeNativeRunGuard guard;prime_native_run_enter(control,&guard);
            uint64_t spent=prime_native_control_step(control,attempt?1u:0u,false,false);
            if(spent)cetta_native_cost_charge(CETTA_COST_QUANTUM,1u);
            control->steps+=spent;
            prime_native_run_leave(&guard);
            CHECK(!spent && control->steps==steps);
            CHECK(control->invalidated && !control->failed && !prime_native_control_closed(control));
            CHECK(control->weight_analysis==analysis && !memcmp(&saved,analysis,sizeof(saved)));
            CHECK(!memcmp(&schedule,&control->hub.schedule,sizeof(schedule)));
            CHECK(cetta_continuation_hub_length(&control->hub)==length);
            for(size_t i=0u;i<length;i++)
                CHECK(!memcmp(&frontier[i],cetta_continuation_hub_at(&control->hub,i),sizeof(frontier[i])));
            CHECK(control->hub.store.next_lease==next_lease && control->hub.store.lease_length==leases);
            CHECK(control->event_cost.total.counts[CETTA_COST_ANALYSIS_STEP]==charged);
            CHECK(control->event_cost.total.counts[CETTA_COST_QUANTUM]==quanta);
            CHECK(cetta_shared_transition_scope_depth==scope &&
                  !cetta_shared_transition_guard_held_by_current_thread());
            CHECK(!control->active_allowance && !control->active_spent);
            if(attempt==1u)cetta_shared_transition_scope_leave();
        }
        free(frontier);
    } else if(invalidation) {
        space_add(&roads,parse_one(&arena,"(Road 8 1)"));
        prime_native_control_run(control,0u,observe,&observed);
        CHECK(control->invalidated && !prime_native_control_closed(control));
        CHECK(control->weight_analysis==analysis && !memcmp(&saved,analysis,sizeof(saved)));
        CHECK(control->steps==steps && analysis->query==saved.query);
    } else {
        /* An unchanged policy resumes its exact cursor. The observed read
         * may finish before optional analysis, retaining the latter. */
        prime_native_control_run(control,2000u,observe,&observed);
        CHECK(!control->failed && !control->invalidated && prime_native_control_closed(control));
        CHECK(observed.count==8u && control->weight_generation==1u);
        CHECK(control->weight_analysis==analysis && analysis->steps>=saved.steps);
        CHECK(control->event_cost.total.counts[CETTA_COST_ANALYSIS_STEP]==analysis->steps);
        /* Equal read owner/law/order alone cannot recycle a certificate for
         * a changed algebra or subject. Old paid state remains archived. */
        PrimeWeightLicence old=*analysis;
        prime_native_weight_adopt_read(control,analysis->algebra,analysis->subject);
        CHECK(control->weight_analysis==analysis && !memcmp(&old,analysis,sizeof(old)));
        Atom *changed=parse_one(control->driver.arena,"(algebra 0 2 + *)");
        prime_native_weight_adopt_read(control,changed,analysis->subject);
        PrimeWeightLicence *second=control->weight_analysis;
        CHECK(second!=analysis && second->generation==2u && second->steps==0u);
        CHECK(!memcmp(&old,analysis,sizeof(old)) && !control->weight_licensed);
        prime_native_weight_adopt_read(control,changed,atom_int(control->driver.arena,99));
        CHECK(control->weight_analysis!=second && control->weight_analysis->generation==3u);
        CHECK(second->steps==0u && !memcmp(&old,analysis,sizeof(old)));
    }
    outcome_set_free(&result);
    cetta_library_context_free(context); free(context); eval_set_library_context(NULL);
    registry_free(&registry); g_registry=NULL;
    space_free(&space); space_free(&roads); space_free(&unrelated); arena_free(&arena);
}

static void owned_outcome_copy_case(unsigned count, bool owned, bool secondary) {
    Arena source, destination, missing, external, primary_source, primary_destination;
    arena_init(&source); arena_set_hashcons(&source, NULL);
    arena_init(&destination); arena_set_hashcons(&destination, NULL);
    arena_init(&missing); arena_set_hashcons(&missing, NULL);
    arena_init(&external); arena_set_hashcons(&external, NULL);
    arena_init(&primary_source); arena_set_hashcons(&primary_source, NULL);
    arena_init(&primary_destination); arena_set_hashcons(&primary_destination, NULL);
    Atom *value = parse_one(&source, "(Payload 17)");
    Atom *late = parse_one(&source, "(Delayed (Payload 17))");
    Atom *variable = atom_var(&source, "$outcome-captured");
    Atom *external_value = parse_one(&external, "(External 29)");
    PrimeNeedSnapshot root = {0}, allocated, running, completed;
    uint64_t cell_id = 0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate_persisted(&source, &root, value, 61u,
            &allocated, &cell_id) &&
        prime_need_snapshot_start_evaluation(&source, &allocated, cell_id, 83u, &running) &&
        prime_need_snapshot_resolve_value(&source, &running, cell_id, 83u, value, &completed));
    completed.max_storage_key += 911u;
    CettaWeightLedger empty = {0}, factors, handled, selected;
    assert(cetta_weight_ledger_append(&source, &empty, 701u, 801u, value, &factors) &&
        cetta_weight_ledger_append(&source, &factors, 702u, 802u, value, &factors) &&
        cetta_weight_ledger_handle(&source, &factors, &empty, 901u, &handled, &selected));
    Bindings seed; bindings_init(&seed);
    assert(bindings_add_id(&seed, var_id_from_base(419u), SYMBOL_ID_NONE, value) &&
        bindings_add_id(&seed, var_id_from_base(422u), SYMBOL_ID_NONE, handled.last->previous->value) &&
        bindings_add_id(&seed, var_id_from_base(423u), SYMBOL_ID_NONE, handled.last->value) &&
        bindings_add_var(&seed, variable, external_value));
    *bindings_need_mut(&seed) = completed;
    *bindings_weight_mut(&seed) = handled;
    PrimeNeedCellView before = {0};
    assert(prime_need_snapshot_lookup(&completed, cell_id, &before) &&
        before.origin == value && before.cached == value &&
        bindings_lookup_value_id(&seed, var_id_from_base(419u)).skeleton == value &&
        bindings_lookup_value_id(&seed, var_id_from_base(422u)).skeleton == handled.last->previous->value &&
        bindings_lookup_value_id(&seed, var_id_from_base(423u)).skeleton == handled.last->value &&
        handled.last->value != value && handled.last->previous->value != value &&
        handled.last->value != handled.last->previous->value);
    VariantShape shape; variant_shape_init(&shape);
    VarId slot = variant_shape_slot_id(0u);
    assert(bindings_add_id(&shape.slot_env, slot, SYMBOL_ID_NONE, value));
    OutcomeSet original, copy;
    outcome_set_init_with_owner(&original, owned ? &source : NULL);
    outcome_set_init(&copy);
    uint64_t occurrences[3] = {0};
    for (unsigned i = 0u; i < count; i++) {
        Outcome *out = outcome_set_push_slot(&original); assert(out);
        outcome_init(out);
        out->atom = value;
        out->materialized_atom = i % 2u ? NULL : value;
        out->delayed = late;
        assert(bindings_clone(&out->env, &seed) &&
            bindings_refresh_occurrence_token(&out->env) &&
            variant_instance_from_shape(&out->variant, &shape) &&
            cetta_var_map_add(&out->answer_ref.goal_instantiation, var_id_from_base(519u), variable));
        occurrences[i] = bindings_occurrence_token(&out->env);
        assert(variant_instance_peek_private_id(&out->variant, slot) == value);
    }
    outcome_set_add_unfactored(&copy, value, &seed);
    assert(copy.len == 1u && copy.items[0].atom == value);
    AtomDeepCopySession *bad = atom_deep_copy_session_new(&missing);
    AtomDeepCopyArenaPair absent[] = {{&source, &missing}};
    assert(bad && atom_deep_copy_session_set_arena_map(bad, absent, 1u));
    if (count) {
        CHECK(!outcome_set_copy_to_arena(&original, &copy, &missing, bad));
        CHECK(copy.len == 1u && copy.items[0].atom == value && original.items[0].atom == value);
    }
    atom_deep_copy_session_free(bad);
    AnswerBank bank; answer_bank_init(&bank);
    AnswerRef ref;
    assert(answer_bank_add(&bank, external_value, NULL, NULL, &ref));
    Arena *primary = secondary ? &primary_destination : &destination;
    AtomDeepCopySession *atoms = atom_deep_copy_session_new(primary);
    AtomDeepCopyArenaPair pairs[] = {{&source, &destination}, {&external, &external},
        {&primary_source, &primary_destination}};
    assert(atoms && atom_deep_copy_session_set_arena_map(atoms, pairs, 3u));
    AtomDeepCopyDestinationScope scope = {0};
    if (secondary) {
        CHECK(!outcome_set_copy_to_arena(&original, &copy, &destination, atoms));
        CHECK(copy.len == 1u && copy.items[0].atom == value);
        assert(atom_deep_copy_destination_enter(atoms, &destination, &scope));
    }
    if (count) {
        original.items[count - 1u].kind = CETTA_OUTCOME_ANSWER_REF;
        original.items[count - 1u].answer_ref.bank = &bank;
        original.items[count - 1u].answer_ref.ref = ref;
        CHECK(!outcome_set_copy_to_arena(&original, &copy, &destination, atoms));
        CHECK(copy.len == 1u && copy.items[0].atom == value &&
              answer_bank_get(original.items[count - 1u].answer_ref.bank, ref) != NULL);
        original.items[count - 1u].kind = CETTA_OUTCOME_INLINE;
        original.items[count - 1u].answer_ref.bank = NULL;
        original.items[count - 1u].answer_ref.ref = CETTA_ANSWER_REF_NONE;
        /* A refused preparation is unpublished; start again under the same
         * source. The existing OutcomeSet remains intact. */
        assert(atom_deep_copy_destination_leave(&scope));
        atom_deep_copy_session_free(atoms);
        arena_free(&destination); arena_init(&destination); arena_set_hashcons(&destination, NULL);
        atoms = atom_deep_copy_session_new(primary);
        assert(atoms && atom_deep_copy_session_set_arena_map(atoms, pairs, 3u));
        if (secondary) assert(atom_deep_copy_destination_enter(atoms, &destination, &scope));
    }
    Atom *transported = atom_deep_copy_session_copy(atoms, value);
    Atom *transported_var = atom_deep_copy_session_copy(atoms, variable);
    CHECK(outcome_set_copy_to_arena(&original, &copy, &destination, atoms));
    CHECK(copy.len == count && copy.cap == original.cap &&
          (copy.items == copy.inline_items) == (original.items == original.inline_items) &&
          copy.payload_owner == (owned ? &destination : NULL));
    for (unsigned i = 0u; i < count; i++) {
        CHECK(original.items[i].atom == value && original.items[i].delayed == late &&
              original.items[i].materialized_atom == (i % 2u ? NULL : value));
    }
    assert(atom_deep_copy_destination_leave(&scope));
    CHECK(atom_deep_copy_session_targets(atoms, primary));
    atom_deep_copy_session_free(atoms);
    outcome_set_free(&original); bindings_free(&seed); variant_shape_free(&shape);
    answer_bank_free(&bank); arena_free(&source); arena_free(&missing);
    arena_free(&primary_source); arena_free(&primary_destination);
    for (unsigned i = 0u; i < count; i++) {
        Outcome *out = &copy.items[i];
        PrimeNeedCellView cell = {0};
        const CettaWeightLedger *weights = bindings_weight_view(&out->env);
        CHECK(out->atom == transported && out->materialized_atom == (i % 2u ? NULL : transported) &&
              out->delayed->expr.elems[1]->expr.elems[1]->ground.ival == 17);
        CHECK(variant_instance_peek_private_id(&out->variant, slot) == transported &&
              cetta_var_map_lookup(&out->answer_ref.goal_instantiation, var_id_from_base(519u)) == transported_var &&
              bindings_lookup_value_id(&out->env, transported_var->var_id).skeleton == external_value);
        CHECK(prime_need_snapshot_lookup(bindings_need_view(&out->env), cell_id, &cell) &&
              cell.origin == transported && cell.cached == transported &&
              bindings_need_view(&out->env)->max_storage_key == completed.max_storage_key &&
              bindings_occurrence_token(&out->env) == occurrences[i]);
        CHECK(weights->length == 2u && weights->last->value != transported &&
              weights->last->value != weights->last->previous->value &&
              bindings_lookup_value_id(&out->env, var_id_from_base(422u)).skeleton == weights->last->previous->value &&
              bindings_lookup_value_id(&out->env, var_id_from_base(423u)).skeleton == weights->last->value &&
              cetta_weight_ledger_handler(weights, 701u) == 901u &&
              cetta_weight_ledger_handler(weights, 702u) == 901u);
        if (i) CHECK(occurrences[i] != occurrences[i - 1u] && copy.items[i - 1u].atom == out->atom);
    }
    outcome_set_free(&copy); arena_free(&destination); arena_free(&external);
}

typedef struct {
    size_t worlds;
    CettaNativeCostSnapshot cost;
} NeedWorldCopyResult;

static NeedWorldCopyResult measure_owned_need_worlds(Arena *arena, const OutcomeSet *outcomes) {
    PrimeNeedEquationContinuation continuation = {0};
    continuation.arena = arena;
    continuation.original_call = outcomes->items[0].atom;
    outcome_set_init(&continuation.forced_worlds);
    CettaNativeCostLedger ledger;
    cetta_native_cost_init(&ledger);
    CettaNativeCostScope scope;
    cetta_native_cost_scope_enter(&scope, &ledger, 991u);
    assert(prime_need_equation_forced_world_push(&continuation, &outcomes->items[0].env));
    assert(prime_need_equation_forced_world_push(&continuation, &outcomes->items[1].env));
    assert(prime_need_equation_forced_world_push(&continuation, &outcomes->items[0].env));
    cetta_native_cost_scope_leave(&scope);
    NeedWorldCopyResult result = {(size_t)continuation.forced_worlds.len, ledger.total};
    assert(!ledger.total.trace_incomplete && !ledger.total.overflow);
    outcome_set_free(&continuation.forced_worlds);
    cetta_native_cost_free(&ledger);
    return result;
}

static bool owned_need_world_copy_case(bool distinct, bool mapped) {
    Arena source, other, destination, other_destination;
    arena_init(&source); arena_set_hashcons(&source, NULL);
    arena_init(&other); arena_set_hashcons(&other, NULL);
    arena_init(&destination); arena_set_hashcons(&destination, NULL);
    arena_init(&other_destination); arena_set_hashcons(&other_destination, NULL);
    Atom *value = atom_expr2(&source, atom_symbol(&source, "Value"), atom_int(&source, 7));
    Atom *delayed = atom_expr2(&source, atom_symbol(&source, "Later"), value);
    PrimeNeedSnapshot root = {0}, allocated, running, completed;
    uint64_t cell = 0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate_persisted(&source, &root, value, 61u, &allocated, &cell) &&
        prime_need_snapshot_start_evaluation(&source, &allocated, cell, 83u, &running) &&
        prime_need_snapshot_resolve_value(&source, &running, cell, 83u, value, &completed));
    completed.max_storage_key += 911u;
    CettaWeightLedger empty = {0}, weights, handled, selected;
    assert(cetta_weight_ledger_append(&source, &empty, 701u, 801u, atom_int(&source, 4), &weights) &&
        cetta_weight_ledger_append(&source, &weights, 702u, 802u, atom_int(&source, 5), &weights) &&
        cetta_weight_ledger_handle(&source, &weights, &empty, 901u, &handled, &selected));
    Bindings seed; bindings_init(&seed);
    *bindings_need_mut(&seed) = completed;
    *bindings_weight_mut(&seed) = handled;
    OutcomeSet original, copied;
    outcome_set_init(&original); outcome_set_init(&copied);
    for (size_t i = 0u; i < 2u; i++) {
        Outcome *out = outcome_set_push_slot(&original); assert(out);
        outcome_init(out);
        out->atom = value; out->materialized_atom = i ? NULL : value;
        out->delayed = delayed;
        assert(bindings_clone(&out->env, &seed) && bindings_refresh_occurrence_token(&out->env));
    }
    if (distinct) {
        AtomDeepCopySession *separate = atom_deep_copy_session_new(&other);
        assert(separate && prime_need_snapshot_promote_with_session(&other, separate,
            bindings_need_mut(&original.items[1].env)));
        atom_deep_copy_session_free(separate);
    }
    const PrimeNeedSnapshot *a = bindings_need_view(&original.items[0].env);
    const PrimeNeedSnapshot *b = bindings_need_view(&original.items[1].env);
    PrimeNeedCellView ca = {0}, cb = {0};
    assert(prime_need_snapshot_lookup(a, cell, &ca) && prime_need_snapshot_lookup(b, cell, &cb));
    bool source_shared = a->top == b->top;
    assert(source_shared == !distinct && ca.authority_id == cb.authority_id &&
        ca.cache_state == cb.cache_state && atom_eq(ca.origin, cb.origin) && atom_eq(ca.cached, cb.cached));
    uint64_t tokens[2] = {bindings_occurrence_token(&original.items[0].env),
                          bindings_occurrence_token(&original.items[1].env)};
    assert(tokens[0] && tokens[1] && tokens[0] != tokens[1]);
    NeedWorldCopyResult before = measure_owned_need_worlds(&source, &original);
    AtomDeepCopySession *atoms = atom_deep_copy_session_new(&destination);
    assert(atoms);
    if (mapped) {
        AtomDeepCopyArenaPair pairs[] = {{&source, &destination}, {&other, &other_destination}};
        assert(atom_deep_copy_session_set_arena_map(atoms, pairs, 2u));
    }
    assert(outcome_set_copy_to_arena(&original, &copied, &destination, atoms));
    bool copied_shared = bindings_need_view(&copied.items[0].env)->top ==
                         bindings_need_view(&copied.items[1].env)->top;
    assert((bindings_need_view(&original.items[0].env)->top ==
            bindings_need_view(&original.items[1].env)->top) == source_shared);
    assert(copied.len == 2u && copied.cap == original.cap);
    atom_deep_copy_session_free(atoms);
    outcome_set_free(&original); bindings_free(&seed);
    arena_free(&source); arena_free(&other);
    bool residual = true;
    for (size_t i = 0u; i < 2u; i++) {
        const Outcome *out = &copied.items[i];
        const CettaWeightLedger *ledger = bindings_weight_view(&out->env);
        PrimeNeedCellView cell_view = {0};
        residual &= bindings_occurrence_token(&out->env) == tokens[i] &&
            out->materialized_atom == (i ? NULL : out->atom) &&
            out->delayed->expr.elems[1] == out->atom &&
            prime_need_snapshot_lookup(bindings_need_view(&out->env), cell, &cell_view) &&
            cell_view.cache_state == PRIME_NEED_CACHE_VALUE &&
            bindings_need_view(&out->env)->max_storage_key == completed.max_storage_key &&
            ledger->length == 2u && ledger->last->value->ground.ival == 5 &&
            ledger->last->previous->value->ground.ival == 4 &&
            cetta_weight_ledger_handler(ledger, 701u) == 901u &&
            cetta_weight_ledger_handler(ledger, 702u) == 901u;
    }
    NeedWorldCopyResult after = measure_owned_need_worlds(&destination, &copied);
    bool equal_cost = before.cost.work == after.cost.work && before.cost.span == after.cost.span;
    for (size_t i = 0u; i < CETTA_COST_KIND_COUNT; i++)
        equal_cost &= before.cost.counts[i] == after.cost.counts[i];
    bool ok = copied_shared == source_shared && before.worlds == after.worlds && equal_cost && residual;
    printf("{\"injective_arena_map\":%s,\"distinct_equal_paths\":%s,\"source_shared_top\":%s,\"copied_shared_top\":%s,"
           "\"source_worlds\":%zu,\"copied_worlds\":%zu,\"source_work\":%" PRIu64 ",\"copied_work\":%" PRIu64 ","
           "\"source_binding_clones\":%" PRIu64 ",\"copied_binding_clones\":%" PRIu64 ","
           "\"same_costs\":%s,\"payload_residual_retained\":%s,\"passed\":%s}\n",
           mapped ? "true" : "false", distinct ? "true" : "false", source_shared ? "true" : "false", copied_shared ? "true" : "false",
           before.worlds, after.worlds, before.cost.work, after.cost.work,
           before.cost.counts[CETTA_COST_BINDING_CLONE], after.cost.counts[CETTA_COST_BINDING_CLONE],
           equal_cost ? "true" : "false", residual ? "true" : "false", ok ? "true" : "false");
    outcome_set_free(&copied); arena_free(&destination); arena_free(&other_destination);
    return ok;
}

static SpaceQueryCursorStep owned_query_row_until_ready(SpaceQueryCursor *cursor, Bindings *row, size_t *steps, CettaNativeCostSnapshot *cost) {
    *steps = 0u;
    CettaNativeCostLedger ledger;cetta_native_cost_init(&ledger);
    CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&ledger,993u);
    assert(space_query_cursor_step_with_budget(cursor,0u,row)==SPACE_QUERY_CURSOR_PROGRESS);
    assert(ledger.total.work==0u);
    SpaceQueryCursorStep status;
    do { status = space_query_cursor_step_with_budget(cursor, 1u, row); (*steps)++; }
    while (status == SPACE_QUERY_CURSOR_PROGRESS && *steps < 100u);
    cetta_native_cost_scope_leave(&scope);
    *cost=ledger.total;assert(!cost->trace_incomplete && !cost->overflow);
    cetta_native_cost_free(&ledger);
    return status;
}

typedef struct { SpaceReadToken original; size_t count; } CapturedQueryReads;
static bool captured_query_read(SpaceReadToken read, void *raw) {
    CapturedQueryReads *check=raw;
    CHECK(read.space==check->original.space && read.instance_id==check->original.instance_id);
    CHECK(read.revision==check->original.revision && read.prefix_epoch==check->original.prefix_epoch);
    CHECK(!space_read_token_is_current(read));
    check->count++;
    return true;
}
static bool captured_query_witness(SpaceReadToken read, CettaIndex index, void *raw) {
    CHECK(index<3u);
    return captured_query_read(read,raw);
}

/* Every later factor reads the captured physical bag, including after the
 * original cursor and its private syntax have been released. A new query
 * starts in the changed world; a strict validation cursor refuses it. */
static void captured_query_publication_case(bool replace, bool relocate, bool alternative) {
    Arena external,input,destination;
    arena_init(&external);arena_init(&input);arena_init(&destination);
    arena_set_hashcons(&input,NULL);arena_set_hashcons(&destination,NULL);
    TermUniverse universe;term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    assert(space_match_backend_try_set(&space,SPACE_ENGINE_NATIVE));
    Atom *values[]={atom_symbol(&external,"A"),atom_symbol(&external,"A"),atom_symbol(&external,"B")};
    Atom *facts[3];
    for(size_t i=0u;i<3u;i++) {
        facts[i]=atom_expr2(&external,atom_symbol(&external,"Fact"),values[i]);
        space_add(&space,facts[i]);
    }
    Atom *x=atom_var_with_spelling(&input,symbol_intern_cstr(g_symbols,"captured-x"),var_id_from_base(124201u));
    Atom *y=atom_var_with_spelling(&input,symbol_intern_cstr(g_symbols,"captured-y"),var_id_from_base(124202u));
    Atom *left=atom_expr2(&input,atom_symbol(&input,"Fact"),x);
    Atom *right=atom_expr2(&input,atom_symbol(&input,"Fact"),alternative?x:y);
    Atom *pattern=atom_expr3(&input,atom_symbol(&input,alternative?"|":","),left,right);
    Atom *projection=alternative?x:atom_expr3(&input,atom_symbol(&input,"Pair"),x,y);
    Bindings row;bindings_init(&row);
    SpaceQueryCursor *cursor=space_query_cursor_new_captured(&space,&input,pattern,NULL);
    SpaceQueryCursor *strict=space_query_cursor_new(&space,&input,pattern,NULL);
    assert(cursor && strict);
    size_t steps=0u;CettaNativeCostSnapshot cost;
    assert(owned_query_row_until_ready(cursor,&row,&steps,&cost)==SPACE_QUERY_CURSOR_ROW);
    Atom *first=alternative?values[0]:atom_expr3(&external,atom_symbol(&external,"Pair"),values[0],values[0]);
    CHECK(atom_eq(bindings_apply_if_vars(&row,&input,projection),first));
    bindings_free(&row);bindings_init(&row);
    SpaceReadToken original=space_read_token(&space);
    if(replace) CHECK(space_remove(&space,facts[2]));
    else space_add(&space,parse_one(&external,"(Fact New)"));
    CHECK(space_query_cursor_step_with_budget(strict,1u,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
    space_query_cursor_free(strict);
    SpaceQueryCursor *copy;
    Arena *result=&input;
    if(relocate) {
        AtomDeepCopySession *atoms=atom_deep_copy_session_new(&destination);
        AtomDeepCopyArenaPair pairs[]={{&input,&destination},{&external,&external}};
        assert(atoms && atom_deep_copy_session_set_arena_map(atoms,pairs,2u));
        copy=space_query_cursor_copy_to_arena(cursor,&destination,atoms);
        projection=atom_deep_copy_session_copy(atoms,projection);
        pattern=atom_deep_copy_session_copy(atoms,pattern);
        assert(copy && projection && pattern);
        atom_deep_copy_session_free(atoms);
        result=&destination;
    } else copy=space_query_cursor_clone(cursor);
    assert(copy);space_query_cursor_free(cursor);
    if(relocate)arena_free(&input);
    CapturedQueryReads reads={original,0u};
    CHECK(space_query_cursor_visit_reads(copy,captured_query_read,&reads) && reads.count==1u);
    size_t expected=alternative?6u:9u;
    for(size_t i=1u;i<expected;i++) {
        assert(owned_query_row_until_ready(copy,&row,&steps,&cost)==SPACE_QUERY_CURSOR_ROW);
        Atom *value=alternative?values[i%3u]:atom_expr3(&external,
            atom_symbol(&external,"Pair"),values[i/3u],values[i%3u]);
        CHECK(atom_eq(bindings_apply_if_vars(&row,result,projection),value));
        reads.count=0u;
        CHECK(space_query_cursor_visit_witnesses(copy,captured_query_witness,&reads));
        CHECK(reads.count==(alternative?1u:2u));
        bindings_free(&row);bindings_init(&row);
    }
    CHECK(owned_query_row_until_ready(copy,&row,&steps,&cost)==SPACE_QUERY_CURSOR_END);
    space_query_cursor_free(copy);
    SpaceQueryCursor *fresh=space_query_cursor_new(&space,result,pattern,NULL);assert(fresh);
    size_t count=0u;SpaceQueryCursorStep status;
    while((status=owned_query_row_until_ready(fresh,&row,&steps,&cost))==SPACE_QUERY_CURSOR_ROW) {
        count++;bindings_free(&row);bindings_init(&row);
    }
    CHECK(status==SPACE_QUERY_CURSOR_END);
    CHECK(count==(alternative?(replace?4u:8u):(replace?4u:16u)));
    space_query_cursor_free(fresh);bindings_free(&row);
    if(!relocate)arena_free(&input);
    arena_free(&destination);space_free(&space);term_universe_free(&universe);arena_free(&external);
}
static bool owned_query_need_world_case(size_t cut, size_t *limit,
        bool distinct, bool query_first, bool fallback) {
    Arena external,input,result,destination_input,destination_result;
    arena_init(&external);arena_init(&input);arena_init(&result);
    arena_init(&destination_input);arena_init(&destination_result);
    Arena *private_arenas[]={&input,&result,&destination_input,&destination_result};
    for(size_t i=0;i<4;i++) {arena_set_hashcons(private_arenas[i],NULL);arena_set_runtime_kind(private_arenas[i],CETTA_ARENA_RUNTIME_KIND_EVAL);}
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    assert(space_match_backend_try_set(&space,SPACE_ENGINE_NATIVE));
    const SpaceMatchBackendOps *native_ops=space.match_backend.ops;
    SpaceMatchBackendOps atomic_ops=*native_ops;
    if(fallback)space.match_backend.ops=&atomic_ops;
    Atom *fact=atom_expr2(&external,atom_symbol(&external,"query-frame"),atom_int(&external,7));
    AtomId fact_id=term_universe_store_atom_id(&universe,NULL,fact);assert(fact_id);
    space_add_atom_id(&space,fact_id);
    Atom *variable=atom_var_with_spelling(&input,symbol_intern_cstr(g_symbols,"query-frame-var"),var_id_from_base(123401u));
    Atom *pattern=atom_expr2(&input,atom_symbol(&input,"query-frame"),variable);
    Atom *value=atom_expr2(&result,atom_symbol(&result,"Value"),atom_int(&result,7));
    PrimeNeedSnapshot root={0},allocated,running,completed;uint64_t cell=0;
    assert(prime_need_snapshot_begin(&root) && prime_need_snapshot_allocate(&result,&root,value,&allocated,&cell));
    assert(prime_need_snapshot_start_evaluation(&result,&allocated,cell,83,&running) &&
        prime_need_snapshot_resolve_value(&result,&running,cell,83,value,&completed));
    Bindings seed,row;bindings_init(&seed);bindings_init(&row);*bindings_need_mut(&seed)=completed;
    OutcomeSet original,copy;outcome_set_init(&original);outcome_set_init(&copy);
    Outcome *slot=outcome_set_push_slot(&original);assert(slot);outcome_init(slot);slot->atom=value;
    assert(bindings_clone(&slot->env,&seed));
    Bindings query_seed;bindings_init(&query_seed);assert(bindings_clone(&query_seed,&seed));
    if(distinct) {
        PrimeNeedSnapshot other;
        assert(prime_need_snapshot_resolve_value(&result,&running,cell,83,value,&other));
        assert(other.top!=completed.top);
        *bindings_need_mut(&query_seed)=other;
    }
    SpaceQueryCursor *cursor=space_query_cursor_new(&space,&result,pattern,&query_seed);assert(cursor);
    assert(space_query_cursor_incremental(cursor)==!fallback);
    for(size_t i=0;i<cut;i++) assert(space_query_cursor_step_with_budget(cursor,1,&row)==SPACE_QUERY_CURSOR_PROGRESS);
    SpaceQueryCursor *reference=space_query_cursor_clone(cursor);assert(reference);
    size_t steps=0;CettaNativeCostSnapshot query_before={0},query_after={0};
    assert(owned_query_row_until_ready(reference,&row,&steps,&query_before)==SPACE_QUERY_CURSOR_ROW);
    if(!cut)*limit=steps;
    bool source_shared=bindings_need_view(&row)->top==completed.top;assert(source_shared==!distinct);
    slot=outcome_set_push_slot(&original);assert(slot);outcome_init(slot);slot->atom=value;assert(bindings_clone(&slot->env,&row));
    NeedWorldCopyResult before=measure_owned_need_worlds(&result,&original);
    outcome_free_fields(&original.items[1]);original.len=1;
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&destination_result);
    AtomDeepCopyArenaPair pairs[]={{&input,&destination_input},{&result,&destination_result},{&external,&external}};
    assert(atoms && atom_deep_copy_session_set_arena_map(atoms,pairs,3));
    SpaceQueryCursor *copied_query=NULL;
    if(query_first)copied_query=space_query_cursor_copy_to_arena(cursor,&destination_result,atoms);
    assert(outcome_set_copy_to_arena(&original,&copy,&destination_result,atoms));
    if(!query_first)copied_query=space_query_cursor_copy_to_arena(cursor,&destination_result,atoms);
    assert(copied_query);
    space_query_cursor_free(reference);space_query_cursor_free(cursor);bindings_free(&row);bindings_free(&seed);bindings_free(&query_seed);outcome_set_free(&original);
    atom_deep_copy_session_free(atoms);arena_free(&result);arena_free(&input);
    bindings_init(&row);size_t copied_steps=0;
    assert(owned_query_row_until_ready(copied_query,&row,&copied_steps,&query_after)==SPACE_QUERY_CURSOR_ROW);
    bool copied_shared=bindings_need_view(&row)->top==bindings_need_view(&copy.items[0].env)->top;
    slot=outcome_set_push_slot(&copy);assert(slot);outcome_init(slot);slot->atom=copy.items[0].atom;assert(bindings_clone(&slot->env,&row));
    NeedWorldCopyResult after=measure_owned_need_worlds(&destination_result,&copy);
    bool costs=before.cost.work==after.cost.work && before.cost.span==after.cost.span &&
        query_before.work==query_after.work && query_before.span==query_after.span;
    for(size_t i=0;i<CETTA_COST_KIND_COUNT;i++)costs &= before.cost.counts[i]==after.cost.counts[i] && query_before.counts[i]==query_after.counts[i];
    bool passed=source_shared==copied_shared && before.worlds==after.worlds && costs && steps==copied_steps;
    printf("query world distinct=%u query_first=%u atomic=%u: ",distinct,query_first,fallback);
    printf("{\"cut\":%zu,\"query_steps\":%zu,\"copied_steps\":%zu,\"source_shared_top\":%s,\"copied_shared_top\":%s,\"source_worlds\":%zu,\"copied_worlds\":%zu,\"source_work\":%"PRIu64",\"copied_work\":%"PRIu64",\"passed\":%s}\n",cut,steps,copied_steps,source_shared?"true":"false",copied_shared?"true":"false",before.worlds,after.worlds,before.cost.work,after.cost.work,passed?"true":"false");
    space_query_cursor_free(copied_query);bindings_free(&row);outcome_set_free(&copy);
    arena_free(&destination_result);arena_free(&destination_input);space.match_backend.ops=native_ops;space_free(&space);term_universe_free(&universe);arena_free(&external);
    return passed;
}

/* Capture is a sequential observation inside one stable native episode. */
static void native_capture_observer_contract_case(
    CettaLanguageId language, const CettaProfile *profile) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space root,dependency,unrelated;
    space_init(&root);space_init(&dependency);space_init(&unrelated);
    CettaLibraryContext *library=calloc(1u,sizeof(*library));assert(library);
    cetta_library_context_init_for_language_profile(library,language,profile);
    eval_set_library_context(library);
    NativeDialectOwner *owner=calloc(1u,sizeof(*owner));assert(owner);
    PrimeNativeControl *control=calloc(1u,sizeof(*control));assert(control);
    owner->control=control;
    owner->context.library_context=library;
    owner->context.controller_admitted=true;
    owner->context.semantic_authority_epoch=41u;
    SpaceReadToken dependency_read=space_read_token(&dependency);
    control->query_reads=&dependency_read;control->query_read_count=1u;
    SymbolId rule=symbol_intern_cstr(g_symbols,"capture-observer-rule");
    assert(cetta_library_petta_translator_rule_set(library,rule,true));
    const uint64_t symbol_instance=symbol_table_instance_id(g_symbols);
    CHECK(library->petta_translator_symbol_table_instance==symbol_instance);
    CettaBranchAdmission ordinary=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    CettaBranchAdmission native=native_dialect_admit_capture(owner,&root);
    CHECK(ordinary.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(native.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(ordinary.capacity==CETTA_BRANCH_CAPTURE_MULTI_SHOT &&
          native.capacity==CETTA_BRANCH_CAPTURE_MULTI_SHOT);
    const CettaLibPrologReadToken admitted_foreign =
        cetta_lib_prolog_read_token(library->lib_prolog);
    CHECK(library->lib_prolog &&
          ordinary.authority.words[4]==admitted_foreign.instance_id &&
          ordinary.authority.words[5]==admitted_foreign.revision);
#if CETTA_BUILD_WITH_LIB_PROLOG
    CHECK(admitted_foreign.instance_id!=0u && admitted_foreign.revision!=0u);
#else
    CHECK(admitted_foreign.instance_id==0u && admitted_foreign.revision==0u);
#endif
    CettaLibraryContext *library_before=malloc(sizeof(*library_before));assert(library_before);
    memcpy(library_before,library,sizeof(*library));
    PettaEvalMachineContext context_before=owner->context;
    PrimeNativeControl *control_before=malloc(sizeof(*control_before));assert(control_before);
    memcpy(control_before,control,sizeof(*control));
    const SpaceReadToken root_read=space_read_token(&root);
    const CettaLibPrologReadToken foreign=cetta_lib_prolog_read_token(library->lib_prolog);
    for(unsigned repeat=0u;repeat<3u;repeat++) {
        CettaBranchAdmission a=petta_eval_machine_admit_branch_capture(&owner->context,&root);
        CettaBranchAdmission b=native_dialect_admit_capture(owner,&root);
        CettaLibPrologReadToken now=cetta_lib_prolog_read_token(library->lib_prolog);
        CHECK(a.status==ordinary.status && b.status==native.status);
        CHECK(cetta_branch_authority_token_equal(&a.authority,&ordinary.authority));
        CHECK(cetta_branch_authority_token_equal(&b.authority,&native.authority));
        CHECK(!memcmp(&context_before,&owner->context,sizeof(context_before)));
        CHECK(!memcmp(library_before,library,sizeof(*library)));
        CHECK(!memcmp(control_before,control,sizeof(*control)));
        CHECK(space_read_token_matches_live_space(root_read,&root));
        CHECK(space_read_token_matches_live_space(dependency_read,&dependency));
        CHECK(now.instance_id==foreign.instance_id && now.revision==foreign.revision);
    }
    free(library_before);free(control_before);
    petta_eval_machine_advance_semantic_authority(&owner->context);
    CettaBranchAdmission changed=native_dialect_admit_capture(owner,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(!cetta_branch_authority_token_equal(&changed.authority,&native.authority));
    CHECK(changed.authority.words[3]==owner->context.semantic_authority_epoch);
    native=changed;
    CHECK(petta_eval_machine_translator_rule_set(&owner->context,rule,false));
    changed=native_dialect_admit_capture(owner,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(!cetta_branch_authority_token_equal(&changed.authority,&native.authority));
    CHECK(changed.authority.words[7]==library->petta_translator_rule_revision);
    CHECK(library->petta_translator_rule_len==0u);
    native=changed;
    ordinary=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    space_add(&unrelated,atom_int(&arena,3));
    changed=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(!cetta_branch_authority_token_equal(&changed.authority,&ordinary.authority));
    changed=native_dialect_admit_capture(owner,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(cetta_branch_authority_token_equal(&changed.authority,&native.authority));
    space_add(&root,atom_int(&arena,7));
    changed=native_dialect_admit_capture(owner,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(!cetta_branch_authority_token_equal(&changed.authority,&native.authority));
    CHECK(changed.authority.words[2]==space_revision(&root));
    space_add(&dependency,atom_int(&arena,9));
    CHECK(native_dialect_admit_capture(owner,&root).status==CETTA_BRANCH_ADMISSION_INVALIDATED);
    CHECK(!control->invalidated);
    dependency_read=space_read_token(&dependency);
    CHECK(native_dialect_admit_capture(owner,&root).status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    control->invalidated=true;
    CHECK(native_dialect_admit_capture(owner,&root).status==CETTA_BRANCH_ADMISSION_INVALIDATED);
    control->invalidated=false;
    owner->context.controller_admitted=false;
    CHECK(native_dialect_admit_capture(owner,&root).status==CETTA_BRANCH_ADMISSION_DEFERRED);
    owner->context.controller_admitted=true;
    PettaEvalTransaction transaction={0};owner->context.transaction=&transaction;
    CHECK(native_dialect_admit_capture(owner,&root).status==CETTA_BRANCH_ADMISSION_DEFERRED);
    owner->context.transaction=NULL;
    CHECK(petta_eval_machine_translator_rule_set(&owner->context,rule,true));
    ordinary=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    const uint64_t translator_before=library->petta_translator_rule_revision;
    const uint64_t admission_before=library->petta_admission_revision;
    CHECK(library->petta_translator_rule_len==1u);
    SymbolTable *previous=g_symbols;
    SymbolTable replacement;symbol_table_init(&replacement);g_symbols=&replacement;
    /* Reassociation is outside the stable episode: the actual service
     * clears symbol-keyed rules and changes authority, rather than merely
     * synchronizing a semantically neutral cache. */
    changed=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    CHECK(changed.status==CETTA_BRANCH_ADMISSION_AVAILABLE);
    CHECK(library->petta_translator_rule_len==0u);
    CHECK(library->petta_translator_symbol_table_instance==symbol_table_instance_id(&replacement));
    CHECK(library->petta_translator_rule_revision==translator_before+1u);
    CHECK(library->petta_admission_revision==admission_before+1u);
    CHECK(!cetta_branch_authority_token_equal(&changed.authority,&ordinary.authority));
    CettaBranchAdmission stable_again=petta_eval_machine_admit_branch_capture(&owner->context,&root);
    CHECK(cetta_branch_authority_token_equal(&stable_again.authority,&changed.authority));
    g_symbols=previous;
    (void)cetta_library_petta_translator_rule_revision(library);
    symbol_table_free(&replacement);
    free(control);free(owner);
    cetta_library_context_free(library);free(library);eval_set_library_context(NULL);
    space_free(&unrelated);space_free(&dependency);space_free(&root);arena_free(&arena);
}

static Atom *owning_consumer_field(Atom *record, const char *name) {
    assert(record && record->kind == ATOM_EXPR);
    for (CettaExprIndex i = 1u; i < record->expr.len; i++) {
        Atom *pair = record->expr.elems[i];
        if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u &&
                atom_is_symbol(pair->expr.elems[0], name))
            return pair->expr.elems[1];
    }
    return NULL;
}

/* Both holders are genuine owners. Equal context-local numbers do not
 * authorize inspection or execution of the other context's resource. */
static void native_owning_consumer_case(unsigned consumer, unsigned carrier,
                                       bool foreign) {
    size_t before = failures;
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry = &registry;
    registry_bind(&registry, "&self", atom_space(&arena, &space));
    space_add(&space, parse_one(&arena, "(= (left-job) LeftAnswer)"));
    space_add(&space, parse_one(&arena, "(= (right-job) RightAnswer)"));
    CettaLibraryContext *contexts[2] = {
        malloc(sizeof(*contexts[0])), malloc(sizeof(*contexts[1]))};
    assert(contexts[0] && contexts[1]);
    for (unsigned i = 0u; i < 2u; i++)
        cetta_library_context_init_for_language_profile(
            contexts[i], CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    Atom *handles[2] = {0}; uint64_t ids[2] = {0};
    PrimeNativeControl *controls[2] = {0};
    PrimeCostAnalysis *analyses[2] = {0};
    for (unsigned i = 0u; i < 2u; i++) {
        eval_set_library_context(contexts[i]);
        if (consumer < 4u) {
            controls[i] = malloc(sizeof(*controls[i])); assert(controls[i]);
            CettaSearchPolicySpec policy = {
                .native_control = true, .all = true, .retain = true,
                .workers = 1u, .work_budget = 1000u, .costs = true};
            assert(prime_native_control_init(controls[i], &space, &arena,
                parse_one(&arena, i ? "(right-job)" : "(left-job)"), -1, true,
                (CettaObservationDemand){.completion = CETTA_OBSERVATION_COMPLETE_BAG},
                &policy, NULL, NULL, NULL));
            assert(cetta_native_handle_alloc(contexts[i], "prime-search", controls[i],
                prime_native_control_destroy, &ids[i]));
            controls[i]->handle_id = ids[i];
            handles[i] = cetta_native_handle_owned_atom(
                contexts[i], &arena, "prime-search", ids[i]);
        } else {
            Atom *result = prime_cost_analyze(&space, &arena,
                parse_one(&arena, i ? "(quote (right-job))" : "(quote (left-job))"), 0u,
                CETTA_COST_GRAPH_UNFOLDING_WORK, false, false, false);
            assert(result);
            handles[i] = owning_consumer_field(result, "residual"); assert(handles[i]);
            assert(cetta_native_handle_arg(handles[i], "prime-cost-analysis", &ids[i]));
            analyses[i] = cetta_native_handle_get_owned(
                contexts[i], "prime-cost-analysis", handles[i]);
            assert(analyses[i]);
        }
        assert(handles[i]);
    }
    const char *kind = consumer < 4u ? "prime-search" : "prime-cost-analysis";
    CHECK(ids[0] == ids[1]);
    CHECK(cetta_native_handle_get_owned(contexts[0], kind, handles[0]) != NULL);
    CHECK(cetta_native_handle_get_owned(contexts[1], kind, handles[1]) != NULL);
    CHECK(cetta_native_handle_get_owned(contexts[0], kind, handles[1]) == NULL);
    CHECK(cetta_native_handle_get_owned(contexts[1], kind, handles[0]) == NULL);
    eval_set_library_context(contexts[0]);
    Atom *handle = handles[foreign ? 1u : 0u];
    if (carrier == 1u) {
        registry_bind(&registry, "&saved-owner", handle);
        handle = atom_symbol(&arena, "&saved-owner");
    } else if (carrier == 2u) {
        handle = atom_deep_copy(&arena, handle);
    }
    const char *heads[] = {"cost:inspect", "cost:trace", "cost:storage", "cost:run",
                          "cost:inspect", "cost:trace", "cost:storage", "cost:analyze"};
    Atom *expression = consumer == 7u
        ? atom_expr3(&arena, atom_symbol(&arena, heads[consumer]), handle, atom_int(&arena, 1000))
        : atom_expr2(&arena, atom_symbol(&arena, heads[consumer]), handle);
    PrimeNativeControl saved_controls[2]; PrimeCostAnalysis saved_analyses[2];
    if (consumer < 4u) {
        for (unsigned i = 0u; i < 2u; i++) saved_controls[i] = *controls[i];
    } else {
        for (unsigned i = 0u; i < 2u; i++) saved_analyses[i] = *analyses[i];
    }
    ResultSet result; result_set_init(&result);
    eval_top_with_registry(&space, &arena, &arena, &registry, expression, &result);
    bool held = result.len == 1u && result.items[0]->kind == ATOM_EXPR &&
        result.items[0]->expr.len > 0u &&
        atom_is_symbol(result.items[0]->expr.elems[0], heads[consumer]);
    CHECK(foreign ? held : !held);
    if (!foreign) {
        const char *expected = consumer == 2u || consumer == 6u ? "NativeStorage" :
            consumer == 3u ? "Observation" : consumer == 7u ? "CostAnalysis" : "NativeCost";
        CHECK(result.len == 1u && result.items[0]->kind == ATOM_EXPR &&
            result.items[0]->expr.len > 0u &&
            atom_is_symbol(result.items[0]->expr.elems[0], expected));
        if (consumer == 3u) {
            CHECK(result.len == 1u && result.items[0]->kind == ATOM_EXPR &&
                result.items[0]->expr.len > 1u &&
                atom_eq(result.items[0]->expr.elems[1], parse_one(&arena, "(LeftAnswer)")));
        } else if (consumer == 7u) {
            CHECK(result.len == 1u && atom_eq(
                owning_consumer_field(result.items[0], "result"), parse_one(&arena, "(Bound 1)")));
        }
    } else if (consumer < 4u) {
        for (unsigned i = 0u; i < 2u; i++)
            CHECK(!memcmp(&saved_controls[i], controls[i], sizeof(*controls[i])));
    } else {
        for (unsigned i = 0u; i < 2u; i++)
            CHECK(!memcmp(&saved_analyses[i], analyses[i], sizeof(*analyses[i])));
    }
    if (failures != before)
        fprintf(stderr, "owning consumer=%u carrier=%u foreign=%u\n", consumer, carrier, foreign);
    result_set_free(&result);
    for (unsigned i = 0u; i < 2u; i++) {
        eval_set_library_context(contexts[i]);
        cetta_library_context_free(contexts[i]); free(contexts[i]);
    }
    eval_set_library_context(NULL);
    registry_free(&registry); g_registry = NULL; space_free(&space); arena_free(&arena);
}

/* These flags are independent retained retry states. This boundary control
 * seeds each combination on a real owned frontier; program-level cases
 * separately establish the provider reasons reached by authored inputs. */
static void pending_zero_case(CettaLanguageId language,const CettaProfile *profile,
                              unsigned mask,bool worker,bool priority) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);
    eval_set_library_context(context);
    space_add(&space,parse_one(&arena,"(= (pending-result) 7)"));
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.costs=true,
        .workers=1u,.work_budget=UINT64_MAX,.priority=priority,.age_share=2u};
    PrimeNativeControl control;
    Atom *call=parse_one(&arena,"(pending-result)");
    assert(prime_native_control_init(&control,&space,&arena,call,-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
    bool scheduled=language==CETTA_LANGUAGE_PRIME
        ? prime_eval_stack_schedule_normalize(&space,&arena,call,-1,
            &control.base_environment,g_prime_need_evaluator_id,&control.output)
        : native_dialect_schedule(&control,&space,call,-1,
            &control.base_environment,NULL,&control.output);
    prime_native_run_leave(&guard);
    assert(scheduled && cetta_continuation_hub_length(&control.hub)==2u);
    control.driver.capacity_blocked=(mask&1u)!=0u;
    control.resource_pending=(mask&2u)!=0u;
    control.effect_pending=(mask&4u)!=0u;
    control.pending_effect=control.effect_pending?parse_one(&arena,"(println! DeferredEffect)"):NULL;
    control.need_pending=(mask&8u)!=0u;
    control.worker_execution=worker;
    PrimeNativeControl retained=control;
    CettaOwnedContinuation frontier[2];PrimeNativeWork packets[2];
    for(size_t i=0u;i<2u;i++) {
        frontier[i]=*cetta_continuation_hub_at(&control.hub,i);
        packets[i]=*(PrimeNativeWork *)frontier[i].payload;
    }
    Observed observed={0};
    for(unsigned repeat=0u;repeat<3u;repeat++) {
        prime_native_control_run(&control,0u,observe,&observed);
        CHECK(!memcmp(&retained,&control,sizeof(control)));
        CHECK(!memcmp(&retained.event_cost.total,&control.event_cost.total,sizeof(control.event_cost.total)));
        CHECK(!observed.count && !control.output.len);
        for(size_t i=0u;i<2u;i++) {
            CHECK(!memcmp(&frontier[i],cetta_continuation_hub_at(&control.hub,i),sizeof(frontier[i])));
            CHECK(!memcmp(&packets[i],frontier[i].payload,sizeof(packets[i])));
        }
    }
    /* A coordinator may retry the same work with a positive allowance.
     * Cancellation before the operation preserves each outstanding reason. */
    control.worker_execution=false;
    _Atomic bool cancel=true;
    _Atomic bool *old_cancel=eval_set_cancel_token(&cancel);
    prime_native_control_run(&control,1u,observe,&observed);
    (void)eval_set_cancel_token(old_cancel);
    CHECK(control.steps==retained.steps && !observed.count);
    CHECK(control.driver.capacity_blocked==retained.driver.capacity_blocked);
    CHECK(control.resource_pending==retained.resource_pending);
    CHECK(control.effect_pending==retained.effect_pending && control.pending_effect==retained.pending_effect);
    CHECK(control.need_pending==retained.need_pending);
    CHECK(control.completion==CETTA_EVAL_INCOMPLETE_CANCELLED);
    prime_native_control_run(&control,1000u,observe,&observed);
    CHECK(observed.count==2u && atom_eq(observed.value,atom_int(&arena,7)));
    CHECK(prime_native_control_closed(&control));
    CHECK(!control.driver.capacity_blocked && !control.resource_pending &&
        !control.effect_pending && !control.pending_effect && !control.need_pending);
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static void pending_zero_cases(void) {
    const CettaLanguageId languages[]={CETTA_LANGUAGE_PRIME,CETTA_LANGUAGE_HE,CETTA_LANGUAGE_PETTA};
    const CettaProfile *profiles[]={cetta_profile_prime_default(),cetta_profile_he_extended(),cetta_profile_petta_extended()};
    for(size_t host=0u;host<3u;host++)
        for(unsigned mask=0u;mask<16u;mask++)
            for(unsigned worker=0u;worker<2u;worker++)
                for(unsigned priority=0u;priority<2u;priority++)
                    pending_zero_case(languages[host],profiles[host],mask,worker!=0u,priority!=0u);
    puts("PASS: zero grants retain complete waiting flags, paid account and frontier; cancellation preserves reasons and positive grants retry");
}

static void pending_paid_boundary_case(CettaLanguageId language,const CettaProfile *profile,bool best) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);
    eval_set_library_context(context);
    space_add(&space,parse_one(&arena,"(= (paid-boundary-result) 7)"));
    space_add(&space,parse_one(&arena,"(= (paid-slow 0) 8)"));
    space_add(&space,parse_one(&arena,"(= (paid-slow $n) (if (> $n 0) (paid-slow (- $n 1)) (empty)))"));
    Atom *variable=parse_one(&arena,"$value");
    CettaSearchPolicySpec policy={.native_control=true,.all=!best,.costs=true,
        .workers=1u,.work_budget=UINT64_MAX,.priority=1,.age_share=2u,
        .selection=best?2:0,.goal_pattern=variable,.goal_expression=variable};
    PrimeNativeControl control;
    Atom *call=parse_one(&arena,"(paid-boundary-result)");
    assert(prime_native_control_init(&control,&space,&arena,call,-1,false,
        (CettaObservationDemand){.completion=best?CETTA_OBSERVATION_FINITE_PREFIX:CETTA_OBSERVATION_COMPLETE_BAG,.prefix_limit=best?1u:0u},
        &policy,NULL,NULL,NULL));
    PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
    Atom *second=best?parse_one(&arena,"(paid-slow 5)"):call;
    bool scheduled=language==CETTA_LANGUAGE_PRIME
        ? prime_eval_stack_schedule_normalize(&space,&arena,second,-1,
            &control.base_environment,g_prime_need_evaluator_id,&control.output)
        : native_dialect_schedule(&control,&space,second,-1,
            &control.base_environment,NULL,&control.output);
    if(best) {
        Atom *third=parse_one(&arena,"(paid-slow 7)");
        scheduled=scheduled && (language==CETTA_LANGUAGE_PRIME
            ? prime_eval_stack_schedule_normalize(&space,&arena,third,-1,
                &control.base_environment,g_prime_need_evaluator_id,&control.output)
            : native_dialect_schedule(&control,&space,third,-1,
                &control.base_environment,NULL,&control.output));
    }
    prime_native_run_leave(&guard);
    assert(scheduled);
    Observed observed={0};
    if(best) {
        unsigned rounds=0u;
        while(!(control.output.len && control.costs_completed==control.output.len &&
                control.published==control.output.len && !control.goal_found)) {
            assert(rounds++<1000u && !control.goal_found);
            prime_native_control_run(&control,1u,observe,&observed);
        }
        assert(!observed.count && cetta_continuation_hub_length(&control.hub)>1u);
    } else {
        /* An actual owned output slot is already complete. Its delivery is
         * allowed with zero evaluator allowance; unprepared work is not. */
        outcome_set_add(&control.output,atom_int(&arena,99),&control.base_environment);
        control.resource_pending=true;
        control.effect_pending=true;control.pending_effect=call;
        control.need_pending=true;control.driver.capacity_blocked=true;
    }
    uint64_t steps=control.steps;
    CettaNativeCostSnapshot paid=control.event_cost.total;
    size_t event_count=control.event_cost.length;
    prime_native_control_run(&control,0u,observe,&observed);
    if(!best) {
        paid.counts[CETTA_COST_PUBLISH]++;paid.work++;paid.span++;
        CHECK(observed.count==1u && atom_eq(observed.value,atom_int(&arena,99)));
        CHECK(control.resource_pending && control.effect_pending && control.pending_effect==call &&
            control.need_pending && control.driver.capacity_blocked);
    } else CHECK(!observed.count && !control.goal_found);
    CHECK(control.steps==steps);
    CHECK(!memcmp(&paid,&control.event_cost.total,sizeof(paid)));
    CHECK(control.event_cost.length>=event_count && control.event_cost.length<=event_count+(!best));
    PrimeNativeControl retained=control;
    for(unsigned retry=0u;retry<3u;retry++) {
        prime_native_control_run(&control,0u,observe,&observed);
        CHECK(!memcmp(&retained,&control,sizeof(control)));
    }
    prime_native_control_run(&control,1000u,observe,&observed);
    CHECK(observed.count==(best?1u:3u));
    CHECK(prime_native_control_closed(&control));
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static void pending_paid_boundary_cases(void) {
    const CettaLanguageId languages[]={CETTA_LANGUAGE_PRIME,CETTA_LANGUAGE_HE,CETTA_LANGUAGE_PETTA};
    const CettaProfile *profiles[]={cetta_profile_prime_default(),cetta_profile_he_extended(),cetta_profile_petta_extended()};
    for(size_t host=0u;host<3u;host++)
        for(unsigned best=0u;best<2u;best++)
            pending_paid_boundary_case(languages[host],profiles[host],best!=0u);
    puts("PASS: paid delivery stops before unpaid priority inspection; completed best costs do not grant a frontier scan");
}

/* A child projects to its own support; its retained siblings and returned
 * private payload still need the complete continuation packet. */
static void normalization_support_bound(Arena *arena, Bindings *env, Atom *variable) {
    Atom *value = bindings_apply_if_vars(env, arena, variable);
    assert(value && value->kind == ATOM_GROUNDED &&
           value->ground.gkind == GV_INT && value->ground.ival == 7);
}

static void normalization_support_case(Space *space, Arena *arena, const char *name) {
    bool sibling = strcmp(name, "sibling") == 0;
    bool tuple = strcmp(name, "tuple-returned") == 0 ||
                 strcmp(name, "tuple-error") == 0;
    bool error = strcmp(name, "error-parent") == 0 ||
                 strcmp(name, "error-child") == 0 ||
                 strcmp(name, "tuple-error") == 0;
    bool parent_support = sibling || strcmp(name, "error-parent") == 0;
    Bindings empty, allocated, parent_env, reply_env;
    bindings_init(&empty);
    Atom *variable = atom_var(arena, "support-value");
    Atom *ref = NULL;
    assert(prime_need_allocate_ref(arena, variable, &empty, &allocated, &ref));
    assert(bindings_clone(&parent_env, &allocated));
    assert(bindings_clone(&reply_env, &allocated));
    assert(bindings_add_var(parent_support ? &parent_env : &reply_env,
                            variable, atom_int(arena, 7)));
    Atom *head = atom_symbol(arena, "Support");
    Atom *source = atom_expr2(arena, head,
        parent_support ? ref : atom_symbol(arena, "old-child"));
    OutcomeSet out;
    outcome_set_init(&out);
    PrimeEvalStackFrame *parent = prime_eval_stack_tuple_frame_new(
        space, arena, source, NULL, sibling ? 0u : 1u, -1,
        &parent_env, 0u, true, &out);
    assert(parent);
    parent->tuple_bind = tuple;
    parent->normalization_value_ready = true;
    Outcome item;
    outcome_init(&item);
    item.atom = error ? atom_error(arena, ref, atom_symbol(arena, "Expected"))
        : sibling ? head : ref;
    assert(bindings_clone(&item.env, &reply_env));
    bool emitted = false, failed = false;
    PrimeEvalStackFrame *next = prime_eval_stack_normalize_child_frame(
        parent, &item, &emitted, &failed);
    assert(!failed);
    if (error) {
        assert(!next && emitted && out.len == 1u);
        normalization_support_bound(arena, &out.items[0].env, variable);
        assert(atom_is_error(out.items[0].atom));
    } else {
        assert(next && !emitted && !out.len);
        normalization_support_bound(arena, &next->env, variable);
        assert(next->normalization_children[parent->normalization_child_index]
            == item.atom);
        assert(next->tuple_bind == tuple && next->normalization_value_ready);
        assert(parent->normalization_children[parent->normalization_child_index]
            == source->expr.elems[parent->normalization_child_index]);
        prime_eval_stack_frame_free(next);
    }
    outcome_free_fields(&item);
    prime_eval_stack_frame_free(parent);
    outcome_set_free(&out);
    bindings_free(&reply_env);
    bindings_free(&parent_env);
    bindings_free(&allocated);
    bindings_free(&empty);
    printf("Support %s passed\n", name);
}

static void normalization_support_world_case(Space *space, Arena *arena) {
    Bindings empty, allocated;
    bindings_init(&empty);
    Atom *ref = NULL;
    assert(prime_need_allocate_ref(arena, atom_int(arena, 7),
                                  &empty, &allocated, &ref));
    uint64_t id = 0u, evaluator = prime_need_fresh_evaluator_id();
    assert(prime_need_ref_belongs_to(ref, bindings_need_view(&allocated), &id));
    PrimeNeedSnapshot evaluating, left, right;
    assert(prime_need_snapshot_start_evaluation(arena,
        bindings_need_view(&allocated), id, evaluator, &evaluating));
    assert(prime_need_snapshot_resolve_value(arena, &evaluating, id,
        evaluator, atom_int(arena, 7), &left));
    assert(prime_need_snapshot_resolve_value(arena, &evaluating, id,
        evaluator, atom_int(arena, 9), &right));
    Atom *head = atom_symbol(arena, "Support");
    Atom *source = atom_expr2(arena, head, ref);
    OutcomeSet out;
    outcome_set_init(&out);
    PrimeEvalStackFrame *parent = prime_eval_stack_tuple_frame_new(
        space, arena, source, NULL, 0u, -1, &allocated, 0u, true, &out);
    assert(parent);
    parent->tuple_bind = false;
    Outcome item;
    outcome_init(&item);
    item.atom = head;
    *bindings_need_mut(&item.env) = left;
    bool emitted = false, failed = false;
    PrimeEvalStackFrame *next = prime_eval_stack_normalize_child_frame(
        parent, &item, &emitted, &failed);
    assert(next && !failed && !emitted);
    PrimeNeedCellView cell;
    assert(prime_need_snapshot_lookup(bindings_need_view(&next->env), id, &cell));
    assert(cell.cache_state == PRIME_NEED_CACHE_VALUE &&
        cell.cached->ground.ival == 7 && cell.value_role == PRIME_NEED_VALUE_DEMAND_RESULT);
    *bindings_need_mut(&parent->env) = left;
    *bindings_need_mut(&item.env) = right;
    PrimeEvalStackFrame *refused = prime_eval_stack_normalize_child_frame(
        parent, &item, &emitted, &failed);
    assert(!refused && !failed && !emitted && !out.len);
    assert(prime_need_snapshot_lookup(bindings_need_view(&parent->env), id, &cell));
    assert(cell.cached->ground.ival == 7);
    prime_eval_stack_frame_free(next);
    prime_eval_stack_frame_free(parent);
    outcome_free_fields(&item);
    outcome_set_free(&out);
    bindings_free(&allocated);
    bindings_free(&empty);
    puts("Support selected-world passed");
}

static void normalization_tuple_return_case(Space *space, Arena *arena,
                                            bool with_caller) {
    Bindings empty, allocated, caller;
    bindings_init(&empty);
    bindings_init(&caller);
    Atom *variable = atom_var(arena, "support-value");
    Atom *ref = NULL;
    assert(prime_need_allocate_ref(arena, variable,
                                  &empty, &allocated, &ref));
    assert(bindings_add_var(with_caller ? &caller : &allocated,
                            variable, atom_int(arena, 7)));
    Atom *head = atom_symbol(arena, "Support");
    OutcomeSet values;
    outcome_set_init(&values);
    outcome_set_add(&values, atom_expr2(arena, head, ref), &allocated);
    prime_need_observe_tuple_returns(space, arena, &values, -1,
                                    with_caller ? &caller : NULL, NULL);
    assert(values.len == 1u);
    Atom *value = outcome_atom_materialize(arena, &values.items[0]);
    assert(atom_eq(value, atom_expr2(arena, head, atom_int(arena, 7))));
    assert(!prime_need_atom_has_observable_ref(value));
    uint64_t id = 0u;
    assert(prime_need_ref_belongs_to(ref, bindings_need_view(&allocated), &id));
    PrimeNeedCellView cell;
    assert(prime_need_snapshot_lookup(bindings_need_view(&values.items[0].env),
                                     id, &cell));
    assert(cell.cache_state == PRIME_NEED_CACHE_VALUE &&
           cell.cached->ground.gkind == GV_INT && cell.cached->ground.ival == 7);
    assert(prime_need_snapshot_lookup(bindings_need_view(&allocated), id, &cell));
    assert(cell.cache_state == PRIME_NEED_CACHE_EMPTY);
    outcome_set_free(&values);
    bindings_free(&caller);
    bindings_free(&allocated);
    bindings_free(&empty);
    printf("Support tuple-%s-caller passed\n", with_caller ? "with" : "without");
}

static void owned_authority_refusal_cases(void) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    Atom *forbidden=atom_expr2(&arena,atom_symbol_id(&arena,g_builtin_syms.println_bang),atom_int(&arena,7));
    EvalCompletionTracker *previous_completion = g_eval_completion_tracker;
    PrimeEvalStackDriver *previous_driver = g_prime_eval_stack_driver;
    EvalCompletionTracker completion={0}; g_eval_completion_tracker=&completion;
    PrimeNativeControl owner={0}, inner={0};
    PrimeEvalStackDriver driver={.pure_authority=true};
    g_prime_eval_stack_driver=&driver;
    CHECK(!prime_eval_check_call_authority(&space,&arena,forbidden));
    CHECK(driver.failed && completion.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    puts("Unowned authority refusal propagates");
    completion=(EvalCompletionTracker){0};
    driver=(PrimeEvalStackDriver){.pure_authority=true,.control=&owner};
    CHECK(!prime_eval_check_call_authority(&space,&arena,forbidden));
    CHECK(driver.failed && owner.failed && owner.completion==CETTA_EVAL_COMPLETE);
    CHECK(completion.completion==CETTA_EVAL_COMPLETE);
    puts("Local owned authority refusal stays owned");
    owner=(PrimeNativeControl){0};
    PrimeEvalStackDriver parent={.control=&owner};
    driver=(PrimeEvalStackDriver){.pure_authority=true,.authority_parent=&parent};
    CHECK(!prime_eval_check_call_authority(&space,&arena,forbidden));
    CHECK(driver.failed && owner.failed && completion.completion==CETTA_EVAL_COMPLETE);
    puts("Synchronous child refusal reaches actual owner");
    owner=(PrimeNativeControl){0}; parent.pure_authority=true;
    driver=(PrimeEvalStackDriver){.control=&inner,.authority_parent=&parent};
    CHECK(!prime_eval_check_call_authority(&space,&arena,forbidden));
    CHECK(parent.failed && owner.failed && !inner.failed && !driver.failed);
    CHECK(completion.completion==CETTA_EVAL_COMPLETE);
    puts("Nested observation cannot catch ancestor denial");
    completion=(EvalCompletionTracker){.completion=CETTA_EVAL_INCOMPLETE_CAPACITY};
    owner=(PrimeNativeControl){0};
    driver=(PrimeEvalStackDriver){.pure_authority=true,.control=&owner};
    CHECK(!prime_eval_check_call_authority(&space,&arena,forbidden));
    CHECK(completion.completion==CETTA_EVAL_INCOMPLETE_CAPACITY);
    puts("Prior capacity failure is retained");
    for (unsigned i=0;i<3;i++) {
        completion=(EvalCompletionTracker){0};
        EvalCompletionTracker child; eval_completion_child_begin(&child);
        OutcomeSet outcomes; outcome_set_init(&outcomes);
        Bindings env; bindings_init(&env);
        PettaMachineStep step=i==0 ? PETTA_MACHINE_STEP_HOST_ERROR :
            i==1 ? PETTA_MACHINE_STEP_STACK : PETTA_MACHINE_STEP_CAPACITY;
        CettaEvalCompletion expected=i==0 ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE :
            i==1 ? CETTA_EVAL_INCOMPLETE_STACK : CETTA_EVAL_INCOMPLETE_CAPACITY;
        petta_eval_machine_record_stop(&arena,&outcomes,forbidden,step,&env,&env,false);
        CHECK(outcomes.len==1 && eval_completion_child_end(&child)==expected);
        CHECK(completion.completion==expected);
        outcome_set_free(&outcomes); bindings_free(&env);
    }
    puts("Nested host stack capacity failures propagate");
    g_prime_eval_stack_driver=previous_driver; g_eval_completion_tracker=previous_completion;
    space_free(&space); arena_free(&arena);
}

static PrimeNativeAdvice *return_advice_cell(PrimeNativeControl *control,Arena *arena,int key) {
    PrimeNativeAdvice *item=calloc(1u,sizeof(*item));assert(item);
    item->key=atom_int(arena,key);bindings_init(&item->environment);
    item->next=control->advice_cells;control->advice_cells=item;return item;
}
static PrimeEvalStackFrame *return_advice_receiver(PrimeNativeControl *control,
    PrimeEvalStackFrameKind kind,OutcomeSet *target) {
    PrimeEvalStackFrame *frame=prime_eval_stack_frame_new(kind);
    frame->space=control->space;frame->arena=control->driver.arena;
    frame->target=target;frame->state=PRIME_EVAL_STACK_FRAME_WAIT_CALL;
    frame->control_pending=1u;
    bindings_init(&frame->env);frame->env_initialized=true;
    outcome_set_init(&frame->child);frame->child_initialized=true;
    control->driver.top=NULL;
    prime_native_control_register_frame(control,frame);
    return frame;
}
static void *regrade_agenda_advice(void *context, Atom *exec,
    const Bindings *bindings, Atom *authored) {
    (void)exec; (void)bindings; (void)authored;
    return context;
}
static bool regrade_agenda_before(void *context, const void *left, const void *right) {
    (void)context; (void)left; (void)right;
    return false;
}
static void regrade_dependency_cases(void) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena,NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_default_fuel(-1); eval_set_library_context(&context);
    PrimeNativeControl control;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
        .priority=2u,.workers=1u,.work_budget=UINT64_MAX};
    assert(prime_native_control_init(&control,&space,&arena,atom_int(&arena,17),-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
    PrimeNativeAdvice *holder=return_advice_cell(&control,&arena,7);
    PrimeNativeAdvice *middle=return_advice_cell(&control,&arena,7);
    PrimeNativeAdvice *leaf=return_advice_cell(&control,&arena,7);
    PrimeNativeAdvice *peer=return_advice_cell(&control,&arena,7);
    middle->inherited=middle->follows=holder;
    leaf->inherited=leaf->follows=middle;
    assert(prime_native_advice_depends_on(leaf,holder));
    assert(!prime_native_advice_depends_on(leaf,peer));
    PrimeNativeWork *queued=cetta_continuation_hub_at(&control.hub,0u)->payload;
    queued->advice=leaf;
    assert(prime_native_control_advice_live(&control,holder));
    assert(!prime_native_control_advice_live(&control,peer));
    queued->advice=NULL;
    PrimeNativeWork pending={.advice=leaf};
    CettaOwnedContinuation packet={.payload=&pending};
    control.parked=&packet; control.parked_count=1u;
    assert(prime_native_control_advice_live(&control,holder));
    control.parked=NULL; control.parked_count=0u;
    CettaOwnedContinuation inflight[2]={{0},packet};
    control.parallel_inflight=inflight; control.parallel_inflight_count=2u;
    assert(prime_native_control_advice_live(&control,holder));
    pending.advice=NULL;
    assert(!prime_native_control_advice_live(&control,holder));
    pending.advice=leaf;
    control.parallel_inflight=NULL; control.parallel_inflight_count=0u;
    assert(control.frames==NULL);
    PrimeEvalStackFrame frame={.kind=PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE,
        .control_advice=leaf,.dialect_source_success=true};
    control.frames=&frame;
    assert(prime_native_control_advice_live(&control,holder));
    frame.control_advice=NULL;
    PrimeNativeReturnAdvice rows[2]={{.advice=leaf},{.advice=middle}};
    frame.control_return_advice=rows; frame.control_return_advice_length=2u;
    assert(prime_native_control_advice_live(&control,holder));
    frame.index=1u;
    assert(prime_native_control_advice_live(&control,holder));
    frame.index=2u;
    assert(!prime_native_control_advice_live(&control,holder));
    frame.dialect_source_success=false; frame.dialect_error_index=1u;
    assert(prime_native_control_advice_live(&control,holder));
    frame.control_closed=true;
    assert(!prime_native_control_advice_live(&control,holder));
    control.frames=NULL;
    middle->follows=NULL;
    queued->advice=leaf;
    assert(!prime_native_control_advice_live(&control,holder));
    assert(prime_native_control_advice_live(&control,middle));
    middle->follows=holder; queued->advice=NULL;
    Space store; space_init(&store);
    Atom **parsed=NULL; int count=parse_metta_text("(exec 0 (,) (O (+ Done)))",&arena,&parsed);
    assert(count==1 && space_admit_atom(&store,&arena,parsed[0])); free(parsed);
    Bindings empty; bindings_init(&empty);
    control.space_agenda=cetta_space_agenda_new(&store,&arena,&empty,
        (CettaSpaceAgendaContract){.set_support=true,.raw_mm2_order=true});
    assert(control.space_agenda);
    CettaSelectionAutomaton schedule; assert(cetta_selection_automaton_fifo(&schedule));
    assert(cetta_space_agenda_control(control.space_agenda,&schedule,
        regrade_agenda_advice,regrade_agenda_before,leaf));
    for (unsigned i=0u;i<100u && !cetta_space_agenda_has_advice(control.space_agenda,leaf);++i)
        assert(cetta_space_agenda_step(control.space_agenda)!=CETTA_SPACE_AGENDA_CLOSED);
    assert(cetta_space_agenda_has_advice(control.space_agenda,leaf));
    assert(prime_native_control_advice_live(&control,holder));
    assert(!prime_native_control_advice_live(&control,peer));
    cetta_space_agenda_free(control.space_agenda); control.space_agenda=NULL;
    bindings_free(&empty); space_free(&store);
    PrimeNativeAdvice cycle[3]={{0}};
    cycle[0].follows=&cycle[1]; cycle[1].follows=&cycle[2]; cycle[2].follows=&cycle[1];
    assert(prime_native_advice_depends_on(&cycle[0],&cycle[2]));
    assert(!prime_native_advice_depends_on(&cycle[0],peer));
    puts("Regrade follows retained advice identities through shared rows and agenda roots");
    prime_native_control_free(&control); outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry=NULL; space_free(&space); arena_free(&arena);
}

static void return_advice_cases(bool priority) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_default_fuel(-1);eval_set_library_context(&context);
    PrimeNativeControl control;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
        .priority=priority?2u:0u,.workers=1u,.work_budget=UINT64_MAX};
    assert(prime_native_control_init(&control,&space,&arena,atom_int(&arena,17),-1,false,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
    PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
    PrimeNativeAdvice *parent=return_advice_cell(&control,&arena,99);
    PrimeNativeAdvice *first=return_advice_cell(&control,&arena,9),*second=return_advice_cell(&control,&arena,1);
    control.active_advice=parent;control.active_key=parent->key;
    PrimeEvalStackFrame *sink=return_advice_receiver(&control,PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM,&control.output);
    PrimeEvalStackFrame *source=return_advice_receiver(&control,PRIME_EVAL_STACK_FRAME_BIND_FINISH,&sink->child);
    Atom *same=atom_int(&arena,7);
    for (size_t i=0u;i<2u;i++) {
        control.active_advice=i?second:first;control.active_key=control.active_advice->key;
        outcome_set_add(&source->child,same,&control.base_environment);
        prime_eval_stack_stream_note(&source->child);
    }
    assert(source->child.len==2u && source->child.items[0].atom==source->child.items[1].atom);
    PrimeNativeWork *work=NULL;
    for(size_t i=0u;i<cetta_continuation_hub_length(&control.hub);i++) {
        PrimeNativeWork *p=cetta_continuation_hub_at(&control.hub,i)->payload;
        if(p->frame==source)work=p;
    }
    assert(work);
    control.active_advice=parent;control.active_key=parent->key;
    if(priority) {
        assert(work->advice==first && work->key==first->key);
        assert(source->control_return_advice_length==2u);
        assert(source->control_return_advice[0].advice==first);
        assert(source->control_return_advice[1].advice==second);
        PrimeNativeWork peer={.key=atom_int(&arena,5)};
        assert(!prime_native_work_key_less(&control,work,&peer));
        first->key=atom_int(&arena,0);
        assert(prime_native_work_key_less(&control,work,&peer));
        puts("Physical next row and late advice identity retained");
    } else {
        assert(!source->control_return_advice && !source->control_return_advice_length);
        assert(!source->control_return_advice_capacity);
        puts("Ungraded Prime return sidecar absent");
    }
    assert(prime_eval_stack_forward_bind(source)==OUTCOME_APPEND_COMMITTED);
    assert(source->stream_forwarded==2u && sink->child.len==2u);
    assert(control.active_advice==parent && control.active_key==parent->key);
    assert(source->control_advice==parent && sink->control_advice==parent);
    if(priority) {
        assert(sink->control_return_advice_length==2u);
        assert(sink->control_return_advice[0].advice==first);
        assert(sink->control_return_advice[1].advice==second);
        assert(sink->child.items[0].atom==sink->child.items[1].atom);
        puts("Equal payload occurrences forward distinct advice without replacing parent");
        size_t bytes=sink->control_return_advice_capacity*sizeof(*sink->control_return_advice);
        assert(bytes>=2u*sizeof(*sink->control_return_advice));
        printf("Return sidecar %zu entries %zu bytes\n",sink->control_return_advice_capacity,bytes);
    }
    prime_native_run_leave(&guard);
    if(priority) {
        size_t length=source->control_return_advice_length;
        unsigned char *saved=malloc(length*sizeof(*source->control_return_advice));assert(saved);
        memcpy(saved,source->control_return_advice,length*sizeof(*source->control_return_advice));
        uint64_t steps=control.steps;CettaSelectionAutomaton schedule=control.hub.schedule;
        prime_native_control_run(&control,0u,NULL,NULL);
        assert(control.steps==steps && !memcmp(&schedule,&control.hub.schedule,sizeof(schedule)));
        assert(!memcmp(saved,source->control_return_advice,length*sizeof(*source->control_return_advice)));
        free(saved);puts("Zero allowance preserves exact retained return metadata");
    }
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}
static void retained_read_service_cases(void) {
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    Space source, foreign;
    space_init(&source);
    space_init(&foreign);
    Registry registry;
    registry_init(&registry);
    g_registry = &registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_default_fuel(-1);
    eval_set_library_context(&context);
    g_prime_need_owner = g_prime_need_episode_owner = &arena;
    space_add(&source, parse_one(&arena, "(= (read-service) (superpose (1 2)))"));
    assert(prime_ordinary_shared_source_admitted(&source));
    PrimeNativeControl parent;
    CettaSearchPolicySpec policy = {.native_control=true, .all=true,
        .work_budget=UINT64_MAX, .workers=2u};
    assert(prime_native_control_init(&parent, &source, &arena,
        parse_one(&arena, "(read-service)"), -1, true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, NULL));
    parent.driver.ordinary_ordered = true;
    parent.driver.ordinary_shared_source = true;
    PrimeNativeWork *work = cetta_continuation_hub_at(&parent.hub, 0u)->payload;
    PrimeNativeRunGuard guard;
    prime_native_run_enter(&parent, &guard);
    assert(prime_native_parallel_prepare(&parent, work));
    prime_native_run_leave(&guard);
    PrimeNativeParallel *parallel = work->parallel;
    PrimeNativeControl *child = parallel->child;
    assert(parallel->borrowed_source.root.space == &source);
    assert(child->driver.ordinary_ordered && !child->driver.capsule_stack);
    Arena *owned_arena = child->driver.arena;
    Space *owned_space = parallel->world.space;
    Atom *call = parse_one(owned_arena, "(superpose (1 2))");
    Atom *mutation = parse_one(owned_arena, "(add-atom &self read-service-forbidden)");
    Registry *saved_registry = g_registry;
    g_registry = &parallel->world.registry;
    PrimeNativeParallel *saved_parallel = g_prime_native_parallel_current;
    g_prime_native_parallel_current = parallel;
    prime_native_run_enter(child, &guard);
    cetta_shared_transition_scope_enter();
    CettaSharedTransitionGuard transition = {0};
    cetta_shared_transition_guard_enter(&transition);
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    puts("Queued owned read service admitted");
    assert(prime_ordinary_capsule_stack_start(child));
    assert(!child->driver.ordinary_ordered && child->driver.capsule_stack);
    PrimeEvalStackTask task = child->driver.task;
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    puts("Retained stack read service admitted");
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, mutation, &child->output));
    assert(!prime_native_parallel_pin_effect(parallel));
    assert(!parallel->resource_owner_pinned && !parallel->coordinator_only);
    puts("Persistent mutation and borrowed pin refused");
    parallel->child = &parent;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    parallel->child = child;
    assert(!prime_native_parallel_read_service_admitted(child, &foreign,
        owned_arena, call, &child->output));
    OutcomeSet unowned;
    outcome_set_init(&unowned);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &unowned));
    Arena *saved_owner = child->output.payload_owner;
    child->output.payload_owner = &arena;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->output.payload_owner = saved_owner;
    puts("Wrong owner source target and payload refused");
    child->driver.capsule_stack = false;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->driver.capsule_stack = true;
    child->driver.ordinary_shared_source = false;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->driver.ordinary_shared_source = true;
    cetta_shared_transition_guard_leave(&transition);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    cetta_shared_transition_guard_enter(&transition);
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    assert(!memcmp(&task, &child->driver.task, sizeof(task)));
    assert(!child->steps && !parent.steps && !child->output.len);
    puts("Representation source and coordinator guard required without debit");
    cetta_shared_transition_guard_leave(&transition);
    cetta_shared_transition_scope_leave();
    space_add(&source, parse_one(&arena, "(= changed-read-source 9)"));
    cetta_shared_transition_scope_enter();
    cetta_shared_transition_guard_enter(&transition);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    cetta_shared_transition_guard_leave(&transition);
    cetta_shared_transition_scope_leave();
    puts("Changed source revision refused");
    prime_native_run_leave(&guard);
    g_prime_native_parallel_current = saved_parallel;
    g_registry = saved_registry;
    outcome_set_free(&unowned);
    release_stack_control(&parent);
    eval_set_library_context(NULL);
    cetta_library_context_free(&context);
    g_prime_need_owner = g_prime_need_episode_owner = NULL;
    registry_free(&registry);
    g_registry = NULL;
    space_free(&foreign);
    space_free(&source);
    arena_free(&arena);
}

static void normalization_support_cases(void) {
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    Space space;
    space_init(&space);
    Registry registry;
    registry_init(&registry);
    g_registry = &registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_default_fuel(-1);
    eval_set_library_context(&context);
    g_prime_need_owner = g_prime_need_episode_owner = &arena;
    const char *cases[] = {"sibling", "returned", "tuple-returned",
        "error-parent", "error-child", "tuple-error"};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++)
        normalization_support_case(&space, &arena, cases[i]);
    normalization_support_world_case(&space, &arena);
    normalization_tuple_return_case(&space, &arena, false);
    normalization_tuple_return_case(&space, &arena, true);
    eval_set_library_context(NULL);
    cetta_library_context_free(&context);
    g_prime_need_owner = g_prime_need_episode_owner = NULL;
    registry_free(&registry);
    g_registry = NULL;
    space_free(&space);
    arena_free(&arena);
}

static void worker_symbol_source_checks(Space *space,Arena *arena) {
    Space independent;space_init(&independent);
    Atom *rule=parse_one(arena,"(= retained-coin heads)");
    space_add(space,rule);space_add(&independent,rule);
    Atom *symbol=atom_symbol(arena,"retained-coin");
    SymbolId head=atom_symbol_current_id(symbol);
    EvalContextState initial=eval_context_capture();
    g_prime_symbol_scope=prime_need_fresh_resample_scope();
    assert(g_prime_symbol_scope);
    PrimeSymbolWorld original=prime_symbol_world(space,head);
    PrimeSymbolWorld unrelated=prime_symbol_world(&independent,head);
    assert(original.instance!=unrelated.instance);
    PrimeNativeControl parent;
    CettaSearchPolicySpec policy={.native_control=true,.all=true,
        .work_budget=UINT64_MAX,.workers=2u};
    assert(prime_native_control_init(&parent,space,arena,parse_one(arena,"(P retained-coin retained-coin)"),
        -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy,NULL,NULL,NULL));
    assert(cetta_continuation_hub_length(&parent.hub)==1u);
    PrimeNativeWork *work=cetta_continuation_hub_at(&parent.hub,0u)->payload;
    PrimeNativeRunGuard guard;prime_native_run_enter(&parent,&guard);
    assert(prime_native_parallel_prepare(&parent,work));
    prime_native_run_leave(&guard);
    PrimeNativeParallel *first=work->parallel;
    assert(first && first->world.space!=space && !parent.steps && !first->child->steps);
    eval_context_restore(&first->context);
    PrimeSymbolWorld image=prime_symbol_world(first->world.space,head);
    assert(prime_symbol_world_eq(&original,&image));
    assert(g_prime_symbol_scope==original.scope);
    image=prime_symbol_world(&independent,head);
    assert(prime_symbol_world_eq(&image,&unrelated));
    PrimeNativeControl *child=first->child;
    PrimeNativeWork *nested=cetta_continuation_hub_at(&child->hub,0u)->payload;
    prime_native_run_enter(child,&guard);
    assert(prime_native_parallel_prepare(child,nested));
    prime_native_run_leave(&guard);
    PrimeNativeParallel *second=nested->parallel;
    assert(second && second->world.space!=first->world.space);
    eval_context_restore(&second->context);
    image=prime_symbol_world(second->world.space,head);
    assert(prime_symbol_world_eq(&original,&image));
    uint64_t second_physical=space_instance_id(second->world.space);
    assert(second_physical!=original.instance);
    EvalContextState saved=eval_context_capture();
    g_prime_symbol_scope=prime_need_fresh_resample_scope();
    image=prime_symbol_world(second->world.space,head);
    assert(image.instance==original.instance && image.scope!=original.scope);
    eval_context_restore(&saved);
    image=prime_symbol_world(second->world.space,head);
    assert(prime_symbol_world_eq(&original,&image));
    g_prime_symbol_source.admission++;
    image=prime_symbol_world(second->world.space,head);
    assert(image.instance==second_physical);
    eval_context_restore(&saved);
    space_add(second->world.space,parse_one(arena,"(= retained-other 9)"));
    image=prime_symbol_world(second->world.space,head);
    assert(image.instance==second_physical && image.equations!=original.equations);
    eval_context_restore(&first->context);
    space_add(space,parse_one(arena,"(= retained-source-change 11)"));
    image=prime_symbol_world(first->world.space,head);
    assert(prime_symbol_world_eq(&original,&image));
    assert(!prime_native_parallel_validate(&parent,first));
    eval_context_restore(&initial);
    release_stack_control(&parent);
    space_free(&independent);
    puts("Worker source actual-copy nested independent revision admission scope passed");
}

static void worker_symbol_source_cases(void) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_default_fuel(-1);eval_set_library_context(&context);
    g_prime_need_owner=g_prime_need_episode_owner=&arena;
    worker_symbol_source_checks(&space,&arena);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
    g_prime_need_owner=g_prime_need_episode_owner=NULL;
    registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}

static bool seen_reference_equal(BindingValue left, BindingValue right) {
    if (left.skeleton != right.skeleton || binding_value_kind(left) != binding_value_kind(right)) return false;
    return binding_value_kind(left) == BINDING_VALUE_MATERIALIZED ||
        cetta_frame_identity_eq(binding_value_epoch(left), binding_value_epoch(right));
}

static void outcome_seen_identity_control(void) {
    Atom *atoms = calloc(4096u, sizeof(*atoms));
    assert(atoms);
    OutcomePreviewSeen seen;
    outcome_preview_seen_init(&seen);
    BindingValue expected[3072];
    size_t expected_len = 0u;
    uint32_t state = 17u;
    for (size_t step = 0u; step < 16000u; step++) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        BindingValue value = binding_value_make(
            (state & 31u) == 0u ? NULL : &atoms[(state >> 8u) % 257u],
            (CettaFrameIdentity){.word = (state >> 24u) % 4u},
            (BindingValueKind)((state >> 20u) % 3u));
        bool found = false;
        for (size_t i = 0u; i < expected_len; i++)
            if (seen_reference_equal(expected[i], value)) { found = true; break; }
        assert(outcome_preview_seen_add_value(&seen, value) == !found);
        if (!found) {
            assert(expected_len < sizeof(expected) / sizeof(expected[0]));
            expected[expected_len++] = value;
        }
        assert(seen.len == expected_len);
    }
    assert(expected_len > 1024u);
    for (size_t i = 0u; i < expected_len; i++) {
        assert(seen.items[i].skeleton == expected[i].skeleton);
        assert(binding_value_kind(seen.items[i]) == binding_value_kind(expected[i]));
        assert(cetta_frame_identity_eq(binding_value_epoch(seen.items[i]), binding_value_epoch(expected[i])));
        assert(!outcome_preview_seen_add_value(&seen, expected[i]));
    }
    outcome_preview_seen_free(&seen);
    outcome_preview_seen_init(&seen);
    for (size_t i = 0u; i < 16u; i++) assert(outcome_preview_seen_add(&seen, &atoms[i]));
    assert(seen.items == seen.inline_items);
    assert(!seen.slots && !seen.slot_cap);
    BindingValue m = binding_value_from_atom(&atoms[32]);
    assert(outcome_preview_seen_add_value(&seen, m));
    binding_value_set_epoch(&m, (CettaFrameIdentity){.word = 91u});
    assert(!outcome_preview_seen_add_value(&seen, m));
    BindingValue c = binding_value_from_context(&atoms[32], (CettaFrameIdentity){.word = 11u});
    assert(outcome_preview_seen_add_value(&seen, c));
    binding_value_set_epoch(&c, (CettaFrameIdentity){.word = 12u});
    assert(outcome_preview_seen_add_value(&seen, c));
    binding_value_set_kind(&c, BINDING_VALUE_CONTEXTUAL_PERSISTENT);
    assert(outcome_preview_seen_add_value(&seen, c));
    assert(outcome_preview_seen_add(&seen, &atoms[33]));
    assert(seen.slots && seen.slot_cap >= 64u);
    assert(outcome_preview_seen_contains_value(&seen, c));
    binding_value_set_epoch(&c, (CettaFrameIdentity){.word = 13u});
    assert(!outcome_preview_seen_contains_value(&seen, c));
    assert(outcome_preview_seen_contains(&seen, &atoms[32]));
    assert(!outcome_preview_seen_contains(&seen, &atoms[34]));
    outcome_preview_seen_free(&seen);
    /* Different addresses collide in the same initial index bucket. */
    outcome_preview_seen_init(&seen);
    size_t selected[32], count = 0u, buckets[64] = {0};
    for (size_t i = 0u; i < 4096u; i++)
        buckets[outcome_preview_seen_hash(binding_value_from_atom(&atoms[i])) & 63u]++;
    size_t bucket = 0u;
    for (size_t i = 1u; i < 64u; i++)
        if (buckets[i] > buckets[bucket]) bucket = i;
    assert(buckets[bucket] >= 64u);
    for (size_t i = 0u; i < 4096u && count < 32u; i++) {
        if ((outcome_preview_seen_hash(binding_value_from_atom(&atoms[i])) & 63u) == bucket)
            selected[count++] = i;
    }
    assert(count == 32u);
    for (size_t i = 0u; i < count; i++) assert(outcome_preview_seen_add(&seen, &atoms[selected[i]]));
    for (size_t i = 0u; i < count; i++) {
        assert(outcome_preview_seen_contains(&seen, &atoms[selected[i]]));
        assert(!outcome_preview_seen_add(&seen, &atoms[selected[i]]));
        assert(seen.items[i].skeleton == &atoms[selected[i]]);
    }
    outcome_preview_seen_free(&seen);
    /* Construct the capacity edge without allocating an impossible array.
     * Empty slots make the pre-insertion lookup independent of that array. */
    outcome_preview_seen_init(&seen);
    seen.slots = calloc(2u, sizeof(*seen.slots));
    assert(seen.slots);
    seen.slot_cap = 2u;
    seen.len = seen.cap = SIZE_MAX / 2u + 1u;
    BindingValue *old_items = seen.items;
    size_t *old_slots = seen.slots;
    assert(!outcome_preview_seen_contains(&seen, &atoms[0]));
    assert(!outcome_preview_seen_add(&seen, &atoms[0]));
    assert(seen.items == old_items && seen.slots == old_slots);
    assert(seen.len == SIZE_MAX / 2u + 1u && seen.cap == seen.len);
    assert(!seen.slots[0] && !seen.slots[1]);
    outcome_preview_seen_free(&seen);
    free(atoms);
    puts("Outcome visit identities, order, inline and indexed boundaries passed");
}

static void stored_judgment_syntax_case(unsigned which) {
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena;arena_init(&arena);
    Bindings lexical,local,foreign;bindings_init(&lexical);bindings_init(&local);bindings_init(&foreign);
    Atom *variable=atom_var(&arena,"captured");
    assert(bindings_add_var_owned(&lexical,variable,atom_int(&arena,17)));
    Atom *term=atom_expr2(&arena,atom_symbol(&arena,"println!"),variable),*ref=NULL;
    assert(prime_need_allocate_source_argument_ref(&arena,term,prime_need_fresh_source_occurrence(),1u,&lexical,&local,&ref));
    PrimeNeedSnapshot world=*bindings_need_view(&local),other;
    uint64_t id=0u;PrimeNeedCellView before,after;
    assert(prime_need_ref_belongs_to(ref,&world,&id));
    assert(prime_need_snapshot_lookup(&world,id,&before));
    assert(before.origin->expr.elems[1]->ground.ival==17);
    Atom *stored=prime_need_persist_data(&arena,ref,&local);assert(prime_need_is_stored_thunk(stored,NULL,NULL,NULL));
    Atom *ready=NULL;assert(prime_need_ready_value_ref(&arena,stored,&local,&ready));
    Atom *box=atom_symbol(&arena,"Pair");
    if(which==0u) {
        Atom *actual=prime_public_bound_operand_syntax(&arena,ready,&local);
        assert(atom_eq(actual,before.origin));
        assert(!prime_need_is_stored_thunk(actual,NULL,NULL,NULL));
    } else if(which==1u) {
        Atom *pair=atom_expr3(&arena,box,ready,ready);
        Atom *actual=prime_public_bound_operand_syntax(&arena,pair,&local);
        assert(actual->kind==ATOM_EXPR && actual->expr.len==3u);
        assert(actual->expr.elems[1]==actual->expr.elems[2]);
        assert(atom_eq(actual->expr.elems[1],before.origin));
    } else if(which==2u) {
        Atom *denied=prime_need_ref_restrict(&arena,ref,&world,CETTA_PRIME_NEED_RIGHT_FORCE);
        assert(denied && prime_public_bound_operand_syntax(&arena,denied,&local)==denied);
        Atom *stored_denied=prime_need_persist_data(&arena,denied,&local);
        assert(stored_denied && prime_public_bound_operand_syntax(&arena,stored_denied,&local)==stored_denied);
    } else if(which==3u) {
        prime_need_snapshot_init(&other);assert(prime_need_snapshot_begin(&other));
        *bindings_need_mut(&foreign)=other;
        assert(prime_public_bound_operand_syntax(&arena,ready,&foreign)==ready);
    } else if(which==4u) {
        Atom *promise=atom_expr2(&arena,atom_symbol_id(&arena,prime_need_symbols()->promise),ref);
        Atom *saved=prime_need_persist_data(&arena,promise,&local);
        assert(prime_need_is_stored_promise(saved,NULL,NULL,NULL));
        assert(prime_public_bound_operand_syntax(&arena,saved,&local)==saved);
        Atom *actual=prime_public_bound_operand_syntax(&arena,promise,&local);
        assert(prime_need_is_promise(actual));
        assert(atom_eq(actual->expr.elems[1],before.origin));
    } else if(which==5u) {
        Atom *deep=ready,*expected=before.origin;
        for(unsigned depth=0u;depth<72u;depth++) {
            deep=atom_expr2(&arena,box,deep);expected=atom_expr2(&arena,box,expected);
        }
        assert(atom_eq(prime_public_bound_operand_syntax(&arena,deep,&local),expected));
    } else if(which==6u) {
        Atom *plain=before.origin;
        for(unsigned depth=0u;depth<72u;depth++)plain=atom_expr2(&arena,box,plain);
        size_t bytes=arena_accounted_live_bytes(&arena);
        assert(prime_public_bound_operand_syntax(&arena,plain,&local)==plain);
        assert(arena_accounted_live_bytes(&arena)==bytes);
    } else if(which==7u) {
        Atom *quote=atom_expr2(&arena,atom_symbol(&arena,"quote"),before.origin);
        Atom *lambda=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"lam"),atom_symbol(&arena,"x"),before.origin},3u);
        assert(prime_public_bound_operand_syntax(&arena,quote,&local)==quote);
        assert(prime_public_bound_operand_syntax(&arena,lambda,&local)==lambda);
    } else assert(false);
    assert(prime_need_snapshot_lookup(bindings_need_view(&local),id,&after));
    assert(before.origin==after.origin && before.cached==after.cached && before.cache_state==after.cache_state && before.evaluator_id==after.evaluator_id && before.value_role==after.value_role);
    printf("StoredJudgment %u passed; origin pending, no producer execution\n",which);
    bindings_free(&local);bindings_free(&foreign);bindings_free(&lexical);
    arena_free(&arena);cetta_library_context_free(&context);eval_set_library_context(NULL);
}

static Atom *dependency_view_judge(Arena *arena, Space *space, const char *text,
                   const char *status) {
    Atom *result = prime_scoped_judgment_judge(
        arena, space, parse_one(arena, text), false, 0u);
    if (!result || result->kind != ATOM_EXPR || result->expr.len != 4u ||
        !atom_is_symbol(result->expr.elems[1], status)) {
        fprintf(stderr, "Unexpected judgment: %s\n", text);
        if (result) atom_print(result, stderr);
        fputc('\n', stderr);
        assert(false);
    }
    return result->expr.elems[3];
}

static Atom *dependency_view_publish(Arena *arena, Space *space, const char *text) {
    Atom *result = dependency_view_judge(arena, space, text, "Established");
    assert(result->kind == ATOM_EXPR && result->expr.len >= 2u &&
           atom_is_symbol(result->expr.elems[0], "HolPublish"));
    for (CettaExprIndex i = 1u; i < result->expr.len; i++) {
        space_add(space, result->expr.elems[i]);
        prime_scoped_judgment_admit(arena, space, result->expr.elems[i]);
    }
    return result->expr.elems[1];
}

static Atom *dependency_view_find_record(Arena *arena, Space *space, const char *name) {
    Atom *pattern = parse_one(arena, "(hol:known $n $p $o $f $d $r $s)");
    CettaIndex *indices = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &indices);
    Atom *result = NULL;
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *record = space_match_candidate_at64(space, indices[i]);
        if (record && record->kind == ATOM_EXPR && record->expr.len == 8u &&
            atom_is_symbol(record->expr.elems[1], name)) {
            assert(!result);
            result = record;
        }
    }
    free(indices);
    assert(result);
    return result;
}

static bool dependency_view_contains_exact(Atom *tree, Atom *value) {
    if (atom_eq(tree, value)) return true;
    if (tree && tree->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0u; i < tree->expr.len; i++)
            if (dependency_view_contains_exact(tree->expr.elems[i], value)) return true;
    return false;
}

static void dependency_view_case(unsigned which) {
    assert(which < 9u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena; arena_init(&arena);
    Space space; space_init(&space);
    space_add(&space, parse_one(&arena, "(hol:profile base)"));
    Atom *premise = dependency_view_publish(&arena, &space, "(hol:axiom premise Falsum)");
    Atom *original = dependency_view_publish(&arena, &space,
        "(hol:theorem cited Falsum (pf:known premise))");
    assert(original->kind == ATOM_EXPR && original->expr.len == 8u);
    Atom *origin = original->expr.elems[5];
    assert(atom_eq(origin, parse_one(&arena, "((premise Falsum))")));
    if (which == 2u) origin = parse_one(&arena, "(malformed)");
    if (which == 3u) origin = atom_symbol(&arena, "malformed");
    if (which == 5u) origin = parse_one(&arena, "(println! must-not-run)");
    if (which == 6u) origin = parse_one(&arena, "((missing Falsum))");
    if (which == 7u) origin = parse_one(&arena, "((premise (imp Falsum Falsum)))");
    if (which == 8u) origin = atom_unit(&arena);
    Bindings lexical, local; bindings_init(&lexical); bindings_init(&local);
    Atom *reference = NULL;
    assert(prime_need_allocate_source_argument_ref(
        &arena, origin, prime_need_fresh_source_occurrence(), 1u,
        &lexical, &local, &reference));
    uint64_t cell_id = 0u;
    assert(prime_need_ref_belongs_to(reference, bindings_need_view(&local), &cell_id));
    PrimeNeedCellView before, after;
    assert(prime_need_snapshot_lookup(bindings_need_view(&local), cell_id, &before));
    Atom *stored_ref = which == 1u
        ? prime_need_ref_restrict(&arena, reference, bindings_need_view(&local),
                                 CETTA_PRIME_NEED_RIGHT_FORCE)
        : reference;
    if (which == 4u)
        stored_ref = atom_expr2(&arena,
            atom_symbol_id(&arena, prime_need_symbols()->promise), reference);
    Atom *stored = prime_need_persist_data(&arena, stored_ref, &local);
    assert(stored);
    Atom *fields[8];
    memcpy(fields, original->expr.elems, sizeof fields);
    fields[1] = atom_symbol(&arena, "probe");
    fields[5] = stored;
    space_add(&space, atom_expr(&arena, fields, 8u));
    Atom *raw = dependency_view_find_record(&arena, &space, "probe");
    Atom *raw_copy = atom_deep_copy(&arena, raw);
    Atom *decoded_fields[8];
    memcpy(decoded_fields, raw->expr.elems, sizeof decoded_fields);
    decoded_fields[5] = origin;
    Atom *decoded = atom_expr(&arena, decoded_fields, 8u);
    assert(!atom_eq(raw, decoded));
    assert(!prime_scoped_judgment_admitted(&arena, &space, raw));
    uint64_t epoch = prime_scoped_judgment_admission_epoch();
    PrimeNeedActiveGuard active;
    prime_need_active_enter(&active, &local);
    Atom *report = dependency_view_judge(&arena, &space, "(try (hol:recheck probe))", "Established");
    assert(report->kind == ATOM_EXPR && report->expr.len == 2u &&
           atom_is_symbol(report->expr.elems[0], "PrimeScopedValue"));
    Atom *diagnostic = report->expr.elems[1];
    assert(diagnostic->kind == ATOM_EXPR && diagnostic->expr.len == 2u);
    const char *reason = which == 6u ? "hol:dependency-missing" :
        which == 7u ? "hol:dependency-changed" :
        which == 8u ? "hol:dependency-unrecorded" : "hol:dependency-syntax";
    assert(atom_is_symbol(diagnostic->expr.elems[0],
                          which == 0u ? "Established" : "Undetermined"));
    if (which != 0u) {
        Atom *why = diagnostic->expr.elems[1];
        assert(why->kind == ATOM_EXPR && atom_is_symbol(why->expr.elems[0], reason));
    }
    assert(prime_scoped_judgment_admission_epoch() == epoch);
    assert(!prime_scoped_judgment_admitted(&arena, &space, raw));
    if (which == 0u) {
        (void)dependency_view_judge(&arena, &space, "(hol:recheck probe)", "Established");
        assert(prime_scoped_judgment_admitted(&arena, &space, raw));
        assert(!prime_scoped_judgment_admitted(&arena, &space, decoded));
        Atom *view = dependency_view_judge(&arena, &space, "(hol:known-proof probe)", "Established");
        assert(view->kind == ATOM_EXPR && view->expr.len == 2u &&
               atom_eq(view->expr.elems[1], raw->expr.elems[4]));
        (void)dependency_view_judge(&arena, &space, "(hol:proves (pf:known probe) Falsum)", "Established");
        assert(space_remove(&space, premise));
        epoch = prime_scoped_judgment_admission_epoch();
        report = dependency_view_judge(&arena, &space, "(try (hol:recheck probe))", "Established");
        assert(dependency_view_contains_exact(report, parse_one(&arena, "(hol:dependency-missing premise)")));
        assert(prime_scoped_judgment_admission_epoch() == epoch);
        assert(prime_scoped_judgment_admitted(&arena, &space, raw));
        (void)dependency_view_judge(&arena, &space, "(hol:recheck probe)", "Undetermined");
        assert(!prime_scoped_judgment_admitted(&arena, &space, raw));
        space_add(&space, premise);
        (void)dependency_view_judge(&arena, &space, "(hol:recheck probe)", "Established");
        assert(prime_scoped_judgment_admitted(&arena, &space, raw));
        assert(!prime_scoped_judgment_admitted(&arena, &space, decoded));
        Atom *assumption_fields[8];
        memcpy(assumption_fields, raw->expr.elems, sizeof assumption_fields);
        assumption_fields[1] = atom_symbol(&arena, "manifest-probe");
        assumption_fields[3] = atom_symbol(&arena, "axiom");
        assumption_fields[4] = atom_unit(&arena);
        space_add(&space, atom_expr(&arena, assumption_fields, 8u));
        Atom *assumption = dependency_view_find_record(&arena, &space, "manifest-probe");
        prime_scoped_judgment_admit(&arena, &space, assumption);
        Atom *manifest = dependency_view_judge(&arena, &space, "(hol:signature manifest)", "Established");
        assert(dependency_view_contains_exact(manifest, assumption));
        assumption_fields[5] = origin;
        Atom *assumption_view = atom_expr(&arena, assumption_fields, 8u);
        assert(!prime_scoped_judgment_admitted(&arena, &space, assumption_view));
        assert(!dependency_view_contains_exact(manifest, assumption_view));
    } else {
        /* A diagnostic must not revoke even an existing admission. Install
         * that precondition through the trusted publication API, then test
         * diagnostic versus plain recheck separately. */
        prime_scoped_judgment_admit(&arena, &space, raw);
        epoch = prime_scoped_judgment_admission_epoch();
        report = dependency_view_judge(&arena, &space, "(try (hol:recheck probe))", "Established");
        assert(dependency_view_contains_exact(report, atom_symbol(&arena, reason)));
        assert(prime_scoped_judgment_admission_epoch() == epoch);
        assert(prime_scoped_judgment_admitted(&arena, &space, raw));
        (void)dependency_view_judge(&arena, &space, "(hol:recheck probe)", "Undetermined");
        assert(!prime_scoped_judgment_admitted(&arena, &space, raw));
    }
    prime_need_active_leave(&active);
    assert(atom_eq(raw, raw_copy));
    assert(prime_need_snapshot_lookup(bindings_need_view(&local), cell_id, &after));
    assert(before.origin == after.origin && before.cached == after.cached &&
           before.cache_state == after.cache_state && before.evaluator_id == after.evaluator_id &&
           before.value_role == after.value_role);
    printf("DependencyView %u passed; raw authority retained, producer pending\n", which);
    bindings_free(&local); bindings_free(&lexical);
    space_free(&space); arena_free(&arena);
    cetta_library_context_free(&context); eval_set_library_context(NULL);
}

static void pattern_root_case(unsigned which) {
    Arena arena;arena_init(&arena);
    Space space;space_init(&space);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Bindings env,branch;bindings_init(&env);bindings_init(&branch);
    Atom *sibling=atom_var(&arena,"retained-sibling");
    assert(bindings_add_var(&env,sibling,atom_int(&arena,19)));
    Atom *raw=parse_one(&arena,"(+ 1 2)");
    if(which==13u)raw=atom_error(&arena,raw,atom_symbol(&arena,"child-fault"));
    Atom *child=NULL;
    assert(prime_need_allocate_source_argument_ref(&arena,raw,
        prime_need_fresh_source_occurrence(),1u,&env,&branch,&child));
    bindings_replace(&env,&branch);
    uint64_t child_id=0u;
    assert(prime_need_ref_belongs_to(child,bindings_need_view(&env),&child_id));
    Atom *payload=atom_expr2(&arena,atom_symbol(&arena,"Box"),
                             which==0u?raw:child);
    Atom *pattern=parse_one(&arena,"(Box $field)");
    if(which==2u || which==13u)pattern=parse_one(&arena,"(Box 3)");
    if(which==3u) {
        PrimeNeedSnapshot evaluating,ready;
        uint64_t evaluator=prime_need_fresh_evaluator_id();
        assert(prime_need_snapshot_start_evaluation(&arena,
            bindings_need_view(&env),child_id,evaluator,&evaluating));
        assert(prime_need_snapshot_resolve_value(&arena,&evaluating,
            child_id,evaluator,atom_int(&arena,37),&ready));
        *bindings_need_mut(&env)=ready;
        payload=atom_expr3(&arena,atom_symbol(&arena,"Pair"),child,child);
        pattern=parse_one(&arena,"(Pair 37 37)");
    } else if(which==4u)pattern=atom_var(&arena,"unused");
    else if(which==5u) {
        payload=atom_expr2(&arena,
            atom_symbol_id(&arena,prime_need_symbols()->promise),child);
        pattern=atom_expr2(&arena,
            atom_symbol_id(&arena,prime_need_symbols()->promise),atom_int(&arena,3));
    } else if(which==6u) {
        payload=atom_expr2(&arena,atom_symbol(&arena,"quote"),child);
        pattern=parse_one(&arena,"(quote 3)");
    } else if(which==9u) {
        payload=atom_error(&arena,raw,atom_symbol(&arena,"probe-fault"));
    } else if(which==10u)payload=atom_empty(&arena);
    Atom *root=NULL;
    if(which==12u) {
        assert(prime_need_allocate_source_argument_ref(&arena,
            parse_one(&arena,"(superpose ())"),prime_need_fresh_source_occurrence(),
            2u,&env,&branch,&root));
        bindings_replace(&env,&branch);
    } else assert(prime_need_ready_value_ref(&arena,payload,&env,&root));
    uint64_t root_id=0u;
    PrimeNeedCellView before,after;
    assert(prime_need_ref_belongs_to(root,bindings_need_view(&env),&root_id));
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),root_id,&before));
    assert(which==12u || before.value_role==PRIME_NEED_VALUE_READY);
    if(which==7u) {
        root=prime_need_ref_restrict(&arena,root,bindings_need_view(&env),
            CETTA_PRIME_NEED_RIGHT_INSPECT);
        assert(root);
    } else if(which==8u) {
        PrimeNeedSnapshot foreign;
        prime_need_snapshot_init(&foreign);
        assert(prime_need_snapshot_begin(&foreign));
        *bindings_need_mut(&branch)=foreign;
    } else if(which==11u) {
        payload=parse_one(&arena,"(Box (+ 1 2))");
        root=payload;
        pattern=parse_one(&arena,"(Box (+ 1 2))");
    }
    OutcomeSet out;outcome_set_init(&out);
    bool refined=prime_pattern_refine_scrutinee(
        &space,&arena,root,which==8u?&branch:&env,
        CETTA_CALL_RESULT_COMPLETED_VALUE,&pattern,1u,-1,&out);
    if(which==4u || which==8u || which==11u) {
        assert(!refined && out.len==0u);
    } else {
        assert(refined);
        if(which==12u)assert(out.len==0u);
        else {
            assert(out.len==1u);
            Atom *result=outcome_atom_materialize(&arena,&out.items[0]);
            assert(result);
            Atom *kept=bindings_apply_if_vars(&out.items[0].env,&arena,sibling);
            assert(kept->kind==ATOM_GROUNDED && kept->ground.ival==19);
            if(which==13u)assert(atom_eq(result,raw));
            else if(which==7u) {
                assert(atom_is_error(result));
                assert(atom_is_symbol_id(result->expr.elems[2],
                    symbol_intern_cstr(g_symbols,"suspension:force-denied")));
            } else if(which==2u) {
                assert(atom_eq(result,parse_one(&arena,"(Box 3)")));
            } else if(which==3u) {
                assert(atom_eq(result,parse_one(&arena,"(Pair 37 37)")));
            } else assert(atom_eq(result,payload));
            if(which!=9u && which!=7u) {
                PrimeNeedCellView returned;
                assert(prime_need_snapshot_lookup(bindings_need_view(&out.items[0].env),
                    child_id,&returned));
                if(which==13u)assert(returned.cache_state==PRIME_NEED_CACHE_STABLE_FAULT);
                else if(which==2u || which==3u) {
                    assert(returned.cache_state==PRIME_NEED_CACHE_VALUE);
                    assert(returned.cached->kind==ATOM_GROUNDED);
                    assert(returned.cached->ground.ival==(which==2u?3:37));
                } else assert(returned.cache_state==PRIME_NEED_CACHE_EMPTY);
            }
        }
    }
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),root_id,&after));
    assert(before.origin==after.origin && before.cached==after.cached &&
        before.cache_state==after.cache_state && before.value_role==after.value_role);
    PrimeNeedCellView unmodified;
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),child_id,&unmodified));
    assert(unmodified.cache_state==(which==3u?PRIME_NEED_CACHE_VALUE:PRIME_NEED_CACHE_EMPTY));
    printf("PatternRoot %u passed\n",which);
    outcome_set_free(&out);bindings_free(&branch);bindings_free(&env);
    space_free(&space);arena_free(&arena);
    cetta_library_context_free(&context);eval_set_library_context(NULL);
}

static Atom *br_parse(Arena *arena,const char *text) {
    Atom **items=NULL;int count=parse_metta_text(text,arena,&items);
    assert(count==1);Atom *value=items[0];free(items);return value;
}
static void br_packet(Arena *arena,OutcomeSet *rows,Atom *value,Atom *variable,
    int64_t binding,CettaCallResultForm form,Atom *condition) {
    Bindings env;bindings_init(&env);
    assert(bindings_add_var(&env,variable,atom_int(arena,binding)));
    outcome_set_add(rows,value,&env);bindings_free(&env);
    assert(rows->len);rows->items[rows->len-1u].result_form=form;
    rows->items[rows->len-1u].delayed=condition;
}
typedef struct {OutcomeSet rows;size_t limit;} BrLoan;
static bool br_loan(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    BrLoan *loan=raw;
    assert(value==outcome_atom_materialize(arena,(Outcome *)occurrence));
    outcome_set_add_existing(&loan->rows,occurrence);
    return loan->rows.len<loan->limit;
}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool br_scalar(void *ctx,const char *site,uint64_t a,uint64_t b) {
    (void)ctx;(void)site;return a==b;
}
static bool br_identity(void *ctx,const char *site,CettaCheckpointIdentityRole role,
    uint64_t a,uint64_t b) {
    (void)role;return br_scalar(ctx,site,a,b);
}
static bool br_address(void *ctx,const char *site,const void *a,const void *b) {
    (void)ctx;(void)site;return (a==NULL)==(b==NULL);
}
static bool br_arena(void *ctx,const char *site,const Arena *a,const Arena *b) {
    (void)ctx;(void)site;return a==b;
}
static bool br_atom(void *ctx,const char *site,const Atom *a,const Atom *b) {
    (void)ctx;(void)site;return a==b || (a && b && atom_eq((Atom *)a,(Atom *)b));
}
static bool br_physical(void *ctx,const char *site,uint64_t a,uint64_t b) {
    (void)ctx;(void)site;(void)a;(void)b;return true;
}
static bool br_bytes(void *ctx,const char *site,const void *a,size_t n,const void *b,size_t m) {
    (void)ctx;(void)site;return n==m && (!n || !memcmp(a,b,n));
}
static bool br_service(void *ctx,const char *site,size_t work,size_t bytes) {
    (void)ctx;(void)site;(void)work;(void)bytes;return true;
}
static CettaCheckpointCompare br_comparison(void) {
    return (CettaCheckpointCompare){.scalar=br_scalar,.identity=br_identity,
        .address=br_address,.arena=br_arena,.atom=br_atom,.physical=br_physical,
        .bytes=br_bytes,.service=br_service};
}
#endif

static bool br_owned_return(void *raw,const CettaOwnedReturn *returned) {
    CettaCallOutcome *out=raw;
    assert(returned && returned->environment);*out=returned->outcome;return true;
}
static void binding_role_case(unsigned which) {
    Arena arena,owned;arena_init(&arena);arena_init(&owned);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);Registry *prior_registry=g_registry;g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Bindings env;bindings_init(&env);
    Atom *variable=atom_var(&arena,"br-retained");
    Atom *raw=br_parse(&arena,"(+ $br-free 1)");
    Atom *condition=br_parse(&arena,"((br:condition $br-free))");
    OutcomeSet rows;outcome_set_init(&rows);
    if(which==0u) {
        eval_for_caller(&space,&arena,NULL,raw,-1,&env,false,&rows);
        assert(rows.len==1u && rows.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
        Atom *earned=atom_expr3(&arena,atom_symbol(&arena,"cons-atom"),
            raw->expr.elems[0],atom_expr(&arena,raw->expr.elems+1u,2u));
        eval_for_caller(&space,&arena,NULL,earned,-1,&env,false,&rows);
        assert(rows.len==2u && rows.items[1].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
        assert(atom_eq(outcome_atom_materialize(&arena,&rows.items[0]),
                       outcome_atom_materialize(&arena,&rows.items[1])));
    } else if(which==1u) {
        br_packet(&arena,&rows,raw,variable,19,CETTA_CALL_RESULT_EXPRESSION,condition);
        br_packet(&arena,&rows,raw,variable,37,CETTA_CALL_RESULT_COMPLETED_VALUE,atom_unit(&arena));
        CettaSearchPolicyOrder orders[]={CETTA_SEARCH_POLICY_ORDER_NATIVE,
            CETTA_SEARCH_POLICY_ORDER_REVERSE,CETTA_SEARCH_POLICY_ORDER_LEX,
            CETTA_SEARCH_POLICY_ORDER_SHORTLEX};
        for(size_t i=0u;i<4u;i++) {
            BrLoan loan={.limit=1u};outcome_set_init(&loan.rows);
            assert(outcome_set_visit_ordered(&arena,&rows,orders[i],br_loan,&loan)==1u);
            assert(loan.rows.len==1u);
            size_t selected=i==1u?1u:0u;
            assert(loan.rows.items[0].result_form==rows.items[selected].result_form);
            assert(loan.rows.items[0].delayed==rows.items[selected].delayed);
            Atom *kept=bindings_apply_if_vars(&loan.rows.items[0].env,&arena,variable);
            assert(kept->kind==ATOM_GROUNDED && kept->ground.ival==(selected?37:19));
            outcome_set_free(&loan.rows);
        }
        BrLoan both={.limit=3u};outcome_set_init(&both.rows);
        assert(outcome_set_visit_ordered(&arena,&rows,CETTA_SEARCH_POLICY_ORDER_NATIVE,br_loan,&both)==2u);
        assert(both.rows.len==2u && both.rows.items[0].result_form!=both.rows.items[1].result_form);
        outcome_set_free(&both.rows);
    } else if(which==2u || which==3u || which==4u) {
        Atom *root=NULL;
        assert(prime_need_returned_value_ref(&arena,raw,&env,&root,CETTA_CALL_RESULT_EXPRESSION));
        assert(bindings_add_var(&env,variable,atom_int(&arena,19)));
        outcome_set_add(&rows,raw,&env);rows.items[0].result_form=CETTA_CALL_RESULT_EXPRESSION;
        rows.items[0].delayed=condition;
        Atom *value=which==4u?atom_expr2(&arena,atom_symbol(&arena,"Box"),root):root;
        outcome_set_add(&rows,value,&env);
        if(which==4u)rows.items[1].result_form=CETTA_CALL_RESULT_EXPRESSION;
        prime_need_observe_tuple_returns(&space,&arena,&rows,which==3u?200:-1,NULL,NULL);
        assert(rows.len==2u && rows.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
        assert(rows.items[0].delayed==condition);
        assert(rows.items[1].result_form==CETTA_CALL_RESULT_EXPRESSION);
        assert(atom_eq(outcome_atom_materialize(&arena,&rows.items[1]),
                       which==4u?atom_expr2(&arena,atom_symbol(&arena,"Box"),raw):raw));
        Atom *kept=bindings_apply_if_vars(&rows.items[1].env,&arena,variable);
        /* Normalization may project unused logical bindings. The unrelated
         * non-observed packet must retain its original caller binding. */
        (void)kept;
        kept=bindings_apply_if_vars(&rows.items[0].env,&arena,variable);
        assert(kept->kind==ATOM_GROUNDED && kept->ground.ival==19);
    } else if(which==5u || which==11u) {
        for(unsigned ready=0u;ready<2u;ready++) {
            PrimeNeedSnapshot pending,evaluating,complete,consumer_pending,consumer_running,forwarded;
            prime_need_snapshot_init(&pending);assert(prime_need_snapshot_begin(&pending));
            uint64_t producer=0u,consumer=0u,evaluator=prime_need_fresh_evaluator_id();
            assert(prime_need_snapshot_allocate(&arena,&pending,raw,&pending,&producer));
            assert(prime_need_snapshot_start_evaluation(&arena,&pending,producer,evaluator,&evaluating));
            assert(ready?prime_need_snapshot_resolve_ready_value(&arena,&evaluating,producer,evaluator,raw,&complete):
                         prime_need_snapshot_resolve_value(&arena,&evaluating,producer,evaluator,raw,&complete));
            assert(prime_need_snapshot_allocate(&arena,&complete,raw,&consumer_pending,&consumer));
            assert(prime_need_snapshot_start_evaluation(&arena,&consumer_pending,consumer,evaluator,&consumer_running));
            forwarded=complete;
            assert(!prime_need_snapshot_resolve_forwarded_value(&arena,&consumer_running,consumer,evaluator+1u,raw,producer,&forwarded));
            assert(!memcmp(&forwarded,&complete,sizeof(complete)));
            assert(prime_need_snapshot_resolve_forwarded_value(&arena,&consumer_running,consumer,evaluator,raw,producer,&forwarded));
            PrimeNeedCellView got,source;
            assert(prime_need_snapshot_lookup(&forwarded,consumer,&got));
            assert(got.value_role==(ready?PRIME_NEED_VALUE_READY:PRIME_NEED_VALUE_DEMAND_RESULT));
            assert(prime_need_snapshot_forwarded_value_source(&forwarded,consumer,&source));
            assert(source.value_role==got.value_role);
            if(which==5u) {
                PrimeNeedSnapshot copy=forwarded;
                assert(prime_need_snapshot_promote(&owned,&copy));
                assert(prime_need_snapshot_lookup(&copy,consumer,&got));
                assert(prime_need_snapshot_forwarded_value_source(&copy,consumer,&source));
                assert(source.value_role==got.value_role);
                assert(prime_need_snapshot_lookup(&forwarded,consumer,&got));
            } else {
                *bindings_need_mut(&env)=forwarded;
                PrimeNeedActiveGuard active;
                prime_need_active_enter(&active, &env);
                Atom *ref=prime_need_ref(&arena,&forwarded,consumer);
                CettaCount first=rows.len;
                prime_need_force_active(&space,&arena,ref,consumer,-1,&rows);
                prime_need_active_leave(&active);
                assert(rows.len==first+1u);
                assert(rows.items[first].result_form==(ready?CETTA_CALL_RESULT_COMPLETED_VALUE:CETTA_CALL_RESULT_EXPRESSION));
            }
        }
    } else if(which==6u || which==7u || which==8u) {
        br_packet(&arena,&rows,raw,variable,19,CETTA_CALL_RESULT_EXPRESSION,condition);
        if(which==6u) {
            Outcome copy,moved;outcome_init(&copy);outcome_init(&moved);
            assert(outcome_clone(&copy,&rows.items[0],&owned));
            assert(copy.result_form==CETTA_CALL_RESULT_EXPRESSION && atom_eq(copy.delayed,condition));
            outcome_move(&moved,&copy);
            assert(moved.result_form==CETTA_CALL_RESULT_EXPRESSION && atom_eq(moved.delayed,condition));
            OutcomeSet published;outcome_set_init(&published);
            outcome_set_add_existing_with_env(&owned,&published,&moved,&moved.env);
            assert(published.len==1u && published.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
            assert(atom_eq(published.items[0].delayed,condition));
            Atom *kept=bindings_apply_if_vars(&published.items[0].env,&owned,variable);
            assert(kept->kind==ATOM_GROUNDED && kept->ground.ival==19);
            outcome_set_free(&published);outcome_free_fields(&moved);outcome_free_fields(&copy);
        } else if(which==7u) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            OutcomeSet copy;outcome_set_init(&copy);outcome_set_add_existing(&copy,&rows.items[0]);
            CettaCheckpointCompare compare=br_comparison();
            assert(prime_replay_compare_outcomes(&rows,&copy,&compare));
            copy.items[0].result_form=CETTA_CALL_RESULT_COMPLETED_VALUE;
            assert(!prime_replay_compare_outcomes(&rows,&copy,&compare));
            copy.items[0].result_form=CETTA_CALL_RESULT_EXPRESSION;
            copy.items[0].delayed=atom_unit(&arena);
            assert(!prime_replay_compare_outcomes(&rows,&copy,&compare));
            copy.items[0].delayed=condition;
            copy.items[0].result_form=(CettaCallResultForm)99;
            rows.items[0].result_form=(CettaCallResultForm)99;
            assert(!prime_replay_compare_outcomes(&rows,&copy,&compare));
            outcome_set_free(&copy);
#else
            assert(!"recording-only return comparison needs recording support");
#endif
        } else {
            PrimeOrdinarySupport support={.space=&space,.arena=&arena,
                .snapshot=bindings_need_view(&env),.environment=&env};
            rows.items[0].result_form=CETTA_CALL_RESULT_COMPLETED_FRONTIER;
            assert(!prime_ordinary_support_outcomes(&support,&rows));
            rows.items[0].result_form=(CettaCallResultForm)99;
            assert(!prime_ordinary_support_outcomes(&support,&rows));
        }
    } else if(which==9u) {
        Outcome seed;outcome_init(&seed);seed.atom=atom_int(&arena,7);
        seed.result_form=CETTA_CALL_RESULT_EXPRESSION;
        eval_delayed_outcome_for_caller(&space,&arena,NULL,NULL,&seed,-1,true,&rows);
        assert(rows.len==1u && rows.items[0].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
        outcome_free_fields(&seed);
    } else if(which==10u) {
        Atom *frontier=atom_expr2(&arena,raw,raw);
        CettaCallOutcome produced=cetta_call_value(frontier);
        produced.result_form=CETTA_CALL_RESULT_COMPLETED_FRONTIER;
        emit_completed_native_call(&space,&arena,produced,frontier,NULL,&env,&rows);
        assert(rows.len==2u);
        assert(rows.items[0].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE &&
               rows.items[1].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
        assert(atom_eq(outcome_atom_materialize(&arena,&rows.items[0]),raw) &&
               atom_eq(outcome_atom_materialize(&arena,&rows.items[1]),raw));
    } else if(which==12u || which==13u) {
        Atom *payload=atom_expr2(&arena,atom_symbol(&arena,"Box"),variable);
        Atom *conditions=atom_expr2(&arena,atom_symbol(&arena,"condition"),variable);
        br_packet(&arena,&rows,payload,variable,19,CETTA_CALL_RESULT_EXPRESSION,conditions);
        OutcomeSet projected;outcome_set_init(&projected);
        if(which==13u) {
            VariantBank bank;variant_bank_init(&bank,(CettaVariantShapeOptions){0});
            VariantShape shape;variant_shape_init(&shape);
            assert(variant_shape_from_atom(&bank,payload,&shape));
            rows.items[0].atom=shape.skeleton;rows.items[0].materialized_atom=NULL;
            variant_instance_free(&rows.items[0].variant);
            assert(variant_instance_from_shape(&rows.items[0].variant,&shape));
            assert(outcome_set_add_compacted_variant(&arena,&projected,&rows.items[0],NULL));
            assert(projected.len==1u && projected.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
            assert(atom_eq(projected.items[0].delayed,br_parse(&arena,"(condition 19)")));
            assert(atom_eq(outcome_atom_materialize(&arena,&projected.items[0]),br_parse(&arena,"(Box 19)")));
            outcome_set_free(&projected);outcome_set_free(&rows);
            variant_shape_free(&shape);variant_bank_free(&bank);
            outcome_set_init(&rows);
        } else {
            outcome_set_add_prefixed_outcome(&arena,&projected,&rows.items[0],NULL,false);
            assert(projected.len==1u && projected.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
            assert(atom_eq(projected.items[0].delayed,br_parse(&arena,"(condition 19)")));
            assert(atom_eq(outcome_atom_materialize(&arena,&projected.items[0]),br_parse(&arena,"(Box 19)")));
            outcome_set_free(&projected);
        }
    } else if(which==14u) {
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        EvalCompletionTracker *prior=g_eval_completion_tracker;g_eval_completion_tracker=&tracker;
        CettaCallOutcome suspended=cetta_call_suspended(raw,condition);
        assert(eval_call_outcome_atom(&arena,suspended)==NULL);
        assert(tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        assert(tracker.interrupted && eval_unwinding());
        assert(suspended.term==raw && suspended.delayed==condition);
        g_eval_completion_tracker=prior;
    } else if(which==15u) {
        PrimeEvalStackFrame *frame=prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_FORCE);
        frame->space=&space;frame->arena=&arena;frame->heap_owner=&arena;frame->target=&rows;
        bindings_init(&frame->env);frame->env_initialized=true;
        outcome_set_init(&frame->child);frame->child_initialized=true;
        prime_need_snapshot_init(&frame->before);assert(prime_need_snapshot_begin(&frame->before));
        assert(prime_need_snapshot_allocate(&arena,&frame->before,raw,&frame->before,&frame->thunk_id));
        frame->evaluator_id=prime_need_fresh_evaluator_id();
        assert(prime_need_snapshot_lookup(&frame->before,frame->thunk_id,&frame->cell));
        assert(prime_need_snapshot_start_evaluation(&arena,&frame->before,frame->thunk_id,frame->evaluator_id,&frame->evaluating));
        *bindings_need_mut(&frame->env)=frame->evaluating;
        outcome_set_add(&frame->child,raw,&frame->env);frame->child.items[0].delayed=condition;
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        EvalCompletionTracker *prior=g_eval_completion_tracker;g_eval_completion_tracker=&tracker;
        prime_eval_stack_publish_force(frame);
        assert(rows.len==0u && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        assert(tracker.interrupted && eval_unwinding());
        PrimeNeedCellView same;
        assert(prime_need_snapshot_lookup(&frame->evaluating,frame->thunk_id,&same));
        assert(same.cache_state==PRIME_NEED_CACHE_EVALUATING && !same.cached);
        assert(frame->child.len==1u && frame->child.items[0].delayed==condition);
        g_eval_completion_tracker=prior;prime_eval_stack_frame_free(frame);
    } else if(which==16u) {
        outcome_set_add(&rows,atom_int(&arena,3),&env);rows.items[0].delayed=condition;
        PrimeOrdinarySupport support={.space=&space,.arena=&arena,.snapshot=bindings_need_view(&env),.environment=&env};
        assert(!prime_ordinary_support_outcomes(&support,&rows));
    } else if(which==17u) {
        EvalOwnedAnswers answers={.space=&space};
        CettaCallOutcome returned=cetta_call_failure();
        EvalOwnedResidual residual={.answers=&answers,.arena=&arena,.consume=br_owned_return,.consumer=&returned,.ok=true};
        assert(eval_owned_residual_body(raw,&env,&residual));
        assert(returned.kind==CETTA_CALL_VALUE && returned.result_form==CETTA_CALL_RESULT_EXPRESSION);
        Atom *earned=atom_expr3(&arena,atom_symbol(&arena,"cons-atom"),raw->expr.elems[0],atom_expr(&arena,raw->expr.elems+1u,2u));
        assert(eval_owned_residual_body(earned,&env,&residual));
        assert(returned.kind==CETTA_CALL_VALUE && returned.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
    } else if(which==18u || which==19u) {
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        EvalCompletionTracker *prior=g_eval_completion_tracker;
        g_eval_completion_tracker=which==18u?&tracker:NULL;
        br_native_calls=0u;br_suspend_native=true;
        eval_for_caller(&space,&arena,NULL,br_parse(&arena,"(+ 2 3)"),-1,&env,false,&rows);
        br_suspend_native=false;
        printf("Counted suspended calls %u rows %zu tracked %u\n",
               br_native_calls,(size_t)rows.len,which==18u);fflush(stdout);
        assert(br_native_calls==1u && rows.len==0u);
        if(which==18u)assert(tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        g_eval_completion_tracker=prior;
    } else if(which==20u || which==21u) {
        Atom *x=atom_var(&arena,"br-alias-x"),*y=atom_var(&arena,"br-alias-y");
        Atom *payload=atom_expr3(&arena,atom_symbol(&arena,"Pair"),x,x);
        VariantBank bank;variant_bank_init(&bank,(CettaVariantShapeOptions){0});
        VariantShape shape;variant_shape_init(&shape);
        Outcome row;outcome_init(&row);row.atom=payload;row.result_form=CETTA_CALL_RESULT_EXPRESSION;
        assert(bindings_add_var(&row.env,x,y));
        Atom *condition_x=x;
        if(which==21u) {
            assert(variant_shape_from_atom(&bank,payload,&shape));
            row.atom=shape.skeleton;
            assert(variant_instance_from_shape(&row.variant,&shape));
            condition_x=shape.skeleton->expr.elems[1];
        }
        row.delayed=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"Condition"),condition_x,y,condition_x},4u);
        OutcomeSet first,second;outcome_set_init(&first);outcome_set_init(&second);
        outcome_set_add_prefixed_outcome(&arena,&first,&row,NULL,false);
        assert(first.len==1u && first.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
        Atom *expected_value=atom_expr3(&arena,atom_symbol(&arena,"Pair"),y,y);
        Atom *expected_condition=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"Condition"),y,y,y},4u);
        assert(atom_eq(outcome_atom_materialize(&arena,&first.items[0]),expected_value));
        assert(first.items[0].delayed && atom_eq(first.items[0].delayed,expected_condition));
        assert(bindings_add_var(&env,y,atom_int(&arena,37)));
        outcome_set_add_prefixed_outcome(&arena,&second,&first.items[0],&env,false);
        assert(second.len==1u && second.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
        assert(atom_eq(outcome_atom_materialize(&arena,&second.items[0]),br_parse(&arena,"(Pair 37 37)")));
        assert(second.items[0].delayed && atom_eq(second.items[0].delayed,br_parse(&arena,"(Condition 37 37 37)")));
        outcome_set_free(&first);outcome_set_free(&second);outcome_free_fields(&row);
        variant_shape_free(&shape);variant_bank_free(&bank);
    } else if(which==22u) {
        EvalOwnedAnswers answers={.space=&space};
        CettaCallOutcome returned=cetta_call_failure();
        Atom *hole=atom_var(&arena,"br-resumption");
        EvalOwnedResidual residual={.answers=&answers,.arena=&arena,.resumption=hole,.hole=hole,
            .consume=br_owned_return,.consumer=&returned,.ok=true};
        assert(eval_owned_residual_body(raw,&env,&residual));
        assert(returned.kind==CETTA_CALL_VALUE && returned.result_form==CETTA_CALL_RESULT_EXPRESSION);
        Atom *earned=atom_expr3(&arena,atom_symbol(&arena,"cons-atom"),raw->expr.elems[0],atom_expr(&arena,raw->expr.elems+1u,2u));
        assert(eval_owned_residual_body(earned,&env,&residual));
        assert(returned.kind==CETTA_CALL_VALUE && returned.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
        assert(atom_eq(returned.term,raw));
#ifdef BR_HAS_RETURN_HELPER
    } else if(which>=23u && which<=25u) {
        if(which!=23u) {
            cetta_library_context_free(&context);
            cetta_library_context_init_for_language_profile(&context,
                which==24u?CETTA_LANGUAGE_HE:CETTA_LANGUAGE_PETTA,
                which==24u?cetta_profile_he_compat():cetta_profile_petta_extended());
            eval_set_library_context(&context);
        }
        assert(eval_current_language_id()==(which==23u?CETTA_LANGUAGE_PRIME:
            which==24u?CETTA_LANGUAGE_HE:CETTA_LANGUAGE_PETTA));
        Atom *hole=atom_var(&arena,"br-resumption-slot");
        Atom *callable=br_parse(&arena,"(+ 2 3)");
        assert(bindings_add_var(&env,variable,atom_int(&arena,19)));
        EvalOwnedAnswers answers={.space=&space};
        for(unsigned ready=0u;ready<2u;ready++) {
            CettaCallOutcome returned=cetta_call_failure();
            EvalOwnedResidual residual={.answers=&answers,.arena=&arena,
                .resumption=atom_expr3(&arena,atom_symbol(&arena,"Pair"),hole,variable),.hole=hole,
                .consume=br_owned_return,.consumer=&returned,.ok=true};
            Outcome row;outcome_init(&row);row.atom=callable;
            row.result_form=ready?CETTA_CALL_RESULT_COMPLETED_VALUE:CETTA_CALL_RESULT_EXPRESSION;
            bindings_copy(&row.env,&env);
            EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
            EvalCompletionTracker *prior=g_eval_completion_tracker;g_eval_completion_tracker=&tracker;
            br_native_calls=0u;br_suspend_native=ready && which!=23u;
            bool accepted=eval_owned_residual_return(&row,&residual);
            br_suspend_native=false;g_eval_completion_tracker=prior;
            if(ready && which!=23u) {
                assert(!accepted && !residual.ok && br_native_calls==0u);
                assert(tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
                assert(returned.kind==CETTA_CALL_FAILURE);
                outcome_free_fields(&row);continue;
            }
            assert(accepted);
            Atom *expected=ready?br_parse(&arena,"(Pair (+ 2 3) 19)"):br_parse(&arena,"(Pair 5 19)");
            printf("Resumption language %u ready %u actual %s\n",which,ready,
                returned.term?atom_to_string(&arena,returned.term):"<null>");fflush(stdout);
            assert(returned.kind==CETTA_CALL_VALUE && atom_eq(returned.term,expected));
            assert(returned.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
            outcome_free_fields(&row);
        }
    } else if(which==26u || which==27u) {
        EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
        EvalCompletionTracker *prior=g_eval_completion_tracker;
        g_eval_completion_tracker=which==26u?&tracker:NULL;
        EvalOwnedAnswers answers={.space=&space};
        CettaCallOutcome returned=cetta_call_failure();
        EvalOwnedResidual residual={.answers=&answers,.arena=&arena,
            .consume=br_owned_return,.consumer=&returned,.ok=true};
        Outcome row;outcome_init(&row);row.atom=raw;
        row.result_form=CETTA_CALL_RESULT_EXPRESSION;row.delayed=condition;
        assert(eval_owned_residual_return(&row,&residual));
        assert(returned.kind==CETTA_CALL_SUSPENDED && returned.term==raw &&
            returned.delayed==condition && returned.result_form==CETTA_CALL_RESULT_EXPRESSION);
        returned=cetta_call_failure();residual.hole=atom_var(&arena,"br-conditional-hole");
        residual.resumption=br_parse(&arena,"(+ 2 3)");
        br_native_calls=0u;br_suspend_native=true;
        assert(!eval_owned_residual_return(&row,&residual));
        br_suspend_native=false;
        assert(!residual.ok && br_native_calls==0u && returned.kind==CETTA_CALL_FAILURE);
        assert(row.delayed==condition && row.atom==raw);
        if(which==26u)assert(tracker.interrupted && tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        outcome_free_fields(&row);g_eval_completion_tracker=prior;
#endif
    } else if(which==28u) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        PrimeReplaySyntaxRoot roots[16];const PrimeNeedSnapshot *snapshots[16];
        PrimeNeedRecording *recordings[16];
        outcome_set_add(&rows,atom_int(&arena,7),&env);
        CettaCallResultForm forms[]={CETTA_CALL_RESULT_EXPRESSION,CETTA_CALL_RESULT_COMPLETED_VALUE,
            CETTA_CALL_RESULT_COMPLETED_FRONTIER,(CettaCallResultForm)99};
        for(unsigned i=0u;i<4u;i++) {
            PrimeReplaySourceScan scan={.space=&space,.arena=&arena,.roots=roots,
                .snapshots=snapshots,.recordings=recordings,.capacity=16u};
            rows.items[0].result_form=forms[i];
            bool accepted=prime_replay_source_outcomes(&scan,&rows);
            assert(accepted==(i<2u));
            if(i>=2u)assert(scan.refusal && !strcmp(scan.refusal,"OutcomeResultForm"));
        }
#else
        assert(!"recording-only source scan needs recording support");
#endif
    } else if(which==29u) {
        cetta_library_context_free(&context);
        cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_HE,cetta_profile_he_extended());
        eval_set_library_context(&context);
        assert(eval_current_language_id()==CETTA_LANGUAGE_HE);
        space_add(&space,br_parse(&arena,"(= (br-dialect-source) 7)"));
        for(unsigned mode=0u;mode<3u;mode++) {
            PrimeNativeControl control;
            CettaSearchPolicySpec policy={.native_control=true,.all=true,.work_budget=UINT64_MAX,.workers=1u};
            assert(prime_native_control_init(&control,&space,&arena,br_parse(&arena,"(br-dialect-source)"),
                -1,false,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
            assert(native_dialect_schedule(&control,&space,br_parse(&arena,"(br-dialect-source)"),
                -1,&control.base_environment,NULL,&control.output));
            assert(cetta_continuation_hub_length(&control.hub)==2u);
            PrimeNativeWork *work=cetta_continuation_hub_at(&control.hub,1u)->payload;
            assert(work && work->dialect && work->dialect->machine);
            PrimeEvalStackFrame frame={.kind=PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE,
                .arena=&arena,.dialect_source=work->dialect};
            outcome_set_init(&frame.child);outcome_set_add(&frame.child,atom_int(&arena,7),&env);
            if(mode==1u)frame.child.items[0].delayed=condition;
            if(mode==2u)frame.child.items[0].result_form=(CettaCallResultForm)99;
            PrimeNativeReturnAdvice advice={0};frame.control_return_advice=&advice;
            frame.control_return_advice_length=1u;
            EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE};
            EvalCompletionTracker *prior=g_eval_completion_tracker;g_eval_completion_tracker=&tracker;
            PrimeNativeRunGuard guard;prime_native_run_enter(&control,&guard);
            bool accepted=native_dialect_source_return(&frame,0u);
            prime_native_run_leave(&guard);
            assert(accepted==(mode==0u));
            assert(cetta_continuation_hub_length(&control.hub)==(mode==0u?3u:2u));
            if(mode)assert(control.failed && tracker.interrupted &&
                tracker.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            g_eval_completion_tracker=prior;
            outcome_set_free(&frame.child);prime_native_control_free(&control);
            outcome_set_free(&control.output);
            if(control.base_environment_initialized)bindings_free(&control.base_environment);
        }
    } else if(which==30u) {
        AnswerBank bank;answer_bank_init(&bank);AnswerRef reference;
        assert(answer_bank_add(&bank,raw,&env,NULL,&reference));
        CettaVarMap map;cetta_var_map_init(&map);
        Outcome row;assert(outcome_init_answer_ref(&arena,&row,&bank,reference,&map,NULL));
        row.delayed=condition;
        assert(outcome_set_add_promoted_existing(&owned,&rows,&row,true));
        assert(rows.len==1u && rows.items[0].result_form==CETTA_CALL_RESULT_EXPRESSION);
        assert(rows.items[0].kind==CETTA_OUTCOME_ANSWER_REF && rows.items[0].answer_ref.bank==&bank);
        assert(rows.items[0].delayed && atom_eq(rows.items[0].delayed,condition));
        assert(atom_graph_is_closed_for_arena(&owned,rows.items[0].delayed));
        outcome_free_fields(&row);cetta_var_map_free(&map);answer_bank_free(&bank);
    } else if(which==31u) {
        for(unsigned dialect=0u;dialect<2u;dialect++) {
            CettaLanguageId language=dialect?CETTA_LANGUAGE_PETTA:CETTA_LANGUAGE_HE;
            cetta_library_context_free(&context);
            cetta_library_context_init_for_language_profile(&context,language,
                dialect?cetta_profile_petta_extended():cetta_profile_he_compat());
            eval_set_library_context(&context);assert(eval_current_language_id()==language);
            Atom *hole=atom_var(&arena,"br-inert-slot");
            Atom *value=br_parse(&arena,"(Box 7)");
            EvalOwnedAnswers answers={.space=&space};
            CettaCallOutcome returned=cetta_call_failure();
            EvalOwnedResidual residual={.answers=&answers,.arena=&arena,
                .resumption=atom_expr3(&arena,atom_symbol(&arena,"Pair"),hole,atom_int(&arena,19)),
                .hole=hole,.consume=br_owned_return,.consumer=&returned,.ok=true};
            Outcome row;outcome_init(&row);row.atom=value;
            assert(eval_owned_residual_return(&row,&residual));
            assert(returned.kind==CETTA_CALL_VALUE && returned.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
            assert(atom_eq(returned.term,br_parse(&arena,"(Pair (Box 7) 19)")));
            outcome_free_fields(&row);
            Atom *producer=br_parse(&arena,"(br-callable-producer)");
            space_add(&space,br_parse(&arena,"(= (br-callable-producer) (cons-atom + (2 3)))"));
            EvalOwnedAnswers refused;br_native_calls=0u;br_suspend_native=true;
            assert(!eval_owned_answers_open(&refused,&space,producer,&env,-1,EVAL_OWNED_DATA));
            Atom *quoted=br_parse(&arena,"(br-quoted-producer)");
            space_add(&space,br_parse(&arena,"(= (br-quoted-producer) (quote (+ 2 3)))"));
            assert(!eval_owned_answers_open(&refused,&space,quoted,&env,-1,EVAL_OWNED_DATA));
            br_suspend_native=false;assert(br_native_calls==0u);
        }
    } else assert(false);
    printf("BindingRole %u passed\n",which);
    if(which==0u)printf("Sizes Outcome %zu Frame %zu\n",sizeof(Outcome),sizeof(PrimeEvalStackFrame));
    outcome_set_free(&rows);bindings_free(&env);space_free(&space);
    arena_free(&owned);arena_free(&arena);
    cetta_library_context_free(&context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=prior_registry;
}
static Atom *projected_own_refuse(Arena *arena, Atom *list) {
    (void)arena;(void)list;return NULL;
}
static void projected_own_case(unsigned which) {
    Arena arena;arena_init(&arena);
    Bindings env;bindings_init(&env);
    const BindingsStructureHooks *prior=bindings_structure_hooks();
    bindings_structure_hooks_set(prime_semantics_binding_hooks());
    Atom *x=atom_var(&arena,"projection-owned-x");
    Atom *y=atom_var(&arena,"projection-owned-y");
    Atom *record=atom_symbol(&arena,"mercury-implicit/per-call/reference");
    Atom *own=atom_expr(&arena,(Atom *[]){atom_internal_tag(&arena,CETTA_INTERNAL_TAG_PRIME_OWN),record,x,y},4u);
    Atom *result=NULL;
    if(which==0u) {
        assert(bindings_add_var(&env,x,atom_int(&arena,5)));
        Atom *parameter=atom_symbol(&arena,"projection-z");
        Atom *body=atom_expr3(&arena,atom_symbol(&arena,"Pair"),parameter,x);
        Atom *lambda=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"lam"),parameter,body,own},4u);
        bool ok=false;result=bindings_apply_body_exact_env(&arena,lambda,&env,&ok);
        assert(ok && result && result->expr.len==4u);
        Atom *image=result->expr.elems[3];
        assert(atom_is_prime_own_list(image) && image->expr.len==3u);
        assert(image->expr.elems[0]==own->expr.elems[0] && image->expr.elems[1]==record);
        assert(image->expr.elems[2]->kind==ATOM_VAR && var_id_eq(image->expr.elems[2]->var_id, y->var_id));
        assert(prime_semantics_is_authored_lambda(&arena,result));
        Atom *application=atom_expr2(&arena,result,atom_int(&arena,1));
        Atom *answer=prime_semantics_authored_head_step(&arena,application);
        assert(answer && atom_eq(answer,atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_int(&arena,1),atom_int(&arena,5))));
    } else if(which==1u) {
        assert(bindings_add_var(&env,x,y));
        result=bindings_apply_projected_body_visible(&env,&arena,own);
        assert(atom_is_prime_own_list(result) && result->expr.len==3u);
        assert(result->expr.elems[1]==record && var_id_eq(result->expr.elems[2]->var_id, y->var_id));
    } else if(which==2u) {
        Atom *value=atom_expr2(&arena,atom_symbol(&arena,"Box"),y);
        assert(bindings_add_var(&env,x,value));
        assert(bindings_add_var(&env,y,atom_int(&arena,5)));
        result=bindings_apply_projected_body_visible(&env,&arena,x);
        assert(result && atom_eq(result,value));
        assert(result->expr.elems[1]->kind==ATOM_VAR && var_id_eq(result->expr.elems[1]->var_id, y->var_id));
    } else if(which==3u) {
        BindingsStructureHooks rejecting=*prime_semantics_binding_hooks();
        rejecting.own_list=projected_own_refuse;bindings_structure_hooks_set(&rejecting);
        assert(bindings_add_var(&env,x,atom_int(&arena,5)));
        bool ok=true;result=bindings_apply_body_exact_env(&arena,own,&env,&ok);
        assert(!result && !ok);
        bindings_structure_hooks_set(prime_semantics_binding_hooks());
    } else if(which==4u) {
        Atom *other=atom_var(&arena,"projection-other");
        assert(bindings_add_var(&env,other,atom_int(&arena,5)));
        result=bindings_apply_projected_body_visible(&env,&arena,own);
        assert(result==own && result->expr.elems[1]==record);
    } else if(which==5u) {
        Atom *ordinary=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"OWN"),record,x,y},4u);
        assert(bindings_add_var(&env,x,atom_int(&arena,5)));
        result=bindings_apply_projected_body_visible(&env,&arena,ordinary);
        assert(result && result->expr.len==4u && !atom_sequence_is_own_list(result));
        assert(result->expr.elems[1]==record && result->expr.elems[2]->kind==ATOM_GROUNDED && result->expr.elems[2]->ground.ival==5);
        assert(result->expr.elems[3]->kind==ATOM_VAR && var_id_eq(result->expr.elems[3]->var_id, y->var_id));
    } else assert(false);
    bindings_structure_hooks_set(prior);bindings_free(&env);arena_free(&arena);
    printf("ProjectedOwn %u passed\n",which);
}

static bool stored_head_has_error(Atom *value) {
    if (!value) return false;
    if (atom_is_error(value) && value->expr.len >= 3u &&
        atom_is_symbol(value->expr.elems[2], "suspension:force-denied")) return true;
    if (value->kind == ATOM_EXPR)
        for (CettaExprIndex i=0u;i<value->expr.len;i++)
            if (stored_head_has_error(value->expr.elems[i])) return true;
    return false;
}
static void stored_head_case(unsigned which) {
    assert(which < 12u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);assert(eval_current_language_id()==CETTA_LANGUAGE_PRIME);
    Arena arena;arena_init(&arena);Space space;space_init(&space);
    Registry registry;registry_init(&registry);Registry *prior_registry=g_registry;g_registry=&registry;
    Bindings lexical,env;bindings_init(&lexical);bindings_init(&env);
    Atom *origin=br_parse(&arena,"(chain (println! StoredHeadForce) $ignored (lam sh:z (Pair Chosen sh:z)))");
    if(which==2u || which==3u)
        origin=br_parse(&arena,"(superpose ((lam sh:z (Pair A sh:z)) (lam sh:z (Pair B sh:z))))");
    Atom *reference=NULL;
    assert(prime_need_allocate_source_argument_ref(&arena,origin,prime_need_fresh_source_occurrence(),0u,&lexical,&env,&reference));
    uint64_t cell_id=0u;assert(prime_need_ref_belongs_to(reference,bindings_need_view(&env),&cell_id));
    PrimeNeedCellView before,after;assert(prime_need_snapshot_lookup(bindings_need_view(&env),cell_id,&before));
    if(which==4u || which==9u) {
        uint64_t evaluator=prime_need_fresh_evaluator_id();PrimeNeedSnapshot running,done;
        assert(prime_need_snapshot_start_evaluation(&arena,bindings_need_view(&env),cell_id,evaluator,&running));
        Atom *earned=br_parse(&arena,which==4u?"(lam sh:z (Pair Cached sh:z))":"(chain (println! StoredHeadForbidden) $ignored (lam sh:z sh:z))");
        assert(prime_need_snapshot_resolve_ready_value(&arena,&running,cell_id,evaluator,earned,&done));
        *bindings_need_mut(&env)=done;
        assert(prime_need_snapshot_lookup(bindings_need_view(&env),cell_id,&before));
    }
    uint32_t rights=which==5u?CETTA_PRIME_NEED_RIGHT_INSPECT:which==6u?CETTA_PRIME_NEED_RIGHT_FORCE:CETTA_PRIME_NEED_RIGHT_ALL;
    Atom *restricted=prime_need_ref_restrict(&arena,reference,bindings_need_view(&env),rights);assert(restricted);
    Atom *stored=prime_need_persist_data(&arena,restricted,&env);assert(stored);
    uint64_t key=0u;uint32_t stored_rights=0u;Atom *stored_origin=NULL;
    if(which==4u || which==9u) {
        assert(atom_eq(stored,before.cached));
        assert(!prime_need_is_stored_thunk(stored,NULL,NULL,NULL));
        key=before.storage_key;
    } else {
        assert(prime_need_is_stored_thunk(stored,&key,&stored_rights,&stored_origin));
        assert(key==before.storage_key && stored_rights==rights && atom_eq(stored_origin,before.origin));
    }
    Atom *ready=NULL;assert(prime_need_ready_value_ref(&arena,stored,&env,&ready));
    Atom *second=ready;uint64_t second_id=cell_id;
    if(which==3u) {
        Bindings next;Atom *other=NULL;
        assert(prime_need_allocate_source_argument_ref(&arena,origin,prime_need_fresh_source_occurrence(),0u,&env,&next,&other));
        bindings_replace(&env,&next);
        assert(prime_need_ref_belongs_to(other,bindings_need_view(&env),&second_id) && second_id!=cell_id);
        Atom *other_stored=prime_need_persist_data(&arena,other,&env);
        uint64_t other_key=0u;assert(prime_need_is_stored_thunk(other_stored,&other_key,NULL,NULL) && other_key!=key);
        assert(prime_need_ready_value_ref(&arena,other_stored,&env,&second));
    }
    OutcomeSet rows;outcome_set_init(&rows);
    if(which==7u || which==8u || which==11u) {
        Atom *head=stored;
        if(which==7u) {
            Atom *promise=atom_expr2(&arena,atom_symbol_id(&arena,prime_need_symbols()->promise),reference);
            head=prime_need_persist_data(&arena,promise,&env);
            assert(prime_need_is_stored_promise(head,NULL,NULL,NULL));
        } else if(which==8u) head=atom_prime_held_wrap(&arena,stored);
        Atom *application=atom_expr2(&arena,head,atom_int(&arena,1));
        Atom *step=prime_runtime_head_step(&space,&arena,application);
        if(which!=11u)assert(step==application);
        else {
            assert(step && step->kind==ATOM_EXPR && step->expr.len==4u);
            assert(atom_is_symbol_id(step->expr.elems[0],g_builtin_syms.chain));
            assert(step->expr.elems[1]==stored && step->expr.elems[2]->kind==ATOM_VAR);
            assert(step->expr.elems[3]->expr.elems[0]==step->expr.elems[2]);
            assert(step->expr.elems[3]->expr.elems[1]==application->expr.elems[1]);
        }
    } else {
        Atom *query=which==10u?atom_expr3(&arena,atom_symbol(&arena,"Pair"),ready,ready):
            atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_expr2(&arena,ready,atom_int(&arena,1)),atom_expr2(&arena,second,atom_int(&arena,2)));
        int fuel=which==1u?20000:-1;
        eval_for_caller(&space,&arena,NULL,query,fuel,&env,true,&rows);
        prime_need_observe_tuple_returns(&space,&arena,&rows,fuel,NULL,NULL);
        size_t expected=which==2u?2u:which==3u?4u:1u;
        assert(rows.len==expected);
        for(CettaCount i=0u;i<rows.len;i++) {
            Atom *value=outcome_atom_materialize(&arena,&rows.items[i]);assert(value);
            if(which==5u)assert(stored_head_has_error(value));
            else if(which!=9u && which!=10u) {
                const char *a=which==2u?(i?"B":"A"):which==3u?(i>=2u?"B":"A"):which==4u?"Cached":"Chosen";
                const char *b=which==3u?(i%2u?"B":"A"):a;
                Atom *want=atom_expr3(&arena,atom_symbol(&arena,"Pair"),
                    atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_symbol(&arena,a),atom_int(&arena,1)),
                    atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_symbol(&arena,b),atom_int(&arena,2)));
                assert(atom_eq(value,want));
            } else if(which==9u) {
                Atom *want=atom_expr3(&arena,atom_symbol(&arena,"Pair"),
                    atom_expr2(&arena,before.cached,atom_int(&arena,1)),atom_expr2(&arena,before.cached,atom_int(&arena,2)));
                assert(atom_eq(prime_public_bound_operand_syntax(&arena,value,&rows.items[i].env),want));
            }
            PrimeNeedCellView got;
            assert(prime_need_snapshot_lookup(bindings_need_view(&rows.items[i].env),cell_id,&got));
            assert(got.storage_key==key && atom_eq(got.origin,before.origin));
            assert(got.cache_state==((which==5u || which==10u)?PRIME_NEED_CACHE_EMPTY:PRIME_NEED_CACHE_VALUE));
            if(which==4u || which==9u)assert(got.cached==before.cached && got.value_role==PRIME_NEED_VALUE_READY);
            if(which==3u) {
                PrimeNeedCellView another;assert(prime_need_snapshot_lookup(bindings_need_view(&rows.items[i].env),second_id,&another));
                assert(another.cache_state==PRIME_NEED_CACHE_VALUE && another.storage_key!=key);
            }
        }
    }
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),cell_id,&after));
    assert(before.origin==after.origin && before.cached==after.cached && before.cache_state==after.cache_state && before.value_role==after.value_role);
    printf("StoredHead %u passed\n",which);
    outcome_set_free(&rows);bindings_free(&env);bindings_free(&lexical);g_registry=prior_registry;registry_free(&registry);
    space_free(&space);arena_free(&arena);cetta_library_context_free(&context);eval_set_library_context(NULL);
}

static void stored_return_role_case(unsigned which) {
    assert(which < 15u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);
    Arena arena;arena_init(&arena);Space projection_space;space_init(&projection_space);
    Bindings lexical,env;bindings_init(&lexical);bindings_init(&env);
    Atom *variable=atom_var(&arena,"stored-capture");
    Atom *origin=which==8u ? atom_expr2(&arena,atom_symbol(&arena,"Source"),variable)
        : br_parse(&arena,"(println! MustNotRunDuringStorage)");
    Atom *reference=NULL;
    assert(prime_need_allocate_source_argument_ref(&arena,origin,
        prime_need_fresh_source_occurrence(),0u,&lexical,&env,&reference));
    uint64_t id=0u;
    assert(prime_need_ref_belongs_to(reference,bindings_need_view(&env),&id));
    PrimeNeedCellView before,after;
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),id,&before));
    Atom *earned=which==8u ? atom_expr2(&arena,atom_symbol(&arena,"Value"),variable)
        : br_parse(&arena,"(+ 1 2)");
    uint64_t child_key=0u;
    if(which==7u || which==9u) {
        Bindings child_env;Atom *child=NULL;
        Atom *child_origin=br_parse(&arena,"(superpose (A B))");
        Atom *child_value=NULL;
        if(which==9u) {
            Bindings leaf_env;Atom *leaf=NULL;
            assert(prime_need_allocate_source_argument_ref(&arena,
                atom_symbol(&arena,"LeafSource"),
                prime_need_fresh_source_occurrence(),0u,&env,&leaf_env,&leaf));
            bindings_replace(&env,&leaf_env);
            uint64_t leaf_id=0u,leaf_evaluator=prime_need_fresh_evaluator_id();
            PrimeNeedSnapshot leaf_running,leaf_ready;
            assert(prime_need_ref_belongs_to(leaf,
                bindings_need_view(&env),&leaf_id));
            assert(prime_need_snapshot_start_evaluation(&arena,
                bindings_need_view(&env),leaf_id,leaf_evaluator,&leaf_running));
            assert(prime_need_snapshot_resolve_ready_value(&arena,&leaf_running,
                leaf_id,leaf_evaluator,atom_int(&arena,31),&leaf_ready));
            *bindings_need_mut(&env)=leaf_ready;
            leaf=prime_need_ref(&arena,&leaf_ready,leaf_id);assert(leaf);
            child_origin=atom_symbol(&arena,"NodeSource");
            child_value=atom_expr3(&arena,atom_symbol(&arena,"Node"),leaf,leaf);
        }
        assert(prime_need_allocate_source_argument_ref(&arena,
            child_origin,prime_need_fresh_source_occurrence(),
            0u,&env,&child_env,&child));
        bindings_replace(&env,&child_env);
        uint64_t child_id=0u;PrimeNeedCellView child_view;
        assert(prime_need_ref_belongs_to(child,bindings_need_view(&env),&child_id));
        assert(prime_need_snapshot_lookup(bindings_need_view(&env),child_id,&child_view));
        child_key=child_view.storage_key;
        if(which==9u) {
            uint64_t child_evaluator=prime_need_fresh_evaluator_id();
            PrimeNeedSnapshot child_running,child_ready;
            assert(prime_need_snapshot_start_evaluation(&arena,
                bindings_need_view(&env),child_id,child_evaluator,&child_running));
            assert(prime_need_snapshot_resolve_ready_value(&arena,&child_running,
                child_id,child_evaluator,child_value,&child_ready));
            *bindings_need_mut(&env)=child_ready;
        }
        earned=atom_expr3(&arena,atom_symbol(&arena,"Pair"),child,child);
    }
    if(which!=0u) {
        uint64_t evaluator=prime_need_fresh_evaluator_id();PrimeNeedSnapshot running,done;
        assert(prime_need_snapshot_start_evaluation(&arena,bindings_need_view(&env),
            id,evaluator,&running));
        if(which==13u)
            done=running;
        else if(which==1u)
            assert(prime_need_snapshot_resolve_value(&arena,&running,id,evaluator,earned,&done));
        else if(which==2u)
            assert(prime_need_snapshot_resolve_stable_fault(&arena,&running,id,evaluator,earned,&done));
        else
            assert(prime_need_snapshot_resolve_ready_value(&arena,&running,id,evaluator,earned,&done));
        *bindings_need_mut(&env)=done;
    }
    if(which==8u)assert(bindings_add_var_owned(&env,variable,atom_int(&arena,17)));
    uint32_t rights=which==4u?CETTA_PRIME_NEED_RIGHT_INSPECT:
        which==5u?CETTA_PRIME_NEED_RIGHT_FORCE:CETTA_PRIME_NEED_RIGHT_ALL;
    Atom *restricted=prime_need_ref_restrict(&arena,reference,bindings_need_view(&env),rights);
    assert(restricted && prime_need_snapshot_lookup(bindings_need_view(&env),id,&before));
    bool selected=which>=3u && which!=4u && which!=13u;
    assert(prime_need_completed_persistence_value(&before,rights,false)==selected);
    assert(!prime_need_completed_persistence_value(&before,rights,true));
    assert(!prime_need_completed_persistence_value(NULL,rights,false));
    Atom *payload=which==6u ? atom_expr2(&arena,
        atom_symbol_id(&arena,prime_need_symbols()->promise),restricted) : restricted;
    Atom *stored=prime_need_persist_data(&arena,payload,&env);assert(stored);
    if(which==6u || !selected) {
        uint64_t key=0u;uint32_t kept_rights=0u;Atom *kept_origin=NULL;
        bool valid=which==6u
            ? prime_need_is_stored_promise(stored,&key,&kept_rights,&kept_origin)
            : prime_need_is_stored_thunk(stored,&key,&kept_rights,&kept_origin);
        assert(valid && key==before.storage_key && kept_rights==rights);
        assert(atom_eq(kept_origin,before.origin));
    } else if(which==7u) {
        assert(stored->kind==ATOM_EXPR && stored->expr.len==3u);
        uint64_t left=0u,right=0u;
        assert(prime_need_is_stored_thunk(stored->expr.elems[1],&left,NULL,NULL));
        assert(prime_need_is_stored_thunk(stored->expr.elems[2],&right,NULL,NULL));
        assert(left==child_key && right==child_key);
    } else if(which==9u) {
        Atom *node=br_parse(&arena,"(Node 31 31)");
        Atom *expected=atom_expr3(&arena,atom_symbol(&arena,"Pair"),node,node);
        assert(atom_eq(stored,expected));
    } else {
        Atom *expected=which==8u ? br_parse(&arena,"(Value 17)") : earned;
        assert(atom_eq(stored,expected));
        assert(!prime_need_is_stored_thunk(stored,NULL,NULL,NULL));
    }
    Atom *ready_input=payload;
    if(which==10u)ready_input=atom_expr3(&arena,
        atom_symbol_id(&arena,g_builtin_syms.prime_lam),
        atom_var_with_id(&arena,"role:x",fresh_var_id()),restricted);
    if(which==11u)ready_input=atom_expr2(&arena,
        atom_symbol_id(&arena,g_builtin_syms.quote),restricted);
    if(which==12u)ready_input=atom_prime_context_bind(&arena,NULL,
        atom_symbol(&arena,"role:key"),restricted);
    if(which==14u) {
        PrimeNeedSnapshot foreign;
        prime_need_snapshot_init(&foreign);
        assert(prime_need_snapshot_begin(&foreign));
        uint64_t foreign_id=0u;
        assert(prime_need_snapshot_allocate(&arena,&foreign,
            atom_symbol(&arena,"ForeignSource"),&foreign,&foreign_id));
        uint64_t foreign_evaluator=prime_need_fresh_evaluator_id();
        PrimeNeedSnapshot foreign_running,foreign_ready;
        assert(prime_need_snapshot_start_evaluation(&arena,&foreign,
            foreign_id,foreign_evaluator,&foreign_running));
        assert(prime_need_snapshot_resolve_ready_value(&arena,&foreign_running,
            foreign_id,foreign_evaluator,atom_int(&arena,53),&foreign_ready));
        ready_input=prime_need_ref(&arena,&foreign_ready,foreign_id);
        assert(ready_input);
    }
    Atom *projected=prime_need_project_ready_call(
        &projection_space,&arena,ready_input,&env);
    if(which==0u || which==1u || which==2u || which==4u || which==7u ||
        which==13u) {
        assert(!projected);
    } else if(which==6u || which==10u || which==11u || which==12u ||
              which==14u) {
        assert(projected && atom_eq(projected,ready_input));
        assert(prime_need_atom_contains_private_capability(projected));
    } else if(which==9u) {
        assert(projected && projected->kind==ATOM_EXPR &&
            projected->expr.len==3u &&
            projected->expr.elems[1]==projected->expr.elems[2]);
        assert(atom_eq(projected,br_parse(&arena,"(Pair (Node 31 31) (Node 31 31))")));
    } else {
        Atom *expected=which==8u ? br_parse(&arena,"(Value 17)") : earned;
        assert(projected && atom_eq(projected,expected));
        assert(!prime_need_atom_contains_private_capability(projected));
    }
    assert(prime_need_snapshot_lookup(bindings_need_view(&env),id,&after));
    assert(after.origin==before.origin && after.cached==before.cached &&
        after.cache_state==before.cache_state && after.value_role==before.value_role);
    printf("StoredReturnRole %u passed\n",which);
    bindings_free(&env);bindings_free(&lexical);space_free(&projection_space);arena_free(&arena);
    cetta_library_context_free(&context);eval_set_library_context(NULL);
}

static void capsule_storage_case(PrimeCapsuleSearch *search) {
    assert(search && search->certificate);
    uint64_t transitions = search->transitions, resumes = search->resumes;
    CettaStorageCensus census = {0};
    prime_capsule_search_storage(search, &census);
    cetta_storage_finish(&census);
    if (census.incomplete)
        fprintf(stderr, "capsule storage refusal: %s\n", census.incomplete);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    assert(census.retained >= sizeof(*search));
    bool program = false, view = search->projection_lease == NULL;
    for (size_t i = 0u; i < census.capacity; i++) {
        if (census.slots[i].domain != CETTA_STORAGE_BLOCK) continue;
        program |= census.slots[i].identity == search->certificate;
        view |= census.slots[i].identity == search->projection_lease;
    }
    assert(program && view);
    size_t retained = census.retained;
    prime_capsule_search_storage(search, &census);
    petta_program_revision_view_storage_census(search->projection_lease, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && census.retained == retained && census.repeated > 0u);
    assert(search->transitions == transitions && search->resumes == resumes);
    cetta_storage_census_free(&census);

    /* Invalid container bounds still refuse rather than omitting storage. */
    PettaEvalTransaction unsupported = {.space_len = 1u};
    assert(!search->context.transaction);
    search->context.transaction = &unsupported;
    prime_capsule_search_storage(search, &census);
    cetta_storage_finish(&census);
    assert(census.incomplete && !strcmp(census.incomplete, "TransactionStorageShape"));
    search->context.transaction = NULL;
    cetta_storage_census_free(&census);
    if (search->service) {
        PrimeCapsuleService *service = search->service;
        PrimeEvalStackTask *active = service->source_driver.active_task;
        service->source_driver.active_task = &service->source_driver.task;
        prime_capsule_search_storage(search, &census);
        cetta_storage_finish(&census);
        assert(census.incomplete && !strcmp(census.incomplete, "ActiveEvaluationDriver"));
        service->source_driver.active_task = active;
        cetta_storage_census_free(&census);
        size_t length = service->exported_length;
        assert(service->exported_capacity < SIZE_MAX);
        service->exported_length = service->exported_capacity + 1u;
        prime_capsule_search_storage(search, &census);
        cetta_storage_finish(&census);
        assert(census.incomplete && !strcmp(census.incomplete, "CapsuleExportShape"));
        service->exported_length = length;
        cetta_storage_census_free(&census);
    }
}

static bool census_has_block(const CettaStorageCensus *census,
        const void *identity, size_t bytes) {
    for (size_t i = 0u; i < census->capacity; i++) {
        const CettaStorageObject *item = &census->slots[i];
        if (item->identity == identity && item->domain == CETTA_STORAGE_BLOCK)
            return item->bytes == bytes;
    }
    return false;
}

static void transaction_storage_case(void) {
    CettaLibraryContext library;
    cetta_library_context_init_for_language_profile(&library,
        CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(&library);
    Arena arena;
    arena_init(&arena);
    Space source;
    space_init(&source);
    space_add(&source, br_parse(&arena, "(source A)"));
    Registry registry;
    registry_init(&registry);
    registry_bind(&registry, "&self", atom_space(&arena, &source));
    for (unsigned i = 0u; i < 20u; i++) {
        char name[32];
        snprintf(name, sizeof(name), "&storage-%u", i);
        registry_bind(&registry, name, atom_int(&arena, i));
    }
    Registry *previous_registry = g_registry;
    g_registry = &registry;
    PettaEvalMachineContext context = {.library_context = &library, .fuel = -1};
    StateCell original = {.value = atom_int(&arena, 6),
        .content_type = atom_symbol_id(&arena, g_builtin_syms.undefined_type)};
    void *outer_handle = NULL, *inner_handle = NULL;
    Space *outer_space = NULL, *inner_space = NULL;
    assert(petta_eval_machine_transaction_begin(&context, &source, &arena,
        &outer_handle, &outer_space));
    PettaEvalTransaction *outer = outer_handle;
    StateCell *outer_state = petta_eval_transaction_clone_state(outer, &original);
    assert(outer_state && context.transaction == outer);
    assert(petta_eval_machine_transaction_begin(&context, outer_space, &arena,
        &inner_handle, &inner_space));
    PettaEvalTransaction *inner = inner_handle;
    StateCell *inner_state = petta_eval_transaction_clone_state(inner, outer_state);
    assert(inner_state && inner->parent == outer && context.transaction == inner);
    inner_state->value = atom_int(&arena, 7);
    space_add(inner_space, br_parse(&arena, "(private B)"));
    uint64_t authority = context.semantic_authority_epoch;
    SpaceReadToken source_read = space_read_token(&source);
    uint32_t registry_length = registry.len;
    CettaStorageCensus census = {0};
    petta_eval_machine_context_storage(&context, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && !census.overflow);
    assert(census_has_block(&census, outer, sizeof(*outer)));
    assert(census_has_block(&census, inner, sizeof(*inner)));
    assert(census_has_block(&census, outer_space, sizeof(*outer_space)));
    assert(census_has_block(&census, inner_space, sizeof(*inner_space)));
    assert(census_has_block(&census, outer_state, sizeof(*outer_state)));
    assert(census_has_block(&census, inner_state, sizeof(*inner_state)));
    assert(census_has_block(&census, inner->initial_registry_values,
        inner->initial_registry_len * sizeof(*inner->initial_registry_values)));
    size_t both = census.retained;
    petta_eval_transaction_storage(outer, &census);
    petta_eval_machine_context_storage(&context, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && census.retained == both && census.repeated > 0u);
    assert(context.semantic_authority_epoch == authority);
    assert(space_read_token_is_current(source_read) && registry.len == registry_length);
    assert(original.value->ground.ival == 6 && outer_state->value->ground.ival == 6);
    assert(inner_state->value->ground.ival == 7 && space_length64(&source) == 1u);
    cetta_storage_census_free(&census);

    size_t capacity = inner->space_cap;
    inner->space_cap = 0u;
    petta_eval_transaction_storage(inner, &census);
    cetta_storage_finish(&census);
    assert(census.incomplete && !strcmp(census.incomplete, "TransactionStorageShape"));
    inner->space_cap = capacity;
    cetta_storage_census_free(&census);

    petta_eval_machine_transaction_rollback(&context, inner);
    assert(context.transaction == outer);
    petta_eval_machine_context_storage(&context, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && census.retained < both);
    assert(census_has_block(&census, outer_state, sizeof(*outer_state)));
    cetta_storage_census_free(&census);
    petta_eval_machine_transaction_rollback(&context, outer);
    assert(!context.transaction && original.value->ground.ival == 6);
    petta_eval_machine_context_storage(&context, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && census.retained == 0u);
    cetta_storage_census_free(&census);
    printf("Nested transaction storage passed\n");
    g_registry = previous_registry;
    registry_free(&registry);
    space_free(&source);
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(&library);
}

static void pinned_revision_storage_case(void) {
    Arena arena;
    arena_init(&arena);
    Space source;
    space_init(&source);
    space_add(&source, br_parse(&arena, "(= (storage:revision) A)"));
    PettaProgram *program = petta_program_new_with_host_intrinsics(NULL);
    assert(program);
    assert(petta_program_synchronize_space(program, &source));
    PettaProgramRevisionView *held = petta_program_revision_view_capture(program, &source);
    assert(held);
    CettaStorageCensus before = {0}, after = {0};
    petta_program_revision_view_storage_census(held, &before);
    cetta_storage_finish(&before);
    assert(!before.incomplete && !before.overflow && before.retained > 0u);
    space_add(&source, br_parse(&arena, "(= (storage:revision) B)"));
    assert(petta_program_synchronize_space(program, &source));
    PettaProgramRevisionView *fresh = petta_program_revision_view_capture(program, &source);
    assert(fresh && fresh != held);
    petta_program_revision_view_free(fresh);
    petta_program_free(program);
    /* The detached old view owns its catalog even after the live program
     * retires. Source syntax is still held by this test's arena. */
    petta_program_revision_view_storage_census(held, &after);
    cetta_storage_finish(&after);
    assert(!after.incomplete && !after.overflow && after.retained == before.retained);
    size_t retained = after.retained;
    petta_program_revision_view_storage_census(held, &after);
    cetta_storage_finish(&after);
    assert(!after.incomplete && after.retained == retained && after.repeated > 0u);
    cetta_storage_census_free(&after);
    cetta_storage_census_free(&before);
    petta_program_revision_view_free(held);
    space_free(&source);
    arena_free(&arena);
}

#if CETTA_BUILD_WITH_RUNTIME_STATS
static uint64_t numeric_capsule_counter(const char *name) {
    CettaRuntimeStats stats; cetta_runtime_stats_snapshot(&stats);
    for (unsigned i=0u;i<CETTA_RUNTIME_COUNTER_COUNT;i++)
        if (!strcmp(cetta_runtime_counter_name((CettaRuntimeCounter)i),name))
            return stats.counters[i];
    return 0u;
}
static void numeric_capsule_expect_value(PrimeEvalStackDriver *driver,
        PrimeEvalStackFrame *frame, int64_t expected) {
    for(unsigned n=0u;n<100u && !frame->child.len && !driver->capacity_blocked;n++)
        prime_capsule_search_resume(frame);
    assert(!driver->capacity_blocked && frame->child.len==1u);
    Atom *value=outcome_atom_materialize(frame->arena,&frame->child.items[0]);
    assert(value && value->kind==ATOM_GROUNDED && value->ground.gkind==GV_INT &&
        value->ground.ival==expected);
    assert(frame->child.items[0].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
}
static void numeric_capsule_account_case(unsigned which) {
    assert(which<21u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    assert(eval_current_language_id()==CETTA_LANGUAGE_PRIME);
    Arena arena,foreign;arena_init(&arena);arena_init(&foreign);
    Space space;space_init(&space);Registry registry;registry_init(&registry);
    Registry *prior_registry=g_registry;g_registry=&registry;
    Arena *prior_need=g_prime_need_owner,*prior_episode=g_prime_need_episode_owner;
    g_prime_need_owner=g_prime_need_episode_owner=&arena;
    Atom *add_equation=br_parse(&arena,"(= (nc:add $x $y) (+ $x $y))");
    space_add(&space,add_equation);
    space_add(&space,br_parse(&arena,"(= (nc:discr $x A) $x)"));
    OutcomeSet out;outcome_set_init_with_owner(&out,&arena);
    Bindings environment;bindings_init(&environment);
    PrimeEvalStackDriver driver={.arena=&arena,.root_target=&out,.raw_call_depth=1u,
        .prepared_pure_cache={.root_space=&space}};
    PrimeEvalStackDriver *prior_driver=g_prime_eval_stack_driver;g_prime_eval_stack_driver=&driver;
    Atom *call=br_parse(&arena,"(nc:add 4 6)");
    if(which==7u)call=atom_expr3(&arena,atom_symbol(&arena,"nc:add"),atom_float(&arena,NAN),atom_float(&arena,1.0));
    if(which>=8u && which<16u) {
        Atom *stored=NULL,*ready_ref=NULL;
        if(which==8u || which==9u) {
            Bindings pending;Atom *pending_ref=NULL;
            assert(prime_need_allocate_source_argument_ref(&arena,
                atom_symbol(&arena,"A"),prime_need_fresh_source_occurrence(),
                0u,&environment,&pending,&pending_ref));
            bindings_replace(&environment,&pending);
            stored=prime_need_persist_data(&arena,pending_ref,&environment);
            assert(stored && prime_need_is_stored_thunk(stored,NULL,NULL,NULL));
        }
        if(which>=9u) {
            Bindings pending;Atom *source_ref=NULL;
            assert(prime_need_allocate_source_argument_ref(&arena,
                br_parse(&arena,"(+ 100 200)"),
                prime_need_fresh_source_occurrence(),0u,
                &environment,&pending,&source_ref));
            bindings_replace(&environment,&pending);
            uint64_t id=0u,evaluator=prime_need_fresh_evaluator_id();
            PrimeNeedSnapshot running,ready;
            assert(prime_need_ref_belongs_to(source_ref,
                bindings_need_view(&environment),&id));
            assert(prime_need_snapshot_start_evaluation(&arena,
                bindings_need_view(&environment),id,evaluator,&running));
            if(which==14u) {
                *bindings_need_mut(&environment)=running;
                ready_ref=prime_need_ref(&arena,&running,id);
            } else {
                Atom *ready_value=which==13u
                    ? br_parse(&arena,"(+ 1 2)") : atom_int(&arena,4);
                assert(prime_need_snapshot_resolve_ready_value(&arena,&running,
                    id,evaluator,ready_value,&ready));
                *bindings_need_mut(&environment)=ready;
                ready_ref=prime_need_ref(&arena,&ready,id);
            }
            assert(ready_ref);
            if(which==11u)ready_ref=prime_need_ref_restrict(&arena,ready_ref,
                &ready,CETTA_PRIME_NEED_RIGHT_FORCE);
            if(which==12u)ready_ref=prime_need_ref_restrict(&arena,ready_ref,
                &ready,CETTA_PRIME_NEED_RIGHT_INSPECT);
            if(which==15u) {
                PrimeNeedSnapshot foreign_world;
                prime_need_snapshot_init(&foreign_world);
                assert(prime_need_snapshot_begin(&foreign_world));
                uint64_t foreign_id=0u;
                assert(prime_need_snapshot_allocate(&arena,&foreign_world,
                    atom_symbol(&arena,"ForeignSource"),&foreign_world,
                    &foreign_id));
                uint64_t foreign_evaluator=prime_need_fresh_evaluator_id();
                PrimeNeedSnapshot foreign_running,foreign_ready;
                assert(prime_need_snapshot_start_evaluation(&arena,
                    &foreign_world,foreign_id,foreign_evaluator,
                    &foreign_running));
                assert(prime_need_snapshot_resolve_ready_value(&arena,
                    &foreign_running,foreign_id,foreign_evaluator,
                    atom_int(&arena,53),&foreign_ready));
                ready_ref=prime_need_ref(&arena,&foreign_ready,foreign_id);
            }
            assert(ready_ref);
        }
        call=(which==8u || which==9u)
            ? atom_expr3(&arena,atom_symbol(&arena,"nc:discr"),
                which==8u?atom_int(&arena,7):ready_ref,stored)
            : atom_expr3(&arena,atom_symbol(&arena,"nc:add"),
                ready_ref,atom_int(&arena,6));
    }
    cetta_runtime_stats_enable();cetta_runtime_stats_reset();
    const PrimeNeedFrame *world_before=bindings_need_view(&environment)->top;
    bool admitted=prime_capsule_search_schedule(&space,&arena,call,
        atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
        &environment,true,&out);
    if(which==8u || which==9u || which==12u || which==13u ||
       which==14u || which==15u) {
        assert(!admitted && !driver.top && !driver.capacity_blocked);
        assert(bindings_need_view(&environment)->top==world_before);
        assert(numeric_capsule_counter("prepared-capsule-step")==0u);
        assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
        printf("NumericCapsule %u passed\n",which);
        prime_eval_stack_abandon(&driver);
        prepared_pure_program_cache_free(&driver.prepared_pure_cache);
        g_prime_eval_stack_driver=prior_driver;g_prime_need_owner=prior_need;
        g_prime_need_episode_owner=prior_episode;
        outcome_set_free(&out);bindings_free(&environment);
        g_registry=prior_registry;registry_free(&registry);
        space_free(&space);arena_free(&foreign);arena_free(&arena);
        eval_set_library_context(NULL);cetta_library_context_free(&context);
        cetta_runtime_stats_disable();
        return;
    }
    assert(admitted);
    PrimeEvalStackFrame *frame=driver.top;assert(frame && frame->kind==PRIME_EVAL_STACK_FRAME_CAPSULE_SEARCH);
    PrimeCapsuleSearch *search=frame->capsule_search;assert(prime_capsule_search_current(search));
    capsule_storage_case(search);
    assert(numeric_capsule_counter("prepared-capsule-step")==0u);
    assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
    assert(!search->returns.has_pending && !frame->child.len && !search->transitions && !search->resumes);
    if(which==20u) {
        /* Exercise the actual retained service callback, including its
         * current-source guard and refusal before any source step. */
        PettaMachineOperationView constructor={.expression=br_parse(&arena,"(Box 7)")};
        PettaMachineOperationView equation={.expression=call};
        PettaMachineOperationView coordinated={.expression=br_parse(&arena,
            "(select 1 (search-policy (control FIFO)) (nc:add 4 6))")};
        PettaMachineOperationView unresolved={.expression=br_parse(&arena,"((Box 7))")};
        CHECK(prime_capsule_service_permit(search,&space,PETTA_MACHINE_OPERATION_LOCAL,NULL));
        CHECK(prime_capsule_service_permit(search,&space,PETTA_MACHINE_OPERATION_CALL,&constructor));
        CHECK(prime_capsule_service_permit(search,&space,PETTA_MACHINE_OPERATION_CALL,&equation));
        CHECK(prime_capsule_service_permit(search,&space,PETTA_MACHINE_OPERATION_CALL,&coordinated));
        Space other;space_init(&other);
        CHECK(!prime_capsule_service_permit(search,&other,PETTA_MACHINE_OPERATION_LOCAL,NULL));
        space_free(&other);
        CHECK(!driver.capacity_blocked && prime_capsule_search_current(search));
        CHECK(!prime_capsule_service_permit(search,&space,PETTA_MACHINE_OPERATION_CALL,&unresolved));
        CHECK(driver.capacity_blocked && search->returns.completion==CETTA_EVAL_INCOMPLETE_CAPACITY);
        CHECK(!search->returns.has_pending && !frame->child.len &&
            !search->transitions && !search->resumes && driver.top==frame);
        CHECK(numeric_capsule_counter("prepared-capsule-step")==0u &&
            numeric_capsule_counter("prepared-capsule-publication")==0u);
        goto complete;
    }
    if(which==16u || which==17u) {
        PreparedPureProgramCache *cache=search->certificate_cache;
        CettaPreparedPureProgram *first=search->certificate;
        numeric_capsule_expect_value(&driver,frame,10);
        prime_eval_stack_pop();
        assert(!driver.top && cache->entries && cache->entries->program==first);
        assert(numeric_capsule_counter("prepared-pure-call-program-cache-store")==1u);
        assert(prime_capsule_search_schedule(&space,&arena,call,
            atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
            &environment,true,&out));
        frame=driver.top;search=frame->capsule_search;
        assert(search->certificate==first && !cache->entries);
        prime_capsule_search_resume(frame);
        assert(search->last_step==CETTA_PREPARED_CLOSED_SUSPENDED &&
            search->transitions==1u && !frame->child.len);
        if(which==16u) {
            /* A live invocation holds its graph out of the pool. A second
             * invocation needs a separate graph, then a third can reuse the
             * second's closed graph while the first remains suspended. */
            Atom *second_call=br_parse(&arena,"(nc:add 20 30)");
            assert(prime_capsule_search_schedule(&space,&arena,second_call,
                atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
                &environment,true,&out));
            PrimeEvalStackFrame *second=driver.top;
            CettaPreparedPureProgram *other=second->capsule_search->certificate;
            assert(other!=first && !cache->entries);
            numeric_capsule_expect_value(&driver,second,50);
            prime_eval_stack_pop();
            assert(driver.top==frame && cache->entries &&
                cache->entries->program==other);
            assert(prime_capsule_search_schedule(&space,&arena,
                br_parse(&arena,"(nc:add 40 60)"),
                atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
                &environment,true,&out));
            PrimeEvalStackFrame *third=driver.top;
            assert(third->capsule_search->certificate==other && !cache->entries);
            numeric_capsule_expect_value(&driver,third,100);
            prime_eval_stack_pop();
            assert(driver.top==frame && prime_capsule_search_current(search));
            numeric_capsule_expect_value(&driver,frame,10);
            prime_eval_stack_pop();
            assert(!driver.top && numeric_capsule_counter(
                "prepared-pure-call-program-cache-hit")==2u);
            assert(numeric_capsule_counter(
                "prepared-pure-call-program-cache-store")==2u);
        } else {
            /* A changed equation invalidates a graph even while it is held
             * exclusively. Abandonment cannot put that old graph back. */
            assert(space_remove(&space,add_equation));
            space_add(&space,br_parse(&arena,
                "(= (nc:add $x $y) (* $x $y))"));
            assert(!prime_capsule_search_current(search));
            prime_eval_stack_abandon(&driver);
            assert(!driver.top && !cache->entries && !out.len);
            assert(numeric_capsule_counter(
                "prepared-pure-call-program-cache-store")==1u);
            assert(prime_capsule_search_schedule(&space,&arena,call,
                atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
                &environment,true,&out));
            frame=driver.top;
            numeric_capsule_expect_value(&driver,frame,24);
            prime_eval_stack_pop();
            assert(cache->entries && numeric_capsule_counter(
                "prepared-pure-call-compile-attempt")==2u);
            assert(numeric_capsule_counter(
                "prepared-pure-call-program-cache-store")==2u);
        }
        assert(!out.len && !driver.prepared_opaque_frames);
        goto complete;
    }
    if(which==0u || which==6u) {
        if(which==6u)assert(prime_capsule_search_publish(frame));
        assert(!search->returns.has_pending && !frame->child.len && !search->transitions && !search->resumes);
    } else if(which==4u) {
        space_add(&space,br_parse(&arena,"(= (nc:new) 42)"));
        assert(!prime_capsule_search_current(search));
        prime_capsule_search_resume(frame);
        assert(driver.capacity_blocked && search->serial_invalidated);
        assert(!search->returns.has_pending && !frame->child.len && !search->transitions && !search->resumes);
    } else {
        if(which==2u) {
            search->context.controller_quantum_limit=0u;
            prime_capsule_search_resume(frame);
            assert(search->resumes==1u && search->transitions==0u && !frame->child.len);
            assert(numeric_capsule_counter("prepared-capsule-step")==0u);
            assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
            search->context.controller_quantum_limit=1u;
        }
        if(which==3u || which>=18u)frame->child.payload_owner=&foreign;
        if(which==5u)cetta_runtime_stats_disable();
        for(unsigned n=0u;n<100u && !frame->child.len && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        assert(search->transitions>0u);
        if(which==3u || which>=18u) {
            assert(driver.capacity_blocked && search->returns.has_pending && search->pending_projection.len==1u);
            assert(frame->child.len==0u);
            assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
            uint64_t spent=search->transitions,resumes=search->resumes;
            Atom *answer=search->returns.pending.outcome.term;
            assert(answer && answer->kind==ATOM_GROUNDED && answer->ground.gkind==GV_INT && answer->ground.ival==10);
            assert(numeric_capsule_counter("prepared-capsule-step")==spent);
            assert(search->returns.computation==search && search->returns.accepted==0u &&
                search->returns.pending.final && !search->returns.terminal &&
                search->returns.pending.outcome.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE &&
                search->returns.pending.environment==&search->execution_environment);
            assert(cetta_owned_execution_sequence(&search->returns,0u,
                prime_capsule_search_consume,search)==CETTA_OWNED_EXECUTION_PAUSED);
            for(unsigned retry=0u;retry<3u;retry++) {
                /* Re-enter the admitted driver scope, keeping the same
                 * conflicting receiver and the complete retained loan. */
                driver.capacity_blocked=false;
                assert(!prime_capsule_search_publish(frame));
            }
            assert(search->returns.has_pending && search->returns.accepted==0u &&
                search->returns.pending.outcome.term==answer &&
                search->transitions==spent && search->resumes==resumes);
            if(which==18u) {
                driver.capacity_blocked=false;
                space_add(&space,br_parse(&arena,"(= (nc:after-loan) 42)"));
                assert(!prime_capsule_search_current(search));
                assert(!prime_capsule_search_publish(frame));
                assert(search->serial_invalidated && search->returns.has_pending &&
                    search->returns.pending.outcome.term==answer &&
                    search->returns.accepted==0u && !frame->child.len &&
                    search->transitions==spent && search->resumes==resumes);
                goto complete;
            }
            if(which==19u) {
                PreparedPureProgramCache *cache=search->certificate_cache;
                CettaPreparedPureProgram *program=search->certificate;
                prime_eval_stack_abandon(&driver);
                assert(!driver.top && !driver.prepared_opaque_frames && !out.len &&
                    cache->entries && cache->entries->program==program);
                driver.capacity_blocked=false;
                assert(prime_capsule_search_schedule(&space,&arena,
                    br_parse(&arena,"(nc:add 20 30)"),
                    atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
                    &environment,true,&out));
                frame=driver.top;search=frame->capsule_search;
                CettaPreparedClosedState fresh;
                assert(search->certificate==program && search->returns.computation==search &&
                    !search->returns.has_pending && !search->returns.accepted &&
                    cetta_prepared_pure_closed_read(search->execution,&fresh) &&
                    fresh.status==CETTA_PREPARED_CLOSED_SUSPENDED && !fresh.performed);
                numeric_capsule_expect_value(&driver,frame,50);
                assert(search->returns.accepted==1u && search->returns.terminal);
                goto complete;
            }
            /* Restore the deliberately conflicting receiver witness. This is
             * the internal checked-append retry, not public serial recovery. */
            frame->child.payload_owner=&arena;driver.capacity_blocked=false;
            assert(prime_capsule_search_current(search));
            prime_capsule_search_resume(frame);
            assert(search->transitions==spent && search->resumes==resumes);
            assert(!search->returns.has_pending && !search->pending_projection.len && search->returns.pending.outcome.term==NULL);
            assert(search->returns.accepted==1u && search->returns.terminal &&
                search->returns.stop==CETTA_OWNED_EXECUTION_COMPLETE);
            assert(cetta_owned_execution_sequence(&search->returns,1u,
                prime_capsule_search_consume,search)==CETTA_OWNED_EXECUTION_COMPLETE);
            assert(search->transitions==spent && search->resumes==resumes &&
                search->returns.accepted==1u);
        }
        assert(frame->child.len==1u && !search->returns.has_pending);
        Atom *value=outcome_atom_materialize(&arena,&frame->child.items[0]);
        assert(value && value->kind==ATOM_GROUNDED);
        if(which==7u)assert(value->ground.gkind==GV_FLOAT && isnan(value->ground.fval));
        else assert(value->ground.gkind==GV_INT && value->ground.ival==10);
        if(which==5u) {
            assert(numeric_capsule_counter("prepared-capsule-step")==0u);
            assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
            cetta_runtime_stats_enable();
        } else {
            assert(numeric_capsule_counter("prepared-capsule-step")==search->transitions);
            assert(numeric_capsule_counter("prepared-capsule-publication")==1u);
        }
        uint64_t spent=search->transitions,resumes=search->resumes;
        assert(prime_capsule_search_publish(frame));
        assert(search->transitions==spent && search->resumes==resumes && frame->child.len==1u);
        assert(numeric_capsule_counter("prepared-capsule-publication")== (which==5u?0u:1u));
    }
    if(which==0u || which==4u || which==6u) {
        assert(numeric_capsule_counter("prepared-capsule-step")==0u);
        assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
    }
complete:
    if (driver.top && driver.top->capsule_search)
        capsule_storage_case(driver.top->capsule_search);
    printf("NumericCapsule %u passed\n",which);
    prime_eval_stack_abandon(&driver);assert(driver.prepared_opaque_frames==0u);
    prepared_pure_program_cache_free(&driver.prepared_pure_cache);
    g_prime_eval_stack_driver=prior_driver;g_prime_need_owner=prior_need;g_prime_need_episode_owner=prior_episode;
    outcome_set_free(&out);bindings_free(&environment);g_registry=prior_registry;registry_free(&registry);
    space_free(&space);arena_free(&foreign);arena_free(&arena);
    eval_set_library_context(NULL);cetta_library_context_free(&context);cetta_runtime_stats_disable();
}
#endif

static void cache_return_caller_case(unsigned which) {
    assert(which < 12u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);assert(eval_current_language_id()==CETTA_LANGUAGE_PRIME);
    Arena arena;arena_init(&arena);Space space;space_init(&space);
    Registry registry;registry_init(&registry);Registry *saved_registry=g_registry;g_registry=&registry;
    Arena *saved_owner=g_prime_need_owner,*saved_episode=g_prime_need_episode_owner;
    g_prime_need_owner=g_prime_need_episode_owner=&arena;
    Bindings caller;bindings_init(&caller);
    Atom *x=atom_var_with_id(&arena,"cache:caller",fresh_var_id());
    Atom *alias=atom_var_with_id(&arena,"cache:alias",fresh_var_id());
    Atom *captured=atom_var_with_id(&arena,"cache:captured",fresh_var_id());
    Atom *wanted=which==1u?atom_int(&arena,37):br_parse(&arena,"(Pair 19 23)");
    assert(bindings_add_var(&caller,x,wanted));
    assert(bindings_add_var(&caller,alias,x));
    assert(bindings_add_var(&caller,captured,atom_int(&arena,5)));
    if(which==3u)assert(bindings_add_constraint_owned(&caller,
        atom_var_with_id(&arena,"cache:left",fresh_var_id()),
        atom_var_with_id(&arena,"cache:right",fresh_var_id())));
    PrimeNeedSnapshot base;prime_need_snapshot_init(&base);assert(prime_need_snapshot_begin(&base));
    uint64_t child_id=0u;
    assert(prime_need_snapshot_allocate(&arena,&base,br_parse(&arena,"(println! CacheChildForbidden)"),&base,&child_id));
    Atom *child=prime_need_ref(&arena,&base,child_id);
    Atom *value=which==1u?atom_int(&arena,7):br_parse(&arena,"(Pair 1 2)");
    if(which==2u)value=atom_expr3(&arena,atom_symbol(&arena,"lam"),atom_symbol(&arena,"cr:v"),
        atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_symbol(&arena,"cr:v"),captured));
    if(which==4u)value=br_parse(&arena,"(+ 1 2)");
    if(which==5u)value=atom_prime_held_wrap(&arena,br_parse(&arena,"(+ 1 2)"));
    if(which==6u)value=atom_expr2(&arena,atom_symbol_id(&arena,prime_need_symbols()->promise),child);
    if(which==10u)value=atom_error(&arena,atom_symbol(&arena,"cached"),atom_symbol(&arena,"fault"));
    Atom *origin=which==2u?value:br_parse(&arena,"(chain (println! CacheProducerForbidden) $unused 7)");
    if(which==11u)origin=br_parse(&arena,"(chain (println! CacheProducerOnce) $unused 7)");
    uint64_t id=0u,evaluator=prime_need_fresh_evaluator_id();
    PrimeNeedSnapshot pending,running,ready;
    assert(prime_need_snapshot_allocate(&arena,&base,origin,&pending,&id));
    assert(prime_need_snapshot_start_evaluation(&arena,&pending,id,evaluator,&running));
    assert(which==10u?prime_need_snapshot_resolve_stable_fault(&arena,&running,id,evaluator,value,&ready):
        prime_need_snapshot_resolve_ready_value(&arena,&running,id,evaluator,value,&ready));
    Atom *reference=prime_need_ref(&arena,&ready,id);
    *bindings_need_mut(&caller)=which==11u?pending:ready;
    if(which==7u)reference=prime_need_ref_restrict(&arena,reference,&ready,CETTA_PRIME_NEED_RIGHT_INSPECT);
    if(which==8u) {
        PrimeNeedSnapshot foreign;prime_need_snapshot_init(&foreign);assert(prime_need_snapshot_begin(&foreign));
        uint64_t foreign_id=0u;assert(prime_need_snapshot_allocate(&arena,&foreign,origin,&foreign,&foreign_id));
        reference=prime_need_ref(&arena,&foreign,foreign_id);
    }
    OutcomeSet rows;outcome_set_init(&rows);
    unsigned rounds=which==9u || which==11u?2u:1u;
    for(unsigned round=0u;round<rounds;round++) {
        if(which==9u) {
            assert(prime_need_snapshot_resolve_ready_value(&arena,&running,id,evaluator,atom_int(&arena,round?47:31),&ready));
            *bindings_need_mut(&caller)=ready;
            Atom *projected=prime_need_project_ready_call(
                &space,&arena,reference,&caller);
            assert(projected && projected->kind==ATOM_GROUNDED &&
                projected->ground.gkind==GV_INT &&
                projected->ground.ival==(round?47:31));
        }
        PrimeNeedActiveGuard active;
        prime_need_active_enter(&active, &caller);
        CettaCount first=rows.len;
        CettaNativeCostLedger cost;cetta_native_cost_init(&cost);
        CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&cost,1u);
        if(which==8u)metta_eval_bind_typed_in_env(&space,&arena,NULL,reference,-1,&caller,&rows);
        else prime_need_force_active(&space,&arena,reference,id,-1,&rows);
        cetta_native_cost_scope_leave(&scope);
        if(which==7u)assert(cost.total.counts[CETTA_COST_CAPTURE]==0u);
        else if(which!=8u && (which!=11u || round))
            assert(cost.total.counts[CETTA_COST_CAPTURE]==1u);
        cetta_native_cost_free(&cost);
        prime_need_active_leave(&active);
        assert(rows.len==first+1u);
        Atom *answer=outcome_atom_materialize(&arena,&rows.items[first]);assert(answer);
        if(which==7u)assert(atom_is_error(answer) && atom_is_symbol(answer->expr.elems[2],"suspension:force-denied"));
        else if(which==8u)assert(atom_eq(answer,reference));
        else {
            assert(atom_eq(bindings_apply_if_vars(&rows.items[first].env,&arena,x),wanted));
            assert(atom_eq(bindings_apply_if_vars(&rows.items[first].env,&arena,alias),wanted));
            assert(rows.items[first].env.eq_len==caller.eq_len);
            Atom *expected=which==2u?bindings_apply_if_vars(&caller,&arena,value):
                which==9u?atom_int(&arena,round?47:31):which==11u?atom_int(&arena,7):value;
            assert(atom_eq(answer,expected));
            if(which!=10u)assert(rows.items[first].result_form==CETTA_CALL_RESULT_COMPLETED_VALUE);
            PrimeNeedCellView observed,child_view;
            assert(prime_need_snapshot_lookup(bindings_need_view(&rows.items[first].env),id,&observed));
            assert(prime_need_snapshot_lookup(bindings_need_view(&rows.items[first].env),child_id,&child_view));
            assert(child_view.cache_state==PRIME_NEED_CACHE_EMPTY);
            assert(observed.origin==origin);
            if(which!=11u)assert(bindings_need_view(&rows.items[first].env)->top==ready.top);
            if(which==9u)assert(observed.cached->ground.ival==(round?47:31));
            if(which==11u && !round) {
                bindings_free(&caller);assert(bindings_clone(&caller,&rows.items[first].env));
            }
        }
    }
    PrimeNeedCellView unchanged;
    assert(prime_need_snapshot_lookup(&pending,id,&unchanged) && unchanged.cache_state==PRIME_NEED_CACHE_EMPTY);
    printf("CacheReturnCaller %u passed\n",which);
    outcome_set_free(&rows);bindings_free(&caller);
    g_prime_need_owner=saved_owner;g_prime_need_episode_owner=saved_episode;g_registry=saved_registry;
    registry_free(&registry);space_free(&space);arena_free(&arena);eval_set_library_context(NULL);cetta_library_context_free(&context);
}

static void terminal_open_reply_case(unsigned which) {
    assert(which < 11u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    assert(eval_current_language_id()==CETTA_LANGUAGE_PRIME);
    Arena arena,foreign;arena_init(&arena);arena_init(&foreign);
    Space space;space_init(&space);Registry registry;registry_init(&registry);
    Registry *prior_registry=g_registry;g_registry=&registry;
    Arena *prior_need=g_prime_need_owner,*prior_episode=g_prime_need_episode_owner;
    g_prime_need_owner=g_prime_need_episode_owner=&arena;
    space_add(&space,br_parse(&arena,"(= (to:leaf) (Node $local $local))"));
    space_add(&space,br_parse(&arena,which==1u
        ? "(= (to:root) (collapse (to:leaf)))"
        : which==10u ? "(= (to:root $n) (Box $n (to:leaf)))"
        : "(= (to:root) (Box (to:leaf)))"));
    OutcomeSet out;outcome_set_init_with_owner(&out,&arena);
    Bindings environment;bindings_init(&environment);
    Atom *caller=atom_var_with_id(&arena,"to:caller",fresh_var_id());
    assert(bindings_add_var(&environment,caller,br_parse(&arena,"(Pair 19 23)")));
    PrimeEvalStackDriver driver={.arena=&arena,.root_target=&out,.raw_call_depth=1u,
        .prepared_pure_cache={.root_space=&space}};
    PrimeEvalStackDriver *prior_driver=g_prime_eval_stack_driver;g_prime_eval_stack_driver=&driver;
    Atom *call=br_parse(&arena,which==10u?"(to:root 17)":"(to:root)");
    assert(prime_capsule_search_schedule(&space,&arena,call,atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,&environment,true,&out));
    PrimeEvalStackFrame *frame=driver.top;assert(frame && frame->kind==PRIME_EVAL_STACK_FRAME_CAPSULE_SEARCH);
    PrimeCapsuleSearch *search=frame->capsule_search;
    for(unsigned n=0u;n<100u && search->last_step!=CETTA_PREPARED_CLOSED_SERVICE && !driver.capacity_blocked;n++)
        prime_capsule_search_resume(frame);
    assert(!driver.capacity_blocked && search->last_step==CETTA_PREPARED_CLOSED_SERVICE);
    assert(prime_capsule_service_prepare(search));
    PrimeCapsuleService *service=search->service;assert(service && service->request.normalize_result==(which!=1u));
    capsule_storage_case(search);
    CettaPreparedClosedState state;
    assert(cetta_prepared_pure_closed_read(search->execution,&state));
    if(which==1u)assert(state.frames>0u);
    else assert(state.frames==0u && state.values==1u);
    for(unsigned n=0u;n<500u && !service->pending && !driver.capacity_blocked;n++)
        prime_capsule_source_resume(search);
    assert(!driver.capacity_blocked && service->pending && service->answer && atom_has_vars(service->answer));
    capsule_storage_case(search);
    if(which==10u) {
        /* Closing a pending terminal request discards its paid reply and
         * service cursor, while retaining only the reusable compiled graph. */
        Atom *abandoned=service->answer;
        assert(abandoned->kind==ATOM_EXPR && abandoned->expr.len==3u &&
            atom_eq(abandoned->expr.elems[1],atom_int(&arena,17)));
        Atom *old_node=abandoned->expr.elems[2];
        assert(old_node->kind==ATOM_EXPR && old_node->expr.len==3u &&
            atom_eq(old_node->expr.elems[1],old_node->expr.elems[2]));
        uint64_t first_ordinal=service->request.ordinal;
        PreparedPureProgramCache *cache=search->certificate_cache;
        CettaPreparedPureProgram *program=search->certificate;
        assert(service->accepted==0u && service->source_cursor==1u &&
            !service->has_reply && !frame->child.len && !out.len);
        prime_eval_stack_pop();
        assert(!driver.top && !driver.prepared_opaque_frames &&
            cache->entries && cache->entries->program==program && !out.len);
        assert(prime_capsule_search_schedule(&space,&arena,
            br_parse(&arena,"(to:root 29)"),
            atom_symbol_id(&arena,g_builtin_syms.undefined_type),-1,
            &environment,true,&out));
        frame=driver.top;search=frame->capsule_search;
        assert(search->certificate==program && !search->service &&
            !search->returns.has_pending && !search->transitions && !search->resumes);
        assert(cetta_prepared_pure_closed_read(search->execution,&state) &&
            state.performed==0u && state.status==CETTA_PREPARED_CLOSED_SUSPENDED);
        for(unsigned n=0u;n<100u && search->last_step!=CETTA_PREPARED_CLOSED_SERVICE && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        assert(!driver.capacity_blocked && search->last_step==CETTA_PREPARED_CLOSED_SERVICE);
        assert(prime_capsule_service_prepare(search));
        service=search->service;
        assert(service && service->request.normalize_result &&
            service->request.ordinal==first_ordinal && service->accepted==0u &&
            service->source_cursor==0u && !service->pending && !service->has_reply);
        for(unsigned n=0u;n<500u && !service->pending && !driver.capacity_blocked;n++)
            prime_capsule_source_resume(search);
        assert(!driver.capacity_blocked && service->pending && service->answer);
        Atom *fresh=service->answer;
        assert(fresh->kind==ATOM_EXPR && fresh->expr.len==3u &&
            atom_eq(fresh->expr.elems[1],atom_int(&arena,29)));
        Atom *node=fresh->expr.elems[2];
        assert(node->kind==ATOM_EXPR && node->expr.len==3u &&
            node->expr.elems[1]->kind==ATOM_VAR &&
            atom_eq(node->expr.elems[1],node->expr.elems[2]) &&
            !atom_eq(node->expr.elems[1],old_node->expr.elems[1]));
        prime_capsule_service_resume(search);
        assert(!driver.capacity_blocked && service->accepted==1u &&
            service->has_reply && !service->pending);
        for(unsigned n=0u;n<10u && !frame->child.len && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        assert(!driver.capacity_blocked && frame->child.len==1u && !out.len &&
            atom_eq(outcome_atom_materialize(&arena,&frame->child.items[0]),fresh));
        uint64_t published=search->transitions;
        assert(prime_capsule_search_publish(frame) && frame->child.len==1u &&
            search->transitions==published);
        goto complete;
    }
    Atom *answer=service->answer;
    assert(answer->kind==ATOM_EXPR && answer->expr.len==(which==1u?1u:2u));
    Atom *node=answer->expr.elems[which==1u?0u:1u];assert(node->kind==ATOM_EXPR && node->expr.len==3u);
    assert(node->expr.elems[1]->kind==ATOM_VAR && atom_eq(node->expr.elems[1],node->expr.elems[2]));
    uint64_t resumes=search->service_resumes,terminal=search->service_terminal_resumes;
    uint64_t dispatches=search->service_terminal_dispatches,spent=search->transitions;
    assert(service->accepted==0u && service->source_cursor==1u);
    uint64_t cell_id=0u;PrimeNeedSnapshot cell_world;prime_need_snapshot_init(&cell_world);
    if(which==4u || which==5u) {
        assert(prime_need_snapshot_begin(&cell_world));
        assert(prime_need_snapshot_allocate(which==4u?&foreign:&arena,&cell_world,
            br_parse(&arena,"(println! TerminalForbidden)"),&cell_world,&cell_id));
        Atom *ref=prime_need_ref(which==4u?&foreign:&arena,&cell_world,cell_id);
        service->answer=atom_expr3(&arena,atom_symbol(&arena,"Opaque"),node->expr.elems[1],ref);
        if(which==5u)*bindings_need_mut(&service->environment)=cell_world;
    }
    if(which==2u)assert(!cetta_prepared_pure_closed_service_reply(search->execution,&arena,search,
        service->cut,service->request.ordinal,service->accepted+1u,service->answer));
    if(which==3u) {
        assert(!cetta_prepared_pure_closed_service_reply(search->execution,&arena,search,
            service->cut,service->request.ordinal+1u,service->accepted,service->answer));
        assert(!cetta_prepared_pure_closed_service_reply(search->execution,&arena,&foreign,
            service->cut,service->request.ordinal,service->accepted,service->answer));
    }
    if(which==6u)space_add(&space,br_parse(&arena,"(= (to:changed) 7)"));
    if(which==7u)frame->child.payload_owner=&foreign;
    if(which==8u) {
        Atom *leaf=node->expr.elems[1];
        assert(bindings_add_var(&service->environment,leaf,atom_int(&arena,37)));
    }
    if(which==9u)service->answer=atom_expr2(&arena,atom_symbol_id(&arena,g_builtin_syms.quote),
        atom_expr3(&arena,atom_symbol(&arena,"+"),node->expr.elems[1],atom_int(&arena,1)));
    prime_capsule_service_resume(search);
    bool refused=which==1u || which==4u || which==6u;
    assert(driver.capacity_blocked==refused);
    assert(search->service_resumes==resumes && search->service_terminal_resumes==terminal &&
        search->service_terminal_dispatches==dispatches && search->transitions==spent);
    if(refused) {
        assert(service->pending && !service->has_reply && frame->child.len==0u && !search->returns.has_pending);
    } else {
        assert(!service->pending && service->has_reply && service->accepted==1u);
        for(unsigned n=0u;n<10u && !frame->child.len && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        if(which==7u) {
            assert(driver.capacity_blocked && search->returns.has_pending && search->pending_projection.len==1u);
            uint64_t published_spent=search->transitions,published_resumes=search->resumes;
            Atom *retained=search->returns.pending.outcome.term;assert(retained==answer);
            frame->child.payload_owner=&arena;driver.capacity_blocked=false;
            assert(prime_capsule_search_publish(frame));
            assert(search->transitions==published_spent && search->resumes==published_resumes);
        }
        assert(!driver.capacity_blocked && frame->child.len==1u);
        Outcome *row=&frame->child.items[0];Atom *value=outcome_atom_materialize(&arena,row);
        assert(atom_eq(bindings_apply_if_vars(&row->env,&arena,caller),br_parse(&arena,"(Pair 19 23)")));
        if(which==8u)assert(atom_eq(bindings_apply_if_vars(&row->env,&arena,value),br_parse(&arena,"(Box (Node 37 37))")));
        else assert(atom_eq(value,service->answer));
        if(which==5u) {
            PrimeNeedCellView view;assert(prime_need_snapshot_lookup(bindings_need_view(&row->env),cell_id,&view));
            assert(view.cache_state==PRIME_NEED_CACHE_EMPTY);
        }
        assert(prime_capsule_search_publish(frame) && frame->child.len==1u);
        assert(search->service_resumes==resumes && search->service_terminal_resumes==terminal &&
            search->service_terminal_dispatches==dispatches);
    }
complete:
    printf("TerminalOpenReply %u passed\n",which);
    prime_eval_stack_abandon(&driver);assert(driver.prepared_opaque_frames==0u);
    prepared_pure_program_cache_free(&driver.prepared_pure_cache);
    g_prime_eval_stack_driver=prior_driver;g_prime_need_owner=prior_need;g_prime_need_episode_owner=prior_episode;
    outcome_set_free(&out);bindings_free(&environment);g_registry=prior_registry;registry_free(&registry);
    space_free(&space);arena_free(&foreign);arena_free(&arena);
    eval_set_library_context(NULL);cetta_library_context_free(&context);
}

static bool guest_idle_control_released, guest_idle_service_released;
static unsigned guest_idle_token_checks;
static void guest_idle_release_control(void *raw) {
    PrimeNativeControl *control = raw;
    assert(!guest_idle_service_released);
    assert(prime_native_control_query_reads_current(control));
    release_stack_control(control);
    guest_idle_control_released = true;
}
static void guest_idle_release_service(void *raw) {
    PrimeHolGuest *guest = raw;
    assert(guest_idle_control_released);
    space_free(&guest->space);
    guest_idle_service_released = true;
}
static bool guest_idle_check_handle(CettaLibraryContext *context, Atom *handle) {
    PrimeNativeControl *control = cetta_native_handle_get_owned(context, "guest-control", handle);
    if (!control) return false;
    guest_idle_token_checks++;
    return prime_native_control_query_reads_current(control);
}
static void guest_read_service_case(unsigned which) {
    assert(which < 8u);
    assert(setenv("CETTA_PRIME_HE_TYPED_APPLICABILITY", which==5u ? "0" : "1",1)==0);
    assert(setenv("CETTA_NIK_TYPED_APPLICABILITY","1",1)==0);
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    Space source, foreign;
    space_init(&source);
    space_init(&foreign);
    Registry registry;
    registry_init(&registry);
    g_registry = &registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_default_fuel(-1);
    eval_set_library_context(&context);
    g_prime_need_owner = g_prime_need_episode_owner = &arena;
    space_add(&source, parse_one(&arena, "(= (read-service) (superpose (1 2)))"));
    space_add(&source, parse_one(&arena,"(= (guest:poly $x) $x)"));
    if(which!=0u && which!=4u)
        space_add(&source,parse_one(&arena,"(: guest:poly (-> Number Number))"));
    if(which==1u || which==2u || which==5u)
        space_add(&source,parse_one(&arena,which==2u?
            "(: guest:poly Atom)":"(: guest:poly (-> Bool Bool))"));
    assert(petta_program_synchronize_space(context.petta_program, &source));
    assert(petta_program_classify_resolved_call(context.petta_program, &source,
        parse_one(&arena, "(guest:poly 7)")) == PETTA_RESOLVED_CALL_RELATION);
    assert(prime_ordinary_shared_source_admitted(&source));
    PrimeNativeControl parent;
    CettaSearchPolicySpec policy = {.native_control=true, .all=true,
        .work_budget=UINT64_MAX, .workers=2u};
    assert(prime_native_control_init(&parent, &source, &arena,
        parse_one(&arena, "(read-service)"), -1, true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, NULL));
    if (which == 7u) {
        PrimeHolGuest guest = {0};
        space_init(&guest.space);
        guest.context_ready = true;
        context.prime_hol_guest = &guest;
        context.prime_hol_guest_free = guest_idle_release_service;
        assert(prime_native_control_record_query_read(&parent, space_read_token(&guest.space)));
        uint64_t id = 0u;
        assert(cetta_native_handle_alloc(&context, "guest-control", &parent,
            guest_idle_release_control, &id));
        Atom *holder = cetta_native_handle_owned_atom(&context, &arena, "guest-control", id);
        Atom *textual = cetta_native_handle_atom(&arena, "guest-control", id);
        CettaLibraryContext other;
        cetta_library_context_init_for_language_profile(&other, CETTA_LANGUAGE_PRIME,
            cetta_profile_prime_default());
        assert(guest_idle_check_handle(&context, holder));
        assert(!guest_idle_check_handle(&context, textual));
        assert(!guest_idle_check_handle(&other, holder));
        assert(guest_idle_token_checks == 1u);
        cetta_library_context_free(&context);
        assert(guest_idle_control_released && guest_idle_service_released);
        assert(!guest_idle_check_handle(&context, holder));
        assert(guest_idle_token_checks == 1u);
        cetta_library_context_free(&other);
        eval_set_library_context(NULL);
        puts("Idle retained control retires before guest; wrong and stale handles refuse before token read");
        goto finish_resources;
    }
    parent.driver.ordinary_ordered = true;
    parent.driver.ordinary_shared_source = true;
    PrimeNativeWork *work = cetta_continuation_hub_at(&parent.hub, 0u)->payload;
    PrimeNativeRunGuard guard;
    prime_native_run_enter(&parent, &guard);
    assert(prime_native_parallel_prepare(&parent, work));
    prime_native_run_leave(&guard);
    PrimeNativeParallel *parallel = work->parallel;
    PrimeNativeControl *child = parallel->child;
    assert(parallel->borrowed_source.root.space == &source);
    assert(child->driver.ordinary_ordered && !child->driver.capsule_stack);
    Arena *owned_arena = child->driver.arena;
    Space *owned_space = parallel->world.space;
    Atom *call = parse_one(owned_arena, "(superpose (1 2))");
    Atom *mutation = parse_one(owned_arena, "(add-atom &self read-service-forbidden)");
    Registry *saved_registry = g_registry;
    g_registry = &parallel->world.registry;
    PrimeNativeParallel *saved_parallel = g_prime_native_parallel_current;
    g_prime_native_parallel_current = parallel;
    prime_native_run_enter(child, &guard);
    cetta_shared_transition_scope_enter();
    CettaSharedTransitionGuard transition = {0};
    cetta_shared_transition_guard_enter(&transition);
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    Atom *typed_call=parse_one(owned_arena,which==4u?"($unknown 7)":"(guest:poly 7)");
    bool requires_guest=which==1u || which==2u;
    if (requires_guest) {
        PettaCandidateSnapshotLease erased = {0};
        assert(petta_program_candidate_snapshot_lease_profiled(context.petta_program,
            owned_space, atom_head_symbol_id(typed_call), &erased, NULL));
        assert(erased.len == 1u && !erased.items[0].rhs_plan);
        petta_program_candidate_snapshot_lease_release(&erased);
    }
    bool guest_admitted = prime_native_parallel_read_service_admitted(child,owned_space,
        owned_arena,typed_call,&child->output);
    if (guest_admitted != requires_guest)
        fprintf(stderr,"Guest service case %u admitted=%u expected=%u\n",
            which, guest_admitted, requires_guest);
    assert(guest_admitted == requires_guest);
    assert(context.prime_hol_guest==NULL);
    PrimeHolGuest guest = {0};
    if (which == 6u) {
        space_init(&guest.space);
        guest.context_ready = true;
        context.prime_hol_guest = &guest;
        SpaceReadToken read = space_read_token(&guest.space);
        assert(prime_native_parallel_borrowed_query_current(parallel, read));
        assert(!prime_native_parallel_borrowed_query_current(parallel,
            space_read_token(&foreign)));
        CettaLibraryContext *saved_library = parallel->borrowed_library;
        parallel->borrowed_library = NULL;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        parallel->borrowed_library = saved_library;
        uint64_t saved_revision = parallel->borrowed_library_revision;
        parallel->borrowed_library_revision++;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        parallel->borrowed_library_revision = saved_revision;
        uint64_t saved_symbols = parallel->borrowed_symbol_instance;
        parallel->borrowed_symbol_instance++;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        parallel->borrowed_symbol_instance = saved_symbols;
        guest.context_ready = false;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        guest.context_ready = true;
        guest.failed = true;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        guest.failed = false;
        context.prime_hol_guest = NULL;
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        context.prime_hol_guest = &guest;
        space_add(&guest.space, parse_one(&arena, "guest-revision-change"));
        assert(!prime_native_parallel_borrowed_query_current(parallel, read));
        read = space_read_token(&guest.space);
        assert(prime_native_parallel_borrowed_query_current(parallel, read));
        assert(prime_native_control_record_query_read(&parent, read));
        assert(parent.query_read_count == 1u);
        assert(parent.query_reads[0].space == &guest.space);
        assert(prime_native_control_query_reads_current(&parent));
        puts("Exact guest identity, library revision, symbols, readiness and query token required");
    }
    if(requires_guest)call=typed_call;
    puts("Queued owned read service admitted");
    assert(prime_ordinary_capsule_stack_start(child));
    assert(!child->driver.ordinary_ordered && child->driver.capsule_stack);
    PrimeEvalStackTask task = child->driver.task;
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    puts("Retained stack read service admitted");
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, mutation, &child->output));
    assert(!prime_native_parallel_pin_effect(parallel));
    assert(!parallel->resource_owner_pinned && !parallel->coordinator_only);
    puts("Persistent mutation and borrowed pin refused");
    parallel->child = &parent;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    parallel->child = child;
    assert(!prime_native_parallel_read_service_admitted(child, &foreign,
        owned_arena, call, &child->output));
    OutcomeSet unowned;
    outcome_set_init(&unowned);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &unowned));
    Arena *saved_owner = child->output.payload_owner;
    child->output.payload_owner = &arena;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->output.payload_owner = saved_owner;
    puts("Wrong owner source target and payload refused");
    child->driver.capsule_stack = false;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->driver.capsule_stack = true;
    child->driver.ordinary_shared_source = false;
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    child->driver.ordinary_shared_source = true;
    cetta_shared_transition_guard_leave(&transition);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    cetta_shared_transition_guard_enter(&transition);
    assert(prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    assert(!memcmp(&task, &child->driver.task, sizeof(task)));
    assert(!child->steps && !parent.steps && !child->output.len);
    puts("Representation source and coordinator guard required without debit");
    cetta_shared_transition_guard_leave(&transition);
    cetta_shared_transition_scope_leave();
    space_add(&source, parse_one(&arena, "(= changed-read-source 9)"));
    cetta_shared_transition_scope_enter();
    cetta_shared_transition_guard_enter(&transition);
    assert(!prime_native_parallel_read_service_admitted(child, owned_space,
        owned_arena, call, &child->output));
    cetta_shared_transition_guard_leave(&transition);
    cetta_shared_transition_scope_leave();
    puts("Changed source revision refused");
    prime_native_run_leave(&guard);
    g_prime_native_parallel_current = saved_parallel;
    g_registry = saved_registry;
    outcome_set_free(&unowned);
    if (which == 6u) {
        work->parallel = NULL;
        prime_native_parallel_release(parallel);
        assert(parent.query_read_count == 1u);
        assert(prime_native_control_query_reads_current(&parent));
        space_add(&guest.space, parse_one(&arena, "guest-second-revision"));
        assert(!prime_native_control_query_reads_current(&parent));
        puts("Parent retains exact guest read after child retirement within live library scope");
    }
    release_stack_control(&parent);
    eval_set_library_context(NULL);
    if (which == 6u) {
        assert(context.prime_hol_guest == &guest);
        context.prime_hol_guest = NULL;
        space_free(&guest.space);
    }
    cetta_library_context_free(&context);
finish_resources:
    g_prime_need_owner = g_prime_need_episode_owner = NULL;
    registry_free(&registry);
    g_registry = NULL;
    space_free(&foreign);
    space_free(&source);
    arena_free(&arena);
}

static void guest_read_service_cases(void) {
    const char *assistant = getenv("CETTA_PRIME_HE_TYPED_APPLICABILITY");
    const char *pruning = getenv("CETTA_NIK_TYPED_APPLICABILITY");
    char *saved_assistant = assistant ? strdup(assistant) : NULL;
    char *saved_pruning = pruning ? strdup(pruning) : NULL;
    assert(!assistant || saved_assistant);
    assert(!pruning || saved_pruning);
    for (unsigned which = 0u; which < 8u; which++) {
        guest_read_service_case(which);
        printf("Guest retained read service %u passed\n", which);
    }
    if (saved_assistant) {
        assert(setenv("CETTA_PRIME_HE_TYPED_APPLICABILITY", saved_assistant, 1) == 0);
        free(saved_assistant);
    } else assert(unsetenv("CETTA_PRIME_HE_TYPED_APPLICABILITY") == 0);
    if (saved_pruning) {
        assert(setenv("CETTA_NIK_TYPED_APPLICABILITY", saved_pruning, 1) == 0);
        free(saved_pruning);
    } else assert(unsetenv("CETTA_NIK_TYPED_APPLICABILITY") == 0);
}

static void native_data_branch_case(unsigned which) {
    assert(which < 8u);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(
        &context, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(&context);
    assert(eval_current_language_id() == CETTA_LANGUAGE_PRIME);
    Arena arena;
    arena_init(&arena);
    Arena *saved_owner = g_prime_need_owner;
    Arena *saved_episode = g_prime_need_episode_owner;
    g_prime_need_owner = g_prime_need_episode_owner = &arena;
    PrimeNeedSnapshot base, selected;
    prime_need_snapshot_init(&base);
    assert(prime_need_snapshot_begin(&base));
    Atom *origin = which == 6u ? atom_int(&arena, 42)
        : br_parse(&arena, "(println! NativeDataForbidden)");
    uint64_t id = 0u;
    assert(prime_need_snapshot_allocate(&arena, &base, origin, &selected, &id));
    if (which == 6u) {
        PrimeNeedSnapshot running;
        uint64_t evaluator = prime_need_fresh_evaluator_id();
        assert(prime_need_snapshot_start_evaluation(
            &arena, &selected, id, evaluator, &running));
        assert(prime_need_snapshot_resolve_ready_value(
            &arena, &running, id, evaluator, atom_int(&arena, 99), &selected));
    }
    Atom *reference = prime_need_ref(&arena, &selected, id);
    assert(reference);
    if (which == 1u)
        reference = prime_need_ref_restrict(
            &arena, reference, &selected, CETTA_PRIME_NEED_RIGHT_FORCE);
    if (which == 2u) {
        PrimeNeedSnapshot foreign;
        prime_need_snapshot_init(&foreign);
        assert(prime_need_snapshot_begin(&foreign));
        uint64_t foreign_id = 0u;
        assert(prime_need_snapshot_allocate(
            &arena, &foreign, origin, &foreign, &foreign_id));
        reference = prime_need_ref(&arena, &foreign, foreign_id);
    }
    Atom *input = atom_expr2(&arena, atom_symbol(&arena, "Box"), reference);
    if (which == 5u)
        input = atom_expr2(&arena, atom_symbol_id(
            &arena, prime_need_symbols()->promise), reference);
    if (which == 7u)
        input = atom_expr3(&arena, atom_symbol(&arena, "Pair"), reference, reference);
    Bindings caller, returned;
    bindings_init(&caller);
    bindings_init(&returned);
    *bindings_need_mut(&caller) = which == 3u || which == 4u ? selected : base;
    *bindings_need_mut(&returned) = selected;
    PrimeNeedCellView before, after;
    assert(prime_need_snapshot_lookup(&selected, id, &before));
    PrimeNeedActiveGuard active;
    prime_need_active_enter(&active, &caller);
    const PrimeNeedFrame *caller_top = g_prime_need_active.top;
    const Bindings *caller_logical = g_prime_need_logical_env;
    Atom *value = prime_need_observation_data_argument(
        &arena, input, which == 4u ? NULL : &returned);
    assert(value);
    assert(g_prime_need_active.top == caller_top);
    assert(g_prime_need_logical_env == caller_logical);
    prime_need_active_leave(&active);
    assert(prime_need_snapshot_lookup(&selected, id, &after));
    assert(before.origin == after.origin && before.cached == after.cached);
    assert(before.cache_state == after.cache_state &&
           before.value_role == after.value_role &&
           before.evaluator_id == after.evaluator_id);
    assert(bindings_need_view(&returned)->top == selected.top);
    if (which == 2u || which == 5u)
        assert(atom_eq(value, input));
    else if (which == 1u)
        assert(atom_eq(value, br_parse(&arena, "(Box suspension:opaque)")));
    else {
        Atom *expected = which == 7u
            ? atom_expr3(&arena, atom_symbol(&arena, "Pair"), origin, origin)
            : atom_expr2(&arena, atom_symbol(&arena, "Box"), origin);
        assert(atom_eq(value, expected));
    }
    printf("NativeDataBranch %u passed\n", which);
    bindings_free(&returned);
    bindings_free(&caller);
    g_prime_need_owner = saved_owner;
    g_prime_need_episode_owner = saved_episode;
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(&context);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
typedef struct {
    const PrimeNeedSavedContext *saved;
    PrimeNeedReceipt receiving;
} SavedContextTransfer;

static void *saved_context_transfer_worker(void *raw) {
    const SavedContextTransfer *transfer = raw;
    PrimeNeedActiveGuard caller;
    prime_need_active_enter(&caller, NULL);
    prime_need_active_receipt_set(transfer->receiving);
    prime_need_saved_context_install(transfer->saved);
    const PrimeNeedActiveGuard *saved = &transfer->saved->value;
    CHECK(g_prime_need_active.session_id == saved->previous.session_id &&
        g_prime_need_active.top == saved->previous.top);
    CHECK(g_prime_need_branch_state_active.session_id == saved->previous_branch_state.session_id &&
        g_prime_weight_active.last == saved->previous_weights.last &&
        g_prime_need_logical_env == saved->previous_logical_env);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active,
        prime_need_saved_context_receipt(transfer->saved)));
    prime_need_active_leave(&caller);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    return NULL;
}

static void active_context_transition_case(bool replace) {
    Arena arena;
    arena_init_detached(&arena);
    Bindings ambient, empty, recorded;
    bindings_init(&ambient); bindings_init(&empty); bindings_init(&recorded);
    CHECK(prime_need_snapshot_begin(bindings_need_mut(&ambient)));
    PrimeNeedSnapshot need = *bindings_need_view(&ambient);
    PrimeNeedActiveGuard outside;
    prime_need_active_enter(&outside, &ambient);
    PrimeNeedReceipt before = g_prime_need_receipt_active;
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){2u, 65536u, 1u, 8u});
    bool demoted = false;
    PrimeNeedReceipt root = {0};
    bool ready = group && prime_need_recording_reserve(
        group, NULL, 2u, 65536u, &root) &&
        prime_need_receipt_observe_cell(&arena, &root, 31u, 1u,
            atom_int(&arena, 7), &root);
    CHECK(ready);
    if (!ready) goto done;

    /* Recording can begin inside a previously plain guard. Leaving it must
     * restore its empty origin, including after a nested context clears it. */
    PrimeNeedActiveGuard late;
    prime_need_active_enter(&late, NULL);
    prime_need_active_receipt_set(root);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    /* Restoring an HE episode must restore the context operations as well
     * as its receipt. A later guard must capture the restored diary. */
    CettaWeightLedger previous_weights = g_prime_weight_active;
    CettaWeightLedger captured_weights = {0};
    CHECK(cetta_weight_ledger_append(&arena, &captured_weights,
        prime_need_fresh_source_occurrence(), prime_need_fresh_source_occurrence(),
        atom_expr2(&arena, atom_symbol(&arena, "Coefficient"), atom_int(&arena, 17)),
        &captured_weights));
    g_prime_weight_active = captured_weights;
    HeCanonicalAmbient ambient_state;
    he_canonical_ambient_capture(&ambient_state);
    g_prime_weight_active = (CettaWeightLedger){0};
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
    he_canonical_ambient_install(&ambient_state);
    CHECK(cetta_weight_ledger_equal(&g_prime_weight_active, &captured_weights));
    g_prime_weight_active = previous_weights;
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    PrimeNeedActiveGuard imported;
    prime_need_active_save(&imported, NULL);
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
    prime_need_active_leave(&imported);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    PrimeNeedActiveGuard captured;
    prime_need_active_enter_captured(&captured, &empty);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    CHECK(!g_prime_need_active.session_id && g_prime_need_logical_env == &empty);
    prime_need_active_leave(&captured);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    CHECK(g_prime_need_active.session_id == need.session_id);
    /* A closed top entry starts empty and restores its complete caller.
     * Exercise both public entries from ordinary and diary-carrying origins. */
    PrimeNeedActiveGuard roundtrip;
    prime_need_active_save(&roundtrip, NULL);
    PrimeNeedBranchState branch = {0};
    CHECK(prime_need_branch_state_begin(&arena, &branch));
    g_prime_need_branch_state_active = branch;
    g_prime_weight_active = captured_weights;
    const Bindings *logical = g_prime_need_logical_env;
    Space top_space; space_init(&top_space);
    Registry top_registry; registry_init(&top_registry);
    for (unsigned diary = 0u; diary < 2u; diary++) {
        PrimeNeedReceipt origin = diary ? root : (PrimeNeedReceipt){0};
        prime_need_active_receipt_set(origin);
        /* Borrowed values stay quiescent through the join. Transporting this
         * context grants no authority to execute the source computation. */
        PrimeNeedSavedContext transferred;
        prime_need_saved_context_capture(&transferred);
        SavedContextTransfer transfer = {
            &transferred, diary ? (PrimeNeedReceipt){0} : root};
        pthread_t receiving_thread;
        int started = pthread_create(&receiving_thread, NULL,
            saved_context_transfer_worker, &transfer);
        CHECK(started == 0);
        if (!started) CHECK(pthread_join(receiving_thread, NULL) == 0);
        CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &origin));
        /* A force starts in the active world, while its lexical environment
         * can retain a different diary. These are independent origins. */
        Bindings lexical;
        bindings_init(&lexical);
        PrimeNeedReceipt lexical_receipt = diary ? (PrimeNeedReceipt){0} : root;
        *bindings_receipt_mut(&lexical) = lexical_receipt;
        *bindings_branch_state_mut(&lexical) = branch;
        *bindings_weight_mut(&lexical) = captured_weights;
        lexical.prime_ext->occurrence_token = 17u;
        Atom *lexical_root = atom_var(&arena, "force-lexical");
        CHECK(bindings_add_var(&lexical, lexical_root, atom_int(&arena, 23)));
        const Bindings *saved_logical = g_prime_need_logical_env;
        g_prime_need_logical_env = &lexical;
        PrimeEvalStackFrame *force =
            prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_FORCE);
        PrimeNeedForceEnvironmentPrepare prepare_force =
            g_prime_need_execution_ops.capture_force(force);
        force->evaluating = need;
        bool captured_force =
            prime_eval_stack_capture_dynamic_env(&force->env, &lexical);
        force->env_initialized = captured_force;
        CHECK(captured_force);
        CHECK(force->before.session_id == need.session_id &&
            prime_need_receipt_equal(&force->before_receipt, &origin));
        /* Dynamic completion fills absent metadata, but never overwrites
         * the already captured lexical diary. */
        CHECK(prime_need_receipt_equal(bindings_receipt_view(&force->env),
            diary ? &origin : &lexical_receipt));
        if (!diary)
            CHECK(!prime_need_receipt_equal(&force->before_receipt,
                bindings_receipt_view(&force->env)));
        /* Both capture routes retain bindings, while installing exactly
         * the force's origin and the caller's operational occurrence. */
        for (unsigned shape = 0u; shape < 4u; shape++) {
            if (shape == 2u) {
                for (unsigned i = 0u; i < 80u; i++) {
                    char name[48];
                    snprintf(name, sizeof(name), "force-unreachable-%u", i);
                    CHECK(bindings_add_var(&lexical, atom_var(&arena, name),
                        atom_int(&arena, (int64_t)i)));
                }
            }
            PrimeNeedCellView cell = {.origin=lexical_root};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
            VarId capture = lexical_root->var_id;
            cell.capture_known = (shape & 1u) != 0u;
            cell.capture_var_ids = &capture;
            cell.capture_var_count = 1u;
#endif
            Bindings projected;
            bool projected_ok = prime_need_project_cell_bindings(
                &arena, &cell, &lexical, &projected);
            CHECK(projected_ok);
            if (projected_ok) {
                CHECK(!projected.prime_ext && projected.len == 1u);
                Bindings unchanged;
                bool unchanged_ok = prime_need_project_cell_logical_env(
                    &arena, &cell, &lexical, &unchanged, NULL, NULL);
                CHECK(unchanged_ok);
                if (unchanged_ok) {
                    CHECK(prime_need_receipt_equal(bindings_receipt_view(&unchanged),
                        &lexical_receipt) && unchanged.len == projected.len);
                    bindings_free(&unchanged);
                }
                prime_need_observation_policy_changed();
                prepare_force(force, &lexical, &projected);
                CHECK(bindings_need_view(&projected)->session_id == need.session_id &&
                    prime_need_branch_state_equal(bindings_branch_state_view(&projected), &branch) &&
                    cetta_weight_ledger_equal(bindings_weight_view(&projected), &captured_weights) &&
                    projected.prime_ext->occurrence_token == 17u &&
                    prime_need_receipt_equal(bindings_receipt_view(&projected), &origin));
                bool applied = false;
                Atom *value = bindings_apply_body_exact_env(
                    &arena, lexical_root, &projected, &applied);
                CHECK(applied && atom_eq(value, atom_int(&arena, 23)));
                bindings_free(&projected);
            }
        }
        prime_eval_stack_frame_free(force);
        g_prime_need_logical_env = saved_logical;
        bindings_free(&lexical);
        PrimeNeedActiveGuard closed;
        prime_need_active_enter_captured(&closed, NULL);
        CHECK(!g_prime_need_active.session_id &&
            !g_prime_need_branch_state_active.session_id &&
            !g_prime_weight_active.length && !g_prime_need_logical_env &&
            !prime_need_receipt_present(&g_prime_need_receipt_active));
        /* New history inside the empty context cannot leak into its caller. */
        prime_need_active_receipt_set(root);
        prime_need_active_leave(&closed);
        CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &origin));
        for (unsigned registered = 0u; registered < 2u; registered++) {
            ResultSet values; result_set_init(&values);
            Atom *request = atom_int(&arena, 7);
            if (registered)
                eval_top_with_registry(&top_space, &arena, NULL, &top_registry,
                    request, &values);
            else
                eval_top(&top_space, &arena, request, &values);
            CHECK(values.len == 1u && atom_eq(values.items[0], request));
            CHECK(g_prime_need_active.session_id == need.session_id &&
                g_prime_need_branch_state_active.session_id == branch.session_id &&
                cetta_weight_ledger_equal(&g_prime_weight_active, &captured_weights) &&
                g_prime_need_logical_env == logical &&
                prime_need_receipt_equal(&g_prime_need_receipt_active, &origin));
            result_set_free(&values);
        }
    }
    registry_free(&top_registry); space_free(&top_space);
    prime_need_active_leave(&roundtrip);
    prime_need_active_leave(&late);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    CHECK(g_prime_need_active.session_id == need.session_id);

    /* A tail's fallback is its entry context. New recorded data arrives only
     * at a successful environment mutation, on both merge and replacement. */
    *bindings_receipt_mut(&recorded) = root;
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, &empty));
    PrimeNeedActiveGuard tail;
    prime_need_active_save(&tail, &empty);
    prime_need_active_tail(&builder.current, &tail);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    bool changed = replace
        ? prime_need_active_replace_environment(&tail, &builder, &recorded)
        : prime_need_active_merge_environment(&tail, &builder, &recorded);
    CHECK(changed);
    if (changed) {
        prime_need_active_tail(&builder.current, &tail);
        CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
        CHECK(g_prime_need_active.session_id == need.session_id);
        CHECK(g_prime_need_logical_env == &builder.current);
    }
    prime_need_active_leave(&tail);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    CHECK(g_prime_need_active.session_id == need.session_id);
    bindings_builder_free(&builder);

    /* A caller with a diary keeps that fallback when its plain tail has no
     * diary of its own. An explicitly captured empty world still clears it. */
    prime_need_active_receipt_set(root);
    PrimeNeedActiveGuard carried;
    prime_need_active_save(&carried, &empty);
    prime_need_active_tail(&empty, &carried);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    CHECK(g_prime_need_active.session_id == need.session_id);
    prime_need_active_leave(&carried);

    /* Refreshing a formerly plain tail after late history creation restores
     * its empty fallback. Its cleanup then uses the ordinary operation. */
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
    prime_need_active_save(&late, &empty);
    prime_need_active_receipt_set(root);
    prime_need_active_tail(&empty, &late);
    CHECK(!prime_need_receipt_present(&g_prime_need_receipt_active));
    CHECK(g_prime_need_active.session_id == need.session_id);
    prime_need_active_leave(&late);

    /* Reusable regions restore the same context before retiring scratch.
     * A new diary prevents eliding a Need suffix even if its value is closed. */
    PrimeNeedRegionPool pool = {.stable_owner=&arena};
    PrimeNeedRegionPool *previous_pool = g_prime_need_region_pool;
    Arena *previous_owner = g_prime_need_owner;
    Arena *previous_branch_owner = g_prime_need_region_branch_owner;
    g_prime_need_region_pool = &pool;
    for (unsigned history = 0u; history < 2u; history++) {
        PrimeNeedReceipt origin = history ? root : (PrimeNeedReceipt){0};
        prime_need_active_receipt_set(origin);
        PrimeNeedRegionScope region;
        bool entered = prime_need_region_scope_enter(&region);
        CHECK(entered);
        if (!entered) continue;
        CHECK(pool.depth == 1u && g_prime_need_owner == &region.level->arena);
        CHECK(prime_need_receipt_equal(
            prime_need_active_original_receipt(&region.context), &origin));
        Outcome candidate;
        outcome_init(&candidate);
        candidate.atom = atom_int(&region.level->arena, 7);
        PrimeNeedSnapshot extended;
        uint64_t cell = 0u;
        bool allocated = prime_need_snapshot_allocate(&region.level->arena,
            &region.context.previous, candidate.atom, &extended, &cell);
        CHECK(allocated);
        if (allocated) {
            *bindings_need_mut(&candidate.env) = extended;
            *bindings_receipt_mut(&candidate.env) = origin;
            PrimeNeedSnapshot retained = {0};
            bool suffix = false;
            CHECK(outcome_region_prefix_only_need(
                &region, &candidate, NULL, &retained, &suffix) && suffix);
            CHECK(retained.session_id == need.session_id &&
                retained.max_storage_key == extended.max_storage_key);
            *bindings_receipt_mut(&candidate.env) = history
                ? (PrimeNeedReceipt){0} : root;
            CHECK(!outcome_region_prefix_only_need(
                &region, &candidate, NULL, &retained, &suffix));
        }
        outcome_free_fields(&candidate);
        prime_need_active_receipt_set(history ? (PrimeNeedReceipt){0} : root);
        prime_need_region_scope_leave(&region);
        CHECK(!pool.depth && g_prime_need_owner == previous_owner &&
            g_prime_need_region_branch_owner == previous_branch_owner);
        CHECK(g_prime_need_active.session_id == need.session_id);
        CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &origin));
    }
    prime_need_region_pool_free(&pool);
    g_prime_need_region_pool = NULL;
    PrimeNeedRegionScope refused;
    CHECK(!prime_need_region_scope_enter(&refused));
    prime_need_region_scope_leave(&refused);
    CHECK(g_prime_need_owner == previous_owner &&
        g_prime_need_active.session_id == need.session_id);
    g_prime_need_region_pool = previous_pool;

    /* An observed datum publishes without the private operational world,
     * while its caller keeps both its logical interpretation and diary. */
    prime_need_active_receipt_set(root);
    const Bindings *lexical = g_prime_need_logical_env;
    CettaLibraryContext prime_library;
    cetta_library_context_init_for_language_profile(&prime_library,
        CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    CettaLibraryContext *previous_library = g_library_context;
    eval_set_library_context(&prime_library);
    OutcomeSet data;
    outcome_set_init(&data);
    outcome_set_add_observed_data(&data, atom_int(&arena, 19));
    CHECK(data.len == 1u && !bindings_prime_present(&data.items[0].env));
    CHECK(g_prime_need_active.session_id == need.session_id &&
        g_prime_need_logical_env == lexical);
    CHECK(prime_need_receipt_equal(&g_prime_need_receipt_active, &root));
    outcome_set_free(&data);
    eval_set_library_context(previous_library);
    cetta_library_context_free(&prime_library);

    /* Held HE work owns coefficient payloads through the same relocation
     * session as its input, and does not become a diary reader merely by
     * retaining the execution. */
    CettaLibraryContext he_library;
    cetta_library_context_init_for_language_profile(&he_library,
        CETTA_LANGUAGE_HE, cetta_profile_he_extended());
    eval_set_library_context(&he_library);
    Space he_space; space_init(&he_space);
    Registry he_registry; registry_init(&he_registry);
    registry_bind_id(&he_registry, g_builtin_syms.self, atom_space(&arena, &he_space));
    Bindings he_input; bindings_init(&he_input);
    *bindings_weight_mut(&he_input) = captured_weights;
    g_prime_weight_active = captured_weights;
    CettaHeExecution *held = eval_he_owned_begin(&he_space, &he_registry,
        CETTA_HE_ENTRY_EVALUATE, atom_int(&arena, 7), NULL, 100,
        &he_input, true, false);
    CHECK(held != NULL);
    g_prime_weight_active = previous_weights;
    if (held) {
        const CettaWeightLedger *copied = bindings_weight_view(&held->input_environment);
        CHECK(copied->length == 1u && copied->owner == &held->storage &&
            atom_graph_is_closed_for_arena(&held->storage, copied->last->value));
        CHECK(copied->length == 1u &&
            held->ambient.context.value.previous_weights.length == 1u &&
            copied->last->value == held->ambient.context.value.previous_weights.last->value);
        CHECK(prime_need_recording_status(group).payload_charge > 0u);
        prime_need_recording_demote(group);
        demoted = true;
        CHECK(prime_need_recording_status(group).payload_charge == 0u);
        CettaHeExecutionView view = {0};
        for (unsigned step = 0u; step < 100u; step++) {
            CHECK(eval_he_owned_inspect(held, &view));
            if (view.closed) break;
            CettaHeAdvance next = eval_he_owned_advance(held);
            CHECK(next == CETTA_HE_ADVANCED || next == CETTA_HE_CLOSED);
            CHECK(cetta_weight_ledger_equal(&g_prime_weight_active, &previous_weights));
            if (next != CETTA_HE_ADVANCED && next != CETTA_HE_CLOSED) break;
        }
        CHECK(eval_he_owned_inspect(held, &view) && view.closed &&
            view.outcomes->len == 1u);
        if (view.closed && view.outcomes->len == 1u) {
            const CettaWeightLedger *result = bindings_weight_view(&view.outcomes->items[0].env);
            CHECK(cetta_weight_ledger_equal(result, &captured_weights));
            CHECK(!prime_need_receipt_present(bindings_receipt_view(&view.outcomes->items[0].env)));
        }
        CHECK(eval_he_owned_destroy(held));
    }
    bindings_free(&he_input);
    registry_free(&he_registry); space_free(&he_space);
    eval_set_library_context(previous_library);
    cetta_library_context_free(&he_library);
done:
    prime_need_active_receipt_set(before);
    prime_need_active_leave(&outside);
    bindings_free(&ambient); bindings_free(&empty); bindings_free(&recorded);
    if (group) {
        if (demoted) prime_need_recording_carrier_release(group);
        else prime_need_recording_release(group);
    }
    arena_free(&arena);
}

static void recording_reader_release_case(void) {
    Arena input, carrier_owner, reader_owner;
    arena_init_detached(&input);
    arena_init_detached(&carrier_owner);
    arena_init_detached(&reader_owner);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){2u, 65536u, 1u, 8u});
    PrimeNeedReceipt root = {0}, carrier = {0}, reader = {0};
    Bindings captured, completed, empty;
    bindings_init(&captured);
    bindings_init(&completed);
    bindings_init(&empty);
    PrimeNeedActiveGuard outside;
    prime_need_active_enter(&outside, NULL);
    bool demoted = false;
    bool ready = group && prime_need_recording_reserve(
        group, NULL, 2u, 65536u, &root) &&
        prime_need_receipt_observe_cell(&input, &root, 31u, 1u,
            atom_int(&input, 7), &root);
    CHECK(ready);
    if (!ready) goto done;
    /* A captured diary remains data with no active recorder. Replacing the
     * observation policy must not replace the captured context's transport. */
    CHECK(!g_prime_need_recording_scope);
    prime_need_active_receipt_set(root);
    prime_need_observation_policy_changed();
    CHECK(bindings_capture_active_need(&captured));
    prime_complete_dynamic_occurrence(&completed);
    CHECK(prime_need_receipt_equal(bindings_receipt_view(&captured), &root));
    CHECK(prime_need_receipt_equal(bindings_receipt_view(&completed), &root));
    PrimeNeedActiveGuard sealed;
    prime_need_active_enter_captured(&sealed, &empty);
    CHECK(bindings_capture_active_need(&captured));
    prime_complete_dynamic_occurrence(&completed);
    CHECK(prime_need_receipt_equal(bindings_receipt_view(&captured), &root));
    CHECK(prime_need_receipt_equal(bindings_receipt_view(&completed), &root));
    CHECK(bindings_capture_active_need(&empty) && !empty.prime_ext);
    prime_need_active_leave(&sealed);
    bindings_free(&completed);
    bindings_init(&completed);
    prime_complete_dynamic_occurrence(&completed);
    CHECK(prime_need_receipt_equal(bindings_receipt_view(&completed), &root));
    carrier = reader = root;
    ready = prime_need_recording_promote_carrier(&carrier_owner, &carrier) &&
        prime_need_recording_promote(&reader_owner, &reader);
    CHECK(ready && carrier.owner == root.owner && reader.owner == &reader_owner);
    if (!ready) goto done;
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_graph_collect(&reader, &graph) ==
        PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 1u);
    uint64_t identity = graph.root_id;
    prime_need_receipt_graph_free(&graph);
    size_t payload = prime_need_recording_status(group).payload_charge;
    CHECK(payload > 0u);
    prime_need_recording_demote(group);
    demoted = true;
    CHECK(!prime_need_receipt_present(&root) && !prime_need_receipt_present(&carrier));
    /* Erasure can happen after context selection. Its weak carrier must not
     * become readable again merely by returning or completing a value. */
    CHECK(bindings_capture_active_need(&empty) && !empty.prime_ext);
    prime_complete_dynamic_occurrence(&empty);
    CHECK(!empty.prime_ext);
    CHECK(prime_need_receipt_graph_collect(&carrier, &graph) ==
        PRIME_NEED_RECEIPT_GRAPH_ABSENT && !graph.length);
    prime_need_receipt_graph_free(&graph);
    CHECK(prime_need_receipt_graph_collect(&reader, &graph) ==
        PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 1u &&
        graph.root_id == identity && prime_need_recording_status(group).payload_charge == payload);
    prime_need_receipt_graph_free(&graph);
    CettaStorageCensus storage = {0};
    prime_need_recording_carrier_storage(group, &storage);
    size_t metadata = storage.retained;
    prime_need_recording_storage(group, &storage);
    cetta_storage_finish(&storage);
    CHECK(!storage.incomplete && storage.retained > metadata);
    cetta_storage_census_free(&storage);
    arena_free(&reader_owner);
    arena_init_detached(&reader_owner);
    CHECK(!prime_need_recording_readable(&reader));
    CHECK(prime_need_recording_status(group).payload_charge == 0u);
    prime_need_recording_storage(group, &storage);
    cetta_storage_finish(&storage);
    CHECK(!storage.incomplete && storage.retained == metadata);
    cetta_storage_census_free(&storage);
    PrimeNeedReceipt erased = carrier;
    CHECK(prime_need_recording_promote(&reader_owner, &erased) &&
        !erased.recording && !erased.top);
    CHECK(!prime_need_recording_promote(&reader_owner, &reader));
done:
    prime_need_active_leave(&outside);
    bindings_free(&empty);
    bindings_free(&completed);
    bindings_free(&captured);
    arena_free(&reader_owner);
    arena_free(&carrier_owner);
    if (demoted) prime_need_recording_carrier_release(group);
    else prime_need_recording_release(group);
    arena_free(&input);
}

static void suspended_history_owner_case(unsigned workers, uint64_t budget,
                                         bool restart) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry = &registry;
    registry_bind(&registry, "&self", atom_space(&arena, &space));
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(&context);
    term_universe_set_persistent_arena(&context.term_universe, &arena);
    const char *rows[] = {
        "(= (coin) (let $_ (println! \"SHARED-NEED\") (superpose (2 3))))",
        "(= (step $x) (+ $x 1))", "(= (consume 3) Three)",
        "(= (consume 4) Four)",
        "(= (relay $x) (Pair (consume (step $x)) (step $x)))",
        "(= (jobs $x) (relay $x))", "(= (jobs $x) Ready)"};
    for (size_t i = 0u; i < sizeof(rows) / sizeof(*rows); i++)
        space_add(&space, parse_one(&arena, rows[i]));
    CettaSearchPolicySpec policy = {.native_control=true, .all=true,
        .retain=true, .workers=workers, .work_budget=budget, .costs=true,
        .history={.enabled=true, .selections=512u, .nodes=32u,
                  .bytes=65536u, .owners=10000u}};
    PrimeNativeControl control;
    assert(prime_native_control_init(&control, &space, &arena,
        parse_one(&arena, "(jobs (coin))"), -1, true,
        (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, NULL));
    Observed observed = {0};
    prime_native_control_run(&control, budget, observe, &observed);
    CHECK(!control.failed && !control.invalidated);
    CHECK(!control.parallel_inflight_count && !control.parallel_wave_pending);
    size_t parallel = 0u;
    for (size_t i = 0u; i < cetta_continuation_hub_length(&control.hub); i++) {
        PrimeNativeWork *work = cetta_continuation_hub_at(&control.hub, i)->payload;
        CHECK(work->owner == &control && !work->dialect && !work->resource_provider);
        if (!work->parallel) continue;
        parallel++;
        PrimeNativeParallel *held = work->parallel;
        printf("SuspendedHistoryOwner workers=%u budget=%"PRIu64
            " phase=%u child=%u prepared=%u completed=%u attempted=%u"
            " spent=%"PRIu64" child-steps=%"PRIu64" effect=%u need=%u\n",
            workers, budget, (unsigned)held->execution.profile,
            held->child != NULL, held->world.prepared,
            held->completed, held->attempted, held->spent,
            held->child ? held->child->steps : 0u,
            held->child && held->child->effect_pending,
            held->child && held->child->need_pending);
    }
    printf("SuspendedHistoryOwner workers=%u budget=%"PRIu64
        " capsules=%zu dispatch=%"PRIu64" join=%"PRIu64"\n", workers,
        budget, parallel, control.event_cost.total.counts[CETTA_COST_WORKER_DISPATCH],
        control.event_cost.total.counts[CETTA_COST_WORKER_JOIN]);
    PrimeNativeHistorySpec stop = {.stop=true};
    PrimeNeedRecording *recording = control.history.group;
    PrimeNeedRecordingStatus frozen = prime_need_recording_status(recording);
    size_t selections = control.history.count;
    CHECK(prime_native_history_configure(&control, &stop));
    CHECK(!control.history.spec.enabled && control.history.stopped);
    CHECK(control.history.group == recording && control.history.count == selections);
    CHECK(prime_native_history_configure(&control, &stop));
    CHECK(control.history.stop == budget);
    prime_native_control_run(&control, 3u, observe, &observed);
    CHECK(control.history.count == selections);
    PrimeNeedRecordingStatus after_gap = prime_need_recording_status(recording);
    CHECK(after_gap.nodes == frozen.nodes && after_gap.omitted == frozen.omitted);
    if (restart) {
        uint64_t start = control.steps;
        PrimeNativeHistory before = control.history;
        PrimeNativeHistorySpec rejected = policy.history;
        rejected.owners = SIZE_MAX;
        CHECK(!prime_native_history_configure(&control, &rejected));
        CHECK(!memcmp(&before, &control.history, sizeof(before)));
        CHECK(control.steps == start && control.policy.history.stop);
        CHECK(prime_native_history_configure(&control, &policy.history));
        CHECK(control.history.spec.enabled && !control.history.stopped);
        CHECK(control.history.start == start && !control.history.count);
        CHECK(control.history.group && control.history.group != recording);
        CHECK(!arena_retains_owner(control.driver.arena, recording,
            prime_need_recording_release));
        CHECK(prime_need_recording_status(recording).nodes == 0u);
    }
    prime_native_control_run(&control, 5000u, observe, &observed);
    CHECK(restart ? control.history.count > 0u : control.history.count == selections);
    if (!restart) {
        PrimeNeedRecordingStatus after = prime_need_recording_status(recording);
        CHECK(after.nodes == frozen.nodes && after.omitted == frozen.omitted);
    }
    printf("SuspendedHistoryResult closed=%u count=%zu failed=%u invalidated=%u need=%u\n",
        prime_native_control_closed(&control), observed.count, control.failed,
        control.invalidated, control.need_pending);
    CHECK(prime_native_control_closed(&control) && observed.count == 3u);
    release_stack_control(&control);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry = NULL;
    space_free(&space); arena_free(&arena);
}
#endif

static void compact_cost_storage_case(unsigned workers, bool recorded_schedule) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry = &registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,
        CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(&context);
    const char *rows[] = {
        "(= (sum $n) (if (== $n 0) 0 (+ $n (sum (- $n 1)))))",
        "(= (compact-rows) (sum 20))",
        "(= (compact-rows) (sum 21))",
        "(= (compact-rows) (sum 21))"};
    for (size_t i = 0u; i < sizeof(rows) / sizeof(*rows); i++)
        space_add(&space, parse_one(&arena, rows[i]));
    CettaSearchPolicySpec policy = {.native_control = true, .all = true,
        .retain = true, .work_budget = UINT64_MAX, .workers = workers,
        .worker_quantum = 8u, .costs = true, .cost_compact = true,
        .receipts = recorded_schedule};
    PrimeNativeControl control;
    assert(prime_native_control_init(&control, &space, &arena,
        parse_one(&arena, "(compact-rows)"), -1, true,
        (CettaObservationDemand){.completion = CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, NULL));
    Observed observed = {0};
    unsigned rounds = 0u;
    while (!prime_native_control_closed(&control) && rounds++ < 10000u) {
        prime_native_control_run(&control, 17u, observe, &observed);
        CHECK(control.event_cost.compact && !control.event_cost.events &&
              !control.event_cost.length && !control.event_cost.capacity);
        CHECK(!control.failed && !control.invalidated && !control.driver.capacity_blocked);
        for (size_t i = 0u; i < cetta_continuation_hub_length(&control.hub); i++) {
            PrimeNativeWork *work = cetta_continuation_hub_at(&control.hub, i)->payload;
            if (work->parallel && work->parallel->child) {
                PrimeNativeControl *child = work->parallel->child;
                CHECK(child->policy.cost_compact);
                CHECK(!child->cost_enabled || (child->event_cost.compact &&
                      !child->event_cost.events && !child->event_cost.capacity));
            }
        }
        if (!recorded_schedule)
            for (PrimeNativeRefillHistory *h = control.refill_history; h; h = h->next)
                CHECK(!h->imported || h == control.refill_pending);
    }
    CHECK(prime_native_control_closed(&control) && observed.count == 3u);
    unsigned first = 0u, repeated = 0u;
    for (CettaCount i = 0u; i < control.output.len; i++) {
        Atom *value = outcome_atom_materialize(&arena, &control.output.items[i]);
        first += atom_eq(value, atom_int(&arena, 210));
        repeated += atom_eq(value, atom_int(&arena, 231));
    }
    CHECK(first == 1u && repeated == 2u);
    CHECK(control.event_cost.total.work > 0u &&
          control.event_cost.total.span <= control.event_cost.total.work);
    if (workers > 1u) {
        CHECK(control.event_cost.total.counts[CETTA_COST_WORKER_DISPATCH] > 0u);
        CHECK(control.event_cost.total.counts[CETTA_COST_WORKER_JOIN] > 0u);
    }
    release_stack_control(&control);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry = NULL;
    space_free(&space); arena_free(&arena);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
typedef struct { size_t checks, failures; } RecordingScopeControl;

static bool recording_scope_query(Arena *arena, Atom *equation, Atom *query,
    const Bindings *environment, bool recorded) {
    OutcomeSet results;
    outcome_set_init(&results);
    PrimeNeedRawEquationResult raw = {.arena=arena, .base_env=environment,
        .raw_results=&results, .equation=equation,
        .source_occurrence_id=prime_need_fresh_source_occurrence(),
        .rule_occurrence_id=1u};
    CettaCount count = query_equation_visit(equation, query, arena,
        prime_need_raw_equation_collector(), &raw);
    bool ok = count == 1u && raw.matched && results.len == 1u;
    if (ok) {
        Atom *answer = outcome_atom_materialize(arena, &results.items[0]);
        ok = atom_eq(answer, equation->expr.elems[2]) &&
            prime_need_receipt_present(bindings_receipt_view(&results.items[0].env)) == recorded;
    }
    outcome_set_free(&results);
    return ok;
}

static bool recording_scope_cached_force(Arena *arena, bool recorded, bool published) {
    PrimeNeedSnapshot root = {0}, allocated, running, ready;
    uint64_t thunk = 0u;
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    Atom *value = atom_int(arena, 7);
    if (!evaluator || !prime_need_snapshot_begin(&root) ||
        !prime_need_snapshot_allocate(arena, &root, value, &allocated, &thunk) ||
        !prime_need_snapshot_start_evaluation(arena, &allocated, thunk, evaluator, &running) ||
        !prime_need_snapshot_resolve_ready_value(arena, &running, thunk, evaluator, value, &ready))
        return false;
    Bindings environment;
    bindings_init(&environment);
    *bindings_need_mut(&environment) = ready;
    PrimeNeedActiveGuard active;
    prime_need_active_enter(&active, &environment);
    OutcomeSet answers;
    outcome_set_init(&answers);
#if CETTA_PRIME_EVAL_STACK
    if (published)
        (void)prime_need_observe_published_value(arena, thunk, &ready, &answers);
    else
#else
    (void)published;
#endif
    {
        Space space;
        space_init(&space);
        prime_need_force_active(&space, arena, prime_need_ref(arena, &ready, thunk),
                                thunk, -1, &answers);
        space_free(&space);
    }
    bool ok = answers.len == 1u;
    if (ok)
        ok = atom_eq(outcome_atom_materialize(arena, &answers.items[0]), value) &&
            answers.items[0].result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            prime_need_receipt_present(bindings_receipt_view(&answers.items[0].env)) == recorded;
    outcome_set_free(&answers);
    prime_need_active_leave(&active);
    bindings_free(&environment);
    return ok;
}

static bool recording_scope_state(Arena *arena, StateCell *cell, bool recorded) {
    Bindings environment;
    bindings_init(&environment);
    Atom *updated = atom_int(arena, 2);
    PrimeNeedBranchState state;
    Atom *value = NULL;
    bool ok = prime_need_read_state(arena, &environment, cell, cell->value) &&
        prime_need_write_state(arena, &environment, cell, updated) &&
        prime_need_effective_state(&environment, cell, &state, &value) &&
        atom_eq(value, updated) &&
        prime_need_receipt_present(bindings_receipt_view(&environment)) == recorded;
    bindings_free(&environment);
    return ok;
}

/* The same bridge callback follows the receiving scope, rather than retaining
 * its first observer. Even a plain delivery must establish occurrence identity. */
static bool recording_scope_relation(Arena *arena, Atom *equation, Atom *query,
    const Bindings *environment, bool observed, bool recorded) {
    PettaEquationCandidate candidate = {.equation=equation,
        .occurrence={.logical_index=0u}};
    Bindings delta;bindings_init(&delta);
    uint64_t call=petta_eval_machine_prime_begin_relation_call(
        NULL,(SpaceReadToken){0},query);
    bool ok=call!=0u &&
        petta_eval_machine_prime_equation_result_payload_observed(NULL)==observed &&
        petta_eval_machine_prime_record_equation_use(NULL,arena,call,&candidate,
            equation->expr.elems[2],environment,&delta);
    if(ok) {
        uint64_t occurrence=bindings_occurrence_token(&delta);
        ok=occurrence!=0u && occurrence!=bindings_occurrence_token(environment) &&
            prime_need_receipt_present(bindings_receipt_view(&delta))==recorded;
        if(recorded) {
            const PrimeNeedReceipt *receipt=bindings_receipt_view(&delta);
            PrimeNeedReceiptGraph graph={0};
            ok=ok && prime_need_receipt_graph_collect(receipt,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
                graph.length==1u && graph.nodes[0].has_event &&
                graph.nodes[0].event.kind==PRIME_NEED_RECEIPT_USE_EQUATION &&
                atom_eq(graph.nodes[0].event.before,equation) &&
                atom_eq(graph.nodes[0].event.after,equation->expr.elems[2]);
            prime_need_receipt_graph_free(&graph);
        }
    }
    bindings_free(&delta);
    return ok;
}

/* Enter through the low-level scope API on a fresh thread, before any
 * evaluator entry. A refused nested slice must not escape its allowance,
 * and leaving the outer scope must stop receipt construction. */
static bool recording_scope_task(CettaParallelWorker *worker, void *task, void *context) {
    (void)context;
    RecordingScopeControl *result = task;
#define SCOPE_CHECK(condition) do { result->checks++; if (!(condition)) { \
    result->failures++; fprintf(stderr,"FAIL: recording scope: %s\n",#condition); } } while (0)
    Arena *arena = cetta_parallel_worker_arena(worker);
    arena_set_hashcons(arena, NULL);
    Bindings environment; bindings_init(&environment);
    Atom *equation = parse_one(arena, "(= (scope-entry) 7)");
    Atom *query = parse_one(arena, "(scope-entry)");
    StateCell *cell = arena_alloc(arena, sizeof(*cell));
    *cell = (StateCell){.value=atom_int(arena, 1)};
    PrimeNeedRecording *group = prime_need_recording_new((PrimeNeedRecordingLimits){
        .nodes=32u, .payload_bytes=32768u, .slices=4u, .owner_links=16u});
    PrimeNeedReceipt receipt = {0}, refused = {0};
    PrimeNeedRecordingScope outer = {0}, inner = {0};
    SCOPE_CHECK(group != NULL);
    if (!group) goto done;
    bool reserved = prime_need_recording_reserve(group, NULL, 16u, 16384u, &receipt);
    SCOPE_CHECK(reserved);
    if (!reserved) goto done;
    prime_need_recording_enter(&outer, &receipt);
    SCOPE_CHECK(recording_scope_query(arena,equation,query,&environment,true));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 1u);
    SCOPE_CHECK(recording_scope_relation(arena,equation,query,&environment,true,true));
    SCOPE_CHECK(recording_scope_cached_force(arena,true,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,true,true));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 4u);
    prime_need_recording_enter(&inner, &refused);
    SCOPE_CHECK(recording_scope_query(arena,equation,query,&environment,false));
    SCOPE_CHECK(recording_scope_relation(arena,equation,query,&environment,true,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,false,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,false,true));
    SCOPE_CHECK(recording_scope_state(arena,cell,false));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 4u);
    prime_need_recording_leave(&inner);
    SCOPE_CHECK(recording_scope_query(arena,equation,query,&environment,true));
    SCOPE_CHECK(recording_scope_relation(arena,equation,query,&environment,true,true));
    SCOPE_CHECK(recording_scope_cached_force(arena,true,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,true,true));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 8u);
    /* Bounded recording refuses raw StateCell pointers explicitly. The
     * operational read/write still succeeds and marks the history incomplete. */
    SCOPE_CHECK(recording_scope_state(arena,cell,true));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 8u);
    SCOPE_CHECK(prime_need_recording_status(group).incomplete);
    SCOPE_CHECK(prime_need_recording_status(group).omitted == 2u);
    prime_need_recording_leave(&outer);
    SCOPE_CHECK(recording_scope_query(arena,equation,query,&environment,false));
    SCOPE_CHECK(recording_scope_relation(arena,equation,query,&environment,false,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,false,false));
    SCOPE_CHECK(recording_scope_cached_force(arena,false,true));
    SCOPE_CHECK(recording_scope_state(arena,cell,false));
    SCOPE_CHECK(prime_need_recording_status(group).nodes == 8u);
    SCOPE_CHECK(prime_need_recording_status(group).omitted == 2u);
done:
    prime_need_recording_leave(&inner);
    prime_need_recording_leave(&outer);
    if (group) prime_need_recording_release(group);
    bindings_free(&environment);
#undef SCOPE_CHECK
    return result->failures == 0u;
}

static void recording_scope_entry_control(void) {
    RecordingScopeControl result = {0};
    PrimeNeedExecutionOps previous = g_prime_need_execution_ops;
    const PrimeNeedReceipt *scope = g_prime_need_recording_scope;
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {.thread_count=1u, .task_fn=recording_scope_task};
    bool initialized = cetta_parallel_executor_init(&executor, &config);
    CHECK(initialized);
    if (!initialized) return;
    bool pushed = cetta_parallel_executor_push(&executor, &result);
    CHECK(pushed);
    if (pushed) {
        bool ran = cetta_parallel_executor_run(&executor);
        CHECK(ran);
        checks += result.checks; failures += result.failures;
    }
    cetta_parallel_executor_free(&executor);
    CHECK(g_prime_need_execution_ops.equation_result == previous.equation_result);
    CHECK(g_prime_need_execution_ops.relation_use == previous.relation_use);
    CHECK(g_prime_need_execution_ops.relation_payload_observed == previous.relation_payload_observed);
    CHECK(g_prime_need_execution_ops.read_state == previous.read_state);
    CHECK(g_prime_need_execution_ops.write_state == previous.write_state);
    CHECK(g_prime_need_execution_ops.force_active == previous.force_active);
#if CETTA_PRIME_EVAL_STACK
    CHECK(g_prime_need_execution_ops.observe_published_value == previous.observe_published_value);
    CHECK(g_prime_need_execution_ops.publish_force == previous.publish_force);
#endif
    CHECK(g_prime_need_recording_scope == scope);
}
#endif

static void space_subtype_lifetime_case(void) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space source, target;
    memset(&source, 0xa5, sizeof(source));
    memset(&target, 0xa5, sizeof(target));
    space_init(&source); space_init(&target);
    CHECK(!source.declares_subtypes && !target.declares_subtypes);
    Atom *edge = parse_one(&arena, "(:< Child Parent)");
    space_add(&source, edge);
    CHECK(source.declares_subtypes);
    space_remove(&source, edge);
    CHECK(source.declares_subtypes); /* conservative within this lifetime */
    space_add(&source, edge);
    space_replace_contents(&target, &source);
    CHECK(target.declares_subtypes && space_length64(&target) == 1u);
    CHECK(!source.declares_subtypes && space_length64(&source) == 0u);
    space_free(&target); space_free(&source); arena_free(&arena);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static Atom *replay_history_call(Space *space, Arena *arena, Atom *call) {
    Bindings environment; bindings_init(&environment);
    OutcomeSet answers; outcome_set_init(&answers);
    assert(prime_native_replay_dispatch(space, arena, call, &environment, false, &answers));
    assert(answers.len == 1u);
    Atom *result = outcome_atom_materialize(arena, &answers.items[0]);
    outcome_set_free(&answers); bindings_free(&environment);
    return result;
}

/* Require a genuine cached-producer use and join before removing its
 * selection-to-forest association. Physical event storage remains intact. */
static bool replay_history_sharing_root(const PrimeNeedReceipt *root, void *raw) {
    PrimeNeedReceiptGraph graph = {0};
    assert(prime_need_receipt_graph_collect(root, &graph) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    bool joined = false, sharing = false;
    for (size_t i = 0u; i < graph.length; i++) {
        const PrimeNeedReceiptGraphNode *use = &graph.nodes[i];
        joined |= !use->has_event;
        if (!use->has_event || use->event.producer_completed || !use->event.producer_update_id) continue;
        for (size_t j = 0u; j < graph.length; j++) {
            const PrimeNeedReceiptGraphNode *producer = &graph.nodes[j];
            sharing |= producer->has_event && producer->event.producer_completed &&
                producer->event.producer_update_id == use->event.producer_update_id &&
                producer->event.need_session_id == use->event.need_session_id &&
                producer->event.thunk_id == use->event.thunk_id;
        }
    }
    *(bool *)raw |= joined && sharing;
    prime_need_receipt_graph_free(&graph);
    return true;
}

/* Damage only an unpublished recorded checkpoint. The replay operation must
 * authenticate that checkpoint against execution, not accept its printed data.
 * The live reserved computation is never the target of these mutations. */
static void replay_history_evidence_case(unsigned damage) {
    HashConsTable globals; hashcons_init(&globals);
    HashConsTable *saved_globals = g_hashcons; g_hashcons = &globals;
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, &globals);
    Arena *saved_persistent = g_eval_fallback_universe.persistent_arena;
    CettaLibraryContext *saved_context = g_library_context;
    CettaLibraryContext *context = malloc(sizeof(*context)); assert(context);
    cetta_library_context_init_for_language_profile(context,
        CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    eval_set_library_context(context);
    term_universe_set_persistent_arena(&context->term_universe, &arena);
    term_universe_set_persistent_arena(&g_eval_fallback_universe, &arena);
    Space space;
    if (damage == 5u) space_init(&space);
    else space_init_with_universe(&space, &context->term_universe);
    Registry registry; registry_init(&registry); Registry *saved_registry = g_registry;
    g_registry = &registry; registry_bind(&registry, "&self", atom_space(&arena, &space));
    /* A previous, independent query may leave a live or stale optimization
     * entry in this thread. Capture must neither borrow it as authority nor
     * clear it to make the new source admissible. */
    Space foreign;space_init(&foreign);
    Atom *foreign_equation=parse_one(&arena,"(= (replay:unrelated) 23)");
    space_add(&foreign,foreign_equation);
    EquationMatchDecisionCache *ambient=equation_match_cache_current();
    EquationMatchDecisionCacheSlot *foreign_slot=equation_match_decision_lookup(
        &foreign,atom_symbol_current_id(atom_symbol(&arena,"replay:unrelated")),
        CETTA_MATCH_DECISION_LINEAR,he_match_decision_semantics(),&arena);
    assert(foreign_slot);
    if(damage%2u)space_add(&foreign,parse_one(&arena,"(= (replay:unrelated) 29)"));
    if(damage==6u)br_service_effects_refusals=1u;
    bool transaction=damage>=7u;
    unsigned corruption=transaction?0u:damage;
    Space target;
    if(transaction) {
        space_init_with_universe(&target,&context->term_universe);
        registry_bind(&registry,"&replay-target",atom_space(&arena,&target));
        space_add(&target,parse_one(&arena,"(exec 0 (I) (O (+ A)))"));
        space_add(&target,parse_one(&arena,"(exec 1 (I) (O (+ B)))"));
    }
    Atom *report = replay_history_call(&space, &arena, parse_one(&arena, transaction?
        "(history:capture (quote (cost:run (search-policy (control FIFO "
        "(budget 0) (demand all) (retain True) (resources Transaction) "
        "(costs native-events-v1) (receipts True) "
        "(history (selections 512) (nodes 64) (bytes 32768) (owners 512)))) "
        "&replay-target)) (ReplayLimits 8 8192 536870912))" :
        "(history:capture (quote (cost:run (search-policy (control FIFO "
        "(budget 0) (demand all) (retain True) (costs native-events-v1) (receipts True) "
        "(history (selections 512) (nodes 64) (bytes 32768) (owners 512)))) "
        "(let $p (delay (superpose (2 3))) (Pair (force $p) (force $p))))) "
        "(ReplayLimits 8 8192 134217728))"));
    Atom *holder = prime_replay_capsule_handle(report);
    PrimeReplayCapsule *capsule = cetta_native_handle_get_owned(context, "prime-replay", holder);
    assert(capsule);
    CHECK(equation_match_cache_current()==ambient);
    bool foreign_retained=false;
    for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++)
        foreign_retained|=ambient->slots[i]==foreign_slot;
    CHECK(foreign_retained);
    if (damage == 5u || damage==6u) {
        CHECK(!capsule->capture_complete);
        CHECK(capsule->refusal && strcmp(capsule->refusal,
            damage==5u?"SourceView":"PendingInitialization") == 0);
        CHECK(!capsule->source_reserved && capsule->frame_count == 0u);
        CHECK(!br_service_effects_refusals);
        if(damage==6u) {
            CettaPrimeObservationReturn *created=capsule->created.prime_observation;
            CHECK(created && created->top.owned.status==CETTA_CONTINUATION_CAPACITY &&
                !created->top.owned.initialized && !created->top.owned.unpublished);
        }
    } else {
        assert(capsule->capture_complete && !capsule->refusal);
        PrimeNativeControl *source=prime_replay_original(capsule);
        CHECK(source && source->source_services &&
            &source->source_services->selectors!=ambient &&
            !atomic_load_explicit(&source->source_services->active,memory_order_acquire));
        PrimeNativeSourceServices *services=source->source_services;
        if(transaction) {
            CHECK(capsule->transaction && capsule->initial.target.complete &&
                capsule->initial.ambient_read && capsule->initial.ambient_read->matched &&
                !capsule->initial.ambient_services_captured && capsule->recording.service_only);
            PrimeNativeControl *auxiliary=prime_replay_checkpoint_control(context,&capsule->recording);
            assert(auxiliary && auxiliary->source_services!=services);
            SpaceReadToken unpublished_target=space_read_token(&target);
            PrimeNativeServiceScope writer={0};
            assert(prime_native_service_enter(services,&writer));
            uint64_t steps=source->steps;
            (void)replay_history_call(&space,&arena,atom_expr3(&arena,
                atom_symbol(&arena,"history:record"),holder,atom_int(&arena,1000)));
            CHECK(!capsule->frame_count && source->steps==steps && capsule->refusal &&
                !strcmp(capsule->refusal,"ChangedRecordingTargetOrService"));
            prime_native_service_leave(&writer);
            CHECK(equation_match_cache_current()==ambient && !services->active &&
                !auxiliary->source_services->active && space_length64(&target)==2u &&
                space_read_token_is_current(unpublished_target));
        }
        (void)replay_history_call(&space, &arena, atom_expr3(&arena,
            atom_symbol(&arena, "history:record"), holder, atom_int(&arena, 1000)));
        assert(capsule->frame_count == 1u && !capsule->refusal);
        PrimeNativeControl *post = prime_replay_checkpoint_control(context, &capsule->frames[0].post);
        PrimeNativeControl *original = prime_replay_original(capsule);
        assert(post && original && post->history.count > 1u);
        CHECK(original->source_services==services && equation_match_cache_current()==ambient);
        SpaceReadToken recorded_target=transaction?space_read_token(&target):(SpaceReadToken){0};
        if(transaction) {
            Atom *first=space_get_at(&target,0u),*second=space_get_at(&target,1u);
            CHECK(space_length64(&target)==2u &&
                ((atom_is_symbol(first,"A") && atom_is_symbol(second,"B")) ||
                 (atom_is_symbol(first,"B") && atom_is_symbol(second,"A"))));
        }
        size_t saved_count = post->history.count;
        uint64_t saved_omitted = post->history.omitted;
        size_t changed = saved_count - 1u;
        PrimeNativeHistorySelection saved_entry = post->history.entries[changed];
        uint64_t original_steps = original->steps;
        PrimeEvalStackFrame *original_frames = original->frames;
        PrimeNeedRecording *original_group = original->history.group;
        size_t original_history = original->history.count;
        if (corruption == 1u) post->history.count--; /* missing selection */
        if (corruption == 2u) { /* duplicate a physical occurrence, preserving the count */
            for (changed = 1u; changed < saved_count; changed++)
                if (post->history.entries[changed].occurrence != post->history.entries[0].occurrence) break;
            assert(changed < saved_count);
            saved_entry = post->history.entries[changed];
            post->history.entries[changed].occurrence = post->history.entries[0].occurrence;
        }
        if (corruption == 3u) post->history.omitted++; /* incomplete evidence */
        if (corruption == 4u) { /* erase a sharing association while keeping all events */
            bool sharing = false;
            for (;;) {
                assert(prime_need_recording_roots(&post->history.entries[changed].operation,
                    replay_history_sharing_root, &sharing));
                if (sharing || changed == 0u) break;
                changed--;
            }
            assert(sharing);
            saved_entry = post->history.entries[changed];
            PrimeNeedRecordingStatus before = prime_need_recording_status(post->history.group);
            post->history.entries[changed].operation = (PrimeNeedReceipt){0};
            PrimeNeedRecordingStatus after = prime_need_recording_status(post->history.group);
            CHECK(after.nodes == before.nodes && after.omitted == before.omitted &&
                  post->history.count == saved_count);
        }
        (void)replay_history_call(&space, &arena, atom_expr3(&arena,
            atom_symbol(&arena, "history:replay"), holder, atom_int(&arena, 2000000000)));
        CHECK(capsule->replayed_count == (corruption ? 0u : 1u));
        CHECK((capsule->accepted_attempt != NULL) == (corruption == 0u));
        CHECK((capsule->failed_attempt != NULL) == (corruption != 0u));
        CHECK(corruption ? capsule->refusal && strcmp(capsule->refusal, "ReplayPostMismatch") == 0
                     : capsule->refusal == NULL);
        CHECK(prime_replay_original(capsule) == original && capsule->source_reserved);
        CHECK(original->steps == original_steps && original->frames == original_frames);
        CHECK(original->history.group == original_group && original->history.count == original_history);
        post->history.count = saved_count; post->history.omitted = saved_omitted;
        post->history.entries[changed] = saved_entry;
        if(transaction)CHECK(space_read_token_is_current(recorded_target) &&
            original->source_services==services && !services->active &&
            equation_match_cache_current()==ambient);
    }
    eval_set_library_context(NULL); cetta_library_context_free(context);
    if(transaction)space_free(&target);
    registry_free(&registry); g_registry = saved_registry; space_free(&foreign);space_free(&space); free(context);
    term_universe_set_persistent_arena(&g_eval_fallback_universe, saved_persistent);
    arena_free(&arena); hashcons_free(&globals); g_hashcons = saved_globals;
    eval_set_library_context(saved_context);
}
#endif

/* The source owner retains its own world. A controller allowance is separate
 * from language fuel and cannot turn a pending root into successful emptiness. */
static void ordinary_source_cost_observe(void *raw, const CettaNativeCostChargeView *view) {
    (void)view;
    (*(size_t *)raw)++;
}

static CettaContinuationStatus ordinary_retry_pending_projection(EvalOutcome *outcome) {
    CettaPrimeObservationReturn *owned=outcome->prime_observation;
    assert(owned && owned->ordinary_returns.has_pending);
    CettaPrimeReturnSlice zero={.limit_preparation=true,.limit_transport=true};
    PrimeNativeControl *control=owned->ordinary_control;
    CettaOwnedReturn loan=owned->ordinary_returns.pending;
    Atom *projected=owned->ordinary_projected;
    uint64_t accepted=owned->ordinary_returns.accepted;
    uint64_t steps=control->steps,remaining=owned->ordinary_allowance;
    CettaCount count=outcome->results.len;
    CHECK(projected && loan.outcome.term==owned->ordinary_reified);
    CHECK(owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_PUBLICATION &&
        owned->ordinary_return_phase_index+1u==owned->ordinary_next);
    CHECK(loan.environment==&control->output.items[owned->ordinary_next-1u].env);
    CHECK(loan.outcome.result_form==control->output.items[accepted].result_form);
    CHECK(loan.outcome.delayed==control->output.items[accepted].delayed);
    CHECK(!owned->ordinary_refusal && accepted==count);
    uint64_t attempts=br_ordinary_slot_attempts;
    CHECK(cetta_owned_execution_sequence(&owned->ordinary_returns,0u,
        prime_ordinary_return_project,owned)==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(br_ordinary_slot_attempts==attempts && control->steps==steps);
    for(unsigned retry=0u;retry<2u;retry++) {
        CHECK(((owned->ordinary_copy_steps || owned->ordinary_preparation_steps)?
            eval_outcome_resume_observation_slice(outcome,0u,&zero,NULL):
            eval_outcome_resume_observation(outcome,0u))==CETTA_CONTINUATION_CAPACITY);
        CHECK(outcome->prime_observation==owned && owned->ordinary_returns.has_pending);
        CHECK(owned->ordinary_returns.pending.environment==loan.environment);
        CHECK(owned->ordinary_returns.pending.outcome.term==loan.outcome.term);
        CHECK(owned->ordinary_returns.pending.outcome.result_form==loan.outcome.result_form);
        CHECK(owned->ordinary_returns.pending.final==loan.final);
        CHECK(owned->ordinary_projected==projected && owned->ordinary_reified==loan.outcome.term);
        CHECK(owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_PUBLICATION &&
            owned->ordinary_return_phase_index+1u==owned->ordinary_next);
        CHECK(owned->ordinary_returns.accepted==accepted && outcome->results.len==count);
        CHECK(control->steps==steps && owned->ordinary_allowance==remaining);
    }
    CHECK(br_ordinary_slot_attempts==attempts+2u && br_ordinary_slot_refusals==0u);
    CettaContinuationStatus status=((owned->ordinary_copy_steps || owned->ordinary_preparation_steps)?
            eval_outcome_resume_observation_slice(outcome,0u,&zero,NULL):
            eval_outcome_resume_observation(outcome,0u));
    CHECK(status==CETTA_CONTINUATION_READY || status==CETTA_CONTINUATION_DEFERRED);
    CHECK(outcome->results.len>count && outcome->results.items[count]==projected);
    if(outcome->prime_observation) {
        CHECK(outcome->prime_observation==owned && owned->ordinary_returns.accepted>accepted);
        CHECK(control->steps-steps<=remaining);
        CHECK(owned->ordinary_allowance==remaining-(control->steps-steps));
    }
    return status;
}

static void ordinary_owned_root_case_transport_profile(const char *program, const char *query,
    const char *const *expected, size_t count, uint64_t quantum, bool completes,
    bool borrowed_cost, uint32_t workers, CettaPrimeSourceProfile profile,
    bool sliced_copy, uint64_t copy_quantum, bool sliced_preparation, uint64_t preparation_quantum) {
    Arena source, receiver;
    arena_init_detached(&source);arena_init_detached(&receiver);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    char worker_repr[32];snprintf(worker_repr,sizeof(worker_repr),"%u",workers);
    assert(cetta_eval_session_record_generic_setting(&context.session,"num-threads",
        CETTA_EVAL_OPTION_VALUE_INT,worker_repr,workers));
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&source,&space));
    Atom **forms=NULL;int form_count=parse_metta_text(program,&source,&forms);
    assert(form_count>=0);
    for(int i=0;i<form_count;i++)space_add(&space,forms[i]);
    free(forms);
    CettaPrimeSourceOwner *owner=NULL;
    CettaContinuationStatus captured=eval_prime_source_owner_capture_profile(&space,&registry,
        parse_one(&source,query),profile,100000u,8u*1024u*1024u,&owner);
    if(captured!=CETTA_CONTINUATION_READY)
        fprintf(stderr,"owned root capture %s: %u\n",query,(unsigned)captured);
    assert(captured==CETTA_CONTINUATION_READY && owner);
    CettaPrimeSourceScope *scope=NULL;
    assert(eval_prime_source_scope_open(owner,0u,&scope)==CETTA_CONTINUATION_READY);
    Space *held_space=NULL;Registry *held_registry=NULL;Arena *held_source=NULL;Atom *held_query=NULL;
    assert(eval_prime_source_scope_inputs(scope,&held_space,&held_registry,&held_source,&held_query));
    CHECK(eval_current_num_threads()==workers);
    EvalOutcome outcome;eval_outcome_init(&outcome);
    CettaNativeCostLedger ledger;CettaNativeCostScope cost_scope;
    CettaNativeCostObserver cost_observer={0};size_t notifications=0u;
    if(borrowed_cost) {
        cetta_native_cost_init(&ledger);
        cetta_native_cost_scope_enter(&cost_scope,&ledger,0u);
        assert(cetta_native_cost_observer_enter(&cost_observer,
            ordinary_source_cost_observe,&notifications));
    }
    eval_top_with_registry_outcome(held_space,&receiver,held_source,held_registry,
        held_query,&outcome,NULL,NULL);
    CettaPrimeObservationState state;
    if(borrowed_cost) {
        cetta_native_cost_observer_leave(&cost_observer);
        cetta_native_cost_scope_leave(&cost_scope);
        CHECK(ledger.total.work>0u && notifications>0u);
        cetta_native_cost_free(&ledger);
        CHECK(!outcome.prime_observation && outcome.completion==CETTA_EVAL_COMPLETE);
    } else {
        if(!outcome.prime_observation)
            fprintf(stderr,"owned root %s: completion %u, rows %u\n",query,
                (unsigned)outcome.completion,(unsigned)outcome.results.len);
        assert(outcome.prime_observation);
        CHECK(eval_outcome_observation_state(&outcome,&state));
        CHECK(state.initialized && !state.committed && !state.producer_complete);
        CHECK(outcome.results.len==0u && outcome.completion!=CETTA_EVAL_COMPLETE);
        CHECK(eval_outcome_zero_status(&outcome)==CETTA_EVAL_ZERO_PENDING);
        CHECK(outcome.prime_observation && outcome.prime_observation->ordinary_control->steps==0u);
        CHECK(outcome.prime_observation->ordinary_control->policy.workers==workers);
        CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_DEFERRED);
        CHECK(outcome.results.len==0u && outcome.prime_observation->ordinary_control->steps==0u);
    }
    eval_prime_source_scope_close(scope);eval_prime_source_owner_release(owner);
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);
    registry_free(&registry);space_free(&space);arena_free(&source);
    /* No original world, registry, caller arena or library remains. */
    if(!borrowed_cost) {
        size_t slices=0u;
        size_t slice_limit=completes?100000u:32u;
        bool retried=false,copy_paused=false,preparation_paused=false,validation_paused=false;
        assert(!br_ordinary_slot_refusals);
        if(count)br_ordinary_slot_refusals=3u;
        while(outcome.prime_observation && slices++<slice_limit) {
            uint64_t before=outcome.prime_observation->ordinary_control->steps;
            uint64_t paid_before=outcome.prime_observation->ordinary_allowance;
            uint64_t copied_before=outcome.prime_observation->ordinary_copy_steps;
            CettaPrimeReturnSlice limits={.preparation=preparation_quantum,
                .transport=copy_quantum,.limit_preparation=sliced_preparation,
                .limit_transport=sliced_copy};
            CettaPrimeReturnWork work={0};
            CettaContinuationStatus status=eval_outcome_resume_observation_slice(
                &outcome,quantum,&limits,&work);
            if (sliced_preparation) CHECK(work.preparation<=preparation_quantum);
            if (sliced_copy) CHECK(work.transport<=copy_quantum);
            if (sliced_preparation && outcome.prime_observation) {
                CettaPrimeObservationReturn *owned=outcome.prime_observation;
                CHECK(eval_outcome_observation_state(&outcome,&state));
                CHECK(state.preparation_steps==owned->ordinary_preparation_steps);
                if (state.preparation_pending) {
                    preparation_paused=true;
                    if (owned->ordinary_validation) {
                        validation_paused=true;
                        CHECK(owned->ordinary_projected && !owned->ordinary_validated &&
                            owned->ordinary_returns.has_pending &&
                            outcome.results.len==owned->ordinary_returns.accepted &&
                            owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_VALIDATION);
                    } else if (owned->ordinary_reification || owned->ordinary_observed)
                        CHECK(owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_REIFICATION);
                    else CHECK(owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_ENVIRONMENT);
                    CHECK(status==CETTA_CONTINUATION_DEFERRED &&
                        outcome.completion==CETTA_EVAL_INCOMPLETE_PAUSED);
                    uint64_t paid=owned->ordinary_control->steps;
                    uint64_t prepared=owned->ordinary_preparation_steps;
                    uint64_t copied=owned->ordinary_copy_steps;
                    uint64_t accepted=owned->ordinary_returns.accepted;
                    PrimeNeedReification *reification=owned->ordinary_reification;
                    PrimeOrdinaryGraphInspection *validation=owned->ordinary_validation;
                    Atom *observed=owned->ordinary_observed,*projected=owned->ordinary_projected;
                    PrimeOrdinaryReturnPhase phase=owned->ordinary_return_phase;
                    uint64_t phase_index=owned->ordinary_return_phase_index;
                    CettaPrimeReturnSlice zero={.limit_preparation=true,.limit_transport=true};
                    for (unsigned retry=0u;retry<2u;retry++) {
                        CettaPrimeReturnWork no_work={UINT64_MAX,UINT64_MAX};
                        CHECK(eval_outcome_resume_observation_slice(&outcome,0u,&zero,&no_work)
                            ==CETTA_CONTINUATION_DEFERRED);
                        CHECK(!no_work.preparation && !no_work.transport && outcome.prime_observation==owned);
                        CHECK(owned->ordinary_control->steps==paid &&
                            owned->ordinary_preparation_steps==prepared && owned->ordinary_copy_steps==copied &&
                            owned->ordinary_returns.accepted==accepted);
                        CHECK(owned->ordinary_reification==reification && owned->ordinary_validation==validation &&
                            owned->ordinary_observed==observed && owned->ordinary_projected==projected &&
                            owned->ordinary_return_phase==phase &&
                            owned->ordinary_return_phase_index==phase_index);
                    }
                }
            }
            if (sliced_copy && outcome.prime_observation) {
                CettaPrimeObservationReturn *owned=outcome.prime_observation;
                CHECK(owned->ordinary_copy_steps-copied_before<=copy_quantum);
                CHECK(eval_outcome_observation_state(&outcome,&state));
                CHECK(state.copy_traversal_steps==owned->ordinary_copy_steps);
                if (state.copy_pending) {
                    copy_paused=true;
                    CHECK(status==CETTA_CONTINUATION_DEFERRED);
                    CHECK(outcome.completion==CETTA_EVAL_INCOMPLETE_PAUSED);
                    CHECK(strcmp(eval_completion_reason(outcome.completion),"paused")==0);
                    CHECK(owned->ordinary_returns.has_pending && !owned->ordinary_projected &&
                        owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_TRANSPORT &&
                        owned->ordinary_return_phase_index+1u==owned->ordinary_next);
                    CettaOwnedReturn loan=owned->ordinary_returns.pending;
                    uint64_t controller=owned->ordinary_control->steps;
                    uint64_t accepted=owned->ordinary_returns.accepted;
                    uint64_t copied=owned->ordinary_copy_steps;
                    for (unsigned retry=0u;retry<2u;retry++) {
                        CHECK(eval_outcome_resume_observation_with_transport(&outcome,0u,0u,NULL)
                            ==CETTA_CONTINUATION_DEFERRED);
                        CHECK(outcome.prime_observation==owned);
                        CHECK(owned->ordinary_control->steps==controller);
                        CHECK(owned->ordinary_copy_steps==copied &&
                            owned->ordinary_returns.accepted==accepted);
                        CHECK(owned->ordinary_returns.pending.outcome.term==loan.outcome.term &&
                            owned->ordinary_returns.pending.environment==loan.environment &&
                            owned->ordinary_returns.pending.final==loan.final &&
                            owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_TRANSPORT &&
                            owned->ordinary_return_phase_index+1u==owned->ordinary_next);
                    }
                }
            }
            if(status==CETTA_CONTINUATION_CAPACITY && count && !retried) {
                status=ordinary_retry_pending_projection(&outcome);
                retried=true;
            }
            CHECK(status==CETTA_CONTINUATION_READY || status==CETTA_CONTINUATION_DEFERRED);
            if(status!=CETTA_CONTINUATION_READY && status!=CETTA_CONTINUATION_DEFERRED)break;
            if(outcome.prime_observation) {
                uint64_t spent=outcome.prime_observation->ordinary_control->steps-before;
                CHECK(spent<=paid_before+quantum);
                CHECK(outcome.prime_observation->ordinary_allowance==paid_before+quantum-spent);
                if(!sliced_copy) CHECK(spent<=quantum);
            }
        }
        if(br_ordinary_slot_refusals || retried!=(count!=0u))
            fprintf(stderr,"owned root publication %s: slices %zu, completion %u, rows %u, refusals %u\n",
                query,slices,(unsigned)outcome.completion,(unsigned)outcome.results.len,
                (unsigned)br_ordinary_slot_refusals);
        CHECK(!br_ordinary_slot_refusals && retried==(count!=0u));
        if(sliced_copy && count) CHECK(copy_paused);
        if(sliced_preparation && count) CHECK(preparation_paused);
        if(sliced_preparation && count) CHECK(validation_paused);
        if(completes)
            CHECK(!outcome.prime_observation && outcome.completion==CETTA_EVAL_COMPLETE);
        else {
            CHECK(outcome.prime_observation && outcome.completion!=CETTA_EVAL_COMPLETE);
            CHECK(eval_outcome_observation_state(&outcome,&state) && !state.producer_complete);
            CHECK(outcome.prime_observation->ordinary_control->steps==32u*quantum);
        }
    }
    CHECK(outcome.results.len==count);
    CHECK(eval_outcome_zero_status(&outcome)==(completes && count==0u
        ? CETTA_EVAL_ZERO_COMPLETE : count ? CETTA_EVAL_NONZERO : CETTA_EVAL_ZERO_PENDING));
    for(size_t i=0;i<count && i<outcome.results.len;i++)
        CHECK(atom_eq(outcome.results.items[i],parse_one(&receiver,expected[i])));
    eval_outcome_free(&outcome);arena_free(&receiver);
}

static void ordinary_owned_root_case_profile(const char *program, const char *query,
    const char *const *expected, size_t count, uint64_t quantum, bool completes,
    bool borrowed_cost, uint32_t workers, CettaPrimeSourceProfile profile) {
    ordinary_owned_root_case_transport_profile(program,query,expected,count,quantum,
        completes,borrowed_cost,workers,profile,false,0u,false,0u);
}

/* A pending publication cannot bypass the captured source or destination
 * identity, and abandoning it releases the same unaccepted loan. */
static void ordinary_owned_pending_boundary_case(unsigned damage,bool sliced_copy) {
    Arena source,receiver;arena_init_detached(&source);arena_init_detached(&receiver);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&source,&space));
    space_add(&space,parse_one(&source,"(= (retained-same $x) (Pair $x $x))"));
    CettaPrimeSourceOwner *owner=NULL;
    assert(eval_prime_source_owner_capture(&space,&registry,
        parse_one(&source,"(retained-same 7)"),100000u,8u*1024u*1024u,&owner)
        ==CETTA_CONTINUATION_READY);
    CettaPrimeSourceScope *scope=NULL;
    assert(eval_prime_source_scope_open(owner,0u,&scope)==CETTA_CONTINUATION_READY);
    Space *held_space=NULL;Registry *held_registry=NULL;Arena *held_source=NULL;Atom *held_query=NULL;
    assert(eval_prime_source_scope_inputs(scope,&held_space,&held_registry,&held_source,&held_query));
    EvalOutcome outcome;eval_outcome_init(&outcome);
    eval_top_with_registry_outcome(held_space,&receiver,held_source,held_registry,
        held_query,&outcome,NULL,NULL);
    assert(outcome.prime_observation);
    eval_prime_source_scope_close(scope);eval_prime_source_owner_release(owner);
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);space_free(&space);arena_free(&source);
    assert(!br_ordinary_slot_refusals);br_ordinary_slot_refusals=sliced_copy?0u:1u;
    CettaContinuationStatus status=CETTA_CONTINUATION_DEFERRED;
    for(unsigned slice=0u;slice<1000u && status==CETTA_CONTINUATION_DEFERRED;slice++) {
        status=sliced_copy?eval_outcome_resume_observation_with_transport(&outcome,1u,1u,NULL):
            eval_outcome_resume_observation(&outcome,1u);
        if(sliced_copy && outcome.prime_observation &&
            outcome.prime_observation->ordinary_copy_paused) break;
    }
    assert(status==(sliced_copy?CETTA_CONTINUATION_DEFERRED:CETTA_CONTINUATION_CAPACITY) &&
        outcome.prime_observation);
    CettaPrimeObservationReturn *owned=outcome.prime_observation;
    PrimeNativeControl *control=owned->ordinary_control;
    CettaOwnedReturn loan=owned->ordinary_returns.pending;
    uint64_t steps=control->steps;
    CHECK(!br_ordinary_slot_refusals && owned->ordinary_returns.has_pending);
    CHECK(loan.environment==&control->output.items[0].env && outcome.results.len==0u);
    CHECK(owned->ordinary_returns.accepted==0u);
    CHECK(sliced_copy?!owned->ordinary_projected:owned->ordinary_projected!=NULL);
    CHECK(owned->ordinary_return_phase==(sliced_copy?
        PRIME_ORDINARY_RETURN_TRANSPORT:PRIME_ORDINARY_RETURN_PUBLICATION));
    CHECK(!owned->ordinary_return_phase_index);
    if(sliced_copy) CHECK(atom_deep_copy_session_pending(owned->ordinary_copy));
    if(sliced_copy && damage>=5u) {
        /* The owner can record failure after issuing a valid prefix. A held
         * loan does not re-poll it, and the old cached loan says COMPLETE. */
        CettaEvalCompletion failure=damage==5u?CETTA_EVAL_INCOMPLETE_HOST_FAILURE:
            CETTA_EVAL_INCOMPLETE_EFFECT_DENIED;
        CHECK(owned->ordinary_returns.completion==CETTA_EVAL_COMPLETE);
        control->failed=true;control->completion=failure;
        uint64_t copied=owned->ordinary_copy_steps,used=1u;
        CHECK(eval_outcome_resume_observation_with_transport(&outcome,0u,0u,&used)
            ==CETTA_CONTINUATION_DEFERRED);
        CHECK(outcome.completion==failure && !used && !outcome.results.len);
        CHECK(outcome.prime_observation==owned && owned->ordinary_returns.has_pending);
        CHECK(control->steps==steps && owned->ordinary_copy_steps==copied &&
            owned->ordinary_returns.accepted==0u);
    }
    if(damage && damage<3u) {
        if(damage==1u) {
            PrimeNativeTopEpisode *captured=owned->top.episode->source_episode;
            space_add(captured->space,parse_one(captured->persistent,"(= (later-source) 9)"));
        } else result_set_add(&outcome.results,atom_symbol(&receiver,"Unrelated"));
        CettaContinuationStatus expected=damage==1u?CETTA_CONTINUATION_INVALIDATED:
            CETTA_CONTINUATION_UNSUPPORTED;
        CHECK(eval_outcome_resume_observation(&outcome,0u)==expected);
        CHECK(eval_outcome_resume_observation(&outcome,0u)==expected);
        CHECK(control->steps==steps && owned->ordinary_returns.accepted==0u);
        CHECK(owned->ordinary_returns.has_pending &&
            owned->ordinary_returns.pending.environment==loan.environment &&
            owned->ordinary_returns.pending.outcome.term==loan.outcome.term);
        CHECK(outcome.results.len==(damage==1u?0u:1u));
    }
    if(sliced_copy && (damage==0u || damage==4u)) {
        CHECK(owned->ordinary_allowance==0u);
        bool saw_copy=false;
        for(unsigned turn=0u;outcome.prime_observation && !outcome.results.len && turn<100u;turn++) {
            uint64_t used=UINT64_MAX;
            CettaContinuationStatus progress=damage==4u?
                eval_outcome_resume_observation(&outcome,0u):
                eval_outcome_resume_observation_with_transport(&outcome,0u,1u,&used);
            if(damage!=4u) {
                CHECK(used<=1u);
                saw_copy=saw_copy || used==1u;
            }
            CHECK(progress==CETTA_CONTINUATION_READY || progress==CETTA_CONTINUATION_DEFERRED);
            if(outcome.prime_observation) {
                CHECK(outcome.prime_observation==owned && control->steps==steps);
                if(!outcome.results.len) {
                    CHECK(owned->ordinary_returns.accepted==0u && owned->ordinary_returns.has_pending);
                    CHECK(owned->ordinary_returns.pending.outcome.term==loan.outcome.term &&
                        owned->ordinary_returns.pending.environment==loan.environment);
                }
            }
        }
        CHECK(damage==4u || saw_copy);
        CHECK(outcome.results.len==1u &&
            atom_eq(outcome.results.items[0],parse_one(&receiver,"(Pair 7 7)")));
        for(unsigned turn=0u;outcome.prime_observation && turn<100u;turn++) {
            CettaContinuationStatus finishing=eval_outcome_resume_observation(&outcome,1u);
            CHECK(finishing==CETTA_CONTINUATION_READY || finishing==CETTA_CONTINUATION_DEFERRED);
        }
        CHECK(!outcome.prime_observation && outcome.completion==CETTA_EVAL_COMPLETE);
        CHECK(outcome.results.len==1u);
    }
    eval_outcome_free(&outcome);
    CHECK(!g_library_context && !g_registry);
    arena_free(&receiver);
}

static void ordinary_owned_root_case(const char *program, const char *query,
    const char *const *expected, size_t count, uint64_t quantum, bool completes,
    bool borrowed_cost, uint32_t workers) {
    ordinary_owned_root_case_profile(program,query,expected,count,quantum,
        completes,borrowed_cost,workers,CETTA_PRIME_SOURCE_IMMUTABLE);
}

static Atom *retained_reification_wrap(Arena *arena,Atom *leaf) {
    Atom *result=atom_expr3(arena,atom_symbol(arena,"Pair"),leaf,leaf);
    for(unsigned depth=0u;depth<48u;depth++)
        result=atom_expr2(arena,atom_symbol(arena,"Wrap"),result);
    return result;
}

/* Each mode has an independently specified leaf. Partitioning only changes
 * traversal boundaries; it never forces the producer or changes its world. */
static uint64_t retained_reification_case(PrimeNeedReifyMode mode,uint64_t quantum) {
    Arena source,destination;arena_init_detached(&source);arena_init_detached(&destination);
    Bindings lexical,environment;bindings_init(&lexical);bindings_init(&environment);
    Atom *origin=parse_one(&source,"(Origin 7)"),*earned=parse_one(&source,"(Earned 99)");
    Atom *reference=NULL;
    assert(prime_need_allocate_source_argument_ref(&source,origin,
        prime_need_fresh_source_occurrence(),0u,&lexical,&environment,&reference));
    uint64_t cell=0u;assert(prime_need_ref_belongs_to(reference,bindings_need_view(&environment),&cell));
    uint64_t evaluator=prime_need_fresh_evaluator_id();PrimeNeedSnapshot running,completed;
    assert(prime_need_snapshot_start_evaluation(&source,bindings_need_view(&environment),cell,evaluator,&running));
    assert(prime_need_snapshot_resolve_ready_value(&source,&running,cell,evaluator,earned,&completed));
    *bindings_need_mut(&environment)=completed;
    Atom *root=retained_reification_wrap(&source,reference);
    Atom *leaf=mode==PRIME_NEED_REIFY_RETURN_VALUE || mode==PRIME_NEED_REIFY_PATTERN_QUERY?reference:
        mode==PRIME_NEED_REIFY_PERSISTENCE || mode==PRIME_NEED_REIFY_MATCH_VALUE ||
        mode==PRIME_NEED_REIFY_READY_DATA || mode==PRIME_NEED_REIFY_READY_CALL?earned:origin;
    Atom *expected=retained_reification_wrap(&source,leaf);
    PrimeNeedReification state;
    prime_need_reification_init(&state,&destination,root,bindings_need_view(&environment),
        mode,&environment,NULL,atom_var(&source,"wild"),NULL);
    uint64_t used=UINT64_MAX,total=0u;
    CHECK(prime_need_reification_run(&state,0u,&used)==CETTA_CONTINUATION_DEFERRED);
    CHECK(!used && !state.started && !state.len && !state.result);
    CettaContinuationStatus status=CETTA_CONTINUATION_DEFERRED;
    for(unsigned turn=0u;status==CETTA_CONTINUATION_DEFERRED && turn<10000u;turn++) {
        size_t len=state.len;PrimeNeedReifyFrame *frames=state.frames;
        CHECK(prime_need_reification_run(&state,0u,&used)==CETTA_CONTINUATION_DEFERRED);
        CHECK(!used && state.len==len && state.frames==frames);
        status=prime_need_reification_run(&state,quantum,&used);total+=used;
        CHECK(used<=quantum && prime_need_reification_current(&state));
    }
    CHECK(status==CETTA_CONTINUATION_READY && state.complete && !state.failed);
    CHECK(state.result && atom_eq(state.result,expected));
    if(mode!=PRIME_NEED_REIFY_PATTERN_QUERY) CHECK(state.frames!=state.inline_frames);
    PrimeNeedCellView final;assert(prime_need_snapshot_lookup(bindings_need_view(&environment),cell,&final));
    CHECK(final.cache_state==PRIME_NEED_CACHE_VALUE && final.cached==earned && final.origin==origin);
    prime_need_reification_destroy(&state);
    bindings_free(&environment);bindings_free(&lexical);arena_free(&destination);arena_free(&source);
    return total;
}

/* A changed service context refuses before touching its partially rebuilt
 * graph. Cancellation may destroy that cursor even after its arena reset. */
static void retained_reification_currency_case(unsigned damage) {
    Arena source,destination;arena_init_detached(&source);arena_init_detached(&destination);
    Bindings lexical,environment;bindings_init(&lexical);bindings_init(&environment);
    Atom *reference=NULL;
    assert(prime_need_allocate_source_argument_ref(&source,atom_int(&source,7),
        prime_need_fresh_source_occurrence(),0u,&lexical,&environment,&reference));
    PrimeNeedReification state;
    prime_need_reification_init(&state,&destination,retained_reification_wrap(&source,reference),
        bindings_need_view(&environment),PRIME_NEED_REIFY_OBSERVATION,&environment,NULL,NULL,NULL);
    uint64_t used=0u;
    CHECK(prime_need_reification_run(&state,120u,&used)==CETTA_CONTINUATION_DEFERRED && used==120u);
    CHECK(state.frames!=state.inline_frames && state.started && !state.complete);
    size_t len=state.len;PrimeNeedReifyFrame *frames=state.frames;
    SymbolTable symbols,*previous_symbols=g_symbols;
    VarInternTable variables,*previous_variables=g_var_intern;
    PrimeNeedSnapshot saved=*bindings_need_view(&environment);
    if(damage==0u) {
        uint64_t cell=0u;
        assert(prime_need_snapshot_allocate(&source,&saved,atom_int(&source,99),
            bindings_need_mut(&environment),&cell));
    } else if(damage==1u) {
        ArenaMark mark=arena_mark(&destination);arena_reset(&destination,mark);
    } else if(damage==2u) {
        symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    } else if(damage==3u) {
        var_intern_init(&variables);g_var_intern=&variables;
    }
    if(damage<4u) {
        CHECK(prime_need_reification_run(&state,7u,&used)==CETTA_CONTINUATION_INVALIDATED);
        CHECK(!used && state.len==len && state.frames==frames && !state.failed && !state.complete);
    }
    prime_need_reification_destroy(&state);
    g_symbols=previous_symbols;g_var_intern=previous_variables;
    if(damage==2u) {symbol_table_init_builtins(previous_symbols,&g_builtin_syms);symbol_table_free(&symbols);}
    if(damage==3u) var_intern_free(&variables);
    bindings_free(&environment);bindings_free(&lexical);arena_free(&destination);arena_free(&source);
}

static void retained_graph_inspection_case(uint64_t quantum,unsigned damage) {
    Arena arena;arena_init_detached(&arena);
    Space space;space_init(&space);
    Atom *leaf=atom_int(&arena,7),*root=atom_expr3(&arena,atom_symbol(&arena,"Pair"),leaf,leaf);
    if(damage==1u) leaf->structural_facts|=ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID;
    if(damage==2u) root->expr.elems[2]=atom_space(&arena,&space);
    if(damage==3u) {
        Atom *variable=atom_var(&arena,"protected-name");variable->name_key=atom_space(&arena,&space);
        root->expr.elems[2]=variable;
    }
    PrimeEvalStackSupportBuffer scan={0};CettaStorageCensus seen={0};PrimeOrdinaryGraphCursor cursor={0};
    prime_eval_stack_support_buffer_init(&scan);
    assert(prime_eval_stack_support_buffer_push(&scan,root));
    uint64_t used=UINT64_MAX,total=0u;
    g_prime_share_refusal=NULL;
    CettaContinuationStatus status=CETTA_CONTINUATION_DEFERRED;
    for(unsigned turn=0u;status==CETTA_CONTINUATION_DEFERRED && turn<100u;turn++) {
        PrimeOrdinaryGraphCursor before=cursor;size_t length=scan.len,visited=seen.length;
        CHECK(prime_ordinary_immutable_graph_run(&scan,&seen,&cursor,0u,&used)==CETTA_CONTINUATION_DEFERRED);
        CHECK(!used && cursor.position==before.position && cursor.child==before.child &&
            cursor.checked==before.checked && scan.len==length && seen.length==visited);
        status=prime_ordinary_immutable_graph_run(&scan,&seen,&cursor,quantum,&used);total+=used;
        CHECK(used<=quantum);
    }
    if(!damage) CHECK(status==CETTA_CONTINUATION_READY && total==7u && seen.length==3u && seen.repeated==1u);
    else CHECK(status==CETTA_CONTINUATION_UNSUPPORTED && g_prime_share_refusal);
    cetta_storage_census_free(&seen);prime_eval_stack_support_buffer_free(&scan);
    g_prime_share_refusal=NULL;space_free(&space);arena_free(&arena);
}

static void retained_return_preparation_cases(void) {
    for(unsigned mode=0u;mode<=PRIME_NEED_REIFY_PATTERN_QUERY;mode++)
        CHECK(retained_reification_case((PrimeNeedReifyMode)mode,1u)==
            retained_reification_case((PrimeNeedReifyMode)mode,7u));
    for(unsigned damage=0u;damage<5u;damage++)retained_reification_currency_case(damage);
    for(unsigned damage=0u;damage<4u;damage++) {
        retained_graph_inspection_case(1u,damage);retained_graph_inspection_case(7u,damage);
    }
}

static void retained_graph_currency_case(unsigned damage) {
    Arena arena;arena_init_detached(&arena);
    CettaPrimeObservationReturn owned={.output=&arena,.ordinary_preparation_limited=true,
        .ordinary_preparation_allowance=2u,.ordinary_projected=atom_expr2(&arena,
            atom_symbol(&arena,"Box"),atom_int(&arena,7))};
    CHECK(!prime_ordinary_validate_return(&owned) && owned.ordinary_preparation_paused &&
        owned.ordinary_validation && !owned.ordinary_validated && owned.ordinary_preparation_steps==2u);
    PrimeOrdinaryGraphInspection *inspection=owned.ordinary_validation;
    PrimeOrdinaryGraphCursor before=inspection->cursor;
    HashConsTable *hashcons=arena.hashcons;uint32_t older=arena.older_identity;
    bool scalar=arena.scalar_cache;
    if(damage==0u) arena_reset(&arena,arena_mark(&arena));
    else if(damage==1u) arena.older_identity++;
    else if(damage==2u) arena.hashcons=(HashConsTable *)(void *)&owned;
    else arena.scalar_cache=!arena.scalar_cache;
    owned.ordinary_preparation_allowance=7u;owned.ordinary_preparation_paused=false;
    CHECK(!prime_ordinary_validate_return(&owned) && owned.top.owned.status==CETTA_CONTINUATION_INVALIDATED);
    CHECK(owned.ordinary_validation==inspection && !owned.ordinary_validated &&
        owned.ordinary_preparation_steps==2u && inspection->cursor.position==before.position &&
        inspection->cursor.child==before.child && inspection->cursor.checked==before.checked);
    arena.hashcons=hashcons;arena.older_identity=older;arena.scalar_cache=scalar;
    prime_ordinary_graph_inspection_free(inspection);arena_free(&arena);
}

/* An unexportable next row must never reuse the preceding row's value. */
static void retained_preparation_failure_case(void) {
    Arena arena;arena_init_detached(&arena);
    Outcome row;outcome_init(&row);Bindings lexical;bindings_init(&lexical);
    Atom *reference=NULL;
    assert(prime_need_allocate_source_argument_ref(&arena,atom_int(&arena,7),
        prime_need_fresh_source_occurrence(),0u,&lexical,&row.env,&reference));
    Atom *cycle=atom_expr2(&arena,atom_symbol(&arena,"Cycle"),atom_int(&arena,0));
    cycle->expr.elems[1]=cycle;cycle->structural_facts=0u;row.atom=cycle;
    PrimeNativeControl *control=calloc(1u,sizeof(*control));assert(control);control->driver.arena=&arena;
    CettaPrimeObservationReturn owned={.ordinary_control=control,.ordinary_preparation_limited=true,
        .ordinary_preparation_allowance=100u,.ordinary_reified=atom_int(&arena,99)};
    for(unsigned retry=0u;retry<2u;retry++) {
        CHECK(prime_ordinary_prepare_return(&owned,&row)==CETTA_MATCH_INCOMPLETE);
        CHECK(!owned.ordinary_reified && !owned.ordinary_reification && owned.ordinary_observed==cycle &&
            !owned.ordinary_next && !owned.ordinary_preparation_paused && !control->steps);
    }
    free(control);outcome_free_fields(&row);bindings_free(&lexical);arena_free(&arena);
}

/* A successfully prepared row has an explicit phase before the provider
 * turns it into a consumer loan. */
static void ordinary_return_prepared_phase_case(void) {
    Arena arena;arena_init_detached(&arena);
    Outcome row;outcome_init(&row);row.atom=atom_int(&arena,7);
    PrimeNativeControl *control=calloc(1u,sizeof(*control));assert(control);
    control->driver.arena=&arena;
    CettaPrimeObservationReturn owned={.ordinary_control=control,.ordinary_next=3u};
    CHECK(prime_ordinary_prepare_return(&owned,&row)==CETTA_MATCH_MATCHED);
    CHECK(owned.ordinary_reified==row.atom && !owned.ordinary_observed &&
        !owned.ordinary_reification && owned.ordinary_return_phase==PRIME_ORDINARY_RETURN_PREPARED &&
        owned.ordinary_return_phase_index==3u);
    prime_ordinary_return_phase_clear(&owned);
    CHECK(owned.ordinary_return_phase==PRIME_ORDINARY_RETURN_NONE && !owned.ordinary_reified);
    free(control);outcome_free_fields(&row);arena_free(&arena);
}

/* A loan acknowledgement retires exactly the row that preceded the provider
 * cursor.  A mismatched row identity leaves the retained representation in
 * place and refuses the transition. */
static void ordinary_return_phase_identity_case(void) {
    Arena arena;arena_init_detached(&arena);
    Atom *value=atom_int(&arena,7);
    CettaPrimeObservationReturn owned={
        .ordinary_return_phase=PRIME_ORDINARY_RETURN_LOANED,
        .ordinary_return_phase_index=7u,
        .ordinary_next=9u,
        .ordinary_reified=value,
    };
    CHECK(!prime_ordinary_return_acknowledge_loan(&owned));
    CHECK(owned.ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
        owned.ordinary_return_phase_index==7u && owned.ordinary_reified==value &&
        owned.ordinary_refusal && !strcmp(owned.ordinary_refusal,"OrdinaryReturnPhaseIdentity"));
    owned.ordinary_refusal=NULL;owned.ordinary_next=8u;owned.ordinary_observed=atom_int(&arena,8);
    CHECK(!prime_ordinary_return_acknowledge_loan(&owned));
    CHECK(owned.ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
        owned.ordinary_return_phase_index==7u && owned.ordinary_reified==value &&
        owned.ordinary_observed && owned.ordinary_refusal &&
        !strcmp(owned.ordinary_refusal,"OrdinaryReturnPhaseIdentity"));
    owned.ordinary_refusal=NULL;owned.ordinary_observed=NULL;
    CHECK(prime_ordinary_return_acknowledge_loan(&owned));
    CHECK(owned.ordinary_return_phase==PRIME_ORDINARY_RETURN_NONE &&
        !owned.ordinary_return_phase_index && !owned.ordinary_reified && !owned.ordinary_refusal);
    arena_free(&arena);
}

static void ordinary_owned_root_cases(void) {
    const char *compound[]={"(Pair 7 (Tag 9))"};
    const char *duplicate[]={"(Pair 1 1)","(Pair 1 1)","(Pair 2 2)"};
    const char *empty_value[]={"()"};
    const char *empty_datum[]={"Empty"};
    const char *number[]={"42"};
    const char *program="(= (pick) 1) (= (pick) 1) (= (pick) 2) "
        "(= (same $x) (Pair $x $x))";
    for(uint32_t workers=1u;workers<=2u;workers++) {
        for(uint64_t quantum=1u;quantum<=7u;quantum+=6u) {
            ordinary_owned_root_case("","(Pair 7 (Tag 9))",compound,1u,quantum,true,false,workers);
            ordinary_owned_root_case(program,"(same (pick))",duplicate,3u,quantum,true,false,workers);
            ordinary_owned_root_case_transport_profile("","(Pair 7 (Tag 9))",compound,
                1u,quantum,true,false,workers,CETTA_PRIME_SOURCE_IMMUTABLE,true,1u,false,0u);
            ordinary_owned_root_case_transport_profile(program,"((lam x (Pair x x)) (pick))",
                duplicate,3u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,true,2u,false,0u);
            ordinary_owned_root_case_transport_profile("","(Pair 7 (Tag 9))",compound,
                1u,quantum,true,false,workers,CETTA_PRIME_SOURCE_IMMUTABLE,true,1u,true,quantum);
            ordinary_owned_root_case_transport_profile(program,"(same (pick))",duplicate,
                3u,quantum,true,false,workers,CETTA_PRIME_SOURCE_IMMUTABLE,true,2u,true,quantum);
            ordinary_owned_root_case_transport_profile(program,"((lam x (Pair x x)) (pick))",
                duplicate,3u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,true,2u,true,quantum);
            ordinary_owned_root_case("","(empty)",NULL,0u,quantum,true,false,workers);
            ordinary_owned_root_case("","()",empty_value,1u,quantum,true,false,workers);
            ordinary_owned_root_case("","Empty",empty_datum,1u,quantum,true,false,workers);
            ordinary_owned_root_case_profile(program,
                "((lam x (Pair x x)) (pick))",duplicate,3u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            ordinary_owned_root_case_profile(program,
                "(((lam x (lam y (Pair x x))) (pick)) ignored)",
                duplicate,3u,quantum,true,false,workers,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *nested[]={"(Two (Pair 1 1) (Pair 1 1))",
                "(Two (Pair 1 1) (Pair 1 1))","(Two (Pair 2 2) (Pair 2 2))"};
            ordinary_owned_root_case_profile(
                "(= (pick) 1) (= (pick) 1) (= (pick) 2) "
                "(= (extract (lam $v (Box $f))) $f)",
                "(let $f (extract ((lam x (lam y (Box (lam z (Pair x x))))) (pick))) "
                "(Two ($f left) ($f right)))",
                nested,3u,quantum,true,false,workers,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *quoted[]={"(Got (first 7 8))"};
            ordinary_owned_root_case_profile(
                "(= (first $x $y) $x) (= (id $x) $x) "
                "(= (forward-code (quote $e)) (Got (id $e)))",
                "(forward-code (quote (first 7 8)))",quoted,1u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *symbol[]={"(Got earned-symbol)"};
            ordinary_owned_root_case_profile(
                "(= earned-symbol 99) (= (id $x) $x) "
                "(= (forward-code (quote $e)) (Got (id $e)))",
                "(forward-code (quote earned-symbol))",symbol,1u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *fresh_program =
                "(seed) (= (owned-fresh:id $x) $x) "
                "(= (fresh) (match &self (seed) "
                "(let $f ((lam y (lam z (Pair y y))) (owned-fresh:id $x)) "
                "(unify $x A $f failed))))";
            const char *fresh[]={"(Pair A A)"};
            ordinary_owned_root_case_profile(fresh_program,
                "((lam f (f ignored)) (fresh))",fresh,1u,quantum,true,false,workers,
                CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *caller[]={"(Out B (Pair A A))"};
            ordinary_owned_root_case_profile(fresh_program,
                "(let $x B (let $s (delay (fresh)) "
                "(let $f (force $s) (Out $x ($f ignored)))))",
                caller,1u,quantum,true,false,workers,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
            const char *cached[]={"(Two (Pair A A) (Pair A A))"};
            ordinary_owned_root_case_profile(fresh_program,
                "(let $s (delay (fresh)) "
                "(let $f (force $s) (Two ($f left) ((force $s) right))))",
                cached,1u,quantum,true,false,workers,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD);
        }
        ordinary_owned_root_case("(= (owned-root-add) (+ 41 1))",
            "(owned-root-add)",number,1u,1u,true,false,workers);
        ordinary_owned_root_case("(= (owned-root-add) (+ 41 1))",
            "(owned-root-add)",number,1u,1u,true,true,workers);
        ordinary_owned_root_case(
            "(= (owned-growing-loop $n) (owned-growing-loop (+ $n 1)))",
            "(owned-growing-loop 0)",NULL,0u,1u,false,false,workers);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats before,after;
    bool was_enabled=cetta_runtime_stats_is_enabled();
    cetta_runtime_stats_enable();
    cetta_runtime_stats_snapshot(&before);
    const char *independent[]={"7","7"};
    ordinary_owned_root_case(
        "(= (owned-independent) (owned-count 15)) "
        "(= (owned-independent) (owned-count 16)) "
        "(= (owned-count $n) (if (== $n 0) 7 (owned-count (- $n 1))))",
        "(owned-independent)",independent,2u,64u,true,false,2u);
    cetta_runtime_stats_snapshot(&after);
    printf("Owned root worker tasks: %"PRIu64"\n",
        after.counters[CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK]-
        before.counters[CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK]);
    CHECK(after.counters[CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK] >
        before.counters[CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK]);
    if(!was_enabled)cetta_runtime_stats_disable();
#endif
}

/* Exercise the common return protocol with the actual retained evaluator,
 * after retiring every original source/service and publication arena. */
static CettaPrimeSourceOwner *ordinary_source_driver_open_profile(const char *program,
    const char *query,uint64_t quantum,uint32_t workers,CettaPrimeSourceProfile profile,
    CettaOwnedExecution *execution) {
    Arena arena;arena_init_detached(&arena);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    char repr[32];snprintf(repr,sizeof(repr),"%u",workers);
    assert(cetta_eval_session_record_generic_setting(&context.session,"num-threads",
        CETTA_EVAL_OPTION_VALUE_INT,repr,workers));
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    Atom **forms=NULL;int count=parse_metta_text(program,&arena,&forms);
    assert(count>=0);
    for(int i=0;i<count;i++)space_add(&space,forms[i]);
    free(forms);
    CettaPrimeSourceOwner *owner=NULL;
    CettaContinuationStatus status=eval_prime_source_owner_capture_profile(&space,&registry,parse_one(&arena,query),
        profile,100000u,8u*1024u*1024u,&owner);
    if(status!=CETTA_CONTINUATION_READY)fprintf(stderr,"source driver capture %s: %u\n",query,(unsigned)status);
    assert(status==CETTA_CONTINUATION_READY);
    CHECK(eval_prime_source_owner_is_current(owner));
    CHECK(eval_prime_source_execution_open(owner,0u,execution)==CETTA_CONTINUATION_UNSUPPORTED);
    CHECK(eval_prime_source_execution_open(owner,UINT64_MAX,execution)==CETTA_CONTINUATION_UNSUPPORTED);
    assert(eval_prime_source_execution_open(owner,quantum,execution)==CETTA_CONTINUATION_READY);
    CettaPrimeObservationReturn *owned=execution->computation;
    CHECK(execution->relocatable && !execution->has_pending && !execution->accepted);
    CHECK(!owned->ordinary_control->steps && !owned->ordinary_next &&
        !owned->output && !owned->top.destination);
    CHECK(owned->ordinary_control->policy.workers==workers);
    CHECK(owned->top.episode->source_episode==owner);
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);
    space_free(&space);arena_free(&arena);
    CHECK(eval_prime_source_owner_is_current(owner));
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
    return owner;
}

static CettaPrimeSourceOwner *ordinary_source_driver_open(const char *program,
    const char *query,uint64_t quantum,uint32_t workers,CettaOwnedExecution *execution) {
    return ordinary_source_driver_open_profile(program,query,quantum,workers,
        CETTA_PRIME_SOURCE_IMMUTABLE,execution);
}

static void ordinary_raw_preparation_refusal_case(unsigned preparation,bool cancel,bool owner_failed) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open(
        preparation?"(= (raw-preparation $x) (Pair $x $x))":
            "(= (raw-preparation $x) (Pair (quote $x) $x))",
        "(raw-preparation 7)",1u,1u,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeControl *control=owned->ordinary_control;
    PrimeOrdinaryReturnScope scope={0};
    CHECK(prime_ordinary_return_enter(owned,&scope));
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE,
        .parent=g_eval_completion_tracker};
    g_eval_completion_tracker=&tracker;
    PrimeEvalTopGuard top;prime_eval_top_enter(&top);g_metta_eval_depth++;
    for(unsigned slice=0u;slice<10000u && !control->published;slice++)
        prime_native_control_run(control,1u,prime_ordinary_stop_after_row,NULL);
    g_metta_eval_depth--;prime_eval_top_leave(&top);
    g_eval_completion_tracker=tracker.parent;
    prime_ordinary_return_leave(owned,&scope);
    assert(control->published==1u && control->output.len==1u &&
        !owned->ordinary_next && !execution.has_pending && !execution.accepted);
    Outcome *row=&control->output.items[0];
    /* The real producer has normalized this result already. Expose that
     * same value through an equivalent bound carrier to inject a preparation
     * refusal at the raw-row boundary, rather than during source execution. */
    Atom *expected=row->atom;
    Atom *alias=atom_var(control->driver.arena,"raw-preparation-result");
    CHECK(bindings_add_var(&row->env,alias,expected));
    row->atom=alias;
    row->materialized_atom=NULL;
    Atom *raw=row->atom;
    AnswerBank bank;answer_bank_init(&bank);CettaVarMap map;cetta_var_map_init(&map);
    if(preparation) {
        Atom *canonical=atom_var(control->driver.arena,"raw-preparation-canonical");
        Bindings cached;bindings_init(&cached);AnswerRef ref;
        CHECK(bindings_add_var(&cached,canonical,expected));
        CHECK(answer_bank_add(&bank,canonical,&cached,NULL,&ref));
        CHECK(cetta_var_map_add(&map,canonical->var_id,alias));
        Outcome reference;
        CHECK(outcome_init_answer_ref(control->driver.arena,&reference,&bank,ref,&map,&row->env));
        outcome_free_fields(row);outcome_move(row,&reference);outcome_free_fields(&reference);
        bindings_free(&cached);
    }
    uint64_t paid=control->steps;
    if(owner_failed) {
        /* Fault injection after a real earned row, independently of whether
         * today's scheduler can record a later failure at this exact cut. */
        control->failed=true;control->completion=CETTA_EVAL_INCOMPLETE_EFFECT_DENIED;
    }
    CettaCallOutcome returned=cetta_call_failure();
    for(unsigned retry=0u;retry<3u;retry++) {
        if(preparation==1u)br_preparation_materialize_refusals=1u;
        else if(preparation==2u) {
            br_preparation_merge_skip=1u;br_preparation_merge_refusals=1u;
        } else br_env_refusals=1u;
        CHECK(cetta_owned_execution_sequence(&execution,1u,br_owned_return,&returned)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(!br_env_refusals && !execution.terminal && !execution.has_pending &&
            !execution.accepted && !owned->ordinary_next);
        CHECK(execution.completion==(owner_failed?CETTA_EVAL_INCOMPLETE_EFFECT_DENIED:
            CETTA_EVAL_INCOMPLETE_HOST_FAILURE));
        CHECK(control->steps==paid && control->published==1u &&
            (preparation?row->kind==CETTA_OUTCOME_ANSWER_REF:row->atom==raw) &&
            !row->materialized_atom && bindings_has_bound_values(&row->env));
        CHECK(!br_preparation_materialize_refusals && !br_preparation_merge_refusals &&
            !br_preparation_merge_skip);
        CHECK(returned.kind==CETTA_CALL_FAILURE && !owned->ordinary_refusal);
    }
    if(!cancel) {
        CettaOwnedExecutionStep step=cetta_owned_execution_sequence(
            &execution,1u,br_owned_return,&returned);
        CHECK(step==CETTA_OWNED_EXECUTION_PAUSED || step==CETTA_OWNED_EXECUTION_COMPLETE);
        CHECK(execution.accepted==1u && owned->ordinary_next==1u &&
            returned.kind==CETTA_CALL_VALUE && !atom_is_error(returned.term));
        CHECK(atom_eq(returned.term,expected));
        CHECK(control->steps==paid && row->materialized_atom);
        if(owner_failed)CHECK(execution.completion==CETTA_EVAL_INCOMPLETE_EFFECT_DENIED);
    }
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    cetta_var_map_free(&map);answer_bank_free(&bank);
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return && !br_env_refusals);
}

/* The ordinary admission excludes active table mode. This replaces a real
 * earned row by a cached carrier at its representation boundary; it does not
 * claim admission of arbitrary tabled programs to the ordinary provider. */
static void ordinary_cached_disposition_case(unsigned refusal,bool cancel) {
    Arena source,receiver;arena_init_detached(&source);arena_init_detached(&receiver);
    Space space;space_init(&space);Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&source,&space));
    space_add(&space,parse_one(&source,"(= (cached-disposition) A)"));
    space_add(&space,parse_one(&source,"(= (cached-disposition) B)"));
    CettaPrimeSourceOwner *owner=NULL;
    CHECK(eval_prime_source_owner_capture(&space,&registry,
        parse_one(&source,"(cached-disposition)"),100000u,8u*1024u*1024u,&owner)
        ==CETTA_CONTINUATION_READY);
    CettaPrimeSourceScope *source_scope=NULL;
    CHECK(eval_prime_source_scope_open(owner,0u,&source_scope)==CETTA_CONTINUATION_READY);
    Space *held_space=NULL;Registry *held_registry=NULL;Arena *held_source=NULL;Atom *held_query=NULL;
    CHECK(eval_prime_source_scope_inputs(source_scope,&held_space,&held_registry,&held_source,&held_query));
    EvalOutcome outcome;eval_outcome_init(&outcome);
    eval_top_with_registry_outcome(held_space,&receiver,held_source,held_registry,
        held_query,&outcome,NULL,NULL);
    CettaPrimeObservationReturn *owned=outcome.prime_observation;CHECK(owned);
    assert(owned);PrimeNativeControl *control=owned->ordinary_control;
    PrimeOrdinaryReturnScope run_scope={0};CHECK(prime_ordinary_return_enter(owned,&run_scope));
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE,.parent=g_eval_completion_tracker};
    g_eval_completion_tracker=&tracker;
    PrimeEvalTopGuard top;prime_eval_top_enter(&top);g_metta_eval_depth++;
    for(unsigned cut=0u;cut<10000u && !prime_native_control_closed(control);cut++)
        prime_native_control_run(control,32u,prime_ordinary_stop_after_row,NULL);
    g_metta_eval_depth--;prime_eval_top_leave(&top);
    g_eval_completion_tracker=tracker.parent;prime_ordinary_return_leave(owned,&run_scope);
    CHECK(prime_native_control_closed(control) && control->published==2u && !owned->ordinary_next);
    assert(control->output.len==2u);
    Outcome *row=&control->output.items[0];
    AnswerBank bank;answer_bank_init(&bank);CettaVarMap map;cetta_var_map_init(&map);
    Atom *alias=atom_var(control->driver.arena,"cached-disposition-alias");
    Atom *canonical=atom_var(control->driver.arena,"cached-disposition-canonical");
    CHECK(bindings_add_var(&row->env,alias,row->atom));
    Bindings cached;bindings_init(&cached);AnswerRef ref;
    CHECK(bindings_add_var(&cached,canonical,atom_symbol(control->driver.arena,"Incompatible")));
    CHECK(answer_bank_add(&bank,canonical,&cached,NULL,&ref));
    CHECK(cetta_var_map_add(&map,canonical->var_id,alias));
    Outcome replacement;CHECK(outcome_init_answer_ref(control->driver.arena,&replacement,
        &bank,ref,&map,&row->env));
    outcome_free_fields(row);outcome_move(row,&replacement);outcome_free_fields(&replacement);
    bindings_free(&cached);
    eval_prime_source_scope_close(source_scope);eval_prime_source_owner_release(owner);
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);space_free(&space);arena_free(&source);
    uint64_t paid=control->steps;
    if(refusal) for(unsigned retry=0u;retry<3u;retry++) {
        if(refusal==1u)br_preparation_materialize_refusals=1u;
        else {br_preparation_merge_skip=1u;br_preparation_merge_refusals=1u;}
        CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_DEFERRED);
        CHECK(outcome.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && !outcome.results.len &&
            !owned->ordinary_next && !owned->ordinary_returns.accepted &&
            row->kind==CETTA_OUTCOME_ANSWER_REF && control->steps==paid &&
            !br_preparation_materialize_refusals && !br_preparation_merge_refusals);
    }
    br_ordinary_slot_refusals=1u;
    CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_CAPACITY);
    CettaPrimeObservationState state;CHECK(eval_outcome_observation_state(&outcome,&state));
    CHECK(owned->ordinary_next==2u && !owned->ordinary_returns.accepted &&
        owned->ordinary_returns.has_pending && state.pending==1u && !outcome.results.len &&
        owned->ordinary_projected && !owned->ordinary_refusal && control->steps==paid);
    CHECK(owned->ordinary_returns.pending.environment==&control->output.items[1].env);
    if(!cancel) {
        CettaNativeCostLedger delivery;cetta_native_cost_init_compact(&delivery);
        CettaNativeCostScope costs;cetta_native_cost_scope_enter(&costs,&delivery,0u);
        CHECK(eval_outcome_resume_observation(&outcome,0u)==CETTA_CONTINUATION_READY);
        cetta_native_cost_scope_leave(&costs);
        /* Completion retires the private owner. Observe the surviving public
         * result and the actual delivery account, rather than that owner. */
        CHECK(outcome.completion==CETTA_EVAL_COMPLETE && outcome.results.len==1u &&
            atom_is_symbol(outcome.results.items[0],"B") && !outcome.prime_observation);
        CHECK(!delivery.total.counts[CETTA_COST_QUANTUM] &&
            !delivery.total.counts[CETTA_COST_EQUATION_ACTIVATION]);
        cetta_native_cost_free(&delivery);
    }
    eval_outcome_free(&outcome);cetta_var_map_free(&map);answer_bank_free(&bank);arena_free(&receiver);
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
}

typedef struct {
    CettaPrimeSourceOwner *owner;
    unsigned refusals;
    uint64_t captures;
    const CettaOwnedReturnImagePolicy *policy;
} OrdinarySourceSharing;
static bool ordinary_source_compatible(void *raw) {
    OrdinarySourceSharing *sharing=raw;
    return eval_prime_source_owner_is_current(sharing->owner);
}
static bool ordinary_source_capture(void *raw,const CettaOwnedReturn *returned,
    CettaSharedReturnRow *row) {
    OrdinarySourceSharing *sharing=raw;sharing->captures++;
    if(sharing->refusals) {sharing->refusals--;return false;}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(!sharing->policy)
        CHECK(!returned->environment || !bindings_receipt_view(returned->environment)->recording);
#endif
    return cetta_shared_return_capture_image((void *)sharing->policy,returned,row);
}
typedef struct {
    CettaOwnedReturnImage *images[8];
    size_t count;
    bool refuse;
    const CettaOwnedReturnImagePolicy *policy;
} OrdinarySourceDelivery;
static bool ordinary_source_publish(void *raw,uint64_t position,const CettaOwnedReturn *returned) {
    OrdinarySourceDelivery *delivery=raw;
    if(delivery->refuse)return false;
    CHECK(position==delivery->count && delivery->count<8u);
    if(position!=delivery->count || delivery->count>=8u)return false;
    CettaOwnedReturnImage *image=cetta_owned_return_image_from_view(returned);
    if(!cetta_owned_return_image_retain(image))return false;
    delivery->images[delivery->count++]=image;return true;
}
static bool ordinary_source_collect(void *raw,const CettaOwnedReturn *returned) {
    OrdinarySourceDelivery *delivery=raw;
    if(delivery->refuse)return false;
    assert(delivery->count<8u);
    CettaOwnedReturnImage *image=cetta_owned_return_image_capture(returned,delivery->policy);
    if(!image)return false;
    delivery->images[delivery->count++]=image;return true;
}
static void ordinary_source_delivery_check(OrdinarySourceDelivery *delivery,
    const char *const *expected,size_t count) {
    CHECK(delivery->count==count);
    Arena arena;arena_init_detached(&arena);
    for(size_t i=0u;i<delivery->count;i++) {
        const CettaOwnedReturn *returned=cetta_owned_return_image_view(delivery->images[i]);
        CHECK(i<count && atom_eq(returned->outcome.term,parse_one(&arena,expected[i])));
        CHECK(returned->outcome.kind==CETTA_CALL_VALUE);
        CHECK(returned->environment);
        cetta_owned_return_image_release(delivery->images[i]);
    }
    arena_free(&arena);
}
static void ordinary_source_pending(CettaOwnedExecution *execution,uint64_t quantum) {
    CettaPrimeObservationReturn *owned=execution->computation;
    uint64_t accepted=execution->accepted;
    OrdinarySourceDelivery refused={.refuse=true};
    uint64_t before=owned->ordinary_control->steps;
    CHECK(cetta_owned_execution_sequence(execution,0u,ordinary_source_collect,&refused)
        ==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(owned->ordinary_control->steps==before);
    for(unsigned i=0u;i<10000u && !execution->has_pending;i++) {
        before=owned->ordinary_control->steps;
        CHECK(cetta_owned_execution_sequence(execution,1u,ordinary_source_collect,&refused)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps-before<=quantum);
    }
    assert(execution->has_pending);
    CHECK(execution->accepted==accepted && owned->ordinary_next==accepted+1u);
}

/* A full-world value needs one raw-row conversion, not source reification.
 * A zero preparation grant parks before that conversion; movement and refused
 * delivery retain the actual runtime value, world and paid producer prefix. */
static void ordinary_source_preparation_case(uint32_t workers,bool cancel) {
    CettaOwnedExecution execution={0},moved={0};
    CettaPrimeSourceOwner *source=ordinary_source_driver_open(
        "(= (prep-pick) 1) (= (prep-pick) 1) (= (prep-pick) 2) "
        "(= (prep-same $x) (Pair $x $x))","(prep-same (prep-pick))",3u,workers,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    OrdinarySourceDelivery delivery={.refuse=true};
    CettaPrimeReturnSlice zero={.limit_preparation=true},one={.preparation=1u,.limit_preparation=true};
    CettaPrimeReturnSlice transport={.transport=1u,.limit_transport=true};
    CettaPrimeReturnWork work={0};
    CHECK(!eval_prime_source_execution_set_return_slice(&execution,&transport));
    CHECK(eval_prime_source_execution_set_return_slice(&execution,&zero));
    CHECK(eval_prime_source_execution_return_work(&execution,&work) && !work.preparation && !work.transport);
    for(unsigned turn=0u;!owned->ordinary_control->published && turn<10000u;turn++)
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(owned->ordinary_control->published && !execution.has_pending && !execution.accepted &&
        owned->ordinary_preparation_paused && !owned->ordinary_next && !owned->ordinary_reified &&
        owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_ENVIRONMENT &&
        !owned->ordinary_return_phase_index);
    uint64_t paid=owned->ordinary_control->steps;
    CHECK(eval_prime_source_execution_set_quantum(&execution,0u));
    for(unsigned retry=0u;retry<2u;retry++) {
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps==paid && !owned->ordinary_next && !owned->ordinary_preparation_steps);
    }
    CHECK(eval_prime_source_execution_set_return_slice(&execution,&one));
    CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
        ==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(!owned->ordinary_preparation_paused && !owned->ordinary_observed && !owned->ordinary_reification &&
        owned->ordinary_preparation_steps==1u && owned->ordinary_control->steps==paid &&
        execution.has_pending && owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
        !owned->ordinary_return_phase_index);
    CettaOwnedReturn prepared=execution.pending;
    CHECK(cetta_owned_execution_move(&moved,&execution));cetta_owned_execution_cancel(&execution);
    CHECK(!eval_prime_source_execution_set_return_slice(&execution,&one));
    CHECK(eval_prime_source_execution_set_return_slice(&moved,&zero));
    for(unsigned retry=0u;retry<2u;retry++) {
        CHECK(cetta_owned_execution_sequence(&moved,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(moved.has_pending && moved.pending.outcome.term==prepared.outcome.term &&
            moved.pending.environment==prepared.environment && !owned->ordinary_reification &&
            owned->ordinary_preparation_steps==1u && owned->ordinary_control->steps==paid &&
            owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
            !owned->ordinary_return_phase_index);
    }
    if(!cancel) {
        CHECK(eval_prime_source_execution_set_return_slice(&moved,&one));
        CHECK(moved.has_pending && !moved.accepted && owned->ordinary_next==1u && !owned->ordinary_reification &&
            owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
            !owned->ordinary_return_phase_index);
        CettaOwnedReturn loan=moved.pending;
        CHECK(eval_prime_source_execution_return_work(&moved,&work) && work.preparation==1u && !work.transport);
        CHECK(eval_prime_source_execution_set_return_slice(&moved,&zero));
        for(unsigned retry=0u;retry<2u;retry++) {
            CHECK(cetta_owned_execution_sequence(&moved,1u,ordinary_source_collect,&delivery)
                ==CETTA_OWNED_EXECUTION_PAUSED);
            CHECK(moved.pending.outcome.term==loan.outcome.term && moved.pending.environment==loan.environment &&
                owned->ordinary_preparation_steps==work.preparation && owned->ordinary_control->steps==paid &&
                owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
                !owned->ordinary_return_phase_index);
        }
        CHECK(eval_prime_source_execution_set_return_slice(&moved,NULL));
        CHECK(eval_prime_source_execution_set_quantum(&moved,3u));delivery.refuse=false;
        for(unsigned turn=0u;!moved.terminal && turn<10000u;turn++)
            cetta_owned_execution_sequence(&moved,1u,ordinary_source_collect,&delivery);
        CHECK(moved.terminal && moved.stop==CETTA_OWNED_EXECUTION_COMPLETE && moved.accepted==3u);
        CHECK(!eval_prime_source_execution_set_return_slice(&moved,&one));
    }
    cetta_owned_execution_cancel(&moved);eval_prime_source_owner_release(source);
    const char *expected[]={"(Pair 1 1)","(Pair 1 1)","(Pair 2 2)"};
    ordinary_source_delivery_check(&delivery,cancel?NULL:expected,cancel?0u:3u);
}

/* An earned prefix is independent of permission to prepare its successor.
 * A stale preceding payload is not a readiness verdict for the next row. */
static void ordinary_source_preparation_after_prefix_case(uint32_t workers,bool cancel) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *source=ordinary_source_driver_open(
        "(= (prefix-pick) 1) (= (prefix-pick) 2) "
        "(= (prefix-pair $x) (Pair $x $x))","(prefix-pair (prefix-pick))",7u,workers,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    OrdinarySourceDelivery delivery={0};
    for(unsigned turn=0u;!execution.accepted && turn<10000u;turn++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    CHECK(execution.accepted==1u && delivery.count==1u && !execution.terminal && owned->ordinary_next==1u);
    Atom *preceding=owned->ordinary_reified;
    CHECK(preceding && owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
        !owned->ordinary_return_phase_index);
    CettaPrimeReturnSlice zero={.limit_preparation=true},one={.limit_preparation=true,.preparation=1u};
    CHECK(eval_prime_source_execution_set_return_slice(&execution,&zero));
    for(unsigned turn=0u;owned->ordinary_control->published<2u && turn<10000u;turn++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    CHECK(owned->ordinary_control->published>=2u && owned->ordinary_preparation_paused);
    CHECK(execution.accepted==1u && delivery.count==1u && owned->ordinary_next==1u && !execution.has_pending);
    CHECK(!owned->ordinary_reified && owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_ENVIRONMENT &&
        owned->ordinary_return_phase_index==1u);
    uint64_t paid=owned->ordinary_control->steps;
    CHECK(eval_prime_source_execution_set_quantum(&execution,0u));
    for(unsigned retry=0u;retry<3u;retry++) {
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps==paid && !owned->ordinary_preparation_steps &&
            owned->ordinary_next==1u && execution.accepted==1u && !execution.has_pending && delivery.count==1u &&
            !owned->ordinary_reified && owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_ENVIRONMENT &&
            owned->ordinary_return_phase_index==1u);
    }
    if(!cancel) {
        delivery.refuse=true;
        CHECK(eval_prime_source_execution_set_return_slice(&execution,&one));
        for(unsigned turn=0u;!execution.has_pending && turn<10000u;turn++)
            CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
                ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(execution.has_pending && execution.accepted==1u && delivery.count==1u &&
            owned->ordinary_next==2u && owned->ordinary_control->steps==paid &&
            owned->ordinary_return_phase==PRIME_ORDINARY_RETURN_LOANED &&
            owned->ordinary_return_phase_index==1u);
        CHECK(execution.pending.environment==&owned->ordinary_control->output.items[1].env &&
            execution.pending.outcome.result_form==owned->ordinary_control->output.items[1].result_form &&
            execution.pending.outcome.delayed==owned->ordinary_control->output.items[1].delayed);
        Atom *value=execution.pending.outcome.term;
        CHECK(value && value!=preceding && value->kind==ATOM_EXPR && value->expr.len==3u &&
            value->expr.elems[1]->kind==ATOM_GROUNDED && value->expr.elems[1]->ground.ival==2 &&
            value->expr.elems[2]->kind==ATOM_GROUNDED && value->expr.elems[2]->ground.ival==2);
        uint64_t prepared=owned->ordinary_preparation_steps;
        CHECK(prepared==1u && eval_prime_source_execution_set_return_slice(&execution,&zero));
        delivery.refuse=false;
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(execution.accepted==2u && delivery.count==2u && owned->ordinary_control->steps==paid &&
            owned->ordinary_preparation_steps==prepared);
        CHECK(eval_prime_source_execution_set_quantum(&execution,7u));
        for(unsigned turn=0u;!execution.terminal && turn<10000u;turn++)
            cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE && execution.accepted==2u);
    }
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(source);
    const char *expected[]={"(Pair 1 1)","(Pair 2 2)"};
    ordinary_source_delivery_check(&delivery,expected,cancel?1u:2u);
}

/* Factors come from actual equation activations, including failed branches;
 * neither this client nor the image adapter manufactures a contribution. */
static const char *ordinary_weighted_program=
    "(=w (weighted-outer) (weighted-choice) 2) "
    "(=w (weighted-choice) (empty) 11) "
    "(=w (weighted-choice) V 3) (=w (weighted-choice) V 5)";
static void ordinary_source_factors(OrdinarySourceDelivery *delivery) {
    CHECK(delivery->count==2u);
    uint64_t shared=0u,branch=0u;
    for(size_t i=0u;i<delivery->count;i++) {
        const CettaWeightLedger *weights=bindings_weight_view(
            cetta_owned_return_image_view(delivery->images[i])->environment);
        CHECK(weights->length==2u);
        const CettaWeightFactor **ordered=cetta_weight_ledger_ordered(weights);
        assert(ordered && weights->length==2u);
        CHECK(ordered[0]->value->kind==ATOM_GROUNDED && ordered[0]->value->ground.gkind==GV_INT &&
            ordered[0]->value->ground.ival==2);
        CHECK(ordered[1]->value->kind==ATOM_GROUNDED && ordered[1]->value->ground.gkind==GV_INT &&
            ordered[1]->value->ground.ival==(i?5:3));
        CHECK(ordered[0]->occurrence && ordered[1]->occurrence &&
            ordered[0]->dependency && ordered[1]->dependency);
        if(i)CHECK(ordered[0]->occurrence==shared && ordered[1]->occurrence!=branch);
        shared=ordered[0]->occurrence;branch=ordered[1]->occurrence;
        free(ordered);
    }
}
static void ordinary_source_weighted_case(bool read,bool private_data) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(ordinary_weighted_program,
        read?"(weight:read (algebra 0 1 + *) (weighted-outer))":"(weighted-outer)",1u,1u,
        private_data?CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD:CETTA_PRIME_SOURCE_IMMUTABLE,&execution);
    OrdinarySourceDelivery delivery={0};
    for(unsigned i=0u;i<10000u && !execution.terminal;i++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);

    CHECK(execution.stop==CETTA_OWNED_EXECUTION_COMPLETE);
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    ordinary_source_factors(&delivery);
    char observed[2][384];
    const char *expected[]={"V","V"};
    if(read) for(size_t i=0u;i<2u;i++) {
        const CettaWeightFactor **factors=cetta_weight_ledger_ordered(bindings_weight_view(
            cetta_owned_return_image_view(delivery.images[i])->environment));
        assert(factors);
        snprintf(observed[i],sizeof(observed[i]),
            "(Weighted V %u (provenance ((Factor %"PRIu64" %"PRIu64" 2) "
            "(Factor %"PRIu64" %"PRIu64" %u))))",i?10u:6u,
            factors[0]->occurrence,factors[0]->dependency,
            factors[1]->occurrence,factors[1]->dependency,i?5u:3u);
        free(factors);expected[i]=observed[i];
    }
    ordinary_source_delivery_check(&delivery,expected,2u);
}

static void ordinary_source_weighted_history_case(unsigned mode) {
    CettaOwnedExecution execution={0};
    CettaOwnedReturnImagePolicy policy={.bounded_history=CETTA_RETURN_HISTORY_RETAIN};
    OrdinarySourceSharing sharing={.policy=&policy};
    sharing.owner=ordinary_source_driver_open(ordinary_weighted_program,"(weighted-outer)",1u,1u,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeControl *control=owned->ordinary_control;
    Arena inspection;arena_init_detached(&inspection);
    Atom *history=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(eval_prime_source_execution_history(&execution,&inspection,&history)==CETTA_CONTINUATION_READY);
    CHECK(atom_is_symbol_id(history,symbol_intern_cstr(g_symbols,"Unrecorded")));
    CHECK(!control->history.group && !control->history.count);
    if(mode==2u) {
        OrdinarySourceDelivery refuse={.refuse=true};
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&refuse);
        CHECK(control->steps==1u && !execution.accepted);
    }
    uint64_t start=control->steps;
    PrimeNativeHistorySpec spec={.enabled=true,.selections=mode==5u?1u:512u,
        .nodes=mode==5u?0u:32u,.bytes=mode==5u?0u:32768u,.owners=4096u};
    CHECK(eval_prime_source_execution_record(&execution,&spec)==CETTA_CONTINUATION_READY);
    CHECK(control->steps==start && control->history.start==start && control->history.spec.enabled);
    if(mode==6u)policy.bounded_history=CETTA_RETURN_HISTORY_REFUSE;
#else
    PrimeNativeHistorySpec spec={.enabled=true,.selections=512u,.nodes=32u,.bytes=32768u,.owners=4096u};
    CHECK(eval_prime_source_execution_record(&execution,&spec)==CETTA_CONTINUATION_UNSUPPORTED);
    CHECK(eval_prime_source_execution_history(&execution,&inspection,&history)==CETTA_CONTINUATION_UNSUPPORTED && !history);
    CHECK(eval_prime_source_execution_forget(&execution)==CETTA_CONTINUATION_UNSUPPORTED);
#endif
    CettaSharedReturnProducer *producer=cetta_shared_return_producer_new(&execution,2u,1u,
        ordinary_source_capture,ordinary_source_compatible,&sharing);
    assert(producer);
    OrdinarySourceDelivery delivery={0};
    bool changed=false;
    CettaSharedReturnStep step=CETTA_SHARED_RETURN_PAUSED;
    for(unsigned i=0u;i<10000u && step!=CETTA_SHARED_RETURN_COMPLETE;i++) {
        if(cetta_shared_return_producer_peek(producer,1u).kind==CETTA_SHARED_DELIVERY_ANSWER) {
            CHECK(cetta_shared_return_producer_take(producer,1u,ordinary_source_publish,&delivery).kind
                ==CETTA_SHARED_DELIVERY_ANSWER);
            if(!changed) {
                const CettaOwnedExecution *held=cetta_shared_return_producer_execution(producer);
                uint64_t paid=control->steps;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
                if(mode==3u) {
                    PrimeNativeHistorySpec stop={.stop=true};
                    CHECK(eval_prime_source_execution_record(held,&stop)==CETTA_CONTINUATION_READY);
                    CHECK(control->history.stopped && !control->history.spec.enabled);
                } else if(mode==4u) {
                    CHECK(eval_prime_source_execution_forget(held)==CETTA_CONTINUATION_READY);
                    CHECK(control->history.forgotten && !control->history.spec.enabled);
                    CHECK(prime_need_recording_readable(bindings_receipt_view(
                        cetta_owned_return_image_view(delivery.images[0])->environment)));
                    policy.bounded_history=CETTA_RETURN_HISTORY_FORGET;
                }
#else
                (void)held;
#endif
                CHECK(control->steps==paid);
                CHECK(cetta_shared_return_producer_cancel(producer,0u));
                changed=true;
            }
            CHECK(cetta_shared_return_producer_trim(producer)==1u);
        }
        uint64_t paid=control->steps;
        step=cetta_shared_return_producer_advance(producer,1u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if(step==CETTA_SHARED_RETURN_CAPTURE_REFUSED && mode==6u) {
            const CettaOwnedExecution *held=cetta_shared_return_producer_execution(producer);
            assert(held->has_pending);
            uint64_t staged=control->steps;
            CettaOwnedReturn loan=held->pending;
            policy.bounded_history=CETTA_RETURN_HISTORY_FORGET;
            CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_PAUSED);
            CHECK(control->steps==staged && !held->has_pending &&
                loan.outcome.term==owned->ordinary_reified);
            step=CETTA_SHARED_RETURN_PAUSED;
        }
#endif
        CHECK(step==CETTA_SHARED_RETURN_PAUSED || step==CETTA_SHARED_RETURN_COMPLETE);
        CHECK(control->steps-paid<=1u);
    }
    if(cetta_shared_return_producer_peek(producer,1u).kind==CETTA_SHARED_DELIVERY_ANSWER)
        CHECK(cetta_shared_return_producer_take(producer,1u,ordinary_source_publish,&delivery).kind
            ==CETTA_SHARED_DELIVERY_ANSWER);
    CHECK(step==CETTA_SHARED_RETURN_COMPLETE && delivery.count==2u);
    history=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(eval_prime_source_execution_history(cetta_shared_return_producer_execution(producer),
        &inspection,&history)==CETTA_CONTINUATION_READY);
    if(mode==4u)CHECK(atom_is_symbol_id(history,symbol_intern_cstr(g_symbols,"Forgotten")));
    else {
        assert(history && history->kind==ATOM_EXPR && history->expr.len==2u);
        CHECK(atom_is_symbol(history->expr.elems[0],"history"));
        Atom *record=history->expr.elems[1];
        CHECK(record->kind==ATOM_EXPR && atom_is_symbol(record->expr.elems[0],"History"));
        CHECK(owning_consumer_field(record,"start-work")->ground.ival==(int64_t)start);
        CHECK(atom_is_symbol(owning_consumer_field(record,"recording"),mode==3u?"False":"True"));
        CHECK(atom_is_symbol(owning_consumer_field(record,"source-correspondence"),"Unchecked"));
        if(mode==5u)CHECK(control->history.omitted || prime_need_recording_status(control->history.group).incomplete);
    }
#endif
    cetta_shared_return_producer_free(producer);eval_prime_source_owner_release(sharing.owner);
    ordinary_source_factors(&delivery);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(mode==1u || mode==2u || mode==3u || mode==4u) {
        const PrimeNeedReceipt *receipt=bindings_receipt_view(
            cetta_owned_return_image_view(delivery.images[0])->environment);
        CHECK(receipt->recording && prime_need_recording_readable(receipt));
        PrimeNeedReceiptGraph graph={0};
        CHECK(prime_need_receipt_graph_collect(receipt,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
        prime_need_receipt_graph_free(&graph);
    }
#endif
    const char *expected[]={"V","V"};
    ordinary_source_delivery_check(&delivery,expected,2u);arena_free(&inspection);
}

static const char *ordinary_shared_program=
    "(= (pick) 1) (= (pick) 1) (= (pick) 2) (= (same $x) (Pair $x $x))";
static const char *ordinary_shared_expected[]={"(Pair 1 1)","(Pair 1 1)","(Pair 2 2)"};

static CettaWorkCreditView work_credit_view(CettaWorkCredit *credit) {
    CettaWorkCreditView view={0};
    CHECK(cetta_work_credit_inspect(credit,&view));
    CHECK(view.root_remaining==view.root_initial-view.root_spent);
    return view;
}

static void work_credit_cases(void) {
    CettaWorkCredit *root=cetta_work_credit_new(10u);assert(root);
    CettaWorkCredit *first=cetta_work_credit_reserve(root,6u);
    CettaWorkCredit *second=cetta_work_credit_reserve(root,4u);assert(first && second);
    CHECK(!cetta_work_credit_reserve(root,1u));
    CHECK(!cetta_work_credit_close(root));
    CHECK(cetta_work_credit_retain(first));
    CettaWorkCredit *alias=first;
    CHECK(cetta_work_credit_spend(alias,2u));
    CHECK(cetta_work_credit_available(first)==4u);
    CettaWorkCredit *nested=cetta_work_credit_reserve(first,3u);assert(nested);
    CHECK(!cetta_work_credit_close(first));
    CHECK(!cetta_work_credit_spend(first,2u));
    CHECK(cetta_work_credit_spend(nested,2u));
    CHECK(work_credit_view(root).root_spent==4u);
    CHECK(cetta_work_credit_close(nested));
    CHECK(!cetta_work_credit_close(nested));
    CHECK(!cetta_work_credit_spend(nested,0u));
    CHECK(cetta_work_credit_available(first)==2u);
    cetta_work_credit_release(nested);
    CHECK(cetta_work_credit_spend(first,2u));
    CHECK(!cetta_work_credit_spend(alias,1u));
    CHECK(cetta_work_credit_close(alias));
    CHECK(!cetta_work_credit_close(first));
    CHECK(!cetta_work_credit_reserve(alias,0u));
    cetta_work_credit_release(first);cetta_work_credit_release(alias);
    CHECK(cetta_work_credit_close(second));
    CHECK(!cetta_work_credit_close(second));
    cetta_work_credit_release(second);
    CettaWorkCreditView view=work_credit_view(root);
    CHECK(view.balance==4u && view.root_spent==6u && !view.children);
    CHECK(cetta_work_credit_close(root));
    CHECK(!cetta_work_credit_spend(root,0u) && !cetta_work_credit_close(root));
    cetta_work_credit_release(root);

    root=cetta_work_credit_new(10u);assert(root);
    first=cetta_work_credit_reserve(root,6u);
    second=cetta_work_credit_reserve(root,4u);assert(first && second);
    CettaWorkCredit *foreign=cetta_work_credit_new(10u);assert(foreign);
    CHECK(cetta_work_credit_spend(first,6u));
    CHECK(!cetta_work_credit_transfer(first,first,0u));
    CHECK(!cetta_work_credit_transfer(foreign,first,1u));
    CHECK(!cetta_work_credit_transfer(second,first,5u));
    CHECK(cetta_work_credit_transfer(second,first,2u));
    view=work_credit_view(root);
    CHECK(view.root_spent==6u && view.root_remaining==4u && !view.balance);
    CHECK(cetta_work_credit_available(first)==2u &&
        cetta_work_credit_available(second)==2u);
    CHECK(cetta_work_credit_spend(first,2u));
    CHECK(cetta_work_credit_close(first));
    CHECK(!cetta_work_credit_transfer(second,first,1u));
    CHECK(!cetta_work_credit_transfer(first,second,0u));
    CHECK(cetta_work_credit_available(second)==2u &&
        cetta_work_credit_available(foreign)==10u);
    cetta_work_credit_release(first);cetta_work_credit_release(second);
    view=work_credit_view(root);
    CHECK(view.balance==2u && view.root_spent==8u && !view.children);
    cetta_work_credit_release(root);cetta_work_credit_release(foreign);

    root=cetta_work_credit_new(UINT64_MAX);assert(root);
    first=cetta_work_credit_reserve(root,UINT64_MAX);assert(first);
    CHECK(cetta_work_credit_spend(first,UINT64_MAX));
    CHECK(!cetta_work_credit_spend(first,1u));
    CHECK(!cetta_work_credit_reserve(root,1u));
    cetta_work_credit_release(first);
    view=work_credit_view(root);
    CHECK(view.root_initial==UINT64_MAX && view.root_spent==UINT64_MAX && !view.balance);
    cetta_work_credit_release(root);
    root=cetta_work_credit_new(0u);assert(root);
    first=cetta_work_credit_reserve(root,0u);assert(first);
    CHECK(cetta_work_credit_spend(first,0u));
    CHECK(!cetta_work_credit_spend(first,1u));
    cetta_work_credit_release(first);cetta_work_credit_release(root);

    /* Releasing the deepest owner unwinds retained parents without using the
     * C stack, while the root's independent reference keeps its account live. */
    root=cetta_work_credit_new(37u);assert(root);first=root;
    for(unsigned i=0u;i<4096u;i++) {
        nested=cetta_work_credit_reserve(first,37u);assert(nested);
        if(first!=root)cetta_work_credit_release(first);
        first=nested;
    }
    CHECK(cetta_work_credit_spend(first,1u));
    cetta_work_credit_release(first);
    view=work_credit_view(root);
    CHECK(view.balance==36u && view.root_spent==1u && !view.children);
    cetta_work_credit_release(root);
}

static uint64_t ordinary_source_credit_case(const char *program,const char *query,
    const char *const *expected,size_t count,uint64_t grant,bool completes) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *source=ordinary_source_driver_open(program,query,7u,1u,&execution);
    CettaOwnedExecutionNext ordinary=execution.next;
    CHECK(eval_prime_source_execution_fund(&execution,NULL,0u)==CETTA_CONTINUATION_READY);
    CHECK(execution.next==ordinary);
    CettaWorkCredit *root=cetta_work_credit_new(grant);assert(root);
    CHECK(!eval_prime_source_execution_add_credit(&execution,root,0u));
    if(grant<UINT64_MAX) {
        CHECK(eval_prime_source_execution_fund(&execution,root,grant+1u)==CETTA_CONTINUATION_CAPACITY);
        CHECK(execution.next==ordinary && work_credit_view(root).balance==grant);
    }
    CHECK(eval_prime_source_execution_fund(&execution,root,grant)==CETTA_CONTINUATION_READY);
    CHECK(execution.next!=ordinary && eval_prime_source_execution_is_current(&execution));
    CHECK(eval_prime_source_execution_fund(&execution,root,0u)==CETTA_CONTINUATION_UNSUPPORTED);
    OrdinarySourceDelivery delivery={0};
    uint64_t work=0u;
    for(unsigned i=0u;i<(completes?10000u:32u) && !execution.terminal;i++) {
        uint64_t before=work;
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(eval_prime_source_execution_work(&execution,&work));
        CHECK(work>=before && work-before<=7u);
        CettaWorkCreditView view=work_credit_view(root);
        CHECK(view.root_spent==work && work<=grant && !view.balance);
    }
    CHECK(execution.terminal==completes);
    if(completes)CHECK(execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
        execution.completion==CETTA_EVAL_COMPLETE && work>0u);
    else {
        CHECK(!execution.accepted && !execution.has_pending);
        if(grant<UINT64_MAX)CHECK(work==grant);
        else CHECK(work>0u && work<grant);
    }
    cetta_owned_execution_cancel(&execution);
    CettaWorkCreditView view=work_credit_view(root);
    CHECK(!view.children && view.balance==grant-work && view.root_spent==work);
    cetta_owned_execution_cancel(&execution);
    CHECK(work_credit_view(root).balance==grant-work);
    cetta_work_credit_release(root);eval_prime_source_owner_release(source);
    ordinary_source_delivery_check(&delivery,expected,count);
    return work;
}

static void ordinary_source_recredit_case(void) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *source=ordinary_source_driver_open(
        "(= (source-loop $n) (source-loop (+ $n 1)))","(source-loop 0)",7u,1u,&execution);
    CettaWorkCredit *root=cetta_work_credit_new(20u);
    CettaWorkCredit *foreign=cetta_work_credit_new(3u);assert(root && foreign);
    CHECK(eval_prime_source_execution_fund(&execution,root,11u)==CETTA_CONTINUATION_READY);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeControl *control=owned->ordinary_control;
    OrdinarySourceDelivery delivery={0};
    uint64_t work=0u;
    for(unsigned i=0u;i<8u;i++) {
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(eval_prime_source_execution_work(&execution,&work));
        CHECK(work<=11u);
    }
    CHECK(work==11u && work_credit_view(root).root_spent==11u);
    CHECK(!execution.has_pending && !execution.accepted && !execution.terminal);
    CHECK(!eval_prime_source_execution_add_credit(&execution,foreign,1u));
    CHECK(!eval_prime_source_execution_add_credit(&execution,root,10u));
    CHECK(work_credit_view(root).balance==9u && work_credit_view(foreign).balance==3u);
    CHECK(eval_prime_source_execution_add_credit(&execution,root,3u));
    CHECK(execution.computation==owned && owned->ordinary_control==control &&
        control->steps==11u && !execution.has_pending && !execution.accepted);
    CHECK(work_credit_view(root).root_spent==11u && work_credit_view(root).balance==6u);
    for(unsigned i=0u;i<8u;i++) {
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(eval_prime_source_execution_work(&execution,&work));
        CHECK(work<=14u);
    }
    CHECK(work==14u && control->steps==14u && !delivery.count);
    CHECK(work_credit_view(root).root_spent==14u && work_credit_view(root).balance==6u);
    cetta_owned_execution_cancel(&execution);
    CHECK(!eval_prime_source_execution_add_credit(&execution,root,1u));
    CHECK(work_credit_view(root).balance==6u && !work_credit_view(root).children);
    cetta_work_credit_release(root);cetta_work_credit_release(foreign);
    eval_prime_source_owner_release(source);
    ordinary_source_delivery_check(&delivery,NULL,0u);
}

static uint64_t ordinary_source_shared_case(uint64_t quantum,uint32_t workers,bool funded) {
    CettaOwnedExecution execution={0},moved={0};
    OrdinarySourceSharing sharing={.refusals=2u};
    sharing.owner=ordinary_source_driver_open(ordinary_shared_program,"(same (pick))",
        quantum,workers,&execution);
    CettaWorkCredit *credit=funded?cetta_work_credit_new(20000u):NULL;
    if(funded) {
        assert(credit);
        CHECK(eval_prime_source_execution_fund(&execution,credit,10000u)==CETTA_CONTINUATION_READY);
    }
    CettaPrimeObservationReturn *owned=execution.computation;
    ordinary_source_pending(&execution,quantum);
    CettaOwnedReturn loan=execution.pending;
    uint64_t paid=owned->ordinary_control->steps;
    if(funded) {
        CHECK(eval_prime_source_execution_add_credit(&execution,credit,7u));
        CHECK(owned->ordinary_control->steps==paid && execution.has_pending &&
            execution.pending.outcome.term==loan.outcome.term &&
            execution.pending.environment==loan.environment && !execution.accepted);
        CHECK(work_credit_view(credit).balance==9993u &&
            work_credit_view(credit).root_spent==paid);
    }
    CHECK(cetta_owned_execution_move(&moved,&execution));
    cetta_owned_execution_cancel(&execution);
    CHECK(moved.has_pending && moved.pending.outcome.term==loan.outcome.term &&
        moved.pending.environment==loan.environment && !moved.accepted);
    CettaSharedReturnProducer *producer=cetta_shared_return_producer_new(&moved,2u,1u,
        ordinary_source_capture,ordinary_source_compatible,&sharing);
    assert(producer && !moved.computation);
    CHECK(cetta_shared_return_producer_advance(producer,0u)==CETTA_SHARED_RETURN_PAUSED);
    CHECK(!sharing.captures && owned->ordinary_control->steps==paid);
    for(unsigned i=0u;i<2u;i++) {
        CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_CAPTURE_REFUSED);
        CHECK(owned->ordinary_control->steps==paid && owned->ordinary_next==1u);
        const CettaOwnedExecution *held=cetta_shared_return_producer_execution(producer);
        CHECK(held->has_pending && !held->accepted &&
            held->pending.outcome.term==loan.outcome.term && held->pending.environment==loan.environment);
    }
    CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_PAUSED);
    CHECK(owned->ordinary_control->steps==paid && sharing.captures==3u);
    OrdinarySourceDelivery first={.refuse=true},remaining={0};
    CettaSharedDelivery peek=cetta_shared_return_producer_peek(producer,0u);
    assert(peek.kind==CETTA_SHARED_DELIVERY_ANSWER);
    CHECK(peek.returned->outcome.term!=loan.outcome.term && peek.returned->environment!=loan.environment);
    CHECK(cetta_shared_return_producer_take(producer,0u,ordinary_source_publish,&first).kind
        ==CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED);
    first.refuse=false;
    CHECK(cetta_shared_return_producer_take(producer,0u,ordinary_source_publish,&first).kind
        ==CETTA_SHARED_DELIVERY_ANSWER);
    CHECK(!cetta_shared_return_producer_trim(producer));
    CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_BACKPRESSURE);
    CHECK(owned->ordinary_control->steps==paid);
    CHECK(cetta_shared_return_producer_cancel(producer,0u));
    CHECK(cetta_shared_return_producer_peek(producer,0u).kind==CETTA_SHARED_DELIVERY_CANCELLED);
    CettaSharedReturnStep step=CETTA_SHARED_RETURN_PAUSED;
    for(unsigned i=0u;i<10000u && step!=CETTA_SHARED_RETURN_COMPLETE;i++) {
        if(cetta_shared_return_producer_peek(producer,1u).kind==CETTA_SHARED_DELIVERY_ANSWER) {
            CHECK(cetta_shared_return_producer_take(producer,1u,ordinary_source_publish,&remaining).kind
                ==CETTA_SHARED_DELIVERY_ANSWER);
            CHECK(cetta_shared_return_producer_trim(producer)==1u);
        }
        paid=owned->ordinary_control->steps;
        step=cetta_shared_return_producer_advance(producer,1u);
        CHECK(step==CETTA_SHARED_RETURN_PAUSED || step==CETTA_SHARED_RETURN_COMPLETE);
        CHECK(owned->ordinary_control->steps-paid<=quantum);
        CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
    }
    if(cetta_shared_return_producer_peek(producer,1u).kind==CETTA_SHARED_DELIVERY_ANSWER)
        CHECK(cetta_shared_return_producer_take(producer,1u,ordinary_source_publish,&remaining).kind
            ==CETTA_SHARED_DELIVERY_ANSWER);
    CHECK(step==CETTA_SHARED_RETURN_COMPLETE && remaining.count==3u && sharing.captures==5u);
    CHECK(cetta_shared_return_producer_execution(producer)->accepted==3u && owned->ordinary_next==3u);
    CHECK(cetta_shared_return_producer_peek(producer,1u).kind==CETTA_SHARED_DELIVERY_COMPLETED);
    paid=owned->ordinary_control->steps;
    if(funded) {
        CHECK(work_credit_view(credit).root_spent==paid &&
            work_credit_view(credit).balance==9993u);
        CHECK(!eval_prime_source_execution_add_credit(
            cetta_shared_return_producer_execution(producer),credit,1u));
    }
    cetta_shared_return_producer_free(producer);eval_prime_source_owner_release(sharing.owner);
    if(funded) {
        CettaWorkCreditView view=work_credit_view(credit);
        CHECK(view.root_spent==paid && view.balance==20000u-paid && !view.children);
        cetta_work_credit_release(credit);
    }
    /* The received logical worlds and values outlive both producer owners. */
    ordinary_source_delivery_check(&first,ordinary_shared_expected,1u);
    ordinary_source_delivery_check(&remaining,ordinary_shared_expected,3u);
    return paid;
}

static void ordinary_source_detach_case(bool pending) {
    CettaOwnedExecution execution={0},detached={0};
    OrdinarySourceSharing sharing={0};
    sharing.owner=ordinary_source_driver_open(ordinary_shared_program,"(same (pick))",1u,1u,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    ordinary_source_pending(&execution,1u);
    sharing.refusals=pending?1u:0u;
    CettaSharedReturnProducer *producer=cetta_shared_return_producer_new(&execution,1u,3u,
        ordinary_source_capture,ordinary_source_compatible,&sharing);
    assert(producer);
    if(pending)CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    else for(unsigned i=0u;i<10000u && cetta_shared_return_producer_execution(producer)->accepted<2u;i++)
        CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_PAUSED);
    uint64_t paid=owned->ordinary_control->steps;
    const CettaOwnedExecution before=*cetta_shared_return_producer_execution(producer);
    CHECK(cetta_shared_return_producer_detach_execution(producer,&detached));
    CHECK(detached.computation==owned && detached.has_pending==before.has_pending &&
        detached.accepted==before.accepted && detached.completion==before.completion);
    CHECK(owned->ordinary_control->steps==paid);
    CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_DETACHED);
    OrdinarySourceDelivery tail={0},prefix={0};
    for(unsigned i=0u;i<10000u && !detached.terminal;i++)
        cetta_owned_execution_sequence(&detached,1u,ordinary_source_collect,&tail);
    CHECK(detached.stop==CETTA_OWNED_EXECUTION_COMPLETE && detached.accepted==3u);
    cetta_owned_execution_cancel(&detached);
    if(!pending)for(unsigned i=0u;i<2u;i++)
        CHECK(cetta_shared_return_producer_take(producer,0u,ordinary_source_publish,&prefix).kind
            ==CETTA_SHARED_DELIVERY_ANSWER);
    CHECK(cetta_shared_return_producer_peek(producer,0u).kind==CETTA_SHARED_DELIVERY_PAUSED);
    cetta_shared_return_producer_free(producer);eval_prime_source_owner_release(sharing.owner);
    ordinary_source_delivery_check(&prefix,ordinary_shared_expected,pending?0u:2u);
    ordinary_source_delivery_check(&tail,ordinary_shared_expected+(pending?0u:2u),pending?3u:1u);
}

static void ordinary_source_currency_case(bool pending,bool private_data) {
    CettaOwnedExecution execution={0},detached={0};
    OrdinarySourceSharing sharing={.refusals=pending?1u:0u};
    sharing.owner=ordinary_source_driver_open_profile(ordinary_shared_program,"(same (pick))",7u,1u,
        private_data?CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD:CETTA_PRIME_SOURCE_IMMUTABLE,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    ordinary_source_pending(&execution,7u);
    CettaSharedReturnProducer *producer=cetta_shared_return_producer_new(&execution,1u,2u,
        ordinary_source_capture,ordinary_source_compatible,&sharing);
    assert(producer);
    CHECK(cetta_shared_return_producer_advance(producer,1u)==(pending?
        CETTA_SHARED_RETURN_CAPTURE_REFUSED:CETTA_SHARED_RETURN_PAUSED));
    uint64_t paid=owned->ordinary_control->steps;
    CettaOccurrenceLogInfo before,after;
    CHECK(cetta_occurrence_log_info(cetta_shared_return_producer_log(producer),&before));
    /* Deliberately violate the captured-source contract. Both buffered data
     * admission and further work must refuse the actual changed world. */
    CettaPrimeSourceScope *scope=NULL;
    assert(eval_prime_source_scope_open(sharing.owner,0u,&scope)==CETTA_CONTINUATION_READY);
    Space *world=NULL;Arena *arena=NULL;
    assert(eval_prime_source_scope_inputs(scope,&world,NULL,&arena,NULL));
    if(private_data) {
        world=&sharing.owner->owned_region->world;
        CHECK(world!=sharing.owner->space);
        space_add(world,parse_one(arena,"(ChangedSource 9)"));
        CHECK(space_read_view_is_current(&sharing.owner->source_view));
    } else space_add(world,parse_one(arena,"(= (changed-source) 9)"));
    eval_prime_source_scope_close(scope);
    CHECK(!eval_prime_source_owner_is_current(sharing.owner));
    CHECK(cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_CONTEXT_REFUSED);
    CHECK(cetta_shared_return_producer_peek(producer,0u).kind==CETTA_SHARED_DELIVERY_CONTEXT_REFUSED);
    OrdinarySourceDelivery delivery={0};
    CHECK(cetta_shared_return_producer_take(producer,0u,ordinary_source_publish,&delivery).kind
        ==CETTA_SHARED_DELIVERY_CONTEXT_REFUSED);
    CHECK(!delivery.count && owned->ordinary_control->steps==paid);
    CHECK(cetta_occurrence_log_info(cetta_shared_return_producer_log(producer),&after));
    CHECK(before.rows==after.rows && before.past_end==after.past_end);
    CHECK(cetta_shared_return_producer_detach_execution(producer,&detached));
    if(!pending) {
        CHECK(cetta_owned_execution_sequence(&detached,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_HANDOFF);
        CHECK(detached.completion==CETTA_EVAL_INCOMPLETE_INVALIDATED);
    } else CHECK(detached.has_pending && !detached.accepted);
    CHECK(owned->ordinary_control->steps==paid);
    cetta_owned_execution_cancel(&detached);
    cetta_shared_return_producer_free(producer);eval_prime_source_owner_release(sharing.owner);
}

static void ordinary_source_stream_case(const char *program,const char *query,
    const char *const *expected,size_t count,bool completes) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *source=ordinary_source_driver_open(program,query,1u,1u,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    OrdinarySourceDelivery delivery={0};
    for(unsigned i=0u;i<(completes?10000u:32u) && !execution.terminal;i++) {
        uint64_t paid=owned->ordinary_control->steps;
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(owned->ordinary_control->steps-paid<=1u);
    }
    CHECK(execution.terminal==completes);
    if(completes)CHECK(execution.stop==CETTA_OWNED_EXECUTION_COMPLETE && execution.completion==CETTA_EVAL_COMPLETE);
    else CHECK(owned->ordinary_control->steps==32u && !execution.has_pending && !execution.accepted);
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(source);
    ordinary_source_delivery_check(&delivery,expected,count);
}

static void ordinary_source_private_world_case(uint32_t workers, bool remove) {
    Arena arena;arena_init_detached(&arena);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    char repr[32];snprintf(repr,sizeof(repr),"%u",workers);
    assert(cetta_eval_session_record_generic_setting(&context.session,"num-threads",
        CETTA_EVAL_OPTION_VALUE_INT,repr,workers));
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    space_add(&space,parse_one(&arena,"(Item A)"));
    space_add(&space,parse_one(&arena,"(Item A)"));
    space_add(&space,parse_one(&arena,"(Item B)"));
    Atom *query=parse_one(&arena,remove
        ? "(match &self (Item $x) (chain (remove-atom &self (Item $x)) $u (Seen $x)))"
        : "(match &self (Item $x) (chain (add-atom &self (Written $x)) $u (Seen $x)))");
    CettaPrimeSourceOwner *owner=NULL;
    CHECK(eval_prime_source_owner_capture(&space,&registry,query,100000u,
        8u*1024u*1024u,&owner)==CETTA_CONTINUATION_UNSUPPORTED && !owner);
    CHECK(eval_prime_source_owner_capture_profile(&space,&registry,query,
        (CettaPrimeSourceProfile)UINT_MAX,100000u,8u*1024u*1024u,&owner)
        ==CETTA_CONTINUATION_UNSUPPORTED && !owner);
    CHECK(eval_prime_source_owner_capture_profile(&space,&registry,query,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,0u,0u,&owner)
        ==CETTA_CONTINUATION_CAPACITY && !owner);
    CettaContinuationStatus status=eval_prime_source_owner_capture_profile(&space,&registry,
        query,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,100000u,8u*1024u*1024u,&owner);
    if(status!=CETTA_CONTINUATION_READY)
        fprintf(stderr,"private world capture workers=%u remove=%u: %u\n",workers,remove,(unsigned)status);
    assert(status==CETTA_CONTINUATION_READY);
    CettaOwnedExecution execution={0};
    assert(eval_prime_source_execution_open(owner,1u,&execution)==CETTA_CONTINUATION_READY);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    CHECK(region->private_data && space_length64(&region->world)==3u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(workers==2u) {
        PrimeNativeHistorySpec record={.enabled=true,.selections=512u,
            .nodes=128u,.bytes=65536u,.owners=4096u};
        CHECK(eval_prime_source_execution_record(&execution,&record)==CETTA_CONTINUATION_READY);
    }
#endif
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);
    space_free(&space);arena_free(&arena);
    CHECK(eval_prime_source_owner_is_current(owner));
    OrdinarySourceDelivery refused={.refuse=true};
    for(unsigned i=0u;i<10000u && !execution.has_pending && !execution.terminal;i++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&refused);
    if(!execution.has_pending)fprintf(stderr,
        "private world workers=%u remove=%u stop=%u completion=%u failed=%u invalidated=%u steps=%"PRIu64" rows=%"PRIu64"\n",
        workers,remove,(unsigned)execution.stop,(unsigned)execution.completion,
        owned->ordinary_control->failed,owned->ordinary_control->invalidated,
        owned->ordinary_control->steps,(uint64_t)space_length64(&region->world));
    CHECK(execution.has_pending && !execution.terminal && !execution.accepted);
    uint64_t held_steps=owned->ordinary_control->steps;
    CettaCount held_rows=space_length64(&region->world);
    CettaOwnedReturn loan=execution.pending;
    CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&refused)
        ==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(owned->ordinary_control->steps==held_steps && space_length64(&region->world)==held_rows &&
        execution.pending.outcome.term==loan.outcome.term && execution.pending.environment==loan.environment);
    CettaOwnedReturnImagePolicy history_policy={.bounded_history=CETTA_RETURN_HISTORY_RETAIN};
    OrdinarySourceDelivery delivery={.policy=&history_policy};
    CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
        ==CETTA_OWNED_EXECUTION_PAUSED);
    CHECK(owned->ordinary_control->steps==held_steps && space_length64(&region->world)==held_rows &&
        execution.accepted==1u && !execution.has_pending);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(workers==2u) {
        CHECK(eval_prime_source_execution_forget(&execution)==CETTA_CONTINUATION_READY);
        CHECK(prime_need_recording_readable(bindings_receipt_view(
            cetta_owned_return_image_view(delivery.images[0])->environment)));
        history_policy.bounded_history=CETTA_RETURN_HISTORY_FORGET;
    }
#endif
    for(unsigned i=0u;i<10000u && !execution.terminal;i++) {
        uint64_t paid=owned->ordinary_control->steps;
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(owned->ordinary_control->steps-paid<=1u);
    }
    CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
        execution.completion==CETTA_EVAL_COMPLETE);
    CHECK(eval_prime_source_owner_is_current(owner));
    CHECK(space_length64(&region->world)==(remove?0u:6u));
    CHECK(space_program_token_is_current(region->program_read));
    const char *expected[]={"(Seen A)","(Seen A)","(Seen B)"};
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(workers==2u) {
        const PrimeNeedReceipt *receipt=bindings_receipt_view(
            cetta_owned_return_image_view(delivery.images[0])->environment);
        CHECK(prime_need_recording_readable(receipt));
        PrimeNeedReceiptGraph graph={0};
        CHECK(prime_need_receipt_graph_collect(receipt,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
        prime_need_receipt_graph_free(&graph);
    }
#endif
    ordinary_source_delivery_check(&delivery,expected,3u);
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
}

static void ordinary_source_imported_context_case(uint64_t quantum, bool denied) {
    Arena arena;arena_init_detached(&arena);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);g_registry=&registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);eval_set_default_fuel(-1);
    registry_bind(&registry,"&self",atom_space(&arena,&space));
    Atom *error=NULL;
    CHECK(cetta_library_import_module(&context,"durable:activity",&space,false,
        &arena,&arena,&registry,1000000,NULL,&error) && !error);
    CHECK(context.loaded_module_len && context.imported_file_len &&
        cetta_library_context_prime_snapshot_source_admitted(&context));
    space_add(&space,parse_one(&arena,"(= (latent-io) (println! must-not-run))"));
    const char *query=denied ? "(latent-io)"
        : "(chain (activity:state) $before "
          "(chain (add-atom (context-space) (host:record 1 state (host:actor 1 \"activity/1\" ready))) $u "
          "(chain (activity:state) $after (Imported $before $after (== (context-space) &self)))))";
    AtomId *ids=NULL;char diagnostic[256];
    int count=prime_semantics_read_source_text_ids(NULL,query,space.native.universe,
        &ids,diagnostic,sizeof(diagnostic));
    CHECK(count==1 && ids);
    Atom *call=count==1 ? term_universe_get_atom(space.native.universe,ids[0]) : NULL;
    assert(call);free(ids);
    CettaPrimeSourceOwner *owner=NULL;
    CettaContinuationStatus status=eval_prime_source_owner_capture_context(&context,
        &space,&registry,call,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,
        1000000u,32u*1024u*1024u,&owner);
    if(status!=CETTA_CONTINUATION_READY)
        fprintf(stderr,"imported context capture denied=%u status=%u\n",denied,(unsigned)status);
    assert(status==CETTA_CONTINUATION_READY);
    PrimeNativeOwnedRegion *region=owner->owned_region;
    CettaCount rows=space_length64(&space);
    CHECK(region->library->active_mask==context.active_mask &&
        !region->library->imported_file_len && !region->library->loaded_module_len &&
        region->program && region->program!=&region->world &&
        space_program_token_is_current(region->program_read));
    CettaOwnedExecution execution={0};
    CHECK(eval_prime_source_execution_open(owner,quantum,&execution)==CETTA_CONTINUATION_READY);
    eval_set_library_context(NULL);g_registry=NULL;
    cetta_library_context_free(&context);registry_free(&registry);
    space_free(&space);arena_free(&arena);
    CHECK(eval_prime_source_owner_is_current(owner));
    OrdinarySourceDelivery delivery={.refuse=!denied};
    for(unsigned i=0u;i<10000u && !execution.terminal && !execution.has_pending;i++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    if(!denied) {
        CHECK(execution.has_pending && !execution.accepted && !execution.terminal);
        uint64_t paid=0u;CHECK(eval_prime_source_execution_work(&execution,&paid));
        CettaOwnedReturn loan=execution.pending;
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        uint64_t still_paid=0u;CHECK(eval_prime_source_execution_work(&execution,&still_paid));
        CHECK(still_paid==paid && execution.pending.outcome.term==loan.outcome.term &&
            space_length64(&region->world)==rows+1u && space_length64(region->program)==rows);
        delivery.refuse=false;
        for(unsigned i=0u;i<10000u && !execution.terminal;i++)
            cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
        CHECK(execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
            execution.completion==CETTA_EVAL_COMPLETE && execution.accepted==1u);
        CHECK(space_program_token_is_current(region->program_read) &&
            eval_prime_source_owner_is_current(owner));
    } else {
        CHECK(execution.terminal && !execution.accepted && !execution.has_pending &&
            execution.completion==CETTA_EVAL_INCOMPLETE_EFFECT_DENIED &&
            space_length64(&region->world)==rows);
    }
    printf("Imported Prime source: quantum=%"PRIu64" denied=%u inspections=%zu nodes=%zu bytes=%zu\n",
        quantum,denied,region->inspection_spent,region->captured_nodes,region->prepared_bytes);
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    const char *expected[]={"(Imported activity:idle ready True)"};
    ordinary_source_delivery_check(&delivery,expected,denied?0u:1u);
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
}

static void ordinary_source_private_readout_case(bool helper) {
    const char *program=helper
        ? "(Item A) (Item A) (Item B) (= (act) (match &self (Item $x) (chain (remove-atom &self (Item $x)) $u (Seen $x))))"
        : "(Item A) (Item A) (Item B)";
    const char *query=helper ? "(act)" :
        "(chain (collapse (match &self (Item $x) $x)) $old "
        "(chain (add-atom &self (Item C)) $u "
        "(Pair $old (collapse (match &self (Item $x) $x)))))";
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(program,query,1u,1u,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    PrimeNativeOwnedRegion *region=owner->owned_region;
    CHECK(owner->space==region->program && owner->space!=&region->world);
    CHECK(space_length64(owner->space)==(helper?4u:3u));
    OrdinarySourceDelivery delivery={0};
    for(unsigned i=0u;i<10000u && !execution.terminal;i++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    if(execution.stop!=CETTA_OWNED_EXECUTION_COMPLETE)fprintf(stderr,
        "private readout helper=%u stop=%u completion=%u world=%"PRIu64" program=%"PRIu64" data_rev=%"PRIu64"/%"PRIu64" program_view=%u program_token=%u\n",
        helper,(unsigned)execution.stop,(unsigned)execution.completion,
        (uint64_t)space_length64(&region->world),(uint64_t)space_length64(owner->space),
        region->data_read.revision,region->world.revision,
        space_read_view_is_current(&owner->source_view),space_program_token_is_current(region->program_read));
    CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
        execution.completion==CETTA_EVAL_COMPLETE);
    CHECK(eval_prime_source_owner_is_current(owner));
    CHECK(space_length64(&region->world)==(helper?1u:4u));
    CHECK(space_length64(owner->space)==(helper?4u:3u));
    const char *old_and_fresh[]={"(Pair (A A B) (A A B C))"};
    const char *seen[]={"(Seen A)","(Seen A)","(Seen B)"};
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    ordinary_source_delivery_check(&delivery,helper?seen:old_and_fresh,helper?3u:1u);
}

static void retained_payload_frame_case(unsigned extra_rows) {
    Arena arena;arena_init_detached(&arena);
    Space base,other,extra[24];space_init(&base);space_init(&other);
    assert(extra_rows<=24u);
    for(unsigned i=0u;i<extra_rows;i++)space_init(&extra[i]);
    bool transactional=eval_set_payload_transactional(true);
    CHECK(!g_eval_payload_frame && eval_payload_redirects_begin(&arena));
    CettaEvalPayloadFrame *parent=g_eval_payload_frame;
    Space *visible=payload_resolve_space_write(&base);
    CHECK(visible && visible!=&base);
    space_add(visible,parse_one(&arena,"(Item Parent)"));
    for(unsigned i=0u;i<extra_rows;i++) CHECK(eval_payload_note_space_redirect(&extra[i],visible));
    CettaEvalPayloadFrame *child=eval_payload_frame_open(&arena);
    CHECK(child && child->previous==parent && parent->references==2u);
    eval_payload_redirects_end();
    CHECK(!g_eval_payload_frame && parent->references==1u);
    CHECK(eval_payload_redirects_begin(&arena));
    CettaEvalPayloadFrame *unrelated=g_eval_payload_frame;
    CHECK(eval_payload_note_space_redirect(&base,&other));
    CettaEvalPayloadScope scope={0};
    CHECK(eval_payload_frame_enter(child,&scope));
    CHECK(eval_resolve_space_read(&base)==visible);
    for(unsigned i=0u;i<extra_rows;i++) CHECK(eval_resolve_space_read(&extra[i])==visible);
    Space *written=payload_resolve_space_write(&base);
    CHECK(written && written!=visible && written!=&other);
    space_add(written,parse_one(&arena,"(Item Child)"));
    CHECK(space_contains_exact(written,parse_one(&arena,"(Item Parent)")) &&
        !space_contains_exact(visible,parse_one(&arena,"(Item Child)")));
    eval_payload_frame_leave(&scope);
    CHECK(g_eval_payload_frame==unrelated && eval_resolve_space_read(&base)==&other);
    eval_payload_redirects_end();
    CHECK(eval_payload_frame_enter(child,&scope));
    CHECK(eval_resolve_space_read(&base)==written &&
        space_contains_exact(written,parse_one(&arena,"(Item Child)")));
    /* The activation itself holds the frame, so dropping the owner's edge
     * cannot retire live scratch. Leaving releases both child and parent. */
    eval_payload_frame_close(child);
    CHECK(g_eval_payload_frame==child && child->references==1u);
    eval_payload_frame_leave(&scope);
    CHECK(!g_eval_payload_frame && eval_payload_space_redirect_count()==0u &&
        space_length64(&base)==0u && space_length64(&other)==0u);
    eval_payload_frame_leave(&scope);
    eval_set_payload_transactional(transactional);
    for(unsigned i=0u;i<extra_rows;i++)space_free(&extra[i]);
    space_free(&other);space_free(&base);arena_free(&arena);
}

typedef struct { void *inner;CettaEvalPayloadFrame *frame; } PayloadProviderProbe;
static PayloadProviderProbe *payload_provider_active;
static bool payload_provider_fail_after_prefix;
static uint64_t payload_provider_opens,payload_provider_steps,payload_provider_pending,payload_provider_closes;
static void *payload_provider_probe_open(Space *space,Arena *arena,Arena *persistent,
    Registry *registry,Atom *expression) {
    PayloadProviderProbe *probe=calloc(1u,sizeof(*probe));assert(probe);
    CHECK(g_eval_payload_frame && eval_payload_transactional());
    probe->frame=g_eval_payload_frame;
    probe->inner=g_prime_payload_provider.open(space,arena,persistent,registry,expression);
    if(!probe->inner) {free(probe);return NULL;}
    payload_provider_opens++;payload_provider_active=probe;return probe;
}
static RhocalcPayloadStatus payload_provider_probe_step(void *raw,uint64_t allowance,ResultSet *out) {
    PayloadProviderProbe *probe=raw;
    CHECK(g_eval_payload_frame==probe->frame && eval_payload_transactional());
    CettaCount before=out->len;
    RhocalcPayloadStatus status=g_prime_payload_provider.step(probe->inner,allowance,out);
    CHECK(out->len>=before);
    if(allowance)payload_provider_steps++;
    if(status==RHOCALC_PAYLOAD_PENDING)payload_provider_pending++;
    if(payload_provider_fail_after_prefix && out->len>before) return RHOCALC_PAYLOAD_FAILED;
    return status;
}
static void payload_provider_probe_close(void *raw) {
    PayloadProviderProbe *probe=raw;
    CHECK(g_eval_payload_frame==probe->frame && eval_payload_transactional());
    g_prime_payload_provider.close(probe->inner);payload_provider_closes++;
    if(payload_provider_active==probe)payload_provider_active=NULL;
    free(probe);
}
static const RhocalcPayloadProvider payload_provider_probe={
    payload_provider_probe_open,payload_provider_probe_step,payload_provider_probe_close};

static Atom *ordinary_source_payload_expression(Arena *arena,Atom *expression) {
    Atom *payload=atom_expr2(arena,atom_symbol(arena,"quote"),expression);
    Atom *channel=atom_expr2(arena,atom_symbol(arena,"rho:quote"),atom_symbol(arena,"rho:nil"));
    Atom *send=atom_expr3(arena,atom_symbol(arena,"rho:send"),channel,
        atom_expr2(arena,atom_symbol(arena,"rho:eval-payload"),payload));
    Atom *receive=parse_one(arena,"(rho:recv (rho:quote rho:nil) $message (rho:drop $message))");
    return atom_expr3(arena,atom_symbol(arena,"rho:par"),send,receive);
}
static Atom *ordinary_source_payload_process(Arena *arena,const char *payload_text) {
    return ordinary_source_payload_expression(arena,parse_one(arena,payload_text));
}

static RhocalcRunAccount ordinary_source_payload_value(Arena *arena,const RhocalcEvalContext *context,
    const char *payload_text,const char *answer,uint64_t quantum) {
    Atom *process=ordinary_source_payload_process(arena,payload_text);
    uint64_t opened=payload_provider_opens,steps=payload_provider_steps;
    uint64_t pending=payload_provider_pending,closed=payload_provider_closes;
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,8u,1u,false};
    RhocalcRun *run=rhocalc_run_open(arena,process,&profile,context);
    CHECK(run);
    assert(run);
    CettaEvalPayloadFrame *frame=g_eval_payload_frame;
    Registry *registry=g_registry;PrimeEvalStackDriver *driver=g_prime_eval_stack_driver;
    EvalExecutionContext *execution=g_eval_execution;
    bool transactional=eval_payload_transactional();uint64_t epoch=eval_payload_owner_epoch();
    uint64_t used=UINT64_MAX;
    CHECK(rhocalc_run_poll(run,0u,&used)==RHOCALC_RUN_PENDING && used==0u);
    CHECK(rhocalc_run_account(run).driver_steps==0u);
    RhocalcRunStatus status=RHOCALC_RUN_PENDING;
    for(unsigned polls=0u;polls<100000u && status==RHOCALC_RUN_PENDING;polls++) {
        status=rhocalc_run_poll(run,quantum,&used);
        CHECK(used<=quantum && g_eval_payload_frame==frame && g_registry==registry &&
            g_prime_eval_stack_driver==driver && g_eval_execution==execution &&
            eval_payload_transactional()==transactional && eval_payload_owner_epoch()==epoch);
    }

    CHECK(status==RHOCALC_RUN_DONE);
    RhoReductionResult result={0};
    bool finished=rhocalc_run_result(run,&result);
    CHECK(finished && result.reductions_taken==1u);
    Atom *values=finished ? rhocalc_observe_top_level_values(arena,result.residual) : NULL;
    if(!values || !atom_eq(values,parse_one(arena,answer)))
        fprintf(stderr,"payload world %s: %s\n",payload_text,values?atom_to_string(arena,values):"failed");
    CHECK(values && atom_eq(values,parse_one(arena,answer)));
    RhocalcRunAccount account=rhocalc_run_account(run);
    rhocalc_run_close(run);
    if(context->payload_provider==&payload_provider_probe) CHECK(
        payload_provider_opens==opened+1u && payload_provider_closes==closed+1u &&
        payload_provider_steps>steps+1u && payload_provider_pending>pending);
    return account;
}

static void payload_completed_data_case(Arena *arena,const RhocalcEvalContext *context) {
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,8u,1u,false};
    RhocalcRun *run=rhocalc_run_open(arena,ordinary_source_payload_process(arena,
        "(cons-atom + (1 2))"),&profile,context);
    CHECK(run);assert(run);
    RhocalcRunStatus status=RHOCALC_RUN_PENDING;
    for(unsigned polls=0u;polls<10000u && status==RHOCALC_RUN_PENDING;polls++)
        status=rhocalc_run_poll(run,1u,NULL);
    RhoReductionResult result={0};CHECK(status==RHOCALC_RUN_DONE && rhocalc_run_result(run,&result));
    Atom *values=result.residual?rhocalc_observe_top_level_values(arena,result.residual):NULL;
    Atom *data=values && values->kind==ATOM_EXPR && values->expr.len==1u?values->expr.elems[0]:NULL;
    CHECK(data && atom_is_he_evaluated(data) && atom_eq(data,parse_one(arena,"(+ 1 2)")));
    rhocalc_run_close(run);
    if(data) {
        ResultSet observed;result_set_init(&observed);
        CHECK(eval_top_with_registry_completion(context->space,arena,context->persistent_arena,
            context->registry,data,&observed)==CETTA_EVAL_COMPLETE);
        CHECK(observed.len==1u && atom_is_he_evaluated(observed.items[0]) && atom_eq(observed.items[0],data));
        result_set_free(&observed);result_set_init(&observed);
        Atom *activated=atom_expr2(arena,atom_symbol(arena,"eval"),data);
        CHECK(eval_top_with_registry_completion(context->space,arena,context->persistent_arena,
            context->registry,activated,&observed)==CETTA_EVAL_COMPLETE);
        CHECK(observed.len==1u && atom_eq(observed.items[0],atom_int(arena,3)));
        result_set_free(&observed);
    }
}

static void payload_top_prelude_cases(Arena *arena,const RhocalcEvalContext *context) {
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,8u,1u,false};
    CettaLibraryContext *library=eval_current_library_context();
    int fuel=library->session.options.fuel_limit;
    int depth=library->session.options.max_stack_depth;
    library->session.options.max_stack_depth=0;
    for(unsigned budget=0u;budget<2u;budget++) {
        library->session.options.fuel_limit=(int)budget;
        CHECK(current_eval_fuel_limit()==(int)budget);
        for(unsigned kind=0u;kind<3u;kind++) {
            Atom *source=kind==0u?atom_int(arena,7):kind==1u?
                atom_symbol(arena,"UnboundPayloadValue"):atom_prime_held_wrap(arena,atom_int(arena,7));
            RhocalcRun *run=rhocalc_run_open(arena,ordinary_source_payload_expression(arena,source),&profile,context);
            CHECK(run);assert(run);
            RhocalcRunStatus status=RHOCALC_RUN_PENDING;
            for(unsigned polls=0u;polls<10000u && status==RHOCALC_RUN_PENDING;polls++)
                status=rhocalc_run_poll(run,1u,NULL);
            bool complete=budget || kind==2u;
            CHECK(status==(complete?RHOCALC_RUN_DONE:RHOCALC_RUN_FAILED));
            RhoReductionResult result={0};
            CHECK(rhocalc_run_result(run,&result)==complete);
            if(complete && result.residual) {
                Atom *values=rhocalc_observe_top_level_values(arena,result.residual);
                CHECK(values && values->kind==ATOM_EXPR && values->expr.len==1u &&
                    atom_eq(values->expr.elems[0],source));
                if(kind==2u)CHECK(atom_prime_held_is(values->expr.elems[0]));
            }
            rhocalc_run_close(run);
        }
    }
    library->session.options.fuel_limit=fuel;
    library->session.options.max_stack_depth=depth;
    /* Atomic registry lookup returns the stored descriptor without turning
     * its captured producer into a live computation. */
    Bindings lexical,local;bindings_init(&lexical);bindings_init(&local);
    Atom *variable=atom_var(arena,"payload-captured");
    CHECK(bindings_add_var_owned(&lexical,variable,atom_int(arena,17)));
    Atom *term=atom_expr3(arena,atom_symbol(arena,"+"),variable,atom_int(arena,1)),*ref=NULL;
    CHECK(prime_need_allocate_source_argument_ref(arena,term,prime_need_fresh_source_occurrence(),
        1u,&lexical,&local,&ref));
    Atom *stored=prime_need_persist_data(arena,ref,&local);
    CHECK(stored && prime_need_is_stored_thunk(stored,NULL,NULL,NULL));
    Registry registry;registry_init(&registry);registry_bind(&registry,"&saved",stored);
    RhocalcEvalContext captured=*context;captured.registry=&registry;
    RhocalcRun *run=rhocalc_run_open(arena,ordinary_source_payload_process(arena,"&saved"),&profile,&captured);
    CHECK(run);assert(run);
    RhocalcRunStatus status=RHOCALC_RUN_PENDING;
    for(unsigned polls=0u;polls<10000u && status==RHOCALC_RUN_PENDING;polls++)
        status=rhocalc_run_poll(run,1u,NULL);
    RhoReductionResult result={0};
    CHECK(status==RHOCALC_RUN_DONE && rhocalc_run_result(run,&result));
    Atom *values=result.residual?rhocalc_observe_top_level_values(arena,result.residual):NULL;
    CHECK(values && values->kind==ATOM_EXPR && values->expr.len==1u &&
        prime_need_is_stored_thunk(values->expr.elems[0],NULL,NULL,NULL) &&
        atom_eq(values->expr.elems[0],stored));
    rhocalc_run_close(run);registry_free(&registry);
    bindings_free(&local);bindings_free(&lexical);
}

static void retained_payload_interleaving_case(Arena *arena,const RhocalcEvalContext *context,
    Space *world,uint64_t quantum) {
    const char *queries[]={
        "(chain (add-atom (context-space) (Item C)) $u (read-both))",
        "(chain (add-atom (context-space) (Item D)) $u (read-both))"};
    const char *answers[]={"((Pair (B C) (B C)))","((Pair (B D) (B D)))"};
    RhoRuntimeProfile profile={RHO_SCHEDULER_CANONICAL,8u,1u,false};
    RhocalcRun *runs[2];RhocalcRunStatus status[2]={RHOCALC_RUN_PENDING,RHOCALC_RUN_PENDING};
    for(unsigned i=0u;i<2u;i++) {
        runs[i]=rhocalc_run_open(arena,ordinary_source_payload_process(arena,queries[i]),&profile,context);
        CHECK(runs[i]);assert(runs[i]);
    }
    CettaEvalPayloadFrame *frame=g_eval_payload_frame;
    uint64_t opened=payload_provider_opens,closed=payload_provider_closes;
    for(unsigned polls=0u;polls<100000u &&
        (status[0]==RHOCALC_RUN_PENDING || status[1]==RHOCALC_RUN_PENDING);polls++) {
        for(unsigned i=0u;i<2u;i++) if(status[i]==RHOCALC_RUN_PENDING) {
            status[i]=rhocalc_run_poll(runs[i],quantum,NULL);
            CHECK(g_eval_payload_frame==frame && !g_prime_eval_stack_driver);
        }
    }
    for(unsigned i=0u;i<2u;i++) {
        RhoReductionResult result={0};
        CHECK(status[i]==RHOCALC_RUN_DONE && rhocalc_run_result(runs[i],&result));
        Atom *values=result.residual?rhocalc_observe_top_level_values(arena,result.residual):NULL;
        CHECK(values && atom_eq(values,parse_one(arena,answers[i])));
        rhocalc_run_close(runs[i]);
    }
    CHECK(payload_provider_opens==opened+2u && payload_provider_closes==closed+2u &&
        !space_contains_exact(world,parse_one(arena,"(Item C)")) &&
        !space_contains_exact(world,parse_one(arena,"(Item D)")));
    /* Cancel before acquisition, while the producer is suspended, and during
     * actual Need return traversal. No completion or partial COMM is issued. */
    for(unsigned cut=0u;cut<3u;cut++) {
        opened=payload_provider_opens;closed=payload_provider_closes;
        RhocalcRun *run=rhocalc_run_open(arena,ordinary_source_payload_process(arena,
            "(let $x (superpose (7 8)) (Pair $x $x))"),&profile,context);
        CHECK(run);assert(run);
        bool reached=cut==0u;
        for(unsigned polls=0u;polls<100000u && !reached;polls++) {
            CHECK(rhocalc_run_poll(run,1u,NULL)==RHOCALC_RUN_PENDING);
            if(payload_provider_opens==opened+1u && payload_provider_active) {
                PrimePayloadEvaluation *job=payload_provider_active->inner;
                reached=cut==1u || job->reifying;
            }
        }
        CHECK(reached && !rhocalc_run_result(run,&(RhoReductionResult){0}));
        /* Retiring storage remains legal after source deselection. */
        PrimeNativeOwnedRegion *region=g_prime_owned_region;g_prime_owned_region=NULL;
        rhocalc_run_close(run);g_prime_owned_region=region;
        CHECK(payload_provider_closes==closed+(cut?1u:0u) &&
            payload_provider_opens==opened+(cut?1u:0u) && g_eval_payload_frame==frame);
    }
    /* A new current caller can narrow authority after provider acquisition.
     * The suspended continuation must not retain the old unrestricted parent. */
    opened=payload_provider_opens;closed=payload_provider_closes;
    RhocalcRun *denied=rhocalc_run_open(arena,
        ordinary_source_payload_process(arena,queries[0]),&profile,context);
    CHECK(denied);assert(denied);
    for(unsigned polls=0u;polls<100000u && payload_provider_opens==opened;polls++)
        CHECK(rhocalc_run_poll(denied,1u,NULL)==RHOCALC_RUN_PENDING);
    CHECK(payload_provider_opens==opened+1u);
    PrimeEvalStackDriver current={.pure_authority=true};
    PrimeEvalStackDriver *previous=g_prime_eval_stack_driver;g_prime_eval_stack_driver=&current;
    RhocalcRunStatus refused=RHOCALC_RUN_PENDING;
    for(unsigned polls=0u;polls<100000u && refused==RHOCALC_RUN_PENDING;polls++)
        refused=rhocalc_run_poll(denied,1u,NULL);
    CHECK(refused==RHOCALC_RUN_FAILED &&
        !rhocalc_run_result(denied,&(RhoReductionResult){0}) &&
        g_prime_eval_stack_driver==&current);
    rhocalc_run_close(denied);g_prime_eval_stack_driver=previous;
    CHECK(payload_provider_closes==closed+1u && g_eval_payload_frame==frame);
    /* A provider failure after one real result cannot publish its prefix. */
    RhocalcRun *partial=rhocalc_run_open(arena,ordinary_source_payload_process(arena,
        "(superpose (7 7 9))"),&profile,context);
    payload_provider_fail_after_prefix=true;
    refused=RHOCALC_RUN_PENDING;
    for(unsigned polls=0u;polls<100000u && refused==RHOCALC_RUN_PENDING;polls++)
        refused=rhocalc_run_poll(partial,1u,NULL);
    payload_provider_fail_after_prefix=false;
    CHECK(refused==RHOCALC_RUN_FAILED && rhocalc_run_account(partial).body_candidates==0u &&
        !rhocalc_run_result(partial,&(RhoReductionResult){0}));
    rhocalc_run_close(partial);
}

static void ordinary_source_payload_world_case(uint64_t quantum,bool retained) {
    const char *program="(Item A) (= (read-both) "
        "(Pair (collapse (match &self (Item $x) $x)) "
        "(collapse (match (context-space) (Item $y) $y))))";
    const char *query="(chain (add-atom (context-space) (Item B)) $u "
        "(chain (remove-atom &self (Item A)) $v (read-both)))";
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(program,query,
        quantum,1u,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    OrdinarySourceDelivery delivery={0};
    for(unsigned i=0u;i<10000u && !execution.terminal;i++)
        cetta_owned_execution_sequence(&execution,quantum,ordinary_source_collect,&delivery);
    CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE);
    const char *expected[]={"(Pair (B) (B))"};
    cetta_owned_execution_cancel(&execution);
    ordinary_source_delivery_check(&delivery,expected,1u);
    CHECK(eval_prime_source_owner_is_current(owner));
    PrimeNativeOwnedRegion *region=owner->owned_region;
    CettaPrimeSourceScope *scope=NULL;
    CHECK(eval_prime_source_scope_open(owner,0u,&scope)==CETTA_CONTINUATION_READY);
    assert(scope);
    Space *code=NULL;Registry *registry=NULL;Arena *persistent=NULL;Atom *source=NULL;
    CHECK(eval_prime_source_scope_inputs(scope,&code,&registry,&persistent,&source));
    CHECK(code==region->program && code!=&region->world &&
        registry_lookup_id(registry,g_builtin_syms.self)->ground.ptr==&region->world);
    Arena arena;arena_init_detached(&arena);
    RhocalcEvalContext context={.space=code,.registry=registry,
        .persistent_arena=persistent,.library_context=eval_current_library_context(),
        .payload_provider=retained?&payload_provider_probe:NULL};
    const char *payloads[]={
        "(read-both)",
        "(collapse (match &self (Item $x) $x))",
        "(collapse (match (context-space) (Item $x) $x))",
        "(chain (add-atom (context-space) (Item C)) $u "
            "(Pair (== (context-space) &self) (read-both)))"
    };
    const char *answers[]={"((Pair (B) (B)))","((B))","((B))",
        "((Pair True (Pair (B C) (B C))))"};
    for(unsigned index=0u;index<4u;index++) {
        ordinary_source_payload_value(&arena,&context,payloads[index],answers[index],quantum);
        CHECK(space_contains_exact(&region->world,parse_one(&arena,"(Item B)")) &&
            !space_contains_exact(&region->world,parse_one(&arena,"(Item C)")) &&
            space_contains_exact(code,parse_one(&arena,"(Item A)")));
    }
    RhocalcRunAccount duplicate=ordinary_source_payload_value(&arena,&context,
        "(superpose (7 7 9))","(7)",quantum);
    CHECK(duplicate.body_candidates==3u);
    RhocalcRunAccount shared=ordinary_source_payload_value(&arena,&context,
        "(let $x (superpose (0 1)) (Pair $x $x))","((Pair 0 0))",quantum);
    CHECK(shared.body_candidates==2u);
    payload_completed_data_case(&arena,&context);
    payload_top_prelude_cases(&arena,&context);
    if(retained) retained_payload_interleaving_case(&arena,&context,&region->world,quantum);
    /* An outer payload may already have a private write. The inner Rho
     * transaction must retain that write while isolating its own suffix. */
    Space outer_data,outer_code;
    space_init_overlay(&outer_data,&region->world);
    space_init_overlay(&outer_code,code);
    space_add(&outer_data,parse_one(&arena,"(Item C)"));
    Registry outer_registry;registry_init(&outer_registry);
    for(uint32_t i=0u;i<registry->len;i++)
        CHECK(registry_copy_entry(&outer_registry,registry,i,registry->entries[i].value));
    registry_bind_id(&outer_registry,g_builtin_syms.self,atom_space(&arena,&outer_data));
    RhocalcEvalContext nested=context;
    nested.space=&outer_code;nested.registry=&outer_registry;
    ordinary_source_payload_value(&arena,&nested,"(read-both)","((Pair (B C) (B C)))",quantum);
    ordinary_source_payload_value(&arena,&nested,
        "(chain (add-atom (context-space) (Item D)) $u "
        "(Pair (== (context-space) &self) (read-both)))",
        "((Pair True (Pair (B C D) (B C D))))",quantum);
    CHECK(space_contains_exact(&outer_data,parse_one(&arena,"(Item C)")) &&
        !space_contains_exact(&outer_data,parse_one(&arena,"(Item D)")) &&
        !space_contains_exact(&region->world,parse_one(&arena,"(Item C)")));
    registry_free(&outer_registry);space_free(&outer_code);space_free(&outer_data);

    Space other,module_view,first,second;
    space_init(&other);
    space_init_overlay(&first,code);space_init_overlay(&second,&first);
    CHECK(prime_native_region_context_space(&first)==&region->world &&
        prime_native_region_context_space(&second)==&region->world);
    CHECK(prime_native_region_context_space(&other)==&other);
    CHECK(space_init_module_own_view(&module_view,code));
    CHECK(prime_native_region_context_space(&module_view)==&module_view);
    /* A different explicit self binding does not redefine this context's
     * data world. It remains explicit to the Rho registry itself. */
    Registry *saved_registry=g_registry;
    Registry unrelated;registry_init(&unrelated);
    registry_bind_id(&unrelated,g_builtin_syms.self,atom_space(&arena,&other));
    g_registry=&unrelated;
    CHECK(prime_native_region_context_space(code)==&region->world);
    g_registry=saved_registry;registry_free(&unrelated);
    space_free(&module_view);space_free(&second);space_free(&first);space_free(&other);

    /* A valid payload registry may omit self or bind an ordinary value.
     * Its context still inherits the outer transaction's visible world. */
    for(unsigned missing=0u;missing<2u;missing++) {
        CHECK(eval_payload_redirects_begin(persistent));
        bool transactional=eval_set_payload_transactional(true);
        Space *parent=malloc(sizeof(*parent));assert(parent);
        space_init_overlay(parent,&region->world);
        CHECK(eval_payload_track_scratch_space(parent));
        CHECK(eval_payload_note_space_redirect(&region->world,parent));
        space_add(parent,parse_one(&arena,"(Item C)"));
        Space parent_code;space_init_overlay(&parent_code,code);
        CHECK(eval_payload_note_space_redirect(code,&parent_code));
        Registry partial;registry_init(&partial);
        if(missing) registry_bind_id(&partial,g_builtin_syms.self,atom_int(&arena,7));
        RhocalcEvalContext context_only=context;
        context_only.space=&parent_code;context_only.registry=&partial;
        ordinary_source_payload_value(&arena,&context_only,
            "(collapse (match (context-space) (Item $x) $x))","((B C))",quantum);
        ordinary_source_payload_value(&arena,&context_only,
            "(chain (add-atom (context-space) (Item D)) $u "
            "(collapse (match (context-space) (Item $x) $x)))","((B C D))",quantum);
        registry_bind(&partial,"&raw",atom_space(&arena,&region->world));
        registry_bind(&partial,"&visible",atom_space(&arena,parent));
        ordinary_source_payload_value(&arena,&context_only,
            "(chain (add-atom &raw (Item D)) $u "
            "(Pair (== &raw &visible) "
            "(Pair (collapse (match &raw (Item $x) $x)) "
            "(collapse (match &visible (Item $y) $y)))))",
            "((Pair True (Pair (B C D) (B C D))))",quantum);
        if(missing) ordinary_source_payload_value(&arena,&context_only,"&self","(7)",quantum);
        CHECK(space_contains_exact(parent,parse_one(&arena,"(Item C)")) &&
            !space_contains_exact(parent,parse_one(&arena,"(Item D)")) &&
            !space_contains_exact(&region->world,parse_one(&arena,"(Item C)")));

        /* A visibility redirect carries no permission to mutate a borrowed
         * parent view. Child writes need a separately owned overlay. */
        CHECK(eval_payload_redirects_begin(persistent));
        CHECK(eval_payload_note_space_redirect(&region->world,parent));
        Space *child=payload_resolve_space_write(&region->world);
        CHECK(child && child!=parent);
        if(child && child!=parent) space_add(child,parse_one(&arena,"(Item D)"));
        CHECK(child && space_contains_exact(child,parse_one(&arena,"(Item D)")) &&
            !space_contains_exact(parent,parse_one(&arena,"(Item D)")));
        eval_payload_redirects_end();
        CHECK(eval_resolve_space_read(&region->world)==parent);

        /* Pushing an empty frame cannot change the current observation,
         * including a raw-key override or an explicit identity redirect. */
        Space shadow;space_init_overlay(&shadow,&region->world);
        CHECK(eval_payload_redirects_begin(persistent));
        CHECK(eval_payload_note_space_redirect(&region->world,&shadow));
        CHECK(eval_resolve_space_read(&region->world)==&shadow);
        CHECK(eval_payload_redirects_begin(persistent));
        CHECK(eval_resolve_space_read(&region->world)==&shadow);
        eval_payload_redirects_end();
        CHECK(eval_payload_note_space_redirect(&region->world,&region->world));
        CHECK(eval_payload_redirects_begin(persistent));
        CHECK(eval_resolve_space_read(&region->world)==&region->world);
        eval_payload_redirects_end();eval_payload_redirects_end();
        CHECK(eval_resolve_space_read(&region->world)==parent);
        space_free(&shadow);registry_free(&partial);
        eval_set_payload_transactional(transactional);
        eval_payload_redirects_end();
        space_free(&parent_code);
    }
    arena_free(&arena);
    eval_prime_source_scope_close(scope);
    eval_prime_source_owner_release(owner);
    CHECK(!g_library_context && !g_registry && !g_prime_owned_region);
}

/* A producer closes over its actual earlier query. Later invocations read
 * the reached private data world, without repeating that producer's write.
 * Neither a ready code value nor an indirect head grants another service. */
static uint64_t ordinary_source_closure_case(uint64_t quantum,uint32_t workers,
    unsigned recording) {
    const char *program=
        "(Item A) (Item A) (Item B) "
        "(= (make-code) (chain (add-atom &self (Made once)) $m "
        "(chain (collapse (match &self (Item $x) $x)) $old "
        "(function (return (chain (add-atom &self (Used once)) $u "
        "(Pair $old (collapse (match &self (Item $x) $x)))))))))";
    const char *query=
        "(chain (make-code) $f "
        "(chain (add-atom &self (Item C)) $c "
        "(chain (eval $f) $first "
        "(chain (add-atom &self (Item D)) $d "
        "(chain (eval $f) $second (Both $first $second))))))";
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(program,query,
        quantum,workers,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    CettaOwnedReturnImagePolicy policy={.bounded_history=CETTA_RETURN_HISTORY_RETAIN};
    OrdinarySourceDelivery delivery={.policy=&policy};
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNativeHistorySpec spec={.enabled=true,.selections=4096u,.nodes=16384u,
        .bytes=4u*1024u*1024u,.owners=8u};
    if(recording)CHECK(eval_prime_source_execution_record(&execution,&spec)==CETTA_CONTINUATION_READY);
#else
    assert(!recording);
#endif
    bool changed=false;
    for(unsigned slice=0u;slice<10000u && !execution.has_pending;slice++) {
        uint64_t before=owned->ordinary_control->steps;
        OrdinarySourceDelivery refused={.refuse=true};
        CettaOwnedExecutionStep step=cetta_owned_execution_sequence(&execution,1u,
            ordinary_source_collect,&refused);
        CHECK(step==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps-before<=quantum);
        if(!changed && space_length64(&region->world)>4u) {
            changed=true;
            CHECK(!execution.accepted);
            CHECK(space_length64(owner->space)==4u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            if(recording==2u) {
                spec.enabled=false;spec.stop=true;
                uint64_t retained=owned->ordinary_control->steps;
                CHECK(eval_prime_source_execution_record(&execution,&spec)==CETTA_CONTINUATION_READY);
                CHECK(eval_prime_source_execution_forget(&execution)==CETTA_CONTINUATION_READY);
                CHECK(owned->ordinary_control->steps==retained);
            }
#endif
        }
    }
    CHECK(changed && execution.has_pending && !execution.accepted);
    uint64_t paid=owned->ordinary_control->steps;
    CettaOwnedReturn pending=execution.pending;
    for(unsigned retry=0u;retry<3u;retry++) {
        OrdinarySourceDelivery refused={.refuse=true};
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&refused)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps==paid && execution.has_pending &&
            execution.pending.outcome.term==pending.outcome.term &&
            execution.pending.environment==pending.environment);
    }
    for(unsigned slice=0u;slice<10000u && !execution.terminal;slice++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
        execution.completion==CETTA_EVAL_COMPLETE);
    CHECK(eval_prime_source_owner_is_current(owner));
    Arena scratch;arena_init_detached(&scratch);
    Atom *made=parse_one(&scratch,"(Made once)"),*used=parse_one(&scratch,"(Used once)");
    unsigned made_count=0u,used_count=0u;
    for(CettaIndex i=0u;i<space_length64(&region->world);i++) {
        Atom *row=space_get_at64(&region->world,i);
        if(atom_eq(row,made))made_count++;
        if(atom_eq(row,used))used_count++;
    }
    CHECK(made_count==1u && used_count==2u);
    CHECK(space_length64(owner->space)==4u && space_length64(&region->world)==9u);
    paid=owned->ordinary_control->steps;
    arena_free(&scratch);
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    const char *expected[]={"(Both (Pair (A A B) (A A B C)) (Pair (A A B) (A A B C D)))"};
    ordinary_source_delivery_check(&delivery,expected,1u);
    return paid;
}

static void ordinary_source_indirect_case(unsigned shape) {
    bool succeeds=shape==2u || shape==5u;
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(
        shape==3u?"(= (extract (lam $v (Box $f))) $f)":
        shape==2u?"(= (same-code $x) (Pair $x $x))":"",shape==5u?
        "(let $f bind! (unify A B ($f &escaped 9) safe))":shape==4u?
        "(let $f bind! (unify A A ($f &escaped 9) failed))":shape==3u?
        "(let $f (extract ((lam x (lam y (Box (lam z (x &escaped 9))))) bind!)) "
        "($f ignored))":shape==2u?
        "(chain same-code $f ($f 7))":shape==1u?
        "(chain capture $f ($f (add-atom &self (Escaped yes))))":
        "(let $f bind! ($f &escaped 9))",1u,1u,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    PrimeNativeOwnedRegion *region=owner->owned_region;
    uint32_t names=owner->registry->len;
    OrdinarySourceDelivery delivery={0};
    for(unsigned slice=0u;slice<10000u && !execution.terminal;slice++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    if(!succeeds && (execution.stop!=CETTA_OWNED_EXECUTION_HANDOFF || delivery.count)) {
        Arena scratch;arena_init_detached(&scratch);
        fprintf(stderr,"indirect refusal shape=%u stop=%u completion=%u rows=%zu failed=%u\n",
            shape,(unsigned)execution.stop,(unsigned)execution.completion,delivery.count,
            ((CettaPrimeObservationReturn *)execution.computation)->ordinary_control->failed);
        for(size_t i=0u;i<delivery.count;i++)fprintf(stderr,"indirect row: %s\n",
            atom_to_string(&scratch,cetta_owned_return_image_view(delivery.images[i])->outcome.term));
        arena_free(&scratch);
    }
    CHECK(execution.terminal && execution.stop==(succeeds?
        CETTA_OWNED_EXECUTION_COMPLETE:CETTA_OWNED_EXECUTION_HANDOFF));
    CHECK(execution.completion==(succeeds?
        CETTA_EVAL_COMPLETE:CETTA_EVAL_INCOMPLETE_EFFECT_DENIED));
    CHECK(delivery.count==(succeeds?1u:0u));
    CHECK(owner->registry->len==names && space_length64(&region->world)==
        (shape==2u || shape==3u ? 1u : 0u));
    CHECK(eval_prime_source_owner_is_current(owner));
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    const char *expected[]={shape==5u?"safe":"(Pair 7 7)"};
    ordinary_source_delivery_check(&delivery,expected,succeeds?1u:0u);
}

/* A host failure at a retained boundary may follow a paid effect. The
 * generic failed flag must not be interpreted as a permission verdict. */
static void ordinary_source_host_failure_case(void) {
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile("(Item A)",
        "(chain (add-atom &self (Before once)) $u (+ 1 2))",1u,1u,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    OrdinarySourceDelivery delivery={0};
    for(unsigned slice=0u;slice<1000u && space_length64(&region->world)==1u;slice++) {
        uint64_t before=owned->ordinary_control->steps;
        CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        CHECK(owned->ordinary_control->steps-before<=1u);
    }
    CHECK(!execution.terminal && !execution.has_pending && !delivery.count);
    CHECK(space_length64(&region->world)==2u);
    CHECK(owned->ordinary_control->completion==CETTA_EVAL_COMPLETE);
    uint64_t paid=owned->ordinary_control->steps;
    CHECK(paid>0u);
    owned->ordinary_control->failed=true;
    CHECK(cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery)
        ==CETTA_OWNED_EXECUTION_HANDOFF);
    CHECK(execution.terminal && execution.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    CHECK(owned->ordinary_control->steps==paid && !delivery.count);
    CHECK(space_length64(&region->world)==2u && eval_prime_source_owner_is_current(owner));
    cetta_owned_execution_cancel(&execution);eval_prime_source_owner_release(owner);
    ordinary_source_delivery_check(&delivery,NULL,0u);
}

typedef struct {
    CettaOwnedTask *task;
    OrdinarySourceDelivery delivery;
    unsigned entrances, deliveries, refusals, blocked;
    bool permitted, revoke_on_delivery;
} OrdinarySourceTaskContext;

static void ordinary_source_task_retain(void *raw) {eval_prime_source_owner_retain(raw);}
static void ordinary_source_task_release(void *raw) {eval_prime_source_owner_release(raw);}
static bool ordinary_source_task_leaf(void *raw,const Atom *leaf,CettaReturnLeafOwner *hold) {
    CettaPrimeSourceOwner *owner=raw;
    /* A retained Need snapshot can still name this mutable private world.
     * Share its actual lifetime explicitly; no execution permission follows. */
    if(!owner || leaf->kind!=ATOM_GROUNDED || leaf->ground.gkind!=GV_SPACE ||
        leaf->ground.ptr!=&owner->owned_region->world)return false;
    *hold=(CettaReturnLeafOwner){.owner=owner,.retain=ordinary_source_task_retain,
        .release=ordinary_source_task_release};
    return true;
}

/* Reusable code is an owned value, not a display of its producer's source.
 * Dispose the original controller, then exercise the ordinary public receiver
 * in the retained world. Equal producer text does not identify distinct cells. */
static void ordinary_source_exported_closure_case(unsigned shape,bool invalidate) {
    const char *program=shape>=4u
        ? "(= (pick $x) (chain (add-atom &self (Made once)) $m $x))"
        : shape==2u
        ? "(= (pick) (chain (add-atom &self (Made once)) $m (superpose (7 7 9))))"
        : "(= (pick) (chain (add-atom &self (Made once)) $m (+ 40 2)))";
    const char *query=shape>=4u
        ? "(let $f ((lam x (lam y (Pair x x))) (pick $outer)) (unify $outer A $f failed))"
        : shape==1u
        ? "((lam x ((lam z (lam y (Pair x z))) (pick))) (pick))"
        : shape==3u
        ? "((lam x (chain x $v (lam y (Pair x x)))) (pick))"
        : "((lam x (lam y (Pair x x))) (pick))";
    CettaOwnedExecution execution={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(program,query,
        1u,1u,CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    CettaOwnedReturnImagePolicy policy={.bounded_history=CETTA_RETURN_HISTORY_RETAIN,
        .describe_leaf_owner=ordinary_source_task_leaf,.context=owner};
    OrdinarySourceDelivery delivery={.policy=&policy};
    for(unsigned poll=0u;poll<100000u && !execution.terminal;poll++)
        cetta_owned_execution_sequence(&execution,1u,ordinary_source_collect,&delivery);
    CHECK(execution.terminal && execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
        execution.completion==CETTA_EVAL_COMPLETE && delivery.count==1u);
    assert(delivery.count==1u);
    CettaPrimeSourceOwner *receiver=owner;
    eval_prime_source_owner_retain(receiver);
    size_t before=(size_t)space_length64(&receiver->owned_region->world);
    CHECK(before==(shape==3u?2u:1u));
    cetta_owned_execution_cancel(&execution);
    eval_prime_source_owner_release(owner);owner=NULL;
    CHECK(!execution.computation && eval_prime_source_owner_is_current(receiver));
    const CettaOwnedReturn *value=cetta_owned_return_image_view(delivery.images[0]);
    assert(value && value->environment);
    Arena arena;arena_init_detached(&arena);
    Atom *call=atom_expr2(&arena,value->outcome.term,atom_symbol(&arena,"ignored"));
    Atom *second=call;
    if(shape==5u) {
        const PrimeNeedSnapshot *snapshot=bindings_need_view(value->environment);
        PrimeNeedHistoryIterator history;PrimeNeedHistoryView entry;
        assert(prime_need_snapshot_history_begin(snapshot,&history));
        uint64_t captured=0u;
        while(prime_need_snapshot_history_next(&history,&entry)==PRIME_NEED_HISTORY_ENTRY)
            if(atom_head_symbol_id(entry.cell.origin)==symbol_intern_cstr(g_symbols,"pick")) {
                captured=entry.thunk_id;break;
            }
        assert(captured);
        Atom *field=value->outcome.term->expr.elems[2]->expr.elems[1];
        uint64_t same=0u;
        CHECK(prime_need_ref_belongs_to(field,snapshot,&same) && same==captured);
        second=prime_need_ref(&arena,snapshot,captured);
    }
    call=atom_expr3(&arena,atom_symbol(&arena,"Both"),call,second);
    CettaPrimeSourceScope *scope=NULL;
    if(invalidate) {
        /* Deliberately violate the source contract without changing the
         * image: retaining lifetime cannot repair a stale program view. */
        space_add(receiver->space,parse_one(&arena,"(= (changed-program) 1)"));
        CHECK(!eval_prime_source_owner_is_current(receiver));
        CHECK(eval_prime_source_scope_open(receiver,0u,&scope)==CETTA_CONTINUATION_INVALIDATED && !scope);
        CHECK(space_length64(&receiver->owned_region->world)==before);
    } else {
        assert(eval_prime_source_scope_open(receiver,0u,&scope)==CETTA_CONTINUATION_READY);
        Space *code=NULL;Registry *registry=NULL;Arena *persistent=NULL;
        assert(eval_prime_source_scope_inputs(scope,&code,&registry,&persistent,NULL));
        Space wrong;space_init(&wrong);
        Registry wrong_registry;registry_init(&wrong_registry);
        Arena wrong_persistent;arena_init_detached(&wrong_persistent);
        for(unsigned damage=0u;damage<3u;damage++) {
            EvalOutcome refused;eval_outcome_init(&refused);
            eval_top_with_registry_outcome(damage==0u?&wrong:code,&arena,
                damage==2u?&wrong_persistent:persistent,
                damage==1u?&wrong_registry:registry,call,&refused,NULL,NULL);
            CHECK(refused.completion==CETTA_EVAL_INCOMPLETE_INVALIDATED &&
                !refused.results.len && !refused.prime_observation &&
                space_length64(&receiver->owned_region->world)==before);
            eval_outcome_free(&refused);
        }
        arena_free(&wrong_persistent);registry_free(&wrong_registry);space_free(&wrong);
        EvalOutcome outcome;eval_outcome_init(&outcome);
        OutcomeSet resumed;outcome_set_init(&resumed);
        /* The native receiving call consumes the full environment, including
         * logical constraints and the exact same cell used by a mixed alias. */
        PrimeNeedActiveGuard active;prime_need_active_enter(&active,value->environment);
        EvalCompletionTracker tracker;eval_completion_child_begin(&tracker);
        metta_call_with_env(code,&arena,call,NULL,-1,value->environment,&resumed);
        outcome.completion=eval_completion_child_end(&tracker);
        prime_need_active_leave(&active);
        for(CettaCount i=0u;i<resumed.len;i++)
            result_set_add(&outcome.results,outcome_atom_materialize(&arena,&resumed.items[i]));
        const char *expected[]={"(Both (Pair 7 7) (Pair 7 7))",
            "(Both (Pair 7 7) (Pair 7 7))","(Both (Pair 9 9) (Pair 9 9))"};
        CHECK(outcome.completion==CETTA_EVAL_COMPLETE && !outcome.prime_observation &&
            outcome.results.len==(shape==2u?3u:1u));
        for(CettaCount i=0u;i<outcome.results.len;i++) {
            Atom *wanted=parse_one(&arena,shape==2u && i<3u?expected[i]:
                shape==5u?"(Both (Pair A A) A)":
                shape==4u?"(Both (Pair A A) (Pair A A))":
                    "(Both (Pair 42 42) (Pair 42 42))");
            CHECK(atom_eq(outcome.results.items[i],wanted));
        }
        size_t made=0u;
        Atom *effect=parse_one(&arena,"(Made once)");
        for(CettaIndex i=0u;i<space_length64(&receiver->owned_region->world);i++)
            if(atom_eq(space_get_at64(&receiver->owned_region->world,i),effect))made++;
        CHECK(made==(shape==1u?2u:1u));
        if(resumed.len) {
            /* Continue from the first branch's actual returned world. The
             * original image remains immutable and is not the updated world. */
            const Bindings *after=&resumed.items[0].env;
            OutcomeSet again;outcome_set_init(&again);
            prime_need_active_enter(&active,after);
            eval_completion_child_begin(&tracker);
            metta_call_with_env(code,&arena,call,NULL,-1,after,&again);
            CHECK(eval_completion_child_end(&tracker)==CETTA_EVAL_COMPLETE && again.len==1u);
            prime_need_active_leave(&active);
            Atom *wanted=parse_one(&arena,shape==2u?expected[0]:shape==5u?"(Both (Pair A A) A)":
                shape==4u?"(Both (Pair A A) (Pair A A))":"(Both (Pair 42 42) (Pair 42 42))");
            CHECK(again.len==1u && atom_eq(outcome_atom_materialize(&arena,&again.items[0]),wanted));
            size_t after_count=0u;
            for(CettaIndex i=0u;i<space_length64(&receiver->owned_region->world);i++)
                if(atom_eq(space_get_at64(&receiver->owned_region->world,i),effect))after_count++;
            CHECK(after_count==made);
            outcome_set_free(&again);
        }
        eval_outcome_free(&outcome);outcome_set_free(&resumed);eval_prime_source_scope_close(scope);
        CHECK(eval_prime_source_owner_is_current(receiver));
    }
    cetta_owned_return_image_release(delivery.images[0]);
    eval_prime_source_owner_release(receiver);arena_free(&arena);
    CHECK(!g_library_context && !g_registry && !g_prime_ordinary_return);
}

static void owned_internal_need_capability_case(void) {
    Arena source;arena_init_detached(&source);
    PrimeNeedSnapshot root={0},world;
    uint64_t cell=0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&source,&root,atom_int(&source,42),&world,&cell));
    Bindings environment;bindings_init(&environment);
    *bindings_need_mut(&environment)=world;
    const CettaPrimeNeedCapability *minted=atom_prime_need_capability_value(
        prime_need_ref(&source,&world,cell));
    assert(minted);
    Atom *reference=atom_prime_need_capability_with_rights(&source,minted->session_id,
        minted->thunk_id,minted->authority_id,CETTA_PRIME_NEED_RIGHT_INSPECT);
    CettaPrimeNeedCapability identity=*atom_prime_need_capability_value(reference);
    CettaOwnedReturn returned={.outcome=cetta_call_completed_value(reference),.environment=&environment};
    CettaOwnedReturnImage *image=cetta_owned_return_image_capture(&returned,NULL);
    CHECK(image!=NULL);assert(image);
    for(unsigned damage=0u;damage<3u;damage++) {
        Atom *forged=atom_prime_need_capability_with_rights(&source,
            identity.session_id+(damage==0u),identity.thunk_id+(damage==1u),
            identity.authority_id+(damage==2u),identity.rights);
        returned.outcome.term=forged;
        CHECK(cetta_owned_return_image_capture(&returned,NULL)==NULL);
    }
    returned.outcome.term=reference;returned.environment=NULL;
    CHECK(cetta_owned_return_image_capture(&returned,NULL)==NULL);
    PrimeNeedSnapshot running;
    assert(prime_need_snapshot_start_evaluation(&source,&world,cell,83u,&running));
    *bindings_need_mut(&environment)=running;
    returned.environment=&environment;
    CHECK(cetta_owned_return_image_capture(&returned,NULL)==NULL);
    bindings_free(&environment);arena_free(&source);
    const CettaOwnedReturn *owned=cetta_owned_return_image_view(image);
    uint32_t rights=0u;
    const PrimeNeedSnapshot *copied=bindings_need_view(owned->environment);
    CHECK(prime_need_ref_parse_rights(owned->outcome.term,NULL,NULL,NULL,&rights) &&
        rights==CETTA_PRIME_NEED_RIGHT_INSPECT &&
        prime_need_ref_belongs_to(owned->outcome.term,copied,NULL) &&
        !prime_need_ref_belongs_to_with_rights(owned->outcome.term,copied,CETTA_PRIME_NEED_RIGHT_FORCE,NULL));
    PrimeNeedCellView kept;
    CHECK(prime_need_snapshot_lookup(copied,cell,&kept) && kept.authority_id==identity.authority_id &&
        kept.origin->kind==ATOM_GROUNDED && kept.origin->ground.ival==42);
    cetta_owned_return_image_release(image);
}

static CettaOwnedTaskAdmission ordinary_source_task_admit(void *raw) {
    OrdinarySourceTaskContext *context=raw;
    context->entrances++;
    if(context->revoke_on_delivery && context->task->execution.has_pending) {
        context->revoke_on_delivery=false;
        context->permitted=false;
    }
    if(!context->permitted) {
        context->blocked++;
        return CETTA_OWNED_TASK_ADMISSION_REFUSED;
    }
    if(!eval_prime_source_execution_is_current(&context->task->execution)) {
        context->blocked++;
        return CETTA_OWNED_TASK_ADMISSION_INVALIDATED;
    }
    return CETTA_OWNED_TASK_ADMISSION_READY;
}
static bool ordinary_source_task_consume(void *raw,const CettaOwnedReturn *returned) {
    OrdinarySourceTaskContext *context=raw;
    context->deliveries++;
    if(context->refusals) {context->refusals--;return false;}
    return ordinary_source_collect(&context->delivery,returned);
}

/* Dispatch actual retained calls, rather than a preselected command word.
 * A divergent yielding task cannot hold up a finite captured-code invocation.
 * Publication permission, paid work and ready membership remain distinct. */
static uint64_t ordinary_source_task_case(uint64_t quantum,uint32_t workers,bool recording) {
    const char *program="(Item A) (Item A) (Item B) "
        "(= (make-code) (chain (add-atom &self (Made once)) $m "
        "(chain (collapse (match &self (Item $x) $x)) $old "
        "(function (return (Pair $old (collapse (match &self (Item $x) $x))))))))";
    const char *query="(chain (make-code) $f "
        "(chain (add-atom &self (Item C)) $c (eval $f)))";
    CettaOwnedTask tasks[3]={0};
    CettaOwnedExecution admitted[3]={0};
    CettaPrimeSourceOwner *owners[3];
    owners[0]=ordinary_source_driver_open(
        "(= (service-loop $n) (service-loop (+ $n 1)))",
        "(service-loop 0)",quantum,workers,&admitted[0]);
    owners[1]=ordinary_source_driver_open_profile(program,query,quantum,workers,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&admitted[1]);
    owners[2]=ordinary_source_driver_open(ordinary_weighted_program,"(weighted-outer)",
        quantum,workers,&admitted[2]);
    CettaPrimeObservationReturn *owned[3];
    CettaOwnedReturnImagePolicy policies[3]={0};
    OrdinarySourceTaskContext contexts[3]={0};
    for(unsigned i=0u;i<3u;i++) {
        owned[i]=admitted[i].computation;
        policies[i]=(CettaOwnedReturnImagePolicy){.bounded_history=CETTA_RETURN_HISTORY_RETAIN,
            .describe_leaf_owner=ordinary_source_task_leaf,.context=owners[i]};
        contexts[i]=(OrdinarySourceTaskContext){.task=&tasks[i],.permitted=true,
            .delivery={.policy=&policies[i]}};
        CHECK(!cetta_owned_task_init(&tasks[i],&admitted[i],NULL,
            ordinary_source_task_consume,&contexts[i]));
        CHECK(admitted[i].computation==owned[i] && !tasks[i].execution.computation);
        CHECK(cetta_owned_task_init(&tasks[i],&admitted[i],ordinary_source_task_admit,
            ordinary_source_task_consume,&contexts[i]));
        CHECK(!admitted[i].computation && tasks[i].execution.computation==owned[i]);
        CHECK(eval_prime_source_execution_is_current(&tasks[i].execution));
        cetta_owned_execution_cancel(&admitted[i]);
    }
    contexts[1].revoke_on_delivery=true;
    contexts[2].refusals=10000u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(recording) {
        PrimeNativeHistorySpec spec={.enabled=true,.selections=4096u,.nodes=16384u,
            .bytes=4u*1024u*1024u,.owners=8u};
        CHECK(eval_prime_source_execution_record(&tasks[1].execution,&spec)==CETTA_CONTINUATION_READY);
    }
#else
    assert(!recording);
#endif
    CettaReadyQueue ready={.capacity=3u},other={.capacity=3u};
    CHECK(cetta_ready_admit(&ready,&tasks[0].ready)==READY_ADMITTED);
    CHECK(cetta_ready_admit(&ready,&tasks[1].ready)==READY_ADMITTED);
    CHECK(cetta_ready_admit(&ready,&tasks[0].ready)==READY_PRESENT && ready.count==2u);
    CHECK(cetta_ready_admit(&other,&tasks[0].ready)==READY_FOREIGN && !other.count);
    CettaOwnedTaskAccount zero_account;
    CettaOwnedVisit zero=cetta_owned_task_visit_accounted(&ready,false,&zero_account);
    CHECK(zero.task==&tasks[0] && zero.step==CETTA_OWNED_EXECUTION_PAUSED &&
        zero.admission==CETTA_OWNED_TASK_ADMISSION_UNCHECKED &&
        !contexts[0].entrances && !owned[0]->ordinary_control->steps);
    CHECK(ready.first==&tasks[1].ready && ready.count==2u);
    CHECK(zero_account.rotations==1u && !zero_account.admission &&
        !zero_account.producer && !zero_account.consumer &&
        !zero_account.final_is_current && !zero_account.retirements &&
        !zero_account.phase_overflow && zero_account.phase_count==1u &&
        zero_account.phases[0]==CETTA_OWNED_TASK_PHASE_ROTATION);
    CettaOwnedReturn held={0};
    CettaOwnedReturnImage *earned=NULL;
    uint64_t held_work=0u;
    bool arrived=false,cancelled=false,unblocked=false;
    unsigned last[3]={0},visits[3]={0};
    for(unsigned slot=1u;slot<3000u && ready.count;slot++) {
        CettaOwnedTask *expected=(CettaOwnedTask *)ready.first;
        unsigned selected=(unsigned)(expected-tasks);
        assert(selected<3u);
        uint64_t before[3];
        for(unsigned i=0u;i<3u;i++)before[i]=owned[i]?owned[i]->ordinary_control->steps:0u;
        unsigned entrances=contexts[selected].entrances,deliveries=contexts[selected].deliveries;
        bool held_before=expected->execution.has_pending;
        CettaOwnedTaskAccount account;
        CettaOwnedVisit visit=slot%2u?cetta_owned_task_visit(&ready,true):
            cetta_owned_task_visit_accounted(&ready,true,&account);
        CHECK(visit.task==expected);
        if(!(slot%2u)) {
            CHECK(account.rotations==1u && account.admission<=2u &&
                account.producer<=1u && account.consumer<=1u &&
                account.final_is_current<=1u && account.retirements<=1u);
            CHECK(!account.phase_overflow && account.phase_count==
                account.rotations+account.admission+account.producer+
                account.consumer+account.final_is_current+account.retirements &&
                account.phases[0]==CETTA_OWNED_TASK_PHASE_ROTATION);
            CHECK(account.admission==contexts[selected].entrances-entrances);
            CHECK(account.consumer==contexts[selected].deliveries-deliveries);
            CHECK(!held_before || !account.producer);
            CHECK(account.retirements==(expected->ready.owner?0u:1u));
        }
        CHECK(!last[selected] || slot-last[selected]<=3u);
        last[selected]=slot;visits[selected]++;
        for(unsigned i=0u;i<3u;i++)if(owned[i]) {
            uint64_t delta=owned[i]->ordinary_control->steps-before[i];
            CHECK(i==selected?delta<=quantum:delta==0u);
        }
        if(!arrived && tasks[1].execution.has_pending) {
            CHECK(visit.admission==CETTA_OWNED_TASK_ADMISSION_REFUSED &&
                !contexts[1].permitted &&
                !tasks[1].execution.accepted && !contexts[1].deliveries);
            held=tasks[1].execution.pending;held_work=owned[1]->ordinary_control->steps;
            CHECK(!cetta_owned_return_image_capture(&held,NULL));
            earned=cetta_owned_return_image_capture(&held,&policies[1]);
            CHECK(earned && eval_prime_source_execution_is_current(&tasks[1].execution));
            assert(earned);
            CHECK(cetta_ready_admit(&ready,&tasks[2].ready)==READY_ADMITTED && ready.count==3u);
            CHECK(!owned[2]->ordinary_control->steps);
            arrived=true;
        }
        if(arrived && !unblocked && contexts[1].blocked>=3u) {
            CHECK(tasks[1].execution.pending.outcome.term==held.outcome.term &&
                tasks[1].execution.pending.environment==held.environment &&
                owned[1]->ordinary_control->steps==held_work);
            contexts[1].permitted=true;contexts[1].refusals=2u;unblocked=true;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            if(recording) {
                Arena inspection;arena_init_detached(&inspection);
                Atom *history=NULL;
                CHECK(eval_prime_source_execution_history(&tasks[1].execution,
                    &inspection,&history)==CETTA_CONTINUATION_READY);
                CHECK(history && history->kind==ATOM_EXPR);
                CHECK(eval_prime_source_execution_forget(&tasks[1].execution)==CETTA_CONTINUATION_READY);
                /* A controller recorder need not attach a bounded occurrence
                 * receipt to every returned value. Readers that were actually
                 * acquired keep their share; an absent receipt stays absent. */
                const PrimeNeedReceipt *receipt=bindings_receipt_view(
                    cetta_owned_return_image_view(earned)->environment);
                CHECK(receipt->recording?prime_need_recording_readable(receipt):!receipt->top);
                arena_free(&inspection);
            }
#endif
        }
        if(unblocked && tasks[1].execution.has_pending)
            CHECK(owned[1]->ordinary_control->steps==held_work && !tasks[1].execution.accepted);
        if(!cancelled && tasks[1].execution.terminal && tasks[2].execution.has_pending) {
            CHECK(tasks[1].execution.stop==CETTA_OWNED_EXECUTION_COMPLETE &&
                !tasks[1].ready.owner && !tasks[0].execution.terminal &&
                visits[0]>1u && !contexts[0].delivery.count);
            uint64_t sibling=owned[2]->ordinary_control->steps;
            CettaOwnedReturn loan=tasks[2].execution.pending;
            cetta_owned_task_cancel(&tasks[0]);owned[0]=NULL;
            CHECK(ready.count==1u && ready.first==&tasks[2].ready &&
                tasks[2].execution.pending.outcome.term==loan.outcome.term &&
                owned[2]->ordinary_control->steps==sibling && !tasks[2].execution.accepted);
            contexts[2].refusals=0u;cancelled=true;
        }
    }
    CHECK(arrived && unblocked && cancelled && !ready.count);
    CHECK(contexts[1].deliveries==3u && tasks[1].execution.accepted==1u &&
        tasks[2].execution.accepted==2u && tasks[2].execution.terminal);
    CHECK(!cetta_owned_task_visit(&ready,true).task);
    uint64_t paid=owned[1]->ordinary_control->steps;
    ordinary_source_factors(&contexts[2].delivery);
    for(unsigned i=0u;i<3u;i++) {
        cetta_owned_task_cancel(&tasks[i]);
        CHECK(!tasks[i].ready.owner && !eval_prime_source_execution_is_current(&tasks[i].execution));
        eval_prime_source_owner_release(owners[i]);
    }
    const char *expected[]={"(Pair (A A B) (A A B C))"},*weighted[]={"V","V"};
    CHECK(atom_eq(cetta_owned_return_image_view(earned)->outcome.term,
        cetta_owned_return_image_view(contexts[1].delivery.images[0])->outcome.term));
    cetta_owned_return_image_release(earned);
    ordinary_source_delivery_check(&contexts[0].delivery,NULL,0u);
    ordinary_source_delivery_check(&contexts[1].delivery,expected,1u);
    ordinary_source_delivery_check(&contexts[2].delivery,weighted,2u);
    return paid;
}

static void ordinary_source_task_stale_case(bool pending) {
    CettaOwnedExecution execution={0};CettaOwnedTask task={0};
    CettaPrimeSourceOwner *owner=ordinary_source_driver_open_profile(
        "(Item A)","(match &self (Item $x) (Seen $x))",1u,1u,
        CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&execution);
    CettaPrimeObservationReturn *owned=execution.computation;
    if(pending)ordinary_source_pending(&execution,1u);
    OrdinarySourceTaskContext context={.task=&task,.permitted=true};
    CHECK(cetta_owned_task_init(&task,&execution,ordinary_source_task_admit,
        ordinary_source_task_consume,&context));
    CettaReadyQueue ready={.capacity=1u};
    CHECK(cetta_ready_admit(&ready,&task.ready)==READY_ADMITTED);
    CettaPrimeSourceScope *scope=NULL;
    assert(eval_prime_source_scope_open(owner,0u,&scope)==CETTA_CONTINUATION_READY);
    Arena *arena=NULL;
    assert(eval_prime_source_scope_inputs(scope,NULL,NULL,&arena,NULL));
    space_add(&owner->owned_region->world,parse_one(arena,"(ChangedSource 9)"));
    eval_prime_source_scope_close(scope);
    uint64_t paid=owned->ordinary_control->steps;
    CettaOwnedReturn loan=task.execution.pending;
    CettaOwnedVisit visit=cetta_owned_task_visit(&ready,true);
    CHECK(visit.task==&task &&
        visit.admission==CETTA_OWNED_TASK_ADMISSION_INVALIDATED &&
        !context.deliveries && ready.count==1u);
    CHECK(!eval_prime_source_execution_is_current(&task.execution) &&
        owned->ordinary_control->steps==paid && task.execution.has_pending==pending &&
        task.execution.pending.outcome.term==loan.outcome.term && !task.execution.terminal);
    cetta_owned_task_cancel(&task);eval_prime_source_owner_release(owner);
    CHECK(!ready.count && !task.ready.owner);
}

static void ordinary_source_shared_cases(void) {
    work_credit_cases();
    ordinary_source_credit_case("","(empty)",NULL,0u,1000u,true);
    ordinary_source_credit_case("(= (source-loop $n) (source-loop (+ $n 1)))",
        "(source-loop 0)",NULL,0u,0u,false);
    ordinary_source_credit_case("(= (source-loop $n) (source-loop (+ $n 1)))",
        "(source-loop 0)",NULL,0u,11u,false);
    ordinary_source_credit_case("(= (source-loop $n) (source-loop (+ $n 1)))",
        "(source-loop 0)",NULL,0u,UINT64_MAX,false);
    ordinary_source_credit_case(ordinary_shared_program,"(same (pick))",
        ordinary_shared_expected,3u,10000u,true);
    ordinary_source_recredit_case();
    uint64_t service_work=ordinary_source_task_case(1u,1u,false);
    CHECK(ordinary_source_task_case(7u,1u,false)==service_work);
    ordinary_source_task_case(1u,2u,false);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(ordinary_source_task_case(1u,1u,true)==service_work);
#endif
    ordinary_source_task_stale_case(false);
    ordinary_source_task_stale_case(true);
    ordinary_source_host_failure_case();
    uint64_t closure_work=ordinary_source_closure_case(1u,1u,0u);
    CHECK(ordinary_source_closure_case(7u,1u,0u)==closure_work);
    ordinary_source_closure_case(1u,2u,0u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(ordinary_source_closure_case(1u,1u,1u)==closure_work);
    CHECK(ordinary_source_closure_case(1u,1u,2u)==closure_work);
#endif
    for(unsigned shape=0u;shape<6u;shape++)ordinary_source_indirect_case(shape);
    ordinary_source_private_readout_case(false);
    ordinary_source_private_readout_case(true);
    for(unsigned retained=0u;retained<2u;retained++) {
        ordinary_source_payload_world_case(1u,retained!=0u);
        ordinary_source_payload_world_case(7u,retained!=0u);
    }
    for(uint64_t quantum=1u;quantum<=7u;quantum+=6u) {
        ordinary_source_imported_context_case(quantum,false);
        ordinary_source_imported_context_case(quantum,true);
    }
    for(uint32_t workers=1u;workers<=2u;workers++)
        for(unsigned remove=0u;remove<2u;remove++)
            ordinary_source_private_world_case(workers,remove!=0u);
    for(unsigned private_data=0u;private_data<2u;private_data++) {
        ordinary_source_weighted_case(false,private_data!=0u);
        ordinary_source_weighted_case(true,private_data!=0u);
    }
    for(unsigned mode=1u;mode<=6u;mode++)ordinary_source_weighted_history_case(mode);
    for(uint32_t workers=1u;workers<=2u;workers++)
        for(uint64_t quantum=1u;quantum<=7u;quantum+=6u) {
            uint64_t ordinary_work=ordinary_source_shared_case(quantum,workers,false);
            CHECK(ordinary_source_shared_case(quantum,workers,true)==ordinary_work);
        }
    for(unsigned pending=0u;pending<2u;pending++) {
        ordinary_source_detach_case(pending!=0u);
        ordinary_source_currency_case(pending!=0u,false);
        ordinary_source_currency_case(pending!=0u,true);
    }
    const char *compound[]={"(Pair 7 (Tag 9))"},*empty[]={"()"},*number[]={"42"};
    ordinary_source_stream_case("","(Pair 7 (Tag 9))",compound,1u,true);
    ordinary_source_stream_case("","()",empty,1u,true);
    ordinary_source_stream_case("","(empty)",NULL,0u,true);
    ordinary_source_stream_case("(= (source-add) (+ 41 1))","(source-add)",number,1u,true);
    ordinary_source_stream_case("(= (source-loop $n) (source-loop (+ $n 1)))",
        "(source-loop 0)",NULL,0u,false);
}

static void prepared_result_cache_case(CettaLanguageId language) {
    Arena arena; arena_init(&arena); arena_set_hashcons(&arena, NULL);
    Space space; space_init(&space);
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context, language,
        language == CETTA_LANGUAGE_HE ? cetta_profile_he_extended()
                                      : cetta_profile_petta_extended());
    eval_set_library_context(&context);
    PreparedPureProgramCache cache = {.root_space = &space};
    const char *equations[] = {
        "(= (result-direct $x) (Box $x))",
        "(= (result-maybe Nil) (empty))",
        "(= (result-maybe (Cons $x $xs)) (Box $x))",
    };
    for (size_t i = 0u; i < 3u; i++)
        space_add(&space, parse_one(&arena, equations[i]));
    if (language == CETTA_LANGUAGE_PETTA)
        assert(petta_program_synchronize_space(context.petta_program, &space));
    const char *calls[] = {"(result-direct 7)", "(result-direct 9)",
        "(result-maybe Nil)", "(result-maybe (Cons 9 Nil))",
        "(result-maybe Other)"};
    const char *expected[] = {"(Box 7)", "(Box 9)", NULL, "(Box 9)", NULL};
#if CETTA_BUILD_WITH_RUNTIME_STATS
    bool was_enabled = cetta_runtime_stats_is_enabled();
    cetta_runtime_stats_enable();
#endif
    for (size_t i = 0u; i < 5u; i++) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
        CettaRuntimeStats before, after;
        cetta_runtime_stats_snapshot(&before);
#endif
        CettaCallOutcome reply = {0};
        bool completed = prepared_pure_closed_result_try(&space, &arena,
            parse_one(&arena, calls[i]), -1, true, true, &cache,
            PREPARED_PURE_PROGRAM_OPTIONAL_VALUE, &reply);
        CHECK(completed == (i != 4u));
        if (expected[i]) {
            CHECK(completed && reply.kind == CETTA_CALL_VALUE);
            CHECK(completed && atom_eq(reply.term, parse_one(&arena, expected[i])));
        } else if (completed) {
            CHECK(reply.kind == CETTA_CALL_FAILURE);
        }
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_snapshot(&after);
        CHECK(after.counters[CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_COMPILE_ATTEMPT] -
            before.counters[CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_COMPILE_ATTEMPT] ==
            ((i == 0u || i == 2u) ? 1u : 0u));
        CHECK(after.counters[CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_PROGRAM_CACHE_HIT] -
            before.counters[CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_PROGRAM_CACHE_HIT] ==
            ((i == 1u || i == 3u || i == 4u) ? 1u : 0u));
#endif
        if (i == 3u) {
            /* The live optional cache entry must not admit a caller that
             * accepts exactly one value. Its refusal leaves that entry. */
            CHECK(!prepared_pure_closed_result_try(&space, &arena,
                parse_one(&arena, "(result-maybe Nil)"), -1, true, true, &cache,
                PREPARED_PURE_PROGRAM_SINGLE_VALUE, &reply));
        }
    }
    /* Changing the source cannot reuse either compiled body. */
    CettaCallOutcome reply = {0};
    space_add(&space, parse_one(&arena, "(= (result-direct $x) extra)"));
    if (language == CETTA_LANGUAGE_PETTA)
        assert(petta_program_synchronize_space(context.petta_program, &space));
    CHECK(!prepared_pure_closed_result_try(&space, &arena,
        parse_one(&arena, "(result-direct 7)"), -1, true, true, &cache,
        PREPARED_PURE_PROGRAM_OPTIONAL_VALUE, &reply));
#if CETTA_BUILD_WITH_RUNTIME_STATS
    if (!was_enabled) cetta_runtime_stats_disable();
#endif
    prepared_pure_program_cache_free(&cache);
    eval_set_library_context(NULL);
    cetta_library_context_free(&context);
    space_free(&space); arena_free(&arena);
}

static bool retained_ancestor_permit(void *raw, Space *space,
        PettaMachineOperation operation, const PettaMachineOperationView *view) {
    (void)space; (void)operation; (void)view;
    (*(unsigned *)raw)++;
    return false;
}

static void prepared_native_admission_case(void) {
#if CETTA_BUILD_WITH_JSON_GSLT
    Arena arena; arena_init(&arena);
    Space space; space_init(&space);
    Registry registry; registry_init(&registry); g_registry = &registry;
    CettaLibraryContext context;
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_set_library_context(&context);
    Atom *error = NULL;
    CHECK(cetta_library_import(&context, "json", &space, &arena, &arena,
        &registry, 1000000, &error) && !error);
    Atom *head = atom_symbol_id(&arena, g_builtin_syms.lib_json_parse);
    Atom *arguments[] = {atom_string(&arena, "42")};
    const CettaInvocation invocation = {
        .head = binding_value_from_atom(head),
        .supplied = {.atoms = arguments, .count = 1u},
    };
    br_library_tracking = true;
    br_library_prepared_calls = br_library_calls = 0u;
    Atom *value = eval_prepared_pure_native_value(
        &context, &space, &arena, &invocation);
    CHECK(value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
        value->ground.ival == 42 && br_library_prepared_calls == 1u);
    PrimeEvalStackDriver *previous = g_prime_eval_stack_driver;
    PrimeNativeControl control = {0}; control.driver.control = &control;
    g_prime_eval_stack_driver = &control.driver;
    br_library_prepared_calls = br_library_calls = 0u;
    CHECK(!eval_prepared_pure_native_value(&context, &space, &arena, &invocation));
    CHECK(!br_library_prepared_calls && !br_library_calls);
    g_prime_eval_stack_driver = previous;
    EvalCompletionTracker tracker = {.completion = CETTA_EVAL_COMPLETE,
        .budget_limited = true, .parent = g_eval_completion_tracker};
    EvalCompletionTracker *prior_tracker = g_eval_completion_tracker;
    g_eval_completion_tracker = &tracker;
    CHECK(!eval_prepared_pure_native_value(&context, &space, &arena, &invocation));
    CHECK(!br_library_prepared_calls && !br_library_calls &&
        tracker.completion == CETTA_EVAL_COMPLETE);
    g_eval_completion_tracker = prior_tracker;
    Space program; space_init(&program);
    space_add(&program, parse_one(&arena,
        "(= (nc:json $text) (__cetta_lib_json_parse $text))"));
    space_add(&program, parse_one(&arena,
        "(= (nc:json-tail $text) (nc:json $text))"));
    const char *calls[] = {"(nc:json \"42\")", "(nc:json-tail \"42\")"};
    for (size_t which = 0u; which < 2u; which++) {
        PrimeNativeControl *held = calloc(1u, sizeof(*held)); assert(held);
        CettaSearchPolicySpec policy = {.native_control=true, .all=true, .workers=1u};
        assert(prime_native_control_init(held, &program, &arena,
            parse_one(&arena, calls[which]), -1, true,
            (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
            &policy, NULL, NULL, NULL));
        held->driver.ordinary_ordered = true;
        held->driver.ordinary_descendants = true;
        assert(prime_ordinary_capsule_stack_start(held));
        PrimeNativeRunGuard run; prime_native_run_enter(held, &run);
        PrimeEvalStackDriver *driver = &held->driver;
        PrimeEvalStackFrame *frame = NULL;
        br_library_prepared_calls = br_library_calls = 0u;
        for (unsigned n = 0u; n < 1000u && !frame; n++) {
            if (driver->task_ready) prime_eval_stack_run_task();
            else if (driver->top) prime_eval_stack_resume_top();
            if (driver->top && driver->top->kind == PRIME_EVAL_STACK_FRAME_CAPSULE_SEARCH)
                frame = driver->top;
        }
        assert(frame && frame->capsule_search);
        PrimeCapsuleSearch *search = frame->capsule_search;
        for (unsigned n = 0u; n < 1000u &&
                search->last_step != CETTA_PREPARED_CLOSED_SERVICE; n++)
            prime_capsule_search_resume(frame);
        assert(search->last_step == CETTA_PREPARED_CLOSED_SERVICE);
        assert(prime_capsule_service_prepare(search));
        PrimeCapsuleService *service = search->service;
        CHECK(!service->request.source_expression && !service->request.normalize_result &&
            service->request.operand_count == 1u && service->request.ready_arguments == 1u);
        PrimeEvalStackTask *task = &service->source_driver.task;
        assert(service->source_driver.task_ready && task->kind == PRIME_EVAL_STACK_TASK_CALL);
        Atom *pending = task->atom;
        uint64_t ordinal = service->request.ordinal, paid = search->transitions;
        uint64_t dispatches = search->service_ready_dispatches;
        Atom *data = atom_he_evaluated_expr(&arena, (Atom *[]){head, arguments[0]}, 2u);
        PettaMachineOperationView view = {.expression = data};
        CHECK(prime_capsule_source_call_permit(service, &program,
            PETTA_MACHINE_OPERATION_CALL, &view));
        CHECK(g_prime_eval_stack_driver == driver && !br_library_prepared_calls && !br_library_calls);
        for (unsigned retry = 0u; retry < 2u; retry++) {
            prime_capsule_source_resume(search);
            CettaPreparedServiceRequest now;
            CHECK(cetta_prepared_pure_closed_service_request(search->execution, search, &now));
            CHECK(search->service == service && now.ordinal == ordinal &&
                now.call == service->request.call);
            CHECK(service->source_driver.task_ready && task->atom == pending &&
                !service->source_cursor && !service->accepted && !service->pending &&
                !service->has_reply && !service->source_exhausted);
            CHECK(search->transitions == paid && search->service_ready_dispatches == dispatches);
            CHECK(!frame->child.len && !held->output.len &&
                !br_library_prepared_calls && !br_library_calls);
        }
        prime_native_run_leave(&run);
        CHECK(!held->driver.call_permit_parent && !held->driver.authority_parent);
        prime_native_control_destroy(held);
    }
    /* A synchronous higher-order call must not publish optimizer equations
     * into the caller's held source presentation. */
    space_add(&program, parse_one(&arena, "(= (nc:apply $f $v) ($f $v))"));
    space_add(&program, parse_one(&arena, "(= (nc:inc $v) (+ $v 1))"));
    SpaceProgramToken before = space_program_token(&program);
    ResultSet answers; result_set_init(&answers);
    metta_eval(&program, &arena, NULL,
        parse_one(&arena, "(nc:apply nc:inc 41)"), -1, &answers);
    CHECK(answers.len == 1u && atom_eq(answers.items[0], atom_int(&arena, 42)));
    CHECK(space_program_token_is_current(before));
    result_set_free(&answers);
    /* Automatic promotion is optional. It cannot move a frontier away from
     * the borrowed protocol that will decide its next actual call. */
    for (unsigned ancestry = 0u; ancestry < 4u; ancestry++) {
        OutcomeSet output; outcome_set_init(&output);
        Atom *call = parse_one(&arena, "(nc:inc 41)");
        unsigned checked = 0u;
        PrimeEvalStackDriver parent = {.call_permit = retained_ancestor_permit,
            .call_permit_context = &checked};
        PrimeEvalStackDriver source = {.arena = &arena, .root_target = &output,
            .task_ready = true};
        source.task = (PrimeEvalStackTask){.kind = PRIME_EVAL_STACK_TASK_CALL,
            .space = &program, .arena = &arena, .atom = call, .fuel = -1,
            .target = &output};
        if (ancestry == 0u) source.authority_parent = &parent;
        else if (ancestry == 1u) source.call_permit_parent = &parent;
        else if (ancestry == 2u) {source.call_permit = retained_ancestor_permit; source.call_permit_context = &checked;}
        else {parent.call_permit = NULL; source.authority_parent = &parent;}
        PrimeEvalStackDriver *prior = g_prime_eval_stack_driver;
        g_prime_eval_stack_driver = &source;
        if (ancestry < 3u) {
            CHECK(!prime_eval_stack_promote_ordinary(&source, &program, call, -1, NULL, 2u));
            CHECK(source.task_ready && source.task.atom == call &&
                source.root_target == &output && !output.len && !checked);
            EvalCompletionTracker check; eval_completion_child_begin(&check);
            CHECK(!prime_eval_check_call_contract(&program, &arena, call) && checked == 1u);
            CHECK(eval_completion_child_end(&check) == CETTA_EVAL_INCOMPLETE_CAPACITY);
        } else {
            CHECK(prime_eval_stack_promote_ordinary(&source, &program, call, -1, NULL, 2u));
            CHECK(!source.task_ready && output.len == 1u &&
                atom_eq(outcome_atom_materialize(&arena, &output.items[0]), atom_int(&arena, 42)) &&
                !checked);
        }
        g_prime_eval_stack_driver = prior;
        prime_eval_stack_task_free(&source.task);
        outcome_set_free(&output);
    }
    space_free(&program);
    br_library_tracking = false;
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); g_registry = NULL; space_free(&space); arena_free(&arena);
#endif
}

int main(void) {
    outcome_seen_identity_control();
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;g_hashcons=NULL;
    scoped_environment_refusal_cases();
    answer_reference_outer_binding_cases();
    checked_table_preparation_cases();
    checked_query_preparation_cases();
    partial_row_consumer_cases();
    scoped_failure_sweep();
    prepared_native_admission_case();
    owned_internal_need_capability_case();
    for(unsigned shape=0u;shape<6u;shape++)ordinary_source_exported_closure_case(shape,false);
    ordinary_source_exported_closure_case(0u,true);
    nested_return_preparation_cases();
    equation_instantiation_cases();
    prepared_result_cache_case(CETTA_LANGUAGE_HE);
    prepared_result_cache_case(CETTA_LANGUAGE_PETTA);
    retained_return_preparation_cases();
    for(unsigned damage=0u;damage<4u;damage++)retained_graph_currency_case(damage);
    retained_preparation_failure_case();
    ordinary_return_prepared_phase_case();
    ordinary_return_phase_identity_case();
    retained_payload_frame_case(0u);
    retained_payload_frame_case(24u);
    ordinary_owned_root_cases();
    for(uint32_t workers=1u;workers<=2u;workers++) {
        ordinary_source_preparation_case(workers,false);
        ordinary_source_preparation_case(workers,true);
        ordinary_source_preparation_after_prefix_case(workers,false);
        ordinary_source_preparation_after_prefix_case(workers,true);
    }
    for(unsigned preparation=0u;preparation<3u;preparation++)
        for(unsigned cancel=0u;cancel<2u;cancel++)
            for(unsigned failed=0u;failed<2u;failed++)
                ordinary_raw_preparation_refusal_case(preparation,cancel!=0u,failed!=0u);
    for(unsigned refusal=0u;refusal<3u;refusal++)
        for(unsigned cancel=0u;cancel<2u;cancel++)
            ordinary_cached_disposition_case(refusal,cancel!=0u);
    ordinary_source_shared_cases();
    for(unsigned damage=0u;damage<3u;damage++)
        ordinary_owned_pending_boundary_case(damage,false);
    for(unsigned damage=0u;damage<7u;damage++)
        ordinary_owned_pending_boundary_case(damage,true);
    space_subtype_lifetime_case();
    match_progress_entry_case("0", false);
    match_progress_entry_case("false", false);
    match_progress_entry_case("off", false);
    match_progress_entry_case("no", false);
    match_progress_entry_case("1", true);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    active_context_transition_case(false);
    active_context_transition_case(true);
    recording_reader_release_case();
    recording_scope_entry_control();
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    for (unsigned damage = 0u; damage < 9u; damage++) replay_history_evidence_case(damage);
#endif
    for (unsigned restart = 0u; restart < 2u; restart++) {
        suspended_history_owner_case(2u, 6u, restart != 0u);
        suspended_history_owner_case(2u, 8u, restart != 0u);
        suspended_history_owner_case(2u, 20u, restart != 0u);
        suspended_history_owner_case(1u, 8u, restart != 0u);
    }
#endif
    for (unsigned workers = 1u; workers <= 4u; workers *= 2u) {
        compact_cost_storage_case(workers, false);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        compact_cost_storage_case(workers, true);
#endif
    }
    guest_read_service_cases();
    for(unsigned which=0u;which<8u;which++)stored_judgment_syntax_case(which);
    for(unsigned which=0u;which<9u;which++)dependency_view_case(which);
    for(unsigned which=0u;which<14u;which++)pattern_root_case(which);
    for(unsigned which=0u;which<32u;which++) {
        if (!CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS && (which==7u || which==28u))
            continue;
        binding_role_case(which);
    }
    for(unsigned which=0u;which<6u;which++)projected_own_case(which);
    for(unsigned which=0u;which<12u;which++)stored_head_case(which);
    for(unsigned which=0u;which<15u;which++)stored_return_role_case(which);
    for(unsigned which=0u;which<12u;which++)cache_return_caller_case(which);
    for(unsigned which=0u;which<11u;which++)terminal_open_reply_case(which);
    for(unsigned which=0u;which<8u;which++)native_data_branch_case(which);
    pinned_revision_storage_case();
    transaction_storage_case();
#if CETTA_BUILD_WITH_RUNTIME_STATS
    for(unsigned which=0u;which<21u;which++)numeric_capsule_account_case(which);
#endif
    owned_authority_refusal_cases();
    retained_read_service_cases();
    return_advice_cases(false);
    return_advice_cases(true);
    regrade_dependency_cases();
    normalization_support_cases();
    worker_symbol_source_cases();
    for (unsigned consumer = 0u; consumer < 8u; consumer++)
        for (unsigned carrier = 0u; carrier < 3u; carrier++) {
            native_owning_consumer_case(consumer, carrier, false);
            native_owning_consumer_case(consumer, carrier, true);
        }
    for (unsigned secondary = 0u; secondary < 2u; secondary++) {
        for (unsigned owned = 0u; owned < 2u; owned++) {
            owned_outcome_copy_case(0u, owned != 0u, secondary != 0u);
            owned_outcome_copy_case(1u, owned != 0u, secondary != 0u);
            owned_outcome_copy_case(3u, owned != 0u, secondary != 0u);
        }
    }
    CHECK(owned_need_world_copy_case(false, false));
    CHECK(owned_need_world_copy_case(true, false));
    CHECK(owned_need_world_copy_case(false, true));
    CHECK(owned_need_world_copy_case(true, true));
    for(unsigned remove=0u;remove<2u;remove++)
        for(unsigned relocate=0u;relocate<2u;relocate++)
            for(unsigned alternative=0u;alternative<2u;alternative++)
                captured_query_publication_case(remove!=0u,relocate!=0u,alternative!=0u);
    for(unsigned distinct=0u;distinct<2u;distinct++)
        for(unsigned query_first=0u;query_first<2u;query_first++)
            for(unsigned fallback=0u;fallback<2u;fallback++) {
                size_t limit=1u;
                for(size_t cut=0u;cut<limit;cut++)
                    CHECK(owned_query_need_world_case(cut,&limit,
                        distinct!=0u,query_first!=0u,fallback!=0u));
            }
    native_capture_observer_contract_case(CETTA_LANGUAGE_HE,cetta_profile_he_extended());
    native_capture_observer_contract_case(CETTA_LANGUAGE_PETTA,cetta_profile_petta_extended());
    sealed_licence_environment_case();
    scoped_claim_conflict_case();
    for(unsigned host=0u;host<2u;host++) {
        CettaLanguageId language=host?CETTA_LANGUAGE_HE:CETTA_LANGUAGE_PETTA;
        refusal_case(language,false,1u);
        refusal_case(language,true,0u);
        refusal_case(language,true,1u);
    }
    unchanged_schedule_case(CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    unchanged_schedule_case(CETTA_LANGUAGE_HE,cetta_profile_he_extended());
    unchanged_schedule_case(CETTA_LANGUAGE_PETTA,cetta_profile_petta_extended());
    pending_zero_cases();
    pending_paid_boundary_cases();
    const CettaLanguageId languages[]={CETTA_LANGUAGE_PRIME,CETTA_LANGUAGE_HE,CETTA_LANGUAGE_PETTA};
    const CettaProfile *profiles[]={cetta_profile_prime_default(),cetta_profile_he_extended(),cetta_profile_petta_extended()};
    for(size_t host=0u;host<3u;host++) {
        retained_licence_case(languages[host],profiles[host],false);
        retained_licence_case(languages[host],profiles[host],true);
        retained_licence_case(languages[host],profiles[host],2u);
        retained_licence_case(languages[host],profiles[host],3u);
        mixed_packet_case(languages[host],profiles[host],false,false,false,0u);
        mixed_packet_case(languages[host],profiles[host],true,false,false,0u);
        mixed_packet_case(languages[host],profiles[host],false,true,false,0u);
        native_call_boundary_case(languages[host],profiles[host]);
        uint64_t whole=mixed_packet_case(languages[host],profiles[host],false,false,true,0u);
        CHECK(mixed_packet_case(languages[host],profiles[host],false,false,true,47u)==whole);
    }
    for(unsigned empty=0u;empty<2u;empty++) {
        implicit_first_source_case(empty!=0u,false);
        implicit_first_source_case(empty!=0u,true);
    }
    for(unsigned change=0u;change<4u;change++)own_query_publication_guard_case(change);
    for(unsigned operation=0u;operation<7u;operation++)
        for(unsigned mutate=0u;mutate<2u;mutate++)
            declared_space_read_dependency_case(operation,mutate!=0u);
    for(unsigned mode=0u;mode<4u;mode++)resume_frontier_case(mode);
    for(unsigned workers=1u;workers<=4u;workers*=2u) {
        for(unsigned which=0u;which<9u;which++)
            for(unsigned refusal=0u;refusal<5u;refusal++)
                owned_weighted_return_case(which,refusal,workers);
        for(unsigned refusal=5u;refusal<=6u;refusal++)
            owned_weighted_return_case(0u,refusal,workers);
    }
    for(unsigned mode=0u;mode<5u;mode++) {
        owned_region_resume_case(mode,false);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        owned_region_resume_case(mode,true);
#endif
    }
    for(unsigned entry=0u;entry<4u;entry++) {
        owned_region_demand_case(entry,false);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        owned_region_demand_case(entry,true);
#endif
    }
    var_intern_free(&variables);g_var_intern=NULL;symbol_table_free(&symbols);g_symbols=NULL;
    printf("Native control lease refusal and retained schedule: %zu checks, %zu failures\n",checks,failures);
    return failures?1:0;
}
