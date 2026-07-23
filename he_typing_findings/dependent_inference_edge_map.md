# he-prime regrounded typer — dependent-inference edge map

*2026-07-15 — Oruži (Fable). The concrete spec for the #1 he-prime work item.*

## Finding

The regrounded `type-of` (WB `he_typing.c`, he-prime) does **simple arrow application**
correctly but does **NOT perform dependent-codomain instantiation**. On any term whose
type requires substituting argument bindings into a dependent codomain, it returns an
unreduced STRUCTURAL PRODUCT where native HE `get-type` COMPUTES the instantiated type.

| Term | native get-type | regrounded type-of | verdict |
|---|---|---|---|
| `(TutS TutZ)` | `TutNat` | `(type-set TutNat)` | AGREE (non-dependent) |
| `(TutIndex TutFZ (TutCons "a" TutNil))` | `String` | structural product (uninstantiated) | DIVERGE |
| `(TutRefl TutZ)` | `(TutEq TutNat TutZ TutZ)` | `(type-set ((-> (: $x $t) (TutEq $t $x $x)) TutNat))` | DIVERGE |
| `(TutSuccCong (TutRefl TutZ))` | `(TutEq TutNat (TutS TutZ) (TutS TutZ))` | structural product | DIVERGE |
| `(Cons 0 (Cons 1 Nil))` | `(Vec Number (S (S Z)))` | structural product | DIVERGE |

## Precise diagnosis (tractable — NOT "build Σ/Id/ι from scratch")

Typing `(f a1 a2)` where `f : (-> D1 D2 Cod)`:
- native unifies each `ai`'s type against `Di`, collects bindings INCLUDING value-level
  ones for dependent params (e.g. `$x := TutZ`, `$t := TutNat`, `$n := (TutS TutZ)`),
  and substitutes them into `Cod`, then reduces.
- regrounded returns the raw `(Cod-as-arrow arg1 arg2 …)` application WITHOUT that
  substitution.

Every corpus divergence (Fin indexing, Refl, congruence, Vec length) is THIS ONE missing
mechanism: **Π-elimination with dependent substitution into the codomain.** Native HE
already implements it — `src/space.c` `get_atom_types` (3286-3345) unifies arg types
against domains via `match_types_builder` and propagates bindings; the codomain
instantiation is the piece to port into `he_typing.c`'s `type_of_application`.

## The #1 he-prime work item (spec)

Extend regrounded `type_of` so an application types by:
1. inferring each argument's type,
2. unifying it against the corresponding domain pattern (respecting dependent binders
   `(: $x D)` — bind BOTH the type var and the value var),
3. applying the collected substitution to the codomain,
4. reducing the instantiated codomain (β+δ already present; recursive constructor indices
   like the Vec length need the substitution applied through the recursion).

Do this WITHOUT abandoning the census fixes: keep type-of total (structural product only
when the head is genuinely not an applicable function), keep three-valued verdicts, keep
`check-typing` as the reject gate. This closes the gap to "at least as good as native HE
on dependent types" — the precondition for adopting the regrounded typer as he-prime's.

## Still open beyond this (do NOT conflate)
- C↔Lean correspondence theorem (owed).
- Certificate generation (checking-only today).
- Nat-refinement for negative indices (open in BOTH native and regrounded — shared frontier).
- True Σ/Id type FORMERS (distinct from the elimination-substitution gap above).

## Repro
Build WB he_typing.c into a CeTTa scratch tree (BUILD=core); append paired
`!(get-type T)` / `!(type-of &self (quote T))` to
`tests/profile_he_prime_dtt_tutorial_ladder.metta`, run `--profile he-prime --lang he`.
