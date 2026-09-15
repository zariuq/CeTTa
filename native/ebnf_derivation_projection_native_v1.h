#ifndef CETTA_EBNF_DERIVATION_PROJECTION_NATIVE_V1_H
#define CETTA_EBNF_DERIVATION_PROJECTION_NATIVE_V1_H

#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>

/* Native realization of the EBNF derivation projection.
 *
 * The operation is generic over receipt data: it reads the lowered grammar
 * document, the helper-origin receipt, the grammar authority, and the
 * parser's CST tuple as plain atoms, and emits the public EBNF derivation
 * described by the authored transformation in
 * langdef/bnf/ebnf_derivation_projection_v1.metta, which remains the
 * independently executable oracle. Nothing here is specialized to a
 * particular grammar family, rule name set, arity, or input size.
 *
 * Grammar metadata (rule definitions with their alternative labels,
 * lexical declarations, helper origins) is prepared once into first-binding
 * lookup tables, then consulted while walking every CST node. Output atoms
 * preserve derivations, spans, order, multiplicity, and the private
 * reversed-iteration discipline of the authored projection exactly.
 *
 * All state lives for the duration of this call only: lookup tables are
 * freed on return and the emitted atoms share subatoms with the receipt
 * where the authored transformation produced structurally equal data.
 */
bool cetta_ebnf_derivation_projection_native_v1(
    const Atom *document,
    const Atom *origins,
    const Atom *authority,
    const Atom *trees,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

#endif
