import Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation
import Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT
import Mettapedia.GSLT.Parsing.PlainBnfSelectedNativeTypeRefinement
import Mathlib

/-!
# Actual generated integer-provider equation inventory

Read the complete generated PeTTa artifact before Pattern lowering and compare
its provider equations with a structural projection of the actual authored
provider source. This checks this artifact's syntax and clause multiplicity;
it does not prove the C printer correct for arbitrary sources or establish
PeTTa operational correspondence. Source parsing is an executable boundary.

The fresh quotations also authenticate the cached source/target snapshots
used by the selected NativeType refinement. These equalities are freshness
checks, not new compiler-correctness theorems.
-/

set_option autoImplicit false

namespace Cetta.PlainBnf.ProviderTargetQualificationV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT
open scoped Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation

@[simp] private theorem natTokenOne : "1".toNat? = some 1 := by
  change (Nat.repr 1).toNat? = some 1
  exact Nat.toNat?_repr 1

@[simp] private theorem natTokenTwo : "2".toNat? = some 2 := by
  change (Nat.repr 2).toNat? = some 2
  exact Nat.toNat?_repr 2

@[simp] private theorem natTokenThree : "3".toNat? = some 3 := by
  change (Nat.repr 3).toNat? = some 3
  exact Nat.toNat?_repr 3

def generatedCommandChunks : List (List (Nat × Bool × SExpr)) :=
  metta_sexpr_command_chunks_file% petta
    "../../../langdef/petta/generated/plain_bnf_semantic_admission_v1.metta"

def generatedDefinitionChunks : List (List (Nat × SExpr)) :=
  generatedCommandChunks.map fun chunk =>
    chunk.filterMap fun (line, query, term) => if query then none else some (line, term)

def generatedRows : List (Nat × SExpr) := generatedDefinitionChunks.flatten

/-- Initialization is retained as a command, never silently treated as an
equation or discarded from the source inventory. -/
theorem initialization_command_exact :
    (generatedCommandChunks.flatten.filter (fun row => row.2.1)).map (fun row => row.2.2) =
    [.list [.atom "let", .atom "$_", .list [.atom "import!", .atom "&self", .atom "langdef"],
      .list [.atom "empty"]]] := by
  rw [List.filter_flatten, List.map_flatten]
  rfl

def authoredProvider : SExpr :=
  metta_sexpr_file% petta
    "../../../experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta"

def authoredAdmission : SExpr :=
  metta_sexpr_file% petta "../../../langdef/bnf/plain_bnf_semantic_admission_v1.metta"

theorem fresh_provider_snapshot :
    authoredProvider = Mettapedia.GSLT.Parsing.GeneratedPeTTaIntegerProviderBridge.sourceSyntax := rfl

theorem fresh_scalar_provider_snapshot :
    authoredProvider = Mettapedia.GSLT.Parsing.PlainBnfScalarOrderSource.providerSyntax := rfl

theorem fresh_admission_snapshot :
    authoredAdmission = Mettapedia.GSLT.Parsing.PlainBnfScalarOrderSource.sourceSyntax := rfl

private def equation? : SExpr → Option (SExpr × SExpr)
  | .list [.atom "=", left, right] => some (left, right)
  | _ => none

def generatedEquations : List (SExpr × SExpr) :=
  generatedDefinitionChunks.flatMap fun chunk => chunk.filterMap fun row => equation? row.2

private def providerHead (token : String) : Bool :=
  token.startsWith "gslt:ground-integer-" ||
    token == "gslt:ground-unicode-scalar" ||
    token == "gslt:ground-non-unicode-scalar"

def generatedProviders : List (SExpr × SExpr) :=
  generatedEquations.filter fun equation =>
    match equation.1 with
    | .list (.atom name :: _) => providerHead name
    | _ => false

/-- The older proof specimen and current installed artifact are different
programs. Compare every matching clause in the selected family, retaining
order and multiplicity. Physical line numbers are not equation semantics. -/
def selectedInstalled (name : String) : List SExpr :=
  generatedDefinitionChunks.flatMap fun chunk =>
    (Mettapedia.GSLT.Parsing.GeneratedPeTTaEquationDispatch.equationsFor name chunk).map Prod.snd

theorem installed_scalar_family_exact (typed : Bool) :
    selectedInstalled (Mettapedia.GSLT.Parsing.PlainBnfGeneratedScalarOrderSyntax.symbol typed) =
    (Mettapedia.GSLT.Parsing.PlainBnfGeneratedScalarOrderSyntax.rows typed).map Prod.snd := by
  cases typed <;> rfl

