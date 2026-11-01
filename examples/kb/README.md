# A metagraph database in MeTTa

## Using persistent Spaces

```metta
!(import! &self (library db))
!(db:open claims "claims.db")
!(db:transaction claims
   (|-> ($facts)
     (let* (($_ (add-atom $facts (supports e1 c1)))
            ($_ (add-atom $facts (source e1 paper7))))
       done)))
!(match (db:snapshot claims)
        (, (supports $e $claim) (source $e $origin))
        ($claim $origin))
!(db:close claims)
```

This creates or reopens a native store in `claims.db`; its immediate parent
must already be a private, process-owned directory with a durable entry.
`claims` is a PeTTa named-state connection. A connection value may
also be kept in a local `let`. Re-running these inserts preserves duplicates,
as ordinary Spaces do.

`db:snapshot` returns a pinned read-only Space. The transaction callback gets
an ordinary editable Space with read-your-writes. Its complete answer bag and
changes publish together; an empty computation, error or incomplete evaluation
publishes no changes. The temporary edit Space closes on every outcome.
Programs stored as atoms remain data. No transaction body is retried automatically.
Same-store nested transactions/publications are refused, including through
a second connection to the same directory; this interface does not provide savepoints.
Effects on files, named state, other stores or external services are outside
this database's rollback/isolation contract.

`db:add connection atom` and `db:remove connection pattern [maximum]` are
one-operation transactions. The two-argument removal preserves PeTTa's
remove-all behavior; the optional non-negative integer maximum is an upper bound on occurrences.

For large results, use `db:query connection pattern template`, followed by
`db:page cursor answer-count work-quantum`. It returns
`(db:page-result rows status)`. A simple-query row is
`(db:row value occurrence-references)`; advanced plans retain their result
values. Status is `db:complete`,
`(db:more cursor)`, `(db:paused cursor)` or `(db:failed reason)`. A zero work
quantum preserves a paused continuation. Completed and failed execution paths release their
resources; invalid-argument errors leave the unconsumed cursor available for
correction or closure. Use `db:close cursor` when abandoning a live continuation. Ordering
remains explicit in the existing query plans, available through `db:query-plan`.

Services can retain a request identity and operation description, then use
`db:transaction-result connection collection request operation body`.
A matching receipt returns the original answers before considering the body.
`db:resolve connection collection request operation` recovers and inspects that
receipt after an uncertain reply. A missing database receipt does not authorize
repeating an external effect. Ordinary calls return values or `(Error ...)`;
the explicit transaction result also distinguishes committed, replayed and aborted.

`db:ensure connection collection index-specs` creates a named collection.
`db:snapshot` and `db:transaction` accept a collection argument when needed.
The default collection is `data`. Public `db` operations are currently qualified
on PeTTa. They use the existing store owner, Space matcher and compositional
query libraries. Remote connection construction is not exposed by this module.

## Atomic constraints and graph lessons

`constraint_curriculum.metta` implements reference and uniqueness checks as
ordinary transaction-body queries over the editable Space. A missing endpoint,
duplicate edge or removal of a referenced entity returns an `Error`; the whole
transaction publishes nothing. Removing the edge and then its former endpoint
commits atomically. A fresh process verifies the result and receipt replay.

```sh
make test-constraint-curriculum
```

`graph_curriculum.metta` uses the dependency collection for outgoing and
incoming neighbourhoods, reachability, bounded paths, all shortest paths and
anchored triangles. Parallel `emit→parse` occurrences produce two distinct
shortest paths and two distinct triangle paths. This keeps occurrence identity
visible rather than silently converting the graph to a set.

## Durable application queue

`space_job_queue.metta` implements controlled queue transitions using ordinary
facts, captured queries, indexes and one atomic publication per operation.
It never runs a tool. Its transition shape is qualified against the existing
execution gateway's SQLite `Store` class when that reference is supplied.

```text
submit → queued → running → succeeded / failed / cancelled
            |        |
         cancel      +→ caller-requested recovery:
                         interrupted read  → failed
                         interrupted write → uncertain
```

```sh
mkdir -m 700 queue-space
./cetta --lang petta --run-contract examples/kb/space_job_queue_run.metta "$PWD/queue-space"
```

The example uses synthetic time, submits and claims one job, and prints its
facts and ordered receipts. Repeating it replays those operations. A committed
or replayed queue outcome grants no authority to repeat an external action.

