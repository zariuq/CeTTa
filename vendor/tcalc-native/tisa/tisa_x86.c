/*
 * tisa_x86.c -- t-ISA profile p1 to x86-64, by fixed templates.
 *
 * Registers: v0..v11 are rdi rsi rdx rcx r8 r9 r10 r11 rax rbx r12 r13, the
 * scratch registers s0 s1 are r14 r15. v0 holds the frame throughout. The
 * public wrapper saves rbx r12 r13 r14 r15 and returns ret_val in rax.
 *
 * A run that stops on a failure jumps to one of three shared handlers, which
 * write the outcome code to status and ret_val (SEMANTICS.txt, section 7).
 * Bounds are checked against the frame's (base, length) words, each step with
 * one carry or borrow test, so the native decision is the reference's.
 */
#define _DEFAULT_SOURCE
#include "tisa_x86.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- Registers and condition codes ---------------------------------------- */

static const uint8_t HW[TISA_TOTAL_REGS] = {7, 6, 2, 1, 8, 9, 10, 11, 0, 3, 12, 13, 14, 15};
enum { RAX = 0, RBX = 3, RDI = 7, R12 = 12, R13 = 13, S0 = 14, S1 = 15 };
enum { CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_BE = 6, CC_A = 7, CC_L = 12, CC_LE = 14 };

/* Frame words (tisa.h). */
#define AT_BASE(r)  ((int32_t)(16 * (r)))
#define AT_LEN(r)   ((int32_t)(16 * (r) + 8))
#define AT_STATUS   144
#define AT_RET_VAL  152
#define AT_BUDGET   160
#define AT_USED     168

/* ---- The assembler -------------------------------------------------------- */

enum { L_INVALID, L_UNSUPPORTED, L_EXHAUSTED, L_FAIL, L_EPILOGUE, L_FIRST_FREE };

typedef struct { size_t at; size_t label; } Fix;

typedef struct Hoist Hoist;

typedef struct {
    uint8_t      *b;      size_t len, cap;
    TcIsaX86Span *spans;  size_t nspans, span_cap;
    size_t       *labels; size_t nlabels, label_cap;   /* offsets; SIZE_MAX until placed */
    Fix          *fixes;  size_t nfixes, fix_cap;
    TcIsaX86Loop *loops;  size_t nloops, loop_cap;
    bool          hoist_bounds;   /* TcIsaX86Options */
    const Hoist  *hoist;          /* the loop whose fast copy is being emitted, else NULL */
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
    if (!a->failed) a->spans[a->nspans++] = (TcIsaX86Span){(uint32_t)from, (uint32_t)(a->len - from), name, stmt,
                                                            a->hoist != NULL};
}

/* REX: W for 64-bit operands, R/X/B extending reg/index/base; force for the
 * byte registers spl bpl sil dil. */
static void rex(Asm *a, bool w, uint8_t reg, uint8_t base, bool force) {
    uint8_t r = (uint8_t)(0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((base & 8) ? 1 : 0));
    if (r != 0x40 || force) put(a, r);
}
static void modrm_reg(Asm *a, uint8_t reg, uint8_t rm) { put(a, (uint8_t)(0xC0 | (reg & 7) << 3 | (rm & 7))); }
/* [base + disp32]; r12 as a base needs a SIB byte. */
static void modrm_mem(Asm *a, uint8_t reg, uint8_t base, int32_t disp) {
    put(a, (uint8_t)(0x80 | (reg & 7) << 3 | (base & 7)));
    if ((base & 7) == 4) put(a, 0x24);
    put32(a, (uint32_t)disp);
}

