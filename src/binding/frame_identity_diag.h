#ifndef CETTA_BINDING_FRAME_IDENTITY_DIAG_H
#define CETTA_BINDING_FRAME_IDENTITY_DIAG_H

#include <stdint.h>

/* Refuse every acquisition from the given one on, counted from process
 * start and from one, as exhaustion refuses it: a sweep over the count
 * meets a refusal at each place that mints.  Zero, the default, refuses
 * none.  Diagnostic only; the runtime never calls it on its own. */
void cetta_frame_identity_refuse_from(uint64_t acquisition);

#endif
