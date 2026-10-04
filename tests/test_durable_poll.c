#define _POSIX_C_SOURCE 200809L
#include "durable_dispatch.h"
#include "durable_inbox.h"
#include "durable_value.h"
#include "telegram_intake.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "telegram_he_setup.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static CettaDurableStore *store;
static Arena arena;
static Arena scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaHostProgram trusted={"telegram/intake/1",&program,&context};
static Atom *policy;
static atomic_uint health;
static void degraded(void *context, const char *key, CettaDurableStatus status) {
    (void)context; (void)key; assert(status!=DURABLE_OK); atomic_fetch_add(&health,1);
}
static const char *text(const Atom *a) {
    assert(a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING); return a->ground.sval;
}
static Atom *response(CettaInboxWindow *w, const char *kind) {
    const CettaDurableRecord *r=cetta_inbox_poll_response(w); assert(r);
    Atom *a=NULL; assert(cetta_durable_value_decode(&arena,r->data,r->size,&a)==DURABLE_OK);
    assert(a->kind==ATOM_EXPR && a->expr.len==7 && atom_is_symbol(a->expr.elems[0],"host:poll"));
    assert(!strcmp(text(a->expr.elems[2]),cetta_inbox_source(w)));
    assert(a->expr.elems[3]->ground.ival==cetta_inbox_offset(w));
    assert(atom_is_symbol(a->expr.elems[4],kind)); return a;
}
static CettaInboxWindow *wait_response(CettaDurableDispatch *d, const char *source) {
    uint64_t generation=0;
    for (unsigned i=0;i<100;++i) {
        CettaInboxWindow *w=NULL;
        CettaDurableStatus s=cetta_inbox_recover_poll(store,source,&w);
        if (s==DURABLE_OK) return w;
        assert(s==DURABLE_PRECONDITION);
        generation=cetta_dispatch_wait(d,generation,100);
    }
    assert(false); return NULL;
}
static CettaDurableStatus start(CettaDurableDispatch *d, const CettaTelegramCredential *c,
                               const char *source, unsigned limit) {
    const char *updates[]={"message","callback_query"};
    CettaTelegramPoll p={c,source,0,limit,updates,2}; return cetta_dispatch_poll(d,&p);
}
static CettaDurableStatus classify(CettaInboxWindow *w, CettaInboxCommit *out) {
    CettaTelegramIntake *t=NULL;
    assert(cetta_telegram_intake_begin(store,cetta_inbox_source(w),trusted.version,&t)==DURABLE_OK);
    assert(cetta_telegram_intake_evaluate(t,&trusted,policy,1000000)==DURABLE_OK);
    CettaDurableStatus s=cetta_telegram_intake_commit(t,out);
    cetta_telegram_intake_free(t); return s;
}
static void empty(CettaInboxWindow *w) {
    CettaInboxCommit out; int64_t offset=cetta_inbox_offset(w);
    assert(classify(w,&out)==DURABLE_OK && out.next_offset==offset && !out.inserted);
    cetta_inbox_window_free(w);
}
int main(int argc, char **argv) {
    assert(argc==6);
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&arena); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&arena,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    assert(he_library_tables_init());
    stdlib_load(&program,&arena);
    assert(telegram_he_prepare_library(&program,&arena,true));
    assert(space_mark_imported_prefix(&program));
    Atom *error=NULL;
    assert(cetta_library_import_module(&context,"durable:telegram",&program,false,&scratch,&arena,&registry,1000000,NULL,&error));
    size_t pos=0; policy=parse_sexpr(&arena,"(telegram:policy 1 (42) False () ())",&pos); assert(policy);
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.record_bytes=4096;
    bool pressure=!strcmp(argv[1],"pressure"); if (pressure) limits.records=1;
    assert(cetta_durable_open(argv[2],&limits,&store)==DURABLE_OK);
    int fd=open(argv[3],O_RDONLY); assert(fd>=0);
    CettaTelegramCredentialConfig cc={argv[4],strcmp(argv[5],"-")?argv[5]:NULL,true};
    CettaTelegramCredential *credential=NULL;
    assert(cetta_telegram_credential_read(fd,&cc,&credential)==TELEGRAM_CREDENTIAL_OK); close(fd);
    CettaDispatchConfig config={NULL,0,3000,65536,NULL,degraded};
    CettaDurableDispatch *d=NULL; assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
    CettaInboxWindow *w=NULL; CettaInboxCommit out;
    if (!strcmp(argv[1],"crash")) {
        assert(start(d,credential,"bot",9)==DURABLE_OK);
        for (;;) pause(); /* Mock kills after receiving the request. */
    }
    if (!strcmp(argv[1],"pending")) {
        assert(start(d,credential,"bot",2)==DURABLE_OK);
        w=wait_response(d,"bot"); response(w,"observed");
        _exit(0); /* Poll persisted, no classification and no clean close. */
    }
    if (!strcmp(argv[1],"recover")) {
        assert(start(d,credential,"bot",2)==DURABLE_PRECONDITION);
        w=wait_response(d,"bot");
        assert(classify(w,&out)==DURABLE_OK && out.inserted==2 && out.next_offset==44);
        cetta_inbox_window_free(w);
        assert(start(d,credential,"bot",2)==DURABLE_OK);
        w=wait_response(d,"bot");
        assert(!strcmp(text(response(w,"observed")->expr.elems[6]),"{\"ok\":true,\"result\":[]}")); empty(w);
    } else {
        assert(pressure || !strcmp(argv[1],"normal") || !strcmp(argv[1],"after-crash"));
        CettaTelegramPoll invalid={credential,"bot",3,2,NULL,0};
        assert(cetta_dispatch_poll(d,&invalid)==DURABLE_INVALID);
        invalid.wait_seconds=0; invalid.limit=0;
        assert(cetta_dispatch_poll(d,&invalid)==DURABLE_INVALID);
        const char *bad[]={"message\",\"offset\":999"};
        invalid.limit=2; invalid.allowed_updates=bad; invalid.update_count=1;
        assert(cetta_dispatch_poll(d,&invalid)==DURABLE_INVALID);
        assert(start(d,credential,"bot",2)==DURABLE_OK);
        CettaDurableStatus s=start(d,credential,"bot",2);
        assert(s==DURABLE_BUSY || s==DURABLE_PRECONDITION);
        w=wait_response(d,"bot");
        assert(cetta_inbox_offset(w)==0);
        assert(start(d,credential,"bot",2)==DURABLE_PRECONDITION);
        if (pressure) {
            assert(classify(w,&out)==DURABLE_LIMIT && out.next_offset<0);
            cetta_inbox_window_free(w);
            assert(start(d,credential,"bot",2)==DURABLE_PRECONDITION);
            assert(cetta_dispatch_free(d)==0); d=NULL;
            cetta_durable_close(store);
            assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
            assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
            w=wait_response(d,"bot"); assert(cetta_inbox_offset(w)==0);
        }
        assert(classify(w,&out)==DURABLE_OK && out.next_offset==44 && out.inserted==2);
        cetta_inbox_window_free(w);
        assert(start(d,credential,"bot",2)==DURABLE_OK);
        w=wait_response(d,"bot");
        assert(!strcmp(text(response(w,"observed")->expr.elems[6]),"{\"ok\":true,\"result\":[]}")); empty(w);
        if (!strcmp(argv[1],"normal")) {
            const char *sources[]={"reflection","lost","oversize","rate-limit","malformed"};
            const char *kinds[]={"privacy-suppressed","uncertain","unrecordable","observed","observed"};
            for (unsigned i=0;i<5;++i) {
                assert(start(d,credential,sources[i],i+3)==DURABLE_OK);
                w=wait_response(d,sources[i]); assert(cetta_inbox_offset(w)==0);
                Atom *a=response(w,kinds[i]);
                if (i==0 || i==2) assert(!*text(a->expr.elems[6]));
                if (i==3) assert(a->expr.elems[5]->expr.elems[3]->ground.ival==429);
                if (i==4) assert(!strcmp(text(a->expr.elems[6]),"{"));
                assert(start(d,credential,sources[i],2)==DURABLE_PRECONDITION);
                assert(classify(w,&out)==DURABLE_PRECONDITION && out.next_offset==-1);
                cetta_inbox_window_free(w); /* Retained for policy/repair, not silently discarded. */
            }
            /* A retry decision survives owner/store restart. Nothing reaches
             * HTTP before its committed deadline; the old offset is reused. */
            CettaTelegramIntake *retry=NULL; int64_t revision;
            assert(cetta_telegram_intake_begin(store,"rate-limit",trusted.version,&retry)==DURABLE_OK);
            assert(cetta_telegram_intake_evaluate_recovery(retry,&trusted,policy,1000,1000000)==DURABLE_OK);
            assert(cetta_telegram_intake_commit_recovery(retry,&revision)==DURABLE_OK);
            cetta_telegram_intake_free(retry);
            assert(cetta_dispatch_free(d)==0); cetta_durable_close(store);
            assert(cetta_durable_open(argv[2],&limits,&store)==DURABLE_OK);
            assert(cetta_dispatch_new(store,&config,&d)==DURABLE_OK);
            const char *updates[]={"message","callback_query"};
            CettaTelegramPoll again={credential,"rate-limit",0,2,updates,2};
            assert(cetta_dispatch_poll_at(d,&again,1999)==DURABLE_PRECONDITION);
            assert(cetta_dispatch_poll_at(d,&again,2000)==DURABLE_OK);
            w=wait_response(d,"rate-limit");
            assert(classify(w,&out)==DURABLE_OK && out.next_offset==44); cetta_inbox_window_free(w);
            /* A lost getUpdates response is repeat-safe at the durable offset.
             * This policy must never be reused for sendMessage. */
            assert(cetta_telegram_intake_begin(store,"lost",trusted.version,&retry)==DURABLE_OK);
            assert(cetta_telegram_intake_evaluate_recovery(retry,&trusted,policy,1000,1000000)==DURABLE_OK);
            assert(cetta_telegram_intake_commit_recovery(retry,&revision)==DURABLE_OK);
            cetta_telegram_intake_free(retry); again.source="lost";
            assert(cetta_dispatch_poll_at(d,&again,1999)==DURABLE_PRECONDITION);
            assert(cetta_dispatch_poll_at(d,&again,2000)==DURABLE_OK);
            w=wait_response(d,"lost");
            assert(classify(w,&out)==DURABLE_OK && out.next_offset==44); cetta_inbox_window_free(w);
            /* An unrelated source can continue despite held failed polls. */
            assert(start(d,credential,"other-bot",2)==DURABLE_OK);
            w=wait_response(d,"other-bot");
            assert(classify(w,&out)==DURABLE_OK && out.next_offset==44); cetta_inbox_window_free(w);
        }
    }
    assert(!atomic_load(&health));
    assert(cetta_dispatch_free(d)==0);
    cetta_durable_close(store); cetta_telegram_credential_free(credential);
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch);
    arena_free(&arena); g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("durable poll: committed offsets, retained screened responses, atomic classification, recovery and isolated faults passed");
}
