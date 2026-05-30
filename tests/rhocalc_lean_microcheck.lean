import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Reduction
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.MultiStep
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Engine
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.SemanticSubstitution
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Soundness

namespace CeTTa.RhocalcLeanMicrocheck

open Mettapedia.OSLF.MeTTaIL.Syntax
open Mettapedia.OSLF.MeTTaIL.Substitution
open Mettapedia.Languages.ProcessCalculi.RhoCalculus
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Reduction
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Soundness

local notation "possibly" => possiblyProp
local notation "rely" => relyProp

def emptyBag : Pattern := .collection .hashBag [] none

def channel : Pattern := .fvar "c"

def payload : Pattern := emptyBag

def body : Pattern := .apply "PDrop" [.bvar 0]

def commInput : Pattern :=
  .collection .hashBag
    [.apply "POutput" [channel, payload],
     .apply "PInput" [channel, .lambda none body]]
    none

def commInputReduct : Pattern :=
  .collection .hashBag [semanticCommSubst body payload] none

def commInputSemanticReduct : Pattern :=
  commInputReduct

def freeDrop : Pattern :=
  .apply "PDrop" [.apply "NQuote" [emptyBag]]

def chainChannel : Pattern :=
  .apply "NQuote" [.apply "POutput" [channel, payload]]

def chainSend : Pattern :=
  .apply "POutput" [chainChannel, emptyBag]

def chainRecv : Pattern :=
  .apply "PInput" [chainChannel, .lambda none emptyBag]

def commParChain : Pattern :=
  .collection .hashBag
    [.apply "POutput" [channel, payload],
     .apply "PInput" [channel, .lambda none chainSend],
     chainRecv]
    none

def commParChainReduct : Pattern :=
  .collection .hashBag [semanticCommSubst chainSend payload, chainRecv] none

theorem commInput_canStep : CanStep commInput := by
  rcases comm_reduces (n := channel) (q := payload) (p := body) with ⟨r, h⟩
  exact ⟨r, ⟨h⟩⟩

theorem commInput_has_one_step : ∃ q, Nonempty (commInput ⇝[1] q) := by
  rcases comm_reduces (n := channel) (q := payload) (p := body) with ⟨r, h⟩
  exact ⟨r, (ReducesN.one_iff_reduces _ _).mpr ⟨h⟩⟩

theorem commInput_presentMoment_contains_expected_reduct :
    commInputReduct ∈ Spice.presentMoment commInput := by
  refine (mem_presentMoment_iff_reduces).2 ?_
  simpa [commInput, commInputReduct] using (⟨@Reduces.comm channel payload body []⟩ :
    Nonempty (Reduces
      (.collection .hashBag
        ([.apply "POutput" [channel, payload],
          .apply "PInput" [channel, .lambda none body]] ++ [])
        none)
      (.collection .hashBag ([semanticCommSubst body payload] ++ []) none)))

theorem commInput_reachableStates_one_overapproximates :
    commInput ∈ Spice.reachableStates commInput 1 ∧
      commInputReduct ∈ Spice.presentMoment commInput ∧
      commInputReduct ≠ commInput := by
  refine ⟨current_mem_reachableStates_one commInput,
          commInput_presentMoment_contains_expected_reduct, ?_⟩
  intro h
  have hlen := congrArg
    (fun p =>
      match p with
      | .collection .hashBag elems none => elems.length
      | _ => 0)
    h
  simp [commInput, commInputReduct] at hlen

theorem commInput_engine_reduces_exactly :
    Engine.reduceStep commInput 1 = [commInputReduct] := by
  native_decide

theorem commInput_engine_sound :
    ∀ q, q ∈ Engine.reduceStep commInput 1 → q ∈ Spice.presentMoment commInput := by
  intro q hq
  exact reduceStep_mem_presentMoment hq

theorem commInput_engine_complete_for_expected_reduct :
    commInputReduct ∈ Engine.reduceStep commInput 1 := by
  simp [commInput_engine_reduces_exactly]

