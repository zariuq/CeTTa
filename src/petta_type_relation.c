#include "petta_type_policy.h"

#include "petta_semantics.h"
#include "space.h"
#include "symbol.h"

#include <stdlib.h>

/* Intrinsic PeTTa candidate order: literal/variable, application, list,
 * declaration; Undefined only when the required-type query has no candidate.
 * Values are inspected, never executed.  The explicit stack handles deeply
 * nested data independently of the evaluator stack. */

typedef struct {
    Atom **items;
    uint32_t length;
    uint32_t capacity;
} PeTTaTypeList;

typedef struct {
    Atom *atom;
    Atom *const *elements;
    CettaExprLen length;
    CettaExprIndex next;
    PeTTaTypeList *element_types;
} PeTTaTypeFrame;

static bool petta_type_list_push(PeTTaTypeList *list, Atom *type) {
    if (!list || !type)
        return false;
    if (list->length == list->capacity) {
        if (list->capacity > UINT32_MAX / 2u)
            return false;
        uint32_t capacity = list->capacity ? list->capacity * 2u : 2u;
        Atom **items = realloc(list->items, sizeof(*items) * capacity);
        if (!items)
            return false;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->length++] = type;
    return true;
}

/* A number's, a string's or a truth value's type, which no declaration
 * overrides; NULL for every other atom. */
static Atom *petta_type_of_literal(Arena *arena, const Atom *atom) {
    if (atom->kind == ATOM_GROUNDED) {
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_BIGINT:
        case GV_RATIONAL:
        case GV_FLOAT:
            return atom_symbol(arena, "Number");
        case GV_STRING:
            return atom_symbol(arena, "String");
        default:
            break;
        }
    }
    bool truth = false;
    return petta_semantics_truth_value(atom, &truth)
        ? atom_symbol(arena, "Bool") : NULL;
}

/* The elements of X when is_list(X) holds: an expression or a list value,
 * or open cells whose last tail is one.  A list pattern, a cell with an
 * open tail, a Prolog compound and every non-expression are not lists. */
static bool petta_type_list_elements(
    Arena *arena, Atom *atom, Atom *const **elements, CettaExprLen *length) {
    if (!petta_semantics_is_open_cons_value(atom))
        return atom_sequence_view(atom, elements, length);
    size_t cells = 0u;
    Atom *end = atom;
    while (petta_semantics_is_open_cons_value(end)) {
        cells++;
        end = end->expr.elems[2];
    }
    Atom *const *tail = NULL;
    CettaExprLen tail_length = 0u;
    if (!atom_sequence_view(end, &tail, &tail_length) ||
        (uint64_t)tail_length > (uint64_t)(SIZE_MAX / sizeof(Atom *)) - cells)
        return false;
    size_t total = cells + (size_t)tail_length;
    Atom **items = total
        ? arena_alloc(arena, sizeof(*items) * total) : NULL;
    if (total && !items)
        return false;
    size_t index = 0u;
    for (Atom *cell = atom; petta_semantics_is_open_cons_value(cell);
         cell = cell->expr.elems[2])
        items[index++] = cell->expr.elems[1];
    for (CettaExprIndex i = 0u; i < tail_length; i++)
        items[index++] = tail[i];
    *elements = items;
    *length = (CettaExprLen)total;
    return true;
}

static bool petta_type_push_declared(
    Space *space, Arena *arena, Atom *subject, PeTTaTypeList *out) {
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, arena, subject, &declared);
    bool ok = true;
    for (uint32_t i = 0u; ok && i < count; i++)
        ok = petta_type_list_push(out, declared[i]);
    free(declared);
    return ok;
}

static bool petta_type_answers_bound(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out);

/* get-type(Argument, Formal) under `bindings`, for each solution the rest
 * of the arguments, then the declaration's result type.  Recursion is by
 * argument position, bounded by the declaration's arity. */
static bool petta_type_check_arguments(
    Space *space, Arena *arena, Atom *const *arguments,
    Atom *declaration,
    CettaExprIndex index, Bindings *bindings, PeTTaTypeList *out) {
    CettaExprLen arity = declaration->expr.len - 2u;
    if (index == arity)
        return petta_type_list_push(
            out, bindings_apply_if_vars(
                     bindings, arena, declaration->expr.elems[arity + 1u]));
    Atom *formal = declaration->expr.elems[index + 1u];
    Atom *argument = arguments[index];
    if (argument->kind == ATOM_VAR)
        return petta_type_check_arguments(
            space, arena, arguments, declaration,
            index + 1u, bindings, out);

    PeTTaTypeList candidates = {0};
    Atom *resolved_formal = bindings_apply_if_vars(bindings, arena, formal);
    Atom *resolved_argument = bindings_apply_if_vars(bindings, arena, argument);
    if (!resolved_formal || !resolved_argument || !petta_type_answers_bound(
            space, arena, resolved_argument, resolved_formal, &candidates)) {
        free(candidates.items);
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0u; ok && i < candidates.length; i++) {
        Bindings trial;
        bindings_init(&trial);
        if (!bindings_clone(&trial, bindings))
            ok = false;
        else if (match_atoms(candidates.items[i], formal, &trial, arena)) {
            ok = petta_type_check_arguments(
                space, arena, arguments, declaration,
                index + 1u, &trial, out);
        }
        bindings_free(&trial);
    }
    free(candidates.items);
    return ok;
}

