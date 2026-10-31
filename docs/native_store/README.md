# Native byte stores

For native ACT snapshot readers backing ordinary MeTTa spaces, see the
[ACT/Space integration requirements](ACT_SPACE_REQUIREMENTS.md). That document
separates byte-format compatibility from occurrence, query and durable-owner
acceptance; it is a design contract, not an implementation-completion claim.

The [DAS interface intake](DAS_INTEGRATION.txt) records a pinned upstream API
review and external-Space qualification gates. It covers reusable streaming,
cancellation and authorization mechanisms, while keeping occurrence identity,
captured revisions and publication guarantees explicit. No DAS adapter is
implemented by that design checkpoint.

`store` is a SQLite-free, persistent byte-key interface. Applications define
records, key layouts, indexes, queries, constraints and receipts in MeTTa.
The native owner validates snapshots and publishes one immutable root for a
whole batch, using the same CSL1 journal framing and checkpoint publication
as the space service. This is not an SQL interface.

The named-space and older durable-runtime interfaces have not yet migrated to
this owner. The byte-store library is available for qualification; adding it
does not migrate an application's existing database.

## First SQLite-replacement milestone

First qualify persistent ordinary Spaces across fresh processes: commit data,
end or kill the writer process, reopen from disk in a new process, and query
the same committed values, multiplicities and occurrence identities. Test
interrupted publication and lost replies as well as clean shutdown. The
owning durable-Space adapter now passes the scoped gates described below. The
MeTTa-configured local service also passes the scoped connection/cursor and
restart gates below, alongside captured backup/restore. Full workload and
release qualification remain.

The first bounded service milestone targets the actual local ledger/queue workloads.
Its durability and isolation requirements remain strict while its API stays
small. The broader graph/query curriculum remains a subsequent milestone.

- Reuse the existing durable owner and shared Space matcher/source/cursors.
  The public owning captured-Space adapter keeps persisted programs inert.
- Start with coordinated single-writer publication and captured readers. Whole-
  revision validation is an acceptable conservative first policy: concurrent
  changes conflict, including changes affecting absent/aggregate/path reads.
  Only qualify narrower dependencies when needed; preserve one commit domain.
- Atomically publish records, indexes, invariant decisions and durable receipts.
  A repeated request ID with the same operation retrieves its original outcome;
  reusing the ID for a different operation refuses. This does not guarantee
  exactly-once external actions.
- Retain one query's view/cursor owner while streaming bounded results. Explicit
  close, cancellation, disconnect and deadlines release local resources. Any
  cursor surviving a protocol message remains server-owned and client-scoped;
  a serialized native handle is not that ownership mechanism.
- Reuse local transport/framing and existing caller authorization. Bound message
  sizes, queued work and result buffers; preserve conflict, busy, corruption,
  unsupported-version, incomplete and uncertain-publication outcomes.
- Provide the access paths required by the chosen workload and correct scan
  fallbacks. First-page work must be incremental; report full scans and resource
  exhaustion rather than calling them complete empty results.
- Supply recover, checkpoint/compact, health and a tested backup/restore path.
  A quiesced backup is an acceptable initial operation; copying a changing
  collection of files without a captured/quiesced publication boundary is not.

Qualification requires crash/fault injection around publication, concurrent
claim/uniqueness and stale-read tests, lost-reply retries, corruption/truncation
refusal, old readers surviving compaction, cancellation/disconnect lifetime
tests, backup/restore and transition parity against the chosen SQLite domain.
Verify acknowledged commits survive the modeled failures and uncertain outcomes
remain recoverable. Process-kill tests alone do not establish real power-loss
durability: retain the documented filesystem assumptions and I/O fault tests.

Remaining release requirements are not covered merely by existing store unit
tests or the older named-space service. The latter materializes query snapshots;
the MeTTa-configured path below instead owns shared incremental cursors.
Declared symbol/string candidate indexes now use the shared Space cursor, with
small chain-join measurements described below. Scalar range indexes, broad
workload qualification and large-KB scaling remain open. Live migration remains
a separately authorized step.

ACT may become a native immutable base/segment under the same durability owner.
Its reader and format qualification need not block the first workload release
over the existing index. Federation, multi-writer distributed commits, online
resharding and a full cost optimizer are later capabilities. Queries and
updates are MeTTa programs over Spaces. SQL/Cypher parsing is outside this
project's scope, including its later roadmap.

The disk ABI is version 2 (`cetta_disk_abi_version`), with a generation in every
`CettaDiskRoot`. A checksummed `CSD2` segment header carries the same generation
as the `CDS2` manifest. Reopening through the wrong segment is refused even
when its offsets and frame checksums happen to be valid. Earlier prototype
`CDS1` manifests are reported as a different version, never reset or silently
read as version 2. Preserve and re-export those offline prototype stores with
their matching implementation. Portable bindings must check the ABI before
passing a root structure.

