import Cetta.GC.SymbolCollection

set_option autoImplicit false

/-!
# First-phase symbol lifetimes

The birth API keeps legacy intern results owned, promotes data monotonically,
and never reuses an ID. `history` is ghost allocation history, not retained C
text. Retirement below models the later sweep; it is not enabled by P0.
Natural IDs model the C allocator only before its checked exhaustion boundary.
-/

namespace Cetta.GC

inductive SymbolLifetime
  | owned | data | retired
  deriving DecidableEq, Repr

structure LifetimeStore where
  nextId : Nat
  history : Nat → Option String
  lifetime : Nat → SymbolLifetime

inductive LifetimeAction
  | allocate (name : String) (owned : Bool)
  | pin (id : Nat)
  | retire (marked : Nat → Bool)
  | shadow

def lifetimeStep (s : LifetimeStore) : LifetimeAction → LifetimeStore
  | .allocate name owned =>
      { nextId := s.nextId + 1
        history := fun id => if id = s.nextId then some name else s.history id
        lifetime := fun id => if id = s.nextId then
          (if owned then .owned else .data) else s.lifetime id }
  | .pin target =>
      { s with lifetime := fun id =>
          if id = target ∧ s.lifetime id = .data then .owned else s.lifetime id }
  | .retire marked =>
      { s with lifetime := fun id =>
          if s.lifetime id = .data ∧ marked id = false then .retired
          else s.lifetime id }
  | .shadow => s

def runLifetime (actions : List LifetimeAction) (s : LifetimeStore) : LifetimeStore :=
  actions.foldl lifetimeStep s

theorem lifetime_next_monotone (s : LifetimeStore) (a : LifetimeAction) :
    s.nextId ≤ (lifetimeStep s a).nextId := by
  cases a <;> simp [lifetimeStep]

theorem lifetime_old_name_preserved (s : LifetimeStore) (a : LifetimeAction)
    (id : Nat) (allocated : id < s.nextId) :
    (lifetimeStep s a).history id = s.history id := by
  cases a <;> simp [lifetimeStep, Nat.ne_of_lt allocated]

/-- No trace can give an already allocated ID a different spelling. This does
    not certify root completeness or the lifetime of a borrowed C pointer. -/
theorem lifetime_trace_never_rebinds (actions : List LifetimeAction)
    (s : LifetimeStore) (id : Nat) (allocated : id < s.nextId) :
    (runLifetime actions s).history id = s.history id := by
  induction actions generalizing s with
  | nil => rfl
  | cons a rest ih =>
      have next := Nat.lt_of_lt_of_le allocated (lifetime_next_monotone s a)
      simpa [runLifetime] using
        (ih (lifetimeStep s a) next).trans (lifetime_old_name_preserved s a id allocated)

theorem owned_stays_owned (s : LifetimeStore) (a : LifetimeAction)
    (id : Nat) (allocated : id < s.nextId) (owned : s.lifetime id = .owned) :
    (lifetimeStep s a).lifetime id = .owned := by
  cases a <;> simp [lifetimeStep, Nat.ne_of_lt allocated, owned]

theorem retired_stays_retired (s : LifetimeStore) (a : LifetimeAction)
    (id : Nat) (allocated : id < s.nextId) (dead : s.lifetime id = .retired) :
    (lifetimeStep s a).lifetime id = .retired := by
  cases a <;> simp [lifetimeStep, Nat.ne_of_lt allocated, dead]

/-- A stale compare-only cache key cannot match the next fresh allocation.
    This says nothing about a cache's strong value roots. -/
theorem stale_key_misses_fresh_id (s : LifetimeStore) (id : Nat)
    (allocated : id < s.nextId) : id ≠ s.nextId := Nat.ne_of_lt allocated

theorem shadow_is_observationally_identity (s : LifetimeStore) :
    lifetimeStep s .shadow = s := rfl

def runPolls (boundaries : List CollectionBoundary) (c : CollectionControl) :
    CollectionControl := boundaries.foldl (fun state b => pollCollection b state) c

theorem no_request_no_collection (boundaries : List CollectionBoundary)
    (completed : Nat) : runPolls boundaries ⟨false, completed⟩ = ⟨false, completed⟩ := by
  induction boundaries with
  | nil => rfl
  | cons b rest ih => simpa [runPolls, pollCollection] using ih

/-- A latched request is serviced in the supplied finite window if that
    window contains an eligible boundary. Pending choices are not a guard.
    Eligibility still assumes complete roots, quiescence and protected borrows. -/
theorem pending_eventually_serviced (boundaries : List CollectionBoundary)
    (c : CollectionControl)
    (eligible : ∃ b ∈ boundaries, readyToCollect b = true) :
    runPolls boundaries (requestCollection c) = ⟨false, c.completed + 1⟩ := by
  induction boundaries with
  | nil => simp at eligible
  | cons b rest ih =>
      by_cases ready : readyToCollect b = true
      · simp only [runPolls, List.foldl_cons, requestCollection, pollCollection, ready,
          Bool.and_self, ite_true]
        exact no_request_no_collection rest (c.completed + 1)
      · have tail : ∃ boundary ∈ rest, readyToCollect boundary = true := by
          obtain ⟨boundary, member, enabled⟩ := eligible
          rcases List.mem_cons.mp member with same | later
          · subst boundary
            exact (ready enabled).elim
          · exact ⟨boundary, later, enabled⟩
        simpa [runPolls, requestCollection, pollCollection, ready] using ih tail

/-- Conditional payload bound at a poll: the allocation margin is exceeded
    by at most `overshoot` before reaching that poll, and the previous live
    data payload is at most `liveMax`. Neither pinned text nor ID metadata is
    included. These scheduling assumptions must be measured in the C host. -/
theorem data_payload_poll_bound (live allocated resident margin overshoot liveMax : Nat)
    (accounted : resident ≤ live + allocated)
    (allocationWindow : allocated ≤ max margin live + overshoot)
    (boundedLive : live ≤ liveMax) :
    resident ≤ 2 * liveMax + margin + overshoot := by
  rcases Nat.le_total margin live with bound | bound
  · rw [Nat.max_eq_right bound] at allocationWindow
    omega
  · rw [Nat.max_eq_left bound] at allocationWindow
    omega

private def tiny : LifetimeStore :=
  { nextId := 1, history := fun _ => none, lifetime := fun _ => .retired }

example : (runLifetime [.allocate "prompt" false, .pin 1,
    .retire (fun _ => false)] tiny).lifetime 1 = .owned := by decide

example : (runLifetime [.allocate "prompt" false, .retire (fun _ => false),
    .pin 1, .allocate "prompt" false] tiny).lifetime 1 = .retired := by decide

example : (runLifetime [.allocate "prompt" false, .retire (fun _ => false),
    .allocate "prompt" false] tiny).history 2 = some "prompt" := by decide

example : runPolls [⟨false, true, 0⟩, ⟨true, true, 0⟩]
    ⟨true, 0⟩ = ⟨false, 1⟩ := by decide

#print axioms lifetime_trace_never_rebinds
#print axioms owned_stays_owned
#print axioms pending_eventually_serviced
#print axioms data_payload_poll_bound

end Cetta.GC
