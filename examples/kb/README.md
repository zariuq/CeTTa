# A metagraph database in MeTTa

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
results. Constraints and the multi-client server remain under development.

`curriculum.metta` exports `curriculum:run path`. The CLI and native value worker
invoke this same entry. It installs all records atomically, captures a new
view, and consumes small pages with explicit work limits. The example collects
its small final answer deliberately; library query cursors stream their inputs.

`portable_patterns.metta` uses ordinary in-memory PeTTa operations on the same
datasets. Its four query answers are in `portable_patterns.expected`; module
import acknowledgements are separate. This portable portion can run on PeTTa
as well as CeTTa. The durable `store:` interface is native to CeTTa.

## Graph interface

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
