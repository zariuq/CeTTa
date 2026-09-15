#include "structural_tree_relabel_v1.h"

#include "src/gslt_horn_runtime.h"
#include "src/symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TREE_RELABEL_COPY = 0,
    TREE_RELABEL_RECURSE,
    TREE_RELABEL_LABEL
} TreeRelabelActionKind;

typedef struct {
    TreeRelabelActionKind kind;
    uint32_t source_index;
} TreeRelabelAction;

typedef struct {
    bool expression;
    SymbolId head;
} TreeRelabelNullary;

typedef struct {
    bool source_expression;
    SymbolId source_head;
    uint32_t source_arity;
    TreeRelabelNullary target;
    TreeRelabelAction *actions;
    uint32_t action_len;
} TreeRelabelRule;

typedef struct {
    TreeRelabelNullary source;
    TreeRelabelNullary target;
} TreeRelabelLabel;

struct CettaStructuralTreeRelabelV1 {
    TreeRelabelRule *rules;
    uint32_t rule_len;
    TreeRelabelLabel *labels;
    uint32_t label_len;
};

typedef struct {
    const CettaStructuralTreeRelabelV1 *plan;
    Arena *arena;
    uint32_t depth_limit;
    uint64_t remaining_work;
    CettaStructuralTreeRelabelV1Status status;
    char *error;
    size_t error_size;
} TreeRelabelContext;

static bool tree_relabel_error(char *buffer, size_t size,
                               const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool tree_relabel_fail(TreeRelabelContext *context,
                              CettaStructuralTreeRelabelV1Status status,
                              const char *message) {
    if (context) {
        context->status = status;
        (void)tree_relabel_error(
            context->error, context->error_size, "%s", message);
    }
    return false;
}

static bool tree_relabel_take_work(TreeRelabelContext *context,
                                   uint32_t depth) {
    if (!context)
        return false;
    if (depth > context->depth_limit)
        return tree_relabel_fail(
            context, CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT,
            "tree relabeling exceeded its depth limit");
    if (context->remaining_work == 0u)
        return tree_relabel_fail(
            context, CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT,
            "tree relabeling exhausted its work limit");
    context->remaining_work--;
    return true;
}

static bool atom_head_is(const Atom *atom, const char *head,
                         CettaExprLen arguments) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == arguments + 1u && atom->expr.elems &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool atom_head_id_is(const Atom *atom, SymbolId head,
                            CettaExprLen arguments) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == arguments + 1u && atom->expr.elems &&
        atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        atom->expr.elems[0]->sym_id == head;
}

static bool nullary_from_atom(const Atom *atom, TreeRelabelNullary *out) {
    if (!atom || !out)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        *out = (TreeRelabelNullary){
            .expression = false,
            .head = atom->sym_id,
        };
        return true;
    }
    if (atom->kind == ATOM_EXPR && atom->expr.len == 1u &&
        atom->expr.elems && atom->expr.elems[0] &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL) {
        *out = (TreeRelabelNullary){
            .expression = true,
            .head = atom->expr.elems[0]->sym_id,
        };
        return true;
    }
    return false;
}

static bool nullary_equal(TreeRelabelNullary left,
                          TreeRelabelNullary right) {
    return left.expression == right.expression && left.head == right.head;
}

static bool nullary_matches(TreeRelabelNullary key, const Atom *atom) {
    TreeRelabelNullary value;
    return nullary_from_atom(atom, &value) && nullary_equal(key, value);
}

static Atom *materialize_nullary(Arena *arena,
                                 TreeRelabelNullary value) {
    Atom *head;

    if (!arena)
        return NULL;
    head = atom_symbol_id(arena, value.head);
    if (!value.expression)
        return head;
    return atom_expr(arena, &head, 1u);
}

static int source_variable_index(const Atom *pattern, VarId variable) {
    CettaExprIndex index;

    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return -1;
    for (index = 1u; index < pattern->expr.len; index++) {
        const Atom *child = pattern->expr.elems[index];
        if (child && child->kind == ATOM_VAR &&
            child->var_id == variable)
            return (int)(index - 1u);
    }
    return -1;
}