theorem commInput_semantic_substituted_body_is_emptyBag :
    semanticCommSubst body payload = emptyBag := by
  native_decide

theorem commInput_semantic_reduct_SC_emptyBag :
    StructuralCongruence commInputSemanticReduct emptyBag := by
  simpa [commInputSemanticReduct, emptyBag] using
    StructuralCongruence.par_singleton emptyBag

theorem commInput_live_reduct_agrees_with_semantic_target :
    commInputReduct = commInputSemanticReduct := by
  rfl

theorem commParChain_has_one_step : ∃ q, Nonempty (commParChain ⇝[1] q) := by
  refine ⟨commParChainReduct, ?_⟩
  exact (ReducesN.one_iff_reduces _ _).mpr
    ⟨by
      simpa [commParChain, commParChainReduct] using
        (@Reduces.comm channel payload chainSend [chainRecv])⟩

theorem commParChain_presentMoment_contains_expected_reduct :
    commParChainReduct ∈ Spice.presentMoment commParChain := by
  refine (mem_presentMoment_iff_reduces).2 ?_
  exact ⟨by
    simpa [commParChain, commParChainReduct] using
      (@Reduces.comm channel payload chainSend [chainRecv])⟩

theorem commParChain_reachableStates_one_overapproximates :
    commParChain ∈ Spice.reachableStates commParChain 1 ∧
      commParChainReduct ∈ Spice.presentMoment commParChain ∧
      commParChainReduct ≠ commParChain := by
  refine ⟨current_mem_reachableStates_one commParChain,
          commParChain_presentMoment_contains_expected_reduct, ?_⟩
  intro h
  have hlen := congrArg
    (fun p =>
      match p with
      | .collection .hashBag elems none => elems.length
      | _ => 0)
    h
  simp [commParChain, commParChainReduct] at hlen

theorem commParChain_engine_reduces_exactly :
    Engine.reduceStep commParChain 1 = [commParChainReduct] := by
  native_decide

theorem commParChain_engine_sound :
    ∀ q, q ∈ Engine.reduceStep commParChain 1 → q ∈ Spice.presentMoment commParChain := by
  intro q hq
  exact reduceStep_mem_presentMoment hq

theorem commParChain_engine_complete_for_expected_reduct :
    commParChainReduct ∈ Engine.reduceStep commParChain 1 := by
  simp [commParChain_engine_reduces_exactly]

theorem emptyBag_zero_step : Nonempty (emptyBag ⇝[0] emptyBag) := by
  exact ⟨ReducesN.zero _⟩

theorem emptyBag_normalForm : NormalForm emptyBag := by
  intro h
  rcases h with ⟨q, ⟨hred⟩⟩
  exact emptyBag_SC_irreducible (StructuralCongruence.refl _) hred

theorem emptyBag_reachableStates_one_false_positive :
    emptyBag ∈ Spice.reachableStates emptyBag 1 ∧
      emptyBag ∉ Spice.presentMoment emptyBag := by
  exact reachableStates_one_false_positive_of_normalForm emptyBag_normalForm

theorem freeDrop_engine_reduces_exactly :
    Engine.reduceStep freeDrop 1 = [] := by
  native_decide

def literalDropStatic : Pattern :=
  .apply "PDrop"
    [.apply "NQuote"
      [.apply "POutput" [channel, payload]]]

theorem semanticCommSubst_collapses_drop_to_payload :
    semanticCommSubst (.apply "PDrop" [.bvar 0]) emptyBag = emptyBag := by
  simpa [emptyBag] using semanticCommSubst_collapses_bound_drop emptyBag

theorem semanticCommSubst_preserves_literal_drop_static :
    semanticCommSubst literalDropStatic emptyBag = literalDropStatic := by
  simpa [literalDropStatic, channel, payload, emptyBag] using
    semanticCommSubst_preserves_output_literal_drop channel payload emptyBag

