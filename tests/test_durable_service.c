#define _POSIX_C_SOURCE 200809L
#include "durable_service.h"
#include "durable_worker_host.h"
#include "durable_value.h"
#include "telegram_action.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static CettaHostProgram trusted={"service-test/1",&program,&context};
static volatile sig_atomic_t stopped;
static atomic_uint http_faults;
static unsigned host_faults;
static void stop(int signal_number) { (void)signal_number; stopped=1; }
static void host_fault(void *unused, const char *component, CettaDurableStatus status) {
    (void)unused; assert(component && status!=DURABLE_OK); ++host_faults;
}
static void http_fault(void *unused, const char *key, CettaDurableStatus status) {
    (void)unused; (void)key; assert(status!=DURABLE_OK); atomic_fetch_add(&http_faults,1);
}
static Atom *parse(const char *text) { size_t pos=0; Atom *a=parse_sexpr(&persistent,text,&pos); assert(a && pos==strlen(text)); return a; }
static const int64_t chats[]={42};
static CettaTelegramActionPolicy action_policy={chats,1,7};
static void seed(bool crash_send) {
    CettaDurableSnapshot snapshot; assert(cetta_durable_snapshot(store,"host.outbox",&snapshot)==DURABLE_OK);
    bool seeded=snapshot.count!=0; cetta_durable_snapshot_free(&snapshot);
    if (seeded && !crash_send) return;
    CettaClockSample now; assert(cetta_clock_sample(&now));
    char clock[80]; snprintf(clock,sizeof(clock),"(clock %lld)",(long long)now.utc_ms);
    unsigned char *bytes; size_t size; assert(cetta_durable_value_encode(parse(clock),&bytes,&size)==DURABLE_OK);
    assert(cetta_durable_snapshot(store,NULL,&snapshot)==DURABLE_OK);
    const char *input=crash_send?"crash":"boot";
    CettaDurableOp op={DURABLE_INSERT,"host.inbox",input,bytes,size}; int64_t revision;
    assert(cetta_durable_commit(store,snapshot.epoch,snapshot.revision,&op,1,&revision)==DURABLE_OK);
    cetta_durable_snapshot_free(&snapshot); free(bytes);
    CettaHostChannelGrant channels[]={{"worker.request","1",cetta_worker_validate,"brain"},
        {"timer.after","1",cetta_timer_validate,NULL},{"telegram.action","1",cetta_telegram_action_validate,&action_policy}};
    CettaHostDecisionSpec spec={input,"actor",trusted.version,NULL,0,channels,3};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    const char *expr=crash_send?"(host:transition waiting () ((host:send 2 (telegram:delete-message 1 42 9) crash-reply)))":
        "(match &self (host:record 0 \"boot\" (clock $now)) "
        "(host:transition waiting () ((host:send 0 (worker:request 1 \"brain\" \"service observation\") reply) "
        "(host:send 1 (timer:at 1 (+ $now 300) 0 FireOnce 0 wake) timer-reply) "
        "(host:send 2 (telegram:send-text 1 42 17 3 \"fixture-one\" \"plain\") reply) (host:send 2 (telegram:edit-text 1 42 9 \"fixture-two\" \"HTML\") reply))))";
    assert(cetta_host_evaluate(d,&trusted,parse(expr),10000)==DURABLE_OK);
    CettaHostCommit commit; assert(cetta_host_accept(d,0,&commit)==DURABLE_OK); cetta_host_decision_free(d);
}
int main(int argc, char **argv) {
    assert(argc==7); /* mode, journal, private credential, origin, CA, inherited listener */
    struct sigaction action={0}; action.sa_handler=stop; sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGTERM,&action,NULL) && !sigaction(SIGINT,&action,NULL));
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    assert(he_library_tables_init());
    stdlib_load(&program,&persistent);
    assert(telegram_he_prepare_library(&program,&persistent,true));
    assert(space_mark_imported_prefix(&program));
    Atom *error=NULL;
    assert(cetta_library_import_module(&context,"durable:telegram",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error));
    assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
    int fd=open(argv[3],O_RDONLY|O_NOFOLLOW); assert(fd>=0);
    CettaTelegramCredentialConfig credential_config={argv[4],strcmp(argv[5],"-")?argv[5]:NULL,true};
    CettaTelegramCredential *credential=NULL;
    assert(cetta_telegram_credential_read(fd,&credential_config,&credential)==TELEGRAM_CREDENTIAL_OK); close(fd);
    seed(!strcmp(argv[1],"crash-send"));
    bool quota=!strcmp(argv[1],"quota");
    if (quota) {
        CettaDurableSnapshot all; assert(cetta_durable_snapshot(store,NULL,&all)==DURABLE_OK);
        CettaDurableLimits limits=cetta_durable_default_limits(); limits.records=all.count;
        cetta_durable_snapshot_free(&all); cetta_durable_close(store);
        assert(cetta_durable_open(argv[2],&limits,&store)==DURABLE_OK);
    }
    const char *updates[]={"message","callback_query"};
    CettaServiceSource source={{credential,"bot",1,100,updates,2},&trusted,parse("(telegram:policy 1 (42) False () ())"),1000000};
    if (!strcmp(argv[1],"fuel")) source.fuel=1;
    CettaDispatchChannel channel={"telegram.action","1",credential,&action_policy,cetta_telegram_action_plan};
    CettaServiceConfig config={{&channel,1,3000,65536,NULL,http_fault},&source,1,atoi(argv[6]),getuid(),"brain",NULL,host_fault};
    CettaDurableService *service=NULL;
    CettaServiceSource duplicate[]={source,source}; CettaServiceConfig invalid=config;
    invalid.sources=duplicate; invalid.source_count=2;
    assert(cetta_service_new(store,&invalid,&service)==DURABLE_INVALID && !service);
    context.session.language_id=CETTA_LANGUAGE_PETTA;
    assert(cetta_service_new(store,&config,&service)==DURABLE_INVALID && !service);
    context.session.language_id=CETTA_LANGUAGE_HE;
    if (quota) config.source_count=0;
    assert(cetta_service_new(store,&config,&service)==DURABLE_OK);
    assert(cetta_service_step(service,101)==DURABLE_INVALID);
    if (quota) {
        CettaDurableStatus status=DURABLE_OK;
        for (unsigned i=0;i<100 && status==DURABLE_OK;++i) status=cetta_service_step(service,25);
        assert(status==DURABLE_LIMIT && host_faults==1);
        assert(cetta_service_step(service,0)==DURABLE_LIMIT && host_faults==1);
        assert(cetta_service_submit(service,"unused")==DURABLE_LIMIT);
        assert(cetta_service_cancel(service,"unused")==DURABLE_LIMIT);
        stopped=1;
    }
    /* This fixture's trusted application has already selected these independent
     * requests. The service mechanism does not choose ordering or retry sends. */
    CettaDurableSnapshot outbox; assert(cetta_durable_snapshot(store,"host.outbox",&outbox)==DURABLE_OK);
    for (size_t i=0;!quota && i<outbox.count;++i) {
        Atom *intent=NULL; assert(cetta_durable_value_decode(&persistent,outbox.records[i].data,outbox.records[i].size,&intent)==DURABLE_OK);
        const char *name=intent->expr.elems[2]->ground.sval;
        if (!strcmp(name,"worker.request")) { char id[65]; assert(cetta_worker_register(store,outbox.records[i].key,"brain",id)==DURABLE_OK); }
        if (!strcmp(name,"telegram.action")) {
            CettaDurableStatus status=cetta_service_submit(service,outbox.records[i].key);
            assert(status==DURABLE_OK || status==DURABLE_PRECONDITION);
        }
    }
    cetta_durable_snapshot_free(&outbox);
    while (!stopped) assert(cetta_service_step(service,25)==DURABLE_OK);
    CettaServiceStats stats; cetta_service_stats(service,&stats);
    assert(!cetta_service_free(service));
    printf("steps=%llu worker=%llu batches=%llu inputs=%llu recoveries=%llu timers=%llu host_faults=%u http_faults=%u\n",
        (unsigned long long)stats.steps,(unsigned long long)stats.worker_requests,(unsigned long long)stats.poll_batches,
        (unsigned long long)stats.inputs,(unsigned long long)stats.recoveries,(unsigned long long)stats.timer_events,host_faults,atomic_load(&http_faults));
    cetta_telegram_credential_free(credential); cetta_durable_close(store);
    cetta_library_context_free(&context); eval_set_library_context(NULL);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
}
