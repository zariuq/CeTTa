/*
 * tisa_x86.c -- t-ISA profile p1 to x86-64 (SysV LP64).
 *
 * Registers: v0..v11 are rdi rsi rdx rcx r8 r9 r10 r11 rax rbx r12 r13, the
 * scratch registers s0 s1 are r14 r15, and rbp holds the remaining allowance
 * (fuel_budget - fuel_used) for the whole run. v0 holds the frame throughout.
 * The entry saves rbx r12 r13 r14 r15 rbp; every exit writes fuel_used back.
 * A run that stops on a failure jumps to one of three shared handlers, which
 * write the outcome code to status and ret_val (SEMANTICS.txt, section 7).
 * The image has the reference's observable behaviour: outcome, status,
 * ret_val, fuel_used and every region byte.
 *
 * Checks are decided by facts gathered in one pass in execution order:
 *
 * Region facts. For each register, whether it holds a region's base plus a
 * known offset c (from the frame's base word, through constant ADDR and MOV),
 * or points into a region with at least `room` bytes after it; for each
 * region, a lower bound on its length (the declared minimum, which entry
 * validation guarantees, raised by each length test passed). An access the
 * facts place inside its region runs unchecked; one at a known offset beyond
 * the bound needs one length test; any other gets the full check, each step
 * with one carry or borrow test. Branches join their facts; a loop forgets
 * what its body writes.
 *
 * Indexed ADDR. Its check covers, at once, the bytes that the loads and stores
 * through its result use before the next side effect (a store, FUND, return,
 * branch or loop): those accesses then run unchecked. The outcome cannot
 * differ: an access that would fail fails at the ADDR instead, and nothing
 * observable happens in between. Each register also has an upper bound on its
 * value's width (from load widths, types and arithmetic); an index narrow
 * enough that the scaled sum cannot overflow needs only the length compare.
 *
 * Loops. In a loop the index x runs from 0 to n - 1 and the body writes
 * neither x nor n (admission). An access whose pointer is b + k*x + c, with b
 * not written by the body, makes checks on partial sums that are monotone in
 * x, so one test at loop entry, at x = 0 and x = n - 1, decides them for every
 * iteration; FUND likewise, against F * n for F units per iteration. When the
 * entry test passes, a copy of the loop runs without those checks; when not,
 * the checked loop runs. Both test at the bottom. The copy is made when it
 * removes checks that depend on x, or the FUND checks of a small body.
 *
 * Compare and branch. A CMP whose result the next statement, an IF, tests is
 * followed by a branch on the flags; the Bool is written only when a branch or
 * later code reads it (backward liveness over the statement tree).
 *
 * Each span of the image names its byte pattern and the statement behind it.
 */
#define _DEFAULT_SOURCE
#include "tisa_x86.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- Registers and condition codes ---------------------------------------- */

static const uint8_t HW[TISA_TOTAL_REGS] = {7, 6, 2, 1, 8, 9, 10, 11, 0, 3, 12, 13, 14, 15};
enum { RAX = 0, RBX = 3, RBP = 5, RDI = 7, R12 = 12, R13 = 13, S0 = 14, S1 = 15 };
enum { CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_BE = 6, CC_A = 7, CC_L = 12, CC_LE = 14 };

/* Frame words (tisa.h). */
#define AT_BASE(r)  ((int32_t)(16 * (r)))
#define AT_LEN(r)   ((int32_t)(16 * (r) + 8))
#define AT_STATUS   144
#define AT_RET_VAL  152
#define AT_BUDGET   160
#define AT_USED     168

/* ---- Facts ----------------------------------------------------------------- */

enum { F_NONE, F_BASE, F_ROOM };
typedef struct {
    uint8_t  kind, region;
    int64_t  c;      /* F_BASE: the register is the region's base plus c */
    uint64_t room;   /* F_ROOM: the bytes [reg, reg + room) lie in the region */
} Fact;
typedef struct {
    Fact     reg[TISA_NUM_VREGS];
    uint8_t  bits[TISA_NUM_VREGS];    /* each register's value is below 2^bits */
    uint64_t len[TISA_MAX_REGIONS];   /* each region's length is at least this */
    bool     dead;                    /* no run reaches this point */
} Env;

/* Liveness: per IF, which registers its branches read before writing. */
typedef struct { const TcIsaStmt *s; uint16_t then_in, else_in; } Live;
typedef struct { Live *t; size_t cap; } LiveMap;

typedef struct Hoist Hoist;

/* ---- The assembler -------------------------------------------------------- */

enum { L_INVALID, L_UNSUPPORTED, L_EXHAUSTED, L_FAIL, L_EPILOGUE, L_FIRST_FREE };
typedef struct { size_t at; size_t label; } Fix;
typedef struct {
    uint8_t      *b;      size_t len, cap;
    TcIsaX86Span *spans;  size_t nspans, span_cap;
    size_t       *labels; size_t nlabels, label_cap;   /* offsets; SIZE_MAX until placed */
    Fix          *fixes;  size_t nfixes, fix_cap;
    TcIsaX86Loop *loops;  size_t nloops, loop_cap;
    const TcIsaProgram *prog;
    LiveMap       live;
    const Hoist  *hoist;          /* the loop whose unchecked copy is being emitted, else NULL */
    bool          failed;
} Asm;

