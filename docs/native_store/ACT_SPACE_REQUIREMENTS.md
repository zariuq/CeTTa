# Native ACT reader requirements for persistent MeTTa spaces

Status: integration requirements and acceptance criteria, 2026-10-30.
Inspected CeTTa baseline: `0bb0b45a`. This document does not claim that the
native C reader or the public owning durable Space adapter is implemented.

The deliverable is a reusable native C ACT reader which can back ordinary
MeTTa Space operations over persistent, captured data. Graph and relational
queries continue to use the existing matcher, binding rules and query cursors.
The reader supplies storage access; the surrounding Space adapter and durable
publication owner supply the remaining database guarantees.

ACT can supply the native durable backing of a Space, including immutable
base segments and checkpoints. It is not limited to interchange. A practical
layout combines a captured ACT base with a mutable delta and the existing
journal/publication owner. Base, delta, dictionaries, occurrence metadata and
receipts must describe one atomically published view. The current native
disk-root API is format-specific; attaching ACT needs an adapter and effective
view validation, rather than passing ACT bytes to the existing index decoder.

## 1. Integration boundary

```text
MeTTa match / pat:query / shared query continuations
                         |
                  captured Space view
                         |
       occurrence source: select / next / identity
                         |
         MeTTa record codec and index projection
                         |
             native C ACT byte-trie reader
                         |
       pinned file generation + captured ACT root

Updates -> existing store owner -> validate reads / publish one root
                                  facts + indexes + metadata + receipts
```

Use [`SpaceSourceCursorOps`](../../src/space_match_backend.h),
[`SpaceOccurrenceCursor` and `SpaceQueryCursor`](../../src/space.h), and
the existing [native byte-store owner](../../src/native_disk_store.h).
[`test_space_source_cursor.c`](../../tests/test_space_source_cursor.c) is
an executable integration example, with a test-only disk source. The owning
public constructor, complete index selection and durable dependency adapter
remain separate implementation obligations.

Responsibilities are split as follows:

| Layer | Required responsibility |
| --- | --- |
| ACT reader | Validate and traverse the specified byte-trie format; expose keys and values incrementally; retain an exact captured backing view. |
| Atom/occurrence codec | Encode supported MeTTa values, variable scope and persistent occurrence references without loss. |
| Space adapter | Supply complete candidates to the existing matcher; preserve occurrence multiplicity, bindings and query completion; expose owned read-only views. |
| Durable owner | Publish coherent generations, validate transactional dependencies, recover committed roots, retain receipts and reclaim obsolete storage safely. |
| MeTTa libraries | Define graph relations, constraints, provenance and query algorithms over those spaces. |

The codec and adapter can be implemented alongside the reader. Acceptance
must identify which layer establishes each guarantee. The reader need not
implement graph algorithms, a second matcher or a second transaction manager.

## 2. Pin the actual ACT format