## Captured persistent atom Spaces

The PeTTa library `(library durable_space)` maintains collections of ordinary
atoms over the native owner. It supplies writes and metadata; it does not
implement another query language or matcher.

- `(durable-space:create owner view collection request-id)` creates an empty
  collection if its metadata and occurrence range are absent.
- `(durable-space:create-indexed owner view collection request-id indexes)`
  additionally declares candidate indexes, each `(SpaceIndexV1 arity position)`.
  Arity includes the head; position zero indexes the head. Declarations are
  immutable for this collection. Empty declarations use the ordinary descriptor.
- `(durable-space:commit owner view collection request-id changes)` accepts a
  list of `(SpaceInsertV1 local-id atom)`, `(SpaceUpdateV1 reference atom)` and
  `(SpaceDeleteV1 reference)` edits. At most one edit may name an occurrence
  in a batch. Update/delete require an existing occurrence. Counts, atom bytes
  and an immutable application receipt publish atomically.
- Creation returns `(KnowledgeCommittedV1 (SpaceCreatedV1 collection) epoch
  revision)`. Edits return `(KnowledgeCommittedV1 (SpaceAppliedV1 references)
  epoch revision)`, listing inserted references in input order. A retry of the
  identical command returns `(KnowledgeReplayedV1 original-answer)`; different
  commands under the same request id refuse. Recovery/receipt inspection uses
  `knowledge:resolve` from the same library foundation.
- `(durable-space:capture view collection)` returns `(StoreSpace space epoch
  revision)`. Ordinary `match`, `get-atoms`, and imported `pat` queries read
  the captured occurrences through the shared incremental Space cursor.
  Eligible declared indexes select candidates; other patterns use a complete
  prefix scan. Both paths use the same matcher and occurrence identities.
- `(durable-space:read view collection reference maximum-bytes)` fetches one
  original occurrence from that captured root. Success is
  `(SpaceOccurrenceReadV1 atom encoded-bytes)`; a missing occurrence is
  `SpaceOccurrenceAbsentV1`. Wrong epoch/collection, invalid bytes, I/O and
  size failures remain explicit. This returns original data, rather than a
  match template in which stored variables may have been instantiated.

For example, with a captured `view` and an initialized `claims` collection:

```metta
(let (StoreSpace $claims $epoch $revision)
     (durable-space:capture $view claims)
     (match $claims (, (supports $e $c) (source $e $origin)) ($c $origin)))
```

Collection/request/local identifiers are ground portable values. An inserted
occurrence reference is `(StoreOccurrenceV1 epoch key)`. Keys include the
collection, insertion request and local id; equal payloads remain distinct.
The receipt prevents a deleted occurrence from being recreated by replaying
its original insertion. An update keeps its reference. A new insertion under
a new request has a new reference. References are identifiers, not authority.
The owner handle remains the write capability.

All writes conservatively validate the supplied view's complete logical
revision. Keep that same view for queries informing a write; capturing a newer
view after an earlier read does not validate that earlier read. Automatic
observation enrollment across arbitrary computations is not established by
this helper. Raw `store:` edits can bypass its collection/receipt invariants;
the guarantees here cover writes made through this collection interface.

Captured atom Spaces are read-only and own their snapshot independently of
the original owner/view handles. Their resources follow ordinary evaluator
Space lifetime and are released at session teardown. An existing captured
Space survives later commits and physical compaction. Raw Space values retain
their ordinary session lifetime; the request-owned cursor below supplies the
separate service resource lifetime. Transactional payload
contexts without the ordinary Space owner are refused. Cross-dialect service
admission is not implied by the PeTTa tests.

Stored rules and callable expressions remain data. Merely reading them does
not install an equation catalog. CAV1 preserves sharing within each stored
atom, with fresh runtime variables on decoding; runtime variable ids are not
durable identities. The current portable codec bounds each encoded packet to
one MiB, including the complete receipt/command envelope. Oversize or
unsupported values refuse before publication. Optional SQL-style nulls and
coercions are not introduced.

The lower-level `(store:space-view view metadata-key)` reads a CAV1
`(StoreSpaceV1 prefix occurrence-count)` descriptor in that same captured root.
Every value under the nonempty prefix must be one CAV1 atom; its full key
identifies an occurrence. The descriptor must lie outside that prefix. A full
scan checks the declared count. Corrupt atom bytes, count mismatches and I/O
failures are incomplete execution, never successful empty or partial bags.
Use `durable_space` to maintain this descriptor rather than editing it by hand.

### Declared candidate indexes

