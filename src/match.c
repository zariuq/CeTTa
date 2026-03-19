#include "match.h"
#include <string.h>

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

bool bindings_add(Bindings *b, const char *var, Atom *val) {
    /* Check for existing binding */
    Atom *existing = bindings_lookup(b, var);
    if (existing) {
        /* Already bound — must be equal */
        return atom_eq(existing, val);
    }
    if (b->len >= MAX_BINDINGS) return false;
    b->entries[b->len].var = var;
    b->entries[b->len].val = val;
    b->len++;
    return true;
}

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    switch (atom->kind) {
    case ATOM_VAR: {
        Atom *val = bindings_lookup(b, atom->name);
        if (!val) return atom;
        /* Follow variable chains (transitive resolution) */
        int depth = 0;
        while (val->kind == ATOM_VAR && depth < MAX_BINDINGS) {
            Atom *next = bindings_lookup(b, val->name);
            if (!next) break;
            val = next;
            depth++;
        }
        /* Recursively apply to the resolved value (may contain vars) */
        return bindings_apply(b, a, val);
    }
    case ATOM_EXPR: {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = bindings_apply(b, a, atom->expr.elems[i]);
            if (new_elems[i] != atom->expr.elems[i]) changed = true;
        }
        if (!changed) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
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

bool match_types(Atom *type1, Atom *type2, Bindings *b) {
    /* %Undefined% and Atom are wildcards — always match */
    if (atom_is_symbol(type1, "%Undefined%") || atom_is_symbol(type1, "Atom") ||
        atom_is_symbol(type2, "%Undefined%") || atom_is_symbol(type2, "Atom")) {
        return true;
    }
    return match_atoms(type1, type2, b);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    /* Var/Var → equality (bind left to right) */
    if (left->kind == ATOM_VAR && right->kind == ATOM_VAR) {
        return bindings_add(b, left->name, right);
    }
    /* Var on left → bind left to right */
    if (left->kind == ATOM_VAR) {
        return bindings_add(b, left->name, right);
    }
    /* Var on right → bind right to left */
    if (right->kind == ATOM_VAR) {
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
            if (!match_atoms(left->expr.elems[i], right->expr.elems[i], b))
                return false;
        }
        return true;
    }
    /* Different kinds (non-variable) → no match */
    return false;
}
