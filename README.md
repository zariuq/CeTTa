# CeTTa

CeTTa is a direct C runtime for [MeTTa](https://metta-lang.dev/). This branch
is the current term-sharing snapshot we can share honestly with Hyperon
developers: it builds, the core golden suite is green, the profile/runtime
surface is real, and the branch is still under active development.

The intended default lane is now:

- `--lang he` for the current HE-style evaluator/driver
- `--profile he_extended` as the default working surface
- `make` as the Python-enabled build that is closest to everyday Hyperon usage

## Build

CeTTa builds with:

- `gcc`
- `make`
- Python 3 development headers (`python3-config --embed` must work)

The normal build bootstraps the stdlib automatically in two stages and now
defaults to the Python-enabled binary:

```bash
make
```

That produces `./cetta`.

If you want the smaller no-Python binary explicitly:

```bash
make BUILD=core
```

If you want the local static MORK bridge (lib_mork / MM2 lane):

```bash
make BUILD=mork     # no Python
make BUILD=main     # with Python
```

If you want generic `(new-space pathmap)` with multiset semantics:

```bash
make BUILD=pathmap  # no Python
make BUILD=full     # with Python
```

### MORK Bridge Prerequisites

The `BUILD=mork`, `BUILD=main`, `BUILD=pathmap`, and `BUILD=full` modes build the
bridge from CeTTa's own Rust workspace (`rust/`). This requires MORK and PathMap
checked out as siblings:

| Repo | Source | Notes |
|------|--------|-------|
| **PathMap** | [Adam-Vandervorst/PathMap](https://github.com/Adam-Vandervorst/PathMap) `master` | Mainline works |
| **MORK** | [zariuq/MORK](https://github.com/zariuq/MORK) | See branch options below |

Clone or checkout at:
- `hyperon/PathMap/` (relative to the claude workspace root)
- `hyperon/MORK/` (relative to the claude workspace root)

**Build modes and MORK branches:**

| Build Mode | Purpose | MORK Branch Needed |
|------------|---------|-------------------|
| `BUILD=mork` | lib_mork / MM2 stepping (set semantics) | `cetta/query-multi-factor-exprs` |
| `BUILD=main` | above + Python | `cetta/query-multi-factor-exprs` |
| `BUILD=pathmap` | generic `(new-space pathmap)` with multiset semantics | `cetta/query-multi-factor-exprs` |
| `BUILD=full` | above + Python | `cetta/query-multi-factor-exprs` |

Once `query-multi-factor-exprs` merges upstream, mainline MORK works for all modes.

**Arithmetic sink examples**: If you want MM2 arithmetic sink examples working
today (`i+`, `i-`, `i*`, `f+`, `f-`, `f*`), use the
[`feature/arithsinks-pr-sync`](https://github.com/zariuq/MORK/tree/feature/arithsinks-pr-sync)
branch instead.

**Build with the MORK bridge (lib_mork / MM2):**

```bash
cd c-projects/CeTTa
ulimit -v 10485760 && make BUILD=mork

# smoke tests (basic MM2)
./cetta examples/mork_showcase.metta
./cetta examples/mork_mm2_showcase.metta

# smoke test (arithmetic sinks - requires arithsinks branch)
./cetta examples/mork_intarith_showcase.metta
```

**Build with counted-key PathMap spaces (multiset semantics):**

```bash
cd c-projects/CeTTa
ulimit -v 10485760 && make BUILD=pathmap

# multiset semantics test
./cetta tests/test_pathmap_counted_space_surface.metta
```

The `BUILD=pathmap` and `BUILD=full` modes enable `(new-space pathmap)` with
counted-key storage. This stores `atom_bytes || count_bytes` as PathMap keys,
preserving multiset semantics (duplicates matter) while benefiting from PathMap's
prefix-based structural matching. See `specs/pathmap_counted_space_design.txt`
for the full design.

The CeTTa-owned bridge includes two compatibility adapters:
- `rust/cetta-pathmap-adapter/`: PathMap compatibility (OverlayZipper, snapshot helpers)
- `rust/cetta-mork-adapter/`: MORK compatibility (factor expression query wrapper)

Both adapters are thin forwarding layers with zero overhead.

## Quick Start

Run a small file:

```bash
./cetta tests/test_map_filter_atom.metta
```

Run the main checked suite:

```bash
make test
```

Run the profile surface suite:

```bash
make test-profiles
```

Promote the current checked binary to the local stable runtime path:

```bash
make promote-runtime
```

## What Works In This Snapshot

- Core HE-style evaluation in C
- Arena-allocated atoms with hash-consing support
- Multiple space kinds and pluggable space engines
- SymbolId-based core dispatch instead of pervasive string comparison
- Runtime counters plus `--profile`-aware surface guards
- Explicit `TermUniverse` / persistent term-store seam
- Variant tabling infrastructure with shared canonicalization substrate
- Optional `pathmap` engine for ordinary MeTTa over PathMap (multiset semantics via counted-key storage)
- Explicit `mork:` helper surface for the MORK/MM2 execution lane (set semantics)
- Local git-module and module-inventory surfaces
- Python foreign-module support in the default build

## Optional MORK / PathMap Bridge

The MORK bridge is built from CeTTa's own Rust workspace at `rust/`. Running
`make BUILD=mork`, `BUILD=pathmap`, or `BUILD=full` builds
`rust/target/release/libcetta_space_bridge.a` and links it statically. Non-MORK
builds can still load a bridge dynamically at runtime.

**lib_mork lane** (MM2 stepping, set semantics):

```bash
./cetta examples/mork_showcase.metta
./cetta examples/mork_mm2_showcase.metta
```

**Counted PathMap spaces** (MeTTa multiset semantics, `BUILD=pathmap` or `BUILD=full`):

```bash
./cetta --profile he_extended --space-engine pathmap \
  tests/test_pathmap_imported_bridge_v2.metta

# Or explicitly create a pathmap space:
./cetta -e '!(bind! &s (new-space pathmap))' \
        -e '!(add-atom &s x)' \
        -e '!(add-atom &s x)' \
        -e '!(match &s x hit)'
# → [hit, hit]  (multiset: two copies, two results)
```

## Test Status

At the current snapshot:

- `make test` -> `332 passed, 0 failed, 47 skipped`
- `make test-profiles` -> `74 passed, 0 failed`

Those `47 skipped` are not hidden failures. They currently break down into:

- `1` dedicated pretty-vars surface test
- `2` pathmap-engine regression lanes
- `17` tests covered by dedicated MM2/MORK suites
- `27` files that are still probe- or translation-shaped and do not yet have
  `.expected` goldens in the fast suite

If you deliberately build the smaller no-Python lane with `BUILD=core`, the
same suite currently reports `328 passed, 0 failed, 51 skipped`.

So the raw harness number is conservative. The branch is in active development,
but the checked surfaces are genuinely green.

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

On our development machine, the full run produces approximately 1,411,430
hypotheses in 3m 22s across 17 query phases.

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
./cetta tests/bench_backchain_heavy_nilbc.metta | tail -1
./cetta tests/test_runtime_stats_surface.metta
```

Python-facing surface in the default build:

```bash
./cetta tests/test_py_ops_surface.metta
```

Optional MORK-facing surface:

```bash
./cetta tests/test_mork_lib_surface.metta
```

Full tile puzzle:

```bash
./cetta --profile he_extended tests/test_tilepuzzle.metta
```

Large genomic benchmark:

```bash
timeout 600 ./cetta tests/bench_bio_1M.metta
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
