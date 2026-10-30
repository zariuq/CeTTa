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
static const void *recording_retirement_watch;
static unsigned recording_retirements;
static _Thread_local bool checkpoint_calloc_refuse;
static _Thread_local size_t checkpoint_calloc_countdown;
void __real_free(void *pointer);
void *__real_calloc(size_t count,size_t size);
void *__wrap_calloc(size_t count,size_t size) {
 if(checkpoint_calloc_refuse){checkpoint_calloc_refuse=false;errno=ENOMEM;return NULL;}
 if(checkpoint_calloc_countdown&&!--checkpoint_calloc_countdown){errno=ENOMEM;return NULL;}
 return __real_calloc(count,size);
}
void __wrap_free(void *pointer) {
 if(pointer && pointer==recording_retirement_watch) {
  recording_retirements++;recording_retirement_watch=NULL;
 }
 __real_free(pointer);
}
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
static bool seen(Arena *a,Atom *v,const Outcome *occurrence,void *raw){const Bindings *b=&occurrence->env;(void)a;Result *r=raw;assert(r->count<32);size_t i=r->count++;r->values[i]=atom_deep_copy(&r->storage,v);const CettaWeightLedger *l=bindings_weight_view(b);assert(l->length<=16);r->lengths[i]=l->length;const CettaWeightFactor **f=cetta_weight_ledger_ordered(l);assert(!l->length||f);for(size_t k=0;k<l->length;k++){assert(f[k]->value->kind==ATOM_GROUNDED&&f[k]->value->ground.gkind==GV_INT);r->factors[i][k]=f[k]->value->ground.ival;}free(f);
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
static size_t inspection_copies,readiness_copies,work_readiness_copies;
static bool inspection_refusals_checked;
static const size_t expected_answers[]={1u,3u,2u,0u,3u,2u,2u,2u,0u,2u,2u,0u,3u,7u,3u};
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static size_t causal_frame_views;
static void request_causal_receipt(Atom *answer,const PrimeNeedReceipt *receipt,void *context) {
 (void)answer;(void)receipt;(void)context;
}
#endif
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %u/%u %s\n",client,cut,#x);}}while(0)
#include "native_parent_run_scope.inc"

/* A checkpoint can own several arenas. Map every advertised owner and the
 * externally borrowed syntax/space arenas before copying any payload. */
static AtomDeepCopyArenaPair *checkpoint_arena_pairs(CettaPrimeCheckpointCopy *copy,
 Arena *syntax,const Space *space,bool include_syntax,size_t *count_out) {
 size_t count=cetta_prime_checkpoint_copy_arena_pair_count(copy);
 if(!count || count>SIZE_MAX-2u)return NULL;
 AtomDeepCopyArenaPair *pairs=calloc(count+2u,sizeof(*pairs));
 if(!pairs)return NULL;
 for(size_t i=0u;i<count;i++)
  if(!cetta_prime_checkpoint_copy_arena_pair_at(copy,i,&pairs[i])) {free(pairs);return NULL;}
 if(include_syntax)pairs[count++]=(AtomDeepCopyArenaPair){syntax,syntax};
 if(space->universe&&space->universe->persistent_arena&&space->universe->persistent_arena!=syntax)
  pairs[count++]=(AtomDeepCopyArenaPair){space->universe->persistent_arena,space->universe->persistent_arena};
 *count_out=count;return pairs;
}

typedef struct {
 Bindings environment;
 OutcomeSet output;
 PrimeEvalStackDriver driver,*previous;
 PrimeNeedActiveGuard active;
 Atom *reference;
 uint64_t thunk;
} CheckpointForkDemand;

static bool checkpoint_fork_drive(PrimeEvalStackDriver *driver,unsigned limit) {
 for(unsigned i=0u;i<limit&&(driver->stream_pending||driver->task_ready||driver->top);i++) {
  if(driver->capacity_blocked||eval_unwinding())return false;
  if(driver->stream_pending) {
   OutcomeSet *ready=driver->stream_pending;driver->stream_pending=NULL;
   prime_eval_stack_stream(ready);
  } else if(driver->task_ready)prime_eval_stack_run_task();
  else prime_eval_stack_resume_top();
 }
 return !driver->stream_pending&&!driver->task_ready&&!driver->top;
}

static bool checkpoint_fork_demand_open(CheckpointForkDemand *demand,
 Space *space,Arena *syntax,const Bindings *world,Atom *argument) {
 memset(demand,0,sizeof(*demand));
 Bindings allocated;bindings_init(&demand->environment);bindings_init(&allocated);
 bool ready=bindings_clone(&demand->environment,world);
 if(ready)ready=prime_need_allocate_ref(syntax,argument,
  &demand->environment,&allocated,&demand->reference);
 if(ready)bindings_replace(&demand->environment,&allocated);
 else bindings_free(&allocated);
 bool installed=false;
 if(ready)ready=prime_need_call_sources_prepare(space,syntax,&demand->reference,1u,
  prime_need_fresh_source_occurrence(),&demand->environment,&installed)==
  CETTA_MATCH_MATCHED&&installed;
 if(!ready){bindings_free(&demand->environment);return false;}
 outcome_set_init_with_owner(&demand->output,syntax);
 demand->driver=(PrimeEvalStackDriver){.arena=syntax,.root_target=&demand->output,
  .streaming=true,.heap_phase=true};
 demand->previous=g_prime_eval_stack_driver;g_prime_eval_stack_driver=&demand->driver;
 prime_need_active_enter_captured(&demand->active,&demand->environment);
 ready=prime_need_ref_belongs_to(demand->reference,
  bindings_need_view(&demand->environment),&demand->thunk);
 return ready;
}

static void checkpoint_fork_demand_close(CheckpointForkDemand *demand) {
 prime_eval_stack_abandon(&demand->driver);
 prepared_pure_program_cache_free(&demand->driver.prepared_pure_cache);
 prime_need_active_leave(&demand->active);g_prime_eval_stack_driver=demand->previous;
 outcome_set_free(&demand->output);bindings_free(&demand->environment);
}

static bool checkpoint_fork_force_existing(PrimeNativeControl *control,
 PrimeNeedCallSourceCell *cell,Space *here,Space *home,Atom *answer) {
 if(!control||!control->owned_top_episode||!cell||!here||!home||!answer)return false;
 PrimeNativeRunGuard run;prime_native_run_enter(control,&run);
 EvalContextState saved=eval_context_capture();
 if(g_context_here_depth>=64u||g_context_home_depth>=64u){prime_native_run_leave(&run);return false;}
 g_context_here_stack[g_context_here_depth++]=here;
 g_context_home_stack[g_context_home_depth++]=home;
 Bindings environment;bindings_init(&environment);
 const Bindings *requester=control->base_environment_initialized
  ? &control->base_environment:&cell->common;
 bool ready=bindings_clone(&environment,requester);
 Arena *arena=&control->owned_top_episode->arena;
 OutcomeSet output;outcome_set_init_with_owner(&output,arena);
 PrimeEvalStackDriver driver={.arena=arena,.root_target=&output,
  .streaming=true,.heap_phase=true};
 PrimeEvalStackDriver *previous=g_prime_eval_stack_driver;
 g_prime_eval_stack_driver=&driver;
 PrimeNeedActiveGuard active;prime_need_active_enter_captured(&active,&environment);
 bool scheduled=ready&&prime_need_call_force(control->space,arena,cell->reference,
  cell->thunk,-1,&output,true);
 bool completed=scheduled&&checkpoint_fork_drive(&driver,100000u);
 ready=completed&&output.len==1u&&atom_eq(output.items[0].atom,answer);
 if(!ready) {
  size_t slots=0u;for(PrimeNeedCallSourceSlot *slot=cell->slots;slot;slot=slot->next)slots++;
  fprintf(stderr,"FORK_RESUME scheduled=%u completed=%u answers=%zu "
   "capacity=%u failed=%u task=%u stack=%u slots=%zu expected=",scheduled,
   completed,output.len,driver.capacity_blocked,driver.failed,driver.task_ready,
   driver.top!=NULL,slots);
  atom_print(answer,stderr);fputs(" value=",stderr);
  if(output.len)atom_print(output.items[0].atom,stderr);else fputs("<none>",stderr);
  fputc('\n',stderr);
 }
 prime_eval_stack_abandon(&driver);
 prepared_pure_program_cache_free(&driver.prepared_pure_cache);
 prime_need_active_leave(&active);g_prime_eval_stack_driver=previous;
 outcome_set_free(&output);bindings_free(&environment);
 eval_context_restore(&saved);prime_native_run_leave(&run);
 return ready;
}

static void one(unsigned client,unsigned cut){
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);Space space;space_init(&space);
 Registry reg;registry_init(&reg);g_registry=&reg;registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(lib);
 const char *rows[]={"(=w (coin) 2 3)","(=w (coin) 2 3)","(=w (coin) 5 7)","(=w (weigh-by-self $x) $x $x)","(= (paired $x) (Pair $x $x))","(D 3)","(D 3)","(= (accepted $x) Mystery)","(=w (priced) Cheap 1)","(=w (priced) Costly 3)","(= (offers) (match &self (D $x) (Offer $x) (where $x)))",
 "(= (stuck A C) value)","(=w (field) 7 5)","(=w (field) 7 5)",
 "(=w (inspect (Pair $x $x) (Box $n)) Header 3)",
 "(=w (inspect (Pair $x $x) (Box 7)) Detail 11)",
 "(= (rank-source) (Box A A (rank-tail)))", "(= (rank-source) (Box B (rank-head) 7))",
 "(= (rank-source) (Box C A 7))", "(= (rank-head) (superpose (A A)))", "(= (rank-tail) (superpose (7 8)))",
 "(= (rank-view (Box $tag A $x)) Early)", "(= (rank-view (Box $tag $x 7)) $tag)",
 "(= (rank-residual-source) (Box A (rank-residual-tail)))", "(= (rank-residual-source) (Box (rank-residual-head) B))",
 "(= (rank-residual-tail) (superpose (8 8)))", "(= (rank-residual-head) 8)",
 "(= (rank-residual-view (Box 7 $x)) Rule0)", "(= (rank-residual-view (Box $x 7)) Rule1)"};
 for(size_t i=0;i<sizeof(rows)/sizeof(*rows);i++)space_add(&space,parse(&syntax,rows[i]));
 const char *programs[]={"(Pair (weight:where 2 3) (weight:where 5 7))","(paired (weigh-by-self (coin)))","(match &self (D $x) $x (where 2))","(weight:where 7 Mystery (admit accepted))","(weight:read (algebra 0 1 + *) (paired (weigh-by-self (coin))))","(weight:read (algebra 1000000 0 min +) (priced))","(weight:read (algebra 1000000 0 min +) (offers))","(superpose (A B C))","(superpose (A A A))","(superpose (A A A))","(superpose (3 1))","(superpose ())",
 "(inspect (Pair (stuck $v B) (stuck $v B)) (Box (field)))",
 "(rank-view (rank-source))", "(rank-residual-view (rank-residual-source))"};
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,.work_budget=UINT64_MAX,.costs=true};
 if(client>=5u&&client<=6u){policy.by_weight=true;policy.descending=false;policy.priority=2;policy.age_share=2;policy.weight_law=prime_need_symbols()->law_nonnegative_sum;}
 if(client>=7u&&client<=11u){policy.selection=client<=9u?1:2;policy.all=false;policy.goal_pattern=parse(&syntax,"$bundle");policy.goal_expression=client>=10u?policy.goal_pattern:parse(&syntax,client==8u?"False":"True");}
 if(client>=10u&&client<=11u)CHECK(policy.goal_pattern==policy.goal_expression);
 Bindings inherited;bindings_init(&inherited);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 assert(prime_need_receipt_begin(&syntax,bindings_receipt_mut(&inherited)));
 CettaPrimeNeedAnswerObserver prior_observer=g_prime_need_answer_observer;
 g_prime_need_answer_observer=request_causal_receipt;
 prime_need_answer_observer_changed();
