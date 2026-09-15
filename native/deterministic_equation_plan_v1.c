#include "deterministic_equation_plan_v1.h"

#include "finite_horn_gslt_v1.h"
#include "gslt_composition_v1.h"
#include "src/symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const Atom *left;
    const Atom *right;
    SymbolId head;
    uint32_t arity;
} DeterministicEquationRuleV1;

struct CettaDeterministicEquationPlanV1 {
    Arena source_arena;
    DeterministicEquationRuleV1 *rules;
    uint32_t rule_count;
};

typedef struct {
    VarId variable;
    const Atom *value;
} DeterministicEquationBindingV1;

typedef struct {
    DeterministicEquationBindingV1 *items;
    uint32_t count;
    uint32_t capacity;
} DeterministicEquationEnvironmentV1;

typedef struct {
    const CettaDeterministicEquationPlanV1 *plan;
    CettaDeterministicPrimitiveFnV1 primitive;
    void *primitive_context;
    Arena *arena;
    AtomDeepCopySession *copy_session;
    uint32_t depth_limit;
    uint64_t work_remaining;
    CettaDeterministicEquationStatusV1 status;
    char *error;
    size_t error_size;
} DeterministicEquationContextV1;

static int equation_rule_compare(const void *left_opaque,
                                 const void *right_opaque) {
    const DeterministicEquationRuleV1 *left = left_opaque;
    const DeterministicEquationRuleV1 *right = right_opaque;

    if (left->head < right->head)
        return -1;
    if (left->head > right->head)
        return 1;
    if (left->arity < right->arity)
        return -1;
    if (left->arity > right->arity)
        return 1;
    return 0;
}

static bool equation_error(char *buffer, size_t size,
                           const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool equation_fail(
    DeterministicEquationContextV1 *context,
    CettaDeterministicEquationStatusV1 status,
    const char *format, ...) {
    va_list arguments;

    if (!context)
        return false;
    context->status = status;
    if (context->error && context->error_size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(
            context->error, context->error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool equation_take_work(
    DeterministicEquationContextV1 *context, uint32_t depth) {
    if (!context)
        return false;
    if (depth > context->depth_limit)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exceeded its depth limit");
    if (context->work_remaining == 0u)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exhausted its work limit");
    context->work_remaining--;
    return true;
}

static bool expression_head(
    const Atom *atom, const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == arity + 1u && atom->expr.elems &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool rule_left_view(
    const Atom *left, SymbolId *head, uint32_t *arity) {
    if (!left || !head || !arity || left->kind != ATOM_EXPR ||
        left->expr.len == 0u || !left->expr.elems ||
        !left->expr.elems[0] ||
        left->expr.elems[0]->kind != ATOM_SYMBOL ||
        left->expr.len - 1u > UINT32_MAX)
        return false;
    *head = left->expr.elems[0]->sym_id;
    *arity = (uint32_t)(left->expr.len - 1u);
    return true;
}

static bool pattern_is_left_linear_at(
    const Atom *term, VarId **variables, size_t *count, size_t *capacity) {
    if (!term || !variables || !count || !capacity)
        return false;
    if (term->kind == ATOM_VAR) {
        for (size_t index = 0u; index < *count; index++) {
            if ((*variables)[index] == term->var_id)
                return false;
        }
        if (*count == *capacity) {
            size_t next = *capacity ? *capacity * 2u : 8u;
            VarId *grown;
            if (next < *capacity || next > SIZE_MAX / sizeof(**variables))
                return false;
            grown = realloc(*variables, next * sizeof(*grown));
            if (!grown)
                return false;
            *variables = grown;
            *capacity = next;
        }
        (*variables)[(*count)++] = term->var_id;
        return true;
    }
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!pattern_is_left_linear_at(
                term->expr.elems[index], variables, count, capacity))
            return false;
    }
    return true;
}

static bool pattern_is_left_linear(const Atom *left) {
    VarId *variables = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    bool result = pattern_is_left_linear_at(
        left, &variables, &count, &capacity);
    free(variables);
    return result;
}

/* Left-linear rule patterns are alpha-equivalent exactly when their
 * non-variable structure is equal and variables occupy the same positions.
 * Variable identifiers are occurrence-local to each parsed presentation and
 * therefore cannot be compared directly across source files. */
static bool pattern_alpha_eq(const Atom *left, const Atom *right) {
    if (!left || !right)
        return false;
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR)
        return left->kind == ATOM_VAR && right->kind == ATOM_VAR;
    if (left->kind != right->kind)
        return false;
    if (left->kind != ATOM_EXPR)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!pattern_alpha_eq(
                left->expr.elems[index], right->expr.elems[index]))
            return false;
    }
    return true;
}

/* Both patterns have already been checked left-linear, so variables are
 * one-use wildcards.  Structural compatibility is therefore equivalent to
 * first-order overlap. */
static bool patterns_overlap(const Atom *left, const Atom *right) {
    if (!left || !right)
        return false;
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR)
        return true;
    if (left->kind != right->kind)
        return false;
    if (left->kind != ATOM_EXPR)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!patterns_overlap(
                left->expr.elems[index], right->expr.elems[index]))
            return false;
    }
    return true;
}

static bool variable_bound(
    const VarId *variables, size_t count, VarId variable) {
    for (size_t index = count; index > 0u; index--) {
        if (variables[index - 1u] == variable)
            return true;
    }
    return false;
}

