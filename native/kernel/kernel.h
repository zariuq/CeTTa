#ifndef CETTA_KERNEL_H
#define CETTA_KERNEL_H
#include "../../vendor/tcalc-native/native/matching/native_output.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CETTA_KERNEL_C,CETTA_KERNEL_X86,CETTA_KERNEL_HYBRID
} CettaKernelMode;
typedef enum {
    CETTA_KERNEL_SOLVED,CETTA_KERNEL_REFUTED,CETTA_KERNEL_DEFER,
    CETTA_KERNEL_RESOURCE,CETTA_KERNEL_EXHAUSTED,CETTA_KERNEL_INVALIDATED,
    CETTA_KERNEL_INTERRUPTED,CETTA_KERNEL_FAULT
} CettaKernelStatus;
typedef struct CettaKernelSession CettaKernelSession;
typedef struct CettaKernelCheckpoint CettaKernelCheckpoint;
typedef struct CettaKernelOutput CettaKernelOutput;
typedef struct {
    uint64_t solves,probes,constructions,c_calls,x86_calls;
    uint64_t fuel_used,imports,callbacks,output_nodes,output_edges;
    uint64_t deferrals,resource_failures;
    uint64_t host_services;
} CettaKernelStats;
typedef struct {
    NmImportLimits import;
    size_t pairs,scan,memo,output_nodes,output_edges,output_stack;
    uint64_t fuel_budget;
} CettaKernelLimits;
typedef struct {
    CettaKernelStatus status;
    NmConstraintTruth truth;
    uint64_t fuel_used;
    bool has_core;
    NmUnifyResult core;
} CettaKernelReceipt;

const char *cetta_kernel_name(CettaKernelMode mode);
bool cetta_kernel_mode(const char *name,CettaKernelMode *mode);
/* Process selection precedes worker creation. Explicit session modes remain
 * independent of that default. C remains the default when unconfigured. */
bool cetta_kernel_select(CettaKernelMode mode,char *error,size_t size);
CettaKernelMode cetta_kernel_current(void);
void cetta_kernel_note_c(void);
void cetta_kernel_note_host(void);
CettaKernelStats cetta_kernel_stats(void);
void cetta_kernel_enable_stats(void);
bool cetta_kernel_stats_enabled(void);
void cetta_kernel_reset_stats(void);
void cetta_kernel_shutdown(void);
void cetta_kernel_print_stats(void);

/* A session supplies independent C or linked native
 * operations. It borrows the source owners and pin, retaining private scoped
 * solutions. Source callbacks must remain stable until the session is freed. */
CettaKernelStatus cetta_kernel_session_open(CettaKernelMode mode,NmQueryView view,
    CettaKernelLimits limits,uint64_t fuel_used,CettaKernelSession **out,char *error,size_t size);
void cetta_kernel_session_free(CettaKernelSession *session);
CettaKernelCheckpoint *cetta_kernel_checkpoint(CettaKernelSession *session);
bool cetta_kernel_restore(CettaKernelSession *session,const CettaKernelCheckpoint *checkpoint);
void cetta_kernel_checkpoint_free(CettaKernelCheckpoint *checkpoint);
CettaKernelStatus cetta_kernel_solve(CettaKernelSession *session,
    const NmHostConstraint *constraints,size_t count,bool probe,CettaKernelReceipt *receipt,
    char *error,size_t size);
/* A complete immutable substitution view, borrowed until the next restore or
 * solve. Newly introduced bindings can be visited through the snapshot API. */
const NmSolution *cetta_kernel_solution(const CettaKernelSession *session);
bool cetta_kernel_visit_delta(const CettaKernelSession *session,
    bool (*visit)(void *,NmVariableKey,NmHostValue),void *context);
CettaKernelStatus cetta_kernel_instantiate(CettaKernelSession *session,
    const NmHostValue *templates,size_t count,CettaKernelOutput **out,NmOutputReceipt *receipt,
    char *error,size_t size);

const NmOutputGraph *cetta_kernel_output_graph(const CettaKernelOutput *output);
bool cetta_kernel_output_origin(const CettaKernelOutput *output,uint64_t node,NmHostValue *origin);
void cetta_kernel_output_free(CettaKernelOutput *output);

/* Optional integration points: standalone C clients need not link the native
 * package. A selected non-C mode is admitted only by the linked selector. */
#if defined(__GNUC__)
extern CettaKernelMode cetta_kernel_current(void) __attribute__((weak));
extern void cetta_kernel_note_c(void) __attribute__((weak));
extern void cetta_kernel_note_host(void) __attribute__((weak));
extern bool cetta_kernel_stats_enabled(void) __attribute__((weak));
extern CettaKernelStatus cetta_kernel_session_open(CettaKernelMode,NmQueryView,
    CettaKernelLimits,uint64_t,CettaKernelSession **,char *,size_t) __attribute__((weak));
extern void cetta_kernel_session_free(CettaKernelSession *) __attribute__((weak));
extern CettaKernelStatus cetta_kernel_solve(CettaKernelSession *,const NmHostConstraint *,
    size_t,bool,CettaKernelReceipt *,char *,size_t) __attribute__((weak));
extern const NmSolution *cetta_kernel_solution(const CettaKernelSession *) __attribute__((weak));
extern bool cetta_kernel_visit_delta(const CettaKernelSession *,bool (*)(void *,NmVariableKey,NmHostValue),void *) __attribute__((weak));
extern CettaKernelStatus cetta_kernel_instantiate(CettaKernelSession *,const NmHostValue *,
    size_t,CettaKernelOutput **,NmOutputReceipt *,char *,size_t) __attribute__((weak));
#endif
static inline CettaKernelMode cetta_kernel_requested(void) {
    return cetta_kernel_current ? cetta_kernel_current() : CETTA_KERNEL_C;
}

#ifdef __cplusplus
}
#endif
#endif