#endif
 assert(prime_native_control_init(source,&space,&syntax,parse(&syntax,programs[client]),-1,true,(CettaObservationDemand){.completion=policy.selection?CETTA_OBSERVATION_FINITE_PREFIX:CETTA_OBSERVATION_COMPLETE_BAG,.prefix_limit=client==10u?3u:policy.selection?2u:0u},&policy,&inherited,NULL,NULL));
 /* Exercise the serial ordered-driver profile used by ordinary sources. */
 if(client>=13u)source->driver.ordinary_ordered=true;
 bindings_free(&inherited);
 Result a={0},b={0},prefix={0};arena_init(&a.storage);arena_init(&b.storage);arena_init(&prefix.storage);
 prime_native_control_run(source,cut,policy.selection?checkpoint_first_seen:seen,&prefix);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 for(const PrimeEvalStackFrame *f=source->frames;f;f=f->control_next) {
  if(f->before_receipt.top)causal_frame_views++;
 }
#endif
 uint64_t source_handle=0u,image_handle=0u;
 assert(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&source_handle));
 source->handle_id=source_handle;
 Atom *source_atom=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",source_handle);assert(source_atom);
 if(client==12u && !inspection_refusals_checked) {
  PrimeNeedEquationContinuation *eq=NULL;
  for(PrimeEvalStackFrame *f=source->frames;f;f=f->control_next)
   if(f->equation_search && f->equation_search->inspect_incrementally &&
      f->equation_search->state==PRIME_NEED_EQUATION_SEARCH_WAIT_FORCE &&
      f->equation_search->inspection.length) {eq=f->equation_search;break;}
  if(eq) {
   PrimeNeedPatternRequest saved=eq->inspection;
   CettaExprIndex coordinate=saved.path[0],argument=eq->force_argument;
   bool all_checked=true;
   for(unsigned damage=0u;damage<5u;damage++) {
    if(damage==0u)eq->inspection.demand=PRIME_NEED_PATTERN_READY;
    if(damage==1u)eq->inspection.length=0u;
    if(damage==2u)eq->force_argument=eq->original_call->expr.len;
    if(damage==3u)eq->inspection.path[0]=coordinate==1u?2u:1u;
    if(damage==4u)eq->inspection.path[0]=eq->original_call->expr.len;
    CettaContinuationStatus refused_status;
    CettaPrimeCheckpointCopy *refused=cetta_prime_checkpoint_copy_new(lib,source_atom,&refused_status);
    CHECK(refused && refused_status==CETTA_CONTINUATION_READY);
    if(!refused || refused_status!=CETTA_CONTINUATION_READY) {
     cetta_prime_checkpoint_copy_free(refused);all_checked=false;
     eq->inspection=saved;eq->inspection.path[0]=coordinate;eq->force_argument=argument;
     break;
    }
    size_t owners_n=0u;
    AtomDeepCopyArenaPair *owners=checkpoint_arena_pairs(refused,&syntax,&space,true,&owners_n);
    CHECK(owners);assert(owners);
    AtomDeepCopySession *session=atom_deep_copy_session_new(owners[0].destination);assert(session);
    CHECK(atom_deep_copy_session_set_arena_map(session,owners,owners_n));
    free(owners);
    CHECK(cetta_prime_checkpoint_copy_prepare(refused,session)!=CETTA_CONTINUATION_READY);
    const char *why=cetta_prime_checkpoint_copy_refusal(refused);
    CHECK(why && !strcmp(why,"ObservationRequestPath"));
    atom_deep_copy_session_free(session);cetta_prime_checkpoint_copy_free(refused);
    CHECK(!source->leased);
    eq->inspection=saved;eq->inspection.path[0]=coordinate;eq->force_argument=argument;
   }
   inspection_refusals_checked=all_checked;
  }
 }
 PrimeNativeControl original=*source;
 CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,source_atom,&status);
 CHECK(copy&&status==CETTA_CONTINUATION_READY);
 if(!copy || status!=CETTA_CONTINUATION_READY) {
  const char *blocked=NULL;
  (void)parent_copy_quiescent_in(source,false,false,&blocked);
  fprintf(stderr,"COPY_UNAVAILABLE %u/%u status=%u boundary=%s\n",
   client,cut,(unsigned)status,blocked?blocked:"other");
  cetta_prime_checkpoint_copy_free(copy);
  goto copy_unavailable;
 }
 CHECK(source->leased);
 size_t pairs_n=0u;
 AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&pairs_n);
 CHECK(pairs);assert(pairs);
 PrimeNativeControl *image=copy->image;
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
 bool mapping=atom_deep_copy_session_set_arena_map(atoms,pairs,pairs_n);
 free(pairs);
 bool copied=mapping&&cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_READY;
 const char *reason=cetta_prime_checkpoint_copy_refusal(copy);
 CHECK(copied);CHECK(!memcmp(&original,source,sizeof(original)));
 if(!copied) {
  fprintf(stderr,"COPY_UNAVAILABLE %u/%u reason=%s\n",client,cut,reason?reason:"unspecified");
  if(reason&&strcmp(reason,"EpisodeExecutionState")==0&&source->owned_top_episode&&
      source->owned_top_episode->execution) {
   const EvalExecutionContext *execution=source->owned_top_episode->execution;
   fprintf(stderr,"EPISODE_EXECUTION %u/%u table=%u answers=%u variants=%u survivor=%u prepared=%u\n",
    client,cut,execution->table_ready,execution->answers_ready,
    execution->variants_ready,execution->survivor_ready,execution->prepared!=NULL);
  }
  atom_deep_copy_session_free(atoms);cetta_prime_checkpoint_copy_free(copy);
  goto copy_unavailable;
 }
 if(copied){
  CopyRelation relation;CHECK(copy_relation(source,image,&relation));
  for(const PrimeEvalStackFrame *f=source->frames;f;f=f->control_next) {
   const PrimeNeedEquationContinuation *eq=f->equation_search;if(!eq)continue;
   inspection_copies+=eq->inspect_incrementally && eq->inspection.length>0u;
   readiness_copies+=eq->observed && eq->observed->length>0u;
   for(size_t i=0u;i<eq->work.len;i++)
    work_readiness_copies+=eq->work_meta[i].observed && eq->work_meta[i].observed->length>0u;
  }
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
   eval_release_temporary_spaces();
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
  int status=0;assert(waitpid(child,&status,0)==child);
  if(!received||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
   fprintf(stderr,"IMAGE_CHILD %u/%u received=%u exited=%u exit=%d signaled=%u signal=%d\n",
    client,cut,received,WIFEXITED(status),WIFEXITED(status)?WEXITSTATUS(status):-1,
    WIFSIGNALED(status),WIFSIGNALED(status)?WTERMSIG(status):-1);
  CHECK(received&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
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
 CHECK(prefix.count+a.count==expected_answers[client]);
 CHECK(prefix.count+b.count==expected_answers[client]);
 if(client==3u)CHECK(pending&&!closed);
 free(events);
 printf("{\"client\":%u,\"cut\":%u,\"copied\":%s,\"reason\":\"%s\",\"prefix\":%zu,\"source_answers\":%zu,\"image_answers\":%zu,\"source_steps\":%"PRIu64",\"image_steps\":%"PRIu64",\"failures\":%zu}\n",client,cut,copied?"true":"false",reason?reason:"",prefix.count,a.count,b.count,steps,image_steps,failures);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 g_prime_need_answer_observer=prior_observer;
 prime_need_answer_observer_changed();
#endif
 assert(cetta_native_handle_close(lib,"prime-search",image_handle));arena_free(&a.storage);arena_free(&b.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 return;

copy_unavailable:
 /* A failed copy remains a failed checkpoint control. Continue only the
  * untouched source so later clients and cuts still receive coverage. */
 CHECK(!source->leased);
 checkpoint_scope_run(source,3000u,&a,&syntax,client,cut);
 CHECK(prefix.count+a.count==expected_answers[client]);
 if(client==3u)CHECK(prime_native_control_waiting_verdict(source)&&!prime_native_control_closed(source));
 CHECK(cetta_native_handle_close(lib,"prime-search",source_handle));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 g_prime_need_answer_observer=prior_observer;prime_need_answer_observer_changed();
#endif
 arena_free(&a.storage);arena_free(&b.storage);arena_free(&prefix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
}

/* A copied parent must carry the installed continuation cut, not replay the
 * visible prefix and not retain the source graph.  Exercise both retirement
 * orders after source and image choose different requesting worlds. */
static void installed_context_fork_checkpoint_cases(void) {
 for(unsigned mode=0u;mode<2u;mode++) {
  unsigned client=500u+mode,cut=0u;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space,left,right,third,home_left,home_right,home_third,log;
  space_init(&space);space_init(&left);space_init(&right);space_init(&third);
  space_init(&home_left);space_init(&home_right);space_init(&home_third);space_init(&log);
  Registry registry;registry_init(&registry);g_registry=&registry;
  registry_bind(&registry,"&self",atom_space(&syntax,&space));
  registry_bind(&registry,"&checkpoint-log",atom_space(&syntax,&log));
  space_add(&left,parse(&syntax,"(context-marker A)"));
  space_add(&right,parse(&syntax,"(context-marker B)"));
  space_add(&third,parse(&syntax,"(context-marker C)"));
  space_add(&home_left,parse(&syntax,"(home-marker X)"));
  space_add(&home_right,parse(&syntax,"(home-marker Y)"));
  space_add(&home_third,parse(&syntax,"(home-marker Z)"));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,
   cetta_profile_prime_default());eval_set_library_context(lib);
  PrimeNativeControl *source=malloc(sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
   .workers=1u,.work_budget=UINT64_MAX,.costs=true};
  CHECK(prime_native_control_init(source,&space,&syntax,
   parse(&syntax,"(superpose ())"),-1,true,
   (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
   &policy,NULL,NULL,NULL));
  Result seed={0};arena_init(&seed.storage);
  prime_native_control_run(source,1000u,seen,&seed);
  CHECK(prime_native_control_closed(source)&&!seed.count&&source->owned_arena);

  Bindings world;bindings_init(&world);
  CHECK(prime_need_snapshot_begin(bindings_need_mut(&world))&&
   prime_need_branch_state_begin(&source->storage,bindings_branch_state_mut(&world)));
  EvalContextState saved=eval_context_capture();
  assert(g_context_here_depth<64u&&g_context_home_depth<64u);
  g_context_here_stack[g_context_here_depth++]=&left;
  g_context_home_stack[g_context_home_depth++]=&home_left;
  PrimeNativeRunGuard run;prime_native_run_enter(source,&run);
  CheckpointForkDemand demand;
  CHECK(checkpoint_fork_demand_open(&demand,&space,&syntax,&world,
   parse(&syntax,"(let $_ (add-atom &checkpoint-log (Prefix)) "
    "(let $h (match &here (context-marker $v) $v) "
    "(match &home (home-marker $w) (Pair $h $w))))")));
  CHECK(prime_need_call_force(&space,&syntax,demand.reference,demand.thunk,
   -1,&demand.output,true));
  CHECK(checkpoint_fork_drive(&demand.driver,100000u)&&demand.output.len==1u&&
   atom_eq(demand.output.items[0].atom,parse(&syntax,"(Pair A X)"))&&log.len==1u);
  PrimeNativeTopEpisode *episode=source->owned_top_episode;
  PrimeNeedCallSources *hub=episode?episode->call_sources:NULL;
  PrimeNeedCallSourceCell *source_cell=hub?hub->cells:NULL;
  PrimeNeedCallForkPoint *source_fork=source_cell?source_cell->forks:NULL;
  PrimeNeedCallForkPoint *source_parent=source_fork?source_fork->parent:NULL;
  CHECK(source->owns_execution_episode&&episode&&episode->execution&&source_cell&&
   source_cell->slots&&source_fork&&source_fork->installed&&
   (source_fork->roles&EVAL_CONTEXT_ROLE_HOME)&&source_fork->origin_held&&
   source_fork->origin==source_cell->slots->source&&source_fork->snapshot&&
   source_fork->snapshot->fork_template&&source_fork->depth==2u&&source_parent&&
   source_parent->installed&&(source_parent->roles&EVAL_CONTEXT_ROLE_HERE)&&
   source_parent->depth==1u&&!source_parent->parent&&
   source_fork->origin==source_parent->origin&&
   source_cell->slots->source->fork_parent==source_fork);
  if(source->base_environment_initialized)bindings_free(&source->base_environment);
  bindings_init(&source->base_environment);
  CHECK(bindings_clone(&source->base_environment,&demand.environment));
  source->base_environment_initialized=true;
  checkpoint_fork_demand_close(&demand);
  prime_native_run_leave(&run);eval_context_restore(&saved);bindings_free(&world);

  uint64_t source_id=0u,image_id=0u;
  CHECK(cetta_native_handle_alloc(lib,"prime-search",source,
   prime_native_control_destroy,&source_id));source->handle_id=source_id;
  Atom *source_atom=cetta_native_handle_owned_atom(lib,&syntax,
   "prime-search",source_id);assert(source_atom);
  if(!mode) {
   PrimeNativeControl stable_control=*source;
   PrimeNeedCallForkPoint stable_fork=*source_fork;
   PrimeNeedCallForkPoint stable_parent=*source_parent;
   PrimeNeedForceProduction stable_origin=*source_fork->origin;
   PrimeNeedForceProduction stable_snapshot=*source_fork->snapshot;
   bool reached_end=false;
   for(size_t nth=1u;nth<=128u&&!reached_end;nth++) {
    checkpoint_calloc_countdown=nth;
    CettaContinuationStatus injected_status;
    CettaPrimeCheckpointCopy *attempt=
     cetta_prime_checkpoint_copy_new(lib,source_atom,&injected_status);
    if(!attempt) {
     CHECK(!checkpoint_calloc_countdown&&
      injected_status==CETTA_CONTINUATION_CAPACITY&&!source->leased);
    } else {
     size_t remaining=checkpoint_calloc_countdown,pair_count=0u;
     checkpoint_calloc_countdown=0u;
     AtomDeepCopyArenaPair *injected_pairs=checkpoint_arena_pairs(
      attempt,&syntax,&space,true,&pair_count);
     CHECK(injected_pairs);assert(injected_pairs);
     AtomDeepCopySession *injected_atoms=
      atom_deep_copy_session_new(injected_pairs[0].destination);assert(injected_atoms);
     CHECK(atom_deep_copy_session_set_arena_map(
      injected_atoms,injected_pairs,pair_count));free(injected_pairs);
     checkpoint_calloc_countdown=remaining;
     injected_status=cetta_prime_checkpoint_copy_prepare(attempt,injected_atoms);
     reached_end=injected_status==CETTA_CONTINUATION_READY&&
      checkpoint_calloc_countdown>0u;
     if(injected_status!=CETTA_CONTINUATION_READY&&
        (checkpoint_calloc_countdown||injected_status!=CETTA_CONTINUATION_CAPACITY))
      fprintf(stderr,"FORK_ALLOCATION nth=%zu status=%u countdown=%zu reason=%s\n",
       nth,(unsigned)injected_status,checkpoint_calloc_countdown,
       cetta_prime_checkpoint_copy_refusal(attempt)
        ?cetta_prime_checkpoint_copy_refusal(attempt):"unspecified");
     CHECK(injected_status==CETTA_CONTINUATION_READY||
      (!checkpoint_calloc_countdown&&injected_status==CETTA_CONTINUATION_CAPACITY));
     atom_deep_copy_session_free(injected_atoms);
     cetta_prime_checkpoint_copy_free(attempt);
    }
    checkpoint_calloc_countdown=0u;
    CHECK(!source->leased&&!memcmp(&stable_control,source,sizeof(stable_control))&&
     source_cell->forks==source_fork&&
     !memcmp(&stable_fork,source_fork,sizeof(stable_fork))&&
     !memcmp(&stable_parent,source_parent,sizeof(stable_parent))&&
     !memcmp(&stable_origin,source_fork->origin,sizeof(stable_origin))&&
     !memcmp(&stable_snapshot,source_fork->snapshot,sizeof(stable_snapshot))&&
     log.len==1u);
   }
   CHECK(reached_end);
  }
  CettaContinuationStatus status;
  CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,source_atom,&status);
  CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
  size_t pair_count=0u;
  AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&pair_count);
  CHECK(pairs);assert(pairs);
  AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
  CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,pair_count));free(pairs);
  bool copied=cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_READY;
  if(!copied)fprintf(stderr,"FORK_COPY_REFUSAL mode=%u reason=%s\n",mode,
   cetta_prime_checkpoint_copy_refusal(copy)?cetta_prime_checkpoint_copy_refusal(copy):"unspecified");
  CHECK(copied);assert(copied);
  PrimeNativeControl *image=copy->image;
  PrimeNativeTopEpisode *image_episode=image->owned_top_episode;
  PrimeNeedCallSourceCell *image_cell=image_episode&&image_episode->call_sources
   ? image_episode->call_sources->cells:NULL;
  PrimeNeedCallForkPoint *image_fork=image_cell?image_cell->forks:NULL;
  PrimeNeedCallForkPoint *image_parent=image_fork?image_fork->parent:NULL;
  CHECK(image_cell&&image_cell!=source_cell&&image_fork&&image_fork!=source_fork&&
   image_fork->cell==image_cell&&image_fork->origin!=source_fork->origin&&
   image_fork->snapshot!=source_fork->snapshot&&image_fork->snapshot->fork_template&&
   image_fork->origin==image_cell->slots->source&&image_fork->origin_held&&
   image_fork->roles==source_fork->roles&&image_fork->depth==source_fork->depth&&
   image_parent&&image_parent!=source_parent&&image_parent->parent==NULL&&
   image_parent->roles==source_parent->roles&&image_parent->depth==source_parent->depth&&
   image_fork->origin==image_parent->origin&&
   image_cell->slots->source->fork_parent==image_fork);
  atom_deep_copy_session_free(atoms);
  Atom *image_atom=NULL;
  CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&image_atom)==
   CETTA_CONTINUATION_READY&&image_atom&&
   cetta_native_handle_arg(image_atom,"prime-search",&image_id));
  cetta_prime_checkpoint_copy_free(copy);

  if(!mode) {
   CHECK(checkpoint_fork_force_existing(source,source_cell,&left,&home_right,
    parse(&syntax,"(Pair A Y)")));CHECK(log.len==1u);
   CHECK(cetta_native_handle_close(lib,"prime-search",source_id));source=NULL;
   CHECK(checkpoint_fork_force_existing(image,image_cell,&third,&home_third,
    parse(&syntax,"(Pair C Z)")));CHECK(log.len==1u);
   CHECK(cetta_native_handle_close(lib,"prime-search",image_id));image=NULL;
  } else {
   CHECK(checkpoint_fork_force_existing(image,image_cell,&left,&home_right,
    parse(&syntax,"(Pair A Y)")));CHECK(log.len==1u);
   CHECK(cetta_native_handle_close(lib,"prime-search",image_id));image=NULL;
   CHECK(checkpoint_fork_force_existing(source,source_cell,&third,&home_third,
    parse(&syntax,"(Pair C Z)")));CHECK(log.len==1u);
   CHECK(cetta_native_handle_close(lib,"prime-search",source_id));source=NULL;
  }
  arena_free(&seed.storage);
  cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
  registry_free(&registry);g_registry=NULL;
  space_free(&log);space_free(&home_third);space_free(&home_right);space_free(&home_left);
  space_free(&third);space_free(&right);space_free(&left);
  space_free(&space);arena_free(&syntax);
  printf("INSTALLED_CONTEXT_FORK mode=%u failures=%zu\n",mode,failures);
 }
}
/* The array is optional, but its rows are occurrence indexed. These direct
 * controls also cover absent/invalid extents and an unowned advice edge. */
