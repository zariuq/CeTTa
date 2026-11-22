/*
 * tisa.h — Transform-Calculus-Native Structured Abstract ISA (`t-ISA`)
 *
 * Semantic authority (per shared contract 2026-11-10-tisa-joint-contract.md):
 *   t-ISA is the admitted, compilable profile of transform-calculus rules over
 *   canonical machine resources (registers, byte-addressed region capabilities,
 *   and structured continuation tokens). It adds only the minimal typed scalar,
 *   capability/layout admission, and native byte-lowering interface on top of
 *   tcalc's TAKE / KEEP / MAKE and guard mechanisms.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "tc_theory_inspect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TISA_SCHEMA_VERSION  1u
#define TISA_PROFILE_VERSION "t-isa-p0-v1"

/* =========================================================================
 * 1. Scalar Types, Registers, and Outcomes
 * ========================================================================= */

typedef enum {
    TISA_TYPE_UNINIT = 0,
    TISA_TYPE_U64    = 1, /* 64-bit modular unsigned word */
    TISA_TYPE_U32Z   = 2, /* 32-bit modular word, canonical zero-extended to 64 bits */
    TISA_TYPE_I32S   = 3, /* 32-bit modular word, canonical sign-extended to 64 bits */
    TISA_TYPE_BOOL   = 4, /* Canonical boolean in {0, 1} */
    TISA_TYPE_PTR    = 5  /* Derived pointer tied to a declared region capability */
} TcIsaValType;

typedef enum {
    TISA_V0  = 0,  /* x86-64: rdi | AArch64: x0  | RV64: a0 (frame ptr on entry) */
    TISA_V1  = 1,  /* x86-64: rsi | AArch64: x1  | RV64: a1 (hot arg 0) */
    TISA_V2  = 2,  /* x86-64: rdx | AArch64: x2  | RV64: a2 (hot arg 1) */
    TISA_V3  = 3,  /* x86-64: rcx | AArch64: x3  | RV64: a3 (hot arg 2) */
    TISA_V4  = 4,  /* x86-64: r8  | AArch64: x4  | RV64: a4 (hot arg 3) */
    TISA_V5  = 5,  /* x86-64: r9  | AArch64: x5  | RV64: a5 (hot arg 4) */
    TISA_V6  = 6,  /* x86-64: r10 | AArch64: x6  | RV64: a6 */
    TISA_V7  = 7,  /* x86-64: r11 | AArch64: x7  | RV64: a7 */
    TISA_V8  = 8,  /* x86-64: rax | AArch64: x8  | RV64: t0 */
    TISA_V9  = 9,  /* x86-64: rbx | AArch64: x9  | RV64: t1 */
    TISA_V10 = 10, /* x86-64: r12 | AArch64: x10 | RV64: t2 */
    TISA_V11 = 11, /* x86-64: r13 | AArch64: x11 | RV64: t3 */
    TISA_NUM_VREGS = 12,
    /* Emitter-reserved macro scratch registers (never user-allocated): */
    TISA_S0  = 12, /* x86-64: r14 | AArch64: x12 | RV64: t4 */
    TISA_S1  = 13, /* x86-64: r15 | AArch64: x13 | RV64: t5 */
    TISA_TOTAL_REGS = 14
} TcIsaReg;

typedef enum {
    TISA_OUTCOME_REFUSED       = 0, /* Semantic guard / premise refusal (matches ORDINARY_FLAT_REFUSED = 0) */
    TISA_OUTCOME_FIRED         = 1, /* Rule / leaf succeeded (matches ORDINARY_FLAT_FIRED = 1) */
    TISA_OUTCOME_UNSUPPORTED   = 2, /* Declined compilation / out-of-profile capacity (matches ORDINARY_FLAT_UNSUPPORTED = 2) */
    TISA_OUTCOME_INVALID_INPUT = 3, /* Invalid machine input: null/overlapping RW capability, out-of-bounds, bad bool */
    TISA_OUTCOME_EXHAUSTED     = 4  /* Exhausted fuel / loop allowance */
} TcIsaOutcome;

/* =========================================================================
 * 2. Region Slice Capabilities and Runtime Frame ABI Layout
 * ========================================================================= */

