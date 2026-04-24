#ifndef CETTA_MM2_LOWER_H
#define CETTA_MM2_LOWER_H

#include "atom.h"
#include "term_universe.h"

/* Lower raw MM2 surface forms into inert internal IR before ordinary HE
   evaluation sees them. The lowering happens in-place on the parsed top-level
   atom array; returned atoms live in the provided arena. */
void cetta_mm2_lower_atoms(Arena *a, Atom **atoms, int n);

/* MM2 program rules are the atoms that belong in the executable program lane. */
bool cetta_mm2_atom_is_exec_rule(Atom *atom);

/* MM2 files are raw surface syntax, not HE scripts. */
bool cetta_mm2_atoms_have_top_level_eval(Atom **atoms, int n);

/* Raise one raw or lowered MM2 atom back to executable MM2 surface syntax.
   This is the inverse bridge used by the MORK runtime seam: lowered IR and
   already-surface MM2 terms both round-trip through one honest serializer. */
Atom *cetta_mm2_raise_atom(Arena *a, Atom *atom);

/* Render one raw or lowered MM2 atom as surface MM2 S-expression text. */
char *cetta_mm2_atom_to_surface_string(Arena *a, Atom *atom);

/* Encode one raw or lowered MM2 atom as stable MORK bridge expr bytes.
   This keeps the CeTTa<->MORK mutation boundary below UTF-8 surface text while
   preserving the same raised/MM2-visible term shape as the text path. */
bool cetta_mm2_atom_to_bridge_expr_bytes(Arena *a, Atom *atom,
                                         uint8_t **out_bytes,
                                         size_t *out_len,
                                         const char **out_error);

/* Encode one raw or lowered transient atom as structural bridge expr bytes plus
   an exact opening context. This is the Atom* sibling of the AtomId encoder,
   used when a query projection is removed before it has canonical AtomId
   ownership. */
bool cetta_mm2_atom_to_contextual_bridge_expr_bytes(Arena *a, Atom *atom,
                                                    uint8_t **out_expr_bytes,
                                                    size_t *out_expr_len,
                                                    uint8_t **out_context_bytes,
                                                    size_t *out_context_len,
                                                    const char **out_error);

/* Encode one stored canonical term directly as stable MORK bridge expr bytes.
   This keeps MORK bridge transport on AtomId ownership instead of decoding
   back through transient Atom* trees first. */
bool cetta_mm2_atom_id_to_bridge_expr_bytes(Arena *a,
                                            const TermUniverse *universe,
                                            AtomId atom_id,
                                            uint8_t **out_bytes,
                                            size_t *out_len,
                                            const char **out_error);

/* Encode one stored canonical term as structural bridge expr bytes plus an
   exact opening context. The context maps each local bridge variable slot back
   to the canonical CeTTa VarId/spelling carried by the term universe. */
bool cetta_mm2_atom_id_to_contextual_bridge_expr_bytes(Arena *a,
                                                       const TermUniverse *universe,
                                                       AtomId atom_id,
                                                       uint8_t **out_expr_bytes,
                                                       size_t *out_expr_len,
                                                       uint8_t **out_context_bytes,
                                                       size_t *out_context_len,
                                                       const char **out_error);

#endif /* CETTA_MM2_LOWER_H */
