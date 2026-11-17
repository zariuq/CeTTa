/*
 * tisa_x86.h -- t-ISA profile p1 to x86-64 (SysV LP64), by fixed templates.
 *
 * Each statement of an admitted program becomes one template: a fixed byte
 * pattern whose only parameters are register numbers, immediates and jump
 * offsets. A proof of a template covers every use of it. The image starts
 * with the public entry; its spans name the template and statement behind
 * every byte range.
 */
#pragma once

#include "tisa_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t         offset, length;   /* a byte range of the image */
    const char      *template_name;    /* the template these bytes instantiate */
    const TcIsaStmt *stmt;             /* the statement realized; NULL for the wrapper and handlers */
    bool             fast;             /* in a loop's copy without its hoisted checks */
} TcIsaX86Span;

/* What became of one loop: whether its bounds checks were hoisted and, if
 * not, why. */
typedef struct {
    const TcIsaStmt *loop;
    bool             hoisted;
    const char      *reason;          /* why not hoisted; NULL when hoisted */
    uint32_t         hoisted_checks;  /* region accesses the fast copy runs unchecked */
    uint32_t         kept_checks;     /* region accesses it still checks */
    uint64_t         fund_units;      /* FUND units per iteration tested at entry; 0 if none */
    /* What the entry test establishes, for x from 0 to n - 1: each range's
     * pointer base + k*x + c, for every c in [lo, hi], lies in its region
     * (base is a register, or 0xFE for the region's own base). */
    uint32_t         nranges;
    struct { uint8_t region, base; uint32_t k; int64_t lo, hi; } ranges[16];
} TcIsaX86Loop;

typedef struct {
    uint8_t      *bytes;  size_t len;     /* the exact image; the entry is offset 0 */
    TcIsaX86Span *spans;  size_t nspans;
    void         *code;   size_t mapped;  /* an executable copy after tisa_x86_load, else NULL */
    TcIsaX86Loop *loops;  size_t nloops;  /* one record per loop, in emission order */
} TcIsaX86;

/* Emission options. All off is the default emission, which tisa_x86_emit
 * produces and the exported manifests record. */
typedef struct {
    /* Loops (counted and early-exit) whose region accesses are affine in the
     * loop index get one entry test on the extreme addresses, and one on the
     * allowance when the body pays FUND. When the tests pass, a copy of the
     * loop runs without those checks; when not, the unchanged template loop
     * runs. */
    bool hoist_bounds;
} TcIsaX86Options;

/* Emits an admitted program. False, with a reason in err, if it is not. */
bool tisa_x86_emit(const TcIsaProgram *prog, TcIsaX86 *out, char *err, size_t cap);
bool tisa_x86_emit_with(const TcIsaProgram *prog, const TcIsaX86Options *opts, TcIsaX86 *out,
                        char *err, size_t cap);

/* Maps the image executable (written, then made read-only and executable). */
bool tisa_x86_load(TcIsaX86 *x, char *err, size_t cap);

/* Validates the entry (section 6) and runs the loaded image: the native
 * counterpart of tisa_run. */
TcIsaOutcome tisa_x86_run(const TcIsaProgram *prog, const TcIsaX86 *x,
                          TcIsaRuntimeFrame *frame, uint64_t regs[TISA_NUM_VREGS]);

void tisa_x86_free(TcIsaX86 *x);

#ifdef __cplusplus
}
#endif
