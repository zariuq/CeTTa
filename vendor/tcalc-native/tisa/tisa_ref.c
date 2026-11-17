/*
 * tisa_ref.c -- t-ISA profile p1: admission and the reference evaluator.
 *
 * Each part implements the section of SEMANTICS.txt that it names. The code
 * favours being evidently right over being fast: it is the reference that
 * emitters and other realizations are compared with.
 *
 * Compiled with TISA_REF_P0_COMPAT (tests only), it reproduces the one place
 * where p0 meant something else on the same program: a loaded BOOL kept its
 * low bit. The differential test needs no other allowance for p0.
 */
#include "tisa_ref.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__extension__ typedef __int128 wide;   /* exact address arithmetic */

/* ---- 1. Values ----------------------------------------------------------- */

static uint64_t norm(TcIsaValType t, uint64_t w) {
    switch (t) {
    case TISA_TYPE_U32Z: return (uint32_t)w;
    case TISA_TYPE_I32S: return (uint64_t)(int64_t)(int32_t)(uint32_t)w;
    default:             return w;   /* U64 and PTR; BOOL operands are checked first */
    }
}

static bool canonical(TcIsaValType t, uint64_t w) {
    return norm(t, w) == w && (t != TISA_TYPE_BOOL || w <= 1);
}

static bool integer_type(TcIsaValType t) {
    return t == TISA_TYPE_U64 || t == TISA_TYPE_U32Z || t == TISA_TYPE_I32S;
}

static bool narrow(TcIsaValType t) { return t == TISA_TYPE_U32Z || t == TISA_TYPE_I32S; }

/* Word i of the 24-word frame; region r's base is word 2r, its length 2r+1. */
static uint64_t frame_word(const TcIsaRuntimeFrame *f, unsigned i) {
    uint64_t w;
    memcpy(&w, (const unsigned char *)f + 8u * i, sizeof w);
    return w;
}

/* ---- 4. Admission -------------------------------------------------------- */

/* A typing gives each register a type, or UNINIT for none. */
typedef struct {
    TcIsaValType type[TISA_NUM_VREGS];
    uint8_t      region[TISA_NUM_VREGS];   /* for PTR */
} Typing;

typedef struct {
    bool   returns;   /* every path through the statement ends in RETURN */
    Typing after;     /* the typing where it falls through */
} Flow;

typedef struct {
    const TcIsaProgram *prog;
    TcIsaAdmission     *out;
    char               *err;
    size_t              cap;
    bool                refused;
} Admit;

static bool refuse(Admit *a, const char *format, ...) {
    if (!a->refused && a->err && a->cap) {
        va_list args;
        va_start(args, format);
        vsnprintf(a->err, a->cap, format, args);
        va_end(args);
    }
    a->refused = true;
    return false;
}

#define NEED(cond, ...) do { if (!(cond)) return refuse(a, __VA_ARGS__); } while (0)

static bool typed(const Typing *t, uint8_t r) {
    return r < TISA_NUM_VREGS && t->type[r] != TISA_TYPE_UNINIT;
}

static bool pointer_to(const Typing *t, uint8_t r, uint8_t region) {
    return typed(t, r) && t->type[r] == TISA_TYPE_PTR && t->region[r] == region;
}

static bool declared(const TcIsaProgram *p, uint8_t region) {
    return region < p->nregions && p->regions[region].mode != TISA_CAP_NONE;
}

static bool same_type(const Typing *x, const Typing *y, unsigned r) {
    return x->type[r] == y->type[r] && (x->type[r] != TISA_TYPE_PTR || x->region[r] == y->region[r]);
}

/* Types are sets of words: BOOL lies in U32Z and in I32S, and every type
 * lies in U64. Where paths meet, a register gets the least type holding
 * both values; one uninitialized on either path has no type. */
static TcIsaValType join_type(const Typing *x, const Typing *y, unsigned r) {
    TcIsaValType a = x->type[r], b = y->type[r];
    if (a == TISA_TYPE_UNINIT || b == TISA_TYPE_UNINIT) return TISA_TYPE_UNINIT;
    if (same_type(x, y, r)) return a;
    if (a == TISA_TYPE_BOOL && (b == TISA_TYPE_U32Z || b == TISA_TYPE_I32S)) return b;
    if (b == TISA_TYPE_BOOL && (a == TISA_TYPE_U32Z || a == TISA_TYPE_I32S)) return a;
    return TISA_TYPE_U64;
}

static Typing join(const Typing *x, const Typing *y) {
    Typing m;
    for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) {
        m.type[r] = join_type(x, y, r);
        m.region[r] = m.type[r] == TISA_TYPE_PTR ? x->region[r] : 0;
    }
    return m;
}

