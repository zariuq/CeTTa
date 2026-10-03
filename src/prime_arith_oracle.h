#ifndef CETTA_PRIME_ARITH_ORACLE_H
#define CETTA_PRIME_ARITH_ORACLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "space.h"

/* Declared arithmetic computed natively, as an oracle with a specification.
 *
 * Arithmetic in Prime is declared: binary numbers are datatypes and the
 * operations are equations admitted by the existing routes.  An oracle lets
 * the runtime compute a closed call of such an operation with its big
 * integers instead, and it places the trust this needs in the open:
 *
 *   (set:numerals space type (kind constructor ...))
 *       reads an admitted datatype as numbers: `positive one bit0 bit1`
 *       (one, 2p, 2p + 1), `natural zero pos`, `integer zero pos neg`,
 *       `fraction over` (z over a positive d), `ordering less same more`,
 *       `quotient-remainder quot-rem`.  The constructors must be those of
 *       the type's admitted declaration, in that order and with those
 *       fields.
 *   (set:oracle space op (equations op) (backend name native))
 *       computes `op` natively.  Its specification is the admitted equations
 *       of `op` and of every definition they call (digested in the record);
 *       `name` is `gmp-mpz` (GMP's integers and rationals, in a build with
 *       GMP) or `uint64` (64-bit words, declining any call whose numbers do
 *       not fit); `native` is add, sub, sub-truncated, mul, compare, divmod,
 *       gcd or normalize, with the signatures over the readings that the
 *       declared type of `op` must have.  Admission checks all of this and
 *       compares the backend with the equations on a few small calls; the
 *       record it publishes is what the runtime reads.
 *
 * A raw record confers nothing, and without a declaration the equations
 * run as written.  Every top-level answer that used an oracle is followed
 * by one trust record per operation:
 *   (uses-oracle op (trusted backend native) (spec-agreement ...) (calls n)
 *                (tests ...))
 * It names the two trusts apart: `(trusted backend native)`, that the
 * backend computes its native operation correctly, and `(spec-agreement
 * (proved theorem ...))` or `(spec-agreement (tested (admission-calls n)))`,
 * that the native operation agrees with the declared equations, proved in
 * Lean or only tested.  `(tests ...)` names the standing tests of both. */

Atom *prime_arith_oracle_judge_numerals(Arena *arena, Space *space,
                                        Atom *judgment);
Atom *prime_arith_oracle_judge_declaration(Arena *arena, Space *space,
                                           Atom *judgment);

/* The native answer for a covered call of a declared operation, or NULL:
 * no admitted oracle names the operation, an argument is not a closed
 * numeral of its reading, or the backend declines. */
Atom *prime_arith_oracle_answer(Arena *arena, Space *space, Atom *call);

/* Trust records of one top-level query. */
void prime_arith_oracle_query_begin(void);
void prime_arith_oracle_query_report(FILE *out);

/* Checking without believing.  The audit replays every oracle answer through
 * the declared equations; the residue check compares sums and products with
 * the arguments modulo three primes, read from the numerals without the
 * backend.  A mismatch is reported once, after the query, and the run
 * stops. */
void prime_arith_oracle_set_audit(bool enabled);
void prime_arith_oracle_set_residues(bool enabled);
bool prime_arith_oracle_check_failed(void);
void prime_arith_oracle_report_failure(FILE *out);
void prime_arith_oracle_report_audit(FILE *out);

#endif