/* The answers of a list: its function types, else its elements' types,
 * then its declarations. */
static bool petta_type_list_answers(
    Space *space, Arena *arena, const PeTTaTypeFrame *frame,
    PeTTaTypeList *out) {
    Atom *head = frame->length > 0u ? frame->elements[0] : NULL;
    if (head && head->kind != ATOM_VAR) {
        Atom **declared = NULL;
        uint32_t count = space_get_declared_types(
            space, arena, head, &declared);
        bool ok = true;
        for (uint32_t i = 0u; ok && i < count; i++) {
            Atom *type = declared[i];
            if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
                type->expr.len - 2u != frame->length - 1u ||
                !atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
                continue;
            Bindings bindings;
            bindings_init(&bindings);
            ok = petta_type_check_arguments(
                space, arena, frame->elements + 1u, type, 0u, &bindings, out);
            bindings_free(&bindings);
        }
        free(declared);
        if (!ok)
            return false;
    }
    if (out->length == 0u) {
        /* maplist(get-type, X, T): every combination of the elements'
         * answers, the last element varying fastest. */
        CettaExprLen length = frame->length;
        uint64_t total = 1u;
        for (CettaExprIndex i = 0u; i < length; i++) {
            total *= frame->element_types[i].length;
            if (total > UINT32_MAX)
                return false;
        }
        uint32_t *indices = length ? calloc((size_t)length, sizeof(*indices))
                                   : NULL;
        Atom **row = length ? malloc(sizeof(*row) * (size_t)length) : NULL;
        if (length && (!indices || !row)) {
            free(indices);
            free(row);
            return false;
        }
        bool ok = true;
        for (uint64_t produced = 0u; ok && produced < total; produced++) {
            for (CettaExprIndex i = 0u; i < length; i++)
                row[i] = frame->element_types[i].items[indices[i]];
            ok = petta_type_list_push(out, atom_expr(arena, row, length));
            for (CettaExprIndex i = length; i > 0u; i--) {
                if (++indices[i - 1u] < frame->element_types[i - 1u].length)
                    break;
                indices[i - 1u] = 0u;
            }
        }
        free(indices);
        free(row);
        if (!ok)
            return false;
    }
    return petta_type_push_declared(space, arena, frame->atom, out);
}

/* The answers of a variable, a literal or an atom that is not a list. */
static bool petta_type_leaf_answers(
    Space *space, Arena *arena, Atom *atom, PeTTaTypeList *out) {
    if (atom->kind == ATOM_VAR)
        return petta_type_list_push(
            out, atom_var_with_id(arena, "__petta_type", fresh_var_id()));
    Atom *literal = petta_type_of_literal(arena, atom);
    if (literal)
        return petta_type_list_push(out, literal);
    return petta_type_push_declared(space, arena, atom, out);
}

static void petta_type_frame_free(PeTTaTypeFrame *frame) {
    if (frame->element_types) {
        for (CettaExprIndex i = 0u; i < frame->length; i++)
            free(frame->element_types[i].items);
        free(frame->element_types);
    }
}

static bool petta_type_frame_open(
    Arena *arena, Atom *atom, PeTTaTypeFrame *frame, bool *is_list) {
    *frame = (PeTTaTypeFrame){.atom = atom};
    *is_list = petta_type_list_elements(
        arena, atom, &frame->elements, &frame->length);
    if (!*is_list || frame->length == 0u)
        return true;
    if ((uint64_t)frame->length > (uint64_t)(SIZE_MAX / sizeof(PeTTaTypeList)))
        return false;
    frame->element_types = calloc(
        (size_t)frame->length, sizeof(*frame->element_types));
    return frame->element_types != NULL;
}

