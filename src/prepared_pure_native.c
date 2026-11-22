/* A prepared pure program's register code lowered to t-ISA p1 and emitted by
 * the in-house x86 emitter (prepared_pure_native.h).
 *
 * The native program keeps each activation's registers as words in the
 * `frames` region: a frame is its return code index, the register its
 * caller's value goes to, its caller's frame offset, the value class its
 * activation owes at return, then the registers.
 * The `state` region holds the code index to run, the current and next free
 * frame offsets, the answer, the heap's next free offset, the arguments a
 * call stages for its callee and the match registers.  Expressions live in
 * the `heap` region as records (prepared_pure_native.h).  A dispatch loop
 * runs one basic block of the register code per turn: a call pushes a frame
 * and enters its callee's head, a tail call enters it in the same frame, a
 * return pops back to the code after the call.  A head is entered by
 * selecting its equation as the register code does: the one equation whose
 * patterns and scalar guard admit the arguments, or none, where the register
 * code declines.  Integers are words; an operation whose exact result is not
 * a word declines, and the register code computes it.  p1 has no divide: a
 * remainder by a literal multiplies by the divisor's invariant reciprocal;
 * one by a computed divisor stays on the register code.  A register keeps
 * the representation of its type, an integer or truth value as such or a
 * term word, and a value converts where it meets another type; one outside
 * the type it converts to declines.
 * Every SLICE turns the run returns so the host can poll.  When the frames
 * region is full a call returns so the host can grow it, and a call is its
 * own block, so the run resumes at the call; a block that builds checks the
 * heap's room for all its records before it starts, and returns there
 * likewise. */
#include "prepared_pure_native.h"

#include "../vendor/tcalc-native/tisa/tisa_ref.h"
#include "../vendor/tcalc-native/tisa/tisa_x86.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPN_REGION_FRAMES = 0,
    PPN_REGION_STATE = 1,
    PPN_REGION_HEAP = 2,
    /* return code, return register, caller, the class the activation owes
     * at return (PpnClass) */
    PPN_FRAME_HEADER = 4u,
    PPN_FRAME_OWED = 24,
    PPN_STATE_PC = 0, PPN_STATE_FRAME = 8, PPN_STATE_TOP = 16,
    PPN_STATE_VALUE = 24, PPN_STATE_SCRATCH = 32, PPN_STATE_HEAP_TOP = 40,
    PPN_STATE_COUNT = 48, PPN_STATE_CHOSEN = 56, PPN_STATE_ARGS = 64,
    PPN_STATE_MATCH = PPN_STATE_ARGS + 8 * PPN_MAX_ARITY,
    PPN_STATE_BYTES = PPN_STATE_MATCH + 8 * PPN_MATCH_REGISTERS,
    PPN_SLICE = 1u << 20,
};
#define PPN_ROOT_RETURN UINT64_C(0xFFFFFFFF)

/* The run's codes, in ret_val. */
enum { PPN_CODE_ANSWER = 0, PPN_CODE_EMPTY, PPN_CODE_DECLINE, PPN_CODE_ROOM,
       PPN_CODE_YIELD, PPN_CODE_HEAP };

/* p1 registers. */
enum { R1 = 1, R2, R3, R4, R_PC, R_GO, R_I, R_N, R_FRAME, R_STATE, R_FRAMES };

struct PpnImage {
    TcIsaProgram program;
    TcIsaX86 code;
    TcIsaStmt **chunks;
    size_t chunk_len, chunk_used;
    uint32_t frame_bytes;
    uint32_t root_code, root_registers, root_argument_base, root_arguments;
    PpnType root_type;
    PpnType argument_types[PPN_MAX_ARITY];
    uint8_t *frames;
    size_t frames_len;
    uint8_t *state;
    size_t state_len;
    PpnHeap heap;
    size_t constant_words; /* the heap's prefix of the program's constants */
    /* What the collector reads: each body's register types, the body of
     * each code index, and the most heap words one block builds. */
    PpnType *body_types;
    uint32_t *body_of;
    uint32_t width;
    size_t code_len;
    size_t block_words;
    const char *declined;  /* why the last run declined */
};

static bool heap_reserve(PpnHeap *heap, size_t words);

/* ---- statements ---------------------------------------------------------- */

typedef struct {
    PpnImage *image;
    bool failed;
} Builder;

static TcIsaStmt *node(Builder *b) {
    PpnImage *im = b->image;
    enum { CHUNK = 1024 };
    if (b->failed)
        return NULL;
    if (im->chunk_len == 0u || im->chunk_used == CHUNK) {
        TcIsaStmt **chunks = realloc(im->chunks, sizeof(*chunks) * (im->chunk_len + 1u));
        TcIsaStmt *chunk = chunks ? calloc(CHUNK, sizeof(*chunk)) : NULL;
        if (!chunk) {
            if (chunks)
                im->chunks = chunks;
            b->failed = true;
            return NULL;
        }
        im->chunks = chunks;
        im->chunks[im->chunk_len++] = chunk;
        im->chunk_used = 0u;
    }
    return &im->chunks[im->chunk_len - 1u][im->chunk_used++];
}

static const TcIsaStmt *instr(Builder *b, TcIsaInstr ins) {
    TcIsaStmt *s = node(b);
    if (!s)
        return NULL;
    s->kind = TISA_CTRL_INSTR;
    s->u.instr = ins;
    return s;
}

static const TcIsaStmt *seq(Builder *b, const TcIsaStmt *x, const TcIsaStmt *y) {
    if (!x)
        return y;
    if (!y)
        return x;
    TcIsaStmt *s = node(b);
    if (!s)
        return NULL;
    s->kind = TISA_CTRL_SEQ;
    s->u.seq.first = x;
    s->u.seq.second = y;
    return s;
}

static const TcIsaStmt *when(Builder *b, uint8_t cond, const TcIsaStmt *then_b,
                             const TcIsaStmt *else_b) {
    TcIsaStmt *s = node(b);
    if (!s)
        return NULL;
    s->kind = TISA_CTRL_IF;
    s->u.if_cond.cond_reg = cond;
    s->u.if_cond.then_branch = then_b;
    s->u.if_cond.else_branch = else_b;
    return s;
}

static const TcIsaStmt *leave(Builder *b, uint64_t code) {
    TcIsaStmt *r = node(b);
    if (!r)
        return NULL;
    r->kind = TISA_CTRL_RETURN;
    r->u.ret.outcome = TISA_OUTCOME_FIRED;
    r->u.ret.ret_reg = R1;
    return seq(b, instr(b, (TcIsaInstr){.op = TISA_OP_IMM, .ty = TISA_TYPE_U64,
                                        .dst = R1, .imm64 = code}), r);
}

static const TcIsaStmt *imm(Builder *b, uint8_t dst, uint64_t value) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_IMM, .ty = TISA_TYPE_U64,
                                 .dst = dst, .imm64 = value});
}

static const TcIsaStmt *imm_bool(Builder *b, uint8_t dst, bool value) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_IMM, .ty = TISA_TYPE_BOOL,
                                 .dst = dst, .imm64 = value ? 1u : 0u});
}

static const TcIsaStmt *mov(Builder *b, uint8_t dst, uint8_t src) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_MOV, .ty = TISA_TYPE_U64,
                                 .dst = dst, .src1 = src});
}

static const TcIsaStmt *bin(Builder *b, TcIsaBinOp op, TcIsaValType ty,
                            uint8_t dst, uint8_t x, uint8_t y) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_BIN, .subop = (uint8_t)op,
                                 .ty = ty, .dst = dst, .src1 = x, .src2 = y});
}

static const TcIsaStmt *shift(Builder *b, TcIsaShiftOp op, uint8_t dst,
                              uint8_t src, uint8_t by) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_SHIFT, .subop = (uint8_t)op,
                                 .ty = TISA_TYPE_U64, .dst = dst, .src1 = src,
                                 .bit_width = by});
}

static const TcIsaStmt *shl(Builder *b, uint8_t dst, uint8_t src, uint8_t by) {
    return shift(b, TISA_SHIFT_SHL, dst, src, by);
}

static const TcIsaStmt *shru(Builder *b, uint8_t dst, uint8_t src, uint8_t by) {
    return shift(b, TISA_SHIFT_SHRU, dst, src, by);
}

static const TcIsaStmt *shrs(Builder *b, uint8_t dst, uint8_t src, uint8_t by) {
    return shift(b, TISA_SHIFT_SHRS, dst, src, by);
}

static const TcIsaStmt *pick(Builder *b, uint8_t dst, uint8_t cond,
                             uint8_t if_true, uint8_t if_false) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_SELECT, .ty = TISA_TYPE_U64,
                                 .dst = dst, .src3 = cond, .src1 = if_true,
                                 .src2 = if_false});
}

static const TcIsaStmt *cmp(Builder *b, TcIsaCmpCond cond, uint8_t dst,
                            uint8_t x, uint8_t y) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_CMP, .subop = (uint8_t)cond,
                                 .ty = TISA_TYPE_U64, .dst = dst, .src1 = x,
                                 .src2 = y});
}

static const TcIsaStmt *cmp_imm(Builder *b, TcIsaCmpCond cond, uint8_t dst,
                                uint8_t x, uint64_t k) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_CMP, .subop = (uint8_t)cond,
                                 .ty = TISA_TYPE_U64, .dst = dst, .src1 = x,
                                 .bit_width = 1u, .imm64 = k});
}

static const TcIsaStmt *base(Builder *b, uint8_t dst, uint8_t region) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_LOAD, .ty = TISA_TYPE_PTR,
                                 .subop = TISA_MEM_U64, .region_id = TISA_REGION_FRAME,
                                 .dst = dst, .src1 = TISA_V0,
                                 .disp32 = (int32_t)(16u * region)});
}

static const TcIsaStmt *length(Builder *b, uint8_t dst, uint8_t region) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_LOAD, .ty = TISA_TYPE_U64,
                                 .subop = TISA_MEM_U64, .region_id = TISA_REGION_FRAME,
                                 .dst = dst, .src1 = TISA_V0,
                                 .disp32 = (int32_t)(16u * region + 8u)});
}

static const TcIsaStmt *addr(Builder *b, uint8_t region, uint8_t dst,
                             uint8_t base_reg, uint8_t index, int32_t disp) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_ADDR, .ty = TISA_TYPE_PTR,
                                 .region_id = region, .dst = dst, .src1 = base_reg,
                                 .src2 = index, .scale_lg2 = 0u, .disp32 = disp});
}

static const TcIsaStmt *load(Builder *b, uint8_t region, uint8_t dst,
                             uint8_t ptr, int32_t disp) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_LOAD, .ty = TISA_TYPE_U64,
                                 .subop = TISA_MEM_U64, .region_id = region,
                                 .dst = dst, .src1 = ptr, .disp32 = disp});
}

