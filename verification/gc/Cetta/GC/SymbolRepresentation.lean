import Cetta.GC.SymbolCollection

set_option autoImplicit false

/-!
# Numbered and managed-spelling symbols

This interface model compares the current numbered representation with an
experimental hybrid representation. It does not certify
arena tracing, evacuation, foreign leases, or the lifetime of borrowed bytes.
Both representations remain symbols; neither is a grounded string.

Spellings are byte lists: embedded NUL and non-UTF-8 bytes are not discarded.
The ID equality optimization requires a canonical table and valid references.
Promotion is observationally invisible only when the installed bytes agree.
Every equality/hash plane must obey this interface, including hash-cons indexes.
-/

namespace Cetta.GC.Representation

abbrev Bytes := List UInt8
abbrev Table := Nat → Option Bytes

inductive SymbolRef
  | numbered (id : Nat)
  | spelled (bytes : Bytes)
  deriving DecidableEq, Repr

def observe (table : Table) : SymbolRef → Option Bytes
  | .numbered id => table id
  | .spelled bytes => some bytes

def Valid (table : Table) : SymbolRef → Prop
  | .numbered id => ∃ bytes, table id = some bytes
  | .spelled _ => True

def Canonical (table : Table) : Prop :=
  ∀ a b bytes, table a = some bytes → table b = some bytes → a = b

def symbolEqual (table : Table) : SymbolRef → SymbolRef → Bool
  | .numbered a, .numbered b => decide (a = b)
  | a, b => decide (observe table a = observe table b)

/-- Numeric equality is a valid fast path only under canonicality and validity.
    Invalid IDs must not acquire equality merely because both lookups fail. -/
theorem equal_correct (table : Table) (canonical : Canonical table)
    (a b : SymbolRef) (validA : Valid table a) (validB : Valid table b) :
    symbolEqual table a b = true ↔ observe table a = observe table b := by
  cases a with
  | numbered left =>
      cases b with
      | numbered right =>
          simp only [symbolEqual, observe, decide_eq_true_eq]
          constructor
          · intro same
            subst right
            rfl
          · intro same
            obtain ⟨bytes, present⟩ := validA
            exact canonical left right bytes present (same.symm.trans present)
      | spelled bytes => simp [symbolEqual]
  | spelled bytes => cases b <;> simp [symbolEqual]

def symbolHash (hash : Bytes → Nat) (table : Table) (s : SymbolRef) : Option Nat :=
  (observe table s).map hash

/-- Equal mixed representations must have the same key in every hash index.
    This constrains both structural hashing and internal canonicalization. -/
theorem equal_hash_coherent (hash : Bytes → Nat) (table : Table)
    (canonical : Canonical table) (a b : SymbolRef)
    (validA : Valid table a) (validB : Valid table b)
    (same : symbolEqual table a b = true) :
    symbolHash hash table a = symbolHash hash table b := by
  unfold symbolHash
  rw [(equal_correct table canonical a b validA validB).mp same]

structure Extension (before after : Table) : Prop where
  preserves : ∀ id bytes, before id = some bytes → after id = some bytes

theorem extension_preserves_observation (before after : Table)
    (extension : Extension before after) (s : SymbolRef) (valid : Valid before s) :
    observe after s = observe before s := by
  cases s with
  | numbered id =>
      obtain ⟨bytes, present⟩ := valid
      simp [observe, present, extension.preserves id bytes present]
  | spelled bytes => rfl

theorem extension_preserves_validity (before after : Table)
    (extension : Extension before after) (s : SymbolRef) (valid : Valid before s) :
    Valid after s := by
  cases s with
  | numbered id =>
      obtain ⟨bytes, present⟩ := valid
      exact ⟨bytes, extension.preserves id bytes present⟩
  | spelled bytes => trivial

def promote (id : Nat) : SymbolRef := .numbered id

theorem promotion_preserves_observation (table : Table) (bytes : Bytes) (id : Nat)
    (installed : table id = some bytes) :
    observe table (promote id) = observe table (.spelled bytes) := by
  simpa [observe, promote] using installed

theorem promotion_preserves_hash (hash : Bytes → Nat) (table : Table)
    (bytes : Bytes) (id : Nat) (installed : table id = some bytes) :
    symbolHash hash table (promote id) = symbolHash hash table (.spelled bytes) := by
  unfold symbolHash
  rw [promotion_preserves_observation table bytes id installed]

