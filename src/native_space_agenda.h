#ifndef CETTA_NATIVE_SPACE_AGENDA_H
#define CETTA_NATIVE_SPACE_AGENDA_H

#include "space.h"
#include "storage_census.h"
#include "search_machine.h"

/* A provider for the evaluator's owned continuation hub. It has no run loop,
 * thread pool or independent scheduler. A step advances one retained scan,
 * matcher node grant, sink, validation or publication obligation. */
typedef struct CettaSpaceAgenda CettaSpaceAgenda;
typedef struct CettaSpaceAgendaMatch CettaSpaceAgendaMatch;
typedef struct CettaSpaceAgendaDiscovery CettaSpaceAgendaDiscovery;
typedef struct CettaSpaceAgendaSelection CettaSpaceAgendaSelection;

typedef struct {
    bool set_support;
    bool raw_mm2_order;
    bool explicit_inputs;
    bool snapshot_worlds;
} CettaSpaceAgendaContract;

typedef enum {
    CETTA_SPACE_AGENDA_PROGRESS,
    CETTA_SPACE_AGENDA_COMMITTED,
    CETTA_SPACE_AGENDA_CLOSED,
    CETTA_SPACE_AGENDA_PENDING,
    CETTA_SPACE_AGENDA_RETRY,
    CETTA_SPACE_AGENDA_FORK,
    CETTA_SPACE_AGENDA_WORLD,
} CettaSpaceAgendaStep;

/* These are realized contracts, not the final universe of resource policies.
 * MM2 uses set support and compact-expression byte selection. Transactions
 * retain bag occurrences and read/take/all/absent input annotations. */
CettaSpaceAgenda *cetta_space_agenda_new(Space *space, Arena *owner,
    const Bindings *environment, CettaSpaceAgendaContract contract);
void cetta_space_agenda_free(CettaSpaceAgenda *agenda);
/* Advice is retained by the native evaluator. The provider borrows its
 * identity; evaluating and updating it uses the evaluator's ordinary Need
 * continuations. Neither callback grants a resource operation. */
typedef void *(*CettaSpaceAgendaAdvise)(void *context,Atom *exec,
    const Bindings *environment,Atom *authored_priority);
typedef bool (*CettaSpaceAgendaBefore)(void *context,const void *left,const void *right);
bool cetta_space_agenda_control(CettaSpaceAgenda *agenda,
    const CettaSelectionAutomaton *schedule,CettaSpaceAgendaAdvise advise,
    CettaSpaceAgendaBefore before,void *context);
bool cetta_space_agenda_has_advice(const CettaSpaceAgenda *agenda,const void *advice);
/* Ordinary flat native Transactions match against the caller-held source
 * without building private read copies. Every matching grant checks its
 * captured read; a changed source restarts that matching operation. The
 * complete replacement is still built privately and published atomically.
 * Other storage profiles retain the copied read contract. */
CettaSpaceAgendaStep cetta_space_agenda_step(CettaSpaceAgenda *agenda);
/* Prepare one physical source occurrence under the provider's contract.
 * A Transaction enters its existing matching phases. Raw MM2 retains only
 * the activation, before directive removal or input matching. Choosing that
 * occurrence is explicit strategy control; ordinary MM2 keeps least-key
 * selection. The provider must be fresh and the supplied read current.
 * Snapshot-world preparation is separate. No live mutation is performed. */
CettaSpaceAgendaStep cetta_space_agenda_prepare_source(CettaSpaceAgenda *agenda,
    SpaceReadToken source, CettaIndex index);
/* Capture only the ready matching operation, including the physical row and
 * its candidate environment. The provider retains the operation through
 * refusal or suspension. A run advances only the canonical inner matcher,
 * never the physical row cursor or a commit. Its paid progress/result must
 * be installed by the coordinator before another grant can run. */
CettaSpaceAgendaMatch *cetta_space_agenda_match_capture(CettaSpaceAgenda *agenda);
/* True while paid progress/result still awaits coordinator installation,
 * including a refused binding promotion. This query performs no match work. */
