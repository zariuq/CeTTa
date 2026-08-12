import Mettapedia.Languages.Megalodon.TermQuantifiedKernel
import Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

namespace Cetta.Prime.NIK.MegalodonTermV1

open Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender

def witness : String :=
  "(nik-megalodon-term-witness-v1\n  " ++
    renderPattern Mettapedia.Languages.Megalodon.TermQuantifiedKernel.goal ++
    "\n  " ++
    renderRawProof Mettapedia.Languages.Megalodon.TermQuantifiedKernel.article ++
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
      IO.eprintln "usage: export_prime_nik_megalodon_term_v1 [OUTPUT]"
      pure 2

end Cetta.Prime.NIK.MegalodonTermV1

def main (arguments : List String) : IO UInt32 :=
  Cetta.Prime.NIK.MegalodonTermV1.main arguments