/* op rm, reg: add 01, or 09, and 21, sub 29, xor 31, cmp 39, test 85, mov 89 */
static void op_rr(Asm *a, uint8_t op, bool w, uint8_t rm, uint8_t reg) { rex(a, w, reg, rm, false); put(a, op); modrm_reg(a, reg, rm); }
static void mov64(Asm *a, uint8_t d, uint8_t s) { if (d != s) op_rr(a, 0x89, true, d, s); }
static void mov32(Asm *a, uint8_t d, uint8_t s) { op_rr(a, 0x89, false, d, s); }   /* zero-extends */
static void movsxd(Asm *a, uint8_t d, uint8_t s) { rex(a, true, d, s, false); put(a, 0x63); modrm_reg(a, d, s); }
static void imul(Asm *a, uint8_t d, uint8_t s) { rex(a, true, d, s, false); put(a, 0x0F); put(a, 0xAF); modrm_reg(a, d, s); }
static void imul_imm(Asm *a, uint8_t d, uint8_t s, uint32_t k) { rex(a, true, d, s, false); put(a, 0x69); modrm_reg(a, d, s); put32(a, k); }
/* lea d, [base + index * 2^scale + disp]; index < 0 for none. Always with a
 * SIB byte, which also covers r12 as a base; r13 as a base needs a displacement. */
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
/* C1 /ext ib: shl 4, shr 5, sar 7 */
static void shift(Asm *a, uint8_t ext, bool w, uint8_t d, uint8_t n) { rex(a, w, 0, d, false); put(a, 0xC1); modrm_reg(a, ext, d); put(a, n); }
/* 81 /ext id: add 0, and 4, sub 5, cmp 7 (the immediate is sign-extended for 64-bit operands) */
static void op_imm32(Asm *a, uint8_t ext, bool w, uint8_t d, uint32_t k) { rex(a, w, 0, d, false); put(a, 0x81); modrm_reg(a, ext, d); put32(a, k); }
/* 83 /ext ib */
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
/* reg op= [base + disp]: add 03, sub 2B, cmp 3B */
static void op_rm(Asm *a, uint8_t op, uint8_t reg, uint8_t base, int32_t disp) { rex(a, true, reg, base, false); put(a, op); modrm_mem(a, reg, base, disp); }
static void add_to_mem(Asm *a, uint8_t base, int32_t disp, uint8_t s) { rex(a, true, s, base, false); put(a, 0x01); modrm_mem(a, s, base, disp); }
static void store_imm32(Asm *a, uint8_t base, int32_t disp, uint32_t k) { rex(a, true, 0, base, false); put(a, 0xC7); modrm_mem(a, 0, base, disp); put32(a, k); }

static void jcc(Asm *a, uint8_t cc, size_t label) { put(a, 0x0F); put(a, (uint8_t)(0x80 | cc)); fix(a, label); }
static void jmp(Asm *a, size_t label) { put(a, 0xE9); fix(a, label); }

/* ---- Templates ------------------------------------------------------------ */

static void normalize(Asm *a, TcIsaValType t, uint8_t d) {
    if (t == TISA_TYPE_U32Z) mov32(a, d, d);
    else if (t == TISA_TYPE_I32S) movsxd(a, d, d);
}

/* s0 := s0 + o for a signed displacement, INVALID on carry or borrow. */
static void add_offset(Asm *a, int32_t o) {
    if (o > 0) { op_imm32(a, 0, true, S0, (uint32_t)o); jcc(a, CC_B, L_INVALID); }
    else if (o < 0) { mov_imm(a, S1, (uint64_t)-(int64_t)o); op_rr(a, 0x29, true, S0, S1); jcc(a, CC_B, L_INVALID); }
}

/* INVALID unless [p + o, p + o + width) lies in region r. The pointer p is
 * in r (its type says so), so q = p - base(r) is in [0, len(r)]. */
static void bounds(Asm *a, uint8_t region, uint8_t p, int32_t o, unsigned width) {
    mov64(a, S0, p);
    op_rm(a, 0x2B, S0, RDI, AT_BASE(region));    /* s0 = q */
    add_offset(a, o);                            /* s0 = q + o */
    load(a, 8, S1, RDI, AT_LEN(region));         /* s1 = len */
    op_imm8(a, 5, true, S1, (uint8_t)width);     /* s1 = len - width */
    jcc(a, CC_B, L_INVALID);
    op_rr(a, 0x39, true, S0, S1);                /* q + o > len - width */
    jcc(a, CC_A, L_INVALID);
}

