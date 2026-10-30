import IcTheory.Compression.Definition31
import Mettapedia.Computability.KolmogorovComplexity.FeatureBPredicates
import Mettapedia.Computability.KolmogorovComplexity.NaturalWordEncoding

/-!
# The original IC interpreter on the shared program model

This client is checked by the original development's own Lean toolchain.
The shared model and predicates are compiled from the same sources used by
Mettapedia. Every program string participates, including malformed programs;
no replacement parser or finite sample defines the correspondence.
-/

set_option autoImplicit false

namespace ICOriginalInterpreterBridge

open IcTheory KolmogorovComplexity
open scoped Classical

def codec : NaturalWordCodec where
  encode := BitString.toNatExact
  decode := BitString.ofNatExact
  decode_encode := BitString.ofNatExact_toNatExact
  encode_decode := BitString.toNatExact_ofNatExact
  encode_computable := BitString.toNatExact_computable
  decode_computable := BitString.ofNatExact_computable

theorem exact_encoder_correspondence (word : BitString) :
    NaturalWordEncoding.encode word = BitString.toNatExact word := by
  induction word with
  | nil => rfl
  | cons bit word ih => cases bit <;> simp [NaturalWordEncoding.encode, BitString.toNatExact, ih]

theorem exact_decoder_correspondence (value : Nat) :
    NaturalWordEncoding.decode value = BitString.ofNatExact value := by
  apply BitString.toNatExact_injective
  rw [← exact_encoder_correspondence, NaturalWordEncoding.encode_decode, BitString.toNatExact_ofNatExact]

noncomputable def original : ConditionalAlgorithm :=
  codec.interpreter UniversalMachine.programToCode

theorem execution_correspondence (program condition output : BitString) :
    output ∈ original program condition ↔ UniversalMachine.runs program condition output := by
  exact codec.interpreter_output_iff UniversalMachine.programToCode program condition output

theorem divergence_correspondence (program condition : BitString) :
    original program condition = Part.none ↔ ¬ ∃ output, UniversalMachine.runs program condition output := by
  rw [Part.eq_none_iff]
  simp only [execution_correspondence, not_exists]

theorem termination_correspondence (program condition : BitString) :
    (original program condition).Dom ↔ ∃ output, UniversalMachine.runs program condition output := by
  rw [Part.dom_iff_mem]
  simp only [execution_correspondence]

theorem original_effective : Partrec₂ original :=
  codec.interpreter_partrec UniversalMachine.programToCode_computable

def compilerLayout (code : Nat.Partrec.Code) : BitString :=
  UniversalMachine.postcomposeSentinel ++
    BitString.exactLengthCode (UniversalMachine.codeToProgram code) ++
      UniversalMachine.codeToProgram code ++ UniversalMachine.packInputWithPayloadSentinel

theorem compiler_layout (code : Nat.Partrec.Code) (payload condition : BitString) :
    Nat.Partrec.Code.eval (UniversalMachine.programToCode (compilerLayout code ++ payload)) (codec.encode condition) =
      Nat.Partrec.Code.eval code (Nat.pair (codec.encode condition) (codec.encode payload)) := by
  have packet : compilerLayout code ++ payload = UniversalMachine.postcomposePackedProgram
      (UniversalMachine.codeToProgram code) (UniversalMachine.packInputWithPayloadPackedProgram payload) := by
    simp only [compilerLayout, UniversalMachine.postcomposePackedProgram,
      UniversalMachine.packInputWithPayloadPackedProgram, BitString.exactPairPayload, List.append_assoc]
  rw [packet, UniversalMachine.programToCode_postcomposePackedProgram,
    UniversalMachine.programToCode_codeToProgram,
    UniversalMachine.programToCode_packInputWithPayloadPackedProgram]
  simp [Nat.Partrec.Code.eval, Nat.Partrec.Code.eval_id, Nat.Partrec.Code.eval_const, Seq.seq, codec]

