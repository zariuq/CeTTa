/*
 * tisa_ref.h -- t-ISA profile p1: admission and the reference evaluator.
 *
 * SEMANTICS.txt defines the meaning; tisa_ref.c implements it section by
 * section. Programs use tisa.h's representation, with TISA_CTRL_FUND for
 * cost (section 8).
 */
#pragma once

#include "tisa.h"

#ifndef TISA_DEV_REFERENCE
#define TISA_DEV_REFERENCE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TISA_P1_PROFILE "t-isa-p1"

/* What admission learns about a program. */
typedef struct {
    uint16_t read_regs, written_regs;        /* v0..v11 read and written */
    uint16_t read_regions, written_regions;  /* declared regions accessed */
    uint32_t fund_points;                    /* FUND statements */
    uint64_t max_receipt;                    /* a bound on what one run can pay, exact over
                                                the program's paths, UINT64_MAX when it does
                                                not fit */
    bool max_receipt_fits;                   /* the bound fits in 64 bits; only then does an
                                                allowance of R + max_receipt (as naturals)
                                                certify that the run cannot run out */
} TcIsaAdmission;

/* Section 4. True when the program is admitted; otherwise err names the
 * first reason. out may be NULL. */
bool tisa_admit(const TcIsaProgram *prog, TcIsaAdmission *out, char *err, size_t cap);

/* Section 6. True when frame and the entry registers v0..v5 meet the
 * program's declarations. Every realization checks this before running. */
bool tisa_entry_valid(const TcIsaProgram *prog, const TcIsaRuntimeFrame *frame,
                      const uint64_t regs[TISA_NUM_VREGS]);

/* Development oracle; excluded from production builds. */
#if TISA_DEV_REFERENCE
/* Sections 6 to 8. Runs an admitted program on frame, with regs holding
 * v0..v11 on entry (v0 the frame's address). Writes status, ret_val and
 * fuel_used as section 7 says, and returns the outcome. */
TcIsaOutcome tisa_run(const TcIsaProgram *prog, TcIsaRuntimeFrame *frame,
                      uint64_t regs[TISA_NUM_VREGS]);
#endif

#ifdef __cplusplus
}
#endif
