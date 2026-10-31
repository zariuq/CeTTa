# CeTTa

CeTTa is a C runtime for the MeTTa family of languages. One binary reads and
runs several MeTTa dialects over one shared substrate (atoms, spaces,
matching, indexing and memory management), and it hosts native proof kernels
for HOL Light, MM0 and Megalodon.

PeTTa is the default language: corrected stable PeTTa, with its reference,
accepted repairs and differences described in
[the compatibility contract](docs/petta_compatibility.txt). Hyperon
Experimental MeTTa and the other languages are selected with `--lang`.

```bash
make
./cetta program.metta              # PeTTa
./cetta --lang he program.metta    # Hyperon Experimental MeTTa
./cetta -e '!(+ 1 2)'             # one inline PeTTa query
```

Each dialect has its own evaluation and typing rules. Start with PeTTa or HE;
the [execution semantic ledger](docs/native_returns.txt) records the boundaries
that integration tests protect. Checked algorithm laws and native qualification
are separate from formal verification of the C implementation.

## Languages

Each dialect is a language of its own, with its own reference
implementation, and a program is written for one of them.

| `--lang` | Language | Profiles (`--profile`) |
|---|---|---|
| `petta` (default) | Corrected stable PeTTa | `extended` adds CeTTa's labeled extensions; `typecheck-v2` and `typecheck-v3` add opt-in native type checking (`petta@1.1`); `petta@1.1.c` is the typed line with declarations as call contracts |
| `he` | Hyperon Experimental MeTTa 0.2.10, in CeTTa's fork | `he` (default) is the fork with its deliberate differences; `he-compat` keeps the Rust implementation's behavior where `he` differs; `extended` adds CeTTa's labeled extensions; `he-prime` adds dependent binders and typed inhabitation search |
| `prime` | Experimental local Prime adapter, very much a work in progress | Frozen compatibility endpoint `prime@0.0.1` |
| `hol-light` | the HOL Light kernel, checking HOLR proof streams | `standard` (default), `open`, `sealed` |
| `rhocalc` | the reflective higher-order rho-calculus | `strict-core`, `cost`, `rhometta` |
| `mm2` | MM2 programs, lowered directly and run on MORK | `gslt` |

