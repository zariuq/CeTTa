#include "he_macro_expansion.h"

#include "eval.h"
#include "match.h"
#include "symbol.h"
#include "term_canon.h"
#include <stdlib.h>

enum { HE_MINIMAL_FOLD_MAX_DEPTH = 256u };

bool he_macro_signature(Space *space, Atom *call, HeMacroArgument *arguments) {
    if (!space || !call || call->kind != ATOM_EXPR || call->expr.len < 2u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL ||
        call->expr.len - 1u > HE_MACRO_MAX_ARITY ||
        !eval_current_language_id ||
        eval_current_language_id() != CETTA_LANGUAGE_HE)
        return false;
    CettaExprLen arity = call->expr.len - 1u;
    SymbolId head = atom_symbol_current_id(call->expr.elems[0]);
    /* The index answers first whether the head declares any type. */
    if (!space_head_declares_type(space, head))
        return false;
    Atom *head_atom = atom_symbol_canonical(head);
    if (!head_atom)
        return false;
    Arena scratch;
    arena_init_detached(&scratch);
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(
        space, &scratch, head_atom, &types);
    bool ok = count == 1u && types && types[0] &&
        types[0]->kind == ATOM_EXPR &&
        types[0]->expr.len == (CettaExprLen)(arity + 2u) &&
        atom_is_symbol_id(types[0]->expr.elems[0], g_builtin_syms.arrow);
    Atom *range = ok ? types[0]->expr.elems[arity + 1u] : NULL;
    ok = ok && (atom_is_symbol_id(range, g_builtin_syms.undefined_type) ||
                atom_is_symbol_id(range, g_builtin_syms.expression));
    bool any_written = false;
    for (CettaExprLen i = 0u; ok && i < arity; i++) {
        Atom *domain = types[0]->expr.elems[i + 1u];
        Atom *argument = call->expr.elems[i + 1u];
        if (atom_is_symbol_id(domain, g_builtin_syms.undefined_type)) {
            arguments[i] = HE_MACRO_ARGUMENT_EVALUATED;
        } else if (atom_is_symbol_id(domain, g_builtin_syms.atom)) {
            arguments[i] = HE_MACRO_ARGUMENT_WRITTEN;
        } else if (atom_is_symbol_id(domain, g_builtin_syms.expression) &&
                   (argument->kind == ATOM_EXPR ||
                    argument->kind == ATOM_VAR)) {
            /* Another argument there is HE's argument type error. */
            arguments[i] = argument->kind == ATOM_EXPR
                ? HE_MACRO_ARGUMENT_WRITTEN
                : HE_MACRO_ARGUMENT_EXPRESSION_VALUE;
        } else {
            ok = false;
            break;
        }
        any_written = any_written ||
            arguments[i] != HE_MACRO_ARGUMENT_EVALUATED;
    }
    free(types);
    arena_free(&scratch);
    return ok && any_written;
}

HeMacroExpandResult he_macro_expand(Space *space, Arena *arena, Atom *call,
                                    HeMacroExpansion *out) {
    if (!arena || !out ||
        !he_macro_signature(space, call, out->arguments))
        return HE_MACRO_NOT_A_MACRO;
    Atom *equation = space_single_linear_equation(
        space, atom_symbol_current_id(call->expr.elems[0]));
    if (!equation)
        return HE_MACRO_NOT_A_MACRO;
    CettaExprLen arity = call->expr.len - 1u;
    Atom *fresh = cetta_instantiate_frame_syntax(arena, equation);
    Atom *lhs = fresh && fresh->kind == ATOM_EXPR && fresh->expr.len == 3u &&
            atom_is_symbol_id(fresh->expr.elems[0], g_builtin_syms.equals)
        ? fresh->expr.elems[1] : NULL;
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len != (CettaExprLen)(arity + 1u))
        return HE_MACRO_FAILED;
    Bindings substitution;
    bindings_init(&substitution);
    bool ok = true;
    for (CettaExprLen i = 0u; ok && i < arity; i++) {
        Atom *parameter = lhs->expr.elems[i + 1u];
        ok = parameter->kind == ATOM_VAR &&
            (out->arguments[i] == HE_MACRO_ARGUMENT_EVALUATED ||
             bindings_add_var(
                 &substitution, parameter, call->expr.elems[i + 1u]));
    }
    Atom *body = ok
        ? bindings_apply_if_vars(&substitution, arena, fresh->expr.elems[2])
        : NULL;
    bindings_free(&substitution);
    if (!body)
        return HE_MACRO_FAILED;
    out->fresh = fresh;
    out->lhs = lhs;
    out->body = body;
    return HE_MACRO_EXPANDED;
}