bool cetta_space_agenda_match_prepared(const CettaSpaceAgenda *agenda);
enum { CETTA_SPACE_AGENDA_MATCH_NODES = 32 };
/* Zero is inert. Node grants count actual worklist nodes, not bytes or CPU
 * inside equality, occurs checking or a binding/copy primitive. */
bool cetta_space_agenda_match_run(CettaSpaceAgendaMatch *match, uint64_t node_budget);
/* Snapshot alternatives are issued once to the enclosing native hub. The
 * root owns their storage; acceptance transfers only the ready-work edge. */
CettaSpaceAgenda *cetta_space_agenda_child(CettaSpaceAgenda *agenda);
bool cetta_space_agenda_accept_child(CettaSpaceAgenda *agenda,
                                    CettaSpaceAgenda *child);
void *cetta_space_agenda_advice(const CettaSpaceAgenda *agenda);
bool cetta_space_agenda_current(const CettaSpaceAgenda *agenda);
bool cetta_space_agenda_ready(const CettaSpaceAgenda *agenda);
/* Borrow the selected source of a current prepared Transaction publication,
 * in its live or private snapshot world. The originating snapshot must remain
 * current. The caller keeps the provider quiescent and holds the shared
 * transition guard through inspection. NULL is not an exhaustion judgment or
 * permission to publish. Reading this source advances no provider phase. */
Atom *cetta_space_agenda_publication_source(const CettaSpaceAgenda *agenda);
/* Optional control before a raw MM2 directive leaves the live store. A
 * refused gate retains the chosen directive and exact source read. Turning
 * the gate off resumes that choice only while the read is still current.
 * No diary is installed. Ordinary operations do not test this scope. */
typedef bool (*CettaSpaceAgendaActivationPermit)(void *, CettaSpaceAgenda *);
typedef struct CettaSpaceAgendaActivationScope {
    CettaSpaceAgenda *agenda;
    CettaSpaceAgendaActivationPermit permit;
    void *context;
    struct CettaSpaceAgendaActivationScope *previous;
    bool entered, notifying;
} CettaSpaceAgendaActivationScope;
bool cetta_space_agenda_activation_enter(CettaSpaceAgendaActivationScope *,
    CettaSpaceAgenda *, CettaSpaceAgendaActivationPermit, void *);
void cetta_space_agenda_activation_leave(CettaSpaceAgendaActivationScope *);
/* A borrowed inspection view, not permission to remove the directive. */
Atom *cetta_space_agenda_activation_source(const CettaSpaceAgenda *);
void cetta_space_agenda_resume(CettaSpaceAgenda *agenda);
/* The returned syntax is owned by the enclosing evaluator arena. */
Atom *cetta_space_agenda_receipt(CettaSpaceAgenda *agenda);
Atom *cetta_space_agenda_pending(CettaSpaceAgenda *agenda);
void cetta_space_agenda_storage(const CettaSpaceAgenda *agenda,
                               CettaStorageCensus *census);

/* Requested discovery over one captured live source. Each grant
 * scans one physical occurrence or advances one existing preparation phase;
 * a matcher grant retains its ordinary node bound. Initialization performs
 * no scan. A zero-grant request must not call step. This is not a wall-time
 * bound on copying, equality or other individual native primitives.
 *
 * Unchanged ordinary native atom spaces supply borrowed reads during
 * discovery, avoiding per-candidate world copies. Other admitted spaces keep
 * copied preparation. Source currency is checked before and after each grant.
 * Taking a Transaction choice ends the borrow; execution uses ordinary
 * revalidation. Prepared alternatives retain their validated proposal and
 * release temporary read copies. Raw MM2 alternatives retain a current
 * activation only, with no computed outputs or matching dependencies. Their
 * exact source read is required before removal, and existing MM2 set support
 * applies to execution. Discovery never mutates live storage. Equal executable
 * atoms have distinct physical selectors. Exhaustion belongs to the captured
 * source; any source revision change invalidates further enumeration.
 * The caller holds the source Space and arena until discovery and every
 * transferred selection are freed. */
