import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
import Mettapedia.GSLT.LanguageDef.ModuleSource.AlgebraInterpretation
import Mettapedia.Algebra.Collections.Free

/-!
# The collection theories of `Theories.module`

Tuples are a fixed-arity constructor; lists are the inductive presentation by
`LNil` and `LCons`, with no equations. Sequences, bags and sets are the
monoid presentation and its two extensions: `Bags` adds commutativity to
`Sequences`, and `Sets` adds idempotence to `Bags`. Each extension is the
theory morphism that forgets order, then multiplicity.

The equations read from the module hold in Lean's free structures: lists
under append, bags under sum, sets under union. Each extension is strict:
lists do not satisfy the bag equations, and bags do not satisfy the set
equations.
-/

open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
set_option maxRecDepth 4096
set_option maxHeartbeats 800000
#module_file "Theories.module"

namespace CeTTaCollectionTheories

open Mettapedia.OSLF.MeTTaIL.Syntax
open Mettapedia.GSLT.LanguageDef.ModuleSource
open AlgebraInterpretation

def theoryValue (path : List String) (args : List Expr) : Value :=
  ((applyTheory CollectionTheories.library CollectionTheories.module path args).toOption.map
    Prod.snd).getD { entries := [] }

def tuples : Value := theoryValue ["Tuples"] []
def lists : Value := theoryValue ["Lists"] []
def sequences : Value := theoryValue ["Sequences"] []
def bags : Value := theoryValue ["Bags"] [.apply ["Sequences"] []]
def sets : Value := theoryValue ["Sets"] [.apply ["Bags"] [.apply ["Sequences"] []]]

def v (label : String) : Pattern := .fvar label
def op (first second : Pattern) : Pattern := .apply "Append" [first, second]
def empty : Pattern := .apply "Empty" []

def monoidLaws : List (Pattern × Pattern) :=
  [(op (op (v "x") (v "y")) (v "z"), op (v "x") (op (v "y") (v "z"))),
   (op empty (v "x"), v "x"),
   (op (v "x") empty, v "x")]

def commutativity : Pattern × Pattern := (op (v "x") (v "y"), op (v "y") (v "x"))
def idempotence : Pattern × Pattern := (op (v "x") (v "x"), v "x")

def rows (value : Value) : List (List Premise × Pattern × Pattern) :=
  (flattenRaw "" value).equations.map fun row => (row.premises, row.left, row.right)

theorem tuples_terms : (flattenRaw "" tuples).terms.map (·.label) = ["MkPair"] ∧
    (flattenRaw "" tuples).equations = [] := by decide

theorem lists_terms : (flattenRaw "" lists).terms.map (·.label) = ["LNil", "LCons"] ∧
    (flattenRaw "" lists).equations = [] := by decide

theorem sequences_rows : rows sequences = monoidLaws.map fun law => ([], law.1, law.2) := by decide

theorem bags_rows :
    rows bags = (monoidLaws ++ [commutativity]).map fun law => ([], law.1, law.2) := by decide

theorem sets_rows :
    rows sets = (monoidLaws ++ [commutativity, idempotence]).map fun law => ([], law.1, law.2) := by
  decide

/-- Every equation row of a value, with its premises and sides. -/
theorem row_of_entry {value : Value} {item : Item} (member : item ∈ value.entries) {row : Equation}
    (body : item.body = Decl.equation row) : (row.premises, row.left, row.right) ∈ rows value := by
  apply List.mem_map_of_mem
  exact List.mem_filterMap.mpr ⟨item, member, by simp [body]⟩

/-- An equation row of the flattened value comes from an entry. -/
theorem entry_of_row {value : Value} {row : Equation} (member : row ∈ (flattenRaw "" value).equations) :
    ∃ item ∈ value.entries, item.body = Decl.equation row := by
  obtain ⟨item, inside, chosen⟩ := List.mem_filterMap.mp member
  refine ⟨item, inside, ?_⟩
  revert chosen
  cases item.body <;> simp

def listOps : String → List (List ℕ) → List ℕ
  | "Append", [first, second] => first ++ second
  | _, _ => []

def bagOps : String → List (Multiset ℕ) → Multiset ℕ
  | "Append", [first, second] => first + second
  | _, _ => 0

