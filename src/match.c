#include "match.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Bindings ───────────────────────────────────────────────────────────── */

#define BINDINGS_RECURSION_LIMIT 512
#define BINDINGS_MIN_CAPACITY 8

static inline bool binding_name_eq(const char *lhs, const char *rhs) {
    return lhs == rhs || strcmp(lhs, rhs) == 0;
}

static Atom *bindings_resolve_atom(Bindings *b, Atom *atom, uint32_t depth) {
    if (!atom || depth > BINDINGS_RECURSION_LIMIT) return atom;
    while (atom->kind == ATOM_VAR) {
        Atom *next = bindings_lookup(b, atom->name);
        if (!next) return atom;
        if (next == atom) return atom;
        if (next->kind == ATOM_VAR && binding_name_eq(next->name, atom->name))
            return atom;
        atom = next;
        if (++depth > BINDINGS_RECURSION_LIMIT) return atom;
    }
    return atom;
}

static bool atom_contains_unbound_var(Bindings *b, Atom *atom, uint32_t depth) {
    atom = bindings_resolve_atom(b, atom, depth);
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (atom_contains_unbound_var(b, atom->expr.elems[i], depth + 1))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool atom_eq_under_bindings(Bindings *b, Atom *lhs, Atom *rhs, uint32_t depth) {
    lhs = bindings_resolve_atom(b, lhs, depth);
    rhs = bindings_resolve_atom(b, rhs, depth);
    if (lhs == rhs) return true;
    if (lhs->kind != rhs->kind) return false;
    switch (lhs->kind) {
    case ATOM_VAR:
        return binding_name_eq(lhs->name, rhs->name);
    case ATOM_SYMBOL:
        return binding_name_eq(lhs->name, rhs->name);
    case ATOM_GROUNDED:
        return atom_eq(lhs, rhs);
    case ATOM_EXPR:
        if (lhs->expr.len != rhs->expr.len) return false;
        for (uint32_t i = 0; i < lhs->expr.len; i++) {
            if (!atom_eq_under_bindings(b, lhs->expr.elems[i], rhs->expr.elems[i], depth + 1))
                return false;
        }
        return true;
    }
    return false;
}

static bool constraint_pair_eq(const BindingConstraint *lhs, const BindingConstraint *rhs) {
    return (atom_eq(lhs->lhs, rhs->lhs) && atom_eq(lhs->rhs, rhs->rhs)) ||
           (atom_eq(lhs->lhs, rhs->rhs) && atom_eq(lhs->rhs, rhs->lhs));
}

static bool bindings_reserve_entries(Bindings *b, uint32_t needed) {
    if (needed <= b->cap) return true;
    uint32_t next_cap = b->cap ? b->cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < needed) next_cap *= 2;
    b->entries = cetta_realloc(b->entries, sizeof(Binding) * next_cap);
    b->cap = next_cap;
    return true;
}

static bool bindings_reserve_constraints(Bindings *b, uint32_t needed) {
    if (needed <= b->eq_cap) return true;
    uint32_t next_cap = b->eq_cap ? b->eq_cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < needed) next_cap *= 2;
    b->constraints = cetta_realloc(b->constraints, sizeof(BindingConstraint) * next_cap);
    b->eq_cap = next_cap;
    return true;
}

static bool bindings_store_constraint(Bindings *b, Atom *lhs, Atom *rhs) {
    BindingConstraint next = {.lhs = lhs, .rhs = rhs};
    for (uint32_t i = 0; i < b->eq_len; i++) {
        if (constraint_pair_eq(&b->constraints[i], &next))
            return true;
    }
    if (!bindings_reserve_constraints(b, b->eq_len + 1)) return false;
    b->constraints[b->eq_len++] = next;
    return true;
}

static bool bindings_add_internal(Bindings *b, const char *var, Atom *val, bool normalize_constraints);
static bool bindings_add_constraint_internal(Bindings *b, Atom *lhs, Atom *rhs,
                                            bool normalize_constraints);

static bool bindings_normalize_constraints(Bindings *b) {
    if (b->eq_len == 0) return true;
    BindingConstraint *pending = cetta_malloc(sizeof(BindingConstraint) * b->eq_len);
    uint32_t npending = b->eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = b->constraints[i];
    b->eq_len = 0;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_internal(b, pending[i].lhs, pending[i].rhs, false)) {
            free(pending);
            return false;
        }
    }
    free(pending);
    return true;
}

