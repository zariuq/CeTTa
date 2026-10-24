import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

set_option maxRecDepth 4096
set_option maxHeartbeats 800000

#module_file "../Descriptors.module"

namespace CeTTaPostingsDescriptors
open Mettapedia.OSLF.MeTTaIL.Syntax

private def language : LanguageDef :=
  PostingsDescriptors.language.toOption.getD (LanguageDef.empty "invalid")

theorem source_loaded : PostingsDescriptors.language = .ok language := rfl

def countModeRows : List (String × String × String × String × String × String) := [
  ("Before", "WireBefore", "before", "Index", "Prefix", "Rows"),
  ("All", "WireAll", "all", "Index", "Complete", "Rows"),
  ("TensorBefore", "WireTensorBefore", "tensor-before", "Index", "Prefix", "Tensor"),
  ("TensorAll", "WireTensorAll", "tensor-all", "Index", "Complete", "Tensor"),
  ("TableBefore", "WireTableBefore", "table-before", "Table", "Prefix", "Rows"),
  ("TableAll", "WireTableAll", "table-all", "Table", "Complete", "Rows"),
  ("TableTensorBefore", "WireTableTensorBefore", "table-tensor-before", "Table", "Prefix", "Tensor"),
  ("TableTensorAll", "WireTableTensorAll", "table-tensor-all", "Table", "Complete", "Tensor"),
  ("TableFoldBefore", "WireTableFoldBefore", "table-fold-before", "Table", "Prefix", "FoldRows"),
  ("TableFoldColumnsBefore", "WireTableFoldColumnsBefore", "table-fold-columns-before", "Table", "Prefix", "FoldColumns"),
  ("Stats", "WireStats", "stats", "Either", "Metadata", "Statistics")
]

def sourceCountModeRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "PostingsCountMode" [Pattern.apply label []] =>
        some (label, equation.right)
    | _ => none

def encodedCountModeRows : List (String × Pattern) :=
  countModeRows.map fun (label, wireName, _wire, family, extent, result) =>
    (label, Pattern.apply "CountMode" [Pattern.apply wireName [],
      Pattern.apply family [], Pattern.apply extent [], Pattern.apply result []])

theorem count_mode_rows_match_source :
    sourceCountModeRows = encodedCountModeRows := by
  decide +kernel

#print axioms count_mode_rows_match_source

def observationRows : List (String × List String) := [
  ("PrepareIndex", ["SourceOccurrenceOrder", "DuplicateOccurrences", "NumericPositionsAscending", "CompleteValidation", "AtomicPublication", "OwnedSnapshot", "AuthoredFallback"]),
  ("PrepareTable", ["MatchingCompatibleKeys", "FirstOuterKeyOrder", "DuplicateOccurrences", "NumericGroupOrder", "NumericPositionsAscending", "CompleteValidation", "AtomicPublication", "OwnedSnapshot", "AuthoredFallback"]),
  ("PrepareSpaceTableAttempt", ["BorrowedSource", "DeclaredProjection", "MatchingCompatibleKeys", "ExactRowWidth", "ProtectedStandaloneSource", "FirstOuterKeyOrder", "DuplicateOccurrences", "NumericGroupOrder", "NumericPositionsAscending", "CompleteValidation", "AtomicPublication", "OwnedSnapshot", "AuthoredFallback"]),
  ("Inspect", ["SourceOccurrenceOrder", "DuplicateOccurrences", "OwnedSnapshot"]),
  ("Counts", ["DuplicateOccurrences", "NumericPositionsAscending", "AuthoredFallback"]),
  ("FoldTable", ["QueryOrder", "NumericGroupOrder", "MatchingCompatibleKeys", "ProjectedGroupIdentity", "OriginalGroupCallback", "IndependentGroupStates", "ExpressionState", "SingleAnswerNumericState", "CompleteValidation", "AtomicPublication", "AuthoredFallback"]),
  ("FoldColumns", ["QueryOrder", "NumericGroupOrder", "MatchingCompatibleKeys", "ProjectedGroupIdentity", "OriginalGroupCallback", "IndependentGroupStates", "ExpressionState", "SingleAnswerNumericState", "FirstGroupEncounterOrder", "DeclaredLeafDTypes", "DenseColumns", "CompleteValidation", "AtomicPublication", "AuthoredFallback"]),
  ("SourcesCurrent", ["ExactSourceRevision", "CompleteValidation", "AuthoredFallback"]),
  ("Close", ["CloseInvalidatesAliases"])
]

def sourceObservationRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "PostingsObservation" [Pattern.apply label []] =>
        some (label, equation.right)
    | _ => none

def encodedObservationRows : List (String × Pattern) :=
  observationRows.map fun (label, policies) =>
    (label, Pattern.apply "Observe"
      (policies.map fun policy => Pattern.apply policy []))

theorem observation_rows_match_source :
    sourceObservationRows = encodedObservationRows := by
  decide +kernel

#print axioms observation_rows_match_source

def blockProjectionFields : List String := ["RowHead", "RowWidth", "KeysField", "GroupField", "PositionField", "PrefixKeys", "ForbiddenHeads"]

def sourceBlockProjection : List Pattern :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "PostingsProjection" [Pattern.apply "BlockLayout" []] =>
        some equation.right
    | _ => none

theorem block_projection_fields_match_source :
    sourceBlockProjection = [Pattern.apply "Projection"
      (Pattern.apply "WireBlockLayout" [] ::
        blockProjectionFields.map (fun field => Pattern.apply field []))] := by
  decide +kernel

#print axioms block_projection_fields_match_source

def columnsLayoutFields : List String := ["LabelsColumn", "ValueColumns"]

def columnTypeRows : List (String × String × String × String) := [
  ("I64", "WireI64", "i64", "IntegerLeaf"),
  ("F64", "WireF64", "f64", "FloatingLeaf")
]

def sourceColumnTypeRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "PostingsColumnDType" [Pattern.apply label []] =>
        some (label, equation.right)
    | _ => none

theorem column_types_match_source :
    sourceColumnTypeRows = columnTypeRows.map (fun (label, wireName, _wire, leaf) =>
      (label, Pattern.apply "ColumnDType" [Pattern.apply wireName [], Pattern.apply leaf []])) := by
  decide +kernel

def sourceColumnsProjection : List Pattern :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply "PostingsProjection" [Pattern.apply "ColumnsLayout" []] =>
        some equation.right
    | _ => none

theorem columns_projection_fields_match_source :
    sourceColumnsProjection = [Pattern.apply "Projection"
      (Pattern.apply "WireColumns" [] ::
        columnsLayoutFields.map (fun field => Pattern.apply field []))] := by
  decide +kernel

#print axioms column_types_match_source
#print axioms columns_projection_fields_match_source

end CeTTaPostingsDescriptors
