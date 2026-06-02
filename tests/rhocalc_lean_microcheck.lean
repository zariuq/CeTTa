import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Reduction
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.MultiStep
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Engine
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.PresentMoment
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.SemanticSubstitution
import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Soundness
import Mettapedia.GSLT.Meredith.InteractiveGSLT
import Mettapedia.GSLT.Meredith.InteractiveCostBridge

namespace CeTTa.RhocalcLeanMicrocheck

open Mettapedia.GSLT
open Mettapedia.OSLF.MeTTaIL.Syntax
open Mettapedia.OSLF.MeTTaIL.Substitution
open Mettapedia.GSLT.Meredith.RhoExample
open Mettapedia.Languages.ProcessCalculi.RhoCalculus
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.PresentMoment
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Reduction
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Soundness

local notation "possibly" => possiblyProp
local notation "rely" => relyProp

def emptyBag : Pattern := .collection .hashBag [] none

def channel : Pattern := .fvar "c"

def payload : Pattern := emptyBag

def body : Pattern := .apply "PDrop" [.bvar 0]

def costSpentExample : Pattern :=
  .apply "rho:cost:sig-mul" [.fvar "alice", .fvar "bob"]

def costSpentExampleSecond : Pattern :=
  .fvar "carol"

def costChannel : Pattern := .fvar "pay"

def costPayloadTerm : Pattern := .fvar "payload"

def costBodyTerm : Pattern := .fvar "cont"

def zeroCostGrade : RhoLedger := 0

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

/-- Source-side quote-drop channel normalization example: the output channel is
`@(*(@0))`, the input channel is `@0`, and the engine should still match them
the same way the live C runtime does. -/
def quoteDropChannelNorm : Pattern :=
  .apply "NQuote" [emptyBag]

def quoteDropChannelAlias : Pattern :=
  .apply "NQuote" [.apply "PDrop" [quoteDropChannelNorm]]

def quoteDropChannelComm : Pattern :=
  .collection .hashBag
    [.apply "POutput" [quoteDropChannelAlias, payload],
     .apply "PInput" [quoteDropChannelNorm, .lambda none body]]
    none

def quoteDropChannelCommReduct : Pattern :=
  .collection .hashBag [semanticCommSubst body payload] none

/-- "Racing senders" frontier example: two sends compete for one receive on the
same channel, producing two distinct one-step COMM successors. -/
def branchingPayload : Pattern :=
  .apply "POutput" [channel, payload]

def branchingNestedSend : Pattern :=
  .apply "POutput" [channel, branchingPayload]

def branchingBody : Pattern :=
  .apply "PDrop" [.bvar 0]

def branchingElems : List Pattern :=
  [.apply "POutput" [channel, payload],
   branchingNestedSend,
   .apply "PInput" [channel, .lambda none branchingBody]]

def branchingComm : Pattern :=
  .collection .hashBag branchingElems none

def branchingReductReceiveEmpty : Pattern :=
  .collection .hashBag [emptyBag, branchingNestedSend] none

def branchingReductReceiveNested : Pattern :=
  .collection .hashBag [branchingPayload, branchingPayload] none

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

theorem quoteDropChannelComm_engine_reduces_exactly :
    Engine.reduceStep quoteDropChannelComm 1 = [quoteDropChannelCommReduct] := by
  native_decide

theorem quoteDropChannelComm_engine_complete_for_expected_reduct :
    quoteDropChannelCommReduct ∈ Engine.reduceStep quoteDropChannelComm 1 := by
  simp [quoteDropChannelComm_engine_reduces_exactly]

theorem quoteDropChannelComm_presentMoment_contains_expected_reduct :
    quoteDropChannelCommReduct ∈ Spice.presentMoment quoteDropChannelComm := by
  exact reduceStep_mem_presentMoment
    quoteDropChannelComm_engine_complete_for_expected_reduct

theorem branchingComm_engine_has_receiveEmpty :
    branchingReductReceiveEmpty ∈ Engine.reduceStep branchingComm 1 := by
  native_decide

theorem branchingComm_engine_has_receiveNested :
    branchingReductReceiveNested ∈ Engine.reduceStep branchingComm 1 := by
  native_decide

