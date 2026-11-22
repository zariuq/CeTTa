#ifndef CETTA_TRANSFORM_TCALC_H
#define CETTA_TRANSFORM_TCALC_H

#include "transform_core.h"

/* Two carrier instances of the shared declared firing protocol: lazy
 * first-order terms and a same-call host frame with current occurrence
 * receipts. Checked execution compares both before the host commits.
 * Publications retain their complete original values after agreement. */
CettaTransformOutcome cetta_transform_tcalc_decide(void *context, Arena *arena,
    const CettaTransformFiring *firing, Atom **error);
void cetta_transform_tcalc_cache_free_for_current_thread(void);

#endif