static void *grow(void *items, size_t *cap, size_t need, size_t size, bool *failed) {
    if (need <= *cap) return items;
    size_t next = *cap ? *cap * 2 : 64;
    while (next < need) next *= 2;
    void *bigger = realloc(items, next * size);
    if (!bigger) { *failed = true; return items; }
    *cap = next;
    return bigger;
}
static void put(Asm *a, uint8_t x) {
    a->b = grow(a->b, &a->cap, a->len + 1, 1, &a->failed);
    if (!a->failed) a->b[a->len++] = x;
}
static void put32(Asm *a, uint32_t x) { for (unsigned i = 0; i < 4; ++i) put(a, (uint8_t)(x >> (8 * i))); }
static void put64(Asm *a, uint64_t x) { for (unsigned i = 0; i < 8; ++i) put(a, (uint8_t)(x >> (8 * i))); }
static size_t new_label(Asm *a) {
    a->labels = grow(a->labels, &a->label_cap, a->nlabels + 1, sizeof *a->labels, &a->failed);
    if (a->failed) return 0;
    a->labels[a->nlabels] = SIZE_MAX;
    return a->nlabels++;
}
static void place(Asm *a, size_t label) { if (!a->failed) a->labels[label] = a->len; }
static void fix(Asm *a, size_t label) {
    a->fixes = grow(a->fixes, &a->fix_cap, a->nfixes + 1, sizeof *a->fixes, &a->failed);
    if (a->failed) return;
    a->fixes[a->nfixes++] = (Fix){a->len, label};
    put32(a, 0);
}
static void span(Asm *a, size_t from, const char *name, const TcIsaStmt *stmt) {
    a->spans = grow(a->spans, &a->span_cap, a->nspans + 1, sizeof *a->spans, &a->failed);
    if (!a->failed)
        a->spans[a->nspans++] = (TcIsaX86Span){(uint32_t)from, (uint32_t)(a->len - from), name, stmt, a->hoist != NULL};
}
static void rex(Asm *a, bool w, uint8_t reg, uint8_t base, bool force) {
    uint8_t r = (uint8_t)(0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((base & 8) ? 1 : 0));
    if (r != 0x40 || force) put(a, r);
}
static void modrm_reg(Asm *a, uint8_t reg, uint8_t rm) { put(a, (uint8_t)(0xC0 | (reg & 7) << 3 | (rm & 7))); }
static void modrm_mem(Asm *a, uint8_t reg, uint8_t base, int32_t disp) {
    put(a, (uint8_t)(0x80 | (reg & 7) << 3 | (base & 7)));
    if ((base & 7) == 4) put(a, 0x24);
    put32(a, (uint32_t)disp);
}
static void op_rr(Asm *a, uint8_t op, bool w, uint8_t rm, uint8_t reg) { rex(a, w, reg, rm, false); put(a, op); modrm_reg(a, reg, rm); }
static void mov64(Asm *a, uint8_t d, uint8_t s) { if (d != s) op_rr(a, 0x89, true, d, s); }
static void mov32(Asm *a, uint8_t d, uint8_t s) { op_rr(a, 0x89, false, d, s); }   /* zero-extends */
static void movsxd(Asm *a, uint8_t d, uint8_t s) { rex(a, true, d, s, false); put(a, 0x63); modrm_reg(a, d, s); }
static void imul(Asm *a, uint8_t d, uint8_t s) { rex(a, true, d, s, false); put(a, 0x0F); put(a, 0xAF); modrm_reg(a, d, s); }
static void imul_imm(Asm *a, uint8_t d, uint8_t s, uint32_t k) { rex(a, true, d, s, false); put(a, 0x69); modrm_reg(a, d, s); put32(a, k); }
/* lea d, [index * 2^scale + disp] */
static void lea_index(Asm *a, uint8_t d, uint8_t index, uint8_t scale, int32_t disp) {
    put(a, (uint8_t)(0x48 | ((d & 8) ? 4 : 0) | ((index & 8) ? 2 : 0)));
    put(a, 0x8D);
    put(a, (uint8_t)((d & 7) << 3 | 4));
    put(a, (uint8_t)(scale << 6 | (index & 7) << 3 | 5));
    put32(a, (uint32_t)disp);
}
/* lea d, [base + index * 2^scale + disp]; index < 0 for none. */
static void lea(Asm *a, uint8_t d, uint8_t base, int index, uint8_t scale, int32_t disp) {
    uint8_t x = index < 0 ? 4 : (uint8_t)index;
    put(a, (uint8_t)(0x48 | ((d & 8) ? 4 : 0) | (index >= 0 && (x & 8) ? 2 : 0) | ((base & 8) ? 1 : 0)));
    put(a, 0x8D);
    uint8_t mod = disp == 0 && (base & 7) != 5 ? 0 : disp >= -128 && disp <= 127 ? 1 : 2;
    put(a, (uint8_t)(mod << 6 | (d & 7) << 3 | 4));
    put(a, (uint8_t)(scale << 6 | (x & 7) << 3 | (base & 7)));
    if (mod == 1) put(a, (uint8_t)(int8_t)disp);
    else if (mod == 2) put32(a, (uint32_t)disp);
}
static void cmov(Asm *a, uint8_t cc, uint8_t d, uint8_t s) { rex(a, true, d, s, false); put(a, 0x0F); put(a, (uint8_t)(0x40 | cc)); modrm_reg(a, d, s); }
static void setcc(Asm *a, uint8_t cc, uint8_t d) { rex(a, false, 0, d, true); put(a, 0x0F); put(a, (uint8_t)(0x90 | cc)); modrm_reg(a, 0, d); }
static void movzx8(Asm *a, uint8_t d, uint8_t s) { rex(a, false, d, s, true); put(a, 0x0F); put(a, 0xB6); modrm_reg(a, d, s); }
/* shifts by an immediate: ext 4 shl, 5 shr, 7 sar */
static void shift(Asm *a, uint8_t ext, bool w, uint8_t d, uint8_t n) { rex(a, w, 0, d, false); put(a, 0xC1); modrm_reg(a, ext, d); put(a, n); }
/* group 1 with imm32 / imm8: ext 0 add, 5 sub, 7 cmp, 4 and */
static void op_imm32(Asm *a, uint8_t ext, bool w, uint8_t d, uint32_t k) { rex(a, w, 0, d, false); put(a, 0x81); modrm_reg(a, ext, d); put32(a, k); }
static void op_imm8(Asm *a, uint8_t ext, bool w, uint8_t d, uint8_t k) { rex(a, w, 0, d, false); put(a, 0x83); modrm_reg(a, ext, d); put(a, k); }
static void mov_imm(Asm *a, uint8_t d, uint64_t k) {
    if (k <= UINT32_MAX) { rex(a, false, 0, d, false); put(a, (uint8_t)(0xB8 | (d & 7))); put32(a, (uint32_t)k); }
    else if ((uint64_t)(int64_t)(int32_t)k == k) { rex(a, true, 0, d, false); put(a, 0xC7); modrm_reg(a, 0, d); put32(a, (uint32_t)k); }
    else { rex(a, true, 0, d, false); put(a, (uint8_t)(0xB8 | (d & 7))); put64(a, k); }
}
static void load(Asm *a, unsigned width, uint8_t d, uint8_t base, int32_t disp) {
    rex(a, width == 8, d, base, false);
    if (width == 1) { put(a, 0x0F); put(a, 0xB6); } else put(a, 0x8B);
    modrm_mem(a, d, base, disp);
}
static void store(Asm *a, unsigned width, uint8_t s, uint8_t base, int32_t disp) {
    rex(a, width == 8, s, base, width == 1);
    put(a, width == 1 ? 0x88 : 0x89);
    modrm_mem(a, s, base, disp);
}
static void op_rm(Asm *a, uint8_t op, uint8_t reg, uint8_t base, int32_t disp) { rex(a, true, reg, base, false); put(a, op); modrm_mem(a, reg, base, disp); }
static void store_imm32(Asm *a, uint8_t base, int32_t disp, uint32_t k) { rex(a, true, 0, base, false); put(a, 0xC7); modrm_mem(a, 0, base, disp); put32(a, k); }
/* cmp qword [base + disp], k (k sign-extended) */
static void cmp_mem_imm32(Asm *a, uint8_t base, int32_t disp, uint32_t k) { rex(a, true, 0, base, false); put(a, 0x81); modrm_mem(a, 7, base, disp); put32(a, k); }
static void jcc(Asm *a, uint8_t cc, size_t label) { put(a, 0x0F); put(a, (uint8_t)(0x80 | cc)); fix(a, label); }
static void jmp(Asm *a, size_t label) { put(a, 0xE9); fix(a, label); }
static void push(Asm *a, uint8_t r) { rex(a, false, 0, r, false); put(a, (uint8_t)(0x50 | (r & 7))); }
static void pop(Asm *a, uint8_t r) { rex(a, false, 0, r, false); put(a, (uint8_t)(0x58 | (r & 7))); }

/* ---- Common pieces ------------------------------------------------------------ */

static void normalize(Asm *a, TcIsaValType t, uint8_t d) {
    if (t == TISA_TYPE_U32Z) mov32(a, d, d);
    else if (t == TISA_TYPE_I32S) movsxd(a, d, d);
}
static void add_offset(Asm *a, int32_t o) {
    if (o > 0) { op_imm32(a, 0, true, S0, (uint32_t)o); jcc(a, CC_B, L_INVALID); }
    else if (o < 0) { mov_imm(a, S1, (uint64_t)-(int64_t)o); op_rr(a, 0x29, true, S0, S1); jcc(a, CC_B, L_INVALID); }
}
/* The full check of [p + o, p + o + width) against region r's frame words. */
static void bounds(Asm *a, uint8_t region, uint8_t p, int32_t o, unsigned width) {
    mov64(a, S0, p);
    op_rm(a, 0x2B, S0, RDI, AT_BASE(region));
    add_offset(a, o);
    load(a, 8, S1, RDI, AT_LEN(region));
    op_imm8(a, 5, true, S1, (uint8_t)width);
    jcc(a, CC_B, L_INVALID);
    op_rr(a, 0x39, true, S0, S1);
    jcc(a, CC_A, L_INVALID);
}
static uint8_t condition(TcIsaCmpCond c) {
    static const uint8_t cc[] = {[TISA_CMP_EQ] = CC_E, [TISA_CMP_NE] = CC_NE, [TISA_CMP_ULT] = CC_B,
                                 [TISA_CMP_ULE] = CC_BE, [TISA_CMP_SLT] = CC_L, [TISA_CMP_SLE] = CC_LE};
    return cc[c];
}

/* ---- Liveness ------------------------------------------------------------- */