theorem branchingComm_engine_reduces_exactly :
    Engine.reduceStep branchingComm 1 =
      [branchingReductReceiveEmpty, branchingReductReceiveNested] := by
  native_decide

theorem branchingComm_engine_reduces_two_successors :
    (Engine.reduceStep branchingComm 1).length = 2 := by
  native_decide

theorem branchingComm_engine_successors_distinct :
    branchingReductReceiveEmpty ≠ branchingReductReceiveNested := by
  native_decide

theorem branchingElems_hasDualRace : hasDualRace branchingElems channel := by
  refine ⟨?_, ?_⟩
  · exact ⟨branchingBody, by simp [branchingElems, branchingNestedSend, branchingBody, channel]⟩
  · refine ⟨payload, branchingPayload, ?_, ?_, ?_⟩
    · simp [branchingElems, branchingNestedSend, branchingPayload, payload, channel]
    · simp [branchingElems, branchingNestedSend, branchingPayload, payload, channel]
    · decide

theorem branchingElems_nodup : branchingElems.Nodup := by
  decide

theorem branchingComm_semantic_nondeterminism :
    ∃ r₁ r₂,
      Nonempty (Reduces branchingComm r₁) ∧
      Nonempty (Reduces branchingComm r₂) ∧
      r₁ ≠ r₂ := by
  simpa [branchingComm, branchingElems] using
    Mettapedia.Languages.ProcessCalculi.RhoCalculus.PresentMoment.dualRace_nondeterminism
      (h_race := branchingElems_hasDualRace)
      (h_nodup := branchingElems_nodup)

theorem branchingComm_presentMoment_contains_receiveEmpty :
    branchingReductReceiveEmpty ∈ Spice.presentMoment branchingComm := by
  exact reduceStep_mem_presentMoment branchingComm_engine_has_receiveEmpty

theorem branchingComm_presentMoment_contains_receiveNested :
    branchingReductReceiveNested ∈ Spice.presentMoment branchingComm := by
  exact reduceStep_mem_presentMoment branchingComm_engine_has_receiveNested

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

theorem costSurface_semantic_subst_body_ignores_payload :
    semanticCommSubst costBodyTerm costPayloadTerm = costBodyTerm := by
  native_decide

theorem costSpentExample_account_is_two :
    rhoSpentSyntaxAccount costSpentExample = rhoCostUnits 2 + rhoTemporalUnits 1 := by
  rw [costSpentExample, rhoSpentSyntaxAccount_sig_mul]
  simp [rhoSignatureSyntaxWidth]

theorem costSurface_continued_contract_records_intrinsic_spent :
    (rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }).spent
      = rhoIntrinsicCommLedger costChannel costPayloadTerm := by
  simpa using
    rhoContinuedCutPresentation_spent
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }

theorem costSurface_continued_contract_spent_shadow :
    rhoLedgerShadow
      ((rhoContinuedCutPresentation.contractWrapped
          costChannel costChannel
          { term := costBodyTerm, grade := zeroCostGrade }
          { term := costPayloadTerm, grade := zeroCostGrade }).spent) =
        rhoIntrinsicCommAccount costChannel costPayloadTerm := by
  simpa using
    rhoContinuedCutPresentation_spent_shadow
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }

theorem costSurface_continued_contract_spent_syntax_eq_shadow :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        ((rhoContinuedCutPresentation.contractWrapped
            costChannel costChannel
            { term := costBodyTerm, grade := zeroCostGrade }
            { term := costPayloadTerm, grade := zeroCostGrade }).spent)) =
        rhoLedgerShadow
          ((rhoContinuedCutPresentation.contractWrapped
              costChannel costChannel
              { term := costBodyTerm, grade := zeroCostGrade }
              { term := costPayloadTerm, grade := zeroCostGrade }).spent) := by
  simpa using
    rhoContinuedCutPresentation_spent_syntax_eq_shadow
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }

theorem costSurface_continued_contract_left_term :
    (rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }).left.term
      = costBodyTerm := by
  have hfst :=
    rhoContinuedCutPresentation.contractWrapped_fst_term
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }
  simpa [costSurface_semantic_subst_body_ignores_payload] using hfst