static bool compile_output_action(
    const Atom *child, const Atom *source_pattern,
    SymbolId entry_operator, SymbolId label_operator,
    TreeRelabelAction *out, const char *rule_name,
    CettaStructuralTreeRelabelV1Status *status,
    char *error, size_t error_size) {
    const Atom *variable = NULL;
    TreeRelabelActionKind kind = TREE_RELABEL_COPY;
    int source_index;

    if (child && child->kind == ATOM_VAR) {
        variable = child;
    } else if (atom_head_id_is(child, entry_operator, 1u) &&
               child->expr.elems[1]->kind == ATOM_VAR) {
        variable = child->expr.elems[1];
        kind = TREE_RELABEL_RECURSE;
    } else if (atom_head_id_is(child, label_operator, 1u) &&
               child->expr.elems[1]->kind == ATOM_VAR) {
        variable = child->expr.elems[1];
        kind = TREE_RELABEL_LABEL;
    } else {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
        return tree_relabel_error(
            error, error_size,
            "%s: output child is not copy, recursive, or label action",
            rule_name ? rule_name : "tree relabel rule");
    }
    source_index = source_variable_index(source_pattern, variable->var_id);
    if (source_index < 0) {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
        return tree_relabel_error(
            error, error_size,
            "%s: output action references a non-source variable",
            rule_name ? rule_name : "tree relabel rule");
    }
    *out = (TreeRelabelAction){
        .kind = kind,
        .source_index = (uint32_t)source_index,
    };
    return true;
}

static bool append_label(CettaStructuralTreeRelabelV1 *plan,
                         TreeRelabelNullary source,
                         TreeRelabelNullary target,
                         CettaStructuralTreeRelabelV1Status *status,
                         char *error, size_t error_size) {
    uint32_t index;
    TreeRelabelLabel *grown;

    for (index = 0u; index < plan->label_len; index++) {
        if (nullary_equal(plan->labels[index].source, source)) {
            if (status)
                *status = CETTA_TREE_RELABEL_V1_AMBIGUOUS_RULE;
            return tree_relabel_error(
                error, error_size,
                "tree relabel presentation has a duplicate label source");
        }
    }
    if (plan->label_len == UINT32_MAX ||
        (size_t)(plan->label_len + 1u) >
            SIZE_MAX / sizeof(*plan->labels)) {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
        return tree_relabel_error(
            error, error_size, "tree relabel label table is too large");
    }
    grown = realloc(
        plan->labels, (size_t)(plan->label_len + 1u) * sizeof(*grown));
    if (!grown) {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
        return tree_relabel_error(
            error, error_size, "cannot allocate tree relabel label table");
    }
    plan->labels = grown;
    plan->labels[plan->label_len++] = (TreeRelabelLabel){source, target};
    return true;
}