def setOps : String → List (Finset ℕ) → Finset ℕ
  | "Append", [first, second] => first ∪ second
  | _, _ => ∅

/-- Lists under append satisfy the sequence equations. -/
theorem lists_model : SourceEquationsHold listOps sequences := by
  intro item member row body _
  have pair := row_of_entry member body
  rw [sequences_rows] at pair
  simp only [monoidLaws, List.map_cons, List.map_nil, List.mem_cons, List.not_mem_nil, or_false,
    Prod.mk.injEq] at pair
  rcases pair with ⟨-, left, right⟩ | ⟨-, left, right⟩ | ⟨-, left, right⟩ <;>
    rw [left, right] <;> simp [EquationHolds, read?, listOps, op, empty, v]

/-- Bags under sum satisfy the bag equations. -/
theorem bags_model : SourceEquationsHold bagOps bags := by
  intro item member row body _
  have pair := row_of_entry member body
  rw [bags_rows] at pair
  simp only [monoidLaws, commutativity, List.cons_append, List.nil_append, List.map_cons,
    List.map_nil, List.mem_cons, List.not_mem_nil, or_false, Prod.mk.injEq] at pair
  rcases pair with ⟨-, left, right⟩ | ⟨-, left, right⟩ | ⟨-, left, right⟩ | ⟨-, left, right⟩ <;>
    rw [left, right] <;> simp [EquationHolds, read?, bagOps, op, empty, v]
  all_goals (intro assignment; ac_rfl)

/-- Sets under union satisfy the set equations. -/
theorem sets_model : SourceEquationsHold setOps sets := by
  intro item member row body _
  have pair := row_of_entry member body
  rw [sets_rows] at pair
  simp only [monoidLaws, commutativity, idempotence, List.cons_append, List.nil_append,
    List.map_cons, List.map_nil, List.mem_cons, List.not_mem_nil, or_false, Prod.mk.injEq] at pair
  rcases pair with ⟨-, left, right⟩ | ⟨-, left, right⟩ | ⟨-, left, right⟩ | ⟨-, left, right⟩ |
      ⟨-, left, right⟩ <;>
    rw [left, right] <;> simp [EquationHolds, read?, setOps, op, empty, v]
  all_goals (intro assignment; ac_rfl)

/-- The equation of a value with the given sides holds in a model. -/
theorem holds_of_row {A : Type} {operations : String → List A → A} {value : Value}
    (holds : SourceEquationsHold operations value) {left right : Pattern}
    (member : ([], left, right) ∈ rows value) : EquationHolds operations left right := by
  obtain ⟨row, inside, same⟩ := List.mem_map.mp member
  simp only [Prod.mk.injEq] at same
  obtain ⟨premises, rfl, rfl⟩ := same
  obtain ⟨item, entry, body⟩ := entry_of_row inside
  exact holds item entry row body premises

/-- `x` is `[1]`, every other variable `[2]`. -/
def apart (label : String) : List ℕ := if label = "x" then [1] else [2]

/-- **Lists do not satisfy the bag equations**: append does not commute. -/
theorem lists_not_bags : ¬ SourceEquationsHold listOps bags := by
  intro holds
  have comm := holds_of_row holds (left := commutativity.1) (right := commutativity.2)
    (by rw [bags_rows]; simp [monoidLaws, commutativity])
  have := comm.equal apart
  simp [read?, listOps, op, v, commutativity, apart] at this

/-- **Bags do not satisfy the set equations**: a sum is not idempotent. -/
theorem bags_not_sets : ¬ SourceEquationsHold bagOps sets := by
  intro holds
  have idem := holds_of_row holds (left := idempotence.1) (right := idempotence.2)
    (by rw [sets_rows]; simp [monoidLaws, commutativity, idempotence])
  have := idem.equal fun _ => {1}
  simp only [read?, op, v, idempotence, List.mapM_cons, List.mapM_nil] at this
  exact absurd this (by decide)

#print axioms sequences_rows
#print axioms bags_rows
#print axioms sets_rows
#print axioms lists_model
#print axioms bags_model
#print axioms sets_model
#print axioms lists_not_bags
#print axioms bags_not_sets

end CeTTaCollectionTheories