theorem semanticCommSubst_literal_drop_static_agrees_with_syntactic_open :
    semanticCommSubst literalDropStatic emptyBag =
      Mettapedia.OSLF.MeTTaIL.Substitution.commSubst literalDropStatic emptyBag := by
  native_decide

theorem semanticCommSubst_preserves_quote_opacity :
    semanticCommSubst
      (.apply "POutput"
        [.apply "NQuote" [.apply "POutput" [.bvar 0, emptyBag]], emptyBag])
      emptyBag
      =
      (.apply "POutput"
        [.apply "NQuote" [.apply "POutput" [.bvar 0, emptyBag]], emptyBag]) := by
  simpa [emptyBag] using
    semanticCommSubst_preserves_quote_opacity_output_emptyBag emptyBag

theorem semanticCommSubst_diverges_from_syntactic_open_on_quote_opacity :
    semanticCommSubst
      (.apply "POutput"
        [.apply "NQuote" [.apply "POutput" [.bvar 0, emptyBag]], emptyBag])
      emptyBag
      ≠
      Mettapedia.OSLF.MeTTaIL.Substitution.commSubst
        (.apply "POutput"
          [.apply "NQuote" [.apply "POutput" [.bvar 0, emptyBag]], emptyBag])
        emptyBag := by
  simpa [emptyBag] using
    semanticCommSubst_diverges_from_syntactic_open_on_quote_opacity_emptyBag

theorem semanticCommSubst_diverges_from_syntactic_open_on_bound_drop :
    semanticCommSubst (.apply "PDrop" [.bvar 0]) emptyBag ≠
      Mettapedia.OSLF.MeTTaIL.Substitution.openBVar 0
        (.apply "NQuote" [emptyBag])
        (.apply "PDrop" [.bvar 0]) := by
  simpa [emptyBag] using semanticCommSubst_bound_drop_differs_from_syntactic_open_on_emptyBag

def quoteDropWholeNameBody : Pattern :=
  .apply "POutput"
    [.apply "NQuote" [.apply "PDrop" [.fvar "x"]], emptyBag]

def quoteDropWholeNamePayload : Pattern :=
  .apply "POutput" [.apply "NQuote" [emptyBag], emptyBag]

def quoteDropWholeNameComm : Pattern :=
  .collection .hashBag
    [.apply "POutput" [channel, quoteDropWholeNamePayload],
     .apply "PInput" [channel, .lambda none quoteDropWholeNameBody]]
    none

def quoteDropWholeNameCommLiveExpected : Pattern :=
  .collection .hashBag [.apply "POutput" [.fvar "x", emptyBag]] none

theorem semanticCommSubst_normalizes_free_quote_drop_channel :
    semanticCommSubst quoteDropWholeNameBody quoteDropWholeNamePayload
      = .apply "POutput" [.fvar "x", emptyBag] := by
  simpa [quoteDropWholeNameBody, quoteDropWholeNamePayload, emptyBag] using
    semanticCommSubst_normalizes_free_quote_drop_output_channel_empty_payload "x"
      quoteDropWholeNamePayload

theorem semanticCommSubst_diverges_from_syntactic_open_on_quote_drop_whole_name :
    semanticCommSubst quoteDropWholeNameBody quoteDropWholeNamePayload ≠
      Mettapedia.OSLF.MeTTaIL.Substitution.commSubst
        quoteDropWholeNameBody quoteDropWholeNamePayload := by
  native_decide

 theorem quoteDropWholeName_engine_reduces_exactly_live :
    Engine.reduceStep quoteDropWholeNameComm 1 = [quoteDropWholeNameCommLiveExpected] := by
  native_decide

theorem quoteDropWholeName_live_reduct_agrees_with_semantic_target :
    Engine.reduceStep quoteDropWholeNameComm 1 = [quoteDropWholeNameCommLiveExpected] := by
  native_decide

