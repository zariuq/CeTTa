# CeTTa

CeTTa is a C runtime for the MeTTa family of languages. One binary reads and
runs several MeTTa dialects over one shared substrate (atoms, spaces,
matching, indexing and memory management), and it hosts native proof kernels
for HOL Light, MM0 and Megalodon.

PeTTa is the default language. Its reference is
[corrected stable PeTTa](https://github.com/godelclaw/PeTTa/tree/stable-fixed),
preserving the stable language's typing policy.
Hyperon Experimental MeTTa and the other languages are selected with `--lang`.

```bash
make
./cetta program.metta              # PeTTa
./cetta --lang he program.metta    # Hyperon Experimental MeTTa
./cetta -e '!(+ 1 2)'             # one inline PeTTa query
```

This pre-main branch is a development preview for experimentation and
contributions. Each dialect has its own evaluation and typing rules.
Start with PeTTa or HE. The [execution semantic ledger](docs/native_returns.txt)
records the boundaries
that integration tests protect. Checked algorithm laws and native qualification
are separate from formal verification of the C implementation.

## Python embedding (development)

This branch includes a native Python package and a reusable C embedding API.
Source installation needs CPython 3.11–3.15 with development headers and venv
support, a C11 compiler, GNU Make 4.3 or newer and GMP development headers.
From the repository root:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install .
```

Use `python -m pip install '.[notebooks]'` for NumPy and Jupyter support.

```python
from cetta import Engine

with Engine() as engine:              # PeTTa (default)
    engine.run('(= (double $x) (* $x 2))')
    assert engine.call('double', 21).to_python() == [42]

with Engine(language="he") as he:     # Hyperon Experimental MeTTa
    he.run('(= (inc $x) (+ $x 1))')
    assert he.call('inc', 41).to_python() == [42]
```

Calls accept values; `eval` executes source. Use `load(path)` for source files
and `import_(spec)` for MeTTa's `import!` operation. Answers retain their storage
after the engine closes. PeTTa is the default embedding language; pass
`language="he"` for Hyperon Experimental MeTTa. Query streams are closable and
support optional stepping; spaces expose facts and scoped bindings.
The embedding runtime does not require SWI-Prolog, Rust or C++. Explicit callback
descriptors support Python functions and generators. Dense NumPy arrays,
including foreign byte orders, use retained read-only views and explicit copy
controls. Sparse COO/CSR/CSC component views preserve absent coordinates and
stored zeros. Linux packages target CPython 3.11–3.15 on glibc and musl,
for x86_64 and aarch64. The guide records the tested checkpoint and distinguishes
native from emulated platform checks; physical ARM64 remains unverified.

For C, C++ or Rust hosts, install the standalone C ABI SDK
(`include/cetta_embed.h`, `libcetta.so.1`, `libcetta.a` and `cetta.pc`):

```bash
make install-cabi PREFIX="$HOME/.local"
PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig" pkg-config --cflags --libs cetta
```

See the [embedding guide](docs/python_embedding.txt)
and [introductory notebook](examples/python/getting_started.ipynb).

## Languages

Each dialect is a language of its own, with its own reference
implementation, and a program is written for one of them.

| `--lang` | Language | Profiles (`--profile`) |
|---|---|---|
| `petta` (default) | Corrected stable PeTTa | `extended` adds CeTTa's labeled extensions; `typecheck-v2` and `typecheck-v3` add native type checking (`petta@1.1`); `petta@1.1.c` is the typed line with declarations as call contracts |
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
which has its own development and
qualification history; this tree's frozen Prime endpoint does not track it.

`./cetta --list-languages` lists every language of the binary, including the
experimental generated ones (`zero`, `subzero`, `zerouv`, `gslt-il`,
`metta-interact`), and `./cetta --lang <name> --list-profiles` lists a
language's profiles.
This branch also carries a working draft of **MeTTa Prime** (`--lang prime`),
a higher-order MeTTa in which code is a value, programs can be checked by a
native dependent type kernel, and proofs are terms that the kernel checks.
The draft is where Prime's semantics is being settled against its formal
metatheory, so its interfaces may still change. See
[MeTTa Prime draft](#metta-prime-draft---lang-prime) below.

The `godelclaw/CeTTa` branch `cetta-ours` combines Prime with the native
process, timer, mailbox, memory and Telegram libraries used by
[GödelClaw](https://github.com/godelclaw/godelclaw). GödelClaw pins its engine
revision in `CETTA_REF`; its cognitive loops use `--lang petta`, while Prime
programs select `--lang prime`. See the
[integration changes and qualification results](verification/agent-trial/upstream-review.txt).
The integration adds no automatic tool admission rule or model budget.

## Requirements

For the default build:

- `gcc` or `clang`, GNU Make 4.3 or newer and `pkg-config`
- Python 3 development headers and a working `python3-config --embed`
- GMP development headers for exact big integers; build with
  `make ENABLE_GMP=0` to leave them out

On Debian or Ubuntu:

```bash
sudo apt install build-essential pkg-config python3-dev python3-venv libgmp-dev
```

Optional:

- SWI-Prolog: when `pkg-config` finds `swipl`, the build links the Prolog
  interoperation library automatically (`ENABLE_LIB_PROLOG=auto`)
- libcurl: `make ENABLE_HTTP=1` enables the HTTP operations
- Native Sign in with ChatGPT: `ENABLE_HTTP=1 ENABLE_SIWC=1` with cJOSE,
  Jansson and OpenSSL; see [native sign-in](docs/siwc/README.md)
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

## Optional Host Interfaces

These features are selected at build time. They do not require a separate
CeTTa checkout or a different source branch.

| Interface | Build selection | Guide |
| --- | --- | --- |
| Native HTTP and asynchronous I/O | `ENABLE_HTTP=1` (libcurl) | [I/O boundary](docs/io/README.md) |
| Native Sign in with ChatGPT | `ENABLE_HTTP=1 ENABLE_SIWC=1` (cJOSE/Jansson/OpenSSL) | [Sign-in, sessions and qualification](docs/siwc/README.md) |
| Buffered LLM protocol library | `lib/llm.metta`; native HTTP for submission | [Requests, ordered results and ownership](verification/llm/README.md) |
| Durable named spaces and host transactions | `ENABLE_DURABLE=1` (SQLite) | [Durable host](docs/durable/README.md) |
| Process helper library | `lib/proc.metta` | [Process module](lib/proc.metta) |
| Shell helper library | `lib/shell.metta` | [Shell module](lib/shell.metta) |
| Telegram transport and action service | `telegram-service` target with HTTP and durable features | [Durable host](docs/durable/README.md), [unit templates](deploy/systemd/) |
| Managed spellings and registered-name reclamation | `ENABLE_MANAGED_SYMBOLS=1` | [Symbol lifetime experiment](verification/gc/README.md) |
| Native memory collection, packed cosine search and journaled space owner | `space-service value-worker` targets | [Local setup and qualification](docs/memory/README.md) |

For example, a Python-enabled CLI and a smaller service executable can be
built from one checkout:

```bash
make BUILD=python ENABLE_HTTP=1 ENABLE_DURABLE=1 \
  ENABLE_JSON_GSLT=1 ENABLE_MANAGED_SYMBOLS=1 cetta
make BUILD=core ENABLE_HTTP=1 ENABLE_DURABLE=1 \
  ENABLE_JSON_GSLT=1 ENABLE_MANAGED_SYMBOLS=1 telegram-service
```

The service is a separate executable so it need not link Python; both builds
use the same source. Managed symbols are still an experimental memory feature,
not a complete collector for every legacy numeric symbol ID.

## JSON ingestion

`lib/json.metta` uses a build-time predictive parser for the authored
RFC-8259 grammar. Regular lexical spans use generated DFA tables; container
actions construct values while reading the input. Ordinary parsing and
stringifying do not materialize forests, render certificates, hash a parse
tree, or reparse emitted output.

Canonical `json:*` values preserve exact number text, ordered duplicate
members, Unicode scalars, occurrence IDs and source spans. Historical
`json-parse` values retain their compatibility representation, including NUL
bytes. Parse failures report a byte position and expectation, and preserve
the caller's arena and output. Parsing, value conversion, Boolean respelling
and writing use explicit continuations, with no default nesting cap. C callers
can request explicit work, depth and byte budgets independently of the grammar.

`json:parse-reference`, `json:parse-legacy-reference` and
`json:stringify-reference` explicitly select the prepared GLL reference.
`json:capabilities` identifies the default as `PredictiveDfaV1`; its remaining
digests describe that reference. Diagnostic preparation is lazy.

The compiler reads the lowered syntax, lexical profile, occurrence-value
language and elaboration plan. It checks semantic layouts and FIRST/FOLLOW
conflicts after lexical folding and erasing value-inert whitespace. It
supports this JSON elaboration fragment and refuses unsupported changes;
it is not a general parser generator with GLL fallback islands.

The C entry `cetta_json_direct_v1_lines` consumes JSONL one row at a time.
Its callback borrows the row's value until returning; retaining it requires
copying it. Completed rows remain delivered if a subsequent row fails.

Run `make test-json-direct-v1` for differential, malformed-input, concurrency,
streaming and generator-conflict checks. The qualification binary also
accepts `--bench FILE`, `--check FILE`, `--corpus DIRECTORY` and
`--corpus-fast DIRECTORY`. Reference resource exhaustion is reported
separately in corpus checks; complete-file agreement requires every row.

## Bridge Setup
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

## Tutorials and guides

Run commands from the repository root. Each guide names its dialect and any
required profile or optional build. Start with the Python notebook or chaining
tutorial for PeTTa and HE.

| Topic | Start here |
|---|---|
| Python getting-started notebook | [`examples/python/getting_started.ipynb`](examples/python/getting_started.ipynb) |
| Python & C ABI embedding guide | [`docs/python_embedding.txt`](docs/python_embedding.txt) |
| Forward & backward chaining (PeTTa and HE) | [`docs/chaining-tutorial.pdf`](docs/chaining-tutorial.pdf) and [`examples/chaining/tutorial/`](examples/chaining/tutorial/) (`make test-chaining-tutorial`) |
| Rho-calculus and resource-accounted reactions | [`docs/rhocalc.md`](docs/rhocalc.md) and [`examples/rho/`](examples/rho/) (`make test-rho-examples`) |
| MORK, MM2 and PathMap spaces | [`MORK_TUTORIAL.md`](MORK_TUTORIAL.md) (bridge build; HE examples) |
| Packed dense/sparse tensors & certified bounds | [`docs/tensor_support_v1.txt`](docs/tensor_support_v1.txt) |
| Random-closure & evidence-ledger diagnosis | [`examples/petta/service_dependency_diagnosis_showcase.metta`](examples/petta/service_dependency_diagnosis_showcase.metta) |
| PeTTa compatibility and language contracts | [`docs/petta_compatibility.txt`](docs/petta_compatibility.txt) and [`docs/language_versions.txt`](docs/language_versions.txt) |

[`SHOWCASE.md`](SHOWCASE.md) is a historical HE typing experiment, separate
from these getting-started guides.

## Libraries

Import a library with `!(import! &self <name>)`, or
`!(import! &self (library <name>))` for a packaged module. Libraries may supply dialect
overlays; use examples and contracts for the dialect you selected.

Nonstandard PeTTa libraries require an explicit import. For example,
`(str:starts-with "abc" "a")` stays data until `!(import! &self str)` loads
the string library. `!(import! &self (str json))` loads both libraries in
order. Importing one library does not open unrelated names.

Packaged PLN libraries use an explicit module specification in PeTTa:

```metta
!(import! &self (library lib_pln))
```

In HE, use `!(import! &self lib_pln)`. Follow the import form shown by the
example for the selected dialect.

| Library | Provides |
|---|---|
| `str` | qualified string operations |
| `re` | regular expressions with a module-authored syntax: compiled and reusable patterns, ordered captures, look-around, Unicode classes, and scalar tries for literal search (`examples/regex_*.metta`) |
| `fs`, `io` | owned files and directories, streaming text input and output (`docs/filesystem_observations_v1.txt`, `docs/text_preparation_v1.txt`) |
| `json` | JSON reading and writing |
| `math` | native numerical operations, including `math:exp`, `math:ln` and base-specific `math:log` (`lib/math.metta`) |
| `(library tensor)` | packed dense and support-aware sparse (`coo`, `csr`, `csc`) tensors, NumPy/C buffer views, retrieval indices, and opt-in `tensor:dot-bounds` / `tensor:matvec-bounds` error enclosures ([contract](docs/tensor_support_v1.txt)) |
| `holkernel` | the native HOL Light kernel (`docs/hol_light.txt`) |
| `mm0kernel` | the native MM0 verifier for MM0 specifications and MMB proofs (`docs/mm0_native.txt`) |
| `megalodon` | native Megalodon (Mathdata) proof checking |
| `lib_pln`, `lib_wmpln` | Probabilistic Logic Networks |
| `lib_random_closure`, `lib_evidence_ledger` | exact rational inference and numerical enclosures for declared probabilistic models; evidence identity and retraction ([PeTTa example](examples/petta/service_dependency_diagnosis_showcase.metta)) |
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
| Candidate readiness | bounded PeTTa/HE/Python checks, reusing unchanged passing evidence | `make main-readiness` |
| Full correctness | broad correctness and dialect audit | `make test-correctness-all test-profiles` |
| Broad readiness | correctness, sanitizers, capacity and performance regressions | `make main-readiness-routine` |
| Exhaustive qualification | frozen release milestones and major runtime changes | `make main-readiness-exhaustive` |

`make main-readiness` has a 20-minute total budget, including incremental builds.
It stops at the first failure and records passing checks individually. Repeating
the command reuses those results when source, configuration and executable
identities still match. A timeout reports **incomplete**, never a pass. It does
not build wheels or start the exhaustive sanitizer and performance campaigns.
The [readiness workflow](docs/main_readiness.txt) explains focused fixes,
evidence reuse and the stronger release qualification boundary.

`make test` runs the checked suite of the current build configuration; on the
bridge builds it includes the bridge regressions. It does not switch `BUILD=`
or start a second build: run `make ENABLE_RUNTIME_STATS=1 test-runtime-stats`
for the runtime-stats lane. Sanitizer lanes use the same build selection:

```bash
make BUILD=core test-asan
make BUILD=python test-tsan
make test-asan-main
```

`make test-petta-release-reference` checks the pinned corrected stable libraries
and selected default observations. The source revision and file hashes are in
`langdef/petta/reference.json`; this check does not claim complete conformance.
`python3 tools/check_petta_stable_corpus.py --reference <PeTTa-checkout> --cetta ./cetta`
records the original corpus and independently framed observations. Raw disagreements
are diagnostic evidence, not a conformance score. `make test-petta-corrected-stable`
checks the selected settled rules. Classifications and open questions are described in
[the compatibility notes](docs/petta_compatibility.txt).

The `Runtime portability` workflow builds clean source archives in Ubuntu
24.04/26.04, Fedora and Alpine/musl. All four Linux entries are required.
It exercises the Python/HTTP/SQLite build, process ownership and descriptor
isolation, native space-service tests and mailbox crash/replay in PeTTa and
Prime; optional Prolog and subscription sign-in are disabled in this matrix.
The musl process adapter shares fork/exec child setup with the I/O provider;
glibc retains its spawn implementation. When the kernel's descriptor-range
operation is unavailable, the child inspects its own descriptor table and
refuses execution if isolation cannot be established. `make test-proc-child`
checks both paths, including descriptors beyond a lowered file limit.
Main-thread stack bounds account for Linux's growable stack mapping; fixed
pthread stacks retain their allocation bounds. Evaluation keeps its stack guard
and service reserve on both glibc and musl.
`tools/ci_linux_portability.sh` holds the shared container check. A manual workflow
run with `native_probes=true` selects experimental macOS and Windows/MSYS2 core
ownership probes, without repeating the Linux matrix already run on pushes.
Those probes do not establish full agent or service support on either OS.
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

The broad and exhaustive tiers need `METTAPEDIA_ROOT` pointing to the Lean project, and the frozen
consolidation baseline's `CETTA_CONSOLIDATION_BASELINE_RECEIPT` and
`CETTA_CONSOLIDATION_BASELINE_SHA256` (the receipt's digest). First run
`make test-execution-theory` to build the maintained execution-model audit and
record its selected source/object closure. Readiness checks that receipt before
freezing, records the theory and live execution gates inside the campaign, and
rejects reuse after a relevant model or object changes. The exhaustive
tier also needs `PETTA_ORACLE_ROOT`, a pinned checkout or source snapshot;
its source files and hashes are checked before testing. Historical performance
gates retain `9050a2d`. Set `CETTA_PERF_BASELINE_ROOT` to a clean checkout or a
frozen artifact directory containing `baseline.json` and `baseline-stats.json`.
The latter validates both builds against their source archive and runtime
library, so the original checkout can be removed. Validate the selected
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

CeTTa's optional bridge build currently follows the same sibling-repo layout
that MORK itself expects.

The bridge is qualified against these upstream revisions:

- PathMap 0.4.0: [`cb4e1e4`](https://github.com/Adam-Vandervorst/PathMap/commit/cb4e1e4eecb75f0be9846b4b75c69d5e2d2f647b) on `master`.
- MORK: [`ed57c671`](https://github.com/trueagi-io/MORK/commit/ed57c6716d8c510296fb5fbb8be6fbfe2df241d7) on `main`.

Cargo uses the checked-in lockfile. Upstream Rust source changes invalidate
the bridge build. CeTTa's overlay adapter uses PathMap's public zipper traits;
its traversal controls check branches formed across both source tries. MORK
integration uses the public expression environment and binding APIs. Both
upstream repositories remain unmodified.

The checked bridge lane vendors its tiny MM2 regression inputs under
`tests/support/mork_mm2/`, so it no longer depends on sibling
`MORK/examples/...` paths staying stable across upstream reorganizations.

Treat `BUILD=main` and `BUILD=mork` as the canonical bridge builds. `BUILD=full`
and `BUILD=pathmap` remain accepted compatibility aliases, but they no longer
name different feature sets.

If your `MORK` and `PathMap` checkouts already live as siblings next to
`CeTTa`, the standard bridge commands should work without extra flags:
Checkouts elsewhere are named directly:

```bash
make doctor-bridge PATHMAP_REPO_DIR=/path/to/PathMap MORK_REPO_DIR=/path/to/MORK
make BUILD=main    PATHMAP_REPO_DIR=/path/to/PathMap MORK_REPO_DIR=/path/to/MORK
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

Run the Rust tests and the ordinary C integration gates:

```bash
make BUILD=mork test-rust-bridge
make BUILD=mork test-mork-lane test-pathmap-lane
```

For clients loading the bridge with `dlopen`, build the separate loadable
profile, which configures jemalloc without initial-exec TLS:

```bash
make BUILD=mork bridge-shared
make BUILD=mork test-mork-shared-bridge
```

`bridge-shared` prints the resulting library path. Static builds retain their
existing allocator configuration. Returned packets own their bytes until
`mork_bytes_free`; they can outlive their source space. Query callback bindings
are borrowed only for that callback. Stable key ordering preserves packet
layout and residual-variable numbering despite upstream trail-order changes.

Independently owned handles may run on different threads. Calls touching
MORK's non-atomic process statistics share a synchronization gate, including
same-thread callback reentry. Concurrent use or destruction of the same handle
still requires caller synchronization. This boundary does not promise parallel
kernel execution.

Stopping a MORK query consumer stops answer delivery, while the underlying
walk finishes so upstream bindings and traversal temporaries are released.
This prevents the callback's early-exit leak; it does not promise immediate
termination of the walk. Counted PathMap queries preserve duplicate
multiplicity and retain stored-variable answers alongside exact ground hits.

The broader MM2 corpus uses the current CeTTa checkout and sibling MORK
resources. The small Apache-licensed chainer inputs are pinned under
`tests/support/mm2_chainer/`; `MORK_REPO_DIR` overrides the resource checkout.
The differential gate checks its full expected program sets and fails when
required inputs are missing.

If you need MM2 arithmetic sink examples (`i+`, `i-`, `i*`, `f+`, `f-`, `f*`)
today, use the MORK branch
[`feature/arithsinks-pr-sync`](https://github.com/zariuq/MORK/tree/feature/arithsinks-pr-sync)
instead.

The first bridge build can take a few minutes on a cold Cargo cache.

For a longer bridge/MM2 walkthrough, see `MORK_TUTORIAL.md`.

Build the smaller no-Python bridge variants:

```bash
make BUILD=mork
make BUILD=pathmap   # compatibility alias
```

All bridge-enabled builds (`BUILD=main`, `BUILD=full`, `BUILD=mork`, and
`BUILD=pathmap`) enable `(new-space pathmap)` with counted-key storage. This
stores `atom_bytes || count_bytes` as PathMap keys, preserving multiset
semantics while still benefiting from PathMap's structural matching. See
`specs/pathmap_counted_space_design.txt` for the full design.

Native spaces remain the default. PathMap provides structural trie operations
and ACT import/export, but backend choice depends on the workload. ACT files
are snapshots, not a coordinated durable transaction journal. A remote
atomspace interface can expose either backend without requiring clients to
link Rust; such a server and its durability protocol are separate from this
in-process FFI.

## Play Here First

If you want one successful first run in each major mode, start here after
choosing and building the mode you want:

### Default HE lane

```bash
make
./cetta tests/test_map_filter_atom.metta
```

### PeTTa lane

```bash
make
./cetta --lang petta examples/chaining/tutorial/01-backward.metta
```

Add `--profile extended` for CeTTa's labeled extensions in either language.

### Chaining and directional matching

`docs/chaining-tutorial.pdf` (source `docs/chaining-tutorial.tex`) builds
runnable forward and backward chainers in both languages and introduces the
directional matching operations of the extended profiles (`pat:match%`,
`pat:%match`, `=%` rules and joint constraints):

```bash
./cetta --lang he --profile extended examples/chaining/tutorial/02-direction.metta
./cetta --lang petta --profile extended examples/chaining/tutorial/02-direction.metta
make BUILD=python test-chaining-tutorial
```

### Python + bridge build

```bash
make BUILD=main
./cetta examples/mork_showcase.metta
```

### PathMap counted-space interface

```bash
make BUILD=main
./cetta --profile extended --lang he tests/test_pathmap_counted_space_interface.metta
```

### MeTTa Prime draft

```bash
make
./cetta --lang prime examples/prime/metaprogramming/show_01_lambda_synthesizer.metta
./cetta --lang prime examples/prime/nupln_lambda/nupln.metta
```

## Verified Test Commands

Choose the smallest lane that matches the decision being made:

| Lane | Recommended use | Commands |
| --- | --- | --- |
| Regular development | Fast feedback for the current build after ordinary changes | `make test-correctness test-profiles bench-light` |
| Full correctness | Before review or merge for the current build | `make test-correctness-all test-profiles` |
| Routine readiness | Property-driven correctness, sanitizer, capacity, and performance regression gate | `make main-readiness-routine` |
| Main readiness | Before merging to main or releasing | `make main-readiness-exhaustive` |

`bench-light` is a development performance smoke test. `bench-heavy` is an
opt-in scale diagnostic, not a substitute for any correctness lane.

Run the ordinary checked suite for the current build:

```bash
make test
```

Run the profile interface suite:

```bash
make test-profiles
```

Run the explicit runtime-stats suite for the current build:

```bash
make ENABLE_RUNTIME_STATS=1 test-runtime-stats
```

Run the complete required correctness lane (fast tests, heavy golden tests,
and runtime-stats tests):

```bash
make test-correctness-all
```

Heavy diagnostic probes are deliberately separate because they are exploratory
and have no golden output. Inspect or run them explicitly with
`make list-heavy-diagnostics` and `make probe-heavy-diagnostics`.

The automated readiness driver records content-addressed evidence under
`runtime/main-readiness/`. Set `METTAPEDIA_ROOT` to the Lean project root before
running it when the required CeTTa-to-Lean differential gate is enabled:

```bash
export METTAPEDIA_ROOT=/path/to/Mettapedia/lean/mettapedia

# Faster property-driven gate for ordinary candidate iteration.
make main-readiness-routine

# Reuse a passing result only when source, binaries, inputs, dependencies,
# toolchain configuration, and suite schema are byte-for-byte identical.
python3 scripts/cetta_main_readiness.py --tier routine --reuse

# Inspect whether this exact candidate has completed qualification.
make main-readiness-calibration-status

# Require both a passing routine run and existing qualification.
make main-readiness-routine-authoritative
```

Qualification compares five routine runs with five exhaustive runs for the
same content identity and fails closed after any relevant byte changes. Running
new exhaustive calibration pairs therefore requires an explicit opt-in:

```bash
MAIN_READINESS_ALLOW_EXHAUSTIVE=1 make main-readiness-calibrate
```

The routine gate uses deterministic counters, geometric scale ladders,
source-threshold probes, adversarial mutants, and adaptive paired timings to
detect regressions cheaply. It does not replace the exhaustive main-integration
boundary. Before merging to main or releasing, run:

```bash
make main-readiness-exhaustive
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

For a full main-readiness sweep, run the correctness and profile lanes across
all supported build shapes:

```bash
make BUILD=core test-correctness-all
make BUILD=core test-profiles

make BUILD=python test-correctness-all
make BUILD=python test-profiles

make BUILD=main test-correctness-all
make BUILD=main test-profiles

make BUILD=mork test-correctness-all
make BUILD=mork test-profiles
```

The sanitizer and benchmark aggregates use the same build selection:

```bash
make BUILD=core test-asan
make BUILD=python test-asan
make test-asan-main
make test-asan-mork

make BUILD=core test-tsan
make BUILD=python test-tsan
make test-tsan-main
make test-tsan-mork

make BUILD=main cetta
make bench-light
make bench-capacity
BENCH_ALLOW_HEAVY=1 make bench-heavy
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
- Runtime counters plus `--profile`-aware interface guards
- Explicit `TermUniverse` / persistent term-store seam
- Variant tabling infrastructure with shared canonicalization substrate
- Optional `pathmap` engine for ordinary MeTTa over PathMap
- Explicit `mork:` helper interface for the MORK/MM2 execution lane
- Local git-module and module-inventory interfaces
- Python foreign-module support in the default build
- Optional HTTP, durable host, process, and Telegram service modules
- Experimental managed spellings and registered-name reclamation
- The MeTTa Prime draft described next

## MeTTa Prime draft (`--lang prime`)

**The language:**
- **Local binding is the default.** A `let`, `let*`, `match` or `case`
  pattern introduces fresh local variables each time that binding runs,
  including in code constructed at run time. The source value is evaluated
  outside the new pattern scope: `(let $y 7 (let $y $y $y))` returns `7`.
  To share a pattern name with the enclosing scope, write a crossing set,
  such as `(let $y 7 body){$y}`. `unify` explicitly refines existing names;
  `(new ($h) body)` introduces a private name. A document can retain implicit
  relational sharing with `(scope:profile mercury-implicit)`. The default
  profile is `lexical-fresh per-call reference`.
- **Code is a value.** `@` quotes code into a sealed value, and `*` activates
  it. Equations never rewrite inside a quotation, so a program can hold,
  inspect, transform and pass on code without running it.
- **A native dependent type kernel** answers `type:check`, `type:eq` and
  `type:of`. It covers dependent functions, dependent pairs (`sigma`),
  identity types with `id:eliminate`, and a hierarchy of universes `(u n)`.
  Universe-polymorphic constants are instantiated separately at each use.
- **Declared inductive types** (`set:inductive`) come with recursors whose
  motive may land in any universe, so a recursor computes types as well as
  values.
- **The identity principle is chosen per run:** `--profile identity-j`,
  `identity-scoped`, `identity-uip` or `identity-univalence`. Run
  `./cetta --lang prime --list-profiles` for the list.
- **Checked theorems and definitions:** `set:proves`, `set:theorem`,
  `set:define` and `set:native-proof`. Revising a definition withdraws every
  conclusion drawn from it until it is derived again. Falsum is the
  definition `all prop (lam p p)`, not an axiom.
- **Verdicts say what is known:**
  - a query that runs out of `--fuel` prints `(Incomplete fuel-exhausted)` in
    its place, the run continues, and the exit status is 1;
  - admission and proof search answer Undetermined unless they hold a
    refutation witness;
  - an `Error` at top level is reported and the run continues.
- **Demand-driven evaluation.** On the default route `once` computes only its
  first answer, and `and`/`or` evaluate their second operand only when needed.
- **`Atom` is the top type.** An argument at an `Atom` position is evaluated
  like any other; a parameter declared `(Data Atom)` receives its argument
  as written. The standard library keeps its HE declarations and their
  meaning.
- **Contexts** are persistent lazy environments; see
  `examples/prime/context_tutorial/`.
- **Owned execution control.** `select` retains unfinished computation across
  bounded runs. FIFO, priority/age selection, resource agendas and worker
  policies use the same owned evaluator. A scheduling priority does not grant
  permission to duplicate a shared producer or consume an unavailable resource.
  See [competing resource worlds](tests/prime/native_resources/competing_worlds.metta)
  [self-budgeted search](tests/prime/native_cost/self_budgeted_search.metta),
  and the [unrecorded monitor](examples/prime/native_control/responsive_monitor.metta).
  The monitor returns after each native-work allowance and resumes its retained
  counter; an allowance is not a wall-clock response guarantee.
- **Optional history and debugging.** A requested `history` policy records
  bounded occurrences and dependency graphs. `history:explain` distinguishes
  rejected, pending and unexamined work; `history:dependencies` exposes retained
  graph packets. Stopping recording and physically forgetting an owned diary
  are separate operations. Forgetting preserves live continuations and other
  readers; recording again starts a suffix. Missing history cannot certify an
  empty search. See [admission explanations](tests/prime/need_admission_history.metta)
  and [retained replay](examples/prime/native_control/retained_replay.metta).
  An owned sequential Prime Transaction can also stop before live publication
  with `(breakpoint pattern)`. The [publication client](examples/prime/native_control/controlled_publication.metta)
  lets a competing consumer use the permit, then resumes the retained proposal
  against the current world. This control does not require history recording.

The optional [resource-history reader](tools/native_resource_history.py)
checks recorded ground `read`/`take`/`+` words from supplied initial rows.
Its causal view forgets independent firing order while retaining consuming
conflicts. `--resource-cost` requests firing work and dependency depth;
`--resource-checker` additionally invokes an external source checker for actions,
ordered premises, output identities and final rows. These checks run only when
requested. They authenticate neither the starting state nor execution rights,
and do not establish that a supplied word is a complete execution. Dependency
depth is distinct from measured worker span.

**Example programs** (all in `examples/prime/`, each with its expected output):
- **Learning:**
  - `nupln_lambda/`: Nil Geisweiller's nuPLN backward chainer and his eleven
    tests, with predicates as ordinary lambdas instead of combinator terms. It
    adds a normal-form variant that learns one hypothesis instead of mirror
    images, and it calls the learned lambda on new values.
  - Popper, Metagol, IGGP and Hopper tasks, checked against their
    ground-truth programs.
- **Metaprogramming** (`metaprogramming/`):
  - a lambda synthesizer that returns the learned program with its proof;
  - a staged interpreter (the first Futamura projection);
  - a tactic whose proofs only the kernel can accept;
  - a self-revising learner;
  - a self-reproducing agent (Kleene's recursion theorem at run time);
  - a rewriter that inspects submitted code before anything runs;
  - typed intake of generated code.
- **Other:** `distributional_space.metta`, a space interpreted against a
  joint probability model. The kernel curriculum is in `tests/prime/scoped/`.

**Known gaps in this draft:**
- General HE-to-Prime declaration elaboration remains unfinished. Native
  proof search requires formed types and explicit dependent parameters;
  an open HE type scheme or a multiply annotated constant does not supply
  them implicitly. The cross-dialect gate retains the HE examples and checks
  native counterparts with their own declarations and proof rechecking.
- The native PLN search counterpart uses exact positive/total evidence counts.
  It does not yet host the HE floating-point truth-value evaluator.
- Substitution into code (`lift`) is an equation-level prototype.

## Example Runs

Draft verification on 2026-09-28 used the isolated Python-enabled build:

- `make -j2 -k CETTA_TEST_ISOLATED=1 test`: exit 0; the main HE/golden
  sweep passed 425 cases, with 73 skipped and five lacking expected output.
  The included Prime fast gate passed 226 cases with zero failures.
- `make -j2 -k CETTA_TEST_ISOLATED=1 test-prime-all`: exit 0, including
  the cross-dialect ownership gate (16 checks), native NIK runtime (104
  checks), regular kernel (906 checks), Megalodon tactics package (29
  production checks and 29 differential checks), and all 12 Need mutations.
- Concurrent object publication passed nine causal controls. Recursive
  builds publish completed object files before making them visible to a linker.

The base aggregate explicitly skips unavailable browser tooling, incompatible
Rhocalc Lean microchecks, and inactive or separately selected MORK, PathMap,
heavy and optional feature lanes. These runs do not qualify every build profile.
Curriculum execution and source-theorem coverage are separate: the dedicated
repair gate checks its complete programs and mutations, while unported source
obligations remain outside those passing counts.
The merged upstream separately recorded the following predecessor profile results.
They do not qualify the joined checkout, whose regression gates must run again.

Test results depend on the build mode and enabled features. Use the verified
test commands above for the checkout and feature set you intend to use.
- `make test-profiles`: `77 passed, 0 failed`
- Explicit bridge lanes passed for `BUILD=mork`, `BUILD=main`,
  and their compatibility aliases `BUILD=pathmap` and `BUILD=full`
- Explicit runtime-stats lane bodies also passed for the checked bridge builds
The bridge lockfile is qualified with these dependency revisions:

| Dependency | Revision |
|---|---|
| [MORK](https://github.com/trueagi-io/MORK) | `ed57c6716d8c510296fb5fbb8be6fbfe2df241d7` |
| [PathMap](https://github.com/Adam-Vandervorst/PathMap) | `ef0259152867ece8e85888acc49c238f264a0330` |

For fresh sibling dependency checkouts:

```bash
git clone https://github.com/trueagi-io/MORK.git ../MORK
git -C ../MORK checkout --detach ed57c6716d8c510296fb5fbb8be6fbfe2df241d7
git clone https://github.com/Adam-Vandervorst/PathMap.git ../PathMap
git -C ../PathMap checkout --detach ef0259152867ece8e85888acc49c238f264a0330
make BUILD=mork
```

Use these revisions with the checked-in Cargo lockfile. A newer upstream head
can require dependency updates and its own bridge qualification.

The standalone MORK command-line tool, used by the comparison benchmarks,
is built separately with nightly Rust:

```bash
(cd ../MORK && cargo +nightly build --release -p mork)
```

This lets Cargo refresh MORK's local lockfile when its path dependencies have
changed. After resolution, adding `--locked` verifies that the resolved build
needs no further lock update. CeTTa's bridge uses its own qualified lockfile.

The bridge builds give `(new-space pathmap)` counted-key storage: keys are
`atom_bytes || count_bytes`, which keeps multiset semantics under PathMap's
structural matching (`specs/pathmap_counted_space_design.txt`). The first
bridge build compiles the Rust crates and can take a few minutes. See
[`MORK_TUTORIAL.md`](MORK_TUTORIAL.md) for a step-by-step tutorial on
`MorkSpace`, MM2 execution and PathMap spaces.

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
$ ./cetta --profile extended tests/test_tilepuzzle.metta
8-puzzle BFS: counting all reachable states from solved position...
[()]
[()]
[181440]
```

Uses queue-space for BFS frontier and hash-space for visited-set dedup. This is
a longer-running example; prefer the smaller files below for a quick smoke
test.

### Rho-calculus process engine (`--lang rhocalc`)

The same binary is a concurrent process engine for the reflective
higher-order rho-calculus: sends and receives rendezvous on quoted-process
channels, with COMM as the only reduction. The `cost` profile adds funded
reactions — a reaction fires only when purses located on its own channel
pay for it, and a run can emit a causal receipt of every spend:

```
$ ./cetta --lang rhocalc --profile cost examples/rho/cost/vending_machine.rho
purse vend {()} | purse vend {machine : ()} | {0}served | {for ($order <- vend) {{0}served}}machine | {vend!({0}coin)}bob
```

One funded sale fired; the unfunded order is left facing a live endpoint —
disabled, not slowed. See `docs/rhocalc.md` for the guided tour (calculus,
interfaces, receipts, honest budgets, threaded waves), `examples/rho/` for
runnable showcases (`make test-rho-examples`), and `make test-rhocalc` for
the full lane.

### MeTTa Prime: a learned program with its proof

A learner is given three library predicates and seven labelled numbers. It
proposes lambda hypotheses as code values, smallest first, and proves each one
against the recorded evidence. No library function runs while it learns: the
call counter stays at 30. The learned lambda is then activated with `*` and
run on all ten numbers.

```
$ ./cetta --lang prime examples/prime/metaprogramming/show_01_lambda_synthesizer.metta | tail -3
[(Evidence 10 (CallsSoFar 30))]
[(Learned "@(lam x (∧ (even x) (¬ (small x))))" (TypingSize 9) (ProofSize 26) (, (cet (ran even 6) (cnt (ran small 6))) ...) (CallsAfterLearning 30))]
[(Accepted (6 8) (CallsAfterUse 45))]
```

### MeTTa Prime: a recursor that computes types

A declared inductive type's recursor may return a type. With the motive
`(lam n (u 0))`, the recursor below gives the number type at zero and
functions on numbers at each successor. Values are then checked at the
computed types:

```metta
!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))

!(type:eq &s (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero))
   (-> num num))                                                   ; → True
!(type:check &s (lam x x)
   (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero)))  ; → True
!(type:check &s zero
   (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero)))  ; → False
