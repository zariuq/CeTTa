#ifndef CETTA_PROC_OWNER_H
#define CETTA_PROC_OWNER_H

#include "library_proc_run.h"

/* One native owner supervises asynchronous children independently of the
 * evaluator. It borrows wake_fd until joined; it never evaluates an Atom. */
typedef struct CettaProcOwner CettaProcOwner;
CettaProcOwner *cetta_proc_owner_new(int wake_fd);
/* Transfers ownership on success only. IDs belong to the enclosing I/O
 * runtime; neither PIDs nor process groups are public request identities. */
bool cetta_proc_owner_add(CettaProcOwner *owner, uint64_t id, CettaProcChild *child);
bool cetta_proc_owner_status(CettaProcOwner *owner, uint64_t id, CettaProcSnapshot *out);
bool cetta_proc_owner_signal(CettaProcOwner *owner, uint64_t id, int number);
/* Public consumption is immediate. Internal child state remains until reap. */
void cetta_proc_owner_abandon(CettaProcOwner *owner, uint64_t id);
void cetta_proc_owner_free(CettaProcOwner *owner);

#endif
