#include "prime_lambda_pi.h"

#include <limits.h>
#include <stddef.h>

typedef struct {
    CettaPrimeLambdaPiStatus status;
    Atom *type;
    bool type_is_top;
    const char *reason;
} PrimeLambdaPiInfer;

typedef struct {
    CettaPrimeLambdaPiStatus status;
    Atom *term;
    const char *reason;
} PrimeLambdaPiNormal;

static bool lp_symbol(Atom *atom, const char *name) {
    return atom && atom_is_symbol(atom, name);
}

static bool lp_expr(Atom *atom, const char *head, CettaExprIndex len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           lp_symbol(atom->expr.elems[0], head);
}

static bool lp_spend(CettaPrimeLambdaPiBudget *budget) {
    if (!budget || !budget->limited) return true;
    if (budget->remaining == 0u) return false;
    budget->remaining--;
    if (budget->spent != UINT64_MAX) budget->spent++;
    return true;
}

void cetta_prime_lambda_pi_budget_init(
    CettaPrimeLambdaPiBudget *budget, bool limited, uint64_t steps) {
    if (!budget) return;
    budget->limited = limited;
    budget->remaining = limited ? steps : 0u;
    budget->spent = 0u;
}

bool cetta_prime_lambda_pi_unwrap_scoped(
    Atom *scoped, Atom **context_out, Atom **term_out) {
    if (!lp_expr(scoped, "PrimeScoped", 3u)) return false;
    if (context_out) *context_out = scoped->expr.elems[1];
    if (term_out) *term_out = scoped->expr.elems[2];
    return true;
}

static CettaPrimeLambdaPiResult lp_result(
    CettaPrimeLambdaPiStatus status, Atom *type, const char *reason) {
    return (CettaPrimeLambdaPiResult){
        .status = status,
        .type = type,
        .reason = reason,
    };
}

static PrimeLambdaPiInfer lp_infer_result(
    CettaPrimeLambdaPiStatus status, Atom *type, bool type_is_top,
    const char *reason) {
    return (PrimeLambdaPiInfer){
        .status = status,
        .type = type,
        .type_is_top = type_is_top,
        .reason = reason,
    };
}

static PrimeLambdaPiNormal lp_normal_result(
    CettaPrimeLambdaPiStatus status, Atom *term, const char *reason) {
    return (PrimeLambdaPiNormal){
        .status = status,
        .term = term,
        .reason = reason,
    };
}

static bool lp_index(Atom *term, uint64_t *index_out) {
    if (!lp_expr(term, "idx", 2u)) return false;
    Atom *value = term->expr.elems[1];
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0) {
        return false;
    }
    if (index_out) *index_out = (uint64_t)value->ground.ival;
    return true;
}

static Atom *lp_make_index(Arena *arena, uint64_t index) {
    if (!arena || index > (uint64_t)INT64_MAX) return NULL;
    return atom_expr2(
        arena, atom_symbol(arena, "idx"), atom_int(arena, (int64_t)index));
}

static bool lp_term_shape(Atom *term) {
    uint64_t ignored = 0u;
    return lp_symbol(term, "U0") || lp_symbol(term, "U1") ||
           lp_index(term, &ignored) || lp_expr(term, "Pi", 3u) ||
           lp_expr(term, "Lam", 3u) || lp_expr(term, "App", 3u);
}

static bool lp_scope_check(Atom *term, uint64_t depth,
                           CettaPrimeLambdaPiBudget *budget,
                           bool *complete) {
    if (!complete || !*complete) return false;
    if (!lp_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!term) return false;
    if (lp_symbol(term, "U0") || lp_symbol(term, "U1")) return true;
    uint64_t index = 0u;
    if (lp_index(term, &index)) return index < depth;
    if (lp_expr(term, "Pi", 3u) || lp_expr(term, "Lam", 3u)) {
        return lp_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               depth != UINT64_MAX &&
               lp_scope_check(
                   term->expr.elems[2], depth + 1u, budget, complete);
    }
    if (lp_expr(term, "App", 3u)) {
        return lp_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               lp_scope_check(
                   term->expr.elems[2], depth, budget, complete);
    }
    return false;
}

