/* Captured run scopes under unrelated callers, including valid empty and
 * explicitly cleared worlds. Caller payloads outlive each resumed control. */
static bool checkpoint_first_seen(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    (void)seen(arena,value,occurrence,raw);
    return false;
}
static void checkpoint_scope_run(PrimeNativeControl *control,uint64_t budget,Result *result,
    Arena *caller_arena,unsigned client,unsigned cut) {
    unsigned mode=(client+cut)%4u;
    Bindings caller,empty;bindings_init(&caller);bindings_init(&empty);
    if(mode) {
        PrimeNeedSnapshot start={0};
        assert(prime_need_snapshot_begin(&start));
        *bindings_need_mut(&caller)=start;
        assert(prime_need_branch_state_begin(caller_arena,bindings_branch_state_mut(&caller)));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        assert(prime_need_receipt_begin(caller_arena,bindings_receipt_mut(&caller)));
#endif
        if(mode>=2u) {
            PrimeNeedSnapshot cell={0},running={0},finished={0};uint64_t id=0u;
            Atom *origin=atom_symbol(caller_arena,"caller-owned-choice");
            Atom *value=atom_int(caller_arena,123);
            assert(prime_need_snapshot_allocate(caller_arena,&start,origin,&cell,&id));
            assert(prime_need_snapshot_start_evaluation(caller_arena,&cell,id,991u,&running));
            assert(prime_need_snapshot_resolve_value(caller_arena,&running,id,991u,value,&finished));
            *bindings_need_mut(&caller)=finished;
            CettaWeightLedger weights={0};
            assert(cetta_weight_ledger_append(caller_arena,&weights,
                prime_need_fresh_source_occurrence(),991u,atom_int(caller_arena,9),&weights));
            *bindings_weight_mut(&caller)=weights;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            PrimeNeedReceipt *receipt=bindings_receipt_mut(&caller);
            assert(prime_need_receipt_observe_cached_cell(caller_arena,receipt,&finished,id,
                true,false,value,receipt));
#endif
        }
    }
    PrimeNeedActiveGuard active=prime_need_active_enter_captured(&caller),sealed={0};
    PrimeNeedSnapshot caller_need=g_prime_need_active;
    PrimeNeedBranchState caller_branch=g_prime_need_branch_state_active;
    CettaWeightLedger caller_weights=g_prime_weight_active;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt caller_receipt=g_prime_need_receipt_active;
#endif
    /* Ordinary clients still overlay only present components. */
    PrimeNeedActiveGuard overlay=prime_need_active_enter(&empty);
    assert(!memcmp(&caller_need,&g_prime_need_active,sizeof(caller_need)));
    assert(!memcmp(&caller_branch,&g_prime_need_branch_state_active,sizeof(caller_branch)));
    assert(!memcmp(&caller_weights,&g_prime_weight_active,sizeof(caller_weights)));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    assert(!memcmp(&caller_receipt,&g_prime_need_receipt_active,sizeof(caller_receipt)));
#endif
    assert(g_prime_need_logical_env==&empty);
    prime_need_active_leave(&overlay);
    if(mode==3u)sealed=prime_need_active_enter_captured(&empty);
    PrimeEvalStackDriver caller_driver={.arena=caller_arena,.nested=true};
    PrimeEvalStackDriver *old_driver=g_prime_eval_stack_driver;
    PrimePrefixDemand old_prefix=g_prime_prefix_demand;
    Arena *old_owner=g_prime_need_owner,*old_episode=g_prime_need_episode_owner;
    Arena *old_branch=g_prime_need_region_branch_owner;
    g_prime_eval_stack_driver=&caller_driver;
    Atom *caller_root=atom_int(caller_arena,41);
    g_prime_prefix_demand=(PrimePrefixDemand){.root=caller_root,.limit=3u};
    g_prime_need_owner=caller_arena;g_prime_need_episode_owner=caller_arena;
    g_prime_need_region_branch_owner=caller_arena;
    EvalGcRootFrame root={0};
    eval_gc_root_frame_enter(&root,&caller_root,&caller,NULL);
    EvalGcRootFrame *roots=g_eval_gc.roots;
    RegistryReadView view={0};assert(registry_read_view_init(&view,g_registry,caller_arena));
    RegistryReadView *previous=registry_read_view_enter(&view,g_registry);
    if(cut==0u) {
        uint64_t steps=control->steps;
        _Atomic bool cancel=true;
        _Atomic bool *old_cancel=eval_set_cancel_token(&cancel);
        prime_native_control_run(control,budget,checkpoint_first_seen,result);
        assert(control->steps==steps);
        assert(!control->failed);
        (void)eval_set_cancel_token(old_cancel);
    }
    PrimeNativeRunGuard entered;
    prime_native_run_enter(control,&entered);
    assert(g_prime_need_logical_env==&control->base_environment);
    assert(!memcmp(&g_prime_need_active,bindings_need_view(&control->base_environment),sizeof(g_prime_need_active)));
    assert(!memcmp(&g_prime_need_branch_state_active,bindings_branch_state_view(&control->base_environment),sizeof(g_prime_need_branch_state_active)));
    assert(!memcmp(&g_prime_weight_active,bindings_weight_view(&control->base_environment),sizeof(g_prime_weight_active)));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    assert(!memcmp(&g_prime_need_receipt_active,bindings_receipt_view(&control->base_environment),sizeof(g_prime_need_receipt_active)));
#endif
    prime_native_run_leave(&entered);
    prime_native_control_run(control,budget,checkpoint_first_seen,result);
    if(control->policy.selection && control->selected_delivered) {
        size_t position=control->selected_delivered;
        size_t *selection=control->selected;
        uint64_t steps=control->steps;
        CettaNativeCostSnapshot paid=control->event_cost.total;
        size_t events=control->event_cost.length;
        uint64_t delivered=control->delivered_count;
        /* False accepts the current item, including the last one. Remaining
         * selected occurrences need no evaluator grant or repeated choice. */
        prime_native_control_run(control,0u,checkpoint_first_seen,result);
        assert(control->selected==selection&&control->selected_delivered>=position);
        uint64_t publications=control->delivered_count-delivered;
        /* Best-result delivery charges publication, without an evaluator
         * quantum. Coalescing may extend the previous ordered cost event. */
        paid.counts[CETTA_COST_PUBLISH]+=publications;paid.work+=publications;paid.span+=publications;
        assert(control->steps==steps&&control->event_cost.length>=events&&control->event_cost.length<=events+publications);
        assert(!memcmp(&paid,&control->event_cost.total,sizeof(paid)));
        assert(control->goal_reported==(control->selected_delivered==control->selected_length));
    }
    prime_native_control_run(control,budget,seen,result);
    assert(g_eval_gc.roots==roots&&caller_root->kind==ATOM_GROUNDED&&caller_root->ground.ival==41);
    assert(g_prime_eval_stack_driver==&caller_driver&&g_prime_prefix_demand.root==caller_root&&g_prime_prefix_demand.limit==3u);
    assert(g_prime_need_owner==caller_arena&&g_prime_need_episode_owner==caller_arena);
    assert(g_prime_need_region_branch_owner==caller_arena);
    registry_read_view_leave(previous);registry_read_view_free(&view);
    eval_gc_root_frame_leave(&root);
    g_prime_eval_stack_driver=old_driver;g_prime_prefix_demand=old_prefix;
    g_prime_need_owner=old_owner;g_prime_need_episode_owner=old_episode;
    g_prime_need_region_branch_owner=old_branch;
    if(mode==3u)prime_need_active_leave(&sealed);
    assert(!memcmp(&caller_need,&g_prime_need_active,sizeof(caller_need)));
    assert(!memcmp(&caller_branch,&g_prime_need_branch_state_active,sizeof(caller_branch)));
    assert(!memcmp(&caller_weights,&g_prime_weight_active,sizeof(caller_weights)));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    assert(!memcmp(&caller_receipt,&g_prime_need_receipt_active,sizeof(caller_receipt)));
#endif
    prime_need_active_leave(&active);
    bindings_free(&empty);bindings_free(&caller);
}


