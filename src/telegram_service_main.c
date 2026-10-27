#define _GNU_SOURCE
#include "telegram_scheduler.h"
#include "telegram_control.h"
#include "telegram_transport.h"
#include "http_worker.h"
#include "eval.h"
#include "durable_value.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum { USAGE=64, CONFIG=78, FAILURE=1 };
typedef struct {
    const char *root, *directory, *worker, *credential, *origin, *ca, *commands;
    int token_fd, listener, operator_fd;
    int64_t deadline_ms;
    int64_t poll_limit;
    int64_t chats[128]; size_t count;
    int64_t operators[16]; size_t operator_count;
    int64_t peers[128]; size_t peer_count;
    int64_t initial_offset;
    bool run, check, channel, program_seen, offset_seen;
} Config;
static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped=1; }
static void fault(void *unused, const char *component, CettaDurableStatus s) {
    (void)unused; fprintf(stderr,"cetta-telegram: %s: %s\n",component,cetta_durable_status_name(s));
}
static void app_fault(void *unused, const char *component, const char *key, CettaDurableStatus s) {
    (void)key; fault(unused,component,s);
}
static void http_fault(void *unused, const char *key, CettaDurableStatus s) {
    (void)key; fault(unused,"transport-record",s);
}
static int error(const char *stage, int status) {
    fprintf(stderr,"cetta-telegram: %s failed\n",stage); return status;
}
static bool number(const char *s, int64_t lo, int64_t hi, int64_t *out) {
    if (!s || !*s || *s=='+' || (*s=='-' && !s[1])) return false;
    for (const char *p=s+(*s=='-');*p;++p) if (*p<'0' || *p>'9') return false;
    errno=0; char *end; long long n=strtoll(s,&end,10);
    if (errno || *end || n<lo || n>hi) return false;
    *out=n; return true;
}
static bool component(const char *s) {
    if (!s || !*s || strnlen(s,65)>64) return false;
    for (;*s;++s) if (!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') ||
        (*s>='0' && *s<='9') || *s=='_' || *s=='-' || *s=='.')) return false;
    return true;
}
/* Operator commands are declarations only, checked here and added to the
 * trusted program as inert facts: (tg-cmd:command "/name" KIND "help"),
 * optionally (tg-cmd:agent "Name") and (tg-cmd:bot "username"). Anything
 * else, a repeated name or a second stop/start/help, refuses the file. */