static const TcIsaStmt *store(Builder *b, uint8_t region, uint8_t ptr,
                              int32_t disp, uint8_t value) {
    return instr(b, (TcIsaInstr){.op = TISA_OP_STORE, .ty = TISA_TYPE_U64,
                                 .subop = TISA_MEM_U64, .region_id = region,
                                 .src1 = ptr, .src2 = value, .disp32 = disp});
}

/* A Bool register's value as a word. */
static const TcIsaStmt *word_of_bool(Builder *b, uint8_t dst, uint8_t cond,
                                     uint8_t one, uint8_t zero) {
    return seq(b, seq(b, imm(b, one, 1u), imm(b, zero, 0u)),
               pick(b, dst, cond, one, zero));
}

/* flag := whether the low three bits of x are `tag`. */
static const TcIsaStmt *has_tag(Builder *b, uint8_t flag, uint8_t x, uint64_t tag) {
    return seq(b, shl(b, flag, x, 61u),
           seq(b, shru(b, flag, flag, 61u), cmp_imm(b, TISA_CMP_EQ, flag, flag, tag)));
}

/* x := |x| as an unsigned word (the magnitude of INT64_MIN is 2^63); uses
 * flag and scratch. */
static const TcIsaStmt *magnitude(Builder *b, uint8_t x, uint8_t flag,
                                  uint8_t scratch) {
    return seq(b, cmp_imm(b, TISA_CMP_SLT, flag, x, 0u),
           seq(b, imm(b, scratch, 0u),
           seq(b, bin(b, TISA_BIN_SUB, TISA_TYPE_U64, scratch, scratch, x),
               pick(b, x, flag, scratch, x))));
}

/* ---- types --------------------------------------------------------------- */

typedef struct {
    const PpnView *view;
    PpnType *params;      /* heads' parameters, PPN_MAX_ARITY each */
    PpnType *results;     /* heads' results; the root's at head_len */
    PpnType *regs;        /* per body (equation, then the root), per register */
    uint32_t width;       /* registers per body */
    uint32_t *body_of;    /* the body each code index belongs to */
    bool changed;
} Types;

/* Two known types meet in a term. */
static void join(Types *t, PpnType *slot, PpnType ty) {
    if (ty == PPN_TYPE_UNKNOWN || *slot == ty || *slot == PPN_TYPE_TERM)
        return;
    *slot = *slot == PPN_TYPE_UNKNOWN ? ty : PPN_TYPE_TERM;
    t->changed = true;
}

static PpnType operand_type(const Types *t, uint32_t body, PpnOperand o) {
    if (o.literal)
        return o.literal_type;
    return o.reg < t->width ? t->regs[(size_t)body * t->width + o.reg]
                            : PPN_TYPE_UNKNOWN;
}

static PpnType *reg_type(Types *t, uint32_t body, uint32_t reg) {
    return reg < t->width ? &t->regs[(size_t)body * t->width + reg] : NULL;
}

static bool op_is_comparison(uint8_t op) { return op >= PPN_EQ; }

/* The body a code index belongs to: reached from a body's first code by
 * falling through, branching and jumping, not across calls' callees. */
static bool assign_bodies(const PpnView *v, uint32_t *body_of) {
    size_t bodies = v->equation_len + 1u;
    uint32_t *stack = malloc(sizeof(*stack) * (2u * v->code_len + 1u));
    if (!stack)
        return false;
    for (size_t c = 0u; c < v->code_len; c++)
        body_of[c] = UINT32_MAX;
    bool ok = true;
    for (size_t body = 0u; ok && body < bodies; body++) {
        uint32_t first = body < v->equation_len ? v->equations[body].first_code
                                                : v->root_code;
        size_t top = 0u;
        stack[top++] = first;
        while (ok && top > 0u) {
            uint32_t c = stack[--top];
            if (c >= v->code_len) {
                ok = false;
                break;
            }
            if (body_of[c] == body)
                continue;
            if (body_of[c] != UINT32_MAX) {
                ok = false; /* code shared by two bodies */
                break;
            }
            body_of[c] = (uint32_t)body;
            const PpnCode *code = &v->code[c];
            bool falls = !code->returns && code->kind != PPN_JUMP &&
                code->kind != PPN_TAIL_CALL && code->kind != PPN_RETURN &&
                code->kind != PPN_FAIL && code->kind != PPN_DECLINE;
            if (falls)
                stack[top++] = c + 1u;
            if (code->kind == PPN_BRANCH || code->kind == PPN_BRANCH_BINARY ||
                code->kind == PPN_JUMP ||
                (code->kind == PPN_MATCH && code->target != PPN_NO_TARGET))
                stack[top++] = code->target;
        }
    }
    free(stack);
    return ok;
}

/* The types a match program gives the locals it binds, its registers typed
 * `first` (the first `first_count`) and terms (the rest).  False where a
 * program reads a register nothing loaded. */
static bool infer_match(Types *t, uint32_t body, const PpnMatchOp *ops,
                        uint32_t op_count, const PpnType *first,
                        uint32_t first_count) {
    PpnType regs[PPN_MATCH_REGISTERS];
    for (uint32_t r = 0u; r < PPN_MATCH_REGISTERS; r++)
        regs[r] = r < first_count ? first[r] : PPN_TYPE_UNKNOWN;
    for (uint32_t i = 0u; i < op_count; i++) {
        const PpnMatchOp *op = &ops[i];
        if (op->source >= PPN_MATCH_REGISTERS)
            return false;
        switch (op->kind) {
        case PPN_MATCH_BIND: {
            PpnType *local = reg_type(t, body, op->operand);
            if (!local)
                return false;
            join(t, local, regs[op->source]);
            break;
        }
        case PPN_MATCH_EXPR:
            if (op->operand + op->length > PPN_MATCH_REGISTERS)
                return false;
            for (uint32_t k = 0u; k < op->length; k++)
                regs[op->operand + k] = PPN_TYPE_TERM;
            break;
        case PPN_MATCH_SAME:
            if (!reg_type(t, body, op->operand))
                return false;
            break;
        default:
            break;
        }
    }
    return true;
}

static uint32_t head_of_equation(const PpnView *v, size_t e) {
    for (size_t h = 0u; h < v->head_len; h++)
        if (e >= v->heads[h].first_equation &&
            e < v->heads[h].first_equation + v->heads[h].equation_count)
            return (uint32_t)h;
    return UINT32_MAX;
}

static bool infer(Types *t) {
    const PpnView *v = t->view;
    size_t root = v->equation_len;
    for (uint32_t i = 0u; i < v->root_arguments; i++)
        t->regs[root * t->width + v->root_argument_base + i] =
            v->root_argument_types[i];
    for (int round = 0; round < 64; round++) {
        t->changed = false;
        for (size_t e = 0u; e < v->equation_len; e++) {
            const PpnEquation *eq = &v->equations[e];
            uint32_t head = head_of_equation(v, e);
            if (head == UINT32_MAX || eq->arity > PPN_MAX_ARITY ||
                !infer_match(t, (uint32_t)e, &v->match_ops[eq->first_op],
                             eq->op_count,
                             &t->params[(size_t)head * PPN_MAX_ARITY], eq->arity))
                return false;
        }
        for (size_t c = 0u; c < v->code_len; c++) {
            const PpnCode *code = &v->code[c];
            uint32_t body = t->body_of[c];
            if (body == UINT32_MAX)
                continue;
            PpnType *result = body < v->equation_len
                ? NULL : &t->results[v->head_len];
            if (body < v->equation_len) {
                uint32_t h = head_of_equation(v, body);
                if (h != UINT32_MAX)
                    result = &t->results[h];
            }
            if (!result)
                return false;
            PpnType value = PPN_TYPE_UNKNOWN;
            switch (code->kind) {
            case PPN_MOVE:
                value = operand_type(t, body, code->a);
                break;
            case PPN_BINARY:
                value = op_is_comparison(code->op) ? PPN_TYPE_BOOL : PPN_TYPE_INT;
                break;
            case PPN_BUILD:
                value = PPN_TYPE_TERM;
                break;
            case PPN_MATCH: {
                PpnType matched = operand_type(t, body, code->a);
                if (!infer_match(t, body, &v->match_ops[code->first_op],
                                 code->op_count, &matched, 1u))
                    return false;
                break;
            }
            case PPN_CALL:
            case PPN_TAIL_CALL:
                if (code->callee >= v->head_len || code->argc > PPN_MAX_ARITY)
                    return false;
                for (uint32_t i = 0u; i < code->argc; i++)
                    join(t, &t->params[(size_t)code->callee * PPN_MAX_ARITY + i],
                         operand_type(t, body, v->args[code->first_arg + i]));
                /* A callee answers in its result type into the register (or
                 * the answer) its call takes: the two have one type. */
                if (code->kind == PPN_TAIL_CALL) {
                    join(t, result, t->results[code->callee]);
                    join(t, &t->results[code->callee], *result);
                } else {
                    value = t->results[code->callee];
                    PpnType *dst = code->returns ? result : reg_type(t, body, code->dst);
                    if (dst)
                        join(t, &t->results[code->callee], *dst);
                }
                break;
            case PPN_RETURN:
                join(t, result, operand_type(t, body, code->a));
                break;
            default:
                break;
            }
            if (code->kind == PPN_MOVE || code->kind == PPN_BINARY ||
                code->kind == PPN_BUILD || code->kind == PPN_CALL) {
                PpnType *dst = reg_type(t, body, code->dst);
                if (code->returns)
                    join(t, result, value);
                else if (dst)
                    join(t, dst, value);
                else
                    return false;
            }
        }
        if (!t->changed)
            return true;
    }
    return false;
}

/* Every operand's type known and fitting its use. */
static bool check_types(const Types *t, char *why, size_t why_size) {
    const PpnView *v = t->view;
    for (size_t c = 0u; c < v->code_len; c++) {
        const PpnCode *code = &v->code[c];
        uint32_t body = t->body_of[c];
        if (body == UINT32_MAX)
            continue;
        PpnType a = operand_type(t, body, code->a);
        PpnType b2 = operand_type(t, body, code->b);
        bool ok = true;
        switch (code->kind) {
        case PPN_MOVE:
        case PPN_RETURN:
        case PPN_MATCH:
        case PPN_BRANCH:
            ok = a != PPN_TYPE_UNKNOWN;
            break;
        case PPN_BINARY:
        case PPN_BRANCH_BINARY:
            ok = a != PPN_TYPE_UNKNOWN && b2 != PPN_TYPE_UNKNOWN;
            if (code->kind == PPN_BRANCH_BINARY)
                ok = ok && op_is_comparison(code->op);
            if (code->op == PPN_SAME_TYPED)
                ok = ok && a == b2 && a != PPN_TYPE_TERM;
            break;
        case PPN_BUILD:
        case PPN_CALL:
        case PPN_TAIL_CALL:
            for (uint32_t i = 0u; ok && i < code->argc; i++)
                ok = operand_type(t, body, v->args[code->first_arg + i]) !=
                    PPN_TYPE_UNKNOWN;
            break;
        default:
            break;
        }
        if (!ok) {
            snprintf(why, why_size, "code %zu: an operand of unknown or unfit type", c);
            return false;
        }
    }
    for (size_t e = 0u; e < v->equation_len; e++) {
        const PpnEquation *eq = &v->equations[e];
        if (eq->guarded &&
            (operand_type(t, (uint32_t)e, eq->guard_a) == PPN_TYPE_UNKNOWN ||
             operand_type(t, (uint32_t)e, eq->guard_b) == PPN_TYPE_UNKNOWN)) {
            snprintf(why, why_size, "equation %zu: a guard over an untyped local", e);
            return false;
        }
    }
    if (t->results[v->head_len] == PPN_TYPE_UNKNOWN) {
        snprintf(why, why_size, "the root's answer has no known type");
        return false;
    }
    return true;
}