static bool right_variables_bound_at(
    const Atom *term, VarId **variables, size_t *count, size_t *capacity) {
    if (!term || !variables || !count || !capacity)
        return false;
    if (term->kind == ATOM_VAR)
        return variable_bound(*variables, *count, term->var_id);
    if (term->kind != ATOM_EXPR)
        return true;
    if (expression_head(term, "let", 3u)) {
        const Atom *binder = term->expr.elems[1];
        size_t saved_count = *count;
        size_t next;
        VarId *grown;
        bool ok;
        if (!binder || binder->kind != ATOM_VAR ||
            !right_variables_bound_at(
                term->expr.elems[2], variables, count, capacity))
            return false;
        if (*count == *capacity) {
            next = *capacity ? *capacity * 2u : 8u;
            if (next < *capacity || next > SIZE_MAX / sizeof(**variables))
                return false;
            grown = realloc(*variables, next * sizeof(*grown));
            if (!grown)
                return false;
            *variables = grown;
            *capacity = next;
        }
        (*variables)[(*count)++] = binder->var_id;
        ok = right_variables_bound_at(
            term->expr.elems[3], variables, count, capacity);
        *count = saved_count;
        return ok;
    }
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!right_variables_bound_at(
                term->expr.elems[index], variables, count, capacity))
            return false;
    }
    return true;
}

static bool right_variables_bound(const Atom *left, const Atom *right) {
    VarId *variables = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    bool result = pattern_is_left_linear_at(
        left, &variables, &count, &capacity) &&
        right_variables_bound_at(right, &variables, &count, &capacity);
    free(variables);
    return result;
}

/* Preserve the source reader's literal interpretation. In particular True,
 * 1.0, and $x are source symbols, not host Booleans, floats, or variables.
 * The reader's tree is projected directly; no text or rule quotation is
 * rendered and reparsed. Variable binding is a later, per-rule operation. */
static Atom *equation_source_atom(
    Arena *arena, const FHGSLTSourceNodeV1 *node,
    char *error, size_t error_size) {
    FHGSLTSourceKindV1 kind = fhgslt_source_kind_v1(node);
    if (kind == FHGSLT_SOURCE_V1_LIST) {
        size_t count = fhgslt_source_child_count_v1(node);
        Atom **children;
        if (count > UINT32_MAX || count > SIZE_MAX / sizeof(*children)) {
            (void)equation_error(error, error_size, "source list is too large");
            return NULL;
        }
        children = arena_alloc(arena, count * sizeof(*children));
        for (size_t index = 0u; index < count; index++) {
            children[index] = equation_source_atom(
                arena, fhgslt_source_child_v1(node, index), error, error_size);
            if (!children[index]) return NULL;
        }
        return atom_expr(arena, children, (CettaExprLen)count);
    }
    size_t length = 0u;
    const uint8_t *bytes = fhgslt_source_text_v1(node, &length);
    bool variable = kind == FHGSLT_SOURCE_V1_VARIABLE;
    if (kind == FHGSLT_SOURCE_V1_INVALID || (!bytes && length != 0u) ||
        length > SIZE_MAX - 2u || (length && memchr(bytes, 0, length))) {
        (void)equation_error(error, error_size,
                             "unrepresentable deterministic source token");
        return NULL;
    }
    char *text = arena_alloc(arena, length + (variable ? 2u : 1u));
    if (variable) text[0] = '?';
    if (length) memcpy(text + (variable ? 1u : 0u), bytes, length);
    text[length + (variable ? 1u : 0u)] = '\0';
    if (kind == FHGSLT_SOURCE_V1_STRING) return atom_string(arena, text);
    if (kind == FHGSLT_SOURCE_V1_INTEGER) {
        char *end = NULL;
        errno = 0;
        intmax_t value = strtoimax(text, &end, 10);
        if (errno || end == text || *end || value < INT64_MIN || value > INT64_MAX)
            return atom_bigint(arena, text);
        return atom_int(arena, (int64_t)value);
    }
    return atom_symbol(arena, text);
}

/* Signatures use size_t arities, but executable source integers have the
 * signed native carrier. Check every rewrite, including nonselected rules. */
static bool equation_source_integers_fit(const Atom *term) {
    if (term->kind == ATOM_GROUNDED && term->ground.gkind == GV_BIGINT)
        return false;
    if (term->kind == ATOM_EXPR)
        for (CettaExprIndex index = 0u; index < term->expr.len; index++)
            if (!equation_source_integers_fit(term->expr.elems[index]))
                return false;
    return true;
}

typedef struct {
    const char *name;
    Atom *value;
} EquationSourceVariableV1;

typedef struct {
    EquationSourceVariableV1 *items;
    size_t count;
    size_t capacity;
} EquationSourceVariablesV1;

static Atom *equation_bind_source_variables(
    Arena *arena, Atom *term, EquationSourceVariablesV1 *variables,
    char *error, size_t error_size) {
    if (cetta_gslt_source_variable_v1(term)) {
        const char *name = cetta_gslt_source_variable_name_v1(term);
        for (size_t index = 0u; index < variables->count; index++)
            if (strcmp(name, variables->items[index].name) == 0)
                return variables->items[index].value;
        if (variables->count == variables->capacity) {
            size_t next = variables->capacity ? variables->capacity * 2u : 8u;
            if (next < variables->capacity || next > SIZE_MAX / sizeof(*variables->items))
                return NULL;
            EquationSourceVariableV1 *grown = realloc(
                variables->items, next * sizeof(*grown));
            if (!grown) return NULL;
            variables->items = grown;
            variables->capacity = next;
        }
        VarId id;
        if (!fresh_var_id_try(&id)) {
            (void)equation_error(error, error_size, "source variable identities exhausted");
            return NULL;
        }
        Atom *value = atom_var_with_id(arena, name, id);
        variables->items[variables->count++] = (EquationSourceVariableV1){name, value};
        return value;
    }
    if (term->kind != ATOM_EXPR) return term;
    Atom **children = arena_alloc(arena, term->expr.len * sizeof(*children));
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        children[index] = equation_bind_source_variables(
            arena, term->expr.elems[index], variables, error, error_size);
        if (!children[index]) return NULL;
    }
    return atom_expr(arena, children, term->expr.len);
}

