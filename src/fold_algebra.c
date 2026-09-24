#include "fold_algebra.h"

#include <string.h>

static bool fold_fit(__int128 value, int64_t *out) {
    if (value > (__int128)INT64_MAX || value < (__int128)INT64_MIN)
        return false;
    *out = (int64_t)value;
    return true;
}

bool cetta_fold_floor_mod(int64_t value, int64_t divisor, int64_t *out) {
    if (divisor == 0)
        return false;
    if (divisor == -1) {
        *out = 0;
        return true;
    }
    int64_t rem = value % divisor;
    if (rem != 0 && ((rem < 0) != (divisor < 0)))
        rem += divisor;
    *out = rem;
    return true;
}

bool cetta_fold_expr_leaf(CettaFoldExpr *expr, CettaFoldExprKind kind,
                          int64_t literal, uint8_t *index_out) {
    if (!expr || !index_out || expr->len >= CETTA_FOLD_EXPR_CAPACITY ||
        (kind != CETTA_FOLD_EXPR_LITERAL && kind != CETTA_FOLD_EXPR_ITEM &&
         kind != CETTA_FOLD_EXPR_ACC))
        return false;
    expr->nodes[expr->len] = (CettaFoldExprNode){
        .kind = kind,
        .literal = kind == CETTA_FOLD_EXPR_LITERAL ? literal : 0,
    };
    *index_out = expr->len++;
    return true;
}

bool cetta_fold_expr_binary(CettaFoldExpr *expr, CettaFoldExprKind kind,
                            uint8_t left, uint8_t right, uint8_t *index_out) {
    if (!expr || !index_out || expr->len >= CETTA_FOLD_EXPR_CAPACITY ||
        left >= expr->len || right >= expr->len ||
        (kind != CETTA_FOLD_EXPR_ADD && kind != CETTA_FOLD_EXPR_SUB &&
         kind != CETTA_FOLD_EXPR_MUL && kind != CETTA_FOLD_EXPR_MOD &&
         kind != CETTA_FOLD_EXPR_MIN && kind != CETTA_FOLD_EXPR_MAX))
        return false;
    expr->nodes[expr->len] = (CettaFoldExprNode){
        .kind = kind, .left = left, .right = right,
    };
    *index_out = expr->len++;
    return true;
}

CettaFoldStatus cetta_fold_expr_eval(const CettaFoldExpr *expr, uint8_t root,
                                     int64_t item, int64_t acc, int64_t *out) {
    if (!expr || !out || root >= expr->len)
        return CETTA_FOLD_UNSUPPORTED;
    /* Children precede parents, so one forward pass evaluates every node. */
    int64_t values[CETTA_FOLD_EXPR_CAPACITY];
    for (uint8_t index = 0u; index <= root; index++) {
        const CettaFoldExprNode *node = &expr->nodes[index];
        switch (node->kind) {
        case CETTA_FOLD_EXPR_LITERAL:
            values[index] = node->literal;
            break;
        case CETTA_FOLD_EXPR_ITEM:
            values[index] = item;
            break;
        case CETTA_FOLD_EXPR_ACC:
            values[index] = acc;
            break;
        case CETTA_FOLD_EXPR_ADD:
            if (!fold_fit((__int128)values[node->left] +
                          values[node->right], &values[index]))
                return CETTA_FOLD_OVERFLOW;
            break;
        case CETTA_FOLD_EXPR_SUB:
            if (!fold_fit((__int128)values[node->left] -
                          values[node->right], &values[index]))
                return CETTA_FOLD_OVERFLOW;
            break;
        case CETTA_FOLD_EXPR_MUL:
            if (!fold_fit((__int128)values[node->left] *
                          values[node->right], &values[index]))
                return CETTA_FOLD_OVERFLOW;
            break;
        case CETTA_FOLD_EXPR_MOD:
            if (!cetta_fold_floor_mod(values[node->left],
                                      values[node->right], &values[index]))
                return CETTA_FOLD_UNDEFINED;
            break;
        case CETTA_FOLD_EXPR_MIN:
            values[index] = values[node->left] < values[node->right]
                ? values[node->left] : values[node->right];
            break;
        case CETTA_FOLD_EXPR_MAX:
            values[index] = values[node->left] > values[node->right]
                ? values[node->left] : values[node->right];
            break;
        default:
            return CETTA_FOLD_UNSUPPORTED;
        }
    }
    *out = values[root];
    return CETTA_FOLD_OK;
}