`--lang <name>@<version>` names a contract: `petta@stable-fixed`, `petta@1.1`
(typed PeTTa), `petta@1.1.c`, `he@0.2.10` (the Hyperon Experimental release
CeTTa's HE forks; every HE profile) and `prime@0.0.1`. See
[the language contracts](docs/language_versions.txt). The historical
`petta@1.0.6` selector is retired; old commits retain that reference.

Prime in this tree is very WIP. For active Prime exploration, use
[`godelclaw/CeTTa`, branch `cetta-ours`](https://github.com/godelclaw/CeTTa/tree/cetta-ours),
the version used by the Gödelclaw agents. It has its own development and
qualification history; this tree's frozen Prime endpoint does not track it.

`./cetta --list-languages` lists every language of the binary, including the
experimental generated ones (`zero`, `subzero`, `zerouv`, `gslt-il`,
`metta-interact`), and `./cetta --lang <name> --list-profiles` lists a
language's profiles.

## Requirements

For the default build:

- `gcc` or `clang`, `make` and `pkg-config`
- Python 3 development headers and a working `python3-config --embed`
- GMP development headers for exact big integers; build with
  `make ENABLE_GMP=0` to leave them out

On Debian or Ubuntu:

```bash
sudo apt install build-essential pkg-config python3-dev libgmp-dev
```

Optional:

- SWI-Prolog: when `pkg-config` finds `swipl`, the build links the Prolog
  interoperation library automatically (`ENABLE_LIB_PROLOG=auto`)
- libcurl: `make ENABLE_HTTP=1` enables the HTTP operations
- Rust/Cargo and `MORK` and `PathMap` checkouts, for the bridge builds below

The Prolog library supplies some PeTTa primitives in addition to the native
ones; it is separate from Python support. With `ENABLE_LIB_PROLOG=0`, an
unregistered operation stays uninterpreted. Optimized execution follows the
same availability rule as ordinary execution.

## Build

```bash
make                # ./cetta, with Python support
make BUILD=core     # a smaller binary without Python
```

The build bootstraps the stdlib in two stages. `./cetta --version` prints the
version and build mode of the binary, and `./cetta --help` the full command
line.

When relocating a binary, keep `lib/` beside it or in its parent directory so
explicit imports can find their source libraries. Running from the checkout
also makes those libraries available.

| Mode | Includes | Python |
|------|----------|--------|
| `BUILD=python` or plain `make` | the default build | yes |
| `BUILD=core` | small standalone binary | no |
| `BUILD=main` | the default build plus the MORK/MM2 bridge and `(new-space pathmap)` | yes |
| `BUILD=mork` | the bridge build without Python | no |

`BUILD=full` and `BUILD=pathmap` remain accepted spellings of `BUILD=main` and
`BUILD=mork`.

## First runs

A PeTTa program, here backward chaining over a small graph; each answer is a
proof:

```bash
./cetta examples/chaining/tutorial/01-backward.metta
```

```
()
()
((proof (step ((proof ab (edge alice bob)) (proof (step ((proof bc (edge bob carol)) (proof (direct ((proof cd (edge carol dana)))) (reachable carol dana)))) (reachable bob dana)))) (reachable alice dana)))
()
((proof (direct ((proof ab (edge alice bob)))) (reachable alice bob)))
()
```

The same chainer in HE, and the HE stdlib's higher-order operations:

```bash
./cetta --lang he examples/chaining/tutorial/01-backward.metta
./cetta --lang he tests/test_map_filter_atom.metta
```

```
[(2 3 4 5)]
[(3 4)]
```

A local Prime smoke example (experimental):

```bash
./cetta --lang prime examples/prime/dependent_telescope.metta
```

A HOL Light proof stream, checked by the native kernel. The standard profile
admits only HOL Light's standard axioms, so a stream that adds an axiom is
refused there and accepted under `open`:

```bash
./cetta --lang hol-light tests/hol_light/fixtures/d1-minimal.hrs
./cetta --lang hol-light tests/hol_light/fixtures/dag-new-axiom.hrs  # refusal; exits 1
./cetta --lang hol-light --profile open tests/hol_light/fixtures/dag-new-axiom.hrs
```

```
(thm () (A (A (c "=" ...
(hol:accepted 27 1)
(hol:refused "POLICY" "axiom policy" 137)
(hol:accepted 138 0)
```

A funded reaction in the rho-calculus:

```bash
./cetta --lang rhocalc --profile cost examples/rho/cost/vending_machine.rho
```

`docs/chaining-tutorial.pdf` builds forward and backward chainers in PeTTa and
HE and introduces the directional matching of the extended profiles
(`make test-chaining-tutorial`); `docs/rhocalc.md` is the guided tour of the
rho-calculus.

## Libraries

Import a library with `!(import! &self <name>)`. Libraries may supply dialect
overlays; use examples and contracts for the dialect you selected.

Nonstandard PeTTa libraries require an explicit import. For example,
`(str:starts-with "abc" "a")` stays data until `!(import! &self str)` loads
the string library. `!(import! &self (str json))` loads both libraries in
order. Importing one library does not open unrelated names.

| Library | Provides |
|---|---|
| `str` | qualified string operations |
| `re` | regular expressions with a module-authored syntax: compiled and reusable patterns, ordered captures, look-around, Unicode classes, and scalar tries for literal search (`examples/regex_*.metta`) |
| `fs`, `io` | owned files and directories, streaming text input and output (`docs/filesystem_observations_v1.txt`, `docs/text_preparation_v1.txt`) |
| `json` | JSON reading and writing |
| `holkernel` | the native HOL Light kernel (`docs/hol_light.txt`) |
| `mm0kernel` | the native MM0 verifier for MM0 specifications and MMB proofs (`docs/mm0_native.txt`) |
| `megalodon` | native Megalodon (Mathdata) proof checking |
| `lib_pln`, `lib_wmpln` | Probabilistic Logic Networks |
| `mork` | HE's MORK helper interface (bridge builds) |
| `(library lib_mm2)` | PeTTa's reference MORK operations over `&mork` (bridge builds) |

## Proof kernels

The HOL Light, MM0 and Megalodon kernels are written in C and share CeTTa's
substrate. A program reaches them through their libraries, and the HOL Light
kernel also reads proof streams directly:

```bash
./cetta --lang hol-light proof.holr
```

Kernel sessions, theories and theorems are sealed values. They print as
`(Sealed ...)`, and no printed text, number or data rebuilds one: only the
kernel's checks create a theorem. The documents under `docs/` state what each
kernel checks and what it leaves to the dialect.

## Tests

| Lane | Use | Command |
|---|---|---|
| Semantic contracts | focused source/value, completion and sidecar checks | `make test-execution-semantic-ledger test-petta-corrected-stable` |
| Regular development | representative suite in the current build | `make test` |
| Dialect/profile changes | admission, imports and dialect boundaries | `make test-profiles` |
| Performance changes | focused benchmark correctness and cost checks | `make bench-light` |
| Full correctness | before review or merge | `make test-correctness-all test-profiles` |
| Routine readiness | correctness, sanitizers, capacity and performance regressions | `make main-readiness-routine` |
| Main readiness | before merging to main or releasing | `make main-readiness-exhaustive` |

`make test` runs the checked suite of the current build configuration; on the
bridge builds it includes the bridge regressions. It does not switch `BUILD=`
or start a second build: run `make ENABLE_RUNTIME_STATS=1 test-runtime-stats`
for the runtime-stats lane. Sanitizer lanes use the same build selection:

```bash
make BUILD=core test-asan
make BUILD=python test-tsan
make test-asan-main
```

`make test-petta-release-reference` validates the pinned PeTTa reference and
library metadata. `make test-petta-corrected-stable` checks the current accepted
observations. The [compatibility contract](docs/petta_compatibility.txt) explains
which repairs supersede historical reference behavior.

For changes to foreign callbacks, run:

```bash
make test-python-callback-completion \
     test-petta-libpl-callback-completion \
     test-petta-libpl-callback-source
```

These exercise the installed providers, including interrupted evaluation,
cleanup, nested entry and preservation of completed data. A provider disabled
in the selected build reports an explicit skip; qualify the corresponding
enabled build before accepting a change to that provider.

For changes to conditional answers and delayed goals, run
`make test-conditional-registration test-delay-service test-petta-constraints`.
These checks cover whole-packet validation, wakeup multiplicity and shared
binding-history rollback.

Readiness needs `METTAPEDIA_ROOT` pointing to the Lean project, and the frozen
consolidation baseline's `CETTA_CONSOLIDATION_BASELINE_RECEIPT` and
`CETTA_CONSOLIDATION_BASELINE_SHA256` (the receipt's digest). The exhaustive
tier also needs `PETTA_ORACLE_ROOT`, a pinned checkout or source snapshot;
its source files and hashes are checked before testing. Validate the selected
inputs and inspect the commands first with
`python3 scripts/cetta_main_readiness.py --tier exhaustive --dry-run`.

## Bridge builds (MORK and PathMap)

`BUILD=main` and `BUILD=mork` link `rust/cetta-space-bridge` statically. They
expect `MORK` and `PathMap` checkouts next to CeTTa:

```text
<parent>/
├── CeTTa/
├── MORK/
└── PathMap/
```

Checkouts elsewhere are named directly:

```bash
make doctor-bridge PATHMAP_REPO_DIR=/path/to/PathMap MORK_REPO_DIR=/path/to/MORK
make BUILD=main    PATHMAP_REPO_DIR=/path/to/PathMap MORK_REPO_DIR=/path/to/MORK
```

The expected upstreams are
[Adam-Vandervorst/PathMap](https://github.com/Adam-Vandervorst/PathMap)
`master` and [trueagi-io/MORK](https://github.com/trueagi-io/MORK) `main`. The
bridge builds give `(new-space pathmap)` counted-key storage: keys are
`atom_bytes || count_bytes`, which keeps multiset semantics under PathMap's
structural matching (`specs/pathmap_counted_space_design.txt`). The first
bridge build compiles the Rust crates and can take a few minutes.

## Examples

### Regular expressions (PeTTa)

```metta
!(import! &self re)
!(let* (($pattern (re:compile "(?<greek>λ)"))
        ($match (re:find $pattern "aλb"))
        ($text (re:group-text $match "aλb" "greek"))
        ($span (re:group-span $match 1)))
   (function (return (RegexCaptureTextV1 $text $span))))
```

```
$ ./cetta examples/regex_capture_access_v1.metta
true
(RegexCaptureTextV1 "λ" (re:span 1 3))
...
```

Names and numeric identities address the same UTF-8 byte spans. The other
`examples/regex_*.metta` files cover flags, classes, look-around,
backreferences, Unicode and relations over matches.

### Proof-carrying backward chaining (HE)

A typed backward chainer whose answers are proof terms:

```metta
!(add-atom &kb (: r-mortal (-> (: $r (researcher $x))
                               (: $c (curious $x))
                               (mortal $x))))
!(bc &kb (fromNumber 1) (: $prf (mortal $x)))
```

```
$ ./cetta --lang he benchmarks/bench_backchain_heavy_nilbc.metta | tail -1
[(: (r-mortal r01 r01c) (mortal r01)), (: (r-mortal r02 r02c) (mortal r02)), ...]
```

`(: (r-mortal r01 r01c) (mortal r01))` means that r01 is mortal, using
researcher proof r01 and curiosity proof r01c.

### Breadth-first search over 181,440 states (HE)

```
$ ./cetta --lang he --profile extended benchmarks/test_tilepuzzle.metta
8-puzzle BFS: reachable states from solved position (including the start state)...
[()]
[()]
[(tilepuzzle-reachable-state-count including-start 181440)]
```

A queue space holds the frontier and a hash space the visited states. This
is a larger example, rather than a quick installation check; its cost depends
on the build, space representation and machine.

### A 1.4M-atom genomic workload (HE)

```
$ ./cetta --lang he benchmarks/bench_bio_1M.metta
=== Phase 1: Direct targets of rs10000544 (depth 1, 1.4M atoms) ===
=== Phase 2: Diseases from rs10000544 (depth 2) ===
=== Phase 3: TF regulatory targets of rs10000544 (depth 2) ===
...
```

This workload loads its input data before querying it. Keep the complete
output when checking results; a printed phase heading alone does not establish
that a query completed. See the benchmark's input paths before running it.

## Repository map

- `src/`: the runtime: atoms and symbols (`atom.c`, `symbol.c`), matching and
  bindings (`match.c`, `binding/`), spaces (`space.c`), shared invocation and
  dialect entrypoints (`eval.c`), PeTTa search and plans (`petta_*.c`), grounded operations
  (`grounded.c`), readers (`parser.c` and the compiled dialect readers), the
  term universe (`term_universe.c`) and the CLI (`main.c`)
- `native/`: the native modules, among them the HOL Light, MM0 and Megalodon
  kernels
- `lib/`: the libraries named above, the stdlib (`stdlib.metta`) and the
  per-dialect overlays (`lib/he`, `lib/petta`, `lib/prime`)
- `langdef/`: language definitions compiled into readers and checkers
- `rust/`: the MORK/PathMap bridge crates
- `docs/`, `specs/`: kernel contracts, language versions and designs
- `examples/`, `tests/`, `benchmarks/`: runnable programs, the checked suite
  and workloads

## Working on CeTTa

Start with the contract for the dialect you are changing:
[PeTTa compatibility](docs/petta_compatibility.txt),
[language versions](docs/language_versions.txt), and the shared
[execution and ownership contract](docs/native_returns.txt). A passing program
in one dialect does not establish another dialect's behavior.

`tests/test_manifest.tsv` records each fixture's dialect, profile, build and
expected-observation policy. Add a small regression with an independent
expected result, then run its affected Makefile target and the current build's
regular suite. Use the broader lanes in the Tests table as the change requires.
Preserve duplicate answers, variable sharing and declared ordering; an equal answer
count alone is insufficient. Run the full readiness campaign on a frozen
candidate before release.

Generated code lives under `src/generated/` and `runtime/generated/`. Change
its owning input or generator under `lib/`, `langdef/` or `tools/`, and let the
Makefile regenerate its outputs. Runtime optimizations should reuse the common
plans, bindings and tagged call outcomes; the execution contract identifies
their ownership and fallback rules.

## Command line

```text
./cetta [--lang <name>[@<version>]] [--profile <name>] <file>
./cetta -e '<expr>'                  inline expressions
./cetta --translate --lang A --lang B <file>
./cetta --lang <name> --list-profiles
./cetta --list-languages
./cetta --count-only <file>          result counts only
./cetta --quiet <file>               hide [()] success results
./cetta --fuel <-1|n> <file>         evaluation budget, positive n or -1 unlimited
./cetta --num-threads <n> <file>     OS threads for parallel-capable execution
./cetta --space-engine <name> <file> select the space engine
./cetta --compile <file.metta>       emit LLVM IR
./cetta --run-report <path> <file>   query report as JSON
```

Without `--lang`, `cetta` runs `petta`. `--lang petta` reads consecutive
`.metta` files in order; the other languages run one file and pass the
remaining arguments to the program (`system:args`). `./cetta --help` lists
every option.
