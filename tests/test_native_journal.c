#define _POSIX_C_SOURCE 200809L
#include "native_journal.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The journal is exercised through real files and injected syscall failures.
 * No database, model provider or live service is involved. */
static unsigned checks;
#define check(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"journal check failed at line %d: %s\n",__LINE__,#x); abort(); \
} } while (0)

static int write_interrupt, read_interrupt, sync_interrupt, rename_failure;
static int sync_failure_fd = -1;
static ssize_t write_remaining = -1;
static size_t read_chunk;
ssize_t __real_write(int,const void *,size_t);
ssize_t __real_pread(int,void *,size_t,off_t);
int __real_fsync(int);
int __real_renameat(int,const char *,int,const char *);

ssize_t __wrap_write(int fd,const void *data,size_t size) {
    if (write_interrupt) { write_interrupt=0; errno=EINTR; return -1; }
    if (write_remaining==0) { errno=EIO; return -1; }
    if (write_remaining>0 && size>(size_t)write_remaining) size=(size_t)write_remaining;
    ssize_t n=__real_write(fd,data,size);
    if (n>0 && write_remaining>0) write_remaining-=n;
    return n;
}
ssize_t __wrap_pread(int fd,void *data,size_t size,off_t offset) {
    if (read_interrupt) { read_interrupt=0; errno=EINTR; return -1; }
    if (read_chunk && size>read_chunk) size=read_chunk;
    return __real_pread(fd,data,size,offset);
}
int __wrap_fsync(int fd) {
    if (sync_interrupt) { sync_interrupt=0; errno=EINTR; return -1; }
    if (fd==sync_failure_fd) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_renameat(int a,const char *x,int b,const char *y) {
    if (rename_failure) { errno=EIO; return -1; }
    return __real_renameat(a,x,b,y);
}

static uint64_t length(int fd) {
    struct stat st; check(!fstat(fd,&st)); return (uint64_t)st.st_size;
}
static void empty(int fd) {
    check(!ftruncate(fd,0)); check(lseek(fd,0,SEEK_SET)==0);
}
static void expect(int fd,uint64_t offset,uint64_t end,const void *data,size_t size) {
    CettaJournalRecord record={0};
    check(cetta_journal_read(fd,offset,end,4096,&record)==CETTA_JOURNAL_OK);
    check(record.size==size && !memcmp(record.data,data,size));
    check(record.next==offset+72+size);
    cetta_journal_record_free(&record);
    check(!record.data && !record.size && !record.next);
}
static bool replacement(int fd,uint64_t *bytes,void *context) {
    const char *text=context;
    return cetta_journal_append(fd,(const unsigned char *)text,strlen(text),false,bytes)==CETTA_JOURNAL_OK;
}
static bool incomplete_checkpoint(int fd,uint64_t *bytes,void *context) {
    check(replacement(fd,bytes,context)); return false;
}

static void framing(int fd) {
    empty(fd);
    uint64_t bytes=0;
    check(cetta_journal_append(fd,(const unsigned char *)"abc",3,true,&bytes)==CETTA_JOURNAL_OK);
    check(bytes==75 && length(fd)==75);
    unsigned char expected[75]={'C','S','L','1',0,0,0,3};
    memcpy(expected+8,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",64);
    memcpy(expected+72,"abc",3);
    unsigned char actual[75];
    check(pread(fd,actual,sizeof(actual),0)==(ssize_t)sizeof(actual));
    check(!memcmp(actual,expected,sizeof(actual)));
    expect(fd,0,bytes,"abc",3);

    /* The same payload twice remains two frames, with distinct positions. */
    check(cetta_journal_append(fd,(const unsigned char *)"abc",3,false,&bytes)==CETTA_JOURNAL_OK);
    expect(fd,75,bytes,"abc",3);
    CettaJournalRecord record={0};
    check(cetta_journal_read(fd,bytes,bytes,4096,&record)==CETTA_JOURNAL_END);
    for (uint64_t cut=1;cut<75;++cut)
        check(cetta_journal_read(fd,0,cut,4096,&record)==CETTA_JOURNAL_TORN);
    for (uint64_t cut=76;cut<150;++cut)
        check(cetta_journal_read(fd,75,cut,4096,&record)==CETTA_JOURNAL_TORN);
    check(cetta_journal_read(fd,0,bytes,2,&record)==CETTA_JOURNAL_CORRUPT);

    /* A complete damaged frame is not a disposable incomplete tail. */
    check(pwrite(fd,"x",1,74)==1);
    check(cetta_journal_read(fd,0,bytes,4096,&record)==CETTA_JOURNAL_CORRUPT);
    check(pwrite(fd,"c",1,74)==1);
    check(pwrite(fd,"X",1,0)==1);
    check(cetta_journal_read(fd,0,bytes,4096,&record)==CETTA_JOURNAL_CORRUPT);
    check(pwrite(fd,"C",1,0)==1);

    unsigned char binary[]={0,255,0,10,13,1};
    check(cetta_journal_append(fd,binary,sizeof(binary),false,&bytes)==CETTA_JOURNAL_OK);
    expect(fd,150,bytes,binary,sizeof(binary));
    read_interrupt=1; read_chunk=1;
    expect(fd,150,bytes,binary,sizeof(binary));
    check(!read_interrupt); read_chunk=0;
}

static void interrupted_writes(int fd) {
    empty(fd); uint64_t bytes=0;
    write_interrupt=1; sync_interrupt=1;
    check(cetta_journal_append(fd,(const unsigned char *)"complete",8,true,&bytes)==CETTA_JOURNAL_OK);
    check(!write_interrupt && !sync_interrupt);
    expect(fd,0,bytes,"complete",8);

    uint64_t before=bytes;
    write_remaining=73;
    check(cetta_journal_append(fd,(const unsigned char *)"partial",7,true,&bytes)==CETTA_JOURNAL_UNKNOWN);
    write_remaining=-1;
    check(bytes==before && length(fd)==before+73);
    CettaJournalRecord record={0};
    check(cetta_journal_read(fd,before,length(fd),4096,&record)==CETTA_JOURNAL_TORN);
    check(cetta_journal_trim_tail(fd,before)==CETTA_JOURNAL_OK);
    check(length(fd)==before);
    check(lseek(fd,(off_t)before,SEEK_SET)==(off_t)before);

    /* A failed sync is uncertain even when readback sees the full record. */
    sync_failure_fd=fd;
    check(cetta_journal_append(fd,(const unsigned char *)"retained",8,true,&bytes)==CETTA_JOURNAL_UNKNOWN);
    sync_failure_fd=-1;
    check(bytes==before);
    expect(fd,before,length(fd),"retained",8);
}

static void checkpointing(int dir,int *fd) {
    empty(*fd); uint64_t bytes=0;
    check(replacement(*fd,&bytes,"old"));
    int old=*fd;
    check(cetta_journal_checkpoint(dir,"journal.csl","checkpoint.new",incomplete_checkpoint,
                                  "incomplete",fd,&bytes)==CETTA_JOURNAL_IO);
    check(*fd==old && bytes==75);
    expect(*fd,0,length(*fd),"old",3);
    check(faccessat(dir,"checkpoint.new",F_OK,0)<0 && errno==ENOENT);

    rename_failure=1;
    check(cetta_journal_checkpoint(dir,"journal.csl","checkpoint.new",replacement,
                                  "new",fd,&bytes)==CETTA_JOURNAL_UNKNOWN);
    rename_failure=0;
    check(*fd==old);
    expect(*fd,0,length(*fd),"old",3);

    sync_failure_fd=dir;
    check(cetta_journal_checkpoint(dir,"journal.csl","checkpoint.new",replacement,
                                  "published",fd,&bytes)==CETTA_JOURNAL_UNKNOWN);
    sync_failure_fd=-1;
    check(*fd==old && bytes==75);
    /* The name changed, while the old descriptor still names the old inode.
     * A caller must recover, not treat this UNKNOWN as a rolled-back write. */
    int recovered=openat(dir,"journal.csl",O_RDWR|O_CLOEXEC);
    check(recovered>=0);
    expect(recovered,0,length(recovered),"published",9);
    close(*fd); *fd=recovered;

    check(cetta_journal_checkpoint(dir,"journal.csl","checkpoint.new",replacement,
                                  "final",fd,&bytes)==CETTA_JOURNAL_OK);
    check(bytes==77);
    expect(*fd,0,length(*fd),"final",5);
    check(cetta_journal_checkpoint(dir,"journal.csl","journal.csl",replacement,
                                  "bad",fd,&bytes)==CETTA_JOURNAL_INVALID);
    expect(*fd,0,length(*fd),"final",5);
}

static void streaming_frames(int fd) {
    empty(fd); uint64_t bytes=0;
    CettaJournalPart parts[]={{(const unsigned char *)"a",1},{NULL,0},
        {(const unsigned char *)"bc",2}};
    check(cetta_journal_append_parts(fd,parts,3,false,&bytes)==CETTA_JOURNAL_OK);
    expect(fd,0,bytes,"abc",3);
    CettaJournalPart invalid[]={{NULL,1}};
    check(cetta_journal_append_parts(fd,invalid,1,false,&bytes)==CETTA_JOURNAL_INVALID);
    check(bytes==75 && length(fd)==75);
    CettaJournalPart oversized[]={{(const unsigned char *)"",UINT32_MAX},
        {(const unsigned char *)"x",1}};
    check(cetta_journal_append_parts(fd,oversized,2,false,&bytes)==CETTA_JOURNAL_INVALID);
    check(bytes==75 && length(fd)==75);
    check(cetta_journal_append_parts(fd,NULL,0,false,&bytes)==CETTA_JOURNAL_OK);
    expect(fd,75,bytes,"",0);
    FILE *copy=tmpfile(); check(copy); int dest=fileno(copy);
    uint64_t written=0;
    read_chunk=1; read_interrupt=1; write_interrupt=1;
    check(cetta_journal_copy(fd,0,bytes,4096,dest,&written)==CETTA_JOURNAL_OK);
    read_chunk=0;
    expect(dest,0,written,"abc",3);
    check(cetta_journal_copy(fd,75,bytes,4096,dest,&written)==CETTA_JOURNAL_OK);
    expect(dest,75,written,"",0);
    check(cetta_journal_copy(fd,bytes,bytes,4096,dest,&written)==CETTA_JOURNAL_END);
    for (ssize_t cut=0;cut<75;++cut) {
        empty(dest); written=0; write_remaining=cut;
        check(cetta_journal_copy(fd,0,bytes,4096,dest,&written)==CETTA_JOURNAL_UNKNOWN);
        write_remaining=-1; check(!written && length(dest)==(uint64_t)cut);
    }
    empty(dest); written=0;
    check(cetta_journal_copy(fd,0,74,4096,dest,&written)==CETTA_JOURNAL_TORN);
    check(!written && !length(dest));
    check(cetta_journal_copy(fd,0,bytes,2,dest,&written)==CETTA_JOURNAL_CORRUPT);
    check(!written && !length(dest));
    check(pwrite(fd,"x",1,74)==1);
    check(cetta_journal_copy(fd,0,bytes,4096,dest,&written)==CETTA_JOURNAL_CORRUPT);
    check(!written); /* Destination is an unpublished, unusable candidate. */
    fclose(copy);
}

int main(void) {
    char directory[]="runtime/native-journal-test.XXXXXX";
    check(mkdtemp(directory)!=NULL);
    int dir=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    check(dir>=0);
    int fd=openat(dir,"journal.csl",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    check(fd>=0);
    framing(fd);
    interrupted_writes(fd);
    streaming_frames(fd);
    checkpointing(dir,&fd);
    close(fd);
    check(!unlinkat(dir,"journal.csl",0));
    close(dir);
    check(!rmdir(directory));
    printf("native journal: %u checks passed\n",checks);
    return 0;
}
