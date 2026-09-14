import Mettapedia.GSLT.Parsing.IntegerProviderNativeTypeExport

/-!
# Source-derived integer completion-type export

Pass source files in their actual composition order. With `--self-test`, the
same live sources exercise serialization, relation renaming, duplicate
occurrences, source mutation, and false type metadata. No provider-name list
is maintained here. The source syntax in the packet is compile-time input;
it is not an emitted runtime rule table.
-/

namespace Cetta.PlainBnf.IntegerProviderNativeTypeExportV1

open Algorithms.MeTTa.Simple.Parser (SExpr)
open Mettapedia.GSLT.LanguageDef.CanonicalSourceGSLT
open Mettapedia.GSLT.Parsing.IntegerProviderNativeTypeCodec
open Mettapedia.GSLT.Parsing.IntegerProviderNativeTypeExport
open Mettapedia.GSLT.Parsing.SourceIntegerProviderNativeType (BinaryTypeInfo)

mutual

private def replaceAtoms (replace : String → String) : SExpr → SExpr
  | .atom token => .atom (replace token)
  | .list elements => .list (replaceElements replace elements)
termination_by expression => sizeOf expression

private def replaceElements (replace : String → String) : List SExpr → List SExpr
  | [] => []
  | head :: tail => replaceAtoms replace head :: replaceElements replace tail
termination_by elements => sizeOf elements

end

private def duplicateAt (occurrence : Nat) : List Source → Option (List Source)
  | [] => none
  | source :: tail =>
      if occurrence < source.rewrites.length then do
        let row ← source.rewrites[occurrence]?
        let duplicate := { row with name := row.name ++ "-duplicate-native-type-control" }
        return { source with rewrites := source.rewrites.take occurrence ++ [row, duplicate] ++
          source.rewrites.drop (occurrence + 1) } :: tail
      else do
        let rest ← duplicateAt (occurrence - source.rewrites.length) tail
        return source :: rest

private def changeAt (change : Rewrite → Rewrite) (occurrence : Nat) :
    List Source → Option (List Source)
  | [] => none
  | source :: tail =>
      if occurrence < source.rewrites.length then do
        let row ← source.rewrites[occurrence]?
        return { source with rewrites := source.rewrites.take occurrence ++ [change row] ++
          source.rewrites.drop (occurrence + 1) } :: tail
      else do
        let rest ← changeAt change (occurrence - source.rewrites.length) tail
        return source :: rest

private def assertCheck (name : String) (condition : Bool) : IO Unit := do
  if !condition then throw (IO.userError s!"FAILED: {name}")
  (← IO.getStderr).putStrLn s!"PASS: {name}"

private def requirePacket (sources : List SExpr) : IO Packet := do
  match inferPacket? sources with
  | none => throw (IO.userError "source composition did not decode as a valid GSLT composition")
  | some packet => return packet

