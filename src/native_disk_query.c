#include "native_disk_query.h"
#include "atom_wire.h"
#include <stdlib.h>
#include <string.h>

struct CettaDiskQuery {
    Arena source_arena, arenas[2];
    TermUniverse universe;
    Space source;
    SpaceQueryCursor *cursor;
    Atom *template;
    unsigned arena;
    bool live, packet_pending, has_packet, terminal;
    CettaDiskQueryStep completion;
    unsigned char *packet;
    size_t packet_size;
    uint64_t next_sequence, packet_sequence, revision;
    CettaDiskSpaceStatistics statistics;
};

static void packet_clear(CettaDiskQuery *query) {
    free(query->packet);query->packet=NULL;query->packet_size=0;
    query->has_packet=query->packet_pending=false;
}
static void release_query(CettaDiskQuery *query) {
    if (!query->live) return;
    cetta_disk_space_statistics(&query->source,&query->statistics);
    space_query_cursor_free(query->cursor);query->cursor=NULL;
    space_free(&query->source);term_universe_free(&query->universe);
    arena_free(&query->arenas[0]);arena_free(&query->arenas[1]);
    arena_free(&query->source_arena);query->template=NULL;query->live=false;
}
static CettaDiskQueryStep finish(CettaDiskQuery *query,CettaDiskQueryStep step) {
    query->terminal=true;query->completion=step;
    release_query(query);packet_clear(query);return step;
}
CettaDiskQueryStep cetta_disk_query_cancel(CettaDiskQuery *query) {
    if (query && (!query->terminal || query->completion!=CETTA_DISK_QUERY_END))
        return finish(query,CETTA_DISK_QUERY_CANCELLED);
    return query ? query->completion : CETTA_DISK_QUERY_CANCELLED;
}
void cetta_disk_query_close(CettaDiskQuery *query) {
    if (!query) return;
    release_query(query);packet_clear(query);free(query);
}

static CettaDiskStoreStatus query_open(const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t count,const CettaDiskSpaceIndex *indexes,
        size_t index_count,Atom *pattern,Atom *template,bool point,CettaDiskQuery **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!view || !pattern || !template) return CETTA_STORE_INVALID;
    CettaDiskQuery *query=calloc(1,sizeof(*query));
    if (!query) return CETTA_STORE_NOMEM;
    arena_init_detached(&query->source_arena);
    arena_init_detached(&query->arenas[0]);arena_init_detached(&query->arenas[1]);
    term_universe_init(&query->universe);
    term_universe_set_persistent_arena(&query->universe,&query->source_arena);
    space_init_with_universe(&query->source,&query->universe);query->live=true;
    CettaDiskStoreStatus status=point ? cetta_disk_space_capture_point(&query->source,view,prefix) :
        cetta_disk_space_capture_indexed(&query->source,view,prefix,count,indexes,index_count);
    if (status!=CETTA_STORE_OK) goto failed;
    query->revision=cetta_disk_store_view_revision(view);
    Arena packet;arena_init_detached(&packet);
    Atom *spec=atom_expr3(&packet,atom_symbol(&packet,"DiskQuerySpecV1"),pattern,template);
    unsigned char *bytes=NULL;size_t length=0;
    CettaAtomWireStatus wire=cetta_atom_wire_encode(spec,&bytes,&length);
    Atom *owned=NULL;
    if (wire==ATOM_WIRE_OK) wire=cetta_atom_wire_decode(&query->arenas[0],bytes,length,&owned);
    free(bytes);arena_free(&packet);
    if (wire!=ATOM_WIRE_OK) {
        status=wire==ATOM_WIRE_NOMEM ? CETTA_STORE_NOMEM
            : wire==ATOM_WIRE_LIMIT ? CETTA_STORE_LIMIT : CETTA_STORE_INVALID;
        goto failed;
    }
    query->template=owned->expr.elems[2];
    query->cursor=space_query_cursor_new_captured(&query->source,
        &query->arenas[0],owned->expr.elems[1],NULL);
    if (!query->cursor || !space_query_cursor_incremental(query->cursor)) {
        status=CETTA_STORE_NOMEM;goto failed;
    }
    *out=query;return CETTA_STORE_OK;
failed:
    cetta_disk_query_close(query);return status;
}
CettaDiskStoreStatus cetta_disk_query_open_indexed(const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t count,const CettaDiskSpaceIndex *indexes,
        size_t index_count,Atom *pattern,Atom *template,CettaDiskQuery **out) {
    return query_open(view,prefix,count,indexes,index_count,pattern,template,false,out);
}
CettaDiskStoreStatus cetta_disk_query_open_point(const CettaDiskStoreView *view,
        CettaDiskBytes key,Atom *pattern,Atom *template,CettaDiskQuery **out) {
    return query_open(view,key,1,NULL,0,pattern,template,true,out);
}
CettaDiskStoreStatus cetta_disk_query_open(const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t count,Atom *pattern,Atom *template,CettaDiskQuery **out) {
    return cetta_disk_query_open_indexed(view,prefix,count,NULL,0,pattern,template,out);
}
bool cetta_disk_query_statistics(const CettaDiskQuery *query,CettaDiskSpaceStatistics *out) {
    if (!query || !out) return false;
    if (query->live) return cetta_disk_space_statistics(&query->source,out);
    *out=query->statistics;return true;
}

