/* Linear real arithmetic for the SMT personality: the general simplex over
 * bounds of Dutertre and de Moura (a fast linear-arithmetic solver for
 * DPLL(T)), exact.
 *
 * Variables 0 .. nvars-1 are the problem's reals.  A linear form over them
 * is a further variable, a slack, defined by the form; bounds are asserted
 * on any variable, each with a reason (the caller's literal), and are undone
 * to an earlier height.  A check either finds values meeting every bound or
 * a conflict: reasons whose bounds, weighed by nonnegative Farkas
 * coefficients, sum to 0 <= c with c < 0 (or 0 < 0 where a strict bound has
 * weight).  Strict bounds are bounds over delta-rationals, c + k*delta for
 * a positive delta small enough, which the values make concrete.
 *
 * Numbers cross the interface as exact text: an integer "-3" or a fraction
 * "7/2".  With GMP they are arbitrary rationals; without it, 64-bit
 * fractions, and an operation that would overflow makes the call fail
 * (LRA_ERROR), never a wrong answer.
 *
 * lra_farkas_check is independent of the solver: it checks a set of bounds
 * over linear forms and their coefficients, as a certificate checker does. */
#ifndef CETTA_LRA_SIMPLEX_V1_H
#define CETTA_LRA_SIMPLEX_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct LraSolver LraSolver;

typedef enum {
    LRA_CONFLICT = 0,
    LRA_OK = 1,
    LRA_ERROR = -1,   /* allocation, a malformed number, or an overflow */
} LraResult;

LraSolver *lra_new(uint32_t nvars);
void lra_free(LraSolver *lra);

/* The variable standing for sum coefs[i] * x[vars[i]]: the variable itself
 * for a single coefficient 1, the slack of an equal form already made, else
 * a new slack.  Forms are made before the first bound.  -1 on an error. */
int32_t lra_form(LraSolver *lra, const uint32_t *vars, const char *const *coefs,
                 uint32_t n);

/* var <= bound (upper) or var >= bound (lower), strict or not, because of
 * `reason`.  LRA_CONFLICT when it crosses the opposite bound; the
 * explanation is then those two. */
LraResult lra_assert(LraSolver *lra, uint32_t var, bool upper, const char *bound,
                     bool strict, int32_t reason);

/* Values meeting every bound (LRA_OK), or a conflict and its explanation. */
LraResult lra_check(LraSolver *lra);

/* After LRA_OK: the least value variable `var` takes under the bounds in
 * force, by the primal simplex with Bland's rule.  LRA_OK with
 * *unbounded false and the infimum c + k*delta as text in *c and *k (k > 0:
 * not attained, only approached); *unbounded true when var decreases
 * without end.  The values then meet every bound and give var that value
 * (lra_value).  That no smaller value is possible is the caller's to
 * certify: asserting var < c (var <= c when k > 0) makes lra_check
 * conflict, with a Farkas explanation. */
LraResult lra_minimize(LraSolver *lra, uint32_t var, bool *unbounded, char **c, char **k);

/* The last conflict: its reasons and their Farkas coefficients, as text
 * (owned by the solver until the next call). */
uint32_t lra_explanation(const LraSolver *lra, const int32_t **reasons,
                         const char *const **coefs);

/* Bounds are asserted on a stack: its height, and undoing to a height. */
uint32_t lra_height(const LraSolver *lra);
void lra_undo(LraSolver *lra, uint32_t height);

/* After LRA_OK: the value of variable `var` (0 <= var < nvars), with delta
 * made concrete; text the caller frees.  NULL on an error. */
char *lra_value(LraSolver *lra, uint32_t var);

/* The sign of sum coefs[i] * values[vars[i]] - bound (-1, 0 or 1) into
 * *sign, exactly; false for a malformed number or a variable without a
 * value. */
bool lra_compare(const uint32_t *vars, const char *const *coefs, uint32_t n,
                 const char *const *values, uint32_t nvalues, const char *bound,
                 int *sign);

/* Exact linear forms for a front end: sum c_i * x_i + constant, built by
 * adding terms, constants and scaled forms.  A number is exact text, or a
 * decimal "1.25" where lra_decimal turns one into exact text. */
typedef struct LraLinear LraLinear;

LraLinear *lra_linear_new(void);
LraLinear *lra_linear_copy(const LraLinear *lin);
void lra_linear_free(LraLinear *lin);
bool lra_linear_add_term(LraLinear *lin, uint32_t var, const char *coef);
bool lra_linear_add_constant(LraLinear *lin, const char *c);
/* dst += factor * src */
bool lra_linear_add_scaled(LraLinear *dst, const LraLinear *src, const char *factor);
/* Whether the form has no variable; its constant then into *value (owned
 * by the caller). */
bool lra_linear_is_constant(const LraLinear *lin, char **value);
/* The variables with nonzero coefficients, increasing, their coefficients
 * and the constant, all owned by the caller (lra_parts_free). */
bool lra_linear_parts(const LraLinear *lin, uint32_t **vars, char ***coefs,
                      uint32_t *n, char **constant);
void lra_parts_free(uint32_t *vars, char **coefs, uint32_t n, char *constant);
/* Exact text for a decimal ("-1.25" -> "-5/4") or an integer; 1 / x; -x;
 * NULL for malformed text, a zero divisor or an overflow. */
char *lra_decimal(const char *text);
char *lra_reciprocal(const char *x);
char *lra_negation(const char *x);

/* A bound of a certificate: sum coefs[i] * x[vars[i]] <= bound (upper) or
 * >= bound (lower), strict or not, with Farkas coefficient `weight`. */
typedef struct {
    const uint32_t *vars;
    const char *const *coefs;
    uint32_t n;
    bool upper;
    bool strict;
    const char *bound;
    const char *weight;
} LraFarkasBound;

/* Whether the weights are nonnegative, the weighted forms sum to zero, and
 * the weighted bounds sum below zero, or to zero with a strict bound of
 * positive weight: no reals meet all the bounds. */
bool lra_farkas_check(const LraFarkasBound *bounds, uint32_t n);

#endif