theorem compiler_layout_length (code : Nat.Partrec.Code) :
    (compilerLayout code).length =
      (UniversalMachine.codeToProgram code).length +
        2 * (BitString.ofNatExact (UniversalMachine.codeToProgram code).length).length + 9 := by
  simp only [compilerLayout, List.length_append]
  have header := BitString.blen_exactLengthCode (UniversalMachine.codeToProgram code)
  simp only [BitString.blen] at header
  simp only [UniversalMachine.postcomposeSentinel, UniversalMachine.packInputWithPayloadSentinel,
    List.length_cons, List.length_nil] at *
  omega

/-- The original machine satisfies the shared constant-prefix notion of
universality. This is derived from its concrete wire layout and execution. -/
theorem original_universal : IsUniversalConditional original :=
  codec.interpreter_isUniversal UniversalMachine.programToCode compilerLayout compiler_layout

theorem represented_lengths_correspond (output condition : BitString) :
    conditionalPlainLengthsFor original condition output = UniversalMachine.DescriptionLengths output condition := by
  ext length
  simp only [conditionalPlainLengthsFor, UniversalMachine.DescriptionLengths,
    execution_correspondence, BitString.blen, and_comm]

theorem plain_complexity_correspondence (output condition : BitString) :
    plainConditionalComplexity original output condition = UniversalMachine.ConditionalComplexity output condition := by
  unfold plainConditionalComplexity UniversalMachine.ConditionalComplexity
  rw [represented_lengths_correspond]

theorem descriptive_map_correspondence (feature descriptive source : BitString) :
    FeatureTheory.DescriptiveMap original feature descriptive source ↔
      Compression.IsDescriptiveMap UniversalMachine.runs feature descriptive source := by
  simp only [FeatureTheory.DescriptiveMap, Compression.IsDescriptiveMap,
    execution_correspondence, Compression.CompressionCondition, BitString.blen]

theorem feature_correspondence (feature source : BitString) :
    FeatureTheory.Feature original feature source ↔ Compression.IsFeature UniversalMachine.runs feature source := by
  simp only [FeatureTheory.Feature, Compression.IsFeature, descriptive_map_correspondence]

theorem shortest_feature_correspondence (feature source : BitString) :
    FeatureTheory.ShortestFeature original feature source ↔
      Compression.IsShortestFeature UniversalMachine.runs feature source := by
  simp only [FeatureTheory.ShortestFeature, Compression.IsShortestFeature,
    feature_correspondence, BitString.blen]

theorem shortest_map_correspondence (feature descriptive source : BitString) :
    FeatureTheory.ShortestDescriptiveMap original feature descriptive source ↔
      Compression.IsShortestDescriptiveMap UniversalMachine.runs feature descriptive source := by
  simp only [FeatureTheory.ShortestDescriptiveMap, Compression.IsShortestDescriptiveMap,
    descriptive_map_correspondence, BitString.blen]

theorem natural_ratio_correspondence (factor : Nat) (positive : 0 < factor) (residual source : BitString) :
    (factor : ℝ) * residual.length ≤ source.length ↔ Compression.ResidualBoundByFactor factor residual source := by
  unfold Compression.ResidualBoundByFactor
  simp only [BitString.blen]
  rw [Nat.le_div_iff_mul_le positive]
  constructor
  · intro bounded
    have natural : factor * residual.length ≤ source.length := by exact_mod_cast bounded
    simpa only [Nat.mul_comm] using natural
  · intro bounded
    have natural : factor * residual.length ≤ source.length := by simpa only [Nat.mul_comm] using bounded
    exact_mod_cast natural

theorem b_descriptive_map_correspondence (factor : Nat) (positive : 0 < factor)
    (feature descriptive source : BitString) :
    FeatureTheory.BDescriptiveMap original factor feature descriptive source ↔
      Compression.IsBDescriptiveMap UniversalMachine.runs factor feature descriptive source := by
  simp only [FeatureTheory.BDescriptiveMap, Compression.IsBDescriptiveMap, execution_correspondence,
    Compression.CompressionCondition, BitString.blen, natural_ratio_correspondence factor positive]

theorem b_feature_correspondence (factor : Nat) (positive : 0 < factor) (feature source : BitString) :
    FeatureTheory.BFeature original factor feature source ↔
      Compression.IsBFeature UniversalMachine.runs factor feature source := by
  simp only [FeatureTheory.BFeature, Compression.IsBFeature, b_descriptive_map_correspondence factor positive]

