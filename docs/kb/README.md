# Native metagraph database library

The first library layer is `lib/petta/kb.metta`, over the existing native byte
store. Its generic point-fact/receipt foundation is `knowledge.metta`, extracted
from the qualified application-neutral knowledge layer. The current layer
supplies identified relations, covering indexes and incremental captured-view
cursors. `kb_query.metta` adds streamed pattern queries and dependent joins.
Graph algorithms, constraints and the service adapter are under development;
this checkpoint does not claim a complete database release.

Import `(library kb)` in PeTTa. Open a store with `store:open`, capture a view
with `store:snapshot`, and close owned resources explicitly. Store directory,
recovery and ownership requirements are documented in [native storage](../native_store/README.md).

`knowledge:` names the older generic point-record API and its
`knowledge/fact/v1/` storage domain. `kb:` names the identified relational facts
in `kb/fact/v1/`. Their read observers are distinct: use `kb:observe-facts` with
`kb:fact`, and `knowledge:observe-bound` with `knowledge:read`. Both layers reuse
the durable receipt protocol. Existing application-local compatibility modules
have not been migrated by introducing these libraries.

## Facts, selectors and publication

- A row is `(KbFact identity relation fields)`. Identity and relation are ground
  CAV values; `fields` is an expression tuple and may contain inert syntax.
- Distinct identities keep equal payloads as separate occurrences. Exact
  selectors use CAV value equality, preserving String/Symbol, Boolean/integer
  and floating representations. They are access paths, not general unification.
- Edits are `(KbPut identity relation fields)` and `(KbDelete identity)`. All
  edits are validated; the last edit of one identity wins within a batch.
- `kb:commit owner view observation receipt command answer edits byte-budget`
  publishes facts, relation/argument indexes and the immutable receipt together.
  The observation must be bound to that actual captured view. Replaying the same
  command/receipt returns its old answer after checking the publisher/view
  domain; a closed, recovering or foreign publisher is refused. A receipt
  collision is refused too. `knowledge:receipt` is the separate lookup-only
  operation when publisher validation is not requested.
- `kb:observe-selector view selector` records the complete selected index prefix,
  including absence. Covering index values contain the full row, so a projected
  non-key field change invalidates the observation too. This initial choice
  spends storage to make dependency coverage direct and inspectable.
- `kb:observe-facts view identities byte-budget` reads and observes exact fact
  identities, including absent ones. `kb:observe-empty view` starts an empty
  observation; `kb:observe-merge left right` combines observations only when
  both belong to the same captured view.

`kb:equal` compares ground CAV values and refuses open operands.
`kb:alpha-equal` compares separate terms modulo variable renaming, preserving
sharing within each term. Neither is a variable-identity test: use MeTTa's
ordinary equality when the identity of live variables is the intended policy.

Selectors are `KbAll`, `(KbRelation relation)` and
`(KbArgument relation zero-based-position exact-ground-value)`. Nonground fields
remain available in the relation stream; they have no exact argument posting.
`kb:explain selector` names the chosen access path and prefix. A raw store client
must not bypass index maintenance in a KB namespace.

## Streaming and lifetime

`kb:query-open view selector byte-budget` returns a `KbCursor` containing owned
scan state and a borrowed captured view. `kb:query-next cursor` returns
`(KbYield fact next-cursor)`, `(KbEnd work)` or an explicit refusal. A cursor is
an in-process resource and cannot be serialized as a client capability.

`kb:query-page cursor positive-count` returns `(KbPage rows (KbMore cursor))` or
`(KbPage rows (KbComplete work))`. A full page may still report `KbMore` until
the next probe establishes exhaustion. It never silently drops the tail.
`kb:query-close cursor` closes its scan; the view has its own lifetime.
`kb:select view selector byte-budget` is the explicit whole-result convenience
operation and closes its cursor on success or failure.

`(KbWork returned-rows examined-keys decoded-bytes)` distinguishes emitted rows
from inspected keys; the first key outside a prefix counts as examined. Source
access seeks to a prefix and stops at its end, without materializing the archive.

The relational library adds `rel:left-join predicate projection missing left right`.
The projection receives the caller-selected absence value for an unmatched
left occurrence. Multiplicity and source order are preserved, and an empty
tuple returned by a projection remains a row. The existing pure, deterministic,
single-result callback contract applies.

## Composable pattern queries

Import `(library kb_query)`. These plans are ordinary inert MeTTa values:

| Plan | Meaning |
| --- | --- |
| `(KqScan selector pattern template)` | Unify each selected fact with the pattern; instantiate the inert template |
| `(KqRows rows)` | An explicitly materialized bag of values |
| `(KqUnion plans)` | Concatenate the answer bags, retaining duplicates |
| `(KqBind left pattern right)` | Match each left row and execute the instantiated right plan |
| `(KqOptional left pattern right missing)` | As bind; emit the instantiated missing value if the right has no answer |
| `(KqSemi left pattern right)` | Keep each original left occurrence with at least one right answer |
| `(KqAnti left pattern right)` | Keep each original left occurrence with no right answer |

Bind/optional/semi/anti first filter left rows through their pattern. Semi and
anti test existence and keep the original left value; they do not enumerate
right-side bindings. The right plan can choose an index using outer bindings.
Pattern and template variables share one scope, freshly instantiated per
candidate. Stored variables are unified too. Reading or projecting a rule or
call expression does not execute it.

The scan selector defines an **exact domain**, followed by general unification;
it is not an automatic optimizer hint. Use the relation stream or `KbAll` when
nonground stored fields may unify with a requested value. Choosing an exact
argument domain intentionally excludes those nonground values. Explanations
report this distinction and the dependent nested-loop strategy. No whole-KB
materialization precedes a scan's first row.

```metta
(KqBind
  (KqScan (KbRelation edge)
    (KbFact $first edge ($a $b)) ($first $a $b))
  ($first $a $b)
  (KqScan (KbArgument edge 0 $b)
    (KbFact $second edge ($b $c)) ($first $second $a $c)))
```

`kbq:open view plan byte-budget` returns an owned `KqCursor` borrowing the view.
`kbq:page cursor positive-row-count nonnegative-work-budget` returns:

- `(KqPage rows (KqMore cursor))`: the row limit was reached;
- `(KqPage rows (KqPaused cursor))`: the work budget was reached, possibly with
  no answers yet;
- `(KqPage rows (KqComplete work))`: the query completed and released its scans;
- `(KqPage rows (KqFailure reason work))`: explicit failure, with the already
  produced prefix, and scans released.

Every step advances at most one leaf stream. `(KqWork steps examined-keys
decoded-source-bytes)` measures driver steps and storage input, not all CPU,
copying or allocation costs. A full page needs another probe to
establish completion. Consume a continuation once; to cancel it, call
`kbq:close cursor`. Terminal cursors have already closed. A rejected page
argument does not consume the cursor. The borrowed view is closed separately.

`kbq:observe cursor` returns a view-bound observation of the complete fact
namespace. This conservative policy includes absent matches and future rows
affecting dependent joins; it can produce avoidable conflicts. These plans
perform no arbitrary callbacks. Additional computations over external state
need their own influencing reads. `kbq:explain plan` reports access paths,
explicit materialization and dependent plans without opening resources.

Run `make test-kb-db` for value-boundary, fact/index, transaction, paging,
query, cancellation and join checks. The Python harness and SQLite oracle are
test tools; the database operations execute in the native MeTTa worker without
SQLite. The generated SQLite comparison covers inner, optional, semi, anti and
union-all answer multiplicities; it does not prescribe SQL null/coercion rules.