def alphaEmpty : NamePred := fun p => p = emptyBag

def semanticTypingCounterexampleCtx : TypingContext :=
  TypingContext.extend TypingContext.empty "x" ⟨"Name", alphaEmpty, by simp⟩

def semanticTypingCounterexampleOrig : Pattern :=
  .apply "PDrop" [.apply "NQuote" [.apply "PDrop" [.fvar "x"]]]

def semanticTypingCounterexampleNorm : Pattern :=
  semanticNormalizeProc semanticTypingCounterexampleOrig

theorem alphaEmpty_ne_possibly_rely_alphaEmpty :
    alphaEmpty ≠ possibly (rely alphaEmpty) := by
  intro hEq
  have h2 : possibly (rely alphaEmpty) emptyBag := by
    rw [← hEq]
    exact rfl
  rcases h2 with ⟨q, hred, _⟩
  exact emptyBag_SC_irreducible (StructuralCongruence.refl emptyBag) hred.some

theorem semanticTypingCounterexample_orig_typed :
    HasType semanticTypingCounterexampleCtx semanticTypingCounterexampleOrig
      ⟨"Proc", rely (possibly (rely alphaEmpty)), by simp⟩ := by
  unfold semanticTypingCounterexampleOrig semanticTypingCounterexampleCtx
  apply HasType.drop
  apply HasType.quote
  apply HasType.drop
  apply HasType.fvar
  simp [TypingContext.extend, TypingContext.lookup]

theorem semanticTypingCounterexample_norm_eq :
    semanticTypingCounterexampleNorm = .apply "PDrop" [.fvar "x"] := by
  rfl

theorem semanticTypingCounterexample_norm_typed :
    HasType semanticTypingCounterexampleCtx semanticTypingCounterexampleNorm
      ⟨"Proc", rely alphaEmpty, by simp⟩ := by
  rw [semanticTypingCounterexample_norm_eq]
  unfold semanticTypingCounterexampleCtx
  apply HasType.drop
  apply HasType.fvar
  simp [TypingContext.extend, TypingContext.lookup]

theorem semanticTypingCounterexample_orig_typed_upToSubjectEquiv :
    HasTypeUpToSubjectEquiv
      semanticTypingCounterexampleCtx
      semanticTypingCounterexampleOrig
      ⟨"Proc", rely alphaEmpty, by simp⟩ := by
  refine ⟨semanticTypingCounterexampleNorm, ?_, semanticTypingCounterexample_norm_typed⟩
  apply TypeSubjectEquiv.of_proc
  apply ProcResidualEquiv.struct
  exact StructuralCongruence.symm _ _ (semanticNormalizeProc_sound semanticTypingCounterexampleOrig)

theorem commInput_semantic_transport_to_representative :
    ProcResidualEquiv
      (semanticCommSubst body payload)
      (semanticCommRepresentative body payload) := by
  simpa [body, payload] using
    semanticCommSubst_transport_to_representative body payload

theorem commInput_representative_typed :
    HasType TypingContext.empty (semanticCommRepresentative body payload)
      ⟨"Proc", rely (possibly (fun _ => True)), by simp⟩ := by
  unfold semanticCommRepresentative body payload emptyBag
  simp [semanticSubstProcNoCollapse, semanticSubstName, semanticSubstNameMark]
  apply HasType.drop
  apply HasType.quote
  apply HasType.par
  intro p hp
  cases hp

theorem commInput_semantic_typed_upToSubjectEquiv_via_representative :
    HasTypeUpToSubjectEquiv
      TypingContext.empty
      (semanticCommSubst body payload)
      ⟨"Proc", rely (possibly (fun _ => True)), by simp⟩ := by
  exact comm_preserves_type_semantic_upToSubjectEquiv_of_representative
    (Γ := TypingContext.empty)
    (p_body := body)
    (q := payload)
    (φ := rely (possibly (fun _ => True)))
    commInput_representative_typed

