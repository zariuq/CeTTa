import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Costed.Differential

open Lean
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Costed

def main (args : List String) : IO UInt32 := do
  match args with
  | ["--schema-example"] =>
      let request : CostWire.PrefixRequest :=
        { schema := CostWire.causalPrefixSchema
          fuel := 0
          term := CostWire.encodeTerm .nil }
      IO.println (toJson [request]).compress
      pure 0
  | [requestText] =>
      match Json.parse requestText >>= fromJson? with
      | .error message =>
          IO.eprintln s!"invalid cost-rho wire request: {message}"
          pure 2
      | .ok (requests : List CostWire.PrefixRequest) =>
          IO.println (toJson (requests.map CostWire.evaluatePrefix)).compress
          pure 0
  | _ =>
      IO.eprintln "usage: rhocalc_cost_differential <request-json> | --schema-example"
      pure 2
