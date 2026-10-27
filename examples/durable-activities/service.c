#define _POSIX_C_SOURCE 200809L
#include "durable_activity.h"
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
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

/* Embedding example: a native parent supplies the private journal, inherited
 * worker listener and trusted intake on stdin. No credentials or network
 * endpoints enter the coordinator. Worker completions are inert CWP1 data. */
static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped=1; }
typedef struct { char *data; size_t size, offset; } Output;
typedef struct { int64_t revision, position; } Cursor;
static uint64_t microseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
    return (uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_nsec/1000u;
}
static CettaDurableStatus flush_output(Output *out) {
    if (!out->data) return DURABLE_OK;
    ssize_t n=write(STDOUT_FILENO,out->data+out->offset,out->size-out->offset);
    if (n<0) return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR?DURABLE_OK:DURABLE_IO;
    if (!n) return DURABLE_IO;
    out->offset+=(size_t)n;
    if (out->offset==out->size) { free(out->data); *out=(Output){0}; }
    return DURABLE_OK;
}
static bool number(const char *text, int64_t *value) {
    char *end; errno=0; *value=strtoll(text,&end,10);
    return !errno && *text && !*end;
}
/* Trusted, self-contained policy source is read and parsed once at startup.
 * Declarations are data here; startup directives and imports are not run. */
