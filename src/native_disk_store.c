#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include "native_journal.h"
#include "native_act_base.h"
#include "storage_census.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { META_BYTES=76, META_FRAME_BYTES=148, META3_BYTES=196, META3_FRAME_BYTES=268 };
typedef struct {
    unsigned char epoch[16], segment[16];
    uint64_t revision, root, end;
    CettaDiskStoreLimits limits;
    unsigned format, frozen_kind;
    unsigned char base[16], base_digest[32], frozen[16];
    uint64_t removes, base_bytes, frozen_puts, frozen_removes, frozen_end, frozen_revision;
} Meta;
struct CettaDiskStore {
    atomic_size_t references;
    int directory;
    dev_t device;
    ino_t inode;
    bool uncertain, confirmed;
    Meta durable;
    CettaActBase *base;
};
struct CettaDiskStoreView {
    CettaDiskRoot root, removes, frozen_puts, frozen_removes;
    CettaActBase *base;
    Meta meta;
    dev_t device;
    ino_t inode;
};
struct CettaDiskStoreBatch {
    CettaDiskRoot puts, removes;
    dev_t device;ino_t inode;unsigned char epoch[16];
    uint64_t maximum, staged;
    bool failed;
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

static bool nonzero(const unsigned char *bytes,size_t count) {
    unsigned char combined=0;for (size_t i=0;i<count;i++) combined|=bytes[i];return combined!=0;
}
static void base_name(const unsigned char id[16],char name[45]) {
    static const char digits[]="0123456789abcdef";memcpy(name,"base-",5);
    for (size_t i=0;i<16;i++) {name[5+i*2]=digits[id[i]>>4];name[6+i*2]=digits[id[i]&15];}
    memcpy(name+37,".cbase",7);
}
static CettaDiskStatus act_status(CettaActStatus status) {
    switch (status) {
    case CETTA_ACT_OK:return CETTA_DISK_OK;
    case CETTA_ACT_END:return CETTA_DISK_END;
    case CETTA_ACT_NOT_FOUND:return CETTA_DISK_ABSENT;
    case CETTA_ACT_NOMEM:return CETTA_DISK_NOMEM;
    case CETTA_ACT_LIMIT:return CETTA_DISK_LIMIT;
    case CETTA_ACT_IO:return CETTA_DISK_IO;
    default:return CETTA_DISK_CORRUPT;
    }
}
static size_t encode_meta(const Meta *meta,unsigned char bytes[META3_BYTES]) {
    memset(bytes,0,META3_BYTES);
    memcpy(bytes,meta->format==3 ? "CDS3" : "CDS2",4);
    memcpy(bytes+4,meta->epoch,16);memcpy(bytes+20,meta->segment,16);
    put_number(bytes+36,meta->revision);put_number(bytes+44,meta->root);put_number(bytes+52,meta->end);
    put_number(bytes+60,meta->limits.key_bytes);put_number(bytes+68,meta->limits.value_bytes);
    if (meta->format!=3) return META_BYTES;
    put_number(bytes+76,meta->removes);memcpy(bytes+84,meta->base,16);put_number(bytes+100,meta->base_bytes);
    memcpy(bytes+108,meta->base_digest,32);memcpy(bytes+140,meta->frozen,16);
    put_number(bytes+156,meta->frozen_puts);put_number(bytes+164,meta->frozen_removes);
    put_number(bytes+172,meta->frozen_end);put_number(bytes+180,meta->frozen_revision);
    put_number(bytes+188,meta->frozen_kind);return META3_BYTES;
}
static bool write_meta(int fd,uint64_t *written,void *context) {
    unsigned char bytes[META3_BYTES];size_t size=encode_meta(context,bytes);
    return cetta_journal_append(fd,bytes,size,false,written)==CETTA_JOURNAL_OK;
}
static CettaDiskStoreStatus read_meta(int directory,bool writing,Meta *meta,int *out) {
    *out=-1;*meta=(Meta){0};
    int fd=file_open(directory,"root.csl",writing ? O_RDWR : O_RDONLY);
    if (fd<0) return errno==ENOENT ? CETTA_STORE_ABSENT : open_error();
    struct stat st;
    if (fstat(fd,&st)) {close(fd);return CETTA_STORE_IO;}
    if (st.st_size!=META_FRAME_BYTES && st.st_size!=META3_FRAME_BYTES) {close(fd);return CETTA_STORE_CORRUPT;}
    CettaJournalRecord record={0};
    CettaJournalStatus result=cetta_journal_read(fd,0,(uint64_t)st.st_size,META3_BYTES,&record);
    CettaDiskStoreStatus status=CETTA_STORE_CORRUPT;
    if (result==CETTA_JOURNAL_OK) {
        bool v2=record.size==META_BYTES && !memcmp(record.data,"CDS2",4);
        bool v3=record.size==META3_BYTES && !memcmp(record.data,"CDS3",4);
        if (!v2 && !v3) status=CETTA_STORE_VERSION;
        else {
            uint64_t keys=get_number(record.data+60),values=get_number(record.data+68);
            memcpy(meta->epoch,record.data+4,16);memcpy(meta->segment,record.data+20,16);
            meta->revision=get_number(record.data+36);meta->root=get_number(record.data+44);
            meta->end=get_number(record.data+52);meta->format=v3 ? 3u : 2u;
            bool valid=keys<=SIZE_MAX && values<=SIZE_MAX && keys<=UINT32_MAX-13u && values<=UINT32_MAX-1u &&
                meta->end>=CETTA_DISK_SEGMENT_HEADER_BYTES && meta->end<=INT64_MAX &&
                (!meta->root || meta->root<=meta->end) && nonzero(meta->epoch,16) && nonzero(meta->segment,16);
            if (v3) {
                meta->removes=get_number(record.data+76);memcpy(meta->base,record.data+84,16);
                meta->base_bytes=get_number(record.data+100);memcpy(meta->base_digest,record.data+108,32);
                memcpy(meta->frozen,record.data+140,16);meta->frozen_puts=get_number(record.data+156);
                meta->frozen_removes=get_number(record.data+164);meta->frozen_end=get_number(record.data+172);
                meta->frozen_revision=get_number(record.data+180);uint64_t kind=get_number(record.data+188);
                valid=valid && meta->removes<=meta->end && kind<=2;
                meta->frozen_kind=(unsigned)kind;
                if (nonzero(meta->base,16)) valid=valid && meta->base_bytes>=186 && meta->base_bytes<=INT64_MAX;
                else valid=valid && !meta->base_bytes && !nonzero(meta->base_digest,32);
                if (kind) valid=valid && nonzero(meta->frozen,16) &&
                    meta->frozen_end>=CETTA_DISK_SEGMENT_HEADER_BYTES && meta->frozen_end<=INT64_MAX &&
                    meta->frozen_puts<=meta->frozen_end && meta->frozen_removes<=meta->frozen_end &&
                    meta->frozen_revision<=meta->revision && memcmp(meta->segment,meta->frozen,16) &&
                    (kind!=1 || (!meta->frozen_removes && !nonzero(meta->base,16)));
                else valid=valid && !nonzero(meta->frozen,16) && !meta->frozen_puts && !meta->frozen_removes &&
                    !meta->frozen_end && !meta->frozen_revision;
            }
            if (valid) {meta->limits=(CettaDiskStoreLimits){(size_t)keys,(size_t)values};status=CETTA_STORE_OK;}
        }
    } else if (result==CETTA_JOURNAL_NOMEM) status=CETTA_STORE_NOMEM;
    else if (result==CETTA_JOURNAL_IO) status=CETTA_STORE_IO;
    cetta_journal_record_free(&record);
    if (status!=CETTA_STORE_OK) close(fd);else *out=fd;
    return status;
}
static CettaDiskStoreStatus view_base(CettaDiskStore *store,CettaDiskStoreView *view) {
    const Meta *meta=&view->meta;
    if (!nonzero(meta->base,16)) return CETTA_STORE_OK;
    CettaActBaseInfo info;
    if (store->base && cetta_act_base_info(store->base,&info) &&
        !memcmp(info.generation,meta->base,16) && !memcmp(info.digest,meta->base_digest,32) &&
        info.bytes==meta->base_bytes && info.limits.key_bytes==meta->limits.key_bytes &&
        info.limits.value_bytes==meta->limits.value_bytes) {
        view->base=store->base;cetta_act_base_retain(view->base);return CETTA_STORE_OK;
    }
    char name[45];base_name(meta->base,name);int fd=file_open(store->directory,name,O_RDONLY);
    if (fd<0) return open_error();
    CettaActBase *base=NULL;CettaDiskStoreStatus status=disk_status(act_status(cetta_act_base_open_fd(fd,&base)));
    close(fd);
    if (status!=CETTA_STORE_OK) return status;
    cetta_act_base_info(base,&info);
    if (memcmp(info.generation,meta->base,16) || memcmp(info.digest,meta->base_digest,32) ||
        info.bytes!=meta->base_bytes || info.limits.key_bytes!=meta->limits.key_bytes ||
        info.limits.value_bytes!=meta->limits.value_bytes) {
        cetta_act_base_release(base);return CETTA_STORE_CORRUPT;
    }
    cetta_act_base_release(store->base);store->base=base;
    view->base=base;cetta_act_base_retain(base);return CETTA_STORE_OK;
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
            view->frozen_puts.fd=-1;
            view->root=(CettaDiskRoot){.fd=fd,.root=meta.root,.end=meta.end,
                .key_limit=meta.limits.key_bytes,.value_limit=meta.limits.value_bytes};
            memcpy(view->root.segment,meta.segment,16);
            status=disk_status(cetta_disk_root_check(&view->root));
            if (status!=CETTA_STORE_OK) { free(view);goto failed; }
            view->meta=meta; view->device=store->device; view->inode=store->inode;
            view->removes=view->root;view->removes.root=meta.removes;
            if (meta.format==3) status=disk_status(cetta_disk_root_check(&view->removes));
            if (status==CETTA_STORE_OK && meta.frozen_kind) {
                char frozen_name[45];segment_name(meta.frozen,frozen_name);
                int frozen=file_open(store->directory,frozen_name,O_RDONLY);struct stat fs;
                if (frozen<0) status=open_error();
                else {
                    view->frozen_puts=(CettaDiskRoot){.fd=frozen,.root=meta.frozen_puts,.end=meta.frozen_end,
                        .key_limit=meta.limits.key_bytes,.value_limit=meta.limits.value_bytes};
                    memcpy(view->frozen_puts.segment,meta.frozen,16);
                    view->frozen_removes=view->frozen_puts;view->frozen_removes.root=meta.frozen_removes;
                    if (fstat(frozen,&fs) || fs.st_size<0 || (uint64_t)fs.st_size<meta.frozen_end) status=CETTA_STORE_CORRUPT;
                    else status=disk_status(cetta_disk_root_check(&view->frozen_puts));
                    if (status==CETTA_STORE_OK) status=disk_status(cetta_disk_root_check(&view->frozen_removes));
                }
            }
            if (status==CETTA_STORE_OK) status=view_base(store,view);
            if (status!=CETTA_STORE_OK) {
                if (view->frozen_puts.fd>=0) close(view->frozen_puts.fd);
                cetta_act_base_release(view->base);free(view);goto failed;
            }
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
static CettaDiskStoreStatus store_open_directory(int directory,bool create,bool create_act,
        const CettaDiskStoreLimits *limits,CettaDiskStore **out) {
    struct stat st;
    if (fstat(directory,&st)) { close(directory);return CETTA_STORE_IO; }
    if (st.st_uid!=geteuid() || (st.st_mode&077)) { close(directory);return CETTA_STORE_OWNERSHIP; }
    CettaDiskStore *store=calloc(1,sizeof(*store));
    if (!store) { close(directory); return CETTA_STORE_NOMEM; }
    *store=(CettaDiskStore){.directory=directory,.device=st.st_dev,.inode=st.st_ino};
    atomic_init(&store->references,1);
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
            meta=(Meta){.end=CETTA_DISK_SEGMENT_HEADER_BYTES,.limits=actual,.format=create_act ? 3u : 2u}; int segment=-1;
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
    return store_open_directory(directory,create,false,limits,out);
}

/* Create only the requested leaf. The already-existing parent is retained
 * while its directory entry is synchronized; initialization uses the opened
 * leaf descriptor, so a path replacement cannot redirect this owner. */
static CettaDiskStoreStatus open_created(const char *path,bool act,
        const CettaDiskStoreLimits *limits,CettaDiskStore **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!path || !*path) return CETTA_STORE_INVALID;
    char *copy=strdup(path);
    if (!copy) return CETTA_STORE_NOMEM;
    size_t length=strlen(copy);
    while (length>1 && copy[length-1]=='/') copy[--length]=0;
    char *slash=strrchr(copy,'/');
    const char *leaf=slash ? slash+1 : copy;
    const char *parent=".";
    if (slash) { *slash=0;parent=slash==copy ? "/" : copy; }
    if (!*leaf) { free(copy);return CETTA_STORE_INVALID; }
    int parent_fd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (parent_fd<0) { CettaDiskStoreStatus status=open_error();free(copy);return status; }
    struct stat parent_stat;
    if (fstat(parent_fd,&parent_stat) || parent_stat.st_uid!=geteuid() ||
            (parent_stat.st_mode&077)) {
        close(parent_fd);free(copy);return CETTA_STORE_OWNERSHIP;
    }
    if (mkdirat(parent_fd,leaf,0700) && errno!=EEXIST) {
        CettaDiskStoreStatus status=open_error();close(parent_fd);free(copy);return status;
    }
    int directory=openat(parent_fd,leaf,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (directory<0) {
        CettaDiskStoreStatus status=open_error();struct stat entry;
        if (!fstatat(parent_fd,leaf,&entry,AT_SYMLINK_NOFOLLOW) && S_ISLNK(entry.st_mode))
            status=CETTA_STORE_OWNERSHIP;
        close(parent_fd);free(copy);return status;
    }
    bool durable=synchronize(parent_fd);
    close(parent_fd);free(copy);
    if (!durable) { close(directory);return CETTA_STORE_IO; }
    return store_open_directory(directory,true,act,limits,out);
}

CettaDiskStoreStatus cetta_disk_store_open_created(const char *path,
        const CettaDiskStoreLimits *limits,CettaDiskStore **out) {
    return open_created(path,false,limits,out);
}
CettaDiskStoreStatus cetta_disk_store_open_created_act(const char *path,
        const CettaDiskStoreLimits *limits,CettaDiskStore **out) {
    return open_created(path,true,limits,out);
}
unsigned cetta_disk_store_view_format(const CettaDiskStoreView *view) {
    return view ? view->meta.format : 0;
}
bool cetta_disk_store_view_same_domain(const CettaDiskStoreView *left,
        const CettaDiskStoreView *right) {
    return left && right && left->device==right->device && left->inode==right->inode;
}
bool cetta_disk_store_has_domain(const CettaDiskStore *store,const CettaDiskStoreView *view) {
    return store && view && store->device==view->device && store->inode==view->inode;
}
void cetta_disk_store_close(CettaDiskStore *store) {
    if (store && atomic_fetch_sub(&store->references,1)==1) {cetta_act_base_release(store->base);close(store->directory);free(store);}
}
CettaDiskStoreStatus cetta_disk_store_clone_owner(const CettaDiskStore *origin,CettaDiskStore **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;if (!origin) return CETTA_STORE_INVALID;
    CettaDiskStore *copy=calloc(1,sizeof(*copy));if (!copy) return CETTA_STORE_NOMEM;
    copy->directory=fcntl(origin->directory,F_DUPFD_CLOEXEC,0);
    if (copy->directory<0) {free(copy);return CETTA_STORE_IO;}
    atomic_init(&copy->references,1);copy->device=origin->device;copy->inode=origin->inode;
    *out=copy;return CETTA_STORE_OK;
}
void cetta_disk_store_require_recovery(CettaDiskStore *store) {
    if (store) store->uncertain=true;
}
void cetta_disk_store_view_close(CettaDiskStoreView *view) {
    if (view) {close(view->root.fd);if (view->frozen_puts.fd>=0) close(view->frozen_puts.fd);cetta_act_base_release(view->base);free(view);}
}
CettaDiskStoreStatus cetta_disk_store_view_clone(const CettaDiskStoreView *view,
        CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!view || view->root.fd<0) return CETTA_STORE_INVALID;
    CettaDiskStoreView *copy=malloc(sizeof(*copy));
    if (!copy) return CETTA_STORE_NOMEM;
    *copy=*view;copy->root.fd=-1;copy->frozen_puts.fd=-1;
    do { copy->root.fd=fcntl(view->root.fd,F_DUPFD_CLOEXEC,0); }
    while (copy->root.fd<0 && errno==EINTR);
    if (copy->root.fd<0) { free(copy); return CETTA_STORE_IO; }
    copy->removes.fd=copy->root.fd;
    if (view->frozen_puts.fd>=0) {
        do {copy->frozen_puts.fd=fcntl(view->frozen_puts.fd,F_DUPFD_CLOEXEC,0);}
        while (copy->frozen_puts.fd<0 && errno==EINTR);
        if (copy->frozen_puts.fd<0) {close(copy->root.fd);free(copy);return CETTA_STORE_IO;}
        copy->frozen_removes.fd=copy->frozen_puts.fd;
    }
    cetta_act_base_retain(copy->base);
    *out=copy;
    return CETTA_STORE_OK;
}
void cetta_disk_store_view_storage_census(const CettaDiskStoreView *view,
        CettaStorageCensus *census) {
    if (view && cetta_storage_object(census,view,sizeof(*view),CETTA_STORAGE_BLOCK)) cetta_act_base_storage(view->base,census);
}
const CettaDiskRoot *cetta_disk_store_view_root(const CettaDiskStoreView *view) { return view && view->meta.format!=3 ? &view->root : NULL; }
uint64_t cetta_disk_store_view_revision(const CettaDiskStoreView *view) { return view ? view->meta.revision : 0; }
const unsigned char *cetta_disk_store_view_epoch(const CettaDiskStoreView *view) { return view ? view->meta.epoch : NULL; }


typedef struct {
    CettaDiskCursor *disk;
    CettaActCursor *act;
    CettaDiskBuffer key;
    CettaDiskStoreBlob value;
    unsigned layer;
    bool removal,ready,ended;
} ReadStream;
struct CettaDiskStoreCursor {
    CettaDiskStoreView *view;
    ReadStream streams[5];
    size_t count;
    CettaDiskBuffer lower;
    bool exclusive,has_lower,reverse,terminal;
    CettaDiskStatus result;
};
struct CettaDiskStoreBlobReader {
    CettaDiskBlobReader *disk;
    CettaActBase *base;
    CettaActBytes value;
    uint64_t offset;bool streaming;
};
CettaDiskStoreLimits cetta_disk_store_view_limits(const CettaDiskStoreView *view) {
    return view ? view->meta.limits : (CettaDiskStoreLimits){0};
}
static CettaDiskStatus layer_lookup(const CettaDiskRoot *puts,const CettaDiskRoot *removes,
        CettaDiskBytes key,unsigned layer,CettaDiskStoreBlob *out,bool *present) {
    *present=false;CettaDiskBlob put,removed;
    CettaDiskStatus a=cetta_disk_lookup(puts,key,&put);
    CettaDiskStatus b=removes && removes->root ? cetta_disk_lookup(removes,key,&removed) : CETTA_DISK_ABSENT;
    if (a!=CETTA_DISK_OK && a!=CETTA_DISK_ABSENT) return a;
    if (b!=CETTA_DISK_OK && b!=CETTA_DISK_ABSENT) return b;
    if (a==CETTA_DISK_OK && b==CETTA_DISK_OK) return CETTA_DISK_CORRUPT;
    if (b==CETTA_DISK_OK) {
        *present=true;size_t got=0;
        if (removed.size) return CETTA_DISK_CORRUPT;
        CettaDiskStatus s=cetta_disk_blob_read(removes,removed,0,NULL,0,&got);
        return s==CETTA_DISK_OK ? CETTA_DISK_ABSENT : s;
    }
    if (a==CETTA_DISK_OK) {*present=true;*out=(CettaDiskStoreBlob){put.reference,put.size,layer};}
    return a;
}
CettaDiskStatus cetta_disk_store_lookup(const CettaDiskStoreView *view,
        CettaDiskBytes key,CettaDiskStoreBlob *out) {
    if (!view || !out) return CETTA_DISK_INVALID;
    *out=(CettaDiskStoreBlob){0};bool found=false;
    CettaDiskStatus s=layer_lookup(&view->root,view->meta.format==3 ? &view->removes : NULL,key,0,out,&found);
    if (found || s!=CETTA_DISK_ABSENT) return s;
    if (view->meta.frozen_kind) {
        s=layer_lookup(&view->frozen_puts,view->meta.frozen_kind==2 ? &view->frozen_removes : NULL,key,1,out,&found);
        if (found || s!=CETTA_DISK_ABSENT) return s;
    }
    if (!view->base) return CETTA_DISK_ABSENT;
    uint64_t reference,size;s=act_status(cetta_act_base_lookup(view->base,
        (CettaActBytes){key.data,key.size},&reference,&size));
    if (s==CETTA_DISK_OK) *out=(CettaDiskStoreBlob){reference,size,2};
    return s;
}
static const CettaDiskRoot *blob_root(const CettaDiskStoreView *view,unsigned storage) {
    if (!view) return NULL;
    if (!storage) return &view->root;
    if (storage==1 && view->meta.frozen_kind) return &view->frozen_puts;
    return NULL;
}
CettaDiskStatus cetta_disk_store_blob_read(const CettaDiskStoreView *view,
        CettaDiskStoreBlob blob,uint64_t offset,void *data,size_t size,size_t *written) {
    if (!written) return CETTA_DISK_INVALID;
    *written=0;
    if (!view || offset>blob.size || (size && !data)) return CETTA_DISK_INVALID;
    if (blob.storage==2 && view->base) {
        CettaActBytes value;CettaDiskStatus s=act_status(cetta_act_base_value(view->base,blob.reference,&value));
        if (s!=CETTA_DISK_OK) return s;
        if (value.size!=blob.size) return CETTA_DISK_CORRUPT;
        size_t n=blob.size-offset<size ? (size_t)(blob.size-offset) : size;
        if (n) memcpy(data,value.data+(size_t)offset,n);
        *written=n;return CETTA_DISK_OK;
    }
    const CettaDiskRoot *root=blob_root(view,blob.storage);
    return root ? cetta_disk_blob_read(root,(CettaDiskBlob){blob.reference,blob.size},offset,data,size,written) : CETTA_DISK_INVALID;
}
CettaDiskStatus cetta_disk_store_blob_reader_open(const CettaDiskStoreView *view,
        CettaDiskStoreBlob blob,CettaDiskStoreBlobReader **out) {
    if (!out) return CETTA_DISK_INVALID;
    *out=NULL;if (!view) return CETTA_DISK_INVALID;
    CettaDiskStoreBlobReader *r=calloc(1,sizeof(*r));if (!r) return CETTA_DISK_NOMEM;
    CettaDiskStatus s;
    if (blob.storage==2 && view->base) {
        s=act_status(cetta_act_base_value(view->base,blob.reference,&r->value));
        if (s==CETTA_DISK_OK && r->value.size!=blob.size) s=CETTA_DISK_CORRUPT;
        if (s==CETTA_DISK_OK) {r->base=view->base;cetta_act_base_retain(r->base);}
    } else {
        const CettaDiskRoot *root=blob_root(view,blob.storage);
        s=root ? cetta_disk_blob_reader_open(root,(CettaDiskBlob){blob.reference,blob.size},&r->disk) : CETTA_DISK_INVALID;
    }
    if (s!=CETTA_DISK_OK) free(r);else *out=r;
    return s;
}
CettaDiskStatus cetta_disk_store_blob_reader_read(CettaDiskStoreBlobReader *r,
        uint64_t offset,void *data,size_t size,size_t *written) {
    if (!r || !written) return CETTA_DISK_INVALID;
    *written=0;
    if (!r->base) return cetta_disk_blob_reader_read(r->disk,offset,data,size,written);
    if (offset>r->value.size || (size && !data)) return CETTA_DISK_INVALID;
    size_t n=r->value.size-offset<size ? r->value.size-(size_t)offset : size;
    if (n) memcpy(data,r->value.data+(size_t)offset,n);
    *written=n;return CETTA_DISK_OK;
}
uint64_t cetta_disk_store_blob_reader_size(const CettaDiskStoreBlobReader *r) {
    return !r ? 0 : r->base ? r->value.size : cetta_disk_blob_reader_size(r->disk);
}
void cetta_disk_store_blob_reader_close(CettaDiskStoreBlobReader *r) {
    if (r) {cetta_act_base_release(r->base);cetta_disk_blob_reader_close(r->disk);free(r);}
}

CettaDiskStatus cetta_disk_store_blob_stream_open(const CettaDiskStoreView *view,
        CettaDiskStoreBlob blob,CettaDiskStoreBlobReader **out) {
    if (!out) return CETTA_DISK_INVALID;
    *out=NULL;if (!view) return CETTA_DISK_INVALID;
    CettaDiskStoreBlobReader *r=calloc(1,sizeof(*r));if (!r) return CETTA_DISK_NOMEM;
    CettaDiskStatus s;
    if (blob.storage==2 && view->base) {
        s=act_status(cetta_act_base_value(view->base,blob.reference,&r->value));
        if (s==CETTA_DISK_OK && r->value.size!=blob.size) s=CETTA_DISK_CORRUPT;
        if (s==CETTA_DISK_OK) {r->base=view->base;cetta_act_base_retain(r->base);}
    } else {
        const CettaDiskRoot *root=blob_root(view,blob.storage);
        s=root ? cetta_disk_blob_stream_open(root,(CettaDiskBlob){blob.reference,blob.size},&r->disk) : CETTA_DISK_INVALID;
    }
    if (s!=CETTA_DISK_OK) free(r);else {r->streaming=true;*out=r;}
    return s;
}
CettaDiskStatus cetta_disk_store_blob_stream_next(CettaDiskStoreBlobReader *r,
        void *bytes,size_t capacity,size_t *written) {
    if (!written) return CETTA_DISK_INVALID;
    *written=0;
    if (!r || !r->streaming || (capacity && !bytes)) return CETTA_DISK_INVALID;
    CettaDiskStatus s;
    if (r->base) {
        if (r->offset==r->value.size) return CETTA_DISK_END;
        s=cetta_disk_store_blob_reader_read(r,r->offset,bytes,capacity,written);
    } else s=cetta_disk_blob_stream_next(r->disk,bytes,capacity,written);
    if (s==CETTA_DISK_OK) r->offset+=*written;
    return s;
}
static CettaDiskStatus add_disk_stream(CettaDiskStoreCursor *c,const CettaDiskRoot *root,
        const CettaDiskBytes *lo,bool exclusive,const CettaDiskBytes *hi,unsigned layer,bool removal) {
    if (c->count>=5) return CETTA_DISK_CORRUPT;
    ReadStream *stream=&c->streams[c->count++];stream->layer=layer;stream->removal=removal;
    return c->reverse ? cetta_disk_cursor_open_reverse_range(root,lo,exclusive,hi,&stream->disk)
                      : cetta_disk_cursor_open_range(root,lo,exclusive,hi,&stream->disk);
}
static CettaDiskStatus store_cursor_open(const CettaDiskStoreView *view,
        const CettaDiskBytes *lower,bool exclusive,const CettaDiskBytes *upper,
        bool reverse,CettaDiskStoreCursor **out) {
    if (!out) return CETTA_DISK_INVALID;
    *out=NULL;if (!view || (lower && (lower->size>view->meta.limits.key_bytes || (lower->size && !lower->data))) ||
        (upper && (upper->size>view->meta.limits.key_bytes || (upper->size && !upper->data)))) return CETTA_DISK_INVALID;
    CettaDiskStoreCursor *c=calloc(1,sizeof(*c));if (!c) return CETTA_DISK_NOMEM;
    c->reverse=reverse;c->exclusive=exclusive;c->has_lower=lower!=NULL;
    CettaDiskStoreStatus v=cetta_disk_store_view_clone(view,&c->view);
    CettaDiskStatus s=v==CETTA_STORE_OK ? CETTA_DISK_OK : v==CETTA_STORE_NOMEM ? CETTA_DISK_NOMEM : CETTA_DISK_IO;
    if (s==CETTA_DISK_OK && lower && lower->size) {
        c->lower.data=malloc(lower->size);
        if (!c->lower.data) s=CETTA_DISK_NOMEM;
        else {memcpy(c->lower.data,lower->data,lower->size);c->lower.size=lower->size;}
    }
    if (s==CETTA_DISK_OK) s=add_disk_stream(c,&view->root,lower,exclusive,upper,0,false);
    if (s==CETTA_DISK_OK && view->meta.format==3) s=add_disk_stream(c,&view->removes,lower,exclusive,upper,0,true);
    if (s==CETTA_DISK_OK && view->meta.frozen_kind) s=add_disk_stream(c,&view->frozen_puts,lower,exclusive,upper,1,false);
    if (s==CETTA_DISK_OK && view->meta.frozen_kind==2) s=add_disk_stream(c,&view->frozen_removes,lower,exclusive,upper,1,true);
    if (s==CETTA_DISK_OK && view->base) {
        ReadStream *stream=&c->streams[c->count++];stream->layer=2;
        CettaActBytes lo={lower ? lower->data : NULL,lower ? lower->size : 0};
        CettaActBytes hi={upper ? upper->data : NULL,upper ? upper->size : 0};
        s=act_status(cetta_act_base_cursor(view->base,lower ? &lo : NULL,upper ? &hi : NULL,reverse,&stream->act));
    }
    if (s!=CETTA_DISK_OK) cetta_disk_store_cursor_close(c);else *out=c;
    return s;
}
CettaDiskStatus cetta_disk_store_cursor_open_range(const CettaDiskStoreView *view,
        const CettaDiskBytes *lo,bool exclusive,const CettaDiskBytes *hi,CettaDiskStoreCursor **out) {
    return store_cursor_open(view,lo,exclusive,hi,false,out);
}
CettaDiskStatus cetta_disk_store_cursor_open_reverse_range(const CettaDiskStoreView *view,
        const CettaDiskBytes *lo,bool exclusive,const CettaDiskBytes *hi,CettaDiskStoreCursor **out) {
    return store_cursor_open(view,lo,exclusive,hi,true,out);
}
static int read_key_order(CettaDiskBytes a,CettaDiskBytes b) {
    size_t n=a.size<b.size ? a.size : b.size;int order=n ? memcmp(a.data,b.data,n) : 0;
    return order ? order : (a.size>b.size)-(a.size<b.size);
}
static CettaDiskStatus stream_pull(CettaDiskStoreCursor *c,ReadStream *stream) {
    if (stream->ready || stream->ended) return CETTA_DISK_OK;
    CettaDiskStatus s;
    if (stream->act) {
        CettaActBytes key;uint64_t reference;
        do {s=act_status(cetta_act_cursor_next(stream->act,&key,&reference));}
        while (s==CETTA_DISK_OK && c->has_lower && c->exclusive &&
            !read_key_order((CettaDiskBytes){key.data,key.size},(CettaDiskBytes){c->lower.data,c->lower.size}));
        if (s==CETTA_DISK_OK) {
            CettaActBytes value;s=act_status(cetta_act_base_value(c->view->base,reference,&value));
            if (s!=CETTA_DISK_OK) return s;
            stream->key.data=malloc(key.size ? key.size : 1);
            if (!stream->key.data) return CETTA_DISK_NOMEM;
            if (key.size) memcpy(stream->key.data,key.data,key.size);
            stream->key.size=key.size;stream->value=(CettaDiskStoreBlob){reference,value.size,2};
        }
    } else {
        CettaDiskBlob value;s=cetta_disk_cursor_next(stream->disk,&stream->key,&value);
        if (s==CETTA_DISK_OK) {
            stream->value=(CettaDiskStoreBlob){value.reference,value.size,stream->layer};
            if (stream->removal) {
                const CettaDiskRoot *root=stream->layer ? &c->view->frozen_removes : &c->view->removes;
                size_t got=0;
                if (value.size) return CETTA_DISK_CORRUPT;
                s=cetta_disk_blob_read(root,value,0,NULL,0,&got);
            }
        }
    }
    if (s==CETTA_DISK_END) {stream->ended=true;return CETTA_DISK_OK;}
    if (s==CETTA_DISK_OK) stream->ready=true;
    return s;
}
CettaDiskStatus cetta_disk_store_cursor_next(CettaDiskStoreCursor *c,
        CettaDiskBuffer *key,CettaDiskStoreBlob *out) {
    if (!c || !key || !out) return CETTA_DISK_INVALID;
    *key=(CettaDiskBuffer){0};*out=(CettaDiskStoreBlob){0};
    if (c->terminal) return c->result;
    for (;;) {
        size_t selected=c->count;
        for (size_t i=0;i<c->count;i++) {
            CettaDiskStatus s=stream_pull(c,&c->streams[i]);
            if (s!=CETTA_DISK_OK) {c->terminal=true;c->result=s;return s;}
            if (!c->streams[i].ready) continue;
            CettaDiskBuffer a=c->streams[i].key;
            if (selected==c->count) selected=i;
            else {
                CettaDiskBuffer b=c->streams[selected].key;
                int order=read_key_order((CettaDiskBytes){a.data,a.size},(CettaDiskBytes){b.data,b.size});
                if (c->reverse ? order>0 : order<0) selected=i;
            }
        }
        if (selected==c->count) {c->terminal=true;c->result=CETTA_DISK_END;return CETTA_DISK_END;}
        ReadStream *chosen=&c->streams[selected];CettaDiskBuffer result=chosen->key;
        CettaDiskStoreBlob value=chosen->value;bool removal=chosen->removal;
        bool same[5]={0};
        for (size_t i=0;i<c->count;i++) if (c->streams[i].ready &&
                !read_key_order((CettaDiskBytes){result.data,result.size},
                    (CettaDiskBytes){c->streams[i].key.data,c->streams[i].key.size})) {
            same[i]=true;
            if (i!=selected && chosen->layer==c->streams[i].layer) {
                c->terminal=true;c->result=CETTA_DISK_CORRUPT;return c->result;
            }
        }
        chosen->key=(CettaDiskBuffer){0};
        for (size_t i=0;i<c->count;i++) if (same[i]) {
            cetta_disk_buffer_free(&c->streams[i].key);c->streams[i].ready=false;
        }
        if (removal) {cetta_disk_buffer_free(&result);continue;}
        *key=result;*out=value;return CETTA_DISK_OK;
    }
}
const CettaDiskStoreView *cetta_disk_store_cursor_view(const CettaDiskStoreCursor *c) {
    return c ? c->view : NULL;
}
void cetta_disk_store_cursor_storage_census(const CettaDiskStoreCursor *c,CettaStorageCensus *census) {
    if (!c || !cetta_storage_object(census,c,sizeof(*c),CETTA_STORAGE_BLOCK)) return;
    cetta_disk_store_view_storage_census(c->view,census);
    cetta_storage_array(census,c->lower.data,c->lower.size,1,0);
    for (size_t i=0;i<c->count;i++) {
        cetta_disk_cursor_storage_census(c->streams[i].disk,census);
        cetta_act_cursor_storage(c->streams[i].act,census);
        cetta_storage_array(census,c->streams[i].key.data,c->streams[i].key.data ? (c->streams[i].key.size ? c->streams[i].key.size : 1) : 0,1,0);
    }
}
void cetta_disk_store_cursor_close(CettaDiskStoreCursor *c) {
    if (!c) return;
    for (size_t i=0;i<c->count;i++) {
        cetta_disk_cursor_close(c->streams[i].disk);cetta_act_cursor_close(c->streams[i].act);
        cetta_disk_buffer_free(&c->streams[i].key);
    }
    cetta_disk_buffer_free(&c->lower);cetta_disk_store_view_close(c->view);free(c);
}
static bool equal_meta(const Meta *left,const Meta *right) {
    unsigned char a[META3_BYTES],b[META3_BYTES];size_t n=encode_meta(left,a),m=encode_meta(right,b);
    return n==m && !memcmp(a,b,n);
}
/* A different process can die after rename and before directory sync. A
 * first observation of a root confirms it before trusting an application
 * receipt. Previously confirmed immutable roots need no repeated barrier. */
static bool sync_named(CettaDiskStore *store,const char *name) {
    int fd=file_open(store->directory,name,O_RDONLY);
    if (fd<0) return false;
    bool ok=synchronize(fd);close(fd);return ok;
}
static CettaDiskStoreStatus confirm(CettaDiskStore *store,const Meta *meta) {
    if (store->confirmed && equal_meta(&store->durable,meta)) return CETTA_STORE_OK;
    char name[45];segment_name(meta->segment,name);
    if (!sync_named(store,name)) return CETTA_STORE_IO;
    if (meta->frozen_kind) {segment_name(meta->frozen,name);if (!sync_named(store,name)) return CETTA_STORE_IO;}
    if (nonzero(meta->base,16)) {base_name(meta->base,name);if (!sync_named(store,name)) return CETTA_STORE_IO;}
    if (!sync_named(store,"root.csl") || !synchronize(store->directory)) return CETTA_STORE_IO;
    store->durable=*meta;store->confirmed=true;return CETTA_STORE_OK;
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
    close(view->root.fd); view->root.fd=reader;view->removes.fd=reader;return CETTA_STORE_OK;
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
static CettaDiskStoreStatus bodies_equal(const CettaDiskStoreView *left,CettaDiskStoreBlob a,
        const CettaDiskStoreView *right,CettaDiskStoreBlob b,bool same_file,bool *equal) {
    *equal=false;
    if (a.size!=b.size) return CETTA_STORE_OK;
    if (same_file && left->meta.format!=3 && right->meta.format!=3 && a.reference==b.reference) { *equal=true; return CETTA_STORE_OK; }
    CettaDiskStoreBlobReader *ar=NULL,*br=NULL;
    CettaDiskStatus status=cetta_disk_store_blob_reader_open(left,a,&ar);
    if (status==CETTA_DISK_OK) status=cetta_disk_store_blob_reader_open(right,b,&br);
    unsigned char ab[32768],bb[32768];
    for (uint64_t offset=0;status==CETTA_DISK_OK && offset<a.size;) {
        size_t an=0,bn=0;
        status=cetta_disk_store_blob_reader_read(ar,offset,ab,sizeof(ab),&an);
        if (status==CETTA_DISK_OK) status=cetta_disk_store_blob_reader_read(br,offset,bb,sizeof(bb),&bn);
        if (status!=CETTA_DISK_OK) break;
        if (an!=bn || !an) { status=CETTA_DISK_CORRUPT; break; }
        if (memcmp(ab,bb,an)) goto done;
        offset+=an;
    }
    *equal=status==CETTA_DISK_OK;
done:
    cetta_disk_store_blob_reader_close(ar); cetta_disk_store_blob_reader_close(br);
    return disk_status(status);
}
static CettaDiskStoreStatus scope_equal(const CettaDiskStoreView *before,
        const CettaDiskStoreView *current,const CettaDiskStoreScope *scope,bool same_file,bool *equal) {
    *equal=false;
    if (scope->kind==CETTA_STORE_POINT) {
        CettaDiskStoreBlob old,now;
        CettaDiskStatus a=cetta_disk_store_lookup(before,scope->lower,&old);
        CettaDiskStatus b=cetta_disk_store_lookup(current,scope->lower,&now);
        if (a==CETTA_DISK_ABSENT && b==CETTA_DISK_ABSENT) { *equal=true; return CETTA_STORE_OK; }
        if (a!=CETTA_DISK_OK && a!=CETTA_DISK_ABSENT) return disk_status(a);
        if (b!=CETTA_DISK_OK && b!=CETTA_DISK_ABSENT) return disk_status(b);
        if (a!=b) return CETTA_STORE_OK;
        return bodies_equal(before,old,current,now,same_file,equal);
    }
    const CettaDiskBytes *lower=scope->has_lower ? &scope->lower : NULL;
    const CettaDiskBytes *upper=scope->has_upper ? &scope->upper : NULL;
    CettaDiskStoreCursor *old=NULL,*now=NULL; CettaDiskStoreStatus status=CETTA_STORE_OK;
    CettaDiskStatus a=cetta_disk_store_cursor_open_range(before,lower,false,upper,&old);
    if (a==CETTA_DISK_OK) a=cetta_disk_store_cursor_open_range(current,lower,false,upper,&now);
    if (a!=CETTA_DISK_OK) { status=disk_status(a); goto done; }
    for (;;) {
        CettaDiskBuffer ak={0},bk={0}; CettaDiskStoreBlob av,bv;
        a=cetta_disk_store_cursor_next(old,&ak,&av);
        CettaDiskStatus b=cetta_disk_store_cursor_next(now,&bk,&bv);
        bool end=a==CETTA_DISK_END && b==CETTA_DISK_END,match=false;
        if (a!=CETTA_DISK_OK && a!=CETTA_DISK_END) status=disk_status(a);
        else if (b!=CETTA_DISK_OK && b!=CETTA_DISK_END) status=disk_status(b);
        else if (end) match=true;
        else if (a==CETTA_DISK_OK && b==CETTA_DISK_OK &&
            !key_order((CettaDiskBytes){ak.data,ak.size},(CettaDiskBytes){bk.data,bk.size}))
            status=bodies_equal(before,av,current,bv,same_file,&match);
        cetta_disk_buffer_free(&ak); cetta_disk_buffer_free(&bk);
        if (end || !match || status!=CETTA_STORE_OK) { *equal=end && status==CETTA_STORE_OK; break; }
    }
done:
    cetta_disk_store_cursor_close(old); cetta_disk_store_cursor_close(now); return status;
}
void cetta_disk_store_batch_close(CettaDiskStoreBatch *batch) {
    if (!batch) return;
    if (batch->puts.fd>=0) close(batch->puts.fd);
    free(batch);
}
void cetta_disk_store_batch_storage_census(const void *raw,CettaStorageCensus *census) {
    if (raw) cetta_storage_object(census,raw,sizeof(CettaDiskStoreBatch),CETTA_STORAGE_BLOCK);
}
bool cetta_disk_store_batch_info(const CettaDiskStoreBatch *batch,uint64_t *bytes,uint64_t *maximum,uint64_t *staged) {
    if (!batch || batch->failed || !bytes || !maximum || !staged) return false;
    *bytes=batch->puts.end;*maximum=batch->maximum;*staged=batch->staged;return true;
}
CettaDiskStoreStatus cetta_disk_store_batch_open(CettaDiskStore *store,
        const CettaDiskStoreView *view,uint64_t maximum,CettaDiskStoreBatch **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;
    if (!store || !view || maximum<CETTA_DISK_SEGMENT_HEADER_BYTES || maximum>INT64_MAX) return CETTA_STORE_INVALID;
    CettaDiskStoreStatus status=cetta_disk_store_check_view(store,view);
    if (status!=CETTA_STORE_OK) return status;
    unsigned char generation[16];if (!random_bytes(generation)) return CETTA_STORE_IO;
    static const char digits[]="0123456789abcdef";char name[40];memcpy(name,".batch-",7);
    for (size_t i=0;i<16;i++) { name[7+i*2]=digits[generation[i]>>4];name[8+i*2]=digits[generation[i]&15]; }
    name[39]=0;
    int fd=openat(store->directory,name,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) return open_error();
    if (unlinkat(store->directory,name,0) && errno!=ENOENT) { close(fd);return CETTA_STORE_IO; }
    status=disk_status(cetta_disk_segment_create(fd,generation));
    if (status!=CETTA_STORE_OK) { close(fd);return status; }
    CettaDiskStoreBatch *batch=calloc(1,sizeof(*batch));
    if (!batch) { close(fd);return CETTA_STORE_NOMEM; }
    batch->puts=(CettaDiskRoot){.fd=fd,.end=CETTA_DISK_SEGMENT_HEADER_BYTES,
        .key_limit=view->root.key_limit,.value_limit=view->root.value_limit};
    memcpy(batch->puts.segment,generation,16);batch->removes=batch->puts;
    batch->device=view->device;batch->inode=view->inode;memcpy(batch->epoch,view->meta.epoch,16);
    batch->maximum=maximum;*out=batch;return CETTA_STORE_OK;
}
CettaDiskStoreStatus cetta_disk_store_batch_add(CettaDiskStoreBatch *batch,const CettaDiskOp *operations,size_t count) {
    if (!batch || batch->failed || (count && !operations)) return CETTA_STORE_INVALID;
    if (count>UINT64_MAX-batch->staged) return CETTA_STORE_LIMIT;
    CettaDiskRoot puts=batch->puts,removes=batch->removes;
    CettaDiskStatus status=CETTA_DISK_OK;
    for (size_t i=0;i<count;i++) {
        CettaDiskOp put=operations[i],remove={.remove=!operations[i].remove,.key=operations[i].key};
        CettaDiskRoot next;
        status=cetta_disk_build(&puts,&put,1,&next);
        if (status!=CETTA_DISK_OK) break;
        puts=next;if (puts.end>batch->maximum) { status=CETTA_DISK_LIMIT;break; }
        removes.end=puts.end;
        status=cetta_disk_build(&removes,&remove,1,&next);
        if (status!=CETTA_DISK_OK) break;
        removes=next;puts.end=removes.end;
        if (puts.end>batch->maximum) { status=CETTA_DISK_LIMIT;break; }
    }
    if (status!=CETTA_DISK_OK) {
        if (ftruncate(batch->puts.fd,(off_t)batch->puts.end)) { batch->failed=true;return CETTA_STORE_IO; }
        return disk_status(status);
    }
    batch->puts=puts;batch->removes=removes;batch->staged+=count;return CETTA_STORE_OK;
}
static CettaDiskStatus delta_change(CettaDiskRoot *puts,CettaDiskRoot *removes,
        const CettaDiskOp *operation) {
    CettaDiskOp put=*operation,opposite={.remove=true,.key=operation->key};
    CettaDiskRoot *first=operation->remove ? removes : puts,*second=operation->remove ? puts : removes;
    if (operation->remove) {put.remove=false;put.value=(CettaDiskBytes){0};}
    CettaDiskRoot next;CettaDiskStatus s=cetta_disk_build(first,&put,1,&next);
    if (s!=CETTA_DISK_OK) return s;
    *first=next;second->end=next.end;
    s=cetta_disk_build(second,&opposite,1,&next);
    if (s!=CETTA_DISK_OK) return s;
    *second=next;first->end=next.end;return CETTA_DISK_OK;
}
static CettaDiskStoreStatus batch_build(const CettaDiskStoreBatch *batch,
        const CettaDiskRoot *base,const CettaDiskRoot *removed,CettaDiskRoot *candidate,CettaDiskRoot *candidate_removes) {
    CettaDiskRoot result=*base,removes=removed ? *removed : (CettaDiskRoot){0};
    for (size_t stream=0;stream<2;stream++) {
        const CettaDiskRoot *source=stream ? &batch->puts : &batch->removes;
        CettaDiskCursor *cursor=NULL;CettaDiskStatus status=cetta_disk_cursor_open(source,&cursor);
        if (status!=CETTA_DISK_OK) return disk_status(status);
        for (;;) {
            CettaDiskBuffer key={0};CettaDiskBlob blob;
            status=cetta_disk_cursor_next(cursor,&key,&blob);
            if (status==CETTA_DISK_END) break;
            if (status!=CETTA_DISK_OK) { cetta_disk_buffer_free(&key);break; }
            unsigned char *body=NULL;size_t got=0;
            if (blob.size>SIZE_MAX || (!stream && blob.size)) status=CETTA_DISK_CORRUPT;
            else {
                body=malloc(blob.size ? (size_t)blob.size : 1);
                if (!body) status=CETTA_DISK_NOMEM;
                else status=cetta_disk_blob_read(source,blob,0,body,(size_t)blob.size,&got);
                if (status==CETTA_DISK_OK && got!=blob.size) status=CETTA_DISK_CORRUPT;
            }
            if (status==CETTA_DISK_OK) {
                CettaDiskOp operation={.remove=!stream,.key={key.data,key.size},.value={body,got}};
                CettaDiskRoot next;
                status=removed ? delta_change(&result,&removes,&operation) : cetta_disk_build(&result,&operation,1,&next);
                if (status==CETTA_DISK_OK && !removed) result=next;
            }
            free(body);cetta_disk_buffer_free(&key);
            if (status!=CETTA_DISK_OK) break;
        }
        cetta_disk_cursor_close(cursor);
        if (status!=CETTA_DISK_END) return disk_status(status);
    }
    *candidate=result;if (candidate_removes) *candidate_removes=removes;return CETTA_STORE_OK;
}
static CettaDiskStoreStatus commit(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskOp *operations,size_t count,
    const CettaDiskStoreScope *reads,size_t read_count,const CettaDiskStoreScope *writes,size_t write_count,
    bool scoped,const CettaDiskStoreBatch *batch,CettaDiskStoreView **committed) {
    if (!committed) return CETTA_STORE_INVALID;
    *committed=NULL;
    if (!store || !expected || (count && !operations) || (read_count && !reads) ||
        (write_count && !writes)) return CETTA_STORE_INVALID;
    if (batch && (scoped || batch->failed || count || batch->device!=expected->device ||
        batch->inode!=expected->inode || memcmp(batch->epoch,expected->meta.epoch,16))) return CETTA_STORE_INVALID;
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
    CettaDiskRoot candidate=current->root,candidate_removes=current->removes;
    bool layered=current->meta.format==3;
    if (batch) status=batch_build(batch,&current->root,layered ? &current->removes : NULL,&candidate,&candidate_removes);
    else if (layered) {
        for (size_t i=0;i<count && status==CETTA_STORE_OK;i++)
            status=disk_status(delta_change(&candidate,&candidate_removes,&operations[i]));
    } else status=disk_status(cetta_disk_build(&current->root,operations,count,&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    status=disk_status(cetta_disk_sync(&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    Meta next=current->meta; ++next.revision; next.root=candidate.root; next.end=candidate.end;
    if (layered) next.removes=candidate_removes.root;
    status=publish(store,&manifest,&next);
    if (status==CETTA_STORE_OK) {
        current->root=candidate;if (layered) current->removes=candidate_removes;current->meta=next;
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
    return commit(store,expected,operations,count,NULL,0,NULL,0,false,NULL,committed);
}
CettaDiskStoreStatus cetta_disk_store_commit_batch(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskStoreBatch *batch,CettaDiskStoreView **committed) {
    if (!batch) { if (committed) *committed=NULL;return CETTA_STORE_INVALID; }
    return commit(store,expected,NULL,0,NULL,0,NULL,0,false,batch,committed);
}
CettaDiskStoreStatus cetta_disk_store_commit_scoped(CettaDiskStore *store,
        const CettaDiskStoreView *expected,const CettaDiskStoreScope *reads,size_t read_count,
        const CettaDiskStoreScope *writes,size_t write_count,const CettaDiskOp *operations,size_t count,
        CettaDiskStoreView **committed) {
    if (read_count>SIZE_MAX-write_count) {
        if (committed) *committed=NULL;
        return CETTA_STORE_INVALID;
    }
    return commit(store,expected,operations,count,reads,read_count,writes,write_count,true,NULL,committed);
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
    if (status==CETTA_STORE_OK && source->meta.format==3) {
        cetta_disk_store_view_close(source);
        if (manifest>=0) close(manifest);
        if (lock>=0) close(lock);
        return cetta_disk_store_compact_act(store,out);
    }
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
    char retained[45],frozen[45]={0},base[45]={0};segment_name(current->meta.segment,retained);
    if (current->meta.frozen_kind) segment_name(current->meta.frozen,frozen);
    if (nonzero(current->meta.base,16)) base_name(current->meta.base,base);
    int directory=openat(store->directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    DIR *scan=directory<0 ? NULL : fdopendir(directory);
    if (!scan) { if (directory>=0) close(directory); status=CETTA_STORE_IO; goto done; }
    errno=0;
    for (;;) {
        struct dirent *entry=readdir(scan);
        if (!entry) { if (errno) status=CETTA_STORE_IO; break; }
        const char *name=entry->d_name;
        /* A process may die between creating a volatile batch and unlinking
         * its name. These names are never published or reopened: removing an
         * active name is safe too, since its creator already owns the fd. */
        if (strlen(name)==39 && !strncmp(name,".batch-",7) &&
            strspn(name+7,"0123456789abcdef")==32) {
            int unlinked=unlinkat(store->directory,name,0);
            if (unlinked && errno!=ENOENT) { status=CETTA_STORE_IO;break; }
            if (!unlinked) ++*removed;
            errno=0;continue;
        }
        size_t length=strlen(name),prefix=0;
        if (length==44 && !strncmp(name,"segment-",8) && !strcmp(name+40,".csi")) prefix=8;
        else if (length==43 && !strncmp(name,"base-",5) && !strcmp(name+37,".cbase")) prefix=5;
        if (!prefix || !strcmp(name,retained) || !strcmp(name,frozen) || !strcmp(name,base) ||
            strspn(name+prefix,"0123456789abcdef")!=32) continue;
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

static CettaDiskStoreStatus empty_directory(int fd,bool lock_created) {
    int scan_fd=openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    DIR *scan=scan_fd<0 ? NULL : fdopendir(scan_fd);
    if (!scan) { if (scan_fd>=0) close(scan_fd);return CETTA_STORE_IO; }
    CettaDiskStoreStatus status=CETTA_STORE_OK;errno=0;
    for (;;) {
        struct dirent *entry=readdir(scan);
        if (!entry) { if (errno) status=CETTA_STORE_IO;break; }
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") ||
            (lock_created && (!strcmp(entry->d_name,"writer.lock") || !strcmp(entry->d_name,"root.csl")))) continue;
        status=CETTA_STORE_CONFLICT;break;
    }
    closedir(scan);return status;
}

static CettaDiskStatus copy_read(void *reader,uint64_t offset,void *bytes,size_t capacity,size_t *written) {
    CettaDiskStoreBlobReader *r=reader;
    if (offset!=r->offset) return CETTA_DISK_INVALID;
    CettaDiskStatus s=cetta_disk_store_blob_stream_next(r,bytes,capacity,written);
    return s==CETTA_DISK_END ? CETTA_DISK_OK : s;
}
static CettaDiskStoreStatus copy_logical(const CettaDiskStoreView *view,int destination,
        const unsigned char generation[16],CettaDiskRoot *out) {
    CettaDiskRoot root={.fd=destination,.end=CETTA_DISK_SEGMENT_HEADER_BYTES,
        .key_limit=view->meta.limits.key_bytes,.value_limit=view->meta.limits.value_bytes};
    memcpy(root.segment,generation,16);
    CettaDiskSortedBuilder *builder=NULL;CettaDiskStoreCursor *cursor=NULL;
    CettaDiskStatus s=cetta_disk_sorted_open(&root,&builder);
    if (s==CETTA_DISK_OK) s=cetta_disk_store_cursor_open_range(view,NULL,false,NULL,&cursor);
    while (s==CETTA_DISK_OK) {
        CettaDiskBuffer key={0};CettaDiskStoreBlob value;
        s=cetta_disk_store_cursor_next(cursor,&key,&value);
        if (s==CETTA_DISK_END) break;
        if (s!=CETTA_DISK_OK) {cetta_disk_buffer_free(&key);break;}
        CettaDiskStoreBlobReader *reader=NULL;
        s=cetta_disk_store_blob_stream_open(view,value,&reader);
        if (s==CETTA_DISK_OK) s=cetta_disk_sorted_put(builder,(CettaDiskBytes){key.data,key.size},value.size,copy_read,reader);
        cetta_disk_store_blob_reader_close(reader);cetta_disk_buffer_free(&key);
    }
    if (s==CETTA_DISK_END) s=cetta_disk_sorted_finish(builder,out);
    cetta_disk_store_cursor_close(cursor);cetta_disk_sorted_close(builder);
    return disk_status(s);
}
CettaDiskStoreStatus cetta_disk_store_backup(const CettaDiskStoreView *view,const char *path) {
    if (!view || !path || !*path) return CETTA_STORE_INVALID;
    CettaDiskStoreStatus status=disk_status(cetta_disk_root_check(&view->root));
    if (status!=CETTA_STORE_OK) return status;
    int directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (directory<0) {
        int code=errno;struct stat entry;
        if (code==ENOTDIR && !lstat(path,&entry) && S_ISLNK(entry.st_mode)) return CETTA_STORE_OWNERSHIP;
        errno=code;return code==ENOENT ? CETTA_STORE_ABSENT : open_error();
    }
    int lock=-1,manifest=-1,segment=-1;struct stat st;
    if (fstat(directory,&st)) { status=CETTA_STORE_IO;goto done; }
    if (st.st_uid!=geteuid() || (st.st_mode&077)) { status=CETTA_STORE_OWNERSHIP;goto done; }
    if ((status=empty_directory(directory,false))!=CETTA_STORE_OK) goto done;
    /* Install the invalid manifest as the first destination change, before
     * even creating writer.lock. A process death in bootstrap must not leave
     * a lock-only directory that create-mode can initialize as empty. */
    manifest=file_open(directory,"root.csl",O_RDWR|O_CREAT|O_EXCL);
    if (manifest<0) { status=errno==EEXIST ? CETTA_STORE_CONFLICT : open_error();goto done; }
    if (!synchronize(manifest) || !synchronize(directory)) { status=CETTA_STORE_IO;goto done; }
    lock=file_open(directory,"writer.lock",O_RDWR|O_CREAT|O_EXCL);
    if (lock<0) { status=errno==EEXIST ? CETTA_STORE_CONFLICT : open_error();goto done; }
    if (flock(lock,LOCK_EX|LOCK_NB)) { status=CETTA_STORE_BUSY;goto done; }
    if ((status=empty_directory(directory,true))!=CETTA_STORE_OK) goto done;
    CettaDiskStore target={.directory=directory,.device=st.st_dev,.inode=st.st_ino};
    Meta meta={.format=2,.revision=view->meta.revision,.limits=view->meta.limits};
    memcpy(meta.epoch,view->meta.epoch,16);
    status=segment_create(&target,meta.segment,&segment);
    if (status!=CETTA_STORE_OK) goto done;
    CettaDiskRoot candidate;
    status=view->meta.format==3 ? copy_logical(view,segment,meta.segment,&candidate)
        : disk_status(cetta_disk_compact(&view->root,segment,&candidate));
    if (status==CETTA_STORE_OK) status=disk_status(cetta_disk_sync(&candidate));
    if (status!=CETTA_STORE_OK) goto done;
    meta.root=candidate.root;meta.end=candidate.end;
    status=publish(&target,&manifest,&meta);
done:
    if (segment>=0) close(segment);
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    close(directory);return status;
}


struct CettaDiskCompaction {
    CettaDiskStore *owner;
    CettaDiskStoreView *source,*result;
    CettaDiskStoreCursor *cursor;
    CettaDiskStoreBlobReader *reader;
    CettaActBaseBuilder *builder;
    CettaActBaseValidation *validation;
    CettaActBase *base;
    CettaActBaseInfo base_info;
    int maintenance,output,scratch[4];
    unsigned char identity[16];
    CettaDiskBuffer key;
    CettaDiskStoreBlob value;
    uint64_t offset;
    bool pending,publication_attempted;
    CettaDiskStoreStatus failure;
    CettaDiskCompactionInfo info;
};
static CettaDiskStoreStatus private_scratch(CettaDiskStore *store,int *out) {
    *out=-1;
    for (;;) {
        unsigned char id[16];if (!random_bytes(id)) return CETTA_STORE_IO;
        char name[40]=".batch-";
        static const char hex[]="0123456789abcdef";
        for (size_t i=0;i<16;i++) {name[7+i*2]=hex[id[i]>>4];name[8+i*2]=hex[id[i]&15];}
        name[39]=0;
        int fd=file_open(store->directory,name,O_RDWR|O_CREAT|O_EXCL);
        if (fd<0) {if (errno==EEXIST) continue;return open_error();}
        if (unlinkat(store->directory,name,0)) {close(fd);return CETTA_STORE_IO;}
        *out=fd;return CETTA_STORE_OK;
    }
}
static CettaDiskStoreStatus base_create(CettaDiskStore *store,unsigned char id[16],int *out) {
    *out=-1;
    for (;;) {
        if (!random_bytes(id)) return CETTA_STORE_IO;
        if (!nonzero(id,16)) continue;
        char name[45];base_name(id,name);int fd=file_open(store->directory,name,O_RDWR|O_CREAT|O_EXCL);
        if (fd<0) {if (errno==EEXIST) continue;return open_error();}
        if (flock(fd,LOCK_EX|LOCK_NB)) {close(fd);unlinkat(store->directory,name,0);return CETTA_STORE_IO;}
        *out=fd;return CETTA_STORE_OK;
    }
}
void cetta_disk_store_compact_act_close(CettaDiskCompaction *job) {
    if (!job) return;
    cetta_disk_store_blob_reader_close(job->reader);cetta_disk_store_cursor_close(job->cursor);
    cetta_disk_store_view_close(job->source);cetta_disk_store_view_close(job->result);
    cetta_act_base_release(job->base);cetta_act_base_validation_close(job->validation);cetta_act_base_builder_close(job->builder);
    cetta_disk_buffer_free(&job->key);
    for (unsigned i=0;i<4;i++) if (job->scratch[i]>=0) close(job->scratch[i]);
    if (job->output>=0) {
        if (!job->publication_attempted) {char name[45];base_name(job->identity,name);unlinkat(job->owner->directory,name,0);}
        close(job->output);
    }
    if (job->maintenance>=0) close(job->maintenance);
    cetta_disk_store_close(job->owner);free(job);
}
static CettaDiskStoreStatus compaction_fail(CettaDiskCompaction *job,CettaDiskStoreStatus status) {
    if (status==CETTA_STORE_UNKNOWN) job->owner->uncertain=true;
    job->failure=status;return status;
}
CettaDiskStoreStatus cetta_disk_store_compact_act_open(CettaDiskStore *store,
        size_t budget,CettaDiskCompaction **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;if (!store) return CETTA_STORE_INVALID;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    if (budget<cetta_act_base_builder_minimum_memory()) return CETTA_STORE_LIMIT;
    CettaDiskCompaction *job=calloc(1,sizeof(*job));if (!job) return CETTA_STORE_NOMEM;
    job->maintenance=job->output=-1;for (unsigned i=0;i<4;i++) job->scratch[i]=-1;
    job->owner=store;atomic_fetch_add(&store->references,1);
    CettaDiskStoreStatus status=CETTA_STORE_OK;
    job->maintenance=file_open(store->directory,"compact.lock",O_RDWR|O_CREAT);
    if (job->maintenance<0) status=open_error();
    else if (flock(job->maintenance,LOCK_EX|LOCK_NB)) status=errno==EWOULDBLOCK || errno==EAGAIN ? CETTA_STORE_BUSY : CETTA_STORE_IO;
    int lock=-1,manifest=-1,segment=-1;CettaDiskStoreView *current=NULL;bool rotation_attempted=false;
    if (status==CETTA_STORE_OK) status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&current,&manifest);
    if (status==CETTA_STORE_OK) status=confirm(store,&current->meta);
    if (status==CETTA_STORE_OK && !current->meta.frozen_kind) {
        unsigned char identity[16];status=segment_create(store,identity,&segment);
        if (status==CETTA_STORE_OK && (!synchronize(segment) || !synchronize(store->directory))) status=CETTA_STORE_IO;
        if (status==CETTA_STORE_OK) {
            Meta next=current->meta;next.format=3;next.frozen_kind=current->meta.format==3 ? 2u : 1u;
            memcpy(next.frozen,current->meta.segment,16);next.frozen_puts=current->meta.root;
            next.frozen_removes=current->meta.format==3 ? current->meta.removes : 0;
            next.frozen_end=current->meta.end;next.frozen_revision=current->meta.revision;
            memcpy(next.segment,identity,16);next.root=next.removes=0;next.end=CETTA_DISK_SEGMENT_HEADER_BYTES;
            rotation_attempted=true;status=publish(store,&manifest,&next);
        }
        if (segment>=0) {close(segment);segment=-1;}
        if (status==CETTA_STORE_OK) {
            cetta_disk_store_view_close(current);current=NULL;close(manifest);manifest=-1;
            status=view_load(store,false,&current,&manifest);
        }
    }
    if (status==CETTA_STORE_OK) status=cetta_disk_store_view_clone(current,&job->source);
    if (status==CETTA_STORE_OK) {
        CettaDiskStoreView *source=job->source;close(source->root.fd);
        source->root=source->frozen_puts;source->removes=source->frozen_removes;
        if (source->meta.frozen_kind==1) source->removes.root=0;
        source->frozen_puts=(CettaDiskRoot){.fd=-1};source->frozen_removes=(CettaDiskRoot){.fd=-1};
        memcpy(source->meta.segment,source->meta.frozen,16);source->meta.root=source->root.root;
        source->meta.removes=source->removes.root;source->meta.end=source->root.end;
        source->meta.revision=source->meta.frozen_revision;source->meta.frozen_kind=0;
        memset(source->meta.frozen,0,16);source->meta.frozen_puts=source->meta.frozen_removes=0;
        source->meta.frozen_end=source->meta.frozen_revision=0;
        job->info.source_revision=source->meta.revision;
        status=base_create(store,job->identity,&job->output);
    }
    for (unsigned i=0;status==CETTA_STORE_OK && i<4;i++) status=private_scratch(store,&job->scratch[i]);
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    cetta_disk_store_view_close(current);
    if (status==CETTA_STORE_OK) {
        CettaDiskStoreLimits limits=job->source->meta.limits;
        status=disk_status(act_status(cetta_act_base_builder_open(job->output,job->scratch[0],job->scratch[1],
            job->scratch[2],job->scratch[3],budget,job->identity,
            (CettaActBaseLimits){limits.key_bytes,limits.value_bytes},&job->builder)));
    }
    if (status==CETTA_STORE_OK) status=disk_status(cetta_disk_store_cursor_open_range(job->source,NULL,false,NULL,&job->cursor));
    if (status!=CETTA_STORE_OK) {
        if (rotation_attempted) {store->uncertain=true;status=CETTA_STORE_UNKNOWN;}
        cetta_disk_store_compact_act_close(job);return status;
    }
    *out=job;return CETTA_STORE_OK;
}
static CettaDiskStoreStatus install_compaction(CettaDiskCompaction *job) {
    CettaDiskStore *store=job->owner;
    if (store->uncertain) return CETTA_STORE_RECOVERY_REQUIRED;
    int lock=-1,manifest=-1;CettaDiskStoreView *current=NULL;
    CettaDiskStoreStatus status=acquire(store,true,&lock);
    if (status==CETTA_STORE_OK) status=view_load(store,false,&current,&manifest);
    if (status==CETTA_STORE_OK && (!same_domain(store,job->source,current) ||
        !current->meta.frozen_kind || memcmp(current->meta.frozen,job->source->meta.segment,16) ||
        current->meta.frozen_puts!=job->source->root.root || current->meta.frozen_removes!=job->source->removes.root ||
        current->meta.frozen_end!=job->source->root.end || current->meta.frozen_revision!=job->source->meta.revision ||
        memcmp(current->meta.base,job->source->meta.base,16))) status=CETTA_STORE_CONFLICT;
    if (status==CETTA_STORE_OK) {
        Meta next=current->meta;memcpy(next.base,job->identity,16);
        memcpy(next.base_digest,job->base_info.digest,32);next.base_bytes=job->base_info.bytes;
        memset(next.frozen,0,16);next.frozen_puts=next.frozen_removes=next.frozen_end=next.frozen_revision=0;
        next.frozen_kind=0;job->publication_attempted=true;
        status=publish(store,&manifest,&next);
        if (status==CETTA_STORE_OK) {
            cetta_act_base_release(store->base);store->base=job->base;cetta_act_base_retain(store->base);
            cetta_act_base_release(current->base);current->base=job->base;cetta_act_base_retain(current->base);
            if (current->frozen_puts.fd>=0) close(current->frozen_puts.fd);
            current->frozen_puts=(CettaDiskRoot){.fd=-1};current->frozen_removes=(CettaDiskRoot){.fd=-1};current->meta=next;
            job->result=current;current=NULL;job->info.phase=4;
        }
    }
    cetta_disk_store_view_close(current);
    if (manifest>=0) close(manifest);
    if (lock>=0) close(lock);
    return status;
}
CettaDiskStoreStatus cetta_disk_store_compact_act_step(CettaDiskCompaction *job,
        uint64_t work,CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;if (!job) return CETTA_STORE_INVALID;
    if (job->failure!=CETTA_STORE_OK) return job->failure;
    if (job->info.phase==4) {
        CettaDiskStoreStatus status=cetta_disk_store_view_clone(job->result,out);
        return status==CETTA_STORE_OK ? status : compaction_fail(job,CETTA_STORE_UNKNOWN);
    }
    if (!work) return CETTA_STORE_PROGRESS;
    if (job->info.phase==0) {
        unsigned char buffer[32768];
        while (work) {
            if (!job->pending) {
                CettaDiskStatus s=cetta_disk_store_cursor_next(job->cursor,&job->key,&job->value);
                if (s==CETTA_DISK_END) {job->info.phase=1;break;}
                if (s!=CETTA_DISK_OK) return compaction_fail(job,disk_status(s));
                s=cetta_disk_store_blob_stream_open(job->source,job->value,&job->reader);
                if (s!=CETTA_DISK_OK) return compaction_fail(job,disk_status(s));
                CettaActStatus a=cetta_act_base_value_begin(job->builder,job->value.size);
                if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
                job->pending=true;job->offset=0;
            }
            if (job->offset<job->value.size || !job->value.size) {
                size_t n=job->value.size-job->offset<sizeof(buffer) ? (size_t)(job->value.size-job->offset) : sizeof(buffer);
                if (n>work) n=(size_t)work;
                size_t got=0;CettaDiskStatus s=cetta_disk_store_blob_stream_next(job->reader,buffer,n,&got);
                if ((s!=CETTA_DISK_OK && !(s==CETTA_DISK_END && !job->value.size)) ||
                    (job->value.size && !got) || got>n)
                    return compaction_fail(job,s==CETTA_DISK_OK || s==CETTA_DISK_END ? CETTA_STORE_CORRUPT : disk_status(s));
                CettaActStatus a=cetta_act_base_value_write(job->builder,(CettaActBytes){buffer,got});
                if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
                job->offset+=got;job->info.bytes_read+=got;work-=got;
            }
            if (job->offset==job->value.size) {
                CettaActStatus a=cetta_act_base_key(job->builder,(CettaActBytes){job->key.data,job->key.size});
                if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
                work=job->key.size>=work ? 0 : work-job->key.size-1;
                cetta_disk_buffer_free(&job->key);cetta_disk_store_blob_reader_close(job->reader);job->reader=NULL;
                job->pending=false;++job->info.records;
            }
        }
        return CETTA_STORE_PROGRESS;
    }
    if (job->info.phase==1) {
        CettaActStatus a=cetta_act_base_builder_finish_step(job->builder,work,&job->base_info);
        if (a==CETTA_ACT_PROGRESS) return CETTA_STORE_PROGRESS;
        if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
        if (job->base_info.records!=job->info.records) return compaction_fail(job,CETTA_STORE_CORRUPT);
        job->info.output_bytes=job->base_info.bytes;
        a=cetta_act_base_validation_open(job->output,&job->validation);
        if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
        job->info.phase=2;return CETTA_STORE_PROGRESS;
    }
    if (job->info.phase==2) {
        CettaActStatus a=cetta_act_base_validation_step(job->validation,work,&job->base);
        if (a==CETTA_ACT_PROGRESS) return CETTA_STORE_PROGRESS;
        if (a!=CETTA_ACT_OK) return compaction_fail(job,disk_status(act_status(a)));
        CettaActBaseInfo actual;cetta_act_base_info(job->base,&actual);
        if (actual.bytes!=job->base_info.bytes || actual.records!=job->base_info.records ||
            memcmp(actual.generation,job->base_info.generation,16) || memcmp(actual.digest,job->base_info.digest,32) ||
            actual.limits.key_bytes!=job->base_info.limits.key_bytes || actual.limits.value_bytes!=job->base_info.limits.value_bytes)
            return compaction_fail(job,CETTA_STORE_CORRUPT);
        if (!synchronize(job->output) || !synchronize(job->owner->directory)) return compaction_fail(job,CETTA_STORE_IO);
        job->info.phase=3;return CETTA_STORE_PROGRESS;
    }
    CettaDiskStoreStatus status=install_compaction(job);
    if (status==CETTA_STORE_BUSY) return CETTA_STORE_PROGRESS;
    if (status!=CETTA_STORE_OK) return compaction_fail(job,status);
    status=cetta_disk_store_view_clone(job->result,out);
    return status==CETTA_STORE_OK ? status : compaction_fail(job,CETTA_STORE_UNKNOWN);
}
bool cetta_disk_store_compact_act_info(const CettaDiskCompaction *job,CettaDiskCompactionInfo *out) {
    if (!job || !out) return false;
    *out=job->info;
    CettaActBaseBuildInfo progress;
    if (job->builder && cetta_act_base_builder_info(job->builder,&progress)) out->output_bytes=progress.assembled_bytes;
    return true;
}
CettaDiskStoreStatus cetta_disk_store_compact_act(CettaDiskStore *store,CettaDiskStoreView **out) {
    if (!out) return CETTA_STORE_INVALID;
    *out=NULL;CettaDiskCompaction *job=NULL;
    CettaDiskStoreStatus status=cetta_disk_store_compact_act_open(store,32u*1024u*1024u,&job);
    if (status!=CETTA_STORE_OK) return status;
    do {status=cetta_disk_store_compact_act_step(job,65536,out);} while (status==CETTA_STORE_PROGRESS);
    cetta_disk_store_compact_act_close(job);return status;
}

bool cetta_disk_store_view_info(const CettaDiskStoreView *view,CettaDiskStoreInfo *out) {
    if (!view || !out) return false;
    *out=(CettaDiskStoreInfo){.format=view->meta.format,.revision=view->meta.revision,
        .base_bytes=view->meta.base_bytes,.delta_bytes=view->meta.end,.frozen_bytes=view->meta.frozen_end,
        .has_base=view->base!=NULL,.has_frozen=view->meta.frozen_kind!=0};
    return true;
}
void cetta_disk_store_blob_reader_storage(const CettaDiskStoreBlobReader *r,CettaStorageCensus *census) {
    if (!r || !cetta_storage_object(census,r,sizeof(*r),CETTA_STORAGE_BLOCK)) return;
    cetta_act_base_storage(r->base,census);cetta_disk_blob_reader_storage(r->disk,census);
}
void cetta_disk_store_compact_act_storage(const CettaDiskCompaction *job,CettaStorageCensus *census) {
    if (!job || !cetta_storage_object(census,job,sizeof(*job),CETTA_STORAGE_BLOCK)) return;
    if (cetta_storage_object(census,job->owner,sizeof(*job->owner),CETTA_STORAGE_BLOCK)) cetta_act_base_storage(job->owner->base,census);
    cetta_disk_store_view_storage_census(job->source,census);cetta_disk_store_view_storage_census(job->result,census);
    cetta_disk_store_cursor_storage_census(job->cursor,census);cetta_disk_store_blob_reader_storage(job->reader,census);
    cetta_act_base_storage(job->base,census);cetta_act_base_builder_storage(job->builder,census);
    cetta_act_base_validation_storage(job->validation,census);
    cetta_storage_array(census,job->key.data,job->key.data ? (job->key.size ? job->key.size : 1) : 0,1,0);
}

void cetta_disk_store_storage(const CettaDiskStore *store,CettaStorageCensus *census) {
    if (store && cetta_storage_object(census,store,sizeof(*store),CETTA_STORAGE_BLOCK)) cetta_act_base_storage(store->base,census);
}