/* ---- the affine compiler ------------------------------------------------
 * A compiled node is either constant (accumulator degree 0: one coefficient,
 * its value) or affine (degree 1: scale and offset).  The rules are those of
 * the checked compiler: a sum or difference is affine when either side is,
 * with the constant side's scale 0; a product of a constant and an affine
 * side scales both coefficients, keeping source argument order; the product
 * of two affine sides has degree two and is refused.  `%` is outside the
 * affine language. */

typedef struct {
    bool affine;
    uint8_t scale;
    uint8_t offset;
} FoldForm;

static bool fold_form_scale(CettaFoldExpr *coeff, const FoldForm *form,
                            uint8_t *out) {
    if (form->affine) {
        *out = form->scale;
        return true;
    }
    return cetta_fold_expr_leaf(coeff, CETTA_FOLD_EXPR_LITERAL, 0, out);
}

static bool fold_combine(CettaFoldExpr *coeff, CettaFoldExprKind op,
                         const FoldForm *left, const FoldForm *right,
                         FoldForm *out) {
    if (!left->affine && !right->affine) {
        out->affine = false;
        return cetta_fold_expr_binary(coeff, op, left->offset,
                                      right->offset, &out->offset);
    }
    if (op == CETTA_FOLD_EXPR_ADD || op == CETTA_FOLD_EXPR_SUB) {
        uint8_t left_scale = 0u;
        uint8_t right_scale = 0u;
        out->affine = true;
        return fold_form_scale(coeff, left, &left_scale) &&
               fold_form_scale(coeff, right, &right_scale) &&
               cetta_fold_expr_binary(coeff, op, left_scale, right_scale,
                                      &out->scale) &&
               cetta_fold_expr_binary(coeff, op, left->offset,
                                      right->offset, &out->offset);
    }
    if (op != CETTA_FOLD_EXPR_MUL || (left->affine && right->affine))
        return false;
    out->affine = true;
    if (!left->affine) {
        return cetta_fold_expr_binary(coeff, CETTA_FOLD_EXPR_MUL,
                                      left->offset, right->scale,
                                      &out->scale) &&
               cetta_fold_expr_binary(coeff, CETTA_FOLD_EXPR_MUL,
                                      left->offset, right->offset,
                                      &out->offset);
    }
    return cetta_fold_expr_binary(coeff, CETTA_FOLD_EXPR_MUL, left->scale,
                                  right->offset, &out->scale) &&
           cetta_fold_expr_binary(coeff, CETTA_FOLD_EXPR_MUL, left->offset,
                                  right->offset, &out->offset);
}

static bool fold_compile(const CettaFoldExpr *source, uint8_t root,
                         CettaFoldExpr *coeff, FoldForm *out) {
    FoldForm forms[CETTA_FOLD_EXPR_CAPACITY];
    for (uint8_t index = 0u; index <= root; index++) {
        const CettaFoldExprNode *node = &source->nodes[index];
        FoldForm *form = &forms[index];
        switch (node->kind) {
        case CETTA_FOLD_EXPR_LITERAL:
        case CETTA_FOLD_EXPR_ITEM:
            form->affine = false;
            if (!cetta_fold_expr_leaf(coeff, node->kind, node->literal,
                                      &form->offset))
                return false;
            break;
        case CETTA_FOLD_EXPR_ACC:
            form->affine = true;
            if (!cetta_fold_expr_leaf(coeff, CETTA_FOLD_EXPR_LITERAL, 1,
                                      &form->scale) ||
                !cetta_fold_expr_leaf(coeff, CETTA_FOLD_EXPR_LITERAL, 0,
                                      &form->offset))
                return false;
            break;
        case CETTA_FOLD_EXPR_ADD:
        case CETTA_FOLD_EXPR_SUB:
        case CETTA_FOLD_EXPR_MUL:
            if (!fold_combine(coeff, node->kind, &forms[node->left],
                              &forms[node->right], form))
                return false;
            break;
        default:
            return false;
        }
    }
    *out = forms[root];
    return true;
}

