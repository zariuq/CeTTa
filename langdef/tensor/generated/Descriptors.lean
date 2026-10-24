import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
import Mathlib.Data.List.Pairwise

open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

set_option maxRecDepth 4096
set_option maxHeartbeats 800000

#module_file "../Descriptors.module"

namespace CeTTaTensorDescriptors
open Mettapedia.OSLF.MeTTaIL.Syntax

inductive DType where
  | i64 | f64 | u8 | f32 | bool | index
  deriving DecidableEq, Repr

def code : DType → Nat
  | .i64 => 1
  | .f64 => 2
  | .u8 => 3
  | .f32 => 4
  | .bool => 5
  | .index => 6

def byteWidth : DType → Nat
  | .i64 => 8
  | .f64 => 8
  | .u8 => 1
  | .f32 => 4
  | .bool => 1
  | .index => 8

theorem byteWidth_positive (dtype : DType) : 0 < byteWidth dtype := by
  cases dtype <;> decide

theorem code_injective : Function.Injective code := by
  intro a b h; cases a <;> cases b <;> simp_all [code]

def exactPromotion : DType → DType → Bool
  | .i64, .i64 => true
  | .f64, .f64 => true
  | .u8, .i64 => true
  | .u8, .f64 => true
  | .u8, .u8 => true
  | .u8, .f32 => true
  | .u8, .index => true
  | .f32, .f64 => true
  | .f32, .f32 => true
  | .bool, .i64 => true
  | .bool, .f64 => true
  | .bool, .u8 => true
  | .bool, .f32 => true
  | .bool, .bool => true
  | .bool, .index => true
  | .index, .index => true
  | _, _ => false

theorem exactPromotion_reflexive (dtype : DType) : exactPromotion dtype dtype = true := by
  cases dtype <;> rfl

def scalarRows : List (String × Nat × Nat × String × Nat) := [
  ("i64", 1, 8, "Signed", 64),
  ("f64", 2, 8, "Floating", 53),
  ("u8", 3, 1, "Unsigned", 8),
  ("f32", 4, 4, "Floating", 24),
  ("bool", 5, 1, "Boolean", 1),
  ("index", 6, 8, "Unsigned", 64)
]

private def language : LanguageDef :=
  TensorDescriptors.language.toOption.getD (LanguageDef.empty "invalid")

theorem source_loaded : TensorDescriptors.language = .ok language := rfl

def sourceScalarRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "TensorDType" [Pattern.apply label []] => some (label, equation.right)
    | _ => none

def encodedScalarRows : List (String × Pattern) :=
  scalarRows.map fun (label, wireCode, width, kind, mantissa) =>
    (label, Pattern.apply "ScalarLayout" [
      Pattern.apply ("Code" ++ toString wireCode) [], Pattern.apply ("Bytes" ++ toString width) [],
      Pattern.apply kind [], Pattern.apply ("Precision" ++ toString mantissa) []])

theorem scalar_rows_match_source : sourceScalarRows = encodedScalarRows := by
  decide +kernel

#print axioms scalar_rows_match_source

def observationRows : List (String × List String) := [
  ("equality", ["DTypeShapeSupportScalarBits"]),
  ("sum", ["StoredCoordinateOrder", "F64AccumulateFloats"]),
  ("matvec", ["StoredRowCoordinateOrder", "F64AccumulateFloats"]),
  ("fixed4", ["FourLanesLeftAssociated", "ExplicitSelection"]),
  ("topk", ["FiniteEnabledDescending", "CoordinateAscendingTies"]),
  ("entries", ["CoordinateAscending", "PackedCoordinateValueVectors"]),
  ("gather", ["IndexPullbackSupport", "BoundsChecked"]),
  ("merge", ["OccurrenceConcatenate", "ExplicitDuplicatePolicy"]),
  ("zipmap", ["EqualShapeSupport", "CapturedValueCall", "CoordinateAscending", "AtomicFallback", "COOResult"]),
  ("screen", ["OwnerQueryMaskKBound", "NearestGradualSequential", "InclusiveThreshold", "CoordinateAscendingTies", "ExactRerank", "FullScanFallback"])
]

def sourceObservationRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "TensorObservation" [Pattern.apply label []] => some (label, equation.right)
    | _ => none

def encodedObservationRows : List (String × Pattern) :=
  observationRows.map fun (label, policies) =>
    (label, Pattern.apply "Observe" (policies.map fun policy => Pattern.apply policy []))

theorem observation_rows_match_source : sourceObservationRows = encodedObservationRows := by
  decide +kernel

#print axioms observation_rows_match_source

def callbackInputRows : List (String × String) := [
  ("i64", "Signed64Atom"),
  ("f64", "Float64Atom"),
  ("u8", "Unsigned8Atom"),
  ("f32", "Float32Atom"),
  ("index", "Unsigned64Atom")
]

def sourceCallbackInputRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "TensorCallbackInput" [Pattern.apply label []] => some (label, equation.right)
    | _ => none

def encodedCallbackInputRows : List (String × Pattern) :=
  callbackInputRows.map fun (label, encoding) =>
    (label, Pattern.apply "ScalarAtomInput" [Pattern.apply encoding []])

theorem callback_input_rows_match_source : sourceCallbackInputRows = encodedCallbackInputRows := by
  decide +kernel

#print axioms callback_input_rows_match_source

end CeTTaTensorDescriptors
