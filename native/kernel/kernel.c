#include "kernel.h"
#include "pair.h"
#include "observe.h"
#include "reference.h"
#include "kernel_private.h"
#include "constructor.h"
#include "batch.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct CettaKernelCheckpoint { uint64_t session_identity;NmSolution *solution; };
typedef struct { NmUnifyProgram *unifier;NmEmitProgram *emitter;CettaKernelPairProgram *pair;CettaKernelObserveProgram *observe;CettaKernelConstructorProgram *constructor; } Programs;
static pthread_mutex_t programs_lock=PTHREAD_MUTEX_INITIALIZER;
static Programs programs;
static _Atomic int selected;
static _Atomic uint64_t solves,probes,constructions,c_calls,x86_calls;
static _Atomic uint64_t fuel_used,imports,callbacks,output_nodes,output_edges,deferrals,resource_failures;
static _Atomic uint64_t host_services;
static _Atomic bool statistics_enabled;
void cetta_kernel_enable_stats(void) { atomic_store_explicit(&statistics_enabled,true,memory_order_relaxed); }
bool cetta_kernel_stats_enabled(void) { return atomic_load_explicit(&statistics_enabled,memory_order_relaxed); }
#define COUNT(counter,amount) do { if (cetta_kernel_stats_enabled()) atomic_fetch_add(&(counter),(amount)); } while (0)
static _Atomic uint64_t session_identity;
static _Atomic size_t active_sessions;
static pthread_once_t cleanup_once=PTHREAD_ONCE_INIT;
static pthread_once_t batch_once=PTHREAD_ONCE_INIT;
static bool batch_image;
static void batch_check(void) { batch_image=cetta_kernel_batch_image_valid(); }
static void cleanup_register(void) { (void)atexit(cetta_kernel_shutdown); }
const char *cetta_kernel_name(CettaKernelMode mode) {
    static const char *const names[]={"c","x86","hybrid"};
    return (unsigned)mode<3 ? names[mode] : "?";
}
bool cetta_kernel_mode(const char *name,CettaKernelMode *mode) {
    if (!name || !mode) return false;
    for (unsigned i=0;i<3;++i) if (!strcmp(name,cetta_kernel_name((CettaKernelMode)i))) { *mode=(CettaKernelMode)i;return true; }
    return false;
}
CettaKernelMode cetta_kernel_current(void) { return (CettaKernelMode)atomic_load(&selected); }
static bool load(bool acquire,Programs *out,char *error,size_t size) {
    pthread_mutex_lock(&programs_lock);
    Programs *p=&programs;
    if (!p->unifier) p->unifier=nm_unify_program_default(true,error,size);
    if (p->unifier && !p->emitter) p->emitter=nm_emit_program_default(true,error,size);
    if (p->emitter && !p->pair) p->pair=cetta_kernel_pair_program(error,size);
    if (p->pair && !p->observe) p->observe=cetta_kernel_observe_program(error,size);
    if (p->observe && !p->constructor) p->constructor=cetta_kernel_constructor_program(error,size);
    bool ok=p->unifier && p->emitter && p->pair && p->observe && p->constructor;*out=*p;
    if (ok && acquire) atomic_fetch_add(&active_sessions,1);
    pthread_mutex_unlock(&programs_lock);
    pthread_once(&cleanup_once,cleanup_register);return ok;
}
void cetta_kernel_shutdown(void) {
    pthread_mutex_lock(&programs_lock);
    if (atomic_load(&active_sessions)) { pthread_mutex_unlock(&programs_lock);return; }
    nm_unify_program_close(programs.unifier);nm_emit_program_close(programs.emitter);
    cetta_kernel_pair_program_free(programs.pair);cetta_kernel_observe_program_free(programs.observe);
    cetta_kernel_constructor_program_free(programs.constructor);programs=(Programs){0};
    pthread_mutex_unlock(&programs_lock);
}
bool cetta_kernel_select(CettaKernelMode mode,char *error,size_t size) {
    if ((unsigned)mode>2) return false;
    if (mode==CETTA_KERNEL_X86) {
#if !defined(__x86_64__)
        if (error && size) snprintf(error,size,"x86 kernel is not executable on this architecture");
        return false;
#endif
        Programs p;if (!load(false,&p,error,size)) return false;
        pthread_once(&batch_once,batch_check);
        if (!batch_image) {
            if (error && size) snprintf(error,size,"native batch image differs from its expected bytes");
            return false;
        }
    }
    pthread_mutex_lock(&programs_lock);
    bool active=atomic_load(&active_sessions)!=0;
    if (!active || cetta_kernel_current()==mode) atomic_store(&selected,(int)mode);
    pthread_mutex_unlock(&programs_lock);
    if (active && cetta_kernel_current()!=mode) {
        if (error && size) snprintf(error,size,"kernel selection cannot change during an active session");
        return false;
    }
    return true;
}
void cetta_kernel_note_c(void) { COUNT(c_calls,1); }
void cetta_kernel_note_host(void) { COUNT(host_services,1); }
CettaKernelStats cetta_kernel_stats(void) {
    return (CettaKernelStats){atomic_load(&solves),atomic_load(&probes),atomic_load(&constructions),
        atomic_load(&c_calls),atomic_load(&x86_calls),atomic_load(&fuel_used),
        atomic_load(&imports),atomic_load(&callbacks),atomic_load(&output_nodes),atomic_load(&output_edges),
        atomic_load(&deferrals),atomic_load(&resource_failures),atomic_load(&host_services)};
}
void cetta_kernel_reset_stats(void) {
    atomic_store(&solves,0);atomic_store(&probes,0);atomic_store(&constructions,0);atomic_store(&c_calls,0);
    atomic_store(&x86_calls,0);atomic_store(&fuel_used,0);atomic_store(&imports,0);
    atomic_store(&callbacks,0);atomic_store(&output_nodes,0);atomic_store(&output_edges,0);
    atomic_store(&deferrals,0);atomic_store(&resource_failures,0);
    atomic_store(&host_services,0);
}
void cetta_kernel_print_stats(void) {
    CettaKernelStats s=cetta_kernel_stats();
    fprintf(stderr,"kernel: requested=%s solves=%llu probes=%llu constructions=%llu c=%llu x86=%llu fuel=%llu imports=%llu callbacks=%llu output_nodes=%llu output_edges=%llu deferred=%llu resource=%llu host_services=%llu\n",
        cetta_kernel_name(cetta_kernel_current()),(unsigned long long)s.solves,(unsigned long long)s.probes,
        (unsigned long long)s.constructions,(unsigned long long)s.c_calls,
        (unsigned long long)s.x86_calls,(unsigned long long)s.fuel_used,(unsigned long long)s.imports,
        (unsigned long long)s.callbacks,(unsigned long long)s.output_nodes,(unsigned long long)s.output_edges,
        (unsigned long long)s.deferrals,(unsigned long long)s.resource_failures,(unsigned long long)s.host_services);
}
static bool valid(const CettaKernelSession *s) { return !s->view.valid || s->view.valid(s->view.pin); }
static CettaKernelStatus view_status(NmViewStatus status) {
    switch(status) {
    case NM_VIEW_RESOURCE:return CETTA_KERNEL_RESOURCE;
    case NM_VIEW_DEFER:return CETTA_KERNEL_DEFER;
    case NM_VIEW_INTERRUPTED:return CETTA_KERNEL_INTERRUPTED;
    default:return CETTA_KERNEL_FAULT;
    }
}
static void note(CettaKernelStatus status) {
    if (status==CETTA_KERNEL_DEFER) COUNT(deferrals,1);
    if (status==CETTA_KERNEL_RESOURCE) COUNT(resource_failures,1);
}
CettaKernelStatus cetta_kernel_session_open(CettaKernelMode mode,NmQueryView view,
    CettaKernelLimits limits,uint64_t used,CettaKernelSession **out,char *error,size_t size) {
    if (out) *out=NULL;
    if (!out || !view.terms.inspect || (unsigned)mode>2 || used>limits.fuel_budget ||
        (limits.memo && (limits.memo&(limits.memo-1)))) return CETTA_KERNEL_FAULT;
    if (view.valid && !view.valid(view.pin)) return CETTA_KERNEL_INVALIDATED;
    CettaKernelSession *s=calloc(1,sizeof *s);if (!s) return CETTA_KERNEL_RESOURCE;
    uint64_t identity=atomic_load(&session_identity);
    while (identity!=UINT64_MAX && !atomic_compare_exchange_weak(&session_identity,&identity,identity+1)) {}
    if (identity==UINT64_MAX) { free(s);return CETTA_KERNEL_RESOURCE; }
    ++identity;
    Programs p={0};
    if (mode==CETTA_KERNEL_C || mode==CETTA_KERNEL_HYBRID) {
        pthread_mutex_lock(&programs_lock);atomic_fetch_add(&active_sessions,1);pthread_mutex_unlock(&programs_lock);
    } else if (!load(true,&p,error,size)) { free(s);return CETTA_KERNEL_RESOURCE; }
    *s=(CettaKernelSession){mode,view,limits,NULL,p.unifier,p.emitter,p.constructor,used,identity};
    *out=s;return CETTA_KERNEL_SOLVED;
}
void cetta_kernel_session_free(CettaKernelSession *s) {
    if (s) { nm_solution_free(s->solution);free(s);atomic_fetch_sub(&active_sessions,1); }
}
CettaKernelCheckpoint *cetta_kernel_checkpoint(CettaKernelSession *s) {
    if (!s || !valid(s)) return NULL;
    CettaKernelCheckpoint *m=malloc(sizeof *m);if (!m) return NULL;
    if (s->solution && !nm_solution_retain(s->solution)) { free(m);return NULL; }
    *m=(CettaKernelCheckpoint){s->identity,s->solution};return m;
}
bool cetta_kernel_restore(CettaKernelSession *s,const CettaKernelCheckpoint *m) {
    if (!s || !m || m->session_identity!=s->identity || !valid(s) || (m->solution && !nm_solution_retain(m->solution))) return false;
    nm_solution_free(s->solution);s->solution=m->solution;return true;
}
void cetta_kernel_checkpoint_free(CettaKernelCheckpoint *m) { if (m) { nm_solution_free(m->solution);free(m); } }
const NmSolution *cetta_kernel_solution(const CettaKernelSession *s) { return s && valid(s) ? s->solution : NULL; }
bool cetta_kernel_visit_delta(const CettaKernelSession *s,bool (*visit)(void *,NmVariableKey,NmHostValue),void *context) {
    return s && valid(s) && s->solution && nm_solution_visit_changes(s->solution,visit,context);
}
static NmTermView current(const CettaKernelSession *s) { return s->solution ? nm_solution_view(s->solution) : s->view.terms; }
static void *allocate(size_t count,size_t width) { return calloc(count ? count : 1,width); }
CettaKernelStatus cetta_kernel_solve(CettaKernelSession *s,const NmHostConstraint *cs,size_t count,bool probe,
    CettaKernelReceipt *receipt,char *error,size_t size) {
    CettaKernelReceipt r={.status=CETTA_KERNEL_FAULT};NmPreparedMatch *g=NULL;NmUnifyWork w={0};
    if (!s || (count && !cs)) goto done;
    r.fuel_used=s->fuel;
    if (!valid(s)) { r.status=CETTA_KERNEL_INVALIDATED;goto done; }
    if (s->fuel==s->limits.fuel_budget) { r.status=CETTA_KERNEL_EXHAUSTED;goto done; }
    ++s->fuel;COUNT(fuel_used,1);
    if (probe) COUNT(probes,1);else COUNT(solves,1);
    NmTermView v=current(s);NmImportStats stats;
    NmViewStatus status=nm_match_prepare_constraints(&v,cs,count,s->limits.import,&g,&stats);
    COUNT(imports,1);COUNT(callbacks,stats.callbacks);
    if (status!=NM_VIEW_READY) { r.status=view_status(status);goto done; }
    if (!valid(s)) { r.status=CETTA_KERNEL_INVALIDATED;goto done; }
    const NmUnifyInput *in=nm_match_input(g);
    size_t memo=s->limits.memo;
    if (!memo) {
        memo=16;
        while (memo<in->nodes_count) {
            if (memo>SIZE_MAX/2) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
            memo*=2;
        }
    }
    if (in->nodes_count>SIZE_MAX-in->variables_count || in->nodes_count+in->variables_count>SIZE_MAX-memo ||
        in->children_count>SIZE_MAX/8-8 || in->nodes_count>SIZE_MAX/8-8) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    w=(NmUnifyWork){.bindings_words=in->variables_count,
        .pairs_count=s->limits.pairs ? s->limits.pairs : 64+8*in->children_count,
        .scan_words=s->limits.scan ? s->limits.scan : 64+8*in->nodes_count,
        .state_words=s->unifier ? nm_unify_state_words(s->unifier) : 1,.marks_words=in->nodes_count+in->variables_count+memo};
    w.bindings=allocate(w.bindings_words,sizeof *w.bindings);w.pairs=allocate(w.pairs_count,sizeof *w.pairs);
    w.scan=allocate(w.scan_words,sizeof *w.scan);w.state=allocate(w.state_words,sizeof *w.state);w.marks=allocate(w.marks_words,sizeof *w.marks);
    if (!w.bindings || !w.pairs || !w.scan || !w.state || !w.marks) { r.status=CETTA_KERNEL_RESOURCE;goto done; }
    NmCallStatus called=NM_CALL_OK;
    CettaKernelStatus c_status=CETTA_KERNEL_SOLVED;
    if (s->mode==CETTA_KERNEL_C || s->mode==CETTA_KERNEL_HYBRID) {
        COUNT(c_calls,1);
        c_status=cetta_kernel_c_solve(in,&w,s->limits.fuel_budget,s->fuel,&r.core);
    } else {
        COUNT(x86_calls,1);
        called=nm_unify_call(s->unifier,NM_NATIVE,in,&w,s->limits.fuel_budget,s->fuel,&r.core,error,size);
        if (called!=NM_CALL_OK) { r.status=called==NM_CALL_RESOURCE ? CETTA_KERNEL_RESOURCE : CETTA_KERNEL_FAULT;goto done; }
    }
    r.has_core=true;COUNT(fuel_used,r.core.fuel_used-s->fuel);s->fuel=r.core.fuel_used;
    if (c_status!=CETTA_KERNEL_SOLVED && c_status!=CETTA_KERNEL_REFUTED) { r.status=c_status;goto done; }
    if (!valid(s)) { r.status=CETTA_KERNEL_INVALIDATED;goto done; }
    r.truth=nm_constraint_truth(called,&r.core);
    if (r.core.outcome==NM_NO_MATCH) { r.status=CETTA_KERNEL_REFUTED;goto done; }
    if (r.core.outcome==NM_EXHAUSTED) { r.status=CETTA_KERNEL_EXHAUSTED;goto done; }
    if (r.core.outcome==NM_UNSUPPORTED) {
        r.status=r.core.payload>=6 ? CETTA_KERNEL_RESOURCE : CETTA_KERNEL_DEFER;goto done;
    }
    if (r.core.outcome!=NM_UNIFIED) goto done;
    if (!probe) {
        NmSolution *next=NULL;status=nm_solution_capture(g,&r.core,&w,&next);
        if (status!=NM_VIEW_READY) { r.status=view_status(status);goto done; }
        nm_solution_free(s->solution);s->solution=next;
    }
    r.status=CETTA_KERNEL_SOLVED;
done:
    if (s) r.fuel_used=s->fuel;
    free(w.bindings);free(w.pairs);free(w.scan);free(w.state);free(w.marks);nm_match_prepared_free(g);note(r.status);
    if (receipt) *receipt=r;
    return r.status;
}
const NmOutputGraph *cetta_kernel_output_graph(const CettaKernelOutput *o) {
    if (!o || (o->valid && !o->valid(o->pin))) return NULL;
    return o->native ? nm_output_graph(o->native) : &o->graph;
}
bool cetta_kernel_output_origin(const CettaKernelOutput *o,uint64_t node,NmHostValue *origin) {
    if (!cetta_kernel_output_graph(o)) return false;
    if (o->origins) {
        if (!origin || node>=o->graph.nodes_count || !o->origin_present[node]) return false;
        *origin=o->origins[node];return true;
    }
    return o->native ? nm_output_origin(o->native,node,origin) : node<o->graph.nodes_count &&
        nm_match_origin(o->prepared,o->nodes[node].source,origin);
}
void cetta_kernel_output_free(CettaKernelOutput *o) {
    if (o) { nm_output_free(o->native);nm_match_prepared_free(o->prepared);free(o->nodes);free(o->children);free(o->origins);free(o->origin_present);free(o); }
}
CettaKernelStatus cetta_kernel_instantiate(CettaKernelSession *s,const NmHostValue *roots,size_t count,
    CettaKernelOutput **out,NmOutputReceipt *receipt,char *error,size_t size) {
    if (out) *out=NULL;
    if (!s || !out) return CETTA_KERNEL_FAULT;
    if (!valid(s)) return CETTA_KERNEL_INVALIDATED;
    NmOutputLimits limits={s->limits.import,s->limits.output_nodes,s->limits.output_edges,s->limits.output_stack};
    NmQueryView view={current(s),s->view.pin,s->view.valid};NmOutputReceipt r={.fuel_used=s->fuel};
    CettaKernelStatus result;
    if (s->mode==CETTA_KERNEL_C || s->mode==CETTA_KERNEL_HYBRID) {
        result=cetta_kernel_c_output(view,roots,count,s->limits,s->limits.fuel_budget,s->fuel,out,&r);
        COUNT(c_calls,1);
    } else {
        CettaKernelOutput *o=calloc(1,sizeof *o);if (!o) return CETTA_KERNEL_RESOURCE;
        NmOutputStatus status=nm_output_build(s->emitter,NM_NATIVE,
            view,roots,count,limits,s->limits.fuel_budget,s->fuel,&o->native,&r,error,size);
        if (status==NM_OUTPUT_READY) *out=o;else free(o);
        if (r.has_core) COUNT(x86_calls,1);
        switch(status) {
        case NM_OUTPUT_READY:result=CETTA_KERNEL_SOLVED;break;
        case NM_OUTPUT_EXHAUSTED:result=CETTA_KERNEL_EXHAUSTED;break;
        case NM_OUTPUT_RESOURCE:result=CETTA_KERNEL_RESOURCE;break;
        case NM_OUTPUT_DEFER:result=CETTA_KERNEL_DEFER;break;
        case NM_OUTPUT_INVALIDATED:result=CETTA_KERNEL_INVALIDATED;break;
        case NM_OUTPUT_INTERRUPTED:result=CETTA_KERNEL_INTERRUPTED;break;
        default:result=CETTA_KERNEL_FAULT;break;
        }
    }
    COUNT(constructions,1);COUNT(imports,1);COUNT(callbacks,r.import.callbacks);
    COUNT(fuel_used,r.fuel_used-s->fuel);s->fuel=r.fuel_used;
    COUNT(output_nodes,r.core.nodes_written);COUNT(output_edges,r.core.children_written);
    if (receipt) *receipt=r;
    note(result);return result;
}

