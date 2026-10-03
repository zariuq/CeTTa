#ifndef CETTA_PETTA_SEMANTICS_H
#define CETTA_PETTA_SEMANTICS_H

#include <stdatomic.h>

#include "atom.h"
#include "grounded.h"
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
    PETTA_FORM_STREAM_ALPHA_UNIQUE,
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
    bool smaller;
} PeTTaNamedArity;

/* PeTTa has two distinct library-reference constructors.  Standard members
 * are resolved only through the language library overlay; rooted members are
 * resolved only through the named registered package root. */
typedef enum {
    PETTA_LIBRARY_REFERENCE_NONE = 0,
    PETTA_LIBRARY_REFERENCE_STANDARD,
    PETTA_LIBRARY_REFERENCE_ROOTED,
} PeTTaLibraryReferenceKind;

typedef struct {
    PeTTaLibraryReferenceKind kind;
    const char *root;
    const char *member;
} PeTTaLibraryReference;

/* PeTTa exposes SWI's ordered dynamic library_path/1 relation through the
 * Predicate boundary.  These are sequence edits, not map updates: duplicate
 * path occurrences and their authored order remain observable to library/3. */
typedef enum {
    PETTA_LIBRARY_PATH_EFFECT_NONE = 0,
    PETTA_LIBRARY_PATH_EFFECT_PREPEND,
    PETTA_LIBRARY_PATH_EFFECT_APPEND,
    PETTA_LIBRARY_PATH_EFFECT_RETRACT_FIRST,
} PeTTaLibraryPathEffectKind;

typedef struct {
    PeTTaLibraryPathEffectKind kind;
    const char *path;
} PeTTaLibraryPathEffect;

/* A table-incarnation-identified snapshot of PeTTa's authored cons head.
 * Classifications are exact only while `petta_semantics_cons_shape_facts_current`
 * holds.  Conservative discriminators must treat a stale snapshot as unknown. */
typedef struct {
    const SymbolTable *symbol_table;
    uint64_t symbol_table_instance_id;
    SymbolId cons;
} PeTTaConsShapeFacts;

bool petta_semantics_cons_shape_facts(PeTTaConsShapeFacts *facts);
bool petta_semantics_cons_shape_facts_current(
    const PeTTaConsShapeFacts *facts);

