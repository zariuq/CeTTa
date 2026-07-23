# CeTTa he-prime: typed search over dependent codomains (experimental vertical slice)

A CeTTa worktree with an experimental HE typing extension for the `he-prime` profile, built on
the shared native HE inference engine: success-typing discipline, dependent-codomain elaboration
with type-level computation, and proof search as type inhabitation. It runs Nil Geisweiller's dependent-typed PLN rules —
the Deduction truth value is computed *inside the type* — which his DTL work had to leave
disabled. Executable evidence throughout; no kernel correspondence is claimed (see the honest
boundary below).

## What it does (each backed by a blessed test or the runnable demo)

- **Functions in dependent result types.** `get-type (Deduction pP pQ pR pPQ pQR)` computes
  `(≞ (→ P R) (STV 0.6333… 0.7))` — the PLN formula executes in the codomain during typing.
- **Proof search = type inhabitation.** `search-first-inhabitant` searches for a term inhabiting a
  goal type and returns it *re-checked*, with the truth value computed in the type (Deduction,
  Modus Ponens, recursive evidential rules).
- **Checked Nat-refinement.** `check-type-refinements` runs declared `type-index-refinement`
  refinements: `(VecN String 2)` accepts, `(VecN String -1)` rejects. It checks declared
  index refinements only — it is NOT a well-formedness judgment (no universes, no binder-domain
  formation, no positivity).
- **Success-typing discipline with explicit edge tags.** Every consistency verdict names
  its reason (exact / structural / dynamic / top / meta-staging), composition takes the weakest
  child tag (a `?` child keeps the whole edge dynamic — no laundering by nesting), and only
  exact/structural edges are the checked search boundary: gradual acceptances can pass `check-type` but can
  never stand as inhabitants.
- **Effect-free type conversion.** Type-level grounded computation is capability-gated
  (`grounded_op_is_type_pure`): arithmetic and friends reduce; anything effectful or
  state-reading is refused with an honest `type-computation-inadmissible` reject, and the ops'
  declared types stage their term/type arguments so argument evaluation cannot run effects
  either. Marked user type-level functions run against a fuel-bounded snapshot (space effects
  contained and discarded) — a scoped experimental policy, not a proven purity judgment.
- **Three-valued honesty end to end.** accept / reject(reason) / unknown(reason); forward
  closure reports `fixpoint` only when one is reached (`resource-incomplete` /
  `rounds-exhausted` otherwise); deep searches report fuel exhaustion instead of a false no.
- Profile-scoped: the extension operations are he-prime only; under `he`/`he-compat`/`he-extended`
  they stay inert, while all four profiles share the same native HE inference engine.

## Build + run

```sh
make BUILD=core
./cetta --profile he-prime --lang he showcase/nil_pln_dtt_exact_demo.metta
```

Regression gates (both wired into `make test-profiles`):
- `tests/profile_he_prime_dtt_chainer_showcase.metta` — the positive capabilities.
- `tests/profile_he_prime_dtt_adversarial.metta` — the semantic negatives: nested laundering
  block (products and arrows), top-not-exact, effect exclusion with a no-mutation check,
  fuel-starved closure honesty. These are the counterexamples a green positive suite misses.

Full `make test` passes.

## Layout
- `src/he_typing.{c,h}` — consistency edges + composition algebra, checked normalization,
  refinement checking, inhabitant search, forward chaining. Hooks are weak-linked so
  standalone unit-test binaries (which link grounded.c without he_typing.c) still build.
- `src/grounded.{c,h}` — `grounded_op_is_type_pure` (the type-level capability, a positive
  list); the `src/space.c`/`src/eval.c` conversion paths gate on it.
- `src/main.c` — he-prime-only declared types for the typing ops (`Atom` params = staged
  arguments, the HE-native mechanism).
- `showcase/` — `nil_pln_core.metta` (a derived encoding of Nil's PLN constructor types — not
  an unchanged execution of his source tree) + `nil_pln_dtt_exact_demo.metta`.
- `he_typing_findings/` — the exploration reports that led here (historical, kept as-is; their
  vocabulary predates the current public surface).

## Honest boundary
- Executable evidence, not theoremhood: the final proof check re-uses this same C
  implementation. The **C↔Lean correspondence theorem is OPEN**; the existing Lean telescope
  model covers the earlier binder elaboration, not this module.
- This is dependent *elimination* plus checked refinements — not full DTT (no Σ/Id/ι formers,
  no universe discipline, no certificate generation).
- Backward search over a computed codomain with free truth-value variables is not handled
  (ground truth values work); deep evidential searches hit fuel and say so.
- The worktree is rebased on stabilized main and includes its all-profile directional `Atom`
  matching fix; the extension remains isolated to `he-prime`.
