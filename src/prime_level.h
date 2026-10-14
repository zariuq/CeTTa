#ifndef CETTA_PRIME_LEVEL_H
#define CETTA_PRIME_LEVEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/* A natural number of any size: a coefficient of a notation, the offset of a
 * parameter, the natural number a notation is.  The NULL natural is zero.
 * Naturals are immutable and shared, and live in the arena they were built
 * in, as notations do.  A client builds one from a machine number or from
 * decimal text and reads it back the same two ways; the arithmetic on them
 * is the library's own. */
typedef struct CettaPrimeLevelNaturalV1 CettaPrimeLevelNaturalV1;

/* A closed universe level is an ordinal notation.  Below epsilon-zero it is
 * a Cantor normal form: zero, or omega^exponent * coefficient + remainder
 * with a positive coefficient and a remainder below omega^exponent, where
 * exponent and remainder are notations of the same kind.  The natural number
 * n > 0 is omega^0 * n, and the NULL notation is zero.  These are the levels
 * an author writes.
 *
 * Above every Cantor normal form lie the levels of the sorts that hold the
 * whole tower: the level of the sort of all sets, of its sort, and so on.
 * The n-th of them is, as an ordinal, epsilon-zero + n.  No author writes
 * one: no sum, product or power of written levels reaches them.
 *
 * Notations exist only in normal form.  They are immutable and shared: the
 * exponent and remainder of a notation, and the notations a level was built
 * from, must live at least as long as whatever refers to them, as the
 * children of an expression atom do.
 *
 * No notation is too large for the library: a coefficient has any number of
 * digits, exponents nest to any depth and a notation has any number of
 * terms.  Nothing here recurses on the depth of a notation. */
typedef struct CettaPrimeLevelNotationV1 CettaPrimeLevelNotationV1;

typedef struct {
    const CettaPrimeLevelNotationV1 *exponent;
    const CettaPrimeLevelNaturalV1 *coefficient;
    const CettaPrimeLevelNotationV1 *remainder;
} CettaPrimeLevelNotationTermV1;

/* A Prime universe level is stored only in canonical form: a constant
 * notation and a sorted, key-distinct collection of parameter-plus-offset
 * atoms.  Source syntax may have const/parameter/successor/maximum structure,
 * but clients cannot manufacture a noncanonical admitted level value.
 *
 * A parameter stands for a level an author writes: a Cantor normal form.
 * So a parameter raised by any offset is below every level above the Cantor
 * normal forms, and the canonical form says it: a level whose constant is
 * such a level has no parameter atoms, and a constant that is a natural
 * number not above the largest offset is dropped.  Two levels have the same
 * value under every valuation of the parameters in the Cantor normal forms
 * exactly when their canonical forms are the same. */
typedef struct CettaPrimeLevelV1 CettaPrimeLevelV1;

typedef struct {
    uint64_t parameter;
    const CettaPrimeLevelNaturalV1 *offset;
} CettaPrimeLevelParameterOffsetV1;

typedef struct {
    const CettaPrimeLevelNotationV1 *constant;
    const CettaPrimeLevelParameterOffsetV1 *parameters;
    size_t parameter_count;
} CettaPrimeLevelViewV1;

typedef enum {
    CETTA_PRIME_LEVEL_OK_V1 = 0,
    CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
    /* The level exists, and there was no memory to compute or to hold it:
     * its value fills more than memory addresses, a size the computation
     * needs is not a `size_t`, or the library's own working storage could
     * not be had.  Authorities must treat this as incomplete computation,
     * never as a semantic refutation.  Where the arena itself cannot grow
     * the process ends, as everywhere an arena is used. */
    CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1,
    /* The level exists, and computing it takes more steps than the budget
     * of the computation has left.  Incomplete computation as well, never a
     * semantic refutation. */
    CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1,
} CettaPrimeLevelStatusV1;

/* The steps a computation may still take, and those it has taken.  The sum,
 * product and power of notations can take far more work than their operands
 * are long, so they count their work against a budget of their caller: one
 * step for every term they read or build and for every pair of terms they
 * compare, and for the numbers in those terms one step for every limb of a
 * sum or difference and for every pair of limbs of a product, a limb being
 * nine decimal digits.  An operation that needs more steps than are left
 * takes what is left and ends with BUDGET_EXHAUSTED.  A budget that is not
 * limited, like no budget at all, counts nothing.
 *
 * A budget may stand within another one.  The steps of an operation are then
 * taken from both, and the operation ends with BUDGET_EXHAUSTED where either
 * has too few.  A caller whose own steps are settled later, but whose levels
 * are the work of a computation with a budget, puts its budget within that
 * one.
 *
 * A computation that has no budget of its own still does not take work
 * without end: its level arithmetic is given the default allowance below,
 * a budget marked `allowance`.  Spending it ends the computation as any
 * exhausted budget does, and a budget of its own, given explicitly,
 * replaces it. */
