import Mettapedia.GSLT.Dynamics.StoreCollectionAdvanced

set_option autoImplicit false

/-!
# CeTTa symbol-collection contract

This executable sweep model refines the pinned MeTTapedia reachability theory.
It specifies the obligations of a future C implementation; it does not certify
the current C symbol table, its roots, its threads, or its allocator.

Names and allocation identities differ. Reintroducing a spelling is allowed;
resolving an old, dead handle to a new allocation is not. Generations below are
unbounded model naturals: a bounded implementation must prevent wraparound.
-/

namespace Cetta.GC

open Mettapedia.GSLT.Dynamics.StoreReachability

structure SymbolHandle where
  slot : Nat
  generation : Nat
  deriving DecidableEq, Repr

/-- A model of symbol payload lookup, not the C hash-table representation. -/
abbrev SymbolPayloads := SymbolHandle → Option String

/-- An executable mask sweep. The intern index is weak and contributes no roots. -/
def sweepSymbols (payloads : SymbolPayloads) (marked : SymbolHandle → Bool) :
    SymbolPayloads :=
  fun h => if marked h then payloads h else none

/-- A marker must compute exactly the least strong/ephemeron fixed point.
    Constructing this certificate for the C scanner remains an implementation task. -/
structure MarkCertificate (W : WStore SymbolHandle) where
  marked : SymbolHandle → Bool
  exact : ∀ h, marked h = true ↔ ReachW W h

/-- Preservation needs an over-approximation, not exact marking. Extra marks
    can prevent reclamation; they cannot justify a space or progress bound. -/
structure SafeMarkCertificate (W : WStore SymbolHandle) where
  marked : SymbolHandle → Bool
  covers : ∀ h, ReachW W h → marked h = true

def MarkCertificate.safe {W : WStore SymbolHandle} (C : MarkCertificate W) :
    SafeMarkCertificate W := ⟨C.marked, fun h live => (C.exact h).mpr live⟩

def certifySafeMark (W : WStore SymbolHandle) (marked : SymbolHandle → Bool)
    (roots : ∀ h, W.strong.root h → marked h = true)
    (edges : ∀ a b, marked a = true → W.strong.pointsTo a b → marked b = true)
    (ephemerons : ∀ t k v, marked t = true → marked k = true →
      W.eph t k v → marked v = true) : SafeMarkCertificate W :=
  ⟨marked, reachW_least W (fun h => marked h = true) roots edges ephemerons⟩

theorem conservatively_marked_payload_preserved (W : WStore SymbolHandle)
    (C : SafeMarkCertificate W) (payloads : SymbolPayloads) (h : SymbolHandle)
    (live : ReachW W h) : sweepSymbols payloads C.marked h = payloads h := by
  simp [sweepSymbols, C.covers h live]

/-- The missing implementation bridge. `mayRead` must include every reference
    the concrete machine, owners, workers or foreign borrowers may use before
    the next protected boundary. References newly allocated after this boundary
    belong to a later allocation epoch. This structure does not establish that
    the C implementation satisfies its `readCoverage` premise. -/
structure RuntimeRootRefinement (State : Type) where
  model : State → WStore SymbolHandle
  mayRead : State → SymbolHandle → Prop
  readCoverage : ∀ state h, mayRead state h → ReachW (model state) h

theorem permitted_concrete_read_preserved {State : Type}
    (R : RuntimeRootRefinement State) (state : State)
    (C : SafeMarkCertificate (R.model state)) (payloads : SymbolPayloads)
    (h : SymbolHandle) (used : R.mayRead state h) :
    sweepSymbols payloads C.marked h = payloads h :=
  conservatively_marked_payload_preserved _ C payloads h
    (R.readCoverage state h used)

theorem more_marks_preserve_safety (W : WStore SymbolHandle)
    (C : SafeMarkCertificate W) (more : SymbolHandle → Bool)
    (superset : ∀ h, C.marked h = true → more h = true) :
    ∀ h, ReachW W h → more h = true :=
  fun h live => superset h (C.covers h live)

/-- Closure covers live objects; justification by reachability rules out extra
    marks. Both establish exact reclamation, while closure alone gives safety. -/
def certifyMark (W : WStore SymbolHandle) (marked : SymbolHandle → Bool)
    (roots : ∀ h, W.strong.root h → marked h = true)
    (edges : ∀ a b, marked a = true → W.strong.pointsTo a b → marked b = true)
    (ephemerons : ∀ t k v, marked t = true → marked k = true →
      W.eph t k v → marked v = true)
    (justified : ∀ h, marked h = true → ReachW W h) : MarkCertificate W where
  marked := marked
  exact h := ⟨justified h, reachW_least W (fun h => marked h = true)
    roots edges ephemerons h⟩

theorem live_payload_preserved (W : WStore SymbolHandle) (C : MarkCertificate W)
    (payloads : SymbolPayloads) (h : SymbolHandle) (live : ReachW W h) :
    sweepSymbols payloads C.marked h = payloads h := by
  simp [sweepSymbols, (C.exact h).mpr live]

