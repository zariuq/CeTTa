#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include "native_journal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { META_BYTES=76, META_FRAME_BYTES=148 };
typedef struct {
    unsigned char epoch[16], segment[16];
    uint64_t revision, root, end;
    CettaDiskStoreLimits limits;
} Meta;
struct CettaDiskStore {
    int directory;
    dev_t device;
    ino_t inode;
    bool uncertain, confirmed;
    Meta durable;
};
struct CettaDiskStoreView {
    CettaDiskRoot root;
    Meta meta;
    dev_t device;
    ino_t inode;
};

static uint64_t get_number(const unsigned char *p) {
    uint64_t value=0;
    for (size_t i=0;i<8;++i) value=(value<<8)|p[i];
    return value;
}
static void put_number(unsigned char *p,uint64_t value) {
    for (size_t i=8;i>0;--i) { p[i-1]=(unsigned char)value; value>>=8; }
}
static bool synchronize(int fd) {
    int result;
    do { result=fsync(fd); } while (result<0 && errno==EINTR);
    return !result;
}
static bool safe_file(int fd) {
    struct stat st;
    if (fstat(fd,&st)) return false;
    if (!S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077) || st.st_nlink!=1) {
        errno=EPERM;return false;
    }
    return true;
}
static int file_open(int directory,const char *name,int flags) {
    int fd;
    do { fd=openat(directory,name,flags|O_CLOEXEC|O_NOFOLLOW,0600); } while (fd<0 && errno==EINTR);
    if (fd>=0 && !safe_file(fd)) { int code=errno;close(fd);errno=code;return -1; }
    return fd;
}
static CettaDiskStoreStatus open_error(void) {
    return errno==EPERM || errno==EACCES || errno==ELOOP ? CETTA_STORE_OWNERSHIP : CETTA_STORE_IO;
}
static CettaDiskStoreStatus acquire(CettaDiskStore *store,bool exclusive,int *out) {
    *out=-1;
    int fd=file_open(store->directory,"writer.lock",exclusive ? O_RDWR : O_RDONLY);
    if (fd<0) return open_error();
    int result;
    do { result=flock(fd,(exclusive ? LOCK_EX : LOCK_SH)|LOCK_NB); } while (result<0 && errno==EINTR);
    if (result) {
        int code=errno; close(fd);
        return code==EWOULDBLOCK || code==EAGAIN ? CETTA_STORE_BUSY : CETTA_STORE_IO;
    }
    *out=fd; return CETTA_STORE_OK;
}
static CettaDiskStoreStatus disk_status(CettaDiskStatus status) {
    switch (status) {
    case CETTA_DISK_OK: return CETTA_STORE_OK;
    case CETTA_DISK_ABSENT: return CETTA_STORE_ABSENT;
    case CETTA_DISK_INVALID: return CETTA_STORE_INVALID;
    case CETTA_DISK_NOMEM: return CETTA_STORE_NOMEM;
    case CETTA_DISK_LIMIT: return CETTA_STORE_LIMIT;
    case CETTA_DISK_CORRUPT: return CETTA_STORE_CORRUPT;
    default: return CETTA_STORE_IO;
    }
}
static void segment_name(const unsigned char id[16],char name[45]) {
    static const char digits[]="0123456789abcdef";
    memcpy(name,"segment-",8);
    for (size_t i=0;i<16;++i) { name[8+i*2]=digits[id[i]>>4]; name[9+i*2]=digits[id[i]&15]; }
    memcpy(name+40,".csi",5);
}
static bool random_bytes(unsigned char out[16]) {
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if (fd<0) return false;
    size_t position=0;
    while (position<16) {
        ssize_t n=read(fd,out+position,16-position);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { close(fd); return false; }
        position+=(size_t)n;
    }
    close(fd); return true;
}
/* Creation and the builder's lock are installed while holding writer.lock.
 * The lock protects a candidate from collection while its copy runs outside
 * the writer lock. Kernel lock release also handles a builder's process death. */