/* The source package is only the existing syntax reader/validator. It is
 * borrowed here and never turned into a Horn execution program. The plan
 * owns the projected GSLT values and selected deterministic equations. */
static bool equation_plan_from_source(
    const FHGSLTPackage *source,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    CettaDeterministicEquationPlanV1 *plan = NULL;
    size_t source_count;
    uint32_t selected = 0u;
    CettaGsltCompositionV1 composition = {0};

    plan = calloc(1u, sizeof(*plan));
    if (!plan) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        return equation_error(
            error, error_size,
            "cannot allocate deterministic equation plan");
    }
    arena_init(&plan->source_arena);
    arena_set_runtime_kind(&plan->source_arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    size_t presentation_count = fhgslt_package_presentation_count(source);
    if (presentation_count > SIZE_MAX / sizeof(Atom *)) {
        if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        (void)equation_error(error, error_size, "too many source presentations");
        goto fail;
    }
    Atom **presentations = arena_alloc(
        &plan->source_arena, presentation_count * sizeof(*presentations));
    for (size_t index = 0u; index < presentation_count; index++) {
        presentations[index] = equation_source_atom(
            &plan->source_arena, fhgslt_package_source_root_v1(source, index),
            error, error_size);
        if (!presentations[index]) goto source_fail;
    }
    if (!cetta_gslt_composition_build_v1(
            presentations, presentation_count, &composition, error, error_size))
        goto source_fail;
    source_count = composition.rewrite_count;
    if (source_count > UINT32_MAX ||
        source_count > SIZE_MAX / sizeof(*plan->rules)) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        (void)equation_error(
            error, error_size,
            "deterministic equation presentation is too large");
        goto fail;
    }
    plan->rules = calloc(
        source_count ? source_count : 1u, sizeof(*plan->rules));
    if (!plan->rules) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        (void)equation_error(
            error, error_size,
            "cannot allocate deterministic equation rule table");
        goto fail;
    }
    for (size_t index = 0u; index < source_count; index++) {
        const CettaGsltRewriteV1 *view = &composition.rewrites[index];
        const Atom *left;
        const Atom *right;
        SymbolId head;
        uint32_t arity;

        if (!equation_source_integers_fit(view->head) ||
            !equation_source_integers_fit(view->body)) {
            (void)equation_error(error, error_size,
                                "%s: source integer exceeds the native signed range",
                                view->name);
            goto source_fail;
        }
        if (!expression_head(view->head, "metta-equation", 2u))
            continue;
        if (view->body->expr.len != 1u) {
            if (status)
                *status = CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE;
            (void)equation_error(
                error, error_size,
                "%s: deterministic equations cannot have premises",
                view->name ? view->name : "equation rule");
            goto fail;
        }
        EquationSourceVariablesV1 variables = {0};
        left = equation_bind_source_variables(
            &plan->source_arena, view->head->expr.elems[1], &variables,
            error, error_size);
        right = left ? equation_bind_source_variables(
            &plan->source_arena, view->head->expr.elems[2], &variables,
            error, error_size) : NULL;
        free(variables.items);
        if (!left || !right) {
            if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
            (void)equation_error(error, error_size, "cannot bind equation source variables");
            goto fail;
        }
        if (!rule_left_view(left, &head, &arity) ||
            !pattern_is_left_linear(left)) {
            if (status)
                *status = CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE;
            (void)equation_error(
                error, error_size,
                "%s: equation left side must be a left-linear symbol-headed call",
                view->name ? view->name : "equation rule");
            goto fail;
        }
        if (!right_variables_bound(left, right)) {
            if (status)
                *status = CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE;
            (void)equation_error(
                error, error_size,
                "%s: equation right side contains an unbound variable",
                view->name ? view->name : "equation rule");
            goto fail;
        }
        for (uint32_t prior = 0u; prior < selected; prior++) {
            if (pattern_alpha_eq(plan->rules[prior].left, left)) {
                if (status)
                    *status =
                        CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE;
                (void)equation_error(
                    error, error_size,
                    "%s and %s have the same deterministic equation left side",
                    plan->rules[prior].name ?
                        plan->rules[prior].name : "prior equation rule",
                    view->name ? view->name : "equation rule");
                goto fail;
            }
            if (patterns_overlap(plan->rules[prior].left, left)) {
                if (status)
                    *status =
                        CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE;
                (void)equation_error(
                    error, error_size,
                    "%s and %s have overlapping deterministic equation left sides",
                    plan->rules[prior].name ?
                        plan->rules[prior].name : "prior equation rule",
                    view->name ? view->name : "equation rule");
                goto fail;
            }
        }
        plan->rules[selected++] = (DeterministicEquationRuleV1){
            .name = view->name,
            .left = left,
            .right = right,
            .head = head,
            .arity = arity,
        };
    }
    if (selected == 0u) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION;
        (void)equation_error(
            error, error_size,
            "presentations contain no deterministic equations");
        goto fail;
    }
    qsort(plan->rules, selected, sizeof(*plan->rules),
          equation_rule_compare);
    plan->rule_count = selected;
    if (status)
        *status = CETTA_DETERMINISTIC_EQUATION_V1_OK;
    *out = plan;
    cetta_gslt_composition_free_v1(&composition);
    return true;

