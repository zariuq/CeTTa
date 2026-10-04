#define _GNU_SOURCE
#include "telegram_control.h"
#include "durable_value.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { PEERS=8, PACKET=256, REPLY=256 };
typedef struct { int fd; char reply[REPLY]; size_t size; uint64_t since; } Peer;
struct CettaTelegramControl {
    CettaDurableStore *store;
    CettaTelegramAgent agent;
    int listener;
    uid_t uid;
    Peer peers[PEERS];
};
static bool component(const char *s) {
    if (!s || !*s || strnlen(s,65)>64) return false;
    for (;*s;++s) if (!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') ||
        (*s>='0' && *s<='9') || *s=='-' || *s=='_' || *s=='.')) return false;
    return true;
}
bool cetta_telegram_control_lane(const CettaTelegramAgent *a, const char *lane) {
    if (!cetta_telegram_agent_valid(a) || !component(lane)) return false;
    char *end; errno=0; int64_t chat=strtoll(lane,&end,10);
    if (errno || end==lane || *end!='.' || !chat) return false;
    const char *part=end+1; errno=0; int64_t thread=strtoll(part,&end,10);
    if (errno || end==part || *end || thread<0) return false;
    char canonical[64]; snprintf(canonical,sizeof(canonical),"%" PRId64 ".%" PRId64,chat,thread);
    if (strcmp(lane,canonical)) return false;
    for (size_t i=0;i<a->actions->chat_count;++i) if (a->actions->chats[i]==chat) return true;
    return false;
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
static bool form(const Atom *a, const char *head, size_t n) {
    return a && a->kind==ATOM_EXPR && a->expr.len==n && atom_is_symbol(a->expr.elems[0],head);
}
static bool version_one(const Atom *a) {
    return a->expr.elems[1]->kind==ATOM_GROUNDED &&
        a->expr.elems[1]->ground.gkind==GV_INT && a->expr.elems[1]->ground.ival==1;
}
static CettaDurableStatus read_record(CettaDurableStore *store, const char *space,
    const char *key, Arena *arena, Atom **value) {
    *value=NULL; CettaDurableScope scope={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    CettaDurableStatus s=cetta_durable_observe(store,&scope,1,&o);
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
        if (v->count) s=cetta_durable_value_decode(arena,v->records[0].data,v->records[0].size,value);
    }
    cetta_durable_observation_free(o); return s;
}
static CettaDurableStatus status(CettaTelegramControl *c, const char *lane, char out[REPLY]) {
    char key[160]; snprintf(key,sizeof(key),"telegram/%s/%s",c->agent.source,lane);
    Arena arena; arena_init(&arena); Atom *actor=NULL;
    CettaDurableStatus s=read_record(c->store,"host.actors",key,&arena,&actor);
    if (s==DURABLE_OK) {
        strcpy(out,"idle");
        if (actor) {
            if (!form(actor,"host:actor",4) || !version_one(actor) || !string(actor->expr.elems[2]) ||
                strcmp(string(actor->expr.elems[2]),c->agent.program->version)) strcpy(out,"incompatible");
            else {
                Atom *state=actor->expr.elems[3];
                if (form(state,"tg-agent:worker-held",3) && component(string(state->expr.elems[1])) && component(string(state->expr.elems[2])))
                    snprintf(out,REPLY,"worker-held %.64s %.64s",string(state->expr.elems[1]),string(state->expr.elems[2]));
                else if (form(state,"tg-agent:idle",1)) strcpy(out,"idle");
                else if (form(state,"tg-agent:waiting",6)) strcpy(out,"waiting");
                else if (form(state,"tg-agent:sending",4)) strcpy(out,"sending");
                else if (form(state,"tg-agent:held",5) && component(string(state->expr.elems[1])) &&
                         state->expr.elems[2]->kind==ATOM_GROUNDED && state->expr.elems[2]->ground.gkind==GV_INT)
                    /* Name the batch, so the release request needs nothing else. */
                    snprintf(out,REPLY,"send-held %.64s %lld",string(state->expr.elems[1]),
                             (long long)state->expr.elems[2]->ground.ival);
                else if (form(state,"tg-agent:held",5)) strcpy(out,"send-held");
                else strcpy(out,"incompatible");
            }
        }
    }
    arena_free(&arena); return s;
}
static CettaDurableStatus release(CettaTelegramControl *c, const char *id,
    const char *lane, const char *batch) {
    char input[80]; snprintf(input,sizeof(input),"control/%s",id);
    CettaDurableField fields[]={
        {.kind=DURABLE_FIELD_SYMBOL,.text={"host:telegram-control",21}},
        {.kind=DURABLE_FIELD_INT,.integer=1},
        {.kind=DURABLE_FIELD_TEXT,.text={c->agent.source,strlen(c->agent.source)}},
        {.kind=DURABLE_FIELD_TEXT,.text={id,strlen(id)}},
        {.kind=DURABLE_FIELD_TEXT,.text={lane,strlen(lane)}},
        {.kind=DURABLE_FIELD_TEXT,.text={"release-worker",14}},
        {.kind=DURABLE_FIELD_TEXT,.text={batch,strlen(batch)}}};
    CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={fields,7}};
    unsigned char *data=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_fields_encode(&value,&data,&size);
    CettaDurableScope scopes[]={{DURABLE_KEY,"telegram.controls",id},{DURABLE_KEY,"host.inbox",input},
        {DURABLE_KEY,"telegram.control-results",id}};
    CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=cetta_durable_observe(c->store,scopes,3,&o);
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
        if (v->count) s=v->records[0].size==size && !memcmp(v->records[0].data,data,size)?DURABLE_OK:DURABLE_CONFLICT;
        else if (cetta_durable_observation_view(o,1)->count || cetta_durable_observation_view(o,2)->count) s=DURABLE_CONFLICT;
        else {
            CettaDurableOp ops[]={{DURABLE_INSERT,"telegram.controls",id,data,size},{DURABLE_INSERT,"host.inbox",input,data,size}};
            int64_t revision; s=cetta_durable_commit_observed(c->store,o,ops,2,&revision);
        }
    }
    cetta_durable_observation_free(o); free(data); return s;
}
static CettaDurableStatus receipt(CettaTelegramControl *c, const char *id, char out[REPLY]) {
    Arena arena; arena_init(&arena); Atom *value=NULL;
    CettaDurableStatus s=read_record(c->store,"telegram.control-results",id,&arena,&value);
    if (s==DURABLE_OK && value) {
        if (form(value,"tg-agent:control-result",4) && version_one(value) &&
            string(value->expr.elems[3]) && component(string(value->expr.elems[3])) && atom_is_symbol(value->expr.elems[2],"released")) strcpy(out,"released");
        else if (form(value,"tg-agent:control-result",4) && version_one(value) &&
            string(value->expr.elems[3]) && component(string(value->expr.elems[3])) && atom_is_symbol(value->expr.elems[2],"refused")) strcpy(out,"refused");
        else s=DURABLE_CORRUPT;
    } else if (s==DURABLE_OK) {
        s=read_record(c->store,"telegram.controls",id,&arena,&value);
        if (s==DURABLE_OK) strcpy(out,value?"recorded":"unknown");
    }
    arena_free(&arena); return s;
}
static CettaDurableStatus request(CettaTelegramControl *c, char *packet, char out[REPLY]) {
    char *save=NULL, *words[5]; size_t n=0;
    for (char *p=strtok_r(packet," ",&save);p && n<5;p=strtok_r(NULL," ",&save)) words[n++]=p;
    strcpy(out,"invalid");
    if (!n) return DURABLE_OK;
    CettaDurableStatus s=DURABLE_OK;
    if (n==2 && !strcmp(words[0],"status") && cetta_telegram_control_lane(&c->agent,words[1]))
        s=status(c,words[1],out);
    else if (n==2 && !strcmp(words[0],"receipt") && component(words[1])) s=receipt(c,words[1],out);
    else if (n==4 && !strcmp(words[0],"release") && component(words[1]) &&
             cetta_telegram_control_lane(&c->agent,words[2]) && component(words[3])) {
        s=release(c,words[1],words[2],words[3]); strcpy(out,"recorded");
    }
    if (s==DURABLE_CONFLICT) { strcpy(out,"conflict"); return DURABLE_OK; }
    return s;
}
static uint64_t now_ms(void) {
    struct timespec ts; if (clock_gettime(CLOCK_MONOTONIC,&ts)) return 0;
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
static void close_peer(Peer *p) { if (p->fd>=0) close(p->fd); *p=(Peer){.fd=-1}; }
CettaDurableStatus cetta_telegram_control_new(CettaDurableStore *store,
    const CettaTelegramAgent *agent, int listener, uid_t uid, CettaTelegramControl **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL; int type=0, accepting=0; struct sockaddr_storage address; socklen_t n=sizeof(address),m=sizeof(int);
    if (!store || !cetta_telegram_agent_valid(agent) || listener<0 ||
        getsockname(listener,(struct sockaddr *)&address,&n) || address.ss_family!=AF_UNIX ||
        getsockopt(listener,SOL_SOCKET,SO_TYPE,&type,&m) || type!=SOCK_SEQPACKET ||
        getsockopt(listener,SOL_SOCKET,SO_ACCEPTCONN,&accepting,&m) || !accepting) return DURABLE_INVALID;
    int flags=fcntl(listener,F_GETFL);
    if (flags<0 || fcntl(listener,F_SETFL,flags|O_NONBLOCK) || fcntl(listener,F_SETFD,FD_CLOEXEC)) return DURABLE_IO;
    CettaTelegramControl *c=calloc(1,sizeof(*c)); if (!c) return DURABLE_NOMEM;
    c->store=store; c->agent=*agent; c->listener=listener; c->uid=uid;
    for (size_t i=0;i<PEERS;++i) c->peers[i].fd=-1;
    *out=c; return DURABLE_OK;
}
CettaDurableStatus cetta_telegram_control_step(CettaTelegramControl *c) {
    if (!c) return DURABLE_INVALID;
    uint64_t now=now_ms(); if (!now) return DURABLE_IO;
    for (size_t i=0;i<PEERS;++i) {
        Peer *p=&c->peers[i];
        if (p->fd>=0 && now-p->since>=5000) close_peer(p);
        if (p->fd<0) {
            int fd=accept4(c->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
            if (fd<0) { if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR) continue; return DURABLE_IO; }
            struct ucred peer; socklen_t n=sizeof(peer);
            if (getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n) || n!=sizeof(peer) || peer.uid!=c->uid) { close(fd); continue; }
            p->fd=fd; p->since=now;
        }
        if (!p->size) {
            char buffer[PACKET+1]; struct iovec iov={buffer,PACKET}; struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1};
            ssize_t n=recvmsg(p->fd,&msg,MSG_DONTWAIT);
            if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) continue;
            if (n<=4 || msg.msg_flags&MSG_TRUNC || memcmp(buffer,"CTC1",4)) { close_peer(p); continue; }
            bool valid=true; for (ssize_t j=4;j<n;++j) if (buffer[j]<32 || buffer[j]>126) valid=false;
            buffer[n]=0; char out[REPLY]="invalid";
            CettaDurableStatus s=valid?request(c,buffer+4,out):DURABLE_OK;
            if (s!=DURABLE_OK) { close_peer(p); return s; }
            int written=snprintf(p->reply,sizeof(p->reply),"CTC1%.240s",out); p->size=(size_t)written;
        }
        ssize_t sent=send(p->fd,p->reply,p->size,MSG_DONTWAIT|MSG_NOSIGNAL);
        if (sent<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) continue;
        close_peer(p);
    }
    return DURABLE_OK;
}
void cetta_telegram_control_free(CettaTelegramControl *c) {
    if (!c) return;
    for (size_t i=0;i<PEERS;++i) close_peer(&c->peers[i]);
    close(c->listener); free(c);
}
