#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include "native_journal.h"
#include "storage_census.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned checks;
#define check(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"disk store check failed at line %d: %s\n",__LINE__,#x); exit(1); \
} } while (0)
static ssize_t write_remaining=-1;
static int sync_failure=-1,sync_fail_at,sync_calls,rename_failure,rename_after_success;
static int pause_ready=-1,pause_resume=-1;
static bool pause_lock,pause_batch;
static off_t drop_unsynced_to=-1;
ssize_t __real_write(int,const void *,size_t);
int __real_fsync(int);
int __real_renameat(int,const char *,int,const char *);
int __real_openat(int,const char *,int,...);
int __wrap_openat(int directory,const char *name,int flags,...) {
    int mode=0;
    if (flags&O_CREAT) { va_list arguments;va_start(arguments,flags);mode=va_arg(arguments,int);va_end(arguments); }
    int fd=__real_openat(directory,name,flags,mode);
    if (fd>=0 && pause_ready>=0 && (flags&O_CREAT) &&
        ((pause_lock && !strcmp(name,"writer.lock")) || (pause_batch && !strncmp(name,".batch-",7)))) {
        int ready=pause_ready,resume=pause_resume;pause_lock=pause_batch=false;pause_ready=pause_resume=-1;char byte='x';
        if (__real_write(ready,&byte,1)!=1 || read(resume,&byte,1)!=1) _exit(2);
        close(ready);close(resume);
    }
    return fd;
}
ssize_t __wrap_write(int fd,const void *data,size_t size) {
    struct stat paused_file;
    if (pause_ready>=0 && !fstat(fd,&paused_file) &&
        paused_file.st_size>=CETTA_DISK_SEGMENT_HEADER_BYTES) {
        int ready=pause_ready,resume=pause_resume;pause_ready=pause_resume=-1;
        char byte='x';
        if (__real_write(ready,&byte,1)!=1 || read(resume,&byte,1)!=1) _exit(2);
        close(ready);close(resume);
    }
    if (!write_remaining) { errno=ENOSPC; return -1; }
    if (write_remaining>0 && size>(size_t)write_remaining) size=(size_t)write_remaining;
    ssize_t n=__real_write(fd,data,size);
    if (n>0 && write_remaining>0) write_remaining-=n;
    return n;
}
int __wrap_fsync(int fd) {
    ++sync_calls;
    if (fd==sync_failure || (sync_fail_at && sync_calls==sync_fail_at)) {
        if (drop_unsynced_to>=0) { check(!ftruncate(fd,drop_unsynced_to));drop_unsynced_to=-1; }
        errno=EIO; return -1;
    }
    return __real_fsync(fd);
}
int __wrap_renameat(int a,const char *x,int b,const char *y) {
    if (rename_failure) { errno=EIO; return -1; }
    int result=__real_renameat(a,x,b,y);
    if (!result && rename_after_success) { errno=EIO; return -1; }
    return result;
}
static CettaDiskBytes text(const char *value) { return (CettaDiskBytes){(const unsigned char *)value,strlen(value)}; }
static void yield_lock(void) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
static CettaDiskStoreView *snapshot(CettaDiskStore *store) {
    CettaDiskStoreView *view=NULL; check(cetta_disk_store_snapshot(store,&view)==CETTA_STORE_OK); return view;
}
static void expect(const CettaDiskStoreView *view,const char *key,const char *value) {
    CettaDiskBlob blob; const CettaDiskRoot *root=cetta_disk_store_view_root(view);
    CettaDiskStatus status=cetta_disk_lookup(root,text(key),&blob);
    if (!value) { check(status==CETTA_DISK_ABSENT); return; }
    check(status==CETTA_DISK_OK && blob.size==strlen(value));
    char bytes[256]; size_t size=0;
    check(cetta_disk_blob_read(root,blob,0,bytes,sizeof(bytes),&size)==CETTA_DISK_OK);
    check(size==strlen(value) && !memcmp(bytes,value,size));
}
static void empty_directory(const char *path) {
    DIR *directory=opendir(path); check(directory);
    struct dirent *entry;
    while ((entry=readdir(directory))) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        check(!unlinkat(dirfd(directory),entry->d_name,0));
    }
    closedir(directory); check(!rmdir(path));
}
static CettaDiskStore *open_store(const char *path) {
    CettaDiskStore *store=NULL;
    check(cetta_disk_store_open(path,true,NULL,&store)==CETTA_STORE_OK); return store;
}
static void ordinary(const char *path) {
    CettaDiskStore *missing=NULL;
    check(cetta_disk_store_open(path,false,NULL,&missing)==CETTA_STORE_ABSENT && !missing);
    CettaDiskStore *first=open_store(path),*second=open_store(path);
    CettaDiskStoreView *old=snapshot(first),*updated=NULL;
    unsigned char epoch[16]; memcpy(epoch,cetta_disk_store_view_epoch(old),16);
    check(cetta_disk_store_view_revision(old)==0);
    CettaDiskOp ops[]={{false,text("receipt"),text("done")},{false,text("usage"),text("once")},
                      {false,text("answer"),text("retained")}};
    check(cetta_disk_store_commit(first,old,ops,3,&updated)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(updated)==1);
    check(cetta_disk_store_check_view(first,old)==CETTA_STORE_OK);
    check(cetta_disk_store_check_view(second,old)==CETTA_STORE_OK);
    puts("STORE_PUBLICATION success committed 1");
    expect(updated,"receipt","done"); expect(updated,"usage","once"); expect(updated,"answer","retained");
    expect(old,"receipt",NULL);
    CettaDiskStoreView *rejected=NULL;
    check(cetta_disk_store_commit(second,old,ops,3,&rejected)==CETTA_STORE_CONFLICT && !rejected);
    CettaDiskStoreView *fresh=snapshot(second);
    check(!memcmp(epoch,cetta_disk_store_view_epoch(fresh),16));
    expect(fresh,"answer","retained");
    CettaDiskStoreView *no_op=NULL;
    check(cetta_disk_store_commit(second,fresh,NULL,0,&no_op)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(no_op)==2);
    CettaDiskStoreView *compacted=NULL;
    check(cetta_disk_store_compact(second,&compacted)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(compacted)==2);
    expect(compacted,"receipt","done"); expect(fresh,"answer","retained");
    uint64_t removed=0;
    check(cetta_disk_store_collect(second,&removed)==CETTA_STORE_OK && removed==1);
    expect(old,"receipt",NULL); expect(updated,"answer","retained");
    check(cetta_disk_store_collect(second,&removed)==CETTA_STORE_OK && removed==0);
    CettaDiskStoreView *after_compaction=NULL;
    check(cetta_disk_store_commit(second,no_op,NULL,0,&after_compaction)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(after_compaction)==3);
    cetta_disk_store_close(first); cetta_disk_store_close(second);
    expect(updated,"usage","once"); /* Views own their fds after owner close. */
    first=open_store(path); CettaDiskStoreView *reopened=snapshot(first);
    expect(reopened,"answer","retained");
    check(cetta_disk_store_view_revision(reopened)==3 && !memcmp(epoch,cetta_disk_store_view_epoch(reopened),16));
    cetta_disk_store_view_close(old); cetta_disk_store_view_close(updated);
    cetta_disk_store_view_close(fresh); cetta_disk_store_view_close(no_op);
    cetta_disk_store_view_close(compacted); cetta_disk_store_view_close(reopened); cetta_disk_store_close(first);
    cetta_disk_store_view_close(after_compaction);
}
static void locking(const char *path) {
    CettaDiskStore *store=open_store(path); CettaDiskStoreView *view=snapshot(store),*out=NULL;
    int directory=open(path,O_RDONLY|O_DIRECTORY),lock=openat(directory,"writer.lock",O_RDWR);
    check(directory>=0 && lock>=0 && !flock(lock,LOCK_EX|LOCK_NB));
    check(cetta_disk_store_snapshot(store,&out)==CETTA_STORE_BUSY && !out);
    check(cetta_disk_store_commit(store,view,NULL,0,&out)==CETTA_STORE_BUSY && !out);
    close(lock); close(directory);
    check(cetta_disk_store_commit(store,view,NULL,0,&out)==CETTA_STORE_OK);
    cetta_disk_store_view_close(out);out=NULL;
    directory=open(path,O_RDONLY|O_DIRECTORY);check(directory>=0);
    lock=openat(directory,"writer.lock",O_RDWR);check(lock>=0 && !flock(lock,LOCK_SH|LOCK_NB));
    CettaDiskStore *also=NULL;
    check(cetta_disk_store_open(path,true,NULL,&also)==CETTA_STORE_OK && also);
    cetta_disk_store_close(also);
    check(cetta_disk_store_commit(store,view,NULL,0,&out)==CETTA_STORE_BUSY && !out);
    close(lock);close(directory);
    cetta_disk_store_view_close(view); cetta_disk_store_view_close(out); cetta_disk_store_close(store);
}
static void unknown(const char *path,bool after) {
    CettaDiskStore *store=open_store(path); CettaDiskStoreView *view=snapshot(store),*out=NULL;
    CettaDiskOp op={false,text("unknown"),text("result")};
    rename_failure=!after; rename_after_success=after;
    check(cetta_disk_store_commit(store,view,&op,1,&out)==CETTA_STORE_UNKNOWN && !out);
    rename_failure=rename_after_success=0;
    check(cetta_disk_store_commit(store,view,&op,1,&out)==CETTA_STORE_RECOVERY_REQUIRED && !out);
    check(cetta_disk_store_recover(store,&out)==CETTA_STORE_OK);
    expect(out,"unknown",after ? "result" : NULL);
    printf("STORE_PUBLICATION rename-%s unknown %llu\n",after ? "after" : "before",
        (unsigned long long)cetta_disk_store_view_revision(out));
    CettaDiskStoreView *again=NULL;
    check(cetta_disk_store_commit(store,out,&op,1,&again)==CETTA_STORE_OK);
    expect(again,"unknown","result");
    cetta_disk_store_view_close(view); cetta_disk_store_view_close(out); cetta_disk_store_view_close(again);
    cetta_disk_store_close(store);
}
static void partial_writes(const char *path) {
    CettaDiskStore *store=open_store(path); CettaDiskStoreView *view=snapshot(store),*out=NULL;
    CettaDiskOp op={false,text("partial"),text("complete")};
    bool finished=false;
    for (ssize_t cut=0;cut<1024;++cut) {
        write_remaining=cut;
        CettaDiskStoreStatus status=cetta_disk_store_commit(store,view,&op,1,&out);
        write_remaining=-1;
        if (status==CETTA_STORE_OK) { finished=true; break; }
        check(status==CETTA_STORE_IO && !out);
        CettaDiskStoreView *current=snapshot(store);
        expect(current,"partial",NULL); check(cetta_disk_store_view_revision(current)==0);
        cetta_disk_store_view_close(current);
    }
    check(finished);
    expect(out,"partial","complete");
    cetta_disk_store_view_close(view); cetta_disk_store_view_close(out); cetta_disk_store_close(store);
}
static void sync_faults(const char *path,int at) {
    CettaDiskStore *store=open_store(path); CettaDiskStoreView *view=snapshot(store),*out=NULL;
    CettaDiskOp op={false,text("receipt"),text("answer received")};
    sync_calls=0; sync_fail_at=at;
    CettaDiskStoreStatus status=cetta_disk_store_commit(store,view,&op,1,&out);
    sync_fail_at=0;
    check(status==(at==3 ? CETTA_STORE_UNKNOWN : CETTA_STORE_IO) && !out);
    CettaDiskStore *reader=open_store(path); /* A new process/handle must confirm an unseen root. */
    sync_calls=0; sync_fail_at=1;
    check(cetta_disk_store_snapshot(reader,&out)==CETTA_STORE_IO && !out);
    sync_fail_at=0;
    out=snapshot(reader);
    expect(out,"receipt",at==3 ? "answer received" : NULL);
    cetta_disk_store_view_close(out); out=NULL;
    check(cetta_disk_store_recover(store,&out)==CETTA_STORE_OK);
    expect(out,"receipt",at==3 ? "answer received" : NULL);
    printf("STORE_PUBLICATION sync-%s %s %llu\n",
        at==1 ? "data" : at==2 ? "manifest" : "directory",
        status==CETTA_STORE_UNKNOWN ? "unknown" : "io",
        (unsigned long long)cetta_disk_store_view_revision(out));
    cetta_disk_store_view_close(view); cetta_disk_store_view_close(out);
    cetta_disk_store_close(reader); cetta_disk_store_close(store);
}
static void corrupt_manifest(const char *path) {
    CettaDiskStore *store=open_store(path); cetta_disk_store_close(store);
    int directory=open(path,O_RDONLY|O_DIRECTORY); check(directory>=0);
    int fd=openat(directory,"root.csl",O_WRONLY|O_TRUNC); check(fd>=0); close(fd);
    check(cetta_disk_store_open(path,true,NULL,&store)==CETTA_STORE_CORRUPT && !store);
    close(directory);
}
static void ownership(const char *path) {
    CettaDiskStore *store=open_store(path);cetta_disk_store_close(store);store=NULL;
    int directory=open(path,O_RDONLY|O_DIRECTORY);check(directory>=0);
    check(!fchmod(directory,0750));
    check(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OWNERSHIP && !store);
    check(!fchmod(directory,0700));
    check(!symlinkat(".",directory,"alias"));char alias[512];
    check(snprintf(alias,sizeof(alias),"%s/alias",path)>0);
    check(cetta_disk_store_open(alias,false,NULL,&store)==CETTA_STORE_OWNERSHIP && !store);
    check(!unlinkat(directory,"alias",0));
    int manifest=openat(directory,"root.csl",O_RDWR);check(manifest>=0);
    check(!fchmod(manifest,0640));
    check(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OWNERSHIP && !store);
    check(!fchmod(manifest,0600));close(manifest);
    check(!linkat(directory,"root.csl",directory,"linked",0));
    check(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OWNERSHIP && !store);
    check(!unlinkat(directory,"linked",0));
    check(!renameat(directory,"root.csl",directory,"saved"));
    check(!symlinkat("saved",directory,"root.csl"));
    check(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OWNERSHIP && !store);
    check(!unlinkat(directory,"root.csl",0));check(!renameat(directory,"saved",directory,"root.csl"));
    store=open_store(path);cetta_disk_store_close(store);close(directory);
}
static void old_manifest_version(const char *path) {
    CettaDiskStore *store=open_store(path);cetta_disk_store_close(store);store=NULL;
    int directory=open(path,O_RDONLY|O_DIRECTORY),fd=openat(directory,"root.csl",O_RDWR);
    struct stat st;check(directory>=0 && fd>=0 && !fstat(fd,&st));CettaJournalRecord record={0};
    check(cetta_journal_read(fd,0,(uint64_t)st.st_size,128,&record)==CETTA_JOURNAL_OK);
    check(record.size>=4 && !memcmp(record.data,"CDS2",4));memcpy(record.data,"CDS1",4);
    check(!ftruncate(fd,0) && lseek(fd,0,SEEK_SET)==0);uint64_t written=0;
    check(cetta_journal_append(fd,record.data,record.size,true,&written)==CETTA_JOURNAL_OK);
    cetta_journal_record_free(&record);close(fd);close(directory);
    check(cetta_disk_store_open(path,true,NULL,&store)==CETTA_STORE_VERSION && !store);
}
static void wrong_segment_header(const char *path) {
    CettaDiskStore *store=open_store(path);CettaDiskStoreView *view=snapshot(store);
    const CettaDiskRoot *root=cetta_disk_store_view_root(view);unsigned char id[16];memcpy(id,root->segment,16);
    static const char digits[]="0123456789abcdef";char name[45];memcpy(name,"segment-",8);
    for (size_t i=0;i<16;++i) { name[8+i*2]=digits[id[i]>>4];name[9+i*2]=digits[id[i]&15]; }
    memcpy(name+40,".csi",5);
    int directory=open(path,O_RDONLY|O_DIRECTORY),fd=openat(directory,name,O_RDWR);check(directory>=0 && fd>=0);
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);store=NULL;
    id[0]^=1;id[1]|=128;check(!ftruncate(fd,0));
    check(cetta_disk_segment_create(fd,id)==CETTA_DISK_OK);check(!fsync(fd));close(fd);close(directory);
    check(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_CORRUPT && !store);
}
static size_t segments(const char *path) {
    DIR *directory=opendir(path);check(directory);size_t count=0;struct dirent *entry;
    while ((entry=readdir(directory))) if (!strncmp(entry->d_name,"segment-",8)) ++count;
    closedir(directory);return count;
}
static void collection_confirmation(const char *path) {
    CettaDiskStore *store=open_store(path);CettaDiskStoreView *old=snapshot(store),*out=NULL;
    sync_calls=0;sync_fail_at=3;
    check(cetta_disk_store_compact(store,&out)==CETTA_STORE_UNKNOWN && !out);
    sync_fail_at=0;
    check(segments(path)==2);
    CettaDiskStore *reader=open_store(path);uint64_t removed=99;
    sync_calls=0;sync_fail_at=1;
    check(cetta_disk_store_collect(reader,&removed)==CETTA_STORE_IO && removed==0);
    sync_fail_at=0;check(segments(path)==2);
    check(cetta_disk_store_collect(reader,&removed)==CETTA_STORE_OK && removed==1);
    check(segments(path)==1);
    expect(old,"absent",NULL); /* Its unlinked generation remains readable. */
    cetta_disk_store_view_close(old);cetta_disk_store_close(reader);cetta_disk_store_close(store);
}
static void lost_writeback(const char *path) {
    CettaDiskStore *store=open_store(path);CettaDiskStoreView *empty=snapshot(store),*stable=NULL,*out=NULL;
    CettaDiskOp first={false,text("receipt"),text("before")};
    check(cetta_disk_store_commit(store,empty,&first,1,&stable)==CETTA_STORE_OK);
    CettaDiskOp ops[]={{false,text("receipt"),text("completed")},
        {false,text("usage"),text("one")},{false,text("answer"),text("retained")}};
    sync_calls=0;sync_fail_at=1;drop_unsynced_to=(off_t)cetta_disk_store_view_root(stable)->end;
    check(cetta_disk_store_commit(store,stable,ops,3,&out)==CETTA_STORE_IO && !out);
    sync_fail_at=0;check(drop_unsynced_to==-1);
    CettaDiskStoreView *current=snapshot(store);expect(current,"receipt","before");expect(current,"usage",NULL);
    check(cetta_disk_store_commit(store,stable,ops,3,&out)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(out)==2);
    expect(out,"receipt","completed");expect(out,"usage","one");expect(out,"answer","retained");
    cetta_disk_store_view_close(empty);cetta_disk_store_view_close(stable);
    cetta_disk_store_view_close(current);cetta_disk_store_view_close(out);cetta_disk_store_close(store);
}
static void live_builder(const char *path) {
    CettaDiskStore *store=open_store(path);CettaDiskStoreView *before=snapshot(store),*written=NULL;
    CettaDiskOp op={false,text("body"),text("preserved")};
    check(cetta_disk_store_commit(store,before,&op,1,&written)==CETTA_STORE_OK);
    cetta_disk_store_view_close(before);cetta_disk_store_view_close(written);
    int ready[2],resume[2];check(!pipe(ready) && !pipe(resume));pid_t child=fork();check(child>=0);
    if (!child) {
        close(ready[0]);close(resume[1]);pause_ready=ready[1];pause_resume=resume[0];
        CettaDiskStoreView *compacted=NULL;
        check(cetta_disk_store_compact(store,&compacted)==CETTA_STORE_OK);
        expect(compacted,"body","preserved");
        cetta_disk_store_view_close(compacted);cetta_disk_store_close(store);_exit(0);
    }
    close(ready[1]);close(resume[0]);char byte;
    check(read(ready[0],&byte,1)==1);check(segments(path)==2);
    uint64_t removed=99;
    check(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed==0);
    check(segments(path)==2);
    check(write(resume[1],"x",1)==1);close(ready[0]);close(resume[1]);
    int status;check(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    before=snapshot(store);expect(before,"body","preserved");
    check(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed==1);
    cetta_disk_store_view_close(before);cetta_disk_store_close(store);
}
static CettaDiskStoreScope point(const char *key) {
    return (CettaDiskStoreScope){.kind=CETTA_STORE_POINT,.lower=text(key)};
}
static void scoped(const char *path) {
    CettaDiskStore *store=open_store(path);
    CettaDiskStoreView *original=snapshot(store),*a=NULL,*b=NULL,*out=NULL;
    CettaDiskStoreScope sa=point("a/one"),sb=point("b/one");
    CettaDiskOp oa={false,text("a/one"),text("a")},ob={false,text("b/one"),text("b")};
    check(cetta_disk_store_commit_scoped(store,original,NULL,0,&sb,1,&ob,1,&b)==CETTA_STORE_OK);
    check(cetta_disk_store_commit_scoped(store,original,NULL,0,&sa,1,&oa,1,&a)==CETTA_STORE_OK);
    expect(a,"a/one","a"); expect(a,"b/one","b");
    check(cetta_disk_store_commit_scoped(store,original,&sa,1,NULL,0,NULL,0,&out)==CETTA_STORE_CONFLICT && !out);
    check(cetta_disk_store_commit_scoped(store,original,NULL,0,&sb,1,&oa,1,&out)==CETTA_STORE_INVALID && !out);
    CettaDiskStoreScope range={.kind=CETTA_STORE_RANGE,.lower=text("a/"),.upper=text("b/"),.has_lower=true,.has_upper=true};
    check(cetta_disk_store_commit_scoped(store,b,&range,1,NULL,0,NULL,0,&out)==CETTA_STORE_CONFLICT && !out);
    check(cetta_disk_store_compact(store,&out)==CETTA_STORE_OK); cetta_disk_store_view_close(out);out=NULL;
    /* Same logical values compare through a different immutable segment. */
    check(cetta_disk_store_commit_scoped(store,a,&range,1,NULL,0,NULL,0,&out)==CETTA_STORE_OK);
    expect(out,"a/one","a"); cetta_disk_store_view_close(out);out=NULL;
    CettaDiskStoreView *stable=snapshot(store),*temporary=NULL;
    CettaDiskStoreScope phantom=point("a/phantom");
    CettaDiskOp add={false,text("a/phantom"),text("temporary")},remove={true,text("a/phantom"),{0}};
    check(cetta_disk_store_commit_scoped(store,stable,NULL,0,&phantom,1,&add,1,&temporary)==CETTA_STORE_OK);
    check(cetta_disk_store_commit_scoped(store,stable,&range,1,NULL,0,NULL,0,&out)==CETTA_STORE_CONFLICT && !out);
    check(cetta_disk_store_commit(store,temporary,&remove,1,&out)==CETTA_STORE_OK);
    cetta_disk_store_view_close(temporary);cetta_disk_store_view_close(out);out=NULL;
    /* Insert followed by removal restores an absent range dependency. */
    check(cetta_disk_store_commit_scoped(store,stable,&range,1,NULL,0,NULL,0,&out)==CETTA_STORE_OK);
    cetta_disk_store_view_close(original);cetta_disk_store_view_close(a);cetta_disk_store_view_close(b);
    cetta_disk_store_view_close(stable);cetta_disk_store_view_close(out);cetta_disk_store_close(store);
}
static void parallel(const char *path,bool spend) {
    CettaDiskStore *store=open_store(path);
    if (spend) {
        CettaDiskStoreView *initial=snapshot(store),*created=NULL;
        CettaDiskOp op={false,text("balance"),text("1")};
        check(cetta_disk_store_commit(store,initial,&op,1,&created)==CETTA_STORE_OK);
        cetta_disk_store_view_close(initial);cetta_disk_store_view_close(created);
    }
    cetta_disk_store_close(store);
    enum { CHILDREN=8, STEPS=30 };
    int ready[2],go[2];check(!pipe(ready) && !pipe(go));pid_t children[CHILDREN];
    for (int child=0;child<CHILDREN;++child) {
        children[child]=fork();check(children[child]>=0);
        if (!children[child]) {
            close(ready[0]);close(go[1]);
            CettaDiskStore *actor=NULL;
            while (cetta_disk_store_open(path,false,NULL,&actor)==CETTA_STORE_BUSY) yield_lock();
            check(actor);
            CettaDiskStoreView *before=NULL;
            while (cetta_disk_store_snapshot(actor,&before)==CETTA_STORE_BUSY) yield_lock();
            if (spend) expect(before,"balance","1");
            check(before);check(write(ready[1],"x",1)==1);char byte;check(read(go[0],&byte,1)==1);
            char key[32];snprintf(key,sizeof(key),spend ? "balance" : "channel/%d",child);
            char receipt[32];snprintf(receipt,sizeof(receipt),"receipt/%d",child);
            CettaDiskStoreScope scope=point(key),write_scopes[]={scope,point(receipt)};
            for (int step=0;step<(spend ? 1 : STEPS);++step) {
                char value[32];snprintf(value,sizeof(value),"%d",step);
                CettaDiskOp ops[]={{false,text(key),text(value)},{false,text(receipt),text("spent")}};
                CettaDiskStoreView *after=NULL;
                CettaDiskStoreStatus status;
                do { status=cetta_disk_store_commit_scoped(actor,before,&scope,1,write_scopes,spend ? 2 : 1,ops,spend ? 2 : 1,&after);
                    if (status==CETTA_STORE_BUSY) yield_lock();
                } while (status==CETTA_STORE_BUSY);
                if (spend && status==CETTA_STORE_CONFLICT) break;
                check(status==CETTA_STORE_OK);cetta_disk_store_view_close(before);before=after;
            }
            cetta_disk_store_view_close(before);cetta_disk_store_close(actor);
            close(ready[1]);close(go[0]);_exit(0);
        }
    }
    close(ready[1]);close(go[0]);char bytes[CHILDREN];
    for (int i=0;i<CHILDREN;++i) check(read(ready[0],bytes,1)==1);
    check(write(go[1],"xxxxxxxx",CHILDREN)==CHILDREN);close(ready[0]);close(go[1]);
    for (int i=0;i<CHILDREN;++i) { int status;check(waitpid(children[i],&status,0)==children[i]);check(WIFEXITED(status) && !WEXITSTATUS(status)); }
    store=open_store(path);CettaDiskStoreView *view=snapshot(store);
    check(cetta_disk_store_view_revision(view)==(spend ? 2 : CHILDREN*STEPS));
    if (spend) {
        expect(view,"balance","0");CettaDiskCursor *cursor=NULL;
        check(cetta_disk_cursor_open(cetta_disk_store_view_root(view),&cursor)==CETTA_DISK_OK);
        size_t count=0;CettaDiskBuffer key;CettaDiskBlob value;
        while (cetta_disk_cursor_next(cursor,&key,&value)==CETTA_DISK_OK) { ++count;cetta_disk_buffer_free(&key); }
        check(count==2);cetta_disk_cursor_close(cursor);
    }
    else for (int i=0;i<CHILDREN;++i) { char key[32];snprintf(key,sizeof(key),"channel/%d",i);expect(view,key,"29"); }
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);
}
static void view_domains(const char *a,const char *b) {
    CettaDiskStore *first=open_store(a),*other=open_store(b);
    CettaDiskStoreView *view=snapshot(first),*recovered=NULL;
    check(cetta_disk_store_check_view(NULL,view)==CETTA_STORE_INVALID);
    check(cetta_disk_store_check_view(first,NULL)==CETTA_STORE_INVALID);
    check(cetta_disk_store_check_view(first,view)==CETTA_STORE_OK);
    check(cetta_disk_store_check_view(other,view)==CETTA_STORE_CONFLICT);
    cetta_disk_store_require_recovery(first);
    check(cetta_disk_store_check_view(first,view)==CETTA_STORE_RECOVERY_REQUIRED);
    check(cetta_disk_store_recover(first,&recovered)==CETTA_STORE_OK);
    check(cetta_disk_store_check_view(first,view)==CETTA_STORE_OK);
    check(cetta_disk_store_view_revision(recovered)==0);
    cetta_disk_store_view_close(recovered);cetta_disk_store_view_close(view);
    cetta_disk_store_close(first);cetta_disk_store_close(other);
}
static void backup_cases(const char *root) {
    enum { DESTINATIONS=15 };
    char source[320],destinations[DESTINATIONS][320];
    check(snprintf(source,sizeof(source),"%s/source",root)>0 && !mkdir(source,0700));
    for (unsigned i=0;i<DESTINATIONS;i++) {
        check(snprintf(destinations[i],sizeof(destinations[i]),"%s/copy-%u",root,i)>0);
        check(!mkdir(destinations[i],0700));
    }
    CettaDiskStore *store=open_store(source);CettaDiskStoreView *empty=snapshot(store),*view=NULL,*newer=NULL;
    check(cetta_disk_store_backup(empty,destinations[13])==CETTA_STORE_OK);
    CettaDiskStore *empty_copy=NULL;check(cetta_disk_store_open(destinations[13],false,NULL,&empty_copy)==CETTA_STORE_OK);
    CettaDiskStoreView *empty_view=snapshot(empty_copy);
    check(cetta_disk_store_view_revision(empty_view)==0);expect(empty_view,"fact",NULL);
    cetta_disk_store_view_close(empty_view);cetta_disk_store_close(empty_copy);
    CettaDiskOp seed[]={{false,text("fact"),text("captured")},{false,text("receipt"),text("once")}};
    check(cetta_disk_store_commit(store,empty,seed,2,&view)==CETTA_STORE_OK);
    cetta_disk_store_view_close(empty);
    CettaDiskOp changed={false,text("fact"),text("later")};
    check(cetta_disk_store_commit(store,view,&changed,1,&newer)==CETTA_STORE_OK);
    cetta_disk_store_view_close(newer);
    check(cetta_disk_store_compact(store,&newer)==CETTA_STORE_OK);cetta_disk_store_view_close(newer);
    uint64_t removed;check(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed>0);
    /* The source owner and original segment name may both be gone. */
    cetta_disk_store_close(store);store=NULL;
    check(cetta_disk_store_backup(view,destinations[0])==CETTA_STORE_OK);
    CettaDiskStore *copy=NULL;
    check(cetta_disk_store_open(destinations[0],false,NULL,&copy)==CETTA_STORE_OK);
    CettaDiskStoreView *restored=snapshot(copy);
    check(cetta_disk_store_view_revision(restored)==1);
    check(!memcmp(cetta_disk_store_view_epoch(restored),cetta_disk_store_view_epoch(view),16));
    expect(restored,"fact","captured");expect(restored,"receipt","once");
    check(cetta_disk_store_check_view(copy,view)==CETTA_STORE_CONFLICT);
    check(cetta_disk_store_backup(view,destinations[0])==CETTA_STORE_CONFLICT);
    check(cetta_disk_store_commit(copy,restored,&changed,1,&newer)==CETTA_STORE_OK);
    expect(newer,"fact","later");expect(view,"fact","captured");
    cetta_disk_store_view_close(newer);cetta_disk_store_view_close(restored);cetta_disk_store_close(copy);
    /* Fail every synchronization boundary. The final directory sync can
     * report UNKNOWN after the complete manifest has become visible. */
    for (int failure=1;failure<=5;failure++) {
        sync_calls=0;sync_fail_at=failure;
        CettaDiskStoreStatus status=cetta_disk_store_backup(view,destinations[failure]);
        sync_fail_at=0;
        check(status==(failure==5 ? CETTA_STORE_UNKNOWN : CETTA_STORE_IO));
        copy=NULL;
        status=cetta_disk_store_open(destinations[failure],true,NULL,&copy);
        if (failure<5) check(status==CETTA_STORE_CORRUPT && !copy);
        else {
            check(status==CETTA_STORE_OK);restored=snapshot(copy);
            expect(restored,"fact","captured");expect(restored,"receipt","once");
            cetta_disk_store_view_close(restored);cetta_disk_store_close(copy);
        }
    }
    write_remaining=100;
    check(cetta_disk_store_backup(view,destinations[6])==CETTA_STORE_IO);write_remaining=-1;
    copy=NULL;check(cetta_disk_store_open(destinations[6],true,NULL,&copy)==CETTA_STORE_CORRUPT && !copy);
    rename_failure=1;check(cetta_disk_store_backup(view,destinations[7])==CETTA_STORE_UNKNOWN);rename_failure=0;
    check(cetta_disk_store_open(destinations[7],true,NULL,&copy)==CETTA_STORE_CORRUPT && !copy);
    rename_after_success=1;check(cetta_disk_store_backup(view,destinations[8])==CETTA_STORE_UNKNOWN);rename_after_success=0;
    check(cetta_disk_store_open(destinations[8],false,NULL,&copy)==CETTA_STORE_OK);
    restored=snapshot(copy);expect(restored,"fact","captured");cetta_disk_store_view_close(restored);cetta_disk_store_close(copy);
    /* Pause a checked copy, prove its destination locked and source live,
     * then either kill it or allow it to finish its original captured root. */
    for (unsigned killed=0;killed<2;killed++) {
        int ready[2],resume[2];check(!pipe(ready) && !pipe(resume));
        pid_t child=fork();check(child>=0);
        if (!child) {
            close(ready[0]);close(resume[1]);pause_ready=ready[1];pause_resume=resume[0];
            _exit(cetta_disk_store_backup(view,destinations[9+killed])==CETTA_STORE_OK ? 0 : 3);
        }
        close(ready[1]);close(resume[0]);char byte;check(read(ready[0],&byte,1)==1);
        copy=NULL;check(cetta_disk_store_open(destinations[9+killed],false,NULL,&copy)==CETTA_STORE_BUSY && !copy);
        store=open_store(source);newer=snapshot(store);restored=NULL;
        check(cetta_disk_store_commit(store,newer,&changed,1,&restored)==CETTA_STORE_OK);
        cetta_disk_store_view_close(restored);cetta_disk_store_view_close(newer);cetta_disk_store_close(store);
        if (killed) check(!kill(child,SIGKILL));else check(write(resume[1],"x",1)==1);
        close(ready[0]);close(resume[1]);int status;check(waitpid(child,&status,0)==child);
        if (killed) {
            check(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
            check(cetta_disk_store_open(destinations[10],true,NULL,&copy)==CETTA_STORE_CORRUPT && !copy);
        } else {
            check(WIFEXITED(status) && WEXITSTATUS(status)==0);
            check(cetta_disk_store_open(destinations[9],false,NULL,&copy)==CETTA_STORE_OK);
            restored=snapshot(copy);expect(restored,"fact","captured");
            check(cetta_disk_store_view_revision(restored)==1);
            cetta_disk_store_view_close(restored);cetta_disk_store_close(copy);
        }
    }
    check(!chmod(destinations[11],0750));
    check(cetta_disk_store_backup(view,destinations[11])==CETTA_STORE_OWNERSHIP);
    check(!chmod(destinations[11],0700));
    /* Kill in bootstrap, immediately after the first lock creation. Even
     * this early boundary must not leave an apparently new empty store. */
    int ready[2],resume[2];check(!pipe(ready) && !pipe(resume));pid_t child=fork();check(child>=0);
    if (!child) {
        close(ready[0]);close(resume[1]);pause_lock=true;pause_ready=ready[1];pause_resume=resume[0];
        _exit(cetta_disk_store_backup(view,destinations[14])==CETTA_STORE_OK ? 0 : 3);
    }
    close(ready[1]);close(resume[0]);char signal_byte;check(read(ready[0],&signal_byte,1)==1);
    check(!kill(child,SIGKILL));close(ready[0]);close(resume[1]);int child_status;
    check(waitpid(child,&child_status,0)==child && WIFSIGNALED(child_status));
    copy=NULL;check(cetta_disk_store_open(destinations[14],true,NULL,&copy)==CETTA_STORE_CORRUPT && !copy);
    char descriptor[64];snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",cetta_disk_store_view_root(view)->fd);
    int corrupt=open(descriptor,O_RDWR|O_CLOEXEC);check(corrupt>=0);
    unsigned char byte;off_t end=(off_t)cetta_disk_store_view_root(view)->end;
    check(pread(corrupt,&byte,1,end-1)==1);byte^=1;check(pwrite(corrupt,&byte,1,end-1)==1);close(corrupt);
    check(cetta_disk_store_backup(view,destinations[12])==CETTA_STORE_CORRUPT);
    check(cetta_disk_store_open(destinations[12],true,NULL,&copy)==CETTA_STORE_CORRUPT && !copy);
    cetta_disk_store_view_close(view);
    for (unsigned i=0;i<DESTINATIONS;i++) empty_directory(destinations[i]);
    empty_directory(source);
}
static void batch_crash_cases(const char *parent) {
    for (unsigned phase=0;phase<3;phase++) {
        char path[512];check(snprintf(path,sizeof(path),"%s/batch-kill-%u",parent,phase)>0 && !mkdir(path,0700));
        CettaDiskStore *store=open_store(path);CettaDiskStoreView *base=snapshot(store);
        CettaDiskOp ops[]={ {.key=text("fact"),.value=text("payload")},
            {.key=text("posting"),.value=text("")}, {.key=text("receipt"),.value=text("done")} };
        int ready[2],resume[2];check(!pipe(ready) && !pipe(resume));pid_t child=fork();check(child>=0);
        if (!child) {
            close(ready[0]);close(resume[1]);CettaDiskStoreBatch *batch=NULL;CettaDiskStoreView *out=NULL;
            if (!phase) { pause_batch=true;pause_ready=ready[1];pause_resume=resume[0]; }
            check(cetta_disk_store_batch_open(store,base,1048576,&batch)==CETTA_STORE_OK);
            if (phase==1) { pause_ready=ready[1];pause_resume=resume[0]; }
            check(cetta_disk_store_batch_add(batch,ops,3)==CETTA_STORE_OK);
            if (phase==2) { pause_ready=ready[1];pause_resume=resume[0]; }
            check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_OK);
            _exit(0);
        }
        close(ready[1]);close(resume[0]);char byte;check(read(ready[0],&byte,1)==1);
        expect(base,"fact",NULL);expect(base,"posting",NULL);expect(base,"receipt",NULL);
        check(!kill(child,SIGKILL));close(ready[0]);close(resume[1]);int status;
        check(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
        CettaDiskStoreView *after=snapshot(store);
        check(cetta_disk_store_view_revision(after)==0);
        expect(after,"fact",NULL);expect(after,"posting",NULL);expect(after,"receipt",NULL);
        uint64_t removed=0;check(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed==(!phase ? 1u : 0u));
        CettaDiskStoreBatch *retry=NULL;CettaDiskStoreView *out=NULL;
        check(cetta_disk_store_batch_open(store,after,1048576,&retry)==CETTA_STORE_OK);
        check(cetta_disk_store_batch_add(retry,ops,3)==CETTA_STORE_OK);
        check(cetta_disk_store_commit_batch(store,after,retry,&out)==CETTA_STORE_OK);
        expect(out,"fact","payload");expect(out,"posting","");expect(out,"receipt","done");
        cetta_disk_store_batch_close(retry);cetta_disk_store_view_close(base);
        cetta_disk_store_view_close(after);cetta_disk_store_view_close(out);cetta_disk_store_close(store);empty_directory(path);
    }
    for (unsigned after=0;after<2;after++) {
        char path[512];check(snprintf(path,sizeof(path),"%s/batch-unknown-%u",parent,after)>0 && !mkdir(path,0700));
        CettaDiskStore *store=open_store(path);CettaDiskStoreView *base=snapshot(store),*out=NULL;
        CettaDiskStoreBatch *batch=NULL;CettaDiskOp ops[]={ {.key=text("fact"),.value=text("value")},
            {.key=text("posting"),.value=text("")}, {.key=text("receipt"),.value=text("done")} };
        check(cetta_disk_store_batch_open(store,base,1048576,&batch)==CETTA_STORE_OK);
        check(cetta_disk_store_batch_add(batch,ops,3)==CETTA_STORE_OK);
        rename_failure=!after;rename_after_success=after;
        check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_UNKNOWN && !out);
        rename_failure=rename_after_success=0;
        check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_RECOVERY_REQUIRED && !out);
        check(cetta_disk_store_recover(store,&out)==CETTA_STORE_OK);
        expect(out,"fact",after ? "value" : NULL);expect(out,"posting",after ? "" : NULL);expect(out,"receipt",after ? "done" : NULL);
        check(cetta_disk_store_view_revision(out)==after);
        cetta_disk_store_view_close(out);cetta_disk_store_view_close(base);cetta_disk_store_batch_close(batch);cetta_disk_store_close(store);empty_directory(path);
    }
}
static void operation_batches(const char *parent) {
    char path[512],other_path[512];
    check(snprintf(path,sizeof(path),"%s/batch",parent)>0 && !mkdir(path,0700));
    check(snprintf(other_path,sizeof(other_path),"%s/other",parent)>0 && !mkdir(other_path,0700));
    CettaDiskStore *store=open_store(path),*other=open_store(other_path);
    CettaDiskStoreView *base=snapshot(store),*other_base=snapshot(other),*out=NULL;
    CettaDiskStoreBatch *batch=NULL;
    check(cetta_disk_store_batch_open(store,base,0,&batch)==CETTA_STORE_INVALID && !batch);
    check(cetta_disk_store_batch_open(store,base,UINT64_C(64)*1024*1024,&batch)==CETTA_STORE_OK);
    uint64_t extent=0,maximum=0,staged=0;
    check(cetta_disk_store_batch_info(batch,&extent,&maximum,&staged) && extent==CETTA_DISK_SEGMENT_HEADER_BYTES && staged==0);
    CettaDiskOp edits[]={
        {.key=text("a"),.value=text("one")}, {.key=text("a"),.value=text("two")},
        {.key=text("empty"),.value=text("")}, {.key=text("gone"),.value=text("old")},
        {.remove=true,.key=text("gone")}, {.remove=true,.key=text("missing")},
        {.key=text("recreated"),.value=text("first")}, {.remove=true,.key=text("recreated")},
        {.key=text("recreated"),.value=text("last")}};
    check(cetta_disk_store_batch_add(batch,edits,sizeof(edits)/sizeof(*edits))==CETTA_STORE_OK);
    CettaDiskStoreView *untouched=snapshot(store);
    check(cetta_disk_store_view_revision(untouched)==0);expect(untouched,"a",NULL);
    cetta_disk_store_view_close(untouched);
    int busy=openat(AT_FDCWD,path,O_RDONLY|O_DIRECTORY);check(busy>=0);
    int lock=openat(busy,"writer.lock",O_RDWR);check(lock>=0 && !flock(lock,LOCK_EX|LOCK_NB));
    CettaDiskOp while_busy={.key=text("staged-while-busy"),.value=text("private")};
    check(cetta_disk_store_batch_add(batch,&while_busy,1)==CETTA_STORE_OK);
    check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_BUSY && !out);
    close(lock);close(busy);
    check(cetta_disk_store_commit_batch(other,other_base,batch,&out)==CETTA_STORE_INVALID && !out);
    CettaStorageCensus census={0};cetta_disk_store_batch_storage_census(batch,&census);cetta_storage_finish(&census);
    size_t retained=census.bytes;check(retained && !census.incomplete);cetta_storage_census_free(&census);
    /* Many individually bounded messages can form a transaction larger than
     * one CAV1 packet; the operation corpus lives in the private byte index. */
    char value[4096];memset(value,'x',sizeof(value));
    for (unsigned i=0;i<512;i++) {
        char key[32];check(snprintf(key,sizeof(key),"bulk/%08u",i)>0);
        CettaDiskOp op={.key=text(key),.value={(unsigned char *)value,sizeof(value)}};
        check(cetta_disk_store_batch_add(batch,&op,1)==CETTA_STORE_OK);
    }
    memset(value,'z',sizeof(value)); /* The staged bytes do not borrow input. */
    check(cetta_disk_store_batch_info(batch,&extent,&maximum,&staged) && staged==522 && extent>2*1024*1024 && extent<=maximum);
    cetta_disk_store_batch_storage_census(batch,&census);cetta_storage_finish(&census);
    check(census.bytes==retained && !census.incomplete);cetta_storage_census_free(&census);
    /* Partial append and synchronization failure do not publish any prefix. */
    write_remaining=100;
    check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_IO && !out);
    write_remaining=-1;untouched=snapshot(store);expect(untouched,"a",NULL);cetta_disk_store_view_close(untouched);
    sync_calls=0;sync_fail_at=1;
    check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_IO && !out);
    sync_fail_at=0;
    check(cetta_disk_store_commit_batch(store,base,batch,&out)==CETTA_STORE_OK && out);
    check(cetta_disk_store_view_revision(out)==1);
    expect(out,"a","two");expect(out,"empty","");expect(out,"gone",NULL);expect(out,"missing",NULL);
    expect(out,"recreated","last");expect(out,"staged-while-busy","private");expect(base,"a",NULL);
    CettaDiskBlob blob;check(cetta_disk_lookup(cetta_disk_store_view_root(out),text("bulk/00000511"),&blob)==CETTA_DISK_OK && blob.size==4096);
    unsigned char copied[4096];size_t got=0;
    check(cetta_disk_blob_read(cetta_disk_store_view_root(out),blob,0,copied,sizeof(copied),&got)==CETTA_DISK_OK && got==sizeof(copied));
    for (size_t i=0;i<sizeof(copied);i++) check(copied[i]=='x');
    CettaDiskStoreView *conflict=NULL;
    check(cetta_disk_store_commit_batch(store,base,batch,&conflict)==CETTA_STORE_CONFLICT && !conflict);
    cetta_disk_store_batch_close(batch);batch=NULL;
    /* A refused staging call retains earlier acknowledged operations, while
     * discarding every operation in that new call. */
    check(cetta_disk_store_batch_open(store,out,16384,&batch)==CETTA_STORE_OK);
    CettaDiskOp first={.key=text("quota/kept"),.value=text("yes")};
    check(cetta_disk_store_batch_add(batch,&first,1)==CETTA_STORE_OK);
    unsigned char huge[32768];memset(huge,'h',sizeof(huge));
    CettaDiskOp refused[]={ {.key=text("quota/not-kept"),.value=text("no")},
        {.key=text("quota/large"),.value={huge,sizeof(huge)}} };
    check(cetta_disk_store_batch_add(batch,refused,2)==CETTA_STORE_LIMIT);
    check(cetta_disk_store_batch_info(batch,&extent,&maximum,&staged) && staged==1 && extent<=maximum);
    write_remaining=1;
    check(cetta_disk_store_batch_add(batch,refused,1)==CETTA_STORE_IO);
    write_remaining=-1;
    CettaDiskStoreView *after=NULL;
    check(cetta_disk_store_commit_batch(store,out,batch,&after)==CETTA_STORE_OK);
    expect(after,"quota/kept","yes");expect(after,"quota/not-kept",NULL);expect(after,"quota/large",NULL);
    cetta_disk_store_batch_close(batch);batch=NULL;
    /* A closed publisher does not own the temporary file's lifetime. */
    check(cetta_disk_store_batch_open(store,after,1048576,&batch)==CETTA_STORE_OK);
    first=(CettaDiskOp){.key=text("reopen"),.value=text("same-domain")};
    check(cetta_disk_store_batch_add(batch,&first,1)==CETTA_STORE_OK);
    cetta_disk_store_close(store);store=open_store(path);
    CettaDiskStoreView *reopened=snapshot(store),*published=NULL;
    check(cetta_disk_store_commit_batch(store,reopened,batch,&published)==CETTA_STORE_OK);
    expect(published,"reopen","same-domain");cetta_disk_store_batch_close(batch);
    /* Reserved scratch names left in the create/unlink crash window are
     * harmless and collectible; ordinary user-named files are preserved. */
    int directory=open(path,O_RDONLY|O_DIRECTORY);check(directory>=0);
    int abandoned=openat(directory,".batch-0123456789abcdef0123456789abcdef",O_WRONLY|O_CREAT|O_EXCL,0600);check(abandoned>=0);close(abandoned);
    uint64_t removed=0;check(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed==1);
    check(faccessat(directory,".batch-0123456789abcdef0123456789abcdef",F_OK,0)<0 && errno==ENOENT);close(directory);
    cetta_disk_store_view_close(published);cetta_disk_store_view_close(reopened);cetta_disk_store_view_close(after);
    cetta_disk_store_view_close(out);cetta_disk_store_view_close(base);cetta_disk_store_view_close(other_base);
    cetta_disk_store_close(other);cetta_disk_store_close(store);
    empty_directory(path);empty_directory(other_path);
}
int main(void) {
    char root[]="runtime/disk-store-test.XXXXXX"; check(mkdtemp(root));
    const char *labels[]={"ordinary","lock","unknown-before","unknown-after","partial","corrupt","sync-data","sync-manifest","sync-directory","scoped","parallel","spenders","collection-confirmation","live-builder","lost-writeback","ownership","old-version","wrong-header","view-domain-a","view-domain-b","backups"};
    enum { CASES=sizeof(labels)/sizeof(*labels) };char paths[CASES][256];
    for (size_t i=0;i<CASES;++i) {
        check(snprintf(paths[i],sizeof(paths[i]),"%s/%s",root,labels[i])>0);
        check(!mkdir(paths[i],0700));
    }
    ordinary(paths[0]); locking(paths[1]); unknown(paths[2],false); unknown(paths[3],true);
    partial_writes(paths[4]); corrupt_manifest(paths[5]);
    for (size_t i=0;i<3;++i) sync_faults(paths[6+i],(int)i+1);
    scoped(paths[9]);parallel(paths[10],false);parallel(paths[11],true);
    collection_confirmation(paths[12]);live_builder(paths[13]);
    lost_writeback(paths[14]);
    ownership(paths[15]);old_manifest_version(paths[16]);wrong_segment_header(paths[17]);
    view_domains(paths[18],paths[19]);
    backup_cases(paths[20]);
    operation_batches(root);
    batch_crash_cases(root);
    for (size_t i=0;i<CASES;++i) empty_directory(paths[i]);
    check(!rmdir(root)); printf("native disk store: %u checks passed\n",checks); return 0;
}