source_fail:
    if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION;
fail:
    cetta_gslt_composition_free_v1(&composition);
    cetta_deterministic_equation_plan_v1_free(plan);
    return false;
}

bool cetta_deterministic_equation_plan_v1_load(
    const char *const *presentation_paths, size_t presentation_count,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    FHGSLTPackage *source = NULL;

    if (error && error_size)
        error[0] = '\0';
    if (status)
        *status = CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (!presentation_paths || presentation_count == 0u || !out)
        return equation_error(
            error, error_size,
            "invalid deterministic equation plan request");
    if (!fhgslt_package_from_paths(
            presentation_paths, presentation_count, &source,
            error, error_size)) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION;
        return false;
    }
    bool ok = equation_plan_from_source(source, out, status, error, error_size);
    fhgslt_package_free(source);
    return ok;
}

bool cetta_deterministic_equation_plan_v1_load_inputs(
    const CettaDeterministicEquationInputV1 *inputs, size_t input_count,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    FHGSLTPackage *source = NULL;

    if (error && error_size)
        error[0] = '\0';
    if (status)
        *status = CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (!inputs || input_count == 0u || !out ||
        input_count > SIZE_MAX / sizeof(FHGSLTInput))
        return equation_error(
            error, error_size,
            "invalid deterministic equation input request");
    FHGSLTInput *source_inputs = calloc(input_count, sizeof(*source_inputs));
    if (!source_inputs) {
        if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        return equation_error(error, error_size, "cannot allocate source input views");
    }
    for (size_t index = 0u; index < input_count; index++)
        source_inputs[index] = (FHGSLTInput){
            inputs[index].bytes, inputs[index].length, inputs[index].source};
    bool loaded = fhgslt_package_from_inputs(
        source_inputs, input_count, &source, error, error_size);
    free(source_inputs);
    if (!loaded) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION;
        return false;
    }
    bool ok = equation_plan_from_source(source, out, status, error, error_size);
    fhgslt_package_free(source);
    return ok;
}

void cetta_deterministic_equation_plan_v1_free(
    CettaDeterministicEquationPlanV1 *plan) {
    if (!plan)
        return;
    arena_free(&plan->source_arena);
    free(plan->rules);
    free(plan);
}

static const Atom *environment_lookup(
    const DeterministicEquationEnvironmentV1 *environment,
    VarId variable) {
    if (!environment)
        return NULL;
    for (uint32_t index = environment->count; index > 0u; index--) {
        if (environment->items[index - 1u].variable == variable)
            return environment->items[index - 1u].value;
    }
    return NULL;
}

static bool environment_push(
    DeterministicEquationEnvironmentV1 *environment,
    VarId variable, const Atom *value) {
    DeterministicEquationBindingV1 *grown;
    uint32_t next;

    if (!environment || !value || environment->count == UINT32_MAX)
        return false;
    if (environment->count == environment->capacity) {
        next = environment->capacity ? environment->capacity * 2u : 16u;
        if (next < environment->capacity ||
            (size_t)next > SIZE_MAX / sizeof(*grown))
            return false;
        grown = realloc(
            environment->items, (size_t)next * sizeof(*grown));
        if (!grown)
            return false;
        environment->items = grown;
        environment->capacity = next;
    }
    environment->items[environment->count++] =
        (DeterministicEquationBindingV1){variable, value};
    return true;
}

static bool match_pattern(
    const Atom *pattern, const Atom *value,
    DeterministicEquationEnvironmentV1 *environment,
    DeterministicEquationContextV1 *context, uint32_t depth) {
    const Atom *prior;

    if (!equation_take_work(context, depth) || !pattern || !value)
        return false;
    if (pattern->kind == ATOM_VAR) {
        prior = environment_lookup(environment, pattern->var_id);
        if (prior)
            return atom_eq((Atom *)prior, (Atom *)value);
        if (!environment_push(environment, pattern->var_id, value))
            return equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic equation bindings");
        return true;
    }
    if (pattern->kind != value->kind)
        return false;
    if (pattern->kind != ATOM_EXPR)
        return atom_eq((Atom *)pattern, (Atom *)value);
    if (pattern->expr.len != value->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        if (!match_pattern(
                pattern->expr.elems[index], value->expr.elems[index],
                environment, context, depth + 1u))
            return false;
    }
    return true;
}

static uint32_t plan_lower_bound(
    const CettaDeterministicEquationPlanV1 *plan,
    SymbolId head, uint32_t arity) {
    uint32_t lower = 0u;
    uint32_t upper = plan ? plan->rule_count : 0u;

    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const DeterministicEquationRuleV1 *rule = &plan->rules[middle];
        if (rule->head < head ||
            (rule->head == head && rule->arity < arity))
            lower = middle + 1u;
        else
            upper = middle;
    }
    return lower;
}

static bool plan_rule_range(
    const CettaDeterministicEquationPlanV1 *plan,
    SymbolId head, uint32_t arity,
    uint32_t *first, uint32_t *count) {
    uint32_t begin;
    uint32_t end;

    if (first)
        *first = 0u;
    if (count)
        *count = 0u;
    if (!plan)
        return false;
    begin = plan_lower_bound(plan, head, arity);
    if (begin == plan->rule_count || plan->rules[begin].head != head ||
        plan->rules[begin].arity != arity)
        return false;
    end = begin + 1u;
    while (end < plan->rule_count && plan->rules[end].head == head &&
           plan->rules[end].arity == arity)
        end++;
    if (first)
        *first = begin;
    if (count)
        *count = end - begin;
    return true;
}

