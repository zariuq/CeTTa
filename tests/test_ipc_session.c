#define _GNU_SOURCE
#include "ipc_session.h"
#include "storage_census.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static unsigned fail_allocation,allocations;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n) {return fail_allocation && ++allocations==fail_allocation ? NULL : __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t width) {return fail_allocation && ++allocations==fail_allocation ? NULL : __real_calloc(n,width);}
static CettaIpcSession *session(int *peer) {
    int fd[2];assert(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));
    CettaIpcSession *s=cetta_ipc_session_new(fd[0]);assert(s);*peer=fd[1];return s;
}
static CettaIpcExchange *begin(CettaIpcSession *s) {
    CettaIpcExchange *x=NULL;assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&x)==CETTA_IPC_SESSION_OK && x);return x;
}
static void complete(CettaIpcExchange *x,int peer) {
    cetta_ipc_exchange_pump(x,128);char bytes[12];assert(recv(peer,bytes,12,0)==12 && !memcmp(bytes,"CSP1\0\0\0\4ping",12));
    assert(!cetta_ipc_exchange_ready(x) && cetta_ipc_exchange_sent(x));
    assert(send(peer,"CSP1\0\0\0\4pong",12,MSG_NOSIGNAL)==12);
    cetta_ipc_exchange_pump(x,128);assert(cetta_ipc_exchange_ready(x) && !cetta_ipc_exchange_error(x));
    size_t n;const unsigned char *reply=cetta_ipc_exchange_reply(x,&n);assert(reply && n==4 && !memcmp(reply,"pong",4));
}
static void retained_children(void) {
    int peer;CettaIpcSession *s=session(&peer);cetta_ipc_session_retain(s);cetta_ipc_session_release(s);
    CettaIpcExchange *first=begin(s),*second=NULL;
    assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&second)==CETTA_IPC_SESSION_BUSY && !second);
    complete(first,peer);
    assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&second)==CETTA_IPC_SESSION_BUSY && !second);
    CettaStorageCensus census={0};cetta_ipc_session_storage(s,&census);cetta_storage_finish(&census);
    assert(!census.incomplete && !census.overflow && census.bytes>128);cetta_storage_census_free(&census);
    assert(cetta_ipc_exchange_accept(first) && cetta_ipc_exchange_accept(first));
    second=begin(s);assert(!cetta_ipc_exchange_cancel(first));
    size_t n;const unsigned char *reply=cetta_ipc_exchange_reply(first,&n);assert(reply && n==4 && !memcmp(reply,"pong",4));
    cetta_ipc_exchange_release(first);complete(second,peer);
    cetta_ipc_exchange_retain(second);cetta_ipc_exchange_release(second);
    assert(cetta_ipc_exchange_accept(second));cetta_ipc_session_release(s);
    cetta_ipc_exchange_release(second);char byte;assert(recv(peer,&byte,1,0)==0);close(peer);
}
static void cancelled(bool sent) {
    int peer;CettaIpcSession *s=session(&peer);CettaIpcExchange *x=begin(s);
    if (sent) {cetta_ipc_exchange_pump(x,128);char bytes[12];assert(recv(peer,bytes,12,0)==12);}
    assert(cetta_ipc_exchange_cancel(x));
    assert(cetta_ipc_exchange_ready(x) && cetta_ipc_exchange_sent(x)==sent && !strcmp(cetta_ipc_exchange_error(x),"cancelled"));
    CettaIpcExchange *other=NULL;
    assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&other)==CETTA_IPC_SESSION_UNAVAILABLE && !other);
    assert(!cetta_ipc_exchange_accept(x));cetta_ipc_exchange_release(x);cetta_ipc_session_release(s);close(peer);
}
static void abandoned(void) {
    int peer;CettaIpcSession *s=session(&peer);CettaIpcExchange *x=begin(s);
    cetta_ipc_exchange_release(x);CettaIpcExchange *other=NULL;
    assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&other)==CETTA_IPC_SESSION_UNAVAILABLE && !other);
    cetta_ipc_session_release(s);char byte;assert(recv(peer,&byte,1,0)==0);close(peer);
}
static void lost_reply(void) {
    int peer;CettaIpcSession *s=session(&peer);CettaIpcExchange *x=begin(s);
    cetta_ipc_exchange_pump(x,128);char bytes[12];assert(recv(peer,bytes,12,0)==12);close(peer);
    cetta_ipc_exchange_pump(x,128);assert(cetta_ipc_exchange_ready(x) && cetta_ipc_exchange_sent(x) && cetta_ipc_exchange_error(x));
    CettaIpcExchange *other=NULL;
    assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&other)==CETTA_IPC_SESSION_UNAVAILABLE);
    cetta_ipc_exchange_release(x);cetta_ipc_session_release(s);
}
static void allocations_refuse_without_consuming(void) {
    int peer;CettaIpcSession *s=session(&peer);
    for (unsigned n=1;n<=3;n++) {
        allocations=0;fail_allocation=n;CettaIpcExchange *x=NULL;
        assert(cetta_ipc_session_begin(s,CETTA_IPC_CSP,"ping",4,1000,1024,&x)==CETTA_IPC_SESSION_NOMEM && !x);
        fail_allocation=0;char byte;assert(recv(peer,&byte,1,0)==-1 && (errno==EAGAIN || errno==EWOULDBLOCK));
        x=begin(s);complete(x,peer);assert(cetta_ipc_exchange_accept(x));cetta_ipc_exchange_release(x);
    }
    CettaIpcExchange *x=begin(s);cetta_ipc_exchange_pump(x,128);
    char bytes[12];assert(recv(peer,bytes,12,0)==12);
    assert(send(peer,"CSP1\0\0\0\4",8,MSG_NOSIGNAL)==8);
    allocations=0;fail_allocation=1;cetta_ipc_exchange_pump(x,128);fail_allocation=0;
    assert(cetta_ipc_exchange_ready(x) && cetta_ipc_exchange_sent(x) && !strcmp(cetta_ipc_exchange_error(x),"memory"));
    cetta_ipc_exchange_release(x);cetta_ipc_session_release(s);close(peer);
}
typedef struct {CettaIpcSession *session;pthread_barrier_t *barrier;CettaIpcSessionStatus status;CettaIpcExchange *exchange;} Racing;
static void *compete(void *raw) {
    Racing *r=raw;pthread_barrier_wait(r->barrier);
    r->status=cetta_ipc_session_begin(r->session,CETTA_IPC_CSP,"ping",4,1000,1024,&r->exchange);return NULL;
}
static void competing_begin(void) {
    int peer;CettaIpcSession *s=session(&peer);pthread_barrier_t barrier;assert(!pthread_barrier_init(&barrier,NULL,3));
    Racing r[2]={{.session=s,.barrier=&barrier},{.session=s,.barrier=&barrier}};pthread_t threads[2];
    for(unsigned i=0;i<2;i++) assert(!pthread_create(&threads[i],NULL,compete,&r[i]));
    pthread_barrier_wait(&barrier);
    for(unsigned i=0;i<2;i++) assert(!pthread_join(threads[i],NULL));
    unsigned winner=r[0].status==CETTA_IPC_SESSION_OK ? 0 : 1;
    assert(r[winner].status==CETTA_IPC_SESSION_OK && r[1-winner].status==CETTA_IPC_SESSION_BUSY && !r[1-winner].exchange);
    complete(r[winner].exchange,peer);
    /* A framed but semantically unaccepted reply must still be rejectable. */
    assert(cetta_ipc_exchange_cancel(r[winner].exchange));
    cetta_ipc_exchange_release(r[winner].exchange);cetta_ipc_session_release(s);close(peer);pthread_barrier_destroy(&barrier);
}
int main(void) {
    retained_children();cancelled(false);cancelled(true);abandoned();lost_reply();allocations_refuse_without_consuming();competing_begin();
    puts("IPC sessions: serialized acceptance, retained children/replies, shared exchange lifetime, cancellation, allocation refusal, competing begins and lost-reply refusal passed");
}