static CettaDiskStoreStatus segment_create(CettaDiskStore *store,unsigned char id[16],int *out) {
    *out=-1;
    for (;;) {
        if (!random_bytes(id)) return CETTA_STORE_IO;
        unsigned char nonzero=0;for (size_t i=0;i<16;++i) nonzero|=id[i];
        if (!nonzero) continue;
        char name[45]; segment_name(id,name);
        int fd=file_open(store->directory,name,O_RDWR|O_CREAT|O_EXCL);
        if (fd<0) { if (errno==EEXIST) continue; return open_error(); }
        if (flock(fd,LOCK_EX|LOCK_NB)) { close(fd); unlinkat(store->directory,name,0); return CETTA_STORE_IO; }
        CettaDiskStoreStatus status=disk_status(cetta_disk_segment_create(fd,id));
        if (status!=CETTA_STORE_OK) { close(fd);unlinkat(store->directory,name,0);return status; }
        *out=fd; return CETTA_STORE_OK;
    }
}
static void encode_meta(const Meta *meta,unsigned char bytes[META_BYTES]) {
    memcpy(bytes,"CDS2",4); memcpy(bytes+4,meta->epoch,16); memcpy(bytes+20,meta->segment,16);
    put_number(bytes+36,meta->revision); put_number(bytes+44,meta->root); put_number(bytes+52,meta->end);
    put_number(bytes+60,meta->limits.key_bytes); put_number(bytes+68,meta->limits.value_bytes);
}
static bool write_meta(int fd,uint64_t *written,void *context) {
    unsigned char bytes[META_BYTES]; encode_meta(context,bytes);
    return cetta_journal_append(fd,bytes,sizeof(bytes),false,written)==CETTA_JOURNAL_OK;
}
static CettaDiskStoreStatus read_meta(int directory,bool writing,Meta *meta,int *out) {
    *out=-1;
    int fd=file_open(directory,"root.csl",writing ? O_RDWR : O_RDONLY);
    if (fd<0) return errno==ENOENT ? CETTA_STORE_ABSENT : open_error();
    struct stat st;
    if (fstat(fd,&st)) { close(fd); return CETTA_STORE_IO; }
    if (st.st_size!=META_FRAME_BYTES) { close(fd); return CETTA_STORE_CORRUPT; }
    CettaJournalRecord record={0};
    CettaJournalStatus result=cetta_journal_read(fd,0,(uint64_t)st.st_size,META_BYTES,&record);
    CettaDiskStoreStatus status=CETTA_STORE_CORRUPT;
    if (result==CETTA_JOURNAL_OK && record.size==META_BYTES) {
        if (memcmp(record.data,"CDS2",4)) status=CETTA_STORE_VERSION;
        else {
            uint64_t keys=get_number(record.data+60),values=get_number(record.data+68);
            memcpy(meta->epoch,record.data+4,16); memcpy(meta->segment,record.data+20,16);
            meta->revision=get_number(record.data+36); meta->root=get_number(record.data+44);
            meta->end=get_number(record.data+52);
            if (keys<=SIZE_MAX && values<=SIZE_MAX && keys<=UINT32_MAX-13u && values<=UINT32_MAX-1u &&
                meta->end>=CETTA_DISK_SEGMENT_HEADER_BYTES && meta->end<=INT64_MAX &&
                (!meta->root || meta->root<=meta->end)) {
                meta->limits=(CettaDiskStoreLimits){(size_t)keys,(size_t)values}; status=CETTA_STORE_OK;
            }
        }
    } else if (result==CETTA_JOURNAL_NOMEM) status=CETTA_STORE_NOMEM;
    else if (result==CETTA_JOURNAL_IO) status=CETTA_STORE_IO;
    cetta_journal_record_free(&record);
    if (status!=CETTA_STORE_OK) close(fd); else *out=fd;
    return status;
}
static CettaDiskStoreStatus view_load(CettaDiskStore *store,bool writing,
                                      CettaDiskStoreView **out,int *manifest) {
    *out=NULL; *manifest=-1;
    Meta meta;
    CettaDiskStoreStatus status=read_meta(store->directory,writing,&meta,manifest);
    if (status!=CETTA_STORE_OK) return status;
    char name[45]; segment_name(meta.segment,name);
    int fd=file_open(store->directory,name,writing ? O_RDWR : O_RDONLY);
    struct stat st;
    if (fd<0) status=open_error();
    else if (fstat(fd,&st)) status=CETTA_STORE_IO;
    else if (st.st_size<0 || (uint64_t)st.st_size<meta.end) status=CETTA_STORE_CORRUPT;
    else {
        CettaDiskStoreView *view=calloc(1,sizeof(*view));
        if (!view) status=CETTA_STORE_NOMEM;
        else {
            view->root=(CettaDiskRoot){.fd=fd,.root=meta.root,.end=meta.end,
                .key_limit=meta.limits.key_bytes,.value_limit=meta.limits.value_bytes};
            memcpy(view->root.segment,meta.segment,16);
            status=disk_status(cetta_disk_root_check(&view->root));
            if (status!=CETTA_STORE_OK) { free(view);goto failed; }
            view->meta=meta; view->device=store->device; view->inode=store->inode;
            *out=view; return CETTA_STORE_OK;
        }
    }
failed:
    if (fd>=0) close(fd);
    close(*manifest); *manifest=-1;
    return status;
}
static CettaDiskStoreStatus publish(CettaDiskStore *store,int *manifest,const Meta *meta) {
    CettaJournalStatus status=cetta_journal_checkpoint(store->directory,"root.csl","root.next",
        write_meta,(void *)meta,manifest,NULL);
    if (status==CETTA_JOURNAL_OK) {
        store->durable=*meta; store->confirmed=true; return CETTA_STORE_OK;
    }
    if (status==CETTA_JOURNAL_UNKNOWN) { store->uncertain=true; return CETTA_STORE_UNKNOWN; }
    return status==CETTA_JOURNAL_NOMEM ? CETTA_STORE_NOMEM : CETTA_STORE_IO;
}
CettaDiskStoreLimits cetta_disk_store_limits(void) {
    return (CettaDiskStoreLimits){65536,UINT32_MAX-1u};
}
CettaDiskStoreStatus cetta_disk_store_open(const char *path,bool create,
        const CettaDiskStoreLimits *limits,CettaDiskStore **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!path || !*path) return CETTA_STORE_INVALID;
    int directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (directory<0) {
        int code=errno;struct stat entry;
        if (code==ENOTDIR && !lstat(path,&entry) && S_ISLNK(entry.st_mode)) return CETTA_STORE_OWNERSHIP;
        errno=code;return code==ENOENT ? CETTA_STORE_ABSENT : open_error();
    }
    struct stat st;
    if (fstat(directory,&st)) { close(directory);return CETTA_STORE_IO; }
    if (st.st_uid!=geteuid() || (st.st_mode&077)) { close(directory);return CETTA_STORE_OWNERSHIP; }
    CettaDiskStore *store=calloc(1,sizeof(*store));
    if (!store) { close(directory); return CETTA_STORE_NOMEM; }
    *store=(CettaDiskStore){.directory=directory,.device=st.st_dev,.inode=st.st_ino};
    if (create) {
        int fd=file_open(directory,"writer.lock",O_RDWR|O_CREAT);
        if (fd<0) { CettaDiskStoreStatus status=open_error();cetta_disk_store_close(store);return status; }
        close(fd);
    } else {
        struct stat root;
        if (fstatat(directory,"writer.lock",&root,AT_SYMLINK_NOFOLLOW) && errno==ENOENT) {
            bool absent=fstatat(directory,"root.csl",&root,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT;
            cetta_disk_store_close(store); return absent ? CETTA_STORE_ABSENT : CETTA_STORE_CORRUPT;
        }
    }
    int lock=-1; CettaDiskStoreStatus status=acquire(store,false,&lock);
    if (status!=CETTA_STORE_OK) { cetta_disk_store_close(store); return status; }
    Meta meta; int manifest=-1;
    status=read_meta(directory,false,&meta,&manifest);
    if (status==CETTA_STORE_ABSENT && create) {
        close(lock);lock=-1;
        status=acquire(store,true,&lock);
        if (status==CETTA_STORE_OK) status=read_meta(directory,false,&meta,&manifest);
    }
    if (status==CETTA_STORE_ABSENT && create) {
        CettaDiskStoreLimits actual=limits ? *limits : cetta_disk_store_limits();
        if (actual.key_bytes>UINT32_MAX-13u || actual.value_bytes>UINT32_MAX-1u) status=CETTA_STORE_INVALID;
        else {
            meta=(Meta){.end=CETTA_DISK_SEGMENT_HEADER_BYTES,.limits=actual}; int segment=-1;
            if (!random_bytes(meta.epoch)) status=CETTA_STORE_IO;
            else status=segment_create(store,meta.segment,&segment);
            if (status==CETTA_STORE_OK) {
                manifest=file_open(directory,"root.init",O_RDWR|O_CREAT);
                if (manifest<0 || !synchronize(segment)) status=CETTA_STORE_IO;
                else status=publish(store,&manifest,&meta);
                close(segment);
                if (status==CETTA_STORE_OK) unlinkat(directory,"root.init",0);
            }
        }
    }
    if (manifest>=0) { close(manifest); manifest=-1; }
    if (status==CETTA_STORE_OK) {
        CettaDiskStoreView *view=NULL;
        status=view_load(store,false,&view,&manifest);
        cetta_disk_store_view_close(view);
        if (manifest>=0) close(manifest);
    }
    if (lock>=0) close(lock);
    if (status!=CETTA_STORE_OK) { cetta_disk_store_close(store); return status; }
    *out=store; return CETTA_STORE_OK;
}
void cetta_disk_store_close(CettaDiskStore *store) {
    if (store) { close(store->directory); free(store); }
}
void cetta_disk_store_require_recovery(CettaDiskStore *store) {
    if (store) store->uncertain=true;
}
void cetta_disk_store_view_close(CettaDiskStoreView *view) {
    if (view) { close(view->root.fd); free(view); }
}
const CettaDiskRoot *cetta_disk_store_view_root(const CettaDiskStoreView *view) { return view ? &view->root : NULL; }
uint64_t cetta_disk_store_view_revision(const CettaDiskStoreView *view) { return view ? view->meta.revision : 0; }
const unsigned char *cetta_disk_store_view_epoch(const CettaDiskStoreView *view) { return view ? view->meta.epoch : NULL; }
static bool equal_meta(const Meta *left,const Meta *right) {
    return !memcmp(left->epoch,right->epoch,16) && !memcmp(left->segment,right->segment,16) &&
        left->revision==right->revision && left->root==right->root && left->end==right->end &&
        left->limits.key_bytes==right->limits.key_bytes && left->limits.value_bytes==right->limits.value_bytes;
}
/* A different process can die after rename and before directory sync. A
 * first observation of a root confirms it before trusting an application
 * receipt. Previously confirmed immutable roots need no repeated barrier. */