static uint16_t bit(uint8_t r) { return r < TISA_NUM_VREGS ? (uint16_t)(1u << r) : 0; }
static uint16_t uses(const TcIsaInstr *i) {
    switch (i->op) {
    case TISA_OP_IMM: return 0;
    case TISA_OP_MOV: case TISA_OP_SHIFT: case TISA_OP_EXTRACT: return bit(i->src1);
    case TISA_OP_BIN: return bit(i->src1) | bit(i->src2);
    case TISA_OP_CMP: return bit(i->src1) | (i->bit_width ? 0 : bit(i->src2));
    case TISA_OP_SELECT: return bit(i->src1) | bit(i->src2) | bit(i->src3);
    case TISA_OP_ADDR: return bit(i->src1) | (i->src2 != 0xFFu ? bit(i->src2) : 0);
    case TISA_OP_LOAD: return i->region_id == TISA_REGION_FRAME ? 0 : bit(i->src1);
    case TISA_OP_STORE: return bit(i->src1) | bit(i->src2);
    }
    return 0xFFFF;
}
static uint16_t defs(const TcIsaInstr *i) { return i->op == TISA_OP_STORE ? 0 : bit(i->dst); }

static Live *live_slot(LiveMap *m, const TcIsaStmt *s) {
    size_t h = ((uintptr_t)s >> 4) * 0x9E3779B97F4A7C15ull >> 7;
    for (size_t k = 0; k < m->cap; ++k) {
        Live *l = &m->t[(h + k) & (m->cap - 1)];
        if (l->s == s || !l->s) { l->s = s; return l; }
    }
    return NULL;
}
static const Live *live_find(const LiveMap *m, const TcIsaStmt *s) {
    size_t h = ((uintptr_t)s >> 4) * 0x9E3779B97F4A7C15ull >> 7;
    for (size_t k = 0; k < m->cap; ++k) {
        const Live *l = &m->t[(h + k) & (m->cap - 1)];
        if (l->s == s) return l;
        if (!l->s) return NULL;
    }
    return NULL;
}
static size_t count_ifs(const TcIsaStmt *s) {
    if (!s) return 0;
    switch (s->kind) {
    case TISA_CTRL_SEQ: return count_ifs(s->u.seq.first) + count_ifs(s->u.seq.second);
    case TISA_CTRL_IF: return 1 + count_ifs(s->u.if_cond.then_branch) + count_ifs(s->u.if_cond.else_branch);
    case TISA_CTRL_COUNT_LOOP: return count_ifs(s->u.count_loop.body);
    case TISA_CTRL_EARLY_EXIT_LOOP: return count_ifs(s->u.early_exit_loop.body);
    default: return 0;
    }
}
/* The registers read before written when s runs and then code reading `out`. */
static uint16_t live_in(LiveMap *m, const TcIsaStmt *s, uint16_t out) {
    if (!s) return out;
    switch (s->kind) {
    case TISA_CTRL_INSTR: return (uint16_t)((out & ~defs(&s->u.instr)) | uses(&s->u.instr));
    case TISA_CTRL_SEQ: return live_in(m, s->u.seq.first, live_in(m, s->u.seq.second, out));
    case TISA_CTRL_IF: {
        uint16_t t = live_in(m, s->u.if_cond.then_branch, out), e = live_in(m, s->u.if_cond.else_branch, out);
        Live *l = live_slot(m, s);
        if (l) { l->then_in = t; l->else_in = e; }
        return (uint16_t)(t | e | bit(s->u.if_cond.cond_reg));
    }
    case TISA_CTRL_COUNT_LOOP:
    case TISA_CTRL_EARLY_EXIT_LOOP: {
        bool early = s->kind == TISA_CTRL_EARLY_EXIT_LOOP;
        uint8_t x = early ? s->u.early_exit_loop.idx_reg : s->u.count_loop.idx_reg;
        uint8_t n = early ? s->u.early_exit_loop.bound_reg : s->u.count_loop.bound_reg;
        uint16_t k = early ? bit(s->u.early_exit_loop.cont_reg) : 0;
        const TcIsaStmt *body = early ? s->u.early_exit_loop.body : s->u.count_loop.body;
        uint16_t head = (uint16_t)(out | bit(x) | bit(n) | k);
        for (;;) {   /* the head reads what the exit and the next iteration read */
            uint16_t next = (uint16_t)(out | bit(x) | bit(n) | k | live_in(m, body, head));
            if (next == head) break;
            head = next;
        }
        return (uint16_t)((head & ~bit(x)) | bit(n) | k);
    }
    case TISA_CTRL_RETURN: return bit(s->u.ret.ret_reg);
    case TISA_CTRL_FUND: return out;
    }
    return 0xFFFF;
}

/* ---- Statement lists -------------------------------------------------------- */

typedef struct { const TcIsaStmt **s; size_t n, cap; bool failed; } Items;
static void flatten(Items *it, const TcIsaStmt *s) {
    if (!s) return;
    if (s->kind == TISA_CTRL_SEQ) { flatten(it, s->u.seq.first); flatten(it, s->u.seq.second); return; }
    it->s = grow(it->s, &it->cap, it->n + 1, sizeof *it->s, &it->failed);
    if (!it->failed) it->s[it->n++] = s;
}

/* The bytes, from register d (region r) on, that the loads and stores through d
 * use after item i and before the next side effect or a write of d. */
static uint64_t window_extent(const Items *it, size_t i, uint8_t d, uint8_t r) {
    uint64_t e = 0;
    for (size_t j = i + 1; j < it->n; ++j) {
        const TcIsaStmt *s = it->s[j];
        if (s->kind != TISA_CTRL_INSTR) break;
        const TcIsaInstr *x = &s->u.instr;
        if ((x->op == TISA_OP_LOAD || x->op == TISA_OP_STORE) && x->region_id == r && x->src1 == d && x->disp32 >= 0) {
            uint64_t need = (uint64_t)x->disp32 + x->subop;
            if (need > e) e = need;
        }
        if (x->op == TISA_OP_STORE || x->dst == d) break;
    }
    return e;
}

/* ---- Facts ------------------------------------------------------------------ */

static void forget(Env *e, uint8_t r) { if (r < TISA_NUM_VREGS) { e->reg[r] = (Fact){0}; e->bits[r] = 64; } }
static void join(Env *a, const Env *b) {
    if (b->dead) return;
    if (a->dead) { *a = *b; return; }
    for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) {
        Fact *x = &a->reg[r]; const Fact *y = &b->reg[r];
        if (x->kind != y->kind || x->region != y->region || (x->kind == F_BASE && x->c != y->c)) *x = (Fact){0};
        else if (x->kind == F_ROOM && y->room < x->room) x->room = y->room;
    }
    for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) if (b->bits[r] > a->bits[r]) a->bits[r] = b->bits[r];
    for (unsigned q = 0; q < TISA_MAX_REGIONS; ++q) if (b->len[q] < a->len[q]) a->len[q] = b->len[q];
}

/* Widths. The value an instruction writes is below 2^bits, by its operands'
 * widths and the normalization its type makes. */
