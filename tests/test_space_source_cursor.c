/* The production query/match machines reading an incremental byte source.
 * This fixture supplies only a read source; it is not a public disk Space
 * backend or a claim that arbitrary Space mutation is supported. */
#include "parser.h"
#include "petta_search_machine.h"
#include "library_pattern.h"
#include "native_disk_store.h"
#include "atom_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned promotion_failure;
bool __real_bindings_promote_logical_delta_to_arena(Bindings *, const Bindings *, Arena *);
bool __wrap_bindings_promote_logical_delta_to_arena(Bindings *result, const Bindings *base, Arena *arena) {
    unsigned failure=promotion_failure; promotion_failure=0;
    if(failure==1) return false;
    bool ready=__real_bindings_promote_logical_delta_to_arena(result,base,arena);
    return failure==2 ? false : ready;
}

typedef struct {
    const CettaDiskRoot *root;
    size_t count, opened, selected, decoded, live;
    bool fail_open, fail_clone, fail_next;
    const PettaMachine *measured_machine;
    size_t peak_machine_bytes;
} Source;

typedef struct {
    Source *owner;
    CettaDiskCursor *disk;
    CettaDiskBuffer key;
    Arena row;
    ArenaMark empty_row;
    Atom *atom, *identity;
    char prefix[2], upper[2];
    bool ranged, done;
    CettaIndex next, last;
} SourceCursor;

static Atom *parse(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int n = parse_metta_text(text, arena, &forms);
    assert(n == 1 && forms);
    Atom *result = forms[0]; free(forms); return result;
}

static CettaDiskBytes bytes(const char *text) {
    return (CettaDiskBytes){(const unsigned char *)text, strlen(text)};
}

static bool source_cursor_new(Source *owner, const CettaDiskRoot *root, Atom *pattern, void **out,
        SpaceOccurrenceCursorStep *failure) {
    SourceCursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) return false;
    cursor->owner = owner;
    arena_init_detached(&cursor->row);
    cursor->empty_row = arena_mark(&cursor->row);
    /* This fixture's key partition is complete for these p/q records. The
     * production adapter must also cover open stored constructors/fields. */
    if (pattern && pattern->kind == ATOM_EXPR && pattern->expr.len &&
        (atom_is_symbol(pattern->expr.elems[0], "p") || atom_is_symbol(pattern->expr.elems[0], "q"))) {
        cursor->ranged = true;
        cursor->prefix[0] = atom_is_symbol(pattern->expr.elems[0], "p") ? 'p' : 'q';
        cursor->upper[0] = cursor->prefix[0] + 1;
    }
    CettaDiskBytes lower = bytes(cursor->prefix), upper = bytes(cursor->upper);
    CettaDiskStatus status=cetta_disk_cursor_open_range(root, cursor->ranged ? &lower : NULL,
            false, cursor->ranged ? &upper : NULL, &cursor->disk);
    if (status != CETTA_DISK_OK) {
        *failure=status==CETTA_DISK_NOMEM ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
        arena_free(&cursor->row); free(cursor); return false;
    }
    owner->live++;
    *out = cursor;
    return true;
}

static bool source_open(Space *space, Atom *pattern, void **out, SpaceOccurrenceCursorStep *failure) {
    Source *source = space->match_backend.source;
    source->opened++;
    return !source->fail_open && source_cursor_new(source, source->root, pattern, out, failure);
}

static void source_free(void *raw) {
    SourceCursor *cursor = raw;
    if (!cursor) return;
    cursor->owner->live--;
    cetta_disk_cursor_close(cursor->disk);
    cetta_disk_buffer_free(&cursor->key);
    arena_free(&cursor->row);
    free(cursor);
}

static bool source_clone(const void *raw, bool rewind, void **out) {
    const SourceCursor *source = raw;
    if (source->owner->fail_clone) return false;
    SourceCursor *copy = calloc(1, sizeof(*copy));
    if (!copy) return false;
    copy->owner = source->owner; copy->ranged = source->ranged;
    memcpy(copy->prefix, source->prefix, sizeof(copy->prefix));
    memcpy(copy->upper, source->upper, sizeof(copy->upper));
    arena_init_detached(&copy->row);
    copy->empty_row = arena_mark(&copy->row);
    CettaDiskBytes lower = bytes(copy->prefix), upper = bytes(copy->upper);
    if (!rewind && source->key.data) lower = (CettaDiskBytes){source->key.data, source->key.size};
    if (cetta_disk_cursor_open_range(cetta_disk_cursor_root(source->disk),
            (!rewind && source->key.data) || copy->ranged ? &lower : NULL,
            !rewind && source->key.data, copy->ranged ? &upper : NULL, &copy->disk) != CETTA_DISK_OK) {
        arena_free(&copy->row); free(copy); return false;
    }
    copy->owner->live++;
    if (!rewind) {
        copy->next = source->next; copy->last = source->last; copy->done = source->done;
        if (source->key.data) {
            copy->key.data = malloc(source->key.size ? source->key.size : 1);
            if (!copy->key.data) { source_free(copy); return false; }
            memcpy(copy->key.data, source->key.data, source->key.size);
            copy->key.size = source->key.size;
        }
        if (source->atom) copy->atom = atom_deep_copy(&copy->row, source->atom);
        if (source->identity) copy->identity = atom_deep_copy(&copy->row, source->identity);
        if ((source->atom && !copy->atom) || (source->identity && !copy->identity)) {
            source_free(copy); return false;
        }
    }
    *out = copy;
    return true;
}