static void return_advice_copy_cases(void) {
 for(unsigned mode=0u;mode<7u;mode++) {
  unsigned client=400u+mode,cut=0u;
  PrimeNativeControl source={0},image={0};
  arena_init(&source.storage);arena_init(&image.storage);
  source.driver.arena=&source.storage;image.driver.arena=&image.storage;
  AtomDeepCopySession *atoms=atom_deep_copy_session_new(&image.storage);assert(atoms);
  AtomDeepCopyArenaPair pair={&source.storage,&image.storage};
  assert(atom_deep_copy_session_set_arena_map(atoms,&pair,1u));
  PrimeParentCopy copy={.source=&source,.image=&image,.atoms=atoms};
  PrimeNativeAdvice holder={0},mapped={0},foreign={0};
  holder.key=atom_int(&source.storage,13);mapped.key=atom_int(&image.storage,13);
  CHECK(parent_copy_register(&copy,&parent_copy_advice_kind,&holder,&mapped,sizeof(holder)));
  PrimeEvalStackFrame from={.kind=PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM,
   .arena=&source.storage,.child_initialized=true},to={0};
  outcome_set_init(&from.child);
  Atom *same=atom_int(&source.storage,5);
  Bindings empty;bindings_init(&empty);
  outcome_set_add(&from.child,same,&empty);outcome_set_add(&from.child,same,&empty);
  bindings_free(&empty);
  from.control_return_advice=calloc(4u,sizeof(*from.control_return_advice));assert(from.control_return_advice);
  PrimeNativeReturnAdvice *owned=from.control_return_advice;
  from.control_return_advice_capacity=4u;from.control_return_advice_length=2u;
  owned[0]=(PrimeNativeReturnAdvice){atom_int(&source.storage,1),atom_int(&source.storage,7),&holder};
  owned[1]=(PrimeNativeReturnAdvice){atom_int(&source.storage,3),atom_int(&source.storage,11),&holder};
  if(mode==1u)from.control_return_advice_length=5u;
  if(mode==2u)from.control_return_advice_length=3u;
  if(mode==3u)from.control_return_advice=NULL;
  if(mode==4u)from.control_return_advice_capacity=0u;
  if(mode==5u)owned[1].advice=&foreign;
  if(mode==6u){from.control_return_advice=NULL;from.control_return_advice_length=0u;from.control_return_advice_capacity=0u;}
  PrimeEvalStackFrame before=from;
  PrimeNativeReturnAdvice saved[2]={owned[0],owned[1]};
  bool copied=parent_copy_frame(&copy,&from,&to);
  CHECK(!memcmp(&from,&before,sizeof(from))&&!memcmp(owned,saved,sizeof(saved)));
  CHECK(copied==(mode==0u||mode==6u));
  if(mode==0u) {
   CHECK(to.control_return_advice!=owned&&to.control_return_advice_length==2u&&to.control_return_advice_capacity==4u);
   CHECK(to.control_return_advice[0].advice==&mapped&&to.control_return_advice[1].advice==&mapped);
   CHECK(atom_eq(to.control_return_advice[0].key,saved[0].key)&&atom_eq(to.control_return_advice[1].key,saved[1].key));
   CHECK(atom_eq(to.control_return_advice[0].bound,saved[0].bound)&&atom_eq(to.control_return_advice[1].bound,saved[1].bound));
   mapped.key=atom_int(&image.storage,29);
   CHECK(to.control_return_advice[0].advice->key==mapped.key&&to.control_return_advice[1].advice->key==mapped.key);
   CHECK(holder.key->ground.ival==13);
  }
  if(mode==1u||mode==2u||mode==4u)CHECK(copy.refusal&&!strcmp(copy.refusal,"ReturnAdviceExtent"));
  if(mode==3u)CHECK(copy.refusal&&!strcmp(copy.refusal,"ArrayExtent"));
  if(mode==5u)CHECK(copy.refusal&&!strcmp(copy.refusal,"UnregisteredEdge"));
  if(mode==6u)CHECK(!to.control_return_advice&&!to.control_return_advice_length&&!to.control_return_advice_capacity);
  outcome_set_free(&from.child);outcome_set_free(&to.child);
  free(owned);
  atom_deep_copy_session_free(atoms);arena_free(&source.storage);
  if(mode==0u)CHECK(to.control_return_advice[0].key->ground.ival==1&&to.control_return_advice[1].bound->ground.ival==11&&mapped.key->ground.ival==29);
  free(to.control_return_advice);
  arena_free(&image.storage);
 }
}

