#define _POSIX_C_SOURCE 200809L
#include "native_disk_store.h"
#include "sqlite3.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { KEY=40, VALUE=96 };
static volatile uint64_t observed;
static double now(void) {struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static void key_for(unsigned char key[KEY],uint64_t id) {
    memset(key,'/',KEY);memcpy(key,"space/facts/v1/collection/demo/",31);
    for(unsigned i=0;i<8;i++)key[KEY-1-i]=(unsigned char)(id>>(8*i));
}
static void value_for(unsigned char value[VALUE],uint64_t id) {
    for(unsigned i=0;i<VALUE;i++)value[i]=(unsigned char)((id*17+i*13)^(id>>(i%8)));
}
static uint64_t random_id(uint64_t *state,size_t count) {*state^=*state<<13;*state^=*state>>7;*state^=*state<<17;return *state%count;}
static uint64_t checksum(const unsigned char *value,size_t n) {
    uint64_t sum=0;for(size_t i=0;i<n;i++)sum+=value[i];return sum;
}
static void sql(sqlite3 *db,const char *statement) {
    char *error=NULL;int status=sqlite3_exec(db,statement,NULL,NULL,&error);
    if(status!=SQLITE_OK){fprintf(stderr,"sqlite: %s\n",error);sqlite3_free(error);exit(2);}
}
static double native_reads(const CettaDiskStoreView *view,size_t count,size_t reads) {
    uint64_t state=UINT64_C(0x1c77882f0231),sum=0;unsigned char key[KEY],value[VALUE];double start=now();
    for(size_t i=0;i<reads;i++) {
        key_for(key,random_id(&state,count));CettaDiskStoreBlob blob;size_t n=0;
        assert(cetta_disk_store_lookup(view,(CettaDiskBytes){key,KEY},&blob)==CETTA_DISK_OK && blob.size==VALUE);
        assert(cetta_disk_store_blob_read(view,blob,0,value,sizeof(value),&n)==CETTA_DISK_OK && n==VALUE);
        sum+=checksum(value,n);
    }
    double elapsed=now()-start;observed=sum;return elapsed;
}
static double sqlite_reads(sqlite3_stmt *statement,size_t count,size_t reads) {
    uint64_t state=UINT64_C(0x1c77882f0231),sum=0;unsigned char key[KEY],value[VALUE];double start=now();
    for(size_t i=0;i<reads;i++) {
        key_for(key,random_id(&state,count));
        assert(sqlite3_bind_blob(statement,1,key,KEY,SQLITE_STATIC)==SQLITE_OK && sqlite3_step(statement)==SQLITE_ROW);
        assert(sqlite3_column_bytes(statement,0)==VALUE);memcpy(value,sqlite3_column_blob(statement,0),VALUE);sum+=checksum(value,VALUE);
        assert(sqlite3_reset(statement)==SQLITE_OK);
    }
    double elapsed=now()-start;observed=sum;return elapsed;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>5){fprintf(stderr,"usage: compare_sqlite NEW_DIRECTORY [ROWS [READS [ROUNDS]]]\n");return 2;}
    size_t count=argc>2 ? (size_t)strtoull(argv[2],NULL,10) : 65536;
    size_t reads=argc>3 ? (size_t)strtoull(argv[3],NULL,10) : count*4;
    unsigned rounds=argc>4 ? (unsigned)strtoul(argv[4],NULL,10) : 5;
    assert(count && reads && rounds && count<SIZE_MAX/(KEY+VALUE));
    assert(!mkdir(argv[1],0700));char path[4096],sqlite_path[4096];
    assert(snprintf(path,sizeof(path),"%s/cetta",argv[1])<(int)sizeof(path));
    assert(snprintf(sqlite_path,sizeof(sqlite_path),"%s/reference.sqlite3",argv[1])<(int)sizeof(sqlite_path));
    CettaDiskStore *owner=NULL;assert(cetta_disk_store_open_created_act(path,NULL,&owner)==CETTA_STORE_OK);
    CettaDiskStoreView *empty=NULL,*delta=NULL,*base=NULL;
    assert(cetta_disk_store_snapshot(owner,&empty)==CETTA_STORE_OK);
    CettaDiskOp *ops=calloc(count,sizeof(*ops));unsigned char *data=malloc(count*(KEY+VALUE));assert(ops && data);
    sqlite3 *db=NULL;assert(sqlite3_open(sqlite_path,&db)==SQLITE_OK);
    sql(db,"PRAGMA synchronous=FULL; PRAGMA journal_mode=DELETE; PRAGMA page_size=4096; PRAGMA cache_size=-65536; CREATE TABLE kv(k BLOB PRIMARY KEY,v BLOB NOT NULL) WITHOUT ROWID; BEGIN IMMEDIATE");
    sqlite3_stmt *insert=NULL;assert(sqlite3_prepare_v2(db,"INSERT INTO kv(k,v) VALUES(?1,?2)",-1,&insert,NULL)==SQLITE_OK);
    for(size_t i=0;i<count;i++) {
        unsigned char *key=data+i*(KEY+VALUE),*value=key+KEY;key_for(key,i);value_for(value,i);
        ops[i]=(CettaDiskOp){false,{key,KEY},{value,VALUE}};
        assert(sqlite3_bind_blob(insert,1,key,KEY,SQLITE_STATIC)==SQLITE_OK && sqlite3_bind_blob(insert,2,value,VALUE,SQLITE_STATIC)==SQLITE_OK);
        assert(sqlite3_step(insert)==SQLITE_DONE && sqlite3_reset(insert)==SQLITE_OK);
    }
    sql(db,"COMMIT; VACUUM");sqlite3_finalize(insert);
    double began=now();assert(cetta_disk_store_commit(owner,empty,ops,count,&delta)==CETTA_STORE_OK);double build_s=now()-began;
    free(ops);free(data);cetta_disk_store_view_close(empty);
    CettaDiskStoreInfo before,after;assert(cetta_disk_store_view_info(delta,&before));
    CettaDiskCompaction *compaction=NULL;began=now();assert(cetta_disk_store_compact_act_open(owner,32u*1024u*1024u,&compaction)==CETTA_STORE_OK);
    CettaDiskStoreStatus status;
    do status=cetta_disk_store_compact_act_step(compaction,65536,&base);while(status==CETTA_STORE_PROGRESS);
    assert(status==CETTA_STORE_OK);double compact_s=now()-began;cetta_disk_store_compact_act_close(compaction);assert(cetta_disk_store_view_info(base,&after));
    sqlite3_stmt *statement=NULL;assert(sqlite3_prepare_v2(db,"SELECT v FROM kv WHERE k=?1",-1,&statement,NULL)==SQLITE_OK);
    native_reads(delta,count,reads);uint64_t expected=observed;native_reads(base,count,reads);assert(observed==expected);sqlite_reads(statement,count,reads);assert(observed==expected);
    struct stat st;assert(!stat(sqlite_path,&st));
    printf("{\"sqlite\":\"%s\",\"rows\":%zu,\"reads_per_round\":%zu,\"key_bytes\":%u,\"value_bytes\":%u,\"native_initial_delta_bytes\":%llu,\"native_act_base_bytes\":%llu,\"native_active_delta_bytes\":%llu,\"sqlite_vacuum_bytes\":%llu,\"native_build_s\":%.6f,\"compact_s\":%.6f,\"rounds\":[",sqlite3_libversion(),count,reads,KEY,VALUE,(unsigned long long)before.delta_bytes,(unsigned long long)after.base_bytes,(unsigned long long)after.delta_bytes,(unsigned long long)st.st_size,build_s,compact_s);
    for(unsigned round=0;round<rounds;round++) {
        double time[3]={0};for(unsigned k=0;k<3;k++) {
            unsigned engine=(round+k)%3;
            time[engine]=engine==0 ? native_reads(delta,count,reads) : engine==1 ? native_reads(base,count,reads) : sqlite_reads(statement,count,reads);
            assert(observed==expected);
        }
        printf("%s{\"delta_ns\":%.3f,\"act_ns\":%.3f,\"sqlite_ns\":%.3f}",round ? "," : "",time[0]*1e9/reads,time[1]*1e9/reads,time[2]*1e9/reads);
    }
    puts("]}");sqlite3_finalize(statement);assert(sqlite3_close(db)==SQLITE_OK);
    cetta_disk_store_view_close(delta);cetta_disk_store_view_close(base);cetta_disk_store_close(owner);return 0;
}