For example, an edge `(knows alice bob)` has arity three. Declarations
`((SpaceIndexV1 3 0) (SpaceIndexV1 3 1) (SpaceIndexV1 3 2))` supply relation,
outgoing and incoming candidate access. A probe uses the last declared eligible
projection whose pattern field is a symbol or string. This is a deterministic
selection rule, not a cardinality optimizer or an intersection of indexes.

An indexed collection stores `(StoreSpaceV3 prefix occurrence-count indexes)`;
each index descriptor is `(StoreSpaceIndexV1 arity position index-prefix)`.
Symbol/string fields have exact postings. Other fields and whole-variable
occurrences have wildcard postings. A selective read merges those two disjoint
candidate ranges, then runs ordinary unification. Consequently a stored open
term can still answer a bound query, and duplicate occurrences remain separate.
Numeric and compound field equality has no new byte-normalization contract;
those fields cannot select an exact index and retain a complete scan unless
another declared projection is eligible. Results remain unordered answer bags.

Posting keys contain the projection and full occurrence key; posting values are
empty. A candidate fetch reads the fact from the same captured root, without
copying every payload into each index. Inserts, updates, deletes, counts,
postings and receipts share one atomic publication. Updates retire old postings.
The library refuses orphaned index prefixes during creation. Dangling or malformed
postings fail execution explicitly. Raw byte-store writes can bypass these
invariants, including deleting a required posting; selective reads do not audit
the entire index for missing postings. Use the collection writer consistently.

Historical `StoreSpaceV2` collections used a projection that could interpret
Boolean-like fields while extracting them, leaving logically incomplete
postings despite valid storage checksums. The current reader ignores their
postings and scans the fact range. Their edits refuse with
`SpaceRefusedV1 index-upgrade-required`; replaying an existing receipt remains
available. A separate explicit rebuild/export upgrade is required before such
a collection resumes writes. It is not silently relabelled as V3. New V3
collections use data-preserving field selection. This is a collection metadata
version, independent of the native CSD2 storage ABI and language selectors.

Complete posting keys must fit the native key limit (currently 65,536 bytes).
An otherwise portable wide string may therefore fit an unindexed collection
but refuse in an indexed projection. Such a write returns `StoreRefused invalid`
atomically, without a fact or success receipt. No posting is silently omitted.
Large declarations and batches remain subject to the existing codec/commit
limits. Rebuilding indexes on existing collections is not implemented.

`(store:space-query-info query)` returns `(StoreQueryInfo scans indexed-traversals
candidates decoded-bytes overflow)`, including after end or cancellation while
the handle remains open. Traversals count actual first advances, not unopened
cursor constructors. Candidates count physical fact/posting rows visited;
decoded bytes count successfully decoded CAV1 fact payloads. These counters do
not measure internal tree-node visits, all allocations or total I/O. Overflow
saturates the counters and sets the flag. Query explanation is limited to these
observed access-path totals; no cost-plan display is claimed.

`test-native-disk-space-index` and `test-indexed-space-api` compare candidate
reads with ordinary in-memory matching, including repeated/open variables,
duplicate edges, joins, inert rules, NUL strings, numeric scan fallback, updates,
replay, compaction, crash/restart and indexed backup/restore. The latter also
checks repeated service queries, cancellation and disconnected cursor owners.
`tests/support/measure_space_index_scaling.py VALUE_WORKER` reports a small
dependency-chain size sweep. Its storage totals include receipts, key/value
encoding and all live native index files after compaction; they are not a
compressed-ACT benchmark. Timings describe that run and do not gate correctness.

Qualification targets: `test-native-disk-space`, `test-persistent-space-api`,
`test-durable-space-transactions`, existing `test-space-source-cursor` and
`test-native-disk-store`. They cover fresh-process kill/reopen, changed symbol
allocation, duplicate/open atoms, inert programs, stable references, lost
replies, atomic edits, stale/absent-read conflicts and old readers across
publication/compaction. These scoped tests do not establish power-loss
durability beyond the native owner's documented filesystem/fault model, nor
complete the broader service, constraint, index or workload acceptance.

### Request-owned pattern cursors

`(durable-space:cursor view collection pattern template)` returns
`(StoreQueryOpened query epoch revision)`. It owns a private captured Space,
universe and the existing `SpaceQueryCursor`. It exposes no raw Space pointer.
Patterns and templates are portable data; shared variables are freshened
together on opening. Templates are instantiated as data, without evaluation.
Use ordinary MeTTa code on the returned values for further computation.

- `(store:space-query-next query sequence transitions maximum-bytes)` returns
  `(StoreQueryRow sequence value occurrence-identities)`, `StoreQueryPaused`,
  `StoreQueryEnd`, `(StoreQueryTooLarge required-bytes)`, `StoreQueryStale`,
  `StoreQueryCancelled` or `(StoreQueryFailed reason)`.