theorem costSurface_continued_contract_spent_and_left_term :
    let step :=
      rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }
    step.spent = rhoIntrinsicCommLedger costChannel costPayloadTerm ∧
      rhoLedgerShadow step.spent = rhoIntrinsicCommAccount costChannel costPayloadTerm ∧
      step.left.term = costBodyTerm := by
  constructor
  · exact costSurface_continued_contract_records_intrinsic_spent
  · constructor
    · exact costSurface_continued_contract_spent_shadow
    · exact costSurface_continued_contract_left_term

theorem costSurface_continued_contract_preserves_wrapped_structure :
    let step : Mettapedia.GSLT.Meredith.AccountedCutStep Pattern RhoLedger RhoLedger :=
      rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }
    step.left.term = costBodyTerm ∧
      step.right.term = .apply "PZero" [] ∧
      step.left.grade = zeroCostGrade ∧
      step.right.grade = zeroCostGrade ∧
      RhoLedger.WellFormedSpent step.spent := by
  simpa [costSurface_semantic_subst_body_ignores_payload, zeroCostGrade] using
    rhoContinuedCutPresentation_preserves_wrapped_structure
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }

theorem costSurface_continued_contract_preserves_direct_witness :
    let step : Mettapedia.GSLT.Meredith.AccountedCutStep Pattern RhoLedger RhoLedger :=
      rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }
    let direct := RhoDirectCutWitness.ofAccountedStep step
    direct.left.body = costBodyTerm ∧
      direct.left.sig.toSignature = zeroCostGrade.spatial ∧
    direct.right.body = .apply "PZero" [] ∧
      direct.right.sig.toSignature = zeroCostGrade.spatial ∧
      direct.spent.toLedger = step.spent ∧
      direct.spent.depth = step.spent.temporalList.length := by
  have hdirect :=
    rhoContinuedCutPresentation_preserves_direct_witness
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }
  have hwrap := costSurface_continued_contract_preserves_wrapped_structure
  constructor
  · exact hwrap.1
  · constructor
    · calc
        (RhoDirectCutWitness.ofAccountedStep
            (rhoContinuedCutPresentation.contractWrapped
              costChannel costChannel
              { term := costBodyTerm, grade := zeroCostGrade }
              { term := costPayloadTerm, grade := zeroCostGrade })).left.sig.toSignature =
            (rhoContinuedCutPresentation.contractWrapped
              costChannel costChannel
              { term := costBodyTerm, grade := zeroCostGrade }
              { term := costPayloadTerm, grade := zeroCostGrade }).left.grade.spatial := by
                simp [RhoDirectCutWitness.ofAccountedStep]
        _ = zeroCostGrade.spatial := by
              exact congrArg RhoLedger.spatial hwrap.2.2.1
    · constructor
      · exact hwrap.2.1
      · constructor
        · calc
            (RhoDirectCutWitness.ofAccountedStep
                (rhoContinuedCutPresentation.contractWrapped
                  costChannel costChannel
                  { term := costBodyTerm, grade := zeroCostGrade }
                  { term := costPayloadTerm, grade := zeroCostGrade })).right.sig.toSignature =
                (rhoContinuedCutPresentation.contractWrapped
                  costChannel costChannel
                  { term := costBodyTerm, grade := zeroCostGrade }
                  { term := costPayloadTerm, grade := zeroCostGrade }).right.grade.spatial := by
                    simp [RhoDirectCutWitness.ofAccountedStep]
            _ = zeroCostGrade.spatial := by
                  exact congrArg RhoLedger.spatial hwrap.2.2.2.1
        · constructor
          · simpa [costSurface_semantic_subst_body_ignores_payload, zeroCostGrade] using
              hdirect.2.2.2.2.1
          · simpa [zeroCostGrade] using hdirect.2.2.2.2.2

theorem costSurface_continued_no_leak :
    let step : Mettapedia.GSLT.Meredith.AccountedCutStep Pattern RhoLedger RhoLedger :=
      rhoContinuedCutPresentation.contractWrapped
        costChannel costChannel
        { term := costBodyTerm, grade := zeroCostGrade }
        { term := costPayloadTerm, grade := zeroCostGrade }
    step.left.grade + step.right.grade = zeroCostGrade ∧
      step.spent = rhoIntrinsicCommLedger costChannel costPayloadTerm ∧
      RhoLedger.WellFormedSpent step.spent ∧
      rhoLedgerShadow step.spent = rhoIntrinsicCommAccount costChannel costPayloadTerm := by
  simpa [zeroCostGrade] using
    rhoContinuedCutPresentation_balance_no_leak
      costChannel costChannel
      { term := costBodyTerm, grade := zeroCostGrade }
      { term := costPayloadTerm, grade := zeroCostGrade }

