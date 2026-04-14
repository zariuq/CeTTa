#include "term_universe.h"
#include "stats.h"
#include "variant_shape.h"

#include <stdlib.h>
#include <string.h>

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

static bool term_universe_atom_is_stable(Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
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
            if (!term_universe_atom_is_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static void term_universe_clear_storage(TermUniverse *universe) {
    if (!universe)
        return;
    free(universe->atoms);
    free(universe->intern_slots);
    free(universe->ptr_slots);
    universe->atoms = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
}

void term_universe_init(TermUniverse *universe) {
    if (!universe)
        return;
    universe->persistent_arena = NULL;
    universe->atoms = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
}

void term_universe_free(TermUniverse *universe) {
    if (!universe)
        return;
    term_universe_clear_storage(universe);
    universe->persistent_arena = NULL;
}

void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena) {
    if (!universe)
        return;
    if (universe->persistent_arena == persistent_arena)
        return;
    term_universe_clear_storage(universe);
    universe->persistent_arena = persistent_arena;
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

static bool term_universe_reserve_atoms(TermUniverse *universe,
                                        uint32_t needed) {
    if (!universe)
        return false;
    if (needed <= universe->cap)
        return true;
    uint32_t next_cap = universe->cap ? universe->cap * 2 : 64;
    while (next_cap < needed)
        next_cap *= 2;
    Atom **next = cetta_realloc(universe->atoms, sizeof(Atom *) * next_cap);
    if (!next)
        return false;
    universe->atoms = next;
    universe->cap = next_cap;
    return true;
}

static bool term_universe_intern_reserve(TermUniverse *universe,
                                         uint32_t min_slots) {
    if (!universe)
        return false;
    if (universe->intern_slots && universe->intern_mask + 1 >= min_slots)
        return true;
    uint32_t size = 1024;
    while (size < min_slots)
        size <<= 1;
    uint32_t *next = cetta_malloc(sizeof(uint32_t) * size);
    if (!next)
        return false;
    memset(next, 0, sizeof(uint32_t) * size);
    uint32_t next_mask = size - 1;
    if (universe->intern_slots) {
        for (uint32_t i = 0; i <= universe->intern_mask; i++) {
            uint32_t slot = universe->intern_slots[i];
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            Atom *atom = universe->atoms[id];
            uint32_t h = atom_hash(atom);
            for (uint32_t probe = 0; probe < size; probe++) {
                uint32_t idx = (h + probe) & next_mask;
                if (next[idx] == 0) {
                    next[idx] = slot;
                    break;
                }
            }
        }
        free(universe->intern_slots);
    }
    universe->intern_slots = next;
    universe->intern_mask = next_mask;
    return true;
}

static uint32_t term_universe_ptr_hash(Atom *atom) {
    uintptr_t bits = (uintptr_t)atom;
    bits ^= bits >> 17;
    bits *= UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11;
    return (uint32_t)bits;
}

static bool term_universe_ptr_reserve(TermUniverse *universe,
                                      uint32_t min_slots) {
    if (!universe)
        return false;
    if (universe->ptr_slots && universe->ptr_mask + 1 >= min_slots)
        return true;
    uint32_t size = 1024;
    while (size < min_slots)
        size <<= 1;
    uint32_t *next = cetta_malloc(sizeof(uint32_t) * size);
    if (!next)
        return false;
    memset(next, 0, sizeof(uint32_t) * size);
    uint32_t next_mask = size - 1;
    if (universe->ptr_slots) {
        for (uint32_t i = 0; i <= universe->ptr_mask; i++) {
            uint32_t slot = universe->ptr_slots[i];
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            Atom *atom = universe->atoms[id];
            uint32_t h = term_universe_ptr_hash(atom);
            for (uint32_t probe = 0; probe < size; probe++) {
                uint32_t idx = (h + probe) & next_mask;
                if (next[idx] == 0) {
                    next[idx] = slot;
                    break;
                }
            }
        }
        free(universe->ptr_slots);
    }
    universe->ptr_slots = next;
    universe->ptr_mask = next_mask;
    return true;
}

static AtomId term_universe_lookup_ptr_id(const TermUniverse *universe,
                                          Atom *src) {
    if (!universe || !universe->ptr_slots || !src)
        return CETTA_ATOM_ID_NONE;
    uint32_t h = term_universe_ptr_hash(src);
    for (uint32_t probe = 0; probe <= universe->ptr_mask; probe++) {
        uint32_t idx = (h + probe) & universe->ptr_mask;
        uint32_t slot = universe->ptr_slots[idx];
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        if (id < universe->len && universe->atoms[id] == src)
            return id;
    }
    return CETTA_ATOM_ID_NONE;
}

static bool term_universe_insert_ptr_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->ptr_slots || id >= universe->len)
        return false;
    Atom *atom = universe->atoms[id];
    uint32_t h = term_universe_ptr_hash(atom);
    for (uint32_t probe = 0; probe <= universe->ptr_mask; probe++) {
        uint32_t idx = (h + probe) & universe->ptr_mask;
        if (universe->ptr_slots[idx] == 0) {
            universe->ptr_slots[idx] = id + 1;
            universe->ptr_used++;
            return true;
        }
    }
    return false;
}

