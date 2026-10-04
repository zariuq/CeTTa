import Mathlib.Order.Basic
import Mathlib.Logic.Relation
import Mathlib.Logic.Function.Basic

/-!
# Store reachability, collection, and deployment-stable garbage

The GSLT form of garbage collection, sealed at its semantic core.

* A store is a pointer graph with roots; **reachability** is the least
  relation closed under roots and edges; **garbage is its complement** — a
  derived judgment, never separately authored.
* **Collection is invisible**: restricting a store to its reachable part
  preserves reachability exactly (`reach_collect`), hence preserves every
  observation that factors through the reachable part, and is idempotent.
* **The deployment theorem** (the Stay condition): garbage is stable under
  deployment contexts — contexts that run alongside a configuration, own
  their own cells, and reference the configuration only through a declared
  live interface.  An unforgeable unreachable cell stays unreachable under
  every such composition (`deployment_cannot_resurrect`).
* **The reification counterexample**: composing WITHOUT the deployment
  closure — a context that forges a reference into the representation —
  resurrects garbage (`forging_resurrects`).  Ambient reification of the
  containing store is exactly what the closure forbids; reified stores must
  be granted explicitly.
* **Complete roots are load-bearing**: scanning with a root class omitted
  makes a live cell unreachable (`omitted_root_loses_cell`) — the semantic
  form of the nested-frame witness.

The step/collect naturality law and the tracing/refcount duality live in the
executable reference model and the native differential gates; this module
seals the reachability core those gates rely on.
-/

namespace Mettapedia.GSLT.Dynamics.StoreReachability

/-- A store: a pointer graph together with its root judgment. -/
structure Store (Cell : Type) where
  pointsTo : Cell → Cell → Prop
  root : Cell → Prop

variable {Cell : Type}

