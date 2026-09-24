#ifndef CETTA_TERM_CANON_H
#define CETTA_TERM_CANON_H

#include "atom.h"

/*
 * Shared variable-remapping seam for canonical keys and re-materialization.
 *
 * Positive example:
 *   - TermUniverse and TableStore share one audited recursive walker while
 *     keeping their distinct variable policies.
 *
 * Negative example:
 *   - Two nearly identical VarId map implementations drifting apart in
 *     separate runtime subsystems.
 */

typedef struct {
    VarId source_id;
    Atom *mapped_var;
} CettaVarMapEntry;

typedef struct CettaVarMap {
    CettaVarMapEntry *items;
    uint32_t len;
    uint32_t cap;
} CettaVarMap;

void cetta_var_map_init(CettaVarMap *map);
void cetta_var_map_free(CettaVarMap *map);
bool cetta_var_map_reserve(CettaVarMap *map, uint32_t needed);
Atom *cetta_var_map_lookup(const CettaVarMap *map, VarId source_id);
bool cetta_var_map_add(CettaVarMap *map, VarId source_id, Atom *mapped_var);
bool cetta_var_map_clone(CettaVarMap *dst, const CettaVarMap *src);
bool cetta_var_map_clone_live(Arena *dst, CettaVarMap *out,
                              const CettaVarMap *src);

typedef Atom *(*CettaVarMapCreateFn)(Arena *dst, Atom *src_var,
                                     uint32_t ordinal, void *ctx);
Atom *cetta_var_map_get_or_add(CettaVarMap *map, Arena *dst, Atom *src_var,
                               CettaVarMapCreateFn create_var, void *ctx);

typedef Atom *(*CettaAtomRewriteVarFn)(Arena *dst, Atom *src_var, void *ctx);
Atom *cetta_atom_rewrite_vars(Arena *dst, Atom *src,
                              CettaAtomRewriteVarFn rewrite_var, void *ctx,
                              bool share_immutable);

/* Compile an authored variable inventory into frame-local execution syntax.
 * The map is an import boundary: equal authored identities share one slot;
 * distinct identities remain distinct even when their spellings agree.
 * Reuse the map across all terms belonging to the same frame. */
Atom *cetta_compile_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory);
/* Import a live term: allocate coordinates for authored variables and preserve
 * identities that already belong to another activation. No substitution is applied. */
Atom *cetta_import_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory,
                              CettaFrameIdentity identity);

/* Instantiate authored terms together in a fresh frame. Sharing between the
 * terms is preserved, while different source identities remain different even
 * when their spellings or local slots agree. The returned syntax owns its
 * identity. This creates variables; it does not reify an existing closure. */
bool cetta_instantiate_frame_terms(Arena *dst, Atom **terms, size_t count);
Atom *cetta_instantiate_frame_syntax(Arena *dst, Atom *source);
/* The same instantiation into new slots of a frame identity the caller owns
 * and keeps alive while the terms are in use.  Every call allocates new
 * slots, so terms instantiated by different calls share no variable; a
 * search that instantiates many rules therefore holds one identity rather
 * than one per instantiation. */
bool cetta_instantiate_frame_terms_within(Arena *dst, Atom **terms,
                                          size_t count,
                                          CettaFrameIdentity identity);

#endif /* CETTA_TERM_CANON_H */
