#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc<4 || argc>6) {
        fputs("Usage: cetta-telegram-control SOCKET status CHAT.THREAD\n"
              "       cetta-telegram-control SOCKET release REQUEST_ID CHAT.THREAD BATCH\n"
              "       cetta-telegram-control SOCKET receipt REQUEST_ID\n",stderr);
        return 64;
    }
    struct stat st; struct sockaddr_un address={.sun_family=AF_UNIX};
    if (strlen(argv[1])>=sizeof(address.sun_path) || lstat(argv[1],&st) ||
        !S_ISSOCK(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077)) {
        fputs("control: private operator socket required\n",stderr); return 1;
    }
    strcpy(address.sun_path,argv[1]); char packet[256]="CTC1"; size_t used=4;
    for (int i=2;i<argc;++i) {
        size_t n=strlen(argv[i]);
        if (!n || n+used+(i>2)>=sizeof(packet)) return 64;
        for (size_t j=0;j<n;++j) if (argv[i][j]<33 || argv[i][j]>126) return 64;
        if (i>2) packet[used++]=' ';
        memcpy(packet+used,argv[i],n); used+=n;
    }
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); struct timeval timeout={5,0};
    int result=1;
    if (fd<0) goto failed;
    if (setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) ||
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) ||
        connect(fd,(struct sockaddr *)&address,sizeof(address))) goto failed;
    struct ucred peer; socklen_t n=sizeof(peer);
    if (getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n) || n!=sizeof(peer) || peer.uid!=geteuid()) goto failed;
    if (send(fd,packet,used,MSG_NOSIGNAL)!=(ssize_t)used) goto failed;
    struct iovec iov={packet,sizeof(packet)-1}; struct msghdr message={.msg_iov=&iov,.msg_iovlen=1};
    ssize_t size=recvmsg(fd,&message,0);
    if (size<=4 || message.msg_flags&MSG_TRUNC || memcmp(packet,"CTC1",4)) goto failed;
    for (ssize_t i=4;i<size;++i) if (packet[i]<32 || packet[i]>126) goto failed;
    packet[size]=0; puts(packet+4); result=0;
failed:
    if (fd>=0) close(fd);
    if (result) fputs("control: request not acknowledged; query its receipt before retrying\n",stderr);
    return result;
}
