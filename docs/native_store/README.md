# Native byte stores

`store` is a SQLite-free, persistent byte-key interface. Applications define
records, key layouts, indexes, queries, constraints and receipts in MeTTa.
The native owner validates snapshots and publishes one immutable root for a
whole batch, using the same CSL1 journal framing and checkpoint publication
as the space service. This is not an SQL interface.

The named-space and older durable-runtime interfaces have not yet migrated to
this owner. The byte-store library is available for qualification; adding it
does not migrate an application's existing database.

The disk ABI is version 2 (`cetta_disk_abi_version`), with a generation in every
`CettaDiskRoot`. A checksummed `CSD2` segment header carries the same generation
as the `CDS2` manifest. Reopening through the wrong segment is refused even
when its offsets and frame checksums happen to be valid. Earlier prototype
`CDS1` manifests are reported as a different version, never reset or silently
read as version 2. Preserve and re-export those offline prototype stores with
their matching implementation. Portable bindings must check the ABI before
passing a root structure.

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
