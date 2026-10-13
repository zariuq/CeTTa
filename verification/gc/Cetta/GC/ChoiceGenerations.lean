import Mettapedia.GSLT.Dynamics.StoreReachability
import Mathlib.Tactic

/-!
# Choice-aware generation transport and owner release

The concrete C obligations are explicit premises: roots include suspended
alternatives and rollback state, every permitted future read is reachable,
and one relocation transports edges and payloads. This model does not certify
the C visitor inventory. It also separates retained payload from capacity,
symbol metadata and leases; its byte bound is not a bound on process RSS.
-/

namespace Cetta.GC.ChoiceGenerations

open Mettapedia.GSLT.Dynamics.StoreReachability

structure Frontier (Cell : Type) where
  active : List Cell
  alternatives : List (List Cell)
  rollback : List Cell

def Frontier.roots (f : Frontier Cell) : List Cell :=
  f.active ++ f.alternatives.flatten ++ f.rollback

def rootedStore (edges : Cell → Cell → Prop) (f : Frontier Cell) : Store Cell :=
  ⟨edges, fun c => c ∈ f.roots⟩

variable {Cell Target Value : Type}
    {source : Store Cell} {target : Store Target}
    {move : Cell → Target} {before : Cell → Value} {after : Target → Value}

structure Transport (source : Store Cell) (target : Store Target)
    (move : Cell → Target) (before : Cell → Value) (after : Target → Value) where
  roots : ∀ c, source.root c → target.root (move c)
  edges : ∀ a b, source.pointsTo a b → target.pointsTo (move a) (move b)
  payload : ∀ c, Reach source c → after (move c) = before c

/-- Choices need not be empty: source root coverage is the condition. -/
theorem future_read_survives
    (t : Transport source target move before after)
    (canRead : Cell → Prop) (coverage : ∀ c, canRead c → Reach source c)
    {c : Cell} (read : canRead c) :
    Reach target (move c) ∧ after (move c) = before c := by
  have live := coverage c read
  exact ⟨live.map move t.roots t.edges, t.payload c live⟩

/-- A suspended branch contributes roots even with an active branch present. -/
theorem suspended_root_live (edges : Cell → Cell → Prop) (f : Frontier Cell)
    (branch : List Cell) (present : branch ∈ f.alternatives)
    {c : Cell} (member : c ∈ branch) : Reach (rootedStore edges f) c := by
  apply Reach.root
  change c ∈ f.active ++ f.alternatives.flatten ++ f.rollback
  simp only [List.mem_append]
  exact Or.inl (Or.inr (List.mem_flatten.mpr ⟨branch, present, member⟩))

/-- Ordered observations preserve duplicates; no set quotient is used. -/
theorem ordered_readout_preserved
    (t : Transport source target move before after)
    (reads : List Cell) (live : ∀ c ∈ reads, Reach source c) :
    (reads.map move).map after = reads.map before := by
  rw [List.map_map]
  apply List.map_congr_left
  intro c hc
  exact t.payload c (live c hc)

/-- Dropping alternatives for a cut commutes with relocating their roots. -/
theorem commit_prefix_preserved (move : Cell → Target)
    (branches : List (List Cell)) (keep : Nat) :
    (branches.map (List.map move)).take keep =
      (branches.take keep).map (List.map move) := by
  simp only [List.map_take]

def releaseMetadata (s : Store Cell) (metadata : Cell → Prop) : Store Cell where
  root c := s.root c ∧ ¬ metadata c
  pointsTo a b := s.pointsTo a b ∧ ¬ metadata a ∧ ¬ metadata b

/-- Releasing compiled metadata is safe only after every live export is
independent of it. Being a live program is not itself this certificate. -/
theorem metadata_release_preserves_reach (s : Store Cell) (metadata : Cell → Prop)
    (independent : ∀ c, Reach s c → ¬ metadata c) (c : Cell) :
    Reach (releaseMetadata s metadata) c ↔ Reach s c := by
  constructor
  · intro live
    induction live with
    | root rooted => exact .root rooted.1
    | step _ edge ih => exact .step ih edge.1
  · intro live
    induction live with
    | root rooted => exact .root ⟨rooted, independent _ (.root rooted)⟩
    | @step a b parent edge ih =>
        exact .step ih ⟨edge, independent a parent, independent b (.step parent edge)⟩

def majorThreshold (lastLive floor : Nat) : Nat := lastLive + max lastLive floor

/-- A polling interval needs an allocation-burst bound. Live storage alone
does not bound allocations made before the next eligible collection point. -/
theorem old_payload_bound (old lastLive limit floor burst : Nat)
    (liveBound : lastLive ≤ limit)
    (pollBound : old ≤ majorThreshold lastLive floor + burst) :
    old ≤ 2 * limit + floor + burst := by
  have slack : max lastLive floor ≤ limit + floor :=
    max_le (by omega) (by omega)
  unfold majorThreshold at pollBound
  omega

/-- Other owners, allocator slack and permanent metadata remain separately
accounted; no theorem here silently discards them. -/
theorem total_accounted_bound (old lastLive limit floor burst other : Nat)
    (liveBound : lastLive ≤ limit)
    (pollBound : old ≤ majorThreshold lastLive floor + burst) :
    old + other ≤ 2 * limit + floor + burst + other := by
  have h := old_payload_bound old lastLive limit floor burst liveBound pollBound
  omega

#print axioms future_read_survives
#print axioms suspended_root_live
#print axioms ordered_readout_preserved
#print axioms commit_prefix_preserved
#print axioms metadata_release_preserves_reach
#print axioms old_payload_bound
#print axioms total_accounted_bound

end Cetta.GC.ChoiceGenerations