static bool equal_typing(const Typing *x, const Typing *y) {
    for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) if (!same_type(x, y, r)) return false;
    return true;
}

static uint16_t bit(uint8_t r) { return (uint16_t)(1u << r); }

#ifdef TISA_REF_CHECK_TYPES
/* Tests only: the typing admission found before each instruction, so that a
 * run can confirm every typed register holds a value of its type there. */
#define CHECKED_SLOTS 8192
static struct { const TcIsaStmt *at; Typing typing; } checked[CHECKED_SLOTS];
static size_t checked_slot(const TcIsaStmt *s) {
    size_t h = ((uintptr_t)s >> 4) % CHECKED_SLOTS;
    while (checked[h].at && checked[h].at != s) h = (h + 1) % CHECKED_SLOTS;
    return h;
}
static void record_typing(const TcIsaStmt *s, const Typing *t) {
    size_t h = checked_slot(s);
    checked[h].at = s; checked[h].typing = *t;
}
#endif

static bool admit_instr(Admit *a, const TcIsaInstr *i, Typing *t, uint16_t locked) {
    const TcIsaProgram *p = a->prog;
    bool writes = i->op != TISA_OP_STORE;
    TcIsaValType result = i->ty;
    uint8_t region = 0;
    uint16_t reads = 0;
    if (writes)
        NEED(i->dst < TISA_NUM_VREGS && !(locked & bit(i->dst)),
             "instruction writes v%u, which cannot be written here", (unsigned)i->dst);
    switch (i->op) {
    case TISA_OP_IMM:
        NEED(integer_type(i->ty) || i->ty == TISA_TYPE_BOOL, "IMM has no value type");
        NEED(i->ty != TISA_TYPE_U32Z || i->imm64 <= UINT32_MAX, "IMM U32Z value exceeds 32 bits");
        NEED(i->ty != TISA_TYPE_BOOL || i->imm64 <= 1, "IMM BOOL value outside {0,1}");
        break;
    case TISA_OP_MOV:
        NEED(typed(t, i->src1), "MOV reads untyped v%u", (unsigned)i->src1);
        reads = bit(i->src1);
        if (result == TISA_TYPE_UNINIT) result = t->type[i->src1];
        NEED(result != TISA_TYPE_BOOL || t->type[i->src1] == TISA_TYPE_BOOL, "MOV BOOL needs a BOOL source");
        NEED(result != TISA_TYPE_PTR || t->type[i->src1] == TISA_TYPE_PTR, "MOV PTR needs a PTR source");
        region = t->region[i->src1];
        break;
    case TISA_OP_BIN:
        NEED(typed(t, i->src1) && typed(t, i->src2), "BIN reads an untyped register");
        NEED(i->subop <= TISA_BIN_MULLO, "BIN operation %u unknown", (unsigned)i->subop);
        if (i->ty == TISA_TYPE_BOOL)
            NEED((i->subop == TISA_BIN_AND || i->subop == TISA_BIN_OR || i->subop == TISA_BIN_XOR) &&
                 t->type[i->src1] == TISA_TYPE_BOOL && t->type[i->src2] == TISA_TYPE_BOOL,
                 "BIN BOOL needs AND, OR or XOR of two BOOL registers");
        else NEED(integer_type(i->ty), "BIN needs an integer or BOOL type");
        reads = bit(i->src1) | bit(i->src2);
        break;
    case TISA_OP_SHIFT:
        NEED(typed(t, i->src1), "SHIFT reads untyped v%u", (unsigned)i->src1);
        NEED(integer_type(i->ty), "SHIFT needs an integer type");
        NEED(i->subop <= TISA_SHIFT_SHRS, "SHIFT operation %u unknown", (unsigned)i->subop);
        NEED(i->bit_width < (narrow(i->ty) ? 32u : 64u), "SHIFT amount %u too large", (unsigned)i->bit_width);
        reads = bit(i->src1);
        break;
    case TISA_OP_EXTRACT:
        NEED(typed(t, i->src1), "EXTRACT reads untyped v%u", (unsigned)i->src1);
        NEED(integer_type(i->ty), "EXTRACT needs an integer type");
        NEED(i->bit_width >= 1 && (unsigned)i->scale_lg2 + i->bit_width <= 64u, "EXTRACT field outside the word");
        reads = bit(i->src1);
        break;
    case TISA_OP_ADDR:
        NEED(declared(p, i->region_id), "ADDR names undeclared region %u", (unsigned)i->region_id);
        NEED(pointer_to(t, i->src1, i->region_id), "ADDR base v%u is not PTR(%u)", (unsigned)i->src1, (unsigned)i->region_id);
        NEED(i->src2 == 0xFFu || typed(t, i->src2), "ADDR index is untyped");
        NEED(i->scale_lg2 <= 3u, "ADDR scale above 8");
        reads = bit(i->src1) | (i->src2 == 0xFFu ? 0 : bit(i->src2));
        result = TISA_TYPE_PTR; region = i->region_id;
        break;
    case TISA_OP_LOAD:
        if (i->region_id == TISA_REGION_FRAME) {
            NEED(i->src1 == TISA_V0 && i->subop == TISA_MEM_U64 && i->disp32 >= 0 && i->disp32 % 8 == 0 &&
                 (size_t)i->disp32 + 8u <= sizeof(TcIsaRuntimeFrame), "frame LOAD outside the frame's words");
            NEED(i->ty != TISA_TYPE_UNINIT, "frame LOAD has no type");
            if (i->ty == TISA_TYPE_PTR) {
                NEED(i->disp32 < 144 && i->disp32 % 16 == 0 && declared(p, (uint8_t)(i->disp32 / 16)),
                     "frame LOAD of a pointer names no declared region");
                region = (uint8_t)(i->disp32 / 16);
            }
        } else {
            NEED(declared(p, i->region_id), "LOAD names undeclared region %u", (unsigned)i->region_id);
            NEED(pointer_to(t, i->src1, i->region_id), "LOAD base v%u is not PTR(%u)", (unsigned)i->src1, (unsigned)i->region_id);
            NEED(i->subop == TISA_MEM_U8 || i->subop == TISA_MEM_U32 || i->subop == TISA_MEM_U64, "LOAD width must be 1, 4 or 8");
            NEED(integer_type(i->ty) || i->ty == TISA_TYPE_BOOL, "LOAD from a region needs a value type");
        }
        reads = bit(i->src1);
        break;
    case TISA_OP_STORE:
        NEED(i->region_id < p->nregions && p->regions[i->region_id].mode == TISA_CAP_RW,
             "STORE to region %u, which is not writable", (unsigned)i->region_id);
        NEED(pointer_to(t, i->src1, i->region_id), "STORE base v%u is not PTR(%u)", (unsigned)i->src1, (unsigned)i->region_id);
        NEED(typed(t, i->src2), "STORE value v%u is untyped", (unsigned)i->src2);
        NEED(i->subop == TISA_MEM_U8 || i->subop == TISA_MEM_U32 || i->subop == TISA_MEM_U64, "STORE width must be 1, 4 or 8");
        reads = bit(i->src1) | bit(i->src2);
        break;
    case TISA_OP_CMP:
        NEED(typed(t, i->src1), "CMP reads untyped v%u", (unsigned)i->src1);
        NEED(i->bit_width != 0 || typed(t, i->src2), "CMP reads untyped v%u", (unsigned)i->src2);
        NEED(i->subop <= TISA_CMP_SLE, "CMP condition %u unknown", (unsigned)i->subop);
        reads = bit(i->src1) | (i->bit_width != 0 ? 0 : bit(i->src2));
        result = TISA_TYPE_BOOL;
        break;
    case TISA_OP_SELECT:
        NEED(typed(t, i->src3) && t->type[i->src3] == TISA_TYPE_BOOL, "SELECT condition v%u is not BOOL", (unsigned)i->src3);
        NEED(typed(t, i->src1) && typed(t, i->src2), "SELECT reads an untyped register");
        NEED(i->ty != TISA_TYPE_UNINIT, "SELECT has no type");
        NEED(i->ty != TISA_TYPE_BOOL || (t->type[i->src1] == TISA_TYPE_BOOL && t->type[i->src2] == TISA_TYPE_BOOL),
             "SELECT BOOL needs BOOL operands");
        NEED(i->ty != TISA_TYPE_PTR || (t->type[i->src1] == TISA_TYPE_PTR && pointer_to(t, i->src2, t->region[i->src1])),
             "SELECT PTR needs two pointers to one region");
        reads = bit(i->src1) | bit(i->src2) | bit(i->src3);
        region = t->region[i->src1];
        break;
    default:
        return refuse(a, "unknown opcode %d", (int)i->op);
    }
    if (a->out) {
        a->out->read_regs |= reads;
        if (i->op == TISA_OP_LOAD && i->region_id != TISA_REGION_FRAME) a->out->read_regions |= bit(i->region_id);
        if (i->op == TISA_OP_STORE) a->out->written_regions |= bit(i->region_id);
    }
    if (writes) {
        t->type[i->dst] = result;
        t->region[i->dst] = result == TISA_TYPE_PTR ? region : 0;
        if (a->out) a->out->written_regs |= bit(i->dst);
    }
    return true;
}

