#define _POSIX_C_SOURCE 200809L
#include "native_act_base.h"
#include "native_sha256.h"
#include "storage_census.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { HEADER=128, DIGEST=32 };
struct CettaActBase {
    atomic_size_t references;
    CettaActTree *mapping,*trie;
    CettaActBytes payload;
    CettaActBaseInfo info;
};
struct CettaActBaseBuilder {
    int output,trie_file,payload_file;
    CettaActBuilder *trie;
    CettaActBaseInfo info;
    uint64_t payload_end,remaining,reference;
    bool ready,finished,finishing;
    unsigned phase;
    uint64_t trie_size,copy_offset,output_end;
    CettaNativeSha256 sha;
    CettaActStatus failure;
    unsigned char buffer[32768];
};
static uint64_t get64(const unsigned char *p) {
    uint64_t v=0;for (unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i);return v;
}
static void put64(unsigned char *p,uint64_t v) {
    for (unsigned i=0;i<8;i++) p[i]=(unsigned char)(v>>(8*i));
}
static bool nonzero(const unsigned char *p,size_t n) {
    unsigned char v=0;for (size_t i=0;i<n;i++) v|=p[i];return v!=0;
}
static bool io_at(int fd,void *data,size_t n,uint64_t at,bool writing) {
    if (at>INT64_MAX || n>(uint64_t)INT64_MAX-at) return false;
    unsigned char *p=data;
    while (n) {
        ssize_t got=writing ? pwrite(fd,p,n,(off_t)at) : pread(fd,p,n,(off_t)at);
        if (got<0 && errno==EINTR) continue;
        if (got<=0) return false;
        p+=got;n-=(size_t)got;at+=(uint64_t)got;
    }
    return true;
}
void cetta_act_base_release(CettaActBase *b) {
    if (b && atomic_fetch_sub(&b->references,1)==1) {
        cetta_act_release(b->trie);cetta_act_release(b->mapping);free(b);
    }
}
void cetta_act_base_retain(CettaActBase *b) { if (b) atomic_fetch_add(&b->references,1); }
bool cetta_act_base_info(const CettaActBase *b,CettaActBaseInfo *out) {
    if (!b || !out) return false;
    *out=b->info;return true;
}
CettaActStatus cetta_act_base_value(const CettaActBase *b,uint64_t reference,CettaActBytes *out) {
    if (!b || !out) return CETTA_ACT_CORRUPT;
    *out=(CettaActBytes){0};
    if (reference>=b->payload.size) return CETTA_ACT_CORRUPT;
    uint64_t n;size_t used;
    if (!cetta_act_varint(b->payload.data+(size_t)reference,b->payload.size-(size_t)reference,&n,&used) ||
        n>b->info.limits.value_bytes || n>b->payload.size-(size_t)reference-used)
        return CETTA_ACT_CORRUPT;
    *out=(CettaActBytes){b->payload.data+(size_t)reference+used,(size_t)n};return CETTA_ACT_OK;
}
CettaActStatus cetta_act_base_lookup(const CettaActBase *b,CettaActBytes key,uint64_t *ref,uint64_t *size) {
    if (!b || !ref || !size || key.size>b->info.limits.key_bytes) return CETTA_ACT_CORRUPT;
    *ref=*size=0;
    CettaActStatus s=cetta_act_get(b->trie,key,ref);
    CettaActBytes value;
    if (s==CETTA_ACT_OK) {
        s=cetta_act_base_value(b,*ref,&value);
        if (s==CETTA_ACT_OK) *size=value.size;
    }
    return s;
}
CettaActStatus cetta_act_base_cursor(CettaActBase *b,const CettaActBytes *lo,
        const CettaActBytes *hi,bool reverse,CettaActCursor **out) {
    if (!b) return CETTA_ACT_CORRUPT;
    return reverse ? cetta_act_cursor_open_reverse(b->trie,lo,hi,out)
                   : cetta_act_cursor_open(b->trie,lo,hi,out);
}
void cetta_act_base_storage(const CettaActBase *b,CettaStorageCensus *census) {
    if (!b || !cetta_storage_object(census,b,sizeof(*b),CETTA_STORAGE_BLOCK)) return;
    cetta_act_storage(b->trie,census);cetta_act_storage(b->mapping,census);
}