typedef enum {
    CETTA_SPACE_CHOICE_TRANSACTION_COMMIT,
    CETTA_SPACE_CHOICE_MM2_ACTIVATION,
    CETTA_SPACE_CHOICE_TRANSACTION_MATCH,
    CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH,
} CettaSpaceAgendaChoiceKind;
typedef struct {
    CettaSpaceAgendaChoiceKind kind;
    uint64_t ticket;
    CettaIndex source_index;
    Atom *source, *outputs, *dependencies;
    bool current;
} CettaSpaceAgendaChoice;
typedef enum {
    CETTA_SPACE_DISCOVERY_PROGRESS,
    CETTA_SPACE_DISCOVERY_CHOICE,
    CETTA_SPACE_DISCOVERY_EXHAUSTED,
    CETTA_SPACE_DISCOVERY_INCOMPLETE,
    CETTA_SPACE_DISCOVERY_INVALIDATED,
} CettaSpaceAgendaDiscoveryStep;
CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new(Space *, Arena *,
    const Bindings *);
/* Capture the input environment into owned storage. The caller's environment
 * and its arena can retire after construction. Each proposal has independent
 * storage and promoted captures. A taken
 * selection remains alive after discovery and its capture arena retire.
 * Views and receipts borrow the proposal owner; copy them before freeing it.
 * The source Space and its runtime services must still remain alive. */
CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new_owned(Space *,
    const Bindings *);
/* Select this discovery contract once. Transaction match discovery retains
 * the existing join cursor and offers every complete physical input binding
 * separately. Each prepared row owns its captures independently; taking it
 * revalidates that exact row, never a replacement binding. Ordinary
 * construction above retains the first-binding Transaction contract.
 * World-match discovery offers the same exact tuples. Taking one requires
 * its original source read throughout execution, revalidates that tuple,
 * and publishes only into an owned private world. The live source is never
 * consumed; the existing world provider continues the selected program.
 * Unsupported kinds refuse without scanning. */
CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new_owned_kind(Space *,
    const Bindings *, CettaSpaceAgendaChoiceKind);
/* Borrow the original captured context while discovery remains held. This
 * neither recaptures a later caller nor grants execution authority. */
const Bindings *cetta_space_agenda_discovery_captured(const CettaSpaceAgendaDiscovery *);
void cetta_space_agenda_discovery_free(CettaSpaceAgendaDiscovery *);
CettaSpaceAgendaDiscoveryStep cetta_space_agenda_discovery_step(
    CettaSpaceAgendaDiscovery *, CettaSpaceAgendaChoice *);
bool cetta_space_agenda_discovery_current(const CettaSpaceAgendaDiscovery *);
const char *cetta_space_agenda_discovery_pending(const CettaSpaceAgendaDiscovery *);
/* Views borrow data from the quiescent owner. A ticket is only a local
 * selector. Taking it transfers its provider exactly once; it neither grants
 * authority nor bypasses ordinary validation/publication. The recipient must
 * use that retained provider under its current invocation permissions. */
bool cetta_space_agenda_discovery_choice(const CettaSpaceAgendaDiscovery *,
    uint64_t ticket, CettaSpaceAgendaChoice *);
CettaSpaceAgendaSelection *cetta_space_agenda_discovery_take(CettaSpaceAgendaDiscovery *,
    uint64_t ticket);
void cetta_space_agenda_discovery_storage(const CettaSpaceAgendaDiscovery *,
    CettaStorageCensus *);
/* A selected Transaction proposal publishes at most once. Revalidation may retain the
 * same proposal after an unrelated change; a changed binding or dependency
 * refuses this selection instead of silently selecting another interaction.
 * A raw MM2 selection activates at most once under its exact captured read;
 * matching and publication remain subsequent ordinary provider operations.
 * A world-match selection publishes its first step privately, then continues
 * through the existing world provider. Separate worlds do not grant two
 * consumes against the live source. Parent edits invalidate their source.
 * Current invocation authority remains the native caller's prerequisite. */
CettaSpaceAgendaStep cetta_space_agenda_selection_step(CettaSpaceAgendaSelection *);
const char *cetta_space_agenda_selection_pending(const CettaSpaceAgendaSelection *);
Atom *cetta_space_agenda_selection_receipt(CettaSpaceAgendaSelection *);
void cetta_space_agenda_selection_free(CettaSpaceAgendaSelection *);
void cetta_space_agenda_selection_storage(const CettaSpaceAgendaSelection *,
    CettaStorageCensus *);
