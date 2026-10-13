# Symbol representation and root-coverage experiments

These experiments separate three questions: whether a scanner covers later
reads, whether unused payloads are released, and whether retained metadata
also stays bounded. They do not qualify a production symbol collector.

## Full-engine representation trial

The evaluator prototype is enabled by `ENABLE_MANAGED_SYMBOLS=1`, off by
default. It retains runtime Symbol spelling in the owning arena until a real
numeric-key boundary needs registration. Known numeric dispatch remains the
fast path. Copying, mixed equality/hash, late registration and binary values
are tested in actual arena, prepared-answer and term-universe owners.

Build two engines with identical Python/Prolog and runtime-stat settings,
changing only `ENABLE_MANAGED_SYMBOLS`. Then run:

```sh
python3 verification/gc/experiments/engine_trials.py \
  --engine path/to/managed-engine --reference path/to/control-engine \
  --output runtime/gc-experiments/engine-trials
```

The runner checks the public language-owner fixture and three paired 100/400
birth workloads: temporary parse/drop, ordinary fact add/remove, and dynamic
equation add/remove. Outputs must agree. Temporary spelled payload must return
to zero; the other two workloads record residual storage rather than hiding it.
Binary/source digests, gauges, RSS and logs are saved under the output directory.
These trials use a normal instrumented build; sanitizers are run separately.

An existing synthetic Omega harness can be supplied with `--omega-harness`
and `--omega-loop`. The runner verifies the pinned loop digest below, compares
100/400 idle passes and 30-pass busy/mixed cases, and requires exact normalized
provider-request records and ordered-action digests. Adapters must supply
`run.metta`, controlled `HARNESS_*` inputs, `harness-summary: ` and
`symbol-table: ` JSON lines. Provider/channel/file/clock effects are synthetic.

On the unchanged pinned loop, the managed engine's idle symbol-table data
payload stops growing, as do its managed arena payload and hash-cons index.
The control keeps about 20 MB at 100 passes and 79 MB at 400. Provider requests
and ordered actions agree in all four cases. This demonstrates the transient
interning repair on that workload, not a universal active-traffic memory bound.

The churn trial finds an additional problem: removing all ordinary facts leaves
their spelled payload in the pointer-backed persistent term archive (about
0.41 MB after 100 births and 1.65 MB after 400). Dynamic equation keys remain
registered after removal. A symbol-table collector alone cannot reclaim the
first residual; term identities, surviving readers and old plan leases need
their own lifecycle contract.

The fixed instruction panel uses three alternating pairs on identically
instrumented on/off binaries with event counters disabled. Guest outputs agree.
Known dispatch, unknown lookup, mixed equality, nilbc, typed calls, Loowoz and
HE are measured separately; this panel makes no wall-time claim. The largest
measured increase is the unknown-lookup case, about 3.2%; other rows range from
a small improvement to about 1.3% extra instructions. These are finite panel
results, not a universal performance guarantee.

Actual implementation mutants are separate from the standalone mutants:

```sh
make -j1 ENABLE_PYTHON=1 ENABLE_RUNTIME_STATS=1 ENABLE_MANAGED_SYMBOLS=1 \
  test-managed-symbol-mutants
```

The runner compiles private copies of `atom.c`, first checks an unchanged
positive control, then corrupts copy ownership, delayed-registration lookup
and mixed hashing in turn. Each must fail a semantic assertion in the actual
value unit. Build failures and sanitizer-startup failures are rejected as
evidence. The original engine sources are left unchanged.

## Generation-checked retained-key experiment

```sh
make -j1 gc-symbol-handle-slots
```

`handle_slots.c` uses a weak canonical lookup and counted strong clients. A
borrow requires a held generation-checked handle. Zero-count payloads are
freed, slots are reused with an incremented generation, and stale reads and
releases fail. Exhausted 32-bit generations refuse allocation rather than
wrapping. Its deliberately broken generation-reuse and missing-strong-copy
controls must fail. Generation checking cannot repair omitted ownership.

For 32,768 unique 4,096-byte keys with 64 held at a time, payload is 262,144
bytes at both 8,192 and 32,768 creations, peak payload is 524,288 bytes, and
metadata is 3,112 bytes throughout. Releasing every handle leaves zero payload.
This uses 128 fixed slots and reports capacity/exhaustion explicitly. It does
not prove unlimited allocation, production throughput, parallel safety or that
all C holders obey the protocol. Handles belong to one table lifetime.

`SymbolHandles.lean` checks counted-borrow preservation, dead-payload removal,
stale-generation rejection and finite-limit refusal. The C experiment also
passes ASan/UBSan with leak detection. No evaluator SymbolId is recycled by it.

## Leased archive-generation experiment

```sh
make -j1 ENABLE_PYTHON=1 ENABLE_RUNTIME_STATS=1 ENABLE_MANAGED_SYMBOLS=1 \
  gc-symbol-archive-regions
```

