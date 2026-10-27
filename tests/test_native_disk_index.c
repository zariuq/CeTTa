#define _POSIX_C_SOURCE 200809L
#include "native_disk_index.h"
#include "native_journal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static unsigned checks;
#define check(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"disk index check failed at line %d: %s\n",__LINE__,#x); exit(1); \
} } while (0)
static ssize_t write_remaining=-1;
static size_t read_chunk;
static uint64_t read_bytes;
static int write_interrupt,read_interrupt,sync_interrupt,sync_failure=-1;
static size_t allocation_limit;
static unsigned oversized_allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t size) {
    if (allocation_limit && size>allocation_limit) { ++oversized_allocations; return NULL; }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count,size_t size) {
    if (allocation_limit && size && count>allocation_limit/size) { ++oversized_allocations; return NULL; }
    return __real_calloc(count,size);
}
void *__wrap_realloc(void *old,size_t size) {
    if (allocation_limit && size>allocation_limit) { ++oversized_allocations; return NULL; }
    return __real_realloc(old,size);
}
ssize_t __real_write(int,const void *,size_t);
ssize_t __real_pread(int,void *,size_t,off_t);
int __real_fsync(int);
ssize_t __wrap_write(int fd,const void *data,size_t size) {
    if (write_interrupt) { write_interrupt=0; errno=EINTR; return -1; }
    if (!write_remaining) { errno=ENOSPC; return -1; }
    if (write_remaining>0 && size>(size_t)write_remaining) size=(size_t)write_remaining;
    ssize_t n=__real_write(fd,data,size);
    if (n>0 && write_remaining>0) write_remaining-=n;
    return n;
}
ssize_t __wrap_pread(int fd,void *data,size_t size,off_t offset) {
    if (read_interrupt) { read_interrupt=0; errno=EINTR; return -1; }
    if (read_chunk && size>read_chunk) size=read_chunk;
    ssize_t n=__real_pread(fd,data,size,offset);
    if (n>0) read_bytes+=(uint64_t)n;
    return n;
}
int __wrap_fsync(int fd) {
    if (sync_interrupt) { sync_interrupt=0; errno=EINTR; return -1; }
    if (fd==sync_failure) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
static CettaDiskBytes bytes(const void *data,size_t size) { return (CettaDiskBytes){data,size}; }
static CettaDiskBytes text(const char *value) { return bytes(value,strlen(value)); }
static CettaDiskRoot empty(int fd) { return (CettaDiskRoot){fd,0,0,4096,4*1024*1024}; }
static void expect(const CettaDiskRoot *root,CettaDiskBytes key,CettaDiskBytes expected,bool present) {
    CettaDiskBlob blob; CettaDiskStatus status=cetta_disk_lookup(root,key,&blob);
    if (!present) { check(status==CETTA_DISK_ABSENT); return; }
    check(status==CETTA_DISK_OK && blob.size==expected.size);
    unsigned char *value=malloc(expected.size ? expected.size : 1); check(value);
    size_t count=123;
    check(cetta_disk_blob_read(root,blob,0,value,expected.size,&count)==CETTA_DISK_OK);
    check(count==expected.size && (!count || !memcmp(value,expected.data,count)));
    free(value);
}
static void set(CettaDiskRoot *root,CettaDiskBytes key,CettaDiskBytes value,bool remove) {
    CettaDiskOp op={remove,key,value}; CettaDiskRoot candidate;
    check(cetta_disk_build(root,&op,1,&candidate)==CETTA_DISK_OK); *root=candidate;
}
static int key_compare(CettaDiskBytes left,CettaDiskBytes right) {
    size_t n=left.size<right.size ? left.size : right.size;
    int order=n ? memcmp(left.data,right.data,n) : 0;
    return order ? order : left.size<right.size ? -1 : left.size>right.size;
}
typedef struct { unsigned char key[10]; size_t size; uint64_t value; bool present; } Row;
static int row_compare(const void *l,const void *r) {
    const Row *left=l,*right=r;
    return key_compare(bytes(left->key,left->size),bytes(right->key,right->size));
}
static void scan(const CettaDiskRoot *root,Row *rows,size_t count) {
    Row *ordered=malloc(count*sizeof(*ordered)); check(ordered);
    memcpy(ordered,rows,count*sizeof(*rows)); qsort(ordered,count,sizeof(*ordered),row_compare);
    CettaDiskCursor *cursor=NULL; check(cetta_disk_cursor_open(root,&cursor)==CETTA_DISK_OK);
    for (size_t i=0;i<count;++i) if (ordered[i].present) {
        CettaDiskBuffer key; CettaDiskBlob value;
        check(cetta_disk_cursor_next(cursor,&key,&value)==CETTA_DISK_OK);
        check(key.size==ordered[i].size && (!key.size || !memcmp(key.data,ordered[i].key,key.size)));
        uint64_t actual=0; size_t size=0;
        check(cetta_disk_blob_read(cetta_disk_cursor_root(cursor),value,0,&actual,sizeof(actual),&size)==CETTA_DISK_OK);
        check(size==sizeof(actual) && actual==ordered[i].value);
        cetta_disk_buffer_free(&key);
    }
    CettaDiskBuffer key; CettaDiskBlob value;
    check(cetta_disk_cursor_next(cursor,&key,&value)==CETTA_DISK_END);
    check(cetta_disk_cursor_next(cursor,&key,&value)==CETTA_DISK_END);
    cetta_disk_cursor_close(cursor); free(ordered);
}
static void range_scan(const CettaDiskRoot *root,Row *rows,size_t count,
                       CettaDiskBytes lower,bool exclusive,CettaDiskBytes upper) {
    Row *ordered=malloc(count*sizeof(*ordered)); check(ordered);
    memcpy(ordered,rows,count*sizeof(*rows)); qsort(ordered,count,sizeof(*ordered),row_compare);
    CettaDiskCursor *cursor=NULL;
    check(cetta_disk_cursor_open_range(root,&lower,exclusive,&upper,&cursor)==CETTA_DISK_OK);
    for (size_t i=0;i<count;++i) {
        CettaDiskBytes expected=bytes(ordered[i].key,ordered[i].size);
        int low=key_compare(expected,lower);
        if (!ordered[i].present || low<0 || (!low && exclusive) || key_compare(expected,upper)>=0) continue;
        CettaDiskBuffer actual; CettaDiskBlob value;
        check(cetta_disk_cursor_next(cursor,&actual,&value)==CETTA_DISK_OK);
        check(key_compare(bytes(actual.data,actual.size),expected)==0);
        cetta_disk_buffer_free(&actual);
    }
    CettaDiskBuffer actual; CettaDiskBlob value;
    check(cetta_disk_cursor_next(cursor,&actual,&value)==CETTA_DISK_END);
    cetta_disk_cursor_close(cursor); free(ordered);
}
static uint64_t seed=0x756e69636f646531ULL;
static uint64_t random_word(void) { seed^=seed<<13; seed^=seed>>7; seed^=seed<<17; return seed; }
static void map_and_snapshots(void) {
    FILE *file=tmpfile(); check(file); CettaDiskRoot root=empty(fileno(file));
    enum { N=160 }; Row rows[N]; memset(rows,0,sizeof(rows));
    /* Empty, NUL-containing, prefix-related and high-byte keys. */
    for (size_t i=1;i<N;++i) {
        rows[i].size=1+i%9;
        rows[i].key[0]=(unsigned char)i;
        for (size_t j=1;j<rows[i].size;++j) rows[i].key[j]=(unsigned char)random_word();
    }
    rows[2].key[0]=1; rows[2].key[1]=0; rows[2].size=2;
    rows[3].key[0]=1; rows[3].key[1]=0; rows[3].key[2]=255; rows[3].size=3;
    rows[1].key[0]=1; rows[1].size=1;
    CettaDiskRoot saved=root; Row saved_rows[N]; memcpy(saved_rows,rows,sizeof(rows));
    for (size_t i=0;i<2500;++i) {
        size_t k=(size_t)(random_word()%N); bool removing=(random_word()%4)==0;
        rows[k].value=random_word(); rows[k].present=!removing;
        set(&root,bytes(rows[k].key,rows[k].size),bytes(&rows[k].value,sizeof(rows[k].value)),removing);
        expect(&root,bytes(rows[k].key,rows[k].size),bytes(&rows[k].value,sizeof(rows[k].value)),!removing);
        if (i==700) { saved=root; memcpy(saved_rows,rows,sizeof(rows)); }
        if (i%137==0) {
            scan(&root,rows,N);
            size_t low=(size_t)(random_word()%N),high=(size_t)(random_word()%N);
            range_scan(&root,rows,N,bytes(rows[low].key,rows[low].size),
                       (i&1)!=0,bytes(rows[high].key,rows[high].size));
        }
    }
    scan(&root,rows,N); scan(&saved,saved_rows,N);
    FILE *copy=tmpfile(); check(copy); CettaDiskRoot compacted;
    check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_OK);
    check(compacted.end<root.end); scan(&compacted,rows,N);
    check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_INVALID);
    scan(&root,rows,N); scan(&saved,saved_rows,N);
    fclose(copy); fclose(file);
}
static void batches_and_faults(void) {
    FILE *file=tmpfile(); check(file); CettaDiskRoot root=empty(fileno(file));
    set(&root,text("one"),text("original"),false);
    CettaDiskOp ops[]={{false,text("one"),text("replacement")},
        {false,text("two"),text("second")},{true,text("one"),{0}},
        {false,text("one"),text("final")}};
    CettaDiskRoot candidate;
    check(cetta_disk_build(&root,ops,4,&candidate)==CETTA_DISK_OK);
    expect(&candidate,text("one"),text("final"),true);
    expect(&candidate,text("two"),text("second"),true);
    expect(&root,text("one"),text("original"),true);
    expect(&root,text("two"),text(""),false);
    ssize_t required=(ssize_t)(candidate.end-root.end);
    for (ssize_t cut=0;cut<required;++cut) {
        write_remaining=cut;
        CettaDiskRoot failed;
        CettaDiskStatus status=cetta_disk_build(&root,ops,4,&failed);
        write_remaining=-1;
        check(status==CETTA_DISK_IO && failed.fd==-1);
        expect(&root,text("one"),text("original"),true);
        expect(&root,text("two"),text(""),false);
    }
    read_chunk=1; read_interrupt=1; write_interrupt=1;
    check(cetta_disk_build(&root,ops,4,&candidate)==CETTA_DISK_OK);
    expect(&candidate,text("one"),text("final"),true); read_chunk=0;
    sync_interrupt=1; check(cetta_disk_sync(&candidate)==CETTA_DISK_OK);
    sync_failure=root.fd; check(cetta_disk_sync(&candidate)==CETTA_DISK_IO); sync_failure=-1;
    expect(&root,text("one"),text("original"),true);
    CettaDiskOp invalid={false,{NULL,2},text("invalid")};
    check(cetta_disk_build(&root,&invalid,1,&candidate)==CETTA_DISK_INVALID);
    expect(&root,text("one"),text("original"),true);
    CettaDiskOp alias={false,text("alias"),text("in place")};
    write_remaining=5;
    check(cetta_disk_build(&root,&alias,1,&root)==CETTA_DISK_IO);
    write_remaining=-1; expect(&root,text("one"),text("original"),true);
    check(cetta_disk_build(&root,&alias,1,&root)==CETTA_DISK_OK);
    expect(&root,text("alias"),text("in place"),true);
    fclose(file);
}
static void large_body_and_reader_ownership(void) {
    FILE *file=tmpfile(),*copy=tmpfile(); check(file && copy);
    CettaDiskRoot root=empty(fileno(file));
    size_t size=2*1024*1024+17; unsigned char *data=malloc(size); check(data);
    for (size_t i=0;i<size;++i) data[i]=(unsigned char)(i*173+7);
    /* An accidental body-sized temporary fails safely rather than exhausting
     * memory, and makes the real large-value/compaction checks fail. */
    allocation_limit=65536;
    set(&root,text("large"),bytes(data,size),false);
    CettaDiskBlob body; check(cetta_disk_lookup(&root,text("large"),&body)==CETTA_DISK_OK);
    uint64_t before_read=read_bytes;
    CettaDiskBlobReader *reader=NULL;
    check(cetta_disk_blob_reader_open(&root,body,&reader)==CETTA_DISK_OK);
    unsigned char chunk[4096]; size_t received=0;
    for (uint64_t offset=0;offset<size;offset+=received) {
        check(cetta_disk_blob_reader_read(reader,offset,chunk,sizeof(chunk),&received)==CETTA_DISK_OK);
        check(received && !memcmp(chunk,data+(size_t)offset,received));
    }
    check(read_bytes-before_read<=(uint64_t)size*2+1024);
    check(cetta_disk_blob_reader_read(reader,0,chunk,sizeof(chunk),&received)==CETTA_DISK_OK);
    check(received==sizeof(chunk) && !memcmp(chunk,data,received));
    unsigned char range[101]; size_t n;
    check(cetta_disk_blob_read(&root,body,32761,range,sizeof(range),&n)==CETTA_DISK_OK);
    check(n==sizeof(range) && !memcmp(range,data+32761,n));
    check(cetta_disk_blob_read(&root,body,size-3,range,sizeof(range),&n)==CETTA_DISK_OK);
    check(n==3 && !memcmp(range,data+size-3,n));
    check(cetta_disk_blob_read(&root,body,size,range,sizeof(range),&n)==CETTA_DISK_OK && !n);
    check(cetta_disk_blob_read(&root,body,size+1,range,sizeof(range),&n)==CETTA_DISK_INVALID && !n);
    CettaDiskCursor *cursor; check(cetta_disk_cursor_open(&root,&cursor)==CETTA_DISK_OK);
    CettaDiskRoot compacted;
    check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_OK);
    check(cetta_disk_sync(&compacted)==CETTA_DISK_OK);
    set(&root,text("large"),text("new"),false);
    fclose(file); /* The cursor owns its old segment, even after owner close. */
    check(cetta_disk_blob_reader_read(reader,123,chunk,sizeof(chunk),&received)==CETTA_DISK_OK);
    check(received==sizeof(chunk) && !memcmp(chunk,data+123,received));
    cetta_disk_blob_reader_close(reader);
    CettaDiskBuffer key;
    check(cetta_disk_cursor_next(cursor,&key,&body)==CETTA_DISK_OK && key.size==5);
    check(cetta_disk_blob_read(cetta_disk_cursor_root(cursor),body,100,range,sizeof(range),&n)==CETTA_DISK_OK);
    check(n==sizeof(range) && !memcmp(range,data+100,n));
    cetta_disk_buffer_free(&key); cetta_disk_cursor_close(cursor);
    check(cetta_disk_lookup(&compacted,text("large"),&body)==CETTA_DISK_OK);
    check(cetta_disk_blob_read(&compacted,body,100,range,sizeof(range),&n)==CETTA_DISK_OK);
    check(n==sizeof(range) && !memcmp(range,data+100,n));
    allocation_limit=0; check(!oversized_allocations);
    free(data); fclose(copy);
}
static void corruption(void) {
    FILE *file=tmpfile(),*copy=tmpfile(); check(file && copy);
    CettaDiskRoot root=empty(fileno(file)); set(&root,text("key"),text("value"),false);
    CettaDiskBlob blob; check(cetta_disk_lookup(&root,text("key"),&blob)==CETTA_DISK_OK);
    unsigned char previous;
    off_t offset=(off_t)(blob.reference-1+72+2);
    check(pread(root.fd,&previous,1,offset)==1);
    unsigned char bad=previous^32; check(pwrite(root.fd,&bad,1,offset)==1);
    unsigned char output[20]; size_t written=1;
    check(cetta_disk_blob_read(&root,blob,0,output,sizeof(output),&written)==CETTA_DISK_CORRUPT && !written);
    CettaDiskRoot compacted;
    check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_CORRUPT && compacted.fd==-1);
    check(pwrite(root.fd,&previous,1,offset)==1);
    expect(&root,text("key"),text("value"),true);
    root.end--; check(cetta_disk_lookup(&root,text("key"),&blob)==CETTA_DISK_CORRUPT);
    fclose(file); fclose(copy);
}
static void put64(unsigned char *data,uint64_t value) {
    for (size_t i=8;i>0;--i) { data[i-1]=(unsigned char)value;value>>=8; }
}
static uint64_t crafted_frame(CettaDiskRoot *root,const unsigned char *data,size_t size) {
    uint64_t reference=root->end+1;
    check(cetta_journal_append(root->fd,data,size,false,&root->end)==CETTA_JOURNAL_OK);
    return reference;
}
static uint64_t crafted_leaf(CettaDiskRoot *root,unsigned char key) {
    const unsigned char value[]={3,'v'};
    uint64_t body=crafted_frame(root,value,sizeof(value));
    unsigned char leaf[14]={2};leaf[4]=1;put64(leaf+5,body);leaf[13]=key;
    return crafted_frame(root,leaf,sizeof(leaf));
}
static uint64_t crafted_branch(CettaDiskRoot *root,uint64_t bit,uint64_t left,uint64_t right) {
    unsigned char branch[25]={1};put64(branch+1,bit);put64(branch+9,left);put64(branch+17,right);
    return crafted_frame(root,branch,sizeof(branch));
}
static void structural_corruption(void) {
    for (int kind=0;kind<4;++kind) {
        FILE *file=tmpfile(),*copy=tmpfile();check(file && copy);
        CettaDiskRoot root=empty(fileno(file));
        uint64_t a=crafted_leaf(&root,kind==3 ? 'y' : 'a'),b=crafted_leaf(&root,'b');
        if (kind==0) root.root=crafted_branch(&root,8,b,a); /* Wrong sides. */
        else if (kind==1) {
            uint64_t x=crafted_branch(&root,8,a,b),y=crafted_branch(&root,8,a,b);
            root.root=crafted_branch(&root,7,x,y); /* Shared subtrees. */
        } else if (kind==2) {
            for (int bit=15;bit>=0;--bit) {
                uint64_t x=crafted_branch(&root,(uint64_t)bit,a,b);
                uint64_t y=crafted_branch(&root,(uint64_t)bit,b,a);a=x;b=y;
            }
            root.root=a; /* A small DAG formerly produced 65,536 rows. */
        } else root.root=crafted_branch(&root,8,a,b); /* Wrong compressed prefix. */
        CettaDiskCursor *cursor=NULL;check(cetta_disk_cursor_open(&root,&cursor)==CETTA_DISK_OK);
        CettaDiskStatus status=CETTA_DISK_OK;size_t rows=0;
        while (rows<4) {
            CettaDiskBuffer key={0};CettaDiskBlob value;
            status=cetta_disk_cursor_next(cursor,&key,&value);cetta_disk_buffer_free(&key);
            if (status!=CETTA_DISK_OK) break;
            ++rows;
        }
        check(status==CETTA_DISK_CORRUPT && rows<=1);cetta_disk_cursor_close(cursor);
        if (!kind) {
            CettaDiskBlob value;
            check(cetta_disk_lookup(&root,text("a"),&value)==CETTA_DISK_CORRUPT);
            CettaDiskRoot candidate;CettaDiskOp op={false,text("c"),text("new")};
            check(cetta_disk_build(&root,&op,1,&candidate)==CETTA_DISK_CORRUPT);
            CettaDiskBytes lower=text("a");
            check(cetta_disk_cursor_open_range(&root,&lower,false,NULL,&cursor)==CETTA_DISK_CORRUPT && !cursor);
        }
        CettaDiskRoot compacted;write_remaining=65536;
        check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_CORRUPT && compacted.fd==-1);
        write_remaining=-1;fclose(file);fclose(copy);
    }
}
static void root_record(const CettaDiskRoot *root,unsigned char record[16]) {
    uint64_t values[]={root->root,root->end};
    for (size_t i=0;i<2;++i) for (size_t j=8;j>0;--j) {
        record[i*8+j-1]=(unsigned char)values[i]; values[i]>>=8;
    }
}
static uint64_t read_number(const unsigned char *data) {
    uint64_t value=0;
    for (size_t i=0;i<8;++i) value=(value<<8)|data[i];
    return value;
}
static CettaDiskRoot recover_root(CettaDiskRoot template,int journal) {
    struct stat st; check(!fstat(journal,&st));
    uint64_t offset=0;
    while (offset<(uint64_t)st.st_size) {
        CettaJournalRecord row={0};
        CettaJournalStatus status=cetta_journal_read(journal,offset,(uint64_t)st.st_size,16,&row);
        if (status==CETTA_JOURNAL_TORN) break;
        check(status==CETTA_JOURNAL_OK && row.size==16);
        template.root=read_number(row.data); template.end=read_number(row.data+8);
        offset=row.next; cetta_journal_record_free(&row);
    }
    return template;
}
/* The real shared journal publishes a complete root after segment sync. This
 * fixture exercises that boundary; the service adapter remains separate. */