static inline bool petta_semantics_facts_is_open_cons_value(
    const PeTTaConsShapeFacts *facts, const Atom *atom) {
    return facts && atom &&
           atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
           atom->expr.elems[0] &&
           atom_is_internal_tag(
               atom->expr.elems[0],
               CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
}

static inline bool petta_semantics_facts_is_cons_constraint(
    const PeTTaConsShapeFacts *facts, const Atom *atom) {
    return petta_semantics_facts_is_open_cons_value(facts, atom) ||
           (facts && facts->cons != SYMBOL_ID_NONE && atom &&
            atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
            atom->expr.elems[0] &&
            atom->expr.elems[0]->kind == ATOM_SYMBOL &&
            atom->expr.elems[0]->sym_id == facts->cons);
}

PeTTaForm petta_semantics_form(SymbolId head);
/* Whether an application of head to nargs arguments is read as its special
 * form: a special form is syntax only at the arities SWI-PeTTa's translator
 * reads (translate_expr), and at any other it is an ordinary application of
 * its name.  True for every other head. */
bool petta_semantics_special_form_reads(SymbolId head, CettaExprLen nargs);

/* A special form of PeTTa is syntax only where it is written.  Reached at
 * run time -- as the value of a variable or expression head, or as the head
 * of the application a `reduce` dispatches -- it is a value: the arguments
 * are evaluated first, and the application is data, unless PeTTa also
 * defines a function of that name, which is called on the values.  Any other
 * head is ordinary: its written call already evaluates its arguments and
 * calls it, so where it is written and where it is reached agree. */
typedef enum {
    PETTA_RUNTIME_HEAD_ORDINARY = 0,
    PETTA_RUNTIME_HEAD_DATA,
    PETTA_RUNTIME_HEAD_FUNCTION,
} PeTTaRuntimeHead;

PeTTaRuntimeHead petta_semantics_runtime_head(SymbolId head);
PeTTaNamedArity petta_semantics_named_arity(
    struct Space *space, Arena *scratch, Atom *head,
    CettaExprLen supplied);
Atom *petta_semantics_function_overapplication_error(
    Arena *arena, Atom *head,
    const CettaExprLen *known_input_arities,
    size_t known_arity_count, CettaExprLen actual_input_arity);
bool petta_semantics_boolean_relation_arity(
    SymbolId head, uint32_t *arity);

/* Operations that read only the size of their one argument, so that a
 * collection only they consume may be represented by its count: PeTTa's
 * `length`, which is a form, and `size-atom` and `size`, which are grounded
 * operations the language profile may withhold. */
typedef enum {
    PETTA_COUNT_CONSUMER_NONE = 0,
    PETTA_COUNT_CONSUMER_FORM,
    PETTA_COUNT_CONSUMER_BUILTIN,
} PeTTaCountConsumer;
PeTTaCountConsumer petta_semantics_count_consumer(SymbolId head);
/* Whether the arguments of an operation `head` are evaluated, so that a
 * counting operation among them consumes its argument's value: not under a
 * quotation, a `return`, a lambda or a predicate. */
bool petta_semantics_count_use_children_executable(SymbolId head);
/* `(== () (R (once (match S P T))))`, in either order, where `R` is
 * `reify`, `collapse` or `primary_reify`: the equality observes only whether
 * the match has a row, which the search machine may answer by membership.
 * The shape is syntactic; the machine decides the rest when it runs. */
bool petta_semantics_match_existence_observer_shape(
    const Atom *expression, SymbolId primary_reify);
bool petta_semantics_intrinsic_partial_arity(
    SymbolId head, CettaExprLen *arity);

/*
 * PeTTa follows SWI's truth atoms.  The generated reader intentionally keeps
 * lower-case `true` and `false` as symbols, so truth interpretation belongs
 * to the language policy rather than to the syntax projection.
 */
bool petta_semantics_truth_value(const Atom *atom, bool *value);
Atom *petta_semantics_boolean_value(Arena *arena, bool value);
/* PeTTa's metatype of a symbol, as SWI-PeTTa's get-metatype/2 gives it:
 * Grounded for a truth value and for a registered function (fun/1), which
 * is one of SWI-PeTTa's registered builtins or, when `registered`, a
 * function the program defines; Symbol for any other symbol, a token
 * included. */
Atom *petta_semantics_symbol_metatype(Arena *arena, SymbolId symbol,
                                      bool registered);
/* Whether SWI-PeTTa registers `symbol` as a builtin function (fun/1) when
 * it loads. */
bool petta_semantics_registered_builtin(SymbolId symbol);
/* A definition at this exact input arity collides with a static predicate
 * in the reference prelude. Dynamic classifiers remain extensible. */
bool petta_semantics_static_builtin_definition(
    SymbolId symbol, CettaExprLen input_arity);
Atom *petta_semantics_builtin_definition_error(
    Arena *arena, SymbolId symbol, CettaExprLen input_arity);

/* Whether SWI-PeTTa registers `symbol` as a builtin function, and if so the
 * input arities its registration records for it (arity/2), as a bit mask:
 * bit n for n arguments.  A registered name may have none. */
bool petta_semantics_registered_builtin_arities(SymbolId symbol,
                                                uint16_t *arities);
/* A registered name's answer about an application to `supplied` arguments:
 * known, and exact, larger or smaller by its recorded arities. */
PeTTaNamedArity petta_semantics_registered_named_arity(
    uint16_t arities, CettaExprLen supplied);
Atom *petta_semantics_success_value(Arena *arena);
bool petta_semantics_library_reference(
    const Atom *atom, PeTTaLibraryReference *reference);
bool petta_semantics_library_path_effect(
    const Atom *atom, PeTTaLibraryPathEffect *effect);

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
/* A list value or a list pattern with a rest as its chain of cells. */
Atom *petta_semantics_flat_list_spine(
    Arena *arena, Atom *flat_list);
/* Whether any open-cons carrier has been built in this process: until one
 * has, no value holds one.  Read on every unification, so inline. */
extern atomic_bool g_petta_open_cons_built;
static inline bool petta_semantics_open_cons_built(void) {
    return atomic_load_explicit(&g_petta_open_cons_built,
                                memory_order_relaxed);
}

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
    /* One past the last element of the flat tail: its length, or for a list
     * pattern [x... | r] the index of r, which the walk continues into. */
    CettaExprIndex flat_end;
    bool in_flat_tail;
    bool invalid;
} PeTTaLogicalListCursor;

void petta_semantics_logical_list_cursor_init(
    PeTTaLogicalListCursor *cursor, Atom *list);