/* Item degree of an accumulator-free coefficient node, or -1 for `%`. */
static int fold_item_degree(const CettaFoldExpr *coeff, uint8_t root) {
    int degree[CETTA_FOLD_EXPR_CAPACITY];
    for (uint8_t index = 0u; index <= root; index++) {
        const CettaFoldExprNode *node = &coeff->nodes[index];
        switch (node->kind) {
        case CETTA_FOLD_EXPR_LITERAL:
            degree[index] = 0;
            break;
        case CETTA_FOLD_EXPR_ITEM:
            degree[index] = 1;
            break;
        case CETTA_FOLD_EXPR_ADD:
        case CETTA_FOLD_EXPR_SUB: {
            int left = degree[node->left];
            int right = degree[node->right];
            degree[index] = left < 0 || right < 0 ? -1
                : left > right ? left : right;
            break;
        }
        case CETTA_FOLD_EXPR_MUL: {
            int left = degree[node->left];
            int right = degree[node->right];
            degree[index] = left < 0 || right < 0 || left + right > 2
                ? -1 : left + right;
            break;
        }
        default:
            degree[index] = -1;
            break;
        }
    }
    return degree[root];
}

/* A coefficient of item degree at most one is slope * item + intercept. */
static bool fold_linear(const CettaFoldExpr *coeff, uint8_t root,
                        int64_t *slope, int64_t *intercept) {
    int degree = fold_item_degree(coeff, root);
    int64_t at_zero = 0;
    int64_t at_one = 0;
    if (degree < 0 || degree > 1 ||
        cetta_fold_expr_eval(coeff, root, 0, 0, &at_zero) != CETTA_FOLD_OK ||
        cetta_fold_expr_eval(coeff, root, 1, 0, &at_one) != CETTA_FOLD_OK ||
        !fold_fit((__int128)at_one - at_zero, slope))
        return false;
    *intercept = at_zero;
    return true;
}

/* ---- the tropical compiler -----------------------------------------------
 * For one semiring direction (`min` or `max`), a compiled node is either
 * accumulator-free (a coefficient) or a shifted accumulator
 * acc -> op(acc + shift, bound) with an optional bound.  Adding or
 * subtracting a coefficient shifts both parts; the semiring operation with a
 * coefficient tightens the bound.  The operation between two accumulator
 * terms, the other direction's operation, products and `%` are refused. */

typedef struct {
    bool has_acc;
    uint8_t shift;
    bool bounded;
    uint8_t bound;
} TropForm;

static bool trop_shift_by(CettaFoldExpr *coeff, CettaFoldExprKind op,
                          const TropForm *term, uint8_t amount,
                          TropForm *out) {
    *out = *term;
    return cetta_fold_expr_binary(coeff, op, term->shift, amount,
                                  &out->shift) &&
           (!term->bounded ||
            cetta_fold_expr_binary(coeff, op, term->bound, amount,
                                   &out->bound));
}

