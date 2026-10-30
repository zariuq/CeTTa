#define _GNU_SOURCE
#include "value_service.h"
#include "atom_wire.h"
#include "eval.h"
#include "ipc_client.h"
#include "native_handle.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct { unsigned char *data;size_t size; } Buffer;
typedef struct {
    int fd;uid_t uid;bool readonly;
    unsigned char header[8];size_t header_read,input_read,sent;
    Buffer input,output,last_request,last_reply;
    Arena cache;Atom *last_id;uint64_t deadline;
} Connection;
typedef struct { bool live;size_t client;Arena arena;Atom *handle;char kind[128],token[64];uint64_t id; } Resource;
typedef struct { Atom *operation;const char *entry;bool write; } Route;
typedef struct {
    CettaLibraryContext *context;Space *program;Registry *registry;Arena *permanent;
    Arena configuration;Atom *data;const char *path;
    Route *routes;size_t route_count,client_count,resource_count,frame,owned_limit;
    uint64_t idle,next_resource;int fuel;char boot[33];
    Connection *clients;Resource *resources;
    const char *evaluation_failure;
} Host;
static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number;stopping=1; }
static uint64_t now_ms(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void clear(Buffer *buffer) { free(buffer->data);*buffer=(Buffer){0}; }
static bool copy(Buffer *out,Buffer source) {
    out->data=malloc(source.size ? source.size : 1);
    if (!out->data) return false;
    memcpy(out->data,source.data,source.size);out->size=source.size;return true;
}
static bool tagged(Atom *a,const char *name,size_t count) { return a && a->kind==ATOM_EXPR && a->expr.len==count && atom_is_symbol(a->expr.elems[0],name); }
static const char *text(Atom *a) { return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING && !memchr(a->ground.sval,0,atom_string_len(a)) ? a->ground.sval : NULL; }
static bool natural(Atom *a,uint64_t *value) { if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT || a->ground.ival<0) return false;*value=(uint64_t)a->ground.ival;return true; }
static Atom *failure(Arena *a,bool ran,const char *reason) { return atom_expr2(a,atom_symbol(a,ran ? "ServiceFailedV1" : "ServiceRefusedV1"),atom_symbol(a,reason)); }
static Atom *reply(Arena *a,Atom *id,bool ok,Atom *value) { return atom_expr(a,(Atom *[]){atom_symbol(a,"reply"),id,atom_symbol(a,ok ? "ok" : "error"),value},4); }
static bool encode(Atom *atom,Buffer *out) { return cetta_atom_wire_encode(atom,&out->data,&out->size)==ATOM_WIRE_OK; }
static const char *incomplete_reason(CettaEvalCompletion completion) {
    switch (completion) {
    case CETTA_EVAL_INCOMPLETE_FUEL: return "handler-fuel";
    case CETTA_EVAL_INCOMPLETE_CANCELLED: return "handler-cancelled";
    case CETTA_EVAL_INCOMPLETE_STACK: return "handler-stack";
    case CETTA_EVAL_INCOMPLETE_CAPACITY: return "handler-capacity";
    case CETTA_EVAL_INCOMPLETE_INVALIDATED: return "handler-invalidated";
    case CETTA_EVAL_INCOMPLETE_HOST_FAILURE: return "handler-host-failure";
    case CETTA_EVAL_INCOMPLETE_EFFECT_DENIED: return "handler-effect-denied";
    case CETTA_EVAL_INCOMPLETE_PAUSED: return "handler-paused";
    default: return "handler-incomplete";
    }
}

