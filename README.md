# CeTTa

A direct C runtime for [MeTTa](https://metta-lang.dev/), the reflective meta-language for AGI.

CeTTa implements the Hyperon Experimental (HE) MeTTa evaluator in ~8,000 lines of C.
It passes 244 oracle-verified tests, runs real genomic inference pipelines,
and is backed by a sorry-free Lean proof of semantic correspondence.

## Quick Start

```bash
# Build (requires GCC + Python 3 dev headers)
make

# Run a MeTTa file
./cetta tests/test_map_filter_atom.metta

# Run the full test suite
make test
```

## Examples

### 1. Higher-Order Stdlib

```metta
!(map-atom (1 2 3 4) $v (eval (+ $v 1)))
!(filter-atom (1 2 3 4) $v (eval (> $v 2)))
```

```
$ ./cetta tests/test_map_filter_atom.metta
[(2 3 4 5)]
[(3 4)]
```

### 2. PLN Evidence Aggregation over Genomic Data

Run typed backward chaining over real GTEx eQTL data with 143 atoms,
3 PLN rules, and proof-score evidence weighting:

```metta
; 3 typed PLN rules: direct target, co-regulation transfer, co-expression transfer
; Backward chainer discovers gene targets with proof terms and evidence scores
!(let (: $proof (target $snp $gene))
      (bc &bio (fromNumber $depth) (: $proof (target $snp $gene)))
   (gene-hypothesis $snp $gene (proof-score $proof) $proof))
```

```
$ ./cetta --lang he tests/bench_bio_bc_pln.metta | tail -3
[(gene-hypothesis rs10000544 ensg00000138735 1.0 (dt eq1)),
 (gene-hypothesis rs10000544 ensg00000178636 0.99 (ct cr1 (dt eq30))), ...]
```

### 3. Proof-Carrying Backward Chaining

Nil Geisweiller's typed backward chainer produces machine-checkable
proof terms at every inference step:

```metta
; Typed inference rules
(: dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))
(: ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

; Query: find all targets of rs10000544 with proof terms
!(bc &bio (fromNumber 2) (: $proof (target rs10000544 $gene)))
; => (: (dt (eqtl-fact rs10000544 ensg00000138735)) (target rs10000544 ensg00000138735))
```

```
$ ./cetta --lang he tests/bench_backchain_heavy_nilbc.metta | tail -1
[(: (r-mortal r01 r01c) (mortal r01)), ..., (: (a-mortal a20 a20c) (mortal a20))]
```

## Performance

CeTTa vs PeTTa (Prolog) and HE (Rust) on real genomic data:

| Benchmark | KB atoms | CeTTa | PeTTa | HE | CeTTa speedup |
|-----------|---------|-------|-------|-----|---------------|
| Focused backward chaining | 143 | **66ms** | 216ms | 1,010ms | 3.3x / 15x |
| PLN evidence aggregation | 143 | **61ms** | 177ms | --- | 2.9x |
| Deep 9-rule BC (1.4M hypotheses) | 261K | **3m22s** | 5m24s | timeout | 1.6x |
| Pattern miner (10K atoms) | 10K | **~23s** | 271s | crash | ~12x |

All runtimes produce identical results on the shared MeTTa fragment.
CeTTa loses 3x to PeTTa on synthetic Metamath proof search (Prolog's
SLD resolution with first-argument indexing wins on deep branching).

## Test Suite

- **244** oracle-verified unit tests (compared against HE CLI v0.2.10)
- **18** benchmark files on real genomic data
- **36** profile tests covering module imports and extension surfaces
- **0** failures

```bash
make test              # run all tests
make test-profiles     # test he_compat / he_extended / he_prime profiles
```

## Architecture

```
src/atom.c     Arena allocator, atom tagged union, hash-consing
src/space.c    Space (atom store) with pluggable match backends
src/match.c    Bidirectional unification, bindings with equalities
src/eval.c     Core evaluator (HE's 6 mutual functions)
src/grounded.c Grounded ops (+, -, *, /, math, string, comparison)
src/parser.c   S-expression parser with variable scoping
src/main.c     Entry point, CLI flags, output handling
lib/stdlib.metta  HE-standard library (precompiled into binary)
```

**Key design choices:**
- Arena allocation (not malloc-per-atom)
- Tail-call optimization via `TAIL_REENTER` trampoline
- Substitution tree with epoch-based standardization apart
- Two-stage stdlib bootstrap (self-compiling)
- Pluggable match backends (SubstTree, discrimination trie, PathMap slot)

## Evaluation Profiles

```bash
./cetta --profile he_compat tests/file.metta     # strict HE compatibility
./cetta --profile he_extended tests/file.metta    # + CeTTa extensions (collect, select, with-space-snapshot)
./cetta --profile he_prime tests/file.metta       # + dependent binder telescope (experimental)
```

**HE'** (he_prime) adds dependent type elaboration for arrow domains:
`(: $e T)` in function type arguments is treated as a dependent binder,
not a literal atom. This enables Curry-Howard backward chainers to work
with type-checked rule applications. Formalized in Lean (230 lines, 0 sorries,
kernel-checked positive and negative examples).

## Verified Translation

CeTTa is backed by a **sorry-free Lean 4 proof** (5,209 lines, 106 theorems)
establishing semantic correspondence between HE and PeTTa evaluation on
the pure MeTTa fragment. The proof covers:

- Pattern matching equivalence (`matchAtoms` / `matchPattern`)
- Equation dispatch correspondence
- `progn` to `let*` and `foldall` to `collapse + foldl-atom` lowering

See the [MeTTaC paper](papers/MeTTaC.pdf) for details.

## CLI Reference

```
./cetta [options] <file.metta>

Options:
  --lang <name>                    Set language mode (default: he)
  --profile <name>                 Set evaluation profile (he_compat|he_extended|he_prime)
  --fuel <n>                       Set evaluation fuel budget (default: unlimited)
  --count-only                     Print result counts instead of atoms
  --space-match-backend <name>     Select pattern matching backend
  --compile <file>                 Emit LLVM IR to stdout
  --list-profiles                  Show available profiles
  --list-space-match-backends      Show available backends
  --list-languages                 Show supported languages
```

## Links

- **Paper:** [MeTTaC: A Direct C Runtime for MeTTa with Verified Translation](papers/MeTTaC.pdf)
- **Lean proofs:** `lean-projects/mettapedia/Mettapedia/Languages/MeTTa/`
- **HE spec:** [metta.md](https://github.com/trueagi-io/hyperon-experimental/blob/main/docs/metta.md)
- **Known discrepancies:** [DISCREPANCIES.md](tests/DISCREPANCIES.md)
