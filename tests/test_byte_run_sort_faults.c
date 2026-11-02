#define _POSIX_C_SOURCE 200809L
#include "byte_run_sort.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned kind,nth,calls;
static bool hit;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
ssize_t __real_pread(int,void *,size_t,off_t);ssize_t __real_pwrite(int,const void *,size_t,off_t);
int __real_ftruncate(int,off_t);
static bool fault(unsigned wanted) {
    if (kind!=wanted || ++calls!=nth) return false;
    hit=true;errno=EIO;return true;
}
void *__wrap_malloc(size_t n) {return fault(1) ? NULL : __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s) {return fault(1) ? NULL : __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n) {return fault(1) ? NULL : __real_realloc(p,n);}
ssize_t __wrap_pread(int fd,void *p,size_t n,off_t at) {return fault(2) ? -1 : __real_pread(fd,p,n,at);}
ssize_t __wrap_pwrite(int fd,const void *p,size_t n,off_t at) {return fault(3) ? -1 : __real_pwrite(fd,p,n,at);}
int __wrap_ftruncate(int fd,off_t n) {return fault(4) ? -1 : __real_ftruncate(fd,n);}
static CettaActStatus drive(CettaByteRunSort *s) {
    CettaActStatus status;
    do status=cetta_byte_sort_step(s,4096);while (status==CETTA_ACT_PROGRESS);
    return status;
}
static bool trial(unsigned selected,unsigned position) {
    int fd[3];
    for (size_t i=0;i<3;i++) {char path[]="runtime/sort-fault-XXXXXX";fd[i]=mkstemp(path);assert(fd[i]>=0 && !unlink(path));}
    kind=selected;nth=position;calls=0;hit=false;
    CettaByteRunSort *s=NULL;CettaActStatus status=cetta_byte_sort_open(fd[0],fd[1],fd[2],45000,&s);
    for (unsigned i=0;i<96 && status==CETTA_ACT_OK;i++) {
        unsigned char key[48];memset(key,'x',sizeof(key));key[46]=(unsigned char)(95-i);
        do {
            status=cetta_byte_sort_put(s,(CettaActBytes){key,sizeof(key)},i);
            if (status==CETTA_ACT_PROGRESS) {status=drive(s);if (status==CETTA_ACT_OK) status=CETTA_ACT_PROGRESS;}
        } while (status==CETTA_ACT_PROGRESS);
    }
    if (status==CETTA_ACT_OK) {status=cetta_byte_sort_seal(s);if (status==CETTA_ACT_PROGRESS) status=drive(s);}
    unsigned count=0;CettaByteSortItem row;
    while (status==CETTA_ACT_OK && (status=cetta_byte_sort_next(s,&row))==CETTA_ACT_OK) {
        unsigned char key[48];size_t n;
        status=cetta_byte_sort_read(s,row.key,0,key,sizeof(key),&n);
        if (status==CETTA_ACT_OK) {assert(n==48 && key[46]==count && row.value==95-count);++count;}
    }
    bool injected=hit;kind=0;
    if (injected) {
        assert(status!=CETTA_ACT_OK && status!=CETTA_ACT_END && status!=CETTA_ACT_PROGRESS);
        if (s) assert(cetta_byte_sort_step(s,4096)==status);
    } else assert(status==CETTA_ACT_END && count==96);
    cetta_byte_sort_close(s);for (size_t i=0;i<3;i++) close(fd[i]);return injected;
}
int main(void) {
    unsigned total=0;
    for (unsigned k=1;k<=4;k++) {
        unsigned n=1;while (trial(k,n)) {++n;++total;assert(n<10000);}
        assert(n>1);
    }
    printf("byte sort faults: %u allocation/read/write/truncation cutpoints passed\n",total);
}
