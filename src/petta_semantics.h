#ifndef CETTA_PETTA_SEMANTICS_H
#define CETTA_PETTA_SEMANTICS_H

#include "atom.h"
#include "match.h"

struct Space;

typedef enum {
    PETTA_FORM_NONE = 0,
    PETTA_FORM_TEST,
    PETTA_FORM_IF,
    PETTA_FORM_PROGN,
    PETTA_FORM_PROG1,
    PETTA_FORM_FOLDALL,
    PETTA_FORM_FORALL,
    PETTA_FORM_MAPLIST,
    PETTA_FORM_MAP_ATOM,
    PETTA_FORM_FOLDL,
    PETTA_FORM_ID,
    PETTA_FORM_APPEND,
    PETTA_FORM_CONS,
    PETTA_FORM_INT_ADD,
    PETTA_FORM_STREAM_UNIQUE,
    PETTA_FORM_STREAM_UNION,
    PETTA_FORM_STREAM_INTERSECTION,
    PETTA_FORM_STREAM_SUBTRACTION,
    PETTA_FORM_LENGTH,
    PETTA_FORM_MSORT,
    PETTA_FORM_FIRST_FROM_PAIR,
    PETTA_FORM_SECOND_FROM_PAIR,
    PETTA_FORM_IS_VAR,
    PETTA_FORM_IS_GROUND,
    PETTA_FORM_IS_EXPR,
    PETTA_FORM_IS_SPACE,
    PETTA_FORM_IS_MEMBER,
    PETTA_FORM_IS_ALPHA_MEMBER,
    PETTA_FORM_ALPHA_UNIQUE,
    PETTA_FORM_LIST_TO_SET,
    PETTA_FORM_EXCLUDE_ITEM,
    PETTA_FORM_REPRA,
    PETTA_FORM_SREAD,
    PETTA_FORM_BIND_STATE,
    PETTA_FORM_GET_STATE,
    PETTA_FORM_CHANGE_STATE,
    PETTA_FORM_NEW_STATE,
    PETTA_FORM_CALL,
    PETTA_FORM_EVAL,
    PETTA_FORM_REDUCE,
    PETTA_FORM_PREDICATE,
    PETTA_FORM_TRANSLATE_PREDICATE,
    PETTA_FORM_IMPORT_PROLOG_FUNCTION,
    PETTA_FORM_PROCESS_METTA_STRING,
    PETTA_FORM_CALL_PREDICATE,
    PETTA_FORM_ASSERTA_PREDICATE,
    PETTA_FORM_ASSERTZ_PREDICATE,
    PETTA_FORM_RETRACT_PREDICATE,
    PETTA_FORM_TABLED,
    PETTA_FORM_ADD_TRANSLATOR_RULE,
    PETTA_FORM_REMOVE_TRANSLATOR_RULE,
    PETTA_FORM_CUT,
    PETTA_FORM_CATCH,
    PETTA_FORM_LAMBDA,
    PETTA_FORM_LET,
    PETTA_FORM_CHAIN,
} PeTTaForm;

typedef struct {
    bool known;
    bool exact;
    bool larger;
} PeTTaNamedArity;

PeTTaForm petta_semantics_form(SymbolId head);
PeTTaNamedArity petta_semantics_named_arity(
    struct Space *space, Arena *scratch, Atom *head,
    CettaExprLen supplied);
bool petta_semantics_boolean_relation_arity(
    SymbolId head, uint32_t *arity);
bool petta_semantics_intrinsic_partial_arity(
    SymbolId head, CettaExprLen *arity);

/*
 * PeTTa follows SWI's truth atoms.  The generated reader intentionally keeps
 * lower-case `true` and `false` as symbols, so truth interpretation belongs
 * to the language policy rather than to the syntax projection.
 */
bool petta_semantics_truth_value(const Atom *atom, bool *value);
Atom *petta_semantics_boolean_value(Arena *arena, bool value);
Atom *petta_semantics_success_value(Arena *arena);
bool petta_semantics_library_descriptor(
    const Atom *atom, const char **member);
bool petta_semantics_library_file_descriptor(
    const Atom *atom, const char **root, const char **member);

/*
 * PeTTa list patterns use `(cons Head Tail)` relationally.  A native
 * expression is the list carrier: its first element is Head and the
 * remaining expression is Tail.  Matching is iterative, bidirectional, and
 * rolls the supplied builder back to its entry mark on failure.
 */
bool petta_semantics_is_cons_constraint(const Atom *atom);
bool petta_semantics_is_open_cons_value(const Atom *atom);
Atom *petta_semantics_open_cons_value(
    Arena *arena, Atom *head, Atom *tail);
Atom *petta_semantics_flat_list_spine(
    Arena *arena, Atom *flat_list);

/*
 * Iterate the logical elements of either a flat expression or a closed
 * internal open-cons chain.  The cursor never exposes the open-cons carrier
 * fields themselves.  An unbound or non-expression tail is INVALID rather
 * than a truncated list.
 *
 * Positive: `(open-cons a (open-cons b (c)))` yields a, b, c, END.
 * Negative: `(open-cons a $tail)` yields a, INVALID.
 */