#define TISA_MAX_REGIONS  9u
#define TISA_REGION_FRAME 0xFFu /* Validated TcIsaRuntimeFrame field read from v0 */

typedef enum {
    TISA_CAP_NONE = 0,
    TISA_CAP_RO   = 1, /* Borrowed read-only byte range (KEEP resource) */
    TISA_CAP_RW   = 2  /* Owned read-write byte range (TAKE -> MAKE resource; must be disjoint from all other live ranges) */
} TcIsaCapMode;

typedef enum {
    TISA_MEM_U8  = 1,
    TISA_MEM_U32 = 4,
    TISA_MEM_U64 = 8
} TcIsaMemWidth;

typedef struct {
    uint8_t      region_id;
    TcIsaCapMode mode;
    uint32_t     min_byte_len;
    uint32_t     align_bytes;
    const char  *name;
} TcIsaRegionDecl;

/*
 * Canonical Runtime Frame passed in v0 (rdi / x0 / a0).
 * Every field is a 64-bit word at a fixed 8-byte multiple offset so layout is
 * identical across x86-64, AArch64, and RV64 LP64 targets.
 */
typedef struct __attribute__((aligned(64))) TcIsaRuntimeFrame {
    uint64_t s0_ptr;      /* offset   0: region 0 base address */
    uint64_t s0_len;      /* offset   8: region 0 byte length */
    uint64_t s1_ptr;      /* offset  16: region 1 base address */
    uint64_t s1_len;      /* offset  24: region 1 byte length */
    uint64_t s2_ptr;      /* offset  32: region 2 base address */
    uint64_t s2_len;      /* offset  40: region 2 byte length */
    uint64_t s3_ptr;      /* offset  48: region 3 base address */
    uint64_t s3_len;      /* offset  56: region 3 byte length */
    uint64_t s4_ptr;      /* offset  64: region 4 base address */
    uint64_t s4_len;      /* offset  72: region 4 byte length */
    uint64_t s5_ptr;      /* offset  80: region 5 base address */
    uint64_t s5_len;      /* offset  88: region 5 byte length */
    uint64_t s6_ptr;      /* offset  96: region 6 base address */
    uint64_t s6_len;      /* offset 104: region 6 byte length */
    uint64_t s7_ptr;      /* offset 112: region 7 base address */
    uint64_t s7_len;      /* offset 120: region 7 byte length */
    uint64_t s8_ptr;      /* offset 128: region 8 base address */
    uint64_t s8_len;      /* offset 136: region 8 byte length */
    uint64_t status;      /* offset 144: TcIsaOutcome */
    uint64_t ret_val;     /* offset 152: primary return value */
    uint64_t fuel_budget; /* offset 160: execution step/loop allowance */
    uint64_t fuel_used;   /* offset 168: steps/iterations consumed */
    uint64_t arg0;        /* offset 176: auxiliary scalar parameter 0 */
    uint64_t arg1;        /* offset 184: auxiliary scalar parameter 1 */
} TcIsaRuntimeFrame;

_Static_assert(sizeof(TcIsaRuntimeFrame) == 192, "TcIsaRuntimeFrame must be 192 bytes");
_Static_assert(offsetof(TcIsaRuntimeFrame, s0_ptr) == 0, "s0_ptr offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, s7_ptr) == 112, "s7_ptr offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, s8_ptr) == 128, "s8_ptr offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, status) == 144, "status offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, ret_val) == 152, "ret_val offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, fuel_budget) == 160, "fuel_budget offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, fuel_used) == 168, "fuel_used offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, arg0) == 176, "arg0 offset");
_Static_assert(offsetof(TcIsaRuntimeFrame, arg1) == 184, "arg1 offset");

/* Public leaf entry signature: v0=frame, v1..v5=hot scalar/pointer args */
typedef uint64_t (*TcIsaNativeFn)(TcIsaRuntimeFrame *frame,
                                  uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4,
                                  uint64_t a5);

/* =========================================================================
 * 3. Transform-Calculus-Native Instruction Vocabulary
 * ========================================================================= */

