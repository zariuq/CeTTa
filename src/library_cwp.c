#define _GNU_SOURCE
#include "library_cwp.h"
#include "symbol.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum { CWP_NAME_MAX = 64, CWP_BODY_MAX = 65536, CWP_REPLY_MAX = 6 + 255 + CWP_BODY_MAX };

static const char *text(Atom *a) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING ? a->ground.sval : NULL;
}
static bool integer(Atom *a, int64_t lo, int64_t hi, int64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < lo || a->ground.ival > hi) return false;
    *out = a->ground.ival; return true;
}
static Atom *failure(Arena *arena, const char *message) {
    Atom *operation = atom_symbol(arena, "cwp:call");
    return atom_error(arena, atom_expr(arena, &operation, 1u), atom_string(arena, message));
}
static Atom *unavailable(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "cwp:unavailable"), atom_symbol(arena, reason));
}
static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
/* Wait for the socket to be ready, never past the deadline. */
static bool ready(int fd, short events, uint64_t deadline) {
    for (;;) {
        uint64_t now = now_ms();
        if (now >= deadline) return false;
        struct pollfd p = {fd, events, 0};
        int n = poll(&p, 1, (int)(deadline - now));
        if (n > 0) return true;
        if (n < 0 && errno != EINTR) return false;
    }
}

static Atom *cwp_call(Arena *arena, Atom **args, uint32_t nargs) {
    int64_t code, timeout;
    const char *path = nargs == 5u ? text(args[0]) : NULL;
    const char *name = nargs == 5u ? text(args[2]) : NULL;
    const char *body = nargs == 5u ? text(args[3]) : NULL;
    if (!path || !name || !body || !integer(args[1], 1, 63, &code) ||
        !integer(args[4], 1, 60000, &timeout))
        return failure(arena, "expected (cwp:call path request-code name body timeout-ms)");
    size_t path_len = strlen(path), name_len = strlen(name), body_len = strlen(body);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (path[0] != '/' || path_len >= sizeof(addr.sun_path))
        return failure(arena, "the socket path must be absolute and short");
    if (name_len > CWP_NAME_MAX || body_len > CWP_BODY_MAX)
        return failure(arena, "request too large");
    for (size_t i = 0; i < name_len; ++i)
        if ((unsigned char)name[i] < 33 || (unsigned char)name[i] > 126)
            return failure(arena, "the name must be printable ASCII");
    memcpy(addr.sun_path, path, path_len + 1);
    uint64_t deadline = now_ms() + (uint64_t)timeout;
    size_t size = 6 + name_len + body_len;
    unsigned char *packet = malloc(size > CWP_REPLY_MAX ? size : CWP_REPLY_MAX);
    if (!packet) return failure(arena, "out of memory");
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    Atom *result = NULL;
    if (fd < 0) { result = unavailable(arena, "socket"); goto done; }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) &&
        (errno != EINPROGRESS || !ready(fd, POLLOUT, deadline))) {
        result = unavailable(arena, errno == ENOENT || errno == ECONNREFUSED ? "absent" : "connect");
        goto done;
    }
    memcpy(packet, "CWP1", 4); packet[4] = (unsigned char)code; packet[5] = (unsigned char)name_len;
    memcpy(packet + 6, name, name_len); memcpy(packet + 6 + name_len, body, body_len);
    ssize_t sent;
    while ((sent = send(fd, packet, size, MSG_NOSIGNAL)) < 0 && (errno == EAGAIN || errno == EINTR))
        if (!ready(fd, POLLOUT, deadline)) break;
    if (sent != (ssize_t)size) { result = unavailable(arena, "send"); goto done; }
    ssize_t got;
    while ((got = recv(fd, packet, CWP_REPLY_MAX, 0)) < 0 && (errno == EAGAIN || errno == EINTR))
        if (!ready(fd, POLLIN, deadline)) break;
    if (got < 0) { result = unavailable(arena, "timeout"); goto done; }
    if (got < 6 || memcmp(packet, "CWP1", 4) || (size_t)got < 6u + packet[5]) {
        result = unavailable(arena, "malformed"); goto done;
    }
    size_t reply_name = packet[5], reply_body = (size_t)got - 6 - reply_name;
    if (memchr(packet + 6, 0, reply_name + reply_body)) { result = unavailable(arena, "malformed"); goto done; }
    char *reply_text = arena_alloc(arena, reply_name + reply_body + 2);
    memcpy(reply_text, packet + 6, reply_name); reply_text[reply_name] = 0;
    memcpy(reply_text + reply_name + 1, packet + 6 + reply_name, reply_body);
    reply_text[reply_name + 1 + reply_body] = 0;
    Atom *parts[] = {atom_symbol(arena, "cwp:reply"), atom_int(arena, packet[4]),
                     atom_string(arena, reply_text), atom_string(arena, reply_text + reply_name + 1)};
    result = atom_expr(arena, parts, 4u);
done:
    if (fd >= 0) close(fd);
    free(packet);
    return result;
}

Atom *cetta_cwp_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    if (!arena || !head || head->kind != ATOM_SYMBOL || atom_symbol_current_id(head) != g_builtin_syms.lib_cwp_call)
        return NULL;
    return cwp_call(arena, args, nargs);
}
