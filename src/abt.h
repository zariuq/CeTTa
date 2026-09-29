#ifndef CETTA_ABT_H
#define CETTA_ABT_H

#include "atom.h"

/* Canonical object binders are ordinary atoms.  A signature entry says how
   many binders each constructor field crosses.  This is deliberately separate
   from VarId/Bindings, which continue to own matching metavariables.

   Entries of the fixed kind give each field a fixed binder count and are
   used by the canonical (idx k) operations.  Entries of the telescope and
   pattern kinds declare binders whose names occur in the term itself; they
   are used by the named operations below. */
typedef enum {
    ABT_ENTRY_FIXED = 0,
    ABT_ENTRY_TELESCOPE = 1,
    ABT_ENTRY_PATTERN = 2,
} AbtEntryKind;

/* Argument `position` of each element of the list field `field`. */
typedef struct {
    uint32_t field;
    uint32_t position;
} AbtEachScope;

typedef struct {
    SymbolId head;
    uint32_t arity;
    uint32_t *depths;
    AbtEntryKind kind;
    /* Head atoms before the fields; NULL for a single symbol head. */
    Atom **prefix;
    uint32_t prefix_len;
    /* Telescope: the list field of declarations.  Pattern: the field whose
       names are bound. */
    uint32_t binder_field;
    /* Telescope declared (In field k): the declarations are element k of the
       field's value; UINT32_MAX when they are the value itself. */
    uint32_t binder_within;
    /* Per field: inside the binder's scope. */
    bool *scoped;
    AbtEachScope *each;
    uint32_t each_len;
    /* Telescope declarations of the form (ANNOTATION-HEAD ... NAME ...
       ANNOTATION ...); any other declaration is the name itself. */
    Atom **annotation_prefix;
    uint32_t annotation_prefix_len;
    uint32_t annotation_name;
    uint32_t annotation_body;
} AbtSignatureEntry;

/* Which atoms are variable names of the object language. */
typedef enum {
    ABT_NAME_FORM,
    ABT_NAME_SYMBOL_INITIAL,
    ABT_NAME_SYMBOL_PREFIX,
    ABT_NAME_STRING_INITIAL,
} AbtNameClassKind;

typedef struct {
    AbtNameClassKind kind;
    /* ABT_NAME_FORM: an expression of these head atoms and one string. */
    Atom **prefix;
    uint32_t prefix_len;
    /* ABT_NAME_SYMBOL_INITIAL, ABT_NAME_STRING_INITIAL: first byte within
       [low, high]. */
    unsigned char low;
    unsigned char high;
    /* ABT_NAME_SYMBOL_PREFIX: spelling starts with text and is longer. */
    const char *text;
    uint32_t text_len;
} AbtNameClass;

typedef struct {
    AbtSignatureEntry *entries;
    uint32_t len;
    uint32_t cap;
    /* Number of telescope and pattern entries.  The canonical operations
       are defined only for signatures without them. */
    uint32_t in_term_binders;
    AbtNameClass *names;
    uint32_t names_len;
    uint32_t names_cap;
} AbtSignature;

void abt_signature_init(AbtSignature *signature);
void abt_signature_free(AbtSignature *signature);
/* A fixed entry: field i of `head` applied to `arity` fields crosses
   depths[i] binders.  Refuses a head and arity already declared. */
bool abt_signature_add_fixed(AbtSignature *signature, SymbolId head,
                             uint32_t arity, const uint32_t *depths);

/* Decode the importable abt-default-signatures equation compiled from
   lib/abt_default_signatures.metta.  The returned atom belongs to arena.
   Add-defaults admits the equation's AbtSignatures right-hand side through the
   ordinary declaration parser; there is no second native declaration table. */
Atom *abt_default_signature_set(Arena *arena);
bool abt_signature_add_defaults(AbtSignature *signature, Arena *arena);

/* Admit package data of the form

     (AbtSignature Head (Fields field0 ... fieldN) BindDecl)

   where BindDecl is Bind0, (Bind1 field), (BindPi domain codomain), or
   (BindFields (BCons (BField field depth) ... BNil)).

   Fields are structural atoms, not merely symbols.  Hand-authored packages can
   use names such as body and codomain, while generated clients can use faithful
   source positions such as (Hole 0) without inventing semantic names.

   A signature set is (AbtSignatures declaration ...).  Duplicate heads at the
   same arity, structurally duplicate fields, missing BindFields entries, bad
   depths, and undeclared fields fail closed. */
bool abt_signature_add_decl(AbtSignature *signature, Atom *declaration);
bool abt_signature_add_set(AbtSignature *signature, Atom *set);

/* Fixed-kind entries only. */
const AbtSignatureEntry *abt_signature_lookup(const AbtSignature *signature,
                                              SymbolId head,
                                              uint32_t arity);