PeTTaLogicalListStep petta_semantics_logical_list_cursor_next(
    PeTTaLogicalListCursor *cursor, Atom **item);
/* PeTTa's is_list/1: a flat expression, or cells ending in one or in a
 * list value, read through the list patterns they pass. */
bool petta_semantics_is_closed_list(Atom *atom);
/* Whether PeTTa runs `head` as the language's type-pure grounded operation.
 * A PeTTa form whose spelling such an operation shares, as `sort-atom`
 * shares HE's, takes the dialect's own evaluation instead. */
static inline bool petta_semantics_grounded_type_pure(SymbolId head) {
    return grounded_op_is_type_pure(head) &&
        petta_semantics_form(head) == PETTA_FORM_NONE;
}
/* Grounded spellings whose arithmetic meaning PeTTa does not define. An
 * unregistered occurrence remains data; an authored function still runs. */
static inline bool petta_semantics_grounded_undefined(SymbolId head) {
    return head == g_builtin_syms.op_floor_div ||
        head == g_builtin_syms.numeric_eq;
}
/* PeTTa's `=alpha` and `==`: tests whose answer is fixed by their
 * arguments' structure up to a consistent renaming of the variables in
 * them, which they neither bind nor show. */
static inline bool petta_semantics_structural_test(SymbolId head) {
    return head == g_builtin_syms.alpha_eq || head == g_builtin_syms.op_eq;
}
/* The elements of a closed list as one flat expression, or as a list value
 * when its cells end in one: `list` itself when it is flat, NULL when its
 * cells end in a non-list or an unbound tail. */
Atom *petta_semantics_closed_list(Arena *arena, Atom *list);
/* PeTTa's `sort-atom` (`total`) and `msort` of a value: a list sorts in
 * SWI's standard order; `sort-atom` gives () for a non-list, as its first
 * clause does.  Anything else, an improper list included, is SWI's
 * type_error(list, Value): NULL with `*type_error` set. */
Atom *petta_semantics_sort_value(Arena *arena, Atom *value, bool total,
                                 bool *type_error);
/* The errors the reference raises, as its catch gives them: Prolog's
 * error(Formal, Context) reads (Error Formal Context), and an unbound
 * context is a fresh variable.
 *
 * `list_error`: what the list builtin `operation` raises for an argument
 * that is no proper list -- instantiation_error for an unbound variable or
 * a list with an unbound tail, type_error(list, Culprit) for anything else
 * -- in the context of the reference's predicate: context(length/2, _) for
 * length and size-atom (which counts with length), context(system:msort/2,
 * _) for msort and sort-atom (which sorts with msort), and unbound for
 * list_to_set.  NULL for another operation.
 * `syntax_error`: syntax_error('Parse error in form: Text') in context
 * none, as sread and parse raise for text they cannot read. */
Atom *petta_semantics_list_error(Arena *arena, SymbolId operation,
                                 Atom *culprit);
Atom *petta_semantics_syntax_error(Arena *arena, const char *text);
/* PeTTa's error for a term that is not a finite tree where only a finite
 * one can go: representation_error(cyclic_term), in the context of the
 * predicate `module:name/arity`, or an unbound context when `name` is NULL. */
Atom *petta_semantics_cyclic_term_error(
    Arena *arena, const char *module, const char *name, int64_t arity);
/* permission_error(modify, static_procedure, Name/Arity) in
 * context(system:Predicate/1, _): Predicate (assertz, retractall) met the
 * static predicate that static-import! made of a space's rows. */
Atom *petta_semantics_static_procedure_error(
    Arena *arena, SymbolId name, int64_t arity, const char *predicate);
/* instantiation_error in context(Module:Name/Arity, _): the predicate needed
 * a bound argument. */
/* A read of a state name that was never set: the reference's nb_getval/2
 * raises existence_error(variable, Name). */
Atom *petta_semantics_state_existence_error(Arena *arena, Atom *name);
Atom *petta_semantics_instantiation_error(
    Arena *arena, const char *module, const char *name, int64_t arity);
/* What (Predicate V) gives for the value V of its argument, the reference's
 * 'Predicate'([F|Args], T) :- T =.. [F|Args]: a list whose first element is
 * a symbol names that compound, and a list of one element names the element.
 * =../2's errors are the answer's: an unbound first element or tail is an
 * instantiation error, and a first element that is no symbol, with
 * arguments after it, a type error, in context(system:(=..)/2, _).  A value
 * that is no nonempty list has no answer. */
