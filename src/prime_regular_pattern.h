#ifndef CETTA_PRIME_REGULAR_PATTERN_H
#define CETTA_PRIME_REGULAR_PATTERN_H

#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "prime_regular_kernel.h"

/* The shared Pattern wire is the canonical inference carrier:
 *
 *   (Var n) | (FVar "x") | (PApp "K" args) | (PLam BNone body)
 *   (PMultiLam n names body) | (PSubst body replacement)
 *   (PCollection kind elements rest)
 *
 * where args/elements are LNil/LCons lists. */
typedef struct {
    const Atom *const *names; /* intrinsic index order: innermost first */
    size_t count;
} CettaPrimeRegularPatternEnvironmentV1;

/* Authored free names supplied by an already admitted declaration context.
 * The names are source-level symbols in the same intrinsic-index order as the
 * corresponding PrimeCtxCons entries.  This is an elaboration environment,
 * not declaration evidence; callers remain responsible for constructing it
 * only from a checked, revision-current signature. */
typedef struct {
    const Atom *const *names;
    size_t count;
} CettaPrimeRegularTermEnvironmentV1;

/* Private declaration-elaboration marker for one already scoped universe
 * parameter.  No parser spelling constructs this value: declaration schema
 * elaboration replaces its explicitly scoped source variables with these
 * markers before the ordinary authored-term lowering pass. */
Atom *cetta_prime_regular_level_parameter_marker_v1(
    Arena *arena, uint64_t parameter);

typedef enum {
    CETTA_PRIME_REGULAR_PATTERN_SYNTAX_NONE = 0,
    CETTA_PRIME_REGULAR_PATTERN_DANGLING_BOUND_VARIABLE,
    CETTA_PRIME_REGULAR_PATTERN_UNKNOWN_FREE_VARIABLE,
    CETTA_PRIME_REGULAR_PATTERN_BINDER_NAME_COLLISION,
    CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
    CETTA_PRIME_REGULAR_PATTERN_UNEXPECTED_BINDER,
    CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_MULTI_BINDER,
    CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_EXPLICIT_SUBSTITUTION,
    CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_COLLECTION
} CettaPrimeRegularPatternSyntaxErrorV1;

typedef enum {
    CETTA_PRIME_REGULAR_PATTERN_OK = 0,
    CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR,
    CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
    CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT,
    CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
    CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT
} CettaPrimeRegularPatternStatusV1;

typedef struct {
    CettaPrimeRegularPatternStatusV1 status;
    Atom *term;
    CettaPrimeRegularPatternSyntaxErrorV1 syntax_error;
    uint64_t index;
    const char *name;
    size_t arity;
    const char *reason;
} CettaPrimeRegularPatternElaborationV1;

typedef enum {
    CETTA_PRIME_REGULAR_PATTERN_PHASE_NONE = 0,
    CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_SYNTAX,
    CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_FORMATION,
    CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_SYNTAX,
    CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_TYPING
} CettaPrimeRegularPatternCheckPhaseV1;

typedef struct {
    CettaPrimeRegularPatternCheckPhaseV1 phase;
    CettaPrimeRegularPatternElaborationV1 syntax;
    CettaPrimeRegularKernelResult judgment;
    Atom *term;
    Atom *expected;
} CettaPrimeRegularPatternCheckV1;

/* Prime's authored regular syntax is intentionally smaller than the Pattern
 * wire and lower-case throughout:
 *
 *   u0 | u1 | (u n) | (idx k) | (lam name body) | (lam (name ...) body)
 *   (-> domain codomain) | (app f x) | (pair x y)
 *   (fst p) | (snd p) | (id A x y) | (refl x)
 *
 * A lexical name is either a bare symbol or an explicit universal name
 * quote, @key (parsed as (quote key)).  Bare `_` is anonymous; `$x` remains
 * a matcher variable and is never admitted as a lexical binder.  Lexical
 * names are erased to Pattern variables before the proved strict Pattern
 * elaborator is invoked.  Multi-binders are syntax sugar for nested unary
 * binders; PMultiLam is not introduced.
 *
 * Arrows and sigma types accept telescope groups such as `(x y : A)` and
 * lower them to nested Pi/Sigma binders.  Annotation-bearing lambdas are
 * deliberately not accepted by this interface: an annotation may only be
 * exposed after the typed elaborator proves that it is checked, never by
 * erasing it into an unannotated Pattern lambda. */
typedef enum {
    CETTA_PRIME_REGULAR_TERM_OK = 0,
    CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX,
    CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
    CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR,
    CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
    CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT
} CettaPrimeRegularTermStatusV1;

