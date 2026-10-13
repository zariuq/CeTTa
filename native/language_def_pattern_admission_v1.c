#include "language_def_pattern_admission_v1.h"

#include <stdio.h>
#include <string.h>

typedef struct PaBoundV1 PaBoundV1;
struct PaBoundV1 {
    const CettaLdTypeExprV1 *type;
    uint64_t count;
    const PaBoundV1 *outer;
};

typedef struct {
    const CettaLanguageDefCoreV1 *language;
    uint32_t depth_limit;
    uint64_t remaining;
    CettaLdPatternAdmissionV1Status status;
    const char *message;
} PaWorkV1;

static bool fail(PaWorkV1 *w, CettaLdPatternAdmissionV1Status status, const char *message) {
    w->status = status;
    w->message = message;
    return false;
}

static bool spend(PaWorkV1 *w, uint32_t depth) {
    if (!w->remaining || depth > w->depth_limit)
        return fail(w, CETTA_LD_PA_V1_RESOURCE_LIMIT, "Pattern admission exhausted its work or depth bound");
    --w->remaining;
    return true;
}

static bool text_equal(const CettaLdTextV1 *a, const CettaLdTextV1 *b) {
    return a && b && a->len == b->len &&
        (!a->len || (a->bytes && b->bytes && !memcmp(a->bytes, b->bytes, a->len)));
}

static bool type_equal(PaWorkV1 *w, const CettaLdTypeExprV1 *a,
                        const CettaLdTypeExprV1 *b, uint32_t depth) {
    if (!spend(w, depth)) return false;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case CETTA_LD_TYPE_BASE_V1: return text_equal(&a->as.base, &b->as.base);
    case CETTA_LD_TYPE_ARROW_V1:
        return type_equal(w, a->as.arrow.domain, b->as.arrow.domain, depth + 1u) &&
            type_equal(w, a->as.arrow.codomain, b->as.arrow.codomain, depth + 1u);
    case CETTA_LD_TYPE_MULTI_BINDER_V1:
        return type_equal(w, a->as.multi_binder_body, b->as.multi_binder_body, depth + 1u);
    case CETTA_LD_TYPE_COLLECTION_V1:
        return a->as.collection.collection_type == b->as.collection.collection_type &&
            type_equal(w, a->as.collection.element_type, b->as.collection.element_type, depth + 1u);
    }
    return false;
}

static bool sort_declared(PaWorkV1 *w, const CettaLdTextV1 *sort) {
    for (uint32_t i = 0; i < w->language->type_len; ++i) {
        if (!spend(w, 0u)) return false;
        if (text_equal(sort, &w->language->types[i].name)) return true;
    }
    return fail(w, CETTA_LD_PA_V1_UNKNOWN_TYPE, "Pattern result sort is not declared");
}

