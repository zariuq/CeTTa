import Cetta.GC.SymbolHandles

set_option autoImplicit false

/-!
Isolated archive-generation contract for `experiments/epoch_regions.c`.
An active ordered projection is copied before publication. Old readers keep
their original generation through counted leases; a generation can be freed
only when it is retired and no counted reader remains.

The C obligation is larger than this model: every future reader must hold a
lease, copying must preserve each term's full observation, and publication
must be synchronized. A Space occurrence pin alone does not retain its
TermUniverse. Functions, mutable resources, plan leases, foreign borrows and
worker ownership are not certified by the fact-only experiment.
-/

namespace Cetta.GC.Regions

abbrev Bytes := Representation.Bytes

structure Archive where
  terms : Nat → Option Bytes
  rows : List Nat

def projection (archive : Archive) : List (Option Bytes) :=
  archive.rows.map archive.terms

/-- Repacking may change physical IDs. It must transport every occurrence in
    order, including duplicates, and preserve each observed binary value. -/
structure CopyRefinement (before after : Archive) (transport : Nat → Nat) : Prop where
  ordered : after.rows = before.rows.map transport
  values : ∀ id ∈ before.rows, after.terms (transport id) = before.terms id

theorem repacking_preserves_projection (before after : Archive)
    (transport : Nat → Nat) (copied : CopyRefinement before after transport) :
    projection after = projection before := by
  unfold projection
  rw [copied.ordered, List.map_map]
  exact List.map_congr_left copied.values

structure Region where
  generation : Nat
  readers : Nat
  active : Bool
  archive : Option Archive

def observeReader (r : Region) (generation : Nat) : Option (List (Option Bytes)) :=
  if r.generation = generation ∧ r.readers > 0 then
    r.archive.map projection
  else none

def retire (r : Region) : Region := { r with active := false }

def collectRegion (r : Region) : Region :=
  if r.active = false ∧ r.readers = 0 then { r with archive := none } else r

theorem retirement_preserves_old_reader (r : Region) (generation : Nat) :
    observeReader (retire r) generation = observeReader r generation := rfl

/-- This is conditional safety. An omitted reader count invalidates its
    premise; generation checking alone cannot replace ownership coverage. -/
theorem collection_preserves_counted_reader (r : Region) (generation : Nat)
    (held : r.readers > 0) :
    observeReader (collectRegion r) generation = observeReader r generation := by
  simp [collectRegion, Nat.ne_of_gt held]

/-- Once a retired generation's last reader is released, the next poll drops
    its archive. No zero-choice-point condition is involved. -/
theorem released_archive_collected_at_next_poll (r : Region)
    (retired : r.active = false) (released : r.readers = 0) :
    (collectRegion r).archive = none := by
  simp [collectRegion, retired, released]

theorem old_generation_cannot_read_replacement (r : Region) (old : Nat)
    (different : r.generation ≠ old) : observeReader r old = none := by
  simp [observeReader, different]

/-- Retained accounting includes old leased archives, not only the current
    facts. Allocator and index overhead must be included in `overhead` by the
    host. Bounding leases and allocation between polls is a separate duty. -/
theorem conditional_region_space_bound
    (retained activeBytes retiredBytes live slack readers regionCap overhead : Nat)
    (accounted : retained ≤ activeBytes + retiredBytes + overhead)
    (activeBound : activeBytes ≤ live + slack)
    (retiredBound : retiredBytes ≤ readers * regionCap) :
    retained ≤ live + slack + readers * regionCap + overhead := by omega

def canPublish (capacity retiredLeased : Nat) : Bool :=
  decide (retiredLeased + 1 < capacity)

theorem all_slots_held_refuses_publication (capacity retiredLeased : Nat)
    (held : capacity ≤ retiredLeased + 1) :
    canPublish capacity retiredLeased = false := by
  simp [canPublish, Nat.not_lt.mpr held]

#print axioms repacking_preserves_projection
#print axioms collection_preserves_counted_reader
#print axioms released_archive_collected_at_next_poll
#print axioms conditional_region_space_bound
#print axioms all_slots_held_refuses_publication

end Cetta.GC.Regions
