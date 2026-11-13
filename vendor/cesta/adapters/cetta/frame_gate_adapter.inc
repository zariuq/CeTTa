typedef struct { unsigned collector; } CestaFrameGcAudit;

static bool cesta_frame_null_reference(CestaFrameGcAudit *audit, const void *reference) {
    (void)audit;
    return reference == NULL;
}
static bool cesta_frame_empty_advice(CestaFrameGcAudit *audit,
                                    const PrimeNativeReturnAdvice *advice, size_t count) {
    (void)audit;
    (void)advice;
    return count == 0;
}
static bool cesta_frame_pending_advice(CestaFrameGcAudit *audit,
                                      const PrimeNativeReturnAdvice *advice) {
    (void)audit;
    (void)advice;
    return false;
}
static inline bool cesta_frame_gc_gate(CestaFrameGcAudit *, const PrimeEvalStackFrame *);
#ifndef CESTA_RECORD_DECLARE_ONLY
#if CETTA_PRIME_NEED_HEAP_INDEX
#include "generated/cesta_frame_gc_gate_heap.h"
#else
#include "generated/cesta_frame_gc_gate_base.h"
#endif
#endif
