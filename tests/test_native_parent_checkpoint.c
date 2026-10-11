/* Complete parent copying through registered residual ownership. Fork is a
 * test-only equal-start allocator context, with one evaluator thread and no
 * active foreign engine; it does not implement production replay. */
#include "../src/eval.c"
#include "parser.h"
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <dlfcn.h>
#include "native_parent_checkpoint_compare.inc"
static Atom *parse(Arena *a,const char *t){Atom **x=NULL;int n=parse_metta_text(t,a,&x);assert(n==1);Atom *r=x[0];free(x);return r;}
typedef struct {Arena storage;Atom *values[32];size_t count;size_t lengths[32];int64_t factors[32][16];
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 char *graphs[32];
#endif
} Result;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static unsigned recording_profile;
/* Serialize the existing checked DAG view while its payload owners are live.
 * Every scalar field, predecessor and payload is compared, including joins.
 * This fixture has no StateCell effects; no pointer-to-wire claim is made. */
static char *receipt_text(Arena *arena,const PrimeNeedReceipt *receipt) {
 PrimeNeedReceiptGraph graph={0};
 PrimeNeedReceiptGraphStatus status=prime_need_receipt_graph_collect(receipt,&graph);
 assert(status==PRIME_NEED_RECEIPT_GRAPH_COMPLETE ||
        (recording_profile && status==PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE));
 char *text=NULL;size_t length=0;FILE *stream=open_memstream(&text,&length);assert(stream);
 fprintf(stream,"%d/%"PRIu64"/%"PRIu64"/%zu/%zu/%d;",status,graph.session_id,graph.root_id,graph.length,graph.event_count,graph.incomplete);
 for(size_t i=0;i<graph.length;i++) {
  const PrimeNeedReceiptGraphNode *n=&graph.nodes[i];const PrimeNeedReceiptEvent *e=&n->event;
  assert(!e->state_cell);
  fprintf(stream,"%"PRIu64",%"PRIu64",%"PRIu64",%d,%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%d;",
   n->identity_id,n->left_id,n->right_id,n->has_event,(unsigned)e->kind,e->event_id,e->need_session_id,e->thunk_id,e->source_occurrence_id,e->source_argument_index,e->rule_occurrence_id,e->producer_update_id,e->producer_completed);
  const char *before=e->before?atom_to_string(arena,e->before):"<null>";
  const char *after=e->after?atom_to_string(arena,e->after):"<null>";
  fprintf(stream,"%d:%zu:%s%d:%zu:%s;",e->before!=NULL,strlen(before),before,e->after!=NULL,strlen(after),after);
 }
 assert(fclose(stream)==0);assert(strlen(text)==length);
 char *stored=arena_alloc(arena,length+1u);assert(stored);memcpy(stored,text,length+1u);free(text);
 prime_need_receipt_graph_free(&graph);return stored;
}
#endif
static bool seen(Arena *a,Atom *v,const Bindings *b,void *raw){(void)a;Result *r=raw;assert(r->count<32);size_t i=r->count++;r->values[i]=atom_deep_copy(&r->storage,v);const CettaWeightLedger *l=bindings_weight_view(b);assert(l->length<=16);r->lengths[i]=l->length;const CettaWeightFactor **f=cetta_weight_ledger_ordered(l);assert(!l->length||f);for(size_t k=0;k<l->length;k++){assert(f[k]->value->kind==ATOM_GROUNDED&&f[k]->value->ground.gkind==GV_INT);r->factors[i][k]=f[k]->value->ground.ival;}free(f);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 if(prime_need_receipt_observer_requested())assert(prime_need_receipt_present(bindings_receipt_view(b)));
 if(prime_need_receipt_present(bindings_receipt_view(b)))r->graphs[i]=receipt_text(&r->storage,bindings_receipt_view(b));
#endif
return true;}
typedef struct {
 uint64_t steps; CettaNativeCostSnapshot total; bool closed,pending; size_t count;
 size_t event_count; CettaNativeCostEvent events[8192];
 size_t lengths[32]; int64_t factors[32][16]; char values[32][2048];
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 char graphs[32][32768];
 char history[1048576];
#endif
} ForkObservation;
static bool exact_io(int fd,void *buffer,size_t size,bool writing){
 unsigned char *p=buffer;while(size){ssize_t n=writing?write(fd,p,size):read(fd,p,size);
 if(n<0&&errno==EINTR)continue;
 if(n<=0)return false;
 p+=n;size-=(size_t)n;}return true;
}
static size_t checks,failures;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static size_t causal_frame_views;
static void request_causal_receipt(Atom *answer,const PrimeNeedReceipt *receipt,void *context) {
 (void)answer;(void)receipt;(void)context;
}
#endif
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %u/%u %s\n",client,cut,#x);}}while(0)
#include "native_parent_run_scope.inc"
static void one(unsigned client,unsigned cut){
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);Space space;space_init(&space);
 Registry reg;registry_init(&reg);g_registry=&reg;registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
 const char *rows[]={"(=w (coin) 2 3)","(=w (coin) 2 3)","(=w (coin) 5 7)","(=w (weigh-by-self $x) $x $x)","(= (paired $x) (Pair $x $x))","(D 3)","(D 3)","(= (accepted $x) Mystery)","(=w (priced) Cheap 1)","(=w (priced) Costly 3)","(= (offers) (match &self (D $x) (Offer $x) (where $x)))"};
 for(size_t i=0;i<sizeof(rows)/sizeof(*rows);i++)space_add(&space,parse(&syntax,rows[i]));
 const char *programs[]={"(Pair (weight:where 2 3) (weight:where 5 7))","(paired (weigh-by-self (coin)))","(match &self (D $x) $x (where 2))","(weight:where 7 Mystery (admit accepted))","(weight:read (algebra 0 1 + *) (paired (weigh-by-self (coin))))","(weight:read (algebra 1000000 0 min +) (priced))","(weight:read (algebra 1000000 0 min +) (offers))","(superpose (A B C))","(superpose (A A A))","(superpose (A A A))","(superpose (3 1))","(superpose ())"};
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true};
 if(client>=5u&&client<=6u){policy.by_weight=true;policy.descending=false;policy.priority=2;policy.age_share=2;policy.weight_law=prime_need_symbols()->law_nonnegative_sum;}
 if(client>=7u){policy.selection=client<=9u?1:2;policy.all=false;policy.goal_pattern=parse(&syntax,"$bundle");policy.goal_expression=client>=10u?policy.goal_pattern:parse(&syntax,client==8u?"False":"True");}
 if(client>=10u)CHECK(policy.goal_pattern==policy.goal_expression);
 Bindings inherited;bindings_init(&inherited);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 assert(prime_need_receipt_begin(&syntax,bindings_receipt_mut(&inherited)));
 CettaPrimeNeedAnswerObserver prior_observer=g_prime_need_answer_observer;
 g_prime_need_answer_observer=request_causal_receipt;
