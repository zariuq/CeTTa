import tests.support.lean.ic_original_interpreter_bridge
import IcTheory.Computability.PrefixComplexity
import Mettapedia.Computability.KolmogorovComplexity.FramedProgramModel

/-!
# The original prefix-coded execution relation as a shared framed program

The original E1/E2 encoders are retained. A complete canonical-frame check
implements precisely the original existential PrefixRuns domain, including
its refusal of nonempty extensions. Its minimum lengths remain distinct from
those of a constant-overhead prefix reference machine.
-/

set_option autoImplicit false

namespace ICOriginalPrefixBridge

open IcTheory KolmogorovComplexity ICOriginalInterpreterBridge
open scoped Classical

def binaryValue (word : BitString) : Nat :=
  word.foldr (fun bit value => cond bit (2 * value + 1) (2 * value)) 0

theorem binaryValue_primrec : Primrec binaryValue := by
  have step : Primrec₂ fun (_word : BitString) (context : Bool × Nat) =>
      cond context.1 (2 * context.2 + 1) (2 * context.2) :=
    Primrec.cond (Primrec.fst.comp Primrec.snd)
      (Primrec.nat_add.comp
        (Primrec.nat_mul.comp (Primrec.const 2) (Primrec.snd.comp Primrec.snd)) (Primrec.const 1))
      (Primrec.nat_mul.comp (Primrec.const 2) (Primrec.snd.comp Primrec.snd))
  exact Primrec.list_foldr Primrec.id (Primrec.const 0) step

theorem binaryValue_encodePosNum (number : PosNum) :
    binaryValue (Computability.encodePosNum number) = (number : Nat) := by
  induction number with
  | one => rfl
  | bit0 number ih =>
      change 2 * binaryValue (Computability.encodePosNum number) = ((number.bit0 : PosNum) : Nat)
      rw [ih, PosNum.cast_bit0]
      omega
  | bit1 number ih =>
      change 2 * binaryValue (Computability.encodePosNum number) + 1 = ((number.bit1 : PosNum) : Nat)
      rw [ih, PosNum.cast_bit1]
      omega

theorem binaryValue_encodeNat (number : Nat) : binaryValue (Computability.encodeNat number) = number := by
  have encoded : ∀ number : Num, binaryValue (Computability.encodeNum number) = (number : Nat) := by
    intro number
    cases number with
    | zero => rfl
    | pos number => exact binaryValue_encodePosNum number
  exact (encoded (number : Num)).trans (Num.to_of_nat number)

theorem encodePosNum_bits (number : PosNum) : Computability.encodePosNum number = Nat.bits (number : Nat) := by
  induction number with
  | one => exact Nat.one_bits.symm
  | bit0 number ih =>
      rw [Computability.encodePosNum, PosNum.cast_bit0, ← two_mul,
        Nat.bit0_bits _ (Nat.ne_of_gt (PosNum.to_nat_pos number)), ih]
  | bit1 number ih =>
      rw [Computability.encodePosNum, PosNum.cast_bit1, ← two_mul, Nat.bit1_bits, ih]

theorem encodeNat_bits (number : Nat) : Computability.encodeNat number = Nat.bits number := by
  have encoded : ∀ number : Num, Computability.encodeNum number = Nat.bits (number : Nat) := by
    intro number
    cases number with
    | zero => exact Nat.zero_bits.symm
    | pos number => exact encodePosNum_bits number
  exact (encoded (number : Num)).trans (congrArg Nat.bits (Num.to_of_nat number))

theorem bits_cons_of_nonzero (number : Nat) (nonzero : number ≠ 0) :
    Nat.bits number = number.bodd :: Nat.bits number.div2 := by
  have leading : number.div2 = 0 → number.bodd = true := by
    intro half
    cases bit : number.bodd with
    | true => rfl
    | false =>
        have value := Nat.bit_bodd_div2 number
        simp only [bit, half, Nat.bit_val, mul_zero, zero_add] at value
        exact False.elim (nonzero value.symm)
  simpa only [Nat.bit_bodd_div2] using Nat.bits_append_bit number.div2 number.bodd leading

def bitTableStep (_unit : Unit) (previous : List BitString) : Option BitString :=
  if previous.length = 0 then some []
  else (previous[previous.length.div2]?).map fun word => previous.length.bodd :: word

theorem bitTableStep_primrec : Primrec₂ bitTableStep := by
  have count : Primrec fun context : Unit × List BitString => context.2.length :=
    Primrec.list_length.comp Primrec.snd
  have lookup : Primrec fun context : Unit × List BitString => context.2[context.2.length.div2]? :=
    Primrec.list_getElem?.comp Primrec.snd (Primrec.nat_div2.comp count)
  have prepend : Primrec₂ fun (context : Unit × List BitString) (word : BitString) => context.2.length.bodd :: word :=
    (Primrec.list_cons.comp (Primrec.nat_bodd.comp (count.comp Primrec.fst)) Primrec.snd).to₂
  exact (Primrec.ite (Primrec.eq.comp count (Primrec.const 0)) (Primrec.const (some []))
    (Primrec.option_map lookup prepend)).to₂

