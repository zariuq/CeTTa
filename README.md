# CeTTa

CeTTa is a direct C runtime for [MeTTa](https://metta-lang.dev/). This
snapshot is the current working runtime we can share honestly in git: it
builds, the fast golden suite is green, the profile suite is green, full
tilepuzzle now finishes, and the large genomic `bench_bio_1M.metta`
workload runs end to end.

## Build

CeTTa builds with:

- `gcc`
- `make`
- Python 3 development headers (`python3-config --embed` must work)

The normal build bootstraps the stdlib automatically in two stages:

```bash
make
```

That produces `./cetta`.

## Quick Start

Run a small file:

```bash
./cetta tests/test_map_filter_atom.metta
```

Run the fast verified suites:

```bash
make test
make test-profiles
```

Promote the current checked binary to the local stable runtime path:

```bash
make promote-runtime
```

## What Works In This Snapshot

- Core HE-style evaluation in C
- Arena-allocated atoms with hash-consing support
- Multiple space kinds and pluggable match backends
- SymbolId-based core dispatch instead of pervasive string comparison
- Runtime counters and profile-aware extension guards
- Optional PathMap/MORK-backed `pathmap-imported` matcher lane
- Local git-module and module-inventory surfaces

## Optional MORK / PathMap Bridge

If the static bridge library exists at
`../../hyperon/MORK/target/release/libcetta_space_bridge.a`, `make`
links it automatically and exposes the imported PathMap matcher.

Positive example:

```bash
./cetta --profile he_extended --space-match-backend pathmap-imported \
  tests/test_pathmap_imported_bridge_v2.metta
```

Negative example:

```bash
./cetta --space-match-backend pathmap-imported tests/file.metta
```

without the bridge library configured. In that case CeTTa will not have the
imported matcher available.

## Test Status

At this snapshot, the checked suites are:

- `make test` -> `285 passed, 0 failed, 16 skipped`
- `make test-profiles` -> `62 passed, 0 failed`

The `16 skipped` are not hidden failures. `make test` only treats
`tests/test_*.metta`, `tests/spec_*.metta`, and `tests/he_*.metta` files with
matching `.expected` goldens as strict pass/fail tests.

Today the skips are:

- `1` heavy benchmark-style file: `tests/test_tilepuzzle.metta`
- `5` probe or integration files without committed `.expected` goldens
- `10` translation or audit surface fixtures without committed `.expected`
  goldens

Positive example:

- a skipped file is exploratory, integration-heavy, or benchmark-shaped and
  has not been promoted into the fast golden suite yet

Negative example:

- a skipped file is not being used to hide a known failing golden comparison

## Examples

### Higher-order stdlib

```metta
!(map-atom (1 2 3 4) $v (eval (+ $v 1)))
!(filter-atom (1 2 3 4) $v (eval (> $v 2)))
```

```
$ ./cetta tests/test_map_filter_atom.metta
[(2 3 4 5)]
[(3 4)]
```

### Proof-carrying backward chaining

Nil Geisweiller's typed backward chainer produces machine-checkable proof
terms:

```metta
(: dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))
(: ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

!(bc &bio (fromNumber 2) (: $proof (target rs10000544 $gene)))
```

```
$ ./cetta --lang he tests/bench_backchain_heavy_nilbc.metta | tail -1
[(: (r-mortal r01 r01c) (mortal r01)), (: (r-mortal r02 r02c) (mortal r02)), ...]
```

Each result is a proof term: `(: (r-mortal r01 r01c) (mortal r01))` reads
"r01 is mortal, proved by rule r-mortal applied to evidence r01c."

### PLN genomic inference (1.4M hypotheses)

```metta
; 9 typed PLN rules over 261K atoms from GTEx, Reactome, Hetionet
!(let (: $proof (pathway-target rs10000544 $gene2 $pathway))
      (bc &bio (fromNumber 3) (: $proof (pathway-target rs10000544 $gene2 $pathway)))
   (pw-target rs10000544 $gene2 $pathway))
```

```
$ ./cetta --lang he tests/bench_bio_bc_deep.metta | head -3
=== Phase 1: Compounds binding rs10000544 targets (depth 2) ===
...
[(binding rs10000544 db00277 ensg00000138735), ...]
```

Full run: 1,411,430 hypotheses in 3m 22s across 17 query phases.

### Structured spaces (queue, hash, stack)

```metta
!(bind! &q (new-space queue))
!(space-push &q (task A))
!(space-push &q (task B))
!(space-pop &q)               ; → (task A)  (FIFO)

!(bind! &seen (new-space hash))
!(add-atom &seen (visited X))
!(match &seen (visited X) found)  ; → found (O(1) lookup)
```

### 8-puzzle BFS (181K states)

```
$ ./cetta --profile he_extended tests/test_tilepuzzle.metta
8-puzzle BFS: counting all reachable states from solved position...
[()]
[()]
[181440]
```

Uses queue-space for BFS frontier and hash-space for visited-set dedup.

## Good First Things To Run

Small regression-sized examples:

```bash
./cetta tests/test_map_filter_atom.metta
./cetta tests/test_runtime_stats_surface.metta
./cetta tests/test_queue_space.metta
```

Optional MORK-facing surface:

```bash
./cetta tests/test_mork_lib_surface.metta
```

Bounded tile probe:

```bash
./cetta --count-only --profile he_extended \
  tests/bench_tilepuzzle_probe.metta
```

Full tile puzzle:

```bash
./cetta --profile he_extended tests/test_tilepuzzle.metta
```

Large genomic benchmark:

```bash
timeout 600 ./cetta tests/bench_bio_1M.metta
```

The large bio benchmark is included because it is now runnable in this tree.
On our March 28, 2026 check, loading the 1.4M-atom dataset took about five
seconds and the full workload finished in about 386 seconds. The remaining
large-KB gap is inference, not loading.

## Repository Map

Core runtime:

- `src/atom.c`, `src/atom.h`: atom representation, arena allocation, equality
- `src/symbol.c`, `src/symbol.h`: SymbolId table and builtin ids
- `src/match.c`, `src/match.h`: unification, bindings, substitution
- `src/space.c`, `src/space.h`: spaces, query dispatch, space operations
- `src/space_match_backend.c`, `src/space_match_backend.h`: backend selection
- `src/subst_tree.c`, `src/subst_tree.h`: indexed equation and substitution tree
- `src/eval.c`, `src/eval.h`: evaluator
- `src/grounded.c`: grounded operators and runtime surfaces
- `src/compile.c`: LLVM IR emission
- `src/mork_space_bridge_runtime.c`, `src/mork_space_bridge_runtime.h`:
  runtime-side bridge glue for the imported MORK lane

Libraries:

- `lib/stdlib.metta`: main bundled stdlib
- `lib/mork.metta`: narrow MORK-facing helper surface
- `lib/metamath.metta`: generic textual Metamath-facing helpers
- `lib/fs.metta`, `lib/system.metta`, `lib/str.metta`, `lib/path.metta`:
  extension libraries used by the current profile surfaces

Useful workloads:

- `tests/test_tilepuzzle.metta`: full tile BFS benchmark
- `tests/bench_tilepuzzle_probe.metta`: bounded tile runtime probe
- `tests/bench_bio_1M.metta`: 1.4M-atom genomic benchmark
- `tests/profile_tilepuzzle_5k.metta`: smaller profiling variant
- `tests/bench_conjunction12_he.metta`: conjunction stress benchmark
- `tests/bench_matchjoin8_he.metta`: join stress benchmark

## CLI

```text
./cetta [options] <file.metta>

Options:
  --lang <name>                    Set language mode
  --profile <name>                 Set evaluation profile
  --fuel <n>                       Set evaluation fuel budget
  --count-only                     Print result counts instead of atoms
  --space-match-backend <name>     Select pattern matching backend
  --compile <file>                 Emit LLVM IR to stdout
  --list-profiles                  Show available profiles
  --list-space-match-backends      Show available backends
  --list-languages                 Show supported languages
```

## Notes

- This repository contains the working runtime snapshot, not a polished
  release branch.
- The fast suites are intentionally strict and cheap.
- Benchmark and probe files that do not yet have `.expected` transcripts may
  still be useful, but they are not counted as golden regressions yet.
