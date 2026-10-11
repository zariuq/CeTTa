/* Exercise actual native-control refusal with its complete retained provider.
 * This translation unit replaces eval.o only in the control executable. */
#include "../src/eval.c"
#include "parser.h"
#include <assert.h>

static size_t checks, failures;
#define CHECK(X) do { checks++; if (!(X)) { failures++; fprintf(stderr,"FAIL: %s:%d: %s\n",__FILE__,__LINE__,#X); } } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **items=NULL;
    int count=parse_metta_text(text,arena,&items);
    assert(count==1);
    Atom *value=items[0];free(items);return value;
}

typedef struct {
    size_t count;
    Atom *value;
    size_t weighted;
    size_t lengths[16];
    uint64_t factors[16][8];
    int64_t coefficients[16][8];
} Observed;
static bool observe(Arena *arena,Atom *value,const Bindings *environment,void *raw) {
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
        CHECK(prime_native_control_step(&control,0u)==0u);
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

/* The nested first-value frame has already returned before mutation. Its
 * complete-query authority still belongs to the waiting caller. */
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
        for(PrimeEvalStackFrame *frame=control.retired_frames;frame;frame=frame->control_next)
            if(frame->kind==PRIME_EVAL_STACK_FRAME_CONTROL_NESTED &&
               frame->control_nested_policy.implicit_first_value && frame->control_nested) {
                returned=frame;break;
            }
    }
    CHECK(returned!=NULL);assert(returned);
    CHECK(returned->control_closed && returned->control_notified);
    CHECK(control.query_read_count==1u && !observed.count && !control.output.len);
    CHECK(space_read_token_is_current(control.query_reads[0]));
    PrimeNativeControl *child=returned->control_nested;
    CHECK(child->query_read_count==1u);
    CHECK(empty_query?prime_native_control_closed(child):child->delivered_count==1u);
    const PrimeNativeControl child_before=*child;
    const PrimeEvalStackFrame return_before=*returned;
    uint64_t paid=control.steps;
    space_add(mutate_read?&kb:&other,parse_one(&arena,empty_query?"(Missing 8)":"(Fact 8)"));
    prime_native_control_run(&control,empty_query?200u:20000u,observe,&observed);
    if(mutate_read) {
        CHECK(control.invalidated && !observed.count && !control.output.len);
        CHECK(!prime_native_control_closed(&control) && control.steps==paid);
        CHECK(!memcmp(&child_before,child,sizeof(*child)));
        CHECK(!memcmp(&return_before,returned,sizeof(*returned)));
        CHECK(returned->control_nested==child);
    } else {
        CHECK(!control.invalidated && prime_native_control_closed(&control)==!empty_query);
        CHECK(empty_query ? observed.count==0u :
            observed.count==1u && atom_eq(observed.value,atom_int(&arena,7)));
    }
    prime_native_control_free(&control);outcome_set_free(&control.output);
    if(control.base_environment_initialized)bindings_free(&control.base_environment);
    cetta_library_context_free(context);free(context);eval_set_library_context(NULL);
    registry_free(&registry);g_registry=NULL;
    space_free(&space);space_free(&kb);space_free(&other);arena_free(&arena);
}

static void native_call_boundary_case(CettaLanguageId language,const CettaProfile *profile) {
    Arena arena;arena_init(&arena);arena_set_hashcons(&arena,NULL);
    Space space;space_init(&space);
    CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
    cetta_library_context_init_for_language_profile(context,language,profile);eval_set_library_context(context);
    if(language==CETTA_LANGUAGE_PRIME) {
        CHECK(prime_native_worker_call_local(&space,&arena,parse_one(&arena,"(Box 7)")));
        CHECK(!prime_native_worker_call_local(&space,&arena,parse_one(&arena,"((Box 7))")));
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
    PrimeNeedActiveGuard outside=prime_need_active_enter(&ambient);
    PrimeNeedActiveGuard sealed=prime_need_active_enter_captured(&empty);
    CHECK(g_prime_need_logical_env==&empty && !g_prime_need_active.session_id);
    CHECK(!g_prime_weight_active.length && !g_prime_weight_active.owner);
    CHECK(!g_prime_need_branch_state_active.owner && !g_prime_need_branch_state_active.session_id);
    prime_need_active_leave(&sealed);
    CHECK(g_prime_need_logical_env==&ambient && g_prime_weight_active.last==weighted.last);
    CHECK(g_prime_need_active.session_id==bindings_need_view(&ambient)->session_id);
    CHECK(g_prime_need_branch_state_active.owner==&source);
    sealed=prime_need_active_enter_captured(&captured);
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
            uint64_t spent=prime_native_control_step(control,attempt?1u:0u);
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
    assert(bindings_add_id(&seed, 419u, SYMBOL_ID_NONE, value) &&
        bindings_add_id(&seed, 422u, SYMBOL_ID_NONE, handled.last->previous->value) &&
        bindings_add_id(&seed, 423u, SYMBOL_ID_NONE, handled.last->value) &&
        bindings_add_var(&seed, variable, external_value));
    *bindings_need_mut(&seed) = completed;
    *bindings_weight_mut(&seed) = handled;
    PrimeNeedCellView before = {0};
    assert(prime_need_snapshot_lookup(&completed, cell_id, &before) &&
        before.origin == value && before.cached == value &&
        bindings_lookup_value_id(&seed, 419u).skeleton == value &&
        bindings_lookup_value_id(&seed, 422u).skeleton == handled.last->previous->value &&
        bindings_lookup_value_id(&seed, 423u).skeleton == handled.last->value &&
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
            cetta_var_map_add(&out->answer_ref.goal_instantiation, 519u, variable));
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
              cetta_var_map_lookup(&out->answer_ref.goal_instantiation, 519u) == transported_var &&
              bindings_lookup_value_id(&out->env, transported_var->var_id).skeleton == external_value);
        CHECK(prime_need_snapshot_lookup(bindings_need_view(&out->env), cell_id, &cell) &&
              cell.origin == transported && cell.cached == transported &&
              bindings_need_view(&out->env)->max_storage_key == completed.max_storage_key &&
              bindings_occurrence_token(&out->env) == occurrences[i]);
        CHECK(weights->length == 2u && weights->last->value != transported &&
              weights->last->value != weights->last->previous->value &&
              bindings_lookup_value_id(&out->env, 422u).skeleton == weights->last->previous->value &&
              bindings_lookup_value_id(&out->env, 423u).skeleton == weights->last->value &&
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
    Atom *variable=atom_var_with_spelling(&input,symbol_intern_cstr(g_symbols,"query-frame-var"),123401u);
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
    CHECK(library->lib_prolog && ordinary.authority.words[4]!=0u &&
          ordinary.authority.words[5]!=0u);
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
                parse_one(&arena, i ? "(quote (right-job))" : "(quote (left-job))"), 0u);
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

int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;g_hashcons=NULL;
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
    var_intern_free(&variables);g_var_intern=NULL;symbol_table_free(&symbols);g_symbols=NULL;
    printf("Native control lease refusal and retained schedule: %zu checks, %zu failures\n",checks,failures);
    return failures?1:0;
}