theorem nat_bits_primrec : Primrec Nat.bits := by
  have recursive : Primrec₂ fun (_unit : Unit) number => Nat.bits number := by
    apply Primrec.nat_strong_rec (fun (_unit : Unit) number => Nat.bits number) bitTableStep_primrec
    intro token number
    cases number with
    | zero => rfl
    | succ number =>
        have smaller : (number + 1).div2 < number + 1 := by
          rw [Nat.div2_val]
          exact Nat.div_lt_self (by omega) (by decide)
        have lookup : ((List.range (number + 1)).map Nat.bits)[(number + 1).div2]? =
            some (Nat.bits (number + 1).div2) := by
          rw [List.getElem?_map, List.getElem?_range smaller]
          rfl
        have shape := bits_cons_of_nonzero (number + 1) (Nat.succ_ne_zero number)
        simp only [bitTableStep, List.length_map, List.length_range, Nat.add_one_ne_zero,
          ↓reduceIte, lookup, Option.map_some, shape]
  exact recursive.comp (Primrec.const Unit.unit) Primrec.id

theorem encodeNat_primrec : Primrec Computability.encodeNat :=
  nat_bits_primrec.of_eq fun number => (encodeNat_bits number).symm

theorem unary_replicate (count : Nat) : Computability.unaryEncodeNat count = List.replicate count true := by
  induction count with
  | zero => rfl
  | succ count ih => simp only [Computability.unaryEncodeNat, List.replicate_succ, ih]

theorem e1_primrec : Primrec BitString.e1 := by
  have markers : Primrec fun word : BitString => word.map fun _ => true :=
    Primrec.list_map Primrec.id (Primrec.const true).to₂
  exact (Primrec.list_append.comp
    (Primrec.list_append.comp markers (Primrec.const [false])) Primrec.id).of_eq fun word => by
      simp [BitString.e1, BitString.unaryHeader, BitString.unary, unary_replicate]

theorem e2_primrec : Primrec BitString.e2 :=
  Primrec.list_append.comp
    (e1_primrec.comp (encodeNat_primrec.comp Primrec.list_length)) Primrec.id

theorem decodePairPayload_primrec : Primrec UniversalMachine.decodePairPayload := by
  have rest := Primrec.snd.comp (BitString.splitAt_primrec.comp
    (Primrec.nat_add.comp BitString.countLeadingTrue_primrec (Primrec.const 1)) Primrec.id)
  exact BitString.splitAt_primrec.comp BitString.countLeadingTrue_primrec rest

def payloadRead (input : BitString) : Option (BitString × BitString) :=
  let fields := UniversalMachine.decodePairPayload input
  some (BitString.splitAt (binaryValue fields.1) fields.2)

theorem payloadRead_primrec : Primrec payloadRead :=
  Primrec.option_some.comp (BitString.splitAt_primrec.comp
    (binaryValue_primrec.comp (Primrec.fst.comp decodePairPayload_primrec))
    (Primrec.snd.comp decodePairPayload_primrec))

def headCode : ContextualWordCode where
  put := BitString.e1
  read := fun input => some (UniversalMachine.decodePairPayload input)
  read_put_append := fun word suffix => by
    change some (UniversalMachine.decodePairPayload (BitString.pair word suffix)) = some (word, suffix)
    rw [UniversalMachine.decodePairPayload_pair]
  put_primrec := e1_primrec
  read_primrec := Primrec.option_some.comp decodePairPayload_primrec

def payloadCode : ContextualWordCode where
  put := BitString.e2
  read := payloadRead
  read_put_append := fun word suffix => by
    unfold payloadRead
    change some (BitString.splitAt
      (binaryValue (UniversalMachine.decodePairPayload
        ((BitString.e1 (Computability.encodeNat word.length) ++ word) ++ suffix)).1)
      (UniversalMachine.decodePairPayload
        ((BitString.e1 (Computability.encodeNat word.length) ++ word) ++ suffix)).2) = some (word, suffix)
    rw [List.append_assoc]
    change some (BitString.splitAt
      (binaryValue (UniversalMachine.decodePairPayload
        (BitString.pair (BitString.ofNat word.length) (word ++ suffix))).1)
      (UniversalMachine.decodePairPayload
        (BitString.pair (BitString.ofNat word.length) (word ++ suffix))).2) = some (word, suffix)
    rw [UniversalMachine.decodePairPayload_pair]
    change some (BitString.splitAt (binaryValue (BitString.ofNat word.length)) (word ++ suffix)) = some (word, suffix)
    rw [binaryValue_encodeNat, BitString.splitAt_eq_take_drop]
    simp
  put_primrec := e2_primrec
  read_primrec := payloadRead_primrec

