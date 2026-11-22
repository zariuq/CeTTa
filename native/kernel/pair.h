#ifndef CETTA_KERNEL_PAIR_H
#define CETTA_KERNEL_PAIR_H
#include "kernel.h"
#include "pair_layout.generated.h"

typedef struct { uint64_t word[NP_FACTS]; } CettaKernelPairFacts;
typedef struct { uint64_t action,prefix,rest,from,count,fuel_used; } CettaKernelPairPlan;
typedef struct CettaKernelPairProgram CettaKernelPairProgram;
CettaKernelPairProgram *cetta_kernel_pair_program(char *error,size_t size);
void cetta_kernel_pair_program_free(CettaKernelPairProgram *program);
bool cetta_kernel_pair_call(const CettaKernelPairProgram *program,
    const CettaKernelPairFacts *facts,uint64_t budget,uint64_t used,CettaKernelPairPlan *plan);
bool cetta_kernel_pair_word_call(const CettaKernelPairProgram *program,
    uint64_t left,uint64_t right,uint64_t budget,uint64_t used,bool *equal,uint64_t *fuel);
size_t cetta_kernel_pair_bytes(const CettaKernelPairProgram *program);

/* One private administrative matcher invocation. No source pointer is retained
 * by a pair operation. Its descriptor contains full scoped variable keys. */
typedef struct CettaKernelPairScope CettaKernelPairScope;
CettaKernelPairScope *cetta_kernel_pair_begin(CettaKernelMode mode);
bool cetta_kernel_pair_next(CettaKernelPairScope *scope,const CettaKernelPairFacts *facts,CettaKernelPairPlan *plan);
bool cetta_kernel_pair_word(CettaKernelPairScope *scope,uint64_t left,uint64_t right,bool *equal);
void cetta_kernel_pair_end(CettaKernelPairScope *scope);
#if defined(__GNUC__)
extern CettaKernelPairScope *cetta_kernel_pair_begin(CettaKernelMode) __attribute__((weak));
extern bool cetta_kernel_pair_next(CettaKernelPairScope *,const CettaKernelPairFacts *,CettaKernelPairPlan *) __attribute__((weak));
extern bool cetta_kernel_pair_word(CettaKernelPairScope *,uint64_t,uint64_t,bool *) __attribute__((weak));
extern void cetta_kernel_pair_end(CettaKernelPairScope *) __attribute__((weak));
#endif
#endif