The inspected upstream implementation is PathMap
[`cb4e1e4eecb75f0be9846b4b75c69d5e2d2f647b`,
`src/arena_compact.rs`](https://github.com/Adam-Vandervorst/PathMap/blob/cb4e1e4eecb75f0be9846b4b75c69d5e2d2f647b/src/arena_compact.rs).
Its executable magic is **`ACTree03`**. Introductory comments still describe
`ACTree01` and an older varint; follow the executable version-3 writer and
reader. The recorded changes include relative offsets in version 2 and a new
varint in version 3. Preserve the upstream MIT license and attribution when
porting implementation code.

For this first target:

- Preserve arbitrary byte keys, including empty keys, embedded zero bytes,
  keys which prefix other keys, and nodes holding both a value and children.
- Preserve the reader's unsigned 64-bit node values. Their application meaning
  belongs to the chosen encoding; a value is not implicitly a fact count,
  pointer, identifier or executable term.
- Implement exact lookup, incremental whole-tree iteration, prefix selection
  and lower-inclusive/upper-exclusive byte-range iteration. Expose a correct
  scan fallback when an access path is unavailable.
- State supported format versions explicitly. Reject unsupported versions;
  never guess a decoder from the filename or quietly recreate a damaged file.
- Test relative offsets, branch/line nodes, child masks, varint boundaries,
  byte order and the captured root against the pinned Rust implementation.
  Logical key/value equivalence is the compatibility criterion; different
  valid trie layouts need not have identical file bytes.

MORK's inspected
[`ed57c6716d8c510296fb5fbb8be6fbfe2df241d7` tree backup](https://github.com/trueagi-io/MORK/blob/ed57c6716d8c510296fb5fbb8be6fbfe2df241d7/kernel/src/space.rs)
exports its structural paths with unit values. Its optional symbol interning
uses separately serialized mappings. Thus an ACT file alone does not specify
all MeTTa atom semantics or supply a bag of identified fact occurrences.

Document two compatibility claims separately: reading upstream ACT bytes, and
restoring a CeTTa persistent Space from a specified atom/occurrence encoding.
Version the latter independently, while retaining ACT byte-format compatibility.
Any required dictionaries or metadata must be named, versioned and bound to
the same published generation. Missing or mismatched dependencies are errors.

## 3. Value fidelity and inert loading

For every supported value kind, round-trip the original value and its
distinctions, rather than just its printed representation. Required examples
include symbols versus strings, numbers versus numeric-looking strings,
Booleans versus similarly named symbols, empty expressions, nested expressions,
Unicode and strings containing zero bytes. Specify supported numeric kinds
and their exactness; refuse unsupported values rather than coerce them.

[`atom_wire.h`](../../src/atom_wire.h) and
[`atom_wire.c`](../../src/atom_wire.c) provide an existing bounded portable
syntax codec to assess for reuse. CAV1 variable indices are packet-scoped;
decoding creates fresh variable identities. It is not automatically the MORK
atom encoding or the persistent occurrence-identity format.

Variable sharing must survive within its declared scope:

```text
(rel $x $x)   differs from   (rel $x $y)
```

Persist the structural sharing needed by ordinary matching. Use the normal
matcher to freshen stored variables when appropriate; do not restore raw
process `VarId` values or accidentally alias unrelated decoded records.
Document any supported sharing across records explicitly.

Opening, scanning and matching a data snapshot must leave stored programs
inert. For example, retrieving `(= (danger) (effect))` returns stored syntax;
it does not install the equation into the caller's environment, call `effect`,
or reinterpret text as an import. Invocation or executable admission remains
an explicit operation under the existing language and authority rules.

Opaque live resources require an explicit unsupported result unless a separate
qualified serialization contract exists. File descriptors, native handles,
arena addresses, evaluator continuations and live authority cannot be restored
from their printed integers or names. Stable reference data remains data.

## 4. Occurrence identity and multiplicity

The KB needs to distinguish these records:

```text
fact f1: (edge alice bob)
fact f2: (edge alice bob)
fact p1: (source-of (fact-ref f1) article-a)
```

`f1` and `f2` are different occurrences with equal payloads. An expression
`(edge alice bob)` is also different from a reference to the occurrence `f1`.
Ordinary bag queries must preserve both occurrences until explicit distinct.
Removing `f1` must leave `f2`; a reference to `f1` must not silently retarget
to `f2`.

The encoding/adapter must therefore provide:

- Stable, ground occurrence identities qualified by a persistent Space/store
  domain, surviving close, restart, index traversal and compaction.
- A representation for duplicate payloads, such as distinct occurrence keys
  with encoded bodies or an explicitly versioned occurrence table. A count
  alone cannot distinguish separately referenced copies.
- The same identity from identity lookup, relation/argument lookup, incoming
  and outgoing adjacency, and full scans. Traversal counters, file offsets,
  pointers and a file's changing content hash are insufficient identities.
- Explicit restore-versus-copy behavior: a restore retains the intended domain
  and identities; importing into another domain allocates or maps identities
  under a declared policy. Retries must not silently duplicate an import.

The existing [MORK ACT fixture](../../tests/test_mork_open_act_syntax.metta)
inserts a duplicate edge but expects two structural entries for three inserts.
Preserve that existing structural-set contract. A bag-capable persistent Space
requires the additional occurrence encoding; the set fixture alone does not
qualify it. Likewise, do not revive the legacy `.copies` convention, which the
[bridge round-trip fixture](../../tests/test_mork_act_roundtrip.metta) rejects.

## 5. Captured views, cursors and ownership

Opening a view captures a specific file generation and root. All legs of a
join, recursive traversal and paged query use that capture, even if a writer
publishes another root between pages or the pathname is replaced.

- Retain the file/mapping and captured root for the full view lifetime.
  A borrowed filename or pointer into a caller's temporary arena is inadequate.
- Give each cursor independent traversal state. Clone/resume observes the same
  capture and logical position; selecting a new pattern on that cursor does
  not reopen the publisher's latest root.
- State row borrowing rules precisely. Existing source callbacks lend rows;
  the shared matcher promotes retained values into its receiving arena before
  advancing or releasing the source. Temporary decoding must not permanently
  intern every rejected candidate or accumulate every visited row.
- Own pattern bounds or encode them during selection. The supplied pattern is
  call-scoped and can move or disappear with its caller's arena.
- Close/cancel/unwind releases pins, mappings, traversal buffers and temporary
  decoded terms. Closing one cursor must not invalidate another live cursor,
  captured view or already owned answer.
- Preserve existing handle checks. Read permission does not imply mutation or
  execution permission; a serialized identifier does not manufacture authority.
  Changing one handle's permissions does not change what other holders need
  from the shared storage representation.

Initial durable views can be explicitly read-only. Mutation through such a
view must refuse before changing storage. Writers and overlays, when supplied,
must use their declared owners and existing Space contracts.

## 6. Incremental queries and complete matching

Opening a cursor and fetching the first page must not materialize or decode
the whole KB into a shadow in-memory Space. A complete validation pass over
encoded bytes can be an explicit opening cost; distinguish it from atom
materialization and report both costs. Mapping a file does not by itself
establish either bounded decoding or bounded resident memory.

An index supplies candidates to the existing matcher. It may over-select;
it must not omit a possible match. In particular, exact encoded-key or
relation-prefix selection is insufficient when stored open terms or variable
heads can unify with the query. Include those candidates or use a complete
scan fallback. Explain the chosen access path and fallback to callers.

Preserve ordinary bindings, repeated-variable constraints, freshening and
answer multiplicity through `match`, directional `pat:query` and retained
query continuations. Byte-key order is not automatically numeric, logical or
application order. Ordered pagination needs an explicit comparison policy and
stable tie-breaker, normally including occurrence identity.

Completion must distinguish item, exhausted, invalidated, cancelled,
capacity/resource exhaustion and failed/incomplete read. Map these onto the
existing source/cursor outcomes without turning failures into normal END or
successful empty answers. A query which yielded rows and then encounters
corruption is incomplete; those rows do not establish a complete observation
for a subsequent write.

## 7. Publication, restart and dependencies

Treat ACT as an immutable snapshot/checkpoint representation or a published
segment under the existing owner. Its root field and optional root history
are not, by themselves, a transaction receipt, crash-recovery protocol or
conflict-validation certificate.

The integration must publish a consistent set of fact records, occurrence
metadata, symbol mappings, indexes and durable application receipts through
one existing commit domain. Stage and validate a KB import before publishing
it; failure or cancellation must not expose a partially imported generation.

Retain the native owner's publication, synchronization and recovery rules.
Keep corruption checks and metadata binding at the appropriate owner layer;
the ACT signature alone is not an integrity check. Recovery restores committed
data and receipts without executing stored programs or replaying external
actions. An uncertain write outcome must remain distinguishable from failure
and be resolved through recovery/receipt lookup before an effect is retried.

Snapshots remain valid while older readers hold them. Compaction or file
reclamation must preserve those readers and occurrence identities. Bytes
reachable from a captured root must remain readable and unchanged. An append
or root-header update must not make an old cursor recapture the new root or
invalidate its mapping; replacing/truncating backing storage needs the owner's
reader-retention protocol.

Query-informed writes must register all influencing reads with the existing
transaction machinery: successful lookups, absence, ranges, counts, optional
and anti joins, and graph/path dependencies. A captured file descriptor alone
does not provide that validation. Start with conservative domain/relation-range
dependencies where narrower sound scopes are not established. Cached derived
results are qualified by the captured revision and invalidated accordingly.

## 8. Defensive decoding and observable failures

Validate every externally derived offset, length, node header, child count,
varint and arithmetic operation before accessing memory. Reject truncated
objects, overflow, out-of-range relative references, malformed child layouts
and cyclic/invalid traversal structures. Account for shared nodes if the
supported format permits them; rejection must follow the pinned format's
actual structural rules.

Rust's fast paths assume adequate backing bytes. A C port must not reproduce
unchecked wide reads at a truncated mapping boundary. Use checked reads or
explicitly proven accessible padding. Keep traversal stack use bounded rather
than proportional to adversarial key depth. Bound allocation, decoded depth,
work and cancellation latency through explicit caller/runtime limits.

Report unsupported version, unsupported atom encoding, missing dictionary,
corruption/truncation, I/O failure, allocation/capacity failure and cancellation
as explicit outcomes. Do not replace a damaged committed KB with an empty
one. Opening a corrupt file or failing halfway through iteration must leave
the previously published Space and its data intact.

## 9. Acceptance tests and handoff

The following are separating tests, not an implementation-completion claim:

| Gate | Required evidence |
| --- | --- |
| Byte compatibility | Native C reads pinned Rust fixtures: empty trie, empty/NUL keys, prefix keys, branch/line boundaries, sparse/dense children and full-width values. Compare exhaustive key/value results and prefix/range pages. |
| Writer compatibility | If a C writer is supplied, pinned Rust reads its output and both directions round-trip logical contents. Reader completion need not wait for a C writer. |
| Atom fidelity | Symbols/strings, exact supported numbers, nesting, zero bytes, repeated/distinct variables; explicit refusal of unsupported live values. |
| Occurrences | Two equal edge payloads retain different IDs across restart and compaction; removing one preserves the other and references do not retarget. |
| Shared Space semantics | Compare bindings, bag multiplicity and completion against an ordinary Space using the same matcher, including open stored terms, shared variables and quoted rules. |
| Captured execution | Publish between join legs and pages; old view and cursor clone retain their original results and identities. New view sees the new root. |
| Failure isolation | Truncate at boundaries, mutate offsets/varints, inject I/O/allocation failures, cancel import/query, and fail cursor clone. No crash, partial publication, false empty result or leaked ownership. |
| Transaction dependencies | Insert a matching fact after an absent/aggregate/path observation; dependent write conflicts. Updates and index/receipt changes recover coherently. |
| Resource lifetime | Repeated open/query/cancel/close over a fixed-size KB releases per-request resources; count candidate visits, decoded bytes, allocations, live mappings and storage handles. |
| Scaling | Size-doubling sweeps separate validation/open, cold/warm query, first-page decode, result size, resident memory and storage bytes. Full scans are visible in explanation and metrics. |

For the reader handoff, supply the pinned source/format/encoding manifest,
reader API and borrowing rules, Rust-generated fixtures, malformed-input
tests and a small ordinary-Space adapter example. Label reader-only results
separately from the full occurrence, transaction and service gates. Reuse the
existing shared-source fixture for lifecycle and query comparisons.

The resulting architecture lets ACT improve persistent Space storage while
MeTTa continues to define matching, computation, graph structure and updates.
