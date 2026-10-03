#ifndef CETTA_EVAL_HE_BOUNDARY_H
#define CETTA_EVAL_HE_BOUNDARY_H

#include "eval.h"

/* Private interface to eval_he_boundary.inc, included only by eval.c.
 *
 * HE specification layers:
 *   - he_type_policy selects source demand and checks call applicability;
 *   - raw invocation obtains ordered (atom, bindings) occurrences;
 *   - minimal instructions compose those occurrences without the full
 *     interpreter's return-demand continuation;
 *   - typed completion resumes full interpretation under the saved codomain.
 *
 * The implementation shares the evaluator's existing services: equation
 * queries and native/foreign dispatch, prepared execution, registry/profile
 * lookup, bindings and ordered outcomes, full interpretation, GC root frames
 * and outcome suspensions, and completion/fuel/unwinding checks. These stay
 * owned by the evaluator; this boundary creates no second execution context.
 * Shared completed-native-result publication remains outside this module.
 *
 * Inputs and prefix bindings are borrowed. The implementation copies the
 * prefix, protects live terms and intermediate outcomes during nested calls,
 * and appends occurrences to the caller-owned OutcomeSet. Empty and
 * NotReducible are raw protocol events, not interchangeable public frontiers.
 */

/* Head classification only; instruction execution validates its operands. */
static bool he_minimal_instruction(Atom *atom);
static bool he_not_reducible(Atom *atom);

/* Complete an applicable public instruction under its instantiated codomain.
 * A true return means a single continuation was transferred to the existing
 * evaluator tail carrier, not that the query succeeded. With allow_tail,
 * tail_env must be initialized and both atom output slots must be writable.
 * A false return leaves completed answers in os or an incomplete status in
 * the evaluator's completion service. Caller projection is preserve_bindings.
 */
static bool he_eval_typed_instruction(
    Space *s, Arena *a, Atom *atom, Atom *codomain, int fuel,
    const Bindings *prefix, bool preserve_bindings, bool allow_tail,
    Atom **tail_next, Atom **tail_type, Bindings *tail_env, OutcomeSet *os);

/* Complete an invocation-delimiter result before applying the caller's saved
 * demand. This is distinct from preserving function syntax as source under
 * Atom demand, and from acquiring an ordinary raw invocation result.
 */
static void he_eval_invocation_delimiter(
    Space *s, Arena *a, Atom *atom, Atom *type, int fuel,
    const Bindings *prefix, bool preserve_bindings, OutcomeSet *os);

#endif