typedef struct CettaPrimeLevelBudgetV1 {
    bool limited;
    uint64_t remaining;
    uint64_t spent;
    struct CettaPrimeLevelBudgetV1 *within;
    /* This budget is the default allowance of a computation without one. */
    bool allowance;
} CettaPrimeLevelBudgetV1;

/* The steps of level arithmetic in a computation that has no budget of its
 * own.  No level an existing program writes takes more than a hundredth of
 * them; the power of omega + 1 to a hundred thousand takes about a seventh. */
#define CETTA_PRIME_LEVEL_DEFAULT_ALLOWANCE_V1 UINT64_C(10000000)

/* A budget of the default allowance: limited to that many steps, within no
 * other budget, and marked as the allowance. */
void cetta_prime_level_budget_allowance_v1(CettaPrimeLevelBudgetV1 *budget);

/* Whether a budget, or one it stands within, is limited: whether the work
 * charged to it is counted at all.  False for NULL. */
bool cetta_prime_level_budget_limited_v1(const CettaPrimeLevelBudgetV1 *budget);

/* Whether a default allowance that a budget is, or stands within, has no
 * steps left: whether the allowance, and not a budget given explicitly, ended
 * a computation. */
bool cetta_prime_level_budget_allowance_spent_v1(
    const CettaPrimeLevelBudgetV1 *budget);

/* A substitution assigns an admitted level to a parameter.  A parameter
 * stands for a level an author writes, so a replacement whose constant is a
 * level above the Cantor normal forms is an invalid argument. */
typedef CettaPrimeLevelStatusV1 (*CettaPrimeLevelSubstitutionV1)(
    void *context, uint64_t parameter,
    const CettaPrimeLevelV1 **replacement_out);

/* A valuation assigns a Cantor normal form to a parameter; NULL is the level
 * zero, so the status alone reports failure.  A level above the Cantor
 * normal forms is the value of no parameter: assigning one is an invalid
 * argument. */
typedef CettaPrimeLevelStatusV1 (*CettaPrimeLevelValuationV1)(
    void *context, uint64_t parameter,
    const CettaPrimeLevelNotationV1 **value_out);

/* The natural number `value`; zero is the NULL natural. */
CettaPrimeLevelStatusV1 cetta_prime_level_natural_v1(
    Arena *owner, uint64_t value,
    const CettaPrimeLevelNaturalV1 **natural_out);

/* The natural number written by decimal digits, of which there may be any
 * number.  Text with any other character, a sign included, and the empty
 * text are invalid arguments. */
CettaPrimeLevelStatusV1 cetta_prime_level_natural_from_decimal_v1(
    Arena *owner, const char *digits,
    const CettaPrimeLevelNaturalV1 **natural_out);

/* Whether a natural number is a machine number, and which one. */
bool cetta_prime_level_natural_fits_uint64_v1(
    const CettaPrimeLevelNaturalV1 *natural, uint64_t *value_out);

/* The decimal digits of a natural number, all of them and without leading
 * zeros; zero is "0". */
CettaPrimeLevelStatusV1 cetta_prime_level_natural_decimal_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *natural,
    const char **digits_out);

/* The notation omega^exponent * coefficient + remainder.  A zero coefficient,
 * a remainder that is not below omega^exponent, and an exponent or remainder
 * above the Cantor normal forms are invalid arguments: such a tree is not a
 * normal form and denotes no level here. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *exponent,
    const CettaPrimeLevelNaturalV1 *coefficient,
    const CettaPrimeLevelNotationV1 *remainder,
    const CettaPrimeLevelNotationV1 **notation_out);

/* The notation of a natural number; zero is the NULL notation. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_natural_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *value,
    const CettaPrimeLevelNotationV1 **notation_out);

/* The n-th level above all Cantor normal forms, for a natural number n of
 * any size: the level of the sort of all sets (n = 0), of its sort (n = 1),
 * and so on; as an ordinal, epsilon-zero + n.  Every Cantor normal form is
 * below every one of these levels, and among themselves they are ordered by
 * n.  The 0-th is a limit, and the (n + 1)-th is the successor of the n-th.
 * Authors cannot write these levels. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_above_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *n,
    const CettaPrimeLevelNotationV1 **notation_out);

/* Whether a notation is one of the levels above all Cantor normal forms,
 * and which one.  A client that walks the terms of a notation asks this
 * first: such a level has no terms. */