theorem shortest_b_feature_correspondence (factor : Nat) (positive : 0 < factor) (feature source : BitString) :
    FeatureTheory.ShortestBFeature original factor feature source ↔
      Compression.IsShortestBFeature UniversalMachine.runs factor feature source := by
  simp only [FeatureTheory.ShortestBFeature, Compression.IsShortestBFeature,
    b_feature_correspondence factor positive, BitString.blen]

theorem shortest_b_map_correspondence (factor : Nat) (positive : 0 < factor)
    (feature descriptive source : BitString) :
    FeatureTheory.ShortestBDescriptiveMap original factor feature descriptive source ↔
      Compression.IsShortestBDescriptiveMap UniversalMachine.runs factor feature descriptive source := by
  simp only [FeatureTheory.ShortestBDescriptiveMap, Compression.IsShortestBDescriptiveMap,
    b_descriptive_map_correspondence factor positive, BitString.blen]

theorem zero_factor_is_not_the_same_observation :
    (0 : ℝ) * ([true] : BitString).length ≤ ([] : BitString).length ∧
      ¬ Compression.ResidualBoundByFactor 0 [true] [] := by
  norm_num [Compression.ResidualBoundByFactor, BitString.blen]

theorem malformed_empty_program_keeps_its_zero_fallback : original [] [true] = Part.some [] := by
  unfold original NaturalWordCodec.interpreter
  have zero : UniversalMachine.programToCode [] = Nat.Partrec.Code.zero := by
    rw [UniversalMachine.programToCode]
    · unfold UniversalMachine.additiveProgramToCode UniversalMachine.decodeAdditiveProgramNat
      simp only [BitString.toNatExact]
      with_unfolding_all rfl
    all_goals simp
  rw [zero]
  change Part.map BitString.ofNatExact (Part.some 0) = Part.some []
  rw [Part.map_some]
  exact congrArg Part.some (by simp [BitString.ofNatExact])

theorem stored_values_reconstruct (payload condition : BitString) :
    payload ∈ original (UniversalMachine.storedValuePackedProgram payload) condition := by
  rw [execution_correspondence, UniversalMachine.runs_storedValuePackedProgram_iff]

/-- The original stored-prefix reader discards its length header but does
not truncate the residual to the declared payload length. -/
theorem permissive_stored_reader (payload suffix : BitString) :
    UniversalMachine.decodeE2Payload (BitString.e2 payload ++ suffix) = payload ++ suffix := by
  change (UniversalMachine.decodePairPayload
    ((BitString.e1 (BitString.ofNat (BitString.blen payload)) ++ payload) ++ suffix)).2 = payload ++ suffix
  rw [List.append_assoc]
  change (UniversalMachine.decodePairPayload
    (BitString.pair (BitString.ofNat (BitString.blen payload)) (payload ++ suffix))).2 = payload ++ suffix
  rw [UniversalMachine.decodePairPayload_pair]

end ICOriginalInterpreterBridge

open Lean Elab Command

run_cmd do
  let env ← getEnv
  let allowed := #[``propext, ``Quot.sound, ``Classical.choice]
  let mut count := 0
  let mut names : Array Json := #[]
  for (name, info) in env.constants.toList do
    unless (`ICOriginalInterpreterBridge).isPrefixOf name do continue
    if info.isUnsafe || info.isPartial then throwError "nonlogical bridge declaration {name}"
    if info matches .axiomInfo _ then throwError "new bridge axiom {name}"
    let dependencies ← liftCoreM <| collectAxioms name
    for dependency in dependencies do
      unless allowed.contains dependency do throwError "unpermitted bridge dependency {name}: {dependency}"
    count := count + 1
    names := names.push (Json.mkObj [("name", toJson name.toString),
      ("axioms", toJson (dependencies.map Name.toString))])
  liftIO <| IO.println (Json.mkObj [("originalInterpreterBridgeDeclarations", toJson count),
    ("auditedDeclarations", toJson names)]).compress
