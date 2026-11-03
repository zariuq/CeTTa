#define _POSIX_C_SOURCE 200809L
#include "library_store.h"
#include "library.h"
#include "native_disk_store.h"
#include "native_disk_space.h"
#include "source_owner.h"
#include "remote_db.h"
#include "library_space_service.h"
#include "space_source_overlay.h"
#include "native_disk_query.h"
#include "native_handle.h"
#include "atom_wire.h"
#include "eval.h"
#include "shared_transition.h"
#include "petta_specializer.h"
#include "petta_semantics.h"
#include <limits.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *owner_kind="disk-store",*view_kind="disk-store-view";
static const char *reader_kind="disk-store-body",*scan_kind="disk-store-scan";
static const char *query_kind="disk-space-query";
static const char *batch_kind="disk-store-batch";
static const char *program_kind="program-watch";
static const char *compaction_kind="disk-store-compaction";
static const char *source_kind="occurrence-source",*candidate_kind="candidate-cursor",*remote_kind="remote-db",*remote_resource_kind="remote-resource";
static void free_remote(void *raw){cetta_remote_db_release(raw);}
static void free_remote_resource(void *raw){cetta_remote_resource_release(raw);}
typedef struct {CettaOccurrenceSource *source;CettaDiskStoreView *origin;CettaDiskBuffer metadata;} SourceLease;
static void free_source(void *raw) {
    SourceLease *lease=raw;if (!lease) return;
    cetta_occurrence_source_release(lease->source);cetta_disk_store_view_close(lease->origin);cetta_disk_buffer_free(&lease->metadata);free(lease);
}
static void source_storage(const void *raw,CettaStorageCensus *census) {
    const SourceLease *lease=raw;
    if (lease && cetta_storage_object(census,lease,sizeof(*lease),CETTA_STORAGE_BLOCK)) {
        cetta_occurrence_source_storage(lease->source,census);
        cetta_storage_array(census,lease->metadata.data,lease->metadata.size,1,0);
        if (lease->origin) cetta_disk_store_view_storage_census(lease->origin,census);
    }
}
static void free_candidate(void *raw) {cetta_candidate_cursor_close(raw);}
typedef struct {CettaDiskCompaction *job;CettaDiskStore *owner;} Compaction;
static void free_compaction(void *raw) {
    Compaction *c=raw;cetta_disk_store_compact_act_close(c->job);free(c);
}
static void compaction_storage(const void *raw,CettaStorageCensus *census) {
    const Compaction *c=raw;
    if (cetta_storage_object(census,c,sizeof(*c),CETTA_STORAGE_BLOCK)) cetta_disk_store_compact_act_storage(c->job,census);
}
static void owner_storage(const void *raw,CettaStorageCensus *census) {
    cetta_disk_store_storage(raw,census);
}
static void reader_storage(const void *raw,CettaStorageCensus *census) {
    cetta_disk_store_blob_reader_storage(raw,census);
}

/* A scoped callback stages its same-domain changes in the supplied Space.
 * Reentrant publication would escape its rollback boundary. Other callers
 * and other stores retain their existing coordinated-publication semantics. */
typedef struct EditDomain {
    const CettaDiskStoreView *view;
    struct EditDomain *previous;
} EditDomain;
static _Thread_local EditDomain *active_edit_domains;
static bool edit_view_active(const CettaDiskStoreView *view) {
    for (EditDomain *scope=active_edit_domains;scope;scope=scope->previous)
        if (cetta_disk_store_view_same_domain(scope->view,view)) return true;
    return false;
}
static bool edit_store_active(const CettaDiskStore *store) {
    for (EditDomain *scope=active_edit_domains;scope;scope=scope->previous)
        if (cetta_disk_store_has_domain(store,scope->view)) return true;
    return false;
}

/* A stored Space is an ordinary Space with arena-owned lifetime. It uses the
 * process-owned fallback universe for its empty native sidecar, not the
 * creating interpreter context. The occurrence source owns its actual data. */
typedef struct {
    Space space;
    atomic_size_t references;
    atomic_bool closed;
    CettaDiskBuffer metadata;
    unsigned char epoch[16];
    uint64_t revision;
} StoredSpace;
static void stored_space_close(StoredSpace *owned) {
    CETTA_SCOPED_SHARED_TRANSITION(publication);
    if (!atomic_exchange(&owned->closed, true)) space_free(&owned->space);
}
static void stored_space_retain(void *raw) { atomic_fetch_add(&((StoredSpace *)raw)->references, 1); }
static void stored_space_release(void *raw) {
    StoredSpace *owned = raw;
    if (atomic_fetch_sub(&owned->references, 1) == 1) {
        stored_space_close(owned); cetta_disk_buffer_free(&owned->metadata); free(owned);
    }
}
static bool stored_space_current(const void *raw) { return !atomic_load(&((const StoredSpace *)raw)->closed); }
static void stored_space_storage(const void *raw, CettaStorageCensus *census) {
    const StoredSpace *owned = raw;
    if (cetta_storage_object(census, owned, sizeof(*owned), CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census,owned->metadata.data,owned->metadata.size ? owned->metadata.size : 1,1,0);
        space_storage_census(&owned->space, census);
    }
}
static const SpaceOwnerOps stored_space_owner_ops = {
    .retain = stored_space_retain, .release = stored_space_release,
    .current = stored_space_current, .storage = stored_space_storage,
};
static StoredSpace *stored_space_new(const CettaDiskStoreView *view,CettaDiskBytes metadata) {
    CETTA_SCOPED_SHARED_TRANSITION(creation);
    StoredSpace *owned = calloc(1, sizeof(*owned)); if (!owned) return NULL;
    owned->metadata.data=malloc(metadata.size ? metadata.size : 1);
    if (!owned->metadata.data) { free(owned);return NULL; }
    if (metadata.size) memcpy(owned->metadata.data,metadata.data,metadata.size);
    owned->metadata.size=metadata.size;
    if (view) {
        memcpy(owned->epoch,cetta_disk_store_view_epoch(view),16);
        owned->revision=cetta_disk_store_view_revision(view);
    }
    atomic_init(&owned->references, 1); atomic_init(&owned->closed, false);
    space_init(&owned->space);
    owned->space.lifetime = (SpaceOwner){owned, &stored_space_owner_ops};
    return owned;
}
static StoredSpace *stored_space_arg(Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_SPACE || !atom->ground.ptr) return NULL;
    Space *space = atom->ground.ptr;
    return space->lifetime.ops == &stored_space_owner_ops ? space->lifetime.owner : NULL;
}
/* A stored-data range uses portable scalars and the existing term order.
 * It does not instantiate variables or evaluate callable-looking symbols. */
static bool scalar_value(const Atom *value) {
    if (!value) return false;
    if (value->kind==ATOM_SYMBOL) return true;
    if (value->kind!=ATOM_GROUNDED) return false;
    switch (value->ground.gkind) {
    case GV_INT: case GV_FLOAT: case GV_STRING: case GV_BOOL:
    case GV_BIGINT: case GV_RATIONAL: return true;
    default: return false;
    }
}
static bool scalar_bound(Atom *bound,const Atom **value,bool *exclusive) {
    *value=NULL;*exclusive=false;
    if (atom_is_symbol(bound,"NoBound")) return true;
    if (!bound || bound->kind!=ATOM_EXPR || bound->expr.len!=2) return false;
    bool strict=atom_is_symbol(bound->expr.elems[0],"Exclusive");
    if ((!strict && !atom_is_symbol(bound->expr.elems[0],"Inclusive")) ||
            !scalar_value(bound->expr.elems[1])) return false;
    *value=bound->expr.elems[1];*exclusive=strict;return true;
}
/* First signed-integer ordinal satisfying integer >/>= bound. None is the
 * conceptual ordinal2^64. At most65 comparisons; never round via double. */
static bool integer_cut(const Atom *bound,bool strict,bool *exists,uint64_t *ordinal) {
    uint64_t low=0,high=UINT64_MAX;*exists=false;*ordinal=0;
    for (;;) {
        uint64_t middle=low+(high-low)/2;
        Atom value={0};value.kind=ATOM_GROUNDED;value.ground.gkind=GV_INT;
        value.ground.ival=(int64_t)(middle^(UINT64_C(1)<<63));int order=0;
        if (!petta_semantics_term_compare(&value,bound,&order)) return false;
        if (order>0 || (!strict && order==0)) {
            *exists=true;*ordinal=middle;
            if (!middle) break;
            high=middle-1;
        } else {
            if (middle==UINT64_MAX) break;
            low=middle+1;
        }
        if (low>high) break;
    }
    return true;
}
static Atom *ordinal_key(Arena *arena,uint64_t ordinal) {
    unsigned char bytes[8];for(size_t i=8;i>0;i--){bytes[i-1]=(unsigned char)ordinal;ordinal>>=8;}
    return atom_string_n(arena,(const char *)bytes,8);
}
typedef struct {
    SpaceProgramToken token;
    PettaProgramReadBarrier *barrier;
    const CettaProfile *profile;
    CettaLanguageId language;
    uint64_t imports,admission,paths;
    /* A completed scoped edit validated its borrowed reads before sealing
     * this epoch. Publication checks it without retaining borrowed pointers. */
    bool memory_sealed;
    uint64_t memory_epoch;
} ProgramWatch;
static void free_program(void *p) {
    ProgramWatch *watch=p;petta_program_read_barrier_free(watch->barrier);free(watch);
}
typedef struct { CettaDiskStoreCursor *cursor; CettaDiskBuffer last; CettaDiskStoreBlob blob; uint64_t position; bool done; } Scan;
typedef struct { CettaDiskStoreScope *items; unsigned char **allocated; size_t count; } Scopes;