static bool source_select(const void *raw, Atom *pattern, void **out, SpaceOccurrenceCursorStep *failure) {
    const SourceCursor *source = raw;
    source->owner->selected++;
    return source_cursor_new(source->owner, cetta_disk_cursor_root(source->disk), pattern, out, failure);
}

static SpaceOccurrenceCursorStep source_next(void *raw, CettaIndex *index) {
    SourceCursor *cursor = raw;
    if (cursor->owner->measured_machine) {
        size_t live=petta_machine_retained_bytes(cursor->owner->measured_machine);
        if(live>cursor->owner->peak_machine_bytes) cursor->owner->peak_machine_bytes=live;
    }
    if (cursor->owner->fail_next) return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    if (cursor->done) return SPACE_OCCURRENCE_CURSOR_END;
    arena_reset(&cursor->row, cursor->empty_row);
    cursor->atom = cursor->identity = NULL;
    cetta_disk_buffer_free(&cursor->key);
    CettaDiskBlob blob;
    CettaDiskStatus status = cetta_disk_cursor_next(cursor->disk, &cursor->key, &blob);
    if (status == CETTA_DISK_END) { cursor->done = true; return SPACE_OCCURRENCE_CURSOR_END; }
    if (status != CETTA_DISK_OK) return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    if (blob.size > SIZE_MAX) return SPACE_OCCURRENCE_CURSOR_CAPACITY;
    unsigned char *body = malloc(blob.size ? (size_t)blob.size : 1);
    if (!body) return SPACE_OCCURRENCE_CURSOR_CAPACITY;
    size_t got = 0;
    status = cetta_disk_blob_read(cetta_disk_cursor_root(cursor->disk), blob, 0,
        body, (size_t)blob.size, &got);
    bool ok = status == CETTA_DISK_OK && got == blob.size &&
        cetta_atom_wire_decode(&cursor->row, body, got, &cursor->atom) == ATOM_WIRE_OK;
    free(body);
    if (!ok) return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    cursor->identity = atom_string_n(&cursor->row, (const char *)cursor->key.data, cursor->key.size);
    cursor->owner->decoded++;
    *index = cursor->last = cursor->next++;
    return SPACE_OCCURRENCE_CURSOR_ITEM;
}

static Atom *source_atom(const void *raw, CettaIndex index) {
    const SourceCursor *cursor = raw;
    return index == cursor->last ? cursor->atom : NULL;
}
static Atom *source_identity(const void *raw, CettaIndex index) {
    const SourceCursor *cursor = raw;
    return index == cursor->last ? cursor->identity : NULL;
}
static uint64_t source_length(const Space *space) {
    return ((Source *)space->match_backend.source)->count;
}
static void source_storage(const void *raw, CettaStorageCensus *census) {
    const SourceCursor *cursor=raw;
    if(!cetta_storage_object(census,cursor,sizeof(*cursor),CETTA_STORAGE_BLOCK)) return;
    cetta_disk_cursor_storage_census(cursor->disk,census);
    cetta_storage_object(census,cursor->key.data,cursor->key.size ? cursor->key.size : 1,CETTA_STORAGE_BLOCK);
    arena_storage_census(&cursor->row,census);
}
static const SpaceSourceCursorOps source_ops = {
    .clone=source_clone, .select=source_select, .next=source_next,
    .atom=source_atom, .identity=source_identity, .free=source_free, .storage=source_storage,
};
static void source_owner_storage(const Space *space, CettaStorageCensus *census) {
    /* Source is fixture-owned on the stack. Its snapshot descriptor is
     * borrowed; open cursors own their fd duplicates and heap traversal. */
    cetta_storage_object(census,space->match_backend.source,0,CETTA_STORAGE_VISITED);
}
static const SpaceMatchBackendOps backend_ops = {
    .name="test-encoded-source", .occurrence_ops=&source_ops,
    .occurrence_open=source_open, .logical_len=source_length, .storage=source_owner_storage,
};

static size_t collect(SpaceQueryCursor *cursor, Arena *arena, Atom *projection, Atom **out) {
    Bindings bindings; bindings_init(&bindings);
    SpaceQueryCursorStep step;
    size_t count = 0, polls = 0;
    do {
        step = space_query_cursor_step_with_budget(cursor, 1, &bindings);
        if (step == SPACE_QUERY_CURSOR_ROW) {
            assert(count < 32);
            out[count++] = bindings_apply_graph(&bindings, arena, projection);
            bindings_free(&bindings); bindings_init(&bindings);
        }
        assert(++polls < 10000);
    } while (step == SPACE_QUERY_CURSOR_ROW || step == SPACE_QUERY_CURSOR_PROGRESS);
    assert(step == SPACE_QUERY_CURSOR_END);
    bindings_free(&bindings);
    return count;
}

