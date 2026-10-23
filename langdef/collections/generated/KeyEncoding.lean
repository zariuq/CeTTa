import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
set_option maxRecDepth 4096
set_option maxHeartbeats 800000
#module_file "../KeyEncoding.module"
namespace CeTTaCollectionKeyEncoding
open Mettapedia.OSLF.MeTTaIL.Syntax
private def language : LanguageDef :=
  CollectionKeyEncoding.language.toOption.getD (LanguageDef.empty "invalid")
theorem source_loaded : CollectionKeyEncoding.language = .ok language := rfl
def rows : List (String × String × List String) := [
  ("CollectionOperation", "Operation", ["WireFactorize", "Keys"]),
  ("CollectionResult", "Result", ["WireResult", "Keys", "Codes", "FirstPositions"]),
  ("CollectionObservation", "Observe", ["FirstMatchingRepresentative", "ZeroBasedCodes", "SourceOrder", "DuplicateOccurrences", "MatchingCompatibleKeys", "CompleteValidation", "AuthoredFallback"])]
def sourceRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply label [Pattern.apply "Factorize" []] => some (label, equation.right)
    | _ => none
theorem rows_match_source : sourceRows = rows.map
    (fun (label, constructor, fields) =>
      (label, Pattern.apply constructor (fields.map fun field => Pattern.apply field []))) := by
  decide +kernel
#print axioms rows_match_source
end CeTTaCollectionKeyEncoding