- Sequence starts at zero. Repeating the last offered sequence returns the
  same row without advancing, until the next sequence is requested. Equal
  values from distinct occurrences remain distinct rows. An undersized byte
  budget retains its pending row; it does not skip it.
- `(store:space-query-checkpoint query)` relocates live continuation/template
  state into the next arena and retires the previous arena. Call it between
  pages or work quanta to avoid retaining all prior answer allocations.
  A refused checkpoint leaves the original cursor and replay row unchanged.
- `(store:space-query-cancel query)` releases its captured root, cursor and
  arenas immediately. An already completed cursor remains `StoreQueryEnd`;
  otherwise cancellation returns `StoreQueryCancelled`. `store:close` releases
  the remaining owning handle. Unreturned handles also retire with the request
  arena, using the existing native-handle lifetime mechanism.

`StoreQueryFailed` is terminal for that cursor and releases its captured root.
It preserves failure as a distinct outcome; it does not turn prior rows into a
complete result or silently reopen a newer revision.

These are iteration/lifetime operations over the shared Space matcher, not a
second query evaluator. The lower-level constructor is
`(store:space-query-open view metadata-key pattern template)`. Syntax and each
row must fit the existing CAV1 limits. Work limits count matcher transitions,
not wall-clock time or all primitive byte costs. Network framing needs its own
reply-envelope allowance, deadlines and resource quotas.

`(durable-space:probe view collection pattern template transitions)` makes one
bounded first-answer attempt, then closes its temporary query on every outcome.
It returns `(SpaceProbeFoundV1 value occurrence-identities)` or
`SpaceProbeAbsentV1` only after a completed negative search. A paused, failed,
oversized or refused search remains that explicit outcome. `StoreQueryPaused`
from this helper has no retained continuation; retry with a larger budget on
the same view if appropriate. Do not treat it as absence. This is a convenience
over the shared cursor, not a separate predicate evaluator.

`test-native-disk-query` checks data/identity replay, pending row byte limits,
checkpoint refusal, variable sharing, sixteen interleaved captured readers,
old roots across compaction and immediate cleanup on errors/end/cancel. Its
4,096-row flat scan checks checkpointed retention independently of prior rows.
`test-persistent-query-worker` compares an executable MeTTa quoted-template
reference, then measures repeated completed, closed, cancelled and abandoned
cursors in a fresh long-lived worker. Tests explicitly import their assertion
library and include a deliberately false assertion as a negative control.
These cursor gates do not yet establish socket disconnect handling or the
complete service acceptance. Session-lived `store:space-view` keeps its
ordinary Space lifetime; this cursor provides the bounded request owner.

### Constraints as MeTTa programs

`examples/native_space_invariants.metta` demonstrates a trusted application
policy over the same captured view and collection writer:

```text
 (ProgramV1 a) ── DependsV1 ──> (ProgramV1 b)
       unique identity              unique identity
       referenced programs cannot be removed

 (JobV1 id QueuedV1 payload) ── claim ──> (JobV1 id (ClaimedV1 worker) payload)
             same durable occurrence; one successful claimant
```

Ordinary pattern probes check program/job uniqueness, both dependency endpoints
and incoming/outgoing references before deletion. The writer validates that
same whole revision when publishing the edits and receipt. An insertion after
an earlier negative check therefore conflicts; the caller must capture a new
view and decide again. This conservative policy also conflicts with unrelated
concurrent changes. No narrower dependency validator is claimed here.

The example checks the original operation's receipt before rechecking current
state. Otherwise a completed claim whose reply was lost could be misreported
as a new `not-queued` refusal. `(durable-space:commit-command owner view
collection request-id command changes)` lets trusted policy code identify a
versioned original command separately from query-derived edits. It uses the
same staging/index/publication machinery as `durable-space:commit`. The command
must describe the complete policy intent, including its collection and semantic
version; the helper does not verify arbitrary policy code. An existing receipt
returns its original result without applying a newly computed edit list.
The ordinary edit API retains its `SpaceCommitV1` fingerprint unchanged.

These are application invariants for a collection created empty and maintained
through that policy. They do not impose a schema on all Spaces or make raw
owner writes constrained. The test service reuses the existing server and read
handlers, replacing its unrestricted Commit route with the trusted Apply route.
Client-supplied terms are data, not selectable executable policy functions.

`test-space-invariants` compares scripted/generated transitions with a SQLite
constraint/claim oracle over overlapping identity semantics. It checks duplicate
edges, reference restrictions, competing uniqueness/claim writers, stale negative
reads, stable occurrences, shared/inert/error-valued payloads, bounded incomplete
probes, receipt collisions/replay, and a lost claim reply across process restart.
The identical policy runs embedded and through the service. A fixed-KB probe
lifetime sweep checks descriptor and retained-arena stability. This small policy
example is not a replacement for a production gateway's deadlines, accounting,
dispatch or external-effect recovery rules, and does not authorize live migration.

