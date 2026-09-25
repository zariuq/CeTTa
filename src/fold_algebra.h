#ifndef CETTA_FOLD_ALGEBRA_H
#define CETTA_FOLD_ALGEBRA_H

/* Integer fold steps and the algebra their consumers admit.
 *
 * A fold step is a pure integer expression in one item and the accumulator:
 * literals, `+`, `-`, `*`, floor `%`, `min` and `max`.  Classification finds the strongest
 * algebraic reading the expression has:
 *
 *  - AFFINE: acc -> scale(item) * acc + offset(item), with coefficient
 *    expressions free of the accumulator.  Admission is exactly
 *    accumulator degree at most one (`+`/`-` take the larger degree, `*`
 *    adds them).  Steps compose in execution order as the product of 2x2
 *    homogeneous matrices, so contiguous runs may be regrouped and a run of
 *    one constant item is a power.
 *  - MODULAR: the whole step is `(% e m)` for a literal m > 0 and an affine
 *    e.  Floor `%` by a positive literal is reduction onto [0, m), a ring
 *    homomorphism, so every step after the first is affine over Z/mZ.
 *  - TROPICAL_MIN / TROPICAL_MAX: acc -> min(acc + shift(item), bound(item))
 *    (max dually), with accumulator-free shift and an optional bound.  These
 *    are affine maps over the (min, +) or (max, +) semiring.  A step whose
 *    shift is the constant 0 is a semilattice step: it commutes and is
 *    idempotent, so its consumer observes only the set of items.
 *  - GENERAL: any other expression of the language, evaluated in order.
 *
 * An affine step whose scale is the constant 1 (a translation) or whose
 * offset is the constant 0 (a scaling) also commutes with every step of its
 * own kind, so a consumer of such steps observes only the occurrence bag.
 *
 * Evaluation is exact.  An intermediate outside int64 is reported as
 * OVERFLOW, and with GMP the fold continues on a bigint accumulator; a zero
 * divisor is UNDEFINED and the ordinary fold reports it.  Callers decline
 * before anything is published. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "search_machine.h"

typedef enum {
    CETTA_FOLD_OK = 0,
    /* The exact value leaves int64. */
    CETTA_FOLD_OVERFLOW,
    /* A zero divisor: the source reports an error, never a value. */
    CETTA_FOLD_UNDEFINED,
    CETTA_FOLD_UNSUPPORTED,
} CettaFoldStatus;

#define CETTA_FOLD_EXPR_CAPACITY 96u

typedef enum {
    CETTA_FOLD_EXPR_LITERAL = 0,
    CETTA_FOLD_EXPR_ITEM,
    CETTA_FOLD_EXPR_ACC,
    CETTA_FOLD_EXPR_ADD,
    CETTA_FOLD_EXPR_SUB,
    CETTA_FOLD_EXPR_MUL,
    CETTA_FOLD_EXPR_MOD,
    CETTA_FOLD_EXPR_MIN,
    CETTA_FOLD_EXPR_MAX,
} CettaFoldExprKind;

typedef struct {
    CettaFoldExprKind kind;
    int64_t literal;
    uint8_t left;
    uint8_t right;
} CettaFoldExprNode;

/* A node pool; children precede their parents. */
typedef struct {
    CettaFoldExprNode nodes[CETTA_FOLD_EXPR_CAPACITY];
    uint8_t len;
} CettaFoldExpr;

bool cetta_fold_expr_leaf(CettaFoldExpr *expr, CettaFoldExprKind kind,
                          int64_t literal, uint8_t *index_out);
bool cetta_fold_expr_binary(CettaFoldExpr *expr, CettaFoldExprKind kind,
                            uint8_t left, uint8_t right, uint8_t *index_out);

/* Floor division remainder, the sign of the divisor (PeTTa's `%`). */
bool cetta_fold_floor_mod(int64_t value, int64_t divisor, int64_t *out);

/* Exact evaluation of node `root` at (item, acc). */
CettaFoldStatus cetta_fold_expr_eval(const CettaFoldExpr *expr, uint8_t root,
                                     int64_t item, int64_t acc, int64_t *out);

typedef enum {
    CETTA_FOLD_STEP_GENERAL = 0,
    CETTA_FOLD_STEP_AFFINE,
    CETTA_FOLD_STEP_MODULAR,
    /* acc -> min(acc + shift, bound) or max(acc + shift, bound). */
    CETTA_FOLD_STEP_TROPICAL_MIN,
    CETTA_FOLD_STEP_TROPICAL_MAX,
} CettaFoldStepClass;

typedef struct {
    CettaFoldExpr source;
    uint8_t root;
    CettaFoldStepClass step_class;
    /* AFFINE and MODULAR: accumulator-free coefficient expressions. */
    CettaFoldExpr coefficients;
    uint8_t scale;
    uint8_t offset;
    int64_t modulus;
    bool uses_item;
    bool uses_acc;
    /* Scale is the constant 1 or offset the constant 0 (AFFINE only). */
    bool translation;
    bool scaling;
    /* TROPICAL: coefficient roots; `bounded` false means no bound.  A
     * shift of constant 0 makes the step a semilattice step. */
    uint8_t shift;
    uint8_t bound;
    bool bounded;
    bool semilattice;
    /* Item-degree at most one: coefficient = slope * item + intercept. */
    bool scale_linear;
    int64_t scale_slope;
    int64_t scale_intercept;
    bool offset_linear;
    int64_t offset_slope;
    int64_t offset_intercept;
} CettaFoldStep;

/* Classify the expression rooted at `root`.  Always succeeds for a
 * well-formed pool; the class may be GENERAL. */
bool cetta_fold_step_classify(const CettaFoldExpr *source, uint8_t root,
                              CettaFoldStep *step);

/* The observation contract of a fold consumer running this step. */
CettaObservationContract cetta_fold_step_contract(const CettaFoldStep *step);

/* One step: acc' = step(item, acc). */
CettaFoldStatus cetta_fold_step_apply(const CettaFoldStep *step, int64_t item,
                                      int64_t acc, int64_t *out);

/* The same step taken `times` times with one item.  AFFINE and MODULAR use
 * powers of the step's summary; GENERAL iterates. */
CettaFoldStatus cetta_fold_step_repeat(const CettaFoldStep *step, int64_t item,
                                       uint64_t times, int64_t acc,
                                       int64_t *out);

#if CETTA_BUILD_WITH_GMP
/* An accumulator that has left int64.  Every later step evaluates the
 * source expression exactly; items stay int64. */
typedef struct {
    bool active;
    mpz_t acc;
    mpz_t values[CETTA_FOLD_EXPR_CAPACITY];
} CettaFoldBig;

void cetta_fold_big_start(CettaFoldBig *big, int64_t acc);
void cetta_fold_big_clear(CettaFoldBig *big);
CettaFoldStatus cetta_fold_step_apply_big(const CettaFoldStep *step,
                                          int64_t item, CettaFoldBig *big);
#endif

#endif