static bool plan_defines_symbol(
    const CettaDeterministicEquationPlanV1 *plan, SymbolId head) {
    uint32_t index;

    if (!plan)
        return false;
    index = plan_lower_bound(plan, head, 0u);
    return index < plan->rule_count && plan->rules[index].head == head;
}

typedef enum {
    EQUATION_FRAME_CONSTRUCTOR = 0,
    EQUATION_FRAME_CALL,
    EQUATION_FRAME_LET_VALUE,
    EQUATION_FRAME_LET_BODY,
    EQUATION_FRAME_RULE_ENVIRONMENT
} DeterministicEquationFrameKindV1;

typedef struct {
    DeterministicEquationFrameKindV1 kind;
    union {
        struct {
            const Atom *term;
            DeterministicEquationEnvironmentV1 *environment;
            Atom **elements;
            CettaExprIndex next;
            bool reusable;
        } constructor;
        struct {
            const Atom *term;
            DeterministicEquationEnvironmentV1 *environment;
            Atom **arguments;
            uint32_t arity;
            uint32_t next;
            SymbolId head;
            bool reusable;
        } call;
        struct {
            DeterministicEquationEnvironmentV1 *environment;
            const Atom *binder;
            const Atom *body;
        } let_value;
        struct {
            DeterministicEquationEnvironmentV1 *environment;
            uint32_t saved_count;
        } let_body;
        struct {
            DeterministicEquationEnvironmentV1 *environment;
        } rule_environment;
    } as;
} DeterministicEquationFrameV1;

typedef struct {
    DeterministicEquationFrameV1 *items;
    uint32_t count;
    uint32_t capacity;
} DeterministicEquationStackV1;

typedef enum {
    EQUATION_CALL_FAILED = 0,
    EQUATION_CALL_VALUE,
    EQUATION_CALL_DESCEND
} DeterministicEquationCallOutcomeV1;

static void equation_frame_dispose(DeterministicEquationFrameV1 *frame) {
    if (!frame)
        return;
    switch (frame->kind) {
    case EQUATION_FRAME_CONSTRUCTOR:
        free(frame->as.constructor.elements);
        break;
    case EQUATION_FRAME_CALL:
        free(frame->as.call.arguments);
        break;
    case EQUATION_FRAME_RULE_ENVIRONMENT:
        if (frame->as.rule_environment.environment) {
            free(frame->as.rule_environment.environment->items);
            free(frame->as.rule_environment.environment);
        }
        break;
    case EQUATION_FRAME_LET_VALUE:
    case EQUATION_FRAME_LET_BODY:
        break;
    }
    memset(frame, 0, sizeof(*frame));
}

static void equation_stack_dispose(DeterministicEquationStackV1 *stack) {
    if (!stack)
        return;
    while (stack->count > 0u)
        equation_frame_dispose(&stack->items[--stack->count]);
    free(stack->items);
    memset(stack, 0, sizeof(*stack));
}

static bool equation_stack_push(
    DeterministicEquationContextV1 *context,
    DeterministicEquationStackV1 *stack,
    const DeterministicEquationFrameV1 *frame) {
    DeterministicEquationFrameV1 *grown;
    uint32_t next;

    if (!context || !stack || !frame)
        return false;
    if (stack->count >= context->depth_limit)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exceeded its continuation depth limit");
    if (stack->count == stack->capacity) {
        next = stack->capacity ? stack->capacity * 2u : 64u;
        if (next < stack->capacity || next > context->depth_limit)
            next = context->depth_limit;
        if (next <= stack->capacity ||
            (size_t)next > SIZE_MAX / sizeof(*grown))
            return equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "deterministic equation continuation stack is too large");
        grown = realloc(stack->items, (size_t)next * sizeof(*grown));
        if (!grown)
            return equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic equation continuation stack");
        stack->items = grown;
        stack->capacity = next;
    }
    stack->items[stack->count++] = *frame;
    return true;
}

static Atom *equation_copy_atom(
    DeterministicEquationContextV1 *context, const Atom *source) {
    Atom *copy;

    if (!context || !context->copy_session || !source)
        return NULL;
    copy = atom_deep_copy_session_copy(
        context->copy_session, (Atom *)source);
    if (!copy)
        (void)equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "cannot copy deterministic equation value");
    return copy;
}

static bool equation_term_reusable(
    const DeterministicEquationContextV1 *context, const Atom *term) {
    return context && term && arena_owns_atom(context->arena, term) &&
        atom_graph_is_closed_for_arena(context->arena, term);
}

