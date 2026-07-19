#ifndef CETTA_ABT_H
#define CETTA_ABT_H

#include "atom.h"

/* Canonical object binders are ordinary atoms.  A signature entry says how
   many binders each constructor field crosses.  This is deliberately separate
   from VarId/Bindings, which continue to own matching metavariables. */
typedef struct {
    SymbolId head;
    uint32_t arity;
    uint32_t *depths;
} AbtSignatureEntry;

typedef struct {
    AbtSignatureEntry *entries;
    uint32_t len;
    uint32_t cap;
} AbtSignature;

void abt_signature_init(AbtSignature *signature);
void abt_signature_free(AbtSignature *signature);

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

const AbtSignatureEntry *abt_signature_lookup(const AbtSignature *signature,
                                              SymbolId head,
                                              uint32_t arity);

/* Iterative, capture-avoiding operations over canonical (Var k) terms.
   Inputs must outlive the returned atom; unchanged subterms may be shared.
   NULL means malformed/cyclic ABT input or arithmetic overflow. */
Atom *abt_shift(const AbtSignature *signature, Arena *arena,
                int64_t delta, uint64_t cutoff, Atom *term);
Atom *abt_subst(const AbtSignature *signature, Arena *arena,
                uint64_t index, Atom *substitution, Atom *term);

/* Locally-nameless seams.  close replaces structurally equal occurrences of
   name with (Var depth); open performs the inverse replacement without the
   index decrement performed by substitution.  Names may be structured atoms
   such as (Free x), which keeps free-name syntax disjoint from package data. */
Atom *abt_close(const AbtSignature *signature, Arena *arena,
                Atom *name, Atom *term);
Atom *abt_open(const AbtSignature *signature, Arena *arena,
               Atom *fresh_name, Atom *term);

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
Atom *abt_parse(const AbtSignature *signature, Arena *arena, Atom *surface);

bool abt_scope_check(const AbtSignature *signature, uint64_t initial_depth,
                     Atom *term);
bool abt_alpha_eq(Atom *left, Atom *right);

bool abt_is_op(SymbolId id);
bool abt_op_data_arg(SymbolId id, uint32_t arg_index);

/* Neutral grounded surface used by the differential gate and package
   elaborators.  The default-signature introspection form takes the explicit
   data capability AbtDefaultsV1.  None of these operations authorizes a
   checking judgment. */
Atom *abt_grounded_dispatch(Arena *arena, Atom *head,
                            Atom **args, uint32_t nargs);

#endif /* CETTA_ABT_H */