static bool checkpoint_borrowed_permit(void *context,Space *space,PettaMachineOperation operation,
 const PettaMachineOperationView *view) {
 (void)context;(void)space;(void)operation;(void)view;return true;
}
static void checkpoint_borrowed_read(void *context,SpaceReadToken token) {(void)context;(void)token;}
static bool checkpoint_borrowed_context(void *context,Space *program,SymbolId name,Space *space) {
 (void)context;(void)program;(void)name;(void)space;return true;
}
static void checkpoint_borrowed_write(void *context,Space *space,uint64_t revision,uint64_t prefix) {
 (void)context;(void)space;(void)revision;(void)prefix;
}
static bool checkpoint_borrowed_role(void *context,Atom *value,Atom *type,
 const Bindings *bindings,bool *role) {
 (void)context;(void)value;(void)type;(void)bindings;(void)role;return false;
}
static bool checkpoint_has_borrowed_driver(const PrimeEvalStackDriver *driver) {
 return driver->authority_parent || driver->call_permit_parent || driver->call_permit ||
  driver->call_permit_context || driver->observe_space_read || driver->space_read_context ||
  driver->observe_context_read || driver->context_read_context ||
  driver->own_data_read || driver->own_data_published || driver->own_data_context ||
  driver->normalization_value_role || driver->normalization_role_context;
}

/* A quiescent image must not acquire pointers to another invocation's stack.
 * Refusing an untransportable source is allowed, but must leave it intact. */
