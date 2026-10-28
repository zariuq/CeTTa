/* Internal recorded-parent composition. The public checkpoint API still
 * refuses a recording owner: this fixture explicitly stages its capability.
 * The same one-thread fork premise as the ordinary parent comparison supplies
 * equal future allocator state, not production replay authority. */
static size_t recorded_paid_holders,recorded_truncated_views,recorded_preparation_views;
static bool recording_status_equal(PrimeNeedRecordingStatus a,PrimeNeedRecordingStatus b) {
 return a.nodes==b.nodes && a.reserved_nodes==b.reserved_nodes &&
  a.payload_charge==b.payload_charge && a.reserved_payload_bytes==b.reserved_payload_bytes &&
  a.metadata_bound==b.metadata_bound && a.retained_bound==b.retained_bound &&
  a.owner_links==b.owner_links && a.omitted==b.omitted &&
  a.incomplete==b.incomplete && a.overflow==b.overflow;
}
static void recorded_one(unsigned client,unsigned cut){
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);Space space;space_init(&space);
 Registry reg;registry_init(&reg);g_registry=&reg;registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
 const char *rows[]={"(=w (coin) 2 3)","(=w (coin) 2 3)","(=w (coin) 5 7)","(=w (weigh-by-self $x) $x $x)","(= (paired $x) (Pair $x $x))","(D 3)","(D 3)","(= (accepted $x) Mystery)","(=w (priced) Cheap 1)","(=w (priced) Costly 3)","(= (offers) (match &self (D $x) (Offer $x) (where $x)))"};
 for(size_t i=0;i<sizeof(rows)/sizeof(*rows);i++)space_add(&space,parse(&syntax,rows[i]));
 const char *programs[]={"(Pair (weight:where 2 3) (weight:where 5 7))","(paired (weigh-by-self (coin)))","(match &self (D $x) $x (where 2))","(weight:where 7 Mystery (admit accepted))","(weight:read (algebra 0 1 + *) (paired (weigh-by-self (coin))))","(weight:read (algebra 1000000 0 min +) (priced))","(weight:read (algebra 1000000 0 min +) (offers))","(superpose (A B C))","(superpose (A A A))","(superpose (A A A))","(superpose (3 1))","(superpose ())"};
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true,.history={.enabled=true,.selections=recording_profile==2u?0u:512u,.nodes=recording_profile==3u?1u:64u,.bytes=recording_profile==3u?1u:65536u,.owners=1024u}};
 if(client>=5u&&client<=6u){policy.by_weight=true;policy.descending=false;policy.priority=2;policy.age_share=2;policy.weight_law=prime_need_symbols()->law_nonnegative_sum;}
 if(client>=7u){policy.selection=client<=9u?1:2;policy.all=false;policy.goal_pattern=parse(&syntax,"$bundle");policy.goal_expression=client>=10u?policy.goal_pattern:parse(&syntax,client==8u?"False":"True");}
 if(client>=10u)CHECK(policy.goal_pattern==policy.goal_expression);
 Bindings inherited;bindings_init(&inherited);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS

#endif
 assert(prime_native_control_init(source,&space,&syntax,parse(&syntax,programs[client]),-1,true,(CettaObservationDemand){.completion=client>=7u?CETTA_OBSERVATION_FINITE_PREFIX:CETTA_OBSERVATION_COMPLETE_BAG,.prefix_limit=client==10u?3u:client>=7u?2u:0u},&policy,&inherited,NULL,NULL));
 bindings_free(&inherited);
 Result a={0},b={0},prefix={0};arena_init(&a.storage);arena_init(&b.storage);arena_init(&prefix.storage);
 prime_native_control_run(source,cut,policy.selection?checkpoint_first_seen:seen,&prefix);
 if(recording_profile==6u) {
  PrimeNativeHistorySpec stop={.stop=true};
  CHECK(prime_native_history_configure(source,&stop));
  CHECK(source->history.stopped&&!source->history.spec.enabled);
 }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 for(const PrimeEvalStackFrame *f=source->frames;f;f=f->control_next) {
  if(f->before_receipt.recording)causal_frame_views++;
 }