/-- Reachability: the least relation containing the roots and closed under
edges. -/
inductive Reach (S : Store Cell) : Cell → Prop
  | root {c : Cell} : S.root c → Reach S c
  | step {c c' : Cell} : Reach S c → S.pointsTo c c' → Reach S c'

/-- Relocating roots and every outgoing reference transports actual paths.
Payload equality alone cannot supply either of these graph conditions. -/
theorem Reach.map {TargetCell : Type} {source : Store Cell} {target : Store TargetCell}
    (relocate : Cell → TargetCell)
    (roots : ∀ c, source.root c → target.root (relocate c))
    (edges : ∀ a b, source.pointsTo a b → target.pointsTo (relocate a) (relocate b))
    {c : Cell} (live : Reach source c) : Reach target (relocate c) := by
  induction live with
  | root rooted => exact .root (roots _ rooted)
  | step _ edge ih => exact .step ih (edges _ _ edge)

/-- Complete destination roots and outgoing references reflect every
reachable target path into the source. Unrelated unreachable cells are allowed. -/
theorem Reach.reflect {TargetCell : Type} {source : Store Cell} {target : Store TargetCell}
    (relocate : Cell → TargetCell)
    (roots : ∀ c, target.root c → ∃ original, source.root original ∧ relocate original = c)
    (edges : ∀ a c, target.pointsTo (relocate a) c →
      ∃ original, source.pointsTo a original ∧ relocate original = c)
    {c : TargetCell} (live : Reach target c) :
    ∃ original, Reach source original ∧ relocate original = c := by
  induction live with
  | root rooted =>
      obtain ⟨original, present, same⟩ := roots _ rooted
      exact ⟨original, .root present, same⟩
  | @step a b _ edge ih =>
      obtain ⟨parent, parentLive, same⟩ := ih
      rw [← same] at edge
      obtain ⟨original, reference, equal⟩ := edges parent b edge
      exact ⟨original, .step parentLive reference, equal⟩

/-- Garbage is the complement of reachability — a derived judgment. -/
def garbage (S : Store Cell) (c : Cell) : Prop := ¬ Reach S c

/-- Collection restricts the graph to its reachable part.  Roots are kept:
they are reachable by construction. -/
def collect (S : Store Cell) : Store Cell where
  pointsTo c c' := Reach S c ∧ S.pointsTo c c'
  root := S.root

/-- **Collection is invisible to reachability**: the collected store reaches
exactly the cells the original store reaches. -/
theorem reach_collect (S : Store Cell) (c : Cell) :
    Reach (collect S) c ↔ Reach S c := by
  constructor
  · intro h
    induction h with
    | root hr => exact Reach.root hr
    | step _ he ih => exact Reach.step ih he.2
  · intro h
    induction h with
    | root hr => exact Reach.root hr
    | @step c c' hc he ih => exact Reach.step ih ⟨hc, he⟩

/-- Garbage is likewise preserved: collection deletes nothing observable. -/
theorem garbage_collect (S : Store Cell) (c : Cell) :
    garbage (collect S) c ↔ garbage S c := by
  unfold garbage
  exact not_congr (reach_collect S c)

/-- **Collection is idempotent.** -/
theorem collect_idempotent (S : Store Cell) :
    collect (collect S) = collect S := by
  unfold collect
  congr 1
  funext c c'
  exact propext ⟨fun ⟨hr, he⟩ => ⟨(reach_collect S c).mp hr, he.2⟩,
    fun ⟨hr, he⟩ => ⟨(reach_collect S c).mpr hr, hr, he⟩⟩

/-- A deployment context over a configuration `S`: it owns its own cells,
its roots lie among its own cells, its edges land only in its own cells or
the declared interface, the interface is live in `S`, and the
configuration's edges never emanate from context-owned cells (disjoint
ownership).  This is the formal content of "K[P] = Q | P with unforgeable
names": composition without representation access. -/
structure Deployment (S : Store Cell) where
  ctxPointsTo : Cell → Cell → Prop
  ctxRoot : Cell → Prop
  owns : Cell → Prop
  interface : Cell → Prop
  roots_owned : ∀ c, ctxRoot c → owns c
  edges_closed : ∀ c c', ctxPointsTo c c' → owns c' ∨ interface c'
  interface_live : ∀ c, interface c → Reach S c
  ownership_disjoint : ∀ c c', S.pointsTo c c' → ¬ owns c

/-- Parallel composition `Q | P`: union of graphs and roots. -/
def compose (S : Store Cell) (D : Deployment S) : Store Cell where
  pointsTo c c' := S.pointsTo c c' ∨ D.ctxPointsTo c c'
  root c := S.root c ∨ D.ctxRoot c

/-- Everything reachable in the composition is reachable in `S` or owned by
the context: a deployment can extend the world but cannot dig into it. -/
theorem compose_reach_cases (S : Store Cell) (D : Deployment S) (c : Cell)
    (h : Reach (compose S D) c) : Reach S c ∨ D.owns c := by
  induction h with
  | root hr =>
    rcases hr with hS | hctx
    · exact Or.inl (Reach.root hS)
    · exact Or.inr (D.roots_owned _ hctx)
  | @step a b _ he ih =>
    rcases he with hSe | hctxe
    · rcases ih with hra | howns
      · exact Or.inl (Reach.step hra hSe)
      · exact absurd howns (D.ownership_disjoint a b hSe)
    · rcases D.edges_closed a b hctxe with howns | hint
      · exact Or.inr howns
      · exact Or.inl (D.interface_live b hint)

/-- **The deployment theorem** (the Stay condition): an unreachable cell the
context does not own stays unreachable under every deployment composition.
Garbage cannot be resurrected by running more programs alongside — only by
forging a reference, which the closure forbids. -/
theorem deployment_cannot_resurrect (S : Store Cell) (D : Deployment S)
    (c : Cell) (hg : garbage S c) (hno : ¬ D.owns c) :
    garbage (compose S D) c := by
  intro hreach
  rcases compose_reach_cases S D c hreach with h | h
  · exact hg h
  · exact hno h

/-! ## The reification counterexample

