import Algorithms.MeTTa.Simple.Parser

open MeTTailCore.MeTTaIL.Syntax

private def hexDigit (n : UInt8) : Char :=
  if n < 10 then Char.ofNat (48 + n.toNat)
  else Char.ofNat (87 + n.toNat)

private def byteToHex (byte : UInt8) : String :=
  String.ofList [hexDigit (byte / 16), hexDigit (byte % 16)]

private def encodeHex (value : String) : String :=
  value.toUTF8.foldl (fun result byte => result ++ byteToHex byte) ""

private def emitResult (path : String) : IO Unit := do
  let input ← IO.FS.readFile path
  match Algorithms.MeTTa.Simple.Parser.parseExprWith
      MeTTailCore.MeTTaSyntax.he input with
  | .ok (.apply name []) =>
      IO.println s!"accepted\t{encodeHex name}"
  | .ok pattern =>
      IO.println s!"accepted-other\t{encodeHex (reprStr pattern)}"
  | .error error =>
      IO.println s!"rejected\t{encodeHex error}"

def main (arguments : List String) : IO UInt32 := do
  IO.println "leatta-parser-oracle-v1"
  for path in arguments do
    emitResult path
  IO.println "end"
  return 0
