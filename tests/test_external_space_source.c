/* Scripted remote execution through the ordinary Space occurrence interface.
 * This is a finite protocol fixture, not a DAS client or network deployment.
 * It deliberately uses shared Space matching instead of remote pushdown. */
#include "parser.h"
#include "atom_wire.h"
#include "space.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { WAIT, ROW, COMPLETE, ERROR, DROP, CANCEL_ACCEPTED, CANCELLED } EventKind;
typedef struct { EventKind kind;size_t sequence,row; } Event;
typedef struct { unsigned char *bytes;size_t length;const char *identity; } Fact;
typedef struct {
    const Fact *facts;size_t length;uint64_t revision;
    bool captured,occurrence_bag;
} Snapshot;
typedef struct Source Source;
typedef struct {
    Source *owner;const Snapshot *snapshot;size_t references,slot;
    const Event *events;size_t event_count;
    uint64_t authority_generation;
    bool terminal,cancel_requested;
} Execution;
struct Source {
    const Snapshot *captured,*published;
    const Event *events;size_t event_count;
    Execution *executions[8];
    size_t live,active,opened,cloned,selected,polls,decoded,cancels,settled;
    bool authorized,require_bag,ack_immediately;
    uint64_t authority_generation;
    EventKind last_terminal;
};
typedef struct {
    Execution *execution;size_t event,next_sequence,last;
    bool failed,ended;
    Arena row;ArenaMark empty;
    Atom *atom,*identity;
} Cursor;
static void free_source(void *raw);