The service entry is `jobs:configuration`, using `CETTA_KB_DIRECTORY`,
`CETTA_KB_SOCKET` and `CETTA_JOB_AGENT`. Embedded `jobs:run` and service `Apply`
use the same transition function. The service offers captured `Snapshot`,
`Find`, `EventsOpen`/`RelPage`/`RelCancel`, `Resolve` and `Compact` operations;
its write surface excludes arbitrary fact edits. Effect labels must come
from the trusted dispatcher configuration. This is storage, not a dispatcher.

Ready and running jobs expose separate Int64 order projections. The explicit
`KqIndexedI64Range` cursor gives FIFO order without scanning terminal history.
Job IDs and sequence values are unique through the closed transition policy;
facts, changed indexes, event receipts and retry outcomes publish together.
Observation time is not a new logical command on retry. A changed intent
under the same operation ID refuses. Per-job receipt sorting is explicit.
Original job membership identities survive state updates and restart.
Submit retries use the trusted dispatcher's fingerprint as the intent identity;
a changed deadline alone does not create another job.

Transition commands carry a trusted observation time. The caller must take
that observation at its decision boundary; the queue does not authenticate a
client-supplied clock or tool classification. A production gateway adapter
still needs that caller qualification before replacing its SQLite store.

Storage-server restart preserves running jobs. Dispatch recovery is a separate
caller action: the store cannot infer whether an external process still runs.
The qualification client under `tests/support` never dispatches anything.

`make test-space-job-queue` checks core transitions, races, durable retries,
restart, more than 128 jobs, atomic recovery of four 300 KB payloads, malformed
recovery-row refusals, read-only rights and cleanup. For actual SQLite
transition comparison, pass `--reference-store /path/to/execution_gateway/store.py`
to `tests/support/check_job_queue.py`. The source hash and scope are recorded.

This does not qualify live migration. The portable-value allowance defaults
to one MiB and is selected before loading with `--value-bytes N`. The large-value
qualification uses an 8 MiB allowance and compares a 2 MiB request against the
actual SQLite store, embedded and served. Smaller policies refuse explicitly.
No job-count limit was introduced.

## Computed properties with the native memo

`space_computed_curriculum.metta` derives syntax-node counts from ordinary
stored program records, using the existing `lib_memo` implementation. It adds
three memberships to the existing curriculum collection:

```text
two equal emit records: Seq ── Use parse    check record: Use parse
                         └─── Use parse
node counts:                  3, 3                     1
```

```sh
mkdir -m 700 computed-space
./cetta --lang petta --run-contract examples/kb/space_computed_run.metta "$PWD/computed-space"
```

The same `KqSpace`/select/map plan runs embedded and through
`space-computed:configuration`. It scans ground stored rows incrementally,
then passes each syntax value explicitly to the recursive pure callback.
Equal syntax can reuse its numerical calculation while every result retains
its own occurrence reference. No whole-KB copy or durable cache is created.

Every data dependency must be in the memoized function's arguments. This
example reads no mutable Space, clock or external service inside that function.
Existing native memo invalidation follows helper-code changes; the query's
program watch refuses mixed-code continuation. A new data value has a different
argument key. Program code itself is loaded explicitly; stored syntax and
code-shaped fields remain inert. Unsupported ground syntax returns
`ComputedSyntaxRefusedV1` with provenance. Open stored rows cannot manufacture
memo arguments through schema binding.

The lesson uses the native memo with unaggregated results and its uncached
MeTTa reference for incompatible policies. It preserves the caller's settings
and returns the same node counts. Policy changes invalidate active program
watches, including a change followed by restoring the original policy. Cached
refusal statuses attach the original input outside the cache, so floating-key
normalization cannot substitute another input in the diagnostic.

`make test-computed-queries` checks native hit/miss counters, duplicate
occurrences, captured old data, new data/code answers, informed commits,
retractions, restart/receipts and repeated cancellation/disconnect. The first
query has two native cache misses; repeating it increases hits without further
misses. The cache is process-local and optional: correctness must not depend
on a warm entry. This qualifies pure row-derived memoization. Recursive graph
tabling over captured durable views still needs its separate contract and
witness; corrected PeTTa's ordinary table declarations retain their existing
static-world semantics.

## Numerical evidence with declared origin units

`space_evidence_curriculum.metta` extends the existing claims collection with
separate synthetic count records. It leaves the original illustrative
`Assessment` values unchanged and never converts them into measured evidence.

```sh
mkdir -m 700 evidence-space
./cetta --lang petta --run-contract examples/kb/space_evidence_run.metta "$PWD/evidence-space"
```

```text
station batch: 8 positive,2 negative ─┬─ source1 ─ evidence1,evidence4
                                    └─ source2(copy) ─ evidence2
survey batch:  1 positive,4 negative ─── source3 ─ evidence3
                         |
                 one contribution per origin/claim
                         |
                 9 positive,6 negative
```