## Owners and snapshots

Import `(library store)` in PeTTa, or `store` in HE and Prime.

- `(store:open path "create")` initializes a missing store, or opens an existing
  one. `"existing"` never initializes a store. Both return `(StoreOpened owner)`.
- The directory must already exist durably, including its parent entry. It must
  belong to the process user and have no group or other permission bits. The
  owner does not create parent directories. It refuses an empty or damaged
  published manifest instead of silently creating new state.
- `(store:snapshot owner)` returns `(StoreSnapshot view epoch revision)`.
  It confirms the durability of a first-seen root before returning it.
- `(store:view-info view)` returns `(StoreViewInfo epoch revision)` for a live
  snapshot handle. These are its captured coordinates, even after later
  publications. The call creates no resource and does not validate a publisher
  or a transaction's read dependencies.
- `(store:view-clone view)` returns `(StoreSnapshot independent-view epoch
  revision)` for that same captured root. Closing the original handle does not
  close the clone. This is an independent read lifetime, not a newer snapshot
  or a publisher capability. It uses the existing native view clone mechanism.
- `(store:check-view owner view)` returns `StoreViewValid` when a live publisher
  and captured view belong to the same physical store domain and epoch. A
  publisher awaiting recovery is refused. An older revision remains valid;
  this check neither publishes nor validates the read/write set of a later
  commit. That later commit still performs its own atomic validation.
- `(store:close resource)` returns `StoreClosed`. Owners, snapshots, body readers
  and scans are independent resources. A reader can outlive its snapshot, and a
  snapshot can outlive its owner. Explicit close and request-arena retirement
  both release resources.

Handles carry native ownership. Their printed identifiers cannot be replayed
as capabilities, sent through the portable value protocol, or saved as durable
application identities. Keep handles within a call; reopen by store path for a
later call. Use a separate owner handle for each concurrent caller.

## Atomic writes and observations

`(store:commit owner view (ops ...))` validates the captured epoch, store domain
and logical revision. Operations are `(put key bytes)` and `(remove key)`.
Accepted batches return a new `StoreSnapshot`; accepted no-ops also advance
the logical revision. Compaction changes the physical generation without
changing that revision.

`(store:commit-scoped owner view (reads ...) (writes ...) (ops ...))` validates
complete declared views instead of the whole-store revision. Scopes are:

- `(point key)` for one exact key, including its absence;
- `(range lower upper)` for a lower-inclusive, upper-exclusive byte range;
- `(prefix bytes)` for all keys beginning with those bytes.

The symbol `NoBound` removes a range bound; the string `"NoBound"` is a real
byte key. Every written key must belong to a declared write scope. Read and
write scopes both participate in validation. Keys and body bytes are compared
exactly, including missing keys; an index projection or a body hash alone is
not a validation certificate. Callers must declare every influencing read,
including negative and capacity dependencies. Unrelated scopes do not conflict.

One root publishes all operations. A snapshot is a repeatable read view, not
a one-use protocol authority. Applications must commit durable idempotency
receipts together with their changes; a bounded reply cache is insufficient.

## Reads without materializing the archive

- `(store:get view key maximum-bytes)` returns `(StoreValue bytes)`, `StoreAbsent`
  or `(StoreTooLarge length)`. The maximum is a caller-selected materialization
  allowance, not a limit on which stored values remain retrievable.
- `(store:blob-open view key)` returns `(StoreBlob reader length)`. The reader
  verifies its immutable body once. `(store:blob-read reader offset count)`
  returns `(StoreBytes bytes next-offset)` and supports repeatable offsets.
- `(store:scan-open view lower upper)` returns `(StoreScan scan)`.
  `(store:scan-next scan position)` starts at position zero and returns
  `(StoreKey key body-length next-position)` or `StoreEnd`. Repeating the last
  position repeats its answer; skipping positions is refused. Keys arrive in
  ascending byte order from the captured revision. Fetch bodies separately.
- `(store:scan-reverse-open view lower upper)` uses the same inclusive lower
  and exclusive upper bounds and the same `scan-next` protocol, returning keys
  in descending byte order. It seeks from the upper edge; finding a maximum
  does not materialize or walk all preceding records. Snapshots, repeatable
  positions and handle cleanup have the same contract in both directions.
- `(store:key-i64 integer)` returns `(StoreKeyBytes bytes)`, a fixed-width signed
  integer order embedding. `(store:key->i64 bytes)` is its inverse and returns
  `(StoreKeyInteger integer)`; a key of any length other than eight is refused.
  Applications decide where to use this encoding in their keys.