static DeterministicEquationCallOutcomeV1 equation_complete_call(
    DeterministicEquationContextV1 *context,
    DeterministicEquationStackV1 *stack,
    const Atom *call, Atom **arguments, uint32_t arity,
    SymbolId head, bool reusable,
    Atom **value, const Atom **next_term,
    DeterministicEquationEnvironmentV1 **next_environment) {
    uint32_t first = 0u;
    uint32_t candidate_count = 0u;
    bool defined = plan_rule_range(
        context->plan, head, arity, &first, &candidate_count);
    const DeterministicEquationRuleV1 *matched = NULL;
    DeterministicEquationEnvironmentV1 matched_environment = {0};
    uint32_t match_count = 0u;
    Atom *evaluated_call = NULL;

    *value = NULL;
    *next_term = NULL;
    *next_environment = NULL;
    if (!defined && plan_defines_symbol(context->plan, head)) {
        free(arguments);
        (void)equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "defined deterministic equation head was called at an unsupported arity");
        return EQUATION_CALL_FAILED;
    }
    if (!defined && context->primitive) {
        CettaDeterministicPrimitiveResultV1 primitive_result =
            context->primitive(
                context->primitive_context,
                atom_name_cstr(call->expr.elems[0]), arguments, arity,
                context->arena, value, context->error,
                context->error_size);
        if (primitive_result == CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT) {
            free(arguments);
            context->status = CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT;
            return EQUATION_CALL_FAILED;
        }
        if (primitive_result == CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED) {
            free(arguments);
            if (!*value) {
                (void)equation_fail(
                    context,
                    CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT,
                    "deterministic primitive returned no value");
                return EQUATION_CALL_FAILED;
            }
            return EQUATION_CALL_VALUE;
        }
    }
    if (!defined) {
        if (reusable) {
            *value = (Atom *)call;
        } else {
            Atom **elements = calloc(
                (size_t)arity + 1u, sizeof(*elements));
            if (!elements) {
                free(arguments);
                (void)equation_fail(
                    context,
                    CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                    "cannot allocate deterministic constructor result");
                return EQUATION_CALL_FAILED;
            }
            elements[0] = atom_symbol_id(context->arena, head);
            for (uint32_t index = 0u; index < arity; index++)
                elements[index + 1u] = arguments[index];
            *value = atom_expr(
                context->arena, elements, (CettaExprLen)arity + 1u);
            free(elements);
        }
        free(arguments);
        return EQUATION_CALL_VALUE;
    }
    if (reusable) {
        evaluated_call = (Atom *)call;
    } else {
        Atom **elements = calloc(
            (size_t)arity + 1u, sizeof(*elements));
        if (!elements) {
            free(arguments);
            (void)equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic match call");
            return EQUATION_CALL_FAILED;
        }
        elements[0] = atom_symbol_id(context->arena, head);
        for (uint32_t index = 0u; index < arity; index++)
            elements[index + 1u] = arguments[index];
        evaluated_call = atom_expr(
            context->arena, elements, (CettaExprLen)arity + 1u);
        free(elements);
    }
    free(arguments);
    for (uint32_t offset = 0u; offset < candidate_count; offset++) {
        const DeterministicEquationRuleV1 *candidate =
            &context->plan->rules[first + offset];
        DeterministicEquationEnvironmentV1 candidate_environment = {0};
        if (match_pattern(
                candidate->left, evaluated_call,
                &candidate_environment, context, stack->count + 1u)) {
            match_count++;
            if (match_count == 1u) {
                matched = candidate;
                matched_environment = candidate_environment;
                memset(&candidate_environment, 0,
                       sizeof(candidate_environment));
            }
        } else if (context->status !=
                   CETTA_DETERMINISTIC_EQUATION_V1_OK) {
            free(candidate_environment.items);
            free(matched_environment.items);
            return EQUATION_CALL_FAILED;
        }
        free(candidate_environment.items);
    }
    if (match_count == 0u) {
        free(matched_environment.items);
        (void)equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "defined deterministic equation call %s/%u has no matching rule",
            atom_name_cstr(call->expr.elems[0]), arity);
        return EQUATION_CALL_FAILED;
    }
    if (match_count != 1u) {
        free(matched_environment.items);
        (void)equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE,
            "defined deterministic equation call matches multiple rules");
        return EQUATION_CALL_FAILED;
    }
    {
        DeterministicEquationEnvironmentV1 *owned = malloc(sizeof(*owned));
        DeterministicEquationFrameV1 cleanup;
        if (!owned) {
            free(matched_environment.items);
            (void)equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic rule environment");
            return EQUATION_CALL_FAILED;
        }
        *owned = matched_environment;
        cleanup = (DeterministicEquationFrameV1){
            .kind = EQUATION_FRAME_RULE_ENVIRONMENT,
            .as.rule_environment.environment = owned,
        };
        if (!equation_stack_push(context, stack, &cleanup)) {
            free(owned->items);
            free(owned);
            return EQUATION_CALL_FAILED;
        }
        *next_term = matched->right;
        *next_environment = owned;
        return EQUATION_CALL_DESCEND;
    }
}