static void root_publication(void) {
    FILE *file=tmpfile(),*journal=tmpfile(); check(file && journal);
    CettaDiskRoot old=empty(fileno(file)); set(&old,text("receipt"),text("before"),false);
    check(cetta_disk_sync(&old)==CETTA_DISK_OK);
    unsigned char record[16]; root_record(&old,record); uint64_t written=0;
    check(cetta_journal_append(fileno(journal),record,sizeof(record),true,&written)==CETTA_JOURNAL_OK);
    CettaDiskOp ops[]={{false,text("receipt"),text("completed")},
        {false,text("usage"),text("one")},{false,text("answer"),text("retained")}};
    CettaDiskRoot candidate; check(cetta_disk_build(&old,ops,3,&candidate)==CETTA_DISK_OK);
    sync_failure=fileno(file);
    check(cetta_disk_sync(&candidate)==CETTA_DISK_IO); sync_failure=-1;
    check(candidate.fd==-1);
    check(cetta_disk_sync(&candidate)==CETTA_DISK_INVALID);
    CettaDiskRoot refused;
    check(cetta_disk_build(&candidate,ops,3,&refused)==CETTA_DISK_INVALID);
    CettaDiskRoot recovered=recover_root(old,fileno(journal));
    expect(&recovered,text("receipt"),text("before"),true);
    expect(&recovered,text("usage"),text(""),false);
    /* Model lost unsynchronized writeback, not merely one reported error.
     * The next candidate must rewrite from the acknowledged root. */
    check(!ftruncate(fileno(file),(off_t)old.end));
    check(cetta_disk_build(&old,ops,3,&candidate)==CETTA_DISK_OK);
    check(cetta_disk_sync(&candidate)==CETTA_DISK_OK); root_record(&candidate,record);
    uint64_t previous=written;
    for (ssize_t cut=0;cut<88;++cut) {
        check(!ftruncate(fileno(journal),(off_t)previous));
        check(lseek(fileno(journal),(off_t)previous,SEEK_SET)==(off_t)previous);
        write_remaining=cut;
        check(cetta_journal_append(fileno(journal),record,sizeof(record),true,&written)==CETTA_JOURNAL_UNKNOWN);
        write_remaining=-1; check(written==previous);
        recovered=recover_root(old,fileno(journal));
        expect(&recovered,text("receipt"),text("before"),true);
        expect(&recovered,text("usage"),text(""),false);
        expect(&recovered,text("answer"),text(""),false);
    }
    check(!ftruncate(fileno(journal),(off_t)previous));
    check(lseek(fileno(journal),(off_t)previous,SEEK_SET)==(off_t)previous);
    sync_failure=fileno(journal);
    check(cetta_journal_append(fileno(journal),record,sizeof(record),true,&written)==CETTA_JOURNAL_UNKNOWN);
    sync_failure=-1;
    /* Readback can resolve an uncertain local acknowledgement. Never turn
     * this into another external request. All three records are visible. */
    recovered=recover_root(old,fileno(journal));
    expect(&recovered,text("receipt"),text("completed"),true);
    expect(&recovered,text("usage"),text("one"),true);
    expect(&recovered,text("answer"),text("retained"),true);
    fclose(file); fclose(journal);
}
static double seconds(void) {
    struct timespec time; check(!clock_gettime(CLOCK_MONOTONIC,&time));
    return (double)time.tv_sec+(double)time.tv_nsec/1e9;
}
static int double_compare(const void *left,const void *right) {
    double l=*(const double *)left,r=*(const double *)right;
    return l<r ? -1 : l>r;
}
static void bulk(size_t count) {
    FILE *file=tmpfile(),*copy=tmpfile(); check(file && copy);
    CettaDiskRoot root=empty(fileno(file)); unsigned char key[8],value[128];
    memset(value,42,sizeof(value)); double start=seconds();
    for (size_t i=0;i<count;++i) {
        uint64_t identity=i;
        for (size_t j=8;j>0;--j) { key[j-1]=(unsigned char)identity; identity>>=8; }
        set(&root,bytes(key,sizeof(key)),bytes(value,sizeof(value)),false);
    }
    check(cetta_disk_sync(&root)==CETTA_DISK_OK); double build=seconds()-start;
    double lookups[1000];
    for (size_t i=0;i<1000;++i) {
        uint64_t identity=random_word()%count;
        for (size_t j=8;j>0;--j) { key[j-1]=(unsigned char)identity; identity>>=8; }
        start=seconds(); expect(&root,bytes(key,sizeof(key)),bytes(value,sizeof(value)),true);
        lookups[i]=(seconds()-start)*1e6;
    }
    qsort(lookups,1000,sizeof(*lookups),double_compare);
    CettaDiskCursor *cursor; check(cetta_disk_cursor_open(&root,&cursor)==CETTA_DISK_OK);
    size_t total=0; CettaDiskStatus status; CettaDiskBuffer found; CettaDiskBlob body;
    start=seconds();
    while ((status=cetta_disk_cursor_next(cursor,&found,&body))==CETTA_DISK_OK) {
        check(found.size==8 && body.size==sizeof(value));
        uint64_t identity=0;
        for (size_t j=0;j<found.size;++j) identity=(identity<<8)|found.data[j];
        check(identity==total++); cetta_disk_buffer_free(&found);
    }
    check(status==CETTA_DISK_END && total==count); double scan_time=seconds()-start;
    cetta_disk_cursor_close(cursor);
    start=seconds(); CettaDiskRoot compacted;
    check(cetta_disk_compact(&root,fileno(copy),&compacted)==CETTA_DISK_OK);
    check(cetta_disk_sync(&compacted)==CETTA_DISK_OK); double compact_time=seconds()-start;
    struct rusage usage; check(!getrusage(RUSAGE_SELF,&usage));
    printf("{\"rows\":%zu,\"body_bytes\":%zu,\"build_seconds\":%.6f,\"scan_seconds\":%.6f,"
           "\"compact_seconds\":%.6f,\"warm_lookup_p50_us\":%.3f,\"warm_lookup_p95_us\":%.3f,"
           "\"segment_bytes\":%llu,\"compact_bytes\":%llu,\"ru_maxrss\":%ld}\n",
        count,sizeof(value),build,scan_time,compact_time,lookups[500],lookups[950],(unsigned long long)root.end,
        (unsigned long long)compacted.end,usage.ru_maxrss);
    fclose(file); fclose(copy);
}
int main(int argc,char **argv) {
    if (argc==3 && !strcmp(argv[1],"--bulk")) {
        char *end=NULL; unsigned long count=strtoul(argv[2],&end,10);
        check(end && !*end && count>0); bulk((size_t)count); return 0;
    }
    check(argc==1);
    map_and_snapshots(); batches_and_faults(); large_body_and_reader_ownership(); corruption();
    structural_corruption();root_publication();
    printf("native disk index: %u checks passed\n",checks); return 0;
}
