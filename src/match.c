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
    char buf[256];
    snprintf(buf, sizeof(buf), "%s#%u", name, fresh_var_suffix());
    Atom *fresh = atom_var(a, buf);
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
