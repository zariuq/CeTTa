# Ground-Call Memoization (Prime) — Design

Status: 2026-07-23. **SHIPPED (synchronous path), off by default.** fib(30) is
now correct and practical: 832040 in ~0.1 s (previously fib(25) alone timed out
at 90 s).  All acceptance gates green on the pinned non-sanitizer core binary
sha256 `a804acc39fc390661cd69a55bd30714b731959adfaa1e4b9e9d5bff4d68f970a`.
Promotion of the default remains a separate decision.

## What shipped (implementation)

- **Two admission predicates** (the only genuinely new logic):
  `prime_need_receipt_delta_is_pure` (src/prime_need.c) and
  `canonical_ground_call_key` (src/table_store.c).
- **Sibling table mode** `CETTA_TABLE_MODE_GROUND_CALL` with a `GroundCallStore`
  (canonical call + hash + AnswerRef bag), `table_store_ground_lookup` /
  `_commit` (src/table_store.c).
- **Invalidation** via a process-global mutation epoch bumped in
  `space_bump_revision` (src/space.c, `space_global_mutation_epoch`).  This is a
  sound over-approximation of the per-space SpaceReadToken check: epoch
  unchanged ⟹ no space mutated ⟹ every consulted space unchanged.  Per-space
  token narrowing is a documented future refinement.
- **Sync-path hook** in `metta_call` (src/eval.c, `#if !CETTA_PRIME_EVAL_STACK`):
  on an applicable ground call it forces the arguments, keys on the forced form,
  replays on a hit, and on a miss dispatches the forced call and commits if the
  produced bag is admissible (non-empty; every occurrence a ground value or a
  completed stable fault; pure receipt delta).  The eval-stack (async) commit
  site is future work; the driver is off by default so the sync hook is the
  default path.
- **The key trap and its guard** (this is the load-bearing subtlety): lazy
  recursion reaches the evaluator as `(f (- 20 1))`, never `(f 19)`, so keying
  on the raw atom hits nothing.  We force arguments to values before keying —
  but *only syntactically pure grounded arithmetic* arguments (numeric literals
  and `+ - * / // %` over them), which are terminating and effect-free by
  construction, so forcing them cannot change observable behaviour even for an
  argument the callee would never demand (`(const 5 (loop))` stays lazy and
  terminates).  Any other argument shape falls through to normal lazy eval, no
  lookup.  This keeps fib's hit-rate ~100% while making the hook impossible to
  blame for a semantics change.
- **Switch**: `pragma! search-table-mode ground-call` or the env
  `CETTA_TABLE_MODE=ground-call`; default off.
  `CETTA_GROUND_MEMO_STATS=1` prints a lookup/hit counter at exit.
- **Gates**: `make test-ground-call` runs seven goldens (in the `test`
  aggregate); `scripts/test_ground_call_mutations.sh` rebuilds three planted
  defects (bag→set, ignore-epoch, admit-impure) and proves each golden catches
  its mutation.  ASan/UBSan clean on the memo path; off-vs-on bag-identical on
  the fib curve; VARIANT table goldens unregressed.

The remainder of this document is the original design (the admission predicate,
data structures, boundary analysis, and test plan) that the above realizes.

## 1. Problem and thesis

Naive `fib(30)` on Prime issues ~2.7M raw calls over ~30 distinct ground
arguments. The Need machine removed the *per-path* accidental cost, but the
call count itself is exponential. A dependence-safe memo table over **ground
canonical calls** collapses those 2.7M calls to ~30 evaluations, making
fib(30) practical — without changing call-by-need semantics, because a memo
hit replays a *previously observed whole answer bag* only when replaying it is
observationally indistinguishable from re-evaluating.

The table is a sibling of `CETTA_TABLE_MODE_VARIANT`. VARIANT memoizes
space *queries* at the space-query layer. GROUND_CALL memoizes *function
calls* at the ground-call boundary, before equation dispatch. They share the
`AnswerBank` and revision-token machinery; they key and gate differently.

## 2. Admission predicate (v1 — all conjuncts, no exceptions)

A completed call `(f a₁ … aₙ)` is admitted for lookup and for commit iff:

- **(a) ground + canonical.** `atom_has_vars(call) == false` and
  `canonical_ground_call_key` succeeds. Ground-only means the answer is a
  function of the call term alone, not of an ambient substitution.
- **(b) completed.** The Need outcome state is `VALUE` or `STABLE_FAULT`
  (`prime_need_fault_is_completed`, eval.c:~11973). `EMPTY`/`EVALUATING`
  (retryable/in-progress) are never cached.
- **(c) pure receipt delta.** `prime_need_receipt_delta_is_pure(before, after)`
  — the events this call added carry no READ_STATE / WRITE_STATE / RESAMPLE.
  A state read makes the result contingent on mutable content; a write is an
  effect a replay would silently drop; a resample is one draw, not a function.
- **(d) exact revision match.** Every space consulted during the call is
  captured as a `SpaceReadToken {space, revision}` (space.h:198-206) at
  evaluation, and *every* token must still match the space's current revision
  at both lookup and replay. A single mutation (`space_bump_revision`,
  space.c:1799) invalidates the entry — mirror the negative-example discipline
  in table_store.h.
- **(e) whole-bag replay.** The stored entry is the entire answer *multiset*
  (with multiplicities and per-occurrence `AnswerRef`s), replayed whole. Never
  a set-collapse: `(superpose (a a b))` must replay three occurrences, and the
  distinct rule-occurrence receipts over one producer outcome must survive
  (this is exactly what `test-prime-shared-cause-probability` checks).

