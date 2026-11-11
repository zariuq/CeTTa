#define _POSIX_C_SOURCE 200809L
#include "act_space.h"
#include "atom_wire.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned kind,nth,calls;static bool hit;
ssize_t __real_pread(int,void *,size_t,off_t);ssize_t __real_pwrite(int,const void *,size_t,off_t);
int __real_fsync(int);int __real_ftruncate(int,off_t);int __real_renameat(int,const char *,int,const char *);
static bool fault(unsigned wanted) {if(kind!=wanted || ++calls!=nth)return false;hit=true;errno=EIO;return true;}
ssize_t __wrap_pread(int f,void *p,size_t n,off_t o) {return fault(1) ? -1 : __real_pread(f,p,n,o);}
ssize_t __wrap_pwrite(int f,const void *p,size_t n,off_t o) {return fault(2) ? -1 : __real_pwrite(f,p,n,o);}
int __wrap_fsync(int f) {return fault(3) ? -1 : __real_fsync(f);}
int __wrap_ftruncate(int f,off_t n) {return fault(4) ? -1 : __real_ftruncate(f,n);}
int __wrap_renameat(int a,const char *p,int b,const char *q) {return fault(5) ? -1 : __real_renameat(a,p,b,q);}
static bool trial(Space *source,const char *path,unsigned k,unsigned point) {
    unsigned char *empty=NULL;size_t size;
    assert(cetta_act_build(NULL,0,&empty,&size)==CETTA_ACT_OK);
    assert(cetta_act_publish(path,(CettaActBytes){empty,size})==CETTA_ACT_OK);free(empty);
    kind=k;nth=point;calls=0;hit=false;
    CettaActSpaceExport *j=NULL;CettaActStatus status=cetta_act_space_export_open(source,path,false,CETTA_ATOM_WIRE_BYTES,1024*1024,&j);
    if(status==CETTA_ACT_OK) do status=cetta_act_space_export_step(j,4096);while(status==CETTA_ACT_PROGRESS);
    bool injected=hit;kind=0;
    if (injected) {
        assert(status!=CETTA_ACT_OK && status!=CETTA_ACT_PROGRESS);
        if (j) assert(cetta_act_space_export_step(j,4096)==status);
    } else assert(status==CETTA_ACT_OK);
    cetta_act_space_export_close(j);
    Space restored;space_init(&restored);
    assert(cetta_act_space_capture(&restored,path,CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
    uint64_t count=space_length64(&restored);
    assert(count==0 || count==9);
    if (status==CETTA_ACT_OK) assert(count==9);
    space_free(&restored);return injected;
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena data;arena_init_detached(&data);TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&data);
    Space source;space_init_with_universe(&source,&universe);
    for (unsigned i=0;i<9;i++) assert(space_admit_atom(&source,&data,atom_expr3(&data,
        atom_symbol(&data,"row"),atom_int(&data,i%3),atom_int(&data,i))));
    char directory[]="runtime/act-export-faults-XXXXXX";assert(mkdtemp(directory));char path[128];snprintf(path,sizeof(path),"%s/data.cact",directory);
    unsigned total=0;
    for (unsigned k=1;k<=5;k++) {
        unsigned n=1;
        while(trial(&source,path,k,n)) {++n;++total;assert(n<10000);}
        assert(n>1);
    }
    assert(!unlink(path) && !rmdir(directory));space_free(&source);term_universe_free(&universe);arena_free(&data);symbol_table_free(&symbols);g_symbols=NULL;
    printf("ACT export faults: %u read/write/sync/truncate/publication cutpoints passed\n",total);
}