static AtomId term_universe_lookup_stable_id(const TermUniverse *universe,
                                             Atom *src) {
    if (!universe || !universe->intern_slots || !src)
        return CETTA_ATOM_ID_NONE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    uint32_t h = atom_hash(src);
    for (uint32_t probe = 0; probe <= universe->intern_mask; probe++) {
        uint32_t idx = (h + probe) & universe->intern_mask;
        uint32_t slot = universe->intern_slots[idx];
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        if (id < universe->len && atom_eq(universe->atoms[id], src)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_HIT);
            return id;
        }
    }
    return CETTA_ATOM_ID_NONE;
}

static bool term_universe_insert_stable_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->intern_slots || id >= universe->len)
        return false;
    Atom *atom = universe->atoms[id];
    uint32_t h = atom_hash(atom);
    for (uint32_t probe = 0; probe <= universe->intern_mask; probe++) {
        uint32_t idx = (h + probe) & universe->intern_mask;
        if (universe->intern_slots[idx] == 0) {
            universe->intern_slots[idx] = id + 1;
            universe->intern_used++;
            return true;
        }
    }
    return false;
}

static Atom *term_universe_store_persistent_atom(TermUniverse *universe,
                                                 Atom *src) {
    Arena *dst = universe ? universe->persistent_arena : NULL;
    if (!dst || !src)
        return NULL;
    if (term_universe_atom_contains_epoch_var(src))
        return term_universe_canonicalize_atom(dst, src);
    if (term_universe_atom_is_stable(src))
        return atom_deep_copy_shared(dst, src);
    return atom_deep_copy(dst, src);
}

AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src) {
    if (!src)
        return CETTA_ATOM_ID_NONE;
    if (!universe || !universe->persistent_arena) {
        (void)fallback;
        return CETTA_ATOM_ID_NONE;
    }

    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;

    bool stable = term_universe_atom_is_stable(src);
    if (stable) {
        AtomId existing = term_universe_lookup_stable_id(universe, src);
        if (existing != CETTA_ATOM_ID_NONE)
            return existing;
    }

    Atom *stored = term_universe_store_persistent_atom(universe, src);
    if (!stored)
        return CETTA_ATOM_ID_NONE;
    if (!term_universe_reserve_atoms(universe, universe->len + 1))
        return CETTA_ATOM_ID_NONE;

    AtomId id = universe->len++;
    universe->atoms[id] = stored;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT);

    uint32_t ptr_needed = universe->ptr_slots ? (universe->ptr_mask + 1) : 0;
    if (ptr_needed == 0 || (universe->ptr_used + 1) * 10 > ptr_needed * 7) {
        if (!term_universe_ptr_reserve(universe, ptr_needed ? ptr_needed * 2 : 1024))
            return id;
    }
    (void)term_universe_insert_ptr_id(universe, id);

    if (stable) {
        uint32_t needed =
            universe->intern_slots ? (universe->intern_mask + 1) : 0;
        if (needed == 0 || (universe->intern_used + 1) * 10 > needed * 7) {
            if (!term_universe_intern_reserve(
                    universe, needed ? needed * 2 : 1024)) {
                return id;
            }
        }
        (void)term_universe_insert_stable_id(universe, id);
    }
    return id;
}

Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id) {
    if (!universe || id == CETTA_ATOM_ID_NONE || id >= universe->len)
        return NULL;
    return universe->atoms[id];
}

Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src) {
    Arena *persistent = universe ? universe->persistent_arena : NULL;
    if (!persistent)
        return atom_deep_copy(fallback, src);
    AtomId id = term_universe_store_atom_id(universe, fallback, src);
    Atom *stored = term_universe_get_atom(universe, id);
    return stored ? stored : term_universe_store_persistent_atom(universe, src);
}