static bool append_rule(
    CettaStructuralTreeRelabelV1 *plan, const Atom *source_pattern,
    const Atom *target_pattern, SymbolId entry_operator,
    SymbolId label_operator, const char *rule_name,
    CettaStructuralTreeRelabelV1Status *status,
    char *error, size_t error_size) {
    TreeRelabelRule candidate;
    uint32_t index;
    TreeRelabelRule *grown;

    memset(&candidate, 0, sizeof(candidate));
    if (source_pattern->kind == ATOM_SYMBOL) {
        candidate.source_expression = false;
        candidate.source_head = source_pattern->sym_id;
        candidate.source_arity = 0u;
    } else if (source_pattern->kind == ATOM_EXPR &&
               source_pattern->expr.len >= 1u &&
               source_pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        CettaExprIndex left;
        candidate.source_expression = true;
        candidate.source_head = source_pattern->expr.elems[0]->sym_id;
        candidate.source_arity =
            (uint32_t)(source_pattern->expr.len - 1u);
        for (left = 1u; left < source_pattern->expr.len; left++) {
            CettaExprIndex right;
            const Atom *variable = source_pattern->expr.elems[left];
            if (!variable || variable->kind != ATOM_VAR) {
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
                return tree_relabel_error(
                    error, error_size,
                    "%s: source constructor arguments are not variables",
                    rule_name ? rule_name : "tree relabel rule");
            }
            for (right = 1u; right < left; right++) {
                if (source_pattern->expr.elems[right]->var_id ==
                    variable->var_id) {
                    if (status)
                        *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
                    return tree_relabel_error(
                        error, error_size,
                        "%s: source constructor is not left-linear",
                        rule_name ? rule_name : "tree relabel rule");
                }
            }
        }
    } else {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
        return tree_relabel_error(
            error, error_size,
            "%s: source is not a symbol-headed constructor",
            rule_name ? rule_name : "tree relabel rule");
    }
    if (target_pattern->kind == ATOM_SYMBOL) {
        candidate.target = (TreeRelabelNullary){
            .expression = false,
            .head = target_pattern->sym_id,
        };
    } else if (target_pattern->kind == ATOM_EXPR &&
               target_pattern->expr.len >= 1u &&
               target_pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        CettaExprIndex child;
        candidate.target = (TreeRelabelNullary){
            .expression = true,
            .head = target_pattern->expr.elems[0]->sym_id,
        };
        candidate.action_len =
            (uint32_t)(target_pattern->expr.len - 1u);
        if (candidate.action_len > 0u) {
            candidate.actions = calloc(
                candidate.action_len, sizeof(*candidate.actions));
            if (!candidate.actions) {
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
                return tree_relabel_error(
                    error, error_size,
                    "cannot allocate tree relabel output actions");
            }
        }
        for (child = 1u; child < target_pattern->expr.len; child++) {
            if (!compile_output_action(
                    target_pattern->expr.elems[child], source_pattern,
                    entry_operator, label_operator,
                    &candidate.actions[child - 1u], rule_name,
                    status, error, error_size)) {
                free(candidate.actions);
                return false;
            }
        }
    } else {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
        return tree_relabel_error(
            error, error_size,
            "%s: target is not a symbol-headed constructor",
            rule_name ? rule_name : "tree relabel rule");
    }
    for (index = 0u; index < plan->rule_len; index++) {
        const TreeRelabelRule *prior = &plan->rules[index];
        if (prior->source_expression == candidate.source_expression &&
            prior->source_head == candidate.source_head &&
            prior->source_arity == candidate.source_arity) {
            free(candidate.actions);
            if (status)
                *status = CETTA_TREE_RELABEL_V1_AMBIGUOUS_RULE;
            return tree_relabel_error(
                error, error_size,
                "tree relabel presentation has overlapping source constructors");
        }
    }
    if (plan->rule_len == UINT32_MAX ||
        (size_t)(plan->rule_len + 1u) > SIZE_MAX / sizeof(*plan->rules)) {
        free(candidate.actions);
        if (status)
            *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
        return tree_relabel_error(
            error, error_size, "tree relabel rule table is too large");
    }
    grown = realloc(
        plan->rules, (size_t)(plan->rule_len + 1u) * sizeof(*grown));
    if (!grown) {
        free(candidate.actions);
        if (status)
            *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
        return tree_relabel_error(
            error, error_size, "cannot allocate tree relabel rule table");
    }
    plan->rules = grown;
    plan->rules[plan->rule_len++] = candidate;
    return true;
}

