#define _POSIX_C_SOURCE 200809L
#include "native_act_base.h"
#include "native_sha256.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { int fd[5]; char name[5][64]; } Files;
static Files files(void) {
    Files f;
    for (unsigned i=0;i<5;i++) {
        snprintf(f.name[i],sizeof(f.name[i]),"runtime/act-base-%u-XXXXXX",i);
        f.fd[i]=mkstemp(f.name[i]);assert(f.fd[i]>=0);
    }
    return f;
}
static void cleanup(Files *f) {
    for (unsigned i=0;i<5;i++) {close(f->fd[i]);assert(!unlink(f->name[i]));}
}
static CettaActBaseBuilder *builder(Files *f) {
    const unsigned char generation[16]={1,2,3};CettaActBaseBuilder *b=NULL;
    assert(cetta_act_base_builder_open(f->fd[0],f->fd[1],f->fd[2],f->fd[3],f->fd[4],
        256*1024,generation,(CettaActBaseLimits){4096,2*1024*1024},&b)==CETTA_ACT_OK);
    return b;
}
static void resign(unsigned char *p,size_t n) {
    CettaNativeSha256 sha;cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha,p,n-32);cetta_native_sha256_finish(&sha,p+n-32);
}
int main(void) {
    Files f=files();CettaActBaseBuilder *b=builder(&f);CettaActBaseInfo built;
    const size_t count=5000;unsigned char key[8],value[128];
    for (size_t i=0;i<count;i++) {
        for (unsigned j=0;j<8;j++) key[j]=(unsigned char)((uint64_t)i>>(56-8*j));
        memset(value,(unsigned char)i,sizeof(value));size_t n=i%3 ? i%sizeof(value) : 0;
        assert(cetta_act_base_value_begin(b,n)==CETTA_ACT_OK);
        for (size_t off=0;off<n;) {
            size_t amount=n-off<7 ? n-off : 7;
            assert(cetta_act_base_value_write(b,(CettaActBytes){value+off,amount})==CETTA_ACT_OK);
            off+=amount;
        }
        assert(cetta_act_base_key(b,(CettaActBytes){key,sizeof(key)})==CETTA_ACT_OK);
    }
    assert(cetta_act_base_builder_finish(b,&built)==CETTA_ACT_OK && built.records==count);
    CettaActBaseInfo again;assert(cetta_act_base_builder_finish(b,&again)==CETTA_ACT_OK);
    assert(!memcmp(&again,&built,sizeof(built)));
    cetta_act_base_builder_close(b);
    CettaActBase *base=NULL;assert(cetta_act_base_open_fd(f.fd[0],&base)==CETTA_ACT_OK);
    CettaActBaseInfo info;assert(cetta_act_base_info(base,&info));
    assert(info.records==count && info.bytes==built.bytes && !memcmp(info.digest,built.digest,32));
    for (size_t i=0;i<count;i++) {
        for (unsigned j=0;j<8;j++) key[j]=(unsigned char)((uint64_t)i>>(56-8*j));
        uint64_t ref,size;assert(cetta_act_base_lookup(base,(CettaActBytes){key,8},&ref,&size)==CETTA_ACT_OK);
        size_t n=i%3 ? i%sizeof(value) : 0;assert(size==n && (n || !ref));
        CettaActBytes bytes;assert(cetta_act_base_value(base,ref,&bytes)==CETTA_ACT_OK && bytes.size==n);
        for (size_t j=0;j<n;j++) assert(bytes.data[j]==(unsigned char)i);
    }
    CettaActCursor *cursor=NULL;
    assert(cetta_act_base_cursor(base,NULL,NULL,true,&cursor)==CETTA_ACT_OK);
    cetta_act_base_retain(base);cetta_act_base_release(base);
    for (size_t i=count;i>0;i--) {
        CettaActBytes got;uint64_t ref;
        assert(cetta_act_cursor_next(cursor,&got,&ref)==CETTA_ACT_OK && got.size==8);
        uint64_t ordinal=0;for (size_t j=0;j<8;j++) ordinal=(ordinal<<8)|got.data[j];
        assert(ordinal==i-1);
    }
    cetta_act_cursor_close(cursor);
    cetta_act_base_release(base);base=NULL;
    size_t n=(size_t)info.bytes;unsigned char *image=malloc(n),*copy=malloc(n);assert(image && copy);
    assert(pread(f.fd[0],image,n,0)==(ssize_t)n);
    size_t bad[]={0,8,16,32,40,48,56,64,72,80,88,96,n-1};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        memcpy(copy,image,n);copy[bad[i]]^=128;
        if (bad[i]==16) memset(copy+16,0,16);
        if (bad[i]==72 || bad[i]==80) memset(copy+bad[i],255,8);
        if (bad[i]!=n-1) resign(copy,n);
        assert(pwrite(f.fd[0],copy,n,0)==(ssize_t)n);
        CettaActBase *broken=NULL;
        assert(cetta_act_base_open_fd(f.fd[0],&broken)==CETTA_ACT_CORRUPT && !broken);
    }
    assert(pwrite(f.fd[0],image,n,0)==(ssize_t)n);free(copy);free(image);
    assert(cetta_act_base_open_fd(f.fd[0],&base)==CETTA_ACT_OK);
    /* Retained mappings remain valid after their names and original fd end. */
    for (unsigned j=0;j<8;j++) key[j]=(unsigned char)((uint64_t)41>>(56-8*j));
    cleanup(&f);
    uint64_t ref,size;assert(cetta_act_base_lookup(base,(CettaActBytes){key,8},&ref,&size)==CETTA_ACT_OK);
    cetta_act_base_release(base);
    f=files();b=builder(&f);
    assert(cetta_act_base_value_begin(b,17)==CETTA_ACT_OK);
    assert(cetta_act_base_value_write(b,(CettaActBytes){(const unsigned char *)"x",1})==CETTA_ACT_OK);
    assert(cetta_act_base_key(b,(CettaActBytes){NULL,0})==CETTA_ACT_CORRUPT);
    assert(cetta_act_base_builder_finish(b,&info)==CETTA_ACT_CORRUPT);
    cetta_act_base_builder_close(b);cleanup(&f);
    f=files();b=builder(&f);
    assert(cetta_act_base_builder_finish(b,&info)==CETTA_ACT_OK && !info.records);
    cetta_act_base_builder_close(b);
    assert(cetta_act_base_open_fd(f.fd[0],&base)==CETTA_ACT_OK);
    cetta_act_base_release(base);cleanup(&f);
    f=files();b=builder(&f);
    const size_t large=1024*1024+37;unsigned char chunk[4093];
    assert(cetta_act_base_value_begin(b,large)==CETTA_ACT_OK);
    for (size_t off=0;off<large;) {
        size_t amount=large-off<sizeof(chunk) ? large-off : sizeof(chunk);
        for (size_t i=0;i<amount;i++) chunk[i]=(unsigned char)(off+i);
        assert(cetta_act_base_value_write(b,(CettaActBytes){chunk,amount})==CETTA_ACT_OK);
        off+=amount;
    }
    assert(cetta_act_base_key(b,(CettaActBytes){(const unsigned char *)"large",5})==CETTA_ACT_OK);
    assert(cetta_act_base_builder_finish(b,&info)==CETTA_ACT_OK);
    cetta_act_base_builder_close(b);
    assert(cetta_act_base_open_fd(f.fd[0],&base)==CETTA_ACT_OK);
    assert(cetta_act_base_lookup(base,(CettaActBytes){(const unsigned char *)"large",5},&ref,&size)==CETTA_ACT_OK && size==large);
    CettaActBytes bytes;assert(cetta_act_base_value(base,ref,&bytes)==CETTA_ACT_OK && bytes.size==large);
    for (size_t i=0;i<large;i++) assert(bytes.data[i]==(unsigned char)i);
    cetta_act_base_release(base);cleanup(&f);
    puts("native ACT base checks passed");
}