/* ---- code ---------------------------------------------------------------- */

typedef struct {
    Builder *b;
    const PpnView *v;
    const Types *t;
    bool *leader;
    uint32_t frame_bytes;
    uint32_t body;        /* the body being lowered */
    uint32_t width;       /* registers per frame */
    bool owes;            /* some typed head owes a class at return */
} Gen;

static int32_t reg_disp(uint32_t reg) {
    return (int32_t)(8u * (PPN_FRAME_HEADER + reg));
}

static const TcIsaStmt *frame_pointer(Gen *g) {
    return addr(g->b, PPN_REGION_FRAMES, R4, R_FRAMES, R_FRAME, 0);
}

static const TcIsaStmt *decline(Gen *g) {
    return leave(g->b, PPN_CODE_DECLINE);
}

/* dst, a value of type `from`, := the same value as type `to`; a value
 * outside `to` declines.  Integers and truth values meet only in terms.
 * Uses scratch. */
static const TcIsaStmt *convert(Gen *g, uint8_t dst, PpnType from, PpnType to,
                                uint8_t scratch) {
    Builder *b = g->b;
    if (from == to)
        return NULL;
    if (to == PPN_TYPE_TERM && from == PPN_TYPE_INT)
        /* in [-2^62, 2^62) iff dst + 2^62 < 2^63 as unsigned words */
        return seq(b, imm(b, scratch, UINT64_C(1) << 62),
               seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, scratch, dst, scratch),
               seq(b, cmp_imm(b, TISA_CMP_ULT, scratch, scratch, UINT64_C(1) << 63),
               seq(b, when(b, scratch, NULL, decline(g)), shl(b, dst, dst, 1u)))));
    if (to == PPN_TYPE_TERM && from == PPN_TYPE_BOOL)
        return seq(b, shl(b, dst, dst, 3u),
               seq(b, imm(b, scratch, PPN_WORD_BOOL),
                   bin(b, TISA_BIN_OR, TISA_TYPE_U64, dst, dst, scratch)));
    if (to == PPN_TYPE_INT && from == PPN_TYPE_TERM)
        return seq(b, shl(b, scratch, dst, 63u),
               seq(b, cmp_imm(b, TISA_CMP_EQ, scratch, scratch, 0u),
               seq(b, when(b, scratch, NULL, decline(g)), shrs(b, dst, dst, 1u))));
    if (to == PPN_TYPE_BOOL && from == PPN_TYPE_TERM)
        return seq(b, has_tag(b, scratch, dst, PPN_WORD_BOOL),
               seq(b, when(b, scratch, NULL, decline(g)), shru(b, dst, dst, 3u)));
    b->failed = true;
    return NULL;
}

/* Decline unless x, a value of type `have`, is of class `cls`, as HE's
 * cast reads it: an integer is a Number, a truth value a Bool, and a term
 * word one of those by its tag.  Uses flag and scratch. */
static const TcIsaStmt *class_check(Gen *g, uint8_t x, PpnType have, uint8_t cls,
                                    uint8_t flag, uint8_t scratch) {
    Builder *b = g->b;
    if (cls == PPN_CLASS_ANY)
        return NULL;
    if (have == PPN_TYPE_INT)
        return cls == PPN_CLASS_BOOL ? decline(g) : NULL;
    if (have == PPN_TYPE_BOOL)
        return cls == PPN_CLASS_NUMBER ? decline(g) : NULL;
    if (have != PPN_TYPE_TERM) {
        b->failed = true;
        return NULL;
    }
    const TcIsaStmt *is_int = seq(b, shl(b, flag, x, 63u),
                                  cmp_imm(b, TISA_CMP_EQ, flag, flag, 0u));
    if (cls == PPN_CLASS_NUMBER)
        return seq(b, is_int, when(b, flag, NULL, decline(g)));
    const TcIsaStmt *is_bool = seq(b, has_tag(b, scratch, x, PPN_WORD_BOOL),
                                   when(b, scratch, NULL, decline(g)));
    if (cls == PPN_CLASS_BOOL)
        return is_bool;
    return seq(b, is_int, when(b, flag, NULL, is_bool));
}

/* dst := the class (NUMBER or BOOL) of x, a value of type `have` already
 * known to have one.  Uses flag and scratch. */
static const TcIsaStmt *class_of(Gen *g, uint8_t dst, uint8_t x, PpnType have,
                                 uint8_t flag, uint8_t scratch) {
    Builder *b = g->b;
    if (have != PPN_TYPE_TERM)
        return imm(b, dst, have == PPN_TYPE_BOOL ? PPN_CLASS_BOOL : PPN_CLASS_NUMBER);
    return seq(b, shl(b, flag, x, 63u),
           seq(b, cmp_imm(b, TISA_CMP_EQ, flag, flag, 0u),
           seq(b, imm(b, dst, PPN_CLASS_NUMBER),
           seq(b, imm(b, scratch, PPN_CLASS_BOOL),
               pick(b, dst, flag, dst, scratch)))));
}

/* dst := the operand as type `want`.  R4 holds the current frame. */
static const TcIsaStmt *value(Gen *g, uint8_t dst, PpnOperand o, PpnType want,
                              uint8_t scratch) {
    Builder *b = g->b;
    PpnType have = operand_type(g->t, g->body, o);
    if (o.literal) {
        uint64_t word = (uint64_t)o.value;
        if (have != want && want == PPN_TYPE_TERM) {
            if (have == PPN_TYPE_INT && !ppn_word_from_int(o.value, &word))
                return decline(g);
            if (have == PPN_TYPE_BOOL)
                word = ppn_word_from_bool(o.value != 0);
            return imm(b, dst, word);
        }
        if (have != want) {
            b->failed = true;
            return NULL;
        }
        return imm(b, dst, word);
    }
    return seq(b, load(b, PPN_REGION_FRAMES, dst, R4, reg_disp(o.reg)),
               convert(g, dst, have, want, scratch));
}

/* dst := the operand in its own type's representation. */
static const TcIsaStmt *operand(Gen *g, uint8_t dst, PpnOperand o) {
    return value(g, dst, o, operand_type(g->t, g->body, o), R_PC);
}

/* Register `reg` of the current frame := src, a value of type `have`. */
static const TcIsaStmt *set_register(Gen *g, uint32_t reg, uint8_t src,
                                     PpnType have, uint8_t scratch) {
    PpnType want = operand_type(g->t, g->body, (PpnOperand){.reg = reg});
    return seq(g->b, convert(g, src, have, want, scratch),
               store(g->b, PPN_REGION_FRAMES, R4, reg_disp(reg), src));
}

static const TcIsaStmt *set_pc(Gen *g, uint64_t pc) {
    return imm(g->b, R_PC, pc);
}

/* R3 := |a| mod d for d > 1 not a power of two, by Granlund and Montgomery's
 * division by an invariant: with l = ceil(log2 d) and
 * m = floor(2^64 (2^l - d) / d) + 1 (a word), t = mulhi(m, n) and
 * q = (t + ((n - t) >> 1)) >> (l - 1) is floor(n / d) for every word n.
 * mulhi is four 32-bit products.  R1 holds |a|, which the state's scratch
 * word keeps across the product; uses R1-R4 and R_PC. */
static const TcIsaStmt *remainder_by_magic(Gen *g, uint64_t d) {
    Builder *b = g->b;
    unsigned l = 64u - (unsigned)__builtin_clzll(d - 1u);
    unsigned __int128 wide = (((unsigned __int128)1 << l) - d) << 64;
    uint64_t m = (uint64_t)(wide / d) + 1u;
    uint64_t mh = m >> 32, ml = m & 0xFFFFFFFFu;
    const TcIsaStmt *s = store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_SCRATCH, R1);
    /* xl in R3, xh in R4; hh accumulates in R2, the middle sum in R_PC */
    s = seq(b, s, seq(b, shl(b, R3, R1, 32u), shru(b, R3, R3, 32u)));
    s = seq(b, s, shru(b, R4, R1, 32u));
    s = seq(b, s, seq(b, imm(b, R2, mh), bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R2, R4, R2)));
    s = seq(b, s, seq(b, imm(b, R_PC, ml),
                  seq(b, bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R_PC, R3, R_PC),
                      shru(b, R_PC, R_PC, 32u))));
    /* the two cross products, each split into the sums */
    s = seq(b, s, seq(b, imm(b, R1, ml), bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R1, R4, R1)));
    s = seq(b, s, seq(b, shl(b, R4, R1, 32u), seq(b, shru(b, R4, R4, 32u),
                  bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R_PC, R_PC, R4))));
    s = seq(b, s, seq(b, shru(b, R1, R1, 32u), bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R2, R2, R1)));
    s = seq(b, s, seq(b, imm(b, R1, mh), bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R1, R3, R1)));
    s = seq(b, s, seq(b, shl(b, R4, R1, 32u), seq(b, shru(b, R4, R4, 32u),
                  bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R_PC, R_PC, R4))));
    s = seq(b, s, seq(b, shru(b, R1, R1, 32u), bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R2, R2, R1)));
    s = seq(b, s, seq(b, shru(b, R_PC, R_PC, 32u), bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R2, R2, R_PC)));
    /* q, then n - q d */
    s = seq(b, s, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_SCRATCH));
    s = seq(b, s, seq(b, bin(b, TISA_BIN_SUB, TISA_TYPE_U64, R3, R1, R2),
                  seq(b, shru(b, R3, R3, 1u),
                  seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R3, R3, R2),
                      shru(b, R3, R3, (uint8_t)(l - 1u))))));
    return seq(b, s, seq(b, imm(b, R4, d),
                     seq(b, bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R4, R3, R4),
                         bin(b, TISA_BIN_SUB, TISA_TYPE_U64, R3, R1, R4))));
}

/* R3 := a REM d or a FMOD d, a literal divisor: the remainder of the
 * magnitudes (a mask for a power of two), then the sign, the dividend's for
 * the remainder; for the floor modulo, a nonzero remainder of the other sign
 * than the divisor moves by the divisor.  A zero divisor declines. */
