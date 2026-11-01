#ifndef CETTA_SPACE_SOURCE_OVERLAY_H
#define CETTA_SPACE_SOURCE_OVERLAY_H

#include "space.h"

/* A local transaction workspace over an immutable occurrence source. On
 * success it owns owned_base (including its malloc-allocated Space); failure
 * leaves both arguments unchanged. target is initialized, empty and native.
 * Base identities survive unchanged. Added identities are explicitly local,
 * not durable references. No publication or write authority is implied.
 *
 * Opening a workspace reads no base rows. Matching retains base selection
 * and scans only the local additions. Cursors capture their own delta view.
 * Standard add/remove use the ordinary bag and exact/unique-alpha contracts.
 * Positional mutation, executable catalogs and nonportable values refuse.
 * The owner is edited through the usual serialized Space mutation boundary.
 */
bool space_source_overlay_init_owned(Space *target, Space *owned_base,
    size_t maximum_bytes);

/* Copy the net local changes, not the database, into the receiving arena.
 * Each list is a data expression. removals contains base occurrence identities;
 * additions contains atoms in admission order. Publishing these is the
 * existing durable owner's responsibility. False is never an empty delta. */
bool space_source_overlay_changes(const Space *space, Arena *arena,
    Atom **additions, Atom **removals);

#endif
