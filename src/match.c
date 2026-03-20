#include "match.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Bindings ───────────────────────────────────────────────────────────── */

void bindings_init(Bindings *b) {
    b->len = 0;
}

Atom *bindings_lookup(Bindings *b, const char *var) {
    for (uint32_t i = 0; i < b->len; i++) {
        if (strcmp(b->entries[i].var, var) == 0)
            return b->entries[i].val;
    }
    return NULL;
}

static void epoch_var_buf(char *buf, size_t buf_size, const char *name, uint32_t epoch) {
    snprintf(buf, buf_size, "%s#%u", name, epoch);
}

static Atom *epoch_var_atom(Arena *a, const char *name, uint32_t epoch) {
    char buf[256];
    epoch_var_buf(buf, sizeof(buf), name, epoch);
    return atom_var(a, buf);
}

bool bindings_add(Bindings *b, const char *var, Atom *val) {
    if (val->kind == ATOM_VAR && strcmp(var, val->name) == 0) {
        return true;
    }
    if (val->kind == ATOM_VAR) {
        Atom *other = bindings_lookup(b, val->name);
        if (other && other->kind == ATOM_VAR && strcmp(other->name, var) == 0) {
            return true;
        }
    }
    /* Check for existing binding */
    Atom *existing = bindings_lookup(b, var);
    if (existing) {
        /* Already bound — unify structurally instead of demanding
           literal equality, so repeated higher-order type constraints
           can refine earlier variable bindings. */
        if (existing == val || atom_eq(existing, val)) return true;
        return match_atoms(existing, val, b);
    }
    if (b->len >= MAX_BINDINGS) return false;
    b->entries[b->len].var = var;
    b->entries[b->len].val = val;
    b->len++;
    return true;
}

bool bindings_try_merge(Bindings *dst, const Bindings *src) {
    Bindings merged = *dst;
    for (uint32_t i = 0; i < src->len; i++) {
        if (!bindings_add(&merged, src->entries[i].var, src->entries[i].val))
            return false;
    }
    *dst = merged;
    return true;
}

static bool bindings_seen_var(const char **seen, uint32_t len, const char *name) {
    for (uint32_t i = 0; i < len; i++) {
        if (strcmp(seen[i], name) == 0) return true;
    }
    return false;
}

