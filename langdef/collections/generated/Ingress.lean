import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
set_option maxRecDepth 4096
set_option maxHeartbeats 800000
#module_file "../Ingress.module"

namespace CeTTaCollectionIngress
open Mettapedia.OSLF.MeTTaIL.Syntax
private def language : LanguageDef :=
  CollectionIngress.language.toOption.getD (LanguageDef.empty "invalid")
theorem source_loaded : CollectionIngress.language = .ok language := rfl

def sourceRows (head : String) : List Pattern :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply label [Pattern.apply "Append" []] =>
        if label = head then some equation.right else none
    | _ => none

def fields : List String := ["WireAppend", "Destination", "Collection"]
def policies : List String := ["SourceOrder", "DuplicateOccurrences", "PrivateBinding", "PureProducer", "ReadyInputs", "EagerProjection", "CompleteValidation", "DataOnly", "AtomicPublication", "AuthoredFallback"]
theorem consumer_matches_source :
    sourceRows "CollectionConsumer" =
      [Pattern.apply "Consumer" (fields.map (fun label => Pattern.apply label []))] := by
  decide +kernel
theorem observation_matches_source :
    sourceRows "CollectionObservation" =
      [Pattern.apply "Observe" (policies.map (fun label => Pattern.apply label []))] := by
  decide +kernel
#print axioms consumer_matches_source
#print axioms observation_matches_source
end CeTTaCollectionIngress