static void free_owner(void *p) { cetta_disk_store_close(p); }
static void free_view(void *p) { cetta_disk_store_view_close(p); }
static void view_storage(const void *p,CettaStorageCensus *census) { cetta_disk_store_view_storage_census(p,census); }
static void free_reader(void *p) { cetta_disk_store_blob_reader_close(p); }
static void free_query(void *p) { cetta_disk_query_close(p); }
static void query_storage(const void *p,CettaStorageCensus *census) { cetta_disk_query_storage_census(p,census); }
static void free_batch(void *p) { cetta_disk_store_batch_close(p); }
static void program_storage(const void *p,CettaStorageCensus *census) {
    const ProgramWatch *watch=p;
    if (watch && cetta_storage_object(census,p,sizeof(ProgramWatch),CETTA_STORAGE_BLOCK))
        petta_program_read_barrier_storage(watch->barrier,census);
}
static bool program_current(CettaLibraryContext *ctx,const ProgramWatch *watch) {
    /* Compare with the actual live root; never dereference a saved identity.
     * A retired/reinitialized root cannot revive this in-process observation. */
    bool valid=ctx && watch && watch->profile==ctx->session.profile &&
        watch->language==ctx->session.language_id && watch->imports==ctx->active_mask &&
        watch->admission==ctx->petta_admission_revision &&
        watch->paths==ctx->petta_library_paths.revision &&
        (!watch->memory_sealed || watch->memory_epoch==space_global_mutation_epoch()) &&
        space_program_token_matches_live_space(watch->token,eval_current_root_space()) &&
        petta_program_read_barrier_current(watch->barrier,eval_current_root_space());
    return valid;
}
static void free_scan(void *p) {
    Scan *scan=p; cetta_disk_store_cursor_close(scan->cursor); cetta_disk_buffer_free(&scan->last); free(scan);
}
static void scan_storage(const void *p,CettaStorageCensus *census) {
    const Scan *scan=p;
    if (!scan || !cetta_storage_object(census,scan,sizeof(*scan),CETTA_STORAGE_BLOCK)) return;
    cetta_disk_store_cursor_storage_census(scan->cursor,census);
    cetta_storage_object(census,scan->last.data,scan->last.size ? scan->last.size : 1,CETTA_STORAGE_BLOCK);
}
static Atom *refused(Arena *a,const char *why) {
    return atom_expr2(a,atom_symbol(a,"StoreRefused"),atom_symbol(a,why));
}
static Atom *status_value(Arena *a,CettaDiskStoreStatus status) {
    static const char *names[]={"ok","absent","invalid","busy","conflict","io","corrupt",
        "capacity","limit","unknown","version","recovery-required","ownership","progress"};
    if (status==CETTA_STORE_UNKNOWN) return atom_expr(a,(Atom *[]){atom_symbol(a,"StoreUncertain")},1);
    return refused(a,(unsigned)status<sizeof(names)/sizeof(*names) ? names[status] : "invalid");
}
static CettaDiskStoreStatus disk_status(CettaDiskStatus status) {
    switch (status) {
    case CETTA_DISK_OK: return CETTA_STORE_OK;
    case CETTA_DISK_ABSENT: return CETTA_STORE_ABSENT;
    case CETTA_DISK_NOMEM: return CETTA_STORE_NOMEM;
    case CETTA_DISK_CORRUPT: return CETTA_STORE_CORRUPT;
    case CETTA_DISK_LIMIT: return CETTA_STORE_LIMIT;
    case CETTA_DISK_IO: return CETTA_STORE_IO;
    default: return CETTA_STORE_INVALID;
    }
}
static bool string(Atom *value,CettaDiskBytes *bytes) {
    if (!value || value->kind!=ATOM_GROUNDED || value->ground.gkind!=GV_STRING) return false;
    *bytes=(CettaDiskBytes){(const unsigned char *)value->ground.sval,atom_string_len(value)}; return true;
}
static bool word(Atom *value,const char *text) {
    CettaDiskBytes bytes;
    return string(value,&bytes) && bytes.size==strlen(text) && !memcmp(bytes.data,text,bytes.size);
}
static bool number(Atom *value,uint64_t *out) {
    if (!value || value->kind!=ATOM_GROUNDED || value->ground.gkind!=GV_INT || value->ground.ival<0) return false;
    *out=(uint64_t)value->ground.ival; return true;
}
static Atom *own(CettaLibraryContext *ctx,Arena *a,const char *kind,void *p,CettaNativeHandleFreeFn release) {
    uint64_t id=0;
    if (!cetta_native_handle_alloc(ctx,kind,p,release,&id)) { release(p); return NULL; }
    CettaStorageVisitor visitor=kind==owner_kind ? owner_storage : kind==view_kind ? view_storage
        : kind==batch_kind ? cetta_disk_store_batch_storage_census
        : kind==program_kind ? program_storage
        : kind==scan_kind ? scan_storage
        : kind==reader_kind ? reader_storage : kind==compaction_kind ? compaction_storage : kind==source_kind ? source_storage
        : kind==candidate_kind ? cetta_candidate_cursor_storage : kind==remote_kind ? cetta_remote_db_storage
        : kind==remote_resource_kind ? cetta_remote_resource_storage : NULL;
    if (visitor && !cetta_native_handle_set_storage(ctx,kind,id,visitor)) {
        cetta_native_handle_close(ctx,kind,id);return NULL;
    }
    Atom *handle=cetta_native_handle_owned_atom(ctx,a,kind,id);
    if (!handle) cetta_native_handle_close(ctx,kind,id);
    return handle;
}
static Atom *own_query(CettaLibraryContext *ctx,Arena *a,CettaDiskQuery *query) {
    uint64_t id=0;
    if (!cetta_native_handle_alloc(ctx,query_kind,query,free_query,&id)) { free_query(query);return NULL; }
    if (!cetta_native_handle_set_storage(ctx,query_kind,id,query_storage)) {
        cetta_native_handle_close(ctx,query_kind,id);return NULL;
    }
    Atom *handle=cetta_native_handle_owned_atom(ctx,a,query_kind,id);
    if (!handle) cetta_native_handle_close(ctx,query_kind,id);
    return handle;
}
static Atom *unsigned_value(Arena *a,uint64_t value) {
    if (value<=INT64_MAX) return atom_int(a,(int64_t)value);
    char text[32];snprintf(text,sizeof(text),"%llu",(unsigned long long)value);
    return atom_bigint(a,text);
}
static Atom *view_epoch_value(Arena *a,const CettaDiskStoreView *view) {
    const unsigned char *epoch=cetta_disk_store_view_epoch(view);
    static const char digits[]="0123456789abcdef"; char text[33];
    for (size_t i=0;i<16;++i) { text[i*2]=digits[epoch[i]>>4]; text[i*2+1]=digits[epoch[i]&15]; }
    text[32]=0;
    return atom_string(a,text);
}
static Atom *view_value(CettaLibraryContext *ctx,Arena *a,CettaDiskStoreView *view,
        bool published,CettaDiskStore *owner) {
    Atom *epoch=view_epoch_value(a,view);
    uint64_t revision=cetta_disk_store_view_revision(view);
    Atom *handle=own(ctx,a,view_kind,view,free_view);
    if (!handle) {
        if (published) cetta_disk_store_require_recovery(owner);
        return published ? status_value(a,CETTA_STORE_UNKNOWN) : refused(a,"capacity");
    }
    Atom *fields[]={atom_symbol(a,"StoreSnapshot"),handle,epoch,unsigned_value(a,revision)};
    return atom_expr(a,fields,4);
}
static bool operations(Atom *list,CettaDiskOp **out,size_t *count) {
    *out=NULL; *count=0;
    if (!list || list->kind!=ATOM_EXPR || !list->expr.len || !atom_is_symbol(list->expr.elems[0],"ops")) return false;
    *count=list->expr.len-1;
    if (*count>SIZE_MAX/sizeof(**out)) return false;
    CettaDiskOp *ops=*count ? calloc(*count,sizeof(*ops)) : NULL;
    if (*count && !ops) return false;
    for (size_t i=0;i<*count;++i) {
        Atom *op=list->expr.elems[i+1];
        if (!op || op->kind!=ATOM_EXPR || op->expr.len<2 || !string(op->expr.elems[1],&ops[i].key)) goto failed;
        if (op->expr.len==2 && atom_is_symbol(op->expr.elems[0],"remove")) ops[i].remove=true;
        else if (!(op->expr.len==3 && atom_is_symbol(op->expr.elems[0],"put") && string(op->expr.elems[2],&ops[i].value))) goto failed;
    }
    *out=ops; return true;
failed:
    free(ops);return false;
}
static void scopes_free(Scopes *scopes) {
    for (size_t i=0;i<scopes->count;++i) free(scopes->allocated ? scopes->allocated[i] : NULL);
    free(scopes->allocated); free(scopes->items); *scopes=(Scopes){0};
}
static bool bound(Atom *value,CettaDiskBytes *bytes,bool *present) {
    if (atom_is_symbol(value,"NoBound")) { *present=false; return true; }
    *present=true; return string(value,bytes);
}
static bool scopes(Atom *list,const char *tag,Scopes *out) {
    *out=(Scopes){0};
    if (!list || list->kind!=ATOM_EXPR || !list->expr.len || !atom_is_symbol(list->expr.elems[0],tag)) return false;
    out->count=list->expr.len-1;
    if (out->count>SIZE_MAX/sizeof(*out->items) || out->count>SIZE_MAX/sizeof(*out->allocated)) goto failed;
    out->items=out->count ? calloc(out->count,sizeof(*out->items)) : NULL;
    out->allocated=out->count ? calloc(out->count,sizeof(*out->allocated)) : NULL;
    if (out->count && (!out->items || !out->allocated)) goto failed;
    for (size_t i=0;i<out->count;++i) {
        Atom *item=list->expr.elems[i+1]; CettaDiskStoreScope *scope=&out->items[i];
        if (!item || item->kind!=ATOM_EXPR) goto failed;
        if (item->expr.len==2 && atom_is_symbol(item->expr.elems[0],"point")) {
            scope->kind=CETTA_STORE_POINT;
            if (!string(item->expr.elems[1],&scope->lower)) goto failed;
        } else if (item->expr.len==3 && atom_is_symbol(item->expr.elems[0],"range")) {
            scope->kind=CETTA_STORE_RANGE;
            if (!bound(item->expr.elems[1],&scope->lower,&scope->has_lower) ||
                !bound(item->expr.elems[2],&scope->upper,&scope->has_upper)) goto failed;
        } else if (item->expr.len==2 && atom_is_symbol(item->expr.elems[0],"prefix")) {
            scope->kind=CETTA_STORE_RANGE; scope->has_lower=true;
            if (!string(item->expr.elems[1],&scope->lower)) goto failed;
            size_t at=scope->lower.size;
            while (at && scope->lower.data[at-1]==255) --at;
            if (at) {
                unsigned char *upper=malloc(at);
                if (!upper) goto failed;
                memcpy(upper,scope->lower.data,at); ++upper[at-1];
                out->allocated[i]=upper;scope->upper=(CettaDiskBytes){upper,at};scope->has_upper=true;
            }
        } else goto failed;
    }
    return true;
failed:
    scopes_free(out);return false;
}
static bool acquire(CettaLibraryContext *ctx,const char *kind,Atom *handle,
                    CettaNativeHandleUseMode mode,CettaNativeHandleUse *use) {
    return cetta_native_handle_acquire_owned(ctx,kind,handle,mode,use);
}
static Atom *candidate_status(Arena *a,CettaCandidateStatus status) {
    const char *name=status==CETTA_CANDIDATE_PROGRESS ? "StoreCandidatePaused"
        : status==CETTA_CANDIDATE_END ? "StoreCandidateEnd"
        : status==CETTA_CANDIDATE_CANCELLED ? "StoreCandidateCancelled"
        : status==CETTA_CANDIDATE_STALE ? "StoreCandidateStale" : NULL;
    return name ? atom_symbol(a,name) : atom_expr2(a,atom_symbol(a,"StoreCandidateFailed"),atom_symbol(a,cetta_candidate_status_name(status)));
}
static Atom *space_view(CettaLibraryContext *,Arena *,Atom **,uint32_t,bool,bool);
typedef struct RemoteEditDomain {
    CettaRemoteDb *db;struct RemoteEditDomain *previous;
} RemoteEditDomain;
static _Thread_local RemoteEditDomain *active_remote_edits;
static bool remote_edit_active(CettaRemoteDb *db) {
    for(RemoteEditDomain *d=active_remote_edits;d;d=d->previous)if(cetta_remote_db_same_domain(db,d->db))return true;
    return false;
}
static Atom *remote_status(Arena *a,CettaValueRpcStatus status) {
    return status==CETTA_RPC_UNCERTAIN ? atom_symbol(a,"StoreRemoteUncertain")
        : status==CETTA_RPC_UNAVAILABLE ? atom_symbol(a,"StoreRemoteUnavailable")
        : atom_expr2(a,atom_symbol(a,"StoreRemoteFailed"),atom_symbol(a,status==CETTA_RPC_CAPACITY ? "capacity" : status==CETTA_RPC_LIMIT ? "limit" : status==CETTA_RPC_REMOTE_ERROR ? "remote-error" : "invalid-response"));
}
static Atom *remote_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
    bool adopt=atom_is_symbol(head,"__cetta_lib_store_remote_adopt");
    bool publish=atom_is_symbol(head,"__cetta_lib_store_remote_publish");
    bool capture=atom_is_symbol(head,"__cetta_lib_store_remote_capture");
    bool related=atom_is_symbol(head,"__cetta_lib_store_remote_related");
    bool observation=atom_is_symbol(head,"__cetta_lib_store_remote_observation");
    bool context=atom_is_symbol(head,"__cetta_lib_store_remote_context");
    bool call=atom_is_symbol(head,"__cetta_lib_store_remote_call");
    bool start=atom_is_symbol(head,"__cetta_lib_store_remote_start");
    bool start_from=atom_is_symbol(head,"__cetta_lib_store_remote_start_from");
    bool start_capture=atom_is_symbol(head,"__cetta_lib_store_remote_source_start");
    bool use_resource=atom_is_symbol(head,"__cetta_lib_store_remote_use");
    if(!adopt && !capture && !context && !call && !publish && !related && !observation && !start && !start_from && !start_capture && !use_resource)return NULL;
    if(n!=(context || observation ? 1u : call || publish || start || start_from || start_capture || use_resource ? 3u : 2u))return refused(a,"remote-arguments");
    CETTA_SCOPED_SHARED_TRANSITION(guard);
    if(start_capture) {
        Space *base=resolve_space(eval_current_registry(),args[0]);CettaRemoteCapture *source=cetta_remote_capture_from_space(base);
        if(!source)return refused(a,"remote-capture-required");
        const char *operation=args[1]->kind==ATOM_SYMBOL ? atom_symbol_bytes(args[1]) : NULL;
        char *name=operation && !memchr(operation,0,atom_symbol_length(args[1])) ? strdup(operation) : NULL;
        if(!name){cetta_remote_capture_release(source);return refused(a,"remote-operation");}
        if(remote_edit_active(cetta_remote_capture_db(source)) && (!strcmp(name,"DbPublish") || !strcmp(name,"DbEnsure") ||
            !strcmp(name,"DbCompactOpen") || !strcmp(name,"DbImportOpen") || !strcmp(name,"DbImportFinish"))) {
            free(name);cetta_remote_capture_release(source);return refused(a,"write-during-space-edit");
        }
        Arena owned;arena_init_detached(&owned);Atom *argument=atom_deep_copy(&owned,args[2]);
        if(!argument){arena_free(&owned);free(name);cetta_remote_capture_release(source);return refused(a,"capacity");}
        CettaRemoteResource *opened=NULL;Atom *metadata=NULL;cetta_shared_transition_guard_leave(&guard);
        CettaValueRpcStatus status=cetta_remote_capture_start(source,name,argument,&owned,&opened,&metadata);
        cetta_shared_transition_guard_enter(&guard);Atom *result;
        if(status!=CETTA_RPC_OK)result=metadata ? atom_deep_copy(a,metadata) : remote_status(a,status);
        else {Atom *handle=own(ctx,a,remote_resource_kind,opened,free_remote_resource);
            result=handle ? atom_expr3(a,atom_symbol(a,"StoreRemoteResource"),handle,atom_deep_copy(a,metadata)) : refused(a,"capacity");}
        arena_free(&owned);free(name);cetta_remote_capture_release(source);return result;
    }
    if(related || observation) {
        Space *base=resolve_space(eval_current_registry(),args[0]);CettaRemoteCapture *origin=cetta_remote_capture_from_space(base);
        if(!origin)return refused(a,"remote-capture-required");
        Atom *result=NULL;
        if(observation)result=cetta_remote_capture_context(origin,a);
        else {
            Arena owned;arena_init_detached(&owned);Atom *collection=atom_deep_copy(&owned,args[1]);
            CettaRemoteCapture *source=NULL;cetta_shared_transition_guard_leave(&guard);
            CettaValueRpcStatus status=cetta_remote_capture_related(origin,collection,&source);
            cetta_shared_transition_guard_enter(&guard);
            if(status!=CETTA_RPC_OK)result=remote_status(a,status);
            else {
                StoredSpace *space=stored_space_new(NULL,(CettaDiskBytes){0});
                if(!space)result=refused(a,"capacity");
                else {
                    result=cetta_remote_capture_space(source,&space->space) ? atom_space(a,&space->space) : refused(a,"remote-space");
                    stored_space_release(space);
                }
            }
            cetta_remote_capture_release(source);arena_free(&owned);
        }
        cetta_remote_capture_release(origin);return result;
    }
    if(adopt) {
        uint64_t timeout;
        if(!number(args[1],&timeout))return refused(a,"remote-timeout");
        CettaIpcSession *session=cetta_space_service_session_acquire(ctx,args[0]);if(!session)return refused(a,"closed-connection");
        size_t maximum=cetta_library_value_byte_limit(ctx);cetta_shared_transition_guard_leave(&guard);
        CettaRemoteDb *db=NULL;CettaValueRpcStatus status=cetta_remote_db_open(session,maximum,timeout,&db);cetta_ipc_session_release(session);
        cetta_shared_transition_guard_enter(&guard);
        if(status!=CETTA_RPC_OK)return remote_status(a,status);
        Atom *metadata=cetta_remote_db_context(db,a),*handle=own(ctx,a,remote_kind,db,free_remote);
        return handle ? atom_expr3(a,atom_symbol(a,"StoreRemoteOpened"),handle,metadata) : refused(a,"capacity");
    }
    if(publish) {
        Space *space=resolve_space(eval_current_registry(),args[0]);CettaRemoteCapture *source=cetta_remote_capture_from_space(space);
        if(!source)return refused(a,"remote-capture-required");
        if(remote_edit_active(cetta_remote_capture_db(source))){cetta_remote_capture_release(source);return refused(a,"write-during-space-edit");}
        CettaNativeHandleUse watch_use={0};
        if(!acquire(ctx,program_kind,args[2],CETTA_NATIVE_HANDLE_USE_LIFETIME,&watch_use)){cetta_remote_capture_release(source);return refused(a,"closed-program-watch");}
        ProgramWatch *watch=cetta_native_handle_use_resource(&watch_use);CettaValueRpc *rpc=NULL;Atom *result=NULL;
        if(!watch->memory_sealed || !program_current(ctx,watch))result=refused(a,"program-invalidated");
        else {
            CettaValueRpcStatus status=cetta_remote_capture_prepare_publish(source,args[1],&rpc);
            if(status!=CETTA_RPC_PENDING)result=remote_status(a,status);
            else {
                cetta_shared_transition_guard_leave(&guard);status=cetta_value_rpc_await(rpc);cetta_shared_transition_guard_enter(&guard);
                result=status==CETTA_RPC_OK || status==CETTA_RPC_REMOTE_ERROR ? atom_deep_copy(a,cetta_value_rpc_result(rpc)) : remote_status(a,status);
            }
        }
        cetta_value_rpc_release(rpc);cetta_native_handle_use_release(&watch_use);cetta_remote_capture_release(source);return result;
    }
    CettaNativeHandleUse use={0};
    if(!acquire(ctx,use_resource || start_from ? remote_resource_kind : remote_kind,args[0],use_resource || start_from ? CETTA_NATIVE_HANDLE_USE_EXCLUSIVE : CETTA_NATIVE_HANDLE_USE_LIFETIME,&use))return refused(a,"closed-remote-db");
    CettaRemoteResource *resource=use_resource || start_from ? cetta_native_handle_use_resource(&use) : NULL;
    CettaRemoteDb *db=resource ? cetta_remote_resource_db(resource) : cetta_native_handle_use_resource(&use);Atom *result=NULL;
    if(context)result=cetta_remote_db_context(db,a);
    else {
        Arena owned;arena_init_detached(&owned);
        Atom *argument=atom_deep_copy(&owned,args[capture ? 1 : 2]);
        const char *operation=!capture && args[1]->kind==ATOM_SYMBOL ? atom_symbol_bytes(args[1]) : NULL;
        char *name=operation && !memchr(operation,0,atom_symbol_length(args[1])) ? strdup(operation) : NULL;
        if(!capture && !name)result=refused(a,"remote-operation");
        else if(!capture && remote_edit_active(db) && (!strcmp(name,"DbPublish") || !strcmp(name,"DbEnsure") || !strcmp(name,"DbCompactOpen") || !strcmp(name,"DbImportOpen") || !strcmp(name,"DbImportFinish")))result=refused(a,"write-during-space-edit");
        else {
            cetta_shared_transition_guard_leave(&guard);
            if(capture) {
                CettaRemoteCapture *source=NULL;CettaValueRpcStatus status=cetta_remote_db_capture(db,argument,&source);
                cetta_shared_transition_guard_enter(&guard);
                if(status!=CETTA_RPC_OK)result=remote_status(a,status);
                else {
                    StoredSpace *space=stored_space_new(NULL,(CettaDiskBytes){0});
                    if(!space)result=refused(a,"capacity");
                    else {
                        bool ok=cetta_remote_capture_space(source,&space->space);
                        result=ok ? atom_space(a,&space->space) : refused(a,"remote-space");stored_space_release(space);
                    }
                    cetta_remote_capture_release(source);
                }
            } else if(start || start_from) {
                CettaRemoteResource *opened=NULL;Atom *metadata=NULL;
                CettaValueRpcStatus status=start_from ? cetta_remote_resource_start(resource,name,argument,&owned,&opened,&metadata)
                    : cetta_remote_db_start(db,name,argument,&owned,&opened,&metadata);
                cetta_shared_transition_guard_enter(&guard);
                if(status!=CETTA_RPC_OK)result=metadata ? atom_deep_copy(a,metadata) : remote_status(a,status);
                else {
                    Atom *handle=own(ctx,a,remote_resource_kind,opened,free_remote_resource);
                    result=handle ? atom_expr3(a,atom_symbol(a,"StoreRemoteResource"),handle,atom_deep_copy(a,metadata)) : refused(a,"capacity");
                }
            } else {
                Atom *answer=NULL;CettaValueRpcStatus status=resource
                    ? cetta_remote_resource_call(resource,name,argument,&owned,&answer)
                    : cetta_remote_db_call(db,name,argument,&owned,&answer);
                cetta_shared_transition_guard_enter(&guard);
                result=status==CETTA_RPC_OK || status==CETTA_RPC_REMOTE_ERROR ? atom_deep_copy(a,answer) : remote_status(a,status);
            }
        }
        free(name);arena_free(&owned);
    }
    cetta_native_handle_use_release(&use);return result;
}
static Atom *source_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
    if(atom_is_symbol(head,"__cetta_lib_store_source_single")) {
        if(n!=2)return refused(a,"source-single-arguments");
        CETTA_SCOPED_SHARED_TRANSITION(guard);
        SourceLease *lease=calloc(1,sizeof(*lease));if(!lease)return refused(a,"capacity");
        CettaCandidateStatus status=cetta_occurrence_source_single(args[0],args[1],cetta_library_value_byte_limit(ctx),&lease->source);
        if(status!=CETTA_CANDIDATE_OK){free_source(lease);return candidate_status(a,status);}
        Atom *handle=own(ctx,a,source_kind,lease,free_source);
        return handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreSourceOpened"),handle,atom_int(a,1),
            unsigned_value(a,cetta_occurrence_source_limit(lease->source))},4) : refused(a,"capacity");
    }
    bool open=atom_is_symbol(head,"__cetta_lib_store_source_open");
    bool from_view=atom_is_symbol(head,"__cetta_lib_store_source_open_view");
    bool view=atom_is_symbol(head,"__cetta_lib_store_source_view");
    bool context=atom_is_symbol(head,"__cetta_lib_store_source_context");
    bool as_space=atom_is_symbol(head,"__cetta_lib_store_source_space");
    bool info=atom_is_symbol(head,"__cetta_lib_store_source_info");
    bool scan=atom_is_symbol(head,"__cetta_lib_store_source_scan");
    bool select=atom_is_symbol(head,"__cetta_lib_store_source_candidates");
    bool clone=atom_is_symbol(head,"__cetta_lib_store_candidate_clone");
    bool next=atom_is_symbol(head,"__cetta_lib_store_candidate_next");
    bool read=atom_is_symbol(head,"__cetta_lib_store_candidate_read");
    bool packet=atom_is_symbol(head,"__cetta_lib_store_candidate_packet");
    bool cancel=atom_is_symbol(head,"__cetta_lib_store_candidate_cancel");
    if (!open && !from_view && !view && !context && !as_space && !info && !scan && !select && !clone && !next && !read && !cancel && !packet) return NULL;
    if (n!=(open || view || context || as_space || info || scan || cancel ? 1u : from_view || select || clone ? 2u : next || packet ? 3u : 5u)) return refused(a,"candidate-arguments");
    if (open || from_view) {
        CettaNativeHandleUse origin={0};Atom *temporary=NULL;
        if (from_view) {
            if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&origin)) return refused(a,"closed-view");
            temporary=space_view(ctx,a,args,n,false,false);
            if (!temporary || temporary->kind!=ATOM_EXPR || temporary->expr.len!=4 || !atom_is_symbol(temporary->expr.elems[0],"StoreSpace")) {
                cetta_native_handle_use_release(&origin);return temporary ? temporary : refused(a,"source-view");
            }
        }
        CettaSharedTransitionGuard guard={0};cetta_shared_transition_guard_enter(&guard);
        Space *space=resolve_space(eval_current_registry(),from_view ? temporary->expr.elems[1] : args[0]);
        if (!space || !space_owner_is_current(space)) {
            cetta_shared_transition_guard_leave(&guard);cetta_native_handle_use_release(&origin);return refused(a,"source-space-required");
        }
        Arena retained;arena_init_detached(&retained);(void)atom_space(&retained,space);
        size_t maximum=cetta_library_value_byte_limit(ctx);
        SourceLease *lease=calloc(1,sizeof(*lease));CettaCandidateStatus status=lease ? CETTA_CANDIDATE_OK : CETTA_CANDIDATE_CAPACITY;
        if (lease && from_view) {
            CettaDiskBytes metadata={0};
            if (!string(args[1],&metadata)) status=CETTA_CANDIDATE_INVALID;
            else if (metadata.size) {
                lease->metadata.data=malloc(metadata.size);
                if (!lease->metadata.data) status=CETTA_CANDIDATE_CAPACITY;
                else {memcpy(lease->metadata.data,metadata.data,metadata.size);lease->metadata.size=metadata.size;}
            }
        }
        cetta_shared_transition_guard_leave(&guard);
        if (status==CETTA_CANDIDATE_OK) status=cetta_occurrence_source_open(space,maximum,&lease->source);
        CettaDiskStoreStatus cloned=CETTA_STORE_OK;
        if (status==CETTA_CANDIDATE_OK && from_view) cloned=cetta_disk_store_view_clone(cetta_native_handle_use_resource(&origin),&lease->origin);
        cetta_shared_transition_guard_enter(&guard);arena_free(&retained);Atom *result;
        if (from_view) stored_space_close(stored_space_arg(temporary->expr.elems[1]));
        cetta_native_handle_use_release(&origin);
        if (status!=CETTA_CANDIDATE_OK || cloned!=CETTA_STORE_OK) {
            free_source(lease);result=cloned!=CETTA_STORE_OK ? status_value(a,cloned) : candidate_status(a,status);
        } else {
            uint64_t count=cetta_occurrence_source_count(lease->source);size_t limit=cetta_occurrence_source_limit(lease->source);
            Atom *handle=own(ctx,a,source_kind,lease,free_source);
            result=handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreSourceOpened"),handle,unsigned_value(a,count),unsigned_value(a,limit)},4) : refused(a,"capacity");
        }
        cetta_shared_transition_guard_leave(&guard);return result;
    }
    bool source_op=info || scan || select || view || context || as_space;
    CettaNativeHandleUse use={0};
    if (!acquire(ctx,source_op ? source_kind : candidate_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy-source");
    void *raw=cetta_native_handle_use_resource(&use);Atom *result=NULL;
    SourceLease *lease=source_op ? raw : NULL;
    if (info) result=atom_expr3(a,atom_symbol(a,"StoreSourceInfo"),unsigned_value(a,cetta_occurrence_source_count(lease->source)),unsigned_value(a,cetta_occurrence_source_limit(lease->source)));
    else if (context) {
        result=lease->origin ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreSourceContextV1"),view_epoch_value(a,lease->origin),
            unsigned_value(a,cetta_disk_store_view_revision(lease->origin)),atom_string_n(a,(const char *)lease->metadata.data,lease->metadata.size)},4)
            : atom_symbol(a,"StoreSourceHistoricalV1");
    }
    else if (view) {
        CettaDiskStoreView *copy=NULL;
        if (!lease->origin) result=refused(a,"source-has-no-publication-view");
        else {
            CettaDiskStoreStatus status=cetta_disk_store_view_clone(lease->origin,&copy);
            result=status==CETTA_STORE_OK ? view_value(ctx,a,copy,false,NULL) : status_value(a,status);
        }
    } else if (as_space) {
        StoredSpace *owned=stored_space_new(NULL,(CettaDiskBytes){0});
        if (!owned) result=refused(a,"capacity");
        else {
            bool ok=cetta_occurrence_source_space(lease->source,&owned->space);
            result=ok ? atom_space(a,&owned->space) : refused(a,"source-space");stored_space_release(owned);
        }
    }
    else if (scan || select || clone) {
        CettaCandidateCursor *cursor=NULL;CettaCandidateStatus status;
        if (clone) {
            bool rewind=atom_is_symbol(args[1],"rewind");
            status=rewind || atom_is_symbol(args[1],"current") ? cetta_candidate_cursor_clone(raw,rewind,&cursor) : CETTA_CANDIDATE_INVALID;
        } else status=cetta_candidate_cursor_open(lease->source,select ? args[1] : NULL,&cursor);
        if (status!=CETTA_CANDIDATE_OK) result=candidate_status(a,status);
        else {
            uint64_t sequence=cetta_candidate_cursor_sequence(cursor);Atom *handle=own(ctx,a,candidate_kind,cursor,free_candidate);
            result=handle ? atom_expr3(a,atom_symbol(a,"StoreCandidateOpened"),handle,unsigned_value(a,sequence)) : refused(a,"capacity");
        }
    } else if (cancel) result=candidate_status(a,cetta_candidate_cursor_cancel(raw));
    else if (next) {
        uint64_t sequence,work;
        if (!number(args[1],&sequence) || !number(args[2],&work)) result=refused(a,"candidate-sequence");
        else {
            CettaCandidateRow row;CettaCandidateStatus status=cetta_candidate_cursor_next(raw,sequence,work,&row);
            result=status==CETTA_CANDIDATE_ROW ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreCandidateRow"),
                unsigned_value(a,row.sequence),unsigned_value(a,row.body_bytes),unsigned_value(a,row.identity_bytes)},4) : candidate_status(a,status);
        }
    } else if (packet) {
        uint64_t sequence;bool identity=atom_is_symbol(args[2],"identity");
        if (!number(args[1],&sequence) || (!identity && !atom_is_symbol(args[2],"body"))) result=refused(a,"candidate-packet");
        else {
            CettaCandidateRow row;CettaCandidateStatus status=cetta_candidate_cursor_next(raw,sequence,0,&row);
            if (status!=CETTA_CANDIDATE_ROW) result=candidate_status(a,status);
            else {
                uint64_t expected=identity ? row.identity_bytes : row.body_bytes;
                const unsigned char *bytes=NULL;size_t size=0;
                if (expected>cetta_library_value_byte_limit(ctx)) result=candidate_status(a,CETTA_CANDIDATE_LIMIT);
                else {
                    status=cetta_candidate_cursor_read(raw,sequence,identity,0,(size_t)expected,&bytes,&size);
                    result=status==CETTA_CANDIDATE_OK && size==expected ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreCandidatePacket"),
                        unsigned_value(a,sequence),atom_symbol(a,identity ? "identity" : "body"),atom_string_n(a,(const char *)bytes,size)},4)
                        : candidate_status(a,status==CETTA_CANDIDATE_OK ? CETTA_CANDIDATE_INCOMPLETE : status);
                }
            }
        }
    } else {
        uint64_t sequence,offset,maximum;bool identity=atom_is_symbol(args[2],"identity");
        if (!number(args[1],&sequence) || !number(args[3],&offset) || !number(args[4],&maximum) ||
            (!identity && !atom_is_symbol(args[2],"body")) || maximum>SIZE_MAX) result=refused(a,"candidate-chunk");
        else {
            size_t allowance=cetta_library_value_byte_limit(ctx)/4;
            if (maximum>allowance) maximum=allowance;
            const unsigned char *bytes=NULL;size_t size=0;
            CettaCandidateStatus status=cetta_candidate_cursor_read(raw,sequence,identity,offset,(size_t)maximum,&bytes,&size);
            result=status==CETTA_CANDIDATE_OK ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreCandidateChunk"),unsigned_value(a,sequence),
                atom_symbol(a,identity ? "identity" : "body"),unsigned_value(a,offset),atom_string_n(a,(const char *)bytes,size)},5) : candidate_status(a,status);
        }
    }
    cetta_native_handle_use_release(&use);return result;
}
static Atom *space_view(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n,bool streaming,bool staging) {
    CettaDiskBytes key;
    if (n!=(streaming ? 4u : 2u) || !string(args[1],&key)) return refused(a,"invalid-space-view");
    /* Other dialects have different Space discovery/execution paths. Keep
     * their public admission explicit until those paths are qualified. */
    if (ctx->session.language_id!=CETTA_LANGUAGE_PETTA)
        return refused(a,"space-language");
    /* Source values require a qualified owning evaluator boundary.
     * An unqualified transactional payload must not acquire a live foreign
     * capability by receiving a raw Space pointer. */
    Arena *storage=eval_current_persistent_arena();
    if (!streaming && (!storage || eval_payload_transactional() || ctx->term_universe.persistent_arena!=storage))
        return refused(a,"space-owner-context");
    CettaNativeHandleUse use={0};
    if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
    CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
    CettaDiskStoreBlob blob;const CettaDiskStoreView *root=view;
    CettaDiskStatus read=cetta_disk_store_lookup(root,key,&blob);
    Atom *result=NULL;
    if (read!=CETTA_DISK_OK) { result=status_value(a,disk_status(read));goto done; }
    if (blob.size>cetta_library_value_byte_limit(ctx)) { result=refused(a,"space-metadata-limit");goto done; }
    unsigned char *body=malloc(blob.size ? (size_t)blob.size : 1);size_t got=0;
    if (!body) { result=refused(a,"capacity");goto done; }
    read=cetta_disk_store_blob_read(root,blob,0,body,(size_t)blob.size,&got);
    Arena metadata;arena_init_detached(&metadata);Atom *record=NULL;
    CettaAtomWireStatus decoded=read==CETTA_DISK_OK && got==blob.size
        ? cetta_atom_wire_decode_bounded(&metadata,body,got,cetta_library_value_byte_limit(ctx),&record) : ATOM_WIRE_CORRUPT;
    free(body);
    CettaDiskBytes prefix;uint64_t count=0;
    bool version2=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV2");
    bool version3=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV3");
    bool version4=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV4");
    bool valid=decoded==ATOM_WIRE_OK && record && record->kind==ATOM_EXPR &&
        ((record->expr.len==3 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV1")) || version2 || version3 || version4) && string(record->expr.elems[1],&prefix) &&
        prefix.size && prefix.size<=cetta_disk_store_view_limits(root).key_bytes && number(record->expr.elems[2],&count) &&
        !(key.size>=prefix.size && !memcmp(key.data,prefix.data,prefix.size));
    CettaDiskSpaceIndex *indexes=NULL;size_t index_count=0;
    if (valid && (version2 || version3 || version4)) {
        Atom *items=record->expr.elems[3];valid=items->kind==ATOM_EXPR;
        if (valid && items->expr.len) {
            size_t declared=items->expr.len;indexes=calloc(declared,sizeof(*indexes));
            if (!indexes) { result=refused(a,"capacity");arena_free(&metadata);goto done; }
            for (size_t i=0;valid && i<declared;i++) {
                Atom *item=items->expr.elems[i];uint64_t arity=0,position=0;CettaDiskBytes index_prefix={0};
                bool exact=item->kind==ATOM_EXPR && item->expr.len==4 && atom_is_symbol(item->expr.elems[0],"StoreSpaceIndexV1");
                bool range=version4 && item->kind==ATOM_EXPR && item->expr.len==4 && atom_is_symbol(item->expr.elems[0],"StoreSpaceRangeIndexV1");
                valid=(exact || range) &&
                    number(item->expr.elems[1],&arity) && arity && arity<=UINT32_MAX &&
                    number(item->expr.elems[2],&position) && position<arity && string(item->expr.elems[3],&index_prefix) &&
                    index_prefix.size && index_prefix.size<=cetta_disk_store_view_limits(root).key_bytes &&
                    !(index_prefix.size>=prefix.size && !memcmp(index_prefix.data,prefix.data,prefix.size)) &&
                    !(prefix.size>=index_prefix.size && !memcmp(prefix.data,index_prefix.data,index_prefix.size));
                for (size_t j=0;valid && j<i;j++) {
                    Atom *previous=items->expr.elems[j];CettaDiskBytes previous_prefix;
                    valid=string(previous->expr.elems[3],&previous_prefix);
                    if (valid && ((index_prefix.size>=previous_prefix.size && !memcmp(index_prefix.data,previous_prefix.data,previous_prefix.size)) ||
                        (previous_prefix.size>=index_prefix.size && !memcmp(previous_prefix.data,index_prefix.data,index_prefix.size)) ||
                        (atom_eq(item->expr.elems[0],previous->expr.elems[0]) && atom_eq(item->expr.elems[1],previous->expr.elems[1]) &&
                         atom_eq(item->expr.elems[2],previous->expr.elems[2])))) valid=false;
                }
                /* Range postings are candidate domains for explicit range
                 * queries, not a new equality normalization for match. */
                if (valid && exact) indexes[index_count++]=(CettaDiskSpaceIndex){(uint32_t)arity,(uint32_t)position,index_prefix};
            }
        }
    }
    if (!valid) result=refused(a,read!=CETTA_DISK_OK ? "space-metadata-read" : "space-metadata-format");
    else if (streaming) {
        CettaDiskQuery *query=NULL;
        /* V2's historical MeTTa projection could interpret Boolean-like
         * fields and leave incomplete postings. Keep its facts readable by
         * the complete scan; V3/V4 declare data-preserving exact projections.
         * V4 range descriptors are used only by explicit range queries. */
        CettaDiskStoreStatus status=cetta_disk_query_open_indexed_bounded(view,prefix,count,indexes,version3 || version4 ? index_count : 0,args[2],args[3],cetta_library_value_byte_limit(ctx),&query);
        if (status!=CETTA_STORE_OK) result=status_value(a,status);
        else {
            Atom *handle=own_query(ctx,a,query);
            result=handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryOpened"),handle,
                view_epoch_value(a,view),unsigned_value(a,cetta_disk_store_view_revision(view))},4) : refused(a,"capacity");
        }
    } else {
        StoredSpace *owned=stored_space_new(view,key);
        if (!owned) { result=refused(a,"capacity");free(indexes);arena_free(&metadata);goto done; }
        Space *base=staging ? malloc(sizeof(*base)) : &owned->space;
        if (!base) { stored_space_release(owned);result=refused(a,"capacity");free(indexes);arena_free(&metadata);goto done; }
        if (staging) {
            CETTA_SCOPED_SHARED_TRANSITION(creation);
            space_init(base);
        }
        CettaDiskStoreStatus status=cetta_disk_space_capture_indexed_bounded(base,view,prefix,count,indexes,version3 || version4 ? index_count : 0,cetta_library_value_byte_limit(ctx));
        if (status==CETTA_STORE_OK && staging &&
                !space_source_overlay_init_owned(&owned->space,base,cetta_library_value_byte_limit(ctx))) status=CETTA_STORE_NOMEM;
        if (status!=CETTA_STORE_OK) {
            if (staging) { space_free(base);free(base); }
            result=status_value(a,status);
        } else {
            Atom *value=atom_space(a,&owned->space);
            Atom *fields[]={atom_symbol(a,"StoreSpace"),value,view_epoch_value(a,view),
                unsigned_value(a,cetta_disk_store_view_revision(view))};
            result=value ? atom_expr(a,fields,4) : refused(a,"capacity");
        }
        stored_space_release(owned);
    }
    free(indexes);arena_free(&metadata);
done:
    cetta_native_handle_use_release(&use);return result;
}

