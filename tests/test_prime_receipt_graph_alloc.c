#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

/* Link wrapping exercises every real collector allocation, including the
 * shared frame worklist.  Fault injection is confined to this test binary. */
static bool probing;
static size_t fail_at;
static size_t allocation_count;
static void (*allocation_hook)(void);

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *memory, size_t size);

static bool refuse_allocation(void) {
    if (allocation_hook) {
        void (*hook)(void)=allocation_hook;
        allocation_hook=NULL;
        hook();
    }
    return probing && allocation_count++ == fail_at;
}

void *__wrap_malloc(size_t size) {
    return refuse_allocation() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
    return refuse_allocation() ? NULL : __real_calloc(count, size);
}

void *__wrap_realloc(void *memory, size_t size) {
    return refuse_allocation() ? NULL : __real_realloc(memory, size);
}

static PrimeNeedReceiptGraphStatus collect_with_fault(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptGraph *graph, size_t index) {
    fail_at = index;
    allocation_count = 0u;
    probing = true;
    PrimeNeedReceiptGraphStatus status =
        prime_need_receipt_graph_collect(receipt, graph);
    probing = false;
    return status;
}

static void check_atom_projection_refusal(void) {
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    Atom *leaf = atom_int(&source, 91);
    Atom *held = atom_prime_held_wrap(&source, leaf);
    Atom *root = held;
    for (unsigned i = 0u; i < 96u; i++) root = atom_expr2(&source, root, root);
    (void)arena_alloc(&destination, 1u);
    ArenaMark mark = arena_mark(&destination);
    allocation_count = 0u; fail_at = SIZE_MAX; probing = true;
    Atom *image = atom_prime_held_strip(&destination, root);
    probing = false;
    assert(image);
    size_t allocations = allocation_count;
    for (unsigned i = 0u; i < 96u; i++) {
        assert(image->expr.elems[0] == image->expr.elems[1]);
        image = image->expr.elems[0];
    }
    assert(image == leaf && allocations > 0u);
    arena_reset(&destination, mark);
    for (size_t i = 0u; i < allocations; i++) {
        allocation_count = 0u; fail_at = i; probing = true;
        image = atom_prime_held_strip(&destination, root);
        probing = false;
        assert(!image && allocation_count >= i + 1u);
        assert(atom_prime_held_is(held) && root->expr.elems[0] == root->expr.elems[1]);
        arena_reset(&destination, mark);
    }
    assert(atom_prime_held_strip(&destination, root));
    arena_free(&destination);
    arena_free(&source);
    printf("PASS: %zu graph projection allocation refusals leave source sharing unchanged\n", allocations);
}


typedef struct {unsigned references;} ReaderAllocationOwner;
static const char reader_allocation_copy_kind;
static bool reader_allocation_retain(void *raw) {
    ((ReaderAllocationOwner *)raw)->references++;return true;
}
static void reader_allocation_release(void *raw) {
    ReaderAllocationOwner *owner=raw;assert(owner->references);owner->references--;
}
static const PrimeNeedReaderValueOps reader_allocation_ops={
    .retain=reader_allocation_retain,.release=reader_allocation_release,
    .copy_kind=&reader_allocation_copy_kind,.copy_size=sizeof(ReaderAllocationOwner),
};
static CettaMatchCompletion reader_allocation_clear(uint64_t key,
        const PrimeNeedReaderValue *value,void *state,bool *clear) {
    (void)value;*clear=state && key!=22u;
    return CETTA_MATCH_MATCHED;
}
static void check_reader_context_refusal(void) {
    ReaderAllocationOwner owner={0};
    PrimeNeedReaderValue value={&owner,&reader_allocation_ops};
    PrimeNeedReaderContext *base=NULL,*first=NULL,*second=NULL,*joined=NULL,*out=NULL;
    assert(prime_need_reader_context_update(NULL,11u,&value,&base)==CETTA_MATCH_MATCHED);
    assert(prime_need_reader_context_update(base,22u,&value,&first)==CETTA_MATCH_MATCHED);
    assert(prime_need_reader_context_update(first,33u,&value,&second)==CETTA_MATCH_MATCHED);
    size_t refused=0u;
    for (unsigned operation=0u;operation<4u;operation++) {
        allocation_count=0u;fail_at=SIZE_MAX;probing=true;
        CettaMatchCompletion status=operation==0u
            ? prime_need_reader_context_update(second,44u,&value,&out)
            : operation==1u ? prime_need_reader_context_merge(base,second,&out)
                : operation==2u ? prime_need_reader_context_import(first,second,base,&out)
                    : prime_need_reader_context_clear_if(second,reader_allocation_clear,&owner,&out);
        probing=false;
        assert(status==CETTA_MATCH_MATCHED && out);
        size_t allocations=allocation_count;
        prime_need_reader_context_release(out);out=NULL;
        for (size_t i=0u;i<allocations;i++) {
            prime_need_reader_context_retain(base);out=base;
            unsigned before=owner.references;
            allocation_count=0u;fail_at=i;probing=true;
            status=operation==0u ? prime_need_reader_context_update(second,44u,&value,&out)
                : operation==1u ? prime_need_reader_context_merge(base,second,&out)
                    : operation==2u ? prime_need_reader_context_import(first,second,base,&out)
                        : prime_need_reader_context_clear_if(second,reader_allocation_clear,&owner,&out);
            probing=false;
            assert(status==CETTA_MATCH_INCOMPLETE && out==base && owner.references==before);
            prime_need_reader_context_release(out);out=NULL;refused++;
        }
    }
    allocation_count=0u;fail_at=0u;probing=true;
    CettaMatchCompletion unchanged=prime_need_reader_context_clear_if(second,reader_allocation_clear,NULL,&out);
    probing=false;
    assert(unchanged==CETTA_MATCH_MATCHED && out==second && allocation_count==0u);
    prime_need_reader_context_release(out);out=NULL;
    prime_need_reader_context_release(base);prime_need_reader_context_release(first);
    prime_need_reader_context_release(second);prime_need_reader_context_release(joined);
    assert(!owner.references);
    printf("PASS: %zu reader-context allocation refusals preserve versions and owners\n",refused);
}

