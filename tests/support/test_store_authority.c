#define _POSIX_C_SOURCE 200809L
#include "library.h"
#include "library_store.h"
#include "native_handle.h"
#include "native_disk_store.h"
#include "eval.h"
#include "shared_transition.h"
#include "petta_specializer.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
static bool fail_view,fail_program;
static Space *program_root;
static Atom *program_edit;
static bool race_publication;
static atomic_bool editor_attempted,commit_returned,editor_valid;
static pthread_t editor;
static CettaLibraryContext *closing_watch_context;
static uint64_t closing_watch_identity;
#define check(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"store authority failed at line %d: %s\n",__LINE__,#test);exit(1); \
} } while (0)
Space *__wrap_eval_current_root_space(void) { return program_root; }
bool __real_space_program_token_matches_live_space(SpaceProgramToken,const Space *);
bool __wrap_space_program_token_matches_live_space(SpaceProgramToken token,const Space *root) {
    if (closing_watch_context) {
        /* A logical close during validation must not free the barrier while
         * this invocation still owns its acquired resource use. */
        CettaLibraryContext *ctx=closing_watch_context;closing_watch_context=NULL;
        check(cetta_native_handle_close(ctx,"program-watch",closing_watch_identity));
    }
    return __real_space_program_token_matches_live_space(token,root);
}
static void *edit_program(void *unused) {
    (void)unused;cetta_shared_transition_scope_enter();
    atomic_store_explicit(&editor_attempted,true,memory_order_release);
    {
        CETTA_SCOPED_SHARED_TRANSITION(program_editor);
        atomic_store_explicit(&editor_valid,atomic_load_explicit(&commit_returned,memory_order_acquire),memory_order_release);
        space_add(program_root,program_edit);
    }
    cetta_shared_transition_scope_leave();return NULL;
}
CettaDiskStoreStatus __real_cetta_disk_store_commit(CettaDiskStore *,const CettaDiskStoreView *,const CettaDiskOp *,size_t,CettaDiskStoreView **);
CettaDiskStoreStatus __wrap_cetta_disk_store_commit(CettaDiskStore *owner,const CettaDiskStoreView *view,
        const CettaDiskOp *ops,size_t count,CettaDiskStoreView **out) {
    if (!race_publication) return __real_cetta_disk_store_commit(owner,view,ops,count,out);
    check(cetta_shared_transition_guard_held_by_current_thread());
    check(!pthread_create(&editor,NULL,edit_program,NULL));
    while (!atomic_load_explicit(&editor_attempted,memory_order_acquire)) sched_yield();
    CettaDiskStoreStatus status=__real_cetta_disk_store_commit(owner,view,ops,count,out);
    atomic_store_explicit(&commit_returned,true,memory_order_release);return status;
}
bool __real_cetta_native_handle_alloc(CettaLibraryContext *,const char *,void *,
    CettaNativeHandleFreeFn,uint64_t *);
