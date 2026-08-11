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

bool cetta_langdef_metta_equations_v1(
    const uint8_t *canonical_presentation,
    size_t canonical_len,
    uint8_t **program_out,
    size_t *program_len_out,
    char *error,
    size_t error_size) {
    char *text = NULL;
    Arena arena;
    Atom **forms = NULL;
    Atom *presentation;
    Atom *rewrites;
    TargetBuffer program = {0};
    int form_count;
    bool ok = false;

    if (canonical_presentation == NULL || program_out == NULL ||
        program_len_out == NULL || canonical_len == SIZE_MAX)
        return target_error(error, error_size,
                            "invalid equation compiler request");
    *program_out = NULL;
    *program_len_out = 0u;
    text = malloc(canonical_len + 1u);
    if (text == NULL)
        return target_error(error, error_size,
                            "out of memory reading canonical presentation");
    memcpy(text, canonical_presentation, canonical_len);
    text[canonical_len] = '\0';
    arena_init(&arena);
    form_count = parse_metta_text(text, &arena, &forms);
    if (form_count != 1 ||
        !(presentation = forms[0]) ||
        presentation->kind != ATOM_EXPR ||
        presentation->expr.len < 3u ||
        !atom_is_symbol(presentation->expr.elems[0],
                        "gslt-presentation-v1") ||
        !(rewrites = target_field(presentation, "rewrites"))) {
        target_error(error, error_size,
                     "canonical equation presentation is malformed");
        goto done;
    }
    for (CettaExprIndex index = 1u;
         index < rewrites->expr.len; index++) {
        Atom *rule = rewrites->expr.elems[index];
        Atom *head;
        Atom *body;
        Atom *equation;
        if (!target_head(rule, "rule", 3u) ||
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
        if (!target_literal(&program, "(= ") ||
            !target_render(&program, equation->expr.elems[1],
                           &arena, 0u) ||
            !target_byte(&program, (uint8_t)' ') ||
            !target_render(&program, equation->expr.elems[2],
                           &arena, 0u) ||
            !target_literal(&program, ")\n")) {
            target_error(error, error_size,
                         "cannot lower equation target syntax");
            goto done;
        }
    }
    if (rewrites->expr.len == 1u) {
        target_error(error, error_size,
                     "equation presentation has no rules");
        goto done;
    }
    *program_out = program.bytes;
    *program_len_out = program.len;
    program.bytes = NULL;
    ok = true;

done:
    free(program.bytes);
    arena_free(&arena);
    free(text);
    return ok;
}
