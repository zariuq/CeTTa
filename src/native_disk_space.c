#include "native_disk_space.h"
#include "atom_wire.h"
#include "storage_census.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaDiskStoreView *view;
    CettaDiskBuffer prefix;
    uint64_t count;
} DiskSpace;

typedef struct {
    CettaDiskCursor *disk;
    CettaDiskBuffer prefix, key;
    Arena row;
    ArenaMark empty;
    Atom *atom, *identity;
    unsigned char epoch[16];
    uint64_t expected, seen;
    CettaIndex last;
    SpaceOccurrenceCursorStep terminal;
    bool done;
} DiskSpaceCursor;

static bool buffer_copy(CettaDiskBuffer *out,CettaDiskBytes bytes) {
    *out=(CettaDiskBuffer){0};
    if (!bytes.size) return true;
    out->data=malloc(bytes.size);
    if (!out->data) return false;
    memcpy(out->data,bytes.data,bytes.size);out->size=bytes.size;return true;
}
static void cursor_free(void *raw) {
    DiskSpaceCursor *cursor=raw;
    if (!cursor) return;
    cetta_disk_cursor_close(cursor->disk);
    cetta_disk_buffer_free(&cursor->prefix);cetta_disk_buffer_free(&cursor->key);
    arena_free(&cursor->row);free(cursor);
}
static bool cursor_new(const CettaDiskRoot *root,CettaDiskBytes prefix,
        const unsigned char epoch[16],uint64_t count,const CettaDiskBytes *after,
        void **out,SpaceOccurrenceCursorStep *failure) {
    *out=NULL;
    DiskSpaceCursor *cursor=calloc(1,sizeof(*cursor));
    if (!cursor) return false;
    arena_init_detached(&cursor->row);cursor->empty=arena_mark(&cursor->row);
    cursor->expected=count;memcpy(cursor->epoch,epoch,16);
    if (!buffer_copy(&cursor->prefix,prefix)) { cursor_free(cursor);return false; }
    CettaDiskBuffer upper={0};
    size_t end=prefix.size;
    while (end && prefix.data[end-1]==255) --end;
    if (end) {
        if (!buffer_copy(&upper,(CettaDiskBytes){prefix.data,end})) { cursor_free(cursor);return false; }
        ++upper.data[end-1];
    }
    CettaDiskBytes bound={upper.data,upper.size};
    const CettaDiskBytes *lower=after ? after : (prefix.size ? &prefix : NULL);
    CettaDiskStatus status=cetta_disk_cursor_open_range(root,lower,after!=NULL,
        end ? &bound : NULL,&cursor->disk);
    cetta_disk_buffer_free(&upper);
    if (status!=CETTA_DISK_OK) {
        if (failure) *failure=status==CETTA_DISK_NOMEM ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
        cursor_free(cursor);return false;
    }
    *out=cursor;return true;
}
static bool source_open(Space *space,Atom *pattern,void **out,
        SpaceOccurrenceCursorStep *failure) {
    (void)pattern;
    DiskSpace *source=space->match_backend.source;
    return source && cursor_new(cetta_disk_store_view_root(source->view),
        (CettaDiskBytes){source->prefix.data,source->prefix.size},
        cetta_disk_store_view_epoch(source->view),source->count,NULL,out,failure);
}
static bool cursor_clone(const void *raw,bool rewind,void **out) {
    const DiskSpaceCursor *source=raw;
    if (!source || !out) return false;
    CettaDiskBytes after={source->key.data,source->key.size};
    if (!cursor_new(cetta_disk_cursor_root(source->disk),
        (CettaDiskBytes){source->prefix.data,source->prefix.size},source->epoch,
        source->expected,!rewind && source->seen && !source->done ? &after : NULL,out,NULL)) return false;
    DiskSpaceCursor *copy=*out;
    if (!rewind) {
        copy->seen=source->seen;copy->last=source->last;copy->terminal=source->terminal;copy->done=source->done;
        if (!buffer_copy(&copy->key,after)) goto failed;
        if (source->atom && !(copy->atom=atom_deep_copy(&copy->row,source->atom))) goto failed;
        if (source->identity && !(copy->identity=atom_deep_copy(&copy->row,source->identity))) goto failed;
    }
    return true;
failed:
    cursor_free(copy);*out=NULL;return false;
}
static bool cursor_select(const void *raw,Atom *pattern,void **out,
        SpaceOccurrenceCursorStep *failure) {
    (void)pattern;
    const DiskSpaceCursor *source=raw;
    return source && cursor_new(cetta_disk_cursor_root(source->disk),
        (CettaDiskBytes){source->prefix.data,source->prefix.size},source->epoch,
        source->expected,NULL,out,failure);
}
static SpaceOccurrenceCursorStep stop(DiskSpaceCursor *cursor,SpaceOccurrenceCursorStep result) {
    cursor->done=true;cursor->terminal=result;return result;
}
static SpaceOccurrenceCursorStep cursor_next(void *raw,CettaIndex *index) {
    DiskSpaceCursor *cursor=raw;
    if (cursor->done) return cursor->terminal;
    arena_reset(&cursor->row,cursor->empty);cursor->atom=cursor->identity=NULL;
    cetta_disk_buffer_free(&cursor->key);
    CettaDiskBlob blob;
    CettaDiskStatus status=cetta_disk_cursor_next(cursor->disk,&cursor->key,&blob);
    if (status==CETTA_DISK_END) return stop(cursor,cursor->seen==cursor->expected
        ? SPACE_OCCURRENCE_CURSOR_END : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    if (status!=CETTA_DISK_OK || cursor->seen>=cursor->expected)
        return stop(cursor,SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    if (blob.size>CETTA_ATOM_WIRE_BYTES) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    unsigned char *body=malloc(blob.size ? (size_t)blob.size : 1);
    if (!body) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    size_t got=0;
    status=cetta_disk_blob_read(cetta_disk_cursor_root(cursor->disk),blob,0,body,(size_t)blob.size,&got);
    CettaAtomWireStatus decoded=status==CETTA_DISK_OK && got==blob.size
        ? cetta_atom_wire_decode(&cursor->row,body,got,&cursor->atom) : ATOM_WIRE_CORRUPT;
    free(body);
    if (decoded!=ATOM_WIRE_OK) return stop(cursor,
        decoded==ATOM_WIRE_NOMEM || decoded==ATOM_WIRE_LIMIT
            ? SPACE_OCCURRENCE_CURSOR_CAPACITY : SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
    static const char hex[]="0123456789abcdef";
    char epoch[33];
    for (size_t i=0;i<16;i++) { epoch[2*i]=hex[cursor->epoch[i]>>4];epoch[2*i+1]=hex[cursor->epoch[i]&15]; }
    epoch[32]=0;
    Atom *fields[]={atom_symbol(&cursor->row,"StoreOccurrenceV1"),atom_string(&cursor->row,epoch),
        atom_string_n(&cursor->row,(const char *)cursor->key.data,cursor->key.size)};
    cursor->identity=atom_expr(&cursor->row,fields,3);
    if (!cursor->identity || !cursor->atom) return stop(cursor,SPACE_OCCURRENCE_CURSOR_CAPACITY);
    *index=cursor->last=(CettaIndex)cursor->seen++;
    return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *cursor_atom(const void *raw,CettaIndex index) {
    const DiskSpaceCursor *cursor=raw;return cursor->seen && index==cursor->last ? cursor->atom : NULL;
}
static Atom *cursor_identity(const void *raw,CettaIndex index) {
    const DiskSpaceCursor *cursor=raw;return cursor->seen && index==cursor->last ? cursor->identity : NULL;
}
static void cursor_storage(const void *raw,CettaStorageCensus *census) {
    const DiskSpaceCursor *cursor=raw;
    if (!cursor || !cetta_storage_object(census,cursor,sizeof(*cursor),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,cursor->prefix.data,cursor->prefix.size,1,0);
    cetta_storage_array(census,cursor->key.data,cursor->key.size ? cursor->key.size : 1,1,0);
    cetta_disk_cursor_storage_census(cursor->disk,census);
    arena_storage_census(&cursor->row,census);
}
static void source_free(Space *space) {
    DiskSpace *source=space->match_backend.source;
    if (!source) return;
    cetta_disk_store_view_close(source->view);cetta_disk_buffer_free(&source->prefix);free(source);
    space->match_backend.source=NULL;
}
static uint64_t source_count(const Space *space) {
    const DiskSpace *source=space->match_backend.source;return source->count;
}
static void source_storage(const Space *space,CettaStorageCensus *census) {
    const DiskSpace *source=space->match_backend.source;
    if (!source || !cetta_storage_object(census,source,sizeof(*source),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,source->prefix.data,source->prefix.size,1,0);
    cetta_disk_store_view_storage_census(source->view,census);
}
static const SpaceSourceCursorOps cursor_ops={
    .clone=cursor_clone,.select=cursor_select,.next=cursor_next,.atom=cursor_atom,
    .identity=cursor_identity,.free=cursor_free,.storage=cursor_storage,
};
static const SpaceMatchBackendOps source_ops={
    .name="native-disk-atoms",.occurrence_ops=&cursor_ops,.occurrence_open=source_open,
    .logical_len=source_count,.free=source_free,.storage=source_storage,
};
CettaDiskStoreStatus cetta_disk_space_capture(Space *target,const CettaDiskStoreView *view,
        CettaDiskBytes prefix,uint64_t occurrence_count) {
    if (!target || !view || (prefix.size && !prefix.data) || target->kind!=SPACE_KIND_ATOM ||
        target->match_backend.kind!=SPACE_ENGINE_NATIVE || space_length64(target) ||
        target->overlay_base || space_has_dependencies(target) || target->module_own_view)
        return CETTA_STORE_INVALID;
    DiskSpace *source=calloc(1,sizeof(*source));
    if (!source) return CETTA_STORE_NOMEM;
    if (!buffer_copy(&source->prefix,prefix)) { free(source);return CETTA_STORE_NOMEM; }
    CettaDiskStoreStatus status=cetta_disk_store_view_clone(view,&source->view);
    if (status!=CETTA_STORE_OK) { cetta_disk_buffer_free(&source->prefix);free(source);return status; }
    source->count=occurrence_count;
    target->match_backend.kind=SPACE_ENGINE_SOURCE;
    target->match_backend.ops=&source_ops;target->match_backend.source=source;
    space_read_view_invalidate(target);
    return CETTA_STORE_OK;
}
