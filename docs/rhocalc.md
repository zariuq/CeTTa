# The rho-calculus in CeTTa: `--lang rhocalc` and the cost profile

Under `--lang rhocalc` the CeTTa runtime stops being a term rewriter and
becomes a concurrent process engine. Its unit of work is a *reaction*: a
rendezvous between a sender and a receiver on a shared channel. This
document is a guided tour — the calculus, the two text surfaces, the
reflective extension that runs MeTTa at a reaction, and the cost profile,
in which a reaction fires only if it is paid for and a run can emit an
auditable receipt of every spend. Receipts are deliberately an optional
observation: the funded transition is the same with or without one.

The implementation lives in `src/rhocalc_core.c` (engine),
`src/rhocalc_syntax.c` (both text surfaces), `lib/lts/rho.metta` and
`lib/lts/rho/cost.metta` (MeTTa API). Runnable showcases live in
`examples/rho/` and are checked by `make test-rho-examples`.

## 1. The calculus in five minutes

The object language is Meredith and Radestock's reflective higher-order
rho-calculus. Everything is built from six constructors:

| form            | s-expression (`.mrho`)     | surface (`.rho`)       | reading                          |
|-----------------|----------------------------|------------------------|----------------------------------|
| inert process   | `rho:nil`                  | `0`                    | does nothing                     |
| parallel        | `(rho:par P Q ...)`        | `P \| Q`               | run side by side                 |
| send            | `(rho:send x P)`           | `x!(P)`                | offer P on channel x             |
| receive         | `(rho:recv x $y P)`        | `for ($y <- x) {P}`    | await a name on x, bind it to $y |
| quote (a name)  | `(rho:quote P)`            | `@{P}`                 | the process P, frozen as a name  |
| drop            | `(rho:drop x)`             | `*x`                   | the process a name quotes        |

There are no primitive channel names: **a name is a quoted process**. The
channels `@{0}` and `@{@{0}!(0)}` are two different names because they
quote two different processes. This is what "reflective higher-order"
means — code can travel as data (quote) and data can run as code (drop).

One rule does all the computing. When a send and a receive meet on the
same channel, they react:

    x!(m) | for ($y <- x) { P }   --COMM-->   P[$y := @m]

The message is consumed, and the receiver continues with the *quoted*
message substituted for its binder. Substitution is where dequotation
happens: if `P` uses `*$y`, the substituted `*@m` becomes `m` — the
received code runs.

Two static equivalences are kept separate and never run anything:

* **Name equivalence** contains the cancellation `@{*x} = x` — quoting
  the drop of a name gives back the name. Aliases of a channel are the
  same channel.
* **Structural congruence** contains alpha-renaming and the
  commutative-monoid laws of `|` (order and grouping of parallel
  components do not matter; `P | 0 = P`).

A positive and a negative example:

    @{0}!(x!(0)) | for ($y <- @{0}) { *$y }     — reducible: one COMM, then the payload runs
    *@{ x!(0) }                                 — stuck: a free-standing drop is INERT in pure rho

The second example is the calculus's most commonly violated law.
`*@{P} -> P` is **not** a reduction rule. Dequotation happens only
through substitution after a communication — you can run exactly the code
you were sent. CeTTa's strict-core profile enforces this: the free drop
does not step, and non-core forms are rejected with a diagnostic rather
than left inert.

Execution without an execution primitive is still expressible — at the
honest price of one handshake (the *runner idiom*):

    run!(P) | for ($y <- run) { *$y }   --COMM-->   P

See `examples/rho/pure/eval_at_comm.rho` for a complete program that
settles at `0` only because the delivered code actually ran.

## 2. Running programs

    ./cetta --lang rhocalc program.rho            # Rholang-like surface
    ./cetta --lang rhocalc program.mrho           # s-expression surface
    ./cetta --lang rhocalc --profile cost program.rho
    ./cetta --num-threads 8 --lang rhocalc --profile cost program.mrho

The engine reduces the program to quiescence and prints the residual in
the input surface. `--rho-reduction-limit N` bounds the run (a divergent
program reports its limit honestly); `--rho-scheduler canonical|rotating`
picks the sequential scheduling policy (canonical = always the key-least
successor, fully deterministic; rotating = cycle the frontier).
`--syntax mrho|rho` overrides the extension-based surface choice, and the
same flag set drives surface-to-surface translation between `.rho` and
`.mrho`. `--emit-runtime-stats` prints engine counters (endpoint matches,
macro-step firings, wave widths, receipt bookkeeping) after the run.

Internally both profiles share one canonical form: parallel components
are flattened and sorted under alpha-invariant keys and the name
cancellation `@{*x} -> x` is oriented as a simplification, so structural
congruence becomes string equality of keys. That is why residuals print
in a deterministic order no matter how the run was scheduled.