static void check_reader_copy_refusal(void) {
    ReaderAllocationOwner owners[2]={{0},{0}},images[2]={{0},{0}};
    PrimeNeedReaderValue values[2]={{&owners[0],&reader_allocation_ops},
        {&owners[1],&reader_allocation_ops}};
    PrimeNeedReaderContext *base=NULL,*production=NULL,*recipient=NULL,*imported=NULL,*out=NULL;
    assert(prime_need_reader_context_update(NULL,11u,&values[0],&base)==CETTA_MATCH_MATCHED);
    assert(prime_need_reader_context_update(base,22u,&values[1],&production)==CETTA_MATCH_MATCHED);
    for (size_t i=0u;i<40u;i++)
        assert(prime_need_reader_context_update(production,33u,&values[i%2u],&production)==
            CETTA_MATCH_MATCHED);
    assert(prime_need_reader_context_update(base,44u,&values[0],&recipient)==CETTA_MATCH_MATCHED);
    assert(prime_need_reader_context_import(base,production,recipient,&imported)==CETTA_MATCH_MATCHED);
    unsigned before[2]={owners[0].references,owners[1].references};
    Arena destination;arena_init_detached(&destination);
    size_t allocations=0u;
    for (size_t attempt=0u;;attempt++) {
        AtomDeepCopySession *session=atom_deep_copy_session_new(&destination);assert(session);
        for (size_t i=0u;i<2u;i++)
            assert(atom_deep_copy_session_object_record(session,&reader_allocation_copy_kind,
                &owners[i],&images[i],sizeof(owners[i])));
        if (attempt) {out=base;prime_need_reader_context_retain(out);}
        allocation_count=0u;fail_at=attempt ? attempt-1u : SIZE_MAX;probing=true;
        CettaMatchCompletion status=prime_need_reader_context_copy_with_session(imported,
            &destination,session,&out);
        probing=false;
        if (!attempt) {
            allocations=allocation_count;
            assert(status==CETTA_MATCH_MATCHED && out && out!=imported && allocations);
        } else assert(status==CETTA_MATCH_INCOMPLETE && out==base);
        PrimeNeedReaderValue unchanged;
        assert(prime_need_reader_context_lookup(imported,11u,&unchanged) && unchanged.value==&owners[0]);
        prime_need_reader_context_release(out);out=NULL;
        atom_deep_copy_session_free(session);
        assert(owners[0].references==before[0] && owners[1].references==before[1] &&
            !images[0].references && !images[1].references);
        if (attempt==allocations) break;
    }
    prime_need_reader_context_release(base);prime_need_reader_context_release(production);
    prime_need_reader_context_release(recipient);prime_need_reader_context_release(imported);
    arena_free(&destination);
    assert(!owners[0].references && !owners[1].references);
    printf("PASS: %zu reader-copy allocation refusals preserve the old output and both owner graphs\n",
        allocations);
}

