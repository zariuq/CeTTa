#ifndef CETTA_PETTA_TYPE_POLICY_H
#define CETTA_PETTA_TYPE_POLICY_H

#include "atom.h"
#include "eval_completion.h"
#include "space.h"

/* PeTTa call typing, not an evaluator.  Classify the literal declaration
 * before any argument runs: binding a formal variable to Atom later must
 * not change an argument into held source.  All views borrow their atoms. */
typedef enum {
    PETTA_TYPE_RAW,
    PETTA_TYPE_TRANSLATE,
    PETTA_TYPE_TRANSLATE_AND_GUARD,
} PettaTypeDemand;

typedef struct {
    Atom *signature;
    CettaExprLen arity;
    Atom *result_type;
    bool guard_result;
    bool hold_result;
    /* Compact literal modes for the leading word of arguments. Larger
     * arities classify the remaining literal domains without a language
     * limit. These fields contain no borrowed pointers or binding state. */
    uint64_t raw_arguments;
    uint64_t translated_arguments;
    bool compiled_demands;
} PettaTypeCall;

typedef struct {
    Atom *formal;
    Atom *binder; /* optional profile-enabled telescope binder */
    PettaTypeDemand demand;
} PettaTypeArgument;

bool petta_type_call_open(Atom *signature, CettaExprLen arity,
                          PettaTypeCall *call);
/* Planning consumes the supplied prefix. Intrinsic application inference,
 * in contrast, requires the full arrow arity (call_open above). */
bool petta_type_call_plan(Atom *signature, CettaExprLen supplied,
                          PettaTypeCall *call);
bool petta_type_call_compile(Atom *signature, PettaTypeCall *call);
/* One fresh signature preserves sharing between domains and codomain.
 * The activation owns every term and mode; it does not borrow cache storage. */
bool petta_type_call_instantiate(Arena *arena, const PettaTypeCall *source,
                                 PettaTypeCall *call);
/* argument is a one-based signature index in 1..call->arity. */
PettaTypeArgument petta_type_call_argument(const PettaTypeCall *call,
                                           CettaExprIndex argument,
                                           bool dependent_domains);
bool petta_type_body_is_data(Atom *const *types, uint32_t count);
PettaTypeDemand petta_type_demand(const Atom *literal_formal);
PettaTypeArgument petta_type_argument(Atom *domain, bool dependent_domains);
bool petta_type_split_domain(Atom *domain, Atom **binder, Atom **formal);
/* Stable variant uniqueness of declarations, not uniqueness of stored
 * atoms or answers.  Compacts a caller-owned array without changing atoms. */
uint32_t petta_type_unique_signatures(Atom **types, uint32_t count);

/* The base guard is (get-type(Value, Required) *-> true ;
 *                    get-metatype(Value, Required)).
 * Search executes these queries with Required as its output operand.
 * An exact answer disables fallback, but does not discard exact answers.
 * Only normal, answerless exhaustion permits the fallback query. */
typedef enum {
    PETTA_TYPE_QUERY_EXACT,
    PETTA_TYPE_QUERY_METATYPE,
} PettaTypeQuery;
Atom *petta_type_query(Arena *arena, Atom *value, PettaTypeQuery query);
/* Positive, allocation-free intrinsic evidence only. Use it when no
 * authored get-type equation adds answers; false means defer, not reject. */
bool petta_type_literal_proves(const Atom *value, const Atom *required);

/* Relational compound queries use these patterns on the caller's existing
 * match/query continuations. They must not enumerate fresh types and filter
 * afterwards: the subject and requirement can both acquire bindings. */
typedef struct {
    Atom *pattern;
    Atom *domains;
} PettaTypeFunctionQuery;
bool petta_type_function_query(Arena *arena, Atom *sequence, Atom *required,
                                PettaTypeFunctionQuery *query);
Atom *petta_type_tuple_query(Arena *arena, CettaExprLen length);
Atom *petta_type_declaration_query(Arena *arena, Atom *subject, Atom *required);

/* Intrinsic get-type without interpretation or nominal HE casts. The caller
 * may admit open compound subjects when declarations have only symbol
 * subjects and no nested authored classifier can execute. The answer array
 * cannot carry subject refinements or classifier effects; those queries use
 * the caller's ordinary relational continuations instead.
 * Required is NULL for a fresh output, otherwise its current binding.
 * Atoms belong to arena; the returned array belongs to the caller.
 * False reports a service failure, never ordinary relational rejection.
 * Authored get-type equations are executed separately by ordinary search. */
bool petta_type_intrinsic_answers(Space *space, Arena *arena,
                                  Atom *subject, Atom *required,
                                  Atom ***types, uint32_t *count,
                                  CettaEvalCompletion *completion);
/* Release this thread's bounded intrinsic facts before its evaluator and
 * hash-cons ownership domain are torn down. Returned answers own their atoms
 * in the request arena and do not borrow these retained schemes. Ground
 * facts may borrow those request-owned answers until the next arena reset;
 * no cache operation extends the request arena's lifetime. */
void petta_type_facts_free_for_current_thread(void);

#endif
