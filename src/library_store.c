#include "library_store.h"
#include "library.h"
#include "native_disk_store.h"
#include "native_disk_space.h"
#include "native_disk_query.h"
#include "native_handle.h"
#include "atom_wire.h"
#include "eval.h"
#include "shared_transition.h"
#include "petta_specializer.h"
#include "petta_semantics.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *owner_kind="disk-store",*view_kind="disk-store-view";
static const char *reader_kind="disk-store-body",*scan_kind="disk-store-scan";
static const char *query_kind="disk-space-query";
static const char *batch_kind="disk-store-batch";
static const char *program_kind="program-watch";
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
} ProgramWatch;
static void free_program(void *p) {
    ProgramWatch *watch=p;petta_program_read_barrier_free(watch->barrier);free(watch);
}
typedef struct { CettaDiskCursor *cursor; CettaDiskBuffer last; CettaDiskBlob blob; uint64_t position; bool done; } Scan;
typedef struct { CettaDiskStoreScope *items; unsigned char **allocated; size_t count; } Scopes;

static void free_owner(void *p) { cetta_disk_store_close(p); }
static void free_view(void *p) { cetta_disk_store_view_close(p); }
static void view_storage(const void *p,CettaStorageCensus *census) { cetta_disk_store_view_storage_census(p,census); }
static void free_reader(void *p) { cetta_disk_blob_reader_close(p); }
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
        space_program_token_matches_live_space(watch->token,eval_current_root_space()) &&
        petta_program_read_barrier_current(watch->barrier,eval_current_root_space());
    return valid;
}
static void free_scan(void *p) {
    Scan *scan=p; cetta_disk_cursor_close(scan->cursor); cetta_disk_buffer_free(&scan->last); free(scan);
}
static void scan_storage(const void *p,CettaStorageCensus *census) {
    const Scan *scan=p;
    if (!scan || !cetta_storage_object(census,scan,sizeof(*scan),CETTA_STORAGE_BLOCK)) return;
    cetta_disk_cursor_storage_census(scan->cursor,census);
    cetta_storage_object(census,scan->last.data,scan->last.size ? scan->last.size : 1,CETTA_STORAGE_BLOCK);
}
static Atom *refused(Arena *a,const char *why) {
    return atom_expr2(a,atom_symbol(a,"StoreRefused"),atom_symbol(a,why));
}
static Atom *status_value(Arena *a,CettaDiskStoreStatus status) {
    static const char *names[]={"ok","absent","invalid","busy","conflict","io","corrupt",
        "capacity","limit","unknown","version","recovery-required","ownership"};
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
    CettaStorageVisitor visitor=kind==view_kind ? view_storage
        : kind==batch_kind ? cetta_disk_store_batch_storage_census
        : kind==program_kind ? program_storage
        : kind==scan_kind ? scan_storage : NULL;
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
static Atom *space_view(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n,bool streaming) {
    CettaDiskBytes key;
    if (n!=(streaming ? 4u : 2u) || !string(args[1],&key)) return refused(a,"invalid-space-view");
    /* Other dialects have different Space discovery/execution paths. Keep
     * their public admission explicit until those paths are qualified. */
    if (ctx->session.language_id!=CETTA_LANGUAGE_PETTA)
        return refused(a,"space-language");
    /* Persistent source values need the ordinary evaluator's Space owner.
     * An unqualified transactional payload must not acquire a live foreign
     * capability by receiving a raw Space pointer. */
    Arena *storage=eval_current_persistent_arena();
    if (!streaming && (!storage || eval_payload_transactional() || ctx->term_universe.persistent_arena!=storage))
        return refused(a,"space-owner-context");
    CettaNativeHandleUse use={0};
    if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
    CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);
    CettaDiskBlob blob;const CettaDiskRoot *root=cetta_disk_store_view_root(view);
    CettaDiskStatus read=cetta_disk_lookup(root,key,&blob);
    Atom *result=NULL;
    if (read!=CETTA_DISK_OK) { result=status_value(a,disk_status(read));goto done; }
    if (blob.size>CETTA_ATOM_WIRE_BYTES) { result=refused(a,"space-metadata-limit");goto done; }
    unsigned char *body=malloc(blob.size ? (size_t)blob.size : 1);size_t got=0;
    if (!body) { result=refused(a,"capacity");goto done; }
    read=cetta_disk_blob_read(root,blob,0,body,(size_t)blob.size,&got);
    Arena metadata;arena_init_detached(&metadata);Atom *record=NULL;
    CettaAtomWireStatus decoded=read==CETTA_DISK_OK && got==blob.size
        ? cetta_atom_wire_decode(&metadata,body,got,&record) : ATOM_WIRE_CORRUPT;
    free(body);
    CettaDiskBytes prefix;uint64_t count=0;
    bool version2=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV2");
    bool version3=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV3");
    bool version4=record && record->kind==ATOM_EXPR && record->expr.len==4 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV4");
    bool valid=decoded==ATOM_WIRE_OK && record && record->kind==ATOM_EXPR &&
        ((record->expr.len==3 && atom_is_symbol(record->expr.elems[0],"StoreSpaceV1")) || version2 || version3 || version4) && string(record->expr.elems[1],&prefix) &&
        prefix.size && prefix.size<=root->key_limit && number(record->expr.elems[2],&count) &&
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
                    index_prefix.size && index_prefix.size<=root->key_limit &&
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
        CettaDiskStoreStatus status=cetta_disk_query_open_indexed(view,prefix,count,indexes,version3 || version4 ? index_count : 0,args[2],args[3],&query);
        if (status!=CETTA_STORE_OK) result=status_value(a,status);
        else {
            Atom *handle=own_query(ctx,a,query);
            result=handle ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryOpened"),handle,
                view_epoch_value(a,view),unsigned_value(a,cetta_disk_store_view_revision(view))},4) : refused(a,"capacity");
        }
    } else {
        Space *space=arena_alloc(storage,sizeof(*space));
        if (!space) { result=refused(a,"capacity");free(indexes);arena_free(&metadata);goto done; }
        space_init_with_universe(space,&ctx->term_universe);
        CettaDiskStoreStatus status=cetta_disk_space_capture_indexed(space,view,prefix,count,indexes,version3 || version4 ? index_count : 0);
        if (status!=CETTA_STORE_OK) { space_free(space);result=status_value(a,status); }
        else if (!eval_track_new_space(space)) { space_free(space);result=refused(a,"capacity"); }
        else {
            Atom *fields[]={atom_symbol(a,"StoreSpace"),atom_space(a,space),view_epoch_value(a,view),
                            unsigned_value(a,cetta_disk_store_view_revision(view))};
            result=atom_expr(a,fields,4);
        }
    }
    free(indexes);arena_free(&metadata);
