# HE typing edge-exploration & PeTTa port — differential findings

*2026-07-15 — Oruži (Fable). Read-only differential; no edits to main CeTTa source.*

Object: WB's regrounded HE type checker (`he_typing.c`, he-prime profile) stress-tested against the
full ~40-file typed corpus and the upstream HE oracle, plus a port of PeTTa's distinctive typed patterns.
Regrounded ops: `type-of` (inference), `check-typing` (checking), `is-consistent` / `type-consistency-kind`
(the named-edge consistency relation), `validate-typing`.

## Headline

1. **Zero native regression.** All 23 typed corpus files run byte-identical under the regrounded binary
   (ops are he-prime-profile-scoped). The regrounding is purely additive.
2. **Design confirmed coherent (Lindahl–Sagonas success typing).** `type-of` inference is TOTAL — on an
   ill-typed application it returns the structural PRODUCT type (e.g. `(not Z)` → `(type-set ((-> Bool Bool)
   Nat))`), never opaque `()`. `check-typing` is the PROVEN-REJECTION gate — `(not Z):Bool` → reject,
   `Z:Bool` → reject, `(not true):Bool` → accept. Inference over-approximates; checking proves rejection.
3. **The laundering block works** (census C2): `Nat ~ %Undefined%` accept(dynamic), `%Undefined% ~ Bool`
   accept(dynamic), but `Nat ~ Bool` REJECT — the dynamic edge does not compose.
4. **C3 is a CeTTa-native bug fix toward UPSTREAM, not just a soundness improvement** (the key 3-way result).

## The C3 three-way finding (highest value)

| Layer | `Atom` handling | Evidence |
|---|---|---|
| **Upstream HE** | ASYMMETRIC — `Atom` is an expected-position wildcard only; `replace_undefined_types` makes only `%Undefined%` symmetric | `hyperon-experimental/lib/src/metta/types.rs:78-88` (`*expected==%Undefined% \|\| meta.contains(expected)`), `:569` (replace touches only `%Undefined%`) |
| **CeTTa-native** | SYMMETRIC wildcard — bare `true` if EITHER side is `Atom` | `CeTTa/src/match.c:2065` (`atom_is_symbol_id(type1,atom) \|\| atom_is_symbol_id(type2,atom) => true`) |
| **Regrounded** | ASYMMETRIC — `Atom` as expected = top (accept); `Atom` as actual = REJECT | `is-consistent Nat Atom → he-accept top`; `is-consistent Atom Nat → he-reject`; native `(get-type (f a))` with `a:Atom`, `f:(-> Nat Nat)` → `[Nat]` (native wrongly accepts) |

CeTTa-native introduced the `Atom`-symmetric-wildcard; the regrounding **restores upstream's asymmetry**.
So C3 is not "beyond HE" — it is "CeTTa drifted from upstream, regrounding fixes it back." (`%Undefined%` is
symmetric in BOTH upstream and CeTTa — agree — so C2's laundering block IS a genuine correction beyond
upstream, whereas C3 is a fidelity restoration.)

## Differential classification (curated divergence terms)

| Term | Native | Regrounded | Class |
|---|---|---|---|
| `overloaded` (Nat+Bool) | `[Nat, Bool]` | `(type-set Nat Bool)` | AGREE (intersection) |
| `(Left 5)`, `Left:(-> %Undefined% Either)` | `[Either]` | `(type-set Either)` | AGREE (dynamic domain) |
| `(get-type (a b))` structural | `[(A B)]` | `(type-set (A B))` | AGREE (structural product) |
| `(not Z)` ill-typed app | `()` | `(type-set ((-> Bool Bool) Nat))` | INTENDED C7/C8 (legible product vs opaque empty) |
| `check (not Z):Bool` | — | REJECT | correct reject gate |
| `is-consistent Nat~Bool` | — | REJECT | C2 laundering block |
| `(f a)`, `a:Atom` → Nat param | `[Nat]` accept | Atom-as-actual REJECT | **C3 upstream-fidelity restoration** |

## PeTTa port (Phase C) — 4 new HE-dialect test files, all green

| Pattern | Status | File |
|---|---|---|
| C2/C3 multiple-type enumeration via `collapse` (`x:Letter,x:Buchstabe`; `blacksmith`→`(Sword Paperclip)`) | **PORTABLE** — works in native HE; corpus never enumerated (coverage gap now filled) | `test_typing_multiple_types_collapse.metta` |
| C5 structural product typing `(get-type (a b))→(A B)` | **PORTABLE** — native + regrounded AGREE | `test_typing_structural_product.metta` |
| C4 output-type-directed pruning | **SEMANTIC DIFFERENCE** — PeTTa prunes to `()`; HE reduces (`Tdefault`, dup per type). HE behavior pinned | `test_typing_output_pruning_divergence.metta` |
| C1 computed `get-type` (`(= (get-type $x) EvenNumber)`) | **CAPABILITY GAP** — HE's `get-type` is a sealed builtin; the override is IGNORED | `test_typing_computed_gettype_gap.metta` |

PeTTa is AHEAD on: user-overridable/computed `get-type` (refinement types from values). HE is AHEAD on:
dependent types (Fin/Vec/Σ/recursors) and explicit type-error diagnostics (`BadArgType`/`pragma! type-check`).

## Proposed corrections (drafts for review — NOT applied)

- **C3 (native `match.c:2065`)**: make `Atom` asymmetric to match upstream — `Atom` as expected accepts (top),
  `Atom` as actual matches only `Atom`/`%Undefined%`. This is a CeTTa-native fidelity fix, independently of
  adopting the regrounded module. (Split the four fused reasons in `match_types` per census C1.)
- **C1 census row**: mark `Atom`-symmetric as a CeTTa-native regression from upstream (was previously read
  as a shared HE trait).

## Census delta
No UNEXPECTED divergences (every divergence traces to C2/C3/C7/C8). One census sharpening: C3's "genuinely
unsound" is upgraded to "CeTTa-native regression from upstream (upstream is asymmetric)".

## Honest boundary
Upstream comparison was read from `types.rs` source (no runnable upstream binary present); the C3 asymmetry
is a source reading, not an executed 3-way. The 4 port files are blessed against the regrounded binary in
native mode; they are staged candidates for `CeTTa/tests/`, not yet moved into the suite.