static const char *declared(const Atom *a, size_t max) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_STRING) return NULL;
    size_t n=strnlen(a->ground.sval,max+1);
    if (!n || n>max) return NULL;
    for (const unsigned char *p=(const unsigned char *)a->ground.sval;*p;++p)
        if (*p<32 || *p==127 || *p=='"' || *p=='\\') return NULL;
    return a->ground.sval;
}
static bool command_name(const char *s) {
    size_t n=s?strlen(s):0;
    if (n<2 || n>33 || s[0]!='/') return false;
    for (size_t i=1;i<n;++i) if (!((s[i]>='a' && s[i]<='z') || (s[i]>='0' && s[i]<='9') || s[i]=='_')) return false;
    return true;
}
static bool bot_name(const char *s) {
    size_t n=s?strlen(s):0;
    if (n<5 || n>32) return false;
    for (size_t i=0;i<n;++i) if (!((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') ||
        (s[i]>='0' && s[i]<='9') || s[i]=='_')) return false;
    return true;
}
static bool commands_name_bot;
static bool load_commands(const char *path, Arena *arena, Space *program) {
    Atom **atoms=NULL; int n=parse_metta_file(path,arena,&atoms);
    const char *names[64]; size_t count=0; unsigned singles=0; bool ok=n>=0 && n<=66;
    for (int i=0;ok && i<n;++i) {
        Atom *d=atoms[i];
        if (d->kind!=ATOM_EXPR || !d->expr.len) { ok=false; break; }
        Atom *head=d->expr.elems[0];
        if (d->expr.len==4 && atom_is_symbol(head,"tg-cmd:command")) {
            const char *name=declared(d->expr.elems[1],33), *help=declared(d->expr.elems[3],200);
            Atom *k=d->expr.elems[2];
            unsigned single=atom_is_symbol(k,"help")?1:atom_is_symbol(k,"stop")?2:atom_is_symbol(k,"start")?4:0;
            if (!name || !command_name(name) || !help || count==64 || (singles&single) ||
                (!single && !atom_is_symbol(k,"delegated"))) { ok=false; break; }
            for (size_t j=0;j<count;++j) if (!strcmp(names[j],name)) ok=false;
            singles|=single; names[count++]=name;
        } else if (d->expr.len==2 && atom_is_symbol(head,"tg-cmd:agent")) {
            if ((singles&8) || !declared(d->expr.elems[1],32)) ok=false;
            singles|=8;
        } else if (d->expr.len==2 && atom_is_symbol(head,"tg-cmd:bot")) {
            const char *bot=declared(d->expr.elems[1],32);
            if ((singles&16) || !bot || !bot_name(bot)) ok=false;
            singles|=16; commands_name_bot=true;
        } else ok=false;
    }
    /* A stop without a start could never be undone from the chat. */
    if (ok && (singles&6) && (singles&6)!=6) ok=false;
    for (int i=0;ok && i<n;++i) space_add(program,atoms[i]);
    free(atoms); return ok;
}
/* The bot's own username, learned from getMe, declares (tg-cmd:bot NAME), so
 * that /command@NAME is this bot's command. It is asked in the background, on
 * a worker of its own, and never delays startup or a command. Until an answer
 * about this bot arrives, an addressed command stays ordinary input; a failed
 * lookup is asked again a minute later. */
typedef struct {
    CettaHttpWorker *worker;
    uint64_t job, retry_ms;
    bool pending, known, reported;
} Identity;
static void identity_ask(Identity *d, const CettaTelegramCredential *credential, uint64_t now_ms) {
    CettaHttpWorkerLimits limits=cetta_http_worker_default_limits();
    if (!d->worker && cetta_http_worker_new(&limits,NULL,&d->worker)!=HTTP_WORKER_OK) d->worker=NULL;
    d->pending=d->worker && cetta_telegram_submit_effect(credential,d->worker,++d->job,"getMe",
        "application/json","{}",2,10000,65536)==HTTP_WORKER_OK;
    if (!d->pending) d->retry_ms=now_ms+60000;
}
static void identity_step(Identity *d, const CettaTelegramCredential *credential, const char *source_id,
        CettaLibraryContext *context, Space *program, Space *commands_space,
        Arena *scratch, Arena *persistent,
        Registry *registry, uint64_t now_ms) {
    if (d->known) return;
    if (!d->pending) { if (now_ms>=d->retry_ms) identity_ask(d,credential,now_ms); return; }
    CettaHttpResult r;
    if (!cetta_http_worker_take(d->worker,&r)) return;
    d->pending=false; d->retry_ms=now_ms+60000;
    /* The answer is screened for the token before anything reads it, and
     * decided by the program; the host checks the name it declares. */
    char *text=r.status==200 && r.body && !r.response_too_large &&
        cetta_telegram_response_safe(credential,r.body,r.body_size)?malloc(r.body_size+1):NULL;
    if (text) {
        memcpy(text,r.body,r.body_size); text[r.body_size]=0;
        Atom *args[]={atom_symbol(scratch,"tg-cmd:identity"),atom_string(scratch,text),
            atom_string(scratch,source_id+9)}; /* "telegram-" and the public bot ID */
        EvalOutcome o; eval_outcome_init(&o);
        eval_top_speculative(context,program,scratch,persistent,registry,atom_expr(scratch,args,3),200000,&o);
        const Atom *v=o.completion==CETTA_EVAL_COMPLETE && !o.effect_denials && o.results.len==1?o.results.items[0]:NULL;
        const char *name=v && v->kind==ATOM_EXPR && v->expr.len==2 && atom_is_symbol(v->expr.elems[0],"tg:some") &&
            v->expr.elems[1]->kind==ATOM_GROUNDED && v->expr.elems[1]->ground.gkind==GV_STRING?
            v->expr.elems[1]->ground.sval:NULL;
        if (name && bot_name(name) && commands_space) {
            Atom *declaration[]={atom_symbol(persistent,"tg-cmd:bot"),atom_string(persistent,name)};
            space_add(commands_space,atom_expr(persistent,declaration,2)); d->known=true;
            printf("cetta-telegram: commands addressed to @%s are this bot's\n",name); fflush(stdout);
        }
        eval_outcome_free(&o); free(text);
    }
    cetta_http_result_free(&r);
    if (!d->known && !d->reported) { fputs("cetta-telegram: bot username not yet known\n",stderr); d->reported=true; }
}
static void usage(void) {
    puts("Usage: cetta-telegram-service (--check | --run) --root DIR --state-dir DIR\n"
         "       --worker NAME [--chat ID ...] [--peer ID ...] [--operator ID ...]\n"
         "       [--program agent|channel] [--initial-offset N] [--poll-limit 1..100]\n"
         "       [--commands FILE] [--command-deadline-ms N]\n"
         "       (--credential-file FILE | --credential-fd FD) [--listener-fd FD]\n"
         "       [--operator-fd FD]\n"
         "       [--mock-origin http[s]://127.0.0.1:PORT [--ca-file FILE]]\n"
         "--check loads the fixed policy and validates configuration without opening\n"
         "the journal, consuming the listener or constructing any transport.\n"
         "--run requires a private Unix seqpacket listener: explicit fd or one\n"
         "systemd activation fd. The supervisor owns the socket's lifetime.");
}
static bool arguments(int argc, char **argv, Config *c) {
    *c=(Config){.token_fd=-1,.listener=-1,.operator_fd=-1}; unsigned seen=0;
    const char *names[]={"--root","--state-dir","--worker","--credential-file","--mock-origin","--ca-file",
                         "--credential-fd","--listener-fd","--operator-fd"};
    const char **strings[]={&c->root,&c->directory,&c->worker,&c->credential,&c->origin,&c->ca};
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--run")) { if (c->run || c->check) return false; c->run=true; continue; }
        if (!strcmp(argv[i],"--check")) { if (c->run || c->check) return false; c->check=true; continue; }
        if (++i==argc) return false;
        const char *option=argv[i-1], *value=argv[i]; int64_t n;
        if (!strcmp(option,"--chat")) {
            if (c->count==128 || !number(value,INT64_MIN,INT64_MAX,&n) || !n) return false;
            for (size_t j=0;j<c->count;++j) if (c->chats[j]==n) return false;
            c->chats[c->count++]=n; continue;
        }
        if (!strcmp(option,"--operator")) {
            if (c->operator_count==16 || !number(value,1,INT64_MAX,&n)) return false;
            for (size_t j=0;j<c->operator_count;++j) if (c->operators[j]==n) return false;
            c->operators[c->operator_count++]=n; continue;
        }
        if (!strcmp(option,"--peer")) {
            if (c->peer_count==128 || !number(value,1,INT64_MAX,&n)) return false;
            for (size_t j=0;j<c->peer_count;++j) if (c->peers[j]==n) return false;
            c->peers[c->peer_count++]=n; continue;
        }
        if (!strcmp(option,"--initial-offset")) {
            if (c->offset_seen || !number(value,0,INT64_MAX-1,&n)) return false;
            c->offset_seen=true; c->initial_offset=n; continue;
        }
        if (!strcmp(option,"--poll-limit")) {
            if (c->poll_limit || !number(value,1,100,&n)) return false;
            c->poll_limit=n; continue;
        }
        if (!strcmp(option,"--commands")) {
            if (c->commands || value[0]!='/') return false;
            c->commands=value; continue;
        }
        if (!strcmp(option,"--command-deadline-ms")) {
            if (c->deadline_ms || !number(value,100,60000,&n)) return false;
            c->deadline_ms=n; continue;
        }
        if (!strcmp(option,"--program")) {
            if (c->program_seen || (strcmp(value,"agent") && strcmp(value,"channel"))) return false;
            c->program_seen=true; c->channel=!strcmp(value,"channel"); continue;
        }
        size_t j=0; while (j<9 && strcmp(option,names[j])) ++j;
        if (j==9 || (seen&(1u<<j))) return false;
        seen|=1u<<j;
        if (j<6) *strings[j]=value;
        else {
            if (!number(value,3,INT32_MAX,&n)) return false;
            if (j==6) c->token_fd=(int)n;
            else if (j==7) c->listener=(int)n; else c->operator_fd=(int)n;
        }
    }
    /* A peer is one private-chat destination and a wake sender, not an
     * operator. Keep those two routing settings together. */
    for (size_t i=0;i<c->peer_count;++i) {
        bool present=false;
        for (size_t j=0;j<c->count;++j) if (c->chats[j]==c->peers[i]) present=true;
        if (!present) {
            if (c->count==128) return false;
            c->chats[c->count++]=c->peers[i];
        }
    }
    if ((!c->run && !c->check) || !c->root || c->root[0]!='/' || !c->directory || c->directory[0]!='/' ||
        !component(c->worker) || !c->count || (!!c->credential==(c->token_fd>=0)) ||
        (c->credential && c->credential[0]!='/') || (c->ca && (c->ca[0]!='/' || !c->origin)) ||
        (c->listener>=0 && c->token_fd==c->listener) ||
        (c->operator_fd>=0 && (c->operator_fd==c->listener || c->operator_fd==c->token_fd)) ||
        ((c->commands || c->deadline_ms) && !c->channel)) return false;
    /* Custom origins are exclusively numeric loopback fixtures, even for TLS.
     * The credential adapter validates scheme/port/trailing bytes afterwards. */
    if (c->origin) {
        const char *host=!strncmp(c->origin,"https://",8)?c->origin+8:
                         !strncmp(c->origin,"http://",7)?c->origin+7:NULL;
        if (!host || strncmp(host,"127.0.0.1:",10)) return false;
    }
    return true;
}
/* The state directory is the owner's, with no access for others; whether
 * the owner's group may read it is the administrator's choice. */