static bool he_minimal_fold_has_var_id(
    const Atom *atom, VarId id, uint32_t depth) {
    if (atom->kind == ATOM_VAR)
        return atom->var_id == id;
    if (atom->kind != ATOM_EXPR || depth > HE_MINIMAL_FOLD_MAX_DEPTH)
        return atom->kind == ATOM_EXPR;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (he_minimal_fold_has_var_id(atom->expr.elems[i], id, depth + 1u))
            return true;
    }
    return false;
}

typedef struct {
    HeMinimalFoldMetavariable metavariable;
    const void *context;
    Arena *arena;
} HeMinimalFolder;

static bool he_minimal_fold_is_metavariable(
    const HeMinimalFolder *folder, const Atom *atom) {
    return atom->kind == ATOM_VAR &&
        folder->metavariable(folder->context, atom->var_id);
}

/* Atoms without variables are matched as HE's unify matches them. */
static HeMinimalFold he_minimal_fold_match(
    const HeMinimalFolder *folder, Atom *left, Atom *right,
    Bindings *bindings, uint32_t depth) {
    if (!left || !right || depth > HE_MINIMAL_FOLD_MAX_DEPTH)
        return HE_MINIMAL_FOLD_DYNAMIC;
    left = bindings_apply_if_vars(bindings, folder->arena, left);
    right = bindings_apply_if_vars(bindings, folder->arena, right);
    bool left_meta = he_minimal_fold_is_metavariable(folder, left);
    bool right_meta = he_minimal_fold_is_metavariable(folder, right);
    if (left_meta || right_meta) {
        if (left_meta && right_meta && left->var_id == right->var_id)
            return HE_MINIMAL_FOLD_STATIC;
        Atom *var = left_meta ? left : right;
        Atom *value = left_meta ? right : left;
        if (he_minimal_fold_has_var_id(value, var->var_id, 0u))
            return HE_MINIMAL_FOLD_DYNAMIC;
        return bindings_add_var(bindings, var, value)
            ? HE_MINIMAL_FOLD_STATIC : HE_MINIMAL_FOLD_DYNAMIC;
    }
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR)
        return left->kind == ATOM_VAR && right->kind == ATOM_VAR &&
                left->var_id == right->var_id
            ? HE_MINIMAL_FOLD_STATIC : HE_MINIMAL_FOLD_DYNAMIC;
    /* HE queries a space a pattern meets; a run decides what it holds. */
    if (left->kind == ATOM_GROUNDED || right->kind == ATOM_GROUNDED) {
        if ((left->kind == ATOM_GROUNDED &&
             left->ground.gkind == GV_SPACE) ||
            (right->kind == ATOM_GROUNDED &&
             right->ground.gkind == GV_SPACE))
            return HE_MINIMAL_FOLD_DYNAMIC;
    }
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len)
            return HE_MINIMAL_FOLD_MISMATCH;
        bool dynamic = false;
        for (CettaExprIndex i = 0u; i < left->expr.len; i++) {
            HeMinimalFold fold = he_minimal_fold_match(
                folder, left->expr.elems[i], right->expr.elems[i],
                bindings, depth + 1u);
            if (fold == HE_MINIMAL_FOLD_MISMATCH)
                return HE_MINIMAL_FOLD_MISMATCH;
            dynamic = dynamic || fold == HE_MINIMAL_FOLD_DYNAMIC;
        }
        return dynamic ? HE_MINIMAL_FOLD_DYNAMIC : HE_MINIMAL_FOLD_STATIC;
    }
    if (left->kind == ATOM_EXPR || right->kind == ATOM_EXPR)
        return HE_MINIMAL_FOLD_MISMATCH;
    Bindings ground;
    bindings_init(&ground);
    bool matched = match_atoms(left, right, &ground, folder->arena);
    bindings_free(&ground);
    return matched ? HE_MINIMAL_FOLD_STATIC : HE_MINIMAL_FOLD_MISMATCH;
}