The immutable disk index keeps payloads separate from searchable nodes. Reads,
scope comparisons and compaction use bounded scratch space rather than loading
the corpus. Per-item format bounds remain explicit in the C API.

`store:encode` and `store:decode` are convenience adapters for the existing CAV1
portable value format. They preserve value kinds and return inert data. Its
portable-wire bounds still apply; a refused record is never truncated. Raw
byte bodies and chunk readers support larger payloads. A chunked record codec
is still needed before migrating a domain with individual records exceeding
the convenience codec's bounds.

## Failure and maintenance

`(StoreRefused reason)` distinguishes conflict, busy, invalid input, corruption,
version, resource capacity and I/O failures. `(StoreUncertain)` means publication
may have happened. It must not be interpreted as rollback or permission to
repeat an external effect.

An ambiguous native publication blocks further writes on that owner until
`(store:recover owner)` verifies and synchronizes the visible root. Recovery
returns a snapshot. The application then checks its own receipt and identity
to decide whether another local write is needed. Recovery never sends HTTP or
replays another external operation. A failure to allocate a returned view after
successful publication is also reported as uncertain to the caller.

Low-level index callers must discard a candidate after any segment-sync error.
The failed candidate is invalidated without closing its borrowed descriptor.
A later successful fsync can still leave failed writeback missing: rebuild from
the last acknowledged root, rewriting discarded bytes, or use a fresh segment.
The store owner follows this rebuild rule and never publishes a failed candidate.

Tree reads check the path they traverse. Streaming scans check strict key order
in the selected direction; compaction checks increasing order. Both bound node
visits by the captured segment's frame capacity, so a small shared-subtree graph cannot expand exponentially.
These checks do not turn a point lookup into a whole-file validation proof.

`(store:compact owner)` builds a new segment outside the writer lock and
publishes it only if the source root is still current. A changed root refuses
that maintenance attempt. `(store:collect owner)` first confirms the visible
root durably, then removes obsolete segment names. Live builders hold kernel
locks and old readers retain open descriptors, so collection cannot revoke
either. The owner lock is nonblocking and covers validation/publication, not
model calls or application planning.

Opening an existing store uses a shared lock even with `"create"`. Only a
missing manifest escalates to an exclusive initialization lock. Callers own
retry/backoff policy for `busy`; the lock provides no fairness guarantee.
Ownership refusals (unsafe links, permissions or owner) are reported separately
as `(StoreRefused ownership)`, rather than as an I/O failure.

## Qualification

`make test-store-library` checks native publication faults, concurrent writers,
scope validation, disk-index behavior, import-bit separation, and the actual
MeTTa interface against an offline SQLite byte-map oracle.
`make test-store-lifetime` measures repeated explicit and automatic resource
release through the value worker.

`tests/check_store_publication.py` compares native fault traces with
MeTTapedia's checked `Mettapedia.Machines.NativeStorePublication` model. This
is finite conformance evidence. The Lean model states its filesystem
assumptions; neither it nor the tests prove the C implementation or fsync.

## MeTTa handlers with connection-owned resources

`examples/native_kb_server.metta` is a small MeTTa program using the same
`durable_space` library as embedded applications. The existing value worker's
`--serve` mode supplies connections and resource lifetimes. It adds no query
evaluator or independent storage owner. The older named-space service remains
a separate compatibility path; its materialized snapshots are not used here.

After building `value-worker`, set `CETTA_KB_DIRECTORY` to an existing private
store directory and `CETTA_KB_SOCKET` to an absolute Unix socket path. Its
immediate parent must be an owned private directory. Run:

```sh
VALUE_WORKER --serve --lang petta /absolute/path/to/examples/native_kb_server.metta kb-server:configuration
```

`VALUE_WORKER` denotes the configured worker executable printed by the build.
An adjacent startup lock serializes socket ownership. A clean stop removes its
socket; restart can remove the stale owned socket left by a killed process.
Store recovery and durable acknowledgement remain the native store owner's
responsibility. Session tokens do not survive a server restart.

The trusted configuration returns `(ValueServiceV1 socket data routes limits)`.
Routes are `(ServiceRouteV1 operation Read|Write handler)`; limits are
`(ServiceLimitsV1 clients resources frame-bytes idle-ms owned-resource-bytes
fuel)`. Each handler receives ordinary data:

```metta
(ServiceCallV1 configured-data (ServicePeerV1 kernel-uid ReadOnly|ReadWrite)
  resource-or-ServiceNoResourceV1 argument)
```

Only the configured handler is invoked. Client arguments are quoted values;
they are not executable request source. The server accepts same-UID clients
on a mode-0600 socket. This initial account-level admission does not implement
cross-account grants, delegation or network authentication. Route labels are
trusted configuration: they must accurately describe their handler's effects.

