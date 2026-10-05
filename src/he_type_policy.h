#ifndef CETTA_HE_TYPE_POLICY_H
#define CETTA_HE_TYPE_POLICY_H

#include "atom.h"
#include "match.h"

/* HE interpretation demand is a judgment about source syntax and its
 * expected type. It precedes execution; it is not PeTTa's post-value guard. */
typedef enum {
    HE_TYPE_KEEP,
    HE_TYPE_CAST,
    HE_TYPE_INTERPRET,
} HeTypeDemand;

HeTypeDemand he_type_demand(const Atom *subject, Atom *expected,
                            Atom *metatype);

/* A borrowed arrow view. Freshening and ownership belong to the existing
 * application continuation. Mode arrows are not ordinary HE function types. */
typedef struct {
    Atom *signature;
    CettaExprLen arity;
    Atom *result_type;
} HeTypeCall;

bool he_type_call_open(Atom *signature, HeTypeCall *call);

typedef struct {
    Atom *binder;
    Atom *formal;
} HeTypeDomain;

/* The base language has no telescope binder. The caller supplies the active
 * profile capability rather than giving this module a mutable session. */
HeTypeDomain he_type_domain(Atom *domain, bool dependent_telescope);

typedef enum {
    HE_TYPE_ARGUMENT_ANY,
    HE_TYPE_ARGUMENT_METATYPE,
    HE_TYPE_ARGUMENT_INFER,
} HeTypeArgumentCheck;

HeTypeArgumentCheck he_type_argument_check(Atom *expected);
Atom *he_type_result_demand(Arena *arena, Atom *codomain);

/* HE's grounded operation types and library declarations: immutable
 * language data, built once per process and shared by every session.
 * Lookups build them on first use; false reports tables that failed to
 * load. */
bool he_library_tables_init(void);
/* HE's grounded operations type themselves, as upstream's grounded atoms do
 * (Grounded::type_): the type is not a declaration atom in any space, and a
 * declaration a program writes for the name does not change it.  The types
 * of a grounded operation's name, or 0 for any other symbol; `extensions`
 * adds CeTTa's extended signatures, which the profile with upstream's exact
 * semantics leaves out. */
uint32_t he_grounded_symbol_types(SymbolId symbol, bool extensions,
                                  Atom *const **types);

/* The declarations of HE's standard library, in upstream's order, parsed by
 * he_library_tables_init.  In HE profiles they are the declarations of the
 * library module &self imports. */
size_t he_library_declaration_count(void);
Atom *he_library_declaration_at(size_t index);
/* Whether a CeTTa library declaration with this subject concerns a name of
 * upstream's library: one upstream declares, or one it defines or uses
 * without declaring. */
bool he_library_names_subject(const Atom *subject);
/* Whether upstream declares `subject` with an arrow of this arity; a CeTTa
 * signature of another arity extends the name's call forms. */
bool he_library_declares(const Atom *subject);
bool he_library_declares_arity(const Atom *subject, CettaExprLen arity);

/* Interpreter type matching treats root Atom and Undefined symmetrically as
 * wildcards, then performs ordinary atom matching, not recursive wildcard
 * matching. Structural inference has its own rules; do not substitute this
 * relation for its evidence checks. Matching/refinement uses the existing
 * binding store and trail, not a second unifier or a Boolean answer cache. */
bool he_type_is_wildcard(const Atom *type);
bool he_type_refine(Atom *actual, Atom *expected, Bindings *bindings,
                     Arena *arena);
bool he_type_refine_builder(Atom *actual, Atom *expected,
                             BindingsBuilder *builder, Arena *arena);

/* Applicability folds source arguments without executing them. Inference is
 * supplied by the existing type service; matching uses its existing trail.
 * Callers keep evaluation, overload selection, GC and outcome publication. */
typedef struct {
    Atom **items;
    uint32_t len, cap;
    Atom *inline_items[8];
} HeTypeErrors;

typedef struct {
    Atom **items;
    uint32_t len, cap;
    Atom *inline_items[4];
} HeTypeContracts;

void he_type_errors_init(HeTypeErrors *errors);
void he_type_errors_free(HeTypeErrors *errors);
bool he_type_errors_push(HeTypeErrors *errors, Atom *error);
void he_type_contracts_init(HeTypeContracts *contracts);
void he_type_contracts_free(HeTypeContracts *contracts);
bool he_type_contracts_push_unique(HeTypeContracts *contracts, Atom *type);

typedef struct {
    void *context;
    /* The array is caller-owned; atoms belong to the application arena.
     * Callbacks must keep borrowed application terms stable. A service
     * doing type-level evaluation must prevent relocation of fold locals. */
    uint32_t (*infer)(void *context, Atom *subject, Atom ***types);
    /* Optional profile-owned normalization of an instantiated domain or
     * codomain before refinement. False retains an incomplete judgment;
     * it must not be turned into a type refutation. */
    bool (*normalize)(void *context, Atom *type, Atom **normalized);
    /* Optional representation projection, never an evaluation request. */
    Atom *(*source_argument)(void *context, Atom *argument);
    /* A refutation service may only reject established impossible pairs. */
    bool (*refuted)(void *context, Atom *actual, Atom *expected);
    /* NULL uses HE interpreter refinement. Non-HE clients of the shared
     * applicability mechanism supply their own relation explicitly. */
    bool (*refine)(Atom *actual, Atom *expected, BindingsBuilder *, Arena *);
    /* Optional observation of an accepted instantiated codomain together
     * with its refinement. It runs before the fold releases its path. */
    bool (*accepted)(void *context, Atom *codomain, const Bindings *refinement);
    bool first_applicable;
    /* HE treats metatype recognition as an early success; its mismatch
     * still permits inferred declared types. Other clients may reject it. */
    bool infer_after_metatype_mismatch;
} HeTypeApplicationServices;

typedef enum {
    HE_TYPE_APPLICABLE,
    HE_TYPE_INAPPLICABLE,
    HE_TYPE_APPLICATION_INCOMPLETE,
} HeTypeApplicability;

HeTypeApplicability he_type_call_applicable(
    Arena *arena, Atom *expression, Atom *signature, Atom *expected_result,
    bool dependent_telescope, const HeTypeApplicationServices *services,
    HeTypeErrors *errors, HeTypeContracts *contracts);

#endif
