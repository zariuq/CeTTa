# CeTTa Rhocalc Path To Success

Date: 2026-05-26

## Plan Management

The current rhocalc plan is always `docs/plans/rhocalc_plan_current.md`, which must be a symlink to a dated plan file in `docs/plans/`. When the plan changes substantively, create a new dated plan file, retarget the symlink, and preserve old dated plans. Keep plan updates factual: current public contract, milestones, audit gates, and open decisions only.

## Purpose

CeTTa should provide a simple, complete strict-core rho-calculus language path:

- `--lang rhocalc` parses rho programs and reduces them by send/receive communication.
- Reduction runs to quiescence, or stops honestly when `--rho-fuel` is exhausted.
- There is no public or hidden `rho:step`, frontier debugger, relation-oracle flag, or other secret CLI surface.
- Testing and comparison tools must not require undocumented production flags.

The core rule is: users write processes; the runtime reduces enabled communications.

## Current Public Contract

The public rhocalc surface is:

- `cetta --lang rhocalc [--rho-fuel N] [--rho-scheduler NAME] file`
- `--rho-fuel N`: maximum number of strict-core COMM reductions.
- `--rho-scheduler canonical|rotating`: explicit scheduler policy.

Any CLI option accepted by `cetta` must appear in `--help`. Hidden and undocumented production flags are not allowed.

## Non-Goals

- Do not reintroduce `rho:step`, `rho:frontier`, or any one-COMM inspection operator in `lib/rho.metta`.
- Do not add hidden `--internal-*`, `--__*`, or undocumented test-only flags to the production binary.
- Do not treat frontier enumeration as a user-facing rhocalc feature.
- Do not widen strict-core rho with Rholang-only constructs such as contracts, `new`, persistence, joins, or richer pattern syntax.

## Milestones

### M1: Strict-Core Runtime Correctness

Lock down `--lang rhocalc` as a reducer:

- strict-core constructors only;
- structural congruence respected;
- alpha-equivalence respected;
- quote/drop name equivalence respected;
- substitution is capture-safe;
- enabled send/receive pairs fire according to COMM;
- reduction stops at quiescence or fuel exhaustion;
- scheduler choice is explicit and documented;
- no hidden CLI flags.

Done when the full core test suite passes and accepted CLI flags exactly match documented help flags.

### M2: Independent Semantic Test Support

Keep semantic testing outside the production CLI:

- curated tiny independent semantics for small strict-core witnesses;
- bounded reachability checks for scheduler membership;
- bounded observational or bisimulation-style checks for scheduler and syntax equivalence;
- progressively stronger Lean bridge checks for tiny COMM and normal-form witnesses.

These tools may live in `scripts/` and tests, but they must not require hidden `cetta` flags.

### M3: Rholang Intersection Testing

Define the overlap between strict-core rhocalc and Rholang:

- map rho surface syntax to canonical strict-core `mrho`;
- identify fixtures that both CeTTa rhocalc and a Rholang implementation can run;
- compare behavior by reachability or bounded observational equivalence, not exact text output;
- keep Rholang-only features out of strict-core rhocalc.

Done when a small, documented cross-implementation fixture set can be run through both sides with an honest equivalence contract.

### M4: Library Surface

Keep `lib/rho.metta` austere:

- it may document or expose strict-core rho constructors/surface helpers if the host evaluator needs them;
- it must not expose step/frontier/debug/relation-oracle functionality;
- it must not pretend to be Rholang.

Future `lib/rholang` or `--profile rholang` may provide Rholang sugar that desugars into strict-core rho where possible.

### M5: Performance And Parallelism

Only after M1-M3 are solid:

- thread the reducer without changing the public contract;
- validate threaded behavior against bounded semantic equivalence;
- add scheduler policies only when their test contract is clear;
- keep performance knobs documented if they are accepted CLI options.

## Audit Gates

### Gate A: Semantic Foundation

Run this audit after M1 and the first serious M2 pass:

- hidden-flag audit;
- strict-core runtime test suite;
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

- Whether the eventual default scheduler should remain reproducible or become a fair/nondeterministic policy once such a policy exists.
- Whether `lib/rho.metta` should expose any constructor helpers, or remain a documentation-only stub while `--lang rhocalc` owns execution.
- How much of the Rholang surface belongs in `lib/rholang` versus `--profile rholang`.
- Which external Rholang implementation should be the first cross-implementation comparison target.

## Definition Of Success

CeTTa succeeds here when a user can write a strict-core rho process, run it with `--lang rhocalc`, and get an honest quiescent or fuel-exhausted residual, while developers can test the implementation against independent small semantics, bounded equivalence, and Rholang-overlap fixtures without any hidden CLI features or object-language step operator.
