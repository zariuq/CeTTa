#include "match.h"

#include <stdlib.h>
#include <assert.h>

struct BindingsOwners {
    size_t references;
    size_t count;
    CettaBindingsValue *values[];
};

void bindings_owners_retain(BindingsOwners *owners) {
    if (owners) {
        assert(owners->references < SIZE_MAX);
        owners->references++;
    }
}

void bindings_owners_release(BindingsOwners *owners) {
    if (!owners || --owners->references != 0u) return;
    for (size_t i = 0; i < owners->count; i++)
        owners->values[i]->release(owners->values[i]);
    free(owners);
}

static void bindings_retain_value_owner(Bindings *bindings, CettaBindingsValue *value) {
    BindingsOwners *old = bindings->owners;
    size_t count = old ? old->count : 0u;
    for (size_t i = 0; i < count; i++)
        if (old->values[i] == value) return;
    BindingsOwners *next = cetta_malloc(sizeof(*next) + (count + 1u) * sizeof(value));
    next->references = 1u;
    next->count = count + 1u;
    for (size_t i = 0; i < count; i++) {
        next->values[i] = old->values[i];
        next->values[i]->retain(next->values[i]);
    }
    next->values[count] = value;
    value->retain(value);
    bindings->owners = next;
    bindings_owners_release(old);
}

void bindings_inherit_owners(Bindings *destination, const Bindings *source) {
    if (!source || !source->owners || destination->owners == source->owners) return;
    if (!destination->owners) {
        destination->owners = source->owners;
        bindings_owners_retain(destination->owners);
        return;
    }
    for (size_t i = 0; i < source->owners->count; i++)
        bindings_retain_value_owner(destination, source->owners->values[i]);
}

typedef struct {
    CettaBindingsValue value;
    size_t references;
    Arena syntax;
    Bindings bindings;
} CapturedBindings;

static void captured_bindings_retain(void *raw) {
    CapturedBindings *captured = raw;
    assert(captured->references < SIZE_MAX);
    captured->references++;
}

static void captured_bindings_release(void *raw) {
    CapturedBindings *captured = raw;
    if (--captured->references != 0u) return;
    free((void *)captured->value.support);
    bindings_free(&captured->bindings);
    arena_free(&captured->syntax);
    free(captured);
}

static Atom *captured_bindings_observe(
        Arena *arena, const CettaBindingsValue *value) {
    const CapturedBindings *captured = (const CapturedBindings *)value;
    return bindings_to_atom(arena, &captured->bindings);
}

static bool captured_bindings_equal(
        const CettaBindingsValue *left, const CettaBindingsValue *right) {
    CapturedBindings *a = (CapturedBindings *)left;
    CapturedBindings *b = (CapturedBindings *)right;
    return bindings_eq(&a->bindings, &b->bindings);
}

Atom *bindings_capture_value(Arena *arena, const Bindings *bindings) {
    CapturedBindings *captured = cetta_malloc(sizeof(*captured));
    *captured = (CapturedBindings){
        .value = {
            .retain = captured_bindings_retain,
            .release = captured_bindings_release,
            .observe = captured_bindings_observe,
            .equal = captured_bindings_equal,
        },
        .references = 1u,
    };
    arena_init_detached(&captured->syntax);
    if (!bindings_clone(&captured->bindings, bindings) ||
        !bindings_promote_atoms_to_arena(&captured->bindings, &captured->syntax)) {
        captured_bindings_release(captured);
        return NULL;
    }
    /* Promotion closes every atom graph in this image over its own arena.
     * Retain nested saved values through that arena, not obsolete ancestor
     * snapshots whose atoms have just been copied. */
    bindings_owners_release(captured->bindings.owners);
    captured->bindings.owners = NULL;
    VarId *support = NULL;
    if (!bindings_collect_support(&captured->bindings, &support,
                                   &captured->value.support_count)) {
        captured_bindings_release(captured);
        return NULL;
    }
    captured->value.support = support;
    Atom *result = atom_bindings_value(arena, &captured->value);
    captured_bindings_release(captured);
    return result;
}