static CettaDiskStoreStatus confirm(CettaDiskStore *store,const Meta *meta) {
    if (store->confirmed && equal_meta(&store->durable,meta)) return CETTA_STORE_OK;
    char name[45]; segment_name(meta->segment,name);
    int segment=file_open(store->directory,name,O_RDWR);
    int manifest=file_open(store->directory,"root.csl",O_RDWR);
    bool ok=segment>=0 && manifest>=0 && synchronize(segment) &&
        synchronize(manifest) && synchronize(store->directory);
    if (segment>=0) close(segment);
    if (manifest>=0) close(manifest);
    if (!ok) return CETTA_STORE_IO;
    store->durable=*meta; store->confirmed=true; return CETTA_STORE_OK;
}
CettaDiskStoreStatus cetta_disk_store_snapshot(CettaDiskStore *store,CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!store) return CETTA_STORE_INVALID;
    int lock=-1,manifest=-1;
    CettaDiskStoreStatus status=acquire(store,false,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,out,&manifest);
    if (status==CETTA_STORE_OK) {
        status=confirm(store,&(*out)->meta);
        if (status!=CETTA_STORE_OK) { cetta_disk_store_view_close(*out); *out=NULL; }
    }
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    return status;
}
static bool same_domain(CettaDiskStore *store,const CettaDiskStoreView *expected,const CettaDiskStoreView *current) {
    return expected && expected->device==store->device && expected->inode==store->inode &&
        !memcmp(expected->meta.epoch,current->meta.epoch,16);
}
CettaDiskStoreStatus cetta_disk_store_check_view(CettaDiskStore *store,
        const CettaDiskStoreView *view) {
    if (!store || !view) return CETTA_STORE_INVALID;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    CettaDiskStoreView *current=NULL;
    CettaDiskStoreStatus status=cetta_disk_store_snapshot(store,&current);
    if (status==CETTA_STORE_OK && !same_domain(store,view,current))
        status=CETTA_STORE_CONFLICT;
    cetta_disk_store_view_close(current);
    return status;
}
static bool same_root(const CettaDiskStoreView *left,const CettaDiskStoreView *right) {
    return left->meta.revision==right->meta.revision && left->meta.root==right->meta.root &&
        left->meta.end==right->meta.end && !memcmp(left->meta.segment,right->meta.segment,16);
}
static CettaDiskStoreStatus readonly_view(CettaDiskStore *store,CettaDiskStoreView *view) {
    char name[45]; segment_name(view->meta.segment,name);
    int reader=file_open(store->directory,name,O_RDONLY);
    if (reader<0) return open_error();
    close(view->root.fd); view->root.fd=reader; return CETTA_STORE_OK;
}
static bool key_valid(CettaDiskBytes key,size_t limit) {
    return key.size<=limit && (!key.size || key.data);
}
static int key_order(CettaDiskBytes left,CettaDiskBytes right) {
    size_t common=left.size<right.size ? left.size : right.size;
    int order=common ? memcmp(left.data,right.data,common) : 0;
    return order ? order : left.size<right.size ? -1 : left.size>right.size;
}
static bool scope_valid(const CettaDiskStoreScope *scope,size_t limit) {
    if (scope->kind==CETTA_STORE_POINT) return key_valid(scope->lower,limit);
    return scope->kind==CETTA_STORE_RANGE && (!scope->has_lower || key_valid(scope->lower,limit)) &&
        (!scope->has_upper || key_valid(scope->upper,limit));
}
static bool scope_covers(const CettaDiskStoreScope *scope,CettaDiskBytes key) {
    if (scope->kind==CETTA_STORE_POINT) return !key_order(key,scope->lower);
    return (!scope->has_lower || key_order(key,scope->lower)>=0) &&
        (!scope->has_upper || key_order(key,scope->upper)<0);
}
static CettaDiskStoreStatus bodies_equal(const CettaDiskRoot *left,CettaDiskBlob a,
        const CettaDiskRoot *right,CettaDiskBlob b,bool same_file,bool *equal) {
    *equal=false;
    if (a.size!=b.size) return CETTA_STORE_OK;
    if (same_file && a.reference==b.reference) { *equal=true; return CETTA_STORE_OK; }
    CettaDiskBlobReader *ar=NULL,*br=NULL;
    CettaDiskStatus status=cetta_disk_blob_reader_open(left,a,&ar);
    if (status==CETTA_DISK_OK) status=cetta_disk_blob_reader_open(right,b,&br);
    unsigned char ab[32768],bb[32768];
    for (uint64_t offset=0;status==CETTA_DISK_OK && offset<a.size;) {
        size_t an=0,bn=0;
        status=cetta_disk_blob_reader_read(ar,offset,ab,sizeof(ab),&an);
        if (status==CETTA_DISK_OK) status=cetta_disk_blob_reader_read(br,offset,bb,sizeof(bb),&bn);
        if (status!=CETTA_DISK_OK) break;
        if (an!=bn || !an) { status=CETTA_DISK_CORRUPT; break; }
        if (memcmp(ab,bb,an)) goto done;
        offset+=an;
    }
    *equal=status==CETTA_DISK_OK;
done:
    cetta_disk_blob_reader_close(ar); cetta_disk_blob_reader_close(br);
    return disk_status(status);
}
static CettaDiskStoreStatus scope_equal(const CettaDiskStoreView *before,
        const CettaDiskStoreView *current,const CettaDiskStoreScope *scope,bool same_file,bool *equal) {
    *equal=false;
    if (scope->kind==CETTA_STORE_POINT) {
        CettaDiskBlob old,now;
        CettaDiskStatus a=cetta_disk_lookup(&before->root,scope->lower,&old);
        CettaDiskStatus b=cetta_disk_lookup(&current->root,scope->lower,&now);
        if (a==CETTA_DISK_ABSENT && b==CETTA_DISK_ABSENT) { *equal=true; return CETTA_STORE_OK; }
        if (a!=CETTA_DISK_OK && a!=CETTA_DISK_ABSENT) return disk_status(a);
        if (b!=CETTA_DISK_OK && b!=CETTA_DISK_ABSENT) return disk_status(b);
        if (a!=b) return CETTA_STORE_OK;
        return bodies_equal(&before->root,old,&current->root,now,same_file,equal);
    }
    const CettaDiskBytes *lower=scope->has_lower ? &scope->lower : NULL;
    const CettaDiskBytes *upper=scope->has_upper ? &scope->upper : NULL;
    CettaDiskCursor *old=NULL,*now=NULL; CettaDiskStoreStatus status=CETTA_STORE_OK;
    CettaDiskStatus a=cetta_disk_cursor_open_range(&before->root,lower,false,upper,&old);
    if (a==CETTA_DISK_OK) a=cetta_disk_cursor_open_range(&current->root,lower,false,upper,&now);
    if (a!=CETTA_DISK_OK) { status=disk_status(a); goto done; }
    for (;;) {
        CettaDiskBuffer ak={0},bk={0}; CettaDiskBlob av,bv;
        a=cetta_disk_cursor_next(old,&ak,&av);
        CettaDiskStatus b=cetta_disk_cursor_next(now,&bk,&bv);
        bool end=a==CETTA_DISK_END && b==CETTA_DISK_END,match=false;
        if (a!=CETTA_DISK_OK && a!=CETTA_DISK_END) status=disk_status(a);
        else if (b!=CETTA_DISK_OK && b!=CETTA_DISK_END) status=disk_status(b);
        else if (end) match=true;
        else if (a==CETTA_DISK_OK && b==CETTA_DISK_OK &&
            !key_order((CettaDiskBytes){ak.data,ak.size},(CettaDiskBytes){bk.data,bk.size}))
            status=bodies_equal(&before->root,av,&current->root,bv,same_file,&match);
        cetta_disk_buffer_free(&ak); cetta_disk_buffer_free(&bk);
        if (end || !match || status!=CETTA_STORE_OK) { *equal=end && status==CETTA_STORE_OK; break; }
    }
done:
    cetta_disk_cursor_close(old); cetta_disk_cursor_close(now); return status;
}
static CettaDiskStoreStatus commit(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskOp *operations,size_t count,
    const CettaDiskStoreScope *reads,size_t read_count,const CettaDiskStoreScope *writes,size_t write_count,
    bool scoped,CettaDiskStoreView **committed) {
    if (!committed) return CETTA_STORE_INVALID;
    *committed=NULL;
    if (!store || !expected || (count && !operations) || (read_count && !reads) ||
        (write_count && !writes)) return CETTA_STORE_INVALID;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    int lock=-1,manifest=-1; CettaDiskStoreView *current=NULL;
    CettaDiskStoreStatus status=acquire(store,true,&lock);
    if (status!=CETTA_STORE_OK) return status;
    status=view_load(store,true,&current,&manifest);
    if (status!=CETTA_STORE_OK) goto done;
    if (!same_domain(store,expected,current) || (!scoped && expected->meta.revision!=current->meta.revision)) {
        status=CETTA_STORE_CONFLICT; goto done;
    }
    if (scoped) {
        for (size_t i=0;i<read_count+write_count;++i) {
            const CettaDiskStoreScope *scope=i<read_count ? &reads[i] : &writes[i-read_count];
            if (!scope_valid(scope,current->root.key_limit)) { status=CETTA_STORE_INVALID; goto done; }
        }
        for (size_t i=0;i<count;++i) {
            if (!key_valid(operations[i].key,current->root.key_limit)) { status=CETTA_STORE_INVALID; goto done; }
            bool covered=false;
            for (size_t j=0;j<write_count;++j) if (scope_covers(&writes[j],operations[i].key)) { covered=true; break; }
            if (!covered) { status=CETTA_STORE_INVALID; goto done; }
        }
        struct stat old_stat,new_stat;
        if (fstat(expected->root.fd,&old_stat) || fstat(current->root.fd,&new_stat)) { status=CETTA_STORE_IO; goto done; }
        bool same_file=old_stat.st_dev==new_stat.st_dev && old_stat.st_ino==new_stat.st_ino;
        for (size_t i=0;i<read_count+write_count;++i) {
            const CettaDiskStoreScope *scope=i<read_count ? &reads[i] : &writes[i-read_count];
            bool equal=false; status=scope_equal(expected,current,scope,same_file,&equal);
            if (status!=CETTA_STORE_OK) goto done;
            if (!equal) { status=CETTA_STORE_CONFLICT; goto done; }
        }
    }
    if (current->meta.revision==UINT64_MAX) { status=CETTA_STORE_LIMIT; goto done; }
    /* Failed candidates are never exposed as views. Every published root in
     * this segment ends no later than current.end, so this unreferenced tail
     * can be reclaimed without harming any old reader or compaction source. */
    struct stat physical;
    if (fstat(current->root.fd,&physical) || physical.st_size<0 ||
        ((uint64_t)physical.st_size>current->root.end && ftruncate(current->root.fd,(off_t)current->root.end))) {
        status=CETTA_STORE_IO; goto done;
    }
    CettaDiskRoot candidate;
    status=disk_status(cetta_disk_build(&current->root,operations,count,&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    status=disk_status(cetta_disk_sync(&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    Meta next=current->meta; ++next.revision; next.root=candidate.root; next.end=candidate.end;
    status=publish(store,&manifest,&next);
    if (status==CETTA_STORE_OK) {
        current->root=candidate; current->meta=next;
        if (readonly_view(store,current)!=CETTA_STORE_OK) {
            store->uncertain=true; status=CETTA_STORE_UNKNOWN;
        } else { *committed=current; current=NULL; }
    }
done:
    cetta_disk_store_view_close(current);
    if (manifest>=0) close(manifest);
    close(lock); return status;
}
CettaDiskStoreStatus cetta_disk_store_commit(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskOp *operations,size_t count,
        CettaDiskStoreView **committed) {
    return commit(store,expected,operations,count,NULL,0,NULL,0,false,committed);
}
CettaDiskStoreStatus cetta_disk_store_commit_scoped(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskStoreScope *reads,size_t read_count,
        const CettaDiskStoreScope *writes,size_t write_count,const CettaDiskOp *operations,size_t count,
        CettaDiskStoreView **committed) {
    if (read_count>SIZE_MAX-write_count) {
        if (committed) *committed=NULL;
        return CETTA_STORE_INVALID;
    }
    return commit(store,expected,operations,count,reads,read_count,writes,write_count,true,committed);
}
CettaDiskStoreStatus cetta_disk_store_recover(CettaDiskStore *store,CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!store) return CETTA_STORE_INVALID;
    int lock=-1,manifest=-1; CettaDiskStoreView *view=NULL;
    CettaDiskStoreStatus status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&view,&manifest);
    if (status==CETTA_STORE_OK) {
        status=confirm(store,&view->meta);
        if (status!=CETTA_STORE_OK) { /* The handle remains write-poisoned. */ }
        else { store->uncertain=false; *out=view; view=NULL; }
    }
    cetta_disk_store_view_close(view);
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    return status;
}
CettaDiskStoreStatus cetta_disk_store_compact(CettaDiskStore *store,CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!store) return CETTA_STORE_INVALID;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    CettaDiskStoreView *source=NULL,*current=NULL;
    int lock=-1,manifest=-1,segment=-1; unsigned char id[16];
    CettaDiskStoreStatus status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&source,&manifest);
    if (status==CETTA_STORE_OK) status=segment_create(store,id,&segment);
    if (manifest>=0) { close(manifest); manifest=-1; }
    if (lock>=0) { close(lock); lock=-1; }
    if (status!=CETTA_STORE_OK) goto done;
    CettaDiskRoot candidate;
    status=disk_status(cetta_disk_compact(&source->root,segment,&candidate));
    if (status==CETTA_STORE_OK) status=disk_status(cetta_disk_sync(&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&current,&manifest);
    if (status!=CETTA_STORE_OK) goto done;
    if (!same_domain(store,source,current) || !same_root(source,current)) { status=CETTA_STORE_CONFLICT; goto done; }
    Meta next=current->meta; memcpy(next.segment,id,16); next.root=candidate.root; next.end=candidate.end;
    status=publish(store,&manifest,&next);
    if (status==CETTA_STORE_OK) {
        /* Keep only a read descriptor in the view: never retain the builder's
         * file lock after the operation, including through its returned view. */
        char name[45]; segment_name(id,name);
        int reader=file_open(store->directory,name,O_RDONLY);
        if (reader<0) { store->uncertain=true; status=CETTA_STORE_UNKNOWN; }
        else {
            close(current->root.fd); current->root=candidate; current->root.fd=reader; current->meta=next;
            *out=current; current=NULL;
        }
    }
done:
    cetta_disk_store_view_close(source); cetta_disk_store_view_close(current);
    if (manifest>=0) close(manifest);
    if (segment>=0) close(segment);
    if (lock>=0) close(lock);
    return status;
}
CettaDiskStoreStatus cetta_disk_store_collect(CettaDiskStore *store,uint64_t *removed) {
    if (!store || !removed) return CETTA_STORE_INVALID;
    *removed=0;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    int lock=-1,manifest=-1; CettaDiskStoreView *current=NULL;
    CettaDiskStoreStatus status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&current,&manifest);
    if (status!=CETTA_STORE_OK) goto done;
    /* Another handle may have observed a rename with an uncertain sync.
     * Make the visible root durable before retiring any alternative segment. */
    status=confirm(store,&current->meta);
    if (status!=CETTA_STORE_OK) goto done;
    char retained[45]; segment_name(current->meta.segment,retained);
    int directory=openat(store->directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    DIR *scan=directory<0 ? NULL : fdopendir(directory);
    if (!scan) { if (directory>=0) close(directory); status=CETTA_STORE_IO; goto done; }
    errno=0;
    for (;;) {
        struct dirent *entry=readdir(scan);
        if (!entry) { if (errno) status=CETTA_STORE_IO; break; }
        const char *name=entry->d_name;
        if (strlen(name)!=44 || strncmp(name,"segment-",8) || strcmp(name+40,".csi") ||
            !strcmp(name,retained)) continue;
        size_t hex=strspn(name+8,"0123456789abcdef");
        if (hex!=32) continue;
        int fd=file_open(store->directory,name,O_RDWR);
        if (fd<0) { status=CETTA_STORE_IO; break; }
        if (!flock(fd,LOCK_EX|LOCK_NB)) {
            if (unlinkat(store->directory,name,0)) status=CETTA_STORE_IO;
            else ++*removed;
        } else if (errno!=EWOULDBLOCK && errno!=EAGAIN) status=CETTA_STORE_IO;
        close(fd);
        if (status!=CETTA_STORE_OK) break;
        errno=0;
    }
    closedir(scan);
    if (status==CETTA_STORE_OK && *removed && !synchronize(store->directory)) status=CETTA_STORE_IO;
done:
    cetta_disk_store_view_close(current);
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    return status;
}
