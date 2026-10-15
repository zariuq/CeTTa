import Mettapedia.GSLT.Dynamics.StoreReachability

/-!
# Advanced collection theory: publication locality, share-collapse, ephemerons

Three open-problem cores from the collector-architecture literature, sealed at
the store-model level.

* **Publication-law soundness** (`local_reach_complete`): under the
  promote-before-publication discipline — no edge from outside the nursery
  into the nursery — a thread's local reachability (its own roots, its own
  cells) coincides EXACTLY with global reachability on nursery cells.  Local
  minor collection is therefore not merely safe but complete: it collects
  precisely what a global collection would.  This is the actor-local
  collection insight (capability runtimes) stated and proved store-
  theoretically, with the discipline as the sole hypothesis.
* **Share-collapsing evacuation** (`collapse_preserves_bag`): a collector may
  merge structurally-equal cells (hash-consing) — a NON-injective renaming —
  while the semantic bag observation over memberships is preserved, provided
  the collapse respects payloads on live cells.  Physical sharing and
  semantic multiplicity are independent: the bag lives on membership edges,
  not on object count.
* **Ephemeron marking as a least fixed point** (`ReachW` + `reachW_least`):
  conditional (table ∧ key ⇒ value) edges make liveness a genuine lfp.  The
  inductive definition IS the sequential specification; `reachW_least` is
  the convergence target any implementation — including a future parallel
  marker — must meet.  `weak_only_not_retained` seals intern-table
  no-retention: a cell held only by weak table slots is not live.

The concurrent/parallel halves of these problems (racing publication,
parallel ephemeron phases, concurrent thunk transitions) remain open and are
NOT claimed here; these theorems are the sequential contracts those designs
must refine.
-/

namespace Mettapedia.GSLT.Dynamics.StoreReachability

variable {Cell V : Type}

/-! ## Publication-law soundness: local collection is exact -/

/-- The promote-before-publication discipline, as a property of one global
store: every edge INTO the nursery originates in the nursery.  Mature and
foreign cells never point at unpublished data — publication promotes first. -/
def NurseryClosed (S : Store Cell) (nursery : Cell → Prop) : Prop :=
  ∀ c c', S.pointsTo c c' → nursery c' → nursery c

/-- The thread-local view: the nursery's own cells, its own edges, and only
the roots that live in the nursery. -/
def localView (S : Store Cell) (nursery : Cell → Prop) : Store Cell where
  pointsTo c c' := S.pointsTo c c' ∧ nursery c ∧ nursery c'
  root c := S.root c ∧ nursery c

/-- **Publication-law soundness.**  Under the discipline, a nursery cell is
globally reachable iff it is reachable in the thread-local view.  Minor
collection from thread-local roots is exact — it reclaims precisely the
globally dead nursery cells, with no global scan and no remembered inbound
set (there are no inbound edges to remember). -/
theorem local_reach_complete (S : Store Cell) (nursery : Cell → Prop)
    (hclosed : NurseryClosed S nursery) (c : Cell) (hc : nursery c) :
    Reach S c ↔ Reach (localView S nursery) c := by
  constructor
  · intro h
    revert hc
    induction h with
    | root hr => intro hcc; exact Reach.root ⟨hr, hcc⟩
    | @step a b _ he ih =>
      intro hb
      have hna : nursery a := hclosed a b he hb
      exact Reach.step (ih hna) ⟨he, hna, hb⟩
  · intro h
    clear hc
    induction h with
    | root hr => exact Reach.root hr.1
    | step _ he ih => exact Reach.step ih he.1

/-- Corollary in collection form: the local view's garbage on nursery cells
is exactly global garbage. -/
theorem local_garbage_exact (S : Store Cell) (nursery : Cell → Prop)
    (hclosed : NurseryClosed S nursery) (c : Cell) (hc : nursery c) :
    garbage S c ↔ garbage (localView S nursery) c := by
  unfold garbage
  exact not_congr (local_reach_complete S nursery hclosed c hc)

/-! ## Share-collapsing evacuation preserves the bag observation -/

/-- A store with payloads and a membership bag: semantic multiplicity lives
on the membership LIST (duplicates meaningful), never on object count. -/
structure BagStore (Cell V : Type) where
  store : Store Cell
  payload : Cell → V
  members : List Cell
  members_rooted : ∀ c ∈ members, Reach store c

