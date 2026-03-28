#define _GNU_SOURCE
#include "atom.h"
#include "stats.h"
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
    a->hashcons = NULL;
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

void arena_set_hashcons(Arena *a, HashConsTable *hc) {
    if (!a) return;
    a->hashcons = hc;
}

/* ── Hash-Consing ──────────────────────────────────────────────────────── */

HashConsTable *g_hashcons = NULL;

uint32_t atom_hash(Atom *a) {
    if (!a) return 0;
    uint32_t h = 5381;
    h = ((h << 5) + h) ^ (uint32_t)a->kind;
    switch (a->kind) {
    case ATOM_SYMBOL: {
        uint64_t sh = symbol_hash_value(g_symbols, a->sym_id);
        h = ((h << 5) + h) ^ (uint32_t)(sh & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(sh >> 32);
        break;
    }
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
    if (!hc || !hc->table) return;
    for (uint32_t i = 0; i < hc->size; i++) {
        Atom *atom = hc->table[i];
        if (!atom) continue;
        if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
            free((void *)atom->ground.sval);
        if (atom->kind == ATOM_EXPR)
            free(atom->expr.elems);
        free(atom);
    }
    free(hc->table);
    hc->table = NULL;
    hc->size = hc->used = 0;
}

static bool atom_is_hash_stable(const Atom *atom) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!atom_is_hash_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool atom_can_hashcons(const Atom *atom) {
    if (!atom || atom->kind == ATOM_VAR) return false;
    return atom_is_hash_stable(atom);
}

static uint32_t hashcons_find_slot(HashConsTable *hc, Atom *atom, bool *found) {
    uint32_t h = atom_hash(atom) % hc->size;
    for (uint32_t i = 0; i < hc->size; i++) {
        uint32_t idx = (h + i) % hc->size;
        if (!hc->table[idx]) {
            *found = false;
            return idx;
        }
        if (atom_eq(hc->table[idx], atom)) {
            *found = true;
            return idx;
        }
    }
    *found = false;
    return hc->size;
}

static void hashcons_grow(HashConsTable *hc) {
    Atom **old_table = hc->table;
    uint32_t old_size = hc->size;
    hc->size *= 2;
    hc->used = 0;
    hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
    memset(hc->table, 0, sizeof(Atom *) * hc->size);
    for (uint32_t i = 0; i < old_size; i++) {
        Atom *atom = old_table[i];
        if (!atom) continue;
        bool found = false;
        uint32_t slot = hashcons_find_slot(hc, atom, &found);
        hc->table[slot] = atom;
        hc->used++;
    }
    free(old_table);
}

static Atom *hashcons_alloc_owned(const Atom *atom) {
    Atom *owned = cetta_malloc(sizeof(Atom));
    *owned = *atom;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        owned->ground.sval = strdup(atom->ground.sval);
        if (!owned->ground.sval)
            cetta_oom(strlen(atom->ground.sval) + 1);
    } else if (atom->kind == ATOM_EXPR) {
        if (atom->expr.len == 0) {
            owned->expr.elems = NULL;
        } else {
            owned->expr.elems = cetta_malloc(sizeof(Atom *) * atom->expr.len);
            memcpy(owned->expr.elems, atom->expr.elems, sizeof(Atom *) * atom->expr.len);
        }
    }
    return owned;
}

Atom *hashcons_get(HashConsTable *hc, Atom *atom) {
    if (!hc || !atom) return atom;
    if (!atom_can_hashcons(atom))
        return atom;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_ATTEMPT);
    bool found = false;
    uint32_t slot = hashcons_find_slot(hc, atom, &found);
    if (found) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_HIT);
        return hc->table[slot];
    }
    if (slot >= hc->size)
        return atom;
    Atom *owned = hashcons_alloc_owned(atom);
    hc->table[slot] = owned;
    hc->used++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_INSERT);
    if (hc->used * 10 > hc->size * 7)
        hashcons_grow(hc);
    return owned;
}

/* ── Variables / cached literal lookups ────────────────────────────────── */

VarInternTable *g_var_intern = NULL;

#define SYMBOL_LITERAL_CACHE_SIZE 64

typedef struct {
    const SymbolTable *table;
    const char *literal;
    SymbolId id;
} SymbolLiteralCacheEntry;

static SymbolLiteralCacheEntry g_symbol_literal_cache[SYMBOL_LITERAL_CACHE_SIZE];

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