static void borrowed_authority_checkpoint_cases(void) {
 for(unsigned mask=0u;mask<=32u;mask++) {
  unsigned client=500u+mask,cut=0u;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
  registry_bind(&reg,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
  eval_set_library_context(lib);term_universe_set_persistent_arena(&lib->term_universe,&syntax);
  PrimeNativeControl *source=calloc(1u,sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
  CHECK(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(superpose (A B))"),
   -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
   &policy,NULL,NULL,NULL));
  PrimeEvalStackDriver ancestry={0},caller={0};SpaceReadToken read=space_read_token(&space);
  unsigned context=0u;
  source->driver.authority_parent=(mask<=3u && (mask&1u))?&ancestry:NULL;
  source->driver.call_permit_parent=(mask<=3u && (mask&2u))?&caller:NULL;
  if(mask==4u)source->driver.call_permit=checkpoint_borrowed_permit;
  if(mask==5u)source->driver.call_permit_context=&context;
  if(mask==6u)source->driver.observe_space_read=checkpoint_borrowed_read;
  if(mask==7u)source->driver.space_read_context=&context;
  if(mask==8u)source->driver.own_data_read=&read;
  if(mask==9u)source->driver.own_data_published=checkpoint_borrowed_write;
  if(mask==10u)source->driver.own_data_context=&context;
  if(mask==11u)source->driver.normalization_value_role=checkpoint_borrowed_role;
  if(mask==12u)source->driver.normalization_role_context=&context;
  if(mask==29u)source->driver.observe_context_read=checkpoint_borrowed_context;
  if(mask==30u)source->driver.context_read_context=&context;
  source->driver.pure_authority=true;source->pure_authority=true;
  uint64_t id=0u;CHECK(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));
  source->handle_id=id;
  PrimeNativeControl initial=*source;
  PrimeEvalStackDriver original=source->driver;
  Atom *holder=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(holder);
  CettaNativeHandleUse use={0};
  if(mask==26u || mask==28u)CHECK(cetta_native_handle_acquire_owned(lib,"prime-search",holder,
   CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use));
  PrimeNativeRunGuard run={0};PrimeEvalStackDriver inner={.authority_parent=&source->driver},cycle={0};
  PrimeEvalStackDriver middle={0};PrimeEvalTopGuard first_top={0},second_top={0};
  PrimeEvalStackDriver deep[18]={0},branch={0};
  for(size_t i=0u;i+1u<18u;i++)deep[i].authority_parent=&deep[i+1u];
  deep[0].call_permit_parent=&branch;
  PrimeEvalStackDriver *previous_driver=g_prime_eval_stack_driver,*previous_top=g_prime_eval_top_authority;
  bool active=mask==13u || mask==14u || mask==18u || mask==19u || mask==25u || mask==27u;
  if(active) {
   g_prime_eval_stack_driver=mask==13u?&caller:NULL;g_prime_eval_top_authority=NULL;
   prime_native_run_enter(source,&run);
   if(mask==18u)inner.call_permit_parent=&caller;
   if(mask==25u)inner.authority_parent=NULL;
   if(mask==27u)source->interaction_selection=(CettaSpaceAgendaSelection *)&context;
   if(mask==19u){g_prime_eval_stack_driver=NULL;g_prime_eval_top_authority=&inner;}
   else g_prime_eval_stack_driver=&inner;
  } else if(mask==17u) {
   cycle.call_permit_parent=&cycle;g_prime_eval_stack_driver=&cycle;g_prime_eval_top_authority=NULL;
  } else if(mask==22u) {
   g_prime_eval_stack_driver=&deep[0];g_prime_eval_top_authority=NULL;checkpoint_calloc_refuse=true;
  } else if(mask==24u) {
   g_prime_eval_stack_driver=&source->driver;g_prime_eval_top_authority=NULL;
   prime_eval_top_enter(&first_top);g_prime_eval_stack_driver=&middle;
   prime_eval_top_enter(&second_top);
  }
  CettaContinuationStatus status;
  CettaPrimeCheckpointCopy *copy=(mask==26u || mask==28u)
   ?cetta_prime_checkpoint_copy_new_reserved(lib,holder,&status)
   :cetta_prime_checkpoint_copy_new(lib,holder,&status);
  if(mask==26u || mask==28u) {
   CHECK(copy && status==CETTA_CONTINUATION_READY);assert(copy);
   size_t count=0u;AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&count);
   CHECK(pairs);assert(pairs);
   AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
   CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));free(pairs);
   CHECK(cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_READY);
   atom_deep_copy_session_free(atoms);
   PrimeNativeControl *image=copy->image;
   if(mask==26u) {
    source->driver.call_permit_context=&context;
    CettaContinuationStatus replaced=cetta_prime_checkpoint_copy_replace(copy,&use);
    CHECK(replaced==CETTA_CONTINUATION_UNSUPPORTED && copy->prepared && copy->image==image);
    if(replaced!=CETTA_CONTINUATION_READY) {
     CHECK(source->leased && source->driver.call_permit_context==&context);
     source->driver.call_permit_context=NULL;
     replaced=cetta_prime_checkpoint_copy_replace(copy,&use);
     CHECK(replaced==CETTA_CONTINUATION_READY);
    }
    if(replaced==CETTA_CONTINUATION_READY) {
     source=NULL;CHECK(cetta_native_handle_use_resource(&use)==image && image->pure_authority);
    }
   } else {
    /* Isolate the failing mutation: if replace wrongly destroys an entered
     * owner, the child exits without dereferencing its stale run guard. */
    fflush(NULL);pid_t pid=fork();assert(pid>=0);
    if(!pid) {
     PrimeNativeRunGuard entered;PrimeEvalStackDriver hidden={0};
     g_prime_eval_stack_driver=NULL;g_prime_eval_top_authority=NULL;
     prime_native_run_enter(source,&entered);g_prime_eval_stack_driver=&hidden;
     CettaContinuationStatus deferred=cetta_prime_checkpoint_copy_replace(copy,&use);
     if(deferred!=CETTA_CONTINUATION_DEFERRED)_exit(1);
     g_prime_eval_stack_driver=&source->driver;prime_native_run_leave(&entered);
     if(!source->leased || !copy->prepared || cetta_native_handle_use_resource(&use)!=source)_exit(2);
     if(cetta_prime_checkpoint_copy_replace(copy,&use)!=CETTA_CONTINUATION_READY ||
        cetta_native_handle_use_resource(&use)!=image)_exit(3);
     _exit(0);
    }
    int child_status=0;CHECK(waitpid(pid,&child_status,0)==pid &&
     WIFEXITED(child_status) && WEXITSTATUS(child_status)==0);
    CHECK(source->leased && copy->reserved && copy->prepared && copy->image==image &&
     cetta_native_handle_use_resource(&use)==source);
   }
   cetta_prime_checkpoint_copy_free(copy);
   if(source)CHECK(!source->leased && !memcmp(&initial,source,sizeof(initial)));
   CHECK(cetta_native_handle_use_release(&use));CHECK(cetta_native_handle_close(lib,"prime-search",id));
   cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
   registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
   continue;
  } else if(active) {
   CHECK(!copy && status==CETTA_CONTINUATION_DEFERRED);
   cetta_prime_checkpoint_copy_free(copy);
   if(mask==27u)source->interaction_selection=NULL;
   g_prime_eval_stack_driver=&source->driver;prime_native_run_leave(&run);
   g_prime_eval_stack_driver=previous_driver;g_prime_eval_top_authority=previous_top;
  } else if(mask==17u) {
   CHECK(!copy && status==CETTA_CONTINUATION_INVALIDATED);
   cetta_prime_checkpoint_copy_free(copy);
   g_prime_eval_stack_driver=previous_driver;g_prime_eval_top_authority=previous_top;
  } else if(mask==22u) {
   CHECK(!copy && status==CETTA_CONTINUATION_CAPACITY && !checkpoint_calloc_refuse);
   checkpoint_calloc_refuse=false;cetta_prime_checkpoint_copy_free(copy);
   g_prime_eval_stack_driver=previous_driver;g_prime_eval_top_authority=previous_top;
  } else if(mask==24u) {
   CHECK(!copy && status==CETTA_CONTINUATION_DEFERRED);
   cetta_prime_checkpoint_copy_free(copy);
   prime_eval_top_leave(&second_top);prime_eval_top_leave(&first_top);
   g_prime_eval_stack_driver=previous_driver;g_prime_eval_top_authority=previous_top;
  } else if(copy && status==CETTA_CONTINUATION_READY) {
   size_t count=0u;AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&count);
   CHECK(pairs);assert(pairs);
   AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
   CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));free(pairs);
   if(mask==15u)source->driver.authority_parent=&ancestry;
   if(mask==16u)source->driver.call_permit_parent=&caller;
   if(mask==20u)source->driver.call_permit=checkpoint_borrowed_permit;
   if(mask==21u)source->driver.normalization_role_context=&context;
   if(mask==31u)source->driver.observe_context_read=checkpoint_borrowed_context;
   if(mask==32u)source->driver.context_read_context=&context;
   if(mask==23u) {
    g_prime_eval_stack_driver=&deep[0];g_prime_eval_top_authority=NULL;checkpoint_calloc_refuse=true;
   }
   CettaContinuationStatus prepared=cetta_prime_checkpoint_copy_prepare(copy,atoms);
   if(mask==23u) {
    CHECK(prepared==CETTA_CONTINUATION_CAPACITY && !source->leased && !checkpoint_calloc_refuse);
    checkpoint_calloc_refuse=false;g_prime_eval_stack_driver=previous_driver;g_prime_eval_top_authority=previous_top;
   } else if(mask>=15u) {
    CHECK(prepared==CETTA_CONTINUATION_UNSUPPORTED && !source->leased);
    CHECK(source->driver.authority_parent==(mask==15u?&ancestry:NULL) &&
     source->driver.call_permit_parent==(mask==16u?&caller:NULL) &&
     source->driver.call_permit==(mask==20u?checkpoint_borrowed_permit:NULL) &&
     source->driver.normalization_role_context==(mask==21u?&context:NULL) &&
     source->driver.observe_context_read==(mask==31u?checkpoint_borrowed_context:NULL) &&
     source->driver.context_read_context==(mask==32u?&context:NULL));
    source->driver.authority_parent=NULL;source->driver.call_permit_parent=NULL;
    source->driver.call_permit=NULL;source->driver.normalization_role_context=NULL;
   source->driver.observe_context_read=NULL;source->driver.context_read_context=NULL;
   } else if(prepared==CETTA_CONTINUATION_READY) {
    CHECK(!checkpoint_has_borrowed_driver(&copy->image->driver) &&
     copy->image->driver.pure_authority && copy->image->pure_authority);
   } else CHECK(mask!=0u && (prepared==CETTA_CONTINUATION_UNSUPPORTED ||
    prepared==CETTA_CONTINUATION_CAPACITY || prepared==CETTA_CONTINUATION_INVALIDATED));
   atom_deep_copy_session_free(atoms);cetta_prime_checkpoint_copy_free(copy);
  } else {
   CHECK(mask!=0u && (mask<15u || mask==29u || mask==30u) && !copy && (status==CETTA_CONTINUATION_UNSUPPORTED || status==CETTA_CONTINUATION_CAPACITY ||
    status==CETTA_CONTINUATION_INVALIDATED));
   if(mask==29u || mask==30u) CHECK(status==CETTA_CONTINUATION_UNSUPPORTED);
   cetta_prime_checkpoint_copy_free(copy);
  }
  CHECK(!source->leased && !memcmp(&initial,source,sizeof(initial)) &&
   !memcmp(&source->driver,&original,sizeof(original)) &&
   source->driver.pure_authority && source->pure_authority);
  CHECK(cetta_native_handle_close(lib,"prime-search",id));
  cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
  registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
 puts("Borrowed driver authority and services cannot escape in a copied checkpoint");
}

/* Disposal must not reclaim a selected control while its driver is hidden by
 * a nested evaluator. Once the enclosing scope leaves, the same handle is
 * still disposable. Run the destructive negative control in a child. */
