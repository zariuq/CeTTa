#include "native_disk_space.h"
#include "atom_wire.h"
#include "storage_census.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t arity, position; CettaDiskBuffer prefix; } Index;
typedef struct {
    _Atomic uint64_t scans, indexed, candidates, bytes;
    _Atomic bool overflow;
} Statistics;
typedef struct {
    CettaDiskStoreView *view;
    CettaDiskBuffer prefix;
    Index *indexes; size_t index_count;
    uint64_t count;
    bool point;
    Statistics statistics;
} DiskSpace;
typedef struct {
    CettaDiskCursor *disks[2];
    CettaDiskBuffer prefix, ranges[2], key, fact_key;
    Index *indexes; size_t index_count, streams, active, value_size;
    Arena row; ArenaMark empty;
    Atom *atom, *identity;
    unsigned char epoch[16];
    uint64_t expected, seen;
    uint32_t arity, position;
    CettaIndex last;
    SpaceOccurrenceCursorStep terminal;
    Statistics *statistics; /* The enclosing Space outlives its queries. */
    bool indexed, started, done,point;
} DiskSpaceCursor;

static void charge(Statistics *statistics,_Atomic uint64_t *counter,uint64_t amount) {
    uint64_t before=atomic_load_explicit(counter,memory_order_relaxed);
    for (;;) {
        bool overflow=amount>UINT64_MAX-before;
        uint64_t after=overflow ? UINT64_MAX : before+amount;
        if (atomic_compare_exchange_weak_explicit(counter,&before,after,memory_order_relaxed,memory_order_relaxed)) {
            if (overflow) atomic_store_explicit(&statistics->overflow,true,memory_order_relaxed);
            return;
        }
    }
}
static bool buffer_copy(CettaDiskBuffer *out,CettaDiskBytes bytes) {
    *out=(CettaDiskBuffer){0};
    if (!bytes.size) return true;
    out->data=malloc(bytes.size);if (!out->data) return false;
    memcpy(out->data,bytes.data,bytes.size);out->size=bytes.size;return true;
}
static CettaDiskBytes borrowed(const CettaDiskBuffer *buffer) {
    return (CettaDiskBytes){buffer->data,buffer->size};
}
static bool starts(CettaDiskBytes text,CettaDiskBytes prefix) {
    return text.size>=prefix.size && (!prefix.size || !memcmp(text.data,prefix.data,prefix.size));
}
static void indexes_free(Index *indexes,size_t count) {
    for (size_t i=0;i<count;i++) cetta_disk_buffer_free(&indexes[i].prefix);
    free(indexes);
}
static bool indexes_copy(Index **out,const Index *source,size_t count) {
    *out=NULL;
    if (!count) return true;
    if (count>SIZE_MAX/sizeof(**out)) return false;
    Index *items=calloc(count,sizeof(*items));if (!items) return false;
    for (size_t i=0;i<count;i++) {
        items[i].arity=source[i].arity;items[i].position=source[i].position;
        if (!buffer_copy(&items[i].prefix,borrowed(&source[i].prefix))) { indexes_free(items,count);return false; }
    }
    *out=items;return true;
}
static bool exact_value(const Atom *value) {
    return value && (value->kind==ATOM_SYMBOL ||
        (value->kind==ATOM_GROUNDED && value->ground.gkind==GV_STRING));
}
static bool bucket(CettaDiskBytes prefix,const char *tag,const unsigned char *value,size_t size,
        size_t maximum,CettaDiskBuffer *out) {
    size_t tag_size=strlen(tag),tail=size ? 1u : 0u;
    if (prefix.size>maximum || tag_size>maximum-prefix.size ||
        size>maximum-prefix.size-tag_size || tail>maximum-prefix.size-tag_size-size) return false;
    out->size=prefix.size+tag_size+size+tail;
    out->data=malloc(out->size ? out->size : 1);if (!out->data) { out->size=0;return false; }
    memcpy(out->data,prefix.data,prefix.size);memcpy(out->data+prefix.size,tag,tag_size);
    if (size) { memcpy(out->data+prefix.size+tag_size,value,size);out->data[out->size-1]='/'; }
    return true;
}
static const CettaDiskRoot *cursor_root(const DiskSpaceCursor *cursor) {
    for (size_t i=0;i<cursor->streams;i++) if (cursor->disks[i]) return cetta_disk_cursor_root(cursor->disks[i]);
    return NULL;
}
static void cursor_free(void *raw) {
    DiskSpaceCursor *cursor=raw;if (!cursor) return;
    for (size_t i=0;i<2;i++) { cetta_disk_cursor_close(cursor->disks[i]);cetta_disk_buffer_free(&cursor->ranges[i]); }
    cetta_disk_buffer_free(&cursor->prefix);cetta_disk_buffer_free(&cursor->key);cetta_disk_buffer_free(&cursor->fact_key);
    indexes_free(cursor->indexes,cursor->index_count);arena_free(&cursor->row);free(cursor);
}
static bool open_range(const CettaDiskRoot *root,CettaDiskBytes prefix,const CettaDiskBytes *after,
        CettaDiskCursor **out,SpaceOccurrenceCursorStep *failure) {
    CettaDiskBuffer upper={0};size_t end=prefix.size;
    while (end && prefix.data[end-1]==255) --end;
    if (end) {
        if (!buffer_copy(&upper,(CettaDiskBytes){prefix.data,end})) return false;
        ++upper.data[end-1];
    }
    CettaDiskBytes bound=borrowed(&upper);
    const CettaDiskBytes *lower=after ? after : (prefix.size ? &prefix : NULL);
    CettaDiskStatus status=cetta_disk_cursor_open_range(root,lower,after!=NULL,end ? &bound : NULL,out);
    cetta_disk_buffer_free(&upper);
    if (status!=CETTA_DISK_OK && failure)
        *failure=status==CETTA_DISK_NOMEM ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
    return status==CETTA_DISK_OK;
}
static DiskSpaceCursor *cursor_allocate(CettaDiskBytes prefix,const Index *indexes,size_t count,
        const unsigned char epoch[16],uint64_t expected,Statistics *statistics) {
    DiskSpaceCursor *cursor=calloc(1,sizeof(*cursor));if (!cursor) return NULL;
    arena_init_detached(&cursor->row);cursor->empty=arena_mark(&cursor->row);
    cursor->expected=expected;cursor->statistics=statistics;memcpy(cursor->epoch,epoch,16);
    if (!buffer_copy(&cursor->prefix,prefix) || !indexes_copy(&cursor->indexes,indexes,count)) { cursor_free(cursor);return NULL; }
    cursor->index_count=count;return cursor;
}
/* Prefer the last eligible declared projection. This is observable access
 * selection, not a cardinality optimizer. Unknown equality keeps a full scan. */