bool cetta_prime_level_notation_above_value_v1(
    const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 **n_out);

/* The leading term of a Cantor normal form and what follows it.  False for
 * zero and for a level above the Cantor normal forms. */
bool cetta_prime_level_notation_term_v1(
    const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelNotationTermV1 *term_out);

/* Whether a notation is a natural number, and which one.  False for a level
 * above the Cantor normal forms. */
bool cetta_prime_level_notation_natural_value_v1(
    const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 **value_out);

/* The ordinal sum, product and power of two Cantor normal forms, by the
 * definitions proved in Mettapedia,
 * TypeTheory/UniverseLevel/NotationArithmetic.lean (`Cnf.add`, `Cnf.mul`,
 * `Cnf.pow`): on normal forms the results are normal forms, and among
 * natural numbers they are the sum, product and power of the numbers.  The
 * operations are not commutative: 1 + omega is omega and omega + 1 is the
 * successor of omega; 2 * omega is omega and omega * 2 is omega + omega;
 * 2 ^ omega is omega.  These are the operations of the levels an author
 * writes: an operand above the Cantor normal forms is an invalid
 * argument.  Their work is counted against `budget`, which may be NULL. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_add_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **sum_out);

CettaPrimeLevelStatusV1 cetta_prime_level_notation_mul_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **product_out);

CettaPrimeLevelStatusV1 cetta_prime_level_notation_pow_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *base,
    const CettaPrimeLevelNotationV1 *exponent,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **power_out);

/* The order of two notations: `*order_out` is negative, zero or positive as
 * the left one is below, equal to or above the right one.  Cantor normal
 * forms compare by exponent, then coefficient, then remainder, with zero
 * least; on normal forms this is the order of the ordinals denoted.  A level
 * above the Cantor normal forms is above every one of them. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_compare_v1(
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, int *order_out);

/* The notation `offset` successors above the given one: a natural last term
 * grows by the offset, any other Cantor normal form gains the offset as its
 * last term, and the n-th level above them becomes the (n + offset)-th. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_offset_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelNotationV1 **shifted_out);

/* The least notation whose successor is at least the given one: the
 * predecessor of a successor notation, and the notation itself when it is
 * zero or a limit.  So `notation <= level + 1` holds exactly when this
 * notation is at most `level`.  The 0-th level above the Cantor normal forms
 * is a limit, and under the (n + 1)-th is the n-th. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_under_successor_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNotationV1 **under_out);

/* The least notation that reaches the given one when it is raised by
 * `offset` successors: the natural last term of a Cantor normal form loses
 * the offset, or all of itself where the offset is larger, and the terms
 * before it stay; the n-th level above the Cantor normal forms becomes the
 * (n - offset)-th, or the 0-th where the offset is larger.  So
 * `notation <= level + offset` holds exactly when this notation is at most
 * `level`.  It is the level under a successor taken `offset` times. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_under_offset_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelNotationV1 **under_out);

/* Notations read from another representation.  A reader reports whether a
 * handle denotes zero (false) and otherwise what it is: a level above the
 * Cantor normal forms with its number, or a term with its exponent, its
 * coefficient and its remainder as handles of the same kind.  The numbers
 * stay handles of the representation, which orders them itself. */
typedef struct {
    /* The handle is the level `number` above every Cantor normal form; it
     * has no exponent and no remainder. */
    bool above;
    const void *exponent;
    const void *number;
    const void *remainder;
} CettaPrimeLevelNotationReadTermV1;

typedef bool (*CettaPrimeLevelNotationReaderV1)(
    const void *handle, CettaPrimeLevelNotationReadTermV1 *term_out);

/* The order of two numbers of a representation: negative, zero or positive
 * as the left one is below, equal to or above the right one. */
typedef int (*CettaPrimeLevelNumberOrderV1)(
    const void *left, const void *right);

/* The reader of the library's own notations and the order of its naturals:
 * a handle is a notation, and a number is a natural. */
bool cetta_prime_level_notation_read_v1(
    const void *handle, CettaPrimeLevelNotationReadTermV1 *term_out);

int cetta_prime_level_natural_order_v1(const void *left, const void *right);

/* The same order, read from another representation of notations.  This lets
 * a client decide whether its own syntax is a normal form before any
 * notation is built, with the one comparison every notation uses. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_compare_read_v1(
    CettaPrimeLevelNotationReaderV1 reader,
    CettaPrimeLevelNumberOrderV1 number_order, const void *left,
    const void *right, int *order_out);

/* A value computed over a notation from the values of its parts: `zero`
 * gives the value of a handle that reads as zero, `above` that of a level
 * above the Cantor normal forms, and `term` that of a term from the values
 * of its exponent and of its remainder.  Each is called once for every
 * handle met, the parts of a term before the term.  A status other than OK
 * ends the computation with that status. */
