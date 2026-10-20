#include "deterministic_equation_plan_v1.h"

#include "deterministic_equation_descent_v1.h"
#include "finite_horn_gslt_v1.h"
#include "gslt_composition_v1.h"
#include "src/symbol.h"
#include "prepared_bytes_v1.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static atomic_uint_fast64_t source_loads, source_admissions, prepared_restores, prepared_audits;

CettaDeterministicEquationPreparationV1 cetta_deterministic_equation_plan_v1_preparation(void) {
    return (CettaDeterministicEquationPreparationV1){
        atomic_load(&source_loads), atomic_load(&source_admissions), atomic_load(&prepared_restores), atomic_load(&prepared_audits)};
}

typedef enum {
    EQUATION_FIRST_ANY = 0,
    EQUATION_FIRST_EXPRESSION,
    EQUATION_FIRST_SYMBOL
} DeterministicEquationFirstV1;

typedef struct {
    const char *name;
    const Atom *left;
    const Atom *right;
    SymbolId head;
    uint32_t arity;
    /* The first argument pattern's shape: a symbol-headed expression of
     * first_len elements, a symbol, or anything else (always a candidate). */
    DeterministicEquationFirstV1 first;
    SymbolId first_head;
    CettaExprLen first_len;
} DeterministicEquationRuleV1;

typedef struct {
    const char *name;
    size_t arity;
} DeterministicEquationOperatorV1;

/* The rules of one head, a contiguous run of the name-sorted rules. */
typedef struct {
    SymbolId head;
    uint32_t first;
    uint32_t count;
} DeterministicEquationHeadV1;

struct CettaDeterministicEquationPlanV1 {
    Arena source_arena;
    DeterministicEquationRuleV1 *rules;
    uint32_t rule_count;
    /* Open addressing by symbol id over the heads of rules; a call finds its
     * candidates without comparing symbol spellings. */
    DeterministicEquationHeadV1 *heads;
    uint32_t head_capacity;
    /* Each rule's left side and right side compiled to environment slots,
     * for executions with no work or depth bound and no primitive. */
    struct DeterministicEquationCompiledRuleV1 *compiled;
    DeterministicEquationOperatorV1 *operators;
    size_t operator_count;
    /* The primitives the plan was admitted with, and runs with. */
    CettaDeterministicVocabularyV1 vocabulary;
};

static bool plan_index_heads(CettaDeterministicEquationPlanV1 *plan);
static void plan_compile_rules(CettaDeterministicEquationPlanV1 *plan);

bool cetta_deterministic_equation_plan_v1_declares_operator(
    const CettaDeterministicEquationPlanV1 *plan, Atom *head, uint32_t arity) {
    if (!plan || !head || head->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0; i < plan->operator_count; ++i)
        if (plan->operators[i].arity == arity && atom_is_symbol(head, plan->operators[i].name)) return true;
    return false;
}

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
    uint64_t work_used;
    bool work_bounded;
    CettaDeterministicEquationStatusV1 status;
    char *error;
    size_t error_size;
    /* A nullary call is a constant: its value is computed once per run. */
    SymbolId *memo_heads;
    Atom **memo_values;
    uint32_t memo_count;
    uint32_t memo_capacity;
    /* The head symbol atoms of the values built in this run, allocated once
     * each in the result arena: open addressing by symbol id. */
    SymbolId *head_ids;
    Atom **head_atoms;
    uint32_t head_count;
    uint32_t head_capacity;
    /* Argument and element arrays live exactly as long as their frame, so
     * they are released in reverse allocation order. */
    struct DeterministicEquationScratchV1 *scratch, *scratch_spare;
    /* One reusable environment for rule-matching attempts; only the match
     * that succeeds is copied into an environment of its own. */
    DeterministicEquationEnvironmentV1 attempt;
    SymbolId let_symbol, nullary_symbol;
    /* No work or depth bound applies and work is not reported, so a rule
     * whose first pattern cannot match need not be attempted. */
    bool uncounted;
} DeterministicEquationContextV1;

typedef struct DeterministicEquationScratchV1 {
    struct DeterministicEquationScratchV1 *below;
    size_t used;
    size_t capacity;
    Atom *items[];
} DeterministicEquationScratchV1;

static Atom **equation_scratch_push(DeterministicEquationContextV1 *context, size_t count) {
    DeterministicEquationScratchV1 *top = context->scratch;
    if (!count)
        return NULL;
    if (!top || top->capacity - top->used < count) {
        DeterministicEquationScratchV1 *block = context->scratch_spare;
        if (block && block->capacity >= count) {
            context->scratch_spare = NULL;
        } else {
            size_t capacity = count > 4096u ? count : 4096u;
            if (capacity > (SIZE_MAX - sizeof(*block)) / sizeof(Atom *))
                return NULL;
            block = malloc(sizeof(*block) + capacity * sizeof(Atom *));
            if (!block)
                return NULL;
            block->capacity = capacity;
        }
        block->used = 0u;
        block->below = top;
        context->scratch = top = block;
    }
    Atom **items = &top->items[top->used];
    memset(items, 0, count * sizeof(*items));
    top->used += count;
    return items;
}

static void equation_scratch_pop(DeterministicEquationContextV1 *context, Atom **items, size_t count) {
    DeterministicEquationScratchV1 *top = context->scratch;
    if (!items || !count)
        return;
    /* Frames are strictly nested; any other order is an evaluator defect. */
    if (!top || top->used < count || &top->items[top->used - count] != items)
        abort();
    top->used -= count;
    if (!top->used && top->below) {
        context->scratch = top->below;
        free(context->scratch_spare);
        context->scratch_spare = top;
    }
}

static void equation_scratch_free(DeterministicEquationContextV1 *context) {
    while (context->scratch) {
        DeterministicEquationScratchV1 *below = context->scratch->below;
        free(context->scratch);
        context->scratch = below;
    }
    free(context->scratch_spare);
    context->scratch_spare = NULL;
}

static int equation_rule_compare(const void *left_opaque,
                                 const void *right_opaque) {
    const DeterministicEquationRuleV1 *left = left_opaque;
    const DeterministicEquationRuleV1 *right = right_opaque;

    int names = symbol_compare(g_symbols, left->head, right->head);
    if (names) return names;
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
    if (context->depth_limit && depth > context->depth_limit)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exceeded its depth limit");
    if (context->work_bounded && context->work_remaining == 0u)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exhausted its work limit");
    if (context->work_bounded) context->work_remaining--;
    if (context->work_used != UINT64_MAX) context->work_used++;
    return true;
}

static bool equation_symbol_is(const Atom *atom, SymbolId id) {
    return id != SYMBOL_ID_NONE && atom && atom->kind == ATOM_SYMBOL && atom->sym_id == id;
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
            if (var_id_eq((*variables)[index], term->var_id))
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
        if (var_id_eq(variables[index - 1u], variable))
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
    const CettaDeterministicVocabularyV1 *vocabulary,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    CettaDeterministicEquationPlanV1 *plan = NULL;
    size_t source_count;
    uint32_t selected = 0u;
    CettaGsltCompositionV1 composition = {0};

    atomic_fetch_add(&source_admissions, 1u);

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
    if (!plan_index_heads(plan)) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        (void)equation_error(error, error_size,
                             "cannot index deterministic equation heads");
        goto fail;
    }
    if (vocabulary)
        plan->vocabulary = *vocabulary;
    {
        /* Admit only programs whose calls descend: every evaluation of an
         * admitted program ends. */
        CettaDescentRuleV1 *descent = calloc(selected, sizeof(*descent));
        if (!descent) {
            if (status)
                *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
            (void)equation_error(error, error_size,
                                 "cannot allocate the descent check");
            goto fail;
        }
        for (uint32_t index = 0u; index < selected; index++)
            descent[index] = (CettaDescentRuleV1){
                .name = plan->rules[index].name,
                .left = plan->rules[index].left,
                .right = plan->rules[index].right,
            };
        bool resource_failure = false;
        bool descends = cetta_deterministic_equation_descends_v1(
            descent, selected, &plan->vocabulary, &resource_failure,
            error, error_size);
        free(descent);
        if (!descends) {
            if (status)
                *status = resource_failure
                    ? CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT
                    : CETTA_DETERMINISTIC_EQUATION_V1_NON_DESCENDING;
            goto fail;
        }
    }
    plan->operators = calloc(
        composition.operator_count ? composition.operator_count : 1u,
        sizeof(*plan->operators));
    if (!plan->operators) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        (void)equation_error(error, error_size,
                             "cannot allocate deterministic equation signature");
        goto fail;
    }
    for (size_t index = 0u; index < composition.operator_count; index++) {
        const char *name = composition.operators[index].name;
        plan->operators[index] = (DeterministicEquationOperatorV1){
            .name = arena_strdup(&plan->source_arena, name),
            .arity = composition.operators[index].arity,
        };
    }
    plan->operator_count = composition.operator_count;
    plan_compile_rules(plan);
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
    const CettaDeterministicVocabularyV1 *vocabulary,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    FHGSLTPackage *source = NULL;
    atomic_fetch_add(&source_loads, 1u);

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
    bool ok = equation_plan_from_source(
        source, vocabulary, out, status, error, error_size);
    fhgslt_package_free(source);
    return ok;
}

