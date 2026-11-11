#define _POSIX_C_SOURCE 200809L
#include "byte_run_sort.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {unsigned char *key;size_t size;uint64_t value;} Item;
static int compare(const void *aa,const void *bb) {
    const Item *a=aa,*b=bb;size_t n=a->size<b->size ? a->size : b->size;
    int c=n ? memcmp(a->key,b->key,n) : 0;
    if (!c) c=(a->size>b->size)-(a->size<b->size);
    if (!c) c=(a->value>b->value)-(a->value<b->value);
    return c;
}
static void drive(CettaByteRunSort *s,uint64_t quantum) {
    unsigned steps=0;CettaActStatus status;
    do {status=cetta_byte_sort_step(s,quantum);assert(++steps<10000000);} while (status==CETTA_ACT_PROGRESS);
    assert(status==CETTA_ACT_OK);
}
static void check(Item *items,size_t count,uint64_t quantum) {
    int fd[3];char names[3][64];
    for (unsigned i=0;i<3;i++) {
        snprintf(names[i],sizeof(names[i]),"runtime/byte-sort-%u-XXXXXX",i);fd[i]=mkstemp(names[i]);assert(fd[i]>=0);
        assert(!unlink(names[i]));
    }
    const size_t budget=65536;CettaByteRunSort *s=NULL;
    assert(cetta_byte_sort_open(fd[0],fd[1],fd[2],budget,&s)==CETTA_ACT_OK);
    for (size_t i=0;i<count;i++) {
        CettaActStatus status;
        do {
            status=cetta_byte_sort_put(s,(CettaActBytes){items[i].key,items[i].size},items[i].value);
            if (status==CETTA_ACT_PROGRESS) drive(s,quantum);
        } while (status==CETTA_ACT_PROGRESS);
        assert(status==CETTA_ACT_OK);
        CettaByteSortInfo info;assert(cetta_byte_sort_info(s,&info) && info.heap_bytes<=budget);
    }
    CettaActStatus status=cetta_byte_sort_seal(s);
    assert(status==CETTA_ACT_OK || status==CETTA_ACT_PROGRESS);
    drive(s,quantum);if (count>1) qsort(items,count,sizeof(*items),compare);
    unsigned char buffer[4093];CettaByteSpan previous={0};
    for (size_t i=0;i<count;i++) {
        CettaByteSortItem got;assert(cetta_byte_sort_next(s,&got)==CETTA_ACT_OK);
        assert(got.value==items[i].value && got.key.size==items[i].size);
        for (uint64_t off=0;off<got.key.size;) {
            size_t n=0;assert(cetta_byte_sort_read(s,got.key,off,buffer,sizeof(buffer),&n)==CETTA_ACT_OK && n);
            assert(!memcmp(buffer,items[i].key+(size_t)off,n));off+=n;
        }
        if (i) {
            int order;unsigned steps=0;
            do {status=cetta_byte_sort_compare(s,previous,got.key,257,&order);assert(++steps<1000000);} while (status==CETTA_ACT_PROGRESS);
            assert(status==CETTA_ACT_OK && order<=0);
        }
        previous=got.key;
    }
    CettaByteSortItem got;assert(cetta_byte_sort_next(s,&got)==CETTA_ACT_END);
    CettaByteSortInfo info;assert(cetta_byte_sort_info(s,&info) && info.heap_bytes<=budget && info.records==count);
    cetta_byte_sort_close(s);for (unsigned i=0;i<3;i++) close(fd[i]);
}
int main(void) {
    check(NULL,0,1);
    Item one={(unsigned char *)"",0,UINT64_MAX};check(&one,1,1);
    size_t count=12000;Item *items=calloc(count,sizeof(*items));assert(items);
    uint64_t random=317;
    for (size_t i=0;i<count;i++) {
        random^=random<<13;random^=random>>7;random^=random<<17;
        size_t n=i%31 ? 40 : 0;
        items[i]=(Item){.key=malloc(n ? n : 1),.size=n,.value=i};assert(items[i].key);
        if (n) {
            memset(items[i].key,'x',32);uint64_t tail=i%7 ? random : i%23;
            for (unsigned j=0;j<8;j++) items[i].key[32+j]=(unsigned char)(tail>>(8*j));
        }
    }
    check(items,count,8192);
    for (size_t i=0;i<count;i++) free(items[i].key);
    free(items);
    Item wide[256];unsigned char labels[256];
    for (unsigned i=0;i<256;i++) {labels[i]=(unsigned char)(255-i);wide[i]=(Item){labels+i,1,i};}
    check(wide,256,1);
    size_t n=1024*1024;unsigned char *long_keys=malloc(n*3);assert(long_keys);memset(long_keys,'a',n*3);
    long_keys[n-1]=2;long_keys[n*2-1]=0;long_keys[n*3-1]=1;
    Item large[]={ {long_keys,n,1},{long_keys+n,n,2},{long_keys+n*2,n,3} };
    check(large,3,4096);free(long_keys);
    puts("byte run sort: multi-pass, duplicate, binary, oversized and tiny-work controls passed");
}
