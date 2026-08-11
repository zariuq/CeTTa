import Mettapedia.Languages.MeTTa.Prime.MinimalCheckingPackage
import Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender
import MeTTailCore.Crypto.SHA256

/-!
# Export the Prime NIK calibration authority catalog

The catalog contains two deliberately different admitted rule packages and
one accepted proof for each.  It is data for CeTTa's generic checker
pipeline; neither package is privileged as Prime's unique type theory.
-/

namespace Cetta.Prime.NIK.AuthorityCatalogV1

open Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender
open Mettapedia.Languages.MeTTa.Prime.MinimalCheckingPackage

private def presentationDigest (rendered : String) : String :=
  MeTTailCore.Crypto.SHA256.sha256Hex rendered

private def authority (shortName system revision : String)
    (presentation goal proof : String) : String :=
  "  (authority " ++ shortName ++ " " ++ quote system ++ " " ++
    quote revision ++ " " ++ quote (presentationDigest presentation) ++
    "\n    " ++ presentation ++
    "\n    (positive " ++ goal ++ " " ++ proof ++ "))"

def catalog : String :=
  let dttPresentation := renderPresentation dttCache
  let hotgPresentation := renderPresentation hotgCache
  "(nik-authority-catalog-v1\n" ++
    authority "DTT" "prime.dtt.calibration" "1"
      dttPresentation (renderPattern dttIdZeroGoal)
      (renderRawProof dttProofIdZero) ++ "\n" ++
    authority "HOTG" "prime.hotg.calibration" "1"
      hotgPresentation (renderPattern hotgPowerGoal)
      (renderRawProof hotgProofPower) ++ "\n)\n"

def main (arguments : List String) : IO UInt32 := do
  match arguments with
  | [] =>
      IO.print catalog
      pure 0
  | [output] =>
      IO.FS.writeFile output catalog
      pure 0
  | _ =>
      IO.eprintln "usage: export_prime_nik_authority_catalog_v1 [OUTPUT]"
      pure 2

end Cetta.Prime.NIK.AuthorityCatalogV1

def main (arguments : List String) : IO UInt32 :=
  Cetta.Prime.NIK.AuthorityCatalogV1.main arguments