def frame : PairedWordCode := ⟨headCode, payloadCode⟩

noncomputable def framed : ConditionalAlgorithm := frame.execute original codec.conditionPairing.pack

theorem prefix_execution_correspondence (program condition output : BitString) :
    output ∈ framed program condition ↔ UniversalMachine.PrefixRuns program condition output := by
  unfold framed
  rw [frame.execute_iff]
  change (∃ head payload, program = BitString.pair head (BitString.e2 payload) ∧
    output ∈ original head (UniversalMachine.packedInput condition payload)) ↔ _
  simp only [UniversalMachine.PrefixRuns, execution_correspondence]

theorem framed_effective : Partrec₂ framed :=
  frame.execute_partrec original_effective codec.conditionPairing.pack_computable

theorem original_prefix_represents_all_outputs (condition output : BitString) :
    ∃ program, output ∈ framed program condition := by
  obtain ⟨length, program, _width, computes⟩ := UniversalMachine.prefixDescriptionLengths_nonempty output condition
  exact ⟨program, (prefix_execution_correspondence program condition output).mpr computes⟩

theorem incomplete_original_frames_refused : frame.read [] = none ∧ frame.read [false] = none := by decide

theorem noncanonical_original_length_refused :
    frame.read (BitString.e1 [] ++ BitString.e1 [true, false] ++ [true]) = none := by decide

theorem prefix_length_sets_correspond (output condition : BitString) :
    conditionalPlainLengthsFor framed condition output = UniversalMachine.PrefixDescriptionLengths output condition := by
  ext length
  simp only [conditionalPlainLengthsFor, UniversalMachine.PrefixDescriptionLengths,
    prefix_execution_correspondence, BitString.blen, and_comm]

theorem prefix_complexity_correspondence (output condition : BitString) :
    plainConditionalComplexity framed output condition = UniversalMachine.PrefixConditionalComplexity output condition := by
  unfold plainConditionalComplexity UniversalMachine.PrefixConditionalComplexity
  rw [prefix_length_sets_correspond]

theorem original_prefix_domain (condition first second : BitString) (pref : first <+: second)
    (different : first ≠ second) (terminates : ∃ output, UniversalMachine.PrefixRuns first condition output) :
    ¬ ∃ output, UniversalMachine.PrefixRuns second condition output := by
  obtain ⟨output, computes⟩ := terminates
  have observed := (prefix_execution_correspondence first condition output).mpr computes
  have absent := frame.execute_prefix_none original codec.conditionPairing.pack pref different
    (Part.dom_iff_mem.mpr ⟨output, observed⟩)
  rintro ⟨later, laterRun⟩
  have laterObserved := (prefix_execution_correspondence second condition later).mpr laterRun
  change later ∈ frame.execute original codec.conditionPairing.pack second condition at laterObserved
  rw [absent] at laterObserved
  exact Part.notMem_none later laterObserved

theorem original_payload_length_bound (payload : BitString) :
    (frame.payload.put payload).length ≤ payload.length + 2 * Nat.log 2 (payload.length + 1) + 3 := by
  have width : Nat.size payload.length ≤ Nat.log 2 (payload.length + 1) + 1 := by
    apply Nat.size_le.mpr
    exact (Nat.lt_succ_self payload.length).trans (Nat.lt_pow_succ_log_self (by decide) (payload.length + 1))
  have length := BitString.blen_e2 payload
  rw [BitString.blen_ofNat_eq_size] at length
  simp only [BitString.blen] at length
  change (BitString.e2 payload).length ≤ _
  omega

theorem every_nonempty_extension_is_refused (head payload suffix : BitString) (nonempty : suffix ≠ []) :
    frame.read (BitString.pair head (BitString.e2 payload) ++ suffix) = none :=
  frame.read_rejects_extension head payload suffix nonempty

end ICOriginalPrefixBridge

open Lean Elab Command

run_cmd do
  let env ← getEnv
  let allowed := #[``propext, ``Quot.sound, ``Classical.choice]
  let mut count := 0
  let mut names : Array Json := #[]
  for (name, info) in env.constants.toList do
    unless (`ICOriginalPrefixBridge).isPrefixOf name do continue
    if info.isUnsafe || info.isPartial then throwError "nonlogical prefix bridge declaration {name}"
    if info matches .axiomInfo _ then throwError "new prefix bridge axiom {name}"
    let dependencies ← liftCoreM <| collectAxioms name
    for dependency in dependencies do
      unless allowed.contains dependency do throwError "unpermitted prefix bridge dependency {name}: {dependency}"
    count := count + 1
    names := names.push (Json.mkObj [("name", toJson name.toString),
      ("axioms", toJson (dependencies.map Name.toString))])
  liftIO <| IO.println (Json.mkObj [("originalPrefixBridgeDeclarations", toJson count),
    ("auditedDeclarations", toJson names)]).compress