theorem dead_payload_removed (W : WStore SymbolHandle) (C : MarkCertificate W)
    (payloads : SymbolPayloads) (h : SymbolHandle) (dead : ¬ ReachW W h) :
    sweepSymbols payloads C.marked h = none := by
  have unmarked : C.marked h ≠ true := fun hm => dead ((C.exact h).mp hm)
  simp [sweepSymbols, unmarked]

/-- Registered direct-holder roots include references outside ordinary Atoms.
    A declaration census alone does not cover every permitted concrete read;
    that separate obligation is `RuntimeRootRefinement.readCoverage`. -/
inductive RootKind
  | builtin
  | atomOrVariable
  | spaceOrState
  | compiledPlanOrIndex
  | continuationOrCursor
  | foreignLease
  | threadTemporary
  deriving DecidableEq, Repr

structure RootCensus (W : WStore SymbolHandle) where
  holds : RootKind → SymbolHandle → Prop
  /-- Completeness relative to the declared `holds` relation only. -/
  complete : ∀ k h, holds k h → W.strong.root h

theorem registered_holder_preserved (W : WStore SymbolHandle)
    (C : MarkCertificate W) (R : RootCensus W) (payloads : SymbolPayloads)
    (k : RootKind) (h : SymbolHandle) (held : R.holds k h) :
    sweepSymbols payloads C.marked h = payloads h :=
  live_payload_preserved W C payloads h (.root (R.complete k h held))

/-- Equality by spelling is preserved for every pair of live symbol handles. -/
theorem live_symbol_equality_preserved (W : WStore SymbolHandle)
    (C : MarkCertificate W) (payloads : SymbolPayloads) (a b : SymbolHandle)
    (ha : ReachW W a) (hb : ReachW W b) :
    (sweepSymbols payloads C.marked a = sweepSymbols payloads C.marked b) ↔
      payloads a = payloads b := by
  rw [live_payload_preserved W C payloads a ha,
    live_payload_preserved W C payloads b hb]

/-- Canonical live symbols do not change identity during this nonmoving sweep. -/
def Canonical (payloads : SymbolPayloads) : Prop :=
  ∀ a b name, payloads a = some name → payloads b = some name → a = b

theorem sweep_preserves_canonical (payloads : SymbolPayloads)
    (marked : SymbolHandle → Bool) (canonical : Canonical payloads) :
    Canonical (sweepSymbols payloads marked) := by
  intro a b name ha hb
  have original (h : SymbolHandle)
      (present : sweepSymbols payloads marked h = some name) :
      payloads h = some name := by
    by_cases hm : marked h = true
    · simpa [sweepSymbols, hm] using present
    · simp [sweepSymbols, hm] at present
  exact canonical a b name (original a ha) (original b hb)

/-- A list of answers keeps ordering and duplicate memberships, even when the
    intern table shares one allocation for equal symbols. -/
theorem live_answer_list_preserved (W : WStore SymbolHandle)
    (C : MarkCertificate W) (payloads : SymbolPayloads) (answers : List SymbolHandle)
    (live : ∀ h ∈ answers, ReachW W h) :
    answers.map (sweepSymbols payloads C.marked) = answers.map payloads := by
  exact List.map_congr_left (fun h hh => live_payload_preserved W C payloads h (live h hh))

/-- The upstream weak-table result supplies the negative liveness obligation. -/
theorem weak_only_symbol_reclaimed (W : WStore SymbolHandle)
    (C : MarkCertificate W) (payloads : SymbolPayloads) (h : SymbolHandle)
    (notRoot : ¬ W.strong.root h)
    (notEdge : ∀ a, ¬ W.strong.pointsTo a h)
    (notEphemeron : ∀ t k, ¬ W.eph t k h) :
    sweepSymbols payloads C.marked h = none :=
  dead_payload_removed W C payloads h
    (weak_only_not_retained W h notRoot notEdge notEphemeron)

def nextGeneration (h : SymbolHandle) : SymbolHandle :=
  { h with generation := h.generation + 1 }

theorem next_generation_fresh (h : SymbolHandle) : nextGeneration h ≠ h := by
  intro eq
  have bad := congrArg SymbolHandle.generation eq
  simp [nextGeneration] at bad

/-- Installation is a model allocation step, not a full interner implementation.
    The caller must also supply a free handle and canonical name lookup. -/
def installSymbol (payloads : SymbolPayloads) (fresh : SymbolHandle) (name : String) :
    SymbolPayloads := fun h => if h = fresh then some name else payloads h

/-- A dead spelling may be allocated again without making its old handle valid.
    Reusing the physical slot needs a new generation (or another no-ABA policy). -/