typedef enum {
    TISA_BIN_ADD    = 0,
    TISA_BIN_SUB    = 1,
    TISA_BIN_AND    = 2,
    TISA_BIN_OR     = 3,
    TISA_BIN_XOR    = 4,
    TISA_BIN_MULLO  = 5
} TcIsaBinOp;

typedef enum {
    TISA_SHIFT_SHL  = 0,
    TISA_SHIFT_SHRU = 1,
    TISA_SHIFT_SHRS = 2
} TcIsaShiftOp;

typedef enum {
    TISA_CMP_EQ  = 0,
    TISA_CMP_NE  = 1,
    TISA_CMP_ULT = 2,
    TISA_CMP_ULE = 3,
    TISA_CMP_SLT = 4,
    TISA_CMP_SLE = 5
} TcIsaCmpCond;

typedef enum {
    TISA_OP_IMM     = 0, /* TAKE Reg(dst, old) -> MAKE Reg(dst, ty, imm64) */
    TISA_OP_MOV     = 1, /* TAKE Reg(dst, old), KEEP Reg({src}\{dst}) -> MAKE Reg(dst, ty) */
    TISA_OP_BIN     = 2, /* TAKE Reg(dst, old), KEEP Reg({s1,s2}\{dst}) -> MAKE Reg(dst, ty, binop(s1,s2)) */
    TISA_OP_SHIFT   = 3, /* TAKE Reg(dst, old), KEEP Reg({src}\{dst}) -> MAKE Reg(dst, ty, shift(src, imm)) */
    TISA_OP_EXTRACT = 4, /* TAKE Reg(dst, old), KEEP Reg({src}\{dst}) -> MAKE Reg(dst, ty, extract(src, lsb, width)) */
    TISA_OP_ADDR    = 5, /* TAKE Reg(dst, old), KEEP Cap(region), Reg({base,idx}\{dst}) -> MAKE Reg(dst, PTR(region)) */
    TISA_OP_LOAD    = 6, /* TAKE Reg(dst, old), KEEP Cap(region), Bytes(region, off..off+w), Reg({base}\{dst}) -> MAKE Reg(dst, ty) */
    TISA_OP_STORE   = 7, /* KEEP Cap(region, RW), Reg({base,src}), TAKE Bytes(region, off..off+w, old) -> MAKE Bytes(new) */
    TISA_OP_CMP     = 8, /* TAKE Reg(dst, old), KEEP Reg({s1,s2}\{dst}) -> MAKE Reg(dst, BOOL, cmp(s1,s2)) */
    TISA_OP_SELECT  = 9  /* TAKE Reg(dst, old), KEEP Reg({cond,vtrue,vfalse}\{dst}) -> MAKE Reg(dst, ty) */
} TcIsaOpcode;

typedef struct {
    TcIsaOpcode   op;
    TcIsaValType  ty;       /* Result type (or value type for STORE / CMP) */
    uint8_t       dst;      /* Destination register (unused for STORE) */
    uint8_t       src1;     /* Source 1 register (or base pointer register for ADDR/LOAD/STORE) */
    uint8_t       src2;     /* Source 2 register (or index for ADDR, value for STORE, false-val for SELECT) */
    uint8_t       src3;     /* Source 3 register (condition register for SELECT) */
    uint8_t       subop;    /* TcIsaBinOp / TcIsaShiftOp / TcIsaCmpCond / TcIsaMemWidth */
    uint8_t       region_id;/* Region capability ID for ADDR / LOAD / STORE */
    uint8_t       scale_lg2;/* 0, 1, 2, 3 (scale 1, 2, 4, 8 for ADDR) or lsb for EXTRACT */
    uint8_t       bit_width;/* Bit width (1..64) for EXTRACT, or shift amount for SHIFT, or has_imm for CMP */
    int32_t       disp32;   /* Signed byte displacement for ADDR / LOAD / STORE */
    uint64_t      imm64;    /* Immediate constant for IMM / CMP-imm */
} TcIsaInstr;

/* =========================================================================
 * 4. Structured Control & Resource-Aware Transform Profile
 * ========================================================================= */

