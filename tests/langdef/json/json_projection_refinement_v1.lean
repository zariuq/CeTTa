import Mettapedia.GSLT.LanguageDef.RFC8259ValueSemantics
import Mettapedia.GSLT.Dynamics.PartialRegionPlan

/-!
# JSON path projection as an observation-indexed partial region

This file fixes the semantic observer used by CeTTa's selective JSON parser.
The reference walks the occurrence-preserving value with `List.find?` and
`List.get?`.  The direct observer uses independent tail-recursive member and
index cursors, matching the native scanner's first-key and zero-based-index
policy.  Their equality establishes the value-side commuting edge needed by
the parser fast path; refinement of the C byte scanner remains native
conformance evidence, not a theorem in this file.
-/

set_option autoImplicit false

namespace Cetta.JSONProjectionRefinementV1

open Mettapedia.GSLT.LanguageDef.RFC8259ValueSemantics
open Mettapedia.GSLT.Dynamics.PartialRegionPlan

inductive PathStep where
  | key (scalars : List Nat)
  | index (offset : Nat)
  deriving DecidableEq, Repr

def referenceMember (wanted : List Nat) :
    List (JSONMember JSONValue) → Option JSONValue :=
  fun members =>
    (members.find? fun member => decide (member.key = wanted)).map
      JSONMember.value

def directMember (wanted : List Nat) :
    List (JSONMember JSONValue) → Option JSONValue
  | [] => none
  | member :: rest =>
      if member.key = wanted then some member.value
      else directMember wanted rest

theorem directMember_eq_referenceMember
    (wanted : List Nat) (members : List (JSONMember JSONValue)) :
    directMember wanted members = referenceMember wanted members := by
  induction members with
  | nil => rfl
  | cons member rest ih =>
      by_cases same : member.key = wanted
      · simp [directMember, referenceMember, same]
      · simp [directMember, referenceMember, same, ih]

def referenceIndex (offset : Nat) (values : List JSONValue) :
    Option JSONValue :=
  values[offset]?

def directIndex : Nat → List JSONValue → Option JSONValue
  | _, [] => none
  | 0, value :: _ => some value
  | offset + 1, _ :: rest => directIndex offset rest

theorem directIndex_eq_referenceIndex
    (offset : Nat) (values : List JSONValue) :
    directIndex offset values = referenceIndex offset values := by
  induction values generalizing offset with
  | nil => simp [directIndex, referenceIndex]
  | cons value rest ih =>
      cases offset with
      | zero => rfl
      | succ offset =>
          simpa [directIndex, referenceIndex] using ih offset

def referenceAt : JSONValue → List PathStep → Option JSONValue
  | value, [] => some value
  | .object members, .key wanted :: rest =>
      (referenceMember wanted members).bind fun value =>
        referenceAt value rest
  | .array values, .index offset :: rest =>
      (referenceIndex offset values).bind fun value =>
        referenceAt value rest
  | _, _ => none

def directAt : JSONValue → List PathStep → Option JSONValue
  | value, [] => some value
  | .object members, .key wanted :: rest =>
      (directMember wanted members).bind fun value =>
        directAt value rest
  | .array values, .index offset :: rest =>
      (directIndex offset values).bind fun value =>
        directAt value rest
  | _, _ => none

theorem directAt_eq_referenceAt
    (value : JSONValue) (path : List PathStep) :
    directAt value path = referenceAt value path := by
  induction path generalizing value with
  | nil => rfl
  | cons step rest ih =>
      cases step with
      | key wanted =>
          cases value <;>
            simp [directAt, referenceAt,
              directMember_eq_referenceMember, ih]
      | index offset =>
          cases value <;>
            simp [directAt, referenceAt,
              directIndex_eq_referenceIndex, ih]

inductive ProjectionExit where
  | missing
  | invalidUnicodeEscape
  | malformedCST
  deriving DecidableEq, Repr

def directRegion (path : List PathStep) :
    PartialRegion ElaborationOutcome JSONValue ProjectionExit
  | .value value =>
      match directAt value path with
      | some selected => some (.inl selected)
      | none => some (.inr .missing)
  | .invalidUnicodeEscape => some (.inr .invalidUnicodeEscape)
  | .malformedCST => some (.inr .malformedCST)

def referenceRegion (path : List PathStep) :
    PartialRegion ElaborationOutcome JSONValue ProjectionExit
  | .value value =>
      match referenceAt value path with
      | some selected => some (.inl selected)
      | none => some (.inr .missing)
  | .invalidUnicodeEscape => some (.inr .invalidUnicodeEscape)
  | .malformedCST => some (.inr .malformedCST)

/-- The direct cursor and the ordinary value observer denote exactly the same
partial deterministic region.  Consequently the existing partial-region
normalization/fusion laws may replace one by the other without changing the
selected value or any visible failure. -/
theorem directRegion_eq_referenceRegion (path : List PathStep) :
    directRegion path = referenceRegion path := by
  funext outcome
  cases outcome with
  | value value =>
      simp only [directRegion, referenceRegion]
      rw [directAt_eq_referenceAt]
  | invalidUnicodeEscape => rfl
  | malformedCST => rfl

/-- Instantiation at the authored CST/value boundary.  This theorem does not
identify a C execution with Lean; it says exactly which semantic equality a
native projection implementation must establish. -/
theorem projection_commutes_after_elaboration
    (tree : Mettapedia.GSLT.Parsing.PresentationExprSemantics.CST)
    (path : List PathStep) :
    directRegion path (elaborateRootCST tree) =
      referenceRegion path (elaborateRootCST tree) := by
  rw [directRegion_eq_referenceRegion]

/-- The same commuting edge for an actual ParserPack root occurrence.  The
derivation certifies that `occurrence.1` parses the supplied input; the
existing ParserPack/source/native equivalences can transport that occurrence
without changing this projection result. -/
theorem parserOccurrence_projection_commutes
    {profile : Mettapedia.GSLT.Parsing.ParserProfileSemantics.ParserProfileLayer}
    {plan : Mettapedia.GSLT.Parsing.ClassAwareParserPackCorrespondence.CompiledParserPackPlan}
    {input : List Nat}
    (occurrence : ParserRootOccurrence profile plan input)
    (path : List PathStep) :
    directRegion path (elaborateRootCST occurrence.1) =
      referenceRegion path (elaborateRootCST occurrence.1) := by
  exact projection_commutes_after_elaboration occurrence.1 path

private def duplicateMembers : List (JSONMember JSONValue) := [
  { occurrence := 0, key := [120], value := .number [49],
    span := { start := 1, stop := 6 } },
  { occurrence := 1, key := [120], value := .number [50],
    span := { start := 7, stop := 12 } }
]

theorem duplicate_key_selects_first_occurrence :
    directAt (.object duplicateMembers) [.key [120]] =
      some (.number [49]) := by
  rfl

theorem wrong_container_kind_is_missing :
    directAt (.array [.null]) [.key [120]] = none := by
  rfl

#print axioms directMember_eq_referenceMember
#print axioms directIndex_eq_referenceIndex
#print axioms directAt_eq_referenceAt
#print axioms directRegion_eq_referenceRegion
#print axioms projection_commutes_after_elaboration
#print axioms parserOccurrence_projection_commutes
#print axioms duplicate_key_selects_first_occurrence
#print axioms wrong_container_kind_is_missing

end Cetta.JSONProjectionRefinementV1