theorem same_name_can_return_without_resurrecting_handle
    (W : WStore SymbolHandle) (C : MarkCertificate W) (payloads : SymbolPayloads)
    (old : SymbolHandle) (name : String) (dead : ¬ ReachW W old) :
    let after := installSymbol (sweepSymbols payloads C.marked) (nextGeneration old) name
    after old = none ∧ after (nextGeneration old) = some name := by
  have different : old ≠ nextGeneration old := Ne.symm (next_generation_fresh old)
  simp [installSymbol, different, dead_payload_removed W C payloads old dead]

/-! Collection requests are latched until the host supplies a safe boundary.
The booleans below are obligations for host integration, not proof that the
current C implementation already has a complete census or a thread barrier. -/

structure CollectionBoundary where
  mutatorsQuiescent : Bool
  rootCensusComplete : Bool
  unprotectedBorrows : Nat
  deriving Repr

def readyToCollect (b : CollectionBoundary) : Bool :=
  b.mutatorsQuiescent && b.rootCensusComplete && decide (b.unprotectedBorrows = 0)

structure CollectionControl where
  pending : Bool
  completed : Nat
  deriving DecidableEq, Repr

def requestCollection (c : CollectionControl) : CollectionControl :=
  { c with pending := true }

def pollCollection (b : CollectionBoundary) (c : CollectionControl) : CollectionControl :=
  if c.pending && readyToCollect b then
    { pending := false, completed := c.completed + 1 }
  else c

theorem unsafe_boundary_keeps_request (b : CollectionBoundary) (c : CollectionControl)
    (notReady : readyToCollect b = false) :
    pollCollection b (requestCollection c) = requestCollection c := by
  simp [pollCollection, notReady]

theorem completed_collection_requires_safe_boundary
    (b : CollectionBoundary) (c : CollectionControl)
    (changed : (pollCollection b c).completed ≠ c.completed) :
    c.pending = true ∧ b.mutatorsQuiescent = true ∧ b.rootCensusComplete = true ∧
      b.unprotectedBorrows = 0 := by
  by_cases guard : (c.pending && readyToCollect b) = true
  · simpa [readyToCollect, Bool.and_eq_true, and_assoc] using guard
  · simp [pollCollection, guard] at changed

/-! ## Executable boundary witnesses

These include a dead unique prompt, a live spelling with two answer
memberships, stale-slot rejection, and a held foreign pointer. They do not
execute or qualify the C runtime.
-/

private def held : SymbolHandle := ⟨1, 0⟩
private def discarded : SymbolHandle := ⟨2, 0⟩

private def exampleStore : WStore SymbolHandle where
  strong := { root := fun h => h = held, pointsTo := fun _ _ => False }
  eph _ _ _ := False

private def exampleCertificate : MarkCertificate exampleStore where
  marked h := decide (h = held)
  exact h := by
    constructor
    · intro hm
      have same : h = held := by simpa using hm
      exact .root same
    · intro hr
      cases hr with
      | root hh => simpa [exampleStore] using hh
      | step _ impossible => exact impossible.elim
      | eph _ _ impossible => exact impossible.elim

private def examplePayloads : SymbolPayloads := fun h =>
  if h = held then some "kept-symbol"
  else if h = discarded then some "transient-prompt" else none

example : sweepSymbols examplePayloads exampleCertificate.marked held =
    some "kept-symbol" := by decide

example : sweepSymbols examplePayloads exampleCertificate.marked discarded = none := by decide

-- Conservative marking is safe but can keep every dead payload indefinitely.
private def allMarked : SafeMarkCertificate exampleStore :=
  { marked := fun _ => true, covers := fun _ _ => rfl }

example : sweepSymbols examplePayloads allMarked.marked held = some "kept-symbol" := by decide
example : sweepSymbols examplePayloads allMarked.marked discarded =
    some "transient-prompt" := by decide

example : [held, held].map (sweepSymbols examplePayloads exampleCertificate.marked) =
    [some "kept-symbol", some "kept-symbol"] := by decide

example : (installSymbol (sweepSymbols examplePayloads exampleCertificate.marked)
    (nextGeneration discarded) "transient-prompt") discarded = none := by decide

example : (installSymbol (sweepSymbols examplePayloads exampleCertificate.marked)
    (nextGeneration discarded) "transient-prompt") (nextGeneration discarded) =
      some "transient-prompt" := by decide

example : pollCollection ⟨true, true, 1⟩ ⟨true, 0⟩ = ⟨true, 0⟩ := by decide

example : pollCollection ⟨true, true, 0⟩ ⟨true, 0⟩ = ⟨false, 1⟩ := by decide

example : pollCollection ⟨true, false, 0⟩ ⟨true, 0⟩ = ⟨true, 0⟩ := by decide

-- Upstream also supplies a checked counterexample to omitting a root class:
#check omitted_root_loses_cell
#check local_garbage_exact
#check collapse_preserves_bag
#print axioms live_answer_list_preserved
#print axioms same_name_can_return_without_resurrecting_handle
#print axioms completed_collection_requires_safe_boundary
#print axioms conservatively_marked_payload_preserved
#print axioms permitted_concrete_read_preserved
#print axioms more_marks_preserve_safety

end Cetta.GC