theorem emptyBag_typed_top_in (Γ : TypingContext) :
    HasType Γ emptyBag ⟨"Proc", fun _ => True, by simp⟩ := by
  unfold emptyBag
  exact HasType.par (by intro p hp; cases hp)

theorem emptyBag_typed_top :
    HasType TypingContext.empty emptyBag ⟨"Proc", fun _ => True, by simp⟩ := by
  exact emptyBag_typed_top_in TypingContext.empty

theorem commInput_body_hypothesis :
    ∀ z, z ∉ ([] : List String) →
      HasType
        (TypingContext.extend TypingContext.empty z ⟨"Name", possibly (fun _ => True), by simp⟩)
        (openBVar 0 (.fvar z) body)
        ⟨"Proc", rely (possibly (fun _ => True)), by simp⟩ := by
  intro z _
  simp [body, openBVar]
  apply HasType.drop
  apply HasType.fvar
  exact lookup_extend_eq

theorem commInput_body_noBoundUnderQuote :
    noBoundUnderQuote 0 body = true := by
  native_decide

theorem commInput_body_rhoProcCoreShape :
    rhoProcCoreShape body = true := by
  native_decide

theorem commInput_body_strictCoreCommBody :
    strictCoreCommBody body = true := by
  native_decide

theorem library_shape_boundary_is_real :
    ∃ p, noBoundUnderQuote 0 p = true ∧ rhoProcCoreShape p = false := by
  exact noBoundUnderQuote_not_sufficient_for_rhoProcCoreShape

theorem commInput_representative_residual_equiv_rho_open :
    ProcResidualEquiv
      (semanticCommRepresentative body payload)
      (rhoOpenNameBVar 0 (.apply "NQuote" [payload]) body) := by
  exact semanticCommRepresentative_residual_equiv_rhoOpenNameBVar_of_rhoProcCoreShape
    commInput_body_rhoProcCoreShape
    commInput_body_noBoundUnderQuote

theorem commInput_semantic_typed_upToSubjectEquiv_via_strictCoreCommBody :
    HasTypeUpToSubjectEquiv
      TypingContext.empty
      (semanticCommSubst body payload)
      ⟨"Proc", rely (possibly (fun _ => True)), by simp⟩ := by
  exact comm_preserves_type_semantic_upToSubjectEquiv_of_strictCoreCommBody
    (Γ := TypingContext.empty)
    (p_body := body)
    (q := payload)
    (φ := rely (possibly (fun _ => True)))
    (L := [])
    commInput_body_hypothesis
    emptyBag_typed_top
    (by native_decide)
    commInput_body_strictCoreCommBody

theorem commInput_semantic_in_saturated_typedAt_via_strictCoreCommBody :
    saturateProcPred
      (fun p =>
        HasType TypingContext.empty p
          ⟨"Proc", rely (possibly (fun _ => True)), by simp⟩)
      (semanticCommSubst body payload) := by
  exact comm_preserves_type_semantic_saturated_typedAt_of_strictCoreCommBody
    (Γ := TypingContext.empty)
    (p_body := body)
    (q := payload)
    (φ := rely (possibly (fun _ => True)))
    (L := [])
    commInput_body_hypothesis
    emptyBag_typed_top
    (by native_decide)
    commInput_body_strictCoreCommBody

theorem hasType_bvar_impossible {Γ : TypingContext} {n : Nat} {τ : NativeType} :
    ¬ HasType Γ (.bvar n) τ := by
  intro h
  cases h