bool cetta_structural_tree_relabel_v1_load_paths(
    const char *const *presentation_paths, size_t presentation_path_count,
    const char *entry_operator,
    const char *label_operator, CettaStructuralTreeRelabelV1 **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error_buf, size_t error_buf_size) {
    CettaGsltHornProgram *program = NULL;
    CettaStructuralTreeRelabelV1 *plan = NULL;
    SymbolId entry_id;
    SymbolId label_id;
    size_t index;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (status)
        *status = CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (!presentation_paths || presentation_path_count == 0u ||
        !entry_operator || !label_operator || !out)
        return tree_relabel_error(
            error_buf, error_buf_size,
            "invalid structural tree relabel plan request");
    if (!cetta_gslt_horn_program_load_paths(
            presentation_paths, presentation_path_count, &program,
            error_buf, error_buf_size)) {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
        return false;
    }
    plan = calloc(1u, sizeof(*plan));
    if (!plan) {
        cetta_gslt_horn_program_free(program);
        if (status)
            *status = CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT;
        return tree_relabel_error(
            error_buf, error_buf_size,
            "cannot allocate structural tree relabel plan");
    }
    entry_id = symbol_intern_cstr(g_symbols, entry_operator);
    label_id = symbol_intern_cstr(g_symbols, label_operator);
    for (index = 0u;
         index < cetta_gslt_horn_program_rule_count(program); index++) {
        CettaGsltHornRuleViewV1 view;
        const Atom *left;
        const Atom *right;

        if (!cetta_gslt_horn_program_rule_view_v1(
                program, index, &view) ||
            !atom_head_is(view.head, "metta-equation", 2u))
            continue;
        left = view.head->expr.elems[1];
        right = view.head->expr.elems[2];
        if (view.body_count != 0u &&
            ((atom_head_id_is(left, entry_id, 1u)) ||
             (atom_head_id_is(left, label_id, 1u)))) {
            if (status)
                *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
            (void)tree_relabel_error(
                error_buf, error_buf_size,
                "%s: selected equation has premises", view.name);
            goto fail;
        }
        if (atom_head_id_is(left, entry_id, 1u)) {
            if (!append_rule(
                    plan, left->expr.elems[1], right,
                    entry_id, label_id, view.name,
                    status, error_buf, error_buf_size))
                goto fail;
        } else if (atom_head_id_is(left, label_id, 1u)) {
            TreeRelabelNullary source;
            TreeRelabelNullary target;
            if (!nullary_from_atom(left->expr.elems[1], &source) ||
                !nullary_from_atom(right, &target)) {
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE;
                (void)tree_relabel_error(
                    error_buf, error_buf_size,
                    "%s: label equation is not nullary-to-nullary",
                    view.name);
                goto fail;
            }
            if (!append_label(
                    plan, source, target, status,
                    error_buf, error_buf_size))
                goto fail;
        }
    }
    cetta_gslt_horn_program_free(program);
    if (plan->rule_len == 0u) {
        if (status)
            *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
        (void)tree_relabel_error(
            error_buf, error_buf_size,
            "presentation contains no equations for the requested entry operator");
        goto fail_without_program;
    }
    for (index = 0u; index < plan->rule_len; index++) {
        uint32_t action;
        for (action = 0u; action < plan->rules[index].action_len; action++) {
            if (plan->rules[index].actions[action].kind ==
                    TREE_RELABEL_LABEL && plan->label_len == 0u) {
                if (status)
                    *status = CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION;
                (void)tree_relabel_error(
                    error_buf, error_buf_size,
                    "tree relabel entry uses a label action without label equations");
                goto fail_without_program;
            }
        }
    }
    if (status)
        *status = CETTA_TREE_RELABEL_V1_OK;
    *out = plan;
    return true;

fail:
    cetta_gslt_horn_program_free(program);
fail_without_program:
    cetta_structural_tree_relabel_v1_free(plan);
    return false;
}

bool cetta_structural_tree_relabel_v1_load(
    const char *presentation_path, const char *entry_operator,
    const char *label_operator, CettaStructuralTreeRelabelV1 **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error_buf, size_t error_buf_size) {
    const char *paths[1];

    if (!presentation_path) {
        if (error_buf && error_buf_size)
            error_buf[0] = '\0';
        if (status)
            *status = CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
        if (out)
            *out = NULL;
        return tree_relabel_error(
            error_buf, error_buf_size,
            "invalid structural tree relabel plan request");
    }
    paths[0] = presentation_path;
    return cetta_structural_tree_relabel_v1_load_paths(
        paths, 1u, entry_operator, label_operator, out,
        status, error_buf, error_buf_size);
}

void cetta_structural_tree_relabel_v1_free(
    CettaStructuralTreeRelabelV1 *plan) {
    uint32_t index;
    if (!plan)
        return;
    for (index = 0u; index < plan->rule_len; index++)
        free(plan->rules[index].actions);
    free(plan->rules);
    free(plan->labels);
    free(plan);
}

static const TreeRelabelRule *find_rule(
    const CettaStructuralTreeRelabelV1 *plan, const Atom *source) {
    bool expression;
    SymbolId head;
    uint32_t arity;
    uint32_t index;

    if (!plan || !source)
        return NULL;
    if (source->kind == ATOM_SYMBOL) {
        expression = false;
        head = source->sym_id;
        arity = 0u;
    } else if (source->kind == ATOM_EXPR && source->expr.len >= 1u &&
               source->expr.elems[0]->kind == ATOM_SYMBOL) {
        expression = true;
        head = source->expr.elems[0]->sym_id;
        arity = (uint32_t)(source->expr.len - 1u);
    } else {
        return NULL;
    }
    for (index = 0u; index < plan->rule_len; index++) {
        const TreeRelabelRule *rule = &plan->rules[index];
        if (rule->source_expression == expression &&
            rule->source_head == head && rule->source_arity == arity)
            return rule;
    }
    return NULL;
}

static Atom *apply_label(TreeRelabelContext *context,
                         const Atom *source) {
    uint32_t index;

    for (index = 0u; index < context->plan->label_len; index++) {
        if (nullary_matches(context->plan->labels[index].source, source))
            return materialize_nullary(
                context->arena, context->plan->labels[index].target);
    }
    (void)tree_relabel_fail(
        context, CETTA_TREE_RELABEL_V1_UNKNOWN_LABEL,
        "tree relabel source label has no authored mapping");
    return NULL;
}