static const TcIsaStmt *modulo(Gen *g, uint8_t op, PpnOperand a, int64_t d) {
    Builder *b = g->b;
    if (d == 0)
        return decline(g);
    uint64_t magnitude_d = d < 0 ? 0u - (uint64_t)d : (uint64_t)d;
    const TcIsaStmt *s = seq(b, value(g, R1, a, PPN_TYPE_INT, R_PC),
                             magnitude(b, R1, R3, R_PC));
    if ((magnitude_d & (magnitude_d - 1u)) == 0u)
        s = seq(b, s, seq(b, imm(b, R3, magnitude_d - 1u),
                      bin(b, TISA_BIN_AND, TISA_TYPE_U64, R3, R1, R3)));
    else
        s = seq(b, s, remainder_by_magic(g, magnitude_d));
    s = seq(b, s, seq(b, frame_pointer(g), value(g, R1, a, PPN_TYPE_INT, R_PC)));
    s = seq(b, s, seq(b, cmp_imm(b, TISA_CMP_SLT, R1, R1, 0u),
                  seq(b, imm(b, R_PC, 0u),
                  seq(b, bin(b, TISA_BIN_SUB, TISA_TYPE_U64, R_PC, R_PC, R3),
                      pick(b, R3, R1, R_PC, R3)))));
    if (op == PPN_REM)
        return s;
    /* the floor modulo: R3 nonzero and of the other sign than d */
    s = seq(b, s, seq(b, imm(b, R2, (uint64_t)d),
                  seq(b, cmp_imm(b, TISA_CMP_NE, R1, R3, 0u),
                  seq(b, bin(b, TISA_BIN_XOR, TISA_TYPE_U64, R_PC, R3, R2),
                  seq(b, cmp_imm(b, TISA_CMP_SLT, R_PC, R_PC, 0u),
                      bin(b, TISA_BIN_AND, TISA_TYPE_BOOL, R1, R1, R_PC))))));
    return seq(b, s, seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R_PC, R3, R2),
                     pick(b, R3, R1, R_PC, R3)));
}

/* R3 := whether the words in R1 and R2 are the same term; two distinct
 * expressions decline (their structures are not compared here).  Uses R_PC. */
static const TcIsaStmt *same_term(Gen *g) {
    Builder *b = g->b;
    const TcIsaStmt *both = seq(b, has_tag(b, R_PC, R1, PPN_WORD_EXPRESSION),
        when(b, R_PC, seq(b, has_tag(b, R_PC, R2, PPN_WORD_EXPRESSION),
                          when(b, R_PC, decline(g), NULL)), NULL));
    return seq(b, cmp(b, TISA_CMP_EQ, R3, R1, R2), when(b, R3, NULL, both));
}

/* R3 := a op b as a word (comparisons give 0/1) or Bool (as_bool);
 * arithmetic that leaves the words declines.  Uses R1, R2, R_PC. */
static const TcIsaStmt *binary(Gen *g, uint8_t op, PpnOperand a, PpnOperand b2,
                               bool as_bool) {
    Builder *b = g->b;
    if (op == PPN_SAME || op == PPN_SAME_TYPED) {
        PpnType ta = operand_type(g->t, g->body, a);
        PpnType tb = operand_type(g->t, g->body, b2);
        PpnType as = ta == tb ? ta : PPN_TYPE_TERM;
        const TcIsaStmt *s = seq(b, value(g, R1, a, as, R_PC), value(g, R2, b2, as, R_PC));
        s = seq(b, s, as == PPN_TYPE_TERM ? same_term(g)
                                          : cmp(b, TISA_CMP_EQ, R3, R1, R2));
        return as_bool ? s : seq(b, s, word_of_bool(b, R3, R3, R1, R2));
    }
    if (op == PPN_REM || op == PPN_FMOD) {
        if (!b2.literal || b2.literal_type != PPN_TYPE_INT) {
            /* p1 has no divide: a computed divisor stays on the register
             * code. */
            b->failed = true;
            return NULL;
        }
        return modulo(g, op, a, b2.value);
    }
    const TcIsaStmt *s = seq(b, value(g, R1, a, PPN_TYPE_INT, R_PC),
                             value(g, R2, b2, PPN_TYPE_INT, R_PC));
    switch (op) {
    case PPN_ADD:
        /* overflow iff (a ^ r) & (b ^ r) is negative */
        s = seq(b, s, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R3, R1, R2));
        s = seq(b, s, bin(b, TISA_BIN_XOR, TISA_TYPE_U64, R_PC, R1, R3));
        s = seq(b, s, bin(b, TISA_BIN_XOR, TISA_TYPE_U64, R2, R2, R3));
        s = seq(b, s, bin(b, TISA_BIN_AND, TISA_TYPE_U64, R_PC, R_PC, R2));
        s = seq(b, s, cmp_imm(b, TISA_CMP_SLT, R2, R_PC, 0u));
        return seq(b, s, when(b, R2, decline(g), NULL));
    case PPN_SUB:
        /* overflow iff (a ^ b) & (a ^ r) is negative */
        s = seq(b, s, bin(b, TISA_BIN_SUB, TISA_TYPE_U64, R3, R1, R2));
        s = seq(b, s, bin(b, TISA_BIN_XOR, TISA_TYPE_U64, R_PC, R1, R2));
        s = seq(b, s, bin(b, TISA_BIN_XOR, TISA_TYPE_U64, R2, R1, R3));
        s = seq(b, s, bin(b, TISA_BIN_AND, TISA_TYPE_U64, R_PC, R_PC, R2));
        s = seq(b, s, cmp_imm(b, TISA_CMP_SLT, R2, R_PC, 0u));
        return seq(b, s, when(b, R2, decline(g), NULL));
    case PPN_MUL: {
        /* both factors in [-2^31, 2^31): the product is a word */
        const TcIsaStmt *range = seq(b, imm(b, R_PC, UINT64_C(1) << 31),
            seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R3, R1, R_PC),
            seq(b, cmp_imm(b, TISA_CMP_ULT, R3, R3, UINT64_C(1) << 32),
            seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R_PC, R2, R_PC),
            seq(b, cmp_imm(b, TISA_CMP_ULT, R_PC, R_PC, UINT64_C(1) << 32),
                bin(b, TISA_BIN_AND, TISA_TYPE_BOOL, R3, R3, R_PC))))));
        s = seq(b, s, range);
        s = seq(b, s, when(b, R3, NULL, decline(g)));
        return seq(b, s, bin(b, TISA_BIN_MULLO, TISA_TYPE_U64, R3, R1, R2));
    }
    default:
        break;
    }
    TcIsaCmpCond cond = TISA_CMP_EQ;
    bool swap = false;
    switch (op) {
    case PPN_EQ: cond = TISA_CMP_EQ; break;
    case PPN_LT: cond = TISA_CMP_SLT; break;
    case PPN_LE: cond = TISA_CMP_SLE; break;
    case PPN_GT: cond = TISA_CMP_SLT; swap = true; break;
    case PPN_GE: cond = TISA_CMP_SLE; swap = true; break;
    default: g->b->failed = true; return NULL;
    }
    s = seq(b, s, cmp(b, cond, R3, swap ? R2 : R1, swap ? R1 : R2));
    if (!as_bool)
        s = seq(b, s, word_of_bool(b, R3, R3, R1, R2));
    return s;
}

/* The type of a code's value (MOVE, BINARY, BUILD, CALL). */
static PpnType code_value_type(const Gen *g, const PpnCode *code) {
    switch (code->kind) {
    case PPN_MOVE:
        return operand_type(g->t, g->body, code->a);
    case PPN_BINARY:
        return op_is_comparison(code->op) ? PPN_TYPE_BOOL : PPN_TYPE_INT;
    case PPN_BUILD:
        return PPN_TYPE_TERM;
    default:
        return PPN_TYPE_UNKNOWN;
    }
}

/* The type a body answers in: its head's result, or the root's. */
static PpnType body_result_type(const Gen *g) {
    if (g->body >= g->v->equation_len)
        return g->t->results[g->v->head_len];
    uint32_t head = head_of_equation(g->v, g->body);
    return head == UINT32_MAX ? PPN_TYPE_UNKNOWN : g->t->results[head];
}

/* Answer R1, a value of type `have`: to the host at the root, else into the
 * caller's register (in the caller's type for it), popping the frame. */
static const TcIsaStmt *answer(Gen *g, PpnType have) {
    Builder *b = g->b;
    /* The class the activation owes, if any, which the answer must have:
     * R2 the class, R1 the answer in its own type. */
    const TcIsaStmt *owed = NULL;
    if (g->owes) {
        const TcIsaStmt *met = have == PPN_TYPE_TERM
            ? seq(b, cmp_imm(b, TISA_CMP_EQ, R3, R2, PPN_CLASS_NUMBER),
                  when(b, R3, class_check(g, R1, have, PPN_CLASS_NUMBER, R3, R_PC),
                       class_check(g, R1, have, PPN_CLASS_BOOL, R3, R_PC)))
            : seq(b, cmp_imm(b, TISA_CMP_NE, R3, R2,
                             have == PPN_TYPE_BOOL ? PPN_CLASS_BOOL : PPN_CLASS_NUMBER),
                  when(b, R3, decline(g), NULL));
        owed = seq(b, load(b, PPN_REGION_FRAMES, R2, R4, PPN_FRAME_OWED),
               seq(b, cmp_imm(b, TISA_CMP_EQ, R3, R2, PPN_CLASS_ANY),
                   when(b, R3, NULL, met)));
    }
    const TcIsaStmt *to_result = seq(b, owed,
        convert(g, R1, have, body_result_type(g), R_PC));
    const TcIsaStmt *root = seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_VALUE, R1),
                                leave(b, PPN_CODE_ANSWER));
    /* The caller converts on its own side: a value goes back in the
     * callee's result type, which every caller register taking it joins. */
    const TcIsaStmt *pop =
        seq(b, load(b, PPN_REGION_FRAMES, R3, R4, 8),
        seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_TOP, R_FRAME),
        seq(b, load(b, PPN_REGION_FRAMES, R_FRAME, R4, 16),
        seq(b, shl(b, R3, R3, 3u),
        seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R3, R3, R_FRAME),
        seq(b, addr(b, PPN_REGION_FRAMES, R4, R_FRAMES, R3, reg_disp(0)),
        seq(b, store(b, PPN_REGION_FRAMES, R4, 0, R1),
            mov(b, R_PC, R2))))))));
    return seq(b, to_result,
           seq(b, seq(b, load(b, PPN_REGION_FRAMES, R2, R4, 0),
                         cmp_imm(b, TISA_CMP_EQ, R3, R2, PPN_ROOT_RETURN)),
               when(b, R3, root, pop)));
}

/* The callee's arguments, each in its parameter's type, into the state's
 * staging words. */