theorem hasType_quote_inv {Γ : TypingContext} {p : Pattern} {τ : NativeType}
    (h : HasType Γ (.apply "NQuote" [p]) τ) :
    ∃ φ : ProcPred,
      τ = ⟨"Name", possibly φ, by simp⟩ ∧
      HasType Γ p ⟨"Proc", φ, by simp⟩ := by
  generalize hpEq : (.apply "NQuote" [p] : Pattern) = pat at h
  cases h with
  | fvar hlookup =>
      simp at hpEq
  | nil =>
      simp at hpEq
  | quote hp =>
      injection hpEq with _ hargs
      cases hargs
      exact ⟨_, rfl, hp⟩
  | drop _ =>
      simp at hpEq
  | output _ _ =>
      simp at hpEq
  | input _ _ =>
      simp at hpEq
  | par _ =>
      simp at hpEq

theorem hasType_output_inv {Γ : TypingContext} {n q : Pattern} {τ : NativeType}
    (h : HasType Γ (.apply "POutput" [n, q]) τ) :
    ∃ α : NamePred, ∃ φ : ProcPred,
      τ = ⟨"Proc", fun _ => True, by simp⟩ ∧
      HasType Γ n ⟨"Name", α, by simp⟩ ∧
      HasType Γ q ⟨"Proc", φ, by simp⟩ := by
  generalize hpEq : (.apply "POutput" [n, q] : Pattern) = pat at h
  cases h with
  | fvar hlookup =>
      simp at hpEq
  | nil =>
      simp at hpEq
  | quote _ =>
      simp at hpEq
  | drop _ =>
      simp at hpEq
  | output hname hproc =>
      simp at hpEq
      rcases hpEq with ⟨hn, hq⟩
      cases hn
      cases hq
      exact ⟨_, _, rfl, hname, hproc⟩
  | input _ _ =>
      simp at hpEq
  | par _ =>
      simp at hpEq

def quoteOpacityBody : Pattern :=
  .apply "POutput"
    [.apply "NQuote" [.apply "POutput" [.bvar 0, emptyBag]], emptyBag]

theorem quoteOpacityBody_hypothesis :
    ∀ z, z ∉ ([] : List String) →
      HasType
        (TypingContext.extend TypingContext.empty z ⟨"Name", possibly (fun _ => True), by simp⟩)
        (openBVar 0 (.fvar z) quoteOpacityBody)
        ⟨"Proc", fun _ => True, by simp⟩ := by
  intro z _
  simp [quoteOpacityBody, openBVar, emptyBag]
  apply HasType.output
  · apply HasType.quote
    apply HasType.output
    · apply HasType.fvar
      exact lookup_extend_eq
    · exact emptyBag_typed_top_in _
  · exact emptyBag_typed_top_in _

theorem quoteOpacityBody_noBoundUnderQuote_false :
    noBoundUnderQuote 0 quoteOpacityBody = false := by
  native_decide

theorem quoteOpacityRepresentative_eq_body :
    semanticCommRepresentative quoteOpacityBody payload = quoteOpacityBody := by
  native_decide

theorem quoteOpacityBody_untypable_empty {τ : NativeType} :
    ¬ HasType TypingContext.empty quoteOpacityBody τ := by
  intro h
  obtain ⟨_, _, _, hchan, _⟩ := hasType_output_inv h
  obtain ⟨_, _, hinner⟩ := hasType_quote_inv hchan
  obtain ⟨_, _, _, hname, _⟩ := hasType_output_inv hinner
  exact hasType_bvar_impossible hname

theorem quoteOpacityRepresentative_untypable_empty :
    ¬ ∃ φ : ProcPred,
        HasType TypingContext.empty
          (semanticCommRepresentative quoteOpacityBody payload)
          ⟨"Proc", φ, by simp⟩ := by
  intro h
  rcases h with ⟨φ, hφ⟩
  rw [quoteOpacityRepresentative_eq_body] at hφ
  exact quoteOpacityBody_untypable_empty hφ

