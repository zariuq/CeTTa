# Native byte stores

## Bounded logical collection transfer

`(library space_transfer)` copies ordinary durable collections through the
existing captured byte cursors and atomic publication owner. It introduces
no second fact store or matcher. Physical backup/restore, described below,
preserves a store's epoch and occurrence identities. A logical copy creates
new destination occurrences and records their origins explicitly.

```text
captured collection → header · row · row · completion footer
                                      |
                         private disk-backed operation batch
                                      |
                 facts + indexes + origin map + receipt → commit
```

Embedded export uses `durable-space:export-open view collection frame-bytes`,
`export-next state` and `export-close state`. Open returns
`(SpaceExportOpenedV1 state header)`. Next returns
`(SpaceExportMoreV1 next-state row)` or
`(SpaceExportCompleteV1 footer)`, closing the captured owner at completion.
Each step reads one key and at most one bounded value. The original view
may be closed after open. Publication, compaction and collection do not
change the exported snapshot. Metadata count mismatches fail at completion.

The portable records are:

```metta
(SpaceExportHeaderV1 source-epoch source-revision source-collection count index-specs)
(SpaceExportRowV1 sequence original-occurrence encoded-CAV1-atom)
(SpaceExportEndV1 count chain-digest)
```

Row sequence starts at zero; source keys must be strictly increasing in
byte order within the source collection. Index specifications travel;
postings are rebuilt from the original atoms. Payloads stay CAV1 bytes so
open variables, sharing within one atom, Booleans versus symbols, numeric
representations and callable-looking data remain distinct and inert. The
digest starts as SHA256(CAV1(header)); each row replaces it with
SHA256(CAV1(`(SpaceExportLinkV1 previous-hex-digest row)`)). This is an
integrity/retry fingerprint, not a signature or source-authentication claim.

Import uses `durable-space:import-open owner view new-collection request-id
header expected-digest spool-maximum`, followed by `import-row state row`
and `import-finish owner state footer`. The destination collection must be
absent, without orphaned rows, postings or import-map entries. Open returns
`(SpaceImportOpenedV1 state)` or an existing durable receipt. Row returns
`(SpaceImportMoreV1 next-state (SpaceImportMappingV1 origin destination))`.
These mappings are provisional until finish commits. `import-close` cancels;
`import-progress` reports rows, retained spool bytes, maximum and staged ops.
All continuations are linear: use the returned state once, then retire it.
A copied older state refuses once its shared spool advances.

No collection data or successful receipt is visible during staging. Finish
checks declared/received counts, ordered origins, canonical payload encoding,
computed/footer/expected digests and the current spool operation count.
It commits facts, metadata, rebuilt exact/range indexes, origin mappings and
the request receipt together under conservative whole-revision validation.
A competing publication conflicts; cancellation, failure, disconnect or
process death before publication discards the unlinked spool. Publication
uses the existing journal and synchronization rules. Lost replies require
receipt resolution before a retry; the same command replays without copying
again, while a changed header/digest with the same request id collides.

Destination identities are derived from destination epoch/collection,
request and sequence. Equal payloads remain distinct. References *inside*
payloads keep their original meaning; import never rewrites arbitrary
terms. After commit, `durable-space:import-origin view collection request
origin` returns `StoreDecoded(SpaceImportMappingV1 origin destination)`, or
`SpaceImportOriginAbsentV1`. Applications can translate references explicitly.
Exact whole-store restore remains the operation for preserving all identities.

`examples/native_transfer_server.metta` adds these routes to the same query
server. ExportOpen borrows a snapshot via `UseV1`; ExportNext/ExportCancel
use the returned linear `UseValueV1` ticket. ImportOpen similarly borrows a
snapshot, then ImportRow/ImportFinish/ImportCancel use linear tickets.
Export and origin lookup require Read; import staging/publication require
Write. Cancellation remains available after read-only attenuation. Tickets
are connection-owned; disconnect releases native handles and staging files.
No client-selected server file path is involved.