typedef struct {
    Space *workspace;
    SpaceReadToken *items;
    size_t count,capacity;
    Arena pins;
} EditReads;

static bool edit_observe(void *raw,SpaceReadToken read) {
    EditReads *reads=raw;
    if (read.space==reads->workspace) return true;
    for (size_t i=0;i<reads->count;i++)
        if (reads->items[i].space==read.space)
            return space_read_token_is_current(reads->items[i]);
    if (reads->count==reads->capacity) {
        size_t maximum=SIZE_MAX/sizeof(*reads->items);
        if (reads->capacity==maximum) return false;
        size_t capacity=reads->capacity ? reads->capacity>maximum/2 ? maximum : reads->capacity*2 : 8;
        SpaceReadToken *items=realloc(reads->items,capacity*sizeof(*items));
        if (!items) return false;
        reads->items=items;reads->capacity=capacity;
    }
    if (!space_retain_in_arena(read.space,&reads->pins)) return false;
    reads->items[reads->count++]=read;return true;
}

/* Execute one callable against an owned edit workspace. The caller's active
 * evaluator owns borrowed ordinary Spaces throughout the callback. Owned
 * source Spaces also get lifetime pins. No borrowed read token escapes:
 * validate it here, then seal the global mutation epoch in the existing
 * program watch. The final guarded commit checks that epoch and the durable
 * revision in one physical publication interval. */