static bool trop_compile(const CettaFoldExpr *source, uint8_t root,
                         CettaFoldExprKind lattice, CettaFoldExpr *coeff,
                         TropForm *out) {
    TropForm forms[CETTA_FOLD_EXPR_CAPACITY];
    for (uint8_t index = 0u; index <= root; index++) {
        const CettaFoldExprNode *node = &source->nodes[index];
        TropForm *form = &forms[index];
        memset(form, 0, sizeof(*form));
        switch (node->kind) {
        case CETTA_FOLD_EXPR_LITERAL:
        case CETTA_FOLD_EXPR_ITEM:
            if (!cetta_fold_expr_leaf(coeff, node->kind, node->literal,
                                      &form->bound))
                return false;
            form->bounded = true;
            break;
        case CETTA_FOLD_EXPR_ACC:
            form->has_acc = true;
            if (!cetta_fold_expr_leaf(coeff, CETTA_FOLD_EXPR_LITERAL, 0,
                                      &form->shift))
                return false;
            break;
        case CETTA_FOLD_EXPR_ADD:
        case CETTA_FOLD_EXPR_SUB: {
            const TropForm *left = &forms[node->left];
            const TropForm *right = &forms[node->right];
            if (!left->has_acc && !right->has_acc) {
                if (!cetta_fold_expr_binary(coeff, node->kind, left->bound,
                                            right->bound, &form->bound))
                    return false;
                form->bounded = true;
            } else if (left->has_acc && !right->has_acc) {
                if (!trop_shift_by(coeff, node->kind, left, right->bound,
                                   form))
                    return false;
            } else if (node->kind == CETTA_FOLD_EXPR_ADD &&
                       !left->has_acc && right->has_acc) {
                if (!trop_shift_by(coeff, CETTA_FOLD_EXPR_ADD, right,
                                   left->bound, form))
                    return false;
            } else {
                return false;
            }
            break;
        }
        case CETTA_FOLD_EXPR_MIN:
        case CETTA_FOLD_EXPR_MAX: {
            if (node->kind != lattice)
                return false;
            const TropForm *left = &forms[node->left];
            const TropForm *right = &forms[node->right];
            if (!left->has_acc && !right->has_acc) {
                if (!cetta_fold_expr_binary(coeff, node->kind, left->bound,
                                            right->bound, &form->bound))
                    return false;
                form->bounded = true;
                break;
            }
            if (left->has_acc && right->has_acc)
                return false;
            const TropForm *term = left->has_acc ? left : right;
            const TropForm *limit = left->has_acc ? right : left;
            *form = *term;
            if (term->bounded) {
                if (!cetta_fold_expr_binary(coeff, lattice, term->bound,
                                            limit->bound, &form->bound))
                    return false;
            } else {
                form->bound = limit->bound;
                form->bounded = true;
            }
            break;
        }
        default:
            return false;
        }
    }
    *out = forms[root];
    return out->has_acc;
}

static bool fold_contains(const CettaFoldExpr *source, uint8_t root,
                          CettaFoldExprKind kind) {
    for (uint8_t index = 0u; index <= root; index++)
        if (source->nodes[index].kind == kind)
            return true;
    return false;
}

/* The tropical reading, when the expression uses exactly one lattice
 * operation and compiles under it. */
static void fold_classify_tropical(CettaFoldStep *step) {
    const CettaFoldExpr *source = &step->source;
    bool has_min = fold_contains(source, step->root, CETTA_FOLD_EXPR_MIN);
    bool has_max = fold_contains(source, step->root, CETTA_FOLD_EXPR_MAX);
    if (has_min == has_max)
        return;
    CettaFoldExprKind lattice = has_min ? CETTA_FOLD_EXPR_MIN
                                        : CETTA_FOLD_EXPR_MAX;
    CettaFoldExpr coeff = {0};
    TropForm form;
    if (!trop_compile(source, step->root, lattice, &coeff, &form))
        return;
    int64_t slope = 0;
    int64_t intercept = 0;
    step->coefficients = coeff;
    step->shift = form.shift;
    step->bounded = form.bounded;
    step->bound = form.bound;
    step->semilattice = fold_linear(&step->coefficients, step->shift, &slope,
                                    &intercept) &&
        slope == 0 && intercept == 0;
    step->step_class = has_min ? CETTA_FOLD_STEP_TROPICAL_MIN
                               : CETTA_FOLD_STEP_TROPICAL_MAX;
}

