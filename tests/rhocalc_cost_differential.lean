import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost.BudgetedDifferential

open Lean
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost

structure DifferentialBatch where
  preconditions : List (Option RuntimePreconditionChecks)
  outcomes : List CostWire.BudgetedPrefixOutcome
  deriving Lean.ToJson

def main (args : List String) : IO UInt32 := do
  match args with
  | ["--schema-example"] =>
      let request : CostWire.BudgetedPrefixRequest :=
        { schema := CostWire.budgetedCausalPrefixSchema
          fuel := 0
          searchFuel := 0
          term := CostWire.encodeTerm .nil }
      IO.println (toJson [request]).compress
      pure 0
  | [requestText] =>
      match Json.parse requestText >>= fromJson? with
      | .error message =>
          IO.eprintln s!"invalid cost-rho wire request: {message}"
          pure 2
      | .ok (requests : List CostWire.BudgetedPrefixRequest) =>
          let batch : DifferentialBatch :=
            { preconditions := requests.map CostWire.budgetedPrefixPreconditions
              outcomes := requests.map CostWire.evaluateBudgetedPrefix }
          IO.println (toJson batch).compress
          pure 0
  | _ =>
      IO.eprintln "usage: rhocalc_cost_differential <request-json> | --schema-example"
      pure 2