Composing without the deployment closure — a context whose edge forges a
reference straight into an unreachable cell — resurrects garbage.  This is
the lookahead/reification breakage, witnessed concretely. -/

/-- A configuration on ℕ: root 1; cell 10 allocated but referenced by
nothing — the unforgeable `u`. -/
def sExample : Store ℕ where
  pointsTo _ _ := False
  root c := c = 1

/-- In the configuration alone, `u` is garbage. -/
theorem sExample_garbage_ten : garbage sExample 10 := by
  intro h
  cases h with
  | root hr =>
    have h10 : (10 : Nat) = 1 := hr
    omega
  | step _ he => exact he.elim

/-- Raw composition with a forging context: root 30 points straight at 10. -/
def kForged : Store ℕ where
  pointsTo c c' := (c = 30 ∧ c' = 10) ∨ sExample.pointsTo c c'
  root c := c = 30 ∨ sExample.root c

/-- **Reification breaks the garbage judgment**: under the forging context,
`u` is reachable. -/
theorem forging_resurrects : Reach kForged 10 :=
  Reach.step (Reach.root (Or.inl rfl)) (Or.inl ⟨rfl, rfl⟩)

/-! ## Complete roots are load-bearing -/

/-- A two-class store: root class A protects cell 1, class B protects
cell 4.  Scanning with class B omitted is modelled by dropping its root. -/
def sTwoClass : Store ℕ where
  pointsTo _ _ := False
  root c := c = 1 ∨ c = 4

def sOmitB : Store ℕ where
  pointsTo _ _ := False
  root c := c = 1

theorem twoClass_reaches_four : Reach sTwoClass 4 :=
  Reach.root (Or.inr rfl)

/-- Omitting a root class loses a live cell: the negative fixture behind
"no moving collection without complete roots." -/
theorem omitted_root_loses_cell : garbage sOmitB 4 := by
  intro h
  cases h with
  | root hr =>
    have h4 : (4 : Nat) = 1 := hr
    omega
  | step _ he => exact he.elim

/-! ## The behavioral bridge

Stay's garbage is temporal — no future interaction — while `garbage` above is
structural.  The bridge: for any mutator that never resurrects (a step never
makes an unreachable cell reachable) and any interaction judgment that only
touches reachable cells, structural garbage implies behavioral garbage.
Completeness fails by design: a reachable cell that never interacts is
behavioral garbage but not structural garbage — reachability is the sound
decidable over-approximation, and that is a theorem, not an apology. -/

/-- A mutator is non-resurrecting when no single step makes an unreachable
cell reachable.  This packages "reachable-only dereferencing + no forging". -/
def NonResurrecting (step : Store Cell → Store Cell → Prop) : Prop :=
  ∀ S S', step S S' → ∀ c, ¬ Reach S c → ¬ Reach S' c