static HeMinimalFold he_minimal_fold_at(
    const HeMinimalFolder *folder, Atom *instruction, Atom **result_out,
    uint32_t depth) {
    if (!instruction || depth > HE_MINIMAL_FOLD_MAX_DEPTH)
        return HE_MINIMAL_FOLD_DYNAMIC;
    Arena *arena = folder->arena;
    SymbolId head = atom_head_symbol_id(instruction);
    CettaExprLen nargs = instruction->kind == ATOM_EXPR &&
            instruction->expr.len > 0u
        ? instruction->expr.len - 1u : 0u;
    Atom *const *args = instruction->kind == ATOM_EXPR
        ? &instruction->expr.elems[1] : NULL;
    if (head == g_builtin_syms.chain && nargs == 3u &&
        args[1]->kind == ATOM_VAR) {
        Atom *nested = NULL;
        if (he_minimal_fold_at(folder, args[0], &nested, depth + 1u) !=
            HE_MINIMAL_FOLD_STATIC)
            return HE_MINIMAL_FOLD_DYNAMIC;
        Bindings result;
        bindings_init(&result);
        Atom *body = bindings_add_var(&result, args[1], nested)
            ? bindings_apply_if_vars(&result, arena, args[2]) : NULL;
        bindings_free(&result);
        return body
            ? he_minimal_fold_at(folder, body, result_out, depth + 1u)
            : HE_MINIMAL_FOLD_DYNAMIC;
    }
    if (head == g_builtin_syms.unify && nargs == 4u) {
        Bindings matched;
        bindings_init(&matched);
        HeMinimalFold fold = he_minimal_fold_match(
            folder, args[0], args[1], &matched, depth + 1u);
        Atom *answer = NULL;
        if (fold == HE_MINIMAL_FOLD_STATIC)
            answer = bindings_apply_if_vars(&matched, arena, args[2]);
        else if (fold == HE_MINIMAL_FOLD_MISMATCH)
            answer = args[3];
        bindings_free(&matched);
        if (fold == HE_MINIMAL_FOLD_DYNAMIC || !answer)
            return HE_MINIMAL_FOLD_DYNAMIC;
        *result_out = answer;
        return HE_MINIMAL_FOLD_STATIC;
    }
    if (head == g_builtin_syms.decons_atom && nargs == 1u) {
        Atom *input = args[0];
        if (input->kind == ATOM_VAR)
            return HE_MINIMAL_FOLD_DYNAMIC;
        *result_out = input->kind == ATOM_EXPR && input->expr.len > 0u
            ? atom_expr2(arena, input->expr.elems[0],
                         atom_expr_suffix(arena, input, 1u))
            : cetta_call_signature_error(
                  arena, instruction, "(decons-atom (: <expr> Expression))");
        return *result_out ? HE_MINIMAL_FOLD_STATIC : HE_MINIMAL_FOLD_DYNAMIC;
    }
    if (head == g_builtin_syms.cons_atom && nargs == 2u) {
        Atom *tail = args[1];
        if (tail->kind == ATOM_VAR)
            return HE_MINIMAL_FOLD_DYNAMIC;
        if (tail->kind != ATOM_EXPR) {
            *result_out = cetta_call_signature_error(
                arena, instruction,
                "(cons-atom <head> (: <tail> Expression))");
            return *result_out ? HE_MINIMAL_FOLD_STATIC
                               : HE_MINIMAL_FOLD_DYNAMIC;
        }
        Atom **elements = arena_alloc(
            arena, sizeof(*elements) * ((size_t)tail->expr.len + 1u));
        elements[0] = args[0];
        for (CettaExprIndex i = 0u; i < tail->expr.len; i++)
            elements[i + 1u] = tail->expr.elems[i];
        *result_out = atom_expr(arena, elements, tail->expr.len + 1u);
        return HE_MINIMAL_FOLD_STATIC;
    }
    if (head == g_builtin_syms.eval || head == g_builtin_syms.evalc ||
        head == g_builtin_syms.chain || head == g_builtin_syms.function ||
        head == g_builtin_syms.unify || head == g_builtin_syms.metta ||
        head == g_builtin_syms.context_space ||
        head == g_builtin_syms.call_native ||
        head == g_builtin_syms.cons_atom ||
        head == g_builtin_syms.decons_atom ||
        head == g_builtin_syms.collapse_bind ||
        head == g_builtin_syms.superpose_bind ||
        head == g_builtin_syms.return_text)
        return HE_MINIMAL_FOLD_DYNAMIC;
    *result_out = instruction;
    return HE_MINIMAL_FOLD_STATIC;
}

