import Mettapedia.Languages.Megalodon.EnvironmentKernel
import Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

namespace Cetta.Prime.NIK.MegalodonPolymorphicV1

open Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

def witness : String :=
  "(nik-megalodon-polymorphic-witness-v1\n  " ++
    renderPattern
      Mettapedia.Languages.Megalodon.EnvironmentKernel.polymorphicReuseDocumentGoal ++
    "\n  " ++
    renderRawProof
      Mettapedia.Languages.Megalodon.EnvironmentKernel.polymorphicReuseDocumentArticle ++
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
        "usage: export_prime_nik_megalodon_polymorphic_v1 [OUTPUT]"
      pure 2

end Cetta.Prime.NIK.MegalodonPolymorphicV1

def main (arguments : List String) : IO UInt32 :=
  Cetta.Prime.NIK.MegalodonPolymorphicV1.main arguments