bool bindings_restore_captured_value(const Atom *atom, Bindings *out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BINDINGS || !atom->ground.ptr)
        return false;
    const CapturedBindings *captured = atom->ground.ptr;
    if (!bindings_clone(out, &captured->bindings)) return false;
    bindings_retain_value_owner(out, (CettaBindingsValue *)&captured->value);
    return true;
}

/* A textual key has no authority until it has a unique identity in the
 * receiver. Repeated occurrences of one identity are harmless; two identities
 * with one spelling are ambiguous, even if their current values coincide. */
typedef struct {
    SymbolId spelling;
    VarId id;
    uint32_t epoch;
    bool ambiguous;
} ScopedName;

static bool scoped_name_visit(const Atom *atom, void *raw) {
    ScopedName *name = raw;
    if (atom->kind != ATOM_VAR || atom->name_key || atom->sym_id != name->spelling)
        return false;
    VarId id = name->epoch ? var_epoch_id(atom->var_id, name->epoch) : atom->var_id;
    if (name->id != VAR_ID_NONE && name->id != id) name->ambiguous = true;
    name->id = id;
    return name->ambiguous;
}

static bool scoped_name_resolve(SymbolId spelling, const Atom *scope,
                                const Bindings *receiver, VarId *out) {
    ScopedName name = {.spelling = spelling, .id = VAR_ID_NONE};
    if (scope) atom_tree_any(scope, scoped_name_visit, &name);
    BindingsIterator iterator = {.bindings = receiver};
    Binding binding;
    while (!name.ambiguous && bindings_iterator_next(&iterator, &binding)) {
        if (!binding.name_key && binding.spelling == spelling) {
            if (name.id != VAR_ID_NONE && name.id != binding.var_id) name.ambiguous = true;
            name.id = binding.var_id;
        }
        name.epoch = binding_value_is_contextual(binding.value)
            ? binding.value.epoch : 0u;
        atom_tree_any(binding.value.skeleton, scoped_name_visit, &name);
    }
    if (name.ambiguous || name.id == VAR_ID_NONE) return false;
    *out = name.id;
    return true;
}

bool bindings_from_atom_scoped(Atom *atom, const Atom *scope,
                               const Bindings *receiver, Bindings *out) {
    bindings_init(out);
    if (!atom) return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BINDINGS)
        return bindings_restore_captured_value(atom, out);
    if (atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.bindings)) return false;
    Atom *assigns = atom->expr.elems[1], *equalities = atom->expr.elems[2];
    if (assigns->kind != ATOM_EXPR || equalities->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0; i < assigns->expr.len; i++) {
        Atom *assignment = assigns->expr.elems[i];
        if (assignment->kind != ATOM_EXPR || assignment->expr.len != 2u) goto fail;
        Atom *key = assignment->expr.elems[0], *value = assignment->expr.elems[1];
        if (key->kind == ATOM_VAR) {
            if (!bindings_add_var(out, key, value)) goto fail;
        } else if (key->kind == ATOM_SYMBOL) {
            VarId id;
            if (!scoped_name_resolve(key->sym_id, scope, receiver, &id) ||
                !bindings_add_id(out, id, key->sym_id, value)) goto fail;
        } else goto fail;
    }
    for (CettaExprIndex i = 0; i < equalities->expr.len; i++) {
        Atom *pair = equalities->expr.elems[i];
        if (pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
            !bindings_add_constraint(out, pair->expr.elems[0], pair->expr.elems[1])) goto fail;
    }
    if (bindings_has_loop(out)) goto fail;
    return true;
fail:
    bindings_free(out);
    return false;
}

bool bindings_from_atom(Atom *atom, Bindings *out) {
    return bindings_from_atom_scoped(atom, NULL, NULL, out);
}