/* Execute the selected interaction through the ordinary provider interface,
 * then continue its remaining program. The scope borrows the owner for one
 * synchronous evaluator run and composes with requested observation. A second
 * use cannot acquire the same selection while this borrow is active. */
typedef struct CettaSpaceAgendaSelectionScope {
    CettaSpaceAgendaSelection *selection;
    struct CettaSpaceAgendaSelectionScope *previous;
    bool entered;
} CettaSpaceAgendaSelectionScope;
bool cetta_space_agenda_selection_enter(CettaSpaceAgendaSelectionScope *,
    CettaSpaceAgendaSelection *);
void cetta_space_agenda_selection_leave(CettaSpaceAgendaSelectionScope *);
/* Borrowed while the selection owner remains held. Advancing through this
 * pointer requires its execution scope and the caller's current authority. */
CettaSpaceAgenda *cetta_space_agenda_selection_provider(CettaSpaceAgendaSelection *);

/* TransactionBoundaryRequestsV1 images use the ordinary agenda destructor
 * and census. A source target is either independently held and borrowed, or
 * is the agenda's embedded owned world. An image always owns that embedded
 * world, initialized at its final address before observer registration.
 * Moving the agenda/Space by value after initialization is not supported.
 *
 * This boundary excludes active transactions, selection/match cursors,
 * callbacks, descendants and opaque world indices. It retains historical
 * tokens, waiting rows, array capacities and every initialized scalar.
 * Inactive transaction Space payloads are released storage, not live roots.
 * Atom/Bindings roots use the enclosing held arena map and shared session;
 * their service limits, grounded authorities and final owner census remain
 * the caller's responsibility. This API grants no producer/commit authority.
 *
 * On refusal, *image may be a partial ordinary owner. Dispose it and discard
 * the enclosing copy session before reusing a source/image relation. Only a
 * successful completion lookup permits publication or execution. */
bool cetta_space_agenda_checkpoint_admitted(const CettaSpaceAgenda *source,
    SpaceCheckpointBudget *budget);
bool cetta_space_agenda_checkpoint_copy(const CettaSpaceAgenda *source,
    Arena *destination, AtomDeepCopySession *atoms,
    CettaSpaceAgenda **image, SpaceCheckpointBudget *budget);
const CettaSpaceAgenda *cetta_space_agenda_checkpoint_lookup(
    const AtomDeepCopySession *atoms, const CettaSpaceAgenda *source);
Space *cetta_space_agenda_checkpoint_target(const CettaSpaceAgenda *source);
bool cetta_space_agenda_checkpoint_owns_target(const CettaSpaceAgenda *source);
/* Addresses of the held provider's target/snapshot/current/replacement
 * roles. Inactive embedded headers are addresses only, never live payloads. */
enum { CETTA_SPACE_AGENDA_REGION_COUNT=4 };
bool cetta_space_agenda_checkpoint_regions(const CettaSpaceAgenda *,
    const Space *regions[CETTA_SPACE_AGENDA_REGION_COUNT]);
/* A non-NULL borrowed_origin is the actual completed copy session for this
 * exact source/destination pair and its freshly initialized target world.
 * Later comparisons between completed independently owned images require
 * NULL here and matching lifetime modes. The construction session, source
 * and both worlds remain held/current for this synchronous witness only.
 * No live semantic field is omitted in either mode. */
bool cetta_space_agenda_checkpoint_read_token(const char *,SpaceReadToken,SpaceReadToken,
    const CettaCheckpointCompare *);
bool cetta_space_agenda_checkpoint_compare(const CettaSpaceAgenda *left,
    const CettaSpaceAgenda *right, const AtomDeepCopySession *borrowed_origin,
    const CettaCheckpointCompare *read, SpaceCheckpointBudget *budget);

/* A synchronous observation of one actual provider boundary. The scope is
 * borrowed for a normally returning serial request; no callback or context
 * is retained in an agenda, checkpoint, or asynchronous matcher job. */