static void check_production_support_refusal(Arena *arena) {
    PrimeNeedSnapshot root = {0}, base = {0}, running = {0}, produced = {0};
    uint64_t thunk = 0u;
    Atom *value = atom_int(arena, 43);
    assert(prime_need_snapshot_begin(&root));
    assert(prime_need_snapshot_allocate(arena, &root, value, &base, &thunk));
    PrimeNeedReadCapture *capture = prime_need_read_capture_new(&base, &base);
    assert(capture);
    PrimeNeedReadScope scope;
    prime_need_read_scope_enter(&scope, capture);
    PrimeNeedCellView cell;
    fail_at = 0u; allocation_count = 0u; probing = true;
    bool found = prime_need_snapshot_lookup(&base, thunk, &cell);
    probing = false;
    prime_need_read_scope_leave(&scope);
    assert(found && cell.cache_state == PRIME_NEED_CACHE_EMPTY && allocation_count == 1u);
    assert(!prime_need_read_capture_complete(capture));
    prime_need_read_capture_free(capture);

    capture = prime_need_read_capture_new(&base, &base);
    assert(capture);
    prime_need_read_scope_enter(&scope, capture);
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(arena, &base, thunk, evaluator, &running));
    assert(prime_need_snapshot_resolve_value(arena, &running, thunk, evaluator, value, &produced));
    prime_need_read_scope_leave(&scope);
    assert(prime_need_read_capture_complete(capture));
    for (size_t i = 0u; i < 2u; i++) {
        fail_at = i; allocation_count = 0u; probing = true;
        PrimeNeedReadCapture *copy = prime_need_read_capture_copy(capture);
        probing = false;
        assert(!copy && allocation_count == i + 1u);
        assert(prime_need_read_capture_complete(capture));
    }
    for (size_t i = 0u; i < 2u; i++) {
        PrimeNeedSnapshot out = root;
        fail_at = i; allocation_count = 0u; probing = true;
        CettaMatchCompletion imported = prime_need_snapshot_import_production_checked(arena, capture, &produced,
            thunk, &base, &out);
        probing = false;
        assert(imported == CETTA_MATCH_INCOMPLETE && allocation_count == i + 1u &&
            memcmp(&out, &root, sizeof(out)) == 0);
    }
    PrimeNeedSnapshot imported;
    assert(prime_need_snapshot_import_production(arena, capture, &produced, thunk, &base, &imported));
    assert(prime_need_snapshot_lookup(&imported, thunk, &cell) && cell.cached == value);
    prime_need_read_capture_free(capture);
    puts("PASS: 5 production-support allocation refusals preserve lookups, evidence and destination");
}

static Atom *read_capture_resolver(void *raw,Atom *atom) {
    (*(unsigned *)raw)++;
    return atom;
}

