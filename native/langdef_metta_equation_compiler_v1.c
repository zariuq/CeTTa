#include "langdef_metta_equation_compiler_v1.h"

#include "atom.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TARGET_DEPTH_LIMIT 4096u

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} TargetBuffer;

typedef struct {
    const char **items;
    size_t len;
    size_t cap;
} TargetVariableStack;

typedef struct {
    Atom *left;
    const char *name;
} TargetEquationPattern;

static bool target_error(char *error, size_t error_size,
                         const char *format, ...) {
    if (error != NULL && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool target_reserve(TargetBuffer *buffer, size_t extra) {
    size_t required;
    size_t next;
    uint8_t *grown;
    if (extra > SIZE_MAX - buffer->len)
        return false;
    required = buffer->len + extra;
    if (required <= buffer->cap)
        return true;
    next = buffer->cap == 0u ? 256u : buffer->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    grown = realloc(buffer->bytes, next);
    if (grown == NULL)
        return false;
    buffer->bytes = grown;
    buffer->cap = next;
    return true;
}

static bool target_append(TargetBuffer *buffer,
                          const void *bytes, size_t len) {
    if (!target_reserve(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool target_literal(TargetBuffer *buffer, const char *text) {
    return target_append(buffer, text, strlen(text));
}

static bool target_byte(TargetBuffer *buffer, uint8_t byte) {
    return target_append(buffer, &byte, 1u);
}

static bool target_head(Atom *atom, const char *name, CettaExprLen arity) {
    return atom != NULL && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static Atom *target_field(Atom *presentation, const char *name) {
    if (presentation == NULL || presentation->kind != ATOM_EXPR)
        return NULL;
    for (CettaExprIndex index = 2u;
         index < presentation->expr.len; index++) {
        Atom *field = presentation->expr.elems[index];
        if (field != NULL && field->kind == ATOM_EXPR &&
            field->expr.len > 0u &&
            atom_is_symbol(field->expr.elems[0], name))
            return field;
    }
    return NULL;
}

static const char *target_variable_name(const Atom *atom) {
    const char *name;
    if (atom == NULL || atom->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)atom);
    return name[0] == '?' && name[1] != '\0' ? name + 1u : NULL;
}

static bool target_variable_contains(
    const TargetVariableStack *variables, const char *name) {
    if (variables == NULL || name == NULL)
        return false;
    for (size_t index = variables->len; index > 0u; index--) {
        if (strcmp(variables->items[index - 1u], name) == 0)
            return true;
    }
    return false;
}

static bool target_variable_push(
    TargetVariableStack *variables, const char *name) {
    const char **grown;
    size_t next;
    if (variables == NULL || name == NULL)
        return false;
    if (variables->len == variables->cap) {
        next = variables->cap == 0u ? 8u : variables->cap * 2u;
        if (next < variables->cap ||
            next > SIZE_MAX / sizeof(*variables->items))
            return false;
        grown = realloc(variables->items, next * sizeof(*grown));
        if (grown == NULL)
            return false;
        variables->items = grown;
        variables->cap = next;
    }
    variables->items[variables->len++] = name;
    return true;
}

static bool target_collect_left_variables(
    const Atom *term, TargetVariableStack *variables) {
    const char *name;
    if (term == NULL || variables == NULL)
        return false;
    name = target_variable_name(term);
    if (name != NULL) {
        if (target_variable_contains(variables, name))
            return false;
        return target_variable_push(variables, name);
    }
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!target_collect_left_variables(
                term->expr.elems[index], variables))
            return false;
    }
    return true;
}

/* Left-linear patterns are alpha-equivalent precisely when variables occupy
 * the same positions and every non-variable node agrees. */
static bool target_pattern_alpha_eq(
    const Atom *left, const Atom *right) {
    const char *left_variable = target_variable_name(left);
    const char *right_variable = target_variable_name(right);
    if (left_variable != NULL || right_variable != NULL)
        return left_variable != NULL && right_variable != NULL;
    if (left == NULL || right == NULL || left->kind != right->kind)
        return false;
    if (left->kind != ATOM_EXPR)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!target_pattern_alpha_eq(
                left->expr.elems[index], right->expr.elems[index]))
            return false;
    }
    return true;
}

/* Because each side is left-linear, a variable is a one-use wildcard.  Two
 * patterns overlap exactly when their non-variable structure is compatible. */
static bool target_patterns_overlap(
    const Atom *left, const Atom *right) {
    if (target_variable_name(left) != NULL ||
        target_variable_name(right) != NULL)
        return true;
    if (left == NULL || right == NULL || left->kind != right->kind)
        return false;
    if (left->kind != ATOM_EXPR)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!target_patterns_overlap(
                left->expr.elems[index], right->expr.elems[index]))
            return false;
    }
    return true;
}

