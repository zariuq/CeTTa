#define _GNU_SOURCE
#include "atom.h"
#include <math.h>
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
        for (const char *p = a->name; *p; p++)
            h = ((h << 5) + h) ^ (uint32_t)*p;
        break;
    case ATOM_VAR:
        h = ((h << 5) + h) ^ (uint32_t)(a->var_id & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(a->var_id >> 32);
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
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            break; /* mutable/contextual — don't hash-cons */
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
        (atom->ground.gkind == GV_SPACE ||
         atom->ground.gkind == GV_STATE ||
         atom->ground.gkind == GV_CAPTURE ||
         atom->ground.gkind == GV_FOREIGN))
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
VarInternTable *g_var_intern = NULL;

#define INTERN_LITERAL_CACHE_SIZE 64

typedef struct {
    const InternTable *table;
    const char *literal;
    const char *interned;
} InternLiteralCacheEntry;

static InternLiteralCacheEntry g_intern_literal_cache[INTERN_LITERAL_CACHE_SIZE];

static uint32_t intern_hash_name(const char *name) {
    uint32_t h = 5381;
    for (const char *p = name; *p; p++)
        h = ((h << 5) + h) ^ (uint32_t)*p;
    return h;
}

static uint32_t var_intern_hash_name(const char *name) {
    return intern_hash_name(name);
}

static uint32_t g_var_base_counter = 1;

VarId fresh_var_id(void) {
    return (VarId)g_var_base_counter++;
}

uint32_t var_base_id(VarId id) {
    return (uint32_t)(id & 0xFFFFFFFFu);
}

uint32_t var_epoch_suffix(VarId id) {
    return (uint32_t)(id >> 32);
}

VarId var_epoch_id(VarId id, uint32_t epoch) {
    uint32_t base = var_base_id(id);
    if (base == 0) {
        base = (uint32_t)fresh_var_id();
    }
    return ((VarId)epoch << 32) | (VarId)base;
}

static void intern_insert_owned(InternTable *t, const char *name) {
    uint32_t h = intern_hash_name(name) % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (!t->names[idx]) {
            t->names[idx] = name;
            t->used++;
            return;
        }
    }
    fprintf(stderr, "fatal: intern table insertion failed during resize\n");
    abort();
}

static void intern_resize(InternTable *t, uint32_t new_size) {
    const char **old_names = t->names;
    uint32_t old_size = t->size;
    t->size = new_size;
    t->used = 0;
    t->names = cetta_malloc(sizeof(const char *) * t->size);
    memset(t->names, 0, sizeof(const char *) * t->size);
    for (uint32_t i = 0; i < old_size; i++) {
        if (old_names[i])
            intern_insert_owned(t, old_names[i]);
    }
    free(old_names);
}

void intern_init(InternTable *t) {
    t->size = INTERN_TABLE_SIZE;
    t->used = 0;
    t->names = cetta_malloc(sizeof(const char *) * t->size);
    memset(t->names, 0, sizeof(const char *) * t->size);
    memset(g_intern_literal_cache, 0, sizeof(g_intern_literal_cache));
}

void intern_free(InternTable *t) {
    for (uint32_t i = 0; i < t->size; i++)
        free((void *)t->names[i]);
    free(t->names);
    t->names = NULL;
    t->size = t->used = 0;
    memset(g_intern_literal_cache, 0, sizeof(g_intern_literal_cache));
}

const char *intern(InternTable *t, const char *name) {
    if (!t || !name) return name;
    if ((t->used + 1) * 10 > t->size * 7)
        intern_resize(t, t->size ? t->size * 2 : INTERN_TABLE_SIZE);
    uint32_t h = intern_hash_name(name) % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (!t->names[idx]) {
            t->names[idx] = strdup(name);
            if (!t->names[idx]) cetta_oom(strlen(name) + 1);
            t->used++;
            return t->names[idx];
        }
        if (strcmp(t->names[idx], name) == 0)
            return t->names[idx];
    }
    return name;
}

void var_intern_init(VarInternTable *t) {
    t->size = INTERN_TABLE_SIZE;
    t->used = 0;
    t->names = cetta_malloc(sizeof(const char *) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->names, 0, sizeof(const char *) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
}

void var_intern_free(VarInternTable *t) {
    free(t->names);
    free(t->ids);
    t->names = NULL;
    t->ids = NULL;
    t->size = t->used = 0;
}

static void var_intern_insert_owned(VarInternTable *t, const char *name, VarId id) {
    uint32_t h = var_intern_hash_name(name) % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (!t->names[idx]) {
            t->names[idx] = name;
            t->ids[idx] = id;
            t->used++;
            return;
        }
    }
    fprintf(stderr, "fatal: variable intern table insertion failed during resize\n");
    abort();
}

static void var_intern_resize(VarInternTable *t, uint32_t new_size) {
    const char **old_names = t->names;
    VarId *old_ids = t->ids;
    uint32_t old_size = t->size;
    t->size = new_size;
    t->used = 0;
    t->names = cetta_malloc(sizeof(const char *) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->names, 0, sizeof(const char *) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
    for (uint32_t i = 0; i < old_size; i++) {
        if (old_names[i])
            var_intern_insert_owned(t, old_names[i], old_ids[i]);
    }
    free(old_names);
    free(old_ids);
}

VarId var_intern(VarInternTable *t, const char *name) {
    if (!t || !name) return fresh_var_id();
    if ((t->used + 1) * 10 > t->size * 7)
        var_intern_resize(t, t->size ? t->size * 2 : INTERN_TABLE_SIZE);
    uint32_t h = var_intern_hash_name(name) % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (!t->names[idx]) {
            const char *stored = g_intern ? intern(g_intern, name) : strdup(name);
            if (!stored) cetta_oom(strlen(name) + 1);
            VarId id = fresh_var_id();
            t->names[idx] = stored;
            t->ids[idx] = id;
            t->used++;
            return id;
        }
        if (strcmp(t->names[idx], name) == 0)
            return t->ids[idx];
    }
    return fresh_var_id();
}

