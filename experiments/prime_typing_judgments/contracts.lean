import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.ProgrammableJudgmentSemantics
import Mettapedia.Languages.MeTTa.HE.TypeRuleCompilation
import Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaNaturalPackage
import Mettapedia.Languages.HOLLight.MeTTa.RulePrograms
import Mettapedia.Languages.HOLLight.ExportReplayComposition
import Mettapedia.GSLT.LanguageDef.IndexedInductiveAppendCompletion
import Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased.ContextualJudgmentReadback
import Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaPackageControls
import Mettapedia.Languages.MeTTa.ProgrammableJudgmentServices
import Mettapedia.GSLT.LanguageDef.CheckedSourceOpen
import Mettapedia.GSLT.LanguageDef.CertificateGSLTOpenApplicationOperational
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.DeclarationCompletion
import Mettapedia.GSLT.LanguageDef.CertificateGSLTJudgmentValues
import Mettapedia.GSLT.LanguageDef.CertificateGSLTOpenSupport
import Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaDataControls
import Mettapedia.Languages.MeTTa.HE.TypeRuleData
import Mettapedia.Languages.HOLLight.MeTTa.RuleData
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.StructuralConversionPrograms
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Examples.IdentityConversionBoundary
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.FormationCheckedTelescopePrograms
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.FormationSensitiveRawBoundary
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Instances.CumulativeRegularity
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Instances.CumulativeJoins

/-!
# The existing dependent binder contract used by the notation experiment

This file exercises the actual scoped calculus and its existing telescope
programs. It does not identify the named MeTTa reader or C implementation
with that calculus. The source context is `A : U1, x : A`, and the supplied
arguments are `U0` and the independently typed ground head.
-/

set_option autoImplicit false

namespace PrimeTypingJudgmentStudy

open Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation
open FormationSensitive
open Mettapedia.TypeTheory.UniverseLevel

abbrev zero : LevelExpr Nat := .const 0
abbrev one : LevelExpr Nat := .succ zero

def sourceContext : Tower.Ctx 2 :=
  .snoc (.snoc .nil (.head (.sort one))) (.var 0)

theorem context_formed : ContextFormation Tower.rules sourceContext := by
  apply ContextFormation.snoc
  · exact .snoc .nil (.headType (.sort one)) (.sort _)
  · exact .var 0
  · exact .sort one

def sourceBody : Tower.Tm 2 := .refl (.var 0)
def sourceType : Tower.Tm 2 := .id (.var 1) (.var 0) (.var 0)

theorem source_judgment :
    Judgment Tower.rules sourceContext sourceBody sourceType :=
  ⟨context_formed, .reflIntro (.var 0)⟩

def arguments : Sub Tower.Head 2 0 :=
  Fin.cases (.head .legacyGround) (fun _ => .head (.sort zero))

theorem arguments_typed : FormationSensitive.CtxMor Tower.rules sourceContext .nil arguments := by
  intro index
  refine Fin.cases ?_ (fun prior => Fin.cases ?_ (fun impossible => Fin.elim0 impossible) prior) index
  · exact .headType .legacyGround
  · exact .headType (.sort zero)

def applied : Tower.Tm 0 :=
  TelescopeAbstraction.applyClosed sourceContext arguments
    (liftClosed (TelescopeAbstraction.closeTerm sourceContext sourceBody))

def result : Tower.Tm 0 := .refl (.head .legacyGround)

theorem applied_judgment :
    Judgment Tower.rules .nil applied (subst arguments sourceType) := by
  refine ⟨.nil, ?_⟩
  exact FormationCheckedTelescopePrograms.apply_typed arguments_typed
    (FormationCheckedTelescopePrograms.close_into_target
      towerUniverseRegularity FormationCheckedTelescopePrograms.tower_joins source_judgment)

theorem result_judgment :
    Judgment Tower.rules .nil result (subst arguments sourceType) :=
  source_judgment.substitute .nil arguments_typed

theorem actual_beta_execution : TelescopeAbstraction.BetaSteps applied result :=
  TelescopeAbstraction.applyClosed_beta sourceContext arguments sourceBody

def computation :=
  FormationCheckedTelescopePrograms.applicationCode sourceContext arguments sourceBody

theorem computation_checked :
    computation.check (fun _ _ => False) FormationCheckedTelescopePrograms.noRoot
      applied result = true :=
  FormationCheckedTelescopePrograms.applicationCode_checked sourceContext arguments sourceBody

#guard computation.stepCount == 2
#guard computation.check (fun _ _ => False) FormationCheckedTelescopePrograms.noRoot applied result
#guard !(computation.check (fun _ _ => False) FormationCheckedTelescopePrograms.noRoot
  applied (.refl (.head (.sort zero))))