static bool target_right_variables_bound(
    const Atom *term, TargetVariableStack *variables) {
    const char *name;
    if (term == NULL || variables == NULL)
        return false;
    name = target_variable_name(term);
    if (name != NULL)
        return target_variable_contains(variables, name);
    if (term->kind != ATOM_EXPR)
        return true;
    if (target_head((Atom *)term, "let", 3u)) {
        const char *binder = target_variable_name(term->expr.elems[1]);
        size_t saved_len = variables->len;
        bool ok;
        if (binder == NULL ||
            !target_right_variables_bound(term->expr.elems[2], variables) ||
            !target_variable_push(variables, binder))
            return false;
        ok = target_right_variables_bound(term->expr.elems[3], variables);
        variables->len = saved_len;
        return ok;
    }
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!target_right_variables_bound(
                term->expr.elems[index], variables))
            return false;
    }
    return true;
}

static bool target_render(TargetBuffer *buffer, Atom *atom,
                          Arena *scratch, size_t depth) {
    const char *name;
    char *printed;
    if (atom == NULL || depth > TARGET_DEPTH_LIMIT)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        name = atom_name_cstr(atom);
        if (name[0] == '?' && name[1] != '\0')
            return target_byte(buffer, (uint8_t)'$') &&
                   target_literal(buffer, name + 1u);
        return target_literal(buffer, name);
    }
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind != GV_INT &&
            atom->ground.gkind != GV_BOOL &&
            atom->ground.gkind != GV_STRING)
            return false;
        printed = atom_to_parseable_string(scratch, atom);
        return printed != NULL && target_literal(buffer, printed);
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return false;
    if (target_head(atom, "metta-nullary", 1u)) {
        Atom *target = atom->expr.elems[1];
        if (target->kind != ATOM_SYMBOL)
            return false;
        return target_byte(buffer, (uint8_t)'(') &&
               target_render(buffer, target, scratch, depth + 1u) &&
               target_byte(buffer, (uint8_t)')');
    }
    if (!target_byte(buffer, (uint8_t)'('))
        return false;
    for (CettaExprIndex index = 0u; index < atom->expr.len; index++) {
        if (index > 0u && !target_byte(buffer, (uint8_t)' '))
            return false;
        if (!target_render(buffer, atom->expr.elems[index],
                           scratch, depth + 1u))
            return false;
    }
    return target_byte(buffer, (uint8_t)')');
}

