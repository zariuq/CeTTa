import Metamath.Verify

open Metamath.Verify

def canonConstName : String → String
  | "(" => "lp"
  | ")" => "rp"
  | "|-" => "turn"
  | "->" => "impl"
  | "0" => "zero"
  | "+" => "plus"
  | "=" => "eq"
  | other => other

def objectLabel : String × Object → String
  | (_, .const c) => "const:" ++ canonConstName c
  | (_, .var v) => "var:" ++ v
  | (_, .hyp _ _ lbl) => "hyp:" ++ lbl
  | (_, .assert _ _ lbl) => "assert:" ++ lbl

def summaryLabels (db : DB) : List String :=
  ((db.objects.toList.map objectLabel).toArray.qsort (fun a b => a < b)).toList

def main (args : List String) : IO UInt32 := do
  let fname := match args with
    | file :: _ => file
    | [] => "set.mm"
  let db ← check fname
  match db.error? with
  | none =>
      IO.println s!"count\t{db.objects.size}"
      for label in summaryLabels db do
        IO.println s!"label\t{label}"
      pure 0
  | some ⟨Error.error pos err, _⟩ =>
      IO.eprintln s!"at {pos}: {err}"
      pure 1
  | some _ =>
      IO.eprintln "unexpected non-error verifier state"
      pure 1
