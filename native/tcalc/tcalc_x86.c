/* tcalc_x86.c - backend x86-flat: a flat firing is decided by the linked
 * runtime image of ordinary_flat_adapter, which packs the selected rows and
 * calls the flat_fire leaf.  The image's symbols are referenced weakly, so a
 * binary built without it still links, and says so. */
#include "tcalc_x86.h"
#include "x86/ordinary_flat_cesta.h"

#include <string.h>

#pragma weak ordinary_flat_adapter

enum { PREMISES = 24, CONSTANTS = 32, OUTPUT = 32, FIELDS = 4 * PREMISES + CONSTANTS };

bool tcalc_x86_available(void) { return ordinary_flat_adapter != NULL; }

uint64_t tcalc_x86_decide(const uint32_t *source, size_t source_words,
                          const uint32_t *premises, size_t npremises,
                          const uint32_t *constants, size_t nconstants,
                          const uint32_t *guards, size_t nguards,
                          const uint32_t *makes, size_t nmakes, uint32_t *out) {
    /* the adapter's workspace: distinct buffers, none of them retained */
    uint32_t fields[FIELDS], rows[PREMISES + 1], dest[OUTPUT], scratch[OUTPUT];
    if (!tcalc_x86_available() || nmakes > OUTPUT) return 2;
    uint64_t verdict = ordinary_flat_adapter(
        (cesta_read_u32_span){source, source_words},
        (cesta_read_u32_span){premises, npremises},
        (cesta_read_u32_span){constants, nconstants},
        (cesta_read_u32_span){guards, nguards},
        (cesta_read_u32_span){makes, nmakes},
        (cesta_write_u32_span){fields, FIELDS},
        (cesta_write_u32_span){rows, PREMISES + 1},
        (cesta_write_u32_span){dest, OUTPUT},
        (cesta_write_u32_span){scratch, OUTPUT});
    if (verdict == 1 && nmakes) memcpy(out, dest, nmakes * sizeof *out);
    return verdict;
}