static bool cursor_plan(DiskSpaceCursor *cursor,const CettaDiskRoot *root,Atom *pattern) {
    unsigned char *value=NULL;size_t size=0;const Index *chosen=NULL;
    if (!cursor->point && pattern && pattern->kind==ATOM_EXPR) {
        for (size_t i=0;i<cursor->index_count;i++) {
            const Index *index=&cursor->indexes[i];
            if (index->arity==pattern->expr.len && exact_value(pattern->expr.elems[index->position])) chosen=index;
        }
    }
    if (chosen) {
        CettaAtomWireStatus status=cetta_atom_wire_encode(pattern->expr.elems[chosen->position],&value,&size);
        if (status==ATOM_WIRE_NOMEM) return false;
        if (status==ATOM_WIRE_OK) {
            bool exact=bucket(borrowed(&chosen->prefix),"/exact/",value,size,root->key_limit,&cursor->ranges[0]);
            if (exact && bucket(borrowed(&chosen->prefix),"/any/",NULL,0,root->key_limit,&cursor->ranges[1])) {
                free(value);cursor->indexed=true;cursor->streams=2;cursor->arity=chosen->arity;cursor->position=chosen->position;cursor->value_size=size;return true;
            }
            cetta_disk_buffer_free(&cursor->ranges[0]);cetta_disk_buffer_free(&cursor->ranges[1]);
        }
        free(value);
    }
    cursor->streams=1;return buffer_copy(&cursor->ranges[0],borrowed(&cursor->prefix));
}
static bool cursor_new(const CettaDiskRoot *root,CettaDiskBytes prefix,const Index *indexes,size_t count,
        const unsigned char epoch[16],uint64_t expected,Statistics *statistics,Atom *pattern,
        bool point,void **out,SpaceOccurrenceCursorStep *failure) {
    *out=NULL;DiskSpaceCursor *cursor=cursor_allocate(prefix,indexes,count,epoch,expected,statistics);
    if (cursor) cursor->point=point;
    if (!cursor || !cursor_plan(cursor,root,pattern)) { cursor_free(cursor);return false; }
    for (size_t i=0;i<cursor->streams;i++)
        if (!open_range(root,borrowed(&cursor->ranges[i]),NULL,&cursor->disks[i],failure)) { cursor_free(cursor);return false; }
    *out=cursor;return true;
}
static bool source_open(Space *space,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    DiskSpace *source=space->match_backend.source;
    return source && cursor_new(cetta_disk_store_view_root(source->view),borrowed(&source->prefix),
        source->indexes,source->index_count,cetta_disk_store_view_epoch(source->view),source->count,
        &source->statistics,pattern,source->point,out,failure);
}
static bool cursor_clone(const void *raw,bool rewind,void **out) {
    const DiskSpaceCursor *source=raw;if (!source || !out) return false;*out=NULL;
    DiskSpaceCursor *copy=cursor_allocate(borrowed(&source->prefix),source->indexes,source->index_count,
        source->epoch,source->expected,source->statistics);
    if (!copy) return false;
    copy->streams=source->streams;copy->indexed=source->indexed;copy->arity=source->arity;copy->position=source->position;
    copy->point=source->point;
    copy->value_size=source->value_size;
    copy->active=rewind ? 0 : source->active;
    copy->started=!rewind && source->started;
    for (size_t i=0;i<source->streams;i++) {
        if (!buffer_copy(&copy->ranges[i],borrowed(&source->ranges[i]))) goto failed;
        CettaDiskBytes after=borrowed(&source->key);
        if (i>=copy->active && !open_range(cursor_root(source),borrowed(&source->ranges[i]),
            !rewind && i==source->active && source->seen && !source->done ? &after : NULL,&copy->disks[i],NULL)) goto failed;
    }
    if (!rewind) {
        copy->seen=source->seen;copy->last=source->last;copy->terminal=source->terminal;copy->done=source->done;
        if (!buffer_copy(&copy->key,borrowed(&source->key)) || !buffer_copy(&copy->fact_key,borrowed(&source->fact_key))) goto failed;
        if (source->atom && !(copy->atom=atom_deep_copy(&copy->row,source->atom))) goto failed;
        if (source->identity && !(copy->identity=atom_deep_copy(&copy->row,source->identity))) goto failed;
    }
    *out=copy;return true;
failed:
    cursor_free(copy);return false;
}
static bool cursor_select(const void *raw,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
    const DiskSpaceCursor *source=raw;
    return source && cursor_new(cursor_root(source),borrowed(&source->prefix),source->indexes,source->index_count,
        source->epoch,source->expected,source->statistics,pattern,source->point,out,failure);
}
static SpaceOccurrenceCursorStep stop(DiskSpaceCursor *cursor,SpaceOccurrenceCursorStep result) {
    cursor->done=true;cursor->terminal=result;return result;
}
static CettaDiskStatus read_buffer(const CettaDiskRoot *root,CettaDiskBlob blob,size_t maximum,CettaDiskBuffer *out) {
    if (blob.size>maximum || blob.size>SIZE_MAX) return CETTA_DISK_LIMIT;
    out->data=malloc(blob.size ? (size_t)blob.size : 1);if (!out->data) return CETTA_DISK_NOMEM;
    size_t got=0;CettaDiskStatus status=cetta_disk_blob_read(root,blob,0,out->data,(size_t)blob.size,&got);
    if (status==CETTA_DISK_OK && got!=blob.size) status=CETTA_DISK_CORRUPT;
    out->size=(size_t)blob.size;return status;
}
static CettaAtomWireStatus posting_shape(const DiskSpaceCursor *cursor) {
    const Atom *atom=cursor->atom;
    if (atom->kind==ATOM_VAR) return cursor->active==1 ? ATOM_WIRE_OK : ATOM_WIRE_CORRUPT;
    if (atom->kind!=ATOM_EXPR || atom->expr.len!=cursor->arity) return ATOM_WIRE_CORRUPT;
    const Atom *value=atom->expr.elems[cursor->position];
    if (cursor->active==1) return !exact_value(value) ? ATOM_WIRE_OK : ATOM_WIRE_CORRUPT;
    if (!exact_value(value)) return ATOM_WIRE_CORRUPT;
    unsigned char *wire=NULL;size_t size=0;
    CettaAtomWireStatus status=cetta_atom_wire_encode((Atom *)value,&wire,&size);
    if (status!=ATOM_WIRE_OK) { free(wire);return status; }
    const CettaDiskBuffer *range=&cursor->ranges[0];
    bool valid=size==cursor->value_size && range->size>=size+1 && range->data[range->size-1]=='/' &&
        !memcmp(range->data+range->size-size-1,wire,size);
    free(wire);return valid ? ATOM_WIRE_OK : ATOM_WIRE_CORRUPT;
}
static SpaceOccurrenceCursorStep cursor_next(void *raw,CettaIndex *index) {
    DiskSpaceCursor *cursor=raw;if (cursor->done) return cursor->terminal;
    if (cursor->point && cursor->seen) return stop(cursor,SPACE_OCCURRENCE_CURSOR_END);
    if (!cursor->started) {
        cursor->started=true;
        if (cursor->statistics) charge(cursor->statistics,
            cursor->indexed ? &cursor->statistics->indexed : &cursor->statistics->scans,1);
    }
    arena_reset(&cursor->row,cursor->empty);cursor->atom=cursor->identity=NULL;
    cetta_disk_buffer_free(&cursor->key);cetta_disk_buffer_free(&cursor->fact_key);
    CettaDiskBlob blob;CettaDiskStatus status;
    for (;;) {
        status=cetta_disk_cursor_next(cursor->disks[cursor->active],&cursor->key,&blob);
        if (status!=CETTA_DISK_END) break;
        if (cursor->active+1==cursor->streams) return stop(cursor,
            cursor->indexed || cursor->seen==cursor->expected ? SPACE_OCCURRENCE_CURSOR_END : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
        cursor->active++;
    }
    if (status!=CETTA_DISK_OK || cursor->seen>=cursor->expected) return stop(cursor,
        status==CETTA_DISK_NOMEM || status==CETTA_DISK_LIMIT ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    if (cursor->point && (cursor->key.size!=cursor->prefix.size ||
            (cursor->key.size && memcmp(cursor->key.data,cursor->prefix.data,cursor->key.size))))
        return stop(cursor,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    const CettaDiskRoot *root=cursor_root(cursor);
    if (cursor->statistics) charge(cursor->statistics,&cursor->statistics->candidates,1);
    if (cursor->indexed) {
        CettaDiskBytes selected=borrowed(&cursor->ranges[cursor->active]);
        size_t got=0;
        if (blob.size || !starts(borrowed(&cursor->key),selected) ||
            cetta_disk_blob_read(root,blob,0,NULL,0,&got)!=CETTA_DISK_OK || got)
            return stop(cursor,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
        CettaDiskBytes fact={cursor->key.data+selected.size,cursor->key.size-selected.size};
        if (!starts(fact,borrowed(&cursor->prefix))) return stop(cursor,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
        if (!buffer_copy(&cursor->fact_key,fact)) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
        status=cetta_disk_lookup(root,borrowed(&cursor->fact_key),&blob);
        if (status!=CETTA_DISK_OK) return stop(cursor,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    } else if (!buffer_copy(&cursor->fact_key,borrowed(&cursor->key))) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    CettaDiskBuffer body={0};status=read_buffer(root,blob,CETTA_ATOM_WIRE_BYTES,&body);
    CettaAtomWireStatus decoded=status==CETTA_DISK_OK
        ? cetta_atom_wire_decode(&cursor->row,body.data,body.size,&cursor->atom) : ATOM_WIRE_CORRUPT;
    if (decoded==ATOM_WIRE_OK && cursor->statistics) charge(cursor->statistics,&cursor->statistics->bytes,body.size);
    cetta_disk_buffer_free(&body);
    if (decoded!=ATOM_WIRE_OK) return stop(cursor,decoded==ATOM_WIRE_NOMEM || decoded==ATOM_WIRE_LIMIT ||
        status==CETTA_DISK_NOMEM || status==CETTA_DISK_LIMIT ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    if (cursor->indexed) {
        CettaAtomWireStatus shape=posting_shape(cursor);
        if (shape!=ATOM_WIRE_OK) return stop(cursor,shape==ATOM_WIRE_NOMEM || shape==ATOM_WIRE_LIMIT
            ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    }
    static const char hex[]="0123456789abcdef";char epoch[33];
    for (size_t i=0;i<16;i++) { epoch[2*i]=hex[cursor->epoch[i]>>4];epoch[2*i+1]=hex[cursor->epoch[i]&15]; }
    epoch[32]=0;
    Atom *fields[]={atom_symbol(&cursor->row,"StoreOccurrenceV1"),atom_string(&cursor->row,epoch),
        atom_string_n(&cursor->row,(const char *)cursor->fact_key.data,cursor->fact_key.size)};
    cursor->identity=atom_expr(&cursor->row,fields,3);
    if (!cursor->identity || !cursor->atom) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    *index=cursor->last=(CettaIndex)cursor->seen++;return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *cursor_atom(const void *raw,CettaIndex index) {
    const DiskSpaceCursor *cursor=raw;return cursor->seen && index==cursor->last ? cursor->atom : NULL;
}
static Atom *cursor_identity(const void *raw,CettaIndex index) {
    const DiskSpaceCursor *cursor=raw;return cursor->seen && index==cursor->last ? cursor->identity : NULL;
}
static void index_storage(const Index *indexes,size_t count,CettaStorageCensus *census) {
    cetta_storage_array(census,indexes,count,sizeof(*indexes),0);
    for (size_t i=0;i<count;i++) cetta_storage_array(census,indexes[i].prefix.data,indexes[i].prefix.size,1,0);
}
static void cursor_storage(const void *raw,CettaStorageCensus *census) {
    const DiskSpaceCursor *cursor=raw;
    if (!cursor || !cetta_storage_object(census,cursor,sizeof(*cursor),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,cursor->prefix.data,cursor->prefix.size,1,0);
    cetta_storage_array(census,cursor->key.data,cursor->key.size ? cursor->key.size : 1,1,0);
    cetta_storage_array(census,cursor->fact_key.data,cursor->fact_key.size ? cursor->fact_key.size : 1,1,0);
    index_storage(cursor->indexes,cursor->index_count,census);
    for (size_t i=0;i<2;i++) {
        cetta_storage_array(census,cursor->ranges[i].data,cursor->ranges[i].size,1,0);
        cetta_disk_cursor_storage_census(cursor->disks[i],census);
    }
    arena_storage_census(&cursor->row,census);
}
static void source_free(Space *space) {
    DiskSpace *source=space->match_backend.source;if (!source) return;
    cetta_disk_store_view_close(source->view);cetta_disk_buffer_free(&source->prefix);
    indexes_free(source->indexes,source->index_count);free(source);space->match_backend.source=NULL;
}
static uint64_t source_count(const Space *space) {
    const DiskSpace *source=space->match_backend.source;return source->count;
}
static void source_storage(const Space *space,CettaStorageCensus *census) {
    const DiskSpace *source=space->match_backend.source;
    if (!source || !cetta_storage_object(census,source,sizeof(*source),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,source->prefix.data,source->prefix.size,1,0);
    index_storage(source->indexes,source->index_count,census);cetta_disk_store_view_storage_census(source->view,census);
}
static const SpaceSourceCursorOps cursor_ops={
    .clone=cursor_clone,.select=cursor_select,.next=cursor_next,.atom=cursor_atom,
    .identity=cursor_identity,.free=cursor_free,.storage=cursor_storage,
};
static const SpaceMatchBackendOps source_ops={
    .name="native-disk-atoms",.occurrence_ops=&cursor_ops,.occurrence_open=source_open,
    .logical_len=source_count,.free=source_free,.storage=source_storage,
};
bool cetta_disk_space_statistics(const Space *space,CettaDiskSpaceStatistics *out) {
    if (!space || !out || space->match_backend.ops!=&source_ops || !space->match_backend.source) return false;
    const Statistics *s=&((const DiskSpace *)space->match_backend.source)->statistics;
    *out=(CettaDiskSpaceStatistics){atomic_load(&s->scans),atomic_load(&s->indexed),
        atomic_load(&s->candidates),atomic_load(&s->bytes),atomic_load(&s->overflow)};return true;
}
CettaDiskStoreStatus cetta_disk_space_capture_indexed(Space *target,const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t count,const CettaDiskSpaceIndex *indexes,size_t index_count) {
    if (!target || !view || (prefix.size && !prefix.data) || (index_count && !indexes) ||
        index_count>SIZE_MAX/sizeof(Index) || target->kind!=SPACE_KIND_ATOM ||
        target->match_backend.kind!=SPACE_ENGINE_NATIVE || space_length64(target) ||
        target->overlay_base || space_has_dependencies(target) || target->module_own_view) return CETTA_STORE_INVALID;
    const CettaDiskRoot *root=cetta_disk_store_view_root(view);
    for (size_t i=0;i<index_count;i++) {
        const CettaDiskSpaceIndex *index=&indexes[i];
        if (!index->arity || index->position>=index->arity || !index->prefix.size || !index->prefix.data ||
            index->prefix.size>root->key_limit || starts(index->prefix,prefix) || starts(prefix,index->prefix)) return CETTA_STORE_INVALID;
        for (size_t j=0;j<i;j++) if ((index->arity==indexes[j].arity && index->position==indexes[j].position) ||
            starts(index->prefix,indexes[j].prefix) || starts(indexes[j].prefix,index->prefix)) return CETTA_STORE_INVALID;
    }
    DiskSpace *source=calloc(1,sizeof(*source));if (!source) return CETTA_STORE_NOMEM;
    if (!buffer_copy(&source->prefix,prefix)) { free(source);return CETTA_STORE_NOMEM; }
    if (index_count) {
        source->indexes=calloc(index_count,sizeof(*source->indexes));
        if (!source->indexes) { cetta_disk_buffer_free(&source->prefix);free(source);return CETTA_STORE_NOMEM; }
        source->index_count=index_count;
        for (size_t i=0;i<index_count;i++) {
            source->indexes[i].arity=indexes[i].arity;source->indexes[i].position=indexes[i].position;
            if (!buffer_copy(&source->indexes[i].prefix,indexes[i].prefix)) {
                indexes_free(source->indexes,index_count);cetta_disk_buffer_free(&source->prefix);free(source);return CETTA_STORE_NOMEM;
            }
        }
    }
    CettaDiskStoreStatus status=cetta_disk_store_view_clone(view,&source->view);
    if (status!=CETTA_STORE_OK) {
        indexes_free(source->indexes,index_count);cetta_disk_buffer_free(&source->prefix);free(source);return status;
    }
    source->count=count;
    atomic_init(&source->statistics.scans,0);atomic_init(&source->statistics.indexed,0);
    atomic_init(&source->statistics.candidates,0);atomic_init(&source->statistics.bytes,0);atomic_init(&source->statistics.overflow,false);
    target->match_backend.kind=SPACE_ENGINE_SOURCE;target->match_backend.ops=&source_ops;target->match_backend.source=source;
    space_read_view_invalidate(target);return CETTA_STORE_OK;
}
CettaDiskStoreStatus cetta_disk_space_capture(Space *target,const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t occurrence_count) {
    return cetta_disk_space_capture_indexed(target,view,prefix,occurrence_count,NULL,0);
}
CettaDiskStoreStatus cetta_disk_space_capture_point(Space *target,const CettaDiskStoreView *view,
        CettaDiskBytes key) {
    if (!view || (key.size && !key.data)) return CETTA_STORE_INVALID;
    CettaDiskBlob value;CettaDiskStatus found=cetta_disk_lookup(cetta_disk_store_view_root(view),key,&value);
    if (found!=CETTA_DISK_OK) return found==CETTA_DISK_ABSENT ? CETTA_STORE_ABSENT :
        found==CETTA_DISK_NOMEM ? CETTA_STORE_NOMEM : found==CETTA_DISK_IO ? CETTA_STORE_IO :
        found==CETTA_DISK_LIMIT ? CETTA_STORE_LIMIT : CETTA_STORE_CORRUPT;
    CettaDiskStoreStatus status=cetta_disk_space_capture_indexed(target,view,key,1,NULL,0);
    if (status==CETTA_STORE_OK) ((DiskSpace *)target->match_backend.source)->point=true;
    return status;
}