static void check_read_capture_transport(void) {
    Arena source,terms,image,image_terms;
    arena_init_detached(&source);arena_init_detached(&terms);
    arena_init_detached(&image);arena_init_detached(&image_terms);
    Atom *value=atom_expr2(&terms,atom_symbol(&terms,"CopiedReadValue"),atom_int(&terms,77));
    Atom *layer=atom_symbol(&terms,"ReadLayer");
    for(unsigned i=0u;i<96u;i++)value=atom_expr2(&terms,layer,value);
    Atom *absent=atom_expr2(&terms,atom_symbol(&terms,"AbsentReadOrigin"),value);
    PrimeNeedSnapshot root={0},allocated={0},context={0},running={0},entrance={0};
    PrimeNeedSnapshot changed={0},stored={0},produced={0},shadow={0};
    PrimeNeedSnapshot import_shadow={0},cell_shadow={0};
    uint64_t dependency=0u,result=0u,stored_cell=0u,shadow_cell=0u,import_cell=0u;
    assert(prime_need_snapshot_begin(&root));
    assert(prime_need_snapshot_allocate(&source,&root,value,&allocated,&dependency));
    assert(prime_need_snapshot_allocate(&source,&allocated,value,&context,&result));
    uint64_t evaluator=prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&source,&context,dependency,evaluator,&running));
    assert(prime_need_snapshot_resolve_value(&source,&running,dependency,evaluator,value,&entrance));
    evaluator=prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&source,&context,dependency,evaluator,&running));
    assert(prime_need_snapshot_resolve_value(&source,&running,dependency,evaluator,
        atom_int(&terms,9),&changed));
    PrimeNeedReadCapture *capture=prime_need_read_capture_new(&context,&entrance);
    assert(capture);
    PrimeNeedReadScope scope;prime_need_read_scope_enter(&scope,capture);
    PrimeNeedCellView cell;
    assert(prime_need_snapshot_lookup(&entrance,dependency,&cell) && cell.cached==value);
    assert(!prime_need_snapshot_lookup(&entrance,UINT64_MAX,&cell));
    assert(!prime_need_snapshot_find_imported_cell(&entrance,119u,absent,NULL));
    assert(prime_need_snapshot_allocate_persisted(&source,&entrance,absent,113u,
        &stored,&stored_cell));
    evaluator=prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&source,&stored,result,evaluator,&running));
    assert(prime_need_snapshot_resolve_value(&source,&running,result,evaluator,value,&produced));
    prime_need_read_scope_leave(&scope);
    assert(prime_need_read_capture_complete(capture));
    assert(prime_need_snapshot_allocate_persisted(&source,&entrance,absent,113u,
        &shadow,&shadow_cell) && shadow_cell!=stored_cell);
    assert(prime_need_snapshot_allocate_persisted(&source,&entrance,absent,119u,
        &import_shadow,&import_cell));
    assert(prime_need_snapshot_allocate_named(&source,&entrance,absent,UINT64_MAX,&cell_shadow));

    AtomDeepCopyArenaPair pairs[]={{&source,&image},{&terms,&image_terms}};
    AtomDeepCopySession *session=atom_deep_copy_session_new(&image);assert(session);
    assert(atom_deep_copy_session_set_arena_map(session,pairs,2u));
    PrimeNeedReadCapture *copy=prime_need_read_capture_copy_with_session(&image,session,capture);
    assert(copy && copy!=capture && prime_need_read_capture_complete(copy));
    PrimeNeedSnapshot mapped_context=context,mapped_entrance=entrance;
    PrimeNeedSnapshot mapped_produced=produced,mapped_changed=changed,mapped_shadow=shadow;
    PrimeNeedSnapshot mapped_import_shadow=import_shadow,mapped_cell_shadow=cell_shadow;
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_context));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_entrance));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_produced));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_changed));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_shadow));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_import_shadow));
    assert(prime_need_snapshot_copy_with_session(&image,session,&mapped_cell_shadow));
    Atom *mapped_value=atom_deep_copy_session_copy(session,value);
    Atom *mapped_absent=atom_deep_copy_session_copy(session,absent);
    assert(mapped_value && mapped_value!=value && mapped_absent && mapped_absent!=absent);
    assert(prime_need_read_capture_has_cuts(copy,&mapped_context,&mapped_entrance));
    assert(!prime_need_read_capture_has_cuts(copy,&context,&entrance));
    assert(prime_need_snapshot_lookup(&mapped_produced,result,&cell) && cell.cached==mapped_value);

    PrimeNeedReadCapture *unfinished=prime_need_read_capture_new(&context,&entrance);
    PrimeNeedSnapshot unrelated={0};assert(unfinished && prime_need_snapshot_begin(&unrelated));
    prime_need_read_scope_enter(&scope,unfinished);
    assert(!prime_need_snapshot_lookup(&unrelated,result,&cell));
    prime_need_read_scope_leave(&scope);
    assert(!prime_need_read_capture_complete(unfinished));
    PrimeNeedReadCapture *unfinished_copy=prime_need_read_capture_copy_with_session(&image,session,unfinished);
    PrimeNeedSnapshot unchanged=mapped_context;
    assert(unfinished_copy && !prime_need_read_capture_complete(unfinished_copy) &&
        prime_need_snapshot_import_production_checked(&image,unfinished_copy,&mapped_produced,
            result,&mapped_entrance,&unchanged)==CETTA_MATCH_INCOMPLETE &&
        memcmp(&unchanged,&mapped_context,sizeof(unchanged))==0);
    prime_need_read_capture_free(unfinished_copy);prime_need_read_capture_free(unfinished);

    /* Missing/identity owners, a wrong target and destructive source
     * forwarding cannot justify this non-destructive metadata transport. */
    for(unsigned mode=0u;mode<6u;mode++) {
        Arena target;arena_init_detached(&target);
        AtomDeepCopySession *bad=atom_deep_copy_session_new(&target);assert(bad);
        AtomDeepCopyArenaPair selected[]={{&source,mode==1u?&source:&target},
            {&terms,&target}};
        if(mode<2u)assert(atom_deep_copy_session_set_arena_map(bad,selected+(!mode),mode?2u:1u));
        if(mode==3u)assert(atom_deep_copy_session_forward_region(bad,&terms,(ArenaMark){0}));
        unsigned resolved=0u;
        if(mode==4u)atom_deep_copy_session_set_resolver(bad,read_capture_resolver,&resolved);
        if(mode==5u) {
            assert(!atom_deep_copy_session_copy(bad,NULL) && atom_deep_copy_session_failed(bad));
            allocation_count=0u;fail_at=0u;probing=true;
        }
        uint32_t original_arena=value->arena_id;
        assert(!prime_need_read_capture_copy_with_session(mode==2u?&image_terms:&target,bad,capture));
        probing=false;
        assert(!resolved && (mode!=5u || allocation_count==0u));
        assert(prime_need_read_capture_complete(capture) &&
            prime_need_read_capture_has_cuts(capture,&context,&entrance) &&
            value->arena_id==original_arena);
        atom_deep_copy_session_free(bad);arena_free(&target);
    }

    /* Preallocate arena blocks so the sweep targets fallible graph/capture
     * preparation, independently of the arena's backing-allocation policy. */
    size_t allocations=0u;
    for(size_t fault=0u;;fault++) {
        Arena target,target_terms;arena_init_detached(&target);arena_init_detached(&target_terms);
        (void)arena_alloc(&target,1u);(void)arena_alloc(&target_terms,1u);
        AtomDeepCopySession *attempt=atom_deep_copy_session_new(&target);assert(attempt);
        AtomDeepCopyArenaPair map[]={{&source,&target},{&terms,&target_terms}};
        assert(atom_deep_copy_session_set_arena_map(attempt,map,2u));
        allocation_count=0u;fail_at=fault;probing=true;
        PrimeNeedReadCapture *candidate=prime_need_read_capture_copy_with_session(&target,attempt,capture);
        probing=false;
        bool done=candidate!=NULL;
        if(done) {allocations=allocation_count;assert(fault==allocations);}
        else assert(allocation_count>=fault+1u);
        assert(prime_need_read_capture_complete(capture) &&
            prime_need_read_capture_has_cuts(capture,&context,&entrance));
        prime_need_read_capture_free(candidate);
        atom_deep_copy_session_free(attempt);arena_free(&target_terms);arena_free(&target);
        if(done)break;
    }
    prime_need_read_capture_free(capture);
    atom_deep_copy_session_free(session);
    arena_free(&terms);arena_free(&source);

    /* The copied support still distinguishes a changed positive dependency
     * and a conflicting insertion after both source arenas have retired. */
    PrimeNeedSnapshot imported=mapped_context,saved=imported;
    assert(prime_need_snapshot_import_production_checked(&image,copy,&mapped_produced,
        result,&mapped_changed,&imported)==CETTA_MATCH_REFUTED &&
        memcmp(&imported,&saved,sizeof(saved))==0);
    assert(prime_need_snapshot_import_production_checked(&image,copy,&mapped_produced,
        result,&mapped_shadow,&imported)==CETTA_MATCH_REFUTED &&
        memcmp(&imported,&saved,sizeof(saved))==0);
    assert(prime_need_snapshot_import_production_checked(&image,copy,&mapped_produced,
        result,&mapped_import_shadow,&imported)==CETTA_MATCH_REFUTED &&
        memcmp(&imported,&saved,sizeof(saved))==0);
    assert(prime_need_snapshot_import_production_checked(&image,copy,&mapped_produced,
        result,&mapped_cell_shadow,&imported)==CETTA_MATCH_REFUTED &&
        memcmp(&imported,&saved,sizeof(saved))==0);
    assert(prime_need_snapshot_import_production(&image,copy,&mapped_produced,result,
        &mapped_entrance,&imported));
    assert(prime_need_snapshot_lookup(&imported,result,&cell) && cell.cached==mapped_value);
    Atom *leaf=cell.cached;
    for(unsigned i=0u;i<96u;i++) {
        assert(leaf->kind==ATOM_EXPR && leaf->expr.len==2u &&
            atom_is_symbol(leaf->expr.elems[0],"ReadLayer"));
        leaf=leaf->expr.elems[1];
    }
    assert(!strcmp(atom_to_string(&image_terms,leaf),"(CopiedReadValue 77)"));
    uint64_t found=0u;
    assert(prime_need_snapshot_find_imported_cell(&imported,113u,mapped_absent,&found) &&
        found==stored_cell);
    prime_need_read_capture_free(copy);arena_free(&image_terms);arena_free(&image);
    printf("PASS: independent read support survives source retirement; %zu allocation refusals preserve both cuts\n",allocations);
}