private def selfTest (paths : List String) : IO UInt32 := do
  try
    let rawSources ← readSources paths
    let packet ← requirePacket rawSources
    let some first := packet.types.head?
      | throw (IO.userError "self-test needs a source composition with an inferred binary provider")
    assertCheck "computed packet authenticates against live ordered sources" (authenticate rawSources packet)
    assertCheck "structured packet roundtrip" (decodePacket (encodePacket packet) == some packet)
    assertCheck "noncanonical occurrence spelling is refused"
      ((decodeInfo (.list [.atom "binary-integer-completion-v1", .atom "01",
        .atom first.relation, .atom "less", .atom "direct"])).isNone)
    let .ok text := checkedText packet
      | throw (IO.userError "rendered packet failed its physical readback")
    let .ok readback := parseSource text
      | throw (IO.userError "cannot read rendered packet")
    assertCheck "rendered MeTTa decodes to the exact computed packet" (decodePacket readback == some packet)

    let renamed := first.relation ++ "-renamed-native-type-control"
    let rename := fun token =>
      if token = first.relation then renamed
      else if token = "gslt:" ++ first.relation then "gslt:" ++ renamed else token
    let renamedSources := rawSources.map (replaceAtoms rename)
    let renamedPacket ← requirePacket renamedSources
    let expectedRenamed := packet.types.map fun info =>
      if info.relation = first.relation then { info with relation := renamed } else info
    assertCheck "renaming is inferred without a provider registry" (renamedPacket.types == expectedRenamed)
    assertCheck "old source binding refuses renamed composition" (!authenticate renamedSources packet)

    let some decodedSources := decodeList decode rawSources
      | throw (IO.userError "source composition did not decode")
    let some sourceHead := decodedSources.head?
      | throw (IO.userError "self-test needs a nonempty source composition")
    let equationalSources :=
      { sourceHead with equations := [.atom "nonempty-equation-section-control"] } :: decodedSources.drop 1
    assertCheck "uninterpreted source equations are refused, not erased"
      ((inferPacket? (equationalSources.map encode)).isNone)
    let some names := Mettapedia.GSLT.Parsing.SourceIntegerProviderNativeType.inputNamesAt?
        decodedSources first.occurrence
      | throw (IO.userError "inferred source variable names missing")
    let renameVariables := fun token =>
      if token = names.1 then "?renamed-first-native-type-control"
      else if token = names.2 then "?renamed-second-native-type-control" else token
    let renamedVariables ← requirePacket (rawSources.map (replaceAtoms renameVariables))
    assertCheck "source variable renaming retains inferred types"
      (renamedVariables.types == packet.types)

    let some repeatedInput := changeAt (fun row =>
        { row with head := replaceAtoms (fun token => if token = names.2 then names.1 else token) row.head })
        first.occurrence decodedSources
      | throw (IO.userError "cannot construct repeated-input control")
    let repeatedInputPacket ← requirePacket (repeatedInput.map encode)
    assertCheck "two arguments sharing one source variable lose the binary type"
      (repeatedInputPacket.types == packet.types.filter (fun info => info.occurrence != first.occurrence))

    let some nonemptyBody := changeAt (fun row => { row with body := [row.head] })
        first.occurrence decodedSources
      | throw (IO.userError "cannot construct nonempty-body control")
    let nonemptyBodyPacket ← requirePacket (nonemptyBody.map encode)
    assertCheck "an added premise is not silently treated as a body-free provider"
      (nonemptyBodyPacket.types == packet.types.filter (fun info => info.occurrence != first.occurrence))

    let some wrongEcho := changeAt (fun row => { row with head := match row.head with
        | .list [tag, lhs, .list [condition, guard, .list [quoteTag,
            .list [relation, firstInput, secondInput]], no]] =>
          .list [tag, lhs, .list [condition, guard, .list [quoteTag,
            .list [relation, secondInput, firstInput]], no]]
        | other => other }) first.occurrence decodedSources
      | throw (IO.userError "cannot construct wrong-echo control")
    let wrongEchoPacket ← requirePacket (wrongEcho.map encode)
    assertCheck "swapped quote arguments lose the input-echo type"
      (wrongEchoPacket.types == packet.types.filter (fun info => info.occurrence != first.occurrence))

    let some duplicatedSources := duplicateAt first.occurrence decodedSources
      | throw (IO.userError "inferred occurrence missing from decoded source")
    let duplicatedPacket ← requirePacket (duplicatedSources.map encode)
    let expectedDuplicated := packet.types.flatMap fun info =>
      if info.occurrence < first.occurrence then [info]
      else if info.occurrence = first.occurrence then
        [info, { info with occurrence := info.occurrence + 1 }]
      else [{ info with occurrence := info.occurrence + 1 }]
    assertCheck "duplicate equations retain both indexed occurrences"
      (duplicatedPacket.types == expectedDuplicated && duplicatedPacket.types.length == packet.types.length + 1)

    let falseInfo := { first with notLess := !first.notLess }
    let falsePacket := { packet with types := falseInfo :: packet.types.drop 1 }
    assertCheck "false polarity metadata is refused" (!authenticate rawSources falsePacket)
    let dropped := { packet with types := packet.types.drop 1 }
    assertCheck "missing inferred occurrence is refused" (!authenticate rawSources dropped)

    let mutatedSources := decodedSources.map fun source =>
      { source with rewrites := source.rewrites.map fun row =>
        { row with head := replaceAtoms (fun token => if token = "1" then "1.0" else token) row.head } }
    let mutatedRaw := mutatedSources.map encode
    assertCheck "integer and float source spellings are not conflated" (mutatedRaw != rawSources)
    assertCheck "integer-to-float mutation invalidates old source binding" (!authenticate mutatedRaw packet)
    let mutPacket ← requirePacket mutatedRaw
    assertCheck "integer-to-float mutation changes structurally inferred family" (mutPacket.types != packet.types)
    (← IO.getStderr).putStrLn s!"source units: {rawSources.length}; inferred occurrences: {packet.types.length}"
    return 0
  catch error =>
    (← IO.getStderr).putStrLn error.toString
    return 1

end Cetta.PlainBnf.IntegerProviderNativeTypeExportV1

def main (arguments : List String) : IO UInt32 :=
  match arguments with
  | "--self-test" :: paths => Cetta.PlainBnf.IntegerProviderNativeTypeExportV1.selfTest paths
  | paths => Mettapedia.GSLT.Parsing.IntegerProviderNativeTypeExport.run paths
