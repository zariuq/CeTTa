import Mettapedia.Languages.VibeITP.Presentation.Authority
import Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender
import MeTTailCore.Crypto.SHA256

/-!
# Export the experimental Vibe-ITP authority catalog

The catalog holds one admitted rule package, the Vibe-ITP kernel rules, with
its calibration proof.  It is kept apart from the default NIK authority
catalog: the Vibe-ITP hosting is experimental (the certificate reader is
hand-written and the execution primitive is outside the hosted fragment), so
no default gate depends on it.
-/

namespace Cetta.Experiments.VibeItpNik.AuthorityCatalogV1

open Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

private def presentationDigest (rendered : String) : String :=
  MeTTailCore.Crypto.SHA256.sha256Hex rendered

private def authority (shortName system revision : String)
    (presentation goal proof : String) : String :=
  "  (authority " ++ shortName ++ " " ++ quote system ++ " " ++
    quote revision ++ " " ++ quote (presentationDigest presentation) ++
    "\n    " ++ presentation ++
    "\n    (positive " ++ goal ++ " " ++ proof ++ "))"

def catalog : String :=
  "(nik-authority-catalog-v1\n" ++
    authority "VIBE-ITP" "vibe-itp.kernel" "1"
      (renderDefinition Mettapedia.Languages.VibeITP.Presentation.kernelDefinition)
      (renderPattern Mettapedia.Languages.VibeITP.Presentation.calibrationGoal)
      (renderRawProof Mettapedia.Languages.VibeITP.Presentation.calibrationArticle) ++
      "\n)\n"

def main (arguments : List String) : IO UInt32 := do
  match arguments with
  | [] =>
      IO.print catalog
      pure 0
  | [output] =>
      IO.FS.writeFile output catalog
      pure 0
  | _ =>
      IO.eprintln "usage: export_vibe_itp_authority_catalog_v1 [OUTPUT]"
      pure 2

end Cetta.Experiments.VibeItpNik.AuthorityCatalogV1

def main (arguments : List String) : IO UInt32 :=
  Cetta.Experiments.VibeItpNik.AuthorityCatalogV1.main arguments
