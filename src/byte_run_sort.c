#define _POSIX_C_SOURCE 200809L
#include "byte_run_sort.h"
#include "storage_census.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {PREFIX=32,FANIN=16,COLLECT,SORT,FLUSH,MERGE_INIT,MERGE_FILL,MERGE_SELECT,READY};
typedef struct {uint64_t offset,size,value;unsigned char prefix[PREFIX];} Entry;
typedef struct {Entry item;uint64_t next,end;bool live;} Head;
struct CettaByteRunSort {
    int keys,run[2],input,output;
    Entry *items;size_t capacity,length;
    uint64_t records,key_end,key_flushed,run_end,read_records,run_size[2];
    size_t key_used;
    unsigned phase;
    bool sealed,failed;
    CettaActStatus failure;
    bool building,sifting;size_t build,end,root,child;unsigned sift_phase;
    uint64_t flush_at,run_records,group_start,group_end,merge_written;
    Head heads[FANIN];unsigned head_count,scan,selected;
    struct {Entry a,b;uint64_t at;bool active;} compare;
    unsigned char key_buffer[32768],left[4096],right[4096];
};
static bool at_io(int fd,void *data,size_t size,uint64_t offset,bool write) {
    if (offset>INT64_MAX || size>(uint64_t)INT64_MAX-offset) return false;
    unsigned char *p=data;
    while (size) {
        ssize_t n=write ? pwrite(fd,p,size,(off_t)offset) : pread(fd,p,size,(off_t)offset);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return false;
        p+=n;size-=(size_t)n;offset+=(uint64_t)n;
    }
    return true;
}
static CettaActStatus fail(CettaByteRunSort *s,CettaActStatus status) {
    s->failed=true;s->failure=status;return status;
}
static bool key_flush(CettaByteRunSort *s) {
    if (!at_io(s->keys,s->key_buffer,s->key_used,s->key_flushed,true)) return false;
    s->key_flushed+=s->key_used;s->key_used=0;return true;
}
void cetta_byte_sort_close(CettaByteRunSort *s) {
    if (!s) return;
    if (s->keys>=0) close(s->keys);
    for (unsigned i=0;i<2;i++) if (s->run[i]>=0) close(s->run[i]);
    free(s->items);free(s);
}
CettaActStatus cetta_byte_sort_open(int keys,int a,int b,size_t budget,CettaByteRunSort **out) {
    if (!out) return CETTA_ACT_CORRUPT;
    *out=NULL;
    if (budget<sizeof(CettaByteRunSort)+2*sizeof(Entry)) return CETTA_ACT_LIMIT;
    int fd[3]={keys,a,b};struct stat st[3];
    for (unsigned i=0;i<3;i++) {
        if (fstat(fd[i],&st[i])) return CETTA_ACT_IO;
        int flags=fcntl(fd[i],F_GETFL);if (flags<0) return CETTA_ACT_IO;
        if ((flags&O_ACCMODE)!=O_RDWR || (flags&O_APPEND) || !S_ISREG(st[i].st_mode) || st[i].st_size) return CETTA_ACT_CORRUPT;
        for (unsigned j=0;j<i;j++) if (st[i].st_dev==st[j].st_dev && st[i].st_ino==st[j].st_ino) return CETTA_ACT_CORRUPT;
    }
    CettaByteRunSort *s=calloc(1,sizeof(*s));if (!s) return CETTA_ACT_NOMEM;
    s->keys=s->run[0]=s->run[1]=-1;s->input=0;s->output=1;
    s->capacity=(budget-sizeof(*s))/sizeof(Entry);
    s->items=malloc(s->capacity*sizeof(Entry));
    if (!s->items) {free(s);return CETTA_ACT_NOMEM;}
    s->keys=fcntl(keys,F_DUPFD_CLOEXEC,0);s->run[0]=fcntl(a,F_DUPFD_CLOEXEC,0);s->run[1]=fcntl(b,F_DUPFD_CLOEXEC,0);
    if (s->keys<0 || s->run[0]<0 || s->run[1]<0) {cetta_byte_sort_close(s);return CETTA_ACT_IO;}
    s->phase=COLLECT;*out=s;return CETTA_ACT_OK;
}
CettaActStatus cetta_byte_sort_key(CettaByteRunSort *s,CettaActBytes bytes,CettaByteSpan *out) {
    if (!s || !out || (bytes.size && !bytes.data)) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (s->sealed || bytes.size>(uint64_t)INT64_MAX-s->key_end) return fail(s,CETTA_ACT_LIMIT);
    *out=(CettaByteSpan){s->key_end,bytes.size};const unsigned char *p=bytes.data;size_t n=bytes.size;
    while (n) {
        size_t amount=sizeof(s->key_buffer)-s->key_used;if (amount>n) amount=n;
        memcpy(s->key_buffer+s->key_used,p,amount);s->key_used+=amount;s->key_end+=amount;p+=amount;n-=amount;
        if (s->key_used==sizeof(s->key_buffer) && !key_flush(s)) return fail(s,CETTA_ACT_IO);
    }
    return CETTA_ACT_OK;
}
static bool entry_span(CettaByteRunSort *s,CettaByteSpan key,uint64_t value,Entry *e) {
    if (key.offset>s->key_end || key.size>s->key_end-key.offset) return false;
    *e=(Entry){.offset=key.offset,.size=key.size,.value=value};
    size_t n=key.size<PREFIX ? (size_t)key.size : PREFIX;
    return !n || at_io(s->keys,e->prefix,n,key.offset,false);
}
static void start_sort(CettaByteRunSort *s) {
    s->phase=SORT;s->building=true;s->build=s->length/2;s->end=s->length;s->sifting=false;
}
CettaActStatus cetta_byte_sort_add(CettaByteRunSort *s,CettaByteSpan key,uint64_t value) {
    if (!s) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (s->sealed) return fail(s,CETTA_ACT_CORRUPT);
    if (s->phase!=COLLECT) return CETTA_ACT_PROGRESS;
    if (key.offset>s->key_end || key.size>s->key_end-key.offset) return fail(s,CETTA_ACT_CORRUPT);
    if (s->records>=(uint64_t)INT64_MAX/sizeof(Entry)) return fail(s,CETTA_ACT_LIMIT);
    if (!key_flush(s)) return fail(s,CETTA_ACT_IO);
    Entry entry;
    if (!entry_span(s,key,value,&entry)) return fail(s,CETTA_ACT_IO);
    s->items[s->length++]=entry;++s->records;
    if (s->length==s->capacity) start_sort(s);
    return CETTA_ACT_OK;
}
CettaActStatus cetta_byte_sort_put(CettaByteRunSort *s,CettaActBytes key,uint64_t value) {
    if (!s) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (s->sealed) return fail(s,CETTA_ACT_CORRUPT);
    if (s->phase!=COLLECT) return CETTA_ACT_PROGRESS;
    CettaByteSpan span;CettaActStatus status=cetta_byte_sort_key(s,key,&span);
    return status==CETTA_ACT_OK ? cetta_byte_sort_add(s,span,value) : status;
}
static void spend(uint64_t *work,uint64_t n) {*work=n>=*work ? 0 : *work-n;}
static CettaActStatus compare_step(CettaByteRunSort *s,const Entry *a,const Entry *b,uint64_t *work,int *order) {
    if (!s->compare.active) {s->compare.a=*a;s->compare.b=*b;s->compare.at=0;s->compare.active=true;}
    else if (s->compare.a.offset!=a->offset || s->compare.a.size!=a->size ||
             s->compare.b.offset!=b->offset || s->compare.b.size!=b->size) return fail(s,CETTA_ACT_CORRUPT);
    uint64_t n=a->size<b->size ? a->size : b->size;
    if (!s->compare.at) {
        size_t initial=n<PREFIX ? (size_t)n : PREFIX;
        int c=initial ? memcmp(a->prefix,b->prefix,initial) : 0;spend(work,initial+1);
        if (c) {*order=c;s->compare.active=false;return CETTA_ACT_OK;}
        s->compare.at=initial;
    }
    while (s->compare.at<n && *work) {
        size_t amount=n-s->compare.at<sizeof(s->left) ? (size_t)(n-s->compare.at) : sizeof(s->left);
        if (amount>*work) amount=(size_t)*work;
        if (!at_io(s->keys,s->left,amount,a->offset+s->compare.at,false) ||
            !at_io(s->keys,s->right,amount,b->offset+s->compare.at,false)) return fail(s,CETTA_ACT_IO);
        int c=memcmp(s->left,s->right,amount);s->compare.at+=amount;spend(work,(uint64_t)amount*2);
        if (c) {*order=c;s->compare.active=false;return CETTA_ACT_OK;}
    }
    if (s->compare.at<n) return CETTA_ACT_PROGRESS;
    *order=(a->size>b->size)-(a->size<b->size);s->compare.active=false;return CETTA_ACT_OK;
}
static CettaActStatus entry_compare(CettaByteRunSort *s,const Entry *a,const Entry *b,uint64_t *work,int *order) {
    CettaActStatus status=compare_step(s,a,b,work,order);
    if (status==CETTA_ACT_OK && !*order) *order=(a->value>b->value)-(a->value<b->value);
    return status;
}
static CettaActStatus sort_step(CettaByteRunSort *s,uint64_t *work) {
    while (*work) {
        if (!s->sifting) {
            if (s->building && s->build) s->root=--s->build;
            else {
                s->building=false;
                if (s->end<2) {s->phase=FLUSH;s->flush_at=0;return CETTA_ACT_OK;}
                Entry e=s->items[0];s->items[0]=s->items[s->end-1];s->items[--s->end]=e;s->root=0;
            }
            s->sifting=true;s->sift_phase=0;spend(work,1);
        }
        if (!s->sift_phase) {
            if (s->root>=s->end/2) {s->sifting=false;continue;}
            s->child=s->root*2+1;
            if (s->child+1<s->end) {
                int order;CettaActStatus status=entry_compare(s,&s->items[s->child],&s->items[s->child+1],work,&order);
                if (status!=CETTA_ACT_OK) return status;
                if (order<0) ++s->child;
            }
            s->sift_phase=1;
        }
        int order;CettaActStatus status=entry_compare(s,&s->items[s->root],&s->items[s->child],work,&order);
        if (status!=CETTA_ACT_OK) return status;
        if (order>=0) s->sifting=false;
        else {Entry e=s->items[s->root];s->items[s->root]=s->items[s->child];s->items[s->child]=e;s->root=s->child;s->sift_phase=0;}
        spend(work,1);
    }
    return CETTA_ACT_PROGRESS;
}
static CettaActStatus prepare_merge(CettaByteRunSort *s) {
    free(s->items);s->items=NULL;
    s->run_records=s->capacity;s->group_start=0;s->merge_written=0;s->input=0;s->output=1;
    if (s->records<=s->capacity) s->phase=READY;
    else {
        if (ftruncate(s->run[s->output],0)) return fail(s,CETTA_ACT_IO);
        s->run_size[s->output]=0;s->phase=MERGE_INIT;
    }
    return CETTA_ACT_OK;
}
CettaActStatus cetta_byte_sort_seal(CettaByteRunSort *s) {
    if (!s) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (s->sealed) return s->phase==READY ? CETTA_ACT_OK : CETTA_ACT_PROGRESS;
    s->sealed=true;
    if (!key_flush(s)) return fail(s,CETTA_ACT_IO);
    if (s->phase==COLLECT) {
        if (s->length) start_sort(s);
        else return prepare_merge(s);
    }
    return CETTA_ACT_PROGRESS;
}
CettaActStatus cetta_byte_sort_step(CettaByteRunSort *s,uint64_t work) {
    if (!s) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    while (work) {
        if (s->phase==READY || s->phase==COLLECT) return CETTA_ACT_OK;
        if (s->phase==SORT) {CettaActStatus status=sort_step(s,&work);if (status!=CETTA_ACT_OK) return status;}
        else if (s->phase==FLUSH) {
            uint64_t bytes=s->length*sizeof(Entry),left=bytes-s->flush_at;
            size_t n=left<32768 ? (size_t)left : 32768;if (n>work) n=(size_t)work;
            if (n && !at_io(s->run[0],(unsigned char *)s->items+(size_t)s->flush_at,n,s->run_end+s->flush_at,true)) return fail(s,CETTA_ACT_IO);
            s->flush_at+=n;s->run_size[0]=s->run_end+s->flush_at;spend(&work,n+1);
            if (s->flush_at==bytes) {
                s->run_end+=bytes;s->length=0;
                if (!s->sealed) s->phase=COLLECT;
                else {CettaActStatus status=prepare_merge(s);if (status!=CETTA_ACT_OK) return status;}
            }
        } else if (s->phase==MERGE_INIT) {
            if (s->group_start>=s->records) {
                if (s->merge_written!=s->records) return fail(s,CETTA_ACT_CORRUPT);
                int old=s->input;s->input=s->output;s->output=old;
                if (s->run_records>UINT64_MAX/FANIN) return fail(s,CETTA_ACT_LIMIT);
                s->run_records*=FANIN;s->group_start=0;s->merge_written=0;
                if (s->run_records>=s->records) {s->phase=READY;continue;}
                if (ftruncate(s->run[s->output],0)) return fail(s,CETTA_ACT_IO);
                s->run_size[s->output]=0;
            }
            memset(s->heads,0,sizeof(s->heads));s->head_count=0;s->scan=0;
            uint64_t at=s->group_start;
            while (at<s->records && s->head_count<FANIN) {
                Head *h=&s->heads[s->head_count++];h->next=at;
                h->end=s->records-at<s->run_records ? s->records : at+s->run_records;at=h->end;
            }
            s->group_end=at;s->phase=MERGE_FILL;spend(&work,1);
        } else if (s->phase==MERGE_FILL) {
            if (s->scan==s->head_count) {s->scan=0;s->selected=FANIN;s->phase=MERGE_SELECT;continue;}
            Head *h=&s->heads[s->scan++];
            if (!h->live && h->next<h->end) {
                if (!at_io(s->run[s->input],&h->item,sizeof(Entry),h->next*sizeof(Entry),false)) return fail(s,CETTA_ACT_IO);
                ++h->next;h->live=true;
            }
            spend(&work,sizeof(Entry));
        } else if (s->phase==MERGE_SELECT) {
            if (s->scan<s->head_count) {
                Head *h=&s->heads[s->scan];
                if (!h->live) {++s->scan;spend(&work,1);continue;}
                if (s->selected==FANIN) s->selected=s->scan;
                else {
                    int order;CettaActStatus status=entry_compare(s,&h->item,&s->heads[s->selected].item,&work,&order);
                    if (status!=CETTA_ACT_OK) return status;
                    if (order<0) s->selected=s->scan;
                }
                ++s->scan;spend(&work,1);
            } else if (s->selected==FANIN) {s->group_start=s->group_end;s->phase=MERGE_INIT;spend(&work,1);}
            else {
                Head *h=&s->heads[s->selected];
                if (!at_io(s->run[s->output],&h->item,sizeof(Entry),s->merge_written*sizeof(Entry),true)) return fail(s,CETTA_ACT_IO);
                ++s->merge_written;s->run_size[s->output]=s->merge_written*sizeof(Entry);h->live=false;s->scan=s->selected;s->phase=MERGE_FILL;
                spend(&work,sizeof(Entry));
            }
        } else return fail(s,CETTA_ACT_CORRUPT);
    }
    return s->phase==READY || s->phase==COLLECT ? CETTA_ACT_OK : CETTA_ACT_PROGRESS;
}
CettaActStatus cetta_byte_sort_next(CettaByteRunSort *s,CettaByteSortItem *out) {
    if (!s || !out) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (s->phase!=READY) return CETTA_ACT_PROGRESS;
    if (s->read_records==s->records) return CETTA_ACT_END;
    Entry e;
    if (!at_io(s->run[s->input],&e,sizeof(e),s->read_records*sizeof(e),false)) return fail(s,CETTA_ACT_IO);
    ++s->read_records;*out=(CettaByteSortItem){{e.offset,e.size},e.value};return CETTA_ACT_OK;
}
CettaActStatus cetta_byte_sort_read(CettaByteRunSort *s,CettaByteSpan span,uint64_t offset,
        void *bytes,size_t capacity,size_t *written) {
    if (!s || !written || (capacity && !bytes)) return CETTA_ACT_CORRUPT;
    *written=0;if (s->failed) return s->failure;
    if (span.offset>s->key_end || span.size>s->key_end-span.offset || offset>span.size) return fail(s,CETTA_ACT_CORRUPT);
    if (!key_flush(s)) return fail(s,CETTA_ACT_IO);
    size_t n=span.size-offset<capacity ? (size_t)(span.size-offset) : capacity;
    if (!at_io(s->keys,bytes,n,span.offset+offset,false)) return fail(s,CETTA_ACT_IO);
    *written=n;return CETTA_ACT_OK;
}
CettaActStatus cetta_byte_sort_compare(CettaByteRunSort *s,CettaByteSpan a,CettaByteSpan b,uint64_t work,int *order) {
    if (!s || !order || s->phase!=READY) return CETTA_ACT_CORRUPT;
    if (s->failed) return s->failure;
    if (!work) return CETTA_ACT_PROGRESS;
    Entry x,y;if (!entry_span(s,a,0,&x) || !entry_span(s,b,0,&y)) return fail(s,CETTA_ACT_IO);
    return compare_step(s,&x,&y,&work,order);
}
int cetta_byte_sort_key_fd(const CettaByteRunSort *s) {return s ? s->keys : -1;}
bool cetta_byte_sort_info(const CettaByteRunSort *s,CettaByteSortInfo *out) {
    if (!s || !out) return false;
    *out=(CettaByteSortInfo){.records=s->records,.key_bytes=s->key_end,.run_bytes=s->run_size[0]+s->run_size[1],
        .read_records=s->read_records,.heap_bytes=sizeof(*s)+(s->items ? s->capacity*sizeof(Entry) : 0),
        .capacity=s->capacity,.phase=s->phase};return true;
}
void cetta_byte_sort_storage(const CettaByteRunSort *s,CettaStorageCensus *census) {
    if (!s || !cetta_storage_object(census,s,sizeof(*s),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,s->items,s->items ? s->capacity : 0,sizeof(Entry),0);
}
