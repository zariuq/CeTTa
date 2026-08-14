#ifndef CETTA_RULE_MACHINE_H
#define CETTA_RULE_MACHINE_H

#include "atom.h"

/*
 * Native realization of the bounded RuleMachineCoreV1 presentation.
 *
 * The public interface consumes and produces ordinary immutable Atoms.  It is
 * deliberately not an arbitrary-MeTTa compiler: rm-package/rm-block values
 * are the admitted source fragment, and compiled-artifact/bc-block values are
 * the inspectable artifact ABI.
 */
Atom *cetta_rule_machine_dispatch(Arena *arena, Atom *head,
                                  Atom **args, uint32_t nargs);

#endif