typedef struct { Atom **items;size_t count,capacity; } Ids;
static bool identity(SpaceReadToken read,Atom *id,void *raw) {
    (void)read;Ids *ids=raw;
    if (ids->count==ids->capacity) {
        size_t next=ids->capacity ? ids->capacity*2 : 4;
        if (next<ids->capacity || next>SIZE_MAX/sizeof(*ids->items)) return false;
        Atom **items=realloc(ids->items,next*sizeof(*items));
        if (!items) return false;
        ids->items=items;ids->capacity=next;
    }
    ids->items[ids->count++]=id;return true;
}
static CettaDiskQueryStep prepare_row(CettaDiskQuery *query,Bindings *bindings) {
    Arena *arena=&query->arenas[query->arena];Ids ids={0};
    Atom *value=bindings_apply_graph(bindings,arena,query->template);
    bool ready=value && space_query_cursor_visit_occurrences(query->cursor,identity,&ids);
    Atom *occurrences=ready ? atom_expr(arena,ids.items,ids.count) : NULL;
    free(ids.items);
    Atom *record=occurrences ? atom_expr3(arena,atom_symbol(arena,"DiskQueryRowV1"),value,occurrences) : NULL;
    CettaAtomWireStatus status=record ? cetta_atom_wire_encode(record,&query->packet,&query->packet_size)
                                    : ATOM_WIRE_NOMEM;
    if (status!=ATOM_WIRE_OK)
        return status==ATOM_WIRE_NOMEM || status==ATOM_WIRE_LIMIT
            ? CETTA_DISK_QUERY_CAPACITY : CETTA_DISK_QUERY_INCOMPLETE;
    query->has_packet=query->packet_pending=true;
    query->packet_sequence=query->next_sequence;
    return CETTA_DISK_QUERY_ROW;
}
static CettaDiskQueryStep offer(CettaDiskQuery *query,size_t maximum,
        CettaDiskBytes *bytes,size_t *required) {
    if (required) *required=query->packet_size;
    if (query->packet_size>maximum) return CETTA_DISK_QUERY_TOO_LARGE;
    if (query->packet_pending) {
        if (query->next_sequence==UINT64_MAX) return finish(query,CETTA_DISK_QUERY_CAPACITY);
        query->next_sequence++;query->packet_pending=false;
    }
    *bytes=(CettaDiskBytes){query->packet,query->packet_size};
    return CETTA_DISK_QUERY_ROW;
}
CettaDiskQueryStep cetta_disk_query_next(CettaDiskQuery *query,uint64_t sequence,
        uint64_t transitions,size_t maximum,CettaDiskBytes *bytes,size_t *required) {
    if (bytes) *bytes=(CettaDiskBytes){0};
    if (required) *required=0;
    if (!query || !bytes) return CETTA_DISK_QUERY_CAPACITY;
    if (query->terminal) return query->completion;
    if (query->has_packet && sequence==query->packet_sequence)
        return offer(query,maximum,bytes,required);
    if (sequence!=query->next_sequence) return CETTA_DISK_QUERY_STALE;
    packet_clear(query); /* Asking for the next row acknowledges the last. */
    for (uint64_t step=0;step<transitions;step++) {
        Bindings bindings;bindings_init(&bindings);
        SpaceQueryCursorStep result=space_query_cursor_step(query->cursor,&bindings);
        CettaDiskQueryStep reply=CETTA_DISK_QUERY_PAUSED;
        if (result==SPACE_QUERY_CURSOR_ROW) reply=prepare_row(query,&bindings);
        bindings_free(&bindings);
        switch (result) {
        case SPACE_QUERY_CURSOR_PROGRESS: continue;
        case SPACE_QUERY_CURSOR_ROW:
            return reply==CETTA_DISK_QUERY_ROW ? offer(query,maximum,bytes,required) : finish(query,reply);
        case SPACE_QUERY_CURSOR_END: return finish(query,CETTA_DISK_QUERY_END);
        case SPACE_QUERY_CURSOR_INVALIDATED: return finish(query,CETTA_DISK_QUERY_INVALIDATED);
        case SPACE_QUERY_CURSOR_CAPACITY: return finish(query,CETTA_DISK_QUERY_CAPACITY);
        case SPACE_QUERY_CURSOR_INCOMPLETE: return finish(query,CETTA_DISK_QUERY_INCOMPLETE);
        }
    }
    return CETTA_DISK_QUERY_PAUSED;
}

bool cetta_disk_query_checkpoint(CettaDiskQuery *query) {
    if (!query || !query->live) return query && query->terminal;
    unsigned target=1-query->arena;
    Arena *from=&query->arenas[query->arena],*to=&query->arenas[target];
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(to);
    AtomDeepCopyArenaPair pair={from,to};
    bool ready=atoms && atom_deep_copy_session_set_arena_map(atoms,&pair,1);
    Atom *template=ready ? atom_deep_copy_session_copy(atoms,query->template) : NULL;
    SpaceQueryCursor *copy=template ? space_query_cursor_copy_to_arena(query->cursor,to,atoms) : NULL;
    atom_deep_copy_session_free(atoms);
    if (!copy) { arena_free(to);arena_init_detached(to);return false; }
    space_query_cursor_free(query->cursor);query->cursor=copy;query->template=template;
    arena_free(from);arena_init_detached(from);query->arena=target;return true;
}
void cetta_disk_query_storage_census(const CettaDiskQuery *query,CettaStorageCensus *census) {
    if (!query || !cetta_storage_object(census,query,sizeof(*query),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,query->packet,query->packet_size,1,0);
    if (!query->live) return;
    arena_storage_census(&query->source_arena,census);
    arena_storage_census(&query->arenas[0],census);arena_storage_census(&query->arenas[1],census);
    term_universe_storage_census(&query->universe,census);
    space_storage_census(&query->source,census);
    space_query_cursor_storage_census(query->cursor,census);
}
uint64_t cetta_disk_query_revision(const CettaDiskQuery *query) { return query ? query->revision : 0; }