static uint8_t bitlen(uint64_t k) { return k ? (uint8_t)(64 - __builtin_clzll(k)) : 0; }
static uint8_t at_most(unsigned b) { return b < 64 ? (uint8_t)b : 64; }
static uint8_t typed_bits(TcIsaValType t, uint8_t b) {
    if (t == TISA_TYPE_BOOL) return 1;
    if (t == TISA_TYPE_U32Z) return b < 32 ? b : 32;
    if (t == TISA_TYPE_I32S) return b <= 31 ? b : 64;
    if (t == TISA_TYPE_PTR) return 64;
    return b;
}
static uint8_t result_bits(const Env *e, const TcIsaInstr *i) {
    uint8_t x = i->src1 < TISA_NUM_VREGS ? e->bits[i->src1] : 64, y = i->src2 < TISA_NUM_VREGS ? e->bits[i->src2] : 64;
    bool narrow = i->ty == TISA_TYPE_U32Z || i->ty == TISA_TYPE_I32S;
    unsigned n = i->bit_width, b = 64;
    switch (i->op) {
    case TISA_OP_IMM: {
        uint64_t k = i->imm64;
        if (i->ty == TISA_TYPE_U32Z) k = (uint32_t)k;
        else if (i->ty == TISA_TYPE_I32S) k = (uint64_t)(int64_t)(int32_t)(uint32_t)k;
        b = bitlen(k); break;
    }
    case TISA_OP_MOV: b = x; break;
    case TISA_OP_BIN:
        switch (i->subop) {
        case TISA_BIN_ADD: b = at_most((x > y ? x : y) + 1u); break;
        case TISA_BIN_AND: b = x < y ? x : y; break;
        case TISA_BIN_OR: case TISA_BIN_XOR: b = x > y ? x : y; break;
        case TISA_BIN_MULLO: b = at_most((unsigned)x + y); break;
        default: b = 64; break;
        }
        break;
    case TISA_OP_SHIFT: {
        unsigned w = narrow && x > 32 ? 32 : x;
        if (i->subop == TISA_SHIFT_SHL) b = at_most(x + n);
        else if (i->subop == TISA_SHIFT_SHRU) b = w > n ? w - n : 0;
        else b = x <= (narrow ? 31u : 63u) ? (x > n ? x - n : 0) : 64;
        break;
    }
    case TISA_OP_EXTRACT: b = n ? n : 64; break;
    case TISA_OP_CMP: b = 1; break;
    case TISA_OP_SELECT: b = x > y ? x : y; break;
    case TISA_OP_LOAD: b = i->region_id == TISA_REGION_FRAME ? 64 : i->subop == 1 ? 8 : i->subop == 4 ? 32 : 64; break;
    default: b = 64; break;
    }
    return typed_bits(i->ty, (uint8_t)b);
}
/* 2: the access [p + o, p + o + w) lies in region r by the facts; 1: it does
 * once r's length is at least *need; 0: it needs the full check. */
static int placed(const Env *e, uint8_t p, uint8_t r, int32_t o, unsigned w, uint64_t *need) {
    if (p >= TISA_NUM_VREGS || r >= TISA_MAX_REGIONS) return 0;
    const Fact *f = &e->reg[p];
    if (f->kind == F_NONE || f->region != r) return 0;
    if (f->kind == F_BASE) {
        int64_t lo = f->c + o;
        if (lo < 0) return 0;
        uint64_t hi = (uint64_t)lo + w;
        if (hi <= e->len[r]) return 2;
        if (hi <= INT32_MAX) { *need = hi; return 1; }
        return 0;
    }
    return o >= 0 && (uint64_t)o + w <= f->room ? 2 : 0;
}
static void length_test(Asm *a, Env *e, uint8_t r, uint64_t need) {
    cmp_mem_imm32(a, RDI, AT_LEN(r), (uint32_t)need);
    jcc(a, CC_B, L_INVALID);
    if (need > e->len[r]) e->len[r] = need;
}

/* ---- Loops: checks decided at entry ------------------------------------------- */

enum { BASE_FRAME = 0xFE, REGION_ANY = 0xFF, MAX_HOISTED = 64, MAX_RANGES = 16 };

typedef struct {        /* the pointer b + k*x + c in a region; b invariant, or BASE_FRAME for q = 0 */
    bool     known;
    uint8_t  region, base;
    uint32_t k;
    int64_t  c;
} Affine;

typedef struct {        /* q + k*x + c' lies in [0, len] for every c' in [lo, hi] */
    uint8_t  region, base;
    uint32_t k;
    int64_t  lo, hi;
} Range;

struct Hoist {
    uint8_t           x;                /* the loop index */
    uint16_t          written;          /* registers the body writes */
    const TcIsaInstr *hoisted[MAX_HOISTED]; unsigned nhoisted;
    unsigned          indexed;          /* of those, accesses whose pointer depends on x */
    Range             ranges[MAX_RANGES];   unsigned nranges;
    uint64_t          fund;             /* FUND units per iteration, at most */
    unsigned          kept;             /* region accesses left checked */
    const char       *reason;           /* why the loop has no unchecked copy */
};

static bool fund_hoisted(const Hoist *h) { return h->fund > 0 && h->fund <= UINT32_MAX; }

static void written(const TcIsaStmt *s, uint16_t *w) {
    if (!s) return;
    switch (s->kind) {
    case TISA_CTRL_INSTR: *w |= defs(&s->u.instr); return;
    case TISA_CTRL_SEQ: written(s->u.seq.first, w); written(s->u.seq.second, w); return;
    case TISA_CTRL_IF: written(s->u.if_cond.then_branch, w); written(s->u.if_cond.else_branch, w); return;
    case TISA_CTRL_COUNT_LOOP: *w |= bit(s->u.count_loop.idx_reg); written(s->u.count_loop.body, w); return;
    case TISA_CTRL_EARLY_EXIT_LOOP: *w |= bit(s->u.early_exit_loop.idx_reg); written(s->u.early_exit_loop.body, w); return;
    default: return;
    }
}

static bool invariant(const Hoist *h, uint8_t r) {
    return r != TISA_V0 && r != h->x && r < TISA_NUM_VREGS && !(h->written & (1u << r));
}

/* The pointer register p as an affine value in region r, or unknown. */
static Affine pointer(const Hoist *h, const Affine *env, uint8_t p, uint8_t r) {
    Affine none = {0};
    if (p >= TISA_NUM_VREGS) return none;
    if (env[p].known) {
        Affine v = env[p];
        if (v.region == REGION_ANY) v.region = r;
        return v.region == r ? v : none;
    }
    if (invariant(h, p)) return (Affine){.known = true, .region = r, .base = p};
    return none;
}

static bool same_affine(const Affine *u, const Affine *v) {
    return u->known == v->known && (!u->known || (u->region == v->region && u->base == v->base && u->k == v->k && u->c == v->c));
}

/* Records access i as checked at entry, for partial sums v + c' with c' in [lo, hi]. */
static bool hoist_access(Hoist *h, const TcIsaInstr *i, const Affine *v, int64_t lo, int64_t hi) {
    const int64_t far = INT64_C(1) << 40;
    if (!v->known || v->k > 65536 || lo < -far || hi > far || h->nhoisted == MAX_HOISTED) { ++h->kept; return false; }
    unsigned k = 0;
    while (k < h->nranges && !(h->ranges[k].region == v->region && h->ranges[k].base == v->base && h->ranges[k].k == v->k)) ++k;
    if (k == h->nranges) {
        if (k == MAX_RANGES) { ++h->kept; return false; }
        h->ranges[h->nranges++] = (Range){v->region, v->base, v->k, lo, hi};
    } else {
        if (lo < h->ranges[k].lo) h->ranges[k].lo = lo;
        if (hi > h->ranges[k].hi) h->ranges[k].hi = hi;
    }
    h->hoisted[h->nhoisted++] = i;
    h->indexed += v->k > 0;
    return true;
}

static void span3(int64_t a, int64_t b, int64_t c, int64_t *lo, int64_t *hi) {
    *lo = a < b ? a : b; if (c < *lo) *lo = c;
    *hi = a > b ? a : b; if (c > *hi) *hi = c;
}

/* Walks the body in execution order; env holds what this iteration has
 * computed so far (registers the body writes start unknown). */
