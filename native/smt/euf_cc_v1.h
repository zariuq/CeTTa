#ifndef CETTA_EUF_CC_V1_H
#define CETTA_EUF_CC_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Congruence closure over ground terms, with explanations and backtracking.
 *
 * A term is a symbol applied to earlier terms (a constant has no
 * arguments); equal symbols on equal arguments are one term.  Equalities are
 * asserted one at a time, each with a reason (a client's number, such as a
 * literal); the closure merges the two classes and every pair of
 * applications whose arguments have become equal (congruence).  Every merge
 * is one edge of an explanation forest, labelled with its reason or with the
 * congruent pair, so explain(a, b) answers the reasons of the asserted
 * equalities from which a = b follows.  Every change is on an undo stack:
 * euf_undo returns to an earlier height of it.
 *
 * Clients: the solver's EUF theory (DPLL(T)), and anything that needs ground
 * equality reasoning with reasons, as an e-graph does. */

typedef struct EufCC EufCC;

EufCC *euf_new(void);
void euf_free(EufCC *cc);

/* The term `symbol(args...)`, made once; UINT32_MAX when out of memory or an
 * argument is no term. */
uint32_t euf_term(EufCC *cc, uint32_t symbol, const uint32_t *args,
                  uint32_t nargs);
uint32_t euf_terms(const EufCC *cc);

/* Assert a = b with `reason`; false when out of memory. */
bool euf_merge(EufCC *cc, uint32_t a, uint32_t b, uint32_t reason);
bool euf_equal(const EufCC *cc, uint32_t a, uint32_t b);

/* The reasons from which a = b follows (they must be equal), each once,
 * written to *reasons (malloc'd), their number to *count. */
bool euf_explain(EufCC *cc, uint32_t a, uint32_t b, uint32_t **reasons,
                 uint32_t *count);

/* The undo stack's height, and a return to an earlier one. */
uint32_t euf_height(const EufCC *cc);
void euf_undo(EufCC *cc, uint32_t height);

#endif
