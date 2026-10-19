#ifndef CETTA_PRIME_ORDINARY_OWNER_H
#define CETTA_PRIME_ORDINARY_OWNER_H
#include "eval.h"
#include "owned_execution.h"

typedef struct PrimeNativeTopEpisode CettaPrimeSourceOwner;
typedef struct CettaPrimeSourceScope CettaPrimeSourceScope;

typedef enum {
    CETTA_PRIME_SOURCE_IMMUTABLE = 0,
    CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,
} CettaPrimeSourceProfile;

/* One existing native observer specification. Node/byte grants are per
 * selection; selections bounds the retained table and group slice grants,
 * while owners bounds the group's independent reader links.
 * enabled starts or resumes recording; stop ends future recording. */
typedef struct {
    bool enabled;
    bool stop;
    size_t selections, nodes, bytes, owners;
} PrimeNativeHistorySpec;

/* A source instance owns one admitted captured program and its services.
 * The default profile also keeps its data world immutable.
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
/* The private-data profile additionally admits ordinary match/get-atoms and
 * add/remove operations in the captured world. Returned code and indirect
 * calls use the existing evaluator and captured logical environment; each
 * resolved operation is checked against the same private services before
 * dispatch. Opaque callable owners and external services are not admitted.
 * A shallow, immutable program
 * Space shares captured term storage with that mutable world; &self keeps
 * naming the data world. Source-scope inputs expose the program Space.
 * Further execution requires that original program, registry and services.
 * Only an actual own data
 * publication advances its execution guard; external changes invalidate it.
 * Query cursors and earned values retain their original observations.
 * This selects ownership of one private world, not external I/O authority. */
CettaContinuationStatus eval_prime_source_owner_capture_profile(
    Space *, Registry *, Atom *, CettaPrimeSourceProfile,
    size_t, size_t, CettaPrimeSourceOwner **);
/* Select an explicit host Prime context only during capture. The captured
 * evaluator owns its context and uses native work grants rather than the
 * caller's language fuel. Context/registry selection and the caller's fuel
 * are restored before return. This performs no source evaluation and has no
 * synchronous fallback. The host serializes the supplied context. */
CettaContinuationStatus eval_prime_source_owner_capture_context(
    CettaLibraryContext *, Space *, Registry *, Atom *, CettaPrimeSourceProfile,
    size_t, size_t, CettaPrimeSourceOwner **);
void eval_prime_source_owner_retain(CettaPrimeSourceOwner *);
void eval_prime_source_owner_release(CettaPrimeSourceOwner *);
/* An eligibility observation under a live source share and its original
 * intern contexts. It neither selects services nor grants execution rights.
 * Hosts serialize source use and recheck before buffered publication when
 * their delivery policy requires the original source to remain current. */
bool eval_prime_source_owner_is_current(const CettaPrimeSourceOwner *);

/* Open the same retained ordinary evaluator as a movable return provider.
 * Admission retains its first task without performing source work. Quantum
 * is a positive, finite controller-work grant per provider invocation;
 * sequencing allowance instead counts accepted return occurrences. A pending
 * loan is retained across refusal and move without another provider call.
 * The destination is initialized and empty. Success owns the computation
 * until cancellation, independently of this source share and caller arenas.
 * Calls and callbacks are serialized. The source's intern contexts remain
 * live. Shared admission and publication must supply their own compatibility
 * gate and complete return capture; a loan does not confer those permissions.
 * Unsupported admission performs no synchronous evaluation fallback. */
CettaContinuationStatus eval_prime_source_execution_open(
    CettaPrimeSourceOwner *, uint64_t quantum, CettaOwnedExecution *);

/* Select conserved finite work before this fresh provider has run. Reserve
 * amount from the caller's existing cell; refusal changes neither owner.
 * The private lease moves with the execution, including through a shared
 * producer. Actual controller work spends it, even on failure or suspension;
 * cancellation returns only unspent credit. Pending delivery does not repeat
 * producer work. Preparation, capture, delivery and service time have separate
 * accounts. NULL payer leaves the ordinary unmetered provider selected. No
 * per-step credit check is added to that ordinary provider. */
CettaContinuationStatus eval_prime_source_execution_fund(
    CettaOwnedExecution *, CettaWorkCredit *payer, uint64_t amount);
/* Transfer a later tranche to the same funded, nonterminal provider. The
 * supplied payer belongs to its bank; refusal changes neither cell. Paid
 * work, the complete residual, history and a pending loan remain untouched.
 * Unused donated credit returns to the original reservation parent on close.
 * This does not replenish a host's independently selected total-work limit. */
bool eval_prime_source_execution_add_credit(
    const CettaOwnedExecution *, CettaWorkCredit *payer, uint64_t amount);

/* Read-only source/service currency check for this actual quiescent driver.
 * It also applies after movement into a host task. A host combines it with
 * its current publication permission before progress and buffered delivery.
 * False neither cancels the computation nor erases an earned loan. */
bool eval_prime_source_execution_is_current(const CettaOwnedExecution *);
/* Quiescent, serialized adapter operations. Work reads the existing native
 * controller counter, not source semantic cost or callback time. A finite
 * quantum configures the next provider poll without advancing it; zero may
 * expose an already earned return/completion but performs no controller work.
 * Neither operation replenishes a host's separate total-work purse. */
bool eval_prime_source_execution_work(const CettaOwnedExecution *, uint64_t *);
bool eval_prime_source_execution_set_quantum(const CettaOwnedExecution *, uint64_t);

/* These operations require the actual live source driver, including after
 * movement into the shared producer. Calls are serialized and the provider
 * is quiescent. They never advance source work or acknowledge a pending loan.
 * Late recording starts a new suffix; stopping keeps its readable history.
 * Forgetting releases this recorder's payload share. Independent captured
 * readers keep theirs. Configuration/inspection are explicitly paid work.
 * The projected History reports bounds, omissions and its native work
 * interval; it does not certify source correspondence or search exhaustion.
 * Recording-disabled builds decline these optional C observer operations. */
CettaContinuationStatus eval_prime_source_execution_record(
    const CettaOwnedExecution *, const PrimeNativeHistorySpec *);
CettaContinuationStatus eval_prime_source_execution_forget(
    const CettaOwnedExecution *);
CettaContinuationStatus eval_prime_source_execution_history(
    const CettaOwnedExecution *, Arena *, Atom **);

/* A scope selects the actual retained services exclusively. Calls use the
 * ordinary public evaluator. Hosts may not change exposed services in this
 * scope or while an outcome retains them. Admitted private-world source
 * actions instead publish their own data changes. Tokens test currency; the
 * counted owner supplies lifetime. Scope close does not cancel outcomes.
 * allowance bounds controller work, independently of language fuel. The
 * retained profile admits an unobserved top-level call with unlimited
 * language fuel and an independent receiving arena. The root retains its
 * first task before evaluation, including at allowance zero, for the
 * selected worker capacity.
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
