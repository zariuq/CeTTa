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

/* Whether admission published `record` in the space that `space` is or
 * views.  Kernel computation trusts no rule and no definition record that
 * admission did not publish. */
bool prime_scoped_judgment_admitted(Arena *a, Space *space, Atom *record);

#endif /* CETTA_PRIME_SCOPED_JUDGMENTS_H */