done:
    cetta_native_handle_use_release(&use);return result;
}
Atom *cetta_store_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
    if (!ctx || !a || !head) return NULL;
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
    if (atom_is_symbol(head,"__cetta_lib_store_space_view")) return space_view(ctx,a,args,n,false);
    if (atom_is_symbol(head,"__cetta_lib_store_space_query_open")) return space_view(ctx,a,args,n,true);
    if (atom_is_symbol(head,"__cetta_lib_store_space_point_query_open")) {
        CettaDiskBytes key;
        if (n!=4 || !string(args[1],&key)) return refused(a,"invalid-space-point-query");
        if (ctx->session.language_id!=CETTA_LANGUAGE_PETTA) return refused(a,"space-language");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,view_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return refused(a,"closed-view");
        CettaDiskStoreView *view=cetta_native_handle_use_resource(&use);CettaDiskQuery *query=NULL;
        CettaDiskStoreStatus status=cetta_disk_query_open_point(view,key,args[2],args[3],&query);
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
            (!number(args[1],&sequence) || !number(args[2],&work) || !number(args[3],&maximum) || maximum>CETTA_ATOM_WIRE_BYTES)))
            return refused(a,"invalid-space-query");
        CettaNativeHandleUse use={0};
        if (!acquire(ctx,query_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy-query");
        CettaDiskQuery *query=cetta_native_handle_use_resource(&use);Atom *result=NULL;
        if (query_info) {
            CettaDiskSpaceStatistics stats={0};
            result=cetta_disk_query_statistics(query,&stats)
                ? atom_expr(a,(Atom *[]){atom_symbol(a,"StoreQueryInfo"),unsigned_value(a,stats.scans),
                    unsigned_value(a,stats.indexed_selections),unsigned_value(a,stats.candidates),
                    unsigned_value(a,stats.decoded_bytes),atom_bool(a,stats.overflow)},6) : refused(a,"query-statistics");
        } else if (query_checkpoint) result=cetta_disk_query_checkpoint(query)
            ? atom_symbol(a,"StoreQueryCheckpointed") : refused(a,"capacity");
        else {
            CettaDiskBytes packet={0};size_t required=0;
            CettaDiskQueryStep step=query_cancel ? cetta_disk_query_cancel(query)
                : cetta_disk_query_next(query,sequence,work,(size_t)maximum,&packet,&required);
            switch (step) {
            case CETTA_DISK_QUERY_ROW: {
                Atom *record=NULL;
                CettaAtomWireStatus decoded=cetta_atom_wire_decode(a,packet.data,packet.size,&record);
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
    if (atom_is_symbol(head,"__cetta_lib_store_open")) {
        CettaDiskBytes path;
        if (n!=2 || !string(args[0],&path) || !path.size || memchr(path.data,0,path.size) ||
            (!word(args[1],"create") && !word(args[1],"existing"))) return refused(a,"invalid-open");
        CettaDiskStore *store=NULL;
        CettaDiskStoreStatus status=cetta_disk_store_open((const char *)path.data,word(args[1],"create"),NULL,&store);
        if (status!=CETTA_STORE_OK) return status_value(a,status);
        Atom *handle=own(ctx,a,owner_kind,store,free_owner);
        return handle ? atom_expr2(a,atom_symbol(a,"StoreOpened"),handle) : refused(a,"capacity");
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
    if (atom_is_symbol(head,"__cetta_lib_store_encode")) {
        if (n!=1) return refused(a,"invalid-encode");
        unsigned char *bytes=NULL;size_t size=0;
        CettaAtomWireStatus status=cetta_atom_wire_encode(args[0],&bytes,&size);
        if (status!=ATOM_WIRE_OK) return atom_expr2(a,atom_symbol(a,"StoreCodecRefused"),atom_string(a,cetta_atom_wire_status_name(status)));
        Atom *result=atom_expr2(a,atom_symbol(a,"StoreEncoded"),atom_string_n(a,(const char *)bytes,size));free(bytes);return result;
    }
    if (atom_is_symbol(head,"__cetta_lib_store_decode")) {
        CettaDiskBytes bytes;Atom *value=NULL;
        if (n!=1 || !string(args[0],&bytes)) return refused(a,"invalid-decode");
        CettaAtomWireStatus status=cetta_atom_wire_decode(a,bytes.data,bytes.size,&value);
        return status==ATOM_WIRE_OK ? atom_expr2(a,atom_symbol(a,"StoreDecoded"),value) :
            atom_expr2(a,atom_symbol(a,"StoreCodecRefused"),atom_string(a,cetta_atom_wire_status_name(status)));
    }
    if (atom_is_symbol(head,"__cetta_lib_store_close")) {
        if (n!=1) return refused(a,"invalid-close");
        const char *kinds[]={owner_kind,view_kind,reader_kind,scan_kind,query_kind,batch_kind,program_kind};
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
    else if (atom_is_symbol(head,"__cetta_lib_store_collect")) simple="collect";
    if (simple) {
        CettaNativeHandleUse use={0};
        if (n!=1 || !acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) return refused(a,"closed-or-busy");
        CettaDiskStore *store=cetta_native_handle_use_resource(&use);CettaDiskStoreView *view=NULL;uint64_t removed=0;
        CettaDiskStoreStatus status=!strcmp(simple,"snapshot") ? cetta_disk_store_snapshot(store,&view) :
            !strcmp(simple,"recover") ? cetta_disk_store_recover(store,&view) :
            !strcmp(simple,"compact") ? cetta_disk_store_compact(store,&view) : cetta_disk_store_collect(store,&removed);
        Atom *answer=status!=CETTA_STORE_OK ? status_value(a,status) :
            view ? view_value(ctx,a,view,!strcmp(simple,"compact"),store) :
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
    bool batch_commit=atom_is_symbol(head,"__cetta_lib_store_commit_batch");
    bool guarded_commit=atom_is_symbol(head,"__cetta_lib_store_commit_guarded");
    if (commit_call || scoped_call || batch_commit || guarded_commit) {
        if (n!=(scoped_call ? 5u : guarded_commit ? 4u : 3u)) return refused(a,"invalid-commit");
        CettaNativeHandleUse owner={0},view={0};
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&owner)) return refused(a,"closed-or-busy");
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view)) {
            cetta_native_handle_use_release(&owner);return refused(a,"closed-view");
        }
        CettaDiskOp *ops=NULL;size_t count=0;Scopes reads={0},writes={0};CettaDiskStoreView *result=NULL;
        CettaNativeHandleUse batch={0},watch={0};
        if (guarded_commit && !acquire(ctx,program_kind,args[2],CETTA_NATIVE_HANDLE_USE_LIFETIME,&watch)) {
            cetta_native_handle_use_release(&view);cetta_native_handle_use_release(&owner);
            return refused(a,"closed-program-watch");
        }
        if (batch_commit && !acquire(ctx,batch_kind,args[2],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&batch)) {
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
        const CettaDiskRoot *root=cetta_disk_store_view_root(cetta_native_handle_use_resource(&use));CettaDiskBlob blob;
        CettaDiskStatus status=cetta_disk_lookup(root,key,&blob);Atom *result=NULL;
        if (status==CETTA_DISK_ABSENT) result=atom_symbol(a,"StoreAbsent");
        else if (status!=CETTA_DISK_OK) result=status_value(a,disk_status(status));
        else if (reading && blob.size>budget) result=atom_expr2(a,atom_symbol(a,"StoreTooLarge"),atom_int(a,(int64_t)blob.size));
        else if (reading) {
            unsigned char *bytes=malloc(blob.size ? (size_t)blob.size : 1);size_t got=0;
            if (!bytes) result=refused(a,"capacity");
            else {
                status=cetta_disk_blob_read(root,blob,0,bytes,(size_t)blob.size,&got);
                result=status==CETTA_DISK_OK ? atom_expr2(a,atom_symbol(a,"StoreValue"),atom_string_n(a,(const char *)bytes,got)) : status_value(a,disk_status(status));
                free(bytes);
            }
        } else {
            CettaDiskBlobReader *reader=NULL;status=cetta_disk_blob_reader_open(root,blob,&reader);
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
        CettaDiskBlobReader *reader=cetta_native_handle_use_resource(&use);
        uint64_t size=cetta_disk_blob_reader_size(reader);
        if (offset>size) { cetta_native_handle_use_release(&use);return refused(a,"invalid-offset"); }
        if (count>size-offset) count=size-offset;
        unsigned char *bytes=malloc(count ? (size_t)count : 1);size_t got=0;Atom *result;
        if (!bytes) result=refused(a,"capacity");
        else {
            CettaDiskStatus status=cetta_disk_blob_reader_read(reader,offset,bytes,(size_t)count,&got);
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
        const CettaDiskRoot *root=cetta_disk_store_view_root(cetta_native_handle_use_resource(&use));
        CettaDiskStatus status=!scan ? CETTA_DISK_NOMEM : reverse_scan ?
            cetta_disk_cursor_open_reverse_range(root,lo ? &lower : NULL,false,hi ? &upper : NULL,&scan->cursor) :
            cetta_disk_cursor_open_range(root,lo ? &lower : NULL,false,hi ? &upper : NULL,&scan->cursor);
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
            CettaDiskBuffer key={0};CettaDiskBlob blob;
            CettaDiskStatus status=cetta_disk_cursor_next(scan->cursor,&key,&blob);
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
