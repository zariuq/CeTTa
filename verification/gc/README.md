# Symbol lifetimes and collection contracts

CeTTa already collects arena objects. Its global symbol table separately keeps
interned spellings until process shutdown. This package supplies lifetime
instrumentation, a non-destructive marker substrate, and checked foundations
for symbol collection. The full evaluator now has an experimental managed
spelling representation that releases transient symbol bytes with their owning
arenas. Dynamic registry names now use a counted collectible namespace.
**The legacy numeric symbol table remains permanent.** Callable-name churn and
persistent term archives still need their own owner migrations; the registry
collector does not sweep legacy raw IDs.

## Actual-engine managed spellings

`ENABLE_MANAGED_SYMBOLS=1` enables the prototype; the default remains zero.
Runtime-created symbols use an existing numeric identity when one is present.
Otherwise they own immutable bytes, length and cached spelling hash in their
arena. The 64-bit `Atom` remains 56 bytes. Both forms retain metatype `Symbol`.
The compiler migration from `sym_id` to `symbol_number` forces Atom readers to
use explicit observation, lookup, copy or registration operations.

Retention and registration are different:

- `atom_symbol_copy` preserves bytes in the destination owner without inserting
  a missing name. Evacuation forwarding is followed before accessing spelling.
- `atom_symbol_current_id` performs non-inserting lookup. Old spelled values
  can observe a later callable declaration; no permanent negative cache is used.
- `atom_symbol_register_id` is reserved for code/callable and audited numeric
  keys: callable heads, declaration subjects, native grammar labels and token classes.
  Dynamic registry and state names instead use `registry_bind_symbol` and
  `registry_state_set_symbol`, retaining spelling without permanent numbering. Stored state values and ordinary facts remain data.
- Registering a dynamic equation does not intern every symbol in its body.
  Body payloads and type expressions keep their arena ownership; declaring
  their enclosing function is not a permanent registration of those values.
- Equality and structural hashing observe spelling. Numeric discrimination
  indexes fall back to candidates verified by the content matcher when an
  identity is missing. They preserve ordered duplicate answers.
- Spelled symbols and expressions containing them are excluded from permanent
  global hash-consing. Term-universe storage copies such values into its existing
  pointer-backed persistent representation.

`atom_symbol_bytes` returns a borrow, not permanent storage. Spelled bytes live
only as long as their arena; copying the value into a surviving owner is needed
across arena reset or evacuation. Registering its spelling does not extend an
already-borrowed pointer's lifetime. Embedded NULs require the recorded length.

Run the actual owner, language, mutation and ordinary PeTTa gates:

```sh
make -j1 ENABLE_PYTHON=1 ENABLE_RUNTIME_STATS=1 ENABLE_MANAGED_SYMBOLS=1 \
  test-managed-symbol-values test-runtime-symbol-births \
  test-managed-symbol-owners test-managed-symbol-mutants test-petta-semantics
```

The tests cover binary values, source-owner release, forwarding, concurrent
registration, prepared duplicate answers, persistent encoded copies, dynamic
state keys and late declarations. Runtime-built grammar labels have distinct
numeric identities. A fact index built before numbering still finds both
duplicate occurrences after registration. Three actual `atom.c` mutants must
fail semantic assertions; compiler and sanitizer-startup failures do not count.

The same owner gates pass AddressSanitizer and UndefinedBehaviorSanitizer using
`ENABLE_SANITIZERS=1`. The matched engine runner and measured costs are described
in [`experiments/README.md`](experiments/README.md). This is a qualified
experimental direction for transient values, not a complete production GC.

The qualification decision is to accept the managed representation as an
experimental option, leaving the default unchanged. Matched engine outputs and
the unchanged offline Omega loop preserve their observations; transient
spelling payload no longer grows linearly. The fixed instruction panel reports
its costs rather than claiming a universal improvement. Retained archives and callable keys still need owner migration. The counted
registry implementation below is integrated; the generation-tagged handle and
archive-region experiments remain separate from those legacy readers.

## Runtime instrumentation

### Compiled PeTTa integration checkpoint