static Atom *evaluate_term(
    DeterministicEquationContextV1 *context, const Atom *root,
    DeterministicEquationEnvironmentV1 *root_environment) {
    DeterministicEquationStackV1 stack = {0};
    const Atom *term = root;
    DeterministicEquationEnvironmentV1 *environment = root_environment;
    Atom *value = NULL;
    bool have_value = false;

    while (true) {
        if (!have_value) {
            const Atom *binding;
            if (!equation_take_work(context, stack.count) || !term)
                goto fail;
            if (term->kind == ATOM_VAR) {
                binding = environment_lookup(environment, term->var_id);
                if (!binding) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_NON_GROUND_TERM,
                        "deterministic equation right side has an unbound variable");
                    goto fail;
                }
                value = equation_copy_atom(context, binding);
                if (!value)
                    goto fail;
                have_value = true;
                continue;
            }
            if (term->kind != ATOM_EXPR) {
                value = equation_copy_atom(context, term);
                if (!value)
                    goto fail;
                have_value = true;
                continue;
            }
            if (term->expr.len == 0u) {
                /* An empty tuple is data, not a call with a missing head. */
                value = equation_copy_atom(context, term);
                if (!value)
                    goto fail;
                have_value = true;
                continue;
            }
            if (expression_head(term, "let", 3u)) {
                DeterministicEquationFrameV1 frame;
                const Atom *binder = term->expr.elems[1];
                if (!binder || binder->kind != ATOM_VAR) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE,
                        "deterministic let binder is not a variable");
                    goto fail;
                }
                frame = (DeterministicEquationFrameV1){
                    .kind = EQUATION_FRAME_LET_VALUE,
                    .as.let_value = {
                        .environment = environment,
                        .binder = binder,
                        .body = term->expr.elems[3],
                    },
                };
                if (!equation_stack_push(context, &stack, &frame))
                    goto fail;
                term = term->expr.elems[2];
                continue;
            }
            if (expression_head(term, "metta-nullary", 1u)) {
                Atom *head;
                if (!term->expr.elems[1] ||
                    term->expr.elems[1]->kind != ATOM_SYMBOL) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE,
                        "metta-nullary expects one literal constructor symbol");
                    goto fail;
                }
                head = atom_symbol_id(
                    context->arena, term->expr.elems[1]->sym_id);
                value = atom_expr(context->arena, &head, 1u);
                have_value = true;
                continue;
            }
            if (!term->expr.elems[0] ||
                term->expr.elems[0]->kind != ATOM_SYMBOL) {
                DeterministicEquationFrameV1 frame;
                Atom **elements = calloc(
                    term->expr.len, sizeof(*elements));
                if (!elements) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                        "cannot allocate deterministic constructor frame");
                    goto fail;
                }
                elements[0] = equation_copy_atom(
                    context, term->expr.elems[0]);
                if (!elements[0]) {
                    free(elements);
                    goto fail;
                }
                if (term->expr.len == 1u) {
                    value = equation_term_reusable(context, term) &&
                            elements[0] == term->expr.elems[0]
                        ? (Atom *)term
                        : atom_expr(context->arena, elements, 1u);
                    free(elements);
                    have_value = true;
                    continue;
                }
                frame = (DeterministicEquationFrameV1){
                    .kind = EQUATION_FRAME_CONSTRUCTOR,
                    .as.constructor = {
                        .term = term,
                        .environment = environment,
                        .elements = elements,
                        .next = 1u,
                        .reusable = equation_term_reusable(context, term) &&
                            elements[0] == term->expr.elems[0],
                    },
                };
                if (!equation_stack_push(context, &stack, &frame)) {
                    equation_frame_dispose(&frame);
                    goto fail;
                }
                term = term->expr.elems[1];
                continue;
            }
            {
                uint32_t arity;
                SymbolId head;
                Atom **arguments = NULL;
                bool reusable = equation_term_reusable(context, term);
                if (term->expr.len - 1u > UINT32_MAX) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                        "deterministic equation call arity is too large");
                    goto fail;
                }
                arity = (uint32_t)(term->expr.len - 1u);
                head = term->expr.elems[0]->sym_id;
                if (arity > 0u) {
                    DeterministicEquationFrameV1 frame;
                    arguments = calloc(arity, sizeof(*arguments));
                    if (!arguments) {
                        (void)equation_fail(
                            context,
                            CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                            "cannot allocate deterministic equation argument frame");
                        goto fail;
                    }
                    frame = (DeterministicEquationFrameV1){
                        .kind = EQUATION_FRAME_CALL,
                        .as.call = {
                            .term = term,
                            .environment = environment,
                            .arguments = arguments,
                            .arity = arity,
                            .next = 0u,
                            .head = head,
                            .reusable = reusable,
                        },
                    };
                    if (!equation_stack_push(context, &stack, &frame)) {
                        equation_frame_dispose(&frame);
                        goto fail;
                    }
                    term = term->expr.elems[1];
                    continue;
                } else {
                    const Atom *next_term = NULL;
                    DeterministicEquationEnvironmentV1 *next_env = NULL;
                    DeterministicEquationCallOutcomeV1 outcome =
                        equation_complete_call(
                            context, &stack, term, NULL, 0u, head,
                            reusable, &value, &next_term, &next_env);
                    if (outcome == EQUATION_CALL_FAILED)
                        goto fail;
                    if (outcome == EQUATION_CALL_DESCEND) {
                        term = next_term;
                        environment = next_env;
                    } else {
                        have_value = true;
                    }
                    continue;
                }
            }
        }

        if (stack.count == 0u) {
            free(stack.items);
            return value;
        }
        {
            DeterministicEquationFrameV1 frame =
                stack.items[--stack.count];
            switch (frame.kind) {
            case EQUATION_FRAME_CONSTRUCTOR: {
                CettaExprIndex index = frame.as.constructor.next;
                frame.as.constructor.elements[index] = value;
                if (value != frame.as.constructor.term->expr.elems[index])
                    frame.as.constructor.reusable = false;
                frame.as.constructor.next++;
                if (frame.as.constructor.next <
                    frame.as.constructor.term->expr.len) {
                    term = frame.as.constructor.term->expr.elems[
                        frame.as.constructor.next];
                    environment = frame.as.constructor.environment;
                    if (!equation_stack_push(context, &stack, &frame)) {
                        equation_frame_dispose(&frame);
                        goto fail;
                    }
                    have_value = false;
                } else {
                    value = frame.as.constructor.reusable
                        ? (Atom *)frame.as.constructor.term
                        : atom_expr(
                              context->arena,
                              frame.as.constructor.elements,
                              frame.as.constructor.term->expr.len);
                    free(frame.as.constructor.elements);
                    have_value = true;
                }
                break;
            }
            case EQUATION_FRAME_CALL: {
                uint32_t index = frame.as.call.next;
                const Atom *next_term = NULL;
                DeterministicEquationEnvironmentV1 *next_env = NULL;
                DeterministicEquationCallOutcomeV1 outcome;
                frame.as.call.arguments[index] = value;
                if (value != frame.as.call.term->expr.elems[index + 1u])
                    frame.as.call.reusable = false;
                frame.as.call.next++;
                if (frame.as.call.next < frame.as.call.arity) {
                    term = frame.as.call.term->expr.elems[
                        frame.as.call.next + 1u];
                    environment = frame.as.call.environment;
                    if (!equation_stack_push(context, &stack, &frame)) {
                        equation_frame_dispose(&frame);
                        goto fail;
                    }
                    have_value = false;
                    break;
                }
                outcome = equation_complete_call(
                    context, &stack, frame.as.call.term,
                    frame.as.call.arguments, frame.as.call.arity,
                    frame.as.call.head, frame.as.call.reusable,
                    &value, &next_term, &next_env);
                frame.as.call.arguments = NULL;
                if (outcome == EQUATION_CALL_FAILED)
                    goto fail;
                if (outcome == EQUATION_CALL_DESCEND) {
                    term = next_term;
                    environment = next_env;
                    have_value = false;
                } else {
                    have_value = true;
                }
                break;
            }
            case EQUATION_FRAME_LET_VALUE: {
                DeterministicEquationFrameV1 body_frame;
                uint32_t saved_count =
                    frame.as.let_value.environment->count;
                if (!environment_push(
                        frame.as.let_value.environment,
                        frame.as.let_value.binder->var_id, value)) {
                    (void)equation_fail(
                        context,
                        CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                        "cannot allocate deterministic let binding");
                    goto fail;
                }
                body_frame = (DeterministicEquationFrameV1){
                    .kind = EQUATION_FRAME_LET_BODY,
                    .as.let_body = {
                        .environment = frame.as.let_value.environment,
                        .saved_count = saved_count,
                    },
                };
                if (!equation_stack_push(context, &stack, &body_frame)) {
                    frame.as.let_value.environment->count = saved_count;
                    goto fail;
                }
                term = frame.as.let_value.body;
                environment = frame.as.let_value.environment;
                have_value = false;
                break;
            }
            case EQUATION_FRAME_LET_BODY:
                frame.as.let_body.environment->count =
                    frame.as.let_body.saved_count;
                have_value = true;
                break;
            case EQUATION_FRAME_RULE_ENVIRONMENT:
                free(frame.as.rule_environment.environment->items);
                free(frame.as.rule_environment.environment);
                have_value = true;
                break;
            }
        }
    }

