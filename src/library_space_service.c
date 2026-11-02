#define _GNU_SOURCE
#include "library_space_service.h"
#include "atom_wire.h"
#include "library.h"
#include "native_handle.h"
#include "ipc_session.h"
#include "shared_transition.h"
#include <limits.h>
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
        uint64_t left=deadline-now;
        int n=poll(&p,1,left>INT_MAX ? INT_MAX : (int)left);
        if (n > 0) return (p.revents & (events | POLLHUP | POLLERR)) != 0;
        if (n < 0 && errno != EINTR) return false;
    }
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
static const char connection_kind[]="space-connection";
typedef struct { CettaIpcSession *session; } Connection;
CettaIpcSession *cetta_space_service_session_acquire(CettaLibraryContext *ctx,Atom *handle) {
    CettaNativeHandleUse use={0};
    if (!cetta_native_handle_acquire_owned(ctx,connection_kind,handle,CETTA_NATIVE_HANDLE_USE_LIFETIME,&use)) return NULL;
    CettaIpcSession *session=((Connection *)cetta_native_handle_use_resource(&use))->session;
    cetta_ipc_session_retain(session);cetta_native_handle_use_release(&use);return session;
}
static void release_connection(void *raw) { Connection *connection=raw;cetta_ipc_session_release(connection->session);free(connection); }
static void connection_storage(const void *raw,CettaStorageCensus *census) {
    const Connection *connection=raw;
    if (cetta_storage_object(census,connection,sizeof(*connection),CETTA_STORAGE_BLOCK)) cetta_ipc_session_storage(connection->session,census);
}
static bool deadline_arg(Atom *arg,uint64_t *deadline) {
    if (!arg || arg->kind!=ATOM_GROUNDED || arg->ground.gkind!=GV_INT || arg->ground.ival<1) return false;
    uint64_t now=now_ms(),timeout=(uint64_t)arg->ground.ival;
    *deadline=timeout>UINT64_MAX-now ? UINT64_MAX : now+timeout;return true;
}
static bool connection_address(Atom *path,struct sockaddr_un *address) {
    if (!path || path->kind!=ATOM_GROUNDED || path->ground.gkind!=GV_STRING) return false;
    size_t length=atom_string_len(path);*address=(struct sockaddr_un){.sun_family=AF_UNIX};
    if (!length || path->ground.sval[0]!='/' || length>=sizeof(address->sun_path) || memchr(path->ground.sval,0,length)) return false;
    memcpy(address->sun_path,path->ground.sval,length);return true;
}
static int open_connection(const struct sockaddr_un *address,uint64_t deadline) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if (fd<0) return -1;
    if (connect(fd,(const struct sockaddr *)address,sizeof(*address))) {
        int error=0;socklen_t size=sizeof(error);
        if (errno!=EINPROGRESS || !ready(fd,POLLOUT,deadline) || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) || error) {
            close(fd);return -1;
        }
    }
    return fd;
}
static Atom *connect_owned(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    uint64_t deadline;struct sockaddr_un address;
    if (n!=2 || !deadline_arg(args[1],&deadline) || !connection_address(args[0],&address))
        return atom_error(a,atom_symbol(a,"space:connect"),atom_string(a,"expected socket and timeout-ms"));
    cetta_shared_transition_guard_leave(&transition);
    int fd=open_connection(&address,deadline);
    CettaIpcSession *session=fd>=0 ? cetta_ipc_session_new(fd) : NULL;
    if (fd>=0 && !session) close(fd);
    cetta_shared_transition_guard_enter(&transition);
    if (!session) return atom_expr2(a,atom_symbol(a,"SpaceConnectionRefused"),atom_symbol(a,fd<0 ? "connect" : "capacity"));
    Connection *connection=malloc(sizeof(*connection));
    if (!connection) {cetta_ipc_session_release(session);return atom_expr2(a,atom_symbol(a,"SpaceConnectionRefused"),atom_symbol(a,"capacity"));}
    connection->session=session;uint64_t id;
    if (!cetta_native_handle_alloc(ctx,connection_kind,connection,release_connection,&id)) {
        release_connection(connection);return atom_expr2(a,atom_symbol(a,"SpaceConnectionRefused"),atom_symbol(a,"capacity"));
    }
    Atom *handle=NULL;
    if (cetta_native_handle_set_storage(ctx,connection_kind,id,connection_storage)) handle=cetta_native_handle_owned_atom(ctx,a,connection_kind,id);
    if (!handle) {cetta_native_handle_close(ctx,connection_kind,id);return atom_expr2(a,atom_symbol(a,"SpaceConnectionRefused"),atom_symbol(a,"capacity"));}
    return atom_expr2(a,atom_symbol(a,"SpaceConnected"),handle);
}
static Atom *call(CettaLibraryContext *ctx,Arena *a,Atom **args,uint32_t n,bool persistent) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    uint64_t deadline;struct sockaddr_un address;
    if (n!=3 || !deadline_arg(args[2],&deadline) || args[1]->kind!=ATOM_EXPR || args[1]->expr.len<3 ||
        !atom_is_symbol(args[1]->expr.elems[0],"request") || args[1]->expr.elems[1]->kind!=ATOM_GROUNDED ||
        args[1]->expr.elems[1]->ground.gkind!=GV_STRING || (!persistent && !connection_address(args[0],&address)))
        return atom_error(a,atom_symbol(a,"space:call"),atom_string(a,"expected socket, (request id operation ...), timeout-ms"));
    Atom *id=args[1]->expr.elems[1];size_t id_size=atom_string_len(id),size=0;
    unsigned char *identity=malloc(id_size ? id_size : 1),*bytes=NULL;
    if (!identity) return atom_error(a,atom_symbol(a,"space:call"),atom_string(a,"capacity"));
    memcpy(identity,id->ground.sval,id_size);
    size_t maximum=cetta_library_value_byte_limit(ctx);
    CettaAtomWireStatus encoded=cetta_atom_wire_encode_bounded(args[1],maximum,&bytes,&size);
    if (encoded!=ATOM_WIRE_OK) {free(identity);return atom_error(a,atom_symbol(a,"space:call"),atom_string(a,cetta_atom_wire_status_name(encoded)));}
    CettaNativeHandleUse use={0};uint64_t connection_id=0;CettaIpcSession *session=NULL;
    if (persistent) {
        if (!cetta_native_handle_arg(args[0],connection_kind,&connection_id) ||
            !cetta_native_handle_acquire_owned(ctx,connection_kind,args[0],CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use)) {
            Atom *result=transport_result(a,atom_string_n(a,(const char *)identity,id_size),false,"closed-or-busy-connection");
            free(identity);free(bytes);return result;
        }
        session=((Connection *)cetta_native_handle_use_resource(&use))->session;cetta_ipc_session_retain(session);
    }
    cetta_shared_transition_guard_leave(&transition);
    if (!persistent) {
        int fd=open_connection(&address,deadline);
        if (fd>=0) {session=cetta_ipc_session_new(fd);if (!session) close(fd);}
    }
    CettaIpcExchange *exchange=NULL;
    const char *failure="connect";bool ran=false;
    if (session) {
        uint64_t now=now_ms();
        if (now>=deadline) failure="timeout";
        else {
            CettaIpcSessionStatus opened=cetta_ipc_session_begin(session,CETTA_IPC_CSP,bytes,size,deadline-now,maximum,&exchange);
            free(bytes);bytes=NULL;
            if (opened!=CETTA_IPC_SESSION_OK) failure=opened==CETTA_IPC_SESSION_BUSY ? "busy-connection" : opened==CETTA_IPC_SESSION_NOMEM ? "memory" : "unavailable";
            else {
                bool timed_out=false;
                while (!cetta_ipc_exchange_ready(exchange)) {
                    if (now_ms()>=deadline) {timed_out=true;cetta_ipc_exchange_cancel(exchange);break;}
                    cetta_ipc_exchange_pump(exchange,65536);
                    if (cetta_ipc_exchange_ready(exchange)) break;
                    uint64_t clock=now_ms(),end=cetta_ipc_exchange_deadline(exchange);
                    if (end>deadline) end=deadline;
                    if (clock>=end) continue;
                    uint64_t left=end-clock;
                    struct pollfd p={cetta_ipc_exchange_fd(exchange),cetta_ipc_exchange_events(exchange),0};
                    int waited=poll(&p,1,left>INT_MAX ? INT_MAX : (int)left);
                    if (waited<0 && errno!=EINTR) {cetta_ipc_exchange_cancel(exchange);break;}
                }
                ran=cetta_ipc_exchange_sent(exchange);failure=timed_out ? "timeout" : cetta_ipc_exchange_error(exchange);
            }
        }
    }
    free(bytes);
    cetta_shared_transition_guard_enter(&transition);
    Atom *result=NULL,*owned_id=atom_string_n(a,(const char *)identity,id_size);free(identity);
    if (exchange && !failure) {
        size_t length;const unsigned char *reply=cetta_ipc_exchange_reply(exchange,&length);
        if (cetta_atom_wire_decode_bounded(a,reply,length,maximum,&result)!=ATOM_WIRE_OK || !result || result->kind!=ATOM_EXPR ||
            result->expr.len!=4 || !atom_is_symbol(result->expr.elems[0],"reply") || !same_text(owned_id,result->expr.elems[1]) ||
            !(atom_is_symbol(result->expr.elems[2],"ok") || atom_is_symbol(result->expr.elems[2],"error")) || !cetta_ipc_exchange_accept(exchange)) {
            result=NULL;failure="malformed-reply";cetta_ipc_exchange_cancel(exchange);
        }
    }
    if (!result && persistent && exchange) cetta_native_handle_close(ctx,connection_kind,connection_id);
    cetta_ipc_exchange_release(exchange);cetta_ipc_session_release(session);cetta_native_handle_use_release(&use);
    return result ? result : transport_result(a,owned_id,ran,failure ? failure : "unavailable");
}