This lesson stipulates independent trial batches, a canonical origin/claim
unit and context-size parameter kappa=1. Distinct source names or disjoint
documents alone do not establish a statistical sampling model. Origin labels
are inputs assumed consistent with source lineage; the demo does not discover
hidden dependence or authenticate those labels. More complex partial overlap
requires a separate evidence policy.

The already existing `lib_wmpln_evidence` supplies componentwise revision and
the count-to-STV view. The numerical subset used here is qualified for finite
nonnegative Int64 input counts: strength is positive/total (zero at total0),
and confidence is total/(total+kappa). Confidence is this chosen evidence
weight view, not the probability that a news statement is true. The example
returns counts(9,6), strength0.6 and confidence15/16. Naively summing joined
copies would return(25,10), strength5/7 and confidence35/36.

Ordinary `KqSpace`, select, bind, optional, order, group, fold and map implement
the lesson over one captured view. A stored-ground-data projection runs before
schema binding so an open calibration atom cannot manufacture counts for an
origin. That projection uses bounded complete scans; it is not advertised as
an indexed numerical-evidence engine. Sorting is explicit, and the result
retains judgement, supporting-evidence, source and calibration occurrence IDs.
Equal copies contribute once but remain visible in the provenance. Conflicting,
missing or invalid calibrations produce `EvidenceRevisionRefusedV1` with their
provenance, instead of choosing the first value or silently using zero. With
no supporting evidence, counts and confidence are zero; the library's strength0
convention carries no positive evidential support for either verdict.

The same plans run through `space-curriculum:configuration` with this module
loaded, or embedded with `space-evidence:run`. `make test-space-evidence`
checks the existing arithmetic subset against an independent rational oracle,
copies/conflicts/missing counts/open data, inert code-shaped fields, original
identities, captured retraction/compaction, fresh-process and service restart,
canonical provenance order and cancellation cleanup. These assumptions and
checks do not establish real-world calibration or a runtime refinement proof.

## Moving a collection between processes or stores

`examples/native_transfer_server.metta` serves the same ordinary Space
queries and adds bounded logical import/export. The independent client is:

```sh
python3 tools/cetta_space_transfer.py service.sock export claims claims.csx
python3 tools/cetta_space_transfer.py service.sock import copied-claims copy-001 claims.csx
```

The archive carries original encoded atoms, duplicate occurrences and index
declarations. A copy creates new destination memberships with a durable
origin-to-destination map. References inside atoms remain original references;
applications can explicitly translate them with `durable-space:import-origin`.
Use the existing captured backup/restore for exact identity-preserving recovery.
Staging is invisible; facts, rebuilt indexes, origin mappings and retry receipt
publish together. Truncation, bad completion and capacity failure refuse.
`make test-space-transfer` exercises the embedded, service and file paths.

## Ordinary persistent Spaces

The current path is `space_curriculum.metta`: one ordinary persistent Space,
with the same MeTTa plans embedded and served. From the repository root:

```sh
mkdir -m 700 demo-space
./cetta --lang petta --run-contract examples/kb/space_run.metta "$PWD/demo-space"
```

Repeat the command to reopen committed data. Installation receipts prevent
duplicate insertion. This uses the native core runtime and existing durable
publication owner. It does not require Python, SWI-Prolog or SQLite at runtime.

```text
ordinary atoms → shared Space pattern cursor → MeTTa joins/folds/paths
                        |
                 captured durable root
                        |
              same embedded/server owner
```

The five lessons return:

| Lesson | Expected logical result |
| --- | --- |
| Claims/evidence/source join | Four supports, each retaining judgement, evidence and source occurrence references |
| Optional evidence | Four present rows and one explicit absence for judgement4/claim2 |
| Two-hop dependencies from emit | Five occurrence paths: dep3/dep2, dep4/dep1, dep4/dep5, dep6/dep1, dep6/dep5 |
| Supports grouped by source origin | `(RelGroup origin1 3)` and `(RelGroup origin2 1)` |
| Explicit distinct origins, then count | `2` |

Four supports are not four independent sources. A copied article and duplicate
extraction retain their lineage and supporting occurrences. Two distinct
origins alone do not prove statistical independence either. The example keeps
the illustrative assessments as data; it does not invent numerical evidence
weights or claim a calibrated truth value.

The payload's logical identifier and its `StoreOccurrenceV1` membership
identity are separate. Query projections carry occurrence references explicitly.
Retracting one evidence occurrence leaves its judgement/source and other
supporting occurrences intact. Stored equations remain inert data.