/* get-type(Subject, T) for a fresh T. */
static bool petta_type_answers_fresh(
    Space *space, Arena *arena, Atom *subject, PeTTaTypeList *out) {
    PeTTaTypeFrame *stack = NULL;
    size_t depth = 0u;
    size_t capacity = 0u;
    bool is_list = false;
    PeTTaTypeFrame root;
    if (!petta_type_frame_open(arena, subject, &root, &is_list))
        return false;
    if (!is_list) {
        bool ok = petta_type_leaf_answers(space, arena, subject, out);
        if (ok && out->length == 0u)
            ok = petta_type_list_push(out, atom_undefined_type(arena));
        return ok;
    }
    bool ok = true;
    stack = malloc(sizeof(*stack) * 8u);
    capacity = 8u;
    if (!stack) {
        petta_type_frame_free(&root);
        return false;
    }
    stack[depth++] = root;
    while (ok && depth > 0u) {
        PeTTaTypeFrame *frame = &stack[depth - 1u];
        if (frame->next < frame->length) {
            Atom *element = frame->elements[frame->next];
            PeTTaTypeFrame child;
            bool child_is_list = false;
            if (!petta_type_frame_open(arena, element, &child, &child_is_list)) {
                ok = false;
                break;
            }
            if (child_is_list) {
                if (depth == capacity) {
                    PeTTaTypeFrame *grown = capacity <= SIZE_MAX / (2u * sizeof(*stack))
                        ? realloc(stack, sizeof(*stack) * capacity * 2u) : NULL;
                    if (!grown) {
                        petta_type_frame_free(&child);
                        ok = false;
                        break;
                    }
                    stack = grown;
                    capacity *= 2u;
                }
                stack[depth++] = child;
                continue;
            }
            PeTTaTypeList *slot = &frame->element_types[frame->next];
            ok = petta_type_leaf_answers(space, arena, element, slot) &&
                 (slot->length > 0u ||
                  petta_type_list_push(slot, atom_undefined_type(arena)));
            frame->next++;
            continue;
        }
        PeTTaTypeList answers = {0};
        ok = petta_type_list_answers(space, arena, frame, &answers) &&
             (answers.length > 0u ||
              petta_type_list_push(&answers, atom_undefined_type(arena)));
        petta_type_frame_free(frame);
        depth--;
        if (!ok) {
            free(answers.items);
            break;
        }
        if (depth == 0u) {
            *out = answers;
            break;
        }
        PeTTaTypeFrame *parent = &stack[depth - 1u];
        parent->element_types[parent->next++] = answers;
    }
    while (depth > 0u)
        petta_type_frame_free(&stack[--depth]);
    free(stack);
    return ok;
}

/* Bound list queries unify the shape first, then query each element with
 * its actual requirement. Enumerating fresh types and filtering loses, for
 * example, get-type((a 6), (Undefined Undefined)). */
static bool petta_type_bound_elements(
    Space *space, Arena *arena, Atom *const *elements, CettaExprLen length,
    Atom *row, CettaExprIndex index, Bindings *bindings, PeTTaTypeList *out) {
    if (index == length)
        return petta_type_list_push(out, bindings_apply_if_vars(bindings, arena, row));
    Atom *subject = bindings_apply_if_vars(bindings, arena, elements[index]);
    Atom *formal = bindings_apply_if_vars(bindings, arena, row->expr.elems[index]);
    PeTTaTypeList answers = {0};
    if (!subject || !formal || !petta_type_answers_bound(
            space, arena, subject, formal, &answers)) {
        free(answers.items);
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0u; ok && i < answers.length; i++) {
        Bindings trial;
        bindings_init(&trial);
        if (!bindings_clone(&trial, bindings))
            ok = false;
        else if (match_atoms(answers.items[i], formal, &trial, arena))
            ok = petta_type_bound_elements(
                space, arena, elements, length, row, index + 1u, &trial, out);
        bindings_free(&trial);
    }
    free(answers.items);
    return ok;
}

static bool petta_type_append_matching(
    Arena *arena, const PeTTaTypeList *candidates, Atom *target, PeTTaTypeList *out) {
    for (uint32_t i = 0u; i < candidates->length; i++) {
        Atom *candidate = candidates->items[i];
        /* A root output variable acquires this candidate at the caller's
         * existing unification boundary; no temporary binding store is
         * needed here. Rigid symbol equality likewise learns no bindings.
         * Structured targets keep the full refining match below. */
        if (target->kind == ATOM_VAR ||
            (target->kind == ATOM_SYMBOL && candidate->kind == ATOM_SYMBOL &&
             target->sym_id == candidate->sym_id)) {
            if (!petta_type_list_push(out, candidate))
                return false;
            continue;
        }
        Bindings trial;
        bindings_init(&trial);
        bool matched = match_atoms(candidate, target, &trial, arena);
        bool ok = !matched || petta_type_list_push(
            out, bindings_apply_if_vars(&trial, arena, target));
        bindings_free(&trial);
        if (!ok)
            return false;
    }
    return true;
}