Atom *cetta_space_service_dispatch(CettaLibraryContext *ctx,Arena *a, Atom *head, Atom **args, uint32_t n) {
    if (!a || !head) return NULL;
    if (atom_is_symbol(head,"__cetta_lib_space_connect")) return connect_owned(ctx,a,args,n);
    if (atom_is_symbol(head,"__cetta_lib_space_exchange")) return call(ctx,a,args,n,true);
    if (atom_is_symbol(head,"__cetta_lib_space_disconnect")) {
        CettaNativeHandleUse use={0};uint64_t id;
        if (n!=1 || !cetta_native_handle_arg(args[0],connection_kind,&id) ||
            !cetta_native_handle_acquire_owned(ctx,connection_kind,args[0],CETTA_NATIVE_HANDLE_USE_LIFETIME,&use))
            return atom_expr2(a,atom_symbol(a,"SpaceConnectionRefused"),atom_symbol(a,"closed-or-busy"));
        cetta_native_handle_close(ctx,connection_kind,id);cetta_native_handle_use_release(&use);
        return atom_symbol(a,"SpaceDisconnected");
    }
    if (atom_is_symbol(head,"__cetta_lib_space_id")) {
        /* Unit gives explicit evaluators an ordinary native call. Keep the
         * original zero-argument form for existing clients. */
        if (n && !(n == 1 && args[0]->kind == ATOM_EXPR &&
                   args[0]->expr.len == 0))
            return atom_error(a,head,atom_string(a,"request-id takes no arguments or unit"));
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
    return call(ctx,a, args, n,false);
}
