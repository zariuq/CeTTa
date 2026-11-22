#ifndef CETTA_KERNEL_TCALC_BRIDGE_H
#define CETTA_KERNEL_TCALC_BRIDGE_H
#include "kernel.h"
#include "tcunify_layout.generated.h"
#include "tcbuild_layout.generated.h"
#include "../tcalc/tcalc_core.h"
#include "../tcalc/tcalc_records.h"

/* tcalc's x86 kernel (tcalc_native.c). Each call borrows the core's term
 * store and solver tables as the regions of one p1 program and returns its
 * answer; it retains no pointer and makes no term. */

/* One unification (tcunify.metta). state holds TU_FACTS words. */
typedef struct {
    const TcNode *node;   uint64_t nodes;
    const uint32_t *item; uint64_t items;
    uint32_t *local;      uint64_t locals;
    uint32_t *unknown;    uint64_t unknowns;
    uint32_t *trail;      uint64_t trail_cap;
    uint32_t *work;       uint64_t work_cap;
    uint32_t *seen;       uint64_t seen_cap;
    uint64_t *state;      uint64_t state_words;
} TcUnifyStore;
/* Its answer (TU_R_*), or TU_R_FAULT when the call itself fails. */
uint64_t cetta_kernel_tcalc_unify(TcUnifyStore *store);

/* The entries that make terms with the assignments applied (tcbuild.metta).
 * state holds TB_FACTS words. */
typedef struct {
    const TcNode *node;     uint64_t nodes;
    const uint32_t *item;   uint64_t items;
    const uint32_t *local;  uint64_t locals;
    const uint32_t *unknown; uint64_t unknowns;
    uint32_t *seen;         uint64_t seen_cap;
    uint32_t *memo;         uint64_t memo_cap;
    uint32_t *out;          uint64_t out_cap;
    uint32_t *stack;        uint64_t stack_cap;
    uint64_t *state;        uint64_t state_words;
} TcBuildStore;
/* Its answer (TB_R_*), or TB_R_FAULT when the call itself fails. A
 * reference with this bit set names an entry (tcbuild.metta). */
uint64_t cetta_kernel_tcalc_build(TcBuildStore *store);
#define TB_ENTRY_BIT (UINT32_C(1) << 31)
#if defined(__GNUC__)
extern uint64_t cetta_kernel_tcalc_unify(TcUnifyStore *store) __attribute__((weak));
extern uint64_t cetta_kernel_tcalc_build(TcBuildStore *store) __attribute__((weak));
#endif
#endif