static bool admit_stmt(Admit *a, const TcIsaStmt *s, const Typing *in, uint16_t locked, Flow *f);

/* The typing at every entry to a loop body: the least typing above the
 * loop's entry typing that the body preserves. Each round can only widen a
 * register's type, at most three times (BOOL, U32Z, U64, none), so this ends
 * within 3 * TISA_NUM_VREGS + 1 rounds. */
static bool loop_invariant(Admit *a, const TcIsaStmt *body, Typing *invariant, uint16_t locked) {
    for (unsigned round = 0; round <= 3 * TISA_NUM_VREGS; ++round) {
        Flow f;
        if (!admit_stmt(a, body, invariant, locked, &f)) return false;
        if (f.returns) return true;          /* the body never comes back to the header */
        Typing next = join(invariant, &f.after);
        if (equal_typing(&next, invariant)) return true;
        *invariant = next;
    }
    return refuse(a, "loop typing did not settle");   /* unreachable: see above */
}

static bool admit_stmt(Admit *a, const TcIsaStmt *s, const Typing *in, uint16_t locked, Flow *f) {
    f->returns = false;
    f->after = *in;
    if (!s) return true;
    switch (s->kind) {
    case TISA_CTRL_INSTR:
#ifdef TISA_REF_CHECK_TYPES
        record_typing(s, in);   /* the last pass over a loop body is under its invariant */
#endif
        return admit_instr(a, &s->u.instr, &f->after, locked);
    case TISA_CTRL_SEQ: {
        Flow first;
        if (!admit_stmt(a, s->u.seq.first, in, locked, &first)) return false;
        /* A statement after one that always returns is unreachable; it is
           still checked, under the typing the first one ended with. */
        if (!admit_stmt(a, s->u.seq.second, &first.after, locked, f)) return false;
        f->returns = first.returns || f->returns;
        return true;
    }
    case TISA_CTRL_IF: {
        uint8_t c = s->u.if_cond.cond_reg;
        NEED(typed(in, c) && in->type[c] == TISA_TYPE_BOOL, "IF condition v%u is not BOOL", (unsigned)c);
        if (a->out) a->out->read_regs |= bit(c);
        Flow yes, no;
        if (!admit_stmt(a, s->u.if_cond.then_branch, in, locked, &yes) ||
            !admit_stmt(a, s->u.if_cond.else_branch, in, locked, &no)) return false;
        f->returns = yes.returns && no.returns;
        f->after = yes.returns ? no.after : no.returns ? yes.after : join(&yes.after, &no.after);
        return true;
    }
    case TISA_CTRL_COUNT_LOOP:
    case TISA_CTRL_EARLY_EXIT_LOOP: {
        bool early = s->kind == TISA_CTRL_EARLY_EXIT_LOOP;
        uint8_t x = early ? s->u.early_exit_loop.idx_reg : s->u.count_loop.idx_reg;
        uint8_t n = early ? s->u.early_exit_loop.bound_reg : s->u.count_loop.bound_reg;
        uint32_t cap = early ? s->u.early_exit_loop.max_iters : s->u.count_loop.max_iters;
        const TcIsaStmt *body = early ? s->u.early_exit_loop.body : s->u.count_loop.body;
        NEED(x < TISA_NUM_VREGS && !(locked & bit(x)), "loop index v%u cannot be written here", (unsigned)x);
        NEED(typed(in, n) && in->type[n] != TISA_TYPE_PTR && in->type[n] != TISA_TYPE_BOOL,
             "loop bound v%u is not an integer", (unsigned)n);
        NEED(x != n, "loop index and bound are one register");
        NEED(cap >= 1, "loop cap is zero");
        Typing invariant = *in;
        invariant.type[x] = TISA_TYPE_U64;
        invariant.region[x] = 0;
        uint16_t inner = (uint16_t)(locked | bit(x) | bit(n));
        if (early) {
            uint8_t k = s->u.early_exit_loop.cont_reg;
            NEED(k != x && k != n, "loop registers are not distinct");
            NEED(typed(in, k) && in->type[k] == TISA_TYPE_BOOL, "loop continuation v%u is not BOOL", (unsigned)k);
            if (a->out) a->out->read_regs |= bit(k);
        }
        if (!loop_invariant(a, body, &invariant, inner)) return false;
        if (early) {
            uint8_t k = s->u.early_exit_loop.cont_reg;
            NEED(invariant.type[k] == TISA_TYPE_BOOL, "loop continuation v%u loses its BOOL type in the body", (unsigned)k);
        }
        if (a->out) { a->out->read_regs |= bit(n); a->out->written_regs |= bit(x); }
        f->after = invariant;     /* the body may run zero times */
        return true;
    }
    case TISA_CTRL_RETURN:
        NEED(typed(in, s->u.ret.ret_reg), "RETURN reads untyped v%u", (unsigned)s->u.ret.ret_reg);
        NEED(s->u.ret.outcome == TISA_OUTCOME_FIRED || s->u.ret.outcome == TISA_OUTCOME_REFUSED ||
             s->u.ret.outcome == TISA_OUTCOME_UNSUPPORTED, "RETURN outcome %d not allowed", (int)s->u.ret.outcome);
        if (a->out) a->out->read_regs |= bit(s->u.ret.ret_reg);
        f->returns = true;
        return true;
    case TISA_CTRL_FUND:
        NEED(s->u.fund.units >= 1, "FUND of zero units");
        if (a->out) a->out->fund_points += 1;
        return true;
    }
    return refuse(a, "unknown statement kind %d", (int)s->kind);
}