static Atom *bindings_apply_seen(Bindings *b, Arena *a, Atom *atom,
                                 const char **seen, uint32_t seen_len) {
    switch (atom->kind) {
    case ATOM_VAR: {
        if (bindings_seen_var(seen, seen_len, atom->name)) return atom;
        Atom *val = bindings_lookup(b, atom->name);
        if (!val) return atom;
        seen[seen_len] = atom->name;
        return bindings_apply_seen(b, a, val, seen, seen_len + 1);
    }
    case ATOM_EXPR: {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = bindings_apply_seen(b, a, atom->expr.elems[i], seen, seen_len);
            if (new_elems[i] != atom->expr.elems[i]) changed = true;
        }
        if (!changed) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    const char *seen[MAX_BINDINGS];
    return bindings_apply_seen(b, a, atom, seen, 0);
}

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
                                       bool original_side,
                                       const char **seen, uint32_t seen_len) {
    switch (atom->kind) {
    case ATOM_VAR: {
        const char *lookup_name = atom->name;
        char tagged[256];
        if (original_side) {
            epoch_var_buf(tagged, sizeof(tagged), atom->name, epoch);
            lookup_name = tagged;
        }
        if (bindings_seen_var(seen, seen_len, lookup_name)) {
            return original_side ? epoch_var_atom(a, atom->name, epoch) : atom;
        }
        Atom *val = bindings_lookup(b, lookup_name);
        if (!val) {
            return original_side ? epoch_var_atom(a, atom->name, epoch) : atom;
        }
        seen[seen_len] = lookup_name;
        return bindings_apply_seen_epoch(b, a, val, epoch, false, seen, seen_len + 1);
    }
    case ATOM_EXPR: {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = bindings_apply_seen_epoch(
                b, a, atom->expr.elems[i], epoch, original_side, seen, seen_len);
            if (new_elems[i] != atom->expr.elems[i]) changed = true;
        }
        if (!changed) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch) {
    const char *seen[MAX_BINDINGS];
    return bindings_apply_seen_epoch(b, a, atom, epoch, true, seen, 0);
}

void binding_set_init(BindingSet *bs) {
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

void binding_set_free(BindingSet *bs) {
    free(bs->items);
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

bool binding_set_push(BindingSet *bs, const Bindings *b) {
    if (bs->len >= bs->cap) {
        bs->cap = bs->cap ? bs->cap * 2 : 8;
        bs->items = cetta_realloc(bs->items, sizeof(Bindings) * bs->cap);
    }
    bs->items[bs->len++] = *b;
    return true;
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

static uint32_t g_var_counter = 0;

uint32_t fresh_var_suffix(void) {
    return g_var_counter++;
}

Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix) {
    switch (atom->kind) {
    case ATOM_VAR: {
        /* $name → $name#suffix */
        char buf[256];
        snprintf(buf, sizeof(buf), "%s#%u", atom->name, suffix);
        return atom_var(a, buf);
    }
    case ATOM_EXPR: {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = rename_vars(a, atom->expr.elems[i], suffix);
            if (new_elems[i] != atom->expr.elems[i]) changed = true;
        }
        if (!changed) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

/* ── One-way pattern matching ───────────────────────────────────────────── */

bool simple_match(Atom *pattern, Atom *target, Bindings *b) {
    /* Variable in pattern binds to target */
    if (pattern->kind == ATOM_VAR) {
        return bindings_add(b, pattern->name, target);
    }

    /* Same kind required */
    if (pattern->kind != target->kind) return false;

    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return strcmp(pattern->name, target->name) == 0;

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind) return false;
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return strcmp(pattern->ground.sval, target->ground.sval) == 0;
        case GV_SPACE:
        case GV_STATE:  return pattern->ground.ptr == target->ground.ptr;
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len) return false;
        for (uint32_t i = 0; i < pattern->expr.len; i++) {
            if (!simple_match(pattern->expr.elems[i], target->expr.elems[i], b))
                return false;
        }
        return true;

    case ATOM_VAR:
        /* Already handled above */
        return false;
    }
    return false;
}

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* ── Loop-binding rejection (occurs check) ─────────────────────────────── */

/* Check if following variable bindings from a starting variable
   eventually leads back to that variable (transitive loop detection) */
static bool has_transitive_loop(Bindings *b, const char *start_var,
                                 Atom *val, const char **visited, uint32_t depth) {
    if (depth > MAX_BINDINGS) return true; /* safety cap */
    switch (val->kind) {
    case ATOM_VAR:
        if (strcmp(val->name, start_var) == 0) return true;
        /* Follow the binding chain */
        if (bindings_seen_var(visited, depth, val->name)) return false;
        visited[depth] = val->name;
        Atom *next = bindings_lookup(b, val->name);
        if (next) return has_transitive_loop(b, start_var, next, visited, depth + 1);
        return false;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < val->expr.len; i++)
            if (has_transitive_loop(b, start_var, val->expr.elems[i], visited, depth))
                return true;
        return false;
    default:
        return false;
    }
}

bool bindings_has_loop(Bindings *b) {
    for (uint32_t i = 0; i < b->len; i++) {
        Atom *val = b->entries[i].val;
        if (val->kind == ATOM_VAR && strcmp(val->name, b->entries[i].var) == 0)
            continue; /* $x = $x is not a loop */
        const char *visited[MAX_BINDINGS];
        visited[0] = b->entries[i].var;
        if (has_transitive_loop(b, b->entries[i].var, val, visited, 1))
            return true;
    }
    return false;
}

/* ── Type matching ─────────────────────────────────────────────────────── */

bool match_types(Atom *type1, Atom *type2, Bindings *b) {
    /* %Undefined% and Atom are wildcards — always match */
    if (atom_is_symbol(type1, "%Undefined%") || atom_is_symbol(type1, "Atom") ||
        atom_is_symbol(type2, "%Undefined%") || atom_is_symbol(type2, "Atom")) {
        return true;
    }
    return match_atoms(type1, type2, b);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_depth(Atom *left, Atom *right, Bindings *b, int depth);
static bool match_atoms_epoch_depth(Atom *left, Atom *right, Bindings *b, Arena *a,
                                    uint32_t epoch, bool right_original, int depth);

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    return match_atoms_depth(left, right, b, 64);
}

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_depth(left, right, b, a, epoch, true, 64);
}