## 3. rhometta: MeTTa at the reaction

The strict core is deliberately tiny, so where does real computation come
from? From the payload. The `rhometta` library (`lib/rhometta.metta`,
over `lib/rho.metta`) defers a MeTTa term as a payload evaluated *at* the
COMM. When the payload evaluates to several results, the reaction forks —
one successor per result — so nondeterministic evaluation becomes a
branching reaction and `rhometta:run` returns one residual per branch
(the may-set). Each payload runs against sibling-isolated copy-on-write
scratch: writes made while one branch evaluates can never leak into a
sibling branch.

## 4. The cost profile: reactions that must be paid for

The `cost` profile accepts a resource-annotated dialect:

| form               | s-expression                      | surface (`.rho`)         | reading                          |
|--------------------|-----------------------------------|--------------------------|----------------------------------|
| signed process     | `(rho:cost:signed P s)`           | `{P}s`                   | P, signed by s                   |
| purse              | `(rho:cost:purse c stack)`        | `purse c {...}`          | a token store located at c       |
| token stack        | `(rho:cost:stack-cons s rest)`    | `{s : s' : ()}`          | tokens, spent head-first         |
| empty stack        | `rho:cost:stack-empty`            | `{()}`                   | no tokens left                   |
| signature product  | `(rho:cost:sig-mul s s' ...)`     | `s * s'`                 | a multiset of signatures         |

Signatures are ground symbols and form a free commutative monoid under
`sig-mul`: a signature product is kept as a sorted multiset, so signature
equality is multiset equality.

**The funding rule.** A COMM between signed endpoints demands the product
of the endpoint signatures (a *whole redex* — one signed `par` of a
matched send/receive pair — demands its single signature). The demand
must be **exactly covered** by tokens drawn from purses **located on the
reaction's own channel**: no ambient bank account, no partial payment, no
change given, and raw process data never funds anything. Location follows
name equivalence, so a purse on `@{*pay}` funds reactions on `pay` —
aliases of a channel are the same account.

The consequence to internalize first: **an unfunded reaction is disabled,
not slowed**. In `examples/rho/cost/vending_machine.rho` a till with two
`machine` tokens serves alice (who has a coin in the channel purse) and
then stands facing bob's order forever — the send and receive sit beside
each other, enabled in the pure calculus, unable to fire in the cost
profile because bob's share of the demand has no cover. Deleting a purse
is the negative example: the same program text, one purse fewer, and the
reaction count drops.

## 5. Receipts: causal records, not counters

What a receipt-observed run returns is not a number but a **receipt**: the
ordered list of events the run fired, plus the residual state.

    (lts:rho:cost:receipt
      ((lts:rho:cost:event 0 ()    ((lts:rho:cost:funding market farm))    farm)
       (lts:rho:cost:event 1 (0 0) ((lts:rho:cost:funding market roaster)) roaster)
       (lts:rho:cost:event 2 (1 1) ((lts:rho:cost:funding market cafe))    cafe))
      <residual>)

Each event carries four fields:

1. a fresh run-local **id**;
2. a **cause list** — the ids of the events that *produced* the exact
   occurrences this event consumed, with repetitions kept: event 1 above
   consumed both a continuation and a purse tail produced by event 0,
   hence `(0 0)`. Initial components have no producer and contribute no
   arc — indistinguishable initial occurrences acquire no invented
   identity, and independent reactions get no false edge merely because
   one event id was emitted before the other;
3. one **funding** record per token spent (purse surface and head
   signature);
4. the raw **consumed signature**.

Run `examples/rho/cost/notary_chain.metta` to see both sides: a
three-stage supply chain whose receipt chains `0 <- 1 <- 2`, and two
independent settlements with empty cause lists.

The algebraic reading: a receipt is a located, measured *pomset* of spend
events, ordered purely by consumption; the engine's emission order is one
linearization of that partial order. "The cost" is deliberately not a
field of the receipt. Total spend, per-signature spend, and critical-path
depth are *valuations* — monoid-valued maps computed from the receipt
afterward. The receipt is the exact object; every price is a lossy,
explicitly-chosen observation of it.

## 6. Honest budgets

The exhaustive surfaces (`lts:rho:cost:steps`,
`lts:rho:cost:causal-trace`, `lts:rho:cost:transitions`) never truncate
silently — funding selection is a genuine exact-cover problem, and a
demand of ten unit coins over twenty unit purses has C(20,10) = 184,756
distinct covers, every one of which the frontier will enumerate if asked.

Bounded execution is strictly opt-in through
`lts:rho:cost:causal-prefix`, which takes two separate allowances —
reaction fuel and cover-search work — and returns
`(lts:rho:cost:prefix STATUS RECEIPT)` where STATUS never lies:

