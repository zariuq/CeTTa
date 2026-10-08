#ifndef CETTA_PRIME_SEMANTICS_H
#define CETTA_PRIME_SEMANTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "nik_direct_authority.h"
#include "prime_typing_authority.h"
#include "prime_regular_kernel.h"
#include "space.h"

/* Certificate-free NIK face for Prime's native `type:` judgments, including
 * `type:may` and `type:must` over explicit answer producers.  Proof replay
 * through external authorities is the distinct `nik:check` judgment. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    Atom *(*judge)(Arena *arena, Space *space, Atom *judgment,
                   bool steps_limited, uint64_t steps);
} CettaPrimeTypingDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_prime_typing_direct_authority_v1;
extern const CettaPrimeTypingDirectServiceV1
    cetta_prime_typing_direct_service_v1;

bool cetta_prime_typing_direct_service_v1_is_valid(
    const CettaPrimeTypingDirectServiceV1 *service);

/* C-side observations for benchmark producers.  The public MeTTa spellings
 * remain `type:check` and `type:of`; this interface adds no executable syntax.
 * One authority result is shared by formation, synthesis, and checking. */

typedef enum {
    CETTA_PRIME_TYPING_ROUTE_NONE = 0,
    CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION,
    CETTA_PRIME_TYPING_ROUTE_LEGACY_HE
} CettaPrimeTypingRouteV1;

typedef struct {
    bool limited;
    uint64_t initial;
    uint64_t spent;
    uint64_t remaining;
    uint64_t formation;
    uint64_t synthesis;
    uint64_t normalization;
    uint64_t checking;
    uint64_t refinement;
    uint64_t evaluation;
} CettaPrimeTypingResourceObservationV1;

/* The result tag determines how to read `payload`: a positive derivation, a
 * checked obstruction, an outside-fragment reason, an incomplete frontier,
 * or an engine-fault diagnostic.  The Atom payload keeps Prime evidence
 * typed without placing a language-specific pointer in the generic NIK ABI. */
typedef struct {
    CettaNikResultV1 result;
    CettaPrimeTypingRouteV1 route;
    Atom *payload;
    /* Checked native object retained by an Established Prime route. Closed
     * routes return intrinsic syntax, scoped routes keep their context, and
     * declared routes quote named declarations from the checked context.
     * This is NULL for non-established results and for legacy routes. A raw
     * boundary can reuse it without repeating elaboration or checking. */
    Atom *canonical_term;
    CettaPrimeTypingResourceObservationV1 resources;
} CettaPrimeTypingAuthorityObservationV1;