static bool match_atoms_depth(Atom *left, Atom *right, Bindings *b, int depth) {
    if (depth <= 0) return false; /* prevent infinite recursion */
    if (left->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup(b, left->name);
        if (existing) return match_atoms_depth(existing, right, b, depth - 1);
        if (right->kind == ATOM_VAR) {
            Atom *right_existing = bindings_lookup(b, right->name);
            if (right_existing) return match_atoms_depth(left, right_existing, b, depth - 1);
            if (strcmp(left->name, right->name) == 0) return true;
        }
        return bindings_add(b, left->name, right);
    }
    if (right->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup(b, right->name);
        if (existing) return match_atoms_depth(left, existing, b, depth - 1);
        return bindings_add(b, right->name, left);
    }
    /* Symbol/Symbol → must be equal */
    if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
        return strcmp(left->name, right->name) == 0;
    }
    /* Grounded/Grounded → structural equality */
    if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
        return atom_eq(left, right);
    }
    /* Expression/Expression → recursive element-wise */
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len) return false;
        for (uint32_t i = 0; i < left->expr.len; i++) {
            if (!match_atoms_depth(left->expr.elems[i], right->expr.elems[i], b, depth - 1))
                return false;
        }
        return true;
    }
    /* Different kinds (non-variable) → no match */
    return false;
}

static bool match_atoms_epoch_depth(Atom *left, Atom *right, Bindings *b, Arena *a,
                                    uint32_t epoch, bool right_original, int depth) {
    if (depth <= 0) return false;
    if (left->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup(b, left->name);
        if (existing)
            return match_atoms_epoch_depth(existing, right, b, a, epoch, right_original, depth - 1);
        if (right->kind == ATOM_VAR) {
            const char *right_name = right->name;
            char tagged[256];
            if (right_original) {
                epoch_var_buf(tagged, sizeof(tagged), right->name, epoch);
                right_name = tagged;
            }
            Atom *right_existing = bindings_lookup(b, right_name);
            if (right_existing)
                return match_atoms_epoch_depth(left, right_existing, b, a, epoch, false, depth - 1);
            if (strcmp(left->name, right_name) == 0) return true;
            return bindings_add(b, left->name,
                                right_original ? epoch_var_atom(a, right->name, epoch) : right);
        }
        return bindings_add(b, left->name,
                            right_original ? rename_vars(a, right, epoch) : right);
    }
    if (right->kind == ATOM_VAR) {
        const char *right_name = right->name;
        char tagged[256];
        if (right_original) {
            epoch_var_buf(tagged, sizeof(tagged), right->name, epoch);
            right_name = tagged;
        }
        Atom *existing = bindings_lookup(b, right_name);
        if (existing)
            return match_atoms_epoch_depth(left, existing, b, a, epoch, false, depth - 1);
        return bindings_add(b,
                            right_original ? arena_strdup(a, right_name) : right_name,
                            left);
    }
    if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
        return strcmp(left->name, right->name) == 0;
    }
    if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
        return atom_eq(left, right);
    }
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len) return false;
        for (uint32_t i = 0; i < left->expr.len; i++) {
            if (!match_atoms_epoch_depth(left->expr.elems[i], right->expr.elems[i], b, a,
                                         epoch, right_original, depth - 1))
                return false;
        }
        return true;
    }
    return false;
}

bool bindings_eq(Bindings *a, Bindings *b) {
    if (a->len != b->len) return false;
    for (uint32_t i = 0; i < a->len; i++) {
        Atom *other = bindings_lookup(b, a->entries[i].var);
        if (!other || !atom_eq(other, a->entries[i].val))
            return false;
    }
    return true;
}
