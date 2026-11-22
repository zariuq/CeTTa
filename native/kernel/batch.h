#ifndef CETTA_KERNEL_BATCH_H
#define CETTA_KERNEL_BATCH_H

/* One declared layout for C and the prelinked SysV entry. */
#define TC_BATCH_INPUT_FIELDS(X) \
 X(0,node,const TcNode *) X(1,nodes,uint64_t) \
 X(2,item,const uint32_t *) X(3,items,uint64_t) \
 X(4,row,const uint32_t *) X(5,rows,uint64_t) \
 X(6,plan,const FlatPlan *) X(7,stage,const uint32_t *) \
 X(8,patterns,uint64_t) X(9,takes,uint64_t) X(10,flags,uint64_t) \
 X(11,quantum,uint64_t) X(12,budget,uint64_t)
#define TC_BATCH_WORK_FIELDS(X) \
 X(0,cursor,uint32_t *) X(1,cursor_words,uint64_t) \
 X(2,output,uint32_t *) X(3,output_words,uint64_t) \
 X(4,produced,uint64_t) X(5,used,uint64_t) \
 X(6,visited,uint64_t) X(7,status,uint64_t)

#define TC_BATCH_END 0
#define TC_BATCH_YIELD 1
#define TC_BATCH_EXHAUSTED 2
#define TC_BATCH_INVALID 3
#define TC_BATCH_RESOURCE 4
#define TC_BATCH_ONE 1
#define TC_BATCH_SYMMETRIC 2
#define TC_BATCH_PATTERNS 24
#define TC_BATCH_CURSOR_WORDS 74
#define TC_BATCH_SELECTED 2
#define TC_BATCH_NEXT 26
#define TC_BATCH_TRIED 50
#define TC_BATCH_STAGE_WORDS 8
#define TC_BATCH_LENGTH 0
#define TC_BATCH_LO0 1
#define TC_BATCH_HI0 2
#define TC_BATCH_LO1 3
#define TC_BATCH_HI1 4
#define TC_BATCH_TWIN 5
#define TC_BATCH_GUARD_BEGIN 6
#define TC_BATCH_GUARD_END 7
#define TC_BP_CONSTANT 0
#define TC_BP_CONSTANTS 128
#define TC_BP_GUARD 132
#define TC_BP_GUARDS 260
#define TC_BP_MAKE 264
#define TC_BP_MAKES 392

#ifdef __ASSEMBLER__
#define TC_BATCH_OFFSET(n,name,type) .equ TBI_##name, 8*n;
TC_BATCH_INPUT_FIELDS(TC_BATCH_OFFSET)
#undef TC_BATCH_OFFSET
#define TC_BATCH_OFFSET(n,name,type) .equ TBW_##name, 8*n;
TC_BATCH_WORK_FIELDS(TC_BATCH_OFFSET)
#undef TC_BATCH_OFFSET
#else
#include "kernel.h"
#include "../tcalc/tcalc_core.h"
#include "../tcalc/tcalc_records.h"
#include "../tcalc/tcalc_flat_layout.h"
#include <stddef.h>

#define TC_BATCH_FIELD(n,name,type) type name;
typedef struct { TC_BATCH_INPUT_FIELDS(TC_BATCH_FIELD) } TcBatchInput;
typedef struct { TC_BATCH_WORK_FIELDS(TC_BATCH_FIELD) } TcBatchWork;
#undef TC_BATCH_FIELD
#define TC_BATCH_OFFSET(n,name,type) \
 _Static_assert(sizeof(type)==8 && offsetof(TcBatchInput,name)==8*(n),"batch input ABI");
TC_BATCH_INPUT_FIELDS(TC_BATCH_OFFSET)
#undef TC_BATCH_OFFSET
#define TC_BATCH_OFFSET(n,name,type) \
 _Static_assert(sizeof(type)==8 && offsetof(TcBatchWork,name)==8*(n),"batch work ABI");
TC_BATCH_WORK_FIELDS(TC_BATCH_OFFSET)
#undef TC_BATCH_OFFSET

/* The source is a pinned canonical ground term pool. Stages are indexed
 * candidate runs, lengths, symmetry predecessors and guard intervals. A
 * cursor contains indices only; renew borrowed pointers after interning.
 * Records contain pattern selections followed by make-field term ids.
 * One unit is charged per candidate proposal and per record activation.
 * END alone certifies a complete scan. No call publishes a store effect. */
uint64_t cetta_kernel_batch(CettaKernelMode mode,const TcBatchInput *input,TcBatchWork *work);
bool cetta_kernel_batch_image_valid(void);
uint64_t tc_batch_c(const TcBatchInput *input,TcBatchWork *work);
uint64_t tc_batch_x86(const TcBatchInput *input,TcBatchWork *work);
bool tc_batch_valid(const TcBatchInput *input,TcBatchWork *work);
#if defined(__GNUC__)
extern uint64_t cetta_kernel_batch(CettaKernelMode,const TcBatchInput *,TcBatchWork *) __attribute__((weak));
#endif
#endif
#endif
