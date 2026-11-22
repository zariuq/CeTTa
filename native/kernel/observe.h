#ifndef CETTA_KERNEL_OBSERVE_H
#define CETTA_KERNEL_OBSERVE_H
#include "kernel.h"
#include "observe_layout.generated.h"
typedef struct { uint64_t word[NO_FACTS]; } CettaKernelObserveFacts;
typedef struct CettaKernelObserveProgram CettaKernelObserveProgram;
CettaKernelObserveProgram *cetta_kernel_observe_program(char *error,size_t size);
void cetta_kernel_observe_program_free(CettaKernelObserveProgram *program);
bool cetta_kernel_observe_call(const CettaKernelObserveProgram *program,
    const CettaKernelObserveFacts *facts,uint64_t budget,uint64_t used,uint64_t *action,uint64_t *fuel);
typedef struct CettaKernelObserveScope CettaKernelObserveScope;
CettaKernelObserveScope *cetta_kernel_observe_begin(CettaKernelMode mode);
bool cetta_kernel_observe_next(CettaKernelObserveScope *scope,const CettaKernelObserveFacts *facts,uint64_t *action);
void cetta_kernel_observe_end(CettaKernelObserveScope *scope);
#if defined(__GNUC__)
extern CettaKernelObserveScope *cetta_kernel_observe_begin(CettaKernelMode) __attribute__((weak));
extern bool cetta_kernel_observe_next(CettaKernelObserveScope *,const CettaKernelObserveFacts *,uint64_t *) __attribute__((weak));
extern void cetta_kernel_observe_end(CettaKernelObserveScope *) __attribute__((weak));
#endif
#endif
