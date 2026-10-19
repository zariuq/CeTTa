/* he_macro_expansion.h — HE's macro definitions and the minimal instructions
 * their expansions run, evaluated where the syntax decides them.
 *
 * A macro definition is a head with one equation over distinct variables
 * whose signature takes some arguments as they are written (Atom parameters,
 * and Expression parameters given an expression) and whose other parameters
 * are %Undefined%: HE evaluates each such other argument before the call.  A
 * call of it means its equation's body with each written argument
 * substituted for its parameter.  Where its range is %Undefined% or
 * Expression, what the body answers is evaluated; where it is Atom, the call
 * answers that syntax as it is, which a callee whose range is not Atom
 * evaluates in turn.
 *
 * The prepared machine compiles that body where the call is, and the effect
 * analysis reads what it evaluates, so a library form written as a macro is
 * compiled and analyzed as its expansion rather than as an opaque call.
 */
#ifndef CETTA_HE_MACRO_EXPANSION_H
#define CETTA_HE_MACRO_EXPANSION_H

#include <stdbool.h>
#include <stdint.h>
#include "atom.h"
#include "match.h"
#include "space.h"

enum { HE_MACRO_MAX_ARITY = 64u };

typedef enum {
    /* HE evaluates the argument before the call. */
    HE_MACRO_ARGUMENT_EVALUATED = 0,
    /* The argument is taken as written. */
    HE_MACRO_ARGUMENT_WRITTEN,
    /* A variable at an Expression parameter: its value is taken as written,
     * and HE requires that value to be an expression. */
    HE_MACRO_ARGUMENT_EXPRESSION_VALUE,
} HeMacroArgument;

typedef enum {
    HE_MACRO_NOT_A_MACRO = 0,
    HE_MACRO_EXPANDED,
    /* A macro call whose definition cannot be instantiated here. */
    HE_MACRO_FAILED,
} HeMacroExpandResult;

typedef struct {
    /* The definition's equation with fresh variables. */
    Atom *fresh;
    /* Its head: parameter i + 1 is the fresh variable of argument i. */
    Atom *lhs;
    /* Its body, each written argument substituted for its parameter. */
    Atom *body;
    HeMacroArgument arguments[HE_MACRO_MAX_ARITY];
    /* The range is Atom: the call answers its body's syntax unevaluated. */
    bool answers_syntax;
} HeMacroExpansion;

/* Whether `call` (a symbol-headed expression) calls a macro definition of
 * `space` under HE, how it takes each argument, and whether it answers
 * syntax (answers_syntax may be NULL). */
bool he_macro_signature(Space *space, Atom *call, HeMacroArgument *arguments,
                        bool *answers_syntax);

/* Expand `call` into `arena` when it calls a macro definition. */
HeMacroExpandResult he_macro_expand(Space *space, Arena *arena, Atom *call,
                                    HeMacroExpansion *out);

typedef enum {
    HE_MINIMAL_FOLD_MISMATCH = 0,
    HE_MINIMAL_FOLD_STATIC,
    HE_MINIMAL_FOLD_DYNAMIC,
} HeMinimalFold;

/* Whether a variable of the syntax being folded is a metavariable, which a
 * fold may bind, or opaque: a value decided when the program runs. */
typedef bool (*HeMinimalFoldMetavariable)(const void *context, VarId id);

/* Unify two atoms of an expansion's syntax as HE's unify does: a
 * metavariable on either side binds, into `bindings`, to the other side; an
 * opaque variable meets only itself, and anything else there is DYNAMIC,
 * decided by the value it holds when the program runs, as is a space that
 * meets a pattern. */
HeMinimalFold he_minimal_fold_unify(
    Atom *left, Atom *right, Bindings *bindings, Arena *arena,
    HeMinimalFoldMetavariable metavariable, const void *context);

/* The atom that running `instruction` answers, computed where the syntax
 * decides it, as HE's instruction machine runs it: a chain runs its
 * computation and then its template, unify answers its selected branch as
 * written, decons-atom and cons-atom take an expression apart and build one.
 * An atom that is not an instruction answers itself; another instruction, or
 * one whose operands only a run decides, is DYNAMIC. */
HeMinimalFold he_minimal_fold_instruction(
    Atom *instruction, Arena *arena, HeMinimalFoldMetavariable metavariable,
    const void *context, Atom **result_out);

/* For the effect analysis: what a call of a macro definition evaluates, when
 * one of its written arguments has structure the expansion can use.  Each
 * argument HE evaluates before the call goes to `evaluated` (at most
 * `capacity` of them) and the body, folded where its syntax decides it, to
 * *body_out; both are evaluation contexts.  False leaves the call to be read
 * as written. */
bool he_macro_effect_expansion(Space *space, Arena *arena, Atom *call,
                               Atom **evaluated, uint32_t capacity,
                               uint32_t *evaluated_count, Atom **body_out);

#endif /* CETTA_HE_MACRO_EXPANSION_H */
