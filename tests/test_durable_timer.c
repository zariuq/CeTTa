#define _POSIX_C_SOURCE 200809L
#include "durable_timer.h"
#include "durable_host.h"
#include "durable_value.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static Arena persistent, scratch;
static Space program;
static Registry registry;
static CettaLibraryContext context;
static CettaDurableStore *store;
static CettaHostProgram trusted={"timer/he/1",&program,&context};
static CettaTimerCursor cursor;
static unsigned faults, inputs;
static Atom *parse(const char *s) {
    size_t p=0; Atom *a=parse_sexpr(&persistent,s,&p); assert(a && p==strlen(s)); return a;
}
static void fault(void *ctx, const char *key, CettaDurableStatus status) {
    (void)ctx; (void)key; assert(status!=DURABLE_OK); ++faults;
}
static void write_record(const char *space, const char *key, Atom *value) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    unsigned char *bytes=NULL; size_t size=0;
    assert(cetta_durable_value_encode(value,&bytes,&size)==DURABLE_OK);
    CettaDurableOp op={cetta_durable_observation_view(o,0)->count?DURABLE_REPLACE:DURABLE_INSERT,space,key,bytes,size}; int64_t rev;
    assert(cetta_durable_commit_observed(store,o,&op,1,&rev)==DURABLE_OK);
    free(bytes); cetta_durable_observation_free(o);
}
static Atom *get(const char *space, const char *key) {
    CettaDurableScope q={DURABLE_KEY,space,key}; CettaDurableObservation *o=NULL;
    assert(cetta_durable_observe(store,&q,1,&o)==DURABLE_OK);
    const CettaDurableSnapshot *v=cetta_durable_observation_view(o,0); Atom *a=NULL;
    if (v->count) assert(cetta_durable_value_decode(&persistent,v->records[0].data,v->records[0].size,&a)==DURABLE_OK);
    cetta_durable_observation_free(o); return a;
}
static void accept(const char *payload, char key[64]) {
    char input[32], expr[1024]; snprintf(input,sizeof(input),"clock-%u",++inputs);
    write_record("host.inbox",input,parse("(clock 1000)"));
    CettaHostChannelGrant channel={"timer.after","1",cetta_timer_validate,NULL};
    CettaHostDecisionSpec spec={input,"actor","timer/he/1",NULL,0,&channel,1};
    CettaHostDecision *d=NULL; assert(cetta_host_begin(store,&spec,&d)==DURABLE_OK);
    snprintf(expr,sizeof(expr),"(match &self (host:record 0 \"%s\" (clock $now)) "
        "(let $request %s (host:transition waiting () ($request))))",input,payload);
    assert(cetta_host_evaluate(d,&trusted,parse(expr),10000)==DURABLE_OK);
    CettaHostCommit c; assert(cetta_host_accept(d,0,&c)==DURABLE_OK);
    snprintf(key,64,"%s/0",c.commit_key); cetta_host_decision_free(d);
}
static void event(const char *kind, int64_t due, int64_t now, int64_t represented) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,"host.inbox",&s)==DURABLE_OK);
    assert(s.count==1);
    Atom *a=NULL; assert(cetta_durable_value_decode(&persistent,s.records[0].data,s.records[0].size,&a)==DURABLE_OK);
    assert(a->kind==ATOM_EXPR && a->expr.len==10 && atom_is_symbol(a->expr.elems[0],"host:timer-event"));
    char key[128]; snprintf(key,sizeof(key),"timer/%s/%lld",a->expr.elems[2]->ground.sval,
        (long long)a->expr.elems[3]->ground.ival);
    assert(!strcmp(key,s.records[0].key));
    assert(atom_is_symbol(a->expr.elems[4],kind));
    assert(a->expr.elems[5]->ground.ival==due && a->expr.elems[6]->ground.ival==now && a->expr.elems[7]->ground.ival==represented);
    CettaDurableOp op={DURABLE_REMOVE,"host.inbox",s.records[0].key,NULL,0}; int64_t rev;
    assert(cetta_durable_commit(store,s.epoch,s.revision,&op,1,&rev)==DURABLE_OK);
    cetta_durable_snapshot_free(&s);
}
static CettaTimerTick tick(int64_t now, size_t budget, size_t emitted) {
    CettaTimerTick out;
    assert(cetta_timer_tick(store,&cursor,now,budget,fault,NULL,&out)==DURABLE_OK && out.emitted==emitted);
    assert(!out.faults); return out;
}
static void drain(void) {
    CettaDurableSnapshot s; assert(cetta_durable_snapshot(store,"host.inbox",&s)==DURABLE_OK);
    if (s.count) {
        CettaDurableOp *ops=calloc(s.count,sizeof(*ops)); assert(ops);
        for (size_t i=0;i<s.count;++i) ops[i]=(CettaDurableOp){DURABLE_REMOVE,"host.inbox",s.records[i].key,NULL,0};
        int64_t rev; assert(cetta_durable_commit(store,s.epoch,s.revision,ops,s.count,&rev)==DURABLE_OK); free(ops);
    }
    cetta_durable_snapshot_free(&s);
}
static void cleanup(const char *path) {
    assert(!unlink(path)); char side[300];
    snprintf(side,sizeof(side),"%s-wal",path); unlink(side);
    snprintf(side,sizeof(side),"%s-shm",path); unlink(side);
}
int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch); space_init(&program); registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    Atom *error=NULL;
    assert(cetta_library_import_module(&context,"durable:timer",&program,false,&scratch,&persistent,&registry,10000,NULL,&error));
    if (argc==3) {
        assert(cetta_durable_open(argv[2],NULL,&store)==DURABLE_OK);
        if (!strcmp(argv[1],"fire")) tick(1100,64,1);
        else assert(!strcmp(argv[1],"before"));
        _exit(0);
    }
    assert(argc==1);
    CettaClockSample sampled; assert(cetta_clock_sample(&sampled));
    CettaClockSample fake={1000,500};
    assert(cetta_timer_wait_ms(1800,&fake,500)==800);
    assert(cetta_timer_wait_ms(1800,&fake,750)==550);
    assert(cetta_timer_wait_ms(1800,&fake,1400)==0);
    assert(cetta_timer_wait_ms(INT64_MAX,&fake,500)==1000);
    assert(cetta_timer_wait_ms(-1,&fake,500)==1000);
    assert(cetta_timer_wait_ms(1800,&fake,400)==800);
    char directory[]="/tmp/cetta-timer-XXXXXX"; assert(mkdtemp(directory));
    char path[256]; snprintf(path,sizeof(path),"%s/journal.db",directory);
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    assert(tick(1000,64,0).next_deadline_ms==-1);
    CettaTimerTick out;
    assert(cetta_timer_tick(store,&cursor,1000,0,fault,NULL,&out)==DURABLE_INVALID);
    assert(cetta_timer_tick(store,&cursor,-1,64,fault,NULL,&out)==DURABLE_INVALID);
    assert(cetta_timer_cancel(store,"absent",1000)==DURABLE_PRECONDITION);
    assert(!cetta_timer_validate(NULL,parse("(timer:at 1 -1 0 FireOnce 0 x)"),parse("reply")));
    assert(!cetta_timer_validate(NULL,parse("(timer:at 1 100 0 unknown 0 x)"),parse("reply")));
    assert(!cetta_timer_validate(NULL,parse("(timer:at 1 100 0 FireOnce 0 $x)"),parse("reply")));
    char one[64], periodic[64], skipped[64], caught[64], other[64];
    accept("(durable:timer:after 0 $now 100 wake reply)",one);
    assert(tick(1099,64,0).next_deadline_ms==1100);
    assert(tick(900,64,0).next_deadline_ms==1100); /* Backward wall jump. */
    cetta_durable_close(store);
    for (size_t i=0;i<2;++i) {
        pid_t pid=fork(); assert(pid>=0);
        if (!pid) { execl(argv[0],argv[0],i?"fire":"before",path,(char *)NULL); _exit(127); }
        int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
        if (!i) assert(!get("host.outcomes",one));
        else { event("fired",1100,1100,1); assert(get("host.outcomes",one)); tick(9000,64,0); }
        cetta_durable_close(store);
    }
    assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    assert(cetta_timer_cancel(store,one,9000)==DURABLE_OK); tick(9000,64,0); /* No second completion. */
    accept("(durable:timer:every 0 $now 0 100 FireOnce 0 beat reply)",periodic);
    tick(1450,64,1); event("fired",1000,1450,5);
    assert(get("host.timer-state",periodic)->expr.elems[3]->ground.ival==1500);
    cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    tick(1300,64,0); /* Clock moved back; do not repeat old deadlines. */
    assert(cetta_timer_cancel(store,periodic,1450)==DURABLE_OK); event("cancelled",1500,1450,0);
    assert(!get("host.timer-state",periodic));
    accept("(durable:timer:every 0 $now 0 100 SkipMissed 60 beat reply)",skipped);
    tick(1450,64,1); event("skipped",1000,1450,4);
    tick(1450,64,1); event("fired",1400,1450,1);
    assert(cetta_timer_cancel(store,skipped,1450)==DURABLE_OK); event("cancelled",1500,1450,0);
    accept("(durable:timer:after 0 $now 0 first reply)",other);
    assert(cetta_timer_cancel(store,other,900)==DURABLE_OK); event("cancelled",1000,900,0);
    tick(2000,64,0);
    accept("(durable:timer:every 0 $now 0 1 CatchUpAll 0 beat reply)",caught);
    out=tick(2000,64,8); assert(out.more_due);
    assert(get("host.timer-state",caught)->expr.elems[3]->ground.ival==1008); drain();
    out=tick(2000,1,1); assert(out.more_due);
    assert(get("host.timer-state",caught)->expr.elems[3]->ground.ival==1009); drain();
    accept("(durable:timer:after 0 $now 500 second reply)",other);
    /* With a one-event budget, round-robin reaches the new one-shot even while
     * the periodic timer has a large catch-up backlog. */
    for (unsigned i=0;i<3 && !get("host.outcomes",other);++i) { tick(2000,1,1); drain(); }
    assert(get("host.outcomes",other));
    assert(cetta_timer_cancel(store,caught,2000)==DURABLE_OK); drain();
    accept("(durable:timer:every 0 $now 0 100 SkipMissed 60 beat reply)",skipped);
    tick(1060,64,1); event("fired",1000,1060,1); /* Grace boundary is inclusive. */
    assert(cetta_timer_cancel(store,skipped,1060)==DURABLE_OK); drain();
    /* Int64 arithmetic exhausts cleanly instead of wrapping deadlines. */
    accept("(host:send 0 (timer:at 1 9223372036854775807 1 FireOnce 0 boundary) reply)",other);
    tick(INT64_MAX,64,1); event("fired",INT64_MAX,INT64_MAX,1);
    assert(atom_is_symbol(get("host.outcomes",other)->expr.elems[3],"exhausted"));
    assert(!faults);
    /* Unsupported timer handler is retained/reported, without starving a valid timer. */
    write_record("host.outbox","bad-version",parse("(host:intent 1 \"timer.after\" \"2\" (timer:at 1 1000 0 FireOnce 0 x) reply)"));
    accept("(durable:timer:after 0 $now 0 third reply)",other);
    assert(cetta_timer_tick(store,&cursor,1000,64,fault,NULL,&out)==DURABLE_OK && out.emitted==1 && out.faults==1);
    event("fired",1000,1000,1); assert(faults==1 && !get("host.outcomes","bad-version"));
    cetta_durable_close(store); cleanup(path);
    /* Acceptance fits, but delivery does not. Never advance/delete the timer
     * unless its wake-up and terminal outcome can commit together. */
    CettaDurableLimits limits=cetta_durable_default_limits(); limits.records=4;
    assert(cetta_durable_open(path,&limits,&store)==DURABLE_OK);
    accept("(durable:timer:after 0 $now 0 retry reply)",one);
    assert(cetta_timer_tick(store,&cursor,1000,64,fault,NULL,&out)==DURABLE_LIMIT && !out.emitted);
    assert(!get("host.outcomes",one) && !get("host.timer-state",one));
    cetta_durable_close(store); assert(cetta_durable_open(path,NULL,&store)==DURABLE_OK);
    tick(1000,64,1); event("fired",1000,1000,1); tick(1000,64,0);
    cetta_durable_close(store); cleanup(path); assert(!rmdir(directory));
    eval_set_library_context(NULL); cetta_library_context_free(&context);
    registry_free(&registry); space_free(&program); arena_free(&scratch); arena_free(&persistent);
    g_var_intern=NULL; var_intern_free(&vars); g_symbols=NULL; symbol_table_free(&symbols);
    puts("durable timers: accepted requests, clock jumps, overdue policies, bounded/fair catch-up, cancellation, overflow, storage failure and crash recovery passed");
}