/* The public retained consumer must keep a partially delivered selection,
 * including a short best result. A changed joint cardinality is a new demand. */
static void checkpoint_selection_resume_cases(void) {
 for(unsigned mode=0u;mode<4u;mode++) {
  unsigned client=300u+mode,cut=0u;
  Arena syntax;arena_init(&syntax);Space space;space_init(&space);
  Registry registry;registry_init(&registry);g_registry=&registry;
  registry_bind(&registry,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
  PrimeNativeControl *control=malloc(sizeof(*control));assert(control);
  bool best=mode==2u;
  size_t goal=best?3u:2u;
  CettaSearchPolicySpec policy={.native_control=true,.retain=true,.selection=best?2:1,
   .workers=1u,.work_budget=UINT64_MAX,.costs=true,
   .goal_pattern=parse(&syntax,"$value"),.goal_expression=parse(&syntax,best?"$value":"True")};
  if(best)policy.goal_expression=policy.goal_pattern;
  assert(prime_native_control_init(control,&space,&syntax,
   parse(&syntax,best?"(superpose (3 1))":mode==1u?"(superpose (A A C))":"(superpose (A B C))"),
   -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_FINITE_PREFIX,.prefix_limit=goal},&policy,NULL,NULL,NULL));
  Result prefix={0};arena_init(&prefix.storage);
  prime_native_control_run(control,3000u,checkpoint_first_seen,&prefix);
  CHECK(prefix.count==1u&&control->selected_length==2u&&control->selected_delivered==1u&&!control->goal_reported);
  CHECK(control->selected[0]!=control->selected[1]);
  uint64_t id=0u;assert(cetta_native_handle_alloc(lib,"prime-search",control,prime_native_control_destroy,&id));control->handle_id=id;
  Atom *handle=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(handle);
  uint64_t steps=control->steps;size_t *selected=control->selected;
  CettaSelectionAutomaton phase=control->hub.schedule;
  policy.work_budget=0u;
  for(unsigned resume=0u;resume<3u;resume++) {
   OutcomeSet result;outcome_set_init(&result);
   CHECK(prime_native_control_select(&space,&syntax,handle,-1,goal,&policy,&result,NULL,NULL));
   CHECK(result.len==1u);
   Atom *observation=result.len?outcome_atom_materialize(&syntax,&result.items[0]):NULL;
   CHECK(observation&&observation->kind==ATOM_EXPR&&observation->expr.len>=2u&&atom_is_symbol(observation->expr.elems[0],"Observation"));
   Atom *values=observation&&observation->kind==ATOM_EXPR&&observation->expr.len>=2u?observation->expr.elems[1]:NULL;
   CHECK(values&&values->kind==ATOM_EXPR&&values->expr.len==(resume?0u:1u));
   if(!resume&&values&&values->kind==ATOM_EXPR&&values->expr.len==1u)
    CHECK(atom_eq(values->expr.elems[0],parse(&syntax,best?"3":mode==1u?"A":"B")));
   CHECK(control->selected==selected&&control->selected_delivered==2u&&control->goal_reported);
   CHECK(control->steps==steps&&!memcmp(&control->hub.schedule,&phase,sizeof(phase)));
   outcome_set_free(&result);
  }
  if(mode==3u) {
   policy.work_budget=3000u;
   OutcomeSet result;outcome_set_init(&result);
   CHECK(prime_native_control_select(&space,&syntax,handle,-1,1u,&policy,&result,NULL,NULL));
   CHECK(control->goal_size==1u&&control->selected_length==1u&&control->selected_delivered==1u&&control->goal_reported);
   CHECK(result.len==1u);outcome_set_free(&result);
  }
  printf("SELECTION_RESUME mode=%u failures=%zu\n",mode,failures);
  CHECK(cetta_native_handle_close(lib,"prime-search",id));arena_free(&prefix.storage);
  cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
  registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
}


static void checkpoint_captured_scope_cases(void) {
 for(unsigned capture=0u;capture<3u;capture++)for(unsigned ambient=0u;ambient<4u;ambient++) {
  unsigned client=400u+capture*4u+ambient,cut=0u;
  Arena syntax;arena_init(&syntax);Space space;space_init(&space);
  Registry registry;registry_init(&registry);g_registry=&registry;
  registry_bind(&registry,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
  Bindings inherited;bindings_init(&inherited);
  if(capture) {
   PrimeNeedSnapshot initial={0};assert(prime_need_snapshot_begin(&initial));
   *bindings_need_mut(&inherited)=initial;
   assert(prime_need_branch_state_begin(&syntax,bindings_branch_state_mut(&inherited)));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
   assert(prime_need_receipt_begin(&syntax,bindings_receipt_mut(&inherited)));
#endif
   if(capture==2u) {
    PrimeNeedSnapshot cell={0},running={0},finished={0};uint64_t id=0u;
    Atom *value=atom_int(&syntax,4);
    assert(prime_need_snapshot_allocate(&syntax,&initial,atom_symbol(&syntax,"captured-cell"),&cell,&id));
    assert(prime_need_snapshot_start_evaluation(&syntax,&cell,id,731u,&running));
    assert(prime_need_snapshot_resolve_value(&syntax,&running,id,731u,value,&finished));
    *bindings_need_mut(&inherited)=finished;
    CettaWeightLedger ledger={0};
    assert(cetta_weight_ledger_append(&syntax,&ledger,prime_need_fresh_source_occurrence(),731u,atom_int(&syntax,5),&ledger));
    *bindings_weight_mut(&inherited)=ledger;
   }
  }
  PrimeNeedActiveGuard initial=prime_need_active_enter_captured(&inherited);
  PrimeNativeControl *control=malloc(sizeof(*control));assert(control);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true};
  assert(prime_native_control_init(control,&space,&syntax,parse(&syntax,"(Pair 4 4)"),-1,true,
   (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,&inherited,NULL,NULL));
  prime_need_active_leave(&initial);
  CHECK(prime_need_snapshot_present(bindings_need_view(&control->base_environment))==(capture!=0u));
  if(capture==1u)CHECK(!bindings_need_view(&control->base_environment)->top);
  if(capture==2u)CHECK(bindings_need_view(&control->base_environment)->top);
  Result result={0};arena_init(&result.storage);
  checkpoint_scope_run(control,1000u,&result,&syntax,client,cut);
  CHECK(result.count==1u&&prime_native_control_closed(control));
  CHECK(result.lengths[0]==(capture==2u?1u:0u));
  if(capture==2u)CHECK(result.factors[0][0]==5);
  printf("CAPTURED_SCOPE capture=%u ambient=%u failures=%zu\n",capture,ambient,failures);
  prime_native_control_destroy(control);arena_free(&result.storage);bindings_free(&inherited);
  cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
  registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
}