`tools/cetta_space_transfer.py` is an optional independent Python client;
the MeTTa server and embedded library require no Python runtime. It stores
`CSX1` plus big-endian four-byte-length CAV1 frames: header, rows, footer,
then EOF. Frames are at most 128 KiB. Import validates the complete file,
rewinds and streams it again; the publisher independently checks the expected
digest. Memory retention is bounded by a frame and the server's existing
disk-backed batch, rather than the collection size. Export creates a new
file, fsyncs it and its directory, and refuses overwrite. An interrupted
file lacks valid completion and is refused by import.

```sh
python3 tools/cetta_space_transfer.py service.sock export claims claims.csx
python3 tools/cetta_space_transfer.py service.sock import copied-claims copy-001 claims.csx
```

The initial transfer contract accepts V1/V3/V4 collections, with a 128 KiB
maximum encoded row and an explicitly bounded spool (the example service
allows 64 MiB). A larger single fact refuses explicitly; it is not silently
omitted. This format is a portable application archive, not ACT or a new
durable storage format. The existing durable owner still owns recovery.
V2 collections require the existing reindex upgrade before logical export.

`test-space-transfer` compares encoded bags, variable sharing, duplicate
memberships, durable origin mappings and ordinary/range queries after copy.
It exercises embedded/service/file execution, snapshots across publication,
malformed/truncated/reordered streams, stale spool aliases, frame/spool
limits, read-only and foreign-ticket refusal, conflicts, disconnect/crash,
lost-reply receipt recovery, compaction and repeated cancellation cleanup.
These are scoped transfer checks, not a hardware power-loss proof or full
application-transition qualification.

## Relational composition over ordinary Spaces

`(library kb_query)` now connects its existing MeTTa join/optional/semi/anti
and union control flow to the ordinary captured Space cursor. It adds no
native relational matcher or separate persistent KB schema:

```metta
(kbq:open-space view (KqSpace collection pattern template) maximum-row-bytes)
(kbq:page cursor row-count work-steps)
(kbq:inspect-space cursor)
(kbq:close cursor)
```

Open returns a linear `KqCursor` owning an independent clone of the captured
root. The original view can be released after open. Each Space leaf uses the
same unifier, candidate indexes and request-owned cursor as ordinary matching.
It returns `(SpaceAnswerV1 instantiated-template occurrence-references)`;
conjunction references remain in premise order. This is a projected answer,
not a copy of the original stored fact. `durable-space:read` retrieves the
original occurrence. Duplicate memberships remain distinct until an explicit
projection/distinct operation discards that information.

`KqBind`, `KqOptional`, `KqSemi`, `KqAnti` and `KqUnion` retain their existing
MeTTa semantics. A dependent inner leaf opens against the same captured root,
even if publication/compaction happens before it is reached. Templates may
explicitly carry outer occurrence references into later results. Projection
does not automatically accumulate provenance from discarded outer fields.
Optional absence uses its explicit caller-provided value; an empty tuple is
an ordinary present value. Paused or failed inner execution is never absence.
Semi/anti stop once a right answer settles existence; unused later work is
not executed. Union preserves multiplicity.

Additional ordinary MeTTa row stages are:

```metta
(KqMap source function-name)
(KqSelect source predicate-name)
(KqDistinct source equivalent-name maximum-encoded-state-bytes)
(KqFold source initial step-name maximum-encoded-state-bytes)
(KqGroup source key-name equivalent-name initial step-name maximum-encoded-state-bytes)
(KqOrder source less-name maximum-encoded-state-bytes)
(KqSlice source offset count)
```