static uint8_t condition(TcIsaCmpCond c) {
    static const uint8_t cc[] = {[TISA_CMP_EQ] = CC_E, [TISA_CMP_NE] = CC_NE, [TISA_CMP_ULT] = CC_B,
                                 [TISA_CMP_ULE] = CC_BE, [TISA_CMP_SLT] = CC_L, [TISA_CMP_SLE] = CC_LE};
    return cc[c];
}

/* ---- Bounds hoisting (TcIsaX86Options.hoist_bounds) -------------------------
 *
 * In a counted loop the index x runs from 0 to n - 1, and the body writes
 * neither x nor n (admission). Take a region access whose pointer is
 * b + k*x + c, where the body does not write b and k, c are constants. Every
 * check its template makes is on a partial sum q + k*x + c', where
 * q = b - base(r) lies in [0, len(r)] by b's type: a borrow test (the sum is
 * at least 0), a carry test, or a comparison with the length. Each is
 * monotone in x. One test at loop entry therefore decides every check of every
 * iteration: the lower bound at x = 0 and the upper at x = n - 1. When the
 * test passes, a copy of the loop runs with those checks left out. When it
 * fails, the unchanged template loop runs and decides each check as before.
 * FUND works the same way: the body pays at most F units per iteration, so an
 * allowance of F * n covers every payment of the loop.
 */

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
    Range             ranges[MAX_RANGES];   unsigned nranges;
    uint64_t          fund;             /* FUND units per iteration, at most */
    unsigned          kept;             /* region accesses left checked */
    const char       *reason;           /* why the loop is not hoisted */
};

static bool fund_hoisted(const Hoist *h) { return h->fund > 0 && h->fund <= UINT32_MAX; }

