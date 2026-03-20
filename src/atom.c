#define _GNU_SOURCE
#include "atom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Arena ──────────────────────────────────────────────────────────────── */

static void cetta_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

void *cetta_malloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) cetta_oom(size);
    return ptr;
}

void *cetta_realloc(void *ptr, size_t size) {
    void *out = realloc(ptr, size == 0 ? 1 : size);
    if (!out) cetta_oom(size);
    return out;
}

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

/* ── Hash-Consing ──────────────────────────────────────────────────────── */

HashConsTable *g_hashcons = NULL;

uint32_t atom_hash(Atom *a) {
    if (!a) return 0;
    uint32_t h = 5381;
    h = ((h << 5) + h) ^ (uint32_t)a->kind;
    switch (a->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        for (const char *p = a->name; *p; p++)
            h = ((h << 5) + h) ^ (uint32_t)*p;
        break;
    case ATOM_GROUNDED:
        h = ((h << 5) + h) ^ (uint32_t)a->ground.gkind;
        switch (a->ground.gkind) {
        case GV_INT: h = ((h << 5) + h) ^ (uint32_t)(a->ground.ival & 0xFFFFFFFF); break;
        case GV_FLOAT: { union { double d; uint64_t u; } conv; conv.d = a->ground.fval;
                         h = ((h << 5) + h) ^ (uint32_t)(conv.u & 0xFFFFFFFF); break; }
        case GV_BOOL: h = ((h << 5) + h) ^ (uint32_t)a->ground.bval; break;
        case GV_STRING: {
            for (const char *p = a->ground.sval; *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_SPACE: case GV_STATE: break; /* mutable — don't hash-cons */
        }
        break;
    case ATOM_EXPR:
        h = ((h << 5) + h) ^ a->expr.len;
        for (uint32_t i = 0; i < a->expr.len; i++)
            h = ((h << 5) + h) ^ atom_hash(a->expr.elems[i]);
        break;
    }
    return h;
}

bool atom_eq_fast(Atom *a, Atom *b) {
    if (a == b) return true;
    /* If both are hash-consed, pointer equality is authoritative for immutable atoms */
    return atom_eq(a, b);
}

void hashcons_init(HashConsTable *hc) {
    hc->size = HASHCONS_TABLE_SIZE;
    hc->used = 0;
    hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
    memset(hc->table, 0, sizeof(Atom *) * hc->size);
}

void hashcons_free(HashConsTable *hc) {
    free(hc->table);
    hc->table = NULL;
    hc->size = hc->used = 0;
}

Atom *hashcons_get(HashConsTable *hc, Arena *a, Atom *atom) {
    if (!hc || !atom) return atom;
    /* Don't hash-cons mutable atoms (Space, State) or variables (freshened names) */
    if (atom->kind == ATOM_VAR) return atom;
    if (atom->kind == ATOM_GROUNDED &&
        (atom->ground.gkind == GV_SPACE || atom->ground.gkind == GV_STATE))
        return atom;

    uint32_t h = atom_hash(atom) % hc->size;
    /* Linear probe */
    for (uint32_t i = 0; i < hc->size; i++) {
        uint32_t idx = (h + i) % hc->size;
        if (!hc->table[idx]) {
            /* Empty slot — insert */
            hc->table[idx] = atom;
            hc->used++;
            /* Grow at 70% */
            if (hc->used * 10 > hc->size * 7) {
                uint32_t old_size = hc->size;
                Atom **old_table = hc->table;
                hc->size *= 2;
                hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
                memset(hc->table, 0, sizeof(Atom *) * hc->size);
                hc->used = 0;
                for (uint32_t j = 0; j < old_size; j++)
                    if (old_table[j]) hashcons_get(hc, a, old_table[j]);
                free(old_table);
            }
            return atom;
        }
        if (atom_eq(hc->table[idx], atom))
            return hc->table[idx]; /* Already exists — return shared copy */
    }
    return atom; /* Table full (shouldn't happen with growth) */
}

/* ── Symbol Interning ───────────────────────────────────────────────────── */

InternTable *g_intern = NULL;

void intern_init(InternTable *t) {
    t->size = INTERN_TABLE_SIZE;
    t->used = 0;
    t->names = cetta_malloc(sizeof(const char *) * t->size);
    memset(t->names, 0, sizeof(const char *) * t->size);
}

void intern_free(InternTable *t) {
    /* Names are arena-allocated or static — don't free them */
    free(t->names);
    t->names = NULL;
    t->size = t->used = 0;
}

const char *intern(InternTable *t, const char *name) {
    if (!t || !name) return name;
    /* Hash lookup */
    uint32_t h = 5381;
    for (const char *p = name; *p; p++)
        h = ((h << 5) + h) ^ (uint32_t)*p;
    h %= t->size;
    /* Linear probe */
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (!t->names[idx]) {
            /* Empty slot — insert (strdup to persistent storage) */
            t->names[idx] = strdup(name);
            t->used++;
            /* Grow if > 70% full */
            if (t->used * 10 > t->size * 7) {
                uint32_t old_size = t->size;
                const char **old_names = t->names;
                t->size *= 2;
                t->names = cetta_malloc(sizeof(const char *) * t->size);
                memset(t->names, 0, sizeof(const char *) * t->size);
                t->used = 0;
                for (uint32_t j = 0; j < old_size; j++)
                    if (old_names[j]) intern(t, old_names[j]);
                free(old_names);
                /* Re-lookup after resize */
                return intern(t, name);
            }
            return t->names[idx];
        }
        if (strcmp(t->names[idx], name) == 0)
            return t->names[idx]; /* Already interned */
    }
    return name; /* Table full (shouldn't happen with growth) */
}

/* ── Arena ──────────────────────────────────────────────────────────────── */

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (!a->head || a->head->used + size > ARENA_BLOCK_SIZE) {
        size_t block_size = sizeof(ArenaBlock);
        if (size > ARENA_BLOCK_SIZE)
            block_size = sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + size;
        ArenaBlock *b = cetta_malloc(block_size);
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
    at->name = g_intern ? intern(g_intern, name) : arena_strdup(a, name);
    return at;
}

Atom *atom_var(Arena *a, const char *name) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_VAR;
    /* Don't intern variables — they have unique freshened names */
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
    if (elems) memcpy(at->expr.elems, elems, sizeof(Atom *) * len);
    /* Hash-consing available but NOT automatic — use atom_expr_shared() explicitly */
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
    if (a->kind != ATOM_SYMBOL) return false;
    /* With interning, pointer comparison suffices for interned names */
    if (g_intern && a->name == intern(g_intern, name)) return true;
    return strcmp(a->name, name) == 0;
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

/* ── Deep copy ──────────────────────────────────────────────────────────── */

static Atom *atom_deep_copy_impl(Arena *dst, Atom *src, bool share) {
    switch (src->kind) {
    case ATOM_SYMBOL:   return atom_symbol(dst, src->name);
    case ATOM_VAR:      return atom_var(dst, src->name);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT: {
            Atom *at = atom_int(dst, src->ground.ival);
            return share ? hashcons_get(g_hashcons, dst, at) : at;
        }
        case GV_FLOAT: {
            Atom *at = atom_float(dst, src->ground.fval);
            return share ? hashcons_get(g_hashcons, dst, at) : at;
        }
        case GV_BOOL: {
            Atom *at = atom_bool(dst, src->ground.bval);
            return share ? hashcons_get(g_hashcons, dst, at) : at;
        }
        case GV_STRING: {
            Atom *at = atom_string(dst, src->ground.sval);
            return share ? hashcons_get(g_hashcons, dst, at) : at;
        }
        case GV_SPACE:  return atom_space(dst, src->ground.ptr);
        case GV_STATE:  return atom_state(dst, (StateCell *)src->ground.ptr);
        }
        return atom_symbol(dst, "?");
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * src->expr.len);
        for (uint32_t i = 0; i < src->expr.len; i++)
            elems[i] = atom_deep_copy_impl(dst, src->expr.elems[i], share);
        Atom *at = atom_expr(dst, elems, src->expr.len);
        return share ? hashcons_get(g_hashcons, dst, at) : at;
    }
    }
    return atom_symbol(dst, "?");
}

Atom *atom_deep_copy(Arena *dst, Atom *src) {
    return atom_deep_copy_impl(dst, src, false);
}

Atom *atom_deep_copy_shared(Arena *dst, Atom *src) {
    return atom_deep_copy_impl(dst, src, true);
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