The current integration experiment uses upstream
`f05a53f52d54e5af3ca3719b70b5938473980e42`, including typed calls, exact
guard facts and continuation transfer. Prepared cursor detachment now uses
the upstream continuation-copy protocol; tests release the original owner
before consuming the remaining answers. Managed-symbol counters follow the
upstream counter range, and symbol lifetime flags remain distinct from
speculative and built-in registration flags.

The combined ownership repair preserves the provider requests and ordered
active/mixed actions of the unchanged pinned Omega loop. In matched idle runs,
the earlier managed engine used 52.69, 68.36, 115.21 and 276.92 MiB maximum RSS
at 100, 400, 1,600 and 6,400 passes. The combined engine used 51.16, 57.43,
59.86 and 59.63 MiB. This is evidence for the repaired owner chain on this
workload, not a general long-running-service space bound.

The existing `symbol_intern_*` functions retain their permanent ownership
contract. `symbol_intern_data_*` records runtime-data births without changing
spellings, ID equality or hashes. An owned intern of an existing data spelling,
or `symbol_pin`, promotes that same ID permanently. Data interning never
downgrades ownership. IDs increase monotonically; exhaustion aborts before
wrapping to the reserved zero ID.

Data births currently cover:

| Path | Birth boundary |
|---|---|
| Python bridge | PeTTa-mode Python string results; HE strings remain grounded strings. |
| Prolog bridge | Ordinary atom results; compound-name and imported code paths remain owned. |
| Runtime reader | `sread`, ordinary `parse`, and PeTTa `repra`; source-file parsing remains owned. |
| Durable values | Decoded symbol fields. |
| String natives | `str:to-symbol`. |

The JSON reader stores variable text as grounded strings and uses fixed symbol
tags; it needs no data-birth change. Compiled source and atom-blob loading keep
their existing owned interning.

Legacy registration hooks pin explicit numeric registry keys, imports,
translator-rule heads and foreign callable code. The instrumentation-only
build also pins equation/type syntax. The managed build instead registers
the callable head or declaration subject, preserving body/type data in its
arena. Dynamic registry names use the counted table below; retaining one no
longer pins a legacy ID. Registry values remain data and must be traced.
A legacy pin persists after a declaration is removed in this first phase.
`atom_is_symbol` and cached C literals also use owned interning for the fixed C
vocabulary; data-spelling observers can use `atom_symbol_equals_cstr`.

`symbol_table_stats_snapshot` reports owned/data counts and payload bytes,
retired count, reserved metadata, allocation totals, promotions and peaks.
Payload accounting includes terminating NULs. The same ten gauges appear in
`runtime-stats!`; resetting event counters does not reset live storage gauges.
Managed retained/allocated byte gauges and thread-local hash-cons entry/index
gauges are additional. Retained managed bytes count allocated arena payload,
not a precise census of semantically live values. Index bytes exclude Atom
payloads and allocator slack. Retired count stays zero in the evaluator.

`symbol_shadow_begin/mark/finish/destroy` snapshots IDs and counts marked and
unmarked data payloads. It changes no spelling, identity or lifetime class.
Births after the snapshot invalidate it for a future sweep. The marker is
single-threaded and must finish before its table is destroyed. It is **not
automatically invoked by either execution machine**, and an unmarked entry is
not evidence that it is unreachable.

`atom_visit_syntax_symbols` visits expression DAGs, symbol leaves, variable
spellings and variable name-key syntax without recursively unfolding sharing.
It does not visit grounded resource interiors. It is a syntax visitor, not a
complete runtime root scanner.

## Holder declaration guard

[`lib/symbol_lifetimes.metta`](../../lib/symbol_lifetimes.metta) declares lifetime
classes. [`scripts/check_symbol_lifetimes.py`](../../scripts/check_symbol_lifetimes.py)
checks them against Clang's AST for the selected build, including typedefs,
arrays, macro-generated fields and static/thread-local IDs. Same-shaped
anonymous records are distinguished by their owning members. Callback
signatures mentioning `SymbolId` are not mistaken for ID storage.

The tested Python/Prolog build has **790 holders**, including 422 generated
builtin fields. **160 holders remain explicitly unresolved.** The normal guard
fails on any undeclared typed holder; `--require-resolved` additionally fails
on unresolved holders and currently must reject this build.