static const TcIsaStmt *stage(Gen *g, const PpnCode *code) {
    const TcIsaStmt *s = NULL;
    for (uint32_t i = 0u; i < code->argc; i++) {
        PpnType want = g->t->params[(size_t)code->callee * PPN_MAX_ARITY + i];
        s = seq(g->b, s, seq(g->b, value(g, R3, g->v->args[code->first_arg + i], want, R_PC),
                             store(g->b, PPN_REGION_STATE, R_STATE,
                                   (int32_t)(PPN_STATE_ARGS + 8u * i), R3)));
    }
    return s;
}

static uint64_t entry_id(const Gen *g, uint32_t head) {
    return g->v->code_len + head;
}

/* ---- patterns ------------------------------------------------------------ */

static int32_t match_disp(uint32_t reg) {
    return (int32_t)(PPN_STATE_MATCH + 8u * reg);
}

/* A match program from op i on: its checks nest, so a register is loaded
 * only once the checks before it passed; binds write the current frame (R4).
 * `regs` types the match registers; `matched` runs where every check
 * passed, `mismatched` (may be NULL) where one failed.  Uses R1-R3, R_PC. */
static const TcIsaStmt *match_from(Gen *g, const PpnMatchOp *ops, uint32_t i,
                                   uint32_t count, PpnType *regs,
                                   const TcIsaStmt *matched,
                                   const TcIsaStmt *mismatched) {
    Builder *b = g->b;
    if (i == count)
        return matched;
    const PpnMatchOp *op = &ops[i];
    PpnType have = regs[op->source];
    const TcIsaStmt *source = load(b, PPN_REGION_STATE, R1, R_STATE, match_disp(op->source));
    switch (op->kind) {
    case PPN_MATCH_BIND: {
        PpnType want = operand_type(g->t, g->body, (PpnOperand){.reg = op->operand});
        return seq(b, seq(b, source, seq(b, convert(g, R1, have, want, R_PC),
                        store(b, PPN_REGION_FRAMES, R4, reg_disp(op->operand), R1))),
                   match_from(g, ops, i + 1u, count, regs, matched, mismatched));
    }
    case PPN_MATCH_ATOM: {
        /* the literal in the register's representation; none that the
         * register can hold never matches */
        PpnOperand lit = op->literal;
        uint64_t word = (uint64_t)lit.value;
        bool possible = true;
        if (have == PPN_TYPE_TERM) {
            if (lit.literal_type == PPN_TYPE_INT)
                possible = ppn_word_from_int(lit.value, &word);
            else if (lit.literal_type == PPN_TYPE_BOOL)
                word = ppn_word_from_bool(lit.value != 0);
        } else {
            possible = lit.literal_type == have;
        }
        if (!possible)
            return mismatched;
        return seq(b, seq(b, source, seq(b, imm(b, R2, word), cmp(b, TISA_CMP_EQ, R3, R1, R2))),
                   when(b, R3, match_from(g, ops, i + 1u, count, regs, matched, mismatched),
                        mismatched));
    }
    case PPN_MATCH_EXPR: {
        if (have != PPN_TYPE_TERM)
            return mismatched;
        /* an expression word, and a record of `length` elements */
        const TcIsaStmt *is_expr = seq(b, source, has_tag(b, R3, R1, PPN_WORD_EXPRESSION));
        const TcIsaStmt *record =
            seq(b, imm(b, R2, PPN_WORD_EXPRESSION),
            seq(b, bin(b, TISA_BIN_SUB, TISA_TYPE_U64, R1, R1, R2),
            seq(b, base(b, R2, PPN_REGION_HEAP),
            seq(b, addr(b, PPN_REGION_HEAP, R2, R2, R1, 0),
            seq(b, load(b, PPN_REGION_HEAP, R3, R2, 0),
                cmp_imm(b, TISA_CMP_EQ, R3, R3, op->length))))));
        const TcIsaStmt *elements = NULL;
        for (uint32_t k = 0u; k < op->length; k++)
            elements = seq(b, elements,
                seq(b, load(b, PPN_REGION_HEAP, R3, R2, (int32_t)(8u * (k + 1u))),
                    store(b, PPN_REGION_STATE, R_STATE, match_disp(op->operand + k), R3)));
        for (uint32_t k = 0u; k < op->length; k++)
            regs[op->operand + k] = PPN_TYPE_TERM;
        const TcIsaStmt *rest = match_from(g, ops, i + 1u, count, regs, matched, mismatched);
        return seq(b, is_expr, when(b, R3, seq(b, record,
                   when(b, R3, seq(b, elements, rest), mismatched)), mismatched));
    }
    case PPN_MATCH_SAME: {
        PpnType local = operand_type(g->t, g->body, (PpnOperand){.reg = op->operand});
        PpnType as = local == have ? have : PPN_TYPE_TERM;
        const TcIsaStmt *s = seq(b, source, convert(g, R1, have, as, R_PC));
        s = seq(b, s, seq(b, load(b, PPN_REGION_FRAMES, R2, R4, reg_disp(op->operand)),
                      convert(g, R2, local, as, R_PC)));
        s = seq(b, s, as == PPN_TYPE_TERM ? same_term(g) : cmp(b, TISA_CMP_EQ, R3, R1, R2));
        return seq(b, s, when(b, R3, match_from(g, ops, i + 1u, count, regs, matched,
                                                mismatched), mismatched));
    }
    default:
        b->failed = true;
        return NULL;
    }
}

static const TcIsaStmt *match_program(Gen *g, const PpnMatchOp *ops, uint32_t count,
                                      const PpnType *first, uint32_t first_count,
                                      const TcIsaStmt *matched,
                                      const TcIsaStmt *mismatched) {
    PpnType regs[PPN_MATCH_REGISTERS];
    for (uint32_t r = 0u; r < PPN_MATCH_REGISTERS; r++)
        regs[r] = r < first_count ? first[r] : PPN_TYPE_UNKNOWN;
    return match_from(g, ops, 0u, count, regs, matched, mismatched);
}

/* ---- blocks -------------------------------------------------------------- */

/* The heap bytes the block from leader c builds. */
static uint64_t block_heap_bytes(const Gen *g, uint32_t c) {
    uint64_t bytes = 0u;
    for (; c < g->v->code_len; c++) {
        const PpnCode *code = &g->v->code[c];
        if (code->kind == PPN_BUILD)
            bytes += 8u * (code->argc + 1u);
        bool ends = code->returns || code->kind == PPN_BRANCH ||
            code->kind == PPN_BRANCH_BINARY || code->kind == PPN_JUMP ||
            code->kind == PPN_CALL || code->kind == PPN_TAIL_CALL ||
            code->kind == PPN_RETURN || code->kind == PPN_FAIL ||
            code->kind == PPN_DECLINE ||
            (code->kind == PPN_MATCH && code->target != PPN_NO_TARGET);
        if (ends || (c + 1u < g->v->code_len && g->leader[c + 1u]))
            break;
    }
    return bytes;
}

/* R3 := the expression of the code's operands, at the heap's top, whose room
 * the block checked. */
static const TcIsaStmt *build(Gen *g, const PpnCode *code) {
    Builder *b = g->b;
    const TcIsaStmt *s =
        seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_HEAP_TOP),
        seq(b, base(b, R2, PPN_REGION_HEAP),
        seq(b, addr(b, PPN_REGION_HEAP, R2, R2, R1, 0),
        seq(b, imm(b, R3, code->argc), store(b, PPN_REGION_HEAP, R2, 0, R3)))));
    for (uint32_t k = 0u; k < code->argc; k++)
        s = seq(b, s, seq(b, value(g, R3, g->v->args[code->first_arg + k], PPN_TYPE_TERM, R_PC),
                          store(b, PPN_REGION_HEAP, R2, (int32_t)(8u * (k + 1u)), R3)));
    return seq(b, s,
           seq(b, imm(b, R_PC, 8u * (code->argc + 1u)),
           seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R_PC, R1, R_PC),
           seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_HEAP_TOP, R_PC),
           seq(b, imm(b, R_PC, PPN_WORD_EXPRESSION),
               bin(b, TISA_BIN_OR, TISA_TYPE_U64, R3, R1, R_PC))))));
}

/* Back to the host at code c, whose block has not begun, with `code`. */
static const TcIsaStmt *resume_at(Gen *g, uint32_t c, uint64_t code) {
    Builder *b = g->b;
    return seq(b, imm(b, R3, c),
           seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_PC, R3),
           seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_FRAME, R_FRAME),
               leave(b, code))));
}

/* One block, from leader c to its terminator. */
static const TcIsaStmt *block(Gen *g, uint32_t c) {
    Builder *b = g->b;
    const PpnView *v = g->v;
    g->body = g->t->body_of[c];
    const TcIsaStmt *s = NULL;
    uint64_t heap_bytes = block_heap_bytes(g, c);
    if (heap_bytes > 0u)
        s = seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_HEAP_TOP),
            seq(b, imm(b, R2, heap_bytes),
            seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R2, R1, R2),
            seq(b, length(b, R3, PPN_REGION_HEAP),
            seq(b, cmp(b, TISA_CMP_ULT, R3, R3, R2),
                when(b, R3, resume_at(g, c, PPN_CODE_HEAP), NULL))))));
    for (;; c++) {
        if (c >= v->code_len) {
            b->failed = true;
            return NULL;
        }
        const PpnCode *code = &v->code[c];
        switch (code->kind) {
        case PPN_MOVE:
        case PPN_BINARY:
        case PPN_BUILD: {
            const TcIsaStmt *computed = code->kind == PPN_MOVE
                ? operand(g, R3, code->a)
                : code->kind == PPN_BINARY
                    ? binary(g, code->op, code->a, code->b, false)
                    : build(g, code);
            s = seq(b, s, computed);
            if (code->returns)
                return seq(b, s, seq(b, mov(b, R1, R3), answer(g, code_value_type(g, code))));
            s = seq(b, s, set_register(g, code->dst, R3, code_value_type(g, code), R_PC));
            break;
        }
        case PPN_MATCH: {
            PpnType matched_type = operand_type(g->t, g->body, code->a);
            const TcIsaStmt *otherwise = code->target == PPN_NO_TARGET
                ? leave(b, PPN_CODE_EMPTY) : set_pc(g, code->target);
            const TcIsaStmt *on_match = code->target == PPN_NO_TARGET
                ? NULL : set_pc(g, c + 1u);
            s = seq(b, s, seq(b, operand(g, R1, code->a),
                              store(b, PPN_REGION_STATE, R_STATE, match_disp(0), R1)));
            const TcIsaStmt *m = match_program(g, &v->match_ops[code->first_op],
                                               code->op_count, &matched_type, 1u,
                                               on_match, otherwise);
            if (code->target != PPN_NO_TARGET)
                return seq(b, s, m);
            /* a mismatch leaves; a match falls through to the next code */
            s = seq(b, s, m);
            break;
        }
        case PPN_BRANCH: {
            const TcIsaStmt *cond = seq(b, value(g, R1, code->a, PPN_TYPE_BOOL, R_PC),
                                        cmp_imm(b, TISA_CMP_NE, R2, R1, 0u));
            return seq(b, s, seq(b, cond, when(b, R2, set_pc(g, c + 1u),
                                               set_pc(g, code->target))));
        }
        case PPN_BRANCH_BINARY:
            return seq(b, s, seq(b, binary(g, code->op, code->a, code->b, true),
                                 when(b, R3, set_pc(g, c + 1u),
                                      set_pc(g, code->target))));
        case PPN_JUMP:
            return seq(b, s, set_pc(g, code->target));
        case PPN_CALL: {
            /* Its own block: room for the callee's frame, else back to the
             * host at this code; then the arguments and the frame. */
            const TcIsaStmt *room =
                seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_TOP),
                seq(b, imm(b, R2, g->frame_bytes),
                seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R2, R1, R2),
                seq(b, length(b, R3, PPN_REGION_FRAMES),
                    cmp(b, TISA_CMP_ULT, R3, R3, R2)))));
            const TcIsaStmt *push =
                seq(b, addr(b, PPN_REGION_FRAMES, R4, R_FRAMES, R1, 0),
                seq(b, imm(b, R3, c + 1u),
                seq(b, store(b, PPN_REGION_FRAMES, R4, 0, R3),
                seq(b, imm(b, R3, code->dst),
                seq(b, store(b, PPN_REGION_FRAMES, R4, 8, R3),
                seq(b, store(b, PPN_REGION_FRAMES, R4, 16, R_FRAME),
                seq(b, imm(b, R3, PPN_CLASS_ANY),
                seq(b, store(b, PPN_REGION_FRAMES, R4, PPN_FRAME_OWED, R3),
                seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_TOP, R2),
                seq(b, mov(b, R_FRAME, R1),
                    set_pc(g, entry_id(g, code->callee))))))))))));
            /* stage reads the caller's registers: before the push moves R4 */
            return seq(b, s, seq(b, room, when(b, R3, resume_at(g, c, PPN_CODE_ROOM),
                                               seq(b, stage(g, code), push))));
        }
        case PPN_TAIL_CALL:
            return seq(b, s, seq(b, stage(g, code),
                                 set_pc(g, entry_id(g, code->callee))));
        case PPN_RETURN:
            return seq(b, s, seq(b, operand(g, R1, code->a),
                                 answer(g, operand_type(g->t, g->body, code->a))));
        case PPN_FAIL:
            return seq(b, s, leave(b, PPN_CODE_EMPTY));
        case PPN_DECLINE:
            return seq(b, s, decline(g));
        default:
            b->failed = true;
            return NULL;
        }
        if (c + 1u < v->code_len && g->leader[c + 1u])
            return seq(b, s, set_pc(g, c + 1u));
    }
}