bool cetta_fold_step_classify(const CettaFoldExpr *source, uint8_t root,
                              CettaFoldStep *step) {
    if (!source || !step || root >= source->len)
        return false;
    memset(step, 0, sizeof(*step));
    step->source = *source;
    step->root = root;
    for (uint8_t index = 0u; index <= root; index++) {
        if (source->nodes[index].kind == CETTA_FOLD_EXPR_ITEM)
            step->uses_item = true;
        if (source->nodes[index].kind == CETTA_FOLD_EXPR_ACC)
            step->uses_acc = true;
    }
    step->step_class = CETTA_FOLD_STEP_GENERAL;

    const CettaFoldExprNode *top = &source->nodes[root];
    uint8_t affine_root = root;
    int64_t modulus = 0;
    if (top->kind == CETTA_FOLD_EXPR_MOD) {
        const CettaFoldExprNode *divisor = &source->nodes[top->right];
        if (divisor->kind != CETTA_FOLD_EXPR_LITERAL || divisor->literal <= 0)
            return true;
        modulus = divisor->literal;
        affine_root = top->left;
    }
    FoldForm form;
    CettaFoldExpr coeff = {0};
    if (!fold_compile(source, affine_root, &coeff, &form) ||
        (!form.affine && !fold_form_scale(&coeff, &form, &form.scale))) {
        if (modulus == 0)
            fold_classify_tropical(step);
        return true;
    }
    step->coefficients = coeff;
    step->scale = form.scale;
    step->offset = form.offset;
    step->modulus = modulus;
    step->step_class = modulus > 0 ? CETTA_FOLD_STEP_MODULAR
                                   : CETTA_FOLD_STEP_AFFINE;
    step->scale_linear = fold_linear(&step->coefficients, step->scale,
                                     &step->scale_slope,
                                     &step->scale_intercept);
    step->offset_linear = fold_linear(&step->coefficients, step->offset,
                                      &step->offset_slope,
                                      &step->offset_intercept);
    if (step->step_class == CETTA_FOLD_STEP_AFFINE) {
        step->translation = step->scale_linear && step->scale_slope == 0 &&
            step->scale_intercept == 1;
        step->scaling = step->offset_linear && step->offset_slope == 0 &&
            step->offset_intercept == 0;
    }
    return true;
}

CettaObservationContract cetta_fold_step_contract(const CettaFoldStep *step) {
    CettaObservationContract contract = {
        .demand = {.completion = CETTA_OBSERVATION_ORDERED_STREAM},
        .algebra = CETTA_OBSERVATION_ALGEBRA_EXACT_OCCURRENCES,
    };
    if (!step || step->step_class == CETTA_FOLD_STEP_GENERAL)
        return contract;
    if (step->semilattice) {
        contract.demand.completion = CETTA_OBSERVATION_COMPLETE_BAG;
        contract.algebra = CETTA_OBSERVATION_ALGEBRA_IDEMPOTENT_FOLD;
        return contract;
    }
    if (step->translation || step->scaling) {
        contract.demand.completion = CETTA_OBSERVATION_COMPLETE_BAG;
        contract.algebra = CETTA_OBSERVATION_ALGEBRA_COMMUTATIVE_FOLD;
        return contract;
    }
    contract.algebra = CETTA_OBSERVATION_ALGEBRA_AFFINE_FOLD;
    return contract;
}

/* The step's summary (scale, offset) at one item. */
static CettaFoldStatus fold_summary(const CettaFoldStep *step, int64_t item,
                                    int64_t *scale, int64_t *offset) {
    CettaFoldStatus status = cetta_fold_expr_eval(
        &step->coefficients, step->scale, item, 0, scale);
    if (status != CETTA_FOLD_OK)
        return status;
    return cetta_fold_expr_eval(&step->coefficients, step->offset, item, 0,
                                offset);
}

