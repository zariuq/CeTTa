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
shared server must expose that same persistence and query core; its request
ownership, cancellation and full release qualification remain unfinished.

The first bounded service milestone targets the actual local ledger/queue workloads.
Its durability and isolation requirements remain strict while its API stays
small. The broader graph/query curriculum remains a subsequent milestone.

- Reuse the existing durable owner and shared Space matcher/source/cursors.
  Finish the public owning captured-Space adapter; keep persisted programs inert.
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

The missing integration is not covered merely by existing store unit tests or
the older named-space service. That service currently materializes query
snapshots; replace that path with shared incremental cursors before claiming
large-KB paging. Live migration remains a separately authorized step.

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
  This adapter has a complete prefix-scan fallback, not a selective atom index.

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
Space survives later commits and physical compaction. Per-request retirement
in a long-lived server is a separate, unfinished gate. Transactional payload
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

Qualification targets: `test-native-disk-space`, `test-persistent-space-api`,
`test-durable-space-transactions`, existing `test-space-source-cursor` and
`test-native-disk-store`. They cover fresh-process kill/reopen, changed symbol
allocation, duplicate/open atoms, inert programs, stable references, lost
replies, atomic edits, stale/absent-read conflicts and old readers across
publication/compaction. These scoped tests do not establish power-loss
durability beyond the native owner's documented filesystem/fault model, nor
complete the broader service, constraint, index or backup acceptance.

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