typedef enum {
    CETTA_AGENDA_EVENT_STEP_BEGIN, CETTA_AGENDA_EVENT_STEP_RESULT,
    CETTA_AGENDA_EVENT_SELECTION, CETTA_AGENDA_EVENT_SELECTED,
    CETTA_AGENDA_EVENT_CAPTURE_RESET, CETTA_AGENDA_EVENT_CAPTURE_BEGIN, CETTA_AGENDA_EVENT_CAPTURE_ROW,
    CETTA_AGENDA_EVENT_CAPTURE_END, CETTA_AGENDA_EVENT_QUERY_BEGIN,
    CETTA_AGENDA_EVENT_QUERY_ROW, CETTA_AGENDA_EVENT_QUERY_END,
    CETTA_AGENDA_EVENT_MATCH_BEGIN, CETTA_AGENDA_EVENT_MATCH_GRANT,
    CETTA_AGENDA_EVENT_MATCH_RESULT, CETTA_AGENDA_EVENT_AGGREGATE,
    CETTA_AGENDA_EVENT_ROW, CETTA_AGENDA_EVENT_JOIN_END,
    CETTA_AGENDA_EVENT_REVALIDATE, CETTA_AGENDA_EVENT_PRECLEAR,
    CETTA_AGENDA_EVENT_PRECOMMIT, CETTA_AGENDA_EVENT_COMMITTED,
    CETTA_AGENDA_EVENT_COUNT
} CettaSpaceAgendaEventKind;

typedef struct {
    const CettaSpaceAgenda *agenda;
    CettaSpaceAgendaEventKind kind;
    uint64_t index, detail, allowance;
    SpaceReadToken read;
    Atom *pattern, *value;
    const Bindings *environment;
} CettaSpaceAgendaEvent;

typedef enum {
    CETTA_AGENDA_CHARGE_SELECTION, CETTA_AGENDA_CHARGE_CAPTURE,
    CETTA_AGENDA_CHARGE_QUERY,
    CETTA_AGENDA_CHARGE_MATCH_CAPTURE, CETTA_AGENDA_CHARGE_ROW_CHECK,
    CETTA_AGENDA_CHARGE_COMMIT
} CettaSpaceAgendaChargeSite;
typedef bool (*CettaSpaceAgendaObserve)(void *, const CettaSpaceAgendaEvent *);
typedef bool (*CettaSpaceAgendaPermit)(void *, const CettaSpaceAgendaEvent *);
typedef bool (*CettaSpaceAgendaChargeWitness)(void *, const CettaSpaceAgenda *,
    CettaSpaceAgendaChargeSite, unsigned kind, uint64_t units, bool after);
typedef struct CettaSpaceAgendaObservationScope {
    struct CettaSpaceAgendaObservationScope *previous;
    CettaSpaceAgenda *agenda;
    CettaSpaceAgendaObserve observe;
    CettaSpaceAgendaPermit permit;
    CettaSpaceAgendaChargeWitness charge;
    void *context;
    uint64_t events, commits;
    const char *refusal;
    bool entered, notifying;
} CettaSpaceAgendaObservationScope;

bool cetta_space_agenda_observation_enter(CettaSpaceAgendaObservationScope *,
    CettaSpaceAgenda *, CettaSpaceAgendaObserve, CettaSpaceAgendaPermit,
    CettaSpaceAgendaChargeWitness, void *context);
void cetta_space_agenda_observation_leave(CettaSpaceAgendaObservationScope *);
bool cetta_space_agenda_observation_complete(const CettaSpaceAgendaObservationScope *);

/* The transcript owns its cells, byte copies and retained Bindings. Its
 * Atom roots live in the caller's held destination arena. One admitted copy
 * session spans the whole request, preserving aliases across events. The
 * caller configures that session's arena/global domains and graph limits.
 * Cell/byte allocations use the supplied direct budget. Atom/Bindings
 * services retain the enclosing session limits and their ordinary charges;
 * the caller must complete the full owner census before accepting a request
 * or permitting a commit. A complete word alone is not storage admission.
 * Finish destroys the owned session before releasing any source backing
 * pins; the destination arena remains owned by the enclosing request. */