`epoch_regions.c` uses actual Arena, TermUniverse and Space owners. At each
publication it copies the current ordered fact projection into a new
generation. Old generations stay alive while counted readers hold them. A
reader wraps the actual occurrence cursor; its clone takes an independent
lease. The test captures a cursor before removing a fact, publishes the
surviving projection, then resumes both the original and cloned readers.
The old reader still observes its captured binary values and duplicate rows.

Released generations are freed at the next poll. Bare TermUniverseRootTokens
do not extend an archive's lifetime, and stale tokens cannot resolve in a new
generation. The fixed four-slot experiment refuses publication when all slots
are held. Omitted-reader and reversed-projection controls must fail semantic
checks. Normal and ASan/UBSan builds pass, including those controls.

The churn trial keeps eight current values and one old snapshot while creating
32,768 unique 4,096-byte values. At both 8,192 and 32,768 creations it has two
regions, 163,880 bytes of managed payload, 229,376 reserved arena bytes and
3,072 bytes of term-entry arrays. Releasing the old snapshot leaves 32,776
bytes of payload and 49,152 reserved arena bytes; destruction leaves no region
payload. Index structures, thread-local caches and allocator overhead are not
all included in these gauges, so this is not a total-memory bound.

`ArchiveRegions.lean` proves the conditional ownership and ordered-copy laws.
This is an isolated fact-only archive collector, not evaluator integration.
Mutable resources, compiled programs, old plan leases, foreign byte borrows
and worker readers require participation in the same ownership protocol
before production archives can be retired. The managed evaluator still
retains removed facts; this experiment identifies and tests the next seam.

## Run the standalone comparison

From the repository root, with the configured Python/Prolog build dependencies:

```sh
make -j1 ENABLE_RUNTIME_STATS=1 gc-symbol-experiments
make -j1 ENABLE_RUNTIME_STATS=1 gc-symbol-runtime-probe
```

The first target prints the result receipt path. Sources are in this directory;
generated objects, binaries and logs stay under ignored `runtime/`. The normal
`test` target does not run these optional experiments. GNU linker wrapping is
required for the runtime probes.

`prototypes.c` compares the following representations under the same byte-value,
equality, hashing, registration, answer-order and explicit-root contract:

- **Weak interning:** actual `src/symbol.c` entries; enumerated strong handles
  mark payloads; the canonicalization index is weak. Dead payloads are freed,
  their IDs retired, and the lookup table rebuilt. Live IDs never move.
- **Managed spellings:** reference-counted byte buffers stand in for managed
  arena payloads. Numbering happens only on explicit registration. Numbered
  names remain permanently interned.
- **Managed spellings with key collection:** the same buffers, with collectible
  registered-name payloads marked by live numbered buffers.

Only this standalone executable frees symbol payloads. Its complete root
enumeration applies to its own explicit handles. It is not a replacement for
CeTTa's arena collector. The separate full-engine prototype above supplies
managed spelling without linking this table sweep into the evaluator.

Semantic checks cover embedded NUL, non-UTF-8 bytes, distinct prefixes, mixed
spelled/numbered equality and hashing, promotion, a failed lookup followed by
registration, existing spelled values seeing a later declaration, ordered
duplicate answers, retained reads and reintroduction of a retired spelling.
Three deliberate mutants must fail: omit a retained root, hash equal values
by incompatible IDs, and rebind a retired ID without stale-handle protection.

The fixed churn workload creates 32,768 distinct 4,096-byte values, retains 64
at a time and polls at an allocation threshold of 64 payloads. It runs both
without numbering and with every 64th value numbered then later dropped.
Snapshots report text payload separately from reserved table and buffer
metadata; allocator slack and whole-process RSS are not measured by them.

For the latter workload, the observed byte counts were:

| Representation | Payload at 8,192 creations | Payload at 32,768 creations | Payload after releasing every handle | Metadata at 32,768 creations |
|---|---:|---:|---:|---:|
| Weak interning | 262,208 | 262,208 | 0 | 3,735,552 |
| Managed spellings, permanent keys | 786,624 | 2,359,872 | 2,097,664 | 2,689,024 |
| Managed spellings with key collection | 266,305 | 266,305 | 0 | 2,689,024 |

The weak table's metadata grew from 2,686,976 bytes at the first snapshot.
Permanent keys explain the managed-only residual. The combined representation
has an additional 4,097-byte table copy of the live registered spelling.
Its metadata remained within the same allocated chunk in this finite test;
it still retires IDs monotonically. Neither collecting variant demonstrates
a total-memory bound for indefinite fresh-name churn. Slot reuse needs a
stale-handle and finite-width exhaustion policy. Rebuilding scans historical
IDs, so these results also do not establish amortized collection cost.

The standalone comparison and all three mutant controls pass AddressSanitizer
and UndefinedBehaviorSanitizer with leak detection. These checks do not apply
to the complete evaluator. A sanitizer build can be run separately:

