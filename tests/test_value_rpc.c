#define _GNU_SOURCE
#include "value_rpc.h"
#include "atom_wire.h"
#include "storage_census.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
typedef struct {int fd;unsigned messages;bool wrong,disconnect,remote_error;} Peer;
static void io(int fd,void *data,size_t size,bool writing) {
    unsigned char *p=data;
    while(size) {
        ssize_t n=writing ? send(fd,p,size,MSG_NOSIGNAL) : recv(fd,p,size,0);
        if(n<0 && errno==EINTR)continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){struct pollfd q={fd,writing ? POLLOUT : POLLIN,0};assert(poll(&q,1,3000)>0);continue;}
        assert(n>0);p+=n;size-=(size_t)n;
    }
}
static void *serve(void *raw) {
    Peer *p=raw;
    for(unsigned i=0;i<p->messages;i++) {
        unsigned char header[8];io(p->fd,header,8,false);size_t size;
        assert(cetta_ipc_csp_size(header,1048576,&size));unsigned char *bytes=malloc(size);assert(bytes);io(p->fd,bytes,size,false);
        Arena arena;arena_init_detached(&arena);Atom *request=NULL;
        assert(cetta_atom_wire_decode(&arena,bytes,size,&request)==ATOM_WIRE_OK);free(bytes);
        if(p->disconnect){arena_free(&arena);break;}
        Atom *id=p->wrong ? atom_string(&arena,"wrong-id") : request->expr.elems[1];
        Atom *reply=atom_expr(&arena,(Atom *[]){atom_symbol(&arena,"reply"),id,
            atom_symbol(&arena,p->remote_error && !i ? "error" : "ok"),request->expr.elems[2]},4);
        assert(cetta_atom_wire_encode(reply,&bytes,&size)==ATOM_WIRE_OK && cetta_ipc_csp_header(header,size));
        for(size_t k=0;k<8;k++)io(p->fd,header+k,1,true);
        for(size_t k=0;k<size;k++)io(p->fd,bytes+k,1,true);
        free(bytes);arena_free(&arena);
    }
    close(p->fd);return NULL;
}
static CettaValueChannel *channel(Peer *peer) {
    int fd[2];assert(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));peer->fd=fd[1];
    CettaIpcSession *session=cetta_ipc_session_new(fd[0]);assert(session);
    CettaValueChannel *c=cetta_value_channel_new(session,1048576,3000);assert(c);cetta_ipc_session_release(session);return c;
}
static void ordered(void) {
    Peer p={.messages=2,.remote_error=true};CettaValueChannel *c=channel(&p);pthread_t thread;assert(!pthread_create(&thread,NULL,serve,&p));
    Arena args;arena_init_detached(&args);CettaValueRpc *first=NULL,*second=NULL;
    Atom *one=atom_expr2(&args,atom_symbol(&args,"first"),atom_int(&args,1));Atom *two=atom_expr2(&args,atom_symbol(&args,"second"),atom_int(&args,2));
    assert(cetta_value_rpc_new(c,one,&first)==CETTA_RPC_PENDING && cetta_value_rpc_new(c,two,&second)==CETTA_RPC_PENDING);
    assert(cetta_value_rpc_step(second,0)==CETTA_RPC_PENDING && !cetta_value_rpc_sent(first));
    cetta_value_channel_release(c);arena_free(&args);
    /* Waiting for the second request drives and validates its predecessor. */
    assert(cetta_value_rpc_await(second)==CETTA_RPC_OK);
    assert(cetta_value_rpc_status(first)==CETTA_RPC_REMOTE_ERROR);
    assert(atom_is_symbol(cetta_value_rpc_result(first)->expr.elems[0],"first"));
    assert(atom_is_symbol(cetta_value_rpc_result(second)->expr.elems[0],"second"));
    CettaStorageCensus census={0};cetta_value_rpc_storage(first,&census);cetta_storage_finish(&census);assert(!census.incomplete && !census.overflow);cetta_storage_census_free(&census);
    cetta_value_rpc_release(first);cetta_value_rpc_release(second);assert(!pthread_join(thread,NULL));
}
static void failures(bool wrong) {
    Peer p={.messages=1,.wrong=wrong,.disconnect=!wrong};CettaValueChannel *c=channel(&p);pthread_t thread;assert(!pthread_create(&thread,NULL,serve,&p));
    Arena a;arena_init_detached(&a);CettaValueRpc *r=NULL;
    assert(cetta_value_rpc_new(c,atom_symbol(&a,"operation"),&r)==CETTA_RPC_PENDING);
    assert(cetta_value_rpc_await(r)==CETTA_RPC_UNCERTAIN && cetta_value_rpc_sent(r) && !cetta_value_rpc_result(r));
    cetta_value_rpc_release(r);
    assert(cetta_value_rpc_new(c,atom_symbol(&a,"next"),&r)==CETTA_RPC_PENDING);
    assert(cetta_value_rpc_await(r)==CETTA_RPC_UNAVAILABLE && !cetta_value_rpc_sent(r));
    cetta_value_rpc_release(r);cetta_value_channel_release(c);arena_free(&a);assert(!pthread_join(thread,NULL));
}
static void cancel_unsent(void) {
    Peer p={.messages=1};CettaValueChannel *c=channel(&p);pthread_t thread;assert(!pthread_create(&thread,NULL,serve,&p));
    Arena a;arena_init_detached(&a);CettaValueRpc *r=NULL,*discard=NULL;
    assert(cetta_value_rpc_new(c,atom_symbol(&a,"keep"),&r)==CETTA_RPC_PENDING);
    assert(cetta_value_rpc_new(c,atom_symbol(&a,"discard"),&discard)==CETTA_RPC_PENDING);
    assert(cetta_value_rpc_step(r,1)==CETTA_RPC_PENDING);
    assert(!cetta_value_rpc_cancel_queued(r));
    assert(cetta_value_rpc_cancel_queued(discard));
    assert(!cetta_value_rpc_cancel_queued(discard));
    assert(cetta_value_rpc_status(discard)==CETTA_RPC_UNAVAILABLE && !cetta_value_rpc_sent(discard));
    cetta_value_rpc_release(discard);assert(cetta_value_rpc_await(r)==CETTA_RPC_OK);
    cetta_value_rpc_release(r);cetta_value_channel_release(c);arena_free(&a);assert(!pthread_join(thread,NULL));
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    ordered();failures(true);failures(false);cancel_unsent();symbol_table_free(&symbols);g_symbols=NULL;
    puts("Value RPC: queued progress, inert retained replies, parent close, malformed/lost replies and unsent cancellation passed");
}
