#ifndef TCALC_NATIVE_TCALC_VIEW_H
#define TCALC_NATIVE_TCALC_VIEW_H
#include "native_match_view.h"
#include "tcalc_core.h"

/* Optional adapter for CeTTa's first-order tcalc term table. The table and
 * every id through last_term must be live and stable throughout an import.
 * last_term is a live high-water id supplied by the table's owner; this
 * adapter does not inspect the private TcTerms layout. Values retain the
 * original TcTerm and context. Locals use that context as their activation
 * scope; state unknowns use state_scope, independently of activation scope.
 *
 * This is a read adapter with an empty external substitution. Publication
 * back into a tcalc state additionally needs its ordinary closing operation
 * for unbound rule locals. It is not supplied or implied by this view. */
typedef struct {
    const TcTerms *terms;
    TcTerm last_term;
    uint64_t state_scope;
} NmTcalcTermView;

NmTermView nm_tcalc_term_view(NmTcalcTermView *source);

#endif