typedef struct {
    Atom *term;
    Atom *expected_type;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingCheckingCandidateV1;

typedef struct {
    Atom *type;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingFormationCandidateV1;

typedef struct {
    Atom *term;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingSynthesisCandidateV1;

typedef struct {
    CettaPrimeTypingCheckingCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingCheckingObservationV1;

typedef struct {
    CettaPrimeTypingFormationCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingFormationObservationV1;

typedef struct {
    CettaPrimeTypingSynthesisCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingSynthesisObservationV1;

typedef struct {
    size_t count;
    CettaPrimeTypingCheckingObservationV1 *occurrences;
    size_t established_count;
    size_t refuted_count;
    size_t undetermined_count;
    size_t incomplete_count;
    size_t engine_fault_count;
} CettaPrimeTypingCheckingBagV1;

/* Observe a candidate exactly once.  Evidence and all returned Atom pointers
 * are owned by `arena`.  A zero explicit budget is invalid rather than being
 * reinterpreted as an unbounded request. */
bool cetta_prime_typing_observe_checking_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidate,
    CettaPrimeTypingCheckingObservationV1 *observation_out);

bool cetta_prime_typing_observe_formation_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingFormationCandidateV1 *candidate,
    CettaPrimeTypingFormationObservationV1 *observation_out);

bool cetta_prime_typing_observe_synthesis_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingSynthesisCandidateV1 *candidate,
    CettaPrimeTypingSynthesisObservationV1 *observation_out);

/* Observe every occurrence in input order with its own producer budget. */
bool cetta_prime_typing_observe_checking_bag_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidates, size_t count,
    CettaPrimeTypingCheckingBagV1 *bag_out);

bool cetta_prime_typing_checking_bag_v1_is_decision_complete(
    const CettaPrimeTypingCheckingBagV1 *bag);

/* Lossy public readout of semantic outcomes.  Engine faults have no status
 * readout and remain exclusively in the outer transport result. */
bool cetta_prime_typing_authority_observation_v1_status(
    const CettaPrimeTypingAuthorityObservationV1 *observation,
    CettaNikStatusV1 *status_out);

/* Exact multiset equality of term/type occurrences.  Resource limits are
 * experiment configuration, not candidate identity. */
bool cetta_prime_typing_checking_candidate_bag_equal_v1(
    Arena *scratch,
    const CettaPrimeTypingCheckingCandidateV1 *left, size_t left_count,
    const CettaPrimeTypingCheckingCandidateV1 *right, size_t right_count);

/* Returns NULL when the claim is not one of Prime's native type judgments. */
Atom *prime_semantics_judge_typing_direct(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

/* The same judgment authority with its actual resource account and retained
 * native term returned to a composing caller. No judgment is replayed. The
 * optional term output follows AuthorityObservation's established-only rule. */
Atom *prime_semantics_judge_typing_accounted(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingResourceObservationV1 *resources_out,
    Atom **canonical_term_out);

/* Check already lowered, closed syntax against the current declaration
 * schemas. Uninstantiated DeclConst occurrences receive fresh universe
 * arguments, exactly as in authored checking. Lambda annotations are kept.
 * A caller may supply a private declaration overlay, without new equations. */
CettaPrimeRegularKernelResult prime_semantics_check_declared_intrinsic_v1(
    Arena *arena, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget);

/* Collect the current logical view's type:rule atoms in kernel spelling.
 * Overlay snapshots and local removals are respected. This reads declarations;
 * it does not validate their typing or termination. NULL denotes no rules. */
Atom *prime_semantics_kernel_rules(Arena *arena, Space *space);

/* Reduce one saturated call by the space's admitted `type:rule` rules,
 * or project `fst`/`snd` of a pair, using the kernel normalizer. The call
 * must already have passed telescope checking. NULL leaves the call as it
 * is: the head has no computation, the budget ran out, or the normal form
 * has no authored spelling. Exhaustion is not a value and not a refutation. */
/* How many rule firings the last covered call reduced by this thread took:
 * one, or more for a definition computed to its normal form at once, or for
 * a call whose argument unfolded under its observation. */
uint64_t prime_semantics_covered_call_firings(void);

/* Whether the written sort `lower`, a universe `(u l)` or a sort above them
 * by name, lies inside the written sort `upper`: universes are cumulative,
 * and every universe an author writes lies inside `set`.  False for anything
 * that is not a closed written sort. */
bool prime_semantics_written_sort_within(Arena *arena, Atom *lower, Atom *upper);

Atom *prime_semantics_reduce_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel);

/* One admitted equation under telescope parameters. A symbol with no
 * declaration stays a parameter of the type; it is not entered as a global.
 * Ordinary execution keeps prime_semantics_reduce_covered_call. */
Atom *prime_semantics_reduce_open_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel);

/* The right side of the admitted rule of arity zero that defines `name`
 * (`set:define` of a constant with no arguments), in the authored spelling;
 * NULL when there is none. */
Atom *prime_semantics_unfold_covered_constant(Arena *arena, Space *space,
                                              Atom *name);

/* The normal form of a closed call by the space's admitted rules alone, in
 * the authored spelling, computed in the kernel within `steps` steps; no
 * oracle takes part.  NULL when there is none: no admitted rules, no
 * elaboration, no authored spelling, or the steps ran out (`*exhausted`). */
Atom *prime_semantics_rules_normal_form(Arena *arena, Space *space,
                                        Atom *call, uint64_t steps,
                                        bool *exhausted);

/* `(fst (pair a b))` and `(snd (pair a b))` as values. NULL if the call
 * is not that projection. */
Atom *prime_semantics_project_pair(Atom *call);

/* When `id:eliminate` is applied to reflexivity at the same point it
 * eliminates, the method is the result. NULL otherwise. */
Atom *prime_semantics_identity_iota(Atom *call);

/* `((lam binders body) a ...)`: the first binder takes the first argument,
 * the remaining binders keep their written domains, and the remaining
 * arguments apply to the result.  NULL when the head is not such a lambda. */
Atom *prime_semantics_beta(Arena *arena, Atom *call);

/* The seal of quoted code (atom_is_quotation).  The session sets it:
 * Prime seals unless its profile keeps quotations as written
 * (quote-as-written); HE and PeTTa never seal.  While it is active, the
 * canonical binders (let, chain, App of Lam) substitute only outside
 * quotations, except for pattern holes, the environment action
 * (prime_semantics_env_apply) never enters a quotation in value position,
 * and authored forms are sealed when they are elaborated
 * (prime_semantics_elaborate_form). */
void prime_quote_seal_set(bool active);
bool prime_quote_seal_active(void);

/* Lambda scope profiles (docs/prime/scope-policy-spectrum-20261006.md): an
 * elaboration property of a document, chosen by the top-level declaration
 * `(scope:profile OWNERSHIP [LIFETIME] [READOUT])`.  Ownership is decided
 * when a form is elaborated; every lambda or map-atom/foldl-atom template
 * that needs it records the profile, with its own slots, in its own list
 * `(OWN RECORD $y ...)`, and is applied by that record whatever the
 * caller's profile.  The default is lexical-fresh per-call reference. */
enum {
    CETTA_PRIME_OWNERSHIP_QUERY_WIDE = 0,
    CETTA_PRIME_OWNERSHIP_MERCURY_IMPLICIT = 1,
    CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH = 2,
    CETTA_PRIME_OWNERSHIP_EXPLICIT_CAPTURE = 3,
    CETTA_PRIME_OWNERSHIP_LEXICAL_INVENTORY = 4
};
enum {
    CETTA_PRIME_LIFETIME_PER_CALL = 0,
    CETTA_PRIME_LIFETIME_PER_CLOSURE = 1
};
enum {
    CETTA_PRIME_READOUT_REFERENCE = 0,
    CETTA_PRIME_READOUT_SNAPSHOT = 1
};
typedef struct {
    uint8_t ownership;
    uint8_t lifetime;
    uint8_t readout;
} CettaPrimeScopeProfile;

/* The profile of a document that declares none: mercury-implicit per-call
 * reference, or for the scope experiment's census and output comparison,
 * CETTA_PRIME_SCOPE_DEFAULT=ownership[/lifetime/readout] (an elaboration
 * default only; declarations and template records win). */
CettaPrimeScopeProfile prime_scope_profile_default(void);
/* The experiment's default, `ownership[/lifetime/readout]`; false when it
 * names no profile. */
bool prime_scope_profile_default_set(const char *text);
/* Set while the main document is read: the scope census counts its forms
 * only, and the document's own profile becomes the profile in force when
 * it runs. */
void prime_scope_document_reading(bool reading);
/* The profile a declaration `(scope:profile OWNERSHIP [LIFETIME]
 * [READOUT])` names; false for any other atom or an unknown name. */
bool prime_scope_profile_from_atom(const Atom *decl,
                                   CettaPrimeScopeProfile *out);
/* The profile in force while the program runs, under which code formed at
 * run time (parse) is elaborated: the main document's own profile, then each
 * query declaration `!(scope:profile ...)` evaluated, from its position on. */
void prime_scope_runtime_profile_set(CettaPrimeScopeProfile profile);
CettaPrimeScopeProfile prime_scope_runtime_profile(void);
/* The scope census (CETTA_PRIME_SCOPE_CENSUS, read by main.c): while the main
 * document is read, each form is elaborated under every ownership option
 * and counted, one line per option on standard output. The mode "plan"
 * instead emits the source migration plan; NULL or empty disables both. */
void prime_scope_census_set(const char *mode);

/* The binding structure of an authored form, computed once when the form is
 * formed and stored in the term (by the reader, or by parse while the
 * program runs, under the profile in force): each `$` parameter
 * of a lambda or of a map-atom/foldl-atom template becomes a slot of the
 * binder's own; each template's own names, and each written quotation's,
 * become its own slots, listed last in it (a quotation in value position is
 * a scope, and each opening of its code copies them); and a mention, inside
 * a quotation in value position, of a variable a pattern binds at a lower
 * quote depth becomes a variable of the quotation's own (the seal: a role
 * belongs to an occurrence; names in pattern positions, quoted or not, are
 * store-name occurrences).  NULL when the form cannot be elaborated. */
Atom *prime_semantics_elaborate_form(Arena *arena, Atom *form)
    __attribute__((weak));
/* A term built at run time from parts (cons-atom, union-atom).  Its root is
 * the node formed now: a lambda or template (or `new`) there gets its
 * binders' identities and its record, and owns none of its parts' names,
 * which keep the scope they were written in; its parts' lambdas are kept as
 * they were formed; a binder there is sealed as the reader seals one; a
 * quotation is code made of a value, whose lambdas keep the binding
 * structure they were formed with, and which owns nothing: its store
 * variables stay shared.  Any other term is returned as it is.  NULL on
 * failure. */
Atom *prime_semantics_form_built(Arena *arena, Atom *built)
    __attribute__((weak));
/* `*` on code: an opening.  The names a written quotation owns are copied
 * for it, and the payload is formed now: the lambdas written in the code
 * keep the ownership decided where they were written, and a value's lambda
 * in it keeps the record it entered the code with.  Contextual code opens as
 * the function of its binders.  NULL when `code` is no code value or cannot
 * be formed. */
Atom *prime_semantics_open_code(Arena *arena, Atom *code);
/* Prime's binding structure in the generic substitution (match.h): the
 * application of bindings, which every route shares, forms each expression
 * whose head it fills with a symbol (a binder sealed, in code too; a lambda
 * given its binder identities, outside code), and reads a variable inside
 * code as code (contextual code as its syntax).  The session sets them for
 * Prime (eval_swap_library_context). */
const BindingsStructureHooks *prime_semantics_binding_hooks(void);
/* Contextual code `(quote M (k ...))`: the code M under the binders k ...,
 * the binding quote's shape.  It is sealed as a quotation is. */
bool prime_semantics_contextual_code(const Atom *term);
/* Prime `==` on terms with binders: bound names compared by position,
 * never by spelling (a lambda's or template's parameters, an elaborated
 * template's own slots, a `new`'s names, in code as in values), as a quoted
 * pattern meets quoted code.  Captured names are compared as the runtime has
 * them.  False when neither term binds a name (the plain comparison
 * decides). */
bool prime_semantics_binders_eq(Arena *arena, Atom *left, Atom *right)
    __attribute__((weak));
/* Whether the pattern holds a quotation: matched against code binder by
 * binder (prime_semantics_code_canonical). */
bool prime_semantics_code_pattern(const Atom *pattern);
/* Whether the pattern takes code apart: a quotation in it holds an
 * expression.  A pattern whose quotations hold bare variables takes code
 * whole and matches binder by binder exactly as it matches as written. */
bool prime_semantics_pattern_takes_code_apart(const Atom *pattern);
/* Whether `term` is a binder form (let, let*, case, switch, unify, match,
 * chain, filter-atom) whose pattern positions take code apart. */
bool prime_semantics_binder_takes_code_apart(const Atom *term);
/* `value` matched against `pattern`, which takes code apart, binder by
 * binder into `bindings` (prime_semantics_code_canonical, then
 * prime_semantics_contextual_match): the operation `let`, `case` and
 * `unify` use, and equation heads and space queries use after their
 * candidates are selected.  *matched is false when they do not match, and
 * `bindings` is then unchanged.  False on failure. */
bool prime_semantics_code_unify(Arena *arena, Atom *pattern, Atom *value,
                                Bindings *bindings, bool *matched);
/* The form in which a quoted pattern is matched against quoted code: each
 * binder of quoted code (a lambda's or template's parameter, a `new`'s
 * name) and its references, as the binder's level, so binders meet by
 * identity, never by spelling.  On the pattern's side a variable at a
 * binder position stays a variable, and takes the code's binder.  NULL on
 * failure. */
Atom *prime_semantics_code_canonical(Arena *arena, Atom *term,
                                     bool pattern_side);
/* After `pattern` matched `value` (in the forms of
 * prime_semantics_code_canonical) with `bindings`: each hole of the pattern
 * (a variable inside a quotation in it) takes the part of the code it met,
 * as the code has it; a part under binders of the code that it mentions is
 * contextual code over those binders, the outermost first.  With `branch`,
 * the branch's quoted mentions of such a hole are replaced by the part's
 * syntax, which the quoted code around it puts back under binders of its
 * own.  *matched is false when a binder would escape its code (another
 * variable bound to it).  False on failure. */
bool prime_semantics_contextual_match(Arena *arena, Atom *pattern,
                                      Atom *value, Bindings *bindings,
                                      Atom **branch, bool *matched);
/* Why the last form could not be elaborated, when the elaboration says (a
 * name both shared and owned by one template, two whole-document profiles
 * that disagree); NULL otherwise. */
const char *prime_semantics_elaboration_error(void) __attribute__((weak));

/* Elaborate the forms a Prime reader produced, in place, each under the
 * profile its document declares before it.  False when a form cannot be
 * elaborated: the reader then refuses the document (fail closed). */
bool prime_semantics_elaborate_read_forms(TermUniverse *universe,
                                          AtomId *forms, size_t count);

/* One step of a Prime `map-atom` or `foldl-atom` whose template carries its
 * own list (an elaborated, literal template): the template is activated by
 * its record for each element (rule 3), in the order of the library
 * definitions it replaces.  The next instruction to evaluate, or a decline
 * (the call is then left to the library). */
typedef enum {
    PRIME_ITERATION_DECLINE = 0,
    PRIME_ITERATION_REENTER,
    PRIME_ITERATION_VALUE
} PrimeIterationStep;
PrimeIterationStep prime_semantics_iteration_step(Arena *arena, Atom *call,
                                                  Atom *space, Atom **out);

/* The one environment action: capture-avoiding substitution of the store
 * `store` over the binding structure.  It stops at a lambda binder, an own
 * name or a template parameter of the same variable (identity, never
 * spelling), never enters a quotation in value position while the session
 * seals, fills store names in pattern positions (quoted ones included), and
 * leaves a template's own names to its activation.  It is idempotent and
 * absorbs refinement: applying a store and then a refinement of it equals
 * applying the refinement.  The `_pattern` variant reads `term` as written
 * in a pattern position.  NULL on failure (memory or nesting). */
Atom *prime_semantics_env_apply(Arena *arena, const Bindings *store,
                                Atom *term);
/* A binder's own slot `var` filled with `value` in `term` (a let's body), by
 * lambda's own capture-avoiding substitution.  Inside a quotation, where the
 * elaboration left only the holes in scope of this binder, the slot is
 * filled as a hole of quoted code, textually.  NULL on failure. */
Atom *prime_semantics_subst_var(Arena *arena, Atom *term, Atom *var,
                                Atom *value);
Atom *prime_semantics_env_apply_pattern(Arena *arena, const Bindings *store,
                                        Atom *term);

/* `lift`, the named syntax operations on code values (prime_semantics.c).
 * `(lift let K @A @C)`: the code C with A in the slot K, by lambda's
 * capture-avoiding substitution; K may be a tuple of keys with a tuple of
 * codes.  `(lift app @F @A1 .. @An)`: the application code @(F A1 .. An).
 * Each returns the built code, or NULL when an argument is not a code value
 * or a key is not a binder key. */
Atom *prime_semantics_lift_let(Arena *arena, Atom *keys, Atom *codes,
                               Atom *code);
Atom *prime_semantics_lift_app(Arena *arena, Atom *const *codes,
                               size_t count);

/* Whether `term` is a meta-argument wrapper the core reads as a crossing
 * set, (meta C {...}) with C a lambda or template, a pattern binder or a
 * quotation: C then runs as itself (its elaboration applied the set). */
bool prime_semantics_meta_core(Arena *arena, Atom *term);

/* One evaluation of `(new ($h ...) body)`: fresh private slots for the
 * names, an instance of their own, in place throughout the body, which is
 * returned to run.  Its names are lexical binders of the body (the
 * elaboration gives them identities of their own), so the caller never
 * sees them and no spelling captures them.  NULL when the term is not such
 * a form (any other shape is data). */
Atom *prime_semantics_new_open(Arena *arena, Atom *term);

/* Open a recorded pattern scope using the lambda activation machinery. The
 * source is outside the activation; only its pattern and continuation own
 * local slots.  Unrelated forms are returned unchanged; NULL means failure. */
Atom *prime_semantics_pattern_activation(Arena *arena, Atom *term);
bool prime_semantics_has_scope_activation(const Atom *term);
/* Open a scope as a control term, retaining the caller's search continuation. */
Atom *prime_semantics_scope_activation(Arena *arena, Atom *term, Atom *space);

/* Whether `term` is an authored lambda `(lam binders body)` whose binders the
 * kernel's grammar reads.  Such a lambda is a value. */
bool prime_semantics_is_authored_lambda(Arena *arena, Atom *term);

/* One beta, pair projection, or reflexivity elimination at the root of an
 * authored term; subterms are not entered. The same pointer means nothing
 * changed. */
Atom *prime_semantics_authored_head_step(Arena *arena, Atom *term);

/* C-internal entry point for proof replay through a named NIK authority.
 * The judgment is exactly `(nik:check authority claim proof)`. */
Atom *prime_semantics_check_nik_direct(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

/* MeTTa-Prime is a language package, not an HE profile.  The weak hooks keep
 * standalone evaluator test binaries linkable when this module is omitted. */
Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) __attribute__((weak));
bool prime_semantics_is_op(const char *name) __attribute__((weak));
bool prime_semantics_is_op_id(SymbolId id) __attribute__((weak));
bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

/* Internal package boundary used by the Prime gate.  Construction returns
 * NULL if the resulting schema does not validate. */
Atom *prime_semantics_package_atom(Arena *a);
bool prime_semantics_validate_package(Atom *package);
/* Neutral named-telescope elaboration.  A non-NULL result is a closed
 * canonical Pi/Var ABT, not evidence that the source type is well formed. */
Atom *prime_semantics_canonicalize_type(Arena *a, Atom *type);
bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out);

/* The represented query a `type:eq` or `type:check` judgment poses to the
 * kernel, without the kernel deciding it. */
Atom *prime_semantics_kernel_query(
    Arena *a, Space *space, Atom *judgment, bool steps_limited,
    uint64_t steps);

/* Published theorems are hypothetical judgments G |- P.  The context of one
 * successful checking judgment, `(PrimeTheoremContextV1 dependency ...)`:
 * the space declarations `(: name type)` of the declared route's class and
 * the admitted rules at those heads.  Empty unless the judgment was decided
 * on the declared route; NULL when that route's class cannot be read. */
Atom *prime_semantics_theorem_context(
    Arena *a, Space *space, Atom *judgment, bool declared_route);

/* Record the contexts of a theorem just published in `space`, one per
 * accepted proof.  While none of them is available, the kernel does not
 * resolve the theorem's name; the statement is never rewritten. */
void prime_semantics_theorem_record(
    Space *space, Atom *name, Atom *type,
    Atom *const *contexts, size_t context_count);

/* A space that comes to hold another's contents holds its theorem records. */
void prime_semantics_theorem_ledger_follow(uint64_t from, uint64_t to);

#endif /* CETTA_PRIME_SEMANTICS_H */
