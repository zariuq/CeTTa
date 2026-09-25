#include "ground_atom_wire_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool ground_atom_wire_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;
    if (buf && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buf, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool wire_head(const Atom *term, const char *head, uint64_t arity) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == arity + 1u && term->expr.elems &&
        atom_is_symbol(term->expr.elems[0], head);
}

static const char *wire_string(const Atom *term) {
    if (!term || term->kind != ATOM_GROUNDED ||
        term->ground.gkind != GV_STRING || !term->ground.sval) {
        return NULL;
    }
    return term->ground.sval;
}

static bool decode_wire(
    const Atom *wire, Arena *arena, uint32_t depth,
    Atom **out, char *error, size_t error_size) {
    const char *text;
    Atom *result = NULL;

    if (!wire || !arena || !out || depth == 0u) {
        return ground_atom_wire_error(
            error, error_size, "ground-atom wire exceeds its depth bound");
    }
    if (wire_head(wire, "ground-atom-symbol-v1", 1u) &&
        (text = wire_string(wire->expr.elems[1])) != NULL) {
        result = atom_symbol(arena, text);
    } else if (wire_head(wire, "ground-atom-variable-v1", 1u) &&
               (text = wire_string(wire->expr.elems[1])) != NULL) {
        result = atom_var(arena, text);
    } else if (wire_head(wire, "ground-atom-integer-v1", 1u) &&
               wire->expr.elems[1] &&
               wire->expr.elems[1]->kind == ATOM_GROUNDED &&
               wire->expr.elems[1]->ground.gkind == GV_INT) {
        result = atom_int(arena, wire->expr.elems[1]->ground.ival);
    } else if (wire_head(wire, "ground-atom-string-v1", 1u) &&
               (text = wire_string(wire->expr.elems[1])) != NULL) {
        result = atom_string(arena, text);
    } else if (wire_head(wire, "ground-atom-boolean-v1", 1u) &&
               atom_is_symbol(wire->expr.elems[1], "false")) {
        result = atom_bool(arena, false);
    } else if (wire_head(wire, "ground-atom-boolean-v1", 1u) &&
               atom_is_symbol(wire->expr.elems[1], "true")) {
        result = atom_bool(arena, true);
    } else if (wire_head(wire, "ground-atom-expression-v1", 1u) &&
               wire->expr.elems[1] &&
               wire->expr.elems[1]->kind == ATOM_EXPR &&
               (wire->expr.elems[1]->expr.len == 0u ||
                wire->expr.elems[1]->expr.elems)) {
        const Atom *items = wire->expr.elems[1];
        CettaExprLen index;
        result = atom_expr_builder_begin(arena, items->expr.len);
        if (!result) {
            return ground_atom_wire_error(
                error, error_size, "cannot allocate decoded ground expression");
        }
        for (index = 0u; index < items->expr.len; index++) {
            if (!decode_wire(items->expr.elems[index], arena, depth - 1u,
                             &result->expr.elems[index], error, error_size)) {
                return false;
            }
        }
        result = atom_expr_builder_finish(arena, result);
    } else if (wire_head(wire, "ground-atom-custom-v1", 2u)) {
        return ground_atom_wire_error(
            error, error_size,
            "custom Lean grounded values have no portable CeTTa carrier");
    } else {
        return ground_atom_wire_error(
            error, error_size, "malformed ground-atom constructor wire");
    }
    if (!result) {
        return ground_atom_wire_error(
            error, error_size, "cannot construct decoded ground atom");
    }
    *out = result;
    return true;
}

bool pp_ground_atom_wire_v1_decode(
    const Atom *wire, Arena *out_arena, uint32_t depth_limit,
    Atom **out, char *error_buf, size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        *out = NULL;
    if (!wire || !out_arena || !out || depth_limit == 0u) {
        return ground_atom_wire_error(
            error_buf, error_buf_size, "bad ground-atom wire arguments");
    }
    return decode_wire(
        wire, out_arena, depth_limit, out, error_buf, error_buf_size);
}