CSP1 frames carry CAV1 `(request id body)` and `(reply id ok|error value)`.
The service accepts these envelopes:

- `(CallV1 operation argument)` invokes a configured operation.
- `(UseV1 token operation argument)` supplies that connection's retained
  native resource to its configured handler.
- `(DropV1 token)` closes a resource immediately. Disconnect and idle expiry
  close every resource owned by that connection.
- `(ReadOnlyV1)` irreversibly attenuates that connection; subsequent Write
  routes refuse before invoking a handler. It is not an account-wide policy.
- `(StatsV1)` reports connected clients, resource count and permanent arena
  bytes. It is a health measurement, not a full process memory census.

A handler returning `(ServiceKeepV1 owning-native-handle metadata)` exports
`(ServiceResourceV1 token metadata)`. The host retains the protected handle
in its own arena and checks its storage census. A token from another connection
or server incarnation refuses. Serialized native identifiers do not confer
ownership. Limits bound frame/cache buffers, connections and retained
resources. One outstanding frame per connection supplies backpressure.

### MeTTa continuation values

`(ServiceKeepValueV1 value metadata)` retains an ordinary MeTTa value as a
connection-owned resource and returns the same `ServiceResourceV1` envelope.
It can hold a traversal frontier, a visited set and owning native cursor/view
leaves together. The host supplies retention and token ownership; the MeTTa
program supplies the algorithm. This adds no graph evaluator or query language.

The value profile is bounded portable syntax plus actual owning native handles
from the current registry. It preserves variable sharing and inert terms.
Raw Spaces, evaluator closures and other unsupported heap values refuse rather
than acquiring a guessed lifetime. Printed `(NativeHandle kind integer)` syntax
without an owning identifier remains data; retaining it grants no authority.
A stale or foreign owning holder refuses. A live native leaf already exported
by another retained resource refuses, including on the same connection. Clone
a captured view explicitly when a continuation needs independent ownership.

Advance such a resource with `(UseValueV1 token operation argument)`. Preflight
refusals, such as wrong connection, mode, route or ReadOnly rights, preserve the
state. Once its handler is invoked, that token is consumed. A successful step
can return `ServiceKeepValueV1` with a successor state and page metadata, yielding
a fresh token. A completed/cancelled step returns its terminal result. An invoked
handler failure, bad successor or reply-limit failure retires the consumed state;
it never leaves an apparently resumable token after hidden cursor advancement.

The last exact wire request/reply cache is checked before token lookup. Repeating
that request id and identical body returns its original successor/page without
advancing again, despite consumption of the input token. A different body under
the cached id refuses. The cache retains one request, not an arbitrary history
or a durable continuation across restart. Disconnect and restart retire states.
An uncertain transport outcome does not authorize inventing a new step id.

State arenas and transitive native resources enter the existing ownership census
and byte budget. Format, ownership, size, capacity and resource-limit failures
remain explicit. Dropping or moving a value retires its arena-backed holders;
it does not explicitly revoke a native handle that a successor has retained.
Direct native resources retain their existing explicit-close semantics.

`test-native-service-state` checks variable sharing, inert/error-looking data,
independent views through compaction, exact replay, consumed/stale tokens,
cross-connection and ReadOnly refusals, malformed/closed/literal handles, unsupported
heap values, depth/byte/resource limits, invoked failures, oversized replies,
completion/cancellation, disconnect/restart and fixed-KB lifetime measurements.
This is the continuation boundary for paged MeTTa algorithms; it does not claim
the graph/relational curriculum itself is complete.

The example exposes Snapshot, Create, Query, Next, Cancel, QueryInfo, Commit,
Resolve, Backup and Compact. Create accepts either `CreateV1` or
`(CreateIndexedV1 collection request-id indexes)`. Query uses the supplied
captured view and its declared access paths. Next grants bounded matcher
work and checkpoints the live cursor; the template is projected as data.
Commits validate the same explicitly supplied informing view. This does not
automatically enroll arbitrary earlier application reads. Queries retain the
complete prefix-scan fallback and may return `StoreQueryTooLarge` if a row
exceeds the configured output allowance.

The last exact request/reply is cached per connection until another id arrives.
Reusing that id with different bytes refuses. This transport replay cache is
not durable exactly-once execution. Create and Commit use the library's durable
request ids and command receipts; after a lost reply, Resolve checks those
receipts, including after restart.

`ServiceRefusedV1` denotes rejection before a handler ran. `ServiceFailedV1`
may follow effects and never implies rollback. Fuel, cancellation, stack,
capacity, invalidation, host failure and paused evaluation retain distinct
failure reasons. An empty or nonsingle handler result is not a successful
empty query. Cursor cancellation is serviced between handler calls; it cannot
preempt a synchronous native primitive or an in-progress publication.