bool cetta_deterministic_equation_plan_v1_load_inputs(
    const CettaDeterministicEquationInputV1 *inputs, size_t input_count,
    const CettaDeterministicVocabularyV1 *vocabulary,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    FHGSLTPackage *source = NULL;
    atomic_fetch_add(&source_loads, 1u);

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
    bool ok = equation_plan_from_source(
        source, vocabulary, out, status, error, error_size);
    fhgslt_package_free(source);
    return ok;
}

void cetta_deterministic_equation_plan_v1_free(
    CettaDeterministicEquationPlanV1 *plan) {
    if (!plan)
        return;
    arena_free(&plan->source_arena);
    free(plan->heads);
    free(plan->compiled);
    free(plan->rules);
    free(plan->operators);
    free(plan);
}

static const Atom *environment_lookup(
    const DeterministicEquationEnvironmentV1 *environment,
    VarId variable) {
    if (!environment)
        return NULL;
    for (uint32_t index = environment->count; index > 0u; index--) {
        if (var_id_eq(environment->items[index - 1u].variable, variable))
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

static uint32_t plan_head_slot(SymbolId head, uint32_t capacity) {
    return (uint32_t)((uint64_t)head * UINT64_C(0x9e3779b97f4a7c15) >> 32) & (capacity - 1u);
}

/* Index the heads of name-sorted rules. Every rule of a head lies in one
 * contiguous run, ordered by arity. */
static void plan_rule_first(DeterministicEquationRuleV1 *rule) {
    rule->first = EQUATION_FIRST_ANY;
    if (rule->arity == 0u)
        return;
    const Atom *pattern = rule->left->expr.elems[1];
    if (pattern && pattern->kind == ATOM_SYMBOL) {
        rule->first = EQUATION_FIRST_SYMBOL;
        rule->first_head = pattern->sym_id;
    } else if (pattern && pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
               pattern->expr.elems[0] && pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        rule->first = EQUATION_FIRST_EXPRESSION;
        rule->first_head = pattern->expr.elems[0]->sym_id;
        rule->first_len = pattern->expr.len;
    }
}

static bool plan_index_heads(CettaDeterministicEquationPlanV1 *plan) {
    uint32_t distinct = 0u, capacity = 8u;
    for (uint32_t i = 0u; i < plan->rule_count; i++)
        plan_rule_first(&plan->rules[i]);
    for (uint32_t i = 0u; i < plan->rule_count; i++)
        if (!i || plan->rules[i].head != plan->rules[i - 1u].head)
            distinct++;
    while (capacity < UINT32_MAX / 2u && capacity / 2u < distinct)
        capacity *= 2u;
    if (capacity / 2u < distinct)
        return false;
    DeterministicEquationHeadV1 *heads = calloc(capacity, sizeof(*heads));
    if (!heads)
        return false;
    for (uint32_t i = 0u; i < plan->rule_count; i++) {
        if (i && plan->rules[i].head == plan->rules[i - 1u].head)
            continue;
        uint32_t end = i + 1u;
        while (end < plan->rule_count && plan->rules[end].head == plan->rules[i].head)
            end++;
        uint32_t slot = plan_head_slot(plan->rules[i].head, capacity);
        while (heads[slot].count) {
            if (heads[slot].head == plan->rules[i].head) {
                /* A head's rules must be one contiguous run. */
                free(heads);
                return false;
            }
            slot = (slot + 1u) & (capacity - 1u);
        }
        heads[slot] = (DeterministicEquationHeadV1){plan->rules[i].head, i, end - i};
    }
    free(plan->heads);
    plan->heads = heads;
    plan->head_capacity = capacity;
    return true;
}

static const DeterministicEquationHeadV1 *plan_head(
    const CettaDeterministicEquationPlanV1 *plan, SymbolId head) {
    if (!plan || !plan->head_capacity)
        return NULL;
    for (uint32_t slot = plan_head_slot(head, plan->head_capacity);;
         slot = (slot + 1u) & (plan->head_capacity - 1u)) {
        const DeterministicEquationHeadV1 *entry = &plan->heads[slot];
        if (!entry->count)
            return NULL;
        if (entry->head == head)
            return entry;
    }
}

static bool plan_rule_range(
    const CettaDeterministicEquationPlanV1 *plan,
    SymbolId head, uint32_t arity,
    uint32_t *first, uint32_t *count) {
    if (first)
        *first = 0u;
    if (count)
        *count = 0u;
    const DeterministicEquationHeadV1 *entry = plan_head(plan, head);
    if (!entry)
        return false;
    uint32_t begin = entry->first, stop = entry->first + entry->count;
    while (begin < stop && plan->rules[begin].arity < arity)
        begin++;
    if (begin == stop || plan->rules[begin].arity != arity)
        return false;
    uint32_t end = begin + 1u;
    while (end < stop && plan->rules[end].arity == arity)
        end++;
    if (first)
        *first = begin;
    if (count)
        *count = end - begin;
    return true;
}

static bool plan_defines_symbol(
    const CettaDeterministicEquationPlanV1 *plan, SymbolId head) {
    return plan_head(plan, head) != NULL;
}

typedef enum {
    EQUATION_FRAME_CONSTRUCTOR = 0,
    EQUATION_FRAME_CALL,
    EQUATION_FRAME_LET_VALUE,
    EQUATION_FRAME_LET_BODY,
    EQUATION_FRAME_RULE_ENVIRONMENT,
    EQUATION_FRAME_MEMOIZE
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
        struct {
            SymbolId head;
        } memoize;
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

static void equation_frame_dispose(DeterministicEquationContextV1 *context,
                                   DeterministicEquationFrameV1 *frame) {
    if (!frame)
        return;
    switch (frame->kind) {
    case EQUATION_FRAME_CONSTRUCTOR:
        equation_scratch_pop(context, frame->as.constructor.elements,
                             frame->as.constructor.term->expr.len);
        break;
    case EQUATION_FRAME_CALL:
        equation_scratch_pop(context, frame->as.call.arguments, frame->as.call.arity);
        break;
    case EQUATION_FRAME_RULE_ENVIRONMENT:
        if (frame->as.rule_environment.environment) {
            free(frame->as.rule_environment.environment->items);
            free(frame->as.rule_environment.environment);
        }
        break;
    case EQUATION_FRAME_LET_VALUE:
    case EQUATION_FRAME_LET_BODY:
    case EQUATION_FRAME_MEMOIZE:
        break;
    }
    memset(frame, 0, sizeof(*frame));
}

static void equation_stack_dispose(DeterministicEquationContextV1 *context,
                                   DeterministicEquationStackV1 *stack) {
    if (!stack)
        return;
    while (stack->count > 0u)
        equation_frame_dispose(context, &stack->items[--stack->count]);
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
    if (context->depth_limit && stack->count >= context->depth_limit)
        return equation_fail(
            context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "deterministic equation evaluation exceeded its continuation depth limit");
    if (stack->count == stack->capacity) {
        next = stack->capacity ? stack->capacity * 2u : 64u;
        uint32_t maximum = context->depth_limit ? context->depth_limit : UINT32_MAX;
        if (next < stack->capacity || next > maximum)
            next = maximum;
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
    /* A value already settled in the result arena is its own copy; the
     * session would only record that identity. */
    if (atom_settled_for_arena(context->arena, source))
        return (Atom *)source;
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

static Atom *equation_memo_lookup(
    const DeterministicEquationContextV1 *context, SymbolId head) {
    for (uint32_t i = 0u; i < context->memo_count; i++) {
        if (context->memo_heads[i] == head)
            return context->memo_values[i];
    }
    return NULL;
}

static bool equation_memo_store(
    DeterministicEquationContextV1 *context, SymbolId head, Atom *value) {
    if (context->memo_count == context->memo_capacity) {
        uint32_t capacity = context->memo_capacity ? context->memo_capacity * 2u : 8u;
        SymbolId *heads = realloc(context->memo_heads, sizeof(*heads) * capacity);
        if (!heads)
            return false;
        context->memo_heads = heads;
        Atom **values = realloc(context->memo_values, sizeof(*values) * capacity);
        if (!values)
            return false;
        context->memo_values = values;
        context->memo_capacity = capacity;
    }
    context->memo_heads[context->memo_count] = head;
    context->memo_values[context->memo_count] = value;
    context->memo_count++;
    return true;
}

static Atom *equation_head_atom(
    DeterministicEquationContextV1 *context, SymbolId head) {
    uint32_t slot;
    if (context->head_count * 2u >= context->head_capacity) {
        uint32_t capacity = context->head_capacity ? context->head_capacity * 2u : 256u;
        SymbolId *ids = calloc(capacity, sizeof(*ids));
        Atom **atoms = calloc(capacity, sizeof(*atoms));
        if (!ids || !atoms) {
            free(ids);
            free(atoms);
            return atom_symbol_id(context->arena, head);
        }
        for (uint32_t i = 0u; i < context->head_capacity; i++) {
            if (!context->head_atoms[i])
                continue;
            slot = (uint32_t)(context->head_ids[i] * 2654435761u) & (capacity - 1u);
            while (atoms[slot])
                slot = (slot + 1u) & (capacity - 1u);
            ids[slot] = context->head_ids[i];
            atoms[slot] = context->head_atoms[i];
        }
        free(context->head_ids);
        free(context->head_atoms);
        context->head_ids = ids;
        context->head_atoms = atoms;
        context->head_capacity = capacity;
    }
    slot = (uint32_t)(head * 2654435761u) & (context->head_capacity - 1u);
    while (context->head_atoms[slot]) {
        if (context->head_ids[slot] == head)
            return context->head_atoms[slot];
        slot = (slot + 1u) & (context->head_capacity - 1u);
    }
    context->head_ids[slot] = head;
    context->head_atoms[slot] = atom_symbol_id(context->arena, head);
    if (context->head_atoms[slot])
        context->head_count++;
    return context->head_atoms[slot];
}

/* A rule's left side (f p1 ... pn) against the evaluated arguments of a
 * call to f: the patterns are matched argument by argument, so no call term
 * is built to match. */
static bool match_arguments(
    const Atom *left, Atom *const *arguments, uint32_t arity,
    DeterministicEquationEnvironmentV1 *environment,
    DeterministicEquationContextV1 *context, uint32_t depth) {
    if (!equation_take_work(context, depth) || !left ||
        left->kind != ATOM_EXPR || left->expr.len != (CettaExprLen)arity + 1u)
        return false;
    for (uint32_t index = 0u; index < arity; index++) {
        if (!match_pattern(left->expr.elems[index + 1u], arguments[index],
                           environment, context, depth + 1u))
            return false;
    }
    return true;
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

    *value = NULL;
    *next_term = NULL;
    *next_environment = NULL;
    if (!defined && plan_defines_symbol(context->plan, head)) {
        equation_scratch_pop(context, arguments, arity);
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
            equation_scratch_pop(context, arguments, arity);
            context->status = CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT;
            return EQUATION_CALL_FAILED;
        }
        if (primitive_result == CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED) {
            equation_scratch_pop(context, arguments, arity);
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
            Atom *inline_elements[16];
            Atom **elements = (size_t)arity + 1u <= sizeof(inline_elements) / sizeof(*inline_elements)
                ? inline_elements : calloc((size_t)arity + 1u, sizeof(*elements));
            if (!elements) {
                equation_scratch_pop(context, arguments, arity);
                (void)equation_fail(
                    context,
                    CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                    "cannot allocate deterministic constructor result");
                return EQUATION_CALL_FAILED;
            }
            elements[0] = equation_head_atom(context, head);
            for (uint32_t index = 0u; index < arity; index++)
                elements[index + 1u] = arguments[index];
            *value = atom_expr(
                context->arena, elements, (CettaExprLen)arity + 1u);
            if (elements != inline_elements)
                free(elements);
        }
        equation_scratch_pop(context, arguments, arity);
        return EQUATION_CALL_VALUE;
    }
    (void)reusable;
    /* The loader admits only pairwise exclusive left sides, so the first
     * rule that matches is the only one. Attempts share one environment. */
    DeterministicEquationFirstV1 shape = EQUATION_FIRST_ANY;
    SymbolId shape_head = SYMBOL_ID_NONE;
    CettaExprLen shape_len = 0u;
    if (context->uncounted && arity > 0u && arguments[0]) {
        const Atom *value = arguments[0];
        if (value->kind == ATOM_SYMBOL) {
            shape = EQUATION_FIRST_SYMBOL; shape_head = value->sym_id;
        } else if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
                   value->expr.elems[0] && value->expr.elems[0]->kind == ATOM_SYMBOL) {
            shape = EQUATION_FIRST_EXPRESSION; shape_head = value->expr.elems[0]->sym_id;
            shape_len = value->expr.len;
        } else {
            /* Neither shape can match a symbol or symbol-headed pattern. */
            shape = EQUATION_FIRST_ANY; shape_head = SYMBOL_ID_NONE;
        }
    }
    for (uint32_t offset = 0u; offset < candidate_count && !matched; offset++) {
        const DeterministicEquationRuleV1 *candidate =
            &context->plan->rules[first + offset];
        if (context->uncounted && arity > 0u && candidate->first != EQUATION_FIRST_ANY &&
            (candidate->first != shape || candidate->first_head != shape_head ||
             (shape == EQUATION_FIRST_EXPRESSION && candidate->first_len != shape_len)))
            continue;
        context->attempt.count = 0u;
        if (match_arguments(
                candidate->left, arguments, arity,
                &context->attempt, context, stack->count + 1u)) {
            matched = candidate;
        } else if (context->status !=
                   CETTA_DETERMINISTIC_EQUATION_V1_OK) {
            equation_scratch_pop(context, arguments, arity);
            return EQUATION_CALL_FAILED;
        }
    }
    if (!matched) {
        const char *first_argument = NULL;
        if (arity > 0u && arguments[0]) {
            Atom *first = arguments[0];
            if (first->kind == ATOM_GROUNDED &&
                first->ground.gkind == GV_STRING)
                first_argument = first->ground.sval;
            else if (first->kind == ATOM_SYMBOL)
                first_argument = atom_name_cstr(first);
        }
        if (first_argument) {
            (void)equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
                "defined deterministic equation call %s/%u with first argument %.120s has no matching rule",
                atom_name_cstr(call->expr.elems[0]), arity,
                first_argument);
        } else {
            (void)equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
                "defined deterministic equation call %s/%u has no matching rule",
                atom_name_cstr(call->expr.elems[0]), arity);
        }
        equation_scratch_pop(context, arguments, arity);
        return EQUATION_CALL_FAILED;
    }
    equation_scratch_pop(context, arguments, arity);
    {
        DeterministicEquationEnvironmentV1 *owned = malloc(sizeof(*owned));
        uint32_t bindings = context->attempt.count;
        DeterministicEquationFrameV1 cleanup;
        if (!owned) {
            (void)equation_fail(
                context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic rule environment");
            return EQUATION_CALL_FAILED;
        }
        *owned = (DeterministicEquationEnvironmentV1){0};
        if (bindings) {
            owned->items = malloc((size_t)bindings * sizeof(*owned->items));
            if (!owned->items) {
                free(owned);
                (void)equation_fail(
                    context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                    "cannot allocate deterministic rule environment");
                return EQUATION_CALL_FAILED;
            }
            memcpy(owned->items, context->attempt.items,
                   (size_t)bindings * sizeof(*owned->items));
            owned->count = owned->capacity = bindings;
        }
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
    DeterministicEquationEnvironmentV1 *root_environment,
    bool entry_arguments_are_values) {
    DeterministicEquationStackV1 stack = {0};
    const Atom *term = root;
    DeterministicEquationEnvironmentV1 *environment = root_environment;
    Atom *value = NULL;
    bool have_value = false;

    if (entry_arguments_are_values) {
        if (!root || root->kind != ATOM_EXPR || root->expr.len == 0u ||
            root->expr.len - 1u > UINT32_MAX ||
            !root->expr.elems[0] ||
            root->expr.elems[0]->kind != ATOM_SYMBOL || atom_has_vars(root)) {
            (void)equation_fail(context,
                CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT,
                "deterministic data application expects a ground symbol-headed call");
            goto fail;
        }
        if (!equation_take_work(context, 0u))
            goto fail;
        uint32_t arity = (uint32_t)(root->expr.len - 1u);
        Atom **arguments = equation_scratch_push(context, arity);
        if (arity && !arguments) {
            (void)equation_fail(context,
                CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic data arguments");
            goto fail;
        }
        bool reusable = equation_term_reusable(context, root);
        for (uint32_t index = 0u; index < arity; index++) {
            arguments[index] = equation_copy_atom(
                context, root->expr.elems[index + 1u]);
            if (!arguments[index]) {
                equation_scratch_pop(context, arguments, arity);
                goto fail;
            }
            reusable = reusable &&
                arguments[index] == root->expr.elems[index + 1u];
        }
        const Atom *next_term = NULL;
        DeterministicEquationEnvironmentV1 *next_environment = NULL;
        DeterministicEquationCallOutcomeV1 outcome = equation_complete_call(
            context, &stack, root, arguments, arity,
            root->expr.elems[0]->sym_id, reusable,
            &value, &next_term, &next_environment);
        if (outcome == EQUATION_CALL_FAILED)
            goto fail;
        if (outcome == EQUATION_CALL_DESCEND) {
            term = next_term;
            environment = next_environment;
        } else {
            have_value = true;
        }
    }

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
            if (term->expr.len == 4u &&
                equation_symbol_is(term->expr.elems[0], context->let_symbol)) {
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
            if (term->expr.len == 2u &&
                equation_symbol_is(term->expr.elems[0], context->nullary_symbol)) {
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
                Atom **elements = equation_scratch_push(context, term->expr.len);
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
                    equation_scratch_pop(context, elements, term->expr.len);
                    goto fail;
                }
                if (term->expr.len == 1u) {
                    value = equation_term_reusable(context, term) &&
                            elements[0] == term->expr.elems[0]
                        ? (Atom *)term
                        : atom_expr(context->arena, elements, 1u);
                    equation_scratch_pop(context, elements, term->expr.len);
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
                    equation_frame_dispose(context, &frame);
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
                    arguments = equation_scratch_push(context, arity);
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
                        equation_frame_dispose(context, &frame);
                        goto fail;
                    }
                    term = term->expr.elems[1];
                    continue;
                } else {
                    const Atom *next_term = NULL;
                    DeterministicEquationEnvironmentV1 *next_env = NULL;
                    Atom *memoized = equation_memo_lookup(context, head);
                    if (memoized) {
                        value = memoized;
                        have_value = true;
                        continue;
                    }
                    DeterministicEquationCallOutcomeV1 outcome =
                        equation_complete_call(
                            context, &stack, term, NULL, 0u, head,
                            reusable, &value, &next_term, &next_env);
                    if (outcome == EQUATION_CALL_FAILED)
                        goto fail;
                    if (outcome == EQUATION_CALL_DESCEND) {
                        /* A defined nullary call: remember its value. */
                        DeterministicEquationFrameV1 memo_frame = {
                            .kind = EQUATION_FRAME_MEMOIZE,
                            .as.memoize = {.head = head},
                        };
                        if (!equation_stack_push(context, &stack, &memo_frame))
                            goto fail;
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
                        equation_frame_dispose(context, &frame);
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
                    equation_scratch_pop(context, frame.as.constructor.elements,
                                         frame.as.constructor.term->expr.len);
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
                        equation_frame_dispose(context, &frame);
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
            case EQUATION_FRAME_MEMOIZE:
                if (!equation_memo_store(context, frame.as.memoize.head, value)) {
                    (void)equation_fail(
                        context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                        "cannot remember a deterministic constant");
                    goto fail;
                }
                have_value = true;
                break;
            }
        }
    }

fail:
    equation_stack_dispose(context, &stack);
    return NULL;
}

/* Compiled rules. A rule's variables become environment slots, its left
 * side a matching program and its right side a template whose evaluation
 * order and values are those of evaluate_term: arguments left to right, a
 * defined call matched against its first applicable rule, constructors built
 * in the result arena, nullary constants remembered. Only executions that
 * can observe no step count use them. */
typedef enum {
    EQUATION_TEMPLATE_SLOT, EQUATION_TEMPLATE_COPY, EQUATION_TEMPLATE_NULLARY,
    EQUATION_TEMPLATE_TUPLE, EQUATION_TEMPLATE_CONSTRUCTOR, EQUATION_TEMPLATE_CALL,
    EQUATION_TEMPLATE_ARITY, EQUATION_TEMPLATE_LET
} DeterministicEquationTemplateKindV1;

typedef struct DeterministicEquationTemplateV1 {
    DeterministicEquationTemplateKindV1 kind;
    uint32_t slot;
    uint32_t len;
    SymbolId head;
    uint32_t first, count;
    const Atom *atom;
    struct DeterministicEquationTemplateV1 **children;
} DeterministicEquationTemplateV1;

typedef enum {
    EQUATION_PATTERN_BIND, EQUATION_PATTERN_SAME, EQUATION_PATTERN_ATOM, EQUATION_PATTERN_EXPRESSION
} DeterministicEquationPatternKindV1;

typedef struct DeterministicEquationPatternV1 {
    DeterministicEquationPatternKindV1 kind;
    uint32_t slot;
    CettaExprLen len;
    const Atom *atom;
    struct DeterministicEquationPatternV1 **children;
} DeterministicEquationPatternV1;

typedef struct DeterministicEquationCompiledRuleV1 {
    DeterministicEquationPatternV1 **arguments;
    DeterministicEquationTemplateV1 *right;
    uint32_t slots;
} DeterministicEquationCompiledRuleV1;

typedef struct {
    VarId *variables;
    uint32_t *slots;
    uint32_t len, capacity, next_slot;
} DeterministicEquationScopeV1;

static bool equation_scope_push(DeterministicEquationScopeV1 *scope, VarId variable, uint32_t *slot) {
    if (scope->len == scope->capacity) {
        uint32_t capacity = scope->capacity ? scope->capacity * 2u : 16u;
        if (capacity <= scope->capacity) return false;
        VarId *variables = realloc(scope->variables, (size_t)capacity * sizeof(*variables));
        if (!variables) return false;
        scope->variables = variables;
        uint32_t *slots = realloc(scope->slots, (size_t)capacity * sizeof(*slots));
        if (!slots) return false;
        scope->slots = slots;
        scope->capacity = capacity;
    }
    if (scope->next_slot == UINT32_MAX) return false;
    *slot = scope->next_slot++;
    scope->variables[scope->len] = variable;
    scope->slots[scope->len++] = *slot;
    return true;
}

static bool equation_scope_find(const DeterministicEquationScopeV1 *scope, VarId variable, uint32_t *slot) {
    for (uint32_t i = scope->len; i > 0u; i--)
        if (var_id_eq(scope->variables[i - 1u], variable)) { *slot = scope->slots[i - 1u]; return true; }
    return false;
}

static DeterministicEquationPatternV1 *equation_compile_pattern(
    Arena *arena, const Atom *pattern, DeterministicEquationScopeV1 *scope) {
    if (!pattern) return NULL;
    DeterministicEquationPatternV1 *out = arena_alloc(arena, sizeof(*out));
    memset(out, 0, sizeof(*out));
    if (pattern->kind == ATOM_VAR) {
        if (equation_scope_find(scope, pattern->var_id, &out->slot)) out->kind = EQUATION_PATTERN_SAME;
        else if (equation_scope_push(scope, pattern->var_id, &out->slot)) out->kind = EQUATION_PATTERN_BIND;
        else return NULL;
        return out;
    }
    if (pattern->kind != ATOM_EXPR) {
        out->kind = EQUATION_PATTERN_ATOM; out->atom = pattern;
        return out;
    }
    out->kind = EQUATION_PATTERN_EXPRESSION; out->len = pattern->expr.len;
    out->children = pattern->expr.len ? arena_alloc(arena, pattern->expr.len * sizeof(*out->children)) : NULL;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++)
        if (!(out->children[i] = equation_compile_pattern(arena, pattern->expr.elems[i], scope))) return NULL;
    return out;
}

static DeterministicEquationTemplateV1 *equation_compile_term(
    const CettaDeterministicEquationPlanV1 *plan, Arena *arena, const Atom *term,
    DeterministicEquationScopeV1 *scope, SymbolId let_symbol, SymbolId nullary_symbol) {
    if (!term) return NULL;
    DeterministicEquationTemplateV1 *out = arena_alloc(arena, sizeof(*out));
    memset(out, 0, sizeof(*out));
    out->atom = term;
    if (term->kind == ATOM_VAR) {
        if (!equation_scope_find(scope, term->var_id, &out->slot)) return NULL;
        out->kind = EQUATION_TEMPLATE_SLOT;
        return out;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u) {
        out->kind = EQUATION_TEMPLATE_COPY;
        return out;
    }
    if (term->expr.len == 4u && equation_symbol_is(term->expr.elems[0], let_symbol)) {
        const Atom *binder = term->expr.elems[1];
        if (!binder || binder->kind != ATOM_VAR) return NULL;
        out->kind = EQUATION_TEMPLATE_LET; out->len = 2u;
        out->children = arena_alloc(arena, 2u * sizeof(*out->children));
        if (!(out->children[0] = equation_compile_term(plan, arena, term->expr.elems[2], scope,
                let_symbol, nullary_symbol))) return NULL;
        uint32_t saved = scope->len;
        if (!equation_scope_push(scope, binder->var_id, &out->slot)) return NULL;
        out->children[1] = equation_compile_term(plan, arena, term->expr.elems[3], scope,
            let_symbol, nullary_symbol);
        scope->len = saved;
        return out->children[1] ? out : NULL;
    }
    if (term->expr.len == 2u && equation_symbol_is(term->expr.elems[0], nullary_symbol)) {
        if (!term->expr.elems[1] || term->expr.elems[1]->kind != ATOM_SYMBOL) return NULL;
        out->kind = EQUATION_TEMPLATE_NULLARY; out->head = term->expr.elems[1]->sym_id;
        return out;
    }
    uint32_t start = 0u;
    if (!term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL) {
        out->kind = EQUATION_TEMPLATE_TUPLE; out->len = term->expr.len;
    } else {
        if (term->expr.len - 1u > UINT32_MAX) return NULL;
        out->head = term->expr.elems[0]->sym_id; out->len = (uint32_t)(term->expr.len - 1u); start = 1u;
        if (plan_rule_range(plan, out->head, out->len, &out->first, &out->count)) out->kind = EQUATION_TEMPLATE_CALL;
        else if (plan_defines_symbol(plan, out->head)) out->kind = EQUATION_TEMPLATE_ARITY;
        else out->kind = EQUATION_TEMPLATE_CONSTRUCTOR;
    }
    out->children = out->len ? arena_alloc(arena, out->len * sizeof(*out->children)) : NULL;
    for (uint32_t i = 0u; i < out->len; i++) {
        if (out->kind == EQUATION_TEMPLATE_TUPLE && i == 0u) {
            /* As in evaluate_term, a non-symbol head is inert data, not an
             * equation call or an environment-slot substitution. */
            out->children[i] = arena_alloc(arena, sizeof(*out->children[i]));
            *out->children[i] = (DeterministicEquationTemplateV1){
                .kind = EQUATION_TEMPLATE_COPY, .atom = term->expr.elems[0]};
        } else if (!(out->children[i] = equation_compile_term(plan, arena,
                term->expr.elems[start + i], scope, let_symbol, nullary_symbol))) return NULL;
    }
    return out;
}

/* Compilation is an optimization: a plan any of whose rules cannot be
 * compiled keeps only the interpreter. */
static void plan_compile_rules(CettaDeterministicEquationPlanV1 *plan) {
    free(plan->compiled);
    plan->compiled = NULL;
    if (!plan->rule_count || plan->vocabulary.primitive || !g_symbols) return;
    DeterministicEquationCompiledRuleV1 *compiled = calloc(plan->rule_count, sizeof(*compiled));
    if (!compiled) return;
    SymbolId let_symbol = symbol_lookup_cstr(g_symbols, "let");
    SymbolId nullary_symbol = symbol_lookup_cstr(g_symbols, "metta-nullary");
    DeterministicEquationScopeV1 scope = {0};
    bool ok = true;
    for (uint32_t i = 0u; ok && i < plan->rule_count; i++) {
        const DeterministicEquationRuleV1 *rule = &plan->rules[i];
        DeterministicEquationCompiledRuleV1 *out = &compiled[i];
        scope.len = 0u; scope.next_slot = 0u;
        out->arguments = rule->arity ? arena_alloc(&plan->source_arena, rule->arity * sizeof(*out->arguments)) : NULL;
        for (uint32_t a = 0u; ok && a < rule->arity; a++)
            ok = (out->arguments[a] = equation_compile_pattern(&plan->source_arena,
                rule->left->expr.elems[a + 1u], &scope)) != NULL;
        if (ok) ok = (out->right = equation_compile_term(plan, &plan->source_arena, rule->right,
            &scope, let_symbol, nullary_symbol)) != NULL;
        out->slots = scope.next_slot;
    }
    free(scope.variables); free(scope.slots);
    if (ok) plan->compiled = compiled;
    else free(compiled);
}

static bool equation_match_compiled(const DeterministicEquationPatternV1 *pattern, Atom *value, Atom **env) {
    switch (pattern->kind) {
    case EQUATION_PATTERN_BIND:
        env[pattern->slot] = value;
        return true;
    case EQUATION_PATTERN_SAME:
        return atom_eq(env[pattern->slot], value);
    case EQUATION_PATTERN_ATOM:
        return pattern->atom->kind == value->kind && atom_eq((Atom *)pattern->atom, value);
    case EQUATION_PATTERN_EXPRESSION:
        if (value->kind != ATOM_EXPR || value->expr.len != pattern->len) return false;
        for (CettaExprIndex i = 0u; i < pattern->len; i++)
            if (!equation_match_compiled(pattern->children[i], value->expr.elems[i], env)) return false;
        return true;
    }
    return false;
}

static void equation_no_rule(DeterministicEquationContextV1 *context, const Atom *call,
                             Atom *const *arguments, uint32_t arity) {
    const char *first_argument = NULL;
    if (arity > 0u && arguments[0]) {
        Atom *first = arguments[0];
        if (first->kind == ATOM_GROUNDED && first->ground.gkind == GV_STRING)
            first_argument = first->ground.sval;
        else if (first->kind == ATOM_SYMBOL)
            first_argument = atom_name_cstr(first);
    }
    if (first_argument)
        (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "defined deterministic equation call %s/%u with first argument %.120s has no matching rule",
            atom_name_cstr(call->expr.elems[0]), arity, first_argument);
    else
        (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "defined deterministic equation call %s/%u has no matching rule",
            atom_name_cstr(call->expr.elems[0]), arity);
}

typedef enum {
    EQUATION_STEP_EVAL, EQUATION_STEP_BUILD, EQUATION_STEP_APPLY, EQUATION_STEP_ARITY,
    EQUATION_STEP_BIND, EQUATION_STEP_RELEASE, EQUATION_STEP_MEMOIZE
} DeterministicEquationStepKindV1;

typedef struct {
    DeterministicEquationStepKindV1 kind;
    const DeterministicEquationTemplateV1 *term;
    Atom **env;
    uint32_t size;
} DeterministicEquationStepV1;

typedef struct {
    DeterministicEquationStepV1 *steps;
    uint32_t steps_len, steps_cap;
    Atom **values;
    uint32_t values_len, values_cap;
} DeterministicEquationMachineV1;

static bool equation_machine_step(DeterministicEquationContextV1 *context,
                                  DeterministicEquationMachineV1 *m, DeterministicEquationStepV1 step) {
    if (m->steps_len == m->steps_cap) {
        uint32_t capacity = m->steps_cap ? m->steps_cap * 2u : 256u;
        void *grown = capacity > m->steps_cap ? realloc(m->steps, (size_t)capacity * sizeof(*m->steps)) : NULL;
        if (!grown)
            return equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic equation continuation stack");
        m->steps = grown; m->steps_cap = capacity;
    }
    m->steps[m->steps_len++] = step;
    return true;
}

static bool equation_machine_value(DeterministicEquationContextV1 *context,
                                   DeterministicEquationMachineV1 *m, Atom *value) {
    if (!value) return false;
    if (m->values_len == m->values_cap) {
        uint32_t capacity = m->values_cap ? m->values_cap * 2u : 256u;
        void *grown = capacity > m->values_cap ? realloc(m->values, (size_t)capacity * sizeof(*m->values)) : NULL;
        if (!grown)
            return equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic equation argument frame");
        m->values = grown; m->values_cap = capacity;
    }
    m->values[m->values_len++] = value;
    return true;
}

/* Match the call's arguments (the top `arity` values) against the head's
 * rules, then continue with the matching rule's right side. */
static bool equation_machine_apply(DeterministicEquationContextV1 *context,
                                   DeterministicEquationMachineV1 *m, const Atom *call,
                                   SymbolId head, uint32_t arity, uint32_t first, uint32_t count) {
    Atom **arguments = &m->values[m->values_len - arity];
    if (arity == 0u) {
        Atom *memoized = equation_memo_lookup(context, head);
        if (memoized) return equation_machine_value(context, m, memoized);
    }
    DeterministicEquationFirstV1 shape = EQUATION_FIRST_ANY;
    SymbolId shape_head = SYMBOL_ID_NONE;
    CettaExprLen shape_len = 0u;
    if (arity > 0u) {
        const Atom *value = arguments[0];
        if (value->kind == ATOM_SYMBOL) {
            shape = EQUATION_FIRST_SYMBOL; shape_head = value->sym_id;
        } else if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
                   value->expr.elems[0] && value->expr.elems[0]->kind == ATOM_SYMBOL) {
            shape = EQUATION_FIRST_EXPRESSION; shape_head = value->expr.elems[0]->sym_id;
            shape_len = value->expr.len;
        }
    }
    for (uint32_t offset = 0u; offset < count; offset++) {
        const DeterministicEquationRuleV1 *rule = &context->plan->rules[first + offset];
        if (arity > 0u && rule->first != EQUATION_FIRST_ANY &&
            (rule->first != shape || rule->first_head != shape_head ||
             (shape == EQUATION_FIRST_EXPRESSION && rule->first_len != shape_len)))
            continue;
        const DeterministicEquationCompiledRuleV1 *compiled = &context->plan->compiled[first + offset];
        Atom **env = equation_scratch_push(context, compiled->slots);
        if (compiled->slots && !env)
            return equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic rule environment");
        bool matched = true;
        for (uint32_t a = 0u; matched && a < arity; a++)
            matched = equation_match_compiled(compiled->arguments[a], arguments[a], env);
        if (!matched) { equation_scratch_pop(context, env, compiled->slots); continue; }
        m->values_len -= arity;
        if (arity == 0u &&
            !equation_machine_step(context, m, (DeterministicEquationStepV1){
                .kind = EQUATION_STEP_MEMOIZE, .size = head}))
            return false;
        return equation_machine_step(context, m, (DeterministicEquationStepV1){
                .kind = EQUATION_STEP_RELEASE, .env = env, .size = compiled->slots}) &&
            equation_machine_step(context, m, (DeterministicEquationStepV1){
                .kind = EQUATION_STEP_EVAL, .term = compiled->right, .env = env});
    }
    equation_no_rule(context, call, arguments, arity);
    return false;
}

static Atom *evaluate_compiled(DeterministicEquationContextV1 *context, const Atom *root) {
    DeterministicEquationMachineV1 m = {0};
    Atom *result = NULL;
    if (!root || root->kind != ATOM_EXPR || root->expr.len == 0u ||
        root->expr.len - 1u > UINT32_MAX || !root->expr.elems[0] ||
        root->expr.elems[0]->kind != ATOM_SYMBOL || atom_has_vars(root)) {
        (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT,
            "deterministic data application expects a ground symbol-headed call");
        return NULL;
    }
    uint32_t arity = (uint32_t)(root->expr.len - 1u);
    SymbolId head = root->expr.elems[0]->sym_id;
    bool reusable = equation_term_reusable(context, root);
    for (uint32_t i = 0u; i < arity; i++) {
        Atom *value = equation_copy_atom(context, root->expr.elems[i + 1u]);
        if (!equation_machine_value(context, &m, value)) goto done;
        reusable = reusable && value == root->expr.elems[i + 1u];
    }
    uint32_t first = 0u, count = 0u;
    if (plan_rule_range(context->plan, head, arity, &first, &count)) {
        if (!equation_machine_apply(context, &m, root, head, arity, first, count)) goto done;
    } else if (plan_defines_symbol(context->plan, head)) {
        (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "defined deterministic equation head was called at an unsupported arity");
        goto done;
    } else {
        Atom *inline_elements[16];
        Atom **elements = (size_t)arity + 1u <= sizeof(inline_elements) / sizeof(*inline_elements)
            ? inline_elements : calloc((size_t)arity + 1u, sizeof(*elements));
        if (!elements) {
            (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                "cannot allocate deterministic constructor result");
            goto done;
        }
        elements[0] = equation_head_atom(context, head);
        memcpy(elements + 1, m.values, (size_t)arity * sizeof(*elements));
        result = reusable ? (Atom *)root : atom_expr(context->arena, elements, (CettaExprLen)arity + 1u);
        if (elements != inline_elements) free(elements);
        goto done;
    }
    while (m.steps_len) {
        DeterministicEquationStepV1 step = m.steps[--m.steps_len];
        const DeterministicEquationTemplateV1 *t = step.term;
        switch (step.kind) {
        case EQUATION_STEP_EVAL:
            switch (t->kind) {
            case EQUATION_TEMPLATE_SLOT:
                if (!equation_machine_value(context, &m, equation_copy_atom(context, step.env[t->slot]))) goto done;
                break;
            case EQUATION_TEMPLATE_COPY:
                if (!equation_machine_value(context, &m, equation_copy_atom(context, t->atom))) goto done;
                break;
            case EQUATION_TEMPLATE_NULLARY: {
                Atom *symbol = atom_symbol_id(context->arena, t->head);
                if (!equation_machine_value(context, &m, atom_expr(context->arena, &symbol, 1u))) goto done;
                break;
            }
            case EQUATION_TEMPLATE_LET:
                if (!equation_machine_step(context, &m, (DeterministicEquationStepV1){
                        .kind = EQUATION_STEP_BIND, .term = t, .env = step.env}) ||
                    !equation_machine_step(context, &m, (DeterministicEquationStepV1){
                        .kind = EQUATION_STEP_EVAL, .term = t->children[0], .env = step.env})) goto done;
                break;
            case EQUATION_TEMPLATE_CONSTRUCTOR:
                /* The head occupies the slot before the arguments. */
                if (!equation_machine_value(context, &m, equation_head_atom(context, t->head))) goto done;
                /* fall through */
            case EQUATION_TEMPLATE_TUPLE:
            case EQUATION_TEMPLATE_CALL:
            case EQUATION_TEMPLATE_ARITY: {
                DeterministicEquationStepKindV1 then = t->kind == EQUATION_TEMPLATE_CALL ? EQUATION_STEP_APPLY :
                    t->kind == EQUATION_TEMPLATE_ARITY ? EQUATION_STEP_ARITY : EQUATION_STEP_BUILD;
                if (!equation_machine_step(context, &m, (DeterministicEquationStepV1){
                        .kind = then, .term = t, .env = step.env})) goto done;
                for (uint32_t i = t->len; i > 0u; i--)
                    if (!equation_machine_step(context, &m, (DeterministicEquationStepV1){
                            .kind = EQUATION_STEP_EVAL, .term = t->children[i - 1u], .env = step.env})) goto done;
                break;
            }
            }
            break;
        case EQUATION_STEP_BUILD: {
            uint32_t len = t->kind == EQUATION_TEMPLATE_CONSTRUCTOR ? t->len + 1u : t->len;
            Atom *built = atom_expr(context->arena, &m.values[m.values_len - len], len);
            m.values_len -= len;
            if (!equation_machine_value(context, &m, built)) goto done;
            break;
        }
        case EQUATION_STEP_APPLY:
            if (!equation_machine_apply(context, &m, t->atom, t->head, t->len, t->first, t->count)) goto done;
            break;
        case EQUATION_STEP_ARITY:
            (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
                "defined deterministic equation head was called at an unsupported arity");
            goto done;
        case EQUATION_STEP_BIND:
            step.env[t->slot] = m.values[--m.values_len];
            if (!equation_machine_step(context, &m, (DeterministicEquationStepV1){
                    .kind = EQUATION_STEP_EVAL, .term = t->children[1], .env = step.env})) goto done;
            break;
        case EQUATION_STEP_RELEASE:
            equation_scratch_pop(context, step.env, step.size);
            break;
        case EQUATION_STEP_MEMOIZE:
            if (!equation_memo_store(context, (SymbolId)step.size, m.values[m.values_len - 1u])) {
                (void)equation_fail(context, CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
                    "cannot remember a deterministic constant");
                goto done;
            }
            break;
        }
    }
    if (m.values_len == 1u) result = m.values[0];
done:
    /* A failure leaves environments behind in the scratch stack; the run's
     * scratch is released as a whole by its caller. */
    free(m.steps); free(m.values);
    return result;
}

static bool deterministic_equation_execute(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size, bool entry_arguments_are_values) {
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
    if (!plan || !call || !arena || !out)
        return equation_error(
            error, error_size,
            "invalid deterministic equation execution request");
    if (primitive != plan->vocabulary.primitive)
        return equation_error(
            error, error_size,
            "a deterministic equation plan runs with the primitives it was admitted with");
    mark = arena_mark(arena);
    context = (DeterministicEquationContextV1){
        .plan = plan,
        .primitive = primitive,
        .primitive_context = primitive_context,
        .arena = arena,
        .depth_limit = depth_limit,
        .work_remaining = work_limit,
        .work_bounded = work_limit != 0u,
        .status = CETTA_DETERMINISTIC_EQUATION_V1_OK,
        .error = error,
        .error_size = error_size,
        .let_symbol = symbol_lookup_cstr(g_symbols, "let"),
        .nullary_symbol = symbol_lookup_cstr(g_symbols, "metta-nullary"),
        .uncounted = work_limit == 0u && depth_limit == 0u && !work_used,
    };
    context.copy_session = atom_deep_copy_session_new(arena);
    if (!context.copy_session) {
        if (status)
            *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        return equation_error(
            error, error_size,
            "cannot allocate deterministic equation copy session");
    }
    *out = entry_arguments_are_values && context.uncounted && plan->compiled && !primitive
        ? evaluate_compiled(&context, call)
        : evaluate_term(&context, call, &environment, entry_arguments_are_values);
    if (work_used)
        *work_used = context.work_used;
    free(environment.items);
    free(context.memo_heads);
    free(context.memo_values);
    free(context.head_ids);
    free(context.head_atoms);
    free(context.attempt.items);
    equation_scratch_free(&context);
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

bool cetta_deterministic_equation_plan_v1_run_counted(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    return deterministic_equation_execute(
        plan, call, primitive, primitive_context, arena, depth_limit, work_limit,
        work_used, out, status, error, error_size, false);
}

bool cetta_deterministic_equation_plan_v1_apply(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    return deterministic_equation_execute(
        plan, call, primitive, primitive_context, arena, depth_limit, work_limit,
        NULL, out, status, error, error_size, true);
}

bool cetta_deterministic_equation_plan_v1_apply_counted(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size) {
    return deterministic_equation_execute(
        plan, call, primitive, primitive_context, arena, depth_limit, work_limit,
        work_used, out, status, error, error_size, true);
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
    case CETTA_DETERMINISTIC_EQUATION_V1_NON_DESCENDING:
        return "non_descending";
    default:
        return "unknown_status";
    }
}

uint32_t cetta_deterministic_equation_plan_v1_rule_count(
    const CettaDeterministicEquationPlanV1 *plan) {
    return plan ? plan->rule_count : 0u;
}

bool cetta_deterministic_equation_plan_v1_rule_view(
    const CettaDeterministicEquationPlanV1 *plan, uint32_t index,
    const char **name, const Atom **left, const Atom **right) {
    if (!plan || index >= plan->rule_count)
        return false;
    if (name)
        *name = plan->rules[index].name;
    if (left)
        *left = plan->rules[index].left;
    if (right)
        *right = plan->rules[index].right;
    return true;
}

bool cetta_deterministic_equation_plan_v1_declares(
    const CettaDeterministicEquationPlanV1 *plan, const char *name,
    size_t arity) {
    if (!plan || !name)
        return false;
    for (size_t index = 0u; index < plan->operator_count; index++) {
        if (plan->operators[index].arity == arity &&
            strcmp(plan->operators[index].name, name) == 0)
            return true;
    }
    return false;
}

typedef struct { Atom **items; size_t len, capacity; } PreparedVariablesV1;

static bool prepared_variable(PreparedVariablesV1 *v, Atom *a) {
    if (v->len == v->capacity) {
        size_t cap = v->capacity ? v->capacity * 2u : 16u;
        if (cap < v->capacity || cap > SIZE_MAX / sizeof(*v->items)) return false;
        void *p = realloc(v->items, cap * sizeof(*v->items));
        if (!p) return false;
        v->items = p; v->capacity = cap;
    }
    v->items[v->len++] = a;
    return true;
}

enum { PREPARED_SYMBOL = 1, PREPARED_VARIABLE, PREPARED_STRING,
    PREPARED_INT, PREPARED_BIGINT, PREPARED_FLOAT, PREPARED_BOOL, PREPARED_EXPR };

static void prepared_write_atom(CettaPreparedWriterV1 *w,
    const CettaDeterministicEquationPlanV1 *plan, const Atom *a,
    PreparedVariablesV1 *variables, unsigned depth) {
    if (!a || depth >= 512u || w->failed) { w->failed = true; return; }
    if (a->kind == ATOM_SYMBOL || a->kind == ATOM_VAR) {
        cpw_u64(w, a->kind == ATOM_SYMBOL ? PREPARED_SYMBOL : PREPARED_VARIABLE);
        if (a->kind == ATOM_VAR) {
            size_t i = 0;
            for (; i < variables->len && !var_id_eq(variables->items[i]->var_id, a->var_id); ++i) {}
            if (i == variables->len && !prepared_variable(variables, (Atom *)a)) w->failed = true;
            cpw_u64(w, i);
        }
        const char *name = atom_name_cstr((Atom *)a);
        cpw_text(w, name, strlen(name));
    } else if (a->kind == ATOM_EXPR) {
        cpw_u64(w, PREPARED_EXPR); cpw_u64(w, a->expr.len);
        CettaDeterministicPrimitiveClassV1 klass = CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
        if (a->expr.len && a->expr.elems[0]->kind == ATOM_SYMBOL && plan->vocabulary.classify)
            klass = plan->vocabulary.classify(plan->vocabulary.context,
                atom_name_cstr(a->expr.elems[0]), (uint32_t)(a->expr.len - 1u));
        cpw_u64(w, klass);
        for (CettaExprIndex i = 0; i < a->expr.len; ++i)
            prepared_write_atom(w, plan, a->expr.elems[i], variables, depth + 1u);
    } else if (a->kind == ATOM_GROUNDED) {
        switch (a->ground.gkind) {
        case GV_STRING:
            cpw_u64(w, PREPARED_STRING); cpw_text(w, a->ground.sval, atom_string_len((Atom *)a)); break;
        case GV_INT:
            cpw_u64(w, PREPARED_INT); cpw_u64(w, (uint64_t)a->ground.ival); break;
        case GV_BIGINT: {
            const char *s = atom_bigint_cstr(a);
            cpw_u64(w, PREPARED_BIGINT); cpw_text(w, s, strlen(s)); break;
        }
        case GV_FLOAT: {
            uint64_t bits; _Static_assert(sizeof(double) == sizeof(bits), "binary64 required");
            memcpy(&bits, &a->ground.fval, sizeof(bits));
            cpw_u64(w, PREPARED_FLOAT); cpw_u64(w, bits); break;
        }
        case GV_BOOL: cpw_u64(w, PREPARED_BOOL); cpw_u64(w, a->ground.bval); break;
        default: w->failed = true;
        }
    } else w->failed = true;
}

static char *prepared_read_name(CettaPreparedReaderV1 *r, Arena *arena) {
    size_t n = 0; const char *s = cpr_text(r, &n);
    if (!s || memchr(s, 0, n)) { r->failed = true; return NULL; }
    char *out = arena_alloc(arena, n + 1u);
    if (!out) { r->failed = true; return NULL; }
    memcpy(out, s, n); out[n] = 0; return out;
}

static Atom *prepared_read_atom(CettaPreparedReaderV1 *r,
    CettaDeterministicEquationPlanV1 *plan, PreparedVariablesV1 *variables, unsigned depth) {
    if (depth >= 512u || r->failed) { r->failed = true; return NULL; }
    Arena *arena = &plan->source_arena;
    uint64_t tag = cpr_u64(r);
    if (tag == PREPARED_SYMBOL || tag == PREPARED_VARIABLE) {
        uint64_t id = tag == PREPARED_VARIABLE ? cpr_u64(r) : 0u;
        char *name = prepared_read_name(r, arena);
        if (!name) return NULL;
        if (tag == PREPARED_SYMBOL) return atom_symbol(arena, name);
        if (id > variables->len) { r->failed = true; return NULL; }
        if (id == variables->len && !prepared_variable(variables, atom_var(arena, name))) { r->failed = true; return NULL; }
        if (strcmp(atom_name_cstr(variables->items[id]), name)) { r->failed = true; return NULL; }
        return variables->items[id];
    }
    if (tag == PREPARED_STRING) {
        size_t n = 0; const char *s = cpr_text(r, &n);
        if (!s || n > UINT32_MAX) { r->failed = true; return NULL; }
        return atom_string_n(arena, s, n);
    }
    if (tag == PREPARED_BIGINT) {
        char *s = prepared_read_name(r, arena);
        const char *p = s;
        if (p && *p == '-') ++p;
        if (!p || !*p) { r->failed = true; return NULL; }
        for (; *p; ++p) if (*p < '0' || *p > '9') { r->failed = true; return NULL; }
        return atom_bigint(arena, s);
    }
    if (tag == PREPARED_INT || tag == PREPARED_FLOAT || tag == PREPARED_BOOL) {
        uint64_t n = cpr_u64(r);
        if (tag == PREPARED_INT) { int64_t i; memcpy(&i, &n, sizeof(i)); return atom_int(arena, i); }
        if (tag == PREPARED_FLOAT) { double d; memcpy(&d, &n, sizeof(d)); return atom_float(arena, d); }
        if (n > 1u) { r->failed = true; return NULL; }
        return atom_bool(arena, n != 0u);
    }
    if (tag != PREPARED_EXPR) { r->failed = true; return NULL; }
    uint64_t n = cpr_u64(r), klass = cpr_u64(r);
    if (r->failed || n > UINT32_MAX || n > (r->len - r->at) / 8u || n > SIZE_MAX / sizeof(Atom *) ||
        klass > CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_STRUCTURE) { r->failed = true; return NULL; }
    Atom **items = arena_alloc(arena, (n ? (size_t)n : 1u) * sizeof(*items));
    if (!items) { r->failed = true; return NULL; }
    for (size_t i = 0; i < n; ++i) {
        items[i] = prepared_read_atom(r, plan, variables, depth + 1u);
        if (!items[i]) return NULL;
    }
    CettaDeterministicPrimitiveClassV1 actual = CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
    if (n && items[0]->kind == ATOM_SYMBOL && plan->vocabulary.classify)
        actual = plan->vocabulary.classify(plan->vocabulary.context, atom_name_cstr(items[0]), (uint32_t)(n - 1u));
    if ((uint64_t)actual != klass) { r->failed = true; return NULL; }
    return atom_expr(arena, items, (CettaExprLen)n);
}

bool cetta_deterministic_equation_plan_v1_store(
    const CettaDeterministicEquationPlanV1 *plan, const char *abi,
    uint8_t **bytes, size_t *length, char *error, size_t error_size) {
    if (!plan || !abi || !*abi || !bytes || !length) return equation_error(error, error_size, "bad prepared equation export arguments");
    CettaPreparedWriterV1 w = {0};
    cpw_bytes(&w, "CEQ1", 4u); cpw_text(&w, abi, strlen(abi));
    cpw_u64(&w, plan->rule_count); cpw_u64(&w, plan->operator_count);
    for (uint32_t i = 0; i < plan->rule_count; ++i) {
        PreparedVariablesV1 variables = {0};
        const DeterministicEquationRuleV1 *rule = &plan->rules[i];
        cpw_text(&w, rule->name, strlen(rule->name));
        prepared_write_atom(&w, plan, rule->left, &variables, 0u);
        prepared_write_atom(&w, plan, rule->right, &variables, 0u);
        free(variables.items);
    }
    for (size_t i = 0; i < plan->operator_count; ++i) {
        cpw_text(&w, plan->operators[i].name, strlen(plan->operators[i].name));
        cpw_u64(&w, plan->operators[i].arity);
    }
    if (w.failed) { free(w.data); return equation_error(error, error_size, "cannot encode prepared equation plan"); }
    *bytes = w.data; *length = w.len; return true;
}

bool cetta_deterministic_equation_plan_v1_restore(
    const uint8_t *bytes, size_t length, const char *abi,
    const CettaDeterministicVocabularyV1 *vocabulary, bool audit,
    CettaDeterministicEquationPlanV1 **out, char *error, size_t error_size) {
    if (!bytes || !abi || !out) return equation_error(error, error_size, "bad prepared equation restore arguments");
    CettaPreparedReaderV1 r = {.data = bytes, .len = length};
    CettaDeterministicEquationPlanV1 *p = NULL;
    if (!cpr_literal(&r, "CEQ1", 4u) || !cpr_abi(&r, abi)) goto fail;
    uint64_t rules = cpr_u64(&r), operators = cpr_u64(&r);
    if (r.failed || !rules || rules > UINT32_MAX || rules > length / 24u ||
        rules > SIZE_MAX / sizeof(*p->rules) || operators > length / 16u ||
        operators > SIZE_MAX / sizeof(*p->operators)) goto fail;
    p = calloc(1u, sizeof(*p));
    if (!p) goto fail;
    arena_init(&p->source_arena);
    arena_set_runtime_kind(&p->source_arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    if (vocabulary) p->vocabulary = *vocabulary;
    p->rules = calloc((size_t)rules, sizeof(*p->rules));
    p->operators = calloc(operators ? (size_t)operators : 1u, sizeof(*p->operators));
    if (!p->rules || !p->operators) goto fail;
    for (uint32_t i = 0; i < rules; ++i) {
        PreparedVariablesV1 variables = {0};
        DeterministicEquationRuleV1 *rule = &p->rules[i];
        rule->name = prepared_read_name(&r, &p->source_arena);
        rule->left = prepared_read_atom(&r, p, &variables, 0u);
        rule->right = rule->left ? prepared_read_atom(&r, p, &variables, 0u) : NULL;
        free(variables.items);
        if (!rule->name || !rule->right || r.failed || !rule_left_view(rule->left, &rule->head, &rule->arity)) goto fail;
        if (i && equation_rule_compare(&p->rules[i - 1u], rule) > 0) goto fail;
        ++p->rule_count;
    }
    for (size_t i = 0; i < operators; ++i) {
        p->operators[i].name = prepared_read_name(&r, &p->source_arena);
        uint64_t arity = cpr_u64(&r);
        if (!p->operators[i].name || arity > UINT32_MAX || r.failed) goto fail;
        p->operators[i].arity = (size_t)arity; ++p->operator_count;
    }
    if (r.failed || r.at != r.len || !plan_index_heads(p)) goto fail;
    if (audit) {
        atomic_fetch_add(&prepared_audits, 1u);
        CettaDescentRuleV1 *descent = calloc(p->rule_count, sizeof(*descent));
        if (!descent) goto fail;
        bool ok = true, resource_failure = false;
        for (uint32_t i = 0; ok && i < p->rule_count; ++i) {
            DeterministicEquationRuleV1 *rule = &p->rules[i];
            ok = pattern_is_left_linear(rule->left) && right_variables_bound(rule->left, rule->right);
            for (uint32_t j = 0; ok && j < i; ++j) ok = !patterns_overlap(p->rules[j].left, rule->left);
            descent[i] = (CettaDescentRuleV1){rule->name, rule->left, rule->right};
        }
        if (ok) ok = cetta_deterministic_equation_descends_v1(descent, p->rule_count,
            &p->vocabulary, &resource_failure, error, error_size);
        free(descent);
        if (!ok) goto fail;
    }
    plan_compile_rules(p);
    atomic_fetch_add(&prepared_restores, 1u);
    *out = p; return true;
fail:
    cetta_deterministic_equation_plan_v1_free(p);
    return equation_error(error, error_size, "invalid prepared equation image or primitive ABI");
}