/* A bound on what one run can add to the receipt, as a natural number:
 * false when it exceeds 64 bits. A loop that cannot iterate pays nothing,
 * whatever its body. */
static bool max_receipt(const TcIsaStmt *s, uint64_t *out) {
    uint64_t x = 0, y = 0;
    *out = 0;
    if (!s) return true;
    switch (s->kind) {
    case TISA_CTRL_FUND: *out = s->u.fund.units; return true;
    case TISA_CTRL_SEQ:
        return max_receipt(s->u.seq.first, &x) && max_receipt(s->u.seq.second, &y) && !__builtin_add_overflow(x, y, out);
    case TISA_CTRL_IF:
        if (!max_receipt(s->u.if_cond.then_branch, &x) || !max_receipt(s->u.if_cond.else_branch, &y)) return false;
        *out = x > y ? x : y;
        return true;
    case TISA_CTRL_COUNT_LOOP:
        if (!s->u.count_loop.max_iters) return true;
        return max_receipt(s->u.count_loop.body, &x) && !__builtin_mul_overflow((uint64_t)s->u.count_loop.max_iters, x, out);
    case TISA_CTRL_EARLY_EXIT_LOOP:
        if (!s->u.early_exit_loop.max_iters) return true;
        return max_receipt(s->u.early_exit_loop.body, &x) && !__builtin_mul_overflow((uint64_t)s->u.early_exit_loop.max_iters, x, out);
    default: return true;
    }
}

