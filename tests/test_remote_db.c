#define _GNU_SOURCE
#include "remote_db.h"
#include "atom_wire.h"
#include "parser.h"
#include "petta_search_machine.h"
#include "storage_census.h"
#include "shared_transition.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static Atom *parse(Arena *a,const char *text){Atom **forms=NULL;assert(parse_metta_text(text,a,&forms)==1);Atom *result=forms[0];free(forms);return result;}
static PettaMachineHostMode classify(void *ctx,Space *space,Atom *call) {
    (void)ctx;(void)space;return atom_head_symbol_id(call)==g_builtin_syms.pat_query ? PETTA_MACHINE_HOST_PATTERN_QUERY : PETTA_MACHINE_HOST_NONE;
}
static Space *resolve(void *ctx,Space *space,Arena *a,Atom *reference){(void)ctx;(void)a;return atom_is_symbol(reference,"&self") ? space : NULL;}
static size_t query(Space *s,Arena *a,const char *text) {
    PettaMachineHost host={.classify=classify,.resolve_space=resolve};PettaMachine machine;
    assert(petta_machine_init(&machine,s,a,parse(a,text),NULL,&host));size_t count=0;PettaMachineStep step;Atom *answer;
    do {Bindings b;step=petta_machine_next(&machine,&answer,&b);bindings_free(&b);if(step==PETTA_MACHINE_STEP_ANSWER)++count;
        if(step==PETTA_MACHINE_STEP_SUSPENDED)assert(petta_machine_await_source(&machine));}
    while(step==PETTA_MACHINE_STEP_ANSWER || step==PETTA_MACHINE_STEP_SUSPENDED);
    assert(step==PETTA_MACHINE_STEP_EXHAUSTED);petta_machine_destroy(&machine);return count;
}
static int connect_socket(const char *path) {
    struct sockaddr_un a={.sun_family=AF_UNIX};assert(strlen(path)<sizeof(a.sun_path));strcpy(a.sun_path,path);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);assert(fd>=0);
    if(connect(fd,(struct sockaddr *)&a,sizeof(a))) {assert(errno==EINPROGRESS);struct pollfd p={fd,POLLOUT,0};assert(poll(&p,1,3000)>0);}
    return fd;
}
static size_t drain(SpaceOccurrenceCursor *c,Arena *a,Atom **id0) {
    size_t count=0;CettaIndex index;Atom *atom=NULL;SpaceOccurrenceCursorStep step;
    do {
        step=space_occurrence_cursor_next(c,&index,&atom);
        if(step==SPACE_OCCURRENCE_CURSOR_ITEM){if(!count && id0)*id0=space_occurrence_cursor_identity(c,index,a);++count;}
    }while(step==SPACE_OCCURRENCE_CURSOR_ITEM || step==SPACE_OCCURRENCE_CURSOR_PROGRESS);
    assert(step==SPACE_OCCURRENCE_CURSOR_END);return count;
}
int main(int argc,char **argv) {
    assert(argc==2 || (argc==3 && !strcmp(argv[2],"--cancel")));SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena a;arena_init_detached(&a);TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&a);
    CettaIpcSession *session=cetta_ipc_session_new(connect_socket(argv[1]));assert(session);CettaRemoteDb *db=NULL;
    assert(cetta_remote_db_open(session,1048576,3000,&db)==CETTA_RPC_OK);cetta_ipc_session_release(session);
    CettaRemoteCapture *capture=NULL;assert(cetta_remote_db_capture(db,atom_symbol(&a,"data"),&capture)==CETTA_RPC_OK);
    Space source;space_init_with_universe(&source,&universe);assert(cetta_remote_capture_space(capture,&source));
    assert(space_length64(&source)==5);
    if(argc==3) {
        SpaceOccurrenceCursor active,queued;CettaIndex at;
        assert(space_occurrence_cursor_init_scan(&source,&active));
        assert(space_occurrence_cursor_init_scan(&source,&queued));
        assert(space_occurrence_cursor_next(&active,&at,NULL)==SPACE_OCCURRENCE_CURSOR_PROGRESS);
        assert(space_occurrence_cursor_next(&queued,&at,NULL)==SPACE_OCCURRENCE_CURSOR_PROGRESS);
        space_occurrence_cursor_release(&queued);space_occurrence_cursor_release(&active);
        assert(query(&source,&a,"(match &self (edge a $to) $to)")==2);
        cetta_remote_capture_release(capture);cetta_remote_db_release(db);space_free(&source);
        term_universe_free(&universe);arena_free(&a);symbol_table_free(&symbols);g_symbols=NULL;
        puts("REMOTE_CANCELLED_READER_OK");return 0;
    }
    assert(query(&source,&a,"(match &self (edge a $to) $to)")==2);
    cetta_shared_transition_scope_enter();
    CettaSharedTransitionGuard outer={0},inner={0};
    cetta_shared_transition_guard_enter(&outer);cetta_shared_transition_guard_enter(&inner);
    assert(query(&source,&a,"(match &self (edge a $to) $to)")==2);
    assert(cetta_shared_transition_guard_held_by_current_thread());
    cetta_shared_transition_guard_leave(&inner);cetta_shared_transition_guard_leave(&outer);
    assert(!cetta_shared_transition_guard_held_by_current_thread());cetta_shared_transition_scope_leave();
    assert(query(&source,&a,"(match &self (rel one one) yes)")==1);
    assert(query(&source,&a,"(match &self (rel one two) yes)")==0);
    Space workspace;space_init_with_universe(&workspace,&universe);assert(space_init_overlay_local(&workspace,&source));
    assert(space_admit_atom(&workspace,&a,parse(&a,"(local edit)")));
    assert(query(&workspace,&a,"(match &self (local $v) $v)")==1);space_free(&workspace);
    SpaceOccurrenceCursor scan,clone;assert(space_occurrence_cursor_init_scan(&source,&scan));CettaIndex index;
    assert(space_occurrence_cursor_next(&scan,&index,NULL)==SPACE_OCCURRENCE_CURSOR_PROGRESS);
    assert(space_occurrence_cursor_clone(&scan,&clone));
    cetta_remote_capture_release(capture);cetta_remote_db_release(db);space_free(&source);
    Atom *left=NULL,*right=NULL;assert(drain(&scan,&a,&left)==5);assert(drain(&clone,&a,&right)==5);
    assert(left && right && atom_eq(left,right));space_occurrence_cursor_release(&scan);space_occurrence_cursor_release(&clone);
    term_universe_free(&universe);arena_free(&a);symbol_table_free(&symbols);g_symbols=NULL;
    puts("REMOTE_SPACE_OK");return 0;
}
