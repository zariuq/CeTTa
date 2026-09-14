import Mettapedia.GSLT.Parsing.FiniteFactTable
import Mettapedia.GSLT.Parsing.CanonicalSourceOperationalGSLT
import Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

/-!
# All-domain translation validation of the native matching-policy result

The source policy, raw native answer stream, and framed result are read
independently from their actual files. Generated data is checked, not used as
the definition of its meaning. The finite domain includes all three outcomes for each
of the 64 inputs. Its exact occurrence table is compared with independently
computed source-rule instances by a kernel-checked equality decision.

This qualifies the result artifact. It is not a proof of the native chart,
of the GSLT compiler implementation, or of the emitted C runtime.
-/

set_option autoImplicit false

namespace Cetta.MatchDecision.PolicySourceQualificationV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT
open Mettapedia.GSLT.Parsing
open HornCertificate CanonicalSourceHornElaboration CanonicalSourceOperationalGSLT
open scoped Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

private def policySyntax : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/core/match_decision_policy_v1.metta"

private def resultSyntax : SExpr :=
  metta_sexpr_file% petta
    "../../../runtime/generated/match_decision_policy_v1.result.metta"

private def answerSyntax : List SExpr :=
  metta_sexpr_lines_file% petta
    "../../../runtime/generated/match_decision_policy_v1.answers"

@[simp] private theorem arityOne : "1".toNat? = some 1 := Nat.toNat?_repr 1
@[simp] private theorem arityTwo : "2".toNat? = some 2 := Nat.toNat?_repr 2
@[simp] private theorem arityThree : "3".toNat? = some 3 := Nat.toNat?_repr 3
@[simp] private theorem arityFive : "5".toNat? = some 5 := Nat.toNat?_repr 5
@[simp] private theorem aritySix : "6".toNat? = some 6 := Nat.toNat?_repr 6

private theorem decode_presentation_isSome (name : String)
    (operators equations rewrites : List SExpr) :
    (decode (.list [.atom "gslt-presentation-v1", .atom name,
      .list (.atom "signature" :: operators), .list (.atom "equations" :: equations),
      .list (.atom "rewrites" :: rewrites)])).isSome =
      ((decodeList decodeOperator operators).isSome &&
        (decodeList decodeRewrite rewrites).isSome) := by
  cases op : decodeList decodeOperator operators <;>
    cases rw : decodeList decodeRewrite rewrites <;>
    simp [decode, atomToken?, op, rw]

def policySource : Source := (decode policySyntax).get (by
  simp [policySyntax, decode, decodeList, decodeOperator, decodeRewrite, atomToken?])
def resultSource : Source := (decode resultSyntax).get (by
  rw [resultSyntax, decode_presentation_isSome]
  rw [Bool.and_eq_true]
  constructor
  · simp [decodeList, decodeOperator, atomToken?]
  · decide +kernel)

theorem actual_policy_decodes : decode policySyntax = some policySource :=
  (Option.some_get _).symm

theorem actual_result_decodes : decode resultSyntax = some resultSource :=
  (Option.some_get _).symm

/-- A structural projection for computation. Full source admission is proved
separately below; this projection is never used as an admission claim. -/
private def rawRewrites? : SExpr → Option (List Rewrite)
  | .list [.atom "gslt-presentation-v1", _, .list (.atom "signature" :: _),
      .list (.atom "equations" :: _), .list (.atom "rewrites" :: rows)] =>
      decodeList decodeRewrite rows
  | _ => none

private theorem rawRewrites?_of_decode {raw : SExpr} {source : Source}
    (accepted : decode raw = some source) : rawRewrites? raw = some source.rewrites := by
  unfold decode at accepted
  split at accepted
  next nameSyntax operatorSyntax equations rewriteSyntax =>
    cases name : atomToken? nameSyntax with
    | none => simp [name] at accepted
    | some nameValue =>
      cases operators : decodeList decodeOperator operatorSyntax with
      | none => simp [name, operators] at accepted
      | some operatorValues =>
        cases rewrites : decodeList decodeRewrite rewriteSyntax with
        | none => simp [name, operators, rewrites] at accepted
        | some rewriteValues =>
          have same : Source.mk nameValue operatorValues equations rewriteValues = source := by
            simpa [name, operators, rewrites] using accepted
          subst source
          simpa only [rawRewrites?] using rewrites
  next => simp at accepted

private def policyRewrites : List Rewrite := (rawRewrites? policySyntax).get (by decide +kernel)
private def resultRewrites : List Rewrite := (rawRewrites? resultSyntax).get (by decide +kernel)

theorem actual_result_rows : resultSource.rewrites = resultRewrites := by
  have projected := rawRewrites?_of_decode actual_result_decodes
  have collected : rawRewrites? resultSyntax = some resultRewrites :=
    (Option.some_get _).symm
  exact Option.some.inj (projected.symm.trans collected)