bool tisa_admit(const TcIsaProgram *prog, TcIsaAdmission *out, char *err, size_t cap) {
    TcIsaAdmission local;
    Admit state = {.prog = prog, .out = out ? out : &local, .err = err, .cap = cap}, *a = &state;
#ifdef TISA_REF_CHECK_TYPES
    memset(checked, 0, sizeof checked);
#endif
    memset(a->out, 0, sizeof *a->out);
    if (err && cap) err[0] = 0;
    NEED(prog && prog->root, "no program");
    NEED(prog->nregions <= TISA_MAX_REGIONS, "%u regions declared; at most %u", (unsigned)prog->nregions, TISA_MAX_REGIONS);
    for (uint8_t r = 0; r < prog->nregions; ++r) {
        TcIsaCapMode m = prog->regions[r].mode;
        NEED(m == TISA_CAP_NONE || m == TISA_CAP_RO || m == TISA_CAP_RW, "region %u has an unknown mode", (unsigned)r);
    }
    NEED(!(prog->init_reg_mask & ~0x3Fu), "only v0..v5 can be initialized on entry");
    Typing entry;
    memset(&entry, 0, sizeof entry);
    for (uint8_t r = 0; r < TISA_NUM_VREGS; ++r) {
        if (!(prog->init_reg_mask & bit(r))) continue;
        TcIsaValType ty = prog->init_reg_types[r];
        NEED(integer_type(ty) || ty == TISA_TYPE_BOOL || ty == TISA_TYPE_PTR, "entry register v%u has no type", (unsigned)r);
        NEED(ty != TISA_TYPE_PTR || declared(prog, prog->init_reg_region[r]) || r == TISA_V0,
             "entry pointer v%u names an undeclared region", (unsigned)r);
        entry.type[r] = ty;
        entry.region[r] = ty == TISA_TYPE_PTR ? prog->init_reg_region[r] : 0;
    }
    /* v0 is the frame; it is read only by frame LOADs and never written. */
    entry.type[TISA_V0] = TISA_TYPE_PTR;
    entry.region[TISA_V0] = TISA_REGION_FRAME;
    Flow f;
    if (!admit_stmt(a, prog->root, &entry, bit(TISA_V0), &f)) return false;
    NEED(f.returns, "a path ends without RETURN");
    uint64_t bound;
    a->out->max_receipt_fits = max_receipt(prog->root, &bound);
    a->out->max_receipt = a->out->max_receipt_fits ? bound : UINT64_MAX;
    return true;
}