typedef enum {
    TISA_CTRL_INSTR           = 0, /* Single primitive transform instruction */
    TISA_CTRL_SEQ             = 1, /* Sequential composition (s1 ; s2) */
    TISA_CTRL_IF              = 2, /* Conditional branch on canonical Bool register */
    TISA_CTRL_COUNT_LOOP      = 3, /* Counted loop for idx in 0 .. bound_reg (max_iters static cap) */
    TISA_CTRL_EARLY_EXIT_LOOP = 4, /* Bounded loop exiting early when guard_bool_reg == 0 */
    TISA_CTRL_RETURN          = 5, /* Structured return with status outcome and return register */
    TISA_CTRL_FUND            = 6  /* Pay u units of the program's cost grading (profile p1) */
} TcIsaCtrlKind;

typedef struct TcIsaStmt TcIsaStmt;

struct TcIsaStmt {
    TcIsaCtrlKind kind;
    union {
        TcIsaInstr instr;
        struct {
            const TcIsaStmt *first;
            const TcIsaStmt *second;
        } seq;
        struct {
            uint8_t          cond_reg;   /* Must hold TISA_TYPE_BOOL */
            const TcIsaStmt *then_branch;
            const TcIsaStmt *else_branch;/* May be NULL (empty no-op) */
        } if_cond;
        struct {
            uint8_t          idx_reg;    /* Loop index register (0 .. bound-1), immutable in body */
            uint8_t          bound_reg;  /* Captured loop bound register, immutable in body */
            uint32_t         max_iters;  /* Static upper bound checked at entry */
            const TcIsaStmt *body;
        } count_loop;
        struct {
            uint8_t          idx_reg;    /* Loop index register, immutable in body */
            uint8_t          bound_reg;  /* Captured loop bound register, immutable in body */
            uint8_t          cont_reg;   /* Bool continuation register: exits when cont_reg == 0 */
            uint32_t         max_iters;  /* Static upper bound checked at entry */
            const TcIsaStmt *body;
        } early_exit_loop;
        struct {
            TcIsaOutcome     outcome;    /* TISA_OUTCOME_FIRED / REFUSED / UNSUPPORTED */
            uint8_t          ret_reg;    /* Register holding return value */
        } ret;
        struct {
            uint32_t         units;      /* 1 .. 2^32-1 units of the program's cost grading */
        } fund;
    } u;
};

/* Explicit TAKE / KEEP / MAKE resource footprint summary for a t-ISA program/stmt */
typedef struct {
    uint16_t keep_reg_mask;     /* Bitmask of logical registers v0..v11 read */
    uint16_t write_reg_mask;    /* Bitmask of logical registers v0..v11 written (TAKE -> MAKE) */
    uint16_t scratch_reg_mask;  /* Bitmask of emitter scratch registers s0..s1 used */
    uint16_t keep_region_mask;  /* Bitmask of region capabilities read */
    uint16_t write_region_mask; /* Bitmask of region capabilities written */
    uint32_t max_loop_bound;    /* Sum/product static loop bound */
    bool     has_return;        /* True iff control flow returns explicitly */
    bool     no_spill_verified; /* True iff register pressure <= 12 and 0 stack/call instructions */
} TcIsaFootprint;

typedef struct {
    const char      *name;
    uint32_t         prog_id;
    uint8_t          nregions;
    TcIsaRegionDecl  regions[TISA_MAX_REGIONS];
    uint16_t         init_reg_mask;             /* Registers initialized on entry */
    TcIsaValType     init_reg_types[TISA_NUM_VREGS];
    uint8_t          init_reg_region[TISA_NUM_VREGS]; /* For TISA_TYPE_PTR */
    const TcIsaStmt *root;
    TcIsaFootprint   footprint;
} TcIsaProgram;

/* =========================================================================
 * 5. Portable Core API: Admission, Reference Evaluator & Real tcalc Host
 * ========================================================================= */

/* Static well-formedness, type, loop-bound immutability & TAKE/KEEP/MAKE footprint checker */
bool tisa_check_program(TcIsaProgram *prog, char *err_buf, size_t err_cap);

/* Overflow-safe capability bounds, alignment & RW-exclusivity frame validator */
TcIsaOutcome tisa_validate_frame(const TcIsaProgram *prog,
                                 const TcIsaRuntimeFrame *frame,
                                 const uint64_t init_regs[TISA_NUM_VREGS],
                                 char *err_buf, size_t err_cap);

