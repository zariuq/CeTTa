#ifndef CETTA_PREPARED_PURE_NATIVE_H
#define CETTA_PREPARED_PURE_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A prepared pure program's register code as native code (t-ISA p1 emitted
 * by the in-house x86 emitter), for the programs over exact integers, truth
 * values, symbols and expressions built from them, whose calls select their
 * equation by patterns and scalar guards.
 *
 * The prepared machine (prepared_pure_machine.c) describes a program in the
 * view below, or refuses; this module lowers the view and runs it.  A run
 * answers what the register code answers, gives no answer where it gives
 * none, and declines wherever the register code would decline or a value
 * leaves the fragment (an integer result that is not a 64-bit word, a
 * comparison of two distinct expressions): the register code then runs. */

typedef enum {
    PPN_TYPE_UNKNOWN = 0,
    PPN_TYPE_INT,     /* a 64-bit word */
    PPN_TYPE_BOOL,    /* 0 or 1 */
    PPN_TYPE_TERM,    /* a term word, below */
} PpnType;

/* A term word: an integer of 63 bits (v << 1), a symbol (id << 3 | 1), a
 * truth value (b << 3 | 5), or an expression, the byte offset of its record
 * in the heap (a multiple of 8) | 3.  A record is its length n, then its n
 * elements' words. */
enum {
    PPN_WORD_SYMBOL = 1u, PPN_WORD_EXPRESSION = 3u, PPN_WORD_BOOL = 5u,
};
#define PPN_INT_WORD_MIN (-(INT64_C(1) << 62))
#define PPN_INT_WORD_MAX ((INT64_C(1) << 62) - 1)

static inline bool ppn_word_from_int(int64_t value, uint64_t *word) {
    if (value < PPN_INT_WORD_MIN || value > PPN_INT_WORD_MAX)
        return false;
    *word = (uint64_t)value << 1;
    return true;
}
static inline uint64_t ppn_word_from_symbol(uint32_t id) {
    return (uint64_t)id << 3 | PPN_WORD_SYMBOL;
}
static inline uint64_t ppn_word_from_bool(bool value) {
    return (uint64_t)value << 3 | PPN_WORD_BOOL;
}
static inline bool ppn_word_is_int(uint64_t word) { return (word & 1u) == 0u; }
static inline bool ppn_word_is_symbol(uint64_t word) { return (word & 7u) == PPN_WORD_SYMBOL; }
static inline bool ppn_word_is_bool(uint64_t word) { return (word & 7u) == PPN_WORD_BOOL; }
static inline bool ppn_word_is_expression(uint64_t word) {
    return (word & 7u) == PPN_WORD_EXPRESSION;
}
static inline int64_t ppn_word_int(uint64_t word) { return (int64_t)word >> 1; }
static inline uint32_t ppn_word_symbol(uint64_t word) { return (uint32_t)(word >> 3); }
static inline bool ppn_word_bool(uint64_t word) { return (word >> 3) != 0u; }

/* A register of the current activation, or a literal: an integer or truth
 * value as such, or a term word (a symbol). */
typedef struct {
    bool literal;
    PpnType literal_type;
    uint32_t reg;
    int64_t value;
} PpnOperand;

typedef enum {
    PPN_MOVE = 0,     /* dst := a */
    PPN_BINARY,       /* dst := a op b */
    PPN_BUILD,        /* dst := the expression of args[first_arg, +argc) */
    PPN_MATCH,        /* a against match ops [first_op, +op_count), binding
                         locals; a mismatch continues at target, or gives no
                         answer where target is PPN_NO_TARGET */
    PPN_BRANCH,       /* a false: continue at target */
    PPN_BRANCH_BINARY,/* (a op b) false: continue at target */
    PPN_JUMP,         /* continue at target */
    PPN_CALL,         /* dst := callee(args) */
    PPN_TAIL_CALL,    /* answer callee(args) */
    PPN_RETURN,       /* answer a */
    PPN_FAIL,         /* no answer */
    PPN_DECLINE,      /* the register code declines */
} PpnKind;
#define PPN_NO_TARGET UINT32_MAX

/* Arithmetic, then comparisons.  REM is the truncating remainder (sign of
 * the dividend), FMOD the floor modulo (sign of the divisor); a zero divisor
 * declines.  EQ and the orders compare integers; SAME compares any two
 * values as atoms (two distinct expressions decline); SAME_TYPED is HE's
 * equality, which also checks that both values have one type: it is lowered
 * only over two integers or two truth values. */
typedef enum {
    PPN_ADD = 0, PPN_SUB, PPN_MUL, PPN_REM, PPN_FMOD,
    PPN_EQ, PPN_SAME, PPN_SAME_TYPED, PPN_LT, PPN_GT, PPN_LE, PPN_GE,
} PpnOp;

typedef struct {
    uint8_t kind;   /* PpnKind */
    uint8_t op;     /* PpnOp, for BINARY and BRANCH_BINARY */
    bool returns;   /* a value instruction that answers for its body */
    uint32_t dst;
    uint32_t target;
    uint32_t callee;     /* a head */
    uint32_t first_arg;  /* args[first_arg, +argc), for calls and BUILD */
    uint32_t argc;
    uint32_t first_op;   /* match ops, for MATCH */
    uint32_t op_count;
    PpnOperand a, b;
} PpnCode;

