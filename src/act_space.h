#ifndef CETTA_ACT_SPACE_H
#define CETTA_ACT_SPACE_H
#include "act_trie.h"
#include "space.h"

/* Capture an immutable upstream ACTree03 set or typed CACT0001 occurrence bag.
 * The initialized empty target becomes an ordinary source-backed Space. */
CettaActStatus cetta_act_space_capture(Space *, const char *path,
                                       size_t maximum_bytes);
CettaActStatus cetta_act_space_dump(Space *, const char *path, bool mork,
                                    size_t maximum_bytes);
/* Validate all incoming rows before publishing one ordinary admission batch.
 * Copying allocates target-domain occurrences; act:open retains saved
 * identities. */
CettaActStatus cetta_act_space_load(Space *, Arena *, const char *path,
                                    size_t maximum_bytes);
#endif