void var_intern_init(VarInternTable *t) {
    t->size = 4096;
    t->used = 0;
    t->spellings = cetta_malloc(sizeof(SymbolId) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->spellings, 0, sizeof(SymbolId) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
}

void var_intern_free(VarInternTable *t) {
    free(t->spellings);
    free(t->ids);
    t->spellings = NULL;
    t->ids = NULL;
    t->size = t->used = 0;
}

static void var_intern_insert_owned(VarInternTable *t, SymbolId spelling, VarId id) {
    uint32_t h = spelling % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (t->spellings[idx] == SYMBOL_ID_NONE) {
            t->spellings[idx] = spelling;
            t->ids[idx] = id;
            t->used++;
            return;
        }
    }
    fprintf(stderr, "fatal: variable intern table insertion failed during resize\n");
    abort();
}

static void var_intern_resize(VarInternTable *t, uint32_t new_size) {
    SymbolId *old_spellings = t->spellings;
    VarId *old_ids = t->ids;
    uint32_t old_size = t->size;
    t->size = new_size;
    t->used = 0;
    t->spellings = cetta_malloc(sizeof(SymbolId) * t->size);
    t->ids = cetta_malloc(sizeof(VarId) * t->size);
    memset(t->spellings, 0, sizeof(SymbolId) * t->size);
    memset(t->ids, 0, sizeof(VarId) * t->size);
    for (uint32_t i = 0; i < old_size; i++) {
        if (old_spellings[i] != SYMBOL_ID_NONE)
            var_intern_insert_owned(t, old_spellings[i], old_ids[i]);
    }
    free(old_spellings);
    free(old_ids);
}

VarId var_intern(VarInternTable *t, SymbolId spelling) {
    if (!t || spelling == SYMBOL_ID_NONE) return fresh_var_id();
    if ((t->used + 1) * 10 > t->size * 7)
        var_intern_resize(t, t->size ? t->size * 2 : 4096);
    uint32_t h = spelling % t->size;
    for (uint32_t i = 0; i < t->size; i++) {
        uint32_t idx = (h + i) % t->size;
        if (t->spellings[idx] == SYMBOL_ID_NONE) {
            VarId id = fresh_var_id();
            t->spellings[idx] = spelling;
            t->ids[idx] = id;
            t->used++;
            return id;
        }
        if (t->spellings[idx] == spelling)
            return t->ids[idx];
    }
    return fresh_var_id();
}

static SymbolId symbol_cached_literal(const char *name) {
    if (!g_symbols || !name || !*name) return SYMBOL_ID_NONE;

    uintptr_t key = (((uintptr_t)g_symbols) >> 4) ^ (((uintptr_t)name) >> 4);
    uint32_t idx = (uint32_t)(key % SYMBOL_LITERAL_CACHE_SIZE);
    SymbolLiteralCacheEntry *entry = &g_symbol_literal_cache[idx];

    if (entry->table == g_symbols && entry->literal == name &&
        entry->id != SYMBOL_ID_NONE) {
        return entry->id;
    }

    SymbolId id = symbol_intern_cstr(g_symbols, name);
    entry->table = g_symbols;
    entry->literal = name;
    entry->id = id;
    return id;
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

static Atom *atom_maybe_hashcons(Arena *a, const Atom *temp) {
    if (!a || !a->hashcons || !atom_can_hashcons(temp))
        return NULL;
    return hashcons_get(a->hashcons, (Atom *)temp);
}

Atom *atom_symbol_id(Arena *a, SymbolId sym_id) {
    Atom temp = {0};
    temp.kind = ATOM_SYMBOL;
    temp.var_id = VAR_ID_NONE;
    temp.sym_id = sym_id;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_symbol(Arena *a, const char *name) {
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols, name));
}

Atom *atom_var_with_spelling(Arena *a, SymbolId spelling, VarId id) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_VAR;
    at->var_id = id ? id : fresh_var_id();
    at->sym_id = spelling;
    return at;
}

Atom *atom_var_with_id(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_intern_cstr(g_symbols, name), id);
}

Atom *atom_var(Arena *a, const char *name) {
    SymbolId spelling = symbol_intern_cstr(g_symbols, name);
    VarId id = g_var_intern ? var_intern(g_var_intern, spelling) : fresh_var_id();
    Atom *at = atom_var_with_id(a, name, id);
    return at;
}

Atom *atom_int(Arena *a, int64_t val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.var_id = VAR_ID_NONE;
    temp.ground.gkind = GV_INT;
    temp.ground.ival = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_space(Arena *a, void *space_ptr) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->ground.gkind = GV_SPACE;
    at->ground.ptr = space_ptr;
    return at;
}

Atom *atom_state(Arena *a, StateCell *cell) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->ground.gkind = GV_STATE;
    at->ground.ptr = cell;
    return at;
}

Atom *atom_capture(Arena *a, CaptureClosure *closure) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->ground.gkind = GV_CAPTURE;
    at->ground.ptr = closure;
    return at;
}

Atom *atom_foreign(Arena *a, CettaForeignValue *value) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = ATOM_GROUNDED;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->ground.gkind = GV_FOREIGN;
    at->ground.ptr = value;
    return at;
}