Callbacks are explicitly invoked pure, deterministic, single-answer functions.
Select uses the existing `RelTrue`/`RelFalse`/`RelUnknown` policy; comparisons
use the existing Boolean contract. Zero/multiple callback answers and invalid
predicate values fail explicitly. Callback arguments are freshened before
invocation, preserving sharing within each argument group without allowing a
filter or comparator to bind a source row or its continuation tail. Ordinary
Space queries capture an owned program observation as described below. These
callbacks must not perform effects or hidden/external reads. The library does
not automatically enroll arbitrary callback reads into a transaction.

Distinct takes an explicit equivalence. `kb:alpha-equal` compares canonical
open terms, including within-row variable sharing; `kb:equal` requires ground
values. Fold incrementally retains one accumulator. Group reuses the existing
`rel` grouping machinery and retains groups rather than the input corpus.
Fold emits its specified initial value on empty input; an empty group query
emits no groups. The example numeric accumulator demonstrates count, sum,
minimum, maximum and average with explicit absence for empty extrema/average.
Numeric and null/coercion policies belong to the chosen functions.

Order buffers only its bounded query rows, then reuses the existing merge sort.
Equal primary keys compare canonical CAV1 row bytes, including carried stable
occurrence references. Equal projected rows remain duplicates. Slice advances
incrementally and closes unused input when its limit is reached; count zero
does not invoke source callbacks. Fold/group/order have blocking output: they
must establish complete input before returning a final aggregate/sorted row.
Explain reports this distinction rather than calling them streaming output.

State budgets bound encoded retained values, not an exact heap/CPU charge.
The service additionally accounts for actual retained continuation resources.
An oversized state/row or incomplete evaluation is a failure, never successful
empty output. Each logical step advances bounded matcher work, but a pure
callback or final bounded sort is not a fixed wall-clock quantum. `KqWork`
reports logical steps, candidate visits and decoded bytes, not all physical
index I/O or evaluator instructions. Inspect shows current active access paths.
`KqSpaceObservationV1` is descriptive; query-informed writes retain actual
data/program handles through the ordinary collection writer.

`examples/native_space_query_server.metta` reuses the existing service owner
and linear continuations with Read routes RelOpen/Page/Inspect/Cancel. RelOpen
uses an existing view and `(RelOpenV1 plan maximum-row-bytes)`. Other operations
use `UseValueV1`; page takes `(RelPageV1 count work-steps)`. Example limits are
128 KiB per row,32rows and4096logical steps per page. A large page can still
exceed the transport frame and fail explicitly; choose smaller pages for large
rows. Disconnect, cancellation, completion and failure release query owners.

Service callbacks must be fixed names declared in the trusted application
program as `(QueryPureCallbackV1 function-name)`. The generic server declares
none. It validates all plan branches before opening; data-dependent callback
names are refused. Policy-looking facts in the stored KB add no authority.
ReadOnly clients retain read/query/cancel access, not permission to invoke any
loaded library function. Approval still requires review of the function's
effects and dependencies; a name allowlist does not prove purity. Embedded
applications use their normal explicit invocation authority.

`examples/kb/space_curriculum.metta` installs synthetic claims/evidence and
program/dependency data in one ordinary persistent collection, exposes the
same plans embedded/server, and combines graph and relational handlers over
that collection. Four supporting occurrences have only two source origins;
judgement, evidence and source occurrence references survive the join. The
lesson reports origin dependence without inventing numerical evidence weights.
It covers optional evidence, grouping, distinct origins, two-hop dependencies,
graph reachability, one-occurrence retraction and fresh-process recovery.

`test-space-query` compares generated joins against SQLite and raw Space
matching, with bindings, bags, identities, paging order, quotas, callback
failures, policy refusals, captured reads and cleanup. `test-space-curriculum`
checks the two datasets in embedded and service modes across restart and
compaction. The full KB/DB qualification remains unfinished.

### Program observations and query-informed writes