/* Pure scalar operation evaluator (matches Mettapedia.Machines.TIsa.Scalar.evaluate) */
bool tisa_scalar_eval(const TcIsaInstr *ins,
                      const uint64_t regs[TISA_NUM_VREGS],
                      uint64_t *out_val);

/* Independent structured reference evaluator over TcIsaRuntimeFrame */
TcIsaOutcome tisa_eval_ref(const TcIsaProgram *prog,
                           TcIsaRuntimeFrame *frame,
                           uint64_t regs[TISA_NUM_VREGS]);

/* =========================================================================
 * 5b. Semantic-PC Lowered Recipe Table for Board-Driven Hosted Execution
 * ========================================================================= */

#define TISA_MAX_RECIPE_POINTS 1024u
#define TISA_POINT_HALT_TAG    ((uint64_t)1u << 60)
#define TISA_POINT_HALT(oc)    ((uint64_t)(TISA_POINT_HALT_TAG | (uint64_t)(oc)))
#define TISA_POINT_IS_HALT(pc) ((((uint64_t)(pc) & ~(uint64_t)0xFFu) == TISA_POINT_HALT_TAG) && \
                                (((uint64_t)(pc) & (uint64_t)0xFFu) <= (uint64_t)TISA_OUTCOME_EXHAUSTED))
#define TISA_POINT_HALT_OUTCOME(pc) ((TcIsaOutcome)((uint64_t)(pc) & 0xFFu))

typedef enum {
    TISA_RECIPE_FUEL            = 0, /* TAKE CTRL+FRAME(fuel_used), KEEP FRAME(fuel_budget) -> MAKE CTRL(next)+FRAME(fuel_used+1) or CTRL(fail) */
    TISA_RECIPE_INSTR           = 1, /* Non-STORE primitive instruction: TAKE CTRL+REG(dst), KEEP reads -> MAKE CTRL(next)+REG(dst) or CTRL(fail) */
    TISA_RECIPE_STORE_BYTE      = 2, /* Byte phase of STORE: TAKE CTRL+BYTE(phys_addr), KEEP REG(base,src),FRAME(ptr,len) -> MAKE CTRL(next)+BYTE */
    TISA_RECIPE_BRANCH          = 3, /* Control.Branch: TAKE CTRL, KEEP REG(cond) -> MAKE CTRL(cond==1 ? next : cond==0 ? alt : fail) */
    TISA_RECIPE_LOOP_ENTRY      = 4, /* Control.Loop.entry: TAKE CTRL+REG(idx), KEEP REG(bound) -> MAKE CTRL(next)+REG(idx:=0) or CTRL(fail) */
    TISA_RECIPE_LOOP_HEADER     = 5, /* Control.Loop.header: TAKE CTRL, KEEP REG(idx,bound) -> MAKE CTRL(idx<bound ? next : alt) */
    TISA_RECIPE_LOOP_LATCH      = 6, /* Control.Loop.latch: TAKE CTRL+REG(idx), KEEP REG(bound) -> MAKE CTRL(next)+REG(idx:=idx+1) */
    TISA_RECIPE_FINALIZE_STATUS = 7, /* TAKE CTRL+FRAME(status), KEEP [REG(ret_reg)] -> MAKE CTRL(next)+FRAME(status:=outcome) */
    TISA_RECIPE_FINALIZE_RETVAL = 8  /* TAKE CTRL+FRAME(ret_val), KEEP [REG(ret_reg)] -> MAKE CTRL(HALT(outcome))+FRAME(ret_val:=val) */
} TcIsaRecipeKind;