/-- A share-collapse: a renaming that may MERGE cells (hash-consing), given a
payload function on the target that agrees with the source on live cells.
Injectivity is deliberately NOT assumed. -/
structure Collapse (S : BagStore Cell V) (Cell' : Type) where
  φ : Cell → Cell'
  payload' : Cell' → V
  agrees : ∀ c, Reach S.store c → payload' (φ c) = S.payload c

/-- The bag observation: the list of member payloads (read as a bag —
permutation-invariant consumers only). -/
def bagObs (S : BagStore Cell V) : List V := S.members.map S.payload

/-- **Share-collapse preservation.**  Evacuating the membership bag through a
collapsing renaming preserves the bag observation exactly: ten memberships of
one hash-consed node observe identically to ten distinct copies.  Physical
liveness count, semantic multiplicity, and sharing degree are independent. -/
theorem collapse_preserves_bag (S : BagStore Cell V) {Cell' : Type}
    (K : Collapse S Cell') :
    (S.members.map K.φ).map K.payload' = bagObs S := by
  unfold bagObs
  rw [List.map_map]
  exact List.map_congr_left (fun c hc =>
    K.agrees c (S.members_rooted c hc))

/-! ## Ephemeron reachability as a least fixed point -/

/-- A store with ephemeron edges: `eph t k v` means table `t` holds `v`
under key `k` — `v` is retained only when BOTH the table and the key are
live.  Plain weak slots are the degenerate table-only case and generate no
retention at all (they simply are not edges). -/
structure WStore (Cell : Type) where
  strong : Store Cell
  eph : Cell → Cell → Cell → Prop

/-- Liveness with ephemerons: the least relation closed under roots, strong
edges, and the conditional ephemeron rule. -/
inductive ReachW (W : WStore Cell) : Cell → Prop
  | root {c} : W.strong.root c → ReachW W c
  | step {c c'} : ReachW W c → W.strong.pointsTo c c' → ReachW W c'
  | eph {t k v} : ReachW W t → ReachW W k → W.eph t k v → ReachW W v

/-- Strong reachability always suffices. -/
theorem reachW_of_reach (W : WStore Cell) {c : Cell}
    (h : Reach W.strong c) : ReachW W c := by
  induction h with
  | root hr => exact ReachW.root hr
  | step _ he ih => exact ReachW.step ih he

/-- **The lfp specification**: `ReachW` is the LEAST set closed under the
three rules.  Any marking implementation — sequential or parallel — is
correct iff its computed live set is exactly such a closed set contained in
every other closed set; this theorem is the convergence target. -/
theorem reachW_least (W : WStore Cell) (X : Cell → Prop)
    (hroot : ∀ c, W.strong.root c → X c)
    (hstep : ∀ c c', X c → W.strong.pointsTo c c' → X c')
    (heph : ∀ t k v, X t → X k → W.eph t k v → X v) :
    ∀ c, ReachW W c → X c := by
  intro c h
  induction h with
  | root hr => exact hroot _ hr
  | step _ he ih => exact hstep _ _ ih he
  | eph _ _ hedge iht ihk => exact heph _ _ _ iht ihk hedge

/-- **Intern-table no-retention.**  A cell with no root, no strong in-edge,
and appearing in no ephemeron conclusion is not live: weak table slots alone
retain nothing.  (The intern table's entries are exactly such cells once
their last strong holder dies.) -/
theorem weak_only_not_retained (W : WStore Cell) (c : Cell)
    (hnoroot : ¬ W.strong.root c)
    (hnoedge : ∀ a, ¬ W.strong.pointsTo a c)
    (hnoeph : ∀ t k, ¬ W.eph t k c) :
    ¬ ReachW W c := by
  intro h
  cases h with
  | root hr => exact hnoroot hr
  | step _ he => exact hnoedge _ he
  | eph _ _ hedge => exact hnoeph _ _ hedge

/-- Ephemerons are strictly stronger than strong-only reachability: the
conditional rule fires only when the key is independently live.  Negative
half: with a dead key, the value stays dead. -/
theorem eph_dead_key_dead_value (W : WStore Cell) (t k v : Cell)
    (hedge : W.eph t k v)
    (hkey_dead : ¬ ReachW W k)
    (hnoroot : ¬ W.strong.root v)
    (hnoedge : ∀ a, ¬ W.strong.pointsTo a v)
    (honly : ∀ t' k', W.eph t' k' v → t' = t ∧ k' = k) :
    ¬ ReachW W v := by
  intro h
  cases h with
  | root hr => exact hnoroot hr
  | step _ he => exact hnoedge _ he
  | eph hkt hkk hedge' =>
    obtain ⟨rfl, rfl⟩ := honly _ _ hedge'
    exact hkey_dead hkk

end Mettapedia.GSLT.Dynamics.StoreReachability