def policyProgram : Program :=
  programOf ((elaborateRewrites? policyRewrites).get (by decide +kernel))

theorem actual_policy_elaborates :
    elaborateProgram? [policySource] = some policyProgram := by
  have rows : policySource.rewrites = policyRewrites := by
    simp [policySource, policyRewrites, policySyntax, rawRewrites?,
      decode, decodeList, decodeOperator, decodeRewrite, atomToken?]
  have valid : compositionValid [policySource] = true := by
    simp [policySource, policySyntax, decode, decodeList, decodeOperator,
      decodeRewrite, atomToken?, compositionValid, Source.hasValidSchema,
      Source.termsValidIn, Rewrite.hasValidShape, termSupported, termsSupported,
      hasOperator, compositionRewriteNames, rewriteName, compositionOperators]
  have empty : policySource.equations = [] := by
    simp [policySource, policySyntax, decode, decodeList, decodeOperator,
      decodeRewrite, atomToken?]
  simp [elaborateProgram?, elaborateComposition?, valid, empty,
    compositionRewrites, rows, policyProgram]
  have accepted : (elaborateRewrites? policyRewrites).isSome = true := by decide +kernel
  exact ⟨_, (Option.some_get accepted).symm, rfl⟩

theorem actual_policy_theory :
    elaborateTheory? [policySource] = some (HornCertificateGSLT.theory policyProgram) :=
  elaborateTheory?_of_program actual_policy_elaborates

private def application (head : String) (arguments : List SExpr) : SExpr :=
  .list (.atom head :: arguments)

private def quotedIndex : Nat → SExpr
  | 0 => .atom "q-zero"
  | n + 1 => application "q-succ" [quotedIndex n]

private def quotedList : List SExpr → SExpr
  | [] => .atom "q-nil"
  | x :: xs => application "q-cons" [x, quotedList xs]

mutual
  private def quotedTerm : Term → SExpr
    | .var i => application "q-var" [quotedIndex i]
    | .atom name => application "q-sym" [.atom name]
    | .integer n => application "q-int" [.atom (toString n)]
    | .app name args => application "q-app"
        [application "q-sym" [.atom name], quotedTerms args]

  private def quotedTerms : Terms → SExpr
    | .nil => .atom "q-nil"
    | .cons x xs => application "q-cons" [quotedTerm x, quotedTerms xs]
end

private def quotedAtom (atom : Atom) : SExpr :=
  application "q-app" [application "q-sym" [.atom atom.relation], quotedTerms atom.arguments]

private def quotedRule (rule : Rule) : SExpr :=
  application "q-rule" [application "q-sym" [.atom rule.name],
    quotedAtom rule.head, quotedList (rule.body.map quotedAtom)]

def policyGoal (observation key arity identity outcome : String) : GroundAtom :=
  ⟨"candidate-policy", GroundTerms.ofList
    [.atom observation, .atom key, .atom arity, .atom identity, .atom outcome]⟩

/-- Decode the already structured result and check its full source quotation.
Metadata rows are checked for source identity but are not semantic evidence. -/
private def decodeRecord? (program : Program) : SExpr → Option (List FiniteFactTable.Cell)
  | .list [.atom "md-compile", .list [.atom "md-cell", origin,
      .atom observation, .atom key, .atom arity, .atom identity, .atom outcome]] => do
      let occurrence ← program.findIdx? (fun rule => quotedRule rule == origin)
      some [(occurrence, policyGoal observation key arity identity outcome)]
  | .list [.atom "md-compile", .list [.atom tag, origin]] => do
      if tag != "md-source" && tag != "md-admitted" then none else do
        let _ ← program.findIdx? (fun rule => quotedRule rule == origin)
        some []
  | _ => none

def decodeResult? (program : Program) (rewrites : List Rewrite) : Option (List FiniteFactTable.Cell) := do
  let rows ← rewrites.mapM fun row =>
    if row.body.isEmpty then decodeRecord? program row.head else none
  some rows.flatten

def nativeCells : List FiniteFactTable.Cell :=
  (decodeResult? policyProgram resultRewrites).get (by decide +kernel)

def decodeAnswers? (program : Program) (rows : List SExpr) :
    Option (List FiniteFactTable.Cell) := do
  let decoded ← rows.mapM (decodeRecord? program)
  some decoded.flatten

/-- These cells come directly from the answer stream consumed by the C table
emitter, without passing through the native answer-facts framer. -/
def answerCells : List FiniteFactTable.Cell :=
  (decodeAnswers? policyProgram answerSyntax).get (by decide +kernel)

def domain : List GroundAtom := do
  let observation ← ["unknown", "absent", "literal", "expression"]
  let key ← ["wildcard", "literal", "expression-arity", "expression-head"]
  let arity ← ["different", "equal"]
  let identity ← ["different", "equal"]
  let outcome ← ["fallback", "keep", "refute"]
  pure (policyGoal observation key arity identity outcome)

