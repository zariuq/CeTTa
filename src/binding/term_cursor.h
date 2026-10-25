#ifndef CETTA_BINDING_TERM_CURSOR_H
#define CETTA_BINDING_TERM_CURSOR_H

#include "../match.h"

/* Loan a value's lexical coordinate to one stable structural read. No
 * substitution, allocation or evaluation occurs. A missing frame directory
 * entry defers; the caller must keep its exact matcher or materialize there.
 * The returned scope has the same read lifetime as the observer below. */
CettaGsltTermViewStatusV1 bindings_value_term_cursor_v1(
    const Bindings *bindings, BindingValue value,
    CettaGsltTermCursorV1 *target_out);

/* Structural reads may enter an open subtree in another lexical frame.
 * Returned scopes borrow context->bindings' frame directory for this stable
 * read only: no writes, rollback, collection, directory replacement or owner
 * release may intervene. Children keep their resolved parent's scope.
 * Missing frames and unresolved aliases defer. A cursor grants no permission
 * to evaluate syntax or to retain its scope in another binding image. */
CettaGsltTermViewStatusV1 bindings_resolve_scoped_term_cursor_v1(
    void *context, CettaGsltTermCursorV1 source,
    CettaGsltTermCursorV1 *target_out);

#endif