An ordinary-Space relational or graph cursor owns a native program watch in
addition to its captured data view. Each step checks the watch before and after
advancing. Changes to the application equation/declaration projection, its
base/module dependencies, language/profile, imported-library availability,
admission or library paths produce `(StoreRefused program-invalidated)`.
Root data-only edits do not invalidate this program observation. The check is
conservative: even an unrelated equation edit may invalidate a query. Earlier
pages remain provisional if the eventual result is incomplete or invalidated;
no mixed-code row from the failing step is returned. Attempted work is counted.

PeTTa's optional specializer can publish generated equations on a first call.
While a program is observed, specialization publication for its captured
root/base/module closure is deferred; existing canonical execution remains
authoritative. Authored edits remain live, and invalidate observations. Other
programs can specialize normally; closing the last observer permits future
specialization again. This avoids treating compiler-generated clauses as an
application edit without guessing from generated symbol names. Canonical
execution can cost more than specialization on some workloads.

The low-level operations are `store:program-watch-open`,
`store:program-watch-clone` and `store:program-watch-check`. A clone retains
the original observation, including its invalidation; it never recaptures a
newer program. Watches own metadata, not source Space headers. Saving a printed
handle or program identity grants no authority, and a retired/reinitialized
root cannot revive an old observation. Close a watch with `store:close`.

For decisions that will cause writes, capture the context before consuming
the query, retain it through complete execution, and use the shared writer:

```metta
(kbq:read-context cursor) ; (DurableReadContextV1 data-view program-watch)
(kbq:commit-read owner context collection request-id changes)
(kbq:commit-read-command owner context collection request-id command changes)
(kbq:close-read-context context)
```

Graph cursors expose `kbg:read-context`; both use `(library durable_query)` and
`durable-query:commit`, `durable-query:commit-command`,
`durable-query:close-context`. Each context independently owns its view/watch
and survives cursor completion. It provides dependencies, not write authority
or proof of completion. Embedded callers must establish complete execution
themselves and keep all influencing reads on that view. Arbitrary callback
reads are not automatically enrolled.

Receipt lookup comes first: the same request/command replays an existing
receipt despite stale data or program observations; a changed command refuses.
For a new operation, the original data view and program watch are validated
through `store:commit-guarded`, publishing facts, postings, counts and receipt
in the same native commit. Program validation and publication share the
existing physical-transition critical interval. Concurrent evaluators must
enter the existing shared-execution scope; the local service serializes
application execution. No MeTTa callback runs under the publication guard.

The example service adds `RelOpenRead`, `RelReadPage`, `RelReadCommit` and
`RelReadCancel`. Open has the same `(RelOpenV1 plan maximum-row-bytes)` argument.
Pages use `(RelPageV1 count work-steps)`. Completion retains an opaque linear
read context; only that completed phase accepts
`(RelReadCommitV1 collection request-id command changes)` with at most 128 edits.
The commit route requires Write authority. A read context cannot grant it.
Invoking a wrong phase consumes/releases that state; a host-level permission
refusal leaves it available for cancellation. Disconnect/cancel releases all
views and watches. Contexts are process-local: after restart resolve durable
receipts before starting a fresh query or retrying a write.

`test-program-query` covers live/transitive/declaration/import/absent-callee
invalidation, unchanged cold calls, data-only changes, graph decisions,
receipt-first retries, stale/closed/forged observations, phase/permission
checks, lost replies, restart and repeated cancellation/disconnect. Native
tests cover specialization deferral/restoration, enrollment failures and an
actual code-edit/publication race. The separate sequential Lean model does
not prove the C implementation, optimizer equivalence or filesystem behavior.
Callback approval and these watches are not a sandbox: impure callback
effects cannot be rolled back by discarding their query result.

For native ACT snapshot readers backing ordinary MeTTa spaces, see the
[ACT/Space integration requirements](ACT_SPACE_REQUIREMENTS.md). That document
separates byte-format compatibility from occurrence, query and durable-owner
acceptance; it is a design contract, not an implementation-completion claim.