static Atom *apply_at(TreeRelabelContext *context,
                      const Atom *source, uint32_t depth) {
    const TreeRelabelRule *rule;
    Atom **elements = NULL;
    Atom *result;
    uint32_t index;

    if (!tree_relabel_take_work(context, depth))
        return NULL;
    if (!source || source->kind == ATOM_VAR) {
        (void)tree_relabel_fail(
            context, CETTA_TREE_RELABEL_V1_NON_GROUND_TERM,
            "tree relabel input is not ground");
        return NULL;
    }
    rule = find_rule(context->plan, source);
    if (!rule) {
        (void)tree_relabel_fail(
            context, CETTA_TREE_RELABEL_V1_UNKNOWN_CONSTRUCTOR,
            "tree relabel source constructor has no authored equation");
        return NULL;
    }
    if (!rule->target.expression)
        return atom_symbol_id(context->arena, rule->target.head);
    elements = calloc((size_t)rule->action_len + 1u, sizeof(*elements));
    if (!elements) {
        (void)tree_relabel_fail(
            context, CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT,
            "cannot allocate tree relabel result frame");
        return NULL;
    }
    elements[0] = atom_symbol_id(context->arena, rule->target.head);
    for (index = 0u; index < rule->action_len; index++) {
        const TreeRelabelAction *action = &rule->actions[index];
        const Atom *child;
        if (!rule->source_expression ||
            action->source_index >= rule->source_arity) {
            (void)tree_relabel_fail(
                context, CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION,
                "compiled tree relabel action escaped its source constructor");
            free(elements);
            return NULL;
        }
        child = source->expr.elems[action->source_index + 1u];
        if (action->kind == TREE_RELABEL_COPY)
            elements[index + 1u] =
                atom_deep_copy_shared(context->arena, (Atom *)child);
        else if (action->kind == TREE_RELABEL_RECURSE)
            elements[index + 1u] = apply_at(context, child, depth + 1u);
        else
            elements[index + 1u] = apply_label(context, child);
        if (!elements[index + 1u]) {
            free(elements);
            return NULL;
        }
    }
    result = atom_expr(
        context->arena, elements, (CettaExprLen)rule->action_len + 1u);
    free(elements);
    return result;
}

bool cetta_structural_tree_relabel_v1_apply(
    const CettaStructuralTreeRelabelV1 *plan, const Atom *source,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaStructuralTreeRelabelV1Status *status,
    char *error_buf, size_t error_buf_size) {
    TreeRelabelContext context;
    ArenaMark mark;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (status)
        *status = CETTA_TREE_RELABEL_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (!plan || !source || !arena || !out || depth_limit == 0u ||
        work_limit == 0u)
        return tree_relabel_error(
            error_buf, error_buf_size,
            "invalid structural tree relabel application");
    mark = arena_mark(arena);
    context = (TreeRelabelContext){
        .plan = plan,
        .arena = arena,
        .depth_limit = depth_limit,
        .remaining_work = work_limit,
        .status = CETTA_TREE_RELABEL_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    *out = apply_at(&context, source, 0u);
    if (!*out) {
        arena_reset(arena, mark);
        if (status)
            *status = context.status;
        return false;
    }
    if (status)
        *status = CETTA_TREE_RELABEL_V1_OK;
    return true;
}

const char *cetta_structural_tree_relabel_v1_status_name(
    CettaStructuralTreeRelabelV1Status status) {
    switch (status) {
    case CETTA_TREE_RELABEL_V1_OK:
        return "ok";
    case CETTA_TREE_RELABEL_V1_BAD_ARGUMENT:
        return "bad_argument";
    case CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION:
        return "invalid_presentation";
    case CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE:
        return "unsupported_rule";
    case CETTA_TREE_RELABEL_V1_AMBIGUOUS_RULE:
        return "ambiguous_rule";
    case CETTA_TREE_RELABEL_V1_UNKNOWN_CONSTRUCTOR:
        return "unknown_constructor";
    case CETTA_TREE_RELABEL_V1_UNKNOWN_LABEL:
        return "unknown_label";
    case CETTA_TREE_RELABEL_V1_NON_GROUND_TERM:
        return "non_ground_term";
    case CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT:
        return "resource_limit";
    default:
        return "unknown_status";
    }
}