This is a declaration check, not generated C tracing or a root-completeness
proof. Handles erased to plain integers, Atom-pointer ownership and borrowed
foreign references still require separate audits. The census covers enabled
translation units, not every optional build configuration.

From the repository root:

```sh
make -j1 test-symbol-lifetimes test-runtime-symbol-births \
  test-symbol-holder-census-unit test-symbol-holder-census test-symbol-id-shift
make -j1 test
```

The guard writes its inventory and compiler cache under ignored `runtime/`.
Its mutation tests check newly introduced fields, unresolved owners, generated
vocabulary and distinct anonymous records. Runtime tests cover promotion,
concurrent interning, resizing, parser modes, DAG traversal, durable decoding,
registration, gauge resets and non-destructive shadow scans.

The shifted-ID executable inserts symbols before builtin initialization and
compares reader, type, data and Python-result observations with the ordinary
engine. The standalone symbol tests also pass AddressSanitizer and
UndefinedBehaviorSanitizer. The earlier instrumentation-only serial test target
passed for its tested Python/Prolog build. The managed representation is
qualified by the owner, ordinary PeTTa and matched-corpus gates above; neither
result qualifies automatic symbol-table collection or every optional build
configuration.

## Offline loop qualification and prerequisite

The unmodified base `ce8d39d423b95670dd6545817bfb1d6fd166440d` fails Omega's
configuration setup before reaching its heartbeat. The minimized
[`runtime_equation_registration.metta`](repros/runtime_equation_registration.metta)
reports successful `add-atom` but loses the newly constructed equation.

Its expected results were checked with clean SWI-PeTTa sources at
`6b7f52f064bdbc82fabd0a0998404121fb01d52e`. CeTTa's unmodified base instead
returns `true`, `()`, and `((empty))`.

The prerequisite fix from
`8e6645861a6eb1121f6fcb4ebaea737bb2819c79` is now included: the specializer
holds the data arguments of native space operations. The minimal fixture and
the existing `specialize_space_arguments` fixture pass, as do the adjacent
data-specialization, dynamic-head and sort-value fixtures. This is a separate
conformance fix, not symbol reclamation. On this worktree the following diff
must now pass:

```sh
./cetta --lang petta verification/gc/repros/runtime_equation_registration.metta \
  > runtime/runtime_equation_registration.actual
diff -u verification/gc/repros/runtime_equation_registration.expected \
  runtime/runtime_equation_registration.actual
```

The unchanged Omega loop at `ee0618a293ec3662a32b10b09c6cbb073f59d6b2` now
boots and reaches its heartbeat with synthetic provider, channel, file and
clock adapters. Imports are explicit; statistics are read by materializing
the grounded `runtime-stats!` space. No live credentials or services are used.

Longer eager-prompt runs exposed a separate opt-base capacity failure: nursery
evacuation attempted to detach a prepared answer producer whose continuation
locals the detach visitor does not copy. The failure was reproduced on an
independent base build with only the prerequisite fix. The collector now checks
`cetta_prepared_pure_answer_cursor_can_detach` before admitting that evacuation.
Unsupported producer frontiers defer nursery evacuation without changing their
answers. Caller-arena producers are also excluded: their detach no-op does not
authorize freeing their caller's storage.

The unchanged core now completes the 100- and 400-pass trials on this branch.
The producer tests check successful private-arena detachment, caller-arena
exclusion, and refusal of a continuation frontier without losing its ordered
answers; existing continuation and handoff tests pass. This is a conservative
admission repair, not complete continuation evacuation. An unsupported frontier
can still retain nursery storage indefinitely. It does not collect symbols.

An isolated deferred-context prototype builds prompts only in the active
branch. On this branch, after the admission repair:

| Variant | Passes | Peak RSS (MiB) | Retained data-symbol bytes | Context reads | Provider calls |
|---|---:|---:|---:|---:|---:|
| Unchanged core | 100 | 64.1 | 20,371,407 | 101 | 2 |
| Unchanged core | 400 | 126.1 | 79,384,407 | 401 | 2 |
| Deferred context | 100 | 44.7 | 808,652 | 2 | 2 |
| Deferred context | 400 | 44.5 | 808,652 | 2 | 2 |