static void hoist_walk(Hoist *h, const TcIsaStmt *s, Affine *env) {
    if (!s || h->reason) return;
    switch (s->kind) {
    case TISA_CTRL_INSTR: {
        const TcIsaInstr *i = &s->u.instr;
        Affine out = {0};
        int64_t lo, hi;
        switch (i->op) {
        case TISA_OP_ADDR: {
            Affine v = pointer(h, env, i->src1, i->region_id);
            if (v.known && (i->src2 == 0xFFu || i->src2 == h->x)) {
                if (i->src2 != 0xFFu) v.k += 1u << i->scale_lg2;
                span3(v.c, v.c, v.c + i->disp32, &lo, &hi);
                if (hoist_access(h, i, &v, lo, hi)) { out = v; out.c += i->disp32; }
            } else ++h->kept;
            break;
        }
        case TISA_OP_LOAD:
            if (i->region_id == TISA_REGION_FRAME) {
                if (i->ty == TISA_TYPE_PTR && i->disp32 >= 0 && i->disp32 % 16 == 0 && i->disp32 / 16 < 9)
                    out = (Affine){.known = true, .region = (uint8_t)(i->disp32 / 16), .base = BASE_FRAME};
                break;
            }
            /* fall through */
        case TISA_OP_STORE: {
            Affine v = pointer(h, env, i->src1, i->region_id);
            if (v.known) {
                span3(v.c, v.c + i->disp32, v.c + i->disp32 + i->subop, &lo, &hi);
                hoist_access(h, i, &v, lo, hi);
            } else ++h->kept;
            break;
        }
        case TISA_OP_MOV:
            if (i->ty == TISA_TYPE_PTR && i->src1 < TISA_NUM_VREGS) {
                if (env[i->src1].known) out = env[i->src1];
                else if (invariant(h, i->src1)) out = (Affine){.known = true, .region = REGION_ANY, .base = i->src1};
            }
            break;
        default: break;
        }
        if (i->op != TISA_OP_STORE && i->dst < TISA_NUM_VREGS) env[i->dst] = out;
        return;
    }
    case TISA_CTRL_SEQ: hoist_walk(h, s->u.seq.first, env); hoist_walk(h, s->u.seq.second, env); return;
    case TISA_CTRL_IF: {
        Affine other[TISA_NUM_VREGS];
        memcpy(other, env, sizeof other);
        hoist_walk(h, s->u.if_cond.then_branch, env);
        hoist_walk(h, s->u.if_cond.else_branch, other);
        for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) if (!same_affine(&env[r], &other[r])) env[r] = (Affine){0};
        return;
    }
    case TISA_CTRL_COUNT_LOOP:
    case TISA_CTRL_EARLY_EXIT_LOOP: h->reason = "a loop in the body"; return;
    case TISA_CTRL_FUND: h->fund += s->u.fund.units; return;
    default: return;
    }
}

static size_t statements(const TcIsaStmt *s) {
    if (!s) return 0;
    switch (s->kind) {
    case TISA_CTRL_SEQ: return statements(s->u.seq.first) + statements(s->u.seq.second);
    case TISA_CTRL_IF: return 1 + statements(s->u.if_cond.then_branch) + statements(s->u.if_cond.else_branch);
    case TISA_CTRL_COUNT_LOOP: return 1 + statements(s->u.count_loop.body);
    case TISA_CTRL_EARLY_EXIT_LOOP: return 1 + statements(s->u.early_exit_loop.body);
    default: return 1;
    }
}

static bool is_hoisted(const Asm *a, const TcIsaInstr *i) {
    if (!a->hoist) return false;
    for (unsigned k = 0; k < a->hoist->nhoisted; ++k) if (a->hoist->hoisted[k] == i) return true;
    return false;
}

/* The entry test, run with 0 < n: jumps to fail (the checked loop, x = 0)
 * unless every entry-decided check passes for every x in [0, n - 1] and the
 * allowance covers F * n. */
static void hoist_test(Asm *a, const Hoist *h, uint8_t n, size_t fail) {
    for (unsigned k = 0; k < h->nranges; ++k) {
        const Range *r = &h->ranges[k];
        if (r->base == BASE_FRAME) op_rr(a, 0x31, false, S0, S0);                 /* q = 0 */
        else { mov64(a, S0, HW[r->base]); op_rm(a, 0x2B, S0, RDI, AT_BASE(r->region)); }  /* q = b - base */
        if (r->lo < 0) { mov_imm(a, S1, (uint64_t)-r->lo); op_rr(a, 0x39, true, S0, S1); jcc(a, CC_B, fail); }
        if (r->k) {                                                              /* q + k * (n - 1) */
            mov64(a, S1, n);
            op_imm8(a, 5, true, S1, 1);
            if (r->k & (r->k - 1)) imul_imm(a, S1, S1, r->k);
            else if (r->k > 1) shift(a, 4, true, S1, (uint8_t)__builtin_ctz(r->k));
            op_rr(a, 0x01, true, S0, S1);
            jcc(a, CC_B, fail);
        }
        if (r->hi > 0) { mov_imm(a, S1, (uint64_t)r->hi); op_rr(a, 0x01, true, S0, S1); jcc(a, CC_B, fail); }
        else if (r->hi < 0) { mov_imm(a, S1, (uint64_t)-r->hi); op_rr(a, 0x29, true, S0, S1); jcc(a, CC_B, fail); }
        op_rm(a, 0x3B, S0, RDI, AT_LEN(r->region));                              /* above len */
        jcc(a, CC_A, fail);
    }
    if (fund_hoisted(h)) {
        mov64(a, S0, n);
        mov_imm(a, S1, h->fund);
        imul(a, S0, S1);                                                         /* F * n < 2^64 */
        op_rr(a, 0x39, true, RBP, S0);                                           /* allowance below F * n */
        jcc(a, CC_B, fail);
    }
}

/* ---- Statements ---------------------------------------------------------------- */

static void block(Asm *a, Env *e, const TcIsaStmt *s);

/* The compare of a CMP; with keep, its Bool too. Returns the condition. */
static uint8_t compare(Asm *a, const TcIsaInstr *i, bool keep) {
    uint8_t d = i->dst < TISA_TOTAL_REGS ? HW[i->dst] : 0;
    uint8_t x = i->src1 < TISA_TOTAL_REGS ? HW[i->src1] : 0;
    uint8_t y = i->src2 < TISA_TOTAL_REGS ? HW[i->src2] : 0;
    bool narrow = i->ty == TISA_TYPE_U32Z || i->ty == TISA_TYPE_I32S;
    if (i->bit_width != 0) {
        if (narrow) op_imm32(a, 7, false, x, (uint32_t)i->imm64);
        else if ((uint64_t)(int64_t)(int32_t)i->imm64 == i->imm64) op_imm32(a, 7, true, x, (uint32_t)i->imm64);
        else { mov_imm(a, S0, i->imm64); op_rr(a, 0x39, true, x, S0); }
    } else op_rr(a, 0x39, !narrow, x, y);
    uint8_t cc = condition((TcIsaCmpCond)i->subop);
    if (keep) { setcc(a, cc, d); movzx8(a, d, d); }
    return cc;
}

