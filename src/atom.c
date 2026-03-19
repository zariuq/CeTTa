#include "atom.h"
#include <stdlib.h>
#include <string.h>

/* ── Arena ──────────────────────────────────────────────────────────────── */

void arena_init(Arena *a) {
    a->head = NULL;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (!a->head || a->head->used + size > ARENA_BLOCK_SIZE) {
        size_t block_size = sizeof(ArenaBlock);
        if (size > ARENA_BLOCK_SIZE)
            block_size = sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + size;
        ArenaBlock *b = malloc(block_size);
        b->used = 0;
        b->next = a->head;
        a->head = b;
    }
    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s) + 1;
    char *dst = arena_alloc(a, len);
    memcpy(dst, s, len);
    return dst;
}

/* ── Constructors ───────────────────────────────────────────────────────── */

Atom *atom_symbol(Arena *a, const char *name) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_SYMBOL;
    at->name = arena_strdup(a, name);
    return at;
}

Atom *atom_var(Arena *a, const char *name) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_VAR;
    at->name = arena_strdup(a, name);
    return at;
}

Atom *atom_int(Arena *a, int64_t val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_INT;
    at->ground.ival = val;
    return at;
}

Atom *atom_space(Arena *a, void *space_ptr) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_SPACE;
    at->ground.ptr = space_ptr;
    return at;
}

Atom *atom_state(Arena *a, StateCell *cell) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_STATE;
    at->ground.ptr = cell;
    return at;
}

Atom *atom_float(Arena *a, double val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_FLOAT;
    at->ground.fval = val;
    return at;
}

Atom *atom_bool(Arena *a, bool val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_BOOL;
    at->ground.bval = val;
    return at;
}

Atom *atom_string(Arena *a, const char *val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->ground.gkind = GV_STRING;
    at->ground.sval = arena_strdup(a, val);
    return at;
}

Atom *atom_expr(Arena *a, Atom **elems, uint32_t len) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_EXPR;
    at->expr.len = len;
    at->expr.elems = arena_alloc(a, sizeof(Atom *) * len);
    memcpy(at->expr.elems, elems, sizeof(Atom *) * len);
    return at;
}

Atom *atom_expr2(Arena *a, Atom *a1, Atom *a2) {
    Atom *elems[2] = {a1, a2};
    return atom_expr(a, elems, 2);
}

Atom *atom_expr3(Arena *a, Atom *a1, Atom *a2, Atom *a3) {
    Atom *elems[3] = {a1, a2, a3};
    return atom_expr(a, elems, 3);
}

/* ── Special atoms ──────────────────────────────────────────────────────── */

Atom *atom_empty(Arena *a) { return atom_symbol(a, "Empty"); }
Atom *atom_unit(Arena *a)  { return atom_expr(a, NULL, 0); }
Atom *atom_true(Arena *a)  { return atom_bool(a, true); }
Atom *atom_false(Arena *a) { return atom_bool(a, false); }

/* ── Type system atoms ──────────────────────────────────────────────────── */

Atom *atom_undefined_type(Arena *a) { return atom_symbol(a, "%Undefined%"); }
Atom *atom_atom_type(Arena *a)      { return atom_symbol(a, "Atom"); }
Atom *atom_symbol_type(Arena *a)    { return atom_symbol(a, "Symbol"); }
Atom *atom_variable_type(Arena *a)  { return atom_symbol(a, "Variable"); }
Atom *atom_expression_type(Arena *a){ return atom_symbol(a, "Expression"); }
Atom *atom_grounded_type(Arena *a)  { return atom_symbol(a, "Grounded"); }

Atom *get_meta_type(Arena *a, Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:   return atom_symbol_type(a);
    case ATOM_VAR:      return atom_variable_type(a);
    case ATOM_GROUNDED: return atom_grounded_type(a);
    case ATOM_EXPR:     return atom_expression_type(a);
    }
    return atom_undefined_type(a);
}

Atom *atom_error(Arena *a, Atom *source, Atom *message) {
    return atom_expr3(a, atom_symbol(a, "Error"), source, message);
}

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name) {
    return a->kind == ATOM_SYMBOL && strcmp(a->name, name) == 0;
}

bool atom_is_empty(Atom *a) {
    return atom_is_symbol(a, "Empty");
}

bool atom_is_error(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol(a->expr.elems[0], "Error");
}

bool atom_is_empty_or_error(Atom *a) {
    return atom_is_empty(a) || atom_is_error(a);
}

bool atom_is_var(Atom *a)  { return a->kind == ATOM_VAR; }
bool atom_is_expr(Atom *a) { return a->kind == ATOM_EXPR; }

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return strcmp(a->name, b->name) == 0;
    case ATOM_GROUNDED:
        if (a->ground.gkind != b->ground.gkind) return false;
        switch (a->ground.gkind) {
        case GV_INT:    return a->ground.ival == b->ground.ival;
        case GV_FLOAT:  return a->ground.fval == b->ground.fval;
        case GV_BOOL:   return a->ground.bval == b->ground.bval;
        case GV_STRING: return strcmp(a->ground.sval, b->ground.sval) == 0;
        case GV_SPACE:  return a->ground.ptr == b->ground.ptr;
        case GV_STATE: {
            StateCell *ca = (StateCell *)a->ground.ptr;
            StateCell *cb = (StateCell *)b->ground.ptr;
            return atom_eq(ca->value, cb->value);
        }
        }
        return false;
    case ATOM_EXPR:
        if (a->expr.len != b->expr.len) return false;
        for (uint32_t i = 0; i < a->expr.len; i++)
            if (!atom_eq(a->expr.elems[i], b->expr.elems[i])) return false;
        return true;
    }
    return false;
}

/* ── Printing ───────────────────────────────────────────────────────────── */

void atom_print(Atom *a, FILE *out) {
    switch (a->kind) {
    case ATOM_SYMBOL:
        fputs(a->name, out);
        break;
    case ATOM_VAR:
        fprintf(out, "$%s", a->name);
        break;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:    fprintf(out, "%ld", (long)a->ground.ival); break;
        case GV_FLOAT:  fprintf(out, "%g", a->ground.fval); break;
        case GV_BOOL:   fputs(a->ground.bval ? "True" : "False", out); break;
        case GV_STRING: {
            fputc('"', out);
            for (const char *p = a->ground.sval; *p; p++) {
                if (*p == '\n') fputs("\\n", out);
                else if (*p == '"') fputs("\\\"", out);
                else if (*p == '\\') fputs("\\\\", out);
                else fputc(*p, out);
            }
            fputc('"', out);
            break;
        }
        case GV_SPACE:  fprintf(out, "<space %p>", a->ground.ptr); break;
        case GV_STATE: {
            StateCell *cell = (StateCell *)a->ground.ptr;
            fputs("(State ", out);
            atom_print(cell->value, out);
            fputc(')', out);
            break;
        }
        }
        break;
    case ATOM_EXPR:
        fputc('(', out);
        for (uint32_t i = 0; i < a->expr.len; i++) {
            if (i > 0) fputc(' ', out);
            atom_print(a->expr.elems[i], out);
        }
        fputc(')', out);
        break;
    }
}