static bool natural(PaWorkV1 *w, const char *text, uint64_t *out, uint32_t depth) {
    if (!text || !*text) return fail(w, CETTA_LD_PA_V1_MALFORMED, "Missing Pattern natural number");
    uint64_t value = 0;
    for (; *text; ++text) {
        if (!spend(w, depth)) return false;
        if (*text < '0' || *text > '9') return fail(w, CETTA_LD_PA_V1_MALFORMED, "Invalid Pattern natural number");
        unsigned digit = (unsigned)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10u)
            return fail(w, CETTA_LD_PA_V1_RESOURCE_LIMIT, "Pattern index exceeds the native context counter");
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static const CettaLdTypeExprV1 *bound_type(PaWorkV1 *w, const PaBoundV1 *bound,
                                         const char *digits, uint32_t depth) {
    uint64_t index;
    if (!natural(w, digits, &index, depth)) return NULL;
    for (; bound; bound = bound->outer) {
        if (!spend(w, depth)) return NULL;
        if (index < bound->count) return bound->type;
        index -= bound->count;
    }
    fail(w, CETTA_LD_PA_V1_SCOPE_MISMATCH, "Bound variable escapes its declared context");
    return NULL;
}

static bool check(PaWorkV1 *, const PaBoundV1 *, const CettaLdPatternV1 *, const CettaLdTypeExprV1 *, uint32_t);

static void reset_result(PaWorkV1 *w) {
    w->status = CETTA_LD_PA_V1_OK;
    w->message = NULL;
}

static bool stopped(const PaWorkV1 *w) {
    return w->status == CETTA_LD_PA_V1_RESOURCE_LIMIT || w->status == CETTA_LD_PA_V1_MALFORMED;
}

static bool argument(PaWorkV1 *w, const PaBoundV1 *bound, const CettaLdPatternV1 *p,
                      const CettaLdTermParamV1 *parameter, uint32_t depth) {
    const CettaLdTypeExprV1 *type = &parameter->type;
    if (!p) return fail(w, CETTA_LD_PA_V1_MALFORMED, "Missing constructor argument");
    if (parameter->kind == CETTA_LD_PARAM_SIMPLE_V1)
        return check(w, bound, p, type, depth);
    bool multi = parameter->kind == CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1;
    if ((!multi && parameter->kind != CETTA_LD_PARAM_ABSTRACTION_NAMED_V1) ||
        (!multi && (p->kind != CETTA_LD_PATTERN_LAMBDA_V1 || p->as.lambda.binder.present)) ||
        (multi && (p->kind != CETTA_LD_PATTERN_MULTI_LAMBDA_V1 || p->as.multi_lambda.binders.len)))
        return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "Binding arguments require canonical unnamed binder nodes");
    CettaLdTypeExprV1 domain = {.kind = CETTA_LD_TYPE_MULTI_BINDER_V1, .as.multi_binder_body = (CettaLdTypeExprV1 *)type};
    CettaLdTypeExprV1 arrow = {.kind = CETTA_LD_TYPE_ARROW_V1,
        .as.arrow = {(CettaLdTypeExprV1 *)(multi ? &domain : type), (CettaLdTypeExprV1 *)type}};
    if (type->kind == CETTA_LD_TYPE_BASE_V1) type = &arrow;
    else if (type->kind != CETTA_LD_TYPE_ARROW_V1 || !type->as.arrow.domain ||
             (multi && type->as.arrow.domain->kind != CETTA_LD_TYPE_MULTI_BINDER_V1))
        return fail(w, CETTA_LD_PA_V1_UNRESOLVED_PROFILE, "Binding parameter has no induced core argument type");
    return check(w, bound, p, type, depth);
}

static bool elements(PaWorkV1 *w, const PaBoundV1 *bound, const CettaLdPatternListV1 *items,
                     const CettaLdTypeExprV1 *type, uint32_t depth) {
    if (!type || (items->len && !items->items))
        return fail(w, CETTA_LD_PA_V1_MALFORMED, "Missing collection elements or element type");
    for (uint32_t i = 0; i < items->len; ++i)
        if (!check(w, bound, &items->items[i], type, depth + 1u)) return false;
    return true;
}