struct CettaActBaseValidation {
    CettaActBase *base;
    CettaActCursor *cursor;
    CettaNativeSha256 sha;
    unsigned char *previous;
    size_t previous_size,previous_capacity;
    uint64_t hashed,seen,next,trie_size;
    unsigned phase;
    bool have_previous;
    CettaActStatus failure;
};
void cetta_act_base_validation_close(CettaActBaseValidation *v) {
    if (v) {cetta_act_cursor_close(v->cursor);cetta_act_base_release(v->base);free(v->previous);free(v);}
}
CettaActStatus cetta_act_base_validation_open(int fd,CettaActBaseValidation **out) {
    if (!out) return CETTA_ACT_CORRUPT;
    *out=NULL;
    CettaActBaseValidation *v=calloc(1,sizeof(*v));if (!v) return CETTA_ACT_NOMEM;
    CettaActBase *b=calloc(1,sizeof(*b));if (!b) {free(v);return CETTA_ACT_NOMEM;}
    atomic_init(&b->references,1);v->base=b;v->next=1;
    CettaActStatus s=cetta_act_map_fd(fd,&b->mapping);
    if (s!=CETTA_ACT_OK) goto failed;
    CettaActBytes image=cetta_act_image(b->mapping);s=CETTA_ACT_CORRUPT;
    if (image.size<HEADER+DIGEST+26 || memcmp(image.data,"CDBACT01",8) ||
        get64(image.data+8)!=HEADER || !nonzero(image.data+16,16) ||
        get64(image.data+32)!=HEADER || get64(image.data+88)!=1 || nonzero(image.data+96,32)) goto failed;
    uint64_t trie_size=get64(image.data+40),payload_at=get64(image.data+48),payload_size=get64(image.data+56);
    uint64_t key_limit=get64(image.data+72),value_limit=get64(image.data+80);
    if (trie_size>image.size-HEADER-DIGEST || payload_at!=HEADER+trie_size ||
        !payload_size || payload_size!=image.size-DIGEST-payload_at || key_limit>SIZE_MAX || value_limit>SIZE_MAX ||
        key_limit>UINT32_MAX-13u || value_limit>UINT32_MAX-1u || image.data[payload_at]) goto failed;
    b->payload=(CettaActBytes){image.data+(size_t)payload_at,(size_t)payload_size};
    memcpy(b->info.generation,image.data+16,16);memcpy(b->info.digest,image.data+image.size-DIGEST,DIGEST);
    b->info.bytes=image.size;b->info.records=get64(image.data+64);
    b->info.limits=(CettaActBaseLimits){(size_t)key_limit,(size_t)value_limit};v->trie_size=trie_size;
    cetta_native_sha256_init(&v->sha);*out=v;return CETTA_ACT_OK;
failed:
    cetta_act_base_validation_close(v);return s;
}
static CettaActStatus validation_fail(CettaActBaseValidation *v,CettaActStatus status) {
    v->failure=status;return status;
}
CettaActStatus cetta_act_base_validation_step(CettaActBaseValidation *v,uint64_t work,CettaActBase **out) {
    if (!v || !out) return CETTA_ACT_CORRUPT;
    *out=NULL;if (v->failure!=CETTA_ACT_OK) return v->failure;
    CettaActBase *b=v->base;CettaActBytes image=cetta_act_image(b->mapping);
    if (v->phase==2) {*out=b;cetta_act_base_retain(b);return CETTA_ACT_OK;}
    while (work && !v->phase) {
        uint64_t remaining=image.size-DIGEST-v->hashed;
        if (!remaining) {
            unsigned char digest[DIGEST];cetta_native_sha256_finish(&v->sha,digest);
            if (memcmp(digest,b->info.digest,DIGEST)) return validation_fail(v,CETTA_ACT_CORRUPT);
            CettaActStatus s=cetta_act_view(b->mapping,HEADER,(size_t)v->trie_size,&b->trie);
            if (s==CETTA_ACT_OK) s=cetta_act_cursor_open(b->trie,NULL,NULL,&v->cursor);
            if (s!=CETTA_ACT_OK) return validation_fail(v,s);
            if (!cetta_act_cursor_key_limit(v->cursor,b->info.limits.key_bytes)) return validation_fail(v,CETTA_ACT_CORRUPT);
            v->phase=1;break;
        }
        size_t n=remaining<32768 ? (size_t)remaining : 32768;
        if (n>work) n=(size_t)work;
        cetta_native_sha256_update(&v->sha,image.data+(size_t)v->hashed,n);v->hashed+=n;work-=n;
    }
    while (work && v->phase==1) {
        CettaActBytes key;uint64_t reference;
        CettaActStatus s=cetta_act_cursor_next_bounded(v->cursor,1,&key,&reference);--work;
        if (s==CETTA_ACT_PROGRESS) continue;
        if (s==CETTA_ACT_END) {
            if (v->seen!=b->info.records || v->next!=b->payload.size) return validation_fail(v,CETTA_ACT_CORRUPT);
            v->phase=2;*out=b;cetta_act_base_retain(b);return CETTA_ACT_OK;
        }
        if (s!=CETTA_ACT_OK) return validation_fail(v,s);
        size_t common=key.size<v->previous_size ? key.size : v->previous_size;
        int order=common ? memcmp(v->previous,key.data,common) : 0;
        if (!order) order=(v->previous_size>key.size)-(v->previous_size<key.size);
        CettaActBytes value;
        if ((v->have_previous && order>=0) || v->seen>=b->info.records ||
            (reference && reference!=v->next) || cetta_act_base_value(b,reference,&value)!=CETTA_ACT_OK ||
            (reference && !value.size)) return validation_fail(v,CETTA_ACT_CORRUPT);
        if (reference) {
            unsigned char length[9];size_t n=cetta_act_put_varint(length,value.size);
            if (memcmp(b->payload.data+(size_t)reference,length,n)) return validation_fail(v,CETTA_ACT_CORRUPT);
            v->next=reference+n+value.size;
        }
        if (key.size>v->previous_capacity) {
            void *p=realloc(v->previous,key.size);
            if (!p) return validation_fail(v,CETTA_ACT_NOMEM);
            v->previous=p;v->previous_capacity=key.size;
        }
        if (key.size) memcpy(v->previous,key.data,key.size);
        v->previous_size=key.size;v->have_previous=true;++v->seen;
        work=key.size>=work ? 0 : work-key.size;
    }
    return CETTA_ACT_PROGRESS;
}
CettaActStatus cetta_act_base_open_fd(int fd,CettaActBase **out) {
    if (!out) return CETTA_ACT_CORRUPT;
    *out=NULL;CettaActBaseValidation *v=NULL;
    CettaActStatus s=cetta_act_base_validation_open(fd,&v);
    if (s!=CETTA_ACT_OK) return s;
    do {s=cetta_act_base_validation_step(v,65536,out);} while (s==CETTA_ACT_PROGRESS);
    cetta_act_base_validation_close(v);return s;
}
static CettaActStatus fail(CettaActBaseBuilder *b,CettaActStatus s) {
    if (b && b->failure==CETTA_ACT_OK) b->failure=s;
    return s;
}
void cetta_act_base_builder_close(CettaActBaseBuilder *b) {
    if (!b) return;
    cetta_act_builder_close(b->trie);
    if (b->output>=0) close(b->output);
    if (b->trie_file>=0) close(b->trie_file);
    if (b->payload_file>=0) close(b->payload_file);
    free(b);
}
size_t cetta_act_base_builder_minimum_memory(void) {
    return sizeof(CettaActBaseBuilder)+cetta_act_builder_minimum_memory();
}
CettaActStatus cetta_act_base_builder_open(int output,int trie,int payload,
        int frontier,int key,size_t budget,const unsigned char generation[16],
        CettaActBaseLimits limits,CettaActBaseBuilder **out) {
    if (!out) return CETTA_ACT_CORRUPT;
    *out=NULL;
    if (!generation || !nonzero(generation,16) || limits.key_bytes>UINT32_MAX-13u ||
        limits.value_bytes>UINT32_MAX-1u) return CETTA_ACT_CORRUPT;
    if (budget<sizeof(CettaActBaseBuilder)) return CETTA_ACT_LIMIT;
    int files[5]={output,trie,payload,frontier,key};struct stat st[5];
    for (unsigned i=0;i<5;i++) {
        if (fstat(files[i],&st[i])) return CETTA_ACT_IO;
        if (!S_ISREG(st[i].st_mode) || st[i].st_size) return CETTA_ACT_CORRUPT;
        for (unsigned j=0;j<i;j++) if (st[i].st_dev==st[j].st_dev && st[i].st_ino==st[j].st_ino)
            return CETTA_ACT_CORRUPT;
    }
    CettaActBaseBuilder *b=calloc(1,sizeof(*b));if (!b) return CETTA_ACT_NOMEM;
    b->output=b->trie_file=b->payload_file=-1;
    b->output=fcntl(output,F_DUPFD_CLOEXEC,0);b->trie_file=fcntl(trie,F_DUPFD_CLOEXEC,0);
    b->payload_file=fcntl(payload,F_DUPFD_CLOEXEC,0);
    CettaActStatus s=b->output<0 || b->trie_file<0 || b->payload_file<0 ? CETTA_ACT_IO :
        cetta_act_builder_open(trie,frontier,key,budget-sizeof(*b),&b->trie);
    unsigned char empty=0;
    if (s==CETTA_ACT_OK && !io_at(b->payload_file,&empty,1,0,true)) s=CETTA_ACT_IO;
    if (s!=CETTA_ACT_OK) {cetta_act_base_builder_close(b);return s;}
    b->payload_end=1;b->info.limits=limits;memcpy(b->info.generation,generation,16);
    *out=b;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_base_value_begin(CettaActBaseBuilder *b,uint64_t size) {
    if (!b || b->finished || b->finishing) return CETTA_ACT_CORRUPT;
    if (b->failure!=CETTA_ACT_OK) return b->failure;
    if (b->ready || size>b->info.limits.value_bytes ||
        (size && (b->payload_end>(uint64_t)INT64_MAX-9 || size>(uint64_t)INT64_MAX-b->payload_end-9)))
        return fail(b,CETTA_ACT_LIMIT);
    b->ready=true;b->remaining=size;b->reference=size ? b->payload_end : 0;
    if (size) {
        unsigned char length[9];size_t n=cetta_act_put_varint(length,size);
        if (!io_at(b->payload_file,length,n,b->payload_end,true)) return fail(b,CETTA_ACT_IO);
        b->payload_end+=n;
    }
    return CETTA_ACT_OK;
}
CettaActStatus cetta_act_base_value_write(CettaActBaseBuilder *b,CettaActBytes bytes) {
    if (!b || b->finished || b->finishing) return CETTA_ACT_CORRUPT;
    if (b->failure!=CETTA_ACT_OK) return b->failure;
    if (!b->ready || bytes.size>b->remaining || (bytes.size && !bytes.data)) return fail(b,CETTA_ACT_CORRUPT);
    if (!io_at(b->payload_file,(void *)bytes.data,bytes.size,b->payload_end,true)) return fail(b,CETTA_ACT_IO);
    b->payload_end+=bytes.size;b->remaining-=bytes.size;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_base_key(CettaActBaseBuilder *b,CettaActBytes key) {
    if (!b || b->finished || b->finishing) return CETTA_ACT_CORRUPT;
    if (b->failure!=CETTA_ACT_OK) return b->failure;
    if (!b->ready || b->remaining || key.size>b->info.limits.key_bytes) return fail(b,CETTA_ACT_CORRUPT);
    CettaActStatus s=cetta_act_builder_put(b->trie,key,b->reference);
    if (s!=CETTA_ACT_OK) return fail(b,s);
    b->ready=false;return CETTA_ACT_OK;
}

CettaActStatus cetta_act_base_builder_finish_step(CettaActBaseBuilder *b,uint64_t work,CettaActBaseInfo *out) {
    if (!b || !out) return CETTA_ACT_CORRUPT;
    if (b->failure!=CETTA_ACT_OK) return b->failure;
    if (b->finished) {*out=b->info;return CETTA_ACT_OK;}
    if (!work) return CETTA_ACT_PROGRESS;
    if (b->ready) return fail(b,CETTA_ACT_CORRUPT);
    b->finishing=true;
    if (!b->phase) {
        CettaActStatus s=cetta_act_builder_finish_step(b->trie,work,&b->trie_size);
        if (s==CETTA_ACT_PROGRESS) return s;
        if (s!=CETTA_ACT_OK) return fail(b,s);
        if (b->trie_size>(uint64_t)INT64_MAX-HEADER-DIGEST ||
            b->payload_end>(uint64_t)INT64_MAX-HEADER-DIGEST-b->trie_size) return fail(b,CETTA_ACT_LIMIT);
        CettaActBuildInfo ti;cetta_act_builder_info(b->trie,&ti);
        unsigned char header[HEADER]={0};memcpy(header,"CDBACT01",8);put64(header+8,HEADER);
        memcpy(header+16,b->info.generation,16);put64(header+32,HEADER);put64(header+40,b->trie_size);
        put64(header+48,HEADER+b->trie_size);put64(header+56,b->payload_end);put64(header+64,ti.records);
        put64(header+72,b->info.limits.key_bytes);put64(header+80,b->info.limits.value_bytes);put64(header+88,1);
        cetta_native_sha256_init(&b->sha);cetta_native_sha256_update(&b->sha,header,HEADER);
        if (!io_at(b->output,header,HEADER,0,true)) return fail(b,CETTA_ACT_IO);
        b->info.records=ti.records;b->output_end=HEADER;b->phase=1;
        return CETTA_ACT_PROGRESS;
    }
    while (work && b->phase<3) {
        uint64_t size=b->phase==1 ? b->trie_size : b->payload_end;
        int source=b->phase==1 ? b->trie_file : b->payload_file;
        if (b->copy_offset==size) {++b->phase;b->copy_offset=0;continue;}
        size_t n=size-b->copy_offset<sizeof(b->buffer) ? (size_t)(size-b->copy_offset) : sizeof(b->buffer);
        if (n>work) n=(size_t)work;
        if (!io_at(source,b->buffer,n,b->copy_offset,false) ||
            !io_at(b->output,b->buffer,n,b->output_end,true)) return fail(b,CETTA_ACT_IO);
        cetta_native_sha256_update(&b->sha,b->buffer,n);
        b->copy_offset+=n;b->output_end+=n;work-=n;
    }
    if (b->phase<3 || !work) return CETTA_ACT_PROGRESS;
    cetta_native_sha256_finish(&b->sha,b->info.digest);
    if (!io_at(b->output,b->info.digest,DIGEST,b->output_end,true)) return fail(b,CETTA_ACT_IO);
    b->info.bytes=b->output_end+DIGEST;b->finished=true;*out=b->info;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_base_builder_finish(CettaActBaseBuilder *b,CettaActBaseInfo *out) {
    CettaActStatus s;
    do {s=cetta_act_base_builder_finish_step(b,UINT64_MAX,out);} while (s==CETTA_ACT_PROGRESS);
    return s;
}

void cetta_act_base_validation_storage(const CettaActBaseValidation *v,CettaStorageCensus *census) {
    if (!v || !cetta_storage_object(census,v,sizeof(*v),CETTA_STORAGE_BLOCK)) return;
    cetta_act_base_storage(v->base,census);cetta_act_cursor_storage(v->cursor,census);
    cetta_storage_array(census,v->previous,v->previous_capacity,1,0);
}
bool cetta_act_base_builder_info(const CettaActBaseBuilder *b,CettaActBaseBuildInfo *out) {
    if (!b || !out) return false;
    CettaActBuildInfo trie;cetta_act_builder_info(b->trie,&trie);
    *out=(CettaActBaseBuildInfo){.phase=b->finishing ? b->phase+1 : 0,.records=trie.records,
        .encoded_bytes=trie.output_bytes+b->payload_end,.assembled_bytes=b->finished ? b->info.bytes : b->output_end};
    return true;
}
void cetta_act_base_builder_storage(const CettaActBaseBuilder *b,CettaStorageCensus *census) {
    if (!b || !cetta_storage_object(census,b,sizeof(*b),CETTA_STORAGE_BLOCK)) return;
    cetta_act_builder_storage(b->trie,census);
}