static void hidden_active_disposal_cases(void) {
 for(unsigned mode=0u;mode<3u;mode++) {
  unsigned client=540u+mode,cut=0u;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
  eval_set_library_context(lib);
  PrimeNativeControl *source=calloc(1u,sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
  CHECK(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(superpose (A B))"),
   -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
   &policy,NULL,NULL,NULL));
  source->resume_choice=calloc(1u,sizeof(*source->resume_choice));assert(source->resume_choice);
  if(mode==2u) {
   source->source_services=prime_native_source_services_new();assert(source->source_services);
  }
  uint64_t id=0u;CHECK(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));
  source->handle_id=id;
  Atom *holder=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(holder);
  Atom *dispose=atom_expr2(&syntax,atom_symbol(&syntax,"interaction:dispose"),holder);
  Bindings env;bindings_init(&env);OutcomeSet out;outcome_set_init(&out);
  fflush(NULL);pid_t pid=fork();assert(pid>=0);
  if(!pid) {
   PrimeEvalStackDriver hidden={0};PrimeNativeRunGuard run={0};PrimeEvalTopGuard top={0};
   if(!mode)prime_native_run_enter(source,&run);
   else if(mode==1u) {g_prime_eval_stack_driver=&source->driver;prime_eval_top_enter(&top);}
   else atomic_store_explicit(&source->source_services->active,true,memory_order_release);
   g_prime_eval_stack_driver=&hidden;g_prime_eval_top_authority=NULL;
   if(prime_native_interaction_dispatch(&space,&syntax,dispose,-1,&env,&out) ||
      out.len || cetta_native_handle_get_owned(lib,"prime-search",holder)!=source)_exit(1);
   if(!mode)prime_native_run_leave(&run);
   else if(mode==1u) {prime_eval_top_leave(&top);g_prime_eval_stack_driver=NULL;}
   else {atomic_store_explicit(&source->source_services->active,false,memory_order_release);
    g_prime_eval_stack_driver=NULL;}
   if(!prime_native_interaction_dispatch(&space,&syntax,dispose,-1,&env,&out) ||
      out.len!=1u || cetta_native_handle_get_owned(lib,"prime-search",holder))_exit(2);
   _exit(0);
  }
  int child_status=0;CHECK(waitpid(pid,&child_status,0)==pid &&
   WIFEXITED(child_status) && WEXITSTATUS(child_status)==0);
  CHECK(cetta_native_handle_get_owned(lib,"prime-search",holder)==source);
  CHECK(cetta_native_handle_close(lib,"prime-search",id));
  outcome_set_free(&out);bindings_free(&env);cetta_library_context_free(lib);free(lib);
  eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
 puts("Hidden active controls survive disposal until their scopes leave");
}

/* A prepared replacement must retain its original owner while one of that
 * owner's source services is entered, and allow retry after the service exits. */
static void active_service_replacement_case(void) {
 unsigned client=550u,cut=0u;
 Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
 Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
 cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
 eval_set_library_context(lib);term_universe_set_persistent_arena(&lib->term_universe,&syntax);
 PrimeNativeControl *source=calloc(1u,sizeof(*source));assert(source);
 CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
 CHECK(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(superpose (A B))"),
  -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
  &policy,NULL,NULL,NULL));
 source->source_services=prime_native_source_services_new();assert(source->source_services);
 uint64_t id=0u;CHECK(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));
 source->handle_id=id;
 Atom *holder=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(holder);
 CettaNativeHandleUse use={0};CHECK(cetta_native_handle_acquire_owned(lib,"prime-search",holder,
  CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use));
 CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new_reserved(lib,holder,&status);
 CHECK(copy && status==CETTA_CONTINUATION_READY);assert(copy);
 size_t count=0u;AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&count);
 CHECK(pairs);assert(pairs);
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
 CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));free(pairs);
 CHECK(cetta_prime_checkpoint_copy_prepare(copy,atoms)==CETTA_CONTINUATION_READY);
 atom_deep_copy_session_free(atoms);
 PrimeNativeControl *image=copy->image;
 fflush(NULL);pid_t pid=fork();assert(pid>=0);
 if(!pid) {
  atomic_store_explicit(&source->source_services->active,true,memory_order_release);
  if(cetta_prime_checkpoint_copy_replace(copy,&use)!=CETTA_CONTINUATION_DEFERRED)_exit(1);
  if(!source->leased || !copy->reserved || !copy->prepared || copy->image!=image ||
     cetta_native_handle_use_resource(&use)!=source)_exit(2);
  atomic_store_explicit(&source->source_services->active,false,memory_order_release);
  if(cetta_prime_checkpoint_copy_replace(copy,&use)!=CETTA_CONTINUATION_READY ||
     cetta_native_handle_use_resource(&use)!=image)_exit(3);
  _exit(0);
 }
 int child_status=0;CHECK(waitpid(pid,&child_status,0)==pid &&
  WIFEXITED(child_status) && WEXITSTATUS(child_status)==0);
 CHECK(copy->reserved && copy->prepared && copy->image==image && source->leased);
 cetta_prime_checkpoint_copy_free(copy);CHECK(!source->leased);
 CHECK(cetta_native_handle_use_release(&use));CHECK(cetta_native_handle_close(lib,"prime-search",id));
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
 registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 puts("Prepared replacement waits for an active source service");
}

/* A hidden active owner must not lose its history or acquire a second
 * retained Resume cursor. Both operations become available after it leaves. */
static void hidden_active_history_and_resume_cases(void) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 const unsigned operations=2u;
#else
 const unsigned operations=1u;
#endif
 for(unsigned operation=0u;operation<operations;operation++)for(unsigned mode=0u;mode<3u;mode++) {
  unsigned client=560u+3u*operation+mode,cut=0u;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
  eval_set_library_context(lib);term_universe_set_persistent_arena(&lib->term_universe,&syntax);
  PrimeNativeControl *source=calloc(1u,sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
  if(operation)policy.history=(PrimeNativeHistorySpec){.enabled=true,.selections=4u,
      .nodes=16u,.bytes=4096u,.owners=8u};
#endif
  CHECK(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(superpose (A B))"),
   -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
   &policy,NULL,NULL,NULL));
  if(mode==2u) {
   source->source_services=prime_native_source_services_new();assert(source->source_services);
  }
  uint64_t id=0u;CHECK(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&id));
  source->handle_id=id;
  Atom *holder=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",id);assert(holder);
  Atom *fields[]={atom_symbol(&syntax,"interaction:choices"),holder,
      atom_symbol(&syntax,"Resume"),atom_int(&syntax,1)};
  Atom *choices=atom_expr(&syntax,fields,4u);assert(choices);
  Bindings env;bindings_init(&env);OutcomeSet out;outcome_set_init(&out);
  fflush(NULL);pid_t pid=fork();assert(pid>=0);
  if(!pid) {
   PrimeEvalStackDriver hidden={0};PrimeNativeRunGuard run={0};PrimeEvalTopGuard top={0};
   if(!mode)prime_native_run_enter(source,&run);
   else if(mode==1u) {g_prime_eval_stack_driver=&source->driver;prime_eval_top_enter(&top);}
   else atomic_store_explicit(&source->source_services->active,true,memory_order_release);
   g_prime_eval_stack_driver=&hidden;g_prime_eval_top_authority=NULL;
   if(!operation) {
    if(!prime_native_interaction_dispatch(&space,&syntax,choices,-1,&env,&out) ||
       out.len!=1u || out.items[0].atom!=choices || source->leased)_exit(1);
   }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
   else {
    const char *refusal=NULL;
    if(prime_checkpoint_forget_recording(holder,1u,&refusal)!=CETTA_CONTINUATION_DEFERRED ||
       source->history.forgotten || !source->history.group)_exit(2);
   }
#endif
   if(!mode)prime_native_run_leave(&run);
   else if(mode==1u) {prime_eval_top_leave(&top);g_prime_eval_stack_driver=NULL;}
   else {atomic_store_explicit(&source->source_services->active,false,memory_order_release);
    g_prime_eval_stack_driver=NULL;}
   if(!operation) {
    if(!prime_native_interaction_dispatch(&space,&syntax,choices,-1,&env,&out) ||
       out.len!=2u || !out.items[1].atom || out.items[1].atom->kind!=ATOM_EXPR ||
       !out.items[1].atom->expr.len ||
       !atom_is_symbol(out.items[1].atom->expr.elems[0],"InteractionChoices") || !source->leased)_exit(3);
   }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
   else {
    const char *refusal=NULL;
    if(prime_checkpoint_forget_recording(holder,1u,&refusal)!=CETTA_CONTINUATION_READY ||
       !source->history.forgotten)_exit(4);
   }
#endif
   _exit(0);
  }
  int child_status=0;CHECK(waitpid(pid,&child_status,0)==pid &&
   WIFEXITED(child_status) && WEXITSTATUS(child_status)==0);
  CHECK(!source->leased && cetta_native_handle_get_owned(lib,"prime-search",holder)==source);
  CHECK(cetta_native_handle_close(lib,"prime-search",id));
  outcome_set_free(&out);bindings_free(&env);cetta_library_context_free(lib);free(lib);
  eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
 puts("Hidden active owners retain history and Resume authority until their scopes leave");
}

/* A queued foreign service owns a live cursor and its creating driver. A
 * refused capture must neither share nor cancel either allocation. */