```sh
cc -std=c11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
  -Wall -Wextra -Werror -pthread -o runtime/gc-experiments/prototypes-sanitized \
  verification/gc/experiments/prototypes.c
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 runtime/gc-experiments/prototypes-sanitized 32768
```

## Non-destructive real-runtime probe

`instrument_machine.py` generates diagnostic copies of the two machine
translation units, preserving `src/`. At sampled outermost guest-machine polls,
the visitor records marks without freeing, poisoning or rewriting payloads.
The GNU link wrappers compare later symbol text, length, hash and pin accesses
against those marks. Reacquiring an ID by spelling is treated separately from
reading an old retained handle. The probe supports one guest thread; it does
not implement worker parking or a C-stack hazard analysis.

`CETTA_GC_SHADOW_EVERY=1` samples every such poll. A larger value samples less
often. `CETTA_GC_SHADOW_OMIT=<family>` deliberately omits a visitor.
`CETTA_GC_SHADOW_DIAG=1` records the first missed read's call offsets without
printing its spelling. `CETTA_GC_PROBE_DUMP=1` emits the measured retention-site
summary when runtime statistics are read.

`shadow_owners.c` has eleven independent expected-read fixtures: syntax,
encoded spaces, state cells, closures, bindings, term graphs, current delayed
goals, explicit foreign leases, durable decode, prepared answers and named
states. Each must pass with its visitor and fail when that visitor is omitted.
Prepared answers retain duplicate order. The foreign-lease fixture also reads
an already-borrowed pointer directly; its independently declared expected ID
catches omission even though the pointer read bypasses the wrappers.

An offline harness exercised the unchanged Omega loop at
`ee0618a293ec3662a32b10b09c6cbb073f59d6b2`, loop SHA-256
`21f7db7ef994b566451468a7e6341b475ab73f3237fb9e65706407cafc4ad2b7`.
It used synthetic provider, channel, file, history and clock adapters.
Reference and instrumented engines produced identical provider-request and
ordered-action digests on five idle, busy, mixed and prompt-deferral cases.
The earlier root-probe trials used numeric symbols. The full-engine managed
spelling trial above subsequently ran the unchanged Omega core separately.

The longer idle trial exposed a real gap in the initial experimental scanner:
`petta_eval_machine_named_state` later read a data symbol owned by the separate
named-state namespace. Normal registry entries did not cover it. Adding a
visitor through `registry_state_count/entry` removed the observed misses.
Omitting that visitor again produces five misses in the 400-pass trial while
leaving the observable guest trace unchanged. The 100-pass trial had missed
this defect. This is a scanner counterexample, not a new evaluator leak.

At 400 idle passes, five reviewed retention crossings observed 3 unique
data-born names, 109 bytes, out of 2,070 births totaling 79,384,516 bytes.
Crossings cover pinning, registered syntax, space addition, encoded stores and
registry keys. They are a sample of known boundaries, not all numeric-ID
consumers. That small fraction motivates reducing transient interning; it
does not prove that all other names are unreachable.

All 1,014 sampled marks in that trial occurred with pending choices. The busy
and mixed cases sampled every poll and also found no wrapped unmarked reads
after the named-state repair. Open cursor frontiers remained explicitly
opaque. Zero observed misses is therefore not a completeness certificate.
Raw-ID comparisons, prior byte borrows, rewindable delay history, old leased
plans, intrinsic-table payload ownership and parallel workers need additional
coverage. The outermost guest-depth check does not itself prove that no
caller C local or foreign reference survives a poll.

For an existing compatible offline harness, `run.py` accepts `--engine`,
`--reference` and `--omega-harness` in addition to `--prototype` and `--owners`.
The harness supplies `run.metta`, `run-lazy.metta`, the pinned loop under
`repos/Omega/src/loop.metta`, controlled `HARNESS_*` inputs and a final
`harness-summary: ` JSON line. The runner records binary/core digests and
compares observations, including the named-state omission control. Such
harness adapters are separate from the isolated representation contract.

## Design interpretation

The measurements support investigating managed transient spellings combined
with collectible retained keys. A weak nonmoving table also gives payload
relief under complete root coverage. The full-engine panel now measures ordinary
dispatch and unknown lookup under managed spelling. Arena evacuation, borrow
obligations and persistent retained churn remain separate. A permanent collector
still needs concrete owner coverage, worker coordination and total-space/progress
accounting.

[Go's weak canonicalization index](https://go.dev/src/unique/canonmap.go)
illustrates separating lookup from strong retention.
[MPS's C root protocol](https://memory-pool-system.readthedocs.io/en/latest/topic/root.html)
makes root registration, scanning and destruction explicit.
[SpiderMonkey's hazard analysis](https://firefox-source-docs.mozilla.org/js/HazardAnalysis/)
examines references live across GC calls; the typed-holder census here does
not perform that analysis. These are design precedents, not evidence that
CeTTa's concrete coverage is complete.