static void written_regs(const TcIsaStmt *s, uint16_t *w) {
    if (!s) return;
    switch (s->kind) {
    case TISA_CTRL_INSTR:
        if (s->u.instr.op != TISA_OP_STORE && s->u.instr.dst < TISA_NUM_VREGS) *w |= (uint16_t)(1u << s->u.instr.dst);
        return;
    case TISA_CTRL_SEQ: written_regs(s->u.seq.first, w); written_regs(s->u.seq.second, w); return;
    case TISA_CTRL_IF: written_regs(s->u.if_cond.then_branch, w); written_regs(s->u.if_cond.else_branch, w); return;
    case TISA_CTRL_COUNT_LOOP: written_regs(s->u.count_loop.body, w); return;
    case TISA_CTRL_EARLY_EXIT_LOOP: written_regs(s->u.early_exit_loop.body, w); return;
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

static bool is_hoisted(const Asm *a, const TcIsaInstr *i) {
    if (!a->hoist) return false;
    for (unsigned k = 0; k < a->hoist->nhoisted; ++k) if (a->hoist->hoisted[k] == i) return true;
    return false;
}

/* The entry test: jumps to fail (the template loop, with x = 0) unless every
 * hoisted check passes for every x in [0, n - 1] and the allowance covers F * n.
 * n = 0 also goes to the template loop, which then exits at once. */
static void hoist_test(Asm *a, const Hoist *h, uint8_t n, size_t fail) {
    op_rr(a, 0x85, true, n, n);
    jcc(a, CC_E, fail);
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
        load(a, 8, S1, RDI, AT_BUDGET);
        op_rm(a, 0x2B, S1, RDI, AT_USED);                                        /* allowance - receipt */
        op_rr(a, 0x39, true, S1, S0);
        jcc(a, CC_B, fail);
    }
}

static size_t loop_record(Asm *a, const TcIsaStmt *s) {
    a->loops = grow(a->loops, &a->loop_cap, a->nloops + 1, sizeof *a->loops, &a->failed);
    if (a->failed) return 0;
    a->loops[a->nloops] = (TcIsaX86Loop){.loop = s};
    return a->nloops++;
}

static const char *instruction(Asm *a, const TcIsaInstr *i) {
    /* Unused fields are ignored (SEMANTICS.txt, section 4): ADDR's missing
       index is 0xFF, and STORE has no destination. */
    uint8_t d = i->dst < TISA_TOTAL_REGS ? HW[i->dst] : 0;
    uint8_t x = i->src1 < TISA_TOTAL_REGS ? HW[i->src1] : 0;
    uint8_t y = i->src2 < TISA_TOTAL_REGS ? HW[i->src2] : 0;
    bool narrow = i->ty == TISA_TYPE_U32Z || i->ty == TISA_TYPE_I32S;
    switch (i->op) {
    case TISA_OP_IMM: {
        uint64_t k = i->imm64;
        if (i->ty == TISA_TYPE_U32Z) k = (uint32_t)k;
        else if (i->ty == TISA_TYPE_I32S) k = (uint64_t)(int64_t)(int32_t)(uint32_t)k;
        mov_imm(a, d, k);
        return "IMM";
    }
    case TISA_OP_MOV:
        mov64(a, d, x);
        normalize(a, i->ty, d);
        return "MOV";
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
        return "BIN";
    }
    case TISA_OP_SHIFT: {
        static const uint8_t ext[] = {[TISA_SHIFT_SHL] = 4, [TISA_SHIFT_SHRU] = 5, [TISA_SHIFT_SHRS] = 7};
        mov64(a, d, x);
        shift(a, ext[i->subop], !narrow, d, i->bit_width);
        normalize(a, i->ty, d);
        return "SHIFT";
    }
    case TISA_OP_EXTRACT: {
        unsigned w = i->bit_width;
        mov64(a, d, x);
        if (i->scale_lg2) shift(a, 5, true, d, i->scale_lg2);
        if (w <= 31) op_imm32(a, 4, false, d, (uint32_t)((UINT64_C(1) << w) - 1));
        else if (w == 32) mov32(a, d, d);
        else if (w < 64) { shift(a, 4, true, d, (uint8_t)(64 - w)); shift(a, 5, true, d, (uint8_t)(64 - w)); }
        normalize(a, i->ty, d);
        return "EXTRACT";
    }
    case TISA_OP_CMP:
        if (i->bit_width != 0) {
            if (narrow) op_imm32(a, 7, false, x, (uint32_t)i->imm64);
            else if ((uint64_t)(int64_t)(int32_t)i->imm64 == i->imm64) op_imm32(a, 7, true, x, (uint32_t)i->imm64);
            else { mov_imm(a, S0, i->imm64); op_rr(a, 0x39, true, x, S0); }
        } else op_rr(a, 0x39, !narrow, x, y);
        setcc(a, condition((TcIsaCmpCond)i->subop), d);
        movzx8(a, d, d);
        return "CMP";
    case TISA_OP_SELECT:
        mov64(a, S0, y);
        op_rr(a, 0x85, true, HW[i->src3], HW[i->src3]);
        cmov(a, CC_NE, S0, x);
        mov64(a, d, S0);
        normalize(a, i->ty, d);
        return "SELECT";
    case TISA_OP_ADDR: {
        uint8_t r = i->region_id;
        if (is_hoisted(a, i)) {                                /* checked at loop entry */
            lea(a, d, x, i->src2 == 0xFFu ? -1 : (int)y, i->scale_lg2, i->disp32);
            return "ADDR-H";
        }
        mov64(a, S0, x);
        op_rm(a, 0x2B, S0, RDI, AT_BASE(r));                 /* s0 = q */
        if (i->src2 != 0xFFu) {
            if (i->scale_lg2) {                               /* the index must not overflow when scaled */
                mov64(a, S1, y);
                shift(a, 5, true, S1, (uint8_t)(64 - i->scale_lg2));
                jcc(a, CC_NE, L_INVALID);
            }
            mov64(a, S1, y);
            if (i->scale_lg2) shift(a, 4, true, S1, i->scale_lg2);
            op_rr(a, 0x01, true, S0, S1);
            jcc(a, CC_B, L_INVALID);
            op_rm(a, 0x3B, S0, RDI, AT_LEN(r));              /* q + t <= len */
            jcc(a, CC_A, L_INVALID);
        }
        if (i->disp32) {
            add_offset(a, i->disp32);
            if (i->disp32 > 0) { op_rm(a, 0x3B, S0, RDI, AT_LEN(r)); jcc(a, CC_A, L_INVALID); }
        }
        op_rm(a, 0x03, S0, RDI, AT_BASE(r));                 /* back to an address */
        mov64(a, d, S0);
        return "ADDR";
    }
    case TISA_OP_LOAD: {
        bool hoisted = is_hoisted(a, i);
        if (i->region_id == TISA_REGION_FRAME) load(a, 8, d, RDI, i->disp32);
        else {
            if (!hoisted) bounds(a, i->region_id, x, i->disp32, i->subop);
            load(a, i->subop, d, x, i->disp32);
        }
        if (i->ty == TISA_TYPE_BOOL) { op_imm8(a, 7, true, d, 1); jcc(a, CC_A, L_INVALID); }
        if (i->ty == TISA_TYPE_U32Z && (i->subop == 8 || i->region_id == TISA_REGION_FRAME)) mov32(a, d, d);
        if (i->ty == TISA_TYPE_I32S && i->subop != 1) movsxd(a, d, d);
        return i->region_id == TISA_REGION_FRAME ? "LOAD-FRAME" : hoisted ? "LOAD-H" : "LOAD";
    }
    case TISA_OP_STORE: {
        bool hoisted = is_hoisted(a, i);
        if (!hoisted) bounds(a, i->region_id, x, i->disp32, i->subop);
        store(a, i->subop, y, x, i->disp32);
        return hoisted ? "STORE-H" : "STORE";
    }
    }
    a->failed = true;
    return "?";
}