static Atom *execute_space_edit(CettaLibraryContext *ctx,Arena *a,StoredSpace *owned,Atom *value,Atom *body,ProgramWatch *watch) {
    EditReads reads={.workspace=&owned->space};arena_init_detached(&reads.pins);
    CettaEvalReadScope scope={.observe=edit_observe,.context=&reads};
    Atom **values=NULL,*error=NULL;size_t count=0;
    Atom *call=atom_expr2(a,body,value);
    eval_read_scope_enter(&scope);
    bool complete=call && eval_interaction_values(eval_current_root_space(),a,call,&values,&count,&error);
    eval_read_scope_leave(&scope);
    CettaSharedTransitionGuard publication={0};cetta_shared_transition_guard_enter(&publication);
    bool alive=stored_space_current(owned);
    bool current=alive && !scope.incomplete && program_current(ctx,watch);
    for (size_t i=0;current && i<reads.count;i++) current=space_read_token_is_current(reads.items[i]);
    Atom *result=NULL,*added=NULL,*removed=NULL;
    if (!complete) result=error ? atom_expr2(a,atom_symbol(a,"StoreSpaceFailed"),error) : refused(a,"edit-incomplete");
    else if (!alive) result=refused(a,"edit-workspace-closed");
    else if (!current) result=refused(a,"edit-read-invalidated");
    else if (!count) result=atom_symbol(a,"StoreSpaceAborted");
    else {
        for (size_t i=0;i<count;i++) if (atom_is_error(values[i])) {
            result=atom_expr2(a,atom_symbol(a,"StoreSpaceFailed"),values[i]);break;
        }
        if (!result && count<=UINT32_MAX && space_source_overlay_changes(&owned->space,a,&added,&removed)) {
            Atom *answers=atom_expr(a,values,(CettaExprLen)count);
            if (answers) result=atom_expr(a,(Atom *[]){atom_symbol(a,"StoreSpaceEdited"),answers,added,removed},4);
        }
    }
    stored_space_close(owned);arena_free(&reads.pins);free(reads.items);free(values);
    if (result && atom_head_symbol_id(result)==symbol_intern_cstr(g_symbols,"StoreSpaceEdited")) {
        watch->memory_epoch=space_global_mutation_epoch();watch->memory_sealed=true;
    }
    cetta_shared_transition_guard_leave(&publication);
    return result ? result : refused(a,"capacity");
}

