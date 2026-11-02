#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include "native_act_base.h"
#include "native_journal.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
static CettaDiskBytes text(const char *s) {return (CettaDiskBytes){(const unsigned char *)s,strlen(s)};}
static void expect(const CettaDiskStoreView *view,const char *key,const char *value) {
    CettaDiskStoreBlob blob;CettaDiskStatus s=cetta_disk_store_lookup(view,text(key),&blob);
    if (!value) {assert(s==CETTA_DISK_ABSENT);return;}
    assert(s==CETTA_DISK_OK && blob.size==strlen(value));unsigned char bytes[256];size_t n=0;
    assert(cetta_disk_store_blob_read(view,blob,0,bytes,sizeof(bytes),&n)==CETTA_DISK_OK);
    assert(n==strlen(value) && !memcmp(bytes,value,n));
}
static void clear(const char *path) {
    DIR *d=opendir(path);assert(d);struct dirent *entry;
    int directory=dirfd(d);
    while ((entry=readdir(d))) if (strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))
        assert(!unlinkat(directory,entry->d_name,0));
    closedir(d);assert(!rmdir(path));
}
static CettaDiskStoreView *snap(CettaDiskStore *store) {
    CettaDiskStoreView *view=NULL;assert(cetta_disk_store_snapshot(store,&view)==CETTA_STORE_OK);return view;
}
static void put64(unsigned char *p,uint64_t value) {
    for (unsigned i=8;i>0;i--) {p[i-1]=(unsigned char)value;value>>=8;}
}
static bool fixture_meta(int fd,uint64_t *written,void *bytes) {
    return cetta_journal_append(fd,bytes,196,false,written)==CETTA_JOURNAL_OK;
}
static void base_fixture(const char *path) {
    int directory=open(path,O_RDONLY|O_DIRECTORY);assert(directory>=0);
    unsigned char generation[16]={42};char name[45]="base-";
    for (unsigned i=0;i<16;i++) snprintf(name+5+2*i,3,"%02x",generation[i]);
    memcpy(name+37,".cbase",7);
    int fd[5];fd[0]=openat(directory,name,O_RDWR|O_CREAT|O_EXCL,0600);assert(fd[0]>=0);
    for (unsigned i=1;i<5;i++) {char scratch[64];snprintf(scratch,sizeof(scratch),"runtime/act-fixture-%u-XXXXXX",i);fd[i]=mkstemp(scratch);assert(fd[i]>=0);assert(!unlink(scratch));}
    CettaActBaseBuilder *b=NULL;CettaDiskStoreLimits limits=cetta_disk_store_limits();
    assert(cetta_act_base_builder_open(fd[0],fd[1],fd[2],fd[3],fd[4],256*1024,generation,
        (CettaActBaseLimits){limits.key_bytes,limits.value_bytes},&b)==CETTA_ACT_OK);
    const char *keys[]={"a","b","c"},*values[]={"base-a","base-b","base-c"};
    for (unsigned i=0;i<3;i++) {
        assert(cetta_act_base_value_begin(b,strlen(values[i]))==CETTA_ACT_OK);
        assert(cetta_act_base_value_write(b,(CettaActBytes){(const unsigned char *)values[i],strlen(values[i])})==CETTA_ACT_OK);
        assert(cetta_act_base_key(b,(CettaActBytes){(const unsigned char *)keys[i],1})==CETTA_ACT_OK);
    }
    CettaActBaseInfo info;assert(cetta_act_base_builder_finish(b,&info)==CETTA_ACT_OK);
    cetta_act_base_builder_close(b);assert(!fsync(fd[0]) && !fsync(directory));
    for (unsigned i=0;i<5;i++) close(fd[i]);
    int manifest=openat(directory,"root.csl",O_RDWR);assert(manifest>=0);struct stat st;assert(!fstat(manifest,&st));
    CettaJournalRecord record={0};assert(cetta_journal_read(manifest,0,(uint64_t)st.st_size,196,&record)==CETTA_JOURNAL_OK);
    assert(record.size==196 && !memcmp(record.data,"CDS3",4));
    memcpy(record.data+84,generation,16);put64(record.data+100,info.bytes);memcpy(record.data+108,info.digest,32);
    assert(cetta_journal_checkpoint(directory,"root.csl","root.next",fixture_meta,record.data,&manifest,NULL)==CETTA_JOURNAL_OK);
    cetta_journal_record_free(&record);close(manifest);close(directory);
}
static void merged(const char *path) {
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open_created_act(path,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *view=snap(store);assert(cetta_disk_store_view_format(view)==3);
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);base_fixture(path);
    assert(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OK);view=snap(store);
    expect(view,"a","base-a");expect(view,"b","base-b");expect(view,"c","base-c");
    CettaDiskOp ops[]={ {.key=text("a"),.value=text("active")}, {.remove=true,.key=text("b")}, {.key=text("d"),.value=text("")} };
    CettaDiskStoreView *next=NULL;assert(cetta_disk_store_commit(store,view,ops,3,&next)==CETTA_STORE_OK);
    expect(next,"a","active");expect(next,"b",NULL);expect(next,"c","base-c");expect(next,"d","");
    expect(view,"a","base-a");expect(view,"b","base-b");expect(view,"d",NULL);
    CettaDiskStoreCursor *cursor=NULL;assert(cetta_disk_store_cursor_open_range(next,NULL,false,NULL,&cursor)==CETTA_DISK_OK);
    const char *expected[]={"a","c","d"};
    for (unsigned i=0;i<3;i++) {
        CettaDiskBuffer key={0};CettaDiskStoreBlob value;
        assert(cetta_disk_store_cursor_next(cursor,&key,&value)==CETTA_DISK_OK && key.size==1 && key.data[0]==expected[i][0]);
        cetta_disk_buffer_free(&key);
    }
    CettaDiskBuffer key={0};CettaDiskStoreBlob value;
    assert(cetta_disk_store_cursor_next(cursor,&key,&value)==CETTA_DISK_END);cetta_disk_store_cursor_close(cursor);
    assert(cetta_disk_store_cursor_open_reverse_range(next,NULL,false,NULL,&cursor)==CETTA_DISK_OK);
    CettaDiskStoreBlobReader *reader=NULL;
    assert(cetta_disk_store_lookup(next,text("c"),&value)==CETTA_DISK_OK && value.storage==2);
    assert(cetta_disk_store_blob_reader_open(next,value,&reader)==CETTA_DISK_OK);
    cetta_disk_store_view_close(next);cetta_disk_store_view_close(view);cetta_disk_store_close(store);
    for (unsigned i=3;i>0;i--) {
        assert(cetta_disk_store_cursor_next(cursor,&key,&value)==CETTA_DISK_OK && key.size==1 && key.data[0]==expected[i-1][0]);
        cetta_disk_buffer_free(&key);
    }
    assert(cetta_disk_store_cursor_next(cursor,&key,&value)==CETTA_DISK_END);cetta_disk_store_cursor_close(cursor);
    char bytes[32];size_t got=0;assert(cetta_disk_store_blob_reader_read(reader,0,bytes,sizeof(bytes),&got)==CETTA_DISK_OK && got==6 && !memcmp(bytes,"base-c",6));
    cetta_disk_store_blob_reader_close(reader);
    assert(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OK);view=snap(store);
    expect(view,"a","active");expect(view,"b",NULL);expect(view,"c","base-c");expect(view,"d","");
    CettaDiskStoreScope scope={.kind=CETTA_STORE_POINT,.lower=text("b")};
    CettaDiskOp restore={.key=text("b"),.value=text("new-b")};
    assert(cetta_disk_store_commit(store,view,&restore,1,&next)==CETTA_STORE_OK);
    expect(next,"b","new-b");CettaDiskStoreView *refused=NULL;
    assert(cetta_disk_store_commit_scoped(store,view,&scope,1,NULL,0,NULL,0,&refused)==CETTA_STORE_CONFLICT && !refused);
    char backup[600];snprintf(backup,sizeof(backup),"%s-backup",path);assert(!mkdir(backup,0700));
    assert(cetta_disk_store_backup(next,backup)==CETTA_STORE_OK);
    CettaDiskStore *copy=NULL;assert(cetta_disk_store_open(backup,false,NULL,&copy)==CETTA_STORE_OK);
    CettaDiskStoreView *copied=snap(copy);assert(cetta_disk_store_view_format(copied)==2);
    expect(copied,"a","active");expect(copied,"b","new-b");expect(copied,"c","base-c");expect(copied,"d","");
    assert(cetta_disk_store_view_revision(copied)==cetta_disk_store_view_revision(next));
    assert(!memcmp(cetta_disk_store_view_epoch(copied),cetta_disk_store_view_epoch(next),16));
    cetta_disk_store_view_close(copied);cetta_disk_store_close(copy);clear(backup);
    cetta_disk_store_view_close(next);cetta_disk_store_view_close(view);cetta_disk_store_close(store);clear(path);
}
static void trace(const char *path) {
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open_created_act(path,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *view=snap(store);char expected[64][32]={{0}};bool present[64]={0};
    uint32_t random=31;
    for (unsigned step=0;step<200;step++) {
        CettaDiskOp ops[3];char keys[3][8],values[3][32];unsigned ids[3];
        for (unsigned i=0;i<3;i++) {
            random^=random<<13;random^=random>>17;random^=random<<5;ids[i]=random%64;
            snprintf(keys[i],sizeof(keys[i]),"%02u",ids[i]);snprintf(values[i],sizeof(values[i]),"v-%u-%u",step,i);
            ops[i]=(CettaDiskOp){.remove=random%4==0,.key=text(keys[i]),.value=text(random%3 ? values[i] : "")};
        }
        CettaDiskStoreView *next=NULL;assert(cetta_disk_store_commit(store,view,ops,3,&next)==CETTA_STORE_OK);
        for (unsigned i=0;i<3;i++) {
            present[ids[i]]=!ops[i].remove;
            snprintf(expected[ids[i]],sizeof(expected[ids[i]]),"%.*s",(int)ops[i].value.size,(const char *)ops[i].value.data);
        }
        for (unsigned i=0;i<64;i++) {char k[8];snprintf(k,sizeof(k),"%02u",i);expect(next,k,present[i] ? expected[i] : NULL);}
        assert(cetta_disk_store_view_revision(next)==step+1);
        cetta_disk_store_view_close(view);view=next;
    }
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);clear(path);
}