typedef struct {
    uint32_t        point;        /* Semantic PC point ID (0 .. npoints-1) */
    TcIsaRecipeKind kind;         /* Recipe transition kind */
    bool            charges_fuel; /* True iff this point charges 1 unit of fuel */
    TcIsaInstr      instr;        /* For TISA_RECIPE_INSTR and TISA_RECIPE_STORE_BYTE */
    uint8_t         byte_phase;   /* 0 .. byte_width-1 for TISA_RECIPE_STORE_BYTE */
    uint8_t         byte_width;   /* 1, 4, or 8 for TISA_RECIPE_STORE_BYTE */
    uint8_t         cond_reg;     /* Condition register for TISA_RECIPE_BRANCH */
    uint8_t         idx_reg;      /* Loop index register for TISA_RECIPE_LOOP_* */
    uint8_t         bound_reg;    /* Loop bound register for TISA_RECIPE_LOOP_* */
    uint32_t        max_iters;    /* Static loop cap for TISA_RECIPE_LOOP_* */
    TcIsaOutcome    outcome;      /* Outcome for TISA_RECIPE_FINALIZE_* */
    uint8_t         ret_reg;      /* Return register (<12 for RETURN, 0xFF for failure finalize) */
    uint64_t        next_point;   /* Primary / true / body / step continuation point (or HALT) */
    uint64_t        alt_point;    /* False / loop-exit continuation point (or UINT64_MAX) */
    uint64_t        fail_point;   /* Failure finalize entry point (or UINT64_MAX) */
} TcIsaPointRecipe;

typedef struct {
    uint32_t         npoints;
    uint64_t         entry_point;
    uint64_t         fail_status_point[5]; /* Entry point of FINALIZE_STATUS for each TcIsaOutcome 0..4 */
    TcIsaPointRecipe recipes[TISA_MAX_RECIPE_POINTS];
} TcIsaRecipeTable;

/* Lower a validated structured t-ISA program into an immutable semantic-PC recipe table */
bool tisa_lower_recipes(const TcIsaProgram *prog,
                        TcIsaRecipeTable *out,
                        char *err_buf, size_t err_cap);

typedef enum {
    TISA_HOST_STATUS_NO_VERDICT   = 0, /* Host/infrastructure/fire/allocation failure: no semantic verdict */
    TISA_HOST_STATUS_BOARD_HALTED = 1, /* Board reached TISA_POINT_HALT(outcome) and committed state */
    TISA_HOST_STATUS_ABI_REJECTED = 2  /* Pre-board entry ABI validation rejected frame/capabilities */
} TcIsaHostedStatus;

typedef struct {
    TcIsaHostedStatus host_status;
    bool              has_verdict;  /* true iff host_status != TISA_HOST_STATUS_NO_VERDICT */
    bool              board_halted; /* true iff host_status == TISA_HOST_STATUS_BOARD_HALTED */
    TcIsaOutcome      outcome;      /* semantic outcome (valid iff has_verdict == true) */
    uint64_t          halt_pc;      /* terminal CTRL cell value on board (UINT64_MAX if !board_halted) */
    uint64_t          tcalc_fires;  /* number of admitted tc_fire transitions executed */
} TcIsaHostedRunResult;

/* Try-run tcalc-hosted execution with explicit resource limits (no-verdict vs semantic halt boundary):
 * returns true iff out_res->has_verdict == true; returns false (TISA_HOST_STATUS_NO_VERDICT) on any
 * tc_init/tc_rule/tc_post/tc_fire/allocation failure without manufacturing a semantic outcome. */
bool tisa_try_eval_tcalc_with_limits(const TcIsaProgram *prog,
                                     TcIsaRuntimeFrame *frame,
                                     uint64_t regs[TISA_NUM_VREGS],
                                     uint32_t max_terms,
                                     uint32_t max_occs,
                                     TcIsaHostedRunResult *out_res);

bool tisa_try_eval_tcalc(const TcIsaProgram *prog,
                         TcIsaRuntimeFrame *frame,
                         uint64_t regs[TISA_NUM_VREGS],
                         TcIsaHostedRunResult *out_res);

/* Compatibility wrapper around tisa_try_eval_tcalc; callers requiring the formal no-verdict
 * vs semantic-outcome boundary MUST use tisa_try_eval_tcalc / tisa_try_eval_tcalc_with_limits. */
TcIsaOutcome tisa_eval_tcalc(const TcIsaProgram *prog,
                             TcIsaRuntimeFrame *frame,
                             uint64_t regs[TISA_NUM_VREGS],
                             uint64_t *out_tcalc_fires);

/* =========================================================================
 * 6. Direct Deterministic x86-64 Byte Emitter & Executable Buffer
 * ========================================================================= */

