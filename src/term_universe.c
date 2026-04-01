#include "term_universe.h"
#include "term_canon.h"

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

static Atom *term_universe_make_canonical_var(Arena *dst, Atom *src_var,
                                              uint32_t ordinal, void *ctx) {
    (void)ctx;
    VarId canonical_id = ((VarId)ordinal << 32) | (VarId)ordinal;
    return atom_var_with_spelling(dst, src_var->sym_id, canonical_id);
}

static Atom *term_universe_rewrite_var(Arena *dst, Atom *src_var, void *ctx) {
    CettaVarMap *map = ctx;
    return cetta_var_map_get_or_add(map, dst, src_var,
                                    term_universe_make_canonical_var, NULL);
}

Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src) {
    if (!term_universe_atom_contains_epoch_var(src))
        return atom_deep_copy(dst, src);

    CettaVarMap map;
    cetta_var_map_init(&map);
    Atom *canonical = cetta_atom_rewrite_vars(dst, src,
                                              term_universe_rewrite_var, &map,
                                              true);
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
