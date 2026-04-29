# CeTTa

CeTTa is a direct C runtime for [MeTTa](https://metta-lang.dev/). The default
public lane is the HE-style evaluator (`--lang he`) with the
`he_extended` profile.

## Requirements

For the default build:

- `gcc`
- `make`
- Python 3 development headers and a working `python3-config --embed`

For the optional bridge builds:

- Rust/Cargo
- local sibling checkouts of PathMap and MORK in the layout shown below

## Quick Build

From the repository root:

```bash
make
```

That bootstraps the stdlib in two stages and produces `./cetta`.

Run a small file:

```bash
./cetta tests/test_map_filter_atom.metta
```

For the full CLI surface, run `./cetta --help`.

If you want the smaller no-Python binary instead:

```bash
make BUILD=core
```

## Verified Test Commands

Run the main checked suite:

```bash
make test
```

Run the profile surface suite:

```bash
make test-profiles
```

`make test` always runs the fast HE/golden suite and the runtime-stats lane.
If the optional Rust bridge dependencies are available locally, it also runs the
MM2/MORK regression lane. If they are not available, those bridge-only lanes are
skipped cleanly instead of failing the default build/test path.
The checked default lanes vendor their tiny Metamath parser fixtures under
`tests/support/`, so they do not require a separate Metamath checkout either.

Promote the current checked binary to the local stable runtime path:

```bash
make promote-runtime
```

## Optional Bridge Builds

The bridge-enabled modes build `rust/cetta-space-bridge` and link it
statically into CeTTa. They are optional; you do not need them for the default
HE-style build above.

Expected checkout layout:

```text
<parent>/
├── CeTTa/
├── MORK/
└── PathMap/
```

CeTTa's optional bridge build currently follows the same sibling-repo layout
that MORK itself expects.

If you already keep `MORK` and `PathMap` elsewhere, the simplest workaround is
to create sibling symlinks next to `CeTTa`:

```bash
ln -s /path/to/MORK ../MORK
ln -s /path/to/PathMap ../PathMap
```

| Mode | Purpose | Python | Extra repos needed |
|------|---------|--------|--------------------|
| `BUILD=core` | small standalone binary | no | none |
| `BUILD=python` or plain `make` | default daily-driver build | yes | none |
| `BUILD=mork` | MM2/lib_mork bridge lane | no | `MORK`, `PathMap` |
| `BUILD=main` | MM2/lib_mork bridge lane | yes | `MORK`, `PathMap` |
| `BUILD=pathmap` | `(new-space pathmap)` with multiset semantics | no | `MORK`, `PathMap` |
| `BUILD=full` | pathmap + Python | yes | `MORK`, `PathMap` |

Current bridge branch expectations:

- PathMap: [Adam-Vandervorst/PathMap](https://github.com/Adam-Vandervorst/PathMap) `master`
- MORK: [zariuq/MORK](https://github.com/zariuq/MORK) `cetta/query-multi-factor-exprs`

If you need MM2 arithmetic sink examples (`i+`, `i-`, `i*`, `f+`, `f-`, `f*`)
today, use the MORK branch
[`feature/arithsinks-pr-sync`](https://github.com/zariuq/MORK/tree/feature/arithsinks-pr-sync)
instead.

Build the MM2/lib_mork lane:

```bash
ulimit -v 10485760 && make BUILD=mork
./cetta examples/mork_showcase.metta
./cetta examples/mork_mm2_showcase.metta
```

The first bridge build can take a few minutes on a cold Cargo cache.

For a longer bridge/MM2 walkthrough, see `MORK_TUTORIAL.md`.

Build counted-key PathMap spaces:

```bash
ulimit -v 10485760 && make BUILD=pathmap
./cetta tests/test_pathmap_counted_space_surface.metta
```

The `BUILD=pathmap` and `BUILD=full` modes enable `(new-space pathmap)` with
counted-key storage. This stores `atom_bytes || count_bytes` as PathMap keys,
preserving multiset semantics while still benefiting from PathMap's structural
matching. See `specs/pathmap_counted_space_design.txt` for the full design.

The bridge code includes two thin compatibility adapters:

- `rust/cetta-pathmap-adapter/` for PathMap compatibility
- `rust/cetta-mork-adapter/` for MORK compatibility

## What Works In This Snapshot

- Core HE-style evaluation in C
- Arena-allocated atoms with hash-consing support
- Multiple space kinds and pluggable space engines
- SymbolId-based core dispatch instead of pervasive string comparison
- Runtime counters plus `--profile`-aware surface guards
- Explicit `TermUniverse` / persistent term-store seam
- Variant tabling infrastructure with shared canonicalization substrate
- Optional `pathmap` engine for ordinary MeTTa over PathMap
- Explicit `mork:` helper surface for the MORK/MM2 execution lane
- Local git-module and module-inventory surfaces
- Python foreign-module support in the default build

## Test Status

Fresh verification on a clean export of this tree produced:

- `make test`: main HE/golden sweep `335 passed, 0 failed, 38 skipped`, then the
  runtime-stats lane `16 passed, 0 failed`
- `make test-profiles`: `77 passed, 0 failed`

If the bridge dependencies are available locally, `make test` also re-runs the
MORK runtime-stats spot checks after the default lanes. If they are not
available, those bridge-only lanes are skipped with an explicit message.

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
(: r-mortal (-> (: $r (researcher $x))
                (: $c (curious $x))
                (mortal $x)))

!(bc &kb (fromNumber 1) (: $prf (mortal $x)))
```

```
$ ./cetta --lang he tests/bench_backchain_heavy_nilbc.metta | tail -1
[(: (r-mortal r01 r01c) (mortal r01)), (: (r-mortal r02 r02c) (mortal r02)), ...]
```

Each result is a proof term: `(: (r-mortal r01 r01c) (mortal r01))` reads
"r01 is mortal, proved by rule r-mortal applied to evidence r01c."

### Large genomic benchmark preview (1.4M atoms)

```bash
$ timeout 15 ./cetta --lang he benchmarks/bench_bio_1M.metta 2>/dev/null | head -3
=== Phase 1: Direct targets of rs10000544 (depth 1, 1.4M atoms) ===
=== Phase 2: Diseases from rs10000544 (depth 2) ===
=== Phase 3: TF regulatory targets of rs10000544 (depth 2) ===
```

This preview uses a tracked workload in `benchmarks/`. The full run on our
development machine produces approximately 1,411,430 hypotheses in 3m 22s
across 17 query phases.

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

Uses queue-space for BFS frontier and hash-space for visited-set dedup. This is
a longer-running example on our development machine; prefer the smaller files
below for a quick smoke test.

## Good First Things To Run

Small regression-sized examples:

```bash
./cetta tests/test_map_filter_atom.metta
./cetta tests/bench_backchain_heavy_nilbc.metta | tail -1
```

Python-facing surface in the default build:

```bash
./cetta tests/test_py_ops_surface.metta
```

Optional MORK-facing surface (requires sibling `MORK` / `PathMap` checkouts):

```bash
make BUILD=mork
./cetta examples/mork_showcase.metta
```

Large genomic benchmark preview:

```bash
timeout 15 ./cetta --lang he benchmarks/bench_bio_1M.metta 2>/dev/null | head -3
```

Full tile puzzle (longer-running):

```bash
./cetta --profile he_extended tests/test_tilepuzzle.metta
```

The large bio benchmark is included because it is runnable in this tree, but
this branch should still be understood as an actively developed runtime rather
than a frozen benchmark release.

## Repository Map

Core runtime:

- `src/atom.c`, `src/atom.h`: atom representation, arena allocation, equality
- `src/symbol.c`, `src/symbol.h`: SymbolId table and builtin ids
- `src/match.c`, `src/match.h`: unification, bindings, substitution
- `src/space.c`, `src/space.h`: spaces, query dispatch, space operations
- `src/space_match_backend.c`, `src/space_match_backend.h`: space-engine selection
- `src/subst_tree.c`, `src/subst_tree.h`: indexed equation and substitution tree
- `src/term_universe.c`, `src/term_universe.h`: persistent term-universe seam
- `src/term_canon.c`, `src/term_canon.h`: shared variable canonicalization/remap substrate
- `src/table_store.c`, `src/table_store.h`: variant tabling and staged answer store
- `src/eval.c`, `src/eval.h`: evaluator
- `src/grounded.c`: grounded operators and runtime surfaces
- `src/compile.c`: LLVM IR emission
- `src/mork_space_bridge_runtime.c`, `src/mork_space_bridge_runtime.h`:
  runtime-side bridge glue for the PathMap / MORK bridge lane

MORK bridge (Rust):

- `rust/Cargo.toml`: workspace root for CeTTa-owned Rust crates
- `rust/cetta-space-bridge/`: C FFI bridge between CeTTa and MORK/PathMap
- `rust/cetta-space-bridge/src/counted_pathmap.rs`: counted-key multiset storage for `(new-space pathmap)`
- `rust/cetta-pathmap-adapter/`: PathMap compatibility layer (OverlayZipper, snapshot helpers)
- `rust/cetta-mork-adapter/`: MORK compatibility layer (factor expression query wrapper)

Specs:

- `specs/pathmap_counted_space_design.txt`: counted-key storage design for multiset over PathMap

Libraries:

- `lib/stdlib.metta`: main bundled stdlib
- `lib/lib_pln.metta`: upstream-compatible PeTTa PLN baseline for CeTTa
- `lib/lib_wmpln.metta`: Lean-aligned wmPLN surface with evidence-backed extensions
- `lib/mork.metta`: narrow MORK-facing helper surface
- `lib/metamath.metta`: generic textual Metamath-facing helpers
- `lib/fs.metta`, `lib/system.metta`, `lib/str.metta`, `lib/path.metta`:
  extension libraries used by the current profile surfaces

Useful workloads:

- `tests/test_tilepuzzle.metta`: full tile BFS benchmark
- `benchmarks/bench_tilepuzzle_probe.metta`: bounded tile runtime probe (runtime-stats surface)
- `benchmarks/bench_bio_1M.metta`: 1.4M-atom genomic benchmark
- `tests/bench_conjunction12_he.metta`: conjunction stress benchmark
- `tests/bench_matchjoin8_he.metta`: join stress benchmark
- `tests/test_pathmap_counted_space_surface.metta`: multiset semantics over PathMap
- `tests/bench_duplicate_conjunction_he.metta`: duplicate conjunction parity test

## CLI

```text
./cetta [options] <file.metta>

Options:
  --lang <name>                    Set language mode
  --profile <name>                 Set evaluation profile
  --fuel <n>                       Set evaluation fuel budget
  --count-only                     Print result counts instead of atoms
  --space-engine <name>            Select space engine
  --compile <file>                 Emit LLVM IR to stdout
  --list-profiles                  Show available profiles
  --list-space-engines             Show available space engines
  --list-languages                 Show supported languages
```

## Notes

- This repository contains the working runtime snapshot, not a polished
  release branch.
- The fast suites are intentionally strict and cheap.
- Benchmark and probe files that do not yet have `.expected` transcripts may
  still be useful, but they are not counted as golden regressions yet.
