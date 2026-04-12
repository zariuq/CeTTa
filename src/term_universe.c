#include "term_universe.h"
#include "variant_shape.h"

static bool term_universe_atom_contains_epoch_var(Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return var_epoch_suffix(atom->var_id) != 0;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (term_universe_atom_contains_epoch_var(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src) {
    if (!term_universe_atom_contains_epoch_var(src))
        return atom_deep_copy(dst, src);

    CettaVarMap map;
    cetta_var_map_init(&map);
    static const CettaVariantShapeOptions kTermUniverseVariantOptions = {
        .slot_policy = CETTA_VARIANT_SLOT_SOURCE_SPELLING,
        .slot_name = NULL,
        .share_immutable = true,
    };
    Atom *canonical = variant_shape_canonicalize_atom(dst, src, &map, NULL,
                                                      &kTermUniverseVariantOptions);
    cetta_var_map_free(&map);
    return canonical;
}

Atom *term_universe_store_atom(const TermUniverse *universe, Arena *fallback,
                               Atom *src) {
    Arena *persistent = universe ? universe->persistent_arena : NULL;
    Arena *dst = persistent ? persistent : fallback;
    if (!persistent || dst != persistent)
        return atom_deep_copy(dst, src);
    if (!term_universe_atom_contains_epoch_var(src))
        return atom_deep_copy_shared(dst, src);

    /*
     * Persisted atoms must not retain match-epoch-only variable ids.
     * Canonicalize any epoch-tagged vars so later rematch can safely rebind
     * them without collapsing previously distinct variables.
     */
    return term_universe_canonicalize_atom(dst, src);
}
