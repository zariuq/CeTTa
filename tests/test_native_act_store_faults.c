#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static unsigned kind,nth,calls;static bool hit;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
ssize_t __real_pread(int,void *,size_t,off_t);ssize_t __real_pwrite(int,const void *,size_t,off_t);
ssize_t __real_write(int,const void *,size_t);int __real_fsync(int);
int __real_renameat(int,const char *,int,const char *);
static bool fault(unsigned wanted) {
    if (kind!=wanted || ++calls!=nth) return false;
    hit=true;errno=EIO;return true;
}
void *__wrap_malloc(size_t n) {return fault(1) ? NULL : __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s) {return fault(1) ? NULL : __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n) {return fault(1) ? NULL : __real_realloc(p,n);}
ssize_t __wrap_pread(int fd,void *p,size_t n,off_t at) {return fault(2) ? -1 : __real_pread(fd,p,n,at);}
ssize_t __wrap_pwrite(int fd,const void *p,size_t n,off_t at) {return fault(3) ? -1 : __real_pwrite(fd,p,n,at);}
ssize_t __wrap_write(int fd,const void *p,size_t n) {return fault(4) ? -1 : __real_write(fd,p,n);}
int __wrap_fsync(int fd) {return fault(5) ? -1 : __real_fsync(fd);}
int __wrap_renameat(int a,const char *x,int b,const char *y) {
    if (fault(6)) return -1;
    int s=__real_renameat(a,x,b,y);
    if (!s && fault(7)) return -1;
    return s;
}
static CettaDiskBytes text(const char *s) {return (CettaDiskBytes){(const unsigned char *)s,strlen(s)};}
static void expect(const CettaDiskStoreView *v,const char *key,const char *value) {
    CettaDiskStoreBlob b;assert(cetta_disk_store_lookup(v,text(key),&b)==CETTA_DISK_OK && b.size==strlen(value));
    char data[32];size_t got=0;assert(cetta_disk_store_blob_read(v,b,0,data,sizeof(data),&got)==CETTA_DISK_OK);
    assert(got==strlen(value) && !memcmp(data,value,got));
}
static void clear(const char *path) {
    DIR *d=opendir(path);assert(d);struct dirent *e;
    while ((e=readdir(d))) if (strcmp(e->d_name,".") && strcmp(e->d_name,"..")) assert(!unlinkat(dirfd(d),e->d_name,0));
    closedir(d);assert(!rmdir(path));
}
static bool trial(unsigned selected,unsigned position) {
    char root[]="runtime/act-owner-fault-XXXXXX";assert(mkdtemp(root));
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open(root,true,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *empty=NULL,*before=NULL;assert(cetta_disk_store_snapshot(store,&empty)==CETTA_STORE_OK);
    CettaDiskOp ops[]={ {.key=text("a"),.value=text("one")}, {.key=text("b"),.value=text("two")},
        {.key=text("empty"),.value=text("")}, {.key=text("receipt"),.value=text("done")} };
    assert(cetta_disk_store_commit(store,empty,ops,4,&before)==CETTA_STORE_OK);cetta_disk_store_view_close(empty);
    kind=selected;nth=position;calls=0;hit=false;
    CettaDiskCompaction *job=NULL;CettaDiskStoreView *out=NULL;
    CettaDiskStoreStatus s=cetta_disk_store_compact_act_open(store,256*1024,&job);
    unsigned steps=0;
    if (s==CETTA_STORE_OK) do {
        s=cetta_disk_store_compact_act_step(job,65536,&out);assert(++steps<10000);
    } while (s==CETTA_STORE_PROGRESS);
    bool injected=hit;kind=0;
    if (injected) assert(s!=CETTA_STORE_OK);
    else assert(s==CETTA_STORE_OK && out);
    if (s==CETTA_STORE_UNKNOWN) {
        CettaDiskStoreView *refused=NULL;
        assert(cetta_disk_store_commit(store,before,NULL,0,&refused)==CETTA_STORE_RECOVERY_REQUIRED && !refused);
    }
    cetta_disk_store_compact_act_close(job);cetta_disk_store_view_close(out);
    CettaDiskStoreView *recovered=NULL;assert(cetta_disk_store_recover(store,&recovered)==CETTA_STORE_OK);
    assert(cetta_disk_store_view_revision(recovered)==1);
    expect(recovered,"a","one");expect(recovered,"b","two");expect(recovered,"empty","");expect(recovered,"receipt","done");
    cetta_disk_store_view_close(recovered);cetta_disk_store_view_close(before);cetta_disk_store_close(store);
    assert(cetta_disk_store_open(root,false,NULL,&store)==CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(store,&recovered)==CETTA_STORE_OK);
    expect(recovered,"a","one");expect(recovered,"receipt","done");
    cetta_disk_store_view_close(recovered);cetta_disk_store_close(store);clear(root);
    return injected;
}
int main(void) {
    unsigned total=0;
    for (unsigned k=1;k<=7;k++) {
        unsigned n=1;
        while (trial(k,n)) {++total;++n;assert(n<10000);}
        assert(n>1);printf("ACT owner fault class %u: %u cutpoints\n",k,n-1);fflush(stdout);
    }
    printf("ACT owner fault matrix: %u allocation/read/write/sync/rename cutpoints passed\n",total);
}