static int64_t fold_reduce(__int128 value, int64_t modulus) {
    __int128 rem = value % modulus;
    return (int64_t)(rem < 0 ? rem + modulus : rem);
}

CettaFoldStatus cetta_fold_step_apply(const CettaFoldStep *step, int64_t item,
                                      int64_t acc, int64_t *out) {
    if (!step || !out)
        return CETTA_FOLD_UNSUPPORTED;
    int64_t scale = 0;
    int64_t offset = 0;
    CettaFoldStatus status = CETTA_FOLD_OK;
    switch (step->step_class) {
    case CETTA_FOLD_STEP_AFFINE:
        /* A coefficient outside int64 does not prove the step's value
         * leaves it; the source evaluator decides. */
        status = fold_summary(step, item, &scale, &offset);
        if (status == CETTA_FOLD_OVERFLOW)
            return cetta_fold_expr_eval(&step->source, step->root, item, acc,
                                        out);
        if (status != CETTA_FOLD_OK)
            return status;
        return fold_fit((__int128)scale * acc + offset, out)
            ? CETTA_FOLD_OK : CETTA_FOLD_OVERFLOW;
    case CETTA_FOLD_STEP_MODULAR: {
        status = fold_summary(step, item, &scale, &offset);
        if (status == CETTA_FOLD_OVERFLOW)
            return cetta_fold_expr_eval(&step->source, step->root, item, acc,
                                        out);
        if (status != CETTA_FOLD_OK)
            return status;
        int64_t m = step->modulus;
        /* Reduction commutes with + and *: reduce every operand first so
         * the product stays inside 128 bits. */
        __int128 product = (__int128)fold_reduce(scale, m) *
            fold_reduce(acc, m);
        *out = fold_reduce(product + fold_reduce(offset, m), m);
        return CETTA_FOLD_OK;
    }
    case CETTA_FOLD_STEP_TROPICAL_MIN:
    case CETTA_FOLD_STEP_TROPICAL_MAX: {
        int64_t shift = 0;
        int64_t bound = 0;
        int64_t shifted = 0;
        status = cetta_fold_expr_eval(&step->coefficients, step->shift, item,
                                      0, &shift);
        if (status == CETTA_FOLD_OK && step->bounded)
            status = cetta_fold_expr_eval(&step->coefficients, step->bound,
                                          item, 0, &bound);
        if (status == CETTA_FOLD_OVERFLOW)
            return cetta_fold_expr_eval(&step->source, step->root, item, acc,
                                        out);
        if (status != CETTA_FOLD_OK)
            return status;
        if (!fold_fit((__int128)acc + shift, &shifted))
            return cetta_fold_expr_eval(&step->source, step->root, item, acc,
                                        out);
        if (step->bounded)
            shifted = step->step_class == CETTA_FOLD_STEP_TROPICAL_MIN
                ? (shifted < bound ? shifted : bound)
                : (shifted > bound ? shifted : bound);
        *out = shifted;
        return CETTA_FOLD_OK;
    }
    case CETTA_FOLD_STEP_GENERAL:
        return cetta_fold_expr_eval(&step->source, step->root, item, acc,
                                    out);
    }
    return CETTA_FOLD_UNSUPPORTED;
}

/* Affine summaries compose in execution order:
 * (a, b) then (c, d) = (c * a, c * b + d). */
static bool fold_compose(int64_t a, int64_t b, int64_t c, int64_t d,
                         int64_t modulus, int64_t *scale, int64_t *offset) {
    if (modulus > 0) {
        *scale = fold_reduce((__int128)c * a, modulus);
        *offset = fold_reduce((__int128)c * b + d, modulus);
        return true;
    }
    return fold_fit((__int128)c * a, scale) &&
           fold_fit((__int128)c * b + d, offset);
}

