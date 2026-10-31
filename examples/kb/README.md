# A metagraph database in MeTTa

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

Callbacks must remain pure and fixed during the query lifetime. Automatic
program pinning/invalidation and program-aware write decisions remain open;
ordinary captured data and whole-revision write validation are already shared
with the durable Space API. Portable in-memory pattern examples remain below.

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
occurrences at `parse`, the distinct reachable programs `{check, parse, lex}`,
and both shortest paths to `lex`: `(dep4 dep1)` and `(dep6 dep1)`. The isolated
program has no reachable endpoint. Repeating the command preserves these
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