static const char *instruction(Asm *a, Env *e, const Items *it, size_t at) {
    const TcIsaInstr *i = &it->s[at]->u.instr;
    uint8_t d = i->dst < TISA_TOTAL_REGS ? HW[i->dst] : 0;
    uint8_t x = i->src1 < TISA_TOTAL_REGS ? HW[i->src1] : 0;
    uint8_t y = i->src2 < TISA_TOTAL_REGS ? HW[i->src2] : 0;
    bool narrow = i->ty == TISA_TYPE_U32Z || i->ty == TISA_TYPE_I32S;
    Fact out = {0};
    uint8_t width = i->op == TISA_OP_STORE ? 64 : result_bits(e, i);
    const char *name = "?";
    switch (i->op) {
    case TISA_OP_IMM: {
        uint64_t k = i->imm64;
        if (i->ty == TISA_TYPE_U32Z) k = (uint32_t)k;
        else if (i->ty == TISA_TYPE_I32S) k = (uint64_t)(int64_t)(int32_t)(uint32_t)k;
        mov_imm(a, d, k);
        name = "IMM"; break;
    }
    case TISA_OP_MOV:
        mov64(a, d, x);
        normalize(a, i->ty, d);
        if (i->ty == TISA_TYPE_PTR && i->src1 < TISA_NUM_VREGS) out = e->reg[i->src1];
        name = "MOV"; break;
    case TISA_OP_BIN: {
        static const uint8_t ops[] = {[TISA_BIN_ADD] = 0x01, [TISA_BIN_SUB] = 0x29, [TISA_BIN_AND] = 0x21,
                                      [TISA_BIN_OR] = 0x09, [TISA_BIN_XOR] = 0x31};
        bool commutes = i->subop != TISA_BIN_SUB;
        uint8_t into = d, other = y;
        if (d == x) other = y;
        else if (d == y && commutes) other = x;
        else if (d == y) { mov64(a, S0, x); into = S0; }
        else mov64(a, d, x);
        if (i->subop == TISA_BIN_MULLO) imul(a, into, other);
        else op_rr(a, ops[i->subop], true, into, other);
        mov64(a, d, into);
        normalize(a, i->ty, d);
        name = "BIN"; break;
    }
    case TISA_OP_SHIFT: {
        static const uint8_t ext[] = {[TISA_SHIFT_SHL] = 4, [TISA_SHIFT_SHRU] = 5, [TISA_SHIFT_SHRS] = 7};
        mov64(a, d, x);
        shift(a, ext[i->subop], !narrow, d, i->bit_width);
        normalize(a, i->ty, d);
        name = "SHIFT"; break;
    }
    case TISA_OP_EXTRACT: {
        unsigned w = i->bit_width;
        mov64(a, d, x);
        if (i->scale_lg2) shift(a, 5, true, d, i->scale_lg2);
        if (w <= 31) op_imm32(a, 4, false, d, (uint32_t)((UINT64_C(1) << w) - 1));
        else if (w == 32) mov32(a, d, d);
        else if (w < 64) { shift(a, 4, true, d, (uint8_t)(64 - w)); shift(a, 5, true, d, (uint8_t)(64 - w)); }
        normalize(a, i->ty, d);
        name = "EXTRACT"; break;
    }
    case TISA_OP_CMP:
        (void)compare(a, i, true);
        name = "CMP"; break;
    case TISA_OP_SELECT:
        mov64(a, S0, y);
        op_rr(a, 0x85, true, HW[i->src3], HW[i->src3]);
        cmov(a, CC_NE, S0, x);
        mov64(a, d, S0);
        normalize(a, i->ty, d);
        name = "SELECT"; break;
    case TISA_OP_ADDR: {
        uint8_t r = i->region_id;
        const Fact f = i->src1 < TISA_NUM_VREGS ? e->reg[i->src1] : (Fact){0};
        bool indexed = i->src2 != 0xFFu;
        if (is_hoisted(a, i)) {                    /* decided at loop entry */
            lea(a, d, x, indexed ? (int)y : -1, indexed ? i->scale_lg2 : 0, i->disp32);
            name = "ADDR-DIRECT"; break;
        }
        if (f.kind == F_BASE && f.region == r && r < TISA_MAX_REGIONS && !indexed && f.c + i->disp32 >= 0 &&
            f.c + i->disp32 <= INT32_MAX) {
            int64_t q = f.c + i->disp32;           /* the result is base + q: valid iff q <= len */
            bool test = (uint64_t)q > e->len[r];
            if (test) length_test(a, e, r, (uint64_t)q);
            lea(a, d, x, -1, 0, i->disp32);
            out = (Fact){.kind = F_BASE, .region = r, .c = q};
            name = test ? "ADDR-TEST" : "ADDR-DIRECT"; break;
        }
        if (f.kind == F_BASE && f.region == r && r < TISA_MAX_REGIONS && indexed && f.c + i->disp32 >= 0) {
            /* base + c + (y << s) + disp, checked with the extent its accesses use: valid iff
               (y << s) does not overflow and (y << s) + K <= len, K = max(c, c + disp + extent) */
            uint64_t ext = window_extent(it, at, i->dst, r);
            int64_t k = f.c + i->disp32 + (int64_t)ext;
            if (k < f.c) k = f.c;
            if (ext <= INT32_MAX && k <= INT32_MAX) {
                if (e->bits[i->src2] + i->scale_lg2 <= 62) {   /* (y << s) + K cannot overflow */
                    if (k == 0 && !i->scale_lg2) op_rm(a, 0x3B, y, RDI, AT_LEN(r));
                    else { lea_index(a, S1, y, i->scale_lg2, (int32_t)k); op_rm(a, 0x3B, S1, RDI, AT_LEN(r)); }
                } else {
                    if (i->scale_lg2) { mov64(a, S1, y); shift(a, 5, true, S1, (uint8_t)(64 - i->scale_lg2)); jcc(a, CC_NE, L_INVALID); }
                    mov64(a, S1, y);
                    if (i->scale_lg2) shift(a, 4, true, S1, i->scale_lg2);
                    if (k > 0) { op_imm32(a, 0, true, S1, (uint32_t)k); jcc(a, CC_B, L_INVALID); }
                    op_rm(a, 0x3B, S1, RDI, AT_LEN(r));
                }
                jcc(a, CC_A, L_INVALID);
                lea(a, d, x, (int)y, i->scale_lg2, i->disp32);
                out = (Fact){.kind = F_ROOM, .region = r, .room = ext};
                name = "ADDR-INDEX"; break;
            }
        }
        if (f.kind == F_ROOM && f.region == r && !indexed && i->disp32 >= 0 && (uint64_t)i->disp32 <= f.room) {
            lea(a, d, x, -1, 0, i->disp32);
            out = (Fact){.kind = F_ROOM, .region = r, .room = f.room - (uint64_t)i->disp32};
            name = "ADDR-DIRECT"; break;
        }
        mov64(a, S0, x);                                   /* the full check */
        op_rm(a, 0x2B, S0, RDI, AT_BASE(r));
        if (indexed) {
            if (i->scale_lg2) { mov64(a, S1, y); shift(a, 5, true, S1, (uint8_t)(64 - i->scale_lg2)); jcc(a, CC_NE, L_INVALID); }
            mov64(a, S1, y);
            if (i->scale_lg2) shift(a, 4, true, S1, i->scale_lg2);
            op_rr(a, 0x01, true, S0, S1);
            jcc(a, CC_B, L_INVALID);
            op_rm(a, 0x3B, S0, RDI, AT_LEN(r));
            jcc(a, CC_A, L_INVALID);
        }
        if (i->disp32) {
            add_offset(a, i->disp32);
            if (i->disp32 > 0) { op_rm(a, 0x3B, S0, RDI, AT_LEN(r)); jcc(a, CC_A, L_INVALID); }
        }
        op_rm(a, 0x03, S0, RDI, AT_BASE(r));
        mov64(a, d, S0);
        name = "ADDR"; break;
    }
    case TISA_OP_LOAD: {
        if (i->region_id == TISA_REGION_FRAME) {
            if (i->disp32 == AT_USED) { load(a, 8, d, RDI, AT_BUDGET); op_rr(a, 0x29, true, d, RBP); }
            else load(a, 8, d, RDI, i->disp32);
            if (i->ty == TISA_TYPE_PTR && i->disp32 >= 0 && i->disp32 % 16 == 0 &&
                (unsigned)(i->disp32 / 16) < a->prog->nregions)
                out = (Fact){.kind = F_BASE, .region = (uint8_t)(i->disp32 / 16), .c = 0};
            name = "LOAD-FRAME";
        } else {
            uint64_t need = 0;
            int how = is_hoisted(a, i) ? 2 : placed(e, i->src1, i->region_id, i->disp32, i->subop, &need);
            if (how == 1) length_test(a, e, i->region_id, need);
            else if (how == 0) bounds(a, i->region_id, x, i->disp32, i->subop);
            load(a, i->subop, d, x, i->disp32);
            name = how == 2 ? "LOAD-DIRECT" : how == 1 ? "LOAD-TEST" : "LOAD";
        }
        if (i->ty == TISA_TYPE_BOOL) { op_imm8(a, 7, true, d, 1); jcc(a, CC_A, L_INVALID); }
        if (i->ty == TISA_TYPE_U32Z && (i->subop == 8 || i->region_id == TISA_REGION_FRAME)) mov32(a, d, d);
        if (i->ty == TISA_TYPE_I32S && i->subop != 1) movsxd(a, d, d);
        break;
    }
    case TISA_OP_STORE: {
        uint64_t need = 0;
        int how = is_hoisted(a, i) ? 2 : placed(e, i->src1, i->region_id, i->disp32, i->subop, &need);
        if (how == 1) length_test(a, e, i->region_id, need);
        else if (how == 0) bounds(a, i->region_id, x, i->disp32, i->subop);
        store(a, i->subop, y, x, i->disp32);
        return how == 2 ? "STORE-DIRECT" : how == 1 ? "STORE-TEST" : "STORE";
    }
    default:
        a->failed = true;
        return "?";
    }
    if (i->dst < TISA_NUM_VREGS) { e->reg[i->dst] = out; e->bits[i->dst] = width; }
    return name;
}