CettaFoldStatus cetta_fold_step_repeat(const CettaFoldStep *step, int64_t item,
                                       uint64_t times, int64_t acc,
                                       int64_t *out) {
    if (!step || !out)
        return CETTA_FOLD_UNSUPPORTED;
    if (times == 0u) {
        *out = acc;
        return CETTA_FOLD_OK;
    }
    if (step->step_class == CETTA_FOLD_STEP_TROPICAL_MIN ||
        step->step_class == CETTA_FOLD_STEP_TROPICAL_MAX) {
        /* k steps of acc -> op(acc + a, b): op(acc + k a, b + extreme of
         * 0 and (k - 1) a), the power of the tropical summary. */
        bool is_min = step->step_class == CETTA_FOLD_STEP_TROPICAL_MIN;
        int64_t shift = 0;
        int64_t bound = 0;
        CettaFoldStatus status = cetta_fold_expr_eval(
            &step->coefficients, step->shift, item, 0, &shift);
        if (status == CETTA_FOLD_OK && step->bounded)
            status = cetta_fold_expr_eval(&step->coefficients, step->bound,
                                          item, 0, &bound);
        if (status != CETTA_FOLD_OK)
            return status;
        __int128 moved = 0;
        __int128 spread = 0;
        if (__builtin_mul_overflow((__int128)shift, (__int128)times,
                                   &moved) ||
            __builtin_mul_overflow((__int128)shift, (__int128)(times - 1u),
                                   &spread))
            return CETTA_FOLD_OVERFLOW;
        __int128 result = moved + acc;
        if (step->bounded) {
            __int128 extra = is_min ? (spread < 0 ? spread : 0)
                                    : (spread > 0 ? spread : 0);
            __int128 limit = (__int128)bound + extra;
            result = is_min ? (result < limit ? result : limit)
                            : (result > limit ? result : limit);
        }
        return fold_fit(result, out) ? CETTA_FOLD_OK : CETTA_FOLD_OVERFLOW;
    }
    if (step->step_class == CETTA_FOLD_STEP_GENERAL) {
        for (uint64_t index = 0u; index < times; index++) {
            CettaFoldStatus status = cetta_fold_expr_eval(
                &step->source, step->root, item, acc, &acc);
            if (status != CETTA_FOLD_OK)
                return status;
        }
        *out = acc;
        return CETTA_FOLD_OK;
    }
    int64_t modulus = step->step_class == CETTA_FOLD_STEP_MODULAR
        ? step->modulus : 0;
    int64_t base_scale = 0;
    int64_t base_offset = 0;
    CettaFoldStatus summary = fold_summary(step, item, &base_scale,
                                           &base_offset);
    if (summary != CETTA_FOLD_OK)
        return summary;
    if (modulus > 0) {
        base_scale = fold_reduce(base_scale, modulus);
        base_offset = fold_reduce(base_offset, modulus);
    } else {
        /* Closed forms whose intermediate powers could leave int64 even
         * when the result does not. */
        if (base_scale == 1) {
            __int128 total = 0;
            if (__builtin_mul_overflow((__int128)base_offset,
                                       (__int128)times, &total))
                return CETTA_FOLD_OVERFLOW;
            return fold_fit(total + acc, out)
                ? CETTA_FOLD_OK : CETTA_FOLD_OVERFLOW;
        }
        if (base_scale == 0) {
            *out = base_offset;
            return CETTA_FOLD_OK;
        }
        if (base_offset == 0 && acc == 0) {
            *out = 0;
            return CETTA_FOLD_OK;
        }
        if (base_scale == -1) {
            if ((times & 1u) == 0u) {
                *out = acc;
                return CETTA_FOLD_OK;
            }
            return fold_fit((__int128)base_offset - acc, out)
                ? CETTA_FOLD_OK : CETTA_FOLD_OVERFLOW;
        }
    }
    /* Powers of one summary commute with each other, so the binary method
     * may multiply in either order. */
    int64_t scale = 1;
    int64_t offset = 0;
    uint64_t remaining = times;
    for (;;) {
        if ((remaining & 1u) != 0u &&
            !fold_compose(scale, offset, base_scale, base_offset, modulus,
                          &scale, &offset))
            return CETTA_FOLD_OVERFLOW;
        remaining >>= 1u;
        if (remaining == 0u)
            break;
        if (!fold_compose(base_scale, base_offset, base_scale, base_offset,
                          modulus, &base_scale, &base_offset))
            return CETTA_FOLD_OVERFLOW;
    }
    if (modulus > 0) {
        __int128 product = (__int128)scale * fold_reduce(acc, modulus);
        *out = fold_reduce(product + offset, modulus);
        return CETTA_FOLD_OK;
    }
    return fold_fit((__int128)scale * acc + offset, out)
        ? CETTA_FOLD_OK : CETTA_FOLD_OVERFLOW;
}