The [DAS interface intake](DAS_INTEGRATION.txt) records a pinned upstream API
review and external-Space qualification gates. It covers reusable streaming,
cancellation and authorization mechanisms, while keeping occurrence identity,
captured revisions and publication guarantees explicit. No DAS adapter is
implemented by that design checkpoint.

The subsequent scripted-source checkpoint is qualified by
`test-external-space-source` and `test-space-source-cursor`. An occurrence
source can yield `SPACE_OCCURRENCE_CURSOR_PROGRESS` for one bounded empty
poll; ordinary query cursors yield progress and PeTTa match/pat retain their
choice and suspend. Only explicit source exhaustion completes a read.
The synchronous transform optimization hands occurrence sources to its
authored MeTTa implementation before opening/polling them. The finite external
fixture checks replay, membership identities, captured generations, profile
refusals, revocation and cancellation acknowledgement, with an executable MeTTa
polling reference. See the intake's scripted-source section for its limits:
this is not a live DAS client, backend snapshot proof or foreign write context.

`store` is a SQLite-free, persistent byte-key interface. Applications define
records, key layouts, indexes, queries, constraints and receipts in MeTTa.
The native owner validates snapshots and publishes one immutable root for a
whole batch, using the same CSL1 journal framing and checkpoint publication
as the space service. This is not an SQL interface.

The named-space and older durable-runtime interfaces have not yet migrated to
this owner. The byte-store library is available for qualification; adding it
does not migrate an application's existing database.

## Exact occurrence matching

`(store:space-point-query-open view key pattern template)` opens the same
owning Space matcher over one exact stored CAV1 occurrence. Its cardinality is
established by lookup; keys that merely share its prefix are excluded. The
result is `StoreQueryOpened`, or `StoreAbsent` for a missing key. Empty keys
have the byte store's usual meaning. The original `StoreOccurrenceV1` identity
and captured revision are preserved. Patterns/templates remain jointly scoped
data, with the existing unifier, replay, checkpoint, cancellation and cleanup.
The caller's view/owner may close after open. No write authority is acquired.

This operation lets an index-selected occurrence return through ordinary
matching without copying a corpus or inventing a filtered Space cardinality.
Declared scalar ranges use this operation as described below.
`test-native-disk-query` and
`test-space-point-query` cover exact/prefix-related and empty keys, missing
records, captured values, identities, checkpoints and bounded repeated cleanup.

`store:scalar-order left right` compares portable stored scalars with the
existing PeTTa term-order contract and returns `(StoreScalarOrderV1 -1|0|1)`.
`store:scalar-range value lower upper` is the corresponding stored-data
predicate, returning `StoreScalarWithinV1` or `StoreScalarOutsideV1`. Scalars
are symbols, strings, Booleans, integers, floats, big integers and rationals.
Variables, expressions and native resources are outside this projection.
Bounds are `NoBound`, `(Inclusive scalar)` or `(Exclusive scalar)`; invalid
bounds refuse explicitly. The comparison preserves the established mixed
number, NaN, signed-zero and float/exact tie rules; it adds no coercion.

`store:integer-window lower upper` returns `StoreIntegerWindowEmptyV1` or
`(StoreIntegerWindowV1 first-ordered-key exclusive-end-key-or-NoBound)`.
The keys use `store:key-i64` ordering. At most 65 existing term comparisons
per bound find the exact Int64 candidate interval without converting integers
to doubles. Other scalar families need their own candidates and residual
predicate; this window alone is not a complete heterogeneous range query.
`test-store-scalar-window` compares MeTTa term-order references and an
independent exact ceil/floor oracle. Declared posting maintenance and bounded
range cursor/service integration are described below.

## Stored scalar ranges