bool cetta_langdef_metta_equation_composition_v1(
    const uint8_t *const *canonical_presentations,
    const size_t *canonical_lens,
    size_t presentation_count,
    uint8_t **program_out,
    size_t *program_len_out,
    char *error,
    size_t error_size) {
    char **texts = NULL;
    Arena arena;
    Atom **presentations = NULL;
    TargetEquationPattern *patterns = NULL;
    size_t pattern_len = 0u;
    size_t pattern_cap = 0u;
    TargetBuffer program = {0};
    bool ok = false;

    if (canonical_presentations == NULL || canonical_lens == NULL ||
        presentation_count == 0u || program_out == NULL ||
        program_len_out == NULL)
        return target_error(error, error_size,
                            "invalid equation compiler request");
    *program_out = NULL;
    *program_len_out = 0u;
    texts = calloc(presentation_count, sizeof(*texts));
    presentations = calloc(presentation_count, sizeof(*presentations));
    if (texts == NULL || presentations == NULL) {
        free(presentations);
        free(texts);
        return target_error(error, error_size,
                            "out of memory reading canonical presentation");
    }
    arena_init(&arena);
    for (size_t presentation_index = 0u;
         presentation_index < presentation_count; presentation_index++) {
        Atom **forms = NULL;
        int form_count;
        size_t canonical_len = canonical_lens[presentation_index];
        if (canonical_presentations[presentation_index] == NULL ||
            canonical_len == SIZE_MAX) {
            target_error(error, error_size,
                         "invalid canonical equation presentation");
            goto done;
        }
        texts[presentation_index] = malloc(canonical_len + 1u);
        if (texts[presentation_index] == NULL) {
            target_error(error, error_size,
                         "out of memory reading canonical presentation");
            goto done;
        }
        memcpy(texts[presentation_index],
               canonical_presentations[presentation_index], canonical_len);
        texts[presentation_index][canonical_len] = '\0';
        form_count = parse_metta_text(
            texts[presentation_index], &arena, &forms);
        if (form_count != 1 || forms == NULL || forms[0] == NULL ||
            forms[0]->kind != ATOM_EXPR || forms[0]->expr.len < 3u ||
            !atom_is_symbol(forms[0]->expr.elems[0],
                            "gslt-presentation-v1") ||
            target_field(forms[0], "rewrites") == NULL) {
            target_error(error, error_size,
                         "canonical equation presentation is malformed");
            goto done;
        }
        presentations[presentation_index] = forms[0];
    }
    for (size_t presentation_index = 0u;
         presentation_index < presentation_count; presentation_index++) {
        Atom *rewrites = target_field(
            presentations[presentation_index], "rewrites");
        for (CettaExprIndex index = 1u;
             index < rewrites->expr.len; index++) {
            Atom *rule = rewrites->expr.elems[index];
            Atom *head;
            Atom *body;
            Atom *equation;
            Atom *left;
            Atom *right;
            const char *rule_name;
            TargetVariableStack variables = {0};
            if (!target_head(rule, "rule", 3u) ||
                rule->expr.elems[1] == NULL ||
                rule->expr.elems[1]->kind != ATOM_SYMBOL ||
                !(head = rule->expr.elems[2]) ||
                !(body = rule->expr.elems[3]) ||
                !target_head(head, "head", 1u) ||
                !target_head(body, "body", 0u) ||
                !(equation = head->expr.elems[1]) ||
                !target_head(equation, "metta-equation", 2u)) {
                target_error(error, error_size,
                             "equation rules must be unconditional metta-equation facts");
                goto done;
            }
            rule_name = atom_name_cstr(rule->expr.elems[1]);
            left = equation->expr.elems[1];
            right = equation->expr.elems[2];
            if (left == NULL || left->kind != ATOM_EXPR ||
                left->expr.len == 0u || left->expr.elems[0] == NULL ||
                left->expr.elems[0]->kind != ATOM_SYMBOL ||
                target_variable_name(left->expr.elems[0]) != NULL ||
                !target_collect_left_variables(left, &variables)) {
                free(variables.items);
                target_error(
                    error, error_size,
                    "%s: equation left side must be a left-linear symbol-headed call",
                    rule_name);
                goto done;
            }
            if (!target_right_variables_bound(right, &variables)) {
                free(variables.items);
                target_error(
                    error, error_size,
                    "%s: equation right side contains an unbound variable",
                    rule_name);
                goto done;
            }
            free(variables.items);
            for (size_t prior = 0u; prior < pattern_len; prior++) {
                if (target_pattern_alpha_eq(patterns[prior].left, left)) {
                    target_error(
                        error, error_size,
                        "%s and %s have the same deterministic equation left side",
                        patterns[prior].name, rule_name);
                    goto done;
                }
                if (target_patterns_overlap(patterns[prior].left, left)) {
                    target_error(
                        error, error_size,
                        "%s and %s have overlapping deterministic equation left sides",
                        patterns[prior].name, rule_name);
                    goto done;
                }
            }
            if (pattern_len == pattern_cap) {
                size_t next = pattern_cap == 0u ? 64u : pattern_cap * 2u;
                TargetEquationPattern *grown;
                if (next < pattern_cap ||
                    next > SIZE_MAX / sizeof(*patterns)) {
                    target_error(error, error_size,
                                 "equation presentation is too large");
                    goto done;
                }
                grown = realloc(patterns, next * sizeof(*grown));
                if (grown == NULL) {
                    target_error(error, error_size,
                                 "out of memory collecting equation patterns");
                    goto done;
                }
                patterns = grown;
                pattern_cap = next;
            }
            patterns[pattern_len++] =
                (TargetEquationPattern){.left = left, .name = rule_name};
            if (!target_literal(&program, "(= ") ||
                !target_render(&program, left, &arena, 0u) ||
                !target_byte(&program, (uint8_t)' ') ||
                !target_render(&program, right, &arena, 0u) ||
                !target_literal(&program, ")\n")) {
                target_error(error, error_size,
                             "cannot lower equation target syntax");
                goto done;
            }
        }
    }
    if (pattern_len == 0u) {
        target_error(error, error_size,
                     "equation presentations have no rules");
        goto done;
    }
    *program_out = program.bytes;
    *program_len_out = program.len;
    program.bytes = NULL;
    ok = true;

done:
    free(patterns);
    free(program.bytes);
    arena_free(&arena);
    if (texts != NULL) {
        for (size_t index = 0u; index < presentation_count; index++)
            free(texts[index]);
    }
    free(presentations);
    free(texts);
    return ok;
}

bool cetta_langdef_metta_equations_v1(
    const uint8_t *canonical_presentation,
    size_t canonical_len,
    uint8_t **program_out,
    size_t *program_len_out,
    char *error,
    size_t error_size) {
    const uint8_t *presentations[1] = {canonical_presentation};
    size_t lengths[1] = {canonical_len};
    return cetta_langdef_metta_equation_composition_v1(
        presentations, lengths, 1u, program_out, program_len_out,
        error, error_size);
}