/* Binders whose names occur in the term.

     (AbtSignature HEAD (Fields field ...) BINDING)

   HEAD is a symbol or (Head atom ...), the closed atoms that begin every
   term of the entry; the fields follow them.  BINDING is

     (BindTelescope field (Scope scope ...) DECLARATIONS)
         field holds a list of declarations.  Each declares one name, bound
         in the declarations after it and in every scope.  DECLARATIONS is
         Names (every declaration is its name) or
         (Annotated HEAD name-index annotation-index): a declaration that is
         an expression beginning with HEAD has its name and its annotation
         at those element indices, and the annotation is in the scope of the
         earlier declarations only; any other declaration is its name.
     (BindPattern field (Scope scope ...))
         the distinct variable names occurring in field are bound in every
         scope.

   A scope is a field or (Each field position): argument position of each
   element of the list field.  Other fields are outside the binder.

   A signature set may contain one (AbtNames class ...) giving the
   variable names of the language:
     (Form HEAD)              an expression of HEAD's atoms and one string
     (SymbolInitial "A" "Z")  symbols whose first byte is in that range
     (SymbolPrefix "?")       symbols that begin with the text
     (StringInitial "A" "Z")  strings whose first byte is in that range

   A binder's name is a symbol, a string, or an expression of symbols that
   ends in one string, such as (var "X").  A fresh name keeps that shape
   and replaces the text's trailing digits by a number.

   Named operations take terms in which binders carry their names.  They
   are defined for signatures whose binding entries are all of these two
   kinds. */
bool abt_is_name(const AbtSignature *signature, const Atom *atom);

/* The distinct free variable names of term, in first-occurrence order, as
   an expression; NULL when the term or signature is malformed. */
Atom *abt_free_names(const AbtSignature *signature, Arena *arena, Atom *term);

/* Simultaneous capture-avoiding substitution of free names.  bindings is
   an expression of (NAME VALUE) pairs with distinct names.  A binder is
   renamed only when its name occurs free in a value that is substituted
   inside its scope; the new name is the old one with a numeric suffix and
   occurs nowhere in the term or the values. */
Atom *abt_substitute(const AbtSignature *signature, Arena *arena,
                     Atom *bindings, Atom *term);

/* Rename binders apart: a binder whose name occurs in the expression avoid,
   occurs free in term, or was already declared by a binder earlier in term
   gets a fresh name.  The result declares every name once, never a free
   name of term, and none of avoid. */
Atom *abt_rename_bound(const AbtSignature *signature, Arena *arena,
                       Atom *avoid, Atom *term);

/* Equality up to the names of binders.  Sets *equal; false when either
   term or the signature is malformed. */
bool abt_named_alpha_equal(const AbtSignature *signature, Arena *arena,
                           Atom *left, Atom *right, bool *equal);

/* Iterative, capture-avoiding operations over canonical (idx k) terms.
   Inputs must outlive the returned atom; unchanged subterms may be shared.
   NULL means malformed/cyclic ABT input or arithmetic overflow. */
Atom *abt_shift(const AbtSignature *signature, Arena *arena,
                int64_t delta, uint64_t cutoff, Atom *term);
Atom *abt_subst(const AbtSignature *signature, Arena *arena,
                uint64_t index, Atom *substitution, Atom *term);

/* Locally-nameless seams.  close replaces structurally equal occurrences of
   name with (idx depth); open performs the inverse replacement without the
   index decrement performed by substitution.  A key may be a bare symbol or
   quoted closed structural syntax. */
Atom *abt_close(const AbtSignature *signature, Arena *arena,
                Atom *name, Atom *term);
Atom *abt_open(const AbtSignature *signature, Arena *arena,
               Atom *fresh_name, Atom *term);

/* Introduce one surrounding binder in a mixed named/canonical syntax.
   Existing loose indices are shifted outward while occurrences structurally
   equal to name become the new index zero.  This single traversal is required
   so shifting existing indices and closing the selected name happen under the
   same binder-depth accounting. */
Atom *abt_bind(const AbtSignature *signature, Arena *arena,
               Atom *name, Atom *term);

/* Invertible, capture-free structural presentation for errors, tools, and
   REPLs.  This is deliberately not a language's mixfix printer.  A binding
   constructor is represented as

     (ABTBind Head (Binders a0 ... aN) field0 ... fieldM)

   while non-binding structure is preserved.  print chooses deterministic
   names that do not occur in the canonical input; parse accepts alpha-renamed
   binders, resolves lexical shadowing innermost-first, and returns canonical
   de Bruijn syntax.  ABTBind is reserved within this presentation.  Both
   traversals are iterative and reject malformed, reserved-marker, or cyclic
   inputs. */
Atom *abt_print(const AbtSignature *signature, Arena *arena, Atom *term);
Atom *abt_parse(const AbtSignature *signature, Arena *arena, Atom *syntax);

bool abt_scope_check(const AbtSignature *signature, uint64_t initial_depth,
                     Atom *term);
bool abt_alpha_eq(Atom *left, Atom *right);

bool abt_is_op(SymbolId id);
bool abt_op_data_arg(SymbolId id, uint32_t arg_index);

/* Neutral grounded syntax used by the differential gate and package
   elaborators.  The default-signature introspection form takes the explicit
   data capability AbtDefaultsV1.  None of these operations authorizes a
   checking judgment. */
Atom *abt_grounded_dispatch(Arena *arena, Atom *head,
                            Atom **args, uint32_t nargs);

#endif /* CETTA_ABT_H */