typedef struct CettaSpaceAgendaTranscript CettaSpaceAgendaTranscript;
/* Static array reservation within bytes, leaving half of the remaining
 * budget for per-event payload owners. Zero means no admitted array. */
size_t cetta_space_agenda_transcript_cell_capacity(size_t bytes,size_t events);
CettaSpaceAgendaTranscript *cetta_space_agenda_transcript_new(Arena *destination,
    AtomDeepCopySession *owned_session, size_t cell_limit, size_t event_limit,
    SpaceCheckpointBudget *budget);
bool cetta_space_agenda_transcript_append(CettaSpaceAgendaTranscript *,
    const CettaSpaceAgendaEvent *, SpaceCheckpointBudget *);
/* Synchronously validate the last complete event against the actual held
 * provider. The caller supplies its bounded source-to-word Atom/Bindings
 * reader and completes that graph comparison before permitting publication.
 * This does not call the encoder or build a second expected word. On success,
 * replacement receives the actual live replacement token (or all zeroes).
 * Invoke only within an enabled recording scope; failures remain sticky. */
bool cetta_space_agenda_transcript_validate_live(CettaSpaceAgendaTranscript *,
    const CettaSpaceAgendaEvent *, const CettaCheckpointCompare *,
    SpaceCheckpointBudget *, SpaceReadToken *replacement);
void cetta_space_agenda_transcript_finish(CettaSpaceAgendaTranscript *);
void cetta_space_agenda_transcript_free(CettaSpaceAgendaTranscript *);
void cetta_space_agenda_transcript_storage(const CettaSpaceAgendaTranscript *,
    CettaStorageCensus *);
/* Borrow the still-live copy relation before finish. The caller supplies
 * source keys only under the original admitted/pinned lifetime contract. */
const Atom *cetta_space_agenda_transcript_forwarded(const CettaSpaceAgendaTranscript *,const Atom *);
/* False after finish, including an empty word. It is not a membership test. */
/* Synchronous domain growth uses the same live transcript copy session.
 * No growth or copy is admitted after finish or an incomplete event. */
bool cetta_space_agenda_transcript_global_extension_budget(const CettaSpaceAgendaTranscript *,
    const HashConsTable *,size_t nodes,size_t *bytes,size_t *work);
bool cetta_space_agenda_transcript_extend_globals(CettaSpaceAgendaTranscript *,
    const HashConsTable *,size_t nodes,size_t work,size_t bytes,AtomDeepCopyGlobalInspection *);
bool cetta_space_agenda_transcript_correspondence_live(const CettaSpaceAgendaTranscript *);
/* The caller must already hold every captured arena header and the global
 * services through this synchronous query. Checking the map is not a lifetime
 * acquisition. A partial word can still have a current borrowed map. */
bool cetta_space_agenda_transcript_maps_arena(const CettaSpaceAgendaTranscript *,
    const Arena *source,const Arena *destination);
bool cetta_space_agenda_transcript_counts(const CettaSpaceAgendaTranscript *,size_t *cells,size_t *capacity);
bool cetta_space_agenda_transcript_complete(const CettaSpaceAgendaTranscript *);
size_t cetta_space_agenda_transcript_events(const CettaSpaceAgendaTranscript *);
uint64_t cetta_space_agenda_transcript_commits(const CettaSpaceAgendaTranscript *);
const char *cetta_space_agenda_transcript_refusal(const CettaSpaceAgendaTranscript *);
bool cetta_space_agenda_transcript_compare(const CettaSpaceAgendaTranscript *,
    const CettaSpaceAgendaTranscript *, const CettaCheckpointCompare *);

/* Compare the complete attempted word through its final PRECOMMIT event
 * against exactly that prefix of the still-owned complete recorded word.
 * The caller separately authenticates the source and compares the required
 * command prefix at the same callback boundary. Later events remain held;
 * request acceptance requires the strict full-word comparison above. */
bool cetta_space_agenda_transcript_compare_prefix(const CettaSpaceAgendaTranscript *recorded,
    const CettaSpaceAgendaTranscript *attempt, const CettaCheckpointCompare *);

#endif
