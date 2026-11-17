#ifndef CETTA_KERNEL_CONSTRUCTOR_H
#define CETTA_KERNEL_CONSTRUCTOR_H
#include "kernel.h"
typedef struct CettaKernelConstructorProgram CettaKernelConstructorProgram;
typedef struct CettaKernelConstructorPlan CettaKernelConstructorPlan;
typedef struct { uint64_t owner_generation,semantic_policy,lexical_context; } CettaKernelConstructorKey;
typedef struct { NmVariableKey port;NmHostValue value; } CettaKernelConstructorArgument;
typedef struct {
    CettaKernelStatus status;
    uint64_t fuel_used;
    bool activated,executed;
    size_t prepared_nodes,prepared_edges;
    NmOutputReceipt holes;
    NmEmitResult constructor;
} CettaKernelConstructorReceipt;
/* A plan owns its fixed shape and borrows its source owners/pin until release.
 * Its ports have full formal identities. Every execution supplies each port
 * explicitly; fresh locals and captured values retain the supplied contexts.
 * No binding-dependent result is cached across executions. */
CettaKernelStatus cetta_kernel_constructor_compile(CettaKernelSession *session,
    const NmHostValue *templates,size_t count,CettaKernelConstructorKey key,
    CettaKernelConstructorPlan **out,char *error,size_t size);
void cetta_kernel_constructor_free(CettaKernelConstructorPlan *plan);
size_t cetta_kernel_constructor_ports(const CettaKernelConstructorPlan *plan);
bool cetta_kernel_constructor_port(const CettaKernelConstructorPlan *plan,size_t at,
    NmVariableKey *key,NmHostValue *source);
CettaKernelStatus cetta_kernel_constructor_instantiate(CettaKernelSession *session,
    const CettaKernelConstructorPlan *plan,CettaKernelConstructorKey key,
    const CettaKernelConstructorArgument *arguments,size_t count,CettaKernelOutput **out,
    CettaKernelConstructorReceipt *receipt,char *error,size_t size);
/* Internal component lifecycle, shared with the session image registry. */
CettaKernelConstructorProgram *cetta_kernel_constructor_program(char *error,size_t size);
void cetta_kernel_constructor_program_free(CettaKernelConstructorProgram *program);
#endif