#if CETTA_BUILD_WITH_GMP
_Static_assert(sizeof(long) == sizeof(int64_t),
               "the bigint continuation passes int64 values as long");

void cetta_fold_big_start(CettaFoldBig *big, int64_t acc) {
    mpz_init_set_si(big->acc, (long)acc);
    for (unsigned index = 0u; index < CETTA_FOLD_EXPR_CAPACITY; index++)
        mpz_init(big->values[index]);
    big->active = true;
}

void cetta_fold_big_clear(CettaFoldBig *big) {
    if (!big->active)
        return;
    mpz_clear(big->acc);
    for (unsigned index = 0u; index < CETTA_FOLD_EXPR_CAPACITY; index++)
        mpz_clear(big->values[index]);
    big->active = false;
}

/* The source expression over exact integers.  `mpz_fdiv_r` takes the sign
 * of the divisor, which is PeTTa's floor `%`. */
CettaFoldStatus cetta_fold_step_apply_big(const CettaFoldStep *step,
                                          int64_t item, CettaFoldBig *big) {
    if (!step || !big || !big->active || step->root >= step->source.len)
        return CETTA_FOLD_UNSUPPORTED;
    const CettaFoldExpr *expr = &step->source;
    for (uint8_t index = 0u; index <= step->root; index++) {
        const CettaFoldExprNode *node = &expr->nodes[index];
        mpz_ptr value = big->values[index];
        switch (node->kind) {
        case CETTA_FOLD_EXPR_LITERAL:
            mpz_set_si(value, (long)node->literal);
            break;
        case CETTA_FOLD_EXPR_ITEM:
            mpz_set_si(value, (long)item);
            break;
        case CETTA_FOLD_EXPR_ACC:
            mpz_set(value, big->acc);
            break;
        case CETTA_FOLD_EXPR_ADD:
            mpz_add(value, big->values[node->left], big->values[node->right]);
            break;
        case CETTA_FOLD_EXPR_SUB:
            mpz_sub(value, big->values[node->left], big->values[node->right]);
            break;
        case CETTA_FOLD_EXPR_MUL:
            mpz_mul(value, big->values[node->left], big->values[node->right]);
            break;
        case CETTA_FOLD_EXPR_MOD:
            if (mpz_sgn(big->values[node->right]) == 0)
                return CETTA_FOLD_UNDEFINED;
            mpz_fdiv_r(value, big->values[node->left],
                       big->values[node->right]);
            break;
        case CETTA_FOLD_EXPR_MIN:
            mpz_set(value, mpz_cmp(big->values[node->left],
                                   big->values[node->right]) <= 0
                        ? big->values[node->left]
                        : big->values[node->right]);
            break;
        case CETTA_FOLD_EXPR_MAX:
            mpz_set(value, mpz_cmp(big->values[node->left],
                                   big->values[node->right]) >= 0
                        ? big->values[node->left]
                        : big->values[node->right]);
            break;
        default:
            return CETTA_FOLD_UNSUPPORTED;
        }
    }
    mpz_set(big->acc, big->values[step->root]);
    return CETTA_FOLD_OK;
}
#endif