* `lts:rho:cost:quiescent` — only after exhaustive search **proves** the
  residual has no funded successor;
* `lts:rho:cost:fuel-exhausted` — the firing allowance ended first (at
  fuel 0 the machine does not even search);
* `lts:rho:cost:search-exhausted` — cover search stopped while further
  search remained semantically relevant. Never reported as quiescence: an
  out-of-fuel machine says "I ran out", not "there was nothing there".

`examples/rho/cost/budget_meter.metta` shows all three verdicts on one
program, plus a two-cover spend demonstrating why the frontier keeps
genuinely different funding futures apart.

## 7. Parallel execution: waves of disjoint claims

With `--num-threads N`, compatible cost firings execute in OS-thread
waves. Before a worker fires, it atomically claims the *exact
occurrences* its plan touches — both endpoints and every funding token —
in a fixed sorted resource order; a wave admits only pairwise-disjoint
claims, and every committed wave is re-validated against occurrence
accounting before the residual is rebuilt. The sequential semantics stays
the reference: a threaded run is a legal parallel refinement, not a new
relation, and because the residual is kept canonical, disjoint workloads
produce byte-identical output at any thread count
(`examples/rho/cost/parallel_settlement.mrho`, checked sequentially and
threaded by `make test-rho-examples`).

Causal receipts are an **optional observer** on this path: a state-only
run and a receipt-observed run consume identical resources and reach the
same residual, and the emitted event order of an observed run is a
checked linearization of the wave's causal order.

## 8. The MeTTa API surface

Import `lts:rho` (strict core) or `lts:rho:cost` (accounted). Every
operation propagates incoming `Error` states unchanged and reports
malformed non-Error input as an explicit `Error` — never as quiescence.

| operation                        | returns                                          |
|----------------------------------|--------------------------------------------------|
| `lts:rho:transitions`            | one-step COMM successors                         |
| `lts:rho:trace-2`                | exact two-step trace pairs                       |
| `lts:rho:is-normal-form` / `is-can-step` | frontier emptiness / nonemptiness        |
| `lts:rho:cost:steps`             | accounted steps `(lts:rho:cost:step cost state)` |
| `lts:rho:cost:transitions`       | residual-state projection of `steps`             |
| `lts:rho:cost:step-cost` / `step-state` | field projections of a step record        |
| `lts:rho:cost:causal-trace`      | receipt of one terminating run                   |
| `lts:rho:cost:causal-prefix`     | `(prefix STATUS receipt)` under two allowances   |
| `lts:rho:cost:is-normal-form` / `is-can-step` | accounted frontier checks           |

Each operation carries a full `@doc` in `lib/lts/rho/cost.metta`.

## 9. Correctness evidence

* `make test-rhocalc` — the full lane: surface runs (`tests/rhocalc_run/`,
  `tests/rhocalc_cost_run/`), the `.metta` API tests
  (`tests/test_lts_rho_cost_*.metta`), rejection tests, translation
  round-trips, the canonical-selector differential, and the examples.
* **Lean bridges** — `tests/rhocalc_cost_lean_bridge.tsv` drives the same
  terms through this engine and the Lean cost executor;
  `tests/rhocalc_cost_receipt_replay.lean` replays compiled-C receipts as
  Lean derivations and rejects tampered ones. This is bounded
  differential evidence about the compiled engine, not a universal
  theorem about C; the theorems live on the Lean side.
* **Commit audit** — `make test-rhocalc-cost-commit-audit[-asan|-tsan]`
  rebuilds with `RHOCOST_COMMIT_AUDIT=1`, which re-derives and re-checks
  every committed parallel firing (redex shape, channel keys, contractum,
  exact funding) inside the wave commit, under thread and
  address/undefined sanitizers, plus a scripted observer-transparency
  check (receipts change nothing about the state path).
* **Macro-step audit** — the strict-core engine may fire an independent,
  quiet frontier as one macro step (a partial-order reduction). Setting
  `CETTA_RHO_NO_MACRO=1` forces the exact interleaving exploration so any
  program can be run both ways; `make test-rhometta-macro-audit` does
  this differentially.

## 10. Where the theory lives

The profile is the executable half of a programme to equip the reflective
higher-order calculus with compositional resource accounting. The
companion papers state the mathematics: the theory paper (*Pure Rho and
Cost*) fixes the pure calculus, the corrected cost hypotheses (why
ordinary quote-faithfulness is impossible, and the pre/post-indexed
repair), and the erasure theorems from funded execution to the pure
calculus; the CeTTa engine paper's cost section documents this
implementation against that theory. The guiding commitments visible
everywhere in the code: funding is located, causality comes from
consumption, receipts are exact, and every price is a named lossy
observation.