void bindings_init(Bindings *b) {
    b->entries = NULL;
    b->len = 0;
    b->cap = 0;
    b->constraints = NULL;
    b->eq_len = 0;
    b->eq_cap = 0;
}

void bindings_free(Bindings *b) {
    free(b->entries);
    free(b->constraints);
    bindings_init(b);
}

bool bindings_clone(Bindings *dst, const Bindings *src) {
    bindings_init(dst);
    if (src->len > 0) {
        if (!bindings_reserve_entries(dst, src->len)) return false;
        memcpy(dst->entries, src->entries, sizeof(Binding) * src->len);
        dst->len = src->len;
    }
    if (src->eq_len > 0) {
        if (!bindings_reserve_constraints(dst, src->eq_len)) {
            bindings_free(dst);
            return false;
        }
        memcpy(dst->constraints, src->constraints,
               sizeof(BindingConstraint) * src->eq_len);
        dst->eq_len = src->eq_len;
    }
    return true;
}

bool bindings_copy(Bindings *dst, const Bindings *src) {
    if (dst == src) return true;
    Bindings tmp;
    if (!bindings_clone(&tmp, src)) return false;
    bindings_free(dst);
    *dst = tmp;
    return true;
}

void bindings_move(Bindings *dst, Bindings *src) {
    *dst = *src;
    bindings_init(src);
}

void bindings_replace(Bindings *dst, Bindings *src) {
    bindings_free(dst);
    bindings_move(dst, src);
}

Atom *bindings_lookup(Bindings *b, const char *var) {
    for (uint32_t i = 0; i < b->len; i++) {
        if (binding_name_eq(b->entries[i].var, var))
            return b->entries[i].val;
    }
    return NULL;
}

char *arena_tagged_var_name(Arena *a, const char *name, uint32_t suffix) {
    size_t name_len = strlen(name);
    size_t needed = name_len + 1 + 10 + 1;
    char *buf = arena_alloc(a, needed);
    snprintf(buf, needed, "%s#%u", name, suffix);
    return buf;
}

static Atom *epoch_var_atom(Arena *a, const char *name, uint32_t epoch) {
    return atom_var(a, arena_tagged_var_name(a, name, epoch));
}