static Atom *evaluate(Host *host,Arena *arena,const char *entry,Atom *input) {
    Atom *name=atom_symbol(arena,entry);
    Atom *call=input ? atom_expr2(arena,name,atom_expr2(arena,atom_symbol(arena,"quote"),input)) : atom_expr(arena,&name,1);
    EvalOutcome outcome;eval_outcome_init(&outcome);
    int previous=host->context->session.options.fuel_limit;
    host->context->session.options.fuel_limit=host->fuel;
    eval_top_with_registry_outcome(host->program,arena,host->permanent,host->registry,call,&outcome,NULL,NULL);
    host->context->session.options.fuel_limit=previous;
    Atom *result=outcome.completion==CETTA_EVAL_COMPLETE && !outcome.petta_raised_error && outcome.results.len==1 && !atom_is_error(outcome.results.items[0])
        ? atom_deep_copy(arena,outcome.results.items[0]) : NULL;
    host->evaluation_failure=outcome.completion!=CETTA_EVAL_COMPLETE ? incomplete_reason(outcome.completion)
        : outcome.petta_raised_error ? "handler-raised-error"
        : outcome.results.len!=1 ? "handler-nonsingle-result"
        : !result ? "handler-error" : NULL;
    eval_outcome_free(&outcome);eval_release_temporary_spaces();eval_reset_form_gc_survivor();return result;
}
static bool handle_info(Host *host,Atom *handle,char kind[128],uint64_t *id,CettaNativeHandleUse *use) {
    if (!tagged(handle,"NativeHandle",3)) return false;
    const char *name=text(handle->expr.elems[1]);
    if (!name || strlen(name)>=128 || !cetta_native_handle_arg(handle,name,id) ||
        !cetta_native_handle_acquire_owned(host->context,name,handle,CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,use)) return false;
    strcpy(kind,name);return true;
}
static void resource_close(Host *host,Resource *resource) {
    if (!resource->live) return;
    cetta_native_handle_close(host->context,resource->kind,resource->id);
    arena_free(&resource->arena);memset(resource,0,sizeof(*resource));
}
static Resource *resource_find(Host *host,size_t client,Atom *token) {
    const char *name=text(token);if (!name) return NULL;
    for (size_t i=0;i<host->resource_count;i++) if (host->resources[i].live && host->resources[i].client==client && !strcmp(host->resources[i].token,name)) return &host->resources[i];
    return NULL;
}
static bool resources_fit(Host *host) {
    CettaStorageCensus census={0};bool ready=true;
    for (size_t i=0;ready && i<host->resource_count;i++) if (host->resources[i].live) {
        Resource *r=&host->resources[i];CettaNativeHandleUse use={0};
        ready=cetta_native_handle_acquire_owned(host->context,r->kind,r->handle,CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use);
        if (ready) cetta_native_handle_use_storage(&use,&census);
        cetta_native_handle_use_release(&use);
    }
    cetta_storage_finish(&census);ready=ready && !census.incomplete && !census.overflow && !census.invalidated && census.bytes<=host->owned_limit;
    cetta_storage_census_free(&census);return ready;
}
static void connection_close(Host *host,size_t index) {
    Connection *c=&host->clients[index];
    for (size_t i=0;host->resources && i<host->resource_count;i++) if (host->resources[i].live && host->resources[i].client==index) resource_close(host,&host->resources[i]);
    if (c->fd>=0) close(c->fd);
    clear(&c->input);clear(&c->output);clear(&c->last_request);clear(&c->last_reply);arena_free(&c->cache);
    *c=(Connection){.fd=-1};
}
static bool queue(Host *host,Connection *c,Buffer body) {
    if (body.size>host->frame || body.size>SIZE_MAX-8) return false;
    c->output.data=malloc(body.size+8);if (!c->output.data) return false;
    c->output.size=body.size+8;c->sent=0;
    if (!cetta_ipc_csp_header(c->output.data,body.size)) { clear(&c->output);return false; }
    memcpy(c->output.data+8,body.data,body.size);c->deadline=now_ms()+host->idle;return true;
}
static Route *route_find(Host *host,Atom *operation) {
    for (size_t i=0;i<host->route_count;i++) if (atom_eq(host->routes[i].operation,operation)) return &host->routes[i];
    return NULL;
}
static Atom *keep(Host *host,size_t client,Arena *arena,Atom *result,Resource **added) {
    Atom *handle=result->expr.elems[1],*metadata=result->expr.elems[2];char kind[128];uint64_t id;
    CettaNativeHandleUse use={0};
    if (!handle_info(host,handle,kind,&id,&use)) return failure(arena,true,"unowned-resource");
    Resource *available=NULL,*existing=NULL;
    for (size_t i=0;i<host->resource_count;i++) {
        Resource *r=&host->resources[i];
        if (!r->live && !available) available=r;
        if (r->live && r->id==id && !strcmp(r->kind,kind)) existing=r;
    }
    cetta_native_handle_use_release(&use);
    if (existing && existing->client!=client) return failure(arena,true,"resource-already-owned");
    if (existing) return atom_expr3(arena,atom_symbol(arena,"ServiceResourceV1"),atom_string(arena,existing->token),metadata);
    if (!available || host->next_resource==UINT64_MAX) {
        cetta_native_handle_close(host->context,kind,id);return failure(arena,true,"resource-limit");
    }
    Resource *r=available;arena_init_detached(&r->arena);
    r->handle=atom_deep_copy(&r->arena,handle);r->client=client;r->id=id;strcpy(r->kind,kind);
    snprintf(r->token,sizeof(r->token),"%s-%llx",host->boot,(unsigned long long)host->next_resource++);r->live=true;*added=r;
    if (!r->handle || !resources_fit(host)) { resource_close(host,r);*added=NULL;return failure(arena,true,"resource-budget"); }
    return atom_expr3(arena,atom_symbol(arena,"ServiceResourceV1"),atom_string(arena,r->token),metadata);
}
static Atom *dispatch(Host *host,size_t index,Arena *arena,Atom *body,Resource **added,bool *ran) {
    Connection *c=&host->clients[index];
    if (tagged(body,"ReadOnlyV1",1)) { c->readonly=true;return atom_symbol(arena,"ServiceReadOnlyV1"); }
    if (tagged(body,"StatsV1",1)) {
        size_t clients=0,resources=0;
        for (size_t i=0;i<host->client_count;i++) clients+=host->clients[i].fd>=0;
        for (size_t i=0;i<host->resource_count;i++) resources+=host->resources[i].live;
        return atom_expr(arena,(Atom *[]){atom_symbol(arena,"ServiceStatsV1"),atom_int(arena,clients),atom_int(arena,resources),
            atom_int(arena,(int64_t)arena_accounted_live_bytes(host->permanent))},4);
    }
    if (tagged(body,"DropV1",2)) {
        Resource *r=resource_find(host,index,body->expr.elems[1]);if (!r) return failure(arena,false,"unknown-resource");
        resource_close(host,r);return atom_symbol(arena,"ServiceClosedV1");
    }
    Atom *operation=NULL,*argument=NULL,*resource=atom_symbol(arena,"ServiceNoResourceV1");
    if (tagged(body,"CallV1",3)) { operation=body->expr.elems[1];argument=body->expr.elems[2]; }
    else if (tagged(body,"UseV1",4)) {
        Resource *r=resource_find(host,index,body->expr.elems[1]);if (!r) return failure(arena,false,"unknown-resource");
        operation=body->expr.elems[2];argument=body->expr.elems[3];resource=r->handle;
    } else return failure(arena,false,"request-shape");
    Route *route=route_find(host,operation);
    if (!route) return failure(arena,false,"unknown-operation");
    if (c->readonly && route->write) return failure(arena,false,"read-only");
    Atom *peer=atom_expr3(arena,atom_symbol(arena,"ServicePeerV1"),atom_int(arena,c->uid),atom_symbol(arena,c->readonly ? "ReadOnly" : "ReadWrite"));
    Atom *input=atom_expr(arena,(Atom *[]){atom_symbol(arena,"ServiceCallV1"),host->data,peer,resource,argument},5);
    *ran=true;Atom *result=evaluate(host,arena,route->entry,input);
    if (!result) return failure(arena,true,host->evaluation_failure);
    if (tagged(result,"ServiceKeepV1",3)) return keep(host,index,arena,result,added);
    if (!resources_fit(host)) {
        for (size_t i=0;i<host->resource_count;i++) if (host->resources[i].live && host->resources[i].client==index) resource_close(host,&host->resources[i]);
        return failure(arena,true,"resource-budget");
    }
    return result;
}
static bool respond(Host *host,size_t index) {
    Connection *c=&host->clients[index];Arena arena;arena_init_detached(&arena);Atom *request=NULL;Buffer encoded={0};bool valid=false;
    Resource *added=NULL;bool ran=false;
    if (cetta_atom_wire_decode(&arena,c->input.data,c->input.size,&request)!=ATOM_WIRE_OK || !tagged(request,"request",3)) goto done;
    Atom *id=request->expr.elems[1];
    if (!text(id) || !atom_string_len(id) || atom_string_len(id)>128) goto done;
    if (c->last_id && atom_string_equal(c->last_id,id)) {
        if (c->last_request.size==c->input.size && !memcmp(c->last_request.data,c->input.data,c->input.size)) { valid=queue(host,c,c->last_reply);goto done; }
        if (!encode(reply(&arena,id,false,failure(&arena,false,"request-id-collision")),&encoded)) goto done;
        valid=queue(host,c,encoded);goto done;
    }
    Atom *body=dispatch(host,index,&arena,request->expr.elems[2],&added,&ran);
    bool ok=!tagged(body,"ServiceFailedV1",2) && !tagged(body,"ServiceRefusedV1",2);
    if (!encode(reply(&arena,id,ok,body),&encoded) || encoded.size>host->frame) {
        if (added) { resource_close(host,added);added=NULL; }
        clear(&encoded);
        if (!encode(reply(&arena,id,false,failure(&arena,ran,"reply-limit")),&encoded)) goto done;
    }
    clear(&c->last_request);clear(&c->last_reply);arena_free(&c->cache);arena_init_detached(&c->cache);c->last_id=NULL;
    if (!copy(&c->last_request,c->input) || !copy(&c->last_reply,encoded)) goto done;
    c->last_id=atom_deep_copy(&c->cache,id);valid=c->last_id && queue(host,c,encoded);
done:
    if (!valid && added) resource_close(host,added);
    clear(&encoded);arena_free(&arena);return valid;
}
static bool read_client(Host *host,size_t index) {
    Connection *c=&host->clients[index];
    while (c->header_read<8) {
        ssize_t n=recv(c->fd,c->header+c->header_read,8-c->header_read,0);
        if (n>0) c->header_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    if (!c->input.data) {
        size_t bytes;
        if (!cetta_ipc_csp_size(c->header,host->frame,&bytes)) return false;
        c->input.data=malloc(bytes);c->input.size=bytes;if (!c->input.data) return false;
    }
    while (c->input_read<c->input.size) {
        ssize_t n=recv(c->fd,c->input.data+c->input_read,c->input.size-c->input_read,0);
        if (n>0) c->input_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    return respond(host,index);
}
static bool write_client(Connection *c) {
    while (c->sent<c->output.size) {
        ssize_t n=send(c->fd,c->output.data+c->sent,c->output.size-c->sent,MSG_NOSIGNAL);
        if (n>0) c->sent+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    clear(&c->input);clear(&c->output);c->header_read=c->input_read=c->sent=0;return true;
}
static bool configure(Host *host,const char *entry) {
    Atom *config=evaluate(host,&host->configuration,entry,NULL);
    if (!tagged(config,"ValueServiceV1",5)) return false;
    host->path=text(config->expr.elems[1]);host->data=config->expr.elems[2];Atom *routes=config->expr.elems[3],*limits=config->expr.elems[4];
    if (!host->path || host->path[0]!='/' || !routes || routes->kind!=ATOM_EXPR || !routes->expr.len || !tagged(limits,"ServiceLimitsV1",7)) return false;
    uint64_t values[6];for (size_t i=0;i<6;i++) if (!natural(limits->expr.elems[i+1],&values[i])) return false;
    if (!values[0] || values[0]>1024 || !values[1] || values[1]>4096 || values[2]<1024 || values[2]>CETTA_ATOM_WIRE_BYTES ||
        !values[3] || values[3]>3600000 || !values[4] || values[4]>SIZE_MAX || !values[5] || values[5]>INT32_MAX) return false;
    host->client_count=(size_t)values[0];host->resource_count=(size_t)values[1];host->frame=(size_t)values[2];host->idle=values[3];host->owned_limit=(size_t)values[4];host->fuel=(int)values[5];
    host->route_count=routes->expr.len;host->routes=calloc(host->route_count,sizeof(*host->routes));
    if (!host->routes) return false;
    for (size_t i=0;i<host->route_count;i++) {
        Atom *r=routes->expr.elems[i];
        if (!tagged(r,"ServiceRouteV1",4) || r->expr.elems[1]->kind!=ATOM_SYMBOL || r->expr.elems[3]->kind!=ATOM_SYMBOL ||
            !(atom_is_symbol(r->expr.elems[2],"Read") || atom_is_symbol(r->expr.elems[2],"Write"))) return false;
        for (size_t j=0;j<i;j++) if (atom_eq(r->expr.elems[1],host->routes[j].operation)) return false;
        host->routes[i]=(Route){r->expr.elems[1],atom_symbol_bytes(r->expr.elems[3]),atom_is_symbol(r->expr.elems[2],"Write")};
    }
    unsigned char random[16];size_t offset=0;
    while (offset<sizeof(random)) { ssize_t n=getrandom(random+offset,sizeof(random)-offset,0);if (n<0 && errno==EINTR) continue;if (n<=0) return false;offset+=(size_t)n; }
    static const char hex[]="0123456789abcdef";for (size_t i=0;i<16;i++) { host->boot[2*i]=hex[random[i]>>4];host->boot[2*i+1]=hex[random[i]&15]; }
    return true;
}
static bool private_parent(const char *path) {
    char *parent=strdup(path);if (!parent) return false;
    char *slash=strrchr(parent,'/');
    if (!slash) { free(parent);return false; }
    if (slash==parent) slash[1]=0;else *slash=0;
    struct stat st;bool valid=!lstat(parent,&st) && S_ISDIR(st.st_mode) &&
        st.st_uid==geteuid() && !(st.st_mode&077);
    free(parent);return valid;
}
int cetta_value_service_run(CettaLibraryContext *context,Space *program,Registry *registry,Arena *permanent,const char *configuration) {
    Host host={.context=context,.program=program,.registry=registry,.permanent=permanent,.fuel=1000000,.next_resource=1};
    arena_init_detached(&host.configuration);int listener=-1,lock=-1,rc=1;struct stat bound={0};
    struct sigaction prior_term,prior_int;bool signals=false;
    if (!configure(&host,configuration)) { fputs("value service: invalid configuration\n",stderr);goto done; }
    if (!private_parent(host.path)) { fputs("value service: socket requires a private owned directory\n",stderr);goto done; }
    char *lock_path=malloc(strlen(host.path)+6);if (!lock_path) goto done;
    sprintf(lock_path,"%s.lock",host.path);lock=open(lock_path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);free(lock_path);
    struct stat lock_stat;
    if (lock<0 || fstat(lock,&lock_stat) || !S_ISREG(lock_stat.st_mode) || lock_stat.st_uid!=geteuid() || (lock_stat.st_mode&077) || flock(lock,LOCK_EX|LOCK_NB)) goto done;
    listener=cetta_ipc_listen(host.path,0600,(gid_t)-1,(int)host.client_count);
    if (listener<0 || lstat(host.path,&bound)) { fputs("value service: listener unavailable\n",stderr);goto done; }
    host.clients=calloc(host.client_count,sizeof(*host.clients));
    if (host.clients) for (size_t i=0;i<host.client_count;i++) host.clients[i].fd=-1;
    host.resources=calloc(host.resource_count,sizeof(*host.resources));
    struct pollfd *fds=calloc(host.client_count+1,sizeof(*fds));if (!host.clients || !host.resources || !fds) { free(fds);goto done; }
    struct sigaction action={.sa_handler=stop};sigemptyset(&action.sa_mask);stopping=0;
    sigaction(SIGTERM,&action,&prior_term);sigaction(SIGINT,&action,&prior_int);signals=true;
    while (!stopping) {
        fds[0]=(struct pollfd){listener,POLLIN,0};uint64_t now=now_ms(),due=now+1000;
        for (size_t i=0;i<host.client_count;i++) {
            Connection *c=&host.clients[i];
            if (c->fd>=0 && now>=c->deadline) connection_close(&host,i);
            fds[i+1]=(struct pollfd){c->fd,c->output.data ? POLLOUT : POLLIN,0};
            if (c->fd>=0 && c->deadline<due) due=c->deadline;
        }
        int result=poll(fds,host.client_count+1,(int)(due-now));if (result<0) { if (errno==EINTR) continue;rc=2;break; }
        if (fds[0].revents&POLLIN) {
            int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);struct ucred peer;socklen_t length=sizeof(peer);size_t slot=host.client_count;
            for (size_t i=0;i<host.client_count;i++) if (host.clients[i].fd<0) { slot=i;break; }
            if (fd>=0) {
                if (slot==host.client_count || getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length) || length!=sizeof(peer) || peer.uid!=geteuid()) close(fd);
                else { host.clients[slot]=(Connection){.fd=fd,.uid=peer.uid,.deadline=now_ms()+host.idle};arena_init_detached(&host.clients[slot].cache); }
            }
        }
        for (size_t i=0;i<host.client_count;i++) {
            Connection *c=&host.clients[i];short events=fds[i+1].revents;if (c->fd<0 || !events) continue;
            if ((events&(POLLERR|POLLNVAL)) || ((events&POLLHUP) && !(events&POLLIN)) ||
                ((events&POLLIN) && !c->output.data && !read_client(&host,i)) ||
                ((events&POLLOUT) && c->output.data && !write_client(c))) connection_close(&host,i);
        }
    }
    free(fds);if (rc!=2) rc=0;
done:
    if (host.clients) for (size_t i=0;i<host.client_count;i++) connection_close(&host,i);
    free(host.clients);free(host.resources);free(host.routes);
    if (listener>=0) { close(listener);struct stat current;if (!lstat(host.path,&current) && current.st_dev==bound.st_dev && current.st_ino==bound.st_ino) unlink(host.path); }
    if (lock>=0) close(lock);
    if (signals) { sigaction(SIGTERM,&prior_term,NULL);sigaction(SIGINT,&prior_int,NULL); }
    arena_free(&host.configuration);return rc;
}
