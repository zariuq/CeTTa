import Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost.ReceiptReplayDifferential

open Lean
open Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost

structure ReceiptReplayBatch where
  outcomes : List CostWire.ReceiptReplayOutcome
  deriving Lean.ToJson

/-- The external CeTTa validation boundary admits only the runtime-supported
raw fragment.  The underlying replay theorem establishes path realizability;
this executable guard additionally rejects dangling binder indices before
that theorem is consulted. -/
def evaluateScopedRequest
    (request : CostWire.ReceiptReplayRequest) : CostWire.ReceiptReplayOutcome :=
  if request.schema != CostWire.receiptReplaySchema then
    .schemaMismatch
  else
    match CostWire.decodeTerm request.term with
    | none => .malformedSource
    | some term =>
        if term.supported then CostWire.evaluateReceiptReplay request
        else .rejected

def evaluateRequestText (requestText : String) : IO UInt32 := do
  match Json.parse requestText >>= fromJson? with
  | .error message =>
      IO.eprintln s!"invalid cost-rho receipt replay request: {message}"
      pure 2
  | .ok (requests : List CostWire.ReceiptReplayRequest) =>
      let batch : ReceiptReplayBatch :=
        { outcomes := requests.map evaluateScopedRequest }
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
