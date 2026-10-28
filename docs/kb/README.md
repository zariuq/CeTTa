# Native metagraph database library

The first library layer is `lib/petta/kb.metta`, over the existing native byte
store. Its generic point-fact/receipt foundation is `knowledge.metta`, extracted
from the qualified application-neutral knowledge layer. The current layer
supplies identified relations, covering indexes and incremental captured-view
cursors. General patterns, graph algorithms and the service adapter are under
development; this checkpoint does not claim a complete database release.

Import `(library kb)` in PeTTa. Open a store with `store:open`, capture a view
with `store:snapshot`, and close owned resources explicitly. Store directory,
recovery and ownership requirements are documented in [native storage](../native_store/README.md).

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
  command/receipt returns its old answer; a receipt collision is refused.
- `kb:observe-selector view selector` records the complete selected index prefix,
  including absence. Covering index values contain the full row, so a projected
  non-key field change invalidates the observation too. This initial choice
  spends storage to make dependency coverage direct and inspectable.

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

Run `make test-kb-db` for value-boundary, fact/index, transaction, paging and
optional-join checks. The Python harness and SQLite oracle are test tools;
the database operations execute in the native MeTTa worker without SQLite.