static void opaque_service_copy_cases(void) {
 for(unsigned resume=0u;resume<2u;resume++) {
  const unsigned client=13u,cut=resume;
  Arena syntax;arena_init(&syntax);arena_set_hashcons(&syntax,NULL);
  Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
  registry_bind(&reg,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
  eval_set_library_context(lib);Atom *error=NULL;
  CHECK(cetta_library_import(lib,"rhometta",&space,&syntax,&syntax,&reg,1000000,&error)&&!error);
  PrimeNativeControl *source=calloc(1u,sizeof(*source));assert(source);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u};
  CHECK(prime_native_control_init(source,&space,&syntax,
    parse(&syntax,"(__cetta_lib_rhometta_values (rho:par (rho:val 1) (rho:val 2)))"),
    -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
  PrimeNativeControl *unpublished=source;Atom *holder=NULL;
  CHECK(prime_native_control_admit_owned(&unpublished,&syntax,&holder)==CETTA_CONTINUATION_READY && !unpublished);
  uint64_t id=source->handle_id;
  Result result={0};arena_init(&result.storage);PrimeEvalStackFrame *frame=NULL;
  for(unsigned n=0u;n<1000u&&!frame;n++) {
   prime_native_control_run(source,1u,seen,&result);
   for(PrimeEvalStackFrame *f=source->frames;f;f=f->control_next)
    if(f->native_service){frame=f;break;}
  }
  CHECK(frame && !source->driver.top && source->driver.prepared_opaque_frames==1u);assert(frame);
  PrimeNativeService *service=frame->native_service;void *computation=service->execution.computation;
  uint64_t spent=source->steps;recording_retirement_watch=service;recording_retirements=0u;
  CettaContinuationStatus status;CettaPrimeCheckpointCopy *copy=cetta_prime_checkpoint_copy_new(lib,holder,&status);
  CHECK(!copy && status==CETTA_CONTINUATION_UNSUPPORTED && !source->leased);
  PrimeParentCopy attempted={0};PrimeEvalStackFrame *shell=NULL;
  CHECK(!parent_copy_frame_shells(&attempted,frame,&shell,false)&&!shell &&
      attempted.refusal&&!strcmp(attempted.refusal,"FrameExternalOwner"));
  /* Even cleanup after a failed lower-level edge copy must own no source
   * service. This exercises the actual frame releaser on the partial image. */
  PrimeNativeControl image={0};arena_init(&image.storage);
  AtomDeepCopySession *atoms=atom_deep_copy_session_new(&image.storage);assert(atoms);
  PrimeParentCopy partial={.source=source,.image=&image,.atoms=atoms};
  shell=calloc(1u,sizeof(*shell));assert(shell);
  CHECK(!parent_copy_frame(&partial,frame,shell));prime_eval_stack_frame_free(shell);
  atom_deep_copy_session_free(atoms);arena_free(&image.storage);
  CHECK(!recording_retirements && frame->native_service==service &&
      service->execution.computation==computation && service->driver==&source->driver &&
      source->driver.prepared_opaque_frames==1u && source->steps==spent && !result.count);
  if(resume) {
   prime_native_control_run(source,3000u,seen,&result);
   CHECK(prime_native_control_closed(source) && result.count==1u &&
       atom_eq(result.values[0],parse(&result.storage,"(1 2)")));
  }
  CHECK(cetta_native_handle_close(lib,"prime-search",id));
  CHECK(recording_retirements==1u && !recording_retirement_watch);
  arena_free(&result.storage);cetta_library_context_free(lib);free(lib);
  eval_set_library_context(NULL);registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
}

static void lifecycle_cases(void);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
#include "native_parent_recording_compare.inc"
#endif
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static void forgetting_recorded_parent(bool weighted, bool replace) {
 const unsigned client=200u+(unsigned)weighted+2u*(unsigned)replace,cut=19u;
 Arena syntax,notebook;arena_init(&syntax);arena_init_detached(&notebook);arena_set_hashcons(&syntax,NULL);
 Space space;space_init(&space);Registry reg;registry_init(&reg);g_registry=&reg;
 registry_bind(&reg,"&self",atom_space(&syntax,&space));
 CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
 cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
 eval_set_library_context(lib);
 const char *equations[]={weighted?"(=w (coin) 2 3)":"(= (coin) 2)",
  weighted?"(=w (coin) 2 3)":"(= (coin) 2)",
  weighted?"(=w (coin) 5 7)":"(= (coin) 5)","(= (paired $x) (Pair $x $x))"};
 for(size_t i=0u;i<4u;i++)space_add(&space,parse(&syntax,equations[i]));
 CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,.workers=1u,
  .work_budget=UINT64_MAX,.costs=true,
  .history={.enabled=true,.selections=512u,.nodes=32u,.bytes=65536u,.owners=10000u}};
 PrimeNativeControl *source=malloc(sizeof(*source));assert(source);
 CHECK(prime_native_control_init(source,&space,&syntax,parse(&syntax,"(paired (coin))"),-1,true,
  (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},&policy,NULL,NULL,NULL));
 Result prefix={0},suffix={0};arena_init(&prefix.storage);arena_init(&suffix.storage);
 prime_native_control_run(source,cut,seen,&prefix);
 CHECK(source->steps==cut&&source->history.count&&source->history.group&&!prime_native_control_closed(source));
 PrimeNeedRecording *recording=source->history.group;
 unsigned retired_before=recording_retirements;
 assert(!recording_retirement_watch);recording_retirement_watch=recording;
 PrimeNeedReceipt outside=source->history.entries[0].operation;
 CHECK(prime_need_recording_promote(&notebook,&outside)&&
  arena_retains_owner(&notebook,recording,prime_need_recording_release));
 uint64_t source_id=0u,image_id=0u;
 CHECK(cetta_native_handle_alloc(lib,"prime-search",source,prime_native_control_destroy,&source_id));
 source->handle_id=source_id;
 Atom *source_token=cetta_native_handle_owned_atom(lib,&syntax,"prime-search",source_id);assert(source_token);
 Atom *alias=atom_deep_copy(&prefix.storage,source_token);assert(alias);
 CettaNativeHandleUse use={0},other_use={0};
 if(replace)CHECK(cetta_native_handle_acquire_owned(lib,"prime-search",source_token,
  CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use));
 PrimeNativeControl before=*source;
 CettaContinuationStatus status;
 CettaPrimeCheckpointCopy *copy=replace
  ?cetta_prime_checkpoint_copy_new_reserved(lib,source_token,&status)
  :cetta_prime_checkpoint_copy_new(lib,source_token,&status);
 CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
 PrimeNativeControl *image=copy->image;
 size_t count=0u;
 AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,true,&count);
 CHECK(pairs);assert(pairs);
 AtomDeepCopySession *atoms=atom_deep_copy_session_new(&image->storage);assert(atoms);
 CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count)&&prime_need_recording_copy_forget(atoms));
 status=cetta_prime_checkpoint_copy_prepare(copy,atoms);
 if(status!=CETTA_CONTINUATION_READY)fprintf(stderr,"FORGET_REFUSAL %s\n",cetta_prime_checkpoint_copy_refusal(copy));
 if(replace)before.leased=true;
 CHECK(status==CETTA_CONTINUATION_READY&&!memcmp(&before,source,sizeof(before)));
 CHECK(!image->history.spec.enabled&&!image->policy.history.enabled&&!image->history.group&&
  !image->history.entries&&!image->history.count&&image->steps==source->steps&&
  image->event_cost.total.work==source->event_cost.total.work&&
  !arena_retains_owner(&image->storage,recording,prime_need_recording_release));
 atom_deep_copy_session_free(atoms);free(pairs);
 Atom *image_token=NULL;
 if(replace) {
  CHECK(source->leased&&copy->reserved);
  CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&image_token)==CETTA_CONTINUATION_UNSUPPORTED&&!image_token);
  CHECK(cetta_native_handle_use_fork_lifetime(&other_use,&use));
  CHECK(cetta_prime_checkpoint_copy_replace(copy,&use)==CETTA_CONTINUATION_DEFERRED&&
   copy->image==image&&copy->prepared&&source->leased);
  CHECK(cetta_native_handle_get_owned(lib,"prime-search",alias)==source&&recording_retirements==retired_before);
  CHECK(cetta_native_handle_use_release(&other_use));
  CHECK(cetta_prime_checkpoint_copy_replace(copy,&use)==CETTA_CONTINUATION_READY);
  image_token=source_token;image_id=source_id;
  CHECK(cetta_native_handle_get_owned(lib,"prime-search",alias)==image&&
   cetta_native_handle_get_owned(lib,"prime-search",source_token)==image&&
   cetta_native_handle_use_resource(&use)==image&&image->handle_id==source_id&&!image->leased);
  CHECK(!copy->image&&!copy->source&&!copy->reserved&&!copy->prepared);
  CHECK(cetta_native_handle_use_release(&use));
 } else {
  CHECK(cetta_prime_checkpoint_copy_publish(copy,&syntax,&image_token)==CETTA_CONTINUATION_READY);
  assert(image_token&&cetta_native_handle_arg(image_token,"prime-search",&image_id));
 }
 cetta_prime_checkpoint_copy_free(copy);
 if(!replace)CHECK(cetta_native_handle_close(lib,"prime-search",source_id));
 CHECK(recording_retirements==retired_before);
 CHECK(prime_need_recording_status(prime_need_receipt_recording_owner(&outside)).nodes>0u);
 arena_free(&notebook);
 CHECK(recording_retirements==retired_before+1u&&!recording_retirement_watch);
 if(replace) {
  CettaSearchPolicySpec resumed_policy=image->policy;resumed_policy.work_budget=10000u;
  OutcomeSet observed;outcome_set_init(&observed);
  CHECK(prime_native_control_select(&space,&syntax,alias,-1,0u,&resumed_policy,&observed,NULL,NULL));
  CHECK(observed.len==1u&&observed.items[0].atom->kind==ATOM_EXPR&&
   atom_is_symbol(observed.items[0].atom->expr.elems[0],"Observation"));
  Atom *values=observed.items[0].atom->expr.elems[1];
  CHECK(values->kind==ATOM_EXPR&&prefix.count+values->expr.len==3u);
  CHECK(cetta_native_handle_get_owned(lib,"prime-search",alias)==image);
  for(size_t i=0u;i<values->expr.len;i++) {
   const Outcome *row=&image->output.items[prefix.count+i];
   CHECK(atom_eq(values->expr.elems[i],row->atom));
   (void)seen(&syntax,row->atom,row,&suffix);
  }
  outcome_set_free(&observed);
 } else prime_native_control_run(image,10000u,seen,&suffix);
 CHECK(prime_native_control_closed(image)&&prefix.count+suffix.count==3u&&!image->history.spec.enabled);
 const char *expected[]={"(Pair 2 2)","(Pair 2 2)","(Pair 5 5)"};
 for(size_t i=0u;i<3u;i++) {
  Result *packet=i<prefix.count?&prefix:&suffix;
  size_t position=i<prefix.count?i:i-prefix.count;
  CHECK(!strcmp(atom_to_string(&syntax,packet->values[position]),expected[i]));
  CHECK(packet->lengths[position]==(weighted?1u:0u));
  if(weighted)CHECK(packet->factors[position][0]==(i<2u?3:7));
 }
 for(size_t i=0u;i<suffix.count;i++)CHECK(!suffix.graphs[i]);
 CHECK(cetta_native_handle_close(lib,"prime-search",image_id));
 arena_free(&prefix.storage);arena_free(&suffix.storage);
 cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
 registry_free(&reg);g_registry=NULL;space_free(&space);arena_free(&syntax);
 printf("FORGOTTEN_PARENT weighted=%u replace=%u failures=%zu\n",weighted,replace,failures);
}
#endif