struct CettaKernelPairScope { CettaKernelPairProgram *program;uint64_t fuel; };
CettaKernelPairScope *cetta_kernel_pair_begin(CettaKernelMode mode) {
    if (mode==CETTA_KERNEL_C || (unsigned)mode>2) return NULL;
    CettaKernelPairScope *s=calloc(1,sizeof *s);if (!s) return NULL;
    Programs p;char error[256];
    if (!load(true,&p,error,sizeof error)) { free(s);return NULL; }
    *s=(CettaKernelPairScope){p.pair,0};COUNT(solves,1);return s;
}
bool cetta_kernel_pair_next(CettaKernelPairScope *s,const CettaKernelPairFacts *facts,CettaKernelPairPlan *plan) {
    if (!s) return false;
    COUNT(x86_calls,1);
    if (!cetta_kernel_pair_call(s->program,facts,UINT64_MAX,s->fuel,plan)) return false;
    COUNT(fuel_used,plan->fuel_used-s->fuel);s->fuel=plan->fuel_used;return true;
}
bool cetta_kernel_pair_word(CettaKernelPairScope *s,uint64_t left,uint64_t right,bool *equal) {
    if (!s) return false;
    uint64_t fuel;
    COUNT(x86_calls,1);
    if (!cetta_kernel_pair_word_call(s->program,left,right,UINT64_MAX,s->fuel,equal,&fuel)) return false;
    COUNT(fuel_used,fuel-s->fuel);s->fuel=fuel;return true;
}
void cetta_kernel_pair_end(CettaKernelPairScope *s) {
    if (s) { free(s);atomic_fetch_sub(&active_sessions,1); }
}