static Atom *parse(Arena *arena,const char *text) {
    Atom **forms=NULL;int count=parse_metta_text(text,arena,&forms);
    assert(count==1 && forms);Atom *value=forms[0];free(forms);return value;
}
static void release_execution(Execution *execution) {
    Source *source=execution->owner;
    assert(!execution->references && execution->terminal && source->executions[execution->slot]==execution);
    source->executions[execution->slot]=NULL;source->active--;source->settled++;free(execution);
}
static void acknowledge_cancel(Source *source) {
    for(size_t i=0;i<8;i++) {
        Execution *execution=source->executions[i];
        if(execution && !execution->references && execution->cancel_requested) {
            execution->terminal=true;source->last_terminal=CANCELLED;release_execution(execution);
        }
    }
}
static Cursor *new_cursor(Execution *execution) {
    Cursor *cursor=calloc(1,sizeof(*cursor));if(!cursor)return NULL;
    cursor->execution=execution;execution->references++;execution->owner->live++;
    arena_init_detached(&cursor->row);cursor->empty=arena_mark(&cursor->row);return cursor;
}
static bool open_source(Space *space,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    (void)pattern;Source *source=space->match_backend.source;source->opened++;
    *failure=SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    if(!source->authorized || !source->captured->captured ||
            (source->require_bag && !source->captured->occurrence_bag))return false;
    size_t slot=0;while(slot<8 && source->executions[slot])slot++;
    if(slot==8){*failure=SPACE_OCCURRENCE_CURSOR_CAPACITY;return false;}
    Execution *execution=calloc(1,sizeof(*execution));
    if(!execution){*failure=SPACE_OCCURRENCE_CURSOR_CAPACITY;return false;}
    *execution=(Execution){.owner=source,.snapshot=source->captured,.slot=slot,
        .events=source->events,.event_count=source->event_count,.authority_generation=source->authority_generation};
    Cursor *cursor=new_cursor(execution);
    if(!cursor){free(execution);*failure=SPACE_OCCURRENCE_CURSOR_CAPACITY;return false;}
    source->executions[slot]=execution;source->active++;*out=cursor;return true;
}
static bool clone_source(const void *raw,bool rewind,void **out) {
    const Cursor *original=raw;Cursor *copy=new_cursor(original->execution);if(!copy)return false;
    if(!rewind) {
        copy->event=original->event;copy->next_sequence=original->next_sequence;
        copy->last=original->last;copy->failed=original->failed;copy->ended=original->ended;
        if(original->atom) {
            AtomDeepCopySession *atoms=atom_deep_copy_session_new(&copy->row);
            copy->atom=atoms ? atom_deep_copy_session_copy(atoms,original->atom) : NULL;
            copy->identity=atoms ? atom_deep_copy_session_copy(atoms,original->identity) : NULL;
            atom_deep_copy_session_free(atoms);
            if(!copy->atom || !copy->identity){free_source(copy);return false;}
        }
    }
    original->execution->owner->cloned++;*out=copy;return true;
}
static bool select_source(const void *raw,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    (void)pattern;(void)failure;const Cursor *cursor=raw;
    cursor->execution->owner->selected++;
    /* This starts no remote query and never recaptures published. The fixture
     * retains one immutable finite stream shared by independent traversals. */
    return clone_source(raw,true,out);
}
static SpaceOccurrenceCursorStep next_source(void *raw,CettaIndex *index) {
    Cursor *cursor=raw;Execution *execution=cursor->execution;Source *source=execution->owner;
    const Snapshot *snapshot=execution->snapshot;source->polls++;
    if(cursor->failed)return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    if(!source->authorized || execution->authority_generation!=source->authority_generation)
        return SPACE_OCCURRENCE_CURSOR_INVALIDATED;
    if(cursor->ended)return SPACE_OCCURRENCE_CURSOR_END;
    arena_reset(&cursor->row,cursor->empty);cursor->atom=cursor->identity=NULL;
    if(cursor->event==execution->event_count){cursor->failed=true;return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;}
    Event event=execution->events[cursor->event++];
    if(event.kind==WAIT)return SPACE_OCCURRENCE_CURSOR_PROGRESS;
    if(event.kind==CANCEL_ACCEPTED) {
        execution->cancel_requested=true;return SPACE_OCCURRENCE_CURSOR_PROGRESS;
    }
    if(event.kind==COMPLETE) {
        if(cursor->next_sequence!=snapshot->length){cursor->failed=true;return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;}
        cursor->ended=true;execution->terminal=true;source->last_terminal=COMPLETE;
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    if(event.kind==ERROR || event.kind==DROP || event.kind==CANCELLED) {
        cursor->failed=true;
        execution->terminal=event.kind!=DROP;source->last_terminal=event.kind;
        return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    }
    /* The mock's captured manifest fixes enumeration sequence -> fact.
     * Replay is checked by frame coordinates, never by equal payloads.
     * A real backend needs corresponding immutable-execution evidence. */
    if(event.kind!=ROW || event.sequence!=event.row || event.row>=snapshot->length ||
            event.sequence>cursor->next_sequence){cursor->failed=true;return SPACE_OCCURRENCE_CURSOR_INCOMPLETE;}
    if(event.sequence<cursor->next_sequence)return SPACE_OCCURRENCE_CURSOR_PROGRESS;
    const Fact *fact=&snapshot->facts[event.row];
    CettaAtomWireStatus status=cetta_atom_wire_decode(&cursor->row,fact->bytes,fact->length,&cursor->atom);
    if(status!=ATOM_WIRE_OK) {
        cursor->failed=true;return status==ATOM_WIRE_NOMEM ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    }
    Atom *membership=atom_string(&cursor->row,fact->identity);
    cursor->identity=membership ? atom_expr3(&cursor->row,atom_symbol(&cursor->row,"ScriptOccurrenceV1"),
        atom_string(&cursor->row,"script-cav1:fixture"),membership) : NULL;
    if(!cursor->identity){cursor->failed=true;return SPACE_OCCURRENCE_CURSOR_CAPACITY;}
    cursor->last=cursor->next_sequence++;
    source->decoded++;*index=cursor->last;return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *source_atom(const void *raw,CettaIndex index) {
    const Cursor *cursor=raw;return index==cursor->last ? cursor->atom : NULL;
}
static Atom *source_identity(const void *raw,CettaIndex index) {
    const Cursor *cursor=raw;return index==cursor->last ? cursor->identity : NULL;
}
static void free_source(void *raw) {
    Cursor *cursor=raw;if(!cursor)return;
    Execution *execution=cursor->execution;Source *source=execution->owner;
    arena_free(&cursor->row);free(cursor);source->live--;assert(execution->references);
    if(!--execution->references) {
        if(!execution->terminal) {
            if(!execution->cancel_requested){execution->cancel_requested=true;source->cancels++;}
            if(source->ack_immediately){execution->terminal=true;source->last_terminal=CANCELLED;}
        }
        if(execution->terminal)release_execution(execution);
    }
}
static void source_storage(const void *raw,CettaStorageCensus *census) {
    const Cursor *cursor=raw;if(!cetta_storage_object(census,cursor,sizeof(*cursor),CETTA_STORAGE_BLOCK))return;
    cetta_storage_object(census,cursor->execution,sizeof(*cursor->execution),CETTA_STORAGE_BLOCK);
    arena_storage_census(&cursor->row,census);
}
static CettaCount source_length(const Space *space) {
    const Source *source=space->match_backend.source;return source->captured->length;
}
static const SpaceSourceCursorOps cursor_ops={.clone=clone_source,.select=select_source,
    .next=next_source,.atom=source_atom,.identity=source_identity,.free=free_source,.storage=source_storage};
static const SpaceMatchBackendOps backend={.name="scripted-external",.occurrence_ops=&cursor_ops,
    .occurrence_open=open_source,.logical_len=source_length};
static void initialize_space(Space *space,Source *source) {
    space_init(space);space->match_backend.kind=SPACE_ENGINE_SOURCE;
    space->match_backend.ops=&backend;space->match_backend.source=source;
}
static size_t drain(SpaceQueryCursor *cursor,Arena *arena,Atom *projection,Atom **results,
        SpaceQueryCursorStep *terminal) {
    Bindings row;bindings_init(&row);size_t count=0,polls=0;
    for(;;) {
        SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1,&row);assert(++polls<10000);
        if(step==SPACE_QUERY_CURSOR_ROW) {
            assert(count<32);results[count++]=bindings_apply_graph(&row,arena,projection);
            bindings_free(&row);bindings_init(&row);
        } else if(step!=SPACE_QUERY_CURSOR_PROGRESS){*terminal=step;break;}
    }
    bindings_free(&row);return count;
}
static void first_row(SpaceQueryCursor *cursor) {
    Bindings row;bindings_init(&row);SpaceQueryCursorStep step;size_t attempts=0;
    do {step=space_query_cursor_step_with_budget(cursor,1,&row);assert(++attempts<1000);} while(step==SPACE_QUERY_CURSOR_PROGRESS);
    assert(step==SPACE_QUERY_CURSOR_ROW);bindings_free(&row);
}
typedef struct { const char *expected;size_t seen; } IdentityCheck;
static bool verify_identity(SpaceReadToken read,Atom *identity,void *raw) {
    IdentityCheck *check=raw;assert(read.space && identity->kind==ATOM_EXPR && identity->expr.len==3);
    assert(atom_is_symbol(identity->expr.elems[0],"ScriptOccurrenceV1") &&
        atom_string_equals_cstr(identity->expr.elems[1],"script-cav1:fixture") &&
        atom_string_equals_cstr(identity->expr.elems[2],check->expected));check->seen++;return true;
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);g_symbols=&symbols;symbol_table_init_builtins(&symbols,&g_builtin_syms);
    Arena arena;arena_init_detached(&arena);
    const char *texts[]={"(edge a a)","(edge a a)","(edge a b)","(open $s $s)","(= (inert-rule) never-executed)"};
    const char *ids[]={"occurrence-a","occurrence-b","occurrence-c","occurrence-open","occurrence-rule"};
    Fact facts[5];Space ordinary;space_init(&ordinary);
    for(size_t i=0;i<5;i++) {
        Atom *value=parse(&arena,texts[i]);space_add(&ordinary,value);facts[i].identity=ids[i];
        assert(cetta_atom_wire_encode(value,&facts[i].bytes,&facts[i].length)==ATOM_WIRE_OK);
    }
    Snapshot captured={facts,5,17,true,true},unknown={facts,5,0,false,true},set={facts,1,18,true,false};
    const Event replay[]={ {WAIT,0,0},{ROW,0,0},{ROW,0,0},{WAIT,0,0},{ROW,1,1},{ROW,2,2},
        {ROW,0,0},{ROW,1,1},{ROW,2,2},{ROW,3,3},{WAIT,0,0},{ROW,4,4},{COMPLETE,0,0} };
    Source input={.captured=&captured,.published=&captured,.events=replay,.event_count=sizeof(replay)/sizeof(*replay),
        .authorized=true,.require_bag=true,.ack_immediately=true};Space external;initialize_space(&external,&input);
    const char *patterns[]={"(edge $x $y)","(edge $x $x)","(open 7 $y)","(open $x $y)","(= (inert-rule) $y)",
        "(, (edge $x $y) (edge $y $z))"};
    for(size_t i=0;i<6;i++) {
        Atom *pattern=parse(&arena,patterns[i]);SpaceQueryCursorStep terminal;
        SpaceQueryCursor *reference=space_query_cursor_new_captured(&ordinary,&arena,pattern,NULL);
        SpaceQueryCursor *cursor=space_query_cursor_new_captured(&external,&arena,pattern,NULL);assert(cursor && reference);
        Atom *expected[32],*actual[32];
        size_t wanted=drain(reference,&arena,pattern,expected,&terminal);assert(terminal==SPACE_QUERY_CURSOR_END);
        size_t found=drain(cursor,&arena,pattern,actual,&terminal);assert(terminal==SPACE_QUERY_CURSOR_END && found==wanted);
        for(size_t r=0;r<found;r++) {
            if(i==3)assert(actual[r]->expr.elems[1]->kind==ATOM_VAR && atom_eq(actual[r]->expr.elems[1],actual[r]->expr.elems[2]));
            else assert(atom_eq(actual[r],expected[r]));
        }
        space_query_cursor_free(reference);space_query_cursor_free(cursor);assert(!input.live && !input.active);
    }
    puts("PASS: captured scripted replay preserves native bindings, duplicate occurrences, shared variables, inert rules and joins");
    /* Both the data root and its finite execution stream stay captured when
     * the mock publisher changes. Rewind/select/clone starts no new query. */
    Snapshot later={facts+2,1,19,true,true};const Event single[]={{ROW,0,0},{COMPLETE,0,0}};
    Atom *edge=parse(&arena,"(edge $x $y)");Atom *rows[32];SpaceQueryCursorStep terminal;
    SpaceQueryCursor *identities=space_query_cursor_new_captured(&external,&arena,edge,NULL);assert(identities);
    for(size_t i=0;i<3;i++) {
        first_row(identities);IdentityCheck expected={ids[i],0};
        assert(space_query_cursor_visit_occurrences(identities,verify_identity,&expected) && expected.seen==1);
    }
    space_query_cursor_free(identities);assert(!input.live && !input.active);
    SpaceQueryCursor *old=space_query_cursor_new_captured(&external,&arena,edge,NULL);assert(old);first_row(old);
    SpaceQueryCursor *copy=space_query_cursor_clone(old);assert(copy);size_t opened=input.opened;
    input.published=input.captured=&later;input.events=single;input.event_count=2;
    assert(drain(old,&arena,edge,rows,&terminal)==2 && terminal==SPACE_QUERY_CURSOR_END);
    assert(drain(copy,&arena,edge,rows,&terminal)==2 && terminal==SPACE_QUERY_CURSOR_END && input.opened==opened);
    space_query_cursor_free(old);space_query_cursor_free(copy);
    SpaceQueryCursor *fresh=space_query_cursor_new_captured(&external,&arena,edge,NULL);assert(fresh);
    assert(drain(fresh,&arena,edge,rows,&terminal)==1 && terminal==SPACE_QUERY_CURSOR_END &&
        atom_eq(rows[0],parse(&arena,"(edge a b)")));space_query_cursor_free(fresh);
    input.captured=&set;input.require_bag=false;
    fresh=space_query_cursor_new_captured(&external,&arena,edge,NULL);assert(fresh);
    assert(drain(fresh,&arena,edge,rows,&terminal)==1 && terminal==SPACE_QUERY_CURSOR_END);
    space_query_cursor_free(fresh);assert(!input.live && !input.active);
    input.captured=input.published=&captured;input.require_bag=true;input.events=replay;input.event_count=sizeof(replay)/sizeof(*replay);
    old=space_query_cursor_new_captured(&external,&arena,edge,NULL);assert(old);first_row(old);
    input.authorized=false;input.authority_generation++;
    assert(!drain(old,&arena,edge,rows,&terminal) && terminal==SPACE_QUERY_CURSOR_INVALIDATED);
    input.authorized=true;input.authority_generation++;
    Bindings result;bindings_init(&result);assert(space_query_cursor_step(old,&result)==SPACE_QUERY_CURSOR_INVALIDATED);
    bindings_free(&result);space_query_cursor_free(old);assert(!input.live && !input.active);
    puts("PASS: clones retain data/stream generations; declared content-set view differs from bag; revoked readers do not revive");
    const Event failures[][3]={{{WAIT,0,0},{ERROR,0,0}},{{ROW,0,0},{DROP,0,0}},
        {{CANCEL_ACCEPTED,0,0},{WAIT,0,0},{CANCELLED,0,0}},{{ROW,1,1}},{{ROW,0,0},{COMPLETE,0,0}}};
    const size_t lengths[]={2,2,3,1,2};
    for(size_t mode=0;mode<5;mode++) {
        input.events=failures[mode];input.event_count=lengths[mode];input.last_terminal=WAIT;
        Atom *pattern=parse(&arena,"(edge $x $y)");SpaceQueryCursorStep terminal;Atom *rows[32];
        SpaceQueryCursor *cursor=space_query_cursor_new_captured(&external,&arena,pattern,NULL);assert(cursor);
        size_t prefix=drain(cursor,&arena,pattern,rows,&terminal);
        assert(terminal==SPACE_QUERY_CURSOR_INCOMPLETE && prefix==(mode==1 || mode==4 ? 1u : 0u));
        space_query_cursor_free(cursor);assert(!input.live && !input.active);
    }
    input.events=replay;input.event_count=sizeof(replay)/sizeof(*replay);
    for(size_t mode=0;mode<2;mode++) {
        input.captured=mode ? &set : &unknown;
        Atom *pattern=parse(&arena,"(edge $x $y)");SpaceQueryCursorStep terminal;Atom *rows[32];
        SpaceQueryCursor *cursor=space_query_cursor_new_captured(&external,&arena,pattern,NULL);
        assert(cursor && !drain(cursor,&arena,pattern,rows,&terminal) && terminal==SPACE_QUERY_CURSOR_INCOMPLETE);
        space_query_cursor_free(cursor);assert(!input.live && !input.active);
    }
    input.captured=&captured;input.ack_immediately=false;
    const Event cancel[]={{CANCEL_ACCEPTED,0,0},{WAIT,0,0}};input.events=cancel;input.event_count=2;
    SpaceOccurrenceCursor accepted;space_occurrence_cursor_init_empty(&accepted);
    assert(space_occurrence_cursor_init_scan(&external,&accepted));
    assert(space_occurrence_cursor_next(&accepted,NULL,NULL)==SPACE_OCCURRENCE_CURSOR_PROGRESS);
    Cursor *waiting=accepted.source_state;assert(waiting->execution->cancel_requested && !waiting->execution->terminal);
    assert(space_occurrence_cursor_next(&accepted,NULL,NULL)==SPACE_OCCURRENCE_CURSOR_PROGRESS);
    space_occurrence_cursor_release(&accepted);assert(input.active==1 && !input.live);
    acknowledge_cancel(&input);assert(!input.active);
    input.events=replay;input.event_count=sizeof(replay)/sizeof(*replay);
    for(size_t i=0;i<8;i++) {
        SpaceOccurrenceCursor cursor;space_occurrence_cursor_init_empty(&cursor);
        assert(space_occurrence_cursor_init_scan(&external,&cursor));space_occurrence_cursor_release(&cursor);
    }
    assert(input.active==8 && !input.live);
    SpaceOccurrenceCursor refused;space_occurrence_cursor_init_empty(&refused);
    assert(space_occurrence_cursor_init_scan(&external,&refused));
    assert(space_occurrence_cursor_next(&refused,NULL,NULL)==SPACE_OCCURRENCE_CURSOR_CAPACITY);
    space_occurrence_cursor_release(&refused);
    assert(input.active==8);acknowledge_cancel(&input);assert(!input.active);
    input.ack_immediately=true;
    size_t low=0,high=0;
    for(size_t i=0;i<2000;i++) {
        SpaceOccurrenceCursor cursor,clone;space_occurrence_cursor_init_empty(&cursor);space_occurrence_cursor_init_empty(&clone);
        assert(space_occurrence_cursor_init_scan(&external,&cursor));
        SpaceOccurrenceCursorStep step;
        do {step=space_occurrence_cursor_next(&cursor,NULL,NULL);} while(step==SPACE_OCCURRENCE_CURSOR_PROGRESS);
        assert(step==SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(space_occurrence_cursor_clone(&cursor,&clone));space_occurrence_cursor_release(&cursor);
        assert(input.active==1 && input.live==1);
        CettaStorageCensus census={0};cursor_ops.storage(clone.source_state,&census);cetta_storage_finish(&census);
        assert(!census.incomplete);if(!i)low=high=census.bytes;else {if(census.bytes<low)low=census.bytes;if(census.bytes>high)high=census.bytes;}
        cetta_storage_census_free(&census);space_occurrence_cursor_release(&clone);assert(!input.active && !input.live);
    }
    assert(low==high);
    printf("PASS: remote completion/profile failures remain incomplete; cancellation acceptance differs from settlement; 2000clone/cancel rounds retain %zu cursor bytes\n",high);
    space_free(&external);space_free(&ordinary);arena_free(&arena);
    for(size_t i=0;i<5;i++)free(facts[i].bytes);
    symbol_table_free(&symbols);g_symbols=NULL;return 0;
}