typedef enum {
    TISA_PROFILE_P0 = 0, /* Baseline x86-64 */
    TISA_PROFILE_P1 = 1  /* x86-64 + BMI1/BMI2 */
} TcIsaTargetProfile;

typedef struct {
    uint8_t           *code_rx;        /* mmap'd PROT_READ | PROT_EXEC page(s) */
    size_t             code_cap;       /* mmap allocation size */
    uint8_t            bytes[16384];   /* Exact emitted byte image */
    size_t             byte_len;       /* Total emitted bytes (wrapper + body) */
    size_t             body_offset;    /* Offset of leaf body after 9-byte prologue */
    size_t             epilogue_offset;/* Offset of 10-byte epilogue */
    uint32_t           branch_count;   /* Number of emitted near branches */
    TcIsaTargetProfile profile;
    TcIsaFootprint     footprint;
    char               sha256_hex[65]; /* SHA-256 of bytes[0..byte_len-1] */
    TcIsaNativeFn      entry;          /* Callable function pointer */
} TcIsaCompiledX86;

bool tisa_compile_x86(const TcIsaProgram *prog,
                      TcIsaTargetProfile profile,
                      TcIsaCompiledX86 *out,
                      char *err_buf, size_t err_cap);

/* Independently decode and verify raw emitted x86-64 bytes (no-spill, no rsp/rbp, valid branches) */
bool tisa_verify_x86_bytes(const uint8_t *bytes,
                           size_t byte_len,
                           size_t body_off,
                           size_t epilogue_off,
                           char *err_buf, size_t err_cap);

void tisa_free_x86(TcIsaCompiledX86 *comp);

/* Execute a compiled x86-64 leaf after capability validation */
TcIsaOutcome tisa_run_x86(const TcIsaProgram *prog,
                          const TcIsaCompiledX86 *comp,
                          TcIsaRuntimeFrame *frame,
                          uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4,
                          uint64_t a5);

/* Format deterministic JSON manifest + readable disassembly for a compiled program */
bool tisa_write_program_manifest_json(FILE *out,
                                      const TcIsaProgram *prog,
                                      const TcIsaCompiledX86 *comp);

/* =========================================================================
 * 7. Table-Driven Rule & Consumer Specializer (tisa_specialize.c)
 * ========================================================================= */

typedef struct {
    TcIsaStmt *stmts;
    size_t     nstmts;
    size_t     cap_stmts;
} TcIsaArena;

void       tisa_arena_init(TcIsaArena *a, size_t initial_cap);
void       tisa_arena_free(TcIsaArena *a);
TcIsaStmt *tisa_stmt_instr(TcIsaArena *a, TcIsaInstr ins);
TcIsaStmt *tisa_stmt_seq(TcIsaArena *a, const TcIsaStmt *s1, const TcIsaStmt *s2);
TcIsaStmt *tisa_stmt_if(TcIsaArena *a, uint8_t cond_reg, const TcIsaStmt *then_b, const TcIsaStmt *else_b);
TcIsaStmt *tisa_stmt_count_loop(TcIsaArena *a, uint8_t idx_reg, uint8_t bound_reg, uint32_t max_iters, const TcIsaStmt *body);
TcIsaStmt *tisa_stmt_early_exit_loop(TcIsaArena *a, uint8_t idx_reg, uint8_t bound_reg, uint8_t cont_reg, uint32_t max_iters, const TcIsaStmt *body);
TcIsaStmt *tisa_stmt_return(TcIsaArena *a, TcIsaOutcome outcome, uint8_t ret_reg);

/* Build a t-ISA program for any premise-bearing flat rule from its TcInspectRule + TcInspectFlat */
bool tisa_specialize_lean_flat_rule(TcIsaArena *arena,
                                    const TcInspectRule *ir,
                                    const TcInspectFlat *ifl,
                                    const char *rule_name,
                                    TcIsaProgram *out_prog,
                                    char *err_buf, size_t err_cap);

/* Build a t-ISA program for Micromath 4-premise arity-4 substitution rules (sub1 / sub2) */
bool tisa_specialize_micromath_sub_rule(TcIsaArena *arena,
                                        const TcInspectRule *ir,
                                        const TcInspectFlat *ifl,
                                        const char *rule_name,
                                        TcIsaProgram *out_prog,
                                        char *err_buf, size_t err_cap);