Atom *atom_float(Arena *a, double val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.var_id = VAR_ID_NONE;
    temp.ground.gkind = GV_FLOAT;
    temp.ground.fval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_bool(Arena *a, bool val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.var_id = VAR_ID_NONE;
    temp.ground.gkind = GV_BOOL;
    temp.ground.bval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_string(Arena *a, const char *val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.var_id = VAR_ID_NONE;
    temp.ground.gkind = GV_STRING;
    temp.ground.sval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->ground.gkind = temp.ground.gkind;
    at->ground.sval = arena_strdup(a, val);
    return at;
}

Atom *atom_expr(Arena *a, Atom **elems, uint32_t len) {
    Atom temp = {0};
    temp.kind = ATOM_EXPR;
    temp.var_id = VAR_ID_NONE;
    temp.expr.len = len;
    temp.expr.elems = elems;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->expr.len = len;
    at->expr.elems = arena_alloc(a, sizeof(Atom *) * len);
    if (elems) memcpy(at->expr.elems, elems, sizeof(Atom *) * len);
    return at;
}

Atom *atom_expr_shared(Arena *a, Atom **elems, uint32_t len) {
    Atom temp = {0};
    temp.kind = ATOM_EXPR;
    temp.var_id = VAR_ID_NONE;
    temp.expr.len = len;
    temp.expr.elems = elems;
    if (g_hashcons && atom_can_hashcons(&temp))
        return hashcons_get(g_hashcons, &temp);
    return atom_expr(a, elems, len);
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

Atom *atom_empty(Arena *a) { return atom_symbol_id(a, g_builtin_syms.empty); }
Atom *atom_unit(Arena *a)  { return atom_expr(a, NULL, 0); }
Atom *atom_true(Arena *a)  { return atom_bool(a, true); }
Atom *atom_false(Arena *a) { return atom_bool(a, false); }

/* ── Type system atoms ──────────────────────────────────────────────────── */

Atom *atom_undefined_type(Arena *a) { return atom_symbol_id(a, g_builtin_syms.undefined_type); }
Atom *atom_atom_type(Arena *a)      { return atom_symbol_id(a, g_builtin_syms.atom); }
Atom *atom_symbol_type(Arena *a)    { return atom_symbol_id(a, g_builtin_syms.symbol); }
Atom *atom_variable_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.variable); }
Atom *atom_expression_type(Arena *a){ return atom_symbol_id(a, g_builtin_syms.expression); }
Atom *atom_grounded_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.grounded); }

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
    return atom_expr3(a, atom_symbol_id(a, g_builtin_syms.error), source, message);
}

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name) {
    return atom_is_symbol_id(a, symbol_cached_literal(name));
}

bool atom_is_empty(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.empty);
}

bool atom_is_error(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.error);
}

bool atom_is_empty_or_error(Atom *a) {
    return atom_is_empty(a) || atom_is_error(a);
}

bool atom_is_var(Atom *a)  { return a->kind == ATOM_VAR; }
bool atom_is_expr(Atom *a) { return a->kind == ATOM_EXPR; }

bool atom_is_symbol_id(Atom *a, SymbolId id) {
    return a && a->kind == ATOM_SYMBOL && a->sym_id == id;
}

const char *atom_name_cstr(Atom *a) {
    if (!a) return "";
    if (a->kind == ATOM_SYMBOL || a->kind == ATOM_VAR)
        return symbol_bytes(g_symbols, a->sym_id);
    return "";
}

SymbolId atom_head_symbol_id(Atom *a) {
    if (!a) return SYMBOL_ID_NONE;
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ATOM_SYMBOL:
        return a->sym_id == b->sym_id;
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
    case ATOM_SYMBOL:   return atom_symbol_id(dst, src->sym_id);
    case ATOM_VAR:      return atom_var_with_spelling(dst, src->sym_id, src->var_id);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_int(dst, src->ground.ival))
                                       : atom_int(dst, src->ground.ival);
        case GV_FLOAT:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_float(dst, src->ground.fval))
                                       : atom_float(dst, src->ground.fval);
        case GV_BOOL:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_bool(dst, src->ground.bval))
                                       : atom_bool(dst, src->ground.bval);
        case GV_STRING:
            return share && g_hashcons ? hashcons_get(g_hashcons, atom_string(dst, src->ground.sval))
                                       : atom_string(dst, src->ground.sval);
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
        return share ? atom_expr_shared(dst, elems, src->expr.len)
                     : atom_expr(dst, elems, src->expr.len);
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
        fputs(atom_name_cstr(a), out);
        break;
    case ATOM_VAR:
        fprintf(out, "$%s", atom_name_cstr(a));
        {
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