static Atom *space_edit(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n) {
    if (n!=4) return refused(a,"invalid-space-edit");
    CettaNativeHandleUse use={0};
    if (!acquire(ctx,program_kind,args[3],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use))
        return refused(a,"closed-or-busy-program-watch");
    ProgramWatch *watch=cetta_native_handle_use_resource(&use);
    if (watch->memory_sealed || !program_current(ctx,watch)) {
        cetta_native_handle_use_release(&use);return refused(a,"program-invalidated");
    }
    CettaNativeHandleUse view_use={0};
    if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view_use)) {
        cetta_native_handle_use_release(&use);return refused(a,"closed-view");
    }
    const CettaDiskStoreView *view=cetta_native_handle_use_resource(&view_use);
    if (edit_view_active(view)) {
        cetta_native_handle_use_release(&view_use);cetta_native_handle_use_release(&use);
        return refused(a,"nested-space-edit");
    }
    Atom *opened=space_view(ctx,a,args,2,false,true);
    if (!opened || opened->kind!=ATOM_EXPR || opened->expr.len!=4 ||
            !atom_is_symbol(opened->expr.elems[0],"StoreSpace")) {
        cetta_native_handle_use_release(&view_use);cetta_native_handle_use_release(&use);return opened;
    }
    Atom *value=opened->expr.elems[1];StoredSpace *owned=stored_space_arg(value);
    if (!owned) { cetta_native_handle_use_release(&view_use);cetta_native_handle_use_release(&use);return refused(a,"invalid-workspace"); }
    EditDomain domain={.view=view,.previous=active_edit_domains};active_edit_domains=&domain;
    Atom *result=execute_space_edit(ctx,a,owned,value,args[2],watch);
    active_edit_domains=domain.previous;
    cetta_native_handle_use_release(&view_use);cetta_native_handle_use_release(&use);
    return result ? result : refused(a,"capacity");
}