typedef struct { const char **expected; size_t size, seen; } IdentityCheck;
static bool count_identity(SpaceReadToken read, Atom *identity, void *raw) {
    assert(read.space && identity->kind == ATOM_GROUNDED && identity->ground.gkind == GV_STRING);
    IdentityCheck *check=raw;
    assert(check->seen<check->size && strcmp(identity->ground.sval,check->expected[check->seen])==0);
    check->seen++;
    return true;
}
static bool forbidden_position(SpaceReadToken read, CettaIndex index, void *raw) {
    (void)read; (void)index; (void)raw; assert(!"non-positional source passed to positional visitor"); return false;
}

static PettaMachineHostMode classify(void *context, Space *space, Atom *call) {
    (void)context; (void)space;
    return atom_head_symbol_id(call) == g_builtin_syms.pat_query
        ? PETTA_MACHINE_HOST_PATTERN_QUERY : PETTA_MACHINE_HOST_NONE;
}
static Space *resolve(void *context, Space *space, Arena *arena, Atom *reference) {
    (void)arena;
    if(atom_is_symbol(reference,"&db")) return context;
    return atom_is_symbol(reference, "&self") ? space : NULL;
}

static CettaBranchAdmission admit(void *context, Space *space) {
    (void)context; (void)space;
    return (CettaBranchAdmission){.status=CETTA_BRANCH_ADMISSION_AVAILABLE,
        .capacity=CETTA_BRANCH_CAPTURE_MULTI_SHOT,
        .authority={.words={0x736f75726365},.length=1}};
}

static PettaMachineStep next_answer(PettaMachine *machine, Atom **answer) {
    Bindings environment;
    PettaMachineStep step = petta_machine_next(machine, answer, &environment);
    bindings_free(&environment);
    return step;
}

