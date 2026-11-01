#define _GNU_SOURCE
#include "ipc_client.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

bool cetta_ipc_csp_header(unsigned char header[8],size_t bytes) {
    if (bytes<4 || bytes>UINT32_MAX) return false;
    memcpy(header,"CSP1",4);
    for (size_t i=0;i<4;i++) header[4+i]=(unsigned char)(bytes>>(24-8*i));
    return true;
}
bool cetta_ipc_csp_size(const unsigned char header[8],size_t maximum,size_t *bytes) {
    if (!bytes || memcmp(header,"CSP1",4)) return false;
    size_t size=0;for (size_t i=4;i<8;i++) size=(size<<8)|header[i];
    if (size<4 || size>maximum) return false;
    *bytes=size;return true;
}
int cetta_ipc_listen(const char *path,mode_t mode,gid_t group,int backlog) {
    struct sockaddr_un address={.sun_family=AF_UNIX};
    size_t n=path ? strlen(path) : 0;
    if (!n || path[0]!='/' || n>=sizeof(address.sun_path)) return -1;
    memcpy(address.sun_path,path,n+1);struct stat st;
    if (!lstat(path,&st)) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) return -1;
        int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if (probe<0) return -1;
        int result=connect(probe,(struct sockaddr *)&address,sizeof(address));
        int saved=errno;close(probe);
        if (!result || (saved!=ECONNREFUSED && saved!=ENOENT)) return -1;
        if (unlink(path)) return -1;
    } else if (errno!=ENOENT) return -1;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if (fd<0) return -1;
    if (bind(fd,(struct sockaddr *)&address,sizeof(address)) ||
        (group!=(gid_t)-1 && chown(path,(uid_t)-1,group)) || chmod(path,mode) || listen(fd,backlog)) {
        close(fd);return -1;
    }
    return fd;
}

struct CettaIpcClient {
    int fd;
    CettaIpcFraming framing;
    enum { CONNECTING, SENDING, HEADER, RECEIVING, FINISHED } phase;
    unsigned char *request, *reply, header[8];
    size_t request_size, sent, received, header_used, reply_size, max_reply;
    uint64_t deadline;
    const char *error;
};

static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}
static void finish(CettaIpcClient *c, const char *error) {
    c->error = error; c->phase = FINISHED;
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
}

CettaIpcClient *cetta_ipc_client_new(const char *path, CettaIpcFraming framing,
    const void *request, size_t size, uint64_t timeout, size_t max_reply) {
    if (!path || !request || !size || !max_reply) return NULL;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (!path[0] || strlen(path) >= sizeof(address.sun_path)) return NULL;
    CettaIpcClient *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = -1; c->framing = framing; c->max_reply = max_reply;
    c->deadline = timeout ? now_ms() + timeout : UINT64_MAX;
    c->request_size = size + (framing == CETTA_IPC_CSP ? 8 : 0);
    c->request = malloc(c->request_size);
    if (!c->request) { free(c); return NULL; }
    if (framing == CETTA_IPC_CSP) {
        memcpy(c->request, "CSP1", 4);
        for (size_t i = 0; i < 4; ++i) c->request[4+i] = (unsigned char)(size >> (24-8*i));
        memcpy(c->request + 8, request, size);
    } else memcpy(c->request, request, size);
    memcpy(address.sun_path, path, strlen(path) + 1);
    c->fd = socket(AF_UNIX, (framing == CETTA_IPC_CWP ? SOCK_SEQPACKET : SOCK_STREAM)
                   | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    c->phase = SENDING;
    if (c->fd < 0) finish(c, "socket");
    else if (connect(c->fd, (struct sockaddr *)&address, sizeof(address))) {
        if (errno == EINPROGRESS) c->phase = CONNECTING;
        else finish(c, errno == ENOENT || errno == ECONNREFUSED ? "absent" : "connect");
    }
    return c;
}

void cetta_ipc_client_pump(CettaIpcClient *c) {
    if (!c || c->phase == FINISHED) return;
    if (now_ms() >= c->deadline) { finish(c, "timeout"); return; }
    if (c->phase == CONNECTING) {
        struct pollfd p = {c->fd, POLLOUT, 0};
        if (poll(&p, 1, 0) <= 0) return;
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            finish(c, "connect"); return;
        }
        c->phase = SENDING;
    }
    while (c->phase == SENDING) {
        ssize_t n = send(c->fd, c->request + c->sent, c->request_size - c->sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0 || (c->framing == CETTA_IPC_CWP && (size_t)n != c->request_size)) {
            finish(c, "send"); return;
        }
        c->sent += (size_t)n;
        if (c->sent == c->request_size) {
            free(c->request); c->request = NULL;
            c->phase = c->framing == CETTA_IPC_CSP ? HEADER : RECEIVING;
        }
    }
    while (c->phase == HEADER) {
        ssize_t n = recv(c->fd, c->header + c->header_used, 8 - c->header_used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) { finish(c, "disconnect"); return; }
        c->header_used += (size_t)n;
        if (c->header_used != 8) continue;
        if (memcmp(c->header, "CSP1", 4)) { finish(c, "malformed-reply"); return; }
        for (size_t i = 4; i < 8; ++i) c->reply_size = (c->reply_size << 8) | c->header[i];
        if (c->reply_size < 4 || c->reply_size > c->max_reply) {
            finish(c, "malformed-reply"); return;
        }
        c->phase = RECEIVING;
    }
    if (!c->reply) {
        c->reply = malloc(c->framing == CETTA_IPC_CWP ? c->max_reply : c->reply_size);
        if (!c->reply) { finish(c, "memory"); return; }
    }
    while (c->phase == RECEIVING) {
        size_t room = c->framing == CETTA_IPC_CWP ? c->max_reply : c->reply_size - c->received;
        ssize_t n = recv(c->fd, c->reply + c->received, room,
                         c->framing == CETTA_IPC_CWP ? MSG_TRUNC : 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) { finish(c, "disconnect"); return; }
        if ((size_t)n > room) { finish(c, "malformed-reply"); return; }
        c->received += (size_t)n;
        if (c->framing == CETTA_IPC_CWP) c->reply_size = c->received;
        if (c->received == c->reply_size) { finish(c, NULL); return; }
    }
}

int cetta_ipc_client_fd(const CettaIpcClient *c) { return c ? c->fd : -1; }
short cetta_ipc_client_events(const CettaIpcClient *c) {
    return c && (c->phase == CONNECTING || c->phase == SENDING) ? POLLOUT : POLLIN;
}
uint64_t cetta_ipc_client_deadline(const CettaIpcClient *c) { return c ? c->deadline : UINT64_MAX; }
bool cetta_ipc_client_ready(const CettaIpcClient *c) { return c && c->phase == FINISHED; }
bool cetta_ipc_client_sent(const CettaIpcClient *c) { return c && c->sent != 0; }
const char *cetta_ipc_client_error(const CettaIpcClient *c) { return c ? c->error : "unavailable"; }
const unsigned char *cetta_ipc_client_reply(const CettaIpcClient *c, size_t *size) {
    if (size) *size = c ? c->reply_size : 0;
    return c ? c->reply : NULL;
}
void cetta_ipc_client_free(CettaIpcClient *c) {
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    free(c->request); free(c->reply); free(c);
}