static bool private_directory(const char *path) {
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); struct stat st;
    bool ok=fd>=0 && !fstat(fd,&st) && st.st_uid==geteuid() && !(st.st_mode&007);
    if (fd>=0) close(fd);
    return ok;
}
static bool listener_valid(int fd) {
    struct sockaddr_un addr={0}; socklen_t n=sizeof(addr), len=sizeof(int); struct stat st;
    int type=0, accepting=0;
    if (fd<3 || getsockname(fd,(struct sockaddr *)&addr,&n) || addr.sun_family!=AF_UNIX ||
        n<=offsetof(struct sockaddr_un,sun_path) || n>sizeof(addr) || !memchr(addr.sun_path,0,sizeof(addr.sun_path)) ||
        addr.sun_path[0]!='/' || getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&len) || type!=SOCK_SEQPACKET ||
        getsockopt(fd,SOL_SOCKET,SO_ACCEPTCONN,&accepting,&len) || !accepting ||
        lstat(addr.sun_path,&st) || !S_ISSOCK(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077)) return false;
    return true;
}
static int activation_listener(void) {
    int64_t pid, count;
    bool ok=number(getenv("LISTEN_PID"),1,INT32_MAX,&pid) && pid==getpid() &&
        number(getenv("LISTEN_FDS"),1,1,&count);
    unsetenv("LISTEN_PID"); unsetenv("LISTEN_FDS"); unsetenv("LISTEN_FDNAMES");
    return ok?3:-1;
}
static CettaDurableStatus bind_identity(CettaDurableStore *store, const char *source, const char *worker,
                                       const char *program) {
    /* A journal belongs to one bot, worker and program; the agent keeps its
     * original identity text, so its existing journals still open. */
    char identity[220]; int n=program?snprintf(identity,sizeof(identity),"telegram-service/1|%s|%s|%s",source,worker,program):
        snprintf(identity,sizeof(identity),"telegram-service/1|%s|%s",source,worker);
    CettaDurableField f={.kind=DURABLE_FIELD_TEXT,.text={identity,(size_t)n}};
    unsigned char *data=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_fields_encode(&f,&data,&size);
    CettaDurableScope q={DURABLE_KEY,"host.service","telegram"}; CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=cetta_durable_observe(store,&q,1,&o);
    if (s==DURABLE_OK) {
        const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0);
        if (v->count) s=v->records[0].size==size && !memcmp(v->records[0].data,data,size)?DURABLE_OK:DURABLE_VERSION;
        else {
            CettaDurableOp op={DURABLE_INSERT,q.space,q.key,data,size}; int64_t revision;
            s=cetta_durable_commit_observed(store,o,&op,1,&revision);
        }
    }
    free(data); cetta_durable_observation_free(o); return s;
}
/* Hand over from another poller: start a fresh journal's cursor at the next
 * update that poller had not yet handled. An existing cursor is authoritative
 * and is never moved; the host.cursors record is the inbox's own format. */