HeMinimalFold he_minimal_fold_unify(
    Atom *left, Atom *right, Bindings *bindings, Arena *arena,
    HeMinimalFoldMetavariable metavariable, const void *context) {
    if (!arena || !metavariable || !bindings)
        return HE_MINIMAL_FOLD_DYNAMIC;
    HeMinimalFolder folder = {
        .metavariable = metavariable,
        .context = context,
        .arena = arena,
    };
    return he_minimal_fold_match(&folder, left, right, bindings, 0u);
}

HeMinimalFold he_minimal_fold_instruction(
    Atom *instruction, Arena *arena, HeMinimalFoldMetavariable metavariable,
    const void *context, Atom **result_out) {
    if (!arena || !metavariable || !result_out)
        return HE_MINIMAL_FOLD_DYNAMIC;
    HeMinimalFolder folder = {
        .metavariable = metavariable,
        .context = context,
        .arena = arena,
    };
    return he_minimal_fold_at(&folder, instruction, result_out, 0u);
}

/* The metavariables of one expansion: its definition's variables, except the
 * parameters of arguments HE evaluates, which hold values. */
typedef struct {
    const HeMacroExpansion *expansion;
    CettaExprLen arity;
} HeMacroEffectFold;

static bool he_macro_var_occurs(const Atom *atom, VarId id, uint32_t depth) {
    if (atom->kind == ATOM_VAR)
        return atom->var_id == id;
    if (atom->kind != ATOM_EXPR || depth > HE_MINIMAL_FOLD_MAX_DEPTH)
        return false;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (he_macro_var_occurs(atom->expr.elems[i], id, depth + 1u))
            return true;
    }
    return false;
}

static bool he_macro_effect_metavariable(const void *context, VarId id) {
    const HeMacroEffectFold *fold = context;
    for (CettaExprLen i = 0u; i < fold->arity; i++) {
        const Atom *parameter = fold->expansion->lhs->expr.elems[i + 1u];
        if (fold->expansion->arguments[i] == HE_MACRO_ARGUMENT_EVALUATED &&
            parameter->var_id == id)
            return false;
    }
    return he_macro_var_occurs(fold->expansion->fresh, id, 0u);
}

bool he_macro_effect_expansion(Space *space, Arena *arena, Atom *call,
                               Atom **evaluated, uint32_t capacity,
                               uint32_t *evaluated_count, Atom **body_out) {
    if (!evaluated_count || !body_out)
        return false;
    *evaluated_count = 0u;
    *body_out = NULL;
    HeMacroExpansion expansion;
    if (he_macro_expand(space, arena, call, &expansion) != HE_MACRO_EXPANDED)
        return false;
    CettaExprLen arity = call->expr.len - 1u;
    /* Written arguments that are variables carry no structure: the call
     * reads as the definition's own body, which its own analysis covers. */
    bool structured = false;
    for (CettaExprLen i = 0u; i < arity; i++) {
        if (expansion.arguments[i] == HE_MACRO_ARGUMENT_WRITTEN &&
            call->expr.elems[i + 1u]->kind != ATOM_VAR)
            structured = true;
    }
    if (!structured)
        return false;
    for (CettaExprLen i = 0u; i < arity; i++) {
        if (expansion.arguments[i] != HE_MACRO_ARGUMENT_EVALUATED)
            continue;
        if (*evaluated_count >= capacity)
            return false;
        evaluated[(*evaluated_count)++] = call->expr.elems[i + 1u];
    }
    /* The body is evaluated: an instruction it answers runs in turn. */
    HeMacroEffectFold fold = {.expansion = &expansion, .arity = arity};
    Atom *body = expansion.body;
    for (uint32_t step = 0u; step < HE_MINIMAL_FOLD_MAX_DEPTH; step++) {
        SymbolId head = atom_head_symbol_id(body);
        Atom *answered = NULL;
        if ((head != g_builtin_syms.chain && head != g_builtin_syms.unify) ||
            he_minimal_fold_instruction(
                body, arena, he_macro_effect_metavariable, &fold,
                &answered) != HE_MINIMAL_FOLD_STATIC ||
            answered == body)
            break;
        body = answered;
    }
    *body_out = body;
    return true;
}
