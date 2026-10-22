import Mettapedia.Algebra.SupportTensor
import Mettapedia.Algebra.SupportTensorRounding
import Mettapedia.Algorithms.Retrieval.PrefixSearch
import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
import Mettapedia.OSLF.MeTTaIL.NonrecursiveReduction

/-!
The authored module supplies the operational rules. The client proves exact
root observations, including no additional result, against Option presence
and filling. This uses the existing Lean module reader; it does not claim
that reader or a native tensor implementation is a verified parser/compiler.
-/

set_option autoImplicit false
open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

#module_file "../../../langdef/tensor/Support.module"
#module_roundtrip TensorSupport

namespace TensorSupportClient

open Mettapedia.OSLF.MeTTaIL.Syntax
open Mettapedia.OSLF.MeTTaIL.Match
open Mettapedia.OSLF.MeTTaIL.NonrecursiveReduction
open Mettapedia.OSLF.MeTTaIL.ContextualStep

def language : LanguageDef :=
  TensorSupport.language.toOption.getD (LanguageDef.empty "invalid")

theorem source_loaded : TensorSupport.language = .ok language := rfl

private theorem rewrite_rows : language.rewrites = [
    { «name» := "MissingPresence", typeContext := [], premises := [],
      left := .apply "PresenceOf" [.apply "Absent" []], right := .apply "Missing" [] },
    { «name» := "StoredPresence", typeContext := [], premises := [],
      left := .apply "PresenceOf" [.apply "Present" [.fvar "value"]],
      right := .apply "Stored" [] },
    { «name» := "FillAbsent", typeContext := [], premises := [],
      left := .apply "Fill" [.apply "Absent" [], .fvar "default"], right := .fvar "default" },
    { «name» := "FillPresent", typeContext := [], premises := [],
      left := .apply "Fill" [.apply "Present" [.fvar "value"], .fvar "default"],
      right := .fvar "value" }] := rfl

def cell : Option Pattern → Pattern
  | none => .apply "Absent" []
  | some value => .apply "Present" [value]

def presence (value : Option Pattern) : Pattern :=
  .apply (if value.isSome then "Stored" else "Missing") []

theorem unconditional : isUnconditionalAligned language = true := by decide +kernel

theorem fill_results (value : Option Pattern) (default : Pattern) :
    rootReducts language (.apply "Fill" [cell value, default]) =
      [value.getD default] := by
  cases value <;>
    simp [rootReducts, rewrite_rows, cell, matchPattern, matchArgs,
      mergeBindings, applyBindings]

theorem presence_results (value : Option Pattern) :
    rootReducts language (.apply "PresenceOf" [cell value]) = [presence value] := by
  cases value <;>
    simp [rootReducts, rewrite_rows, cell, presence, matchPattern, matchArgs,
      mergeBindings, applyBindings]

/-- Preservation and reflection: filling cannot award itself another result. -/
theorem fill_step_iff (base : BasePremiseEvaluator) (value : Option Pattern)
    (default result : Pattern) :
    Step base language (.apply "Fill" [cell value, default]) result ↔
      result = value.getD default := by
  rw [step_iff_mem_rootReducts base language unconditional, fill_results]
  exact List.mem_singleton

theorem presence_step_iff (base : BasePremiseEvaluator) (value : Option Pattern)
    (result : Pattern) :
    Step base language (.apply "PresenceOf" [cell value]) result ↔
      result = presence value := by
  rw [step_iff_mem_rootReducts base language unconditional, presence_results]
  exact List.mem_singleton

/-- Data constructors have no operational rule of their own. -/
theorem cell_is_inert (value : Option Pattern) : rootReducts language (cell value) = [] := by
  cases value <;> simp [rootReducts, rewrite_rows, cell, matchPattern]

theorem filled_tensor_observation {I : Type} (tensor : I → Option Pattern)
    (default : Pattern) (i : I) :
    rootReducts language (.apply "Fill" [cell (tensor i), default]) =
      [Mettapedia.Algebra.SupportTensor.fill default tensor i] :=
  fill_results (tensor i) default

theorem absent_and_stored_have_different_observations (value : Pattern) :
    rootReducts language (.apply "PresenceOf" [cell none]) ≠
      rootReducts language (.apply "PresenceOf" [cell (some value)]) := by
  rw [presence_results, presence_results]
  simp [presence]

theorem no_invented_fill_result (base : BasePremiseEvaluator) :
    ¬ Step base language
      (.apply "Fill" [cell none, .apply "Left" []]) (.apply "Right" []) := by
  rw [fill_step_iff]
  simp

#print axioms fill_step_iff
#print axioms presence_step_iff
#print axioms filled_tensor_observation
#print axioms Mettapedia.Algebra.SupportTensor.matvec_eq_mulVec
#print axioms Mettapedia.Algebra.SupportTensor.eq_iff_presence_and_fill
#print axioms Mettapedia.Algebra.SupportTensor.sparseRows_toMatrix
#print axioms Mettapedia.Algorithms.Retrieval.PrefixSearch.lowerBound_eq_countBefore
#print axioms Mettapedia.Algebra.SupportTensor.finite_entries_roundtrip
#print axioms Mettapedia.Algebra.SupportTensorRounding.roundedMatvec_l1_error

end TensorSupportClient
