#ifndef CETTA_PRIME_SCOPED_JUDGMENTS_H
#define CETTA_PRIME_SCOPED_JUDGMENTS_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"
#include "symbol.h"

/* Draft scoped judgment bubbles beside the existing `type:` vocabulary.
 *
 * `set:`  — simple-typed judgments and proposition proofs over an admitted
 *           set-theoretic signature.  Terms and types are the same authored
 *           syntax that `type:` accepts; every `set:` term judgment is the
 *           corresponding `type:` judgment in an environment extended by the
 *           signature, guarded by a syntactic descent check that the operands
 *           lie in the constant-family (simple) fragment.  `set:proves` checks
 *           a proof term against a proposition of type `prop` bidirectionally.
 * `lang:` — language-level operations: registered language inventory,
 *           parsing and printing of source text, and the diamond native type
 *           over Prime's own evaluation.
 * `try`   — one evaluation of a judgment with its outcome made explicit:
 *           `(Established e)`, `(Refuted r)`, `(Undetermined r)` or
 *           `(Incomplete r)`.  Nothing is stored and nothing is re-run: a
 *           plain judgment yields True, False or stays inert; `try` is the
 *           same single evaluation with the diagnostic returned instead.
 *
 * Ownership is by exact symbol: only these spellings are judgments.  Any
 * other `set:`- or `lang:`-prefixed symbol is ordinary vocabulary. */

bool prime_scoped_judgment_is_head_id(SymbolId head);

/* Returns NULL when the judgment head is not owned by this module. */
Atom *prime_scoped_judgment_judge(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

/* Spellings of the declaration identities that proof packages generate.  No
 * program declares them: admission refuses them and declaration lookup does
 * not see them, so a printed spelling confers no authority. */
#define CETTA_PRIME_PROOF_IDENTITY_PREFIX "__cetta_proof_"
#define CETTA_PRIME_HOLDS_IDENTITY_PREFIX "__cetta_holds_"
bool prime_scoped_judgment_reserved_name(Atom *name);

/* Admission of a record that the evaluator has just published. */
void prime_scoped_judgment_admit(Arena *a, Space *space, Atom *record);

/* Keep what was published in a space, admitted records and theorem records,
 * with the space's contents when another space comes to hold them. */
void prime_scoped_judgment_follow_space_contents(void);

/* Whether admission published `record` in the space that `space` is or
 * views.  Kernel computation trusts no rule and no definition record that
 * admission did not publish. */
bool prime_scoped_judgment_admitted(Arena *a, Space *space, Atom *record);

/* A number that changes whenever what is admitted may have changed. */
uint64_t prime_scoped_judgment_admission_epoch(void);

/* What a refutation by the conversion algorithm of `left` and `right`
 * assumes in `space`: `(assumes-completeness kind ...)`, the kinds of
 * declaration the comparison meets for which Lean has not proved the
 * algorithm complete, or NULL when it meets none. */
Atom *prime_scoped_judgment_completeness_assumed(Arena *a, Space *space, Atom *left,
                                                 Atom *right);

/* The Lean theorem a refutation of `left` against `right` rests on without
 * assuming the conversion algorithm complete, or NULL: the empty list
 * against `cons` of closed constructor terms, in a space whose numbers and
 * lists are declared as Lean declares them, rests on
 * `nil_not_equal_cons`. */
const char *prime_scoped_judgment_refutation_without_completeness(
    Arena *a, Space *space, Atom *left, Atom *right);

/* The typed account of a query.  `term` is `(match space pattern returned)`
 * against a space that declares the pattern's functor typed with
 * `(type:stored (F T1 ... Tn))`, or a call `(f k)` of a function whose
 * stored rules `space` declares typed with `(type:stored (= (f K) T))`; the
 * verdict's evidence is
 * `(PrimeTypedQuery answer-type (Answers (Answer answer value) ...)
 * (NotAnswers (NotAnswer occurrence key) ...))`, the answer type
 * `Sigma (o : F@occurrence). Id Tp (F@argp o) k` in the authored spelling.
 * NULL for any other term, which the other routes judge. */
Atom *prime_scoped_typed_query_judge(Arena *a, Space *space, Atom *judgment,
                                     Atom *term, bool limited, uint64_t steps);

/* The space an ordinary `type:` judgment reads: `space` itself, or, where
 * it declares stored atoms typed with `(type:stored (F T1 ... Tn))`, the
 * space extended with what they give: the type `F@occurrence`, one
 * occurrence `F@x1@...@xn` for each stored atom (`#k` after the k-th copy
 * of one atom, from 2), and the argument maps `F@argi` with one equation
 * for each stored atom.  The extension is rebuilt when the space changes. */
Space *prime_scoped_stored_space(Arena *a, Space *space);

#endif /* CETTA_PRIME_SCOPED_JUDGMENTS_H */