Declare `(SpaceRangeIndexV1 expression-arity field-position)` through
`durable-space:create-indexed`. It can coexist with an exact `SpaceIndexV1`
on the same projection. New mixed descriptors use `StoreSpaceV4`; exact-only
and unindexed collections retain their existing versions. Facts, old/new
postings, counts, metadata and receipts still publish in one native owner
commit. `durable-space:reindex-open/page` rebuilds both descriptor kinds through
the same bounded staging/batch mechanism. A range descriptor hidden under V3
refuses; historical V2 stays scan-only until its existing upgrade operation.

```metta
(KqRange collection arity position lower upper pattern template)
```

Import `(library kb_query)` and open/page/inspect/close this plan with the
ordinary `kbq:open-space` interface. Positions include the expression head at
zero. The stored record must have the declared arity and a portable scalar at
that position. This is a stored-data projection: whole-variable records,
unbound fields and compound fields are outside it, even if a separate ordinary
match could instantiate one numerically. They remain queryable with `KqSpace`.
Range filtering does not bind variables or evaluate stored code.

`pattern` is one atomic row pattern; conjunction/alternative roots refuse.
Compose relational conditions with the existing KqBind/Optional/Semi/Anti
operators. Each eligible exact occurrence is passed to the same Space matcher
and instantiated data template used elsewhere. Results are ordinary
`SpaceAnswerV1` values with original membership references. Duplicate payloads
retain distinct occurrences and ordinary matching owns all variable bindings.

Bounds and scalar ordering use the contract above. Integer postings seek the
exact Int64 candidate window; all other portable scalars retain their separate
candidate bucket and residual comparison. An empty integer interval never
discards other scalars. Without a declared range index, the same predicate
consumes a complete captured Space scan with count/identity validation. No
corpus is copied before a first page. A candidate may be decoded for its stored
field and again by its owning point matcher; counters include those actual
logical visits/decoded bytes. This is not a physical index-I/O or CPU charge.

Range answers are bags. Index and full-scan enumeration order may differ.
Use KqOrder for ordered pagination, retaining its existing membership
tie-breaker policy. Explain/inspect distinguish integer seeks plus other-scalar
fallback from complete scans, and show the active scan/point statistics.
Pages retain their captured root even when publication, compaction and
collection occur before a later candidate is matched. Malformed or stale extra
postings, missing referenced facts and wrong domains fail explicitly rather
than become successful empty output. Index completeness relies on the same
qualified-writer/metadata trust boundary as existing exact candidate indexes.

The existing RelOpen/Page/Inspect/Cancel service routes accept KqRange without
a second transport or matcher. RelOpenRead/ReadPage/ReadCommit retain its same
data/program observations through completion. Full-revision validation covers
absent-range phantoms as well as positive reads; write authority is unchanged.
Cancelled/disconnected states release their scans, point owners, views and
watches. Incomplete execution and errors remain distinct from absence.

`test-scalar-indexes` checks posting bytes, cross-family updates/deletion,
ordinary match compatibility, mixed descriptors, repair, retries and corruption.
`test-scalar-ranges` compares index/full-scan results with an independent exact
ordering oracle and exercises joins, ordered paging, invalid inputs, service
replay, captured compaction, data/code conflicts, absent-range phantoms, restart
receipts and repeated cancellation/disconnect. Scalar-family access paths are
explicit; acceleration of every other scalar family is a later measured
optimization. Broader whole-project scaling/application acceptance remains open.

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
available. The explicit rebuild below is required before such a collection
resumes writes. It is not silently relabelled as V3. New V3
collections use data-preserving field selection. This is a collection metadata
version, independent of the native CSD2 storage ABI and language selectors.

Complete posting keys must fit the native key limit (currently 65,536 bytes).
An otherwise portable wide string may therefore fit an unindexed collection
but refuse in an indexed projection. Such a write returns `StoreRefused invalid`
atomically, without a fact or success receipt. No posting is silently omitted.
Individual declarations, records and staging messages remain subject to the
existing codec/key limits. A paged rebuild can stage a transaction larger than
one codec message without materializing its entire operation list.

### Paged index maintenance