/-- Registration and lookup are separate operations. A previously unnumbered
    spelling must see later registry updates, without caching an eternal miss. -/
def lookup {α : Type} (table : Table) (registry : Bytes → Option α)
    (s : SymbolRef) : Option α :=
  (observe table s).bind registry

theorem spelled_lookup_needs_no_registration {α : Type} (table : Table)
    (registry : Bytes → Option α) (bytes : Bytes) :
    lookup table registry (.spelled bytes) = registry bytes := rfl

theorem promotion_preserves_lookup {α : Type} (table : Table)
    (registry : Bytes → Option α) (bytes : Bytes) (id : Nat)
    (installed : table id = some bytes) :
    lookup table registry (promote id) = lookup table registry (.spelled bytes) := by
  unfold lookup
  rw [promotion_preserves_observation table bytes id installed]

theorem old_spelling_sees_new_declaration {α : Type} (table : Table)
    (registry : Bytes → Option α) (bytes : Bytes) (value : α)
    (declared : registry bytes = some value) :
    lookup table registry (.spelled bytes) = some value := declared

/-- Collection may delete unreachable numbered entries, but every reference
    retained by the program must retain its spelling. This is the same live
    observation obligation for managed spellings and weak interned payloads. -/
theorem retained_answers_preserved (before after : Table) (answers : List SymbolRef)
    (retained : ∀ s ∈ answers, observe after s = observe before s) :
    answers.map (observe after) = answers.map (observe before) :=
  List.map_congr_left retained

inductive Value
  | symbol (reference : SymbolRef)
  | groundedString (bytes : Bytes)

inductive Observation
  | symbol (bytes : Option Bytes)
  | groundedString (bytes : Bytes)
  deriving DecidableEq, Repr

def observeValue (table : Table) : Value → Observation
  | .symbol reference => .symbol (observe table reference)
  | .groundedString bytes => .groundedString bytes

theorem symbol_is_not_grounded_string (table : Table) (s : SymbolRef) (bytes : Bytes) :
    observeValue table (.symbol s) ≠ observeValue table (.groundedString bytes) := by
  simp [observeValue]

/-- An index may return extra candidates for unnumbered symbols. Its ordered
    occurrence list must include every true match. Filtering by the real
    matcher then preserves the full answer list, including duplicates. This
    does not establish the C index's coverage or ordering premises. -/
theorem verified_overapproximation {α : Type} (entries : List α)
    (candidate accepts : α → Bool)
    (covers : ∀ x ∈ entries, accepts x = true → candidate x = true) :
    (entries.filter candidate).filter accepts = entries.filter accepts := by
  induction entries with
  | nil => rfl
  | cons first rest ih =>
      have tailCoverage : ∀ x ∈ rest, accepts x = true → candidate x = true := by
        intro x hx
        exact covers x (List.mem_cons_of_mem first hx)
      have tail := ih tailCoverage
      by_cases accepted : accepts first = true
      · have included := covers first (by simp) accepted
        simp [List.filter_cons, accepted, included, tail]
      · have rejected : accepts first = false := Bool.eq_false_iff.mpr accepted
        cases hc : candidate first <;> simp [List.filter_cons, rejected, hc, tail]

private def tinyTable : Table := fun id =>
  if id = 0 then some [97, 0, 255] else none

example : symbolEqual tinyTable (.numbered 0) (.spelled [97, 0, 255]) = true := by decide
example : symbolEqual tinyTable (.numbered 0) (.spelled [97]) = false := by decide
example : lookup tinyTable (fun bytes => if bytes = [120] then some 7 else none)
    (.spelled [120]) = some 7 := by decide
example : [SymbolRef.spelled [97], .numbered 0, .spelled [97]].map (observe tinyTable) =
    [some [97], some [97, 0, 255], some [97]] := by decide

/-- Counterexample to dropping the canonical-table premise: two different
    numeric IDs can have equal spellings in an invalid table. -/
example : let broken : Table := fun _ => some [97]
    symbolEqual broken (.numbered 0) (.numbered 1) = false ∧
      observe broken (.numbered 0) = observe broken (.numbered 1) := by decide

#print axioms verified_overapproximation
#print axioms equal_correct
#print axioms equal_hash_coherent
#print axioms extension_preserves_observation
#print axioms promotion_preserves_lookup
#print axioms retained_answers_preserved
#print axioms symbol_is_not_grounded_string

end Cetta.GC.Representation