theorem costSurface_continued_totalAction_shadow_eq_totalCost_oneStepPath :
    rhoLedgerShadow
      (totalAction rhoIntrinsicLedgerAction
        (continuedCommPath costChannel costPayloadTerm costBodyTerm)) =
        totalCost rhoIntrinsicCostMap
          (continuedCommPath costChannel costPayloadTerm costBodyTerm) := by
  simpa using
    continuedCommTotalAction_shadow_eq_totalCost_oneStepPath
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_continued_totalAction_spentSyntax_eq_totalCost_oneStepPath :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction
          (continuedCommPath costChannel costPayloadTerm costBodyTerm))) =
        totalCost rhoIntrinsicCostMap
          (continuedCommPath costChannel costPayloadTerm costBodyTerm) := by
  simpa using
    continuedCommTotalAction_spentSyntax_eq_totalCost_oneStepPath
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_continued_totalAction_temporalLength_eq_path_length :
    (totalAction rhoIntrinsicLedgerAction
      (continuedCommPath costChannel costPayloadTerm costBodyTerm)).temporalList.length =
        (continuedCommPath costChannel costPayloadTerm costBodyTerm).length := by
  simpa using
    continuedCommTotalAction_temporalLength_eq_length
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_continued_traceAccount_eq_totalCost_oneStepPath :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      (continuedCommTrace costChannel costPayloadTerm costBodyTerm) =
        totalCost rhoIntrinsicCostMap
          (continuedCommPath costChannel costPayloadTerm costBodyTerm) := by
  simpa using
    continuedCommTraceAccount_eq_totalCost_oneStepPath
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_continued_trace_ticks_is_one :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      (continuedCommTrace costChannel costPayloadTerm costBodyTerm) 1 = 1 := by
  simpa using
    continuedCommTraceAccount_ticks
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_continued_trace_ticks_eq_path_length :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      (continuedCommTrace costChannel costPayloadTerm costBodyTerm) 1 =
        (continuedCommPath costChannel costPayloadTerm costBodyTerm).length := by
  simpa using
    continuedCommTraceAccount_ticks_eq_length
      costChannel costPayloadTerm costBodyTerm

theorem costSurface_two_step_traceAccount_eq_totalCost :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      continuedTwoStepTrace =
        totalCost rhoIntrinsicCostMap
          continuedTwoStepPath := by
  simpa using continuedTwoStepTraceAccount_eq_totalCost

theorem costSurface_two_step_trace_ticks_is_two :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      continuedTwoStepTrace 1 = 2 := by
  simpa using continuedTwoStepTraceAccount_ticks

theorem costSurface_two_step_trace_ticks_eq_path_length :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      continuedTwoStepTrace 1 =
        continuedTwoStepPath.length := by
  simpa using continuedTwoStepTraceAccount_ticks_eq_length

theorem costSurface_two_step_totalAction_eq_stepSpentSum :
    totalAction rhoIntrinsicLedgerAction continuedTwoStepPath =
      rhoIntrinsicStepLedger continuedTwoStepFirstStep +
        rhoIntrinsicStepLedger continuedTwoStepSecondStep := by
  simpa using continuedTwoStepTotalAction_eq_stepSpentSum

theorem costSurface_two_step_totalAction_shadow_eq_totalCost :
    rhoLedgerShadow (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath := by
  simpa using continuedTwoStepTotalAction_shadow_eq_totalCost

theorem costSurface_two_step_totalAction_spentSyntax_eq_totalCost :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath := by
  simpa using continuedTwoStepTotalAction_spentSyntax_eq_totalCost

theorem costSurface_two_step_totalAction_publicSpentSyntax_width_eq_spatialCard :
    rhoSpentSyntaxWidth
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath).spatial.card := by
  simpa using continuedTwoStepTotalAction_publicSpentSyntax_width_eq_spatialCard