/* The executable reference semantics is a development-only oracle. */
#if TISA_DEV_REFERENCE
/* ---- 5 to 8. A run ------------------------------------------------------- */

typedef struct {
    const TcIsaProgram *prog;
    TcIsaRuntimeFrame  *frame;
    uint64_t           *v;          /* registers v0..v11 */
    TcIsaOutcome        outcome;
} Run;

/* Section 7: a failure leaves its code in status and ret_val. */
static bool fail(Run *r, TcIsaOutcome o) {
    r->outcome = o;
    r->frame->status = (uint64_t)o;
    r->frame->ret_val = (uint64_t)o;
    return false;
}

#define CHECK_BOOL(r, w) do { if ((w) > 1) return fail((r), TISA_OUTCOME_INVALID_INPUT); } while (0)

/* The address of [e, e + width) when it lies inside region; e computed exactly. */
static bool inside(Run *r, uint8_t region, wide e, unsigned width, uint64_t *at) {
    wide base = frame_word(r->frame, 2u * region), len = frame_word(r->frame, 2u * region + 1u);
    if (e < base || e + width > base + len) return false;
    *at = (uint64_t)e;
    return true;
}

static uint64_t load_le(uint64_t at, unsigned width) {
    const unsigned char *p = (const unsigned char *)(uintptr_t)at;
    uint64_t w = 0;
    for (unsigned b = 0; b < width; ++b) w |= (uint64_t)p[b] << (8u * b);
    return w;
}

static void store_le(uint64_t at, unsigned width, uint64_t w) {
    unsigned char *p = (unsigned char *)(uintptr_t)at;
    for (unsigned b = 0; b < width; ++b) p[b] = (unsigned char)(w >> (8u * b));
}

static bool compare(TcIsaCmpCond c, TcIsaValType t, uint64_t x, uint64_t y) {
    if (narrow(t)) {
        uint32_t ux = (uint32_t)x, uy = (uint32_t)y;
        int32_t sx = (int32_t)ux, sy = (int32_t)uy;
        switch (c) {
        case TISA_CMP_EQ:  return ux == uy;
        case TISA_CMP_NE:  return ux != uy;
        case TISA_CMP_ULT: return ux < uy;
        case TISA_CMP_ULE: return ux <= uy;
        case TISA_CMP_SLT: return sx < sy;
        case TISA_CMP_SLE: return sx <= sy;
        }
    }
    switch (c) {
    case TISA_CMP_EQ:  return x == y;
    case TISA_CMP_NE:  return x != y;
    case TISA_CMP_ULT: return x < y;
    case TISA_CMP_ULE: return x <= y;
    case TISA_CMP_SLT: return (int64_t)x < (int64_t)y;
    case TISA_CMP_SLE: return (int64_t)x <= (int64_t)y;
    }
    return false;
}

static uint64_t shift(TcIsaShiftOp op, TcIsaValType t, uint64_t w, unsigned s) {
    if (narrow(t)) {
        uint32_t u = (uint32_t)w;
        uint32_t out = op == TISA_SHIFT_SHL ? u << s : op == TISA_SHIFT_SHRU ? u >> s : (uint32_t)((int32_t)u >> s);
        return norm(t, out);
    }
    return op == TISA_SHIFT_SHL ? w << s : op == TISA_SHIFT_SHRU ? w >> s : (uint64_t)((int64_t)w >> s);
}

