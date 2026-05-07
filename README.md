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
- local `MORK` and `PathMap` checkouts; sibling layout works by default, and
  `PATHMAP_REPO_DIR` / `MORK_REPO_DIR` overrides are supported below

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

Run `./cetta -v` (or `./cetta --version`) to see the exact version and active
build mode of the binary currently sitting at `./cetta`.

If you want the smaller no-Python binary instead:

```bash
make BUILD=core
```

## Build Modes

Default modes:

| Mode | Includes | Python |
|------|----------|--------|
| `BUILD=python` or plain `make` | default HE-style evaluator | yes |
| `BUILD=core` | small standalone binary | no |

Bridge-capable modes:

| Mode | Includes | Python | When to use |
|------|----------|--------|-------------|
| `BUILD=main` | `lib_mork` / MM2 bridge + generic `(new-space pathmap)` | yes | recommended bridge daily-driver |
| `BUILD=full` | compatibility alias of `BUILD=main` | yes | accepted old spelling for the same bridge build |
| `BUILD=mork` | same bridge surface as `BUILD=main`, without Python | no | recommended smaller no-Python bridge binary |
| `BUILD=pathmap` | compatibility alias of `BUILD=mork` | no | accepted old spelling for the same no-Python bridge build |

## Bridge Setup

The bridge-enabled modes build `rust/cetta-space-bridge` and link it
statically into CeTTa. They are optional; you do not need them for the default
HE-style build above.

Default checkout layout:

```text
<parent>/
├── CeTTa/
├── MORK/
└── PathMap/
```

CeTTa's optional bridge build currently follows the same sibling-repo layout
that MORK itself expects.

Current bridge branch expectations:

- PathMap: [Adam-Vandervorst/PathMap](https://github.com/Adam-Vandervorst/PathMap) `master`
- MORK: [trueagi-io/MORK](https://github.com/trueagi-io/MORK) `main`

The checked bridge lane vendors its tiny MM2 regression inputs under
`tests/support/mork_mm2/`, so it no longer depends on sibling
`MORK/examples/...` paths staying stable across upstream reorganizations.

Treat `BUILD=main` and `BUILD=mork` as the canonical bridge builds. `BUILD=full`
and `BUILD=pathmap` remain accepted compatibility aliases, but they no longer
name different feature sets.

If your `MORK` and `PathMap` checkouts already live as siblings next to
`CeTTa`, the standard bridge commands should work without extra flags:

```bash
ulimit -v 10485760 && make BUILD=main
ulimit -v 10485760 && make BUILD=mork
```

If your checkouts live elsewhere, you do not need symlinks. Pass the repo
locations directly:

```bash
make doctor-bridge \
  PATHMAP_REPO_DIR=/path/to/PathMap \
  MORK_REPO_DIR=/path/to/MORK
make BUILD=main \
  PATHMAP_REPO_DIR=/path/to/PathMap \
  MORK_REPO_DIR=/path/to/MORK
make BUILD=mork \
  PATHMAP_REPO_DIR=/path/to/PathMap \
  MORK_REPO_DIR=/path/to/MORK
```

The top-level `Makefile` uses those overrides both for dependency discovery and
for generating a bridge workspace manifest with the resolved Rust paths, so the
bridge build no longer requires a symlinked sibling mirror just to satisfy
Cargo path dependencies.

If you need MM2 arithmetic sink examples (`i+`, `i-`, `i*`, `f+`, `f-`, `f*`)
today, use the MORK branch
[`feature/arithsinks-pr-sync`](https://github.com/zariuq/MORK/tree/feature/arithsinks-pr-sync)
instead.

The first bridge build can take a few minutes on a cold Cargo cache.

For a longer bridge/MM2 walkthrough, see `MORK_TUTORIAL.md`.

Build the smaller no-Python bridge variants:

```bash
ulimit -v 10485760 && make BUILD=mork
ulimit -v 10485760 && make BUILD=pathmap   # compatibility alias
```

All bridge-enabled builds (`BUILD=main`, `BUILD=full`, `BUILD=mork`, and
`BUILD=pathmap`) enable `(new-space pathmap)` with counted-key storage. This
stores `atom_bytes || count_bytes` as PathMap keys, preserving multiset
semantics while still benefiting from PathMap's structural matching. See
`specs/pathmap_counted_space_design.txt` for the full design.

## Play Here First

If you want one successful first run in each major mode, start here after
choosing and building the mode you want:

### Default HE lane

```bash
make
./cetta tests/test_map_filter_atom.metta
```

### Python + bridge build

```bash
make BUILD=main
./cetta examples/mork_showcase.metta
```

### PathMap counted-space surface

```bash
make BUILD=main
./cetta tests/test_pathmap_counted_space_surface.metta
```

## Verified Test Commands

Run the ordinary checked suite for the current build:

```bash
make test
```

Run the profile surface suite:

```bash
make test-profiles
```

Run the explicit runtime-stats suite for the current build:

```bash
make ENABLE_RUNTIME_STATS=1 test-runtime-stats
```

`make test` now stays inside the current build configuration. It runs the fast
HE/golden suite, and on bridge-enabled builds it also runs the matching current
bridge regressions:

- `BUILD=main` / `BUILD=full`: MORK/MM2 bridge core lane plus pathmap-space lane
- `BUILD=mork` / `BUILD=pathmap`: the same regression set without Python

It does not silently switch `BUILD=` and it does not silently trigger a second
runtime-stats build. Use `test-runtime-stats` explicitly when you want the
compile-time runtime-stats lane.

The checked default lanes vendor their tiny Metamath parser fixtures under
`tests/support/`, so they do not require a separate Metamath checkout either.

For a full checked sweep of a bridge build, use the same `BUILD=` consistently:

```bash
make BUILD=main test
make BUILD=main test-profiles
make BUILD=main ENABLE_RUNTIME_STATS=1 test-runtime-stats

make BUILD=mork test
make BUILD=mork test-profiles
make BUILD=mork ENABLE_RUNTIME_STATS=1 test-runtime-stats
```

Promote the current checked binary to the local stable runtime path:

```bash
make promote-runtime
make BUILD=main promote-runtime
```

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

Recent verification in a clean `origin/main` clone produced:

- `make test`: main HE/golden sweep `335 passed, 0 failed, 38 skipped`
- `make test-profiles`: `77 passed, 0 failed`
- Explicit bridge lanes passed for `BUILD=mork`, `BUILD=main`,
  and their compatibility aliases `BUILD=pathmap` and `BUILD=full`
- Explicit runtime-stats lane bodies also passed for the checked bridge builds

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

This preview uses a tracked workload in `benchmarks/`. A full run on a
development workstation produces approximately 1,411,430 hypotheses in 3m 22s
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
a longer-running example; prefer the smaller files below for a quick smoke
test.

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
make BUILD=main
./cetta examples/mork_showcase.metta
```

Optional counted PathMap space surface:

```bash
make BUILD=main
./cetta tests/test_pathmap_counted_space_surface.metta
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