/* ---- entries ------------------------------------------------------------- */

/* Equation eq's guard over its bound locals (R4: the frame): R3 := whether
 * a op b is guard_truth.  A local that is not an integer declines. */
static const TcIsaStmt *guard(Gen *g, const PpnEquation *eq) {
    Builder *b = g->b;
    bool swap = false;
    TcIsaCmpCond cond = TISA_CMP_EQ;
    bool truth = eq->guard_truth;
    switch (eq->guard_op) {
    case PPN_EQ: cond = truth ? TISA_CMP_EQ : TISA_CMP_NE; break;
    case PPN_LT: cond = truth ? TISA_CMP_SLT : TISA_CMP_SLE; swap = !truth; break;
    case PPN_LE: cond = truth ? TISA_CMP_SLE : TISA_CMP_SLT; swap = !truth; break;
    case PPN_GT: cond = truth ? TISA_CMP_SLT : TISA_CMP_SLE; swap = truth; break;
    case PPN_GE: cond = truth ? TISA_CMP_SLE : TISA_CMP_SLT; swap = truth; break;
    default: b->failed = true; return NULL;
    }
    return seq(b, value(g, R1, eq->guard_a, PPN_TYPE_INT, R_PC),
           seq(b, value(g, R2, eq->guard_b, PPN_TYPE_INT, R_PC),
               cmp(b, cond, R3, swap ? R2 : R1, swap ? R1 : R2)));
}

/* What an equation's patterns ask of argument i: anything, an atom, or an
 * expression of some length, headed by an atom or not. */
typedef struct {
    uint8_t kind;        /* 0 anything, 1 an atom, 2 an expression */
    PpnOperand atom;     /* the atom, or the expression's head */
    bool headed;
    uint32_t length;
} Shape;

static Shape argument_shape(const PpnView *v, const PpnEquation *eq, uint32_t i) {
    const PpnMatchOp *ops = &v->match_ops[eq->first_op];
    Shape shape = {0};
    for (uint32_t k = 0u; k < eq->op_count; k++) {
        if (ops[k].source != i)
            continue;
        if (ops[k].kind == PPN_MATCH_ATOM) {
            shape.kind = 1u;
            shape.atom = ops[k].literal;
        } else if (ops[k].kind == PPN_MATCH_EXPR) {
            shape.kind = 2u;
            shape.length = ops[k].length;
            for (uint32_t j = 0u; j < eq->op_count; j++)
                if (ops[j].kind == PPN_MATCH_ATOM && ops[k].length > 0u &&
                    ops[j].source == ops[k].operand) {
                    shape.headed = true;
                    shape.atom = ops[j].literal;
                }
        }
    }
    return shape;
}

static bool same_literal(PpnOperand x, PpnOperand y) {
    return x.literal_type == y.literal_type && x.value == y.value;
}

/* Whether no arguments meet both equations' patterns: at some position
 * they ask for different atoms, an atom and an expression, or expressions
 * of different lengths or heads. */
static bool disjoint(const PpnView *v, const PpnEquation *x, const PpnEquation *y) {
    for (uint32_t i = 0u; i < x->arity && i < y->arity; i++) {
        Shape a = argument_shape(v, x, i), c = argument_shape(v, y, i);
        if (a.kind == 0u || c.kind == 0u)
            continue;
        if (a.kind != c.kind ||
            (a.kind == 1u && !same_literal(a.atom, c.atom)) ||
            (a.kind == 2u && (a.length != c.length ||
                              (a.headed && c.headed && !same_literal(a.atom, c.atom)))))
            return true;
    }
    return false;
}

/* The chosen equation's term registers its patterns do not bind, cleared:
 * the collector reads a frame's term registers, never a word another body
 * left there.  Uses R1. */
static const TcIsaStmt *clear_unbound(Gen *g, uint32_t e) {
    Builder *b = g->b;
    const PpnEquation *eq = &g->v->equations[e];
    const PpnMatchOp *ops = &g->v->match_ops[eq->first_op];
    const TcIsaStmt *s = NULL;
    for (uint32_t r = 0u; r < eq->registers && r < g->width; r++) {
        if (operand_type(g->t, e, (PpnOperand){.reg = r}) != PPN_TYPE_TERM)
            continue;
        bool bound = false;
        for (uint32_t k = 0u; k < eq->op_count && !bound; k++)
            bound = ops[k].kind == PPN_MATCH_BIND && ops[k].operand == r;
        if (!bound)
            s = seq(b, s, store(b, PPN_REGION_FRAMES, R4, reg_disp(r), R1));
    }
    return s ? seq(b, imm(b, R1, 0u), s) : NULL;
}

#define PPN_NONE UINT64_MAX

/* A head's entry (R4: the callee's frame), over the staged arguments.  The
 * register code enters the one equation whose patterns and guard admit
 * them, and declines where none or several do.  Where the equations'
 * patterns are pairwise disjoint and unguarded, the first that matches is
 * the one: it binds as it matches and is entered.  Otherwise every
 * equation is tried, and exactly one admitting is matched again to bind. */
static const TcIsaStmt *entry(Gen *g, uint32_t h) {
    Builder *b = g->b;
    const PpnView *v = g->v;
    const PpnHead *head = &v->heads[h];
    const PpnType *params = &g->t->params[(size_t)h * PPN_MAX_ARITY];
    const TcIsaStmt *s = NULL;
    /* A typed head: each argument of its parameter's class, as HE casts
     * it, and the class the activation owes at return taken on, or met
     * with the one a tail call's caller owes already. */
    for (uint32_t i = 0u; i < head->arity; i++)
        if (head->classes[i] != PPN_CLASS_ANY)
            s = seq(b, s, seq(b, load(b, PPN_REGION_STATE, R1, R_STATE,
                                      (int32_t)(PPN_STATE_ARGS + 8u * i)),
                              class_check(g, R1, params[i], head->classes[i], R2, R3)));
    if (head->range != PPN_CLASS_ANY || head->range_from_parameter) {
        const TcIsaStmt *owed = head->range_from_parameter
            ? seq(b, load(b, PPN_REGION_STATE, R1, R_STATE,
                          (int32_t)(PPN_STATE_ARGS + 8u * head->range_parameter)),
                  class_of(g, R1, R1, params[head->range_parameter], R2, R3))
            : imm(b, R1, head->range);
        s = seq(b, s, seq(b, owed,
                      seq(b, load(b, PPN_REGION_FRAMES, R2, R4, PPN_FRAME_OWED),
                      seq(b, cmp_imm(b, TISA_CMP_EQ, R3, R2, PPN_CLASS_ANY),
                          when(b, R3, store(b, PPN_REGION_FRAMES, R4, PPN_FRAME_OWED, R1),
                               seq(b, cmp(b, TISA_CMP_NE, R3, R2, R1),
                                   when(b, R3, decline(g), NULL)))))));
    }
    for (uint32_t i = 0u; i < head->arity; i++)
        s = seq(b, s, seq(b, load(b, PPN_REGION_STATE, R1, R_STATE,
                                  (int32_t)(PPN_STATE_ARGS + 8u * i)),
                          store(b, PPN_REGION_STATE, R_STATE, match_disp(i), R1)));
    bool first_match = true;
    for (uint32_t k = 0u; k < head->equation_count && first_match; k++) {
        const PpnEquation *x = &v->equations[head->first_equation + k];
        first_match = !x->guarded;
        for (uint32_t j = k + 1u; j < head->equation_count && first_match; j++)
            first_match = disjoint(v, x, &v->equations[head->first_equation + j]);
    }
    if (first_match) {
        s = seq(b, s, seq(b, imm(b, R1, PPN_NONE),
                          store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_CHOSEN, R1)));
        for (uint32_t k = 0u; k < head->equation_count; k++) {
            uint32_t e = head->first_equation + k;
            const PpnEquation *eq = &v->equations[e];
            g->body = e;
            const TcIsaStmt *entered = seq(b, clear_unbound(g, e),
                seq(b, imm(b, R1, eq->first_code),
                    store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_CHOSEN, R1)));
            const TcIsaStmt *attempt = match_program(g, &v->match_ops[eq->first_op],
                                                     eq->op_count, params, eq->arity,
                                                     entered, NULL);
            s = seq(b, s, k == 0u ? attempt
                : seq(b, seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_CHOSEN),
                             cmp_imm(b, TISA_CMP_EQ, R2, R1, PPN_NONE)),
                      when(b, R2, attempt, NULL)));
        }
        return seq(b, s, seq(b, load(b, PPN_REGION_STATE, R_PC, R_STATE, PPN_STATE_CHOSEN),
                         seq(b, cmp_imm(b, TISA_CMP_EQ, R1, R_PC, PPN_NONE),
                             when(b, R1, decline(g), NULL))));
    }
    s = seq(b, s, seq(b, imm(b, R1, 0u),
                  seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_COUNT, R1),
                      store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_CHOSEN, R1))));
    for (uint32_t k = 0u; k < head->equation_count; k++) {
        uint32_t e = head->first_equation + k;
        const PpnEquation *eq = &v->equations[e];
        g->body = e;
        const TcIsaStmt *admitted =
            seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_COUNT),
            seq(b, imm(b, R2, 1u),
            seq(b, bin(b, TISA_BIN_ADD, TISA_TYPE_U64, R1, R1, R2),
            seq(b, store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_COUNT, R1),
            seq(b, imm(b, R1, k),
                store(b, PPN_REGION_STATE, R_STATE, PPN_STATE_CHOSEN, R1))))));
        if (eq->guarded)
            admitted = seq(b, guard(g, eq), when(b, R3, admitted, NULL));
        s = seq(b, s, match_program(g, &v->match_ops[eq->first_op], eq->op_count,
                                    params, eq->arity, admitted, NULL));
    }
    /* exactly one: bind its locals again and run its body */
    const TcIsaStmt *chosen = decline(g);
    for (uint32_t k = head->equation_count; k-- > 0u;) {
        uint32_t e = head->first_equation + k;
        const PpnEquation *eq = &v->equations[e];
        g->body = e;
        const TcIsaStmt *enter = match_program(g, &v->match_ops[eq->first_op], eq->op_count,
                                               params, eq->arity,
                                               seq(b, clear_unbound(g, e),
                                                   set_pc(g, eq->first_code)),
                                               decline(g));
        chosen = seq(b, seq(b, load(b, PPN_REGION_STATE, R2, R_STATE, PPN_STATE_CHOSEN),
                            cmp_imm(b, TISA_CMP_EQ, R3, R2, k)),
                     when(b, R3, enter, chosen));
    }
    return seq(b, s, seq(b, seq(b, load(b, PPN_REGION_STATE, R1, R_STATE, PPN_STATE_COUNT),
                                cmp_imm(b, TISA_CMP_EQ, R3, R1, 1u)),
                         when(b, R3, chosen, decline(g))));
}