bool __wrap_cetta_native_handle_alloc(CettaLibraryContext *ctx,const char *kind,void *resource,
        CettaNativeHandleFreeFn release,uint64_t *identity) {
    if (fail_view && !strcmp(kind,"disk-store-view")) { fail_view=false;*identity=0;return false; }
    if (fail_program && !strcmp(kind,"program-watch")) { fail_program=false;*identity=0;return false; }
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
static void program_dependencies(void) {
    CettaLibraryContext ctx;cetta_library_context_init_for_language_profile(&ctx,CETTA_LANGUAGE_PETTA,NULL);
    Arena arena;arena_init_detached(&arena);Space source,module,overlay;
    space_init(&source);space_init(&module);check(space_add_dependency(&source,&module));
    check(space_init_overlay_local(&overlay,&source));program_root=&overlay;
    for (unsigned mode=0;mode<6;mode++) {
        Atom *opened=call(&ctx,&arena,"__cetta_lib_store_program_watch_open",NULL,0);
        check(shape(opened,"StoreProgramWatch",2));Atom *args[]={opened->expr.elems[1]};
        check(atom_is_symbol(call(&ctx,&arena,"__cetta_lib_store_program_watch_check",args,1),"StoreProgramCurrent"));
        if (mode<2) {
            Atom *equation=atom_expr3(&arena,atom_symbol(&arena,"="),
                atom_expr2(&arena,atom_symbol(&arena,"dependency-code"),atom_int(&arena,mode)),atom_int(&arena,mode));
            space_add(mode ? &source : &module,equation);
        } else if (mode==2) ctx.petta_admission_revision++;
        else if (mode==3) ctx.petta_library_paths.revision++;
        else if (mode==4) ctx.active_mask^=1u;
        else {
            /* A saved pointer is not a lifetime claim. Reusing the exact
             * root header must not revive the earlier observation. */
            space_free(&overlay);check(space_init_overlay_local(&overlay,&source));
        }
        Atom *invalid=call(&ctx,&arena,"__cetta_lib_store_program_watch_check",args,1);
        if (!shape(invalid,"StoreRefused",2)) fprintf(stderr,"program dependency mode %u remained current\n",mode);
        check(shape(invalid,"StoreRefused",2) && atom_is_symbol(invalid->expr.elems[1],"program-invalidated"));
        call(&ctx,&arena,"__cetta_lib_store_close",args,1);
    }
    Atom *opened=call(&ctx,&arena,"__cetta_lib_store_program_watch_open",NULL,0);
    check(shape(opened,"StoreProgramWatch",2));Atom *args[]={opened->expr.elems[1]};
    check(cetta_native_handle_arg(args[0],"program-watch",&closing_watch_identity));closing_watch_context=&ctx;
    check(atom_is_symbol(call(&ctx,&arena,"__cetta_lib_store_program_watch_check",args,1),"StoreProgramCurrent"));
    check(!closing_watch_context && !petta_program_read_barrier_active(&overlay));
    check(shape(call(&ctx,&arena,"__cetta_lib_store_program_watch_check",args,1),"StoreRefused",2));
    arena_free(&arena);check(cetta_native_handle_count(&ctx)==0);
    space_free(&overlay);space_free(&source);space_free(&module);program_root=NULL;cetta_library_context_free(&ctx);
}
static void program_publication(void) {
    CettaLibraryContext ctx;cetta_library_context_init_for_language_profile(&ctx,CETTA_LANGUAGE_PETTA,NULL);
    Arena arena;arena_init_detached(&arena);Space source;space_init(&source);program_root=&source;
    char directory[]="runtime/program-publication.XXXXXX";check(mkdtemp(directory));
    Atom *args[]={atom_string(&arena,directory),atom_string(&arena,"create")};
    Atom *opened=call(&ctx,&arena,"__cetta_lib_store_open",args,2);check(shape(opened,"StoreOpened",2));
    Atom *owner=opened->expr.elems[1],*owner_args[]={owner};
    Atom *snapshot=call(&ctx,&arena,"__cetta_lib_store_snapshot",owner_args,1);check(shape(snapshot,"StoreSnapshot",4));
    fail_program=true;
    check(shape(call(&ctx,&arena,"__cetta_lib_store_program_watch_open",NULL,0),"StoreRefused",2));
    check(!fail_program && !petta_program_read_barrier_active(&source));
    Atom *watched=call(&ctx,&arena,"__cetta_lib_store_program_watch_open",NULL,0);check(shape(watched,"StoreProgramWatch",2));
    Atom *watch=watched->expr.elems[1],*watch_args[]={watch};
    Atom *clone=call(&ctx,&arena,"__cetta_lib_store_program_watch_clone",watch_args,1);check(shape(clone,"StoreProgramWatch",2));
    Atom *copy_args[]={clone->expr.elems[1]};
    call(&ctx,&arena,"__cetta_lib_store_close",watch_args,1);
    check(atom_is_symbol(call(&ctx,&arena,"__cetta_lib_store_program_watch_check",copy_args,1),"StoreProgramCurrent"));
    uint64_t identity=0;check(cetta_native_handle_arg(clone->expr.elems[1],"program-watch",&identity));
    Atom *literal_args[]={cetta_native_handle_atom(&arena,"program-watch",identity)};
    check(shape(call(&ctx,&arena,"__cetta_lib_store_program_watch_check",literal_args,1),"StoreRefused",2));
    Atom *put=atom_expr3(&arena,atom_symbol(&arena,"put"),atom_string(&arena,"receipt"),atom_string(&arena,"once"));
    Atom *ops=atom_expr2(&arena,atom_symbol(&arena,"ops"),put);
    Atom *commit_args[]={owner,snapshot->expr.elems[1],clone->expr.elems[1],ops};
    program_edit=atom_expr3(&arena,atom_symbol(&arena,"="),
        atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"new-program-code")},1),atom_symbol(&arena,"changed"));
    cetta_shared_transition_scope_enter();race_publication=true;
    Atom *committed=call(&ctx,&arena,"__cetta_lib_store_commit_guarded",commit_args,4);
    race_publication=false;check(!pthread_join(editor,NULL));check(atomic_load_explicit(&editor_valid,memory_order_acquire));
    cetta_shared_transition_scope_leave();
    check(shape(committed,"StoreSnapshot",4) && committed->expr.elems[3]->ground.ival==1);
    Atom *invalid=call(&ctx,&arena,"__cetta_lib_store_program_watch_check",copy_args,1);
    check(shape(invalid,"StoreRefused",2) && atom_is_symbol(invalid->expr.elems[1],"program-invalidated"));
    Atom *get_args[]={committed->expr.elems[1],atom_string(&arena,"receipt"),atom_int(&arena,16)};
    check(shape(call(&ctx,&arena,"__cetta_lib_store_get",get_args,3),"StoreValue",2));
    commit_args[1]=committed->expr.elems[1];
    invalid=call(&ctx,&arena,"__cetta_lib_store_commit_guarded",commit_args,4);
    check(shape(invalid,"StoreRefused",2) && atom_is_symbol(invalid->expr.elems[1],"program-invalidated"));
    Atom *now=call(&ctx,&arena,"__cetta_lib_store_snapshot",owner_args,1);check(now->expr.elems[3]->ground.ival==1);
    arena_free(&arena);check(cetta_native_handle_count(&ctx)==0);
    space_free(&source);program_root=NULL;cetta_library_context_free(&ctx);empty_directory(directory);
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    CettaLibraryContext ctx;cetta_library_context_init(&ctx);
    Arena arena;arena_init_detached(&arena);
    CettaLanguageId language=ctx.session.language_id;ctx.session.language_id=CETTA_LANGUAGE_PRIME;
    Atom *point_args[]={atom_symbol(&arena,"not-a-view"),atom_string(&arena,"key"),
        atom_symbol(&arena,"pattern"),atom_symbol(&arena,"template")};
    Atom *point_refused=call(&ctx,&arena,"__cetta_lib_store_space_point_query_open",point_args,4);
    check(shape(point_refused,"StoreRefused",2) && atom_is_symbol(point_refused->expr.elems[1],"space-language"));
    ctx.session.language_id=language;
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
    cetta_library_context_free(&ctx);program_dependencies();program_publication();symbol_table_free(&symbols);g_symbols=NULL;
    empty_directory(directory);printf("store reply authority: %u checks passed\n",checks);return 0;
}
