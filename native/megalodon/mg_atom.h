#ifndef CETTA_MEGALODON_ATOM_H
#define CETTA_MEGALODON_ATOM_H

/*
 * Ordinary MeTTa data <-> kernel syntax (NativeSyntaxWire / NativeCursorWire
 * over CeTTa atoms).
 *
 * Decoding is iterative (input depth uses no C stack) and memoized on atom
 * identity, so a DAG-shaped atom decodes in time linear in its distinct
 * subterms.  It builds untrusted syntax in a theory's store (the caller
 * holds the theory lock); it never grants authority, and the kernel's
 * guards (Protected, scopes, rules) still decide everything.  A failure is
 * MG_MALFORMED for data that is not wire syntax, MG_NOMEM / MG_CAPACITY for
 * resource stops.
 *
 * Encoding is iterative and shares the atom of a shared kernel node.
 */

#include "atom.h"
#include "mg_core.h"
#include "mg_session.h"

MgTp *mg_atom_tp(MgStore *s, Atom *atom, MgStatus *status);
MgTm *mg_atom_tm(MgStore *s, Atom *atom, MgStatus *status);
MgPf *mg_atom_pf(MgStore *s, Atom *atom, MgStatus *status);
MgCtx *mg_atom_context(MgStore *s, Atom *atom, MgStatus *status);
bool mg_atom_judgment(MgStore *s, Atom *atom, MgJudgment *out,
                      MgStatus *status);
bool mg_atom_witness(MgStore *s, Atom *atom, MgWitness *out,
                     MgStatus *status);
/* (Megalodon.row JUDGMENT WITNESS (parents A ..) ORIGIN).  Parents go to
 * *parents (grown with mg_sys_realloc, owned by the caller). */
bool mg_atom_row(MgStore *s, Atom *atom, MgRow *out, uint64_t **parents,
                 uint32_t *parents_cap, MgStatus *status);
/* A natural number atom (GV_INT >= 0). */
bool mg_atom_natural(const Atom *atom, uint64_t *out);
/* The text of a string or symbol atom (borrowed), or NULL. */
const char *mg_atom_text(const Atom *atom, size_t *len);

Atom *mg_atom_of_tp(Arena *a, const MgTp *t);
Atom *mg_atom_of_tm(Arena *a, const MgStore *s, const MgTm *m);
Atom *mg_atom_of_pf(Arena *a, const MgStore *s, const MgPf *p);
Atom *mg_atom_of_context(Arena *a, const MgStore *s, const MgCtx *ctx);
Atom *mg_atom_of_judgment(Arena *a, const MgStore *s, MgJudgment j);
Atom *mg_atom_of_witness(Arena *a, const MgStore *s, const MgWitness *w);
Atom *mg_atom_of_row(Arena *a, const MgStore *s, const MgRow *row);

#endif /* CETTA_MEGALODON_ATOM_H */