/-- The enumerated domain contains exactly every combination of the declared
observation, key, comparison, and outcome alternatives. -/
theorem domain_exact (goal : GroundAtom) :
    goal ∈ domain ↔
      ∃ observation ∈ ["unknown", "absent", "literal", "expression"],
      ∃ key ∈ ["wildcard", "literal", "expression-arity", "expression-head"],
      ∃ arity ∈ ["different", "equal"],
      ∃ identity ∈ ["different", "equal"],
      ∃ outcome ∈ ["fallback", "keep", "refute"],
        policyGoal observation key arity identity outcome = goal := by
  simp only [domain, List.bind_eq_flatMap, List.mem_flatMap]
  have pure_mem (value : GroundAtom) : goal ∈ (pure value : List GroundAtom) ↔ value = goal := by
    change goal ∈ [value] ↔ value = goal
    simp only [List.mem_singleton, eq_comm]
  simp only [pure_mem]

theorem domain_has_no_repeated_atoms : domain.Nodup := by decide +kernel

/-- The qualified source has no premise-bearing rules hidden outside the
finite fact-table check. -/
theorem all_policy_rules_are_facts : ∀ rule ∈ policyProgram, rule.body = [] := by
  decide +kernel

theorem actual_source_and_result_sizes :
    policyProgram.length = 17 ∧ resultRewrites.length = 98 ∧
      nativeCells.length = 64 ∧ domain.length = 192 := by decide +kernel

theorem source_occurrence_names_are_unique :
    (policyProgram.map Rule.name).Nodup := by decide +kernel

#eval do IO.println s!"(MatchDecisionPolicySemanticCheckV1 {FiniteFactTable.check policyProgram domain nativeCells})"

#eval do IO.println s!"(MatchDecisionPolicyFramingCheckV1 {decide (answerCells.Perm nativeCells)})"

/-- The native framer may reorder records, but cannot remove or invent
semantic cells or their multiplicities relative to the actual answer stream. -/
theorem raw_answers_match_framed_result : answerCells.Perm nativeCells := by decide +kernel

theorem native_result_checks :
    FiniteFactTable.check policyProgram domain nativeCells = true := by decide +kernel

/-- Every supplied cell is a source instance, and every source instance in
the complete finite domain occurs in the supplied native result. -/
theorem native_result_exact (cell : FiniteFactTable.Cell) :
    cell ∈ nativeCells ↔ cell.2 ∈ domain ∧
      FiniteFactTable.SourceInstance policyProgram cell.1 cell.2 :=
  FiniteFactTable.checked_iff_instances native_result_checks cell

theorem native_result_preserves_occurrence_counts (cell : FiniteFactTable.Cell) :
    nativeCells.count cell = (FiniteFactTable.expected policyProgram domain).count cell :=
  FiniteFactTable.checked_counts native_result_checks cell

theorem native_cell_has_source_GSLT_path {cell : FiniteFactTable.Cell}
    (member : cell ∈ nativeCells) :
    (HornCertificateGSLT.theory policyProgram).MultiStep [(1, cell.2)] [] :=
  FiniteFactTable.checked_cell_terminal_path native_result_checks member

theorem raw_answers_exact (cell : FiniteFactTable.Cell) :
    cell ∈ answerCells ↔ cell.2 ∈ domain ∧
      FiniteFactTable.SourceInstance policyProgram cell.1 cell.2 :=
  raw_answers_match_framed_result.mem_iff.trans (native_result_exact cell)

theorem raw_answers_preserve_occurrence_counts (cell : FiniteFactTable.Cell) :
    answerCells.count cell = (FiniteFactTable.expected policyProgram domain).count cell :=
  (raw_answers_match_framed_result.count_eq cell).trans
    (native_result_preserves_occurrence_counts cell)

theorem raw_answer_has_source_GSLT_path {cell : FiniteFactTable.Cell}
    (member : cell ∈ answerCells) :
    (HornCertificateGSLT.theory policyProgram).MultiStep [(1, cell.2)] [] :=
  native_cell_has_source_GSLT_path (raw_answers_match_framed_result.mem_iff.mp member)

#print axioms native_result_exact
#print axioms actual_policy_elaborates
#print axioms actual_result_decodes
#print axioms actual_result_rows
#print axioms actual_policy_theory
#print axioms domain_exact
#print axioms all_policy_rules_are_facts
#print axioms native_result_preserves_occurrence_counts
#print axioms native_cell_has_source_GSLT_path
#print axioms raw_answers_match_framed_result
#print axioms raw_answers_exact
#print axioms raw_answers_preserve_occurrence_counts
#print axioms raw_answer_has_source_GSLT_path

end Cetta.MatchDecision.PolicySourceQualificationV1