typedef enum {
    PETTA_LOGICAL_LIST_ITEM = 0,
    PETTA_LOGICAL_LIST_END,
    PETTA_LOGICAL_LIST_INVALID,
} PeTTaLogicalListStep;

typedef struct {
    Atom *rest;
    CettaExprIndex flat_index;
    bool in_flat_tail;
    bool invalid;
} PeTTaLogicalListCursor;

void petta_semantics_logical_list_cursor_init(
    PeTTaLogicalListCursor *cursor, Atom *list);
PeTTaLogicalListStep petta_semantics_logical_list_cursor_next(
    PeTTaLogicalListCursor *cursor, Atom **item);
bool petta_semantics_logical_list_length(
    Atom *list, CettaExprLen *length);
Atom *petta_semantics_materialize_closed_logical_list(
    Arena *arena, Atom *list);

bool petta_semantics_contains_cons_constraint(const Atom *atom);
/*
 * Conservative clause-index discriminator for PeTTa list patterns.
 *
 * `false` is a proof that `pattern` cannot match `value` because an aligned
 * `(cons Head Tail)` constraint faces a rigid empty or non-expression
 * value.  `true` means possible or unknown; in particular, variables and
 * structurally ambiguous applications are never rejected.
 *
 * Positive example: `(f (cons $x $xs))` may match `(f (a b))`.
 * Negative example: `(f (cons $x $xs))` cannot match `(f ())`.
 */
bool petta_semantics_cons_pattern_may_match(
    const Atom *pattern, const Atom *value);
bool petta_semantics_match_cons_constraint(
    Arena *arena, Atom *constraint, Atom *value,
    BindingsBuilder *builder);

/*
 * HE stdlib equations reused by PeTTa are lowered to private value-binding
 * control heads.  This keeps their implementation-level sequencing distinct
 * from PeTTa's relational surface `let` and `chain`.
 */
Atom *petta_semantics_lower_shared_atom(Arena *arena, Atom *atom);
bool petta_semantics_is_value_let(SymbolId head);
bool petta_semantics_is_value_chain(SymbolId head);

/*
 * Lower syntax-local PeTTa control forms into the shared CeTTa evaluator
 * vocabulary.  A NULL result means that the form is not a syntax-local
 * lowering (or has the wrong arity), not that evaluation failed.
 */
Atom *petta_semantics_lower(Arena *arena, Atom *form, PeTTaForm kind);

/*
 * PeTTa's msort follows SWI-Prolog's standard term order and preserves
 * duplicate occurrences.  Empty and non-empty CeTTa expressions correspond
 * to Prolog's [] and list cells, respectively.
 */
bool petta_semantics_term_compare(
    const Atom *left, const Atom *right, int *ordering);
Atom *petta_semantics_msort(Arena *arena, Atom *list);

/*
 * Stable first-occurrence deduplication modulo alpha equivalence.  Keys use
 * first-occurrence variable ordinals and collision-safe structural equality;
 * the returned list retains the original atoms and variable identities.
 */
Atom *petta_semantics_alpha_unique(Arena *arena, Atom *list);

/*
 * SWI-compatible list operations use exact term/variable identity rather
 * than alpha equivalence.  Both preserve the order of retained elements;
 * exclude-item removes every occurrence exactly identical to `item`.
 */
Atom *petta_semantics_list_to_set(Arena *arena, Atom *list);
Atom *petta_semantics_exclude_item(
    Arena *arena, Atom *item, Atom *list);

/* Apply one argument to a symbolic or expression-shaped PeTTa callable. */
Atom *petta_semantics_apply(Arena *arena, Atom *callable, Atom *argument);

/*
 * PeTTa lexical functions use CeTTa's neutral ABT `Lam` constructor, tagged
 * by a dialect marker so hosted object-language lambdas remain ordinary data.
 * The body is canonical locally-nameless syntax.
 */
Atom *petta_semantics_lambda_value(Arena *arena, Atom *canonical_body);
bool petta_semantics_lambda_body(const Atom *atom, Atom **canonical_body);
Atom *petta_semantics_nullary_lambda_value(Arena *arena, Atom *body);
bool petta_semantics_nullary_lambda_body(const Atom *atom, Atom **body);

/*
 * A named under-application has PeTTa's observable `(partial f (args ...))`
 * representation.  These helpers are the sole recognizer/constructor for
 * that representation.
 */
Atom *petta_semantics_partial_value(
    Arena *arena, Atom *base, Atom *const *arguments, CettaExprLen nargs);
bool petta_semantics_partial_view(
    const Atom *atom, Atom **base, Atom **arguments);

/*
 * A CLOSED open-cons chain denotes exactly the flat list it spells — the
 * reference cannot distinguish the two.  Rewrite every closed chain in the
 * atom to its flat image (recursively); a chain whose tail is unbound keeps
 * its carrier.  Returns the input atom unchanged when nothing rewrites.
 */
Atom *petta_semantics_flatten_closed_open_cons(Arena *arena, Atom *atom);

#endif /* CETTA_PETTA_SEMANTICS_H */
