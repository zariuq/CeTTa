import Cetta.GC.SymbolRepresentation

set_option autoImplicit false

/-!
An isolated contract for collectible retained keys. Handles carry a slot and
generation; strong clients must contribute a reference before borrowing bytes.
This is not CeTTa's current SymbolId ABI. C registration coverage, reference
count accounting, table-instance leases and worker synchronization remain
implementation obligations. Generations have an explicit finite limit.
-/

namespace Cetta.GC.Handles

abbrev Bytes := Representation.Bytes

structure Handle where
  slot : Nat
  generation : Nat
  deriving DecidableEq

structure Slot where
  generation : Nat
  references : Nat
  payload : Option Bytes

def observeSlot (s : Slot) (generation : Nat) : Option Bytes :=
  if s.generation = generation ∧ s.references > 0 then s.payload else none

def sweepSlot (s : Slot) : Slot :=
  if s.references = 0 then { s with payload := none } else s

/-- Sweeping cannot invalidate a counted borrow. That every actual client is
    counted is a separate host premise, not inferred from this theorem. -/
theorem counted_borrow_preserved (s : Slot) (generation : Nat)
    (held : s.references > 0) :
    observeSlot (sweepSlot s) generation = observeSlot s generation := by
  simp [sweepSlot, Nat.ne_of_gt held]

theorem released_payload_removed (s : Slot) (released : s.references = 0) :
    (sweepSlot s).payload = none := by simp [sweepSlot, released]

def allocateSlot (s : Slot) (bytes : Bytes) (limit : Nat) : Option Slot :=
  if s.payload = none ∧ s.references = 0 ∧ s.generation < limit then
    some ⟨s.generation + 1, 1, some bytes⟩
  else none

/-- Reusing a slot changes its generation, so a stale handle cannot observe
    another spelling through it. No wraparound is modeled or permitted. -/
theorem reused_slot_rejects_stale (s : Slot) (bytes : Bytes) (limit : Nat)
    (fresh : Slot) (allocated : allocateSlot s bytes limit = some fresh) :
    observeSlot fresh s.generation = none := by
  unfold allocateSlot at allocated
  split at allocated
  · cases allocated
    simp [observeSlot]
  · simp at allocated

theorem generation_exhaustion_refuses (s : Slot) (bytes : Bytes) (limit : Nat)
    (exhausted : limit ≤ s.generation) : allocateSlot s bytes limit = none := by
  simp [allocateSlot, Nat.not_lt.mpr exhausted]

theorem allocation_keeps_generation_bounded (s : Slot) (bytes : Bytes)
    (limit : Nat) (fresh : Slot)
    (allocated : allocateSlot s bytes limit = some fresh) :
    fresh.generation ≤ limit := by
  unfold allocateSlot at allocated
  split at allocated
  · rename_i available
    cases allocated
    exact available.2.2
  · simp at allocated

example : observeSlot ⟨2, 1, some [98]⟩ 1 = none := by decide
example : allocateSlot ⟨7, 0, none⟩ [99] 7 = none := by decide

#print axioms counted_borrow_preserved
#print axioms reused_slot_rejects_stale
#print axioms generation_exhaustion_refuses
#print axioms allocation_keeps_generation_bounded

end Cetta.GC.Handles