typedef enum {
    CETTA_PRIME_REGULAR_TERM_SYNTAX_NONE = 0,
    CETTA_PRIME_REGULAR_TERM_WRONG_ARITY,
    CETTA_PRIME_REGULAR_TERM_EMPTY_BINDER_LIST,
    CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME,
    CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER,
    CETTA_PRIME_REGULAR_TERM_BINDER_TYPE_ARITY_MISMATCH,
    CETTA_PRIME_REGULAR_TERM_TYPED_BINDER_REQUIRES_AUTHORITY,
    CETTA_PRIME_REGULAR_TERM_INVALID_INDEX,
    CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL
} CettaPrimeRegularTermSyntaxErrorV1;

typedef struct {
    CettaPrimeRegularTermStatusV1 status;
    Atom *pattern;
    Atom *unresolved_name;
    CettaPrimeRegularTermSyntaxErrorV1 syntax_error;
    size_t position;
    size_t arity;
    const char *reason;
} CettaPrimeRegularTermElaborationV1;

typedef enum {
    CETTA_PRIME_REGULAR_TERM_PHASE_NONE = 0,
    CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX,
    CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN,
    CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION,
    CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX,
    CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN,
    CETTA_PRIME_REGULAR_TERM_PHASE_TERM_TYPING
} CettaPrimeRegularTermCheckPhaseV1;

typedef struct {
    CettaPrimeRegularTermCheckPhaseV1 phase;
    CettaPrimeRegularTermElaborationV1 syntax;
    CettaPrimeRegularPatternElaborationV1 pattern;
    CettaPrimeRegularKernelResult judgment;
    Atom *term;
    Atom *expected;
} CettaPrimeRegularTermCheckV1;

CettaPrimeRegularPatternElaborationV1
cetta_prime_regular_pattern_elaborate_v1(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget);

/* Expected syntax and formation precede subject syntax and typing.  Formation
 * is prepared once and consumed without replay by the final checking phase. */
CettaPrimeRegularPatternCheckV1
cetta_prime_regular_pattern_elaborate_and_check_v1(
    Arena *arena, Atom *context,
    CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *term_pattern, Atom *expected_pattern,
    CettaPrimeRegularKernelBudget *budget);

/* Translate one public lower-case term to the canonical Pattern wire.  The
 * result distinguishes a term outside this syntax from a recognized syntax
 * term whose interior lies outside the proved regular fragment. */
CettaPrimeRegularTermElaborationV1
cetta_prime_regular_term_to_pattern_v1(
    Arena *arena, Atom *syntax, CettaPrimeRegularKernelBudget *budget);

/* As above, but resolves free authored symbols through a declaration context.
 * An unresolved symbol is returned in `unresolved_name`, allowing a caller to
 * look up and admit precisely the missing declaration before retrying. */
CettaPrimeRegularTermElaborationV1
cetta_prime_regular_term_to_pattern_in_environment_v1(
    Arena *arena, CettaPrimeRegularTermEnvironmentV1 environment,
    Atom *syntax, CettaPrimeRegularKernelBudget *budget);

/* Cheap root recognition only; this never grants authority. */
bool cetta_prime_regular_term_maybe_syntax_v1(Atom *syntax);

/* Map one intrinsic regular-kernel constructor to its lower-case authored
 * spelling.  Non-constructors are returned unchanged. */
Atom *cetta_prime_regular_term_authored_symbol_v1(
    Arena *arena, Atom *intrinsic);

/* Quote one intrinsic regular term into the lower-case authored vocabulary.
 * Explicit closed tower sorts are printed as `(u n)`.  A NULL result means
 * that the term contains an intrinsic form for which this authored fragment
 * has not yet earned a public spelling; callers must not leak the wire form. */
Atom *cetta_prime_regular_term_quote_intrinsic_v1(
    Arena *arena, Atom *intrinsic);

/* Elaborate and prepare one authored type without inspecting a subject term. */
CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_form_v1(
    Arena *arena, Atom *context, Atom *expected_syntax,
    CettaPrimeRegularKernelBudget *budget);

/* Elaborate and synthesize one authored declaration-free regular term. */
CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_synth_v1(
    Arena *arena, Atom *context, Atom *term_syntax,
    CettaPrimeRegularKernelBudget *budget);

/* Syntax lowering preserves the same phase discipline as the proved Pattern
 * checker: expected syntax and formation are settled before subject syntax is
 * inspected. */
CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_elaborate_and_check_v1(
    Arena *arena, Atom *context, Atom *term_syntax, Atom *expected_syntax,
    CettaPrimeRegularKernelBudget *budget);

#endif /* CETTA_PRIME_REGULAR_PATTERN_H */