Import `(library durable_reindex)` and use:

```metta
(durable-space:reindex-open owner view collection request-id maximum-spool-bytes)
(durable-space:reindex-page owner state steps)
(durable-space:reindex-progress state)
(durable-space:reindex-close state)
```

Open returns `(SpaceReindexOpenedV1 state)` or a receipt replay/error. Page
returns `(SpaceReindexMoreV1 successor progress)` until it returns the final
commit/error. Treat the state as a linear owner: continue with its successor,
or close it when abandoning the operation. It retains an independent captured
view, a temporary native batch and at most one active scan/query cursor.
An invalid embedded page argument is refused before advancing; the caller
still owns that state and must close it or retry with valid arguments. The
example server retires a state when an invoked page handler rejects its limit.
Progress is `(SpaceReindexProgressV1 phase rows count spool-bytes maximum
staged-operations)`, with phases `Clearing`, `Filling`, and `Ready`.

The library clears every declared posting range in the private batch, scans
the original facts through the ordinary Space cursor, and stages corrected
postings. It preserves fact records, occurrence identities and multiplicity.
Only after the full scan finishes and its count agrees does it stage current
metadata and `(KnowledgeReceiptV1 request-id (SpaceReindexV1 collection)
(SpaceReindexedV1 collection count))`. One whole-revision commit publishes all
of these together. Concurrent writes conservatively conflict, including writes
to other collections. A conflict or quota/error response does not certify any
part of the new index. An uncertain commit uses the existing recovery/receipt
resolution contract. A completed identical request replays its receipt.

V1, historical V2 and current V3 metadata are accepted explicitly. V2 stays
scan-only and refuses normal edits until publication completes. Old captured
readers keep their original view through publication and compaction. A large
record or posting still has to fit its individual codec/native key limit.

The reusable native primitives are `store:batch-open`, `store:batch-add`,
`store:batch-info`, and `store:commit-batch`. They use two roots in one private
CSD2 file to normalize puts/removals by key; an empty value remains different
from removal. The file is unlinked immediately. Closing or crashing discards
unpublished operations; the rare name left between creation and unlink is
collectible. Staging does not publish a revision or create a durable resumable
job. After a crash, start a new rebuild or resolve its final durable receipt.
These primitives confer no owner/write authority and add no retry receipt by
themselves. They reuse the existing byte index, lock, journal and sync path.

The spool limit bounds retained disk extent, not the final published store or
aggregate disk use across clients. A refused append may temporarily write one
candidate beyond that limit before truncating it; failure to truncate poisons
the batch. Heap use consists of traversal state and bounded input/value data,
not a corpus-sized edit list. Clearing/filling are paged; final native commit
is synchronous and holds the writer lock while applying the normalized batch.
It cannot be preempted by a service cancellation. Larger maintenance therefore
still needs measured publication latency before use on a busy service.

The example server adds `ReindexOpen` and `ReindexPage` as Write routes and
`ReindexCancel` as a Read route, so cleanup remains available after ReadOnly
attenuation. Open borrows a view with `(ReindexOpenV1 collection request-id
maximum-spool-bytes)` and returns a connection-owned value state. Page uses
`UseValueV1` with `(ReindexPageV1 steps)`; Cancel uses `UseValueV1` too. Its
example limits are 64 MiB per spool and 128 steps per page. Disconnect retires
the owned state. Retrying the exact last transport request follows the existing
linear-continuation replay rule.

`test-durable-reindex` covers a rebuild larger than one CAV1 message, duplicate
occurrences, open/shared variables, Boolean-like symbols/native values, inert
rules, scan-only V2 with a missing posting, old readers, a competing write,
quotas, cancellation/disconnect, lost replies, process restart, receipt replay,
compaction and resumption of normal indexed edits. It also checks fixed-KB
cancellation lifetimes. Native batch fault tests cover partial writes, sync
failures, kills during creation/staging/publication, uncertain publication,
last-operation normalization and domain/revision checks. These are scoped
failure experiments, not a hardware power-loss proof.

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