theorem quoteOpacityBody_obstruction_survives :
    (∀ z, z ∉ ([] : List String) →
      HasType
        (TypingContext.extend TypingContext.empty z ⟨"Name", possibly (fun _ => True), by simp⟩)
        (openBVar 0 (.fvar z) quoteOpacityBody)
        ⟨"Proc", fun _ => True, by simp⟩) ∧
      noBoundUnderQuote 0 quoteOpacityBody = false ∧
      ¬ ∃ φ : ProcPred,
          HasType TypingContext.empty
            (semanticCommRepresentative quoteOpacityBody payload)
            ⟨"Proc", φ, by simp⟩ := by
  exact ⟨quoteOpacityBody_hypothesis,
    quoteOpacityBody_noBoundUnderQuote_false,
    quoteOpacityRepresentative_untypable_empty⟩

theorem quoteDropWholeNamePayload_typed_top_ctx :
    HasType semanticTypingCounterexampleCtx quoteDropWholeNamePayload
      ⟨"Proc", fun _ => True, by simp⟩ := by
  unfold quoteDropWholeNamePayload
  apply HasType.output
  · apply HasType.quote
    exact emptyBag_typed_top_in _
  · exact emptyBag_typed_top_in _

theorem quoteDropWholeName_body_hypothesis :
    ∀ z, z ∉ (["x"] : List String) →
      HasType
        (TypingContext.extend semanticTypingCounterexampleCtx z
          ⟨"Name", possibly (fun _ => True), by simp⟩)
        (openBVar 0 (.fvar z) quoteDropWholeNameBody)
        ⟨"Proc", fun _ => True, by simp⟩ := by
  intro z hz
  have hzx : z ≠ "x" := by
    intro hEq
    apply hz
    simp [hEq]
  simp [quoteDropWholeNameBody, openBVar, emptyBag]
  apply HasType.output
  · apply HasType.quote
    apply HasType.drop
    apply HasType.fvar
    rw [lookup_extend_neq hzx]
    unfold semanticTypingCounterexampleCtx
    exact lookup_extend_eq
  · exact emptyBag_typed_top_in _

theorem quoteDropWholeName_body_noBoundUnderQuote :
    noBoundUnderQuote 0 quoteDropWholeNameBody = true := by
  native_decide

theorem quoteDropWholeName_representative_eq_live :
    semanticCommRepresentative quoteDropWholeNameBody quoteDropWholeNamePayload =
      .apply "POutput" [.fvar "x", emptyBag] := by
  native_decide

theorem quoteDropWholeName_representative_typed_ctx :
    HasType semanticTypingCounterexampleCtx
      (semanticCommRepresentative quoteDropWholeNameBody quoteDropWholeNamePayload)
      ⟨"Proc", fun _ => True, by simp⟩ := by
  rw [quoteDropWholeName_representative_eq_live]
  apply HasType.output
  · apply HasType.fvar
    unfold semanticTypingCounterexampleCtx
    exact lookup_extend_eq
  · exact emptyBag_typed_top_in _

theorem quoteDropWholeName_semantic_in_saturated_typedAt_via_representative :
    saturateProcPred
      (fun p =>
        HasType semanticTypingCounterexampleCtx p
          ⟨"Proc", fun _ => True, by simp⟩)
      (semanticCommSubst quoteDropWholeNameBody quoteDropWholeNamePayload) := by
  exact comm_preserves_type_semantic_saturated_typedAt_of_representative
    (Γ := semanticTypingCounterexampleCtx)
    (p_body := quoteDropWholeNameBody)
    (q := quoteDropWholeNamePayload)
    (φ := fun _ => True)
    quoteDropWholeName_representative_typed_ctx

def quoteDropWholeNameDropBody : Pattern :=
  semanticTypingCounterexampleOrig

def quoteDropWholeNameDropPayload : Pattern :=
  emptyBag

def quoteDropWholeNameDropChannel : Pattern :=
  .apply "NQuote" [emptyBag]

def quoteDropWholeNameDropComm : Pattern :=
  .collection .hashBag
    [.apply "POutput" [quoteDropWholeNameDropChannel, quoteDropWholeNameDropPayload],
     .apply "PInput" [quoteDropWholeNameDropChannel, .lambda none quoteDropWholeNameDropBody]]
    none