/* Section 5. False when the run stopped. */
static bool instruction(Run *r, const TcIsaInstr *i) {
    uint64_t *v = r->v, out = 0, at;
    switch (i->op) {
    case TISA_OP_IMM:
        out = norm(i->ty, i->imm64);
        break;
    case TISA_OP_MOV:
        if (i->ty == TISA_TYPE_BOOL) CHECK_BOOL(r, v[i->src1]);
        out = norm(i->ty, v[i->src1]);
        break;
    case TISA_OP_BIN: {
        uint64_t x = v[i->src1], y = v[i->src2];
        if (i->ty == TISA_TYPE_BOOL) { CHECK_BOOL(r, x); CHECK_BOOL(r, y); }
        switch ((TcIsaBinOp)i->subop) {
        case TISA_BIN_ADD:   out = x + y; break;
        case TISA_BIN_SUB:   out = x - y; break;
        case TISA_BIN_AND:   out = x & y; break;
        case TISA_BIN_OR:    out = x | y; break;
        case TISA_BIN_XOR:   out = x ^ y; break;
        case TISA_BIN_MULLO: out = x * y; break;
        }
        out = norm(i->ty, out);
        break;
    }
    case TISA_OP_SHIFT:
        out = shift((TcIsaShiftOp)i->subop, i->ty, v[i->src1], i->bit_width);
        break;
    case TISA_OP_EXTRACT: {
        uint64_t field = v[i->src1] >> i->scale_lg2;
        if (i->bit_width < 64) field &= (UINT64_C(1) << i->bit_width) - 1;
        out = norm(i->ty, field);
        break;
    }
    case TISA_OP_CMP:
        out = compare((TcIsaCmpCond)i->subop, i->ty, v[i->src1], i->bit_width != 0 ? i->imm64 : v[i->src2]);
        break;
    case TISA_OP_SELECT:
        CHECK_BOOL(r, v[i->src3]);
        if (i->ty == TISA_TYPE_BOOL) { CHECK_BOOL(r, v[i->src1]); CHECK_BOOL(r, v[i->src2]); }
        out = norm(i->ty, v[i->src3] ? v[i->src1] : v[i->src2]);
        break;
    case TISA_OP_ADDR: {
        /* A pointer never leaves its region: first with the index added,
           then with the displacement. */
        wide index = i->src2 == 0xFFu ? 0 : (wide)v[i->src2];
        wide e = (wide)v[i->src1] + index * ((wide)1 << i->scale_lg2);
        if (!inside(r, i->region_id, e, 0, &at) || !inside(r, i->region_id, e + i->disp32, 0, &out))
            return fail(r, TISA_OUTCOME_INVALID_INPUT);
        break;
    }
    case TISA_OP_LOAD:
        if (i->region_id == TISA_REGION_FRAME) out = frame_word(r->frame, (unsigned)i->disp32 / 8u);
        else {
            if (!inside(r, i->region_id, (wide)v[i->src1] + i->disp32, i->subop, &at))
                return fail(r, TISA_OUTCOME_INVALID_INPUT);
            out = load_le(at, i->subop);
        }
#ifdef TISA_REF_P0_COMPAT
        if (i->ty == TISA_TYPE_BOOL) out &= 1;   /* p0 kept the low bit of a loaded BOOL */
#endif
        if (i->ty == TISA_TYPE_BOOL) CHECK_BOOL(r, out);
        out = norm(i->ty, out);
        break;
    case TISA_OP_STORE:
        if (!inside(r, i->region_id, (wide)v[i->src1] + i->disp32, i->subop, &at))
            return fail(r, TISA_OUTCOME_INVALID_INPUT);
        store_le(at, i->subop, v[i->src2]);
        return true;
    }
    v[i->dst] = out;
    return true;
}

/* Sections 3, 7 and 8. False when the run stopped. */
static bool statement(Run *r, const TcIsaStmt *s) {
    uint64_t *v = r->v;
    if (!s) return true;
    switch (s->kind) {
    case TISA_CTRL_INSTR:
#ifdef TISA_REF_CHECK_TYPES
        {
            size_t h = checked_slot(s);
            const Typing *t = &checked[h].typing;
            if (checked[h].at != s) { fprintf(stderr, "type check: instruction never admitted\n"); abort(); }
            for (unsigned q = 1; q < TISA_NUM_VREGS; ++q) {
                TcIsaValType ty = t->type[q];
                bool ok = ty == TISA_TYPE_UNINIT || (ty == TISA_TYPE_PTR ?
                    (v[q] >= frame_word(r->frame, 2u * t->region[q]) &&
                     v[q] - frame_word(r->frame, 2u * t->region[q]) <= frame_word(r->frame, 2u * t->region[q] + 1u)) :
                    canonical(ty, v[q]));
                if (!ok) { fprintf(stderr, "type check: v%u is not a value of its admitted type %d\n", q, (int)ty); abort(); }
            }
        }
#endif
        return instruction(r, &s->u.instr);
    case TISA_CTRL_SEQ:
        return statement(r, s->u.seq.first) && statement(r, s->u.seq.second);
    case TISA_CTRL_IF:
        CHECK_BOOL(r, v[s->u.if_cond.cond_reg]);
        return statement(r, v[s->u.if_cond.cond_reg] ? s->u.if_cond.then_branch : s->u.if_cond.else_branch);
    case TISA_CTRL_COUNT_LOOP: {
        uint8_t x = s->u.count_loop.idx_reg;
        uint64_t n = v[s->u.count_loop.bound_reg];
        if (n > s->u.count_loop.max_iters) return fail(r, TISA_OUTCOME_UNSUPPORTED);
        for (v[x] = 0; v[x] < n; ++v[x])
            if (!statement(r, s->u.count_loop.body)) return false;
        return true;
    }
    case TISA_CTRL_EARLY_EXIT_LOOP: {
        uint8_t x = s->u.early_exit_loop.idx_reg, k = s->u.early_exit_loop.cont_reg;
        uint64_t n = v[s->u.early_exit_loop.bound_reg];
        if (n > s->u.early_exit_loop.max_iters) return fail(r, TISA_OUTCOME_UNSUPPORTED);
        for (v[x] = 0; v[x] < n; ++v[x]) {
            CHECK_BOOL(r, v[k]);
            if (!v[k]) break;
            if (!statement(r, s->u.early_exit_loop.body)) return false;
            CHECK_BOOL(r, v[k]);
            if (!v[k]) break;
        }
        return true;
    }
    case TISA_CTRL_RETURN:
        r->outcome = s->u.ret.outcome;
        r->frame->status = (uint64_t)s->u.ret.outcome;
        r->frame->ret_val = v[s->u.ret.ret_reg];
        return false;
    case TISA_CTRL_FUND: {
        uint64_t allowance = r->frame->fuel_budget, receipt = r->frame->fuel_used;
        if (allowance - receipt < s->u.fund.units) return fail(r, TISA_OUTCOME_EXHAUSTED);
        r->frame->fuel_used = receipt + s->u.fund.units;
        return true;
    }
    }
    return fail(r, TISA_OUTCOME_UNSUPPORTED);
}

