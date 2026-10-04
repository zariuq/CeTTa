import Mathlib.Tactic

set_option autoImplicit false

/-!
The counted-leaf protocol in `registered_symbols.c`. Canonicalization indexes
are weak; handles and borrows contribute holds. Tokens increase monotonically
and stop before finite-width exhaustion. The C refinement obligations are:
every permitted byte read owns a handle or borrow, every successful acquisition
has one release, and mutex-protected publication implements these transitions.
The model does not discharge ownership of legacy bare SymbolId readers.
-/
namespace Cetta.GC.RegisteredSymbols

abbrev Bytes := List UInt8

structure Entry where
  spelling : Bytes
  holds : Nat
  deriving DecidableEq

def borrow (e : Entry) : Entry := { e with holds := e.holds + 1 }

def release (e : Entry) : Option Entry :=
  if e.holds ≤ 1 then none else some { e with holds := e.holds - 1 }

/-- The borrow's own hold survives releasing the handle that opened it. -/
theorem borrow_survives_owner_release (e : Entry) (owned : 0 < e.holds) :
    release (borrow e) = some e := by
  simp only [release, borrow]
  have h : ¬ e.holds + 1 ≤ 1 := by omega
  simp [h]

theorem last_release_reclaims (e : Entry) (last : e.holds = 1) :
    release e = none := by simp [release, last]

theorem other_hold_preserves_spelling (e : Entry) (other : 1 < e.holds) :
    (release e).map Entry.spelling = some e.spelling := by
  simp [release, Nat.not_le.mpr other]

structure Tokens where
  next : Nat
  limit : Nat

def fresh (s : Tokens) : Option (Nat × Tokens) :=
  if s.next < s.limit then some (s.next, { s with next := s.next + 1 })
  else none

/-- Retired identities require no retained slot history. Monotone allocation
    separates every old token from every subsequent fresh allocation. -/
theorem retired_token_not_reissued (s : Tokens) (old : Nat)
    (past : old < s.next) (token : Nat) (after : Tokens)
    (allocated : fresh s = some (token, after)) : old ≠ token := by
  unfold fresh at allocated
  split at allocated
  · cases allocated
    omega
  · simp at allocated

theorem finite_exhaustion_refuses (s : Tokens) (full : s.limit ≤ s.next) :
    fresh s = none := by simp [fresh, Nat.not_lt.mpr full]

theorem fresh_preserves_bound (s : Tokens) (token : Nat) (after : Tokens)
    (allocated : fresh s = some (token, after)) :
    after.next ≤ after.limit ∧ s.next < after.next := by
  unfold fresh at allocated
  split at allocated
  · rename_i available
    cases allocated
    constructor <;> simp_all <;> omega
  · simp at allocated

/-- Reserved buckets are charged separately. This bound requires successful
    index compaction; on allocation failure the C gauges report the larger
    reservation. Allocator headers and RSS are outside these byte counts. -/
def retainedBytes (payload entries buckets nodeBytes pointerBytes base : Nat) : Nat :=
  payload + entries * nodeBytes + 2 * buckets * pointerBytes + base

theorem accounted_space_bound
    (payload entries buckets nodeBytes pointerBytes base minimum : Nat)
    (indexBound : buckets ≤ minimum + 4 * entries) :
    retainedBytes payload entries buckets nodeBytes pointerBytes base ≤
      payload + entries * nodeBytes +
        2 * (minimum + 4 * entries) * pointerBytes + base := by
  unfold retainedBytes
  gcongr

/-- Growth at a full index keeps its reservation linear in live keys. -/
theorem grow_index_bound (entries capacity minimum : Nat)
    (full : capacity ≤ entries) :
    2 * capacity ≤ minimum + 4 * (entries + 1) := by omega

/-- One-key release followed by halving maintains the charged reservation.
    The implementation keeps the minimum bucket array instead of shrinking it. -/
theorem shrink_index_bound (entries capacity minimum : Nat)
    (prior : capacity ≤ minimum + 4 * (entries + 1))
    (minimum_large : 4 ≤ minimum) :
    capacity / 2 ≤ minimum + 4 * entries := by omega

#print axioms borrow_survives_owner_release
#print axioms last_release_reclaims
#print axioms retired_token_not_reissued
#print axioms finite_exhaustion_refuses
#print axioms accounted_space_bound

end Cetta.GC.RegisteredSymbols
