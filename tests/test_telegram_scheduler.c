#define _POSIX_C_SOURCE 200809L
#include "telegram_scheduler.h"
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
static volatile sig_atomic_t stopped;
static atomic_uint transport_faults;
static unsigned faults;
static void stop(int signal_number) { (void)signal_number; stopped=1; }
static void http_fault(void *unused,const char *key,CettaDurableStatus s) {
    (void)unused; (void)key; assert(s!=DURABLE_OK); atomic_fetch_add(&transport_faults,1);
}
static void app_fault(void *unused,const char *component,const char *key,CettaDurableStatus s) {
    (void)unused; (void)key; fprintf(stderr,"fault %s %s\n",component,cetta_durable_status_name(s)); ++faults;
}
static void service_fault(void *unused,const char *component,CettaDurableStatus s) { app_fault(unused,component,"",s); }
int main(int argc,char **argv) {
    assert(argc==6 || argc==7); /* journal, credential, origin, CA, listener, optional pause after acceptance */
    bool pause=argc==7 && !strcmp(argv[6],"pause"), quota=argc==7 && !strcmp(argv[6],"quota");
    struct sigaction action={0}; action.sa_handler=stop; sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGTERM,&action,NULL) && !sigaction(SIGINT,&action,NULL));
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    Arena persistent,scratch; arena_init(&persistent); arena_init(&scratch);
    Space program; space_init(&program); Registry registry; registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    CettaLibraryContext context; cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    assert(he_library_tables_init());
    stdlib_load(&program,&persistent);
    assert(telegram_he_prepare_library(&program,&persistent,true));
    assert(space_mark_imported_prefix(&program));
    Atom *error=NULL; assert(cetta_library_import_module(&context,"durable:telegram_agent",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error));
    CettaHostProgram trusted={"telegram-agent/1",&program,&context};
    CettaDurableStore *store=NULL; assert(cetta_durable_open(argv[1],NULL,&store)==DURABLE_OK);
    int fd=open(argv[2],O_RDONLY|O_NOFOLLOW); assert(fd>=0);
    CettaTelegramCredentialConfig cc={argv[3],strcmp(argv[4],"-")?argv[4]:NULL,true};
    CettaTelegramCredential *credential=NULL; assert(cetta_telegram_credential_read(fd,&cc,&credential)==TELEGRAM_CREDENTIAL_OK); close(fd);
    const int64_t chats[]={42,84}; CettaTelegramActionPolicy actions={chats,2,7};
    const char *updates[]={"message"}; size_t pos=0;
    Atom *policy=parse_sexpr(&persistent,"(telegram:policy 1 (42 84) False () ())",&pos); assert(policy);
    CettaServiceSource source={{credential,"bot",1,100,updates,1},&trusted,policy,1000000};
    CettaDispatchChannel channel={"telegram.action","1",credential,&actions,cetta_telegram_action_plan};
    CettaServiceConfig config={{&channel,1,3000,65536,NULL,http_fault},&source,1,atoi(argv[5]),getuid(),"brain",NULL,service_fault};
    CettaDurableService *service=NULL; assert(cetta_service_new(store,&config,&service)==DURABLE_OK);
    CettaTelegramSchedulerConfig ac={{&trusted,"bot","brain",&actions,2000000},256,256,8*1024*1024,NULL,app_fault};
    if (quota) ac.pending_effects=ac.pending_inputs=1;
    CettaTelegramScheduler *scheduler=NULL;
    CettaTelegramSchedulerConfig wrong=ac; wrong.agent.source="other-bot";
    assert(cetta_telegram_scheduler_new(store,service,&wrong,&scheduler)==DURABLE_INVALID && !scheduler);
    wrong=ac; wrong.agent.worker="other-worker";
    assert(cetta_telegram_scheduler_new(store,service,&wrong,&scheduler)==DURABLE_INVALID && !scheduler);
    CettaTelegramActionPolicy other_policy=actions; wrong=ac; wrong.agent.actions=&other_policy;
    assert(cetta_telegram_scheduler_new(store,service,&wrong,&scheduler)==DURABLE_INVALID && !scheduler);
    assert(cetta_telegram_scheduler_new(store,service,&ac,&scheduler)==DURABLE_OK);
    while (!stopped) {
        assert(cetta_service_step(service,25)==DURABLE_OK);
        CettaDurableStatus status=cetta_telegram_scheduler_step(scheduler,pause?1:8);
        assert(status==DURABLE_OK);
        if (pause) {
            CettaTelegramSchedulerStats now; cetta_telegram_scheduler_stats(scheduler,&now);
            if (now.accepted==3) { puts("paused-after-accept"); fflush(stdout); raise(SIGSTOP); }
        }
    }
    CettaTelegramSchedulerStats stats; cetta_telegram_scheduler_stats(scheduler,&stats);
    cetta_telegram_scheduler_free(scheduler); assert(!cetta_service_free(service));
    printf("evaluations=%llu accepted=%llu blocked=%llu admissions=%llu published=%llu faults=%u transport_faults=%u\n",
        (unsigned long long)stats.evaluations,(unsigned long long)stats.accepted,(unsigned long long)stats.blocked,
        (unsigned long long)stats.admissions,(unsigned long long)stats.published,faults,atomic_load(&transport_faults));
    cetta_telegram_credential_free(credential); cetta_durable_close(store);
    cetta_library_context_free(&context); eval_set_library_context(NULL); registry_free(&registry); space_free(&program);
    arena_free(&scratch); arena_free(&persistent); var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
}