All four runs produced the same two provider requests and ordered tool actions.
The extra context read in the unchanged core occurs before the terminal
heartbeat stops the fixture. These are synthetic idle workloads, not a memory
bound for active traffic or a fix for the symbol table. The earlier working
merge engine at `c75a261c758367fde080ecf25603d927af387a06` showed the same
qualitative contrast; it is not an exact-base control or a source-level bisect.

The prototype is not a byte-identical replacement for the pinned core.
Controlled busy and mixed-input runs preserve provider requests and ordered
tool actions when context is stable. A heartbeat that changes context gives a
different request, as expected: reading context after heartbeat/channel effects
requires a purity and commutation contract. The pinned core remains unchanged.

## Lean foundations

`upstream/` contains two unchanged MeTTapedia modules and their MIT license,
including Zarathustra Goertzel's copyright, from
[`42c00805b497232ce29034806ac2eab396a26683`](https://github.com/zariuq/MeTTapedia/commit/42c00805b497232ce29034806ac2eab396a26683).
`upstream-pins.json` records paths, SHA-256 digests and original dependencies:
Lean 4.33.1 and mathlib `0df444a360eaa60ab8c11dca51a86af692955474`.

The unchanged sources and all three CeTTa modules also pass with Lean 4.31.0 and
mathlib `fabf563a7c95a166b8d7b6efca11c8b4dc9d911f`. This compatibility check
does not upgrade CeTTa dependencies or claim that the whole upstream builds.

With an existing built mathlib project and its matching elan toolchain:

```sh
bash verification/gc/verify.sh path/to/built/lean/project
```

The command verifies source digests, compiles exactly seven modules, prints
foundational axioms for selected theorems and records revisions in
`.build/verification-result.json`. The dependency project is read only.

The imported theory covers reachability, evacuation, nursery locality, sharing
with answer multiplicity preserved, and ephemeron marking as a least fixed
point. `SymbolCollection.lean` checks live spellings, canonical identity,
ordered duplicate answers, weak slots, stale handles, collection certificates
and deferred requests.

`SafeMarkCertificate` separates safety from exact marking: retaining a superset
of reachable symbols preserves their payloads. `RuntimeRootRefinement` adds an
explicit obligation that every permitted concrete future read is represented
by model reachability. `RootCensus.complete` only covers declared holders;
it cannot discharge that C-to-model obligation. A checked over-marking example
preserves an unreachable payload, demonstrating why safety alone gives no
reclamation or space bound.

`SymbolLifetime.lean` adds monotonic ownership, no ID rebinding over traces,
retired-key separation, non-destructive shadow behavior, request progress and
a conditional data-payload bound. Its allocation history is ghost state, not
retained C text. Retirement and scheduler steps model future operations;
they are not assertions that those C operations have been implemented.

`SymbolRepresentation.lean` checks an alternative hybrid interface over byte
spellings, including embedded NUL and non-UTF-8 values. Under canonicality and
valid-reference premises, mixed numbered/spelled equality agrees with observed
spelling. Content hashes agree across representations; promotion preserves
observation, hashing and registry lookup; table extension preserves existing
observations; old spelled values see later declarations; and answer lists keep
their ordering and duplicate entries. Symbols remain distinct from grounded
strings. A counterexample demonstrates why canonicality cannot be omitted.
The C prototype is tested against this interface contract; the theorem is not
a proof of its implementation. `verified_overapproximation` additionally proves
that an ordered candidate filter covering every true match preserves final
answers and duplicates after content verification.

`SymbolHandles.lean` checks the next retained-key experiment: counted borrows
survive sweeping, zero-count payloads are released, reused slots reject stale
generations, and exhaustion refuses allocation instead of wrapping. These
theorems model the explicit handle protocol. They do not establish that all
CeTTa C clients participate in that protocol.

`ArchiveRegions.lean` checks ordered projection transport across changed term
IDs, old-reader preservation across retirement, collection after the last
counted reader is released, stale-generation rejection and bounded-slot
backpressure. Its space law includes old leased archives and allocation slack;
it requires separate host accounting for indexes and allocator overhead.
The actual Arena/TermUniverse/Space experiment preserves pending cursor
positions, binary values and duplicates while releasing retired generations.
This does not establish that the evaluator's readers all hold generation
leases; ordinary fact removal still retains its archive in the evaluator.

There are no `sorry`, new axioms or `native_decide` proofs. The executable
examples are model checks. Safety requires over-approximate marking and
concrete read coverage; exact marking is a stronger reclamation premise.
Quiescence and protected borrows remain explicit hypotheses. The payload bound excludes
owned text and ID metadata and assumes a bound on allocations between
eligible collection polls. No C memory-safety or universal performance proof
is claimed.

## Experimental comparison

[`experiments/README.md`](experiments/README.md) describes the matched workloads,
negative controls, accounting and limitations. From the repository root:

```sh
make -j1 ENABLE_RUNTIME_STATS=1 gc-symbol-experiments
make -j1 ENABLE_RUNTIME_STATS=1 gc-symbol-runtime-probe
```

The first command tests isolated representations and eleven selected real
runtime owners. The second builds a non-destructive evaluator probe. Diagnostic
translation units and receipts live under ignored `runtime/gc-experiments/`;
ordinary engine builds do not use the probe. No live provider or service is
required.

## Representation choices

The two approaches can be combined, but have different first implementation
obligations:

| Approach | Preserved interface | Remaining safety and growth obligations |
|---|---|---|
| Collect data-born interned payloads | Current Atom layout, numeric IDs and all existing ID-based keys. | Complete strong roots and foreign leases, choice-aware collection boundaries, protected borrows, weak-cache policy; retirement without ID reuse still retains metadata. |
| Keep transient symbol bytes in managed arenas; intern at required numeric-key boundaries | The language's symbol value, with numbered/spelled representations underneath. | Arena tracing/copying/borrow discipline, raw-ID consumers and encoded keys, coherent equality and hash indexes, lookup after later registration; persistent term archives and names registered then dropped still grow. |

Managed spellings do not remove the existing arena's collection obligations.
In particular, pending choices must survive evacuation. Current structural
hashing uses spelling, while the permanent hash-cons slot hash mixes numeric
identity. The prototype excludes spelled graphs from that permanent index;
changing only structural hashing would not have been sufficient.
The compiler holder census also does not discover handles erased to ordinary
integers. A representation change needs an additional consumer/encoding audit.

Neither a permanent interner nor first-phase monotonic retirement establishes
a total-memory bound for indefinite fresh-name churn. The present Lean payload
bound explicitly excludes owned text and metadata. Reclaiming/reusing metadata
eventually requires registered ownership and an ABA-safe handle policy, or an
equivalent bounded representation.

## Remaining runtime obligations

The first sweep must keep the nursery and existing generated evaluator-root
frames, preserve live IDs and retire dead IDs without reuse. Permanent C
vocabulary is protected by the legacy intern contract. Runtime data still
needs a complete root walk across spaces, states, bindings, suspended choices,
OEM and prepared-pure cursors, incomplete tables, strong caches, canonical
term encodings, closures and foreign-held values.

Pending choices must not block collection: long-running agent loops can retain
them continuously. A choice-aware outermost guest-step boundary must establish
quiescence and protect borrowers; a writer mutex alone does not stop unlocked
`symbol_bytes()` readers. Nested evaluation and parallel workers need explicit
deferral or an audited parking protocol. Numbered table text remains valid until
table destruction; managed spelling borrows already have arena lifetimes.

A byte-based request should remain latched until such a boundary is eligible.
An imported or user-defined `gc` must keep its own meaning; this package adds
no name-based interception. Omega's imported helper can continue collecting
SWI-owned storage independently.

Before freeing: resolve holder ownership, implement the complete scanner,
exercise each retention path, run off/on differential and adversarial tests,
and measure the real loop with synthetic providers and channels. Poison
experiments change observations and require isolation. Passing a differential
suite is useful evidence, not a proof of all possible roots.

No-reuse prevents an old ID from aliasing a different spelling; it does not
make a missed root safe. Missing roots can still corrupt values or invalidate
borrowed pointers. The first phase may retain entry metadata after retirement.
Existing table resize visits all issued IDs, so its cost is not merely
proportional to live symbols. Bounded metadata and amortized collection work
need further design and measurement before making those claims.

## Pending choices and terminal transfers

The combined engine also evacuates the older arena while the admitted
movable equation/answer/once alternatives remain pending. One copy session
transports the related value roots, including bindings, choices and trails.
Physical arena checkpoints rebase; logical commitment and answer order remain.
Delayed owners, shared tables, scratch owners and unsupported borrowed
producers conservatively defer this collection. This is not a progress proof
for every machine state.

A terminal compiled-to-host transfer no longer leases metadata-only compiler
storage. Descriptor-free plans instead receive bounded, content-keyed owned
snapshots; outgoing values travel in one copy session before the producer is
released. Plans containing deterministic-region or region-hole descriptors
keep their original lease. Capacity exhaustion uses that same safe fallback.

`Cetta/GC/ChoiceGenerations.lean` gives the transport, observation, commitment,
release and conditional space contracts. The direct machine tests retain
pending alternatives and binary payloads across a major collection, require
progress before excessive promotion, and check snapshot lifetime and fallback.
The combined verifier checks this module with the symbol models above.

## Collectible registered names

`registered_symbols.c` implements an owned canonical spelling table. Its two
indexes are weak: neither index contributes a reference. An acquired handle,
cloned handle, or byte borrow contributes one hold. Releasing the last hold
unlinks and frees the entry and its bytes. Shrinking indexes also release
reserved metadata. Entry addresses stay stable while held; publication and
reference transitions run under the table mutex. The payload is a leaf and
cannot form a strong-reference cycle.

Handles contain a table identity and a monotone 64-bit token. Tokens and table
identities never repeat; finite-width exhaustion refuses allocation. Retired
entries consume no slot history. A token is not a legacy `SymbolId`, and may
not be cast into one. Copying handle bits is not a retain operation. Destroying
a table requires external quiescence and is refused while a hold exists.

`registered_symbol_owner.c` connects this protocol to actual Arena owners.
Copying a view retains its owner in the destination; resetting the last owning
arena ends its borrow. A tiny spelling does not retain the source arena.

The first evaluator integration is `Registry`. Unknown symbol names own a
counted spelling; known permanent IDs retain their numeric fast path. Scope
copies acquire independent ownership. Lookup never inserts a missing name.
Later numeric registration of the spelling cannot hide an earlier dynamic
entry. Removal releases its key; registry destruction releases all keys.
Named state retains names under the same protocol, with separate storage for
values and fresh variable copies on writes. A still-declared state remains
live until its registry is destroyed. Structural name keys keep their own
copied atoms instead of forcing their symbols into the global table.

Registry mutation keeps its existing owner/serialization contract; making its
spelling table thread-safe does not make concurrent registry mutation valid.
Returned values remain borrowed until copied, as before. Lookup, transaction,
worker and rho scope-copy sites use the full name rather than dropping it to
an absent numeric ID.

Run the collector and integrated owner gates:

```sh
make -j1 BUILD=python ENABLE_LIB_PROLOG=1 ENABLE_RUNTIME_STATS=1 \
  ENABLE_MANAGED_SYMBOLS=1 test-registered-symbols test-registered-registry \
  test-registered-symbol-names test-registry-resolver \
  test-managed-symbol-owners test-petta-semantics
python3 verification/gc/experiments/registered_mutants.py \
  --output runtime/gc-experiments/registered-mutants
```

The collector test includes binary names, independently owned borrows, stale
handles, exhaustion, concurrent publication, and actual arena transport.
Churn checks both payload release and index reservation. The registry test
checks source-registry destruction after copying, later numeric registration,
and named-state value replacement without permanent interning.

`Cetta/GC/RegisteredSymbols.lean` checks borrow survival, last-release
reclamation, fresh-token separation, finite exhaustion and an accounted space
bound. These are protocol properties. C ownership coverage and synchronized
publication remain explicit refinement obligations, exercised by the native
tests rather than purportedly proved by Lean. The space bound charges entry
metadata and index capacity, assumes successful compaction, and excludes
allocator overhead, registry arrays, Arena owner records, and unrelated live
values. Allocation failure retains the old index safely and remains visible
in the gauges. It does not justify a bound on the legacy symbol table.