static Atom *lp_shift(Arena *arena, Atom *term, int64_t amount,
                      uint64_t cutoff, bool *ok,
                      CettaPrimeLambdaPiBudget *budget) {
    if (!ok || !*ok || !lp_spend(budget) || !term) {
        if (ok) *ok = false;
        return NULL;
    }
    if (lp_symbol(term, "U0") || lp_symbol(term, "U1")) return term;
    uint64_t index = 0u;
    if (lp_index(term, &index)) {
        if (index < cutoff) return term;
        if (amount < 0) {
            uint64_t magnitude = (uint64_t)(-(amount + 1)) + 1u;
            if (index < magnitude) {
                *ok = false;
                return NULL;
            }
            index -= magnitude;
        } else {
            uint64_t magnitude = (uint64_t)amount;
            if (UINT64_MAX - index < magnitude) {
                *ok = false;
                return NULL;
            }
            index += magnitude;
        }
        Atom *shifted = lp_make_index(arena, index);
        if (!shifted) *ok = false;
        return shifted;
    }
    if (lp_expr(term, "Pi", 3u) || lp_expr(term, "Lam", 3u)) {
        Atom *domain = lp_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        if (!domain || cutoff == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *body = lp_shift(
            arena, term->expr.elems[2], amount, cutoff + 1u, ok, budget);
        if (!body) return NULL;
        return atom_expr3(arena, term->expr.elems[0], domain, body);
    }
    if (lp_expr(term, "App", 3u)) {
        Atom *function = lp_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        Atom *argument = function ? lp_shift(
            arena, term->expr.elems[2], amount, cutoff, ok, budget) : NULL;
        return function && argument
            ? atom_expr3(arena, term->expr.elems[0], function, argument)
            : NULL;
    }
    *ok = false;
    return NULL;
}

static Atom *lp_substitute_zero_rec(
    Arena *arena, Atom *body, Atom *argument, uint64_t binder_depth,
    bool *ok, CettaPrimeLambdaPiBudget *budget) {
    if (!ok || !*ok || !lp_spend(budget) || !body) {
        if (ok) *ok = false;
        return NULL;
    }
    if (lp_symbol(body, "U0") || lp_symbol(body, "U1")) return body;
    uint64_t index = 0u;
    if (lp_index(body, &index)) {
        if (index < binder_depth) return body;
        if (index == binder_depth) {
            return lp_shift(
                arena, argument, (int64_t)binder_depth, 0u, ok, budget);
        }
        return lp_make_index(arena, index - 1u);
    }
    if (lp_expr(body, "Pi", 3u) || lp_expr(body, "Lam", 3u)) {
        Atom *domain = lp_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        if (!domain || binder_depth == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *nested = lp_substitute_zero_rec(
            arena, body->expr.elems[2], argument, binder_depth + 1u,
            ok, budget);
        return nested
            ? atom_expr3(arena, body->expr.elems[0], domain, nested)
            : NULL;
    }
    if (lp_expr(body, "App", 3u)) {
        Atom *function = lp_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        Atom *value = function ? lp_substitute_zero_rec(
            arena, body->expr.elems[2], argument, binder_depth, ok, budget)
            : NULL;
        return function && value
            ? atom_expr3(arena, body->expr.elems[0], function, value)
            : NULL;
    }
    *ok = false;
    return NULL;
}

static Atom *lp_substitute_zero(
    Arena *arena, Atom *body, Atom *argument, bool *ok,
    CettaPrimeLambdaPiBudget *budget) {
    return lp_substitute_zero_rec(
        arena, body, argument, 0u, ok, budget);
}

static bool lp_uses_outer_zero(
    Atom *term, uint64_t binder_depth, CettaPrimeLambdaPiBudget *budget,
    bool *complete) {
    if (!complete || !*complete || !lp_spend(budget) || !term) {
        if (complete) *complete = false;
        return false;
    }
    if (lp_symbol(term, "U0") || lp_symbol(term, "U1")) return false;
    uint64_t index = 0u;
    if (lp_index(term, &index)) return index == binder_depth;
    if (lp_expr(term, "Pi", 3u) || lp_expr(term, "Lam", 3u)) {
        if (lp_uses_outer_zero(
                term->expr.elems[1], binder_depth, budget, complete)) {
            return true;
        }
        if (binder_depth == UINT64_MAX) {
            *complete = false;
            return false;
        }
        return lp_uses_outer_zero(
            term->expr.elems[2], binder_depth + 1u, budget, complete);
    }
    if (lp_expr(term, "App", 3u)) {
        return lp_uses_outer_zero(
                   term->expr.elems[1], binder_depth, budget, complete) ||
               lp_uses_outer_zero(
                   term->expr.elems[2], binder_depth, budget, complete);
    }
    *complete = false;
    return false;
}

static PrimeLambdaPiNormal lp_normalize(
    Arena *arena, Atom *term, CettaPrimeLambdaPiBudget *budget) {
    if (!lp_spend(budget))
        return lp_normal_result(
            CETTA_PRIME_LP_INCOMPLETE, NULL, "normalization-budget");
    if (!term || !lp_term_shape(term))
        return lp_normal_result(
            CETTA_PRIME_LP_REFUTED, NULL, "malformed-lambda-pi-term");
    if (lp_symbol(term, "U0") || lp_symbol(term, "U1") ||
        lp_expr(term, "idx", 2u)) {
        return lp_normal_result(CETTA_PRIME_LP_ESTABLISHED, term, NULL);
    }
    if (lp_expr(term, "Pi", 3u) || lp_expr(term, "Lam", 3u)) {
        PrimeLambdaPiNormal domain = lp_normalize(
            arena, term->expr.elems[1], budget);
        if (domain.status != CETTA_PRIME_LP_ESTABLISHED) return domain;
        PrimeLambdaPiNormal body = lp_normalize(
            arena, term->expr.elems[2], budget);
        if (body.status != CETTA_PRIME_LP_ESTABLISHED) return body;
        if (lp_expr(term, "Lam", 3u) && lp_expr(body.term, "App", 3u)) {
            uint64_t argument_index = 0u;
            if (lp_index(body.term->expr.elems[2], &argument_index) &&
                argument_index == 0u) {
                bool complete = true;
                bool used = lp_uses_outer_zero(
                    body.term->expr.elems[1], 0u, budget, &complete);
                if (!complete)
                    return lp_normal_result(
                        CETTA_PRIME_LP_INCOMPLETE, NULL,
                        "normalization-budget");
                if (!used) {
                    bool ok = true;
                    Atom *lowered = lp_shift(
                        arena, body.term->expr.elems[1], -1, 0u,
                        &ok, budget);
                    if (!ok || !lowered)
                        return lp_normal_result(
                            CETTA_PRIME_LP_INCOMPLETE, NULL,
                            "eta-lowering-failed");
                    return lp_normal_result(
                        CETTA_PRIME_LP_ESTABLISHED, lowered, NULL);
                }
            }
        }
        return lp_normal_result(
            CETTA_PRIME_LP_ESTABLISHED,
            atom_expr3(arena, term->expr.elems[0], domain.term, body.term),
            NULL);
    }
    PrimeLambdaPiNormal function = lp_normalize(
        arena, term->expr.elems[1], budget);
    if (function.status != CETTA_PRIME_LP_ESTABLISHED) return function;
    PrimeLambdaPiNormal argument = lp_normalize(
        arena, term->expr.elems[2], budget);
    if (argument.status != CETTA_PRIME_LP_ESTABLISHED) return argument;
    if (lp_expr(function.term, "Lam", 3u)) {
        bool ok = true;
        Atom *substituted = lp_substitute_zero(
            arena, function.term->expr.elems[2], argument.term, &ok, budget);
        if (!ok || !substituted)
            return lp_normal_result(
                CETTA_PRIME_LP_INCOMPLETE, NULL,
                "beta-substitution-failed");
        return lp_normalize(arena, substituted, budget);
    }
    return lp_normal_result(
        CETTA_PRIME_LP_ESTABLISHED,
        atom_expr3(
            arena, atom_symbol(arena, "App"), function.term, argument.term),
        NULL);
}

static CettaPrimeLambdaPiStatus lp_convert_terms(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeLambdaPiBudget *budget, bool *equal_out,
    const char **reason_out) {
    PrimeLambdaPiNormal left_normal = lp_normalize(arena, left, budget);
    if (left_normal.status != CETTA_PRIME_LP_ESTABLISHED) {
        if (reason_out) *reason_out = left_normal.reason;
        return left_normal.status;
    }
    PrimeLambdaPiNormal right_normal = lp_normalize(arena, right, budget);
    if (right_normal.status != CETTA_PRIME_LP_ESTABLISHED) {
        if (reason_out) *reason_out = right_normal.reason;
        return right_normal.status;
    }
    if (equal_out) *equal_out = atom_eq(left_normal.term, right_normal.term);
    return CETTA_PRIME_LP_ESTABLISHED;
}

static Atom *lp_context_extend(Arena *arena, Atom *context, Atom *domain) {
    return atom_expr3(
        arena, atom_symbol(arena, "PrimeCtxCons"), domain, context);
}

static Atom *lp_context_lookup(
    Arena *arena, Atom *context, uint64_t index,
    CettaPrimeLambdaPiBudget *budget, bool *complete) {
    if (!complete || !*complete) return NULL;
    Atom *cursor = context;
    for (uint64_t current = 0u; current < index; current++) {
        if (!lp_spend(budget)) {
            *complete = false;
            return NULL;
        }
        if (!lp_expr(cursor, "PrimeCtxCons", 3u))
            return NULL;
        cursor = cursor->expr.elems[2];
    }
    if (!lp_spend(budget)) {
        *complete = false;
        return NULL;
    }
    if (!lp_expr(cursor, "PrimeCtxCons", 3u) ||
        index >= (uint64_t)INT64_MAX) {
        return NULL;
    }
    bool ok = true;
    Atom *shifted = lp_shift(
        arena, cursor->expr.elems[1], (int64_t)(index + 1u),
        0u, &ok, budget);
    if (!shifted && !ok && budget && budget->limited &&
        budget->remaining == 0u) {
        *complete = false;
    }
    return shifted;
}

static PrimeLambdaPiInfer lp_infer(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeLambdaPiBudget *budget);

static CettaPrimeLambdaPiStatus lp_ordinary_type(
    Arena *arena, Atom *context, Atom *type,
    CettaPrimeLambdaPiBudget *budget, const char **reason_out) {
    PrimeLambdaPiInfer inferred = lp_infer(arena, context, type, budget);
    if (inferred.status != CETTA_PRIME_LP_ESTABLISHED) {
        if (reason_out) *reason_out = inferred.reason;
        return inferred.status;
    }
    bool equal = false;
    CettaPrimeLambdaPiStatus converted = lp_convert_terms(
        arena, inferred.type, atom_symbol(arena, "U1"), budget,
        &equal, reason_out);
    if (converted != CETTA_PRIME_LP_ESTABLISHED) return converted;
    if (!equal) {
        if (reason_out) *reason_out = "expected-formed-type";
        return CETTA_PRIME_LP_REFUTED;
    }
    return CETTA_PRIME_LP_ESTABLISHED;
}

static CettaPrimeLambdaPiStatus lp_check_term(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    CettaPrimeLambdaPiBudget *budget, const char **reason_out) {
    if (!lp_spend(budget)) {
        if (reason_out) *reason_out = "checking-budget";
        return CETTA_PRIME_LP_INCOMPLETE;
    }
    if (lp_expr(term, "Lam", 3u)) {
        CettaPrimeLambdaPiStatus annotation_status = lp_ordinary_type(
            arena, context, term->expr.elems[1], budget, reason_out);
        if (annotation_status != CETTA_PRIME_LP_ESTABLISHED)
            return annotation_status;
        PrimeLambdaPiNormal expected_normal = lp_normalize(
            arena, expected, budget);
        if (expected_normal.status != CETTA_PRIME_LP_ESTABLISHED) {
            if (reason_out) *reason_out = expected_normal.reason;
            return expected_normal.status;
        }
        if (!lp_expr(expected_normal.term, "Pi", 3u)) {
            if (reason_out) *reason_out = "lambda-needs-function-type";
            return CETTA_PRIME_LP_REFUTED;
        }
        bool domains_equal = false;
        CettaPrimeLambdaPiStatus converted = lp_convert_terms(
            arena, term->expr.elems[1],
            expected_normal.term->expr.elems[1], budget,
            &domains_equal, reason_out);
        if (converted != CETTA_PRIME_LP_ESTABLISHED) return converted;
        if (!domains_equal) {
            if (reason_out) *reason_out = "lambda-domain-mismatch";
            return CETTA_PRIME_LP_REFUTED;
        }
        Atom *extended = lp_context_extend(
            arena, context, expected_normal.term->expr.elems[1]);
        return lp_check_term(
            arena, extended, term->expr.elems[2],
            expected_normal.term->expr.elems[2], budget, reason_out);
    }
    PrimeLambdaPiInfer inferred = lp_infer(arena, context, term, budget);
    if (inferred.status != CETTA_PRIME_LP_ESTABLISHED) {
        if (reason_out) *reason_out = inferred.reason;
        return inferred.status;
    }
    bool equal = false;
    CettaPrimeLambdaPiStatus converted = lp_convert_terms(
        arena, inferred.type, expected, budget, &equal, reason_out);
    if (converted != CETTA_PRIME_LP_ESTABLISHED) return converted;
    if (!equal) {
        if (reason_out) *reason_out = "type-mismatch";
        return CETTA_PRIME_LP_REFUTED;
    }
    return CETTA_PRIME_LP_ESTABLISHED;
}

static PrimeLambdaPiInfer lp_infer(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeLambdaPiBudget *budget) {
    if (!lp_spend(budget))
        return lp_infer_result(
            CETTA_PRIME_LP_INCOMPLETE, NULL, false, "synthesis-budget");
    if (!term || !lp_term_shape(term))
        return lp_infer_result(
            CETTA_PRIME_LP_REFUTED, NULL, false,
            "malformed-lambda-pi-term");
    if (lp_symbol(term, "U0"))
        return lp_infer_result(
            CETTA_PRIME_LP_ESTABLISHED,
            atom_symbol(arena, "U1"), true, NULL);
    if (lp_symbol(term, "U1"))
        return lp_infer_result(
            CETTA_PRIME_LP_REFUTED, NULL, false,
            "upper-sort-has-no-type");
    uint64_t index = 0u;
    if (lp_index(term, &index)) {
        bool complete = true;
        Atom *type = lp_context_lookup(
            arena, context, index, budget, &complete);
        return type
            ? lp_infer_result(
                  CETTA_PRIME_LP_ESTABLISHED, type, false, NULL)
            : lp_infer_result(
                  complete ? CETTA_PRIME_LP_REFUTED
                           : CETTA_PRIME_LP_INCOMPLETE,
                  NULL, false,
                  complete ? "loose-index" : "synthesis-budget");
    }
    if (lp_expr(term, "Pi", 3u)) {
        const char *reason = NULL;
        CettaPrimeLambdaPiStatus domain_status = lp_ordinary_type(
            arena, context, term->expr.elems[1], budget, &reason);
        if (domain_status != CETTA_PRIME_LP_ESTABLISHED)
            return lp_infer_result(domain_status, NULL, false, reason);
        Atom *extended = lp_context_extend(
            arena, context, term->expr.elems[1]);
        CettaPrimeLambdaPiStatus body_status = lp_ordinary_type(
            arena, extended, term->expr.elems[2], budget, &reason);
        if (body_status != CETTA_PRIME_LP_ESTABLISHED)
            return lp_infer_result(body_status, NULL, false, reason);
        return lp_infer_result(
            CETTA_PRIME_LP_ESTABLISHED,
            atom_symbol(arena, "U1"), true, NULL);
    }
    if (lp_expr(term, "Lam", 3u)) {
        const char *reason = NULL;
        CettaPrimeLambdaPiStatus domain_status = lp_ordinary_type(
            arena, context, term->expr.elems[1], budget, &reason);
        if (domain_status != CETTA_PRIME_LP_ESTABLISHED)
            return lp_infer_result(domain_status, NULL, false, reason);
        Atom *extended = lp_context_extend(
            arena, context, term->expr.elems[1]);
        PrimeLambdaPiInfer body = lp_infer(
            arena, extended, term->expr.elems[2], budget);
        if (body.status != CETTA_PRIME_LP_ESTABLISHED) return body;
        if (body.type_is_top)
            return lp_infer_result(
                CETTA_PRIME_LP_REFUTED, NULL, false,
                "lambda-result-type-is-upper-sort");
        return lp_infer_result(
            CETTA_PRIME_LP_ESTABLISHED,
            atom_expr3(
                arena, atom_symbol(arena, "Pi"),
                term->expr.elems[1], body.type),
            false, NULL);
    }
    PrimeLambdaPiInfer function = lp_infer(
        arena, context, term->expr.elems[1], budget);
    if (function.status != CETTA_PRIME_LP_ESTABLISHED) return function;
    if (function.type_is_top)
        return lp_infer_result(
            CETTA_PRIME_LP_REFUTED, NULL, false,
            "application-function-has-upper-sort");
    PrimeLambdaPiNormal function_type = lp_normalize(
        arena, function.type, budget);
    if (function_type.status != CETTA_PRIME_LP_ESTABLISHED)
        return lp_infer_result(
            function_type.status, NULL, false, function_type.reason);
    if (!lp_expr(function_type.term, "Pi", 3u))
        return lp_infer_result(
            CETTA_PRIME_LP_REFUTED, NULL, false,
            "expected-function-type");
    const char *reason = NULL;
    CettaPrimeLambdaPiStatus argument_status = lp_check_term(
        arena, context, term->expr.elems[2],
        function_type.term->expr.elems[1], budget, &reason);
    if (argument_status != CETTA_PRIME_LP_ESTABLISHED)
        return lp_infer_result(argument_status, NULL, false, reason);
    bool ok = true;
    Atom *result_type = lp_substitute_zero(
        arena, function_type.term->expr.elems[2],
        term->expr.elems[2], &ok, budget);
    if (!ok || !result_type)
        return lp_infer_result(
            CETTA_PRIME_LP_INCOMPLETE, NULL, false,
            "result-substitution-failed");
    return lp_infer_result(
        CETTA_PRIME_LP_ESTABLISHED, result_type, false, NULL);
}

static CettaPrimeLambdaPiStatus lp_context_valid(
    Arena *arena, Atom *context, CettaPrimeLambdaPiBudget *budget,
    uint64_t *length_out, const char **reason_out) {
    if (!lp_spend(budget)) {
        if (reason_out) *reason_out = "context-budget";
        return CETTA_PRIME_LP_INCOMPLETE;
    }
    if (lp_symbol(context, "PrimeCtxNil")) {
        if (length_out) *length_out = 0u;
        return CETTA_PRIME_LP_ESTABLISHED;
    }
    if (!lp_expr(context, "PrimeCtxCons", 3u)) {
        if (reason_out) *reason_out = "malformed-context";
        return CETTA_PRIME_LP_REFUTED;
    }
    Atom *domain = context->expr.elems[1];
    Atom *tail = context->expr.elems[2];
    uint64_t tail_length = 0u;
    CettaPrimeLambdaPiStatus tail_status = lp_context_valid(
        arena, tail, budget, &tail_length, reason_out);
    if (tail_status != CETTA_PRIME_LP_ESTABLISHED) return tail_status;
    if (tail_length == UINT64_MAX) {
        if (reason_out) *reason_out = "context-too-deep";
        return CETTA_PRIME_LP_REFUTED;
    }
    bool scope_complete = true;
    if (!lp_scope_check(domain, tail_length, budget, &scope_complete)) {
        if (reason_out)
            *reason_out = scope_complete
                ? "context-domain-out-of-scope" : "context-budget";
        return scope_complete
            ? CETTA_PRIME_LP_REFUTED : CETTA_PRIME_LP_INCOMPLETE;
    }
    CettaPrimeLambdaPiStatus domain_status = lp_ordinary_type(
        arena, tail, domain, budget, reason_out);
    if (domain_status == CETTA_PRIME_LP_ESTABLISHED && length_out)
        *length_out = tail_length + 1u;
    return domain_status;
}

static CettaPrimeLambdaPiStatus lp_prepare_scoped(
    Arena *arena, Atom *scoped, Atom **context_out, Atom **term_out,
    uint64_t *length_out, CettaPrimeLambdaPiBudget *budget,
    const char **reason_out) {
    Atom *context = NULL;
    Atom *term = NULL;
    if (!cetta_prime_lambda_pi_unwrap_scoped(scoped, &context, &term))
        return CETTA_PRIME_LP_NOT_SCOPED;
    uint64_t length = 0u;
    CettaPrimeLambdaPiStatus context_status = lp_context_valid(
        arena, context, budget, &length, reason_out);
    if (context_status != CETTA_PRIME_LP_ESTABLISHED)
        return context_status;
    bool scope_complete = true;
    if (!lp_scope_check(term, length, budget, &scope_complete)) {
        if (reason_out)
            *reason_out = scope_complete
                ? "term-out-of-scope" : "scope-budget";
        return scope_complete
            ? CETTA_PRIME_LP_REFUTED : CETTA_PRIME_LP_INCOMPLETE;
    }
    if (context_out) *context_out = context;
    if (term_out) *term_out = term;
    if (length_out) *length_out = length;
    return CETTA_PRIME_LP_ESTABLISHED;
}

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_synth(
    Arena *arena, Atom *scoped, CettaPrimeLambdaPiBudget *budget) {
    Atom *context = NULL;
    Atom *term = NULL;
    const char *reason = NULL;
    CettaPrimeLambdaPiStatus prepared = lp_prepare_scoped(
        arena, scoped, &context, &term, NULL, budget, &reason);
    if (prepared != CETTA_PRIME_LP_ESTABLISHED)
        return lp_result(prepared, NULL, reason);
    PrimeLambdaPiInfer inferred = lp_infer(arena, context, term, budget);
    return lp_result(inferred.status, inferred.type, inferred.reason);
}

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_check(
    Arena *arena, Atom *scoped, Atom *expected,
    CettaPrimeLambdaPiBudget *budget) {
    Atom *context = NULL;
    Atom *term = NULL;
    uint64_t length = 0u;
    const char *reason = NULL;
    CettaPrimeLambdaPiStatus prepared = lp_prepare_scoped(
        arena, scoped, &context, &term, &length, budget, &reason);
    if (prepared != CETTA_PRIME_LP_ESTABLISHED)
        return lp_result(prepared, NULL, reason);
    bool scope_complete = true;
    if (!lp_scope_check(expected, length, budget, &scope_complete)) {
        return lp_result(
            scope_complete
                ? CETTA_PRIME_LP_REFUTED : CETTA_PRIME_LP_INCOMPLETE,
            NULL,
            scope_complete
                ? "expected-out-of-scope" : "checking-budget");
    }
    if (!lp_symbol(expected, "U1")) {
        CettaPrimeLambdaPiStatus formed = lp_ordinary_type(
            arena, context, expected, budget, &reason);
        if (formed != CETTA_PRIME_LP_ESTABLISHED)
            return lp_result(formed, NULL, reason);
    }
    CettaPrimeLambdaPiStatus checked = lp_check_term(
        arena, context, term, expected, budget, &reason);
    return lp_result(checked, checked == CETTA_PRIME_LP_ESTABLISHED
        ? expected : NULL, reason);
}

CettaPrimeLambdaPiResult cetta_prime_lambda_pi_convert(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeLambdaPiBudget *budget) {
    Atom *left_context = NULL;
    Atom *left = NULL;
    Atom *right_context = NULL;
    Atom *right = NULL;
    const char *reason = NULL;
    CettaPrimeLambdaPiStatus left_prepared = lp_prepare_scoped(
        arena, left_scoped, &left_context, &left, NULL, budget, &reason);
    if (left_prepared != CETTA_PRIME_LP_ESTABLISHED)
        return lp_result(left_prepared, NULL, reason);
    CettaPrimeLambdaPiStatus right_prepared = lp_prepare_scoped(
        arena, right_scoped, &right_context, &right, NULL, budget, &reason);
    if (right_prepared != CETTA_PRIME_LP_ESTABLISHED)
        return lp_result(right_prepared, NULL, reason);
    if (!atom_eq(left_context, right_context))
        return lp_result(
            CETTA_PRIME_LP_REFUTED, NULL, "conversion-context-mismatch");

    if (!(lp_symbol(left, "U1") && lp_symbol(right, "U1"))) {
        PrimeLambdaPiInfer left_type = lp_infer(
            arena, left_context, left, budget);
        if (left_type.status != CETTA_PRIME_LP_ESTABLISHED)
            return lp_result(left_type.status, NULL, left_type.reason);
        PrimeLambdaPiInfer right_type = lp_infer(
            arena, right_context, right, budget);
        if (right_type.status != CETTA_PRIME_LP_ESTABLISHED)
            return lp_result(right_type.status, NULL, right_type.reason);
        bool types_equal = false;
        CettaPrimeLambdaPiStatus type_conversion = lp_convert_terms(
            arena, left_type.type, right_type.type, budget,
            &types_equal, &reason);
        if (type_conversion != CETTA_PRIME_LP_ESTABLISHED)
            return lp_result(type_conversion, NULL, reason);
        if (!types_equal)
            return lp_result(
                CETTA_PRIME_LP_REFUTED, NULL,
                "conversion-type-mismatch");
    }

    bool equal = false;
    CettaPrimeLambdaPiStatus converted = lp_convert_terms(
        arena, left, right, budget, &equal, &reason);
    if (converted != CETTA_PRIME_LP_ESTABLISHED)
        return lp_result(converted, NULL, reason);
    return equal
        ? lp_result(CETTA_PRIME_LP_ESTABLISHED, NULL, NULL)
        : lp_result(CETTA_PRIME_LP_REFUTED, NULL, "distinct-normal-forms");
}
