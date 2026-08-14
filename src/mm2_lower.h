#ifndef CETTA_MM2_LOWER_H
#define CETTA_MM2_LOWER_H

#include "atom.h"
#include "term_universe.h"

/* Lower raw MM2 syntax forms into inert internal IR before ordinary HE
   evaluation sees them. The lowering happens in-place on the parsed top-level
   atom array; returned atoms live in the provided arena. */
void cetta_mm2_lower_atoms(Arena *a, Atom **atoms, int n);

/* MM2 program rules are the atoms that belong in the executable program lane. */
bool cetta_mm2_atom_is_exec_rule(Atom *atom);

/* MM2 files are raw syntax syntax, not HE scripts. */
bool cetta_mm2_atoms_have_top_level_eval(Atom **atoms, int n);

/* Raise one raw or lowered MM2 atom back to executable MM2 syntax syntax.
   This is the inverse bridge used by the MORK runtime seam: lowered IR and
   already-syntax MM2 terms both round-trip through one honest serializer. */
Atom *cetta_mm2_raise_atom(Arena *a, Atom *atom);

/* Render one raw or lowered MM2 atom as syntax MM2 S-expression text. */
char *cetta_mm2_atom_to_syntax_string(Arena *a, Atom *atom);

/* Encode one raw or lowered MM2 atom as stable MORK bridge expr bytes.
   This keeps the CeTTa<->MORK mutation boundary below UTF-8 syntax text while
   preserving the same raised/MM2-visible term shape as the text path.

   Object-binder ABTs cross this boundary only after scope admission in closed
   canonical form.  Bridge variable slots represent CeTTa metavariables, not
   de Bruijn indices; the bridge therefore remains binder-neutral. */
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

/* Pack canonical, ground AtomIds as a sequence of compact bridge expressions:

     repeated { u32 expr_len_be; u8 expr_bytes[expr_len] }

   This is the low-level transaction payload consumed by counted PathMap batch
   mutation.  It intentionally declines terms that need target-local packet
   normalization (for example, over-wide symbols) so the caller can preserve
   semantics through the established singular packet path. */
bool cetta_mm2_atom_ids_to_bridge_expr_bytes_batch(
    Arena *a, const TermUniverse *universe, const AtomId *atom_ids,
    CettaCount atom_count, uint8_t **out_packet, size_t *out_packet_len,
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

/* Length-delimited bridge packets preserve symbols of arbitrary byte length.
   The target MORK space normalizes these packets into its local compact
   expression representation, interning long symbols in that space's symbol
   table. */
bool cetta_mm2_atom_to_bridge_expr_packet(Arena *a, Atom *atom,
                                          uint8_t **out_packet,
                                          size_t *out_len,
                                          const char **out_error);
bool cetta_mm2_atom_to_contextual_bridge_expr_packet(
    Arena *a, Atom *atom, uint8_t **out_packet, size_t *out_packet_len,
    uint8_t **out_context_bytes, size_t *out_context_len,
    const char **out_error);
bool cetta_mm2_atom_id_to_bridge_expr_packet(
    Arena *a, const TermUniverse *universe, AtomId atom_id,
    uint8_t **out_packet, size_t *out_len, const char **out_error);
bool cetta_mm2_atom_id_to_contextual_bridge_expr_packet(
    Arena *a, const TermUniverse *universe, AtomId atom_id,
    uint8_t **out_packet, size_t *out_packet_len,
    uint8_t **out_context_bytes, size_t *out_context_len,
    const char **out_error);

/* Decode one stable, length-delimited MORK bridge expression into a CeTTa
   atom. Variable slots are expression-local and receive deterministic
   presentation names v0, v1, ... while preserving co-reference. */
bool cetta_mm2_bridge_expr_packet_to_atom(Arena *a,
                                          const uint8_t *packet,
                                          size_t packet_len,
                                          Atom **out_atom,
                                          const char **out_error);

/* Canonicalize only the presentation of variables in one MM2 atom. This is
   the encode/decode round trip used to make reference and physical observers
   agree without rerunning either semantics. */
Atom *cetta_mm2_alpha_canonicalize_atom(Arena *a, Atom *atom,
                                        const char **out_error);

/* Project one atom to MM2's observable syntax representation. Variables are
   alpha-canonicalized and f64 tokens use the shortest round-tripping spelling
   selected by Rust's Debug contract. The latter is part of MORK's pure-sink
   behavior, not CeTTa's language-independent grounded-value printer. */
Atom *cetta_mm2_canonical_syntax_atom(Arena *a, Atom *atom,
                                       const char **out_error);

#endif /* CETTA_MM2_LOWER_H */