static bool bindings_add_internal(Bindings *b, const char *var, Atom *val,
                                  bool normalize_constraints) {
    Bindings next;
    if (!bindings_clone(&next, b)) return false;
    if (val->kind == ATOM_VAR && binding_name_eq(var, val->name)) {
        bindings_free(&next);
        return true;
    }
    if (val->kind == ATOM_VAR) {
        Atom *other = bindings_lookup(&next, val->name);
        if (other && other->kind == ATOM_VAR && binding_name_eq(other->name, var)) {
            bindings_free(&next);
            return true;
        }
    }
    /* Check for existing binding */
    Atom *existing = bindings_lookup(&next, var);
    if (existing) {
        /* Already bound — unify structurally instead of demanding
           literal equality, so repeated higher-order type constraints
           can refine earlier variable bindings. */
        bool ok = (existing == val || atom_eq(existing, val))
            ? true
            : match_atoms(existing, val, &next);
        if (!ok) {
            bindings_free(&next);
            return false;
        }
        if (normalize_constraints && !bindings_normalize_constraints(&next)) {
            bindings_free(&next);
            return false;
        }
        bindings_replace(b, &next);
        return true;
    }
    if (!bindings_reserve_entries(&next, next.len + 1)) {
        bindings_free(&next);
        return false;
    }
    next.entries[next.len].var = var;
    next.entries[next.len].val = val;
    next.len++;
    if (normalize_constraints && !bindings_normalize_constraints(&next)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add(Bindings *b, const char *var, Atom *val) {
    return bindings_add_internal(b, var, val, true);
}

static bool bindings_add_constraint_internal(Bindings *b, Atom *lhs, Atom *rhs,
                                            bool normalize_constraints) {
    Bindings next;
    if (!bindings_clone(&next, b)) return false;
    lhs = bindings_resolve_atom(&next, lhs, 0);
    rhs = bindings_resolve_atom(&next, rhs, 0);

    if (atom_eq_under_bindings(&next, lhs, rhs, 0)) {
        bindings_free(&next);
        return true;
    }
    if (lhs->kind == ATOM_VAR) {
        if (!bindings_add_internal(&next, lhs->name, rhs, false)) {
            bindings_free(&next);
            return false;
        }
    } else if (rhs->kind == ATOM_VAR) {
        if (!bindings_add_internal(&next, rhs->name, lhs, false)) {
            bindings_free(&next);
            return false;
        }
    } else if (!atom_contains_unbound_var(&next, lhs, 0) &&
               !atom_contains_unbound_var(&next, rhs, 0)) {
        bindings_free(&next);
        return false;
    } else if (!bindings_store_constraint(&next, lhs, rhs)) {
        bindings_free(&next);
        return false;
    }

    if (normalize_constraints && !bindings_normalize_constraints(&next)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_constraint(Bindings *b, Atom *lhs, Atom *rhs) {
    return bindings_add_constraint_internal(b, lhs, rhs, true);
}

bool bindings_try_merge(Bindings *dst, const Bindings *src) {
    Bindings merged;
    if (!bindings_clone(&merged, dst)) return false;
    BindingConstraint *pending = cetta_malloc(sizeof(BindingConstraint) *
                                              (merged.eq_len + src->eq_len + 1));
    uint32_t npending = 0;
    for (uint32_t i = 0; i < merged.eq_len; i++)
        pending[npending++] = merged.constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++) {
        pending[npending++] = src->constraints[i];
    }
    merged.eq_len = 0;
    for (uint32_t i = 0; i < src->len; i++) {
        if (!bindings_add_internal(&merged, src->entries[i].var, src->entries[i].val, false)) {
            free(pending);
            bindings_free(&merged);
            return false;
        }
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_internal(&merged, pending[i].lhs, pending[i].rhs, false)) {
            free(pending);
            bindings_free(&merged);
            return false;
        }
    }
    free(pending);
    if (!bindings_normalize_constraints(&merged)) {
        bindings_free(&merged);
        return false;
    }
    bindings_replace(dst, &merged);
    return true;
}

static bool bindings_seen_var(const char **seen, uint32_t len, const char *name) {
    for (uint32_t i = 0; i < len; i++) {
        if (binding_name_eq(seen[i], name)) return true;
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
    uint32_t seen_cap = b->len ? b->len : 1;
    const char **seen = cetta_malloc(sizeof(const char *) * seen_cap);
    Atom *result = bindings_apply_seen(b, a, atom, seen, 0);
    free(seen);
    return result;
}

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
                                       bool original_side,
                                       const char **seen, uint32_t seen_len) {
    switch (atom->kind) {
    case ATOM_VAR: {
        const char *lookup_name = atom->name;
        if (original_side) {
            lookup_name = arena_tagged_var_name(a, atom->name, epoch);
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
    uint32_t seen_cap = b->len ? b->len : 1;
    const char **seen = cetta_malloc(sizeof(const char *) * seen_cap);
    Atom *result = bindings_apply_seen_epoch(b, a, atom, epoch, true, seen, 0);
    free(seen);
    return result;
}

Atom *bindings_to_atom(Arena *a, const Bindings *b) {
    Atom **assigns = NULL;
    if (b->len > 0) {
        assigns = arena_alloc(a, sizeof(Atom *) * b->len);
        for (uint32_t i = 0; i < b->len; i++) {
            assigns[i] = atom_expr2(a,
                atom_symbol(a, b->entries[i].var),
                b->entries[i].val);
        }
    }
    Atom **equalities = NULL;
    if (b->eq_len > 0) {
        equalities = arena_alloc(a, sizeof(Atom *) * b->eq_len);
        for (uint32_t i = 0; i < b->eq_len; i++) {
            equalities[i] = atom_expr2(a,
                b->constraints[i].lhs,
                b->constraints[i].rhs);
        }
    }
    return atom_expr3(a,
        atom_symbol(a, "Bindings"),
        atom_expr(a, assigns, b->len),
        atom_expr(a, equalities, b->eq_len));
}

bool bindings_from_atom(Atom *atom, Bindings *out) {
    bindings_init(out);
    if (atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !atom_is_symbol(atom->expr.elems[0], "Bindings")) {
        return false;
    }

    Atom *assigns = atom->expr.elems[1];
    Atom *equalities = atom->expr.elems[2];
    if (assigns->kind != ATOM_EXPR || equalities->kind != ATOM_EXPR) {
        return false;
    }

    for (uint32_t i = 0; i < assigns->expr.len; i++) {
        Atom *assign = assigns->expr.elems[i];
        if (assign->kind != ATOM_EXPR || assign->expr.len != 2 ||
            assign->expr.elems[0]->kind != ATOM_SYMBOL) {
            bindings_free(out);
            return false;
        }
        if (!bindings_add(out, assign->expr.elems[0]->name, assign->expr.elems[1])) {
            bindings_free(out);
            return false;
        }
    }

    for (uint32_t i = 0; i < equalities->expr.len; i++) {
        Atom *pair = equalities->expr.elems[i];
        if (pair->kind != ATOM_EXPR || pair->expr.len != 2) {
            bindings_free(out);
            return false;
        }
        if (!bindings_add_constraint(out, pair->expr.elems[0], pair->expr.elems[1])) {
            bindings_free(out);
            return false;
        }
    }

    if (bindings_has_loop(out)) {
        bindings_free(out);
        return false;
    }
    return true;
}

void binding_set_init(BindingSet *bs) {
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

void binding_set_free(BindingSet *bs) {
    for (uint32_t i = 0; i < bs->len; i++)
        bindings_free(&bs->items[i]);
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
    if (!bindings_clone(&bs->items[bs->len], b))
        return false;
    bs->len++;
    return true;
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

static uint32_t g_var_counter = 0;

typedef struct {
    const char **items;
    uint32_t len;
    uint32_t cap;
} VarNameSet;

typedef struct {
    const char *name;
    Atom *mapped;
} RenameVarEntry;

typedef struct {
    RenameVarEntry *items;
    uint32_t len;
    uint32_t cap;
} RenameVarMap;

uint32_t fresh_var_suffix(void) {
    return g_var_counter++;
}

static void var_name_set_init(VarNameSet *set) {
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static void var_name_set_free(VarNameSet *set) {
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool var_name_set_contains(const VarNameSet *set, const char *name) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (strcmp(set->items[i], name) == 0)
            return true;
    }
    return false;
}

static void var_name_set_add(VarNameSet *set, const char *name) {
    if (var_name_set_contains(set, name))
        return;
    if (set->len >= set->cap) {
        set->cap = set->cap ? set->cap * 2 : 8;
        set->items = cetta_realloc(set->items, sizeof(const char *) * set->cap);
    }
    set->items[set->len++] = name;
}

static void collect_var_names(Atom *atom, VarNameSet *set) {
    switch (atom->kind) {
    case ATOM_VAR:
        var_name_set_add(set, atom->name);
        return;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++)
            collect_var_names(atom->expr.elems[i], set);
        return;
    default:
        return;
    }
}

static void rename_var_map_init(RenameVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void rename_var_map_free(RenameVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *rename_var_map_lookup(RenameVarMap *map, const char *name) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (strcmp(map->items[i].name, name) == 0)
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *rename_var_map_add_fresh(RenameVarMap *map, Arena *a, const char *name) {
    Atom *fresh = atom_var(a, arena_tagged_var_name(a, name, fresh_var_suffix()));
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items, sizeof(RenameVarEntry) * map->cap);
    }
    map->items[map->len].name = name;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

static Atom *rename_vars_except_rec(Arena *a, Atom *atom,
                                    const VarNameSet *ignore,
                                    RenameVarMap *map) {
    switch (atom->kind) {
    case ATOM_VAR: {
        if (var_name_set_contains(ignore, atom->name))
            return atom;
        Atom *mapped = rename_var_map_lookup(map, atom->name);
        if (mapped)
            return mapped;
        return rename_var_map_add_fresh(map, a, atom->name);
    }
    case ATOM_EXPR: {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = rename_vars_except_rec(a, atom->expr.elems[i], ignore, map);
            if (new_elems[i] != atom->expr.elems[i])
                changed = true;
        }
        if (!changed)
            return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix) {
    switch (atom->kind) {
    case ATOM_VAR: {
        /* $name → $name#suffix */
        return atom_var(a, arena_tagged_var_name(a, atom->name, suffix));
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

Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec) {
    VarNameSet ignore;
    RenameVarMap map;
    var_name_set_init(&ignore);
    rename_var_map_init(&map);
    collect_var_names(ignore_spec, &ignore);
    Atom *result = rename_vars_except_rec(a, atom, &ignore, &map);
    rename_var_map_free(&map);
    var_name_set_free(&ignore);
    return result;
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
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
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
    if (depth > BINDINGS_RECURSION_LIMIT) return true; /* safety cap */
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
        uint32_t visited_cap = (b->len + b->eq_len + 1);
        if (visited_cap == 0) visited_cap = 1;
        const char **visited = cetta_malloc(sizeof(const char *) * visited_cap);
        visited[0] = b->entries[i].var;
        bool has_loop = has_transitive_loop(b, b->entries[i].var, val, visited, 1);
        free(visited);
        if (has_loop)
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

typedef struct {
    const char *left;
    const char *right;
} AlphaPair;

typedef struct {
    AlphaPair *items;
    uint32_t len;
    uint32_t cap;
} AlphaPairSet;

static void alpha_pair_set_init(AlphaPairSet *pairs) {
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static void alpha_pair_set_free(AlphaPairSet *pairs) {
    free(pairs->items);
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static const char *alpha_lookup_left(const AlphaPairSet *pairs, const char *left) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        const char *stored = pairs->items[i].left;
        size_t slen = strcspn(stored, "#");
        size_t llen = strcspn(left, "#");
        if (slen == llen && strncmp(stored, left, slen) == 0)
            return pairs->items[i].right;
    }
    return NULL;
}

static const char *alpha_lookup_right(const AlphaPairSet *pairs, const char *right) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        const char *stored = pairs->items[i].right;
        size_t slen = strcspn(stored, "#");
        size_t rlen = strcspn(right, "#");
        if (slen == rlen && strncmp(stored, right, slen) == 0)
            return pairs->items[i].left;
    }
    return NULL;
}

static bool alpha_add_pair(AlphaPairSet *pairs, const char *left, const char *right) {
    if (pairs->len >= pairs->cap) {
        pairs->cap = pairs->cap ? pairs->cap * 2 : 8;
        pairs->items = cetta_realloc(pairs->items, sizeof(AlphaPair) * pairs->cap);
    }
    pairs->items[pairs->len].left = left;
    pairs->items[pairs->len].right = right;
    pairs->len++;
    return true;
}

static bool atom_alpha_eq_rec(Atom *left, Atom *right, AlphaPairSet *pairs) {
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
        if (left->kind != ATOM_VAR || right->kind != ATOM_VAR)
            return false;
        const char *mapped_right = alpha_lookup_left(pairs, left->name);
        const char *mapped_left = alpha_lookup_right(pairs, right->name);
        if (mapped_right || mapped_left) {
            size_t mr_len = mapped_right ? strcspn(mapped_right, "#") : 0;
            size_t rl_len = strcspn(right->name, "#");
            size_t ml_len = mapped_left ? strcspn(mapped_left, "#") : 0;
            size_t ll_len = strcspn(left->name, "#");
            return mapped_right && mapped_left &&
                   mr_len == rl_len &&
                   ml_len == ll_len &&
                   strncmp(mapped_right, right->name, mr_len) == 0 &&
                   strncmp(mapped_left, left->name, ml_len) == 0;
        }
        return alpha_add_pair(pairs, left->name, right->name);
    }

    if (left->kind != right->kind)
        return false;

    switch (left->kind) {
    case ATOM_SYMBOL:
        return strcmp(left->name, right->name) == 0;
    case ATOM_GROUNDED:
        return atom_eq(left, right);
    case ATOM_EXPR:
        if (left->expr.len != right->expr.len)
            return false;
        for (uint32_t i = 0; i < left->expr.len; i++) {
            if (!atom_alpha_eq_rec(left->expr.elems[i], right->expr.elems[i], pairs))
                return false;
        }
        return true;
    case ATOM_VAR:
        return false;
    }
    return false;
}

bool atom_alpha_eq(Atom *left, Atom *right) {
    AlphaPairSet pairs;
    alpha_pair_set_init(&pairs);
    bool ok = atom_alpha_eq_rec(left, right, &pairs);
    alpha_pair_set_free(&pairs);
    return ok;
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
            if (right_original) {
                right_name = arena_tagged_var_name(a, right->name, epoch);
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
        if (right_original) {
            right_name = arena_tagged_var_name(a, right->name, epoch);
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
    if (a->eq_len != b->eq_len) return false;
    for (uint32_t i = 0; i < a->len; i++) {
        Atom *other = bindings_lookup(b, a->entries[i].var);
        if (!other || !atom_eq(other, a->entries[i].val))
            return false;
    }
    bool *matched = NULL;
    if (b->eq_len > 0) {
        matched = cetta_malloc(sizeof(bool) * b->eq_len);
        memset(matched, 0, sizeof(bool) * b->eq_len);
    }
    for (uint32_t i = 0; i < a->eq_len; i++) {
        bool found = false;
        for (uint32_t j = 0; j < b->eq_len; j++) {
            if (!matched[j] &&
                constraint_pair_eq(&a->constraints[i], &b->constraints[j])) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            free(matched);
            return false;
        }
    }
    free(matched);
    return true;
}
