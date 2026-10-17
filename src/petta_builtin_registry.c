#include "petta_builtin_registry.h"

#include <string.h>

#define GRADE_det PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC
#define GRADE_semidet PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC
#define GRADE_nondet PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC
#define GRADE_unspecified PETTA_ANALYSIS_CARDINALITY_UNDETERMINED
#define EAGER_eager true
#define EAGER_special false
#define SIGNATURE_signature true
#define SIGNATURE_untyped false
#define SIGNATURE_contextual false
#define PETTA_BUILTIN(ordinal, name, arity, implementation, evaluation, typing, grade, a, b, out, contextual, rule, flat, lowering) \
    [ordinal] = {name, arity, ordinal, #implementation, EAGER_##evaluation, SIGNATURE_##typing, \
     GRADE_##grade, {PETTA_BUILTIN_TYPE_##a, PETTA_BUILTIN_TYPE_##b}, \
     PETTA_BUILTIN_TYPE_##out, contextual, PETTA_BUILTIN_CARD_##rule, \
     GRADE_##flat, lowering},
static const PettaBuiltinSpec builtin_specs[] = {
#include "petta_builtin_registry.def"
};
#undef PETTA_BUILTIN

/* Publication follows upstream declaration order; only lookup is sorted. */
#define PETTA_BUILTIN(ordinal, ...) &builtin_specs[ordinal],
static const PettaBuiltinSpec *const builtin_lookup[] = {
#include "petta_builtin_registry.def"
};
#undef PETTA_BUILTIN

size_t petta_builtin_spec_count(void) {
    return sizeof(builtin_specs) / sizeof(builtin_specs[0]);
}

const PettaBuiltinSpec *petta_builtin_spec_at(size_t index) {
    return index < petta_builtin_spec_count() ? &builtin_specs[index] : NULL;
}

const PettaBuiltinSpec *petta_builtin_spec_find(const char *name, uint16_t arity) {
    if (!name)
        return NULL;
    /* The table is sorted by name and arity. */
    size_t first = 0u, end = petta_builtin_spec_count();
    while (first < end) {
        size_t middle = first + (end - first) / 2u;
        const PettaBuiltinSpec *spec = builtin_lookup[middle];
        int order = strcmp(spec->name, name);
        if (order < 0 || (order == 0 && spec->arity < arity))
            first = middle + 1u;
        else
            end = middle;
    }
    if (first < petta_builtin_spec_count() &&
        builtin_lookup[first]->arity == arity &&
        strcmp(builtin_lookup[first]->name, name) == 0)
        return builtin_lookup[first];
    return NULL;
}

static bool literal_boolean(Atom *atom) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_BOOL;
    if (atom->kind != ATOM_SYMBOL)
        return false;
    const char *name = atom_name_cstr(atom);
    return strcmp(name, "true") == 0 || strcmp(name, "false") == 0 ||
        strcmp(name, "True") == 0 || strcmp(name, "False") == 0;
}

PettaAnalysisCardinality petta_builtin_literal_cardinality(
    const PettaBuiltinSpec *spec, Atom *const *arguments, size_t arity) {
    if (!spec || arity != spec->arity || (arity != 0u && !arguments))
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    if (spec->cardinality_rule == PETTA_BUILTIN_CARD_MANIFEST_BOOLEANS) {
        for (size_t index = 0u; index < arity; index++)
            if (!literal_boolean(arguments[index]))
                return spec->flat_cardinality;
        return PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
    }
    return spec->flat_cardinality;
}

static Atom *builtin_type(Arena *arena, PettaBuiltinType type, Atom **variables) {
    switch (type) {
    case PETTA_BUILTIN_TYPE_NUMBER: return atom_symbol(arena, "Number");
    case PETTA_BUILTIN_TYPE_BOOL: return atom_symbol(arena, "Bool");
    case PETTA_BUILTIN_TYPE_STRING: return atom_symbol(arena, "String");
    case PETTA_BUILTIN_TYPE_UNDEFINED: return atom_symbol(arena, "%Undefined%");
    case PETTA_BUILTIN_TYPE_VAR0:
    case PETTA_BUILTIN_TYPE_VAR1: {
        size_t index = type == PETTA_BUILTIN_TYPE_VAR0 ? 0u : 1u;
        if (!variables[index])
            variables[index] = atom_var_with_id(
                arena, index == 0u ? "a" : "b", fresh_var_id());
        return variables[index];
    }
    case PETTA_BUILTIN_TYPE_LIST_VAR0:
    case PETTA_BUILTIN_TYPE_LIST_UNDEFINED: {
        Atom *element = builtin_type(arena,
            type == PETTA_BUILTIN_TYPE_LIST_VAR0
                ? PETTA_BUILTIN_TYPE_VAR0 : PETTA_BUILTIN_TYPE_UNDEFINED,
            variables);
        return atom_expr2(arena, atom_symbol(arena, "List"), element);
    }
    case PETTA_BUILTIN_TYPE_NONE: return NULL;
    }
    return NULL;
}

Atom *petta_builtin_signature(Arena *arena, const PettaBuiltinSpec *spec) {
    if (!arena || !spec || !spec->has_signature || spec->arity > 2u)
        return NULL;
    const char *arrow = spec->declared_grade == PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC
        ? "-[det]->" : spec->declared_grade == PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC
        ? "-[semidet]->" : spec->declared_grade == PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC
        ? "-[nondet]->" : "->";
    Atom *variables[2] = {NULL, NULL};
    Atom *parts[4] = {atom_symbol(arena, arrow)};
    for (size_t index = 0u; index < spec->arity; index++)
        parts[index + 1u] = builtin_type(arena, spec->arguments[index], variables);
    parts[spec->arity + 1u] = builtin_type(arena, spec->result, variables);
    for (size_t index = 0u; index < spec->arity + 2u; index++)
        if (!parts[index])
            return NULL;
    return atom_expr(arena, parts, spec->arity + 2u);
}