fail:
    equation_stack_dispose(&stack);
    return NULL;
}

bool cetta_deterministic_equation_plan_v1_run_counted(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    DeterministicEquationContextV1 context;
    DeterministicEquationEnvironmentV1 environment = {0};
    ArenaMark mark;

    if (error && error_size)
        error[0] = '\0';
    if (status)
        *status = CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (work_used)
        *work_used = 0u;
    if (!plan || !call || !arena || !out || depth_limit == 0u ||
        work_limit == 0u)
        return equation_error(
            error, error_size,
            "invalid deterministic equation execution request");
    mark = arena_mark(arena);
    context = (DeterministicEquationContextV1){
        .plan = plan,
        .primitive = primitive,
        .primitive_context = primitive_context,
        .arena = arena,
        .depth_limit = depth_limit,
        .work_remaining = work_limit,
        .status = CETTA_DETERMINISTIC_EQUATION_V1_OK,
        .error = error,
        .error_size = error_size,
    };
    context.copy_session = atom_deep_copy_session_new(arena);
    if (!context.copy_session) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        return equation_error(
            error, error_size,
            "cannot allocate deterministic equation copy session");
    }
    *out = evaluate_term(&context, call, &environment);
    if (work_used)
        *work_used = work_limit - context.work_remaining;
    free(environment.items);
    atom_deep_copy_session_free(context.copy_session);
    if (!*out) {
        arena_reset(arena, mark);
        if (status)
            *status = context.status;
        return false;
    }
    if (atom_has_vars(*out)) {
        arena_reset(arena, mark);
        *out = NULL;
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_NON_GROUND_TERM;
        return equation_error(
            error, error_size,
            "deterministic equation result is not ground");
    }
    if (status)
        *status = CETTA_DETERMINISTIC_EQUATION_V1_OK;
    return true;
}

bool cetta_deterministic_equation_plan_v1_run(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    return cetta_deterministic_equation_plan_v1_run_counted(
        plan, call, primitive, primitive_context,
        arena, depth_limit, work_limit, NULL,
        out, status, error, error_size);
}

const char *cetta_deterministic_equation_status_name_v1(
    CettaDeterministicEquationStatusV1 status) {
    switch (status) {
    case CETTA_DETERMINISTIC_EQUATION_V1_OK:
        return "ok";
    case CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT:
        return "bad_argument";
    case CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION:
        return "invalid_presentation";
    case CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE:
        return "unsupported_rule";
    case CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE:
        return "no_rule";
    case CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE:
        return "ambiguous_rule";
    case CETTA_DETERMINISTIC_EQUATION_V1_NON_GROUND_TERM:
        return "non_ground_term";
    case CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT:
        return "primitive_fault";
    case CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT:
        return "resource_limit";
    default:
        return "unknown_status";
    }
}