typedef enum {
    PETTA_PREDICATE_TERM_NONE = 0,
    PETTA_PREDICATE_TERM_VALUE,
    PETTA_PREDICATE_TERM_ERROR,
    PETTA_PREDICATE_TERM_CAPACITY,
} PeTTaPredicateTerm;
PeTTaPredicateTerm petta_semantics_predicate_term(
    Arena *arena, Atom *value, Atom **term);
/* Whether a declared type of `head` at `arity` gives parameter `index`
 * (0-based) the type Atom, itself or as the dependent domain (: $v Atom):
 * the argument is then passed as written, for any function
 * (translator.pl:707-710). */
bool petta_semantics_parameter_declared_atom(
    Space *space, Arena *arena, Atom *head, CettaExprLen arity,
    CettaExprIndex index);
/* A list read against an expression pattern of `length` elements: exactly
 * that many elements, copied to `elements` when it is given; a proper list
 * of another length; or a tail that is not a list, a partial or an improper
 * list's, which a structural reader leaves undecided. */
typedef enum {
    PETTA_LIST_READ_EXACT = 0,
    PETTA_LIST_READ_OTHER_LENGTH,
    PETTA_LIST_READ_UNDECIDED,
} PeTTaListRead;
PeTTaListRead petta_semantics_read_list(
    Atom *list, CettaExprLen length, Atom **elements);

bool petta_semantics_logical_list_length(
    Atom *list, CettaExprLen *length);
Atom *petta_semantics_materialize_closed_logical_list(
    Arena *arena, Atom *list);
/* Reify the complete logical-list carrier for observation.  Closed spines
 * become the kind they end in: PeTTa's flat expression carrier, or a list
 * value for cells ending in one; an unresolved or improper tail is retained
 * as authored `(cons Head Tail)` syntax.  The private carrier tag is never
 * observable in either case. */
Atom *petta_semantics_materialize_logical_list(
    Arena *arena, Atom *list);

/* Constructor and observation policies used by generated machines.  An
 * evaluated `(cons Head Tail)` becomes the internal O(1) list carrier;
 * unrelated constructors remain ordinary expressions.  Observation copies
 * a carrier graph to an arena-owned PeTTa value, preserving an unresolved
 * tail as authored `cons` syntax. */
Atom *petta_semantics_construct_value(
    Arena *arena, Atom **elements, CettaExprLen length);
/* Whether petta_semantics_construct_value builds an ordinary expression,
 * as atom_expr does, for every value of `length` elements headed by
 * `head`. */
bool petta_semantics_construct_value_is_expression(
    const Atom *head, CettaExprLen length);
bool petta_semantics_construct_value_allocation_bound(
    CettaExprLen length, size_t *bytes_out);
Atom *petta_semantics_materialize_value(
    Arena *arena, Atom *value);
/* True exactly when materialize_value has an observable open-cons carrier
 * to erase.  Opaque closures and quoted syntax are not observation
 * boundaries, so carriers below them deliberately do not count.  Allocation
 * failure is conservative: callers must not expose a carrier through an
 * optimization merely because the inspection could not finish. */
bool petta_semantics_value_contains_observable_open_cons(
    const Atom *value);
bool petta_semantics_contains_cons_constraint(const Atom *atom);
/* A structural index query for a resolved logical pattern. Closed spines
 * become flat expressions. Remaining carriers use a private wildcard to
 * request every occurrence; that wildcard is never a unification pattern.
 * `exact` distinguishes equivalent queries from complete approximations. */
Atom *petta_semantics_match_index_pattern(
    Arena *arena, Atom *pattern, bool *exact);
/*
 * Conservative equation-index discriminator for PeTTa list patterns.
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
/* A lowered head has already classified authored constructors and replaced
 * logical cons patterns with private list carriers. Interpret only those
 * carriers here: retained `cons` syntax in quoted or structural data must
 * not acquire list-pattern meaning during matching. */
bool petta_semantics_match_lowered_head(
    Arena *arena, Atom *head, Atom *value, BindingsBuilder *builder);
/* Same match as freshening the head with this epoch first. Observed
 * structure stays borrowed; retaining source syntax uses the eager match. */
bool petta_semantics_match_lowered_head_epoch(
    Arena *arena, Atom *head, Atom *value,
    BindingsBuilder *builder, uint32_t epoch);