The `(library space_service)` client reuses its existing transport with
`space:connect`, `space:exchange` and `space:disconnect`. A successful connect
returns `(SpaceConnected connection)`. Exchange takes a complete request and
timeout in milliseconds. The protected connection survives copied owners and
closes when its last receiving arena retires. A failed exchange closes the
stream and preserves unavailable versus uncertain delivery. The original
one-shot `space:call` remains available; each call disconnects, so it cannot
retain this service's resources across calls.

`test-native-kb-service` checks both Python and native MeTTa clients, captured
readers across compaction, token/handle forgery refusals, stale commits,
lost-reply recovery, crash/restart, transport replay, bounded clients/resources,
idle/partial-frame cleanup, explicit and automatic connection retirement,
incomplete handler outcomes, and repeated query/cancel memory measurements.
It is a scoped service milestone. Captured backup/restore is described below;
scalar range indexes, application constraints and application transition parity
remain required before any live replacement is qualified.

### Worker evaluation failures

The embedded value-worker entry contract requires one complete, encodable
protocol result. Empty/nonsingle evaluation is distinct from a handler returning
an explicit empty result value. A failed evaluation returns
`(WorkerEvaluationFailure completion answer-count raised-error diagnostic)`.
Completion preserves fuel, cancellation, stack, capacity, invalidation, host
failure, effect denial and paused outcomes separately; an available single
language error is retained as the diagnostic. Profiled error replies preserve
their metrics field, including when the result cannot be encoded.

The Python worker client raises `WorkerEvaluationError` with those fields and
keeps the process after a valid correlated failure reply. That failure does
not imply rollback: effects may already have committed. A malformed, wrong-id
or timed-out reply still discards the transport process; it does not trigger an
automatic retry. `test-worker-failure-feedback` checks diagnostics, live-process
continuation and a committed write followed by failed evaluation without replay.

## Captured backup and restore

`(store:backup view destination-directory)` creates a self-contained native
store containing exactly that captured logical epoch/revision. The destination
must be an existing, empty, private owned directory whose parent entry is
already durable, as required for `store:open`. Success returns
`(StoreBackedUp epoch revision)`. An occupied destination returns a conflict;
the operation never overwrites an existing database or backup.

The operation reuses the native checked compactor to stream reachable keys and
value records into a new physical segment. It copies application records,
occurrences, indexes and receipts without a corpus-sized materialization.
Unreachable history is omitted. A captured view owns its original descriptor,
so later source commits, compaction, collection and owner closure do not change
the copy. No source writer lock is held while copying.

The destination has one exclusive publication owner throughout construction.
Before writing its segment, the owner synchronizes an invalid manifest and the
directory. A process killed during copying therefore leaves a destination that
refuses open/create as corrupt, rather than silently becoming an empty store.
After all live records have been checked and copied, the segment is synced and
the normal journal checkpoint publishes/syncs its manifest and directory.
Success is acknowledged only after these steps. Partial destinations are kept
for inspection; a failed attempt does not clear or initialize them.

`StoreUncertain` means the destination may have been published. Reopen it with
`store:open destination "existing"`, capture its view and compare its epoch and
revision with the requested capture. Snapshot/recovery confirms the visible
root through the existing native owner. Do not overwrite or blindly replay a
backup after an uncertain reply. An earlier I/O/corruption failure may leave an
intentionally incomplete destination; use a fresh empty destination for a new
attempt.

Restore consists of opening the completed copy with `"existing"` and using
the normal Space/library/service operations. Logical occurrence identities and
application retry receipts are preserved. Subsequent writes belong to its own
physical commit domain; a source view cannot authorize a transaction on the
copy. Preserve an archive by restoring into another fresh destination through
the same captured-backup operation before making writes. Independently
modified copies are not automatically merged or federated.

The example service exposes `(UseV1 view-token Backup (BackupV1 directory))` as
a Write route because it writes the destination filesystem. The destination
path is supplied by a same-UID caller under the trusted account-level service
configuration. A client needing less filesystem authority should use a handler
whose trusted configuration chooses the destination. Source queries themselves
remain read-only.

`test-native-disk-store` includes empty and nonempty copies, source mutation
and unlink/compaction during copying, physical-domain separation, each sync
boundary, partial writes, uncertain rename outcomes, killed copies and corrupt
source refusal. A separating negative control loses the invalid manifest and
is rejected by the reopen/create test. `test-native-kb-service` restores the
same occurrences through both service and embedded queries, resolves copied
receipts, refuses forged view handles and proves independent subsequent writes.
These tests use the native owner's existing filesystem assumptions; process
kills alone do not prove power-loss behavior on every filesystem/device.