#endif
 assert(prime_native_control_init(source,&space,&syntax,parse(&syntax,programs[client]),-1,true,(CettaObservationDemand){.completion=client>=7u?CETTA_OBSERVATION_FINITE_PREFIX:CETTA_OBSERVATION_COMPLETE_BAG,.prefix_limit=client==10u?3u:client>=7u?2u:0u},&policy,&inherited,NULL,NULL));
 bindings_free(&inherited);
 Result a={0},b={0},prefix={0};arena_init(&a.storage);arena_init(&b.storage);arena_init(&prefix.storage);
 prime_native_control_run(source,cut,policy.selection?checkpoint_first_seen:seen,&prefix);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 for(const PrimeEvalStackFrame *f=source->frames;f;f=f->control_next) {
  if(f->before_receipt.top)causal_frame_views++;
  if(f->producer_receipt.top)causal_frame_views++;
 }
#endif
 uint64_t source_handle=0u,image_handle=0u;
 assert(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&source_handle));
 source->handle_id=source_handle;
 Atom *source_atom=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",source_handle);assert(source_atom);
 PrimeNativeControl original=*source;
 CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,source_atom,&status);
 CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
 CHECK(source->leased);
 AtomDeepCopyArenaPair pairs[4];CHECK(cetta_prime_checkpoint_copy_arena_pair(copy,&pairs[0]));
 PrimeNativeControl *image=copy->image;
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
 pairs[1]=(AtomDeepCopyArenaPair){&syntax,&syntax};size_t pairs_n=2;
 if(space.universe&&space.universe->persistent_arena&&space.universe->persistent_arena!=&syntax)pairs[pairs_n++]=(AtomDeepCopyArenaPair){space.universe->persistent_arena,space.universe->persistent_arena};
 bool mapping=atom_deep_copy_session_set_arena_map(atoms,pairs,pairs_n);
 bool copied=mapping&&cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_READY;
 const char *reason=cetta_prime_checkpoint_copy_refusal(copy);
 CHECK(copied);CHECK(!memcmp(&original,source,sizeof(original)));
 if(copied){
  CopyRelation relation;CHECK(copy_relation(source,image,&relation));
  const PrimeWeightLicence *a=source->weight_analyses,*b=image->weight_analyses;
  for(;a&&b;a=a->next,b=b->next){CHECK(a->phase==b->phase&&a->steps==b->steps&&a->generation==b->generation&&a->work_count==b->work_count&&a->head_position==b->head_position);CHECK(!memcmp(&a->equations,&b->equations,sizeof(a->equations)));CHECK(a->control!=b->control&&a->read_owner!=b->read_owner);CHECK(copy_bindings(&relation,&a->environment,&b->environment));CHECK(copy_bindings(&relation,&a->candidate,&b->candidate));}
  CHECK(!a&&!b);
  printf("ALIASES %u/%u classes=%zu shared=%zu\n",client,cut,relation.count,relation.shared);
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
 Atom *image_atom=NULL;
 CHECK(copied&&cetta_prime_checkpoint_copy_publish(copy,&syntax,&image_atom)==CETTA_CONTINUATION_READY);
 assert(image_atom&&cetta_native_handle_arg(image_atom,"prime-search",&image_handle));
 CHECK(image_handle!=source_handle);CHECK(cetta_native_handle_get_owned(lib,"prime-search",image_atom)==image);
 cetta_prime_checkpoint_copy_free(copy);
 int channel[2]={-1,-1};pid_t child=-1;
 if(copied){
  assert(pipe(channel)==0);fflush(NULL);child=fork();assert(child>=0);
  if(!child){
   close(channel[0]);assert(cetta_native_handle_close(lib,"prime-search",source_handle));source=NULL;
   checkpoint_scope_run(image,3000u,&b,&syntax,client,cut);
   ForkObservation *observed=calloc(1u,sizeof(*observed));assert(observed);
   observed->steps=image->steps;observed->total=image->event_cost.total;
   observed->event_count=image->event_cost.length;assert(observed->event_count<=8192u);
   memcpy(observed->events,image->event_cost.events,observed->event_count*sizeof(*observed->events));
   observed->closed=prime_native_control_closed(image);observed->pending=prime_native_control_waiting_verdict(image);
   observed->count=b.count;memcpy(observed->lengths,b.lengths,sizeof(b.lengths));memcpy(observed->factors,b.factors,sizeof(b.factors));
   for(size_t i=0;i<b.count;i++){const char *t=atom_to_string(&b.storage,b.values[i]);assert(strlen(t)<sizeof(observed->values[i]));strcpy(observed->values[i],t);}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
   for(size_t i=0;i<b.count;i++)if(b.graphs[i]){assert(strlen(b.graphs[i])<sizeof(observed->graphs[i]));strcpy(observed->graphs[i],b.graphs[i]);}
#endif
   assert(cetta_native_handle_close(lib,"prime-search",image_handle));image=NULL;
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
 assert(cetta_native_handle_close(lib,"prime-search",source_handle));source=NULL;
 uint64_t image_steps=image->steps;
 if(copied){
  ForkObservation *observed=calloc(1u,sizeof(*observed));assert(observed);
  bool received=exact_io(channel[0],observed,sizeof(*observed),false);close(channel[0]);
  int status=0;assert(waitpid(child,&status,0)==child);CHECK(received&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  if(received){
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
 printf("{\"client\":%u,\"cut\":%u,\"copied\":%s,\"reason\":\"%s\",\"prefix\":%zu,\"source_answers\":%zu,\"image_answers\":%zu,\"source_steps\":%"PRIu64",\"image_steps\":%"PRIu64",\"failures\":%zu}\n",client,cut,copied?"true":"false",reason?reason:"",prefix.count,a.count,b.count,steps,image_steps,failures);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 g_prime_need_answer_observer=prior_observer;
#endif
 assert(cetta_native_handle_close(lib,"prime-search",image_handle));arena_free(&a.storage);arena_free(&b.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
}
static void lifecycle_cases(void);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
#include "native_parent_recording_compare.inc"
#endif
int main(void){SymbolTable syms;symbol_table_init(&syms);symbol_table_init_builtins(&syms,&g_builtin_syms);g_symbols=&syms;VarInternTable vars;var_intern_init(&vars);g_var_intern=&vars;g_hashcons=NULL;
 const unsigned cuts[]={0,1,3,7,13,19,31,49,79,149,163,401};
 for(unsigned c=0;c<12;c++)for(size_t i=0;i<sizeof(cuts)/sizeof(*cuts);i++)one(c,cuts[i]);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 printf("CAUSAL_FRAME_VIEWS %zu\n",causal_frame_views);
 assert(causal_frame_views>0u);
 recorded_cases();
#endif
 checkpoint_selection_resume_cases();
 checkpoint_captured_scope_cases();
 lifecycle_cases();
 eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;}

typedef struct { unsigned retains, releases; } RetirementCounter;
static void count_retention(void *raw) { ++((RetirementCounter *)raw)->retains; }
static void count_retirement(void *raw) { ++((RetirementCounter *)raw)->releases; }
static void lifecycle_cases(void) {
 for(unsigned mode=0u;mode<10u;mode++) {
  unsigned client=100u+mode,cut=19u;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
  registry_bind(&reg,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
  space_add(&space,parse(&syntax,"(D 3)"));space_add(&space,parse(&syntax,"(D 3)"));
  PrimeNativeControl *source=malloc(sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true};
  assert(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(match &self (D $x) $x (where 2))"),-1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
  Result prefix={0},result={0};arena_init(&prefix.storage);arena_init(&result.storage);
  prime_native_control_run(source,cut,seen,&prefix);
  uint64_t id=0u;assert(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));source->handle_id=id;
  Atom *handle=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(handle);
  if(mode==6u){source->active_key=parse(&syntax,"(Held 99173)");assert(arena_owns_atom(&syntax,source->active_key));}
  if(mode==7u){arena_init(&source->driver.prepared_pure_cache.execution_scratch);source->driver.prepared_pure_cache.execution_scratch_ready=true;}
  PrimeNativeControl original=*source;
  CettaContinuationStatus status;
  Atom *textual=cetta_native_handle_atom(&syntax,"prime-search",id);
  CHECK(!cetta_prime_checkpoint_copy_new(lib,textual,&status));
  CHECK(status==CETTA_CONTINUATION_INVALIDATED);
  if(mode==7u) {
   uint64_t issued=lib->native_handle_next_id;uint32_t slots=lib->native_handle_len;
   CHECK(!cetta_prime_checkpoint_copy_new(lib,handle,&status));
   CHECK(status==CETTA_CONTINUATION_UNSUPPORTED);
   CHECK(!memcmp(&original,source,sizeof(original)));
   CHECK(lib->native_handle_next_id==issued&&lib->native_handle_len==slots);
   arena_free(&source->driver.prepared_pure_cache.execution_scratch);
   source->driver.prepared_pure_cache.execution_scratch_ready=false;
   original=*source;
  }
  CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,handle,&status);CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
  CHECK(source->leased);CHECK(!cetta_prime_checkpoint_copy_new(lib,handle,&status)&&status==CETTA_CONTINUATION_DEFERRED);
  AtomDeepCopyArenaPair pairs[3];CHECK(cetta_prime_checkpoint_copy_arena_pair(copy,&pairs[0]));
  pairs[1]=(AtomDeepCopyArenaPair){&syntax,&syntax};size_t count=2u;
  if(space.universe&&space.universe->persistent_arena&&space.universe->persistent_arena!=&syntax)pairs[count++]=(AtomDeepCopyArenaPair){space.universe->persistent_arena,space.universe->persistent_arena};
  AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
  if(mode==6u)pairs[1]=pairs[--count];
  if(mode!=1u)CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));
  if(mode==7u) {
   arena_init(&source->driver.prepared_pure_cache.execution_scratch);
   source->driver.prepared_pure_cache.execution_scratch_ready=true;
   original.driver.prepared_pure_cache=source->driver.prepared_pure_cache;
  }
  if(mode==0u){atom_deep_copy_session_free(atoms);atoms=NULL;cetta_prime_checkpoint_copy_free(copy);copy=NULL;}
  else {
   if(mode==4u){CHECK(cetta_native_handle_close(lib,"prime-search",id));source=NULL;}
   if(mode==5u)eval_set_library_context(NULL);
   if(mode==8u){cetta_library_context_free(lib);free(lib);lib=NULL;source=NULL;eval_set_library_context(NULL);CHECK(!copy->context&&!copy->source&&!copy->image&&!copy->reserved);}
   status=cetta_prime_checkpoint_copy_prepare(copy,atoms);
   if(mode==5u)eval_set_library_context(lib);
   CHECK(status==((mode==2u||mode==3u||mode==9u)?CETTA_CONTINUATION_READY:
     (mode==4u||mode==5u||mode==8u)?CETTA_CONTINUATION_INVALIDATED:CETTA_CONTINUATION_UNSUPPORTED));
   CHECK(cetta_prime_checkpoint_copy_prepare(copy,atoms)==status);
   if(mode==7u)CHECK(copy->refusal&&!strcmp(copy->refusal,"QuiescentOwnerProfile"));
   atom_deep_copy_session_free(atoms);atoms=NULL;
  }
  if(source)CHECK(!memcmp(&original,source,sizeof(original)));
  if(mode==9u) {
   CHECK(copy->prepared&&copy->image&&copy->image->frames&&cetta_continuation_hub_length(&copy->image->hub)>0u);
   RetirementCounter retirements={0};
   CHECK(arena_retain_owner(&copy->image->storage,&retirements,count_retention,count_retirement));
   CHECK(retirements.retains==1u&&retirements.releases==0u);
   uint64_t watch=copy->context_watch;
   CettaStorageCensus census={0};prime_checkpoint_storage(copy,&census);cetta_storage_finish(&census);CHECK(census.retained>sizeof(*copy));cetta_storage_census_free(&census);
   cetta_library_context_free(lib);eval_set_library_context(NULL);source=NULL;
   CHECK(!copy->context&&!copy->image&&!copy->source&&!copy->reserved&&!copy->prepared);
   CHECK(retirements.retains==1u&&retirements.releases==1u);
   CHECK(!cetta_native_handle_close(lib,"prime-checkpoint-copy",watch));
   free(lib);lib=NULL;
   Atom *unchanged=atom_symbol(&syntax,"Unchanged"),*result_handle=unchanged;
   CHECK(cetta_prime_checkpoint_copy_prepare(copy,NULL)==CETTA_CONTINUATION_INVALIDATED);
   CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&result_handle)==CETTA_CONTINUATION_INVALIDATED);
   CHECK(result_handle==unchanged);cetta_prime_checkpoint_copy_free(copy);copy=NULL;CHECK(retirements.releases==1u);
  } else if(mode==2u||mode==3u) {
   PrimeNativeControl *image=copy->image;PrimeNativeControl prepared=*image;
   Atom *sentinel=atom_symbol(&syntax,"Unchanged"),*published=sentinel;
   uint64_t next_id=lib->native_handle_next_id;lib->native_handle_next_id=0u;
   CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&published)==CETTA_CONTINUATION_CAPACITY);
   CHECK(published==sentinel&&!memcmp(&prepared,image,sizeof(prepared)));
   CHECK(cetta_native_handle_close(lib,"prime-search",id));source=NULL;
   if(mode==3u)space_add(&space,parse(&syntax,"(D 9)"));
   CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&published)==CETTA_CONTINUATION_CAPACITY);
   CHECK(published==sentinel&&!memcmp(&prepared,image,sizeof(prepared)));
   lib->native_handle_next_id=next_id;
   CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&published)==CETTA_CONTINUATION_READY);
   CHECK(cetta_native_handle_get_owned(lib,"prime-search",published)==image);
   uint64_t image_id=0u;assert(cetta_native_handle_arg(published,"prime-search",&image_id));
   CHECK(image_id!=id);CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&published)==CETTA_CONTINUATION_UNSUPPORTED);
   cetta_prime_checkpoint_copy_free(copy);copy=NULL;
   CettaNativeCostSnapshot paid=image->event_cost.total;uint64_t steps=image->steps;
   prime_native_control_run(image,0u,seen,&result);
   CHECK(image->invalidated==(mode==3u));CHECK(image->steps==steps&&!memcmp(&paid,&image->event_cost.total,sizeof(paid)));
   CettaSearchPolicySpec resume_policy=image->policy;resume_policy.work_budget=1000u;
   OutcomeSet resumed;outcome_set_init(&resumed);
   CHECK(prime_native_control_select(&space,&syntax,published,-1,0u,&resume_policy,&resumed,NULL,NULL));
   CHECK(resumed.len==1u&&resumed.items[0].atom->kind==ATOM_EXPR&&atom_is_symbol(resumed.items[0].atom->expr.elems[0],"Observation"));
   CHECK(mode==3u?image->invalidated&&image->steps==steps:prime_native_control_closed(image)&&image->delivered_count==2u);
   CHECK(cetta_native_handle_get_owned(lib,"prime-search",published)==image);outcome_set_free(&resumed);
   CHECK(cetta_native_handle_close(lib,"prime-search",image_id));
  } else {
   cetta_prime_checkpoint_copy_free(copy);copy=NULL;
   if(source){
    CHECK(!memcmp(&original,source,sizeof(original)));
    prime_native_control_run(source,1000u,seen,&result);
    CHECK(prime_native_control_closed(source)&&prefix.count+result.count==2u);
    CHECK(cetta_native_handle_close(lib,"prime-search",id));source=NULL;
   }
  }
  printf("LIFECYCLE mode=%u failures=%zu\n",mode,failures);
  arena_free(&prefix.storage);arena_free(&result.storage);
  if(lib){cetta_library_context_free(lib);free(lib);}eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
 /* Registry-local integers are insufficient ownership evidence. */
 unsigned client=200u,cut=0u;
 Arena arena;arena_init(&arena);CettaLibraryContext *left=malloc(sizeof(*left)),*right=malloc(sizeof(*right));assert(left&&right);
 cetta_library_context_init_for_language_profile(left,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
 cetta_library_context_init_for_language_profile(right,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
 Space space;space_init(&space);Registry registry;registry_init(&registry);g_registry=&registry;
 registry_bind(&registry,"&self",atom_space(&arena,&space));
 PrimeNativeControl *x=malloc(sizeof(*x)),*y=malloc(sizeof(*y));assert(x&&y);
 CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true};
 eval_set_library_context(left);
 assert(prime_native_control_init(x,&space,&arena,parse(&arena,"(Pair 7 9)"),-1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
 eval_set_library_context(right);
 assert(prime_native_control_init(y,&space,&arena,parse(&arena,"(Pair 7 9)"),-1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
 uint64_t a=0,b=0;assert(cetta_native_handle_alloc(left,"prime-search",x,prime_native_control_destroy,&a));assert(cetta_native_handle_alloc(right,"prime-search",y,prime_native_control_destroy,&b));CHECK(a==b);
 Atom *lh=cetta_native_handle_owned_atom(left,&arena,"prime-search",a),*rh=cetta_native_handle_owned_atom(right,&arena,"prime-search",b);assert(lh&&rh);
 CHECK(cetta_native_handle_get(left,"prime-search",b)==x);
 CHECK(cetta_native_handle_get_owned(left,"prime-search",lh)==x);
 CHECK(cetta_native_handle_get_owned(right,"prime-search",rh)==y);
 CHECK(!cetta_native_handle_get_owned(left,"prime-search",rh));CHECK(!cetta_native_handle_get_owned(right,"prime-search",lh));
 eval_set_library_context(left);CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *wrong=cetta_prime_checkpoint_copy_new(left,rh,&status);
 CHECK(!wrong&&status==CETTA_CONTINUATION_INVALIDATED);cetta_prime_checkpoint_copy_free(wrong);CHECK(!x->leased&&!y->leased);
 CHECK(cetta_native_handle_close(left,"prime-search",a));CHECK(!cetta_native_handle_get_owned(left,"prime-search",lh));
 cetta_library_context_free(left);cetta_library_context_free(right);free(left);free(right);eval_set_library_context(NULL);registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&arena);
}