#endif

/* Section 6. */
bool tisa_entry_valid(const TcIsaProgram *p, const TcIsaRuntimeFrame *f, const uint64_t v[TISA_NUM_VREGS]) {
    uint64_t frame_at = (uint64_t)(uintptr_t)f;
    uint64_t frame_end = frame_at + sizeof *f;
    uint64_t written_at = frame_at + offsetof(TcIsaRuntimeFrame, status);
    if (v[TISA_V0] != frame_at) return false;
    if (f->fuel_used > f->fuel_budget) return false;
    for (uint8_t r = 0; r < p->nregions; ++r) {
        const TcIsaRegionDecl *d = &p->regions[r];
        if (d->mode == TISA_CAP_NONE) continue;
        uint64_t base = frame_word(f, 2u * r), len = frame_word(f, 2u * r + 1u);
        if (len < d->min_byte_len || (len && !base) || base > UINT64_MAX - len) return false;
        if (d->align_bytes > 1 && base % d->align_bytes) return false;
        if (!len) continue;
        /* A writable region is disjoint from every other region and the
           frame; a read-only one from the frame words a run writes. */
        uint64_t end = base + len;
        if (d->mode == TISA_CAP_RW ? !(end <= frame_at || frame_end <= base)
                                   : !(end <= written_at || frame_end <= base)) return false;
        for (uint8_t q = (uint8_t)(r + 1); q < p->nregions; ++q) {
            const TcIsaRegionDecl *e = &p->regions[q];
            uint64_t qbase = frame_word(f, 2u * q), qlen = frame_word(f, 2u * q + 1u);
            if (e->mode == TISA_CAP_NONE || !qlen || (d->mode != TISA_CAP_RW && e->mode != TISA_CAP_RW)) continue;
            if (qbase > UINT64_MAX - qlen) return false;
            if (!(end <= qbase || qbase + qlen <= base)) return false;
        }
    }
    for (uint8_t r = 1; r < TISA_NUM_VREGS; ++r) {
        if (!(p->init_reg_mask & bit(r))) continue;
        TcIsaValType t = p->init_reg_types[r];
        if (t == TISA_TYPE_PTR) {
            uint8_t q = p->init_reg_region[r];
            uint64_t base = frame_word(f, 2u * q), len = frame_word(f, 2u * q + 1u);
            if (v[r] < base || v[r] - base > len) return false;
        } else if (!canonical(t, v[r])) return false;
    }
    return true;
}

#if TISA_DEV_REFERENCE
TcIsaOutcome tisa_run(const TcIsaProgram *prog, TcIsaRuntimeFrame *frame, uint64_t regs[TISA_NUM_VREGS]) {
    Run r = {.prog = prog, .frame = frame, .v = regs, .outcome = TISA_OUTCOME_UNSUPPORTED};
    if (!frame) return TISA_OUTCOME_INVALID_INPUT;
    if (!tisa_admit(prog, NULL, NULL, 0)) { fail(&r, TISA_OUTCOME_UNSUPPORTED); return r.outcome; }
    if (!tisa_entry_valid(prog, frame, regs)) { fail(&r, TISA_OUTCOME_INVALID_INPUT); return r.outcome; }
    if (statement(&r, prog->root)) fail(&r, TISA_OUTCOME_UNSUPPORTED);   /* admitted programs always return */
    return r.outcome;
}

#endif
