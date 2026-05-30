# CeTTa Rhocalc Path To Success

Date: 2026-05-27

## Plan Management

The current rhocalc plan is always `docs/plans/rhocalc_plan_current.md`, which must be a symlink to a dated plan file in `docs/plans/`. When the plan changes substantively, create a new dated plan file, retarget the symlink, and preserve old dated plans. Keep plan updates factual: current public contract, milestones, audit gates, and open decisions only.

## Purpose

CeTTa should provide a simple, complete strict-core rho-calculus language path:

- `--lang rhocalc` parses rho programs and reduces them by send/receive communication.
- Reduction runs to quiescence, or stops with a reduction-limit-exhausted residual when `--rho-reduction-limit` is exhausted.
- There is no public or hidden `rho:step`, frontier debugger, relation-oracle flag, or other secret CLI surface.
- Testing and comparison tools must not require undocumented production flags.

The core rule is: users write processes; the runtime reduces enabled communications.

## Current Public Contract

The public rhocalc surface is:

- `cetta --lang rhocalc [--rho-reduction-limit N] [--rho-scheduler NAME] file`
- `--rho-reduction-limit N`: maximum number of strict-core COMM reductions.
- `--rho-scheduler canonical|rotating`: explicit scheduler policy.

This reduction limit is a CeTTa evaluator bound on selected COMM reductions. It is not Rholang phlo/gas, which is resource accounting over parsing, normalization, substitution, storage, and runtime events.

Any CLI option accepted by `cetta` must appear in `--help`. Hidden and undocumented production flags are not allowed.

## Non-Goals

- Do not reintroduce `rho:step`, `rho:frontier`, or any one-COMM inspection operator in `lib/rho.metta`.
- Do not add hidden `--internal-*`, `--__*`, or undocumented test-only flags to the production binary.
- Do not add a MeTTa-level callable in the `rho:` namespace that evaluates rho processes, such as `rho:reduce`, `rho:eval`, `rho:step`, or `rho:frontier`.
- Do not treat frontier enumeration as a user-facing rhocalc feature.
- Do not widen strict-core rho with Rholang-only constructs such as contracts, `new`, persistence, joins, or richer pattern syntax.
- Do not add persistent runtime state separate from the process. Channel indexes and reduction state are caches of the residual process, not independent stores.

## Milestones

### M1: Strict-Core Runtime Correctness

Lock down `--lang rhocalc` as a reducer for the six paper constructors only:

- `rho:nil`
- `rho:par`
- `rho:send` (lift/output)
- `rho:recv` (input)
- `rho:drop`
- `rho:quote`

No additional `rho:` constructors belong in strict-core rhocalc. Replication is encoded via lift/drop, not added as a primitive.

Required invariants:

- The reduction relation is the primary semantic object; a scheduler chooses one successor trajectory through that relation.
- The relation may be non-confluent; deterministic scheduling is an implementation policy, not a calculus property.
- Name equivalence, structural congruence, and alpha-equivalence must be treated together because the paper definitions are mutually dependent.
- Lift/output is asynchronous: `rho:send` has channel and payload only, never a continuation.
- Reduction respects the monoidal structure of `rho:par`; syntactic order of parallel components must not change the enabled COMM set modulo structural congruence.
- Substitution is capture-safe.
- Reduction stops at a quiescent residual with no enabled COMM, or at a reduction-limit-exhausted residual with the reduction limit reported.
- Scheduler choice is explicit and documented.
- No accepted CLI flag may be hidden from `--help`.

Done when the full core test suite passes and accepted CLI flags exactly match documented help flags.

### M2: Independent Semantic Test Support

Keep semantic testing outside the production CLI:

- curated tiny independent semantics for small strict-core witnesses;
- bounded reachability checks for scheduler membership;
- bounded bisimulation or bounded observational checks for scheduler and syntax equivalence;
- progressively stronger Lean bridge checks for tiny COMM, normal-form, and bounded trace witnesses (`CanStep`, `NormalForm`, `ReducesN`, `ReducesStar`).

Bisimulation is the paper-grounded equivalence target for process behavior. Exact residual equality is only a local engineering check for deterministic scheduler fixtures; cross-scheduler and cross-implementation tests should be relation- or equivalence-based.

