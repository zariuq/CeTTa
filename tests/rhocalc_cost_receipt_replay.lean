import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Costed.ReceiptReplayDifferential

open Lean
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Costed

structure ReceiptReplayBatch where
  outcomes : List CostWire.ReceiptReplayOutcome
  deriving Lean.ToJson

def evaluateRequestText (requestText : String) : IO UInt32 := do
  match Json.parse requestText >>= fromJson? with
  | .error message =>
      IO.eprintln s!"invalid cost-rho receipt replay request: {message}"
      pure 2
  | .ok (requests : List CostWire.ReceiptReplayRequest) =>
      let batch : ReceiptReplayBatch :=
        { outcomes := requests.map CostWire.evaluateReceiptReplay }
      IO.println (toJson batch).compress
      pure 0

def main (args : List String) : IO UInt32 := do
  match args with
  | ["--stdin"] =>
      let input ← IO.getStdin
      evaluateRequestText (← input.readToEnd)
  | [requestText] => evaluateRequestText requestText
  | _ =>
      IO.eprintln
        "usage: rhocalc_cost_receipt_replay <request-json> | --stdin"
      pure 2