/-- An unbound declaration may occur in the raw grammar while the refined
judgment refuses its use as an identity function's domain. -/
theorem missing_formation_refused :
    ¬ Judgment Tower.rules .nil (.lam (.var 0))
      (.pi (.const `Missing) (.const `Missing)) := by
  exact (raw_identity_missing_domain towerUniverseRegularity (name := `Missing) rfl).2

#print axioms context_formed
#print axioms source_judgment
#print axioms arguments_typed
#print axioms applied_judgment
#print axioms result_judgment
#print axioms actual_beta_execution
#print axioms computation_checked
#print axioms missing_formation_refused

open FormationSensitiveContextual

abbrev semanticSource : Context Tower.rules := ⟨2, sourceContext, context_formed⟩
abbrev semanticTarget : Context Tower.rules := ⟨0, .nil, .nil⟩
def semanticType : TypeOver semanticSource :=
  ⟨sourceType, .sort one, .sort one, .idForm (.var 1) (.sort one) (.var 0) (.var 0)⟩
def semanticBody : Term semanticSource semanticType := ⟨sourceBody, source_judgment.typing⟩
def semanticArguments : Hom semanticTarget semanticSource := ⟨arguments, arguments_typed⟩

theorem application_semantic_value :
    TermFibre.mk (ProgrammableJudgmentSemantics.application towerUniverseRegularity
      FormationCheckedTelescopePrograms.tower_joins semanticBody semanticArguments) =
      (TermFibre.mk semanticBody).reindex semanticArguments :=
  ProgrammableJudgmentSemantics.application_value Tower.Checking.decoder towerUniverseRegularity
    FormationCheckedTelescopePrograms.tower_joins semanticBody semanticArguments

/-- Conversion observation identifies these values; it does not identify
the retained application syntax with the returned reflexivity syntax. -/
theorem application_keeps_distinct_code :
    (ProgrammableJudgmentSemantics.application towerUniverseRegularity
      FormationCheckedTelescopePrograms.tower_joins semanticBody semanticArguments).code ≠
        (semanticBody.reindex semanticArguments).code := by
  intro same
  cases same

#print axioms application_semantic_value
#print axioms application_keeps_distinct_code

end PrimeTypingJudgmentStudy

#print axioms Mettapedia.GSLT.LanguageDef.CertificateGSLT.applyOpen?_exact
#print axioms Mettapedia.GSLT.LanguageDef.CertificateGSLT.checked_substitute_assoc
#print axioms Mettapedia.GSLT.LanguageDef.CertificateGSLT.applyOpen?_interprets
#print axioms Mettapedia.GSLT.LanguageDef.InferenceCettaWire.checkDerivationValue_exact
#print axioms Mettapedia.GSLT.LanguageDef.InferenceRuleSupport.checkOpenRaw_eq_of_agreement
#print axioms Mettapedia.Languages.MeTTa.HE.Spec.Type.RulePresentation.source_iff_tree
#print axioms Mettapedia.Languages.MeTTa.HE.TypeRuleData.readSequent_sequentData
#print axioms Mettapedia.Languages.HOLLight.MeTTa.RuleData.read_invocation_request
#print axioms Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaData.two_forms_same_admission
#print axioms Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaDataControls.block_vector_guest_formed
#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.StructuralConversionPrograms.zero_budget_can_miss_beta
#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.IdentityConversionBoundary.identity_without_judgmental_conversion

#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.Declaration.checkComplete_selected
#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.Declaration.checkComplete_computes
#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.DeclarationConversionCode.changed_delta_body_refuses_old_endpoints

#print axioms Mettapedia.GSLT.LanguageDef.CertificateGSLT.applyOpen?_operational
#print axioms Mettapedia.GSLT.LanguageDef.CertificateGSLT.checked_ledger_iff_closureDiamond

#print axioms Mettapedia.GSLT.LanguageDef.CheckedSource.GSLTSource.applyOpen_exact
#print axioms Mettapedia.GSLT.LanguageDef.CheckedSource.GSLTSource.applyOpen_preserves_identity

#print axioms Mettapedia.Languages.MeTTa.ProgrammableJudgmentTransforms.step_preserves_validity
#print axioms Mettapedia.Languages.MeTTa.ProgrammableJudgmentTransforms.prepare_publish_fusion
#print axioms Mettapedia.Languages.MeTTa.ProgrammableJudgmentServices.program_intents_preserve
#print axioms Mettapedia.Languages.MeTTa.ProgrammableJudgmentServices.unfinished_episode

#print axioms Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased.ContextualJudgmentReadback.read_rule_application
#print axioms Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased.ContextualJudgmentReadback.nativeClosed_exact
#print axioms Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaPackageControls.vector_packet_readback
#print axioms Mettapedia.GSLT.LanguageDef.ModuleSource.InductiveMeTTaPackageControls.vector_receipts

#print axioms Mettapedia.Languages.HOLLight.MeTTa.RulePrograms.plan_trace_history
#print axioms Mettapedia.Languages.HOLLight.ExportReplay.composed_root
#print axioms Mettapedia.Languages.HOLLight.ExportReplay.replayFrom_policy_congr
#print axioms Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.Declaration.completed_call_typed
#print axioms Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.AppendDefinitionCompletion.stale_negative_changes
#print axioms Mettapedia.GSLT.LanguageDef.IndexedInductive.AppendSourceCompletion.source_body_checked
