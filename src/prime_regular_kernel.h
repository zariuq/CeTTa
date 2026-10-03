#ifndef CETTA_PRIME_REGULAR_KERNEL_H
#define CETTA_PRIME_REGULAR_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "prime_level.h"

typedef enum {
    CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED = 0,
    CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
    CETTA_PRIME_REGULAR_KERNEL_REFUTED,
    CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
    CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
    CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
    /* The operands are admitted and the check ran, but no theorem makes its
     * negative outcome a refutation: the judgment stays open. */
    CETTA_PRIME_REGULAR_KERNEL_UNDECIDED
} CettaPrimeRegularKernelStatus;

/* The steps a judgment may still take.  It is the budget of the level
 * library, so that the arithmetic of a level counts against the budget of
 * the judgment that reads the level.  The kernel and the lowering take their
 * own steps from the budget alone; where it stands within another one, the
 * arithmetic of levels is taken from that one as well. */
typedef CettaPrimeLevelBudgetV1 CettaPrimeRegularKernelBudget;

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

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *term;
    Atom *type;
    Atom *source_type;
    const char *reason;
} CettaPrimeRegularKernelNormalFormV1;

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

/* The sorts above every universe an author writes are written by name.
 * The sort of all sets, `(Sort (LevelAbove 0))`, is `set`; its sort,
 * `(Sort (LevelAbove 1))`, is `class`; and the n-th of them is `(class n)`,
 * for n of any size.  The names are spelled in one place, behind these
 * functions.
 *
 * `sort_above_word` tells whether a symbol is one of the two names.
 * `sort_above_spelling` tells whether syntax is one of the spellings, and
 * gives n as a numeral of the level wire (built in `arena` for a name).
 * `sort_above_name` is the spelling of the n-th sort: the name for 0 and 1,
 * and `(class n)` otherwise.  `sort_above_term` is the kernel term of the
 * n-th sort, `(Sort (LevelAbove n))`.  NULL when there is no memory. */
bool cetta_prime_regular_kernel_sort_above_word_v1(Atom *symbol);

bool cetta_prime_regular_kernel_sort_above_spelling_v1(
    Arena *arena, Atom *syntax, Atom **numeral_out);

Atom *cetta_prime_regular_kernel_sort_above_name_v1(
    Arena *arena, const CettaPrimeLevelNaturalV1 *n);

Atom *cetta_prime_regular_kernel_sort_above_term_v1(
    Arena *arena, Atom *numeral);

/* Whether the closed sort `lower` lies inside the closed sort `upper`, both
 * in kernel spelling `(Sort level)`: universes are cumulative, so a type of
 * a universe is also a type of every sort above it, the sort of all sets
 * included.  False when either is not a closed sort. */
bool cetta_prime_regular_kernel_sort_within_v1(Arena *arena, Atom *lower,
                                              Atom *upper);

/* Quote a declaration-free closed explicit sort as an author writes it: a
 * universe at a level an author writes is the public `(u level)` form, the
 * level in that notation (a numeral of any length, or an ordinal notation
 * such as `omega`, `(+ omega 1)` or `(* (^ omega 2) 3)`), and a sort above
 * all of them is its name.  The sealed legacy marker `U1` is deliberately
 * not rewritten by this API.  NULL means "not an explicit sort" or "not
 * closed". */
Atom *cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
    Arena *arena, Atom *term);

/* The name of a closed sort above every universe an author writes, and NULL
 * for any other term, a universe at a written level included. */
Atom *cetta_prime_regular_kernel_quote_sort_above_v1(Arena *arena, Atom *term);

/* A numeral of the level wire is a natural number of any size: an integer
 * atom that is not negative, a machine integer or a long one.  These tell
 * whether an atom is one, give its number, and write a number as one. */
bool cetta_prime_regular_kernel_level_numeral_v1(const Atom *numeral);

CettaPrimeLevelStatusV1 cetta_prime_regular_kernel_level_numeral_value_v1(
    Arena *arena, const Atom *numeral,
    const CettaPrimeLevelNaturalV1 **natural_out);

Atom *cetta_prime_regular_kernel_level_numeral_atom_v1(
    Arena *arena, const CettaPrimeLevelNaturalV1 *natural);

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

