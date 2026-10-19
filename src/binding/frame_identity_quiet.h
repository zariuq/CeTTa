/* frame_identity_quiet.h — acquiring a frame identity where a refusal is
 * neutral.
 *
 * A refused acquisition is counted (cetta_frame_identity_exhaustions), and a
 * machine that sees the count move during its run reports the run as cut
 * short by capacity.  Where the caller has a complete fallback for a refusal
 * (a pre-check that keeps its candidate, a cursor whose caller evaluates the
 * reference way), the refusal loses nothing, so it is not counted. */
#ifndef CETTA_FRAME_IDENTITY_QUIET_H
#define CETTA_FRAME_IDENTITY_QUIET_H

#include "frame_identity.h"

bool cetta_frame_identity_acquire_quiet(CettaFrameIdentity *identity_out);
bool cetta_frame_identity_scope_try_quiet(CettaFrameIdentityScope *scope,
                                          CettaFrameIdentity *identity_out);

#endif