/* A pattern as a program over match registers: an equation's arguments are
 * registers 0..arity-1, a MATCH's value register 0. */
typedef enum {
    PPN_MATCH_BIND = 0, /* local `operand` := register `source` */
    PPN_MATCH_ATOM,     /* register `source` equals `literal` */
    PPN_MATCH_EXPR,     /* register `source` is an expression of `length`
                           elements, which load into registers `operand`
                           onward */
    PPN_MATCH_SAME,     /* register `source` equals local `operand` */
} PpnMatchKind;

typedef struct {
    uint8_t kind;   /* PpnMatchKind */
    uint32_t source;
    uint32_t operand;
    uint32_t length;
    PpnOperand literal;
} PpnMatchOp;

#define PPN_MATCH_REGISTERS 64u
/* The most arguments a head of the fragment takes. */
#define PPN_MAX_ARITY 16u

typedef struct {
    uint32_t arity;
    uint32_t first_op;   /* match_ops[first_op, +op_count) over the arguments */
    uint32_t op_count;
    uint32_t first_code;
    uint32_t registers;
    /* The scalar guard over its locals, if any: a op b must be guard_truth. */
    bool guarded;
    uint8_t guard_op;
    PpnOperand guard_a, guard_b;
    bool guard_truth;
} PpnEquation;

/* The value class a typed head asks of an argument, or owes for its answer,
 * as HE's cast decides it by kind: an integer is a Number, a truth value a
 * Bool; KNOWN is a value of some class (a parameter written as a type
 * variable).  A value outside the class declines, as the register code
 * does. */
typedef enum {
    PPN_CLASS_ANY = 0,
    PPN_CLASS_NUMBER,
    PPN_CLASS_BOOL,
    PPN_CLASS_KNOWN,
} PpnClass;

typedef struct {
    uint32_t arity;
    uint32_t first_equation;
    uint32_t equation_count;
    /* A typed head: the class each argument must have on entry, and the
     * class its activation owes at return: `range`, or with
     * range_from_parameter the class of argument range_parameter.  A tail
     * call keeps the obligation of the activation it replaces. */
    uint8_t classes[PPN_MAX_ARITY];
    uint8_t range;
    bool range_from_parameter;
    uint32_t range_parameter;
} PpnHead;

/* A heap of expression records, word by word. */
typedef struct {
    uint64_t *words;
    size_t len, cap;
} PpnHeap;

/* An expression record of `length` elements at the heap's end; its word. */
bool ppn_heap_expression(PpnHeap *heap, uint32_t length,
                         const uint64_t *elements, uint64_t *word_out);
/* An expression word's length and elements, or false. */
bool ppn_heap_read(const PpnHeap *heap, uint64_t word, uint32_t *length_out,
                   const uint64_t **elements_out);
void ppn_heap_free(PpnHeap *heap);

typedef struct {
    PpnCode *code;
    size_t code_len;
    PpnOperand *args;
    size_t arg_len;
    PpnMatchOp *match_ops;
    size_t match_op_len;
    PpnEquation *equations;
    size_t equation_len;
    PpnHead *heads;
    size_t head_len;
    uint32_t root_code;
    uint32_t root_registers;
    uint32_t root_argument_base; /* entry argument i is root register base + i */
    uint32_t root_arguments;
    /* The entry arguments' types, as the host can supply them (INT where
     * every one is an integer, TERM where some are not). */
    PpnType root_argument_types[16];
    /* The records of the program's literal expressions, whose words its
     * operands hold: every run's heap begins with them. */
    PpnHeap constants;
} PpnView;

typedef struct PpnImage PpnImage;

/* Lowers the view, or NULL with a reason. */
PpnImage *ppn_compile(const PpnView *view, char *why, size_t why_size);
void ppn_free(PpnImage *image);

/* The type the root's entry argument i takes, and the root's answer's. */
PpnType ppn_argument_type(const PpnImage *image, uint32_t i);

/* A run: ppn_begin empties the image's heap but for the program's
 * constants and returns it; the host places the entry arguments'
 * expressions there; ppn_run runs the root; the answer's expressions are
 * read from the heap until the next ppn_begin. */
PpnHeap *ppn_begin(PpnImage *image);

/* Why the image's last run declined, for debugging. */
const char *ppn_declined(const PpnImage *image);

typedef enum {
    PPN_RUN_ANSWER = 0,
    PPN_RUN_EMPTY,
    PPN_RUN_DECLINED,
    PPN_RUN_INTERRUPTED,
} PpnRunResult;

/* Runs the root on its entry arguments, each in its type's representation
 * (an integer, 0 or 1, or a term word).  An answer is in the representation
 * of the root's type (*type_out).  poll, when not NULL, is called between
 * bounded slices of the run; its true interrupts it. */
PpnRunResult ppn_run(PpnImage *image, const uint64_t *arguments,
                     uint32_t argument_count, bool (*poll)(void *),
                     void *poll_context, uint64_t *value_out,
                     PpnType *type_out);

#endif
