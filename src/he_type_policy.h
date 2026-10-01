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
    /* Optional representation projection, never an evaluation request. */
    Atom *(*source_argument)(void *context, Atom *argument);
    /* A refutation service may only reject established impossible pairs. */
    bool (*refuted)(void *context, Atom *actual, Atom *expected);
    /* NULL uses HE interpreter refinement. Non-HE clients of the shared
     * applicability mechanism supply their own relation explicitly. */
    bool (*refine)(Atom *actual, Atom *expected, BindingsBuilder *, Arena *);
    /* The remaining services are optional; NULL keeps HE's reading. They
     * serve a client whose telescope and type equality are richer. */
    /* Reads one domain. A binder that is not a variable is a name: it is
     * bound by `instantiate`, never by the binding store. */
    HeTypeDomain (*domain)(void *context, Atom *domain);
    /* The type with the names bound so far replaced by the source arguments
     * supplied for them. Required when `domain` can return a name. */
    Atom *(*instantiate)(void *context, Arena *arena, Atom *type,
                         Atom **names, Atom **arguments, size_t count);
    /* A formal that takes its argument as written, as Atom and %Undefined%
     * do. */
    bool (*takes_source)(void *context, Atom *expected);
    /* Consulted when refinement fails. It decides whether two types are
     * equivalent and learns no substitution. */
    bool (*equivalent)(void *context, Atom *actual, Atom *expected);
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