static void compaction(const char *path) {
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open_created(path,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *empty=snap(store),*before=NULL;
    CettaDiskOp initial[]={ {.key=text("a"),.value=text("old-a")}, {.key=text("b"),.value=text("old-b")},
        {.key=text("c"),.value=text("old-c")}, {.key=text("receipt"),.value=text("done")} };
    assert(cetta_disk_store_commit(store,empty,initial,4,&before)==CETTA_STORE_OK);cetta_disk_store_view_close(empty);
    CettaDiskCompaction *job=NULL;assert(cetta_disk_store_compact_act_open(store,1,&job)==CETTA_STORE_LIMIT && !job);
    empty=snap(store);assert(cetta_disk_store_view_format(empty)==2);cetta_disk_store_view_close(empty);
    assert(cetta_disk_store_compact_act_open(store,256*1024,&job)==CETTA_STORE_OK);
    empty=snap(store);assert(cetta_disk_store_view_format(empty)==3 && cetta_disk_store_view_revision(empty)==1);
    cetta_disk_store_view_close(empty);
    CettaDiskOp changes[]={ {.key=text("a"),.value=text("new-a")}, {.remove=true,.key=text("b")}, {.key=text("d"),.value=text("")} };
    CettaDiskStoreView *during=NULL;
    assert(cetta_disk_store_commit(store,before,changes,3,&during)==CETTA_STORE_OK);
    assert(cetta_disk_store_view_revision(during)==2);
    CettaDiskStore *other=NULL;assert(cetta_disk_store_open(path,false,NULL,&other)==CETTA_STORE_OK);
    CettaDiskCompaction *busy=NULL;assert(cetta_disk_store_compact_act_open(other,256*1024,&busy)==CETTA_STORE_BUSY && !busy);
    /* Closing the initiating handle releases one owner; the job stays valid. */
    cetta_disk_store_close(store);store=NULL;
    bool late=false;unsigned steps=0;CettaDiskStoreStatus status;CettaDiskStoreView *finished=NULL;
    do {
        status=cetta_disk_store_compact_act_step(job,17,&finished);
        assert(status==CETTA_STORE_PROGRESS || status==CETTA_STORE_OK);
        assert(++steps<10000);
        CettaDiskCompactionInfo info;assert(cetta_disk_store_compact_act_info(job,&info));
        if (!late && info.phase==1) {
            CettaDiskStoreView *latest=snap(other),*next=NULL;
            CettaDiskOp update={.key=text("c"),.value=text("late-c")};
            assert(cetta_disk_store_commit(other,latest,&update,1,&next)==CETTA_STORE_OK);
            cetta_disk_store_view_close(latest);cetta_disk_store_view_close(next);late=true;
            uint64_t collected;assert(cetta_disk_store_collect(other,&collected)==CETTA_STORE_OK);
        }
    } while (status==CETTA_STORE_PROGRESS);
    assert(late && finished && cetta_disk_store_view_revision(finished)==3);
    expect(finished,"a","new-a");expect(finished,"b",NULL);expect(finished,"c","late-c");expect(finished,"d","");expect(finished,"receipt","done");
    expect(before,"b","old-b");expect(during,"c","old-c");
    CettaDiskStoreView *repeated=NULL;assert(cetta_disk_store_compact_act_step(job,1,&repeated)==CETTA_STORE_OK);
    expect(repeated,"c","late-c");cetta_disk_store_view_close(repeated);
    cetta_disk_store_compact_act_close(job);job=NULL;
    uint64_t removed;assert(cetta_disk_store_collect(other,&removed)==CETTA_STORE_OK);
    expect(before,"a","old-a");expect(during,"b",NULL);
    /* Cancelled jobs retain one frozen cut; a new job resumes that same cut. */
    assert(cetta_disk_store_compact_act_open(other,256*1024,&job)==CETTA_STORE_OK);
    CettaDiskCompactionInfo cut;assert(cetta_disk_store_compact_act_info(job,&cut));
    assert(cut.source_revision==3);cetta_disk_store_compact_act_close(job);job=NULL;
    empty=snap(other);CettaDiskOp after_cancel={.key=text("e"),.value=text("after-cancel")};
    assert(cetta_disk_store_commit(other,empty,&after_cancel,1,&repeated)==CETTA_STORE_OK);
    cetta_disk_store_view_close(empty);cetta_disk_store_view_close(repeated);
    cetta_disk_store_close(other);assert(cetta_disk_store_open(path,false,NULL,&other)==CETTA_STORE_OK);
    assert(cetta_disk_store_compact_act_open(other,256*1024,&job)==CETTA_STORE_OK);
    CettaDiskCompactionInfo resumed;assert(cetta_disk_store_compact_act_info(job,&resumed));
    assert(resumed.source_revision==cut.source_revision);
    do {status=cetta_disk_store_compact_act_step(job,65536,&repeated);} while (status==CETTA_STORE_PROGRESS);
    assert(status==CETTA_STORE_OK && cetta_disk_store_view_revision(repeated)==4);
    expect(repeated,"e","after-cancel");expect(repeated,"a","new-a");expect(repeated,"b",NULL);
    cetta_disk_store_compact_act_close(job);cetta_disk_store_view_close(repeated);
    CettaDiskStoreView *second=NULL;assert(cetta_disk_store_compact(other,&second)==CETTA_STORE_OK);
    assert(cetta_disk_store_view_revision(second)==4);expect(second,"c","late-c");expect(second,"e","after-cancel");
    char backup[600];snprintf(backup,sizeof(backup),"%s-backup",path);assert(!mkdir(backup,0700));
    assert(cetta_disk_store_backup(second,backup)==CETTA_STORE_OK);
    CettaDiskStore *copy=NULL;assert(cetta_disk_store_open(backup,false,NULL,&copy)==CETTA_STORE_OK);
    empty=snap(copy);assert(cetta_disk_store_view_format(empty)==2);expect(empty,"receipt","done");expect(empty,"e","after-cancel");
    cetta_disk_store_view_close(empty);cetta_disk_store_close(copy);clear(backup);
    cetta_disk_store_view_close(second);cetta_disk_store_view_close(finished);cetta_disk_store_view_close(during);
    cetta_disk_store_view_close(before);cetta_disk_store_close(other);clear(path);
}

static void killed_compaction(const char *path,unsigned stage) {
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open_created(path,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *empty=snap(store),*original=NULL;
    CettaDiskOp initial[]={ {.key=text("a"),.value=text("one")}, {.key=text("receipt"),.value=text("done")} };
    assert(cetta_disk_store_commit(store,empty,initial,2,&original)==CETTA_STORE_OK);cetta_disk_store_view_close(empty);
    int ready[2],hold[2];assert(!pipe(ready) && !pipe(hold));pid_t child=fork();assert(child>=0);
    if (!child) {
        close(ready[0]);close(hold[1]);
        CettaDiskCompaction *job=NULL;assert(cetta_disk_store_compact_act_open(store,256*1024,&job)==CETTA_STORE_OK);
        CettaDiskStoreView *out=NULL;
        for (;;) {
            CettaDiskCompactionInfo info;assert(cetta_disk_store_compact_act_info(job,&info));
            if (info.phase==stage) break;
            CettaDiskStoreStatus status=cetta_disk_store_compact_act_step(job,31,&out);
            assert(status==CETTA_STORE_PROGRESS || status==CETTA_STORE_OK);
        }
        char byte='x';assert(write(ready[1],&byte,1)==1);
        ssize_t got=read(hold[0],&byte,1);_exit(got==1 ? 3 : 4);
    }
    close(ready[1]);close(hold[0]);struct pollfd wait={.fd=ready[0],.events=POLLIN};
    assert(poll(&wait,1,10000)==1 && (wait.revents&POLLIN));char byte;assert(read(ready[0],&byte,1)==1);
    CettaDiskStore *writer=NULL;assert(cetta_disk_store_open(path,false,NULL,&writer)==CETTA_STORE_OK);
    CettaDiskStoreView *before=snap(writer),*after=NULL;CettaDiskOp update={.key=text("late"),.value=text("persisted")};
    assert(cetta_disk_store_commit(writer,before,&update,1,&after)==CETTA_STORE_OK);
    cetta_disk_store_view_close(before);cetta_disk_store_view_close(after);cetta_disk_store_close(writer);
    assert(!kill(child,SIGKILL));int status;assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    close(ready[0]);close(hold[1]);expect(original,"a","one");
    cetta_disk_store_view_close(original);cetta_disk_store_close(store);
    assert(cetta_disk_store_open(path,false,NULL,&store)==CETTA_STORE_OK);
    before=snap(store);expect(before,"late","persisted");expect(before,"receipt","done");
    assert(cetta_disk_store_view_revision(before)==2);cetta_disk_store_view_close(before);
    uint64_t removed;assert(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK);
    CettaDiskCompaction *job=NULL;assert(cetta_disk_store_compact_act_open(store,256*1024,&job)==CETTA_STORE_OK);
    CettaDiskCompactionInfo info;assert(cetta_disk_store_compact_act_info(job,&info));
    assert(info.source_revision==(stage==4 ? 2u : 1u));
    CettaDiskStoreStatus stepped;
    do {stepped=cetta_disk_store_compact_act_step(job,65536,&after);} while (stepped==CETTA_STORE_PROGRESS);
    assert(stepped==CETTA_STORE_OK && cetta_disk_store_view_revision(after)==2);
    expect(after,"a","one");expect(after,"late","persisted");expect(after,"receipt","done");
    cetta_disk_store_view_close(after);cetta_disk_store_compact_act_close(job);cetta_disk_store_close(store);clear(path);
}

static void binary_backup(const char *path) {
    CettaDiskStore *store=NULL;assert(cetta_disk_store_open_created_act(path,NULL,&store)==CETTA_STORE_OK);
    CettaDiskStoreView *empty=snap(store),*view=NULL;
    unsigned char single[256],longer[2][2]={{0,0},{255,0}};
    CettaDiskOp ops[260];
    ops[0]=(CettaDiskOp){.key={NULL,0},.value=text("empty-key")};
    for (unsigned i=0;i<256;i++) {
        single[i]=(unsigned char)i;ops[i+1]=(CettaDiskOp){.key={single+i,1},.value=text(i%2 ? "value" : "")};
    }
    ops[257]=(CettaDiskOp){.key={longer[0],2},.value=text("zero-prefix")};
    ops[258]=(CettaDiskOp){.key={longer[1],2},.value=text("wide-prefix")};
    size_t large_size=1024*1024+37;unsigned char *large=malloc(large_size);assert(large);
    for (size_t i=0;i<large_size;i++) large[i]=(unsigned char)i;
    ops[259]=(CettaDiskOp){.key=text("large"),.value={large,large_size}};
    assert(cetta_disk_store_commit(store,empty,ops,260,&view)==CETTA_STORE_OK);
    cetta_disk_store_view_close(empty);
    char backup[600];snprintf(backup,sizeof(backup),"%s-backup",path);assert(!mkdir(backup,0700));
    assert(cetta_disk_store_backup(view,backup)==CETTA_STORE_OK);
    CettaDiskStore *copy=NULL;assert(cetta_disk_store_open(backup,false,NULL,&copy)==CETTA_STORE_OK);
    empty=snap(copy);const CettaDiskRoot *root=cetta_disk_store_view_root(empty);assert(root);
    for (unsigned i=0;i<260;i++) {
        CettaDiskBlob blob;assert(cetta_disk_lookup(root,ops[i].key,&blob)==CETTA_DISK_OK && blob.size==ops[i].value.size);
        CettaDiskBlobReader *reader=NULL;assert(cetta_disk_blob_reader_open(root,blob,&reader)==CETTA_DISK_OK);
        unsigned char bytes[4093];
        for (uint64_t offset=0;offset<blob.size;) {
            size_t got=0;assert(cetta_disk_blob_reader_read(reader,offset,bytes,sizeof(bytes),&got)==CETTA_DISK_OK && got);
            assert(!memcmp(bytes,ops[i].value.data+(size_t)offset,got));offset+=got;
        }
        cetta_disk_blob_reader_close(reader);
    }
    free(large);cetta_disk_store_view_close(empty);cetta_disk_store_close(copy);clear(backup);
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);clear(path);
}
int main(void) {
    char root[]="runtime/act-store-XXXXXX";assert(mkdtemp(root));char path[512];
    snprintf(path,sizeof(path),"%s/merged",root);merged(path);
    snprintf(path,sizeof(path),"%s/trace",root);trace(path);
    snprintf(path,sizeof(path),"%s/compact",root);compaction(path);
    snprintf(path,sizeof(path),"%s/binary",root);binary_backup(path);
    snprintf(path,sizeof(path),"%s/legacy",root);CettaDiskStore *store=NULL;
    assert(cetta_disk_store_open_created(path,NULL,&store)==CETTA_STORE_OK);cetta_disk_store_close(store);
    assert(cetta_disk_store_open_created_act(path,NULL,&store)==CETTA_STORE_OK);CettaDiskStoreView *view=snap(store);
    assert(cetta_disk_store_view_format(view)==2);cetta_disk_store_view_close(view);cetta_disk_store_close(store);clear(path);
    for (unsigned i=0;i<5;i++) {snprintf(path,sizeof(path),"%s/kill-%u",root,i);killed_compaction(path,i);}
    assert(!rmdir(root));puts("native ACT store: merged reads, 600 operations, concurrent compaction, backup and five SIGKILL cutpoints passed");
}