```

The full file, `tests/prime/scoped/large_recursor.metta`, also builds tuple
types, uses motives into `(u 1)`, and eliminates identity proofs over a large
motive.

## Good First Things To Run

Small regression-sized examples:

```bash
./cetta tests/test_map_filter_atom.metta
./cetta tests/bench_backchain_heavy_nilbc.metta | tail -1
```

Python-facing interface in the default build:

```bash
./cetta tests/test_py_ops_interface.metta
```

Optional MORK-facing interface (requires sibling `MORK` / `PathMap` checkouts):

```bash
make BUILD=main
./cetta examples/mork_showcase.metta
```

Optional counted PathMap space interface:

```bash
make BUILD=main
./cetta --profile extended --lang he tests/test_pathmap_counted_space_interface.metta
```

Large genomic benchmark preview:

```bash
timeout 15 ./cetta --lang he benchmarks/bench_bio_1M.metta 2>/dev/null | head -3
```

Full tile puzzle (longer-running):

```bash
./cetta --profile extended tests/test_tilepuzzle.metta
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
- `src/grounded.c`: grounded operators and runtime interfaces
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
- `lib/lib_wmpln.metta`: Lean-aligned wmPLN interface with evidence-backed extensions
- `lib/mork.metta`: narrow MORK-facing helper interface
- `lib/fs.metta`, `lib/system.metta`, `lib/str.metta`, `lib/path.metta`:
  extension libraries used by the current profile interfaces
  - `fs:read-text` preserves all bytes. `fs:read-text-result path max-bytes errors`
    returns structured success or filesystem failure; `max-bytes` is caller-owned
    (0 means unlimited), and `errors` is `"raw"` or `"replace"` for malformed UTF-8.
    `fs:line-window` streams numbered line windows. `fs:tree-page` and
    `fs:grep-page` return explicit continuation offsets; callers choose skipped
    directories. Search uses PCRE2 (build with `libpcre2-dev`, run with
    `libpcre2-8`); unavailable support returns an explicit error.
    `fs:update-text` supports write, append and exact edits with atomic replacement,
    permissions, SHA-256 receipts and collision-free backups in a caller-selected
    directory. `fs:is-file` distinguishes regular files from directories.
    Oversized files return `("limit" max-bytes)`, never silently truncated text.

Useful workloads:

- `tests/test_tilepuzzle.metta`: full tile BFS benchmark
- `benchmarks/bench_tilepuzzle_probe.metta`: bounded tile runtime probe (runtime-stats interface)
- `benchmarks/bench_bio_1M.metta`: 1.4M-atom genomic benchmark
- `tests/bench_conjunction12_he.metta`: conjunction stress benchmark
- `tests/bench_matchjoin8_he.metta`: join stress benchmark
- `tests/test_pathmap_counted_space_interface.metta`: multiset semantics over PathMap
- `tests/bench_duplicate_conjunction_he.metta`: duplicate conjunction parity test

## CLI
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
count alone is insufficient. Run the bounded candidate check once the fixes
settle. Schedule the broader audits for the affected subsystems and frozen
release milestones; a small follow-up fix does not restart every campaign.

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
