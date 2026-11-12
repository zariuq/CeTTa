#ifndef CESTA_LINK_H
#define CESTA_LINK_H

#include "cesta.h"

/* Link records, (cesta link-contracts/1 (unit ...) ...), are written by
   cesta cc --contracts for units checked for ownership. The link check reads
   the records of every object and archive that goes into one program. A call
   that a checked body makes into another unit is matched with the
   definition the program links: it composes when that definition was
   checked under the same ownership contract, C type, callback contracts and
   record layouts. A definition left unchecked, or no Cesta definition at
   all, leaves the call's obligation open; the check lists it. A difference
   from a checked definition is an error, reported at the call; a difference
   from an unchecked one is a warning, and the call rests on its own
   obligation. */
typedef struct {
    size_t units;       /* checked units in the program */
    size_t checked;     /* cross-unit calls that reach a definition checked under their contract */
    size_t native;      /* functions called under a contract with no Cesta definition in the program */
    size_t unchecked;   /* functions whose definitions in the program are left unchecked */
    size_t mismatches;  /* calls whose contract differs from a checked definition's, or other refusals */
    size_t differing;   /* calls whose contract differs from an unchecked definition's */
} CeLinkSummary;

/* With obligations, the program's open obligations are written to that path. */
bool ce_link_check(CeArena *arena, const char *program, char **records, size_t nrecords,
                   const char *obligations, CeLinkSummary *summary, CeError *error);

/* One record for an archive: its members' units, in order. With no records,
   path is removed. */
bool ce_link_merge(CeArena *arena, char **records, size_t nrecords, const char *path, CeError *error);

/* cesta link-check [--obligations FILE] [--program NAME] RECORD... */
int ce_link_command(int argc, char **argv);

#endif