theorem costSurface_two_step_totalAction_publicSpentSyntax_ticks_eq_path_length :
    rhoSpentSyntaxTicks
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        continuedTwoStepPath.length := by
  simpa using continuedTwoStepTotalAction_publicSpentSyntax_ticks_eq_length

theorem costSurface_two_step_totalAction_publicSpentSyntax_width_eq_totalCost_zero :
    rhoSpentSyntaxWidth
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 := by
  simpa using continuedTwoStepTotalAction_publicSpentSyntax_width_eq_totalCost_zero

theorem costSurface_two_step_totalAction_publicSpentSyntax_ticks_eq_totalCost_one :
    rhoSpentSyntaxTicks
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 := by
  simpa using continuedTwoStepTotalAction_publicSpentSyntax_ticks_eq_totalCost_one

theorem costSurface_two_step_totalAction_publicSpentSyntax_modulus :
    rhoSpentSyntaxWidth
      (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          continuedTwoStepPath.length := by
  simpa using continuedTwoStepTotalAction_publicSpentSyntax_modulus

theorem costSurface_two_step_totalAction_shadow_eq_traceAccount :
    rhoLedgerShadow (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) =
      traceAccount (S := rhoGSLT) (A := Nat) (k := 2) continuedTwoStepTrace := by
  simpa using continuedTwoStepTotalAction_shadow_eq_traceAccount

theorem costSurface_two_step_totalAction_temporalLength_eq_path_length :
    (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath).temporalList.length =
      continuedTwoStepPath.length := by
  simpa using continuedTwoStepTotalAction_temporalLength_eq_length

theorem costSurface_two_step_directSpent_toLedger :
    continuedTwoStepDirectSpent.toLedger =
      totalAction rhoIntrinsicLedgerAction continuedTwoStepPath := by
  simpa using continuedTwoStepDirectSpent_toLedger

theorem costSurface_two_step_directSpent_shadow_eq_totalCost :
    rhoLedgerShadow continuedTwoStepDirectSpent.toLedger =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath := by
  simpa using continuedTwoStepDirectSpent_shadow_eq_totalCost

theorem costSurface_two_step_directSpent_depth_eq_path_length :
    continuedTwoStepDirectSpent.depth = continuedTwoStepPath.length := by
  simpa using continuedTwoStepDirectSpent_depth_eq_length

theorem costSurface_two_step_directSpent_spentSyntax_eq_totalCost :
    rhoSpentSyntaxAccount continuedTwoStepDirectSpent.toPattern =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath := by
  simpa using continuedTwoStepDirectSpent_spentSyntax_eq_totalCost

theorem costSurface_two_step_directSpent_ticks_eq_path_length :
    rhoSpentSyntaxTicks continuedTwoStepDirectSpent.toPattern =
      continuedTwoStepPath.length := by
  simpa using continuedTwoStepDirectSpent_ticks_eq_length

theorem costSurface_two_step_directSpent_eq_append_steps :
    continuedTwoStepDirectSpent =
      RhoDirectStack.append
        (rhoIntrinsicDirectSpentStack
          (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))
        (rhoIntrinsicDirectSpentStack
          (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)) := by
  simpa using continuedTwoStepDirectSpent_eq_append_steps

theorem costSurface_two_step_directSpent_eq_traceSteps :
    continuedTwoStepDirectSpent =
      RhoDirectStack.append
        (rhoIntrinsicDirectStepSpent continuedTwoStepFirstStep)
        (rhoIntrinsicDirectStepSpent continuedTwoStepSecondStep) := by
  simpa using continuedTwoStepDirectSpent_eq_traceSteps

theorem costSurface_two_step_directSpentTrace_eq_traceSteps :
    rhoIntrinsicDirectSpentTrace continuedTwoStepPath =
      RhoDirectStack.append
        (rhoIntrinsicDirectStepSpent continuedTwoStepFirstStep)
        (rhoIntrinsicDirectStepSpent continuedTwoStepSecondStep) := by
  simpa using continuedTwoStepDirectSpentTrace_eq_traceSteps

theorem costSurface_two_step_directSpentTrace_account_eq_publicSpentSyntax :
    rhoSpentSyntaxAccount
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        rhoSpentSyntaxAccount
          (rhoLedgerToSpentSyntax
            (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) := by
  simpa using continuedTwoStepDirectSpentTrace_account_eq_publicSpentSyntax

theorem costSurface_two_step_directSpentTrace_width_eq_totalCost_zero :
    rhoSpentSyntaxWidth (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 := by
  simpa using continuedTwoStepDirectSpentTrace_width_eq_totalCost_zero

theorem costSurface_two_step_directSpentTrace_ticks_eq_totalCost_one :
    rhoSpentSyntaxTicks (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 := by
  simpa using continuedTwoStepDirectSpentTrace_ticks_eq_totalCost_one

theorem costSurface_two_step_directSpentTrace_modulus :
    rhoSpentSyntaxWidth (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
      totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoSpentSyntaxTicks (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        continuedTwoStepPath.length := by
  simpa using continuedTwoStepDirectSpentTrace_modulus

theorem costSurface_two_step_publicSpentSyntax_no_leak_append :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction
                (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction
                (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) := by
  simpa using continuedTwoStepPublicSpentSyntax_no_leak_append

theorem costSurface_two_step_directSpentTrace_no_leak_append :
    rhoSpentSyntaxAccount
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        rhoSpentSyntaxAccount
          (rhoIntrinsicDirectSpentTrace
            (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
        rhoSpentSyntaxAccount
          (rhoIntrinsicDirectSpentTrace
            (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger) := by
  simpa using continuedTwoStepDirectSpentTrace_no_leak_append

theorem costSurface_two_step_publicSpentSyntax_semantics :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            continuedTwoStepPath.length ∧
      RhoLedger.TraceCoherent
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) := by
  simpa using continuedTwoStepPublicSpentSyntax_semantics

theorem costSurface_two_step_directSpentTrace_semantics :
    (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).SurfaceLike ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger =
        totalAction rhoIntrinsicLedgerAction continuedTwoStepPath ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPublicPattern =
        rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxWidth
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxTicks
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          continuedTwoStepPath.length := by
  simpa using continuedTwoStepDirectSpentTrace_semantics

theorem costSurface_two_step_directSpentTrace_toPublicPattern_eq_publicSpentSyntax :
    (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPublicPattern =
      rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) := by
  simpa using continuedTwoStepDirectSpentTrace_toPublicPattern_eq_publicSpentSyntax

theorem costSurface_two_step_publicSpentSyntax_modulus_reducesN :
    rhoSpentSyntaxWidth
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction
          (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) 0 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
              totalCost rhoIntrinsicCostMap
                (rhoRewritePathOfReducesN continuedTwoStepReducesN) 1 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
              2 := by
  simpa using continuedTwoStepPublicSpentSyntax_modulus_reducesN

theorem costSurface_two_step_directSpentTrace_modulus_reducesN :
    rhoSpentSyntaxWidth
      (rhoIntrinsicDirectSpentTrace
        (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
          totalCost rhoIntrinsicCostMap
            (rhoRewritePathOfReducesN continuedTwoStepReducesN) 0 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) 1 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            2 := by
  simpa using continuedTwoStepDirectSpentTrace_modulus_reducesN

theorem costSurface_two_step_first_reducesN_path_eq :
    rhoRewritePathOfReducesN continuedTwoStepFirstReducesN =
      oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep := by
  simpa using continuedTwoStepFirstReducesN_path_eq

theorem costSurface_two_step_second_reducesN_path_eq :
    rhoRewritePathOfReducesN continuedTwoStepSecondReducesN =
      oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep := by
  simpa using continuedTwoStepSecondReducesN_path_eq

theorem costSurface_two_step_publicSpentSyntax_semantics_reducesN :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction
          (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
              totalCost rhoIntrinsicCostMap
                (rhoRewritePathOfReducesN continuedTwoStepReducesN) 0 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
              totalCost rhoIntrinsicCostMap
                (rhoRewritePathOfReducesN continuedTwoStepReducesN) 1 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN))) =
              2 ∧
      RhoLedger.TraceCoherent
        (totalAction rhoIntrinsicLedgerAction
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)) := by
  simpa using continuedTwoStepPublicSpentSyntax_semantics_reducesN

theorem costSurface_two_step_directSpentTrace_semantics_reducesN :
    (rhoIntrinsicDirectSpentTrace
      (rhoRewritePathOfReducesN continuedTwoStepReducesN)).SurfaceLike ∧
      (rhoIntrinsicDirectSpentTrace
        (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toLedger =
          totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN continuedTwoStepReducesN) ∧
      (rhoIntrinsicDirectSpentTrace
        (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPublicPattern =
          rhoLedgerToSpentSyntax
            (totalAction rhoIntrinsicLedgerAction
              (rhoRewritePathOfReducesN continuedTwoStepReducesN)) ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toLedger) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            rhoSpentSyntaxAccount
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (rhoRewritePathOfReducesN continuedTwoStepReducesN))) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (rhoRewritePathOfReducesN continuedTwoStepReducesN))) ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) 0 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (rhoRewritePathOfReducesN continuedTwoStepReducesN))) ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            totalCost rhoIntrinsicCostMap
              (rhoRewritePathOfReducesN continuedTwoStepReducesN) 1 ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN continuedTwoStepReducesN)).toPattern =
            2 := by
  simpa using continuedTwoStepDirectSpentTrace_semantics_reducesN

