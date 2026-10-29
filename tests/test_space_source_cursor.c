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

typedef struct {
    const CettaDiskRoot *root;
    size_t count, opened, selected, decoded, live;
    bool fail_open, fail_clone, fail_next;
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

static bool source_cursor_new(Source *owner, Atom *pattern, void **out) {
    SourceCursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) return false;
    cursor->owner = owner;
    arena_init(&cursor->row);
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
    if (cetta_disk_cursor_open_range(owner->root, cursor->ranged ? &lower : NULL,
            false, cursor->ranged ? &upper : NULL, &cursor->disk) != CETTA_DISK_OK) {
        arena_free(&cursor->row); free(cursor); return false;
    }
    owner->live++;
    *out = cursor;
    return true;
}

static bool source_open(Space *space, Atom *pattern, void **out) {
    Source *source = space->match_backend.source;
    source->opened++;
    return !source->fail_open && source_cursor_new(source, pattern, out);
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
    arena_init(&copy->row);
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
    }
    *out = copy;
    return true;
}

static bool source_select(const void *raw, Atom *pattern, void **out) {
    const SourceCursor *source = raw;
    source->owner->selected++;
    return source_cursor_new(source->owner, pattern, out);
}

static SpaceOccurrenceCursorStep source_next(void *raw, CettaIndex *index) {
    SourceCursor *cursor = raw;
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
static const SpaceSourceCursorOps source_ops = {
    .clone=source_clone, .select=source_select, .next=source_next,
    .atom=source_atom, .identity=source_identity, .free=source_free,
};
static const SpaceMatchBackendOps backend_ops = {
    .name="test-encoded-source", .occurrence_ops=&source_ops,
    .occurrence_open=source_open, .logical_len=source_length,
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
    (void)context; (void)arena;
    return atom_is_symbol(reference, "&self") ? space : NULL;
}

static PettaMachineStep next_answer(PettaMachine *machine, Atom **answer) {
    Bindings environment;
    PettaMachineStep step = petta_machine_next(machine, answer, &environment);
    bindings_free(&environment);
    return step;
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
    assert(cetta_atom_wire_encode(parse(&persistent,"(p 9 9)"),&body,&length)==ATOM_WIRE_OK);
    CettaDiskOp update={false,bytes("p/a"),{body,length}};
    assert(cetta_disk_store_commit(owner,view,&update,1,&updated)==CETTA_STORE_OK); free(body);
    size_t n=collect(cursor,&answers,projection,actual);
    assert(n+1==wanted && atom_eq(held,expected[0]));
    space_query_cursor_free(cursor);
    for(size_t i=0;i<n;i++) assert(atom_eq(actual[i],expected[i+1]));
    assert(collect(clone,&answers,projection,actual)==n); space_query_cursor_free(clone);
    assert(input.live==0 && atom_eq(held,parse(&answers,"1")));

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
    PettaMachineHost host={.classify=classify,.resolve_space=resolve};
    const char *calls[]={"(match &self (p 1 $y) $y)","(pat:query &self match% (p 1 $y))"};
    const size_t counts[]={4,3};
    for(size_t i=0;i<2;i++) {
        PettaMachine machine; assert(petta_machine_init(&machine,&source,&answers,parse(&answers,calls[i]),NULL,&host));
        Atom *answer=NULL; size_t count=0; PettaMachineStep result;
        while((result=next_answer(&machine,&answer))==PETTA_MACHINE_STEP_ANSWER) {
            if(i==1) {
                assert(count<3);
                Atom *identity=answer->expr.elems[1]->expr.elems[0]->expr.elems[1]->expr.elems[3];
                assert(identity->kind==ATOM_GROUNDED && identity->ground.gkind==GV_STRING);
                assert(strcmp(identity->ground.sval,keys[count])==0);
            }
            count++;
        }
        assert(result==PETTA_MACHINE_STEP_EXHAUSTED && count==counts[i]);
        petta_machine_destroy(&machine); assert(input.live==0);
    }

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
        if(i==0) assert(result==PETTA_MACHINE_STEP_CAPACITY);
        else {
            /* Ordinary unhandled PeTTa exceptions publish an Error value. */
            assert(result==PETTA_MACHINE_STEP_ANSWER && atom_is_error(answer));
            assert(atom_is_symbol(answer->expr.elems[2],"OccurrenceSourceIncomplete"));
            assert(next_answer(&machine,&answer)==PETTA_MACHINE_STEP_EXHAUSTED);
        }
        petta_machine_destroy(&machine); assert(input.live==0);
        input.fail_open=input.fail_next=false;
    }

    /* The snapshot's root and row identities remain stable after publication. */
    cursor=space_query_cursor_new_captured(&source,&answers,query,NULL); assert(cursor);
    assert(collect(cursor,&answers,projection,actual)==wanted); space_query_cursor_free(cursor);
    cetta_disk_store_view_close(updated); cetta_disk_store_view_close(view); cetta_disk_store_close(owner);
    space_free(&source); space_free(&ordinary); term_universe_free(&universe);
    arena_free(&answers); arena_free(&persistent); symbol_table_free(&symbols);
    puts("PASS: shared Space cursors and language matching stream encoded occurrences with owned rows, identities, clones and explicit failure");
    return 0;
}