/* Build the generic ordinary_flat_fire (flat_fire) t-ISA program */
bool tisa_build_ordinary_flat_fire_prog(TcIsaArena *arena,
                                        TcIsaProgram *out_prog,
                                        char *err_buf, size_t err_cap);

/* Build the complete ordinary_flat_adapter (pack + flat_fire) t-ISA program */
bool tisa_build_ordinary_flat_adapter_prog(TcIsaArena *arena,
                                           TcIsaProgram *out_prog,
                                           char *err_buf, size_t err_cap);

typedef enum {
    TISA_MODE_REF    = 0,
    TISA_MODE_TCALC  = 1,
    TISA_MODE_X86    = 2,
    TISA_MODE_P1_REF = 3,  /* profile p1 reference (tisa_ref.c), through tisa_p1_runner */
    TISA_MODE_P1_X86 = 4   /* profile p1 native code (tisa_x86.c), through tisa_p1_runner */
} TcIsaExecMode;

/* Runs a program under profile p1 in mode TISA_MODE_P1_REF or _P1_X86. A
 * program linking tisa_ref.c and tisa_x86.c installs it; while it is NULL,
 * those modes report UNSUPPORTED. */
extern TcIsaOutcome (*tisa_p1_runner)(const TcIsaProgram *prog, TcIsaExecMode mode,
                                      TcIsaRuntimeFrame *frame, uint64_t regs[TISA_NUM_VREGS]);

/* Execute ordinary_flat_fire (flat_fire) via Ref, tcalc-Hosted, or Direct x86-64 */
TcIsaOutcome tisa_run_ordinary_flat_fire(const TcIsaProgram *prog,
                                         const TcIsaCompiledX86 *comp,
                                         TcIsaExecMode mode,
                                         const uint32_t *fields, size_t nfields,
                                         const uint32_t *rows, size_t nrows,
                                         const uint32_t *guards, size_t nguards,
                                         const uint32_t *make, size_t nmake,
                                         uint32_t *dest, size_t ndest,
                                         uint32_t *scratch, size_t nscratch,
                                         uint64_t *out_steps);

/* Execute ordinary_flat_adapter (pack + flat_fire) via Ref, tcalc-Hosted, or Direct x86-64 */
TcIsaOutcome tisa_run_ordinary_flat_adapter(const TcIsaProgram *prog,
                                            const TcIsaCompiledX86 *comp,
                                            TcIsaExecMode mode,
                                            const uint32_t *source, size_t nsource,
                                            const uint32_t *premises, size_t nprem,
                                            const uint32_t *constants, size_t nconst,
                                            const uint32_t *guards, size_t nguards,
                                            const uint32_t *makes, size_t nmakes,
                                            uint32_t *fields, size_t nfields,
                                            uint32_t *rows, size_t nrows,
                                            uint32_t *dest, size_t ndest,
                                            uint32_t *scratch, size_t nscratch,
                                            uint64_t *out_steps);

/* Execute a table-specialized Micromath sub1/sub2 program via Ref, tcalc-Hosted, or Direct x86-64 */
TcIsaOutcome tisa_run_micromath_sub(const TcIsaProgram *prog,
                                    const TcIsaCompiledX86 *comp,
                                    TcIsaExecMode mode,
                                    const uint64_t p0[4],
                                    const uint64_t p1[4],
                                    const uint64_t p2[4],
                                    const uint64_t p3[4],
                                    uint64_t dest_slot[4],
                                    uint64_t dest_idx,
                                    uint64_t nslot,
                                    uint64_t *out_steps);

/* Execute a table-specialized Lean premise-bearing flat rule via Ref, tcalc-Hosted, or Direct x86-64 */
TcIsaOutcome tisa_run_lean_flat_rule(const TcIsaProgram *prog,
                                     const TcIsaCompiledX86 *comp,
                                     TcIsaExecMode mode,
                                     const void *const *prem_tuples,
                                     uint8_t nprem,
                                     void *out_tuple,
                                     uint64_t *out_steps);

#ifdef __cplusplus
}
#endif