static CettaDurableStatus seed_cursor(CettaDurableStore *store, const char *source, int64_t offset) {
    CettaDurableField f[]={
        {.kind=DURABLE_FIELD_SYMBOL,.text={"host:cursor",11}},
        {.kind=DURABLE_FIELD_INT,.integer=1},
        {.kind=DURABLE_FIELD_TEXT,.text={source,strlen(source)}},
        {.kind=DURABLE_FIELD_INT,.integer=offset}};
    CettaDurableField value={.kind=DURABLE_FIELD_EXPR,.expression={f,4}};
    unsigned char *data=NULL; size_t size=0;
    CettaDurableStatus s=cetta_durable_fields_encode(&value,&data,&size);
    CettaDurableScope q={DURABLE_KEY,"host.cursors",source}; CettaDurableObservation *o=NULL;
    if (s==DURABLE_OK) s=cetta_durable_observe(store,&q,1,&o);
    if (s==DURABLE_OK && !cetta_durable_observation_view(o,0)->count) {
        CettaDurableOp op={DURABLE_INSERT,q.space,q.key,data,size}; int64_t revision;
        s=cetta_durable_commit_observed(store,o,&op,1,&revision);
    }
    free(data); cetta_durable_observation_free(o); return s;
}
int main(int argc,char **argv) {
    if (argc==2 && !strcmp(argv[1],"--help")) { usage(); return 0; }
    Config c; if (!arguments(argc,argv,&c)) { usage(); return USAGE; }
    struct rlimit core={0,0};
    if (setrlimit(RLIMIT_CORE,&core) || prctl(PR_SET_DUMPABLE,0) || !private_directory(c.directory))
        return error("process/state isolation",CONFIG);
    umask(007);
    if (c.run) {
        if (c.listener<0) c.listener=activation_listener();
        if (!listener_valid(c.listener) || c.token_fd==c.listener || c.operator_fd==c.listener ||
            (c.operator_fd>=0 && !listener_valid(c.operator_fd))) return error("listener",CONFIG);
        if (c.operator_fd>=0) {
            struct stat a,b;
            if (fstat(c.listener,&a) || fstat(c.operator_fd,&b) ||
                (a.st_dev==b.st_dev && a.st_ino==b.st_ino)) return error("separate operator listener",CONFIG);
        }
    }
    char root[PATH_MAX], database[PATH_MAX];
    if (!realpath(c.root,root) || chdir(root) ||
        snprintf(database,sizeof(database),"%s/journal.db",c.directory)>=(int)sizeof(database)) return error("paths",CONFIG);
    int fd=c.credential?open(c.credential,O_RDONLY|O_NOFOLLOW|O_CLOEXEC):c.token_fd;
    CettaTelegramCredentialConfig cc={c.origin,c.ca,c.origin!=NULL}; CettaTelegramCredential *credential=NULL;
    CettaTelegramCredentialStatus cs=cetta_telegram_credential_read(fd,&cc,&credential);
    if (fd>=0) close(fd);
    if (cs!=TELEGRAM_CREDENTIAL_OK) return error("credential",CONFIG);
    char source_id[65]; cetta_telegram_credential_source(credential,source_id);
    struct sigaction sa={0}; sa.sa_handler=stop; sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM,&sa,NULL) || sigaction(SIGINT,&sa,NULL)) {
        cetta_telegram_credential_free(credential); return error("signals",FAILURE);
    }
    /* Module paths are rooted in the administrator-selected installation, never
     * the caller's cwd, a worker file or a provider payload. */
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    Arena persistent,scratch; arena_init(&persistent); arena_init(&scratch);
    Space program; space_init(&program); Registry registry; registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    CettaLibraryContext context; cetta_library_context_init(&context);
    snprintf(context.root_dir,sizeof(context.root_dir),"%s",root);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    int result=CONFIG; Atom *import_error=NULL; CettaDurableStore *store=NULL;
    CettaDurableService *service=NULL; CettaTelegramScheduler *scheduler=NULL;
    CettaTelegramControl *control=NULL; Identity identity={0};
    Space *commands_space=NULL;
    if (!he_library_tables_init()) { error("HE library types",CONFIG); goto done; }
    stdlib_load(&program,&persistent);
    if (!telegram_he_prepare_library(&program,&persistent,true)) { error("HE library",CONFIG); goto done; }
    if (!space_mark_imported_prefix(&program)) { error("HE library view",CONFIG); goto done; }
    /* agent: cognition per message (telegram-agent/1). channel: the agent's own
     * loop reads deliveries and submits keyed actions (telegram-channel/1). */
    const char *module=c.channel?"durable:telegram_channel":"durable:telegram_agent";
    bool imported=cetta_library_import_module(&context,module,&program,false,
        &scratch,&persistent,&registry,1000000,NULL,&import_error);
    if (!imported) {
        error("trusted policy",CONFIG); goto done;
    }
    if (c.commands) {
        if (!cetta_library_import_module(&context,"durable:telegram_commands",&program,true,
                                         &scratch,&persistent,&registry,1000000,
                                         &commands_space,&import_error) ||
            !commands_space || !load_commands(c.commands,&persistent,commands_space)) {
            error("commands",CONFIG); goto done;
        }
    }
    CettaHostProgram trusted={c.channel?"telegram-channel/1":"telegram-agent/1",&program,&context};
    Atom *ids[128]; for (size_t i=0;i<c.count;++i) ids[i]=atom_int(&persistent,c.chats[i]);
    Atom *operators[16]; for (size_t i=0;i<c.operator_count;++i) operators[i]=atom_int(&persistent,c.operators[i]);
    Atom *peers[128]; for (size_t i=0;i<c.peer_count;++i) peers[i]=atom_int(&persistent,c.peers[i]);
    Atom *args[]={atom_symbol(&persistent,"telegram:policy"),atom_int(&persistent,1),atom_expr(&persistent,ids,c.count),
        atom_bool(&persistent,false),atom_expr(&persistent,operators,c.operator_count),atom_expr(&persistent,peers,c.peer_count)};
    Atom *policy=atom_expr(&persistent,args,6);
    /* The channel client edits and deletes its own messages; ownership is
     * recorded in its receipts. The per-message agent only replies. */
    CettaTelegramActionPolicy actions={c.chats,c.count,
        c.channel?TELEGRAM_SEND_TEXT|TELEGRAM_EDIT_TEXT|TELEGRAM_DELETE_MESSAGE|TELEGRAM_ANSWER_CALLBACK:TELEGRAM_SEND_TEXT};
    if (c.check) {
        printf("cetta-telegram: configuration and trusted %s policy valid; dispatch disabled\n",trusted.version);
        result=0; goto done;
    }
    if (stopped) { result=0; goto done; }
    CettaDurableLimits storage_limits=cetta_durable_bounded_limits();
    CettaDurableStatus s=cetta_durable_open(database,&storage_limits,&store);
    if (s==DURABLE_OK) s=bind_identity(store,source_id,c.worker,c.channel?trusted.version:NULL);
    if (s==DURABLE_OK && c.offset_seen) s=seed_cursor(store,source_id,c.initial_offset);
    if (s!=DURABLE_OK) { fault(NULL,"journal identity/open",s); result=s==DURABLE_VERSION?CONFIG:FAILURE; goto done; }
    const char *updates[]={"message","edited_message","callback_query"};
    CettaServiceSource source={{credential,source_id,20,c.poll_limit?c.poll_limit:100,updates,3},&trusted,policy,2000000};
    CettaDispatchChannel channel={"telegram.action","1",credential,&actions,cetta_telegram_action_plan};
    CettaServiceConfig config={{&channel,1,30000,65536,NULL,http_fault},&source,1,c.listener,getuid(),c.worker,NULL,fault};
    s=cetta_service_new(store,&config,&service);
    if (s==DURABLE_OK) c.listener=-1;
    if (s==DURABLE_OK && stopped) { result=0; goto done; }
    CettaTelegramSchedulerConfig ac={{&trusted,source_id,c.worker,&actions,2000000,(uint32_t)c.deadline_ms},
        256,256,8*1024*1024,NULL,app_fault};
    if (s==DURABLE_OK) s=cetta_telegram_scheduler_new(store,service,&ac,&scheduler);
    if (s==DURABLE_OK && c.operator_fd>=0) {
        s=cetta_telegram_control_new(store,&ac.agent,c.operator_fd,getuid(),&control);
        if (s==DURABLE_OK) c.operator_fd=-1;
    }
    if (s!=DURABLE_OK) { fault(NULL,"startup",s); result=FAILURE; goto done; }
    puts("cetta-telegram: ready"); fflush(stdout);
    identity.known=!c.channel || !c.commands || commands_name_bot;
    uint64_t maintenance=0; CettaDurableLimits limits=storage_limits;
    while (!stopped) {
        s=cetta_service_step(service,25);
        if (stopped) break;
        if (s==DURABLE_OK && control) s=cetta_telegram_control_step(control);
        if (s==DURABLE_OK) s=cetta_telegram_scheduler_step(scheduler,8);
        CettaClockSample now;
        if (s==DURABLE_OK && !cetta_clock_sample(&now)) s=DURABLE_IO;
        if (s==DURABLE_OK) identity_step(&identity,credential,source_id,&context,&program,commands_space,&scratch,&persistent,
            &registry,now.monotonic_ms);
        if (s==DURABLE_OK && now.monotonic_ms>=maintenance) {
            maintenance=now.monotonic_ms+1000; CettaDurableUsage u;
            s=cetta_durable_usage(store,&u);
            if (s==DURABLE_OK && u.history_bytes>=(int64_t)(limits.history_bytes/2)) s=cetta_durable_checkpoint(store);
        }
        if (s!=DURABLE_OK) { fault(NULL,"runtime",s); break; }
    }
    result=s==DURABLE_OK?0:FAILURE;
 done:
    if (identity.worker) cetta_http_worker_free(identity.worker);
    cetta_telegram_control_free(control);
    cetta_telegram_scheduler_free(scheduler);
    if (cetta_service_free(service)) { error("unrecorded transport observations",FAILURE); result=FAILURE; }
    if (c.run && c.listener>=0) close(c.listener);
    if (c.run && c.operator_fd>=0) close(c.operator_fd);
    cetta_durable_close(store); cetta_telegram_credential_free(credential);
    cetta_library_context_free(&context); eval_set_library_context(NULL); registry_free(&registry); space_free(&program);
    arena_free(&scratch); arena_free(&persistent); var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
    return result;
}