/* The arms of the dispatch, ids ascending, as a balanced tree of compares
 * on R_PC; an id no arm has declines. */
static const TcIsaStmt *dispatch_tree(Gen *g, const uint64_t *ids,
                                      const TcIsaStmt *const *arms, size_t n) {
    Builder *b = g->b;
    if (n <= 3u) {
        const TcIsaStmt *d = decline(g);
        for (size_t i = n; i-- > 0u;)
            d = seq(b, cmp_imm(b, TISA_CMP_EQ, R1, R_PC, ids[i]), when(b, R1, arms[i], d));
        return d;
    }
    size_t mid = n / 2u;
    return seq(b, cmp_imm(b, TISA_CMP_ULT, R1, R_PC, ids[mid]),
               when(b, R1, dispatch_tree(g, ids, arms, mid),
                    dispatch_tree(g, ids + mid, arms + mid, n - mid)));
}

static bool generate(PpnImage *im, const PpnView *v, const Types *t,
                     uint32_t width, char *why, size_t why_size) {
    Builder b = {.image = im};
    bool *leader = calloc(v->code_len + 1u, sizeof(*leader));
    if (!leader) {
        snprintf(why, why_size, "out of memory");
        return false;
    }
    leader[v->root_code] = true;
    for (size_t e = 0u; e < v->equation_len; e++)
        leader[v->equations[e].first_code] = true;
    for (size_t c = 0u; c < v->code_len; c++) {
        const PpnCode *code = &v->code[c];
        if (t->body_of[c] == UINT32_MAX)
            continue;
        bool branches = code->kind == PPN_BRANCH || code->kind == PPN_BRANCH_BINARY ||
            code->kind == PPN_JUMP ||
            (code->kind == PPN_MATCH && code->target != PPN_NO_TARGET);
        if (branches)
            leader[code->target] = true;
        if (branches || code->kind == PPN_CALL)
            leader[c + 1u] = true;
        if (code->kind == PPN_CALL)
            leader[c] = true;
    }
    Gen g = {.b = &b, .v = v, .t = t, .leader = leader,
             .frame_bytes = 8u * (PPN_FRAME_HEADER + width), .width = width};
    for (size_t h = 0u; h < v->head_len; h++)
        g.owes = g.owes || v->heads[h].range != PPN_CLASS_ANY ||
            v->heads[h].range_from_parameter;
    im->frame_bytes = g.frame_bytes;
    for (size_t c = 0u; c < v->code_len; c++)
        if (leader[c] && t->body_of[c] != UINT32_MAX) {
            uint64_t bytes = block_heap_bytes(&g, (uint32_t)c);
            if (bytes / 8u > im->block_words)
                im->block_words = (size_t)(bytes / 8u);
        }
    /* The dispatch: the code's blocks, then the heads' entries, by id. */
    size_t arm_len = 0u;
    uint64_t *ids = calloc(v->code_len + v->head_len + 1u, sizeof(*ids));
    const TcIsaStmt **arms = calloc(v->code_len + v->head_len + 1u, sizeof(*arms));
    if (!ids || !arms)
        b.failed = true;
    for (size_t c = 0u; !b.failed && c < v->code_len; c++) {
        if (!leader[c] || t->body_of[c] == UINT32_MAX)
            continue;
        ids[arm_len] = c;
        arms[arm_len++] = block(&g, (uint32_t)c);
    }
    for (size_t h = 0u; !b.failed && h < v->head_len; h++) {
        ids[arm_len] = entry_id(&g, (uint32_t)h);
        arms[arm_len++] = entry(&g, (uint32_t)h);
    }
    const TcIsaStmt *dispatch = b.failed ? NULL : dispatch_tree(&g, ids, arms, arm_len);
    free(ids);
    free(arms);
    TcIsaStmt *fund = node(&b);
    TcIsaStmt *loop = node(&b);
    if (fund && loop) {
        fund->kind = TISA_CTRL_FUND;
        fund->u.fund.units = 1u;
        loop->kind = TISA_CTRL_EARLY_EXIT_LOOP;
        loop->u.early_exit_loop.idx_reg = R_I;
        loop->u.early_exit_loop.bound_reg = R_N;
        loop->u.early_exit_loop.cont_reg = R_GO;
        loop->u.early_exit_loop.max_iters = PPN_SLICE;
        loop->u.early_exit_loop.body = seq(&b, fund, seq(&b, frame_pointer(&g), dispatch));
    }
    const TcIsaStmt *root =
        seq(&b, base(&b, R_FRAMES, PPN_REGION_FRAMES),
        seq(&b, base(&b, R_STATE, PPN_REGION_STATE),
        seq(&b, load(&b, PPN_REGION_STATE, R_PC, R_STATE, PPN_STATE_PC),
        seq(&b, load(&b, PPN_REGION_STATE, R_FRAME, R_STATE, PPN_STATE_FRAME),
        seq(&b, imm(&b, R_N, PPN_SLICE),
        seq(&b, imm_bool(&b, R_GO, true),
        seq(&b, loop,
        seq(&b, store(&b, PPN_REGION_STATE, R_STATE, PPN_STATE_PC, R_PC),
        seq(&b, store(&b, PPN_REGION_STATE, R_STATE, PPN_STATE_FRAME, R_FRAME),
            leave(&b, PPN_CODE_YIELD))))))))));
    free(leader);
    if (b.failed || !root) {
        snprintf(why, why_size, "the program could not be lowered");
        return false;
    }
    TcIsaProgram *p = &im->program;
    memset(p, 0, sizeof *p);
    p->name = "prepared_pure_native";
    p->nregions = 3u;
    p->regions[0] = (TcIsaRegionDecl){.region_id = PPN_REGION_FRAMES, .mode = TISA_CAP_RW,
                                      .min_byte_len = 8u, .align_bytes = 8u, .name = "frames"};
    p->regions[1] = (TcIsaRegionDecl){.region_id = PPN_REGION_STATE, .mode = TISA_CAP_RW,
                                      .min_byte_len = PPN_STATE_BYTES,
                                      .align_bytes = 8u, .name = "state"};
    p->regions[2] = (TcIsaRegionDecl){.region_id = PPN_REGION_HEAP, .mode = TISA_CAP_RW,
                                      .min_byte_len = 8u, .align_bytes = 8u, .name = "heap"};
    p->root = root;
    TcIsaAdmission admission;
    if (!tisa_admit(p, &admission, why, why_size))
        return false;
    if (!tisa_x86_emit(p, &im->code, why, why_size))
        return false;
    return tisa_x86_load(&im->code, why, why_size);
}

PpnImage *ppn_compile(const PpnView *v, char *why, size_t why_size) {
    if (!v || v->code_len == 0u || v->root_code >= v->code_len ||
        v->root_arguments > PPN_MAX_ARITY) {
        snprintf(why, why_size, "no code");
        return NULL;
    }
    uint32_t width = v->root_registers;
    for (size_t e = 0u; e < v->equation_len; e++)
        if (v->equations[e].registers > width)
            width = v->equations[e].registers;
    for (size_t h = 0u; h < v->head_len; h++)
        if (v->heads[h].arity > PPN_MAX_ARITY) {
            snprintf(why, why_size, "a head of more than %u arguments", PPN_MAX_ARITY);
            return NULL;
        }
    if (width == 0u)
        width = 1u;
    size_t bodies = v->equation_len + 1u;
    Types t = {
        .view = v, .width = width,
        .params = calloc(v->head_len * PPN_MAX_ARITY + 1u, sizeof(PpnType)),
        .results = calloc(v->head_len + 1u, sizeof(PpnType)),
        .regs = calloc(bodies * width, sizeof(PpnType)),
        .body_of = calloc(v->code_len + 1u, sizeof(uint32_t)),
    };
    PpnImage *im = calloc(1u, sizeof(*im));
    bool ok = im && t.params && t.results && t.regs && t.body_of;
    if (!ok)
        snprintf(why, why_size, "out of memory");
    if (ok && !(ok = assign_bodies(v, t.body_of)))
        snprintf(why, why_size, "code shared by two bodies, or out of range");
    if (ok && !(ok = infer(&t)))
        snprintf(why, why_size, "register types do not settle");
    ok = ok && check_types(&t, why, why_size);
    if (ok) {
        im->root_code = v->root_code;
        im->root_registers = v->root_registers;
        im->root_argument_base = v->root_argument_base;
        im->root_arguments = v->root_arguments;
        im->root_type = t.results[v->head_len];
        for (uint32_t i = 0u; i < v->root_arguments; i++)
            im->argument_types[i] =
                t.regs[(size_t)v->equation_len * width + v->root_argument_base + i];
        im->width = width;
        im->code_len = v->code_len;
        ok = heap_reserve(&im->heap, v->constants.len + 512u);
        if (ok && v->constants.len > 0u)
            memcpy(im->heap.words, v->constants.words, 8u * v->constants.len);
        im->heap.len = im->constant_words = v->constants.len;
        ok = ok && generate(im, v, &t, width, why, why_size);
        if (ok) {
            /* the collector reads the types the code was lowered by */
            im->body_types = t.regs;
            im->body_of = t.body_of;
            t.regs = NULL;
            t.body_of = NULL;
        }
    }
    free(t.params);
    free(t.results);
    free(t.regs);
    free(t.body_of);
    if (!ok) {
        ppn_free(im);
        return NULL;
    }
    return im;
}