/-- Unreachability is invariant along every non-resurrecting trace. -/
theorem unreach_along_trace {step : Store Cell → Store Cell → Prop}
    (h : NonResurrecting step) {S S' : Store Cell} {c : Cell}
    (hg : ¬ Reach S c) (hsteps : Relation.ReflTransGen step S S') :
    ¬ Reach S' c := by
  induction hsteps with
  | refl => exact hg
  | tail _ hlast ih => exact h _ _ hlast c ih

/-- An interaction judgment over a mutator: which cells a state touches.
The one structural obligation is that interaction requires reachability. -/
structure SyncModel (step : Store Cell → Store Cell → Prop) where
  Sync : Store Cell → Cell → Prop
  sync_reachable : ∀ S c, Sync S c → Reach S c

/-- Behavioral garbage: the cell never interacts in any future state. -/
def BehavioralGarbage {step : Store Cell → Store Cell → Prop}
    (M : SyncModel step) (S : Store Cell) (c : Cell) : Prop :=
  ∀ S', Relation.ReflTransGen step S S' → ¬ M.Sync S' c

/-- **The bridge (soundness)**: structural garbage is behavioral garbage for
every non-resurrecting mutator and reachability-respecting interaction. -/
theorem structural_implies_behavioral
    {step : Store Cell → Store Cell → Prop} (h : NonResurrecting step)
    (M : SyncModel step) (S : Store Cell) (c : Cell)
    (hg : garbage S c) : BehavioralGarbage M S c := by
  intro S' hsteps hsync
  exact unreach_along_trace h hg hsteps (M.sync_reachable S' c hsync)

/-- **Completeness fails, witnessed**: a reachable cell that never interacts
is behavioral garbage without being structural garbage. -/
theorem behavioral_not_structural :
    ∃ (step : Store ℕ → Store ℕ → Prop) (M : SyncModel step),
      BehavioralGarbage M sTwoClass 4 ∧ ¬ garbage sTwoClass 4 := by
  refine ⟨fun _ _ => False,
    ⟨fun _ _ => False, fun _ _ hf => hf.elim⟩, ?_, ?_⟩
  · intro S' _ hsync
    exact hsync
  · intro hg
    exact hg twoClass_reaches_four

/-! ## Moving collection: the evacuation certificate

The native collector MOVES live cells.  Its correctness statement is not
`reach_collect` but preservation up to renaming: an evacuation carries live
cells across an injective renaming, keeps exactly the live edges, and
preserves reachability as the image.  The from-space invariant — no edge of
the evacuated store touches an unmoved cell — holds by construction. -/

variable {Cell' : Type}

/-- Evacuation: move the reachable part of `S` across a renaming `φ`. -/
def evacuate (φ : Cell → Cell') (S : Store Cell) : Store Cell' where
  pointsTo d d' := ∃ c c', φ c = d ∧ φ c' = d' ∧ Reach S c ∧ S.pointsTo c c'
  root d := ∃ c, φ c = d ∧ S.root c

theorem evacuate_reach_of (φ : Cell → Cell') (S : Store Cell) {c : Cell}
    (h : Reach S c) : Reach (evacuate φ S) (φ c) := by
  induction h with
  | root hr => exact Reach.root ⟨_, rfl, hr⟩
  | @step a b ha he ih => exact Reach.step ih ⟨a, b, rfl, rfl, ha, he⟩

theorem evacuate_reach_inv (φ : Cell → Cell') (S : Store Cell) {d : Cell'}
    (h : Reach (evacuate φ S) d) : ∃ c, φ c = d ∧ Reach S c := by
  induction h with
  | root hr =>
    obtain ⟨c, rfl, hc⟩ := hr
    exact ⟨c, rfl, Reach.root hc⟩
  | step _ he ih =>
    obtain ⟨a, b, ha, hb, hra, hedge⟩ := he
    exact ⟨b, hb, Reach.step hra hedge⟩

/-- **The evacuation certificate**: under an injective renaming, the
evacuated store reaches exactly the image of what the source reaches. -/
theorem evacuate_reach_iff (φ : Cell → Cell') (hφ : Function.Injective φ)
    (S : Store Cell) (c : Cell) :
    Reach (evacuate φ S) (φ c) ↔ Reach S c := by
  constructor
  · intro h
    obtain ⟨c₀, heq, hr⟩ := evacuate_reach_inv φ S h
    rwa [hφ heq] at hr
  · exact evacuate_reach_of φ S

/-- **The from-space invariant, by construction**: every edge of the
evacuated store runs between images of LIVE source cells. -/
theorem evacuate_edges_live (φ : Cell → Cell') (S : Store Cell) {d d' : Cell'}
    (h : (evacuate φ S).pointsTo d d') :
    ∃ c c', φ c = d ∧ φ c' = d' ∧ Reach S c ∧ Reach S c' := by
  obtain ⟨c, c', hc, hc', hr, he⟩ := h
  exact ⟨c, c', hc, hc', hr, Reach.step hr he⟩

end Mettapedia.GSLT.Dynamics.StoreReachability