theorem costSurface_two_step_publicSpentSyntax_no_leak_reducesN_concat :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
              rhoSpentSyntaxAccount
                (rhoLedgerToSpentSyntax
                  (totalAction rhoIntrinsicLedgerAction
                    (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
              rhoSpentSyntaxAccount
                (rhoLedgerToSpentSyntax
                  (totalAction rhoIntrinsicLedgerAction
                    (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN
              (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
                rhoSpentSyntaxWidth
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
                rhoSpentSyntaxWidth
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN
              (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
                rhoSpentSyntaxTicks
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
                rhoSpentSyntaxTicks
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) := by
  simpa using continuedTwoStepPublicSpentSyntax_no_leak_reducesN_concat

theorem costSurface_two_step_directSpentTrace_no_leak_reducesN_concat :
    rhoSpentSyntaxAccount
      (rhoIntrinsicDirectSpentTrace
        (rhoRewritePathOfReducesN
          (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
            rhoSpentSyntaxAccount
              (rhoIntrinsicDirectSpentTrace
                (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
            rhoSpentSyntaxAccount
              (rhoIntrinsicDirectSpentTrace
                (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
              rhoSpentSyntaxWidth
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
              rhoSpentSyntaxWidth
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
              rhoSpentSyntaxTicks
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
              rhoSpentSyntaxTicks
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toLedger) := by
  simpa using continuedTwoStepDirectSpentTrace_no_leak_reducesN_concat

theorem costSurface_two_step_publicSpentSyntax_semantics_reducesN_concat :
    rhoSpentSyntaxAccount
      (rhoLedgerToSpentSyntax
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction
                (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction
                (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            2 ∧
      RhoLedger.TraceCoherent
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) := by
  simpa using continuedTwoStepPublicSpentSyntax_semantics_reducesN_concat

theorem costSurface_two_step_directSpentTrace_semantics_reducesN_concat :
    (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).SurfaceLike ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger =
        totalAction rhoIntrinsicLedgerAction continuedTwoStepPath ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPublicPattern =
        rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxAccount
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxAccount
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          2 := by
  simpa using continuedTwoStepDirectSpentTrace_semantics_reducesN_concat

theorem costSurface_two_step_semantic_bridge :
    traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
      continuedTwoStepTrace =
        totalCost rhoIntrinsicCostMap
          continuedTwoStepPath ∧
      rhoLedgerShadow (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      rhoSpentSyntaxAccount
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath).spatial.card ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          continuedTwoStepPath.length ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
          totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoLedgerShadow (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) =
        traceAccount (S := rhoGSLT) (A := Nat) (k := 2) continuedTwoStepTrace ∧
      continuedTwoStepDirectSpent.toLedger =
        totalAction rhoIntrinsicLedgerAction continuedTwoStepPath ∧
      rhoLedgerShadow continuedTwoStepDirectSpent.toLedger =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      continuedTwoStepDirectSpent.depth = continuedTwoStepPath.length ∧
      rhoSpentSyntaxAccount continuedTwoStepDirectSpent.toPattern =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath ∧
      rhoSpentSyntaxTicks continuedTwoStepDirectSpent.toPattern =
        continuedTwoStepPath.length ∧
      continuedTwoStepDirectSpent =
        RhoDirectStack.append
          (rhoIntrinsicDirectSpentStack
            (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))
          (rhoIntrinsicDirectSpentStack
            (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)) ∧
      continuedTwoStepDirectSpent =
        RhoDirectStack.append
          (rhoIntrinsicDirectStepSpent continuedTwoStepFirstStep)
          (rhoIntrinsicDirectStepSpent continuedTwoStepSecondStep) ∧
      rhoIntrinsicDirectSpentTrace continuedTwoStepPath =
        RhoDirectStack.append
          (rhoIntrinsicDirectStepSpent continuedTwoStepFirstStep)
          (rhoIntrinsicDirectStepSpent continuedTwoStepSecondStep) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxAccount
            (rhoLedgerToSpentSyntax
              (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) ∧
      rhoSpentSyntaxWidth (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 0 ∧
      rhoSpentSyntaxTicks (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
        totalCost rhoIntrinsicCostMap continuedTwoStepPath 1 ∧
      rhoSpentSyntaxAccount
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN
              (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
                rhoSpentSyntaxAccount
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
                rhoSpentSyntaxAccount
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN
              (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
                rhoSpentSyntaxWidth
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
                rhoSpentSyntaxWidth
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction
            (rhoRewritePathOfReducesN
              (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN)))) =
                rhoSpentSyntaxTicks
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
                rhoSpentSyntaxTicks
                  (rhoLedgerToSpentSyntax
                    (totalAction rhoIntrinsicLedgerAction
                      (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
              rhoSpentSyntaxAccount
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
              rhoSpentSyntaxAccount
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
              rhoSpentSyntaxWidth
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
              rhoSpentSyntaxWidth
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toPattern =
              rhoSpentSyntaxTicks
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
              rhoSpentSyntaxTicks
                (rhoIntrinsicDirectSpentTrace
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace
          (rhoRewritePathOfReducesN
            (reducesN_concat continuedTwoStepFirstReducesN continuedTwoStepSecondReducesN))).toLedger) ∧
      rhoSpentSyntaxAccount
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxAccount
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxAccount
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxWidth
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxWidth
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep))) +
            rhoSpentSyntaxTicks
              (rhoLedgerToSpentSyntax
                (totalAction rhoIntrinsicLedgerAction
                  (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep))) ∧
      rhoSpentSyntaxTicks
        (rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath)) =
            2 ∧
      RhoLedger.TraceCoherent
        (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).SurfaceLike ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger =
        totalAction rhoIntrinsicLedgerAction continuedTwoStepPath ∧
      (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPublicPattern =
        rhoLedgerToSpentSyntax
          (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath) ∧
      RhoLedger.TraceCoherent
        ((rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toLedger) ∧
      rhoSpentSyntaxAccount
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxAccount
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxAccount
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxWidth
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxWidth
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepFirstStep)).toPattern +
          rhoSpentSyntaxTicks
            (rhoIntrinsicDirectSpentTrace
              (oneStepPath (S := rhoGSLT) continuedTwoStepSecondStep)).toPattern ∧
      rhoSpentSyntaxTicks
        (rhoIntrinsicDirectSpentTrace continuedTwoStepPath).toPattern =
          2 ∧
      (totalAction rhoIntrinsicLedgerAction continuedTwoStepPath).temporalList.length =
        continuedTwoStepPath.length ∧
      traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
        continuedTwoStepTrace 1 = 2 ∧
      traceAccount (S := rhoGSLT) (A := Nat) (k := 2)
        continuedTwoStepTrace 1 =
          continuedTwoStepPath.length := by
  simpa using continuedTwoStepSemanticBridge

theorem commInput_continued_contract_transport_to_representative :
    ProcResidualEquiv
      (rhoContinuedCutPresentation.contractWrapped
          channel channel
          { term := body, grade := zeroCostGrade }
          { term := payload, grade := zeroCostGrade }).left.term
      (semanticCommRepresentative body payload) := by
  simpa [body, payload] using
    rhoContinuedCutPresentation_contract_fst_transport_to_representative
      channel channel
      { term := body, grade := zeroCostGrade }
      { term := payload, grade := zeroCostGrade }

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
