import Mettapedia.Languages.Megalodon.DefinitionConversionWireRefinement
import Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

namespace Cetta.Prime.NIK.MegalodonDefinitionV1

open Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

def witness : String :=
  "(nik-megalodon-definition-witness-v1\n  " ++
    renderPattern
      Mettapedia.Languages.Megalodon.DefinitionConversionKernel.definitionIdentityGoal ++
    "\n  " ++
    renderRawProof
      Mettapedia.Languages.Megalodon.DefinitionConversionKernel.definitionIdentityArticle ++
    ")\n"

def main (arguments : List String) : IO UInt32 := do
  match arguments with
  | [] =>
      IO.print witness
      pure 0
  | [output] =>
      IO.FS.writeFile output witness
      pure 0
  | _ =>
      IO.eprintln
        "usage: export_prime_nik_megalodon_definition_v1 [OUTPUT]"
      pure 2

end Cetta.Prime.NIK.MegalodonDefinitionV1

def main (arguments : List String) : IO UInt32 :=
  Cetta.Prime.NIK.MegalodonDefinitionV1.main arguments
