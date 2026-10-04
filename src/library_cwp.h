#ifndef CETTA_LIBRARY_CWP_H
#define CETTA_LIBRARY_CWP_H

#include "atom.h"

/*
 * Dispatch the internal operation exported by lib/cwp.metta: one CWP1
 * request to a CeTTa worker endpoint and its reply, over a local Unix
 * seqpacket socket, bounded by a timeout. It is for an agent's own loop
 * talking to its channel service; service reactions never import it.
 */
Atom *cetta_cwp_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);

#endif