### Graph traversal through ordinary Space cursors

`(kbg:open-space view collection arity plan maximum-bytes maximum-states)`
uses the existing MeTTa graph walker over `durable-space:cursor`. Import
`(library kb_graph)`. Arity includes the relation head; field positions in
`(KgRelation relation source-position target-position)` exclude it. For binary
`(depends parser checker)` edges, use arity three and positions zero and one.

```text
     parser --(x2)--> checker ---+
       |                        v
       +-----------> optimizer → runtime
       ^                         |
       +-------------------------+

two parser→checker occurrences remain two edges
reachability returns each endpoint once
shortest/all retains distinct tied edge-occurrence paths
```

Plans are `KgNeighbours`, `KgReach`, `KgPaths`, `KgTriangles` and `KgShortest`,
as exercised in `test-space-graph`. They select incoming/outgoing direction.
Paths carry nodes, stable edge occurrences and distance. Bounded paths enumerate
walks with an explicit finite maximum. Triangles are directed anchored cycles
with three distinct vertices. Reachability uses positive-length paths, so a
cycle can make its start reachable. Shortest `one` chooses one tied path; `all`
returns every tied occurrence path. A start equal to its goal has a zero-length
seed path. No separately stored vertex catalogue is implied by this edge view.

The profile projects actual stored expression occurrences with ground relation
and endpoint identities. A match template may instantiate a stored variable;
that alone does not establish a ground stored edge. The adapter retrieves the
original occurrence before admitting it. Open rows still participate normally
in ordinary `match`; they are excluded from this finite graph projection.
Computed/open relations need a separately specified finite-domain source.

Graph identities use exact portable values. Integers, floats, signed zero,
native Boolean values and Boolean-like symbols remain distinct data where their
encodings differ. Data selectors keep an item in a constructor until it is
bound, preventing expression completion from interpreting it. Safe symbol/string
probes use candidate indexes. Other identities use broader candidates and exact
post-filtering; connective-looking relation heads use a complete scan. Stored
rules and callable-looking identities remain inert.

`kbg:page` returns a bounded page plus `KgMore`, `KgPaused`, `KgComplete` or
`KgFailure`. The cursor owns an independent clone of its captured root. Close
with `kbg:close`; end/error closes it automatically. Both frontier and visited
state have an explicit state allowance. This reference frontier uses linear
visited lookup and does not claim an optimized graph planner. `KgWork` counts
graph steps, candidate visits and decoded fact bytes, including successful
original-occurrence reads. It does not count all metadata/tree I/O. Where a
failure prevents observing counters, no complete cost accounting is implied.

`kbg:inspect-space` describes captured coordinates and the current adjacency
cursor's observed `StoreQueryInfo`, when active. `kbg:observe` returns
`KgSpaceObservationV1` coordinates for this source; they are descriptive, not
a writable read-set certificate. Keep the original informing view for a write
and validate its whole revision. The older `kbg:explain`/point observation
contract describes the legacy KbFact reference source, not this collection.

`examples/native_space_graph_server.metta` reuses the existing server and adds
GraphOpen, GraphPage, GraphInspect and GraphCancel handlers. They retain the
MeTTa cursor using the same connection-owned continuation protocol. No graph
traversal is implemented in the host. The test fixture invokes the same walker
embedded, including after closing its original owner/view. Generated finite
graph oracles and adversarial cases compare both paths with exhaustive results,
including multiplicity, stable identities, ties, retractions and compaction.
The full relational and claims/evidence curriculum remains separate work.

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

The example exposes Snapshot, Create, Query, Next, Cancel, QueryInfo,
ReadOccurrence, Commit, ReindexOpen/Page/Cancel, Resolve, Backup and Compact.
Create accepts either `CreateV1` or
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
