#ifndef CETTA_PRIME_ORDINARY_OWNER_H
#define CETTA_PRIME_ORDINARY_OWNER_H
#include "eval.h"

typedef struct PrimeNativeTopEpisode CettaPrimeSourceOwner;
typedef struct CettaPrimeSourceScope CettaPrimeSourceScope;

/* A source instance owns one admitted immutable snapshot and its services.
 * Capture is explicit and may be amortized across ordinary calls. The input
 * source, registry and current context must be live and quiescent through
 * capture. The enclosing snapshot owns its world, registry, context and
 * arenas; process SymbolTable and VarInternTable remain caller-held, with
 * their original identities, until every source scope/outcome is disposed.
 * Capture limits cover the region preparation, not a complete retained
 * execution-context census. A refusal grants no execution or reclamation
 * authority. */
CettaContinuationStatus eval_prime_source_owner_capture(
    Space *, Registry *, Atom *, size_t, size_t, CettaPrimeSourceOwner **);
void eval_prime_source_owner_retain(CettaPrimeSourceOwner *);
void eval_prime_source_owner_release(CettaPrimeSourceOwner *);

/* A scope selects the actual retained services exclusively. Calls use the
 * ordinary public evaluator. Mutable input services may not be changed in
 * this scope or while an outcome retains them. Tokens test currency; the
 * counted owner supplies lifetime. Scope close does not cancel outcomes.
 * allowance bounds controller work, independently of language fuel. The
 * initial retained profile admits unlimited language fuel and >=2 workers;
 * other admitted source calls keep their ordinary synchronous behavior.
 * A wrong source/service tuple is INVALIDATED before evaluation. Foreign
 * roots without captured ownership retain synchronous caller ownership;
 * this API does not grant them a retained or parallel execution licence. */
CettaContinuationStatus eval_prime_source_scope_open(CettaPrimeSourceOwner *,
    uint64_t allowance, CettaPrimeSourceScope **);
void eval_prime_source_scope_close(CettaPrimeSourceScope *);
bool eval_prime_source_scope_inputs(CettaPrimeSourceScope *, Space **,
    Registry **, Arena **, Atom **);

/* The outcome is exclusively held, and its receiving arena remains live
 * through resume. Disposal is quiescent and may follow receiver retirement.
 * All slices join before return; retaining an owner is not permission for
 * simultaneous mutation or observation of the same episode. */
#endif