static bool collection(PaWorkV1 *w, const PaBoundV1 *bound, const CettaLdPatternV1 *p,
                        const CettaLdTypeExprV1 *type, uint32_t depth) {
    if (p->as.collection.rest.present)
        return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "A collection rest is a schema, not a closed object");
    if ((unsigned)p->as.collection.collection_type >= 3u)
        return fail(w, CETTA_LD_PA_V1_MALFORMED, "Unknown collection kind");
    if (type->kind == CETTA_LD_TYPE_BASE_V1) {
        CettaLdPatternAdmissionV1Status status = CETTA_LD_PA_V1_UNKNOWN_CONSTRUCTOR;
        const char *message = "No bare collection constructor at this sort";
        for (uint32_t i = 0; i < w->language->term_len; ++i) {
            if (!spend(w, depth)) return false;
            const CettaLdGrammarRuleV1 *r = &w->language->terms[i];
            if (!text_equal(&type->as.base, &r->category) || r->param_len != 1u || !r->params ||
                r->params[0].kind != CETTA_LD_PARAM_SIMPLE_V1 || r->params[0].type.kind != CETTA_LD_TYPE_COLLECTION_V1 ||
                r->params[0].type.as.collection.collection_type != p->as.collection.collection_type) continue;
            reset_result(w);
            if (elements(w, bound, &p->as.collection.elements, r->params[0].type.as.collection.element_type, depth)) return true;
            if (stopped(w)) return false;
            if (status != CETTA_LD_PA_V1_UNRESOLVED_PROFILE) { status = w->status; message = w->message; }
        }
        return fail(w, status, message);
    }
    if (type->kind != CETTA_LD_TYPE_COLLECTION_V1 || type->as.collection.collection_type != p->as.collection.collection_type)
        return fail(w, CETTA_LD_PA_V1_TYPE_MISMATCH, "Collection kind does not match its expected type");
    return elements(w, bound, &p->as.collection.elements, type->as.collection.element_type, depth);
}

static bool arguments(PaWorkV1 *w, const PaBoundV1 *bound, const CettaLdPatternV1 *p,
                      const CettaLdGrammarRuleV1 *r, uint32_t depth) {
    if (r->param_len != p->as.apply.arguments.len)
        return fail(w, CETTA_LD_PA_V1_ARITY_MISMATCH, "Pattern constructor arity differs from its declaration");
    if (r->param_len && (!r->params || !p->as.apply.arguments.items))
        return fail(w, CETTA_LD_PA_V1_MALFORMED, "Missing constructor parameters or arguments");
    if (r->param_len == 1u && r->params[0].kind == CETTA_LD_PARAM_SIMPLE_V1 &&
        r->params[0].type.kind == CETTA_LD_TYPE_COLLECTION_V1)
        return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "Bare collection declarations require PCollection");
    for (uint32_t i = 0; i < r->param_len; ++i)
        if (!argument(w, bound, &p->as.apply.arguments.items[i], &r->params[i], depth + 1u)) return false;
    return true;
}

