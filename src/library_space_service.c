#define _GNU_SOURCE
#include "library_space_service.h"
#include "atom_wire.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
static bool ready(int fd, short events, uint64_t deadline) {
    for (;;) {
        uint64_t now = now_ms();
        if (now >= deadline) return false;
        struct pollfd p = {fd, events, 0};
        int n = poll(&p, 1, (int)(deadline - now));
        if (n > 0) return (p.revents & (events | POLLHUP | POLLERR)) != 0;
        if (n < 0 && errno != EINTR) return false;
    }
}
static bool transfer(int fd, void *buffer, size_t size, bool writing,
                     uint64_t deadline, size_t *progress) {
    unsigned char *bytes = buffer;
    for (size_t pos = 0; pos < size;) {
        if (now_ms() >= deadline) return false;
        ssize_t n = writing ? send(fd, bytes + pos, size - pos, MSG_NOSIGNAL)
                            : recv(fd, bytes + pos, size - pos, 0);
        if (n > 0) { pos += (size_t)n; if (writing) *progress += (size_t)n; }
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!ready(fd, writing ? POLLOUT : POLLIN, deadline)) return false;
        } else return false;
    }
    return true;
}
static Atom *transport_result(Arena *a, Atom *id, bool uncertain, const char *reason) {
    return atom_expr3(a, atom_symbol(a, uncertain ? "space:uncertain" : "space:unavailable"),
                     id, atom_symbol(a, reason));
}
static bool same_text(Atom *left, Atom *right) {
    return left && right && left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED &&
        left->ground.gkind == GV_STRING && right->ground.gkind == GV_STRING &&
        atom_string_equal(left, right);
}
static Atom *call(Arena *a, Atom **args, uint32_t n) {
    Atom *source = atom_symbol(a, "space:call");
    if (n != 3 || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_STRING ||
        args[2]->kind != ATOM_GROUNDED || args[2]->ground.gkind != GV_INT ||
        args[2]->ground.ival < 1 || args[2]->ground.ival > 60000 ||
        args[1]->kind != ATOM_EXPR || args[1]->expr.len < 3 ||
        !atom_is_symbol(args[1]->expr.elems[0], "request") ||
        args[1]->expr.elems[1]->kind != ATOM_GROUNDED ||
        args[1]->expr.elems[1]->ground.gkind != GV_STRING)
        return atom_error(a, source, atom_string(a, "expected socket, (request id operation ...), timeout-ms"));
    Atom *id = args[1]->expr.elems[1];
    size_t path_len = atom_string_len(args[0]);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    const char *path = args[0]->ground.sval;
    if (!path_len || path[0] != '/' || path_len >= sizeof(address.sun_path) ||
        memchr(path, 0, path_len))
        return atom_error(a, source, atom_string(a, "socket must be a short absolute path"));
    memcpy(address.sun_path, path, path_len); address.sun_path[path_len] = 0;
    unsigned char *bytes = NULL;
    size_t size = 0, progress = 0;
    CettaAtomWireStatus status = cetta_atom_wire_encode(args[1], &bytes, &size);
    if (status != ATOM_WIRE_OK)
        return atom_error(a, source, atom_string(a, cetta_atom_wire_status_name(status)));
    unsigned char header[8] = {'C', 'S', 'P', '1'};
    for (size_t i = 0; i < 4; ++i) header[4 + i] = (unsigned char)(size >> (24 - 8 * i));
    uint64_t deadline = now_ms() + (uint64_t)args[2]->ground.ival;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    Atom *result = NULL;
    const char *failure = "connect";
    if (fd < 0) { failure = "socket"; goto done; }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if (errno != EINPROGRESS || !ready(fd, POLLOUT, deadline)) goto done;
        int error = 0; socklen_t len = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) || error) goto done;
    }
    failure = "send";
    if (!transfer(fd, header, 8, true, deadline, &progress) ||
        !transfer(fd, bytes, size, true, deadline, &progress)) goto done;
    free(bytes); bytes = NULL;
    failure = "timeout-or-disconnect";
    if (!transfer(fd, header, 8, false, deadline, &progress)) goto done;
    failure = "malformed-reply";
    if (memcmp(header, "CSP1", 4)) goto done;
    size = 0;
    for (size_t i = 4; i < 8; ++i) size = (size << 8) | header[i];
    if (size < 4 || size > CETTA_ATOM_WIRE_BYTES) goto done;
    bytes = malloc(size);
    if (!bytes) { failure = "memory"; goto done; }
    if (!transfer(fd, bytes, size, false, deadline, &progress)) {
        failure = "timeout-or-disconnect"; goto done;
    }
    if (cetta_atom_wire_decode(a, bytes, size, &result) != ATOM_WIRE_OK ||
        !result || result->kind != ATOM_EXPR || result->expr.len != 4 ||
        !atom_is_symbol(result->expr.elems[0], "reply") ||
        !same_text(id, result->expr.elems[1]) ||
        !(atom_is_symbol(result->expr.elems[2], "ok") ||
          atom_is_symbol(result->expr.elems[2], "error"))) {
        result = NULL; goto done;
    }
done:
    if (fd >= 0) close(fd);
    free(bytes);
    return result ? result : transport_result(a, id, progress != 0, failure);
}

Atom *cetta_space_service_dispatch(Arena *a, Atom *head, Atom **args, uint32_t n) {
    if (!a || !head) return NULL;
    if (atom_is_symbol(head,"__cetta_lib_space_id")) {
        if (n) return atom_error(a,head,atom_string(a,"request-id takes no arguments"));
        unsigned char bytes[16]; size_t used=0;
        while (used<sizeof(bytes)) {
            ssize_t got=getrandom(bytes+used,sizeof(bytes)-used,0);
            if (got<0 && errno==EINTR) continue;
            if (got<=0) return atom_error(a,head,atom_string(a,"request-id unavailable"));
            used+=(size_t)got;
        }
        static const char hex[]="0123456789abcdef";
        char text[33];
        for (size_t i=0;i<16;++i) { text[2*i]=hex[bytes[i]>>4]; text[2*i+1]=hex[bytes[i]&15]; }
        text[32]=0; return atom_string(a,text);
    }
    if (!atom_is_symbol(head, "__cetta_lib_space_call")) return NULL;
    return call(a, args, n);
}