/*
 * HE stdlib equations reused by PeTTa are lowered to private value-binding
 * control heads.  This keeps their implementation-level sequencing distinct
 * from PeTTa's relational syntax `let` and `chain`.
 */
Atom *petta_semantics_lower_shared_atom(Arena *arena, Atom *atom);
bool petta_semantics_is_value_let(SymbolId head);
bool petta_semantics_is_value_chain(SymbolId head);

/*
 * Lower syntax-local PeTTa control forms into the shared CeTTa evaluator
 * vocabulary.  A NULL result means that the form is not a syntax-local
 * lowering (or has the wrong arity), not that evaluation failed.
 */
Atom *petta_semantics_lower(
    Arena *arena, Atom *form, PeTTaForm kind, SymbolId reify_head);

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
 * by a dialect marker and a nominal source-occurrence identity. The identity
 * is minted at translation, preserved by copying/capture/substitution, and
 * does not change the neutral constructor's binding structure. The body is
 * canonical locally-nameless syntax.
 */
Atom *petta_semantics_new_callable_identity(Arena *arena);
Atom *petta_semantics_lambda_value(
    Arena *arena, Atom *identity, Atom *canonical_body);
bool petta_semantics_lambda_body(const Atom *atom, Atom **canonical_body);
/* A value PeTTa applies when it heads an application: a lambda, a nullary
 * lambda or a partial application.  Each is recognized by its top level
 * alone. */
bool petta_semantics_runtime_callable_value(const Atom *atom);
Atom *petta_semantics_nullary_lambda_value(
    Arena *arena, Atom *identity, Atom *body);
bool petta_semantics_nullary_lambda_body(const Atom *atom, Atom **body);

/*
 * A named under-application prints as `(partial f (args ...))` but has a
 * private compound carrier. An authored list with that spelling is data.
 * These helpers are the sole recognizer/constructor for the retained value.
 */
Atom *petta_semantics_partial_value(
    Arena *arena, Atom *base, Atom *const *arguments, CettaExprLen nargs);
bool petta_semantics_partial_view(
    const Atom *atom, Atom **base, Atom **arguments);
bool petta_semantics_partial_head(const Atom *head);

/* The representation exposed to PeTTa value observers, independently of the
 * expression-shaped implementation of a callable. A translated closed
 * callable is a registered name; a partial or captured callable is a compound.
 * Neither is a sequence. Ordinary authored expressions keep their list role. */
PeTTaValueRepresentation petta_semantics_value_representation(const Atom *value);
bool petta_semantics_is_nonlist_carrier(const Atom *value);
bool petta_semantics_sequence_view(
    const Atom *value, Atom *const **elements, CettaExprLen *length);

/* Closed callable carriers are already PeTTa values.  A generated evaluator
 * must not reinterpret their representation as a fresh call. */
bool petta_semantics_is_opaque_runtime_value(const Atom *value);
/* A lambda's canonical value, or a partial application of one: a closure a
 * program holds in its code once its lambdas are compiled.  It is a value
 * wherever it stands, and nothing in its encoding is code. */
bool petta_semantics_is_canonical_closure(const Atom *atom);
/* SWI-PeTTa's MORK space `&mork` (mork_ffi/morkspaces.pl) and its step
 * function `mm2-exec`, which is defined for that space alone. */
bool petta_semantics_is_mm2_exec(SymbolId head);
bool petta_semantics_is_mork_space_name(const Atom *atom);
/* The parameters a compiled closure `(partial LAMBDA (captured ...))` still
 * takes: its lambda's parameters beyond the captured variables.  False for
 * anything else. */
bool petta_semantics_closure_remaining(const Atom *atom,
                                       CettaExprLen *remaining);
/* The child of a function body that gives the body's value, as SWI-PeTTa
 * reads a body it compiles: the body of let, chain and let*, the last form
 * of progn, the first of prog1.  Zero for any other body. */
CettaExprIndex petta_semantics_output_child(const Atom *body);

/*
 * A CLOSED open-cons chain denotes exactly the flat list it spells — the
 * reference cannot distinguish the two.  Rewrite every closed chain in the
 * atom to its flat image (recursively); a chain whose tail is unbound keeps
 * its carrier.  Returns the input atom unchanged when nothing rewrites.
 */
Atom *petta_semantics_flatten_closed_open_cons(Arena *arena, Atom *atom);

#endif /* CETTA_PETTA_SEMANTICS_H */
