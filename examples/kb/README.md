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
Unbounded traversal, shortest paths, constraints and the multi-client server
are subsequent curriculum steps under development.

`curriculum.metta` exports `curriculum:run path`. The CLI and native value worker
invoke this same entry. It installs all records atomically, captures a new
view, and consumes small pages with explicit work limits. The example collects
its small final answer deliberately; library query cursors stream their inputs.

`portable_patterns.metta` uses ordinary in-memory PeTTa operations on the same
datasets. Its four query answers are in `portable_patterns.expected`; module
import acknowledgements are separate. This portable portion can run on PeTTa
as well as CeTTa. The durable `store:` interface is native to CeTTa.