/* Lower-level runs use the ambient execution owner until explicit cleanup.
 * A first query may create it after the run guard has saved an empty slot. */
static void ambient_execution_scope_cases(void) {
 EvalExecutionContext *previous=g_eval_execution;g_eval_execution=NULL;
 for(unsigned mode=0u;mode<4u;mode++) {
  unsigned client=600u+mode,cut=0u;
  Arena syntax;arena_init(&syntax);Space space;space_init(&space);
  Registry registry;registry_init(&registry);g_registry=&registry;
  registry_bind(&registry,"&self",atom_space(&syntax,&space));
  CettaLibraryContext *lib=malloc(sizeof(*lib));assert(lib);
  cetta_library_context_init_for_language_profile(lib,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
  eval_set_library_context(lib);
  space_add(&space,parse(&syntax,"(=w (ambient-coin) 2 3)"));
  space_add(&space,parse(&syntax,"(=w (ambient-coin) 2 3)"));
  space_add(&space,parse(&syntax,"(=w (ambient-coin) 5 7)"));
  space_add(&space,parse(&syntax,"(= (ambient-pair $x) (Pair $x $x))"));
  space_add(&space,parse(&syntax,"(= (ambient-use $x) $x)"));
  bool private=mode>=2u;
  EvalExecutionContext *initial=(mode&1u)?eval_execution_context():NULL;
  PrimeNativeControl *control=malloc(sizeof(*control));assert(control);
  CettaSearchPolicySpec policy={.native_control=true,.all=true,.retain=true,
   .workers=1u,.work_budget=UINT64_MAX,.costs=true};
  assert(prime_native_control_init(control,&space,&syntax,
   parse(&syntax,private?"(ambient-use (ambient-coin))":
       "(weight:read (algebra 0 1 + *) (ambient-pair (ambient-coin)))"),
   -1,true,(CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
   &policy,NULL,NULL,NULL));
  if(private) {
   /* Exercise first selection during an entered run, rather than relying on
    * a source expression to select a particular compilation strategy. */
   PrimeNativeRunGuard run;prime_native_run_enter(control,&run);
   Bindings common,entrance;bindings_init(&common);bindings_init(&entrance);
   CHECK(prime_need_snapshot_begin(bindings_need_mut(&common)));
   Atom *reference=NULL;
   CHECK(prime_need_allocate_ref(&syntax,parse(&syntax,"(ambient-coin)"),
       &common,&entrance,&reference));
   bool installed=false;
   CHECK(prime_need_call_sources_prepare(&space,&syntax,&reference,1u,
       prime_need_fresh_source_occurrence(),&entrance,&installed)==CETTA_MATCH_MATCHED && installed);
   bindings_free(&entrance);bindings_free(&common);
   prime_native_run_leave(&run);
   CHECK(g_eval_execution==initial);
  }
  Result result={0};arena_init(&result.storage);
  prime_native_control_run(control,3000u,seen,&result);
  CHECK(result.count==3u && prime_native_control_closed(control));
  EvalExecutionContext *owner=private && control->owned_top_episode?
      control->owned_top_episode->execution:g_eval_execution;
  CHECK(owner && (private?(control->owns_execution_episode &&
      owner!=initial && g_eval_execution==initial):(!initial || owner==initial)));
  EvalExecutionLease *lease=owner?owner->lease:NULL;
  CHECK(lease && eval_execution_lease_hold(lease));
  prime_native_control_run(control,0u,seen,&result);
  CHECK(g_eval_execution==(private?initial:owner));
  prime_native_control_destroy(control);arena_free(&result.storage);
  CHECK(lease && (private?!lease->context:lease->context==owner));
  eval_release_temporary_spaces();
  CHECK(!g_eval_execution && lease && !lease->context);
  eval_execution_lease_release(lease);
  cetta_library_context_free(lib);free(lib);eval_set_library_context(NULL);
  registry_free(&registry);g_registry=NULL;space_free(&space);arena_free(&syntax);
 }
 g_eval_execution=previous;
}

int main(int argc,char **argv){SymbolTable syms;symbol_table_init(&syms);symbol_table_init_builtins(&syms,&g_builtin_syms);g_symbols=&syms;VarInternTable vars;var_intern_init(&vars);g_var_intern=&vars;g_hashcons=NULL;
 if(argc==2&&!strcmp(argv[1],"--ambient-execution")) {
  ambient_execution_scope_cases();
  eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;
  symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;
 }
 if(argc==4&&!strcmp(argv[1],"--case")) {
  char *client_end=NULL,*cut_end=NULL;
  unsigned long client=strtoul(argv[2],&client_end,10),cut=strtoul(argv[3],&cut_end,10);
  if(!client_end||*client_end||!cut_end||*cut_end||client>UINT_MAX||cut>UINT_MAX)return 2;
  one((unsigned)client,(unsigned)cut);
  eval_release_temporary_spaces();eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;
  symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;
 }
 if(argc==2&&!strcmp(argv[1],"--borrowed-authority")) {
  borrowed_authority_checkpoint_cases();
  hidden_active_disposal_cases();
  active_service_replacement_case();
  hidden_active_history_and_resume_cases();
  eval_release_temporary_spaces();eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;
  symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;
 }
 if(argc==2&&!strcmp(argv[1],"--installed-fork")) {
  installed_context_fork_checkpoint_cases();
  eval_release_temporary_spaces();eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;
  symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;
 }
 ambient_execution_scope_cases();
 borrowed_authority_checkpoint_cases();
 hidden_active_disposal_cases();
 active_service_replacement_case();
 hidden_active_history_and_resume_cases();
 return_advice_copy_cases();
 opaque_service_copy_cases();
 installed_context_fork_checkpoint_cases();
 const unsigned cuts[]={0,1,3,7,13,19,31,49,79,149,163,401};
 for(unsigned c=0;c<15;c++)for(size_t i=0;i<sizeof(cuts)/sizeof(*cuts);i++)one(c,cuts[i]);
 for(unsigned cut=80u;cut<=128u;cut+=4u)one(12u,cut);
 printf("INSPECTION_COPIES %zu CURRENT_READINESS %zu WORK_READINESS %zu\n",
  inspection_copies,readiness_copies,work_readiness_copies);
 {unsigned client=999u,cut=0u;
  CHECK(inspection_copies>0u && readiness_copies>0u && work_readiness_copies>0u && inspection_refusals_checked);}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
 printf("CAUSAL_FRAME_VIEWS %zu\n",causal_frame_views);
 assert(causal_frame_views>0u);
 recorded_cases();
 forgetting_recorded_parent(false,false);
 forgetting_recorded_parent(true,false);
 forgetting_recorded_parent(false,true);
 forgetting_recorded_parent(true,true);
#endif
 checkpoint_selection_resume_cases();
 checkpoint_captured_scope_cases();
 lifecycle_cases();
 eval_release_temporary_spaces();eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;symbol_table_free(&syms);g_symbols=NULL;printf("CHECKS %zu FAILURES %zu\n",checks,failures);return failures?1:0;}

typedef struct { unsigned retains, releases; } RetirementCounter;
static void count_retention(void *raw) { ++((RetirementCounter *)raw)->retains; }
static void count_retirement(void *raw) { ++((RetirementCounter *)raw)->releases; }
static void lifecycle_cases(void) {
 for(unsigned mode=0u;mode<12u;mode++) {
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
   uint64_t issued=lib->native_handle_next_id;size_t slots=cetta_native_handle_count(lib);
   CHECK(!cetta_prime_checkpoint_copy_new(lib,handle,&status));
   CHECK(status==CETTA_CONTINUATION_UNSUPPORTED);
   CHECK(!memcmp(&original,source,sizeof(original)));
   CHECK(lib->native_handle_next_id==issued&&cetta_native_handle_count(lib)==slots);
   arena_free(&source->driver.prepared_pure_cache.execution_scratch);
   source->driver.prepared_pure_cache.execution_scratch_ready=false;
   original=*source;
  }
  CettaPrimeCheckpointCopy *copy=mode>=10u
   ?cetta_prime_checkpoint_copy_new_reserved(lib,handle,&status)
   :cetta_prime_checkpoint_copy_new(lib,handle,&status);CHECK(copy&&status==CETTA_CONTINUATION_READY);assert(copy);
  CHECK(source->leased);CHECK(!cetta_prime_checkpoint_copy_new(lib,handle,&status)&&status==CETTA_CONTINUATION_DEFERRED);
  size_t count=0u;
  AtomDeepCopyArenaPair *pairs=checkpoint_arena_pairs(copy,&syntax,&space,mode!=6u,&count);
  CHECK(pairs);assert(pairs);
  AtomDeepCopySession *atoms=atom_deep_copy_session_new(pairs[0].destination);assert(atoms);
  if(mode!=1u)CHECK(atom_deep_copy_session_set_arena_map(atoms,pairs,count));
  free(pairs);
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
   CHECK(status==((mode==2u||mode==3u||mode==9u||mode>=10u)?CETTA_CONTINUATION_READY:
     (mode==4u||mode==5u||mode==8u)?CETTA_CONTINUATION_INVALIDATED:CETTA_CONTINUATION_UNSUPPORTED));
   CHECK(cetta_prime_checkpoint_copy_prepare(copy,atoms)==status);
   if(mode==7u)CHECK(copy->refusal&&!strcmp(copy->refusal,"QuiescentOwnerProfile"));
   atom_deep_copy_session_free(atoms);atoms=NULL;
  }
  if(source){if(mode>=10u)original.leased=true;CHECK(!memcmp(&original,source,sizeof(original)));}
  if(mode>=10u) {
   CHECK(source->leased&&copy->reserved&&copy->prepared);
   if(mode==11u) {
    CHECK(cetta_native_handle_close(lib,"prime-search",id));source=NULL;
    CettaNativeHandleUse empty={0};
    CHECK(cetta_prime_checkpoint_copy_replace(copy,&empty)==CETTA_CONTINUATION_INVALIDATED);
    CHECK(copy->prepared&&copy->image);
   }
   cetta_prime_checkpoint_copy_free(copy);copy=NULL;
   if(source){CHECK(!source->leased);original.leased=false;CHECK(!memcmp(&original,source,sizeof(original)));}
  }
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