/* Compute a pure intrinsic term using the same beta/eta, projection and
 * declared-rule engine as conversion. Synthesize the source first, then
 * normalize both the term and its synthesized dependent type, then check the
 * output at the computed type. Retain the original displayed type separately:
 * substitution can place checking-only introductions inside its indices.
 * All phases share one budget. This is explicit strong normalization, not
 * the ordinary evaluator, and no termination claim is made for arbitrary
 * supplied declaration rules. Non-success never returns a partial value. */
CettaPrimeRegularKernelNormalFormV1
cetta_prime_regular_kernel_normalize_intrinsic_v1(
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

/* One step of the search for the least level instance of a declaration,
 * exposed for the comparison with the definition it follows (Mettapedia,
 * TypeTheory/UniverseLevel/LeastInstance.lean, `raise`).  The parameters
 * `parameters[0..parameter_count)` hold the closed levels `assignments`, NULL
 * where a parameter holds nothing yet.  They are raised by the least
 * assignment at which `level` reaches the closed level `bound`.  With the
 * status ESTABLISHED, `*outside_out` tells that no least raise exists, and
 * otherwise every assignment that is not NULL is a closed level constant in
 * the core spelling.  A parameter stands for a level an author writes, so a
 * bound above those levels is reached by no assignment: the status is then
 * REFUTED, and nothing is assigned. */
CettaPrimeRegularKernelStatus
cetta_prime_regular_kernel_raise_level_parameters_v1(
    Arena *arena, Atom *level, Atom *bound,
    const uint64_t *parameters, Atom **assignments, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget, bool *outside_out);

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_intrinsic_conversion_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *left, Atom *right,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget);

/* Identity policy of the active profile, mirrored from the session layer.
 * The kernel itself is the same under every policy: elimination with one
 * iota rule and every route retained.  A policy only decides which
 * uniqueness or guest declarations the language makes available, as
 * explicit assumptions used through elimination, never as conversion rules:
 * 0 = none; 1 = a per-carrier uniqueness axiom on request; 2 = a global
 * uniqueness axiom; 3 = as 1, plus the univalence guest declarations. */
enum {
    CETTA_PRIME_IDENTITY_J = 0,
    CETTA_PRIME_IDENTITY_SCOPED = 1,
    CETTA_PRIME_IDENTITY_UIP = 2,
    CETTA_PRIME_IDENTITY_UNIVALENCE = 3
};
void cetta_prime_identity_policy_set(int policy);
int cetta_prime_identity_policy(void);

/* Computation rules as data for the duration of a kernel call: a list
 * (LCons (PrimeRule head arity (pattern ...) rhs) ...) or NULL. */
void cetta_prime_regular_kernel_rules_set(Atom *rules);

/* One admitted `type:rule` step at the root of an intrinsic application.
 * Subterms are not normalized and the term is not rechecked. NULL means no
 * rule matched. A budget failure is also NULL; the caller keeps the call. */
Atom *cetta_prime_regular_kernel_rule_contractum_v1(
    Arena *arena, Atom *term, CettaPrimeRegularKernelBudget *budget);

/* The same step, where an argument the rule inspects that is a call waiting
 * for an observation (a rule at arity `(PObserved n)`) unfolds first; and
 * how many rule firings the step took, the unfoldings included. */
Atom *cetta_prime_regular_kernel_rule_contractum_counted_v1(
    Arena *arena, Atom *term, CettaPrimeRegularKernelBudget *budget,
    uint64_t *firings_out);

/* Whether a rule of `name` in the rules set for the call is guarded: admitted
 * on a set solution, it unfolds only at closed arguments, or, admitted with a
 * family's set model, only under an observation. */
bool cetta_prime_regular_kernel_rule_guarded_v1(Atom *name);

/* The normal form of `term` by the rules set for the call, and how many rule
 * firings it took.  NULL on a budget or engine failure. */
Atom *cetta_prime_regular_kernel_rule_normal_form_v1(
    Arena *arena, Atom *term, CettaPrimeRegularKernelBudget *budget,
    uint64_t *firings_out);

#endif /* CETTA_PRIME_REGULAR_KERNEL_H */
