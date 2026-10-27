#define _POSIX_C_SOURCE 200809L
#include "library.h"
#include "library_store.h"
#include "native_handle.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
static bool fail_view;
#define check(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"store authority failed at line %d: %s\n",__LINE__,#test);exit(1); \
} } while (0)
bool __real_cetta_native_handle_alloc(CettaLibraryContext *,const char *,void *,
    CettaNativeHandleFreeFn,uint64_t *);
bool __wrap_cetta_native_handle_alloc(CettaLibraryContext *ctx,const char *kind,void *resource,
        CettaNativeHandleFreeFn release,uint64_t *identity) {
    if (fail_view && !strcmp(kind,"disk-store-view")) { fail_view=false;*identity=0;return false; }
    return __real_cetta_native_handle_alloc(ctx,kind,resource,release,identity);
}
static bool shape(Atom *value,const char *tag,uint32_t length) {
    return value && value->kind==ATOM_EXPR && value->expr.len==length &&
        atom_is_symbol(value->expr.elems[0],tag);
}
static Atom *call(CettaLibraryContext *ctx,Arena *arena,const char *name,Atom **arguments,uint32_t n) {
    Atom *result=cetta_store_dispatch(ctx,arena,atom_symbol(arena,name),arguments,n);check(result);return result;
}
static void empty_directory(const char *path) {
    DIR *directory=opendir(path);check(directory);struct dirent *entry;
    while ((entry=readdir(directory)))
        if (strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))
            check(!unlinkat(dirfd(directory),entry->d_name,0));
    closedir(directory);check(!rmdir(path));
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    CettaLibraryContext ctx;cetta_library_context_init(&ctx);
    Arena arena;arena_init_detached(&arena);
    char directory[]="runtime/store-authority.XXXXXX";check(mkdtemp(directory));
    Atom *open_args[]={atom_string(&arena,directory),atom_string(&arena,"create")};
    Atom *opened=call(&ctx,&arena,"__cetta_lib_store_open",open_args,2);check(shape(opened,"StoreOpened",2));
    Atom *owner=opened->expr.elems[1],*owner_args[]={owner};
    Atom *snapshot=call(&ctx,&arena,"__cetta_lib_store_snapshot",owner_args,1);
    check(shape(snapshot,"StoreSnapshot",4));
    Atom *key=atom_string(&arena,"receipt"),*body=atom_string(&arena,"once");
    Atom *operation=atom_expr3(&arena,atom_symbol(&arena,"put"),key,body);
    Atom *operations=atom_expr2(&arena,atom_symbol(&arena,"ops"),operation);
    Atom *commit_args[]={owner,snapshot->expr.elems[1],operations};
    fail_view=true;
    check(shape(call(&ctx,&arena,"__cetta_lib_store_commit",commit_args,3),"StoreUncertain",1));
    check(!fail_view);
    Atom *blocked=call(&ctx,&arena,"__cetta_lib_store_commit",commit_args,3);
    check(shape(blocked,"StoreRefused",2) && atom_is_symbol(blocked->expr.elems[1],"recovery-required"));
    Atom *recovered=call(&ctx,&arena,"__cetta_lib_store_recover",owner_args,1);
    check(shape(recovered,"StoreSnapshot",4) && recovered->expr.elems[3]->ground.ival==1);
    Atom *get_args[]={recovered->expr.elems[1],key,atom_int(&arena,16)};
    Atom *read=call(&ctx,&arena,"__cetta_lib_store_get",get_args,3);
    check(shape(read,"StoreValue",2) && atom_string_equals_cstr(read->expr.elems[1],"once"));
    commit_args[1]=recovered->expr.elems[1];
    fail_view=true;
    check(shape(call(&ctx,&arena,"__cetta_lib_store_compact",owner_args,1),"StoreUncertain",1));
    check(!fail_view);
    blocked=call(&ctx,&arena,"__cetta_lib_store_commit",commit_args,3);
    check(shape(blocked,"StoreRefused",2) && atom_is_symbol(blocked->expr.elems[1],"recovery-required"));
    recovered=call(&ctx,&arena,"__cetta_lib_store_recover",owner_args,1);
    check(shape(recovered,"StoreSnapshot",4) && recovered->expr.elems[3]->ground.ival==1);
    get_args[0]=recovered->expr.elems[1];
    read=call(&ctx,&arena,"__cetta_lib_store_get",get_args,3);
    check(shape(read,"StoreValue",2) && atom_string_equals_cstr(read->expr.elems[1],"once"));
    arena_free(&arena);check(cetta_native_handle_count(&ctx)==0);
    cetta_library_context_free(&ctx);symbol_table_free(&symbols);g_symbols=NULL;
    empty_directory(directory);printf("store reply authority: %u checks passed\n",checks);return 0;
}