#endif
 bool paid_holder=false;
 if(recording_profile==4u && source->history.count) {
  PrimeNeedReceipt receipt=source->history.entries[0].operation;
  CHECK(receipt.recording && prime_need_recording_promote(&source->storage,&receipt));
  paid_holder=arena_retains_owner(&source->storage,source->history.group,prime_need_recording_release);
  CHECK(paid_holder);recorded_paid_holders+=paid_holder;
 }
 /* This schema control supplies an already retained preparation projection;
  * it does not claim that this serial parent dispatched a worker. */
 if(recording_profile==5u && source->history.count) {
  source->history.entries[0].prepared=source->history.entries[0].operation;
  source->history.entries[0].preparation=true;
  source->history.entries[0].paid=true;
  recorded_preparation_views++;
 }
 PrimeNeedRecordingStatus captured=prime_need_recording_status(source->history.group);
 if(recording_profile==3u && captured.incomplete)recorded_truncated_views++;
 PrimeNativeControl original=*source;
 PrimeNativeControl *image=calloc(1u,sizeof(*image));assert(image);
 arena_init(&image->storage);image->owned_arena=true;outcome_set_init(&image->output);
 PrimeNeedRecordingCopy *recording=prime_need_recording_copy_new(source->history.group);assert(recording);
 size_t recorded_pairs=prime_need_recording_copy_pair_count(recording);
 AtomDeepCopyArenaPair *pairs=calloc(recorded_pairs+3u,sizeof(*pairs));assert(pairs);
 pairs[0]=(AtomDeepCopyArenaPair){&source->storage,&image->storage};
 pairs[1]=(AtomDeepCopyArenaPair){&syntax,&syntax};size_t pairs_n=2u;
 if(space.universe&&space.universe->persistent_arena&&space.universe->persistent_arena!=&syntax)
  pairs[pairs_n++]=(AtomDeepCopyArenaPair){space.universe->persistent_arena,space.universe->persistent_arena};
 for(size_t i=0u;i<recorded_pairs;i++)CHECK(prime_need_recording_copy_pair(recording,i,&pairs[pairs_n++]));
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(&image->storage);assert(atoms);
 bool mapping=atom_deep_copy_session_set_arena_map(atoms,pairs,pairs_n);free(pairs);
 CHECK(mapping);
 const char *reason=NULL;
 CHECK(!prime_native_parent_copy_with_session(source,image,atoms,false,NULL,&reason));
 CHECK(reason&&!strcmp(reason,"RecordingOwner"));
 bool prepared=mapping&&prime_need_recording_copy_prepare(recording,atoms)&&prime_need_recording_copy_enable(atoms);
 CHECK(prepared);reason=NULL;
 bool copied=prepared&&prime_native_parent_copy_with_session(source,image,atoms,false,NULL,&reason);
 CHECK(copied);CHECK(!memcmp(&original,source,sizeof(original)));
 CHECK(recording_status_equal(captured,prime_need_recording_status(source->history.group)));
 CHECK(copied && arena_retains_owner(&image->storage,image->history.group,
     prime_need_recording_release)==paid_holder);
 CHECK(copied&&image->history.group&&image->history.group!=source->history.group);
 CHECK(copied&&image->history.count==source->history.count&&image->history.reservations==source->history.reservations&&
   image->history.start==source->history.start&&image->history.omitted==source->history.omitted&&image->history.overflow==source->history.overflow);
 CHECK(copied&&image->history.stopped==source->history.stopped&&
  image->history.stop==source->history.stop&&image->history.spec.enabled==source->history.spec.enabled);
 if(copied){
  PrimeNeedRecordingStatus x=prime_need_recording_status(source->history.group),y=prime_need_recording_status(image->history.group);
  CHECK(recording_status_equal(x,y));
  Arena readout;arena_init_detached(&readout);
  Atom *xv=prime_native_history_atom(source,&readout),*yv=prime_native_history_atom(image,&readout);
  CHECK(atom_eq(xv,yv));
  if(!atom_eq(xv,yv))fprintf(stderr,"PREFIX %u/%u\n%s\n%s\n",client,cut,atom_to_string(&readout,xv),atom_to_string(&readout,yv));
  arena_free(&readout);
 }
 if(copied){
  CopyRelation relation;CHECK(copy_relation(source,image,&relation));
  const PrimeWeightLicence *a=source->weight_analyses,*b=image->weight_analyses;
  for(;a&&b;a=a->next,b=b->next){CHECK(a->phase==b->phase&&a->steps==b->steps&&a->generation==b->generation&&a->work_count==b->work_count&&a->head_position==b->head_position);CHECK(!memcmp(&a->equations,&b->equations,sizeof(a->equations)));CHECK(a->control!=b->control&&a->read_owner!=b->read_owner);CHECK(copy_bindings(&relation,&a->environment,&b->environment));CHECK(copy_bindings(&relation,&a->candidate,&b->candidate));}
  CHECK(!a&&!b);
  printf("RECORDED_ALIASES %u/%u/%u classes=%zu shared=%zu\n",recording_profile,client,cut,relation.count,relation.shared);
  CHECK(image->event_cost.length==source->event_cost.length);
  CHECK(!source->event_cost.length||!memcmp(image->event_cost.events,source->event_cost.events,source->event_cost.length*sizeof(*source->event_cost.events)));
  CHECK(image->steps==source->steps);CHECK(!memcmp(&image->event_cost.total,&source->event_cost.total,sizeof(image->event_cost.total)));
  CHECK(!memcmp(&image->hub.schedule,&source->hub.schedule,sizeof(image->hub.schedule)));
  CHECK(image->hub.store.identity!=source->hub.store.identity);
  CHECK(cetta_continuation_hub_length(&image->hub)==cetta_continuation_hub_length(&source->hub));
  for(size_t i=0;i<cetta_continuation_hub_length(&image->hub);i++){
   const CettaOwnedContinuation *x=cetta_continuation_hub_at(&source->hub,i),*y=cetta_continuation_hub_at(&image->hub,i);
   CHECK(x->occurrence_id==y->occurrence_id&&x->parent_occurrence_id==y->parent_occurrence_id&&x->payload!=y->payload);
  }
 }
 atom_deep_copy_session_free(atoms);
 prime_need_recording_copy_free(recording);
 assert(copied);
 int channel[2]={-1,-1};pid_t child=-1;
 if(copied){
  assert(pipe(channel)==0);fflush(NULL);child=fork();assert(child>=0);
  if(!child){
   close(channel[0]);prime_native_control_destroy(source);source=NULL;
   checkpoint_scope_run(image,3000u,&b,&syntax,client,cut);
   ForkObservation *observed=calloc(1u,sizeof(*observed));assert(observed);
   observed->steps=image->steps;observed->total=image->event_cost.total;
   observed->event_count=image->event_cost.length;assert(observed->event_count<=8192u);
   memcpy(observed->events,image->event_cost.events,observed->event_count*sizeof(*observed->events));
   observed->closed=prime_native_control_closed(image);observed->pending=prime_native_control_waiting_verdict(image);
   Atom *hv=prime_native_history_atom(image,&b.storage);
   const char *ht=atom_to_string(&b.storage,hv);assert(strlen(ht)<sizeof(observed->history));strcpy(observed->history,ht);
   observed->count=b.count;memcpy(observed->lengths,b.lengths,sizeof(b.lengths));memcpy(observed->factors,b.factors,sizeof(b.factors));
   for(size_t i=0;i<b.count;i++){const char *t=atom_to_string(&b.storage,b.values[i]);assert(strlen(t)<sizeof(observed->values[i]));strcpy(observed->values[i],t);}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
   for(size_t i=0;i<b.count;i++)if(b.graphs[i]){assert(strlen(b.graphs[i])<sizeof(observed->graphs[i]));strcpy(observed->graphs[i],b.graphs[i]);}
#endif
   prime_native_control_destroy(image);image=NULL;
   bool sent=exact_io(channel[1],observed,sizeof(*observed),true);free(observed);close(channel[1]);
   arena_free(&a.storage);arena_free(&b.storage);arena_free(&prefix.storage);
   if(lib){cetta_library_context_free(lib);free(lib);}eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
   int (*leak_check)(void)=(int (*)(void))dlsym(RTLD_DEFAULT,"__lsan_do_recoverable_leak_check");
   int leaks=leak_check?leak_check():0;
   _exit(!sent?2:leaks?3:0);
  }
  close(channel[1]);
 }
 checkpoint_scope_run(source,3000u,&a,&syntax,client,cut);
 uint64_t steps=source->steps;bool closed=prime_native_control_closed(source),pending=prime_native_control_waiting_verdict(source);
 CettaNativeCostSnapshot total=source->event_cost.total;
 size_t event_count=source->event_cost.length;
 CettaNativeCostEvent *events=malloc(event_count*sizeof(*events));assert(!event_count||events);
 if(event_count)memcpy(events,source->event_cost.events,event_count*sizeof(*events));
 Atom *hv=prime_native_history_atom(source,&a.storage);
 const char *ht=atom_to_string(&a.storage,hv);
 prime_native_control_destroy(source);source=NULL;
 uint64_t image_steps=image->steps;
 if(copied){
  ForkObservation *observed=calloc(1u,sizeof(*observed));assert(observed);
  bool received=exact_io(channel[0],observed,sizeof(*observed),false);close(channel[0]);
  int status=0;assert(waitpid(child,&status,0)==child);CHECK(received&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  if(received){
   CHECK(!strcmp(ht,observed->history));
   if(strcmp(ht,observed->history))fprintf(stderr,"FUTURE HISTORY %u/%u\n%s\n%s\n",client,cut,ht,observed->history);
   image_steps=observed->steps;CHECK(observed->steps==steps);CHECK(!memcmp(&observed->total,&total,sizeof(total)));
   CHECK(observed->event_count==event_count);
   for(size_t e=0;e<event_count&&e<observed->event_count;e++){
    const CettaNativeCostEvent *x=&events[e],*y=&observed->events[e];
    CHECK(x->identity==y->identity&&x->origin==y->origin&&x->occurrence==y->occurrence&&
        x->units==y->units&&x->repetitions==y->repetitions&&x->kind==y->kind);
   }
   CHECK(observed->closed==closed);CHECK(observed->pending==pending);b.count=observed->count;
   CHECK(a.count==b.count);
   for(size_t i=0;i<a.count&&i<b.count;i++){
    const char *t=atom_to_string(&a.storage,a.values[i]);
    if(strcmp(t,observed->values[i]))fprintf(stderr,"VALUE %u/%u/%zu SOURCE %s IMAGE %s\n",client,cut,i,t,observed->values[i]);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(!strcmp(a.graphs[i]?a.graphs[i]:"",observed->graphs[i]));
#endif
    CHECK(!strcmp(t,observed->values[i]));CHECK(a.lengths[i]==observed->lengths[i]);CHECK(!memcmp(a.factors[i],observed->factors[i],sizeof(a.factors[i])));
   }
  }
  free(observed);
 }
 static const size_t expected[]={1u,3u,2u,0u,3u,2u,2u,2u,0u,2u,2u,0u};
 CHECK(prefix.count+a.count==expected[client]);
 CHECK(prefix.count+b.count==expected[client]);
 if(client==3u)CHECK(pending&&!closed);
 free(events);
 printf("{\"recording_profile\":%u,\"client\":%u,\"cut\":%u,\"copied\":%s,\"reason\":\"%s\",\"prefix\":%zu,\"source_answers\":%zu,\"image_answers\":%zu,\"source_steps\":%"PRIu64",\"image_steps\":%"PRIu64",\"failures\":%zu}\n",recording_profile,client,cut,copied?"true":"false",reason?reason:"",prefix.count,a.count,b.count,steps,image_steps,failures);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS

#endif
 prime_native_control_destroy(image);arena_free(&a.storage);arena_free(&b.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
}
static void recorded_refusal_one(unsigned mode,unsigned cut) {
 unsigned client=300u+mode;
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
 Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
 registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
 cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
 space_add(&space,parse(&syntax,"(D 3)"));space_add(&space,parse(&syntax,"(D 3)"));
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);
 CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,
  .work_budget=UINT64_MAX,.costs=true,
  .history={.enabled=true,.selections=512u,.nodes=64u,.bytes=65536u,.owners=1024u}};
 assert(prime_native_control_init(source,&space,&syntax,
  parse(&syntax,"(match &self (D $x) $x (where 2))"),-1,true,
  (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
 Result prefix={0},result={0};arena_init(&prefix.storage);arena_init(&result.storage);
 prime_native_control_run(source,cut,seen,&prefix);
 source->active_key=parse(&syntax,"(RetainedSource 99173)");
 PrimeNativeControl *image=calloc(1u,sizeof(*image));assert(image);
 arena_init(&image->storage);image->owned_arena=true;outcome_set_init(&image->output);
 PrimeNeedRecording *foreign=NULL,*group=source->history.group;
 if(mode==8u) {
  foreign=prime_need_recording_new((PrimeNeedRecordingLimits){.nodes=1u,.payload_bytes=1u,.slices=1u,.owner_links=1u});
  assert(foreign);group=foreign;
 }
 PrimeNeedRecordingCopy *recording=prime_need_recording_copy_new(group);assert(recording);
 size_t recorded_pairs=prime_need_recording_copy_pair_count(recording),count=0u;
 AtomDeepCopyArenaPair *pairs=calloc(recorded_pairs+3u,sizeof(*pairs));assert(pairs);
 pairs[count++]=(AtomDeepCopyArenaPair){&source->storage,&image->storage};
 if(mode!=6u)pairs[count++]=(AtomDeepCopyArenaPair){&syntax,&syntax};
 if(space.universe&&space.universe->persistent_arena&&space.universe->persistent_arena!=&syntax)
  pairs[count++]=(AtomDeepCopyArenaPair){space.universe->persistent_arena,space.universe->persistent_arena};
 for(size_t i=0u;i<recorded_pairs;i++)CHECK(prime_need_recording_copy_pair(recording,i,&pairs[count++]));
 if(mode==3u){CHECK(recorded_pairs>0u);count--;}
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(&image->storage);assert(atoms);
 CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));free(pairs);
 bool prepared=mode!=1u&&prime_need_recording_copy_prepare(recording,atoms);
 CHECK(prepared==(mode!=1u&&mode!=3u));
 if(mode!=0u)CHECK(prime_need_recording_copy_enable(atoms));
 PrimeNeedReceipt active={0};PrimeNeedRecordingScope scope={0};
 if(mode==4u||mode==2u) {
  CHECK(prime_need_recording_reserve(source->history.group,NULL,1u,1u,&active));
  if(mode==2u)prime_need_recording_enter(&scope,&active);
 }
 if(mode==5u)arena_reset(&image->storage,arena_mark(&image->storage));
 PrimeNativeControl original=*source;
 PrimeNeedRecordingStatus captured=prime_need_recording_status(source->history.group);
 Atom *before=prime_native_history_atom(source,&prefix.storage);
 const char *reason=NULL;
 bool copied=mode!=7u&&prime_native_parent_copy_with_session(source,image,atoms,false,NULL,&reason);
 prime_need_recording_leave(&scope);
 CHECK(!copied);CHECK(!memcmp(&original,source,sizeof(original)));
 CHECK(recording_status_equal(captured,prime_need_recording_status(source->history.group)));
 CHECK(atom_eq(before,prime_native_history_atom(source,&result.storage)));
 CHECK(!image->history.group);
 if(mode==0u||mode==1u||mode==2u||mode==3u||mode==4u||mode==8u)
  CHECK(reason&&!strcmp(reason,"RecordingOwner"));
 if(mode==5u||mode==7u)CHECK(!reason);
 CHECK(!arena_retains_owner(&image->storage,source->history.group,prime_need_recording_release));
 atom_deep_copy_session_free(atoms);
 prime_native_control_destroy(image);
 prime_need_recording_copy_free(recording);prime_need_recording_release(foreign);
 prime_native_control_run(source,3000u,seen,&result);
 CHECK(prime_native_control_closed(source));CHECK(prefix.count+result.count==2u);
 printf("RECORDED_REFUSAL mode=%u cut=%u reason=%s failures=%zu\n",mode,cut,reason?reason:"",failures);
 prime_native_control_destroy(source);arena_free(&result.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
 registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
}

static void recorded_public_refusal(unsigned cut) {
 unsigned client=320u;
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
 Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
 registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
 cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);
 CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,
  .work_budget=UINT64_MAX,.costs=true,
  .history={.enabled=true,.selections=512u,.nodes=64u,.bytes=65536u,.owners=1024u}};
 assert(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(weight:where 7 3)"),-1,true,
  (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
 Result prefix={0},result={0};arena_init(&prefix.storage);arena_init(&result.storage);
 prime_native_control_run(source,cut,seen,&prefix);
 uint64_t id=0u;assert(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));source->handle_id=id;
 Atom *handle=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(handle);
 PrimeNativeControl original=*source;
 PrimeNeedRecordingStatus captured=prime_need_recording_status(source->history.group);
 CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,handle,&status);
 CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
 size_t count=0u;
 AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&count);
 CHECK(pairs);assert(pairs);
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
 CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));
 free(pairs);
 CHECK(cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_UNSUPPORTED);
 CHECK(!strcmp(cetta_prime_checkpoint_copy_refusal(copy),"RecordingOwner"));
 atom_deep_copy_session_free(atoms);cetta_prime_checkpoint_copy_free(copy);
 CHECK(!memcmp(&original,source,sizeof(original)));
 CHECK(recording_status_equal(captured,prime_need_recording_status(source->history.group)));
 prime_native_control_run(source,3000u,seen,&result);
 CHECK(prime_native_control_closed(source)&&prefix.count+result.count==1u);
 CHECK(cetta_native_handle_close(lib,"prime-search",id));
 printf("RECORDED_PUBLIC_REFUSAL cut=%u failures=%zu\n",cut,failures);
 arena_free(&result.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
 registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
}

static void recorded_cases(void) {
 const unsigned cuts[]={0,1,3,7,13,19,31,49,79,149,163,401};
 for(recording_profile=1u;recording_profile<=6u;recording_profile++)
  for(unsigned c=0;c<12;c++)for(size_t i=0;i<sizeof(cuts)/sizeof(*cuts);i++)
   recorded_one(c,cuts[i]);
 printf("RECORDED_OWNER_VIEWS paid=%zu truncated=%zu prepared=%zu\n",
  recorded_paid_holders,recorded_truncated_views,recorded_preparation_views);
 assert(recorded_paid_holders && recorded_truncated_views && recorded_preparation_views);
 for(unsigned mode=0u;mode<9u;mode++) {
  if(mode!=3u)recorded_refusal_one(mode,0u);
  recorded_refusal_one(mode,31u);
 }
 recorded_public_refusal(0u);recorded_public_refusal(31u);
 recording_profile=0u;
}