/* An IF; fused >= 0 is the condition of the flags the preceding compare set. */
static void conditional(Asm *a, Env *e, const TcIsaStmt *s, int fused) {
    size_t from = a->len, no = new_label(a);
    if (fused < 0) {
        uint8_t c = HW[s->u.if_cond.cond_reg];
        op_rr(a, 0x85, true, c, c);
        jcc(a, CC_E, no);
    } else jcc(a, (uint8_t)(fused ^ 1), no);
    span(a, from, fused < 0 ? "IF" : "IF-FLAGS", s);
    Env other = *e;
    block(a, e, s->u.if_cond.then_branch);
    if (s->u.if_cond.else_branch) {
        size_t end = new_label(a);
        if (!e->dead) { size_t at = a->len; jmp(a, end); span(a, at, "IF-ELSE", s); }
        place(a, no);
        block(a, &other, s->u.if_cond.else_branch);
        place(a, end);
    } else place(a, no);
    join(e, &other);
}

static size_t loop_record(Asm *a, const TcIsaStmt *s) {
    a->loops = grow(a->loops, &a->loop_cap, a->nloops + 1, sizeof *a->loops, &a->failed);
    if (a->failed) return 0;
    a->loops[a->nloops] = (TcIsaX86Loop){.loop = s};
    return a->nloops++;
}

/* The latch of a copy of the loop: (k and) x + 1 < n runs the body again. */
static void latch(Asm *a, const TcIsaStmt *s, bool early, uint8_t x, uint8_t n, uint8_t k, size_t top, size_t end,
                  const char *name) {
    size_t from = a->len;
    if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
    op_imm8(a, 0, true, x, 1);
    op_rr(a, 0x39, true, x, n);
    jcc(a, CC_B, top);
    span(a, from, name, s);
}

/* A loop: n > cap is UNSUPPORTED; entered when 0 < n (and k). With checks
 * decided at entry, the entry test chooses the unchecked copy or the checked
 * one; each runs the body and tests at the bottom. */
static void loop(Asm *a, Env *e, const TcIsaStmt *s) {
    bool early = s->kind == TISA_CTRL_EARLY_EXIT_LOOP;
    uint8_t xv = early ? s->u.early_exit_loop.idx_reg : s->u.count_loop.idx_reg;
    uint8_t x = HW[xv];
    uint8_t n = HW[early ? s->u.early_exit_loop.bound_reg : s->u.count_loop.bound_reg];
    uint8_t k = early ? HW[s->u.early_exit_loop.cont_reg] : 0;
    uint32_t cap = early ? s->u.early_exit_loop.max_iters : s->u.count_loop.max_iters;
    const TcIsaStmt *body = early ? s->u.early_exit_loop.body : s->u.count_loop.body;
    size_t rec = loop_record(a, s);
    Hoist h = {.x = xv};
    Affine env[TISA_NUM_VREGS];
    memset(env, 0, sizeof env);
    written(body, &h.written);
    hoist_walk(&h, body, env);
    /* A copy doubles the body's code: it is made when it removes checks that
       depend on x, which the facts cannot decide, or the FUND checks of a
       small body. */
    if (!h.reason && !h.nhoisted && !fund_hoisted(&h)) h.reason = "no affine region access or FUND";
    else if (!h.reason && !h.indexed && (!fund_hoisted(&h) || statements(body) > 32))
        h.reason = "no index-dependent check, and no FUND in a small body";
    if (!a->failed) {
        TcIsaX86Loop *l = &a->loops[rec];
        *l = (TcIsaX86Loop){.loop = s, .hoisted = !h.reason, .reason = h.reason,
                            .hoisted_checks = h.reason ? 0 : h.nhoisted, .kept_checks = h.kept,
                            .fund_units = !h.reason && fund_hoisted(&h) ? h.fund : 0};
        if (!h.reason) {
            l->nranges = h.nranges;
            for (unsigned q = 0; q < h.nranges; ++q) {
                l->ranges[q].region = h.ranges[q].region; l->ranges[q].base = h.ranges[q].base;
                l->ranges[q].k = h.ranges[q].k; l->ranges[q].lo = h.ranges[q].lo; l->ranges[q].hi = h.ranges[q].hi;
            }
        }
    }
    size_t from = a->len, top = new_label(a), end = new_label(a);
    mov_imm(a, S0, cap);
    op_rr(a, 0x39, true, n, S0);            /* n > cap: UNSUPPORTED */
    jcc(a, CC_A, L_UNSUPPORTED);
    op_rr(a, 0x31, false, x, x);            /* x := 0 */
    op_rr(a, 0x39, true, x, n);
    jcc(a, CC_AE, end);
    if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
    span(a, from, early ? "EARLY-LOOP-ENTRY" : "LOOP-ENTRY", s);
    uint16_t w = 0;
    written(s, &w);
    for (unsigned r = 0; r < TISA_NUM_VREGS; ++r) if (w & (1u << r)) forget(e, (uint8_t)r);
    if (!h.reason) {
        size_t test = a->len, fast = new_label(a);
        hoist_test(a, &h, n, top);
        /* The unchecked copy starts on a 32-byte boundary of the image (which
           is mapped page-aligned), so a short loop sits in one fetch window.
           The filler is jumped over, never run; it is made of multi-byte NOPs
           (0F 1F /0, 3 to 5 bytes) so that the whole image decodes. */
        size_t pad = (32 - (a->len + 2) % 32) % 32;
        if (pad < 3) pad += 32;
        put(a, 0xEB); put(a, (uint8_t)pad);
        static const uint8_t nop3[] = {0x0F, 0x1F, 0x00}, nop4[] = {0x0F, 0x1F, 0x40, 0x00},
                             nop5[] = {0x0F, 0x1F, 0x44, 0x00, 0x00};
        size_t left = pad;
        if (left % 3 == 1) { for (unsigned b = 0; b < 4; ++b) put(a, nop4[b]); left -= 4; }
        else if (left % 3 == 2) { for (unsigned b = 0; b < 5; ++b) put(a, nop5[b]); left -= 5; }
        for (; left; left -= 3) for (unsigned b = 0; b < 3; ++b) put(a, nop3[b]);
        span(a, test, "HOIST-TEST", s);
        place(a, fast);
        const Hoist *outer = a->hoist;
        a->hoist = &h;
        Env in = *e;
        block(a, &in, body);
        latch(a, s, early, x, n, k, fast, end, early ? "EARLY-FAST-LATCH" : "FAST-LATCH");
        size_t at = a->len;
        jmp(a, end);
        span(a, at, "FAST-EXIT", s);
        a->hoist = outer;
    }
    place(a, top);
    Env in = *e;
    block(a, &in, body);
    latch(a, s, early, x, n, k, top, end, early ? "EARLY-LOOP-LATCH" : "LOOP-LATCH");
    place(a, end);
}

