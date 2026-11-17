#ifndef CETTA_KERNEL_TCALC_BRIDGE_H
#define CETTA_KERNEL_TCALC_BRIDGE_H
#include "kernel.h"
#include "../tcalc/tcalc_core.h"

typedef struct TcKernelSolver TcKernelSolver;
typedef NmViewStatus (*TcNativeInspect)(TcTerms *,TcKernelSolver *,NmHostValue,NmViewNode *);
typedef NmViewStatus (*TcNativeChild)(TcTerms *,TcKernelSolver *,NmHostValue,uint64_t,NmHostValue *);
typedef NmViewStatus (*TcNativeBinding)(TcTerms *,TcKernelSolver *,bool,NmHostValue,bool *,NmHostValue *);
typedef bool (*TcNativeApply)(TcTerms *,TcKernelSolver *,NmVariableKey,NmHostValue);
typedef TcTerm (*TcNativeMake)(TcTerms *,const TcTerm *,uint32_t);
typedef struct {
    TcNativeInspect inspect;
    TcNativeChild child;
    TcNativeBinding binding;
    TcNativeApply apply;
    TcNativeMake make;
} CettaTcKernelOps;

/* Synchronous native boundary. Context, callbacks, source syntax and buffers
 * are borrowed for this call only. All sessions and workspaces are released
 * before return. Checked callbacks receive the original typed context. A
 * failed adoption can leave private solver writes for caller rollback. No
 * Space is published and no host commit callback is invoked. */
CettaKernelStatus cetta_kernel_tcalc_solve(TcTerms *,TcKernelSolver *,const CettaTcKernelOps *,
    CettaKernelMode,TcTerm,uint32_t,TcTerm,uint32_t);
CettaKernelStatus cetta_kernel_tcalc_construct(TcTerms *,TcKernelSolver *,const CettaTcKernelOps *,
    CettaKernelMode,const TcTerm *,size_t,uint32_t,bool,TcTerm *);
#if defined(__GNUC__)
extern CettaKernelStatus cetta_kernel_tcalc_solve(TcTerms *,TcKernelSolver *,const CettaTcKernelOps *,
    CettaKernelMode,TcTerm,uint32_t,TcTerm,uint32_t) __attribute__((weak));
extern CettaKernelStatus cetta_kernel_tcalc_construct(TcTerms *,TcKernelSolver *,const CettaTcKernelOps *,
    CettaKernelMode,const TcTerm *,size_t,uint32_t,bool,TcTerm *) __attribute__((weak));
#endif
#endif