The same application is a local server using the existing CSP1/CAV1 worker:

```sh
CETTA_KB_DIRECTORY="$PWD/demo-space" CETTA_KB_SOCKET="$PWD/demo-space.sock" \
  path/to/cetta-value-worker --serve --lang petta \
  examples/kb/space_curriculum.metta space-curriculum:configuration
```

Use the value-worker executable produced by `make value-worker`.
The application combines ordinary query, mutation, graph, maintenance and
backup handlers over the same collection. It declares its reviewed pure row
functions in the server program; policy-looking KB facts grant nothing.
For its dependency atoms `(dependency logical-id source target)`, graph
projection uses `(KgRelation dependency 1 2)` and arity4: the logical edge
identifier occupies field0. Reachability from `emit` returns parse/check/lex.
The socket is local, private and same-user. This command starts an example,
not an automatic migration or deployment of another application's store.

`tests/support/check_space_curriculum.py VALUE_WORKER` checks both execution
modes, fresh-process reinstall/reopen, paging, graph reachability, a single
evidence retraction, compaction and restart. The generic
`tests/support/check_space_query.py VALUE_WORKER` additionally checks generated
joins and numeric count/sum/min/max/average against SQLite, explicit distinct,
canonical ordering ties, slicing, callback errors and state limits. SQLite is
an offline oracle, not the runtime backend. The
[query contract](../../docs/native_store/README.md#relational-composition-over-ordinary-spaces)
documents ownership, completion, callback authority and current limits.

Callbacks must remain pure. Ordinary-Space cursors now invalidate on application
code/declaration changes, and defer optional specialization publication while
the program is observed. Query-informed writes can retain a data/program read
context through completion and commit through the same durable Space writer.
The query contract documents `kbq:read-context`, `durable-query:commit-command`
and the service's `RelOpenRead`/`RelReadPage`/`RelReadCommit` operations. Contexts
provide dependencies; they grant neither write authority nor proof that an
embedded caller finished its query. Portable in-memory pattern examples remain
below.

## Scalar ranges and program dependencies

`space_range_curriculum.metta` adds a scalar-range chapter over the same native
owner and ordinary query interface:

```sh
./cetta --lang petta --run-contract examples/kb/space_range_run.metta "$PWD/demo-space"
```

It creates a separate example collection in that store, preserving the earlier
lesson installation and receipts. The three results are: programs with size
10..30 (`check 10`, `parse 20`, `compile 20.5`); two distinct memberships of
`emit 40`; and three range-to-dependency paths (`parse→lex`, twice `emit→parse`).
The float remains eligible through the other-scalar fallback. Row bags have
explicit occurrence references; enumeration order is unspecified. Reopening
the example preserves data and IDs through installation receipts.
These are synthetic program sizes. See the
[range contract](../../docs/native_store/README.md#stored-scalar-ranges)
for stored-data projection, scalar ordering, access paths and current limits.

## Earlier point-store reference examples

The older `curriculum.metta`/`graph_curriculum.metta` examples below retain the
point-store reference interface for comparison. New KB applications use the
ordinary-Space path above; these examples are not a second production backend.

The two datasets are synthetic. Run the native example from the repository root:

```sh
mkdir -m 700 demo-kb
./cetta --lang petta --run-contract examples/kb/run.metta "$PWD/demo-kb"
```

The directory is a persistent native store. Run the command again to reopen it;
the installation receipt prevents duplicate insertion. The only required
runtime is CeTTa's core build. The example neither contacts a service nor uses
Python, Prolog or SQLite.

```text
claim1 <- judgement1 <- evidence1 -> source1 -> origin1
                    <- evidence4 -----^          ^
       <- judgement2 <- evidence2 -> source2 -----|
       <- judgement3 <- evidence3 -> source3 -> origin2

emit --dep3--> check --dep2--> parse --dep1--> lex
  |                            |  ^
  +---------dep4---------------+  |
  +---------dep6------------------+
                 parse --dep5--> check
```

The first result contains four supporting occurrences, three judgement
identities and two declared source origins. `evidence1` and `evidence4` have
equal payloads and distinct identities; the provenance row refers specifically
to those occurrences. `source2` copies `source1`. `(OriginCounts 4 2)` makes
that dependence visible; two origins alone do not prove statistical independence.
The illustrative assessment numbers are retained, not combined or presented
as calibrated probabilities.

The optional query returns four `HasEvidence` rows and
`(NoEvidence judgement4 claim2)`. Missing evidence has an explicit tag.

The dependency query returns five two-edge paths from `emit`. `dep4` and
`dep6` remain distinct, so paths through each appear separately. The cycle
between `parse` and `check` is harmless for this explicitly two-hop query.
The graph chapter adds finite reachability and shortest paths on this same
captured store:

```sh
./cetta --lang petta --run-contract examples/kb/graph_run.metta "$PWD/demo-kb"
```

It reports three outgoing edge occurrences from `emit`, three incoming edge
occurrences at `parse`, the distinct reachable programs
`{check, parse, lex, emit}` (the cycle reaches its start again),
and both shortest paths to `lex`: `(dep4 dep1)` and `(dep6 dep1)`. The isolated
program has no reachable endpoint. `dep7` closes two directed triangles
anchored at `emit`, one for each distinct `emit→parse` occurrence. Repeating the command preserves these
results. These older commands do not exercise the ordinary-Space constraint
or multi-client service adapters described above.

`curriculum.metta` exports `curriculum:run path`. The CLI and native value worker
invoke this same entry. It installs all records atomically, captures a new
view, and consumes small pages with explicit work limits. The example collects
its small final answer deliberately; library query cursors stream their inputs.

`portable_patterns.metta` uses ordinary in-memory PeTTa operations on the same
datasets. Its four query answers are in `portable_patterns.expected`; module
import acknowledgements are separate. This portable portion can run on PeTTa
as well as CeTTa. The durable `store:` interface is native to CeTTa.

## Earlier graph reference interface

Import `(library kb_graph)`. A graph specification is
`(KgRelation relation source-field target-field)`, using zero-based positions.
Its edges are the relation's records having both specified fields present and
ground. Other fields may hold open syntax. This is an explicit projection:
it does not unify open endpoints with a requested vertex. Ordinary KB pattern
queries continue to include and match those open records.

The inert plans are:

| Plan | Result |
| --- | --- |
| `(KgNeighbours spec start out-or-in)` | `(KgEdge fact-id source target)`, preserving occurrences |
| `(KgPaths spec start minimum maximum out-or-in)` | Bounded walks, including repeated vertices/edges |
| `(KgReach spec start out-or-in)` | Distinct endpoints of positive-length paths; includes `start` only when a cycle reaches it |
| `(KgShortest spec start goal one-or-all out-or-in)` | One shortest path, or all shortest occurrence paths |
| `(KgTriangles spec start out-or-in)` | Anchored directed three-edge cycles with three distinct vertices |

Paths are `(KgPath (vertex...) (edge-id...) length)`. A zero-length path has
the supplied start identity and no edges; shortest `start = goal` returns it.
The graph specification does not require a separate vertex relation. `one`
chooses the first shortest answer in the captured index traversal order;
it is not a promise of lexicographically least vertex names. Triangles retain
parallel-edge occurrences and do not identify different anchor rotations.

`(kbg:open view plan maximum-row-bytes maximum-retained-states)` opens a cursor.
The view must be live even for a zero-length path. Plan identities must be
portable CAV1 values; unsupported resources return a codec refusal, never a
completed empty traversal. Comparisons and explanations propagate refusals.
`(kbg:page cursor row-count step-budget)` returns `KgPage` with `KgMore`,
`KgPaused`, `KgComplete` or `KgFailure`. Both paused and nonterminal pages carry
the next cursor. Consume each continuation once. Closing releases its active
scan; the captured view is borrowed and remains the caller's responsibility.
These are in-process continuations, not serializable remote capabilities.
Public cursor operations reject non-ground control records and invalid shapes
before destructuring them. This validation does not authenticate arbitrary
caller-constructed states; use continuations returned by the library.

`kbg:observe` records a conservative relation-range dependency, covering
negative paths and edges not visited by an early shortest answer. Combine
additional application reads before a query-informed write. `kbg:explain`
shows the argument index, graph projection and traversal strategy.

`KgWork` reports driver steps, candidate keys, decoded source bytes and
frontier admissions. These are not total CPU/allocation costs. A page advances
at most one indexed edge per step, although continuation validation, a
visited-list lookup or frontier rotation may inspect multiple retained entries. The initial visited-set
implementation is a linear MeTTa reference. The state allowance bounds queued
paths plus remembered identities, not bytes or elapsed time; capacity failure
is distinct from completed emptiness. General scaling and service lifetime
qualification remain separate obligations.

The scaling driver is `tests/support/bench_kb_graph.py`. Its version-2 receipt
separates case-level loading, storage and repeated updates from query samples.
`VmHWM` remains process-cumulative, including loading and prior operations;
it is not a query-specific peak. Empirical p95 is a statistic of the recorded
sample and equals its maximum when fewer than 20 samples are used. The raw
samples and their count are retained.