static void need_product_sharing(Space *source) {
    for(size_t cut=0;cut<16;cut++) {
        Arena input, output; arena_init_detached(&input); arena_init_detached(&output);
        Atom *pattern=parse(&input,"(p $x $y)");
        Atom *value=atom_expr2(&input,atom_symbol(&input,"NeedValue"),pattern->expr.elems[1]);
        PrimeNeedSnapshot root={0},allocated,running,completed; uint64_t cell=0;
        assert(prime_need_snapshot_begin(&root));
        assert(prime_need_snapshot_allocate(&input,&root,value,&allocated,&cell));
        assert(prime_need_snapshot_start_evaluation(&input,&allocated,cell,83,&running));
        assert(prime_need_snapshot_resolve_value(&input,&running,cell,83,value,&completed));
        Bindings seed,row,copied_seed;
        bindings_init(&seed); bindings_init(&row); bindings_init(&copied_seed);
        assert(bindings_add_var(&seed,pattern->expr.elems[1],atom_int(&input,1)));
        assert(bindings_add_id(&seed,var_id_from_base(984404),symbol_intern_cstr(g_symbols,"seed-origin"),value));
        *bindings_need_mut(&seed)=completed;
        SpaceQueryCursor *cursor=space_query_cursor_new_captured(source,&input,pattern,&seed); assert(cursor);
        for(size_t i=0;i<cut;i++) {
            SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1,&row);
            assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
            if(step==SPACE_QUERY_CURSOR_ROW) assert(bindings_need_view(&row)->top==completed.top);
            bindings_free(&row); bindings_init(&row);
        }
        SpaceQueryCursor *reference=space_query_cursor_clone(cursor); assert(reference);
        SpaceQueryCursorStep step;
        do { step=space_query_cursor_step(reference,&row); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
        assert(step==SPACE_QUERY_CURSOR_ROW && bindings_need_view(&row)->top==completed.top);
        PrimeNeedCellView observed;
        assert(prime_need_snapshot_lookup(bindings_need_view(&row),cell,&observed));
        assert(bindings_lookup_value_id(&row,var_id_from_base(984404)).skeleton==observed.cached);
        assert(atom_eq(bindings_apply_graph(&row,&input,observed.cached),parse(&input,"(NeedValue 1)")));
        space_query_cursor_free(reference); bindings_free(&row); bindings_init(&row);
        AtomDeepCopySession *copy=atom_deep_copy_session_new(&output);
        AtomDeepCopyArenaPair pair={&input,&output};
        assert(copy && atom_deep_copy_session_set_arena_map(copy,&pair,1));
        assert(bindings_clone(&copied_seed,&seed));
        assert(bindings_copy_owned_atoms_with_session(&copied_seed,&output,copy));
        SpaceQueryCursor *relocated=space_query_cursor_copy_to_arena(cursor,&output,copy); assert(relocated);
        atom_deep_copy_session_free(copy); space_query_cursor_free(cursor);
        bindings_free(&seed); arena_free(&input);
        do { step=space_query_cursor_step(relocated,&row); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
        assert(step==SPACE_QUERY_CURSOR_ROW && bindings_need_view(&row)->top==bindings_need_view(&copied_seed)->top);
        assert(prime_need_snapshot_lookup(bindings_need_view(&row),cell,&observed));
        assert(bindings_lookup_value_id(&row,var_id_from_base(984404)).skeleton==observed.cached);
        assert(atom_eq(bindings_apply_graph(&row,&output,observed.cached),parse(&output,"(NeedValue 1)")));
        bindings_free(&row); bindings_free(&copied_seed); space_query_cursor_free(relocated); arena_free(&output);
    }
    puts("PASS: source matching preserves the caller's Need product across clones and paired relocation");
}

typedef struct { VarId id; Atom *key; bool found; } NamedBindingCheck;
static bool named_binding_check(const Bindings *bindings, NamedBindingCheck *check) {
    BindingsIterator iterator={.bindings=bindings}; Binding binding;
    while(bindings_iterator_next(&iterator,&binding)) {
        if(var_id_eq(binding.var_id,check->id)) {
            assert(binding.name_key==check->key); check->found=true;
        }
    }
    return check->found;
}

static void framed_alias_sharing(Space *source) {
    for(unsigned mode=0;mode<3;mode++) {
        Arena external,input,output;
        arena_init_detached(&external); arena_init_detached(&input); arena_init_detached(&output);
        Atom *retained=parse(&external,"(borrowed-object 17)");
        Atom *name_key=parse(&external,"(structural-key borrowed)");
        CettaFrameIdentityScope scope={0};
        CettaFrameIdentity epoch=cetta_frame_identity_scope_fresh(&scope);
        VarId ids[]={var_id_from_base(1),var_id_from_base(2)};
        VarId bound=var_epoch_id(ids[0],epoch), fresh=var_epoch_id(ids[1],epoch);
        Bindings seed,row; bindings_init(&seed); bindings_init(&row);
        assert(bindings_register_contextual_frame(&seed,ids,mode==2 ? 1u : 2u,epoch));
        BindingsBuilder builder; assert(bindings_builder_init(&builder,&seed));
        assert(bindings_builder_add_var_fresh(&builder,atom_var_with_name_key(&input,name_key,bound),retained));
        bindings_free(&seed); bindings_builder_take(&builder,&seed); bindings_builder_free(&builder);
        Atom *variable=mode ? atom_var_with_id(&input,"new-slot",fresh)
            : atom_var_with_id(&input,"answer",fresh_var_id());
        Atom *pattern=atom_expr3(&input,atom_symbol(&input,"p"),atom_int(&input,1),variable);
        SpaceQueryCursor *cursor=space_query_cursor_new_captured(source,&input,pattern,&seed); assert(cursor);
        SpaceQueryCursorStep step;
        do { step=space_query_cursor_step(cursor,&row); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
        assert(step==SPACE_QUERY_CURSOR_ROW && bindings_lookup_value_id(&row,bound).skeleton==retained);
        NamedBindingCheck names={bound,name_key,false};
        assert(named_binding_check(&row,&names));
        bindings_free(&row); bindings_init(&row);
        AtomDeepCopySession *copy=atom_deep_copy_session_new(&output);
        AtomDeepCopyArenaPair pairs[]={{&input,&output},{&external,&external}};
        assert(copy && atom_deep_copy_session_set_arena_map(copy,pairs,2));
        SpaceQueryCursor *relocated=space_query_cursor_copy_to_arena(cursor,&output,copy); assert(relocated);
        atom_deep_copy_session_free(copy); space_query_cursor_free(cursor); bindings_free(&seed); arena_free(&input);
        do { step=space_query_cursor_step(relocated,&row); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
        assert(step==SPACE_QUERY_CURSOR_ROW && bindings_lookup_value_id(&row,bound).skeleton==retained);
        names.found=false;
        assert(named_binding_check(&row,&names));
        space_query_cursor_free(relocated); bindings_free(&row); arena_free(&output); arena_free(&external);
        cetta_frame_identity_scope_clear(&scope);
    }
    puts("PASS: unchanged, updated and extended binding frames retain external seed aliases");
}

static void shifted_constraint_sharing(void) {
    Arena external,scratch,answers;
    arena_init_detached(&external); arena_init_detached(&scratch); arena_init_detached(&answers);
    Atom *left=parse(&external,"(box $x)"), *right=parse(&external,"(box 1)");
    Atom *survivor_left=parse(&external,"(pending $u)"), *survivor_right=parse(&external,"(pending $v)");
    Bindings seed,row; bindings_init(&seed); bindings_init(&row);
    assert(bindings_add_constraint(&seed,left,right));
    assert(bindings_add_constraint(&seed,survivor_left,survivor_right) && seed.eq_len==2);
    assert(bindings_clone(&row,&seed) && bindings_promote_logical_atoms_to_arena(&row,&scratch));
    assert(bindings_add_var(&row,left->expr.elems[1],atom_int(&scratch,1)));
    assert(row.eq_len==1 && seed.eq_len==2);
    assert(bindings_promote_logical_delta_to_arena(&row,&seed,&answers));
    arena_free(&scratch);
    assert(row.constraints[0].lhs.skeleton==seed.constraints[1].lhs.skeleton &&
           row.constraints[0].rhs.skeleton==seed.constraints[1].rhs.skeleton);
    bindings_free(&row); bindings_free(&seed);
    arena_free(&answers); arena_free(&external);
    puts("PASS: discharged prefix constraints preserve surviving external aliases");
}

static void rejected_row_storage(CettaDiskStore *owner, CettaDiskStoreView *base,
        TermUniverse *universe) {
    enum { ROWS=256, PAYLOAD=4096 };
    Arena values; arena_init_detached(&values);
    char payload[PAYLOAD]; memset(payload,'x',sizeof(payload));
    Atom *large=atom_string_n(&values,payload,sizeof(payload));
    CettaDiskOp ops[ROWS]; unsigned char *bodies[ROWS]; char keys[ROWS][24];
    for(size_t i=0;i<ROWS;i++) {
        snprintf(keys[i],sizeof(keys[i]),"r/%06zu",i);
        Atom *value=atom_expr3(&values,atom_symbol(&values,"blob"),atom_int(&values,(int64_t)i),large);
        size_t bytes_count=0;
        assert(cetta_atom_wire_encode(value,&bodies[i],&bytes_count)==ATOM_WIRE_OK);
        ops[i]=(CettaDiskOp){false,bytes(keys[i]),{bodies[i],bytes_count}};
    }
    CettaDiskStoreView *snapshot=NULL;
    assert(cetta_disk_store_commit(owner,base,ops,ROWS,&snapshot)==CETTA_STORE_OK);
    for(size_t i=0;i<ROWS;i++) free(bodies[i]);
    arena_free(&values);
    Source input={.root=cetta_disk_store_view_root(snapshot),.count=ROWS+6};
    Space source; space_init_with_universe(&source,universe);
    source.match_backend.kind=SPACE_ENGINE_SOURCE; source.match_backend.ops=&backend_ops;
    source.match_backend.source=&input;
    Arena answers; arena_init_detached(&answers);
    Atom *pattern=parse(&answers,"(blob -1 $data)");
    size_t before=arena_accounted_live_bytes(&answers),peak=before;
    SpaceQueryCursor *cursor=space_query_cursor_new_captured(&source,&answers,pattern,NULL); assert(cursor);
    Bindings row; bindings_init(&row); SpaceQueryCursorStep step;
    do {
        step=space_query_cursor_step_with_budget(cursor,1,&row);
        size_t current=arena_accounted_live_bytes(&answers); if(current>peak) peak=current;
        assert(step!=SPACE_QUERY_CURSOR_ROW);
    } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_END && input.decoded==ROWS+6);
    assert(peak-before<65536);
    space_query_cursor_free(cursor); bindings_free(&row); assert(input.live==0);
    printf("PASS: %u rejected %u-byte rows retain %zu result-arena bytes\n",ROWS,PAYLOAD,peak-before);
    PettaMachineHost host={.context=&source,.classify=classify,.resolve_space=resolve};
    PettaMachine machine;
    assert(petta_machine_init(&machine,&source,&answers,
        parse(&answers,"(match &self (blob -1 $data) $data)"),NULL,&host));
    input.measured_machine=&machine;
    before=petta_machine_retained_bytes(&machine); input.peak_machine_bytes=before;
    Atom *answer=NULL;
    assert(next_answer(&machine,&answer)==PETTA_MACHINE_STEP_EXHAUSTED);
    assert(input.peak_machine_bytes-before<262144);
    printf("PASS: rejected-row ordinary match peaks %zu bytes above its initial heap\n",input.peak_machine_bytes-before);
    petta_machine_destroy(&machine); input.measured_machine=NULL; assert(input.live==0);
    arena_free(&answers); space_free(&source); cetta_disk_store_view_close(snapshot);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols=&symbols;
    Arena persistent, answers; arena_init_detached(&persistent); arena_init_detached(&answers);
    TermUniverse universe; term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe,&persistent);
    Space source, ordinary;
    space_init_with_universe(&source,&universe); space_init_with_universe(&ordinary,&universe);
    CettaDiskStore *owner = NULL;
    CettaDiskStoreView *empty = NULL, *view = NULL;
    assert(cetta_disk_store_open(argv[1], true, NULL, &owner) == CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(owner, &empty) == CETTA_STORE_OK);
    const char *keys[] = {"p/a", "p/b", "p/c", "p/d", "q/a", "q/b"};
    const char *texts[] = {"(p 1 1)", "(p 1 2)", "(p 1 1)", "(p $s $s)", "(q 1 z)", "(q 2 z)"};
    CettaDiskOp ops[6]; unsigned char *encoded[6];
    for (size_t i=0; i<6; i++) {
        Atom *value=parse(&persistent,texts[i]); space_add(&ordinary,value);
        size_t length=0;
        assert(cetta_atom_wire_encode(value,&encoded[i],&length)==ATOM_WIRE_OK);
        ops[i]=(CettaDiskOp){false,bytes(keys[i]),{encoded[i],length}};
    }
    assert(cetta_disk_store_commit(owner,empty,ops,6,&view)==CETTA_STORE_OK);
    for (size_t i=0;i<6;i++) free(encoded[i]);
    cetta_disk_store_view_close(empty);
    Source input={.root=cetta_disk_store_view_root(view),.count=6};
    source.match_backend.kind=SPACE_ENGINE_SOURCE;
    source.match_backend.ops=&backend_ops;
    source.match_backend.source=&input;

    Atom *query=parse(&answers,"(, (p 1 $y) (q $y z))");
    Atom *projection=query->expr.elems[1]->expr.elems[2];
    Atom *expected[32], *actual[32];
    SpaceQueryCursor *reference=space_query_cursor_new_captured(&ordinary,&answers,query,NULL);
    size_t wanted=collect(reference,&answers,projection,expected); space_query_cursor_free(reference);
    assert(wanted==4);
    SpaceQueryCursor *cursor=space_query_cursor_new_captured(&source,&answers,query,NULL);
    assert(cursor && space_query_cursor_incremental(cursor) && input.decoded==0);
    Bindings first; bindings_init(&first);
    SpaceQueryCursorStep step;
    do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_ROW && input.decoded==2 && input.selected==2);
    CettaStorageCensus census={0};
    space_query_cursor_storage_census(cursor,&census); cetta_storage_finish(&census);
    if(census.incomplete) fprintf(stderr,"source census refusal: %s\n",census.incomplete);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    assert(census.bytes>=2*sizeof(SourceCursor));
    size_t retained=census.bytes;
    space_query_cursor_storage_census(cursor,&census); cetta_storage_finish(&census);
    assert(census.bytes==retained);
    cetta_storage_census_free(&census);
    const char *first_ids[]={"p/a","q/a"}; IdentityCheck witnesses={first_ids,2,0};
    assert(space_query_cursor_visit_occurrences(cursor,count_identity,&witnesses) && witnesses.seen==2);
    assert(!space_query_cursor_visit_witnesses(cursor,forbidden_position,NULL));
    Atom *held=bindings_apply_graph(&first,&answers,projection);
    bindings_free(&first);
    SpaceQueryCursor *clone=space_query_cursor_clone(cursor); assert(clone);
    /* Failed clone admission leaves both the live source and its readers
     * unchanged, and the original remains resumable. */
    size_t live=input.live;
    input.fail_clone=true;
    assert(!space_query_cursor_clone(cursor) && input.live==live);
    input.fail_clone=false;
    CettaDiskStoreView *updated=NULL;
    unsigned char *body=NULL; size_t length=0;
    assert(cetta_atom_wire_encode(parse(&persistent,"(q 1 newer)"),&body,&length)==ATOM_WIRE_OK);
    CettaDiskOp update={false,bytes("q/a"),{body,length}};
    assert(cetta_disk_store_commit(owner,view,&update,1,&updated)==CETTA_STORE_OK); free(body);
    /* New open requests now see the new publisher revision, but both active
     * joins must keep selecting later premises from their captured root. */
    input.root=cetta_disk_store_view_root(updated);
    size_t n=collect(cursor,&answers,projection,actual);
    assert(n+1==wanted && atom_eq(held,expected[0]));
    space_query_cursor_free(cursor);
    for(size_t i=0;i<n;i++) assert(atom_eq(actual[i],expected[i+1]));
    assert(collect(clone,&answers,projection,actual)==n); space_query_cursor_free(clone);
    assert(input.live==0 && atom_eq(held,parse(&answers,"1")));
    input.root=cetta_disk_store_view_root(view);

    /* Relocate paused matcher state and completed occurrence witnesses, then
     * retire the original query arena. The external snapshot is still held. */
    for (size_t cut=0;cut<20;cut++) {
        Arena input_arena, output_arena;
        arena_init_detached(&input_arena); arena_init_detached(&output_arena);
        Atom *pattern=parse(&input_arena,"(, (p 1 $y) (q $y z))");
        Atom *value=pattern->expr.elems[1]->expr.elems[2];
        SpaceQueryCursor *before=space_query_cursor_new_captured(&source,&input_arena,pattern,NULL);
        assert(before); Bindings row; bindings_init(&row); size_t published=0;
        for(size_t i=0;i<cut;i++) {
            SpaceQueryCursorStep status=space_query_cursor_step_with_budget(before,1,&row);
            assert(status==SPACE_QUERY_CURSOR_PROGRESS || status==SPACE_QUERY_CURSOR_ROW);
            if(status==SPACE_QUERY_CURSOR_ROW) published++;
            bindings_free(&row); bindings_init(&row);
        }
        AtomDeepCopySession *atoms=atom_deep_copy_session_new(&output_arena);
        AtomDeepCopyArenaPair mapping={&input_arena,&output_arena};
        assert(atoms && atom_deep_copy_session_set_arena_map(atoms,&mapping,1));
        Atom *copied_value=atom_deep_copy_session_copy(atoms,value);
        size_t decoded=input.decoded;
        SpaceQueryCursor *after=space_query_cursor_copy_to_arena(before,&output_arena,atoms);
        assert(after && copied_value && input.decoded==decoded);
        atom_deep_copy_session_free(atoms); space_query_cursor_free(before); arena_free(&input_arena);
        assert(collect(after,&output_arena,copied_value,actual)+published==wanted);
        space_query_cursor_free(after); bindings_free(&row); arena_free(&output_arena);
        assert(input.live==0);
    }

    /* Ordinary language match and directional pattern queries use the same
     * occurrence source, including stable identities and open stored terms. */
    PettaMachineHost host={.context=&source,.classify=classify,.resolve_space=resolve,
        .admit_branch_capture=admit,.retain_query_variable_identity=true};
    const char *calls[]={"(match &self (p 1 $y) $y)","(pat:query &self match% (p 1 $y))"};
    const size_t counts[]={4,3};
    for(size_t i=0;i<2;i++) {
        PettaMachine machine; assert(petta_machine_init(&machine,&source,&answers,parse(&answers,calls[i]),NULL,&host));
        Atom *answer=NULL; size_t count=0; PettaMachineStep result;
        CettaOwnedContinuation saved; cetta_owned_continuation_init(&saved);
        CettaContinuationMachine continuation=petta_machine_continuation_machine(&machine);
        while((result=next_answer(&machine,&answer))==PETTA_MACHINE_STEP_ANSWER) {
            if(i==1) {
                assert(count<3);
                Atom *identity=answer->expr.elems[1]->expr.elems[0]->expr.elems[1]->expr.elems[3];
                assert(identity->kind==ATOM_GROUNDED && identity->ground.gkind==GV_STRING);
                assert(strcmp(identity->ground.sval,keys[count])==0);
            }
            count++;
            if(count==1) assert(cetta_continuation_capture(continuation,&saved)==CETTA_CONTINUATION_READY);
        }
        assert(result==PETTA_MACHINE_STEP_EXHAUSTED && count==counts[i]);
        assert(cetta_continuation_restore(continuation,&saved)==CETTA_CONTINUATION_READY);
        count=0;
        while((result=next_answer(&machine,&answer))==PETTA_MACHINE_STEP_ANSWER) count++;
        assert(result==PETTA_MACHINE_STEP_EXHAUSTED && count==counts[i]-1);
        cetta_owned_continuation_destroy(&saved);
        petta_machine_destroy(&machine); assert(input.live==0);
    }

    PettaMachine counted;
    assert(petta_machine_init(&counted,&source,&answers,
        parse(&answers,"(size-atom (collapse (match &self (p 1 $y) $y)))"),NULL,&host));
    Atom *count_answer=NULL;
    assert(next_answer(&counted,&count_answer)==PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(count_answer,parse(&answers,"4")));
    petta_machine_destroy(&counted); assert(input.live==0);

    HashConsTable hc; hashcons_init(&hc);
    HashConsTable *previous_hc=g_hashcons; g_hashcons=&hc;
    size_t interned=hc.used;
    SpaceOccurrenceCursor raw_cursor; space_occurrence_cursor_init_empty(&raw_cursor);
    assert(space_occurrence_cursor_init_scan(&source,&raw_cursor));
    CettaIndex raw_index; size_t scanned=0;
    while(space_occurrence_cursor_next(&raw_cursor,&raw_index,NULL)==SPACE_OCCURRENCE_CURSOR_ITEM) scanned++;
    space_occurrence_cursor_release(&raw_cursor);
    assert(scanned==6 && hc.used==interned);
    g_hashcons=previous_hc; hashcons_free(&hc);

    space_match_backend_clear_error();
    uint64_t revision=space_revision(&source);
    space_add(&source,parse(&persistent,"(p ignored mutation)"));
    assert(source.native.len==0 && space_revision(&source)==revision);
    assert(space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_READ_ONLY);
    assert(!space_add_dependency(&ordinary,&source));
    assert(!space_add_dependency(&source,&ordinary));
    space_match_backend_clear_error();

    /* Unsupported composite views refuse before any positional fallback can
     * certify the source's empty native shadow as a complete answer. */
    source.overlay_base=&ordinary;
    cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
    bindings_init(&first);
    assert(space_query_cursor_step(cursor,&first)==SPACE_QUERY_CURSOR_INCOMPLETE);
    space_query_cursor_free(cursor); bindings_free(&first);
    PettaMachine composite;
    assert(petta_machine_init(&composite,&source,&answers,parse(&answers,calls[0]),NULL,&host));
    Atom *composite_answer=NULL;
    assert(next_answer(&composite,&composite_answer)==PETTA_MACHINE_STEP_HOST_ERROR);
    petta_machine_destroy(&composite); source.overlay_base=NULL;

    input.fail_open=true;
    cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
    do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_CAPACITY); space_query_cursor_free(cursor); input.fail_open=false;
    input.fail_next=true;
    cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
    do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_INCOMPLETE);
    input.fail_next=false;
    assert(space_query_cursor_step(cursor,&first)==SPACE_QUERY_CURSOR_INCOMPLETE);
    space_query_cursor_free(cursor); assert(input.live==0);

    /* A failure after a published prefix is still incomplete, not END. A
     * saved failed continuation cannot reinterpret a later successful read
     * as permission to restart or silently finish the original query. */
    Atom *one_pattern=parse(&answers,"(p 1 $y)");
    cursor=space_query_cursor_new_captured(&source,&answers,one_pattern,NULL); assert(cursor);
    bindings_init(&first);
    do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_ROW); bindings_free(&first); bindings_init(&first);
    input.fail_next=true;
    do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_INCOMPLETE);
    clone=space_query_cursor_clone(cursor); assert(clone);
    input.fail_next=false;
    assert(space_query_cursor_step(clone,&first)==SPACE_QUERY_CURSOR_INCOMPLETE);
    space_query_cursor_free(cursor); space_query_cursor_free(clone); bindings_free(&first);
    assert(input.live==0);

    for(size_t i=0;i<2;i++) {
        input.fail_open=i==0; input.fail_next=i==1;
        PettaMachine machine;
        assert(petta_machine_init(&machine,&source,&answers,parse(&answers,calls[0]),NULL,&host));
        Atom *answer=NULL;
        PettaMachineStep result=next_answer(&machine,&answer);
        assert(result==(i==0 ? PETTA_MACHINE_STEP_CAPACITY : PETTA_MACHINE_STEP_HOST_ERROR));
        petta_machine_destroy(&machine); assert(input.live==0);
        input.fail_open=input.fail_next=false;
    }

    space_add(&ordinary,parse(&persistent,"(= (source-read $x) (match &db (p $x $y) $y))"));
    space_add(&ordinary,parse(&persistent,"(= (source-read $x) hidden-fallback)"));
    input.fail_next=true;
    PettaMachine dispatched;
    assert(petta_machine_init(&dispatched,&ordinary,&answers,
        parse(&answers,"(let $f source-read ($f 1))"),NULL,&host));
    Atom *dispatched_answer=NULL;
    assert(next_answer(&dispatched,&dispatched_answer)==PETTA_MACHINE_STEP_HOST_ERROR);
    petta_machine_destroy(&dispatched); input.fail_next=false; assert(input.live==0);

    /* The snapshot's root and row identities remain stable after publication. */
    cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
    assert(collect(cursor,&answers,projection,actual)==wanted); space_query_cursor_free(cursor);
    rejected_row_storage(owner,updated,&universe);
    need_product_sharing(&source); assert(input.live==0);
    framed_alias_sharing(&source); assert(input.live==0);
    shifted_constraint_sharing(); assert(input.live==0);
    for(unsigned failure=1;failure<=2;failure++) {
        promotion_failure=failure;
        cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
        bindings_init(&first);
        do { step=space_query_cursor_step(cursor,&first); } while(step==SPACE_QUERY_CURSOR_PROGRESS);
        assert(step==SPACE_QUERY_CURSOR_CAPACITY && !promotion_failure);
        clone=space_query_cursor_clone(cursor); assert(clone);
        Arena relocated_arena; arena_init_detached(&relocated_arena);
        AtomDeepCopySession *copy=atom_deep_copy_session_new(&relocated_arena);
        AtomDeepCopyArenaPair pair={&answers,&relocated_arena};
        assert(copy && atom_deep_copy_session_set_arena_map(copy,&pair,1));
        SpaceQueryCursor *relocated=space_query_cursor_copy_to_arena(cursor,&relocated_arena,copy); assert(relocated);
        atom_deep_copy_session_free(copy); space_query_cursor_free(cursor);
        assert(space_query_cursor_step(clone,&first)==SPACE_QUERY_CURSOR_CAPACITY);
        assert(space_query_cursor_step(relocated,&first)==SPACE_QUERY_CURSOR_CAPACITY);
        space_query_cursor_free(clone); space_query_cursor_free(relocated); arena_free(&relocated_arena);
        bindings_free(&first); assert(input.live==0);
    }
    cetta_disk_store_view_close(updated); cetta_disk_store_view_close(view); cetta_disk_store_close(owner);
    space_free(&source); space_free(&ordinary); term_universe_free(&universe);
    arena_free(&answers); arena_free(&persistent); symbol_table_free(&symbols);
    puts("PASS: shared Space cursors and language matching stream encoded occurrences with owned rows, identities, clones and explicit failure");
    return 0;
}