static bool load_prime_policy(const char *path,Space *space,Arena *arena) {
    const size_t limit=4u*1024u*1024u;
    FILE *file=fopen(path,"rb");if(!file)return false;
    char *text=malloc(limit+2u);if(!text){fclose(file);return false;}
    size_t length=fread(text,1,limit+1u,file);
    bool okay=!ferror(file) && length<=limit && !memchr(text,0,length);
    fclose(file);text[length]=0;
    Atom **forms=NULL;int count=okay?parse_metta_text(text,arena,&forms):-1;
    free(text);okay=count>0;
    for(int i=0;i<count && okay;i++) {
        Atom *form=forms[i];
        okay=form && form->kind==ATOM_EXPR && form->expr.len==3 &&
            (atom_is_symbol(form->expr.elems[0],"=") || atom_is_symbol(form->expr.elems[0],":"));
        if(okay)space_add(space,form);
    }
    free(forms);return okay;
}
static bool record(CettaDurableStore *store,CettaActivityRuntime *runtime,char *line,Output *out) {
    if (!strcmp(line,"quit")) { stopped=1; return true; }
    if (!strncmp(line,"cleanup\t",8u)) {
        const char *command=line+8u;
        FILE *frame=open_memstream(&out->data,&out->size);if(!frame)return false;
        if (!strcmp(command,"status")) {
            CettaActivityStats stats;cetta_activity_stats(runtime,&stats);
            fprintf(frame,"cleanup {\"polls\":%" PRIu64 ",\"completed\":%" PRIu64
                ",\"atomic_waits\":%" PRIu64 ",\"ready\":%zu}\n",
                stats.cleanup_polls,stats.cleanup_completed,stats.cleanup_atomic_waits,stats.ready);
        } else if (!strcmp(command,"allow") || !strcmp(command,"hold")) {
            bool allowed=!strcmp(command,"allow");
            bool selected=cetta_activity_set_cleanup_atomic(runtime,allowed);
            fprintf(frame,"cleanup %s\n",selected?(allowed?"allowed":"held"):"ordinary");
        } else {fclose(frame);return false;}
        return fclose(frame)==0;
    }
    char *save=NULL, *kind=strtok_r(line,"\t",&save);
    char *id=strtok_r(NULL,"\t",&save), *owner=strtok_r(NULL,"\t",&save), *generation=strtok_r(NULL,"\t",&save);
    int64_t gen=0,due=0,period=0;
    if (!kind || !id || !owner || !generation || !number(generation,&gen)) return false;
    CettaActivityKind k;
    if (!strcmp(kind,"work")) k=ACTIVITY_WORK;
    else if (!strcmp(kind,"notice")) k=ACTIVITY_NOTICE;
    else if (!strcmp(kind,"cancel")) k=ACTIVITY_CANCEL;
    else if (!strcmp(kind,"timer")) {
        k=ACTIVITY_TIMER;
        char *d=strtok_r(NULL,"\t",&save), *p=strtok_r(NULL,"\t",&save);
        if (!d || !p || !number(d,&due) || !number(p,&period)) return false;
    } else return false;
    if (!save || !*save) return false;
    CettaDurableStatus s=cetta_activity_record(store,id,owner,gen,k,save,due,period);
    FILE *frame=open_memstream(&out->data,&out->size); if (!frame) return false;
    fprintf(frame,"intake %s %s\n",id,cetta_durable_status_name(s));
    if (fclose(frame)) return false;
    return s==DURABLE_OK || s==DURABLE_CONFLICT || s==DURABLE_INVALID;
}
static CettaDurableStatus results(CettaDurableStore *store, Cursor *cursor, Output *out, bool *staged) {
    *staged=false;
    if (out->data) return DURABLE_OK;
    CettaDurableSnapshot values={0};
    CettaDurableStatus s=cetta_durable_snapshot(store,"activity.results",&values);
    Arena arena; arena_init(&arena);
    const CettaDurableRecord *selected=NULL;
    for (size_t i=0;s==DURABLE_OK && i<values.count;++i) {
        const CettaDurableRecord *r=&values.records[i];
        if (r->revision<cursor->revision || (r->revision==cursor->revision && r->position<=cursor->position)) continue;
        if (!selected || r->revision<selected->revision ||
            (r->revision==selected->revision && r->position<selected->position)) selected=r;
    }
    if (s==DURABLE_OK && selected) {
        Atom *value=NULL; s=cetta_durable_value_decode(&arena,selected->data,selected->size,&value);
        if (s==DURABLE_OK) {
            FILE *frame=open_memstream(&out->data,&out->size);
            if (!frame) s=DURABLE_NOMEM;
            else {
                fprintf(frame,"result %s ",selected->key); atom_print(value,frame); fputc('\n',frame);
                if (fclose(frame)) s=DURABLE_NOMEM;
                else { *cursor=(Cursor){selected->revision,selected->position}; *staged=true; }
            }
        }
    } else if (s==DURABLE_OK) *cursor=(Cursor){values.revision,INT64_MAX};
    arena_free(&arena); cetta_durable_snapshot_free(&values); return s;
}
int main(int argc, char **argv) {
    if (argc<5 || argc>10) {
        fprintf(stderr,"usage: activity-service JOURNAL LISTENER WORKER READY-CAPACITY [PRIME-POLICY [QUANTUM [WORK-LIMIT [CLEANUP-QUANTUM [ATOMIC-CLEANUP]]]]]\n");
        return 64;
    }
    bool prime=argc>=6;int64_t quantum=32,work=2000000,cleanup=0,atomic_cleanup=0;
    if ((argc>=7 && (!number(argv[6],&quantum) || quantum<=0)) ||
        (argc>=8 && (!number(argv[7],&work) || work<0)) ||
        (argc>=9 && (!number(argv[8],&cleanup) || cleanup<0)) ||
        (argc>=10 && (!number(argv[9],&atomic_cleanup) || atomic_cleanup<0 || atomic_cleanup>1))) return 64;
    struct sigaction action={0}; action.sa_handler=stop; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL); sigaction(SIGINT,&action,NULL);
    signal(SIGPIPE,SIG_IGN);
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    Arena persistent,scratch; arena_init(&persistent); arena_init(&scratch);
    Space program; space_init(&program); Registry registry; registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    CettaLibraryContext context;
    if(prime)cetta_library_context_init_for_language_profile(&context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());
    else {cetta_library_context_init(&context);cetta_eval_session_init_he_extended(&context.session);}
    cetta_library_context_set_exec_path(&context,argv[0]);eval_set_library_context(&context);
    Atom *error=NULL;
    bool loaded;
    if(prime)loaded=load_prime_policy(argv[5],&program,&persistent);
    else {
        loaded=he_library_tables_init();stdlib_load(&program,&persistent);
        loaded=loaded && telegram_he_prepare_library(&program,&persistent,true) && space_mark_imported_prefix(&program);
        loaded=loaded && cetta_library_import_module(&context,"durable:activity",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error);
    }
    if (!loaded) {
        fprintf(stderr,"activity-service: library loading failed\n");
        if (error) { atom_print(error,stderr); fputc('\n',stderr); }
        return 78;
    }
    CettaDurableStore *store=NULL;
    CettaDurableLimits storage_limits=cetta_durable_bounded_limits();
    CettaDurableStatus s=cetta_durable_open(argv[1],&storage_limits,&store);
    CettaHostProgram trusted={"activity/1",&program,&context};
    size_t capacity=(size_t)atoi(argv[4]);
    CettaActivityConfig config={
        .program=&trusted,
        .worker=argv[3],
        .listener=atoi(argv[2]),
        .worker_uid=getuid(),
        .fuel=2000000,
        .ready_capacity=capacity,
        .worker_peer_capacity=capacity,
        .cleanup_quantum=(uint64_t)cleanup,
        .allow_atomic_cleanup=atomic_cleanup!=0,
    };
    if(prime)config.planning=(CettaHostPlanLimits){.quantum=(uint64_t)quantum,.work_limit=(uint64_t)work,
        .preparation_inspections=1000000,.preparation_bytes=64u*1024u*1024u,.results=2,
        .return_slice={.preparation=(uint64_t)quantum,.transport=(uint64_t)quantum,
            .limit_preparation=true,.limit_transport=true}};
    CettaActivityRuntime *runtime=NULL;
    if (s==DURABLE_OK) s=cetta_activity_new(store,&config,&runtime);
    if (s!=DURABLE_OK) { fprintf(stderr,"activity-service: %s\n",cetta_durable_status_name(s)); return 78; }
    int flags=fcntl(STDIN_FILENO,F_GETFL);
    if (flags<0 || fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)<0) s=DURABLE_IO;
    flags=fcntl(STDOUT_FILENO,F_GETFL);
    if (flags<0 || fcntl(STDOUT_FILENO,F_SETFL,flags|O_NONBLOCK)<0) s=DURABLE_IO;
    char input[65536+256]={0}; size_t used=0; Cursor cursor={-1,-1};
    Output output={strdup("ready\n"),6,0};
    uint64_t max_loop_us=0;
    if (!output.data) s=DURABLE_NOMEM;
    while (!stopped && s==DURABLE_OK) {
        uint64_t loop_started=microseconds();
        s=flush_output(&output);
        if (s==DURABLE_OK && !output.data) {
            if (!strchr(input,'\n')) {
                ssize_t n=read(STDIN_FILENO,input+used,sizeof(input)-used-1);
                if (n>0) { used+=(size_t)n; input[used]=0; }
                else if (!n) { if (used) s=DURABLE_INVALID; else stopped=1; }
                else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) s=DURABLE_IO;
            }
            char *end=strchr(input,'\n');
            if (s==DURABLE_OK && end) {
                *end=0;
                if (!record(store,runtime,input,&output)) s=DURABLE_INVALID;
                size_t consumed=(size_t)(end+1-input); used-=consumed;
                memmove(input,end+1,used); input[used]=0;
            }
            if (used==sizeof(input)-1) s=DURABLE_LIMIT;
        }
        if (s==DURABLE_OK && !stopped) s=cetta_activity_step(runtime,2);
        if (s==DURABLE_OK) s=flush_output(&output);
        bool staged=false;
        if (s==DURABLE_OK) s=results(store,&cursor,&output,&staged);
        if (s==DURABLE_OK) s=flush_output(&output);
        uint64_t elapsed=microseconds()-loop_started;
        if (elapsed>max_loop_us) max_loop_us=elapsed;
        if (s==DURABLE_OK && !stopped) s=cetta_activity_wait_io(runtime,
            output.data?-1:STDIN_FILENO,output.data?STDOUT_FILENO:-1,
            !output.data && (staged || strchr(input,'\n'))?0:1000);
    }
    /* Unread output remains in the journal for replay; shutdown never waits
     * for a stalled consumer. At most one formatted frame was owned here. */
    free(output.data);
    CettaActivityStats stats; cetta_activity_stats(runtime,&stats);
    CettaDurableUsage usage={0}; cetta_durable_usage(store,&usage);
    struct rusage resources; getrusage(RUSAGE_SELF,&resources);
    dprintf(STDOUT_FILENO,"stats {\"steps\":%" PRIu64 ",\"accepted\":%" PRIu64 ",\"published\":%" PRIu64
        ",\"blocked\":%" PRIu64 ",\"timer_events\":%" PRIu64 ",\"max_step_us\":%" PRIu64 ",\"max_loop_us\":%" PRIu64
        ",\"ready\":%zu,\"watch_bytes\":%zu,\"plan_polls\":%" PRIu64 ",\"plan_work\":%" PRIu64
        ",\"plan_setup\":%" PRIu64 ",\"plan_setup_bytes\":%" PRIu64
        ",\"plan_preparation\":%" PRIu64 ",\"plan_transport\":%" PRIu64
        ",\"plan_receipt_events\":%" PRIu64 ",\"plan_receipt_units\":%" PRIu64
        ",\"plan_admission\":%" PRIu64 ",\"plan_producer\":%" PRIu64
        ",\"plan_consumer\":%" PRIu64 ",\"plan_final\":%" PRIu64
        ",\"plan_rotations\":%" PRIu64 ",\"plan_retirements\":%" PRIu64
        ",\"plan_cleanup_polls\":%" PRIu64 ",\"plan_cleanup_work\":%" PRIu64
        ",\"cleanup_polls\":%" PRIu64 ",\"cleanup_completed\":%" PRIu64
        ",\"cleanup_atomic_waits\":%" PRIu64 ",\"cleanup_units\":%" PRIu64
        ",\"slot_admission\":%" PRIu64 ",\"slot_preparation\":%" PRIu64
        ",\"slot_execution\":%" PRIu64 ",\"slot_delivery\":%" PRIu64
        ",\"slot_teardown\":%" PRIu64 ",\"slot_arrivals\":%" PRIu64
        ",\"slot_bookkeeping\":%" PRIu64 ",\"slot_units\":%" PRIu64
        ",\"max_slot_units\":%" PRIu64
        ",\"live_bytes\":%" PRId64 ",\"peak_rss_kib\":%ld}\n",
        stats.steps,stats.accepted,stats.published,stats.blocked,stats.timer_events,stats.max_step_us,max_loop_us,
        stats.ready,stats.watch_bytes,stats.plan_polls,stats.plan_work,
        stats.plan_setup,stats.plan_setup_bytes,
        stats.plan_preparation,stats.plan_transport,
        stats.plan_receipt_events,stats.plan_receipt_units,
        stats.plan_protocol.admission,stats.plan_protocol.producer,
        stats.plan_protocol.consumer,stats.plan_protocol.final_is_current,
        stats.plan_protocol.rotations,stats.plan_protocol.retirements,
        stats.plan_protocol.cleanup_polls,stats.plan_protocol.cleanup_work,
        stats.cleanup_polls,stats.cleanup_completed,stats.cleanup_atomic_waits,stats.cleanup_units,
        stats.slot_admission,stats.slot_preparation,stats.slot_execution,
        stats.slot_delivery,stats.slot_teardown,stats.slot_arrivals,
        stats.slot_bookkeeping,stats.slot_units,stats.max_slot_units,
        usage.live_bytes,resources.ru_maxrss);
    cetta_activity_free(runtime); cetta_durable_close(store);
    cetta_library_context_free(&context); eval_set_library_context(NULL);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
    if (s!=DURABLE_OK) { fprintf(stderr,"activity-service: %s\n",cetta_durable_status_name(s)); return 1; }
    return 0;
}
