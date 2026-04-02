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

typedef struct {
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

typedef Atom *(*CettaVarMapCreateFn)(Arena *dst, Atom *src_var,
                                     uint32_t ordinal, void *ctx);
Atom *cetta_var_map_get_or_add(CettaVarMap *map, Arena *dst, Atom *src_var,
                               CettaVarMapCreateFn create_var, void *ctx);

typedef Atom *(*CettaAtomRewriteVarFn)(Arena *dst, Atom *src_var, void *ctx);
/*
 * Rebuilt expressions follow the destination arena's own hash-cons policy via
 * the normal constructors. `share_immutable` additionally allows callers with
 * long-lived/global storage policy to share immutable leaves more aggressively.
 */
Atom *cetta_atom_rewrite_vars(Arena *dst, Atom *src,
                              CettaAtomRewriteVarFn rewrite_var, void *ctx,
                              bool share_immutable);

#endif /* CETTA_TERM_CANON_H */
