#ifndef CETTA_SUPPORT_V1_H
#define CETTA_SUPPORT_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Well-founded support by source pointers.
 *
 * Atoms are 0..natoms-1.  A justification has a head atom and body atoms; it
 * is usable when its client says so (support_enabled_fn).  An atom is
 * supported when some usable justification of it has every body atom
 * supported, the supports well founded: no atom rests on itself through a
 * cycle.  The core keeps, per atom, a source: a usable justification whose
 * body atoms have sources of their own, assigned in a well-founded order, so
 * the atoms with sources are exactly the supported ones once every lost
 * source has been reported.
 *
 * A client reports a justification that stopped being usable
 * (support_lose): the atoms resting on it lose their sources, and so do the
 * atoms resting on those.  support_recheck then gives sources again, from
 * the atoms that kept theirs, to the atoms that lost them, except the ones
 * the client excludes (support_excluded_fn: an answer-set solver's false
 * atoms); it answers the atoms it looked at that are left without one.  An
 * excluded atom without a source waits until the client reopens it.
 *
 * Clients: the answer-set solver's unfounded sets (an atom's rules as its
 * justifications, a rule usable while its body is not false), and maintained
 * derived spaces (rule instances as justifications, base facts as
 * justifications without a body). */

typedef struct SupportGraph SupportGraph;

typedef bool (*support_enabled_fn)(void *context, uint32_t justification);
typedef bool (*support_excluded_fn)(void *context, uint32_t atom);

SupportGraph *support_new(uint32_t natoms);
void support_free(SupportGraph *g);

/* Room for atoms up to natoms - 1; new atoms start without a source and are
 * looked at by the next recheck. */
bool support_grow(SupportGraph *g, uint32_t natoms);
uint32_t support_atoms(const SupportGraph *g);

/* A justification of `head` from the body atoms (repeats allowed); its id,
 * or UINT32_MAX when out of memory.  Its head is looked at by the next
 * recheck when it has no source. */
uint32_t support_add(SupportGraph *g, uint32_t head, const uint32_t *body,
                     uint32_t nbody);

/* Justification j stopped being usable: the sources resting on it are lost. */
bool support_lose(SupportGraph *g, uint32_t j);

/* Atom a, excluded until now, is open again: the next recheck looks at it if
 * it has no source. */
bool support_reopen(SupportGraph *g, uint32_t a);

/* Sources for the atoms that lost them, from the atoms that kept theirs.
 * The atoms looked at that remain without a source and are not excluded are
 * written to *unsupported (malloc'd, ascending), their number to *count. */
bool support_recheck(SupportGraph *g, support_enabled_fn enabled,
                     support_excluded_fn excluded, void *context,
                     uint32_t **unsupported, uint32_t *count);

bool support_has_source(const SupportGraph *g, uint32_t a);
/* The justification that is a's source, or UINT32_MAX. */
uint32_t support_source(const SupportGraph *g, uint32_t a);

#endif