static void statement(Asm *a, const TcIsaStmt *s) {
    if (!s || a->failed) return;
    size_t from = a->len;
    switch (s->kind) {
    case TISA_CTRL_INSTR: {
        const char *name = instruction(a, &s->u.instr);
        span(a, from, name, s);
        return;
    }
    case TISA_CTRL_SEQ:
        statement(a, s->u.seq.first);
        statement(a, s->u.seq.second);
        return;
    case TISA_CTRL_IF: {
        uint8_t c = HW[s->u.if_cond.cond_reg];
        size_t no = new_label(a);
        op_rr(a, 0x85, true, c, c);
        jcc(a, CC_E, no);
        span(a, from, "IF", s);
        statement(a, s->u.if_cond.then_branch);
        if (s->u.if_cond.else_branch) {
            size_t end = new_label(a);
            size_t jump_at = a->len;
            jmp(a, end);
            span(a, jump_at, "IF-ELSE", s);
            place(a, no);
            statement(a, s->u.if_cond.else_branch);
            place(a, end);
        } else place(a, no);
        return;
    }
    case TISA_CTRL_COUNT_LOOP:
    case TISA_CTRL_EARLY_EXIT_LOOP: {
        bool early = s->kind == TISA_CTRL_EARLY_EXIT_LOOP;
        uint8_t x = HW[early ? s->u.early_exit_loop.idx_reg : s->u.count_loop.idx_reg];
        uint8_t n = HW[early ? s->u.early_exit_loop.bound_reg : s->u.count_loop.bound_reg];
        uint8_t k = early ? HW[s->u.early_exit_loop.cont_reg] : 0;
        uint32_t cap = early ? s->u.early_exit_loop.max_iters : s->u.count_loop.max_iters;
        const TcIsaStmt *body = early ? s->u.early_exit_loop.body : s->u.count_loop.body;
        size_t rec = loop_record(a, s);
        Hoist h = {.x = early ? s->u.early_exit_loop.idx_reg : s->u.count_loop.idx_reg};
        if (!a->hoist_bounds) h.reason = "option off";
        else {
            Affine env[TISA_NUM_VREGS];
            memset(env, 0, sizeof env);
            written_regs(body, &h.written);
            hoist_walk(&h, body, env);
            if (!h.reason && !h.nhoisted && !fund_hoisted(&h)) h.reason = "no affine region access or FUND";
        }
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
        size_t top = new_label(a), end = new_label(a);
        mov_imm(a, S0, cap);
        op_rr(a, 0x39, true, n, S0);            /* n > cap: UNSUPPORTED */
        jcc(a, CC_A, L_UNSUPPORTED);
        op_rr(a, 0x31, false, x, x);            /* x := 0 */
        span(a, from, "LOOP-ENTRY", s);
        if (!h.reason) {
            /* The entry test, then the copy without the hoisted checks:
               entered with x = 0 < n, it tests x < n at the bottom. An early-
               exit loop tests k first, as its head would, and again at the
               latch; between the two, k does not change. */
            size_t test = a->len, fast = new_label(a);
            hoist_test(a, &h, n, top);
            if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
            /* The copy starts on a 32-byte boundary of the image (which is
               mapped page-aligned), so a short loop sits in one fetch window.
               The filler between is jumped over, never run; it is made of
               multi-byte NOPs (0F 1F /0, 3 to 5 bytes) so that the whole
               image decodes as instructions. */
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
            statement(a, body);
            a->hoist = outer;
            size_t latch = a->len;
            if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
            op_imm8(a, 0, true, x, 1);
            op_rr(a, 0x39, true, x, n);
            jcc(a, CC_B, fast);
            jmp(a, end);
            span(a, latch, early ? "EARLY-FAST-LATCH" : "FAST-LATCH", s);
        }
        size_t head = a->len;
        place(a, top);
        op_rr(a, 0x39, true, x, n);
        jcc(a, CC_AE, end);
        if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
        span(a, head, early ? "EARLY-LOOP-HEAD" : "LOOP-HEAD", s);
        statement(a, body);
        size_t latch = a->len;
        if (early) { op_rr(a, 0x85, true, k, k); jcc(a, CC_E, end); }
        op_imm8(a, 0, true, x, 1);
        jmp(a, top);
        span(a, latch, early ? "EARLY-LOOP-LATCH" : "LOOP-LATCH", s);
        place(a, end);
        return;
    }
    case TISA_CTRL_RETURN:
        mov64(a, RAX, HW[s->u.ret.ret_reg]);
        store_imm32(a, RDI, AT_STATUS, (uint32_t)s->u.ret.outcome);
        store(a, 8, RAX, RDI, AT_RET_VAL);
        jmp(a, L_EPILOGUE);
        span(a, from, "RETURN", s);
        return;
    case TISA_CTRL_FUND:
        mov_imm(a, S1, s->u.fund.units);
        if (a->hoist && fund_hoisted(a->hoist)) {    /* the allowance was tested at loop entry */
            add_to_mem(a, RDI, AT_USED, S1);
            span(a, from, "FUND-H", s);
            return;
        }
        load(a, 8, S0, RDI, AT_BUDGET);
        op_rm(a, 0x2B, S0, RDI, AT_USED);        /* allowance - receipt */
        op_rr(a, 0x39, true, S0, S1);
        jcc(a, CC_B, L_EXHAUSTED);
        add_to_mem(a, RDI, AT_USED, S1);
        span(a, from, "FUND", s);
        return;
    }
    a->failed = true;
}

