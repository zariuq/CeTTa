/* Exercise actual native-control refusal with its complete retained provider.
 * This translation unit replaces eval.o only in the control executable. */
#define BR_HAS_RETURN_HELPER 1
#define _GNU_SOURCE
#include "../src/grounded.h"
static bool br_native_call(Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
#define grounded_call br_native_call
#include "../src/eval.c"
#undef grounded_call
static bool br_suspend_native;
static unsigned br_native_calls;
static bool br_native_call(Arena *arena, Atom *head, Atom **args,
                           uint32_t arity, CettaCallOutcome *out) {
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

static size_t checks, failures;
#define CHECK(X) do { checks++; if (!(X)) { failures++; fprintf(stderr,"FAIL: %s:%d: %s\n",__FILE__,__LINE__,#X); } } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **items=NULL;
    int count=parse_metta_text(text,arena,&items);
    assert(count==1);
    Atom *value=items[0];free(items);return value;
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
    CHECK(empty_query ? !returned->control_closed && returned->equation_search!=NULL
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
    if (left.skeleton != right.skeleton || left.kind != right.kind) return false;
    return left.kind == BINDING_VALUE_MATERIALIZED || left.epoch == right.epoch;
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
        BindingValue value = {
            .skeleton = (state & 31u) == 0u ? NULL : &atoms[(state >> 8u) % 257u],
            .kind = (BindingValueKind)((state >> 20u) % 3u),
            .epoch = (state >> 24u) % 4u,
        };
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
        assert(seen.items[i].kind == expected[i].kind);
        assert(seen.items[i].epoch == expected[i].epoch);
        assert(!outcome_preview_seen_add_value(&seen, expected[i]));
    }
    outcome_preview_seen_free(&seen);
    outcome_preview_seen_init(&seen);
    for (size_t i = 0u; i < 16u; i++) assert(outcome_preview_seen_add(&seen, &atoms[i]));
    assert(seen.items == seen.inline_items);
    assert(!seen.slots && !seen.slot_cap);
    BindingValue m = binding_value_from_atom(&atoms[32]);
    assert(outcome_preview_seen_add_value(&seen, m));
    m.epoch = 91u;
    assert(!outcome_preview_seen_add_value(&seen, m));
    BindingValue c = binding_value_from_context(&atoms[32], 11u);
    assert(outcome_preview_seen_add_value(&seen, c));
    c.epoch = 12u;
    assert(outcome_preview_seen_add_value(&seen, c));
    c.kind = BINDING_VALUE_CONTEXTUAL_PERSISTENT;
    assert(outcome_preview_seen_add_value(&seen, c));
    assert(outcome_preview_seen_add(&seen, &atoms[33]));
    assert(seen.slots && seen.slot_cap >= 64u);
    assert(outcome_preview_seen_contains_value(&seen, c));
    c.epoch = 13u;
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
    PrimeNeedActiveGuard active = prime_need_active_enter(&local);
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
        &space,&arena,root,which==8u?&branch:&env,&pattern,1u,-1,&out);
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
                PrimeNeedActiveGuard active=prime_need_active_enter(&env);
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
        eval_delayed_outcome_for_caller(&space,&arena,NULL,&seed,-1,true,&rows);
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
        assert(image->expr.elems[2]->kind==ATOM_VAR && image->expr.elems[2]->var_id==y->var_id);
        assert(prime_semantics_is_authored_lambda(&arena,result));
        Atom *application=atom_expr2(&arena,result,atom_int(&arena,1));
        Atom *answer=prime_semantics_authored_head_step(&arena,application);
        assert(answer && atom_eq(answer,atom_expr3(&arena,atom_symbol(&arena,"Pair"),atom_int(&arena,1),atom_int(&arena,5))));
    } else if(which==1u) {
        assert(bindings_add_var(&env,x,y));
        result=bindings_apply_projected_body_visible(&env,&arena,own);
        assert(atom_is_prime_own_list(result) && result->expr.len==3u);
        assert(result->expr.elems[1]==record && result->expr.elems[2]->var_id==y->var_id);
    } else if(which==2u) {
        Atom *value=atom_expr2(&arena,atom_symbol(&arena,"Box"),y);
        assert(bindings_add_var(&env,x,value));
        assert(bindings_add_var(&env,y,atom_int(&arena,5)));
        result=bindings_apply_projected_body_visible(&env,&arena,x);
        assert(result && atom_eq(result,value));
        assert(result->expr.elems[1]->kind==ATOM_VAR && result->expr.elems[1]->var_id==y->var_id);
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
        assert(result->expr.elems[3]->kind==ATOM_VAR && result->expr.elems[3]->var_id==y->var_id);
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
}
static void numeric_capsule_account_case(unsigned which) {
    assert(which<18u);
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
    assert(numeric_capsule_counter("prepared-capsule-step")==0u);
    assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
    assert(!search->raw_pending && !frame->child.len && !search->transitions && !search->resumes);
    if(which>=16u) {
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
        assert(!search->raw_pending && !frame->child.len && !search->transitions && !search->resumes);
    } else if(which==4u) {
        space_add(&space,br_parse(&arena,"(= (nc:new) 42)"));
        assert(!prime_capsule_search_current(search));
        prime_capsule_search_resume(frame);
        assert(driver.capacity_blocked && search->serial_invalidated);
        assert(!search->raw_pending && !frame->child.len && !search->transitions && !search->resumes);
    } else {
        if(which==2u) {
            search->context.controller_quantum_limit=0u;
            prime_capsule_search_resume(frame);
            assert(search->resumes==1u && search->transitions==0u && !frame->child.len);
            assert(numeric_capsule_counter("prepared-capsule-step")==0u);
            assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
            search->context.controller_quantum_limit=1u;
        }
        if(which==3u)frame->child.payload_owner=&foreign;
        if(which==5u)cetta_runtime_stats_disable();
        for(unsigned n=0u;n<100u && !frame->child.len && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        assert(search->transitions>0u);
        if(which==3u) {
            assert(driver.capacity_blocked && search->raw_pending && search->pending_projection.len==1u);
            assert(frame->child.len==0u);
            assert(numeric_capsule_counter("prepared-capsule-publication")==0u);
            uint64_t spent=search->transitions,resumes=search->resumes;
            Atom *answer=search->raw_answer;
            assert(answer && answer->kind==ATOM_GROUNDED && answer->ground.gkind==GV_INT && answer->ground.ival==10);
            assert(numeric_capsule_counter("prepared-capsule-step")==spent);
            /* Restore the deliberately conflicting receiver witness. This is
             * the internal checked-append retry, not public serial recovery. */
            frame->child.payload_owner=&arena;driver.capacity_blocked=false;
            assert(prime_capsule_search_current(search));
            prime_capsule_search_resume(frame);
            assert(search->transitions==spent && search->resumes==resumes);
            assert(!search->raw_pending && !search->pending_projection.len && search->raw_answer==NULL);
        }
        assert(frame->child.len==1u && !search->raw_pending);
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
        PrimeNeedActiveGuard active=prime_need_active_enter(&caller);
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
    CettaPreparedClosedState state;
    assert(cetta_prepared_pure_closed_read(search->execution,&state));
    if(which==1u)assert(state.frames>0u);
    else assert(state.frames==0u && state.values==1u);
    for(unsigned n=0u;n<500u && !service->pending && !driver.capacity_blocked;n++)
        prime_capsule_source_resume(search);
    assert(!driver.capacity_blocked && service->pending && service->answer && atom_has_vars(service->answer));
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
            !search->raw_pending && !search->transitions && !search->resumes);
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
        assert(service->pending && !service->has_reply && frame->child.len==0u && !search->raw_pending);
    } else {
        assert(!service->pending && service->has_reply && service->accepted==1u);
        for(unsigned n=0u;n<10u && !frame->child.len && !driver.capacity_blocked;n++)
            prime_capsule_search_resume(frame);
        if(which==7u) {
            assert(driver.capacity_blocked && search->raw_pending && search->pending_projection.len==1u);
            uint64_t published_spent=search->transitions,published_resumes=search->resumes;
            Atom *retained=search->raw_answer;assert(retained==answer);
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
    assert(prime_native_parallel_read_service_admitted(child,owned_space,
        owned_arena,typed_call,&child->output)==requires_guest);
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
    PrimeNeedActiveGuard active = prime_need_active_enter(&caller);
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

int main(void) {
    outcome_seen_identity_control();
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;g_hashcons=NULL;
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
#if CETTA_BUILD_WITH_RUNTIME_STATS
    for(unsigned which=0u;which<18u;which++)numeric_capsule_account_case(which);
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