typedef struct {
    CettaPrimeLevelStatusV1 (*zero)(
        void *context, const void *handle, void **value_out);
    CettaPrimeLevelStatusV1 (*above)(
        void *context, const void *handle,
        const CettaPrimeLevelNotationReadTermV1 *read, void **value_out);
    CettaPrimeLevelStatusV1 (*term)(
        void *context, const void *handle,
        const CettaPrimeLevelNotationReadTermV1 *read, void *exponent_value,
        void *remainder_value, void **value_out);
} CettaPrimeLevelNotationFoldV1;

/* Compute such a value over a notation of any depth and length.  This is
 * how a client turns notations into its own syntax and its own syntax into
 * notations without recursing on either. */
CettaPrimeLevelStatusV1 cetta_prime_level_notation_fold_read_v1(
    CettaPrimeLevelNotationReaderV1 reader,
    const CettaPrimeLevelNotationFoldV1 *fold, void *context,
    const void *handle, void **value_out);

CettaPrimeLevelStatusV1 cetta_prime_level_constant_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *constant,
    const CettaPrimeLevelV1 **level_out);

CettaPrimeLevelStatusV1 cetta_prime_level_parameter_v1(
    Arena *owner, uint64_t parameter, const CettaPrimeLevelV1 **level_out);

CettaPrimeLevelStatusV1 cetta_prime_level_successor_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelV1 **successor_out);

/* Add a natural offset in one operation.  This is successor iteration at the
 * admitted level, without constructing an offset-deep syntax tree. */
CettaPrimeLevelStatusV1 cetta_prime_level_offset_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelV1 **shifted_out);

CettaPrimeLevelStatusV1 cetta_prime_level_maximum_v1(
    Arena *owner, const CettaPrimeLevelV1 *left,
    const CettaPrimeLevelV1 *right,
    const CettaPrimeLevelV1 **maximum_out);

/* Simultaneous substitution of admitted levels for parameters.  The callback
 * is invoked exactly once for every distinct parameter in the source.  A
 * replacement whose constant is a level above the Cantor normal forms is an
 * invalid argument: no parameter stands for such a level. */
CettaPrimeLevelStatusV1 cetta_prime_level_substitute_v1(
    Arena *owner, const CettaPrimeLevelV1 *source,
    CettaPrimeLevelSubstitutionV1 substitution, void *context,
    const CettaPrimeLevelV1 **result_out);

/* Whether two levels have the same value under every valuation of the
 * parameters in the Cantor normal forms: whether their canonical forms are
 * the same. */
CettaPrimeLevelStatusV1 cetta_prime_level_equal_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right,
    bool *equal_out);

/* Whether the left level is at most the right one under every valuation of
 * the parameters in the Cantor normal forms.  It is decided on the canonical
 * forms (Mettapedia, TypeTheory/UniverseLevel/Bounded.lean,
 * `LevelBounds.CanonicalLeUnder`, with every parameter bounded by the 0-th
 * level above the Cantor normal forms):
 *
 *   Where the constant of the right level is a level above the Cantor
 *   normal forms, the left level is at most the right one exactly when the
 *   constant of the left level is at most that constant.  The parameters of
 *   the left level, raised by any offsets, are below it.
 *
 *   Otherwise every parameter of the left level must occur in the right
 *   level with an offset that is not smaller, and the constant of the left
 *   level must be at most the constant of the right level or, where it is a
 *   natural number, at most the largest offset of the right level: what the
 *   right level shows where every parameter is zero.
 *
 * Where the criterion fails, a valuation in the Cantor normal forms puts the
 * left level above the right one: every parameter at zero, or one parameter
 * of the left level at the successor of what the right level shows without
 * it, which is the larger of its constant and of the largest offset of its
 * other parameters, and the others at zero. */
CettaPrimeLevelStatusV1 cetta_prime_level_le_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right,
    bool *le_out);

/* The closed level a level denotes once its parameters are assigned Cantor
 * normal forms: the maximum of the constant and of every assigned parameter
 * raised by its offset.  A valuation that assigns a level above the Cantor
 * normal forms is an invalid argument. */
CettaPrimeLevelStatusV1 cetta_prime_level_evaluate_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    CettaPrimeLevelValuationV1 valuation, void *context,
    const CettaPrimeLevelNotationV1 **value_out);

bool cetta_prime_level_view_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelViewV1 *view_out);

#endif /* CETTA_PRIME_LEVEL_H */