struct CettaKernelObserveScope { CettaKernelObserveProgram *program;uint64_t fuel; };
CettaKernelObserveScope *cetta_kernel_observe_begin(CettaKernelMode mode) {
    if (mode==CETTA_KERNEL_C || (unsigned)mode>2) return NULL;
    CettaKernelObserveScope *s=calloc(1,sizeof *s);if (!s) return NULL;
    Programs p;char error[256];
    if (!load(true,&p,error,sizeof error)) { free(s);return NULL; }
    *s=(CettaKernelObserveScope){p.observe,0};COUNT(constructions,1);return s;
}
bool cetta_kernel_observe_next(CettaKernelObserveScope *s,const CettaKernelObserveFacts *facts,uint64_t *action) {
    if (!s) return false;
    uint64_t fuel;
    COUNT(x86_calls,1);
    if (!cetta_kernel_observe_call(s->program,facts,UINT64_MAX,s->fuel,action,&fuel)) return false;
    COUNT(fuel_used,fuel-s->fuel);s->fuel=fuel;return true;
}
void cetta_kernel_observe_end(CettaKernelObserveScope *s) {
    if (s) { free(s);atomic_fetch_sub(&active_sessions,1); }
}

void cetta_kernel_constructor_account(CettaKernelMode mode,bool executed,uint64_t fuel,
        size_t nodes,size_t edges,CettaKernelStatus status) {
    COUNT(fuel_used,fuel);COUNT(output_nodes,nodes);COUNT(output_edges,edges);
    if (executed) {
        COUNT(constructions,1);
        if (mode==CETTA_KERNEL_C || mode==CETTA_KERNEL_HYBRID) COUNT(c_calls,1);
        else COUNT(x86_calls,1);
    }
    note(status);
}

uint64_t cetta_kernel_batch(CettaKernelMode mode,const TcBatchInput *input,TcBatchWork *work) {
    if ((unsigned)mode>CETTA_KERNEL_HYBRID || !input || !work) return TC_BATCH_INVALID;
    if (work->cursor_words<TC_BATCH_CURSOR_WORDS) return TC_BATCH_RESOURCE;
    if (input->plan && input->patterns<=TC_BATCH_PATTERNS && input->plan->makes<=FLAT_MAKES) {
        uint64_t stride=input->patterns+input->plan->makes;if (!stride) stride=1;
        if (work->output_words<stride) return TC_BATCH_RESOURCE;
    }
    if (!tc_batch_valid(input,work)) return TC_BATCH_INVALID;
    if (mode==CETTA_KERNEL_X86) {
        pthread_once(&batch_once,batch_check);
        if (!batch_image) return TC_BATCH_INVALID;
    }
    uint64_t before=work->used,status;
    COUNT(solves,1);
    if (mode==CETTA_KERNEL_X86) { COUNT(x86_calls,1);status=tc_batch_x86(input,work); }
    else { COUNT(c_calls,1);status=tc_batch_c(input,work); }
    COUNT(fuel_used,work->used-before);
    return status;
}