Conjunctive with no exceptions: any conjunct failing = evaluate normally, do
not cache. A false miss forfeits a cache entry (cheap); a false hit is
unsound (forbidden). Every predicate is biased to the safe side.

## 3. The two new predicates (LANDED)

Both are the only genuinely new logic; everything else composes existing
substrate.

- `prime_need_receipt_delta_is_pure(before, after)` — src/prime_need.c
  (after `prime_need_receipt_event_at`), declared in src/prime_need.h.
  Set-difference over the immutable event-DAG frames: an event in `after` not
  reachable in `before`, of kind READ_STATE/WRITE_STATE/RESAMPLE, taints the
  call. Ambient effects already in `before` do not veto. Conservative on
  allocation failure (returns impure). Reuses `collect_events` /
  `frames_contains`.
- `canonical_ground_call_key(call, arena, &canonical, &hash)` —
  src/table_store.c, declared in src/table_store.h. Ground-only guard;
  `term_universe_canonicalize_atom` (identity for ground terms) then
  `atom_hash` — documented interning-compatible (term_universe.c:1176-1180).
  Callers bucket by `hash`, disambiguate collisions with `atom_eq` on
  `canonical`.

## 4. Table entry and operations (TO BUILD)

Add ground-call fields/ops to `TableStore`, gated on
`mode == CETTA_TABLE_MODE_GROUND_CALL` (parallel to the VARIANT gate that
every existing public fn already carries):

```
GroundCallEntry {
    Atom     *canonical_call;    // key; compare with atom_eq on hash bucket
    uint64_t  key_hash;
    SpaceReadToken *tokens;      // every space consulted; len, cap
    uint32_t  token_len;
    TableStoredAnswers answers;  // whole bag of AnswerRef (reuse existing type)
    bool      stable_fault;      // outcome was STABLE_FAULT vs VALUE
}
```

- `table_store_ground_lookup(store, call, tokens_now, out_bag)` — find the
  entry by (hash, atom_eq); verify every stored token still matches current
  revision; on full match replay the whole `answers` bag. On any token
  mismatch: miss (and the stale entry may be reused as a storage slot, never
  as a lookup hit — same discipline as VARIANT).
- `table_store_ground_commit(store, call, tokens, answers, stable_fault)` —
  admit only under §2; store the whole bag + tokens. Idempotent: re-committing
  an identical key at an identical revision must not duplicate (no-dup gate).

Token capture (which spaces were consulted) is the one wiring subtlety: v1
captures the set of spaces read during the call via the receipt's
`OBSERVE_CELL`/`READ_STATE`-adjacent machinery, or — simpler and strictly
conservative for v1 — captures every space currently live in the episode and
requires all their revisions unchanged. Start conservative; narrow later with
a documented frontier change (never silently).

## 5. Boundary hook (TO BUILD)

Consult in `metta_eval_bind` (src/eval.c ~:14342), immediately before the
`metta_call` equation dispatch, gated on
`active_search_table_mode() == CETTA_TABLE_MODE_GROUND_CALL` and a ground
head-with-equations call:

1. `canonical_ground_call_key` → (canonical, hash). If not ground: skip hook.
2. `table_store_ground_lookup`. On hit: emit the replayed bag, return.
3. On miss: snapshot `before` receipt + live space revisions, evaluate
   normally (existing path), then at completion check §2(b–e) and
   `table_store_ground_commit` if admitted.

The hook is a behavior change to the hot call path, so it ships with the
both-paths differential (mode off vs on, bag-identical) and the three planted
mutations (below) before any default consideration.

## 6. The switch

Extend `active_search_table_mode()` (src/eval.c:1464) to map
`pragma! search-table-mode ground-call` → `CETTA_TABLE_MODE_GROUND_CALL`, and
honor an env override `CETTA_TABLE_MODE=ground-call`. Default remains
`NONE`/off. No default flip — promotion is a separate, explicit decision.

## 7. Test plan (TO BUILD)

Golden set `tests/prime/test_ground_call_*.metta` mirroring the seven
`test_table_*` patterns: hit/miss equivalence; invalidation on add/remove
(revision bump); no-dup; stale-reuse rejection; duplicate-multiplicity
preservation; STABLE_FAULT replay; retryable-not-cached.

Three planted mutations, each caught by an off-vs-on differential:
(i) bag→set collapse in replay (caught by the multiplicity golden and the
shared-cause probability gate); (ii) replay across a revision bump (caught by
the invalidation goldens); (iii) admission of a RESAMPLE-tainted call (caught
by a resample-in-call differential — `prime_need_receipt_delta_is_pure`
returning true would be the bug).

Two existing semantic gates that must never trip: `test-prime-shared-cause-
probability` (1/2 vs naive-OR 3/4, from receipts) and the equation-call
sharing tournament + drift guard (Makefile:1358-1372) — any deliberate
frontier change to `*.current.expected` gets a written rationale.

Performance: on a pinned non-sanitizer core binary (record sha256),
`MAM_JETTA_SCALE=paper MAM_JETTA_CASES=fibonacci
scripts/bench_mam_jetta_suite.sh` — fib(30) correct and practical (seconds),
the 10/15/20/25/30 curve monotone-sane, all five mam_jetta cases green at
smoke for he and prime.

## 8. Why this is call-by-need-sound

A memo hit substitutes a previously observed whole answer bag for
re-evaluation. Under conjuncts (a)–(e) that substitution is observationally
identical: the call is a pure function of its ground term (a, c), it ran to a
committed outcome (b), no mutation has occurred to any consulted space since
(d), and every occurrence and its receipt is preserved (e). Call-time choice
is respected because the bag — not a single winner — is replayed, and the
per-occurrence receipts keep dependence structure intact for the downstream
probability computation.
