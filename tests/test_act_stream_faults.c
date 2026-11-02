#define _POSIX_C_SOURCE 200809L
#include "native_act_base.h"
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
    if (kind!=wanted) return false;
    if (++calls!=nth) return false;
    hit=true;errno=EIO;return true;
}
void *__wrap_malloc(size_t n) {return fault(1) ? NULL : __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s) {return fault(1) ? NULL : __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n) {return fault(1) ? NULL : __real_realloc(p,n);}
ssize_t __wrap_pread(int fd,void *p,size_t n,off_t at) {return fault(2) ? -1 : __real_pread(fd,p,n,at);}
ssize_t __wrap_pwrite(int fd,const void *p,size_t n,off_t at) {return fault(3) ? -1 : __real_pwrite(fd,p,n,at);}
int __wrap_ftruncate(int fd,off_t n) {return fault(4) ? -1 : __real_ftruncate(fd,n);}
static bool trial(bool base,unsigned selected,unsigned position) {
    char names[5][64];int fd[5];
    for (unsigned i=0;i<5;i++) {
        snprintf(names[i],sizeof(names[i]),"runtime/act-stream-fault-%u-XXXXXX",i);
        fd[i]=mkstemp(names[i]);assert(fd[i]>=0);
    }
    const char *keys[]={"", "a", "aa", "aaa", "aaaa", "aaaab", "aaab", "aab", "ab", "b"};
    CettaActBuilder *trie=NULL;CettaActBaseBuilder *b=NULL;
    const unsigned char generation[16]={1};
    kind=selected;nth=position;calls=0;hit=false;
    CettaActStatus s=base ? cetta_act_base_builder_open(fd[0],fd[1],fd[2],fd[3],fd[4],
        256*1024,generation,(CettaActBaseLimits){4096,4096},&b)
        : cetta_act_builder_open(fd[0],fd[1],fd[2],256*1024,&trie);
    for (unsigned i=0;s==CETTA_ACT_OK && i<sizeof(keys)/sizeof(*keys);i++) {
        if (base) {
            s=cetta_act_base_value_begin(b,4);
            if (s==CETTA_ACT_OK) s=cetta_act_base_value_write(b,(CettaActBytes){(const unsigned char *)"data",4});
            if (s==CETTA_ACT_OK) s=cetta_act_base_key(b,(CettaActBytes){(const unsigned char *)keys[i],strlen(keys[i])});
        } else s=cetta_act_builder_put(trie,(CettaActBytes){(const unsigned char *)keys[i],strlen(keys[i])},i);
    }
    CettaActBaseInfo info;uint64_t size=0;
    if (s==CETTA_ACT_OK) s=base ? cetta_act_base_builder_finish(b,&info) : cetta_act_builder_finish(trie,&size);
    bool injected=hit;kind=0;
    if (injected) {
        assert(s!=CETTA_ACT_OK);
        if (b) assert(cetta_act_base_builder_finish(b,&info)!=CETTA_ACT_OK);
        if (trie) assert(cetta_act_builder_finish(trie,&size)!=CETTA_ACT_OK);
    } else {
        assert(s==CETTA_ACT_OK);
        if (base) {CettaActBase *view=NULL;assert(cetta_act_base_open_fd(fd[0],&view)==CETTA_ACT_OK);cetta_act_base_release(view);}
        else {CettaActTree *view=NULL;assert(cetta_act_open(names[0],&view)==CETTA_ACT_OK);assert(cetta_act_validate(view,NULL)==CETTA_ACT_OK);cetta_act_release(view);}
    }
    cetta_act_base_builder_close(b);cetta_act_builder_close(trie);
    for (unsigned i=0;i<5;i++) {close(fd[i]);assert(!unlink(names[i]));}
    return injected;
}
int main(void) {
    unsigned total=0;
    for (unsigned base=0;base<2;base++) for (unsigned k=1;k<=4;k++) {
        unsigned n=1;
        while (trial(base!=0,k,n)) {++n;++total;assert(n<1000);}
        assert(n>1);
    }
    printf("ACT streaming faults: %u allocation/read/write/truncation cutpoints\n",total);
}
