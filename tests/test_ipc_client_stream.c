#define _GNU_SOURCE
#include "ipc_client.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static void pair(int fd[2],int type) {assert(!socketpair(AF_UNIX,type|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));}
static size_t read_available(int fd,unsigned char *out,size_t n) {
    ssize_t got=recv(fd,out,n,0);
    if (got<0) {assert(errno==EAGAIN || errno==EWOULDBLOCK);return 0;}
    assert(got>0);return (size_t)got;
}
static void request(CettaIpcClient *c,int peer) {
    unsigned char frame[12];size_t at=0;
    for(unsigned i=0;at<sizeof(frame) && i<100;i++) {
        cetta_ipc_client_pump_bounded(c,1);at+=read_available(peer,frame+at,sizeof(frame)-at);
    }
    assert(at==12 && !memcmp(frame,"CSP1\0\0\0\4ping",12));
    size_t n;const unsigned char *bytes=cetta_ipc_client_request(c,&n);
    assert(bytes && n==4 && !memcmp(bytes,"ping",4));
    assert(!cetta_ipc_client_ready(c) && cetta_ipc_client_sent(c));
    assert(!cetta_ipc_client_reply(c,&n) && !n);
}
static void fragmented_reuse(void) {
    int fd[2];pair(fd,SOCK_STREAM);
    for(unsigned turn=0;turn<3;turn++) {
        CettaIpcClient *c=cetta_ipc_client_new_connected(fd[0],CETTA_IPC_CSP,"ping",4,UINT64_MAX,1024);assert(c);
        assert(cetta_ipc_client_deadline(c)==UINT64_MAX);
        cetta_ipc_client_pump_bounded(c,0);
        unsigned char unused;assert(!read_available(fd[1],&unused,1));
        request(c,fd[1]);
        const unsigned char reply[]="CSP1\0\0\0\4pong";
        for(size_t i=0;i<sizeof(reply)-1;i++) {
            assert(send(fd[1],reply+i,1,MSG_NOSIGNAL)==1);
            cetta_ipc_client_pump_bounded(c,1);
            assert(cetta_ipc_client_ready(c)==(i==sizeof(reply)-2));
        }
        assert(!cetta_ipc_client_error(c) && !cetta_ipc_client_events(c));
        size_t n;const unsigned char *bytes=cetta_ipc_client_reply(c,&n);
        assert(bytes && n==4 && !memcmp(bytes,"pong",4));
        fd[0]=cetta_ipc_client_take_connection(c);assert(fd[0]>=0 && cetta_ipc_client_take_connection(c)==-1);
        cetta_ipc_client_free(c);
    }
    close(fd[0]);close(fd[1]);
}
static void interrupted(bool sent,bool partial) {
    int fd[2];pair(fd,SOCK_STREAM);
    CettaIpcClient *c=cetta_ipc_client_new_connected(fd[0],CETTA_IPC_CSP,"ping",4,1000,1024);assert(c);
    if(sent) request(c,fd[1]);
    if(partial) {
        assert(sent && send(fd[1],"CSP1\0\0\0\4po",10,MSG_NOSIGNAL)==10);
        cetta_ipc_client_pump(c);assert(!cetta_ipc_client_ready(c));
    }
    close(fd[1]);cetta_ipc_client_pump(c);
    assert(cetta_ipc_client_ready(c) && cetta_ipc_client_error(c));
    assert(cetta_ipc_client_sent(c)==sent && cetta_ipc_client_fd(c)==-1 && cetta_ipc_client_take_connection(c)==-1);
    size_t n;assert(!cetta_ipc_client_reply(c,&n) && !n);cetta_ipc_client_free(c);
}
static void malformed(void) {
    int fd[2];pair(fd,SOCK_STREAM);
    CettaIpcClient *c=cetta_ipc_client_new_connected(fd[0],CETTA_IPC_CSP,"ping",4,1000,4);assert(c);request(c,fd[1]);
    assert(send(fd[1],"CSP1\0\0\0\5",8,MSG_NOSIGNAL)==8);cetta_ipc_client_pump(c);
    assert(cetta_ipc_client_ready(c) && !strcmp(cetta_ipc_client_error(c),"malformed-reply"));
    assert(cetta_ipc_client_fd(c)==-1);cetta_ipc_client_free(c);close(fd[1]);
}
static void packet(void) {
    int fd[2];pair(fd,SOCK_SEQPACKET);
    CettaIpcClient *c=cetta_ipc_client_new_connected(fd[0],CETTA_IPC_CWP,"ping",4,1000,4);assert(c);
    cetta_ipc_client_pump_bounded(c,1);char bytes[4];assert(recv(fd[1],bytes,4,0)==4 && !memcmp(bytes,"ping",4));
    assert(send(fd[1],"pong",4,MSG_NOSIGNAL)==4);cetta_ipc_client_pump_bounded(c,1);
    assert(cetta_ipc_client_ready(c) && !cetta_ipc_client_error(c));
    fd[0]=cetta_ipc_client_take_connection(c);assert(fd[0]>=0);cetta_ipc_client_free(c);close(fd[0]);close(fd[1]);
}
static void cancellation(void) {
    int fd[2];pair(fd,SOCK_STREAM);
    assert(!cetta_ipc_client_new_connected(fd[0],(CettaIpcFraming)99,"ping",4,1000,4));
    assert(fcntl(fd[0],F_GETFL)>=0); /* Refusal did not consume the caller's fd. */
    CettaIpcClient *c=cetta_ipc_client_new_connected(fd[0],CETTA_IPC_CSP,"ping",4,1000,1024);assert(c);
    cetta_ipc_client_free(c);char byte;assert(recv(fd[1],&byte,1,0)==0);close(fd[1]);
}
int main(void) {
    fragmented_reuse();interrupted(false,false);interrupted(true,false);interrupted(true,true);
    malformed();packet();cancellation();
    puts("IPC stream: fragmented bounded IO, connection reuse, uncertain delivery, malformed replies, packet framing and cancellation passed");
}
