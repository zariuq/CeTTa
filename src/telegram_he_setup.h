#ifndef CETTA_TELEGRAM_HE_SETUP_H
#define CETTA_TELEGRAM_HE_SETUP_H

#include "he_type_policy.h"
#include "space.h"
#include "symbol.h"
#include <stdint.h>
#include <stdlib.h>

/* Service-local copy of main.c's HE library preparation. Keep the upstream
 * declarations and distinct-arity extensions in the same order as the CLI. */
/* The library module &self imports in HE profiles.  Its declarations are
 * upstream's, in upstream's order.  CeTTa's library file keeps its own
 * declarations of names upstream's library does not know; of the others it
 * keeps only, outside the profile with upstream's exact semantics, those
 * adding a call form of an arity upstream does not declare.  A grounded
 * operation's name has no declaration: its type is the operation's own. */
static bool telegram_he_prepare_library(Space *space, Arena *arena,
                                    bool extensions) {
    CettaCount len = space_length64(space);
    uint8_t *mask = calloc(len ? (size_t)len : 1u, 1u);
    AtomId *kept = calloc(len ? (size_t)len : 1u, sizeof(*kept));
    if (!mask || !kept) {
        free(mask);
        free(kept);
        return false;
    }
    CettaCount kept_len = 0u;
    for (CettaIndex i = 0u; i < len; i++) {
        Atom *atom = space_get_at64(space, i);
        if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
            !atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon))
            continue;
        Atom *subject = atom->expr.elems[1];
        Atom *type = atom->expr.elems[2];
        bool grounded = subject->kind == ATOM_SYMBOL &&
            he_grounded_symbol_types(atom_symbol_current_id(subject), extensions, NULL) != 0u;
        if (!grounded && !he_library_names_subject(subject))
            continue;
        mask[i] = 1u;
        if (!grounded && extensions && he_library_declares(subject) &&
            type->kind == ATOM_EXPR && type->expr.len >= 2u &&
            atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow) &&
            !he_library_declares_arity(subject, type->expr.len - 2u))
            kept[kept_len++] = space_get_atom_id_at64(space, i);
    }
    CettaCount removed = 0u;
    bool ok = space_remove_occurrence_mask_stable(space, mask, len, &removed);
    for (size_t i = 0u; ok && i < he_library_declaration_count(); i++) {
        Atom *declaration = he_library_declaration_at(i);
        if (!space_admit_atom(space, arena, declaration))
            space_add(space, declaration);
    }
    for (size_t i = 0u; ok && !extensions && i < he_library_equation_count(); i++) {
        Atom *equation = he_library_equation_at(i);
        if (!equation) {
            ok = false;
            break;
        }
        if (!space_admit_atom(space, arena, equation))
            space_add(space, equation);
    }
    for (CettaCount i = 0u; ok && i < kept_len; i++)
        space_add_atom_id(space, kept[i]);
    free(mask);
    free(kept);
    return ok;
}

#endif /* CETTA_TELEGRAM_HE_SETUP_H */