static void check_capture_allocation_refusal(Arena *arena) {
    Atom *left=atom_int(arena,7),*right=atom_int(arena,7);
    for (unsigned i=0u;i<192u;i++) {
        left=atom_expr2(arena,left,left);
        right=atom_expr2(arena,right,right);
    }
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    bool agrees=atom_capture_identical(left,right);
    probing=false;
    size_t allocations=allocation_count;
    assert(agrees && allocations>=4u);
    for (size_t i=0u;i<allocations;i++) {
        fail_at=i;allocation_count=0u;probing=true;
        CettaMatchCompletion status=atom_capture_identical_checked(left,right);
        probing=false;
        assert(status==CETTA_MATCH_INCOMPLETE && allocation_count==i+1u);
        assert(atom_capture_identical(left,right));
    }
    printf("PASS: %zu captured-input allocation refusals preserve both source DAGs\n",allocations);
}

static CettaMatchCompletion state_selected_initial(void *context, StateCell *cell, Atom **out) {
    (void)context;
    /* All allocations before this selected-view observation are checked
     * scratch preparation. Arena allocation policy remains a separate owner. */
    probing = false;
    *out = cell->value;
    return CETTA_MATCH_MATCHED;
}

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    PrimeNeedRecording *group;
    bool collecting,retired;
} graph_retirement={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};