static Atom *source_edit(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n) {
    if(n!=3)return refused(a,"source-edit-arguments");
    CettaNativeHandleUse use={0};
    if(!acquire(ctx,program_kind,args[2],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use))return refused(a,"closed-or-busy-program-watch");
    ProgramWatch *watch=cetta_native_handle_use_resource(&use);
    if(watch->memory_sealed || !program_current(ctx,watch)){cetta_native_handle_use_release(&use);return refused(a,"program-invalidated");}
    CettaSharedTransitionGuard guard={0};cetta_shared_transition_guard_enter(&guard);
    Space *base=resolve_space(eval_current_registry(),args[0]);CettaRemoteCapture *capture=cetta_remote_capture_from_space(base);
    if(!base || !space_owner_is_current(base) || base->match_backend.kind!=SPACE_ENGINE_SOURCE || (capture && remote_edit_active(cetta_remote_capture_db(capture)))) {
        cetta_remote_capture_release(capture);cetta_shared_transition_guard_leave(&guard);cetta_native_handle_use_release(&use);return refused(a,"nested-or-invalid-source-edit");
    }
    StoredSpace *owned=stored_space_new(NULL,(CettaDiskBytes){0});Atom *result=NULL;
    if(owned){owned->space.kind=base->kind;space_set_strict(&owned->space,base->strict);}
    if(!owned)result=refused(a,"capacity");
    else if(!base->match_backend.ops || !base->match_backend.ops->overlay_local ||
            !base->match_backend.ops->overlay_local(&owned->space,base))result=refused(a,"source-workspace");
    else {
        Atom *value=atom_space(a,&owned->space);RemoteEditDomain domain={.db=capture ? cetta_remote_capture_db(capture) : NULL,.previous=active_remote_edits};
        if(capture)active_remote_edits=&domain;
        cetta_shared_transition_guard_leave(&guard);result=execute_space_edit(ctx,a,owned,value,args[1],watch);cetta_shared_transition_guard_enter(&guard);
        if(capture)active_remote_edits=domain.previous;
    }
    if(owned)stored_space_release(owned);
    cetta_remote_capture_release(capture);cetta_shared_transition_guard_leave(&guard);cetta_native_handle_use_release(&use);return result;
}
static Atom *compaction_progress(Arena *a,const CettaDiskCompaction *job) {
    CettaDiskCompactionInfo info;
    if (!cetta_disk_store_compact_act_info(job,&info)) return refused(a,"compaction-info");
    static const char *phases[]={"copy","assemble","validate","install","complete"};
    Atom *fields[]={atom_symbol(a,"StoreCompactionInfo"),
        atom_symbol(a,info.phase<5 ? phases[info.phase] : "invalid"),unsigned_value(a,info.source_revision),
        unsigned_value(a,info.records),unsigned_value(a,info.bytes_read),unsigned_value(a,info.output_bytes)};
    return atom_expr(a,fields,6);
}
Atom *cetta_store_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
    if (!ctx || !a || !head) return NULL;
    if (atom_is_symbol(head,"__cetta_lib_store_view_domain")) {
        CettaNativeHandleUse use={0};uintmax_t device,inode;
        if (n!=1 || !acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        bool ready=cetta_disk_store_view_domain(cetta_native_handle_use_resource(&use),&device,&inode);
        char name[4*sizeof(uintmax_t)+3];
        if (ready) snprintf(name,sizeof(name),"%jx:%jx",device,inode);
        cetta_native_handle_use_release(&use);
        return ready ? atom_expr2(a,atom_symbol(a,"StoreDomainV1"),atom_string(a,name)) : refused(a,"view-domain");
    }
    if(atom_is_symbol(head,"__cetta_lib_store_source_edit"))return source_edit(ctx,a,args,n);
    Atom *source_result=source_dispatch(ctx,a,head,args,n);if (source_result) return source_result;
    Atom *remote_result=remote_dispatch(ctx,a,head,args,n);if(remote_result)return remote_result;
    if (atom_is_symbol(head,"__cetta_lib_store_scalar_order")) {
        int order=0;
        if (n!=2 || !scalar_value(args[0]) || !scalar_value(args[1]) ||
                !petta_semantics_term_compare(args[0],args[1],&order)) return refused(a,"invalid-scalar");
        return atom_expr2(a,atom_symbol(a,"StoreScalarOrderV1"),atom_int(a,order<0 ? -1 : order>0 ? 1 : 0));
    }
    bool scalar_range=atom_is_symbol(head,"__cetta_lib_store_scalar_range");
    bool integer_window=atom_is_symbol(head,"__cetta_lib_store_integer_window");
    if (scalar_range || integer_window) {
        const Atom *lower=NULL,*upper=NULL;bool lo_strict=false,hi_strict=false;
        if (n!=(scalar_range ? 3u : 2u) ||
                !scalar_bound(args[scalar_range ? 1 : 0],&lower,&lo_strict) ||
                !scalar_bound(args[scalar_range ? 2 : 1],&upper,&hi_strict)) return refused(a,"invalid-scalar-bound");
        if (scalar_range) {
            int lo=0,hi=0;bool inside=scalar_value(args[0]);
            if (inside && lower) {
                if (!petta_semantics_term_compare(args[0],lower,&lo)) return refused(a,"invalid-scalar-order");
                inside=lo>0 || (!lo_strict && !lo);
            }
            if (inside && upper) {
                if (!petta_semantics_term_compare(args[0],upper,&hi)) return refused(a,"invalid-scalar-order");
                inside=hi<0 || (!hi_strict && !hi);
            }
            return atom_symbol(a,inside ? "StoreScalarWithinV1" : "StoreScalarOutsideV1");
        }
        uint64_t first=0,last=0;bool has_first=true,has_last=false;
        if ((lower && !integer_cut(lower,lo_strict,&has_first,&first)) ||
                (upper && !integer_cut(upper,!hi_strict,&has_last,&last))) return refused(a,"invalid-scalar-order");
        if (!has_first || (has_last && first>=last)) return atom_symbol(a,"StoreIntegerWindowEmptyV1");
        return atom_expr3(a,atom_symbol(a,"StoreIntegerWindowV1"),ordinal_key(a,first),
            has_last ? ordinal_key(a,last) : atom_symbol(a,"NoBound"));
    }
    bool program_open=atom_is_symbol(head,"__cetta_lib_store_program_watch_open");
    bool program_clone=atom_is_symbol(head,"__cetta_lib_store_program_watch_clone");
    bool program_check=atom_is_symbol(head,"__cetta_lib_store_program_watch_check");
    if (program_open || program_clone || program_check) {
        if (n!=(program_open ? 0u : 1u)) return refused(a,"invalid-program-watch");
        CETTA_SCOPED_SHARED_TRANSITION(program_observation);
        ProgramWatch captured={0};CettaNativeHandleUse use={0};
        if (program_open) {
            Space *root=eval_current_root_space();
            if (!root || ctx->session.language_id!=CETTA_LANGUAGE_PETTA) return refused(a,"program-profile");
            captured=(ProgramWatch){.token=space_program_token(root),.profile=ctx->session.profile,
                .language=ctx->session.language_id,.imports=ctx->active_mask,
                .admission=ctx->petta_admission_revision,.paths=ctx->petta_library_paths.revision};
            captured.barrier=petta_program_read_barrier_new(root);
            if (!captured.barrier) return refused(a,"capacity");
        } else {
            if (!acquire(ctx,program_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-program-watch");
            captured=*(ProgramWatch *)cetta_native_handle_use_resource(&use);
            if (program_check) {
                bool current=program_current(ctx,&captured);
                cetta_native_handle_use_release(&use);
                return current ? atom_symbol(a,"StoreProgramCurrent") : refused(a,"program-invalidated");
            }
            bool retained=petta_program_read_barrier_retain(captured.barrier);
            cetta_native_handle_use_release(&use);
            if (!retained) return refused(a,"capacity");
        }
        ProgramWatch *watch=malloc(sizeof(*watch));
        if (!watch) { petta_program_read_barrier_free(captured.barrier);return refused(a,"capacity"); }
        *watch=captured;Atom *handle=own(ctx,a,program_kind,watch,free_program);
        return handle ? atom_expr2(a,atom_symbol(a,"StoreProgramWatch"),handle) : refused(a,"capacity");
    }
    if (atom_is_symbol(head,"__cetta_lib_store_space_view")) return space_view(ctx,a,args,n,false,false);
    if (atom_is_symbol(head,"__cetta_lib_store_space_workspace")) return space_view(ctx,a,args,n,false,true);
    if (atom_is_symbol(head,"__cetta_lib_store_space_edit")) return space_edit(ctx,a,args,n);
    if (atom_is_symbol(head,"__cetta_lib_store_space_query_source")) {
        if (n!=3 || ctx->session.language_id!=CETTA_LANGUAGE_PETTA) return refused(a,"invalid-source-query");
        CETTA_SCOPED_SHARED_TRANSITION(capture);
        Space *source=resolve_space(eval_current_registry(),args[0]);CettaDiskQuery *query=NULL;
        if (!source || source->match_backend.kind!=SPACE_ENGINE_SOURCE) return refused(a,"source-space-required");
        if (!space_owner_is_current(source)) return refused(a,"closed-source");
        CettaDiskStoreStatus status=cetta_space_query_open_source_bounded(source,args[1],args[2],cetta_library_value_byte_limit(ctx),&query);
        if (status!=CETTA_STORE_OK) return status_value(a,status);
        Atom *handle=own_query(ctx,a,query);
        return handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryOpened"),handle,
            atom_symbol(a,"HistoricalSource"),unsigned_value(a,cetta_disk_query_revision(query))},4) : refused(a,"capacity");
    }
    if (atom_is_symbol(head,"__cetta_lib_store_space_query_open")) return space_view(ctx,a,args,n,true,false);
    if (atom_is_symbol(head,"__cetta_lib_store_space_changes")) {
        CettaDiskBytes key;
        if (n!=3 || !string(args[2],&key)) return refused(a,"invalid-space-changes");
        StoredSpace *owned=stored_space_arg(args[0]);
        if (!owned || !stored_space_current(owned)) return refused(a,"closed-space");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
        bool same=owned->revision==cetta_disk_store_view_revision(view) &&
            !memcmp(owned->epoch,cetta_disk_store_view_epoch(view),16) &&
            key.size==owned->metadata.size && (!key.size || !memcmp(key.data,owned->metadata.data,key.size));
        cetta_native_handle_use_release(&use);
        if (!same) return refused(a,"workspace-view-mismatch");
        Atom *added=NULL,*removed=NULL;
        return space_source_overlay_changes(&owned->space,a,&added,&removed)
            ? atom_expr3(a,atom_symbol(a,"StoreSpaceChanges"),added,removed) : refused(a,"not-workspace");
    }
    if (atom_is_symbol(head,"__cetta_lib_store_space_point_query_open")) {
        CettaDiskBytes key;
        if (n!=4 || !string(args[1],&key)) return refused(a,"invalid-space-point-query");
        if (ctx->session.language_id!=CETTA_LANGUAGE_PETTA) return refused(a,"space-language");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);CettaDiskQuery *query=NULL;
        CettaDiskStoreStatus status=cetta_disk_query_open_point_bounded(view,key,args[2],args[3],cetta_library_value_byte_limit(ctx),&query);
        Atom *result=NULL;
        if (status==CETTA_STORE_OK) {
            Atom *handle=own_query(ctx,a,query);
            result=handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryOpened"),handle,
                view_epoch_value(a,view),unsigned_value(a,cetta_disk_store_view_revision(view))},4) : refused(a,"capacity");
        } else result=status==CETTA_STORE_ABSENT ? atom_symbol(a,"StoreAbsent") : status_value(a,status);
        cetta_native_handle_use_release(&use);return result;
    }
    bool query_next=atom_is_symbol(head,"__cetta_lib_store_space_query_next");
    bool query_checkpoint=atom_is_symbol(head,"__cetta_lib_store_space_query_checkpoint");
    bool query_info=atom_is_symbol(head,"__cetta_lib_store_space_query_info");
    bool query_cancel=atom_is_symbol(head,"__cetta_lib_store_space_query_cancel");
    if (query_next || query_checkpoint || query_cancel || query_info) {
        uint64_t sequence=0,work=0,maximum=0;
        if (n!=(query_next ? 4u : 1u) || (query_next &&
            (!number(args[1],&sequence) || !number(args[2],&work) || !number(args[3],&maximum) || maximum>cetta_library_value_byte_limit(ctx))))
            return refused(a,"invalid-space-query");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,query_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy-query");
        CettaDiskQuery *query=cetta_native_handle_use_resource(&use);Atom *result=NULL;
        if (query_info) {
            CettaDiskSpaceStatistics stats={0};
            result=cetta_disk_query_statistics(query,&stats)
                ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryInfo"),unsigned_value(a,stats.scans),
                    unsigned_value(a,stats.indexed_selections),unsigned_value(a,stats.candidates),
                    unsigned_value(a,stats.decoded_bytes),atom_bool(a,stats.overflow)},6) : atom_symbol(a,"StoreQueryInfoUnavailable");
        } else if (query_checkpoint) result=cetta_disk_query_checkpoint(query)
            ? atom_symbol(a,"StoreQueryCheckpointed") : refused(a,"capacity");
        else {
            CettaDiskBytes packet={0};size_t required=0;
            CettaDiskQueryStep step=query_cancel ? cetta_disk_query_cancel(query)
                : cetta_disk_query_next(query,sequence,work,(size_t)maximum,&packet,&required);
            switch (step) {
            case CETTA_DISK_QUERY_ROW: {
                Atom *record=NULL;
                CettaAtomWireStatus decoded=cetta_atom_wire_decode_bounded(a,packet.data,packet.size,cetta_library_value_byte_limit(ctx),&record);
                if (decoded!=ATOM_WIRE_OK) result=refused(a,decoded==ATOM_WIRE_NOMEM ? "capacity" : "query-packet");
                else result=atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryRow"),unsigned_value(a,sequence),
                    record->expr.elems[1],record->expr.elems[2]},4);
                break;
            }
            case CETTA_DISK_QUERY_TOO_LARGE: result=atom_expr2(a,atom_symbol(a,"StoreQueryTooLarge"),unsigned_value(a,required));break;
            case CETTA_DISK_QUERY_PAUSED: result=atom_symbol(a,"StoreQueryPaused");break;
            case CETTA_DISK_QUERY_END: result=atom_symbol(a,"StoreQueryEnd");break;
            case CETTA_DISK_QUERY_STALE: result=atom_symbol(a,"StoreQueryStale");break;
            case CETTA_DISK_QUERY_CANCELLED: result=atom_symbol(a,"StoreQueryCancelled");break;
            case CETTA_DISK_QUERY_INCOMPLETE: result=atom_expr2(a,atom_symbol(a,"StoreQueryFailed"),atom_symbol(a,"incomplete"));break;
            case CETTA_DISK_QUERY_INVALIDATED: result=atom_expr2(a,atom_symbol(a,"StoreQueryFailed"),atom_symbol(a,"invalidated"));break;
            case CETTA_DISK_QUERY_CAPACITY: result=atom_expr2(a,atom_symbol(a,"StoreQueryFailed"),atom_symbol(a,"capacity"));break;
            }
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_key_i64")) {
        if (n!=1 || args[0]->kind!=ATOM_GROUNDED || args[0]->ground.gkind!=GV_INT) return refused(a,"invalid-integer");
        uint64_t value=(uint64_t)args[0]->ground.ival ^ (UINT64_C(1)<<63);unsigned char bytes[8];
        for (size_t i=8;i>0;--i) { bytes[i-1]=(unsigned char)value;value>>=8; }
        return atom_expr2(a,atom_symbol(a,"StoreKeyBytes"),atom_string_n(a,(const char *)bytes,8));
    }
    if (atom_is_symbol(head,"__cetta_lib_store_key_to_i64")) {
        CettaDiskBytes bytes;
        if (n!=1 || !string(args[0],&bytes) || bytes.size!=8) return refused(a,"invalid-integer-key");
        uint64_t ordered=0;
        for (size_t i=0;i<8;++i) ordered=(ordered<<8)|bytes.data[i];
        uint64_t sign=UINT64_C(1)<<63;
        int64_t value=ordered<sign ? INT64_MIN+(int64_t)ordered : (int64_t)(ordered-sign);
        return atom_expr2(a,atom_symbol(a,"StoreKeyInteger"),atom_int(a,value));
    }
    if (atom_is_symbol(head,"__cetta_lib_store_owner_clone")) {
        CettaNativeHandleUse use={0};
        if (n!=1 || !acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy-owner");
        CettaDiskStore *copy=NULL;CettaDiskStoreStatus status=cetta_disk_store_clone_owner(cetta_native_handle_use_resource(&use),&copy);
        cetta_native_handle_use_release(&use);
        if (status!=CETTA_STORE_OK) return status_value(a,status);
        Atom *handle=own(ctx,a,owner_kind,copy,free_owner);
        return handle ? atom_expr2(a,atom_symbol(a,"StoreOpened"),handle) : refused(a,"capacity");
    }
    if (atom_is_symbol(head,"__cetta_lib_store_open")) {
        CettaDiskBytes path;
        if (n!=2 || !string(args[0],&path) || !path.size || memchr(path.data,0,path.size) ||
            (!word(args[1],"create") && !word(args[1],"existing") && !word(args[1],"create-directory") && !word(args[1],"create-act-directory"))) return refused(a,"invalid-open");
        CettaDiskStore *store=NULL;
        CettaDiskStoreStatus status=word(args[1],"create-act-directory")
            ? cetta_disk_store_open_created_act((const char *)path.data,NULL,&store)
            : word(args[1],"create-directory") ? cetta_disk_store_open_created((const char *)path.data,NULL,&store)
            : cetta_disk_store_open((const char *)path.data,word(args[1],"create"),NULL,&store);
        if (status!=CETTA_STORE_OK) return status_value(a,status);
        Atom *handle=own(ctx,a,owner_kind,store,free_owner);
        return handle ? atom_expr2(a,atom_symbol(a,"StoreOpened"),handle) : refused(a,"capacity");
    }

    if (atom_is_symbol(head,"__cetta_lib_store_storage_info")) {
        CettaNativeHandleUse use={0};
        if (n!=1 || !acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        CettaDiskStoreInfo info;const CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
        bool ok=cetta_disk_store_view_info(view,&info);Atom *result;
        if (!ok) result=refused(a,"storage-info");
        else {
            Atom *fields[]={atom_symbol(a,"StoreStorageInfo"),atom_int(a,info.format),view_epoch_value(a,view),
                unsigned_value(a,info.revision),unsigned_value(a,info.base_bytes),unsigned_value(a,info.delta_bytes),
                unsigned_value(a,info.frozen_bytes),atom_bool(a,info.has_base),atom_bool(a,info.has_frozen)};
            result=atom_expr(a,fields,9);
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_compact_act_open")) {
        uint64_t budget=0;CettaNativeHandleUse use={0};
        if (n!=2 || !number(args[1],&budget) || budget>SIZE_MAX) return refused(a,"compaction-budget");
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy");
        CettaDiskStore *store=cetta_native_handle_use_resource(&use);Atom *result=NULL;
        {
            Compaction *c=calloc(1,sizeof(*c));
            if (!c) result=refused(a,"capacity");
            else {
                c->owner=store;CettaDiskStoreStatus status=cetta_disk_store_compact_act_open(store,(size_t)budget,&c->job);
                if (status!=CETTA_STORE_OK) {free(c);result=status_value(a,status);}
                else {
                    Atom *progress=compaction_progress(a,c->job);
                    Atom *handle=own(ctx,a,compaction_kind,c,free_compaction);
                    if (!handle) {cetta_disk_store_require_recovery(store);result=status_value(a,CETTA_STORE_UNKNOWN);}
                    else result=atom_expr3(a,atom_symbol(a,"StoreCompactionOpened"),handle,progress);
                }
            }
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_compact_act_step") ||
        atom_is_symbol(head,"__cetta_lib_store_compact_act_info")) {
        bool step=atom_is_symbol(head,"__cetta_lib_store_compact_act_step");uint64_t work=0;
        if (n!=(step ? 2u : 1u) || (step && !number(args[1],&work))) return refused(a,"compaction-work");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,compaction_kind,args[0],step ? CETTA_NATIVE_HANDLE_USE_EXCLUSIVE : CETTA_NATIVE_HANDLE_USE_LIFETIME,&use))
            return refused(a,"closed-or-busy-compaction");
        Compaction *c=cetta_native_handle_use_resource(&use);Atom *result;
        if (!step) result=compaction_progress(a,c->job);
        else {
            CettaDiskStoreView *view=NULL;CettaDiskStoreStatus status=cetta_disk_store_compact_act_step(c->job,work,&view);
            result=status==CETTA_STORE_PROGRESS ? atom_expr2(a,atom_symbol(a,"StoreCompactionMore"),compaction_progress(a,c->job))
                : status==CETTA_STORE_OK ? view_value(ctx,a,view,true,c->owner) : status_value(a,status);
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_backup")) {
        CettaDiskBytes path;CettaNativeHandleUse use={0};
        if (n!=2 || !string(args[1],&path) || !path.size || memchr(path.data,0,path.size)) return refused(a,"invalid-backup");
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
        CettaDiskStoreStatus status=cetta_disk_store_backup(view,(const char *)path.data);
        Atom *result=status==CETTA_STORE_OK
            ? atom_expr3(a,atom_symbol(a,"StoreBackedUp"),view_epoch_value(a,view),unsigned_value(a,cetta_disk_store_view_revision(view)))
            : status_value(a,status);
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_value_byte_limit")) {
        bool unit=n==1 && args[0] && args[0]->kind==ATOM_EXPR && args[0]->expr.len==0;
        return n && !unit ? refused(a,"invalid-value-byte-limit") : unsigned_value(a,cetta_library_value_byte_limit(ctx));
    }
    if (atom_is_symbol(head,"__cetta_lib_store_encode")) {
        if (n!=1) return refused(a,"invalid-encode");
        unsigned char *bytes=NULL;size_t size=0;
        CettaAtomWireStatus status=cetta_atom_wire_encode_bounded(args[0],cetta_library_value_byte_limit(ctx),&bytes,&size);
        if (status!=ATOM_WIRE_OK) return atom_expr2(a,atom_symbol(a,"StoreCodecRefused"),atom_string(a,cetta_atom_wire_status_name(status)));
        Atom *result=atom_expr2(a,atom_symbol(a,"StoreEncoded"),atom_string_n(a,(const char *)bytes,size));free(bytes);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_decode")) {
        CettaDiskBytes bytes;Atom *value=NULL;
        if (n!=1 || !string(args[0],&bytes)) return refused(a,"invalid-decode");
        CettaAtomWireStatus status=cetta_atom_wire_decode_bounded(a,bytes.data,bytes.size,cetta_library_value_byte_limit(ctx),&value);
        return status==ATOM_WIRE_OK ? atom_expr2(a,atom_symbol(a,"StoreDecoded"),value) :
            atom_expr2(a,atom_symbol(a,"StoreCodecRefused"),atom_string(a,cetta_atom_wire_status_name(status)));
    }
    if (atom_is_symbol(head,"__cetta_lib_store_close")) {
        if (n!=1) return refused(a,"invalid-close");
        StoredSpace *space=stored_space_arg(args[0]);
        if (space) { stored_space_close(space);return atom_symbol(a,"StoreClosed"); }
        const char *kinds[]={owner_kind,view_kind,reader_kind,scan_kind,query_kind,batch_kind,program_kind,compaction_kind,source_kind,candidate_kind,remote_kind,remote_resource_kind};
        for (size_t i=0;i<sizeof(kinds)/sizeof(*kinds);++i) {
            CettaNativeHandleUse use={0};uint64_t id=0;
            if (!acquire(ctx,kinds[i],args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) continue;
            bool closed=cetta_native_handle_arg(args[0],kinds[i],&id) && cetta_native_handle_close(ctx,kinds[i],id);
            cetta_native_handle_use_release(&use);
            return closed ? atom_symbol(a,"StoreClosed") : refused(a,"closed");
        }
        return refused(a,"closed");
    }
    const char *simple=NULL;
    if (atom_is_symbol(head,"__cetta_lib_store_snapshot")) simple="snapshot";
    else if (atom_is_symbol(head,"__cetta_lib_store_recover")) simple="recover";
    else if (atom_is_symbol(head,"__cetta_lib_store_compact")) simple="compact";
    else if (atom_is_symbol(head,"__cetta_lib_store_compact_act")) simple="compact-act";
    else if (atom_is_symbol(head,"__cetta_lib_store_collect")) simple="collect";
    if (simple) {
        CettaNativeHandleUse use={0};
        if (n!=1 || !acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy");
        CettaDiskStore *store=cetta_native_handle_use_resource(&use);CettaDiskStoreView *view=NULL;uint64_t removed=0;
        CettaDiskStoreStatus status=!strcmp(simple,"snapshot") ? cetta_disk_store_snapshot(store,&view) :
            !strcmp(simple,"recover") ? cetta_disk_store_recover(store,&view) :
            !strcmp(simple,"compact") ? cetta_disk_store_compact(store,&view) :
            !strcmp(simple,"compact-act") ? cetta_disk_store_compact_act(store,&view) : cetta_disk_store_collect(store,&removed);
        Atom *answer=status!=CETTA_STORE_OK ? status_value(a,status) :
            view ? view_value(ctx,a,view,!strcmp(simple,"compact") || !strcmp(simple,"compact-act"),store) :
            atom_expr2(a,atom_symbol(a,"StoreCollected"),unsigned_value(a,removed));
        cetta_native_handle_use_release(&use);return answer;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_view_clone")) {
        CettaNativeHandleUse use={0};
        if (n!=1 || !acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-view");
        CettaDiskStoreView *copy=NULL;
        CettaDiskStoreStatus status=cetta_disk_store_view_clone(cetta_native_handle_use_resource(&use),&copy);
        cetta_native_handle_use_release(&use);
        return status==CETTA_STORE_OK ? view_value(ctx,a,copy,false,NULL) : status_value(a,status);
    }
    if (atom_is_symbol(head,"__cetta_lib_store_view_info")) {
        if (n!=1) return refused(a,"invalid-view-info");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use))
            return refused(a,"closed-view");
        CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
        Atom *fields[]={atom_symbol(a,"StoreViewInfo"),view_epoch_value(a,view),
                       unsigned_value(a,cetta_disk_store_view_revision(view))};
        Atom *answer=atom_expr(a,fields,3);
        cetta_native_handle_use_release(&use);
        return answer;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_check_view")) {
        if (n!=2) return refused(a,"invalid-view-check");
        CettaNativeHandleUse owner={0},view={0};
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&owner))
            return refused(a,"closed-or-busy");
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view)) {
            cetta_native_handle_use_release(&owner); return refused(a,"closed-view");
        }
        CettaDiskStoreStatus status=cetta_disk_store_check_view(
            cetta_native_handle_use_resource(&owner),cetta_native_handle_use_resource(&view));
        cetta_native_handle_use_release(&view);
        cetta_native_handle_use_release(&owner);
        return status==CETTA_STORE_OK ? atom_symbol(a,"StoreViewValid") : status_value(a,status);
    }
    if (atom_is_symbol(head,"__cetta_lib_store_batch_open")) {
        uint64_t maximum=0;CettaNativeHandleUse owner={0},view={0};
        if (n!=3 || !number(args[2],&maximum)) return refused(a,"invalid-batch-limit");
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&owner)) return refused(a,"closed-or-busy");
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view)) {
            cetta_native_handle_use_release(&owner);return refused(a,"closed-view");
        }
        CettaDiskStoreBatch *batch=NULL;
        CettaDiskStoreStatus status=cetta_disk_store_batch_open(cetta_native_handle_use_resource(&owner),cetta_native_handle_use_resource(&view),maximum,&batch);
        cetta_native_handle_use_release(&view);cetta_native_handle_use_release(&owner);
        if (status!=CETTA_STORE_OK) return status_value(a,status);
        Atom *handle=own(ctx,a,batch_kind,batch,free_batch);
        return handle ? atom_expr2(a,atom_symbol(a,"StoreBatch"),handle) : refused(a,"capacity");
    }
    bool batch_add=atom_is_symbol(head,"__cetta_lib_store_batch_add");
    if (batch_add || atom_is_symbol(head,"__cetta_lib_store_batch_info")) {
        if (n!=(batch_add ? 2u : 1u)) return refused(a,"invalid-batch");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,batch_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy-batch");
        CettaDiskStoreBatch *batch=cetta_native_handle_use_resource(&use);Atom *answer=NULL;
        if (batch_add) {
            CettaDiskOp *ops=NULL;size_t count=0;
            CettaDiskStoreStatus status=operations(args[1],&ops,&count)
                ? cetta_disk_store_batch_add(batch,ops,count) : CETTA_STORE_INVALID;
            free(ops);answer=status==CETTA_STORE_OK ? atom_symbol(a,"StoreBatchStaged") : status_value(a,status);
        } else {
            uint64_t bytes=0,maximum=0,staged=0;
            answer=cetta_disk_store_batch_info(batch,&bytes,&maximum,&staged)
                ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreBatchInfo"),unsigned_value(a,bytes),unsigned_value(a,maximum),unsigned_value(a,staged)},4)
                : refused(a,"invalid-batch");
        }
        cetta_native_handle_use_release(&use);return answer;
    }
    bool commit_call=atom_is_symbol(head,"__cetta_lib_store_commit"),scoped_call=atom_is_symbol(head,"__cetta_lib_store_commit_scoped");
    bool guarded_batch=atom_is_symbol(head,"__cetta_lib_store_commit_batch_guarded");
    bool batch_commit=guarded_batch || atom_is_symbol(head,"__cetta_lib_store_commit_batch");
    bool guarded_commit=guarded_batch || atom_is_symbol(head,"__cetta_lib_store_commit_guarded");
    if (commit_call || scoped_call || batch_commit || guarded_commit) {
        if (n!=(scoped_call ? 5u : guarded_commit ? 4u : 3u)) return refused(a,"invalid-commit");
        CettaNativeHandleUse owner={0},view={0};
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&owner)) return refused(a,"closed-or-busy");
        if (edit_store_active(cetta_native_handle_use_resource(&owner))) {
            cetta_native_handle_use_release(&owner);return refused(a,"write-during-space-edit");
        }
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view)) {
            cetta_native_handle_use_release(&owner);return refused(a,"closed-view");
        }
        CettaDiskOp *ops=NULL;size_t count=0;Scopes reads={0},writes={0};CettaDiskStoreView *result=NULL;
        CettaNativeHandleUse batch={0},watch={0};
        if (guarded_commit && !acquire(ctx,program_kind,args[2],CETTA_NATIVE_HANDLE_USE_LIFETIME,&watch)) {
            cetta_native_handle_use_release(&view);cetta_native_handle_use_release(&owner);
            return refused(a,"closed-program-watch");
        }
        if (batch_commit && !acquire(ctx,batch_kind,args[guarded_batch ? 3 : 2],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&batch)) {
            cetta_native_handle_use_release(&view);cetta_native_handle_use_release(&owner);
            return refused(a,"closed-or-busy-batch");
        }
        bool valid=batch_commit || operations(args[scoped_call ? 4 : guarded_commit ? 3 : 2],&ops,&count);
        if (valid && scoped_call) valid=scopes(args[2],"reads",&reads) && scopes(args[3],"writes",&writes);
        CettaDiskStoreStatus status=CETTA_STORE_INVALID;
        /* The program observation and disk publication are one validation
         * interval in the existing physical-transition domain. No MeTTa
         * callback executes while that guard or the writer lock is held. */
        CettaSharedTransitionGuard publication={0};
        if (guarded_commit) cetta_shared_transition_guard_enter(&publication);
        bool program_valid=!guarded_commit || program_current(ctx,cetta_native_handle_use_resource(&watch));
        if (valid && program_valid) status=batch_commit ? cetta_disk_store_commit_batch(cetta_native_handle_use_resource(&owner),
            cetta_native_handle_use_resource(&view),cetta_native_handle_use_resource(&batch),&result) : scoped_call ? cetta_disk_store_commit_scoped(cetta_native_handle_use_resource(&owner),
            cetta_native_handle_use_resource(&view),reads.items,reads.count,writes.items,writes.count,ops,count,&result) :
            cetta_disk_store_commit(cetta_native_handle_use_resource(&owner),cetta_native_handle_use_resource(&view),ops,count,&result);
        cetta_shared_transition_guard_leave(&publication);
        free(ops);scopes_free(&reads);scopes_free(&writes);
        cetta_native_handle_use_release(&watch);
        cetta_native_handle_use_release(&batch);
        cetta_native_handle_use_release(&view);
        Atom *answer=!program_valid ? refused(a,"program-invalidated") : status==CETTA_STORE_OK ? view_value(ctx,a,result,true,cetta_native_handle_use_resource(&owner)) : status_value(a,status);
        cetta_native_handle_use_release(&owner);return answer;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_get") || atom_is_symbol(head,"__cetta_lib_store_blob_open")) {
        bool reading=atom_is_symbol(head,"__cetta_lib_store_get");CettaDiskBytes key;uint64_t budget=0;
        if (n!=(reading ? 3u : 2u) || !string(args[1],&key) ||
            (reading && (!number(args[2],&budget) || budget>CETTA_STRING_BYTES_MAX))) return refused(a,"invalid-get");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        const CettaDiskStoreView *root=cetta_native_handle_use_resource(&use);CettaDiskStoreBlob blob;
        CettaDiskStatus status=cetta_disk_store_lookup(root,key,&blob);Atom *result=NULL;
        if (status==CETTA_DISK_ABSENT) result=atom_symbol(a,"StoreAbsent");
        else if (status!=CETTA_DISK_OK) result=status_value(a,disk_status(status));
        else if (reading && blob.size>budget) result=atom_expr2(a,atom_symbol(a,"StoreTooLarge"),atom_int(a,(int64_t)blob.size));
        else if (reading) {
            unsigned char *bytes=malloc(blob.size ? (size_t)blob.size : 1);size_t got=0;
            if (!bytes) result=refused(a,"capacity");
            else {
                status=cetta_disk_store_blob_read(root,blob,0,bytes,(size_t)blob.size,&got);
                result=status==CETTA_DISK_OK ? atom_expr2(a,atom_symbol(a,"StoreValue"),atom_string_n(a,(const char *)bytes,got)) : status_value(a,disk_status(status));
                free(bytes);
            }
        } else {
            CettaDiskStoreBlobReader *reader=NULL;status=cetta_disk_store_blob_reader_open(root,blob,&reader);
            if (status!=CETTA_DISK_OK) result=status_value(a,disk_status(status));
            else {
                Atom *handle=own(ctx,a,reader_kind,reader,free_reader);
                result=handle ? atom_expr3(a,atom_symbol(a,"StoreBlob"),handle,atom_int(a,(int64_t)blob.size)) : refused(a,"capacity");
            }
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_blob_read")) {
        uint64_t offset,count;
        if (n!=3 || !number(args[1],&offset) || !number(args[2],&count) || count>CETTA_STRING_BYTES_MAX) return refused(a,"invalid-read");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,reader_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-reader");
        CettaDiskStoreBlobReader *reader=cetta_native_handle_use_resource(&use);
        uint64_t size=cetta_disk_store_blob_reader_size(reader);
        if (offset>size) { cetta_native_handle_use_release(&use);return refused(a,"invalid-offset"); }
        if (count>size-offset) count=size-offset;
        unsigned char *bytes=malloc(count ? (size_t)count : 1);size_t got=0;Atom *result;
        if (!bytes) result=refused(a,"capacity");
        else {
            CettaDiskStatus status=cetta_disk_store_blob_reader_read(reader,offset,bytes,(size_t)count,&got);
            result=status==CETTA_DISK_OK ? atom_expr3(a,atom_symbol(a,"StoreBytes"),atom_string_n(a,(const char *)bytes,got),atom_int(a,(int64_t)(offset+got))) : status_value(a,disk_status(status));
            free(bytes);
        }
        cetta_native_handle_use_release(&use);return result;
    }
    bool reverse_scan=atom_is_symbol(head,"__cetta_lib_store_scan_reverse_open");
    if (atom_is_symbol(head,"__cetta_lib_store_scan_open") || reverse_scan) {
        CettaDiskBytes lower={0},upper={0};bool lo=false,hi=false;
        if (n!=3 || !bound(args[1],&lower,&lo) || !bound(args[2],&upper,&hi)) return refused(a,"invalid-range");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        Scan *scan=calloc(1,sizeof(*scan));Atom *result;
        const CettaDiskStoreView *root=cetta_native_handle_use_resource(&use);
        CettaDiskStatus status=!scan ? CETTA_DISK_NOMEM : reverse_scan ?
            cetta_disk_store_cursor_open_reverse_range(root,lo ? &lower : NULL,false,hi ? &upper : NULL,&scan->cursor) :
            cetta_disk_store_cursor_open_range(root,lo ? &lower : NULL,false,hi ? &upper : NULL,&scan->cursor);
        if (status!=CETTA_DISK_OK) { free(scan);result=status_value(a,disk_status(status)); }
        else {
            Atom *handle=own(ctx,a,scan_kind,scan,free_scan);
            result=handle ? atom_expr2(a,atom_symbol(a,"StoreScan"),handle) : refused(a,"capacity");
        }
        cetta_native_handle_use_release(&use);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_scan_next")) {
        uint64_t position;
        if (n!=2 || !number(args[1],&position)) return refused(a,"invalid-position");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,scan_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy");
        Scan *scan=cetta_native_handle_use_resource(&use);Atom *result=NULL;
        if (scan->last.data && position==scan->position-1) { /* Repeat the last read without advancing. */ }
        else if (position!=scan->position) result=refused(a,"stale-position");
        else if (scan->done) result=atom_symbol(a,"StoreEnd");
        else {
            CettaDiskBuffer key={0};CettaDiskStoreBlob blob;
            CettaDiskStatus status=cetta_disk_store_cursor_next(scan->cursor,&key,&blob);
            if (status==CETTA_DISK_END) { scan->done=true;result=atom_symbol(a,"StoreEnd"); }
            else if (status!=CETTA_DISK_OK) result=status_value(a,disk_status(status));
            else { cetta_disk_buffer_free(&scan->last);scan->last=key;scan->blob=blob;++scan->position; }
        }
        if (!result) {
            Atom *fields[]={atom_symbol(a,"StoreKey"),atom_string_n(a,(const char *)scan->last.data,scan->last.size),
                atom_int(a,(int64_t)scan->blob.size),atom_int(a,(int64_t)scan->position)};
            result=atom_expr(a,fields,4);
        }
        cetta_native_handle_use_release(&use);return result;
    }
    return NULL;
}
