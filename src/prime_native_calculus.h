#ifndef CETTA_PRIME_NATIVE_CALCULUS_H
#define CETTA_PRIME_NATIVE_CALCULUS_H

#include "atom.h"
#include "prime_typed_flow.h"
#include "space.h"

/* Result of asking Prime's language-owned native calculus to realize one
 * ordinary authored application.  Decline is an execution fallback, not a
 * typing verdict; infrastructure failure remains distinct from it. */
typedef enum {
    CETTA_PRIME_NATIVE_EXECUTION_DECLINED = 0,
    CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
    CETTA_PRIME_NATIVE_EXECUTION_FAULT,
} CettaPrimeNativeExecutionKindV1;

typedef enum {
    CETTA_PRIME_NATIVE_RESULT_VALUE = 0,
    CETTA_PRIME_NATIVE_RESULT_PLAN,
} CettaPrimeNativeResultFormV1;

typedef struct {
    CettaPrimeNativeExecutionKindV1 kind;
    /* A value has reached the native operation's promised weak-head form.
     * A plan is ordinary MeTTa that must still run before its occurrences may
     * be observed or matched by an enclosing continuation. */
    CettaPrimeNativeResultFormV1 result_form;
    Atom *value;
    /* Current typed value that licenses this realization.  For a native
     * constructor it is the typed result.  Relational execution carries the
     * ordinary typed denotation when an exact authored carrier/meaning
     * vocabulary is available; otherwise it carries the typed proof program
     * whose structure was compiled. */
    const CettaPrimeTypedValueV1 *typed_value;
} CettaPrimeNativeExecutionV1;

/* Try the exact structure-licensed native realization currently owned by
 * each covered Prime operation.  Covered operations presently have one such
 * realization apiece: the eliminator-defined List map program, typed
 * occurrence-bag construction for the ordinary
 * `hyp:chain-candidate-typed` and length-indexed
 * `hyp:path-candidate-typed` relations, and structural execution of the
 * indexed `hyp` family.  When an operation acquires multiple licensed
 * realizations, request-indexed maximal selection belongs upstream of this
 * adapter; syntax order never defines a global strength tier.  The evaluator
 * remains oblivious to map, List, `hyp`, and MIL: it invokes this one language
 * adapter and falls back unchanged when the adapter declines. */
CettaPrimeNativeExecutionV1 cetta_prime_native_calculus_try_v1(
    Arena *owner, Space *space, Atom *application);

#endif /* CETTA_PRIME_NATIVE_CALCULUS_H */