static bool check(PaWorkV1 *w, const PaBoundV1 *bound, const CettaLdPatternV1 *p,
                   const CettaLdTypeExprV1 *type, uint32_t depth) {
    if (!spend(w, depth)) return false;
    if (!p || !type) return fail(w, CETTA_LD_PA_V1_MALFORMED, "Missing Pattern or expected type");
    switch (p->kind) {
    case CETTA_LD_PATTERN_BVAR_V1: {
        const CettaLdTypeExprV1 *actual = bound_type(w, bound, p->as.bvar_decimal, depth);
        if (!actual) return false;
        if (type_equal(w, actual, type, depth)) return true;
        return w->status != CETTA_LD_PA_V1_OK ? false :
            fail(w, CETTA_LD_PA_V1_TYPE_MISMATCH, "Bound variable has another type");
    }
    case CETTA_LD_PATTERN_FVAR_V1:
        return fail(w, CETTA_LD_PA_V1_SCOPE_MISMATCH, "Closed Pattern contains a free variable");
    case CETTA_LD_PATTERN_APPLY_V1: {
        if (type->kind != CETTA_LD_TYPE_BASE_V1)
            return fail(w, CETTA_LD_PA_V1_TYPE_MISMATCH, "Core constructors produce base sorts");
        CettaLdPatternAdmissionV1Status status = CETTA_LD_PA_V1_UNKNOWN_CONSTRUCTOR;
        const char *message = "Pattern constructor is not declared at this sort";
        for (uint32_t i = 0; i < w->language->term_len; ++i) {
            if (!spend(w, depth)) return false;
            const CettaLdGrammarRuleV1 *r = &w->language->terms[i];
            if (!text_equal(&p->as.apply.head, &r->label) || !text_equal(&type->as.base, &r->category)) continue;
            reset_result(w);
            if (arguments(w, bound, p, r, depth)) return true;
            if (stopped(w)) return false;
            if (status != CETTA_LD_PA_V1_UNRESOLVED_PROFILE) { status = w->status; message = w->message; }
        }
        return fail(w, status, message);
    }
    case CETTA_LD_PATTERN_LAMBDA_V1: {
        if (p->as.lambda.binder.present)
            return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "Closed objects require canonical unnamed lambda nodes");
        if (type->kind != CETTA_LD_TYPE_ARROW_V1 || !type->as.arrow.domain || !type->as.arrow.codomain)
            return fail(w, CETTA_LD_PA_V1_TYPE_MISMATCH, "Lambda requires an arrow type");
        PaBoundV1 inner = {type->as.arrow.domain, 1u, bound};
        return check(w, &inner, p->as.lambda.body, type->as.arrow.codomain, depth + 1u);
    }
    case CETTA_LD_PATTERN_MULTI_LAMBDA_V1: {
        if (p->as.multi_lambda.binders.len)
            return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "Closed objects require canonical unnamed multi-lambda nodes");
        if (type->kind != CETTA_LD_TYPE_ARROW_V1 || !type->as.arrow.domain ||
            type->as.arrow.domain->kind != CETTA_LD_TYPE_MULTI_BINDER_V1)
            return fail(w, CETTA_LD_PA_V1_TYPE_MISMATCH, "Multi-lambda requires a multi-binder arrow domain");
        uint64_t arity;
        if (!natural(w, p->as.multi_lambda.arity_decimal, &arity, depth)) return false;
        PaBoundV1 inner = {type->as.arrow.domain->as.multi_binder_body, arity, bound};
        return check(w, &inner, p->as.multi_lambda.body, type->as.arrow.codomain, depth + 1u);
    }
    case CETTA_LD_PATTERN_SUBST_V1:
        return fail(w, CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "Explicit substitution is schema machinery, not a closed object");
    case CETTA_LD_PATTERN_COLLECTION_V1: return collection(w, bound, p, type, depth);
    }
    return fail(w, CETTA_LD_PA_V1_MALFORMED, "Unknown Pattern constructor");
}

bool cetta_language_def_pattern_v1_admit(const CettaLanguageDefCoreV1 *language,
    const char *category, const CettaLdPatternV1 *pattern, uint32_t depth_limit,
    uint64_t work_limit, CettaLdPatternAdmissionV1Status *status, char *error, size_t error_size) {
    PaWorkV1 w = {language, depth_limit, work_limit, CETTA_LD_PA_V1_OK, NULL};
    if (error && error_size) error[0] = '\0';
    bool ok = false;
    if (!language || !category || !pattern || !work_limit ||
        (language->term_len && !language->terms) || (language->type_len && !language->types)) {
        fail(&w, CETTA_LD_PA_V1_BAD_ARGUMENT, "Invalid Pattern admission request");
    } else if (strlen(category) > UINT32_MAX) {
        fail(&w, CETTA_LD_PA_V1_RESOURCE_LIMIT, "Category name exceeds wire size");
    } else {
        CettaLdTypeExprV1 type = {.kind = CETTA_LD_TYPE_BASE_V1,
            .as.base = {(uint8_t *)category, (uint32_t)strlen(category)}};
        ok = sort_declared(&w, &type.as.base) && check(&w, NULL, pattern, &type, 0u);
    }
    if (error && error_size && w.message) snprintf(error, error_size, "%s", w.message);
    if (status) *status = w.status;
    return ok;
}

const char *cetta_ld_pattern_admission_v1_status_name(CettaLdPatternAdmissionV1Status status) {
    static const char *const names[] = {"ok", "bad_argument", "malformed", "unknown_type",
        "unknown_constructor", "arity_mismatch", "type_mismatch", "scope_mismatch",
        "representation_mismatch", "unresolved_profile", "resource_limit"};
    return (unsigned)status < sizeof(names) / sizeof(*names) ? names[status] : "unknown_status";
}
