import Mettapedia.GSLT.LanguageDef.IndexedInductiveAppendCompletion

/-!
Executable controls for the actual recursive-body admission and call
receipts. This driver runs the Lean model; it makes no C refinement claim.
-/

namespace PrimeDefinitionCompletionControls

open Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation
open Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId
open NativeNaturalVectorFamilies AppendDefinitionCompletion

local instance : DecidableRel AppendDefinitionCompletion.rules.headEq :=
  fun h u => inferInstanceAs (Decidable (Tower.rules.headEq h u))
local instance : DecidableRel Programs.rules.headEq :=
  fun h u => inferInstanceAs (Decidable (Tower.rules.headEq h u))

def emptyCall : StructuralConversionCode.Code Tower.Head decoder.Code 0 := callEvidence natTm zeroTm zeroTm (vnilApp natTm) (vnilApp natTm)

def controls : List (String × Bool) :=
  Mettapedia.GSLT.LanguageDef.IndexedInductive.AppendSourceCompletion.verdicts ++ [
    ("completed call receipt", emptyCall.check AppendDefinitionCompletion.rules.headEq decoder.decode
      (call natTm zeroTm zeroTm (vnilApp natTm) (vnilApp natTm))
      (Programs.append natTm zeroTm zeroTm (vnilApp natTm) (vnilApp natTm))),
    ("old call receipt refuses changed body", !emptyCall.check Programs.rules.headEq changedDecoder.decode
      (call natTm zeroTm zeroTm (vnilApp natTm) (vnilApp natTm))
      (Programs.append natTm zeroTm zeroTm (vnilApp natTm) (vnilApp natTm))),
    ("old endpoint refusal", !unfolding.check AppendDefinitionCompletion.rules.headEq decoder.decode
      (.const name) replacement),
    ("new body changes endpoint verdict", unfolding.check AppendDefinitionCompletion.rules.headEq changedDecoder.decode
      (.const name) replacement)]

set_option maxRecDepth 10000 in
set_option maxHeartbeats 2000000 in
theorem all_controls : controls.all Prod.snd = true := by decide +kernel

def main : IO UInt32 := do
  for (label, passed) in controls do
    IO.println s!"{if passed then "PASS" else "FAIL"}: {label}"
  pure (if controls.all Prod.snd then 0 else 1)

#print axioms all_controls

end PrimeDefinitionCompletionControls

def main : IO UInt32 := PrimeDefinitionCompletionControls.main