static void statement(Asm *a, Env *e, const Items *it, size_t at) {
    const TcIsaStmt *s = it->s[at];
    size_t from = a->len;
    switch (s->kind) {
    case TISA_CTRL_INSTR: { const char *name = instruction(a, e, it, at); span(a, from, name, s); return; }
    case TISA_CTRL_SEQ: block(a, e, s); return;
    case TISA_CTRL_IF: conditional(a, e, s, -1); return;
    case TISA_CTRL_COUNT_LOOP:
    case TISA_CTRL_EARLY_EXIT_LOOP: loop(a, e, s); return;
    case TISA_CTRL_RETURN:
        mov64(a, RAX, HW[s->u.ret.ret_reg]);
        store_imm32(a, RDI, AT_STATUS, (uint32_t)s->u.ret.outcome);
        store(a, 8, RAX, RDI, AT_RET_VAL);
        jmp(a, L_EPILOGUE);
        span(a, from, "RETURN", s);
        e->dead = true;
        return;
    case TISA_CTRL_FUND: {
        uint32_t u = s->u.fund.units;
        if (a->hoist && fund_hoisted(a->hoist)) {   /* the allowance was tested at loop entry */
            if (u <= 127) op_imm8(a, 5, true, RBP, (uint8_t)u);
            else if (u <= INT32_MAX) op_imm32(a, 5, true, RBP, u);
            else { mov_imm(a, S1, u); op_rr(a, 0x29, true, RBP, S1); }
            span(a, from, "FUND-H", s);
            return;
        }
        /* EXHAUSTED unless u <= rbp, then pay */
        if (u <= 127) { op_imm8(a, 7, true, RBP, (uint8_t)u); jcc(a, CC_B, L_EXHAUSTED); op_imm8(a, 5, true, RBP, (uint8_t)u); }
        else if (u <= INT32_MAX) { op_imm32(a, 7, true, RBP, u); jcc(a, CC_B, L_EXHAUSTED); op_imm32(a, 5, true, RBP, u); }
        else { mov_imm(a, S1, u); op_rr(a, 0x39, true, RBP, S1); jcc(a, CC_B, L_EXHAUSTED); op_rr(a, 0x29, true, RBP, S1); }
        span(a, from, "FUND", s);
        return;
    }
    }
    a->failed = true;
}

static void block(Asm *a, Env *e, const TcIsaStmt *s) {
    Items it = {0};
    flatten(&it, s);
    if (it.failed) { a->failed = true; free(it.s); return; }
    for (size_t i = 0; i < it.n && !a->failed; ++i) {
        const TcIsaStmt *x = it.s[i];
        if (x->kind == TISA_CTRL_INSTR && x->u.instr.op == TISA_OP_CMP && i + 1 < it.n &&
            it.s[i + 1]->kind == TISA_CTRL_IF && it.s[i + 1]->u.if_cond.cond_reg == x->u.instr.dst) {
            const Live *l = live_find(&a->live, it.s[i + 1]);
            bool keep = !l || ((l->then_in | l->else_in) & bit(x->u.instr.dst));
            size_t from = a->len;
            uint8_t cc = compare(a, &x->u.instr, keep);
            span(a, from, keep ? "CMP" : "CMP-FLAGS", x);
            forget(e, x->u.instr.dst);
            conditional(a, e, it.s[++i], cc);
            continue;
        }
        statement(a, e, &it, i);
    }
    free(it.s);
}

/* ---- The program ------------------------------------------------------------- */

bool tisa_x86_emit(const TcIsaProgram *prog, TcIsaX86 *out, char *err, size_t cap) {
    memset(out, 0, sizeof *out);
    if (!tisa_admit(prog, NULL, err, cap)) return false;
    Asm a = {.prog = prog};
    size_t ifs = count_ifs(prog->root), size = 16;
    while (size < 2 * ifs + 2) size *= 2;
    a.live = (LiveMap){calloc(size, sizeof(Live)), size};
    if (!a.live.t) a.failed = true;
    else (void)live_in(&a.live, prog->root, 0);
    for (unsigned l = 0; l < L_FIRST_FREE; ++l) new_label(&a);
    static const uint8_t saved[] = {RBX, R12, R13, S0, S1, RBP};
    size_t from = a.len;
    for (unsigned r = 0; r < 6; ++r) push(&a, saved[r]);
    load(&a, 8, RBP, RDI, AT_BUDGET);                    /* rbp = allowance - receipt */
    op_rm(&a, 0x2B, RBP, RDI, AT_USED);
    span(&a, from, "PROLOGUE", NULL);
    Env e = {0};
    memset(e.bits, 64, sizeof e.bits);
    for (unsigned r = 0; r < prog->nregions && r < TISA_MAX_REGIONS; ++r)
        e.len[r] = prog->regions[r].mode == TISA_CAP_NONE ? 0 : prog->regions[r].min_byte_len;
    block(&a, &e, prog->root);
    /* Failure handlers: the code in eax becomes status and ret_val. */
    static const struct { unsigned label; uint32_t code; } handlers[] = {
        {L_UNSUPPORTED, TISA_OUTCOME_UNSUPPORTED}, {L_INVALID, TISA_OUTCOME_INVALID_INPUT}, {L_EXHAUSTED, TISA_OUTCOME_EXHAUSTED}};
    from = a.len;
    for (unsigned h = 0; h < 3; ++h) {
        place(&a, handlers[h].label);
        mov_imm(&a, RAX, handlers[h].code);
        if (h < 2) jmp(&a, L_FAIL);
    }
    place(&a, L_FAIL);
    store(&a, 8, RAX, RDI, AT_STATUS);
    store(&a, 8, RAX, RDI, AT_RET_VAL);
    span(&a, from, "FAILURE", NULL);
    from = a.len;
    place(&a, L_EPILOGUE);
    load(&a, 8, S0, RDI, AT_BUDGET);                     /* fuel_used = allowance - rbp */
    op_rr(&a, 0x29, true, S0, RBP);
    store(&a, 8, S0, RDI, AT_USED);
    for (unsigned r = 6; r > 0; --r) pop(&a, saved[r - 1]);
    put(&a, 0xC3);
    span(&a, from, "EPILOGUE", NULL);
    bool far = false;   /* a displacement that rel32 cannot hold */
    for (size_t f = 0; !a.failed && f < a.nfixes; ++f) {
        size_t target = a.labels[a.fixes[f].label];
        if (target == SIZE_MAX) { a.failed = true; break; }
        int64_t rel = (int64_t)target - (int64_t)(a.fixes[f].at + 4);
        if (rel < INT32_MIN || rel > INT32_MAX) { a.failed = far = true; break; }
        for (unsigned i = 0; i < 4; ++i) a.b[a.fixes[f].at + i] = (uint8_t)((uint64_t)rel >> (8 * i));
    }
    free(a.labels); free(a.fixes); free(a.live.t);
    if (a.failed) {
        free(a.b); free(a.spans); free(a.loops);
        if (err && cap)
            snprintf(err, cap, far ? "emission failed (a jump displacement does not fit in 32 bits)"
                                   : "emission failed (out of memory or an unknown statement)");
        return false;
    }
    out->bytes = a.b; out->len = a.len;
    out->spans = a.spans; out->nspans = a.nspans;
    out->loops = a.loops; out->nloops = a.nloops;
    return true;
}

bool tisa_x86_load(TcIsaX86 *x, char *err, size_t cap) {
    long page = sysconf(_SC_PAGESIZE);
    size_t size = (x->len + (size_t)page - 1) / (size_t)page * (size_t)page;
    void *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { if (err && cap) snprintf(err, cap, "cannot map code"); return false; }
    memcpy(code, x->bytes, x->len);
    if (mprotect(code, size, PROT_READ | PROT_EXEC)) {
        munmap(code, size);
        if (err && cap) snprintf(err, cap, "cannot make code executable");
        return false;
    }
    x->code = code; x->mapped = size;
    return true;
}

TcIsaOutcome tisa_x86_run(const TcIsaProgram *prog, const TcIsaX86 *x,
                          TcIsaRuntimeFrame *frame, uint64_t regs[TISA_NUM_VREGS]) {
    if (!tisa_entry_valid(prog, frame, regs)) {
        frame->status = frame->ret_val = TISA_OUTCOME_INVALID_INPUT;
        return TISA_OUTCOME_INVALID_INPUT;
    }
    union { void *code; TcIsaNativeFn fn; } entry = {.code = x->code};
    entry.fn(frame, regs[1], regs[2], regs[3], regs[4], regs[5]);
    return (TcIsaOutcome)frame->status;
}

void tisa_x86_free(TcIsaX86 *x) {
    if (x->code) munmap(x->code, x->mapped);
    free(x->bytes); free(x->spans); free(x->loops);
    memset(x, 0, sizeof *x);
}
