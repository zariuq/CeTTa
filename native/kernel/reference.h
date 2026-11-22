#ifndef CETTA_KERNEL_REFERENCE_H
#define CETTA_KERNEL_REFERENCE_H
#include "kernel.h"
/* Independent C algorithms over the common, privately imported graph. */
CettaKernelStatus cetta_kernel_c_solve(const NmUnifyInput *input,NmUnifyWork *work,
    uint64_t budget,uint64_t used,NmUnifyResult *result);
CettaKernelStatus cetta_kernel_c_output(NmQueryView view,const NmHostValue *roots,size_t count,
    CettaKernelLimits limits,uint64_t budget,uint64_t used,CettaKernelOutput **out,NmOutputReceipt *receipt);
struct CettaKernelOutput {
    NmOutput *native;
    NmPreparedMatch *prepared;
    NmEmitNode *nodes;
    uint64_t *children;
    NmOutputGraph graph;
    NmHostValue *origins;
    unsigned char *origin_present;
    void *pin;
    bool (*valid)(void *);
};
#endif