void ppn_free(PpnImage *im) {
    if (!im)
        return;
    tisa_x86_free(&im->code);
    for (size_t i = 0u; i < im->chunk_len; i++)
        free(im->chunks[i]);
    free(im->chunks);
    free(im->frames);
    free(im->state);
    ppn_heap_free(&im->heap);
    free(im->body_types);
    free(im->body_of);
    free(im);
}

const char *ppn_declined(const PpnImage *im) {
    return im && im->declined ? im->declined : "";
}

PpnType ppn_argument_type(const PpnImage *im, uint32_t i) {
    return im && i < im->root_arguments ? im->argument_types[i] : PPN_TYPE_UNKNOWN;
}

/* ---- the heap ------------------------------------------------------------ */

static bool heap_reserve(PpnHeap *heap, size_t words) {
    if (words <= heap->cap)
        return true;
    size_t cap = heap->cap ? heap->cap : 512u;
    while (cap < words) {
        if (cap > ((size_t)1u << 31))
            return false;
        cap *= 2u;
    }
    uint64_t *grown = realloc(heap->words, cap * sizeof(*grown));
    if (!grown)
        return false;
    heap->words = grown;
    heap->cap = cap;
    return true;
}

bool ppn_heap_expression(PpnHeap *heap, uint32_t len, const uint64_t *elements,
                         uint64_t *word_out) {
    if (!heap || !heap_reserve(heap, heap->len + len + 1u))
        return false;
    uint64_t *record = heap->words + heap->len;
    record[0] = len;
    if (len > 0u)
        memcpy(record + 1, elements, 8u * (size_t)len);
    *word_out = (uint64_t)(8u * heap->len) | PPN_WORD_EXPRESSION;
    heap->len += (size_t)len + 1u;
    return true;
}

bool ppn_heap_read(const PpnHeap *heap, uint64_t word, uint32_t *len_out,
                   const uint64_t **elements_out) {
    if (!heap || !ppn_word_is_expression(word))
        return false;
    size_t at = (size_t)((word - PPN_WORD_EXPRESSION) / 8u);
    if (at >= heap->len || at + 1u + heap->words[at] > heap->len)
        return false;
    *len_out = (uint32_t)heap->words[at];
    *elements_out = heap->words + at + 1u;
    return true;
}

void ppn_heap_free(PpnHeap *heap) {
    if (!heap)
        return;
    free(heap->words);
    memset(heap, 0, sizeof(*heap));
}

PpnHeap *ppn_begin(PpnImage *im) {
    if (!im)
        return NULL;
    im->heap.len = im->constant_words;
    return &im->heap;
}

/* ---- collection ---------------------------------------------------------- */

#define PPN_FORWARDED (UINT64_C(1) << 63)

/* *slot's expression, moved from `from` to `to` once (its old header then
 * names where); constants stay. */
static void forward(uint64_t *slot, PpnHeap *from, PpnHeap *to, size_t constants) {
    uint64_t word = *slot;
    if (!ppn_word_is_expression(word))
        return;
    size_t at = (size_t)((word - PPN_WORD_EXPRESSION) / 8u);
    if (at < constants)
        return;
    uint64_t header = from->words[at];
    if (header & PPN_FORWARDED) {
        *slot = 8u * (header & ~PPN_FORWARDED) | PPN_WORD_EXPRESSION;
        return;
    }
    size_t moved = to->len;
    memcpy(to->words + moved, from->words + at, 8u * (size_t)(header + 1u));
    to->len += (size_t)header + 1u;
    from->words[at] = PPN_FORWARDED | moved;
    *slot = 8u * moved | PPN_WORD_EXPRESSION;
}

/* A copying collection of the heap at a block's start, where every value a
 * run still needs is in a frame's register: the frames from the current
 * one back to the root, each read as the body it runs (the current one
 * from the code index, a caller from the code its callee returns to) and
 * its term registers forwarded; then the copied records' elements.  The
 * heap then has room for twice what survived and one block's records. */
static bool collect(PpnImage *im) {
    uint64_t *state = (uint64_t *)im->state;
    PpnHeap to = {0};
    if (!heap_reserve(&to, im->heap.len))
        return false;
    memcpy(to.words, im->heap.words, 8u * im->constant_words);
    to.len = im->constant_words;
    uint64_t pc = state[PPN_STATE_PC / 8u];
    uint64_t frame = state[PPN_STATE_FRAME / 8u];
    if (pc >= im->code_len) {
        free(to.words);
        return false;
    }
    uint32_t body = im->body_of[pc];
    for (;;) {
        uint64_t *registers = (uint64_t *)(im->frames + frame);
        if (body == UINT32_MAX || frame + im->frame_bytes > im->frames_len) {
            free(to.words);
            return false;
        }
        for (uint32_t r = 0u; r < im->width; r++)
            if (im->body_types[(size_t)body * im->width + r] == PPN_TYPE_TERM)
                forward(&registers[PPN_FRAME_HEADER + r], &im->heap, &to,
                        im->constant_words);
        uint64_t returns_to = registers[0];
        if (returns_to == PPN_ROOT_RETURN)
            break;
        if (returns_to >= im->code_len) {
            free(to.words);
            return false;
        }
        body = im->body_of[returns_to];
        frame = registers[2];
    }
    for (size_t at = im->constant_words; at < to.len; at += (size_t)to.words[at] + 1u)
        for (uint64_t k = 0u; k < to.words[at]; k++)
            forward(&to.words[at + 1u + k], &im->heap, &to, im->constant_words);
    if (!heap_reserve(&to, 2u * to.len + im->block_words + 512u)) {
        free(to.words);
        return false;
    }
    ppn_heap_free(&im->heap);
    im->heap = to;
    state[PPN_STATE_HEAP_TOP / 8u] = 8u * to.len;
    return true;
}

/* ---- runs ---------------------------------------------------------------- */

PpnRunResult ppn_run(PpnImage *im, const uint64_t *arguments,
                     uint32_t argument_count, bool (*poll)(void *),
                     void *poll_context, uint64_t *value_out,
                     PpnType *type_out) {
    if (!im || argument_count != im->root_arguments)
        return PPN_RUN_DECLINED;
    im->declined = NULL;
    size_t root_bytes = (size_t)8u * (PPN_FRAME_HEADER + im->root_registers);
    size_t want = im->frame_bytes * 64u;
    if (want < root_bytes)
        want = root_bytes;
    if (im->frames_len < want) {
        uint8_t *frames = realloc(im->frames, want);
        if (!frames) {
            im->declined = "no memory for the frames";
            return PPN_RUN_DECLINED;
        }
        im->frames = frames;
        im->frames_len = want;
    }
    if (!im->state) {
        im->state_len = PPN_STATE_BYTES;
        im->state = calloc(1u, im->state_len);
        if (!im->state)
            return PPN_RUN_DECLINED;
    }
    if (!heap_reserve(&im->heap, im->heap.len + 512u))
        return PPN_RUN_DECLINED;
    /* The root's frame: its registers clear, the entry arguments above its
     * locals. */
    memset(im->frames, 0, root_bytes);
    uint64_t *frame = (uint64_t *)im->frames;
    frame[0] = PPN_ROOT_RETURN;
    for (uint32_t i = 0u; i < argument_count; i++)
        frame[PPN_FRAME_HEADER + im->root_argument_base + i] = arguments[i];
    uint64_t *state = (uint64_t *)im->state;
    state[PPN_STATE_PC / 8u] = im->root_code;
    state[PPN_STATE_FRAME / 8u] = 0u;
    state[PPN_STATE_TOP / 8u] = im->frame_bytes > root_bytes ? im->frame_bytes : root_bytes;
    state[PPN_STATE_HEAP_TOP / 8u] = 8u * im->heap.len;
    /* A cancellation already pending wins, as before the register code's
     * first step. */
    if (poll && poll(poll_context))
        return PPN_RUN_INTERRUPTED;
    for (;;) {
        TcIsaRuntimeFrame f;
        uint64_t regs[TISA_NUM_VREGS] = {0};
        memset(&f, 0, sizeof f);
        f.s0_ptr = (uint64_t)(uintptr_t)im->frames;
        f.s0_len = im->frames_len;
        f.s1_ptr = (uint64_t)(uintptr_t)im->state;
        f.s1_len = im->state_len;
        f.s2_ptr = (uint64_t)(uintptr_t)im->heap.words;
        f.s2_len = 8u * im->heap.cap;
        f.fuel_budget = UINT64_MAX;
        regs[0] = (uint64_t)(uintptr_t)&f;
        TcIsaOutcome outcome = tisa_x86_run(&im->program, &im->code, &f, regs);
        im->heap.len = (size_t)(state[PPN_STATE_HEAP_TOP / 8u] / 8u);
        if (outcome != TISA_OUTCOME_FIRED) {
            im->declined = "the native code stopped short (a region bound or fuel)";
            return PPN_RUN_DECLINED;
        }
        switch (f.ret_val) {
        case PPN_CODE_ANSWER:
            *value_out = state[PPN_STATE_VALUE / 8u];
            *type_out = im->root_type;
            return PPN_RUN_ANSWER;
        case PPN_CODE_EMPTY:
            return PPN_RUN_EMPTY;
        case PPN_CODE_ROOM: {
            if (im->frames_len > ((size_t)1u << 30))
                return PPN_RUN_DECLINED;
            uint8_t *frames = realloc(im->frames, im->frames_len * 2u);
            if (!frames)
                return PPN_RUN_DECLINED;
            im->frames = frames;
            im->frames_len *= 2u;
            break;
        }
        case PPN_CODE_HEAP:
            if (!collect(im)) {
                im->declined = "the heap could not be collected";
                return PPN_RUN_DECLINED;
            }
            break;
        case PPN_CODE_YIELD:
            if (poll && poll(poll_context))
                return PPN_RUN_INTERRUPTED;
            break;
        case PPN_CODE_DECLINE:
            im->declined = "the program declined (as the register code would, or a value left the fragment)";
            return PPN_RUN_DECLINED;
        default:
            im->declined = "the run returned an unknown code";
            return PPN_RUN_DECLINED;
        }
    }
}
