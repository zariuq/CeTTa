#include "library_store.h"
#include "library.h"
#include "native_disk_store.h"
#include "native_handle.h"
#include "atom_wire.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *owner_kind="disk-store",*view_kind="disk-store-view";
static const char *reader_kind="disk-store-body",*scan_kind="disk-store-scan";
typedef struct { CettaDiskCursor *cursor; CettaDiskBuffer last; CettaDiskBlob blob; uint64_t position; bool done; } Scan;
typedef struct { CettaDiskStoreScope *items; unsigned char **allocated; size_t count; } Scopes;

static void free_owner(void *p) { cetta_disk_store_close(p); }
static void free_view(void *p) { cetta_disk_store_view_close(p); }
static void free_reader(void *p) { cetta_disk_blob_reader_close(p); }
static void free_scan(void *p) {
    Scan *scan=p; cetta_disk_cursor_close(scan->cursor); cetta_disk_buffer_free(&scan->last); free(scan);
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
    Atom *handle=cetta_native_handle_owned_atom(ctx,a,kind,id);
    if (!handle) cetta_native_handle_close(ctx,kind,id);
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
Atom *cetta_store_dispatch(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t n) {
    if (!ctx || !a || !head) return NULL;
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
        const char *kinds[]={owner_kind,view_kind,reader_kind,scan_kind};
        for (size_t i=0;i<4;++i) {
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
    bool commit_call=atom_is_symbol(head,"__cetta_lib_store_commit"),scoped_call=atom_is_symbol(head,"__cetta_lib_store_commit_scoped");
    if (commit_call || scoped_call) {
        if (n!=(scoped_call ? 5u : 3u)) return refused(a,"invalid-commit");
        CettaNativeHandleUse owner={0},view={0};
        if (!acquire(ctx,owner_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&owner)) return refused(a,"closed-or-busy");
        if (!acquire(ctx,view_kind,args[1],CETTA_NATIVE_HANDLE_USE_LIFETIME,&view)) {
            cetta_native_handle_use_release(&owner);return refused(a,"closed-view");
        }
        CettaDiskOp *ops=NULL;size_t count=0;Scopes reads={0},writes={0};CettaDiskStoreView *result=NULL;
        bool valid=operations(args[scoped_call ? 4 : 2],&ops,&count);
        if (valid && scoped_call) valid=scopes(args[2],"reads",&reads) && scopes(args[3],"writes",&writes);
        CettaDiskStoreStatus status=CETTA_STORE_INVALID;
        if (valid) status=scoped_call ? cetta_disk_store_commit_scoped(cetta_native_handle_use_resource(&owner),
            cetta_native_handle_use_resource(&view),reads.items,reads.count,writes.items,writes.count,ops,count,&result) :
            cetta_disk_store_commit(cetta_native_handle_use_resource(&owner),cetta_native_handle_use_resource(&view),ops,count,&result);
        free(ops);scopes_free(&reads);scopes_free(&writes);
        cetta_native_handle_use_release(&view);
        Atom *answer=status==CETTA_STORE_OK ? view_value(ctx,a,result,true,cetta_native_handle_use_resource(&owner)) : status_value(a,status);
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