static void push(Asm *a, uint8_t r) { rex(a, false, 0, r, false); put(a, (uint8_t)(0x50 | (r & 7))); }
static void pop(Asm *a, uint8_t r) { rex(a, false, 0, r, false); put(a, (uint8_t)(0x58 | (r & 7))); }

bool tisa_x86_emit(const TcIsaProgram *prog, TcIsaX86 *out, char *err, size_t cap) {
    return tisa_x86_emit_with(prog, NULL, out, err, cap);
}

bool tisa_x86_emit_with(const TcIsaProgram *prog, const TcIsaX86Options *opts, TcIsaX86 *out,
                        char *err, size_t cap) {
    memset(out, 0, sizeof *out);
    if (!tisa_admit(prog, NULL, err, cap)) return false;
    Asm a = {0};
    a.hoist_bounds = opts && opts->hoist_bounds;
    for (unsigned l = 0; l < L_FIRST_FREE; ++l) new_label(&a);
    static const uint8_t saved[] = {RBX, R12, R13, S0, S1};
    size_t from = a.len;
    for (unsigned r = 0; r < 5; ++r) push(&a, saved[r]);
    span(&a, from, "PROLOGUE", NULL);
    statement(&a, prog->root);
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
    for (unsigned r = 5; r > 0; --r) pop(&a, saved[r - 1]);
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
    free(a.labels); free(a.fixes);
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