static const char *intern_cached_literal(const char *name) {
    if (!g_intern || !name) return name;

    uintptr_t key = (((uintptr_t)g_intern) >> 4) ^ (((uintptr_t)name) >> 4);
    uint32_t idx = (uint32_t)(key % INTERN_LITERAL_CACHE_SIZE);
    InternLiteralCacheEntry *entry = &g_intern_literal_cache[idx];

    if (entry->table == g_intern && entry->literal == name && entry->interned) {
        return entry->interned;
    }

    const char *interned = intern(g_intern, name);
    entry->table = g_intern;
    entry->literal = name;
    entry->interned = interned;
    return interned;
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
    at->var_id = VAR_ID_NONE;
    at->name = g_intern ? intern(g_intern, name) : arena_strdup(a, name);
    return at;
}

Atom *atom_var_with_id(Arena *a, const char *name, VarId id) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_VAR;
    at->var_id = id ? id : fresh_var_id();
    at->name = g_intern ? intern(g_intern, name) : arena_strdup(a, name);
    return at;
}

Atom *atom_var(Arena *a, const char *name) {
    VarId id = g_var_intern ? var_intern(g_var_intern, name) : fresh_var_id();
    Atom *at = atom_var_with_id(a, name, id);
    return at;
}

Atom *atom_int(Arena *a, int64_t val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_INT;
    at->ground.ival = val;
    return at;
}

Atom *atom_space(Arena *a, void *space_ptr) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_SPACE;
    at->ground.ptr = space_ptr;
    return at;
}

Atom *atom_state(Arena *a, StateCell *cell) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_STATE;
    at->ground.ptr = cell;
    return at;
}

Atom *atom_capture(Arena *a, CaptureClosure *closure) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_CAPTURE;
    at->ground.ptr = closure;
    return at;
}

Atom *atom_foreign(Arena *a, CettaForeignValue *value) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_FOREIGN;
    at->ground.ptr = value;
    return at;
}

Atom *atom_float(Arena *a, double val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_FLOAT;
    at->ground.fval = val;
    return at;
}

Atom *atom_bool(Arena *a, bool val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_BOOL;
    at->ground.bval = val;
    return at;
}

Atom *atom_string(Arena *a, const char *val) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->ground.gkind = GV_STRING;
    at->ground.sval = arena_strdup(a, val);
    return at;
}

Atom *atom_expr(Arena *a, Atom **elems, uint32_t len) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_EXPR;
    at->var_id = VAR_ID_NONE;
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
    if (a->name == name) return true;
    if (g_intern && a->name == intern_cached_literal(name)) return true;
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
        return strcmp(a->name, b->name) == 0;
    case ATOM_VAR:
        return a->var_id == b->var_id;
    case ATOM_GROUNDED:
        if (a->ground.gkind != b->ground.gkind) return false;
        switch (a->ground.gkind) {
        case GV_INT:    return a->ground.ival == b->ground.ival;
        case GV_FLOAT:  return a->ground.fval == b->ground.fval;
        case GV_BOOL:   return a->ground.bval == b->ground.bval;
        case GV_STRING: return strcmp(a->ground.sval, b->ground.sval) == 0;
        case GV_SPACE:  return a->ground.ptr == b->ground.ptr;
        case GV_CAPTURE: return a->ground.ptr == b->ground.ptr;
        case GV_FOREIGN: return a->ground.ptr == b->ground.ptr;
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
    case ATOM_VAR:      return atom_var_with_id(dst, src->name, src->var_id);
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
        case GV_CAPTURE: return atom_capture(dst, (CaptureClosure *)src->ground.ptr);
        case GV_FOREIGN: return atom_foreign(dst, (CettaForeignValue *)src->ground.ptr);
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
        if (!strchr(a->name, '#')) {
            uint32_t epoch = var_epoch_suffix(a->var_id);
            if (epoch != 0)
                fprintf(out, "#%u", epoch);
        }
        break;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:    fprintf(out, "%ld", (long)a->ground.ival); break;
        case GV_FLOAT:
            if (isnan(a->ground.fval))
                fputs("NaN", out);
            else if (isinf(a->ground.fval))
                fputs(signbit(a->ground.fval) ? "-inf" : "inf", out);
            else if (isfinite(a->ground.fval) && floor(a->ground.fval) == a->ground.fval)
                fprintf(out, "%.1f", a->ground.fval);
            else
                fprintf(out, "%.16g", a->ground.fval);
            break;
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
        case GV_CAPTURE:
            fputs("capture", out);
            break;
        case GV_FOREIGN:
            fprintf(out, "<foreign %p>", a->ground.ptr);
            break;
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

char *atom_to_string(Arena *a, Atom *atom) {
    if (atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return arena_strdup(a, atom->ground.sval);
    }
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    if (!mem) {
        return arena_strdup(a, "");
    }
    atom_print(atom, mem);
    fclose(mem);
    char *out = arena_strdup(a, buf ? buf : "");
    free(buf);
    return out;
}
