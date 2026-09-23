import Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.Declarations.NativeConvertedIntroductionComputation

/-!
The runtime library shares one computed package and reads both projections
from it. These candidate-syntax laws say the first component keeps its type
and the second component is checked at the family instantiated by the first
component of that same pair.
-/

open Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation
open Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.NativeJudgmentReplay
open ConvertedIntroductionComputation

set_option autoImplicit false

namespace Cetta.Prime.CertifiedTransformShare

theorem shared_package_components
    {n : Nat} {context : Tower.Ctx n} {contextCode : ContextCode n}
    {A first second : Tower.Tm n} {B : Tower.Tm (n + 1)} {pairCode : Code n}
    (fstAccepted : check context (Tm.pair first second).fst A contextCode
      (StructuralTypingReplay.Code.fstElim B pairCode) = true)
    (sndAccepted : check context (Tm.pair first second).snd
      (inst0 (Tm.pair first second).fst B) contextCode
      (StructuralTypingReplay.Code.sndElim A B pairCode) = true) :
    ∃ fstCode sndCode,
      betaSigmaFst contextCode A B first second pairCode = some fstCode ∧
      betaSigmaSnd contextCode A B first second pairCode = some sndCode ∧
      check context first A contextCode fstCode = true ∧
      check context second (inst0 (Tm.pair first second).fst B) contextCode sndCode = true := by
  obtain ⟨fstCode, fstComputed, fstChecked⟩ := betaSigmaFst_checked fstAccepted
  obtain ⟨sndCode, sndComputed, sndChecked⟩ := betaSigmaSnd_checked sndAccepted
  exact ⟨fstCode, sndCode, fstComputed, sndComputed, fstChecked, sndChecked⟩

#print axioms shared_package_components

end Cetta.Prime.CertifiedTransformShare