def quoteDropWholeNameDropCommLiveExpected : Pattern :=
  .collection .hashBag [semanticTypingCounterexampleNorm] none

theorem quoteDropWholeNameDrop_engine_reduces_exactly_live :
    Engine.reduceStep quoteDropWholeNameDropComm 1 = [quoteDropWholeNameDropCommLiveExpected] := by
  native_decide

theorem quoteDropWholeNameDrop_body_strictCoreCommBody :
    strictCoreCommBody quoteDropWholeNameDropBody = true := by
  native_decide

theorem quoteDropWholeNameDrop_body_hypothesis :
    ∀ z, z ∉ (["x"] : List String) →
      HasType
        (TypingContext.extend semanticTypingCounterexampleCtx z
          ⟨"Name", possibly (fun _ => True), by simp⟩)
        (openBVar 0 (.fvar z) quoteDropWholeNameDropBody)
        ⟨"Proc", rely (possibly (rely alphaEmpty)), by simp⟩ := by
  intro z hz
  have hzx : z ≠ "x" := by
    intro hEq
    apply hz
    simp [hEq]
  simp [quoteDropWholeNameDropBody, semanticTypingCounterexampleOrig, openBVar]
  apply HasType.drop
  apply HasType.quote
  apply HasType.drop
  apply HasType.fvar
  rw [lookup_extend_neq hzx]
  unfold semanticTypingCounterexampleCtx
  exact lookup_extend_eq

theorem quoteDropWholeNameDrop_representative_eq_live :
    semanticCommRepresentative quoteDropWholeNameDropBody quoteDropWholeNameDropPayload =
      semanticTypingCounterexampleNorm := by
  native_decide

theorem quoteDropWholeNameDrop_representative_typed_ctx :
    HasType semanticTypingCounterexampleCtx
      (semanticCommRepresentative quoteDropWholeNameDropBody quoteDropWholeNameDropPayload)
      ⟨"Proc", rely alphaEmpty, by simp⟩ := by
  rw [quoteDropWholeNameDrop_representative_eq_live]
  exact semanticTypingCounterexample_norm_typed

theorem quoteDropWholeNameDrop_semantic_in_saturated_typedAt_via_representative :
    saturateProcPred
      (fun p =>
        HasType semanticTypingCounterexampleCtx p
          ⟨"Proc", rely alphaEmpty, by simp⟩)
      (semanticCommSubst quoteDropWholeNameDropBody quoteDropWholeNameDropPayload) := by
  exact comm_preserves_type_semantic_saturated_typedAt_of_representative
    (Γ := semanticTypingCounterexampleCtx)
    (p_body := quoteDropWholeNameDropBody)
    (q := quoteDropWholeNameDropPayload)
    (φ := rely alphaEmpty)
    quoteDropWholeNameDrop_representative_typed_ctx

theorem quoteDropWholeNameDrop_semantic_in_saturated_typedAt_via_strictCoreCommBody :
    saturateProcPred
      (fun p =>
        HasType semanticTypingCounterexampleCtx p
          ⟨"Proc", rely (possibly (rely alphaEmpty)), by simp⟩)
      (semanticCommSubst quoteDropWholeNameDropBody quoteDropWholeNameDropPayload) := by
  exact comm_preserves_type_semantic_saturated_typedAt_of_strictCoreCommBody
    (Γ := semanticTypingCounterexampleCtx)
    (p_body := quoteDropWholeNameDropBody)
    (q := quoteDropWholeNameDropPayload)
    (φ := rely (possibly (rely alphaEmpty)))
    (L := ["x"])
    quoteDropWholeNameDrop_body_hypothesis
    (emptyBag_typed_top_in semanticTypingCounterexampleCtx)
    (by native_decide)
    quoteDropWholeNameDrop_body_strictCoreCommBody

end CeTTa.RhocalcLeanMicrocheck