static void *graph_retire_reader(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&graph_retirement.mutex));
    while (!graph_retirement.collecting)
        assert(!pthread_cond_wait(&graph_retirement.changed,&graph_retirement.mutex));
    assert(!pthread_mutex_unlock(&graph_retirement.mutex));
    prime_need_recording_forget(graph_retirement.group);
    prime_need_recording_release(graph_retirement.group);
    assert(!pthread_mutex_lock(&graph_retirement.mutex));
    graph_retirement.retired=true;
    assert(!pthread_cond_signal(&graph_retirement.changed));
    assert(!pthread_mutex_unlock(&graph_retirement.mutex));
    return NULL;
}

static void graph_retire_during_collection(void) {
    assert(!pthread_mutex_lock(&graph_retirement.mutex));
    graph_retirement.collecting=true;
    assert(!pthread_cond_signal(&graph_retirement.changed));
    while (!graph_retirement.retired)
        assert(!pthread_cond_wait(&graph_retirement.changed,&graph_retirement.mutex));
    assert(!pthread_mutex_unlock(&graph_retirement.mutex));
}

/* Interpose at the collector's first real allocation, after it has entered
 * the selected view and before it traverses the retained frame. Retirement
 * on the other thread must not reclaim that frame or its returned payload. */
static void check_recording_graph_retirement(Arena *arena) {
    PrimeNeedRecording *group=prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u,16384u,1u,0u});
    PrimeNeedReceipt receipt={0};
    assert(group && prime_need_recording_reserve(group,NULL,4u,16384u,&receipt) &&
        prime_need_receipt_observe_cell(arena,&receipt,31u,9u,atom_int(arena,71),&receipt));
    prime_need_recording_carrier_retain(group);
    PrimeNeedReceiptGraph graph={0};
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    PrimeNeedReceiptGraphStatus status=prime_need_receipt_graph_collect(&receipt,&graph);
    probing=false;size_t allocations=allocation_count;
    assert(status==PRIME_NEED_RECEIPT_GRAPH_COMPLETE && allocations>0u);
    PrimeNeedReceiptGraph saved=graph;
    for(size_t i=0u;i<allocations;i++) {
        fail_at=i;allocation_count=0u;probing=true;
        status=prime_need_receipt_graph_collect(&receipt,&graph);
        probing=false;
        assert(status==PRIME_NEED_RECEIPT_GRAPH_REFUSED &&
            allocation_count>=i+1u && !memcmp(&graph,&saved,sizeof(graph)));
    }
    prime_need_receipt_graph_free(&graph);
    graph_retirement.group=group;
    pthread_t retire;
    assert(!pthread_create(&retire,NULL,graph_retire_reader,NULL));
    allocation_hook=graph_retire_during_collection;
    status=prime_need_receipt_graph_collect(&receipt,&graph);
    bool interposed=allocation_hook==NULL;
    if (!interposed) {allocation_hook=NULL;graph_retire_during_collection();}
    assert(!pthread_join(retire,NULL));
    assert(interposed && status==PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length==1u &&
        graph.event_count==1u && graph.recording_owner==group &&
        graph.nodes[0].event.after && graph.nodes[0].event.after->ground.ival==71 &&
        prime_need_receipt_graph_validate(&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    assert(!prime_need_receipt_present(&receipt) && prime_need_recording_status(group).nodes==1u);
    PrimeNeedReceiptGraph erased={0};
    assert(prime_need_receipt_graph_collect(&receipt,&erased)==PRIME_NEED_RECEIPT_GRAPH_ABSENT &&
        !erased.nodes && !erased.recording_owner);
    prime_need_receipt_graph_free(&erased);
    prime_need_receipt_graph_free(&graph);
    assert(!prime_need_recording_status(group).nodes);
    prime_need_recording_carrier_release(group);
    printf("PASS: concurrent recording retirement and %zu graph refusals preserve view lifetime\n",allocations);
}

static void check_state_import_allocation_refusal(Arena *arena) {
    StateCell a={.value=atom_int(arena,0)},b={.value=atom_int(arena,0)};
    PrimeNeedBranchState common={0},entrance={0},production={0},recipient={0},out={0};
    assert(prime_need_branch_state_begin(arena,&common));
    assert(prime_need_branch_state_write(arena,&common,&a,a.value,atom_int(arena,1),&entrance));
    assert(prime_need_branch_state_write(arena,&entrance,&b,b.value,atom_int(arena,7),&production));
    assert(prime_need_branch_state_write(arena,&common,&a,a.value,atom_int(arena,2),&recipient));
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    CettaMatchCompletion status=prime_need_branch_state_import_suffix(arena,&common,&entrance,
        &production,&recipient,state_selected_initial,NULL,&out);
    probing=false;size_t allocations=allocation_count;
    assert(status==CETTA_MATCH_MATCHED && allocations>4u);
    for(size_t i=0u;i<allocations;i++) {
        out=recipient;fail_at=i;allocation_count=0u;probing=true;
        status=prime_need_branch_state_import_suffix(arena,&common,&entrance,
            &production,&recipient,state_selected_initial,NULL,&out);
        probing=false;
        assert(status==CETTA_MATCH_INCOMPLETE && allocation_count>=i+1u &&
            out.top==recipient.top && out.session_id==recipient.session_id && out.owner==recipient.owner);
    }
    assert(prime_need_branch_state_import_suffix(arena,&common,&entrance,&production,&recipient,
        state_selected_initial,NULL,&out)==CETTA_MATCH_MATCHED);
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    status=prime_need_branch_state_same_writes_checked(&production,&out,&b);
    probing=false;size_t comparisons=allocation_count;
    assert(status==CETTA_MATCH_MATCHED && comparisons>0u);
    for(size_t i=0u;i<comparisons;i++) {
        fail_at=i;allocation_count=0u;probing=true;
        status=prime_need_branch_state_same_writes_checked(&production,&out,&b);
        probing=false;
        assert(status==CETTA_MATCH_INCOMPLETE && allocation_count>=i+1u);
    }
    printf("PASS: %zu State input comparison refusals remain incomplete\n",comparisons);
    Atom *value=NULL;uint64_t write_id=0u;
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    status=prime_need_branch_state_lookup_version_checked(&out,&b,&value,&write_id);
    probing=false;size_t inspections=allocation_count;
    assert(status==CETTA_MATCH_MATCHED && value && write_id && inspections>0u);
    for(size_t i=0u;i<inspections;i++) {
        value=a.value;write_id=UINT64_MAX;
        fail_at=i;allocation_count=0u;probing=true;
        status=prime_need_branch_state_lookup_version_checked(&out,&b,&value,&write_id);
        probing=false;
        assert(status==CETTA_MATCH_INCOMPLETE && allocation_count>=i+1u &&
            value==a.value && write_id==UINT64_MAX);
    }
    printf("PASS: %zu State version inspection refusals preserve both outputs\n",inspections);
    CettaWeightLedger empty={0},entry={0},produced={0},reader={0},result={0};
    assert(cetta_weight_ledger_append(arena,&empty,11u,21u,atom_int(arena,3),&entry));
    assert(cetta_weight_ledger_append(arena,&entry,12u,22u,atom_int(arena,7),&produced));
    assert(cetta_weight_ledger_append(arena,&empty,13u,23u,atom_int(arena,5),&reader));
    for(size_t i=0u;i<3u;i++) {
        result=reader;fail_at=i;allocation_count=0u;probing=true;
        status=cetta_weight_ledger_import_suffix_checked(arena,&entry,&produced,&reader,&result);
        probing=false;
        assert(status==CETTA_MATCH_INCOMPLETE && allocation_count>=i+1u &&
            result.last==reader.last && result.length==reader.length);
    }
    printf("PASS: %zu State and 3 weight import allocation refusals preserve destination\n",allocations);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    check_atom_projection_refusal();
    arena_init(&arena);
    check_capture_allocation_refusal(&arena);
    check_state_import_allocation_refusal(&arena);
    check_recording_graph_retirement(&arena);
    PrimeNeedReceipt receipt = {0};
    PrimeNeedReceiptGraph graph = {0};
    assert(collect_with_fault(&receipt, &graph, 0u) ==
           PRIME_NEED_RECEIPT_GRAPH_ABSENT);
    assert(allocation_count == 0u);
    assert(prime_need_receipt_begin(&arena, &receipt));
    assert(collect_with_fault(&receipt, &graph, 0u) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    assert(allocation_count == 0u && graph.session_id != 0u);
    assert(prime_need_receipt_use_equation_ids(
        &arena, &receipt, 1u, 1u, &receipt));
    assert(prime_need_receipt_graph_collect(&receipt, &graph) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    PrimeNeedReceiptGraph saved = graph;
    for (uint64_t i = 2u; i <= 80u; i++)
        assert(prime_need_receipt_use_equation_ids(
            &arena, &receipt, 1u, i, &receipt));
    PrimeNeedReceipt source = receipt;
    PrimeNeedReceiptGraph complete = {0};
    assert(collect_with_fault(&receipt, &complete, SIZE_MAX) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    size_t allocations = allocation_count;
    assert(allocations >= 4u && complete.length == 80u &&
           complete.event_count == 80u);
    for (size_t i = 0u; i < allocations; i++) {
        assert(collect_with_fault(&receipt, &graph, i) ==
               PRIME_NEED_RECEIPT_GRAPH_REFUSED);
        assert(allocation_count >= i + 1u);
        assert(graph.nodes == saved.nodes && graph.length == saved.length &&
               graph.event_count == saved.event_count &&
               graph.session_id == saved.session_id &&
               graph.root_id == saved.root_id);
        assert(receipt.top == source.top && receipt.owner == source.owner &&
               receipt.session_id == source.session_id);
        assert(prime_need_receipt_graph_validate(&graph) ==
               PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    }
    fail_at = 0u;
    allocation_count = 0u;
    probing = true;
    PrimeNeedReceiptGraphStatus status =
        prime_need_receipt_graph_validate(&complete);
    probing = false;
    assert(status == PRIME_NEED_RECEIPT_GRAPH_REFUSED && allocation_count == 1u);
    assert(complete.length == 80u && complete.event_count == 80u);
    PrimeNeedReceiptGraphNode producers[64] = {0};
    for (size_t i = 0u; i < 64u; i++)
        producers[i] = (PrimeNeedReceiptGraphNode){.has_event = true,
            .event = {.need_session_id = 3u, .thunk_id = 5u,
                .producer_update_id = i + 1u, .producer_completed = true}};
    PrimeNeedReceiptGraph producer_graph = {.nodes = producers, .length = 64u};
    bool retained[64] = {0}, untouched[64] = {0};
    fail_at = 0u;
    allocation_count = 0u;
    probing = true;
    bool indexed = prime_need_receipt_graph_producers(&producer_graph, retained);
    probing = false;
    assert(!indexed && allocation_count == 1u &&
           memcmp(retained, untouched, sizeof(retained)) == 0 &&
           producers[6].event.producer_update_id == 7u && producers[6].event.producer_completed);
    assert(prime_need_receipt_graph_producers(&producer_graph, retained));
    for (size_t i = 0u; i < 64u; i++) assert(retained[i]);
    assert(prime_need_receipt_graph_collect(&receipt, &graph) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 80u);
    prime_need_receipt_graph_free(&graph);
    prime_need_receipt_graph_free(&complete);
    check_reader_context_refusal();
    check_reader_copy_refusal();
    check_production_support_refusal(&arena);
    check_read_capture_transport();
    prime_need_receipt_graph_free(&graph);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: %zu graph allocation refusals preserve the old view and receipt\n",
           allocations);
    return 0;
}