The tiny independent semantics is intentionally partial. It is a curated subset oracle for fixtures whose strict-core fragment it is known to model faithfully, not a full second implementation of rhocalc. Richer alpha-sensitive, whole-name-substitution, and similarly complex cases remain covered by public reducer fixtures, bounded bisimulation, or Lean-bridge checks until they are independently modeled.

These tools may live in `scripts/` and tests, and they may use public `cetta` surfaces plus independent semantics helpers, but they must not require hidden `cetta` flags.

### M3: Rholang Intersection Testing

Define the overlap between strict-core rhocalc and Rholang:

- map rho surface syntax to canonical strict-core `mrho`;
- identify fixtures that both CeTTa rhocalc and a Rholang implementation can run;
- compare behavior by reachability or bounded observational equivalence, not exact text output;
- keep Rholang-only features out of strict-core rhocalc.

Done when a small, documented cross-implementation fixture set can be run through both sides with an explicit equivalence contract.

### M4: Library Surface

Keep `lib/rho.metta` austere:

- it may remain a documentation-only stub, or expose strict-core rho constructors/surface helpers if the host evaluator needs them;
- any exposed helpers must stay constructor- or surface-level, not evaluator-like;
- it must not expose step/frontier/debug/relation-oracle functionality;
- it must not expose a `rho:` evaluator for rho processes;
- it must not pretend to be Rholang.

Future `lib/rholang` or `--profile rholang` may provide Rholang sugar that desugars into strict-core rho where possible.

### M5: Performance And Parallelism

Only after M1-M3 are solid:

- thread the reducer without changing the public contract;
- validate threaded behavior against bounded semantic equivalence;
- add scheduler policies only when their test contract is clear;
- keep performance knobs documented if they are accepted CLI options.

The default-scheduler decision must be resolved before M5 begins, because threaded reduction validation depends on whether the main contract is reproducible exact output or equivalence over nondeterministic trajectories.

## Audit Gates

### Gate A: Semantic Foundation

Run this audit after M1 and the first serious M2 pass:

- exact parser-vs-help CLI audit: every accepted `--flag` in `src/main.c` matches the flags shown in `--help`;
- broad grep audits for hidden/debug/internal vocabulary as a supplementary smoke check, not a substitute for the exact parser-vs-help audit;
- `lib/rho.metta` hygiene audit: no evaluator surface, no `rho:step`, no `rho:frontier`, no debug/relation-oracle surface, and no MeTTa-level `rho:` evaluator;
- strict-core runtime test suite;
- six-constructor boundary check;
- name/structure/alpha equivalence interaction tests;
- asynchronous lift and process-state-only checks;
- tiny independent semantics coverage;
- bounded reachability/equivalence coverage;
- Lean bridge status;
- known gaps listed explicitly.

### Gate B: Rholang Intersection

Run this audit after the first cross-Rholang fixture set:

- overlap subset is documented;
- non-overlap is documented;
- comparison relation is documented;
- both implementations run the selected fixtures.

### Gate C: Threaded Reducer

Run this audit before merging or relying on threaded reduction:

- sequential and threaded reducers satisfy the same semantic tests;
- scheduler policy is explicit;
- no new hidden flags or test-only production surfaces were introduced.

## Open Decisions

- Whether the eventual default scheduler should remain reproducible or become a fair/nondeterministic policy once such a policy exists. Resolve this before M5.
- Whether `lib/rho.metta` should expose any constructor helpers, or remain a documentation-only stub while `--lang rhocalc` owns execution.
- How much of the Rholang surface belongs in `lib/rholang` versus `--profile rholang`.
- Which external Rholang implementation should be the first cross-implementation comparison target.
- Whether the Lean bridge is the formal anchor, or whether to also pursue a categorical formalization of the operational semantics as a long-term target.

## Definition Of Success

CeTTa succeeds here when a user can write a strict-core process using only the six paper constructors, run it with `--lang rhocalc`, and get either a quiescent residual with no enabled COMM or a reduction-limit-exhausted residual with the limit reported. The accepted CLI flag set must exactly match `--help`, no `rho:` namespace construct may exceed the six core constructors, and the reducer's behavior must be captured by the reduction relation with a documented scheduler policy.
