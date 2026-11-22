#ifndef CETTA_KERNEL_PRIVATE_H
#define CETTA_KERNEL_PRIVATE_H
#include "kernel.h"
typedef struct CettaKernelConstructorProgram CettaKernelConstructorProgram;
struct CettaKernelSession {
    CettaKernelMode mode;
    NmQueryView view;
    CettaKernelLimits limits;
    NmSolution *solution;
    NmUnifyProgram *unifier;
    NmEmitProgram *emitter;
    CettaKernelConstructorProgram *constructor;
    uint64_t fuel;
    uint64_t identity;
};
void cetta_kernel_constructor_account(CettaKernelMode mode,bool executed,uint64_t fuel,
    size_t nodes,size_t edges,CettaKernelStatus status);
#endif