theorem installed_scalar_caller_exact (typed : Bool) :
    selectedInstalled (if typed then "admitted:gslt:mode:BNFValidateScalarListV1:110"
      else "gslt:mode:BNFValidateScalarListV1:110") =
    (Mettapedia.GSLT.Parsing.PlainBnfGeneratedScalarOrderSyntax.callerRows typed).map Prod.snd := by
  cases typed <;> decide +kernel

theorem installed_integer_provider_family_exact :
    (generatedDefinitionChunks.flatMap fun chunk =>
      (chunk.filter fun row =>
        Mettapedia.GSLT.Parsing.GeneratedPeTTaIntegerProviderBridge.isIntegerProvider row.2).map Prod.snd) =
    Mettapedia.GSLT.Parsing.GeneratedPeTTaIntegerProviderBridge.generatedProviders := by
  decide +kernel

private def validTopLevel : SExpr → Bool
  | .list [.atom "=", _, _] => true
  | .list [.atom ":", _, _] => true
  | _ => false

private def namedHead : SExpr → Bool
  | .list (.atom name :: _) =>
      !name.startsWith "$" && !name.startsWith "?"
  | _ => false

mutual
  /-- Structural notation change for the selected authored provider language:
  source metavariable spelling and explicit nullary applications. -/
  private def targetTerm : SExpr → SExpr
    | .atom token =>
      if token.startsWith "?" then .atom (String.ofList ('$' :: token.toList.tail))
      else .atom token
    | .list [.atom "metta-nullary", .atom name] => .list [.atom name]
    | .list elements => .list (targetTerms elements)
  termination_by term => sizeOf term

  private def targetTerms : List SExpr → List SExpr
    | [] => []
    | first :: rest => targetTerm first :: targetTerms rest
  termination_by terms => sizeOf terms
end

private def providerEquation? (rewrite : Rewrite) : Option (SExpr × SExpr) :=
  match rewrite.head, rewrite.body with
  | .list [.atom "metta-equation", left, right], [] =>
      some (targetTerm left, targetTerm right)
  | _, _ => none

def authoredTargets? : Option (List (SExpr × SExpr)) := do
  let source ← decode authoredProvider
  source.rewrites.mapM providerEquation?

theorem all_generated_forms_classified :
    generatedRows.all (fun row => validTopLevel row.2) = true := by
  rw [generatedRows, List.all_flatten]
  rfl

/-- No wildcard/function-valued equation head is being silently skipped by
the selected provider-head inventory. -/
theorem all_generated_equation_heads_named :
    generatedEquations.all (fun equation => namedHead equation.1) = true := by
  decide +kernel

theorem generated_provider_count : generatedProviders.length = 6 := by decide +kernel

/-- Independent source and target files meet by structural computation, not by
defining either side to be the other. Order and duplicate clauses are retained. -/
theorem generated_providers_equal_authored_projection :
    authoredTargets? = some generatedProviders := by
  simp [authoredTargets?, authoredProvider, decode, atomToken?, decodeList,
    decodeOperator, decodeRewrite, providerEquation?, targetTerm, targetTerms]
  decide +kernel

theorem duplicated_provider_changes_inventory :
    (generatedProviders ++ generatedProviders).length = 12 := by
  simp [generated_provider_count]

theorem duplicate_provider_inventory_not_qualified :
    authoredTargets? ≠ some (generatedProviders ++ generatedProviders) := by
  rw [generated_providers_equal_authored_projection]
  intro equal
  have lengths := congrArg List.length (Option.some.inj equal)
  simp [generated_provider_count] at lengths

#print axioms generated_providers_equal_authored_projection
#print axioms duplicate_provider_inventory_not_qualified
#print axioms initialization_command_exact
#print axioms installed_scalar_family_exact
#print axioms installed_scalar_caller_exact
#print axioms installed_integer_provider_family_exact
#print axioms fresh_provider_snapshot
#print axioms fresh_scalar_provider_snapshot
#print axioms fresh_admission_snapshot
#print axioms Mettapedia.GSLT.Parsing.PlainBnfSelectedNativeTypeRefinement.actual_packet_caller_let
#print axioms Mettapedia.GSLT.Parsing.PlainBnfSelectedNativeTypeRefinement.authenticated_scalar_list_caller_let

end Cetta.PlainBnf.ProviderTargetQualificationV1
