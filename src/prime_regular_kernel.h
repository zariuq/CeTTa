#ifndef CETTA_PRIME_REGULAR_KERNEL_H
#define CETTA_PRIME_REGULAR_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED = 0,
    CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
    CETTA_PRIME_REGULAR_KERNEL_REFUTED,
    CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
    CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
    CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
} CettaPrimeRegularKernelStatus;

typedef struct {
    bool limited;
    uint64_t remaining;
    uint64_t spent;
} CettaPrimeRegularKernelBudget;

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *type;
    const char *reason;
} CettaPrimeRegularKernelResult;

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *term;
    const char *reason;
} CettaPrimeRegularKernelFormedSchemaV1;

/* A conversion decision distinguishes a judgment inside the native fragment
 * from an input term the native kernel declines.  In particular, unequal
 * well-typed operands are admitted and refuted; they are not a fallback. */
typedef struct {
    CettaPrimeRegularKernelStatus status;
    bool operands_admitted;
    bool equal;
    Atom *left_type;
    Atom *right_type;
    const char *reason;
} CettaPrimeRegularKernelConversionDecision;

typedef struct CettaPrimeRegularKernelPreparedExpectedV1
    CettaPrimeRegularKernelPreparedExpectedV1;

typedef struct {
    CettaPrimeRegularKernelStatus status;
    CettaPrimeRegularKernelPreparedExpectedV1 *prepared;
    const char *reason;
} CettaPrimeRegularKernelPreparedExpectedResultV1;

void cetta_prime_regular_kernel_budget_init(
    CettaPrimeRegularKernelBudget *budget, bool limited, uint64_t steps);

bool cetta_prime_regular_kernel_unwrap_scoped(
    Atom *scoped, Atom **context_out, Atom **term_out);

/* Cheap root-shape prefilter only.  A true result is not typing evidence;
 * exact fragment membership is reported by the conversion decision. */
bool cetta_prime_regular_kernel_term_maybe_syntax(Atom *term);

/* Cheap root-shape prefilter for the declaration-free intrinsic grammar.
 * This additionally admits the proved Pattern boundary's `(Lam body)` form.
 * A true result remains only a routing hint, never typing evidence. */
bool cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(Atom *term);

/* Recognize the internal universe-head wire (`U1` or an explicit `Sort`).
 * This is a shape predicate, not formation evidence; callers establish the
 * enclosing synthesis judgment before using it as a universe classification. */
bool cetta_prime_regular_kernel_term_is_universe_sort_v1(Atom *term);

/* Quote a declaration-free closed explicit sort as the public `(u n)` form.
 * The sealed legacy marker `U1` is deliberately not rewritten by this API.
 * NULL means either "not an explicit sort" or "not closed". */
Atom *cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
    Arena *arena, Atom *term);

/* Exact closed-syntax recognition for the native regular-kernel class.
 * ESTABLISHED means every constructor is in the fragment, every term index is
 * bound, and every universe level is closed.  Schematic level parameters must
 * be freshly instantiated by a declaration authority before this boundary.
 * NOT_SCOPED means the ordinary Prime route must remain authoritative. */
CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_closed_syntax(
    Atom *term, CettaPrimeRegularKernelBudget *budget);

/* Exact closed recognition for the declaration-free intrinsic grammar, with
 * the same no-schematic-level condition as the authored term grammar. */
CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
    Atom *term, CettaPrimeRegularKernelBudget *budget);

/* Exact scoped-syntax recognition.  ESTABLISHED means the context and term
 * use only regular-kernel constructors and every de Bruijn index is bound.
 * OUT_OF_CLASS is a routing result, not a typing or conversion refutation. */
CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_scoped_syntax(
    Atom *scoped, CettaPrimeRegularKernelBudget *budget);

/* As above, additionally requiring the expected type to be regular syntax
 * well scoped under the wrapper's context. */
CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_scoped_check_syntax(
    Atom *scoped, Atom *expected, CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_synth(
    Arena *arena, Atom *scoped, CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_check(
    Arena *arena, Atom *scoped, Atom *expected,
    CettaPrimeRegularKernelBudget *budget);

/* Check the declaration-free intrinsic grammar used by the proved strict
 * Pattern boundary.  Its lambda is `(Lam body)`, unlike the annotated Prime
 * term representation `(Lam domain body)`.  This function establishes the
 * direct regular judgment but does not itself mint a NIK admission token. */
CettaPrimeRegularKernelResult cetta_prime_regular_kernel_check_intrinsic(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget);

/* Weaken one already elaborated intrinsic type across newly prepended
 * declaration assumptions.  This is the structural renaming operation used
 * when a polymorphic declaration occurrence receives its own context entry;
 * it neither forms the type nor establishes a typing judgment. */
CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_weaken_intrinsic_type_v1(
    Arena *arena, Atom *type, uint64_t assumptions,
    CettaPrimeRegularKernelBudget *budget);

/* Synthesize one declaration-free intrinsic term under an explicit regular
 * context.  OUT_OF_CLASS is abstention: it is not evidence that the term has
 * no type. */
CettaPrimeRegularKernelResult cetta_prime_regular_kernel_synth_intrinsic_v1(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeRegularKernelBudget *budget);

/* Instantiate the named universe parameters while checking one
 * declaration-bound judgment.  Parameters are local elaboration
 * metavariables, not object-language terms: constraints select a closed
 * instance and the ordinary intrinsic kernel then replays that instance.
 * Thus the instantiator can propose a level assignment but cannot mint the
 * final judgment. */
CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_synth_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *term,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_form_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

/* Form an intrinsic type schema while solving only the named elaboration
 * parameters, then return the instantiated schema.  Other LevelParam leaves
 * are rigid schema variables and remain explicit in the result.  The result
 * is replayed by the ordinary intrinsic kernel before it is returned. */
CettaPrimeRegularKernelFormedSchemaV1
cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
    Arena *arena, Atom *context, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

/* Formation-directed split used by the strict Pattern boundary.  Successful
 * preparation establishes expected-type syntax and formation exactly once;
 * the private value is then the only entry to the no-repeat checking half. */
CettaPrimeRegularKernelPreparedExpectedResultV1
cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
    Arena *arena, Atom *context, Atom *expected,
    CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_check_prepared_intrinsic_v1(
    Arena *arena, const CettaPrimeRegularKernelPreparedExpectedV1 *prepared,
    Atom *term, CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_convert(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_conversion(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeRegularKernelBudget *budget);

/* Decide conversion for two intrinsic terms in one explicit regular context.
 * This is the conversion half of the authored Pattern boundary; it does not
 * accept the separately scoped transport representation. */
CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
    Arena *arena, Atom *context, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget);

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_intrinsic_conversion_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *left, Atom *right,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

#endif /* CETTA_PRIME_REGULAR_KERNEL_H */