static bool petta_type_answers_bound(
    Space *space, Arena *arena, Atom *subject, Atom *target, PeTTaTypeList *out) {
    PeTTaTypeList candidates = {0};
    Atom *literal = petta_type_of_literal(arena, subject);
    bool primitive = subject->kind == ATOM_VAR || literal;
    bool ok = true;
    if (primitive) {
        ok = petta_type_leaf_answers(space, arena, subject, &candidates) &&
             petta_type_append_matching(arena, &candidates, target, out);
        free(candidates.items);
        candidates = (PeTTaTypeList){0};
        if (!ok || out->length > 0u)
            return ok;
    } else {
        Atom *const *elements = NULL;
        CettaExprLen length = 0u;
        if (petta_type_list_elements(arena, subject, &elements, &length)) {
            PeTTaTypeFrame frame = {.elements = elements, .length = length};
            /* An element-type tuple cannot unify with a rigid scalar.  In
             * that case neither its fields nor its negative function probe
             * are needed; function and declaration answers still matter. */
            bool tuple_possible = target->kind == ATOM_VAR ||
                                  target->kind == ATOM_EXPR;
            bool function_present = false;
            if (length > 0u && elements[0]->kind != ATOM_VAR) {
                Atom **declared = NULL;
                uint32_t count = space_get_declared_types(
                    space, arena, elements[0], &declared);
                for (uint32_t i = 0u; ok && i < count; i++) {
                    PettaTypeCall call;
                    if (!petta_type_call_open(declared[i], length - 1u, &call))
                        continue;
                    Bindings bindings;
                    bindings_init(&bindings);
                    if (match_atoms(call.result_type, target, &bindings, arena))
                        ok = petta_type_check_arguments(
                            space, arena, elements + 1u,
                            declared[i], 0u, &bindings, &candidates);
                    bindings_free(&bindings);
                }
                function_present = candidates.length > 0u;
                /* The list alternative tests absence of ANY function
                 * type, not absence of a function type matching target. */
                for (uint32_t i = 0u;
                     ok && tuple_possible && !function_present && i < count; i++) {
                    if (!petta_type_call_open(declared[i], length - 1u, NULL))
                        continue;
                    Bindings bindings;
                    bindings_init(&bindings);
                    PeTTaTypeList probe = {0};
                    ok = petta_type_check_arguments(
                        space, arena, elements + 1u,
                        declared[i], 0u, &bindings, &probe);
                    function_present = probe.length > 0u;
                    free(probe.items);
                    bindings_free(&bindings);
                }
                free(declared);
            }
            ok = ok && petta_type_append_matching(arena, &candidates, target, out);
            free(candidates.items);
            candidates = (PeTTaTypeList){0};
            if (ok && tuple_possible && !function_present) {
                Atom **vars = length
                    ? arena_alloc(arena, sizeof(*vars) * (size_t)length) : NULL;
                if (length && !vars)
                    return false;
                for (CettaExprIndex i = 0u; i < length; i++)
                    vars[i] = atom_var_with_id(arena, "__petta_type", fresh_var_id());
                Atom *row = atom_expr(arena, vars, length);
                Bindings bindings;
                bindings_init(&bindings);
                if (row && match_atoms(row, target, &bindings, arena))
                    ok = petta_type_bound_elements(
                        space, arena, frame.elements, length, row, 0u, &bindings, out);
                bindings_free(&bindings);
                if (!row)
                    ok = false;
            }
        }
    }
    if (ok)
        ok = petta_type_push_declared(space, arena, subject, &candidates) &&
             petta_type_append_matching(arena, &candidates, target, out);
    free(candidates.items);
    if (!ok || out->length > 0u)
        return ok;
    if (target->kind == ATOM_SYMBOL)
        return !atom_is_symbol_id(target, g_builtin_syms.undefined_type) ||
               petta_type_list_push(out, target);
    PeTTaTypeList fallback = {0};
    ok = petta_type_list_push(&fallback, atom_undefined_type(arena)) &&
         petta_type_append_matching(arena, &fallback, target, out);
    free(fallback.items);
    return ok;
}

bool petta_type_intrinsic_answers(
    Space *space, Arena *arena, Atom *subject, Atom *target,
    Atom ***types_out, uint32_t *count_out) {
    if (!types_out || !count_out)
        return false;
    *types_out = NULL;
    *count_out = 0u;
    if (!space || !arena || !subject)
        return false;
    uint64_t identity_exhaustions = cetta_frame_identity_exhaustions();
    PeTTaTypeList answers = {0};
    if (!(target ? petta_type_answers_bound(space, arena, subject, target, &answers)
                 : petta_type_answers_fresh(space, arena, subject, &answers)) ||
        cetta_frame_identity_exhaustions() != identity_exhaustions) {
        free(answers.items);
        return false;
    }
    *types_out = answers.items;
    *count_out = answers.length;
    return true;
}
